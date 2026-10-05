#include "ui/screens/AudioScreens.h"

#include <algorithm>
#include <cstdio>

#include "audio/AudioFiles.h"
#include "ui/screens/ScreenCommon.h"

namespace screens {
    namespace {

        constexpr uint32_t kDeleteConfirmMs = 4'000;
        constexpr int16_t kGap = 6;

        AudioRequest request(AudioRequest::Type type, int32_t value = 0, size_t index = SIZE_MAX) {
            return {.type = type, .value = value, .index = index};
        }

    } // namespace

    AudioRequest AudiobooksScreen::drawList(ui::Context& ui, std::span<const AudiobookEntry> books,
                                            const NowPlaying& playing, Screen& screen) {
        AudioRequest result;
        result.action = detail::navigation(ui, Screen::Audiobooks, screen);
        if (result.action != Action::None)
            return result;
        const ui::Rect content = detail::tabContent(ui);
        if (books.empty()) {
            ui.label(content, ui.text(UiText::NoAudiobooks), 2, ui::themes::ColorRole::Muted, ui::TextAlign::Center,
                     3);
            return result;
        }
        const bool active = playing.status.content == audio::Content::Audiobook && !playing.path.empty();
        const auto current = std::ranges::find(books, playing.path, &AudiobookEntry::path);
        if (revealCurrent_ && current != books.end())
            list_.reveal(static_cast<size_t>(current - books.begin()));
        revealCurrent_ = false;

        std::string details[8];
        std::string trailing[8];
        size_t slot = 0;
        const size_t tapped = list_.draw(ui, content, books.size(), [&](size_t index) {
            const AudiobookEntry& book = books[index];
            const bool isCurrent = active && book.path == playing.path;
            std::string& detail = details[slot % 8];
            std::string& percent = trailing[slot % 8];
            ++slot;
            if (isCurrent && playing.status.mode == audio::Mode::Playing)
                detail = audio::formatClock(playing.status.positionMs) + " / "
                       + audio::formatClock(playing.status.durationMs);
            else
                detail.clear();
            const uint8_t shown = isCurrent && playing.status.durationMs > 0
                                    ? audio::percentListened({playing.status.positionMs, playing.status.durationMs})
                                    : book.percent;
            percent = book.started || isCurrent ? std::to_string(shown) + "%" : std::string{};
            return ListRow{.title = book.name,
                           .detail = detail,
                           .trailing = percent,
                           .progress = static_cast<int8_t>(book.started || isCurrent ? shown : -1),
                           .marked = isCurrent};
        });
        if (tapped != ListView::kNone) {
            result = request(AudioRequest::Type::Play, 0, tapped);
            screen = Screen::AudiobookPlayer;
        }
        return result;
    }

