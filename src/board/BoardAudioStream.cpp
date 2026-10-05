#include "board/BoardAudio.h"

// Fallbacks for boards whose platform code has no audio streaming; a platform's own definitions replace these.
namespace Board::Audio {

    __attribute__((weak)) bool startOutput(uint32_t) {
        return false;
    }

    __attribute__((weak)) size_t writeFrames(const int16_t*, size_t, uint32_t) {
        return 0;
    }

    __attribute__((weak)) void stopOutput() {}

    __attribute__((weak)) void setVolume(uint8_t) {}

    __attribute__((weak)) bool hasMicrophone() {
        return false;
    }

    __attribute__((weak)) bool startInput(uint32_t) {
        return false;
    }

    __attribute__((weak)) size_t readMono(int16_t*, size_t, uint32_t) {
        return 0;
    }

    __attribute__((weak)) void stopInput() {}

} // namespace Board::Audio
