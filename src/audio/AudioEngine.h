#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "audio/AudioStatus.h"
#include "audiobook/Mp4Audio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// Plays audiobooks and voice memos through the speaker and records memos from the microphones, on one task
// that owns the SD files and the I2S bus. The UI sends commands and reads a status snapshot.
namespace audio {

    class AudioEngine {
    public:
        bool begin();

        // Audiobooks resume from their saved position; memos start from the beginning.
        void play(std::string_view path, Content content);
        void togglePause();
        void pause();
        void skip(int32_t seconds);
        void stepChapter(int delta);
        void stop();
        void record(std::string_view path);
        void setVolume(uint8_t percent);
        // Waits until the task has closed its files; for power off and USB transfer.
        void stopAndWait(uint32_t timeoutMs);

        Status status() const;
        std::string path() const;
        std::string title() const;
        std::string chapterTitle(int32_t chapter) const;
        size_t chapterCount() const;
        // Playing or recording: the device should stay awake.
        bool busy() const;

    private:
        enum class CommandType : uint8_t {
            Play,
            TogglePause,
            Pause,
            Skip,
            Chapter,
            Stop,
            Record,
            Volume,
        };

        struct Command {
            CommandType type = CommandType::Stop;
            Content content = Content::None;
            int32_t value = 0;
            char path[192] = {};
        };

        static void taskEntry(void* context);
        void run();
        void send(const Command& command);
        void handle(const Command& command);
        void publish(bool force = false);

        QueueHandle_t queue_ = nullptr;
        TaskHandle_t task_ = nullptr;
        mutable std::mutex mutex_;
        Status status_;
        std::string path_;
        std::string title_;
        std::vector<audiobook::Chapter> chapters_;
        uint8_t volume_ = 75;
    };

} // namespace audio
