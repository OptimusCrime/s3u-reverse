// Flood fills over the map that use the tile ring (world+0xdfdc48, 10,000
// (uint16 x, uint16 y) entries; read index +0xe07888, write index
// +0xe0788c) as their queue, and a step that moves a tile between areas.
//
// Each fill puts the start tile into ring slot 0 (read = 0, write = 1),
// then repeatedly takes the tile at the read index and offers its six
// neighbours to a step function, which pushes the ones it accepts at the
// write index. The six neighbours, in this order: (x-1, y) if x != 0,
// (x+1, y) if x != mapSize-1, (x, y-1) if y != 0, (x, y+1) if y !=
// mapSize-1, (x-1, y-1) if x != 0 and y != 0, (x+1, y+1) if x != mapSize-1
// and y != mapSize-1. The read index wraps from 10,000 to 0; the ring is
// never checked for overflow (a frontier of 10,000 tiles would overwrite
// unread entries, in the original as here).
//
// CGameData::FloodFillTileWord16 (0x0051c6c0): (x, y, value). The uint16 at
//   tile +0x16 of the start tile is `old`; the start tile is set to `value`
//   (low word) and every connected tile whose word equals `old` too (step
//   CGameData::ReplaceTileWord16AndQueue, which compares the zero-extended
//   word with `old` and stores the low word of `value`). Loops for ever if
//   the low word of `value` equals `old`, as the original does. x and y are
//   used at full 32 bits for the start tile, and stored as uint16 in the
//   ring. EAX = the final read index (callers 0x51c545 tail-return it).
//   ret 0xc.
// CGameData::FloodFillTileByte10 (0x0051c8a0): the same for the byte at
//   tile +0x10 (step CGameData::ReplaceTileByte10AndQueue). ret 0xc.
// CGameData::FloodMarkAreaTerrain5To7 (0x0051cee0): (x, y, value). Marks
//   the start tile and every connected tile flagged 0x10 (tile +0xa) whose
//   terrain (+0x6) is 5..7 and flag 0x80 clear with area word value +
//   0x8000 (CGameData::SetTileAreaMarked); the step
//   CGameData::QueueMarkedTileTerrain5To7 clears the 0x10 flag of every tile
//   it queues, so each tile is queued once. A do-while: the start tile is
//   processed without a check. EAX = write index (callers ignore it).
//   ret 0xc.
// CGameData::FloodSetByte10MarkedTiles (0x0051d450): (x, y, value). Like
//   FloodMarkAreaTerrain5To7, but sets the tile byte +0x10 to `value` (low
//   byte) and spreads over tiles flagged 0x10 without flag 0x01
//   (CGameData::QueueMarkedTileNoOwner). EAX = write index (ignored).
//   ret 0xc.
// CGameData::FloodSetWord16RoomyTiles (0x0051d2b0, batch 27): (x, y,
//   value). Like FloodSetByte10MarkedTiles, but sets the word +0x16 to
//   `value` (low word) and spreads with CGameData_QueueMarkedTileWithRoom
//   (logic_helpers.cpp: marked 0x10, and no blocked tile or terrain class
//   0x2_ within two steps). EAX = write index (ignored). ret 0xc. Called by
//   the area set-up 0x51a580.
// CGameData::MoveTileToAreaAndQueue (0x004e6430): (x, y, oldArea, newArea):
//   if the tile's area word (tile +0x14) equals oldArea (zero-extended,
//   32-bit compare): CGameData::RemoveTileFromArea(x, y),
//   CGameData::AssignTileToArea(x, y, newArea), and push (x, y) at the
//   write index. A step of the area flood fills (callers 0x4e6150 and
//   others). EAX (ignored by callers) reproduced: oldArea if the area
//   differed, else the incremented write index before it wraps. ret 0x10.
// CGameData::FloodMoveArea (0x004e62c0): (x, y, newArea): the start tile's
//   area word is `old`; puts the start tile in the ring, moves it to newArea
//   (RemoveTileFromArea, AssignTileToArea) and floods with the step
//   CGameData::MoveTileToAreaAndQueue(neighbour, old, newArea). Loops for
//   ever if newArea == old. EAX = the final read index (ignored). ret 0xc.
// CGameData::FloodAssignMarkedTiles (0x004e6cd0): (x, y, area, player):
//   puts the start tile in the ring; each tile taken from the ring leaves
//   its area (RemoveTileFromArea), joins `area` (AssignTileToArea) and
//   queues its neighbours with CGameData::QueueMarkedTile(player) (marked
//   0x10, not blocked, owned by the player; clears the mark). The start tile
//   is taken without these checks. EAX = the final read index. Caller
//   0x51a580 (area set-up, with a new AllocArea). ret 0x10.
// CGameData::MergeTileWord16Regions (0x0051c5e0): (x, y): looks at the
//   neighbours given by the offset table entries 1..6 (world+0x110a598):
//   the first with a non-zero word +0x16 gives the tile its word; every
//   later neighbour with another non-zero word has that id freed (byte 0 in
//   the 8192-id table at world+0xe125e8) and its region flooded with the
//   first word (FloodFillTileWord16). With no such neighbour the tile gets a
//   new id (CGameData::FindFreeId8192, marked 1 in the table). EAX = 0, or
//   the new id. ret 8.
// CGameData::MergeTileByte10Regions (0x0051ca00): the same for the byte
//   +0x10, over the neighbours that are not blocked (flag bit 0 of +0xa,
//   any value counts), with the 256-id table at world+0xe114dc
//   (FindFreeId256, FloodFillTileByte10). EAX = the number of unblocked
//   neighbours, or the new id. ret 8.
// CGameData::AssignWord16RegionsAround (0x0051c0f0): (x, y): for the
//   offset table entries 0..18 (the tile and two rings), a tile on the map
//   (unsigned compares) without a region id (word +0x16 == 0) whose own 19
//   offset tiles on the map are all unblocked (flag bit 0 of +0xa) and not
//   of terrain class 0x20 (mountain; terrain & 0xf0) gets a region through
//   CGameData::MergeTileWord16Regions. EAX (one caller tail-returns it) =
//   world + 0x110a6bc (the loop's table pointer). ret 8.
// CGameData::SplitTileWord16Region (0x0051c1f0): (x, y): the tile leaves
//   its region (word +0x16 = 0; `old` = its id before). Counts the offset
//   table neighbours 1..6 that are in `old` (no map check). None: the id is
//   freed (byte 0 at world+0xe125e8 + old). Only for 2..4 such neighbours
//   it checks whether the region falls apart: the six neighbours
//   (NeighbourX/NeighbourY, those on the map) in `old` get flag 0x10 of
//   +0xa, the others lose it; starting at the last such neighbour s, facing
//   the tile, it walks along the region (at each step it turns through the
//   directions upwards from the current one until a tile on the map in
//   `old` is found, moves there, clears its flag 0x10 and turns back by
//   one). After each step: if no neighbour of the tile is flagged any more,
//   the region is still connected and it stops. It is done when it is back
//   on s facing the same way as on its first move, or when s has no way
//   out at all; then every neighbour still flagged and in `old` gets a new
//   id (FindFreeId8192, marked 1) for its part (FloodFillTileWord16). Tiles
//   visited by the walk keep their flag 0x10 cleared. Callers ignore EAX.
//   ret 8.
// CGameData::SplitTileByte10Region (0x0051caf0): the same for the byte
//   +0x10 with the 256-id table at world+0xe114dc (FindFreeId256,
//   FloodFillTileByte10). ret 8.
// CGameData::SplitWord16RegionsAround (0x0051c080): (x, y): for the offset
//   table entries 0..18 (the tile and two rings), each tile on the map
//   (unsigned compares, map size re-read) with a region id (word +0x16 !=
//   0) gets CGameData::SplitTileWord16Region. EAX = 0 (the loop counter).
//   ret 8.
// All thiscall (ECX = world).

#include "game/enums/terrain.h"
#include "game/enums/directions.h"
#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <vector>

