#include "board/BoardAudio.h"

#include <Wire.h>
#include <driver/i2s_std.h>
#include <driver/i2s_tdm.h>
#include <esp_log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

#include "board/BoardPower.h"
#include "drivers/audio/es7210/Es7210.h"
#include "drivers/audio/es8311/Es8311.h"
#include "platforms/waveshare_lcd_349/WaveshareLcd349.h"

// The ES8311 speaker codec and the ES7210 microphone ADC share one I2S bus. Playback runs it as stereo
// Philips I2S at the stream's rate; recording runs it as four 16-bit TDM slots at 16 kHz. Both keep
// MCLK at 256 x LRCK, which is what the codecs' clock dividers are set for.
namespace {

    constexpr char kTag[] = "audio";
    constexpr uint32_t kBeepRateHz = 16000;
    constexpr uint32_t kBeepFrequencyHz = 1320;
    constexpr uint32_t kBeepMs = 120;
    constexpr int16_t kBeepAmplitude = 9000;
    constexpr uint32_t kRailSettleMs = 15;
    constexpr size_t kInputSlots = 4;
    constexpr size_t kInputFrames = 256;
    constexpr uint32_t kSlotReportFrames = 16000;

    BoardDrivers::Es8311::Context gCodec{
        Wire1,
        WaveshareLcd349::AudioWiring::kEs8311Address,
        I2S_NUM_0,
        WaveshareLcd349::AudioWiring::kMclkPin,
        WaveshareLcd349::AudioWiring::kBclkPin,
        WaveshareLcd349::AudioWiring::kWsPin,
        WaveshareLcd349::AudioWiring::kDoutPin,
    };
    BoardDrivers::Es7210::Context gMics{Wire1, WaveshareLcd349::AudioWiring::kEs7210Address};

    enum class Bus : uint8_t {
        None,
        Output,
        Input,
    };

    i2s_chan_handle_t gTx = nullptr;
    i2s_chan_handle_t gRx = nullptr;
    Bus gBus = Bus::None;
    uint32_t gRate = 0;
    bool gRailOn = false;
    uint8_t gVolumeLevel = 0xBF;
    std::array<int16_t, kInputFrames * kInputSlots> gInputFrames{};
    std::array<uint64_t, kInputSlots> gSlotEnergy{};
    uint32_t gSlotFrames = 0;

    constexpr gpio_num_t pin(int value) {
        return value < 0 ? I2S_GPIO_UNUSED : static_cast<gpio_num_t>(value);
    }

    void closeBus() {
        for (i2s_chan_handle_t* channel: {&gTx, &gRx}) {
            if (*channel == nullptr)
                continue;
            i2s_channel_disable(*channel);
            i2s_del_channel(*channel);
            *channel = nullptr;
        }
        gBus = Bus::None;
        gRate = 0;
    }

    bool railOn() {
        if (!gRailOn) {
            gRailOn = Board::Power::enableAudioPowerIfAvailable();
            delay(kRailSettleMs);
        }
        return gRailOn;
    }

