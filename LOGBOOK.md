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

**In progress (session 51, 2026-11-24):** three parallel worktree batches
of the readability round: (a) `wip/cleanups-a` lints, FakeClock,
`--changed` reselection, GenerateRandomMap coverage; (b)
`wip/readability-b` the naming-pass renames of verified code (UI,
framework, AI incl. DoomNote) and readability of the AI/UI/framework/
graphics files and map_effect_spawns.cpp; (c) `wip/enums-c` SettlerJob
names into job_ids.h, shared terrain/object/resource constants into
`game/enums/`. Merge order a, b, c. Then the bookkeeping restructure the
user asked for: this file shortened (done), TODO.md split into
`docs/todo/`, CLAUDE.md slimmed to rules and pointers (details into
`docs/tools/`), `tools/emutest/specs.py` split by subsystem. Then the
full emulator suite with `--full`.

**Next, after this round (the user's order of session 48):**
1. Readability leftovers of this round (whatever the batches report).
   Optional follow-ups of the source layout plan (renames that read
   better, splitting the catch-all files `framework_small.cpp`/
   `misc_helpers.cpp`/`logic_helpers.cpp`/`ai_*helpers.cpp`, a CMake
   library for the simulation): `docs/source_layout_plan.md` section 5,
   step 6.
2. **Computer players, continued** (backlog "Computer players"): A2
   leftovers (0x591e00, 0x572420, the larger economy conditions, the
   snapshot 0x567910), A4 command senders, A5 Cevaland, A6 the agents'
   run functions, then 0x550a30 / 0x550f80. The whole AI range is named
   (session 50): start from the names and `re/computer_players_naming.md`.
3. **File formats** (savegame loader 0x508fa0 first, then maps,
   graphics). Container format, chunk tables and load call trees are in
   `re/file_formats.md`; start with the CMapFile container.
4. Open from session 49: the live-vs-replay header/rest hash difference
   of the user's full game (backlog "Evidence and testing").

**Standing decisions** (details in `CLAUDE.md` and the archive):
core gameplay first, no network/lobby code (2026-11-09); reimplementation
coverage may run ahead of verification (`S3RE_HOOK_UNVERIFIED`,
2026-11-09); new code follows `docs/coding_style.md`; the x87 question
(proposal in the archive, "x87 floating point") is still undecided:
decide it before reimplementing code with real floating point.

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

### 2026-11-23 (48): source tree move, source_file column, enums split

The user answered the six questions of `docs/source_layout_plan.md` "as
recommended" (subsystem folders, the proposed names, trace/replay under
tooling, `game/enums/job_ids.h`, the `source_file` column, now). Three
commits, each verified and pushed:

1. **The move** (2d0d075): `tools/move_sources.py` (the record; mapping
   from the plan's appendix plus the four A2/A3 AI files, which go to
   `game/ai/`) moved 122 files with `git mv` into `game/{world, map,
   mapgen, environment, economy, buildings, settlers, settlers/jobs,
   pathing, spells, logic, logic/commands, ai, ui, graphics, net,
   framework}/` and `tooling/{trace,replay}/` (savegame_start.cpp to
   `tooling/`), rewrote the includes, the grouped CMake source list and
   the `src/s3re/...` / `game/...` paths in CLAUDE.md, re/, tools/ and
   src comments (LOGBOOK and the plan's appendix untouched; a bare
   `trace/` or `replay/` without `src/s3re/` is not rewritten;
   `re/update_settlers_jobs.csv` is Latin-1 and has no paths, so it is
   skipped). A second run finds nothing to do. Evidence: Debug and
   Release build, inventory check 0 errors, in-process tests 587/587,
   emulator: `--changed` selected 142 of 703 specs (not "most": the
   anonymous-namespace symbols changed less than the plan feared), 6 of
   them hit the quick budget, so the full suite ran: 703 specs, 0
   mismatches, only GenerateRandomMap 338/415 blocks (known). frontier,
   whole_game summary, progress, asm_tools run. CLAUDE.md "Code layout"
   describes the tree and its rules.
