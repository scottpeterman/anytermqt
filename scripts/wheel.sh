#!/usr/bin/env bash
# scripts/wheel.sh
#
# Builds a wheel, then installs it into a throwaway virtualenv and imports it
# from a directory with no source tree in sight.
#
# The second half is the point. A wheel that imports in the checkout proves
# almost nothing -- the build tree is still on disk, the rpath from the link
# line still resolves, and Qt is still installed. The failure this catches is
# a module that works only on the machine that built it.
#
#   scripts/wheel.sh              build, then verify
#   scripts/wheel.sh --no-test    build only
#   scripts/wheel.sh --isolated   let pip resolve its own build dependencies
#   scripts/wheel.sh --no-repair  Linux: skip the manylinux retag, keep
#                                 linux_x86_64 -- installable here, not
#                                 distributable
set -euo pipefail

here=$(cd -- "$(dirname -- "$0")" && pwd)

run_test=1
isolated=0
repair=1
for arg in "$@"; do
    case "$arg" in
        --no-test)   run_test=0 ;;
        --isolated)  isolated=1 ;;
        --no-repair) repair=0 ;;
        *) echo "unknown argument: $arg" >&2; exit 2 ;;
    esac
done

. "$here/env.sh"

DIST_DIR=${DIST_DIR:-$REPO_ROOT/dist}

# --- 1. the pin in pyproject.toml is the Qt we are building against -----------
#
# The wheel declares an exact PySide6 version, and the module links whichever
# Qt this machine has. If those disagree the wheel is wrong at the moment it is
# created: it will install cleanly for someone, pull the PySide6 it names, and
# crash on import against a Qt it was never built for.
#
# Checked here rather than trusted, because the pin is static metadata and the
# Qt is not -- and because these two drift apart exactly when you build on a
# second machine, which is when it is hardest to notice.

# Parsed by scripts/_wheelcheck.py rather than here, so this and wheel.bat
# read the pin the same way rather than through two implementations.
if ! pin=$("$PYTHON" "$here/_wheelcheck.py" pin "$REPO_ROOT/pyproject.toml"); then
    echo "[wheel] Could not read the PySide6 pin from pyproject.toml." >&2
    exit 1
fi

if [ "$pin" != "$PYSIDE_QT" ]; then
    echo "[wheel] pyproject.toml pins PySide6==$pin; this environment has $PYSIDE_QT." >&2
    echo "[wheel] A wheel built here would name a PySide6 it was not built against." >&2
    echo "[wheel] Either align the environment:" >&2
    echo "[wheel]   $PYTHON -m pip install pyside6==$pin shiboken6==$pin shiboken6_generator==$pin" >&2
    echo "[wheel] or change the pin in pyproject.toml to $PYSIDE_QT -- both the" >&2
    echo "[wheel] dependencies line and the build-system requires block." >&2
    exit 1
fi

echo "[wheel] PySide6 pin $pin matches this environment"

# --- 2. build ------------------------------------------------------------------
#
# --no-build-isolation by default: this interpreter already has the pinned
# PySide6, Shiboken and generator, verified above and by env.sh. Isolation
# would download all three again -- several hundred megabytes -- into a
# temporary environment, for no gain and one more place for the version to
# differ. --isolated is there for reproducing what a CI runner or an end user
# building from an sdist would get.

if [ "$isolated" = 0 ]; then
    if ! "$PYTHON" -c "import scikit_build_core" >/dev/null 2>&1; then
        echo "[wheel] scikit-build-core is not installed in $PYTHON." >&2
        echo "[wheel]   $PYTHON -m pip install scikit-build-core" >&2
        echo "[wheel] Or pass --isolated to let pip fetch it into a build env." >&2
        exit 1
    fi
    isolation_args=(--no-build-isolation)
else
    isolation_args=()
fi

# --no-deps or pip downloads a PySide6 wheel into dist/ alongside ours.
build_args=(--no-deps -w "$DIST_DIR" "${isolation_args[@]}")

