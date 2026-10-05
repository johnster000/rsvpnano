#include "ui/screens/ScreenCommon.h"

#include <algorithm>
#include <array>

namespace screens::detail {

    namespace {
        constexpr int16_t kRailWidth = 136;
        constexpr int16_t kContentGap = 12;
        // The side-rail LCD powers off from its PWR key, so its content runs to the right edge.
        constexpr int16_t kRightMargin = 8;
        constexpr int16_t kPowerSize = 36;

        // Short, wide panels keep the side rail; squarer panels put the same tabs across the top.
        bool sideRail(ui::Context& ui) {
            return ui.width() >= 620 && ui.height() >= 150 && ui.height() <= 240;
        }

        bool topBar(ui::Context& ui) {
            return !sideRail(ui) && ui.height() >= 300;
        }

        int16_t topBarHeight(ui::Context& ui) {
            return std::clamp<int16_t>(static_cast<int16_t>(ui.height() / 8), 48, 64);
        }

        // Read, Audiobooks, Voice and Settings; Settings also holds what the Device tab used to.
        struct Tab {
            UiText label;
            ui::Icon icon;
            Screen destination;
        };
        constexpr std::array<Tab, 4> kTabs{{
            {UiText::Read, ui::Icon::Books, Screen::Read},
            {UiText::Audiobooks, ui::Icon::Headphones, Screen::Audiobooks},
            {UiText::Voice, ui::Icon::Microphone, Screen::Voice},
            {UiText::Settings, ui::Icon::Edit, Screen::Settings},
        }};

        size_t activeTab(Screen active) {
            switch (active) {
            case Screen::Read:
            case Screen::Library:
            case Screen::Chapters:
            case Screen::BookFonts:
                return 0;
            case Screen::Audiobooks:
            case Screen::AudiobookPlayer:
                return 1;
            case Screen::Voice:
                return 2;
            default:
                return 3;
            }
        }

        Action topNavigation(ui::Context& ui, Screen active, Screen& screen) {
            const int16_t barHeight = topBarHeight(ui);
            const int16_t powerX = static_cast<int16_t>(ui.width() - kPowerSize - 8);
            ui::Row tabs{{0, 0, static_cast<int16_t>(powerX - 4), barHeight}};
            const int16_t tabWidth = static_cast<int16_t>(tabs.bounds.w / kTabs.size());
            // Narrow tabs give their labels the room an icon would take.
            const bool icons = tabWidth >= 140;
            const size_t selected = activeTab(active);
            for (size_t index = 0; index < kTabs.size(); ++index) {
                const int16_t width =
                    index + 1 == kTabs.size() ? static_cast<int16_t>(tabs.bounds.w - tabs.cursor) : tabWidth;
                if (ui.tab(tabs.next(width), ui.text(kTabs[index].label), index == selected,
                           icons ? kTabs[index].icon : ui::Icon::None))
                    screen = kTabs[index].destination;
            }
            if (ui.iconButton({powerX, static_cast<int16_t>((barHeight - kPowerSize) / 2), kPowerSize, kPowerSize},
                              ui::Icon::Power)) {
                return Action::PowerOff;
            }
            return Action::None;
        }
    } // namespace

    bool back(ui::Context& ui, ui::Rect rect) {
        return ui.controlButton(rect, ui::Icon::Back, rect.w >= 108 ? ui.text(UiText::Back) : std::string_view{});
    }

    Action navigation(ui::Context& ui, Screen active, Screen& screen) {
        if (topBar(ui))
            return topNavigation(ui, active, screen);
        if (!sideRail(ui))
            return Action::None;

        const int16_t tabHeight = static_cast<int16_t>(ui.height() / kTabs.size());
        ui::Column tabs{{0, 0, kRailWidth, ui.height()}};
        const size_t selected = activeTab(active);
        for (size_t index = 0; index < kTabs.size(); ++index) {
            const int16_t height = index + 1 == kTabs.size()
                                     ? static_cast<int16_t>(ui.height() - tabHeight * (kTabs.size() - 1))
                                     : tabHeight;
            if (ui.tab(tabs.next(height), ui.text(kTabs[index].label), index == selected, kTabs[index].icon))
                screen = kTabs[index].destination;
        }
        return Action::None;
    }

    ui::Rect content(ui::Context& ui) {
        return {8, 8, static_cast<int16_t>(ui.width() - 16), static_cast<int16_t>(ui.height() - 16)};
    }

    ui::Rect tabContent(ui::Context& ui) {
        if (sideRail(ui)) {
            const int16_t x = static_cast<int16_t>(kRailWidth + kContentGap);
            return {x, 8, static_cast<int16_t>(ui.width() - x - kRightMargin), static_cast<int16_t>(ui.height() - 16)};
        }
        if (!topBar(ui))
            return content(ui);
        const int16_t top = static_cast<int16_t>(topBarHeight(ui) + kContentGap);
        return {8, top, static_cast<int16_t>(ui.width() - 16), static_cast<int16_t>(std::max(0, ui.height() - top - 8))};
    }

} // namespace screens::detail

namespace screens {
    Screen deviceHome() {
        return Screen::Settings;
    }
} // namespace screens
