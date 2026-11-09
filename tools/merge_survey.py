#!/usr/bin/env python3
"""Adds the rows of re/function_survey.csv (written by tools/survey_functions.py)
to re/functions.csv as new rows.

- Existing rows are never changed; survey rows for addresses that already have
  a row are skipped (the inventory wins).
- status: `identified` when the survey confidence is high and the name is
  meaningful (not a `<Subsystem>_sub_<addr>` placeholder or a `...::sub_<addr>` /
  `ctor_or_dtor_<addr>` name); otherwise `classified`. hook none, no verified_by.
- notes: "Survey (<confidence>; subsystem <S>; source <F>): <evidence>".
- Categories that inventory.py does not allow are mapped to `game`.
- A name that is already used gets the suffix _<address>.
- Rows stay sorted by address; the file keeps its CSV format (LF, minimal
  quoting, the same column order). Unchanged rows are written back byte for
  byte as the csv module wrote them (the inventory is written by the same
  module).

Usage:
  python tools/merge_survey.py [--inventory PATH] [--survey PATH] [--output PATH]
                               [--min-confidence low|medium|high] [--skip-unknown]
                               [--dry-run]
By default it rewrites re/functions.csv in place; run `python tools/inventory.py
check` afterwards.
"""

from __future__ import annotations

import argparse
import csv
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import inventory  # noqa: E402

LEVELS = {"low": 0, "medium": 1, "high": 2}
PLACEHOLDER = re.compile(r"(^[A-Za-z]+(_[A-Za-z0-9]+)?_sub_[0-9a-f]{6}$|::sub_[0-9a-f]{6}$|"
                         r"ctor_or_dtor_[0-9a-f]{6}$|_sub_[0-9a-f]{6}$)")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--inventory", default=str(inventory.INVENTORY))
    ap.add_argument("--survey", default=os.path.join(ROOT, "re", "function_survey.csv"))
    ap.add_argument("--output", default=None, help="default: overwrite --inventory")
    ap.add_argument("--min-confidence", choices=list(LEVELS), default="low")
    ap.add_argument("--skip-unknown", action="store_true",
                    help="leave out rows whose subsystem is Unknown")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    with open(args.inventory, encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        columns = list(reader.fieldnames or [])
        rows = list(reader)
    if columns != inventory.COLUMNS:
        sys.exit(f"unexpected inventory header {columns}")
    baseline = inventory.load_baseline()
    have = {int(r["address"], 16) for r in rows}
    names = {r["name"] for r in rows}

    added = skipped = 0
    stats: dict[str, int] = {}
    with open(args.survey, encoding="utf-8", newline="") as f:
        for s in csv.DictReader(f):
            address = int(s["address"], 16)
            if address in have or address not in baseline:
                skipped += 1
                continue
            if LEVELS[s["confidence"]] < LEVELS[args.min_confidence]:
                skipped += 1
                continue
            if args.skip_unknown and s["subsystem"].startswith("Unknown"):
                skipped += 1
                continue
            name = s["name"]
            if name in names:
                name = f"{name}_{address:06x}"
            category = s["category"] if s["category"] in inventory.CATEGORIES else "game"
            status = ("identified" if s["confidence"] == "high" and not PLACEHOLDER.search(name)
                      else "classified")
            where = [s["confidence"]]
            if s["subsystem"]:
                where.append(f"subsystem {s['subsystem']}")
            if s["source_file"]:
                where.append(f"source {s['source_file']}")
            notes = f"Survey ({'; '.join(where)}): {s['evidence']}"
            if s["category"] != category:
                notes = f"[{s['category']}] " + notes
            rows.append({"address": f"0x{address:08x}", "name": name, "category": category,
                         "status": status, "hook": "none", "verified_by": "", "notes": notes})
            have.add(address)
            names.add(name)
            added += 1
            stats[status] = stats.get(status, 0) + 1

    rows.sort(key=lambda r: int(r["address"], 16))
    buf = io.StringIO()
    writer = csv.DictWriter(buf, fieldnames=columns, lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)
    total = len(baseline)
    print(f"added {added} rows ({stats}), skipped {skipped}; inventory now {len(rows)} rows "
          f"= {100 * len(rows) / total:.1f}% of {total} known functions")
    if not args.dry_run:
        with open(args.output or args.inventory, "w", encoding="utf-8", newline="") as f:
            f.write(buf.getvalue())
    return 0


if __name__ == "__main__":
    sys.exit(main())
