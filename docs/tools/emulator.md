# Emulator differential tests (tools/emutest)

**Where specs live (session 51):** `tools/emutest/specs/` is a package of
parts, one per folder of `src/s3re/game/` (`ai.py`, `settlers.py`, ...;
`msvc6.py`, `watcom.py` for those folders), plus `common.py` (the `Spec`
class, argument generators, helpers used by several parts),
`self_test.py` and `covered_elsewhere.py`. `specs/__init__.py` executes
the parts in its `PARTS` order into one shared namespace (common first),
exactly like the former single `specs.py`, so `specs.SPECS` and every
helper are used as before and a part needs no imports. Put a new spec
into the part of the folder its replacement's `.cpp` is in, its helpers
next to it; a helper that a second part starts using moves to
`common.py`. A new folder gets a new part and an entry in `PARTS`.
**One namespace, so no name twice (guard, session 57):** names in a
helper's body resolve when it is called, so a top-level name bound again
by a later statement (in the same part or a later one) silently changes
every earlier spec whose helpers use it (GFX_HEADERS in session 55;
`evaland_object`, `evaland_globals`, `ground_note_globals`,
`ring_offsets`, `null_string_count` in session 57, renamed so that each
spec keeps the value it was written for; all presets and globals gave
the same output before and after on 30 seeds, and the 23 affected specs
passed again). `specs/__init__.py` therefore executes the parts
statement by statement and raises `NameClashError` at import (run.py,
changed.py, everything) on a top-level name bound twice with different
values: defs and classes are compared by their source (the same helper
text twice is fine), everything else by `==`. Exempt: names starting
with `_` (loop temporaries); in-place changes (`SPECS += [...]`,
`TABLE[key] = ...`) are not rebindings. When it fires, give the later
helper its own name (a `Spec(...)` takes `presets=`/`globals=` when it
is built, so the earlier specs keep theirs).
`changed.py` fingerprints the parts concatenated in `PARTS` order
(`specs_source`), so moving an entry between parts doesn't change its
fingerprint.

Moved out of `CLAUDE.md` on 2026-11-24 (workflow step 4 in detail).

