#include <unity.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <vector>

#include "audiobook/AacDecoder.h"
#include "audiobook/Mp4Audio.h"

namespace {

    std::vector<uint8_t> readFixture(const char* name) {
        std::ifstream file(std::string{"test/fixtures/audiobook/"} + name, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    // Renames every occurrence of one box type, to hide a feature from the parser.
    void renameBox(std::vector<uint8_t>& bytes, const char* from, const char* to) {
        for (size_t index = 0; index + 4 <= bytes.size(); ++index) {
            if (std::memcmp(bytes.data() + index, from, 4) == 0)
                std::memcpy(bytes.data() + index, to, 4);
        }
    }

    struct Decoded {
        std::vector<int16_t> pcm;
        uint32_t sampleRate = 0;
        uint8_t channels = 0;
    };

    Decoded decodeFrom(audiobook::Mp4Audio& audio, audiobook::ByteSource& source, uint32_t first, uint32_t count) {
        audiobook::AacDecoder decoder;
        TEST_ASSERT_TRUE(decoder.begin(audio.format()).has_value());
        TEST_ASSERT_TRUE(audio.seek(first));
        Decoded result;
        std::vector<int16_t> frame(audiobook::AacDecoder::kMaxFrameSamples);
        std::vector<uint8_t> bytes;
        for (uint32_t decoded = 0; decoded < count; ++decoded) {
            const auto sample = audio.next();
            if (!sample)
                break;
            bytes.resize(sample->size);
            TEST_ASSERT_EQUAL(sample->size, source.readAt(sample->offset, bytes));
            const auto output = decoder.decode(bytes, frame);
            TEST_ASSERT_TRUE_MESSAGE(output.has_value(), output ? "" : output.error().c_str());
            result.sampleRate = output->sampleRate;
            result.channels = output->channels;
            result.pcm.insert(result.pcm.end(), frame.begin(), frame.begin() + output->samples);
        }
        return result;
    }

    // Zero crossings per second of the first channel, halved: the tone's frequency.
    int toneFrequency(const Decoded& decoded, size_t firstFrame, size_t frames) {
        size_t crossings = 0;
        for (size_t frame = firstFrame + 1; frame < firstFrame + frames; ++frame) {
            const int16_t previous = decoded.pcm[(frame - 1) * decoded.channels];
            const int16_t current = decoded.pcm[frame * decoded.channels];
            crossings += (previous < 0) != (current < 0);
        }
        return static_cast<int>(std::lround(crossings * static_cast<double>(decoded.sampleRate) / (2.0 * frames)));
    }

    void append32(std::vector<uint8_t>& out, uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8)
            out.push_back(static_cast<uint8_t>(value >> shift));
    }

    void append64(std::vector<uint8_t>& out, uint64_t value) {
        append32(out, static_cast<uint32_t>(value >> 32));
        append32(out, static_cast<uint32_t>(value));
    }

    std::vector<uint8_t> box(const char* type, const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> out;
        append32(out, static_cast<uint32_t>(payload.size() + 8));
        out.insert(out.end(), type, type + 4);
        out.insert(out.end(), payload.begin(), payload.end());
        return out;
    }

    std::vector<uint8_t> concat(std::initializer_list<std::vector<uint8_t>> parts) {
        std::vector<uint8_t> out;
        for (const auto& part: parts)
            out.insert(out.end(), part.begin(), part.end());
        return out;
    }

    struct SyntheticMovie {
        std::vector<uint8_t> bytes;
        std::vector<audiobook::Sample> samples;
    };

    // An AAC-LC 22.05 kHz mono track whose chunks hold 7, then 3, then 13 samples, with gaps between chunks:
    // enough frames to cross many lazy-table windows and chunk-run boundaries.
    SyntheticMovie syntheticMovie(bool largeOffsets) {
        struct Run {
            uint32_t chunks, perChunk;
        };
        constexpr Run runs[]{{39, 7}, {50, 3}, {81, 13}};
        SyntheticMovie movie;
        std::vector<uint8_t> media;
        std::vector<uint64_t> chunkOffsets;
        std::vector<uint32_t> sizes;
        const std::vector<uint8_t> ftyp = box("ftyp", {'M', '4', 'B', ' ', 0, 0, 2, 0});
        const uint64_t mediaStart = ftyp.size() + 8;
        uint32_t chunk = 0;
        for (const Run run: runs) {
            for (uint32_t index = 0; index < run.chunks; ++index, ++chunk) {
                media.insert(media.end(), chunk % 5, 0xEE);
                chunkOffsets.push_back(mediaStart + media.size());
                for (uint32_t inChunk = 0; inChunk < run.perChunk; ++inChunk) {
                    const auto sample = static_cast<uint32_t>(sizes.size());
                    const uint32_t size = 100 + (sample * 37) % 200;
                    movie.samples.push_back({sample, mediaStart + media.size(), size});
                    sizes.push_back(size);
                    media.insert(media.end(), size, static_cast<uint8_t>(sample));
                }
            }
        }

        std::vector<uint8_t> esdsPayload{0, 0, 0, 0, 0x03, 25, 0, 1, 0, 0x04, 17, 0x40, 0x15};
        esdsPayload.insert(esdsPayload.end(), 11, 0);
        esdsPayload.insert(esdsPayload.end(), {0x05, 2, 0x13, 0x88, 0x06, 1, 0x02});
        std::vector<uint8_t> entry(6, 0);
        entry.insert(entry.end(), {0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 16, 0, 0, 0, 0, 0x56, 0x22, 0, 0});
        const auto mp4a = box("mp4a", concat({entry, box("esds", esdsPayload)}));
        std::vector<uint8_t> stsd{0, 0, 0, 0};
        append32(stsd, 1);
        stsd.insert(stsd.end(), mp4a.begin(), mp4a.end());
        std::vector<uint8_t> stts{0, 0, 0, 0};
        append32(stts, 1);
        append32(stts, static_cast<uint32_t>(sizes.size()));
        append32(stts, 1024);
        std::vector<uint8_t> stsc{0, 0, 0, 0};
        append32(stsc, 3);
        uint32_t firstChunk = 1;
        for (const Run run: runs) {
            append32(stsc, firstChunk);
            append32(stsc, run.perChunk);
            append32(stsc, 1);
            firstChunk += run.chunks;
        }
        std::vector<uint8_t> stsz{0, 0, 0, 0, 0, 0, 0, 0};
        append32(stsz, static_cast<uint32_t>(sizes.size()));
        for (const uint32_t size: sizes)
            append32(stsz, size);
        std::vector<uint8_t> offsets{0, 0, 0, 0};
        append32(offsets, static_cast<uint32_t>(chunkOffsets.size()));
        for (const uint64_t offset: chunkOffsets)
            largeOffsets ? append64(offsets, offset) : append32(offsets, static_cast<uint32_t>(offset));
        std::vector<uint8_t> mdhd{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        append32(mdhd, 22050);
        append32(mdhd, static_cast<uint32_t>(sizes.size() * 1024));
        append32(mdhd, 0);
        std::vector<uint8_t> hdlr{0, 0, 0, 0, 0, 0, 0, 0, 's', 'o', 'u', 'n'};
        hdlr.insert(hdlr.end(), 13, 0);
        std::vector<uint8_t> tkhd(12, 0);
        append32(tkhd, 1);
        tkhd.resize(84, 0);
        const auto stbl = box("stbl", concat({box("stsd", stsd), box("stts", stts), box("stsc", stsc),
                                              box("stsz", stsz), box(largeOffsets ? "co64" : "stco", offsets)}));
        const auto trak = box("trak", concat({box("tkhd", tkhd), box("mdia", concat({box("mdhd", mdhd), box("hdlr", hdlr),
                                                                                     box("minf", stbl)}))}));
        movie.bytes = concat({ftyp, box("mdat", media), box("moov", trak)});
        return movie;
    }

} // namespace

void setUp() {}
void tearDown() {}

void test_reads_format_metadata_and_chapters() {
    for (const char* name: {"tones-mono-22k.m4b", "tones-mono-22k-faststart.m4b"}) {
        const auto bytes = readFixture(name);
        TEST_ASSERT_FALSE(bytes.empty());
        audiobook::MemorySource source{bytes};
        audiobook::Mp4Audio audio;
        const auto opened = audio.open(source);
        TEST_ASSERT_TRUE_MESSAGE(opened.has_value(), opened ? name : opened.error().c_str());
        TEST_ASSERT_EQUAL(2, audio.format().objectType);
        TEST_ASSERT_EQUAL(22050, audio.format().sampleRate);
        TEST_ASSERT_EQUAL(1, audio.format().channels);
        TEST_ASSERT_EQUAL_STRING("Test Audiobook", audio.title().c_str());
        TEST_ASSERT_EQUAL_STRING("RSVP Nano", audio.author().c_str());
        TEST_ASSERT_UINT64_WITHIN(60, 3000, audio.durationMs());
        TEST_ASSERT_EQUAL(3, audio.chapters().size());
        TEST_ASSERT_EQUAL_STRING("Opening", audio.chapters()[0].title.c_str());
        TEST_ASSERT_EQUAL_STRING("The Middle", audio.chapters()[1].title.c_str());
        TEST_ASSERT_EQUAL_STRING("Ending", audio.chapters()[2].title.c_str());
        TEST_ASSERT_UINT64_WITHIN(30, 1000, audio.chapters()[1].startMs);
        TEST_ASSERT_UINT64_WITHIN(30, 2000, audio.chapters()[2].startMs);
        TEST_ASSERT_EQUAL(1, audio.chapterAtMs(1500));
    }
}

void test_both_chapter_formats_are_read_on_their_own() {
    auto neroOnly = readFixture("tones-mono-22k.m4b");
    renameBox(neroOnly, "chap", "xxxx");
    auto trackOnly = readFixture("tones-mono-22k.m4b");
    renameBox(trackOnly, "chpl", "free");
    for (auto* bytes: {&neroOnly, &trackOnly}) {
        audiobook::MemorySource source{*bytes};
        audiobook::Mp4Audio audio;
        TEST_ASSERT_TRUE(audio.open(source).has_value());
        TEST_ASSERT_EQUAL(3, audio.chapters().size());
        TEST_ASSERT_EQUAL_STRING("The Middle", audio.chapters()[1].title.c_str());
        TEST_ASSERT_UINT64_WITHIN(30, 1000, audio.chapters()[1].startMs);
    }
}

void test_header_position_does_not_change_samples() {
    const auto tail = readFixture("tones-mono-22k.m4b");
    const auto head = readFixture("tones-mono-22k-faststart.m4b");
    audiobook::MemorySource tailSource{tail}, headSource{head};
    audiobook::Mp4Audio tailAudio, headAudio;
    TEST_ASSERT_TRUE(tailAudio.open(tailSource).has_value());
    TEST_ASSERT_TRUE(headAudio.open(headSource).has_value());
    TEST_ASSERT_EQUAL(tailAudio.sampleCount(), headAudio.sampleCount());
    TEST_ASSERT_GREATER_THAN(50, tailAudio.sampleCount());
    while (const auto expected = tailAudio.next()) {
        const auto actual = headAudio.next();
        TEST_ASSERT_TRUE(actual.has_value());
        TEST_ASSERT_EQUAL(expected->size, actual->size);
        TEST_ASSERT_EQUAL_MEMORY(tail.data() + expected->offset, head.data() + actual->offset, expected->size);
    }
    TEST_ASSERT_FALSE(headAudio.next().has_value());
}

void test_decodes_each_chapter_tone() {
    const auto bytes = readFixture("tones-mono-22k.m4b");
    audiobook::MemorySource source{bytes};
    audiobook::Mp4Audio audio;
    TEST_ASSERT_TRUE(audio.open(source).has_value());
    const Decoded decoded = decodeFrom(audio, source, 0, audio.sampleCount());
    TEST_ASSERT_EQUAL(22050, decoded.sampleRate);
    TEST_ASSERT_EQUAL(1, decoded.channels);
    TEST_ASSERT_GREATER_OR_EQUAL(3 * 22050, decoded.pcm.size());
    // Sample the middle of each one-second chapter, past the encoder's start-up delay.
    for (const auto [second, frequency]: {std::pair{0, 440}, {1, 880}, {2, 1320}}) {
        const int measured = toneFrequency(decoded, static_cast<size_t>(second * 22050 + 6000), 8000);
        TEST_ASSERT_INT_WITHIN(frequency * 3 / 100, frequency, measured);
    }
}

void test_seeking_to_a_chapter_decodes_its_tone() {
    const auto bytes = readFixture("tones-mono-22k-faststart.m4b");
    audiobook::MemorySource source{bytes};
    audiobook::Mp4Audio audio;
    TEST_ASSERT_TRUE(audio.open(source).has_value());
    const uint64_t start = audio.chapters()[2].startMs;
    const uint32_t first = audio.sampleAtMs(start + 200);
    TEST_ASSERT_LESS_OR_EQUAL(start + 200, audio.msAtSample(first));
    TEST_ASSERT_GREATER_OR_EQUAL(start + 200 - 50, audio.msAtSample(first));
    const Decoded decoded = decodeFrom(audio, source, first, 12);
    TEST_ASSERT_INT_WITHIN(40, 1320, toneFrequency(decoded, 4096, 6000));
}

void test_decodes_stereo() {
    const auto bytes = readFixture("tones-stereo-44k.m4b");
    audiobook::MemorySource source{bytes};
    audiobook::Mp4Audio audio;
    TEST_ASSERT_TRUE(audio.open(source).has_value());
    TEST_ASSERT_EQUAL(2, audio.format().channels);
    TEST_ASSERT_EQUAL(44100, audio.format().sampleRate);
    const Decoded decoded = decodeFrom(audio, source, 0, 40);
    TEST_ASSERT_EQUAL(2, decoded.channels);
    TEST_ASSERT_EQUAL(44100, decoded.sampleRate);
    TEST_ASSERT_INT_WITHIN(13, 440, toneFrequency(decoded, 8000, 20000));
}

void test_rejects_damaged_and_protected_files() {
    const auto bytes = readFixture("tones-mono-22k.m4b");
    {
        // The movie header is at the end; a partial copy has none.
        const std::vector<uint8_t> truncated(bytes.begin(), bytes.begin() + static_cast<long>(bytes.size() / 2));
        audiobook::MemorySource source{truncated};
        audiobook::Mp4Audio audio;
        TEST_ASSERT_FALSE(audio.open(source).has_value());
    }
    {
        std::vector<uint8_t> garbage(4096, 0xA5);
        audiobook::MemorySource source{garbage};
        audiobook::Mp4Audio audio;
        TEST_ASSERT_FALSE(audio.open(source).has_value());
    }
    {
        auto protectedCopy = bytes;
        renameBox(protectedCopy, "mp4a", "enca");
        audiobook::MemorySource source{protectedCopy};
        audiobook::Mp4Audio audio;
        const auto opened = audio.open(source);
        TEST_ASSERT_FALSE(opened.has_value());
        TEST_ASSERT_EQUAL_STRING("this audiobook is copy-protected", opened.error().c_str());
    }
    {
        // A sample-size table claiming far more entries than the file holds must be refused up front.
        auto inflated = bytes;
        for (size_t index = 0; index + 16 <= inflated.size(); ++index) {
            if (std::memcmp(inflated.data() + index, "stsz", 4) == 0) {
                const uint8_t huge[4]{0x7F, 0xFF, 0xFF, 0xFF};
                std::memcpy(inflated.data() + index + 12, huge, 4);
                break;
            }
        }
        audiobook::MemorySource source{inflated};
        audiobook::Mp4Audio audio;
        TEST_ASSERT_FALSE(audio.open(source).has_value());
    }
}

void test_long_tables_cross_windows_and_chunk_runs() {
    for (const bool largeOffsets: {false, true}) {
        const SyntheticMovie movie = syntheticMovie(largeOffsets);
        audiobook::MemorySource source{movie.bytes};
        audiobook::Mp4Audio audio;
        const auto opened = audio.open(source);
        TEST_ASSERT_TRUE_MESSAGE(opened.has_value(), opened ? "" : opened.error().c_str());
        TEST_ASSERT_EQUAL(movie.samples.size(), audio.sampleCount());
        TEST_ASSERT_EQUAL(22050, audio.format().sampleRate);
        for (const auto& expected: movie.samples) {
            const auto actual = audio.next();
            TEST_ASSERT_TRUE(actual.has_value());
            TEST_ASSERT_EQUAL(expected.index, actual->index);
            TEST_ASSERT_EQUAL(expected.offset, actual->offset);
            TEST_ASSERT_EQUAL(expected.size, actual->size);
            TEST_ASSERT_EQUAL(static_cast<uint8_t>(expected.index), movie.bytes[actual->offset]);
        }
        TEST_ASSERT_FALSE(audio.next().has_value());
        // Seeks land on run boundaries, window boundaries and the last sample.
        for (const uint32_t index: {0U, 6U, 7U, 127U, 128U, 272U, 273U, 274U, 422U, 423U, 700U, 1475U}) {
            TEST_ASSERT_TRUE(audio.seek(index));
            const auto actual = audio.next();
            TEST_ASSERT_TRUE(actual.has_value());
            TEST_ASSERT_EQUAL(movie.samples[index].offset, actual->offset);
            if (index + 1 < movie.samples.size())
                TEST_ASSERT_EQUAL(movie.samples[index + 1].offset, audio.next()->offset);
        }
        TEST_ASSERT_FALSE(audio.seek(static_cast<uint32_t>(movie.samples.size())));
        // Every time maps to the sample whose span contains it.
        for (const uint64_t ms: {0ULL, 1ULL, 32507ULL, 32508ULL, 40000ULL, 68000ULL}) {
            const uint32_t index = audio.sampleAtMs(ms);
            TEST_ASSERT_LESS_OR_EQUAL(ms * 22050, static_cast<uint64_t>(index) * 1024 * 1000);
            TEST_ASSERT_GREATER_THAN(ms * 22050, static_cast<uint64_t>(index + 1) * 1024 * 1000);
        }
        TEST_ASSERT_UINT64_WITHIN(1, 700ULL * 1024 * 1000 / 22050, audio.msAtSample(700));
        TEST_ASSERT_UINT64_WITHIN(1, movie.samples.size() * 1024ULL * 1000 / 22050, audio.durationMs());
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_reads_format_metadata_and_chapters);
    RUN_TEST(test_both_chapter_formats_are_read_on_their_own);
    RUN_TEST(test_header_position_does_not_change_samples);
    RUN_TEST(test_decodes_each_chapter_tone);
    RUN_TEST(test_seeking_to_a_chapter_decodes_its_tone);
    RUN_TEST(test_decodes_stereo);
    RUN_TEST(test_rejects_damaged_and_protected_files);
    RUN_TEST(test_long_tables_cross_windows_and_chunk_runs);
    return UNITY_END();
}
