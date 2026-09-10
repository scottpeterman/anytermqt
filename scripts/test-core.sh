#!/usr/bin/env bash
# scripts/test-core.sh
#
# Builds and tests the core on its own, with no Qt anywhere near it, in a
# separate build directory so it cannot borrow anything the main build already
# configured.
#
# This is the check worth running before a commit. It is what stops the core
# from quietly growing a dependency on the widget -- a dependency that the
# ordinary build would never reveal, because there the widget is always
# present.
set -euo pipefail

export QTPYTE_NEED_PYTHON=0
. "$(cd -- "$(dirname -- "$0")" && pwd)/env.sh"

core_dir=$REPO_ROOT/build-core

cmake -S "$REPO_ROOT" -B "$core_dir" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DBUILD_WIDGET=OFF
cmake --build "$core_dir"
ctest --test-dir "$core_dir" --output-on-failure

echo "[test-core] OK"
