#include "audiobook/Mp4Audio.h"

#include <algorithm>
#include <span>

namespace audiobook {
    namespace {

        constexpr uint32_t fourcc(const char (&text)[5]) {
            return (static_cast<uint32_t>(static_cast<uint8_t>(text[0])) << 24)
                 | (static_cast<uint32_t>(static_cast<uint8_t>(text[1])) << 16)
                 | (static_cast<uint32_t>(static_cast<uint8_t>(text[2])) << 8)
                 | static_cast<uint32_t>(static_cast<uint8_t>(text[3]));
        }

        constexpr uint32_t kCopyrightSign = 0xA9U << 24;
        constexpr uint32_t kTitleTag = kCopyrightSign | (static_cast<uint32_t>('n') << 16) | ('a' << 8) | 'm';
        constexpr uint32_t kAlbumTag = kCopyrightSign | (static_cast<uint32_t>('a') << 16) | ('l' << 8) | 'b';
        constexpr uint32_t kArtistTag = kCopyrightSign | (static_cast<uint32_t>('A') << 16) | ('R' << 8) | 'T';
        constexpr size_t kMaxTextBytes = 256;
        constexpr size_t kMaxDescriptionBytes = 4096;
        constexpr size_t kMaxChapters = 2000;
        constexpr int kMaxDepth = 8;

        uint16_t be16(const uint8_t* bytes) {
            return static_cast<uint16_t>((bytes[0] << 8) | bytes[1]);
        }

        uint32_t be32(const uint8_t* bytes) {
            return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16)
                 | (static_cast<uint32_t>(bytes[2]) << 8) | bytes[3];
        }

        uint64_t be64(const uint8_t* bytes) {
            return (static_cast<uint64_t>(be32(bytes)) << 32) | be32(bytes + 4);
        }

        struct Box {
            uint32_t type = 0;
            uint64_t payload = 0;
            uint64_t end = 0;
        };

        std::optional<Box> readBox(ByteSource& source, uint64_t offset, uint64_t end) {
            if (offset + 8 > end)
                return std::nullopt;
            std::array<uint8_t, 16> header{};
            const size_t read = source.readAt(offset, std::span{header}.first(std::min<uint64_t>(16, end - offset)));
            if (read < 8)
                return std::nullopt;
            uint64_t size = be32(header.data());
            uint64_t headerSize = 8;
            if (size == 1) {
                if (read < 16)
                    return std::nullopt;
                size = be64(header.data() + 8);
                headerSize = 16;
            } else if (size == 0) {
                size = end - offset;
            }
            if (size < headerSize || size > end - offset)
                return std::nullopt;
            return Box{be32(header.data() + 4), offset + headerSize, offset + size};
        }

        // Calls visit(box) for each child box in [offset, end).
        template<typename Visit>
        void forEachBox(ByteSource& source, uint64_t offset, uint64_t end, Visit&& visit) {
            while (const auto box = readBox(source, offset, end)) {
                visit(*box);
                offset = box->end;
            }
        }

        class BitReader {
        public:
            explicit BitReader(std::span<const uint8_t> bytes) : bytes_(bytes) {}

            uint32_t read(uint8_t bits) {
                uint32_t value = 0;
                for (uint8_t bit = 0; bit < bits; ++bit) {
                    const size_t byte = position_ / 8;
                    const uint8_t current = byte < bytes_.size() ? bytes_[byte] : 0;
                    value = (value << 1) | ((current >> (7 - position_ % 8)) & 1U);
                    ++position_;
                }
                return value;
            }

            bool exhausted() const {
                return position_ > bytes_.size() * 8;
            }

        private:
            std::span<const uint8_t> bytes_;
            size_t position_ = 0;
        };

        constexpr std::array<uint32_t, 13> kSampleRates{96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                                        22050, 16000, 12000, 11025, 8000,  7350};

        uint32_t readSampleRate(BitReader& bits) {
            const uint32_t index = bits.read(4);
            if (index == 15)
                return bits.read(24);
            return index < kSampleRates.size() ? kSampleRates[index] : 0;
        }

        uint8_t readObjectType(BitReader& bits) {
            const uint8_t type = static_cast<uint8_t>(bits.read(5));
            return type == 31 ? static_cast<uint8_t>(32 + bits.read(6)) : type;
        }

