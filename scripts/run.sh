#!/usr/bin/env bash
# scripts/run.sh
#
# Runs the native app. Arguments go to it unchanged:
#     scripts/run.sh --shell /bin/zsh --scrollback 20000
set -euo pipefail

export QTPYTE_NEED_PYTHON=0
. "$(cd -- "$(dirname -- "$0")" && pwd)/env.sh"

exe=$BUILD_DIR/qtpyte/qtpyte-term
if [ ! -x "$exe" ]; then
    echo "[run] $exe not built. Run scripts/build.sh" >&2
    exit 1
fi

# Only for a Qt that is not the system one. A distro Qt is already on the
# loader path and prepending to it invites the wrong copy.
if [ -n "${QT_DIR:-}" ] && [ -d "$QT_DIR/lib" ] && [ "$QT_DIR" != /usr ]; then
    case "$(uname -s)" in
        Darwin) export DYLD_FRAMEWORK_PATH="$QT_DIR/lib${DYLD_FRAMEWORK_PATH:+:$DYLD_FRAMEWORK_PATH}" ;;
        *)      export LD_LIBRARY_PATH="$QT_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ;;
    esac
fi

exec "$exe" "$@"
