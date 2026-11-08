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
// GameWorld::FloodFillTileWord16 (0x0051c6c0): (x, y, value). The uint16 at
//   tile +0x16 of the start tile is `old`; the start tile is set to `value`
//   (low word) and every connected tile whose word equals `old` too (step
//   GameWorld::ReplaceTileWord16AndQueue, which compares the zero-extended
//   word with `old` and stores the low word of `value`). Loops for ever if
//   the low word of `value` equals `old`, as the original does. x and y are
//   used at full 32 bits for the start tile, and stored as uint16 in the
//   ring. EAX = the final read index (callers 0x51c545 tail-return it).
//   ret 0xc.
// GameWorld::FloodFillTileByte10 (0x0051c8a0): the same for the byte at
//   tile +0x10 (step GameWorld::ReplaceTileByte10AndQueue). ret 0xc.
// GameWorld::FloodMarkAreaTerrain5To7 (0x0051cee0): (x, y, value). Marks
//   the start tile and every connected tile flagged 0x10 (tile +0xa) whose
//   terrain (+0x6) is 5..7 and flag 0x80 clear with area word value +
//   0x8000 (GameWorld::SetTileAreaMarked); the step
//   GameWorld::QueueMarkedTileTerrain5To7 clears the 0x10 flag of every tile
//   it queues, so each tile is queued once. A do-while: the start tile is
//   processed without a check. EAX = write index (callers ignore it).
//   ret 0xc.
// GameWorld::FloodSetByte10MarkedTiles (0x0051d450): (x, y, value). Like
//   FloodMarkAreaTerrain5To7, but sets the tile byte +0x10 to `value` (low
//   byte) and spreads over tiles flagged 0x10 without flag 0x01
//   (GameWorld::QueueMarkedTileNoOwner). EAX = write index (ignored).
//   ret 0xc.
// GameWorld::MoveTileToAreaAndQueue (0x004e6430): (x, y, oldArea, newArea):
//   if the tile's area word (tile +0x14) equals oldArea (zero-extended,
//   32-bit compare): GameWorld::RemoveTileFromArea(x, y),
//   GameWorld::AssignTileToArea(x, y, newArea), and push (x, y) at the
//   write index. A step of the area flood fills (callers 0x4e6150 and
//   others). EAX (ignored by callers) reproduced: oldArea if the area
//   differed, else the incremented write index before it wraps. ret 0x10.
// GameWorld::FloodMoveArea (0x004e62c0): (x, y, newArea): the start tile's
//   area word is `old`; puts the start tile in the ring, moves it to newArea
//   (RemoveTileFromArea, AssignTileToArea) and floods with the step
//   GameWorld::MoveTileToAreaAndQueue(neighbour, old, newArea). Loops for
//   ever if newArea == old. EAX = the final read index (ignored). ret 0xc.
// GameWorld::FloodAssignMarkedTiles (0x004e6cd0): (x, y, area, player):
//   puts the start tile in the ring; each tile taken from the ring leaves
//   its area (RemoveTileFromArea), joins `area` (AssignTileToArea) and
//   queues its neighbours with GameWorld::QueueMarkedTile(player) (marked
//   0x10, not blocked, owned by the player; clears the mark). The start tile
//   is taken without these checks. EAX = the final read index. Caller
//   0x51a580 (area set-up, with a new AllocArea). ret 0x10.
// GameWorld::MergeTileWord16Regions (0x0051c5e0): (x, y): looks at the
//   neighbours given by the offset table entries 1..6 (world+0x110a598):
//   the first with a non-zero word +0x16 gives the tile its word; every
//   later neighbour with another non-zero word has that id freed (byte 0 in
//   the 8192-id table at world+0xe125e8) and its region flooded with the
//   first word (FloodFillTileWord16). With no such neighbour the tile gets a
//   new id (GameWorld::FindFreeId8192, marked 1 in the table). EAX = 0, or
//   the new id. ret 8.
// GameWorld::MergeTileByte10Regions (0x0051ca00): the same for the byte
//   +0x10, over the neighbours that are not blocked (flag bit 0 of +0xa,
//   any value counts), with the 256-id table at world+0xe114dc
//   (FindFreeId256, FloodFillTileByte10). EAX = the number of unblocked
//   neighbours, or the new id. ret 8.
// GameWorld::AssignWord16RegionsAround (0x0051c0f0): (x, y): for the
//   offset table entries 0..18 (the tile and two rings), a tile on the map
//   (unsigned compares) without a region id (word +0x16 == 0) whose own 19
//   offset tiles on the map are all unblocked (flag bit 0 of +0xa) and not
//   of terrain kind 0x20 (terrain & 0xf0) gets a region through
//   GameWorld::MergeTileWord16Regions. EAX (one caller tail-returns it) =
//   world + 0x110a6bc (the loop's table pointer). ret 8.
// GameWorld::SplitTileWord16Region (0x0051c1f0): (x, y): the tile leaves
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
// GameWorld::SplitTileByte10Region (0x0051caf0): the same for the byte
//   +0x10 with the 256-id table at world+0xe114dc (FindFreeId256,
//   FloodFillTileByte10). ret 8.
// GameWorld::SplitWord16RegionsAround (0x0051c080): (x, y): for the offset
//   table entries 0..18 (the tile and two rings), each tile on the map
//   (unsigned compares, map size re-read) with a region id (word +0x16 !=
//   0) gets GameWorld::SplitTileWord16Region. EAX = 0 (the loop counter).
//   ret 8.
// All thiscall (ECX = world).

#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uint32_t kTiles = 0x67444;
constexpr std::uint32_t kTileTerrain = 0x6;
constexpr std::uint32_t kTileFlags = 0xa;
constexpr std::uint32_t kTileByte10 = 0x10;
constexpr std::uint32_t kTileArea = 0x14;
constexpr std::uint32_t kTileWord16 = 0x16;
constexpr std::uint32_t kTileRing = 0xdfdc48;
constexpr std::uint32_t kTileRingRead = 0xe07888;
constexpr std::uint32_t kTileRingWrite = 0xe0788c;
constexpr std::uint32_t kTileRingEntries = 10000;
constexpr std::uint8_t kTileBlocked = 0x01;  // in kTileFlags
constexpr std::uint32_t kOffsetTable = 0x110a588;  // int32 dx, int32 dy, 16-byte stride
constexpr std::uint32_t kOffsetTableBytes = 0x10;
constexpr std::uint32_t kIds256 = 0xe114dc;   // uint8 per id, 1 = in use
constexpr std::uint32_t kIds8192 = 0xe125e8;  // uint8 per id, 1 = in use

template <typename T>
T& At(GameWorld* world, std::uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<std::uint8_t*>(world) + offset);
}

std::uint32_t Tile(std::uint32_t x, std::uint32_t y) {
    return kTiles + (x * 0x300 + y) * 24;
}

std::uint32_t MapSize(GameWorld* world) {
    return At<std::uint32_t>(world, world_offset::kMapSize);
}

// Ring slot 0 = (x, y), read = 0, write = 1.
void StartRing(GameWorld* world, std::uint32_t x, std::uint32_t y) {
    At<std::uint16_t>(world, kTileRing) = static_cast<std::uint16_t>(x);
    At<std::uint16_t>(world, kTileRing + 2) = static_cast<std::uint16_t>(y);
    At<std::uint32_t>(world, kTileRingRead) = 0;
    At<std::uint32_t>(world, kTileRingWrite) = 1;
}

