#include "drivers/audio/es7210/Es7210.h"

#include <esp_log.h>

#include <algorithm>

// Register sequence follows Espressif's esp_codec_dev ES7210 driver, as used by Waveshare's 3.49 audio example.
namespace BoardDrivers::Es7210 {
    namespace {

        constexpr char kTag[] = "es7210";

        constexpr uint8_t kReset = 0x00;
        constexpr uint8_t kClockOff = 0x01;
        constexpr uint8_t kMainClock = 0x02;
        constexpr uint8_t kPowerDown = 0x06;
        constexpr uint8_t kOsr = 0x07;
        constexpr uint8_t kModeConfig = 0x08;
        constexpr uint8_t kTimeControl0 = 0x09;
        constexpr uint8_t kTimeControl1 = 0x0A;
        constexpr uint8_t kSdpInterface1 = 0x11;
        constexpr uint8_t kSdpInterface2 = 0x12;
        constexpr uint8_t kAdc34Hpf2 = 0x20;
        constexpr uint8_t kAdc34Hpf1 = 0x21;
        constexpr uint8_t kAdc12Hpf1 = 0x22;
        constexpr uint8_t kAdc12Hpf2 = 0x23;
        constexpr uint8_t kAnalog = 0x40;
        constexpr uint8_t kMic12Bias = 0x41;
        constexpr uint8_t kMic34Bias = 0x42;
        constexpr uint8_t kMic1Gain = 0x43;
        constexpr uint8_t kMic1Power = 0x47;
        constexpr uint8_t kMic12Power = 0x4B;
        constexpr uint8_t kMic34Power = 0x4C;

        bool write(Context& context, uint8_t reg, uint8_t value) {
            context.wire.beginTransmission(context.address);
            context.wire.write(reg);
            context.wire.write(value);
            return context.wire.endTransmission(true) == 0;
        }

        bool read(Context& context, uint8_t reg, uint8_t& value) {
            context.wire.beginTransmission(context.address);
            context.wire.write(reg);
            if (context.wire.endTransmission(false) != 0)
                return false;
            if (context.wire.requestFrom(static_cast<int>(context.address), 1, 1) != 1)
                return false;
            value = static_cast<uint8_t>(context.wire.read());
            return true;
        }

        bool update(Context& context, uint8_t reg, uint8_t mask, uint8_t bits) {
            uint8_t value = 0;
            return read(context, reg, value)
                && write(context, reg, static_cast<uint8_t>((value & ~mask) | (bits & mask)));
        }

        // Powers all four inputs: the two microphones sit on two of them, and TDM carries every slot.
        bool selectMicrophones(Context& context, uint8_t gainIndex) {
            bool ok = true;
            for (uint8_t input = 0; input < 4; ++input)
                ok &= update(context, static_cast<uint8_t>(kMic1Gain + input), 0x10, 0x00);
            ok &= write(context, kMic12Power, 0xFF) && write(context, kMic34Power, 0xFF);
            ok &= update(context, kClockOff, 0x0B, 0x00) && write(context, kMic12Power, 0x00);
            ok &= update(context, kClockOff, 0x15, 0x00) && write(context, kMic34Power, 0x00);
            for (uint8_t input = 0; input < 4; ++input) {
                const uint8_t reg = static_cast<uint8_t>(kMic1Gain + input);
                ok &= update(context, reg, 0x10, 0x10) && update(context, reg, 0x0F, gainIndex);
            }
            return ok && write(context, kSdpInterface2, 0x02); // TDM
        }

    } // namespace

    bool begin(Context& context) {
        if (context.available)
            return true;
        const bool ok =
            write(context, kReset, 0xFF) && write(context, kReset, 0x41) && write(context, kClockOff, 0x3F)
            && write(context, kTimeControl0, 0x30) && write(context, kTimeControl1, 0x30)
            && write(context, kAdc12Hpf2, 0x2A) && write(context, kAdc12Hpf1, 0x0A)
            && write(context, kAdc34Hpf2, 0x0A) && write(context, kAdc34Hpf1, 0x2A)
            && update(context, kModeConfig, 0x01, 0x00) // I2S slave
            && write(context, kAnalog, 0x43) && write(context, kMic12Bias, 0x70) && write(context, kMic34Bias, 0x70)
            && write(context, kOsr, 0x20)
            && write(context, kMainClock, 0xC1) // MCLK = 256 x LRCK
            && update(context, kSdpInterface1, 0xE0, 0x60) // 16-bit words
            && update(context, kSdpInterface1, 0x03, 0x00); // Philips I2S framing
        if (!ok) {
            ESP_LOGW(kTag, "ES7210 not responding at 0x%02X", context.address);
            return false;
        }
        context.available = true;
        ESP_LOGI(kTag, "Microphone ADC ready");
        return true;
    }

    bool start(Context& context, uint8_t gainIndex) {
        if (!context.available)
            return false;
        gainIndex = static_cast<uint8_t>(std::min<uint8_t>(gainIndex, 11));
        bool ok = selectMicrophones(context, gainIndex);
        uint8_t clockOff = 0;
        ok &= read(context, kClockOff, clockOff);
        ok &= write(context, kClockOff, clockOff) && write(context, kPowerDown, 0x00) && write(context, kAnalog, 0x43);
        for (uint8_t input = 0; input < 4; ++input)
            ok &= write(context, static_cast<uint8_t>(kMic1Power + input), 0x08);
        ok &= selectMicrophones(context, gainIndex) && write(context, kAnalog, 0x43) && write(context, kReset, 0x71)
            && write(context, kReset, 0x41);
        context.running = ok;
        if (!ok)
            ESP_LOGW(kTag, "ES7210 start failed");
        return ok;
    }

    void stop(Context& context) {
        if (!context.available)
            return;
        for (uint8_t input = 0; input < 4; ++input)
            write(context, static_cast<uint8_t>(kMic1Power + input), 0xFF);
        write(context, kMic12Power, 0xFF);
        write(context, kMic34Power, 0xFF);
        write(context, kAnalog, 0xC0);
        write(context, kClockOff, 0x7F);
        write(context, kPowerDown, 0x07);
        context.running = false;
    }

} // namespace BoardDrivers::Es7210
