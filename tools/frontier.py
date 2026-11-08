#!/usr/bin/env python3
"""The replacement frontier: which functions can be replaced next.

Combines the call graph from the dumpbin disassembly (build/s3.asm, see
tools/asm_tools.py for how to make it) with the status column of
re/functions.csv. A callee counts as done when it is verified, is in the
statically linked C runtime (>= 0x61e4ef, or category crt), or is DebugLog.

Sections (default range 0x4c0000..0x560000, the simulation and logic thread):
  READY     unverified functions whose direct callees are all done; sorted by
            how many NEAR functions they would unblock, then size
  NEAR      exactly one callee missing (listed)
  BLOCKERS  missing callees that would make the most NEAR functions READY

Flags: api = calls an imported API (call ds:[...]); indirect = calls through
a register or memory (vtables, function pointers: check the targets by hand);
seh = uses fs: (exception frame); tail = has tail jumps to other functions.
Tail jumps count as calls.

Usage:
  python tools/frontier.py [--range LOW HIGH] [--max-instructions N]
                           [--limit N] [--all-flags]
By default functions with indirect calls are left out of READY/NEAR (their
real callees are unknown); --all-flags keeps them.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from asm_tools import ROOT, containing, load_inventory, load_starts, read_asm  # noqa: E402

CRT_START = 0x61e4ef
DEBUG_LOG = 0x428720
GAME_LOW, GAME_HIGH = 0x401000, CRT_START
# Default range: the simulation and logic thread (where the batches work);
# --range 401000 61e4ef for all game code.
SIM_LOW, SIM_HIGH = 0x4c0000, 0x560000

DIRECT = re.compile(r"(call|jmp)\s+([0-9A-F]{8})$")


def build_graph(asm, starts, low, high):
    start_set = set(starts)
    info = defaultdict(lambda: {"n": 0, "callees": set(), "flags": set()})
    for address, ins in asm:
        if not low <= address < high or ins.startswith("nop"):
            continue
        f = containing(starts, address)
        d = info[f]
        d["n"] += 1
        if "fs:" in ins:
            d["flags"].add("seh")
        m = DIRECT.match(ins)
        if m:
            target = int(m.group(2), 16)
            if m.group(1) == "call":
                d["callees"].add(target)
            elif target in start_set and target != f:
                d["callees"].add(target)
                d["flags"].add("tail")
        elif ins.startswith("call"):
            d["flags"].add("api" if "ds:[" in ins else "indirect")
        elif ins.startswith("jmp") and "ds:[" in ins:
            d["flags"].add("api")
        elif ins.startswith("jmp") and re.match(r"jmp\s+(dword ptr \[|e[a-z]x)", ins) \
                and "*4" not in ins:
            d["flags"].add("indirect")
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--asm", default=os.path.join(ROOT, "build", "s3.asm"))
    parser.add_argument("--range", nargs=2, metavar=("LOW", "HIGH"))
    parser.add_argument("--max-instructions", type=int, default=400)
    parser.add_argument("--limit", type=int, default=40)
    parser.add_argument("--all-flags", action="store_true")
    args = parser.parse_args()
    if not os.path.exists(args.asm):
        sys.exit(f"{args.asm} not found; see tools/asm_tools.py for how to make it")
    low, high = (int(args.range[0], 16), int(args.range[1], 16)) if args.range \
        else (SIM_LOW, SIM_HIGH)

    asm = read_asm(args.asm)
    starts = load_starts()
    inventory = load_inventory()
    # Graph over all game code, so caller counts are complete.
    graph = build_graph(asm, starts, GAME_LOW, GAME_HIGH)
    callers = Counter()
    for d in graph.values():
        for c in d["callees"]:
            callers[c] += 1

    def done(address):
        row = inventory.get(address, {})
        return (row.get("status") == "verified" or address >= CRT_START
                or address == DEBUG_LOG or row.get("category") == "crt")

    def name(address):
        return inventory.get(address, {}).get("name", "")

    ready, near = [], []
    for f, d in graph.items():
        if not low <= f < high or done(f) or d["n"] < 4 or d["n"] > args.max_instructions:
            continue
        if "indirect" in d["flags"] and not args.all_flags:
            continue
        missing = sorted(c for c in d["callees"] if not done(c))
        if not missing:
            ready.append(f)
        elif len(missing) == 1:
            near.append((f, missing[0]))

    blockers = Counter(m for _, m in near)

    def line(f, extra=""):
        d = graph[f]
        flags = ",".join(sorted(d["flags"]))
        return (f"{f:#x} {d['n']:4} ins {callers[f]:3} callers {blockers[f]:3} unblocks  "
                f"{name(f):40} {flags:12} {extra}").rstrip()

    def rank(f):  # most unblocked first, then reachable ones, then small ones
        return (-blockers[f], callers[f] == 0, graph[f]["n"])

    print(f"READY ({len(ready)}): all callees done")
    for f in sorted(ready, key=rank)[:args.limit]:
        print("  " + line(f))
    print(f"\nNEAR ({len(near)}): one callee missing")
    for f, m in sorted(near, key=lambda p: rank(p[0]))[:args.limit]:
        print("  " + line(f, f"needs {m:#x} ({graph[m]['n']} ins) {name(m)}"))
    blockers = Counter(m for _, m in near)
    print("\nBLOCKERS: missing callees that unblock the most NEAR functions")
    for m, count in blockers.most_common(min(args.limit, 20)):
        print(f"  {m:#x} unblocks {count:3}  {graph[m]['n']:4} ins  {name(m)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
