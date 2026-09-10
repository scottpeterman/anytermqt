// src/selection.cpp
//
// Mouse selection, in its own translation unit rather than in
// terminalwidget.cpp, which is already the largest file here and is about
// turning cells into glyphs. Selection is about turning pixels back into
// coordinates, which is the opposite direction and shares almost nothing.
//
// The one rule everything else follows from: a selection is held in absolute
// line coordinates and converted to viewport rows only at paint time. Hold it
// in viewport coordinates instead and the highlight crawls up the screen
// whenever output arrives, while the user is holding perfectly still.

#include "qtpyte/terminalwidget.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>

#include <algorithm>

namespace qtpyte {
namespace {

// How long a pointer may rest past the edge before the next scroll step.
constexpr int kAutoScrollIntervalMs = 60;

// A word, for the purposes of double-click. Wider than isalnum on purpose:
// in a terminal the thing worth selecting is usually a path, a URL, an
// address or an identifier, and stopping at every dot or slash turns one
// double-click into six.
bool isWordChar(char32_t ch) {
    if (ch < 128) {
        const char c = static_cast<char>(ch);
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
               (c >= 'a' && c <= 'z') || c == '_' || c == '-' || c == '.' ||
               c == '/' || c == ':' || c == '~' || c == '@' || c == '+' ||
               c == '=' || c == '%' || c == '#';
    }
    // Anything non-ASCII is treated as part of a word: the alternative is a
    // character-class table for every script, and being wrong in this
    // direction only means selecting slightly too much.
    return ch != U' ';
}

}  // namespace

// --- coordinates -----------------------------------------------------------

void TerminalWidget::pointToCell(const QPoint &pos, int &line, int &col) const {
    const int row = std::clamp(pos.y() / cell_h_, 0, rows_ - 1);

    // Columns clamp to cols_, not cols_ - 1. The end of a selection is
    // exclusive, so "past the last character" has to be expressible -- it is
    // what dragging off the right edge means, and without it the last
    // character on a row can never be included.
    col = std::clamp(pos.x() / cell_w_, 0, cols_);

    line = screen_.view_top() + row;
}

// --- state -----------------------------------------------------------------

int TerminalWidget::viewTopLine() const { return screen_.view_top(); }

int TerminalWidget::firstLine() const { return screen_.first_line(); }

int TerminalWidget::endLine() const { return screen_.end_line(); }

QString TerminalWidget::lineAt(int line) const {
    return QString::fromStdString(screen_.line_at(line));
}

bool TerminalWidget::hasSelection() const {
    return selection_.active && !selection_.isEmpty();
}

QString TerminalWidget::selectedText() const {
    if (!hasSelection()) {
        return QString();
    }
    int startLine = 0;
    int startCol = 0;
    int endLine = 0;
    int endCol = 0;
    selection_.normalized(startLine, startCol, endLine, endCol);

    const std::string text =
        screen_.text_in_range(startLine, startCol, endLine, endCol);
    return QString::fromStdString(text);
}

void TerminalWidget::setSelection(int startLine, int startCol, int endLine,
                                  int endCol) {
    selection_.anchorLine = startLine;
    selection_.anchorCol = startCol;
    selection_.focusLine = endLine;
    selection_.focusCol = endCol;
    selection_.active = true;
    selection_.dragging = false;
    notifySelectionChanged();
    viewport()->update();
}

void TerminalWidget::selectAll() {
    setSelection(firstLine(), 0, endLine() - 1, cols_);
}

void TerminalWidget::clearSelection() {
    if (!selection_.active) {
        return;
    }
    selection_.clear();
    notifySelectionChanged();
    viewport()->update();
}

void TerminalWidget::copySelection() {
    const QString text = selectedText();
    if (text.isEmpty()) {
        return;
    }
    QGuiApplication::clipboard()->setText(text);
}

void TerminalWidget::setSelectionColor(const QColor &color) {
    selection_color_ = color;
    if (hasSelection()) {
        viewport()->update();
    }
}

void TerminalWidget::notifySelectionChanged() {
    Q_EMIT selectionChanged(hasSelection());
}

bool TerminalWidget::mouseReportingActive() const {
    // See the header. The core does not parse DECSET 1000/1002/1006 yet, so
    // nothing on the far end can be asking for the mouse.
    return false;
}

// --- painting --------------------------------------------------------------

void TerminalWidget::paintSelection(QPainter &painter, int first, int last) {
    if (!hasSelection()) {
        return;
    }

    const int top = screen_.view_top();
    for (int y = first; y <= last; ++y) {
        int from = 0;
        int to = 0;
        if (!selection_.spanOnLine(top + y, cols_, from, to)) {
            continue;
        }
        from = std::clamp(from, 0, cols_);
        to = std::clamp(to, 0, cols_);
        if (to <= from) {
            continue;
        }
        // Over the glyphs rather than under them, with alpha. Painting the
        // highlight as a background instead would mean re-deciding every
        // run's colours, and would lose whatever the application chose --
        // which for something like btop is most of the information on screen.
        painter.fillRect(cellRect(from, y, to - from), selection_color_);
    }
}

// --- word and line ---------------------------------------------------------

void TerminalWidget::selectWordAt(int line, int col) {
    const int row = line - screen_.view_top();
    if (row < 0 || row >= rows_ || col < 0 || col >= cols_) {
        return;
    }

    // Cells rather than the row's text: at() indexes by grid column, which is
    // what a click gives, and a double-width glyph would put the two out of
    // step anywhere text is indexed instead.
    const auto charAt = [this, row](int x) {
        const pyte::Cell &cell = screen_.at(x, row);
        return cell.wide_continuation ? U' ' : cell.ch;
    };

    if (!isWordChar(charAt(col))) {
        setSelection(line, col, line, col + 1);
        return;
    }

    int start = col;
    while (start > 0 && isWordChar(charAt(start - 1))) {
        --start;
    }
    int end = col;
    while (end + 1 < cols_ && isWordChar(charAt(end + 1))) {
        ++end;
    }
    setSelection(line, start, line, end + 1);
}

void TerminalWidget::selectLineAt(int line) {
    setSelection(line, 0, line, cols_);
}

// --- auto-scroll -----------------------------------------------------------

void TerminalWidget::updateAutoScroll(const QPoint &pos) {
    const int height = viewport()->height();
    const int direction = (pos.y() < 0) ? -1 : (pos.y() > height ? 1 : 0);

    if (direction == 0) {
        stopAutoScroll();
        return;
    }

    autoscroll_dir_ = direction;
    if (autoscroll_ == nullptr) {
        // Created on first use and parented, so a widget that is never
        // dragged past its edge never allocates one.
        autoscroll_ = new QTimer(this);
        connect(autoscroll_, &QTimer::timeout, this, [this]() {
            QScrollBar *bar = verticalScrollBar();
            const int next = bar->value() + autoscroll_dir_;
            if (next < bar->minimum() || next > bar->maximum()) {
                stopAutoScroll();
                return;
            }
            bar->setValue(next);

            // Extend the focus to the line just revealed. Without this the
            // selection stops growing the moment the pointer leaves the
            // widget, which looks like the drag was dropped.
            QPoint edge = last_drag_pos_;
            edge.setY(autoscroll_dir_ < 0 ? 0 : viewport()->height() - 1);
            extendFocusTo(edge);
        });
    }
    if (!autoscroll_->isActive()) {
        autoscroll_->start(kAutoScrollIntervalMs);
    }
}

void TerminalWidget::stopAutoScroll() {
    autoscroll_dir_ = 0;
    if (autoscroll_ != nullptr) {
        autoscroll_->stop();
    }
}

void TerminalWidget::extendFocusTo(const QPoint &pos) {
    int line = 0;
    int col = 0;
    pointToCell(pos, line, col);
    selection_.focusLine = line;
    selection_.focusCol = col;
    selection_.active = true;
    viewport()->update();
}

// --- events ----------------------------------------------------------------

void TerminalWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }

    // Shift is the override: when the far end is reading the mouse, an
    // unmodified drag belongs to it and a shifted one belongs to the user.
    const bool forceSelect = event->modifiers().testFlag(Qt::ShiftModifier);
    if (mouseReportingActive() && !forceSelect) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }

    const QPoint pos = event->position().toPoint();

    // Click runs. Qt raises a double click as its own event but has nothing
    // for a triple, so the run is counted here against the platform's own
    // interval.
    if (click_timer_.isValid() &&
        click_timer_.elapsed() < QApplication::doubleClickInterval()) {
        ++click_count_;
    } else {
        click_count_ = 1;
    }
    click_timer_.restart();

    int line = 0;
    int col = 0;
    pointToCell(pos, line, col);

    if (click_count_ >= 3) {
        selectLineAt(line);
        selection_.dragging = false;
        return;
    }

    if (forceSelect && selection_.active) {
        // Shift-click extends an existing selection rather than starting a
        // new one, which is what every other text surface does.
        extendFocusTo(pos);
        selection_.dragging = true;
        return;
    }

    selection_.clear();
    selection_.anchorLine = line;
    selection_.anchorCol = col;
    selection_.focusLine = line;
    selection_.focusCol = col;
    selection_.dragging = true;
    last_drag_pos_ = pos;

    notifySelectionChanged();
    viewport()->update();
}

