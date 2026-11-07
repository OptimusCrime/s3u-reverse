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
1. Add a pass-through hook on `CLogicThread::Step` (`0x53db30`). Each step,
   log the tick (`g_gameTick`), the RNG state (world `+0x2d2dc`) and a cheap
   hash of selected world regions (e.g. the map at `+0x62c2c`).
   `CLogicThread::Step` is in `re/extra_functions.tsv`, so check that Detours
   can hook it (it starts with a normal SEH prologue).
2. Ask the user to start the same single-player map twice without touching
   anything, then compare the two logs. If they match, the simulation is
   deterministic given the same inputs. If they diverge immediately, find
   where the seed comes from (time?) and where player commands enter.
3. Find how player commands reach the simulation (a command queue that
   lockstep multiplayer would send over the network); that's what a replay
   recorder needs to capture.
4. Then build record/replay: record the commands, replay them in the hooked
   build, and compare per-tick hashes. This is the system-level verification
   described in `CLAUDE.md`.

## Open questions and loose ends

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
