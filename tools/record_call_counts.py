#!/usr/bin/env python3
"""Record the call counts of a whole-game check in the inventory notes.

After `tools/check_replay.py --against-original` passed, s3re.log (next to
the DLL) holds one "calls <name> <n>" line per replacement for the replay
with our code. This script maps the names to addresses through the
S3RE_HOOK lines in src/ and, for every verified row whose notes still say
"Whole-game check not run yet (...)." or that have no whole-game note at
all, writes the result:
  "Whole-game check (RECORDING, seed S, T ticks) identical to the original, N calls."
  or "Not called in the whole-game check (RECORDING, T ticks), so no whole-game evidence."
Rows that already carry a whole-game note are left alone.

It also merges every count (including 0) into re/whole_game_calls.csv as
source replay:<recording name> (the file name without its extension;
tools/whole_game.py; the structured
record that tools/progress.py reads). Run it only after a check that
passed.

Usage: python tools/record_call_counts.py --recording NAME --seed S --ticks T [--log PATH]
"""

import argparse
import csv
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import whole_game  # noqa: E402
INVENTORY = os.path.join(ROOT, "re", "functions.csv")
HOOK_RE = re.compile(r"S3RE_HOOK(?:_ALWAYS)?\(\s*(0x[0-9a-fA-F]+)\s*,\s*(\w+)")
PENDING = re.compile(r"\s*Whole-game check not run yet \([^)]*\)\.")
CALLS = re.compile(r"\bcalls (\w+) (\d+)$")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--recording", required=True)
    parser.add_argument("--seed", required=True)
    parser.add_argument("--ticks", type=int, required=True)
    parser.add_argument("--log", default=os.path.join(ROOT, "build", "x86", "bin", "Debug", "s3re.log"))
    args = parser.parse_args()

    names = {}
    for folder, _, files in os.walk(os.path.join(ROOT, "src")):
        for name in files:
            if name.endswith(".cpp"):
                text = open(os.path.join(folder, name), encoding="utf-8").read()
                for address, hook in HOOK_RE.findall(text):
                    names[hook] = int(address, 16)
    counts = {}
    for line in open(args.log, encoding="utf-8", errors="replace"):
        m = CALLS.search(line.rstrip())
        if m:
            counts[m.group(1)] = int(m.group(2))
    if not counts:
        sys.exit(f"no 'calls' lines in {args.log}")
    by_address = {names[n]: c for n, c in counts.items() if n in names}

    with open(INVENTORY, encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        fields, rows = reader.fieldnames, list(reader)
    changed = 0
    for row in rows:
        address = int(row["address"], 16)
        if row["status"] != "verified" or address not in by_address:
            continue
        notes = row["notes"]
        pending = PENDING.search(notes)
        if not pending and re.search(r"[Ww]hole-game|scripted-seed127", notes):
            continue
        n = by_address[address]
        text = (f"Whole-game check ({args.recording}, seed {args.seed}, {args.ticks:,} ticks) "
                f"identical to the original, {n:,} calls." if n else
                f"Not called in the whole-game check ({args.recording}, {args.ticks:,} ticks), "
                "so no whole-game evidence.")
        notes = PENDING.sub("", notes).rstrip()
        row["notes"] = (notes + " " + text).strip()
        changed += 1
    with open(INVENTORY, "w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    stem = os.path.splitext(os.path.basename(args.recording))[0]
    added, updated = whole_game.record(f"replay:{stem}", args.ticks,
                                       by_address, note=f"seed {args.seed}")
    print(f"{len(counts)} call counts read, {changed} rows updated")
    print(f"re/whole_game_calls.csv: {added} rows added, {updated} updated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