# The one per-machine setting, handed over through the environment rather than
# a pip --config-settings. CMake reads CMAKE_PREFIX_PATH from the environment
# when the cache variable is not set, and scikit-build-core runs cmake as a
# child process that inherits it -- so this works on any pip. The -C flag does
# not: it arrived in pip 23.1.
#
# Left alone when QT_DIR is empty, which is the ordinary case on a distro Qt.
# Naming an empty prefix is worse than naming none -- same reasoning as
# configure.sh.
if [ -n "${QT_DIR:-}" ]; then
    export CMAKE_PREFIX_PATH="$QT_DIR${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
fi

echo "[wheel] Building into $DIST_DIR"
rm -f "$DIST_DIR"/anytermqt-*.whl
"$PYTHON" -m pip wheel "$REPO_ROOT" "${build_args[@]}"

wheel=$(ls -t "$DIST_DIR"/anytermqt-*.whl 2>/dev/null | head -1)
if [ -z "$wheel" ]; then
    echo "[wheel] pip reported success but no anytermqt wheel is in $DIST_DIR." >&2
    exit 1
fi
echo "[wheel] Built $(basename -- "$wheel")"

if ! "$PYTHON" "$here/_wheelcheck.py" contents "$wheel"; then
    exit 1
fi

# --- 3. Linux only: a platform tag that means something -------------------------
#
# scikit-build-core tags this linux_x86_64, which is not a real platform tag.
# PyPI rejects it outright, and it carries no glibc floor, so it says nothing
# about where the wheel will load. auditwheel replaces it with a manylinux tag
# derived from the symbol versions the module actually references.
#
# The awkward part is that auditwheel's other job is vendoring, and this wheel
# exists specifically not to vendor. Left to itself it walks the whole
# transitive Qt graph -- libQt6DBus, libxkbcommon, libsystemd, libpng, forty
# libraries deep -- and grafts a second Qt into the wheel, which is the exact
# crash the no-Qt decision prevents. It also computes the tag from that graph,
# so the tag comes out both wrong and far stricter than the module warrants.
#
# So every dependency auditwheel would otherwise graft is excluded, leaving it
# only the tag to compute. The list is derived in _wheelcheck.py by subtracting
# the manylinux allowlist from the module's own DT_NEEDED entries, rather than
# named here: a hand-written Qt/PySide6/Shiboken pattern looks complete and is
# not, because Qt6Gui drags in libOpenGL.so.0, which matches none of those
# names and is not allowlisted.
#
# The exclusions are then checked rather than trusted. One missing entry is not
# a warning and not a failure -- auditwheel reports success and writes a wheel
# with thirty libraries in it, and the tag is the only visible difference.

