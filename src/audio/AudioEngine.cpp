#include "audio/AudioEngine.h"

#include <esp_heap_caps.h>
#include <esp_log.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>

#include "audio/AudioFiles.h"
#include "audiobook/AacDecoder.h"
#include "board/BoardAudio.h"
#include "board/BoardStorage.h"
#include "storage/fs/StorageFiles.h"

namespace audio {
    namespace {

        constexpr char kTag[] = "audio";
        constexpr uint32_t kStackBytes = 12 * 1024;
        constexpr UBaseType_t kPriority = 5;
        constexpr BaseType_t kCore = 0;
        constexpr UBaseType_t kQueueLength = 8;
        constexpr uint32_t kWriteTimeoutMs = 1000;
        constexpr uint32_t kReadTimeoutMs = 200;
        constexpr uint32_t kPublishIntervalMs = 250;
        constexpr uint32_t kAutosaveIntervalMs = 30'000;
        constexpr uint32_t kHeaderRefreshMs = 5'000;
        constexpr uint64_t kMaxRecordingMs = 3ULL * 60 * 60 * 1000;
        constexpr uint64_t kChapterRestartMs = 3'000;
        constexpr size_t kFrameBytes = 4096;
        constexpr size_t kPcmSamples = audiobook::AacDecoder::kMaxFrameSamples;
        constexpr size_t kMemoFrames = 512;

        void* allocate(size_t bytes) {
            void* memory = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            return memory != nullptr ? memory : heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
        }

        struct Free {
            void operator()(void* memory) const {
                heap_caps_free(memory);
            }
        };

        // SD file reads through two cached windows, so sample-table lookups and audio frames do not evict
        // each other on every frame.
        class FileSource final : public audiobook::ByteSource {
        public:
            bool open(const char* path) {
                close();
                file_ = Board::Storage::filesystem().open(path, FILE_READ);
                if (!file_)
                    return false;
                size_ = file_.size();
                for (Slot& slot: slots_) {
                    if (!slot.data)
                        slot.data.reset(static_cast<uint8_t*>(allocate(kSlotBytes)));
                    slot.length = 0;
                    slot.start = 0;
                    slot.used = 0;
                }
                return slots_[0].data && slots_[1].data;
            }

            void close() {
                if (file_)
                    file_.close();
                size_ = 0;
            }

            uint64_t size() const override {
                return size_;
            }

            size_t readAt(uint64_t offset, std::span<uint8_t> output) override {
                if (offset >= size_ || output.empty())
                    return 0;
                if (output.size() > kSlotBytes) {
                    if (!file_.seek(offset))
                        return 0;
                    return file_.read(output.data(), output.size());
                }
                ++clock_;
                Slot* hit = nullptr;
                for (Slot& slot: slots_)
                    if (slot.length > 0 && offset >= slot.start && offset + output.size() <= slot.start + slot.length)
                        hit = &slot;
                if (hit == nullptr) {
                    hit = slots_[0].used <= slots_[1].used ? &slots_[0] : &slots_[1];
                    hit->start = offset;
                    hit->length = file_.seek(offset) ? file_.read(hit->data.get(), kSlotBytes) : 0;
                    if (hit->length == 0)
                        return 0;
                }
                hit->used = clock_;
                const size_t available = static_cast<size_t>(hit->start + hit->length - offset);
                const size_t count = std::min(output.size(), available);
                std::memcpy(output.data(), hit->data.get() + (offset - hit->start), count);
                return count;
            }

        private:
            static constexpr size_t kSlotBytes = 8 * 1024;
            struct Slot {
                std::unique_ptr<uint8_t, Free> data;
                uint64_t start = 0;
                size_t length = 0;
                uint32_t used = 0;
            };
            fs::File file_;
            uint64_t size_ = 0;
            uint32_t clock_ = 0;
            std::array<Slot, 2> slots_;
        };

