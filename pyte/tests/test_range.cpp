// tests/test_range.cpp
//
// Absolute line addressing and text_in_range().
//
// These sit in the core rather than beside the widget because the hard parts
// are core parts: which absolute line a row is, what survives scrollback
// trimming, and where a column falls when a double-width glyph has already
// consumed two columns and contributed one character. None of that needs Qt,
// a display, or a pty to be wrong.

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "pyte/screen.h"

using pyte::Screen;

namespace {

void write(Screen &s, const std::string &ascii) {
    for (char c : ascii) {
        s.draw(static_cast<char32_t>(static_cast<unsigned char>(c)));
    }
}

// A line of text followed by a newline, the way output actually arrives.
void writeLine(Screen &s, const std::string &ascii) {
    write(s, ascii);
    s.carriage_return();
    s.linefeed();
}

void writeWide(Screen &s, const std::u32string &text) {
    for (char32_t c : text) {
        s.draw(c);
    }
}

}  // namespace

TEST_CASE("absolute line numbers span history and live screen") {
    Screen s(20, 4, 100);

    for (int i = 1; i <= 10; ++i) {
        writeLine(s, "line-" + std::to_string(i));
    }

    // Ten lines written into a four-row screen: six have scrolled off, and
    // the numbering covers both halves without a discontinuity.
    CHECK(s.first_line() == 0);
    CHECK(s.base_line() == s.first_line() + s.history_size());
    CHECK(s.end_line() == s.base_line() + 4);

    // The oldest line is still addressable, and it is the first one written.
    CHECK(s.line_at(s.first_line()).substr(0, 6) == "line-1");

    // The live screen sits at the top end of the same axis. Asserted as a
    // relationship rather than a line number: exactly which of the ten lines
    // lands on row 0 depends on where the last linefeed left the cursor,
    // which is a fact about linefeed, not about addressing.
    CHECK(s.line_at(s.base_line()) == s.line_text(0));
    CHECK(s.line_at(s.base_line() + 1) == s.line_text(1));
}

TEST_CASE("view_top converts a viewport row to an absolute line") {
    Screen s(20, 4, 100);
    for (int i = 1; i <= 10; ++i) {
        writeLine(s, "line-" + std::to_string(i));
    }

    // Following live output, the viewport shows the live screen.
    CHECK(s.view_top() == s.base_line());
    CHECK(s.line_at(s.view_top() + 0) == s.line_text(0));

    // Scrolled back, the same conversion still holds -- which is the whole
    // reason a selection is anchored to absolute lines. Viewport row 0 now
    // means different text; its absolute line does not.
    s.scroll_history_to_top();
    CHECK(s.view_top() == s.first_line());
    CHECK(s.line_at(s.view_top() + 1) == s.line_text(1));
}

TEST_CASE("lines_in_range clamps rather than failing") {
    Screen s(20, 4, 100);
    for (int i = 1; i <= 8; ++i) {
        writeLine(s, "line-" + std::to_string(i));
    }

    const std::vector<std::string> all =
        s.lines_in_range(s.first_line(), s.end_line());
    CHECK(all.size() == static_cast<std::size_t>(s.end_line() - s.first_line()));

    // A range reaching past either end comes back with what exists. A
    // selection dragged to the top of a long history and left there while
    // output arrives can outlive its own oldest line.
    const std::vector<std::string> over =
        s.lines_in_range(s.first_line() - 50, s.end_line() + 50);
    CHECK(over.size() == all.size());

    // An inverted or empty range is empty, not a crash.
    CHECK(s.lines_in_range(5, 5).empty());
    CHECK(s.lines_in_range(9, 3).empty());
}

TEST_CASE("text_in_range slices columns on one line") {
    Screen s(20, 4, 100);
    write(s, "hello world");

    const int line = s.base_line();

    // End column is exclusive.
    CHECK(s.text_in_range(line, 0, line, 5) == "hello");
    CHECK(s.text_in_range(line, 6, line, 11) == "world");
    CHECK(s.text_in_range(line, 0, line, 11) == "hello world");

    // Selecting past the text picks up padding, which is trimmed: a terminal
    // row is padded to full width and nobody wants the padding.
    CHECK(s.text_in_range(line, 0, line, 20) == "hello world");

    // Nothing selected is nothing returned.
    CHECK(s.text_in_range(line, 5, line, 5).empty());
    CHECK(s.text_in_range(line, 8, line, 2).empty());
}