namespace s3re::game {

namespace {

// Tile fields (re/structs.md "Tile"): Tile::walk_region is the "byte
// +0x10" and Tile::wide_region the "word +0x16" regions of the names; the
// id tables are world->walk_region_used (256 ids) and
// world->wide_region_used (8192 ids), the queue is world->tile_ring with
// world->tile_ring_index, the offset table world->offsets.
using layout::kTileBlocked;
constexpr std::uint32_t kTileRingEntries = 10000;
static_assert(sizeof(CGameData::tile_ring) / sizeof(layout::TilePosition) == kTileRingEntries);

// Offset table entries 1..6 are the six neighbours of a tile, 0..18 the tile
// and the two rings around it.
constexpr std::uint32_t kFirstNeighbourEntry = 1;
constexpr std::uint32_t kLastNeighbourEntry = 6;
constexpr std::uint32_t kTwoRingEntries = 19;
constexpr std::uint32_t kLastDirection = kDirectionCount - 1;

// The id tables (walk_region_used, wide_region_used): 1 = id in use.
constexpr std::uint8_t kIdFree = 0;
constexpr std::uint8_t kIdInUse = 1;

// Tile (x, y) with the original's 32-bit arithmetic (neighbours from the
// offset table and test frames off the map wrap).
layout::Tile& TileAt(CGameData* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

std::uint32_t MapSize(CGameData* world) {
    return world->map_size;
}

// Ring slot 0 = (x, y), read = 0, write = 1.
void StartRing(CGameData* world, std::uint32_t x, std::uint32_t y) {
    world->tile_ring[0].x = static_cast<std::uint16_t>(x);
    world->tile_ring[0].y = static_cast<std::uint16_t>(y);
    world->tile_ring_index.read = 0;
    world->tile_ring_index.write = 1;
}

void AdvanceRead(CGameData* world) {
    std::uint32_t& read = world->tile_ring_index.read;
    read = read + 1 == kTileRingEntries ? 0 : read + 1;
}

// Calls step(nx, ny) for the six neighbours of (x, y), in the original's order.
template <typename Step>
void ForEachNeighbour(CGameData* world, std::uint32_t x, std::uint32_t y, Step step) {
    if (x != 0) {
        step(x - 1, y);
    }
    if (x != MapSize(world) - 1) {
        step(x + 1, y);
    }
    if (y != 0) {
        step(x, y - 1);
    }
    if (y != MapSize(world) - 1) {
        step(x, y + 1);
    }
    if (x != 0 && y != 0) {
        step(x - 1, y - 1);
    }
    const std::uint32_t last = MapSize(world) - 1;
    if (x != last && y != last) {
        step(x + 1, y + 1);
    }
}

// Runs the queue until it is empty: for each queued tile, visit(x, y).
template <typename Visit>
void RunQueue(CGameData* world, Visit visit) {
    do {
        const std::uint32_t read = world->tile_ring_index.read;
        const std::uint32_t x = world->tile_ring[read].x;
        const std::uint32_t y = world->tile_ring[read].y;
        visit(x, y);
        AdvanceRead(world);
    } while (world->tile_ring_index.read != world->tile_ring_index.write);
}

}  // namespace

using FloodFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x,
                                           std::uint32_t y, std::uint32_t value);

// logic_helpers.cpp
std::uint32_t __fastcall CGameData_QueueMarkedTileWithRoom(CGameData* world, void* edx, std::uint32_t x,
                                                           std::uint32_t y);
using MoveTileFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x,
                                              std::uint32_t y, std::uint32_t oldArea,
                                              std::uint32_t newArea);

// Purpose: Renumbers a whole connected region: every tile reachable from the start with the same region
// number gets the new one. Used when two regions merge.
std::uint32_t __fastcall CGameData_FloodFillTileWord16(CGameData* world, void*, std::uint32_t x,
                                                       std::uint32_t y, std::uint32_t value) {
    std::uint16_t& start = TileAt(world, x, y).wide_region;
    const std::uint32_t old = start;
    StartRing(world, x, y);
    start = static_cast<std::uint16_t>(value);
    // The original checks read != write before the first round (always true).
    RunQueue(world, [&](std::uint32_t px, std::uint32_t py) {
        ForEachNeighbour(world, px, py, [&](std::uint32_t nx, std::uint32_t ny) {
            CGameData_ReplaceTileWord16AndQueue(world, nullptr, nx, ny, old, value);
        });
    });
    return world->tile_ring_index.read;
}

// Purpose: Like FloodFillTileWord16, for the other kind of region number.
std::uint32_t __fastcall CGameData_FloodFillTileByte10(CGameData* world, void*, std::uint32_t x,
                                                       std::uint32_t y, std::uint32_t value) {
    std::uint8_t& start = TileAt(world, x, y).walk_region;
    const std::uint32_t old = start;
    StartRing(world, x, y);
    start = static_cast<std::uint8_t>(value);
    RunQueue(world, [&](std::uint32_t px, std::uint32_t py) {
        ForEachNeighbour(world, px, py, [&](std::uint32_t nx, std::uint32_t ny) {
            CGameData_ReplaceTileByte10AndQueue(world, nullptr, nx, ny, old, value);
        });
    });
    return world->tile_ring_index.read;
}

// Purpose: Gives one area number to a whole connected stretch of marked tiles of terrain types 5 to 7. Part
// of recomputing the areas.
std::uint32_t __fastcall CGameData_FloodMarkAreaTerrain5To7(CGameData* world, void*, std::uint32_t x,
                                                            std::uint32_t y, std::uint32_t value) {
    StartRing(world, x, y);
    RunQueue(world, [&](std::uint32_t px, std::uint32_t py) {
        CGameData_SetTileAreaMarked(world, nullptr, px, py, value);
        ForEachNeighbour(world, px, py, [&](std::uint32_t nx, std::uint32_t ny) {
            CGameData_QueueMarkedTileTerrain5To7(world, nullptr, nx, ny);
        });
    });
    return world->tile_ring_index.write;
}

// Purpose: Gives one region number to a whole connected stretch of marked tiles. Part of recomputing the
// regions.
std::uint32_t __fastcall CGameData_FloodSetByte10MarkedTiles(CGameData* world, void*, std::uint32_t x,
                                                             std::uint32_t y, std::uint32_t value) {
    StartRing(world, x, y);
    RunQueue(world, [&](std::uint32_t px, std::uint32_t py) {
        TileAt(world, px, py).walk_region = static_cast<std::uint8_t>(value);
        ForEachNeighbour(world, px, py, [&](std::uint32_t nx, std::uint32_t ny) {
            CGameData_QueueMarkedTileNoOwner(world, nullptr, nx, ny);
        });
    });
    return world->tile_ring_index.write;
}

// Purpose: Gives one word-16 region number to a connected stretch of marked
// tiles that have room around them (no blocked tile nearby); part of the
// area set-up (probably regions where large things can move or be built).
std::uint32_t __fastcall CGameData_FloodSetWord16RoomyTiles(CGameData* world, void*, std::uint32_t x,
                                                            std::uint32_t y, std::uint32_t value) {
    StartRing(world, x, y);
    RunQueue(world, [&](std::uint32_t px, std::uint32_t py) {
        TileAt(world, px, py).wide_region = static_cast<std::uint16_t>(value);
        ForEachNeighbour(world, px, py, [&](std::uint32_t nx, std::uint32_t ny) {
            CGameData_QueueMarkedTileWithRoom(world, nullptr, nx, ny);
        });
    });
    return world->tile_ring_index.write;
}

// Purpose: One step of moving a whole area to a new number: if a tile belongs to the old area, move it and
// remember it so its neighbours get checked next.
std::uint32_t __fastcall CGameData_MoveTileToAreaAndQueue(CGameData* world, void*, std::uint32_t x,
                                                          std::uint32_t y, std::uint32_t oldArea,
                                                          std::uint32_t newArea) {
    // The area word, addressed as the original does: ((x*0x300 + y + 0x44d9) * 24),
    // which is Tile::area of TileOffset(x, y).
    const std::uint32_t area = TileAt(world, x, y).area;
    if (area != oldArea) {
        return oldArea;
    }
    CGameData_RemoveTileFromArea(world, nullptr, x, y);
    CGameData_AssignTileToArea(world, nullptr, x, y, newArea);
    std::uint32_t& write = world->tile_ring_index.write;
    world->tile_ring[write].x = static_cast<std::uint16_t>(x);
    world->tile_ring[write].y = static_cast<std::uint16_t>(y);
    const std::uint32_t next = write + 1;
    write = next == kTileRingEntries ? 0 : next;
    return next;
}

S3RE_HOOK(0x0051c6c0, CGameData_FloodFillTileWord16, FloodFn, Original_CGameData_FloodFillTileWord16,
          CGameData_FloodFillTileWord16);
S3RE_HOOK(0x0051c8a0, CGameData_FloodFillTileByte10, FloodFn, Original_CGameData_FloodFillTileByte10,
          CGameData_FloodFillTileByte10);
S3RE_HOOK(0x0051cee0, CGameData_FloodMarkAreaTerrain5To7, FloodFn,
          Original_CGameData_FloodMarkAreaTerrain5To7, CGameData_FloodMarkAreaTerrain5To7);
S3RE_HOOK(0x0051d450, CGameData_FloodSetByte10MarkedTiles, FloodFn,
          Original_CGameData_FloodSetByte10MarkedTiles, CGameData_FloodSetByte10MarkedTiles);
S3RE_HOOK(0x0051d2b0, CGameData_FloodSetWord16RoomyTiles, FloodFn,
          Original_CGameData_FloodSetWord16RoomyTiles, CGameData_FloodSetWord16RoomyTiles);
S3RE_HOOK(0x004e6430, CGameData_MoveTileToAreaAndQueue, MoveTileFn,
          Original_CGameData_MoveTileToAreaAndQueue, CGameData_MoveTileToAreaAndQueue);

using MergeFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);

