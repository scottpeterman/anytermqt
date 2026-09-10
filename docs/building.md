<!-- docs/building.md -->

# Building and running

Three components, each optional above the one below it:

- `pyte/` — the emulation core. Plain C++17, no Qt, no display.
- `qtpyte/` — the Qt widget. Needs Qt 6.2+, Widgets only.
- `bindings/` — the Python module. Needs Qt 6.10+, PySide6 and Shiboken.

Requirements everywhere: CMake 3.21+, a C++17 compiler.

`scripts/` wraps everything below with the preflight checks for each platform —
`build.sh` and `build.bat`, same names and arguments on all three. This document
is what those scripts encode, and what to read when one of them stops. See
`scripts/README.md`.

---

## Linux

The widget, against the distro Qt:

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/qtpyte/qtpyte-term
```

No `CMAKE_PREFIX_PATH` — a distro Qt installs its package files where CMake
already looks, and naming a prefix is mostly a chance to name the wrong one.

That Qt will not do for the bindings. Ubuntu 22.04 ships 6.2 and 24.04 ships
6.4, both below the 6.10 floor, so on Linux the mismatch with PySide6 is the
normal state rather than a mistake. The fix is a second Qt, not a pip pin — see
*Linux prerequisites* under Python bindings.

## macOS

```bash
brew install cmake ninja qt
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
./build/qtpyte/qtpyte-term
```

`CMAKE_PREFIX_PATH` is required here and not on Linux: Homebrew's Qt is
keg-only, so nothing lands on `PATH` or in CMake's search paths and the usual
symptom is CMake reporting no Qt6 on a machine where Qt is plainly installed.

On Apple Silicon, check that Python is native before building the bindings.
An x86_64 interpreter under Rosetta links against an arm64 Qt and then fails
to load, naming a file rather than a slice:

```bash
python3 -c "import platform; print(platform.machine())"   # must match uname -m
```

## Windows

**Visual Studio** — any 2022 edition including Build Tools, with the
**Desktop development with C++** workload. That workload supplies the
compiler, the Windows SDK, CMake and Ninja. A Node/Electron toolchain often
installs only a subset and will be missing CMake and Ninja.

**Qt**, via `aqtinstall` — the official prebuilt binaries, no Qt account, and
just `qtbase` rather than several gigabytes of SDK:

```bat
python -m pip install --user aqtinstall
python -m aqt list-qt windows desktop --arch 6.10.3
python -m aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 --archives qtbase -O C:\Qt
```

Read the architecture string from the `--arch` output rather than assuming
it; older releases use `win64_msvc2019_64`.

**Build** from the *x64 Native Tools Command Prompt for VS 2022* — not a
plain `cmd`, and not the x86 prompt:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=C:\Qt\6.10.3\msvc2022_64
cmake --build build
set PATH=C:\Qt\6.10.3\msvc2022_64\bin;%PATH%
build\qtpyte\qtpyte-term.exe
```

The `PATH` line lets the Qt DLLs resolve at run time. For a distributable
build use `windeployqt` instead.

---

## Python bindings

Off by default. Enable with `-DBUILD_BINDINGS=ON`.

### Version matching is the whole problem

The extension module links Qt, and PySide6 ships its own copy of Qt. If those
are different builds, two Qt copies end up in one process, and the result is
usually a crash rather than an error message. The Qt you build against and the
Qt inside PySide6 must be the same version.

PySide6 patch numbers do not track Qt patch numbers, and a Qt version can
appear in `aqt list-qt` before it is published to the mirrors. So pick a Qt
version that actually installs, then pin PySide6 to match it:

```bash
pip install --user pyside6==6.10.3 shiboken6==6.10.3 shiboken6_generator==6.10.3
python -c "from PySide6.QtCore import qVersion; print(qVersion())"
```

That must print the same version as the Qt you installed. 6.10 is the floor:
the `Shiboken6Tools` CMake package arrived in 6.10 and replaces several
hundred lines of manual generator wiring.

`shiboken6_generator` is on PyPI. Older documentation says it is not, and
points at `https://download.qt.io/official_releases/QtForPython/`; that is no
longer necessary.

