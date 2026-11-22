#!/usr/bin/env python3
"""Check command record/replay, unattended.

Usage: python tools/check_replay.py [--ticks N] [--seed N] [--map PATH]
                                    [--random-map SPEC [--goods G]
                                     [--computers N] [--race R]]
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

With --recording FILE (e.g. a game a human played with --record-commands;
a path, or a name in recordings/ such as `scripted-seed127`), step 1 is
skipped and the replay is compared against FILE's own trace, which must sit
next to it as FILE.tsv (traces are not committed: use --against-original,
or keep the .tsv locally). Replays take the game seed and map from the
recording (version 2+), and a random-map game's setup (version 3+);
--seed, --map, --random-map, --goods, --computers and --race only apply when
recording (they are passed to s3re_launcher), --seed and --map also to
version 1 recordings, which don't store them.

With --against-original, the reference is instead a replay of the same
recording with the original code (s3re_launcher --original), and the replay
with our reimplementations must match it: the system-level check for every
replaced function. The original's trace is cached (tools/trace_cache.py;
--no-cache, --refresh-cache, --verify-cache), so usually only our code runs.

Replays run with --fast (the logic ticks as fast as it can, see
src/s3re/tooling/fast.cpp); --no-fast runs them in real time. A new
recording runs fast too, except with --synthetic-input: that input is timed
by the wall clock, so recording with it stays at normal speed (at fast
speed the script would only get a few clicks into the game).

The game window takes over the screen while it runs; don't touch mouse or
keyboard. Exit code 0 if every check passed.
"""

import argparse
import dataclasses
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
import first_calls  # noqa: E402
import game_lock  # noqa: E402
import trace_cache  # noqa: E402

DEFAULT_SEED = 1234
DEFAULT_MAP = r"Map\MULTI\384-2-Lakes.map"


def run_game(launcher, bin_dir, args, extra, input_seed=None, new_game=False):
    """Runs one traced game; returns the new trace file path or None.
    With input_seed, tools/synthetic_input.py plays during the run.
    new_game: a recording run, which needs a seed and a map; replays take
    them from the recording unless given explicitly."""
    with game_lock.held("check_replay"):
        return _run_game(launcher, bin_dir, args, extra, input_seed, new_game)


def game_command(launcher, args, extra, input_seed=None, new_game=False):
    """The launcher command line of one traced run."""
    timeout = 60 + args.ticks // 5
    seed = args.seed if args.seed is not None else (DEFAULT_SEED if new_game else None)
    game_map = args.map if args.map is not None else (DEFAULT_MAP if new_game else None)
    # Synthetic input follows the wall clock: record it at normal speed.
    fast = args.fast and input_seed is None
    setup = []
    if new_game and args.random_map:
        setup = ["--random-map", args.random_map]
        for option in ("goods", "computers", "race"):
            if getattr(args, option) is not None:
                setup += [f"--{option}", str(getattr(args, option))]
    return [launcher, "--trace-determinism", *setup,
            *(["--seed", str(seed)] if seed is not None else []),
            *(["--fast"] if fast else []),
            "--exit-after-ticks", str(args.ticks), "--timeout", str(timeout),
            *extra, "--", *(["-map", game_map] if game_map is not None else [])]