// Purpose: Moves all tiles connected to a starting tile from one area to another, spreading outwards like
// paint. Used when areas are joined or split, e.g. after territory changes.
std::uint32_t __fastcall CGameData_FloodMoveArea(CGameData* world, void*, std::uint32_t x, std::uint32_t y,
                                                 std::uint32_t newArea) {
    const std::uint32_t old = TileAt(world, x, y).area;
    StartRing(world, x, y);
    CGameData_RemoveTileFromArea(world, nullptr, x, y);
    CGameData_AssignTileToArea(world, nullptr, x, y, newArea);
    RunQueue(world, [&](std::uint32_t px, std::uint32_t py) {
        ForEachNeighbour(world, px, py, [&](std::uint32_t nx, std::uint32_t ny) {
            CGameData_MoveTileToAreaAndQueue(world, nullptr, nx, ny, old, newArea);
        });
    });
    return world->tile_ring_index.read;
}

// Purpose: Puts a connected patch of marked tiles of one player into an area, spreading outwards from a
// start tile. Used when the map's areas are first set up (probably at game start).
std::uint32_t __fastcall CGameData_FloodAssignMarkedTiles(CGameData* world, void*, std::uint32_t x, std::uint32_t y,
                                                          std::uint32_t area, std::uint32_t player) {
    StartRing(world, x, y);
    RunQueue(world, [&](std::uint32_t px, std::uint32_t py) {
        CGameData_RemoveTileFromArea(world, nullptr, px, py);
        CGameData_AssignTileToArea(world, nullptr, px, py, area);
        ForEachNeighbour(world, px, py, [&](std::uint32_t nx, std::uint32_t ny) {
            CGameData_QueueMarkedTile(world, nullptr, nx, ny, player);
        });
    });
    return world->tile_ring_index.read;
}

namespace {

// The neighbour from offset table entry k (1..6) of (x, y).
std::uint32_t OffsetNeighbourX(CGameData* world, std::uint32_t x, std::uint32_t k) {
    return x + static_cast<std::uint32_t>(world->offsets[k].dx);
}
std::uint32_t OffsetNeighbourY(CGameData* world, std::uint32_t y, std::uint32_t k) {
    return y + static_cast<std::uint32_t>(world->offsets[k].dy);
}

}  // namespace

// Purpose: When a tile becomes part of the walkable network (e.g. after something blocking it is removed),
// it joins the region of its neighbours; if it connects several regions they are merged into one;
// if it has no neighbour region, it starts a new one.
std::uint32_t __fastcall CGameData_MergeTileWord16Regions(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    std::uint32_t count = 0;
    std::uint32_t first = 0;
    for (std::uint32_t k = kFirstNeighbourEntry; k <= kLastNeighbourEntry; ++k) {
        const std::uint32_t nx = OffsetNeighbourX(world, x, k);
        const std::uint32_t ny = OffsetNeighbourY(world, y, k);
        const std::uint32_t id = TileAt(world, nx, ny).wide_region;
        if (id == 0) {
            continue;
        }
        if (++count == 1) {
            first = id;
            TileAt(world, x, y).wide_region = static_cast<std::uint16_t>(id);
        } else if (id != first) {
            world->wide_region_used[id] = kIdFree;
            CGameData_FloodFillTileWord16(world, nullptr, nx, ny, first);
        }
    }
    if (count != 0) {
        return 0;
    }
    const std::uint32_t id = CGameData_FindFreeId8192(world, nullptr);
    world->wide_region_used[id] = kIdInUse;
    TileAt(world, x, y).wide_region = static_cast<std::uint16_t>(id);
    return id;
}

// Purpose: Like MergeTileWord16Regions, for the other kind of region (over unblocked tiles).
std::uint32_t __fastcall CGameData_MergeTileByte10Regions(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    std::uint32_t count = 0;
    std::uint32_t first = 0;
    for (std::uint32_t k = kFirstNeighbourEntry; k <= kLastNeighbourEntry; ++k) {
        const std::uint32_t nx = OffsetNeighbourX(world, x, k);
        const std::uint32_t ny = OffsetNeighbourY(world, y, k);
        const layout::Tile& tile = TileAt(world, nx, ny);
        if ((tile.flags & kTileBlocked) != 0) {
            continue;
        }
        const std::uint32_t id = tile.walk_region;
        if (++count == 1) {
            first = id;
            TileAt(world, x, y).walk_region = static_cast<std::uint8_t>(id);
        } else if (id != first) {
            world->walk_region_used[id] = kIdFree;
            CGameData_FloodFillTileByte10(world, nullptr, nx, ny, first);
        }
    }
    if (count != 0) {
        return count;  // unlike MergeTileWord16Regions, which returns 0 here
    }
    const std::uint32_t id = CGameData_FindFreeId256(world, nullptr);
    world->walk_region_used[id] = kIdInUse;
    TileAt(world, x, y).walk_region = static_cast<std::uint8_t>(id);
    return id;
}

S3RE_HOOK(0x004e62c0, CGameData_FloodMoveArea, FloodFn, Original_CGameData_FloodMoveArea, CGameData_FloodMoveArea);
using FloodAssignFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                 std::uint32_t area, std::uint32_t player);
S3RE_HOOK(0x004e6cd0, CGameData_FloodAssignMarkedTiles, FloodAssignFn, Original_CGameData_FloodAssignMarkedTiles,
          CGameData_FloodAssignMarkedTiles);
S3RE_HOOK(0x0051c5e0, CGameData_MergeTileWord16Regions, MergeFn, Original_CGameData_MergeTileWord16Regions,
          CGameData_MergeTileWord16Regions);
S3RE_HOOK(0x0051ca00, CGameData_MergeTileByte10Regions, MergeFn, Original_CGameData_MergeTileByte10Regions,
          CGameData_MergeTileByte10Regions);

// Purpose: After something changed near a tile, gives region numbers to the nearby tiles that should have
// one now (no blocked tile or mountain within two steps) but don't yet.
std::uint32_t __fastcall CGameData_AssignWord16RegionsAround(CGameData* world, void*, std::uint32_t x,
                                                              std::uint32_t y) {
    constexpr std::uint32_t kEntries = kTwoRingEntries;
    for (std::uint32_t k = 0; k < kEntries; ++k) {
        const std::uint32_t size = MapSize(world);
        const std::uint32_t nx = OffsetNeighbourX(world, x, k);
        const std::uint32_t ny = OffsetNeighbourY(world, y, k);
        if (nx >= size || ny >= size || TileAt(world, nx, ny).wide_region != 0) {
            continue;
        }
        bool clear = true;
        for (std::uint32_t j = 0; j < kEntries; ++j) {
            const std::uint32_t mx = OffsetNeighbourX(world, nx, j);
            const std::uint32_t my = OffsetNeighbourY(world, ny, j);
            if (mx >= size || my >= size) {
                continue;
            }
            const layout::Tile& tile = TileAt(world, mx, my);
            if ((tile.flags & kTileBlocked) != 0 || (tile.terrain & kTerrainClassMask) == kTerrainClassMountain) {
                clear = false;
            }
        }
        if (clear) {
            CGameData_MergeTileWord16Regions(world, nullptr, nx, ny);
        }
    }
    // EAX: the loop's table pointer, &offsets[19].dy.
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&world->offsets[kEntries].dy));
}

S3RE_HOOK(0x0051c0f0, CGameData_AssignWord16RegionsAround, MergeFn, Original_CGameData_AssignWord16RegionsAround,
          CGameData_AssignWord16RegionsAround);

