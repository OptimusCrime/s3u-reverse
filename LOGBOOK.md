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

**In progress (session 51, 2026-11-24):** two parallel worktree
batches: computer players A2 leftovers (`wip/ai-a2`) and the CMapFile
container (`wip/mapfile`); new specs go into one new `SPECS += [` block
at the end of specs.py. Meanwhile, in main: the split of
`tools/emutest/specs.py` by subsystem (the last part of the bookkeeping
restructure).

**Next, after this round (the user's order of session 48):**
1. Readability leftovers (`docs/todo/readability.md`). Optional follow-ups of the source layout plan (renames that read
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
