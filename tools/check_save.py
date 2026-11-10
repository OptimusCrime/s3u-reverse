#!/usr/bin/env python3
"""Whole-game check from a savegame, unattended.

Usage: python tools/check_save.py SAVE [--ticks N] [--against-original] [--no-fast]
                                  [--no-cache | --refresh-cache | --verify-cache]
                                  [--build B]

SAVE is a savegame, as a path or a name in <game dir>\\Save (e.g.
"slot 1.sav"). Every run starts the game directly from it
(s3re_launcher --load-save, no menus), runs N ticks from the tick the
savegame starts at, and writes the determinism trace.

Without --against-original: runs the savegame twice with the original code
(--original) and compares the traces: is a game loaded from this savegame
deterministic?

With --against-original: runs it once with the original code (reference)
and once with our reimplementations, and compares the traces: the
whole-game check for every replaced function the late game calls. The
original's trace is cached (tools/trace_cache.py), so usually only our code
runs. Runs use --fast (src/s3re/tooling/fast.cpp) unless --no-fast. Prints
which replacements were called and writes the call counts to
build/x86/bin/<build>/save-check/<save>-calls.tsv; if the traces are
identical, it also merges them into re/whole_game_calls.csv (source
save:<savegame file name>, see tools/whole_game.py).

The user's savegames are never written: the launcher loads a copy. As a
safety net, this script also checks the SHA-256 of every *.sav under
<game dir>\\Save before and after, and restores a changed one from a backup
it made first. Every game run takes the game lock (tools/game_lock.py).
The game window takes over the screen while it runs; don't touch mouse or
keyboard. Exit code 0 if the traces are identical.
"""

import argparse
import glob
import hashlib
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import first_calls  # noqa: E402
import game_lock  # noqa: E402
import trace_cache  # noqa: E402
import whole_game  # noqa: E402


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def savegames():
    return sorted(glob.glob(os.path.join(game_lock.game_dir(), "Save", "**", "*.sav"),
                            recursive=True))


def back_up_saves(backup_dir):
    """Copies every savegame to backup_dir (once) and returns their hashes."""
    hashes = {}
    for path in savegames():
        hashes[path] = sha256(path)
        copy = os.path.join(backup_dir, hashes[path] + ".sav")
        if not os.path.exists(copy):
            os.makedirs(backup_dir, exist_ok=True)
            shutil.copyfile(path, copy)
    return hashes


def check_saves(hashes, backup_dir):
    """Restores any savegame that changed; returns False if one did."""
    ok = True
    for path, digest in hashes.items():
        if not os.path.exists(path) or sha256(path) != digest:
            print(f"WARNING: {path} changed during the run; restoring it from the backup")
            shutil.copyfile(os.path.join(backup_dir, digest + ".sav"), path)
            ok = False
    new = set(savegames()) - set(hashes)
    if new:
        print(f"note: new savegame files appeared: {', '.join(sorted(new))}")
    return ok


def game_command(launcher, save, ticks, extra):
    timeout = 120 + ticks // 3
    return [launcher, "--trace-determinism", "--load-save", save,
            "--exit-after-ticks", str(ticks), "--timeout", str(timeout), *extra]


