#include "ui/screens/LibraryScreen.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "ui/screens/ScreenCommon.h"

namespace screens {
    namespace {

        constexpr int16_t kRowGap = 4;
        // About 6.5 mm on the 3.49 in panel: the smallest row a thumb hits reliably.
        constexpr int16_t kMinimumRowHeight = 48;
        constexpr int16_t kScrollWidth = 52;
        constexpr int16_t kPositionHeight = 20;
        constexpr int32_t kDragThreshold = ui::TouchTiming{}.tapMoveTolerancePx;

        std::string_view title(const LibraryItem& item) {
            return item.book == nullptr ? std::string_view{} : BookLibrary::displayName(*item.book);
        }

        std::string_view author(ui::Context& ui, const LibraryItem& item) {
            return item.book == nullptr || item.book->author.empty() ? ui.text(UiText::Unknown)
                                                                     : std::string_view{item.book->author};
        }

        void drawRow(ui::Context& ui, ui::Rect rect, const LibraryItem& item, bool down) {
            Arduino_GFX& gfx = ui.gfx();
            const uint16_t accent = ui.color(ui::themes::ColorRole::Accent);
            gfx.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 5,
                              ui.color(down ? ui::themes::ColorRole::SurfaceActive : ui::themes::ColorRole::SurfaceMuted));
            gfx.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 5,
                              down || item.current ? accent : ui.color(ui::themes::ColorRole::Outline));
            // The open book carries an accent edge so it is easy to find again.
            if (item.current)
                gfx.fillRect(static_cast<int16_t>(rect.x + 4), static_cast<int16_t>(rect.y + 6), 3,
                             static_cast<int16_t>(rect.h - 12), accent);

            char percent[5];
            std::snprintf(percent, sizeof(percent), "%u%%", static_cast<unsigned>(std::min<uint8_t>(item.progress, 100)));
            constexpr int16_t kPercentWidth = 56;
            const int16_t textX = static_cast<int16_t>(rect.x + 14);
            const int16_t textWidth = static_cast<int16_t>(std::max(0, rect.w - 14 - kPercentWidth - 12));
            ui.drawText({textX, static_cast<int16_t>(rect.y + 5), textWidth, 18}, title(item), 2,
                        ui.color(ui::themes::ColorRole::Foreground));
            ui.drawText({textX, static_cast<int16_t>(rect.y + 25), textWidth, 10}, author(ui, item), 1,
                        ui.color(ui::themes::ColorRole::Muted));
            ui.drawText({static_cast<int16_t>(rect.x + rect.w - kPercentWidth - 10), rect.y, kPercentWidth,
                         static_cast<int16_t>(rect.h - 6)},
                        percent, 2, item.progress > 0 ? accent : ui.color(ui::themes::ColorRole::Muted),
                        ui::TextAlign::Right);

