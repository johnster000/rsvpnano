#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// File-level rules for audiobooks and voice memos that need no hardware: WAV headers, names and saved positions.
namespace audio {

    inline constexpr const char* kAudiobooksPath = "/audiobooks";
    inline constexpr const char* kPositionsPath = "/audiobooks/.positions";
    inline constexpr const char* kMemosPath = "/voice";
    inline constexpr uint32_t kMemoSampleRateHz = 16000;

    // 16-bit PCM WAV layout.
    struct WavInfo {
        uint32_t sampleRate = 0;
        uint16_t channels = 0;
        uint16_t bitsPerSample = 0;
        uint32_t dataOffset = 0;
        uint32_t dataBytes = 0;
    };

    inline constexpr size_t kWavHeaderBytes = 44;
    std::array<uint8_t, kWavHeaderBytes> wavHeader(uint32_t sampleRate, uint16_t channels, uint32_t dataBytes);
    // Reads the RIFF chunks in `prefix` (the start of the file) up to the data chunk. A data size of zero
    // or beyond the file, as left by a recording cut off by power loss, is replaced by `fileSize`.
    std::expected<WavInfo, std::string> parseWavHeader(std::span<const uint8_t> prefix, uint64_t fileSize);
    uint64_t wavDurationMs(const WavInfo& info);

    bool isAudiobookFile(std::string_view name);
    // File name without folders or extension, with underscores read as spaces.
    std::string displayName(std::string_view path);

    // Memos are memo-0001.wav, memo-0002.wav, ...; other files in the folder are left alone.
    std::optional<uint32_t> memoNumber(std::string_view name);
    std::string memoFileName(uint32_t number);

    struct SavedPosition {
        uint64_t positionMs = 0;
        uint64_t durationMs = 0;
    };
    std::optional<SavedPosition> parsePosition(std::string_view text);
    std::string formatPosition(const SavedPosition& position);
    std::string positionPathFor(std::string_view bookPath);
    uint8_t percentListened(const SavedPosition& position);

    // 0:42, 12:05, 1:02:03.
    std::string formatClock(uint64_t ms);

} // namespace audio
