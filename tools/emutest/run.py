#!/usr/bin/env python3
"""Emulator differential tests: our replacements against the original code.

Usage: python tools/emutest/run.py [filter] [--cases N] [--seed N] [--build Debug]

For every spec in tools/emutest/specs.py (or those whose name contains
`filter`), runs N cases. Each case calls the original function from S3.EXE
and our replacement from s3re.dll in the emulator (tools/emutest/emu.py) on
the same arguments, registers and lazily-random world, and compares:

  - return value (EAX, masked per spec; 0 = not compared),
  - every byte written to the world or to S3.EXE's memory,
  - bytes popped from the stack on return,
  - callee-saved registers EBX, ESI, EDI, EBP (must also be preserved),
  - the calls of the game's DebugLog: format and arguments, in order,
  - the calls of operator new/delete (sizes and blocks) and of std::_Xlen /
    std::_Xran (which end the run as a throw would; then the stack and the
    callee-saved registers are not compared),
  - faults: if both fault the case is skipped as invalid input; if only one
    faults, that's a mismatch.

It reports the basic-block coverage of each original function; functions
with uncovered blocks are listed. Exit code 0 only if there were no
mismatches (and, with --require-coverage, full coverage).

Needs a build (the DLL and its linker map s3re.map) and
`python -m pip install -r tools/requirements.txt`. Doesn't start the game.
"""

from __future__ import annotations

import argparse
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import emu  # noqa: E402
import specs  # noqa: E402

ROOT = emu.ROOT


def first_difference(original: emu.Outcome, ours: emu.Outcome, mask: int) -> str | None:
    if original.spec_error or ours.spec_error:
        return f"spec error: {original.spec_error or ours.spec_error}"
    if (original.fault is None) != (ours.fault is None):
        return f"fault: original {original.fault!r}, ours {ours.fault!r}"
    if original.thrown != ours.thrown:
        return f"throw: original {original.thrown}, ours {ours.thrown}"
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
        before = original.initial.get(address, ours.initial.get(address))
        a = original.final.get(address, before)
        b = ours.final.get(address, before)
        if a != b:
            where = (f"world+{address - emu.WORLD_BASE:#x}"
                     if emu.WORLD_BASE <= address < emu.WORLD_BASE + emu.WORLD_SIZE
                     else f"{address:#x}")
            return f"memory at {where}: original {a:#04x}, ours {b:#04x} (before {before:#04x})"
    return None


def run_spec(machine: emu.Emulator, spec: specs.Spec, cases: int, seed: int) -> dict:
    original = machine.analyze(spec.original, spec.cover_until)
    ours = machine.our_function(spec.ours)
    rng = random.Random(f"{spec.ours}:{seed}")
    pool = sorted(set(original.constants) | set(range(0, 9)) | {0xFF, 0xFFFF, 0xFFFFFFFF})
    machine.blocks = set()
    machine.virtuals = spec.virtuals or {}
    result = {"cases": 0, "invalid": 0, "mismatch": None}
    if spec.max_cases is not None:
        cases = min(cases, spec.max_cases)
    for case in range(cases):
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
        if spec.globals:
            for address, value, size in spec.globals(rng, pool, args):
                mask = (1 << (8 * size)) - 1
                machine.uc.mem_write(address, (value & mask).to_bytes(size, "little"))
        a = machine.call(spec.original, ecx, edx, stack_args, registers,
                         cover=(original.start, original.end))
        b = machine.call(ours, ecx, edx, stack_args, registers)
        if a.fault and b.fault and not (a.spec_error or b.spec_error):
            result["invalid"] += 1
            continue
        result["cases"] += 1
        problem = first_difference(a, b, spec.eax_mask)
        if problem is None and a.thrown is None:
            for name, value in registers.items():
                if name != "eax" and a.saved[name] != value:
                    problem = f"{name} not preserved by the original (spec wrong?)"
        if problem:
            result["mismatch"] = (f"case {case}: args "
                                  f"[{', '.join(f'{x:#x}' for x in args)}]: {problem}")
            break
    leaders = [a for a in original.leaders if a not in spec.exempt_blocks]
    covered = machine.covered(leaders)
    result["blocks"] = len(leaders)
    result["covered"] = len(covered)
    result["uncovered"] = sorted(set(leaders) - covered)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("filter", nargs="?", default="")
    parser.add_argument("--cases", type=int, default=2000)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--build", default="Debug")
    parser.add_argument("--require-coverage", action="store_true")
    parser.add_argument("--all-uncovered", action="store_true",
                        help="list every uncovered block, not just the first six")
    parser.add_argument("--self-test", action="store_true",
                        help="run the deliberately wrong pairings in specs.SELF_TEST; "
                             "each must be caught")
    args = parser.parse_args()

    bin_dir = os.path.join(ROOT, "build", "x86", "bin", args.build)
    machine = emu.Emulator(os.path.join(ROOT, "Settlers3Ultimate", "S3.EXE"),
                           os.path.join(bin_dir, "s3re.dll"),
                           os.path.join(bin_dir, "s3re.map"))
    if args.self_test:
        caught = 0
        for spec in specs.SELF_TEST:
            r = run_spec(machine, spec, 500, args.seed)
            print(f"{'caught' if r['mismatch'] else 'MISSED':8} {spec.ours} against "
                  f"{spec.original:#x}: {r['mismatch'] or 'no difference found'}")
            caught += bool(r["mismatch"])
        print(f"self-test: {caught} of {len(specs.SELF_TEST)} wrong pairings caught")
        return 0 if caught == len(specs.SELF_TEST) else 1

    selected = [s for s in specs.SPECS if args.filter.lower() in s.ours.lower()]
    mismatches = 0
    incomplete = 0
    for spec in selected:
        started = time.time()
        r = run_spec(machine, spec, args.cases, args.seed)
        status = "MISMATCH" if r["mismatch"] else "ok"
        coverage = f"{r['covered']}/{r['blocks']} blocks"
        if r["uncovered"]:
            shown = r["uncovered"] if args.all_uncovered else r["uncovered"][:6]
            coverage += " (uncovered: " + ", ".join(f"{a:#x}" for a in shown) + \
                        (" ..." if len(r["uncovered"]) > len(shown) else "") + ")"
            incomplete += 1
        print(f"{status:8} {spec.ours:45} {r['cases']:6} cases, {r['invalid']:4} invalid, "
              f"{coverage}  [{time.time() - started:.1f}s]", flush=True)
        if r["mismatch"]:
            print(f"         {r['mismatch']}")
            mismatches += 1
    print(f"{len(selected)} function(s): {mismatches} mismatch(es), "
          f"{incomplete} with incomplete coverage")
    if mismatches or (args.require_coverage and incomplete):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
