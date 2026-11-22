# TODO (backlog)

Things we should do at some point, but not right now. The current plan
and its order are in `LOGBOOK.md` ("Current focus"); larger directions are
in its "Roadmap" section; unexplained behaviour goes under "Open
questions". Add an item here whenever something is postponed, and remove
it when it's done (mention it in the LOGBOOK session entry).

Each item: what, why, and where to start.

## Readability pass (docs/coding_style.md)

- **Convert the remaining files** to the style guide (pilot done in
  session 45: settler_idle.cpp, update_economy.cpp, settler_walk.cpp).
  Batches by file group, see the LOGBOOK session 45 entry.
- **Typed fields:** once every user of `Settler::job`, `Settler::art`,
  `Building::art`, `Pile::good` or the race fields is converted, give the
  field its enum type in `layout.h` and drop its accessor from
  `game/typed_fields.h`.
- **Rename `BuildingWaterBytes`** (layout.h, re/structs.md) to a workshop
  name: its buildings 0x1f/0x2f/0x30/0x32 are the catapult, ballista and
  cannon workshops and the large gong (re/enums.md).
- **BuildingTypeAllowedForRace** (player_queries.cpp) does not fit the
  building numbering of enums.h; find what its argument really is.
- **Lint** for hex literals in comparisons and magic numbers in
  job/type/goods comparisons (coding_style.md "Checks").

## Naming

- **Audit provisional names.** Many names were given early from partial
  evidence (e.g. `CGameData::UpdateSettlers`,
  `Logic_sub_*` / `SaveGame_sub_*` / `AI_sub_*` survey names). Review them
  when the functions are worked on; see the naming rule in `CLAUDE.md`.
- **WalkSettler whole-game evidence for shooting and boarding:** no check
  so far calls SpawnProjectile (soldiers shooting from a post) or, probably,
  boards a ship; a recorded late game with fighting and ships would cover
  them (settler_walk.cpp; LOGBOOK session 45).
- **Name constructors** (`re/classes.json` lists the functions that store
  each vtable).
- **Remaining `unk` fields of the world records** (layout.h, session 45
  struct pass): Settler +0x1b/+0x2f, Building +0x01/+0x36/+0x37 and the
  site byte +0x1e, Player +0x8/+0x18/+0x1c/+0x88/+0xb4..+0xc0/+0x12c,
  the MapEffect graphics fields (+0x6, +0x18..+0x24, +0x2c..+0x39; their
  reader is the draw code), world +0x2d2ec/+0x2d304..+0x2d310. Name them
  when their readers (UpdateSettlers, ExecuteCommands, PrepareDrawData)
  are reimplemented.

## Evidence and testing

- **Emulator memory: the CGameData_UpdateSoldier spec peaks at ~1.6 GB
  per worker** (measured 2026-11-22 with `run.py CGameData_UpdateSoldier
  --jobs 1 --memory`; WalkSettler/UpdateEconomy/UpdateEnvironment ~120 MB).
  With 8 workers a full suite can need ~13 GB. Find what grows (probably a
  huge lazily-random world range or the garrison walks) and bound it;
  update WORKER_MEMORY in run.py and CLAUDE.md's figure.
- **In-process tests that crash on other inputs:** test inputs are seeded
  from the test name. During the GameWorld -> CGameData rename (session
  45) the new names gave new inputs, and
  `CGameData_ReleaseSiteWorkers_MatchesOriginal` crashed inside the
  original (access violation at S3.EXE+0x10e443, address 0x0548B002); the
  crash left CalleeStubs patches installed, so later stub tests
  (MinerJobFood, StealGoodFromPile) crashed too. The rename now hashes the
  old names (`SeedName` in difftest.cpp, `seed_name` in emutest/run.py),
  so inputs are unchanged. To do: make that test's input generator
  produce only states the original can handle (try `S3RE_TEST_SEED`
  values), and have a crashing test's CalleeStubs restore its patches
  (the SEH guard skips the destructor).

- **Emulator coverage of `CGameData::GenerateRandomMap`:** 338/415 blocks; the rest
  needs maps of 130+ tiles, too slow in the emulator (a 320-tile case ran
  for over an hour). Options: a release build of s3re.dll for the emulator,
  a faster emulator path, or accept the in-process whole-world comparison.
  The plain Release build doesn't work for the emulator yet (2026-11-10):
  `run.py --build Release` stops with "CGameData_CenterCameraOnLocalPlayer:
  0 matching symbols in s3re.map", because the linker folds identical
  functions (/OPT:ICF) and keeps only one name. A separate emulator
  configuration with `/OPT:NOICF` (maybe `/Ob1`) would be needed; then
  compare its full run with Debug's and time CGameData_GenerateRandomMap.
- **Full serial emulator run as a cross-check of `--jobs`:** the parallel
  runs (3 schedules: specs.py order, longest first, slow specs split into
  parts) gave identical output; the `--jobs 1` run was stopped after its
  first 70 specs (all identical). Run `python tools/emutest/run.py
  --require-coverage --jobs 1` once overnight and compare it (without the
  `[...s]` times) with a parallel run's output. (Session 45 found and
  fixed one schedule dependence: stale translated code after a spec's
  stub over FindPath; `--self-test` now checks split runs.)
