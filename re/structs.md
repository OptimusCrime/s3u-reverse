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
`src/s3re/trace/determinism.cpp`; `UI` = presentation state.

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
| 0x06 | u8 | terrain ("worldart") | ground type; map file byte 1; high nibble = class (owned land when non-zero; 0x10 grass/land, 0x60 stream, 0x15 swamp); 0x70/0xF0 tested together (water?); 0x1C worn path (decays to 0x10); 0x16 a worked patch (SetTerrain16Patch 0x51BA00 turns a grass tile surrounded by grass into 0x16; probably a field or bed); dump: 7 on 79,249 tiles (water on this map), 16 on 42,334 | map_terrain.cpp, LogPlayerLandResources, 0x503980 | | V (field), S (values) |
| 0x07 | u8 | object | landscape object: trees 0x44..0x53, stone 0x73..0x7E (smaller = more left), others; map file byte 2; must be 0 to build; a new construction site puts 0x70 on its own tile and 0x71 (stakes) on the outermost footprint tile per direction sector (CheckBuildingSite 0x4D99E0); AddBuildingSite 0x4DD280 sets 0xFF on the door tile list of some types | ScatterObjects, AreOffsetTilesClear, map loader, LogPlayerLandResources, building_sites.cpp | | V |
| 0x08 | u8 | owner | player owning the ground, 0xFF = nobody; map file byte 3 (mapped to game players) | map_terrain.cpp, CountTilesOwnedBy, SetOwnerOfFreeTile, AI_TileByte8 0x599570 (its comment had guessed terrain); dump 255/0/1 | | V |
| 0x09 | u8 | draw flags | bit 0x40 territory border (UpdateBorderFlagsAround), bit 0x80 redraw (set by the simulation when the tile changes, e.g. 0x5039DE; cleared by PrepareDrawData); low bits unknown (dump: 63, 191, 146...) | map_terrain.cpp, determinism.cpp | bit 0x80 skip | V (0x40, 0x80) |
| 0x0A | u8 | flags | 0x01 blocked; 0x02 (UpdateSettlers sets); 0x04 being levelled (CheckBuildingSite sets it on every tile whose height a placed site changes; PlanSiteLevelling keeps such a tile and five neighbours fixed; building sites need it clear); 0x08 part of a building footprint ("Building-Bits"; set on the footprint and border tiles of a placed site by CheckBuildingSite; cleared by the map loader with 0x01 from the layout table at 0x6AA170); 0x10 temporary mark of flood fills/splits; 0x20 word +0x2 is a building; 0x40 owned (map loader sets it when the owner is valid; 0x519A90); 0x80 tested by FloodMarkAreaTerrain5To7; map file byte 4 | area_split.cpp, flood_fills.cpp, map loader | | V (0x01, 0x10, 0x20), S (others) |
| 0x0B | u8 | search mark | the search generation (+0x2D300) of the last search/overlay that visited the tile; the map loader sets 1 for tiles of a missing player | NextSearchGeneration, overlays, map loader | skip | V |
| 0x0C | u8 | overlay value | value drawn for the overlay mark (ring number etc.) | DrawOverlayLine, DrawWorkAreaOverlay, PrepareDrawData | skip, UI | V |
| 0x0D | u8 | slice byte | cleared one eighth of the map per call (ClearTileByte11Slice, counter +0x110A580); compared with 0x23 in 0x518F20; 0..40 in the dump | map_marks.cpp | | S (writes), G (meaning) |
| 0x0E | u16 | original object | zero-extended copy of the object byte made by the map loader (0x4FD5F1) and Create; bit 0x400 cleared by 0x548380 (GodlyEvents area); read by PrepareDrawData; dump: same distribution as +0x7 | map loader | | S |
| 0x10 | u8 | walk region | connected region of unblocked tiles (ids from the 256-id table at +0xE114DC); 0 = none; settlers only take work in their region; dump: 1 on 52,366 tiles | Merge/SplitTileByte10Region, PlaceSettlerNear, SendNearestWorkerToBuilding | | V (mechanics), S (meaning) |
| 0x11 | u8 | resources | high nibble kind (0 fish, 1 coal, 2 iron, 3 gold, 4 gems, 5 sulfur), low nibble amount; map file byte 5 | LogPlayerLandResources, ScatterTileByte11 | | V |
| 0x12 | u8 | traffic | incremented when a settler walks over the tile (0x503940, from the walking code 0x500860; at 12 calls 0x51B7F0 GameWorld::WearPath: grass 0x10/0x12/0x13/0x18 with grass or path around becomes path 0x1C, traffic += 9); decays in UpdateEnvironment (0x503980; at 7 a path tile 0x1C turns back into 0x10 and the tile is redrawn); 0xFF = fixed | 0x503940, 0x503980 | | S |
| 0x14 | u16 | area ("ecosec") | economy area 1..0xFFF (record in Areas); 0x8000 + n marks the tile as part of water body n (SetTileAreaMarked; terrain 5..7 flood): dump has 0x8001..0x8007 on 81,023 tiles | map_areas.cpp, area_split.cpp, LoadGame (areas 1..0xFFF not referenced are dropped) | | V |
| 0x16 | u16 | wide region | region id (8192-id table at +0xE125E8) for tiles whose 2-ring neighbourhood is unblocked and not terrain class 0x20; used by Dirk's pathfinder (0x4A1EE0, 0x4A36A0, 0x4A3870, 0x4A3940) | flood_fills.cpp | | V (mechanics), G (purpose) |
| 0x01, 0x03, 0x0F, 0x13, 0x17 | | (high bytes of words; 0x0F, 0x13, 0x17 never accessed) | always 0 in the dump | | | V |

