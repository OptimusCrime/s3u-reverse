# Logbook

What has been done, what is in progress, and what comes next. Read this at
the start of every session and update it before stopping (see `CLAUDE.md`).
Permanent facts and procedures belong in `CLAUDE.md`; this file tracks
progress.

## Current focus

**Replacing simulation functions, bottom up, gated by the emulator tests.**

Per function: reimplement; in-process differential test (`s3re_launcher
--test`); emulator spec in `tools/emutest/specs.py` with
`python tools/emutest/run.py --require-coverage` (0 mismatches, all basic
blocks of the original covered). The whole-game replay
(`check_replay.py --against-original`) is a milestone check now.

Status: all 59 replacements pass the emulator suite with full block
coverage (2,000 cases each, fewer for the slow ones: ShiftStatistics 60,
the map-wide loops 200-400); `--self-test` catches 9/9 wrong pairings.
Specs can now also set S3.EXE globals per case (`Spec.globals`).

Next steps:
1. Continue with leaf functions (list below), each with an emulator spec.
2. Shadow mode (compare against the original during normal play), a faster
   game clock and savegames, for real and late-game data.
3. Optional: symbolic equivalence proofs (angr/Z3) for loop-free functions.

Done (verified: differential tests incl. mutation tests + whole-game
replay identical to the original):
- search/statistics: TileDistance, GameWorld::NextSearchGeneration,
  InitStatisticsKernel, ShiftStatistics, AddStatistic
- directions: NeighbourX/Y, Previous/NextDirection, DirectionFromDelta
- players: GameWorld::AddPlayerCounter, SubtractPlayerCounter,
  ClearPlayerListLengths
- settlers: GameWorld::CanSettlerEnterTile, SettlerGroup
- map: GameWorld::IsOnMap, IsInsideMapBorder, TileScreenX, TileScreenY

Batch 8 (2026-11-07, 20 functions; in-process + emulator tests, full
coverage; whole-game check passed, 7 of the 20 called):
- UI state (ui_state.cpp): IsUiSelectionMode, IsUiField298,
  IsUiSelectionParam1/2, IsUiSelectionMode4Field298Is2, IsGameOutcome1,
  ScreenCenterTileX/Y, PushUiFeedback
- free ids (id_allocators.cpp): FindFreeId256/4096/8192
- map areas (map_areas.cpp): AssignTileToArea, RemoveTileFromArea,
  SetTileAreaMarked, SetAreaBuildingSlotA/B
- map marks (map_marks.cpp): Set/ClearDiagonalTileByteB,
  ClearTileByte11Slice

How to find the next ones: leaf functions (no calls) in
0x4d0000..0x560000, listed by a small script over the dumpbin disassembly
(function starts from re/ghidra_functions.tsv + re/extra_functions.tsv;
count instructions, skip any with `call` or `fs:`). About 110 remain.
Skipped so far because their meaning is unclear (do them once understood):
0x4fa6b0/0x4faa40 (type ids 1/55), 0x4ffb90 (7-byte list at
world+0x1195dcd), 0x505bc0/0x505bf0 (byte counters in 14-byte records at
world+0x123885f), 0x510440/0x50e380/0x510470 (settler record fields),
0x504650, 0x4ff380, 0x4ff3c0, 0x523540 (reads an uninitialized stack
byte), 0x5437b0 and 0x51e650 (objects other than the world, class
unknown), 0x53f530 (CLogicThread, +0x84 unknown).

Next steps:
1. Continue with leaf functions; then move up to their callers (e.g. the
   area flood fill 0x51a580 uses FindFreeId256; 0x4e6150/0x4e62c0/
   0x4e6430/0x4e6cd0 use AssignTileToArea/RemoveTileFromArea).
2. Settle the tile record layout. Evidence so far for records starting at
   0x67444 (not 0x67440 as in world.h): the tile area word is indexed as
   (x*0x300+y+0x44d9)*24 = 0x67458 in 130 places, i.e. field +0x14 of a
   record at 0x67444; 0x67444 is the most used absolute offset (215
   uses); the y-1 neighbour accesses (0x6742c..0x67436) are fields
   +0x0..+0xa of the record before. Changing world.h affects
   determinism.cpp's per-tile hashing; do it as its own step.
3. (done 2026-11-08) Replays are self-contained: seed and map in the
   recording.

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
- **Replays need the game seed they were recorded with**, which recordings
  now store (fixed 2026-11-08, see session log). `recordings/scripted-seed127.bin` was
  recorded with game seed 1234 (check_replay's default; "127" is the
  synthetic-input seed). Replaying it with `--seed 127` gives a different
  game (player races 1/2 instead of 2/3: OnStart draws a random race with
  GameWorld::Random() % 4) into which the recorded commands are fed, and
  the original game crashes after ~800-2,300 ticks: a recorded command
  builds building type 0x24 (an Asian-only building) for the Egyptian
  player, and drawing it looks up a shadow sprite that doesn't exist
  (0x4a9960 -> 0x5fabb0 "GFX: ShadowAnimation not found 0:0", whose bounds
  check only calls DebugLog, then a garbage pointer is dereferenced in
  0x5fcdc0 at 0x5fcdfb). Fixed: recordings carry seed and map now.

