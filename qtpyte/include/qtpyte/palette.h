// include/qtpyte/palette.h
#pragma once

#include <QColor>

namespace qtpyte {

// Maps the colour indices pyte::Attrs carries onto actual colours.
//
// pyte::kDefaultColor means "whatever the terminal's default is", which only
// the widget knows, so it is resolved here against a supplied default rather
// than baked into the table.
class Palette {
public:
    Palette();

    // index is a pyte::Attrs fg/bg value: 0-15 for the ANSI colours, 16-255
    // for the xterm cube and greyscale ramp, a packed 24-bit colour, or
    // pyte::kDefaultColor.
    // `bold` brightens indices 0-7, which is what terminals have always done
    // with bold and what applications still assume.
    QColor color(int index, const QColor &fallback, bool bold = false) const;

    QColor foreground() const { return foreground_; }
    QColor background() const { return background_; }
    QColor cursor() const { return cursor_; }
    void set_foreground(const QColor &c) { foreground_ = c; }
    void set_background(const QColor &c) { background_ = c; }
    void set_cursor(const QColor &c) { cursor_ = c; }

    // Replace one of the 16 ANSI entries; the 216-colour cube and the
    // greyscale ramp above them are defined by xterm and not adjustable.
    void set_ansi(int index, const QColor &c);

private:
    QColor ansi_[16];
    QColor foreground_;
    QColor background_;
    QColor cursor_;
};

}  // namespace qtpyte
