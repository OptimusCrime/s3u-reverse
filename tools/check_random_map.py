#!/usr/bin/env python3
"""Whole-game check on random maps, unattended.

Usage: python tools/check_random_map.py [--maps SIZE[,SYM][;SIZE[,SYM]...]]
                                        [--seed N] [--ticks N] [--map FILE]
                                        [--against-original] [--skip-hooks NAMES]
                                        [--build B]

Every run starts a game on a random map without menus (s3re_launcher
--random-map SIZE,SYM --seed N, see src/s3re/tooling/random_map_start.cpp),
so the game's random map generator (CGameData 0x5166d0, our
CGameData::GenerateRandomMap) builds the landscape; the player setup comes from --map
(default Map\\MULTI\\384-2-Lakes.map: one human who does nothing, one
computer player). Each run writes the determinism trace for N ticks.

Without --against-original: each map is run twice with the original code
(--original) and the traces are compared: is a random-map game
deterministic?

With --against-original: each map is run once with the original code
(reference) and once with our reimplementations, and the traces are
compared: the whole-game check for the random map generator and everything
else the game calls. If they are identical, the call counts are merged into
re/whole_game_calls.csv as source "random:SIZE,SYM seed N"
(tools/whole_game.py). Call counts per map also go to
build/x86/bin/<build>/random-map-check/.

--skip-hooks NAMES (with --against-original) leaves these replacements out
of the run with our code (s3re_launcher --skip-hooks): to find which
replacement makes a game diverge, or to count our callees of an original
function (e.g. --skip-hooks CGameData_GenerateRandomMap: the original generator then
calls our ExpandTerrain and MarkTerrainEdge through the counting thunk).
Such runs are recorded with the source suffix " skip NAMES".

Every game run takes the game lock (tools/game_lock.py). The game window
takes over the screen while it runs; don't touch mouse or keyboard. Exit
code 0 if all traces are identical.
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import game_lock  # noqa: E402
import whole_game  # noqa: E402

DEFAULT_MAPS = "384,0;448,3"
DEFAULT_SEED = 1234


def run_game(launcher, bin_dir, random_map, seed, ticks, game_map, extra, label):
    before = set(glob.glob(os.path.join(bin_dir, "determinism-*.tsv")))
    timeout = 120 + ticks // 3
    command = [launcher, "--trace-determinism", "--random-map", random_map,
               "--seed", str(seed), "--exit-after-ticks", str(ticks),
               "--timeout", str(timeout), *extra,
               *(["--", "-map", game_map] if game_map else [])]
    print(f"{label}: random map {random_map}, seed {seed}, {ticks} ticks", flush=True)
    with game_lock.held("check_random_map"):
        result = subprocess.run(command, capture_output=True, text=True)
    time.sleep(2)  # trace file names have one-second resolution
    new = sorted(set(glob.glob(os.path.join(bin_dir, "determinism-*.tsv"))) - before)
    log = open(os.path.join(bin_dir, "s3re.log"), encoding="utf-8", errors="replace").read()
    for line in log.splitlines():
        if "random-map:" in line or "ending the game" in line:
            print("   ", line.split("] ", 1)[-1])
    if "random-map: size" not in log:
        print("the random map was not set up (no 'random-map:' line in s3re.log)")
        return None, log
    if result.returncode != 0 or len(new) != 1:
        print(result.stdout + result.stderr)
        print(f"run failed (exit code {result.returncode}, {len(new)} new trace file(s))")
        return None, log
    return new[0], log


def trace_ticks(path):
    """First and last tick in a determinism trace."""
    ticks = []
    for line in open(path, encoding="utf-8", errors="replace"):
        fields = line.split("\t")
        if fields[0] == "T" and len(fields) > 1:
            ticks.append(int(fields[1]))
    return (ticks[0], ticks[-1]) if ticks else (None, None)


def same_traces(a, b):
    result = subprocess.run([sys.executable, "-I",
                             os.path.join(ROOT, "tools", "compare_traces.py"), a, b],
                            capture_output=True, text=True)
    return result.returncode == 0, result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--maps", default=DEFAULT_MAPS,
                        help=f"random maps as SIZE[,SYMMETRY], separated by ';' "
                             f"(default {DEFAULT_MAPS})")
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--ticks", type=int, default=1000)
    parser.add_argument("--map", help="map file for the player setup (launcher default: "
                                      r"Map\MULTI\384-2-Lakes.map)")
    parser.add_argument("--against-original", action="store_true")
    parser.add_argument("--skip-hooks", help="replacements to leave out of the run with our code")
    parser.add_argument("--no-fast", dest="fast", action="store_false",
                        help="run in real time instead of with --fast "
                             "(src/s3re/tooling/fast.cpp)")
    parser.add_argument("--build", default="Debug")
    args = parser.parse_args()

    bin_dir = os.path.join(ROOT, "build", "x86", "bin", args.build)
    launcher = os.path.join(bin_dir, "s3re_launcher.exe")
    if not os.path.exists(launcher):
        print(f"launcher not found: {launcher} (build first)")
        return 2
    work = os.path.join(bin_dir, "random-map-check")
    os.makedirs(work, exist_ok=True)

    skip = ["--skip-hooks", args.skip_hooks] if args.skip_hooks else []
    runs = ([("original", ["--original"]), ("ours", skip)] if args.against_original
            else [("original run 1", ["--original"]), ("original run 2", ["--original"])])
    all_ok = True
    for random_map in [m.strip() for m in args.maps.split(";") if m.strip()]:
        parts = random_map.split(",")
        size, symmetry = int(parts[0]), int(parts[1]) if len(parts) > 1 else 0
        stem = f"random-{size}-{symmetry}-seed{args.seed}" + ("-skip" if args.skip_hooks else "")
        traces, logs = [], []
        for label, extra in runs:
            trace, log = run_game(launcher, bin_dir, f"{size},{symmetry}", args.seed,
                                  args.ticks, args.map,
                                  extra + (["--fast"] if args.fast else []), label)
            if trace is None:
                return 2
            copy = os.path.join(work, f"{stem}-{label.replace(' ', '-')}.tsv")
            shutil.copyfile(trace, copy)
            first, last = trace_ticks(copy)
            print(f"    trace {copy}: ticks {first}..{last}")
            traces.append(copy)
            logs.append(log)
        ok, report = same_traces(traces[0], traces[1])
        all_ok = all_ok and ok
        print(f"  {size},{symmetry}: {runs[0][0]} vs {runs[1][0]}: "
              f"{'identical' if ok else 'DIFFERENT'}", flush=True)
        if not ok:
            print(report)
        if args.against_original:
            calls = {m.group(1): int(m.group(2))
                     for m in re.finditer(r"\] calls (\S+) (\d+)", logs[1])}
            if not calls:
                continue
            called = sorted(n for n, c in calls.items() if c > 0)
            print(f"  replaced functions called in this game: {len(called)} of {len(calls)}")
            for name in ("CGameData_GenerateRandomMap", "CGameData_ExpandTerrain",
                         "CGameData_MarkTerrainEdge", "CGameData_BuildRegionsAndAreas"):
                if name in calls:
                    print(f"    {name}: {calls[name]}")
            first, last = trace_ticks(traces[1])
            out = os.path.join(work, f"{stem}-calls.tsv")
            with open(out, "w", encoding="utf-8", newline="\n") as f:
                f.write(f"# random map {size},{symmetry} seed {args.seed} ticks {first}..{last} "
                        f"{'identical' if ok else 'DIFFERENT'}\n")
                for name in sorted(calls):
                    f.write(f"{name}\t{calls[name]}\n")
            print(f"  call counts: {out}")
            if ok and first is not None:
                added, updated = whole_game.record_named(
                    f"random:{size},{symmetry} seed {args.seed}"
                    + (f" skip {args.skip_hooks}" if args.skip_hooks else ""),
                    last - first + 1, calls)
                print(f"  re/whole_game_calls.csv: {added} rows added, {updated} updated")

    print("RANDOM MAP CHECK OK" if all_ok else "RANDOM MAP CHECK FAILED")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
