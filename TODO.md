# TODO (backlog)

Things we should do at some point, but not right now. The current plan
and its order are in `LOGBOOK.md` ("Current focus"); larger directions are
in its "Roadmap" section; unexplained behaviour goes under "Open
questions". Add an item here whenever something is postponed, and remove
it when it's done (mention it in the LOGBOOK session entry).

Each item: what, why, and where to start.

## Readability pass (docs/coding_style.md)

- **SettlerJob names from UpdateSettlers (session 45)** for
  `game/enums/job_ids.h` / `re/enums.md`, once the readability rollout no
  longer changes the job enum
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
  `kObjectFirstTree`..., `kResourceCoal`...; move them to
  `game/enums/terrain.h` (and new `objects.h`/`resources.h` headers there;
  Tile::terrain/object/resources, with re/enums.md evidence).
- **Typed fields:** once every user of `Settler::job`, `Settler::art`,
  `Building::art`, `Pile::good` or the race fields is converted, give the
  field its enum type in `layout.h` and drop its accessor from
  `game/typed_fields.h`.
- **BuildingTypeAllowedForRace** (player_queries.cpp) does not fit the
  building numbering of game/enums/buildings.h; find what its argument really is (the
  type numbers stay decimal numbers there until then).
- **Map effect record fields** (layout.h `MapEffect`): +0x06, +0x2c
  (probably a graphics id), +0x32, +0x34 (the value the "stationary"
  make function 0x543f70 sets) are still `unkNN`; the spawn functions in
  map_effects.cpp name the arguments by offset only.
- **Lint for enum narrowing (session 48):** `static_cast<BuildingType>(x)`
  of a 16-bit value truncates to the uint8 enum (A2's SiteKindOf: type 270
  became Vineyard; caught by its test). Flag casts of wider integers to
  8-bit enums in `typed_access_lint.py` or a new check; switch on the word
  with `case Value(...)` instead.
- **Lint** for hex literals in comparisons and magic numbers in
  job/type/goods comparisons (coding_style.md "Checks").

## Computer players (re/computer_players.md)

- **Package A2: small READY helpers** of the AI range (`python
  tools/frontier.py --range 54cbd0 59fcd0`). Done in session 48: the 25
  T_ECONOMY_AGENT conditions 0x568250..0x569d20 and the map queries
  0x599510..0x5999e0, 0x58ecb0, 0x58ed30, 0x58ed70, 0x59dbb0, 0x59dc70,
  0x593140, and the site queries 0x571d00..0x5720c0, 0x5930f0, 0x593f50.
  Left: 0x591e00 (sets up g_aiGroupOrders, vectors at 0x7e3690), 0x572420
  (fills AiPlayerData::building_counts), the larger economy conditions
  0x567e00, 0x568420, 0x568bc0, 0x568d80, 0x568f40, the snapshot 0x567910
  (needs 0x59dcc0), and the other small READY ones of the frontier (e.g.
  0x597110..0x597260, 0x59cf20, 0x58f8c0, 0x58f940).
  The ~60 identical 104-instruction SEH functions on the READY list are
  probably static std::string-array initializers (check before counting
  them as work).
