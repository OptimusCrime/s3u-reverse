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

**Next (updated in session 56). The user's priority (2026-11-24): function
batches (reimplement + verify), less cleanup; cleanup only where it blocks
a batch. The user wants each round followed by the next right away.**

**Round 3's evidence is complete** (full suite and whole-game checks,
see its session entry).

Next, the round-3 leftovers (details in their backlog files):
1. GameSettings::LoadSaveGame 0x432b00 and CGameData::InitPlayerInfo
   0x4fd0d0 are S3RE_HOOK_UNVERIFIED: finish their emulator specs
   (LoadSaveGame: team-vector preset fixed, rerun) and switch to verified.
2. Tiny-function trampolines: CButton::IsPressed 0x41a990, SetChecked
   0x41aa20, CDropdown::OnFocusChanged 0x422770 returned a wrong AL when
   the original was called through its Detours trampoline in-process
   (emulator agrees with ours). Find out whether the trampolines of tiny
   functions are broken: it may affect other hooks
   (docs/todo/known-differences.md).
3. Rendering: CDrawer::SetDisplayMode 0x42ce90, Render_sub_4c7490 /
   4c6d40, CLandscapeSurface ctor 0x54bd30 (the fake DirectDraw is ready),
   CGameDrawer ctor/dtor (device/display-mode vectors first), the
   landscape drawing Render_sub_54cbd0 (large) (docs/todo/evidence.md).
4. Framework: CCDAudio::Init (two catch handlers), the CD window
   procedure, the other ThrowWin32Error callers (CRegistryKey, CTimer,
   CThread::Start, CMessageQueue::ProcessMessage).
5. UI: CScreenGroup::~CScreenGroup 0x416da0, CStaticText::SetText
   0x41c810, CListBox::UpdateLayout, the rest of GameConfig.
6. Logic: ~CLogicThread, FinishGame, PublishDrawData (their callees are
   done now), DumpTableToFile, Logic_sub_4d6740 / 0x4d8400.
7. SendPrioritizeSite 0x58e510 needs one game run with computer players
   (log message byte 6 against the site owner); asked the user, no answer
   yet. Behind it: COMP_EmergencyPowersActStone, UpdateMaterialShortages,
   T_ECONOMY_AGENT::Run.

After that: SendPrioritizeSite 0x58e510 needs a runtime look; the
live-vs-replay header hash difference from session 49 (backlog
"Evidence and testing"); readability leftovers only when they block.

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

### 2026-11-26 (56, round 3): framework errors, DirectDraw, UI GDI, settings load

Round 3 (wip/framework-errors, wip/settings-load, wip/ddraw-render,
wip/ui-gdi), stopped at a deadline set by the user and merged in a side
worktree (s3re-merge-r3) while round 2's whole-game checks ran; verified
1514 -> 1714.
- **Framework errors**, 54 functions: CHResult, ThrowWin32Error,
  ThrowHResult, CTimeout, CMMException, CMCIException and their throwers
  (one shared message builder), 12 exception destructors, the CEvent /
  CSemaphore / CMutex / Wait / PostMessage wrappers, CCDAudio
  Open/Close/DoPlay (win32_throwers.cpp, win32_synchronize.cpp).
- **DirectDraw**, 70 functions: a scripted fake DirectDraw
  (test/fake_ddraw.*), all CDirectDraw and surface members, CGameDrawer's
  surface management, CDisplayMode, the Watcom intrusive list code.
  Quirk kept: RemoveSurface of an unlisted surface removes the first one.
- **UI GDI**, 63 functions: fake GDI (in-process and emu.py),
  ShowTooltip, CTextView/CListBox FillBackground, CProgressBar and
  CStaticSurface Draw, 44 GameConfig functions (serial check digit reads
  past its table, kept), old iostream lock/unlock/setf, CButton /
  CVScrollBar / CDropdown members. 3 tiny functions unverified (see
  Current focus).
- **Settings load**, 15 verified: SetupLocalPlayer, LowestFreeNetId,
  FindTeamForClan, CTeam clan helpers, GamePlayerSetup SetType /
  AttachNetPlayer, index checks; LoadSaveGame and InitPlayerInfo
  reimplemented, unverified. re/false_starts.tsv: 41 false function starts
  that frontier.py now skips.