At run time it is PySide6's Qt that gets loaded, not the one you built
against — PySide6 is imported first and its DLL directory wins regardless of
`PATH`. The pin above is what makes that harmless rather than a crash. It is
also why the plugin path in any Qt warning points into `site-packages`.

### Linux prerequisites

Three things the C++ build does not need, each of which fails late and
unhelpfully when missing.

**A Qt that matches PySide6.** The distro one will not. Install a second Qt
alongside it and leave the distro one where it is:

```bash
python3 -m venv ~/venvs/qt && source ~/venvs/qt/bin/activate
pip install aqtinstall pyside6==6.10.3 shiboken6==6.10.3 shiboken6_generator==6.10.3

python -m aqt list-qt linux desktop --arch 6.10.3
python -m aqt install-qt linux desktop 6.10.3 linux_gcc_64 --archives qtbase icu -O ~/Qt
```

**`icu` in that archive list.** Qt 6 on Linux links against a specific ICU
major version and ships it in `lib/`, but ICU is a *separate archive inside*
the `qtbase` module — so `--archives qtbase` alone downloads Qt and drops the
ICU it depends on. The distro copy cannot substitute, because the symbols carry
the version. The failure is at link time, forty targets in:

```
libicui18n.so.73, needed by libQt6Core.so.6.10.3, not found
undefined reference to `ucnv_open_73'
```

Verify with `ls ~/Qt/6.10.3/gcc_64/lib/libicu*`. Windows needs no equivalent,
which is why the `--archives qtbase` line in the Windows section above is
correct there and not here.

**Clang, for the generator.** Shiboken parses headers with libclang, and
libclang needs its own builtin include directory — where `stddef.h` lives.
That comes with a clang installation, not with Qt and not with the
`shiboken6_generator` wheel, which ships the generator binary and some Qt
libraries and no clang resource headers at all:

```bash
sudo apt install clang llvm libclang-dev
```

`llvm` matters as much as `clang`: it provides `llvm-config`, which is how
Shiboken locates the directory. If it still is not found, name it —
`ls -d /usr/lib/llvm-*` gives the number:

```bash
export LLVM_INSTALL_DIR=/usr/lib/llvm-14
```

Then build against the new Qt rather than the distro one:

```bash
export QT_DIR=$HOME/Qt/6.10.3/gcc_64
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_BINDINGS=ON \
      -DCMAKE_PREFIX_PATH="$QT_DIR"
```

Runtime X11 dependencies, if the window fails to open rather than the build
failing: `sudo apt install libgl1-mesa-dev libxcb-cursor0`. The second is the
one that bites late — without it Qt 6.5+ reports that it could not load the
xcb platform plugin, after everything has built cleanly.

### Build the bindings in Release

On Windows, always. PySide6 ships release binaries only, so a Debug extension
wants the debug CRT and `python310_d.lib` and has nothing to link against.
Debug configures and builds on Linux and macOS, where the same libraries serve
both.

`bindings/CMakeLists.txt` gives the imported generator a Debug location so a
Debug configure at least reaches the compiler — see the troubleshooting entry
below for what goes wrong without it.

### Building

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_BINDINGS=ON \
      -DCMAKE_PREFIX_PATH=/path/to/Qt/6.10.3/gcc_64
cmake --build build
PYTHONPATH=build/bindings python bindings/smoke_test.py
QT_QPA_PLATFORM=offscreen PYTHONPATH=build/bindings python bindings/widget_smoke_test.py
```

On Windows a `pip install --user` puts the packages where CMake will not find
them, so pass the location explicitly:

```bat
python -c "import shiboken6_generator,os;print(os.path.dirname(os.path.dirname(shiboken6_generator.__file__)))"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_BINDINGS=ON ^
      -DCMAKE_PREFIX_PATH=C:\Qt\6.10.3\msvc2022_64 ^
      -DSHIBOKEN_SITELIB=<path printed above>
```

A virtualenv avoids this entirely and is the better habit.

Running the smoke tests on Windows, in one line — note the quoting, which
matters:

