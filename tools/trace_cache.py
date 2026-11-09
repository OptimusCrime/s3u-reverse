#!/usr/bin/env python3
"""Cache of reference traces of the ORIGINAL code, for whole-game checks.

A run with `s3re_launcher --original` is fully determined by its inputs:
S3.EXE (SHA-checked by the launcher), the recording or savegame, the
launcher options that affect the game (seed, map, --fast, game options
after `--`, ...), the number of ticks, and the DLL code that still runs in
original mode (the tooling: trace, replay, seed, load-save, fast hooks).
So check_replay.py / check_save.py --against-original store the original's
trace here and reuse it instead of running the original every time.

Cache key (all of it; a change of any part is a miss):
  - SHA-256 of S3.EXE and of the input file (recording or savegame)
  - the launcher arguments of the run, without --exit-after-ticks and
    --timeout, with file paths replaced by their SHA-256
  - the trace format version ("s3re determinism trace vN", read from
    s3re.dll) and the original-mode tooling version (S3RE_ORIGINAL_TOOLING
    _VERSION in src/s3re/tooling/version.h, read from s3re.dll; bump it when
    that code changes)
  - the kind of check (replay or save)
  Fast and normal speed are part of the arguments, so they have separate
  entries.
The number of ticks is not part of the key: an entry from a longer run
serves a shorter one, cut to the shorter run's ticks (both traces are then
compared up to the tick before the last one, because the last tick of a
run can be cut short).

Only the trace is cached. The call counts and first-call ticks come from
the run with our code, so nothing else of the original run is needed.

Storage: build/trace_cache/<key hash>.tsv (the trace) and .json (the key,
ticks, the tick the run counted from, date). Delete the folder to clear it.

Options added to the checks (add_arguments): --no-cache (always run the
original, don't store), --refresh-cache (run the original and replace the
entry), --verify-cache (run the original and check that it equals the
cached trace; a sanity check, e.g. after changing tooling code).
"""

import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE_DIR = os.path.join(ROOT, "build", "trace_cache")

_sha_cache = {}


def sha256(path):
    path = os.path.abspath(path)
    stat = os.stat(path)
    key = (path, stat.st_size, stat.st_mtime_ns)
    if key not in _sha_cache:
        h = hashlib.sha256()
        with open(path, "rb") as f:
            for block in iter(lambda: f.read(1 << 20), b""):
                h.update(block)
        _sha_cache[key] = h.hexdigest()
    return _sha_cache[key]


def dll_versions(bin_dir):
    """(trace format version, original-mode tooling version) from s3re.dll."""
    data = open(os.path.join(bin_dir, "s3re.dll"), "rb").read()
    trace = re.search(rb"s3re determinism trace v(\d+)", data)
    tooling = re.search(rb"s3re original-mode tooling version (\d+)", data)
    return (int(trace.group(1)) if trace else None,
            int(tooling.group(1)) if tooling else None)


def add_arguments(parser):
    group = parser.add_argument_group("reference trace cache (tools/trace_cache.py)")
    group.add_argument("--no-cache", action="store_true",
                       help="always run the original for the reference; don't use or store "
                            "the cache")
    group.add_argument("--refresh-cache", action="store_true",
                       help="run the original and replace the cached reference")
    group.add_argument("--verify-cache", action="store_true",
                       help="run the original and check it against the cached reference")


_DROP_WITH_VALUE = {"--exit-after-ticks", "--timeout"}


def make_key(kind, bin_dir, game_dir, input_file, launcher_args):
    """The cache key of an original run. launcher_args: the run's launcher
    arguments (file arguments are replaced by their hashes)."""
    args = []
    skip = False
    for arg in launcher_args:
        if skip:
            skip = False
            continue
        if arg in _DROP_WITH_VALUE:
            skip = True
            continue
        if os.path.isfile(arg):
            arg = "file:" + sha256(arg)
        args.append(arg)
    trace_version, tooling_version = dll_versions(bin_dir)
    return {
        "kind": kind,
        "exe_sha256": sha256(os.path.join(game_dir, "S3.EXE")),
        "input_sha256": sha256(input_file),
        "args": args,
        "trace_version": trace_version,
        "tooling_version": tooling_version,
    }


def _key_hash(key):
    return hashlib.sha256(json.dumps(key, sort_keys=True).encode()).hexdigest()[:24]


