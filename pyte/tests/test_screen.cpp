// tests/test_screen.cpp
#include <doctest/doctest.h>

#include "pyte/screen.h"
#include "pyte/unicode.h"

using pyte::Screen;

namespace {

// Trailing spaces make expectations unreadable; compare trimmed rows.
std::string row(const Screen &s, int y) {
    std::string line = s.line_text(y);
    const std::size_t end = line.find_last_not_of(' ');
    return end == std::string::npos ? std::string() : line.substr(0, end + 1);
}

void write(Screen &s, const std::string &utf8) {
    // Test helper only — the real path decodes UTF-8 in Stream. This assumes
    // ASCII, which every caller below uses.
    for (char c : utf8) {
        s.draw(static_cast<char32_t>(static_cast<unsigned char>(c)));
    }
}

}  // namespace

TEST_CASE("a new screen is blank and the cursor is home") {
    Screen s(10, 3);
    CHECK(s.cols() == 10);
    CHECK(s.rows() == 3);
    CHECK(s.cursor_x() == 0);
    CHECK(s.cursor_y() == 0);
    CHECK(row(s, 0).empty());
}

TEST_CASE("drawing advances the cursor") {
    Screen s(10, 3);
    write(s, "lab1");
    CHECK(row(s, 0) == "lab1");
    CHECK(s.cursor_x() == 4);
    CHECK(s.cursor_y() == 0);
}

TEST_CASE("autowrap moves to the next line at the right margin") {
    Screen s(4, 3);
    write(s, "abcdef");
    CHECK(row(s, 0) == "abcd");
    CHECK(row(s, 1) == "ef");
    CHECK(s.cursor_y() == 1);
}

TEST_CASE("autowrap off overwrites the last column") {
    Screen s(4, 3);
    s.set_autowrap(false);
    write(s, "abcdef");
    CHECK(row(s, 0) == "abcf");
    CHECK(s.cursor_y() == 0);
}

TEST_CASE("carriage return and linefeed are independent") {
    Screen s(10, 3);
    write(s, "one");
    s.carriage_return();
    s.linefeed();
    write(s, "two");
    CHECK(row(s, 0) == "one");
    CHECK(row(s, 1) == "two");
}

TEST_CASE("linefeed at the bottom scrolls") {
    Screen s(10, 2);
    write(s, "first");
    s.carriage_return();
    s.linefeed();
    write(s, "second");
    s.carriage_return();
    s.linefeed();
    write(s, "third");
    CHECK(row(s, 0) == "second");
    CHECK(row(s, 1) == "third");
}

TEST_CASE("cursor_position is 1-based") {
    Screen s(10, 5);
    s.cursor_position(3, 4);
    CHECK(s.cursor_y() == 2);
    CHECK(s.cursor_x() == 3);
}

TEST_CASE("cursor movement clamps at the edges") {
    Screen s(10, 5);
    s.cursor_up(99);
    CHECK(s.cursor_y() == 0);
    s.cursor_back(99);
    CHECK(s.cursor_x() == 0);
    s.cursor_down(99);
    CHECK(s.cursor_y() == 4);
    s.cursor_forward(99);
    CHECK(s.cursor_x() == 9);
}

TEST_CASE("erase_in_line honours its three modes") {
    Screen s(8, 1);

    SUBCASE("to end") {
        write(s, "abcdefgh");
        s.cursor_to_column(4);
        s.erase_in_line(0);
        CHECK(row(s, 0) == "abc");
    }
    SUBCASE("to start") {
        write(s, "abcdefgh");
        s.cursor_to_column(4);
        s.erase_in_line(1);
        CHECK(row(s, 0) == "    efgh");
    }
    SUBCASE("whole line") {
        write(s, "abcdefgh");
        s.erase_in_line(2);
        CHECK(row(s, 0).empty());
    }
}

TEST_CASE("erase_in_display clears the whole buffer in mode 2") {
    Screen s(6, 3);
    write(s, "aaa");
    s.cursor_position(2, 1);
    write(s, "bbb");
    s.erase_in_display(2);
    CHECK(row(s, 0).empty());
    CHECK(row(s, 1).empty());
}

TEST_CASE("delete_characters pulls the tail left") {
    Screen s(8, 1);
    write(s, "abcdefgh");
    s.cursor_to_column(3);
    s.delete_characters(2);
    CHECK(row(s, 0) == "abefgh");
}

TEST_CASE("insert_characters pushes the tail right") {
    Screen s(8, 1);
    write(s, "abcdefgh");
    s.cursor_to_column(3);
    s.insert_characters(2);
    CHECK(row(s, 0) == "ab  cdef");
}

TEST_CASE("margins confine scrolling") {
    Screen s(6, 4);
    s.set_margins(2, 3);
    s.cursor_position(1, 1);
    write(s, "top");
    s.cursor_position(2, 1);
    write(s, "aaa");
    s.cursor_position(3, 1);
    write(s, "bbb");
    s.index();  // at the bottom margin: scrolls rows 2-3 only
    CHECK(row(s, 0) == "top");
    CHECK(row(s, 1) == "bbb");
    CHECK(row(s, 2).empty());
}

