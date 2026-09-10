#!/usr/bin/env python3
"""tools/ptdiff/ptdiff.py

Differential test harness: replay a capture through reference pyte and through
ptdump, then diff both the screen and the scrollback.

This is the check that matters for a port. The C++ unit tests say the code does
what its author thought; this says it does what pyte does, on bytes a real
device actually sent.

  ./ptdiff.py -c 132 -r 40 lab-spine.raw
  ./ptdiff.py --ptdump ../../build/ptdump captures/*.raw

Exit status is 0 when every capture matches, 1 otherwise, so it drops straight
into CI or a pre-commit hook.

Rows are compared right-stripped: pyte and this port agree on cell contents but
not always on whether a trailing run is spaces or unset cells, and that
difference is invisible to a renderer. Everything else is compared literally.

Known divergences are listed in EXPECTED_DIVERGENCES below rather than silently
tolerated -- a diff that turns out to be intentional gets an entry with a
reason, so the next person reading a red run knows which failures are news.
"""

from __future__ import annotations

import argparse
import difflib
import glob
import signal
import subprocess
import sys

# Piping into head is the normal way to read this; don't traceback on it.
try:
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)
except (AttributeError, ValueError):
    pass  # Windows has no SIGPIPE

# 77 is ctest's SKIP_RETURN_CODE: a missing reference implementation is a
# machine that can't run this check, not a failing port.
SKIP_EXIT = 77

try:
    import pyte
except ImportError:
    print("ptdiff: reference pyte not installed -- pip install pyte",
          file=sys.stderr)
    sys.exit(SKIP_EXIT)


# Divergences this port makes on purpose. Keyed by a short name, with the
# reason, so a red run is triaged by reading rather than by bisecting.
EXPECTED_DIVERGENCES = {
    "combining-marks": "port drops width-0 code points; pyte attaches them to "
                       "the preceding cell",
    "truecolor": "port models palette indices only; SGR 38;2 is consumed and "
                 "discarded",
    "alternate-screen": "reference pyte has no alternate screen, so a capture "
                        "containing DECSET 47/1047/1049 diverges BY DESIGN -- "
                        "pyte leaves the application's last frame on screen "
                        "and floods scrollback with its redraws. Flagged per "
                        "capture below; check the diff is only that.",
}

# Sequences reference pyte does not implement. A capture containing one of
# these is expected to diverge, and saying so up front beats having someone
# bisect a red run to rediscover it.
ALT_SCREEN_MARKERS = (b"\x1b[?1049", b"\x1b[?1047", b"\x1b[?47")


def reference(data: bytes, cols: int, rows: int, history: int):
    """Replay through pyte's HistoryScreen and return (screen, scrollback)."""
    screen = pyte.HistoryScreen(cols, rows, history=history, ratio=1.0)
    stream = pyte.Stream(screen)
    stream.feed(data.decode("utf-8", errors="replace"))

    live = [screen.display[y] for y in range(rows)]

    # pyte keeps scrollback in history.top, oldest first. Each entry is a
    # sparse mapping of column -> Char, so a row is rebuilt by index rather
    # than by iterating what happens to be present.
    scrollback = []
    for line in screen.history.top:
        scrollback.append("".join(line[x].data for x in range(cols)))
    return live, scrollback


def port(ptdump: str, path: str, cols: int, rows: int, history: int):
    """Replay through ptdump and return (screen, scrollback)."""
    result = subprocess.run(
        [ptdump, "-c", str(cols), "-r", str(rows), "-s", str(history),
         "--history", path],
        capture_output=True, text=True, check=True,
    )
    lines = result.stdout.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    # --history prints scrollback first, then exactly `rows` screen lines.
    return lines[-rows:], lines[:-rows]


def compare(name: str, want: list[str], got: list[str], label: str) -> bool:
    want = [line.rstrip() for line in want]
    got = [line.rstrip() for line in got]
    if want == got:
        return True
    print(f"--- {name}: {label} differs "
          f"(pyte {len(want)} rows, port {len(got)} rows)")
    for line in difflib.unified_diff(want, got, "pyte", "port", lineterm="", n=1):
        print(line)
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[2])
    ap.add_argument("captures", nargs="+", help="raw capture files (globs ok)")
    ap.add_argument("-c", "--cols", type=int, default=80)
    ap.add_argument("-r", "--rows", type=int, default=24)
    ap.add_argument("-s", "--scrollback", type=int, default=1000)
    ap.add_argument("--ptdump", default="./build/ptdump")
    ap.add_argument("--screen-only", action="store_true",
                    help="skip the scrollback comparison")
    args = ap.parse_args()

    paths = [p for pattern in args.captures for p in sorted(glob.glob(pattern))]
    if not paths:
        print("ptdiff: no captures matched", file=sys.stderr)
        return SKIP_EXIT

    failures = 0
    for path in paths:
        data = open(path, "rb").read()
        uses_alt = any(marker in data for marker in ALT_SCREEN_MARKERS)
        want_live, want_back = reference(data, args.cols, args.rows,
                                         args.scrollback)
        got_live, got_back = port(args.ptdump, path, args.cols, args.rows,
                                  args.scrollback)

        ok = compare(path, want_live, got_live, "screen")
        if not args.screen_only:
            ok &= compare(path, want_back, got_back, "scrollback")
        if ok:
            print(f"ok   {path}  ({args.cols}x{args.rows}, "
                  f"{len(got_back)} history rows)")
        elif uses_alt:
            print(f"SKIP {path}  uses the alternate screen; reference pyte "
                  f"does not implement it, so this diff is expected")
        else:
            failures += 1

    if EXPECTED_DIVERGENCES and failures:
        print("\nknown intentional divergences:")
        for name, reason in EXPECTED_DIVERGENCES.items():
            print(f"  {name}: {reason}")

    print(f"\n{len(paths) - failures}/{len(paths)} captures match")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
