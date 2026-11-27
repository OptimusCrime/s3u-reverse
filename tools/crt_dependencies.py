#!/usr/bin/env python3
"""Which C runtime functions the game depends on (re/crt_dependencies.md).

Splits the inventory's category-crt rows into
  candidates  functions whose behaviour the game or our replacements depend
              on: verified like game code (they keep their status), and
  platform    the rest (startup, heap internals, locking, file and console
              I/O internals, locale, SEH runtime, iostream internals):
              status `platform`, the standalone build uses the platform's
              C library.

Rule (from the call graph in build/s3.asm, docs/tools/analysis.md):
  1. every crt function referenced (call, jmp or address operand) from a
     function that isn't crt (game, watcom, stl, thirdparty, compiler, or a
     function without an inventory row) is a candidate;
  2. so is every crt function those reach through crt-internal calls,
     except through OS-facing ones: a crt function whose note says it
     calls a Win32 API, or one of the runtime-internal names in
     PLATFORM_NAME, stops the walk (and is not added by it);
  3. named rows (identified/verified) stay candidates unless their name is
     in PLATFORM_NAME; verified rows always stay;
  4. trivial functions (tools/trivial.py) keep status identified.

Commands:
  report   print the split (default)
  apply    write status `platform` into re/functions.csv for the rest
"""

import bisect
import collections
import csv
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import inventory  # noqa: E402

ASM = inventory.ROOT / "build" / "s3.asm"
PLATFORM_NAME = re.compile(
    r"^(IOS_|ios::|streambuf|strstreambuf|__global_unwind|__local_unwind|__abnormal_term|"
    r"__NLG_|__exit|__amsg_exit|WinMainCRTStartup|__nh_malloc|__fclose_lk|__freebuf|"
    r"__CallSettingFrame|__startOneArg|AtexitReg|StaticDtorReg|atexit$|__CxxThrowException|"
    r"exception::|__EH_prolog|_chkstk|__setjmp3|_longjmp|ImportThunk|_malloc$)")
NOTE = ("Platform library (2026-11-27, re/crt_dependencies.md): not referenced from game or "
        "library code nor reached from a behaviour-relevant CRT function; the standalone build "
        "uses the platform C library.")
INSTRUCTION = re.compile(r"^  ([0-9A-F]{8}): \w+\s+(.*)$")
OPERAND = re.compile(r"\b0?([0-9A-F]{6,8})h?\b")


def split():
    starts = sorted(inventory.load_baseline())
    _, rows = inventory.load_inventory()
    inv = {int(r["address"], 16): r for r in rows}
    crt = {a for a, r in inv.items() if r["category"] == "crt"}

    def owner(address):
        i = bisect.bisect_right(starts, address) - 1
        return starts[i] if i >= 0 else None

    callers = collections.defaultdict(set)
    for line in ASM.open(encoding="latin-1"):
        m = INSTRUCTION.match(line)
        if not m:
            continue
        for operand in OPERAND.findall(m.group(2)):
            target = int(operand, 16)
            if target in crt:
                o = owner(int(m.group(1), 16))
                if o is not None and o != target:
                    callers[target].add(o)

    def is_crt(a):
        return a in inv and inv[a]["category"] == "crt"

    def os_facing(a):
        return bool(PLATFORM_NAME.match(inv[a]["name"])) or " calls " in inv[a]["notes"]

    direct = {t for t, cs in callers.items() if any(not is_crt(c) for c in cs)}
    callees = collections.defaultdict(set)
    for t, cs in callers.items():
        for c in cs:
            if c in crt:
                callees[c].add(t)
    keep = set(direct)
    todo = [a for a in direct if not os_facing(a)]
    while todo:
        for g in callees.get(todo.pop(), ()):
            if g not in keep and not os_facing(g):
                keep.add(g)
                todo.append(g)
    keep |= {a for a in crt if inv[a]["status"] == "verified"
             or (inv[a]["status"] == "identified" and not PLATFORM_NAME.match(inv[a]["name"]))}
    keep |= {a for a in crt if inv[a]["notes"].startswith("Trivial, proven by its bytes")}
    return rows, inv, crt, direct, keep


def main() -> int:
    command = sys.argv[1] if len(sys.argv) > 1 else "report"
    rows, inv, crt, direct, keep = split()
    platform = crt - keep
    print(f"crt rows {len(crt)}: candidates {len(keep)} (referenced from non-crt code "
          f"{len(direct)}), platform {len(platform)}")
    if command == "report":
        for a in sorted(keep):
            print(f"  candidate 0x{a:08x} {inv[a]['name']}{' (direct)' if a in direct else ''}")
        return 0
    if command != "apply":
        print(__doc__)
        return 2
    for a in platform:
        row = inv[a]
        if row["status"] == "platform":
            continue
        assert row["hook"] == "none" and row["status"] in ("classified", "identified"), row
        row["status"] = "platform"
        row["notes"] = f"{row['notes']} {NOTE}" if row["notes"] else NOTE
    with inventory.INVENTORY.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=inventory.COLUMNS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    return 0


if __name__ == "__main__":
    sys.exit(main())