TEST_CASE("save and restore cursor round-trips position and attributes") {
    Screen s(10, 5);
    s.cursor_position(3, 5);
    s.select_graphic_rendition({1, 31});
    s.save_cursor();
    s.cursor_position(1, 1);
    s.select_graphic_rendition({0});
    s.restore_cursor();
    CHECK(s.cursor_y() == 2);
    CHECK(s.cursor_x() == 4);
    CHECK(s.current_attrs().bold);
    CHECK(s.current_attrs().fg == 1);
}

TEST_CASE("SGR sets and clears attributes") {
    Screen s(10, 2);
    s.select_graphic_rendition({1, 4, 32});
    CHECK(s.current_attrs().bold);
    CHECK(s.current_attrs().underline);
    CHECK(s.current_attrs().fg == 2);

    s.select_graphic_rendition({22});
    CHECK_FALSE(s.current_attrs().bold);
    CHECK(s.current_attrs().underline);

    s.select_graphic_rendition({0});
    CHECK_FALSE(s.current_attrs().underline);
    CHECK(s.current_attrs().fg == pyte::kDefaultColor);
}

TEST_CASE("attributes are captured per cell at draw time") {
    Screen s(10, 1);
    s.select_graphic_rendition({31});
    s.draw(U'r');
    s.select_graphic_rendition({0});
    s.draw(U'n');
    CHECK(s.at(0, 0).attrs.fg == 1);
    CHECK(s.at(1, 0).attrs.fg == pyte::kDefaultColor);
}

TEST_CASE("a wide character occupies two cells") {
    Screen s(6, 1);
    s.draw(U'\u4F60');  // CJK, width 2
    CHECK(s.cursor_x() == 2);
    CHECK(s.at(0, 0).ch == U'\u4F60');
    CHECK(s.at(1, 0).wide_continuation);
    CHECK(s.line_text(0).substr(0, 3) == pyte::to_utf8(U'\u4F60'));
}

TEST_CASE("a wide character wraps rather than splitting") {
    Screen s(3, 2);
    write(s, "ab");
    s.draw(U'\u4F60');
    CHECK(s.cursor_y() == 1);
    CHECK(s.at(0, 1).ch == U'\u4F60');
}

TEST_CASE("tab stops sit every eight columns") {
    Screen s(20, 1);
    s.tab();
    CHECK(s.cursor_x() == 8);
    s.tab();
    CHECK(s.cursor_x() == 16);
}

TEST_CASE("dirty rows accumulate until cleared") {
    Screen s(10, 3);
    s.clear_dirty();
    CHECK(s.dirty().empty());
    write(s, "x");
    CHECK(s.dirty().count(0) == 1);
    s.clear_dirty();
    CHECK(s.dirty().empty());
}

TEST_CASE("resize preserves content and clamps the cursor") {
    // Shrinking anchors the bottom of the window, so a row leaving the top is
    // pushed into scrollback rather than discarded. test_history.cpp covers
    // where it lands and that growing back recovers it.
    Screen s(10, 3);
    write(s, "keepme");
    s.cursor_position(3, 9);
    s.resize(6, 2);
    CHECK(s.cursor_x() <= 5);
    CHECK(s.cursor_y() <= 1);
    s.resize(6, 3);
    CHECK(row(s, 0) == "keepme");
}

TEST_CASE("truecolor is kept, not discarded") {
    Screen s(10, 1);

    SUBCASE("semicolon form, as btop and most applications send it") {
        s.select_graphic_rendition({38, 2, 255, 128, 64});
        const int fg = s.current_attrs().fg;
        CHECK(pyte::is_rgb(fg));
        CHECK(pyte::rgb_red(fg) == 255);
        CHECK(pyte::rgb_green(fg) == 128);
        CHECK(pyte::rgb_blue(fg) == 64);
    }
    SUBCASE("background too") {
        s.select_graphic_rendition({48, 2, 10, 20, 30});
        CHECK(pyte::rgb_green(s.current_attrs().bg) == 20);
    }
    SUBCASE("packed colours never collide with palette indices") {
        s.select_graphic_rendition({38, 2, 0, 0, 0});
        // Black as RGB must not read back as palette index 0.
        CHECK(pyte::is_rgb(s.current_attrs().fg));
        CHECK(s.current_attrs().fg != 0);
    }
    SUBCASE("a 256-colour index is still an index") {
        s.select_graphic_rendition({38, 5, 208});
        CHECK_FALSE(pyte::is_rgb(s.current_attrs().fg));
        CHECK(s.current_attrs().fg == 208);
    }
    SUBCASE("reset clears it") {
        s.select_graphic_rendition({38, 2, 1, 2, 3});
        s.select_graphic_rendition({0});
        CHECK(s.current_attrs().fg == pyte::kDefaultColor);
    }
    SUBCASE("truecolor is captured per cell like any other attribute") {
        s.select_graphic_rendition({48, 2, 200, 100, 50});
        s.draw(U'x');
        CHECK(pyte::rgb_red(s.at(0, 0).attrs.bg) == 200);
    }
}