- Merge fixes: two `kOpenParenthesis` texts (second renamed
  kOpenParenthesisOnly); GameConfig::RandomRaceAmazons implemented by two
  batches (kept game_config.cpp's); spec lists whose closing bracket the
  conflict ate.
- Evidence on the merged tree (Debug and Release build): inventory check
  clean; `--test` 972/972; `--self-test` 34/34 + RAM check "separate";
  quick `run.py --changed --require-coverage` (all 1718 specs selected):
  0 mismatches except the unverified LoadSaveGame, 13 slow specs short of
  coverage in quick mode.
- Evidence completed afterwards (same day, main at e5d3a50): full emulator
  suite `--full --require-coverage` (Release, 15 workers, 31 min): 1718
  specs, 0 mismatches and 0 incomplete coverage except the unverified
  GameSettings_LoadSaveGame (77/132 blocks, spec unfinished); whole-game
  checks against the original (Debug): check_save slots 1-3 (2,000
  ticks), check_random_map 384,0 and 448,3 (1,000 ticks), scripted-seed127
  replay (3,000 ticks): all identical; call counts recorded. Identical in
  a whole-game check: 935 -> 1034.

### 2026-11-26 (56, round 2): AI Step, sound effects, UI controls, settings

Round 2 (wip/ai-step2, wip/sound-fx, wip/ui-controls, wip/settings-logic),
merged serially; verified 1324 -> 1514. All four hit the 5-hour usage
limit once and were resumed.
- **Computer players (wip/ai-step2)**: CComputerPlayer::Step 0x550f80,
  COMP_FoodForMines, ConsiderBuildings, WorkOnNote and three small ones.
  Left in the AI: COMP_EmergencyPowersActStone / SendPrioritizeSite
  (runtime look) and, behind them, UpdateMaterialShortages and
  T_ECONOMY_AGENT::Run. Quirk kept: the Asians' large-temple condition uses
  the Pyramid's countdown.
- **Spec name-clash guard (wip/ai-step2)**: specs/__init__.py runs the
  parts statement by statement and fails on a top-level name rebound with
  a different value; the five old clashes renamed (docs/tools/emulator.md).
- **Sound effects (wip/sound-fx)**, 56 functions: CSoundEffect (Play,
  Load, Restore, PurgeStoppedDuplicates), CSoundFile / CMemorySoundFile,
  the duplicates' dictionary, CDirectSoundException, basic CCDAudio.
- **UI controls (wip/ui-controls)**, 104 functions: 61 deleting
  destructors, CControl ctor/dtor/OnTimer, CGroupControl::Tick, buttons,
  CScreen::PostMessage, App::WaitTime (the -debug switch g_debugSwitch).
  Bug fixed in the verified CControl::SetTooltipTextId (callee argument
  counts swapped; only worked because the stack balanced).
- **Settings and logic (wip/settings-logic)**, 33 functions: std::wstring
  _Copy/_Grow/_Split/assign, GamePlayerSetup copy/assign/Set (Set is now
  S3RE_HOOK_ALWAYS with the --race override; S3RE_ORIGINAL_TOOLING_VERSION
  8), the settings' Watcom vectors, BuildOffsetTable, InitAfterMapLoad,
  ~CGameData.
- Merging: re/functions.csv conflicts are resolved per row (the branch's
  row when name/status changed, otherwise main's with the call counts).
- Evidence on the merged main: Debug and Release build; inventory check
  clean; `--self-test` 34/34 + RAM check "separate"; `--test` 942/942;
  full emulator suite `--full --require-coverage`: 1514 functions, 0
  mismatches, 0 incomplete; whole-game checks against the original
  (check_save slots 1-3, check_random_map 384,0 and 448,3, scripted-seed127
  replay): all identical; call counts recorded. Identical in a whole-game
  check: 858 -> 935.

### 2026-11-25 (56): AI agents and area lists, sound loader, UI Watcom containers

Parallel round of four batch agents (wip/ai-agents, wip/ai-areas,
wip/sound, wip/ui-watcom), merged serially; verified 1102 -> 1324.
- **Computer players, agents (wip/ai-agents)**, 23 functions: the
  expand-empire and map-worker agents' Run / MakeBuildOrders (the
  55-case switch as a rule table), HasBuildingTypeNear, AdjustSoldierShares,
  OnBuildingFinished, CentrumCheck, credits debug helpers. Quirks kept:
  castle ordered only when an own castle is within 60 (inverted test); the
  mill checks for waterworks; a finished gem mine tears down the owner's
  gold mines and smelters; the expand agent always builds at the advanced
  position.
- **Computer players, area lists (wip/ai-areas)**, 13 functions:
  FillAreaSiteLists, RefillAreaSiteList, AddArea, ProcessPendingEvents,
  COMP_FoodForMinesOptimum, CComputerPlayer::Init and ctor, the AreaList
  ctor/dtor and bitset ctor (manual rows). AddArea re-points the per-type
  site tree pointers after the insert (open point closed). Event types
  documented. Both AI agents had done OnBuildingFinished: the ai-agents one
  kept at merge. Only CComputerPlayer::Step is left in the AI.
- **Sound (wip/sound)**, 39 functions: CSoundData (ctor, Open,
  DetachFromMedium, Close, Restore, volume), GetEffect / PurgeEffects, 26
  DirectSound wrappers (framework/directx_sound.cpp, fake DirectSound in
  test/fake_dsound.*). SND format in re/sound_formats.md. Quirks kept: the
  effect cache key drops the file number; PurgeEffects spares only effects
  used exactly 300,000 ms ago. New subsystem folder game/sound/.
- **UI range (wip/ui-watcom)**, 152 functions: Watcom vectors with one
  shared helper (watcom/vector_base.h), 37 deleting destructors, MSVC6
  string/wstring members, 22 small control members, Dirk::CTeam.
  GamePlayerSetup is RTTI's Dirk::CPlayer (rename in docs/todo/naming.md).
- Evidence on the merged main: Debug and Release build; inventory check
  clean; emulator `--self-test` 32/32 + RAM check "separate"; `--test`
  894/894 (Debug); full emulator suite `--full --require-coverage`: 1324
  functions, 0 mismatches, 0 incomplete coverage; whole-game checks against
  the original: check_save slots 1-3 (2,000 ticks), check_random_map 384,0
  and 448,3 (1,000 ticks), the scripted-seed127 replay (3,000 ticks): all
  identical; call counts recorded. Identical in a whole-game check: 777 ->
  858.

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