void TerminalWidget::mouseMoveEvent(QMouseEvent *event) {
    if (!selection_.dragging) {
        QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }

    const QPoint pos = event->position().toPoint();
    last_drag_pos_ = pos;
    extendFocusTo(pos);
    updateAutoScroll(pos);
    notifySelectionChanged();
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::MiddleButton) {
        // X11's primary selection, which is not the clipboard and is pasted
        // by middle click by convention. Elsewhere the buffer does not exist
        // and this does nothing, which is the correct amount of nothing.
        QClipboard *clipboard = QGuiApplication::clipboard();
        if (clipboard->supportsSelection()) {
            paste(clipboard->text(QClipboard::Selection));
        }
        return;
    }

    if (event->button() != Qt::LeftButton || !selection_.dragging) {
        QAbstractScrollArea::mouseReleaseEvent(event);
        return;
    }

    selection_.dragging = false;
    stopAutoScroll();

    // A click that never moved is not a selection; it is how someone dismisses
    // the last one.
    if (selection_.isEmpty()) {
        clearSelection();
        return;
    }

    selection_.active = true;

    // On X11 the primary selection is expected to follow the highlight. That
    // is the platform's own convention and costs the user nothing, unlike the
    // clipboard, which stays untouched until copySelection() is called.
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard->supportsSelection()) {
        clipboard->setText(selectedText(), QClipboard::Selection);
    }

    notifySelectionChanged();
}

void TerminalWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mouseDoubleClickEvent(event);
        return;
    }
    if (mouseReportingActive() &&
        !event->modifiers().testFlag(Qt::ShiftModifier)) {
        QAbstractScrollArea::mouseDoubleClickEvent(event);
        return;
    }

    click_count_ = 2;
    click_timer_.restart();

    int line = 0;
    int col = 0;
    pointToCell(event->position().toPoint(), line, col);
    selectWordAt(line, std::min(col, cols_ - 1));
    selection_.dragging = false;
}

// --- keys ------------------------------------------------------------------

bool TerminalWidget::handleSelectionKey(QKeyEvent *event) {
    const bool ctrlShift =
        event->modifiers().testFlag(Qt::ControlModifier) &&
        event->modifiers().testFlag(Qt::ShiftModifier);

    if (!ctrlShift) {
        return false;
    }

    // Ctrl+Shift rather than Ctrl: Ctrl+C is SIGINT and Ctrl+V is a literal
    // control character, and a terminal that swallows either to serve the
    // clipboard has broken the thing it exists to do.
    switch (event->key()) {
    case Qt::Key_C:
        if (hasSelection()) {
            copySelection();
            return true;
        }
        return false;
    case Qt::Key_V:
        paste(QGuiApplication::clipboard()->text());
        return true;
    case Qt::Key_A:
        selectAll();
        return true;
    default:
        return false;
    }
}

}  // namespace qtpyte