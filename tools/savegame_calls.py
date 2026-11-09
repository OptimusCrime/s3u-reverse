#!/usr/bin/env python3
"""Summarize the call counts of savegame checks (tools/check_save.py).

Usage: python tools/savegame_calls.py [CALLS.tsv ...] [--markdown]

Reads the <save>-calls.tsv files that `check_save.py --against-original`
writes (default: every one in build/x86/bin/Debug/save-check/), maps hook
names to addresses through the S3RE_HOOK lines in src/, and lists the
replacements that were called in a savegame run although their inventory
notes say they were not called in the recorded-game whole-game check:
the new whole-game evidence the savegames give. Changes nothing.
"""

import argparse
import csv
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INVENTORY = os.path.join(ROOT, "re", "functions.csv")
HOOK_RE = re.compile(r"S3RE_HOOK(?:_ALWAYS|_UNVERIFIED)?\(\s*(0x[0-9a-fA-F]+)\s*,\s*(\w+)")
NOT_CALLED = re.compile(r"Not called in the whole-game check")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("files", nargs="*")
    parser.add_argument("--markdown", action="store_true")
    args = parser.parse_args()
    files = args.files or sorted(glob.glob(os.path.join(
        ROOT, "build", "x86", "bin", "Debug", "save-check", "*-calls.tsv")))
    if not files:
        sys.exit("no *-calls.tsv files (run tools/check_save.py --against-original first)")

    addresses = {}
    for folder, _, names in os.walk(os.path.join(ROOT, "src")):
        for name in names:
            if name.endswith(".cpp"):
                text = open(os.path.join(folder, name), encoding="utf-8").read()
                for address, hook in HOOK_RE.findall(text):
                    addresses[hook] = int(address, 16)
    rows = {int(r["address"], 16): r for r in
            csv.DictReader(open(INVENTORY, encoding="utf-8", newline=""))}

    runs = []
    counts = {}
    for path in files:
        lines = open(path, encoding="utf-8").read().splitlines()
        runs.append((os.path.basename(path), lines[0].lstrip("# ")))
        for line in lines[1:]:
            name, n = line.split("\t")
            counts.setdefault(name, []).append(int(n))

    called = {n for n, c in counts.items() if sum(c) > 0}
    new = []
    unchecked = []
    for name in sorted(called):
        row = rows.get(addresses.get(name, -1))
        if row is None:
            continue
        if NOT_CALLED.search(row["notes"]):
            new.append((addresses[name], row["name"], counts[name]))
        elif ("Whole-game check not run yet" in row["notes"]
              or not re.search(r"whole-game", row["notes"], re.IGNORECASE)):
            unchecked.append((addresses[name], row["name"], counts[name]))

    print(f"runs: {'; '.join(f'{f}: {d}' for f, d in runs)}")
    print(f"replacements installed: {len(counts)}, called in at least one savegame run: "
          f"{len(called)}")
    for title, items in (
            ("called here but not in the recorded-game whole-game check", new),
            ("called here; the inventory has no whole-game note for them yet", unchecked)):
        print(f"{title}: {len(items)}")
        for address, name, c in sorted(items):
            joined = " / ".join(str(x) for x in c)
            if args.markdown:
                print(f"| 0x{address:08x} | {name} | {joined} |")
            else:
                print(f"  0x{address:08x} {name} {joined}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