namespace {

// The two kinds of region ids that SplitTileRegion works on.
struct Word16Regions {
    using Id = std::uint16_t;
    static Id& Field(layout::Tile& tile) { return tile.wide_region; }
    static std::uint8_t* Ids(CGameData* world) { return world->wide_region_used; }
    static std::uint32_t NewId(CGameData* world) { return CGameData_FindFreeId8192(world, nullptr); }
    static void Fill(CGameData* world, std::uint32_t x, std::uint32_t y, std::uint32_t id) {
        CGameData_FloodFillTileWord16(world, nullptr, x, y, id);
    }
};

struct Byte10Regions {
    using Id = std::uint8_t;
    static Id& Field(layout::Tile& tile) { return tile.walk_region; }
    static std::uint8_t* Ids(CGameData* world) { return world->walk_region_used; }
    static std::uint32_t NewId(CGameData* world) { return CGameData_FindFreeId256(world, nullptr); }
    static void Fill(CGameData* world, std::uint32_t x, std::uint32_t y, std::uint32_t id) {
        CGameData_FloodFillTileByte10(world, nullptr, x, y, id);
    }
};

constexpr std::uint8_t kTileSplitMark = layout::kTileFloodMark;  // in Tile::flags

template <typename Regions>
void SplitTileRegion(CGameData* world, std::uint32_t x, std::uint32_t y) {
    auto id = [&](std::uint32_t tx, std::uint32_t ty) -> std::uint32_t {
        return Regions::Field(TileAt(world, tx, ty));
    };
    auto flags = [&](std::uint32_t tx, std::uint32_t ty) -> std::uint8_t& {
        return TileAt(world, tx, ty).flags;
    };
    auto onMap = [&](std::uint32_t tx, std::uint32_t ty) { return tx < MapSize(world) && ty < MapSize(world); };

    const std::uint32_t old = id(x, y);
    Regions::Field(TileAt(world, x, y)) = 0;
    std::uint32_t same = 0;
    for (std::uint32_t k = kFirstNeighbourEntry; k <= kLastNeighbourEntry; ++k) {
        if (id(OffsetNeighbourX(world, x, k), OffsetNeighbourY(world, y, k)) == old) {
            ++same;
        }
    }
    if (same == 0) {
        Regions::Ids(world)[old] = kIdFree;
        return;
    }
    if (same < 2 || same > 4) {
        return;
    }
    // Flag the neighbours in the region; remember the last one.
    std::uint32_t last = 0;
    for (std::uint32_t d = 0; d < kDirectionCount; ++d) {
        const std::uint32_t nx = NeighbourX(x, d);
        const std::uint32_t ny = NeighbourY(y, d);
        if (!onMap(nx, ny)) {
            continue;
        }
        if (id(nx, ny) == old) {
            flags(nx, ny) = static_cast<std::uint8_t>(flags(nx, ny) | kTileSplitMark);
            last = d;
        } else {
            flags(nx, ny) = static_cast<std::uint8_t>(flags(nx, ny) & ~kTileSplitMark);
        }
    }
    const std::uint32_t sx = NeighbourX(x, last);
    const std::uint32_t sy = NeighbourY(y, last);
    // Facing the tile: the opposite direction.
    constexpr std::uint32_t kHalfTurn = kDirectionCount / 2;
    std::uint32_t direction = last + kHalfTurn >= kDirectionCount ? last - kHalfTurn : last + kHalfTurn;
    flags(sx, sy) = static_cast<std::uint8_t>(flags(sx, sy) & ~kTileSplitMark);
    std::uint32_t cx = sx;
    std::uint32_t cy = sy;
    bool started = false;
    std::uint32_t firstDirection = 0;
    bool split = false;
    for (;;) {
        // One step of the walk.
        for (std::uint32_t tries = 1;; ++tries) {
            const std::uint32_t nx = NeighbourX(cx, direction);
            const std::uint32_t ny = NeighbourY(cy, direction);
            if (onMap(nx, ny) && id(nx, ny) == old) {
                if (started && cx == sx && cy == sy && firstDirection == direction) {
                    split = true;
                }
                cx = nx;
                cy = ny;
                if (!started) {
                    started = true;
                    firstDirection = direction;
                }
                direction = direction == 0 ? kLastDirection : direction - 1;
                flags(nx, ny) = static_cast<std::uint8_t>(flags(nx, ny) & ~kTileSplitMark);
                break;
            }
            if (tries == kDirectionCount && !started) {
                split = true;
            }
            direction = direction == kLastDirection ? 0 : direction + 1;
            if (tries == kDirectionCount) {
                break;
            }
        }
        bool flagged = false;
        for (std::uint32_t d = 0; d < kDirectionCount && !flagged; ++d) {
            flagged = (flags(NeighbourX(x, d), NeighbourY(y, d)) & kTileSplitMark) != 0;
        }
        if (!flagged) {
            return;  // every neighbour was reached: still one region
        }
        if (split) {
            break;
        }
    }
    for (std::uint32_t d = 0; d < kDirectionCount; ++d) {
        const std::uint32_t nx = NeighbourX(x, d);
        const std::uint32_t ny = NeighbourY(y, d);
        if (id(nx, ny) == old && (flags(nx, ny) & kTileSplitMark) != 0) {
            const std::uint32_t newId = Regions::NewId(world);
            Regions::Ids(world)[newId] = kIdInUse;
            Regions::Fill(world, nx, ny, newId);
        }
    }
}

}  // namespace

using SplitFn = void(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);

// Purpose: When a tile stops being part of the walkable network (e.g. something is built on it), its
// region may fall apart into pieces that can no longer reach each other; each cut-off piece gets a new
// region number.
void __fastcall CGameData_SplitTileWord16Region(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    SplitTileRegion<Word16Regions>(world, x, y);
}

// Purpose: Like SplitTileWord16Region, for the other kind of region number.
void __fastcall CGameData_SplitTileByte10Region(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    SplitTileRegion<Byte10Regions>(world, x, y);
}

// Purpose: After something changed near a tile, checks for every nearby tile with a region number whether
// its region has fallen apart.
std::uint32_t __fastcall CGameData_SplitWord16RegionsAround(CGameData* world, void*, std::uint32_t x,
                                                            std::uint32_t y) {
    for (std::uint32_t k = 0; k < 19; ++k) {
        const std::uint32_t nx = OffsetNeighbourX(world, x, k);
        const std::uint32_t ny = OffsetNeighbourY(world, y, k);
        const std::uint32_t size = MapSize(world);
        if (nx < size && ny < size && TileAt(world, nx, ny).wide_region != 0) {
            CGameData_SplitTileWord16Region(world, nullptr, nx, ny);
        }
    }
    return 0;
}

S3RE_HOOK(0x0051c1f0, CGameData_SplitTileWord16Region, SplitFn, Original_CGameData_SplitTileWord16Region,
          CGameData_SplitTileWord16Region);
S3RE_HOOK(0x0051caf0, CGameData_SplitTileByte10Region, SplitFn, Original_CGameData_SplitTileByte10Region,
          CGameData_SplitTileByte10Region);
S3RE_HOOK(0x0051c080, CGameData_SplitWord16RegionsAround, MergeFn, Original_CGameData_SplitWord16RegionsAround,
          CGameData_SplitWord16RegionsAround);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// Covers the tiles, the ring and its indices; area records are only touched
// by MoveTileToAreaAndQueue, which has its own, larger world.
constexpr std::size_t kTestWorld = offsetof(CGameData, tile_ring_index) + sizeof(layout::Ring1000Indices);
constexpr std::uint32_t kTestAreas = 64;
// Up to the tile count of area record kTestAreas.
constexpr std::size_t kAreaTestWorld = offsetof(CGameData, areas) + 4 + kTestAreas * sizeof(layout::Area);

// The two kinds of tile regions the tests set up: Tile::wide_region with
// world->wide_region_used (the "word +0x16" ones) or Tile::walk_region with
// world->walk_region_used (the "byte +0x10" ones).
enum class RegionField { kWord16, kByte10 };

void SetRegionId(CGameData* world, std::uint32_t x, std::uint32_t y, RegionField field, std::uint32_t value) {
    if (field == RegionField::kWord16) {
        TileAt(world, x, y).wide_region = static_cast<std::uint16_t>(value);
    } else {
        TileAt(world, x, y).walk_region = static_cast<std::uint8_t>(value);
    }
}

std::uint8_t* RegionIdsUsed(CGameData* world, RegionField field) {
    return field == RegionField::kWord16 ? world->wide_region_used : world->walk_region_used;
}

std::uint32_t& RegionIdsHighWater(CGameData* world, RegionField field) {
    return field == RegionField::kWord16 ? world->wide_region_high_water : world->walk_region_high_water;
}

std::uint32_t RegionIdsSize(RegionField field) {
    return field == RegionField::kWord16 ? 0x2000 : 0x100;
}

// A random map size: often the full 384, sometimes small (edges are hit often).
std::uint32_t RandomMapSize(test::Context& ctx) {
    return ctx.rng().Chance(1, 3) ? 384 : 3 + ctx.rng().Below(60);
}

struct Region {
    std::uint32_t x0, x1, y0, y1;  // inclusive
};

// A random rectangle inside the map that contains (x, y); 1 in 8 the whole map.
Region RandomRegion(test::Context& ctx, std::uint32_t size, std::uint32_t x, std::uint32_t y) {
    if (ctx.rng().Chance(1, 8)) {
        return {0, size - 1, 0, size - 1};
    }
    const std::uint32_t reach = 1 + ctx.rng().Below(40);
    auto low = [&](std::uint32_t v) { return v > reach ? v - ctx.rng().Below(reach + 1) : ctx.rng().Below(v + 1); };
    auto high = [&](std::uint32_t v) {
        const std::uint32_t room = size - 1 - v;
        return v + ctx.rng().Below((room < reach ? room : reach) + 1);
    };
    return {low(x), high(x), low(y), high(y)};
}

template <typename Setup>
void ForRegion(const Region& region, Setup setup) {
    for (std::uint32_t x = region.x0; x <= region.x1; ++x) {
        for (std::uint32_t y = region.y0; y <= region.y1; ++y) {
            setup(x, y);
        }
    }
}

// Runs `cases` cases: `setup` prepares both worlds and returns the stack
// arguments; EAX and the whole world are compared after every case.
template <typename Fn, typename Setup>
void CompareFloods(test::Context& ctx, Fn original, Fn candidate, int cases, Setup setup) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = RandomMapSize(ctx);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        std::uint32_t x = ctx.rng().Below(size);
        std::uint32_t y = ctx.rng().Below(size);
        std::uint32_t value = 0;
        setup(worlds, size, x, y, value);
        const std::uint32_t expected = original(worlds.original(), nullptr, x, y, value);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, x, y, value);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%u, %u) value 0x%x, map %u", i, x, y, value, size);
            return;
        }
    }
}

