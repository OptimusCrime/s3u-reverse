# Category values: settler jobs and types, buildings, goods, races

The named values of `src/s3re/game/enums.h`, with the evidence for each
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
   (`SettlerJob::Job309`, `SettlerType::Type20`, `Good::Good31`) so they can
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
  gong** (worker: smith; "Gong bauen" at 0x32). The layout view keeps its
  old name for now (TODO.md).
- 0xc/0xd "tile drop buildings" are the **tool smith and weapon smith**;
  0x13 is the **barracks**.
- **Settler type 0x29 is the catapult** (session 45 rollout): the
  "SettlerType29" in `FindNearbySettlerType29` / `IsFreeOfSettlerType29Nearby`
  means catapult; the seven extra tiles of the war machine workshops
  (TakeBuildingExtraTiles) are where the machine stands. `UnblockType12BuildingTiles`
  works on **castles** (type 18). Terrain 0x16, which `SetTerrain16Patch`
  makes (jobs 145, 230, 247 of the farmer, rice farmer and winegrower), is a
  **field**, and UpdateEnvironment turns a field without growing objects (0x55..0x6f) around back
  into grass. The function names are kept for now (TODO.md).

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
| 37 | 0x25 | Type37 | 99 | placeholder | |
| 38 | 0x26 | RiceFarmer | 0 | high | building 36 |
| 39 | 0x27 | Donkey | 30 | medium | list |
| 40 | 0x28 | Pioneer | 21 | medium | list; claims tiles (job PioneerWork, SetOwnerOfFreeTile) |
| 41 | 0x29 | Catapult | 40 | high | seven tiles, health 250, "Angriff Katapult" |
| 42 | 0x2a | Type42 | 41 | placeholder | health 0 |
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
| 12..17 | Shovel, Hammer, Axe, Pickaxe, Saw, FishingRod | list | tools (tool smith) |
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
| 32 | Scythe | list | |
| 33 | RiceLiquor | high | Asian offering |
| 34 | Mead | high | Amazon offering |
| 35 | Honey | high | meadery input, Amazon mine food |
| 255 | Empty | high | building pile without a goods type |

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
| 0..3 | Spell0..Spell3 | placeholder | handler 0x548340 (spell passed on) |
| 4 | Spell4 | placeholder | handler 0x548fb0 |
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

