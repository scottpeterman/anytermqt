# bindings/smoke_test.py
#
# Phase-0 check. It does not test Palette so much as test that a C++ class
# from this project can be constructed from Python, hold Qt types, and hand
# them back with their values intact -- which is the whole toolchain in one
# assertion.
#
# Run with the build's binding directory on the path:
#   PYTHONPATH=build/bindings python bindings/smoke_test.py

from PySide6.QtWidgets import QApplication  # noqa: F401 — loads Qt6Widgets.dll
from PySide6.QtGui import QColor

import anytermqt

import sys

def main() -> int:
    palette = anytermqt.Palette()

    # Defaults come from the C++ constructor, not from Python.
    print("foreground:", palette.foreground().name())
    print("background:", palette.background().name())
    print("cursor:    ", palette.cursor().name())

    # A QColor made in Python, stored in C++, read back in Python. If the
    # PySide6 build and this module disagree about Qt, this is where it
    # shows -- usually as a crash rather than a wrong answer.
    palette.set_ansi(1, QColor("#ff0000"))
    got = palette.color(1, QColor("#000000"))
    assert got.name() == "#ff0000", f"round trip failed: {got.name()}"

    # Index 9 is bright red: the bold brightening rule lives in C++.
    bright = palette.color(1, QColor("#000000"), True)
    print("ansi 1:    ", got.name())
    print("ansi 1 bold:", bright.name())

    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