// Region tiles mostly hold `old` in the field (1 or 2 bytes);
// the new value never has `old` in its low bytes (that would loop for ever).
void SetUpReplaceFill(test::Context& ctx, test::WorldPair& worlds, std::uint32_t size, std::uint32_t x,
                      std::uint32_t y, std::uint32_t& value, RegionField field) {
    const std::uint32_t bytes = field == RegionField::kWord16 ? 2 : 1;
    const std::uint32_t old = ctx.rng().Below(3);
    const Region region = RandomRegion(ctx, size, x, y);
    const std::uint32_t density = 1 + ctx.rng().Below(4);  // of 4
    ForRegion(region, [&](std::uint32_t tx, std::uint32_t ty) {
        // The start tile always holds `old` (else `value` could equal it).
        const bool start = tx == x && ty == y;
        const std::uint32_t v = start || ctx.rng().Chance(density, 4) ? old : 3 + ctx.rng().Below(3);
        worlds.Both([&](CGameData* w) { SetRegionId(w, tx, ty, field, v); });
    });
    value = old + 1 + ctx.rng().Below(6);
    if (ctx.rng().Chance(1, 4)) {
        value |= ctx.rng().Next32() & (bytes == 2 ? 0xffff0000u : 0xffffff00u);
    }
}

// Region tiles mostly carry flag 0x10; `terrain` also sets terrain 5..7 and
// clears 0x80 on most, otherwise flag 0x01 is mostly clear.
void SetUpMarkedFill(test::Context& ctx, test::WorldPair& worlds, std::uint32_t size, std::uint32_t x,
                     std::uint32_t y, std::uint32_t& value, bool terrain) {
    const Region region = RandomRegion(ctx, size, x, y);
    ForRegion(region, [&](std::uint32_t tx, std::uint32_t ty) {
        std::uint8_t flags = static_cast<std::uint8_t>(ctx.rng().Next32());
        if (ctx.rng().Chance(3, 4)) {
            flags = static_cast<std::uint8_t>((flags | 0x10) & (terrain ? ~0x80 : ~0x01));
        }
        worlds.Both([&](CGameData* w) { TileAt(w, tx, ty).flags = flags; });
        if (terrain) {
            const auto type = static_cast<std::uint8_t>(ctx.rng().Chance(4, 5) ? 5 + ctx.rng().Below(3)
                                                                               : ctx.rng().Below(10));
            worlds.Both([&](CGameData* w) { TileAt(w, tx, ty).terrain = type; });
        }
    });
    value = ctx.rng().Chance(1, 2) ? ctx.rng().Below(0x200) : ctx.rng().Next32();
}

// Wrong: spreads to four neighbours only (no diagonals).
std::uint32_t __fastcall WrongFloodFillTileWord16(CGameData* world, void*, std::uint32_t x, std::uint32_t y,
                                                  std::uint32_t value) {
    std::uint16_t& start = TileAt(world, x, y).wide_region;
    const std::uint32_t old = start;
    StartRing(world, x, y);
    start = static_cast<std::uint16_t>(value);
    RunQueue(world, [&](std::uint32_t px, std::uint32_t py) {
        ForEachNeighbour(world, px, py, [&](std::uint32_t nx, std::uint32_t ny) {
            if (nx == px || ny == py) {
                CGameData_ReplaceTileWord16AndQueue(world, nullptr, nx, ny, old, value);
            }
        });
    });
    return world->tile_ring_index.read;
}

void CompareWord16Fill(test::Context& ctx, FloodFn candidate, int cases) {
    CompareFloods(ctx, Original_CGameData_FloodFillTileWord16, candidate, cases,
                  [&](test::WorldPair& worlds, std::uint32_t size, std::uint32_t x, std::uint32_t y,
                      std::uint32_t& value) { SetUpReplaceFill(ctx, worlds, size, x, y, value, RegionField::kWord16); });
}

// Wrong: offers the (x+1, y+1) neighbour first. The same tiles get marked,
// but the ring holds them in another order.
std::uint32_t __fastcall WrongFloodMarkAreaTerrain5To7(CGameData* world, void* edx, std::uint32_t x,
                                                       std::uint32_t y, std::uint32_t value) {
    StartRing(world, x, y);
    RunQueue(world, [&](std::uint32_t px, std::uint32_t py) {
        CGameData_SetTileAreaMarked(world, edx, px, py, value);
        const std::uint32_t last = MapSize(world) - 1;
        if (px != last && py != last) {
            CGameData_QueueMarkedTileTerrain5To7(world, nullptr, px + 1, py + 1);
        }
        ForEachNeighbour(world, px, py, [&](std::uint32_t nx, std::uint32_t ny) {
            if (!(nx == px + 1 && ny == py + 1)) {
                CGameData_QueueMarkedTileTerrain5To7(world, nullptr, nx, ny);
            }
        });
    });
    return world->tile_ring_index.write;
}

void CompareTerrainFill(test::Context& ctx, FloodFn candidate, int cases) {
    CompareFloods(ctx, Original_CGameData_FloodMarkAreaTerrain5To7, candidate, cases,
                  [&](test::WorldPair& worlds, std::uint32_t size, std::uint32_t x, std::uint32_t y,
                      std::uint32_t& value) { SetUpMarkedFill(ctx, worlds, size, x, y, value, true); });
}

}  // namespace

S3RE_TEST(CGameData_FloodFillTileWord16_MatchesOriginal) {
    CompareWord16Fill(ctx, &CGameData_FloodFillTileWord16, 300);
}
S3RE_TEST(CGameData_FloodFillTileWord16_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareWord16Fill(probe, &WrongFloodFillTileWord16, 100);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_FloodFillTileByte10_MatchesOriginal) {
    CompareFloods(ctx, Original_CGameData_FloodFillTileByte10, &CGameData_FloodFillTileByte10, 300,
                  [&](test::WorldPair& worlds, std::uint32_t size, std::uint32_t x, std::uint32_t y,
                      std::uint32_t& value) { SetUpReplaceFill(ctx, worlds, size, x, y, value, RegionField::kByte10); });
}
S3RE_TEST(CGameData_FloodMarkAreaTerrain5To7_MatchesOriginal) {
    CompareTerrainFill(ctx, &CGameData_FloodMarkAreaTerrain5To7, 300);
}
S3RE_TEST(CGameData_FloodMarkAreaTerrain5To7_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareTerrainFill(probe, &WrongFloodMarkAreaTerrain5To7, 100);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_FloodSetByte10MarkedTiles_MatchesOriginal) {
    CompareFloods(ctx, Original_CGameData_FloodSetByte10MarkedTiles, &CGameData_FloodSetByte10MarkedTiles, 300,
                  [&](test::WorldPair& worlds, std::uint32_t size, std::uint32_t x, std::uint32_t y,
                      std::uint32_t& value) { SetUpMarkedFill(ctx, worlds, size, x, y, value, false); });
}

namespace {

// Marked tiles (flag 0x10, mostly unblocked) with terrain that is rarely of
// class 0x2_, so that the two-step room check passes often enough.
void SetUpRoomyFill(test::Context& ctx, test::WorldPair& worlds, std::uint32_t size, std::uint32_t x,
                    std::uint32_t y, std::uint32_t& value) {
    const Region region = RandomRegion(ctx, size, x, y);
    const std::uint32_t blocked = ctx.rng().Below(40);  // of 1000
    ForRegion(region, [&](std::uint32_t tx, std::uint32_t ty) {
        std::uint8_t flags = static_cast<std::uint8_t>(ctx.rng().Next32());
        if (!ctx.rng().Chance(blocked, 1000)) {
            flags = static_cast<std::uint8_t>((flags | 0x10) & ~0x01);
        }
        worlds.Both([&](CGameData* w) { TileAt(w, tx, ty).flags = flags; });
        std::uint8_t terrain = static_cast<std::uint8_t>(ctx.rng().Next32());
        if ((terrain & kTerrainClassMask) == kTerrainClassMountain && !ctx.rng().Chance(1, 20)) {
            terrain ^= 0x40;
        }
        worlds.Both([&](CGameData* w) { TileAt(w, tx, ty).terrain = terrain; });
    });
    value = ctx.rng().Chance(1, 2) ? ctx.rng().Below(0x200) : ctx.rng().Next32();
}

// Wrong: writes only the low byte of the region word.
std::uint32_t __fastcall WrongFloodSetWord16RoomyTiles(CGameData* world, void* edx, std::uint32_t x,
                                                       std::uint32_t y, std::uint32_t value) {
    return CGameData_FloodSetWord16RoomyTiles(world, edx, x, y, value & 0xff);
}

}  // namespace