def run_game(launcher, bin_dir, save, ticks, extra, label):
    before = set(glob.glob(os.path.join(bin_dir, "determinism-*.tsv")))
    command = game_command(launcher, save, ticks, extra)
    print(f"{label}: {ticks} ticks from {save}", flush=True)
    with game_lock.held("check_save"):
        started = time.time()
        result = subprocess.run(command, capture_output=True, text=True)
        print(f"    {time.time() - started:.0f} s{' (fast)' if '--fast' in command else ''}",
              flush=True)
    time.sleep(2)  # trace file names have one-second resolution
    new = sorted(set(glob.glob(os.path.join(bin_dir, "determinism-*.tsv"))) - before)
    log = open(os.path.join(bin_dir, "s3re.log"), encoding="utf-8", errors="replace").read()
    for line in log.splitlines():
        if "load-save:" in line or "ending the game" in line:
            print("   ", line.split("] ", 1)[-1])
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
    parser.add_argument("save")
    parser.add_argument("--ticks", type=int, default=500)
    parser.add_argument("--against-original", action="store_true")
    parser.add_argument("--dump-tick", metavar="T[,T...]",
                        help="also dump the hashed world at these (absolute) ticks in both "
                             "runs and show the differing fields (tools/diff_world_dumps.py)")
    parser.add_argument("--no-fast", dest="fast", action="store_false",
                        help="run in real time instead of with --fast "
                             "(src/s3re/tooling/fast.cpp)")
    parser.add_argument("--build", default="Debug")
    trace_cache.add_arguments(parser)
    args = parser.parse_args()

    bin_dir = os.path.join(ROOT, "build", "x86", "bin", args.build)
    launcher = os.path.join(bin_dir, "s3re_launcher.exe")
    if not os.path.exists(launcher):
        print(f"launcher not found: {launcher} (build first)")
        return 2
    save = args.save
    if not os.path.exists(save):
        save = os.path.join(game_lock.game_dir(), "Save", args.save)
    if not os.path.exists(save):
        print(f"savegame not found: {args.save}")
        return 2
    save = os.path.abspath(save)
    work = os.path.join(bin_dir, "save-check")
    os.makedirs(work, exist_ok=True)
    stem = re.sub(r"[^A-Za-z0-9_-]+", "_", os.path.splitext(os.path.basename(save))[0])

    backup_dir = os.path.join(work, "backup")
    hashes = back_up_saves(backup_dir)
    print(f"savegames protected by hash/backup: {len(hashes)}")

    runs = ([("original", ["--original"]), ("ours", [])] if args.against_original
            else [("original run 1", ["--original"]), ("original run 2", ["--original"])])
    dump = ["--dump-tick", args.dump_tick] if args.dump_tick else []
    fast = ["--fast"] if args.fast else []
    if dump:
        args.no_cache = True  # the dumps come from the runs themselves
    traces = []
    logs = []
    cached = None
    try:
        for index, (label, extra) in enumerate(runs):
            extra = extra + fast + dump
            if args.against_original and index == 0:
                key = trace_cache.make_key("save", bin_dir, game_lock.game_dir(), save,
                                           game_command(launcher, save, args.ticks, extra)[1:])
                trace, cached, cache_ok = trace_cache.reference(
                    args, key, args.ticks,
                    lambda: run_game(launcher, bin_dir, save, args.ticks, extra, label), work)
                if not cache_ok:
                    print("SAVE CHECK FAILED (the original's trace differs from the cached one)")
                    return 1
                log = ""
            else:
                trace, log = run_game(launcher, bin_dir, save, args.ticks, extra, label)
            if trace is None:
                return 2
            copy = os.path.join(work, f"{stem}-{label.replace(' ', '-')}.tsv")
            shutil.copyfile(trace, copy)
            for tick in (args.dump_tick.split(",") if args.dump_tick else []):
                dumped = trace[:-4] + f"-world-{tick}.bin"
                if os.path.exists(dumped):
                    shutil.move(dumped, copy[:-4] + f"-world-{tick}.bin")
            first, last = trace_ticks(copy)
            print(f"    trace {copy}: ticks {first}..{last}")
            traces.append(copy)
            logs.append(log)
    finally:
        saves_ok = check_saves(hashes, backup_dir)

    if cached is not None:
        ok, report = trace_cache.compare(cached, traces[1], args.ticks, work)
    else:
        ok, report = same_traces(traces[0], traces[1])
    print(f"  {runs[0][0]} vs {runs[1][0]}: {'identical' if ok else 'DIFFERENT'}")
    if not ok:
        print(report)
        for tick in (args.dump_tick.split(",") if args.dump_tick else []):
            a = traces[0][:-4] + f"-world-{tick}.bin"
            b = traces[1][:-4] + f"-world-{tick}.bin"
            if os.path.exists(a) and os.path.exists(b):
                print(f"  world dump differences at tick {tick}:", flush=True)
                subprocess.run([sys.executable, os.path.join(ROOT, "tools", "diff_world_dumps.py"),
                                a, b, "--runs", "30"])

    if args.against_original:
        calls = {m.group(1): int(m.group(2))
                 for m in re.finditer(r"\] calls (\S+) (\d+)", logs[1])}
        if calls:
            called = sorted(n for n, c in calls.items() if c > 0)
            print(f"  replaced functions called in this game: {len(called)} of {len(calls)}")
            print("\n".join(first_calls.summary(first_calls.first_calls(logs[1])[0])))
            first, last = trace_ticks(traces[1])
            out = os.path.join(work, f"{stem}-calls.tsv")
            with open(out, "w", encoding="utf-8", newline="\n") as f:
                f.write(f"# {os.path.basename(save)} ticks {first}..{last} "
                        f"{'identical' if ok else 'DIFFERENT'}\n")
                for name in sorted(calls):
                    f.write(f"{name}\t{calls[name]}\n")
            print(f"  call counts: {out}")
            if ok and saves_ok and first is not None:
                added, updated = whole_game.record_named(
                    f"save:{os.path.basename(save)}", last - first + 1, calls)
                print(f"  re/whole_game_calls.csv: {added} rows added, {updated} updated")

    if not saves_ok:
        print("a savegame changed during the runs (restored); treat the result with care")
    print("SAVE CHECK OK" if ok and saves_ok else "SAVE CHECK FAILED")
    return 0 if ok and saves_ok else 1


if __name__ == "__main__":
    sys.exit(main())
