// Terrain changes and territory border flags on the map tiles.
//
// Tile fields used here (tile (x, y) at world+0x67444 + (x*0x300 + y)*24):
// +0x6 terrain type (the high nibble is non-zero for land a player can own),
// +0x8 owner (player, 0xff = none), +0x9 flags (0x40 = territory border).
//
// GameWorld::ReplaceTerrainF3 (0x0051a400): (newType): calls
//   GameWorld::ReplaceTerrainType(0xf3, newType) and returns its EAX. Used
//   four times by GameWorld::Create. ret 4.
// GameWorld::SpreadTerrainRandomly (0x00519330): (from, neighbour, to, percent):
//   for x in [1, mapSize-1), y in [1, mapSize-1) (signed bounds; nothing if
//   mapSize - 1 <= 1), a tile whose terrain is `from` and that has a
//   neighbour of terrain `neighbour` becomes `to` (low byte) with probability
//   percent/100: r = Random() & 0xffff, (r * 100) >> 16 < percent (signed).
//   The six neighbours are checked in the order (x+1, y), (x-1, y),
//   (x, y-1), (x, y+1), (x+1, y+1), (x-1, y-1) and Random is drawn only when
//   one matches. Changed tiles are seen by the tiles scanned after them.
//   Terrain bytes are compared zero-extended with the full 32-bit `from`
//   and `neighbour`. EAX = mapSize - 1 (callers ignore it). ret 0x10.
// GameWorld::UpdateBorderFlagsAround (0x0051a460): (x, y): for the first 7
//   entries of the offset table at world+0x110a588 (int32 dx, dy; 16-byte
//   stride: the tile itself and its six neighbours), each tile (x+dx, y+dy)
//   inside the map border (GameWorld::IsInsideMapBorder) gets border flag
//   0x40 set if it is owned land (terrain & 0xf0, owner != 0xff) and one of
//   its six neighbours is owned land of another owner (terrain & 0xf0 and
//   owner differs), and cleared otherwise. Neighbour order: (x+1, y),
//   (x+1, y+1), (x, y+1), (x-1, y), (x-1, y-1), (x, y-1). EAX = 0. ret 8.
// GameWorld::GrowTerrain (0x00519470): (from, into, percent):
//   SpreadTerrainRandomly(from, into, 0xf0, percent), then
//   ReplaceTerrainType(0xf0, into): tiles next to `into` become `into` with
//   the probability, through the marker 0xf0 so that tiles converted in this
//   pass don't spread further in it. EAX = ReplaceTerrainType's. ret 0xc.
// GameWorld::UpdateAllBorderFlags (0x0051a420): UpdateBorderFlagsAround(x,
//   y) for every tile, x outer, y inner (unsigned, map size re-read). EAX =
//   mapSize (two callers tail-return it). Plain ret.
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
constexpr std::uint32_t kTileOwner = 0x8;
constexpr std::uint32_t kTileFlags9 = 0x9;
constexpr std::uint8_t kBorderFlag = 0x40;
constexpr std::uint32_t kNoOwner = 0xff;
constexpr std::uint32_t kOffsetTable = 0x110a588;  // int32 dx, int32 dy, 16-byte stride
constexpr std::uint32_t kOffsetTableBytes = 0x10;
constexpr std::uint32_t kAroundEntries = 7;

constexpr std::int32_t kRow = 0x300 * 24;  // x + 1
constexpr std::int32_t kColumn = 24;       // y + 1

template <typename T>
T& At(GameWorld* world, std::uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<std::uint8_t*>(world) + offset);
}

std::uint32_t Tile(std::uint32_t x, std::uint32_t y) {
    return kTiles + (x * 0x300 + y) * 24;
}

std::uint32_t Terrain(GameWorld* world, std::uint32_t tile) {
    return At<std::uint8_t>(world, tile + kTileTerrain);
}

}  // namespace

using ReplaceF3Fn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t newType);
using SpreadTerrainFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t from,
                                                   std::uint32_t neighbour, std::uint32_t to,
                                                   std::uint32_t percent);
using AroundFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x,
                                            std::uint32_t y);

std::uint32_t __fastcall GameWorld_ReplaceTerrainF3(GameWorld* world, void*, std::uint32_t newType) {
    return GameWorld_ReplaceTerrainType(world, nullptr, 0xf3, newType);
}

