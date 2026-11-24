#!/usr/bin/env python3
"""The replacement frontier: which functions can be replaced next.

Combines the call graph from the dumpbin disassembly (build/s3.asm, see
tools/asm_tools.py for how to make it) with the status column of
re/functions.csv. A callee counts as done when it is verified or
reimplemented (unverified, S3RE_HOOK_UNVERIFIED; --verified-only leaves
those out), is in the statically linked C runtime (>= 0x61e4ef, or category
crt), or is DebugLog.

Sections (default range 0x4c0000..0x560000, the simulation and logic thread):
  READY     unverified functions whose direct callees are all done; sorted by
            how many NEAR functions they would unblock, then size
  NEAR      exactly one callee missing (listed)
  BLOCKERS  missing callees that would make the most NEAR functions READY

Trivial functions (tools/trivial.py: a lone `ret`, a constant return, a
`jmp` or this-adjusting thunk; recorded as identified, proven by their bytes)
count as done; a forwarding one only once its target is done.
--no-trivial leaves them out (the header prints how many count).

Library code: functions identified as library code (status identified,
category stl, mfc, watcom or compiler; crt counts as done anyway) are not
verified, so by default they still block their callers; they are listed in
their own LIBRARY section. --library-done counts them as done (the header
then says so).

Static-initializer stubs: the compiler emits one tiny function per global
object (about 2,000, mostly `push -1; push text; mov ecx, global; call
std::string ctor; ret`). They are recognized by shape (`mov ecx, constant`
plus only pushes of constants, one direct call and ret, or a tail jump) and
hidden from all sections; their number is printed. --show-stubs keeps them.

Flags: api = calls an imported API (call ds:[...]); indirect = calls through
a register or memory (vtables, function pointers: check the targets by hand);
seh = uses fs: (exception frame); tail = has tail jumps to other functions.
Tail jumps count as calls.

Usage:
  python tools/frontier.py [--range LOW HIGH] [--max-instructions N]
                           [--limit N] [--all-flags] [--library-done]
                           [--show-stubs] [--verified-only] [--no-trivial]
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
from trivial import find_trivial  # noqa: E402

CRT_START = 0x61e4ef
DEBUG_LOG = 0x428720
GAME_LOW, GAME_HIGH = 0x401000, CRT_START
# Default range: the simulation and logic thread (where the batches work);
# --range 401000 61e4ef for all game code.
SIM_LOW, SIM_HIGH = 0x4c0000, 0x560000

LIBRARY = {"stl", "crt", "mfc", "watcom", "thirdparty", "compiler"}
STUB_PART = re.compile(r"(push\s+(0?[0-9A-F]+h|[0-9])|mov\s+ecx,(0?[0-9A-F]+h|[0-9])|ret)$")

DIRECT = re.compile(r"(call|jmp)\s+([0-9A-F]{8})$")


def build_graph(asm, starts, low, high):
    start_set = set(starts)
    info = defaultdict(lambda: {"n": 0, "callees": set(), "flags": set(), "other": 0})
    for address, ins in asm:
        if not low <= address < high or ins.startswith("nop"):
            continue
        f = containing(starts, address)
        d = info[f]
        d["n"] += 1
        if "fs:" in ins:
            d["flags"].add("seh")
        m = DIRECT.match(ins)
        if not m and not STUB_PART.match(ins):
            d["other"] += 1
        if ins.startswith("mov") and STUB_PART.match(ins):
            d["mov_ecx"] = True
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
    parser.add_argument("--library-done", action="store_true",
                        help="count identified library functions (stl/crt/...) as done")
    parser.add_argument("--show-stubs", action="store_true",
                        help="keep static-initializer stubs in the lists")
    parser.add_argument("--verified-only", action="store_true",
                        help="don't count reimplemented (unverified) functions as done")
    parser.add_argument("--no-trivial", action="store_true",
                        help="don't count trivial functions (tools/trivial.py) as done")
    args = parser.parse_args()
    if not os.path.exists(args.asm):
        sys.exit(f"{args.asm} not found; see tools/asm_tools.py for how to make it")
    low, high = (int(args.range[0], 16), int(args.range[1], 16)) if args.range \
        else (SIM_LOW, SIM_HIGH)

    asm = read_asm(args.asm)
    starts = load_starts()
    inventory = load_inventory()
    trivial = {} if args.no_trivial else find_trivial(starts)
    # Graph over all game code, so caller counts are complete.
    graph = build_graph(asm, starts, GAME_LOW, GAME_HIGH)
    callers = Counter()
    for d in graph.values():
        for c in d["callees"]:
            callers[c] += 1

    def is_library(address):
        # Trivial functions follow their own rule (see done()), not this one.
        row = inventory.get(address, {})
        return address not in trivial and row.get("category") in LIBRARY and row.get("status") == "identified"

    def is_stub(f):
        # Only constant pushes, mov ecx,constant (the global), ret and exactly
        # one direct call or tail jump: a compiler-generated initializer.
        d = graph[f]
        return d["other"] == 0 and d.get("mov_ecx") and len(d["callees"]) == 1 \
            and d["n"] <= 8 \
            and "indirect" not in d["flags"] and "api" not in d["flags"]

    def done(address, depth=0):
        row = inventory.get(address, {})
        done_statuses = ("verified",) if args.verified_only else ("verified", "reimplemented")
        if (row.get("status") in done_statuses or address >= CRT_START
                or address == DEBUG_LOG or row.get("category") == "crt"
                or (args.library_done and is_library(address))):
            return True
        t = trivial.get(address)
        if t and depth < 8:  # a forwarder is done when its target is
            return t[1] is None or done(t[1], depth + 1)
        return False

    def name(address):
        return inventory.get(address, {}).get("name", "")

    ready, near, stubs = [], [], 0
    for f, d in graph.items():
        if not low <= f < high or done(f) or d["n"] < 4 or d["n"] > args.max_instructions:
            continue
        if is_stub(f) and not args.show_stubs:
            stubs += 1
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

    library = sorted(f for f in graph if is_library(f))
    trivial_done = sum(1 for f in trivial if done(f))
    print(f"(range {low:#x}..{high:#x}; {stubs} static-initializer stubs hidden"
          f"{'' if args.show_stubs else ', --show-stubs lists them'}; "
          f"{len(library)} identified library functions "
          f"{'counted as done (--library-done)' if args.library_done else 'not counted as done'}; "
          f"{trivial_done} of {len(trivial)} trivial functions done"
          f"{' (--no-trivial)' if args.no_trivial else ''})\n")
    print(f"READY ({len(ready)}): all callees done")
    for f in sorted(ready, key=rank)[:args.limit]:
        print("  " + line(f))
    print(f"\nNEAR ({len(near)}): one callee missing")
    for f, m in sorted(near, key=lambda p: rank(p[0]))[:args.limit]:
        print("  " + line(f, f"needs {m:#x} ({graph[m]['n']} ins) {name(m)}"))
    print("\nLIBRARY: identified library functions (not verified; callers in all game code)")
    for f in sorted(library, key=lambda f: -callers[f])[:args.limit]:
        print(f"  {f:#x} {graph[f]['n']:4} ins {callers[f]:4} callers  {name(f)}")
    print("\nBLOCKERS: missing callees that unblock the most NEAR functions")
    for m, count in blockers.most_common(min(args.limit, 20)):
        print(f"  {m:#x} unblocks {count:3}  {graph[m]['n']:4} ins  {name(m)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
