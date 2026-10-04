#include "ui/screens/ReaderLayout.h"
#include "fonts/RFont4Format.h"

#include <algorithm>

namespace screens::readerLayout {
    namespace {
        constexpr int16_t kInfoHeight = 36;
        constexpr int16_t kBarBottomMargin = 4;
        constexpr int16_t kEdge = 8;
        constexpr int16_t kGap = 8;
        constexpr int16_t kSingleRowMinimumWidth = 560;

        // Denser square panels need taller targets for the same physical size as the 172 px LCD.
        int16_t barHeight(int16_t height) {
            // Even heights keep AMOLED two-row paint alignment from widening the bar into the word area.
            return std::clamp<int16_t>(static_cast<int16_t>((height / 6) & ~1), 46, 76);
        }

        int16_t barRows(int16_t width) {
            return width >= kSingleRowMinimumWidth ? 1 : 2;
        }

        int16_t barTop(int16_t width, int16_t height) {
            const int16_t rows = barRows(width);
            return static_cast<int16_t>(height - kBarBottomMargin - barHeight(height) * rows - kGap * (rows - 1));
        }
    } // namespace

    size_t pageStrikeIndex() {
        return RFont4::kCompactStrikeIndex;
    }
    ui::Rect readingArea(int16_t width, int16_t height, bool verticalPage) {
        if (!verticalPage) {
            // Stop above the paused control bar so word repaints never overwrite it.
            return {0, kInfoHeight, width,
                    static_cast<int16_t>(std::max<int>(0, barTop(width, height) - 2 - kInfoHeight))};
        }
        const int16_t left = portraitTopStrip(height).h;
        const int16_t right = portraitBottomStrip(height, width).h;
        return {left, 36, static_cast<int16_t>(std::max<int>(0, width - left - right)),
                static_cast<int16_t>(std::max<int>(0, height - 72))};
    }

    Controls controls(int16_t width, int16_t height, bool leftHanded) {
        Controls result;
        const auto chrome = horizontalChrome(width, height, leftHanded);
        result.info = {0, 0, width, kInfoHeight};
        result.battery = chrome.battery;
        const int16_t progressWidth = std::min<int16_t>(108, static_cast<int16_t>(width / 5));
        result.progress = {static_cast<int16_t>(result.battery.x - progressWidth - kGap), 0, progressWidth, 30};
        result.chapter = {12, 0, static_cast<int16_t>(std::max<int>(0, result.progress.x - kGap - 12)), 30};

        const int16_t rowHeight = barHeight(height);
        const int16_t scale100 = static_cast<int16_t>(rowHeight * 100 / 46);
        const auto scaled = [scale100](int16_t base) {
            return static_cast<int16_t>(base * scale100 / 100);
        };
        const int16_t inner = static_cast<int16_t>(width - kEdge * 2);
        const bool singleRow = barRows(width) == 1;
        int16_t x = kEdge;
        int16_t y = barTop(width, height);
        const auto place = [&](int16_t w) {
            const ui::Rect rect{x, y, w, rowHeight};
            x = static_cast<int16_t>(x + w + kGap);
            return rect;
        };
        // Laid out for the right hand, then mirrored so the primary action stays under the reading thumb.
        ui::Rect menu, rewind, slower, faster, play;
        if (singleRow) {
            const int16_t menuWidth = std::min(scaled(112), static_cast<int16_t>(inner * 18 / 100));
            const int16_t playWidth = std::min(scaled(140), static_cast<int16_t>(inner * 22 / 100));
            const int16_t stepWidth = std::min(scaled(64), static_cast<int16_t>(inner * 11 / 100));
            const int16_t speedWidth = std::min(scaled(108), static_cast<int16_t>(inner * 17 / 100));
            menu = place(menuWidth);
            rewind = place(stepWidth);
            const int16_t speedGroup = static_cast<int16_t>(stepWidth * 2 + speedWidth + kGap * 2);
            const int16_t playX = static_cast<int16_t>(width - kEdge - playWidth);
            x = static_cast<int16_t>(std::max<int>(x, x + (playX - kGap - x - speedGroup) / 2));
            slower = place(stepWidth);
            result.speed = place(speedWidth);
            faster = place(stepWidth);
            play = {playX, y, playWidth, rowHeight};
        } else {
            // Speed sits above navigation on narrow panels so every target keeps a usable width.
            const int16_t stepWidth = std::max<int16_t>(rowHeight, static_cast<int16_t>(inner / 4));
            const int16_t speedWidth = static_cast<int16_t>(inner - stepWidth * 2 - kGap * 2);
            slower = place(stepWidth);
            result.speed = place(speedWidth);
            faster = place(stepWidth);
            x = kEdge;
            y = static_cast<int16_t>(y + rowHeight + kGap);
            const int16_t sideWidth = static_cast<int16_t>((inner - kGap * 2) * 30 / 100);
            menu = place(sideWidth);
            rewind = place(static_cast<int16_t>(inner - sideWidth * 2 - kGap * 2));
            play = place(sideWidth);
        }
        result.buttons = {{{Control::Menu, menu},
                           {Control::Rewind, rewind},
                           {Control::Slower, slower},
                           {Control::Faster, faster},
                           {Control::Play, play}}};
        if (leftHanded) {
            const auto mirror = [width](ui::Rect& rect) {
                rect.x = static_cast<int16_t>(width - rect.x - rect.w);
            };
            for (auto& button: result.buttons)
                mirror(button.rect);
            mirror(result.speed);
        }
        return result;
    }