std::uint32_t __fastcall GameWorld_SpreadTerrainRandomly(GameWorld* world, void*, std::uint32_t from,
                                                         std::uint32_t neighbour, std::uint32_t to,
                                                         std::uint32_t percent) {
    auto last = [&] { return static_cast<std::int32_t>(At<std::uint32_t>(world, world_offset::kMapSize) - 1); };
    for (std::int32_t x = 1; x < last(); ++x) {
        for (std::int32_t y = 1; y < last(); ++y) {
            const std::uint32_t tile = Tile(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
            if (Terrain(world, tile) != from) {
                continue;
            }
            const bool nearby = Terrain(world, tile + kRow) == neighbour || Terrain(world, tile - kRow) == neighbour ||
                                Terrain(world, tile - kColumn) == neighbour || Terrain(world, tile + kColumn) == neighbour ||
                                Terrain(world, tile + kRow + kColumn) == neighbour ||
                                Terrain(world, tile - kRow - kColumn) == neighbour;
            if (!nearby) {
                continue;
            }
            const std::int32_t roll = static_cast<std::int32_t>((GameWorld_Random(world, nullptr) & 0xffffu) * 100u) >> 16;
            if (roll < static_cast<std::int32_t>(percent)) {
                At<std::uint8_t>(world, tile + kTileTerrain) = static_cast<std::uint8_t>(to);
            }
        }
    }
    return At<std::uint32_t>(world, world_offset::kMapSize) - 1;
}

std::uint32_t __fastcall GameWorld_UpdateBorderFlagsAround(GameWorld* world, void*, std::uint32_t x,
                                                           std::uint32_t y) {
    static constexpr std::int32_t kNeighbours[6] = {kRow, kRow + kColumn, kColumn, -kRow, -kRow - kColumn, -kColumn};
    for (std::uint32_t entry = 0; entry < kAroundEntries; ++entry) {
        const std::uint32_t at = kOffsetTable + entry * kOffsetTableBytes;
        const std::uint32_t tx = x + At<std::uint32_t>(world, at);
        const std::uint32_t ty = y + At<std::uint32_t>(world, at + 4);
        if (!GameWorld_IsInsideMapBorder(world, nullptr, tx, ty)) {
            continue;
        }
        const std::uint32_t tile = Tile(tx, ty);
        const std::uint32_t owner = At<std::uint8_t>(world, tile + kTileOwner);
        bool border = false;
        if ((Terrain(world, tile) & 0xf0) != 0 && owner != kNoOwner) {
            for (const std::int32_t step : kNeighbours) {
                const std::uint32_t neighbour = tile + step;
                if (At<std::uint8_t>(world, neighbour + kTileOwner) != owner &&
                    (Terrain(world, neighbour) & 0xf0) != 0) {
                    border = true;
                    break;
                }
            }
        }
        std::uint8_t& flags = At<std::uint8_t>(world, tile + kTileFlags9);
        flags = border ? static_cast<std::uint8_t>(flags | kBorderFlag) : static_cast<std::uint8_t>(flags & ~kBorderFlag);
    }
    return 0;
}

S3RE_HOOK(0x0051a400, GameWorld_ReplaceTerrainF3, ReplaceF3Fn, Original_GameWorld_ReplaceTerrainF3,
          GameWorld_ReplaceTerrainF3);
S3RE_HOOK(0x00519330, GameWorld_SpreadTerrainRandomly, SpreadTerrainFn,
          Original_GameWorld_SpreadTerrainRandomly, GameWorld_SpreadTerrainRandomly);
S3RE_HOOK(0x0051a460, GameWorld_UpdateBorderFlagsAround, AroundFn, Original_GameWorld_UpdateBorderFlagsAround,
          GameWorld_UpdateBorderFlagsAround);

using GrowTerrainFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t from,
                                                 std::uint32_t into, std::uint32_t percent);
using NoArgsFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx);

std::uint32_t __fastcall GameWorld_GrowTerrain(GameWorld* world, void*, std::uint32_t from, std::uint32_t into,
                                               std::uint32_t percent) {
    GameWorld_SpreadTerrainRandomly(world, nullptr, from, into, 0xf0, percent);
    return GameWorld_ReplaceTerrainType(world, nullptr, 0xf0, into);
}

std::uint32_t __fastcall GameWorld_UpdateAllBorderFlags(GameWorld* world, void*) {
    auto size = [&] { return At<std::uint32_t>(world, world_offset::kMapSize); };
    for (std::uint32_t x = 0; x < size(); ++x) {
        for (std::uint32_t y = 0; y < size(); ++y) {
            GameWorld_UpdateBorderFlagsAround(world, nullptr, x, y);
        }
    }
    return size();
}

S3RE_HOOK(0x00519470, GameWorld_GrowTerrain, GrowTerrainFn, Original_GameWorld_GrowTerrain, GameWorld_GrowTerrain);
S3RE_HOOK(0x0051a420, GameWorld_UpdateAllBorderFlags, NoArgsFn, Original_GameWorld_UpdateAllBorderFlags,
          GameWorld_UpdateAllBorderFlags);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kTestWorld = kOffsetTable + 64 * kOffsetTableBytes;

