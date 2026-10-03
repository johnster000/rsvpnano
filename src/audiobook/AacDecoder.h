#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

#include "audiobook/Mp4Audio.h"

namespace audiobook {

    struct DecodedFrame {
        size_t samples = 0; // interleaved 16-bit values written
        uint32_t sampleRate = 0;
        uint8_t channels = 0;
    };

    // Decodes raw AAC access units from an MP4 track (no ADTS headers) with the Helix decoder.
    class AacDecoder {
    public:
        // Largest output of one frame: 1024 samples, doubled by SBR, for two channels.
        static constexpr size_t kMaxFrameSamples = 1024 * 2 * 2;

        AacDecoder() = default;
        AacDecoder(const AacDecoder&) = delete;
        AacDecoder& operator=(const AacDecoder&) = delete;
        ~AacDecoder();

        std::expected<void, std::string> begin(const AudioFormat& format);
        std::expected<DecodedFrame, std::string> decode(std::span<const uint8_t> frame, std::span<int16_t> pcm);
        // Drops decoder history after a seek so the next frame starts clean.
        void reset();
        void end();

    private:
        void* decoder_ = nullptr;
        AudioFormat format_;
    };

} // namespace audiobook