    ui::Rect portraitTopStrip(int16_t width) {
        return {0, 0, width, 58};
    }

    ui::Rect portraitBatteryRect() {
        return {6, 4, 92, 30};
    }

    ui::Rect portraitFooterRect(int16_t width) {
        return {static_cast<int16_t>(width - 72), 4, 66, 30};
    }

    ui::Rect portraitChapterRect(int16_t width, int16_t height) {
        return {static_cast<int16_t>(width - 36), 58, 30, static_cast<int16_t>(height - 106)};
    }

    ui::Rect portraitFeedbackRect() {
        return {6, 36, 118, 20};
    }

    ui::Rect portraitBottomStrip(int16_t width, int16_t height) {
        return {0, static_cast<int16_t>(height - 48), width, 48};
    }

    ui::Rect portraitPreviousRect(int16_t width, int16_t height, bool leftHanded) {
        return {static_cast<int16_t>(leftHanded ? 8 : width - 48), static_cast<int16_t>(height - 40), 40, 30};
    }

    ui::Rect previousSentenceRect(int16_t width, int16_t height, bool leftHanded, bool) {
        return {static_cast<int16_t>(leftHanded ? 0 : std::max<int>(0, width - 112)), 0,
                std::min<int16_t>(width, leftHanded ? 113 : 112), height};
    }
    ui::Rect arrowArea(int16_t width, int16_t height, bool leftHanded, int16_t wordHeight) {
        auto rect = horizontalChrome(width, height, leftHanded).arrows;
        rect.h = std::max(rect.h, wordHeight);
        rect.y = (height - rect.h) / 2;
        return rect;
    }
    HorizontalChrome horizontalChrome(int16_t width, int16_t height, bool leftHanded, int16_t footerWidth) {
        const int16_t y = height - 26;
        return {
            .chapter = {static_cast<int16_t>(leftHanded && footerWidth ? footerWidth + 42 : 18), y,
                        static_cast<int16_t>(footerWidth ? width - footerWidth - 60 : width - 36), 26},
            .progress = {static_cast<int16_t>(leftHanded ? 18 : width - footerWidth - 18), y, footerWidth, 26},
            .battery = {static_cast<int16_t>(width - 126), 0, 116, 36},
            .arrows = {static_cast<int16_t>(leftHanded ? 8 : width - 52), static_cast<int16_t>(height / 2 - 22), 44,
                       44},
            .textSize = 2,
        };
    }