- **Package A3 follow-ups** (A3 done in session 48): rename the
  deletion-queue adders in ai_helpers.cpp / ai_small_helpers.cpp, which
  add a note id to CComputerPlayer::doomed_notes: `AI::List32Add`
  0x551910 -> `CComputerPlayer::DoomNote`, `AI::List32AddValue_551780`
  -> `CComputerPlayer::DoomNoteId`, `AI::List32AddWord8_551760` ->
  `CComputerPlayer::DoomNoteOf` (the note's id); left out of A3 because
  package A2 worked in those files (both merged now). Also: who creates
  the AiPlayerData nodes; the constructor part 0x550a30 (calls AddAgent)
  is next to 0x550f80.
- **Package A4: command senders** 0x58e220..0x58ec33 and
  0x590990..0x590d86: need `MessageQueue::Push` (pass-through hook) and
  `GameSetup_sub_4318a0` (0x7a7a18 +0x7c flag; name it) as probed callees.
- **Package A5: Cevaland** (land evaluation) and **A6: the agents' run
  functions** (slot 8), smallest first; then 0x550f80 and the
  constructor 0x550900 / 0x550a30.
- **Open:** who creates the AiPlayerData nodes (one per
  economy area; list +0x200 is the deletion queue, session 48); `AiList50Owner` in
  ai_layout.h is the same object as ComputerPlayer (merge them when
  ai_helpers.cpp is next touched); a world-field list of the AI range with
  `tools/world_fields.py`.
- **Corrections from the naming pass (session 50,
  `re/computer_players_naming.md`)**, each touching code or layouts:
  unify `AI::EvaLand::*` /
  `AI::CEvaLand_*` under `AI::CEvaLand::`; `AI::BlockCentreByte8` reads
  the centre tile's owner. Layouts: `g_aiGroupOrders` is 0x20f8 bytes
  (not 0x898), `g_aiArmyState` only 0x51c (its fields from +0x630 belong
  to the census at 0x7e5ef8; savegame chunks 0x15..0x17);
  `g_aiUnk7e3134` is the CEvaLand object (`g_aiLandEvaluation`);
  CComputerPlayer +0x774 is that pointer, not "settlement_lists";
  AiPlayerData offsets are list-node offsets (value from +8).
  re/computer_players.md: "the AI never writes the world directly" is
  wrong (the military code edits player_lists, ProcessWorldEvents 0x590da0
  consumes the event ring); the "list changed" flag is +0xf0 + 4*p; the AI
  code ends at 0x59e0a0 (0x59e6f0.. is Credits code; also in
  re/source_files.csv).
- **Original bugs to keep** (found in the naming pass): 0x58af60 always
  returns 0 (best distance starts at 0); `AI::FewUnownedTilesNear`
  0x59c2c0 always returns 1; `AI::CastBattleSpells` 0x5953b0 pairs the
  Amazons' mana checks with the wrong casters.

## File formats (re/file_formats.md)

- **Corrections from the naming pass (session 50):** `g_strengthDivisor`
  0x7acfa4 is the number of active players (`g_activePlayerCount`;
  UpdateEnvironment's 0x600 / (n + 4)); `g_playerTallies` 0x7acfd4 is +0x2c
  into a 20 x 0x44-byte player-info table at 0x7acfa8 (re-base the global
  with the real record); the world regions' original names from the save
  labels ("timer" = effects +0x36700, "warmap" = presence_a/b,
  "activesettlers" = player_lists, "diggertab" = site_cache +
  footprint_scratch, which is saved, so not scratch; "sectors", "events",
  "messages"): rename in layout.h / re/world_layout.md. Dirk/Pathing.cpp's
  range in re/source_files.csv probably wrongly includes the CMapFile
  stream 0x4a55e0..0x4a5c80.
- **Reimplementation order:** the CMapFile container (checksum, XOR key,
  chunk table) is self-contained and testable against every map and save
  file; then `CGameData::LoadMap` 0x4fd540 and
  `CLogicThread::LoadSaveGame` 0x508fa0 (mostly memcpy of world regions).
- **Graphics (re/graphics_formats.md, session 50):** the game's graphics
  are the memory-mapped `GFX\siedler3_NN.<pixel format>.dat` files read by
  CGraphicsData (8 tables, RLE sprites, palettes by brightness); the draw
  spans 0x5f4980..0x5f5910 pick by brightness. Reimplement the loader and
  the sprite decoding first (verifiable by comparing decoded bytes).
- **Function-start fixes found in the naming pass (session 50):**
  missing starts (add as `manual` rows in re/extra_functions.tsv with
  evidence): 0x5a0920, 0x5a0950, 0x5a1a60, 0x5a42c0, 0x5a4610, 0x5a4970
  (re/picture_code.md) and 13 draw spans plus the halve span 0x5fd0c0
  (re/graphics_formats.md). False starts (keep out of the counts, e.g. a
  `notfunction` kind): the 26 `ProtectCD_RunEmbeddedCheck_junk_*` rows
  inside the obfuscated 0x5b2f70, the catch-continuation points 0x5fe948,
  0x5fe9d8, 0x5feb05 inside CSoundData::Open, the `_tail` rows 0x5f6c3d,
  0x5f65f2, 0x5f674b, 0x5fd902, and 0x551637 inside DeactivateNote.
  The source-file range `Credits\cpp_exo_text__diverses.h`
  0x5a0d20..0x5b50ad really ends at the credits code (~0x5a4220).
  More from the DirectX/Win32 rounds: missing starts 0x5b7810 (the
  EnumDisplayModes callback), the six surface-copy candidates around
  0x5c3a40..0x5c40f0 (re/directx_framework.md), 0x60ec20, 0x610380,
  0x610040, 0x6101a0, 0x615bd0, 0x615bb0, 0x618a30
  (re/win32_framework.md); false starts 0x5c45e3 (after a catch of
  0x5c4520), 0x615601 (`CCDAudio::Init_tail`). Source-file ranges
  around DirectX\ are shifted by about one file (netsession.cpp's last
  rows are Ddraw.cpp, the DirectSound wrapper sits in "Dplay.cpp",
  0x5c3740..0x5c4140 in "Dsound.cpp" is CPU detection and the copy
  benchmark); ExceptionHandler.cpp 0x61b990..0x61c4b8 is not Detach.cpp;
  0x615520/0x615601 belong to cdaudio.cpp. Fix re/source_files.csv's
  boundaries (`source_files.py scan`) or add overrides.
- **Verified names found wrong in the DirectX/Win32/UI rounds (session
  50)**, each a code rename: `GuidNames_Construct_5b6790` is the
  `CDirectDrawDevice` (GUID, description, name) constructor;
  `CWin32Waitable::WaitForXYZ` 0x463100 runs on a wait set
  (`CWin32WaitableBag`; touches the `--fast` hook in fast.cpp, so bump
  S3RE_ORIGINAL_TOOLING_VERSION); framework_layout.h CMessageQueue +4 is
  the window handle, not `owner`; the CSimpleSurface/CSurface fields in
  re/directx_framework.md. CTextException::vf01 0x618250 is a
  linker-folded `mov eax,[ecx+4]; ret` with 132 callers (a neutral name).
- **Verified UI names found misleading (session 50, re/ui_classes.md)**,
  each a code rename: `CControl::OnMessage10` -> `OnLButtonDown`;
  `CScreenElement::ParentQuery78` -> `GetScreen`, `IsQuery78Zero` ->
  `IsOffScreen`; `CScreenElement::Enable` (slot 26) is the parent-changed
  handler and slot 27 "OnEnable" means shown on / removed from a screen
  (real enabling is `CControl::SetEnabled` 0x418810); the
  `Dialog::Global7a823c`/`7a8238`/`Table7a809c`/`Table7a8064`/
  `Global7a8240`/`CopyGlobals7a8020` globals are the GDI surface,
  CGraphicsData, font table, colour table, pen and pixel format (filled by
  `Dialog::Init` 0x416190); screen_element_layout.h CControl
  `tooltip_owner` (+0x08) is a functor called every timer tick;
  `UI_WideEos_428880` -> `std::wstring::_Eos`; `Menus::CDrawThread::
  OnDialogResult` 0x494990 handles only state 0 (`OnMainMenuResult`).
  Missing function starts of the UI rounds (dialog handlers merged into
  their neighbours, e.g. 0x47df70, 0x47efd0, 0x4882e0, 0x48d930,
  0x48d9e0; 0x41e410, 0x421260, 0x4271e0) and false starts (0x419b5f,
  0x427cab, 0x49859c) are listed in re/ui_classes.md; the rows 0x47ecc0,
  0x482d00, 0x486ea0, 0x48d620 span more than one function. 0x42c2d0..
  0x42ca70 belong to Draw.cpp, not dialog.cpp.

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
  have tests only. The user's full game (fullgame-20261124-0055-seed10779,
  session 49) adds ids 0x06, 0x07, 0x08, 0x0e, 0x14, 0x15, 0x18, 0xf1 and
  0xf5 (identical to the original over 138,402 ticks); ExecuteCommands is
  one hook, so per-id counts would need a temporary counter.
- **UpdateSettlers handlers with tests only (session 45).** The
  whole-game checks (two replays, three saves, a random map) never reach
  these UpdateSettlers jobs; the user's full-game recording
  (fullgame-20261124-0055-seed10779, session 49) is identical to the
  original and recorded; which of these jobs it reached is unknown until
  a per-job hit count exists. A per-job hit count needs a temporary counter
  in update_settlers.cpp's dispatch. Jobs (hex):
  building 0x38, 0x6a; drinks 0xe2..0x127 (rice farmer, winegrower,
  distiller/brewer/powder maker, charcoal burner); food 0x6c..0x73,
  0xce..0xd3, 0x131, 0x132; metal 0x32..0x34, 0x3a..0x41, 0x52..0x54,
  0x56, 0x5c..0x5f, 0xd6..0xe1, 0x6b, 0x7d..0x82, 0xaf, 0x128..0x139,
  0x186..0x18f; special 0x134, 0x136, 0x13a..0x14f, 0x150..0x160,
  0x164..0x171, 0x17c..0x185, 0x190..0x194; units 0xae and the walking
  jobs 0xe5, 0xe8, 0xed, 0xf1, 0xfe, 0x102, 0x133, 0x135, 0x140, 0x141,
  0x147..0x14a, 0x165, 0x168; the 20 no-op jobs.
- **FakeClock after a crashed test (session 48):** CalleeStubs and
  ScopedJump are restored after a crash now (`LivePatches()`,
  `RestoreLeftoverPatches()` in test/callee_stub.h), but `FakeClock`
  (screen_element.cpp, patches the timeGetTime import) is not. Harmless so
  far (later tests keep running); register it in `LivePatches()` too.

- **Emulator coverage of `CGameData::GenerateRandomMap`:** 338/415 blocks
  in full Debug runs (1 quick case per batch since 2026-11-23); the rest
  needs maps of 130+ tiles. Its real evidence is the in-process
  whole-world tests and the random-map whole-game checks. Since c3f29be
  the Release build works for the emulator (`run.py --build Release`,
  ~5x faster on FindPath); time GenerateRandomMap there and decide whether
  a bounded larger-map case is worth it, or exempt the rest with reasons
  so `--require-coverage` stops failing on it.
- **`--changed` reselected 200 specs in a fresh worktree (session 48)**
  although only comments and tests had changed (backlog-cleanups agent,
  wip/cleanups). Probably the worktree's own build gives different
  fingerprints than the passes recorded from main's build (absolute
  paths, build order?). Compare the fingerprints of one such spec in
  both builds (`tools/emutest/changed.py`).
