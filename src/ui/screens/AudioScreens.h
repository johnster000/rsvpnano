#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "audio/AudioStatus.h"
#include "ui/Ui.h"
#include "ui/screens/ListView.h"
#include "ui/screens/Screens.h"

namespace screens {

    // What a tap on an audio screen asks the app to do; the screens never touch the audio engine.
    struct AudioRequest {
        enum class Type : uint8_t {
            None,
            Play,
            TogglePause,
            Skip,
            Chapter,
            Volume,
            Record,
            Delete,
        };
        Type type = Type::None;
        int32_t value = 0;
        size_t index = SIZE_MAX;
        // A tab or power action from the navigation drawn with the screen.
        Action action = Action::None;
    };

    struct AudiobookEntry {
        std::string path;
        std::string name;
        uint8_t percent = 0;
        bool started = false;
    };

    struct MemoEntry {
        std::string path;
        uint32_t number = 0;
        uint64_t durationMs = 0;
    };

    // What is loaded in the audio engine, for the screens to show.
    struct NowPlaying {
        audio::Status status;
        std::string_view path;
        std::string_view title;
        std::string_view chapter;
    };

    class AudiobooksScreen {
    public:
        AudioRequest drawList(ui::Context& ui, std::span<const AudiobookEntry> books, const NowPlaying& playing,
                              Screen& screen);
        AudioRequest drawPlayer(ui::Context& ui, const NowPlaying& playing, Screen& screen);
        void revealCurrent() {
            revealCurrent_ = true;
        }

    private:
        ListView list_;
        bool revealCurrent_ = true;
    };

    class VoiceScreen {
    public:
        AudioRequest draw(ui::Context& ui, std::span<const MemoEntry> memos, const NowPlaying& playing,
                          bool microphone, uint32_t nowMs, Screen& screen);
        void reset();

    private:
        ListView list_;
        size_t selected_ = SIZE_MAX;
        uint32_t deleteArmedAtMs_ = 0;
        bool deleteArmed_ = false;
    };

} // namespace screens