def exit_base(log_text):
    """The tick --exit-after-ticks counted from, from s3re.log."""
    m = re.search(r"\(\d+ ticks after (\d+)\); ending the game", log_text)
    return int(m.group(1)) if m else None


def lookup(key, ticks):
    """A cached entry (dict with 'trace', 'ticks', 'base') covering at least
    `ticks` ticks, or None."""
    if None in (key["trace_version"], key["tooling_version"]):
        return None
    stem = os.path.join(CACHE_DIR, _key_hash(key))
    try:
        entry = json.load(open(stem + ".json", encoding="utf-8"))
    except (OSError, ValueError):
        return None
    if entry.get("key") != key or entry.get("ticks", 0) < ticks or \
            not os.path.exists(stem + ".tsv"):
        return None
    entry["trace"] = stem + ".tsv"
    return entry


def store(key, ticks, trace, log_text):
    if None in (key["trace_version"], key["tooling_version"]):
        print("  trace cache: s3re.dll has no trace/tooling version string; not cached")
        return
    base = exit_base(log_text)
    if base is None:
        print("  trace cache: run end not found in s3re.log; not cached")
        return
    old = lookup(key, 0)
    if old is not None and old["ticks"] > ticks:
        return  # keep the longer run
    os.makedirs(CACHE_DIR, exist_ok=True)
    stem = os.path.join(CACHE_DIR, _key_hash(key))
    shutil.copyfile(trace, stem + ".tsv")
    with open(stem + ".json", "w", encoding="utf-8", newline="\n") as f:
        json.dump({"key": key, "ticks": ticks, "base": base,
                   "stored": datetime.datetime.now().isoformat(timespec="seconds")},
                  f, indent=1)
    print(f"  trace cache: stored the original's trace ({ticks} ticks) as {stem}.tsv")


def cut_trace(source, target, last_tick):
    """Copies a trace, keeping only lines up to last_tick."""
    with open(source, encoding="utf-8", errors="replace") as f, \
            open(target, "w", encoding="utf-8", newline="\n") as out:
        for line in f:
            fields = line.split("\t")
            if fields[0] in ("T", "H") and len(fields) > 1 and int(fields[1]) > last_tick:
                continue
            out.write(line)


def compare(entry, trace, ticks, work):
    """Compares a cached reference (entry from lookup) with the trace of a run
    of `ticks` ticks. Returns (ok, report). If the cached run was longer, both
    are cut to the tick before the shorter run's last one (several steps can
    share the last tick, and a run ends after the first of them)."""
    a, b = entry["trace"], trace
    if entry["ticks"] != ticks:
        last = entry["base"] + ticks - 1
        a = os.path.join(work, "cached-reference-cut.tsv")
        b = os.path.join(work, "run-cut.tsv")
        cut_trace(entry["trace"], a, last)
        cut_trace(trace, b, last)
    result = subprocess.run([sys.executable, "-I",
                             os.path.join(ROOT, "tools", "compare_traces.py"), a, b],
                            capture_output=True, text=True)
    return result.returncode == 0, result.stdout


def reference(args, key, ticks, run_original, work):
    """Gets the reference trace of the original code.

    run_original() runs the original and returns (trace path, s3re.log text)
    or (None, log). Returns (trace path, cached entry or None, ok); with a
    cached entry the caller compares with compare(entry, ...)."""
    use_cache = not args.no_cache
    entry = lookup(key, ticks) if use_cache and not args.refresh_cache else None
    if entry is not None and not args.verify_cache:
        print(f"  reference: CACHED original trace ({entry['ticks']} ticks, stored "
              f"{entry['stored']}; --refresh-cache to rerun the original)", flush=True)
        return entry["trace"], entry, True
    trace, log = run_original()
    if trace is None:
        return None, None, False
    if entry is not None:  # --verify-cache
        ok, report = compare(entry, trace, ticks, work)
        print(f"  cache check: the original's new trace vs the cached one: "
              f"{'identical' if ok else 'DIFFERENT'}")
        if not ok:
            print(report)
        return trace, None, ok
    print("  reference: ran the original" + ("" if use_cache else " (cache not used)"))
    if use_cache:
        store(key, ticks, trace, log)
    return trace, None, True