        // MPEG-4 descriptor length: up to four 7-bit groups.
        bool readDescriptor(std::span<const uint8_t> bytes, size_t& position, uint8_t& tag, size_t& length) {
            if (position >= bytes.size())
                return false;
            tag = bytes[position++];
            length = 0;
            for (int count = 0; count < 4; ++count) {
                if (position >= bytes.size())
                    return false;
                const uint8_t value = bytes[position++];
                length = (length << 7) | (value & 0x7FU);
                if ((value & 0x80U) == 0)
                    return position + length <= bytes.size();
            }
            return false;
        }

        void appendUtf8(std::string& text, uint32_t codepoint) {
            if (codepoint < 0x80) {
                text += static_cast<char>(codepoint);
            } else if (codepoint < 0x800) {
                text += static_cast<char>(0xC0 | (codepoint >> 6));
                text += static_cast<char>(0x80 | (codepoint & 0x3F));
            } else if (codepoint < 0x10000) {
                text += static_cast<char>(0xE0 | (codepoint >> 12));
                text += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                text += static_cast<char>(0x80 | (codepoint & 0x3F));
            } else {
                text += static_cast<char>(0xF0 | (codepoint >> 18));
                text += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
                text += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                text += static_cast<char>(0x80 | (codepoint & 0x3F));
            }
        }

