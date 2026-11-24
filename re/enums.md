# Category values: settler jobs and types, buildings, goods, races

The named values of `src/s3re/game/enums/` (one header per category,
split from `game/enums.h` on 2026-11-23: `races.h`, `settler_types.h`
(SettlerType, SettlerClass), `buildings.h`, `goods.h`, `piles.h`,
`spells.h`, `job_ids.h` (SettlerJob), `commands.h` (NetMessage,
CommandId), `terrain.h`, `directions.h`; `game/enums.h` includes them
all), with the evidence for each
name and how sure we are. Made in session 45 (2026-11-22, readability
pilot). Values are decimal, with hex where the disassembly shows it.

## Method

1. **The binary first.** A name is given when S3.EXE shows what a value
   means: a German DebugLog string next to it, what the code does with it,
   or the type tables in `.rdata`:
   - `0x658f64` settler class per type (int32 x 48),
   - `0x6590e4` start health per type,
   - `0x6591a4` walking speed per type,
   - `0x659264` consumer building type per consumer slot (51 entries).
2. **Public Settlers 3 tools as hints.** The open-source remake
   *jsettlers* (`jsettlers.logic/.../map/loading/original/data/`
   `EOriginalMapBuildingType`, `EOriginalMapSettlersType`,
   `EOriginalMapStackType`) lists building, settler and goods ids of the
   original map format. Its lists were used as candidate names only; every
   name kept here agrees with the binary's behaviour (below). Where we found
   no behaviour to check a value against, the name still comes from that
   list and is marked **list** (medium).
3. **Unknown values** get a placeholder with their decimal number
   (`SettlerJob::Job390`, `SettlerType::Type20`, `Good::Good31`) so they can
   be grepped and renamed later.

Confidence: **high** = a string or several independent behaviours agree;
**medium** = one behaviour or the list plus a consistent behaviour;
**low** = a plausible reading of one use; **placeholder** = not known.

## Cross-checks that fix the numbering

UpdateEconomy (`update_economy.cpp`, 0x4e05e0) requests for each building
type a worker of one settler type and input goods. With the list's ids
every pair fits (worker type / building / inputs):

| building | worker | inputs (goods) |
|---|---|---|
| 2 Woodcutter | 3 Woodcutter | - |
| 3 Stonecutter | 4 Stonecutter | - |
| 4 Sawmill | 6 Sawmiller | 3 Log |
| 5 Forester | 7 Forester | - |
| 7, 8, 9, 37 mines (coal, gold, iron, gems) | 10 Miner | 8 Fish, 24 Meat, 4 Bread |
| 29 Sulfur mine | 10 Miner | 27 Rice, 4 Bread |
| 56 Amazon sulfur mine | 10 Miner | 35 Honey, 4 Bread |
| 10 Gold smelter / 11 Iron smelter | 9 Smelter | 5 Coal + 6 Gold ore / 7 Iron ore |
| 12 Toolsmith / 13 Weaponsmith | 11 Smith | 5 Coal, 11 Iron (products dropped on 7 / 3 tiles) |
| 14 Vineyard | 29 Winegrower | - |
| 17 Mill | 12 Miller | 9 Grain |
| 19 Barracks | carriers become recruits | 18 Sword, 19 Bow, 20 Spear |
| 20 Bakery | 13 Baker | 22 Flour, 26 Water |
| 21 Slaughterhouse | 14 Butcher | 23 Pig |
| 22 Distillery | 25 Distiller | 27 Rice, 5 Coal |
| 23 Pig farm / 49 Donkey farm | 28 Pig farmer | 9 Grain, 26 Water |
| 24 Farm / 25 Fishery / 30 Waterworks | 16 / 17 / 18 | - |
| 31 / 47 / 48 / 50 workshops | 11 Smith | 1 Board, 11 Iron, + 2 Stone / 3 Log / 30 Gunpowder / 10 Gold |
| 32 Shipyard | 19 Shipyard worker | 1 Board, 11 Iron |
| 35 Hospital | 21 Healer | - |
| 36 Rice farm | 38 Rice farmer | - |
| 38 Brewery | 26 Brewer | 9 Grain, 26 Water |
| 39 Charcoal burner | 24 Charcoal burner | 1 Board |
| 40 Powder maker | 27 Powder maker | 5 Coal, 25 Sulfur |
| 51 Apiary | 45 Beekeeper | - |
| 52 Meadery | 46 Mead maker | 26 Water, 35 Honey |
| 53 Laboratory | 47 Alchemist | 28 Gems, 25 Sulfur |

More checks:
- Military buildings 15, 16, 18 have 250, 500, 1000 hit points (S3.EXE
  0x659664): small tower, large tower, castle.
- Residences 26, 27, 28 send out 1, 3, 10 settlers (BuildingDone) and raise
  the population limit by 10, 30, 100.
- Big temples 41, 43, 45, 55 create a settler of type 36 (Mage); small
  temples 42, 44, 46, 54 consume 29 Beer, 21 Wine, 33 Rice liquor (twice the
  mana), 34 Mead: one drink per race (Egyptian, Roman, Asian, Amazon).
- The settler class table: types 5, 32, 35 are class 1, 8, 30, 33 class 2,
  15, 31, 34 class 3, with start health 100, 120, 150: three soldier kinds
  in three levels (swordsmen, bowmen, pikemen). Class 2 soldiers take a
  slot in the building (LeaveBuildingSlot when they leave): bowmen.
- Type 41 stands on seven tiles and has 250 health: the catapult. Types 43
  and 44 have 250 health; 43 carries cargo (cargo fields, "Schiff laedt
  ab"), 44 carries settlers (passengers, BoardShip): trade ship and ferry.
- Coal's consumer slots 1..6 name buildings 11, 10, 13, 12, 22, 40 (the
  smelters, smiths, distillery and powder maker).
- Race 3 = Amazons: a random race is `Random() % 4` with the Amazons,
  `% 3` without (CLogicThread::OnStart 0x53cf96). 0..2 = Roman, Egyptian,
  Asian from the list and the temples' drinks (by building type).

## Corrections of earlier guesses

- **Harbour and marketplace were swapped.** `update_economy.cpp` called
  0x21 "market-like" and 0x22 "port-like". 0x21 (33) needs a landing
  direction (`Building::dock_direction`, set by TakeBuildingExtraTiles for
  the shipyard and 0x21), so it is the **harbour**; 0x22 (34) sends settlers
  (donkeys) to its ring piles: the **marketplace**.
- **The "water buildings"** 0x1f, 0x2f, 0x30, 0x32 (`BuildingWaterBytes` in
  layout.h) are the **catapult, ballista and cannon workshops and the large
  gong** (worker: smith; "Gong bauen" at 0x32). Their layout view is now
  `Building::workshop` (`BuildingWorkshopBytes`, was `water` /
  `BuildingWaterBytes`, renamed 2026-11-23); the shipyard 0x20 uses it too.
- 0xc/0xd "tile drop buildings" are the **tool smith and weapon smith**;
  0x13 is the **barracks**.
- **Settler type 0x29 is the catapult** (session 45 rollout): the
  "SettlerType29" in `FindNearbySettlerType29` / `IsFreeOfSettlerType29Nearby`
  meant catapult; the seven extra tiles of the war machine workshops
  (TakeBuildingExtraTiles) are where the machine stands. `UnblockType12BuildingTiles`
  works on **castles** (type 18). Terrain 0x16, which `SetTerrain16Patch`
  makes (jobs 145, 230, 247 of the farmer, rice farmer and winegrower), is a
  **field**, and UpdateEnvironment turns a field without growing objects (0x55..0x6f) around back
  into grass. `IsFreeCastleSite` checks the small tower (type 15). Renamed
  2026-11-23: `FindNearbyOwnCatapult`, `IsFreeOfCatapultsNearby`,
  `UnblockCastleTiles`, `MakeFieldTile`, `IsFreeSmallTowerSite`.
- **The "waiting lists" of SendNearestSettlerToPile/-ToBuilding**
  (settler_queries.cpp, player record +0x6e/+0x76) are the idle lists of
  settler types 39 and 43 (`Player::settler_lists` is indexed by type):
  the first sends a **donkey** to a pile (one or two units: job 307, the
  `second_unit` byte), the second a **trade ship** to the water in front of
  a harbour (job 327). They were thought to be carriers and builders.
- **The miner jobs** (session 45 rollout): each mine type has four jobs,
  eat (85/86/87/216/217), dig (88/92/96/218/222), found nothing
  (89/93/97/219/223) and found (90/94/98/220/224), named per mine.
