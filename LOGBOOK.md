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

**Next (updated in session 55). The user's priority (2026-11-24): function
batches (reimplement + verify), less cleanup; cleanup only where it blocks
a batch.**
1. **Computer players, the rest** (`docs/todo/computer-players.md`): done
   up to RunMilitary, the SiteTree containers, the work-area finders,
   CEvaLand construction and AI load/save (session 55). Left: AddArea
   0x555260, FillAreaSiteLists 0x556630 and RefillAreaSiteList 0x5579d0
   (~1.4k ins each), ProcessPendingEvents 0x554980, the expand-empire and
   map-worker agents' MakeBuildOrders (0x56cd90, 0x57ce90) and Run, then
   0x550900 / 0x550a30 and CComputerPlayer::Step 0x550f80. Three helpers
   without function starts (0x553e70, 0x5591e0, 0x559240) need manual
   rows. SendPrioritizeSite 0x58e510 needs a runtime look (log message
   byte 6 against the site owner in a game with computer players).
2. **Sound and the rest of the data loaders**: CSoundData (ctor 0x5fdb60,
   Open 0x5fe760, DetachFromMedium 0x5ff230, GetEffect; SND format not
   analysed yet). The graphics loader and drawing are verified.
3. **Settings / savegame leftovers** (`docs/todo/file-formats.md`):
   GameSettings::LoadSaveGame 0x432b00 (Watcom vector members first), the
   GamePlayerSetup copy constructor 0x49eaf0 / operator= 0x49edf0 (16-bit
   string assign 0x428400 first).
4. **Other READY functions** (`frontier.py --library-done`): the UI range
   0x400000..0x4c0000 has ~265 READY (mostly Watcom containers and
   controls).
5. **Tooling guard (session 55):** spec parts share one namespace and
   names resolve at call time, so a redefinition in a later part silently
   changes earlier specs (GFX_HEADERS, fixed). Add a check that fails on
   top-level names defined twice with different values.
6. Open from session 49: the live-vs-replay header/rest hash difference
   of the user's full game (backlog "Evidence and testing").