void AdvanceRead(GameWorld* world) {
    std::uint32_t& read = At<std::uint32_t>(world, kTileRingRead);
    read = read + 1 == kTileRingEntries ? 0 : read + 1;
}

// Calls step(nx, ny) for the six neighbours of (x, y), in the original's order.
template <typename Step>
void ForEachNeighbour(GameWorld* world, std::uint32_t x, std::uint32_t y, Step step) {
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
void RunQueue(GameWorld* world, Visit visit) {
    do {
        const std::uint32_t read = At<std::uint32_t>(world, kTileRingRead);
        const std::uint32_t x = At<std::uint16_t>(world, kTileRing + read * 4);
        const std::uint32_t y = At<std::uint16_t>(world, kTileRing + read * 4 + 2);
        visit(x, y);
        AdvanceRead(world);
    } while (At<std::uint32_t>(world, kTileRingRead) != At<std::uint32_t>(world, kTileRingWrite));
}

}  // namespace

using FloodFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x,
                                           std::uint32_t y, std::uint32_t value);
using MoveTileFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x,
                                              std::uint32_t y, std::uint32_t oldArea,
                                              std::uint32_t newArea);

// Purpose: Renumbers a whole connected region: every tile reachable from the start with the same region
// number gets the new one. Used when two regions merge.
std::uint32_t __fastcall GameWorld_FloodFillTileWord16(GameWorld* world, void*, std::uint32_t x,
                                                       std::uint32_t y, std::uint32_t value) {
    std::uint16_t& start = At<std::uint16_t>(world, Tile(x, y) + kTileWord16);
    const std::uint32_t old = start;
    StartRing(world, x, y);
    start = static_cast<std::uint16_t>(value);
    // The original checks read != write before the first round (always true).
    RunQueue(world, [&](std::uint32_t px, std::uint32_t py) {
        ForEachNeighbour(world, px, py, [&](std::uint32_t nx, std::uint32_t ny) {
            GameWorld_ReplaceTileWord16AndQueue(world, nullptr, nx, ny, old, value);
        });
    });
    return At<std::uint32_t>(world, kTileRingRead);
}

// Purpose: Like FloodFillTileWord16, for the other kind of region number.
std::uint32_t __fastcall GameWorld_FloodFillTileByte10(GameWorld* world, void*, std::uint32_t x,
                                                       std::uint32_t y, std::uint32_t value) {
    std::uint8_t& start = At<std::uint8_t>(world, Tile(x, y) + kTileByte10);
    const std::uint32_t old = start;
    StartRing(world, x, y);
    start = static_cast<std::uint8_t>(value);
    RunQueue(world, [&](std::uint32_t px, std::uint32_t py) {
        ForEachNeighbour(world, px, py, [&](std::uint32_t nx, std::uint32_t ny) {
            GameWorld_ReplaceTileByte10AndQueue(world, nullptr, nx, ny, old, value);
        });
    });
    return At<std::uint32_t>(world, kTileRingRead);
}

// Purpose: Gives one area number to a whole connected stretch of marked tiles of terrain types 5 to 7. Part
// of recomputing the areas.
std::uint32_t __fastcall GameWorld_FloodMarkAreaTerrain5To7(GameWorld* world, void*, std::uint32_t x,
                                                            std::uint32_t y, std::uint32_t value) {
    StartRing(world, x, y);
    RunQueue(world, [&](std::uint32_t px, std::uint32_t py) {
        GameWorld_SetTileAreaMarked(world, nullptr, px, py, value);
        ForEachNeighbour(world, px, py, [&](std::uint32_t nx, std::uint32_t ny) {
            GameWorld_QueueMarkedTileTerrain5To7(world, nullptr, nx, ny);
        });
    });
    return At<std::uint32_t>(world, kTileRingWrite);
}

// Purpose: Gives one region number to a whole connected stretch of marked tiles. Part of recomputing the
// regions.
std::uint32_t __fastcall GameWorld_FloodSetByte10MarkedTiles(GameWorld* world, void*, std::uint32_t x,
                                                             std::uint32_t y, std::uint32_t value) {
    StartRing(world, x, y);
    RunQueue(world, [&](std::uint32_t px, std::uint32_t py) {
        At<std::uint8_t>(world, Tile(px, py) + kTileByte10) = static_cast<std::uint8_t>(value);
        ForEachNeighbour(world, px, py, [&](std::uint32_t nx, std::uint32_t ny) {
            GameWorld_QueueMarkedTileNoOwner(world, nullptr, nx, ny);
        });
    });
    return At<std::uint32_t>(world, kTileRingWrite);
}

// Purpose: One step of moving a whole area to a new number: if a tile belongs to the old area, move it and
// remember it so its neighbours get checked next.
std::uint32_t __fastcall GameWorld_MoveTileToAreaAndQueue(GameWorld* world, void*, std::uint32_t x,
                                                          std::uint32_t y, std::uint32_t oldArea,
                                                          std::uint32_t newArea) {
    // The area word, addressed as the original does: ((x*0x300 + y + 0x44d9) * 24).
    const std::uint32_t area = At<std::uint16_t>(world, Tile(x, y) + kTileArea);
    if (area != oldArea) {
        return oldArea;
    }
    GameWorld_RemoveTileFromArea(world, nullptr, x, y);
    GameWorld_AssignTileToArea(world, nullptr, x, y, newArea);
    std::uint32_t& write = At<std::uint32_t>(world, kTileRingWrite);
    At<std::uint16_t>(world, kTileRing + write * 4) = static_cast<std::uint16_t>(x);
    At<std::uint16_t>(world, kTileRing + write * 4 + 2) = static_cast<std::uint16_t>(y);
    const std::uint32_t next = write + 1;
    write = next == kTileRingEntries ? 0 : next;
    return next;
}

S3RE_HOOK(0x0051c6c0, GameWorld_FloodFillTileWord16, FloodFn, Original_GameWorld_FloodFillTileWord16,
          GameWorld_FloodFillTileWord16);
S3RE_HOOK(0x0051c8a0, GameWorld_FloodFillTileByte10, FloodFn, Original_GameWorld_FloodFillTileByte10,
          GameWorld_FloodFillTileByte10);
S3RE_HOOK(0x0051cee0, GameWorld_FloodMarkAreaTerrain5To7, FloodFn,
          Original_GameWorld_FloodMarkAreaTerrain5To7, GameWorld_FloodMarkAreaTerrain5To7);
S3RE_HOOK(0x0051d450, GameWorld_FloodSetByte10MarkedTiles, FloodFn,
          Original_GameWorld_FloodSetByte10MarkedTiles, GameWorld_FloodSetByte10MarkedTiles);
S3RE_HOOK(0x004e6430, GameWorld_MoveTileToAreaAndQueue, MoveTileFn,
          Original_GameWorld_MoveTileToAreaAndQueue, GameWorld_MoveTileToAreaAndQueue);

using MergeFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);

// Purpose: Moves all tiles connected to a starting tile from one area to another, spreading outwards like
// paint. Used when areas are joined or split, e.g. after territory changes.
std::uint32_t __fastcall GameWorld_FloodMoveArea(GameWorld* world, void*, std::uint32_t x, std::uint32_t y,
                                                 std::uint32_t newArea) {
    const std::uint32_t old = At<std::uint16_t>(world, Tile(x, y) + kTileArea);
    StartRing(world, x, y);
    GameWorld_RemoveTileFromArea(world, nullptr, x, y);
    GameWorld_AssignTileToArea(world, nullptr, x, y, newArea);
    RunQueue(world, [&](std::uint32_t px, std::uint32_t py) {
        ForEachNeighbour(world, px, py, [&](std::uint32_t nx, std::uint32_t ny) {
            GameWorld_MoveTileToAreaAndQueue(world, nullptr, nx, ny, old, newArea);
        });
    });
    return At<std::uint32_t>(world, kTileRingRead);
}

