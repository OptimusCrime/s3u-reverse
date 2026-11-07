#!/usr/bin/env python3
"""Run the game twice with the same seed and map, unattended, and compare.

Usage: python tools/check_determinism.py [--ticks N] [--seed N] [--map PATH]
                                         [--runs N] [--build Debug|Release]

Each run starts S3.EXE through s3re_launcher with --trace-determinism, a
fixed RNG seed, the game's own `-map` option (skips the menus) and
--exit-after-ticks, so nobody has to click anything. The game window takes
over the screen while it runs; don't touch mouse or keyboard. Then every
trace is compared with the first one (tools/compare_traces.py).

Exit code 0 if all traces are identical.
"""

import argparse
import glob
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--ticks", type=int, default=1000)
    parser.add_argument("--seed", type=int, default=1234)
    parser.add_argument("--map", default=r"Map\MULTI\384-2-Lakes.map",
                        help="map path relative to the game directory")
    parser.add_argument("--runs", type=int, default=2)
    parser.add_argument("--build", default="Debug")
    args = parser.parse_args()

    bin_dir = os.path.join(ROOT, "build", "x86", "bin", args.build)
    launcher = os.path.join(bin_dir, "s3re_launcher.exe")
    if not os.path.exists(launcher):
        print(f"launcher not found: {launcher} (build first)")
        return 2

    # About 15 ticks per second, plus loading; generous safety margin.
    timeout = 60 + args.ticks // 5
    traces = []
    for run in range(args.runs):
        before = set(glob.glob(os.path.join(bin_dir, "determinism-*.tsv")))
        command = [launcher, "--trace-determinism", "--seed", str(args.seed),
                   "--exit-after-ticks", str(args.ticks), "--timeout", str(timeout),
                   "--", "-map", args.map]
        print(f"run {run + 1}/{args.runs}: {args.ticks} ticks, seed {args.seed}, {args.map}", flush=True)
        result = subprocess.run(command, capture_output=True, text=True)
        new = sorted(set(glob.glob(os.path.join(bin_dir, "determinism-*.tsv"))) - before)
        if result.returncode != 0 or len(new) != 1:
            print(result.stdout + result.stderr)
            print(f"run {run + 1} failed (exit code {result.returncode}, "
                  f"{len(new)} new trace file(s))")
            return 2
        traces.append(new[0])
        time.sleep(2)  # trace file names have one-second resolution

    ok = True
    for other in traces[1:]:
        result = subprocess.run([sys.executable, "-I",
                                 os.path.join(ROOT, "tools", "compare_traces.py"),
                                 traces[0], other])
        ok &= result.returncode == 0
    print("DETERMINISTIC" if ok else "NOT DETERMINISTIC")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
