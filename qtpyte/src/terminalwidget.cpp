// src/terminalwidget.cpp
#include "qtpyte/terminalwidget.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QtMath>

#include <algorithm>
#include <vector>

namespace qtpyte {
namespace {

// DEC private mode numbers this widget acts on. Everything else is observed
// and ignored; the emulator reports all of them either way.
constexpr int kModeCursorKeys = 1;      // DECCKM
constexpr int kModeCursorVisible = 25;  // DECTCEM
constexpr int kModeBracketedPaste = 2004;

}  // namespace

TerminalWidget::TerminalWidget(QWidget *parent)
    : QAbstractScrollArea(parent),
      screen_(80, 24, 5000),
      stream_(screen_) {
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setAutoFillBackground(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFrameStyle(QFrame::NoFrame);

    // Replies to Device Status Report and friends go back out the same way
    // keystrokes do. Without this the queries are still parsed, they just go
    // unanswered -- and some applications sit waiting for the answer.
    stream_.set_responder([this](const std::string &reply) {
        Q_EMIT dataReady(QByteArray(reply.data(), static_cast<int>(reply.size())));
    });
    stream_.set_mode_handler([this](int mode, bool set, bool private_mode) {
        handleMode(mode, set, private_mode);
    });

    setTerminalFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
}

TerminalWidget::~TerminalWidget() = default;

// --- font and geometry ------------------------------------------------------

void TerminalWidget::setTerminalFont(const QFont &font) {
    font_ = font;
    font_.setFixedPitch(true);
    font_.setStyleHint(QFont::Monospace);
    applyFontMetrics();
    updateGeometryForSize();
    viewport()->update();
}

void TerminalWidget::applyFontMetrics() {
    // Measured before letter spacing is applied, or the correction would be
    // computed against a font that already carries the previous one.
    font_.setLetterSpacing(QFont::PercentageSpacing, 100.0);

    const QFontMetricsF fm(font_);
    const qreal advance = fm.horizontalAdvance(QLatin1Char('M'));
    cell_w_ = std::max(1, qRound(advance));
    cell_h_ = std::max(1, qRound(fm.lineSpacing()));
    baseline_ = qRound(fm.ascent());

    // A monospace font's natural advance is almost never a whole number of
    // pixels -- at 12pt a common one is 9.625 -- and the grid has to be
    // integral or glyphs and their background rectangles disagree. Drawing a
    // whole run in one call and letting Qt use the natural advance therefore
    // walks the text off the grid: 80 columns of a 9.625px font land 30px
    // (three cells) from where the grid says they should, and the run
    // overflows into the next run's cells.
    //
    // Absolute letter spacing pins every glyph to the cell width, so a run
    // can still be drawn in one call and still land on the grid.
    const qreal correction = static_cast<qreal>(cell_w_) - advance;
    font_.setLetterSpacing(QFont::AbsoluteSpacing, correction);

    // Letter spacing only fixes the drift if every glyph starts from the same
    // advance. Check a few shapes that differ most in a proportional face; if
    // they disagree, fall back to positioning each cell individually.
    const QFontMetricsF spaced(font_);
    const qreal target = spaced.horizontalAdvance(QLatin1Char('M'));
    uniform_advance_ = true;
    for (QChar probe : {QLatin1Char('i'), QLatin1Char('W'), QLatin1Char(' '),
                        QLatin1Char('.')}) {
        if (qAbs(spaced.horizontalAdvance(probe) - target) > 0.01) {
            uniform_advance_ = false;
            break;
        }
    }
}

void TerminalWidget::updateGeometryForSize() {
    const int cols = std::max(1, viewport()->width() / cell_w_);
    const int rows = std::max(1, viewport()->height() / cell_h_);
    if (cols == cols_ && rows == rows_) {
        return;
    }
    cols_ = cols;
    rows_ = rows;
    screen_.resize(cols_, rows_);
    // Both halves, in this order: the emulator needs the new shape to lay out
    // what it already holds, and whatever is producing the bytes needs it to
    // redraw. An application that does not repaint after a window resize is
    // usually missing the second half, not the first.
    syncScrollBar();
    Q_EMIT resized(cols_, rows_);
}

void TerminalWidget::resizeEvent(QResizeEvent *event) {
    QAbstractScrollArea::resizeEvent(event);
    updateGeometryForSize();
}

void TerminalWidget::changeEvent(QEvent *event) {
    QAbstractScrollArea::changeEvent(event);
    if (event->type() == QEvent::FontChange) {
        applyFontMetrics();
        updateGeometryForSize();
    }
}

void TerminalWidget::refresh() {
    viewport()->update();
}

// --- bytes ------------------------------------------------------------------

void TerminalWidget::feed(const QByteArray &data) {
    if (data.isEmpty()) {
        return;
    }
    stream_.feed(data.constData(), static_cast<std::size_t>(data.size()));
    syncScrollBar();
    viewport()->update();
}

void TerminalWidget::send(const QString &text) {
    const std::string bytes = encode_text(text, false);
    Q_EMIT dataReady(QByteArray(bytes.data(), static_cast<int>(bytes.size())));
}

void TerminalWidget::paste(const QString &text) {
    const std::string bytes = encode_text(text, bracketed_paste_);
    Q_EMIT dataReady(QByteArray(bytes.data(), static_cast<int>(bytes.size())));
}

void TerminalWidget::handleMode(int mode, bool set, bool private_mode) {
    if (!private_mode) {
        return;
    }
    switch (mode) {
        case kModeCursorKeys:
            key_modes_.application_cursor = set;
            break;
        case kModeCursorVisible:
            cursor_visible_ = set;
            viewport()->update();
            break;
        case kModeBracketedPaste:
            bracketed_paste_ = set;
            break;
        default:
            break;  // observed, not acted on
    }
}

// --- input ------------------------------------------------------------------

void TerminalWidget::keyPressEvent(QKeyEvent *event) {
    // Scrollback navigation is the widget's, not the child's -- but only when
    // the child is not a full-screen application, which wants Shift-PageUp
    // for itself. Requiring Shift keeps the plain keys available to it.
    if (event->modifiers() & Qt::ShiftModifier) {
        switch (event->key()) {
            case Qt::Key_PageUp:
                screen_.scroll_history_up(rows_ / 2);
                syncScrollBar();
                viewport()->update();
                return;
            case Qt::Key_PageDown:
                screen_.scroll_history_down(rows_ / 2);
                syncScrollBar();
                viewport()->update();
                return;
            default:
                break;
        }
    }

    const std::string bytes = encode_key(event, key_modes_);
    if (bytes.empty()) {
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }
        if (handleSelectionKey(event)) {
        return;
    }

    // Typing returns to the live screen, matching what the emulator does on
    // output: a keystroke you cannot see the result of is worse than losing
    // your place in the scrollback.
    if (screen_.viewing_history()) {
        screen_.scroll_history_to_bottom();
        syncScrollBar();
    }

    Q_EMIT dataReady(QByteArray(bytes.data(), static_cast<int>(bytes.size())));
    event->accept();
    viewport()->update();
}

void TerminalWidget::focusInEvent(QFocusEvent *event) {
    QAbstractScrollArea::focusInEvent(event);
    viewport()->update();
}

void TerminalWidget::focusOutEvent(QFocusEvent *event) {
    QAbstractScrollArea::focusOutEvent(event);
    viewport()->update();
}

// --- scrollback -------------------------------------------------------------

void TerminalWidget::syncScrollBar() {
    QScrollBar *bar = verticalScrollBar();
    const int history = screen_.history_size();
    // Blocked: setValue would otherwise re-enter scrollContentsBy and set the
    // history position we are in the middle of reading.
    const QSignalBlocker blocker(bar);
    bar->setRange(0, history);
    bar->setPageStep(std::max(1, rows_));
    bar->setSingleStep(1);
    // The bar counts down from the top of history; the emulator counts up
    // from the live screen. Bottom is live, which is where a terminal sits.
    bar->setValue(history - screen_.history_position());
}

void TerminalWidget::scrollContentsBy(int dx, int dy) {
    Q_UNUSED(dx);
    Q_UNUSED(dy);
    const QScrollBar *bar = verticalScrollBar();
    screen_.scroll_history_to(bar->maximum() - bar->value());
    viewport()->update();
}

// --- painting ---------------------------------------------------------------

QRect TerminalWidget::cellRect(int x, int y, int span) const {
    return QRect(x * cell_w_, y * cell_h_, cell_w_ * span, cell_h_);
}

void TerminalWidget::paintEvent(QPaintEvent *event) {
    QPainter painter(viewport());
    painter.fillRect(event->rect(), palette_.background());
    painter.setFont(font_);

    const int first = std::max(0, event->rect().top() / cell_h_);
    const int last = std::min(rows_ - 1, event->rect().bottom() / cell_h_);
    for (int y = first; y <= last; ++y) {
        paintRow(painter, y);
    }
    paintSelection(painter, first, last);
    paintCursor(painter);

    screen_.clear_dirty();
}

void TerminalWidget::paintRow(QPainter &painter, int y) {
    int x = 0;
    while (x < cols_) {
        const pyte::Cell &first = screen_.at(x, y);
        if (first.wide_continuation) {
            ++x;  // painted by its left half
            continue;
        }

        // Gather a run of cells sharing attributes and draw it in one call.
        // Per-cell drawText is correct and unusably slow on a full screen --
        // a redraw is thousands of calls, and it shows on scrolling output.
        const pyte::Attrs attrs = first.attrs;
        QString text;
        const int run_start = x;
        int span = 0;
        while (x < cols_) {
            const pyte::Cell &cell = screen_.at(x, y);
            if (cell.wide_continuation) {
                ++x;
                ++span;
                continue;
            }
            if (cell.attrs != attrs && !text.isEmpty()) {
                break;
            }
            // QString::fromUcs4 rather than QChar::fromUcs4: the latter
            // returns a proxy in Qt 6 and cannot represent a code point
            // outside the BMP as one QChar anyway.
            const char32_t cp = (cell.ch == U'\0') ? U' ' : cell.ch;
            text += QString::fromUcs4(&cp, 1);
            ++x;
            ++span;
        }

        QColor fg = palette_.color(attrs.fg, palette_.foreground(), attrs.bold);
        QColor bg = palette_.color(attrs.bg, palette_.background());
        if (attrs.reverse) {
            std::swap(fg, bg);
        }

        const QRect rect = cellRect(run_start, y, span);
        if (bg != palette_.background()) {
            painter.fillRect(rect, bg);
        }

        if (!text.trimmed().isEmpty() || attrs.underline || attrs.strikethrough) {
            QFont font = font_;
            font.setBold(attrs.bold);
            font.setItalic(attrs.italic);
            font.setUnderline(attrs.underline);
            font.setStrikeOut(attrs.strikethrough);
            painter.setFont(font);
            painter.setPen(fg);
            drawRun(painter, text, run_start, y, span);
        }
    }
}

// The letter-spacing correction is derived from the PRIMARY font's advance,
// so it only holds for glyphs the primary font actually has. Box-drawing,
// braille (which is what btop's graphs are made of), and anything else that
// falls through to a substitute font advances by the substitute's metrics and
// drifts within the run. Those runs are positioned per cell instead.
//
// Restricting the fast path to printable ASCII is a coarse test, but it is
// the one that costs nothing to make: it is a range check per character, and
// the overwhelming majority of terminal output passes it.
bool TerminalWidget::isPlainAscii(const QString &text) {
    for (const QChar ch : text) {
        const ushort code = ch.unicode();
        if (code < 0x20 || code > 0x7E) {
            return false;
        }
    }
    return true;
}

void TerminalWidget::drawRun(QPainter &painter, const QString &text, int x,
                             int y, int span) {
    const int baseline = y * cell_h_ + baseline_;

    if (uniform_advance_ && span == text.size() && isPlainAscii(text)) {
        // The common case: narrow cells the primary font certainly has, and
        // letter spacing has already pinned each glyph to the grid. One call
        // for the whole run.
        painter.drawText(x * cell_w_, baseline, text);
        return;
    }

    // Either the font is not uniform, or the run contains a double-width
    // glyph -- letter spacing adds its correction once per glyph, so a glyph
    // occupying two cells comes up one correction short and everything after
    // it in the run shifts. Position each cell instead.
    int column = x;
    for (int i = 0; i < text.size(); ++i) {
        const QString glyph = text.mid(i, 1);
        painter.drawText(column * cell_w_, baseline, glyph);
        column += (QFontMetricsF(painter.font()).horizontalAdvance(glyph) >
                   cell_w_ * 1.5)
                      ? 2
                      : 1;
    }
}

void TerminalWidget::paintCursor(QPainter &painter) {
    // No cursor while scrolled back: it marks where the next character lands,
    // and while looking at history that is not on screen.
    if (!cursor_visible_ || screen_.viewing_history()) {
        return;
    }
    const int cx = screen_.cursor_x();
    const int cy = screen_.cursor_y();
    if (cx < 0 || cx >= cols_ || cy < 0 || cy >= rows_) {
        return;
    }

    const pyte::Cell &cell = screen_.at(cx, cy);
    const int span = (cx + 1 < cols_ && screen_.at(cx + 1, cy).wide_continuation) ? 2 : 1;
    const QRect rect = cellRect(cx, cy, span);

    if (!hasFocus()) {
        // Unfocused terminals draw an outline. It is the cheapest way to show
        // that keystrokes are going somewhere else.
        painter.setPen(palette_.cursor());
        painter.drawRect(rect.adjusted(0, 0, -1, -1));
        return;
    }

    painter.fillRect(rect, palette_.cursor());
    if (cell.ch != U'\0' && cell.ch != U' ') {
        painter.setPen(palette_.background());
        painter.setFont(font_);
        const char32_t cp = cell.ch;
        painter.drawText(rect.left(), rect.top() + baseline_,
                         QString::fromUcs4(&cp, 1));
    }
}

// --- text out ---------------------------------------------------------------

QString TerminalWidget::screenText() const {
    QStringList lines;
    for (int y = 0; y < rows_; ++y) {
        lines << QString::fromStdString(screen_.line_text(y));
    }
    return lines.join(QLatin1Char('\n'));
}

QString TerminalWidget::sessionText() const {
    QStringList lines;
    for (const std::string &line : screen_.history_lines()) {
        lines << QString::fromStdString(line);
    }
    for (int y = 0; y < rows_; ++y) {
        lines << QString::fromStdString(screen_.line_text(y));
    }
    return lines.join(QLatin1Char('\n'));
}

}  // namespace qtpyte
