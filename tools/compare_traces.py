#!/usr/bin/env python3
"""Compare two determinism traces written by `s3re_launcher --trace-determinism`.

Usage: python tools/compare_traces.py <trace a.tsv> <trace b.tsv>

Traces are split into segments (one per game started in that session). The
segments are compared pairwise by tick: the RNG state after each step ("T"
lines) and the region hashes every 32 ticks ("H" lines). Prints the first
divergence of each kind and a summary. Exit code 0 if the overlapping ticks
match exactly, 1 otherwise.
"""

import sys


def load(path):
    segments = []
    current = None
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("# segment"):
                current = {"header": line[2:], "T": {}, "H": {}}
                segments.append(current)
                continue
            if not line or line.startswith("#") or current is None:
                continue
            fields = line.split("\t")
            tick = int(fields[1])
            if fields[0] == "T":
                # Several steps can share a tick if the RNG moved in between;
                # keep them in order.
                current["T"].setdefault(tick, []).append(tuple(fields[2:5]))
            elif fields[0] == "H":
                current["H"][tick] = tuple(fields[2:6])
    return segments


def compare_segment(index, a, b):
    print(f"segment {index}:")
    print(f"  A: {a['header']}")
    print(f"  B: {b['header']}")
    ticks_a, ticks_b = set(a["T"]), set(b["T"])
    common = sorted(ticks_a & ticks_b)
    if not common:
        print("  no ticks in common")
        return False
    print(f"  ticks: A {min(ticks_a)}..{max(ticks_a)}, B {min(ticks_b)}..{max(ticks_b)}, "
          f"{len(common)} in common")

    ok = True
    first_rng = next((t for t in common if a["T"][t] != b["T"][t]), None)
    if first_rng is None:
        print("  RNG state: identical on all common ticks")
    else:
        ok = False
        mismatches = sum(1 for t in common if a["T"][t] != b["T"][t])
        print(f"  RNG state: FIRST DIFFERENCE at tick {first_rng} "
              f"({mismatches} of {len(common)} ticks differ)")
        print(f"    A: {a['T'][first_rng]}")
        print(f"    B: {b['T'][first_rng]}")

    hash_ticks = sorted(set(a["H"]) & set(b["H"]))
    names = ("header", "map", "rest")
    for i, name in enumerate(names):
        first = next((t for t in hash_ticks if a["H"][t][i] != b["H"][t][i]), None)
        if first is None:
            print(f"  {name} hash: identical at all {len(hash_ticks)} hashed ticks")
        else:
            ok = False
            print(f"  {name} hash: FIRST DIFFERENCE at tick {first}")
    rest_sizes = {a["H"][t][3] for t in hash_ticks} | {b["H"][t][3] for t in hash_ticks}
    if len(rest_sizes) > 1:
        print(f"  note: rest region size varied: {sorted(rest_sizes)}")
    return ok


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    a, b = load(sys.argv[1]), load(sys.argv[2])
    print(f"A: {sys.argv[1]} ({len(a)} segment(s))")
    print(f"B: {sys.argv[2]} ({len(b)} segment(s))")
    if not a or not b:
        print("a trace has no segments (did a game run with --trace-determinism?)")
        return 1
    ok = True
    for index in range(min(len(a), len(b))):
        ok &= compare_segment(index + 1, a[index], b[index])
    if len(a) != len(b):
        print(f"segment counts differ ({len(a)} vs {len(b)}); compared the first "
              f"{min(len(a), len(b))}")
    print("RESULT: identical" if ok else "RESULT: different")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