## Session log

### 2026-11-08 (13): self-contained recordings

- Recording format version 2 (`src/common/recording_format.h`, shared by
  launcher and DLL): header with game seed, settings (bit 0: random races
  include the Amazons) and the -map argument. The DLL writes it when
  recording (seed from the SeedRandom hook, `game::LastSeed`) and, when
  replaying, stops the game if seed, setting or map differ. The launcher
  takes seed and map from the recording; a conflicting --seed/-map or a
  version 1 recording without --seed is refused (checked by hand: all
  three refused).
- `tools/command_log.py`: reads both versions, `upgrade` converts version
  1. `recordings/scripted-seed127.bin` upgraded (seed 1234; the version 1
  original is kept as `scripted-seed127.v1.bin`).
- `check_replay.py` passes --seed/--map only when recording (or when given
  explicitly).
- Checked with the game: a fresh 300-tick record/replay round trip
  (header written and applied, mutation detected) REPLAY OK; the upgraded
  scripted recording, replayed without --seed, original vs our code:
  traces identical over 1,000 ticks (compared by hand, see next point).
- Loose end: when the game's clean shutdown hangs ("did not finish within
  15 s"), the logic thread keeps stepping and the trace reopens as a second
  file; check_replay.py then reports "run failed (2 new trace files)"
  although the run was fine. Make run_game take the first new trace.

### 2026-11-07 (12): batch 8 (20 leaf functions)

- Replaced 20 functions (UI state queries, screen-centre tile, free-id
  search, tile areas, diagonal/slice tile marks; list under "Current
  focus"). New files: game/ui_state.cpp, id_allocators.cpp,
  map_areas.cpp, map_marks.cpp, globals.h. In-process tests 92/92 pass
  (27 new incl. 4 mutation tests); emulator suite 59/59 with full block
  coverage; self-test 9/9.
- emutest: `Spec.globals` sets S3.EXE globals per case (for
  0x730f18/0x730f1c/0x730f64; added to re/globals.csv).
- Tile layout: evidence that tile records start at 0x67444 (see next
  steps); world.h not changed yet.
- Whole-game check: first attempts crashed in the reference run (original
  code). Cause: wrong seed for the recording (see open questions), not
  our code. Found with two new diagnostics: a crash logger
  (src/s3re/crash_log.cpp: vectored exception handler, logs registers,
  memory and callers of access violations in game code) and a DebugLog
  passthrough (src/s3re/debug_log.cpp: the game's 688 debug messages,
  silent in the release build, now go to s3re.log, 5 per call site). The
  seed hook also logs the "random race includes Amazons" setting.
- With the right seed (1234): `check_replay.py --recording
  recordings/scripted-seed127.bin --against-original --ticks 3000` REPLAY
  OK with all 59 replacements; 30 of 59 called. Of batch 8: AssignTileToArea
  (8,179 calls), RemoveTileFromArea (2,092), SetTileAreaMarked (81,023),
  FindFreeId256/4096/8192 (4/7/83), ClearTileByte11Slice (448); the other
  13 (UI queries, building slots, diagonals) were not called.

### 2026-11-07 (11): emulator test harness

- tools/emutest: Unicorn-based differential harness (emu.py: PE loading,
  lazy random world, write undo, block coverage; run.py: comparison and
  report; specs.py: one spec per replacement). Linker map s3re.map added
  to the build for symbol lookup. Packages pinned in tools/requirements.txt.
- All 39 replacements match with full block coverage; the first run found
  uncovered branches in 5 functions (fixed with targeted presets).

### 2026-11-07 (10): batches 5-7, call coverage, testing strategy

- Batches 5-7 (tile neighbourhood checks, counting functions, building
  request flags): all differential tests pass (68/68).
- Added call counting to replacement hooks. The coverage run showed that
  16 of 39 replacements were never called in the scripted 3,000-tick game
  (e.g. NextSearchGeneration, all Count* functions, the SetBuildingFlag*
  functions, TileScreenX/Y), so the earlier "whole-game check passed"
  notes were wrong for them; the inventory notes now give call counts.
- Decided with the user: the whole-game replay is too slow and too
  coverage-limited as a per-batch gate. Next: an emulator-based
  differential harness (Unicorn) that runs the original bytes from S3.EXE
  and ours from s3re.dll, measures branch coverage of the original and
  checks calling-convention details; later symbolic equivalence proofs
  (angr/Z3) for loop-free functions, a shadow mode for real game data, a
  faster game clock and savegames for late-game states.

### 2026-11-07 (9): more leaf functions

- 14 more functions replaced in three batches (directions, player counters,
  settler helpers, map coordinates); each batch passed all tests and the
  whole-game check against the original. Added test/world_buffer.h.
- Some callers only overwrite AL after a call, so for those functions the
  exact EAX of the original is reproduced and tested.
- Don't rebuild while a game run is going: the link fails because the DLL
  is in use (and a rebuild between the two runs of --against-original
  would mix builds).

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
