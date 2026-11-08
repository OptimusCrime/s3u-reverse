// Checks on a tile and its neighbourhood.
//
// Neighbours of (x, y) on the grid (see map_directions.cpp): (x+1, y),
// (x+1, y+1), (x, y+1), (x-1, y), (x-1, y-1), (x, y-1). In the tile array a
// step in x is 0x4800 bytes, in y 0x18 bytes. Tile byte +0xa is a terrain
// (or ground) type; 0x70 and 0xf0 are tested together below (0xf0 = 0x70
// with the top bit set), probably water. Tile byte +0x9 and the meaning of
// settler type 0x29 are not known; names are provisional.
//
// GameWorld::ReplaceTerrainType (0x00519210): (old, new) sets byte +0xa to
// `new` on every tile with x and y in [1, mapSize - 2] whose byte +0xa equals
// `old` (compared as a full 32-bit value). Several callers return right
// after it, so its EAX reaches their callers: it is always mapSize - 1.
// GameWorld::IsNeighbourhoodByte9Clear (0x00519860): (x, y) -> 1 if byte
// +0x9 is 0 on the tile and its six neighbours.
// GameWorld::HasAtMostTwoType70Neighbours (0x005198d0): (x, y) -> 1 if at
// most two of the six neighbours have terrain 0x70 or 0xf0.
// GameWorld::IsClearOfType70Nearby (0x00519960): (x, y) -> 1 if none of the
// tile, its six neighbours and (x, y+2), (x-1, y+1), (x+1, y+2) has terrain
// 0x70 (exactly).
// GameWorld::IsFreeOfSettlerType29Nearby (0x005199e0): (x, y) -> 0 if the
// tile has an occupant (word +0x4); else 0 if any of 18 offsets from the
// table at world+0x110a598 (int32 dx, dy; 16-byte entries) lands on the map
// on a tile whose occupant is a settler of type 0x29; else 1.
// All five: thiscall (ECX = world), ret 8.
//
// GameWorld::IsNeighbourhoodTerrain (0x0051b450): (x, y, terrain) -> 1 if
// byte +0xa is `terrain` (compared as int32) on the tile and its six
// neighbours, else 0. Order of the checks: tile, x+1, x-1, y+1, y-1,
// (x+1, y+1), (x-1, y-1). ret 0xc. Callers 0x51aed0, 0x51b010, 0x51b1a0
// (test EAX).
// GameWorld::AreOffsetTilesClear (0x0051b3c0): (x, y, count) -> 1 if for
// each of the first `count` (unsigned) entries of the offset table at
// world+0x110a588 (int32 dx, dy; 16-byte entries) the tile (x+dx, y+dy) has
// byte +0xb == 0 and bit 0x1 of byte +0xe clear; 0 otherwise. No bounds
// check. ret 0xc. Callers 0x51b010, 0x51b1a0 (test EAX).

#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

using world_offset::kMapRowBytes;
using world_offset::kMapRowTiles;
using world_offset::kMapTiles;
using world_offset::kTileBytes;

constexpr std::size_t kTileOccupant = 0x4;  // uint16
constexpr std::size_t kTileByte9 = 0x9;
constexpr std::size_t kTileTerrain = 0xa;
constexpr std::uint8_t kTerrain70 = 0x70;
constexpr std::uint8_t kTerrainF0 = 0xf0;

constexpr std::size_t kNearbyOffsets = 0x110a598;  // int32 dx, int32 dy, 16-byte stride
constexpr std::size_t kNearbyOffsetBytes = 0x10;
constexpr int kNearbyOffsetCount = 18;

constexpr std::size_t kTileByteB = 0xb;
constexpr std::size_t kTileFlagsE = 0xe;

constexpr std::size_t kOffsetTable = 0x110a588;  // int32 dx, int32 dy, 16-byte stride
constexpr std::size_t kOffsetTableBytes = 0x10;

constexpr std::size_t kSettlers = 0x12a5e60;
constexpr std::size_t kSettlerBytes = 0x40;
constexpr std::size_t kSettlerType = 0x1;
constexpr std::uint8_t kSettlerType29 = 0x29;