- **Job names of the session 45 notes that the handlers contradicted**
  (session 51): 314 is `ShipyardGoIn` (ArriveAtWork's job for the
  shipyard worker, counted in by 0x4f6e84), not "ShipyardUnitDone"; 336
  is `HealerGoIn`, not a "Done" job; 76..79 are a carrier moving goods
  from a source pile to a target pile (a storehouse's pile or a trade
  building's; DispatchPileCarrier, FetchGoodsToTradeBuilding), not
  "trade pile" jobs (handlers renamed `CarrierAtSourcePile`,
  `CarrierAtTargetPile`); 307/308 are donkeys (a comment in
  building_crush.cpp still said "probably ships"); the farmer's 0x8f/0x95
  leave the building (`FarmerLeaveToSow`/`...ToHarvest`), the walks are
  144/156; 142 is set on each attack (`UnitFighting`), its handler ends
  the fight; 0x6c..0x73 follow the `<Worker>GoIn` scheme.

## Open questions

- `BuildingTypeAllowedForRace` (player_queries.cpp) does not fit this
  numbering (it allows 0x15 only for race 0 and 0x19 only for races 2, 3);
  its argument is probably another id (e.g. a build-menu index).
- `SettlerType::Type20`, `Type37` (class 99), `Type42` (class 41, health 0),
  `Good::Good31`: no use seen yet.

## Race (`Race`, int32)

| value | name | confidence | evidence |
|---|---|---|---|
| 0 | Roman | medium | list; small temple 44 (Roman) offers wine |
| 1 | Egyptian | medium | list; sphinx 42 offers beer; soldier strength counts gems (28) for race 1 |
| 2 | Asian | medium | list; small pagoda 46 offers rice liquor |
| 3 | Amazon | high | random race `% 4` only with the Amazons (0x53cf96); weaker soldiers (SettlerStartHealth -20) |

## Settler types (`SettlerType`, uint8; Settler::art)

| value | hex | name | class | confidence | evidence |
|---|---|---|---|---|---|
| 0 | 0x00 | Carrier | 0 | high | carries goods (DistributeGoods sends type 0); recruits |
| 1 | 0x01 | Digger | 0 | high | digger slots, "digger!" |
| 2 | 0x02 | Builder | 0 | high | builder slots, BuildingDone |
| 3 | 0x03 | Woodcutter | 0 | high | worker of building 2 |
| 4 | 0x04 | Stonecutter | 0 | high | worker of building 3 |
| 5 | 0x05 | Swordsman1 | 1 | high | class 1, health 100 |
| 6 | 0x06 | Sawmiller | 0 | high | worker of building 4 |
| 7 | 0x07 | Forester | 0 | high | worker of building 5 |
| 8 | 0x08 | Bowman1 | 2 | high | class 2, health 100 |
| 9 | 0x09 | Smelter | 0 | high | worker of buildings 10, 11 |
| 10 | 0x0a | Miner | 0 | high | worker of the mines; MinerJob* |
| 11 | 0x0b | Smith | 0 | high | worker of 12, 13 and the workshops |
| 12 | 0x0c | Miller | 0 | high | building 17 |
| 13 | 0x0d | Baker | 0 | high | building 20 |
| 14 | 0x0e | Butcher | 0 | high | building 21 |
| 15 | 0x0f | Pikeman1 | 3 | high | class 3, health 100 |
| 16 | 0x10 | Farmer | 0 | high | building 24 |
| 17 | 0x11 | Fisher | 0 | high | building 25 |
| 18 | 0x12 | Waterworker | 0 | high | building 30 |
| 19 | 0x13 | ShipyardWorker | 0 | high | building 32 |
| 20 | 0x14 | Type20 | 0 | placeholder | |
| 21 | 0x15 | Healer | 0 | high | building 35 |
| 22 | 0x16 | Geologist | 20 | medium | list; works on mountain tiles (WalkSettler) |
| 23 | 0x17 | Thief | 10 | high | jobs with "Dieb" strings; slower with a stolen good |
| 24 | 0x18 | CharcoalBurner | 0 | high | building 39 |
| 25 | 0x19 | Distiller | 0 | high | building 22 |
| 26 | 0x1a | Brewer | 0 | high | building 38 |
| 27 | 0x1b | PowderMaker | 0 | high | building 40 |
| 28 | 0x1c | PigFarmer | 0 | high | buildings 23 and 49 |
| 29 | 0x1d | Winegrower | 0 | high | building 14 |
| 30 | 0x1e | Bowman2 | 2 | high | class 2, health 120 |
| 31 | 0x1f | Pikeman2 | 3 | high | class 3, health 120 |
| 32 | 0x20 | Swordsman2 | 1 | high | class 1, health 120 |
| 33 | 0x21 | Bowman3 | 2 | high | class 2, health 150 |
| 34 | 0x22 | Pikeman3 | 3 | high | class 3, health 150 |
| 35 | 0x23 | Swordsman3 | 1 | high | class 1, health 150 |
| 36 | 0x24 | Mage | 22 | medium | list; made by the big temples |
| 37 | 0x25 | Type37 | 99 | placeholder | a military building's guard: UpdateSoldier creates one (job 173, health 1) as Building::guard of a captured tower; a valid target (class 99) |
| 38 | 0x26 | RiceFarmer | 0 | high | building 36 |
| 39 | 0x27 | Donkey | 30 | medium | list |
| 40 | 0x28 | Pioneer | 21 | medium | list; claims tiles (job PioneerWork, SetOwnerOfFreeTile) |
| 41 | 0x29 | Catapult | 40 | high | seven tiles, health 250, "Angriff Katapult" |
| 42 | 0x2a | Type42 | 41 | placeholder | health 0; the six parts of a seven-tile unit (CreateSevenTileUnit, job 304); hits on a part go to the catapult nearby (UpdateSoldier) |
| 43 | 0x2b | TradeShip | 50 | high | cargo, "Schiff laedt ab" |
| 44 | 0x2c | Ferry | 51 | high | passengers (BoardShip) |
| 45 | 0x2d | Beekeeper | 0 | high | building 51 |
| 46 | 0x2e | MeadMaker | 0 | high | building 52 |
| 47 | 0x2f | Alchemist | 0 | high | building 53 |

## Building types (`BuildingType`, uint8; Building::art)

| value | hex | name | confidence | evidence |
|---|---|---|---|---|
| 1 | 0x01 | Storehouse | list | no economy handler |
| 2..5 | | Woodcutter, Stonecutter, Sawmill, Forester | high | workers and inputs (table above) |
| 6 | 0x06 | LookoutTower | list | no economy handler |
| 7, 8, 9 | | CoalMine, GoldMine, IronMine | high | miner; MinerJobEnter per mine (jobs 82..84) |
| 10, 11 | 0x0a, 0x0b | GoldSmelter, IronSmelter | high | ore inputs |
| 12, 13 | 0x0c, 0x0d | Toolsmith, Weaponsmith | high | coal + iron; consumer slots |
| 14 | 0x0e | Vineyard | high | winegrower |
| 15, 16, 18 | 0x0f, 0x10, 0x12 | SmallTower, LargeTower, Castle | high | hit points 250/500/1000, garrison |
| 17 | 0x11 | Mill | high | |
| 19 | 0x13 | Barracks | high | weapons in, recruits out |
| 20, 21 | | Bakery, Slaughterhouse | high | |
| 22 | 0x16 | Distillery | high | rice |
| 23, 24, 25 | | PigFarm, Farm, Fishery | high | |
| 26, 27, 28 | 0x1a..0x1c | Small/Medium/LargeResidence | high | births 1/3/10 |
| 29 | 0x1d | SulfurMine | high | miner, job 215 |
| 30 | 0x1e | Waterworks | high | |
| 31 | 0x1f | CatapultWorkshop | medium | list; smith, boards/iron/stone, state machine |
| 32 | 0x20 | Shipyard | high | shipyard worker, ship tables |
| 33 | 0x21 | Harbour | medium | landing direction (see corrections) |
| 34 | 0x22 | Marketplace | medium | ring piles fetched by settlers |
| 35 | 0x23 | Hospital | high | healer |
| 36 | 0x24 | RiceFarm | high | |
| 37 | 0x25 | GemMine | high | miner, job 214 |
| 38, 39, 40 | | Brewery, CharcoalBurner, PowderMaker | high | |
| 41, 43, 45, 55 | | Pyramid, LargeTemple, LargePagoda, AmazonLargeTemple | medium | make mages; names per race from the list |
| 42, 44, 46, 54 | | Sphinx, SmallTemple, SmallPagoda, AmazonSmallTemple | medium | offerings (drink per race) |
| 47, 48 | 0x2f, 0x30 | BallistaWorkshop, CannonWorkshop | medium | list; smith; log / gunpowder input |
| 49 | 0x31 | DonkeyFarm | medium | list; pig farmer, grain + water |
| 50 | 0x32 | LargeGong | high | "Gong bauen" |
| 51, 52, 53 | | Apiary, Meadery, Laboratory | high | |
| 56 | 0x38 | AmazonSulfurMine | high | miner, job 215 |

## Goods (`Good`, uint8; Pile::good)

| value | name | confidence | evidence |
|---|---|---|---|
| 0 | Nothing | high | a free pile record |
| 1 | Board | high | building material, pile 0 of a site |
| 2 | Stone | high | building material, pile 1 of a site |
| 3 | Log | high | sawmill input |
| 4 | Bread | high | mine food |
| 5 | Coal | high | consumer slots name smelters and smiths |
| 6 | GoldOre | high | gold smelter input |
| 7 | IronOre | high | iron smelter input |
| 8 | Fish | high | mine food |
| 9 | Grain | high | mill, farms, brewery input |
| 10 | Gold | medium | soldier strength; gong input |
| 11 | Iron | high | smiths, workshops |
| 12..17 | Shovel, Hammer, Axe, Pickaxe, Saw, FishingRod | high | tools (tool smith); RequestSettlerForBuilding 0x4dacd0: the tool a carrier fetches to become a worker: digger shovel, builder/smith/shipyard worker hammer, woodcutter/butcher axe, stonecutter/miner pickaxe, sawmiller saw, fisher rod; notifications 24..30 for a missing tool in this order |
| 18, 19, 20 | Sword, Bow, Spear | high | barracks inputs, one per soldier kind |
| 21 | Wine | high | Roman offering |
| 22 | Flour | high | bakery input |
| 23 | Pig | high | slaughterhouse input |
| 24 | Meat | high | mine food |
| 25 | Sulfur | high | powder maker, laboratory |
| 26 | Water | high | bakery, farms, brewery |
| 27 | Rice | high | distillery, Asian mine food |
| 28 | Gems | high | laboratory; Egyptian soldier strength |
| 29 | Beer | medium | Egyptian offering (list: "keg") |
| 30 | Gunpowder | high | cannon workshop |
| 31 | Good31 | placeholder | |
| 32 | Scythe | high | the farmer's tool (RequestSettlerForBuilding), notification 30 |
| 33 | RiceLiquor | high | Asian offering |
| 34 | Mead | high | Amazon offering |
| 35 | Honey | high | meadery input, Amazon mine food |
| 255 | Empty | high | building pile without a goods type |

## Pile kinds (`PileKind`, uint8; Pile::art)

Who a goods pile belongs to. Added in the readability rollout (group 3).

| value | name | confidence | evidence |
|---|---|---|---|
| 0 | Storehouse | high | BuildingDone creates a storehouse's six piles with kind 0 ("Storage.ProductionPile"); TakeGoodFromPile gives an emptied kind-0 pile back the goods type stored in its building's byte +0xe; DispatchPileCarrier carries goods to kinds 0..50 |
| 1..50 | (kLastStoragePileKind = 50) | medium | CreatePile (0x504420), StealGoodFromPile (0x505c20) and DispatchPileCarrier (0x5052c0) treat kinds up to 0x32 like 0; RecountPlayerGoods counts kinds 0..8; no creator of 1..50 known |
| 252 | TradeRing | high | BuildingDone: six piles of a marketplace/harbour (tables 0x6594a4, 0x659564), linked in a ring; `leaving` counts arrivals in its high nibble |
| 253 | Order | high | a building's input piles and a site's boards/stones (BuildingDone step 4, building_sites); StealGoodFromPile's DebugLog "OrderPile nix mehr frei!" |
| 254 | Dropped | high | DropGoodNear, CrushBuilding: goods put on the ground; removed when empty (TakeGoodFromPile) |
| 255 | Output | high | BuildingDone: CreatePile(door, good, 0, 0xff) for a producer's output pile (stored in +0xe); leaves its goods list while all units are reserved; UpdateEconomy's source search and FetchGoodsToTradeBuilding score these piles by distance only (were "free-standing"/"plain" piles there) |


## Spells (`Spell`, uint32; the spell argument of ExecuteSpell)

Each race has eight spells (int32[4][8] at 0x658d64, the slots of
`Player::spell_mana`): Romans 5, 3, 9, 10, 21, 18, 8, 11; Egyptians 2, 0,
19, 6, 22, 12, 13, 15; Asians 1, 4, 7, 23, 20, 16, 17, 14; Amazons 31, 30,
29, 28, 26, 27, 24, 25. Names come from ExecuteSpell's (0x5480b0) handlers
(reimplemented in spells.cpp) and the DebugLog text it logs before a call
("xxx about to execute EventConvertGoods", "...EventGodlyGoods",
"...EventGoodsBan", "...EventGoodsSummon", "...EventTreeCurse",
"...EventRevealMap"). The race of the variants follows from the lists
(each race has one ConvertGoods and one GodlyGoods spell).

| value | name | confidence | evidence |
|---|---|---|---|
| 0..3 | Spell0..Spell3 | placeholder | handler 0x548340 SpellTransformLand (spell passed on as the transform kind): by the terrain chains of NextTransformTerrain probably 0 dry swamp (0x50 -> grass 0x10), 1 melt snow (0x80 -> 0x20), 2 grass -> desert (0x40), 3 green the desert |
| 4 | Spell4 | placeholder | handler 0x548fb0 SpellCreateStones: stones (objects 0x73..0x7d) on free land |
| 5 | RevealSpot | high | SpellRevealSpot: a reveal point for 250 ticks |
| 6 | BurnTrees | high | SpellBurnTrees: trees catch fire |
| 7 | AddFish | high | SpellAddFish: fish in water tiles |
| 8 | HalveResources | high | SpellHalveResources |
| 9 | GrowTrees | high | SpellGrowTrees: young trees +3 stages |
| 10, 12, 16, 17, 24, 25 | Spell10 ... | placeholder | shared handler 0x549f40 |
| 11, 13, 14, 15 | Spell11 ... | placeholder | handlers 0x54a370, 0x54a7c0, 0x54a540, 0x54a1e0 |
| 18, 19, 20, 27 | ConvertGoods{Roman,Egyptian,Asian,Amazon} | high | "EventConvertGoods", handler 0x549bd0 |
| 21, 22, 23, 26 | GodlyGoods{Roman,Egyptian,Asian,Amazon} | high | "EventGodlyGoods", handler 0x5499b0 |
| 28 | GoodsBan | high | "EventGoodsBan", handler 0x54aa40 |
| 29 | GoodsSummon | high | "EventGoodsSummon", handler 0x54acd0 |
| 30 | TreeCurse | high | "EventTreeCurse", handler 0x5492a0 |
| 31 | RevealMap | high | "EventRevealMap", SpellRevealMap |

## Settler jobs (`SettlerJob`, uint16; Settler::job)

405 values (0..0x194), 375 named (session 51: 315 names from the
handlers); the 30 placeholders left are the no-op jobs and 390..399, whose
handlers call an empty function. "walking" = handled by WalkSettler
(IsWalkingJob's list); "no-op" = straight to the loop end. Handlers and job
chains: `re/update_settlers_jobs.csv` and `re/update_settlers.md`.

Naming scheme of the work chains (each name was checked against its
handler and the job that sets it):

- `<Worker>GoIn`: the first job that ArriveAtWork (4) gives a worker by its
  type (and building); it counts the worker in (Building +0x19) and leaves
  its tile.
- `<Worker>Start` / `...Check` / `...TakeInputs` / `...Search`: the
  round's first test (inputs there, output not full, building wants work).
- `<Worker>Take<Good>`, `...Work<N>` (animation only), `...Store<Good>` /
  `...Produce` / `...Done`: the steps of a round.
- `<Worker>...Again`: the same handler as the round's first step, set when
  the inputs for the next round are already there.
- `<Worker>EndRound`: leaves the work tile and goes back to the round's
  test. `<Worker>Rest` / `...BackIn`: back in the building after a trip
  outside, then the search again.
- `<Worker>Leave...` / `...Walk...` (walking) / `...At...` / `...Home` /
  `...GoInAgain`: trips outside.

| value | hex | name | confidence | handler | evidence |
|---|---|---|---|---|---|
| 0 | 0x0 | New | high | handler 0x4e971c | job 0 handler 0x4e971c: a new settler; sets job 1 (class 0) or 0x37 and links it into an idle list |
| 1 | 0x1 | Nothing | high | handler 0x4ea855 | DebugLog "JobNothing/StepAside auf economysector 0" (ResetSettlerToIdle, jobs 1 and 2); idle civilian |
| 2 | 0x2 | StepAside | high | walking, handler 0x4ead27 | same string; JobAfter01Or37 turns job 1 into 2 when a settler must make room; walking, back to 1 on arrival |
| 3 | 0x3 | WalkToWork | medium | walking, handler 0x4ead27 | walking, becomes 4 (ArriveAtWork) on arrival; set by 0x69 after a carrier took its tool |
| 4 | 0x4 | ArriveAtWork | high | handler 0x4ead38 | handler 0x4ead38: per settler type, moves the worker into its building and starts its work chain |
| 5 | 0x5 | DiggerDig | high | handler 0x4eb43b | start job of type 1 (digger) in job 4; reaches DebugLog "digger!" / "Digger trouble" |
| 6 | 0x6 | DiggerWalkToSpot | medium | walking, handler 0x4ead27 | walking, becomes 7 on arrival; jobs 5..9 hold a digger slot (ResetSettlerToIdle gives it back) |
| 7 | 0x7 | DiggerLevel | medium | handler 0x4eb8ae | next job of 6; animation and WearPath, then 5, 8 or 9; a digger-slot job |
| 8 | 0x8 | DiggerLower | high | handler 0x4ebab8 | DiggerLevel (7) sets it after lowering the tile one step (animation 1); handler 0x4ebab8 goes back to 7 |
| 9 | 0x9 | DiggerRaise | high | handler 0x4ebab8 | DiggerLevel (7) sets it after raising the tile one step; handler 0x4ebab8 goes back to 7 |
| 10 | 0xa | CarrierWalkToPile | high | walking, handler 0x4ead27 | DistributeGoods (UpdateEconomy) sends a carrier to a pile with job 10, the goods type and the target building |
| 11 | 0xb | CarrierAtPile | medium | handler 0x4ec00f | next job of 10 on arrival; animation, then 0xa9 |
| 12 | 0xc | CarrierWalkToBuilding | medium | walking, handler 0x4ead27 | walking with a carried good (ResetSettlerToIdle drops it); three times the foot traffic |
| 13 | 0xd | CarrierAtBuilding | medium | handler 0x4ec27e | next job of 0xc on arrival; animation, then 0x50 |
| 14 | 0xe | BuilderStart | high | handler 0x4ec58d | start job of type 2 (builder) in job 4; builder jobs 0xe/0xf/0x11/0x12 hold a builder slot (ResetSettlerToIdle) |
| 15 | 0xf | BuilderWalkToWait | high | walking, handler 0x4ead27 | walking; BuilderStart sends the builder to a free tile next to the door; on arrival BuilderStart again |
| 16 | 0x10 | SwordsmanEnterTower | high | handler 0x4ecc6a | ArriveAtWork for Swordsman1..3 (StartGuard); handler 0x4ecc6a counts class1_coming down and joins the garrison chain, then 23 |
| 17 | 0x11 | BuilderWalkToSpot | high | walking, handler 0x4ead27 | walking; BuilderStart sends the builder to the next free spot of the building layout; on arrival BuilderBuild |
| 18 | 0x12 | BuilderBuild | high | handler 0x4ec960 | handler 0x4ec960 calls BuildingDone; a builder job (builder slots) |
| 19 | 0x13 | WoodcutterGoIn | high | handler 0x4ece32 | ArriveAtWork for the woodcutter; handler 0x4ece32 counts it in (+0x19), then 24 |
| 20 | 0x14 | StonecutterGoIn | high | handler 0x4ecea1 | ArriveAtWork for the stonecutter; handler 0x4ecea1, then 25 |
| 21 | 0x15 | SawmillerGoIn | high | handler 0x4ecf3c | ArriveAtWork for the sawmiller; handler 0x4ecf3c, then 26 |
| 22 | 0x16 | ForesterGoIn | high | handler 0x4ecf66 | ArriveAtWork for the forester; handler 0x4ecf66, then 27 |
| 23 | 0x17 | SwordsmanInTower | high | handler 0x4f8b7c | next job of 16; also a class 1 tower conqueror (UpdateSoldier, GuardBuilding); handler 0x4f8b7c waits; a swordsman that captured a tower (UpdateSoldier) |
| 24 | 0x18 | WoodcutterSearch | high | handler 0x4ecf82 | handler 0x4ecf82: offset-table search for trees 0x44..0x52; notification 0x12 after 3000 entries |
| 25 | 0x19 | StonecutterSearch | high | handler 0x4ed213 | handler 0x4ed213: the same for stones 0x73..0x7e; notification 0x13 |
| 26 | 0x1a | SawmillerStart | high | handler 0x4edffb | handler 0x4edffb: a log in the input and room in the output, then 49 |
| 27 | 0x1b | ForesterSearch | high | handler 0x4edadd | handler 0x4edadd: an empty grass tile to plant on, then 44 |
| 28 | 0x1c | WoodcutterWalkToTree | high | walking, handler 0x4ead27 | walking; set by 30; on arrival 32 |
| 29 | 0x1d | StonecutterWalkToStone | high | walking, handler 0x4ead27 | walking; set by 31; on arrival 34 |
| 30 | 0x1e | WoodcutterLeave | high | handler 0x4ed4a4 | set by 24 when a tree is found; handler 0x4ed4a4 leaves the building (MoveSettlerToBuildingTileB), then 28 |
| 31 | 0x1f | StonecutterLeave | high | handler 0x4ed4c3 | set by 25; handler 0x4ed4c3, then 29 |
| 32 | 0x20 | WoodcutterAtTree | high | handler 0x4ed4e2 | handler 0x4ed4e2: starts cutting (33) if the tree is still there, else walks home (42) |
| 33 | 0x21 | WoodcutterCut | high | handler 0x4ed58e | handler 0x4ed58e: nine cutting steps, the tree falls; carries a log home (42) |
| 34 | 0x22 | StonecutterAtStone | high | handler 0x4ed53b | handler 0x4ed53b: starts cutting (35) or walks home (43) |
| 35 | 0x23 | StonecutterCut | high | handler 0x4ed7c7 | handler 0x4ed7c7: cuts a piece, a used-up stone frees its tiles; carries the stone home (43) |
| 36 | 0x24 | WoodcutterHome | high | handler 0x4ed92f | walking 42 arrives here; handler 0x4ed92f: with a log on to 37, else in (102) |
| 37 | 0x25 | WoodcutterPutLog | high | handler 0x4ed971 | handler 0x4ed971: the log onto the output pile (Good::Log), then 38 |
| 38 | 0x26 | WoodcutterGoInAgain | high | handler 0x4ed951 | handler 0x4ed951: into the building, then 102 |
| 39 | 0x27 | StonecutterHome | high | handler 0x4eda06 | walking 43 arrives here; handler 0x4eda06: with a stone on to 40, else 103 |
| 40 | 0x28 | StonecutterPutStone | high | handler 0x4eda48 | handler 0x4eda48: the stone onto the output pile (Good::Stone), then 41 |
| 41 | 0x29 | StonecutterGoInAgain | high | handler 0x4eda28 | handler 0x4eda28: into the building, then 103 |
| 42 | 0x2a | WoodcutterWalkHome | high | walking, handler 0x4ead27 | walking; set by 32 and 33; on arrival 36 |
| 43 | 0x2b | StonecutterWalkHome | high | walking, handler 0x4ead27 | walking; set by 34 and 35; on arrival 39 |
| 44 | 0x2c | ForesterLeave | high | handler 0x4eddbe | set by 27; handler 0x4eddbe leaves the building with a sapling, then 45 |
| 45 | 0x2d | ForesterWalkToSpot | high | walking, handler 0x4ead27 | walking; on arrival 46 |
| 46 | 0x2e | ForesterAtSpot | high | handler 0x4edde4 | handler 0x4edde4: checks the spot again, plants (47) or walks home (48) |
| 47 | 0x2f | ForesterPlant | high | handler 0x4edf93 | handler 0x4edf93: two steps, the sapling (object 0x54), walks home (48) |
| 48 | 0x30 | ForesterWalkHome | high | walking, handler 0x4ead27 | walking; on arrival into the building with 101 |
| 49 | 0x31 | SawmillerTakeLog | high | handler 0x4ee0c1 | set by 26; handler 0x4ee0c1 takes a log from input 0, then 50 |
| 50 | 0x32 | SawmillerBoardDone | high | handler 0x4ee114 | handler 0x4ee114: a board onto the output pile; again (52) or 51 |
| 51 | 0x33 | SawmillerEndRound | high | handler 0x4ee21f | handler 0x4ee21f leaves the work tile, back to 26 |
| 52 | 0x34 | SawmillerTakeLogAgain | high | handler 0x4ee0c1 | set by 50 when the next log is there; same handler as 49 |
| 53 | 0x35 | UnitWalk | medium | walking, handler 0x4eacfc | walking job of units in the player's idle lists (JobAfter01Or37: 0x37 -> 0x35, like 1 -> 2); on arrival the walk states of +0x1a (board a ship, shoot, thief) |
| 54 | 0x36 | SettlerDies | high | handler 0x4eef33 | handler 0x4eef33 counts the death per player (soldiers_lost / civilians_lost); UpdateSoldier sets it on a kill; killed by a soldier (UpdateSoldier, with animation 70) |
| 55 | 0x37 | UnitIdle | high | handler 0x4e98be | idle soldier or special unit (class != 0); job 0 sets it, it wanders (handler 0x4e98be) |
| 56 | 0x38 | GoldSmelterGoIn | high | handler 0x4ecf4a | ArriveAtWork for a smelter at a gold smelter; handler 0x4ecf4a, then 65 |
| 57 | 0x39 | Job57 | placeholder | no-op |  |
| 58 | 0x3a | GoldSmelterTakeOre | high | handler 0x4ee35a | set by 65; handler 0x4ee35a takes the ore (input 1), then 59 |
| 59 | 0x3b | GoldSmelterTakeCoal | high | handler 0x4ee3de | handler 0x4ee3de takes the coal (input 0), furnace effect (variant 1), then 60 |
| 60 | 0x3c | GoldSmelterWork1 | high | handler 0x4ee4cf | handler 0x4ee4cf: animation 76 only, then 61 |
| 61 | 0x3d | GoldSmelterWork2 | high | handler 0x4ee4e7 | handler 0x4ee4e7: animation 77 only, then 62 |
| 62 | 0x3e | GoldBarDone | high | handler 0x4ee4ff | handler 0x4ee4ff: Good::Gold onto the output pile, points tally; again (64) or 63 |
| 63 | 0x3f | GoldSmelterEndRound | high | handler 0x4ee698 | handler 0x4ee698 leaves the work tile, back to 65 |
| 64 | 0x40 | GoldSmelterTakeOreAgain | high | handler 0x4ee35a | set by 62 when the next inputs are there; same handler as 58 |
| 65 | 0x41 | GoldSmelterStart | high | handler 0x4ee252 | handler 0x4ee252: both inputs there and room in the output, then 58 |
| 66 | 0x42 | IronSmelterGoIn | high | handler 0x4ecf58 | ArriveAtWork for a smelter at an iron smelter; handler 0x4ecf58, then 75 |
| 67 | 0x43 | Job67 | placeholder | no-op |  |
| 68 | 0x44 | IronSmelterTakeOre | high | handler 0x4ee7d3 | set by 75; handler 0x4ee7d3, then 69 |
| 69 | 0x45 | IronSmelterTakeCoal | high | handler 0x4ee857 | handler 0x4ee857, furnace effect (variant 0), then 70 |
| 70 | 0x46 | IronSmelterWork1 | high | handler 0x4ee948 | handler 0x4ee948: animation 85 only, then 71 |
| 71 | 0x47 | IronSmelterWork2 | high | handler 0x4ee960 | handler 0x4ee960: animation 86 only, then 72 |
| 72 | 0x48 | IronBarDone | high | handler 0x4ee978 | handler 0x4ee978: Good::Iron onto the output pile; again (74) or 73 |
| 73 | 0x49 | IronSmelterEndRound | high | handler 0x4eeaca | handler 0x4eeaca, back to 75 |
| 74 | 0x4a | IronSmelterTakeOreAgain | high | handler 0x4ee7d3 | set by 72; same handler as 68 |
| 75 | 0x4b | IronSmelterStart | high | handler 0x4ee6cb | handler 0x4ee6cb, then 68 |
| 76 | 0x4c | CarrierWalkToSourcePile | high | walking, handler 0x4ead27 | walking; DispatchPileCarrier (to a storehouse pile) and FetchGoodsToTradeBuilding (to a trade building's pile) send a carrier to the source pile; on arrival 77; carrier fetching goods for a marketplace/harbour (FetchGoodsToTradeBuilding) |
| 77 | 0x4d | CarrierAtSourcePile | high | handler 0x4ec1f6 | handler 0x4ec1f6 takes the good and heads for the target pile (+0x22), then 78 (was thought to be a trade pile) |
| 78 | 0x4e | CarrierWalkToTargetPile | high | walking, handler 0x4ead27 | walking; on arrival 79 |
| 79 | 0x4f | CarrierAtTargetPile | high | handler 0x4ec3fb | handler 0x4ec3fb: the good onto the target pile; a trade pile (art 0xfc) books it in the trade record |
| 80 | 0x50 | CarrierDeliver | medium | handler 0x4ec2a0 | next job of 0xd; AddGoodsToPile; a carried-good job in ResetSettlerToIdle |
| 81 | 0x51 | CarrierDone | high | handler 0x4ec67f | set by CarrierDeliver (80); handler 0x4ec67f clears the job (ClearSettlerJob) |
| 82 | 0x52 | MinerEnterCoalMine | medium | handler 0x4eeafd | job 4, type 10 (miner), building type 7 (coal mine); MinerJobEnter |
| 83 | 0x53 | MinerEnterGoldMine | medium | handler 0x4eeafd | job 4, type 10, building type 8 (gold mine) |
| 84 | 0x54 | MinerEnterIronMine | medium | handler 0x4eeafd | job 4, type 10, building type 9 (iron mine) |
| 85 | 0x55 | MinerEatCoalMine | medium | handler 0x4eeb0a | set by MinerJobEnter 0x4f9a00 for building type 7 (coal mine); its handler calls MinerJobFood 0x4f9b80 |
| 86 | 0x56 | MinerEatGoldMine | medium | handler 0x4eeb0a | set by MinerJobEnter 0x4f9a00 for building type 8 (gold mine); its handler calls MinerJobFood 0x4f9b80 |
| 87 | 0x57 | MinerEatIronMine | medium | handler 0x4eeb0a | set by MinerJobEnter 0x4f9a00 for building type 9 (iron mine); its handler calls MinerJobFood 0x4f9b80 |
| 88 | 0x58 | MinerDigCoalMine | medium | handler 0x4eeb31 | set by MinerJobFood for building type 7 (coal mine); its handler calls MinerJobDig 0x4fa220 |
| 89 | 0x59 | MinerFoundNothingCoalMine | medium | handler 0x4eeb3e | MinerJobDig's job after a failed dig, building type 7 (coal mine); next job: MinerEatCoalMine |
| 90 | 0x5a | MinerFoundCoalMine | medium | handler 0x4eeb71 | MinerJobDig's job after a find, building type 7 (coal mine); its handler adds the good to the output pile |
| 91 | 0x5b | MinerEndRoundCoalMine | high | handler 0x4eeb3e | set by 90 after the coal is out; handler 0x4eeb3e leaves the work tile, back to eating (85) |
| 92 | 0x5c | MinerDigGoldMine | medium | handler 0x4eeb31 | set by MinerJobFood for building type 8 (gold mine); its handler calls MinerJobDig 0x4fa220 |
| 93 | 0x5d | MinerFoundNothingGoldMine | medium | handler 0x4eebff | MinerJobDig's job after a failed dig, building type 8 (gold mine); next job: MinerEatGoldMine |
| 94 | 0x5e | MinerFoundGoldMine | medium | handler 0x4eec32 | MinerJobDig's job after a find, building type 8 (gold mine); its handler adds the good to the output pile |
| 95 | 0x5f | MinerEndRoundGoldMine | high | handler 0x4eebff | set by 94; handler 0x4eebff, back to 86 |
| 96 | 0x60 | MinerDigIronMine | medium | handler 0x4eeb31 | set by MinerJobFood for building type 9 (iron mine); its handler calls MinerJobDig 0x4fa220 |
| 97 | 0x61 | MinerFoundNothingIronMine | medium | handler 0x4eecc0 | MinerJobDig's job after a failed dig, building type 9 (iron mine); next job: MinerEatIronMine |
| 98 | 0x62 | MinerFoundIronMine | medium | handler 0x4eecf3 | MinerJobDig's job after a find, building type 9 (iron mine); its handler adds the good to the output pile |
| 99 | 0x63 | MinerEndRoundIronMine | high | handler 0x4eecc0 | set by 98; handler 0x4eecc0, back to 87 |
| 100 | 0x64 | LeaveHouse | medium | handler 0x4eef09 | set for a settler created at the door of a residence or big temple (UpdateEconomy); handler only clears the job |
| 101 | 0x65 | ForesterBackIn | high | handler 0x4ecf74 | walking 48 ends in the building with it; handler 0x4ecf74, back to 27 |
| 102 | 0x66 | WoodcutterBackIn | high | handler 0x4ece93 | set by 36 and 38; handler 0x4ece93, back to 24 |
| 103 | 0x67 | StonecutterBackIn | high | handler 0x4ecf09 | set by 39 and 41; handler 0x4ecf09, back to 25 |
| 104 | 0x68 | CarrierWalkToTool | medium | walking, handler 0x4ead27 | walking; a carrier fetching a tool (Settler::tool_type is the type it becomes) |
| 105 | 0x69 | CarrierTakeTool | medium | handler 0x4ebe0f | next job of 0x68; SubtractPlayerCounter, then job 3 |
| 106 | 0x6a | BowmanEnterTower | high | handler 0x4ecc94 | ArriveAtWork for Bowman1..3 (StartGuard); handler 0x4ecc94 counts class2_coming down, takes a window slot, then 140 |
| 107 | 0x6b | ToolsmithGoIn | high | handler 0x4ef162 | ArriveAtWork for a smith at the toolsmith; handler 0x4ef162 counts it in, then 116 |
| 108 | 0x6c | WeaponSmithGoIn | high | handler 0x4f10be | ArriveAtWork for a smith at the weaponsmith; handler 0x4f10be, then 117 |
| 109 | 0x6d | FisherGoIn | high | handler 0x4f10cc | ArriveAtWork for the fisher; handler 0x4f10cc, then 118 |
| 110 | 0x6e | PigFarmGoIn | high | handler 0x4f10da | ArriveAtWork for a pig farmer at a pig farm; handler 0x4f10da, then 119 |
| 111 | 0x6f | ButcherGoIn | high | handler 0x4f17ad | ArriveAtWork for the butcher; handler 0x4f17ad, then 120 |
| 112 | 0x70 | FarmerGoIn | high | handler 0x4f1a39 | ArriveAtWork for the farmer; handler 0x4f1a39, then 121 |
| 113 | 0x71 | MillerGoIn | high | handler 0x4f22d2 | ArriveAtWork for the miller; handler 0x4f22d2, then 122 |
| 114 | 0x72 | BakerGoIn | high | handler 0x4f2734 | ArriveAtWork for the baker; handler 0x4f2734, then 123 |
| 115 | 0x73 | WaterworkerGoIn | high | handler 0x4f2aff | ArriveAtWork for the waterworker; handler 0x4f2aff, then 124 |
| 116 | 0x74 | ToolsmithStart | high | handler 0x4ef170 | handler 0x4ef170: coal and iron there, tool piles not full, a tool wanted; then 125 |
| 117 | 0x75 | WeaponSmithTakeInputs | high | handler 0x4f0816 | handler 0x4f0816 reserves coal and iron when a weapon is wanted, then 131 |
| 118 | 0x76 | FisherSearch | high | handler 0x4f2b0d | handler 0x4f2b0d: water with fish, then 177 |
| 119 | 0x77 | PigFarmTakeInputs | high | handler 0x4f10e8 | handler 0x4f10e8: feeds (200) while fewer than 5 pigs, else takes one out (203) |
| 120 | 0x78 | ButcherTakeInputs | high | handler 0x4f17bb | handler 0x4f17bb reserves a pig, then 195 |
| 121 | 0x79 | FarmerSearch | high | handler 0x4f1a47 | handler 0x4f1a47: a field to sow (143) or to harvest (149) |
| 122 | 0x7a | MillerTakeInputs | high | handler 0x4f22e0 | handler 0x4f22e0 reserves grain, then 158 |
| 123 | 0x7b | BakerTakeInputs | high | handler 0x4f2742 | handler 0x4f2742 reserves flour and water, then 163 |
| 124 | 0x7c | WaterworkerSearch | high | handler 0x4f2fa0 | handler 0x4f2fa0: water, then 186 |
| 125 | 0x7d | ToolsmithTakeCoal | high | handler 0x4ef41b | handler 0x4ef41b takes the coal, then 126 |
| 126 | 0x7e | ToolsmithWork | high | handler 0x4ef46e | handler 0x4ef46e: animation 127 only, then 127 |
| 127 | 0x7f | ToolsmithTakeIron | high | handler 0x4ef486 | handler 0x4ef486 takes the iron, forge effect, then 128 |
| 128 | 0x80 | ToolsmithToolDone | high | handler 0x4ef53d | handler 0x4ef53d: the tool onto its pile; the next tool at once (130) or 129 |
| 129 | 0x81 | ToolsmithEndRound | high | handler 0x4f0036 | handler 0x4f0036 leaves the work tile, back to 116 |
| 130 | 0x82 | ToolsmithTakeCoalAgain | high | handler 0x4ef41b | set by 128; same handler as 125 |
| 131 | 0x83 | WeaponSmithTakeCoal | high | handler 0x4f09f7 | handler 0x4f09f7 takes the coal, then 132 |
| 132 | 0x84 | WeaponSmithWork | high | handler 0x4f0a4d | handler 0x4f0a4d: animation only, then 133 |
| 133 | 0x85 | WeaponSmithTakeIron | high | handler 0x4f0a68 | handler 0x4f0a68 takes the iron, anvil effect, then 134 |
| 134 | 0x86 | WeaponSmithFinish | high | handler 0x4f0b1f | handler 0x4f0b1f: the weapon onto its pile at the door; again (136) or 135 |
| 135 | 0x87 | WeaponSmithEndRound | high | handler 0x4f108b | handler 0x4f108b leaves the tile, back to 117 |
| 136 | 0x88 | WeaponSmithTakeCoalAgain | high | handler 0x4f09f7 | set by 134; same handler as 131 |
| 137 | 0x89 | Job137 | placeholder | no-op |  |
| 138 | 0x8a | RecruitAtBarracks | medium | handler 0x4ebac6 | job 4 start for type 0 (carrier): a carrier sent by the barracks (UpdateEconomy) to become a soldier; its saved_counter is the input slot |
| 139 | 0x8b | PikemanEnterTower | high | handler 0x4ecd51 | ArriveAtWork for Pikeman1..3 (StartGuard); handler 0x4ecd51 counts class3_coming down, then 141 |
| 140 | 0x8c | BowmanInTower | high | handler 0x4ece16 | next job of 106, and a class 2 tower conqueror; handler 0x4ece16 shoots (UpdateSoldier) from the building; bowman shooting from a tower: range 22/10, arrows from the tower places (UpdateSoldier) |
| 141 | 0x8d | PikemanInTower | high | handler 0x4f8b7c | next job of 139, and a class 3 tower conqueror; handler 0x4f8b7c waits; a pikeman that captured a tower (UpdateSoldier) |
| 142 | 0x8e | UnitFighting | medium | handler 0x4e9884 | UpdateSoldier sets it on each attack; handler 0x4e9884 turns it back into UnitIdle (55); a soldier attacking (set by UpdateSoldier on each hit; next job 55) |
| 143 | 0x8f | FarmerLeaveToSow | high | handler 0x4f1e6d | set by 121; handler 0x4f1e6d leaves the building with seed, then 144 |
| 144 | 0x90 | FarmerWalkToSow | high | walking, handler 0x4ead27 | walking; on arrival 145 |
| 145 | 0x91 | FarmerSow | high | handler 0x4f1e93 | handler 0x4f1e93 sows if the tile is still free, then 146 (or walks home, 147) |
| 146 | 0x92 | FarmerSown | high | handler 0x4f20d5 | handler 0x4f20d5: the field object, walks home (147) |
| 147 | 0x93 | FarmerWalkHome | high | walking, handler 0x4ead27 | walking; on arrival into the building with 148 |
| 148 | 0x94 | FarmerBackIn | high | handler 0x4f2108 | handler 0x4f2108, back to 121 |
| 149 | 0x95 | FarmerLeaveToHarvest | high | handler 0x4f2116 | set by 121; handler 0x4f2116 leaves the building, then 156 |
| 150 | 0x96 | FarmerAtField | high | handler 0x4f213c | handler 0x4f213c: still ripe? then 151, else walks home (147) |
| 151 | 0x97 | FarmerHarvest | high | handler 0x4f218a | handler 0x4f218a: two harvest steps, then 152 with the grain |
| 152 | 0x98 | FarmerWalkHomeLoaded | high | walking, handler 0x4ead27 | walking; on arrival 153 |
| 153 | 0x99 | FarmerHome | high | handler 0x4f21f5 | handler 0x4f21f5: with grain on to 154, else into the building (157) |
| 154 | 0x9a | FarmerStore | high | handler 0x4f223a | handler 0x4f223a: Good::Grain onto the output pile, then 155 |
| 155 | 0x9b | FarmerGoInAgain | high | handler 0x4f221a | handler 0x4f221a: into the building, then 157 |
| 156 | 0x9c | FarmerWalkToHarvest | high | walking, handler 0x4ead27 | walking; on arrival 150 |
| 157 | 0x9d | FarmerRest | high | handler 0x4f2108 | set by 153 and 155; handler 0x4f2108 (as 148), back to 121 |
| 158 | 0x9e | MillerGrind | high | handler 0x4f23a9 | set by 122; handler 0x4f23a9 takes the grain, the sails turn, then 159 |
| 159 | 0x9f | MillerWork | high | handler 0x4f24bf | handler 0x4f24bf: animation only, then 160 |
| 160 | 0xa0 | MillerStoreFlour | high | handler 0x4f24da | handler 0x4f24da: Good::Flour onto the output pile; again (162) or 161 |
| 161 | 0xa1 | MillerEndRound | high | handler 0x4f25ec | handler 0x4f25ec leaves the tile, back to 122 |
| 162 | 0xa2 | MillerGrindAgain | high | handler 0x4f261f | set by 160; handler 0x4f261f (the same code as 158) |
| 163 | 0xa3 | BakerTakeWater | high | handler 0x4f284d | set by 123; handler 0x4f284d takes the water (input 1), then 164 |
| 164 | 0xa4 | BakerTakeFlour | high | handler 0x4f28a3 | handler 0x4f28a3 takes the flour, oven effect, then 165 |
| 165 | 0xa5 | BakerWork | high | handler 0x4f295a | handler 0x4f295a: animation only, then 166 |
| 166 | 0xa6 | BakerStoreBread | high | handler 0x4f2975 | handler 0x4f2975: Good::Bread onto the output pile; again (168) or 167 |
| 167 | 0xa7 | BakerEndRound | high | handler 0x4f2acc | handler 0x4f2acc, back to 123 |
| 168 | 0xa8 | BakerTakeWaterAgain | high | handler 0x4f284d | set by 166; same handler as 163 |
| 169 | 0xa9 | CarrierPickUp | medium | handler 0x4ec031 | next job of 0xb; RestoreSettlerAnimation, then 0xaa |
| 170 | 0xaa | CarrierLeavePile | medium | handler 0x4ec086 | next job of 0xa9; SetSettlerTarget to the building, then 0xc; carries the good |
| 171 | 0xab | WalkToOwnArea | medium | walking, handler 0x4ead27 | walking; on arrival in its own area the job is cleared, else health -2 and job 0xac (WalkSettler) |
| 172 | 0xac | SeekOwnArea | medium | handler 0x4ea55d | set by ResetSettlerToIdle; handler picks a target (Random, SetSettlerTarget) and walks there with 0xab |
| 173 | 0xad | GuardHeal | medium | handler 0x4ea531 | the guard (type 37) that GuardBuilding and BuildingDone create as Building::guard; handler 0x4ea531 heals one point every 20 ticks up to 100; a military building's guard (Building::guard; UpdateSoldier) |
| 174 | 0xae | GuardBuilding | low | handler 0x4ea314 | a soldier in this job guards a military building against shots (WalkSettler's target search) |
| 175 | 0xaf | GarrisonSoldierDies | high | handler 0x4ef005 | handler 0x4ef005: a garrison soldier killed while others remain; the best of the garrison takes its place (174); a garrison soldier killed while others remain (UpdateSoldier) |
| 176 | 0xb0 | UnitWalkCountdown | high | walking, handler 0x4eacd2 | walking; handler 0x4eacd2 counts +0x1a down, then UnitWalk (53) |
| 177 | 0xb1 | FisherLeave | high | handler 0x4f2d69 | set by 118; handler 0x4f2d69 leaves the building, then 178 |
| 178 | 0xb2 | FisherWalkToWater | high | walking, handler 0x4ead27 | walking; on arrival 179 |
| 179 | 0xb3 | FisherCast | high | handler 0x4f2d88 | handler 0x4f2d88 casts towards water, a fish by chance (tile resources), then 180 |
| 180 | 0xb4 | FisherCastDone | high | handler 0x4f2de1 | handler 0x4f2de1 walks home (181) |
| 181 | 0xb5 | FisherWalkHome | high | walking, handler 0x4ead27 | walking; on arrival 182 |
| 182 | 0xb6 | FisherHome | high | handler 0x4f2eb5 | handler 0x4f2eb5: with a fish on to 183, else into the building (185) |
| 183 | 0xb7 | FisherStore | high | handler 0x4f2efa | handler 0x4f2efa: Good::Fish onto the output pile, then 184 |
| 184 | 0xb8 | FisherGoInAgain | high | handler 0x4f2eda | handler 0x4f2eda: into the building, then 185 |
| 185 | 0xb9 | FisherRest | high | handler 0x4f2f92 | handler 0x4f2f92, back to 118 |
| 186 | 0xba | WaterworkerLeave | high | handler 0x4f31c5 | set by 124; handler 0x4f31c5 leaves the building, then 187 |
| 187 | 0xbb | WaterworkerWalkToWater | high | walking, handler 0x4ead27 | walking; on arrival 189 |
| 188 | 0xbc | WaterworkerWalkHome | high | walking, handler 0x4ead27 | walking; on arrival 191 |
| 189 | 0xbd | WaterworkerDraw | high | handler 0x4f31e4 | handler 0x4f31e4 draws water if the tile is still water, then 190 |
| 190 | 0xbe | WaterworkerDrawn | high | handler 0x4f3261 | handler 0x4f3261 walks home (188) |
| 191 | 0xbf | WaterworkerHome | high | handler 0x4f326f | handler 0x4f326f: with water on to 192, else into the building (194) |
| 192 | 0xc0 | WaterworkerStore | high | handler 0x4f32b4 | handler 0x4f32b4: Good::Water onto the output pile, then 193 |
| 193 | 0xc1 | WaterworkerGoInAgain | high | handler 0x4f3294 | handler 0x4f3294: into the building, then 194 |
| 194 | 0xc2 | WaterworkerRest | high | handler 0x4f334c | handler 0x4f334c, back to 124 |
| 195 | 0xc3 | ButcherTakePig | high | handler 0x4f1884 | set by 120; handler 0x4f1884 takes the pig, then 196 |
| 196 | 0xc4 | ButcherWork | high | handler 0x4f18da | handler 0x4f18da: animation only, then 197 |
| 197 | 0xc5 | ButcherStoreMeat | high | handler 0x4f18f5 | handler 0x4f18f5: Good::Meat onto the output pile; again (199) or 198 |
| 198 | 0xc6 | ButcherEndRound | high | handler 0x4f1a06 | handler 0x4f1a06, back to 120 |
| 199 | 0xc7 | ButcherTakePigAgain | high | handler 0x4f1884 | set by 197; same handler as 195 |
| 200 | 0xc8 | PigFarmTakeGrain | high | handler 0x4f125f | set by 119; handler 0x4f125f takes the grain, then 201 |
| 201 | 0xc9 | PigFarmTakeWater | high | handler 0x4f12b5 | handler 0x4f12b5 takes the water, then 202 |
| 202 | 0xca | PigFarmFed | high | handler 0x4f130b | handler 0x4f130b: fed; by chance one pig more (Random above 15000); back to 119 |
| 203 | 0xcb | PigFarmTakeOut | high | handler 0x4f1352 | set by 119 with five pigs in the pen; handler 0x4f1352, then 204 |
| 204 | 0xcc | PigFarmStorePig | high | handler 0x4f1394 | handler 0x4f1394: Good::Pig onto the output pile, then 205 |
| 205 | 0xcd | PigFarmEndRound | high | handler 0x4f1425 | handler 0x4f1425, back to 119 |
| 206 | 0xce | DonkeyFarmTakeGrain | high | handler 0x4f15bf | set by 306; handler 0x4f15bf, then 207 |
| 207 | 0xcf | DonkeyFarmTakeWater | high | handler 0x4f1615 | handler 0x4f1615, then 208 |
| 208 | 0xd0 | DonkeyFarmFed | high | handler 0x4f166b | handler 0x4f166b: fed; by chance one donkey more (Random above 30000); back to 306 |
| 209 | 0xd1 | DonkeyFarmTakeOut | high | handler 0x4f16df | set by 306 with three donkeys in the pen; handler 0x4f16df, then 210 |
| 210 | 0xd2 | DonkeyFarmRelease | high | handler 0x4f1721 | handler 0x4f1721: a donkey walks out (SpawnSettlersAround, SettlerType::Donkey), then 211 |
| 211 | 0xd3 | DonkeyFarmEndRound | high | handler 0x4f177a | handler 0x4f177a, back to 306 |
| 212 | 0xd4 | GeologistWork | medium | handler 0x4f335a | type 0x16 (geologist) on a free mountain tile (terrain class 0x20) starts it (WalkSettler) |
| 213 | 0xd5 | GeologistDone | high | handler 0x4f376b | handler 0x4f376b (shared with 303) sends the unit back to idle |
| 214 | 0xd6 | MinerEnterGemMine | medium | handler 0x4eeafd | job 4, type 10, building type 0x25 (gem mine) |
| 215 | 0xd7 | MinerEnterSulfurMine | medium | handler 0x4eeafd | job 4, type 10, building types 0x1d / 0x38 (sulfur mines) |
| 216 | 0xd8 | MinerEatGemMine | medium | handler 0x4eeb0a | set by MinerJobEnter 0x4f9a00 for building type 37 (0x25, gem mine); its handler calls MinerJobFood 0x4f9b80 |
| 217 | 0xd9 | MinerEatSulfurMine | medium | handler 0x4eeb0a | set by MinerJobEnter 0x4f9a00 for building type 29/56 (0x1d/0x38, sulfur mines); its handler calls MinerJobFood 0x4f9b80 |
| 218 | 0xda | MinerDigGemMine | medium | handler 0x4eeb31 | set by MinerJobFood for building type 37 (0x25, gem mine); its handler calls MinerJobDig 0x4fa220 |
| 219 | 0xdb | MinerFoundNothingGemMine | medium | handler 0x4eed81 | MinerJobDig's job after a failed dig, building type 37 (0x25, gem mine); next job: MinerEatGemMine |
| 220 | 0xdc | MinerFoundGemMine | medium | handler 0x4eedb4 | MinerJobDig's job after a find, building type 37 (0x25, gem mine); its handler adds the good to the output pile |
| 221 | 0xdd | MinerEndRoundGemMine | high | handler 0x4eed81 | set by 220; handler 0x4eed81, back to 216 |
| 222 | 0xde | MinerDigSulfurMine | medium | handler 0x4eeb31 | set by MinerJobFood for building type 29/56 (0x1d/0x38, sulfur mines); its handler calls MinerJobDig 0x4fa220 |
| 223 | 0xdf | MinerFoundNothingSulfurMine | medium | handler 0x4eee45 | MinerJobDig's job after a failed dig, building type 29/56 (0x1d/0x38, sulfur mines); next job: MinerEatSulfurMine |
| 224 | 0xe0 | MinerFoundSulfurMine | medium | handler 0x4eee78 | MinerJobDig's job after a find, building type 29/56 (0x1d/0x38, sulfur mines); its handler adds the good to the output pile |
| 225 | 0xe1 | MinerEndRoundSulfurMine | high | handler 0x4eee45 | set by 224; handler 0x4eee45, back to 217 |
| 226 | 0xe2 | RiceFarmerGoIn | high | handler 0x4f3d45 | ArriveAtWork for the rice farmer; handler 0x4f3d45 counts it in, then 227 |
| 227 | 0xe3 | RiceFarmerSearch | high | handler 0x4f3d53 | handler 0x4f3d53: a ripe field (234) or a spot to plant (228) |
| 228 | 0xe4 | RiceFarmerLeaveToPlant | high | handler 0x4f4071 | handler 0x4f4071 leaves the building, then 229 |
| 229 | 0xe5 | RiceFarmerWalkToPlant | high | walking, handler 0x4ead27 | walking; on arrival 230 |
| 230 | 0xe6 | RiceFarmerPlant | high | handler 0x4f4097 | handler 0x4f4097, then 231 |
| 231 | 0xe7 | RiceFarmerPlanted | high | handler 0x4f4226 | handler 0x4f4226: the rice object 0x67, walks home (232) |
| 232 | 0xe8 | RiceFarmerWalkHome | high | walking, handler 0x4ead27 | walking; on arrival into the building with 233 |
| 233 | 0xe9 | RiceFarmerBackIn | high | handler 0x4f4259 | handler 0x4f4259 (as 242), back to 227 |
| 234 | 0xea | RiceFarmerLeaveToHarvest | high | handler 0x4f4267 | handler 0x4f4267 leaves the building, then 241 |
| 235 | 0xeb | RiceFarmerAtField | high | handler 0x4f428d | handler 0x4f428d: still ripe? then 236, else walks home |
| 236 | 0xec | RiceFarmerHarvest | high | handler 0x4f42db | handler 0x4f42db: two steps, then 237 with the rice |
| 237 | 0xed | RiceFarmerWalkHomeLoaded | high | walking, handler 0x4ead27 | walking; on arrival 238 |
| 238 | 0xee | RiceFarmerHome | high | handler 0x4f4383 | handler 0x4f4383: with rice on to 239, else into the building (242) |
| 239 | 0xef | RiceFarmerStore | high | handler 0x4f43cb | handler 0x4f43cb: Good::Rice onto the output pile, then 240 |
| 240 | 0xf0 | RiceFarmerGoInAgain | high | handler 0x4f43a8 | handler 0x4f43a8: into the building, then 242 |
| 241 | 0xf1 | RiceFarmerWalkToHarvest | high | walking, handler 0x4ead27 | walking; on arrival 235 |
| 242 | 0xf2 | RiceFarmerRest | high | handler 0x4f4259 | handler 0x4f4259, back to 227 |
| 243 | 0xf3 | WinegrowerGoIn | high | handler 0x4f4463 | ArriveAtWork for the winegrower; handler 0x4f4463, then 244 |
| 244 | 0xf4 | WinegrowerSearch | high | handler 0x4f4471 | handler 0x4f4471: ripe vines (251) or a spot to plant (245) |
| 245 | 0xf5 | WinegrowerLeaveToPlant | high | handler 0x4f49f3 | handler 0x4f49f3, then 246 |
| 246 | 0xf6 | WinegrowerWalkToPlant | high | walking, handler 0x4ead27 | walking; on arrival 247 |
| 247 | 0xf7 | WinegrowerPlant | high | handler 0x4f4a19 | handler 0x4f4a19, then 248 |
| 248 | 0xf8 | WinegrowerPlanted | high | handler 0x4f4d54 | handler 0x4f4d54: the vine object 0x5e, walks home (249) |
| 249 | 0xf9 | WinegrowerWalkHome | high | walking, handler 0x4ead27 | walking; on arrival into the building with 250 |
| 250 | 0xfa | WinegrowerBackIn | high | handler 0x4f4d87 | handler 0x4f4d87 (as 259), back to 244 |
| 251 | 0xfb | WinegrowerLeaveToHarvest | high | handler 0x4f4d95 | handler 0x4f4d95, then 258 |
| 252 | 0xfc | WinegrowerAtField | high | handler 0x4f4dbb | handler 0x4f4dbb: still ripe? then 253, else walks home |
| 253 | 0xfd | WinegrowerHarvest | high | handler 0x4f4e09 | handler 0x4f4e09: one step, then 254 with the grapes |
| 254 | 0xfe | WinegrowerWalkHomeLoaded | high | walking, handler 0x4ead27 | walking; on arrival 255 |
| 255 | 0xff | WinegrowerHome | high | handler 0x4f4e5f | handler 0x4f4e5f: with grapes into the building (256), else in (259) |
| 256 | 0x100 | WinegrowerEnterLoaded | high | handler 0x4f4e90 | handler 0x4f4e90, then 257 |
| 257 | 0x101 | WinegrowerStore | high | handler 0x4f4eb3 | handler 0x4f4eb3: Good::Wine onto the output pile, then 260 |
| 258 | 0x102 | WinegrowerWalkToHarvest | high | walking, handler 0x4ead27 | walking; on arrival 252 |
| 259 | 0x103 | WinegrowerRest | high | handler 0x4f4d87 | handler 0x4f4d87, back to 244 |
| 260 | 0x104 | WinegrowerGoInAgain | high | handler 0x4f4f4b | handler 0x4f4f4b: into the building, then 259 |
| 261 | 0x105 | DistillerGoIn | high | handler 0x4f4f6e | ArriveAtWork for the distiller; handler 0x4f4f6e, then 288 |
| 262 | 0x106 | Job262 | placeholder | no-op |  |
| 263 | 0x107 | DistillerTakeCoal | high | handler 0x4f5087 | set by 288; handler 0x4f5087 takes the coal (input 1), then 264 |
| 264 | 0x108 | DistillerTakeRice | high | handler 0x4f50dd | handler 0x4f50dd takes the rice (input 0), then 265 |
| 265 | 0x109 | DistillerProduce | high | handler 0x4f5133 | handler 0x4f5133: Good::RiceLiquor onto the output pile; again (267) or 266 |
| 266 | 0x10a | DistillerEndRound | high | handler 0x4f528a | handler 0x4f528a leaves the tile, back to 288 |
| 267 | 0x10b | DistillerTakeCoalAgain | high | handler 0x4f5087 | set by 265; same handler as 263 |
| 268 | 0x10c | BrewerGoIn | high | handler 0x4f560c | ArriveAtWork for the brewer; handler 0x4f560c, then 289 |
| 269 | 0x10d | Job269 | placeholder | no-op |  |
| 270 | 0x10e | BrewerTakeWater | high | handler 0x4f5725 | set by 289; handler 0x4f5725, then 271 |
| 271 | 0x10f | BrewerTakeGrain | high | handler 0x4f577b | handler 0x4f577b, then 272 |
| 272 | 0x110 | BrewerProduce | high | handler 0x4f57d1 | handler 0x4f57d1: Good::Beer; again (274) or 273 |
| 273 | 0x111 | BrewerEndRound | high | handler 0x4f5928 | handler 0x4f5928, back to 289 |
| 274 | 0x112 | BrewerTakeWaterAgain | high | handler 0x4f5725 | set by 272; same handler as 270 |
| 275 | 0x113 | PowderMakerGoIn | high | handler 0x4f52bd | ArriveAtWork for the powder maker; handler 0x4f52bd, then 290 |
| 276 | 0x114 | Job276 | placeholder | no-op |  |
| 277 | 0x115 | PowderMakerTakeCoal | high | handler 0x4f53d6 | set by 290; handler 0x4f53d6, then 278 |
| 278 | 0x116 | PowderMakerTakeSulfur | high | handler 0x4f542c | handler 0x4f542c, then 279 |
| 279 | 0x117 | PowderMakerProduce | high | handler 0x4f5482 | handler 0x4f5482: Good::Gunpowder; again (281) or 280 |
| 280 | 0x118 | PowderMakerEndRound | high | handler 0x4f55d9 | handler 0x4f55d9, back to 290 |
| 281 | 0x119 | PowderMakerTakeCoalAgain | high | handler 0x4f53d6 | set by 279; same handler as 277 |
| 282 | 0x11a | CharcoalBurnerGoIn | high | handler 0x4f595b | ArriveAtWork for the charcoal burner; handler 0x4f595b, then 291 |
| 283 | 0x11b | Job283 | placeholder | no-op |  |
| 284 | 0x11c | CharcoalBurnerTakeBoard1 | high | handler 0x4f5abe | set by 291; handler 0x4f5abe: the first of five boards, smoke effect, then 285 |
| 285 | 0x11d | CharcoalBurnerTakeBoard2 | high | handler 0x4f5b73 | handler 0x4f5b73, then 292 |
| 286 | 0x11e | CharcoalBurnerEndRound | high | handler 0x4f5e81 | handler 0x4f5e81, back to 291 |
| 287 | 0x11f | CharcoalBurnerTakeBoard1Again | high | handler 0x4f5eb4 | set by 295; same handler as 284 |
| 288 | 0x120 | DistillerCheck | high | handler 0x4f4f7c | handler 0x4f4f7c waits until both inputs are there and the output has room, then 263 |
| 289 | 0x121 | BrewerCheck | high | handler 0x4f561a | handler 0x4f561a, then 270 |
| 290 | 0x122 | PowderMakerCheck | high | handler 0x4f52cb | handler 0x4f52cb, then 277 |
| 291 | 0x123 | CharcoalBurnerCheck | high | handler 0x4f5969 | handler 0x4f5969: five boards there and room in the output, then 284 |
| 292 | 0x124 | CharcoalBurnerTakeBoard3 | high | handler 0x4f5bc9 | handler 0x4f5bc9, then 293 |
| 293 | 0x125 | CharcoalBurnerTakeBoard4 | high | handler 0x4f5c1f | handler 0x4f5c1f, then 294 |
| 294 | 0x126 | CharcoalBurnerTakeBoard5 | high | handler 0x4f5c75 | handler 0x4f5c75, then 295 |
| 295 | 0x127 | CharcoalBurnerProduce | high | handler 0x4f5ccb | handler 0x4f5ccb: Good::Coal onto the output pile; again (287) or 286 |
| 296 | 0x128 | CatapultWorkshopGoIn | high | handler 0x4f0069 | ArriveAtWork for a smith at the catapult workshop; handler 0x4f0069, then 297 |
| 297 | 0x129 | CatapultWorkshopPutInput | high | handler 0x4f0077 | handler 0x4f0077 puts one unit of a wanted input into the machine |
| 298 | 0x12a | BallistaWorkshopGoIn | high | handler 0x4f034e | the same at the ballista workshop; handler 0x4f034e, then 299 |
| 299 | 0x12b | BallistaWorkshopPutInput | high | handler 0x4f035c | handler 0x4f035c |
| 300 | 0x12c | CannonWorkshopGoIn | high | handler 0x4f05b2 | the same at the cannon workshop; handler 0x4f05b2, then 301 |
| 301 | 0x12d | CannonWorkshopPutInput | high | handler 0x4f05c0 | handler 0x4f05c0 |
| 302 | 0x12e | PioneerWork | medium | handler 0x4f37bd | type 0x28 (pioneer) on a tile of no or another owner starts it (WalkSettler); handler calls SetOwnerOfFreeTile |
| 303 | 0x12f | PioneerDone | high | handler 0x4f3cf3 | handler 0x4f3cf3 (shared with 213) sends the unit back to idle |
| 304 | 0x130 | SevenTileUnitPart | high | handler 0x4f5f6b | the six parts of a seven-tile unit (CreateSevenTileUnit); handler 0x4f5f6b sets the counter to 100; the parts (type 42) of a seven-tile unit (CreateSevenTileUnit) |
| 305 | 0x131 | DonkeyFarmGoIn | high | handler 0x4f1458 | ArriveAtWork for a pig farmer at a donkey farm; handler 0x4f1458, then 306 |
| 306 | 0x132 | DonkeyFarmTakeInputs | high | handler 0x4f1466 | handler 0x4f1466: feeds (206) while fewer than 3 donkeys, else takes one out (209) |
| 307 | 0x133 | DonkeyWalkToPile | high | walking, handler 0x4ead27 | walking; SendNearestSettlerToPile sends an idle donkey (kIdleDonkeys) to a pile; on arrival 308; a donkey sent to a pile (SendNearestSettlerToPile; idle list of type 39) |
| 308 | 0x134 | DonkeyLoad | high | handler 0x4f5f79 | handler 0x4f5f79 loads one or two units and sets off along the trade route (309) |
| 309 | 0x135 | DonkeyWalkToMarket | high | walking, handler 0x4ead27 | walking; on arrival 310; a hit in this job does no damage (UpdateSoldier) but makes it unload; a donkey (class 30) job in which a soldier's hit does no damage (-> 310; UpdateSoldier) |
| 310 | 0x136 | DonkeyUnload | high | handler 0x4f6255 | handler 0x4f6255 drops its goods at the marketplace and walks the route back (330) |
| 311 | 0x137 | CatapultProduction | high | handler 0x4f0229 | DebugLog "Katapultproduktion owner %u" |
| 312 | 0x138 | BallistaProduction | high | handler 0x4f04cc | handler 0x4f04cc: the ballista workshop's WorkshopCheck (as 311) |
| 313 | 0x139 | CannonProduction | high | handler 0x4f0730 | handler 0x4f0730: the cannon workshop's WorkshopCheck |
| 314 | 0x13a | ShipyardGoIn | high | handler 0x4f6e84 | ArriveAtWork for the shipyard worker; handler 0x4f6e84 counts it in, then 315 (the provisional ShipyardUnitDone was wrong) |
| 315 | 0x13b | ShipyardFetch | high | handler 0x4f6e92 | handler 0x4f6e92: a board (316) or iron (318) for the ship |
| 316 | 0x13c | ShipyardTakeBoard | high | handler 0x4f6ff4 | handler 0x4f6ff4 takes the board, then 317 |
| 317 | 0x13d | ShipyardLeaveWithBoard | high | handler 0x4f70ae | handler 0x4f70ae leaves for the slipway (320) |
| 318 | 0x13e | ShipyardTakeIron | high | handler 0x4f7051 | handler 0x4f7051 takes the iron, then 319 |
| 319 | 0x13f | ShipyardLeaveWithIron | high | handler 0x4f70ae | handler 0x4f70ae (as 317) |
| 320 | 0x140 | ShipyardWalkToSlipway | high | walking, handler 0x4ead27 | walking; on arrival 323 |
| 321 | 0x141 | ShipyardWalkHome | high | walking, handler 0x4ead27 | walking; on arrival into the building with 322 |
| 322 | 0x142 | ShipyardBackIn | high | handler 0x4f73b2 | handler 0x4f73b2, back to 315 |
| 323 | 0x143 | ShipyardBuild | high | handler 0x4f70d4 | handler 0x4f70d4: ten work steps on the ship (325), launches it after the last; then 324 |
| 324 | 0x144 | ShipyardLeaveSlipway | high | handler 0x4f735e | handler 0x4f735e walks to the door (321) |
| 325 | 0x145 | ShipBeingBuilt | high | handler 0x4f8b7c | the ship at the shipyard while it is built (ShipyardBuild counts its +0x1c); handler 0x4f8b7c waits |
| 326 | 0x146 | OnBoard | high | handler 0x4f8b7c | a settler that boarded a ferry (WalkSettler BoardShip) |
| 327 | 0x147 | ShipWalkToHarbour | high | walking, handler 0x4ead27 | walking (waypoints); SendNearestSettlerToBuilding sends an idle trade ship to a harbour; on arrival 331; a trade ship sent to a harbour (SendNearestSettlerToBuilding; idle list of type 43) |
| 328 | 0x148 | ShipSailWithCargo | high | walking, handler 0x4ead27 | walking (waypoints); set by 331; on arrival ShipUnload (332) |
| 329 | 0x149 | ShipSailHome | high | walking, handler 0x4ead27 | walking (waypoints); set by ShipUnload; on arrival job 0 |
| 330 | 0x14a | DonkeyWalkHome | high | walking, handler 0x4ead27 | walking; set by DonkeyUnload on the route back; on arrival job 0 |
| 331 | 0x14b | ShipLoad | high | handler 0x4f6514 | handler 0x4f6514: a trade ship at the harbour takes the fullest ring piles as cargo, then 328 |
| 332 | 0x14c | ShipUnload | high | handler 0x4f6a18 | DebugLog "Schiff laedt ab" (ship unloads) |
| 333 | 0x14d | HealerWait | high | handler 0x4f745b | handler 0x4f745b looks for a wounded soldier of its team |
| 334 | 0x14e | HealerHeal | high | handler 0x4f776f | handler 0x4f776f: the patient gets its full health back, then 335 |
| 335 | 0x14f | HealerHealed | high | handler 0x4f780f | handler 0x4f780f leaves the tile, back to 333 |
| 336 | 0x150 | HealerGoIn | high | handler 0x4f73ec | ArriveAtWork for the healer; handler 0x4f73ec counts it in, then 333 (the provisional Done was wrong) |
| 337 | 0x151 | Shoot | high | handler 0x4f7842 | a soldier shooting from its post (WalkSettler SoldierShoot: projectile, effect record) |
| 338 | 0x152 | ThiefChoosePile | medium | handler 0x4f78b0 | walk state 3 of type 0x17 (thief); AddPileClaim, then 0x193 |
| 339 | 0x153 | AlchemistGoIn | high | handler 0x4f8399 | ArriveAtWork for the alchemist; handler 0x4f8399, then 341 |
| 340 | 0x154 | Job340 | placeholder | no-op |  |
| 341 | 0x155 | AlchemistStart | high | handler 0x4f83a7 | handler 0x4f83a7: both inputs there and a door pile with room, then 342 |
| 342 | 0x156 | AlchemistTakeInput1 | high | handler 0x4f8a95 | handler 0x4f8a95 takes input 0, then 343 |
| 343 | 0x157 | AlchemistWork1 | high | handler 0x4f84f8 | handler 0x4f84f8: animation only, then 344 |
| 344 | 0x158 | AlchemistTakeInput2 | high | handler 0x4f8513 | handler 0x4f8513 takes input 1, then 345 |
| 345 | 0x159 | AlchemistWork2 | high | handler 0x4f859a | handler 0x4f859a: animation only, then 346 |
| 346 | 0x15a | AlchemistRoll | high | handler 0x4f85b5 | handler 0x4f85b5 rolls 0..3 iron and gold, then 347..350 by the outcome |
| 347 | 0x15b | AlchemistStoreGold | high | handler 0x4f876b | set by 346 for gold only; handler 0x4f876b stores the yield |
| 348 | 0x15c | AlchemistStoreIron | high | handler 0x4f876b | set by 346 for iron only; handler 0x4f876b |
| 349 | 0x15d | AlchemistStoreBoth | high | handler 0x4f876b | set by 346 for iron and gold; handler 0x4f876b |
| 350 | 0x15e | AlchemistStoreNothing | high | handler 0x4f876b | set by 346 for nothing; handler 0x4f876b |
| 351 | 0x15f | AlchemistTakeInput1Again | high | handler 0x4f8a95 | set by 347..350 when the next inputs are there; same handler as 342 |
| 352 | 0x160 | AlchemistEndRound | high | handler 0x4f8b1c | handler 0x4f8b1c leaves the tile, back to 341 |
| 353 | 0x161 | BeekeeperGoIn | high | handler 0x4f7c4b | ArriveAtWork for the beekeeper (and 360, 366, 368); handler 0x4f7c4b counts it in, then 355 |
| 354 | 0x162 | Job354 | placeholder | no-op |  |
| 355 | 0x163 | BeekeeperSearch | high | handler 0x4f7c59 | handler 0x4f7c59: flowers to harvest (361) or a spot for a hive (356) |
| 356 | 0x164 | BeekeeperLeaveToPlaceHive | high | handler 0x4f8065 | handler 0x4f8065 leaves the building, then 357 |
| 357 | 0x165 | BeekeeperWalkToHiveSpot | high | walking, handler 0x4ead27 | walking; on arrival 358 |
| 358 | 0x166 | BeekeeperPlaceHive | high | handler 0x4f8084 | handler 0x4f8084 sets up a hive, then 359 |
| 359 | 0x167 | BeekeeperHivePlaced | high | handler 0x4f8181 | handler 0x4f8181: the hive (object 247) is up; walks home (360) |
| 360 | 0x168 | BeekeeperWalkHome | high | walking, handler 0x4ead27 | walking; on arrival into the building with 353 |
| 361 | 0x169 | BeekeeperLeaveToHarvest | high | handler 0x4f81c4 | handler 0x4f81c4 leaves the building, then 362 |
| 362 | 0x16a | BeekeeperWalkToHive | high | walking, handler 0x4ead27 | walking; on arrival 363 |
| 363 | 0x16b | BeekeeperHarvest | high | handler 0x4f81e3 | handler 0x4f81e3 harvests the hive, then 364 |
| 364 | 0x16c | BeekeeperHarvested | high | handler 0x4f8246 | handler 0x4f8246 walks home (365) |
| 365 | 0x16d | BeekeeperWalkHomeLoaded | high | walking, handler 0x4ead27 | walking; on arrival 366 |
| 366 | 0x16e | BeekeeperHome | high | handler 0x4f82c1 | handler 0x4f82c1: with honey on to 367, else into the building (353) |
| 367 | 0x16f | BeekeeperStore | high | handler 0x4f82ea | handler 0x4f82ea: Good::Honey onto the output pile, then 368 |
| 368 | 0x170 | BeekeeperGoInAgain | high | handler 0x4eb3d2 | handler 0x4eb3d2: into the building, then 353 |
| 369 | 0x171 | BeekeeperEndRound | medium | handler 0x4f838b | handler 0x4f838b leaves the tile, back to 355; nothing in S3.EXE sets it (no 171h immediate in the disassembly) |
| 370 | 0x172 | Job370 | placeholder | no-op |  |
| 371 | 0x173 | Job371 | placeholder | no-op |  |
| 372 | 0x174 | Job372 | placeholder | no-op |  |
| 373 | 0x175 | Job373 | placeholder | no-op |  |
| 374 | 0x176 | Job374 | placeholder | no-op |  |
| 375 | 0x177 | Job375 | placeholder | no-op |  |
| 376 | 0x178 | Job376 | placeholder | no-op |  |
| 377 | 0x179 | Job377 | placeholder | no-op |  |
| 378 | 0x17a | Job378 | placeholder | no-op |  |
| 379 | 0x17b | Job379 | placeholder | no-op |  |
| 380 | 0x17c | MeadMakerGoIn | high | handler 0x4f8b4f | ArriveAtWork for the mead maker; handler 0x4f8b4f, then 382 |
| 381 | 0x17d | Job381 | placeholder | no-op |  |
| 382 | 0x17e | MeadMakerStart | high | handler 0x4f8b5d | handler 0x4f8b5d: both inputs there and room in the output, then 383 |
| 383 | 0x17f | MeadMakerWork1 | high | handler 0x4f8c5c | handler 0x4f8c5c: animation only, then 384 |
| 384 | 0x180 | MeadMakerTakeInput1 | high | handler 0x4f8c77 | handler 0x4f8c77 takes input 0, then 385 |
| 385 | 0x181 | MeadMakerTakeInput2 | high | handler 0x4f8cfe | handler 0x4f8cfe takes input 1, then 386 |
| 386 | 0x182 | MeadMakerWork2 | high | handler 0x4f8d85 | handler 0x4f8d85: animation only, then 387 |
| 387 | 0x183 | MeadMakerStore | high | handler 0x4f8da0 | handler 0x4f8da0: Good::Mead onto the output pile; again (389) or 388 |
| 388 | 0x184 | MeadMakerEndRound | high | handler 0x4f8efa | handler 0x4f8efa leaves the tile, back to 382 |
| 389 | 0x185 | MeadMakerAgain | high | handler 0x4f8f2d | set by 387; handler 0x4f8f2d sets 384 |
| 390 | 0x186 | Job390 | placeholder | handler 0x4eeb17 |  |
| 391 | 0x187 | Job391 | placeholder | handler 0x4eeb17 |  |
| 392 | 0x188 | Job392 | placeholder | handler 0x4eeb17 |  |
| 393 | 0x189 | Job393 | placeholder | handler 0x4eeb17 |  |
| 394 | 0x18a | Job394 | placeholder | handler 0x4eeb17 |  |
| 395 | 0x18b | Job395 | placeholder | handler 0x4eeb24 |  |
| 396 | 0x18c | Job396 | placeholder | handler 0x4eeb24 |  |
| 397 | 0x18d | Job397 | placeholder | handler 0x4eeb24 |  |
| 398 | 0x18e | Job398 | placeholder | handler 0x4eeb24 |  |
| 399 | 0x18f | Job399 | placeholder | handler 0x4eeb24 |  |
| 400 | 0x190 | GongGoIn | high | handler 0x4f8f3b | ArriveAtWork for a smith at the large gong workshop; handler 0x4f8f3b, then 401 |
| 401 | 0x191 | GongFetch | high | handler 0x4f8f70 | handler 0x4f8f70 fetches boards, iron or gold for the gong, then 402 |
| 402 | 0x192 | GongProduction | high | handler 0x4f9122 | DebugLog "Gongproduktion owner %u" |
| 403 | 0x193 | ThiefSteal | high | handler 0x4f7a84 | DebugLog "Dieb[%u (%u,%u)]: StealGoodFromPile fail!" (Dieb = thief) |
| 404 | 0x194 | ThiefReturned | high | handler 0x4f7bdf | set by UnitIdle when a thief with loot is back on its own ground; handler 0x4f7bdf drops the loot and goes idle |

## Net messages (`NetMessage`, uint8) and commands (`CommandId`, uint8)

Byte 0 and byte 1 of the messages CLogicThread::ExecuteCommands (0x535990)
pops from CLogicThread+0x3c8. Analysis, payloads and the handler of every
id: `re/execute_commands.md`.

| value | name | confidence | evidence |
|-------|------|------------|----------|
| msg 1 | Command | high | dispatched on byte 1 (jump table 0x53b898); the recorder sees the same ids as 0x21 messages |
| msg 2 | StepGrant | high | sets this+0x3b4 and ends the loop (CNetBounceThread's "run up to step+5") |
| msg 3 | PlayerLost | high | DebugLog "Message PlayerLost", "LOGIC: Kill wegen lag" |
| msg 4 | FilmEnd | high | DebugLog "Message FilmEnd" |
| msg 5 | KillPlayer | high | DebugLog "Message KillPlayer" |
| msg 6 | InGameChat | high | DebugLog "Message InGameChat" |
| msg 8 | ChecksumFailure | high | DebugLog "Message ChecksumFailure" |
| msg 0x22 | LagReport | low | compares per-player words with 5000 and shows texts |
| 0x01 | PlaceBuilding | high | CheckBuildingSite(x, y, type, player, 1) |
| 0x02 | ScrollCamera | high | camera += deltas, ClampCamera |
| 0x03 | SetSettlerGroup | medium | writes player_lists[player][group] and its length |
| 0x04 | SetBuildingWorking | medium | request flag 0x01; off recalls the carriers |
| 0x05 | SetWorkArea | medium | work tile of production buildings (woodcutter, forester, ...) within distance 60 |
| 0x06 | DestroyBuilding | high | CrushBuilding |
| 0x07 | ChangeGoodsPriority | medium | area settings1[good] +-1, 0..20 |
| 0x08 | SetGoodsSetting | low | area settings2[good] = value |
| 0x09 | MoveUnits | high | soldier targets (SetSettlerTarget), "GongAttack" |
| 0x0a | ChangeTransportOrder | low | area settings4/5 order list |
| 0x0b | ChangeToolSettings | low | area bytes +0x23e..+0x244, shares in steps of 5 |
| 0x0c | ChangeGarrison | medium | towers/castle: wanted soldiers per class |
| 0x0d..0x0f | MoveUnitsMode1..3 | high | the same handler as 0x09 with a mode |
| 0x10 | ConvertCarriers | medium | carriers become types 0x16/0x17/0x28 or back |
| 0x11 | SetAreaCondition | medium | area condition_enabled[kind], ResetAreaConditionCounters |
| 0x12 | SetTradeRoute | medium | Trade route points and orders of harbour/marketplace |
| 0x13 | SetWorkshopProduction | low | production state of water buildings / shipyard |
| 0x14 | SetShipyardDock | medium | BlockBuildingLine + PlaceDockLine on a shipyard |
| 0x15 | CastSpell | high | DebugLog "GodlyEvent (ID#)%u GodRange: %u" |
| 0x16 | UnloadShip | medium | a ferry (type 0x2c) puts its passengers on the shore |
| 0x17 | SetHarbourDock | medium | as 0x14 for a harbour |
| 0x18 | PromoteSoldiers | medium | soldier levels +0xb8..+0xc0 for mana, SetPromotionCost |
| 0x19 | LoadShip | low | units from water buildings into a ship (+0x15 up to 6) |
| 0x1b | HoldPosition | low | soldiers get flag 0x20 and their position as home |
| 0x1c | SetStorehouseGoods | medium | storehouse (type 1) piles: ChangePileGood |
| 0xf1 | RevealMap | medium | global 0x7dfd84 = 1 (also set by SpellRevealMap) |
| 0xf2 | ClearPlayerFlag | placeholder | CLogicThread+0x235[player] = 0 |
| 0xf4 | SyncCheck | high | "CheckSum player %u", "Desync World!" ... |
| 0xf5 | PrioritizeSite | medium | sets all four request bits of a site |
| 0xf6 | UnprioritizeSite | medium | clears them |
| 0xf7 | SaveRequest | high | "Multiplayersave ..." |
| 0xf8 | SaveDone | high | "Multiplayersave was ok! Calling SaveGameCommit()" |
| 0xf9 | SaveFailed | high | "Multiplayersave failed Calling SaveGameRollBack()" |
| 0xfa | SaveAllowed | low | tick limit, pushes 0xf7 for the local player |
| 0xfb | SyncRandomCheck | medium | (was CheckVersion) another player's RNG history word (g_syncRandomHistory 0x7dfb58, stored by Step every 256 ticks) differs: text 0x80129, "Desync!", CLogicThread +0x675 = 1; every recording sends it at step 1 |
| 0xfc | PlayerLost | medium | Step sends it when g_gameOutcome is 0 (CheckGameEnd); crushes the military buildings |
| 0xfd | PlayerWon | medium | Step sends it when g_gameOutcome is 1 |
| 0xfe | DropPlayer | high | "LOGIC: Dropped player %i %i", "Neuer Computer %u bei step %u" |
| 0xff | PlayerLeft | medium | the same handling as 0xfe |
