#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "audiobook/ByteSource.h"

namespace audiobook {

    struct Chapter {
        uint64_t startMs = 0;
        std::string title;
    };

    // AAC stream parameters from the MP4 sample description.
    struct AudioFormat {
        uint8_t objectType = 0; // 2 = AAC-LC; 5 and 29 signal SBR (HE-AAC) explicitly.
        uint32_t sampleRate = 0; // Core rate; implicit SBR doubles the decoded rate.
        uint8_t channels = 0;
        std::vector<uint8_t> decoderConfig; // AudioSpecificConfig
    };

    struct Sample {
        uint32_t index = 0;
        uint64_t offset = 0;
        uint32_t size = 0;
    };

    // Reads one AAC track from an MP4/M4A/M4B file. Sample tables are read from the source on demand,
    // so memory stays bounded for books with millions of frames.
    class Mp4Audio {
    public:
        std::expected<void, std::string> open(ByteSource& source);

        const AudioFormat& format() const {
            return format_;
        }
        const std::vector<Chapter>& chapters() const {
            return chapters_;
        }
        const std::string& title() const {
            return title_;
        }
        const std::string& author() const {
            return author_;
        }
        uint32_t sampleCount() const {
            return sampleCount_;
        }
        uint64_t durationMs() const;
        uint64_t msAtSample(uint32_t index);
        uint32_t sampleAtMs(uint64_t ms);
        size_t chapterAtMs(uint64_t ms) const;

        // Positions the sequential reader; next() then returns samples in order.
        bool seek(uint32_t index);
        std::optional<Sample> next();

    private:
        // Fixed-size big-endian table entries read lazily through a small window.
        class Table {
        public:
            void reset(ByteSource* source, uint64_t offset, uint32_t count, uint8_t fieldBytes, uint8_t fields);
            uint32_t count() const {
                return count_;
            }
            bool entry(uint32_t index, std::array<uint64_t, 3>& fields);

        private:
            static constexpr uint32_t kWindowEntries = 128;
            ByteSource* source_ = nullptr;
            uint64_t offset_ = 0;
            uint32_t count_ = 0;
            uint8_t fieldBytes_ = 0;
            uint8_t fields_ = 0;
            uint32_t windowStart_ = UINT32_MAX;
            uint32_t windowCount_ = 0;
            std::vector<uint8_t> window_;
        };

        struct TrackTables {
            uint32_t trackId = 0;
            uint32_t timescale = 0;
            uint64_t duration = 0;
            uint32_t handler = 0;
            uint32_t chapterTrackId = 0;
            uint32_t uniformSampleSize = 0;
            uint32_t sampleCount = 0;
            bool largeChunkOffsets = false;
            uint64_t sampleDescription = 0;
            uint64_t sampleDescriptionSize = 0;
            uint64_t timeToSample = 0;
            uint32_t timeToSampleCount = 0;
            uint64_t sampleToChunk = 0;
            uint32_t sampleToChunkCount = 0;
            uint64_t sampleSizes = 0;
            uint64_t chunkOffsets = 0;
            uint32_t chunkCount = 0;
        };

        struct Cursor {
            uint32_t sample = 0;
            uint32_t chunk = 0; // zero-based
            uint32_t chunkFirstSample = 0;
            uint32_t chunkSamples = 0;
            uint32_t run = 0; // stsc entry for chunk
            uint64_t offset = 0; // file offset of the next sample
        };

        std::expected<void, std::string> parseMovie(uint64_t offset, uint64_t end);
        std::expected<void, std::string> parseTrack(uint64_t offset, uint64_t end, TrackTables& track);
        std::expected<void, std::string> parseAudioDescription(const TrackTables& track);
        void parseMetadata(uint64_t offset, uint64_t end);
        void parseNeroChapters(uint64_t offset, uint64_t end);
        void readTrackChapters(const TrackTables& track);
        bool chunkRun(uint32_t run, uint32_t& firstChunk, uint32_t& samplesPerChunk);
        bool sampleSize(uint32_t index, uint32_t& size);
        bool chunkOffset(uint32_t chunk, uint64_t& offset);
        bool readBytes(uint64_t offset, std::span<uint8_t> output);

        ByteSource* source_ = nullptr;
        TrackTables audio_;
        std::vector<TrackTables> tracks_;
        Table timeToSample_;
        Table sampleToChunk_;
        Table sampleSizes_;
        Table chunkOffsets_;
        Cursor cursor_;
        AudioFormat format_;
        std::vector<Chapter> chapters_;
        std::string title_;
        std::string author_;
        uint32_t sampleCount_ = 0;
    };

} // namespace audiobook
