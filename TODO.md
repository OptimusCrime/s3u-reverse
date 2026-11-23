# TODO (backlog)

Things we should do at some point, but not right now. The current plan
and its order are in `LOGBOOK.md` ("Current focus"); larger directions are
in its "Roadmap" section; unexplained behaviour goes under "Open
questions". Add an item here whenever something is postponed, and remove
it when it's done (mention it in the LOGBOOK session entry).

Each item: what, why, and where to start.

## Readability pass (docs/coding_style.md)

- **SettlerJob names from UpdateSettlers (session 45)** for `enums.h` /
  `re/enums.md`, once the readability rollout no longer changes enums.h
  (the handler files then switch from hex job numbers to these names):
  8/9 DiggerLower/DiggerRaise; 0x10/0x6a/0x8b Swordsman/Bowman/PikemanEnterTower;
  0x8c BowmanInTower; 0x13..0x16 Woodcutter/Stonecutter/Sawmiller/ForesterGoIn;
  0x18/0x19/0x1b Woodcutter/Stonecutter/ForesterSearch; 0x1a SawmillerStart;
  0x1e/0x1f Woodcutter/StonecutterLeave; 0x20/0x21 WoodcutterAtTree/Cut;
  0x22/0x23 StonecutterAtStone/Cut; 0x24/0x25/0x26 WoodcutterHome/PutLog/GoInAgain;
  0x27/0x28/0x29 the same for the stonecutter; 0x2c/0x2e/0x2f
  ForesterLeave/AtSpot/Plant; 0x31/0x32/0x33 SawmillTakeLog/BoardDone/Leave;
  0x36 SettlerDies; 0x3e GoldBarDone, 0x41 GoldSmelterStart; 0x48
  IronBarDone, 0x4b IronSmelterStart; 0x4d CarrierAtTradePile, 0x4f
  CarrierAtTradeBuilding, 0x51 CarrierDone; 0x65/0x66/0x67
  Forester/Woodcutter/StonecutterBackIn; 0x6c..0x73, 0x131 StartWork*;
  0x74 ToolsmithStart, 0x80 ToolsmithToolDone; 0x75 WeaponSmithTakeInputs,
  0x86 WeaponSmithFinish; 0x76 FisherSearch, 0xb3 FisherCast, 0xb7
  FisherDeliver; 0x77/0x132 Pig/DonkeyFarmTakeInputs; 0xca/0xd0
  Pig/DonkeyFed; 0xd2 DonkeyRelease; farmer 0x79 Search, 0x8f WalkToSow,
  0x91 Sow, 0x92 Sown, 0x95 WalkToHarvest, 0x96 AtField, 0x97 Harvest,
  0x9a Deliver; 0x9e/0xa2 MillerGrind; 0x7c WaterworkerSearch, 0xbd Draw,
  0xc0 Deliver; 0x8e UnitAfterFight; 0xad GuardHeal; 0xaf
  GarrisonSoldierDies; 0xb0 UnitWalkCountdown; 0xd5 GeologistDone;
  rice 0xe2 Start, 0xe3 Search, 0xe4/0xea LeaveToPlant/LeaveToHarvest,
  0xe6/0xe7 Plant/Planted, 0xeb/0xec AtField/Harvest, 0xee/0xef/0xf0
  Arrive/Store/Enter; wine 0xf4 Search, 0xf7/0xf8 VinePlant/Planted,
  0xfc/0xfd AtField/Harvest, 0xff/0x100/0x101/0x104
  Arrive/EnterLoaded/Store/Enter; 0x120..0x123 Distiller/Brewer/Powder/
  CharcoalCheck; 0x129/0x12b/0x12d WorkshopPutInput*; 0x12f PioneerDone;
  0x138/0x139 Ballista/CannonProduction; 308 DonkeyLoad, 310 DonkeyUnload,
  331 ShipLoad, 332 ShipUnload; 314 ShipyardUnitDone, 315 Fetch, 316/318
  TakeBoard/TakeIron, 317/319 ToSlipway, 323 Build, 324 Home, 325
  ShipBeingBuilt; 333 HealerWait, 334 Heal, 335/336 Done; 341
  AlchemistStart, 346 Roll, 347..350 Store; 355 BeekeeperSearch, 358
  PlaceHive, 363 Harvest, 367 StoreHoney; 0x170 BeekeeperStart; 382
  MeadStart, 387 MeadStore; 0x194 ThiefReturned; 401 GongFetch (decimal
  where given so). Provisional: check each against its handler first.
