# Logbook

What is in progress and what comes next. Read this at the start of every
session; update it before stopping (see `CLAUDE.md`, "Session routine").

**Keep this file short (under ~400 lines).** It holds only the current
focus, the open questions and the session entries of the last three
sessions. At the end of a session, move older session entries to
`docs/history/logbook-YYYY-MM.md` (newest first, by the month they were
written), and remove "Current focus" text that is done or out of date
(history goes to the archive, facts to `CLAUDE.md`/`docs/`/`re/`, chores
to `docs/todo/`). The archive is for searching, not for reading whole.

## Current focus

**Next (updated in session 54). The user's priority (2026-11-24): function
batches (reimplement + verify), less cleanup; cleanup only where it blocks
a batch.**
1. **Graphics (the user's choice for the next round, 2026-11-24):** the
   loader of the memory-mapped `GFX\siedler3_NN.<pixel format>.dat` files
   (CGraphicsData, 8 tables, RLE sprites, palettes by brightness) and the
   sprite decoding, verified by comparing decoded bytes
   (`re/graphics_formats.md`, `docs/todo/file-formats.md` "Graphics").
   Session 54 already did the CDrawer draw queues (graphics/drawer_queues.cpp).
2. **Computer players, continued** (`docs/todo/computer-players.md`):
   small READY helpers 0x58ae90, 0x599760, 0x59a260, 0x59a7a0, 0x573670,
   FillAreaSiteLists 0x556630; residences 0x568890 (x87, like
   ConsiderCutters); SendHunter 0x5935b0, StartAttack 0x594080;
   SendPrioritizeSite 0x58e510 needs a runtime look; then A5 Cevaland, A6
   the agents' run functions, 0x550a30 / 0x550f80. Start from the names
   and `re/computer_players_naming.md`.
3. **File formats** (`docs/todo/file-formats.md`): LoadMap is verified
   (session 54). Next: the savegame writer `CLogicThread::SaveGame`
   0x509980 (1,365 lines; needs a way to redirect its `Save\` writes to
   temp files; calls the AI save 0x551220). GameSettings::PlayerSetup
   0x4351e0 is still called as the original.
4. **Other READY functions** (`frontier.py --library-done`): DrawData
   0x523480 (SEH, string-allocator byte), CGameData::FindNearest_b
   0x4ff170 (screen-to-tile conversion?), Logic_sub_4d79c0.
5. Open from session 49: the live-vs-replay header/rest hash difference
   of the user's full game (backlog "Evidence and testing").
6. Later, only when it blocks a batch: readability leftovers
   (`docs/todo/readability.md`; the session 54 typed-access audit found
   ~0.9% raw access, gaps in typed_access_lint.py and opaque classes:
   postponed by the user) and the source layout follow-ups.

**Standing decisions** (details in `CLAUDE.md` and the archive):
core gameplay first, no network/lobby code (2026-11-09); reimplementation
coverage may run ahead of verification (`S3RE_HOOK_UNVERIFIED`,
2026-11-09); new code follows `docs/coding_style.md`; x87 floating point is written
as `double` in the original's order with `_ftol` truncation (decided
2026-11-24, CLAUDE.md "Floating point").

**Simulation state (2026-11-22):** CLogicThread::Step's three world
updates (`UpdateEnvironment` 0x4e7810, `UpdateEconomy` 0x4e05e0,
`UpdateSettlers` 0x4e9670) and all their callees are ours and verified;
so are ExecuteCommands 0x535990, CheckGameEnd 0x511c50 and all 32 spells.
Left in Step: `CComputerPlayer::Step` 0x550f80 (the AI) and per-step
UI/draw work. Jobs and spells with tests only: backlog "Evidence and
testing".

## Open questions and loose ends

- Original source files: refine the code ranges (they run from the first
  to the last voting function; unnamed files sit in the gaps and maybe
  inside ranges, e.g. `WinMain` lands in LobbyManager.cpp). Does the
  alphabetical link order of the named files predict the names of unnamed
  ones? (`re/source_files.md`; corrections in the backlog.)
- The 128-slot position list at world+0x1195dd4 is changed per logic step
  (g_recentPositionsCursor, 0x7ace60), not per tick. Does it affect the
  simulation, and is it in the determinism hash? (found in batch 9)
- Where does the game seed (`g_gameSettings+0x88`) originally come from?
  It's copied from the settings struct at `[0x7e2eb0]+0x364` by `0x567910`.
  Not traced further. Recording the seed is enough for replays.
- `CLogicThread::Step` stores the RNG state every 256 ticks into
  `g_syncRandomHistory` (`0x7dfb58`) and sends it via `0x4309c0`. This is
  probably the multiplayer desync check, more evidence for lockstep.
- Tick pacing is understood (2026-11-10, see CLAUDE.md "Tick pacing and
  `--fast`"): a 64 ms CTimer, Step ticks only when it fired. Per-step work
  (`0x524fa0`, `0x51f110` PrepareDrawData) still runs a timing-dependent
  number of times; volatile/UI world fields known so far: `+0x2d2c8`,
  `+0x2d2e4..+0x2d2eb`, `+0x2d301` (skipped by the trace hash). Normal
  speed and `--fast` gave identical traces on the recording and one save;
  other saves/longer games may still find more such fields. `+0x2d2d8`
  (incremented by Step before every ExecuteCommands attempt, also when the
  network allowance stops it) was identical in those runs but is a
  candidate.
- Tile byte 15 is a "visited" stamp shared by the simulation's searches
  (CGameData::NextSearchGeneration) and the UI overlays. Safe as long as
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

- `0x51f110(0)` (PrepareDrawData) is called between the world update
  phases when `g_stepCountdown` is 0 (session 50: fills the draw data).
- Where is the `CGameData` object allocated, and how big is it exactly?
- 84 virtual functions are shared by unrelated classes (linker folding); only
  `_purecall`, `DebugLog` and the empty `NoOp_retN` ones are named.
- The exact boundary between game code and the C runtime in `.text` is
  unknown (somewhere around `0x61e4ef`).
- `CThreadData`, `CTrace` and `CTextException` are categorized as game
  classes by default; they might be library code.

## Session log (recent sessions; older ones in docs/history/)

### 2026-11-24/25 (54): LoadMap, computer players land and army, logic/draw helpers

Parallel round of four batch agents (worktrees wip/loadmap2, wip/ai-land,
wip/ai-army, wip/logic-misc), merged serially; 75 functions verified.
- **`CGameData::LoadMap` 0x4fd540 (`wip/loadmap2`)**, map/load_map.cpp,
  split into helpers per chunk, the colours, UnblockCastleTiles and the
  campaign fix-ups (missions 49, 50, 53, 59, 61, 70). Evidence: all 166
  maps under Map\ (read only) loaded by both into ConstructWorld worlds
  (whole world, writable S3.EXE bytes, DebugLog, CRT rand, CMapFile
  caches), 30 synthetic variants, a wrong-implementation test, emulator
  spec 170/170 blocks. GameSettings::PlayerSetup 0x4351e0 stays the
  original (called through a typed pointer). Findings in
  re/file_formats.md "Verified map loader" (chunk 2's 45-byte slot
  records, untaken slots, garrison record byte 6, mission fix-ups; 100 of
  166 maps have only chunk 6; one tile uses slot 191, past the 20-entry
  table). SaveGame 0x509980 not started (see Current focus).
- **Computer players, land (`wip/ai-land`)**, 19 functions:
  CEvaLand::EvaluateCell 0x5540f0, EvaluateAllCells, UpdateNextCells,
  ReevaluateAroundTile, DiscountExploitedResources, HasForeignCellAround,
  FewUnownedTilesNear, MarkBuildingTypesInLandGrid (ai_evaland_cells.cpp);
  CountResourceInOwnersLand / CountPlayersResourcesAround;
  HOUSE_GROUND_REQUEST_NOTE CheckSite / CheckAndImproveSite /
  NextSpiralPosition (ai_ground_request.cpp); four order helpers
  (ai_building_orders.cpp); InitAiGlobals, SaveState (ai_state.cpp).
  AiGridCell fully named (re/computer_players.md "The land evaluation
  grid"). Quirks kept: amounts > 250 stored as 250, others halved; a
  shift count wraps to 31; fish not counted (DebugLog per tile).
- **Computer players, army (`wip/ai-army`)**, 21 functions: the army
  group bookkeeping (ai_army_groups.cpp, 9), the military-building
  searches, CheckGroupTargets (x87 compare as double), Mission56FinalAssault,
  RandomPointNearSettler(Far), SendThieves, SendGeologists
  (ai_army_orders.cpp), DemolishSurplusResidence, PromoteSoldiers
  (ai_economy_actions.cpp), ConsiderWoodcutter / ConsiderStonecutter (x87
  as double). emu.py records OutputDebugStringA and sprintf'd DebugLog
  texts; `--self-test` 21/21. Findings: mission 56's assault targets
  player 0's building; AssignSoldier's 0x592aeb is dead; AiGroupTarget
  x/y are probably building ids. Residences 0x568890 not started.
- **Logic and draw helpers (`wip/logic-misc`)**, 34 functions: CLogicThread
  members (logic_thread_members.cpp; the message ring and draw fields
  named in logic_thread_layout.h), the CDrawer draw queues (were
  Logic_sub_4d7xxx: shadows, sprites, colour layers per row;
  graphics/drawer_queues.cpp, drawer_layout.h), screen layout and ambient
  sound pan/volume (graphics/screen_layout.cpp), CGameData
  FinishInitialising (was Logic_sub_4fce90), PickHitArea (was
  FindNearest_a), BuildStatisticsChart, GoToNotification, BuildMinimap,
  RebuildFogOfWar, InitTileShadesAndMapEdge, the MapEffect queries.
- **Typed-access audit** (read-only agent, user's question): ~0.9% of
  access sites raw (~100 lines in code, the rest in tests); CGameData
  fully mapped; opaque: logic_thread_layout.h (~90% unknown bytes),
  menus/game-settings layouts; typed_access_lint.py misses byte casts of
  non-world pointers, small-object offsets, inline hex arithmetic, C
  casts. Postponed by the user.
- Evidence on the merged main: Debug and Release build; inventory check
  clean; full emulator suite `--full --require-coverage` on main after
  the first two merges (816 functions, 0 mismatches, full coverage), then
  `--changed --require-coverage` after all four (151 specs, 0
  mismatches; LoadSaveGame run `--full` alone: 40/40 blocks);
  `--test` 727/727 (Debug); whole-game checks against the original: check_save slots
  1-3 (2,000 ticks) and the scripted-seed127 replay (3,000 ticks)
  identical; check_random_map 384,0 and 448,3 (1,000 ticks) identical
  (call counts recorded; the replay's were not). Identical in a
  whole-game check: 539 -> 582. Verified: 796 -> 871.
- Lessons: `check_random_map.py` reads `s3re.log` in the bin folder, so a
  concurrent `--test` from the same build overwrites it ("random map was
  not set up"); run it after the tests. The army agent left a hung
  cl.exe (PID 10656) locking files in its worktree (its kill was denied;
  left for the user).

### 2026-11-24 (53): computer players battle spells and queries, map loader callees

- **Computer players (`wip/ai-spells`)**, 22 functions verified (in-process
  tests with wrong-implementation checks, emulator specs with all blocks
  covered; the branch's full `--test` 636/636). Battle spells
  (ai_battle_spells.cpp): the ten casters 0x5957c0..0x596210,
  AI::CastBattleSpells 0x5953b0 (the crossed Amazon casters kept),
  AI::InitBattleSpellSlots 0x590870. Enemy counts 0x58a2c0 / 0x58a610
  (ai_enemy_search.cpp); cell resources OwnersLandHasResource 0x599df0,
  WorkCellHasTrees/Stones 0x59a380 / 0x59a8b0, FindResourceCellAround
  0x59a120 (ai_cell_resources.cpp); FindNode 0x573be0, IsForceProduction
  0x572fa0 (ai_player_nodes.cpp); HasSiteNoteNear 0x571c40;
  HOUSE_GROUND_REQUEST_NOTE::Init 0x575d60. Findings in
  re/computer_players.md "Battle spells": per mage a mana check by its own
  race, then the race's casters in fixed order, each with one Random draw
  (low word > 25000, ~24%); enemies within 20 tiles; the Romans cast
  nothing in campaign mission 67; several old caster notes corrected.
  Known difference: a mage of race > 3 makes the original read leftover
  stack bytes as its mana bits (at most a side-effect-free enemy count,
  never a cast); ours skips it.
- **Map loader callees (`wip/loadmap`)**, 7 functions verified (in-process
  tests incl. whole-world comparisons on worlds built by the original
  CGameData constructor + GenerateRandomMap, emulator specs full
  coverage; the branch's full `--test` 632/632): PrepareLoadedTerrain
  0x519540 (water depth bands from the shore; map/load_map.cpp),
  SpawnSettlersOnWater 0x50dc10, PlaceTown 0x506cf0 and AddPlayerTowns
  0x5074b0 (map/player_towns.cpp), GameSettings::StartGoods 0x412300 and
  GamePlayerSetup::Colour 0x49fec0 (ui/game_settings.cpp). 0x4351e0 is
  GameSettings::PlayerSetup(index) (identified; Watcom vector access, UI
  side). Findings: the start building is a small tower (type 15) with a
  Swordsman1 garrison (job 23); start goods from GameSettings +0x74;
  AddPlayerTowns mirrors teams 1..3 by the symmetry (world +0x2d274);
  quirk kept: symmetry 3 with given start positions places teams 2/3
  whenever team 1 exists (PlaceTown(-1), never reached by the game). Test
  support for LoadMap: map/map_test_support.h.
- The residences condition 0x568890 also uses x87 float curves, like the
  woodcutter and stonecutter conditions. **Decision (the user,
  2026-11-24): x87 code is written as `double` in the original's order,
  with `_ftol` truncation and emulator specs near rounding edges**
  (CLAUDE.md "Floating point", docs/reimplementation_notes.md); the
  runtime check of the x87 control word is in the backlog. The three
  conditions are unblocked.
- Evidence on the merged main (both batches): Debug and Release build;
  `--test` 648/648; full emulator suite `--full --require-coverage`: 796
  functions, 0 mismatches, 0 with incomplete coverage (16 workers,
  1,299 s); inventory check clean. Verified: 768 -> 796.
- **Whole-game checks against the original** (run over the user's Remote
  Desktop session: works): `check_save.py` slots 1, 2, 3 (2,000 ticks
  each), `check_random_map.py` 384,0 and 448,3 (1,000 ticks), and
  `check_replay.py --recording scripted-seed127` (3,000 ticks): all
  identical; call counts recorded (re/whole_game_calls.csv). Identical in
  a whole-game check: 470 -> 539 functions; every verified function now
  has a whole-game row (257 with tests only).

### 2026-11-24 (52): savegame loader, computer players A2/A4, progress page

- **Computer players (`wip/ai-a4`)**, 28 functions verified (in-process
  tests with wrong-implementation checks; emulator specs run `--full`, 0
  mismatches, all blocks covered). A2: AI::CountSitesOfType 0x59db30
  (quirk kept: a non-zero area counts all the player's sites),
  FirstBuildingInArea 0x58b410, FirstMilitaryBuildingInArea 0x58b380,
  MineSuccessPercent 0x573810, ResourceAmountOfKindAt 0x571f60,
  ResourceAmountNear 0x571fa0, IsFarmLand 0x59a720, NextSparseMapOffset
  0x58a250, Army::GroupFightingShareAbove 0x593040 (x87 share in double,
  original order), Army::PruneHunters 0x5934a0 (skip-after-erase bug
  kept). A4 (ai_commands.cpp): 18 command senders 0x58e220..0x58ebc0 and
  0x590990..0x590c80, pushed message bytes compared; 0x4318a0 named
  GameSettings::IsHost (medium). Known difference: the economy senders
  leave header byte 3 and some payload bytes as stack leftovers (ours 0),
  which no handler reads. Not replaced: SendPrioritizeSite 0x58e510 (its
  handler reads the unwritten byte 6: behaviour depends on stack
  contents; docs/todo/computer-players.md).
- **Savegame loader (`wip/savegame`)**, 7 functions verified:
  `CLogicThread::LoadSaveGame` 0x508fa0 (logic/load_save_game.cpp, chunk
  structs in fileformats/savegame_chunks.h) and the rest of the CMapFile
  container: constructor 0x4a5a70, Open 0x4a5c80, Close 0x4a5f40,
  WriteChunk 0x4a6540, WriteChunkWithTail 0x4a6600, WriteChunkData
  0x4a66d0 (fileformats/map_file_io.cpp). Evidence: all 3 savegames (read
  only) and 26 synthetic ones loaded by the original and ours into two
  worlds: whole world, S3.EXE globals, DebugLog calls and chunk cache
  identical; all 181 game files opened through our Open; 120 random
  savegames written to temp files byte-identical; emulator specs full
  coverage. Findings (re/file_formats.md): chunk 0 is never released;
  chunk 2/1 restores only 750 of 1000 events; the game's savegames hold
  16 colours in chunk 0x19 but 20 are copied (8 heap bytes past the
  chunk; the savegame test skips colours 16..19); Close writes the
  checksum only in mode 2; known difference: a failed read in Open uses
  uninitialized stack in the original (only the error code can differ).
  New globals g_playerInfo 0x7acfa8, g_lastDrawnTick 0x7dfd4c,
  g_victoryProtectActive 0x7deb54. debug_log.cpp ignores calls from six
  Watcom destructors folded onto DebugLog's address (original-mode
  tooling version 7).
- Evidence on the merged main (both batches): Debug and Release build;
  `--test` 620/620; full emulator suite `--full --require-coverage`: 768
  functions, 0 mismatches, 0 with incomplete coverage (16 workers,
  1,152 s); inventory check clean. Verified: 733 -> 768. Not yet run
  (needs the game window): check_save.py against the original.
- **Progress page** (frontend-design subagent, on the user's request: the
  session 50 redesign was harder to read): a headline share, two stacked
  bars (game code, all code) whose segments show their own shares (the
  old strips showed running totals), a table with plain-word groups; the
  seven detailed statuses became the treemap legend; the README picture
  follows. Kept: treemap and hover card.
