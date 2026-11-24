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

**Next (updated in session 51). The user's priority (2026-11-24): function
batches (reimplement + verify), less cleanup; cleanup only where it blocks
a batch.**
1. **Computer players, continued** (`docs/todo/computer-players.md`):
   READY helpers (`frontier.py --range 54cbd0 59fcd0 --library-done`, 74
   READY after session 53: EvaluateCell 0x5540f0, RandomPointNearSettler
   0x594f70 / 0x5952d0, Army 0x592100 / 0x592240, military-building
   searches 0x58aa70 / 0x58a7e0, CountResourceInOwnersLand 0x599a30,
   CheckSite 0x5760d0, ...); residences 0x568890, woodcutter 0x567e00 and
   stonecutter 0x568420 (x87, now unblocked by the floating-point rule);
   SendPrioritizeSite
   0x58e510 needs a runtime look; then A5 Cevaland, A6 the agents' run
   functions, 0x550a30 / 0x550f80.
   Start from the names and `re/computer_players_naming.md`.
2. **File formats** (`docs/todo/file-formats.md`): the CMapFile container,
   LoadSaveGame and LoadMap's callees are verified (sessions 51-53). Next:
   `CGameData::LoadMap` 0x4fd540 (needs only GameSettings::PlayerSetup
   0x4351e0: reimplement it or keep calling the original), with an
   all-maps whole-world test (map/map_test_support.h); then the savegame
   writer `CLogicThread::SaveGame` 0x509980. **Needs the screen once:**
   `check_save.py "slot N.sav" --ticks 2000 --against-original` (slots
   1..3), `check_random_map.py --maps "384,0;448,3" --ticks 1000
   --against-original`, `check_replay.py --recording scripted-seed127
   --against-original --ticks 3000`: whole-game evidence for the loaders.
3. Open from session 49: the live-vs-replay header/rest hash difference
   of the user's full game (backlog "Evidence and testing").
4. Later, only when it blocks a batch: readability leftovers
   (`docs/todo/readability.md`) and the optional follow-ups of the source
   layout plan (`docs/source_layout_plan.md` section 5, step 6).

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
  1,299 s); inventory check clean. Verified: 768 -> 797 (796 specs).

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

### 2026-11-24 (51): computer players A2 leftovers, CMapFile container, specs split

- **specs.py split** (main): `tools/emutest/specs/` with `common.py`, one
  part per source folder, `self_test.py`, `covered_elsewhere.py`;
  `specs/__init__.py` executes them in `PARTS` order into one namespace.
  Two helpers defined twice with different bodies were renamed
  (player_colour_globals, decay_traffic_tile). Evidence: every spec,
  self-test pairing and COVERED_ELSEWHERE identical before/after (by
  compiled code); all fingerprints identical (`changed.py` reads the parts
  concatenated, `specs_source`), so the recorded passes stayed valid;
  `--self-test` 21/21. Both batches' spec blocks later moved into their
  parts without changing fingerprints. **Bug of the split, found by the
  round-end full suite:** four helpers that read S3.EXE (buildings,
  common, economy, settlers parts; only some cases call them) located it
  three folders up from `__file__`, which is now `specs/__init__.py`, one
  folder deeper; fixed with `REPO_ROOT`/`S3_EXE_PATH` in common.py. The
  pushed commits 59d2b56..2495c95 have it. Lesson: a snapshot of the spec
  objects doesn't run lazily executed helper code; only the full suite
  does.
- **Computer players A2 (`wip/ai-a2`)**, 16 functions verified:
  AI::Army::Reset 0x591e00, AI::CountNodeBuildingsByType 0x572420, the
  nine AI::WrappedPos members 0x597070..0x597300 (wrapping grid cursor of
  the site searches), the coal mine / iron mine / iron smelter conditions
  0x568bc0 / 0x568d80 / 0x568f40, the economy snapshot 0x567910 and
  AI::AreaPopulation 0x59dcc0 (new ai_army.cpp, ai_wrapped_pos.cpp,
  ai_economy_snapshot.cpp). Findings in re/computer_players.md: mine and
  smelter conditions take the higher of a per-population and a
  stock-per-demand priority; the gold mine tables serve all mines; one
  smelter per two smiths; the ~93 "READY" SEH functions of the AI range
  are already classified string initializers (use `--library-done`).
