// tests/test_selection.cpp
//
// Mouse selection in the widget. No pty and no shell anywhere in this file:
// bytes go in through feed(), and everything under test is what happens
// between a pointer position and a string on the clipboard.
//
// The core's own tests cover column slicing and what survives scrollback
// trimming. What is left here is the part that only exists in the widget --
// turning pixels into cells, extending a selection while the view moves
// underneath it, and the word and line shortcuts.

#include <QApplication>
#include <QClipboard>
#include <QImage>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>

#include "qtpyte/terminalwidget.h"

using qtpyte::TerminalWidget;

namespace {

// A widget with a known grid. The font is set explicitly rather than left to
// the system, because every coordinate below is in cells and the cell size
// has to be something the test can compute with.
void setUp(TerminalWidget &term) {
    QFont font(QStringLiteral("Monospace"), 10);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    term.setTerminalFont(font);
    term.resize(800, 400);
    term.show();
    QCoreApplication::processEvents();
}

// The centre of a cell, in viewport coordinates. Centres rather than corners:
// a corner lands on the boundary and which cell it belongs to is a rounding
// question the test has no business depending on.
QPoint cellCentre(const TerminalWidget &term, int col, int row) {
    return QPoint(col * term.cellWidth() + term.cellWidth() / 2,
                  row * term.cellHeight() + term.cellHeight() / 2);
}

void dragBetween(TerminalWidget &term, QPoint from, QPoint to) {
    QTest::mousePress(term.viewport(), Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(term.viewport(), to);
    QCoreApplication::processEvents();
    QTest::mouseRelease(term.viewport(), Qt::LeftButton, Qt::NoModifier, to);
    QCoreApplication::processEvents();
}

// The absolute line at the top of the viewport.
//
// Not zero. Absolute lines are numbered from the oldest row the store still
// holds, so line 0 means "the first row I wrote" only until something scrolls
// off -- after that it names a line that no longer exists, and a selection
// pinned to it comes back empty. Every hardcoded line number in a test is
// that bug waiting to happen.
int topLine(const TerminalWidget &term) { return term.viewTopLine(); }

// The absolute line whose text starts with `needle`, or -1.
//
// Searched rather than calculated, because firstLine() is not where output
// begins. The widget is constructed at 80x24 and resized to its real grid
// when shown, so the oldest rows in history are that initial blank screen --
// they scrolled off like any others. A test that treats firstLine() as "the
// first thing I wrote" selects blank rows and gets empty strings back, which
// is the correct answer to the wrong question.
int lineStartingWith(const TerminalWidget &term, const QString &needle) {
    for (int line = term.firstLine(); line < term.endLine(); ++line) {
        if (term.lineAt(line).startsWith(needle)) {
            return line;
        }
    }
    return -1;
}

// The offscreen platform plugin has no clipboard owner, so setText and text
// do not round-trip through it.
bool hasWorkingClipboard() {
    return QGuiApplication::platformName() != QStringLiteral("offscreen");
}

// Put text on the clipboard and confirm it went, returning false if it did
// not.
//
// Windows allows one clipboard owner at a time, and OpenClipboard fails
// outright when another process still holds it -- including the previous
// test in this same binary, a moment earlier. The set reports nothing; the
// text simply is not there afterwards. A test that assumes its own setup
// worked then fails on the assertion after it, blaming the code it was
// pointed at for a precondition that never happened.
bool putOnClipboard(const QString &text) {
    QGuiApplication::clipboard()->setText(text);
    return QGuiApplication::clipboard()->text() == text;
}

void feedLines(TerminalWidget &term, const QString &prefix, int count) {
    QByteArray bytes;
    for (int i = 1; i <= count; ++i) {
        bytes += (prefix + QString::number(i) + QStringLiteral("\r\n")).toUtf8();
    }
    term.feed(bytes);
    QCoreApplication::processEvents();
}

}  // namespace

class SelectionTest : public QObject {
    Q_OBJECT

private slots:
    void nothing_is_selected_to_begin_with();
    void a_drag_selects_what_it_crossed();
    void a_plain_click_clears_the_selection();
    void setSelection_spans_lines();
    void selection_survives_scrolling();
    void selection_reaches_into_scrollback();
    void firstLine_is_older_than_the_first_output();
    void an_end_column_past_the_grid_is_clamped();
    void double_click_takes_a_word();
    void double_click_takes_a_path_whole();
    void triple_click_takes_the_line();
    void selectAll_takes_everything_including_history();
    void copy_puts_the_selection_on_the_clipboard();
    void copy_does_not_happen_by_itself();
    void the_highlight_reaches_the_pixels();
    void selectionChanged_reports_both_directions();
};

// --- the basics ------------------------------------------------------------

void SelectionTest::nothing_is_selected_to_begin_with() {
    TerminalWidget term;
    setUp(term);
    term.feed(QByteArray("hello world\r\n"));

    QVERIFY(!term.hasSelection());
    QVERIFY(term.selectedText().isEmpty());
}

void SelectionTest::a_drag_selects_what_it_crossed() {
    TerminalWidget term;
    setUp(term);
    term.feed(QByteArray("hello world\r\n"));
    QCoreApplication::processEvents();

    // Row 0 holds the text; drag from column 0 to column 5.
    dragBetween(term, cellCentre(term, 0, 0), cellCentre(term, 5, 0));

    QVERIFY(term.hasSelection());
    QCOMPARE(term.selectedText(), QStringLiteral("hello"));
}

void SelectionTest::a_plain_click_clears_the_selection() {
    TerminalWidget term;
    setUp(term);
    term.feed(QByteArray("hello world\r\n"));

    const int top = topLine(term);
    term.setSelection(top, 0, top, 5);
    QVERIFY(term.hasSelection());

    const QPoint at = cellCentre(term, 3, 0);
    QTest::mousePress(term.viewport(), Qt::LeftButton, Qt::NoModifier, at);
    QTest::mouseRelease(term.viewport(), Qt::LeftButton, Qt::NoModifier, at);
    QCoreApplication::processEvents();

    QVERIFY(!term.hasSelection());
}

void SelectionTest::setSelection_spans_lines() {
    TerminalWidget term;
    setUp(term);
    term.feed(QByteArray("first line\r\nsecond line\r\nthird line\r\n"));
    QCoreApplication::processEvents();

    const int top = topLine(term);
    term.setSelection(top, 6, top + 2, 5);

    QCOMPARE(term.selectedText(),
             QStringLiteral("line\nsecond line\nthird"));
}

// --- the point of absolute coordinates -------------------------------------

void SelectionTest::selection_survives_scrolling() {
    TerminalWidget term;
    setUp(term);
    feedLines(term, QStringLiteral("lab-line-"), 200);

    // Found rather than calculated: see lineStartingWith. The end column is
    // the widget's own width rather than a guess.
    const int fourth = lineStartingWith(term, QStringLiteral("lab-line-4"));
    QVERIFY(fourth >= 0);
    term.setSelection(fourth, 0, fourth, term.columns());
    const QString before = term.selectedText();
    QCOMPARE(before, QStringLiteral("lab-line-4"));

    // Move the viewport. The text under the selection must not change --
    // this is the single behaviour the absolute coordinates exist for, and
    // the one a viewport-relative selection gets wrong.
    term.verticalScrollBar()->setValue(0);
    QCoreApplication::processEvents();
    QCOMPARE(term.selectedText(), before);

    term.verticalScrollBar()->setValue(term.verticalScrollBar()->maximum());
    QCoreApplication::processEvents();
    QCOMPARE(term.selectedText(), before);

    // And more output arriving must not move it either.
    feedLines(term, QStringLiteral("more-"), 20);
    QCOMPARE(term.selectedText(), before);
}

void SelectionTest::selection_reaches_into_scrollback() {
    TerminalWidget term;
    setUp(term);
    feedLines(term, QStringLiteral("lab-line-"), 100);

    const int first = lineStartingWith(term, QStringLiteral("lab-line-1"));
    QVERIFY(first >= 0);
    term.setSelection(first, 0, first + 2, term.columns());
    QCOMPARE(term.selectedText(),
             QStringLiteral("lab-line-1\nlab-line-2\nlab-line-3"));
}

// --- word and line ---------------------------------------------------------

void SelectionTest::firstLine_is_older_than_the_first_output() {
    TerminalWidget term;
    setUp(term);
    feedLines(term, QStringLiteral("lab-line-"), 100);

    // The widget is built at 80x24 and resized to its real grid when shown,
    // so the initial blank screen is pushed into history ahead of anything
    // written. firstLine() is the oldest row held, which is one of those --
    // not the first line of output. This is the assumption that has to be
    // wrong once for anyone writing against these coordinates, so it is
    // written down here rather than left to be rediscovered.
    const int output = lineStartingWith(term, QStringLiteral("lab-line-1"));
    QVERIFY(output >= 0);
    QVERIFY2(output > term.firstLine(),
             "expected blank rows from the initial screen ahead of the output");

    // Both are real lines on one axis; the blank ones are blank, not missing.
    QVERIFY(term.lineAt(term.firstLine()).trimmed().isEmpty());
    QCOMPARE(term.lineAt(output).trimmed(), QStringLiteral("lab-line-1"));
}

void SelectionTest::an_end_column_past_the_grid_is_clamped() {
    TerminalWidget term;
    setUp(term);
    term.feed(QByteArray("short line\r\n"));
    QCoreApplication::processEvents();

    const int top = topLine(term);

    // A host converting a pixel position at the right edge can land one past
    // the last column, and a host that just passes "the whole row" may pass
    // something larger still. Both mean the same thing: to the end of the
    // row.
    QCOMPARE(term.selectedText(), QString());
    term.setSelection(top, 0, top, term.columns());
    const QString whole = term.selectedText();
    QCOMPARE(whole, QStringLiteral("short line"));

    term.setSelection(top, 0, top, term.columns() + 40);
    QCOMPARE(term.selectedText(), whole);
}

void SelectionTest::double_click_takes_a_word() {
    TerminalWidget term;
    setUp(term);
    term.feed(QByteArray("alpha beta gamma\r\n"));
    QCoreApplication::processEvents();

    QTest::mouseDClick(term.viewport(), Qt::LeftButton, Qt::NoModifier,
                       cellCentre(term, 7, 0));
    QCoreApplication::processEvents();

    QCOMPARE(term.selectedText(), QStringLiteral("beta"));
}

void SelectionTest::double_click_takes_a_path_whole() {
    TerminalWidget term;
    setUp(term);
    term.feed(QByteArray("see /etc/nginx/nginx.conf now\r\n"));
    QCoreApplication::processEvents();

    // Slashes, dots and dashes are word characters here on purpose: in a
    // terminal the thing worth double-clicking is usually a path, a URL or
    // an address, and stopping at every separator turns one gesture into six.
    QTest::mouseDClick(term.viewport(), Qt::LeftButton, Qt::NoModifier,
                       cellCentre(term, 10, 0));
    QCoreApplication::processEvents();

    QCOMPARE(term.selectedText(), QStringLiteral("/etc/nginx/nginx.conf"));
}

void SelectionTest::triple_click_takes_the_line() {
    TerminalWidget term;
    setUp(term);
    term.feed(QByteArray("the whole line here\r\n"));
    QCoreApplication::processEvents();

    const QPoint at = cellCentre(term, 4, 0);
    QTest::mouseDClick(term.viewport(), Qt::LeftButton, Qt::NoModifier, at);
    QTest::mousePress(term.viewport(), Qt::LeftButton, Qt::NoModifier, at);
    QTest::mouseRelease(term.viewport(), Qt::LeftButton, Qt::NoModifier, at);
    QCoreApplication::processEvents();

    QCOMPARE(term.selectedText(), QStringLiteral("the whole line here"));
}

void SelectionTest::selectAll_takes_everything_including_history() {
    TerminalWidget term;
    setUp(term);
    feedLines(term, QStringLiteral("lab-line-"), 60);

    term.selectAll();
    const QString text = term.selectedText();

    QVERIFY(text.contains(QStringLiteral("lab-line-1\n")));
    QVERIFY(text.contains(QStringLiteral("lab-line-60")));
}

// --- the clipboard ---------------------------------------------------------

void SelectionTest::copy_puts_the_selection_on_the_clipboard() {
    TerminalWidget term;
    setUp(term);
    term.feed(QByteArray("copy this text\r\n"));
    QCoreApplication::processEvents();

    const int top = topLine(term);
    term.setSelection(top, 0, top, 9);

    // The text is right whatever the platform can do with it.
    QCOMPARE(term.selectedText(), QStringLiteral("copy this"));

    if (!hasWorkingClipboard()) {
        QSKIP("the offscreen platform has no clipboard owner");
    }

    if (!putOnClipboard(QStringLiteral("previous contents"))) {
        QSKIP("could not take ownership of the clipboard");
    }

    term.copySelection();
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("copy this"));
}

