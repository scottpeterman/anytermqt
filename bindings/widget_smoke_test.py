# bindings/widget_smoke_test.py
#
# Phase-1 check: TerminalWidget across the boundary. A value-type round trip
# proved the toolchain; this proves the three things a widget binding does
# that a value binding does not.
#
#   1. The widget can be put in a Python-owned layout, which transfers
#      ownership to the C++ parent.
#   2. Its signals are real PySide signals, so the moc metaobject made it
#      into the extension module.
#   3. It survives interpreter shutdown. A wrong ownership rule does not
#      fail at addWidget -- it fails on the way out, after everything has
#      apparently worked.
#
# Point three is why this runs to completion and checks the exit status
# rather than stopping at a visible window. Run it headless:
#
#   QT_QPA_PLATFORM=offscreen PYTHONPATH=build/bindings \
#       python bindings/widget_smoke_test.py
#
# Drop QT_QPA_PLATFORM to watch it, and pass --show to keep the window up
# until it is closed.

import sys

from PySide6.QtCore import QByteArray, QTimer
from PySide6.QtGui import QColor, QFont
from PySide6.QtWidgets import QApplication, QVBoxLayout, QWidget

import anytermqt


def main() -> int:
    app = QApplication(sys.argv)

    window = QWidget()
    window.setWindowTitle("anytermqt smoke test")
    layout = QVBoxLayout(window)

    terminal = anytermqt.TerminalWidget()
    terminal.setTerminalFont(QFont("Monospace", 11))

    # Ownership crosses here. After this the C++ parent owns the widget and
    # Python must not delete it; if the binding disagrees, the process
    # crashes at shutdown rather than now.
    layout.addWidget(terminal)

    # Palette by value in and out. The reference-returning palette() is not
    # bound -- see the typesystem for why -- so this is the supported route.
    palette = terminal.terminalPalette()
    palette.set_background(QColor("#101010"))
    palette.set_foreground(QColor("#d0d0d0"))
    palette.set_ansi(1, QColor("#ff5555"))
    terminal.setTerminalPalette(palette)

    assert terminal.terminalPalette().background().name() == "#101010", \
        "palette did not survive the round trip"

    # Signals. These only exist if the moc metaobject for TerminalWidget was
    # compiled into the extension module; if it was not, the attribute lookup
    # fails here rather than at connect time.
    sent: list[bytes] = []
    terminal.dataReady.connect(lambda data: sent.append(bytes(data)))

    resizes: list[tuple[int, int]] = []
    terminal.resized.connect(lambda cols, rows: resizes.append((cols, rows)))

    # Bytes in. Escape sequences are the core's job, not the widget's, so a
    # coloured string exercises both halves in one call.
    terminal.feed(QByteArray(b"\x1b[31mhello\x1b[0m from c++\r\n"))
    assert "hello from c++" in terminal.screenText(), \
        "fed bytes did not reach the screen"

    # Bytes out. send() encodes and emits synchronously, so the signal has
    # already fired by the next line -- no event loop needed for this part.
    terminal.send("ls -la\r")
    assert sent, "dataReady never fired"
    assert b"ls -la\r" in b"".join(sent), f"unexpected output: {sent!r}"

    print("columns x rows:", terminal.columns(), "x", terminal.terminalRows())
    print("cell:          ", terminal.cellWidth(), "x", terminal.cellHeight())
    print("scrollback:    ", terminal.scrollbackSize())
    print("sent:          ", b"".join(sent))

    window.resize(800, 480)
    window.show()

    if "--show" in sys.argv:
        return app.exec()

    # Let the event loop turn once so the resize lands and a paint happens
    # under whichever platform plugin is in use, then leave. Shutdown is the
    # part being tested.
    QTimer.singleShot(250, app.quit)
    app.exec()

    assert resizes, "resized never fired -- the child would never be told"
    print("resized to:    ", resizes[-1])

    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())