    bool openOutputBus(uint32_t rate) {
        if (gBus == Bus::Output && gRate == rate)
            return true;
        closeBus();
        i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        // About 90 ms at 44.1 kHz, so an SD card pause does not starve the speaker.
        channel.dma_desc_num = 8;
        channel.dma_frame_num = 480;
        channel.auto_clear = true;
        if (i2s_new_channel(&channel, &gTx, nullptr) != ESP_OK) {
            ESP_LOGW(kTag, "no I2S TX channel");
            return false;
        }
        const i2s_std_config_t config = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
            .gpio_cfg =
                {
                    .mclk = pin(WaveshareLcd349::AudioWiring::kMclkPin),
                    .bclk = pin(WaveshareLcd349::AudioWiring::kBclkPin),
                    .ws = pin(WaveshareLcd349::AudioWiring::kWsPin),
                    .dout = pin(WaveshareLcd349::AudioWiring::kDoutPin),
                    .din = I2S_GPIO_UNUSED,
                    .invert_flags = {},
                },
        };
        if (i2s_channel_init_std_mode(gTx, &config) != ESP_OK || i2s_channel_enable(gTx) != ESP_OK) {
            ESP_LOGW(kTag, "I2S output at %u Hz failed", static_cast<unsigned>(rate));
            closeBus();
            return false;
        }
        gBus = Bus::Output;
        gRate = rate;
        return true;
    }

    bool openInputBus(uint32_t rate) {
        if (gBus == Bus::Input && gRate == rate)
            return true;
        closeBus();
        i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        channel.dma_desc_num = 6;
        channel.dma_frame_num = kInputFrames;
        if (i2s_new_channel(&channel, nullptr, &gRx) != ESP_OK) {
            ESP_LOGW(kTag, "no I2S RX channel");
            return false;
        }
        i2s_tdm_config_t config = {
            .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(rate),
            .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(
                I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO,
                static_cast<i2s_tdm_slot_mask_t>(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3)),
            .gpio_cfg =
                {
                    .mclk = pin(WaveshareLcd349::AudioWiring::kMclkPin),
                    .bclk = pin(WaveshareLcd349::AudioWiring::kBclkPin),
                    .ws = pin(WaveshareLcd349::AudioWiring::kWsPin),
                    .dout = I2S_GPIO_UNUSED,
                    .din = pin(WaveshareLcd349::AudioWiring::kDinPin),
                    .invert_flags = {},
                },
        };
        config.slot_cfg.total_slot = kInputSlots;
        if (i2s_channel_init_tdm_mode(gRx, &config) != ESP_OK || i2s_channel_enable(gRx) != ESP_OK) {
            ESP_LOGW(kTag, "I2S input at %u Hz failed", static_cast<unsigned>(rate));
            closeBus();
            return false;
        }
        gBus = Bus::Input;
        gRate = rate;
        return true;
    }

    // The codec's registers need MCLK running, so its first setup happens with the output bus open.
    bool ensureCodec(uint32_t rate) {
        if (!railOn() || !openOutputBus(rate))
            return false;
        return BoardDrivers::Es8311::beginCodec(gCodec);
    }

} // namespace

namespace Board::Audio {

    bool begin() {
        return ensureCodec(kBeepRateHz);
    }

    bool available() {
        return BoardDrivers::Es8311::available(gCodec);
    }

    bool startOutput(uint32_t sampleRateHz) {
        if (gMics.running)
            BoardDrivers::Es7210::stop(gMics);
        if (!ensureCodec(sampleRateHz) || !BoardDrivers::Es8311::startDac(gCodec))
            return false;
        BoardDrivers::Es8311::setDacVolume(gCodec, gVolumeLevel);
        return true;
    }

    size_t writeFrames(const int16_t* frames, size_t count, uint32_t timeoutMs) {
        if (gBus != Bus::Output || frames == nullptr || count == 0)
            return 0;
        size_t written = 0;
        i2s_channel_write(gTx, frames, count * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(timeoutMs));
        return written / (2 * sizeof(int16_t));
    }

    void stopOutput() {
        BoardDrivers::Es8311::muteDac(gCodec);
        if (gBus == Bus::Output)
            closeBus();
    }

    void setVolume(uint8_t percent) {
        // 0 mutes; otherwise -36 dB at 1% up to +12 dB at 100%, in the DAC's half-decibel steps.
        percent = std::min<uint8_t>(percent, 100);
        gVolumeLevel = percent == 0 ? 0 : static_cast<uint8_t>(0x77 + percent * 96 / 100);
        if (gBus == Bus::Output)
            BoardDrivers::Es8311::setDacVolume(gCodec, gVolumeLevel);
    }