S3RE_TEST(CGameData_FloodSetWord16RoomyTiles_MatchesOriginal) {
    CompareFloods(ctx, Original_CGameData_FloodSetWord16RoomyTiles, &CGameData_FloodSetWord16RoomyTiles, 300,
                  [&](test::WorldPair& worlds, std::uint32_t size, std::uint32_t x, std::uint32_t y,
                      std::uint32_t& value) { SetUpRoomyFill(ctx, worlds, size, x, y, value); });
}

S3RE_TEST(CGameData_FloodSetWord16RoomyTiles_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareFloods(probe, Original_CGameData_FloodSetWord16RoomyTiles, &WrongFloodSetWord16RoomyTiles, 100,
                  [&](test::WorldPair& worlds, std::uint32_t size, std::uint32_t x, std::uint32_t y,
                      std::uint32_t& value) { SetUpRoomyFill(probe, worlds, size, x, y, value); });
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CGameData_MoveTileToAreaAndQueue_MatchesOriginal) {
    test::WorldPair worlds(ctx, kAreaTestWorld);
    for (int i = 0; i < 3000; ++i) {
        const std::uint32_t x = ctx.rng().Below(384);
        const std::uint32_t y = ctx.rng().Below(384);
        const std::uint32_t area = ctx.rng().Below(kTestAreas);
        worlds.Both([&](CGameData* w) { TileAt(w, x, y).area = static_cast<std::uint16_t>(area); });
        std::uint32_t oldArea = ctx.rng().Chance(2, 3) ? area : ctx.rng().Below(kTestAreas);
        if (ctx.rng().Chance(1, 16)) {
            oldArea |= 0x10000u << ctx.rng().Below(16);  // equal low word, but not equal
        }
        const std::uint32_t newArea = ctx.rng().Below(kTestAreas);
        const std::uint32_t write = ctx.rng().Chance(1, 8) ? kTileRingEntries - 1 : ctx.rng().Below(kTileRingEntries);
        worlds.Both([&](CGameData* w) { w->tile_ring_index.write = write; });
        const std::uint32_t expected =
            Original_CGameData_MoveTileToAreaAndQueue(worlds.original(), nullptr, x, y, oldArea, newArea);
        const std::uint32_t actual = CGameData_MoveTileToAreaAndQueue(worlds.ours(), nullptr, x, y, oldArea, newArea);
        if (!S3RE_EXPECT_EQ(expected, actual) ||
            ((i % 64 == 63 || i == 2999) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 63 before it)", i);
            return;
        }
    }
}

namespace {

constexpr std::size_t kMergeTestWorld = offsetof(CGameData, offsets) + 20 * sizeof(layout::MapOffset);

void SetOffset(test::WorldPair& worlds, std::uint32_t k, std::int32_t dx, std::int32_t dy) {
    worlds.Both([&](CGameData* w) {
        w->offsets[k].dx = dx;
        w->offsets[k].dy = dy;
    });
}

// The real first 7 entries of the offset table: the tile and its neighbours.
void SetUpNeighbourOffsets(test::WorldPair& worlds) {
    static constexpr std::int32_t kOffsets[7][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {-1, 0}, {-1, -1}, {0, -1}};
    for (std::uint32_t k = 0; k < 7; ++k) {
        SetOffset(worlds, k, kOffsets[k][0], kOffsets[k][1]);
    }
}

// Ids table: ids 1..first-1 used, then free (FindFreeId scans from 1).
void SetUpIds(test::Context& ctx, test::WorldPair& worlds, RegionField field) {
    const std::uint32_t used = 1 + ctx.rng().Below(40);
    for (std::uint32_t id = 0; id < used + 2 && id < RegionIdsSize(field); ++id) {
        worlds.Both([&](CGameData* w) { RegionIdsUsed(w, field)[id] = static_cast<std::uint8_t>(id < used ? 1 : 0); });
    }
    const std::uint32_t mark = ctx.rng().Below(used + 2);
    worlds.Both([&](CGameData* w) { RegionIdsHighWater(w, field) = mark; });
}

template <typename Setup>
void CompareMerge(test::Context& ctx, MergeFn original, MergeFn candidate, int cases, Setup setup) {
    test::WorldPair worlds(ctx, kMergeTestWorld);
    SetUpNeighbourOffsets(worlds);
    for (int i = 0; i < cases; ++i) {
        // A small map (the floods stay small), the tile inside it.
        const std::uint32_t size = 4 + ctx.rng().Below(24);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        const std::uint32_t x = 1 + ctx.rng().Below(size - 2);
        const std::uint32_t y = 1 + ctx.rng().Below(size - 2);
        setup(worlds, size);
        const std::uint32_t expected = original(worlds.original(), nullptr, x, y);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, x, y);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 32 == 31 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 31 before it): (%u, %u) map %u", i, x, y, size);
            return;
        }
    }
}

// Wrong: frees the merged id in the table of the other kind.
std::uint32_t __fastcall WrongMergeTileWord16Regions(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t result = CGameData_MergeTileWord16Regions(world, edx, x, y);
    world->wide_region_used[1] = 1;
    return result;
}

void CompareWord16Merge(test::Context& ctx, MergeFn candidate, int cases) {
    CompareMerge(ctx, Original_CGameData_MergeTileWord16Regions, candidate, cases,
                 [&](test::WorldPair& worlds, std::uint32_t size) {
                     // Region ids 0..3 (0 = none) over the whole small map.
                     for (std::uint32_t tx = 0; tx < size; ++tx) {
                         for (std::uint32_t ty = 0; ty < size; ++ty) {
                             const std::uint32_t id = ctx.rng().Below(4);
                             worlds.Both([&](CGameData* w) { SetRegionId(w, tx, ty, RegionField::kWord16, id); });
                         }
                     }
                     SetUpIds(ctx, worlds, RegionField::kWord16);
                 });
}

}  // namespace

S3RE_TEST(CGameData_MergeTileWord16Regions_MatchesOriginal) {
    CompareWord16Merge(ctx, &CGameData_MergeTileWord16Regions, 3000);
}
S3RE_TEST(CGameData_MergeTileWord16Regions_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareWord16Merge(probe, &WrongMergeTileWord16Regions, 500);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_MergeTileByte10Regions_MatchesOriginal) {
    CompareMerge(ctx, Original_CGameData_MergeTileByte10Regions, &CGameData_MergeTileByte10Regions, 3000,
                 [&](test::WorldPair& worlds, std::uint32_t size) {
                     for (std::uint32_t tx = 0; tx < size; ++tx) {
                         for (std::uint32_t ty = 0; ty < size; ++ty) {
                             const std::uint32_t id = ctx.rng().Below(4);
                             worlds.Both([&](CGameData* w) { SetRegionId(w, tx, ty, RegionField::kByte10, id); });
                             const std::uint8_t flags = static_cast<std::uint8_t>(ctx.rng().Next32());
                             const std::uint8_t stored =
                                 ctx.rng().Chance(1, 3) ? flags : static_cast<std::uint8_t>(flags & ~kTileBlocked);
                             worlds.Both([&](CGameData* w) { TileAt(w, tx, ty).flags = stored; });
                         }
                     }
                     SetUpIds(ctx, worlds, RegionField::kByte10);
                 });
}

S3RE_TEST(CGameData_FloodMoveArea_MatchesOriginal) {
    test::WorldPair worlds(ctx, kAreaTestWorld);
    for (int i = 0; i < 300; ++i) {
        const std::uint32_t size = RandomMapSize(ctx);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        const std::uint32_t x = ctx.rng().Below(size);
        const std::uint32_t y = ctx.rng().Below(size);
        // Areas 1..3 in a region around the start; the start in area `old`.
        const std::uint32_t old = 1 + ctx.rng().Below(3);
        const Region region = RandomRegion(ctx, size, x, y);
        ForRegion(region, [&](std::uint32_t tx, std::uint32_t ty) {
            const bool start = tx == x && ty == y;
            const auto area = static_cast<std::uint16_t>(start || ctx.rng().Chance(2, 3) ? old : 1 + ctx.rng().Below(3));
            worlds.Both([&](CGameData* w) { TileAt(w, tx, ty).area = area; });
        });
        std::uint32_t newArea = 1 + ctx.rng().Below(kTestAreas - 1);
        if (newArea == old) {
            newArea = old + 4;  // the same area would loop for ever
        }
        const std::uint32_t expected = Original_CGameData_FloodMoveArea(worlds.original(), nullptr, x, y, newArea);
        const std::uint32_t actual = CGameData_FloodMoveArea(worlds.ours(), nullptr, x, y, newArea);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%u, %u) -> area %u, map %u", i, x, y, newArea, size);
            return;
        }
    }
}

