#!/usr/bin/env python3
"""Play a whole game and record it (for whole-game checks of late-game code).

Usage: python tools/play_game.py [--size 768] [--symmetry 0] [--computers 2]
                                 [--goods high] [--race RACE] [--seed N]
                                 [--name NAME] [--ours] [--build Debug]
       python tools/play_game.py --map M704-2-Longrun [--race RACE]
                                 [--goods high] [--computers N] [--seed N]

With --map NAME (a map of the game, e.g. M704-2-Longrun, or a path) the game is played
on that map instead, with its own start positions and player setup (unless
--computers is given); --race and --goods work the same.

Starts S3.EXE through s3re_launcher on a new random map, without menus:
you against --computers computer players (each in a party of its own),
with --goods start goods, at normal speed, with the ORIGINAL game code
(`--original`, so a bug in our reimplementations can't spoil the game;
`--ours` plays with the verified reimplementations instead). Every command
is written to the recording as it happens, and the determinism trace of
the game is written alongside, so the recording can later be replayed fast
against the original (tools/check_replay.py --recording FILE).

The files go to recordings/ in the repository (--out-dir to change):
  NAME.s3rec      the recording (format version 4, a text file: seed, map,
                  game setup, one line per command); committed to git
  NAME.s3rec.tsv  the determinism trace of the played game (the reference
                  for `check_replay.py --recording NAME`; not committed)
  NAME.log        s3re.log of the session (not committed)
NAME defaults to fullgame-<date>-<time>-seed<N>.

Quit through the game's own menu or by closing the window; the recording is
complete up to that moment either way (also after a crash). Don't press F12
(the game's fast-forward: not recorded, the replay would diverge) and don't
load a savegame. Saving is fine. See docs/recording_a_game.md.

Takes the game lock (tools/game_lock.py) for the whole session.
"""

import argparse
import datetime
import glob
import os
import random
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import command_log  # noqa: E402
import game_lock  # noqa: E402


GAME_DIR = os.path.join(ROOT, "Settlers3Ultimate")


