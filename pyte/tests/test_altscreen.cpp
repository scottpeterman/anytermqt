// tests/test_altscreen.cpp
//
// The alternate screen buffer and the two scroll sequences that share the
// buffer with it, SU (CSI S) and SD (CSI T). Driven through Stream where the
// point is the escape-sequence policy, and through Screen where the point is
// the buffer swap itself.
#include <doctest/doctest.h>

#include <string>
#include <tuple>
#include <vector>

#include "pyte/screen.h"
#include "pyte/stream.h"

using pyte::Screen;
using pyte::Stream;

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

void writeln(Screen &s, const std::string &text) {
    write(s, text);
    s.carriage_return();
    s.linefeed();
}

void fill(Screen &s, int n) {
    for (int i = 1; i <= n; ++i) {
        writeln(s, "lab" + std::to_string(i));
    }
}

}  // namespace

// --- the buffer swap -------------------------------------------------------

TEST_CASE("the alternate screen is a separate buffer with no history") {
    Screen s(10, 3);
    fill(s, 5);
    REQUIRE(s.history_size() == 3);

    s.set_alternate_screen(true);
    CHECK(s.alternate_screen());
    CHECK(s.history_size() == 0);
    CHECK(row(s, 0).empty());
    CHECK(row(s, 1).empty());

    SUBCASE("and its own output never becomes scrollback") {
        s.cursor_position(1, 1);
        fill(s, 20);
        CHECK(s.history_size() == 0);
    }

    SUBCASE("leaving restores the primary buffer untouched") {
        s.cursor_position(1, 1);
        write(s, "fullscreen app");
        s.set_alternate_screen(false);

        CHECK_FALSE(s.alternate_screen());
        CHECK(s.history_size() == 3);
        CHECK(hrow(s, 0) == "lab1");
        CHECK(row(s, 0) == "lab4");
        CHECK(row(s, 1) == "lab5");
    }
}

TEST_CASE("entering the alternate screen twice does not stack buffers") {
    Screen s(10, 2);
    fill(s, 4);
    const int before = s.history_size();

    s.set_alternate_screen(true);
    s.set_alternate_screen(true);
    s.set_alternate_screen(false);
    s.set_alternate_screen(false);

    CHECK_FALSE(s.alternate_screen());
    CHECK(s.history_size() == before);
    CHECK(hrow(s, 0) == "lab1");
}

TEST_CASE("scrolling back is a no-op on the alternate screen") {
    Screen s(10, 2);
    fill(s, 6);
    s.set_alternate_screen(true);

    s.scroll_history_up(5);
    CHECK_FALSE(s.viewing_history());
    CHECK(s.history_position() == 0);
}

TEST_CASE("entering the alternate screen leaves a scrolled-back view") {
    Screen s(10, 2);
    fill(s, 6);
    s.scroll_history_to_top();
    REQUIRE(s.viewing_history());

    s.set_alternate_screen(true);
    CHECK_FALSE(s.viewing_history());
    CHECK(row(s, 0).empty());
}

TEST_CASE("the retention budget follows the primary buffer across a swap") {
    Screen s(10, 2, 500);
    s.set_alternate_screen(true);
    CHECK(s.max_scrollback() == 500);

    s.set_max_scrollback(4);
    CHECK(s.max_scrollback() == 4);

    s.set_alternate_screen(false);
    CHECK(s.max_scrollback() == 4);
    fill(s, 10);
    CHECK(s.history_size() == 4);
}

TEST_CASE("resizing while on the alternate screen reshapes both buffers") {
    Screen s(10, 4);
    fill(s, 3);
    s.set_alternate_screen(true);
    s.resize(8, 6);

    CHECK(s.cols() == 8);
    CHECK(s.rows() == 6);

    s.set_alternate_screen(false);
    CHECK(s.cols() == 8);
    CHECK(s.rows() == 6);
    // Every row of the restored buffer is the new width, not the old one.
    for (int y = 0; y < s.rows(); ++y) {
        CHECK(s.line_text(y).size() >= 8);
    }
    CHECK(row(s, 0) == "lab1");
}

TEST_CASE("reset returns to the primary buffer") {
    Screen s(10, 2);
    fill(s, 4);
    s.set_alternate_screen(true);
    write(s, "app");

    s.reset();
    CHECK_FALSE(s.alternate_screen());
    CHECK(s.history_size() == 0);
    CHECK(row(s, 0).empty());
}

// --- the sequences that drive it -------------------------------------------

TEST_CASE("DECSET 1049 saves the cursor, switches, and clears") {
    Screen s(20, 3);
    Stream stream(s);

    stream.feed("prompt$ ");
    const int saved_x = s.cursor_x();
    stream.feed("\033[?1049h");

    CHECK(s.alternate_screen());
    CHECK(row(s, 0).empty());  // 1049 clears on the way in

    stream.feed("\033[2;1Hfullscreen");
    stream.feed("\033[?1049l");

    CHECK_FALSE(s.alternate_screen());
    CHECK(row(s, 0) == "prompt$");
    CHECK(s.cursor_x() == saved_x);  // and puts the cursor back
    CHECK(s.cursor_y() == 0);
}

