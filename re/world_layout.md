# The world object (`CGameData`): memory map

The simulation state is one object of **0x153A128 bytes (22,258,984)**.
This file maps it from the first byte to the last. Record layouts are in
`re/structs.md`; the C++ header is `src/s3re/game/layout.h` (in the build).
Offsets are from the start of the object (the ECX of `CGameData::`
methods, `CLogicThread+0x3a0`).

Written 2026-11-09 from: the constructor, the save/load code, the debug
dumper, the map loader, `tools/world_fields.py` (every displacement in
`build/s3.asm`, grouped by the regions below) and a world dump from a real
game (`build/x86/bin/Debug/determinism-20261107-224635-world-1000.bin`,
map `384-2-Lakes`, tick 1000, 2 players).

Confidence:
- **verified**: proven by code that sizes or copies the region (constructor
  `rep stos` counts, save/load copy sizes) *and* consistent in the runtime
  dump, or covered by our verified reimplementations.
- **strong**: several independent code uses agree; meaning partly inferred.
- **guess**: plausible reading of few uses; check before relying on it.

## Identity, allocation and size (verified)

- **The class is `CGameData`** (RTTI `.?AVCGameData@@`, base
  `Win32::CObject`). The object is polymorphic after all: **+0x0 is the
  vtable pointer 0x658C18** (one slot: the scalar deleting destructor
  0x4FCE60; destructor 0x4FCE80 just resets the vtable and calls the
  `CObject` destructor 0x610330). The runtime dump has 0x658C18 at +0.
- **Allocated in `CLogicThread::OnStart` (0x53C9E0):** `operator
  new(0x153A128)` at 0x53CB31, constructor `CGameData::CGameData`
  (0x4FC990) at 0x53CB50, pointer stored at `CLogicThread+0x3a0`
  (0x53CB63). If the allocation fails, 0x610960 reports an error (line
  0x82). The `CLogicThread` constructor (0x53C5E0) sets +0x3a0 to 0
  (0x53C645). Where it is deleted was not traced.
- **The last region ends exactly at 0x153A128** (the reciprocal table at
  0x1539528, 768 int32). Every byte of the object belongs to a region below
  except 2 bytes at 0x1238856 (padding).
- In the dumps the object starts 0x20 bytes into a page and the readable
  memory ends 0xEB8 bytes after it (all zero): the CRT gave it its own
  committed block. The determinism trace's "rest" hash currently reads
  those 0xEB8 bytes too (see "Consequences" below).

Initialization order (all verified by reading the code):
1. `CGameData::CGameData` (0x4FC990): CObject ctor (0x610320); member at
   +0x14D2BC8 constructed (0x5442C0); vtable; `DebugLog("Initialising
   globals")`; ring indices zeroed; offset table built (0x516530);
   reciprocal table (0x4A1EB0, Dirk\Pathing.cpp); every record array
   cleared and its "free" marker set; player records defaulted (calls
   0x508450 per player); `InitStatisticsKernel` (0x4FF4C0).
2. `CGameData::Init` (our name for 0x4FCE90, called from
   `CLogicThread::Step`, logs `*** GAMEDATA Initialising done`): resets the
   UI block +0x2D27C..+0x2D2FC and +0x2D314..+0x2D31C, search generation =
   1, the member at +0x14D2BC8 is sized for the map (0x544320), chart max
   +0x2D238 = 0; if a NewGameObjective is set (+0x1195DCC) the UI selection
   mode becomes 0xB.
