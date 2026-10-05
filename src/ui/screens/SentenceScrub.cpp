#include "ui/screens/SentenceScrub.h"

#include <algorithm>
#include <climits>
#include <vector>

#include "text/UnicodeText.h"

namespace screens::sentenceScrub {
    namespace {

        int32_t gapBefore(size_t index, size_t lineStart, const Metrics& metrics, const Attaches& attaches) {
            return index == lineStart || attaches(index) ? 0 : metrics.gap;
        }

        int32_t lineWidth(const Line& line, const Metrics& metrics, const Advance& advance, const Attaches& attaches) {
            int32_t width = 0;
            for (size_t index = line.start; index < line.end; ++index)
                width += gapBefore(index, line.start, metrics, attaches) + advance(index);
            return width;
        }

        // Greedy wrap of [start, end) into at most `limit` lines; a cut-off last line makes room for an ellipsis.
        void wrap(Layout& layout, size_t start, size_t end, Part part, size_t limit, const Metrics& metrics,
                  const Advance& advance, const Attaches& attaches) {
            size_t index = start;
            size_t used = 0;
            while (index < end && used < limit && layout.count < kMaximumLines) {
                Line& line = layout.lines[layout.count++];
                line = {.start = index, .part = part};
                int32_t width = 0;
                while (index < end) {
                    const int32_t add = gapBefore(index, line.start, metrics, attaches) + advance(index);
                    if (index > line.start && width + add > metrics.width)
                        break;
                    width += add;
                    ++index;
                }
                line.end = index;
                ++used;
            }
            if (index >= end || used == 0)
                return;
            Line& last = layout.lines[layout.count - 1];
            last.trailingEllipsis = true;
            while (last.end - last.start > 1
                   && lineWidth(last, metrics, advance, attaches) + metrics.gap + metrics.ellipsis > metrics.width)
                --last.end;
        }

    } // namespace

    Layout layout(const Sentences& sentences, const Metrics& metrics, const Advance& advance,
                  const Attaches& attaches) {
        Layout result;
        const size_t lines = std::clamp<size_t>(metrics.lines, 1, kMaximumLines);
        const bool hasPrevious = sentences.previous < sentences.current && lines >= 2;
        const bool hasNext = sentences.next < sentences.after && lines >= 3;

        if (hasPrevious) {
            // As much of the previous sentence's end as fits one line, so the reader sees what led in.
            size_t begin = sentences.current;
            int32_t width = 0;
            while (begin > sentences.previous) {
                const size_t candidate = begin - 1;
                const int32_t add =
                    advance(candidate) + (begin < sentences.current && !attaches(begin) ? metrics.gap : 0);
                const int32_t reserve = candidate > sentences.previous ? metrics.ellipsis + metrics.gap : 0;
                if (begin < sentences.current && width + add + reserve > metrics.width)
                    break;
                width += add;
                begin = candidate;
            }
            result.lines[result.count++] = {.start = begin,
                                            .end = sentences.current,
                                            .part = Part::Previous,
                                            .leadingEllipsis = begin > sentences.previous};
        }

        const size_t currentLimit = std::max<size_t>(1, lines - result.count - (hasNext ? 1 : 0));
        wrap(result, sentences.current, sentences.next, Part::Current, currentLimit, metrics, advance, attaches);
        if (hasNext && result.count < lines)
            wrap(result, sentences.next, sentences.after, Part::Next, lines - result.count, metrics, advance,
                 attaches);
        return result;
    }