// The real offset table starts with the tile itself and its six
// neighbours; tests use that, sometimes with a random entry.
void SetUpOffsets(test::Context& ctx, test::WorldPair& worlds) {
    static constexpr std::int32_t kOffsets[7][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {-1, 0}, {-1, -1}, {0, -1}};
    for (std::uint32_t i = 0; i < kAroundEntries; ++i) {
        std::int32_t dx = kOffsets[i][0];
        std::int32_t dy = kOffsets[i][1];
        if (ctx.rng().Chance(1, 16)) {
            dx = ctx.rng().Range(-3, 3);
            dy = ctx.rng().Range(-3, 3);
        }
        worlds.Set<std::int32_t>(kOffsetTable + i * kOffsetTableBytes, dx);
        worlds.Set<std::int32_t>(kOffsetTable + i * kOffsetTableBytes + 4, dy);
    }
}

// Owners and terrains of the tiles within 4 of (x, y) (inside the map): few
// owners, mostly land.
void SetUpTerritory(test::Context& ctx, test::WorldPair& worlds, std::uint32_t size, std::uint32_t x,
                    std::uint32_t y) {
    for (std::int32_t dx = -4; dx <= 4; ++dx) {
        for (std::int32_t dy = -4; dy <= 4; ++dy) {
            const std::uint32_t tx = x + dx;
            const std::uint32_t ty = y + dy;
            if (tx >= size || ty >= size) {
                continue;
            }
            const std::uint32_t tile = Tile(tx, ty);
            const std::uint32_t owner = ctx.rng().Chance(1, 6) ? kNoOwner : ctx.rng().Below(3);
            worlds.Set<std::uint8_t>(tile + kTileOwner, static_cast<std::uint8_t>(owner));
            const std::uint8_t terrain = static_cast<std::uint8_t>(ctx.rng().Next32());
            worlds.Set<std::uint8_t>(tile + kTileTerrain,
                                     ctx.rng().Chance(1, 5) ? static_cast<std::uint8_t>(terrain & 0x0f) : terrain);
        }
    }
}

// Wrong: ignores the terrain of the neighbours.
std::uint32_t __fastcall WrongUpdateBorderFlagsAround(GameWorld* world, void*, std::uint32_t x,
                                                      std::uint32_t y) {
    static constexpr std::int32_t kNeighbours[6] = {kRow, kRow + kColumn, kColumn, -kRow, -kRow - kColumn, -kColumn};
    for (std::uint32_t entry = 0; entry < kAroundEntries; ++entry) {
        const std::uint32_t at = kOffsetTable + entry * kOffsetTableBytes;
        const std::uint32_t tx = x + At<std::uint32_t>(world, at);
        const std::uint32_t ty = y + At<std::uint32_t>(world, at + 4);
        if (!GameWorld_IsInsideMapBorder(world, nullptr, tx, ty)) {
            continue;
        }
        const std::uint32_t tile = Tile(tx, ty);
        const std::uint32_t owner = At<std::uint8_t>(world, tile + kTileOwner);
        bool border = false;
        if ((Terrain(world, tile) & 0xf0) != 0 && owner != kNoOwner) {
            for (const std::int32_t step : kNeighbours) {
                border = border || At<std::uint8_t>(world, tile + step + kTileOwner) != owner;
            }
        }
        std::uint8_t& flags = At<std::uint8_t>(world, tile + kTileFlags9);
        flags = border ? static_cast<std::uint8_t>(flags | kBorderFlag) : static_cast<std::uint8_t>(flags & ~kBorderFlag);
    }
    return 0;
}

