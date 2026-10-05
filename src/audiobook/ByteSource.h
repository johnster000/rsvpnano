#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace audiobook {

    // Random-access bytes of one file: an SD card file on the reader, memory in host tests.
    class ByteSource {
    public:
        virtual ~ByteSource() = default;
        virtual uint64_t size() const = 0;
        // Reads up to output.size() bytes at offset and returns how many were read.
        virtual size_t readAt(uint64_t offset, std::span<uint8_t> output) = 0;
    };

    class MemorySource final : public ByteSource {
    public:
        explicit MemorySource(std::span<const uint8_t> bytes) : bytes_(bytes) {}

        uint64_t size() const override {
            return bytes_.size();
        }

        size_t readAt(uint64_t offset, std::span<uint8_t> output) override {
            if (offset >= bytes_.size())
                return 0;
            const size_t count = std::min<size_t>(output.size(), bytes_.size() - static_cast<size_t>(offset));
            std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset), count, output.begin());
            return count;
        }

    private:
        std::span<const uint8_t> bytes_;
    };

} // namespace audiobook
