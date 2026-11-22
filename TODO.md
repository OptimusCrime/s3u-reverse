# TODO (backlog)

Things we should do at some point, but not right now. The current plan
and its order are in `LOGBOOK.md` ("Current focus"); larger directions are
in its "Roadmap" section; unexplained behaviour goes under "Open
questions". Add an item here whenever something is postponed, and remove
it when it's done (mention it in the LOGBOOK session entry).

Each item: what, why, and where to start.

## Naming

- **Rename `GameWorld::Create` (0x5166d0) to `CGameData::GenerateRandomMap`.**
  It is the random map generator ("Erstelle Zufallswelt"), not a general
  world constructor; file maps use 0x4fd540, savegames 0x508fa0.
  Postponed until the struct refactor (batch 2b) has landed.
- **Rename the `GameWorld` class to `CGameData` everywhere** (code,
  inventory names, docs, test names). `CGameData` is the real class name
  from RTTI. Large mechanical change; do it in one dedicated commit after
  the struct refactor.
- **Audit provisional names.** Many names were given early from partial
  evidence (e.g. `GameWorld::UpdateSettlers`, `GameWorld::UpdatePhase2`,
  `Logic_sub_*` / `SaveGame_sub_*` / `AI_sub_*` survey names). Review them
  when the functions are worked on; see the naming rule in `CLAUDE.md`.
- **Rename `Settler::direction` (+0x8).** `GameWorld::SetSettlerAnimation`
  (0x50e2c0) stores an animation id there, so "direction" is too narrow
  (found in the typed-access refactor, re/structs.md).
- **Name the layout.h fields the refactor left as `unk`:** e.g.
  `GameWorld::unk2d274` (the symmetry argument of GameWorld::Create),
  `Building::unk1b[0/1]`, and split
  `Player`/`GameWorld::new_game_objective` into enabled + objectives.
  Workers kept the old names to avoid merge conflicts; the meanings are in
  the code comments and re/structs.md.
- **Rename `GameWorld::AddToRecentPositions` (0x510770) to
  `GameWorld::AddPileClaim`**, and the layout field `recent_positions`
  (world+0x1195dd4) to `pile_claims` (entries {pile, settler}), the global
  `g_recentPositionsCursor` (0x7ace60) and the emulator presets. Its only
  caller, UpdateSettlers job 0x152, passes (settler, pile), and
  CountOtherPileClaims (0x510800) counts the entries of one pile held by
  other settlers (found 2026-11-10, session 43; re/world_layout.md).
- **Name constructors** (`re/classes.json` lists the functions that store
  each vtable).
- **Rename `Player::presence_index` (+0xc) to `team`.** It is the team /
  alliance index: the fog of war (fog_of_war.cpp), the minimap and
  CrushBuilding compare it between players (package D, session 45).

## Evidence and testing

- **Record/replay of random-map games:** `--random-map` can't be combined
  with the command recorder, because recordings store only a map file
  name. Add the random-map parameters to the recording format (version 3)
  so synthetic-input games on random maps can be replayed.
- **Emulator coverage of `GameWorld::Create`:** 338/415 blocks; the rest
  needs maps of 130+ tiles, too slow in the emulator (a 320-tile case ran
  for over an hour). Options: a release build of s3re.dll for the emulator,
  a faster emulator path, or accept the in-process whole-world comparison.
  The plain Release build doesn't work for the emulator yet (2026-11-10):
  `run.py --build Release` stops with "GameWorld_CenterCameraOnLocalPlayer:
  0 matching symbols in s3re.map", because the linker folds identical
  functions (/OPT:ICF) and keeps only one name. A separate emulator
  configuration with `/OPT:NOICF` (maybe `/Ob1`) would be needed; then
  compare its full run with Debug's and time GameWorld_Create.
- **Full serial emulator run as a cross-check of `--jobs`:** the parallel
  runs (3 schedules: specs.py order, longest first, slow specs split into
  parts) gave identical output; the `--jobs 1` run was stopped after its
  first 70 specs (all identical). Run `python tools/emutest/run.py
  --require-coverage --jobs 1` once overnight and compare it (without the
  `[...s]` times) with a parallel run's output.
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

The dispatcher GameWorld::ExecuteSpell 0x5480B0 and spells 5..9 and 31 are
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

- A few `RawAt`/`RawAtAddress` uses remain, each with a comment, for
  objects whose layout isn't mapped yet (GfxData frame tables in
  graphics_helpers.cpp, screen_element.cpp's parent vbtable pointer and
  dialog globals, single fields of unidentified lobby/AI objects, the
  single-string classes in string_members.cpp). Give them packed structs
  once more of their fields are known (`grep -rn RawAt src`).
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