Add a spec to `tools/emutest/specs/` and run `python
tools/emutest/run.py [filter] --require-coverage` (no game needed; needs
`pip install -r tools/requirements.txt`). It runs the original bytes
from S3.EXE and our function from s3re.dll in an x86 emulator (Unicorn)
on identical arguments and a lazily-random world, compares EAX, every
byte written to the world/S3.EXE, bytes popped and callee-saved
registers, and reports basic-block coverage of the original. The gate is
0 mismatches and every block covered; add presets to a spec when random
data doesn't reach a branch (`globals` sets S3.EXE globals, which
otherwise keep their image values; `edx` sets EDX for __fastcall
helpers; `cover_until`/`exempt_blocks` handle code past a false function
start and blocks that are dead or only reachable by a C++ exception, with
the reason in a comment). The game's operator new/delete are a bump
allocator in the world area (calls compared), std::_Xlen/_Xran end the
run as a throw (compared), FS points at a fake TIB so SEH frames work.
Virtual calls: point an object's vtable at `emu.FAKE_VTABLE` with a
preset and declare the slots in `Spec.virtuals` (calls recorded and
compared); imported Windows functions go to API probes
(`emu.API_HANDLERS`; add a handler for a new one; GetDriveTypeA
returns a drive type fixed by the case and the root string). Convention
`fastcall` passes args[0]/args[1] in ECX/EDX. `Spec.virtual_effects`
lets a probed virtual call change memory (e.g. a child that shrinks its
parent's list), `Spec.new_fails` makes operator new return null in some
cases, `Spec.max_instructions` raises the per-call limit (20M) for
inputs our debug build needs more for, `Spec.case_scale` runs N times
`--cases` (UpdateSettlers: 12, one job family per case), and `Spec.ignore_bytes` leaves out output bytes that provably hold
each function's own stack leftovers (padding of a copied stack
temporary; say why at the spec and add a self-test pairing that differs
next to them). GDI text functions (SelectObject, GetTextExtentPoint32A, SetBkMode, SetTextColor, SetBkColor, TextOutA; texts recorded by contents, sizes drawn per call) and Enter/LeaveCriticalSection are API probes as well (session 58; the in-process counterpart is test/fake_gdi.h). InterlockedExchange (stores and returns the old value), SendMessageA and DeleteCriticalSection are API probes as well (wip/logic-r4). InitializeCriticalSection, ShowWindow, ResumeThread, PeekMessageA (its MSG buffer by presence) and CoUninitialize are API probes since wip/menus-r5. timeGetTime and Sleep are probed too (read timeGetTime
through S3.EXE's import 0x655358, which in-process tests can point at a
fake clock). COM methods: a `Spec.virtuals` entry `(count, pointers,
{"com": True, "returns": f})` records the call without ECX (stdcall,
interface as the first argument) and lets `f(h)` pick EAX (e.g. a set of
HRESULTs); a pointer argument's recorded bytes can be `((offset, size),
...)` for structures with uninitialized fields. `Spec.callees` probes
calls of S3.EXE functions that are not reimplemented and can't run in
the emulator (exception constructors, the CRT's free): recorded with
their arguments, returning `this` or a probe value. `specs.LOCKIT_PROBES` (session 55) probes std::_Lockit
(0x61e6e3 / 0x61e77f, the critical section of the MSVC6 tree code):
recorded without the lock object's address, order compared; add it to
the callees of every spec whose code reaches tree functions. `_CxxThrowException`
0x61f027 ends the run like _Xlen/_Xran and is compared with the thrown
object pointer and throw info. In-process, a game C++ exception can be
caught with SEH (`__try`/`__except`, code 0xE06D7363; ExceptionInformation
[1] is the address of the thrown value), see directx_surface.cpp.
`run.py --self-test` checks that the
harness catches deliberately wrong pairings; `--all-uncovered` lists
every uncovered block (to write the dead-block reasons at a spec). Our replacements must not
rely on DLL startup code (the emulator doesn't run it); call the game's
operator new/delete/memmove (0x61efb9/0x61e4ef/0x61e860) where the
original does.

- **Unicorn RAM offsets (bug found 2026-11-25):** Unicorn 2.1.4
  gives each mapped region an offset in QEMU's `ram_addr` space (the
  index of the dirty bitmaps and of the per-page lists of translated
  code). Until a region has been unmapped it puts a new region after
  the *last block of its size-sorted list* (`find_ram_offset_last`),
  so mapping S3.EXE (0x3ed000) before the bigger s3re.dll (0x629000)
  gave the world and the stack the same offsets as s3re.dll. A store
  to world+X then looked like self-modifying code at s3re.dll+X: the
  store was reported twice, and when it hit the block being run,
  Unicorn called no memory hooks any more, so the rest of the run
  wasn't tracked (writes not compared, lazy world not filled). Whether
  it struck depended only on where our code sits in the DLL:
  GenerateRandomMap's Release build failed after the unrelated ai-mil
  merge 799fca5 moved its tile loop to s3re.dll+0x7dda0 (it stores to
  world+0x7dda2), Debug passed. `emu.new_uc` maps and unmaps a page
  first, which switches Unicorn to QEMU's correct gap search; `--self-test`
  checks that no region aliases code (`ram_offset_check`). A symptom
  to recognize: "ours" (or the original) writes far fewer bytes than
  the other, or a mismatch that comes and goes with unrelated code
  changes or between Debug and Release.
- **Parallel runs:** specs run in `--jobs N` worker processes
  (default, 2026-11-23: the free logical cores minus 2, measured over
  0.5 s, so cores busy with other agents' work count as taken; capped
  so that 80 % of the free memory holds 400 MB per worker plus the
  extra of each selected spec with `Spec.memory_mb` (none needs it
  since 2026-11-23: UpdateSoldier's 2.6 GB came from one invalid case
  whose runaway loop wrote 8M world bytes in 20M instructions, each
  tracked in Python dicts; its `max_instructions=1_000_000` bounds it
  to ~250 MB with identical results; look for such a case first); or
  `S3RE_EMU_JOBS`; workers run at
  below-normal priority; `--jobs 1` runs in one process; the run
  prints the count and why). The user's machine became unresponsive
  with 18 workers while other agents were busy: when several agents
  work in parallel, only one of them runs a full suite at a time (per
  batch use `--changed`). Every spec starts from a
  fresh emulator state (`Emulator.reset`: images as loaded, stack
  zeroed, no world bytes, Unicorn's translated-code cache flushed),
  and every call starts with an empty x87 stack and fixed flags, so
  results don't depend on the order or the worker count, and the
  output is printed in SPECS order. Python writes over S3.EXE code
  (a stub in `Spec.globals`) go through `Emulator.write_image`, which
  drops the stale translated blocks: Unicorn doesn't notice them (a
  WalkSettler FindPath stub ran the real FindPath after
  Pathing_FindPath in the same worker, 2026-11-22).
  The slowest specs start first (times in the gitignored
  `build/emutest_times.json`), and specs that took longer than about
  total/jobs/2 run as several parts (case ranges; the inputs of the
  skipped cases are still drawn, so the merged result equals a whole
  run; `--self-test` checks this for two specs, with each part run
  after another spec). Full suite on 18 workers: ~15 min wall time (the serial run
  takes over an hour); a worker peaks at ~380 MB. `--memory` prints
  workers, peak memory and wall time. `--bin-dir DIR` tests the DLL in another
  build directory (e.g. the main checkout's).
- **Selective runs (by compiled code, 2026-11-23):** `run.py
  --changed` runs only the specs without a recorded pass for their
  current fingerprints: the compiled code of the replacement in
  s3re.dll plus everything of ours it reaches (calls, data, pointers
  in data; addresses normalized to symbol names, so moved code
  doesn't count; names without MSVC's per-path anonymous-namespace
  hash, data without trailing alignment padding, a past-the-end
  pointer named after its table, so another worktree's build gives
  the same fingerprints, 2026-11-24), its spec entry (with the helpers it uses;
  comments don't count) and the harness (emu.py, requirements, the
  `re/*_functions.tsv` lists, the case-running functions of run.py).
  So a header change that doesn't change code (a new enum, comments,
  moved lines) selects nothing, and a changed callee selects the
  specs of everything that reaches it. Every run with the standard
  inputs (2000 cases, seed 1; any filter, quick or full) records its
  passes (0 mismatches and full coverage, or all its cases run) in
  `build/emutest_passes.json` of the **main checkout**, shared by
  all worktrees (a fresh worktree reuses them); a harness that hasn't
  passed `--self-test` yet runs the self-test too. `--dry-run`
  prints the selection. Measured: a new enum in enums.h selects 0
  specs (by headers: all); readability commit 571aab8 15 specs (by
  headers 92), fb7e93c 41 (82). The older header-based selection
  stays available: `--since REF` or `--changed --by-headers` (tracking
  logs + `build/emutest_last_full.json`, written by full runs with 0
  mismatches). Details: `tools/emutest/changed.py`.
- **Quick mode** (the default of `--changed`; `--quick` elsewhere):
  a spec stops once every block is covered and at least 25 % of its
  cases ran (the cases that ran are exactly the first ones of a full
  run, so a quick pass is "0 mismatches + full coverage" too); no new
  case starts after `--budget` seconds (default 60): reported as
  "budget", fine with full coverage, otherwise a warning listing the
  specs and no recorded pass. Quick runs don't split specs into parts.
  `Spec.quick_cases` caps very slow specs whose main evidence is
  elsewhere (CGameData_GenerateRandomMap: 1 case of ~30 s in Release;
  CheckGameEnd, FollowRightWall, UpdateSoldier, UpdateSettlers,
  ExecuteCommands: about 35 s each, since full coverage takes them
  50..1350 s, 2026-11-24); a quick run that ran all of a spec's
  quick_cases records the pass and only notes the incomplete coverage
  (no `--require-coverage` failure). `specs.COVERED_ELSEWHERE` lists
  blocks a `--full` run measurably doesn't reach, with their evidence
  (GenerateRandomMap's 77 big-map blocks); they are reported as covered
  elsewhere. `--full` runs every case without budget.
- **Build (2026-11-23):** run.py tests the **Release** build by
  default (`cmake --build --preset release` first; its full suite
  matched Debug's, 0 mismatches, several times faster); `--build Debug`
  for debugging a mismatch. Passes are recorded per compiled code, so
  each build has its own.
- **Policy:** small and medium functions get full emulator evidence;
  large ones (GenerateRandomMap, UpdateSettlers, UpdateSoldier) get
  bounded emulator runs per batch and rely on the in-process tests
  and the whole-game replays. Per batch: `run.py --changed
  --require-coverage` (quick mode; it includes the batch's new
  specs). At the end of each parallel round of agents and before
  milestones: the full suite with `--full`. Run `--self-test` when
  the harness changed (`--changed` does it by itself then).
