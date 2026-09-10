// tests/test_stream.cpp
#include <doctest/doctest.h>

#include <string>

#include "pyte/screen.h"
#include "pyte/stream.h"

using pyte::Screen;
using pyte::Stream;

namespace {

std::string row(const Screen &s, int y) {
    std::string line = s.line_text(y);
    const std::size_t end = line.find_last_not_of(' ');
    return end == std::string::npos ? std::string() : line.substr(0, end + 1);
}

}  // namespace

TEST_CASE("plain text lands on the screen") {
    Screen s(20, 3);
    Stream st(s);
    st.feed("lab-sw01#");
    CHECK(row(s, 0) == "lab-sw01#");
}

TEST_CASE("CR and LF are handled as C0 controls") {
    Screen s(20, 3);
    Stream st(s);
    st.feed("one\r\ntwo");
    CHECK(row(s, 0) == "one");
    CHECK(row(s, 1) == "two");
}

TEST_CASE("CSI H positions the cursor") {
    Screen s(20, 5);
    Stream st(s);
    st.feed("\033[3;5Hhere");
    CHECK(row(s, 2) == "    here");
}

TEST_CASE("CSI H with no parameters homes the cursor") {
    Screen s(20, 5);
    Stream st(s);
    st.feed("\033[10;10Hx\033[Hy");
    CHECK(row(s, 0) == "y");
}

TEST_CASE("CSI J clears the display") {
    Screen s(20, 3);
    Stream st(s);
    st.feed("dirty\r\nrows\033[2J");
    CHECK(row(s, 0).empty());
    CHECK(row(s, 1).empty());
}

TEST_CASE("CSI K clears to end of line") {
    Screen s(20, 2);
    Stream st(s);
    st.feed("abcdefgh\033[5G\033[K");
    CHECK(row(s, 0) == "abcd");
}

TEST_CASE("CSI m sets attributes on subsequent cells") {
    Screen s(20, 2);
    Stream st(s);
    st.feed("\033[1;31mred\033[0mplain");
    CHECK(s.at(0, 0).attrs.bold);
    CHECK(s.at(0, 0).attrs.fg == 1);
    CHECK_FALSE(s.at(3, 0).attrs.bold);
    CHECK(s.at(3, 0).attrs.fg == pyte::kDefaultColor);
}

TEST_CASE("cursor movement sequences") {
    Screen s(20, 5);
    Stream st(s);
    st.feed("\033[3;3H");
    st.feed("\033[2A");
    CHECK(s.cursor_y() == 0);
    st.feed("\033[4C");
    CHECK(s.cursor_x() == 6);
    st.feed("\033[2D");
    CHECK(s.cursor_x() == 4);
}

TEST_CASE("a sequence split across feeds still parses") {
    Screen s(20, 5);
    Stream st(s);
    st.feed("\033[3");
    st.feed(";5H");
    st.feed("split");
    CHECK(row(s, 2) == "    split");
}

TEST_CASE("a UTF-8 code point split across feeds still decodes") {
    Screen s(20, 2);
    Stream st(s);
    const std::string ni = "\xE4\xBD\xA0";  // U+4F60
    st.feed(ni.substr(0, 1));
    st.feed(ni.substr(1));
    CHECK(s.at(0, 0).ch == U'\u4F60');
    CHECK(s.cursor_x() == 2);
}

TEST_CASE("OSC strings are swallowed, BEL-terminated") {
    Screen s(20, 2);
    Stream st(s);
    st.feed("\033]0;window title\x07visible");
    CHECK(row(s, 0) == "visible");
}

TEST_CASE("OSC strings are swallowed, ST-terminated") {
    Screen s(20, 2);
    Stream st(s);
    st.feed("\033]0;window title\033\\visible");
    CHECK(row(s, 0) == "visible");
}

TEST_CASE("ESC D and ESC M move between lines") {
    Screen s(20, 4);
    Stream st(s);
    st.feed("\033[2;1Hmid");
    st.feed("\033D");
    CHECK(s.cursor_y() == 2);
    st.feed("\033M");
    CHECK(s.cursor_y() == 1);
}

TEST_CASE("DECSTBM confines scrolling to the region") {
    Screen s(10, 4);
    Stream st(s);
    st.feed("\033[1;1Hkeep");
    st.feed("\033[2;4r");     // margins rows 2-4
    st.feed("\033[4;1Hbot");  // bottom margin
    st.feed("\n");
    CHECK(row(s, 0) == "keep");
    CHECK(row(s, 2) == "bot");
}

TEST_CASE("DECAWM off is honoured through the parser") {
    Screen s(4, 3);
    Stream st(s);
    st.feed("\033[?7l");
    CHECK_FALSE(s.autowrap());
    st.feed("abcdef");
    CHECK(s.cursor_y() == 0);
    st.feed("\033[?7h");
    CHECK(s.autowrap());
}

TEST_CASE("unknown final bytes are counted, not crashed on") {
    Screen s(20, 2);
    Stream st(s);
    st.feed("\033[99;99Zok");
    CHECK(st.unhandled_count() == 1);
    CHECK(row(s, 0) == "ok");
}

TEST_CASE("save and restore via ESC 7 / ESC 8") {
    Screen s(20, 5);
    Stream st(s);
    st.feed("\033[2;3H\0337\033[5;5Hx\0338y");
    CHECK(s.cursor_y() == 1);
    CHECK(row(s, 1) == "  y");
}

