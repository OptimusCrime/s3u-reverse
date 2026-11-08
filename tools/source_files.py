#!/usr/bin/env python3
"""Recover the original source files of S3.EXE from leftover __FILE__ paths.

S3.EXE has no debug symbols, but it still contains 57 full source paths such
as "E:\\Siedler3\\Volker\\LogicHandleNet.cpp" (leftovers of assert-style
macros; no code references them). The linker lays out each .obj file's data
in one block and its code in one block, in the same order. So:

  1. The data right after a path string (up to the next path, at most
     --window bytes) belongs to that source file.
  2. Every game function that references such strings votes for that file
     (only string literals count: globals are shared between files).
  3. The file order is the same in data and code, so we assign each voting
     function to a file such that file indices never decrease with the code
     address and as many votes as possible agree (dynamic programming; one
     vote per function, split over the files it references).
     The first and last function assigned to a file give its code range.

Results go to re/source_files.csv. Gaps between ranges hold source files
that left no path (most of the simulation, for example). See
re/source_files.md for the notes and the translations.

Needs the full disassembly in build/s3.asm (see tools/asm_tools.py).

Usage:
  python tools/source_files.py scan [--window N]    write re/source_files.csv
  python tools/source_files.py strings [FILTER]     strings stored after each
                                                    path (clues to the content)
  python tools/source_files.py which ADDR [ADDR...] which file an address is in
Options: --asm PATH, --exe PATH
"""

from __future__ import annotations

import argparse
import bisect
import csv
import os
import re
import struct
import sys
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "Settlers3Ultimate", "S3.EXE")
ASM = os.path.join(ROOT, "build", "s3.asm")
OUT = os.path.join(ROOT, "re", "source_files.csv")
IMAGE_BASE = 0x400000
# The statically linked C runtime starts here (see CLAUDE.md); not game code.
CRT_START = 0x61E4EF
PREFIX = "E:\\Siedler3\\"


def read_sections(data: bytes):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    sections = []
    for i in range(count):
        o = pe + 24 + opt_size + 40 * i
        name = data[o:o + 8].rstrip(b"\0").decode()
        vsize, va, raw_size, raw_ptr = struct.unpack_from("<IIII", data, o + 8)
        sections.append((name, IMAGE_BASE + va, vsize, raw_ptr, raw_size))
    return sections


def offset_to_va(sections, offset: int):
    for _, va, _, raw_ptr, raw_size in sections:
        if raw_ptr <= offset < raw_ptr + raw_size:
            return va + offset - raw_ptr
    return None


def section_of(sections, va: int):
    for name, start, vsize, _, _ in sections:
        if start <= va < start + vsize:
            return name
    return None


def find_strings(data: bytes, sections):
    """All printable strings of 4+ characters: list of (va, text).
    The German texts are Latin-1 (Windows-1252), e.g. 0xE4 = ä, 0xFC = ü."""
    out = []
    for m in re.finditer(rb"[\x20-\x7e\xa0-\xff]{4,}", data):
        raw = m.group()
        while raw and raw[0] >= 0x80 and raw[0] not in b"\xc4\xd6\xdc":
            # e.g. a global 0xFFFFFFFF right before a literal: not part of it
            raw = raw[1:]
        high = sum(b >= 0x80 for b in raw)
        if len(raw) - high < 4 or high * 4 > len(raw):
            continue  # binary data that happens to look like Latin-1
        va = offset_to_va(sections, m.end() - len(raw))
        if va is not None:
            out.append((va, raw.decode("latin-1")))
    return out


def find_paths(strings):
    """Source paths, sorted by address: list of (va, relative path)."""
    return sorted((va, s[len(PREFIX):]) for va, s in strings if s.startswith(PREFIX))


def load_function_starts():
    starts = set()
    for name in ("ghidra_functions.tsv", "extra_functions.tsv"):
        with open(os.path.join(ROOT, "re", name)) as f:
            for line in f:
                cols = line.rstrip("\n").split("\t")
                if "funclet" in cols:
                    continue
                try:
                    starts.add(int(cols[0], 16))
                except ValueError:
                    pass
    return sorted(starts)