// Purpose: Puts a connected patch of marked tiles of one player into an area, spreading outwards from a
// start tile. Used when the map's areas are first set up (probably at game start).
std::uint32_t __fastcall GameWorld_FloodAssignMarkedTiles(GameWorld* world, void*, std::uint32_t x, std::uint32_t y,
                                                          std::uint32_t area, std::uint32_t player) {
    StartRing(world, x, y);
    RunQueue(world, [&](std::uint32_t px, std::uint32_t py) {
        GameWorld_RemoveTileFromArea(world, nullptr, px, py);
        GameWorld_AssignTileToArea(world, nullptr, px, py, area);
        ForEachNeighbour(world, px, py, [&](std::uint32_t nx, std::uint32_t ny) {
            GameWorld_QueueMarkedTile(world, nullptr, nx, ny, player);
        });
    });
    return At<std::uint32_t>(world, kTileRingRead);
}

namespace {

// The neighbour from offset table entry k (1..6) of (x, y).
std::uint32_t OffsetNeighbourX(GameWorld* world, std::uint32_t x, std::uint32_t k) {
    return x + At<std::uint32_t>(world, kOffsetTable + k * kOffsetTableBytes);
}
std::uint32_t OffsetNeighbourY(GameWorld* world, std::uint32_t y, std::uint32_t k) {
    return y + At<std::uint32_t>(world, kOffsetTable + k * kOffsetTableBytes + 4);
}

}  // namespace

// Purpose: When a tile becomes part of the walkable network (e.g. after something blocking it is removed),
// it joins the region of its neighbours; if it connects several regions they are merged into one;
// if it has no neighbour region, it starts a new one.
std::uint32_t __fastcall GameWorld_MergeTileWord16Regions(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    std::uint32_t count = 0;
    std::uint32_t first = 0;
    for (std::uint32_t k = 1; k <= 6; ++k) {
        const std::uint32_t nx = OffsetNeighbourX(world, x, k);
        const std::uint32_t ny = OffsetNeighbourY(world, y, k);
        const std::uint32_t id = At<std::uint16_t>(world, Tile(nx, ny) + kTileWord16);
        if (id == 0) {
            continue;
        }
        if (++count == 1) {
            first = id;
            At<std::uint16_t>(world, Tile(x, y) + kTileWord16) = static_cast<std::uint16_t>(id);
        } else if (id != first) {
            At<std::uint8_t>(world, kIds8192 + id) = 0;
            GameWorld_FloodFillTileWord16(world, nullptr, nx, ny, first);
        }
    }
    if (count != 0) {
        return 0;
    }
    const std::uint32_t id = GameWorld_FindFreeId8192(world, nullptr);
    At<std::uint8_t>(world, kIds8192 + id) = 1;
    At<std::uint16_t>(world, Tile(x, y) + kTileWord16) = static_cast<std::uint16_t>(id);
    return id;
}

// Purpose: Like MergeTileWord16Regions, for the other kind of region (over unblocked tiles).
std::uint32_t __fastcall GameWorld_MergeTileByte10Regions(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    std::uint32_t count = 0;
    std::uint32_t first = 0;
    for (std::uint32_t k = 1; k <= 6; ++k) {
        const std::uint32_t nx = OffsetNeighbourX(world, x, k);
        const std::uint32_t ny = OffsetNeighbourY(world, y, k);
        const std::uint32_t tile = Tile(nx, ny);
        if ((At<std::uint8_t>(world, tile + kTileFlags) & kTileBlocked) != 0) {
            continue;
        }
        const std::uint32_t id = At<std::uint8_t>(world, tile + kTileByte10);
        if (++count == 1) {
            first = id;
            At<std::uint8_t>(world, Tile(x, y) + kTileByte10) = static_cast<std::uint8_t>(id);
        } else if (id != first) {
            At<std::uint8_t>(world, kIds256 + id) = 0;
            GameWorld_FloodFillTileByte10(world, nullptr, nx, ny, first);
        }
    }
    if (count != 0) {
        return count;  // unlike MergeTileWord16Regions, which returns 0 here
    }
    const std::uint32_t id = GameWorld_FindFreeId256(world, nullptr);
    At<std::uint8_t>(world, kIds256 + id) = 1;
    At<std::uint8_t>(world, Tile(x, y) + kTileByte10) = static_cast<std::uint8_t>(id);
    return id;
}

S3RE_HOOK(0x004e62c0, GameWorld_FloodMoveArea, FloodFn, Original_GameWorld_FloodMoveArea, GameWorld_FloodMoveArea);
using FloodAssignFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                 std::uint32_t area, std::uint32_t player);
S3RE_HOOK(0x004e6cd0, GameWorld_FloodAssignMarkedTiles, FloodAssignFn, Original_GameWorld_FloodAssignMarkedTiles,
          GameWorld_FloodAssignMarkedTiles);
S3RE_HOOK(0x0051c5e0, GameWorld_MergeTileWord16Regions, MergeFn, Original_GameWorld_MergeTileWord16Regions,
          GameWorld_MergeTileWord16Regions);
S3RE_HOOK(0x0051ca00, GameWorld_MergeTileByte10Regions, MergeFn, Original_GameWorld_MergeTileByte10Regions,
          GameWorld_MergeTileByte10Regions);

// Purpose: After something changed near a tile, gives region numbers to the nearby tiles that should have
// one now (not blocked, not water-like terrain) but don't yet.
std::uint32_t __fastcall GameWorld_AssignWord16RegionsAround(GameWorld* world, void*, std::uint32_t x,
                                                              std::uint32_t y) {
    constexpr std::uint32_t kEntries = 19;
    for (std::uint32_t k = 0; k < kEntries; ++k) {
        const std::uint32_t size = MapSize(world);
        const std::uint32_t nx = OffsetNeighbourX(world, x, k);
        const std::uint32_t ny = OffsetNeighbourY(world, y, k);
        if (nx >= size || ny >= size || At<std::uint16_t>(world, Tile(nx, ny) + kTileWord16) != 0) {
            continue;
        }
        bool clear = true;
        for (std::uint32_t j = 0; j < kEntries; ++j) {
            const std::uint32_t mx = OffsetNeighbourX(world, nx, j);
            const std::uint32_t my = OffsetNeighbourY(world, ny, j);
            if (mx >= size || my >= size) {
                continue;
            }
            const std::uint32_t tile = Tile(mx, my);
            if ((At<std::uint8_t>(world, tile + kTileFlags) & kTileBlocked) != 0 ||
                (At<std::uint8_t>(world, tile + kTileTerrain) & 0xf0) == 0x20) {
                clear = false;
            }
        }
        if (clear) {
            GameWorld_MergeTileWord16Regions(world, nullptr, nx, ny);
        }
    }
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world)) + kOffsetTable + 4 +
           kEntries * kOffsetTableBytes;
}

S3RE_HOOK(0x0051c0f0, GameWorld_AssignWord16RegionsAround, MergeFn, Original_GameWorld_AssignWord16RegionsAround,
          GameWorld_AssignWord16RegionsAround);