- **`--changed` reselects 5 slow specs every time (session 48):**
  CheckGameEnd, FollowRightWall, UpdateSoldier, UpdateSettlers,
  ExecuteCommands stop at the quick budget without full coverage, so no
  pass is recorded and every worktree reruns them (seen by all three
  agents of session 48). A `--full` run of them on main records passes;
  better presets or `quick_cases` would stop it for good.
- **Specs over the quick-mode budget (60 s) before full coverage:** they
  get no recorded pass from quick runs, so `--changed` reselects them
  until a `--full` run passes. Seen in the first quick run (2026-11-23):
  see LOGBOOK session 45 "faster verification tooling". Options: better
  presets that reach the rare blocks early (then quick mode stops
  early), or `Spec.quick_cases` with the evidence named at the spec.
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

- **Live game vs its replay: header and rest hashes differ (session 49).**
  `compare_traces.py` of the user's live trace
  (`recordings/fullgame-20261124-0055-seed10779.s3rec.tsv`) against the
  original's replay of it: RNG state and map hash identical on all
  138,402 ticks, but the header hash differs from tick 192 (right after
  the player's first commands) and the rest hash from tick 4,992. Most
  likely world fields written by the live UI (mouse, menus) that the
  replay doesn't repeat and the trace doesn't skip yet; the replays of
  the original and ours are identical, so the verification evidence
  stands. To find them, the live game needs world dumps: let
  `play_game.py` pass `--dump-tick` at a few ticks (e.g. 200,5000;
  ~22 MB each), replay with the same `--dump-tick`, then
  `tools/diff_world_dumps.py`, check the writers and add skips
  (trace version bump).

## Spells: whole-game evidence

The dispatcher CGameData::ExecuteSpell 0x5480B0 and all spell handlers
of Volker\GodlyEvents.cpp are verified (spells.cpp, spells_land.cpp,
spell_units.cpp, spell_goods.cpp; packages S1/S2 of session 46). The
user's full game (fullgame-20261124-0055-seed10779, session 49) reaches
ExecuteSpell, HasManaForSpell, PayForSpell, SpawnSpellEffects,
SpellRevealSpot and SpellTransformLand; the other spell handlers still
have tests only. Note: SpellConvertGoods (0x549BD0) hangs the original game when
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
