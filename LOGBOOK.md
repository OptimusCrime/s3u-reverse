# Logbook

What has been done, what is in progress, and what comes next. Read this at
the start of every session and update it before stopping (see `CLAUDE.md`).
Permanent facts and procedures belong in `CLAUDE.md`; this file tracks
progress.

## Current focus

**Replacing simulation functions, bottom up.**

Workflow per function (see `CLAUDE.md`): reimplement, differential tests
with a mutation test, `s3re_launcher --test`, then the whole-game check
`python tools/check_replay.py --recording
build/x86/bin/Debug/recordings/scripted-seed127.bin --against-original
--ticks 3000` (that recording has 6 scripted player commands; it lives in
the build folder, recreate one with `--synthetic-input` if it's gone).

Done: TileDistance (0x4ff320), GameWorld::NextSearchGeneration (0x4ff3f0),
the statistics functions InitStatisticsKernel (0x4ff4c0), ShiftStatistics
(0x4ff470) and AddStatistic (0x4ff510). Verified: tests plus a 3,000-tick
replay identical to the original.

Next steps:
1. More leaf functions near these: 0x4ff380 (64000 - 8000a - 4000b, meaning
   unknown), 0x4ff3c0 (clears 9 dwords of a 0x2e0-byte record at
   world+0xE147BA), GameWorld::BuildStatisticsChart (0x4ff550),
   FindNearest_a/b (0x4ff0a0/0x4ff170), the footprint check (0x4da440),
   GameWorld::PushUiFeedback (0x4febe0), BuildMinimap (0x4fb650).
   A good way to find more: functions in 0x4e0000..0x520000 with no callees
   (leaves) and few lines.
2. Make replays self-contained: store the seed and map in the recording.
3. Decode the command ids (ExecuteCommands 0x535990).

## Open questions and loose ends

- Where does the game seed (`g_gameSettings+0x88`) originally come from?
  It's copied from the settings struct at `[0x7e2eb0]+0x364` by `0x567910`.
  Not traced further. Recording the seed is enough for replays.
- `CLogicThread::Step` stores the RNG state every 256 ticks into
  `g_syncRandomHistory` (`0x7dfb58`) and sends it via `0x4309c0`. This is
  probably the multiplayer desync check, more evidence for lockstep.
- `CLogicThread::Step` is called about 240 times a second, but the tick
  advances only about 15 times a second; the step decides when to tick.
  Some work runs per step, not per tick (`0x524fa0`, `0x51f110`), so its
  side effects depend on timing. Known volatile world fields: `+0x2d2e4`
  to `+0x2d2eb` and `+0x2d301` (skipped by the trace hash). Check whether
  they can ever influence the simulation.
- Tile byte 15 is a "visited" stamp shared by the simulation's searches
  (GameWorld::NextSearchGeneration) and the UI overlays. Safe as long as
  every search bumps the generation before marking; only spot-checked
  (0x4fd540 compares with 0 but runs only in OnStart).
- Byte 3 of command messages varies between runs for the same command
  (seen 3, 4, 0xa, 0xcc); possibly uninitialized like bytes 9..11.
- Bytes 9..11 of 12-byte command messages are uninitialized stack memory
  in many commands (vary between runs). Replay feeds the recorded bytes, and
  no effect on the simulation has been seen, but confirm per command id
  when decoding them.
- `ProcessEvents` (`0x524fa0`) does extra work when the game's `-record`
  flag (`0x7a800a`) is set (mouse positions scaled by `0x7acf84`, pushed
  somewhere). Not analyzed; we don't use `-record`.
- The `-record` file header value is the last ExecuteStep + 5; `-play`
  sends a type 4 message when reaching it (probably "end of movie").
- Determinism has only been tested on one 2-player map without input.
  Test other maps, computer opponents, and longer runs.

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

### 2026-11-07 (8): first simulation functions replaced

- Reimplemented and verified 5 world functions (src/s3re/game/
  world_search.cpp, statistics.cpp): 10 new differential tests incl.
  mutation tests, all 19 tests pass. Found that the statistics history
  (20 players x 36 categories x 256 samples) sits at world+0x38.
- Added original mode (`s3re_launcher --original`, `S3RE_HOOK_ALWAYS` for
  hooks that are also tooling) and `check_replay.py --against-original`.
  A scripted 3,000-tick game replayed identically with the original code
  and with ours.

### 2026-11-07 (7): scripted player input, UI state in the world

- `tools/synthetic_input.py`: random play through SendInput (verified with
  screenshots: 640x480 full screen; the build menu opens, building-site
  markers are detected in a screenshot and clicked). It produces real
  player commands.
- Replays with player input keep the RNG identical, but the world hashes
  differed. Dump diffs (`--dump-tick` now takes several ticks and dumps
  the whole hashed world; `tools/diff_world_dumps.py`) found only UI state
  inside the world: camera, menu mode/sub-pages, UI refresh flag, cursor
  mode, search generation, overlay marks (tile bytes 15/16), the tile
  redraw bit (byte 13 bit 7), the footprint check's scratch grids, a UI
  feedback list, the statistics chart. All excluded from the hashes with
  code evidence (trace format v4).
- `check_replay.py`: `--synthetic-input SEED`, `--dump-every N`; the
  mutation check now removes a player command when there is one.

### 2026-11-07 (6): commands, lockstep, record/replay

- The game's `-record` writes `Siedler3.evt` (`CEventFile`): 8-byte header
  (magic `0x47832443`, last step + 5), then 20-byte announce packets. Even
  without human input it holds about 50 commands per 1,000 ticks (computer
  player, sync checks). `-play` adds the file's commands to the live ones,
  so they run twice and the replay diverges (from tick 32).
- Single player runs a loopback network thread, `CNetBounceThread`.
  Traced the full command path: logic pushes 0x21 commands and 0x20 step
  messages on one queue; the bounce thread returns them in order; the
  executor `CLogicThread::ExecuteCommands` (`0x535990`) runs them.
- Built our own recorder/replayer on `MessageQueue::Push` (`0x4309c0`):
  `src/s3re/replay/command_log.cpp`, launcher `--record-commands`,
  `--replay-commands`, `--log-commands`; `tools/command_log.py`,
  `tools/check_replay.py`. Result: replay identical over 1,000 ticks
  (50/50 live commands matched); removing one command diverges from tick 43.
- `--exit-after-ticks` now shuts down through WM_CLOSE (needed so `-record`
  files are completed); about 1 s to exit.
- Pass-through hook on `CNetThread::OnNetAnnounce` (multiplayer only; it's
  never called in single player).

### 2026-11-07 (5): unattended runs, determinism confirmed

- The game has built-in command-line options (German help text at
  `0x6960e4`): `-map <file>` starts a map directly, `-record` / `-play`
  record and replay a "movie" (`Siedler3.evt`), `-demo` skips menus (but
  crashes with `-map`), `-debug`, `-netmon`, `-version`, `-help`.
- Added `--exit-after-ticks`, `--timeout` and `--dump-tick` to the
  launcher, and `tools/check_determinism.py` (unattended runs + compare).
- Diffing header dumps found only timing-dependent scratch fields; after
  skipping them, runs are identical (1,000 and 2,000 tick runs).

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