namespace {

// Wrong: ignores the owner (queues marked tiles of every player).
std::uint32_t __fastcall WrongFloodAssignMarkedTiles(CGameData* world, void*, std::uint32_t x, std::uint32_t y,
                                                     std::uint32_t area, std::uint32_t /*player*/) {
    StartRing(world, x, y);
    RunQueue(world, [&](std::uint32_t px, std::uint32_t py) {
        CGameData_RemoveTileFromArea(world, nullptr, px, py);
        CGameData_AssignTileToArea(world, nullptr, px, py, area);
        ForEachNeighbour(world, px, py, [&](std::uint32_t nx, std::uint32_t ny) {
            CGameData_QueueMarkedTile(world, nullptr, nx, ny, TileAt(world, nx, ny).owner);
        });
    });
    return world->tile_ring_index.read;
}

void CompareFloodAssign(test::Context& ctx, FloodAssignFn candidate, int cases) {
    test::WorldPair worlds(ctx, kAreaTestWorld);
    // No tile is marked at first; region tiles get valid area words, so
    // every tile the flood can reach has its area record in the world.
    for (std::uint32_t tx = 0; tx < 384; ++tx) {
        for (std::uint32_t ty = 0; ty < 384; ++ty) {
            const std::uint8_t flags = TileAt(worlds.original(), tx, ty).flags;
            worlds.Both([&](CGameData* w) { TileAt(w, tx, ty).flags = static_cast<std::uint8_t>(flags & ~0x10); });
        }
    }
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = RandomMapSize(ctx);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        const std::uint32_t x = ctx.rng().Below(size);
        const std::uint32_t y = ctx.rng().Below(size);
        const std::uint32_t player = ctx.rng().Below(3);
        const Region region = RandomRegion(ctx, size, x, y);
        const std::uint32_t density = 1 + ctx.rng().Below(4);  // of 4
        ForRegion(region, [&](std::uint32_t tx, std::uint32_t ty) {
            std::uint8_t flags = static_cast<std::uint8_t>(ctx.rng().Next32());
            flags = static_cast<std::uint8_t>(ctx.rng().Chance(density, 4) ? (flags | 0x10) & ~kTileBlocked : flags);
            const auto owner = static_cast<std::uint8_t>(ctx.rng().Chance(3, 4) ? player : ctx.rng().Below(3));
            const auto area = static_cast<std::uint16_t>(ctx.rng().Below(4));
            worlds.Both([&](CGameData* w) {
                layout::Tile& tile = TileAt(w, tx, ty);
                tile.flags = flags;
                tile.owner = owner;
                tile.area = area;
            });
        });
        const std::uint32_t area = 1 + ctx.rng().Below(kTestAreas - 1);
        const std::uint32_t expected =
            Original_CGameData_FloodAssignMarkedTiles(worlds.original(), nullptr, x, y, area, player);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, x, y, area, player);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%u, %u) -> area %u, player %u, map %u", i, x, y, area, player,
                     size);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(CGameData_FloodAssignMarkedTiles_MatchesOriginal) {
    CompareFloodAssign(ctx, &CGameData_FloodAssignMarkedTiles, 300);
}
S3RE_TEST(CGameData_FloodAssignMarkedTiles_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareFloodAssign(probe, &WrongFloodAssignMarkedTiles, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

std::uint32_t RelativeToWorld(std::uint32_t value, CGameData* world) {
    return value - static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));
}

// Wrong: ignores the terrain check.
std::uint32_t __fastcall WrongAssignWord16RegionsAround(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    for (std::uint32_t k = 0; k < 19; ++k) {
        const std::uint32_t size = MapSize(world);
        const std::uint32_t nx = OffsetNeighbourX(world, x, k);
        const std::uint32_t ny = OffsetNeighbourY(world, y, k);
        if (nx >= size || ny >= size || TileAt(world, nx, ny).wide_region != 0) {
            continue;
        }
        bool clear = true;
        for (std::uint32_t j = 0; j < 19; ++j) {
            const std::uint32_t mx = OffsetNeighbourX(world, nx, j);
            const std::uint32_t my = OffsetNeighbourY(world, ny, j);
            if (mx < size && my < size && (TileAt(world, mx, my).flags & kTileBlocked) != 0) {
                clear = false;
            }
        }
        if (clear) {
            CGameData_MergeTileWord16Regions(world, nullptr, nx, ny);
        }
    }
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&world->offsets[19].dy));
}

