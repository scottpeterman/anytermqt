<!-- README.md -->

# anytermqt — terminal core + Qt widget

A terminal emulation core in plain C++17, a Qt terminal widget built on top of
it, and a Python module that exposes the widget to PySide6.

Three folders, one build. The split is the point: `pyte/` knows what a screen
*contains* and nothing about how it looks; `qtpyte/` knows how it looks and
what the keyboard sends, and parses no escape sequences at all.

```
pyte/      the emulation core -- no Qt, no display, no dependencies to inherit
qtpyte/    a Qt terminal widget that consumes it
bindings/  the widget as a Python module, for PySide6
python/    the Python package the module ships inside
```

Each has its own README with the detail. This one covers the shape;
[`docs/building.md`](https://github.com/scottpeterman/anytermqt/blob/main/docs/building.md) covers building on each platform.

![btop over ssh, in the widget, driven from Python on Windows](https://raw.githubusercontent.com/scottpeterman/anytermqt/main/screenshots/btop-remote.png)
![windows cmd.exet, driven from Python on Windows](https://raw.githubusercontent.com/scottpeterman/anytermqt/main/screenshots/windows_python_binding.png)


That is btop running on a Linux box, over ssh from Windows, rendered by this
widget inside a PySide6 application — no WebEngine, no xterm.js, no JavaScript
bridge. Braille sparklines, box drawing, 24-bit colour and the alternate
screen, all at once.

Worth being precise about that path, because it is a stronger claim than a
local shell would be: ConPTY is running Windows' own `ssh.exe`, and what the
core is parsing is the *remote* btop's escape stream relayed through it. The
bytes crossed a network and two terminal conventions before reaching
`pyte::Stream`, and nothing on the Windows side normalised them on the way.

## Installing


Not on PyPI yet. Wheels are built from this tree -- `scripts/wheel.sh` or
`scripts\wheel.bat`, and `docs/building.md` for what they do.

One wheel covers Python 3.9 and later on a given platform: the module is built
against the limited API, so the tag is `abi3` rather than a single Python
version. It carries no Qt of its own and pulls an exact PySide6, which is the
one thing about it worth knowing before pinning anything in an application --
the module links Qt and PySide6 ships Qt, and the two have to be the same
build.

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

On Windows, `scripts/` wraps all of the above with the environment checks that
account for most of the ways this goes wrong — the wrong Native Tools prompt, a
`--user` pip install CMake cannot find, and a PySide6 whose Qt does not match
the one being built against. `scripts/README.md` has the detail.

`docs/building.md` has the version-matching rules and the Windows specifics.

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

Confirmed against htop, btop, neofetch and doom-ascii on Linux, cmd.exe and
btop on Windows, and htop on macOS -- the same widget on all three, over a
POSIX pty or ConPTY depending on where it is running.

### Selection

Drag to select, double-click for a word, triple-click for a line, shift-click
to extend. Dragging past the top or bottom edge scrolls and keeps extending.

A word here is wider than `isalnum`: `_ - . / : ~ @ + = % #` all count, because
in a terminal the thing worth double-clicking is usually a path, a URL, an
address or an identifier, and stopping at every dot or slash turns one
double-click into six.

Copying is never automatic. `Ctrl+Shift+C` copies, `Ctrl+Shift+V` pastes,
`Ctrl+Shift+A` selects all — shifted, so `Ctrl+C` stays SIGINT and `Ctrl+V`
stays a literal control character. On X11 the primary selection follows the
highlight and middle-click pastes it, which is the platform's own convention
and costs nothing; the clipboard proper stays untouched until you ask.

The selection is held in absolute line coordinates and converted to viewport
rows only at paint time, so a highlight does not crawl up the screen when
output arrives underneath it.

For a host application: `hasSelection()`, `selectedText()`, `setSelection()`,
`selectAll()`, `clearSelection()`, `copySelection()`, a `selectionChanged(bool)`
signal for enabling a Copy menu item, and `setSelectionColor()`.

Mouse *reporting* — `DECSET 1000/1002/1006`, the modes a full-screen
application uses to read the mouse itself — is not implemented yet. The mouse
handlers already ask the question, so that when it lands an application gets
the drag and Shift forces selection, which is xterm's convention.

## From Python

The widget is a real `QWidget` subclass on the Python side, so it goes into a
PySide6 layout like any other:

```python
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

session = anytermqt.PtySession.create(window)
session.dataReceived.connect(terminal.feed)     # bytes in
terminal.dataReady.connect(session.write)       # keystrokes and replies out
terminal.resized.connect(session.resize)        # the far end needs telling

session.start("ssh", ["user@host"], [],
              terminal.columns(), terminal.terminalRows())

window.show()
terminal.setFocus()
app.exec()
```

Those three connections are what `qtpyte::attach()` does in C++, written out
here because getting one wrong gives a terminal that looks almost right:
typing works and full-screen applications never redraw, or everything renders
until the window is resized. `dataReady` in particular carries the emulator's
replies to Device Status Report and friends, not just keystrokes, and an
application waiting on one of those answers hangs rather than failing.

`terminal.setFocus()` is worth keeping. The widget is `Qt::StrongFocus` and in
a layout by itself will usually take focus anyway, but in a real host window
with other focusable widgets it will not, and a terminal that ignores the
keyboard reads as a broken terminal rather than as an unfocused one.

`PtySession.create()` gives a pseudo-terminal — a POSIX pty or ConPTY,
whichever platform you are on — and starts whatever you name in it. A shell
(`/bin/bash`, `cmd.exe`) gives a local terminal; `ssh` gives a remote one, and
the widget neither knows nor cares which, because all it ever sees is bytes.

The session is parented to the window above, so the window owns it. Created
without a parent it is owned by the Python name bound to it, and collecting
that while the child is still running kills the child.

Nothing obliges you to use it. `PtySession` is an interface in terms of bytes,
so a Python class can subclass it and implement `start`, `write` and `resize`
over a paramiko channel or a socket — and a caller that would rather not
subclass anything can skip the session entirely, connect `dataReady` to
whatever sends bytes, and call `feed()` when bytes arrive.

Two differences from the C++ API, both deliberate:

**`terminalPalette()` / `setTerminalPalette()` rather than `palette()`.** The
C++ accessor hands out a reference to mutate in place, which does not survive
a language binding — the generated wrapper copies it, so the change would land
on a temporary and nothing would report an error. The name also avoids
colliding with `QWidget.palette()`, which PySide6 already provides and which
returns a `QPalette`. Same reason `setTerminalFont` is not called `setFont`.

**The module is tied to one PySide6 version.** It links Qt, and PySide6 ships
its own copy of Qt; the two must be the same build or the import crashes
rather than erroring. See `docs/building.md`.

Bindings are PySide6 only. Shiboken generates against PySide6's type registry,
so a PyQt6 application cannot import this module.

## SSH, or anything else

Nothing in the widget knows what a pty is. `PtySession` is one implementation
of a three-point contract, not a requirement, and the other end can be any
byte source at all:

```
bytes arriving       ->  terminal.feed(QByteArray)
terminal.dataReady   ->  whatever sends them onward
terminal.resized     ->  telling the far end the new size
```

`examples/ssh_terminal.py` is that written against paramiko — a small connect
dialog, then a live session, with no C++ involved:

```python
self.session.dataReceived.connect(self.terminal.feed)
self.terminal.dataReady.connect(self.session.write)
self.terminal.resized.connect(self.session.resize)
```

![the connect dialog from examples/ssh_terminal.py](https://raw.githubusercontent.com/scottpeterman/anytermqt/main/screenshots/ssh-example-dialog.png)

![btop running over the paramiko session](https://raw.githubusercontent.com/scottpeterman/anytermqt/main/screenshots/ssh-example-terminal.png)

Two things that example gets right and are easy to get wrong:

`recv()` blocks, so the reader runs in a thread, and it reaches the widget by
emitting a signal rather than calling `feed()` directly. Emitting across
threads gives a queued connection, so the bytes land on the GUI thread.
Calling `feed()` from the reader appears to work and corrupts the screen under
load.

The size is passed to `invoke_shell()` as well as on resize. Without it the
far end assumes 80x24 and anything full-screen draws to the wrong shape until
the first resize happens to correct it.

Run it with:

```
pip install anytermqt paramiko
python examples/ssh_terminal.py
```

Network gear often needs key exchange and cipher algorithms that current
paramiko no longer offers by default. The example does not adjust them, so a
negotiation failure against an older switch is the example's limitation rather
than the device's.

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

GPL-3.0. See `LICENSE`.

That is worth reading before building this into something: it is a copyleft
licence, and linking the widget or importing the Python module puts your
application under it. Open an issue if that is the only thing standing between
you and using it.