// Byte offset of tile (x, y) from the world base, with the original's 32-bit
// arithmetic.
std::uint32_t TileOffset(std::uint32_t x, std::uint32_t y) {
    return static_cast<std::uint32_t>(kMapTiles) + (x * kMapRowTiles + y) * kTileBytes;
}

std::uint8_t TileByte(GameWorld* world, std::uint32_t x, std::uint32_t y, std::size_t field) {
    return reinterpret_cast<const std::uint8_t*>(world)[TileOffset(x, y) + field];
}

std::uint16_t TileOccupant(GameWorld* world, std::uint32_t x, std::uint32_t y) {
    std::uint16_t value;
    std::memcpy(&value, reinterpret_cast<const std::uint8_t*>(world) + TileOffset(x, y) +
                            kTileOccupant,
                sizeof(value));
    return value;
}

// The six neighbours as (dx, dy).
constexpr int kNeighbours[6][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 0}, {-1, -1}, {0, -1}};

}  // namespace

using ReplaceTerrainFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx,
                                                    std::uint32_t oldType, std::uint32_t newType);
using TileCheckFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x,
                                               std::uint32_t y);
using TileValueCheckFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x,
                                                    std::uint32_t y, std::uint32_t value);

// Purpose: Changes every tile of one terrain type into another. Used by map generation and by GrowTerrain.
std::uint32_t __fastcall GameWorld_ReplaceTerrainType(GameWorld* world, void* /*edx*/,
                                                      std::uint32_t oldType,
                                                      std::uint32_t newType) {
    // Signed loop bounds, as in the original.
    const auto last = WorldField<std::int32_t>(world, world_offset::kMapSize) - 1;
    for (std::int32_t x = 1; x < last; ++x) {
        for (std::int32_t y = 1; y < last; ++y) {
            std::uint8_t& terrain = reinterpret_cast<std::uint8_t*>(
                world)[TileOffset(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)) +
                       kTileTerrain];
            if (terrain == oldType) {
                terrain = static_cast<std::uint8_t>(newType);
            }
        }
    }
    return static_cast<std::uint32_t>(last);
}

// Purpose: Whether a tile and its six neighbours all have a certain field clear (meaning unknown). Probably
// a building placement check.
std::uint32_t __fastcall GameWorld_IsNeighbourhoodByte9Clear(GameWorld* world, void* /*edx*/,
                                                             std::uint32_t x, std::uint32_t y) {
    if (TileByte(world, x, y, kTileByte9) != 0) {
        return 0;
    }
    for (const auto& d : kNeighbours) {
        if (TileByte(world, x + d[0], y + d[1], kTileByte9) != 0) {
            return 0;
        }
    }
    return 1;
}

// Purpose: Whether at most two of a tile's neighbours have a certain terrain (0x70/0xf0, probably mountain
// or rock). Probably a placement check.
std::uint32_t __fastcall GameWorld_HasAtMostTwoType70Neighbours(GameWorld* world, void* /*edx*/,
                                                                std::uint32_t x, std::uint32_t y) {
    int count = 0;
    for (const auto& d : kNeighbours) {
        const std::uint8_t terrain = TileByte(world, x + d[0], y + d[1], kTileTerrain);
        if (terrain == kTerrain70 || terrain == kTerrainF0) {
            ++count;
        }
    }
    return count <= 2 ? 1 : 0;
}

// Purpose: Whether no tile around a position has terrain 0x70 (probably mountain or rock). Probably a
// placement check.
std::uint32_t __fastcall GameWorld_IsClearOfType70Nearby(GameWorld* world, void* /*edx*/,
                                                         std::uint32_t x, std::uint32_t y) {
    static constexpr int kTiles[10][2] = {{0, 0},  {1, 0},  {-1, 0}, {0, 1},  {0, -1},
                                          {1, 1},  {-1, -1}, {0, 2}, {-1, 1}, {1, 2}};
    for (const auto& d : kTiles) {
        if (TileByte(world, x + d[0], y + d[1], kTileTerrain) == kTerrain70) {
            return 0;
        }
    }
    return 1;
}