void CompareAssignRegions(test::Context& ctx, MergeFn candidate, int cases) {
    test::WorldPair worlds(ctx, kMergeTestWorld);
    SetUpNeighbourOffsets(worlds);
    // Entries 7..18: the second ring (any order).
    static constexpr std::int32_t kRing2[12][2] = {{2, 0}, {2, 1}, {2, 2}, {1, 2}, {0, 2}, {-1, 1},
                                                   {-2, 0}, {-2, -1}, {-2, -2}, {-1, -2}, {0, -2}, {1, -1}};
    for (std::uint32_t k = 0; k < 12; ++k) {
        SetOffset(worlds, 7 + k, kRing2[k][0], kRing2[k][1]);
    }
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = 8 + ctx.rng().Below(20);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        // At least 3 tiles from the edge: the merge looks one tile beyond
        // the tiles it fills and would flood off the map (real maps have a
        // border of tiles that never qualify).
        const std::uint32_t x = 3 + ctx.rng().Below(size - 6);
        const std::uint32_t y = 3 + ctx.rng().Below(size - 6);
        const std::uint32_t blocked = ctx.rng().Below(40);
        for (std::uint32_t tx = 0; tx < size + 3; ++tx) {
            for (std::uint32_t ty = 0; ty < size + 3; ++ty) {
                const auto region = static_cast<std::uint16_t>(ctx.rng().Chance(2, 3) ? 0 : ctx.rng().Below(4));
                const std::uint8_t flags = static_cast<std::uint8_t>(ctx.rng().Next32());
                const auto stored = static_cast<std::uint8_t>(ctx.rng().Below(1000) < blocked ? flags | 1 : flags & 0xfe);
                const auto terrain = static_cast<std::uint8_t>(ctx.rng().Below(1000) < blocked ? 0x21 : 0x10);
                worlds.Both([&](CGameData* w) {
                    layout::Tile& tile = TileAt(w, tx, ty);
                    tile.wide_region = region;
                    tile.flags = stored;
                    tile.terrain = terrain;
                });
            }
        }
        SetUpIds(ctx, worlds, RegionField::kWord16);
        const std::uint32_t expected =
            RelativeToWorld(Original_CGameData_AssignWord16RegionsAround(worlds.original(), nullptr, x, y), worlds.original());
        const std::uint32_t actual = RelativeToWorld(candidate(worlds.ours(), nullptr, x, y), worlds.ours());
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 32 == 31 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 31 before it): (%u, %u) map %u", i, x, y, size);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(CGameData_AssignWord16RegionsAround_MatchesOriginal) {
    CompareAssignRegions(ctx, &CGameData_AssignWord16RegionsAround, 3000);
}
S3RE_TEST(CGameData_AssignWord16RegionsAround_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareAssignRegions(probe, &WrongAssignWord16RegionsAround, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

// Wrong: never splits (a region stays one even when cut in two).
void __fastcall WrongSplitTileWord16Region(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t old = TileAt(world, x, y).wide_region;
    TileAt(world, x, y).wide_region = 0;
    std::uint32_t same = 0;
    for (std::uint32_t k = kFirstNeighbourEntry; k <= kLastNeighbourEntry; ++k) {
        if (TileAt(world, OffsetNeighbourX(world, x, k), OffsetNeighbourY(world, y, k)).wide_region == old) {
            ++same;
        }
    }
    if (same == 0) {
        world->wide_region_used[old] = 0;
    }
}

// A small map of region ids (`field`): ids 1..3 and 0 at
// random, so that regions touch the tile in many ways; random flags (bit
// 0x10 often set, to show which tiles the walk clears). The frame of tiles
// just off the map has no region (as on real maps, whose border tiles never
// get one; a neighbour off the map in a region would be flooded from x or
// y = 0xffff). The ids table marks 1..3 (and more) in use, so a new id
// never equals an old one.
void SetUpSplit(test::Context& ctx, test::WorldPair& worlds, std::uint32_t size, RegionField field) {
    const std::uint32_t empty = ctx.rng().Below(5);  // in 8: share of tiles without a region
    const std::uint32_t kinds = 1 + ctx.rng().Below(3);
    for (std::uint32_t tx = 0xffffffffu; tx != size + 1; ++tx) {
        for (std::uint32_t ty = 0xffffffffu; ty != size + 1; ++ty) {
            const bool onMap = tx < size && ty < size;
            const std::uint32_t value = !onMap || ctx.rng().Below(8) < empty ? 0 : 1 + ctx.rng().Below(kinds);
            const auto flags = static_cast<std::uint8_t>(ctx.rng().Next32());
            worlds.Both([&](CGameData* w) {
                SetRegionId(w, tx, ty, field, value);  // tx, ty may be -1 (32-bit wrap)
                TileAt(w, tx, ty).flags = flags;
            });
        }
    }
    const std::uint32_t used = 4 + ctx.rng().Below(40);
    for (std::uint32_t id = 0; id < used + 2 && id < RegionIdsSize(field); ++id) {
        worlds.Both([&](CGameData* w) { RegionIdsUsed(w, field)[id] = static_cast<std::uint8_t>(id < used ? 1 : 0); });
    }
    const std::uint32_t mark = ctx.rng().Below(used + 2);
    worlds.Both([&](CGameData* w) { RegionIdsHighWater(w, field) = mark; });
}

// Like the game's regions: random walkable tiles, each connected stretch
// (six neighbours) with its own id 1, 2, ..., none for the rest and the
// frame off the map; ids in use marked in the table. (With ids shared by
// separate stretches, a freed id could be handed out again while still in
// use, and the flood fill would then loop for ever, in the original too.)
void SetUpLabelledRegions(test::Context& ctx, test::WorldPair& worlds, std::uint32_t size, RegionField field) {
    const std::uint32_t tableSize = RegionIdsSize(field);
    static constexpr std::int32_t kSteps[6][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 0}, {-1, -1}, {0, -1}};
    const std::uint32_t density = 3 + ctx.rng().Below(5);  // of 8
    std::vector<std::uint8_t> walkable(size * size);
    for (auto& w : walkable) {
        w = ctx.rng().Below(8) < density ? 1 : 0;
    }
    std::vector<std::uint32_t> label(size * size, 0);
    std::uint32_t next = 1;
    std::vector<std::uint32_t> queue;
    for (std::uint32_t start = 0; start < size * size; ++start) {
        if (!walkable[start] || label[start] != 0 || next + 1 >= tableSize) {
            continue;
        }
        label[start] = next;
        queue.assign(1, start);
        while (!queue.empty()) {
            const std::uint32_t t = queue.back();
            queue.pop_back();
            for (const auto& step : kSteps) {
                const std::int32_t nx = static_cast<std::int32_t>(t / size) + step[0];
                const std::int32_t ny = static_cast<std::int32_t>(t % size) + step[1];
                if (nx < 0 || ny < 0 || nx >= static_cast<std::int32_t>(size) || ny >= static_cast<std::int32_t>(size)) {
                    continue;
                }
                const std::uint32_t n = static_cast<std::uint32_t>(nx) * size + static_cast<std::uint32_t>(ny);
                if (walkable[n] && label[n] == 0) {
                    label[n] = next;
                    queue.push_back(n);
                }
            }
        }
        ++next;
    }
    for (std::uint32_t tx = 0xffffffffu; tx != size + 1; ++tx) {
        for (std::uint32_t ty = 0xffffffffu; ty != size + 1; ++ty) {
            const bool onMap = tx < size && ty < size;
            const std::uint32_t value = onMap ? label[tx * size + ty] : 0;
            const auto flags = static_cast<std::uint8_t>(ctx.rng().Next32());
            worlds.Both([&](CGameData* w) {
                SetRegionId(w, tx, ty, field, value);  // tx, ty may be -1 (32-bit wrap)
                TileAt(w, tx, ty).flags = flags;
            });
        }
    }
    for (std::uint32_t id = 0; id < next + 2 && id < tableSize; ++id) {
        worlds.Both([&](CGameData* w) {
            RegionIdsUsed(w, field)[id] = static_cast<std::uint8_t>(id != 0 && id < next ? 1 : 0);
        });
    }
    const std::uint32_t mark = ctx.rng().Below(next + 2);
    worlds.Both([&](CGameData* w) { RegionIdsHighWater(w, field) = mark; });
}

// What to do when the tile itself has no region id (`field`): give it
// one (random regions), skip the case (labelled regions, where an id given
// to it would be shared), or call anyway (SplitWord16RegionsAround).
enum class Unlabelled { kGiveId, kSkip, kCall };

template <typename Fn, typename Setup>
void CompareSplit(test::Context& ctx, Fn original, Fn candidate, int cases, bool edges, Setup setup,
                  Unlabelled unlabelled = Unlabelled::kGiveId, RegionField field = RegionField::kWord16) {
    test::WorldPair worlds(ctx, kMergeTestWorld);
    SetUpNeighbourOffsets(worlds);
    static constexpr std::int32_t kRing2[12][2] = {{2, 0}, {2, 1}, {2, 2}, {1, 2}, {0, 2}, {-1, 1},
                                                   {-2, 0}, {-2, -1}, {-2, -2}, {-1, -2}, {0, -2}, {1, -1}};
    for (std::uint32_t k = 0; k < 12; ++k) {
        SetOffset(worlds, 7 + k, kRing2[k][0], kRing2[k][1]);
    }
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = 6 + ctx.rng().Below(22);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        // Mostly inside; with `edges` sometimes on the edge of the map.
        const bool edge = edges && ctx.rng().Chance(1, 6);
        const std::uint32_t x = edge ? (ctx.rng().Chance(1, 2) ? 0 : size - 1) : 1 + ctx.rng().Below(size - 2);
        const std::uint32_t y = edge && ctx.rng().Chance(1, 2) ? (ctx.rng().Chance(1, 2) ? 0 : size - 1) : 1 + ctx.rng().Below(size - 2);
        setup(worlds, size);
        // The tile itself is in a region (its callers only call it then).
        const bool none = field == RegionField::kWord16 ? TileAt(worlds.original(), x, y).wide_region == 0
                                               : TileAt(worlds.original(), x, y).walk_region == 0;
        if (none && unlabelled == Unlabelled::kSkip) {
            continue;
        }
        if (unlabelled == Unlabelled::kGiveId) {
            if (TileAt(worlds.original(), x, y).wide_region == 0) {
                worlds.Both([&](CGameData* w) { TileAt(w, x, y).wide_region = 1; });
            }
            if (TileAt(worlds.original(), x, y).walk_region == 0) {
                worlds.Both([&](CGameData* w) { TileAt(w, x, y).walk_region = 1; });
            }
        }
        const std::uint32_t expected = original(worlds.original(), nullptr, x, y);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, x, y);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 32 == 31 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 31 before it): (%u, %u) map %u", i, x, y, size);
            return;
        }
    }
}

// The split functions return nothing; compare them through a wrapper that returns 0.
template <SplitFn* Fn>
std::uint32_t __fastcall ReturnZero(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y) {
    (*Fn)(world, edx, x, y);
    return 0;
}

SplitFn g_ourSplitWord16 = &CGameData_SplitTileWord16Region;
SplitFn g_ourSplitByte10 = &CGameData_SplitTileByte10Region;
SplitFn g_wrongSplitWord16 = &WrongSplitTileWord16Region;

void CompareWord16Split(test::Context& ctx, MergeFn candidate, int cases) {
    CompareSplit(ctx, &ReturnZero<&Original_CGameData_SplitTileWord16Region>, candidate, cases, true,
                 [&](test::WorldPair& worlds, std::uint32_t size) {
                     SetUpSplit(ctx, worlds, size, RegionField::kWord16);
                 });
}

}  // namespace

S3RE_TEST(CGameData_SplitTileWord16Region_MatchesOriginal) {
    CompareWord16Split(ctx, &ReturnZero<&g_ourSplitWord16>, 5000);
}
S3RE_TEST(CGameData_SplitTileWord16Region_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareWord16Split(probe, &ReturnZero<&g_wrongSplitWord16>, 1000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_SplitTileByte10Region_MatchesOriginal) {
    CompareSplit(ctx, &ReturnZero<&Original_CGameData_SplitTileByte10Region>, &ReturnZero<&g_ourSplitByte10>, 5000, true,
                 [&](test::WorldPair& worlds, std::uint32_t size) {
                     SetUpSplit(ctx, worlds, size, RegionField::kByte10);
                 });
}
S3RE_TEST(CGameData_SplitTileWord16Region_LabelledRegionsMatchOriginal) {
    CompareSplit(ctx, &ReturnZero<&Original_CGameData_SplitTileWord16Region>, &ReturnZero<&g_ourSplitWord16>, 3000,
                 true, [&](test::WorldPair& worlds, std::uint32_t size) {
                     SetUpLabelledRegions(ctx, worlds, size, RegionField::kWord16);
                 }, Unlabelled::kSkip, RegionField::kWord16);
}
S3RE_TEST(CGameData_SplitTileByte10Region_LabelledRegionsMatchOriginal) {
    CompareSplit(ctx, &ReturnZero<&Original_CGameData_SplitTileByte10Region>, &ReturnZero<&g_ourSplitByte10>, 3000,
                 true, [&](test::WorldPair& worlds, std::uint32_t size) {
                     SetUpLabelledRegions(ctx, worlds, size, RegionField::kByte10);
                 }, Unlabelled::kSkip, RegionField::kByte10);
}
S3RE_TEST(CGameData_SplitWord16RegionsAround_MatchesOriginal) {
    // Labelled regions: it splits several tiles in a row, and a region id
    // freed by one split must not be in use elsewhere.
    CompareSplit(ctx, Original_CGameData_SplitWord16RegionsAround, &CGameData_SplitWord16RegionsAround, 2000, false,
                 [&](test::WorldPair& worlds, std::uint32_t size) {
                     SetUpLabelledRegions(ctx, worlds, size, RegionField::kWord16);
                 }, Unlabelled::kCall);
}

}  // namespace s3re::game