    AudioRequest AudiobooksScreen::drawPlayer(ui::Context& ui, const NowPlaying& playing, Screen& screen) {
        AudioRequest result;
        result.action = detail::navigation(ui, Screen::AudiobookPlayer, screen);
        if (result.action != Action::None)
            return result;
        const ui::Rect content = detail::tabContent(ui);
        constexpr int16_t kTopHeight = 34;
        constexpr int16_t kVolumeWidth = 40;
        const int16_t backWidth = 64;
        if (detail::back(ui, {content.x, content.y, backWidth, kTopHeight})) {
            screen = Screen::Audiobooks;
            revealCurrent_ = true;
            return result;
        }
        const int16_t volumeX = static_cast<int16_t>(content.x + content.w - kVolumeWidth * 2 - kGap);
        if (ui.controlButton({volumeX, content.y, kVolumeWidth, kTopHeight}, ui::Icon::Minus))
            result = request(AudioRequest::Type::Volume, -10);
        if (ui.controlButton({static_cast<int16_t>(volumeX + kVolumeWidth + kGap), content.y, kVolumeWidth, kTopHeight},
                             ui::Icon::Plus))
            result = request(AudioRequest::Type::Volume, 10);
        const int16_t titleX = static_cast<int16_t>(content.x + backWidth + kGap);
        const ui::Rect titleRect{titleX, content.y, static_cast<int16_t>(volumeX - kGap - titleX), kTopHeight};
        ui.label(titleRect, playing.title, 2, ui::themes::ColorRole::Foreground, ui::TextAlign::Left);

        const audio::Status& status = playing.status;
        const int16_t chapterY = static_cast<int16_t>(content.y + kTopHeight + 4);
        char volume[8];
        std::snprintf(volume, sizeof(volume), "%u%%", static_cast<unsigned>(status.volume));
        const int16_t volumeLabelWidth = 48;
        ui.label({content.x, chapterY, static_cast<int16_t>(content.w - volumeLabelWidth), 20},
                 status.failed ? ui.text(UiText::Unavailable) : playing.chapter, 2,
                 status.failed ? ui::themes::ColorRole::Accent : ui::themes::ColorRole::Muted, ui::TextAlign::Left);
        ui.label({static_cast<int16_t>(content.x + content.w - volumeLabelWidth), chapterY, volumeLabelWidth, 20},
                 volume, 1, ui::themes::ColorRole::Muted, ui::TextAlign::Right);

        const int16_t progressY = static_cast<int16_t>(chapterY + 24);
        const uint8_t percent = audio::percentListened({status.positionMs, status.durationMs});
        ui.progress({content.x, progressY, content.w, 4}, percent);
        const std::string elapsed = audio::formatClock(status.positionMs);
        const std::string remaining =
            "-" + audio::formatClock(status.durationMs > status.positionMs ? status.durationMs - status.positionMs : 0);
        const int16_t timesY = static_cast<int16_t>(progressY + 6);
        const int16_t half = static_cast<int16_t>(content.w / 2);
        ui.label({content.x, timesY, half, 12}, elapsed, 1, ui::themes::ColorRole::Muted, ui::TextAlign::Left);
        ui.label({static_cast<int16_t>(content.x + half), timesY, half, 12}, remaining, 1,
                 ui::themes::ColorRole::Muted, ui::TextAlign::Right);

        // Transport: chapter back, 30 s back, play/pause, 30 s on, chapter on.
        const int16_t controlsY = static_cast<int16_t>(timesY + 16);
        const int16_t controlsHeight = static_cast<int16_t>(content.y + content.h - controlsY);
        if (controlsHeight < 32)
            return result;
        const int16_t playWidth = static_cast<int16_t>(content.w * 28 / 100);
        const int16_t sideWidth = static_cast<int16_t>((content.w - playWidth - kGap * 4) / 4);
        int16_t x = content.x;
        const auto next = [&](int16_t width) {
            const ui::Rect rect{x, controlsY, width, controlsHeight};
            x = static_cast<int16_t>(x + width + kGap);
            return rect;
        };
        if (ui.controlButton(next(sideWidth), ui::Icon::Previous))
            result = request(AudioRequest::Type::Chapter, -1);
        if (ui.controlButton(next(sideWidth), ui::Icon::None, "-30"))
            result = request(AudioRequest::Type::Skip, -30);
        const bool playingNow = status.mode == audio::Mode::Playing;
        if (ui.controlButton(next(playWidth), playingNow ? ui::Icon::Pause : ui::Icon::Play, {}, true))
            result = request(AudioRequest::Type::TogglePause);
        if (ui.controlButton(next(sideWidth), ui::Icon::None, "+30"))
            result = request(AudioRequest::Type::Skip, 30);
        if (ui.controlButton(next(static_cast<int16_t>(content.x + content.w - x)), ui::Icon::Next))
            result = request(AudioRequest::Type::Chapter, 1);
        return result;
    }

    void VoiceScreen::reset() {
        deleteArmed_ = false;
        list_.reset();
    }

