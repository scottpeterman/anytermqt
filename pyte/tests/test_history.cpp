// tests/test_history.cpp
#include <doctest/doctest.h>

#include <string>

#include "pyte/screen.h"

using pyte::Screen;

namespace {

std::string trim(const std::string &s) {
    const std::size_t end = s.find_last_not_of(' ');
    return end == std::string::npos ? std::string() : s.substr(0, end + 1);
}

std::string row(const Screen &s, int y) { return trim(s.line_text(y)); }

std::string hrow(const Screen &s, int i) { return trim(s.history_line_text(i)); }

void write(Screen &s, const std::string &ascii) {
    for (char c : ascii) {
        s.draw(static_cast<char32_t>(static_cast<unsigned char>(c)));
    }
}

// Write `text` on its own line and move to the next one, scrolling at the
// bottom the way a shell printing output does.
void writeln(Screen &s, const std::string &text) {
    write(s, text);
    s.carriage_return();
    s.linefeed();
}

// Fill a screen with numbered lab lines: lab1, lab2, ... labn.
void fill(Screen &s, int n) {
    for (int i = 1; i <= n; ++i) {
        writeln(s, "lab" + std::to_string(i));
    }
}

}  // namespace

TEST_CASE("rows scrolled off the top are retained") {
    Screen s(10, 3);
    fill(s, 5);

    CHECK(s.history_size() == 3);
    CHECK(hrow(s, 0) == "lab1");
    CHECK(hrow(s, 1) == "lab2");
    CHECK(hrow(s, 2) == "lab3");
    CHECK(row(s, 0) == "lab4");
    CHECK(row(s, 1) == "lab5");
    CHECK(row(s, 2).empty());
}

TEST_CASE("history is capped at the retention budget, oldest first") {
    Screen s(10, 3, 2);
    fill(s, 8);

    CHECK(s.max_scrollback() == 2);
    CHECK(s.history_size() == 2);
    CHECK(hrow(s, 0) == "lab5");
    CHECK(hrow(s, 1) == "lab6");
    CHECK(row(s, 0) == "lab7");
    CHECK(row(s, 1) == "lab8");
}

TEST_CASE("a zero budget keeps nothing") {
    Screen s(10, 2, 0);
    fill(s, 6);

    CHECK(s.history_size() == 0);
    CHECK(s.history_lines().empty());
    CHECK(row(s, 0) == "lab6");
}

TEST_CASE("lowering the budget trims immediately") {
    Screen s(10, 2);
    fill(s, 10);
    CHECK(s.history_size() == 9);

    s.set_max_scrollback(3);
    CHECK(s.history_size() == 3);
    CHECK(hrow(s, 0) == "lab7");
    CHECK(hrow(s, 2) == "lab9");
}

TEST_CASE("scrolling back moves what the read API shows") {
    Screen s(10, 2);
    fill(s, 5);
    CHECK_FALSE(s.viewing_history());
    CHECK(row(s, 0) == "lab5");

    s.scroll_history_up(2);
    CHECK(s.viewing_history());
    CHECK(s.history_position() == 2);
    CHECK(row(s, 0) == "lab3");
    CHECK(row(s, 1) == "lab4");
    CHECK(s.at(0, 0).ch == U'l');

    SUBCASE("and scrolling forward again returns to the live screen") {
        s.scroll_history_down(2);
        CHECK_FALSE(s.viewing_history());
        CHECK(s.history_position() == 0);
        CHECK(row(s, 0) == "lab5");
    }
    SUBCASE("clamped at the oldest retained row") {
        s.scroll_history_up(999);
        CHECK(s.history_position() == s.history_size());
        CHECK(row(s, 0) == "lab1");
    }
    SUBCASE("to_bottom snaps back in one call") {
        s.scroll_history_to_bottom();
        CHECK_FALSE(s.viewing_history());
        CHECK(row(s, 0) == "lab5");
    }
}

TEST_CASE("scrolling back with no history is a no-op, not a false view") {
    Screen s(10, 3);
    write(s, "lab1");

    s.scroll_history_up(5);
    CHECK_FALSE(s.viewing_history());
    CHECK(s.history_position() == 0);
    CHECK(row(s, 0) == "lab1");
}

TEST_CASE("writing while scrolled back snaps to the live screen first") {
    Screen s(10, 2);
    fill(s, 5);
    s.scroll_history_to_top();
    REQUIRE(s.viewing_history());

    write(s, "new");
    CHECK_FALSE(s.viewing_history());
    CHECK(s.history_position() == 0);
    CHECK(row(s, 0) == "lab5");
    CHECK(row(s, 1) == "new");
    // The write landed on the live screen, not on the row that was on show.
    CHECK(hrow(s, 0) == "lab1");
}

TEST_CASE("a scroll region below the top scrolls without touching history") {
    Screen s(6, 4);
    s.set_margins(2, 4);  // rows 1-3, top of the screen excluded
    s.cursor_position(1, 1);
    write(s, "hdr");
    s.cursor_position(4, 1);
    write(s, "bot");
    s.index();  // at the bottom margin

    CHECK(s.history_size() == 0);
    CHECK(row(s, 0) == "hdr");
    CHECK(row(s, 2) == "bot");
    CHECK(row(s, 3).empty());
}

