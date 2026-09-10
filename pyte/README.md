# pyte

A headless terminal emulator in C++17. Bytes in, screen buffer out — no GUI, no
platform code, no event loop. A port of the emulation half of gopyte, which is
itself a port of Python's pyte.

The point of keeping it headless: it's testable without a window, it builds
identically on Linux, macOS, and Windows, and a renderer (Qt or otherwise) links
it without inheriting any dependency it doesn't want.

## Layout

```
include/pyte/     public API — what a consumer includes
  cell.h          Cell and Attrs
  rowstore.h      RowStore: the row list backing scrollback and the screen
  screen.h        Screen: the buffer and every operation that mutates it
  stream.h        Stream: the escape-sequence parser
  unicode.h       width and UTF-8 helpers
src/              implementation
tests/            doctest suite
tools/ptdump/     CLI: feed it a capture, print the resulting screen
tools/ptdiff/     differential check against reference pyte
```

`Screen` knows nothing about escape sequences. `Stream` parses bytes and calls
`Screen` methods. That split is the same one pyte and gopyte use, and it's what
makes the screen independently testable.

Below `Screen` sits `RowStore`: one grow-only list of rows in which the visible
screen is just a window, defined by `base()`. Scrollback is not a second
container — it is the rows below `base()`. A linefeed at the bottom is
`base()++` plus one appended row, so there is no copy between containers and no
seam for a reader to re-fuse. Viewing history moves a read offset; no screen
state is saved or swapped, so nothing can fall out of sync with the live
screen.

## Building

Requires CMake 3.21+ and a C++17 compiler. Dependencies (utf8proc, doctest) are
fetched at configure time from pinned tags — no package manager needed, and the
first configure needs network access.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