    AudioRequest VoiceScreen::draw(ui::Context& ui, std::span<const MemoEntry> memos, const NowPlaying& playing,
                                   bool microphone, uint32_t nowMs, Screen& screen) {
        AudioRequest result;
        result.action = detail::navigation(ui, Screen::Voice, screen);
        if (result.action != Action::None)
            return result;
        const ui::Rect content = detail::tabContent(ui);
        const audio::Status& status = playing.status;
        const bool recording = status.mode == audio::Mode::Recording;
        if (deleteArmed_ && nowMs - deleteArmedAtMs_ > kDeleteConfirmMs)
            deleteArmed_ = false;
        if (selected_ != SIZE_MAX && selected_ >= memos.size())
            selected_ = SIZE_MAX;

        // Left column: record or stop, then what is happening or delete.
        const int16_t columnWidth = std::min<int16_t>(160, static_cast<int16_t>(content.w * 34 / 100));
        const int16_t recordHeight = static_cast<int16_t>(content.h * 58 / 100);
        const ui::Rect recordRect{content.x, content.y, columnWidth, recordHeight};
        if (ui.controlButton(recordRect, recording ? ui::Icon::Stop : ui::Icon::Record,
                             ui.text(recording ? UiText::Stop : UiText::Record), true)
            && (recording || microphone))
            result = {.type = AudioRequest::Type::Record};
        const ui::Rect lower{content.x, static_cast<int16_t>(content.y + recordHeight + kGap), columnWidth,
                             static_cast<int16_t>(content.h - recordHeight - kGap)};
        if (recording) {
            const std::string elapsed = audio::formatClock(status.positionMs);
            ui.label({lower.x, lower.y, lower.w, static_cast<int16_t>(lower.h - 10)}, elapsed, 2,
                     ui::themes::ColorRole::Accent, ui::TextAlign::Center);
            ui.progress({lower.x, static_cast<int16_t>(lower.y + lower.h - 6), lower.w, 6}, status.level / 10);
        } else if (!microphone) {
            ui.label(lower, ui.text(UiText::Unavailable), 2, ui::themes::ColorRole::Muted, ui::TextAlign::Center);
        } else if (ui.button(lower, ui.text(deleteArmed_ ? UiText::AreYouSure : UiText::Delete),
                             selected_ != SIZE_MAX)) {
            if (deleteArmed_) {
                result = {.type = AudioRequest::Type::Delete, .index = selected_};
                deleteArmed_ = false;
                selected_ = SIZE_MAX;
            } else {
                deleteArmed_ = true;
                deleteArmedAtMs_ = nowMs;
            }
        }

        // Right: the memos, newest first.
        const ui::Rect listRect{static_cast<int16_t>(content.x + columnWidth + kGap * 2), content.y,
                                static_cast<int16_t>(content.w - columnWidth - kGap * 2), content.h};
        if (memos.empty()) {
            ui.label(listRect, ui.text(UiText::NoMemos), 2, ui::themes::ColorRole::Muted, ui::TextAlign::Center);
            return result;
        }
        std::string titles[8];
        std::string trailing[8];
        size_t slot = 0;
        const size_t tapped = list_.draw(ui, listRect, memos.size(), [&](size_t index) {
            const MemoEntry& memo = memos[index];
            const bool isCurrent = status.content == audio::Content::Memo && !recording && memo.path == playing.path
                                && status.mode != audio::Mode::Idle;
            std::string& title = titles[slot % 8];
            std::string& time = trailing[slot % 8];
            ++slot;
            title = std::string{ui.text(UiText::Memo)} + " " + std::to_string(memo.number);
            time = isCurrent ? audio::formatClock(status.positionMs) + "/" + audio::formatClock(memo.durationMs)
                             : audio::formatClock(memo.durationMs);
            return ListRow{.title = title,
                           .trailing = time,
                           .progress = static_cast<int8_t>(
                               isCurrent ? audio::percentListened({status.positionMs, memo.durationMs}) : -1),
                           .marked = index == selected_ || isCurrent};
        });
        if (tapped != ListView::kNone && !recording) {
            selected_ = tapped;
            deleteArmed_ = false;
            const bool current = status.content == audio::Content::Memo && memos[tapped].path == playing.path
                              && (status.mode == audio::Mode::Playing || status.mode == audio::Mode::Paused);
            result = current ? AudioRequest{.type = AudioRequest::Type::TogglePause}
                             : request(AudioRequest::Type::Play, 0, tapped);
        }
        return result;
    }

} // namespace screens
