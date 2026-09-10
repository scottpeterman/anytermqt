#!/usr/bin/env bash
# scripts/configure.sh
#
# Configures a Release build. Run once; build.sh re-runs CMake by itself when a
# CMakeLists.txt changes.
#
# Release rather than Debug is a hard requirement only for the bindings --
# PySide6 ships release binaries, so a Debug extension module has nothing to
# link against. It is the default here for both so that the C++ you debug and
# the C++ Python loads are the same build.
#
#   scripts/configure.sh            with bindings
#   scripts/configure.sh --no-bindings   C++ only, no PySide6 needed
set -euo pipefail

bindings=ON
for arg in "$@"; do
    case "$arg" in
        --no-bindings) bindings=OFF ;;
        *) echo "unknown argument: $arg" >&2; exit 2 ;;
    esac
done

[ "$bindings" = ON ] || export QTPYTE_NEED_PYTHON=0
. "$(cd -- "$(dirname -- "$0")" && pwd)/env.sh"

args=(-S "$REPO_ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release)
args+=(-DBUILD_BINDINGS="$bindings")

# Only when we actually found one. Naming an empty prefix is worse than naming
# none: CMake stops looking in the places a distro Qt lives.
[ -n "${QT_DIR:-}" ] && args+=(-DCMAKE_PREFIX_PATH="$QT_DIR")
[ "$bindings" = ON ] && args+=(-DSHIBOKEN_SITELIB="$SHIBOKEN_SITELIB")

if ! cmake "${args[@]}"; then
    echo
    echo "[configure] Failed. If this ran before against a different Qt or"
    echo "[configure] compiler, CMake has cached the old one: scripts/clean.sh"
    echo "[configure] and configure again."
    exit 1
fi

echo "[configure] OK"