if [ "$PLATFORM" = linux ] && [ "$repair" = 1 ]; then
    # Found through $PYTHON, not through PATH. The interpreter resolved in
    # env.sh is frequently a venv that this shell has not activated -- that is
    # the whole reason the resolution exists -- so its bin/ is not on PATH and
    # a pip install into it leaves command -v seeing nothing. Checking PATH
    # here reported auditwheel missing immediately after it was installed.
    if ! "$PYTHON" -c 'import auditwheel' >/dev/null 2>&1; then
        echo "[wheel] auditwheel is not importable by $PYTHON. The wheel is" >&2
        echo "[wheel] tagged linux_x86_64, which installs on this machine and" >&2
        echo "[wheel] is not distributable." >&2
        echo "[wheel]   $PYTHON -m pip install auditwheel patchelf" >&2
        echo "[wheel] pip rather than apt for patchelf: auditwheel wants >= 0.14.5" >&2
        echo "[wheel] and the distro package is older than that on several LTS" >&2
        echo "[wheel] releases, which fails at repair rather than at install." >&2
        echo "[wheel] Or pass --no-repair to keep the untagged wheel deliberately." >&2
        exit 1
    fi

    # patchelf is the one thing auditwheel does not import: it shells out and
    # looks on PATH. So the interpreter's own bin/ goes on the front, which is
    # where pip put it, and ahead of any distro copy that may be below the
    # 0.14.5 auditwheel requires.
    PATH="$(dirname -- "$(command -v "$PYTHON" || echo "$PYTHON")"):$PATH"
    export PATH

    if ! command -v patchelf >/dev/null 2>&1; then
        echo "[wheel] patchelf not found, and auditwheel shells out to it." >&2
        echo "[wheel]   $PYTHON -m pip install patchelf" >&2
        echo "[wheel] Or pass --no-repair." >&2
        exit 1
    fi

    excludes=()
    excluded_names=()
    while read -r soname; do
        [ -n "$soname" ] || continue
        excludes+=(--exclude "$soname")
        excluded_names+=("$soname")
    done < <("$PYTHON" "$here/_wheelcheck.py" excludes "$wheel")

    if [ ${#excludes[@]} -eq 0 ]; then
        echo "[wheel] The module depends on nothing outside the manylinux" >&2
        echo "[wheel] allowlist -- no Qt, no PySide6, no Shiboken. That is not a" >&2
        echo "[wheel] wheel this project can produce; check that BUILD_BINDINGS" >&2
        echo "[wheel] was ON and the module is the real one." >&2
        exit 1
    fi

    echo "[wheel] Excluding from the graft: ${excluded_names[*]}"

    repaired_dir=$DIST_DIR/manylinux
    rm -rf "$repaired_dir"
    if ! "$PYTHON" -m auditwheel repair -w "$repaired_dir" "${excludes[@]}" "$wheel"; then
        echo "[wheel] auditwheel repair failed." >&2
        exit 1
    fi

    repaired=$(ls -t "$repaired_dir"/anytermqt-*.whl 2>/dev/null | head -1)
    if [ -z "$repaired" ]; then
        echo "[wheel] auditwheel reported success but wrote no wheel." >&2
        exit 1
    fi

    if ! "$PYTHON" "$here/_wheelcheck.py" grafted "$repaired"; then
        exit 1
    fi

    # From here on the repaired wheel is the artifact. Verifying the original
    # would test something that is no longer what ships.
    wheel=$repaired
    echo "[wheel] Retagged $(basename -- "$wheel")"
elif [ "$PLATFORM" = linux ]; then
    echo "[wheel] --no-repair: left as linux_x86_64, not distributable"
fi

[ "$run_test" = 1 ] || { echo "[wheel] OK (not verified -- --no-test)"; exit 0; }

# --- 4. verify it somewhere else ------------------------------------------------
#
# A separate interpreter, so nothing the development environment has can stand
# in for something the wheel should have carried, and a working directory
# outside the repository, so no part of the checkout is importable.

venv=${WHEEL_TEST_VENV:-$REPO_ROOT/build/wheel-test-venv}

if [ ! -x "$venv/bin/python" ]; then
    echo "[wheel] Creating test environment at $venv"
    "$PYTHON" -m venv "$venv"
fi

echo "[wheel] Installing the wheel (PySide6 $pin comes with it -- this is slow once)"
"$venv/bin/python" -m pip install --quiet --upgrade pip
"$venv/bin/python" -m pip install --quiet --force-reinstall "$wheel"

tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT

# offscreen so this runs over ssh and in CI. The import itself needs no
# display -- it is dlopen and symbol resolution, which is what is being
# tested -- but constructing anything Qt later would.
if (cd "$tmp" && QT_QPA_PLATFORM=offscreen \
        "$venv/bin/python" "$here/_wheelcheck.py" verify); then
    echo "[wheel] OK"
else
    echo "[wheel] The wheel built but does not import outside the source tree." >&2
    echo "[wheel] On Linux and macOS that is almost always the rpath: check it with" >&2
    if [ "$(uname -s)" = Darwin ]; then
        echo "[wheel]   otool -l \$($venv/bin/python -c 'import anytermqt,os;print(anytermqt.anytermqt.__file__)') | grep -A2 LC_RPATH" >&2
    else
        echo "[wheel]   objdump -x \$($venv/bin/python -c 'import anytermqt,os;print(anytermqt.anytermqt.__file__)') | grep -i runpath" >&2
    fi
    echo "[wheel] It should be relative -- \$ORIGIN or @loader_path -- and not name" >&2
    echo "[wheel] this machine's Qt." >&2
    exit 1
fi