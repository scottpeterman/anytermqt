#!/usr/bin/env bash
# scripts/build.sh
#
# Builds everything: the core, the widget, the app and the extension module.
# Configures every time, so this is the only script needed from a clean
# checkout and also the only one needed after changing your mind about
# options.
#
#   scripts/build.sh                     everything
#   scripts/build.sh --no-bindings       C++ only, no PySide6 needed
#   scripts/build.sh --target anytermqt  anything else goes to cmake --build
set -euo pipefail

here=$(cd -- "$(dirname -- "$0")" && pwd)

bindings=ON
passthrough=()
for arg in "$@"; do
    case "$arg" in
        --no-bindings) bindings=OFF ;;
        *) passthrough+=("$arg") ;;
    esac
done

[ "$bindings" = ON ] || export QTPYTE_NEED_PYTHON=0
. "$here/env.sh"

# Every time, not only when CMakeCache.txt is absent. Guarding on the file
# means a directory configured with different options is never corrected: a
# build with --no-bindings leaves BUILD_BINDINGS=OFF in the cache, and the
# next run without it skips configure, finds nothing to do, and reports
# success having built no module at all. The configure costs a second or two
# and is idempotent. The guard cost an afternoon.
if [ "$bindings" = ON ]; then
    "$here/configure.sh"
else
    "$here/configure.sh" --no-bindings
fi

cmake --build "$BUILD_DIR" ${passthrough[@]+"${passthrough[@]}"}

# The module is the point of the default build, so its absence is reported
# here rather than as an ImportError from run-py.sh later.
if [ "$bindings" = ON ] && [ ${#passthrough[@]} -eq 0 ]; then
    if ! compgen -G "$BUILD_DIR/bindings/anytermqt*.so" >/dev/null; then
        echo "[build] The build succeeded but no anytermqt*.so is under" >&2
        echo "[build] $BUILD_DIR/bindings -- BUILD_BINDINGS did not take effect." >&2
        exit 1
    fi
fi

echo "[build] OK"
echo "[build]   scripts/run.sh        native app"
[ "$bindings" = ON ] && echo "[build]   scripts/run-py.sh     the same widget from Python"
exit 0