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

How (2026-11-23): the bytes are decoded from Settlers3Ultimate/S3.EXE with
capstone, starting exactly at each function start. (Until then the dumpbin
listing build/s3.asm was read; jump tables in .text desync that linear
listing, so 0x53ba50 `mov eax,[007E9168h]; ret` read as a lone `ret`.)
Padding is checked byte by byte (0x90 / 0xCC). The instructions are quoted
in dumpbin's spelling, so notes stay comparable with the listing; `check`
also requires that a marked row's note quotes exactly these instructions.

Usage:
  python tools/trivial.py list            print every trivial function in game code
  python tools/trivial.py apply           record them in re/functions.csv (status
                                          identified + note); never touches verified,
                                          reimplemented or hooked rows
  python tools/trivial.py check           exit 1 if the inventory is out of sync
Options: --exe PATH (default Settlers3Ultimate/S3.EXE)
"""

from __future__ import annotations

import argparse
import csv
import os
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from asm_tools import ROOT, function_end, load_starts  # noqa: E402

CRT_START = 0x61e4ef
GAME_LOW = 0x401000
NOTE_PREFIX = "Trivial, proven by its bytes"
INVENTORY = os.path.join(ROOT, "re", "functions.csv")
EXE = os.path.join(ROOT, "Settlers3Ultimate", "S3.EXE")
PADDING_BYTES = (0x90, 0xCC)  # nop, int 3
MAX_REAL = 3                  # the longest shape: vtordisp, adjust, jmp


def load_image(path=EXE):
    """(image base, memory-mapped image bytes) of S3.EXE."""
    import pefile
    pe = pefile.PE(path, fast_load=True)
    return pe.OPTIONAL_HEADER.ImageBase, pe.get_memory_mapped_image()


def dumpbin_number(value: int) -> str:
    """A constant spelled like dumpbin: 4, 9ECh, 0FFFFFFFFh."""
    if value < 10:
        return str(value)
    text = f"{value:X}"
    return ("0" + text if text[0] in "ABCDEF" else text) + "h"


def decode(code: bytes, address: int):
    """The instructions from `address` up to and including the first ret/jmp,
    as (kind, value, text) tuples, if each is one of the trivial building
    blocks, there are at most MAX_REAL, and only padding bytes follow;
    otherwise None."""
    from capstone import CS_ARCH_X86, CS_MODE_32, Cs
    from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG, X86_REG_AL, \
        X86_REG_EAX, X86_REG_ECX
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    out = []
    for i in md.disasm(code, address):
        if len(out) == MAX_REAL:
            return None
        ops = i.operands
        kinds = [o.type for o in ops]
        m = i.mnemonic
        entry = None
        if any(i.prefix):
            entry = None
        elif m == "ret" and kinds in ([], [X86_OP_IMM]):
            n = ops[0].imm if ops else None
            entry = ("ret", n, "ret" if n is None else f"ret {dumpbin_number(n)}")
        elif m == "jmp" and kinds == [X86_OP_IMM]:
            entry = ("jmp", ops[0].imm, f"jmp {ops[0].imm:08X}")
        elif m in ("xor", "mov", "or", "add", "sub") and len(ops) == 2 \
                and kinds[0] == X86_OP_REG:
            reg = ops[0].reg
            r = {X86_REG_EAX: "eax", X86_REG_AL: "al"}.get(reg)
            if kinds[1] == X86_OP_REG and m == "xor" and ops[1].reg == reg and r:
                entry = ("const", 0, f"xor {r},{r}")
            elif kinds[1] == X86_OP_IMM:
                imm = ops[1].imm & (0xFF if reg == X86_REG_AL else 0xFFFFFFFF)
                if m == "mov" and r:
                    entry = ("const", imm, f"mov {r},{dumpbin_number(imm)}")
                elif m == "or" and reg == X86_REG_EAX and imm == 0xFFFFFFFF:
                    entry = ("const", imm, "or eax,0FFFFFFFFh")
                elif m in ("add", "sub") and reg == X86_REG_ECX:
                    entry = ("adjust", imm, f"{m} ecx,{dumpbin_number(imm)}")
            elif kinds[1] == X86_OP_MEM and m == "sub" and reg == X86_REG_ECX:
                mem = ops[1].mem
                if mem.base == X86_REG_ECX and not mem.index and mem.disp < 0 \
                        and not mem.segment and ops[1].size == 4:
                    entry = ("vtordisp", -mem.disp,
                             f"sub ecx,dword ptr [ecx-{dumpbin_number(-mem.disp)}]")
        if entry is None:
            return None
        out.append(entry)
        if entry[0] in ("ret", "jmp"):
            rest = code[i.address + i.size - address:]
            return out if all(b in PADDING_BYTES for b in rest) else None
    return None


def classify(body, start_set) -> tuple[str, int | None] | None:
    """(kind, forward target or None) when the decoded body is a trivial shape."""
    if body is None:
        return None
    kinds = [k for k, _, _ in body]
    if kinds == ["ret"]:
        return ("leaf", None)
    if kinds == ["const", "ret"]:
        return ("constant", None)
    if kinds[-1] != "jmp" or body[-1][1] not in start_set:
        return None
    if kinds[:-1] in ([], ["adjust"], ["vtordisp"], ["vtordisp", "adjust"]):
        return ("forward", body[-1][1])
    return None


def find_trivial(starts, exe=EXE) -> dict[int, tuple[str, int | None, list[str]]]:
    """{start: (kind, forward target or None, instructions in dumpbin spelling)}
    for every trivial function in game code."""
    base, image = load_image(exe)
    start_set = set(starts)
    out = {}
    for f in starts:
        if not GAME_LOW <= f < CRT_START or f == starts[-1]:
            continue
        end = min(function_end(starts, f), CRT_START)
        body = decode(image[f - base:end - base], f)
        c = classify(body, start_set)
        if c:
            out[f] = (c[0], c[1], [text for _, _, text in body])
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
        elif marked and not row["notes"].startswith(
                f"{NOTE_PREFIX} ({'; '.join(trivial[address][2])})"):
            print(f"error: {row['address']} is marked trivial but its note doesn't quote "
                  f"its instructions ({'; '.join(trivial[address][2])})")
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
    parser.add_argument("--exe", default=EXE)
    args = parser.parse_args()
    if not os.path.exists(args.exe):
        sys.exit(f"{args.exe} not found")
    trivial = find_trivial(load_starts(), args.exe)
    return {"list": cmd_list, "apply": cmd_apply, "check": cmd_check}[args.command](trivial)


if __name__ == "__main__":
    sys.exit(main())
