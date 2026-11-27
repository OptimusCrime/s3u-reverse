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

**Next (updated in session 58). The user's priority (2026-11-24): function
batches (reimplement + verify), less cleanup; cleanup only where it blocks
a batch. The user wants each round followed by the next right away.**

**Round 7 is merged** (session 58 entry): main has cursor-r7, panel-r7,
settlerdraw-r7 and libs-r7; all worktrees are removed. The round's
evidence run is done (full emulator suite, self-test, whole-game checks
against the original: all identical; session 58 entry).

**Next jobs, in order:**
1. Render_sub_4b26c0 (the next piece of the frame drawer's tree after
   DrawPanel and QueueSettler); it needs the landscape renderer
   Render_sub_54cbd0 0x54cbd0 first (with the CLandscapeSurface ctor
   0x54bd30 and the CGameDrawer ctor/dtor leftovers).
2. **CRT "platform library" status** (CLAUDE.md "Library and compiler
   code", backlog `docs/todo/evidence.md`): the status for the CRT
   functions the game doesn't depend on, counted apart from verified.
3. The 28 odd static-initializer stubs that tools/static_inits.py's
   classes don't cover.
4. The png_read_* in-process test that hangs (backlog
   `docs/todo/evidence.md`; libs-r7 verified them by the emulator); the
   6 unverified libpng functions from libs-r7 (re/picture_code.md).
5. Console/startup code: App_sub_40d700 and the rest of
   CConsoleThread::OnStart's callees.
6. Then the **decision point on network code** (so far skipped, user
   decision 2026-11-09): ask the user whether the lobby/net threads come
   next or the AI (CComputerPlayer::Step 0x550f80).
- Watcom leftovers: after libs-r7's 101 recategorizations, the rest are
  misnamed game/lobby functions that inline vectors (rename them).
  0x472eb0 stays original (folded DebugLog call).
  GamePlayerSetup_Assign_MatchesOriginal is possibly flaky (heap-address
  dependent; passed in all session-58 runs; backlog it if it fails).

Backlog of the rest: `TODO.md` and `docs/todo/*.md`. Session 56's
leftover list is mostly done (LoadSaveGame, InitPlayerInfo, the tiny
UI functions, SetDisplayMode/ChangeDisplayMode, ApplyScreenLayout,
~CScreenGroup, CStaticText::SetText are verified since rounds 4-5); still
open from it: the remaining rendering (CLandscapeSurface ctor 0x54bd30,
CGameDrawer ctor/dtor, Render_sub_54cbd0), CCDAudio::Init and the CD
window procedure, CListBox::UpdateLayout, the rest of GameConfig,
DumpTableToFile, Logic_sub_4d6740 / 0x4d8400 (check their rows first).
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

### 2026-11-27 (58): rounds 6 and 7 merged, milestone evidence

Merged into main, one at a time (re/functions.csv three-way per row
against the merge base; no row was changed on both sides except
0x60b6a0):
- wip/watcom-r6 -> a8c84fc (241 Watcom instantiations): conflicts only in
  the generated progress files; the build then failed on four vtable
  constants (kWCValListBase_CScreenElementP/_bool/_MsgConsole,
  kWCIsvSListBase_MsgConsole) that menus-r5 had already put in vtables.h
  (same values): the branch's copies dropped. `--test` 1086/1086,
  emutest `--changed` 395 functions, 0 mismatches.
- wip/png-r6 -> 6437c8e (28 verified, 2 unverified): re/globals.csv both
  sides' rows kept (no duplicate addresses). `--test` 1097/1097, emutest
  `--changed` 78 functions, 0 mismatches.
- wip/text-r6 -> ec7653e (34 functions): its WideToNarrowString 0x60b6a0
  (framework/wide_to_narrow.cpp + tests + spec) duplicated main's
  (framework/wide_strings.cpp, menus-r5; same logic, main's has the
  emulator spec with the wcstombs probe and the self-test pairing): the
  branch's files, CMake lines and spec dropped, main's row kept, so
  0x60b6a0 is hooked once. specs/framework.py, re/globals.csv and
  docs/tools/emulator.md combined. `--test` 1119/1119 (the branch's
  earlier 992/993 did not reproduce); emutest `--changed` (harness
  changed, so all specs, quick) 0 mismatches.
- Milestone evidence on the merged main: full emulator suite (`--full
  --require-coverage`, Release, 15 workers, 44 min): 12,763 functions,
  0 mismatches, 0 incomplete coverage; `--self-test` 40/40 + RAM
  "separate". Whole-game checks against the original (Debug): check_save
  slots 1-3 (2,000 ticks), check_random_map 384,0 and 448,3 (1,000
  ticks), scripted-seed127 replay (3,000 ticks): all identical; call
  counts recorded (record_call_counts.py: 34 rows updated).
- Numbers (progress.py): verified 12,764 functions (74.20 %), game code
  41.1 % verified (headline), all code 48.5 % of the bytes; identical in
  a whole-game check 11,665 functions (39.48 % of the bytes).
- wip/cursor-r7 -> ef65f96 (27 verified: SetMouseCursor 0x42cb60, CDrawer
  surfaces/lifetime, CGameThread/CGameDrawThread, CDrawThread steps,
  CGameDrawer ctor/Shutdown, two Watcom vector ctors): drawer_layout.h
  combined (text-r6 panel fields + int32 unk05f6b4 + cursor-r7's tail),
  one kDDUnlock, specs appended; `--test` 1153/1153, emutest `--changed`
  27 functions 0 mismatches; verified 12,791 (48.57 % of the bytes); no
  whole-game check yet for 28.
- wip/panel-r7 -> e5c5e18 (CDrawer::DrawPanel 0x4b4d00 in six panel_*.cpp
  files, BuildingAnimationSlot 0x4b1a00): conflicts only texts.h and
  specs/graphics.py (both appended); `--test` 1157/1157, emutest
  `--changed` 0 mismatches, CDrawer_DrawPanel `--full` 8000 cases 0
  mismatches 852/868 blocks (bounded spec); check_save slot 1 (2,000
  ticks) identical to the original, DrawPanel 529 calls; verified 12,793
  (51.75 % of the bytes), identical in a whole-game check 11,691 (42.75 %).
- wip/settlerdraw-r7 -> e863d96 (CDrawer::QueueSettler 0x4c85a0, was
  Render_sub_4c85a0): draw_data_layout.h combined (panel-r7's union kept;
  sound_level +0x5c, local_player +0x98 in the union, player_team +0x294),
  texts.h / re/globals.csv appended; `--test` 1159/1159, emutest
  CDrawer_QueueSettler 8000 cases 0 mismatches 547/547 blocks;
  check_save slot 1 identical, 14,479 calls; verified 12,794 (53.46 % of
  the bytes), identical in a whole-game check 11,692 (44.46 %).
- wip/libs-r7 -> c99bb55 (37 Watcom/libpng/PaintLib functions verified,
  6 libpng unverified, 101 rows renamed/recategorized, SetErrorMode API
  probe, WrongFinishRow self-test pairing). Duplicate: the Watcom vector
  constructors 0x5b7ec0 / 0x5b7f40 were in both main (cursor-r7) and the
  branch, in the same file bare_vectors.cpp with identical code: the
  branch's functions, hooks, table entries, specs and vtable constants
  dropped, main's kept (verified_by gains the branch's
  wrong-implementation test). re/functions.csv three-way: only those two
  and 0x5b69c0 (CDirectDrawDevice::IsEqual: main's verified row, the
  branch's category game) changed on both sides. Debug + Release build,
  `--test` 1167/1167 (it took 48 min while the full emulator suite ran
  next to it), emutest `--changed` 0 mismatches. Worktree removed.
- Round 7 evidence on c99bb55: full emulator suite (`--full
  --require-coverage`, Release, 16 workers, 36 min): 12,834 functions,
  0 mismatches, 1 with incomplete coverage (CDrawer_DrawPanel, bounded
  spec, 852/868 blocks as designed); `--self-test` 41/41 + RAM
  "separate". Whole-game checks against the original (Debug; the
  original's traces from the cache): check_save slots 1-3 (2,000
  ticks), check_random_map 384,0 and 448,3 (1,000 ticks),
  scripted-seed127 replay (3,000 ticks): all identical;
  record_call_counts.py: 28 rows updated.
- Numbers (progress.py): verified 12,829 functions, game code 45.5 %
  verified (headline; 37.5 % in a whole game), all code 54.05 % of the
  bytes; identical in a whole-game check 11,693 functions (44.46 %).

### 2026-11-26/27 (57): merges of rounds 4-5, static-initializer stubs, milestone evidence

Merged into main: wip/app-r4 (58 functions), wip/images-r4 (98),
wip/logic-r4 (148), wip/draw-r5 (4 functions, 43.5 KB), wip/menus-r5 (47)
and wip/stubs-r5 (10,394 compiler-generated static-initializer stubs,
generated by tools/static_inits.py; 7,344 missed function starts from the
CRT initializer table added). Decisions recorded in CLAUDE.md: library
and compiler code counts; no serial numbers in the repo (the serial
literal was removed, THIRD_PARTY_NOTICES.md added).
- stubs-r5 merge (3d0d027): conflicts only in re/globals.csv (both sides
  kept, no duplicate addresses) and re/functions.csv (three-way per row:
  4,441 branch-only changes, 53 main-only, none changed on both sides,
  7,344 new rows); progress files regenerated. Every address is hooked
  once (12,469 hooks). Debug and Release build; `--test` 1064/1064.
- **Bug found by the whole-game checks:** CDrawer::PlayAmbientSounds
  0x4c7730 (logic-r4) called CSoundData::GetEffect with 5 arguments and
  CSoundEffect::Play with none; the original pushes Play's (volume, pan,
  0) before GetEffect(id, -1), which pops only 8 bytes. In the Debug build
  this failed the stack check, the game lost activation (display back to
  the desktop mode) and StopThreads waited forever, so the shutdown
  watchdog killed every run and no call counts were written (traces were
  still identical: sound isn't simulation). The in-process stubs and the
  spec probes had the same wrong counts; fixed all three. Found by
  bisecting with the new `--skip-hooks` prefix patterns (`Name*`); the
  watchdog now logs every thread's stack ("hang:" lines). This was
  probably the crashing the user saw.
- Evidence on the merged main (Release for the emulator, Debug for the
  game): full suite `--full --require-coverage` (8 workers, 49 min):
  12,460 functions, 0 mismatches, 0 incomplete coverage (PlayAmbientSounds
  rerun after the fix: 29/29 blocks, 0 mismatches); `--self-test` 40/40
  + RAM check "separate"; whole-game checks against the original:
  check_save slots 1-3 (2,000 ticks), check_random_map 384,0 and 448,3
  (1,000 ticks), scripted-seed127 replay (3,000 ticks): all identical,
  clean shutdowns, call counts recorded. Identical in a whole-game
  check: 1034 -> 11,606 functions (38.82 % of the bytes; most of the new
  ones are the static-initializer stubs, which run at every start);
  verified 12,461 functions, 45.51 % of the bytes.

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
