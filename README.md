# anytermqt

A native terminal widget for PySide6. It is a real `QWidget`, it runs a local
shell on Windows, Linux and macOS out of the box, and there is no browser
underneath it — no WebEngine, no xterm.js, no JavaScript bridge.

![btop over ssh, in the widget, driven from Python on Windows](https://raw.githubusercontent.com/scottpeterman/anytermqt/main/screenshots/btop-remote.png)

That is btop on a Linux box, over ssh from Windows, rendered by this widget
inside a PySide6 application: braille sparklines, box drawing, 24-bit colour
and the alternate screen, all at once. Underneath is a terminal emulation core
in plain C++17 and a Qt widget built on it, exposed to Python through
Shiboken — the same widget a native C++ application gets.

## Install

Wheels are attached to each [GitHub release](https://github.com/scottpeterman/anytermqt/releases).
Not on PyPI yet; until it is, point pip at the release and it picks the right
wheel for your platform:

```sh
pip install anytermqt --find-links https://github.com/scottpeterman/anytermqt/releases/expanded_assets/v0.1.0
```

| Platform | Wheel |
|---|---|
| Windows x64 | `win_amd64` |
| Linux x86_64, glibc 2.34+ (Ubuntu 22.04+, Debian 12+, RHEL 9+) | `manylinux_2_34_x86_64` |
| macOS Apple Silicon, macOS 26+ | `macosx_26_0_arm64` |

One `abi3` wheel per platform covers Python 3.10 through 3.14.

**PySide6 is pinned exactly.** The wheel carries no Qt of its own and depends on
`PySide6==6.10.3`. The module links Qt and PySide6 ships Qt, so the two must be
the same build — a mismatch crashes on import rather than failing in the
resolver. Install into a venv: pip will replace any other PySide6 already in
the environment.

## Quick start

A local shell in a window — `cmd.exe` on Windows, bash elsewhere:

```python
import os
import sys

from PySide6.QtGui import QFontDatabase
from PySide6.QtWidgets import QApplication, QVBoxLayout, QWidget

import anytermqt

app = QApplication(sys.argv)
window = QWidget()
layout = QVBoxLayout(window)

terminal = anytermqt.TerminalWidget()
font = QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont)
font.setPointSize(11)
terminal.setTerminalFont(font)
layout.addWidget(terminal)

session = anytermqt.PtySession.create(window)
session.dataReceived.connect(terminal.feed)     # bytes in
terminal.dataReady.connect(session.write)       # keystrokes and replies out
terminal.resized.connect(session.resize)        # the far end needs telling
session.finished.connect(lambda code: app.quit())

shell = "cmd.exe" if os.name == "nt" else "/bin/bash"
if not session.start(shell, [], [], terminal.columns(), terminal.terminalRows()):
    sys.exit(f"failed to start {shell}: {session.error()}")

window.resize(900, 560)
window.show()
terminal.setFocus()
sys.exit(app.exec())
```

![windows cmd.exe, driven from Python on Windows](https://raw.githubusercontent.com/scottpeterman/anytermqt/main/screenshots/windows_python_binding.png)

Those three connections are the whole integration, and getting one wrong gives
a terminal that looks almost right: typing works and full-screen applications
never redraw, or everything renders until the window is resized. `dataReady`
in particular carries the emulator's replies to Device Status Report and
friends, not just keystrokes, and an application waiting on one of those
answers hangs rather than failing.

`terminal.setFocus()` is worth keeping. The widget is `Qt::StrongFocus` and in
a layout by itself will usually take focus anyway, but in a real host window
with other focusable widgets it will not, and a terminal that ignores the
keyboard reads as a broken terminal rather than as an unfocused one.

## Sessions

`PtySession.create()` gives a pseudo-terminal — a POSIX pty or ConPTY,
whichever platform you are on — and starts whatever you name in it. A shell
gives a local terminal; `ssh` gives a remote one:

```python
session.start("ssh", ["user@host"], [],
              terminal.columns(), terminal.terminalRows())
```

The widget neither knows nor cares which, because all it ever sees is bytes.

Parent the session, as above, so the window owns it. Created without a parent
it is owned by the Python name bound to it, and collecting that while the
child is still running kills the child.

Nothing obliges you to use it. The widget owns no pty, no shell and no SSH
client, and the other end can be any byte source at all:

```
bytes arriving       ->  terminal.feed(QByteArray)
terminal.dataReady   ->  whatever sends them onward
terminal.resized     ->  telling the far end the new size
```

A Python class can subclass `PtySession` and implement `start`, `write` and
`resize` over a paramiko channel or a socket — or skip the session entirely,
connect `dataReady` to whatever sends bytes, and call `feed()` when bytes
arrive.

### SSH with paramiko

`examples/ssh_terminal.py` is that contract written against paramiko — a small
connect dialog, then a live session, with no C++ involved:

![the connect dialog from examples/ssh_terminal.py](https://raw.githubusercontent.com/scottpeterman/anytermqt/main/screenshots/ssh-example-dialog.png)

![btop running over the paramiko session](https://raw.githubusercontent.com/scottpeterman/anytermqt/main/screenshots/ssh-example-terminal.png)

```sh
pip install anytermqt paramiko --find-links https://github.com/scottpeterman/anytermqt/releases/expanded_assets/v0.1.0
python examples/ssh_terminal.py
```

Two things that example gets right and are easy to get wrong:

`recv()` blocks, so the reader runs in a thread, and it reaches the widget by
emitting a signal rather than calling `feed()` directly. Emitting across
threads gives a queued connection, so the bytes land on the GUI thread.
Calling `feed()` from the reader appears to work and corrupts the screen under
load.

The size is passed to `invoke_shell()` as well as on resize. Without it the
far end assumes 80x24 and anything full-screen draws to the wrong shape until
the first resize happens to correct it.

Network gear often needs key exchange and cipher algorithms that current
paramiko no longer offers by default. The example does not adjust them, so a
negotiation failure against an older switch is the example's limitation rather
than the device's.

## Selection

Drag to select, double-click for a word, triple-click for a line, shift-click
to extend. Dragging past the top or bottom edge scrolls and keeps extending.

A word is wider than `isalnum`: `_ - . / : ~ @ + = % #` all count, because in a
terminal the thing worth double-clicking is usually a path, a URL, an address
or an identifier.

Copying is never automatic. `Ctrl+Shift+C` copies, `Ctrl+Shift+V` pastes,
`Ctrl+Shift+A` selects all — shifted, so `Ctrl+C` stays SIGINT and `Ctrl+V`
stays a literal control character. On X11 the primary selection follows the
highlight and middle-click pastes it; the clipboard proper stays untouched
until you ask.

The selection is held in absolute line coordinates and converted to viewport
rows only at paint time, so a highlight does not crawl up the screen when
output arrives underneath it.

For a host application: `hasSelection()`, `selectedText()`, `setSelection()`,
`selectAll()`, `clearSelection()`, `copySelection()`, a `selectionChanged(bool)`
signal for enabling a Copy menu item, and `setSelectionColor()`.

Mouse *reporting* — `DECSET 1000/1002/1006`, the modes a full-screen
application uses to read the mouse itself — is not implemented yet. When it
lands, an application gets the drag and Shift forces selection, which is
xterm's convention.

## Python API notes

**`terminalPalette()` / `setTerminalPalette()` rather than `palette()`.** The
C++ accessor hands out a reference to mutate in place, which does not survive a
language binding — the generated wrapper copies it, so a change would land on a
temporary and nothing would report an error. The name also avoids colliding
with `QWidget.palette()`, which returns a `QPalette`. Same reason
`setTerminalFont` is not called `setFont`.

```python
palette = terminal.terminalPalette()
palette.set_background(QColor("#101010"))
terminal.setTerminalPalette(palette)
```

**PySide6 only.** Shiboken generates against PySide6's type registry, so a
PyQt6 application cannot import this module.

## What it handles

The core covers the escape-sequence vocabulary real applications use: SGR
including 24-bit colour, scrolling regions, insert and delete, the alternate
screen, and scrollback with paging and random access. It is verified against
reference pyte on captured device sessions, not only against its own
expectations.

Confirmed against htop, btop, neofetch and doom-ascii on Linux, cmd.exe and
btop on Windows, and htop on macOS — the same widget on all three, over a POSIX
pty or ConPTY depending on where it is running.

The screenshot at the top is a harder test than a local shell: ConPTY is
running Windows' own `ssh.exe`, and what the core parses is the *remote*
btop's escape stream relayed through it. The bytes crossed a network and two
terminal conventions before reaching `pyte::Stream`, and nothing on the Windows
side normalised them on the way.

## How it is built

Three folders, one build. The split is the point: `pyte/` knows what a screen
*contains* and nothing about how it looks; `qtpyte/` knows how it looks and
what the keyboard sends, and parses no escape sequences at all.

```
pyte/      the emulation core -- no Qt, no display, no dependencies to inherit
qtpyte/    a Qt terminal widget that consumes it
bindings/  the widget as a Python module, for PySide6
python/    the Python package the module ships inside
```

Each has its own README with the detail.

## Building from source

The C++ core and widget:

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/qtpyte/qtpyte-term
```

Requires CMake 3.21+, a C++17 compiler, and Qt 6.2+ for the widget. On Ubuntu:
`apt install qt6-base-dev cmake ninja-build build-essential`.

The core alone, with no Qt anywhere near it — worth running before a commit,
because it is what stops the core from quietly growing a dependency on the
widget:

```sh
cmake -B build-core -G Ninja -DBUILD_WIDGET=OFF
cmake --build build-core && ctest --test-dir build-core
```

The Python module is off by default and needs a Qt that matches PySide6 and
Shiboken exactly — 6.10.3 for the current wheels:

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_BINDINGS=ON
```

To build a wheel, `scripts/wheel.sh` or `scripts\wheel.bat`. On Windows,
`scripts/` wraps all of the above with the environment checks that account for
most of the ways this goes wrong — the wrong Native Tools prompt, a `--user`
pip install CMake cannot find, and a PySide6 whose Qt does not match the one
being built against. `scripts/README.md` has the detail, and
[`docs/building.md`](https://github.com/scottpeterman/anytermqt/blob/main/docs/building.md)
has the version-matching rules and each platform's specifics.

## Not a fork of pyte

Named for it and reasoned against it, but written from scratch, and it diverges
where the reference is worth diverging from — resize anchors the bottom of the
window so shrinking pushes rows into scrollback instead of dropping them, and
`CSI 3 J` clears saved lines as xterm defines it. The `ptdiff` harness in
`pyte/tools/` documents the intentional divergences rather than tolerating them
silently.

pyte is by Sergei Lebedev, under BSD-3-Clause:
<https://github.com/selectel/pyte>. The row-store model the core uses came from
the Go port in PathfinderSSH rather than from pyte itself.

## Third-party

- **utf8proc** — character width and Unicode data, fetched at configure time
- **doctest** — the core's test framework, fetched at configure time
- **Qt 6** — the widget only, found on the system
- **PySide6 and Shiboken** — the bindings only, found in site-packages

## Licence

GPL-3.0. See `LICENSE`.