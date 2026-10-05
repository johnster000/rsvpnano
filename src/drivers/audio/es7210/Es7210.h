#pragma once

#include <Arduino.h>
#include <Wire.h>

// ES7210 four-channel microphone ADC as an I2S slave, sending all four inputs in TDM slots of 16 bits.
namespace BoardDrivers::Es7210 {

    struct Context {
        TwoWire& wire;
        uint8_t address = 0x40;
        bool available = false;
        bool running = false;
    };

    // Gain applied to every microphone, in 3 dB steps up to 33 dB (index 11).
    inline constexpr uint8_t kDefaultGainIndex = 10; // 30 dB

    bool begin(Context& context);
    // MCLK, BCLK and LRCK must already be running at 256 x LRCK before starting.
    bool start(Context& context, uint8_t gainIndex = kDefaultGainIndex);
    void stop(Context& context);

} // namespace BoardDrivers::Es7210