        // State owned by the audio task alone.
        struct Player {
            FileSource source;
            audiobook::Mp4Audio book;
            audiobook::AacDecoder decoder;
            fs::File file;
            WavInfo wav;
            std::unique_ptr<uint8_t, Free> frame;
            std::unique_ptr<int16_t, Free> pcm;
            Mode mode = Mode::Idle;
            Content content = Content::None;
            std::string path;
            uint32_t outputRate = 0;
            // Rate of the frames counted since baseMs; unlike outputRate it survives a pause.
            uint32_t frameRate = 0;
            uint64_t baseMs = 0;
            uint64_t framesSinceBase = 0;
            uint64_t durationMs = 0;
            uint64_t recordedBytes = 0;
            uint32_t lastPublishMs = 0;
            uint32_t lastSaveMs = 0;
            uint32_t lastHeaderMs = 0;
            uint32_t decodeErrors = 0;
            uint16_t level = 0;
            bool failed = false;
        };

        Player gPlayer;

        uint64_t positionMs(const Player& player) {
            const uint32_t rate = player.content == Content::Memo ? player.wav.sampleRate : player.frameRate;
            return player.baseMs + (rate == 0 ? 0 : player.framesSinceBase * 1000 / rate);
        }

        void savePosition(Player& player) {
            if (player.content != Content::Audiobook || player.path.empty())
                return;
            if (!StorageFiles::ensureDirectory(kPositionsPath))
                return;
            const std::string target = positionPathFor(player.path);
            fs::File file = Board::Storage::filesystem().open(target.c_str(), FILE_WRITE);
            if (!file)
                return;
            file.print(formatPosition({positionMs(player), player.durationMs}).c_str());
            file.close();
        }

        uint64_t savedPosition(std::string_view path) {
            const std::string target = positionPathFor(path);
            const auto text = StorageFiles::readTextFile(Board::Storage::filesystem(), target.c_str(), 64);
            if (!text)
                return 0;
            const auto saved = parsePosition(*text);
            return saved ? saved->positionMs : 0;
        }

        void closeFiles(Player& player) {
            player.source.close();
            if (player.file)
                player.file.close();
            player.decoder.end();
        }

        void stopOutput(Player& player) {
            Board::Audio::stopOutput();
            player.outputRate = 0;
        }

        bool seekBook(Player& player, uint64_t ms) {
            const uint32_t sample = player.book.sampleAtMs(ms);
            if (!player.book.seek(sample))
                return false;
            player.decoder.reset();
            player.baseMs = player.book.msAtSample(sample);
            player.framesSinceBase = 0;
            return true;
        }

        bool seekMemo(Player& player, uint64_t ms) {
            const uint32_t blockAlign = player.wav.channels * 2U;
            const uint64_t frame = std::min<uint64_t>(ms * player.wav.sampleRate / 1000,
                                                      player.wav.dataBytes / blockAlign);
            if (!player.file.seek(player.wav.dataOffset + frame * blockAlign))
                return false;
            player.baseMs = frame * 1000 / player.wav.sampleRate;
            player.framesSinceBase = 0;
            return true;
        }