        // QuickTime text samples are UTF-8, or UTF-16 when they start with a byte-order mark.
        std::string decodeText(std::span<const uint8_t> bytes) {
            std::string text;
            if (bytes.size() >= 2 && ((bytes[0] == 0xFE && bytes[1] == 0xFF) || (bytes[0] == 0xFF && bytes[1] == 0xFE))) {
                const bool big = bytes[0] == 0xFE;
                for (size_t index = 2; index + 1 < bytes.size(); index += 2) {
                    uint32_t unit = big ? be16(bytes.data() + index) : static_cast<uint16_t>(bytes[index] | (bytes[index + 1] << 8));
                    if (unit >= 0xD800 && unit < 0xDC00 && index + 3 < bytes.size()) {
                        const uint32_t low = big ? be16(bytes.data() + index + 2)
                                                 : static_cast<uint16_t>(bytes[index + 2] | (bytes[index + 3] << 8));
                        unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                        index += 2;
                    }
                    appendUtf8(text, unit);
                }
                return text;
            }
            text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            while (!text.empty() && text.back() == '\0')
                text.pop_back();
            return text;
        }

    } // namespace

    void Mp4Audio::Table::reset(ByteSource* source, uint64_t offset, uint32_t count, uint8_t fieldBytes,
                                uint8_t fields) {
        source_ = source;
        offset_ = offset;
        count_ = count;
        fieldBytes_ = fieldBytes;
        fields_ = fields;
        windowStart_ = UINT32_MAX;
        windowCount_ = 0;
        window_.clear();
    }

    bool Mp4Audio::Table::entry(uint32_t index, std::array<uint64_t, 3>& fields) {
        if (source_ == nullptr || index >= count_)
            return false;
        const size_t entryBytes = static_cast<size_t>(fieldBytes_) * fields_;
        if (windowStart_ == UINT32_MAX || index < windowStart_ || index >= windowStart_ + windowCount_) {
            windowStart_ = index - index % kWindowEntries;
            windowCount_ = std::min(kWindowEntries, count_ - windowStart_);
            window_.resize(entryBytes * windowCount_);
            if (source_->readAt(offset_ + static_cast<uint64_t>(windowStart_) * entryBytes, window_) != window_.size()) {
                windowStart_ = UINT32_MAX;
                return false;
            }
        }
        const uint8_t* bytes = window_.data() + (index - windowStart_) * entryBytes;
        for (uint8_t field = 0; field < fields_; ++field)
            fields[field] = fieldBytes_ == 8 ? be64(bytes + field * 8) : be32(bytes + field * 4);
        return true;
    }

    bool Mp4Audio::readBytes(uint64_t offset, std::span<uint8_t> output) {
        return source_->readAt(offset, output) == output.size();
    }

    std::expected<void, std::string> Mp4Audio::open(ByteSource& source) {
        *this = {};
        source_ = &source;
        bool foundMovie = false;
        std::expected<void, std::string> parsed;
        forEachBox(source, 0, source.size(), [&](const Box& box) {
            if (box.type == fourcc("moov") && !foundMovie) {
                foundMovie = true;
                parsed = parseMovie(box.payload, box.end);
            }
        });
        if (!foundMovie)
            return std::unexpected("not an MP4 audiobook: no movie header");
        if (!parsed)
            return parsed;

        const auto audio = std::ranges::find(tracks_, fourcc("soun"), &TrackTables::handler);
        if (audio == tracks_.end())
            return std::unexpected("no audio track");
        audio_ = *audio;
        if (auto described = parseAudioDescription(audio_); !described)
            return described;
        if (audio_.sampleCount == 0 || audio_.timescale == 0 || audio_.chunkCount == 0 || audio_.sampleToChunkCount == 0)
            return std::unexpected("audio track has no samples");

        timeToSample_.reset(source_, audio_.timeToSample, audio_.timeToSampleCount, 4, 2);
        sampleToChunk_.reset(source_, audio_.sampleToChunk, audio_.sampleToChunkCount, 4, 3);
        sampleSizes_.reset(source_, audio_.sampleSizes, audio_.uniformSampleSize ? 0 : audio_.sampleCount, 4, 1);
        chunkOffsets_.reset(source_, audio_.chunkOffsets, audio_.chunkCount, audio_.largeChunkOffsets ? 8 : 4, 1);
        sampleCount_ = audio_.sampleCount;

        if (audio_.chapterTrackId != 0) {
            const auto chapterTrack = std::ranges::find(tracks_, audio_.chapterTrackId, &TrackTables::trackId);
            if (chapterTrack != tracks_.end())
                readTrackChapters(*chapterTrack);
        }
        tracks_.clear();
        if (!seek(0))
            return std::unexpected("audio sample tables are invalid");
        return {};
    }

    std::expected<void, std::string> Mp4Audio::parseMovie(uint64_t offset, uint64_t end) {
        std::expected<void, std::string> result;
        forEachBox(*source_, offset, end, [&](const Box& box) {
            if (!result)
                return;
            if (box.type == fourcc("trak")) {
                TrackTables track;
                result = parseTrack(box.payload, box.end, track);
                tracks_.push_back(track);
            } else if (box.type == fourcc("udta")) {
                forEachBox(*source_, box.payload, box.end, [&](const Box& child) {
                    if (child.type == fourcc("meta"))
                        parseMetadata(child.payload, child.end);
                    else if (child.type == fourcc("chpl") && chapters_.empty())
                        parseNeroChapters(child.payload, child.end);
                });
            }
        });
        return result;
    }

    std::expected<void, std::string> Mp4Audio::parseTrack(uint64_t offset, uint64_t end, TrackTables& track) {
        std::expected<void, std::string> result;
        std::array<uint8_t, 32> bytes{};
        const auto visit = [&](auto& self, const Box& box, int depth) -> void {
            if (!result || depth > kMaxDepth)
                return;
            const uint64_t payloadSize = box.end - box.payload;
            const auto read = [&](size_t count) {
                return payloadSize >= count && readBytes(box.payload, std::span{bytes}.first(count));
            };
            switch (box.type) {
            case fourcc("trak"):
            case fourcc("mdia"):
            case fourcc("minf"):
            case fourcc("stbl"):
            case fourcc("tref"):
                forEachBox(*source_, box.payload, box.end, [&](const Box& child) { self(self, child, depth + 1); });
                break;
            case fourcc("tkhd"):
                if (read(24))
                    track.trackId = bytes[0] == 1 ? be32(bytes.data() + 20) : be32(bytes.data() + 12);
                break;
            case fourcc("chap"):
                if (read(4))
                    track.chapterTrackId = be32(bytes.data());
                break;
            case fourcc("mdhd"):
                if (read(32)) {
                    if (bytes[0] == 1) {
                        track.timescale = be32(bytes.data() + 20);
                        track.duration = be64(bytes.data() + 24);
                    } else {
                        track.timescale = be32(bytes.data() + 12);
                        track.duration = be32(bytes.data() + 16);
                    }
                } else if (read(20)) {
                    track.timescale = be32(bytes.data() + 12);
                    track.duration = be32(bytes.data() + 16);
                }
                break;
            case fourcc("hdlr"):
                if (read(12))
                    track.handler = be32(bytes.data() + 8);
                break;
            case fourcc("stsd"):
                track.sampleDescription = box.payload;
                track.sampleDescriptionSize = payloadSize;
                break;
            case fourcc("stts"):
                if (read(8)) {
                    track.timeToSample = box.payload + 8;
                    track.timeToSampleCount = be32(bytes.data() + 4);
                }
                break;
            case fourcc("stsc"):
                if (read(8)) {
                    track.sampleToChunk = box.payload + 8;
                    track.sampleToChunkCount = be32(bytes.data() + 4);
                }
                break;
            case fourcc("stsz"):
                if (read(12)) {
                    track.uniformSampleSize = be32(bytes.data() + 4);
                    track.sampleCount = be32(bytes.data() + 8);
                    track.sampleSizes = box.payload + 12;
                }
                break;
            case fourcc("stz2"):
                result = std::unexpected("compact sample sizes are not supported");
                break;
            case fourcc("stco"):
            case fourcc("co64"):
                if (read(8)) {
                    track.largeChunkOffsets = box.type == fourcc("co64");
                    track.chunkOffsets = box.payload + 8;
                    track.chunkCount = be32(bytes.data() + 4);
                }
                break;
            default:
                break;
            }
        };
        forEachBox(*source_, offset, end, [&](const Box& box) { visit(visit, box, 1); });
        // Table counts must fit inside the file before anything trusts them.
        const uint64_t size = source_->size();
        const auto fits = [size](uint64_t start, uint64_t count, uint64_t entryBytes) {
            return start <= size && count <= (size - start) / std::max<uint64_t>(entryBytes, 1);
        };
        if (result
            && (!fits(track.timeToSample, track.timeToSampleCount, 8)
                || !fits(track.sampleToChunk, track.sampleToChunkCount, 12)
                || (!track.uniformSampleSize && !fits(track.sampleSizes, track.sampleCount, 4))
                || !fits(track.chunkOffsets, track.chunkCount, track.largeChunkOffsets ? 8 : 4)))
            result = std::unexpected("sample tables extend past the end of the file");
        return result;
    }

    std::expected<void, std::string> Mp4Audio::parseAudioDescription(const TrackTables& track) {
        if (track.sampleDescriptionSize < 16 || track.sampleDescriptionSize > kMaxDescriptionBytes)
            return std::unexpected("missing audio sample description");
        std::vector<uint8_t> description(static_cast<size_t>(track.sampleDescriptionSize));
        if (!readBytes(track.sampleDescription, description))
            return std::unexpected("could not read audio sample description");
        // stsd: version/flags, entry count, then the first sample entry box.
        const uint32_t entrySize = be32(description.data() + 8);
        const uint32_t entryType = be32(description.data() + 12);
        if (entryType == fourcc("enca"))
            return std::unexpected("this audiobook is copy-protected");
        if (entryType != fourcc("mp4a"))
            return std::unexpected("audio is not AAC");
        if (entrySize < 36 || 8 + static_cast<size_t>(entrySize) > description.size())
            return std::unexpected("audio sample entry is truncated");
        const std::span<const uint8_t> entry{description.data() + 16, entrySize - 8};
        const uint16_t soundVersion = be16(entry.data() + 8);
        const uint16_t entryChannels = be16(entry.data() + 16);
        size_t children = soundVersion == 1 ? 44 : soundVersion == 2 ? 64 : 28;
        if (children > entry.size())
            return std::unexpected("audio sample entry is truncated");

        // esds sits directly in the entry, or inside a QuickTime 'wave' box.
        std::span<const uint8_t> esds;
        const auto findEsds = [&](auto& self, std::span<const uint8_t> boxes, int depth) -> void {
            size_t position = 0;
            while (esds.empty() && depth <= 2 && position + 8 <= boxes.size()) {
                const uint32_t size = be32(boxes.data() + position);
                const uint32_t type = be32(boxes.data() + position + 4);
                if (size < 8 || position + size > boxes.size())
                    return;
                const auto payload = boxes.subspan(position + 8, size - 8);
                if (type == fourcc("esds"))
                    esds = payload;
                else if (type == fourcc("wave"))
                    self(self, payload, depth + 1);
                position += size;
            }
        };
        findEsds(findEsds, entry.subspan(children), 0);
        if (esds.size() < 4)
            return std::unexpected("missing AAC decoder configuration");

        std::span<const uint8_t> config;
        size_t position = 4; // version and flags
        uint8_t tag = 0;
        size_t length = 0;
        if (!readDescriptor(esds, position, tag, length) || tag != 0x03 || length < 3)
            return std::unexpected("invalid elementary stream descriptor");
        const uint8_t flags = esds[position + 2];
        position += 3;
        if (flags & 0x80U)
            position += 2;
        if (flags & 0x40U)
            position += position < esds.size() ? 1 + esds[position] : 0;
        if (flags & 0x20U)
            position += 2;
        if (!readDescriptor(esds, position, tag, length) || tag != 0x04 || length < 13)
            return std::unexpected("invalid decoder configuration descriptor");
        const uint8_t streamObjectType = esds[position];
        if (streamObjectType != 0x40 && streamObjectType != 0x66 && streamObjectType != 0x67
            && streamObjectType != 0x68)
            return std::unexpected("audio is not AAC");
        position += 13;
        if (!readDescriptor(esds, position, tag, length) || tag != 0x05 || length < 2)
            return std::unexpected("missing AAC audio specific configuration");
        config = esds.subspan(position, length);

        BitReader bits{config};
        uint8_t objectType = readObjectType(bits);
        uint32_t sampleRate = readSampleRate(bits);
        uint8_t channels = static_cast<uint8_t>(bits.read(4));
        format_.objectType = objectType;
        if (objectType == 5 || objectType == 29) {
            // Explicit SBR/PS: the extension rate follows, then the core object type.
            (void) readSampleRate(bits);
            objectType = readObjectType(bits);
        }
        if (bits.exhausted() || sampleRate == 0)
            return std::unexpected("invalid AAC audio specific configuration");
        if (objectType != 2)
            return std::unexpected("only AAC-LC and HE-AAC audiobooks are supported");
        format_.sampleRate = sampleRate;
        format_.channels = channels != 0 ? channels : static_cast<uint8_t>(entryChannels);
        if (format_.channels == 0 || format_.channels > 2)
            return std::unexpected("only mono and stereo audiobooks are supported");
        format_.decoderConfig.assign(config.begin(), config.end());
        return {};
    }

    void Mp4Audio::parseMetadata(uint64_t offset, uint64_t end) {
        // MP4 'meta' is a full box; QuickTime writes it without version and flags.
        std::array<uint8_t, 8> peek{};
        if (offset + 8 <= end && readBytes(offset, peek) && be32(peek.data() + 4) != fourcc("hdlr"))
            offset += 4;
        std::string album;
        std::string albumArtist;
        forEachBox(*source_, offset, end, [&](const Box& box) {
            if (box.type != fourcc("ilst"))
                return;
            forEachBox(*source_, box.payload, box.end, [&](const Box& item) {
                std::string* target = item.type == kTitleTag    ? &title_
                                    : item.type == kAlbumTag    ? &album
                                    : item.type == kArtistTag   ? &author_
                                    : item.type == fourcc("aART") ? &albumArtist
                                                                  : nullptr;
                if (target == nullptr)
                    return;
                forEachBox(*source_, item.payload, item.end, [&](const Box& data) {
                    if (data.type != fourcc("data") || data.end - data.payload <= 8)
                        return;
                    std::vector<uint8_t> value(
                        static_cast<size_t>(std::min<uint64_t>(data.end - data.payload - 8, kMaxTextBytes)));
                    if (readBytes(data.payload + 8, value))
                        *target = decodeText(value);
                });
            });
        });
        if (title_.empty())
            title_ = album;
        if (author_.empty())
            author_ = albumArtist;
    }

    void Mp4Audio::parseNeroChapters(uint64_t offset, uint64_t end) {
        std::array<uint8_t, 9> header{};
        if (offset + 5 > end || !readBytes(offset, std::span{header}.first(5)))
            return;
        uint64_t position = offset + 4;
        if (header[0] != 0)
            position += 4; // version 1 carries an extra reserved word
        std::array<uint8_t, 1> count{};
        if (position >= end || !readBytes(position++, count))
            return;
        std::vector<Chapter> chapters;
        for (uint8_t index = 0; index < count[0] && position + 9 <= end; ++index) {
            if (!readBytes(position, header))
                return;
            const uint64_t start = be64(header.data());
            std::vector<uint8_t> title(header[8]);
            position += 9;
            if (position + title.size() > end || !readBytes(position, title))
                return;
            position += title.size();
            chapters.push_back({start / 10000, decodeText(title)}); // 100 ns units
        }
        chapters_ = std::move(chapters);
    }

    void Mp4Audio::readTrackChapters(const TrackTables& track) {
        if (track.timescale == 0 || track.sampleCount == 0 || track.sampleCount > kMaxChapters)
            return;
        Table times, runs, sizes, chunks;
        times.reset(source_, track.timeToSample, track.timeToSampleCount, 4, 2);
        runs.reset(source_, track.sampleToChunk, track.sampleToChunkCount, 4, 3);
        sizes.reset(source_, track.sampleSizes, track.uniformSampleSize ? 0 : track.sampleCount, 4, 1);
        chunks.reset(source_, track.chunkOffsets, track.chunkCount, track.largeChunkOffsets ? 8 : 4, 1);

        std::vector<Chapter> chapters;
        std::array<uint64_t, 3> fields{};
        uint32_t timeEntry = 0;
        uint64_t timeRemaining = 0;
        uint64_t delta = 0;
        uint64_t time = 0;
        uint32_t sample = 0;
        for (uint32_t run = 0; run < runs.count() && sample < track.sampleCount; ++run) {
            if (!runs.entry(run, fields))
                return;
            const uint64_t firstChunk = fields[0];
            const uint64_t perChunk = fields[1];
            uint64_t lastChunk = track.chunkCount + 1ULL;
            if (run + 1 < runs.count()) {
                std::array<uint64_t, 3> next{};
                if (!runs.entry(run + 1, next))
                    return;
                lastChunk = next[0];
            }
            for (uint64_t chunk = firstChunk; chunk < lastChunk && sample < track.sampleCount; ++chunk) {
                if (chunk == 0 || !chunks.entry(static_cast<uint32_t>(chunk - 1), fields))
                    return;
                uint64_t offset = fields[0];
                for (uint64_t inChunk = 0; inChunk < perChunk && sample < track.sampleCount; ++inChunk, ++sample) {
                    uint64_t size = track.uniformSampleSize;
                    if (size == 0) {
                        if (!sizes.entry(sample, fields))
                            return;
                        size = fields[0];
                    }
                    while (timeRemaining == 0 && timeEntry < times.count()) {
                        if (!times.entry(timeEntry++, fields))
                            return;
                        timeRemaining = fields[0];
                        delta = fields[1];
                    }
                    std::array<uint8_t, 2> length{};
                    if (size >= 2 && readBytes(offset, length)) {
                        std::vector<uint8_t> text(std::min<size_t>(be16(length.data()), std::min<uint64_t>(size - 2, kMaxTextBytes)));
                        if (readBytes(offset + 2, text))
                            chapters.push_back({time * 1000 / track.timescale, decodeText(text)});
                    }
                    offset += size;
                    if (timeRemaining > 0) {
                        --timeRemaining;
                        time += delta;
                    }
                }
            }
        }
        if (!chapters.empty())
            chapters_ = std::move(chapters);
    }

    uint64_t Mp4Audio::durationMs() const {
        return audio_.timescale == 0 ? 0 : audio_.duration * 1000 / audio_.timescale;
    }

    uint64_t Mp4Audio::msAtSample(uint32_t index) {
        if (audio_.timescale == 0)
            return 0;
        uint64_t units = 0;
        uint32_t base = 0;
        std::array<uint64_t, 3> fields{};
        for (uint32_t entry = 0; entry < timeToSample_.count() && base < index; ++entry) {
            if (!timeToSample_.entry(entry, fields))
                break;
            const uint64_t count = std::min<uint64_t>(fields[0], index - base);
            units += count * fields[1];
            base += static_cast<uint32_t>(count);
        }
        return units * 1000 / audio_.timescale;
    }

    uint32_t Mp4Audio::sampleAtMs(uint64_t ms) {
        if (sampleCount_ == 0)
            return 0;
        const uint64_t target = ms * audio_.timescale / 1000;
        uint64_t units = 0;
        uint64_t base = 0;
        std::array<uint64_t, 3> fields{};
        for (uint32_t entry = 0; entry < timeToSample_.count(); ++entry) {
            if (!timeToSample_.entry(entry, fields))
                break;
            const uint64_t span = fields[0] * fields[1];
            if (fields[1] != 0 && target < units + span)
                return static_cast<uint32_t>(std::min<uint64_t>(base + (target - units) / fields[1], sampleCount_ - 1));
            units += span;
            base += fields[0];
        }
        return sampleCount_ - 1;
    }

    size_t Mp4Audio::chapterAtMs(uint64_t ms) const {
        size_t index = 0;
        for (size_t candidate = 0; candidate < chapters_.size() && chapters_[candidate].startMs <= ms; ++candidate)
            index = candidate;
        return index;
    }

    bool Mp4Audio::chunkRun(uint32_t run, uint32_t& firstChunk, uint32_t& samplesPerChunk) {
        std::array<uint64_t, 3> fields{};
        if (!sampleToChunk_.entry(run, fields) || fields[0] == 0 || fields[1] == 0)
            return false;
        firstChunk = static_cast<uint32_t>(fields[0] - 1);
        samplesPerChunk = static_cast<uint32_t>(fields[1]);
        return true;
    }

    bool Mp4Audio::sampleSize(uint32_t index, uint32_t& size) {
        if (audio_.uniformSampleSize != 0) {
            size = audio_.uniformSampleSize;
            return true;
        }
        std::array<uint64_t, 3> fields{};
        if (!sampleSizes_.entry(index, fields))
            return false;
        size = static_cast<uint32_t>(fields[0]);
        return true;
    }

    bool Mp4Audio::chunkOffset(uint32_t chunk, uint64_t& offset) {
        std::array<uint64_t, 3> fields{};
        if (!chunkOffsets_.entry(chunk, fields))
            return false;
        offset = fields[0];
        return true;
    }

    bool Mp4Audio::seek(uint32_t index) {
        if (index >= sampleCount_) {
            cursor_ = {.sample = sampleCount_};
            return false;
        }
        uint32_t base = 0;
        for (uint32_t run = 0; run < sampleToChunk_.count(); ++run) {
            uint32_t firstChunk = 0, perChunk = 0;
            if (!chunkRun(run, firstChunk, perChunk))
                return false;
            uint32_t endChunk = audio_.chunkCount;
            if (run + 1 < sampleToChunk_.count()) {
                uint32_t nextChunk = 0, nextPerChunk = 0;
                if (!chunkRun(run + 1, nextChunk, nextPerChunk))
                    return false;
                endChunk = nextChunk;
            }
            if (endChunk < firstChunk)
                return false;
            const uint64_t runSamples = static_cast<uint64_t>(endChunk - firstChunk) * perChunk;
            if (index < base + runSamples) {
                const uint32_t chunkInRun = (index - base) / perChunk;
                Cursor cursor{.sample = index,
                              .chunk = firstChunk + chunkInRun,
                              .chunkFirstSample = base + chunkInRun * perChunk,
                              .chunkSamples = perChunk,
                              .run = run};
                if (!chunkOffset(cursor.chunk, cursor.offset))
                    return false;
                for (uint32_t sample = cursor.chunkFirstSample; sample < index; ++sample) {
                    uint32_t size = 0;
                    if (!sampleSize(sample, size))
                        return false;
                    cursor.offset += size;
                }
                cursor_ = cursor;
                return true;
            }
            base += static_cast<uint32_t>(runSamples);
        }
        return false;
    }

    std::optional<Sample> Mp4Audio::next() {
        if (cursor_.sample >= sampleCount_)
            return std::nullopt;
        uint32_t size = 0;
        if (!sampleSize(cursor_.sample, size))
            return std::nullopt;
        const Sample sample{cursor_.sample, cursor_.offset, size};
        ++cursor_.sample;
        cursor_.offset += size;
        if (cursor_.sample - cursor_.chunkFirstSample >= cursor_.chunkSamples && cursor_.sample < sampleCount_) {
            ++cursor_.chunk;
            cursor_.chunkFirstSample = cursor_.sample;
            uint32_t nextChunk = 0, perChunk = 0;
            if (cursor_.run + 1 < sampleToChunk_.count() && chunkRun(cursor_.run + 1, nextChunk, perChunk)
                && cursor_.chunk >= nextChunk) {
                ++cursor_.run;
                cursor_.chunkSamples = perChunk;
            }
            if (!chunkOffset(cursor_.chunk, cursor_.offset))
                cursor_.sample = sampleCount_;
        }
        return sample;
    }

} // namespace audiobook
