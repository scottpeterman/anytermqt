// src/palette.cpp
#include "qtpyte/palette.h"

#include "pyte/cell.h"

namespace qtpyte {
namespace {

// The 16 ANSI entries, in pyte's index order: 0-7 normal, 8-15 bright.
// These are the xterm defaults rather than a designed scheme -- a scheme is a
// caller's decision, and set_ansi() is how it makes one.
constexpr const char *kAnsiDefaults[16] = {
    "#000000", "#cd0000", "#00cd00", "#cdcd00",
    "#0000ee", "#cd00cd", "#00cdcd", "#e5e5e5",
    "#7f7f7f", "#ff0000", "#00ff00", "#ffff00",
    "#5c5cff", "#ff00ff", "#00ffff", "#ffffff",
};

// xterm's 6x6x6 cube uses these levels, not an even 0-255 split.
constexpr int kCubeLevels[6] = {0, 95, 135, 175, 215, 255};

}  // namespace

Palette::Palette()
    : foreground_(QColor("#d0d0d0")),
      background_(QColor("#101010")),
      cursor_(QColor("#d0d0d0")) {
    for (int i = 0; i < 16; ++i) {
        ansi_[i] = QColor(kAnsiDefaults[i]);
    }
}

void Palette::set_ansi(int index, const QColor &c) {
    if (index >= 0 && index < 16) {
        ansi_[index] = c;
    }
}

QColor Palette::color(int index, const QColor &fallback, bool bold) const {
    if (index == pyte::kDefaultColor) {
        return fallback;
    }
    // A packed 24-bit colour is used as sent -- there is no palette entry to
    // look up and no brightening to apply, because the application already
    // said exactly what it wanted.
    if (pyte::is_rgb(index)) {
        return QColor(pyte::rgb_red(index), pyte::rgb_green(index),
                      pyte::rgb_blue(index));
    }
    if (index >= 0 && index < 8) {
        return ansi_[bold ? index + 8 : index];
    }
    if (index >= 8 && index < 16) {
        return ansi_[index];
    }
    if (index >= 16 && index < 232) {
        const int n = index - 16;
        return QColor(kCubeLevels[(n / 36) % 6],
                      kCubeLevels[(n / 6) % 6],
                      kCubeLevels[n % 6]);
    }
    if (index >= 232 && index < 256) {
        const int level = 8 + (index - 232) * 10;
        return QColor(level, level, level);
    }
    return fallback;
}

}  // namespace qtpyte