        bool openBook(Player& player) {
            if (!player.source.open(player.path.c_str())) {
                ESP_LOGW(kTag, "cannot open %s", player.path.c_str());
                return false;
            }
            player.book = {};
            if (auto opened = player.book.open(player.source); !opened) {
                ESP_LOGW(kTag, "%s: %s", player.path.c_str(), opened.error().c_str());
                return false;
            }
            if (auto ready = player.decoder.begin(player.book.format()); !ready) {
                ESP_LOGW(kTag, "decoder: %s", ready.error().c_str());
                return false;
            }
            player.durationMs = player.book.durationMs();
            uint64_t start = savedPosition(player.path);
            // A finished book starts over rather than ending again at once.
            if (player.durationMs > 0 && start + 2'000 >= player.durationMs)
                start = 0;
            ESP_LOGI(kTag, "audiobook %s: %u Hz, %u ch, %llu ms, %u chapters, from %llu ms", player.path.c_str(),
                     static_cast<unsigned>(player.book.format().sampleRate),
                     static_cast<unsigned>(player.book.format().channels),
                     static_cast<unsigned long long>(player.durationMs),
                     static_cast<unsigned>(player.book.chapters().size()), static_cast<unsigned long long>(start));
            return seekBook(player, start);
        }

        bool openMemo(Player& player) {
            player.file = Board::Storage::filesystem().open(player.path.c_str(), FILE_READ);
            if (!player.file)
                return false;
            std::array<uint8_t, 512> prefix{};
            const size_t read = player.file.read(prefix.data(), prefix.size());
            auto info = parseWavHeader(std::span{prefix}.first(read), player.file.size());
            if (!info) {
                ESP_LOGW(kTag, "%s: %s", player.path.c_str(), info.error().c_str());
                return false;
            }
            player.wav = *info;
            player.durationMs = wavDurationMs(player.wav);
            return seekMemo(player, 0);
        }

        // Duplicates mono samples into both stereo slots, back to front so the buffer can be shared.
        void widen(int16_t* pcm, size_t frames) {
            for (size_t index = frames; index-- > 0;) {
                pcm[index * 2 + 1] = pcm[index];
                pcm[index * 2] = pcm[index];
            }
        }

        bool ensureOutput(Player& player, uint32_t rate, uint8_t volume) {
            if (player.outputRate == rate)
                return true;
            if (!Board::Audio::startOutput(rate)) {
                ESP_LOGW(kTag, "speaker output unavailable at %u Hz", static_cast<unsigned>(rate));
                return false;
            }
            Board::Audio::setVolume(volume);
            player.outputRate = rate;
            return true;
        }

        // One decoded unit per call; returns false when playback ended or failed.
        bool pumpBook(Player& player, uint8_t volume) {
            const auto sample = player.book.next();
            if (!sample)
                return false;
            if (sample->size > kFrameBytes) {
                ++player.decodeErrors;
                return true;
            }
            const std::span<uint8_t> frame{player.frame.get(), sample->size};
            if (player.source.readAt(sample->offset, frame) != sample->size) {
                ESP_LOGW(kTag, "read failed at frame %u", static_cast<unsigned>(sample->index));
                player.failed = true;
                return false;
            }
            const auto decoded = player.decoder.decode(frame, std::span{player.pcm.get(), kPcmSamples});
            if (!decoded || decoded->channels == 0 || decoded->samples == 0) {
                if (player.decodeErrors++ == 0)
                    ESP_LOGW(kTag, "decode: %s", decoded ? "empty frame" : decoded.error().c_str());
                return true;
            }
            if (!ensureOutput(player, decoded->sampleRate, volume)) {
                player.failed = true;
                return false;
            }
            const size_t frames = decoded->samples / decoded->channels;
            if (decoded->channels == 1)
                widen(player.pcm.get(), frames);
            Board::Audio::writeFrames(player.pcm.get(), frames, kWriteTimeoutMs);
            if (player.frameRate != decoded->sampleRate) {
                player.baseMs = positionMs(player);
                player.framesSinceBase = 0;
                player.frameRate = decoded->sampleRate;
            }
            player.framesSinceBase += frames;
            return true;
        }

        bool pumpMemo(Player& player, uint8_t volume) {
            const uint32_t blockAlign = player.wav.channels * 2U;
            const uint64_t consumed = player.file.position() - player.wav.dataOffset;
            if (consumed >= player.wav.dataBytes)
                return false;
            const size_t bytes = static_cast<size_t>(std::min<uint64_t>(kMemoFrames * blockAlign,
                                                                        player.wav.dataBytes - consumed));
            const size_t read = player.file.read(reinterpret_cast<uint8_t*>(player.pcm.get()), bytes);
            const size_t frames = read / blockAlign;
            if (frames == 0)
                return false;
            if (!ensureOutput(player, player.wav.sampleRate, volume)) {
                player.failed = true;
                return false;
            }
            if (player.wav.channels == 1)
                widen(player.pcm.get(), frames);
            Board::Audio::writeFrames(player.pcm.get(), frames, kWriteTimeoutMs);
            player.framesSinceBase += frames;
            return true;
        }

        void refreshHeader(Player& player) {
            const auto header = wavHeader(kMemoSampleRateHz, 1, static_cast<uint32_t>(player.recordedBytes));
            const size_t end = player.file.position();
            player.file.seek(0);
            player.file.write(header.data(), header.size());
            player.file.seek(end);
            player.file.flush();
        }

        bool pumpRecording(Player& player) {
            int16_t* samples = player.pcm.get();
            const size_t count = Board::Audio::readMono(samples, kMemoFrames, kReadTimeoutMs);
            if (count == 0)
                return true;
            const size_t bytes = count * sizeof(int16_t);
            if (player.file.write(reinterpret_cast<const uint8_t*>(samples), bytes) != bytes) {
                ESP_LOGW(kTag, "memo write failed; card full?");
                player.failed = true;
                return false;
            }
            player.recordedBytes += bytes;
            player.framesSinceBase += count;
            int32_t peak = 0;
            for (size_t index = 0; index < count; ++index)
                peak = std::max<int32_t>(peak, std::abs(static_cast<int32_t>(samples[index])));
            const uint16_t level = static_cast<uint16_t>(std::min<int32_t>(1000, peak * 1000 / 32767));
            player.level = std::max<uint16_t>(level, static_cast<uint16_t>(player.level * 4 / 5));
            const uint32_t now = millis();
            if (now - player.lastHeaderMs >= kHeaderRefreshMs) {
                player.lastHeaderMs = now;
                refreshHeader(player);
            }
            return positionMs(player) < kMaxRecordingMs;
        }

        void finishRecording(Player& player) {
            if (player.file) {
                refreshHeader(player);
                player.file.close();
            }
            Board::Audio::stopInput();
            ESP_LOGI(kTag, "memo %s: %llu ms", player.path.c_str(),
                     static_cast<unsigned long long>(positionMs(player)));
        }

    } // namespace

