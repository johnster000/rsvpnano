#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

#include "fonts/AlphaFont.h"
#include "ui/Ui.h"

// Line layout for the horizontal scrub view: the tail of the previous sentence, the sentence the reader will
// resume from, and the start of the next one, each beginning on its own line.
namespace screens::sentenceScrub {

    enum class Part : uint8_t {
        Previous,
        Current,
        Next,
    };

    struct Line {
        size_t start = 0;
        size_t end = 0;
        Part part = Part::Current;
        bool leadingEllipsis = false;
        bool trailingEllipsis = false;
    };

    // Word index boundaries; an empty range means that sentence is absent.
    struct Sentences {
        size_t previous = 0;
        size_t current = 0;
        size_t next = 0;
        size_t after = 0;
    };

    inline constexpr size_t kMaximumLines = 6;

    struct Layout {
        std::array<Line, kMaximumLines> lines{};
        size_t count = 0;
    };

    struct Metrics {
        int16_t width = 0;
        int16_t gap = 0;
        int16_t ellipsis = 0;
        size_t lines = 4;
    };

    using Advance = std::function<int16_t(size_t)>;
    // True when a word joins the previous one without a space, as CJK text does.
    using Attaches = std::function<bool(size_t)>;

    Layout layout(const Sentences& sentences, const Metrics& metrics, const Advance& advance,
                  const Attaches& attaches);

    // Where the words and their faces come from; `wordAt` may reuse its buffer between calls.
    struct Source {
        std::function<std::string_view(size_t)> wordAt;
        std::function<size_t(size_t)> family;
        std::function<const ui::fonts::AlphaFont&(size_t)> font;
    };

    // Full-screen scrub view: a book progress line, then the layout above with the current sentence marked.
    void draw(ui::Context& ui, ui::fonts::AlphaTextRenderer<640>& text, const Sentences& sentences, uint8_t percent,
              uint32_t state, const Source& source);

} // namespace screens::sentenceScrub
