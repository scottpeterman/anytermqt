"""test_terminal.py

Drives the extension module the way a host application would: a widget in a
layout, a session supplying bytes, and nothing else.

The selection probes below exist to split one symptom -- selection works in
the native binary and not here -- into the three causes that produce it.
Read the block printed at startup:

  * ImportError, or an empty selection API list
        The module predates the selection code, or the generated wrapper does.
        Check the path and mtime printed first, then bindings/CMakeLists.txt.

  * setSelection() paints a highlight, dragging does not
        The selection code is present and mouse events are not reaching
        TerminalWidget::mousePressEvent. Set PROBE_MOUSE and watch whether the
        viewport sees them at all.

  * hasSelection() is True and nothing is painted
        Neither the bindings nor the event path; look at paintSelection and
        the selection colour.

Set PROBE_MOUSE to trace every mouse event arriving at the viewport. It is
noisy by design -- leave it off unless the second case above is the one.
"""

import os
import pathlib
import sys
import time

from PySide6.QtCore import QEvent, QObject, QTimer
from PySide6.QtGui import QFont
from PySide6.QtWidgets import QApplication, QVBoxLayout, QWidget

import anytermqt

PROBE_MOUSE = False


class MouseSpy(QObject):
    """Reports mouse events reaching the viewport.

    The viewport, not the widget: QAbstractScrollArea's children receive the
    mouse and the scroll area sees it only because viewportEvent() forwards
    it. If events show up here but selection does not move, the forwarding is
    where to look; if nothing shows up here, the widget never had them.
    """

    WATCHED = {
        QEvent.Type.MouseButtonPress,
        QEvent.Type.MouseButtonRelease,
        QEvent.Type.MouseButtonDblClick,
        QEvent.Type.MouseMove,
    }

    def eventFilter(self, obj, event):
        if event.type() in self.WATCHED:
            print(f"viewport {event.type().name}", flush=True)
        return False


def report_module():
    path = pathlib.Path(anytermqt.__file__)
    stamp = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(path.stat().st_mtime))
    print(f"module: {path}")
    print(f"built:  {stamp}")


def report_selection_api(term):
    """What the generated wrapper actually exposes.

    A short list here is the whole answer: the wrapper was generated from a
    header that did not have this API yet, and no amount of looking at the
    C++ will show it, because the C++ is correct.
    """
    names = sorted(n for n in dir(term) if "select" in n.lower())
    print(f"selection API: {names or 'MISSING'}")


def probe_selection(term):
    """Select a range without touching the mouse.

    This is the split. Everything downstream of setSelection() -- the range
    query, the signal, the highlight -- is exercised here with the event path
    taken out of the picture.
    """
    top = term.viewTopLine()
    term.setSelection(top, 0, top, 20)
    print(f"hasSelection: {term.hasSelection()}")
    print(f"selectedText: {term.selectedText()!r}")
    print("a highlight should now be over the first 20 cells of the top row")


def main():
    app = QApplication(sys.argv)

    window = QWidget()
    window.setWindowTitle("anytermqt")
    layout = QVBoxLayout(window)

    term = anytermqt.TerminalWidget()
    term.setTerminalFont(QFont("Consolas" if os.name == "nt" else "Monospace", 11))
    term.setScrollbackSize(10000)
    layout.addWidget(term)

    term.selectionChanged.connect(
        lambda has: print(f"selectionChanged {has} {term.selectedText()!r}", flush=True)
    )

    spy = None
    if PROBE_MOUSE:
        # Parented, or it is collected while the filter is still installed.
        spy = MouseSpy(term)
        term.viewport().installEventFilter(spy)

    session = anytermqt.PtySession.create(window)
    session.dataReceived.connect(term.feed)
    term.dataReady.connect(session.write)
    term.resized.connect(session.resize)
    session.finished.connect(lambda code: app.quit())

    shell = "cmd.exe" if os.name == "nt" else "/bin/bash"
    if not session.start(shell, [], [], term.columns(), term.terminalRows()):
        print(f"failed to start {shell}: {session.error()}", file=sys.stderr)
        return 1

    window.resize(900, 560)
    window.show()
    term.setFocus()

    report_module()
    report_selection_api(term)

    # After the shell has had a chance to draw a prompt, so the selected range
    # covers something legible rather than a blank screen.
    QTimer.singleShot(500, lambda: probe_selection(term))

    return app.exec()


if __name__ == "__main__":
    sys.exit(main())