# terminal core + Qt widget

A terminal emulation core in plain C++17, a Qt terminal widget built on top of
it, and a Python module that exposes the widget to PySide6.

Three folders, one build. The split is the point: `pyte/` knows what a screen
*contains* and nothing about how it looks; `qtpyte/` knows how it looks and
what the keyboard sends, and parses no escape sequences at all.

```
pyte/      the emulation core -- no Qt, no display, no dependencies to inherit
qtpyte/    a Qt terminal widget that consumes it
bindings/  the widget as a Python module, for PySide6
```

Each has its own README with the detail. This one covers the shape;
`docs/BUILDING.md` covers building on each platform.

## Building

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/qtpyte/qtpyte-term
```

Requires CMake 3.21+, a C++17 compiler, and Qt 6.2+ for the widget. On Ubuntu:
`apt install qt6-base-dev cmake ninja-build build-essential`.

The core alone, with no Qt anywhere near it:

```sh
cmake -B build-core -G Ninja -DBUILD_WIDGET=OFF
cmake --build build-core && ctest --test-dir build-core
```

That configuration is worth running before a commit. It is what stops the core
from quietly growing a dependency on the widget.

The Python module is off by default and adds requirements the C++ build does
not have — Qt 6.10+, PySide6 and Shiboken, matched to each other:

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_BINDINGS=ON
```

`docs/BUILDING.md` has the version-matching rules and the Windows specifics.

## What it does

The core handles the escape-sequence vocabulary real applications use: SGR
including 24-bit colour, scrolling regions, insert and delete, the alternate
screen, and scrollback with paging and random access. It is verified against
reference pyte on captured device sessions, not only against its own
expectations.

The widget renders that to a grid of glyphs, with a scrollbar, resize
propagated to whatever is on the far end, and a keymap that sends what a
terminal sends.

It owns no pty, no shell and no SSH client. Bytes arrive through `feed()` and
leave through `dataReady()`, so what produces them is the caller's business: a
local pseudo-terminal on POSIX or ConPTY on Windows via `qtpyte::PtySession`,
an SSH channel, a socket, or a recorded stream in a test.

Confirmed against htop, btop, neofetch and doom-ascii on Linux, and cmd.exe and
btop on Windows.

## From Python

The widget is a real `QWidget` subclass on the Python side, so it goes into a
PySide6 layout like any other:

```python
from PySide6.QtCore import QByteArray
from PySide6.QtGui import QColor, QFont
from PySide6.QtWidgets import QApplication, QVBoxLayout, QWidget

import anytermqt

app = QApplication([])
window = QWidget()
layout = QVBoxLayout(window)

terminal = anytermqt.TerminalWidget()
terminal.setTerminalFont(QFont("Consolas", 11))
layout.addWidget(terminal)

palette = terminal.terminalPalette()
palette.set_background(QColor("#101010"))
terminal.setTerminalPalette(palette)

terminal.dataReady.connect(transport.write)     # keystrokes and replies out
terminal.resized.connect(transport.resize)      # the far end needs telling
terminal.feed(QByteArray(b"hello\r\n"))         # bytes in

window.show()
app.exec()
```

`PtySession` is not bound yet, so `transport` above is yours to supply from
Python — `ptyprocess` or `pywinpty` for a local shell, or a paramiko channel
for a remote one. Given the widget's contract that is a feature rather than a
gap: anything that can hand over bytes can drive it.

Two differences from the C++ API, both deliberate:

**`terminalPalette()` / `setTerminalPalette()` rather than `palette()`.** The
C++ accessor hands out a reference to mutate in place, which does not survive
a language binding — the generated wrapper copies it, so the change would land
on a temporary and nothing would report an error. The name also avoids
colliding with `QWidget.palette()`, which PySide6 already provides and which
returns a `QPalette`. Same reason `setTerminalFont` is not called `setFont`.

**The module is tied to one PySide6 version.** It links Qt, and PySide6 ships
its own copy of Qt; the two must be the same build or the import crashes
rather than erroring. See `docs/BUILDING.md`.

Bindings are PySide6 only. Shiboken generates against PySide6's type registry,
so a PyQt6 application cannot import this module.

## Not a fork of pyte

Named for it and reasoned against it, but written from scratch, and it
diverges where the reference is worth diverging from -- resize anchors the
bottom of the window so shrinking pushes rows into scrollback instead of
dropping them, and `CSI 3 J` clears saved lines as xterm defines it. The
`ptdiff` harness in `pyte/tools/` documents the intentional divergences
rather than tolerating them silently.

pyte is by Sergei Lebedev, under BSD-3-Clause:
<https://github.com/selectel/pyte>. The row-store model the core uses came
from the Go port in PathfinderSSH rather than from pyte itself.

## Third-party

- **utf8proc** -- character width and Unicode data, fetched at configure time
- **doctest** -- the core's test framework, fetched at configure time
- **Qt 6** -- the widget only, found on the system
- **PySide6 and Shiboken** -- the bindings only, found in site-packages

## Licence

See `LICENSE`.