405 values (0..0x194), 40 named, the rest placeholders. "walking" =
handled by WalkSettler (IsWalkingJob's list); "no-op" = straight to the
loop end. Handlers and job chains: `re/update_settlers_jobs.csv` and
`re/update_settlers.md`.

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
| 8 | 0x8 | Job8 | placeholder | handler 0x4ebab8 |  |
| 9 | 0x9 | Job9 | placeholder | handler 0x4ebab8 |  |
| 10 | 0xa | CarrierWalkToPile | high | walking, handler 0x4ead27 | DistributeGoods (UpdateEconomy) sends a carrier to a pile with job 10, the goods type and the target building |
| 11 | 0xb | CarrierAtPile | medium | handler 0x4ec00f | next job of 10 on arrival; animation, then 0xa9 |
| 12 | 0xc | CarrierWalkToBuilding | medium | walking, handler 0x4ead27 | walking with a carried good (ResetSettlerToIdle drops it); three times the foot traffic |
| 13 | 0xd | CarrierAtBuilding | medium | handler 0x4ec27e | next job of 0xc on arrival; animation, then 0x50 |
| 14 | 0xe | BuilderStart | high | handler 0x4ec58d | start job of type 2 (builder) in job 4; builder jobs 0xe/0xf/0x11/0x12 hold a builder slot (ResetSettlerToIdle) |
| 15 | 0xf | Job15 | placeholder | walking, handler 0x4ead27 |  |
| 16 | 0x10 | Job16 | placeholder | handler 0x4ecc6a |  |
| 17 | 0x11 | Job17 | placeholder | walking, handler 0x4ead27 |  |
| 18 | 0x12 | BuilderBuild | high | handler 0x4ec960 | handler 0x4ec960 calls BuildingDone; a builder job (builder slots) |
| 19 | 0x13 | Job19 | placeholder | handler 0x4ece32 |  |
| 20 | 0x14 | Job20 | placeholder | handler 0x4ecea1 |  |
| 21 | 0x15 | Job21 | placeholder | handler 0x4ecf3c |  |
| 22 | 0x16 | Job22 | placeholder | handler 0x4ecf66 |  |
| 23 | 0x17 | Job23 | placeholder | handler 0x4f8b7c |  |
| 24 | 0x18 | Job24 | placeholder | handler 0x4ecf82 |  |
| 25 | 0x19 | Job25 | placeholder | handler 0x4ed213 |  |
| 26 | 0x1a | Job26 | placeholder | handler 0x4edffb |  |
| 27 | 0x1b | Job27 | placeholder | handler 0x4edadd |  |
| 28 | 0x1c | Job28 | placeholder | walking, handler 0x4ead27 |  |
| 29 | 0x1d | Job29 | placeholder | walking, handler 0x4ead27 |  |
| 30 | 0x1e | Job30 | placeholder | handler 0x4ed4a4 |  |
| 31 | 0x1f | Job31 | placeholder | handler 0x4ed4c3 |  |
| 32 | 0x20 | Job32 | placeholder | handler 0x4ed4e2 |  |
| 33 | 0x21 | Job33 | placeholder | handler 0x4ed58e |  |
| 34 | 0x22 | Job34 | placeholder | handler 0x4ed53b |  |
| 35 | 0x23 | Job35 | placeholder | handler 0x4ed7c7 |  |
| 36 | 0x24 | Job36 | placeholder | handler 0x4ed92f |  |
| 37 | 0x25 | Job37 | placeholder | handler 0x4ed971 |  |
| 38 | 0x26 | Job38 | placeholder | handler 0x4ed951 |  |
| 39 | 0x27 | Job39 | placeholder | handler 0x4eda06 |  |
| 40 | 0x28 | Job40 | placeholder | handler 0x4eda48 |  |
| 41 | 0x29 | Job41 | placeholder | handler 0x4eda28 |  |
| 42 | 0x2a | Job42 | placeholder | walking, handler 0x4ead27 |  |
| 43 | 0x2b | Job43 | placeholder | walking, handler 0x4ead27 |  |
| 44 | 0x2c | Job44 | placeholder | handler 0x4eddbe |  |
| 45 | 0x2d | Job45 | placeholder | walking, handler 0x4ead27 |  |
| 46 | 0x2e | Job46 | placeholder | handler 0x4edde4 |  |
| 47 | 0x2f | Job47 | placeholder | handler 0x4edf93 |  |
| 48 | 0x30 | Job48 | placeholder | walking, handler 0x4ead27 |  |
| 49 | 0x31 | Job49 | placeholder | handler 0x4ee0c1 |  |
| 50 | 0x32 | Job50 | placeholder | handler 0x4ee114 |  |
| 51 | 0x33 | Job51 | placeholder | handler 0x4ee21f |  |
| 52 | 0x34 | Job52 | placeholder | handler 0x4ee0c1 |  |
| 53 | 0x35 | UnitWalk | medium | walking, handler 0x4eacfc | walking job of units in the player's idle lists (JobAfter01Or37: 0x37 -> 0x35, like 1 -> 2); on arrival the walk states of +0x1a (board a ship, shoot, thief) |
| 54 | 0x36 | Job54 | placeholder | handler 0x4eef33 |  |
| 55 | 0x37 | UnitIdle | high | handler 0x4e98be | idle soldier or special unit (class != 0); job 0 sets it, it wanders (handler 0x4e98be) |
| 56 | 0x38 | Job56 | placeholder | handler 0x4ecf4a |  |
| 57 | 0x39 | Job57 | placeholder | no-op |  |
| 58 | 0x3a | Job58 | placeholder | handler 0x4ee35a |  |
| 59 | 0x3b | Job59 | placeholder | handler 0x4ee3de |  |
| 60 | 0x3c | Job60 | placeholder | handler 0x4ee4cf |  |
| 61 | 0x3d | Job61 | placeholder | handler 0x4ee4e7 |  |
| 62 | 0x3e | Job62 | placeholder | handler 0x4ee4ff |  |
| 63 | 0x3f | Job63 | placeholder | handler 0x4ee698 |  |
| 64 | 0x40 | Job64 | placeholder | handler 0x4ee35a |  |
| 65 | 0x41 | Job65 | placeholder | handler 0x4ee252 |  |
| 66 | 0x42 | Job66 | placeholder | handler 0x4ecf58 |  |
| 67 | 0x43 | Job67 | placeholder | no-op |  |
| 68 | 0x44 | Job68 | placeholder | handler 0x4ee7d3 |  |
| 69 | 0x45 | Job69 | placeholder | handler 0x4ee857 |  |
| 70 | 0x46 | Job70 | placeholder | handler 0x4ee948 |  |
| 71 | 0x47 | Job71 | placeholder | handler 0x4ee960 |  |
| 72 | 0x48 | Job72 | placeholder | handler 0x4ee978 |  |
| 73 | 0x49 | Job73 | placeholder | handler 0x4eeaca |  |
| 74 | 0x4a | Job74 | placeholder | handler 0x4ee7d3 |  |
| 75 | 0x4b | Job75 | placeholder | handler 0x4ee6cb |  |
| 76 | 0x4c | Job76 | placeholder | walking, handler 0x4ead27 |  |
| 77 | 0x4d | Job77 | placeholder | handler 0x4ec1f6 |  |
| 78 | 0x4e | Job78 | placeholder | walking, handler 0x4ead27 |  |
| 79 | 0x4f | Job79 | placeholder | handler 0x4ec3fb |  |
| 80 | 0x50 | CarrierDeliver | medium | handler 0x4ec2a0 | next job of 0xd; AddGoodsToPile; a carried-good job in ResetSettlerToIdle |
| 81 | 0x51 | Job81 | placeholder | handler 0x4ec67f |  |
| 82 | 0x52 | MinerEnterCoalMine | medium | handler 0x4eeafd | job 4, type 10 (miner), building type 7 (coal mine); MinerJobEnter |
| 83 | 0x53 | MinerEnterGoldMine | medium | handler 0x4eeafd | job 4, type 10, building type 8 (gold mine) |
| 84 | 0x54 | MinerEnterIronMine | medium | handler 0x4eeafd | job 4, type 10, building type 9 (iron mine) |
| 85 | 0x55 | Job85 | placeholder | handler 0x4eeb0a |  |
| 86 | 0x56 | Job86 | placeholder | handler 0x4eeb0a |  |
| 87 | 0x57 | Job87 | placeholder | handler 0x4eeb0a |  |
| 88 | 0x58 | Job88 | placeholder | handler 0x4eeb31 |  |
| 89 | 0x59 | Job89 | placeholder | handler 0x4eeb3e |  |
| 90 | 0x5a | Job90 | placeholder | handler 0x4eeb71 |  |
| 91 | 0x5b | Job91 | placeholder | handler 0x4eeb3e |  |
| 92 | 0x5c | Job92 | placeholder | handler 0x4eeb31 |  |
| 93 | 0x5d | Job93 | placeholder | handler 0x4eebff |  |
| 94 | 0x5e | Job94 | placeholder | handler 0x4eec32 |  |
| 95 | 0x5f | Job95 | placeholder | handler 0x4eebff |  |
| 96 | 0x60 | Job96 | placeholder | handler 0x4eeb31 |  |
| 97 | 0x61 | Job97 | placeholder | handler 0x4eecc0 |  |
| 98 | 0x62 | Job98 | placeholder | handler 0x4eecf3 |  |
| 99 | 0x63 | Job99 | placeholder | handler 0x4eecc0 |  |
| 100 | 0x64 | LeaveHouse | medium | handler 0x4eef09 | set for a settler created at the door of a residence or big temple (UpdateEconomy); handler only clears the job |
| 101 | 0x65 | Job101 | placeholder | handler 0x4ecf74 |  |
| 102 | 0x66 | Job102 | placeholder | handler 0x4ece93 |  |
| 103 | 0x67 | Job103 | placeholder | handler 0x4ecf09 |  |
| 104 | 0x68 | CarrierWalkToTool | medium | walking, handler 0x4ead27 | walking; a carrier fetching a tool (Settler::tool_type is the type it becomes) |
| 105 | 0x69 | CarrierTakeTool | medium | handler 0x4ebe0f | next job of 0x68; SubtractPlayerCounter, then job 3 |
| 106 | 0x6a | Job106 | placeholder | handler 0x4ecc94 |  |
| 107 | 0x6b | Job107 | placeholder | handler 0x4ef162 |  |
| 108 | 0x6c | Job108 | placeholder | handler 0x4f10be |  |
| 109 | 0x6d | Job109 | placeholder | handler 0x4f10cc |  |
| 110 | 0x6e | Job110 | placeholder | handler 0x4f10da |  |
| 111 | 0x6f | Job111 | placeholder | handler 0x4f17ad |  |
| 112 | 0x70 | Job112 | placeholder | handler 0x4f1a39 |  |
| 113 | 0x71 | Job113 | placeholder | handler 0x4f22d2 |  |
| 114 | 0x72 | Job114 | placeholder | handler 0x4f2734 |  |
| 115 | 0x73 | Job115 | placeholder | handler 0x4f2aff |  |
| 116 | 0x74 | Job116 | placeholder | handler 0x4ef170 |  |
| 117 | 0x75 | Job117 | placeholder | handler 0x4f0816 |  |
| 118 | 0x76 | Job118 | placeholder | handler 0x4f2b0d |  |
| 119 | 0x77 | Job119 | placeholder | handler 0x4f10e8 |  |
| 120 | 0x78 | Job120 | placeholder | handler 0x4f17bb |  |
| 121 | 0x79 | Job121 | placeholder | handler 0x4f1a47 |  |
| 122 | 0x7a | Job122 | placeholder | handler 0x4f22e0 |  |
| 123 | 0x7b | Job123 | placeholder | handler 0x4f2742 |  |
| 124 | 0x7c | Job124 | placeholder | handler 0x4f2fa0 |  |
| 125 | 0x7d | Job125 | placeholder | handler 0x4ef41b |  |
| 126 | 0x7e | Job126 | placeholder | handler 0x4ef46e |  |
| 127 | 0x7f | Job127 | placeholder | handler 0x4ef486 |  |
| 128 | 0x80 | Job128 | placeholder | handler 0x4ef53d |  |
| 129 | 0x81 | Job129 | placeholder | handler 0x4f0036 |  |
| 130 | 0x82 | Job130 | placeholder | handler 0x4ef41b |  |
| 131 | 0x83 | Job131 | placeholder | handler 0x4f09f7 |  |
| 132 | 0x84 | Job132 | placeholder | handler 0x4f0a4d |  |
| 133 | 0x85 | Job133 | placeholder | handler 0x4f0a68 |  |
| 134 | 0x86 | Job134 | placeholder | handler 0x4f0b1f |  |
| 135 | 0x87 | Job135 | placeholder | handler 0x4f108b |  |
| 136 | 0x88 | Job136 | placeholder | handler 0x4f09f7 |  |
| 137 | 0x89 | Job137 | placeholder | no-op |  |
| 138 | 0x8a | RecruitAtBarracks | medium | handler 0x4ebac6 | job 4 start for type 0 (carrier): a carrier sent by the barracks (UpdateEconomy) to become a soldier; its saved_counter is the input slot |
| 139 | 0x8b | Job139 | placeholder | handler 0x4ecd51 |  |
| 140 | 0x8c | Job140 | placeholder | handler 0x4ece16 |  |
| 141 | 0x8d | Job141 | placeholder | handler 0x4f8b7c |  |
| 142 | 0x8e | Job142 | placeholder | handler 0x4e9884 |  |
| 143 | 0x8f | Job143 | placeholder | handler 0x4f1e6d |  |
| 144 | 0x90 | Job144 | placeholder | walking, handler 0x4ead27 |  |
| 145 | 0x91 | Job145 | placeholder | handler 0x4f1e93 |  |
| 146 | 0x92 | Job146 | placeholder | handler 0x4f20d5 |  |
| 147 | 0x93 | Job147 | placeholder | walking, handler 0x4ead27 |  |
| 148 | 0x94 | Job148 | placeholder | handler 0x4f2108 |  |
| 149 | 0x95 | Job149 | placeholder | handler 0x4f2116 |  |
| 150 | 0x96 | Job150 | placeholder | handler 0x4f213c |  |
| 151 | 0x97 | Job151 | placeholder | handler 0x4f218a |  |
| 152 | 0x98 | Job152 | placeholder | walking, handler 0x4ead27 |  |
| 153 | 0x99 | Job153 | placeholder | handler 0x4f21f5 |  |
| 154 | 0x9a | Job154 | placeholder | handler 0x4f223a |  |
| 155 | 0x9b | Job155 | placeholder | handler 0x4f221a |  |
| 156 | 0x9c | Job156 | placeholder | walking, handler 0x4ead27 |  |
| 157 | 0x9d | Job157 | placeholder | handler 0x4f2108 |  |
| 158 | 0x9e | Job158 | placeholder | handler 0x4f23a9 |  |
| 159 | 0x9f | Job159 | placeholder | handler 0x4f24bf |  |
| 160 | 0xa0 | Job160 | placeholder | handler 0x4f24da |  |
| 161 | 0xa1 | Job161 | placeholder | handler 0x4f25ec |  |
| 162 | 0xa2 | Job162 | placeholder | handler 0x4f261f |  |
| 163 | 0xa3 | Job163 | placeholder | handler 0x4f284d |  |
| 164 | 0xa4 | Job164 | placeholder | handler 0x4f28a3 |  |
| 165 | 0xa5 | Job165 | placeholder | handler 0x4f295a |  |
| 166 | 0xa6 | Job166 | placeholder | handler 0x4f2975 |  |
| 167 | 0xa7 | Job167 | placeholder | handler 0x4f2acc |  |
| 168 | 0xa8 | Job168 | placeholder | handler 0x4f284d |  |
| 169 | 0xa9 | CarrierPickUp | medium | handler 0x4ec031 | next job of 0xb; RestoreSettlerAnimation, then 0xaa |
| 170 | 0xaa | CarrierLeavePile | medium | handler 0x4ec086 | next job of 0xa9; SetSettlerTarget to the building, then 0xc; carries the good |
| 171 | 0xab | WalkToOwnArea | medium | walking, handler 0x4ead27 | walking; on arrival in its own area the job is cleared, else health -2 and job 0xac (WalkSettler) |
| 172 | 0xac | SeekOwnArea | medium | handler 0x4ea55d | set by ResetSettlerToIdle; handler picks a target (Random, SetSettlerTarget) and walks there with 0xab |
| 173 | 0xad | Job173 | placeholder | handler 0x4ea531 |  |
| 174 | 0xae | GuardBuilding | low | handler 0x4ea314 | a soldier in this job guards a military building against shots (WalkSettler's target search) |
| 175 | 0xaf | Job175 | placeholder | handler 0x4ef005 |  |
| 176 | 0xb0 | Job176 | placeholder | walking, handler 0x4eacd2 |  |
| 177 | 0xb1 | Job177 | placeholder | handler 0x4f2d69 |  |
| 178 | 0xb2 | Job178 | placeholder | walking, handler 0x4ead27 |  |
| 179 | 0xb3 | Job179 | placeholder | handler 0x4f2d88 |  |
| 180 | 0xb4 | Job180 | placeholder | handler 0x4f2de1 |  |
| 181 | 0xb5 | Job181 | placeholder | walking, handler 0x4ead27 |  |
| 182 | 0xb6 | Job182 | placeholder | handler 0x4f2eb5 |  |
| 183 | 0xb7 | Job183 | placeholder | handler 0x4f2efa |  |
| 184 | 0xb8 | Job184 | placeholder | handler 0x4f2eda |  |
| 185 | 0xb9 | Job185 | placeholder | handler 0x4f2f92 |  |
| 186 | 0xba | Job186 | placeholder | handler 0x4f31c5 |  |
| 187 | 0xbb | Job187 | placeholder | walking, handler 0x4ead27 |  |
| 188 | 0xbc | Job188 | placeholder | walking, handler 0x4ead27 |  |
| 189 | 0xbd | Job189 | placeholder | handler 0x4f31e4 |  |
| 190 | 0xbe | Job190 | placeholder | handler 0x4f3261 |  |
| 191 | 0xbf | Job191 | placeholder | handler 0x4f326f |  |
| 192 | 0xc0 | Job192 | placeholder | handler 0x4f32b4 |  |
| 193 | 0xc1 | Job193 | placeholder | handler 0x4f3294 |  |
| 194 | 0xc2 | Job194 | placeholder | handler 0x4f334c |  |
| 195 | 0xc3 | Job195 | placeholder | handler 0x4f1884 |  |
| 196 | 0xc4 | Job196 | placeholder | handler 0x4f18da |  |
| 197 | 0xc5 | Job197 | placeholder | handler 0x4f18f5 |  |
| 198 | 0xc6 | Job198 | placeholder | handler 0x4f1a06 |  |
| 199 | 0xc7 | Job199 | placeholder | handler 0x4f1884 |  |
| 200 | 0xc8 | Job200 | placeholder | handler 0x4f125f |  |
| 201 | 0xc9 | Job201 | placeholder | handler 0x4f12b5 |  |
| 202 | 0xca | Job202 | placeholder | handler 0x4f130b |  |
| 203 | 0xcb | Job203 | placeholder | handler 0x4f1352 |  |
| 204 | 0xcc | Job204 | placeholder | handler 0x4f1394 |  |
| 205 | 0xcd | Job205 | placeholder | handler 0x4f1425 |  |
| 206 | 0xce | Job206 | placeholder | handler 0x4f15bf |  |
| 207 | 0xcf | Job207 | placeholder | handler 0x4f1615 |  |
| 208 | 0xd0 | Job208 | placeholder | handler 0x4f166b |  |
| 209 | 0xd1 | Job209 | placeholder | handler 0x4f16df |  |
| 210 | 0xd2 | Job210 | placeholder | handler 0x4f1721 |  |
| 211 | 0xd3 | Job211 | placeholder | handler 0x4f177a |  |
| 212 | 0xd4 | GeologistWork | medium | handler 0x4f335a | type 0x16 (geologist) on a free mountain tile (terrain class 0x20) starts it (WalkSettler) |
| 213 | 0xd5 | Job213 | placeholder | handler 0x4f376b |  |
| 214 | 0xd6 | MinerEnterGemMine | medium | handler 0x4eeafd | job 4, type 10, building type 0x25 (gem mine) |
| 215 | 0xd7 | MinerEnterSulfurMine | medium | handler 0x4eeafd | job 4, type 10, building types 0x1d / 0x38 (sulfur mines) |
| 216 | 0xd8 | Job216 | placeholder | handler 0x4eeb0a |  |
| 217 | 0xd9 | Job217 | placeholder | handler 0x4eeb0a |  |
| 218 | 0xda | Job218 | placeholder | handler 0x4eeb31 |  |
| 219 | 0xdb | Job219 | placeholder | handler 0x4eed81 |  |
| 220 | 0xdc | Job220 | placeholder | handler 0x4eedb4 |  |
| 221 | 0xdd | Job221 | placeholder | handler 0x4eed81 |  |
| 222 | 0xde | Job222 | placeholder | handler 0x4eeb31 |  |
| 223 | 0xdf | Job223 | placeholder | handler 0x4eee45 |  |
| 224 | 0xe0 | Job224 | placeholder | handler 0x4eee78 |  |
| 225 | 0xe1 | Job225 | placeholder | handler 0x4eee45 |  |
| 226 | 0xe2 | Job226 | placeholder | handler 0x4f3d45 |  |
| 227 | 0xe3 | Job227 | placeholder | handler 0x4f3d53 |  |
| 228 | 0xe4 | Job228 | placeholder | handler 0x4f4071 |  |
| 229 | 0xe5 | Job229 | placeholder | walking, handler 0x4ead27 |  |
| 230 | 0xe6 | Job230 | placeholder | handler 0x4f4097 |  |
| 231 | 0xe7 | Job231 | placeholder | handler 0x4f4226 |  |
| 232 | 0xe8 | Job232 | placeholder | walking, handler 0x4ead27 |  |
| 233 | 0xe9 | Job233 | placeholder | handler 0x4f4259 |  |
| 234 | 0xea | Job234 | placeholder | handler 0x4f4267 |  |
| 235 | 0xeb | Job235 | placeholder | handler 0x4f428d |  |
| 236 | 0xec | Job236 | placeholder | handler 0x4f42db |  |
| 237 | 0xed | Job237 | placeholder | walking, handler 0x4ead27 |  |
| 238 | 0xee | Job238 | placeholder | handler 0x4f4383 |  |
| 239 | 0xef | Job239 | placeholder | handler 0x4f43cb |  |
| 240 | 0xf0 | Job240 | placeholder | handler 0x4f43a8 |  |
| 241 | 0xf1 | Job241 | placeholder | walking, handler 0x4ead27 |  |
| 242 | 0xf2 | Job242 | placeholder | handler 0x4f4259 |  |
| 243 | 0xf3 | Job243 | placeholder | handler 0x4f4463 |  |
| 244 | 0xf4 | Job244 | placeholder | handler 0x4f4471 |  |
| 245 | 0xf5 | Job245 | placeholder | handler 0x4f49f3 |  |
| 246 | 0xf6 | Job246 | placeholder | walking, handler 0x4ead27 |  |
| 247 | 0xf7 | Job247 | placeholder | handler 0x4f4a19 |  |
| 248 | 0xf8 | Job248 | placeholder | handler 0x4f4d54 |  |
| 249 | 0xf9 | Job249 | placeholder | walking, handler 0x4ead27 |  |
| 250 | 0xfa | Job250 | placeholder | handler 0x4f4d87 |  |
| 251 | 0xfb | Job251 | placeholder | handler 0x4f4d95 |  |
| 252 | 0xfc | Job252 | placeholder | handler 0x4f4dbb |  |
| 253 | 0xfd | Job253 | placeholder | handler 0x4f4e09 |  |
| 254 | 0xfe | Job254 | placeholder | walking, handler 0x4ead27 |  |
| 255 | 0xff | Job255 | placeholder | handler 0x4f4e5f |  |
| 256 | 0x100 | Job256 | placeholder | handler 0x4f4e90 |  |
| 257 | 0x101 | Job257 | placeholder | handler 0x4f4eb3 |  |
| 258 | 0x102 | Job258 | placeholder | walking, handler 0x4ead27 |  |
| 259 | 0x103 | Job259 | placeholder | handler 0x4f4d87 |  |
| 260 | 0x104 | Job260 | placeholder | handler 0x4f4f4b |  |
| 261 | 0x105 | Job261 | placeholder | handler 0x4f4f6e |  |
| 262 | 0x106 | Job262 | placeholder | no-op |  |
| 263 | 0x107 | Job263 | placeholder | handler 0x4f5087 |  |
| 264 | 0x108 | Job264 | placeholder | handler 0x4f50dd |  |
| 265 | 0x109 | Job265 | placeholder | handler 0x4f5133 |  |
| 266 | 0x10a | Job266 | placeholder | handler 0x4f528a |  |
| 267 | 0x10b | Job267 | placeholder | handler 0x4f5087 |  |
| 268 | 0x10c | Job268 | placeholder | handler 0x4f560c |  |
| 269 | 0x10d | Job269 | placeholder | no-op |  |
| 270 | 0x10e | Job270 | placeholder | handler 0x4f5725 |  |
| 271 | 0x10f | Job271 | placeholder | handler 0x4f577b |  |
| 272 | 0x110 | Job272 | placeholder | handler 0x4f57d1 |  |
| 273 | 0x111 | Job273 | placeholder | handler 0x4f5928 |  |
| 274 | 0x112 | Job274 | placeholder | handler 0x4f5725 |  |
| 275 | 0x113 | Job275 | placeholder | handler 0x4f52bd |  |
| 276 | 0x114 | Job276 | placeholder | no-op |  |
| 277 | 0x115 | Job277 | placeholder | handler 0x4f53d6 |  |
| 278 | 0x116 | Job278 | placeholder | handler 0x4f542c |  |
| 279 | 0x117 | Job279 | placeholder | handler 0x4f5482 |  |
| 280 | 0x118 | Job280 | placeholder | handler 0x4f55d9 |  |
| 281 | 0x119 | Job281 | placeholder | handler 0x4f53d6 |  |
| 282 | 0x11a | Job282 | placeholder | handler 0x4f595b |  |
| 283 | 0x11b | Job283 | placeholder | no-op |  |
| 284 | 0x11c | Job284 | placeholder | handler 0x4f5abe |  |
| 285 | 0x11d | Job285 | placeholder | handler 0x4f5b73 |  |
| 286 | 0x11e | Job286 | placeholder | handler 0x4f5e81 |  |
| 287 | 0x11f | Job287 | placeholder | handler 0x4f5eb4 |  |
| 288 | 0x120 | Job288 | placeholder | handler 0x4f4f7c |  |
| 289 | 0x121 | Job289 | placeholder | handler 0x4f561a |  |
| 290 | 0x122 | Job290 | placeholder | handler 0x4f52cb |  |
| 291 | 0x123 | Job291 | placeholder | handler 0x4f5969 |  |
| 292 | 0x124 | Job292 | placeholder | handler 0x4f5bc9 |  |
| 293 | 0x125 | Job293 | placeholder | handler 0x4f5c1f |  |
| 294 | 0x126 | Job294 | placeholder | handler 0x4f5c75 |  |
| 295 | 0x127 | Job295 | placeholder | handler 0x4f5ccb |  |
| 296 | 0x128 | Job296 | placeholder | handler 0x4f0069 |  |
| 297 | 0x129 | Job297 | placeholder | handler 0x4f0077 |  |
| 298 | 0x12a | Job298 | placeholder | handler 0x4f034e |  |
| 299 | 0x12b | Job299 | placeholder | handler 0x4f035c |  |
| 300 | 0x12c | Job300 | placeholder | handler 0x4f05b2 |  |
| 301 | 0x12d | Job301 | placeholder | handler 0x4f05c0 |  |
| 302 | 0x12e | PioneerWork | medium | handler 0x4f37bd | type 0x28 (pioneer) on a tile of no or another owner starts it (WalkSettler); handler calls SetOwnerOfFreeTile |
| 303 | 0x12f | Job303 | placeholder | handler 0x4f3cf3 |  |
| 304 | 0x130 | Job304 | placeholder | handler 0x4f5f6b |  |
| 305 | 0x131 | Job305 | placeholder | handler 0x4f1458 |  |
| 306 | 0x132 | Job306 | placeholder | handler 0x4f1466 |  |
| 307 | 0x133 | Job307 | placeholder | walking, handler 0x4ead27 |  |
| 308 | 0x134 | Job308 | placeholder | handler 0x4f5f79 |  |
| 309 | 0x135 | Job309 | placeholder | walking, handler 0x4ead27 |  |
| 310 | 0x136 | Job310 | placeholder | handler 0x4f6255 |  |
| 311 | 0x137 | CatapultProduction | high | handler 0x4f0229 | DebugLog "Katapultproduktion owner %u" |
| 312 | 0x138 | Job312 | placeholder | handler 0x4f04cc |  |
| 313 | 0x139 | Job313 | placeholder | handler 0x4f0730 |  |
| 314 | 0x13a | Job314 | placeholder | handler 0x4f6e84 |  |
| 315 | 0x13b | Job315 | placeholder | handler 0x4f6e92 |  |
| 316 | 0x13c | Job316 | placeholder | handler 0x4f6ff4 |  |
| 317 | 0x13d | Job317 | placeholder | handler 0x4f70ae |  |
| 318 | 0x13e | Job318 | placeholder | handler 0x4f7051 |  |
| 319 | 0x13f | Job319 | placeholder | handler 0x4f70ae |  |
| 320 | 0x140 | Job320 | placeholder | walking, handler 0x4ead27 |  |
| 321 | 0x141 | Job321 | placeholder | walking, handler 0x4ead27 |  |
| 322 | 0x142 | Job322 | placeholder | handler 0x4f73b2 |  |
| 323 | 0x143 | Job323 | placeholder | handler 0x4f70d4 |  |
| 324 | 0x144 | Job324 | placeholder | handler 0x4f735e |  |
| 325 | 0x145 | Job325 | placeholder | handler 0x4f8b7c |  |
| 326 | 0x146 | OnBoard | high | handler 0x4f8b7c | a settler that boarded a ferry (WalkSettler BoardShip) |
| 327 | 0x147 | Job327 | placeholder | walking, handler 0x4ead27 |  |
| 328 | 0x148 | Job328 | placeholder | walking, handler 0x4ead27 |  |
| 329 | 0x149 | Job329 | placeholder | walking, handler 0x4ead27 |  |
| 330 | 0x14a | Job330 | placeholder | walking, handler 0x4ead27 |  |
| 331 | 0x14b | Job331 | placeholder | handler 0x4f6514 |  |
| 332 | 0x14c | ShipUnload | high | handler 0x4f6a18 | DebugLog "Schiff laedt ab" (ship unloads) |
| 333 | 0x14d | Job333 | placeholder | handler 0x4f745b |  |
| 334 | 0x14e | Job334 | placeholder | handler 0x4f776f |  |
| 335 | 0x14f | Job335 | placeholder | handler 0x4f780f |  |
| 336 | 0x150 | Job336 | placeholder | handler 0x4f73ec |  |
| 337 | 0x151 | Shoot | high | handler 0x4f7842 | a soldier shooting from its post (WalkSettler SoldierShoot: projectile, effect record) |
| 338 | 0x152 | ThiefChoosePile | medium | handler 0x4f78b0 | walk state 3 of type 0x17 (thief); AddPileClaim, then 0x193 |
| 339 | 0x153 | Job339 | placeholder | handler 0x4f8399 |  |
| 340 | 0x154 | Job340 | placeholder | no-op |  |
| 341 | 0x155 | Job341 | placeholder | handler 0x4f83a7 |  |
| 342 | 0x156 | Job342 | placeholder | handler 0x4f8a95 |  |
| 343 | 0x157 | Job343 | placeholder | handler 0x4f84f8 |  |
| 344 | 0x158 | Job344 | placeholder | handler 0x4f8513 |  |
| 345 | 0x159 | Job345 | placeholder | handler 0x4f859a |  |
| 346 | 0x15a | Job346 | placeholder | handler 0x4f85b5 |  |
| 347 | 0x15b | Job347 | placeholder | handler 0x4f876b |  |
| 348 | 0x15c | Job348 | placeholder | handler 0x4f876b |  |
| 349 | 0x15d | Job349 | placeholder | handler 0x4f876b |  |
| 350 | 0x15e | Job350 | placeholder | handler 0x4f876b |  |
| 351 | 0x15f | Job351 | placeholder | handler 0x4f8a95 |  |
| 352 | 0x160 | Job352 | placeholder | handler 0x4f8b1c |  |
| 353 | 0x161 | Job353 | placeholder | handler 0x4f7c4b |  |
| 354 | 0x162 | Job354 | placeholder | no-op |  |
| 355 | 0x163 | Job355 | placeholder | handler 0x4f7c59 |  |
| 356 | 0x164 | Job356 | placeholder | handler 0x4f8065 |  |
| 357 | 0x165 | Job357 | placeholder | walking, handler 0x4ead27 |  |
| 358 | 0x166 | Job358 | placeholder | handler 0x4f8084 |  |
| 359 | 0x167 | Job359 | placeholder | handler 0x4f8181 |  |
| 360 | 0x168 | Job360 | placeholder | walking, handler 0x4ead27 |  |
| 361 | 0x169 | Job361 | placeholder | handler 0x4f81c4 |  |
| 362 | 0x16a | Job362 | placeholder | walking, handler 0x4ead27 |  |
| 363 | 0x16b | Job363 | placeholder | handler 0x4f81e3 |  |
| 364 | 0x16c | Job364 | placeholder | handler 0x4f8246 |  |
| 365 | 0x16d | Job365 | placeholder | walking, handler 0x4ead27 |  |
| 366 | 0x16e | Job366 | placeholder | handler 0x4f82c1 |  |
| 367 | 0x16f | Job367 | placeholder | handler 0x4f82ea |  |
| 368 | 0x170 | Job368 | placeholder | handler 0x4eb3d2 |  |
| 369 | 0x171 | Job369 | placeholder | handler 0x4f838b |  |
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
| 380 | 0x17c | Job380 | placeholder | handler 0x4f8b4f |  |
| 381 | 0x17d | Job381 | placeholder | no-op |  |
| 382 | 0x17e | Job382 | placeholder | handler 0x4f8b5d |  |
| 383 | 0x17f | Job383 | placeholder | handler 0x4f8c5c |  |
| 384 | 0x180 | Job384 | placeholder | handler 0x4f8c77 |  |
| 385 | 0x181 | Job385 | placeholder | handler 0x4f8cfe |  |
| 386 | 0x182 | Job386 | placeholder | handler 0x4f8d85 |  |
| 387 | 0x183 | Job387 | placeholder | handler 0x4f8da0 |  |
| 388 | 0x184 | Job388 | placeholder | handler 0x4f8efa |  |
| 389 | 0x185 | Job389 | placeholder | handler 0x4f8f2d |  |
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
| 400 | 0x190 | Job400 | placeholder | handler 0x4f8f3b |  |
| 401 | 0x191 | Job401 | placeholder | handler 0x4f8f70 |  |
| 402 | 0x192 | GongProduction | high | handler 0x4f9122 | DebugLog "Gongproduktion owner %u" |
| 403 | 0x193 | ThiefSteal | high | handler 0x4f7a84 | DebugLog "Dieb[%u (%u,%u)]: StealGoodFromPile fail!" (Dieb = thief) |
| 404 | 0x194 | Job404 | placeholder | handler 0x4f7bdf |  |
