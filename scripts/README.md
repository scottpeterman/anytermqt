# scripts/env.sh
#
# Sourced by the other scripts. The counterpart of env.bat, but the checks are
# not a translation of it, because the two platforms fail differently.
#
# On Windows the trouble is the prompt and where pip put things. Here it is Qt
# version: the distro or Homebrew Qt is whatever the packager shipped, and the
# bindings need it to match PySide6 exactly. So this looks Qt up rather than
# assuming a path, and treats "found a Qt" and "found the right Qt" as two
# different questions.
#
# Sourced, not run -- the variables have to survive into the caller:
#     . "$(dirname "$0")/env.sh"
#
# Override anything by exporting it first:
#     QT_DIR=$HOME/Qt/6.10.3/gcc_64 scripts/build.sh
#
# Sets: REPO_ROOT BUILD_DIR QT_DIR QT_VERSION PYTHON PYSIDE_QT SHIBOKEN_SITELIB
# QT_DIR may legitimately be empty on Linux: a distro Qt installs its CMake
# package files where CMake already looks, and naming a prefix would only be
# a chance to name the wrong one.

env_die() { echo "[env] $*" >&2; return 1; }
env_note() { echo "[env] $*"; }

REPO_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]:-$0}")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-$REPO_ROOT/build}

case "$(uname -s)" in
    Darwin) PLATFORM=macos ;;
    Linux)  PLATFORM=linux ;;
    *)      PLATFORM=unknown ;;
esac

PYTHON=${PYTHON:-python3}

# --- 1. the tools -------------------------------------------------------------

command -v cmake >/dev/null 2>&1 || env_die "cmake not found." || return 1

if ! command -v ninja >/dev/null 2>&1; then
    if [ "$PLATFORM" = macos ]; then
        env_die "ninja not found.  brew install ninja" || return 1
    else
        env_die "ninja not found.  apt install ninja-build" || return 1
    fi
fi

command -v c++ >/dev/null 2>&1 || command -v g++ >/dev/null 2>&1 \
    || env_die "no C++ compiler on PATH." || return 1

# --- 2. which Qt ---------------------------------------------------------------
#
# In order: an explicit QT_DIR, a qmake on PATH, Homebrew. The last is macOS
# only and worth having, because a Homebrew Qt does not put qmake on PATH --
# it is keg-only, and the usual symptom is CMake reporting no Qt6 on a machine
# where Qt is plainly installed.

# An already-configured build directory is asked before the environment is.
# The cache records the Qt the module was actually built against, and that is
# the one the module has to load -- so run.sh and run-py.sh follow the build
# rather than whatever a later shell happens to have on PATH. Passing QT_DIR
# to one command and not the next was silently giving two different answers.
if [ -z "${QT_DIR:-}" ] && [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    cached_qt=$(sed -n 's/^CMAKE_PREFIX_PATH:[A-Z]*=//p' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | head -1)
    if [ -n "$cached_qt" ]; then
        QT_DIR=${cached_qt%%;*}
        env_note "Qt from build cache: $QT_DIR"
    fi
fi

qt_qmake=""
if [ -n "${QT_DIR:-}" ]; then
    for candidate in "$QT_DIR/bin/qmake6" "$QT_DIR/bin/qmake" \
                     "$QT_DIR/libexec/qmake6" "$QT_DIR/libexec/qmake"; do
        if [ -x "$candidate" ]; then qt_qmake=$candidate; break; fi
    done
    if [ -z "$qt_qmake" ]; then
        # An aqtinstall prefix is .../<version>/<arch>, so the version is the
        # directory above. Worth recovering: without it the mismatch check
        # below has nothing to compare and silently does not run, which is
        # worse than a wrong answer because it looks like a pass.
        guessed=$(basename -- "$(dirname -- "$QT_DIR")")
        case "$guessed" in
            [0-9]*.[0-9]*) QT_VERSION_GUESS=$guessed ;;
        esac
        env_note "QT_DIR has no qmake in bin/ or libexec/ -- trusting the path"
        [ -n "${QT_VERSION_GUESS:-}" ] && env_note "  version taken from the path: $QT_VERSION_GUESS"
    fi
