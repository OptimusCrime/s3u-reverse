# Logbook

What has been done, what is in progress, and what comes next. Read this at
the start of every session and update it before stopping (see `CLAUDE.md`).
Permanent facts and procedures belong in `CLAUDE.md`; this file tracks
progress.

## Current focus

**Check that the simulation is deterministic.**

The game loop and simulation step are mapped (see "Runtime structure" in
`CLAUDE.md`). The RNG keeps its state inside the world object, which points
towards a deterministic lockstep simulation. Next, confirm that by
observation.

Next steps:
1. DONE: the determinism trace. `s3re_launcher --trace-determinism` writes
   `determinism-<date>-<time>.tsv` next to `s3re.dll`, and
   `python tools/compare_traces.py a.tsv b.tsv` compares two of them.
2. DONE: two traced runs of the same map without input differ from tick 1:
   every game starts with a different RNG seed. Found the seeding:
   `GameWorld::SeedRandom` (`0x4feb40`), called twice from
   `CLogicThread::OnStart` (by `GameWorld::Create` and then with the seed
   from `g_gameSettings+0x88`). Reimplemented and verified it; the hook logs
   every seed, and `s3re_launcher --seed N` forces it.
2b. WAITING ON THE USER: two traced runs with the same forced seed
   (`--trace-determinism --seed 1234`), same map, no input. If they match,
   the simulation is deterministic given seed + inputs. If they still
   diverge, the first differing tick/region shows the next nondeterminism
   source (candidate: the CRT `rand()`, seeded by `srand(time)` at
   `0x431852` and used in many places, e.g. around `0x549xxx`).
3. Find how player commands reach the simulation (a command queue that
   lockstep multiplayer would send over the network); that's what a replay
   recorder needs to capture.
4. Then build record/replay: record the commands, replay them in the hooked
   build, and compare per-tick hashes. This is the system-level verification
   described in `CLAUDE.md`.

## Open questions and loose ends

- Where does the game seed (`g_gameSettings+0x88`) originally come from?
  It's copied from the settings struct at `[0x7e2eb0]+0x364` by `0x567910`.
  Not traced further. Recording the seed is enough for replays.
- `CLogicThread::Step` stores the RNG state every 256 ticks into
  `g_syncRandomHistory` (`0x7dfb58`) and sends it via `0x4309c0`. This is
  probably the multiplayer desync check, more evidence for lockstep.
- `CLogicThread::Step` is called about 240 times a second, but the tick
  advances only about 15 times a second; the step decides when to tick.

- `GameWorld::UpdatePhase2` (`0x4e05e0`): purpose unknown.
- `GameWorld::UpdateSettlers` (`0x4e9670`): the name is provisional.
- `0x550f80` is called on 20 objects at `CLogicThread+0x350` each step.
  Per-player objects (AI?) are likely; `CComputerPlayer` exists in the RTTI.
- `0x51f110(0)` is called between the world update phases when
  `g_stepCountdown` is 0. Unknown.
- Where is the `GameWorld` object allocated, and how big is it exactly?
- Constructors aren't named. `re/classes.json` lists, per vtable, the
  functions that store it (`writers`); separate constructors from
  destructors and inlined code.
- 84 virtual functions are shared by unrelated classes (linker folding); only
  `_purecall`, `DebugLog` and the empty `NoOp_retN` ones are named.
- The exact boundary between game code and the C runtime in `.text` is
  unknown (somewhere around `0x61e4ef`).
- `CThreadData`, `CTrace` and `CTextException` are categorized as game
  classes by default; they might be library code.
- REA can't create functions in Ghidra, so the 973 functions in
  `re/extra_functions.tsv` can't be decompiled. Use `dumpbin` (see
  `CLAUDE.md`).
- The user hasn't yet played a game with the hooks active (currently
  `_Eos` and `GameWorld::Random` replaced) as an integration check.

## Session log

### 2026-11-07 (4): first determinism runs, RNG seeding

- First real traces: 384x384 map, about 15 ticks/s, the RNG changes every
  tick. Two runs without input differ from tick 1 (different seed).
- Disassembled all of `S3.EXE` with dumpbin to grep for the RNG state
  offsets (`0x2d2dc..0x2d2e0`). Found `GameWorld::SeedRandom` (`0x4feb40`),
  its two callers in `CLogicThread::OnStart`, the savegame-load copy of the
  RNG state (`0x509xxx`), and the sync check in `CLogicThread::Step`.
- Reimplemented `GameWorld::SeedRandom` (verified, 100,000 cases); added
  `--seed` to the launcher and step-count diagnostics to `s3re.log`.

### 2026-11-07 (3): determinism trace

- Pass-through hook on `CLogicThread::Step` (`src/s3re/game/logic_thread.cpp`)
  feeds `src/s3re/trace/determinism.cpp`. After each step that changes them,
  it logs the tick and RNG state; every 32 ticks it logs hashes of the world
  header (`0..0x67440`), the map tiles (rows of `mapSize` tiles) and the
  rest (up to 8 MB after the tile array, clamped to committed memory).
- Added `tools/compare_traces.py` (checked on synthetic traces).
- Map tile layout from `UpdateEnvironment`: tile (x, y) at world
  `+0x67440 + (x*0x300 + y)*24` (record start 0x67440 or 0x67444, not
  settled).

### 2026-11-07 (2): game loop and RNG

- Traced `WinMain` → `CConsoleThread` (the application object) →
  `Win32::CThread::Execute` → the OnStart/Main/OnExit slots. The main thread
  is a UI/message loop; the simulation runs in `CLogicThread`.
- `DirectX::CGameThread::Main` is the shared game-thread loop; slot 9
  (`Step`) is pure virtual there. `CLogicThread::Step` (`0x53db30`, missed by
  Ghidra, disassembled with dumpbin) is the simulation step. It calls three
  world updates and a per-object update on 20 objects.
- Found the simulation RNG `GameWorld::Random` (`0x4feb70`) and the tick
  counter `g_gameTick` (`0x7dfd48`). Reimplemented the RNG and verified it
  against the original over 1,000,000 calls (with a mutation test).
- Added `re/globals.csv` for global variables, `src/s3re/game/world.h` for
  world offsets, and a "Runtime structure" section in `CLAUDE.md`.
- Inventory: 1,495 rows, 15 understood, 2 verified.

### 2026-11-07 (1): project setup, tooling, class structure

- Set up the repo (gitignore excludes the game install; LF line endings via
  `.gitattributes`), GitHub remote `origin`, branch `main`.
- Confirmed REA works (Ghidra 12.1.4 backend). Ghidra's analysis of `S3.EXE`
  takes about 2–3 minutes per session.
- Analyzed the binaries: `S3.EXE` and `S3_multi.EXE` differ in 7 bytes (GOG
  serial-number patches). MSVC 6.0, C++, fixed base `0x400000`.
- Chose the approach: incremental replacement via an injected DLL, with
  differential testing (see `CLAUDE.md`).
- Built `s3re_launcher.exe` + `s3re.dll` (Detours). Verified in a real run:
  the `WinMain` pass-through hook works and the game exits cleanly.
- Built the function inventory (`re/functions.csv`, `tools/inventory.py`)
  and the differential test harness (`s3re_launcher --test`). First verified
  function: `std::string::_Eos` (`0x401f60`), with 100,000 random cases.
- Recovered the class structure from RTTI (`tools/rtti.py`): 586 classes,
  1,376 virtual functions, 210 deleting destructors, 649 thunks; 973
  functions Ghidra missed. The inventory went from 5 to 1,486 named
  functions out of 9,836 known.