```bat
set "PATH=C:\Qt\6.10.3\msvc2022_64\bin;%PATH%" && set "PYTHONPATH=build\bindings" && set "QT_QPA_PLATFORM=offscreen" && python bindings\widget_smoke_test.py
```

`set VAR=value && ...` without the quotes puts the space before `&&` inside
the value, and Qt then looks for a platform plugin named `"offscreen "`.

### The three smoke tests

`smoke_test.py` constructs `Palette`, stores a `QColor` made in Python and
reads it back. It is the toolchain in one assertion: if the module and PySide6
disagree about Qt, that is where it shows.

`widget_smoke_test.py` puts a `TerminalWidget` in a Python-owned layout,
connects its signals, feeds it bytes and exits. The assertions matter less
than the exit: a wrong ownership rule does not fail at `addWidget`, it fails
at interpreter shutdown after everything has apparently worked. Run it to
completion and check the status.

`session_smoke_test.py` starts a real shell through `PtySession`, drives it
from Python and reads back what the emulator made of the output. It is the only
one that exercises the pty backend, so it is the one that fails when ConPTY or
the POSIX pty is at fault rather than the bindings.

Add `--show` to watch any of them instead of running headless.

### Adding a class to the bindings

Three edits, all in `bindings/`:

1. `anytermqt_global.h` — include the class's header. Include **specific** Qt
   headers, never the module umbrellas (`<QtCore/QtCore>` and friends): those
   pull in `qplugin.h`, which Clang cannot parse under MSVC.
2. `typesystem_anytermqt.xml` — a `<value-type>` for a copyable class, an
   `<object-type>` for anything deriving from QObject.
3. `CMakeLists.txt` — add the generated wrapper filename to
   `GENERATED_SOURCES`. Shiboken names them
   `<lowercase-namespace>_<lowercase-class>_wrapper.cpp`.

A method returning a mutable reference to a member needs more than an entry.
The generated wrapper copies a value-type on return, so the C++ idiom of
mutating the reference and calling `refresh()` compiles, runs, reports success
and changes nothing. Remove the accessor with `<modify-function ...
remove="all"/>` and expose a by-value getter and setter instead;
`TerminalWidget::terminalPalette()` is the worked example. The same applies to
overloads differing only in constness, which Python cannot express.

---

## Packaging a wheel

The wheel does not contain Qt.

That is the decision everything else here follows from. PySide6 already ships a
complete Qt, it is imported before this module, and its copy wins in the
process regardless of what the module linked against. A wheel that vendored a
second Qt would reproduce, on every user's machine, the crash the version check
above exists to prevent. So the wheel holds the extension module and the
package `__init__.py`, and nothing else -- Qt, libpyside6 and libshiboken6 all
resolve out of site-packages at import time.

What that costs is an exact pin. `pyproject.toml` declares
`PySide6==6.10.3`, not `>=`, because neither Qt nor Shiboken promises binary
compatibility across a minor release and the failure mode for a mismatch is a
crash rather than a resolver error. A new PySide6 means a new wheel.

```bash
scripts/wheel.sh              # Linux, macOS
scripts\wheel.bat             # Windows
```

Both build the wheel and then verify it: a separate virtualenv, a working
directory outside the repository, and an import with nothing Qt-related on
`PATH`. `--no-test` skips the second half; `--isolated` lets pip resolve its
own build dependencies rather than using the ones this environment already has.

### How the module finds Qt

Two mechanisms, because the platforms have nothing in common here.

On Linux and macOS it is an rpath, baked into the binary at link time and
relative to the binary's own location -- `$ORIGIN` and `@loader_path`
respectively. The layout on the far side is predictable, because pip puts
everything in one place:

```
site-packages/
|-- anytermqt/<module>.so     <-- $ORIGIN / @loader_path is this directory
|-- PySide6/                  libpyside6, and Qt6*.dll on Windows
|   `-- Qt/lib/               libQt6Core.so.6 / QtCore.framework
`-- shiboken6/                libshiboken6
```

`INSTALL_RPATH_USE_LINK_PATH` is off in `bindings/CMakeLists.txt`, and that one
is load-bearing. Left on, CMake appends the link directories -- this machine's
Homebrew or aqtinstall Qt -- to the installed rpath as well. The wheel then
imports perfectly on the machine that built it and fails everywhere else, which
is the worst available outcome: a broken artifact that passes its own test.

