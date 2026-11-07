#!/usr/bin/env python3
"""Check command record/replay, unattended.

Usage: python tools/check_replay.py [--ticks N] [--seed N] [--map PATH]
                                    [--synthetic-input SEED]
                                    [--recording FILE] [--no-mutation]
                                    [--build Debug|Release]

1. Unless --recording is given, record a game: --record-commands, with the
   determinism trace. Without --synthetic-input the commands come only from
   the computer players and the game itself; with it, a script plays the
   game with random mouse input (tools/synthetic_input.py).
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
import re
import shutil
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import command_log  # noqa: E402


def run_game(launcher, bin_dir, args, extra, input_seed=None):
    """Runs one traced game; returns the new trace file path or None.
    With input_seed, tools/synthetic_input.py plays during the run."""
    before = set(glob.glob(os.path.join(bin_dir, "determinism-*.tsv")))
    player = None
    if input_seed is not None:
        # About 15 ticks per second; stop playing well before the end.
        duration = max(args.ticks / 15 - 25, 10)
        player = subprocess.Popen([sys.executable, os.path.join(ROOT, "tools", "synthetic_input.py"),
                                   "--seed", str(input_seed), "--duration", str(duration),
                                   "--start-delay", "8",
                                   "--log", os.path.join(bin_dir, "replay-check", "input.log")],
                                  stdout=subprocess.DEVNULL)
    timeout = 60 + args.ticks // 5
    command = [launcher, "--trace-determinism", "--seed", str(args.seed),
               "--exit-after-ticks", str(args.ticks), "--timeout", str(timeout),
               *extra, "--", "-map", args.map]
    result = subprocess.run(command, capture_output=True, text=True)
    if player is not None:
        try:
            player.wait(timeout=30)
        except subprocess.TimeoutExpired:
            player.kill()
        print(f"    synthetic input: {player.returncode=}")
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
    parser.add_argument("--synthetic-input", type=int, metavar="SEED",
                        help="play the recorded game with random mouse input "
                             "(tools/synthetic_input.py) using this seed")
    parser.add_argument("--dump-every", type=int, metavar="N",
                        help="dump the world every N ticks in the recording and the replay "
                             "and, if they differ, show the differing fields "
                             "(tools/diff_world_dumps.py); about 22 MB per dump")
    parser.add_argument("--build", default="Debug")
    args = parser.parse_args()

    bin_dir = os.path.join(ROOT, "build", "x86", "bin", args.build)
    launcher = os.path.join(bin_dir, "s3re_launcher.exe")
    if not os.path.exists(launcher):
        print(f"launcher not found: {launcher} (build first)")
        return 2
    work = os.path.join(bin_dir, "replay-check")
    os.makedirs(work, exist_ok=True)
    dump_args = []
    if args.dump_every:
        for old in glob.glob(os.path.join(bin_dir, "determinism-*-world-*.bin")):
            os.remove(old)
        ticks = list(range(args.dump_every, args.ticks + 1, args.dump_every))[:16]
        dump_args = ["--dump-tick", ",".join(map(str, ticks))]

    if args.recording:
        recording = os.path.abspath(args.recording)
        reference = recording + ".tsv"
        if not os.path.exists(reference):
            print(f"no reference trace {reference}")
            return 2
    else:
        recording = os.path.join(work, "recording.bin")
        print(f"record: {args.ticks} ticks, seed {args.seed}, {args.map}", flush=True)
        trace = run_game(launcher, bin_dir, args, ["--record-commands", recording, *dump_args],
                         input_seed=args.synthetic_input)
        record_trace = trace
        if trace is None:
            return 2
        reference = recording + ".tsv"
        shutil.copyfile(trace, reference)

    commands = command_log.read(recording)
    print(f"recording has {len(commands)} commands")

    print("replay", flush=True)
    trace = run_game(launcher, bin_dir, args, ["--replay-commands", recording, *dump_args])
    if trace is None:
        return 2
    ok, report = same_traces(reference, trace)
    print(f"  replay vs recording: {'identical' if ok else 'DIFFERENT'}")
    if not ok:
        print(report)
        if dump_args and not args.recording:
            for tick in dump_args[1].split(","):
                a = record_trace[:-4] + f"-world-{tick}.bin"
                b = trace[:-4] + f"-world-{tick}.bin"
                if os.path.exists(a) and os.path.exists(b):
                    print(f"  world dump differences at tick {tick}:", flush=True)
                    subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                                 "diff_world_dumps.py"),
                                    a, b, "--runs", "15"])

    if not args.no_mutation:
        # Remove a command: preferably one that came from the (scripted)
        # player. Those are exactly the recorded commands the replay reports
        # as "not issued live" (nobody clicks during a replay). Otherwise
        # remove the first command that isn't a sync check (0xfb).
        log = open(os.path.join(bin_dir, "s3re.log"), encoding="utf-8",
                   errors="replace").read()
        player = [int(m) for m in re.findall(r"was not issued live .*? recorded #(\d+) ", log)]
        index = player[0] if player else next(
            (i for i, (_, dwords) in enumerate(commands)
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