    bool AudioEngine::begin() {
        if (task_ != nullptr)
            return true;
        if (!gPlayer.frame)
            gPlayer.frame.reset(static_cast<uint8_t*>(allocate(kFrameBytes)));
        if (!gPlayer.pcm)
            gPlayer.pcm.reset(static_cast<int16_t*>(allocate(kPcmSamples * sizeof(int16_t))));
        if (!gPlayer.frame || !gPlayer.pcm)
            return false;
        queue_ = xQueueCreate(kQueueLength, sizeof(Command));
        if (queue_ == nullptr)
            return false;
        if (xTaskCreatePinnedToCore(taskEntry, "audio", kStackBytes, this, kPriority, &task_, kCore) != pdPASS) {
            vQueueDelete(queue_);
            queue_ = nullptr;
            task_ = nullptr;
            return false;
        }
        return true;
    }

    void AudioEngine::send(const Command& command) {
        if (!begin())
            return;
        xQueueSend(queue_, &command, pdMS_TO_TICKS(100));
    }

    void AudioEngine::play(std::string_view path, Content content) {
        Command command{.type = CommandType::Play, .content = content};
        path.copy(command.path, std::min(path.size(), sizeof(command.path) - 1));
        send(command);
    }

    void AudioEngine::togglePause() {
        send({.type = CommandType::TogglePause});
    }

    void AudioEngine::pause() {
        send({.type = CommandType::Pause});
    }

    void AudioEngine::skip(int32_t seconds) {
        send({.type = CommandType::Skip, .value = seconds});
    }

    void AudioEngine::stepChapter(int delta) {
        send({.type = CommandType::Chapter, .value = delta});
    }

    void AudioEngine::stop() {
        if (task_ != nullptr)
            send({.type = CommandType::Stop});
    }

    void AudioEngine::record(std::string_view path) {
        Command command{.type = CommandType::Record, .content = Content::Memo};
        path.copy(command.path, std::min(path.size(), sizeof(command.path) - 1));
        send(command);
    }

    void AudioEngine::setVolume(uint8_t percent) {
        percent = std::min<uint8_t>(percent, 100);
        {
            std::lock_guard lock(mutex_);
            volume_ = percent;
            status_.volume = percent;
            ++status_.revision;
        }
        if (task_ != nullptr)
            send({.type = CommandType::Volume, .value = percent});
    }

    void AudioEngine::stopAndWait(uint32_t timeoutMs) {
        if (task_ == nullptr)
            return;
        stop();
        const uint32_t started = millis();
        while (millis() - started < timeoutMs) {
            if (status().mode == Mode::Idle && uxQueueMessagesWaiting(queue_) == 0)
                return;
            delay(10);
        }
        ESP_LOGW(kTag, "audio did not stop within %u ms", static_cast<unsigned>(timeoutMs));
    }

