#!/usr/bin/env python3
"""Queries over the full dumpbin disassembly of S3.EXE.

Make the disassembly once (from PowerShell; Git Bash mangles /options):
  & "<VS>\\VC\\Tools\\MSVC\\14.50.35717\\bin\\Hostx64\\x86\\dumpbin.exe" `
      /disasm:nobytes /out:build\\s3.asm Settlers3Ultimate\\S3.EXE
Function boundaries come from re/ghidra_functions.tsv + re/extra_functions.tsv
(a function ends where the next one starts), names from re/functions.csv.

Usage (ASM defaults to build/s3.asm):
  python tools/asm_tools.py disasm ADDR [ADDR...]   disassemble whole functions
  python tools/asm_tools.py leaves [LOW HIGH]       small functions without calls
                                                    (default 0x4d0000..0x560000),
                                                    not yet verified
  python tools/asm_tools.py eax-use ADDR [ADDR...]  how callers use EAX after
                                                    calling ADDR (READ = they use the
                                                    result; PARTIAL = they only
                                                    overwrite AL/AX, so the rest of
                                                    EAX may matter; flow = a jump or
                                                    ret follows: check by hand)
  python tools/asm_tools.py xrefs REGEX [REGEX...]  functions whose instructions
                                                    match (e.g. a field offset like
                                                    "2D2A0h\\]"), with read/write
                                                    counts
  python tools/asm_tools.py callers ADDR            functions that call ADDR
Options: --asm PATH
"""

from __future__ import annotations

import argparse
import bisect
import csv
import os
import re
import sys
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load_starts() -> list[int]:
    starts = set()
    for name in ("ghidra_functions.tsv", "extra_functions.tsv"):
        for line in open(os.path.join(ROOT, "re", name), encoding="utf-8"):
            if line.startswith("0x") and "funclet" not in line:
                starts.add(int(line.split("\t")[0], 16))
    return sorted(starts)


def load_false_starts() -> set[int]:
    """Starts that are not functions (re/false_starts.tsv)."""
    path = os.path.join(ROOT, "re", "false_starts.tsv")
    if not os.path.exists(path):
        return set()
    return {int(line.split("	")[0], 16) for line in open(path, encoding="utf-8") if line.startswith("0x")}


def load_inventory() -> dict[int, dict]:
    rows = {}
    with open(os.path.join(ROOT, "re", "functions.csv"), encoding="utf-8") as f:
        for row in csv.DictReader(f):
            rows[int(row["address"], 16)] = row
    return rows


def read_asm(path: str) -> list[tuple[int, str]]:
    """(address, instruction text) for every instruction line."""
    out = []
    for line in open(path, encoding="utf-8", errors="replace"):
        if len(line) > 12 and line[10] == ":":
            try:
                out.append((int(line[2:10], 16), line[12:].rstrip()))
            except ValueError:
                pass
    return out


def containing(starts: list[int], address: int) -> int:
    return starts[bisect.bisect_right(starts, address) - 1]


def function_end(starts: list[int], start: int) -> int:
    return starts[bisect.bisect_right(starts, start)]


def cmd_disasm(asm, starts, inventory, addresses):
    for text in addresses:
        start = int(text, 16)
        end = function_end(starts, start)
        name = inventory.get(start, {}).get("name", "")
        print(f"===== {start:#x} {name}")
        for address, ins in asm:
            if start <= address < end and not ins.startswith("nop"):
                print(f"{address:08X}: {ins}")


def cmd_leaves(asm, starts, inventory, low, high, max_instructions=60):
    stats = defaultdict(lambda: {"n": 0, "calls": 0, "fs": 0, "ret": None})
    for address, ins in asm:
        if not low <= address < high or ins.startswith("nop"):
            continue
        d = stats[containing(starts, address)]
        d["n"] += 1
        d["calls"] += ins.startswith("call")
        d["fs"] += "fs:" in ins
        m = re.match(r"ret\s*(\w*)", ins)
        if m:
            d["ret"] = m.group(1) or "0"
    for start, d in sorted(stats.items()):
        row = inventory.get(start, {})
        if d["calls"] or d["fs"] or not 4 <= d["n"] <= max_instructions:
            continue
        if row.get("status") == "verified":
            continue
        print(f"{start:#x} {d['n']:3} instructions, ret {d['ret']:4} {row.get('name', '')}")