2. **`source_file` column** (e3681df): `tools/source_files.py apply`
   fills it from `re/source_files.csv` (2,769 rows in a named file, 1,032
   `unnamed: A.cpp..B.cpp`, 6,037 library/CRT empty); `inventory.py`
   documents it and `check` fails when it is out of date (tested with a
   changed value). Rerun `apply` after `scan`, `rtti.py apply` or
   `merge_survey.py` (new rows start empty).
3. **enums split** (edd82c9): `game/enums/{value, races, settler_types, buildings,
   goods, piles, spells, job_ids, commands, terrain, directions}.h`;
   `game/enums.h` is the umbrella (nothing includes it any more). The 45
   includers now include only the category headers they use (chosen by
   a script from the identifiers in their code; files that got the enums
   through `execute_commands.h` or `settler_jobs.h` include them
   directly). Every category header includes `value.h`. Evidence: Debug
   and Release build (0 warnings), `run.py --changed --dry-run` selects
   0 specs, in-process tests 587/587, inventory check 0 errors.
   re/enums.md, CLAUDE.md, coding_style.md, TODO.md updated; the TODO
   item is removed.

Next: the readability pass (Current focus 1).

### 2026-11-23 (48): round end (computer players A2 + A3, backlog cleanups, layout plan)

- Four parallel agents (entries below): computer players A2 (46
  functions) and A3 (30), backlog cleanups (4 items), the source layout
  plan (`docs/source_layout_plan.md`). The computer sleeping mid-round
  didn't break anything; all agents resumed.