    bool hasMicrophone() {
        // Probed once, with the audio rail up, so screens can ask every frame without bus traffic.
        static int8_t present = -1;
        if (present < 0) {
            railOn();
            Wire1.beginTransmission(gMics.address);
            present = Wire1.endTransmission() == 0 ? 1 : 0;
            ESP_LOGI(kTag, "microphone ADC %s", present ? "found" : "not found");
        }
        return present > 0;
    }

    bool startInput(uint32_t sampleRateHz) {
        if (!railOn())
            return false;
        BoardDrivers::Es8311::muteDac(gCodec);
        if (!openInputBus(sampleRateHz))
            return false;
        if (!BoardDrivers::Es7210::begin(gMics) || !BoardDrivers::Es7210::start(gMics)) {
            closeBus();
            return false;
        }
        gSlotEnergy.fill(0);
        gSlotFrames = 0;
        return true;
    }

    size_t readMono(int16_t* samples, size_t count, uint32_t timeoutMs) {
        if (gBus != Bus::Input || samples == nullptr || count == 0)
            return 0;
        count = std::min(count, kInputFrames);
        size_t bytes = 0;
        i2s_channel_read(gRx, gInputFrames.data(), count * kInputSlots * sizeof(int16_t), &bytes,
                         pdMS_TO_TICKS(timeoutMs));
        const size_t frames = bytes / (kInputSlots * sizeof(int16_t));
        for (size_t frame = 0; frame < frames; ++frame) {
            const int16_t* slots = &gInputFrames[frame * kInputSlots];
            int32_t sum = 0;
            for (size_t slot = 0; slot < kInputSlots; ++slot) {
                sum += slots[slot];
                if (gSlotFrames < kSlotReportFrames)
                    gSlotEnergy[slot] += static_cast<uint64_t>(static_cast<int32_t>(slots[slot]) * slots[slot]);
            }
            // Two of the four inputs carry microphones; halving the sum averages them.
            samples[frame] = static_cast<int16_t>(std::clamp<int32_t>(sum / 2, INT16_MIN, INT16_MAX));
        }
        if (gSlotFrames < kSlotReportFrames && gSlotFrames + frames >= kSlotReportFrames) {
            // One line per recording shows which inputs the microphones are on.
            ESP_LOGI(kTag, "mic slot rms %u %u %u %u",
                     static_cast<unsigned>(std::sqrt(static_cast<double>(gSlotEnergy[0]) / kSlotReportFrames)),
                     static_cast<unsigned>(std::sqrt(static_cast<double>(gSlotEnergy[1]) / kSlotReportFrames)),
                     static_cast<unsigned>(std::sqrt(static_cast<double>(gSlotEnergy[2]) / kSlotReportFrames)),
                     static_cast<unsigned>(std::sqrt(static_cast<double>(gSlotEnergy[3]) / kSlotReportFrames)));
        }
        gSlotFrames += static_cast<uint32_t>(frames);
        return frames;
    }

    void stopInput() {
        BoardDrivers::Es7210::stop(gMics);
        if (gBus == Bus::Input)
            closeBus();
    }

    bool beep() {
        if (gBus == Bus::Input || (gBus == Bus::Output && gRate != kBeepRateHz))
            return false;
        if (!startOutput(kBeepRateHz))
            return false;
        constexpr uint32_t halfPeriod = kBeepRateHz / (kBeepFrequencyHz * 2);
        std::array<int16_t, 2 * 160> block{};
        const uint32_t total = kBeepRateHz * kBeepMs / 1000;
        for (uint32_t frame = 0; frame < total; frame += block.size() / 2) {
            const uint32_t frames = std::min<uint32_t>(block.size() / 2, total - frame);
            for (uint32_t index = 0; index < frames; ++index) {
                const int16_t value = ((frame + index) / halfPeriod) % 2 == 0 ? kBeepAmplitude : -kBeepAmplitude;
                block[index * 2] = value;
                block[index * 2 + 1] = value;
            }
            writeFrames(block.data(), frames, 250);
        }
        return true;
    }

} // namespace Board::Audio
