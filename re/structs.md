# Record types of the world object

Field tables for the records in the world (`CGameData`, see
`re/world_layout.md`). Offsets are inside the record. Original names come
from the game's own debug dumper (0x4FF6D0: "World", "Econ", "Trad",
"Play", "Pile", "Sett", "Buil") and its German `DebugLog` texts.

Evidence columns: function names are from `re/functions.csv`; "dump" means
the runtime world dump at tick 1000 of map 384-2-Lakes
(`build/x86/bin/Debug/determinism-20261107-224635-world-1000.bin`);
"uses" counts come from `python tools/world_fields.py fields NAME`
(reads r / writes w; direct displacement forms only).
Confidence: **V** verified (our verified reimplementations or code that
proves it, plus the dump), **S** strong, **G** guess.

"Hash": `skip` = left out of the determinism hash by
`src/s3re/tooling/trace/determinism.cpp`; `UI` = presentation state.

---

## Tile (24 bytes, 0x300 x 0x300 at world+0x67444)

Tile (x, y) is at `0x67444 + (x * 0x300 + y) * 24`; a step in x is 0x4800
bytes, in y 0x18. The map uses the first `mapSize` rows and columns
(`mapSize` at +0x2D2D0; 384 in the dump; the rest stays zero).

### The base is 0x67444, not 0x67440 (settled)

Six independent proofs:
1. **Constructor** (0x4FCC49): `mov ecx,360000h; lea edi,[ebp+67444h];
   rep stos` clears 0x360000 dwords = 0xD80000 bytes = exactly 0x300 x
   0x300 x 24 from 0x67444. The dword at 0x67440 is cleared separately
   (0x4FCA80) together with the table at 0x36700 it belongs to.
2. **Savegame**: LoadGame copies, for each x, `mapSize * 24` bytes to
   `world + 0x67444 + x * 0x4800` (0x509247); SaveGame reads the same
   (0x50A347). Records cut at 0x67440 would split a tile across rows.
3. **Debug dumper** 0x4FF6D0 prints the fields of each tile with a pointer
   to 0x67456 and offsets -0x12..+0x4, i.e. record fields +0x0..+0x16 of a
   record at 0x67444, stepping 0x18.
4. **Map loader** 0x4FD540 writes the six bytes per tile of the map file
   to +0x4, +0x6, +0x7, +0x8, +0xA, +0x11 and clears +0x0, +0x2, +0x9,
   +0x10, +0x12, +0x14 of a record at 0x67444 (pointer 0x6744F, 0x4FD5C4).
5. The dword at **0x67440 is the high-water mark of table 0x36700**
   (AllocTable36700Record 0x4FEFD0, LoadGame chunk 0xE, UpdateEnvironment
   0x4E78B8), and the region after the array starts at exactly
   0xDE7444 = 0x67444 + 0xD80000 (presence max index).
6. **Runtime dump**: with base 0x67444, word +0x0 of the tile under every
   settler is that settler (124 of 126; the others are inside buildings),
   word +0x2 under every building's position is the building (11 of 11)
   and under 55 of 56 piles the pile; area +0x4 tile counts equal the
   number of tiles whose word +0x14 holds the area (all 4 areas); bytes
   +0x1, +0x3, +0xF, +0x13, +0x17 (the high halves of the words and the
   unused bytes) are 0 on all 147,456 tiles.

Offsets in our code that use the 0x67440 base and would change by -4:
`world.h` (`kMapTiles`, `kTileSearchMark` 15 -> 11), `map.cpp`
(`kTileHeight` 0x8 -> 0x4), `map_neighbourhood.cpp` (`kTileOccupant` 0x4
-> 0x0, `kTileTerrain` 0xA -> 0x6, byte 0x9 -> 0x5, `kTileByteB` 0xB ->
0x7, `kTileFlagsE` 0xE -> 0xA), `settlers.cpp` (occupant 0x4 -> 0x0, owner
0xC -> 0x8, flags 0xE -> 0xA), `world_search.cpp` (uses `kTileSearchMark`),
`map_marks.cpp` (absolute constants, unaffected; test sizes use
`kMapTiles`), comments in `map_areas.cpp`, `building_tiles.cpp`,
`map_marks.cpp`; and in `determinism.cpp`: per-tile skips 15..17 -> 11..13
(`kTileSkipBegin/End`), redraw byte 13 -> 9 (`kTileRedrawByte`), row length
`(mapSize + 1) * 24` -> `mapSize * 24`, header hash up to 0x67444 (then it
includes the 0x36700 high-water mark, now hashed as part of row 0), rest
from 0xDE7444. The hash values change, so traces made before and after
are not comparable (bump the trace version). Keep
`tools/diff_world_dumps.py` in sync. Test world sizes computed from
`kMapTiles + rows * kMapRowBytes` grow by 4 bytes, which is harmless.

### Fields

| Off | Size | Name | Meaning / values | Evidence | Hash | Conf. |
|---|---|---|---|---|---|---|
| 0x00 | u16 | settler | number of the settler standing on the tile (0 = none) | CreateSettler, PlaceSettlerNear, MoveSettlerToBuildingTileA/B, CanSettlerEnterTile; dump | | V |
| 0x02 | u16 | object number | building number if flag 0x20 of +0xA, else goods-pile number (0 = none) | RemoveBuildingFromTiles, AddTilePileToArea, RemovePileFromTile; dump | | V |
| 0x04 | u8 | height | terrain height; map file byte 0; TileScreenY subtracts it, CenterCameraOnTile too | TileScreenY 0x519610, map loader | | V |
| 0x05 | u8 | shade | recomputed from heights for a tile and its six neighbours by 0x519670 (via 0x519720); ~0x40 on flat land (dump: 64 on 93,813 tiles); read for drawing | 0x519670, PrepareDrawData, IsNeighbourhoodByte9Clear | | S |
| 0x06 | u8 | terrain ("worldart") | ground type; map file byte 1; high nibble = class (owned land when non-zero; 0x10 grass/land, 0x60 stream, 0x15 swamp); 0x70/0xF0 tested together (water?); 0x1C worn path (decays to 0x10); 0x16 a field (MakeFieldTile 0x51BA00, called by the farmer, rice farmer and winegrower jobs, turns a grass tile surrounded by grass into 0x16); dump: 7 on 79,249 tiles (water on this map), 16 on 42,334 | map_terrain.cpp, LogPlayerLandResources, 0x503980 | | V (field), S (values) |
| 0x07 | u8 | object | landscape object: trees 0x44..0x53, stone 0x73..0x7E (smaller = more left), fields (UpdateSettlers farmer jobs, session 45): grain 0x55 sown, 0x5A..0x5C ripe, 0x5D harvested; vines 0x5E planted, rice 0x67 planted, ripe vines and rice 0x63..0x65 and 0x6C..0x6E (which is which not checked); others; map file byte 2; must be 0 to build; a new construction site puts 0x70 on its own tile and 0x71 (stakes) on the outermost footprint tile per direction sector (CheckBuildingSite 0x4D99E0); AddBuildingSite 0x4DD280 sets 0xFF on the door tile list of some types | ScatterObjects, AreOffsetTilesClear, map loader, LogPlayerLandResources, building_sites.cpp | | V |
| 0x08 | u8 | owner | player owning the ground, 0xFF = nobody; map file byte 3 (mapped to game players) | map_terrain.cpp, CountTilesOwnedBy, SetOwnerOfFreeTile, AI::TileOwner 0x599570 (was AI_TileByte8; its comment had guessed terrain); dump 255/0/1 | | V |
| 0x09 | u8 | draw flags | bit 0x40 territory border (UpdateBorderFlagsAround), bit 0x80 redraw (set by the simulation when the tile changes, e.g. 0x5039DE; cleared by PrepareDrawData); bits 3..5 fog level (0 unseen .. 7 seen; 7 everywhere while 0x7DFD84 reveals the map), bits 0..2 the previous level after a change (UpdateFogSlice 0x519010; dump: 63, 191, 146...) | map_terrain.cpp, fog_of_war.cpp, determinism.cpp | bit 0x80 skip | V |
| 0x0A | u8 | flags | 0x01 blocked; 0x02 (UpdateSettlers sets); 0x04 being levelled (CheckBuildingSite sets it on every tile whose height a placed site changes; PlanSiteLevelling keeps such a tile and five neighbours fixed; building sites need it clear); 0x08 part of a building footprint ("Building-Bits"; set on the footprint and border tiles of a placed site by CheckBuildingSite; cleared by the map loader with 0x01 from the layout table at 0x6AA170); 0x10 temporary mark of flood fills/splits; 0x20 word +0x2 is a building; 0x40 owned (map loader sets it when the owner is valid; 0x519A90); 0x80 tested by FloodMarkAreaTerrain5To7; map file byte 4 | area_split.cpp, flood_fills.cpp, map loader | | V (0x01, 0x10, 0x20), S (others) |
| 0x0B | u8 | search mark | the search generation (+0x2D300) of the last search/overlay that visited the tile; the map loader sets 1 for tiles of a missing player | NextSearchGeneration, overlays, map loader | skip | V |
| 0x0C | u8 | overlay value | value drawn for the overlay mark (ring number etc.) | DrawOverlayLine, DrawWorkAreaOverlay, PrepareDrawData | skip, UI | V |
| 0x0D | u8 | sight | fog of war: how well the local player's team sees the tile now (0 = not at all). UpdateEnvironment's 56-step cycle (counter +0x110A580) clears an eighth of the rows per step (ClearSightSlice 0x518EC0), then raises it to radius - distance + 1 around the team's buildings (0x519FA0), settlers (0x51A150) and reveal points (0x518F20; 0x24 - distance), and turns it into the fog level of draw flags bits 3..5 (UpdateFogSlice 0x519010); 0..40 in the dump | fog_of_war.cpp, map_marks.cpp | | V |
| 0x0E | u16 | seen object | what the local team last saw on the tile, drawn under the fog (SeenObjectAt 0x51D5F0, stored by UpdateFogSlice when sight drops to <= 2 from a level >= 3: building (race+0x20)<<6 + type with progress bits 0x100..0x300, pile (amount+0x40)<<6 + good, object + 0x1800, 0x400 worn path); initially a zero-extended copy of the object byte made by the map loader (0x4FD5F1) and GenerateRandomMap; bit 0x400 cleared by 0x548380 (GodlyEvents area); read by PrepareDrawData; dump: same distribution as +0x7 | map loader | | S |
| 0x10 | u8 | walk region | connected region of unblocked tiles (ids from the 256-id table at +0xE114DC); 0 = none; settlers only take work in their region; dump: 1 on 52,366 tiles | Merge/SplitTileByte10Region, PlaceSettlerNear, SendNearestWorkerToBuilding | | V (mechanics), S (meaning) |
| 0x11 | u8 | resources | high nibble kind (0 fish, 1 coal, 2 iron, 3 gold, 4 gems, 5 sulfur), low nibble amount; map file byte 5 | LogPlayerLandResources, ScatterTileByte11 | | V |
| 0x12 | u8 | traffic | incremented when a settler walks over the tile (0x503940, from the walking code 0x500860; at 12 calls 0x51B7F0 CGameData::WearPath: grass 0x10/0x12/0x13/0x18 with grass or path around becomes path 0x1C, traffic += 9); decays in UpdateEnvironment (0x503980; at 7 a path tile 0x1C turns back into 0x10 and the tile is redrawn); 0xFF = fixed | 0x503940, 0x503980 | | S |
| 0x14 | u16 | area ("ecosec") | economy area 1..0xFFF (record in Areas); 0x8000 + n marks the tile as part of water body n (SetTileAreaMarked; terrain 5..7 flood): dump has 0x8001..0x8007 on 81,023 tiles | map_areas.cpp, area_split.cpp, LoadGame (areas 1..0xFFF not referenced are dropped) | | V |
| 0x16 | u16 | wide region | region id (8192-id table at +0xE125E8) for tiles whose 2-ring neighbourhood is unblocked and not terrain class 0x20; used by Dirk's pathfinder (0x4A1EE0, 0x4A36A0, 0x4A3870, 0x4A3940) | flood_fills.cpp | | V (mechanics), G (purpose) |
| 0x01, 0x03, 0x0F, 0x13, 0x17 | | (high bytes of words; 0x0F, 0x13, 0x17 never accessed) | always 0 in the dump | | | V |

