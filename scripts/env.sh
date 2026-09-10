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

PYTHON=${PYTHON:-}

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

# --- 3a. which interpreter -----------------------------------------------------
#
# Same reasoning as the Qt lookup above, and the same failure it prevents. The
# module links whichever PySide6 this interpreter carries, so "a python3" and
# "the python3 the module was built against" are two different questions, and
# a plain python3 answers the wrong one.
#
# It shows up after a reboot rather than after a mistake: the Qt survives,
# because it is a path in CMakeCache.txt, and the interpreter does not, because
# it was a venv activated in a shell that is gone. The script then reports that
# shiboken6_generator is not importable, which is true of the system python3
# and irrelevant to the one that has been building this all along.
#
# In order: an explicit PYTHON, an activated venv, the build cache, a venv in
# the checkout. Each is checked for the generator before being accepted -- a
# candidate that cannot build the bindings is not a candidate, and adopting one
# silently only moves the confusion later.

py_has_generator() {
    [ -x "$1" ] && "$1" -c 'import shiboken6_generator' >/dev/null 2>&1
}

if [ -n "$PYTHON" ]; then
    py_source="PYTHON"
else
    py_candidates=()
    py_labels=()

    [ -n "${VIRTUAL_ENV:-}" ] && {
        py_candidates+=("$VIRTUAL_ENV/bin/python"); py_labels+=("active venv"); }

    if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
        # Two names, and the underscored one is the usual case. FindPython
        # does not cache Python_EXECUTABLE: it caches its own lookup as
        # _Python_EXECUTABLE:INTERNAL, and Python_EXECUTABLE appears as a
        # cache entry only when it was passed on the command line with -D.
        # Matching the documented name alone finds nothing, and finds it
        # quietly -- the candidate is simply absent from the list.
        cached_py=$(sed -n \
                    -e 's/^Python_EXECUTABLE:[A-Z]*=//p' \
                    -e 's/^_Python_EXECUTABLE:[A-Z]*=//p' \
                    "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | head -1)
        [ -n "$cached_py" ] && {
            py_candidates+=("$cached_py"); py_labels+=("build cache"); }
    fi

    py_candidates+=("$REPO_ROOT/.venv/bin/python"); py_labels+=("checkout venv")
    py_candidates+=("$(command -v python3 2>/dev/null || echo python3)")
    py_labels+=("PATH")

    PYTHON=""
    for i in "${!py_candidates[@]}"; do
        if py_has_generator "${py_candidates[$i]}"; then
            PYTHON=${py_candidates[$i]}
            py_source=${py_labels[$i]}
            break
        fi
    done

    # Nothing had it. Fall back to python3 so the diagnostics below run against
    # something real and name a concrete interpreter rather than an empty
    # string -- the message is the whole value of this path.
    if [ -z "$PYTHON" ]; then
        PYTHON=${py_candidates[-1]}
        py_source="PATH"
        env_note "no interpreter with shiboken6_generator found -- tried:"
        for i in "${!py_candidates[@]}"; do
            env_note "  ${py_labels[$i]}: ${py_candidates[$i]}"
        done
    fi
fi

# Exported so configure.sh, which build.sh runs as a child process and which
# sources this file again, resolves to the same interpreter rather than
# repeating the search and possibly answering differently.
export PYTHON

command -v "$PYTHON" >/dev/null 2>&1 || env_die "$PYTHON not found. Set PYTHON=." || return 1
env_note "python     $PYTHON  ($py_source)"

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
    # The common case by a wide margin, and it is not a missing install: a venv
    # that exists on disk and is not activated in this shell. Named here
    # because the message above is otherwise a true statement about the wrong
    # interpreter, and sends you to reinstall something you already have.
    if [ "$py_source" = PATH ]; then
        env_note "  If the bindings have built here before, the interpreter that"
        env_note "  did it is probably a venv this shell has not activated:"
        env_note "    source <venv>/bin/activate && scripts/build.sh"
        env_note "    PYTHON=<venv>/bin/python scripts/build.sh"
        env_note "  A venv at $REPO_ROOT/.venv is found without either."
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
        env_note "    # macOS needs no icu archive; Linux does -- see below"
        env_note "    QT_DIR=\$HOME/Qt/$PYSIDE_QT/macos scripts/build.sh"
    else
        env_note "    $PYTHON -m aqt install-qt linux desktop $PYSIDE_QT linux_gcc_64 --archives qtbase icu -O \$HOME/Qt"
        env_note "    QT_DIR=\$HOME/Qt/$PYSIDE_QT/gcc_64 scripts/build.sh"
    fi
    env_note "  Set QT_SKIP_VERSION_CHECK=1 to build anyway."
    [ -n "${QT_SKIP_VERSION_CHECK:-}" ] || return 1
    env_note "QT_SKIP_VERSION_CHECK is set -- continuing."
fi

# --- 5. the Qt install is complete -------------------------------------------
#
# Linux only, and only for a Qt that is not the system one. An aqtinstall Qt
# links against ICU 73 and ships it in lib/, but ICU is a separate archive
# inside the qtbase module -- so `--archives qtbase` downloads Qt and drops
# the ICU it depends on. The distro copy cannot stand in: the symbols are
# version-suffixed (ucnv_open_73), so a system ICU 70 gives undefined
# references rather than a version conflict, at link time, forty targets in.
#
# Re-run aqt with `--archives qtbase icu`, or without --archives at all.

qt_is_own_install=no
if [ "$PLATFORM" = linux ] && [ -n "${QT_DIR:-}" ] && [ "$QT_DIR" != /usr ]; then
    compgen -G "$QT_DIR/lib/libQt6Core.so*" >/dev/null 2>&1 && qt_is_own_install=yes
fi

if [ "$qt_is_own_install" = yes ] && ! compgen -G "$QT_DIR/lib/libicuuc.so*" >/dev/null 2>&1; then
    env_die "$QT_DIR has Qt but no ICU."
    env_note "  Qt 6 on Linux links against a specific ICU major version and"
    env_note "  ships it in lib/. The system copy cannot substitute -- the"
    env_note "  symbols carry the version, so this fails at link time with"
    env_note "  undefined references to things like ucnv_open_73."
    env_note "  ICU is a separate archive inside qtbase, so --archives qtbase"
    env_note "  omits it. Re-run the install with it named:"
    qt_parent=$(dirname -- "$(dirname -- "$QT_DIR")")
    env_note "    $PYTHON -m aqt install-qt linux desktop ${QT_VERSION:-6.10.3} linux_gcc_64 --archives qtbase icu -O $qt_parent"
    return 1
fi

# --- 6. libclang has its builtin headers ---------------------------------------
#
# No Windows counterpart. Shiboken parses the headers with libclang, and
# libclang needs its own builtin include directory -- where stddef.h and
# stdarg.h live. That directory comes with a clang installation, not with Qt
# and not with the shiboken6_generator wheel, which ships the generator binary
# and some Qt libraries and no clang resource headers at all.
#
# On Windows it finds MSVC's and this never comes up. On Linux and a Homebrew
# macOS the failure is a fatal "'stddef.h' file not found" from inside Qt's own
# qtypes.h, thirty-odd targets into a build that configured cleanly -- which
# reads like a broken Qt install rather than a missing tool.
#
# Shiboken looks at LLVM_INSTALL_DIR, then CLANG_INSTALL_DIR, then llvm-config.
# Checked in the same order here.

clang_ok=""
if [ -n "${LLVM_INSTALL_DIR:-}" ] || [ -n "${CLANG_INSTALL_DIR:-}" ]; then
    clang_ok=env
elif command -v llvm-config >/dev/null 2>&1; then
    clang_ok=llvm-config
elif [ "$PLATFORM" = macos ] && [ -d "$(xcrun --show-sdk-path 2>/dev/null)" ]; then
    # A full Xcode or the command line tools carry a clang, and shiboken finds
    # it without llvm-config on PATH.
    clang_ok=xcode
fi

if [ -z "$clang_ok" ]; then
    env_die "no llvm-config, and neither LLVM_INSTALL_DIR nor CLANG_INSTALL_DIR is set."
    env_note "  Shiboken parses headers with libclang and needs clang's builtin"
    env_note "  include directory. Without it the build fails much later with"
    env_note "  \"'stddef.h' file not found\" from inside a Qt header."
    if [ "$PLATFORM" = macos ]; then
        env_note "    xcode-select --install"
        env_note "    brew install llvm    # then: export LLVM_INSTALL_DIR=\$(brew --prefix llvm)"
    else
        env_note "    sudo apt install clang llvm libclang-dev"
        env_note "  llvm is the package that provides llvm-config; clang alone is"
        env_note "  often not enough. If it still is not found, name it:"
        env_note "    export LLVM_INSTALL_DIR=/usr/lib/llvm-\$(ls -d /usr/lib/llvm-* 2>/dev/null | sed 's|.*llvm-||' | sort -n | tail -1)"
    fi
    return 1
fi

env_note "libclang   via $clang_ok"

# --- 7. one architecture, on Apple Silicon -------------------------------------
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