elif command -v qmake6 >/dev/null 2>&1; then
    qt_qmake=$(command -v qmake6)
elif command -v qmake >/dev/null 2>&1; then
    qt_qmake=$(command -v qmake)
elif [ "$PLATFORM" = macos ] && command -v brew >/dev/null 2>&1; then
    brew_qt=$(brew --prefix qt6 2>/dev/null || brew --prefix qt 2>/dev/null || true)
    if [ -n "$brew_qt" ] && [ -x "$brew_qt/bin/qmake6" ]; then
        qt_qmake=$brew_qt/bin/qmake6
    elif [ -n "$brew_qt" ] && [ -x "$brew_qt/bin/qmake" ]; then
        qt_qmake=$brew_qt/bin/qmake
    fi
fi

QT_VERSION=${QT_VERSION_GUESS:-}
if [ -n "$qt_qmake" ]; then
    QT_VERSION=$("$qt_qmake" -query QT_VERSION 2>/dev/null || true)
    [ -n "${QT_DIR:-}" ] || QT_DIR=$("$qt_qmake" -query QT_INSTALL_PREFIX 2>/dev/null || true)
fi

if [ -z "$QT_VERSION" ]; then
    # Not fatal on Linux: a qt6-base-dev install has no qmake6 on PATH under
    # some packagings and CMake finds it regardless. Fatal nowhere, since the
    # C++ build is the common case and CMake's own error is clearer than a
    # guess made here.
    env_note "no qmake found -- leaving Qt discovery to CMake"
    env_note "  ubuntu: apt install qt6-base-dev"
    env_note "  macos:  brew install qt"
else
    env_note "Qt         ${QT_VERSION}${QT_DIR:+  ($QT_DIR)}"
fi

# --- 3. bindings only ----------------------------------------------------------
#
# Everything below concerns the Python module. The C++ build does not need any
# of it, so the scripts that only build C++ set QTPYTE_NEED_PYTHON=0 and stop
# here -- no reason to demand PySide6 from someone building a widget.

if [ "${QTPYTE_NEED_PYTHON:-1}" = "0" ]; then
    env_note "build      $BUILD_DIR"
    return 0
fi

command -v "$PYTHON" >/dev/null 2>&1 || env_die "$PYTHON not found. Set PYTHON=." || return 1

# Ask the package where it is, rather than asking Python where packages go: a
# pip install --user lands in the user site directory while sysconfig still
# reports the machine-wide one. On Linux this is the common case, because the
# system Python's site-packages is not writable and pip falls back silently.
SHIBOKEN_SITELIB=$("$PYTHON" -c 'import os,shiboken6_generator as g;print(os.path.dirname(os.path.dirname(g.__file__)))' 2>/dev/null || true)

pip_hint="$PYTHON -m pip install --user pyside6==${QT_VERSION:-6.10.3} shiboken6==${QT_VERSION:-6.10.3} shiboken6_generator==${QT_VERSION:-6.10.3}"

if [ -z "$SHIBOKEN_SITELIB" ]; then
    env_die "shiboken6_generator is not importable by $PYTHON."
    env_note "  $pip_hint"
    if [ "$PLATFORM" = linux ]; then
        env_note "  A distro python3-pyside6 package is not enough -- it ships"
        env_note "  the runtime and not the generator."
    fi
    return 1
fi

PYSIDE_QT=$("$PYTHON" -c 'from PySide6.QtCore import qVersion;print(qVersion())' 2>/dev/null || true)
if [ -z "$PYSIDE_QT" ]; then
    env_die "PySide6 is not importable by $PYTHON."
    env_note "  $pip_hint"
    return 1
