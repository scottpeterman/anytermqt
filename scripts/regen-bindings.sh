#!/usr/bin/env bash
# scripts/regen-bindings.sh
#
# Throws away the generated wrapper sources and builds them again.
#
# The symptom this exists for: something works in the native binary and is
# missing or inert through Python. The widget's .cpp files rebuild into
# qtpyte_core and the app picks the change up at once, while the extension
# module keeps a wrapper Shiboken generated from an older header -- so the C++
# looks correct, because it is.
#
# bindings/CMakeLists.txt names the widget headers as explicit DEPENDS on the
# generation rule, which is what stops this happening. (The macro's own
# IMPLICIT_DEPENDS covers it only under the Makefile generator; Ninja and
# Visual Studio ignore it silently, and these scripts use Ninja.) So this
# should be rare. Reach for it after changing a header that is not on that
# DEPENDS list, or to rule the possibility out in thirty seconds rather than
# reason about it.
set -euo pipefail

. "$(cd -- "$(dirname -- "$0")" && pwd)/env.sh"

gen_dir=$BUILD_DIR/bindings/anytermqt

if [ -d "$gen_dir" ]; then
    echo "[regen] Removing $gen_dir"
    rm -rf -- "$gen_dir"
else
    echo "[regen] Nothing generated yet at $gen_dir"
fi

cmake --build "$BUILD_DIR" --target anytermqt
echo "[regen] OK"
