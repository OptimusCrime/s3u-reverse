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
// All thiscall (ECX = world).

#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>

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

}  // namespace s3re::game