def _run_game(launcher, bin_dir, args, extra, input_seed, new_game):
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
    command = game_command(launcher, args, extra, input_seed, new_game)
    started = time.time()
    result = subprocess.run(command, capture_output=True, text=True)
    print(f"    {time.time() - started:.0f} s{' (fast)' if '--fast' in command else ''}",
          flush=True)
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
    parser.add_argument("--seed", type=int,
                        help=f"game seed for a new recording (default {DEFAULT_SEED}); "
                             "replays use the recording's")
    parser.add_argument("--map",
                        help="map path relative to the game directory, for a new recording "
                             f"(default {DEFAULT_MAP}); replays use the recording's")
    parser.add_argument("--random-map", metavar="SIZE[,SYM[,FIELD0C]]",
                        help="record a game on a random map (s3re_launcher --random-map)")
    parser.add_argument("--goods", help="with --random-map: start goods (low, medium, high)")
    parser.add_argument("--computers", type=int,
                        help="with --random-map: number of computer opponents")
    parser.add_argument("--race", help="with --random-map: your race")
    parser.add_argument("--recording",
                        help="replay this recording instead of making one (a path, or a name "
                             "in recordings/, e.g. scripted-seed127)")
    parser.add_argument("--no-mutation", action="store_true")
    parser.add_argument("--against-original", action="store_true",
                        help="verify our reimplementations: replay the recording with the "
                             "original code (--original) as the reference, then with ours, "
                             "and compare (implies --no-mutation)")
    parser.add_argument("--synthetic-input", type=int, metavar="SEED",
                        help="play the recorded game with random mouse input "
                             "(tools/synthetic_input.py) using this seed")
    parser.add_argument("--dump-every", type=int, metavar="N",
                        help="dump the world every N ticks (and the header at every hashed "
                             "tick) in the recording and the replay and, if they differ, "
                             "show the differing fields (tools/diff_world_dumps.py); "
                             "about 22 MB per dump")
    parser.add_argument("--no-fast", dest="fast", action="store_false",
                        help="run in real time instead of with --fast")
    parser.add_argument("--build", default="Debug")
    trace_cache.add_arguments(parser)
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
        for old in (glob.glob(os.path.join(bin_dir, "determinism-*-world-*.bin")) +
                    glob.glob(os.path.join(bin_dir, "determinism-*-headers.bin"))):
            os.remove(old)
        ticks = list(range(args.dump_every, args.ticks + 1, args.dump_every))[:16]
        dump_args = ["--dump-tick", ",".join(map(str, ticks)), "--dump-headers"]

    if args.recording:
        recording = os.path.abspath(command_log.resolve(args.recording))
        reference = recording + ".tsv"
        if not os.path.exists(reference) and not args.against_original:
            print(f"no reference trace {reference}")
            return 2
    else:
        recording = os.path.join(work, "recording" + command_log.EXTENSION)
        print(f"record: {args.ticks} ticks, seed {args.seed or DEFAULT_SEED}, "
              f"{args.map or DEFAULT_MAP}", flush=True)
        trace = run_game(launcher, bin_dir, args, ["--record-commands", recording, *dump_args],
                         input_seed=args.synthetic_input, new_game=True)
        record_trace = trace
        if trace is None:
            return 2
        reference = recording + ".tsv"
        shutil.copyfile(trace, reference)

    loaded = command_log.read(recording)
    commands = loaded.commands
    print(f"recording has {len(commands)} commands: {command_log.describe(loaded)}")
    if loaded.version == 1 and (args.seed is None or args.map is None):
        print("a version 1 recording needs --seed and --map (or convert it with "
              "tools/command_log.py upgrade)")
        return 2

    cached = None
    if args.against_original:
        args.no_mutation = True
        print("replay with the original code (reference)", flush=True)
        original_extra = ["--replay-commands", recording, "--original", *dump_args]

        def run_original():
            trace = run_game(launcher, bin_dir, args, original_extra)
            log = open(os.path.join(bin_dir, "s3re.log"), encoding="utf-8",
                       errors="replace").read() if trace else ""
            return trace, log

        if dump_args:
            args.no_cache = True  # the dumps come from the original run itself
        key = trace_cache.make_key("replay", bin_dir, game_lock.game_dir(), recording,
                                   game_command(launcher, args, original_extra)[1:])
        trace, cached, cache_ok = trace_cache.reference(args, key, args.ticks, run_original, work)
        if trace is None:
            return 2
        if not cache_ok:
            print("REPLAY FAILED (the original's trace differs from the cached one)")
            return 1
        reference = os.path.join(work, "original.tsv")
        shutil.copyfile(trace, reference)
        record_trace = trace

    print("replay" + (" with our code" if args.against_original else ""), flush=True)
    trace = run_game(launcher, bin_dir, args, ["--replay-commands", recording, *dump_args])
    if trace is None:
        return 2
    if cached is not None:
        ok, report = trace_cache.compare(cached, trace, args.ticks, work)
    else:
        ok, report = same_traces(reference, trace)
    print(f"  replay vs {'original' if args.against_original else 'recording'}: "
          f"{'identical' if ok else 'DIFFERENT'}")
    # Player commands = recorded commands nobody issued live in the replay.
    replay_log = open(os.path.join(bin_dir, "s3re.log"), encoding="utf-8",
                      errors="replace").read()
    # Which of our replacements the game called (s3re logs "calls <name> <n>"
    # at exit): a function that was never called isn't covered by this run.
    calls = {m.group(1): int(m.group(2))
             for m in re.finditer(r"\] calls (\S+) (\d+)", replay_log)}
    if calls:
        unused = sorted(name for name, n in calls.items() if n == 0)
        print(f"  replaced functions called in this game: {len(calls) - len(unused)} of "
              f"{len(calls)}")
        if unused:
            print(f"  not called (not covered by this run): {', '.join(unused)}")
        print("\n".join(first_calls.summary(first_calls.first_calls(replay_log)[0])))
    player_commands = len(re.findall(r"was not issued live", replay_log))
    print(f"  player commands in the recording: {player_commands}"
          f"{' (at least; the log lists 20 differences at most)' if player_commands >= 20 else ''}")
    if args.synthetic_input is not None and player_commands == 0 and not args.against_original:
        print("  the scripted player issued no commands, so this run proves little")
        ok = False
    if not ok:
        print(report)
        if dump_args and (args.against_original or not args.recording):
            print("  header differences at hashed ticks:", flush=True)
            subprocess.run([sys.executable, os.path.join(ROOT, "tools", "diff_world_dumps.py"),
                            "--headers", record_trace[:-4] + "-headers.bin",
                            trace[:-4] + "-headers.bin"])
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
            mutated = os.path.join(work, "mutated" + (command_log.EXTENSION if loaded.version > 1
                                                      else ".bin"))
            command_log.write(mutated, dataclasses.replace(
                loaded, commands=commands[:index] + commands[index + 1:]))
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