REG = r"\b(eax|ax|al|ah)\b"


def classify_after_call(asm, index):
    for _, ins in asm[index + 1:index + 12]:
        m = re.match(r"(\w+)\s*(.*)", ins)
        if not m:
            continue
        op, operands = m.groups()
        dst, _, src = operands.partition(",")
        if op in ("jmp", "ret"):
            return "flow", ins
        if op == "call":
            return "clobbered", ins
        if op == "xor" and dst.strip() == "eax" and src.strip() == "eax":
            return "clobbered", ins
        reads_dst = op in ("cmp", "test", "push", "add", "sub", "and", "or", "xor", "inc", "dec",
                           "imul", "shl", "shr", "sar", "neg")
        if re.search(REG, src) or (reads_dst and re.search(REG, dst)) or \
                re.search(r"\[[^\]]*" + REG, dst):
            return "READ", ins
        if op in ("mov", "movsx", "movzx", "lea", "pop") and dst.strip() == "eax":
            return "clobbered", ins
        if op == "mov" and dst.strip() in ("ax", "al", "ah"):
            return "PARTIAL", ins
    return "unknown", ""


def cmd_eax_use(asm, starts, inventory, addresses):
    for text in addresses:
        target = f"call        {int(text, 16):08X}"
        counts, examples = Counter(), []
        for i, (address, ins) in enumerate(asm):
            if ins.startswith(target):
                kind, follow = classify_after_call(asm, i)
                counts[kind] += 1
                if kind != "clobbered" and len(examples) < 4:
                    examples.append(f"{address:08X}: {kind}: {follow}")
        print(f"{int(text, 16):#x}: {dict(counts)}")
        for e in examples:
            print(f"    {e}")


def cmd_xrefs(asm, starts, inventory, patterns):
    rx = re.compile("|".join(patterns))
    found = defaultdict(Counter)
    for address, ins in asm:
        if rx.search(ins):
            write = bool(re.match(r"(mov|add|sub|inc|dec|and|or|xor|neg|not|shl|shr|sar)\s+"
                                  r"(byte|word|dword) ptr \[", ins)) and \
                    rx.search(ins.split(",")[0])
            found[containing(starts, address)]["W" if write else "R"] += 1
    for start, c in sorted(found.items()):
        print(f"{start:#x} {inventory.get(start, {}).get('name', ''):45} {dict(c)}")


def cmd_callers(asm, starts, inventory, address_text):
    target = f"call        {int(address_text, 16):08X}"
    for address, ins in asm:
        if ins.startswith(target):
            f = containing(starts, address)
            print(f"{address:#x} in {f:#x} {inventory.get(f, {}).get('name', '')}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--asm", default=os.path.join(ROOT, "build", "s3.asm"))
    parser.add_argument("command", choices=["disasm", "leaves", "eax-use", "xrefs", "callers"])
    parser.add_argument("args", nargs="*")
    args = parser.parse_args()
    if not os.path.exists(args.asm):
        sys.exit(f"{args.asm} not found; see the docstring for how to make it")
    asm = read_asm(args.asm)
    starts = load_starts()
    inventory = load_inventory()
    if args.command == "disasm":
        cmd_disasm(asm, starts, inventory, args.args)
    elif args.command == "leaves":
        low, high = (int(args.args[0], 16), int(args.args[1], 16)) if args.args \
            else (0x4d0000, 0x560000)
        cmd_leaves(asm, starts, inventory, low, high)
    elif args.command == "eax-use":
        cmd_eax_use(asm, starts, inventory, args.args)
    elif args.command == "xrefs":
        cmd_xrefs(asm, starts, inventory, args.args)
    else:
        cmd_callers(asm, starts, inventory, args.args[0])
    return 0


if __name__ == "__main__":
    sys.exit(main())