namespace {

// The two kinds of region ids that SplitTileRegion works on.
struct Word16Regions {
    using Id = std::uint16_t;
    static constexpr std::uint32_t kField = kTileWord16;
    static constexpr std::uint32_t kIds = kIds8192;
    static std::uint32_t NewId(GameWorld* world) { return GameWorld_FindFreeId8192(world, nullptr); }
    static void Fill(GameWorld* world, std::uint32_t x, std::uint32_t y, std::uint32_t id) {
        GameWorld_FloodFillTileWord16(world, nullptr, x, y, id);
    }
};

struct Byte10Regions {
    using Id = std::uint8_t;
    static constexpr std::uint32_t kField = kTileByte10;
    static constexpr std::uint32_t kIds = kIds256;
    static std::uint32_t NewId(GameWorld* world) { return GameWorld_FindFreeId256(world, nullptr); }
    static void Fill(GameWorld* world, std::uint32_t x, std::uint32_t y, std::uint32_t id) {
        GameWorld_FloodFillTileByte10(world, nullptr, x, y, id);
    }
};

constexpr std::uint8_t kTileSplitMark = 0x10;  // in kTileFlags

template <typename Regions>
void SplitTileRegion(GameWorld* world, std::uint32_t x, std::uint32_t y) {
    auto id = [&](std::uint32_t tx, std::uint32_t ty) -> std::uint32_t {
        return At<typename Regions::Id>(world, Tile(tx, ty) + Regions::kField);
    };
    auto flags = [&](std::uint32_t tx, std::uint32_t ty) -> std::uint8_t& {
        return At<std::uint8_t>(world, Tile(tx, ty) + kTileFlags);
    };
    auto onMap = [&](std::uint32_t tx, std::uint32_t ty) { return tx < MapSize(world) && ty < MapSize(world); };

    const std::uint32_t old = id(x, y);
    At<typename Regions::Id>(world, Tile(x, y) + Regions::kField) = 0;
    std::uint32_t same = 0;
    for (std::uint32_t k = 1; k <= 6; ++k) {
        if (id(OffsetNeighbourX(world, x, k), OffsetNeighbourY(world, y, k)) == old) {
            ++same;
        }
    }
    if (same == 0) {
        At<std::uint8_t>(world, Regions::kIds + old) = 0;
        return;
    }
    if (same < 2 || same > 4) {
        return;
    }
    // Flag the neighbours in the region; remember the last one.
    std::uint32_t last = 0;
    for (std::uint32_t d = 0; d < 6; ++d) {
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
    std::uint32_t direction = last + 3 >= 6 ? last - 3 : last + 3;  // facing the tile
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
                direction = direction == 0 ? 5 : direction - 1;
                flags(nx, ny) = static_cast<std::uint8_t>(flags(nx, ny) & ~kTileSplitMark);
                break;
            }
            if (tries == 6 && !started) {
                split = true;
            }
            direction = direction == 5 ? 0 : direction + 1;
            if (tries == 6) {
                break;
            }
        }
        bool flagged = false;
        for (std::uint32_t d = 0; d < 6 && !flagged; ++d) {
            flagged = (flags(NeighbourX(x, d), NeighbourY(y, d)) & kTileSplitMark) != 0;
        }
        if (!flagged) {
            return;  // every neighbour was reached: still one region
        }
        if (split) {
            break;
        }
    }
    for (std::uint32_t d = 0; d < 6; ++d) {
        const std::uint32_t nx = NeighbourX(x, d);
        const std::uint32_t ny = NeighbourY(y, d);
        if (id(nx, ny) == old && (flags(nx, ny) & kTileSplitMark) != 0) {
            const std::uint32_t newId = Regions::NewId(world);
            At<std::uint8_t>(world, Regions::kIds + newId) = 1;
            Regions::Fill(world, nx, ny, newId);
        }
    }
}

}  // namespace

using SplitFn = void(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);

// Purpose: When a tile stops being part of the walkable network (e.g. something is built on it), its
// region may fall apart into pieces that can no longer reach each other; each cut-off piece gets a new
// region number.
void __fastcall GameWorld_SplitTileWord16Region(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    SplitTileRegion<Word16Regions>(world, x, y);
}

// Purpose: Like SplitTileWord16Region, for the other kind of region number.
void __fastcall GameWorld_SplitTileByte10Region(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    SplitTileRegion<Byte10Regions>(world, x, y);
}

// Purpose: After something changed near a tile, checks for every nearby tile with a region number whether
// its region has fallen apart.
std::uint32_t __fastcall GameWorld_SplitWord16RegionsAround(GameWorld* world, void*, std::uint32_t x,
                                                            std::uint32_t y) {
    for (std::uint32_t k = 0; k < 19; ++k) {
        const std::uint32_t nx = OffsetNeighbourX(world, x, k);
        const std::uint32_t ny = OffsetNeighbourY(world, y, k);
        const std::uint32_t size = MapSize(world);
        if (nx < size && ny < size && At<std::uint16_t>(world, Tile(nx, ny) + kTileWord16) != 0) {
            GameWorld_SplitTileWord16Region(world, nullptr, nx, ny);
        }
    }
    return 0;
}

S3RE_HOOK(0x0051c1f0, GameWorld_SplitTileWord16Region, SplitFn, Original_GameWorld_SplitTileWord16Region,
          GameWorld_SplitTileWord16Region);
S3RE_HOOK(0x0051caf0, GameWorld_SplitTileByte10Region, SplitFn, Original_GameWorld_SplitTileByte10Region,
          GameWorld_SplitTileByte10Region);
S3RE_HOOK(0x0051c080, GameWorld_SplitWord16RegionsAround, MergeFn, Original_GameWorld_SplitWord16RegionsAround,
          GameWorld_SplitWord16RegionsAround);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// Covers the tiles, the ring and its indices; area records are only touched
// by MoveTileToAreaAndQueue, which has its own, larger world.
constexpr std::size_t kTestWorld = kTileRingWrite + 4;
constexpr std::size_t kAreaRecords = 0xe145ec;
constexpr std::uint32_t kAreaRecordBytes = 0x2e0;
constexpr std::uint32_t kTestAreas = 64;
constexpr std::size_t kAreaTestWorld = kAreaRecords + kTestAreas * kAreaRecordBytes;

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
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
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

// Region tiles mostly hold `old` in the field (size 1 or 2 bytes at `field`);
// the new value never has `old` in its low bytes (that would loop for ever).
void SetUpReplaceFill(test::Context& ctx, test::WorldPair& worlds, std::uint32_t size, std::uint32_t x,
                      std::uint32_t y, std::uint32_t& value, std::uint32_t field, std::uint32_t bytes) {
    const std::uint32_t old = ctx.rng().Below(3);
    const Region region = RandomRegion(ctx, size, x, y);
    const std::uint32_t density = 1 + ctx.rng().Below(4);  // of 4
    ForRegion(region, [&](std::uint32_t tx, std::uint32_t ty) {
        // The start tile always holds `old` (else `value` could equal it).
        const bool start = tx == x && ty == y;
        const std::uint32_t v = start || ctx.rng().Chance(density, 4) ? old : 3 + ctx.rng().Below(3);
        if (bytes == 2) {
            worlds.Set<std::uint16_t>(Tile(tx, ty) + field, static_cast<std::uint16_t>(v));
        } else {
            worlds.Set<std::uint8_t>(Tile(tx, ty) + field, static_cast<std::uint8_t>(v));
        }
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
        worlds.Set<std::uint8_t>(Tile(tx, ty) + kTileFlags, flags);
        if (terrain) {
            worlds.Set<std::uint8_t>(Tile(tx, ty) + kTileTerrain,
                                     static_cast<std::uint8_t>(ctx.rng().Chance(4, 5) ? 5 + ctx.rng().Below(3)
                                                                                      : ctx.rng().Below(10)));
        }
    });
    value = ctx.rng().Chance(1, 2) ? ctx.rng().Below(0x200) : ctx.rng().Next32();
}

