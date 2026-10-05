#include "ui/screens/AudioScreens.h"

#include "ui/screens/watch/Layout.h"

// Watch layouts have no audio tabs; these screens only exist so shared app code links.
namespace screens {

    AudioRequest AudiobooksScreen::drawList(ui::Context& ui, std::span<const AudiobookEntry>, const NowPlaying&,
                                            Screen& screen) {
        detail::navigation(ui, Screen::Read, screen);
        ui.label(detail::tabContent(ui), ui.text(UiText::Unavailable), 2, ui::themes::Muted, ui::TextAlign::Center);
        return {};
    }

    AudioRequest AudiobooksScreen::drawPlayer(ui::Context& ui, const NowPlaying& playing, Screen& screen) {
        return drawList(ui, {}, playing, screen);
    }

    void VoiceScreen::reset() {}

    AudioRequest VoiceScreen::draw(ui::Context& ui, std::span<const MemoEntry>, const NowPlaying& playing, bool,
                                   uint32_t, Screen& screen) {
        AudiobooksScreen fallback;
        return fallback.drawList(ui, {}, playing, screen);
    }

} // namespace screens