---

## Area / economy sector ("Econ", 0x2E0 bytes, 4096 at world+0xE145E8)

Numbers 1..0xFFE (0xFFF is never handed out); high-water mark +0xE115DC
(`maxusedeconomysector`). Allocated by AllocArea (0x4E64C0), set up by
InitArea (0x4E6510, defaults per race). All fields below are from
`src/s3re/game/map/area_split.cpp` and its siblings (verified
reimplementations) unless marked.

| Off | Size | Meaning | Evidence | Conf. |
|---|---|---|---|---|
| 0x000 | u8 | player; 0xFF = free | ctor, AllocArea, SumGoldPerPlayer; dump | V |
| 0x004 | u32 | number of tiles | AssignTileToArea, RemoveTileFromArea; dump (3156, 3127) | V |
| 0x008 | u16 | head of the list of **empty piles** (goods byte 0xFF; MakePileEmpty 0x506080 pushes a pile here after unlinking it from its goods list) | Move/MergeAreaContents, MakePileEmpty | V |
| 0x00A | u16[36] | heads of the pile lists per goods type (piles linked +0xA/+0xC) | AddTilePileToArea, FetchGoodsToTradeBuilding | V |
| 0x052 | u16[36] | heads of the goods-request lists per goods type (buildings linked by their input word 2A) | AddBuildingGoodsRequest | V |
| 0x09A | u16[36] | building per goods type, list A (cleared for buildings 0x21/0x22) | SetAreaBuildingSlotA, RemoveBuildingFromAreaRequests | V (layout), G (trade buildings) |
| 0x0E2 | u16[36] | building per goods type, list B | SetAreaBuildingSlotB | V (layout) |
| 0x12A | u16[48] | heads of settler lists per settler type (settlers linked +0x10/+0x12); list 0 is used for carriers by FetchGoodsToTradeBuilding | Move/MergeAreaContents | V (layout), S (carriers) |
| 0x18A | u8[36] | setting array 1 per goods type (race defaults) | InitArea, ExecuteCommands | V (layout), G (meaning: distribution/priority settings) |
| 0x1AE | u8[36] | setting array 2 | same | same |
| 0x1D2 | u8[36] | setting array 3 (cleared by ClearAreaGoodsSettings1D2) | same | same |
| 0x1F6 | u8[36] | setting array 4 | same | same |
| 0x21A | u8[36] | setting array 5 | same | same |
| 0x23E | u8[3] | settings copied with the arrays | Move/MergeAreaContents | V (layout) |
| 0x241 | u8 | setting copied last | same | V (layout) |
| 0x242 | u8[3] | counters, summed on merge | MergeAreaContents | V (layout) |
| 0x245 | u8[51] | per-condition enable bytes (51 = building types?); AddBuildingGoodsRequest skips a request when its condition byte is 0 | AddBuildingGoodsRequest | V (layout), G (meaning) |
| 0x278 | u16[51] | per building type counters, cleared on move/merge | Move/MergeAreaContents | V (layout) |
| 0x2DE | 2 | padding | | S |

UpdateSettlers and PrepareDrawData read single bytes at +0x196..+0x19E,
+0x1AA, +0x1BA..+0x1C2, +0x1CE, +0x1DE..+0x1E6, +0x1F2 (elements of the
arrays above for fixed goods types).

---

## Goods pile ("Pile", 14 bytes, 32000 at world+0x123885C)

High-water mark +0x1238858 (`maxusedpile`). Free record: byte +0x0 = 0
(ctor; RemovePileFromTile clears it). Sources: area_piles.cpp,
goods_piles.cpp, the DebugLog `PileIncAmountLeaving: art = %u - pilno =
%u - amount = %u - good = %u - leaving = %u - (x,y)`.

