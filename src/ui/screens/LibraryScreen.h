#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "library/BookLibrary.h"
#include "ui/screens/Screens.h"
#include "ui/Layouts.h"

class IndexedBookStore;
class StorageManager;
struct ReadingSession;

namespace screens {

    struct LibraryItem {
        const BookLibrary::Entry* book = nullptr;
        std::string chapter;
        uint8_t progress = 0;
        bool current = false;
    };

    class LibraryScreen {
    public:
        Action draw(ui::Context& ui, const std::vector<LibraryItem>& items, uint32_t nowMs, Screen& screen);
        void reset();
        void invalidate();
        const std::vector<LibraryItem>& items(StorageManager& storage, const IndexedBookStore& bookStore,
                                              const ReadingSession& session);
        size_t selectedIndex() const {
            return selectedIndex_;
        }

    private:
        bool dragging_ = false;
        bool moved_ = false;
        bool scrollToCurrent_ = true;
        uint16_t startY_ = 0;
        size_t first_ = 0;
        size_t dragStartFirst_ = 0;
        size_t pressedRow_ = SIZE_MAX;
        size_t selectedIndex_ = 0;
        std::vector<LibraryItem> items_;
        bool itemsValid_ = false;
        ui::CarouselGesture carouselGesture_;
    };

} // namespace screens