TEST_CASE("text_in_range spans lines with the ends clipped") {
    Screen s(20, 4, 100);
    writeLine(s, "first line here");
    writeLine(s, "second line");
    write(s, "third line");

    const int first = s.base_line();

    // From mid-first to mid-third: the first row is cut at its start column,
    // the last at its end column, and everything between comes whole.
    CHECK(s.text_in_range(first, 6, first + 2, 5) ==
          "line here\nsecond line\nthird");

    // Whole rows, with the padding trimmed off each.
    CHECK(s.text_in_range(first, 0, first + 1, 20) ==
          "first line here\nsecond line");
}

TEST_CASE("text_in_range reaches into scrollback") {
    Screen s(20, 4, 100);
    for (int i = 1; i <= 12; ++i) {
        writeLine(s, "line-" + std::to_string(i));
    }

    REQUIRE(s.history_size() > 4);

    // A selection dragged from history into the live screen, which is the
    // case viewport coordinates cannot express at all.
    const std::string text =
        s.text_in_range(s.first_line(), 0, s.first_line() + 2, 20);
    CHECK(text == "line-1\nline-2\nline-3");

    // Still true after the viewport moves: the text is addressed absolutely,
    // not relative to what is on show.
    s.scroll_history_to_top();
    CHECK(s.text_in_range(s.first_line(), 0, s.first_line(), 20) == "line-1");
    s.scroll_history_to_bottom();
    CHECK(s.text_in_range(s.first_line(), 0, s.first_line(), 20) == "line-1");
}

TEST_CASE("text_in_range counts columns, not characters") {
    Screen s(20, 4, 100);

    // Two double-width glyphs then ASCII. On the grid this is
    //   col 0-1: 世   col 2-3: 界   col 4: o   col 5: k
    // so a character index and a column index part company at column 2.
    writeWide(s, U"\u4e16\u754cok");

    const int line = s.base_line();

    // Both wide glyphs, by column.
    CHECK(s.text_in_range(line, 0, line, 4) == "\xe4\xb8\x96\xe7\x95\x8c");

    // The ASCII after them starts at column 4, not column 2. Slicing the
    // rendered string instead of the cells would return "\u4e16\u754c" here.
    CHECK(s.text_in_range(line, 4, line, 6) == "ok");

    // A cut through the middle of a wide glyph takes the glyph rather than
    // half of it: the left cell carries the character, the right is a
    // continuation and contributes nothing.
    CHECK(s.text_in_range(line, 0, line, 1) == "\xe4\xb8\x96");
    CHECK(s.text_in_range(line, 1, line, 2).empty());
}

TEST_CASE("text_in_range on the alternate screen sees only the screen") {
    Screen s(20, 4, 100);
    for (int i = 1; i <= 10; ++i) {
        writeLine(s, "primary-" + std::to_string(i));
    }
    const int history_before = s.history_size();
    REQUIRE(history_before > 0);

    s.set_alternate_screen(true);
    write(s, "alt content");

    // No scrollback on the alternate screen, so the absolute range collapses
    // to the visible rows -- which is what makes a selection there behave
    // sensibly rather than reaching into the primary's history.
    CHECK(s.first_line() == s.base_line());
    CHECK(s.end_line() - s.base_line() == 4);

    // Addressed through view_top() rather than base_line(). The two are the
    // same while following live output, but view_top() is what a click
    // converts through, so it is the one worth pinning -- and the text was
    // written wherever the cursor happened to be when the buffer was
    // switched, which is not necessarily row 0.
    const int written = s.view_top() + s.cursor_y();
    CHECK(s.text_in_range(written, 0, written, 20) == "alt content");

    // Leaving restores the primary and its history untouched.
    s.set_alternate_screen(false);
    CHECK(s.history_size() == history_before);
    CHECK(s.text_in_range(s.first_line(), 0, s.first_line(), 20) == "primary-1");
}