TEST_CASE("a realistic prompt redraw ends in the expected state") {
    Screen s(40, 6);
    Stream st(s);
    // Cursor home, clear, write a prompt, then a bold status line.
    st.feed("\033[H\033[2J");
    st.feed("lab-rtr01> show version\r\n");
    st.feed("\033[1mUptime: 4 days\033[0m\r\n");
    st.feed("lab-rtr01> ");
    CHECK(row(s, 0) == "lab-rtr01> show version");
    CHECK(row(s, 1) == "Uptime: 4 days");
    CHECK(s.at(0, 1).attrs.bold);
    CHECK(s.cursor_y() == 2);
    CHECK(s.cursor_x() == 11);
    CHECK(st.unhandled_count() == 0);
}

// --- device queries --------------------------------------------------------

TEST_CASE("DSR 5n reports ready") {
    Screen s(20, 5);
    Stream st(s);
    std::string replies;
    st.set_responder([&replies](const std::string &r) { replies += r; });

    st.feed("\033[5n");
    CHECK(replies == "\033[0n");
    CHECK(st.responses_sent() == 1);
    CHECK(st.unhandled_count() == 0);
}

TEST_CASE("DSR 6n reports the cursor position, 1-based") {
    Screen s(20, 10);
    Stream st(s);
    std::string replies;
    st.set_responder([&replies](const std::string &r) { replies += r; });

    st.feed("\033[4;7H\033[6n");
    CHECK(replies == "\033[4;7R");
}

TEST_CASE("DECXCPR keeps the private marker in its reply") {
    Screen s(20, 10);
    Stream st(s);
    std::string replies;
    st.set_responder([&replies](const std::string &r) { replies += r; });

    st.feed("\033[2;3H\033[?6n");
    CHECK(replies == "\033[?2;3R");
}

TEST_CASE("primary and secondary device attributes answer separately") {
    Screen s(20, 5);
    Stream st(s);
    std::string replies;
    st.set_responder([&replies](const std::string &r) { replies += r; });

    st.feed("\033[c");
    CHECK(replies == "\033[?1;2c");

    replies.clear();
    st.feed("\033[>c");
    CHECK(replies == "\033[>0;10;0c");
}

TEST_CASE("the reported identity is configurable") {
    Screen s(20, 5);
    Stream st(s);
    std::string replies;
    st.set_responder([&replies](const std::string &r) { replies += r; });
    st.set_primary_da("\033[?6c");  // announce a plain VT102

    st.feed("\033[c");
    CHECK(replies == "\033[?6c");
}

TEST_CASE("ESC Z answers like primary DA") {
    Screen s(20, 5);
    Stream st(s);
    std::string replies;
    st.set_responder([&replies](const std::string &r) { replies += r; });

    st.feed("\033Z");
    CHECK(replies == "\033[?1;2c");
    CHECK(st.unhandled_count() == 0);
}

TEST_CASE("queries are recognised even with no responder wired up") {
    Screen s(20, 5);
    Stream st(s);
    st.feed("\033[5n\033[c");
    CHECK(st.unhandled_count() == 0);  // recognised, just unanswered
    CHECK(st.responses_sent() == 0);
}

TEST_CASE("an unknown DSR code still counts as unhandled") {
    Screen s(20, 5);
    Stream st(s);
    st.feed("\033[99n");
    CHECK(st.unhandled_count() == 1);
}

TEST_CASE("a query does not disturb the screen") {
    Screen s(20, 3);
    Stream st(s);
    std::string replies;
    st.set_responder([&replies](const std::string &r) { replies += r; });

    st.feed("before\033[5nafter");
    CHECK(row(s, 0) == "beforeafter");
}

TEST_CASE("the responder survives a parser reset") {
    Screen s(20, 3);
    Stream st(s);
    std::string replies;
    st.set_responder([&replies](const std::string &r) { replies += r; });

    st.feed("\033[3");  // truncated sequence
    st.reset();
    st.feed("\033[5n");
    CHECK(replies == "\033[0n");
}
TEST_CASE("colon sub-parameters are separators, not digits") {
    pyte::Screen screen(20, 2);
    pyte::Stream stream(screen);

    SUBCASE("the colon form of direct colour") {
        // ISO 8613-6 form: an extra colour-space slot between the 2 and the
        // components. Without ':' handled as a separator the digits ran
        // together and produced one enormous parameter instead of a colour.
        stream.feed("\033[38:2::255:0:0m");
        const int fg = screen.current_attrs().fg;
        CHECK(pyte::is_rgb(fg));
        CHECK(pyte::rgb_red(fg) == 255);
        CHECK(pyte::rgb_green(fg) == 0);
        CHECK(pyte::rgb_blue(fg) == 0);
    }
    SUBCASE("the semicolon form still works") {
        stream.feed("\033[38;2;0;255;0m");
        CHECK(pyte::rgb_green(screen.current_attrs().fg) == 255);
    }
    SUBCASE("a colon form mixed with ordinary parameters") {
        stream.feed("\033[1;38:2::10:20:30;4m");
        CHECK(screen.current_attrs().bold);
        CHECK(screen.current_attrs().underline);
        CHECK(pyte::rgb_blue(screen.current_attrs().fg) == 30);
    }
    SUBCASE("truecolor followed by another attribute keeps both") {
        stream.feed("\033[38;2;9;8;7;1m");
        CHECK(pyte::rgb_red(screen.current_attrs().fg) == 9);
        CHECK(screen.current_attrs().bold);
    }
}
