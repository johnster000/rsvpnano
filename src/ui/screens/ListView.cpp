#include "ui/screens/ListView.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace screens {
    namespace {

        constexpr int16_t kRowGap = 4;
        constexpr int16_t kMinimumRowHeight = 48;
        constexpr int16_t kScrollWidth = 52;
        constexpr int16_t kPositionHeight = 20;
        constexpr int32_t kDragThreshold = ui::TouchTiming{}.tapMoveTolerancePx;

        void drawRow(ui::Context& ui, ui::Rect rect, const ListRow& row, bool down) {
            Arduino_GFX& gfx = ui.gfx();
            const uint16_t accent = ui.color(ui::themes::ColorRole::Accent);
            gfx.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 5,
                              ui.color(down ? ui::themes::ColorRole::SurfaceActive : ui::themes::ColorRole::SurfaceMuted));
            gfx.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 5,
                              down || row.marked ? accent : ui.color(ui::themes::ColorRole::Outline));
            if (row.marked)
                gfx.fillRect(static_cast<int16_t>(rect.x + 4), static_cast<int16_t>(rect.y + 6), 3,
                             static_cast<int16_t>(rect.h - 12), accent);

            const int16_t trailingWidth =
                row.trailing.empty() ? 0 : std::min<int16_t>(static_cast<int16_t>(rect.w / 3),
                                                             static_cast<int16_t>(ui.textWidth(row.trailing, 2) + 4));
            const int16_t textX = static_cast<int16_t>(rect.x + 14);
            const int16_t textWidth = static_cast<int16_t>(std::max(0, rect.w - 14 - trailingWidth - 16));
            const bool twoLines = !row.detail.empty();
            ui.drawText({textX, static_cast<int16_t>(rect.y + (twoLines ? 5 : (rect.h - 18) / 2)), textWidth, 18},
                        row.title, 2, ui.color(ui::themes::ColorRole::Foreground));
            if (twoLines)
                ui.drawText({textX, static_cast<int16_t>(rect.y + 25), textWidth, 10}, row.detail, 1,
                            ui.color(ui::themes::ColorRole::Muted));
            if (trailingWidth > 0)
                ui.drawText({static_cast<int16_t>(rect.x + rect.w - trailingWidth - 10), rect.y, trailingWidth,
                             static_cast<int16_t>(rect.h - (row.progress >= 0 ? 6 : 0))},
                            row.trailing, 2, row.marked ? accent : ui.color(ui::themes::ColorRole::Muted),
                            ui::TextAlign::Right);
            if (row.progress >= 0) {
                const int16_t barWidth = static_cast<int16_t>(std::max(0, rect.w - 28));
                const int16_t barY = static_cast<int16_t>(rect.y + rect.h - 6);
                gfx.fillRect(textX, barY, barWidth, 2, ui.color(ui::themes::ColorRole::ProgressTrack));
                const int16_t filled = static_cast<int16_t>(barWidth * std::min<int8_t>(row.progress, 100) / 100);
                if (filled > 0)
                    gfx.fillRect(textX, barY, filled, 2, accent);
            }
        }

    } // namespace

    void ListView::reveal(size_t index) {
        revealed_ = index;
    }

    void ListView::reset() {
        dragging_ = false;
        moved_ = false;
        pressedRow_ = kNone;
    }

    size_t ListView::draw(ui::Context& ui, ui::Rect area, size_t count, const std::function<ListRow(size_t)>& row) {
        if (count == 0) {
            first_ = 0;
            return kNone;
        }
        const int16_t rows =
            std::max<int16_t>(1, static_cast<int16_t>((area.h + kRowGap) / (kMinimumRowHeight + kRowGap)));
        const size_t visible = std::min<size_t>(count, static_cast<size_t>(rows));
        const bool scrolls = count > visible;
        const int16_t rowHeight = static_cast<int16_t>((area.h - kRowGap * (rows - 1)) / rows);
        const int16_t rowStep = static_cast<int16_t>(rowHeight + kRowGap);
        const ui::Rect list{area.x, area.y,
                            static_cast<int16_t>(scrolls ? area.w - kScrollWidth - kRowGap * 2 : area.w), area.h};
        const size_t lastFirst = count - visible;
        if (revealed_ != kNone) {
            const size_t index = std::min(revealed_, count - 1);
            if (index < first_ || index >= first_ + visible)
                first_ = index > (visible - 1) / 2 ? index - (visible - 1) / 2 : 0;
            revealed_ = kNone;
        }
        first_ = std::min(first_, lastFirst);

        if (scrolls) {
            const ui::Rect column{static_cast<int16_t>(list.x + list.w + kRowGap * 2), area.y, kScrollWidth, area.h};
            const int16_t buttonHeight = static_cast<int16_t>((column.h - kPositionHeight - 4) / 2);
            if (ui.controlButton({column.x, column.y, column.w, buttonHeight}, ui::Icon::Up))
                first_ = first_ > visible ? first_ - visible : 0;
            if (ui.controlButton({column.x, static_cast<int16_t>(column.y + column.h - buttonHeight), column.w,
                                  buttonHeight},
                                 ui::Icon::Down))
                first_ = std::min(first_ + visible, lastFirst);
            char label[16];
            std::snprintf(label, sizeof(label), "%u/%u", static_cast<unsigned>(first_ + visible),
                          static_cast<unsigned>(count));
            ui.label({column.x, static_cast<int16_t>(column.y + buttonHeight + 2), column.w, kPositionHeight}, label,
                     1, ui::themes::ColorRole::Muted, ui::TextAlign::Center);
        }

        const auto rowRect = [&](size_t index) {
            return ui::Rect{list.x, static_cast<int16_t>(list.y + rowStep * static_cast<int16_t>(index)), list.w,
                            rowHeight};
        };
        size_t result = kNone;
        const ui::Touch* touch = ui.touch();
        if (touch != nullptr && ui::hasTouch(*touch, ui::TouchStart) && ui::contains(list, touch->x, touch->y)) {
            dragging_ = true;
            moved_ = false;
            startY_ = touch->y;
            dragStartFirst_ = first_;
            pressedRow_ = kNone;
            for (size_t index = 0; index < visible; ++index)
                if (ui::contains(rowRect(index), touch->x, touch->y))
                    pressedRow_ = index;
        }
        if (dragging_ && touch != nullptr
            && (ui::hasTouch(*touch, ui::TouchMove) || ui::hasTouch(*touch, ui::TouchRelease))) {
            const int32_t dy = static_cast<int32_t>(touch->y) - startY_;
            moved_ = moved_ || std::abs(dy) > kDragThreshold;
            if (moved_) {
                const int32_t rowsMoved = (-dy + (dy < 0 ? rowStep / 2 : -rowStep / 2)) / rowStep;
                first_ = static_cast<size_t>(std::clamp<int32_t>(static_cast<int32_t>(dragStartFirst_) + rowsMoved, 0,
                                                                 static_cast<int32_t>(lastFirst)));
            }
        }
        if (dragging_ && touch != nullptr && ui::hasTouch(*touch, ui::TouchRelease)) {
            const size_t pressed = pressedRow_;
            dragging_ = false;
            pressedRow_ = kNone;
            if (!moved_ && ui::hasTouch(*touch, ui::TouchPress) && pressed < visible
                && ui::contains(rowRect(pressed), touch->x, touch->y))
                result = first_ + pressed;
        }

        for (size_t index = 0; index < visible; ++index) {
            const ListRow item = row(first_ + index);
            const bool down = dragging_ && !moved_ && pressedRow_ == index;
            uint32_t state = ui::Context::signature(item.title);
            state = ui::Context::signature(item.detail, state);
            state = ui::Context::signature(item.trailing, state);
            state = ui::Context::combine(state, static_cast<uint8_t>(item.progress));
            state = ui::Context::combine(state, item.marked);
            state = ui::Context::combine(state, down);
            const ui::Rect rect = rowRect(index);
            if (ui.redraw(rect, state))
                drawRow(ui, rect, item, down);
        }
        return result;
    }

} // namespace screens
