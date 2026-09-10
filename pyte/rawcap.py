#!/usr/bin/env python3
"""tools/rawcap/rawcap.py

Capture a raw SSH session byte stream for terminal-emulator testing.

Unlike an automation library, this deliberately keeps everything: escape
sequences, pager prompts, CRLF, and whatever redraw the device does. That noise
is the point -- it is the corpus ptdump runs against.

Paging is left ENABLED by default. A session with `terminal length 0` produces
almost no escape sequences and makes a useless test corpus. The script answers
the pager itself so the capture still contains the full command output.

  ./rawcap.py eng-spine-1 -u admin -c "show running-config" -o lab-spine.raw
  ./rawcap.py eng-leaf-1 -u admin -c "show version" -c "show interfaces" \\
      --cols 132 --rows 40 -o lab-leaf.raw

Then:

  ptdump -c 132 -r 400 --trim lab-spine.raw
"""

from __future__ import annotations

import argparse
import getpass
import ipaddress
import os
import re
import select
import socket
import sys
import time

import paramiko

# CGNAT space per RFC 6598. An address in this range is very often a NAT
# artifact rather than a routable management address, so resolve the name
# instead of dialing the address.
CGNAT_NET = ipaddress.ip_network("100.64.0.0/10")

# Pager prompts across the platforms this has been pointed at. Matched against
# the tail of the received buffer, so a prompt split across two reads still
# fires once the second read lands.
PAGER_PATTERNS = [
    rb"--\s*More\s*--",           # Arista EOS, Cisco IOS / IOS-XE
    rb"---\(more[^)]*\)---",      # Junos
    rb"<-+\s*More\s*-+>",         # some NX-OS builds
    rb"lines \d+-\d+",            # less(1) on a Linux host
]
PAGER_RE = re.compile(b"|".join(PAGER_PATTERNS), re.IGNORECASE)


def resolve_target(host: str) -> str:
    """Return the address to connect to, forward-resolving CGNAT addresses.

    If `host` is a literal address inside 100.64.0.0/10, the address itself is
    usually not the reachable one. Reverse-resolve it to a name, then forward-
    resolve that name and use the result.
    """
    try:
        addr = ipaddress.ip_address(host)
    except ValueError:
        return host  # already a name; nothing to do

    if addr.version != 4 or addr not in CGNAT_NET:
        return host

    print(f"[rawcap] {host} is CGNAT ({CGNAT_NET}); resolving by name", file=sys.stderr)
    try:
        name = socket.gethostbyaddr(host)[0]
    except OSError as exc:
        print(f"[rawcap] no PTR for {host} ({exc}); using the address as given",
              file=sys.stderr)
        return host

    try:
        resolved = socket.gethostbyname(name)
    except OSError as exc:
        print(f"[rawcap] {name} did not forward-resolve ({exc}); using the address",
              file=sys.stderr)
        return host

    print(f"[rawcap] {host} -> {name} -> {resolved}", file=sys.stderr)
    return resolved


def drain(chan, sink, idle: float, total_cap: float, answer_pager: bool) -> bytes:
    """Read until the channel goes quiet for `idle` seconds.

    Every byte is written to `sink` untouched. Returns the bytes read, so the
    caller can look for a prompt.
    """
    collected = bytearray()
    tail = b""
    started = time.monotonic()
    last_data = time.monotonic()

    while True:
        if time.monotonic() - started > total_cap:
            print("[rawcap] hit the overall read cap; moving on", file=sys.stderr)
            break

        ready, _, _ = select.select([chan], [], [], 0.2)
        if ready:
            if chan.recv_ready():
                data = chan.recv(65536)
                if not data:
                    break
                sink.write(data)
                collected.extend(data)
                tail = (tail + data)[-256:]
                last_data = time.monotonic()

                if answer_pager and PAGER_RE.search(tail):
                    chan.send(b" ")
                    tail = b""
                continue

        if chan.exit_status_ready() and not chan.recv_ready():
            break
        if time.monotonic() - last_data > idle:
            break

    return bytes(collected)


def summarize(raw: bytes) -> None:
    esc = raw.count(0x1B)
    cr = raw.count(0x0D)
    lf = raw.count(0x0A)
    finals = sorted({m.group(1).decode("latin-1")
                     for m in re.finditer(rb"\x1b\[[0-9;?]*([@-~])", raw)})
    print(f"[rawcap] {len(raw)} bytes  ESC={esc}  CR={cr}  LF={lf}", file=sys.stderr)
    if finals:
        print(f"[rawcap] CSI final bytes seen: {' '.join(finals)}", file=sys.stderr)
    else:
        print("[rawcap] no CSI sequences -- the capture will not exercise the parser",
              file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host")
    ap.add_argument("-u", "--username", required=True)
    ap.add_argument("-p", "--password",
                    help="omit to read from RAWCAP_PASSWORD or prompt")
    ap.add_argument("-c", "--command", action="append", default=[],
                    help="repeatable; run in order")
    ap.add_argument("-o", "--output", required=True, help="raw capture file")
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--term", default="xterm")
    ap.add_argument("--cols", type=int, default=80)
    ap.add_argument("--rows", type=int, default=24)
    ap.add_argument("--idle", type=float, default=2.0,
                    help="seconds of quiet that ends a read (default 2)")
    ap.add_argument("--cap", type=float, default=120.0,
                    help="overall seconds cap per read (default 120)")
    ap.add_argument("--no-pager-answer", action="store_true",
                    help="do not send space at a pager prompt")
    ap.add_argument("--disable-paging", action="store_true",
                    help="send 'terminal length 0' first -- produces a much less "
                         "interesting capture; off by default on purpose")
    ap.add_argument("--strict-hostkey", action="store_true",
                    help="reject unknown host keys (off by default for lab use)")
    ap.add_argument("--legacy-kex", action="store_true",
                    help="allow older kex/host-key algorithms for legacy gear")
    args = ap.parse_args()

    password = args.password or os.environ.get("RAWCAP_PASSWORD")
    if not password:
        password = getpass.getpass(f"password for {args.username}@{args.host}: ")

    target = resolve_target(args.host)

    client = paramiko.SSHClient()
    client.load_system_host_keys()
    if args.strict_hostkey:
        client.set_missing_host_key_policy(paramiko.RejectPolicy())
    else:
        # Lab default. Flip on --strict-hostkey when it matters.
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    connect_kwargs = dict(
        hostname=target,
        port=args.port,
        username=args.username,
        password=password,
        look_for_keys=False,
        allow_agent=False,
        timeout=15,
    )
    if args.legacy_kex:
        connect_kwargs["disabled_algorithms"] = {"pubkeys": ["rsa-sha2-512", "rsa-sha2-256"]}

    client.connect(**connect_kwargs)

    chan = client.invoke_shell(term=args.term, width=args.cols, height=args.rows)
    chan.settimeout(1.0)

    answer_pager = not args.no_pager_answer

    with open(args.output, "wb") as sink:
        drain(chan, sink, args.idle, args.cap, answer_pager)  # banner and prompt

        if args.disable_paging:
            chan.send(b"terminal length 0\n")
            drain(chan, sink, args.idle, args.cap, answer_pager)

        for command in args.command:
            chan.send(command.encode() + b"\n")
            drain(chan, sink, args.idle, args.cap, answer_pager)

        chan.send(b"exit\n")
        drain(chan, sink, 1.0, 10.0, False)

    client.close()

    with open(args.output, "rb") as handle:
        raw = handle.read()
    summarize(raw)
    print(f"[rawcap] wrote {args.output}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())