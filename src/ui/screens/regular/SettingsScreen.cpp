#include "ui/screens/ScreenCommon.h"

#include <array>
#include <string>

namespace screens {
    namespace {

        enum class Entry : uint8_t {
            Reading,
            Pacing,
            ReaderLayout,
            Display,
            Network,
            Storage,
            Encryption,
            UsbTransfer,
            CompanionSync,
            RefreshRss,
            Firmware,
        };

        constexpr std::array kEntries{Entry::Reading,     Entry::Pacing,        Entry::ReaderLayout, Entry::Display,
                                      Entry::Network,     Entry::Storage,       Entry::Encryption,   Entry::UsbTransfer,
                                      Entry::CompanionSync, Entry::RefreshRss, Entry::Firmware};

    } // namespace

    // One menu for reading preferences and the device tools that used to sit under their own tab.
    Action settings(ui::Context& ui, const DeviceSummary& device, Screen& screen) {
        if (const Action action = detail::navigation(ui, Screen::Settings, screen); action != Action::None)
            return action;
        const ui::Rect content = detail::tabContent(ui);
        const uint8_t columns = content.w >= 420 ? 3 : content.w >= 280 ? 2 : 1;
        const ui::PagedGrid grid = ui.pagedGrid(content, kEntries.size(), columns, 34);

        std::string storage;
        if (device.storageReady) {
            storage = std::to_string(device.bookCount);
            storage += " ";
            storage += ui.text(UiText::Items);
        } else {
            storage = ui.text(UiText::Unavailable);
        }
        const bool encryptionOn = device.encryption == settings::NvsEncryptionState::Enabled;
        const bool encryptionAvailable =
            device.encryption == settings::NvsEncryptionState::Available && device.storageReady;
        const std::string_view encryption =
            ui.text(encryptionOn ? UiText::On : encryptionAvailable ? UiText::Off : UiText::Unavailable);

        Action result = Action::None;
        for (size_t index = grid.first; index < grid.first + grid.count; ++index) {
            const ui::Rect rect = grid.item(index);
            switch (kEntries[index]) {
            case Entry::Reading:
                if (ui.button(rect, ui.text(UiText::Reading)))
                    screen = Screen::ReadingSettings;
                break;
            case Entry::Pacing:
                if (ui.button(rect, ui.text(UiText::WordPacing)))
                    screen = Screen::PacingSettings;
                break;
            case Entry::ReaderLayout:
                if (ui.button(rect, ui.text(UiText::ReaderLayout)))
                    screen = Screen::ReaderAppearance;
                break;
            case Entry::Display:
                if (ui.button(rect, ui.text(UiText::Display)))
                    screen = Screen::InterfaceSettings;
                break;
            case Entry::Network:
                if (ui.button(rect, ui.text(UiText::NetworkUpdates)))
                    screen = Screen::NetworkSettings;
                break;
            case Entry::Storage:
                if (ui.setting(rect, ui.text(UiText::Storage), storage, ui::SettingLayout::Inline))
                    result = Action::StorageStatus;
                break;
            case Entry::Encryption: {
                if (ui.setting(rect, ui.text(UiText::Encryption), encryption, ui::SettingLayout::Inline)
                    && encryptionAvailable)
                    screen = Screen::StorageEncryption;
                // Turning protection off again is deliberate: it takes a hold, not a tap.
                const ui::Touch* touch = ui.touch();
                if (encryptionOn && touch != nullptr && ui::hasTouch(*touch, ui::TouchHold)
                    && ui::contains(rect, touch->x, touch->y))
                    screen = Screen::StorageEncryption;
                break;
            }
            case Entry::UsbTransfer:
                if (ui.button(rect, ui.text(UiText::UsbTransfer)))
                    result = Action::UsbTransfer;
                break;
            case Entry::CompanionSync:
                if (ui.button(rect, ui.text(UiText::CompanionSync)))
                    result = Action::CompanionSync;
                break;
            case Entry::RefreshRss:
                if (ui.button(rect, ui.text(UiText::RefreshRss)))
                    result = Action::RssRefresh;
                break;
            case Entry::Firmware:
                if (ui.button(rect, ui.text(UiText::OtaUpdate)))
                    screen = Screen::Ota;
                break;
            }
        }
        return result;
    }

} // namespace screens
