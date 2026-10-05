#include <unity.h>

#include <vector>

#include "audio/AudioFiles.h"

void setUp() {}
void tearDown() {}

void test_wav_header_round_trips_and_survives_a_cut_off_recording() {
    const auto header = audio::wavHeader(16000, 1, 32000);
    std::vector<uint8_t> file(header.begin(), header.end());
    file.resize(file.size() + 32000);
    auto info = audio::parseWavHeader(file, file.size());
    TEST_ASSERT_TRUE(info.has_value());
    TEST_ASSERT_EQUAL_UINT32(16000, info->sampleRate);
    TEST_ASSERT_EQUAL_UINT16(1, info->channels);
    TEST_ASSERT_EQUAL_UINT32(44, info->dataOffset);
    TEST_ASSERT_EQUAL_UINT32(32000, info->dataBytes);
    TEST_ASSERT_EQUAL_UINT32(1000, audio::wavDurationMs(*info));

    // A memo whose header was never patched still plays everything that reached the card.
    const auto unpatched = audio::wavHeader(16000, 1, 0);
    std::copy(unpatched.begin(), unpatched.end(), file.begin());
    file.push_back(0x7F); // an odd trailing byte is not a whole sample
    info = audio::parseWavHeader(file, file.size());
    TEST_ASSERT_TRUE(info.has_value());
    TEST_ASSERT_EQUAL_UINT32(32000, info->dataBytes);
}

void test_wav_parser_skips_extra_chunks_and_rejects_other_formats() {
    const auto header = audio::wavHeader(44100, 2, 8);
    std::vector<uint8_t> file(header.begin(), header.begin() + 36);
    const uint8_t list[] = {'L', 'I', 'S', 'T', 3, 0, 0, 0, 'a', 'b', 'c', 0};
    file.insert(file.end(), std::begin(list), std::end(list));
    file.insert(file.end(), header.begin() + 36, header.end());
    file.resize(file.size() + 8);
    const auto info = audio::parseWavHeader(file, file.size());
    TEST_ASSERT_TRUE(info.has_value());
    TEST_ASSERT_EQUAL_UINT16(2, info->channels);
    TEST_ASSERT_EQUAL_UINT32(56, info->dataOffset);

    std::vector<uint8_t> floatWav(header.begin(), header.end());
    floatWav[20] = 3;
    TEST_ASSERT_FALSE(audio::parseWavHeader(floatWav, floatWav.size()).has_value());
    const std::vector<uint8_t> notWav{'O', 'g', 'g', 'S', 0, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_FALSE(audio::parseWavHeader(notWav, notWav.size()).has_value());
}

void test_memo_names_number_and_ignore_other_files() {
    TEST_ASSERT_EQUAL_STRING("memo-0007.wav", audio::memoFileName(7).c_str());
    TEST_ASSERT_EQUAL_UINT32(7, *audio::memoNumber("/voice/memo-0007.wav"));
    TEST_ASSERT_EQUAL_UINT32(12345, *audio::memoNumber("memo-12345.wav"));
    TEST_ASSERT_FALSE(audio::memoNumber("memo-.wav").has_value());
    TEST_ASSERT_FALSE(audio::memoNumber("memo-0000.wav").has_value());
    TEST_ASSERT_FALSE(audio::memoNumber("memo-12a.wav").has_value());
    TEST_ASSERT_FALSE(audio::memoNumber("song.wav").has_value());
    TEST_ASSERT_FALSE(audio::memoNumber("memo-0003.mp3").has_value());
}

void test_audiobook_files_and_names() {
    TEST_ASSERT_TRUE(audio::isAudiobookFile("/audiobooks/Dune.m4b"));
    TEST_ASSERT_TRUE(audio::isAudiobookFile("/audiobooks/Author/Book One.M4A"));
    TEST_ASSERT_TRUE(audio::isAudiobookFile("talk.mp4"));
    TEST_ASSERT_FALSE(audio::isAudiobookFile("/audiobooks/._Dune.m4b"));
    TEST_ASSERT_FALSE(audio::isAudiobookFile("/audiobooks/cover.jpg"));
    TEST_ASSERT_FALSE(audio::isAudiobookFile("/audiobooks/notes.mp3"));
    TEST_ASSERT_EQUAL_STRING("The Hobbit", audio::displayName("/audiobooks/Tolkien/The_Hobbit.m4b").c_str());
    TEST_ASSERT_EQUAL_STRING(".hidden", audio::displayName(".hidden").c_str());
}

void test_saved_positions_parse_clamp_and_format() {
    auto saved = audio::parsePosition("61000 120000\n");
    TEST_ASSERT_TRUE(saved.has_value());
    TEST_ASSERT_EQUAL_UINT64(61000, saved->positionMs);
    TEST_ASSERT_EQUAL_UINT64(120000, saved->durationMs);
    TEST_ASSERT_EQUAL_UINT8(50, audio::percentListened(*saved));
    TEST_ASSERT_EQUAL_STRING("61000 120000\n", audio::formatPosition(*saved).c_str());

    saved = audio::parsePosition("500000 120000");
    TEST_ASSERT_EQUAL_UINT64(120000, saved->positionMs);
    saved = audio::parsePosition("4200");
    TEST_ASSERT_EQUAL_UINT64(4200, saved->positionMs);
    TEST_ASSERT_EQUAL_UINT8(0, audio::percentListened(*saved));
    TEST_ASSERT_FALSE(audio::parsePosition("garbage").has_value());
    TEST_ASSERT_EQUAL_STRING("/audiobooks/.positions/Dune.m4b.pos",
                             audio::positionPathFor("/audiobooks/Herbert/Dune.m4b").c_str());
}

void test_clock_text() {
    TEST_ASSERT_EQUAL_STRING("0:00", audio::formatClock(999).c_str());
    TEST_ASSERT_EQUAL_STRING("0:42", audio::formatClock(42'000).c_str());
    TEST_ASSERT_EQUAL_STRING("12:05", audio::formatClock(725'000).c_str());
    TEST_ASSERT_EQUAL_STRING("1:02:03", audio::formatClock(3'723'000).c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_wav_header_round_trips_and_survives_a_cut_off_recording);
    RUN_TEST(test_wav_parser_skips_extra_chunks_and_rejects_other_formats);
    RUN_TEST(test_memo_names_number_and_ignore_other_files);
    RUN_TEST(test_audiobook_files_and_names);
    RUN_TEST(test_saved_positions_parse_clamp_and_format);
    RUN_TEST(test_clock_text);
    return UNITY_END();
}