- **CMapFile container (`wip/mapfile`)**, 14 functions verified in the new
  folder `src/s3re/game/fileformats/`: CMapFileStream constructor, Open,
  Close, Read, Write, Seek, GetSize, HasError (0x4a55e0..0x4a5a60) and
  CMapFile GetChunk (rolling XOR), ReleaseChunk, AddChunk, SetMemoryOnly,
  GetError, GetChecksum. The original's catch (CHResult*/CFileNotFound*) is
  an SEH filter matching MSVC 6.0 catchable types. `MapFile_AllGameFiles`:
  all 181 files under Map\ and Save\ (read only), 4,651 chunks / 395 MB
  identical between original, ours and our own decoding. emu.py: callee
  effects may return EAX, and a 5th element replaces recorded arguments.
  Format findings in re/file_formats.md ("Verified container").
- Merges: specs blocks moved into specs/ai.py and the new
  specs/fileformats.py; re/globals.csv kept both sides.
- Evidence on the merged main (both batches + the fix): Debug and Release
  build; `--test` 602/602; full emulator suite `--full --require-coverage`:
  733 functions, 0 mismatches, 0 with incomplete coverage (17 workers,
  925 s); inventory check and style lint clean. Verified: 703 -> 733.

### 2026-11-24 (51): bookkeeping restructure, readability round (a, b, c)

Bookkeeping, on the user's request (the files read every session kept
growing): LOGBOOK.md 3,526 -> ~500 lines (current focus, open questions,
last three sessions; older entries and outdated plans in
`docs/history/logbook-2026-11.md`); TODO.md is a one-screen index over
`docs/todo/<area>.md`; CLAUDE.md 863 -> 400 lines (55 -> 24 KB; it is
loaded into every session and subagent): rules, decisions and a "Where
things are" table stay, the details moved verbatim to `docs/tools/`
(emulator, runtime_checks, game_lock, analysis),
`docs/reimplementation_notes.md`, `re/commands_lockstep.md` and
docs/coding_style.md (checked line by line: nothing lost). Left: split
specs.py by subsystem.

Three parallel worktree batches, merged a, b, c:
- (a) cleanups: `tools/style_lint.py` ("narrowing" enforced in
  inventory.py check; "hex-compare"/"magic-category" report-only);
  FakeClock is a live patch (restored after a crashed test); `--changed`
  fingerprints are equal in every worktree (MSVC's per-path
  anonymous-namespace hash, data padding, past-the-end table pointers
  normalized: 206 of 703 differed, now 0); the 5 slow specs get
  `quick_cases` (full coverage needs e.g. 22,784 cases for
  UpdateSettlers, 24,240 for ExecuteCommands: `--full` only);
  GenerateRandomMap's 77 large-map blocks are `specs.COVERED_ELSEWHERE`.
- (b) the naming pass's corrections of verified code (UI OnLButtonDown,
  GetScreen, IsOffScreen, OnParentChanged, OnShow, the Dialog globals,
  std::wstring::_Eos, OnMainMenuResult; CDirectDrawDevice constructor,
  CWin32WaitableBag::WaitFor (original-mode tooling version 6),
  CMessageQueue::window; CComputerPlayer::DoomNote*, AI::CEvaLand::*,
  CellCentreOwner/TileOwner, g_aiLandEvaluation, AI global sizes and the
  census g_aiCensus), and readability of map_effect_spawns and the
  AI/UI/framework/graphics files (msvc6/game_heap.h, named vtable slots
  and message types).
- (c) 315 SettlerJob names from their handlers (375 of 405 named;
  scheme and evidence in re/enums.md; corrected: 314 ShipyardGoIn, 336
  HealerGoIn, 76..79 pile-to-pile carrier moves, 307/308 donkeys, 142
  UnitFighting); shared constants in game/enums/terrain.h, objects.h,
  resources.h (14 files' local copies gone; trees 0x44..0x53, crops,
  felled/young trees, signs, fire, hives; desert/swamp/snow classes);
  `Settler::job` is a `SettlerJob` (JobOf/SetJob gone).
