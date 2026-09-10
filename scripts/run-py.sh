#!/usr/bin/env bash
# scripts/run-py.sh
#
# Runs a script against the freshly built module. Defaults to
# test_terminal.py; pass another to run that instead:
#     scripts/run-py.sh bindings/widget_smoke_test.py
set -euo pipefail

. "$(cd -- "$(dirname -- "$0")" && pwd)/env.sh"

script=${1:-$REPO_ROOT/test_terminal.py}
[ $# -gt 0 ] && shift

if ! compgen -G "$BUILD_DIR/bindings/anytermqt*.so" >/dev/null; then
    echo "[run-py] No anytermqt*.so under $BUILD_DIR/bindings" >&2
    echo "[run-py] Run scripts/build.sh" >&2
    exit 1
fi

export PYTHONPATH="$BUILD_DIR/bindings${PYTHONPATH:+:$PYTHONPATH}"

# PySide6 is imported first and its Qt wins regardless, so this is for the
# module's own dependencies. The version check in env.sh is what makes having
# both in one process harmless.
#
# On macOS this line does less than it looks like it does: SIP strips DYLD_*
# from anything under /usr/bin, so a system python3 never sees it. A Homebrew
# or python.org interpreter does. If an import fails to find a Qt framework on
# a Mac, that is the first thing to check.
if [ -n "${QT_DIR:-}" ] && [ -d "$QT_DIR/lib" ] && [ "$QT_DIR" != /usr ]; then
    case "$(uname -s)" in
        Darwin) export DYLD_FRAMEWORK_PATH="$QT_DIR/lib${DYLD_FRAMEWORK_PATH:+:$DYLD_FRAMEWORK_PATH}" ;;
        *)      export LD_LIBRARY_PATH="$QT_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ;;
    esac
fi

echo "[run-py] $script"
exec "$PYTHON" "$script" "$@"
