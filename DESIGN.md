<!-- docs/DESIGN.md -->

# Design Notes

Why this widget exists, what it deliberately does not do, and the
constraints that shape the binding and packaging work.

## Motivation

A developer writing a Qt application in Python who needs an embedded
terminal currently has three options, none of them good:

1. **QTermWidget** — mature and battle-tested, with sip bindings in-tree
   and a third-party PySide6 packaging on PyPI. But it descends from
   Konsole and is Unix pty to the bone. Published wheels cover Linux and
   macOS. Windows is not served.
2. **QPlainTextEdit + pyte** — the common roll-your-own approach. Works,
   but the paint path in Python is slow enough to be noticeable, and
   these implementations are almost always written around spawning a
   local shell.
3. **QWebEngineView + xterm.js** — the default answer, and the reason a
   great many Qt applications carry a Chromium runtime for the sole
   purpose of drawing eighty columns of text.

The gap is a terminal widget that is (a) native, (b) cross-platform
including Windows, and (c) indifferent to where its bytes come from.

That last point is the one that matters most in practice. The
overwhelming majority of embedded-terminal code assumes the terminal's
job is to host a local shell. But plenty of tools need a terminal
attached to something else entirely — an SSH channel, a serial port, a
subprocess on another host, a socket to a local engine process. Those
tools end up reimplementing terminal emulation badly, or reaching for a
browser.

This project is the widget the author wanted years ago: on Windows,
building basic automation tooling, with no terminal option available at
all.

## Core design decision: the transport is not our problem

The widget does **not** own a pty, a shell, an SSH client, or a serial
port. It owns a screen.

The interface is deliberately narrow:

| Direction | Surface | Meaning |
|---|---|---|
| In | `feed(bytes)` | Push received bytes into the emulator |
| Out | `dataReady(QByteArray)` | Keystrokes and responses to send upstream |
| Control | `resize(cols, rows)` | Geometry change, caller propagates as needed |

Everything else — connection setup, authentication, reconnect, window
size negotiation — belongs to the caller.

### Why not an abstract `Transport` base class?

The obvious alternative is a C++ `Transport` interface that callers
subclass. It is the wrong shape here, for one specific reason: once the
widget is bound to Python, subclassing means virtual dispatch back
across the language boundary. Every inbound byte then requires acquiring
the GIL on whatever thread the data arrived on, and marshalling to Qt's
main thread. That is the failure mode that makes this class of binding
fragile and hard to debug.

Inverting it removes the problem entirely. `feed()` is a slot; the
caller invokes it from the GUI thread, having done its own threading
however it likes. There is no callback from C++ into Python at all.

### What this buys

- Windows works for the SSH and serial cases immediately, without
  touching ConPTY.
- Any transport is supported without the widget knowing it exists:
  paramiko, asyncssh, `QProcess`, a socket to a sidecar process, a
  serial library, a test fixture replaying a captured stream.
- Testing is trivial. Feed a recorded byte stream, assert on screen
  state. No process, no pty, no network.

A local-pty convenience implementation may ship as an optional extra,
but it is a courtesy, not the architecture.

## Binding strategy

The constraint here is the Qt ABI, not the binding syntax.

PyQt6 and PySide6 each ship their own bundled copy of Qt. A `QWidget`
must be a first-class citizen of one binding's type system, and loading
two Qt copies into one process is a reliable way to produce crashes that
are miserable to diagnose. This forces the choice:

- **sip** — the tool PyQt itself uses. Produces a genuine PyQt widget.
  PyQt only.
- **Shiboken6** — Qt's own tool for Qt for Python. Produces a genuine
  PySide6 widget. PySide only.
- **pybind11 + wrap-instance shim** — expose the `QWidget*` as an
  integer from a binding-agnostic module, then wrap it on the Python
  side via `sip.wrapinstance` or `shiboken6.wrapInstance` in a thin
  per-binding shim. One C++ core, two shims — but still a separate
  compile against each target's Qt, so it does not escape the matrix.

**Initial target: PySide6 via Shiboken6, Windows first.** It is the
official Qt tooling, and Windows is the square nobody currently
occupies. PyQt6 support is a candidate for later, not a launch
requirement.

## Packaging is the actual project

The C++ is the interesting part; the wheels are the work. Adoption is
decided entirely by whether `pip install` succeeds on the first attempt.

The matrix is bindings × CPython versions × platforms, and it must be
rebuilt against each new PySide6 release, because each release may carry
a different Qt. Plan for cibuildwheel plus CI from the beginning rather
than bolting it on, and treat the recurring rebuild as ongoing
maintenance rather than a one-time setup cost.

## Scope boundaries

Explicitly **out of scope**, permanently:

- SSH, telnet, or serial client implementations
- Session management, connection profiles, credential storage
- Configuration file formats
- Tabs, splits, or window management

These belong to applications built on the widget. Absorbing them turns a
dependency into a competing product and makes the widget harder to adopt
for every use case that wanted only a screen.

**In scope:** emulation correctness, selection and clipboard, scrollback,
theming and font handling, and predictable performance under fast output.

## Open items

- License lineage needs to be traced and stated clearly before
  distribution, from the original Python emulator through the Go port to
  this C++ implementation.
- Windows key handling and clipboard semantics need explicit test
  coverage; they are the most likely source of platform-specific
  surprises.
- Decide whether an optional local-pty helper ships in the same wheel or
  as a separate extra.