- Merge: (b) conflicted in CLAUDE.md (kept the slim one, its line went
  to docs/tools/runtime_checks.md) and screen_element.cpp (includes);
  after (c), the narrowing lint flagged 7 test-input casts into
  SettlerJob: made the 16-bit truncation explicit.
- Evidence on the merged main: Debug and Release build; `--test`
  587/587; full emulator suite `--full --require-coverage`: 703
  functions, 0 mismatches, 0 with incomplete coverage (17 workers, 954 s);
  inventory check and style lint clean.
- Also answered: the serial-number and CD checks are located (no code
  change; Menus::priv::CMainMenu::CheckSerialNumber 0x476920, validator
  0x411d10, the GOG patches sit in the registry reader 0x40fb40;
  PromptForCD 0x497040 and ProtectCD_RunEmbeddedCheck 0x5b2f70); network
  code stays out of scope; library code needs no reimplementation (CRT
  behaviour list added to docs/todo/known-differences.md).

### 2026-11-24 (50): naming pass over the AI range and the file formats

The user asked to identify and name more functions in the areas we work
on next. Five parallel read-only reviews (Ghidra through REA plus
build/s3.asm), one per range: AI 0x54cbd0..0x565000, 0x565000..0x58b520,
0x58b520..0x596650, 0x596650..0x5a0000, and the file formats
(Dirk/SaveGames.cpp 0x4a55e0..0x4a77c0, the loaders in 0x4fd000..0x50a000,
Win32\Mapping.cpp, 0x60b000..0x60d000). Each wrote proposals (address,
old name, new name, confidence, evidence); the new `tools/apply_names.py`
merged them into re/functions.csv: 425 names (high -> identified, medium
stays classified, note "Named (session 50, conf): evidence. Was OLD.").
Then a mechanical rule over the whole binary found 192 more static
std::string initializers (registry path "Software\BlueByte\Siedler3\1.0"
+ "\General" / "\Index Server" / "\Patches", one set per source file
that includes the header): followed by StaticDtorReg_X, writes X, calls
0x401c70 -> `StaticInit_X`, category compiler. 15 MSVC6 container rows
moved to category stl. Understood: 58.8 % -> 64.0 % (game classified
3,047 -> 2,536). inventory.py check passes. No code changed, nothing
verified.

Findings (details in the new `re/file_formats.md` and
`re/computer_players_naming.md`):
- Maps and savegames share the Dirk::CMapFile container: a checksum over
  bytes 8..end, the file type, then chunks `[sub<<16|type][size][data]`,
  data XORed with a rolling key; no compression. A parser written from it
  recomputed the stored checksums of 384-2-Lakes.map and slot 1.sav.
- 0x4fd540 is `CGameData::LoadMap`, 0x508fa0 `CLogicThread::LoadSaveGame`,
  0x509980 `CLogicThread::SaveGame`; SaveGame's labels give the original
  names of world regions ("timer", "warmap", "activesettlers",
  "diggertab", ...; TODO.md "File formats").
