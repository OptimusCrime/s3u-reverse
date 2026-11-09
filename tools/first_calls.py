#!/usr/bin/env python3
"""When were our replacements first called in a run? (how long must a run be?)

Usage: python tools/first_calls.py S3RE_LOG [--base TICK] [--list]

At exit s3re.dll logs, for each counted replacement that was called, the
game tick of its first call ("first-call <name> <tick>", see hooks.h). This
prints how many replacements were first called in each tick range, counted
from the tick the run started at (--base; by default the one in the
"--exit-after-ticks" log line, 0 for a new game). Calls during loading count
as tick 0. --list prints every function with its tick.
"""

import argparse
import re
import sys

FIRST_CALL = re.compile(r"\] first-call (\S+) (\d+)$", re.M)
EXIT_BASE = re.compile(r"\(\d+ ticks after (\d+)\); ending the game")
MARKS = (0, 100, 500, 1000, 2000, 3000)


def first_calls(log_text, base=None):
    """{name: ticks after the run's start} and the base tick used."""
    if base is None:
        m = EXIT_BASE.search(log_text)
        base = int(m.group(1)) if m else 0
    return {name: max(int(tick) - base, 0)
            for name, tick in FIRST_CALL.findall(log_text)}, base


def summary(calls):
    """Lines: how many functions were first called after each mark."""
    if not calls:
        return ["  first calls: none logged"]
    lines = [f"  first calls of {len(calls)} called replacements, by ticks after the start:"]
    for mark in MARKS:
        later = sorted((t, n) for n, t in calls.items() if t > mark)
        if not later:
            lines.append(f"    after {mark:5}: 0")
            break
        names = ", ".join(f"{n} ({t})" for t, n in later[:6])
        more = f", ... {len(later) - 6} more" if len(later) > 6 else ""
        lines.append(f"    after {mark:5}: {len(later):3}  {names}{more}")
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("log")
    parser.add_argument("--base", type=int)
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()
    calls, base = first_calls(open(args.log, encoding="utf-8", errors="replace").read(),
                              args.base)
    print(f"base tick {base}")
    print("\n".join(summary(calls)))
    if args.list:
        for name, tick in sorted(calls.items(), key=lambda item: (item[1], item[0])):
            print(f"{tick}\t{name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