- **Convert the remaining files** to the style guide (pilot done in
  session 45: settler_idle.cpp, update_economy.cpp, settler_walk.cpp;
  rollout groups 1..5 done (settlers, buildings, piles/areas, map,
  environment/spells), see LOGBOOK "readability rollout"; map_effects,
  pathing, logic_helpers, player_queries in session 47). Still to do:
  map_effect_spawns.cpp (light touch only: its effect tables are still
  hex), the AI, UI and framework files.
- **Tile::object enum:** the landscape object values (trees 0x44..0x52,
  young trees 0x55..0x6f, fire 0xf3..0xf6, ...) stay hex in
  environment.cpp, spells.cpp and map_generation.cpp until they have names.
- **Shared terrain, object and resource constants:** building_sites.cpp,
  player_queries.cpp, environment.cpp, terrain_updates.cpp, spells_land.cpp,
  settler_jobs_food.cpp and logic_helpers.cpp each define their own
  `kTerrainGrass`/`kTerrainClassGrass`, `kTerrainWornPath`,
  `kObjectFirstTree`..., `kResourceCoal`...; move them to one header when
  enums.h is split (Tile::terrain/object/resources, with re/enums.md
  evidence).
- **Typed fields:** once every user of `Settler::job`, `Settler::art`,
  `Building::art`, `Pile::good` or the race fields is converted, give the
  field its enum type in `layout.h` and drop its accessor from
  `game/typed_fields.h`.
- **BuildingTypeAllowedForRace** (player_queries.cpp) does not fit the
  building numbering of enums.h; find what its argument really is (the
  type numbers stay decimal numbers there until then).
- **Map effect record fields** (layout.h `MapEffect`): +0x06, +0x2c
  (probably a graphics id), +0x32, +0x34 (the value the "stationary"
  make function 0x543f70 sets) are still `unkNN`; the spawn functions in
  map_effects.cpp name the arguments by offset only.
- **Lint** for hex literals in comparisons and magic numbers in
  job/type/goods comparisons (coding_style.md "Checks").

## Computer players (re/computer_players.md)

- **Package A2: small READY helpers** of the AI range (`python
  tools/frontier.py --range 54cbd0 59fcd0`): the T_ECONOMY_AGENT
  conditions 0x5693c0..0x569c90 (calls of AI::RecordBestCandidate; first
  find what the agent fields +0x42..+0x90 are), the map queries
  0x599510..0x5999e0, 0x58ecb0, 0x58ed30, 0x58ed70, 0x59dbb0, 0x59dc70,
  0x5930f0, 0x593140, 0x593f50, 0x5720c0, 0x571d00..0x572420.
- **Package A3: CComputerPlayer's note tree**: DeleteNote 0x551330,
  0x551270, 0x551400, 0x551490, dispatch 0x551510, 0x5514c0, 0x551590,
  0x5516e0, 0x551670, the destructor 0x5509d0 / 0x550f30, 0x551a70, the
  note callbacks 0x582480 / 0x582510 (virtual calls: Spec.virtuals).
- **Package A4: command senders** 0x58e220..0x58ec33 and
  0x590990..0x590d86: need `MessageQueue::Push` (pass-through hook) and
  `GameSetup_sub_4318a0` (0x7a7a18 +0x7c flag; name it) as probed callees.
- **Package A5: Cevaland** (land evaluation) and **A6: the agents' run
  functions** (slot 8), smallest first; then 0x550f80 and the
  constructor 0x550900 / 0x550a30.
- **Open:** which classes kinds 1, 2, 4 of AI::CreateAgent make (read its
  jump table 0x56fa68); the meaning of CComputerPlayer list +0x200 and of
  the AiPlayerData nodes (per settlement?); `AiList50Owner` in
  ai_layout.h is the same object as ComputerPlayer (merge them when
  ai_helpers.cpp is next touched); a world-field list of the AI range with
  `tools/world_fields.py`.

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
  site byte +0x1e, Player +0x8 (bit 0: big temple destroyed)/+0x18/+0x1c (network id)/+0x88/+0x12c,
  the MapEffect graphics fields (+0x6, +0x18..+0x24, +0x2c..+0x39; their
  reader is the draw code), world +0x2d2ec/+0x2d304..+0x2d310. Name them
  when their readers (UpdateSettlers, ExecuteCommands, PrepareDrawData)
  are reimplemented.

## Evidence and testing