// Wrong: spreads to four neighbours only (no diagonals).
std::uint32_t __fastcall WrongFloodFillTileWord16(GameWorld* world, void*, std::uint32_t x, std::uint32_t y,
                                                  std::uint32_t value) {
    std::uint16_t& start = At<std::uint16_t>(world, Tile(x, y) + kTileWord16);
    const std::uint32_t old = start;
    StartRing(world, x, y);
    start = static_cast<std::uint16_t>(value);
    RunQueue(world, [&](std::uint32_t px, std::uint32_t py) {
        ForEachNeighbour(world, px, py, [&](std::uint32_t nx, std::uint32_t ny) {
            if (nx == px || ny == py) {
                GameWorld_ReplaceTileWord16AndQueue(world, nullptr, nx, ny, old, value);
            }
        });
    });
    return At<std::uint32_t>(world, kTileRingRead);
}

void CompareWord16Fill(test::Context& ctx, FloodFn candidate, int cases) {
    CompareFloods(ctx, Original_GameWorld_FloodFillTileWord16, candidate, cases,
                  [&](test::WorldPair& worlds, std::uint32_t size, std::uint32_t x, std::uint32_t y,
                      std::uint32_t& value) { SetUpReplaceFill(ctx, worlds, size, x, y, value, kTileWord16, 2); });
}

// Wrong: offers the (x+1, y+1) neighbour first. The same tiles get marked,
// but the ring holds them in another order.
std::uint32_t __fastcall WrongFloodMarkAreaTerrain5To7(GameWorld* world, void* edx, std::uint32_t x,
                                                       std::uint32_t y, std::uint32_t value) {
    StartRing(world, x, y);
    RunQueue(world, [&](std::uint32_t px, std::uint32_t py) {
        GameWorld_SetTileAreaMarked(world, edx, px, py, value);
        const std::uint32_t last = MapSize(world) - 1;
        if (px != last && py != last) {
            GameWorld_QueueMarkedTileTerrain5To7(world, nullptr, px + 1, py + 1);
        }
        ForEachNeighbour(world, px, py, [&](std::uint32_t nx, std::uint32_t ny) {
            if (!(nx == px + 1 && ny == py + 1)) {
                GameWorld_QueueMarkedTileTerrain5To7(world, nullptr, nx, ny);
            }
        });
    });
    return At<std::uint32_t>(world, kTileRingWrite);
}

void CompareTerrainFill(test::Context& ctx, FloodFn candidate, int cases) {
    CompareFloods(ctx, Original_GameWorld_FloodMarkAreaTerrain5To7, candidate, cases,
                  [&](test::WorldPair& worlds, std::uint32_t size, std::uint32_t x, std::uint32_t y,
                      std::uint32_t& value) { SetUpMarkedFill(ctx, worlds, size, x, y, value, true); });
}

}  // namespace

S3RE_TEST(GameWorld_FloodFillTileWord16_MatchesOriginal) {
    CompareWord16Fill(ctx, &GameWorld_FloodFillTileWord16, 300);
}
S3RE_TEST(GameWorld_FloodFillTileWord16_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareWord16Fill(probe, &WrongFloodFillTileWord16, 100);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_FloodFillTileByte10_MatchesOriginal) {
    CompareFloods(ctx, Original_GameWorld_FloodFillTileByte10, &GameWorld_FloodFillTileByte10, 300,
                  [&](test::WorldPair& worlds, std::uint32_t size, std::uint32_t x, std::uint32_t y,
                      std::uint32_t& value) { SetUpReplaceFill(ctx, worlds, size, x, y, value, kTileByte10, 1); });
}
S3RE_TEST(GameWorld_FloodMarkAreaTerrain5To7_MatchesOriginal) {
    CompareTerrainFill(ctx, &GameWorld_FloodMarkAreaTerrain5To7, 300);
}
S3RE_TEST(GameWorld_FloodMarkAreaTerrain5To7_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareTerrainFill(probe, &WrongFloodMarkAreaTerrain5To7, 100);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_FloodSetByte10MarkedTiles_MatchesOriginal) {
    CompareFloods(ctx, Original_GameWorld_FloodSetByte10MarkedTiles, &GameWorld_FloodSetByte10MarkedTiles, 300,
                  [&](test::WorldPair& worlds, std::uint32_t size, std::uint32_t x, std::uint32_t y,
                      std::uint32_t& value) { SetUpMarkedFill(ctx, worlds, size, x, y, value, false); });
}

S3RE_TEST(GameWorld_MoveTileToAreaAndQueue_MatchesOriginal) {
    test::WorldPair worlds(ctx, kAreaTestWorld);
    for (int i = 0; i < 3000; ++i) {
        const std::uint32_t x = ctx.rng().Below(384);
        const std::uint32_t y = ctx.rng().Below(384);
        const std::uint32_t area = ctx.rng().Below(kTestAreas);
        worlds.Set<std::uint16_t>(Tile(x, y) + kTileArea, static_cast<std::uint16_t>(area));
        std::uint32_t oldArea = ctx.rng().Chance(2, 3) ? area : ctx.rng().Below(kTestAreas);
        if (ctx.rng().Chance(1, 16)) {
            oldArea |= 0x10000u << ctx.rng().Below(16);  // equal low word, but not equal
        }
        const std::uint32_t newArea = ctx.rng().Below(kTestAreas);
        worlds.Set<std::uint32_t>(kTileRingWrite, ctx.rng().Chance(1, 8) ? kTileRingEntries - 1
                                                                         : ctx.rng().Below(kTileRingEntries));
        const std::uint32_t expected =
            Original_GameWorld_MoveTileToAreaAndQueue(worlds.original(), nullptr, x, y, oldArea, newArea);
        const std::uint32_t actual = GameWorld_MoveTileToAreaAndQueue(worlds.ours(), nullptr, x, y, oldArea, newArea);
        if (!S3RE_EXPECT_EQ(expected, actual) ||
            ((i % 64 == 63 || i == 2999) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 63 before it)", i);
            return;
        }
    }
}