fi

# --- 4. one Qt in the process, not two -----------------------------------------
#
# The module links the Qt found above; PySide6 loads its own and wins at import
# time. Two different builds in one process is a crash rather than an error, so
# it is worth the check here.
#
# This is where Linux and macOS diverge from Windows in practice. On Windows Qt
# comes from aqtinstall at a version you chose. Here it comes from a packager,
# and Ubuntu 24.04 ships 6.4 while PySide6 is on 6.10 -- so the mismatch is the
# default state rather than an accident, and the fix is a second Qt rather than
# a pip pin.

if [ -n "$QT_VERSION" ] && [ "$PYSIDE_QT" != "$QT_VERSION" ]; then
    env_die "Qt version mismatch: building against $QT_VERSION, PySide6 carries $PYSIDE_QT."
    env_note "  These must match or importing the module will crash rather than error."
    # Only offer the downgrade when it could actually work. bindings/
    # CMakeLists.txt uses shiboken_generator_create_binding, which no 6.2 or
    # 6.4 Shiboken has -- so on a distro Qt the only direction is up, and
    # suggesting a pin sends you to spend an afternoon on a dead end.
    case "$QT_VERSION" in
        6.[0-9].*|6.10.*)
            qt_major_minor=${QT_VERSION%.*}
            case "$qt_major_minor" in
                6.2|6.3|6.4|6.5|6.6|6.7|6.8|6.9)
                    env_note "  Qt $QT_VERSION is too old for these bindings regardless -- the"
                    env_note "  generator macro they use ships with 6.10 and later. Install a"
                    env_note "  matching Qt alongside it:"
                    ;;
                *)
                    env_note "  Either pin PySide6 to the Qt you have:"
                    env_note "    $PYTHON -m pip install pyside6==$QT_VERSION shiboken6==$QT_VERSION shiboken6_generator==$QT_VERSION"
                    env_note "  or install a matching Qt and point at it:"
                    ;;
            esac
            ;;
    esac
    if [ "$PLATFORM" = macos ]; then
        env_note "    $PYTHON -m aqt install-qt mac desktop $PYSIDE_QT clang_64 --archives qtbase -O \$HOME/Qt"
        env_note "    QT_DIR=\$HOME/Qt/$PYSIDE_QT/macos scripts/build.sh"
    else
        env_note "    $PYTHON -m aqt install-qt linux desktop $PYSIDE_QT gcc_64 --archives qtbase -O \$HOME/Qt"
        env_note "    QT_DIR=\$HOME/Qt/$PYSIDE_QT/gcc_64 scripts/build.sh"
    fi
    env_note "  Set QT_SKIP_VERSION_CHECK=1 to build anyway."
    [ -n "${QT_SKIP_VERSION_CHECK:-}" ] || return 1
    env_note "QT_SKIP_VERSION_CHECK is set -- continuing."
fi

# --- 5. one architecture, on Apple Silicon -------------------------------------
#
# No Windows equivalent. An x86_64 Python under Rosetta with an arm64 Qt links
# and then fails to load, and the message names a file rather than a slice.

if [ "$PLATFORM" = macos ]; then
    py_arch=$("$PYTHON" -c 'import platform;print(platform.machine())' 2>/dev/null || true)
    host_arch=$(uname -m)
    if [ -n "$py_arch" ] && [ "$py_arch" != "$host_arch" ]; then
        env_die "architecture mismatch: $PYTHON is $py_arch on a $host_arch host."
        env_note "  That is a Rosetta Python. Qt and the module will be $host_arch"
        env_note "  and the import will fail to load rather than reporting why."
        env_note "  Use a native Python, or set PYTHON to one."
        return 1
    fi
fi

env_note "PySide6 Qt $PYSIDE_QT"
env_note "sitelib    $SHIBOKEN_SITELIB"
env_note "build      $BUILD_DIR"
return 0