- AI: CEvaLand grid cells and area records decoded, the AI event ring,
  the census layout, the request chain down to the PlaceBuilding sender,
  the military AI (`AI::RunMilitary` 0x58f990, command senders named by
  command id), AI save/load (`AI::SaveState`/`LoadState`, chunks
  0x15..0x17). Several existing names and layouts are wrong (verified
  `CancelAtHandler`, the AI globals' sizes; TODO.md), and three original
  bugs a reimplementation must keep.
- The LOGBOOK roadmap's "Win32 file wrappers" and "decompressor" guesses
  were wrong (corrected in the roadmap item).

Second round, on the user's request: the graphics loaders, three more
reviews (0x5a0000..0x5a8f20, 0x5a8f20..0x5b50ad, CGraphicsData
0x5f0000..0x600000): 313 names, and a new inventory category
`thirdparty` (inventory.py, frontier.py, source_files.py, progress) for
156 rows of PaintLib, libpng 1.0.2 and zlib 1.1.3. Notes in
`re/graphics_formats.md` and `re/picture_code.md`. Findings:
- The "Picture" range is not the game's graphics: it is the credits text
  scroller (DATA.DAT is the credits as HTML), then PaintLib + libpng +
  zlib, used only for the online lobby's Banner\Banner.png. The 26
  "Zlib_sub" rows are junk targets inside 0x5b2f70, which writes the
  ProtectCD.dll embedded in S3.EXE (0x735b58) to a temp file and calls it.
- The game's graphics are `GFX\siedler3_NN.<RGB masks>.dat`, memory
  mapped: 0x54-byte header (8 table offsets), RLE sprites, 8 brightness
  palettes; confirmed on all 45 files.
- Four verified names are wrong (png_free, CGraphicsData::TextAddress,
  GuiImagePointer, SequenceFrameCount): renamed in the next batch with the
  AI ones.

Renames of verified (and one trivial) functions found wrong by the naming
pass, each confirmed from the disassembly first: `Picture::FreeIfBoth` ->
`png_free` (category thirdparty), `CGraphicsData::VariantFrameAddress` ->
`TextAddress`, `FramePointer` -> `GuiImagePointer`, `FrameCount` ->
`SequenceFrameCount` (with the gfx_layout.h fields: TextTable
textsPerLanguage/languages, textTables, language, languageCount,
guiImageTables, SequenceTable/sequenceTables),
`BLACK_BOARD_NOTE::CancelAtHandler` -> `ExecuteAtHandler`
(`agent_slot::kCancelNote` -> `kExecute`), `AI::ListFirst` ->
`AI::SiteTree::begin`, `AI::List::begin_580220` / `empty_580230` ->
`std::multimap::end_580220` / `empty_580230` (category stl, like their
neighbours 0x580240..0x580270), `BLACK_BOARD_AGENT::vf05` -> `IsBusy`
(slot 5 stays `agent_slot::kBusy`). Hooks, trampolines, specs, docs and
re/whole_game_calls.csv follow; the seed-name tables (difftest.cpp
SeedName, run.py RENAMED_FUNCTIONS) map the new names to the old ones, so
inputs are unchanged. Evidence: Debug and Release build; all 587
in-process tests pass; `run.py --changed --require-coverage` (every spec,
because run.py changed): 703 specs, 0 mismatches, self-test 21 of 21;
8 large specs stopped at the 60 s budget before full coverage (code
unchanged; their passes need a `--full` run) and GenerateRandomMap is
capped at 1 case as usual.

Third round, on the user's request: the DirectX framework (Gamebase,
Dxbase, dinput; Ddraw and Dsound; network files left out) and the Win32
framework (Synchronize, Classes; cdaudio, Registry, Internet, Mapping,
Detach): four reviews, 269 names. Notes in `re/directx_framework.md` and
`re/win32_framework.md`. Bruce Dawson's ExceptionHandler.cpp
(0x61b990..0x61c4b8, writes errorlog.txt) is category thirdparty.
`apply_names.py` now appends to "Trivial, proven by its bytes" notes
(trivial.py check needs that note first). Findings:
- Game-thread slot 10 is OnActivate(bool) (WM_ACTIVATEAPP -> SetActive ->
  request bit 2), not "OnEvent" (CLAUDE.md corrected; also
  CComputerPlayer::Step there).
- Display: fullscreen uses a flip chain, windowed a primary plus an
  offscreen back buffer copied by a routine chosen by a startup benchmark
  of five copy methods (with CPU detection). Lost surfaces restore through
  CDirectDraw slot 3. Sounds: a master buffer plus duplicates kept in a
  Watcom dictionary, purged every 2.5 s.
- Input is the mouse only, buffered DirectInput.
- Registry: ~30 values under HKLM\Software\BlueByte\Siedler3\1.0\General
  (re/win32_framework.md has the table); CD music through MCI with a
  callback window; threads post unhandled exceptions to their parent;
  framework timeouts become infinite with `-debug`.
- Source-file ranges around DirectX\ are shifted by about one file, and
  more missing/false function starts (TODO.md).

Fourth round: the user interface, five reviews (dialog.cpp in three
parts with Draw.cpp/DrawInitSurfaces.cpp, MenuShared.cpp and netdialogs.cpp
in two; lobby/network rows left out): 472 names, notes in
`re/ui_classes.md`. Findings:
- Messages: slot 31 PostMessage goes up to CScreen's queue (+0x80), which
  Menus::CDrawThread::Update drains back down to slot 22; message types
  0x10/0x12 mouse button, 1 key, 3 character, 0x30 timer, 0x40 command.
