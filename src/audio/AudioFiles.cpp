#include "audio/AudioFiles.h"

#include <algorithm>
#include <charconv>
#include <cstdio>

namespace audio {
    namespace {

        void put16(uint8_t* out, uint16_t value) {
            out[0] = static_cast<uint8_t>(value);
            out[1] = static_cast<uint8_t>(value >> 8);
        }

        void put32(uint8_t* out, uint32_t value) {
            for (int shift = 0; shift < 32; shift += 8)
                *out++ = static_cast<uint8_t>(value >> shift);
        }

        uint16_t get16(std::span<const uint8_t> bytes, size_t at) {
            return static_cast<uint16_t>(bytes[at] | (bytes[at + 1] << 8));
        }

        uint32_t get32(std::span<const uint8_t> bytes, size_t at) {
            return static_cast<uint32_t>(bytes[at]) | (static_cast<uint32_t>(bytes[at + 1]) << 8)
                 | (static_cast<uint32_t>(bytes[at + 2]) << 16) | (static_cast<uint32_t>(bytes[at + 3]) << 24);
        }

        bool tagIs(std::span<const uint8_t> bytes, size_t at, std::string_view tag) {
            return std::equal(tag.begin(), tag.end(), bytes.begin() + static_cast<std::ptrdiff_t>(at));
        }

        char lower(char value) {
            return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
        }

        bool endsWith(std::string_view value, std::string_view suffix) {
            if (value.size() < suffix.size())
                return false;
            return std::equal(suffix.begin(), suffix.end(), value.end() - static_cast<std::ptrdiff_t>(suffix.size()),
                              [](char left, char right) { return lower(left) == lower(right); });
        }

        std::string_view baseName(std::string_view path) {
            const size_t slash = path.find_last_of('/');
            return slash == std::string_view::npos ? path : path.substr(slash + 1);
        }

    } // namespace

    std::array<uint8_t, kWavHeaderBytes> wavHeader(uint32_t sampleRate, uint16_t channels, uint32_t dataBytes) {
        std::array<uint8_t, kWavHeaderBytes> header{};
        const uint16_t blockAlign = static_cast<uint16_t>(channels * 2);
        std::copy_n("RIFF", 4, header.begin());
        put32(&header[4], 36 + dataBytes);
        std::copy_n("WAVEfmt ", 8, header.begin() + 8);
        put32(&header[16], 16);
        put16(&header[20], 1);
        put16(&header[22], channels);
        put32(&header[24], sampleRate);
        put32(&header[28], sampleRate * blockAlign);
        put16(&header[32], blockAlign);
        put16(&header[34], 16);
        std::copy_n("data", 4, header.begin() + 36);
        put32(&header[40], dataBytes);
        return header;
    }

    std::expected<WavInfo, std::string> parseWavHeader(std::span<const uint8_t> prefix, uint64_t fileSize) {
        if (prefix.size() < 12 || !tagIs(prefix, 0, "RIFF") || !tagIs(prefix, 8, "WAVE"))
            return std::unexpected("Not a WAV file");
        WavInfo info;
        bool format = false;
        size_t at = 12;
        while (at + 8 <= prefix.size()) {
            const uint32_t size = get32(prefix, at + 4);
            if (tagIs(prefix, at, "fmt ")) {
                if (size < 16 || at + 8 + 16 > prefix.size())
                    return std::unexpected("Short format chunk");
                if (get16(prefix, at + 8) != 1)
                    return std::unexpected("Only PCM WAV is supported");
                info.channels = get16(prefix, at + 10);
                info.sampleRate = get32(prefix, at + 12);
                info.bitsPerSample = get16(prefix, at + 22);
                format = true;
            } else if (tagIs(prefix, at, "data")) {
                if (!format)
                    return std::unexpected("Data before format");
                if (info.bitsPerSample != 16 || info.channels == 0 || info.channels > 2 || info.sampleRate == 0)
                    return std::unexpected("Unsupported sample format");
                info.dataOffset = static_cast<uint32_t>(at + 8);
                const uint64_t available = fileSize > info.dataOffset ? fileSize - info.dataOffset : 0;
                info.dataBytes = size == 0 || size > available ? static_cast<uint32_t>(available) : size;
                info.dataBytes -= info.dataBytes % (info.channels * 2U);
                return info;
            }
            at += 8 + size + (size & 1U);
        }
        return std::unexpected("No data chunk");
    }

