#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

#include "ui/Ui.h"

namespace screens {

    struct ListRow {
        std::string_view title;
        std::string_view detail;
        std::string_view trailing;
        // 0-100 draws a progress line under the row; negative draws none.
        int8_t progress = -1;
        bool marked = false;
    };

    // Rows at least 48 px tall that a thumb can hit, paged with up/down buttons when they overflow and
    // dragged by whole rows. Returns the tapped row, or SIZE_MAX.
    class ListView {
    public:
        static constexpr size_t kNone = SIZE_MAX;

        size_t draw(ui::Context& ui, ui::Rect area, size_t count, const std::function<ListRow(size_t)>& row);
        // Scrolls so `index` is in view on the next draw.
        void reveal(size_t index);
        void reset();

    private:
        size_t first_ = 0;
        size_t revealed_ = kNone;
        size_t dragStartFirst_ = 0;
        size_t pressedRow_ = kNone;
        uint16_t startY_ = 0;
        bool dragging_ = false;
        bool moved_ = false;
    };

} // namespace screens