- Menus::CDrawThread: SetActualScreen 0x494550 builds each menu state's
  screen; 0x494260 dispatches dialog results to one handler per state
  (slots 14..29); ExitMenus posts 0x801 (1 start game, 2 quit);
  modal dialogs run a nested CGameThread::Main loop.
- CListView is a table of typed cell controls; CDropdown, CDialog keyboard
  handling (Tab, Enter, Escape), the controls' fields; the draw thread
  double-buffers the draw data (FetchDrawData 0x42c780); CDrawer slots.
- Some verified UI names are misleading (CControl::OnMessage10 ->
  OnLButtonDown, CScreenElement::ParentQuery78 -> GetScreen,
  CScreenElement::Enable is the parent-changed handler,
  UI_WideEos_428880 -> std::wstring::_Eos; TODO.md), and ~16 more missing
  function starts (dialog handlers Ghidra merged into their neighbours).

Fifth round: the logic thread 0x509980..0x54cbd0 (61 names, 17 existing
names confirmed and set to identified; notes in `re/logic_thread.md`):
a tutorial ("boot camp") state machine (world +0x2d304 wait countdown,
+0x2d308 step; UpdateTutorial 0x53fbb0 when g_campaignMission == 99);
the draw-data handoff (PrepareDrawData fills a 0xc7720-byte DrawData,
PublishDrawData 0x51f0e0 swaps it to the draw thread; the verified
LogicRecord24 is the map-effect draw record); the on-screen message ring
and chat input at CLogicThread +0x414/+0x3f8; lobby/ladder status
functions; FinishGame 0x53e720 and the music start/stop/next track.
Progress page: "understood" (identified or better) added to the stats,
treemap labels, tooltips and a "Colour by" selector.
Then, on the user's request, a redesign of docs/progress.html (frontend-design
plugin): a "frontier" strip per scope (game code, all code) with markers at
the identical / verified / understood boundaries instead of nine stat boxes,
a one-sentence summary, the status table beside it, serif headings on a
cool green-grey palette, an outline on the hovered treemap box, and a new
hover card (class and method, a one-line description from the inventory
note, status chip, whole-game calls, address and size; groups: function
count, a status bar and the shares). progress.py now writes that
description (`describe`, notes without bookkeeping prefixes, first
sentence, at most 150 characters; 2,365 functions; data file 1.0 MB).


### 2026-11-24 (49): the user's full game
- The user played a whole game: `python tools/play_game.py --map
  M704-2-Longrun --race roman` (seed 10779, high goods, original code,
  normal speed, no F12), 00:55..03:23, 138,402 ticks, 23,735 commands,
  ended through the game's menu (FinishGame). Committed as
  `recordings/fullgame-20261124-0055-seed10779.s3rec` (README row). The
  aborted first attempt (seed 43191) was deleted.
- `check_replay.py --recording fullgame-20261124-0055-seed10779
  --against-original --ticks 138402`: **replay vs original: identical**
  (original 276 s, ours 343 s with `--fast`); 376 of 702 replacements
  called. The original's trace is cached.
- `record_call_counts.py`: 702 rows as source
  `replay:fullgame-20261124-0055-seed10779`; 53 verified functions are
  seen in a whole game for the first time (spells: ExecuteSpell,
  HasManaForSpell, PayForSpell, SpawnSpellEffects, SpellRevealSpot,
  SpellTransformLand; destroying buildings: DestroyBuildingAt,
  CrushBuilding, RemoveBuildingFromTiles, RecallBuildingCarriers...;
  docks: PlaceDockLine, OpenDockLineTile; piles: StealGoodFromPile,
  ChangePileGood...). Verified functions: 470 seen, 233 tests only.
- Live trace vs the original's replay (`compare_traces.py`): RNG and map
  hash identical on every tick, header hash differs from tick 192, rest
  hash from 4,992: probably UI-written fields not yet skipped by the
  trace. Needs world dumps of a live game to find (TODO.md).