    Status AudioEngine::status() const {
        std::lock_guard lock(mutex_);
        return status_;
    }

    std::string AudioEngine::path() const {
        std::lock_guard lock(mutex_);
        return path_;
    }

    std::string AudioEngine::title() const {
        std::lock_guard lock(mutex_);
        return title_;
    }

    std::string AudioEngine::chapterTitle(int32_t chapter) const {
        std::lock_guard lock(mutex_);
        return chapter >= 0 && static_cast<size_t>(chapter) < chapters_.size() ? chapters_[chapter].title
                                                                               : std::string{};
    }

    size_t AudioEngine::chapterCount() const {
        std::lock_guard lock(mutex_);
        return chapters_.size();
    }

    bool AudioEngine::busy() const {
        const Mode mode = status().mode;
        return mode == Mode::Playing || mode == Mode::Recording;
    }

    void AudioEngine::taskEntry(void* context) {
        static_cast<AudioEngine*>(context)->run();
    }

    void AudioEngine::publish(bool force) {
        const uint32_t now = millis();
        if (!force && now - gPlayer.lastPublishMs < kPublishIntervalMs)
            return;
        gPlayer.lastPublishMs = now;
        const uint64_t position = positionMs(gPlayer);
        const int32_t chapter = gPlayer.content == Content::Audiobook && !gPlayer.book.chapters().empty()
                                  ? static_cast<int32_t>(gPlayer.book.chapterAtMs(position))
                                  : -1;
        std::lock_guard lock(mutex_);
        status_.mode = gPlayer.mode;
        status_.content = gPlayer.content;
        status_.positionMs = position;
        status_.durationMs = gPlayer.durationMs;
        status_.chapter = chapter;
        status_.level = gPlayer.level;
        status_.failed = gPlayer.failed;
        ++status_.revision;
    }

