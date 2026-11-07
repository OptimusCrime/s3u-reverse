#!/usr/bin/env python3
"""Check command record/replay, unattended.

Usage: python tools/check_replay.py [--ticks N] [--seed N] [--map PATH]
                                    [--recording FILE] [--no-mutation]
                                    [--build Debug|Release]

1. Unless --recording is given, record a game: --record-commands, with the
   determinism trace (no human input, so the commands come from the
   computer players and the game itself).
2. Replay the recording (--replay-commands) with the trace on, and compare
   the traces: they must be identical.
3. Mutation check: replay the recording with one command removed. The trace
   must now differ, which proves the replay really drives the game.

With --recording FILE (e.g. a game a human played with --record-commands),
step 1 is skipped and the replay is compared against FILE's own trace,
which must sit next to it as FILE.tsv.

The game window takes over the screen while it runs; don't touch mouse or
keyboard. Exit code 0 if every check passed.
"""

import argparse
import glob
import os
import shutil
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import command_log  # noqa: E402


def run_game(launcher, bin_dir, args, extra):
    """Runs one traced game; returns the new trace file path or None."""
    before = set(glob.glob(os.path.join(bin_dir, "determinism-*.tsv")))
    timeout = 60 + args.ticks // 5
    command = [launcher, "--trace-determinism", "--seed", str(args.seed),
               "--exit-after-ticks", str(args.ticks), "--timeout", str(timeout),
               *extra, "--", "-map", args.map]
    result = subprocess.run(command, capture_output=True, text=True)
    new = sorted(set(glob.glob(os.path.join(bin_dir, "determinism-*.tsv"))) - before)
    log = open(os.path.join(bin_dir, "s3re.log"), encoding="utf-8", errors="replace").read()
    for line in log.splitlines():
        if "record:" in line or "replay:" in line:
            print("   ", line.split("] ", 1)[-1])
    time.sleep(2)  # trace file names have one-second resolution
    if result.returncode != 0 or len(new) != 1:
        print(result.stdout + result.stderr)
        print(f"run failed (exit code {result.returncode}, {len(new)} new trace file(s))")
        return None
    return new[0]


def same_traces(a, b):
    result = subprocess.run([sys.executable, "-I",
                             os.path.join(ROOT, "tools", "compare_traces.py"), a, b],
                            capture_output=True, text=True)
    return result.returncode == 0, result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--ticks", type=int, default=1000)
    parser.add_argument("--seed", type=int, default=1234)
    parser.add_argument("--map", default=r"Map\MULTI\384-2-Lakes.map",
                        help="map path relative to the game directory")
    parser.add_argument("--recording", help="replay this recording instead of making one")
    parser.add_argument("--no-mutation", action="store_true")
    parser.add_argument("--build", default="Debug")
    args = parser.parse_args()

    bin_dir = os.path.join(ROOT, "build", "x86", "bin", args.build)
    launcher = os.path.join(bin_dir, "s3re_launcher.exe")
    if not os.path.exists(launcher):
        print(f"launcher not found: {launcher} (build first)")
        return 2
    work = os.path.join(bin_dir, "replay-check")
    os.makedirs(work, exist_ok=True)

    if args.recording:
        recording = os.path.abspath(args.recording)
        reference = recording + ".tsv"
        if not os.path.exists(reference):
            print(f"no reference trace {reference}")
            return 2
    else:
        recording = os.path.join(work, "recording.bin")
        print(f"record: {args.ticks} ticks, seed {args.seed}, {args.map}", flush=True)
        trace = run_game(launcher, bin_dir, args, ["--record-commands", recording])
        if trace is None:
            return 2
        reference = recording + ".tsv"
        shutil.copyfile(trace, reference)

    commands = command_log.read(recording)
    print(f"recording has {len(commands)} commands")

    print("replay", flush=True)
    trace = run_game(launcher, bin_dir, args, ["--replay-commands", recording])
    if trace is None:
        return 2
    ok, report = same_traces(reference, trace)
    print(f"  replay vs recording: {'identical' if ok else 'DIFFERENT'}")
    if not ok:
        print(report)

    if not args.no_mutation:
        # Remove the first command that isn't a sync check (cmd 0xfb).
        index = next((i for i, (_, dwords) in enumerate(commands)
                      if struct.pack("<I", dwords[0])[1] != 0xfb), None)
        if index is None:
            print("mutation check skipped: no command to remove")
        else:
            mutated = os.path.join(work, "mutated.bin")
            command_log.write(mutated, commands[:index] + commands[index + 1:])
            print(f"mutation: replay without command {index} (slot {commands[index][0]})",
                  flush=True)
            trace = run_game(launcher, bin_dir, args, ["--replay-commands", mutated])
            if trace is None:
                return 2
            mutation_same, _ = same_traces(reference, trace)
            print(f"  mutated replay vs recording: "
                  f"{'identical (BAD: replay ignored)' if mutation_same else 'different (good)'}")
            ok &= not mutation_same

    print("REPLAY OK" if ok else "REPLAY FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