---

## Area / economy sector ("Econ", 0x2E0 bytes, 4096 at world+0xE145E8)

Numbers 1..0xFFE (0xFFF is never handed out); high-water mark +0xE115DC
(`maxusedeconomysector`). Allocated by AllocArea (0x4E64C0), set up by
InitArea (0x4E6510, defaults per race). All fields below are from
`src/s3re/game/area_split.cpp` and its siblings (verified
reimplementations) unless marked.

| Off | Size | Meaning | Evidence | Conf. |
|---|---|---|---|---|
| 0x000 | u8 | player; 0xFF = free | ctor, AllocArea, SumAreaGoods10PerPlayer; dump | V |
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
| 0x4 | u8 | art (kind) | 0xFF free-standing pile (listed in the area); 0xFE, 0xFD seen in the dump (building piles); <= 8 counted by RecountPlayerGoods | V (values), G (meaning of 0xFE/0xFD) |
| 0x5 | u8 | | always 0 in the dump, never accessed | S (padding) |
| 0x6 | u16 | x | tile | V |
| 0x8 | u16 | y | tile | V |
| 0xA | u16 | prev | previous pile in the area's list / building's ring | V |
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
| 0x08 | u32 | direction | 0..5 (RandomBelow6 at creation and in PlaceSettlerNear; LeaveBuildingSlot sets 2); but SetSettlerAnimation (0x50E2C0) stores a graphics/animation id (group << 16 \| index, from the table at S3.EXE 0x6A8060) here and frames - 1 in +0xC, so "direction" is probably too narrow (animation/direction state?); not renamed yet | V (writes), S (name) |
| 0x0C | u16 | counter | random 0..31 at creation, reset with a new job (animation/step counter?) | V (writes), G (meaning) |
| 0x0E | u16 | job | current job ("todo"; jobs 1 and 0x37 are followed by 2 and 0x35 (IsSettlerJob01Or37 0x4FA6B0, JobAfter01Or37 0x4FAA40, probably walking); e.g. 3 work, 0x4C fetch for trade, 0x133 carry to pile, 0x145 ship, 0x147 to building, 0xAD soldier in building) | V (values), S (name) |
| 0x10 | u16 | next | list link (area/player settler lists) | V |
| 0x12 | u16 | prev | list link | V |
| 0x14 | u8 | flags | bit 0x40: in a building slot (LeaveBuildingSlot) | V (0x40) |
| 0x15 | u8 | slot | low 3 bits: the settler's slot in its building; LeaveBuildingSlot clears the tile (A.x + slot + 1, A.y) | V |
| 0x16..0x18 | u8 | | job state bytes (UpdateSettlers) | G |
| 0x17 | u8 | | 1 when a carrier takes two units (SendNearestSettlerToPile) | V |
| 0x19 | u8 | | set to 1 with a new target if it was 0 | V |
| 0x1A | u8 | good | goods type being carried / fetched | V |
| 0x1C | u32 | saved direction | restored by RestoreSettlerDirection | V |
| 0x20 | u16 | saved counter | restored with it (accessed in the folded form `(n + 0x4A97A) << 6`) | V |
| 0x22 | u16 | building | the settler's building / destination record | V |
| 0x24 | u16 | target x | SetSettlerTarget | V |
| 0x26 | u16 | target y | | V |
| 0x28 | u16 | x2 | a second position (equals the position in the dump); written by ExecuteCommands | G |
| 0x2A | u16 | y2 | | G |
| 0x2C | u8 | | 1 with a new target (path request?) | V |
| 0x2D, 0x2E | u8 | | 0x4A33D0 (Dirk\Pathing.cpp) | S (pathing) |
| 0x30..0x3A | u16[6] | path cache | set to 0xFFFF with a new target | V (writes), S (meaning) |
| 0x3C | u8 | | cleared by SendNearestSettlerToBuilding | V |
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
| 0x06 | u8 | art | building type (0x0F castle in the dump at the player position; 0x0F/0x10/0x12 military; 0x1F/0x2F/0x30/0x32 at the water; 0x20 shipyard; 0x21/0x22 trade buildings) | V (field), S (values) |
| 0x07 | u8 | race | the owner's race (AddBuildingSite 0x4DD280; indexes the layout table); read by UpdatePhase2/UpdateSettlers | V |
| 0x08 | u16 | | non-zero in finished military buildings (2 and 60 in the dump); CountBuildingsWithWord8 | G (garrison?) |
| 0x0A | u16 | door x | "tile B": the building's area is the area of this tile | V |
| 0x0C | u16 | door y | | V |
| 0x0E | u16 | | first pile of the building's ring of six (FetchGoodsToTradeBuilding); also used as a centre while under construction (DrawWorkAreaOverlay); set by AddBuildingSite 0x4DD280: 0xFF for type 1, x for types 2, 3, 5, 0x19, 0x1E, 0x33, x + 4/5/6 (clamped to the map) for types 0xE/0x18/0x24 (with +0x14 = y, y + 8/10/12); PileNeighbourBuildingByteE 0x505FD0 reads only its low byte (`mov al, [..+0x1499E72]`), PileIncAmountLeaving 0x5058C0 the whole word | V (uses) |
| 0x10 | u16 | pile 1 | input/construction pile numbers (BuildingPileHasGoods slot 1) | V |
| 0x12 | u16 | pile 2 | slot 2 | V |
| 0x14 | u16 | pile 3 | slot 3 | V |
| 0x16, 0x17 | u8 | | counters (UpdatePhase2 inc/dec) | G |
| 0x18 | u8 | request flags | bit 0x01 enables bits 0x02/0x04/0x08/0x10 (player counters +0x20..+0x28); 1 for a new site | V |
| 0x19 | u8 | | compared with pile 1's amount (SetBuildingFlag02) | V (use) |
| 0x1A | u8 | | compared with pile 2's amount (SetBuildingFlag04) | V (use) |
| 0x1B | u8 | | new site: min(8, (earth + 16) / 32 + 2) (8-bit), probably the number of diggers; cleared by BuildingDone | V (value), G (meaning) |
| 0x1C | u8 | | new site: header word [0] of the layout | V (value), G (meaning) |
| 0x1D, 0x1E | u8 | | 0 for a new site; cleared by BuildingDone; used by UpdatePhase2 | G |
| 0x19, 0x1A (new site) | u8 | materials | from the layout's word [1] (high / low byte), probably boards and stones; +0x1F = their sum for a new site | V (values), S (meaning) |
| 0x1F | u8 | state | high nibble = state (0x20 done, 0x30/0x40 shipyard states) | V (Take/ReleaseBuildingExtraTiles) |
| 0x20 | u8 | | 0xFE once the extra tiles are taken | V |
| 0x21 | u8 | buildup | non-zero while a construction site (DebugLog "building[i].buildup"); 1 for a new site, 2 for a new mine (no levelling) | V |
| 0x22 | u16 | | construction site: its levelling job slot (CheckBuildingSite); shipyard: direction of the slip (Take/ReleaseBuildingExtraTiles); military: the soldier created by BuildingDone (0x4DBFB9, folded form `(n + 0x5AEDF) * 0x3A`) | V |
| 0x24/0x26/0x28 | u16[3] | input word 24 | requested amount per input slot; construction site: word +0x28 sums the height differences of the levelled tiles (CheckBuildingSite) | V |
| 0x2A/0x2C/0x2E | u16[3] | input word 2A | next building in the area's request list per slot | V |
| 0x30 | u16 | work x | construction site: the earth to move (PlanSiteLevelling's amount); = position after BuildingDone; centre of the work area; shipyard: ship position (0xFFFF = none) | V (uses), S (work area) |
| 0x32 | u16 | work y | construction site: the player's countdown +0x84 when it was placed | V (site) |
| 0x34 | u16 | trade | number of the building's Trade record (AllocBuildingExtra caller 0x4DBC50) | V |
| 0x36 | u8 | | compared with 0x18 by TakeBuildingExtraTiles | V (use) |
| 0x38 | u16 | | 100 after BuildingDone, per type for military (S3.EXE 0x659664..); probably hit points | G |

---

## Player ("Play", 0x148 bytes, 20 at world+0x11943DC)

Defaults from the constructor (0x4FCD1F loop) and 0x4FD020. Sources:
statistics.cpp, players.cpp, player_queries.cpp, godly_events.cpp,
building_flags.cpp, settler_queries.cpp, dump (players 0 and 1).

| Off | Size | Meaning / values | Conf. |
|---|---|---|---|
| 0x000 | i32 | taking part: 0 = no; dump: 1 (local human), 2 (other player, probably computer) | V (non-zero test), G (1/2) |
| 0x004 | i32 | race 0..3 (dump 2, 3; also 0x1195FD4) | V |
| 0x008 | i32 | added in savegame version 0x89 (cleared for older saves); read by ExecuteCommands, BuildingDone | S |
| 0x00C | i32 | index in the presence maps / team (FindPlayerByField0C) | S |
| 0x010 | i32 | start position x (= castle position in the dump) | V |
| 0x014 | i32 | start position y | V |
| 0x018 | i32 | 0 in ctor; player placement 0x5074B0; dump 1 | G |
| 0x01C | i32 | -1 in ctor; dump: 0 for player 0, -1 for player 1 (controller / net id?) | G |
| 0x020 | u16[48] | heads of idle-settler lists per settler type (lists 5, 8, 0xF, 0x1E..0x23, 0x27 (+0x6E), 0x2B (+0x76) seen); see the overlap below | V |
| 0x020..0x028 | u16 x5 | building request counters (+0x24 total, capped at 10; +0x20/+0x22/+0x26/+0x28 per bit, bit 7 = "set once") | V |
| 0x080 | i32 | 0x80 in ctor | G |
| 0x084 | i32 | 0xFFFF in ctor; counts down by one per new building site (AddBuildingSite 0x4DD280), at 60000 or below it restarts at 0xFFFF; the value goes to building +0x32 | V (mechanics), G (meaning) |
| 0x088 | i32 | UI (ProcessEvents/PrepareDrawData only) | G |
| 0x08C | i32 | mana | V |
| 0x090 | i32 | 0 in ctor; ExecuteCommands, UpdatePhase2, BuildingDone | G |
| 0x094 | i32[8] | mana needed per spell slot (1 in ctor, then S3.EXE 0x658E64[spell of the race's slot]) | V |
| 0x0B4..0x0C0 | i32 x4 | written by 0x508450 (per player init) | G |
| 0x0C4..0x0D8 | i32 x6 | read by UpdatePhase2, 0x5104C0, UpdateSettlers | G |
| 0x0DC | u16[36] | goods counters per goods type (Add/SubtractPlayerCounter, RecountPlayerGoods) | V |
| 0x124 | u8 | 0 in ctor; OnStart, Step, 0x5495D0 | G |
| 0x128 | i32 | 0 in ctor; Step, 0x5495D0 | G |
| 0x12C..0x134 | i32 | game-end check 0x511C50, UpdateSettlers | G |
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
| 0x01 | u8 | 1 at allocation | V |
| 0x02, 0x04, 0x06 | u16 | route points 1..3, x (0xFFFF = unset) | V (layout), S (route) |
| 0x08 | u16 | route end x (0xFFFF = no route) | V |
| 0x0A, 0x0C, 0x0E | u16 | route points 1..3, y | V |
| 0x10 | u16 | route end y | V |
| 0x12 | u8 | read by UpdatePhase2/UpdateSettlers, PrepareDrawData | G |
| 0x14, 0x19 | u8 | written by the game-end check 0x511C50 | G |
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
  +0x32870/+0x32874): int32 type, a, b. Types: 1 (building done), 2, 4
  (area; deduplicated). Read by the AI (0x554980). **V** (layout).
- **Notification** (12 bytes, 4 lists x 100 at world+0x2D320): int32
  message type, x, y; local player only (0x4FEC10). **S**.
- **Notification timer** (12 bytes, 42 types x 10 at world+0x2E5E0): int32
  countdown (0 = unused; active entries first), x, y. CountDownTimers.
  **V** (layout).
- **Tile ring entry** (4 bytes, 10000 at world+0xDFDC48 and +0xE07890):
  uint16 x, uint16 y. **V**. Recent positions (world+0x1195DD4) store
  uint16 y, x.

## Other small records

- **MapOffset** (16 bytes, 20000 at world+0x110A588; built by 0x516530):
  int32 dx, int32 dy, int32 ring distance (0, 1 x6, 2 x12, ...,
  non-decreasing; 75 at the end), int32 direction sector 0..5. Entry 0 is
  (0, 0, 0, 0); entries 1..6 the neighbours in direction order; the last
  entry (19999) is all zero. Dump-verified. **V**.
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
  **V** (src/s3re/game/map_effects.cpp).
  Field names in `layout.h` (`MapEffect`): +0x0 start_tick (0 = free
  record; MapEffects_FindFreeSlot), +0x4 duration (i16), +0x8/+0xC
  start_x/y, +0x10/+0x14 end_x/y (pixels; MapEffect_PositionAt
  interpolates, MapEffects_Add files the record under the end position's
  256-pixel block), +0x28 arc (i16 hop height), +0x40 list (block),
  +0x44 previous, +0x48 next (MapEffect_SetLinks); the rest unkXX. Never
  written by either constructor: +0x2A..+0x2B, +0x3B, +0x3E..+0x3F
  (pad fields; the make functions copy stack garbage there). The table
  (`MapEffectTable`): +0x5CC60 cell_heads[10000], +0x668A0 slots_used,
  +0x668A4 blocks_x, +0x668A8 blocks_y, +0x668AC unknown, +0x668B0
  block_count_16, +0x668B4 width, +0x668B8 height. **V** (layout, from
  map_effects.cpp's verified functions), **S** (names).
- **New game objectives** (world+0x1195DCC): byte +0x0 non-zero = enabled;
  bytes +0x1..+0x7 the seven chosen objective ids (1..34, drawn by 0x4FFBC0
  with Random, distinct, never 21, 25, 27..31, 33, 34; checked by
  IsNgoObjectiveChosen 0x4FFB90). **V** (layout), **S** (meaning, from
  the "NGO" DebugLog texts).
- **Table at world+0x14D28A8** (16 bytes x 50, mark +0x14D28A4): byte
  +0x0, dword +0x4, dword +0xC written by 0x549530 (Volker area near
  GodlyEvents); UpdateEnvironment and 0x518F20 read it. **G**.