    void AudioEngine::handle(const Command& command) {
        Player& player = gPlayer;
        const auto stopCurrent = [&] {
            if (player.mode == Mode::Recording) {
                finishRecording(player);
            } else if (player.mode != Mode::Idle) {
                savePosition(player);
                stopOutput(player);
            }
            closeFiles(player);
            player.mode = Mode::Idle;
            player.level = 0;
        };

        switch (command.type) {
        case CommandType::Play: {
            stopCurrent();
            player.path = command.path;
            player.content = command.content;
            player.failed = false;
            player.decodeErrors = 0;
            player.durationMs = 0;
            player.baseMs = 0;
            player.framesSinceBase = 0;
            player.frameRate = 0;
            const bool opened = command.content == Content::Audiobook ? openBook(player) : openMemo(player);
            if (!opened) {
                closeFiles(player);
                player.failed = true;
                player.content = Content::None;
            } else {
                player.mode = Mode::Playing;
                player.lastSaveMs = millis();
            }
            {
                std::lock_guard lock(mutex_);
                path_ = player.path;
                title_ = command.content == Content::Audiobook && opened && !player.book.title().empty()
                           ? player.book.title()
                           : displayName(player.path);
                chapters_ = command.content == Content::Audiobook && opened ? player.book.chapters()
                                                                            : std::vector<audiobook::Chapter>{};
            }
            break;
        }
        case CommandType::TogglePause:
        case CommandType::Pause:
            if (player.mode == Mode::Playing) {
                player.mode = Mode::Paused;
                stopOutput(player);
                savePosition(player);
            } else if (player.mode == Mode::Paused && command.type == CommandType::TogglePause) {
                player.mode = Mode::Playing;
            }
            break;
        case CommandType::Skip:
        case CommandType::Chapter: {
            if (player.mode != Mode::Playing && player.mode != Mode::Paused)
                break;
            const uint64_t current = positionMs(player);
            uint64_t target = current;
            if (command.type == CommandType::Skip) {
                const int64_t moved = static_cast<int64_t>(current) + static_cast<int64_t>(command.value) * 1000;
                target = static_cast<uint64_t>(std::clamp<int64_t>(moved, 0, static_cast<int64_t>(player.durationMs)));
            } else if (player.content == Content::Audiobook && !player.book.chapters().empty()) {
                const auto& chapters = player.book.chapters();
                size_t chapter = player.book.chapterAtMs(current);
                if (command.value < 0) {
                    // Like a CD player: back once restarts the chapter, twice goes to the one before.
                    if (current - chapters[chapter].startMs < kChapterRestartMs && chapter > 0)
                        --chapter;
                } else if (chapter + 1 < chapters.size()) {
                    ++chapter;
                } else {
                    break;
                }
                target = chapters[chapter].startMs;
            } else {
                target = command.value < 0 ? 0 : player.durationMs;
            }
            const bool sought = player.content == Content::Audiobook ? seekBook(player, target)
                                                                     : seekMemo(player, target);
            if (!sought)
                ESP_LOGW(kTag, "seek to %llu ms failed", static_cast<unsigned long long>(target));
            savePosition(player);
            break;
        }
        case CommandType::Stop:
            stopCurrent();
            player.content = Content::None;
            break;
        case CommandType::Record: {
            stopCurrent();
            player.path = command.path;
            player.content = Content::Memo;
            player.failed = false;
            player.recordedBytes = 0;
            player.baseMs = 0;
            player.framesSinceBase = 0;
            player.durationMs = 0;
            player.wav = {.sampleRate = kMemoSampleRateHz, .channels = 1, .bitsPerSample = 16};
            const bool folder = StorageFiles::ensureDirectory(kMemosPath).has_value();
            if (folder && Board::Audio::startInput(kMemoSampleRateHz)) {
                player.file = Board::Storage::filesystem().open(player.path.c_str(), FILE_WRITE);
                if (player.file) {
                    const auto header = wavHeader(kMemoSampleRateHz, 1, 0);
                    player.file.write(header.data(), header.size());
                    player.lastHeaderMs = millis();
                    player.mode = Mode::Recording;
                    ESP_LOGI(kTag, "recording %s", player.path.c_str());
                } else {
                    Board::Audio::stopInput();
                }
            }
            if (player.mode != Mode::Recording) {
                ESP_LOGW(kTag, "could not start recording %s", player.path.c_str());
                player.failed = true;
            }
            {
                std::lock_guard lock(mutex_);
                path_ = player.path;
                title_ = displayName(player.path);
                chapters_.clear();
            }
            break;
        }
        case CommandType::Volume:
            if (player.outputRate != 0)
                Board::Audio::setVolume(static_cast<uint8_t>(command.value));
            break;
        }
        publish(true);
    }

    void AudioEngine::run() {
        Command command;
        while (true) {
            const bool streaming = gPlayer.mode == Mode::Playing || gPlayer.mode == Mode::Recording;
            if (xQueueReceive(queue_, &command, streaming ? 0 : pdMS_TO_TICKS(500)) == pdTRUE) {
                handle(command);
                continue;
            }
            if (gPlayer.mode == Mode::Playing) {
                uint8_t volume;
                {
                    std::lock_guard lock(mutex_);
                    volume = volume_;
                }
                const bool more = gPlayer.content == Content::Audiobook ? pumpBook(gPlayer, volume)
                                                                        : pumpMemo(gPlayer, volume);
                if (!more) {
                    // The end of a book is remembered as finished; the next play starts it over.
                    if (!gPlayer.failed && gPlayer.content == Content::Audiobook) {
                        gPlayer.framesSinceBase = 0;
                        gPlayer.baseMs = gPlayer.durationMs;
                    }
                    savePosition(gPlayer);
                    stopOutput(gPlayer);
                    closeFiles(gPlayer);
                    gPlayer.mode = Mode::Idle;
                    publish(true);
                    continue;
                }
                if (millis() - gPlayer.lastSaveMs >= kAutosaveIntervalMs) {
                    gPlayer.lastSaveMs = millis();
                    savePosition(gPlayer);
                }
                publish();
            } else if (gPlayer.mode == Mode::Recording) {
                if (!pumpRecording(gPlayer)) {
                    finishRecording(gPlayer);
                    gPlayer.mode = Mode::Idle;
                    gPlayer.level = 0;
                    publish(true);
                    continue;
                }
                publish();
            }
        }
    }

} // namespace audio
