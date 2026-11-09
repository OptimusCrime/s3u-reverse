#!/usr/bin/env python3
"""Shared loaders for tools/survey_functions.py: per-function features from
build/s3.asm and S3.EXE (calls, imports, string and vtable references).

Function bounds as in tools/asm_tools.py: a function runs from its start to
the next known start (re/ghidra_functions.tsv + re/extra_functions.tsv,
funclets excluded, so exception funclets count as part of the function before
them).
"""

from __future__ import annotations

import bisect
import json
import os
import pickle
import re
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from asm_tools import ROOT, load_inventory, load_starts, read_asm  # noqa: E402

EXE = os.path.join(ROOT, "Settlers3Ultimate", "S3.EXE")
ASM = os.path.join(ROOT, "build", "s3.asm")
IMAGE_BASE = 0x400000

CALL = re.compile(r"(call|jmp)\s+([0-9A-F]{8})$")
JCC = re.compile(r"j[a-z]+\s+([0-9A-F]{8})$")
IAT_REF = re.compile(r"(?:dword ptr )?ds:\[([0-9A-F]{8})h\]")
HEX = re.compile(r"\b0?([0-9A-F]{6,8})h\b")
MOV_REG_IAT = re.compile(r"mov\s+(e[a-z]{2}),dword ptr ds:\[([0-9A-F]{8})h\]")
CALL_REG = re.compile(r"(call|jmp)\s+(e[a-z]{2})$")


def read_pe():
    import pefile
    pe = pefile.PE(EXE, fast_load=True)
    pe.parse_data_directories(directories=[
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
    imports = {}
    for entry in pe.DIRECTORY_ENTRY_IMPORT:
        dll = entry.dll.decode().upper()
        for imp in entry.imports:
            name = imp.name.decode() if imp.name else f"ord{imp.ordinal}"
            imports[imp.address] = (dll, name)
    sections = []
    for s in pe.sections:
        name = s.Name.rstrip(b"\0").decode()
        sections.append((name, IMAGE_BASE + s.VirtualAddress, s.Misc_VirtualSize,
                         s.PointerToRawData, s.SizeOfRawData))
    data = open(EXE, "rb").read()
    return data, sections, imports


def va_to_offset(sections, va):
    for _, start, vsize, raw, rsize in sections:
        if start <= va < start + min(vsize, rsize):
            return raw + va - start
    return None


def string_at(data, sections, va, minimum=3):
    """Latin-1 C string at va, or None if it does not look like text."""
    off = va_to_offset(sections, va)
    if off is None:
        return None
    end = data.find(b"\0", off, off + 400)
    if end < 0:
        return None
    raw = data[off:end]
    if len(raw) < minimum:
        return None
    if any(b < 0x20 and b not in (9, 10, 13) for b in raw) or any(0x7f <= b < 0xa0 for b in raw):
        return None
    high = sum(b >= 0x80 for b in raw)
    if high * 4 > len(raw):
        return None
    letters = sum(chr(b).isalpha() for b in raw)
    if letters * 3 < len(raw) and len(raw) < 8:
        return None
    # The byte before a literal is usually 0 (literals are 4-aligned and padded).
    if off > 0 and data[off - 1] not in (0,) and va % 4:
        return None
    return raw.decode("latin-1")


class Func:
    __slots__ = ("start", "end", "insns", "calls", "tails", "imports", "strings",
                 "consts", "indirect", "jumps_ok", "vtables", "mem")

    def __init__(self, start, end):
        self.start, self.end = start, end
        self.insns = []
        self.calls = []      # direct call targets, in order
        self.tails = set()   # jmp to another function start
        self.imports = []    # (dll, name)
        self.strings = []    # (va, text)
        self.consts = []     # other immediate/absolute addresses in .data/.rdata
        self.indirect = 0
        self.vtables = []    # vtable addresses stored/referenced
        self.mem = []        # raw instruction texts with memory operands (for world fields)


def build(cache=True):
    cache_path = os.path.join(ROOT, "build", "survey_cache.pickle")
    if cache and os.path.exists(cache_path) and \
            os.path.getmtime(cache_path) > os.path.getmtime(ASM):
        with open(cache_path, "rb") as f:
            return pickle.load(f)
    data, sections, imports = read_pe()
    starts = load_starts()
    start_set = set(starts)
    vtables = set()
    classes = json.load(open(os.path.join(ROOT, "re", "classes.json"), encoding="utf-8"))
    vt_owner = {}
    for c in classes["classes"]:
        for vt in c["vtables"]:
            vt_owner[int(vt["address"], 16)] = c["name"]
    vtables = set(vt_owner)
    funcs = {}
    for i, s in enumerate(starts):
        funcs[s] = Func(s, starts[i + 1] if i + 1 < len(starts) else 0x62f000)
    data_lo = min(st for n, st, *_ in sections if n != ".text")
    reg_iat = {}
    string_cache = {}
    for address, ins in read_asm(ASM):
        idx = bisect.bisect_right(starts, address) - 1
        if idx < 0:
            continue
        f = funcs[starts[idx]]
        if ins.startswith("nop") or ins.startswith("int         3"):
            f.insns.append((address, ins))
            continue
        f.insns.append((address, ins))
        m = CALL.match(ins)
        if m:
            t = int(m.group(2), 16)
            if m.group(1) == "call":
                f.calls.append(t)
            elif t in start_set and not (f.start <= t < f.end):
                f.tails.add(t)
            elif not (f.start <= t < f.end):
                f.tails.add(t)
            continue
        m = MOV_REG_IAT.match(ins)
        if m and int(m.group(2), 16) in imports:
            reg_iat[(f.start, m.group(1))] = imports[int(m.group(2), 16)]
        m = IAT_REF.search(ins)
        if m and int(m.group(1), 16) in imports:
            f.imports.append(imports[int(m.group(1), 16)])
        elif ins.startswith("call") or (ins.startswith("jmp") and "*4" not in ins
                                         and not JCC.match(ins)):
            m2 = CALL_REG.match(ins)
            if m2 and (f.start, m2.group(2)) in reg_iat:
                f.imports.append(reg_iat[(f.start, m2.group(2))])
            elif ins.startswith("call") or "ptr" in ins or m2:
                f.indirect += 1
        if "ptr [" in ins:
            f.mem.append(ins)
        for h in HEX.findall(ins):
            v = int(h, 16)
            if v < data_lo or v >= 0x900000:
                continue
            if v in vtables:
                f.vtables.append((v, vt_owner[v]))
                continue
            if v not in string_cache:
                string_cache[v] = string_at(data, sections, v)
            s = string_cache[v]
            if s is not None and not ins.startswith("call"):
                f.strings.append((v, s))
            else:
                f.consts.append(v)
    result = (starts, funcs)
    os.makedirs(os.path.dirname(cache_path), exist_ok=True)
    with open(cache_path, "wb") as fh:
        pickle.dump(result, fh)
    return result


def callers_map(funcs):
    callers = defaultdict(set)
    for f in funcs.values():
        for t in f.calls:
            callers[t].add(f.start)
        for t in f.tails:
            callers[t].add(f.start)
    return callers


if __name__ == "__main__":
    starts, funcs = build(cache=False)
    from collections import Counter
    dlls = Counter(d for f in funcs.values() for d, _ in f.imports)
    print(len(funcs), dlls)
    print(sum(1 for f in funcs.values() if f.strings), "functions with strings")