namespace {

constexpr std::size_t kMergeTestWorld = kOffsetTable + 20 * kOffsetTableBytes;

// The real first 7 entries of the offset table: the tile and its neighbours.
void SetUpNeighbourOffsets(test::WorldPair& worlds) {
    static constexpr std::int32_t kOffsets[7][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {-1, 0}, {-1, -1}, {0, -1}};
    for (std::uint32_t k = 0; k < 7; ++k) {
        worlds.Set<std::int32_t>(kOffsetTable + k * kOffsetTableBytes, kOffsets[k][0]);
        worlds.Set<std::int32_t>(kOffsetTable + k * kOffsetTableBytes + 4, kOffsets[k][1]);
    }
}

// Ids table: ids 1..first-1 used, then free (FindFreeId scans from 1).
void SetUpIds(test::Context& ctx, test::WorldPair& worlds, std::uint32_t table, std::uint32_t mark,
              std::uint32_t size) {
    const std::uint32_t used = 1 + ctx.rng().Below(40);
    for (std::uint32_t id = 0; id < used + 2 && id < size; ++id) {
        worlds.Set<std::uint8_t>(table + id, static_cast<std::uint8_t>(id < used ? 1 : 0));
    }
    worlds.Set<std::uint32_t>(mark, ctx.rng().Below(used + 2));
}

template <typename Setup>
void CompareMerge(test::Context& ctx, MergeFn original, MergeFn candidate, int cases, Setup setup) {
    test::WorldPair worlds(ctx, kMergeTestWorld);
    SetUpNeighbourOffsets(worlds);
    for (int i = 0; i < cases; ++i) {
        // A small map (the floods stay small), the tile inside it.
        const std::uint32_t size = 4 + ctx.rng().Below(24);
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
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
std::uint32_t __fastcall WrongMergeTileWord16Regions(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t result = GameWorld_MergeTileWord16Regions(world, edx, x, y);
    At<std::uint8_t>(world, kIds8192 + 1) = 1;
    return result;
}

void CompareWord16Merge(test::Context& ctx, MergeFn candidate, int cases) {
    CompareMerge(ctx, Original_GameWorld_MergeTileWord16Regions, candidate, cases,
                 [&](test::WorldPair& worlds, std::uint32_t size) {
                     // Region ids 0..3 (0 = none) over the whole small map.
                     for (std::uint32_t tx = 0; tx < size; ++tx) {
                         for (std::uint32_t ty = 0; ty < size; ++ty) {
                             worlds.Set<std::uint16_t>(Tile(tx, ty) + kTileWord16,
                                                       static_cast<std::uint16_t>(ctx.rng().Below(4)));
                         }
                     }
                     SetUpIds(ctx, worlds, kIds8192, 0xe125e4, 0x2000);
                 });
}

}  // namespace

S3RE_TEST(GameWorld_MergeTileWord16Regions_MatchesOriginal) {
    CompareWord16Merge(ctx, &GameWorld_MergeTileWord16Regions, 3000);
}
S3RE_TEST(GameWorld_MergeTileWord16Regions_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareWord16Merge(probe, &WrongMergeTileWord16Regions, 500);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_MergeTileByte10Regions_MatchesOriginal) {
    CompareMerge(ctx, Original_GameWorld_MergeTileByte10Regions, &GameWorld_MergeTileByte10Regions, 3000,
                 [&](test::WorldPair& worlds, std::uint32_t size) {
                     for (std::uint32_t tx = 0; tx < size; ++tx) {
                         for (std::uint32_t ty = 0; ty < size; ++ty) {
                             worlds.Set<std::uint8_t>(Tile(tx, ty) + kTileByte10,
                                                      static_cast<std::uint8_t>(ctx.rng().Below(4)));
                             const std::uint8_t flags = static_cast<std::uint8_t>(ctx.rng().Next32());
                             worlds.Set<std::uint8_t>(Tile(tx, ty) + kTileFlags,
                                                      ctx.rng().Chance(1, 3) ? flags : static_cast<std::uint8_t>(flags & ~kTileBlocked));
                         }
                     }
                     SetUpIds(ctx, worlds, kIds256, 0xe114d8, 0x100);
                 });
}

S3RE_TEST(GameWorld_FloodMoveArea_MatchesOriginal) {
    test::WorldPair worlds(ctx, kAreaTestWorld);
    for (int i = 0; i < 300; ++i) {
        const std::uint32_t size = RandomMapSize(ctx);
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
        const std::uint32_t x = ctx.rng().Below(size);
        const std::uint32_t y = ctx.rng().Below(size);
        // Areas 1..3 in a region around the start; the start in area `old`.
        const std::uint32_t old = 1 + ctx.rng().Below(3);
        const Region region = RandomRegion(ctx, size, x, y);
        ForRegion(region, [&](std::uint32_t tx, std::uint32_t ty) {
            const bool start = tx == x && ty == y;
            worlds.Set<std::uint16_t>(Tile(tx, ty) + kTileArea,
                                      static_cast<std::uint16_t>(start || ctx.rng().Chance(2, 3) ? old : 1 + ctx.rng().Below(3)));
        });
        std::uint32_t newArea = 1 + ctx.rng().Below(kTestAreas - 1);
        if (newArea == old) {
            newArea = old + 4;  // the same area would loop for ever
        }
        const std::uint32_t expected = Original_GameWorld_FloodMoveArea(worlds.original(), nullptr, x, y, newArea);
        const std::uint32_t actual = GameWorld_FloodMoveArea(worlds.ours(), nullptr, x, y, newArea);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%u, %u) -> area %u, map %u", i, x, y, newArea, size);
            return;
        }
    }
}

namespace {

// Wrong: ignores the owner (queues marked tiles of every player).
std::uint32_t __fastcall WrongFloodAssignMarkedTiles(GameWorld* world, void*, std::uint32_t x, std::uint32_t y,
                                                     std::uint32_t area, std::uint32_t /*player*/) {
    StartRing(world, x, y);
    RunQueue(world, [&](std::uint32_t px, std::uint32_t py) {
        GameWorld_RemoveTileFromArea(world, nullptr, px, py);
        GameWorld_AssignTileToArea(world, nullptr, px, py, area);
        ForEachNeighbour(world, px, py, [&](std::uint32_t nx, std::uint32_t ny) {
            GameWorld_QueueMarkedTile(world, nullptr, nx, ny, At<std::uint8_t>(world, Tile(nx, ny) + 0x8));
        });
    });
    return At<std::uint32_t>(world, kTileRingRead);
}

void CompareFloodAssign(test::Context& ctx, FloodAssignFn candidate, int cases) {
    test::WorldPair worlds(ctx, kAreaTestWorld);
    // No tile is marked at first; region tiles get valid area words, so
    // every tile the flood can reach has its area record in the world.
    for (std::uint32_t tx = 0; tx < 384; ++tx) {
        for (std::uint32_t ty = 0; ty < 384; ++ty) {
            const std::uint8_t flags = At<std::uint8_t>(worlds.original(), Tile(tx, ty) + kTileFlags);
            worlds.Set<std::uint8_t>(Tile(tx, ty) + kTileFlags, static_cast<std::uint8_t>(flags & ~0x10));
        }
    }
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = RandomMapSize(ctx);
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
        const std::uint32_t x = ctx.rng().Below(size);
        const std::uint32_t y = ctx.rng().Below(size);
        const std::uint32_t player = ctx.rng().Below(3);
        const Region region = RandomRegion(ctx, size, x, y);
        const std::uint32_t density = 1 + ctx.rng().Below(4);  // of 4
        ForRegion(region, [&](std::uint32_t tx, std::uint32_t ty) {
            std::uint8_t flags = static_cast<std::uint8_t>(ctx.rng().Next32());
            flags = static_cast<std::uint8_t>(ctx.rng().Chance(density, 4) ? (flags | 0x10) & ~kTileBlocked : flags);
            worlds.Set<std::uint8_t>(Tile(tx, ty) + kTileFlags, flags);
            worlds.Set<std::uint8_t>(Tile(tx, ty) + 0x8,
                                     static_cast<std::uint8_t>(ctx.rng().Chance(3, 4) ? player : ctx.rng().Below(3)));
            worlds.Set<std::uint16_t>(Tile(tx, ty) + kTileArea, static_cast<std::uint16_t>(ctx.rng().Below(4)));
        });
        const std::uint32_t area = 1 + ctx.rng().Below(kTestAreas - 1);
        const std::uint32_t expected =
            Original_GameWorld_FloodAssignMarkedTiles(worlds.original(), nullptr, x, y, area, player);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, x, y, area, player);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%u, %u) -> area %u, player %u, map %u", i, x, y, area, player,
                     size);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(GameWorld_FloodAssignMarkedTiles_MatchesOriginal) {
    CompareFloodAssign(ctx, &GameWorld_FloodAssignMarkedTiles, 300);
}
S3RE_TEST(GameWorld_FloodAssignMarkedTiles_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareFloodAssign(probe, &WrongFloodAssignMarkedTiles, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

std::uint32_t RelativeToWorld(std::uint32_t value, GameWorld* world) {
    return value - static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));
}