            const int16_t barWidth = static_cast<int16_t>(std::max(0, rect.w - 28));
            const int16_t barY = static_cast<int16_t>(rect.y + rect.h - 6);
            gfx.fillRect(textX, barY, barWidth, 2, ui.color(ui::themes::ColorRole::ProgressTrack));
            const int16_t filled = static_cast<int16_t>(barWidth * std::min<uint8_t>(item.progress, 100) / 100);
            if (filled > 0)
                gfx.fillRect(textX, barY, filled, 2, accent);
        }

    } // namespace

    Action LibraryScreen::draw(ui::Context& ui, const std::vector<LibraryItem>& items, uint32_t nowMs, Screen& screen) {
        (void) nowMs;
        Action result = detail::navigation(ui, Screen::Library, screen);

        const ui::Rect content = detail::tabContent(ui);
        if (items.empty()) {
            ui.label(content, ui.text(UiText::NoLibraryItems), 2, ui::themes::ColorRole::Muted,
                     ui::TextAlign::Center);
            return result;
        }

        const size_t count = items.size();
        const int16_t rows = std::max<int16_t>(1, static_cast<int16_t>((content.h + kRowGap)
                                                                       / (kMinimumRowHeight + kRowGap)));
        const size_t visible = std::min<size_t>(count, static_cast<size_t>(rows));
        const bool scrolls = count > visible;
        const int16_t rowHeight = static_cast<int16_t>((content.h - kRowGap * (rows - 1)) / rows);
        const int16_t rowStep = static_cast<int16_t>(rowHeight + kRowGap);
        const ui::Rect list{content.x, content.y,
                            static_cast<int16_t>(scrolls ? content.w - kScrollWidth - kRowGap * 2 : content.w),
                            content.h};
        const size_t lastFirst = count - visible;
        selectedIndex_ = std::min(selectedIndex_, count - 1);

        if (scrollToCurrent_) {
            scrollToCurrent_ = false;
            const auto current = std::ranges::find_if(items, &LibraryItem::current);
            const size_t index = current == items.end() ? 0 : static_cast<size_t>(current - items.begin());
            first_ = index > (visible - 1) / 2 ? index - (visible - 1) / 2 : 0;
        }
        first_ = std::min(first_, lastFirst);

        // Paging runs before the rows draw so a press repaints the list in the same frame.
        if (scrolls) {
            const ui::Rect column{static_cast<int16_t>(list.x + list.w + kRowGap * 2), content.y, kScrollWidth,
                                  content.h};
            const int16_t buttonHeight = static_cast<int16_t>((column.h - kPositionHeight - 4) / 2);
            const ui::Rect up{column.x, column.y, column.w, buttonHeight};
            const ui::Rect position{column.x, static_cast<int16_t>(column.y + buttonHeight + 2), column.w,
                                    kPositionHeight};
            const ui::Rect down{column.x, static_cast<int16_t>(column.y + column.h - buttonHeight), column.w,
                                buttonHeight};
            if (ui.controlButton(up, ui::Icon::Up))
                first_ = first_ > visible ? first_ - visible : 0;
            if (ui.controlButton(down, ui::Icon::Down))
                first_ = std::min(first_ + visible, lastFirst);
            char label[16];
            std::snprintf(label, sizeof(label), "%u/%u", static_cast<unsigned>(first_ + visible),
                          static_cast<unsigned>(count));
            ui.label(position, label, 1, ui::themes::ColorRole::Muted, ui::TextAlign::Center);
        }

        const auto rowRect = [&](size_t row) {
            return ui::Rect{list.x, static_cast<int16_t>(list.y + rowStep * static_cast<int16_t>(row)), list.w,
                            rowHeight};
        };
        const ui::Touch* touch = ui.touch();
        if (touch != nullptr && ui::hasTouch(*touch, ui::TouchStart) && ui::contains(list, touch->x, touch->y)) {
            dragging_ = true;
            moved_ = false;
            startY_ = touch->y;
            dragStartFirst_ = first_;
            pressedRow_ = SIZE_MAX;
            for (size_t row = 0; row < visible; ++row)
                if (ui::contains(rowRect(row), touch->x, touch->y))
                    pressedRow_ = row;
        }
        if (dragging_ && touch != nullptr
            && (ui::hasTouch(*touch, ui::TouchMove) || ui::hasTouch(*touch, ui::TouchRelease))) {
            const int32_t dy = static_cast<int32_t>(touch->y) - startY_;
            moved_ = moved_ || std::abs(dy) > kDragThreshold;
            if (moved_) {
                // Whole rows follow the finger, so every row stays fully drawn and readable.
                const int32_t rowsMoved = (-dy + (dy < 0 ? rowStep / 2 : -rowStep / 2)) / rowStep;
                first_ = static_cast<size_t>(std::clamp<int32_t>(static_cast<int32_t>(dragStartFirst_) + rowsMoved, 0,
                                                                 static_cast<int32_t>(lastFirst)));
            }
        }
        if (dragging_ && touch != nullptr && ui::hasTouch(*touch, ui::TouchRelease)) {
            const size_t row = pressedRow_;
            dragging_ = false;
            pressedRow_ = SIZE_MAX;
            if (!moved_ && ui::hasTouch(*touch, ui::TouchPress) && row < visible
                && ui::contains(rowRect(row), touch->x, touch->y)) {
                selectedIndex_ = first_ + row;
                result = Action::OpenBook;
            }
        }

        for (size_t row = 0; row < visible; ++row) {
            const LibraryItem& item = items[first_ + row];
            const bool down = dragging_ && !moved_ && pressedRow_ == row;
            uint32_t state = ui::Context::signature(title(item));
            state = ui::Context::signature(author(ui, item), state);
            state = ui::Context::combine(state, item.progress);
            state = ui::Context::combine(state, item.current);
            state = ui::Context::combine(state, down);
            const ui::Rect rect = rowRect(row);
            if (ui.redraw(rect, state))
                drawRow(ui, rect, item, down);
        }
        return result;
    }

} // namespace screens
