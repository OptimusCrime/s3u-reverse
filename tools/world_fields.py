#!/usr/bin/env python3
"""Collects the world-object (CGameData) field accesses in the dumpbin
disassembly and groups them by region and record field.

Every memory operand with a register and a displacement inside the world
object (0x2d000 .. 0x153a128, the size passed to operator new in
CLogicThread::OnStart) is taken as world-relative; for record arrays the
field is (displacement - start) % record size. Limits:
  - displacements below 0x2d000 (vtable, kernel, statistics) are not
    collected: they are too small to tell apart from other objects;
  - displacements 0x400000..0x7fffff are skipped (S3.EXE tables);
  - CDrawer fields (0x4a000..0x5f800) overlap the effects table; functions
    using them are dropped below the tile array;
  - tile accesses are recognized by index * 8 scaling, by the folded form
    lea r,[i+i*2+K] (K*8 = field) and by constants close to
    0x67444 + k*0x4800 + j*24; tile neighbours x-1.. are folded back;
  - pointer walks ([esi+4] after lea esi,[...]) and other folded index
    forms (e.g. (n + 0x4a97a) << 6) are not seen.
So the counts are lower bounds; check single fields by hand.

The region table is the one documented in re/world_layout.md (keep both in
sync).

Usage:
  python tools/world_fields.py regions               uses per region
  python tools/world_fields.py fields NAME [--funcs] fields of one region
                                                     (with the functions using them)
  python tools/world_fields.py unknown               displacements outside every region
  python tools/world_fields.py at OFFSET             instructions using one world offset
  python tools/world_fields.py check                 the regions tile the object without
                                                     gaps (no disassembly needed)
Options: --asm PATH (default build/s3.asm)
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asm_tools  # noqa: E402

WORLD_SIZE = 0x153A128
TILES = 0x67444
X_STEP = 0x300 * 24

# (name, start, record size, count). Count * size = region length; records
# that are numbered from 1 still start at `start` (record 0 unused).
REGIONS = [
    ("header", 0x0, 0, 0x38),  # vtable + kernel (not collected, see above)
    ("statistics", 0x38, 0x2400, 20),
    ("scalars", 0x2D038, 0, 0x2E8),  # fields are offsets from 0x2d038
    ("notifications", 0x2D320, 12, 400),
    ("notificationTimers", 0x2E5E0, 12, 420),
    ("pendingEvents", 0x2F990, 12, 1000),
    ("pendingEventIdx", 0x32870, 0, 8),
    ("events", 0x32878, 16, 1000),
    ("eventIdx", 0x366F8, 0, 8),
    ("effects", 0x36700, 20, 10000),
    ("effectsHwm", 0x67440, 0, 4),
    ("tiles", 0x67444, 24, 0x300 * 0x300),
    ("presenceMax", 0xDE7444, 0, 4),
    ("presenceA", 0xDE7448, 20, 48 * 48),
    ("presenceB", 0xDF2848, 20, 48 * 48),
    ("tileRing", 0xDFDC48, 4, 10000),
    ("tileRingIdx", 0xE07888, 0, 8),
    ("tileRing2", 0xE07890, 4, 10000),
    ("tileRing2Idx", 0xE114D0, 0, 8),
    ("ids256Hwm", 0xE114D8, 0, 4),
    ("ids256", 0xE114DC, 1, 256),
    ("areaHwm", 0xE115DC, 0, 4),
    ("ids4096Hwm", 0xE115E0, 0, 4),
    ("ids4096", 0xE115E4, 1, 4096),
    ("ids8192Hwm", 0xE125E4, 0, 4),
    ("ids8192", 0xE125E8, 1, 8192),
    ("areas", 0xE145E8, 0x2E0, 4096),
    ("trades", 0x10F45E8, 0x5A, 1000),
    ("tradeHwm", 0x110A578, 0, 4),
    ("counters110a57c", 0x110A57C, 0, 0xC),
    ("offsetTable", 0x110A588, 16, 0x4E20),
    ("territoryScratch", 0x1158788, 20, 101 * 101),
    ("playerLists", 0x118A47C, 2000, 20),
    ("playerListLengths", 0x11940BC, 40, 20),
    ("players", 0x11943DC, 0x148, 20),
    ("perPlayer1195d7c", 0x1195D7C, 4, 20),
    ("newGameObjective", 0x1195DCC, 0, 8),
    ("recentPositions", 0x1195DD4, 4, 128),
    ("playerRaces", 0x1195FD4, 4, 20),
    ("siteCache", 0x1196024, 0x3C2, 600),
    ("footprintScratch", 0x1222ED4, 0, 0x782),
    ("minimap", 0x1223656, 2, 0xA900),
    ("pad1238856", 0x1238856, 0, 2),
    ("pileHwm", 0x1238858, 0, 4),
    ("piles", 0x123885C, 14, 32000),
    ("settlerHwm", 0x12A5E5C, 0, 4),
    ("settlers", 0x12A5E60, 0x40, 32000),
    ("buildingHwm", 0x1499E60, 0, 4),
    ("buildings", 0x1499E64, 0x3A, 4000),
    ("revealPointHwm", 0x14D28A4, 0, 4),
    ("revealPoints", 0x14D28A8, 16, 50),
    ("drawCells", 0x14D2BC8, 0, 0x668BC),
    ("uiFeedback", 0x1539484, 4, 10),
    ("uiFeedbackCount", 0x15394AC, 0, 4),
    ("flag15394b0", 0x15394B0, 0, 1),
    ("victoryConditions", 0x15394B1, 0, 0x77),
    ("reciprocals", 0x1539528, 4, 768),
]

MEM = re.compile(r"(byte|word|dword|qword|fword|tbyte) ptr \[([^\]]+)\]|lea\s+\w+,\[([^\]]+)\]")
DISP = re.compile(r"([0-9A-F]+)h$")
LEA3 = re.compile(r"lea\s+(e\w\w),\[(e\w\w)\+\2\*2\+([0-9A-F]+)h\]")


def region_of(off):
    for name, start, size, count in REGIONS:
        length = size * count if size else count
        if start <= off < start + length:
            return name, start, size
    return None, None, None


def parse_disp(expr):
    """Displacement of a memory expression with at least one register."""
    if not re.search(r"\be[a-ds][xpi]\b|\be[sd]i\b|\bebp\b|\besp\b", expr):
        return None
    if "esp" in expr.split("+")[0] and "*" not in expr:
        return None  # stack
    parts = expr.replace("-", "+-").split("+")
    last = parts[-1].strip()
    m = DISP.match(last)
    if not m:
        return None
    value = int(m.group(1), 16)
    if value >= 0x80000000:
        return None
    return value


def collect(asm_path):
    asm = asm_tools.read_asm(asm_path)
    starts = asm_tools.load_starts()
    inv = asm_tools.load_inventory()
    hits = []  # (addr, func, offset, width, kind, text)
    for addr, text in asm:
        m = MEM.search(text)
        if not m:
            continue
        expr = m.group(2) or m.group(3)
        width = m.group(1) or "lea"
        disp = parse_disp(expr)
        if disp is None:
            continue
        # folded tile form: lea r,[i+i*2+K], K*8 in the tile array
        lm = LEA3.match(text)
        if lm:
            k = int(lm.group(3), 16) * 8
            if 0x67444 - 0x4800 <= k < 0xDE7444 + 0x4800:
                func = asm_tools.containing(starts, addr)
                hits.append((addr, func, k, "folded", "r", text))
            continue
        if disp < 0x2D000 or disp >= WORLD_SIZE:
            continue
        if 0x400000 <= disp < 0x800000:
            continue  # S3.EXE image addresses (tables indexed by a register)
        op = text.split()[0]
        kind = "r"
        if op in ("mov", "movsx", "movzx") and text.split(None, 1)[1].lstrip().startswith(width + " ptr"):
            kind = "w"
        elif op in ("inc", "dec", "add", "sub", "or", "and", "xor", "shl", "shr", "neg", "not", "adc", "sbb", "rol", "ror", "sar") and text.split(None, 1)[1].startswith(width + " ptr"):
            kind = "rw"
        elif op == "lea":
            kind = "lea"
        func = asm_tools.containing(starts, addr)
        hits.append((addr, func, disp, width, kind, text))
    return postprocess(hits), inv


def near_tile_constant(off):
    """True if off = 0x67444 + k * 0x4800 + j * 24 + field for small k, j."""
    d = off - TILES
    k = round(d / X_STEP)
    r = d - k * X_STEP
    return abs(k) <= 12 and -12 * 24 <= r < 13 * 24


def postprocess(hits):
    """Separates CDrawer fields and x-1.. tile neighbours below 0x67444.

    CDrawer (vtable 0x656aec) has fields around 0x4b000..0x5f800, which
    overlap table36700's range; functions using such unscaled
    displacements are taken as CDrawer code and their hits below the tile
    array are dropped. Displacements below 0x67444 that are scaled by 8
    (tile index * 3 * 8) are tile accesses of a neighbour x-k (kept as
    tiles; their field is computed modulo 24 from 0x67444).
    """
    drawer = set()
    for addr, func, off, width, kind, text in hits:
        if 0x4A000 <= off < 0x5F800 and "*" not in text:
            drawer.add(func)
    tile_funcs = {h[1] for h in hits if TILES <= h[2] < TILES + 0x300 * X_STEP}
    out = []
    for h in hits:
        addr, func, off, width, kind, text = h
        if 0x36700 <= off < TILES + 0x300 * X_STEP and width != "folded":
            if off < TILES and "*4" in text:
                pass  # table36700 (20-byte records: index * 5 * 4)
            elif "*8" not in text and not near_tile_constant(off):
                continue  # index arithmetic or another object, not a tile field
        if off < TILES and off >= 0x36700:
            scaled8 = "*8" in text
            near = off >= TILES - 12 * X_STEP and func in tile_funcs and "*4" not in text
            if func in drawer and not scaled8:
                continue
            if scaled8 or near:
                k = (TILES - off + X_STEP - 1) // X_STEP
                out.append((addr, func, off + k * X_STEP, width, kind, text))
                continue
        out.append(h)
    return out


def fname(inv, func):
    row = inv.get(func)
    return row["name"] if row else f"sub_{func:x}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd")
    ap.add_argument("arg", nargs="?")
    ap.add_argument("--asm", default=os.path.join(asm_tools.ROOT, "build", "s3.asm"))
    ap.add_argument("--funcs", action="store_true")
    a = ap.parse_args()
    if a.cmd == "check":
        pos = 0
        for name, start, size, count in REGIONS:
            if start != pos:
                sys.exit(f"gap or overlap before {name}: {pos:#x} != {start:#x}")
            pos = start + (size * count if size else count)
        if pos != WORLD_SIZE:
            sys.exit(f"regions end at {pos:#x}, object size {WORLD_SIZE:#x}")
        print(f"{len(REGIONS)} regions tile 0..{WORLD_SIZE:#x}")
        return
    hits, inv = collect(a.asm)
    out = sys.stdout
    if a.cmd == "regions":
        per = defaultdict(lambda: [0, set()])
        for addr, func, off, width, kind, text in hits:
            name, _, _ = region_of(off)
            per[name or "?"][0] += 1
            per[name or "?"][1].add(func)
        for name, start, size, count in REGIONS:
            n, fs = per.get(name, [0, set()])
            print(f"{start:#09x} {name:20s} rec={size:#x} n={count} uses={n} funcs={len(fs)}", file=out)
        print(f"? outside {per['?'][0]}", file=out)
    elif a.cmd == "fields":
        region = [r for r in REGIONS if r[0] == a.arg][0]
        _, start, size, count = region
        fields = defaultdict(lambda: defaultdict(int))
        funcs = defaultdict(set)
        for addr, func, off, width, kind, text in hits:
            name, _, _ = region_of(off)
            if name != a.arg:
                continue
            f = (off - start) % size if size else off - start
            fields[f][f"{width}:{kind}"] += 1
            funcs[f].add(func)
        for f in sorted(fields):
            desc = " ".join(f"{k}={v}" for k, v in sorted(fields[f].items()))
            line = f"+{f:#06x} {desc}"
            if a.funcs:
                names = sorted(fname(inv, x) for x in funcs[f])
                line += "\n      " + ", ".join(names[:25]) + (" ..." if len(names) > 25 else "")
            else:
                line += f"  funcs={len(funcs[f])}"
            print(line, file=out)
    elif a.cmd == "unknown":
        c = defaultdict(int)
        for addr, func, off, width, kind, text in hits:
            if region_of(off)[0] is None:
                c[off] += 1
        for off in sorted(c):
            print(f"{off:#x} {c[off]}", file=out)
    elif a.cmd == "at":
        target = int(a.arg, 16)
        for addr, func, off, width, kind, text in hits:
            if off == target:
                print(f"{addr:08x} {fname(inv, func):40s} {text}", file=out)


if __name__ == "__main__":
    main()