| Off | Size | Name | Meaning / values | Conf. |
|---|---|---|---|---|
| 0x0 | u8 | good | goods type (index into the 36 per-goods lists and player counters); 0 = free record; 0xFF = an allocated pile without goods, listed in its area's empty-pile list +0x8 (MakePileEmpty, FetchGoodsToTradeBuilding) | V |
| 0x1 | u8 | amount | units on the pile (up to 8; DebugLog when >= 9) | V |
| 0x2 | u8 | leaving | units reserved to leave (PileIncAmountLeaving / PileDecAmountLeaving 0x505AC0: a pile whose units are all reserved leaves its area's goods list and comes back when one is released); building output piles use the high nibble for units on the way (FetchGoodsToTradeBuilding `+= 0x10`) | V |
| 0x3 | u8 | coming | units on the way to the pile (PileInc/DecAmountComing, AddGoodsToPile -1) | V |
| 0x4 | u8 | art (kind) | `PileKind` (game/enums/piles.h, re/enums.md): 0 storehouse (kinds up to 0x32 act alike), 0xFC trade ring, 0xFD order (building input, site materials), 0xFE dropped on the ground, 0xFF a building's output pile; <= 8 and 0xFE counted by RecountPlayerGoods | V |
| 0x5 | u8 | | always 0 in the dump, never accessed | S (padding) |
| 0x6 | u16 | x | tile | V |
| 0x8 | u16 | y | tile | V |
| 0xA | u16 | prev | previous pile in the area's list / building's ring; for a building pile (kind 0xFD/0xFE) the building it belongs to (UpdateSettlers special family: donkey/ship loading reads the building through the pile under the settler) | V |
| 0xC | u16 | next | next pile | V |

---

## Settler ("Sett", 0x40 bytes, 32000 at world+0x12A5E60)

Numbers 1..0x7CFE; high-water mark +0x12A5E5C (`maxusedsettler`). Free:
byte +0x0 = 0xFF. CreateSettler (0x50C9E0) is the allocator. Sources:
settler_records.cpp, settler_queries.cpp, building_tiles.cpp, counting.cpp.

| Off | Size | Name | Meaning / values | Conf. |
|---|---|---|---|---|
| 0x00 | u8 | owner | player; 0xFF = free | V |
| 0x01 | u8 | art | settler type (class table S3.EXE 0x658F64; start health 0x6590E4; type 0x29 special, 0x2B/0x2C ships, 0x25 the soldier BuildingDone creates); 48 types (area/player list arrays) | V |
| 0x02 | u8 | race | copied from the player record at creation | V |
| 0x03 | u8 | health | start health per type (20 less for race 3 soldiers) | V |
| 0x04 | u16 | x | tile | V |
| 0x06 | u16 | y | tile | V |
| 0x08 | u32 | direction / animation (union) | `direction` 0..5 while walking (RandomBelow6 at creation and in PlaceSettlerNear; LeaveBuildingSlot sets 2; WalkSettler stores each step's direction; Pathing reads it as the facing while < 6); `animation`: SetSettlerAnimation (0x50E2C0) stores a graphics/animation id (group << 16 \| index, from the table at S3.EXE 0x6A8060) here and frames - 1 in +0xC; RestoreSettlerAnimation (0x50E380, was RestoreSettlerDirection) copies +0x1C back | V |
| 0x0C | u16 | counter | random 0..31 at creation, reset with a new job; frames - 1 of a new animation (SetSettlerAnimation), step_time - 1 per step (WalkSettler): ticks left of the current animation / step | V (writes), S (meaning) |
| 0x0E | u16 | job | current job ("todo"; jobs 1 and 0x37 are followed by 2 and 0x35 (IsSettlerJob01Or37 0x4FA6B0, JobAfter01Or37 0x4FAA40, probably walking); e.g. 3 work, 0x4C fetch for trade, 0x133 carry to pile, 0x145 ship, 0x147 to building, 0xAD soldier in building) | V (values), S (name) |
| 0x10 | u16 | next | list link (area/player settler lists) | V |
| 0x12 | u16 | prev | list link | V |
| 0x14 | u8 | flags | bit 0x40: in a building slot (LeaveBuildingSlot); soldier spells (SpellMarkSoldiers 0x549F40) set 0x01 (spell 10, enemies), 0x02 (12), 0x04 (17), 0x08 (16), 0x80 (24, enemies), 0x10 (25) for 375 ticks (188 for 24), removed by a timed event of type 3; soldier.cpp reads 1 half damage, 2 double, 4 strong hit, 8 three quarters | V (0x40, spell bits) |
| 0x15 | u8 | slot / arrows / passengers (union) | `slot` low 3 bits: the settler's slot in its building; LeaveBuildingSlot clears the tile (A.x + slot + 1, A.y). `arrows`: a shooting soldier's arrows (WalkSettler, -1 per shot; race 3 sets 6). `passengers`: a ship's passenger count (+1 per boarding settler, capacity per race S3.EXE 0x659644; UpdateEnvironment frees the passengers of a sunk ship); type 0x17 with +0x15 != 0 walks one tick slower | V |
| 0x16 | u8 | frame | animation frame while walking: WalkSettler adds +0x19 each step modulo 12 (type 0x29: 36), 0 at the target, 1 after a step if 0 | V |
| 0x17 | u8 | second_unit / food / cargo_amounts01 (union) | job data: carrier jobs 0x133/0x134: 1 when it takes a second unit (SendNearestSettlerToPile; PileGoodsMoves and ResetSettlerToIdle release it); miner: meals left (MinerJobEnter sets 10, MinerJobFood adds 10/2 (+4 computer players) per meal and uses one per dig); ship 0x2B: cargo amounts of goods slots 0 / 1 in the low / high nibble (goods in the low bytes of +0x1C; UpdateEnvironment subtracts them from the player when the ship sinks) | V |
| 0x18 | u8 | tool_type / cargo_amounts23 (union) | carrier fetching a tool (job 0x68/0x69, economy.cpp): the settler type it becomes (1 digger, 2 builder count as site workers); ship 0x2B: amounts of cargo slots 2 / 3 (goods in +0x20) | V |
| 0x19 | u8 | step time | set to 1 with a new target if it was 0; WalkSettler: ticks per step = terrain speed (S3.EXE 0x658c1c) + type speed (0x6591a4), counter +0xC = it - 1; 0 while waiting | V |
| 0x1A | u8 | good / walk_state (union) | goods type being carried / fetched; `walk_state` for jobs 0x35/0xB0 (WalkSettler): 1 look for a ship to board, 2 shoot from the post (path cache [2]/[3]), 3/4/5 for type 0x17 | V |
| 0x1B | u8 | | no direct access found | |
| 0x1C | u32 | saved animation | restored by RestoreSettlerAnimation (0x50E380); ship 0x2B: cargo goods of slots 0 / 1 in its low bytes; geologists (0x16) and pioneers (0x28): 0xFFFF = no work target (ReleaseSpellTarget 0x54B000 then also sets +0x20 = 0xFFFF and unclaims the tile) | V |
| 0x20 | u16 | saved counter | restored with it (accessed in the folded form `(n + 0x4A97A) << 6`); SetSettlerSavedAnimation 0x50E320 stores the frame count - 1 here. For seven-tile units (CreateSevenTileUnit 0x50DD20) it is the next part settler instead (main settler -> part 1 -> ... -> part 6) | V |
| 0x22 | u16 | building | the settler's building / destination record | V |
| 0x24 | u16 | target x | SetSettlerTarget | V |
| 0x26 | u16 | target y | | V |
| 0x28 | u16 | waypoint x | end of the current straight leg of the path: Pathing::WalkLine (0x4A22F0) walks towards it, FindPath plans a new leg when the settler reaches it (equals the position in the dump); written by ExecuteCommands | V (pathing) |
| 0x2A | u16 | waypoint y | | V (pathing) |
| 0x2C | u8 | path mode | what Pathing::FindPath does next: 0x01 plan a new straight leg (SetSettlerTarget sets 1), 0x02 walk towards the waypoint (WalkLine), 0x04 blocked, look for a way around (StartEdgeWalk), 0x08 walk along the obstacle's edge (FollowEdge), 0x10 the edge walk's side, 0x20 waiting for a settler in the way (+0x2E = the blocked direction; ResolveBlocker follows such queues); 0xC1 = plan anew | V (pathing) |
| 0x2D | u8 | path steps | steps left of an edge walk (CompareEdges sets it, FollowEdge counts down, back to mode 2 at 0) | V (pathing) |
| 0x2E | u8 | path direction | direction 0..5 where the edge walk's search starts, or the blocked step while waiting | V (pathing) |
| 0x2F | u8 | | no direct access found | |
| 0x30..0x3A | u16[6] | path cache | set to 0xFFFF with a new target. WalkSettler: (0, 1) the next target, taken when the settler arrives (the rest shifts down); [4] = 0xFFFE: patrol, (0, 1) and (2, 3) swap; [4] = 0xFFFD: arrived when within [5] steps of ([2], [3]) (a soldier's post) | V |
| 0x3C | u8 | ship_walk | cleared by SendNearestSettlerToBuilding. Ships (WalkSettler): low 3 bits the last direction, upper 5 bits a count of steps where its two path searches disagreed (at 0xF8 it gives up: job 0x37); type 0x2B with 0xFF walks like a settler | V |
| 0x3D..0x3F | | | not accessed | |

---

## Building ("Buil", 0x3A bytes, 4000 at world+0x1499E64)

Numbers 1..; high-water mark +0x1499E60 (`maxusedbuilding`). Free: byte
+0x0 = 0xFF. Created by 0x4DBC50 (DebugLog "AddBuilding", "BuildingDone").
Sources: building_tiles.cpp, building_flags.cpp, building_goods.cpp,
counting.cpp, area_split.cpp (Take/ReleaseBuildingExtraTiles), dump.

| Off | Size | Name | Meaning / values | Conf. |
|---|---|---|---|---|
| 0x00 | u8 | owner | player; 0xFF = free | V |
| 0x02 | u16 | x | position ("tile A"; tile word +0x2 = the building, flag 0x20) | V |
| 0x04 | u16 | y | | V |
| 0x06 | u8 | art | building type (0x0F castle in the dump at the player position; 0x0F/0x10/0x12 military; 0x1F/0x2F/0x30 catapult/ballista/cannon workshops, 0x32 large gong; 0x20 shipyard; 0x21/0x22 trade buildings) | V (field), S (values) |
| 0x07 | u8 | race | the owner's race (AddBuildingSite 0x4DD280; indexes the layout table); read by UpdateEconomy/UpdateSettlers | V |
| 0x08 | u16 | worker / garrison (union) | production buildings: the worker (UpdateEconomy requests one with RequestSettlerForBuilding while 0, inputs only while set); military: head of the garrison chain (settlers linked by +0x10; UpdateEconomy counts it per class, takes a surplus soldier out); CountBuildingsWithWord8 | V (UpdateEconomy) |
| 0x0A | u16 | door x | "tile B": the building's area is the area of this tile | V |
| 0x0C | u16 | door y | | V |
| 0x0E | u16 | | output pile of production buildings (UpdateEconomy sends a carrier, DispatchPileCarrier, once amount - leaving >= 5); first pile of the building's ring of six (FetchGoodsToTradeBuilding); also used as a centre while under construction (DrawWorkAreaOverlay); set by AddBuildingSite 0x4DD280: 0xFF for type 1, x for types 2, 3, 5, 0x19, 0x1E, 0x33, x + 4/5/6 (clamped to the map) for types 0xE/0x18/0x24 (with +0x14 = y, y + 8/10/12); PileNeighbourBuildingByteE 0x505FD0 reads only its low byte (`mov al, [..+0x1499E72]`), PileIncAmountLeaving 0x5058C0 the whole word | V (uses) |
| 0x10 | u16 | pile 1 | input/construction pile numbers (BuildingPileHasGoods slot 1) | V |
| 0x12 | u16 | pile 2 | slot 2 | V |
| 0x14 | u16 | pile 3 | slot 3 | V |
| 0x16, 0x17 | u8 | digger_slots, builder_slots | construction site: digger (+0x16) / builder (+0x17) slot bits; UpdateEconomy requests a settler of type 1 / 2 for each clear bit below the count +0x1B / +0x1C and sets the bit on success | V |
| 0x18 | u8 | request flags | bit 0x01 enables bits 0x02/0x04/0x08/0x10 (player counters +0x20..+0x28); 1 for a new site; UpdateEconomy requests inputs only with bit 0x01; on a site bit 0x10 (diggers) / 0x08 (builders) / 0x02 / 0x04 (materials) let it request although the player's counter +0x28 / +0x26 / +0x20 / +0x22 says otherwise | V |
| 0x19..0x20 | u8[8] | per kind (union of views, layout.h `BuildingSiteBytes` ...) | see "Bytes +0x19..+0x20 per kind" below; `kind_bytes[8]` is the raw view | V |
| 0x19..0x20 (finished) | u8 | per type (summary) | UpdateEconomy (update_economy.cpp): military: soldiers coming / wanted per class (+0x19/+0x1A class 1, +0x1B/+0x1C class 2, +0x1F/+0x20 class 3); residences 0x1A..0x1C: births waiting (+0x19); temples 0x29/0x2B/0x2D/0x37: countdown +0x1C (250), points +0x19; altars 0x2A/0x2C/0x2E/0x36: countdown +0x1C (30); workshops (war machines, large gong) / shipyard: wanted input amounts +0x1A..+0x1C, total in +0x20 once the extra tiles are taken (0xFF before, 0xFE while taking); trade 0x21/0x22: goods cursor +0x1B, settler countdown +0x20 (6); barracks-like 0x13: recruits made per input slot +0x19..+0x1B | V (uses), G (meanings) |
| 0x21 | u8 | buildup | non-zero while a construction site (DebugLog "building[i].buildup"); 1 for a new site, 2 for a new mine (no levelling) | V |
| 0x22 | u16 | levelling_slot / guard / dock_direction (union) | construction site: its levelling job slot (CheckBuildingSite); shipyard: direction of the slip (Take/ReleaseBuildingExtraTiles; BlockBuildingLine 0x4DBB90 blocks the tiles from the work tile along it up to terrain 0x30, then sets 0xFFFF); site cache slot read by ClearBuildingFootprint 0x51AAE0 while buildup is 1; military: the soldier created by BuildingDone (0x4DBFB9, folded form `(n + 0x5AEDF) * 0x3A`) | V |
| 0x24/0x26/0x28 | u16[3] | input word 24 | requested amount per input slot; construction site: word +0x28 sums the height differences of the levelled tiles (CheckBuildingSite) | V |
| 0x2A/0x2C/0x2E | u16[3] | input word 2A | next building in the area's request list per slot | V |
| 0x30 | u16 | work x | construction site: the earth to move (PlanSiteLevelling's amount); = position after BuildingDone; centre of the work area; shipyard: ship position (0xFFFF = none) | V (uses), S (work area) |
| 0x32 | u16 | work y | construction site: the player's countdown +0x84 when it was placed | V (site) |
| 0x34 | u16 | trade | number of the building's Trade record (AllocBuildingExtra caller 0x4DBC50) | V |
| 0x36 | u8 | | 0 for a new site; TakeBuildingExtraTiles (state 0x20) marks the tiles taken only while it is <= 0x18 | V (use) |
| 0x37 | u8 | | no direct access found | |
| 0x38 | u16 | hit_points | 100 after BuildingDone, per type for military (S3.EXE 0x659664..); UpdateEconomy adds 1 per visit up to 250/500/1000 (types 0xF/0x10/0x12); a hit (effect type 5 "Gebäude getroffen", UpdateEnvironment) subtracts its damage and crushes the building when the damage exceeds it | V |

### Bytes +0x19..+0x20 per kind

The eight bytes mean different things per kind of building. `layout.h`
gives each kind a view in an anonymous union (`b.site.diggers`,
`b.military.class1_wanted`, ...); bytes a kind is not known to use are
unnamed there. Sources: AddBuildingSite 0x4DD280, BuildingDone 0x4DBC50,
UpdateEconomy (update_economy.cpp), ResetSettlerToIdle, UpdateSoldier,
CrushBuilding, SeenObjectAt, MinerJobEnter/Dig, TakeBuildingExtraTiles.

| View (kinds) | +0x19 | +0x1A | +0x1B | +0x1C | +0x1D | +0x1E | +0x1F | +0x20 | Conf. |
|---|---|---|---|---|---|---|---|---|---|
| `site` (buildup != 0) | boards_missing: boards (goods 1, layout word [1] high byte) not delivered yet; the site requests boards until pile 1 (amount + coming) holds this many; CrushBuilding drops half of the delivered ones | stones_missing (goods 2, low byte; pile 2) | diggers: min(8, (earth + 16) / 32 + 2) wanted | builders: layout header word [0] | workers_at_work: diggers/builders holding a slot bit (ResetSettlerToIdle -1 when it clears one); while non-zero and the earth < 0x28, builders come before the levelling is done | unk1e: 0 for a new site, cleared by BuildingDone, read by UpdateSettlers | materials_total = boards + stones | materials_used: built in (SeenObjectAt: progress (buildup + 50 * used) * 249 / (total * 50 + 4)) | V (values, uses), S (names) |
| `production` (worker buildings; mines 7, 8, 9, 0x1D, 0x25, 0x38) | worker_inside: MinerJobEnter +1; ResetSettlerToIdle clears it with the worker | digs (mines): digs so far, up to 32 (MinerJobDig; with 16+ digs and no find in the history work_y:work_x it notifies "mine empty"); cleared by MinerJobEnter | | | | | | | V (uses), S (names) |
| `military` (0x0F, 0x10, 0x12) | class1_coming: soldiers of class 1 on their way | class1_wanted | class2_coming | class2_wanted | | | class3_coming | class3_wanted | V |
| `workshop` (`BuildingWorkshopBytes`; was `water`: workshops 0x1F, 0x2F, 0x30, large gong 0x32; shipyard 0x20) | units done (UpdateSettlers shipyard/workshop jobs, session 45) | wanted[0]: input amount wanted per slot, from the state (shipyard: S3.EXE 0x659670 / 0x659680) | wanted[1] | wanted[2] | | | state: high nibble the production state (0x20: TakeBuildingExtraTiles needs +0x36 <= 0x18; shipyard 0x30 / 0x40 build ship 0x2B / 0x2C); the order state of the UpdateSettlers workshop/shipyard/gong jobs (0x10..0x12, 0x20..0x22, 0x13 for the gong) | extra_tiles: 0xFF tiles not taken yet; 0xFE (kExtraDone) taken; then the sum of wanted; afterwards the steps left of the current unit (UpdateSettlers; 0 at the gong) | V |
| `trading` (0x21, 0x22) | | | goods_cursor: next goods type offered for a free ring pile | | | | | call_countdown (0x21): visits until it calls a settler to the door again (6) | V (uses), S (names) |
| `temple` (temples 0x29/0x2B/0x2D/0x37, altars 0x2A/0x2C/0x2E/0x36) | points: temple +1 per 250 visits; two make a settler of type 0x24 | | | countdown: temple 250, altar 30 visits per offering | | | | | V (uses), G (kinds) |
| `residence` (0x1A, 0x1B, 0x1C) | births: settlers still to come out (BuildingDone: 1, 3, 10 units of S3.EXE byte kResidenceUnit) | | | | | | | | V |
| `recruiting` (0x13, probably the barracks) | recruits[0..2] (+0x19..+0x1B): carriers sent per input slot (the settler's saved_counter = slot + 1; ResetSettlerToIdle -1) | | | | | | | | V (uses), G (kind) |

---

## Player ("Play", 0x148 bytes, 20 at world+0x11943DC)

Defaults from the constructor (0x4FCD1F loop) and 0x4FD020. Sources:
statistics.cpp, players.cpp, player_queries.cpp, godly_events.cpp,
building_flags.cpp, settler_queries.cpp, dump (players 0 and 1).

| Off | Size | Meaning / values | Conf. |
|---|---|---|---|
| 0x000 | i32 | taking part: 0 = no; dump: 1 (local human), 2 (other player, probably computer) | V (non-zero test), G (1/2) |
| 0x004 | i32 | race 0..3 (dump 2, 3; also 0x1195FD4) | V |
| 0x008 | i32 | added in savegame version 0x89 (cleared for older saves); bit 0: set by command 0x06 when a finished big temple (0x29/0x2B/0x2D/0x37) of the player is destroyed; BuildingDone then gives no temple mana (probably) | S |
| 0x00C | i32 | team: the team / alliance index (`team`, was presence_index): the fog of war shows what the local player's team sees, enemies are settlers/buildings of another team (UpdateSoldier, WalkSettler, UpdateEconomy's military check, CrushBuilding), the minimap colours by it, the presence maps are indexed by it (UpdateEconomy phases 1..16); FindPlayerByField0C | V (uses), S (name) |
| 0x010 | i32 | start position x (= castle position in the dump) | V |
| 0x014 | i32 | start position y | V |
| 0x018 | i32 | 0 in ctor; player placement 0x5074B0; dump 1 | G |
| 0x01C | i32 | -1 in ctor; dump: 0 for player 0, -1 for player 1; the player's network id: ExecuteCommands finds the player of network messages 3/5/6/8/0x22 by it and puts it in the kill message of 0xfe | S |
| 0x020 | u16[48] | heads of idle-settler lists per settler type (lists 5, 8, 0xF, 0x1E..0x23, 0x27 (+0x6E), 0x2B (+0x76) seen); see the overlap below | V |
| 0x020..0x028 | u16 x5 | building request counters (+0x24 total, capped at 10; +0x20/+0x22/+0x26/+0x28 per bit, bit 7 = "set once") | V |
| 0x080 | i32 | 0x80 in ctor | G |
| 0x084 | i32 | site_stamp: 0xFFFF in ctor; counts down by one per new building site (AddBuildingSite 0x4DD280), at 60000 or below it restarts at 0xFFFF; the value goes to the site's work_y (+0x32), probably an order stamp | V (mechanics), G (meaning) |
| 0x088 | i32 | UI (ProcessEvents/PrepareDrawData only) | G |
| 0x08C | i32 | mana | V |
| 0x090 | i32 | mana_total: 0 in ctor; only ever added to, together with the mana +0x8C (a new temple +8 in BuildingDone, each offering in UpdateEconomy); read by ExecuteCommands, PrepareDrawData, ProcessEvents; command 0x18 (PromoteSoldiers) pays promotions from it (+0xB4), and with flag 0x80 first sets it to that cost (a cheat or test path) | V (writes), S (meaning) |
| 0x094 | i32[8] | mana needed per spell slot (1 in ctor, then S3.EXE 0x658E64[spell of the race's slot]) | V |
| 0x0B4 | i32 | promotion_cost: mana cost of the next soldier promotion, set by SetPromotionCost 0x508450 from the sum of the three levels (10, 20, 30, 50, 60, 30, then -1); command 0x18 (PromoteSoldiers) raises a level when +0x90 >= the cost (unsigned) and takes the cost from +0x90 | V |
| 0x0B8..0x0C0 | i32[3] | soldier_levels: level 0..2 per weapon kind (swordsmen, bowmen, pikemen); command 0x18 raises 0 -> 1 always, 1 -> 2 only when both other kinds are at 1 or more; UpdateSettlers gives new soldiers the type of their level (jobs 0x10/0x6A/0x8B) | V |
| 0x0C4 | u32 | population: civilians (settler class 0 or 10..29), recounted by RecountPlayerPopulation 0x5104C0 (UpdateEconomy); UpdateEconomy creates no new settler while it is above +0xC8 | V (count), G (use) |
| 0x0C8 | u32 | population limit: +0xCC + 10/30/100 per finished building of type 0x1A/0x1B/0x1C (residences, probably) | V (formula), G (meaning) |
| 0x0CC | u32 | base of the limit; +1 per civilian (type class 0 or 10..29) created by SpawnSettlersAround 0x50CB20 and in UpdateEconomy 0x4E36B7 | S |
| 0x0D0 | u32 | carriers (settler type 0) | V |
| 0x0D4 | u32 | surplus carriers: carriers - ((limit << 16) / population * carriers >> 16) when population > limit, else 0 | V (formula), G (meaning) |
| 0x0D8 | u32 | carriers marked with settler flag 0x80; UpdateSettlers job 1 marks/unmarks idle carriers until it equals +0xD4 | S |
| 0x0DC | u16[36] | goods counters per goods type (Add/SubtractPlayerCounter, RecountPlayerGoods) | V |
| 0x124 | u8 | map_revealed: 0 in ctor; SpellRevealMap (0x5495D0, spell 31) sets 1 for the local player; Step (0x53E530) clears it and the reveal global 0x7DFD84 once (tick - +0x128) * 64 / 1000 >= 15 (15 s) | V |
| 0x128 | i32 | map_revealed_tick: the game tick of that spell | V |
| 0x12C | i32 | soldiers_lost: +1 per dying settler of class 1..9 (UpdateSettlers jobs 0xAF GarrisonSoldierDies / 0x36 SettlerDies); campaign mission 52 is won when players 1..3 together reach 350 (CheckGameEnd 0x511C50) | V |
| 0x130 | i32 | civilians_lost: +1 per dying settler of another class | V |
| 0x134 | i32 | mission_state: low 12 bits counted up by SpellConvertSettlers 0x54A370 in campaign mission 67 ((count + 1) & 0xFFF per converted Amazon); CheckGameEnd: mission 67 sets bit 0x1000 of the local player's and needs bits 0..11 >= 10, mission 68 stores a game tick in player 0's (when its three towers first stand) | V |
| 0x138..0x147 | | not accessed | |

Overlap: the request counters +0x20..+0x28 share bytes with the list
heads of settler types 0..4. No caller passes a list below 5, so types
0..4 probably never have such a list. Confirm when the users are
reimplemented.

---

## Trade record ("Trad", 0x5A bytes, 1000 at world+0x10F45E8; our "building extra")

Numbers 1..998; high-water mark +0x110A578 (`maxusedtrade`). Free: byte
+0x0 = 0xFF. AllocBuildingExtra (0x4DF0A0) initializes it; the building
stores the number at +0x34. Sources: world_tables.cpp, ui_overlays.cpp,
settler_queries.cpp.

| Off | Size | Meaning | Conf. |
|---|---|---|---|
| 0x00 | u8 | the allocator's argument (0xFF = free) | V |
| 0x01 | u8 | mode: 1 at allocation; UpdateEconomy offers the wanted goods through the area's slot list A (1) or B (2), nothing when 0; set by ExecuteCommands, shown by PrepareDrawData/ProcessEvents | V (uses), S (name) |
| 0x02, 0x04, 0x06 | u16 | route points 1..3, x (0xFFFF = unset) | V (layout), S (route) |
| 0x08 | u16 | route end x (0xFFFF = no route) | V |
| 0x0A, 0x0C, 0x0E | u16 | route points 1..3, y | V |
| 0x10 | u16 | route end y | V |
| 0x12 | u8[36] | ordered: units wanted per goods type; UpdateEconomy offers a type while it is non-zero and differs from coming +0x36 (and a ring pile can take it); read by UpdateSettlers, PrepareDrawData; the game-end check 0x511C50 writes elements 2 and 7 (+0x14, +0x19) | V (layout, UpdateEconomy), S (name) |
| 0x36 | u8[36] | units of each goods type on the way (FetchGoodsToTradeBuilding +1) | V |

---

## Effect (20 bytes, 10000 at world+0x36700; "table 36700")

Records 1..9998; high-water mark at world+0x67440. Free: word +0x0 = 0.
AllocTable36700Record (0x4FEFD0) stores ten words; UpdateEnvironment
(0x4E78CF) walks 1..hwm.

| Off | Size | Meaning | Conf. |
|---|---|---|---|
| 0x00 | u16 | type (0 free); 1 -> 0x5474E0, 2 -> 0x5480B0 (Volker\LogicInit.cpp), 3, 4, 5 ("Gebäude getroffen", building hit), 6 ("Katapult getroffen"), 7 ("Schiff getroffen") | V (dispatch), S (meanings) |
| 0x02 | u16 | countdown; decremented each UpdateEnvironment, fires at 0 | V |
| 0x04..0x12 | u16 x8 | parameters (x, y, owner, ... passed to the handlers) | S |

---

## Ring entries

- **Event** (16 bytes, ring of 1000 at world+0x32878, read/write index
  +0x366F8/+0x366FC): int32 type, a, b, c. Types seen: 5 (settler
  removed: settler, type, player), 6 (area: area, 0, 0; deduplicated
  against the pending ring). The dump holds 102 type-6 events. Read by
  the AI (0x590DA0). **V** (layout).
- **Pending event** (12 bytes, ring of 1000 at world+0x2F990, indices
  +0x32870/+0x32874): int32 type, a, b. Types: 1 building done, 2 site
  placed, 3 + 6 building crushed (x, y; type, player), 4 area changed
  (deduplicated), 5 building conquered (building, old owner), 7 worker idle
  (G), 8 re-evaluate (x, y). Read by the AI (CEvaLand::ProcessPendingEvents
  0x554980, re/computer_players.md "The AI event ring"). **V**.
- **Notification** (12 bytes, 4 lists x 100 at world+0x2D320): int32
  message type, x, y; local player only (0x4FEC10). **S**.
- **Notification timer** (12 bytes, 42 types x 10 at world+0x2E5E0): int32
  countdown (0 = unused; active entries first), x, y. CountDownTimers.
  **V** (layout).
- **Tile ring entry** (4 bytes, 10000 at world+0xDFDC48 and +0xE07890):
  uint16 x, uint16 y. **V**. The first ring is also the queue of
  SpreadTerrainTransform 0x548760 (grid cells, not map tiles).
- **TerrainGrid** (0x2018 bytes, on TransformTerrainAround's stack,
  0x548380; passed by pointer to SpreadTerrainTransform 0x548760 and the
  table functions 0x548bb0 / 0x548d50; `TerrainGrid` in spells_land.cpp):
  +0x0 kind (spell 0..3 or 32), +0x4 x0, +0x8 y0 (map position of cell
  0, 0), +0xC cx, +0x10 cy (the centre's cell), +0x14 side (32),
  +0x18 uint8 can_change[64][64] ([gx][gy], 1 = may change, border 0),
  +0x1018 uint8 terrain[64][64] (the new terrain). **V**.
- **Pile claim** (4 bytes, 128 at world+0x1195DD4, `PileClaim`): uint16
  pile, uint16 settler (both 0 = free). AddPileClaim 0x510770 (was
  AddToRecentPositions) adds one, called only by UpdateSettlers job 0x152
  with (settler, pile); CountOtherPileClaims 0x510800 counts a pile's
  claims by other settlers; the logic thread clears the slot at the cursor
  `g_pileClaimsCursor` (0x7ACE60) every step. **V** (layout), **S**
  (meaning).

## Other small records

- **MapOffset** (16 bytes, 20000 at world+0x110A588; built by 0x516530):
  int32 dx, int32 dy, int32 ring distance (0, 1 x6, 2 x12, ...,
  non-decreasing; 75 at the end), int32 direction sector 0..5. Entry 0 is
  (0, 0, 0, 0); entries 1..6 the neighbours in direction order; the last
  entry (19999) is all zero. Dump-verified. **V**. Built by
  BuildOffsetTable (verified, session 57): 5000 hex points (ring r,
  step a < r) by distance 2500 (2r - a)^2 + 7569 a^2, the 3333 nearest
  each written in its 6 rotations; the ring field is a counter that grows
  after each point with a = 0.
- **Presence block** (20 bytes = one byte per player, 48 x 48 at
  world+0xDE7448, block (bx, by) at +bx*960 + by*20): non-zero where the
  player is present. Second map at +0xDF2848 with the same shape.
  **V** (A), **S** (B).
- **Levelling jobs** (0x3C2 = 962 bytes, 20 players x 30 slots at
  world+0x1196024, record p*30 + k): u8 in use (set to 1 when
  CheckBuildingSite 0x4D99E0 places a site; a site needs a free slot of
  its player, its number goes to building +0x22), then 31x31 bytes: the
  planned height of tile (x-15+i, y-15+j) at [i*31 + j], 0 where the
  height stays. **V** (written by building_sites.cpp; readers not done).
- **Levelling scratch** (world+0x1222ED4 heights, +0x1223295 states,
  31x31 bytes each, cell [i*31 + j] = tile (x-15+i, y-15+j)): written by
  PlanSiteLevelling 0x4DA440 for every check (preview included); states 0
  free, 1 to propagate, 2 changed this round, 100 fixed. **V**.
- **Building layouts** (S3.EXE .data 0x6AA170, 2400 bytes per race*57 +
  type, int32): header [0] (to building +0x1C), [1] materials (high byte
  +0x19, low byte +0x1A), then marker lists: 0x80000005 (unused here),
  0x80000004 door (dx, dy) + more tiles, 0x80000000 footprint (levelled; a
  0x80000001 inside ends the stake candidates), 0x80000002 border (must be
  free, not levelled), 0x80000003 end. Format in building_sites.cpp. **V**.
- **Territory scratch** (world+0x1158788): 101 x 101 cells of 20 bytes
  (one per player), centred on (x, y) with an offset of 50; 0xFF = in
  range of a tower. **S**.
- **Member at world+0x14D2BC8** (0x668BC bytes, constructed by 0x5442C0,
  sized by 0x544320(mapSize, 200)): 5000 records of 0x4C bytes
  (constructor 0x543830 each), then 10000 dwords at +0x5CC60, then seven
  dwords at +0x668A0 (cell counts derived from the map size:
  +0x668B4 = (mapSize*12 + 200)*2, +0x668B8 = mapSize*9 + 400, +0x668A4 /
  +0x668A8 = those / 256 + 1, +0x668B0 = their product / 16). Probably
  the logic thread's draw/sprite lists in screen cells. **S** (bounds),
  **G** (purpose).
- **MapEffect** (our name; the 0x4C-byte records of the member above).
  Default constructor 0x543830: everything 0 except dwords +0x2C, +0x34 =
  -1 and byte +0x38 = 0xFF (+0x30, +0x40 untouched). The 20-argument
  constructor 0x543880 writes, in argument order: dword +0x0, word +0x4,
  word +0x6, byte +0x3A, word +0x3C, dwords +0x8, +0xC, +0x10, +0x14, word
  +0x28, dwords +0x18, +0x1C, +0x20, +0x24, dword +0x2C, words +0x30,
  +0x32, bytes +0x38, +0x39, dword +0x34; dwords +0x40..+0x48 = 0. The 13
  "make" functions 0x543930..0x543F70 pass fixed values (e.g. type 9,
  graphics id 0xD0031) and the tick; 0x5445E0 adds a record (it passes
  +0x10/+0x14 to 0x545EB0, probably a position). Meanings **G**; layout
  **V** (src/s3re/game/environment/map_effects.cpp).
  Field names in `layout.h` (`MapEffect`): +0x0 start_tick (0 = free
  record; MapEffects_FindFreeSlot), +0x4 duration (i16), +0x8/+0xC
  start_x/y, +0x10/+0x14 end_x/y (pixels; MapEffect_PositionAt
  interpolates, MapEffects_Add files the record under the end position's
  256-pixel block), +0x18/+0x1C start_tile_x/y and +0x20/+0x24
  end_tile_x/y (the tile it is over, MapEffect::TileAt 0x5441C0
  interpolates them; session 54), +0x6 anim_period, +0x2C graphic (-1 =
  none), +0x30 first_frame, +0x32 frame_count, +0x39 brightness (non-zero:
  overrides the fog brightness; MapEffect::MakeDrawRecord 0x544100), +0x28 arc (i16 hop height), +0x40 list (block),
  +0x44 previous, +0x48 next (MapEffect_SetLinks), +0x3A source_kind and
  +0x3C source_id (what made the effect: MapEffects::RemoveMatching
  0x545C80 removes those of a kind and number; CrushBuilding passes kind 0
  and the building); the rest unkXX. Never
  written by either constructor: +0x2A..+0x2B, +0x3B, +0x3E..+0x3F
  (pad fields; the make functions copy stack garbage there). The table
  (`MapEffectTable`): +0x5CC60 cell_heads[10000], +0x668A0 slots_used,
  +0x668A4 blocks_x, +0x668A8 blocks_y, +0x668AC expire_cursor (the cell
  MapEffects::ExpireSome 0x545D40 checks next), +0x668B0
  block_count_16, +0x668B4 width, +0x668B8 height. **V** (layout, from
  map_effects.cpp's verified functions), **S** (names).
- **VictoryConditions** (world+0x15394B1, 0x77 bytes, layout::VictoryConditions;
  savegame chunk 0x18): +0x00 destroy_players flag, +0x01 u8[20] marked
  slots; +0x15 destroy_buildings flag, +0x16 10 x {i8 player (-1 unused),
  i8 building type}; +0x2A hold_places flag, +0x2B 5 x {u8 active, u16 x,
  u16 y} (5 bytes each); +0x44 protect_players flag, +0x45 u16[20] minutes;
  +0x6D collect_goods flag, +0x6E 3 x {u16 amount, i8 good}. Slots are
  colours outside campaigns (CGameData::PlayerOfColour 0x4FFE00).
  Evaluated by CLogicThread::CheckVictoryConditions (0x5145C0). **V**
  (layout, from the verified reimplementation), **S** (meaning).
- **Game end:** world +0x2D248 objective_timeout (minutes; the NGO game
  ends at tick >= minutes * 60000 >> 6), +0x2D250 counters_disabled (set
  then by CheckGameEnd 0x511C50). CLogicThread +0x24C: game tick of the
  last CheckGameEnd evaluation (every 50 ticks). **V**.
- **New game objectives** (world+0x1195DCC): byte +0x0 `objectives_enabled`
  (non-zero = on); bytes +0x1..+0x7 `objectives`, the seven chosen objective ids (1..34, drawn by 0x4FFBC0
  with Random, distinct, never 21, 25, 27..31, 33, 34; checked by
  IsNgoObjectiveChosen 0x4FFB90). **V** (layout), **S** (meaning, from
  the "NGO" DebugLog texts).
- **Table at world+0x14D28A8** (16 bytes x 50, mark +0x14D28A4): byte
  +0x0, dword +0x4, dword +0xC written by 0x549530 (Volker area near
  GodlyEvents); UpdateEnvironment and 0x518F20 read it. **G**.

- **Found by ExecuteCommands (session 47, `re/execute_commands.md`):**
  Settler +0x1C (dword) / +0x20 (word) of geologists (22) and pioneers (40):
  their work tile (0xFFFF = none), reserved by tile flag 0x02; Settler
  `path_cache` holds up to three waypoints (MoveUnits 0x0d), 0xFFFE in [4]
  and [5] marks a patrol, 0xFFFD in [4] an attack target with a race value
  in [5]; Settler +0x3C of non-ships: 0 (command 0x0f) or 40 (attack-move);
  walk_state 3: a thief's patrol. Area +0x1F6 (settings4) = transport place
  per good, +0x21A (settings5) = transport flag per good, +0x23E..+0x240
  tool shares (steps of 5, sum <= 95), +0x242..+0x244 percentages, +0x241 a
  setting (command 0x0b). Building +0x1A / +0x1C / +0x20 of towers and the
  castle: wanted swordsmen / bowmen / pikemen (command 0x0c). A storehouse's
  +0x0E: the good its six piles take (command 0x1c). Workshops' +0x36: war
  machines waiting, loaded into ships (command 0x19). World +0x2D298
  (ui_special_menu) is the building menu's page: 4..13 trade, 14..16
  workshop. Player +0x00 == 2: a dropped player a computer took over. **V**
  (accesses), **S** (meanings).

---

# Classes outside the world

Layouts of the other objects our code touches, by class (RTTI names where
the binary has them). The C++ structs are the source of truth for offsets
(every field `static_assert`ed); this lists what is known and the evidence.
Headers: `src/s3re/game/graphics/gfx_layout.h`, `framework_layout.h`,
`screen_element_layout.h`, `menus_layout.h`, `game_settings_layout.h`,
`ai_layout.h`, `logic_thread_layout.h`.

## CGraphicsData (0xC44 bytes; pointer at 0x7A8010)

Dirk\graphicsdata.cpp; base Win32::CObject, vtable 0x65D3E4. Constructor
0x5F67C0 (operator new 0xC44 at 0x4932FE and in CDrawer's constructor),
destructor 0x5F68B0, Close 0x5FA690. Every per-group array has 70 entries
(the loops count 0x46; arrays 0x118 bytes apart); the original never
checks the group, and tests use groups up to 0xAF. **V** (layout).

| offset | field | evidence |
|---|---|---|
| +0x000 | vtable | constructor |
| +0x024 | std::string (probably the GFX file or folder name) | constructor/destructor **S** |
| +0x034 | dword[70], not cleared by Close | **G** |
| +0x14C | Win32::CFileMapping*[70] (new 0x2C, ctor 0x61A3A0) | constructor, Close **V** |
| +0x264 | views[70]: each group's mapped view; table offsets are relative to it | Close (UnmapView 0x61AA40), GfxData_* **V** |
| +0x37C | textTables[70] (TextTable: word +8 texts per language, word +0xA languages (8), dword offsets to C strings from +0xC; was frameTables) | CGraphicsData_TextAddress **V** |
| +0x494 | animationTables[70] (OffsetTable: word +6 count, dword offsets from +8) | CGraphicsData_AnimationPointer **V** |
| +0x5AC | guiImageTables[70] (OffsetTable, count not checked; filled only for GUI groups; was frameTables5ac) | CGraphicsData_GuiImagePointer **V** |
| +0x6C4, +0x7DC, +0x8F4, +0xA0C | four more table arrays, zeroed by Close | **S** |
| +0xB24 | sequenceTables[70] (SequenceTable: dword entries from +8, relative to the view; was spriteTables) | CGraphicsData_SequenceFrameCount **V** |
| +0xC3C | languageCount, zeroed by constructor and Close | re/graphics_formats.md |
| +0xC40 | language: the current language (used for -1; was currentVariant) | CGraphicsData_TextAddress **V** |

Tests: `src/s3re/test/fake_graphics.h` (FakeGraphics) installs a fake
object for the simulation tests that read animation lengths.

## CLandscapeSurface (0xA8 bytes)

Volker\landscape.cpp; DirectX::COffscreenSurface -> CSurface ->
CSimpleSurface. Constructor 0x54BD30 (operator new 0xA8 at 0x42EFB0).
+0x90 the CGraphicsData (constructor argument); +0x94 new(0x364), +0x9C
new(1,000,000), +0xA0 colours (new(0x202): uint16 slots 0..256,
Landscape_FindOrAddColour), +0xA4 new(0x10000); the destructor frees all
four, not +0x98. **V** (layout, constructor), **S** (colours).

## UI: CScreenElement, CScreenGroup, CScreen, CControl, CStaticText

Dirk\dialog.cpp. CScreenElement (0x20 bytes, vtable 0x655A68, constructor
0x415610): +0x04 background colour (0x1111), +0x08 RECT, +0x18 parent (a
CScreenGroup (sub)object), +0x1C has_focus, +0x1D mouse_inside, +0x1E
needs_redraw, +0x1F probably "transparent". CScreenGroup, CControl and
CScreen derive *virtually* from CScreenElement (RTTI), so each starts with
its own vtable (+0) and a vbtable pointer (+4), and the CScreenElement part
follows its own fields: CScreenGroup 0x80 bytes (vtable 0x655B70,
children Watcom vector +0x0C, focus +0x28, captured +0x30, three Watcom
containers +0x34/+0x4C/+0x64, vtordisp +0x7C; constructor around
0x416CA0), CScreen = CScreenGroup + 0x18 bytes (element at +0x98),
CControl 0x3C bytes (vtable 0x655D28, tooltip fields +0x08..+0x34,
vtordisp +0x38; constructor around 0x418000). Their methods get the
CScreenElement part as `this`. CStaticText (0x44 bytes): text +0x28,
font +0x3C, style +0x40. **V** (verified replacements in
screen_element.cpp, ui_static_text.cpp; constructors).

## Menus::CDrawThread

RTTI: DirectX::CGameDrawThread part at +0 (0x54 bytes), CScreen at
+0x54, CScreen's virtual base CScreenElement at +0x150 (vtable 0x6583D8
stored through the vbtable at +0x58). Own fields: +0x104 menu state (0x14
start the startup map, 4 start the game), +0x134 byte, +0x144 (-1 when a
loaded game starts). **S** (savegame_start.cpp).

## DirectX::CGameDrawThread and CDrawThread (the in-game draw thread)

CGameDrawThread (0x54 bytes; base DirectX::CGameThread, virtual base
CWin32Handle at +4; vtables 0x65CA40 / 0x65CA34): CGameThread +0x28
active byte; +0x44 the drawer (DirectX::CGameDrawer), +0x48 the game
window, +0x4C Step count, +0x50 full-redraw flag (constructor 1,
OnActivate 1, Step clears it after a frame). CDrawThread (vtables
0x656AB0 / 0x656AA4) adds +0x54 the CDirectDraw the drawer is built on, +0x58
the shared draw-data slot (InterlockedExchange), +0x5C its own draw data,
+0x60 the waitable the logic thread signals (slot 0 Wait(timeout, 1)),
+0x64 the "frame taken" CEvent, +0x68 no-drawer byte, +0x69 initialized,
+0x6C start message wParam, +0x70 lobby byte. **V** (wip/cursor-r7,
directx_draw_thread.cpp, draw_thread_steps.cpp).

## CDrawer (in-game drawer, 0x5F700 bytes): surfaces and cursor

+0x5F6C0..+0x5F6CC four surfaces (primary/flip, back, draw, one more;
ReleaseSurfaces forgets them), +0x5F6D5 software-cursor byte, +0x5F6D8 the
Dirk::CMouseSurface, +0x5F6DC current cursor id (-1 none), +0x5F6E0 the
Dirk::CTextSurface. Layout: `src/s3re/game/graphics/drawer_layout.h`.
**V** (wip/cursor-r7, drawer_surfaces.cpp).

## GameSettings (g_gameSettings, 0x7A7A18; no RTTI)

Layout in `src/s3re/game/ui/game_settings_layout.h`. +0x00 a
Win32::CCriticalSection (CRITICAL_SECTION; GameSettings::PlayerSetup holds
it through Win32::CLock); +0x2C std::string map name; +0x58 the
player-setup vector (Watcom value vector: +0x58 vtable, +0x60 data,
+0x64 capacity, +0x68 filled, +0x6C length = player count; elements are
0xCC-byte GamePlayerSetup records); +0x74 start goods; +0x78 local player
id; +0x88 word: the game seed; +0xB0 the game's GUID (NewGame;
GameSettings::GameGuid 0x437400 copies it, SaveGameToSlot formats it with
UuidToStringA); +0xC8 a dword read by the network code (chunk 0x1b); +0xCC
byte: random races include the Amazons; +0xCD byte: the goods objective
(NGO) is on; +0xD0 dword: the NGO time in minutes (GameSettings::NgoTimeout
0x437470). **V** (layout of these fields, session 55; +0xD0 session 57).

## GamePlayerSetup (0xCC bytes; Dirk\nethelp.cpp; no RTTI)

One player of the game setup (GameSettings +0x58). Constructor 0x49e8f0,
copy constructor 0x49eaf0, operator= 0x49edf0, destructor 0x49ec80
(GamePlayerSetup_Destruct), filled by GamePlayerSetup::Set 0x49f120.

| Off | Meaning | Conf. |
|---|---|---|
| +0x04 | "Ids" second value, printed %x by LogSlot 0x4a01c0 | S |
| +0x08 | "Version" in the LogSlot line | S |
| +0x10 | dword, saved as a byte in chunk 0x14 (getter 0x49f4d0); the first "Ids" value (probably the network id) | V |
| +0x14 | party (Set argument 3; getter 0x49f590) | M |
| +0x18 | std::string | V |
| +0x28 | 16-bit string (MSVC6 basic_string<unsigned short>) | V |
| +0x38 | Watcom vector (vtable 0x657410, capacity 10 on construction; not copied by the copy functions) | V |
| +0x5C | type: 0 human, 2 computer (getter 0x49f350) | M |
| +0x64 | race, 0xFF random (getter 0x49f510) | M |
| +0x68 | byte, Set argument 5 (getter 0x49f580) | V |
| +0x6C, +0x70 | set from Set's arguments 4 and 3 only while -1 | M |
| +0xA8..+0xB0 | three values saved in chunk 0x14 (getter GamePlayerSetup::SavedValues 0x49fed0) | V |
| +0xB4 | colour index (getter 0x49fec0) | V |
| +0xB8 | "Clan" in the LogSlot line | S |
| +0xBC | std::string, the text LogSlot ends with (probably the player's name) | V |

The copy constructor and operator= copy +0x00..+0x0c, byte +0x0c,
+0x10..+0x17, byte +0x54, +0x58..+0x60, byte +0x60, +0x64..+0x67, byte
+0x68, +0x6C..+0xBB and the three strings (shared where the count allows);
the padding bytes +0x0D..+0x0F, +0x55..+0x57, +0x61..+0x63, +0x69..+0x6B
and the vector stay. The copy constructor's std::string allocator bytes
are the low byte of the source's address (session 57, verified).

## Dirk::CTeam (0x40 bytes; RTTI)

+0x00 id (sort key, -1 by default), +0x04 not copied by the copy
constructor 0x4a03a0 or operator= 0x4a05c0, +0x08 WCValSortedVector<int>
(the player ids; AddPlayerId 0x4a06d0 inserts at the position of its
virtual find, slot 3, mode 3), +0x24 WCValOrderedVector<Dirk::CPlayer>
(GamePlayerSetup elements). **V** (session 57).

## CLogicThread (fields our code uses; `logic_thread_layout.h`)

| Off | Meaning | Conf. |
|---|---|---|
| 0x04C | set to 1 by ExecuteCommands 0x10 when the local player's settler group 0 changed (`group_selection_changed`) | S |
| 0x224 | save_request_tick: game tick of the last multiplayer save request (0xf7 sets, 0xf9 clears; 0xfa allows a new request 937 ticks later) | V |
| 0x228 / 0x22C | save_failures / save_acks of the running multiplayer save (0xf7 clears, 0xf9 / 0xf8 count) | V |
| 0x235 | u8[20] per player, cleared by 0xf2 / 0xfe / 0xff (then 0x5411B0) | V (writes), G (meaning) |
| 0x350 | CComputerPlayer*[20]: 0xfe / 0xff create one (new 0x780, ctor 0x550900) for a dropped player | V |
| 0x3A0 | the world | V |
| 0x3B0 | step: the lockstep step the logic runs next (0 before the first) | V |
| 0x3B4 | granted_step: the last StepGrant | V |
| 0x3C4 / 0x3C8 | MessageQueue* logic -> network / network -> logic | V |
| 0x3A8, 0x3B8 (u8), 0x3C0 | Setup 0x53c970 arguments 8, 4, 1 (`setup_3a8` / `setup_3b8` / `setup_3c0`; meaning unknown) | V (writes) |
| 0x3CC / 0x3D0 | draw_slot (the shared draw-data slot) / draw_data (the buffer being filled) | V (Setup), S |
| 0x3D4 / 0x3D8 | draw_event / draw_request_event (Setup arguments 6, 7) | V (Setup), S |
| 0x414 | MessageLine[20] (0x1c each: wstring, +0x10 lifetime, +0x14 kind, +0x18 dirty); ClearMessages 0x51e650, AgeMessages 0x51e680 | V |
| 0x644 / 0x648 | message_count / message_index (ClearMessages: index = count - 1) | V |
| 0x66C / 0x670 | lobby / lobby_client (Setup arguments 9, 10) | V (writes) |
| 0x675 | desync_detected: set by 0xfb (SyncRandomCheck) | V |

**MessageQueue** (ring of dwords, Pop 0x430980 / Push 0x4309C0): +0x00 vtable (slot 3: Wait(timeout, flag), throws Win32::CTimeout*), +0x14 buffer, +0x18 capacity (power of two), +0x1C read, +0x20 write, +0x24 count, +0x28 event object (+8 = HANDLE, SetEvent on Push), +0x2C CRITICAL_SECTION*, +0x30 peak count. **V**.

## Smaller objects

- **LobbyIdObject** (unidentified lobby class, Dirk\lobbyclient.cpp /
  protocol.cpp): +0x04 id (Lobby_EqualField4 0x606AC0, Lobby_HasField4
  0x5DA3A0), strings at +0x08 and +0x18, byte +0x28, dword +0x2C,
  container +0x9C (its assignment 0x5DB590). **S**.
- **AiTaggedRecord** = the start of a **BLACK_BOARD_AGENT** (computer
  players; AI_SetWord4 0x55CA60): vtable +0 (stored by the factory
  AI::CreateAgent 0x56F6D0), word +0x04 the agent kind 0..10 (the
  subclass constructors pass 3, 5..10). **V** (constructors), details in
  `re/computer_players.md`.
- **WCSkipListDictKeyVal<unsigned int / int, String>** (Watcom): key +0,
  string +4 (destructors 0x459440, 0x49A070; 0x4581A0, 0x499470 same
  shape). **S**.
- **SessionDesc** (DirectX\netsession.cpp): DPSESSIONDESC2 copy, name
  string at +0x50 (0x45F060 probably its destructor). **S**.
- **DDBLTFX** (DirectDraw): the SDK struct (`<ddraw.h>`), 0x64 bytes,
  dwFillColor at +0x50, as CScreenElement_FillBackground builds it.

## Computer players (CComputerPlayer, notes, census)

Overview, classes and the per-step entry: `re/computer_players.md`; C++
layouts in `src/s3re/game/ai/ai_layout.h`.

- **CComputerPlayer** (0x780 bytes, vtable 0x65A530; 20 pointers at
  CLogicThread+0x350): +0x004 root note ids (100 words, 0xFFFF free,
  packed) and count +0x0CC; +0x0CE pending note ids (50) and count +0x132;
  +0x134 active note ids (100), count +0x1FC; +0x1FE the round-robin
  cursor over them (0x551670); +0x200 the notes to delete ("doomed",
  50; CComputerPlayer::DoomNote 0x551910 adds, 0x551790 deletes them after each active note's
  turn), count +0x264; +0x266 handler agent kind per note type (6 words,
  0xFFFF none; set by AddAgent 0x551400 from agent slot 2); +0x272 player number (byte); +0x274 / +0x278 / +0x27C step
  counters of 0x550F80 (periods 15, 71, 841 steps, offset by the player
  index); +0x280 notes by id (300 pointers); +0x730 agents by kind (11
  pointers); +0x75C world; +0x760 the logic->net MessageQueue; +0x764
  steps; +0x76C player index; +0x770 setup index; +0x774 the CEvaLand
  (AiLandEvaluation*, from CEvaLand::Acquire; its `lists[p]` are the per-player
  node lists, 12 bytes each: +4 list, +8 count; "area list changed" flag at
  CEvaLand +0xF0 + 4 * player); +0x778
  the current AiPlayerData node; +0x77C its key (node word +8). **V**
  (lists, notes, node pointer, cursor, agents, handlers: batch 1 and
  package A3 functions), **R** (the rest).
- **CEvaLand area record** (`AiArea`, 0x73C bytes; the value of a 0x744-byte
  node of `CEvaLand::lists[p]`, so AiPlayerData offsets are these + 8):
  +0x000 area id word, +0x004 basic economy, +0x008 / +0x00C short of
  wood / stone, +0x010 force production, +0x01C construction sites,
  +0x01E best[114] (6 bytes), +0x2CA candidate countdowns[57], +0x33C
  building counts[57], +0x3AE type blocked[57], +0x3E8 per type a pointer
  to one of the site trees, +0x4CC 39 site trees (`SiteTree`, 0x10 bytes:
  +0 allocator byte, +1 comparator byte, +4 head, +8 _Multi, +0xC size;
  nodes 0x18: left, parent, right, 6-byte value +0xC (key, pad, x, y),
  colour dword +0x14, 0 red 1 black; shared nil 0x7E0570). AiPlayerData
  +0x550 = site tree 7's size. **V** (session 55 round 2: copy constructor,
  destructor, the tree code).
- **BLACK_BOARD_NOTE** (0x1A bytes + subclass fields): +0 vtable, +4 flags
  (1 dispatched, 2 taken by an agent, 4 active), +6 type, +8 own id, +0xA
  word and +0xC dword (0 after a reset), +0x10 parent id, +0x12 next
  sibling, +0x14 first child (0xFFFF = none; AddNote 0x551270 puts a new
  child in front), +0x16 requester: kind of the agent that made it
  (AddNote's third argument; ReportToRequester 0x582480 calls its slot 9),
  +0x18 handler: kind of the agent that took it (written by the agents'
  slot 3 with flag 2; ExecuteAtHandler 0x582510 calls its slot 10, "carry it out"). **V**
  (0x582400..0x5824D0, package A3), flag 2 and +0x18's writer **R**.
- **Request notes** (HOUSE_REQUEST_NOTE, LOCAL_WARE_REQUEST_NOTE,
  T_DO_BUILD_HOUSE_NOTE; `RequestNote` in ai_layout.h): +0x1C building
  type, +0x1E x, +0x20 y, +0x22 a value (32 for the expansion's small
  towers), +0x24 / +0x26 cleared (not by the local ware note's init).
  **V** (layout, inits 0x57ab20 / 0x578760 / 0x565470), meanings **G**.
- **BLACK_BOARD_AGENT** as the CComputerPlayer uses it: +0 vtable, +4 kind
  (index in agents[]). Virtual slots called: 0 scalar deleting
  destructor, 2 note types it handles (into a word buffer, AX = count), 3
  take a new note, 4 work on an active note, 5 busy (skip 4), 7 wants to
  run, 8 run, 9 a note it made reports back (parent, note, result, value),
  10 a note it took is withdrawn (G). **V** (calls), meanings **R**/**G**.
- **AiPlayerData** node: +0x26 per building type (57) 6 bytes: build
  priority, x, y (AI::RecordBestCandidate; CComputerPlayer::
  RaiseBuildPriorities raises the priority by the tables 0x73295C up to
  0x7328E8); +0x2D2 per-type countdowns (reset to 0x7329D0[type], lowered
  every 15 steps); +0x20 dword counted up per step (`steps_since_expansion`, G: above 4200 the expand agent's MakeBuildOrders 0x56CD90 tries all six directions, "LANGE NICHT MEHR AUSGEDEHNT"; **V** read, session 56); +0x10 / +0x14 dwords: when set,
  StartBestBuilding 0x551a70 only starts woodcutter, sawmill, forester or
  small tower / stonecutter or small tower (G: short of wood / stone;
  **V** reads). **V**. Session 48
  (A2): +0x26 + 6 * (57 + type) the candidate taken for building
  (AI::TakeBestCandidate 0x58ECB0 moves best[type] there); +0x344 the
  node's buildings per type (57 words; filled by 0x572420, copied by the
  economy agent); +0x3B6 a byte per type, non-zero: the economy agent
  skips the type (cleared by 0x565630, 0x573890); +0x550 dword the
  fishery condition needs non-zero (probably "has fishing water"); +0xC
  dword basic_economy (AI::CheckBasicEconomy 0x5720C0). **V** (the
  conditions), **R** (writers).
- **GLOBAL_EXPAND_EMPIRE_AGENT** (12 bytes): +8 dword military_cursor, the
  garrisoned military building the agent expanded from last (Run
  0x56CD20). **T_MAP_WORKER_AGENT** (20 bytes): +8 byte run counter (works
  every 14th run, 0x57CDE0), +0xE word residence_cursor (the military
  building the last small residence went to, MakeBuildOrders 0x57CE90),
  +0x10 word (cleared by the constructor), +0x12 not cleared. **V**
  (session 56). **AiPlayerSetup** +0xC2 word: the expand agent's resource
  search radius in grid cells. **V**.
- **CompMapWorkerAgent globals** (ai_layout.h): AiArmyState at 0x7E58C8
  (0x51C bytes): the step counters of AI::RunMilitary 0x58F990 (+0x000
  military steps, +0x050 spell steps, +0x0F0 / +0x140 thief / mage hunt
  steps per player; +0x504 disband, +0x508 rebuild, +0x50C recruit, +0x510
  settler census, +0x514 building census, shared), +0x0A0 the gather
  countdowns, +0x190 StartAttack's warm-up calls, +0x194 the attack delays
  per player and unit group, +0x4B4 the geologist timers. AiGroupOrders
  at 0x7E37D0 (0x20F8 bytes): +0x000 home areas, +0x190 home buildings,
  +0x320 steal flags, +0x3E8 AiGroupTarget {patrol0, patrol1, prey} per
  player and unit group, +0x898 gather buildings, +0x8E8 thief cursors,
  +0x988 mages per group, +0xCA8 the settler bitset, +0x1C48 patrol step
  counters, +0x1D10 highest group, +0x1D60 group size, +0x1DB0 gather
  positions, +0x20D0 gather offsets. **V** (session 55: RunMilitary,
  ProcessWorldEvents, StartAttack and their callees).
- **T_SAFETY_AGENT** (SafetyAgent, 0x1C bytes; kind 9): +8 run counter,
  +0xA / +0xC the grid cell cursor. **V** (0x586850, 0x5868A0).
- **T_ECONOMY_AGENT** (EconomyAgent, 0xA0 bytes; kind 8): +8 race
  (Player::race), +0xC the node's area, +0x10 run counter, +0x2C the last
  have * 100 / wanted ratio, +0x30 the forester condition's priority,
  +0x34 population stage 0..3, +0x3E population (settlers of class 0 in
  the area), +0x40 carriers in the area, +0x42.. goods in stock (logs,
  boards, coal, iron, ?, gold ore, sulfur, stones), +0x52..+0x9E the
  node's buildings per type (one word per type the agent plans; the
  race-specific words hold the race's own building). Snapshot by 0x567910;
  read by the 25 conditions 0x568250..0x569D20. **V** (conditions), **R**
  (0x567910).
- **AI census** (AiAgentData, 0x1AD8 bytes; constructor 0x55B6B0): +0
  settlers counted, +1 buildings counted, +2 set by the counts, +4 / +0x194
  per player and unit group the target building / defending flag,
  +0x2B4 / +0x304 / +0x354 / +0x3A4 / +0x3F4 geologists, thieves,
  pioneers, other settlers, all settlers per player (CountSettlers
  0x55B880), +0x25C team strength per
  player (20 dwords), +0x2AC its valid flag, +0x444 soldiers per player
  (20 x 9 dwords), +0x714 finished buildings per player and type (20 x
  57 dwords). **V** (0x55BE00, 0x55BEA0).

---

## Draw cell (48 bytes, 130 x 130 at DrawData +0x500, [column][row])

The visible part of the map in the draw data (filled by PrepareDrawData
0x51f110). Read by CDrawer::QueueCellObject 0x4d5a50,
QueueLandscapeObject 0x4d3050 and QueueBuilding 0x4a9960 (session 59; C++
in src/s3re/game/graphics/draw_data_layout.h and drawer_buildings.cpp).
The DrawData header fields they read: +0x24/+0x28 map column/row of cell
(0, 0), +0x48 the animation clock, +0x60 a UI mode (1 selection sprites,
4 placement markers) with +0x64/+0x68 group/item, +0x74, +0x94 (variant
0..3), +0xa4, +0x264/+0x268 the selected cell.

| Off | Size | Meaning | Conf. |
|---|---|---|---|
| 0x00 | u8 | lift: drawn this many pixels higher | V |
| 0x02 | u8 | compared with 0x50 (water animation A), 2..7 (B), <= 7 (objects 13..16) | S |
| 0x03 | u8 | landscape object (tile byte 7) | V |
| 0x04 | u8 | owner (0xff none) | V |
| 0x05 | u8 | low 6 bits: the sprites' parameter / 4 (> 72 enables hit areas, smoke, water) | V |
| 0x07 | u8 | cell marker 1..0x20 | V |
| 0x08 | u16 | seen object (0xf800 bits 0x1800: an object, low byte) | S |
| 0x0a | u16 | animated object (frame + 1 in bits 0..6, type in 7..14); for a building cell its number (compared with g_selectedBuilding) | V |
| 0x0c | u8 | building: race 0..3; water: level (low nibble) | V |
| 0x0d | u8 | building type 1..0x38 | V |
| 0x0e | u8 | building: frame of the extra sprite | V |
| 0x10 | u8 | building: construction progress (0 finished, 1 nothing, 2..255 part) | V |
| 0x11 | u8 | building: number of sequence animations (types 0x17, 0x31) | V |
| 0x12..0x17 | u8[6] | building: worker animation flags; +0x13 is also the stock (types 0x1f, 0x2f, 0x30) | V |
| 0x1c | u8 | building: 255 - smoke amount | V |
| 0x1d | u8 | building flags: bit 2 hides type 0x11's sprite | V |