Windows has no rpath. The DLL search path is a runtime property of the process,
so `python/anytermqt/__init__.py` sets it with `os.add_dll_directory` before
the `.pyd` loads, and imports PySide6 first so that its Qt is the one in the
process. That file is not a convenience wrapper; without it the import fails on
any machine that does not happen to have Qt on `PATH`.

### One wheel per platform, not per Python version

Shiboken's CMake package attaches this to anything linking it:

```
INTERFACE_COMPILE_DEFINITIONS "Py_LIMITED_API=0x03090000;NDEBUG"
```

So the module is already compiled against the limited API and already links
`python3.dll` rather than a versioned one. Only the wheel tag needed telling:

```toml
[tool.scikit-build]
wheel.py-api = "cp39"
```

That turns a `cp310-cp310-win_amd64` wheel, which pip refuses to install on
3.12, into `cp39-abi3-win_amd64`, which installs on 3.9 and later. Confirmed by
building on 3.10 and running on 3.12.

The tag is a claim, not a guarantee -- it is worth checking that the module
really does link the limited-API library before trusting it:

```bat
dumpbin /dependents build\wheel-cp39-abi3-win_amd64\bindings\anytermqt.pyd | findstr /i python
```

`python3.dll` is correct. A versioned `python310.dll` means the tag is lying
and the wheel will install on any 3.9+ and fail at import on all but one of
them.

### Verified

Built on Windows against Python 3.10, installed on a different machine running
Python 3.12 with no Qt, no Visual Studio and no source tree, driving cmd.exe
through ConPTY with selection working.

Linux and macOS wheels are not yet built. The rpath they depend on is a
different mechanism from the one Windows exercised, so treat that as untested
rather than as following from the above.

### When it goes wrong

**`is not a supported wheel on this platform`** -- the tag does not match the
interpreter. Without `wheel.py-api` the wheel is built for exactly the Python
that built it. `python -c "import sysconfig;print(sysconfig.get_platform())"`
and the tag in the filename should agree.

**A wheel that builds but contains no module.** An empty wheel is a valid
wheel: if the `install()` rules in `bindings/CMakeLists.txt` did not run, or
`wheel.packages` names a directory that does not exist, nothing reports an
error. scikit-build-core skips a missing package path silently. This is what
the contents check in `scripts/_wheelcheck.py` is for -- run it against any
wheel you did not build through the scripts.

**`no such option: -C`** -- pip older than 23.1, which is what ships with a
good many Python 3.10 installs. The scripts pass Qt through the
`CMAKE_PREFIX_PATH` environment variable rather than `--config-settings` for
this reason; CMake reads it when the cache variable is unset, and
scikit-build-core runs cmake as a child process that inherits it.

**An import that works in the checkout and fails outside it.** On Linux and
macOS that is the rpath. Read it back off the installed module:

```bash
objdump -x <module>.so | grep -i runpath        # Linux
otool -l <module>.so | grep -A2 LC_RPATH        # macOS
```

It should be relative, and should not name the machine that built it.

---

## Running the demo application

| Option | Meaning | Default |
|---|---|---|
| `-s`, `--shell <path>` | Program to run | `/bin/bash`, `cmd.exe` on Windows |
| `-b`, `--scrollback <rows>` | Scrollback rows retained | 5000 |

## Tests

```bash
./build/pyte/tests/pyte_tests            # emulation core
./build/qtpyte/tests/qtpyte_tests        # widget
ctest --test-dir build                   # both
```

Add `QT_QPA_PLATFORM=offscreen` for the widget tests on a headless machine.
Some of them spawn a POSIX shell and do not yet run on Windows.

## Build options