    void chrome(ui::Context& ui, const Chrome& view, const settings::ReadingSettings& settings,
                const Board::Power::BatteryState& battery) {
        const bool vertical = view.vertical;
        const bool showChapter = settings::visible(settings.chapterVisibility, view.reading);
        const bool showProgress = settings::visible(settings.progressVisibility, view.reading);
        const bool showArrows = settings::visible(settings.arrowsVisibility, view.reading);
        const bool showBatteryLabel = settings::visible(settings.batteryLabelVisibility, view.reading);
        const bool showBatteryIcon = settings::visible(settings.batteryIconVisibility, view.reading);
        const auto chapterLabel = view.chapter, footer = view.footer, batteryLabel = view.batteryLabel;
        if (vertical) {
            const int16_t portraitWidth = ui.height();
            const int16_t portraitHeight = ui.width();
            const auto overlay = view.overlay;
            uint32_t topState = ui::Context::signature(footer, view.topState);
            topState = ui::Context::signature(batteryLabel, topState);
            topState = ui::Context::combine(topState, battery.status.percent);
            topState = ui::Context::combine(topState, battery.charging);
            topState = ui::Context::combine(topState, showBatteryIcon);
            topState = ui::Context::combine(topState, showBatteryLabel);
            topState = ui::Context::combine(topState, showProgress);
            if (ui.redraw(ui::rotateClockwise(portraitTopStrip(portraitWidth), portraitWidth), topState)) {
                if (showBatteryLabel || showBatteryIcon)
                    ui.portraitBattery(portraitBatteryRect(), battery.status.percent, battery.charging,
                                       showBatteryLabel ? batteryLabel : std::string_view{}, showBatteryIcon);
                if (showProgress)
                    ui.portraitText(portraitFooterRect(portraitWidth), footer, 2,
                                    ui.color(ui::themes::ColorRole::Muted), ui::TextAlign::Right);
                if (!overlay.empty())
                    ui.portraitText(portraitFeedbackRect(), overlay, 1, ui.color(ui::themes::ColorRole::Accent),
                                    ui::TextAlign::Center);
            }

            const std::string_view visibleChapter = showChapter ? chapterLabel : std::string_view{};
            uint32_t chapterState = ui::Context::signature(visibleChapter);
            chapterState = ui::Context::combine(chapterState, showChapter);
            const ui::Rect chapterArea = portraitChapterRect(portraitWidth, portraitHeight);
            if (ui.redraw(ui::rotateClockwise(chapterArea, portraitWidth), chapterState))
                ui.portraitVerticalText(chapterArea, visibleChapter, 1, ui.color(ui::themes::ColorRole::Muted),
                                        view.locale);

            const uint32_t bottomState = ui::Context::combine(view.bottomState, showArrows);
            if (ui.redraw(ui::rotateClockwise(portraitBottomStrip(portraitWidth, portraitHeight), portraitWidth),
                          bottomState)) {
                if (showArrows)
                    ui.portraitText(portraitPreviousRect(portraitWidth, portraitHeight, settings.leftHanded), "<<", 2,
                                    ui.color(ui::themes::ColorRole::Muted), ui::TextAlign::Center);
            }
        } else if (!view.reading && !view.ghostHidden) {
            pausedControls(ui, view, settings, battery);
        } else {
            horizontalChrome(ui, view, settings, battery);
        }
    }

    void pausedControls(ui::Context& ui, const Chrome& view, const settings::ReadingSettings& settings,
                        const Board::Power::BatteryState& battery) {
        const Controls layout = controls(ui.width(), ui.height(), settings.leftHanded);
        const auto shown = [&](settings::Visibility visibility) {
            return settings::visible(visibility, false);
        };
        ui.label(layout.chapter, shown(settings.chapterVisibility) ? view.chapter : std::string_view{}, 2,
                 ui::themes::Muted, ui::TextAlign::Left, 1, view.locale);
        ui.label(layout.progress, shown(settings.progressVisibility) ? view.footer : std::string_view{}, 2,
                 ui::themes::Muted, ui::TextAlign::Right);
        ui.battery(layout.battery, battery.status.percent, battery.charging,
                   shown(settings.batteryLabelVisibility) ? view.batteryLabel : std::string_view{},
                   shown(settings.batteryIconVisibility));
        ui.progress({12, 30, static_cast<int16_t>(ui.width() - 24), 4}, view.percent);
        for (const ControlButton& button: layout.buttons) {
            // The reader owns these touches, so it reports which button is under the finger.
            const bool held = view.pressed == button.control;
            switch (button.control) {
            case Control::Menu:
                ui.controlButton(button.rect, ui::Icon::Menu, ui.text(UiText::Menu), false, held);
                break;
            case Control::Rewind:
                ui.controlButton(button.rect, ui::Icon::Rewind, {}, false, held);
                break;
            case Control::Slower:
                ui.controlButton(button.rect, ui::Icon::Minus, {}, false, held);
                break;
            case Control::Faster:
                ui.controlButton(button.rect, ui::Icon::Plus, {}, false, held);
                break;
            case Control::Play:
                ui.controlButton(button.rect, ui::Icon::Play, ui.text(UiText::Resume), true, held);
                break;
            case Control::None:
                break;
            }
        }
        ui.label(layout.speed, view.speed, 2, ui::themes::Accent, ui::TextAlign::Center);
    }
} // namespace screens::readerLayout
