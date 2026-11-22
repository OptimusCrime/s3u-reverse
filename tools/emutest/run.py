#!/usr/bin/env python3
"""Emulator differential tests: our replacements against the original code.

Usage: python tools/emutest/run.py [filter] [--cases N] [--seed N] [--build Debug]
                                   [--bin-dir DIR] [--jobs N]

For every spec in tools/emutest/specs.py (or those whose name contains
`filter`), runs N cases. Each case calls the original function from S3.EXE
and our replacement from s3re.dll in the emulator (tools/emutest/emu.py) on
the same arguments, registers and lazily-random world, and compares:

  - return value (EAX, masked per spec; 0 = not compared),
  - every byte written to the world or to S3.EXE's memory (except a spec's
    ignore_bytes, documented at the spec),
  - bytes popped from the stack on return,
  - callee-saved registers EBX, ESI, EDI, EBP (must also be preserved),
  - the calls of the game's DebugLog: format and arguments, in order,
  - the calls of operator new/delete (sizes and blocks; a spec's new_fails
    makes new return null in some cases) and of std::_Xlen /
    std::_Xran / _CxxThrowException (which end the run as a throw would;
    then the stack and the callee-saved registers are not compared),
  - the probed calls (virtual, COM, API and Spec.callees) and their
    arguments, in order,
  - faults: if both fault the case is skipped as invalid input; if only one
    faults, that's a mismatch.

It reports the basic-block coverage of each original function; functions
with uncovered blocks are listed. Exit code 0 only if there were no
mismatches (and, with --require-coverage, full coverage).

Specs run in parallel worker processes (--jobs, default: half the cores
minus 2, fewer if memory is short, or S3RE_EMU_JOBS; workers run at
below-normal priority). Each worker has its own emulator
and resets it before every spec (emu.Emulator.reset), so the results don't
depend on the number of workers or on the order; the output is printed in
the order of specs.py either way. Each spec's run time is kept in
build/emutest_times.json, and the slowest specs are started first.

Needs a build (the DLL and its linker map s3re.map) and
`python -m pip install -r tools/requirements.txt`. Doesn't start the game.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import math
import multiprocessing
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import emu  # noqa: E402
import specs  # noqa: E402

ROOT = emu.ROOT


def first_difference(original: emu.Outcome, ours: emu.Outcome, mask: int,
                     ignored: frozenset[int] = frozenset()) -> str | None:
    if original.spec_error or ours.spec_error:
        return f"spec error: {original.spec_error or ours.spec_error}"
    if (original.fault is None) != (ours.fault is None):
        return f"fault: original {original.fault!r}, ours {ours.fault!r}"
    if original.thrown != ours.thrown:
        def show_throw(thrown):
            if thrown is None:
                return "none"
            if isinstance(thrown, int):
                return f"{thrown:#x}"
            return "(" + ", ".join(f"{v:#x}" for v in thrown) + ")"
        return f"throw: original {show_throw(original.thrown)}, ours {show_throw(ours.thrown)}"
    if original.heap_calls != ours.heap_calls:
        return f"heap calls: original {original.heap_calls}, ours {ours.heap_calls}"
    if original.probe_calls != ours.probe_calls:
        def show_probes(calls):
            return "; ".join(str(tuple(f"{v:#x}" if isinstance(v, int) else v for v in call))
                             for call in calls) or "none"
        return (f"virtual/API calls: original {show_probes(original.probe_calls)}, "
                f"ours {show_probes(ours.probe_calls)}")
    thrown = original.thrown is not None  # then the stack, EAX and registers are mid-function
    if not thrown and original.popped != ours.popped:
        return f"stack: original pops {original.popped} bytes, ours {ours.popped}"
    if not thrown and mask and (original.eax & mask) != (ours.eax & mask):
        return f"EAX: original {original.eax:#010x}, ours {ours.eax:#010x} (mask {mask:#x})"
    if original.debug_logs != ours.debug_logs:
        def show(calls):
            return "; ".join("(" + ", ".join(f"{v:#x}" for v in call) + ")" for call in calls) or "none"
        return f"DebugLog calls: original {show(original.debug_logs)}, ours {show(ours.debug_logs)}"
    for name in emu.CALLEE_SAVED:
        if not thrown and original.saved[name] != ours.saved[name]:
            return f"{name}: original {original.saved[name]:#x}, ours {ours.saved[name]:#x}"
    for address in sorted(set(original.final) | set(ours.final)):
        if address in ignored:
            continue  # Spec.ignore_bytes
        before = original.initial.get(address, ours.initial.get(address))
        a = original.final.get(address, before)
        b = ours.final.get(address, before)
        if a != b:
            where = (f"world+{address - emu.WORLD_BASE:#x}"
                     if emu.WORLD_BASE <= address < emu.WORLD_BASE + emu.WORLD_SIZE
                     else f"{address:#x}")
            return f"memory at {where}: original {a:#04x}, ours {b:#04x} (before {before:#04x})"
    return None


def case_count(spec: specs.Spec, cases: int) -> int:
    return cases if spec.max_cases is None else min(cases, spec.max_cases)


def seed_name(name: str) -> str:
    """The name a spec's inputs are derived from: the name before the
    GameWorld -> CGameData rename (2026-11-22; GameWorld_Create became
    CGameData_GenerateRandomMap), so every spec keeps the inputs it was
    verified with. Same rule as SeedName in src/s3re/test/difftest.cpp."""
    return name.replace("CGameData_GenerateRandomMap", "GameWorld_Create").replace("CGameData", "GameWorld")


def run_spec(machine: emu.Emulator, spec: specs.Spec, cases: int, seed: int,
             first: int = 0, last: int | None = None) -> dict:
    """Runs the spec's cases first..last-1 (default: all). The inputs of the
    skipped cases before `first` are still drawn (and their globals
    written), so case k gets the same inputs and state as in a run of all
    cases; merge_chunks combines the parts of a spec run in pieces."""
    machine.reset()  # every spec starts from the loaded images
    original = machine.analyze(spec.original, spec.cover_until)
    ours = machine.our_function(spec.ours)
    rng = random.Random(f"{seed_name(spec.ours)}:{seed}")
    pool = sorted(set(original.constants) | set(range(0, 9)) | {0xFF, 0xFFFF, 0xFFFFFFFF})
    machine.blocks = set()
    machine.virtuals = spec.virtuals or {}
    machine.virtual_effects = spec.virtual_effects or {}
    machine.set_callees(spec.callees or {})
    ignored = frozenset(emu.WORLD_BASE + offset for offset in spec.ignore_bytes)
    result = {"cases": 0, "invalid": 0, "mismatch": None, "mismatch_case": None}
    cases = case_count(spec, cases)
    last = cases if last is None else min(last, cases)
    for case in range(last):
        args = [generate(rng, pool) for generate in spec.args]
        presets = {}
        if spec.presets:
            for offset, value, size in spec.presets(rng, pool, args):
                for i in range(size):
                    presets[emu.WORLD_BASE + offset + i] = (value >> (8 * i)) & 0xFF
        registers = {name: rng.getrandbits(32) for name in ("eax", *emu.CALLEE_SAVED)}
        edx = spec.edx(rng, pool) if spec.edx else rng.getrandbits(32)
        stack_args = args
        if spec.convention == "fastcall":
            # The first two arguments go in ECX and EDX, the rest on the stack.
            ecx = args[0]
            if len(args) > 1:
                edx = args[1]
            stack_args = args[2:]
        elif spec.convention == "thiscall":
            ecx = emu.WORLD_BASE
        elif spec.convention == "logicthread":
            # A fake CLogicThread inside the world area whose world pointer
            # (+0x3a0) is the world; its other fields are lazily random.
            ecx = emu.WORLD_BASE + specs.LOGIC_THREAD
            for i in range(4):
                presets[emu.WORLD_BASE + specs.LOGIC_THREAD + 0x3a0 + i] =                     (emu.WORLD_BASE >> (8 * i)) & 0xFF
        else:
            ecx = rng.getrandbits(32)
        machine.begin_case(rng.getrandbits(32), pool, presets)
        machine.new_fails = spec.new_fails > 0 and rng.random() < spec.new_fails
        machine.new_fails_size = spec.new_fails_size
        if spec.globals:
            for address, value, size in spec.globals(rng, pool, args):
                mask = (1 << (8 * size)) - 1
                machine.uc.mem_write(address, (value & mask).to_bytes(size, "little"))
        if case < first:
            continue  # another part of the spec runs this case
        limit =spec.max_instructions or emu.MAX_INSTRUCTIONS
        a = machine.call(spec.original, ecx, edx, stack_args, registers,
                         cover=(original.start, original.end), max_instructions=limit)
        b = machine.call(ours, ecx, edx, stack_args, registers, max_instructions=limit)
        if a.fault and b.fault and not (a.spec_error or b.spec_error):
            result["invalid"] += 1
            continue
        result["cases"] += 1
        problem = first_difference(a, b, spec.eax_mask, ignored)
        if problem is None and a.thrown is None:
            for name, value in registers.items():
                if name != "eax" and a.saved[name] != value:
                    problem = f"{name} not preserved by the original (spec wrong?)"
        if problem:
            result["mismatch"] = (f"case {case}: args "
                                  f"[{', '.join(f'{x:#x}' for x in args)}]: {problem}")
            result["mismatch_case"] = case
            break
    leaders = [a for a in original.leaders if a not in spec.exempt_blocks]
    covered = machine.covered(leaders)
    result["blocks"] = len(leaders)
    result["covered"] = len(covered)
    result["uncovered"] = sorted(set(leaders) - covered)
    result["covered_set"] = sorted(covered)
    return result


def merge_chunks(parts: list[dict]) -> dict:
    """The result of a whole spec from its parts (in case order), as a run
    of all cases would report it: counts and coverage up to the first
    mismatch."""
    merged = {"cases": 0, "invalid": 0, "mismatch": None, "mismatch_case": None}
    covered: set[int] = set()
    for part in parts:
        merged["cases"] += part["cases"]
        merged["invalid"] += part["invalid"]
        covered |= set(part["covered_set"])
        if part["mismatch"]:
            merged["mismatch"], merged["mismatch_case"] = part["mismatch"], part["mismatch_case"]
            break
    leaders = set(parts[0]["covered_set"]) | set(parts[0]["uncovered"])
    merged["blocks"] = parts[0]["blocks"]
    merged["covered"] = len(covered)
    merged["uncovered"] = sorted(leaders - covered)
    merged["covered_set"] = sorted(covered)
    return merged


TIMES_PATH = os.path.join(ROOT, "build", "emutest_times.json")
# Peak memory (private bytes) of one worker over a full run, measured
# 2026-11-10; caps the default --jobs on machines with little free memory.
WORKER_MEMORY = 600 * 2**20


def spec_key(spec: specs.Spec) -> str:
    return f"{spec.ours}@{spec.original:#x}"


def load_times() -> dict:
    try:
        with open(TIMES_PATH, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def save_times(times: dict) -> None:
    try:
        os.makedirs(os.path.dirname(TIMES_PATH), exist_ok=True)
        with open(TIMES_PATH, "w", encoding="utf-8") as f:
            json.dump(times, f, indent=0, sort_keys=True)
    except OSError:
        pass


def available_memory() -> int | None:
    """Free physical memory in bytes (Windows; None elsewhere)."""
    if os.name != "nt":
        return None

    class MemoryStatus(ctypes.Structure):
        _fields_ = [("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong),
                    *((name, ctypes.c_ulonglong) for name in (
                        "ullTotalPhys", "ullAvailPhys", "ullTotalPageFile", "ullAvailPageFile",
                        "ullTotalVirtual", "ullAvailVirtual", "ullAvailExtendedVirtual"))]
    status = MemoryStatus()
    status.dwLength = ctypes.sizeof(status)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
        return None
    return status.ullAvailPhys


def peak_memory() -> int:
    """Peak private bytes of this process (Windows; 0 elsewhere)."""
    if os.name != "nt":
        return 0

    class Counters(ctypes.Structure):
        _fields_ = [("cb", ctypes.c_ulong), ("PageFaultCount", ctypes.c_ulong),
                    *((name, ctypes.c_size_t) for name in (
                        "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
                        "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage",
                        "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage"))]
    counters = Counters()
    counters.cb = ctypes.sizeof(counters)
    kernel32, psapi = ctypes.windll.kernel32, ctypes.windll.psapi
    kernel32.GetCurrentProcess.restype = ctypes.c_void_p
    psapi.GetProcessMemoryInfo.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulong]
    psapi.GetProcessMemoryInfo(kernel32.GetCurrentProcess(), ctypes.byref(counters), counters.cb)
    return counters.PeakPagefileUsage


def default_jobs() -> int:
    # Half the cores at most (the user's machine became unresponsive with 18
    # workers while other agents were also running), overridable with the
    # environment variable S3RE_EMU_JOBS.
    override = os.environ.get("S3RE_EMU_JOBS")
    if override:
        return max(1, int(override))
    jobs = max(1, (os.cpu_count() or 1) // 2 - 2)
    memory = available_memory()
    if memory is not None:
        jobs = min(jobs, max(1, int(memory * 0.8) // WORKER_MEMORY))
    return jobs


# -- workers (one emulator per process) --------------------------------------

_machine: emu.Emulator | None = None


def _worker_init(bin_dir: str) -> None:
    global _machine
    # Below-normal priority keeps the machine responsive while the suite runs.
    if sys.platform == "win32":
        BELOW_NORMAL_PRIORITY_CLASS = 0x4000
        kernel32 = ctypes.windll.kernel32
        kernel32.SetPriorityClass(kernel32.GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS)
    _machine = emu.Emulator(os.path.join(ROOT, "Settlers3Ultimate", "S3.EXE"),
                            os.path.join(bin_dir, "s3re.dll"),
                            os.path.join(bin_dir, "s3re.map"))


def _worker_run(task: tuple) -> tuple[int, int, dict, float, int]:
    """(self-test?, spec index, cases, seed, part, first case, end case) ->
    (index, part, result, seconds, peak memory). Specs hold lambdas, so
    workers get indices into specs.py."""
    self_test, index, cases, seed, part, first, last = task
    spec = (specs.SELF_TEST if self_test else specs.SPECS)[index]
    started = time.time()
    result = run_spec(_machine, spec, cases, seed, first, last)
    return index, part, result, time.time() - started, peak_memory()


def run_all(selected: list[tuple[int, specs.Spec]], self_test: bool, cases: int, seed: int,
            bin_dir: str, jobs: int, report) -> int:
    """Runs the specs (in `jobs` worker processes if more than 1) and calls
    report(spec, result, seconds) for each, in the order of `selected`, as
    soon as it and all before it are done. Returns a worker's peak memory.

    Specs that took long in earlier runs are split into parts (ranges of
    cases, see run_spec) that run in parallel, so one slow spec doesn't set
    the wall time; merge_chunks puts them together again."""
    times = load_times()
    # Longest first; specs without a time count as slow, so new ones start early.
    order = sorted(selected, key=lambda item: -times.get(spec_key(item[1]), 1e9)) \
        if jobs > 1 else selected
    total = sum(times.get(spec_key(spec), 1.0) for _, spec in selected)
    target = max(60.0, total / jobs / 2)  # seconds per part
    tasks = []
    parts_of: dict[int, int] = {}
    for index, spec in order:
        n = case_count(spec, cases)
        parts = 1
        if jobs > 1 and spec_key(spec) in times:
            parts = max(1, min(n, math.ceil(times[spec_key(spec)] / target)))
        parts_of[index] = parts
        bounds = [n * k // parts for k in range(parts + 1)]
        tasks += [(self_test, index, cases, seed, k, bounds[k], bounds[k + 1]) for k in range(parts)]
    by_index = dict(selected)
    pending = [index for index, _ in selected]
    pieces: dict[int, dict[int, tuple[dict, float]]] = {}
    done: dict[int, tuple[dict, float]] = {}
    peak = 0

    def finish(outcome):
        nonlocal peak
        index, part, result, seconds, memory = outcome
        peak = max(peak, memory)
        pieces.setdefault(index, {})[part] = (result, seconds)
        if len(pieces[index]) < parts_of[index]:
            return
        ordered = [pieces[index][k] for k in range(parts_of[index])]
        seconds = sum(s for _, s in ordered)
        done[index] = (ordered[0][0] if len(ordered) == 1 else merge_chunks([r for r, _ in ordered]),
                       seconds)
        if not self_test:
            times[spec_key(by_index[index])] = round(seconds, 2)
        while pending and pending[0] in done:
            first = pending.pop(0)
            report(by_index[first], *done.pop(first))

    try:
        if jobs <= 1:
            _worker_init(bin_dir)
            for task in tasks:
                finish(_worker_run(task))
        else:
            context = multiprocessing.get_context("spawn")
            with context.Pool(jobs, initializer=_worker_init, initargs=(bin_dir,)) as pool:
                for outcome in pool.imap_unordered(_worker_run, tasks, chunksize=1):
                    finish(outcome)
    finally:
        if not self_test:
            save_times(times)
    return peak


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("filter", nargs="?", default="")
    parser.add_argument("--cases", type=int, default=2000)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--build", default="Debug",
                        help="configuration under build/x86/bin (Debug or Release)")
    parser.add_argument("--bin-dir", help="directory with s3re.dll and s3re.map "
                                          "(instead of build/x86/bin/BUILD)")
    parser.add_argument("--jobs", "-j", type=int, default=None,
                        help="worker processes (default: cores minus 2, fewer if memory "
                             "is short; 1 runs everything in this process)")
    parser.add_argument("--memory", action="store_true",
                        help="print the worker count, a worker's peak memory and the wall time")
    parser.add_argument("--require-coverage", action="store_true")
    parser.add_argument("--all-uncovered", action="store_true",
                        help="list every uncovered block, not just the first six")
    parser.add_argument("--self-test", action="store_true",
                        help="run the deliberately wrong pairings in specs.SELF_TEST; "
                             "each must be caught")
    parser.add_argument("--changed", action="store_true",
                        help="only the specs the changes since the last full clean run "
                             "(or --since REF) can affect; see tools/emutest/changed.py")
    parser.add_argument("--since", metavar="REF",
                        help="with --changed: compare with this git commit instead")
    parser.add_argument("--dry-run", action="store_true",
                        help="with --changed: print the selection, don't run it")
    args = parser.parse_args()

    bin_dir = args.bin_dir or os.path.join(ROOT, "build", "x86", "bin", args.build)
    started = time.time()
    jobs_wanted = args.jobs if args.jobs is not None else default_jobs()

    def show_memory(jobs, peak):
        if args.memory:
            print(f"{jobs} worker(s), peak memory of a worker {peak / 2**20:.0f} MB, "
                  f"{time.time() - started:.0f}s wall time")

    def self_test() -> bool:
        selected = list(enumerate(specs.SELF_TEST))
        jobs = max(1, min(jobs_wanted, len(selected)))
        caught = 0

        def report(spec, r, seconds):
            nonlocal caught
            print(f"{'caught' if r['mismatch'] else 'MISSED':8} {spec.ours} against "
                  f"{spec.original:#x}: {r['mismatch'] or 'no difference found'}", flush=True)
            caught += bool(r["mismatch"])

        peak = run_all(selected, True, 500, args.seed, bin_dir, jobs, report)
        print(f"self-test: {caught} of {len(specs.SELF_TEST)} wrong pairings caught")
        show_memory(jobs, peak)
        return caught == len(specs.SELF_TEST)

    if args.self_test:
        return 0 if self_test() else 1

    selected = [(i, s) for i, s in enumerate(specs.SPECS)
                if args.filter.lower() in s.ours.lower()]
    harness_changed = False
    if args.changed or args.since:
        import changed  # only needed here
        symbols = emu.read_dll_symbols(os.path.join(bin_dir, "s3re.map"))
        names, harness_changed, why = changed.select(
            specs, lambda name: emu.find_our_function(symbols, name), bin_dir, args.since)
        total = len(selected)
        if names is not None:
            selected = [(i, s) for i, s in selected if s.ours in names]
        print(f"{len(selected)} of {total} specs: {why}", flush=True)
        if args.dry_run:
            for _, spec in selected:
                print(f"  {spec.ours}")
            return 0
    # A full run (every spec, default cases and seed) becomes the new
    # reference for --changed when it passes cleanly.
    full_run = len(selected) == len(specs.SPECS) and args.cases >= 2000 and args.seed == 1 \
        and args.build == "Debug"
    jobs = max(1, min(jobs_wanted, len(selected)))

    mismatches = 0
    incomplete = 0
    incomplete_names: list[str] = []

    def report(spec, r, seconds):
        nonlocal mismatches, incomplete
        status = "MISMATCH" if r["mismatch"] else "ok"
        coverage = f"{r['covered']}/{r['blocks']} blocks"
        if r["uncovered"]:
            shown = r["uncovered"] if args.all_uncovered else r["uncovered"][:6]
            coverage += " (uncovered: " + ", ".join(f"{a:#x}" for a in shown) + \
                        (" ..." if len(r["uncovered"]) > len(shown) else "") + ")"
            incomplete += 1
            incomplete_names.append(spec.ours)
        print(f"{status:8} {spec.ours:45} {r['cases']:6} cases, {r['invalid']:4} invalid, "
              f"{coverage}  [{seconds:.1f}s]", flush=True)
        if r["mismatch"]:
            print(f"         {r['mismatch']}", flush=True)
            mismatches += 1

    peak = run_all(selected, False, args.cases, args.seed, bin_dir, jobs, report) if selected else 0
    print(f"{len(selected)} function(s): {mismatches} mismatch(es), "
          f"{incomplete} with incomplete coverage")
    show_memory(jobs, peak)
    failed = bool(mismatches or (args.require_coverage and incomplete))
    if full_run and not mismatches:
        # Also with incomplete coverage (CGameData_Create is known to be
        # incomplete): a spec whose inputs didn't change has the same
        # coverage next time, so --changed may skip it. The incomplete ones
        # are kept in the record.
        import changed
        changed.record_full_pass(args.bin_dir or args.build, incomplete_names)
    if harness_changed:
        print("the harness or the build setup changed: running the self-test too", flush=True)
        failed |= not self_test()
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
