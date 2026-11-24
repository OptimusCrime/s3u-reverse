# Readability and code style

Part of the backlog (index: `TODO.md`). Each item: what, why, and
where to start. Remove an item when it is done (mention it in the
LOGBOOK session entry).

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