def find_map(name):
    """The -map argument (relative to the game folder, backslashes) for a map
    name such as "M704-2-Longrun" (searched in Map\\*\\, case-insensitive,
    ".map" optional) or a path relative to the game folder. None (after
    printing why) if there is no such map or the name is ambiguous."""
    if os.path.isfile(os.path.join(GAME_DIR, name)):
        return name.replace("/", "\\")
    wanted = name.lower()
    if not wanted.endswith(".map"):
        wanted += ".map"
    found = []
    for folder in sorted(os.listdir(os.path.join(GAME_DIR, "Map"))):
        path = os.path.join(GAME_DIR, "Map", folder)
        if os.path.isdir(path):
            found += [f"Map\\{folder}\\{entry}" for entry in sorted(os.listdir(path))
                      if entry.lower() == wanted]
    if len(found) == 1:
        return found[0]
    if found:
        print(f"map name {name!r} is ambiguous: {', '.join(found)}; give the path")
    else:
        print(f"no map named {name!r} in Settlers3Ultimate\\Map\\*\\ "
              "(file names without .map, e.g. M704-2-Longrun)")
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--size", type=int, default=768,
                        help="map size, a multiple of 64 (the dialog offers 384..768; default 768)")
    parser.add_argument("--symmetry", type=int, default=0, help="symmetry bits 0..3 (default 0)")
    parser.add_argument("--map", help="play this map instead of a random map: a name "
                        "(e.g. M704-2-Longrun, looked up in the game's Map folders) or a "
                        "path relative to the game folder")
    parser.add_argument("--computers", type=int, default=None,
                        help="computer opponents (default 2)")
    parser.add_argument("--goods", default="high", choices=("low", "medium", "high"),
                        help="start goods (default high)")
    parser.add_argument("--race", choices=("roman", "egyptian", "asian", "amazon"),
                        help="your race (default: random, chosen by the game)")
    parser.add_argument("--seed", type=int, help="game and map seed 1..65535 (default random)")
    parser.add_argument("--name", help="file name of the recording, without .s3rec")
    parser.add_argument("--out-dir", default=command_log.RECORDINGS_DIR,
                        help="folder for the files (default: recordings/)")
    parser.add_argument("--ours", action="store_true",
                        help="play with our verified reimplementations instead of the "
                             "original code")
    parser.add_argument("--exit-after-ticks", type=int,
                        help="end the game at this tick (for testing this script)")
    parser.add_argument("--build", default="Debug")
    args = parser.parse_args()

    bin_dir = os.path.join(ROOT, "build", "x86", "bin", args.build)
    launcher = os.path.join(bin_dir, "s3re_launcher.exe")
    if not os.path.exists(launcher):
        print(f"launcher not found: {launcher} (build first: cmake --preset x86, "
              "cmake --build --preset debug)")
        return 2
    seed = args.seed if args.seed is not None else random.randint(1, 0xFFFF)
    if not 0 <= seed <= 0xFFFF:
        print("--seed must be 0..65535")
        return 2
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M")
    name = args.name or f"fullgame-{stamp}-seed{seed}"
    out_dir = os.path.abspath(args.out_dir)
    os.makedirs(out_dir, exist_ok=True)
    recording = os.path.join(out_dir, name + command_log.EXTENSION)
    if os.path.exists(recording):
        print(f"{recording} exists already; choose another --name")
        return 2

    map_file = None
    if args.map:
        map_file = find_map(args.map)
        if map_file is None:
            return 2
        print(f"Map: {map_file}", flush=True)
        where = []
        computers = [] if args.computers is None else ["--computers", str(args.computers)]
    else:
        where = ["--random-map", f"{args.size},{args.symmetry}"]
        computers = ["--computers", str(2 if args.computers is None else args.computers)]
    command = [launcher, *where,
               "--goods", args.goods, *computers,
               *(["--race", args.race] if args.race else []),
               "--seed", str(seed), "--record-commands", recording, "--trace-determinism",
               *([] if args.ours else ["--original"]),
               *(["--exit-after-ticks", str(args.exit_after_ticks)]
                 if args.exit_after_ticks else []),
               *(["--", "-map", map_file] if map_file else [])]
    print("Starting:", " ".join(command), flush=True)
    print(f"Recording to {recording}", flush=True)
    print("Quit through the game's menu or close the window when you are done. "
          "Don't press F12 and don't load a savegame.", flush=True)

    before = set(glob.glob(os.path.join(bin_dir, "determinism-*.tsv")))
    with game_lock.held("play_game (a person is playing)"):
        started = time.time()
        result = subprocess.run(command)
        minutes = (time.time() - started) / 60
    print(f"S3.EXE ended after {minutes:.0f} minutes (exit code {result.returncode})")

    log_path = os.path.join(bin_dir, "s3re.log")
    if os.path.exists(log_path):
        shutil.copyfile(log_path, os.path.join(out_dir, name + ".log"))
    new = sorted(set(glob.glob(os.path.join(bin_dir, "determinism-*.tsv"))) - before)
    if new:
        shutil.copyfile(new[-1], recording + ".tsv")
        with open(recording + ".tsv", encoding="utf-8", errors="replace") as trace:
            last = [line.split("	")[1] for line in trace if line.startswith("T	")]
        print(f"trace: {recording}.tsv (last tick {last[-1] if last else 'none'})")
    else:
        print("no determinism trace was written")
    if not os.path.exists(recording):
        print("no recording was written (see s3re.log)")
        return 1
    loaded = command_log.read(recording)
    print(f"recording: {recording}: {command_log.describe(loaded)}, "
          f"{len(loaded.commands)} commands")
    log = open(log_path, encoding="utf-8", errors="replace").read() if os.path.exists(
        log_path) else ""
    if "fast-forward (F12) was used" in log:
        print("WARNING: F12 (fast-forward) was used during the game; the replay will "
              "probably diverge from that point (see the .log)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
