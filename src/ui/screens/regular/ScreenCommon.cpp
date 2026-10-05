#include "ui/screens/ScreenCommon.h"

#include <algorithm>

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

        Action topNavigation(ui::Context& ui, Screen active, Screen& screen) {
            const int16_t barHeight = topBarHeight(ui);
            const int16_t powerX = static_cast<int16_t>(ui.width() - kPowerSize - 8);
            ui::Row tabs{{0, 0, static_cast<int16_t>(powerX - 4), barHeight}};
            const int16_t tabWidth = static_cast<int16_t>(tabs.bounds.w / 4);
            // Narrow tabs give their labels the room an icon would take.
            const bool icons = tabWidth >= 140;
            const auto tab = [&](UiText text, bool selected, ui::Icon icon, Screen destination, bool last = false) {
                const int16_t width =
                    last ? static_cast<int16_t>(tabs.bounds.w - tabs.cursor) : tabWidth;
                if (ui.tab(tabs.next(width), ui.text(text), selected, icons ? icon : ui::Icon::None))
                    screen = destination;
            };
            tab(UiText::Read,
                active == Screen::Read || active == Screen::Library || active == Screen::Chapters
                    || active == Screen::BookFonts,
                ui::Icon::Books, Screen::Read);
            tab(UiText::Settings,
                active == Screen::Settings || active == Screen::ReadingSettings || active == Screen::InterfaceSettings
                    || active == Screen::PacingSettings || active == Screen::ReaderAppearance
                    || active == Screen::NetworkSettings,
                ui::Icon::Edit, Screen::Settings);
            tab(UiText::Device,
                active == Screen::Device || active == Screen::StorageEncryption || active == Screen::Sync
                    || active == Screen::Ota,
                ui::Icon::Device, Screen::Device);
            tab(UiText::Focus,
                active == Screen::FocusTimers || active == Screen::FocusEditor || active == Screen::FocusNameEdit
                    || active == Screen::FocusSession,
                ui::Icon::Hourglass, Screen::FocusTimers, true);
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

        const int16_t tabHeight = static_cast<int16_t>(ui.height() / 4);
        ui::Column tabs{{0, 0, kRailWidth, ui.height()}};
        if (ui.tab(tabs.next(tabHeight), ui.text(UiText::Read),
                   active == Screen::Read || active == Screen::Library || active == Screen::Chapters
                       || active == Screen::BookFonts,
                   ui::Icon::Books)) {
            screen = Screen::Read;
        }
        if (ui.tab(tabs.next(tabHeight), ui.text(UiText::Settings),
                   active == Screen::Settings || active == Screen::ReadingSettings
                       || active == Screen::InterfaceSettings || active == Screen::PacingSettings
                       || active == Screen::ReaderAppearance || active == Screen::NetworkSettings,
                   ui::Icon::Edit)) {
            screen = Screen::Settings;
        }
        if (ui.tab(tabs.next(tabHeight), ui.text(UiText::Device),
                   active == Screen::Device || active == Screen::StorageEncryption || active == Screen::Sync
                       || active == Screen::Ota,
                   ui::Icon::Device)) {
            screen = Screen::Device;
        }
        if (ui.tab(tabs.next(static_cast<int16_t>(ui.height() - tabHeight * 3)), ui.text(UiText::Focus),
                   active == Screen::FocusTimers || active == Screen::FocusEditor || active == Screen::FocusNameEdit
                       || active == Screen::FocusSession,
                   ui::Icon::Hourglass)) {
            screen = Screen::FocusTimers;
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