void SelectionTest::copy_does_not_happen_by_itself() {
    TerminalWidget term;
    setUp(term);
    term.feed(QByteArray("hello world\r\n"));
    QCoreApplication::processEvents();

    if (!hasWorkingClipboard()) {
        QSKIP("the offscreen platform has no clipboard owner");
    }

    const QString untouched = QStringLiteral("something the user was keeping");
    if (!putOnClipboard(untouched)) {
        QSKIP("could not take ownership of the clipboard");
    }

    dragBetween(term, cellCentre(term, 0, 0), cellCentre(term, 5, 0));
    QVERIFY(term.hasSelection());

    // Selecting is not copying. A terminal that rewrites the clipboard on
    // every drag is a terminal that loses whatever was there.
    QCOMPARE(QGuiApplication::clipboard()->text(), untouched);
}

// --- painting --------------------------------------------------------------

void SelectionTest::the_highlight_reaches_the_pixels() {
    TerminalWidget term;
    setUp(term);
    term.feed(QByteArray("highlight me\r\n"));
    QCoreApplication::processEvents();

    const QColor mark(255, 0, 128, 255);
    term.setSelectionColor(mark);

    const int top = topLine(term);
    term.setSelection(top, 0, top, 9);

    // update() schedules a repaint rather than performing one, and grab()
    // will happily photograph the frame before it.
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(term.viewport(), QEvent::UpdateRequest);
    QCoreApplication::processEvents();

    const QImage shot = term.viewport()->grab().toImage();
    bool found = false;
    for (int y = 0; y < shot.height() && !found; ++y) {
        for (int x = 0; x < shot.width(); ++x) {
            const QColor c = shot.pixelColor(x, y);
            if (c.red() > 200 && c.green() < 60 && c.blue() > 90 &&
                c.blue() < 170) {
                found = true;
                break;
            }
        }
    }
    QVERIFY2(found, "the selection highlight never reached the pixels");
}

void SelectionTest::selectionChanged_reports_both_directions() {
    TerminalWidget term;
    setUp(term);
    term.feed(QByteArray("hello world\r\n"));
    QCoreApplication::processEvents();

    QSignalSpy spy(&term, &TerminalWidget::selectionChanged);

    const int top = topLine(term);
    term.setSelection(top, 0, top, 5);
    QVERIFY(spy.count() > 0);
    QCOMPARE(spy.last().at(0).toBool(), true);

    term.clearSelection();
    QCOMPARE(spy.last().at(0).toBool(), false);
}

QTEST_MAIN(SelectionTest)
#include "test_selection.moc"