See [Testing](#testing) for what that runs and what else is worth running
before a change lands.

Ubuntu 22.04: `apt install build-essential cmake ninja-build`
macOS: `brew install cmake ninja`
Windows: CMake + Visual Studio Build Tools; drop `-G Ninja` to use the default
generator.

Options: `-DPYTE_BUILD_TESTS=OFF`, `-DPYTE_BUILD_TOOLS=OFF` — set both when
adding this as a subdirectory of a larger project.

## ptdump

The diff harness for the port. Run the same capture through the reference
implementation and through this, then compare.

```sh
./build/ptdump -c 132 -r 40 session.raw
cat session.raw | ./build/ptdump --trim
./build/ptdump --history -s 500 session.raw
```

`--history` prints retained scrollback above the screen, oldest first, and
reports the row count on stderr. That makes the whole session diffable, not
just its last screenful. It also gives a cheap self-check: a capture replayed
at `-r 40 --history` should produce exactly the text of the same capture
replayed on a screen tall enough that nothing ever scrolls.

`--trim` strips trailing spaces for readability. Leave it off when diffing:
full-width rows compare byte for byte.

`Screen::dump()` and `line_text()` produce the same text; a nonzero
"unhandled sequence(s)" line on stderr means the parser recognised a sequence
shape it has no dispatch for, which is usually the first thing to check when a
diff doesn't match.

## Testing

Three layers, cheapest first.

**Unit tests** — `ctest --test-dir build --output-on-failure`, or
`./build/tests/pyte_tests` to run the binary directly. doctest's own flags are
useful when chasing one thing:

```sh
./build/tests/pyte_tests -ts='*history*'          # one test file's cases
./build/tests/pyte_tests -tc='*alternate screen*' # one case by name
./build/tests/pyte_tests -s                       # show successful assertions
```

These say the code does what its author thought.

**Differential tests** — `tools/ptdiff/ptdiff.py` replays a capture through
reference pyte and through `ptdump`, then diffs both the screen and the
scrollback. These say the code does what *pyte* does, on bytes a real device
sent, which is the check that matters for a port.

```sh
pip install pyte
python3 tools/ptdiff/ptdiff.py --ptdump build/ptdump -c 132 -r 40 *.raw
```

`ctest` registers this automatically at 132x40 and 80x24 over every `.raw` in
the project root and in `tests/captures/`. Two geometries because most
scrolling bugs only appear when rows are scarce. Without reference pyte
installed the test reports SKIP rather than failure — a missing tool is not a
broken port.

Divergences that are intentional are listed in `EXPECTED_DIVERGENCES` at the
top of the script rather than silently tolerated, so a red run is triaged by
reading. The largest one: reference pyte has no alternate screen, so any
capture containing DECSET 47/1047/1049 diverges by design — pyte leaves the
application's last frame on screen and floods scrollback with its redraws.
ptdiff detects those captures and reports SKIP for them by name.

**Sanitizers** — worth a run before anything touching the row store lands,
since that layer is all index arithmetic:

```sh
cmake -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan && ./build-asan/tests/pyte_tests
```

### Getting captures

`rawcap.py` records a real session with paging left enabled, because a session
run with `terminal length 0` produces almost no escape sequences and makes a
useless corpus. Drop what it writes into `tests/captures/` and ctest picks it
up.

```sh
./rawcap.py lab-spine-1 -u admin -c "show running-config" \
    --cols 132 --rows 40 -o tests/captures/lab-spine-1.raw
```

A capture is worth keeping when it exercises something the current corpus
doesn't: a pager that redraws, a device that reverse-indexes, a config long
enough to fill scrollback several times, a platform whose escape vocabulary
differs.

### A check that needs no reference implementation

Scrollback plus screen should reconstruct the session exactly. Replay a capture
on a screen tall enough that nothing ever scrolls, then replay it at a normal
size with `--history`, and the text should be identical:

```sh
./build/ptdump --history --trim -c 132 -r 40  session.raw > a.txt
./build/ptdump --trim           -c 132 -r 400 session.raw | head -n $(wc -l < a.txt) > b.txt
diff a.txt b.txt
```

This catches an entire class of bug — rows lost at the seam, double-pushed, or
pushed in the wrong order — with no dependency on pyte being installed and no
expectations to maintain.

## What's implemented

Text and C0 controls (BS, HT, LF, VT, FF, CR). Cursor movement (CUU/CUD/CUF/CUB,
CUP/HVP, CHA, VPA, CNL/CPL). Erasing (ED, EL, ECH). Editing (ICH, DCH, IL, DL).
Scrolling regions (DECSTBM), IND, RI, NEL, SU/SD (`CSI S` / `CSI T`). Cursor
save/restore (ESC 7/8, CSI s/u, DECSET 1048). Scrollback, with paging and
random access, and `CSI 3 J` to clear it. Alternate screen buffer (DECSET 47,
1047, 1049). SGR: bold, italic, underline, reverse, strikethrough, 8/16-colour and
256-colour indices, and direct 24-bit colour in both the semicolon
(`38;2;r;g;b`) and colon (`38:2::r:g:b`) forms. DECAWM autowrap. UTF-8 decoding with double-width cell
handling. OSC and DCS strings are parsed and discarded.

## Colour

`Attrs::fg` and `Attrs::bg` hold one of three things: `kDefaultColor`, a
palette index 0-255, or a packed 24-bit colour. Packed colours are encoded in
the same `int` rather than given their own fields, which keeps `Cell` at 24
bytes and leaves attribute equality a plain integer compare — both matter,
because there is one `Cell` per screen position and attribute equality is what
run-batching in a renderer keys on.

```cpp
if (pyte::is_rgb(cell.attrs.fg)) {
    use(pyte::rgb_red(cell.attrs.fg), pyte::rgb_green(cell.attrs.fg),
        pyte::rgb_blue(cell.attrs.fg));
} else if (cell.attrs.fg != pyte::kDefaultColor) {
    use(palette[cell.attrs.fg]);
}
```

`Stream` normalises the colon sub-parameter form before dispatching, so
`Screen` sees one layout and never has to know which separator the host chose.

## Known gaps

These are deliberate stopping points for a scaffold, not oversights:

- **Combining marks are dropped.** `Screen::draw` returns early on width-0 code
  points. A full implementation keeps a combining sequence per cell.
- **Tab stops are fixed at every 8 columns.** No HTS/TBC stop table.
- **No mouse reporting, bracketed paste, or cursor-visibility state.** These are
  parsed and ignored; they belong to a layer above the buffer.

## Alternate screen

Full-screen applications get a second buffer with no scrollback of its own, so
their redraws never reach your history. `Screen` owns the swap
(`set_alternate_screen`); `Stream` applies the policy, which differs by mode:

| Sequence | Cursor | On entry | On exit |
| --- | --- | --- | --- |
| `?47h` / `?47l` | untouched | nothing cleared | nothing cleared |
| `?1047h` / `?1047l` | untouched | nothing cleared | alternate cleared |
| `?1049h` / `?1049l` | saved / restored | alternate cleared | nothing cleared |
| `?1048h` / `?1048l` | saved / restored | no swap | no swap |

Both buffers live for the life of the `Screen` and are swapped, never rebuilt —
the alternate screen's contents survive a switch away and back. That
persistence is the reason 1047 has to clear on the way out at all.

Modes arrive in lists (`CSI ?1049;1000;2004h` is one sequence, not three), so
every parameter is dispatched. Cursor visibility, bracketed paste and mouse
reporting are recognised and deliberately left to a layer above the buffer;
they do not count against `unhandled_count()`.

The payoff is measurable. A synthetic pager session — 38 lines of shell, then
200 full-screen redraws of 22 lines each, then back to the prompt:

```
alt screen honoured:  38 history rows, shell screen restored intact
alt screen ignored:   1000 history rows (the retention budget, all of it
                      the pager's redraws)
```

## Scrollback

`Screen` retains rows that scroll off the top. The budget is a constructor
argument, defaulting to `kDefaultScrollback` (1000 rows); 0 disables retention.

```cpp
pyte::Screen screen(80, 24, 5000);
```

Rows only become scrollback when the region being scrolled includes the top of
the screen. A DECSTBM region that starts below row 0 scrolls in place, and IL /
DL are edits inside a region rather than scrolls of the screen, so neither
contributes history — which is why a full-screen editor's redraws don't fill
your scrollback with garbage.

Two ways to read it, for two different callers:

- **Paging**, for a keyboard: `scroll_history_up/down`, `scroll_history_to_top`,
  `scroll_history_to_bottom`. These move the window, so `at()`, `line_text()`
  and `dump()` show history instead of the live screen, and
  `viewing_history()` tells a renderer to suppress the cursor and stop
  auto-scrolling.
- **Random access**, for a scrollbar: `history_at(x, i)`,
  `history_line_text(i)`, `history_lines()`, where index 0 is the oldest row
  still held. These ignore where the window is pointed.

Anything that writes cells snaps the window back to the live screen first, so
output arriving while the user is scrolled back cannot land in history — the
view drops to the bottom instead, which is what every terminal does.

Cost is bounded and predictable: one `Cell` is 24 bytes, so retention costs
roughly `budget × cols × 24` bytes and is trimmed on every scroll rather than
growing with session length. 200k lines through an 80-column screen holds flat
at the same footprint as no scrollback at all.

### SU and SD

`scroll_up(n)` / `scroll_down(n)` scroll the contents of the current region and
leave the cursor where it is — `CSI S` and `CSI T`. Rows leaving the top become
scrollback on the same terms as any other scroll, so a region that starts below
row 0 scrolls in place and contributes nothing.

The five-parameter form of `CSI T` is xterm's highlight mouse tracking, which
shares the final byte and has nothing to do with scrolling; it is counted as
unhandled rather than acted on, because scrolling on it shreds the screen.

Don't confuse these with the `scroll_history_*` calls, which move the viewport
and touch no cells.

### Resize

Shrinking anchors the *bottom* of the window, so rows leaving the top become
scrollback rather than disappearing, and growing pulls them back into view. The
cursor moves with the rows: it names a screen row, and every resident row lands
on a different one when `base()` shifts. Without that adjustment the cursor
silently comes to mean a different line of text, and the next thing written — a
shell prompt — paints over content still on screen, at exactly the row offset
the window changed by.
