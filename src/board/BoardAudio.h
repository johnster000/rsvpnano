#pragma once

#include <Arduino.h>

namespace Board::Audio {

    bool begin();
    bool beep();
    bool available();

    // Streaming for audiobooks and voice memos. Boards without these paths report false and move no data;
    // output and input are exclusive because they share one I2S bus.
    bool startOutput(uint32_t sampleRateHz);
    // Interleaved 16-bit stereo frames; blocks until the DMA ring has room or the timeout passes.
    size_t writeFrames(const int16_t* frames, size_t count, uint32_t timeoutMs);
    void stopOutput();
    // 0 mutes, 100 is the loudest the speaker path allows.
    void setVolume(uint8_t percent);

    bool hasMicrophone();
    bool startInput(uint32_t sampleRateHz);
    // Mono 16-bit samples mixed from the board's microphones.
    size_t readMono(int16_t* samples, size_t count, uint32_t timeoutMs);
    void stopInput();

} // namespace Board::Audio