    void draw(ui::Context& ui, ui::fonts::AlphaTextRenderer<640>& text, const Sentences& sentences, uint8_t percent,
              uint32_t state, const Source& source) {
        const ui::Rect screen = ui.paintBounds({0, 0, ui.width(), ui.height()});
        if (!ui.redraw(screen, state, true))
            return;

        size_t activeFamily = SIZE_MAX;
        const auto activate = [&](size_t index) {
            const size_t family = source.family(index);
            if (family == activeFamily)
                return;
            activeFamily = family;
            text.setFont(source.font(family));
        };
        activate(sentences.current);
        const ui::fonts::AlphaFont& font = source.font(activeFamily);
        const bool inkKnown = font.wordInkTop < 0 && font.wordInkBottom >= 0;
        const int16_t inkTop = inkKnown ? font.wordInkTop : static_cast<int16_t>(-font.ascent);
        const int16_t inkBottom = inkKnown ? font.wordInkBottom : static_cast<int16_t>(font.descent);
        const int16_t inkSpan = static_cast<int16_t>(inkBottom - inkTop);
        // Strike spaces are tight for whole lines; keep at least 0.28 em between words.
        const int16_t gap = std::max<int16_t>(text.glyphAdvance(' '),
                                              static_cast<int16_t>((text.pixelsPerEm() * 28 + 50) / 100));
        constexpr std::string_view kEllipsis = "...";
        const int16_t ellipsisWidth = text.textAdvance(kEllipsis);
        constexpr int16_t kBarX = 6;
        constexpr int16_t kTextX = 18;
        constexpr int16_t kTop = 7;
        const int16_t available = static_cast<int16_t>(screen.h - kTop - 3);
        const int16_t pitch = static_cast<int16_t>(inkSpan + 8);
        const size_t lines = static_cast<size_t>(std::clamp<int>(
            (available - inkSpan) / std::max<int16_t>(1, pitch) + 1, 2, static_cast<int>(kMaximumLines)));

        std::vector<int16_t> widths(sentences.after - sentences.previous, -1);
        const auto advance = [&](size_t index) -> int16_t {
            int16_t& width = widths[index - sentences.previous];
            if (width < 0) {
                activate(index);
                width = text.textAdvance(source.wordAt(index));
            }
            return width;
        };
        const auto attaches = [&](size_t index) {
            // Check one word at a time: the source may reuse its buffer for the next lookup.
            if (index == 0 || !UnicodeText::isCjkText(source.wordAt(index)))
                return false;
            return UnicodeText::isCjkText(source.wordAt(index - 1));
        };
        const Layout placed = layout(sentences,
                                     {.width = static_cast<int16_t>(screen.w - kTextX - 10),
                                      .gap = gap,
                                      .ellipsis = ellipsisWidth,
                                      .lines = lines},
                                     advance, attaches);
        const int lineCount = static_cast<int>(placed.count);
        const int16_t blockHeight = static_cast<int16_t>(inkSpan + pitch * std::max(0, lineCount - 1));
        const int16_t blockTop = static_cast<int16_t>(kTop + std::max(0, available - blockHeight) / 2);

        const uint16_t background = ui.color(ui::themes::ColorRole::Background);
        ui.paint(screen, [&](Arduino_GFX& output, ui::Rect translated) {
            Arduino_GFX& previousOutput = text.setOutput(output);
            const int16_t dx = static_cast<int16_t>(translated.x - screen.x);
            const int16_t dy = static_cast<int16_t>(translated.y - screen.y);
            output.fillRect(translated.x, translated.y, translated.w, translated.h, background);
            const int16_t barWidth = static_cast<int16_t>(screen.w - 16);
            output.fillRect(static_cast<int16_t>(8 + dx), static_cast<int16_t>(1 + dy), barWidth, 3,
                            ui.color(ui::themes::ColorRole::ProgressTrack));
            output.fillRect(static_cast<int16_t>(8 + dx), static_cast<int16_t>(1 + dy),
                            static_cast<int16_t>(barWidth * std::min<uint8_t>(percent, 100) / 100), 3,
                            ui.color(ui::themes::ColorRole::Accent));

            int16_t currentTop = INT16_MAX;
            int16_t currentBottom = INT16_MIN;
            for (int lineIndex = 0; lineIndex < lineCount; ++lineIndex) {
                const Line& line = placed.lines[static_cast<size_t>(lineIndex)];
                const int16_t baseline = static_cast<int16_t>(blockTop - inkTop + pitch * lineIndex);
                const bool current = line.part == Part::Current;
                const uint16_t ink =
                    ui.color(current ? ui::themes::ColorRole::Foreground : ui::themes::ColorRole::Muted);
                if (current) {
                    currentTop = std::min<int16_t>(currentTop, static_cast<int16_t>(baseline + inkTop));
                    currentBottom = std::max<int16_t>(currentBottom, static_cast<int16_t>(baseline + inkBottom));
                }
                int16_t x = static_cast<int16_t>(kTextX + dx);
                activate(line.start);
                text.setTextColor(ink, background);
                if (line.leadingEllipsis) {
                    text.drawString(kEllipsis, x, static_cast<int16_t>(baseline + dy));
                    x = static_cast<int16_t>(x + ellipsisWidth + gap);
                }
                for (size_t index = line.start; index < line.end; ++index) {
                    if (index > line.start && !attaches(index))
                        x = static_cast<int16_t>(x + gap);
                    const int16_t width = advance(index);
                    activate(index);
                    text.setTextColor(ink, background);
                    text.drawString(source.wordAt(index), x, static_cast<int16_t>(baseline + dy));
                    x = static_cast<int16_t>(x + width);
                }
                if (line.trailingEllipsis) {
                    text.setTextColor(ink, background);
                    text.drawString(kEllipsis, static_cast<int16_t>(x + gap), static_cast<int16_t>(baseline + dy));
                }
            }
            if (currentTop < currentBottom)
                output.fillRect(static_cast<int16_t>(kBarX + dx), static_cast<int16_t>(currentTop + dy), 3,
                                static_cast<int16_t>(currentBottom - currentTop),
                                ui.color(ui::themes::ColorRole::Accent));
            text.setOutput(previousOutput);
        });
    }

} // namespace screens::sentenceScrub