void CompareBorderFlags(test::Context& ctx, AroundFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = ctx.rng().Chance(1, 2) ? 384 : 8 + ctx.rng().Below(40);
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
        // Anywhere on the map; the edges often on the small maps.
        const std::uint32_t x = ctx.rng().Below(size);
        const std::uint32_t y = ctx.rng().Below(size);
        SetUpOffsets(ctx, worlds);
        SetUpTerritory(ctx, worlds, size, x, y);
        const std::uint32_t expected = Original_GameWorld_UpdateBorderFlagsAround(worlds.original(), nullptr, x, y);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, x, y);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 32 == 31 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 31 before it)", i);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(GameWorld_UpdateBorderFlagsAround_MatchesOriginal) {
    CompareBorderFlags(ctx, &GameWorld_UpdateBorderFlagsAround, 3000);
}
S3RE_TEST(GameWorld_UpdateBorderFlagsAround_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareBorderFlags(probe, &WrongUpdateBorderFlagsAround, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_SpreadTerrainRandomly_MatchesOriginal) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < 40; ++i) {
        const std::uint32_t size = ctx.rng().Chance(1, 4) ? 384 : ctx.rng().Below(60);
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
        // Few terrain types, so tiles match often.
        const std::uint32_t limit = size < 384 ? size + 1 : 384;
        for (std::uint32_t x = 0; x < limit; ++x) {
            for (std::uint32_t y = 0; y < limit; ++y) {
                worlds.Set<std::uint8_t>(Tile(x, y) + kTileTerrain, static_cast<std::uint8_t>(ctx.rng().Below(4)));
            }
        }
        const std::uint32_t from = ctx.rng().Below(4);
        const std::uint32_t neighbour = ctx.rng().Chance(1, 8) ? from : ctx.rng().Below(4);
        std::uint32_t to = ctx.rng().Below(4);
        if (ctx.rng().Chance(1, 4)) {
            to |= ctx.rng().Next32() & ~0xffu;
        }
        const std::uint32_t percent = ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(110);
        worlds.Set<std::uint16_t>(world_offset::kRandomState, static_cast<std::uint16_t>(ctx.rng().Next32()));
        worlds.Set<std::uint32_t>(world_offset::kRandomState + 2, ctx.rng().Next32());
        const std::uint32_t expected =
            Original_GameWorld_SpreadTerrainRandomly(worlds.original(), nullptr, from, neighbour, to, percent);
        const std::uint32_t actual = GameWorld_SpreadTerrainRandomly(worlds.ours(), nullptr, from, neighbour, to, percent);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: map %u, %u neighbour %u -> 0x%x at %u%%", i, size, from, neighbour, to,
                     percent);
            return;
        }
    }
}

S3RE_TEST(GameWorld_ReplaceTerrainF3_MatchesOriginal) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < 20; ++i) {
        const std::uint32_t size = ctx.rng().Chance(1, 4) ? 384 : ctx.rng().Below(60);
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
        for (std::uint32_t x = 0; x < 64; ++x) {
            for (std::uint32_t y = 0; y < 64; ++y) {
                if (ctx.rng().Chance(1, 3)) {
                    worlds.Set<std::uint8_t>(Tile(x, y) + kTileTerrain, 0xf3);
                }
            }
        }
        const std::uint32_t newType = ctx.rng().Chance(1, 2) ? ctx.rng().Below(256) : ctx.rng().Next32();
        const std::uint32_t expected = Original_GameWorld_ReplaceTerrainF3(worlds.original(), nullptr, newType);
        const std::uint32_t actual = GameWorld_ReplaceTerrainF3(worlds.ours(), nullptr, newType);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

S3RE_TEST(GameWorld_GrowTerrain_MatchesOriginal) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < 30; ++i) {
        const std::uint32_t size = ctx.rng().Chance(1, 4) ? 384 : ctx.rng().Below(60);
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
        const std::uint32_t limit = size < 384 ? size + 1 : 384;
        for (std::uint32_t x = 0; x < limit; ++x) {
            for (std::uint32_t y = 0; y < limit; ++y) {
                worlds.Set<std::uint8_t>(Tile(x, y) + kTileTerrain, static_cast<std::uint8_t>(ctx.rng().Below(4)));
            }
        }
        const std::uint32_t from = ctx.rng().Below(4);
        const std::uint32_t into = ctx.rng().Below(4);
        const std::uint32_t percent = ctx.rng().Below(110);
        worlds.Set<std::uint32_t>(world_offset::kRandomState, ctx.rng().Next32());
        const std::uint32_t expected = Original_GameWorld_GrowTerrain(worlds.original(), nullptr, from, into, percent);
        const std::uint32_t actual = GameWorld_GrowTerrain(worlds.ours(), nullptr, from, into, percent);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

S3RE_TEST(GameWorld_UpdateAllBorderFlags_MatchesOriginal) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < 12; ++i) {
        const std::uint32_t size = ctx.rng().Chance(1, 3) ? 384 : ctx.rng().Below(80);
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
        SetUpOffsets(ctx, worlds);
        const std::uint32_t limit = size < 384 ? size : 384;
        for (std::uint32_t x = 0; x < limit; x += 7) {
            for (std::uint32_t y = 0; y < limit; y += 7) {
                SetUpTerritory(ctx, worlds, size, x, y);
            }
        }
        const std::uint32_t expected = Original_GameWorld_UpdateAllBorderFlags(worlds.original(), nullptr);
        const std::uint32_t actual = GameWorld_UpdateAllBorderFlags(worlds.ours(), nullptr);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: map %u", i, size);
            return;
        }
    }
}

}  // namespace s3re::game
