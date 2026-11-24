#!/usr/bin/env python3
"""Emulator differential tests: our replacements against the original code.

Usage: python tools/emutest/run.py [filter] [--cases N] [--seed N] [--build Release|Debug]
                                   [--bin-dir DIR] [--jobs N] [--changed] [--full]
                                   [--quick] [--budget S]

Per batch: `run.py --changed --require-coverage` runs only the specs without
a recorded pass for their current compiled code (tools/emutest/changed.py),
in quick mode: a spec stops once every block is covered and a quarter of its
cases ran, no new case starts after --budget seconds (60; "budget" in the
report; a warning, and no recorded pass, if coverage is incomplete then),
and Spec.quick_cases caps the slowest specs. --full runs every case without
a budget (round ends, milestones).

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

Specs run in parallel worker processes (--jobs, default: the free logical
cores minus 2, fewer if 80 % of the free memory doesn't hold WORKER_MEMORY_MB
per worker plus the Spec.memory_mb extras, or S3RE_EMU_JOBS; workers run at
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


# Quick runs (the default of --changed): a spec stops once every block is
# covered and at least this fraction of its cases ran.
QUICK_FRACTION = 0.25
# Quick runs: seconds per spec after which no further case starts.
DEFAULT_BUDGET = 60.0


def case_count(spec: specs.Spec, cases: int) -> int:
    cases *= spec.case_scale
    return cases if spec.max_cases is None else min(cases, spec.max_cases)


# New name -> name before the renames of functions named after old guesses
# (2026-11-23, session 50); the same table as SeedName in src/s3re/test/difftest.cpp.
RENAMED_FUNCTIONS = (
    ("CGameData_FindNearbyOwnCatapult", "CGameData_FindNearbySettlerType29"),
    ("CGameData_IsFreeOfCatapultsNearby", "CGameData_IsFreeOfSettlerType29Nearby"),
    ("CGameData_UnblockCastleTiles", "CGameData_UnblockType12BuildingTiles"),
    ("CGameData_MakeFieldTile", "CGameData_SetTerrain16Patch"),
    ("CGameData_IsFreeSmallTowerSite", "CGameData_IsFreeCastleSite"),
    ("CGraphicsData_TextAddress", "GfxData_FrameOffset"),
    ("CGraphicsData_SequenceFrameCount", "GfxData_SpriteWord"),
    ("CGraphicsData_GuiImagePointer", "GfxData_FramePointer_5fac70"),
    ("CGraphicsData_AnimationPointer", "GfxData_AnimationPointer_5faaf0"),
    ("GfxDword_Construct", "GfxData_SetDword"),
    # Session 50 renames.
    ("png_free", "Picture_FreeIfBoth"),
    ("BLACK_BOARD_NOTE_ExecuteAtHandler", "BLACK_BOARD_NOTE_CancelAtHandler"),
    ("AI_SiteTree_begin", "AI_ListFirst"),
    ("std_multimap_end_580220", "AI_List_Begin"),
    ("std_multimap_empty_580230", "AI_List_Empty"),
)


def seed_name(name: str) -> str:
    """The name a spec's inputs are derived from: the name before the
    renames (RENAMED_FUNCTIONS, 2026-11-23) and before the GameWorld ->
    CGameData rename (2026-11-22; GameWorld_Create became
    CGameData_GenerateRandomMap), so every spec keeps the inputs it was
    verified with. Same rule as SeedName in src/s3re/test/difftest.cpp."""
    for now, before in RENAMED_FUNCTIONS:
        name = name.replace(now, before, 1)
    return name.replace("CGameData_GenerateRandomMap", "GameWorld_Create").replace("CGameData", "GameWorld")


def quick_case_count(spec: specs.Spec, cases: int) -> int:
    """Cases of a quick run: Spec.quick_cases if the spec has it (specs too
    slow for per-batch runs, whose main evidence is elsewhere), else all."""
    n = case_count(spec, cases)
    return n if spec.quick_cases is None else min(n, spec.quick_cases)


def run_spec(machine: emu.Emulator, spec: specs.Spec, cases: int, seed: int,
             first: int = 0, last: int | None = None, quick: bool = False,
             budget: float | None = None) -> dict:
    """Runs the spec's cases first..last-1 (default: all). The inputs of the
    skipped cases before `first` are still drawn (and their globals
    written), so case k gets the same inputs and state as in a run of all
    cases; merge_chunks combines the parts of a spec run in pieces. This
    relies on no case depending on how earlier cases or specs ran: their
    writes are undone, and Emulator.call/reset/write_image reset the CPU
    state and the translated-code cache (independence_check tests it).

    quick: stop as soon as every block is covered and at least
    QUICK_FRACTION of the cases ran (cases are independent, so the cases
    that ran are exactly those of a full run's start); result["stopped"] =
    "covered". budget: seconds after which no further case starts
    (result["stopped"] = "budget")."""
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
    result = {"cases": 0, "invalid": 0, "mismatch": None, "mismatch_case": None, "stopped": None}
    cases = quick_case_count(spec, cases) if quick else case_count(spec, cases)
    last = cases if last is None else min(last, cases)
    leaders = [a for a in original.leaders if a not in spec.exempt_blocks]
    remaining = set(leaders)  # blocks not yet covered (quick mode)
    quick_min = max(1, math.ceil(cases * QUICK_FRACTION))
    started = time.time()
    for case in range(last):
        if case > first:
            ran = case - first
            if budget is not None and time.time() - started > budget:
                result["stopped"] = "budget"
                break
            if quick and ran >= quick_min and (ran == quick_min or ran % 16 == 0):
                remaining -= machine.covered(sorted(remaining))
                if not remaining:
                    result["stopped"] = "covered"
                    break
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
                machine.write_image(address, (value & mask).to_bytes(size, "little"))
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
    result["ran_all"] = result["stopped"] is None and result["mismatch"] is None
    result["planned"] = last - first
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
    merged = {"cases": 0, "invalid": 0, "mismatch": None, "mismatch_case": None, "stopped": None}
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
    merged["ran_all"] = merged["mismatch"] is None and all(p["ran_all"] for p in parts)
    merged["planned"] = sum(p["planned"] for p in parts)
    return merged


# The independence check of --self-test: (spec, cases, a spec run just
# before each part). Pathing_FindPath runs the code that WalkSettler's
# globals replace with a stub (the bug of 2026-11-22: stale translated code).
INDEPENDENCE_CHECKS = (("CGameData_WalkSettler", 300, "Pathing_FindPath"),
                       ("Pathing_WalkLine", 300, None))


def independence_check(bin_dir: str) -> bool:
    """Checks the promise run_all relies on: a spec split into parts, each
    run after another spec in the same emulator, gives the same merged
    result as one whole run (cases, invalid cases, mismatch and covered
    blocks)."""
    by_name = {spec.ours: spec for spec in specs.SPECS}
    keys = ("cases", "invalid", "mismatch", "covered_set")

    def shown(result, key):
        return len(result[key]) if key == "covered_set" else result[key]

    ok = True
    for name, cases, before in INDEPENDENCE_CHECKS:
        spec = by_name[name]
        _worker_init(bin_dir)  # the whole run in a fresh emulator
        whole = run_spec(_machine, spec, cases, 1)
        _worker_init(bin_dir)
        parts = []
        for first, last in ((0, cases // 3), (cases // 3, 2 * cases // 3), (2 * cases // 3, cases)):
            if before:
                run_spec(_machine, by_name[before], 1, 1)
            parts.append(run_spec(_machine, spec, cases, 1, first, last))
        merged = merge_chunks(parts)
        same = all(whole[k] == merged[k] for k in keys)
        ok &= same
        details = ", ".join(f"{k} {shown(whole, k)}" + ("" if whole[k] == merged[k]
                                                         else f" vs {shown(merged, k)}")
                            for k in keys)
        after = f" (each after {before})" if before else ""
        print(f"{'same' if same else 'DIFFERENT':9} {name}: whole vs 3 parts{after}: {details}",
              flush=True)
    return ok


TIMES_PATH = os.path.join(ROOT, "build", "emutest_times.json")
# Peak memory (private bytes) of one worker running ordinary specs, in MB
# (a worker peaked at ~380 MB over a whole run, 2026-11-10); specs far above
# it say so with Spec.memory_mb. (A runaway case that faults after the
# instruction limit is the usual cause: every byte it writes is tracked.)
WORKER_MEMORY_MB = 400


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


def free_cores(sample: float = 0.5) -> float:
    """Logical cores not busy right now (Windows: from GetSystemTimes over
    `sample` seconds; elsewhere all cores)."""
    cores = os.cpu_count() or 1
    if os.name != "nt":
        return float(cores)

    def times():
        idle, kernel, user = (ctypes.c_ulonglong() for _ in range(3))
        ctypes.windll.kernel32.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user))
        return idle.value, kernel.value + user.value  # kernel time includes idle time
    idle0, total0 = times()
    time.sleep(sample)
    idle1, total1 = times()
    if total1 <= total0:
        return float(cores)
    return cores * (idle1 - idle0) / (total1 - total0)


def default_jobs(selected: list[specs.Spec]) -> tuple[int, str]:
    """Worker processes for these specs, and why: the free logical cores
    minus 2, and no more than fit in 80 % of the free memory, counting
    WORKER_MEMORY_MB per worker plus the extra of each spec with a larger
    Spec.memory_mb (a worker keeps its peak after such a spec). The
    environment variable S3RE_EMU_JOBS (or --jobs) overrides it."""
    override = os.environ.get("S3RE_EMU_JOBS")
    if override:
        return max(1, int(override)), "S3RE_EMU_JOBS"
    cores = free_cores()
    jobs = max(1, int(cores) - 2)
    why = f"{cores:.1f} free cores"
    memory = available_memory()
    if memory is not None:
        extra = sum(max(0, (spec.memory_mb or 0) - WORKER_MEMORY_MB) for spec in selected) * 2**20
        fit = max(1, int((memory * 0.8 - extra) // (WORKER_MEMORY_MB * 2**20)))
        why += f", {memory / 2**30:.1f} GB free memory"
        if fit < jobs:
            jobs, why = fit, why + " (memory-bound)"
    return jobs, why


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
    self_test, index, cases, seed, part, first, last, quick, budget = task
    spec = (specs.SELF_TEST if self_test else specs.SPECS)[index]
    started = time.time()
    result = run_spec(_machine, spec, cases, seed, first, last, quick, budget)
    return index, part, result, time.time() - started, peak_memory()


def run_all(selected: list[tuple[int, specs.Spec]], self_test: bool, cases: int, seed: int,
            bin_dir: str, jobs: int, report, quick: bool = False,
            budget: float | None = None) -> int:
    """Runs the specs (in `jobs` worker processes if more than 1) and calls
    report(spec, result, seconds) for each, in the order of `selected`, as
    soon as it and all before it are done. Returns a worker's peak memory.

    Specs that took long in earlier runs are split into parts (ranges of
    cases, see run_spec) that run in parallel, so one slow spec doesn't set
    the wall time; merge_chunks puts them together again. Quick runs
    (`quick`, `budget`, see run_spec) don't split: a part can't know when
    the whole spec is covered, and the budget bounds a spec anyway."""
    times = load_times()
    # Longest first; specs without a time count as slow, so new ones start early.
    order = sorted(selected, key=lambda item: -times.get(spec_key(item[1]), 1e9)) \
        if jobs > 1 else selected
    total = sum(times.get(spec_key(spec), 1.0) for _, spec in selected)
    target = max(60.0, total / jobs / 2)  # seconds per part
    tasks = []
    parts_of: dict[int, int] = {}
    for index, spec in order:
        n = quick_case_count(spec, cases) if quick else case_count(spec, cases)
        parts = 1
        if jobs > 1 and spec_key(spec) in times and not quick and budget is None:
            parts = max(1, min(n, math.ceil(times[spec_key(spec)] / target)))
        parts_of[index] = parts
        bounds = [n * k // parts for k in range(parts + 1)]
        tasks += [(self_test, index, cases, seed, k, bounds[k], bounds[k + 1], quick, budget)
                  for k in range(parts)]
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
        if not self_test and done[index][0]["ran_all"]:
            times[spec_key(by_index[index])] = round(seconds, 2)  # the time of all cases
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


# The build the emulator tests by default (2026-11-23: the Release full suite
# gave 0 mismatches over all 556 specs, like Debug, about 5x faster on the
# heavy specs).
DEFAULT_BUILD = "Release"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("filter", nargs="?", default="")
    parser.add_argument("--cases", type=int, default=2000)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--build", default=DEFAULT_BUILD,
                        help="configuration under build/x86/bin (default Release: its full "
                             "suite matches Debug's and runs several times faster; Debug for "
                             "debugging a mismatch)")
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
                        help="run the deliberately wrong pairings in specs.SELF_TEST "
                             "(each must be caught) and the check that a spec split into "
                             "parts gives the result of a whole run")
    parser.add_argument("--changed", action="store_true",
                        help="only the specs without a recorded pass for their current compiled "
                             "code, specs.py entry and harness (see tools/emutest/changed.py); "
                             "runs in quick mode unless --full")
    parser.add_argument("--since", metavar="REF",
                        help="select by changed source files since this commit instead "
                             "(the older, header-based selection)")
    parser.add_argument("--by-headers", action="store_true",
                        help="with --changed: the header-based selection since the last full "
                             "clean run (as before 2026-11-23)")
    parser.add_argument("--dry-run", action="store_true",
                        help="with --changed: print the selection, don't run it")
    parser.add_argument("--full", action="store_true",
                        help="every case of every selected spec, no time budget (round ends, "
                             "milestones)")
    parser.add_argument("--quick", action="store_true",
                        help="quick mode without --changed: stop a spec once it is fully covered "
                             f"and {QUICK_FRACTION * 100:.0f} %% of its cases ran, with a time budget")
    parser.add_argument("--budget", type=float, default=DEFAULT_BUDGET,
                        help=f"quick mode: seconds per spec after which no further case starts "
                             f"(default {DEFAULT_BUDGET:.0f}; 0: none)")
    args = parser.parse_args()

    bin_dir = args.bin_dir or os.path.join(ROOT, "build", "x86", "bin", args.build)
    started = time.time()
    by_code = args.changed and not args.since and not args.by_headers
    quick = (args.changed or args.quick) and not args.full
    budget = args.budget if quick and args.budget > 0 else None
    standard_inputs = args.cases >= 2000 and args.seed == 1  # what a recorded pass means
    symbols = emu.read_dll_symbols(os.path.join(bin_dir, "s3re.map"))

    def machine_symbol(name):
        return emu.find_our_function(symbols, name)

    def show_memory(jobs, peak):
        if args.memory:
            print(f"{jobs} worker(s), peak memory of a worker {peak / 2**20:.0f} MB, "
                  f"{time.time() - started:.0f}s wall time")

    def jobs_for(chosen: list[specs.Spec]) -> int:
        if args.jobs is not None:
            jobs, why = args.jobs, "--jobs"
        else:
            jobs, why = default_jobs(chosen)
        jobs = max(1, min(jobs, len(chosen)))
        print(f"{jobs} worker(s) ({why})", flush=True)
        return jobs

    def self_test() -> bool:
        selected = list(enumerate(specs.SELF_TEST))
        jobs = jobs_for([spec for _, spec in selected])
        caught = 0

        def report(spec, r, seconds):
            nonlocal caught
            print(f"{'caught' if r['mismatch'] else 'MISSED':8} {spec.ours} against "
                  f"{spec.original:#x}: {r['mismatch'] or 'no difference found'}", flush=True)
            caught += bool(r["mismatch"])

        peak = run_all(selected, True, 500, args.seed, bin_dir, jobs, report)
        print(f"self-test: {caught} of {len(specs.SELF_TEST)} wrong pairings caught")
        show_memory(jobs, peak)
        independent = independence_check(bin_dir)
        passed = caught == len(specs.SELF_TEST) and independent
        if passed:
            import changed
            changed.record_passes({}, changed.harness_fingerprint())
        return passed

    if args.self_test:
        return 0 if self_test() else 1

    selected = [(i, s) for i, s in enumerate(specs.SPECS)
                if args.filter.lower() in s.ours.lower()]
    harness_changed = False
    keys: dict[str, dict] = {}
    if by_code:
        import changed
        names, harness_changed, why, keys = changed.select_by_code(specs, machine_symbol, bin_dir)
        total = len(selected)
        selected = [(i, s) for i, s in selected if s.ours in names]
        print(f"{len(selected)} of {total} specs: {why}", flush=True)
    elif args.changed or args.since:
        import changed  # only needed here
        names, harness_changed, why = changed.select(specs, machine_symbol, bin_dir, args.since)
        total = len(selected)
        if names is not None:
            selected = [(i, s) for i, s in selected if s.ours in names]
        print(f"{len(selected)} of {total} specs: {why}", flush=True)
    if args.dry_run and (args.changed or args.since):
        for _, spec in selected:
            print(f"  {spec.ours}")
        return 0
    if not keys and standard_inputs and selected:
        import changed
        try:
            keys = changed.select_by_code(specs, machine_symbol, bin_dir)[3]
        except (KeyError, OSError) as error:
            print(f"(passes not recorded: {error})", flush=True)
    # A full run (every spec, default cases and seed) becomes the new
    # reference for the header-based selection when it passes cleanly.
    full_run = len(selected) == len(specs.SPECS) and standard_inputs and not quick \
        and args.build == DEFAULT_BUILD
    jobs = jobs_for([spec for _, spec in selected]) if selected else 1
    if quick:
        print(f"quick mode: a spec stops once fully covered after {QUICK_FRACTION:.0%} of its "
              f"cases, budget {budget or 0:.0f}s per spec (--full runs everything)", flush=True)

    mismatches = 0
    incomplete = 0
    incomplete_names: list[str] = []
    over_budget: list[str] = []
    passes: dict[str, dict] = {}
    run_commit = None

    def report(spec, r, seconds):
        nonlocal mismatches, incomplete, run_commit
        status = "MISMATCH" if r["mismatch"] else "ok"
        coverage = f"{r['covered']}/{r['blocks']} blocks"
        if r["uncovered"]:
            shown = r["uncovered"] if args.all_uncovered else r["uncovered"][:6]
            coverage += " (uncovered: " + ", ".join(f"{a:#x}" for a in shown) + \
                        (" ..." if len(r["uncovered"]) > len(shown) else "") + ")"
            incomplete += 1
            incomplete_names.append(spec.ours)
        stopped = ""
        if r["stopped"] == "covered":
            stopped = f" (quick: {r['cases'] + r['invalid']} of {r['planned']} cases)"
        elif r["stopped"] == "budget":
            stopped = f" (budget: {r['cases'] + r['invalid']} of {r['planned']} cases)"
            status = "budget" if not r["mismatch"] else status
            if r["uncovered"] and not r["mismatch"]:
                over_budget.append(spec.ours)
        print(f"{status:8} {spec.ours:45} {r['cases']:6} cases, {r['invalid']:4} invalid, "
              f"{coverage}{stopped}  [{seconds:.1f}s]", flush=True)
        if r["mismatch"]:
            print(f"         {r['mismatch']}", flush=True)
            mismatches += 1
        # A pass: no mismatch, and every block covered or every case run
        # (then the coverage is what a rerun would give too).
        key = keys.get(spec_key(spec))
        if key and standard_inputs and not r["mismatch"] and (not r["uncovered"] or r["ran_all"]):
            if run_commit is None:
                import changed
                run_commit = changed.head_commit()
            passes[spec_key(spec)] = {
                **key, "mode": "quick" if quick else "full", "cases": r["cases"],
                "covered": r["covered"], "blocks": r["blocks"], "commit": run_commit,
                "time": time.strftime("%Y-%m-%dT%H:%M:%S")}

    try:
        peak = run_all(selected, False, args.cases, args.seed, bin_dir, jobs, report,
                       quick, budget) if selected else 0
    finally:
        if passes:
            import changed
            changed.record_passes(passes)
    print(f"{len(selected)} function(s): {mismatches} mismatch(es), "
          f"{incomplete} with incomplete coverage")
    if over_budget:
        print(f"WARNING: {len(over_budget)} spec(s) hit the time budget before full coverage "
              f"(not recorded as passed; run them with --full): {', '.join(over_budget)}")
    show_memory(jobs, peak)
    over = set(over_budget)
    failed = bool(mismatches or (args.require_coverage
                                 and any(name not in over for name in incomplete_names)))
    if full_run and not mismatches:
        # Also with incomplete coverage (CGameData_GenerateRandomMap is known to be
        # incomplete): a spec whose inputs didn't change has the same
        # coverage next time, so the header-based selection may skip it.
        # The incomplete ones are kept in the record.
        import changed
        changed.record_full_pass(args.bin_dir or args.build, incomplete_names)
    if harness_changed:
        print("the harness or the build setup changed: running the self-test too", flush=True)
        failed |= not self_test()
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