- **ExecuteCommands whole-game evidence (session 47).** Reimplemented and
  verified (in-process + emulator); the recordings contain ids 0x01, 0x03,
  0x04, 0x05, 0x09, 0x0b, 0x0c, 0x0f, 0x10, 0x11, 0xfb. Run the round-end
  replays/saves against the original and record the call count; the
  multiplayer/UI paths (0x06 text, 0xf4, 0xf7..0xff, messages 3..0x22)
  have tests only.
- **UpdateSettlers handlers with tests only (session 45).** The
  whole-game checks (two replays, three saves, a random map) never reach
  these UpdateSettlers jobs; check them against the user's full-game
  recording (`check_replay.py --recording FILE --against-original`, then
  `record_call_counts.py`). A per-job hit count needs a temporary counter
  in update_settlers.cpp's dispatch. Jobs (hex):
  building 0x38, 0x6a; drinks 0xe2..0x127 (rice farmer, winegrower,
  distiller/brewer/powder maker, charcoal burner); food 0x6c..0x73,
  0xce..0xd3, 0x131, 0x132; metal 0x32..0x34, 0x3a..0x41, 0x52..0x54,
  0x56, 0x5c..0x5f, 0xd6..0xe1, 0x6b, 0x7d..0x82, 0xaf, 0x128..0x139,
  0x186..0x18f; special 0x134, 0x136, 0x13a..0x14f, 0x150..0x160,
  0x164..0x171, 0x17c..0x185, 0x190..0x194; units 0xae and the walking
  jobs 0xe5, 0xe8, 0xed, 0xf1, 0xfe, 0x102, 0x133, 0x135, 0x140, 0x141,
  0x147..0x14a, 0x165, 0x168; the 20 no-op jobs.
- **Emulator memory: the CGameData_UpdateSoldier spec peaks at ~1.6 GB
  per worker** (measured 2026-11-22 with `run.py CGameData_UpdateSoldier
  --jobs 1 --memory`; WalkSettler/UpdateEconomy/UpdateEnvironment ~120 MB).
  The default worker count now counts it (`Spec.memory_mb=1600`, 400 MB
  for the others, session 45 tooling). Still to do: find what grows
  (probably a huge lazily-random world range or the garrison walks) and
  bound it; then drop `memory_mb` and update CLAUDE.md's figure.
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

- **Emulator coverage of `CGameData::GenerateRandomMap`:** 338/415 blocks
  in full Debug runs (1 quick case per batch since 2026-11-23); the rest
  needs maps of 130+ tiles. Its real evidence is the in-process
  whole-world tests and the random-map whole-game checks. Since c3f29be
  the Release build works for the emulator (`run.py --build Release`,
  ~5x faster on FindPath); time GenerateRandomMap there and decide whether
  a bounded larger-map case is worth it, or exempt the rest with reasons
  so `--require-coverage` stops failing on it.
- **Specs over the quick-mode budget (60 s) before full coverage:** they
  get no recorded pass from quick runs, so `--changed` reselects them
  until a `--full` run passes. Seen in the first quick run (2026-11-23):
  see LOGBOOK session 45 "faster verification tooling". Options: better
  presets that reach the rare blocks early (then quick mode stops
  early), or `Spec.quick_cases` with the evidence named at the spec.
- **Split `game/enums.h` by category** (user request, 2026-11-23; not
  done in the tooling session): one header per category under
  `game/enums/` (jobs, settler types, buildings, goods, races, spells,
  piles, ...), `enums.h` kept as an umbrella include for now, each file
  including only what it uses. With the emulator selecting by compiled
  code this mainly helps build times.
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

## Spells: whole-game evidence

The dispatcher CGameData::ExecuteSpell 0x5480B0 and all spell handlers
of Volker\GodlyEvents.cpp are verified (spells.cpp, spells_land.cpp,
spell_units.cpp, spell_goods.cpp; packages S1/S2 of session 46). No
recorded game casts spells yet (all spell counts 0 in the replays and
savegames): a recording with spells (temples, mana) would give whole-game
evidence. Note: SpellConvertGoods (0x549BD0) hangs the original game when
cast within ~8 tiles of the map edge (kept).

## Known differences to clean up

- Some replacements leak temporaries on fatal error paths because they have
  no exception-unwind frames (std::string `_Xlen`/`_Xran` paths, the throw
  helpers 0x5ffcc0 / ThrowOutOfMemory, ...). Harmless today (the game
  ends), but worth fixing for the standalone build. List: inventory notes
  containing "Known difference".

## Typed access leftovers

- **Class layouts left after struct pass S2 (session 45).** The object
  `RawAt`s are gone (two test-only ones in string_members.cpp are marked
  `raw-ok`). Left, by value:
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