| Option | Default | Effect |
|---|---|---|
| `BUILD_WIDGET` | `ON` | Build `qtpyte/`. Off builds the core alone, no Qt needed. |
| `BUILD_BINDINGS` | `OFF` | Build `bindings/`. Requires `BUILD_WIDGET`. |
| `PYTE_BUILD_TESTS` | `ON` | Core test suite |
| `PYTE_BUILD_TOOLS` | `ON` | The `ptdump` CLI |
| `QTPYTE_BUILD_TESTS` | `ON` | Widget test suite |
| `QTPYTE_BUILD_APP` | `ON` | The `qtpyte-term` demo |
| `SHIBOKEN_SITELIB` | auto | site-packages holding PySide6 and Shiboken |

`SHIBOKEN_SITELIB` is detected by checking both the interpreter's `purelib`
and its user site directory for a `shiboken6_generator`. A machine-wide Python
whose install directory is not writable — `C:\Program Files\Python310` is the
usual case — makes pip fall back to a `--user` install that lands in the
second while `sysconfig` still reports the first.

`qtpyte/` can also be configured standalone against a core elsewhere, with
`-DQTPYTE_PYTE_DIR=<path>`.

---

## Troubleshooting

### Configure

**`Could not find a configuration file for package "Qt6" ... version: 6.x.x
(64bit)`** — the compiler and Qt disagree on *architecture*, despite the
message naming a version. You are in the x86 Native Tools prompt; the 64-bit
one reports `Hostx64/x64/cl.exe` when detecting the compiler.

**The same error survives switching prompts** — CMake caches the detected
compiler. `rmdir /s /q build`, then configure again.

**`'cmake' is not recognized`** — not in a Native Tools prompt, or the C++
workload is missing. Check with `where cmake`, `where ninja`, `where cl`.

**`Manually-specified variables were not used by the project: BUILD_BINDINGS`**
— the option is not in the root `CMakeLists.txt`. CMake accepts unknown `-D`
flags silently and only warns at the end.

**`Could not find a package configuration file provided by "Shiboken6Tools"`**
— a `--user` pip install. Pass `-DSHIBOKEN_SITELIB=<path>`.

**`get_property could not find TARGET Qt6::Core`** — Qt's imported targets are
directory-scoped, so `bindings/CMakeLists.txt` needs its own
`find_package(Qt6 ... COMPONENTS Core Gui Widgets)` even though `qtpyte/`
already has one.

### Build

**`'shiboken_path-NOTFOUND' is not recognized as an internal or external
command`** — a Debug build. `shiboken_generator_create_binding` reads
`IMPORTED_LOCATION_DEBUG` from the imported generator target when
`CMAKE_BUILD_TYPE` is `Debug`, and `IMPORTED_LOCATION_RELEASE` otherwise; the
wheel ships only a release target file, so in a Debug build the property is
unset and `get_target_property` yields that string, which goes into the
generation rule verbatim. Configure succeeds and the build fails much later
with what reads like a typo.

It looks like a missing executable and is not: the same message appears
whatever the generator's real location. Check `CMAKE_BUILD_TYPE` before
searching site-packages. `bindings/CMakeLists.txt` now copies the release
location onto the debug property, which is correct — the generator is a build
tool, and the configuration it was compiled in has no bearing on what it
emits.

**`ninja: no work to do` and no module appears** — the build directory was
configured earlier with `-DBUILD_BINDINGS=OFF` (or without the flag). A script
that configures only when `CMakeCache.txt` is absent will skip the configure,
find nothing to do, and report success having built nothing. CMake is
idempotent and cheap; configure every time rather than guarding on the cache
file. Check with `grep BUILD_BINDINGS build/CMakeCache.txt`.

**Ninja re-runs CMake forever, edge count climbing** — a source file carries a
timestamp in the future, so the regenerated `build.ninja` is never new enough.
`ninja -C build -d explain -n` names the file. Fix with:

```bash
find . -newermt "now" -type f -exec touch {} +
```

```bat
powershell -Command "Get-ChildItem -Recurse -File | Where-Object { $_.LastWriteTime -gt (Get-Date) } | ForEach-Object { $_.LastWriteTime = Get-Date }"
```

**`constexpr variable 'HeaderOffset' must be initialized by a constant
expression`** in `qplugin.h` — `anytermqt_global.h` is including a Qt module
umbrella. Include only the specific headers the bound API mentions.

