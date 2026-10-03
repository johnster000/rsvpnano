#include "audiobook/AacDecoder.h"

#include <libhelix-aac/aacdec.h>

namespace audiobook {

    AacDecoder::~AacDecoder() {
        end();
    }

    std::expected<void, std::string> AacDecoder::begin(const AudioFormat& format) {
        end();
        decoder_ = AACInitDecoder();
        if (decoder_ == nullptr)
            return std::unexpected("not enough memory for the AAC decoder");
        format_ = format;
        AACFrameInfo info{};
        info.nChans = format.channels;
        info.sampRateCore = static_cast<int>(format.sampleRate);
        info.profile = AAC_PROFILE_LC;
        if (AACSetRawBlockParams(static_cast<HAACDecoder>(decoder_), 0, &info) != ERR_AAC_NONE) {
            end();
            return std::unexpected("unsupported AAC stream parameters");
        }
        return {};
    }

    std::expected<DecodedFrame, std::string> AacDecoder::decode(std::span<const uint8_t> frame,
                                                                std::span<int16_t> pcm) {
        if (decoder_ == nullptr)
            return std::unexpected("decoder not started");
        if (pcm.size() < kMaxFrameSamples)
            return std::unexpected("output buffer too small");
        if (frame.empty())
            return DecodedFrame{};
        // Helix advances the input pointer; the frame itself is never written.
        auto* input = const_cast<unsigned char*>(frame.data());
        int remaining = static_cast<int>(frame.size());
        const int status = AACDecode(static_cast<HAACDecoder>(decoder_), &input, &remaining, pcm.data());
        if (status != ERR_AAC_NONE)
            return std::unexpected("AAC decode error " + std::to_string(status));
        AACFrameInfo info{};
        AACGetLastFrameInfo(static_cast<HAACDecoder>(decoder_), &info);
        return DecodedFrame{static_cast<size_t>(info.outputSamps), static_cast<uint32_t>(info.sampRateOut),
                            static_cast<uint8_t>(info.nChans)};
    }

    void AacDecoder::reset() {
        if (decoder_ != nullptr)
            AACFlushCodec(static_cast<HAACDecoder>(decoder_));
    }

    void AacDecoder::end() {
        if (decoder_ != nullptr)
            AACFreeDecoder(static_cast<HAACDecoder>(decoder_));
        decoder_ = nullptr;
    }

} // namespace audiobook