// Wrong: ignores the terrain check.
std::uint32_t __fastcall WrongAssignWord16RegionsAround(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    for (std::uint32_t k = 0; k < 19; ++k) {
        const std::uint32_t size = MapSize(world);
        const std::uint32_t nx = OffsetNeighbourX(world, x, k);
        const std::uint32_t ny = OffsetNeighbourY(world, y, k);
        if (nx >= size || ny >= size || At<std::uint16_t>(world, Tile(nx, ny) + kTileWord16) != 0) {
            continue;
        }
        bool clear = true;
        for (std::uint32_t j = 0; j < 19; ++j) {
            const std::uint32_t mx = OffsetNeighbourX(world, nx, j);
            const std::uint32_t my = OffsetNeighbourY(world, ny, j);
            if (mx < size && my < size && (At<std::uint8_t>(world, Tile(mx, my) + kTileFlags) & kTileBlocked) != 0) {
                clear = false;
            }
        }
        if (clear) {
            GameWorld_MergeTileWord16Regions(world, nullptr, nx, ny);
        }
    }
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world)) + kOffsetTable + 4 + 19 * kOffsetTableBytes;
}

void CompareAssignRegions(test::Context& ctx, MergeFn candidate, int cases) {
    test::WorldPair worlds(ctx, kMergeTestWorld);
    SetUpNeighbourOffsets(worlds);
    // Entries 7..18: the second ring (any order).
    static constexpr std::int32_t kRing2[12][2] = {{2, 0}, {2, 1}, {2, 2}, {1, 2}, {0, 2}, {-1, 1},
                                                   {-2, 0}, {-2, -1}, {-2, -2}, {-1, -2}, {0, -2}, {1, -1}};
    for (std::uint32_t k = 0; k < 12; ++k) {
        worlds.Set<std::int32_t>(kOffsetTable + (7 + k) * kOffsetTableBytes, kRing2[k][0]);
        worlds.Set<std::int32_t>(kOffsetTable + (7 + k) * kOffsetTableBytes + 4, kRing2[k][1]);
    }
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = 8 + ctx.rng().Below(20);
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
        // At least 3 tiles from the edge: the merge looks one tile beyond
        // the tiles it fills and would flood off the map (real maps have a
        // border of tiles that never qualify).
        const std::uint32_t x = 3 + ctx.rng().Below(size - 6);
        const std::uint32_t y = 3 + ctx.rng().Below(size - 6);
        const std::uint32_t blocked = ctx.rng().Below(40);
        for (std::uint32_t tx = 0; tx < size + 3; ++tx) {
            for (std::uint32_t ty = 0; ty < size + 3; ++ty) {
                const std::uint32_t tile = Tile(tx, ty);
                worlds.Set<std::uint16_t>(tile + kTileWord16,
                                          static_cast<std::uint16_t>(ctx.rng().Chance(2, 3) ? 0 : ctx.rng().Below(4)));
                const std::uint8_t flags = static_cast<std::uint8_t>(ctx.rng().Next32());
                worlds.Set<std::uint8_t>(tile + kTileFlags, static_cast<std::uint8_t>(
                                                                ctx.rng().Below(1000) < blocked ? flags | 1 : flags & 0xfe));
                worlds.Set<std::uint8_t>(tile + kTileTerrain,
                                         static_cast<std::uint8_t>(ctx.rng().Below(1000) < blocked ? 0x21 : 0x10));
            }
        }
        SetUpIds(ctx, worlds, kIds8192, 0xe125e4, 0x2000);
        const std::uint32_t expected =
            RelativeToWorld(Original_GameWorld_AssignWord16RegionsAround(worlds.original(), nullptr, x, y), worlds.original());
        const std::uint32_t actual = RelativeToWorld(candidate(worlds.ours(), nullptr, x, y), worlds.ours());
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 32 == 31 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 31 before it): (%u, %u) map %u", i, x, y, size);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(GameWorld_AssignWord16RegionsAround_MatchesOriginal) {
    CompareAssignRegions(ctx, &GameWorld_AssignWord16RegionsAround, 3000);
}
S3RE_TEST(GameWorld_AssignWord16RegionsAround_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareAssignRegions(probe, &WrongAssignWord16RegionsAround, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

// Wrong: never splits (a region stays one even when cut in two).
void __fastcall WrongSplitTileWord16Region(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t old = At<std::uint16_t>(world, Tile(x, y) + kTileWord16);
    At<std::uint16_t>(world, Tile(x, y) + kTileWord16) = 0;
    std::uint32_t same = 0;
    for (std::uint32_t k = 1; k <= 6; ++k) {
        if (At<std::uint16_t>(world, Tile(OffsetNeighbourX(world, x, k), OffsetNeighbourY(world, y, k)) + kTileWord16) == old) {
            ++same;
        }
    }
    if (same == 0) {
        At<std::uint8_t>(world, kIds8192 + old) = 0;
    }
}

// A small map of region ids (field of `bytes` bytes): ids 1..3 and 0 at
// random, so that regions touch the tile in many ways; random flags (bit
// 0x10 often set, to show which tiles the walk clears). The frame of tiles
// just off the map has no region (as on real maps, whose border tiles never
// get one; a neighbour off the map in a region would be flooded from x or
// y = 0xffff). The ids table marks 1..3 (and more) in use, so a new id
// never equals an old one.
void SetUpSplit(test::Context& ctx, test::WorldPair& worlds, std::uint32_t size, std::uint32_t field,
                std::uint32_t bytes, std::uint32_t table, std::uint32_t mark, std::uint32_t tableSize) {
    const std::uint32_t empty = ctx.rng().Below(5);  // in 8: share of tiles without a region
    const std::uint32_t kinds = 1 + ctx.rng().Below(3);
    for (std::uint32_t tx = 0xffffffffu; tx != size + 1; ++tx) {
        for (std::uint32_t ty = 0xffffffffu; ty != size + 1; ++ty) {
            const bool onMap = tx < size && ty < size;
            const std::uint32_t value = !onMap || ctx.rng().Below(8) < empty ? 0 : 1 + ctx.rng().Below(kinds);
            if (bytes == 2) {
                worlds.Set<std::uint16_t>(Tile(tx, ty) + field, static_cast<std::uint16_t>(value));
            } else {
                worlds.Set<std::uint8_t>(Tile(tx, ty) + field, static_cast<std::uint8_t>(value));
            }
            worlds.Set<std::uint8_t>(Tile(tx, ty) + kTileFlags, static_cast<std::uint8_t>(ctx.rng().Next32()));
        }
    }
    const std::uint32_t used = 4 + ctx.rng().Below(40);
    for (std::uint32_t id = 0; id < used + 2 && id < tableSize; ++id) {
        worlds.Set<std::uint8_t>(table + id, static_cast<std::uint8_t>(id < used ? 1 : 0));
    }
    worlds.Set<std::uint32_t>(mark, ctx.rng().Below(used + 2));
}

// Like the game's regions: random walkable tiles, each connected stretch
// (six neighbours) with its own id 1, 2, ..., none for the rest and the
// frame off the map; ids in use marked in the table. (With ids shared by
// separate stretches, a freed id could be handed out again while still in
// use, and the flood fill would then loop for ever, in the original too.)
void SetUpLabelledRegions(test::Context& ctx, test::WorldPair& worlds, std::uint32_t size, std::uint32_t field,
                          std::uint32_t bytes, std::uint32_t table, std::uint32_t mark, std::uint32_t tableSize) {
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
            if (bytes == 2) {
                worlds.Set<std::uint16_t>(Tile(tx, ty) + field, static_cast<std::uint16_t>(value));
            } else {
                worlds.Set<std::uint8_t>(Tile(tx, ty) + field, static_cast<std::uint8_t>(value));
            }
            worlds.Set<std::uint8_t>(Tile(tx, ty) + kTileFlags, static_cast<std::uint8_t>(ctx.rng().Next32()));
        }
    }
    for (std::uint32_t id = 0; id < next + 2 && id < tableSize; ++id) {
        worlds.Set<std::uint8_t>(table + id, static_cast<std::uint8_t>(id != 0 && id < next ? 1 : 0));
    }
    worlds.Set<std::uint32_t>(mark, ctx.rng().Below(next + 2));
}