**`fatal error: 'qtpyte/palette.h' file not found`** during generation —
`shiboken_generator_create_binding` does not inherit include directories from
`LIBRARY_TARGET`. They go through `SHIBOKEN_EXTRA_OPTIONS` as `-I` flags.

**Missing generated source** — a filename in `GENERATED_SOURCES` does not match
what Shiboken produced. List `build/bindings/anytermqt/` for the real names.

**`warning C4458: declaration of 'data' hides class member`** — benign.
`TerminalWidget::feed(const QByteArray &data)` shadows an old protected
`QWidget::data` member. MSVC-only, under `/W4`.

**Warnings about `QCborStreamReader` template base classes** — benign
generator noise from Qt headers the bindings never touch. Two of them, always,
alongside a "34 known issues" count.

**`Unable to locate Clang's built-in include directory`** — benign on Windows,
where libclang finds the MSVC headers anyway. Fatal on Linux, where the next
line is `fatal error: 'stddef.h' file not found` from inside Qt's own
`qtypes.h` and the build stops. Read the second line before dismissing the
first: on Linux this means clang is not installed, not that Qt is broken.
`sudo apt install clang llvm libclang-dev`, or set `LLVM_INSTALL_DIR`. See
*Linux prerequisites*.

**`undefined reference to 'ucnv_open_73'`** and `libicui18n.so.73 ... not
found`, linking `qtpyte-term` — an aqtinstall Qt without its ICU. `--archives
qtbase` omits it. See *Linux prerequisites*.

**A change works in `qtpyte-term` and is missing through the Python module** —
a stale generated wrapper, not a binding bug. The widget's `.cpp` files rebuild
into `qtpyte_core` and the app sees the change at once; Shiboken does not
re-run, so the module keeps a wrapper generated from the older header. The C++
looks correct because it is.

`shiboken_generator_create_binding` declares its rule with `DEPENDS` on the
typesystem and the global header only, plus `IMPLICIT_DEPENDS CXX` for the
transitive includes — and `IMPLICIT_DEPENDS` is honoured by the Makefile
generator and ignored, silently, by Ninja and Visual Studio. So editing
`qtpyte/include/qtpyte/terminalwidget.h` regenerates nothing.

`bindings/CMakeLists.txt` names the widget headers explicitly with
`add_custom_command(... APPEND)` to close it. Add new public headers to that
list. To force it once: `rm -rf build/bindings/anytermqt` and rebuild, or
`scripts/regen-bindings.sh`.

`test_terminal.py` prints the module path, its build time and the API the
wrapper actually exposes at startup, which answers the same question without a
rebuild.

### Run

**`ModuleNotFoundError: No module named 'anytermqt'`** — `PYTHONPATH` is right
but the module was never built. Check for `anytermqt*.pyd` or
`anytermqt*.so` under `build/bindings/`, and point `PYTHONPATH` at whichever
directory actually holds it.

**Qt DLL errors on launch** — the Qt `bin` directory is not on `PATH`.

**`Could not find the Qt platform plugin "offscreen "`** — note the trailing
space. `set QT_QPA_PLATFORM=offscreen && ...` in `cmd` includes everything up
to the `&&` in the value. Quote the assignment: `set "QT_QPA_PLATFORM=offscreen"`.

**`QFontDatabase: Cannot find font directory ...`** and **`This plugin does not
support propagateSizeHints()`** — benign, and specific to the offscreen
platform plugin. Qt no longer ships fonts in the wheels, and the offscreen
plugin implements only part of the platform interface.

**`'aqt'` or `'pyside6-config'` is not recognized** — a `--user` install puts
console scripts outside `PATH`. Use `python -m aqt` for aqt. `pyside6-config`
is a script rather than a module, so `-m` will not reach it; ask Python
directly instead:

```bash
python -c "from PySide6.QtCore import qVersion; print(qVersion())"
```

**`aqt` lists a Qt version but `install-qt` cannot find its XML data** — that
release is not on the mirrors yet. Walk down to the previous patch release;
the version list and the download repository are not the same source.

**Importing the module crashes rather than erroring** — PySide6 and the module
were built against different Qt versions. Compare `qVersion()` against the Qt
you configured with.