TEST_CASE("a region anchored at the top scrolls into history and leaves the rest alone") {
    // The full-screen-editor shape: everything scrolls except a status line.
    Screen s(6, 4);
    s.set_margins(1, 3);  // rows 0-2; row 3 is the status line
    s.cursor_position(4, 1);
    write(s, "status");
    s.cursor_position(1, 1);
    write(s, "aaa");
    s.cursor_position(2, 1);
    write(s, "bbb");
    s.cursor_position(3, 1);
    write(s, "ccc");

    s.index();  // at the bottom margin, region top is the screen top

    CHECK(s.history_size() == 1);
    CHECK(hrow(s, 0) == "aaa");
    CHECK(row(s, 0) == "bbb");
    CHECK(row(s, 1) == "ccc");
    CHECK(row(s, 2).empty());
    CHECK(row(s, 3) == "status");  // never dragged up into the text
}

TEST_CASE("line edits inside a region are not scrollback") {
    Screen s(6, 3);
    fill(s, 2);
    s.cursor_position(1, 1);

    SUBCASE("delete_lines") {
        s.delete_lines(1);
        CHECK(s.history_size() == 0);
        CHECK(row(s, 0) == "lab2");
    }
    SUBCASE("insert_lines") {
        s.insert_lines(1);
        CHECK(s.history_size() == 0);
        CHECK(row(s, 0).empty());
        CHECK(row(s, 1) == "lab1");
    }
}

TEST_CASE("reverse_index scrolls the region and does not pull from history") {
    Screen s(6, 2);
    fill(s, 3);
    REQUIRE(s.history_size() == 2);

    s.cursor_position(1, 1);
    s.reverse_index();

    CHECK(s.history_size() == 2);
    CHECK(row(s, 0).empty());
    CHECK(row(s, 1) == "lab3");
}

TEST_CASE("erase_in_display 2 keeps history, 3 clears it") {
    Screen s(6, 2);

    SUBCASE("mode 2 leaves saved lines alone") {
        fill(s, 5);
        REQUIRE(s.history_size() == 4);
        s.erase_in_display(2);
        CHECK(s.history_size() == 4);
        CHECK(hrow(s, 0) == "lab1");
        CHECK(row(s, 0).empty());
    }
    SUBCASE("mode 3 erases saved lines too") {
        fill(s, 5);
        REQUIRE(s.history_size() == 4);
        s.erase_in_display(3);
        CHECK(s.history_size() == 0);
        CHECK(row(s, 0).empty());
    }
}

TEST_CASE("reset clears the screen and its history") {
    Screen s(6, 2);
    fill(s, 5);
    s.scroll_history_up(1);
    REQUIRE(s.history_size() == 4);

    s.reset();
    CHECK(s.history_size() == 0);
    CHECK_FALSE(s.viewing_history());
    CHECK(row(s, 0).empty());
}

TEST_CASE("history_at exposes attributes of retained rows") {
    Screen s(6, 2);
    s.select_graphic_rendition({31});
    writeln(s, "red");
    s.select_graphic_rendition({0});
    writeln(s, "two");
    writeln(s, "three");
    REQUIRE(s.history_size() >= 1);

    CHECK(s.history_at(0, 0).ch == U'r');
    CHECK(s.history_at(0, 0).attrs.fg == 1);
    // Out of range reads are defined, not undefined.
    CHECK(s.history_at(0, 999).ch == U' ');
}

TEST_CASE("shrinking scrolls rows into history instead of dropping them") {
    Screen s(10, 3);
    write(s, "keepme");
    s.cursor_position(3, 9);
    s.resize(6, 2);

    CHECK(s.cols() == 6);
    CHECK(s.rows() == 2);
    CHECK(s.cursor_x() <= 5);
    CHECK(s.cursor_y() <= 1);
    CHECK(s.history_size() == 1);
    CHECK(hrow(s, 0) == "keepme");

    SUBCASE("and growing back pulls them into view") {
        s.resize(6, 3);
        CHECK(s.history_size() == 0);
        CHECK(row(s, 0) == "keepme");
    }
}

TEST_CASE("resize keeps the cursor on the line it was on") {
    Screen s(10, 4);
    fill(s, 3);        // lab1..lab3 on rows 0-2, cursor now on row 3
    write(s, "here");  // the line the cursor sits on
    REQUIRE(s.cursor_y() == 3);

    s.resize(10, 2);
    CHECK(row(s, s.cursor_y()) == "here");

    s.resize(10, 4);
    CHECK(row(s, s.cursor_y()) == "here");
}

TEST_CASE("history_lines returns retained rows oldest first") {
    Screen s(10, 2);
    fill(s, 4);
    const std::vector<std::string> lines = s.history_lines();
    REQUIRE(lines.size() == 3);
    CHECK(trim(lines[0]) == "lab1");
    CHECK(trim(lines[2]) == "lab3");
}

TEST_CASE("scroll_history_to positions absolutely, as a scrollbar needs") {
    Screen s(10, 2);
    fill(s, 6);
    REQUIRE(s.history_size() == 5);

    s.scroll_history_to(3);
    CHECK(s.history_position() == 3);
    CHECK(s.viewing_history());
    CHECK(row(s, 0) == "lab3");

    SUBCASE("0 means live, and clears the viewing flag") {
        s.scroll_history_to(0);
        CHECK(s.history_position() == 0);
        CHECK_FALSE(s.viewing_history());
        CHECK(row(s, 0) == "lab6");
    }
    SUBCASE("out of range clamps rather than failing") {
        s.scroll_history_to(999);
        CHECK(s.history_position() == s.history_size());
        s.scroll_history_to(-5);
        CHECK(s.history_position() == 0);
        CHECK_FALSE(s.viewing_history());
    }
    SUBCASE("no scrollback means every position is live") {
        Screen empty(10, 2, 0);
        empty.scroll_history_to(10);
        CHECK_FALSE(empty.viewing_history());
    }
}