// Purpose: Whether a tile and the tiles around it are free of a particular kind of settler (type 0x29,
// unknown). Probably a placement check.
std::uint32_t __fastcall GameWorld_IsFreeOfSettlerType29Nearby(GameWorld* world, void* /*edx*/,
                                                               std::uint32_t x, std::uint32_t y) {
    if (TileOccupant(world, x, y) != 0) {
        return 0;
    }
    const auto mapSize = WorldField<std::uint32_t>(world, world_offset::kMapSize);
    std::uint32_t result = 1;
    for (int i = 0; i < kNearbyOffsetCount; ++i) {
        const std::size_t entry = kNearbyOffsets + i * kNearbyOffsetBytes;
        const std::uint32_t nx = x + WorldField<std::uint32_t>(world, entry);
        const std::uint32_t ny = y + WorldField<std::uint32_t>(world, entry + 4);
        if (nx >= mapSize || ny >= mapSize) {
            continue;
        }
        const std::uint16_t occupant = TileOccupant(world, nx, ny);
        if (occupant != 0 &&
            WorldField<std::uint8_t>(world, kSettlers + occupant * kSettlerBytes + kSettlerType) ==
                kSettlerType29) {
            result = 0;
        }
    }
    return result;
}

// Purpose: Whether a tile and its six neighbours all have the same given terrain. Used by map generation to
// place things only in the middle of suitable terrain.
std::uint32_t __fastcall GameWorld_IsNeighbourhoodTerrain(GameWorld* world, void* /*edx*/,
                                                         std::uint32_t x, std::uint32_t y,
                                                         std::uint32_t terrain) {
    static constexpr int kTiles[7][2] = {{0, 0}, {1, 0},  {-1, 0}, {0, 1},
                                         {0, -1}, {1, 1}, {-1, -1}};
    for (const auto& d : kTiles) {
        if (TileByte(world, x + d[0], y + d[1], kTileTerrain) != terrain) {
            return 0;
        }
    }
    return 1;
}

// Purpose: Whether all tiles in a given radius around a position are free of objects and not blocked. Used
// by map generation before placing trees, stones and the like.
std::uint32_t __fastcall GameWorld_AreOffsetTilesClear(GameWorld* world, void* /*edx*/,
                                                       std::uint32_t x, std::uint32_t y,
                                                       std::uint32_t count) {
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t entry = kOffsetTable + i * kOffsetTableBytes;
        const std::uint32_t nx = x + WorldField<std::uint32_t>(world, entry);
        const std::uint32_t ny = y + WorldField<std::uint32_t>(world, entry + 4);
        if (TileByte(world, nx, ny, kTileByteB) != 0 ||
            (TileByte(world, nx, ny, kTileFlagsE) & 1) != 0) {
            return 0;
        }
    }
    return 1;
}

S3RE_HOOK(0x0051b450, GameWorld_IsNeighbourhoodTerrain, TileValueCheckFn,
          Original_GameWorld_IsNeighbourhoodTerrain, GameWorld_IsNeighbourhoodTerrain);
S3RE_HOOK(0x0051b3c0, GameWorld_AreOffsetTilesClear, TileValueCheckFn,
          Original_GameWorld_AreOffsetTilesClear, GameWorld_AreOffsetTilesClear);
S3RE_HOOK(0x00519210, GameWorld_ReplaceTerrainType, ReplaceTerrainFn,
          Original_GameWorld_ReplaceTerrainType, GameWorld_ReplaceTerrainType);
S3RE_HOOK(0x00519860, GameWorld_IsNeighbourhoodByte9Clear, TileCheckFn,
          Original_GameWorld_IsNeighbourhoodByte9Clear, GameWorld_IsNeighbourhoodByte9Clear);
S3RE_HOOK(0x005198d0, GameWorld_HasAtMostTwoType70Neighbours, TileCheckFn,
          Original_GameWorld_HasAtMostTwoType70Neighbours,
          GameWorld_HasAtMostTwoType70Neighbours);
S3RE_HOOK(0x00519960, GameWorld_IsClearOfType70Nearby, TileCheckFn,
          Original_GameWorld_IsClearOfType70Nearby, GameWorld_IsClearOfType70Nearby);
S3RE_HOOK(0x005199e0, GameWorld_IsFreeOfSettlerType29Nearby, TileCheckFn,
          Original_GameWorld_IsFreeOfSettlerType29Nearby, GameWorld_IsFreeOfSettlerType29Nearby);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestMapSize = 384;