    uint64_t wavDurationMs(const WavInfo& info) {
        const uint32_t bytesPerSecond = info.sampleRate * info.channels * 2U;
        return bytesPerSecond == 0 ? 0 : static_cast<uint64_t>(info.dataBytes) * 1000U / bytesPerSecond;
    }

    bool isAudiobookFile(std::string_view name) {
        const std::string_view base = baseName(name);
        if (base.empty() || base.front() == '.')
            return false;
        return endsWith(base, ".m4b") || endsWith(base, ".m4a") || endsWith(base, ".mp4");
    }

    std::string displayName(std::string_view path) {
        std::string_view base = baseName(path);
        const size_t dot = base.find_last_of('.');
        if (dot != std::string_view::npos && dot > 0)
            base = base.substr(0, dot);
        std::string name{base};
        std::ranges::replace(name, '_', ' ');
        return name;
    }

    std::optional<uint32_t> memoNumber(std::string_view name) {
        name = baseName(name);
        constexpr std::string_view prefix = "memo-";
        if (name.size() <= prefix.size() + 4 || !endsWith(name, ".wav")
            || !std::equal(prefix.begin(), prefix.end(), name.begin()))
            return std::nullopt;
        const std::string_view digits = name.substr(prefix.size(), name.size() - prefix.size() - 4);
        uint32_t number = 0;
        const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), number);
        if (error != std::errc{} || end != digits.data() + digits.size() || number == 0)
            return std::nullopt;
        return number;
    }

    std::string memoFileName(uint32_t number) {
        char name[24];
        std::snprintf(name, sizeof(name), "memo-%04u.wav", static_cast<unsigned>(number));
        return name;
    }

    std::optional<SavedPosition> parsePosition(std::string_view text) {
        SavedPosition saved;
        const char* cursor = text.data();
        const char* end = text.data() + text.size();
        auto [next, error] = std::from_chars(cursor, end, saved.positionMs);
        if (error != std::errc{})
            return std::nullopt;
        cursor = next;
        while (cursor < end && *cursor == ' ')
            ++cursor;
        if (cursor < end) {
            auto [after, durationError] = std::from_chars(cursor, end, saved.durationMs);
            if (durationError != std::errc{})
                saved.durationMs = 0;
            (void) after;
        }
        if (saved.durationMs > 0)
            saved.positionMs = std::min(saved.positionMs, saved.durationMs);
        return saved;
    }

    std::string formatPosition(const SavedPosition& position) {
        return std::to_string(position.positionMs) + " " + std::to_string(position.durationMs) + "\n";
    }

    std::string positionPathFor(std::string_view bookPath) {
        return std::string{kPositionsPath} + "/" + std::string{baseName(bookPath)} + ".pos";
    }

    uint8_t percentListened(const SavedPosition& position) {
        if (position.durationMs == 0)
            return 0;
        return static_cast<uint8_t>(std::min<uint64_t>(100, position.positionMs * 100 / position.durationMs));
    }

    std::string formatClock(uint64_t ms) {
        const uint64_t seconds = ms / 1000;
        const unsigned hours = static_cast<unsigned>(seconds / 3600);
        const unsigned minutes = static_cast<unsigned>((seconds / 60) % 60);
        const unsigned rest = static_cast<unsigned>(seconds % 60);
        char text[16];
        if (hours > 0)
            std::snprintf(text, sizeof(text), "%u:%02u:%02u", hours, minutes, rest);
        else
            std::snprintf(text, sizeof(text), "%u:%02u", minutes, rest);
        return text;
    }

} // namespace audio