7. Later, only when it blocks a batch: readability leftovers
   (`docs/todo/readability.md`; the session 54 typed-access audit,
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

### 2026-11-25 (55, round 2): emulator fix, AI land side, settings, graphics loader

Round 2 of session 55: three batch agents (wip/ai-step, wip/gfx-open,
wip/settings) and two debugging agents (wip/emufix, wip/soldierfix);
verified 1031 -> 1102. All agents hit the 5-hour usage limit once and
were resumed.
- **Emulator harness bug (wip/emufix):** Unicorn 2.1.4 placed regions in
  its RAM address space after the last block of a size-sorted list until
  something had been unmapped, so the world shared RAM offsets with
  s3re.dll code; a store to world+X then counted as self-modifying code
  at s3re.dll+X and Unicorn stopped calling the memory hooks for the rest
  of the run (writes untracked). This caused the Release-only
  GenerateRandomMap mismatch (the ai-mil merge only moved code). Fix:
  emu.new_uc() maps and unmaps a scratch page first; `--self-test` now
  checks that no store aliases code. With the fix, all specs rerun: 0
  mismatches (docs/tools/emulator.md "Unicorn RAM offsets").
- **UpdateSoldier Release-only failure (wip/soldierfix):** a test issue:
  map effect records are built in an uninitialized stack temporary, so
  pad2a/pad3b/pad3e hold stack leftovers in both versions; Debug's happened
  to match. Shared WorldPair helper (ClearEffectPadding /
  CheckpointIgnoringEffectPadding) replaces five local copies
  (docs/reimplementation_notes.md "Padding of map effect records").
- **Computer players (wip/ai-step)**, 46 functions: AI::SiteTree (MSVC6
  _Tree, 16 functions, ai_site_tree.cpp; std::_Lockit calls compared as
  probes, specs.LOCKIT_PROBES), the six work-area finders
  (ai_work_area_finders.cpp; the finders' freed-node padding byte +0xd
  ignored with a self-test pairing), area records and CEvaLand
  construction (ai_area_records.cpp), AI LoadState / LoadGlobals /
  SaveGlobals, CreateAgent. Findings: woodcutter/stonecutter finders take
  the cell with the fewest trees/stones once stock > 10; farm-type finders
  key a unique map by the score's low byte (first cell wins); area
  records have 39 site trees.
- **Graphics loader (wip/gfx-open)**, 19 functions: Win32 CFile and
  CFileMapping (framework/win32_mapping.cpp), DetachMemory, CGraphicsData
  ctor/dtor, FindDirectoryContainingFile, DetachFromMedium and Open
  (graphics/gfx_loader.cpp; Open run on the real GFX folder in both pixel
  formats and all group sets). Shared MSVC6 catch matcher
  msvc6/catch_match.h (backlog item done); test/fake_win32.h; emu.py file
  and memory API handlers and a "throw" callee kind. Quirks kept: CFile
  Seek/GetSize throw on success in one case (inverted VERIFY);
  DetachMemory never advances past an uncommitted region. Known
  difference: Open's local strings leak when a call throws (missing file).
- **Settings side (wip/settings)**, 18 functions: GameSettings::PlayerSetup
  and SaveSetup (ui/game_settings_players.cpp), SaveGameToSlot
  (single-player "slot%2i.sav", multiplayer Multi\temp.mps; the game GUID
  at GameSettings +0xb0 is formatted and discarded), TileAtScreenPoint
  (was FindNearest_b), DrawValueBar (was Logic_sub_4d79c0), DrawData,
  GamePlayerSetup ctor and getters. Renames: GamePlayerSetup::~GamePlayerSetup
  (was NetHelp::Destroy_49ec80), GameSettings::GameGuid (was CopyRectB0).
- Spec namespace clash found at merge: graphics.py redefined GFX_HEADERS
  (used by economy/logic/settlers helpers at call time); renamed to
  GFX_FAKE_HEADERS.
- Evidence on the merged main: Debug and Release build; inventory check
  clean; `--self-test` 26/26 + RAM check "separate"; `--test` 831/831 (Debug); full emulator suite
  `--full --require-coverage`: 1102 functions, 0 mismatches, 0 with
  incomplete coverage; whole-game checks against the original: check_save
  slots 1-3, check_random_map 384,0 and 448,3, the scripted-seed127 replay:
  all identical (slot 2 first failed with CAppNotActiveException when the
  game window lost focus; rerun identical). Identical in a whole-game check:
  726 -> 777.

### 2026-11-25 (55): graphics drawing, savegame writer, computer players economy and military

Parallel round of four batch agents (wip/gfx, wip/savewriter, wip/ai-econ,
wip/ai-mil), merged serially; verified 871 -> 1031.
- **Graphics drawing (`wip/gfx`, the user's choice)**, 104 functions in
  graphics/gfx_spans.cpp, gfx_images.cpp, gfx_rle.cpp, gfx_draw.cpp,
  gfx_files.cpp: the 32 shade spans, 8 masked spans, shadow spans, the
  16-bit / colour-indexed / shadow RLE blitters and their clip variants,
  the Draw methods, the table accessors, CGraphicsData::Close; 14 missing
  function starts added (re/extra_functions.tsv). Evidence: every image of
  the RGB565 set (52,892 sprites, 47,121 colour layers, 49,223 shadows,
  1,617 GUI images; GFX read only) drawn by both, whole surfaces with
  guard bytes compared. Findings (re/graphics_formats.md "Drawing,
  verified"): RLE skip = bits 0..6, bit 7 ends the row; shade level i =
  (i+1)/32 (level 0 black); construction draws the bottom
  min(h, ((h+8)*f)>>16) rows plus up to 8 dissolving rows; shadow method
  1 is a checkerboard phased by origin mod 32. Known difference: the
  SequenceFrame ctor has no catch(...) (the original only logs, a no-op,
  and rethrows). Not done: CGraphicsData::Open (needs the Win32
  file-mapping classes).
- **Savegame writer (`wip/savewriter`)**: CLogicThread::SaveGame 0x509980
  (logic/save_game.cpp), GameSettings::MapName 0x436a60, ~CMapFile
  0x4a5b50. Tests redirect g_gameFolder (0x7a7c80) to %TEMP%; files from
  original and ours byte-identical (3 savegames, 14 maps, 12 random worlds)
  outside bytes neither writes (heap/stack leftovers after names, SaveSetup
  record byte 0xb; documented), round trip through LoadSaveGame. Findings
  (re/file_formats.md "Verified savegame writer"): chunk 0x19's size counts
  its header (16 of 20 colours saved: why the loader overreads 8 bytes);
  AI chunks 0x15-0x17 written once per computer player; names copied
  without length limit. Still the original: GameSettings::SaveSetup
  0x433490, AI::SaveGlobals 0x551220.
- **Computer players, economy (`wip/ai-econ`)**, 38 functions:
  ConsiderResidences 0x568890 (x87 as double; g_aiWantedCarriers,
  g_aiResidenceSlopes), forced production / wood emergency / tool supply
  (ai_force_production.cpp), the request Execute chain down to
  PlaceBuilding (ai_request_execute.cpp; before the basic economy only the
  first woodcutter, stonecutter, sawmill, forester and 4 small towers),
  agent slots and request agents, work-area searches. Quirks kept:
  ForceProduction logs "AUS" when switching on; HasSameTypeWorkAreaNear's
  radius is a leftover register for other types. emu.py: new_fails_size
  may be a tuple. Skipped: SiteTree containers and farm/fishery finders
  (std::_Lockit, uninitialized padding in tree nodes).
- **Computer players, military (`wip/ai-mil`)**, 24 functions up to
  AI::RunMilitary 0x58f990 and T_ATTACK_AGENT::Run 0x55ec10 (ai_military.cpp,
  ai_army_attack.cpp, ai_safety_agent.cpp, ai_census_settlers.cpp,
  ai_world_events.cpp). AiGroupTarget = two patrol buildings + a prey
  settler; RunMilitary's step counters named; g_aiGarrisonedBuildings is a
  bitset<4000>. Bugs kept: SendHunter tests the last member's type 11
  times; FindSpareGroup passes x twice; ProcessWorldEvents skips the entry
  after an erased catapult. SendPrioritizeSite still needs a runtime look.
- **Round end:** a full `--test` found the minimap tests crashing after
  the LoadMap tests (both original and ours read colour entry 0xffff left
  in g_playerColourIndex); the minimap tests now set and restore the
  colour indices themselves. Evidence on the merged main: Debug and
  Release build; inventory check clean; `--test` 787/787; emulator
  `--self-test` 21/21; full emulator suite `--full --require-coverage`:
  1031 functions, 1 mismatch: CGameData_GenerateRandomMap fails in the
  Release DLL only (Debug: 10/10 cases, full coverage; in-process Release
  passes). Bisected to the wip/ai-mil merge (only AI code); a debugging
  agent is on it (wip/emufix). Whole-game checks against the original:
  check_save slots 1-3 (2,000 ticks), check_random_map 384,0 and 448,3
  (1,000 ticks), the scripted-seed127 replay (3,000 ticks, it has computer
  players: the new AI code runs in it): all identical; call counts
  recorded. Identical in a whole-game check: 582 -> 726.
- Also: the hung cl.exe from session 54 ended (the user's OK); old
  worktrees removed.

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