// Up to the last settler record a 16-bit occupant can name.
constexpr std::size_t kTestWorld = kSettlers + 0x10000 * kSettlerBytes;

// Sets a tile byte in both worlds.
void SetTileByte(test::WorldPair& worlds, std::uint32_t x, std::uint32_t y, std::size_t field,
                 std::uint8_t value) {
    worlds.Set<std::uint8_t>(TileOffset(x, y) + field, value);
}

// Biased random values for the fields the checks look at, in a 7x7 square
// around (x, y).
void RandomizeAround(test::Context& ctx, test::WorldPair& worlds, std::uint32_t x,
                     std::uint32_t y) {
    for (int dx = -3; dx <= 3; ++dx) {
        for (int dy = -3; dy <= 3; ++dy) {
            const std::uint32_t tx = x + dx;
            const std::uint32_t ty = y + dy;
            if (tx >= kTestMapSize || ty >= kTestMapSize) {
                continue;
            }
            SetTileByte(worlds, tx, ty, kTileByte9,
                        ctx.rng().Chance(7, 8) ? 0 : static_cast<std::uint8_t>(ctx.rng().Next32()));
            static constexpr std::uint8_t kTerrains[] = {0x70, 0xf0, 0x10, 0x30, 0x71, 0x00};
            SetTileByte(worlds, tx, ty, kTileTerrain,
                        ctx.rng().Chance(1, 4) ? kTerrains[ctx.rng().Below(6)]
                                               : static_cast<std::uint8_t>(ctx.rng().Next32()));
            const std::uint16_t occupant =
                ctx.rng().Chance(1, 2) ? 0 : static_cast<std::uint16_t>(ctx.rng().Next32());
            worlds.Set<std::uint16_t>(TileOffset(tx, ty) + kTileOccupant, occupant);
            if (occupant != 0) {
                worlds.Set<std::uint8_t>(kSettlers + occupant * kSettlerBytes + kSettlerType,
                                         ctx.rng().Chance(1, 3)
                                             ? kSettlerType29
                                             : static_cast<std::uint8_t>(ctx.rng().Below(64)));
            }
        }
    }
}

void CompareTileCheck(test::Context& ctx, TileCheckFn original, TileCheckFn candidate,
                      int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    worlds.Set<std::uint32_t>(world_offset::kMapSize, kTestMapSize);
    for (int i = 0; i < cases; ++i) {
        // Tiles at least 2 from the edge (the checks read up to 2 away);
        // some near the edge for the bounds-checked offset table.
        const std::uint32_t x = 2 + ctx.rng().Below(kTestMapSize - 4);
        const std::uint32_t y = 2 + ctx.rng().Below(kTestMapSize - 4);
        RandomizeAround(ctx, worlds, x, y);
        if (i % 64 == 0) {  // new offset table now and then
            for (int k = 0; k < kNearbyOffsetCount; ++k) {
                const std::size_t entry = kNearbyOffsets + k * kNearbyOffsetBytes;
                worlds.Set<std::int32_t>(entry, ctx.rng().Chance(1, 20)
                                                    ? static_cast<std::int32_t>(ctx.rng().Next32())
                                                    : ctx.rng().Range(-3, 3));
                worlds.Set<std::int32_t>(entry + 4, ctx.rng().Range(-3, 3));
            }
        }
        const std::uint32_t expected = original(worlds.original(), nullptr, x, y);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, x, y);
        if (!S3RE_EXPECT_EQ(expected, actual) || (i % 2048 == 2047 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%u, %u)", i, x, y);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

void CompareReplaceTerrain(test::Context& ctx, ReplaceTerrainFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        // Small maps keep this fast; random terrain from a small set so
        // that matches are common.
        const std::int32_t mapSize = i == 0 ? 0 : ctx.rng().Range(-1, 40);
        const std::size_t rows = mapSize > 0 ? static_cast<std::size_t>(mapSize) : 0;
        test::WorldPair worlds(ctx, kMapTiles + (rows + 1) * kMapRowBytes);
        worlds.Set<std::int32_t>(world_offset::kMapSize, mapSize);
        for (std::size_t x = 0; x < rows; ++x) {
            for (std::size_t y = 0; y < rows; ++y) {
                SetTileByte(worlds, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                            kTileTerrain, static_cast<std::uint8_t>(ctx.rng().Below(4)));
            }
        }
        // `old` sometimes only matches in its low byte (must not count).
        std::uint32_t oldType = ctx.rng().Below(4);
        if (ctx.rng().Chance(1, 4)) {
            oldType |= 0x100;
        }
        const std::uint32_t newType = ctx.rng().Next32();
        const std::uint32_t expected =
            Original_GameWorld_ReplaceTerrainType(worlds.original(), nullptr, oldType, newType);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, oldType, newType);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: mapSize %d, old 0x%x, new 0x%x", i, mapSize,
                     oldType, newType);
            return;
        }
    }
}