TEST_CASE("DECSET 47 switches without clearing either buffer") {
    Screen s(20, 3);
    Stream stream(s);

    stream.feed("primary");
    stream.feed("\033[?47h");
    CHECK(s.alternate_screen());
    CHECK(row(s, 0).empty());  // a separate buffer, so blank to begin with

    stream.feed("\033[Halt content");
    stream.feed("\033[?47l");
    CHECK_FALSE(s.alternate_screen());
    CHECK(row(s, 0) == "primary");

    // 47 clears neither buffer, so the previous frame is still there on the
    // way back in. That is what separates it from 1047 and 1049.
    stream.feed("\033[?47h");
    CHECK(row(s, 0) == "alt content");
}

TEST_CASE("DECSET 1047 clears the alternate screen on the way out") {
    Screen s(20, 3);
    Stream stream(s);

    stream.feed("\033[?1047h");
    stream.feed("\033[Halt content");
    stream.feed("\033[?1047l");
    CHECK_FALSE(s.alternate_screen());

    // Re-entering must not inherit the previous application's last frame.
    stream.feed("\033[?1047h");
    CHECK(row(s, 0).empty());
}

TEST_CASE("a mode list dispatches every parameter, not just the first") {
    Screen s(20, 3);
    Stream stream(s);

    stream.feed("\033[?1049;1000;2004h");
    CHECK(s.alternate_screen());

    stream.feed("\033[?2004;1049;1000l");
    CHECK_FALSE(s.alternate_screen());
    CHECK(stream.unhandled_count() == 0);  // mouse and paste are recognised
}

TEST_CASE("DECSET 1048 round-trips the cursor without swapping buffers") {
    Screen s(20, 3);
    Stream stream(s);

    stream.feed("\033[2;5H\033[?1048h");
    stream.feed("\033[1;1H");
    stream.feed("\033[?1048l");

    CHECK_FALSE(s.alternate_screen());
    CHECK(s.cursor_y() == 1);
    CHECK(s.cursor_x() == 4);
}

// --- SU / SD ---------------------------------------------------------------

TEST_CASE("SU scrolls the screen up and retains what leaves the top") {
    Screen s(10, 3);
    Stream stream(s);
    stream.feed("lab1\r\nlab2\r\nlab3");
    s.cursor_position(1, 1);

    stream.feed("\033[2S");

    CHECK(row(s, 0) == "lab3");
    CHECK(row(s, 1).empty());
    CHECK(s.history_size() == 2);
    CHECK(hrow(s, 0) == "lab1");
    CHECK(hrow(s, 1) == "lab2");
    CHECK(s.cursor_y() == 0);  // SU does not move the cursor
    CHECK(s.cursor_x() == 0);
}

TEST_CASE("SD scrolls the screen down without pulling from history") {
    Screen s(10, 3);
    Stream stream(s);
    stream.feed("lab1\r\nlab2\r\nlab3");
    stream.feed("\033[T");

    CHECK(row(s, 0).empty());
    CHECK(row(s, 1) == "lab1");
    CHECK(row(s, 2) == "lab2");
    CHECK(s.history_size() == 0);
}

TEST_CASE("SU and SD honour the scroll region") {
    Screen s(6, 4);
    Stream stream(s);
    stream.feed("\033[1;1Hhdr");
    stream.feed("\033[2;1Haaa");
    stream.feed("\033[3;1Hbbb");
    stream.feed("\033[2;3r");  // region is rows 1-2, top of screen excluded
    stream.feed("\033[S");

    CHECK(row(s, 0) == "hdr");  // outside the region, untouched
    CHECK(row(s, 1) == "bbb");
    CHECK(row(s, 2).empty());
    CHECK(s.history_size() == 0);  // region does not include the screen top
}

TEST_CASE("SU with no parameter scrolls one line") {
    Screen s(10, 2);
    Stream stream(s);
    stream.feed("lab1\r\nlab2");
    stream.feed("\033[S");
    CHECK(row(s, 0) == "lab2");
    CHECK(s.history_size() == 1);
}

TEST_CASE("the five-parameter CSI T is mouse tracking, not a scroll") {
    Screen s(10, 3);
    Stream stream(s);
    stream.feed("lab1\r\nlab2\r\nlab3");
    const std::string before = s.dump();

    stream.feed("\033[1;2;3;4;5T");

    CHECK(s.dump() == before);
    CHECK(stream.unhandled_count() == 1);
}

// --- mode reporting --------------------------------------------------------

TEST_CASE("every mode is reported, acted on or not") {
    Screen s(20, 3);
    Stream stream(s);

    std::vector<std::tuple<int, bool, bool>> seen;
    stream.set_mode_handler([&](int mode, bool set, bool priv) {
        seen.emplace_back(mode, set, priv);
    });

    stream.feed("\033[?1h");      // DECCKM: the buffer ignores it
    stream.feed("\033[?7l");      // DECAWM: the buffer acts on it
    stream.feed("\033[?25;2004h");
    stream.feed("\033[4h");       // an ANSI mode, no private marker

    REQUIRE(seen.size() == 5);
    CHECK(seen[0] == std::make_tuple(1, true, true));
    CHECK(seen[1] == std::make_tuple(7, false, true));
    CHECK(seen[2] == std::make_tuple(25, true, true));
    CHECK(seen[3] == std::make_tuple(2004, true, true));
    CHECK(seen[4] == std::make_tuple(4, true, false));

    // Reported and still acted on, not reported instead of acted on.
    CHECK_FALSE(s.autowrap());
}

TEST_CASE("no mode handler is not an error") {
    Screen s(20, 3);
    Stream stream(s);
    stream.feed("\033[?1;25;2004h\033[?1049h");
    CHECK(s.alternate_screen());
}