3. Map loading `0x4FD540` (map file chunks 6 = landscape, 8 = buildings,
   settlers, piles; logs "Loading map...", "Worldsize: %u") or savegame
   loading `CLogicThread::LoadGame` (0x508FA0), or map generation
   `CGameData::GenerateRandomMap` (0x5166D0). `0x4FD020` (from OnStart) then resets
   per-game state (players' lists, spell costs, camera).

## Top-level map

| Offset | End | Size | Record | Count | What | Confidence |
|---|---|---|---|---|---|---|
| 0x0 | 0x4 | 4 | | | vtable `CGameData` (0x658C18) | verified |
| 0x4 | 0x38 | 0x34 | int32 | 13 | statistics smoothing kernel 3,6,8,9,9,10,10,10,9,9,8,6,3 | verified |
| 0x38 | 0x2D038 | 0x2D000 | uint8[36][256] | 20 players | **ProdStatArray**: statistics history | verified |
| 0x2D038 | 0x2D320 | 0x2E8 | | | scalars: chart buffer, UI state, map size, RNG, ... (table below) | mixed |
| 0x2D320 | 0x2E5E0 | 0x12C0 | 12 B | 4 x 100 | local player's notifications {type, x, y} | strong |
| 0x2E5E0 | 0x2F990 | 0x13B0 | 12 B | 42 x 10 | notification repeat timers {countdown, x, y} | strong |
| 0x2F990 | 0x32870 | 0x2EE0 | 12 B | 1000 | ring: events for the AI {type, a, b} ("pending events") | verified (layout) |
| 0x32870 | 0x32878 | 8 | uint32 x2 | | its read / write index | verified |
| 0x32878 | 0x366F8 | 0x3E80 | 16 B | 1000 | ring: events {type, a, b, c} | verified (layout) |
| 0x366F8 | 0x36700 | 8 | uint32 x2 | | its read / write index | verified |
| 0x36700 | 0x67440 | 0x30D40 | 20 B | 10000 | delayed effects (projectiles, spells), `Effect` | strong |
| 0x67440 | 0x67444 | 4 | uint32 | | high-water mark of 0x36700 | verified |
| **0x67444** | 0xDE7444 | 0xD80000 | 24 B | 0x300 x 0x300 | **map tiles**, `Tile` | verified |
| 0xDE7444 | 0xDE7448 | 4 | uint32 | | highest player index of the presence maps | verified |
| 0xDE7448 | 0xDF2848 | 0xB400 | uint8[20] | 48 x 48 | presence map A (16x16-tile blocks) | verified |
| 0xDF2848 | 0xDFDC48 | 0xB400 | uint8[20] | 48 x 48 | presence map B | verified (size) |
| 0xDFDC48 | 0xE07888 | 0x9C40 | uint16 x,y | 10000 | tile ring (flood-fill queue) | verified |
| 0xE07888 | 0xE07890 | 8 | uint32 x2 | | its read / write index | verified |
| 0xE07890 | 0xE114D0 | 0x9C40 | uint16 x,y | 10000 | second tile ring (UpdateSettlers only) | strong |
| 0xE114D0 | 0xE114D8 | 8 | uint32 x2 | | its read / write index | strong |
| 0xE114D8 | 0xE114DC | 4 | uint32 | | high-water mark of the 256-id table | verified |
| 0xE114DC | 0xE115DC | 0x100 | uint8 | 256 | in-use table: walk-region ids (tile +0x10) | verified |
| 0xE115DC | 0xE115E0 | 4 | uint32 | | **area high-water mark** (`maxusedeconomysector`) | verified |
| 0xE115E0 | 0xE115E4 | 4 | uint32 | | high-water mark of the 4096-id table | verified |
| 0xE115E4 | 0xE125E4 | 0x1000 | uint8 | 4096 | in-use table, ids 1..4094 (user: 0x51a580 set-up) | verified (layout) |
| 0xE125E4 | 0xE125E8 | 4 | uint32 | | high-water mark of the 8192-id table | verified |
| 0xE125E8 | 0xE145E8 | 0x2000 | uint8 | 8192 | in-use table: word-16 region ids (tile +0x16) | verified |
| 0xE145E8 | 0x10F45E8 | 0x2E0000 | 0x2E0 B | 4096 | **areas = economy sectors** ("Econ"), `Area` | verified |
| 0x10F45E8 | 0x110A578 | 0x15F90 | 0x5A B | 1000 | **trade records** ("Trad", our "building extra"), `Trade` | verified (layout) |
| 0x110A578 | 0x110A57C | 4 | uint32 | | trade high-water mark (`maxusedtrade`) | verified |
| 0x110A57C | 0x110A588 | 0xC | uint32 x3 | | saved counters: +0 (UpdateEconomy), +4 fog-of-war cycle counter 0..55 (UpdateEnvironment; fog_of_war.cpp), +8 minimap cycle counter 0..63 (UpdateEnvironment, 0x4FB520; reset to 0x3F) | strong |
| 0x110A588 | 0x1158788 | 0x4E200 | 16 B | 20000 | **offset table** (tiles sorted by distance), `MapOffset` | verified |
| 0x1158788 | 0x118A47C | 0x31CF4 | uint8[20] | 101 x 101 | territory claims of the players for the 101 x 101 tiles around (x, y) (cell [i][j] = tile (x - 50 + i, y - 50 + j)), filled by RecalculateTerritory 0x519A90 | verified |
| 0x118A47C | 0x11940BC | 0x9C40 | uint16[10][100] | 20 players | player settler lists (set by command 3) | verified |
| 0x11940BC | 0x11943DC | 0x320 | int32[10] | 20 players | their lengths | verified |
| 0x11943DC | 0x1195D7C | 0x19A0 | 0x148 B | 20 | **player records** ("Play"), `Player` | verified |
| 0x1195D7C | 0x1195DCC | 0x50 | int32 | 20 | per player, zeroed by the ctor; no other use found | guess |
| 0x1195DCC | 0x1195DD4 | 8 | u8 + u8[7] | | **NewGameObjective**: `objectives_enabled`, then `objectives`, the seven chosen objective ids (IsNgoObjectiveChosen 0x4FFB90, chosen by 0x4FFBC0; see structs.md) | strong (verified layout) |
| 0x1195DD4 | 0x1195FD4 | 0x200 | uint16 pile, settler | 128 | `pile_claims` (was "recent positions"; cursor `g_pileClaimsCursor` 0x7ACE60; filled only by AddPileClaim(settler, pile) 0x510770 from UpdateSettlers job 0x152, counted by CountOtherPileClaims 0x510800) | verified (layout), strong (meaning) |
| 0x1195FD4 | 0x1196024 | 0x50 | int32 | 20 | per player; = race in the dump (2, 3); saved with players | strong |
| 0x1196024 | 0x1222ED4 | 0x8CEB0 | 0x3C2 B | 20 x 30 | levelling jobs per player and slot: in-use byte + 31x31 planned heights (CheckBuildingSite 0x4D99E0) | verified |
| 0x1222ED4 | 0x1223656 | 0x782 | 31x31 B | 2 | levelling scratch: planned heights, then cell states (PlanSiteLevelling 0x4da440), saved but rewritten per call | verified |
| 0x1223656 | 0x1238856 | 0x15200 | uint16 | 43264 | minimap image (BuildMinimap, 0x4FB520) | strong |
| 0x1238856 | 0x1238858 | 2 | | | padding | |
| 0x1238858 | 0x123885C | 4 | uint32 | | pile high-water mark (`maxusedpile`) | verified |
| 0x123885C | 0x12A5E5C | 0x6D600 | 14 B | 32000 | **goods piles** ("Pile"), `Pile` | verified |
| 0x12A5E5C | 0x12A5E60 | 4 | uint32 | | settler high-water mark (`maxusedsettler`) | verified |
| 0x12A5E60 | 0x1499E60 | 0x1F4000 | 0x40 B | 32000 | **settlers** ("Sett"), `Settler` | verified |
| 0x1499E60 | 0x1499E64 | 4 | uint32 | | building high-water mark (`maxusedbuilding`) | verified |
| 0x1499E64 | 0x14D28A4 | 0x38A40 | 0x3A B | 4000 | **buildings** ("Buil"), `Building` | verified |
| 0x14D28A4 | 0x14D28A8 | 4 | int32 | | highest used reveal point (signed, -1 = none) | verified |
| 0x14D28A8 | 0x14D2BC8 | 0x320 | 16 B | 50 | **reveal points** (`RevealPoint`: active, x, y, countdown; 0x549530 writes, UpdateEnvironment counts down, MarkRevealPointsSight 0x518F20 reads) | strong |
| 0x14D2BC8 | 0x1539484 | 0x668BC | | | member object (Volker\LogicInit.cpp): 5000 x 0x4C + 10000 dwords + 7 dwords | strong (bounds) |
| 0x1539484 | 0x15394AC | 0x28 | uint32 | 10 | UI feedback list (`PushUiFeedback`) | verified |
| 0x15394AC | 0x15394B0 | 4 | uint32 | | its count | verified |
| 0x15394B0 | 0x15394B1 | 1 | uint8 | | flag (ExecuteCommands, OnStart) | guess |
| 0x15394B1 | 0x1539528 | 0x77 | uint8 | 119 | **VictoryConditions** (savegame name; read by 0x5145c0) | strong |
| 0x1539528 | 0x153A128 | 0xC00 | int32 | 768 | reciprocals 0x10000 / i (i = 1..767; [0] = 0), Dirk\Pathing.cpp | verified |

"Free" markers set by the constructor: areas, settlers, buildings, trade
records: byte +0x0 = 0xFF; piles: byte +0x0 = 0; table 0x36700: word +0x0
= 0; record 0 of every numbered array is never used (numbers start at 1,
and the allocators never hand out the last record: areas 1..0xFFE,
settlers 1..0x7CFE, trade records 1..998, effects 1..9998).

`python tools/world_fields.py check` verifies that the region table (the
same as this one) tiles 0..0x153A128 without gaps;
`python tools/world_fields.py fields NAME --funcs` lists a region's fields
with the functions using them.

### What the savegame stores (`CLogicThread::LoadGame` 0x508FA0; the saver is `CLogicThread::SaveGame` 0x509980, Volker\GameDataSave.cpp)

The loader takes numbered chunks (0x4A60F0 = open chunk n, 0x4A6310 =
close) and copies them straight into the world. That proves which regions
are game state:

| Chunk | World regions |
|---|---|
| 1 | +0x2D25C/+0x2D260 camera, +0x2D2D0 map size, RNG +0x2D2DC..+0x2D2E1, +0x110A57C, +0x110A580 (+0x110A584 is set to 0x3F), `g_gameTick` and 0x7DFD4C, a 20 x 0x44-byte player table at S3.EXE 0x7ACFAC, 0x7ACFA4, `g_gameOutcome`, 0x7DFDB0 (+1), +0x2D304, +0x2D308, +0xDE7444; +0x2D254/+0x2D258 are set to 10,000,000 |
| 2.0 | ring 0x2F990 with its indices |
| 2.1 | ring 0x32878 with its indices |
| 3.x | tile row x: `mapSize * 24` bytes from **0x67444 + x * 0x4800** |
| 4 | id high-water marks (256, area, 4096, 8192), the three id tables, areas 0..hwm |
| 6 | +0x2D2D4 local player, 0x1195FD4 (20 int32), the 20 player records |
| 7 / 8 / 9 | settlers / buildings / piles 0..hwm with their marks |
| 0xA | footprint grids 0x1222ED4 and 0x1223295, cached grids 0x1196024 |
| 0xB | minimap 0x1223656 |
| 0xC | player lists 0x118A47C and lengths 0x11940BC |
| 0xD | trade records 0..hwm |
| 0xE | table 0x36700 0..hwm (mark at 0x67440) |
| 0xF | notifications 0x2D320 and timers 0x2E5E0 |
| 0x10 | presence maps A and B |
| 0x18 (version >= 0x82) | VictoryConditions 0x15394B1 |
| 0x19 (>= 0x82) | PlayerColors (S3.EXE 0x7ACE64, 10 dwords) |
| 0x1C (>= 0x86) | ProdStatArray (statistics) |
| 0x1D (>= 0x96) | NewGameObjective 0x1195DCC / 0x1195DD0 |
| 0x1F (>= 0x99) | NewGameObjectiveTimeout +0x2D248 (default 0x3C) |
| (< 0x89) | player record +0x8 is cleared |

Not saved (rebuilt or transient): the offset table, the tile rings, the
territory scratch 0x1158788, the member at 0x14D2BC8, the reciprocal
table, the 50-entry table 0x14D28A8, the UI block (except camera).
After loading, the loader re-runs `UnblockType12BuildingTiles`, frees
("LOAD: Dropped eco %i") every area 1..0xFFF that no tile's area word
refers to, and sets the minimap flag +0x2D278.

The debug dumper `CGameData::DumpTables` (our name, 0x4FF6D0) writes
`d:\Worldump.dmp`, `EconDump`, `TradDump`, `PlayDump`, `PileDump`,
`SettDump`, `BuilDump` with record sizes 0x18, 0x2E0, 0x5A, 0x148 (20),
0xE, 0x40, 0x3A and the counts `WorldSize`, `maxusedeconomysector`,
`maxusedtrade`, `maxplayers`, `maxusedpile`, `maxusedsettler`,
`maxusedbuilding`: the original names of the record types.

## The scalar block 0x2D038 .. 0x2D320

"UI" means presentation state that changes with the local player's input;
"skip" means `src/s3re/trace/determinism.cpp` leaves it out of the hash.

| Offset | Type | Name / meaning | Users | Conf. |
|---|---|---|---|---|
| 0x2D038..0x2D237 | | statistics chart buffer (BuildStatisticsChart 0x4FF550) | UI, skip | verified |
| 0x2D238 | uint16 | chart maximum (0 in Init) | UI, skip | strong |
| 0x2D244 | int32 | statistics window selection | UI, skip | strong |
| 0x2D248 | int32 | NewGameObjectiveTimeout (minutes; 0x4FFBC0 "NGO TimeOut", game end 0x511C50) | sim | strong |
| 0x2D24C | int32 | `end_warning_minutes`: 10 (OnStart, NGO setup 0x4FFBC0); Step (0x53E230) shows "Spiel endet in %u min" (0x730A60) with it at tick (objective_timeout - it) * 60000 / 64 | sim | strong |
| 0x2D250 | uint8 | when set, Add/SubtractPlayerCounter do nothing | sim | verified |
| 0x2D254, 0x2D258 | int32 | last camera position drawn (10,000,000 = force); equal to the camera in the dump | UI, skip | strong |
| 0x2D25C, 0x2D260 | int32 | camera x, y in pixels | UI, skip, saved | verified |
| 0x2D264 | int32 | redraw flag | UI, skip | verified |
| 0x2D268..0x2D270 | int32 | UI (Init clears; ProcessEvents reads +0x2D268) | UI, hashed | guess |
| 0x2D274 | int32 | `random_map_symmetry`: CGameData::GenerateRandomMap stores its symmetry argument here (bit 0 mirror across x = y, bit 1 point symmetry); also read by the player placement 0x5074B0 | sim | strong |
| 0x2D278 | int32 | minimap ready (1 after ctor and load) | UI, skip | verified |
| 0x2D27C | int32 | refresh-UI flag (ExecuteCommands) | UI, skip | verified |
| 0x2D280 | int32 | draw hand-off flag | UI, skip | strong |
| 0x2D284 | int32 | UI selection mode (0xB when an objective is set) | UI, skip | verified |
| 0x2D288, 0x2D28C | int32 | selection parameters 1, 2 | UI, skip | verified |
| 0x2D290 | int32 | sub-page of menu 2 | UI, skip | strong |
| 0x2D294 | int32 | UI (ProcessEvents writes, PrepareDrawData reads) | UI, hashed | strong |
| 0x2D298 | int32 | special menu of the selected settler group (`ui_special_menu`; set by CLogicThread::UpdateGroupMenu 0x542990: 1 for one own settler of type 0x24, 2..7 for groups of types 0x2C/0x2B/0x29/0x28/0x16/0x17, else 0); `IsUiField298`; 340 reads | UI | strong |
| 0x2D29C | int32 | UI (also 0x542990) | UI, hashed | strong |
| 0x2D2A0 | int32 | UI menu sub-page | UI, skip | strong |
| 0x2D2A4..0x2D2CC | int32 x 10 | UI fields written by ProcessEvents, read by PrepareDrawData | UI, hashed | strong |
| 0x2D2D0 | uint32 | **map size** (square; tiles per side) | sim, saved | verified |
| 0x2D2D4 | int32 | **local player** | sim/UI, saved | verified |
| 0x2D2D8 | int32 | `command_attempts`: Step adds 4 before every ExecuteCommands attempt (0x53DDA5, also when the network allowance stops it); ProcessEvents reads it, PrepareDrawData tests bit 0x20 (blinking) | UI? | strong |
| 0x2D2DC | uint16 x3 | **RNG state** a, b, c (`CGameData::Random`) | sim, saved | verified |
| 0x2D2E4, 0x2D2E8 | int32 | last search result position (-1 after Init) | volatile, skip | verified |
| 0x2D2F0 | int32 | UI cursor mode (6 after Init) | UI, skip | verified |
| 0x2D2F4, 0x2D2F8, 0x2D2FC | int32 | UpdateEnvironment counters (Init: 0, 0, 0xD) | sim | strong |
| 0x2D300 | uint8 | search generation (stamped into tile +0xB) | skip | verified |
| 0x2D301 | uint8 | PrepareDrawData call counter | volatile, skip | verified |
| 0x2D304 | int32 | saved; ProcessEvents, 0x515AF0/0x515B00 | ? | guess |
| 0x2D308 | int32 | saved; UI code 0x53F950/0x53FBB0 | UI? | guess |
| 0x2D314..0x2D31C | int32 x3 | 0x4FEE90 and ProcessEvents (notifications) | UI | guess |

## Accessors per region (names from `re/functions.csv`)

Counts are the displacement uses `tools/world_fields.py regions` finds
(direct `[reg+disp]` forms only; pointer walks are not counted).

- **Tiles** (1602 uses, 177 functions). Verified accessors: IsOnMap,
  TileScreenX/Y, AssignTileToArea, RemoveTileFromArea, SetTileAreaMarked,
  BlockTile, UnblockTile, SetOwnerOfFreeTile, SplitAreaAtTile,
  JoinAreasAtTile, the flood fills, Scatter*, ReplaceTerrain*,
  UpdateBorderFlags*, CanSettlerEnterTile, NextSearchGeneration, overlays.
  Writers of the whole array: ctor, map loader 0x4FD540, LoadGame, GenerateRandomMap.
- **Areas** (728 uses, 61 functions): AllocArea, InitArea,
  Move/MergeAreaContents, Split/JoinAreasAtTile, AssignTileToArea,
  AddTilePileToArea, AddGoodsToPile, PileIncAmountLeaving,
  AddBuildingGoodsRequest, RemoveBuildingFromAreaRequests,
  SetAreaBuildingSlotA/B, FetchGoodsToTradeBuilding, ClearAreaGoodsSettings1D2,
  SumAreaGoods10PerPlayer; UpdateSettlers/UpdateEconomy/ExecuteCommands for
  the settings arrays.
- **Settlers** (3844 uses, 118 functions): CreateSettler, SetSettlerTarget,
  ClearSettlerJob, RestoreSettlerAnimation, SettlerStartHealth,
  PlaceSettlerNear, RemoveSettlerFromLists, Send*ToBuilding/Pile,
  LeaveBuildingSlot, MoveSettlerToBuildingTileA/B, CountSettlers*; most uses
  are in UpdateSettlers (0x4E9670) and 0x500860 (walking), 0x50CF50
  (settler removal), 0x50E9F0.
- **Buildings** (2629 uses, 97 functions): 0x4DBC50 (BuildingDone),
  0x4DD280, RemoveBuildingFromTiles, Take/ReleaseBuildingExtraTiles,
  building flags/goods functions, CountBuildings*, UpdateEconomy.
- **Piles** (729 uses, 51 functions): area_piles.cpp and goods_piles.cpp
  functions, 0x504420, 0x504F70 ("Pile %u Lost"), 0x5052C0, 0x505AC0.
- **Players** (739 uses, 105 functions): ShiftStatistics,
  Add/SubtractPlayerCounter, RecountPlayerGoods, HasManaForSpell,
  FindPlayerByField0C, CreateSettler, building flags, the AI code
  0x58A2C0..0x5953B0 (reads the settler-list heads).
- **Events**: QueueEvent / QueuePendingEvent write; **both rings are read by
  the computer players** (Rainer's files): the 16-byte ring by
  0x590DA0 (Rainer\CompMapWorkerAgent.cpp, from 0x58F990), the 12-byte ring
  by 0x554980 (Rainer\Cevaland.cpp, from 0x550F80, the per-player step that
  `CLogicThread::Step` calls on 20 objects at +0x350).
- **Effects table 0x36700**: AllocTable36700Record (10 call sites, e.g.
  0x4F2481 in UpdateSettlers) writes; UpdateEnvironment (0x4E78B8) walks
  records 1..hwm through a pointer, counts word +0x2 down and fires the
  record when it reaches 0 by type (+0x0): 1 -> 0x5474E0, 2 -> 0x5480B0
  (both Volker\LogicInit.cpp), 5/6/7 -> hits ("Gebäude getroffen",
  "Katapult getroffen", "Schiff getroffen", 0x4D9250 "Start crushing").
- **Offset table** (206 uses, 77 functions): built by 0x516530 in the
  ctor, read by every neighbourhood scan.
- **Notifications 0x2D320 / timers 0x2E5E0**: 0x4FEC10 (player, type, x,
  y) adds a notification for the local player only, unless a recent one of
  the same type lies within the type's radius (table S3.EXE 0x659DD0, 20
  bytes per type; it also depends on a UI setting read by 0x49F510 on
  0x7A7AF0); CountDownTimers ages the timers each UpdateEconomy; 0x4FEE90 and
  ProcessEvents/PrepareDrawData show them.
- **Presence maps**: IsOtherPlayerNear (A), UpdateEconomy (B), OnStart.
- **Territory scratch 0x1158788**: 0x519A90 (cleared on each call): for
  military buildings of types 0xF/0x10/0x12 with +0x8 != 0 and +0x21 == 0
  it marks, per player byte, the cells within offset-table entry 0xDAC /
  0xFA0 / 0x1388 of a 101x101 window around (x, y); called from
  UpdateSettlers, 0x506CF0, 0x50E3B0, 0x50E9F0 ("Player %u wins tower from
  %u").

## Consequences for our code (not applied; see the LOGBOOK)

1. **Tile base is 0x67444** (proof in `re/structs.md`, "Tile"). world.h's
   `kMapTiles = 0x67440` is 4 bytes early.
2. determinism.cpp's first header skip starts at **0x2D02C**, but the
   statistics run to 0x2D038: it hides the last 12 samples of player 19,
   category 35 (and the chart starts at 0x2D038). Should be `{0x2d038,
   0x2d248}`.
3. The "rest" region (`kRestRegionCap = 0x800000` from 0xDE7440) runs
   past the object end 0x153A128; reads stop at the end of committed memory,
   which in practice is 0xEB8 bytes of zeros after the object. Cap it at
   the object size (0x153A128) instead.
4. Fields to consider adding to the hash skips after checking writers
   (all UI per the table above, currently hashed): +0x2D268..+0x2D270,
   +0x2D294, +0x2D29C, +0x2D2A4..+0x2D2CC, +0x2D308, +0x2D314..+0x2D31C. They
   haven't caused differences so far, so they may only change in menus the
   synthetic input doesn't reach. The notifications (0x2D320, 0x2E5E0) only
   exist for the local player and depend on a UI setting; they are game
   state in the savegame but not read by the simulation as far as seen.
5. Saved but scratch: the footprint grids 0x1222ED4 (already skipped).
