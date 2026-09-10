# bindings/session_smoke_test.py
#
# Phase-2 check: a live terminal, entirely from Python. A PtySession starts a
# shell, the widget and the session are wired to each other, a command runs
# and its output reaches the screen.
#
# The previous two tests proved the boundary works. This one proves the
# library does what it exists to do, and it is the first test where a failure
# means something is wrong with the terminal rather than with the binding.
#
# Runs headless and exits on its own:
#
#   QT_QPA_PLATFORM=offscreen PYTHONPATH=build/bindings \
#       python bindings/session_smoke_test.py
#
# On Windows, quote each assignment or the value picks up the space before
# the &&:
#
#   set "PYTHONPATH=build\\bindings" && set "QT_QPA_PLATFORM=offscreen" && ^
#       python bindings\\session_smoke_test.py
#
# Pass --show to watch it in a window instead.

import sys

from PySide6.QtCore import QTimer
from PySide6.QtGui import QFont
from PySide6.QtWidgets import QApplication, QVBoxLayout, QWidget

import anytermqt

MARKER = "ANYTERMQT_OK"
TIMEOUT_MS = 10000


def marker_count(text: str) -> int:
    """How many times the marker appears, ignoring where lines break.

    A terminal wraps at the column, not at a word, so a marker straddling
    the right edge arrives as two pieces on two rows and a plain count
    misses it. Stripping whitespace before counting makes the check
    independent of the grid width, which is what is actually being tested
    -- the bytes reached the screen, not where they landed.
    """
    return "".join(text.split()).count(MARKER)


def shell() -> tuple[str, list[str]]:
    # There is no portable default, which is why PtySession::start does not
    # supply one either.
    if sys.platform == "win32":
        return "cmd.exe", []
    return "/bin/sh", ["-i"]


def main() -> int:
    app = QApplication(sys.argv)

    window = QWidget()
    window.setWindowTitle("anytermqt session smoke test")
    layout = QVBoxLayout(window)

    terminal = anytermqt.TerminalWidget()
    terminal.setTerminalFont(QFont("Monospace", 11))
    terminal.setScrollbackSize(2000)
    layout.addWidget(terminal)

    # The session is parented to the window, so the window owns it. Without a
    # parent it would need a Python reference held for as long as the child
    # runs.
    session = anytermqt.PtySession.create(window)

    # The three connections attach() makes in C++. Doing them here is the
    # same thing, and it is worth seeing them written out once: getting one
    # wrong gives a terminal that looks almost right -- typing works, but
    # full-screen applications never redraw.
    session.dataReceived.connect(terminal.feed)
    terminal.dataReady.connect(session.write)
    terminal.resized.connect(session.resize)

    exit_codes: list[int] = []
    session.finished.connect(exit_codes.append)

    program, args = shell()
    started = session.start(program, args, [],
                            terminal.columns(), terminal.terminalRows())
    if not started:
        print("start failed:", session.error(), file=sys.stderr)
        return 1

    print("running:", program, "pid alive:", session.running())

    # Type a command the way a user would: through the widget, so this
    # exercises the keymap and the write path rather than the session alone.
    QTimer.singleShot(400, lambda: terminal.send(f"echo {MARKER}\r"))

    # Poll the screen rather than sleeping a fixed time: fast on an idle
    # machine, patient on a loaded one.
    def check() -> None:
        # The echoed command line contains the marker too, so wait until it
        # appears twice: once as input, once as output.
        if marker_count(terminal.screenText()) >= 2:
            print("marker seen")
            terminal.send("exit\r")

    poll = QTimer()
    poll.timeout.connect(check)
    poll.start(100)

    # A hard stop, so a hang fails the test instead of sitting there.
    QTimer.singleShot(TIMEOUT_MS, app.quit)
    session.finished.connect(lambda _: QTimer.singleShot(200, app.quit))

    window.resize(800, 480)
    window.show()

    if "--show" in sys.argv:
        return app.exec()

    app.exec()

    text = terminal.screenText()
    assert marker_count(text) >= 2, \
        f"command output never reached the screen:\n{text}"
    assert exit_codes, "the shell never reported an exit"

    print("exit code:", exit_codes[0])
    print("scrollback retained:", terminal.scrollbackSize())
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())