def load_names():
    names = {}
    path = os.path.join(ROOT, "re", "functions.csv")
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            try:
                names[int(row["address"], 16)] = row["name"]
            except (KeyError, ValueError):
                pass
    return names


def nearest_path(path_vas, value, window, side):
    """Index of the path whose data block holds value, or None."""
    j = bisect.bisect_right(path_vas, value) - 1
    after = j if j >= 0 and value - path_vas[j] <= window else None
    before = (j + 1 if j + 1 < len(path_vas) and path_vas[j + 1] - value <= window
              else None)
    if side == "after":
        return after
    if side == "before":
        return before
    if after is None or before is None:
        return after if before is None else before
    return after if value - path_vas[after] <= path_vas[before] - value else before


def collect_votes(asm_path, paths, sections, starts, window, side, string_vas):
    """votes[function start] = Counter(file index -> references).
    Only references to string literals count (string_vas): globals are
    shared between files, literals are mostly used by their own file."""
    path_vas = [va for va, _ in paths]
    data_sections = {".rdata", ".data"}
    line_re = re.compile(r"^\s+([0-9A-F]{8}):(.*)$")
    # dumpbin prints immediates and addresses as e.g. "6D8AB0h" or "[007E1230h]"
    imm_re = re.compile(r"(?<![0-9A-Fa-f])0*([0-9A-F]{6})h")
    votes = defaultdict(Counter)
    with open(asm_path, errors="replace") as f:
        for line in f:
            m = line_re.match(line)
            if not m:
                continue
            addr = int(m.group(1), 16)
            if addr >= CRT_START:
                continue
            for text in imm_re.findall(m.group(2)):
                value = int(text, 16)
                if string_vas is not None and value not in string_vas:
                    continue
                j = nearest_path(path_vas, value, window, side)
                if j is None:
                    continue
                if section_of(sections, value) not in data_sections:
                    continue
                if section_of(sections, value) != section_of(sections, path_vas[j]):
                    continue
                func = starts[bisect.bisect_right(starts, addr) - 1]
                votes[func][j] += 1
    return votes


def monotone_assignment(votes, n_files):
    """Assign each function to a file, non-decreasing with the address,
    maximizing agreement. Each function has one vote in total, split over
    the files it references (a function that prints 100 messages must not
    outvote its 10 neighbours). Returns {func: file index}."""
    funcs = sorted(votes)
    neg = float("-inf")
    # best[k] = best score so far with the last function assigned to file <= k
    best = [0] * n_files
    choice = []  # per function: list of (score, from-prefix) for back-tracking
    for func in funcs:
        v = votes[func]
        total = sum(v.values())
        here = [best[k] + v.get(k, 0) / total for k in range(n_files)]
        choice.append(here)
        running = neg
        for k in range(n_files):
            running = max(running, here[k])
            best[k] = running
    # back-track
    assignment = {}
    k = n_files - 1
    for i in range(len(funcs) - 1, -1, -1):
        here = choice[i]
        k = max(range(k + 1), key=lambda x: (here[x], x))
        assignment[funcs[i]] = k
    return assignment