- **186 verified functions have only test evidence** (never called
  in a whole-game check; see the progress page's tests-only list). Done
  so far: random maps (session 40) and an 8-player recording
  (fun8-seed2024-input31.bin). Still missing: longer games (10,000+ ticks,
  needs faster runs), a human-race variety, the user's late-game savegame
  (promised for later). 41 of them may run uncounted (called directly
  by our own replacements); `--skip-hooks CALLER` counts them (the
  original caller then calls our callee through the thunk).
- **Integration play:** the user hasn't played a normal game with all
  hooks active yet (CLAUDE.md workflow step 8).

- **Longer and more varied fast runs:** with `--fast` a 10,000-tick
  recording takes about a minute; record one (synthetic input records at
  normal speed, so a long one takes real time once) and check whether
  more replacements get called late (first-call ticks, `tools/first_calls.py`).
- **Run normal vs `--fast` on the other savegames** ("slot 2/3.sav") once;
  only "slot 1.sav" and the recording are shown identical so far. Watch
  `+0x2d2d8` (per-step counter, see LOGBOOK open questions).
- **Trace cache refinements:** cache the original's runs for
  check_determinism.py and check_random_map.py too; prune old entries.
- **Normal vs `--fast` on a random map:** check_random_map.py uses
  `--fast` by default, but normal vs fast has only been compared on file
  maps and a savegame.

## Spells left of UpdateEnvironment's call tree (package D, session 45)

The dispatcher CGameData::ExecuteSpell 0x5480B0 and spells 5..9 and 31 are
verified (spells.cpp); the other handlers of Volker\GodlyEvents.cpp are
still original code, called by address (plan and sizes in
`re/update_environment.md`): 0x548340 (spells 0..3, with 0x548380 area
effect -> 0x548760, 0x548A10, 0x548BB0, 0x548D50; 0x548380 is also called
by UpdateEnvironment for burning trees 0xF4), 0x548FB0 (4), 0x549F40 (10,
12, 16, 17, 24, 25), 0x54A370 (11), 0x54A7C0 (13), 0x54A540 (14),
0x54A1E0 (15) with helpers 0x54AF60 / 0x54B000, 0x549BD0 (EventConvert,
18..20, 27), 0x5499B0 (EventGodlyG.., 21..23, 26; x87), 0x54AA40 /
0x54ACD0 (EventGoodsB/S, 28/29), 0x5492A0 (EventTreeCut, 30). About 2,800
instructions. No recorded game casts spells yet (all spell counts 0 in the
replays and savegames): a recording with spells (temples, mana) would give
whole-game evidence.

## Known differences to clean up

- Some replacements leak temporaries on fatal error paths because they have
  no exception-unwind frames (std::string `_Xlen`/`_Xran` paths, the throw
  helpers 0x5ffcc0 / ThrowOutOfMemory, ...). Harmless today (the game
  ends), but worth fixing for the standalone build. List: inventory notes
  containing "Known difference".

## Worktrees

- **Leftover worktree `.claude/worktrees/agent-a06bfddbf49ac7135`**
  (branch `worktree-agent-a06bfddbf49ac7135`, "Typed access: settlers and
  buildings (group B)", not merged by hash into origin/main; its content
  was probably merged as "Merge typed-access conversion (group B)"). Check
  with `git diff origin/main...worktree-agent-a06bfddbf49ac7135`, then
  remove it **only after deleting its `Settlers3Ultimate` junction first**
  (`cmd /c rmdir .claude\worktrees\agent-a06bfddbf49ac7135\Settlers3Ultimate`,
  see CLAUDE.md: `git worktree remove --force` deletes through junctions).

## Typed access leftovers

- **Class layouts left after struct pass S2 (session 45).** The object
  `RawAt`s are gone (two test-only ones in string_members.cpp are marked
  `raw-ok`). Left, by value:
  - Rename the `GfxData_*` functions to `CGraphicsData::*` (the class is
    identified, game/gfx_layout.h): inventory, hooks, tests,
    `whole_game.py fix-names`.
  - `DrawTarget` (graphics_helpers.cpp; Render_PutPixel16 / Entry204:
    pitch +0x90, pixel buffer +0xa4, table +0x204; Volker\DrawBuilding.cpp):
    class not identified (not CLandscapeSurface, which is 0xa8 bytes).
  - `CLogicThread` (logic_thread_layout.h): mirror its RTTI bases
    (DirectX::CGameThread -> Win32::CThread) once their sizes are known;
    tooling/fast.cpp still uses raw offsets +0x3bc (timer) and +0x3dc
    (wait set) - original-mode tooling, so bump
    `S3RE_ORIGINAL_TOOLING_VERSION` if it changes.
  - Per-file partial structs of not yet identified classes:
    framework_helpers.cpp (OwningPointer, DwordPair, CreditsTextStyle,
    LobbyRecord5ebfb0, NetHelpRecord), framework_small.cpp (IsvList,
    IntVector: move to watcom/ headers as WCIsvSList / WCValVector
    layouts), id_allocators.cpp (IdTable), folded_getters.cpp, and the
    single-string classes of string_members.cpp (named after their
    functions; identify the classes, mostly lobby code).
  - Constructors of CScreenGroup (~0x416ca0) and CControl (~0x418000) are
    known now; their remaining unk fields can be named from them.
- `MessageQueue` (logic_thread_layout.h) is opaque; map its ring-buffer
  fields when the net/bounce thread code is reimplemented.

## Code organisation

- **Reorganize our source tree after the original source files**
  (`re/source_files.md`; plan first, see the LOGBOOK roadmap).

## Postponed by decision

- **Network and lobby code** (CNetThread, Lobby::*, LobbyClient::*,
  DirectPlay, CLM* messages): skipped since 2026-11-09 (single player
  against computer players is easier to verify; online play may not matter
  today). Revisit when the single-player game is far along.