// Wrong: also replaces on the outer edge.
std::uint32_t __fastcall WrongReplaceTerrain(GameWorld* world, void* /*edx*/,
                                             std::uint32_t oldType, std::uint32_t newType) {
    const auto size = WorldField<std::int32_t>(world, world_offset::kMapSize);
    for (std::int32_t x = 0; x < size; ++x) {
        for (std::int32_t y = 0; y < size; ++y) {
            std::uint8_t& terrain = reinterpret_cast<std::uint8_t*>(
                world)[TileOffset(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)) +
                       kTileTerrain];
            if (terrain == oldType) {
                terrain = static_cast<std::uint8_t>(newType);
            }
        }
    }
    return static_cast<std::uint32_t>(size - 1);
}

// Wrong: counts only 0x70, not 0xf0.
std::uint32_t __fastcall WrongAtMostTwo(GameWorld* world, void* /*edx*/, std::uint32_t x,
                                        std::uint32_t y) {
    int count = 0;
    for (const auto& d : kNeighbours) {
        count += TileByte(world, x + d[0], y + d[1], kTileTerrain) == kTerrain70 ? 1 : 0;
    }
    return count <= 2 ? 1 : 0;
}

// Wrong: misses the occupant check of the tile itself.
std::uint32_t __fastcall WrongFreeOf29(GameWorld* world, void* edx, std::uint32_t x,
                                       std::uint32_t y) {
    const std::size_t occupant = TileOffset(x, y) + kTileOccupant;
    std::uint16_t saved;
    std::memcpy(&saved, reinterpret_cast<std::uint8_t*>(world) + occupant, 2);
    std::memset(reinterpret_cast<std::uint8_t*>(world) + occupant, 0, 2);
    const std::uint32_t result = GameWorld_IsFreeOfSettlerType29Nearby(world, edx, x, y);
    std::memcpy(reinterpret_cast<std::uint8_t*>(world) + occupant, &saved, 2);
    return result;
}