def scan(args):
    data = open(args.exe, "rb").read()
    sections = read_sections(data)
    strings = find_strings(data, sections)
    paths = find_paths(strings)
    starts = load_function_starts()
    string_vas = None if args.all_data else {va for va, _ in strings}
    votes = collect_votes(args.asm, paths, sections, starts, args.window,
                          args.side, string_vas)
    assignment = monotone_assignment(votes, len(paths))

    per_file = defaultdict(list)
    for func, k in assignment.items():
        per_file[k].append(func)
    total_votes = sum(sum(v.values()) for v in votes.values())
    agreeing = sum(votes[f][k] for f, k in assignment.items())

    rows = []
    for k, (va, rel) in enumerate(paths):
        funcs = sorted(per_file.get(k, []))
        agree = sum(votes[f][k] for f in funcs)
        total = sum(sum(votes[f].values()) for f in funcs)
        folder, _, name = rel.rpartition("\\")
        rows.append({
            "order": k,
            "folder": folder,
            "file": name,
            "path_string_va": f"0x{va:06x}",
            "section": section_of(sections, va),
            "code_start": f"0x{funcs[0]:06x}" if funcs else "",
            "code_last_function": f"0x{funcs[-1]:06x}" if funcs else "",
            "functions": len(funcs),
            "functions_disagreeing": sum(
                1 for f in funcs if votes[f].most_common(1)[0][0] != k),
            "votes_agreeing": agree,
            "votes_total": total,
        })
    with open(OUT, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]), lineterminator="\n")
        w.writeheader()
        w.writerows(rows)

    print(f"{len(paths)} source paths, {len(votes)} game functions with votes, "
          f"{agreeing}/{total_votes} votes agree with the monotone assignment")
    print(f"{'file':38s} {'code range':>19s} {'funcs':>5s} {'agree':>9s}")
    for r in rows:
        rng = (f"{r['code_start']}-{r['code_last_function']}"
               if r["functions"] else "-")
        print(f"{r['folder'] + '/' + r['file']:38s} {rng:>19s} {r['functions']:5d} "
              f"{r['votes_agreeing']:4d}/{r['votes_total']:<4d}")
    print(f"wrote {os.path.relpath(OUT, ROOT)}")


def load_ranges():
    with open(OUT, newline="") as f:
        rows = [r for r in csv.DictReader(f) if r["code_start"]]
    return [(int(r["code_start"], 16), int(r["code_last_function"], 16),
             f"{r['folder']}/{r['file']}") for r in rows]


def which(args):
    ranges = load_ranges()
    starts = load_function_starts()
    names = load_names()
    for text in args.addresses:
        addr = int(text, 16)
        func = starts[bisect.bisect_right(starts, addr) - 1]
        label = names.get(func, f"FUN_{func:06x}")
        inside = [n for lo, hi, n in ranges if lo <= func <= hi]
        if inside:
            where = inside[0]
        else:
            before = [n for lo, hi, n in ranges if hi < func]
            after = [n for lo, hi, n in ranges if lo > func]
            where = (f"unnamed file(s) between {before[-1] if before else 'start'} "
                     f"and {after[0] if after else 'end'}")
        print(f"0x{addr:06x} ({label}): {where}")


def show_strings(args):
    data = open(args.exe, "rb").read()
    sections = read_sections(data)
    strings = find_strings(data, sections)
    paths = find_paths(strings)
    path_vas = [va for va, _ in paths]
    by_file = defaultdict(list)
    for va, s in strings:
        j = bisect.bisect_right(path_vas, va) - 1
        if j >= 0 and 0 < va - path_vas[j] <= args.window:
            by_file[j].append((va, s))
    for k, (va, rel) in enumerate(paths):
        if args.filter and args.filter.lower() not in rel.lower():
            continue
        print(f"== {rel} (0x{va:06x})")
        for sva, s in by_file[k][:args.limit]:
            print(f"   0x{sva:06x} {s[:100]}")


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--asm", default=ASM)
    p.add_argument("--exe", default=EXE)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("scan")
    s.add_argument("--window", type=lambda x: int(x, 0), default=0x3000,
                   help="max distance of data from a path (default 0x3000)")
    s.add_argument("--side", choices=("after", "before", "nearest"),
                   default="after", help="which path data belongs to (after: best fit)")
    s.add_argument("--all-data", action="store_true",
                   help="count references to any data, not only strings")
    w = sub.add_parser("which")
    w.add_argument("addresses", nargs="+")
    t = sub.add_parser("strings")
    t.add_argument("filter", nargs="?")
    t.add_argument("--window", type=lambda x: int(x, 0), default=0x3000)
    t.add_argument("--limit", type=int, default=25)
    args = p.parse_args()
    {"scan": scan, "which": which, "strings": show_strings}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