// What to do when the tile itself has no region id (field `field`): give it
// one (random regions), skip the case (labelled regions, where an id given
// to it would be shared), or call anyway (SplitWord16RegionsAround).
enum class Unlabelled { kGiveId, kSkip, kCall };

template <typename Fn, typename Setup>
void CompareSplit(test::Context& ctx, Fn original, Fn candidate, int cases, bool edges, Setup setup,
                  Unlabelled unlabelled = Unlabelled::kGiveId, std::uint32_t field = kTileWord16) {
    test::WorldPair worlds(ctx, kMergeTestWorld);
    SetUpNeighbourOffsets(worlds);
    static constexpr std::int32_t kRing2[12][2] = {{2, 0}, {2, 1}, {2, 2}, {1, 2}, {0, 2}, {-1, 1},
                                                   {-2, 0}, {-2, -1}, {-2, -2}, {-1, -2}, {0, -2}, {1, -1}};
    for (std::uint32_t k = 0; k < 12; ++k) {
        worlds.Set<std::int32_t>(kOffsetTable + (7 + k) * kOffsetTableBytes, kRing2[k][0]);
        worlds.Set<std::int32_t>(kOffsetTable + (7 + k) * kOffsetTableBytes + 4, kRing2[k][1]);
    }
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = 6 + ctx.rng().Below(22);
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
        // Mostly inside; with `edges` sometimes on the edge of the map.
        const bool edge = edges && ctx.rng().Chance(1, 6);
        const std::uint32_t x = edge ? (ctx.rng().Chance(1, 2) ? 0 : size - 1) : 1 + ctx.rng().Below(size - 2);
        const std::uint32_t y = edge && ctx.rng().Chance(1, 2) ? (ctx.rng().Chance(1, 2) ? 0 : size - 1) : 1 + ctx.rng().Below(size - 2);
        setup(worlds, size);
        // The tile itself is in a region (its callers only call it then).
        const bool none = field == kTileWord16 ? At<std::uint16_t>(worlds.original(), Tile(x, y) + kTileWord16) == 0
                                               : At<std::uint8_t>(worlds.original(), Tile(x, y) + kTileByte10) == 0;
        if (none && unlabelled == Unlabelled::kSkip) {
            continue;
        }
        if (unlabelled == Unlabelled::kGiveId) {
            if (At<std::uint16_t>(worlds.original(), Tile(x, y) + kTileWord16) == 0) {
                worlds.Set<std::uint16_t>(Tile(x, y) + kTileWord16, 1);
            }
            if (At<std::uint8_t>(worlds.original(), Tile(x, y) + kTileByte10) == 0) {
                worlds.Set<std::uint8_t>(Tile(x, y) + kTileByte10, 1);
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
std::uint32_t __fastcall ReturnZero(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y) {
    (*Fn)(world, edx, x, y);
    return 0;
}

SplitFn g_ourSplitWord16 = &GameWorld_SplitTileWord16Region;
SplitFn g_ourSplitByte10 = &GameWorld_SplitTileByte10Region;
SplitFn g_wrongSplitWord16 = &WrongSplitTileWord16Region;

void CompareWord16Split(test::Context& ctx, MergeFn candidate, int cases) {
    CompareSplit(ctx, &ReturnZero<&Original_GameWorld_SplitTileWord16Region>, candidate, cases, true,
                 [&](test::WorldPair& worlds, std::uint32_t size) {
                     SetUpSplit(ctx, worlds, size, kTileWord16, 2, kIds8192, 0xe125e4, 0x2000);
                 });
}

}  // namespace

S3RE_TEST(GameWorld_SplitTileWord16Region_MatchesOriginal) {
    CompareWord16Split(ctx, &ReturnZero<&g_ourSplitWord16>, 5000);
}
S3RE_TEST(GameWorld_SplitTileWord16Region_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareWord16Split(probe, &ReturnZero<&g_wrongSplitWord16>, 1000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_SplitTileByte10Region_MatchesOriginal) {
    CompareSplit(ctx, &ReturnZero<&Original_GameWorld_SplitTileByte10Region>, &ReturnZero<&g_ourSplitByte10>, 5000, true,
                 [&](test::WorldPair& worlds, std::uint32_t size) {
                     SetUpSplit(ctx, worlds, size, kTileByte10, 1, kIds256, 0xe114d8, 0x100);
                 });
}
S3RE_TEST(GameWorld_SplitTileWord16Region_LabelledRegionsMatchOriginal) {
    CompareSplit(ctx, &ReturnZero<&Original_GameWorld_SplitTileWord16Region>, &ReturnZero<&g_ourSplitWord16>, 3000,
                 true, [&](test::WorldPair& worlds, std::uint32_t size) {
                     SetUpLabelledRegions(ctx, worlds, size, kTileWord16, 2, kIds8192, 0xe125e4, 0x2000);
                 }, Unlabelled::kSkip, kTileWord16);
}
S3RE_TEST(GameWorld_SplitTileByte10Region_LabelledRegionsMatchOriginal) {
    CompareSplit(ctx, &ReturnZero<&Original_GameWorld_SplitTileByte10Region>, &ReturnZero<&g_ourSplitByte10>, 3000,
                 true, [&](test::WorldPair& worlds, std::uint32_t size) {
                     SetUpLabelledRegions(ctx, worlds, size, kTileByte10, 1, kIds256, 0xe114d8, 0x100);
                 }, Unlabelled::kSkip, kTileByte10);
}
S3RE_TEST(GameWorld_SplitWord16RegionsAround_MatchesOriginal) {
    // Labelled regions: it splits several tiles in a row, and a region id
    // freed by one split must not be in use elsewhere.
    CompareSplit(ctx, Original_GameWorld_SplitWord16RegionsAround, &GameWorld_SplitWord16RegionsAround, 2000, false,
                 [&](test::WorldPair& worlds, std::uint32_t size) {
                     SetUpLabelledRegions(ctx, worlds, size, kTileWord16, 2, kIds8192, 0xe125e4, 0x2000);
                 }, Unlabelled::kCall);
}

}  // namespace s3re::game
