#!/usr/bin/env python3
"""Trivial functions: bodies whose bytes alone prove what they do.

Some functions are too short for a Detours hook (a lone `ret` is 1 byte, a
hook needs 5) and have nothing to reimplement: an empty virtual function, a
constant return, a compiler-generated thunk that adjusts `this` and jumps on.
They are recorded in re/functions.csv as status `identified` (never
`verified`, nothing was tested) with a note starting with NOTE_PREFIX, and
tools/frontier.py counts them as done (forwarders only once their target is).

The rule (deliberately narrow). The whole body, from the function start to
the next known start, must be one of these shapes, followed only by `nop` /
`int 3` padding:
  leaf      `ret` / `ret N`
  constant  `xor eax,eax` | `mov eax,IMM` | `or eax,0FFFFFFFFh` | `xor al,al`
            | `mov al,IMM`, then `ret` / `ret N`
  forward   `jmp T`
            `add ecx,IMM` / `sub ecx,IMM`, then `jmp T` (this-adjusting thunk)
            `sub ecx,dword ptr [ecx-K]`, optionally `add|sub ecx,IMM`, then
            `jmp T` (vtordisp thunk)
            T must be a known function start; a forwarder is done when T is.
Anything else (field getters/setters, `mov ecx,global; jmp` initializer
stubs, which frontier.py hides by shape anyway) does not qualify.

Usage:
  python tools/trivial.py list            print every trivial function in game code
  python tools/trivial.py apply           record them in re/functions.csv (status
                                          identified + note); never touches verified,
                                          reimplemented or hooked rows
  python tools/trivial.py check           exit 1 if the inventory is out of sync
Options: --asm PATH (default build/s3.asm, see tools/asm_tools.py)
"""

from __future__ import annotations

import argparse
import csv
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from asm_tools import ROOT, containing, read_asm, load_starts  # noqa: E402

CRT_START = 0x61e4ef
GAME_LOW = 0x401000
NOTE_PREFIX = "Trivial, proven by its bytes"
INVENTORY = os.path.join(ROOT, "re", "functions.csv")

RET = re.compile(r"ret(\s+[0-9A-F]+h?)?$")
CONSTANT = re.compile(r"(xor\s+eax,eax|mov\s+eax,[0-9A-F]+h?|or\s+eax,0FFFFFFFFh"
                      r"|xor\s+al,al|mov\s+al,[0-9A-F]+h?)$")
JMP = re.compile(r"jmp\s+([0-9A-F]{8})$")
ADJUST = re.compile(r"(add|sub)\s+ecx,[0-9A-F]+h?$")
VTORDISP = re.compile(r"sub\s+ecx,dword ptr \[ecx-[0-9A-F]+h?\]$")
PADDING = re.compile(r"(nop|int\s+3)$")


def bodies(asm, starts, low=GAME_LOW, high=CRT_START):
    out = defaultdict(list)
    for address, ins in asm:
        if low <= address < high:
            out[containing(starts, address)].append(re.sub(r"\s+", " ", ins).strip())
    return out


def classify(ins: list[str], start_set) -> tuple[str, int | None] | None:
    """(kind, forward target or None) when the body is trivial, else None."""
    real = list(ins)
    while real and PADDING.match(real[-1]):
        real.pop()
    if not real or any(PADDING.match(i) for i in real):
        return None
    if len(real) == 1 and RET.match(real[0]):
        return ("leaf", None)
    if len(real) == 2 and CONSTANT.match(real[0]) and RET.match(real[1]):
        return ("constant", None)
    m = JMP.match(real[-1])
    if not m:
        return None
    target = int(m.group(1), 16)
    if target not in start_set:
        return None
    prefix = real[:-1]
    ok = (not prefix
          or (len(prefix) == 1 and ADJUST.match(prefix[0]))
          or (len(prefix) == 1 and VTORDISP.match(prefix[0]))
          or (len(prefix) == 2 and VTORDISP.match(prefix[0]) and ADJUST.match(prefix[1])))
    return ("forward", target) if ok else None


def find_trivial(asm, starts) -> dict[int, tuple[str, int | None, list[str]]]:
    start_set = set(starts)
    out = {}
    for f, ins in bodies(asm, starts).items():
        c = classify(ins, start_set)
        if c:
            real = [i for i in ins if not PADDING.match(i)]
            out[f] = (c[0], c[1], real)
    return out


def note_for(kind, target, real):
    text = "; ".join(real)
    what = {"leaf": "returns at once", "constant": "returns a constant",
            "forward": f"forwards to {target:#x} (done when that is)" if target else ""}[kind]
    return f"{NOTE_PREFIX} ({text}): {what}; too short to hook, nothing to reimplement"


def load_rows():
    with open(INVENTORY, encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        return reader.fieldnames, list(reader)


def cmd_list(trivial):
    kinds = Counter(k for k, _, _ in trivial.values())
    for f, (kind, target, real) in sorted(trivial.items()):
        print(f"{f:#x} {kind:8} {'; '.join(real)}")
    print(f"{len(trivial)} trivial functions: " +
          ", ".join(f"{k} {n}" for k, n in sorted(kinds.items())))
    return 0


def cmd_apply(trivial):
    fields, rows = load_rows()
    changed = skipped = 0
    for row in rows:
        address = int(row["address"], 16)
        if address not in trivial:
            continue
        if row["status"] in ("verified", "reimplemented") or row["hook"] != "none":
            skipped += 1
            continue
        if row["notes"].startswith(NOTE_PREFIX):
            continue
        kind, target, real = trivial[address]
        note = note_for(kind, target, real)
        row["notes"] = note + (". Before: " + row["notes"] if row["notes"] else "")
        row["status"] = "identified"
        changed += 1
    with open(INVENTORY, "w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    missing = sorted(set(trivial) - {int(r["address"], 16) for r in rows})
    print(f"{changed} rows marked trivial, {skipped} left alone (verified/hooked), "
          f"{len(missing)} without inventory row")
    return 0


def cmd_check(trivial):
    _, rows = load_rows()
    errors = 0
    for row in rows:
        address = int(row["address"], 16)
        marked = row["notes"].startswith(NOTE_PREFIX)
        if marked and address not in trivial:
            print(f"error: {row['address']} is marked trivial but its bytes are not")
            errors += 1
        elif marked and row["status"] != "identified":
            print(f"error: {row['address']} is marked trivial but status is {row['status']}")
            errors += 1
        elif address in trivial and not marked and row["status"] not in ("verified", "reimplemented")                 and row["hook"] == "none":
            print(f"error: {row['address']} is trivial but not marked (run apply)")
            errors += 1
    print(f"trivial check: {len(trivial)} trivial functions, {errors} error(s)")
    return 1 if errors else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("command", choices=("list", "apply", "check"))
    parser.add_argument("--asm", default=os.path.join(ROOT, "build", "s3.asm"))
    args = parser.parse_args()
    if not os.path.exists(args.asm):
        sys.exit(f"{args.asm} not found; see tools/asm_tools.py for how to make it")
    trivial = find_trivial(read_asm(args.asm), load_starts())
    return {"list": cmd_list, "apply": cmd_apply, "check": cmd_check}[args.command](trivial)


if __name__ == "__main__":
    sys.exit(main())