- Merged main: Debug and Release build; in-process suite 587/587;
  `run.py --changed --require-coverage` 12 specs (the agents' passes were
  already recorded in main's passes file), 0 mismatches, full coverage.
- Whole game, all identical to the original: scripted-seed127 (3,000
  ticks), fun8-seed2024-input31 (5,000), random768-seed4321-input7
  (1,500), call counts recorded; slots 1-3 (2,000 ticks); random maps
  384,0 and 448,3 (2,000 ticks). Of the 76 new functions 61 are seen; the
  other 15 run directly inside our own AI replacements (destructor chain,
  note tree) and are uncounted. Totals: 703 verified, 417 seen in a
  whole game, 286 tests only.
- No full emulator suite this round (only the `--changed` runs); the
  next round end should run `--full` (TODO: 5 slow specs without a
  recorded pass).

### 2026-11-23 (48): computer players A2 (parallel worktree)

- **46 functions verified** in three batches (branch wip/ai-a2):
  - The 25 T_ECONOMY_AGENT conditions 0x568250..0x569d20
    (`T_ECONOMY_AGENT::Consider<Type>`, ai_economy_conditions.cpp): one per
    building type, asking for it with AI::RecordBestCandidate. They encode
    the economy's build order (a sawmill per three woodcutters, farms per
    three mines, fisheries per two mines, mills / pig farms by the grain
    left over, toolsmith once coal and iron are there, barracks per three
    weaponsmiths, the race's drink building and temples by population).
    Gold mines and weaponsmiths follow a "wanted for the population"
    curve (stages 0/40/100/500/2000, x87 + _ftol).
  - 14 map queries (ai_map_queries.cpp): BuildingAt, AreaAt,
    BuildingRecordArea, WalkRegionAlongRow, StoneAmountAt, IsTreeAt,
    ResourceAmount/KindAt, TerritoryRadius (distance of the 3500th /
    4000th / 5000th map offset per tower type), SiteKindOf (2 storehouse,
    3 military, 4 mine, 5 field), TakeBestCandidate, CountSitesInArea,
    CountAreaSettlersOfType, TeamHasActivePlayer.
  - 7 site queries (ai_site_queries.cpp): HasSiteNoteInArea,
    HasBuildingNear, CountBuildingsNear, NextGarrisonedMilitaryBuilding,
    CheckBasicEconomy, ResetUnitGroup, ArmyState_Reset.
  - Evidence: in-process tests AiEconomyConditions_*, AiMapQueries_*,
    AiSiteQueries_* (with wrong-implementation probes) and emulator specs
    with 0 mismatches and full block coverage for all 46.
- **Layouts** (ai_layout.h, re/structs.md): EconomyAgent (0xa0 bytes: race,
  area, population stage, goods in stock, the node's buildings per type),
  AiPlayerData (+0xc basic_economy, +0x344 building_counts, +0x3b6
  type_blocked, +0x550, best[57 + type] = taken candidates), AiArmyState
  (0x7e58c8) and AiGroupOrders (0x7e37d0) with per player and unit group
  arrays (meanings G). Globals/tables: g_aiPopulationStages,
  g_aiWantedGoldMines / Weaponsmiths and their slopes.
- Lesson: never switch on `static_cast<BuildingType>(uint16)`; it
  truncates to the uint8 enum (caught by the SiteKindOf test). Switch on
  the word with `case Value(...)` (lint idea in TODO.md).
- Next (TODO.md "Computer players"): 0x591e00, 0x572420, the larger
  economy conditions 0x567e00 / 0x568420 / 0x568bc0 / 0x568d80 /
  0x568f40, the snapshot 0x567910, more small READY ones; then A4/A5/A6.

### 2026-11-23 (48): computer players A3 (parallel worktree)

- **30 functions verified** (package A3, two commits on wip/ai-a3).
  Batch 1, `ai_computer_player.cpp`: CComputerPlayer's note tree and
  agents: AddNote 0x551270 / AddNoteUnder 0x551230, DeleteNote 0x551330
  (recursive, subtree first), AddAgent 0x551400 (agent slot 2 lists the
  note types it handles; first come first served in handlers[]),
  DeleteAgent 0x551490, DispatchNote 0x551510 / DispatchPendingNotes
  0x5514c0, RunAgents 0x551590, WorkOnNote 0x5516e0 /
  WorkOnNextActiveNote 0x551670, DeleteDoomedNotes 0x551790,
  UpdateBlackboard 0x5517d0, Shutdown 0x550f30, ~CComputerPlayer 0x5509d0
  and its deleting destructor 0x5509b0, StartBestBuilding 0x551a70 (fewer
  than 4 construction sites in the area: hand on the best build
  candidate; wood/stone shortage modes), the note callbacks
  ReportToRequester 0x582480 / CancelAtHandler 0x582510. Batch 2,
  `ai_request_notes.cpp`: the request agents' constructors (kinds 0, 1,
  2, 4), the request notes' inits 0x57ab20 / 0x578760 / 0x565470,
  T_DO_BUILD_HOUSE_NOTE's constructor 0x5801c0, BLACK_BOARD_NOTE's
  constructor / destructor / deleting destructors (0x5801b0, 0x565590,
  0x5655a0, 0x56f6b0).
- **Evidence:** in-process `AiNoteTree_*` (consistent random note
  forests, fake vtables recording every virtual call, the original side
  run entirely as original code through ScopedJumps to the trampolines;
  StartBestBuilding's callees 0x59dbb0 / 0x58ecb0 as recording stubs)
  and `AiRequestNotes_*`, both with wrong-implementation probes; seeds 0-3
  pass. Emulator: 30 new specs (virtual calls via `TREE_VIRTUALS`, agent
  slot 2 fills the caller's stack buffer as an effect; StartBestBuilding
  runs 0x59dbb0 / 0x58ecb0 as original code on preset buildings), 0
  mismatches, all blocks covered; `--changed` runs clean.
- **Findings / names:** list +0x200 is the deletion queue (`doomed_notes`;
  RemoveFromList200 -> RemoveDoomedNote), +0x1fe the active-note cursor;
  note +0x16 requester / +0x18 handler (flag 2 "taken"); the agent slots
  (`agent_slot`: 0 delete, 2 note types, 3 take, 4 work, 5 busy, 7/8 run,
  9 report, 10 cancel); factory kinds 1/2/4 = HOUSE_GROUND_REQUEST /
  HOUSE_REQUEST / T_DO_BUILD_HOUSE (`ai::AgentKind`, now shared), note
  type = handling agent kind (`ai::NoteType`); AiPlayerData nodes are
  per economy area (ComputerPlayer +0x77c `area`), +0x10/+0x14 shortage
  flags (G), best[57 + type] the candidate handed on; `RequestNote`
  fields. 0x56cd90 / 0x57ce90 renamed *::MakeBuildOrders (were named as
  T_DO_BUILD_HOUSE_NOTE constructors).
- Merged with A2 by the coordinator: conflicts only in AiPlayerData's head
  (A2's +0xc basic_economy and A3's +0x10/+0x14 combined), structs.md and
  TODO.md.
- Next (TODO.md): rename AI::List32Add* to CComputerPlayer::DoomNote*,
  who creates the AiPlayerData nodes, then 0x550a30 / 0x550f80 once A4/A5
  are in.

### 2026-11-23 (48): backlog cleanups (parallel worktree)

Four backlog items from TODO "Evidence and testing", branch wip/cleanups,
one commit each:

- **trivial.py decodes the bytes itself** (capstone on S3.EXE from each
  function start; padding checked byte by byte; `check` also requires each
  note to quote the decoded instructions). The dumpbin listing, desynced by
  jump tables, had made 0x53ba50 a false "ret". Result: 707 trivial
  functions (was 708); 0x53ba50 was the only false positive (row already
  fixed), no new ones, all 706 notes match. frontier.py: only the count
  changes (435 of 707 done).
- **Comments with old names:** Player::unk0b4[0] -> promotion_cost
  (godly_events.cpp), Building::water -> workshop (area_split.cpp),
  SpriteWord -> FrameCount, GfxData::FrameOffset ->
  CGraphicsData::VariantFrameAddress, SumAreaGoods10PerPlayer ->
  SumGoldPerPlayer, g_pixelChannelShifts -> g_pixelFormat, AreaDirection
  (found by checking Struct::member references and the "Was X" names).
- **Crashing tests no longer leave stubs behind:** CalleeStubs/ScopedJump
  record their patches in LivePatches(); after a crash RunAll restores them
  and clears the DebugLog capture (a test that ends with patches left
  fails). ReleaseSiteWorkers_MatchesOriginal crashed in the original
  because settler_high_water stayed random until the first case with
  workers; it now starts at 0. Crash seed 18266603132645735647 and seeds
  0..20 pass; with only the harness fix the crash restored 4 patches and
  MinerJobFood/StealGoodFromPile passed. In-process suite 577/577.
- **UpdateSoldier emulator memory 2.6 GB -> 247 MB:** one invalid case
  (409, random owner >= 20 -> RemoveSettlerFromLists shifts a list of
  random length) wrote 8M bytes in 20M instructions. Spec
  max_instructions = 1M (all valid cases need < 200k: same 1490 cases /
  10 invalid / 294 of 294 blocks at 200k, 1M and 20M, Release and Debug);
  memory_mb dropped; 191 s -> 67 s. run.py --help fixed. --self-test 21/21,
  --changed (200 specs, --jobs 4): 0 mismatches.

Open: FakeClock (screen_element.cpp) isn't restored after a crash
(harmless so far; TODO.md). `--changed` selected 200 specs in the fresh
worktree although only comments/tests changed (probably its own build's
fingerprints vs main's recorded passes; not investigated).

### 2026-11-23 (48): source layout plan (parallel worktree)

`docs/source_layout_plan.md` (commit 1c6b658, merged): inventory of
`src/s3re` (126 of 154 files, 70,500 of 76,117 lines in `game/`; 51 % of
the hooks in unnamed original files), options (by original file, by
subsystem, hybrid) and the recommendation: subsystem folders (world, map,
mapgen, environment, economy, buildings, settlers/jobs, pathing, spells,
logic/commands, ai; ui, graphics, net, framework), shared headers stay in
`game/`, `enums.h` split into `game/enums/`, a generated `source_file`
column in re/functions.csv, one script-driven move commit
(`tools/move_sources.py`) when no worktree is open. Waiting for the user's
answers to its open questions.
