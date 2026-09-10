// tools/ptdump/main.cpp
//
// Feed a byte stream through the emulator and print the resulting screen.
//
// This is the diff harness for the port: run the same capture through the
// reference implementation and through this, then compare the two dumps.
//
//   ptdump -c 132 -r 40 session.raw
//   cat session.raw | ptdump --trim
//   ptdump --history -s 500 session.raw
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "pyte/screen.h"
#include "pyte/stream.h"

namespace {

void usage() {
    std::cerr << "usage: ptdump [-c cols] [-r rows] [-s lines] [--history] [--trim] [file]\n"
              << "  -s, --scrollback  rows of history to retain (default "
              << pyte::kDefaultScrollback << ")\n"
              << "      --history     print retained history above the screen\n"
              << "      --trim        strip trailing spaces; leave off when diffing\n"
              << "  reads stdin when no file is given\n";
}

std::string rtrim(const std::string &s) {
    std::size_t end = s.find_last_not_of(' ');
    return end == std::string::npos ? std::string() : s.substr(0, end + 1);
}

}  // namespace

int main(int argc, char *argv[]) {
    int cols = 80;
    int rows = 24;
    int scrollback = pyte::kDefaultScrollback;
    bool trim = false;
    bool show_history = false;
    const char *path = nullptr;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if ((std::strcmp(arg, "-c") == 0 || std::strcmp(arg, "--cols") == 0) && i + 1 < argc) {
            cols = std::atoi(argv[++i]);
        } else if ((std::strcmp(arg, "-r") == 0 || std::strcmp(arg, "--rows") == 0) && i + 1 < argc) {
            rows = std::atoi(argv[++i]);
        } else if ((std::strcmp(arg, "-s") == 0 || std::strcmp(arg, "--scrollback") == 0) &&
                   i + 1 < argc) {
            scrollback = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--history") == 0) {
            show_history = true;
        } else if (std::strcmp(arg, "--trim") == 0) {
            trim = true;
        } else if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            usage();
            return 0;
        } else {
            path = arg;
        }
    }

    std::string data;
    if (path != nullptr) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            std::cerr << "ptdump: cannot open " << path << "\n";
            return 1;
        }
        std::ostringstream buf;
        buf << in.rdbuf();
        data = buf.str();
    } else {
        std::ostringstream buf;
        buf << std::cin.rdbuf();
        data = buf.str();
    }

    pyte::Screen screen(cols, rows, scrollback);
    pyte::Stream stream(screen);
    stream.feed(data);

    if (show_history) {
        // Oldest first, so history and screen read as one continuous session.
        for (const std::string &line : screen.history_lines()) {
            std::cout << (trim ? rtrim(line) : line) << "\n";
        }
        std::cerr << "ptdump: " << screen.history_size() << " history row(s)\n";
    }

    for (int y = 0; y < screen.rows(); ++y) {
        const std::string line = screen.line_text(y);
        std::cout << (trim ? rtrim(line) : line) << "\n";
    }

    if (stream.unhandled_count() > 0) {
        std::cerr << "ptdump: " << stream.unhandled_count()
                  << " unhandled sequence(s)\n";
    }
    return 0;
}