// IsNeighbourhoodTerrain and AreOffsetTilesClear: a few terrain values so
// that all-equal neighbourhoods are common; byte +0xb and flag 0x1 mostly
// clear; a new offset table (small offsets, count up to 20) now and then.
void CompareTileValueCheck(test::Context& ctx, TileValueCheckFn original,
                           TileValueCheckFn candidate, bool countArgument, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t x = 4 + ctx.rng().Below(kTestMapSize - 8);
        const std::uint32_t y = 4 + ctx.rng().Below(kTestMapSize - 8);
        const std::uint8_t common = static_cast<std::uint8_t>(ctx.rng().Below(4));
        const bool uniform = ctx.rng().Chance(1, 2);
        for (int dx = -3; dx <= 3; ++dx) {
            for (int dy = -3; dy <= 3; ++dy) {
                SetTileByte(worlds, x + dx, y + dy, kTileTerrain,
                            uniform && ctx.rng().Chance(15, 16)
                                ? common
                                : static_cast<std::uint8_t>(ctx.rng().Below(4)));
                SetTileByte(worlds, x + dx, y + dy, kTileByteB,
                            ctx.rng().Chance(15, 16) ? 0 : static_cast<std::uint8_t>(ctx.rng().Next32()));
                SetTileByte(worlds, x + dx, y + dy, kTileFlagsE,
                            static_cast<std::uint8_t>(ctx.rng().Next32() &
                                                      (ctx.rng().Chance(15, 16) ? 0xfe : 0xff)));
            }
        }
        if (i % 64 == 0) {
            for (int k = 0; k < 20; ++k) {
                const std::size_t entry = kOffsetTable + k * kOffsetTableBytes;
                worlds.Set<std::int32_t>(entry, ctx.rng().Range(-3, 3));
                worlds.Set<std::int32_t>(entry + 4, ctx.rng().Range(-3, 3));
            }
        }
        const std::uint32_t value = countArgument
                                        ? (ctx.rng().Chance(1, 8) ? 0 : ctx.rng().Below(21))
                                        : (ctx.rng().Chance(1, 16) ? ctx.rng().Next32() : common);
        const std::uint32_t expected = original(worlds.original(), nullptr, x, y, value);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, x, y, value);
        if (!S3RE_EXPECT_EQ(expected, actual) || (i % 2048 == 2047 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%u, %u, %#x)", i, x, y, value);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

// Wrong: ignores flag 0x1 of byte +0xe.
std::uint32_t __fastcall WrongOffsetTilesClear(GameWorld* world, void* /*edx*/, std::uint32_t x,
                                               std::uint32_t y, std::uint32_t count) {
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t entry = kOffsetTable + i * kOffsetTableBytes;
        if (TileByte(world, x + WorldField<std::uint32_t>(world, entry),
                     y + WorldField<std::uint32_t>(world, entry + 4), kTileByteB) != 0) {
            return 0;
        }
    }
    return 1;
}

}  // namespace

S3RE_TEST(GameWorld_IsNeighbourhoodTerrain_MatchesOriginal) {
    CompareTileValueCheck(ctx, Original_GameWorld_IsNeighbourhoodTerrain,
                          &GameWorld_IsNeighbourhoodTerrain, false, 50000);
}

S3RE_TEST(GameWorld_AreOffsetTilesClear_MatchesOriginal) {
    CompareTileValueCheck(ctx, Original_GameWorld_AreOffsetTilesClear,
                          &GameWorld_AreOffsetTilesClear, true, 50000);
}

S3RE_TEST(GameWorld_AreOffsetTilesClear_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareTileValueCheck(probe, Original_GameWorld_AreOffsetTilesClear, &WrongOffsetTilesClear,
                          true, 5000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_ReplaceTerrainType_MatchesOriginal) {
    CompareReplaceTerrain(ctx, &GameWorld_ReplaceTerrainType, 200);
}

S3RE_TEST(GameWorld_ReplaceTerrainType_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareReplaceTerrain(probe, &WrongReplaceTerrain, 20);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_IsNeighbourhoodByte9Clear_MatchesOriginal) {
    CompareTileCheck(ctx, Original_GameWorld_IsNeighbourhoodByte9Clear,
                     &GameWorld_IsNeighbourhoodByte9Clear, 50000);
}

S3RE_TEST(GameWorld_HasAtMostTwoType70Neighbours_MatchesOriginal) {
    CompareTileCheck(ctx, Original_GameWorld_HasAtMostTwoType70Neighbours,
                     &GameWorld_HasAtMostTwoType70Neighbours, 50000);
}

S3RE_TEST(GameWorld_HasAtMostTwoType70Neighbours_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareTileCheck(probe, Original_GameWorld_HasAtMostTwoType70Neighbours, &WrongAtMostTwo,
                     5000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_IsClearOfType70Nearby_MatchesOriginal) {
    CompareTileCheck(ctx, Original_GameWorld_IsClearOfType70Nearby,
                     &GameWorld_IsClearOfType70Nearby, 50000);
}

S3RE_TEST(GameWorld_IsFreeOfSettlerType29Nearby_MatchesOriginal) {
    CompareTileCheck(ctx, Original_GameWorld_IsFreeOfSettlerType29Nearby,
                     &GameWorld_IsFreeOfSettlerType29Nearby, 50000);
}

S3RE_TEST(GameWorld_IsFreeOfSettlerType29Nearby_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareTileCheck(probe, Original_GameWorld_IsFreeOfSettlerType29Nearby, &WrongFreeOf29, 5000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
