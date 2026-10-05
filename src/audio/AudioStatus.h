#pragma once

#include <cstdint>

namespace audio {

    enum class Mode : uint8_t {
        Idle,
        Playing,
        Paused,
        Recording,
    };

    enum class Content : uint8_t {
        None,
        Audiobook,
        Memo,
    };

    struct Status {
        Mode mode = Mode::Idle;
        Content content = Content::None;
        uint64_t positionMs = 0;
        uint64_t durationMs = 0;
        int32_t chapter = -1;
        // Microphone peak while recording, 0-1000.
        uint16_t level = 0;
        uint8_t volume = 75;
        // The last request could not start, or stopped on a read, decode or write error.
        bool failed = false;
        // Changes whenever anything above changes, so screens can skip redraws.
        uint32_t revision = 0;
    };

} // namespace audio
