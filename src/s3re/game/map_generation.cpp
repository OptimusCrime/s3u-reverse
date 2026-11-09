// Random scattering of map features, used by GameWorld::Create (0x5166d0)
// when it builds a world (each is called several times there, 0x51b010 94
// times). All draw from GameWorld::Random, so the order of the draws below
// is part of the behaviour.
//
// Shared pieces (int32 arithmetic as in the original):
//   blocks   = mapSize / 64 (signed, truncated toward zero)
//   coord(r) = ((Random() & 0xffff) * r) / 65536 (32-bit product, signed
//              division truncated toward zero): a random coordinate in
//              [0, r) for a sane r
//   offsets  = the table at world+0x110a588 (int32 dx, dy; 16-byte stride)
// Tile fields (tile (x, y) at world+0x67444 + (x*0x300 + y)*24): +0x6
// terrain, +0x7 an object/resource byte, +0xa flags (bit 0 = blocked), +0x11
// another resource byte.
//
// GameWorld::ScatterTileByte11 (0x0051ad40): (base, density): rounds while
//   round < (blocks*blocks*density) >> 3 (unsigned). Each round draws a
//   centre (x = coord(mapSize), y = coord(mapSize)), a count k = (Random &
//   0x3f) + 32 and a threshold t = (Random & 0x7fff) + 0x8000; then for the
//   first k offsets, a tile strictly inside the map (signed 1 <= x, y <
//   mapSize-1) whose terrain & 0xf0 is 0x20 or 0x80 and whose byte +0x11 is
//   0 gets, if (Random & 0xffff) < t, byte +0x11 = (Random & 0xffff) % 15 +
//   base + 1 (low byte). ret 8.
// GameWorld::ScatterTerrainNear (0x0051aed0): (required, terrain, density):
//   rounds while round < (blocks*blocks*density) / 8 (signed, truncated).
//   Each round draws a centre as above, then 8 times a tile (x + (Random &
//   0x1f) - 16, y + (Random & 0x1f) - 16): strictly inside the map, byte +0x7
//   == 0 and GameWorld::IsNeighbourhoodTerrain(tile, required) -> terrain
//   = `terrain` (low byte). ret 0xc.
// GameWorld::ScatterObjects (0x0051b010): (required, object, density,
//   blockMode, extra): rounds while round < (blocks*blocks*density) >> 1
//   (unsigned). Each round draws a tile (coord(mapSize-2) + 1, twice); if
//   IsNeighbourhoodTerrain(tile, required) and GameWorld::AreOffsetTilesClear
//   (tile, blockMode + extra == 3 ? 19 : 7): byte +0x7 = object (low byte),
//   and with blockMode 1 the tile, with blockMode 2 the tile and its six
//   neighbours, get flag bit 0 of +0xa. ret 0x14.
// GameWorld::ScatterObjectGroups (0x0051b1a0): (required, low, high,
//   density, blockMode, extra): rounds while round < (blocks*blocks*density)
//   >> 4 (unsigned). Each round draws a centre (coord(mapSize-2) + 1, twice)
//   and a count k = (Random & 0xf) + 4; then k times a random offset entry
//   (Random & 0x1ff) relative to the centre; a tile on the map (signed 0 <=
//   x, y < mapSize) that passes the same two checks as in ScatterObjects
//   gets byte +0x7 = low + ((Random & 0xffff) * (high - low + 1)) >> 16
//   (unsigned shift, low byte) and the same blocking. ret 0x18.
// All thiscall (ECX = world); callers ignore EAX.

#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <initializer_list>

namespace s3re::game {

namespace {

constexpr std::uint32_t kTiles = 0x67444;
constexpr std::uint32_t kTileTerrain = 0x6;
constexpr std::uint32_t kTileObject = 0x7;
constexpr std::uint32_t kTileFlags = 0xa;
constexpr std::uint32_t kTileByte11 = 0x11;
constexpr std::uint8_t kTileBlocked = 0x01;
constexpr std::uint32_t kOffsetTable = 0x110a588;  // int32 dx, int32 dy, 16-byte stride
constexpr std::uint32_t kOffsetTableBytes = 0x10;

constexpr std::int32_t kRow = 0x300 * 24;  // x + 1
constexpr std::int32_t kColumn = 24;       // y + 1

template <typename T>
T& At(GameWorld* world, std::uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<std::uint8_t*>(world) + offset);
}

std::uint32_t Tile(std::int32_t x, std::int32_t y) {
    return kTiles + (static_cast<std::uint32_t>(x) * 0x300 + static_cast<std::uint32_t>(y)) * 24;
}

std::int32_t MapSize(GameWorld* world) {
    return static_cast<std::int32_t>(At<std::uint32_t>(world, world_offset::kMapSize));
}

// (blocks * blocks * density) as the original's 32-bit imul chain.
std::uint32_t BlockArea(GameWorld* world, std::uint32_t density) {
    const std::int32_t blocks = MapSize(world) / 64;
    return static_cast<std::uint32_t>(blocks) * static_cast<std::uint32_t>(blocks) * density;
}

std::uint32_t Random16(GameWorld* world) {
    return GameWorld_Random(world, nullptr) & 0xffffu;
}

// ((Random & 0xffff) * range) / 65536, signed 32-bit, truncated toward zero.
std::int32_t RandomCoordinate(GameWorld* world, std::int32_t range) {
    const auto product = static_cast<std::int32_t>(Random16(world) * static_cast<std::uint32_t>(range));
    return product / 65536;
}

bool StrictlyInside(GameWorld* world, std::int32_t x, std::int32_t y) {
    const std::int32_t last = MapSize(world) - 1;
    return x >= 1 && x < last && y >= 1 && y < last;
}

std::int32_t OffsetX(GameWorld* world, std::uint32_t entry) {
    return At<std::int32_t>(world, kOffsetTable + entry * kOffsetTableBytes);
}
std::int32_t OffsetY(GameWorld* world, std::uint32_t entry) {
    return At<std::int32_t>(world, kOffsetTable + entry * kOffsetTableBytes + 4);
}

// The tile check shared by ScatterObjects and ScatterObjectGroups.
bool CanPlaceObject(GameWorld* world, std::int32_t x, std::int32_t y, std::uint32_t required,
                    std::uint32_t blockMode, std::uint32_t extra) {
    const auto ux = static_cast<std::uint32_t>(x);
    const auto uy = static_cast<std::uint32_t>(y);
    return GameWorld_IsNeighbourhoodTerrain(world, nullptr, ux, uy, required) != 0 &&
           GameWorld_AreOffsetTilesClear(world, nullptr, ux, uy, blockMode + extra == 3 ? 19 : 7) != 0;
}

void BlockTiles(GameWorld* world, std::uint32_t tile, std::uint32_t blockMode) {
    if (blockMode == 1) {
        At<std::uint8_t>(world, tile + kTileFlags) |= kTileBlocked;
    } else if (blockMode == 2) {
        for (const std::int32_t step : {0, kRow, -kRow, kColumn, -kColumn, kRow + kColumn, -kRow - kColumn}) {
            At<std::uint8_t>(world, tile + step + kTileFlags) |= kTileBlocked;
        }
    }
}

}  // namespace

using Scatter2Fn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t a, std::uint32_t b);
using Scatter3Fn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t a, std::uint32_t b,
                                              std::uint32_t c);
using Scatter5Fn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t a, std::uint32_t b,
                                              std::uint32_t c, std::uint32_t d, std::uint32_t e);
using Scatter6Fn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t a, std::uint32_t b,
                                              std::uint32_t c, std::uint32_t d, std::uint32_t e, std::uint32_t f);

// Purpose: Map generation: scatters clusters of a resource value over suitable land (probably the hidden
// resources a geologist finds: coal, iron, gold, stone).
std::uint32_t __fastcall GameWorld_ScatterTileByte11(GameWorld* world, void*, std::uint32_t base,
                                                     std::uint32_t density) {
    for (std::uint32_t round = 0; round < BlockArea(world, density) >> 3; ++round) {
        const std::int32_t x = RandomCoordinate(world, MapSize(world));
        const std::int32_t y = RandomCoordinate(world, MapSize(world));
        const std::uint32_t count = (GameWorld_Random(world, nullptr) & 0x3fu) + 0x20u;
        const std::uint32_t threshold = (GameWorld_Random(world, nullptr) & 0x7fffu) + 0x8000u;
        for (std::uint32_t entry = 0; entry < count; ++entry) {
            const std::int32_t tx = OffsetX(world, entry) + x;
            const std::int32_t ty = OffsetY(world, entry) + y;
            if (!StrictlyInside(world, tx, ty)) {
                continue;
            }
            const std::uint32_t tile = Tile(tx, ty);
            const std::uint8_t kind = At<std::uint8_t>(world, tile + kTileTerrain) & 0xf0;
            if ((kind != 0x20 && kind != 0x80) || At<std::uint8_t>(world, tile + kTileByte11) != 0) {
                continue;
            }
            if (Random16(world) < threshold) {
                At<std::uint8_t>(world, tile + kTileByte11) =
                    static_cast<std::uint8_t>(Random16(world) % 15 + base + 1);
            }
        }
    }
    return 0;
}

// Purpose: Map generation: sprinkles patches of a terrain type into another terrain, to vary the landscape.
std::uint32_t __fastcall GameWorld_ScatterTerrainNear(GameWorld* world, void*, std::uint32_t required,
                                                      std::uint32_t terrain, std::uint32_t density) {
    auto rounds = [&] { return static_cast<std::int32_t>(BlockArea(world, density)) / 8; };
    if (rounds() <= 0) {
        return 0;
    }
    std::int32_t round = 0;
    do {
        const std::int32_t x = RandomCoordinate(world, MapSize(world));
        const std::int32_t y = RandomCoordinate(world, MapSize(world));
        for (int i = 0; i < 8; ++i) {
            const std::int32_t tx = static_cast<std::int32_t>(GameWorld_Random(world, nullptr) & 0x1fu) + x - 16;
            const std::int32_t ty = static_cast<std::int32_t>(GameWorld_Random(world, nullptr) & 0x1fu) + y - 16;
            if (!StrictlyInside(world, tx, ty)) {
                continue;
            }
            const std::uint32_t tile = Tile(tx, ty);
            if (At<std::uint8_t>(world, tile + kTileObject) == 0 &&
                GameWorld_IsNeighbourhoodTerrain(world, nullptr, static_cast<std::uint32_t>(tx),
                                                 static_cast<std::uint32_t>(ty), required) != 0) {
                At<std::uint8_t>(world, tile + kTileTerrain) = static_cast<std::uint8_t>(terrain);
            }
        }
        ++round;
    } while (round < rounds());
    return 0;
}

// Purpose: Map generation: places single objects (probably trees, stones and decorations) on random
// suitable tiles, optionally making them block movement.
std::uint32_t __fastcall GameWorld_ScatterObjects(GameWorld* world, void*, std::uint32_t required,
                                                  std::uint32_t object, std::uint32_t density,
                                                  std::uint32_t blockMode, std::uint32_t extra) {
    for (std::uint32_t round = 0; round < BlockArea(world, density) >> 1; ++round) {
        const std::int32_t x = RandomCoordinate(world, MapSize(world) - 2) + 1;
        const std::int32_t y = RandomCoordinate(world, MapSize(world) - 2) + 1;
        if (!CanPlaceObject(world, x, y, required, blockMode, extra)) {
            continue;
        }
        const std::uint32_t tile = Tile(x, y);
        At<std::uint8_t>(world, tile + kTileObject) = static_cast<std::uint8_t>(object);
        BlockTiles(world, tile, blockMode);
    }
    return 0;
}

// Purpose: Map generation: places groups of objects around random centres (probably forests or rock
// fields), each object picked from a range of types.
std::uint32_t __fastcall GameWorld_ScatterObjectGroups(GameWorld* world, void*, std::uint32_t required,
                                                       std::uint32_t low, std::uint32_t high, std::uint32_t density,
                                                       std::uint32_t blockMode, std::uint32_t extra) {
    for (std::uint32_t round = 0; round < BlockArea(world, density) >> 4; ++round) {
        const std::int32_t x = RandomCoordinate(world, MapSize(world) - 2) + 1;
        const std::int32_t y = RandomCoordinate(world, MapSize(world) - 2) + 1;
        const std::uint32_t count = (GameWorld_Random(world, nullptr) & 0xfu) + 4u;
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t entry = GameWorld_Random(world, nullptr) & 0x1ffu;
            const std::int32_t tx = OffsetX(world, entry) + x;
            const std::int32_t ty = OffsetY(world, entry) + y;
            const std::int32_t size = MapSize(world);
            if (tx < 0 || tx >= size || ty < 0 || ty >= size) {
                continue;
            }
            if (!CanPlaceObject(world, tx, ty, required, blockMode, extra)) {
                continue;
            }
            const std::uint32_t tile = Tile(tx, ty);
            const std::uint32_t spread = (Random16(world) * (high - low + 1u)) >> 16;
            At<std::uint8_t>(world, tile + kTileObject) = static_cast<std::uint8_t>(spread + low);
            BlockTiles(world, tile, blockMode);
        }
    }
    return 0;
}

S3RE_HOOK(0x0051ad40, GameWorld_ScatterTileByte11, Scatter2Fn, Original_GameWorld_ScatterTileByte11,
          GameWorld_ScatterTileByte11);
S3RE_HOOK(0x0051aed0, GameWorld_ScatterTerrainNear, Scatter3Fn, Original_GameWorld_ScatterTerrainNear,
          GameWorld_ScatterTerrainNear);
S3RE_HOOK(0x0051b010, GameWorld_ScatterObjects, Scatter5Fn, Original_GameWorld_ScatterObjects,
          GameWorld_ScatterObjects);
S3RE_HOOK(0x0051b1a0, GameWorld_ScatterObjectGroups, Scatter6Fn, Original_GameWorld_ScatterObjectGroups,
          GameWorld_ScatterObjectGroups);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestOffsets = 512;
constexpr std::size_t kTestWorld = kOffsetTable + kTestOffsets * kOffsetTableBytes;

// A random landscape: few terrain types (so the neighbourhood checks pass
// often), mostly empty object and resource bytes, few blocked tiles; the
// offset table with small offsets; a random RNG state and map size.
void SetUpLandscape(test::Context& ctx, test::WorldPair& worlds) {
    const std::uint32_t size = ctx.rng().Chance(1, 2) ? 384 : ctx.rng().Below(400);
    worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
    static constexpr std::uint8_t kTerrains[] = {0x20, 0x21, 0x80, 0x10};
    const std::uint32_t limit = size < 384 ? size : 384;
    for (std::uint32_t x = 0; x < limit; ++x) {
        for (std::uint32_t y = 0; y < limit; ++y) {
            const std::uint32_t tile = Tile(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
            // Patches of 8 x 8 tiles of one terrain, some noise.
            const std::uint32_t patch = ((x >> 3) + (y >> 3) * 3) & 1;
            worlds.Set<std::uint8_t>(tile + kTileTerrain, kTerrains[ctx.rng().Chance(15, 16) ? patch : ctx.rng().Below(4)]);
            worlds.Set<std::uint8_t>(tile + kTileObject, static_cast<std::uint8_t>(ctx.rng().Chance(31, 32) ? 0 : 1));
            worlds.Set<std::uint8_t>(tile + kTileByte11, static_cast<std::uint8_t>(ctx.rng().Chance(3, 4) ? 0 : 5));
            worlds.Set<std::uint8_t>(tile + kTileFlags,
                                     static_cast<std::uint8_t>(ctx.rng().Next32() & (ctx.rng().Chance(31, 32) ? 0xfe : 0xff)));
        }
    }
    for (std::uint32_t entry = 0; entry < kTestOffsets; ++entry) {
        const std::int32_t reach = entry < 96 ? 5 : 12;
        worlds.Set<std::int32_t>(kOffsetTable + entry * kOffsetTableBytes, ctx.rng().Range(-reach, reach));
        worlds.Set<std::int32_t>(kOffsetTable + entry * kOffsetTableBytes + 4, ctx.rng().Range(-reach, reach));
    }
    worlds.Set<std::uint32_t>(world_offset::kRandomState, ctx.rng().Next32());
    worlds.Set<std::uint16_t>(world_offset::kRandomState + 4, static_cast<std::uint16_t>(ctx.rng().Next32()));
}

std::uint32_t Density(test::Context& ctx) {
    return ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(12);
}

std::uint32_t Terrain(test::Context& ctx) {
    return ctx.rng().Chance(1, 4) ? 0x21 : 0x20;
}

template <typename Call>
void CompareScatter(test::Context& ctx, int cases, Call call) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        SetUpLandscape(ctx, worlds);
        call(worlds);
        if (!worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

// Wrong: always uses the 7-entry clearance check.
std::uint32_t __fastcall WrongScatterObjects(GameWorld* world, void* edx, std::uint32_t required,
                                             std::uint32_t object, std::uint32_t density, std::uint32_t blockMode,
                                             std::uint32_t extra) {
    return GameWorld_ScatterObjects(world, edx, required, object, density, blockMode,
                                    blockMode + extra == 3 ? extra + 1 : extra);
}

void CompareScatterObjects(test::Context& ctx, Scatter5Fn candidate, int cases) {
    CompareScatter(ctx, cases, [&](test::WorldPair& worlds) {
        const std::uint32_t required = Terrain(ctx);
        const std::uint32_t object = ctx.rng().Next32();
        const std::uint32_t density = Density(ctx) & 0xfff;
        const std::uint32_t blockMode = ctx.rng().Below(4);
        const std::uint32_t extra = ctx.rng().Chance(1, 2) ? 3 - blockMode : ctx.rng().Below(4);
        Original_GameWorld_ScatterObjects(worlds.original(), nullptr, required, object, density, blockMode, extra);
        candidate(worlds.ours(), nullptr, required, object, density, blockMode, extra);
    });
}

}  // namespace

S3RE_TEST(GameWorld_ScatterTileByte11_MatchesOriginal) {
    CompareScatter(ctx, 40, [&](test::WorldPair& worlds) {
        const std::uint32_t base = ctx.rng().Next32();
        const std::uint32_t density = Density(ctx) & 0xfff;
        Original_GameWorld_ScatterTileByte11(worlds.original(), nullptr, base, density);
        GameWorld_ScatterTileByte11(worlds.ours(), nullptr, base, density);
    });
}

S3RE_TEST(GameWorld_ScatterTerrainNear_MatchesOriginal) {
    CompareScatter(ctx, 40, [&](test::WorldPair& worlds) {
        const std::uint32_t required = Terrain(ctx);
        const std::uint32_t terrain = ctx.rng().Next32();
        // Sometimes negative: no rounds (signed count).
        const std::uint32_t density = ctx.rng().Chance(1, 8) ? 0u - ctx.rng().Below(100) : ctx.rng().Below(40);
        Original_GameWorld_ScatterTerrainNear(worlds.original(), nullptr, required, terrain, density);
        GameWorld_ScatterTerrainNear(worlds.ours(), nullptr, required, terrain, density);
    });
}

S3RE_TEST(GameWorld_ScatterObjects_MatchesOriginal) {
    CompareScatterObjects(ctx, &GameWorld_ScatterObjects, 40);
}
S3RE_TEST(GameWorld_ScatterObjects_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareScatterObjects(probe, &WrongScatterObjects, 20);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_ScatterObjectGroups_MatchesOriginal) {
    CompareScatter(ctx, 40, [&](test::WorldPair& worlds) {
        const std::uint32_t required = Terrain(ctx);
        const std::uint32_t low = ctx.rng().Chance(1, 4) ? ctx.rng().Next32() : ctx.rng().Below(8);
        const std::uint32_t high = ctx.rng().Chance(1, 4) ? ctx.rng().Next32() : low + ctx.rng().Below(8);
        const std::uint32_t density = Density(ctx) & 0xfff;
        const std::uint32_t blockMode = ctx.rng().Below(4);
        const std::uint32_t extra = ctx.rng().Chance(1, 2) ? 3 - blockMode : ctx.rng().Below(4);
        Original_GameWorld_ScatterObjectGroups(worlds.original(), nullptr, required, low, high, density, blockMode,
                                               extra);
        GameWorld_ScatterObjectGroups(worlds.ours(), nullptr, required, low, high, density, blockMode, extra);
    });
}

// ---------------------------------------------------------------------------
// GameWorld::ReplaceTerrainNextTo (0x00519270): (terrain, neighbour,
// replacement): for x = 1 .. mapSize-2, inner y = 1 .. mapSize-2 (signed
// compares; mapSize at world+0x2d2d0 re-read per step), a tile whose
// terrain byte +0x6 equals `terrain` (zero-extended, 32-bit compare) and
// that has one of the six neighbours (x+1, y), (x-1, y), (x, y-1),
// (x, y+1), (x+1, y+1), (x-1, y-1) with terrain `neighbour` gets terrain
// `replacement` (low byte). Works in place, so a replaced tile can make the
// next one match. Called four times by GameWorld::Create (map generation,
// probably to put transition terrain such as beaches along borders).
// thiscall, ret 0xc; EAX is clobbered by every caller (void here).
// ---------------------------------------------------------------------------

using ReplaceNextToFn = void(__fastcall*)(GameWorld* world, void* edx, std::uint32_t terrain, std::uint32_t neighbour,
                                          std::uint32_t replacement);

// Purpose: Map generation: turns tiles of one terrain into another where they border a third (probably to
// add transition terrain, e.g. shore along water).
void __fastcall GameWorld_ReplaceTerrainNextTo(GameWorld* world, void*, std::uint32_t terrain, std::uint32_t neighbour,
                                               std::uint32_t replacement) {
    const std::int32_t& mapSize = At<std::int32_t>(world, 0x2d2d0);
    for (std::int32_t x = 1; x < mapSize - 1; ++x) {
        for (std::int32_t y = 1; y < mapSize - 1; ++y) {
            const std::uint32_t tile = Tile(x, y) + kTileTerrain;
            if (At<std::uint8_t>(world, tile) != terrain) {
                continue;
            }
            static constexpr std::int32_t kNeighbours[] = {kRow, -kRow, -kColumn, kColumn, kRow + kColumn, -kRow - kColumn};
            for (const std::int32_t step : kNeighbours) {
                if (At<std::uint8_t>(world, tile + static_cast<std::uint32_t>(step)) == neighbour) {
                    At<std::uint8_t>(world, tile) = static_cast<std::uint8_t>(replacement);
                    break;
                }
            }
        }
    }
}

S3RE_HOOK(0x00519270, GameWorld_ReplaceTerrainNextTo, ReplaceNextToFn, Original_GameWorld_ReplaceTerrainNextTo,
          GameWorld_ReplaceTerrainNextTo);

namespace {

// Small maps (sometimes 0..3 or negative sizes) with terrains 0..3 (so
// matches and chains are common); arguments mostly 0..3, sometimes with
// high bits (must never match).
void CompareReplaceNextTo(test::Context& ctx, ReplaceNextToFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::int32_t n = ctx.rng().Chance(1, 10) ? static_cast<std::int32_t>(ctx.rng().Below(8)) - 4
                                                       : static_cast<std::int32_t>(4 + ctx.rng().Below(30));
        worlds.Set<std::int32_t>(0x2d2d0, n);
        for (std::int32_t x = 0; x < 36; ++x) {
            for (std::int32_t y = 0; y < 36; ++y) {
                worlds.Set<std::uint8_t>(Tile(x, y) + kTileTerrain, static_cast<std::uint8_t>(ctx.rng().Below(4)));
            }
        }
        auto argument = [&]() { return ctx.rng().Chance(1, 10) ? ctx.rng().Next32() : ctx.rng().Below(4); };
        const std::uint32_t terrain = argument();
        const std::uint32_t neighbour = argument();
        const std::uint32_t replacement = ctx.rng().Chance(1, 4) ? ctx.rng().Next32() : ctx.rng().Below(4);
        Original_GameWorld_ReplaceTerrainNextTo(worlds.original(), nullptr, terrain, neighbour, replacement);
        candidate(worlds.ours(), nullptr, terrain, neighbour, replacement);
        if (!worlds.Same(ctx, kTiles, Tile(36, 0) - kTiles) || (i % 64 == 63 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: size %d, (%u, %u, %u)", i, n, terrain, neighbour, replacement);
            return;
        }
    }
}

// Wrong: decides on the terrains before any replacement (not in place).
void __fastcall WrongReplaceTerrainNextTo(GameWorld* world, void*, std::uint32_t terrain, std::uint32_t neighbour,
                                          std::uint32_t replacement) {
    const std::int32_t mapSize = At<std::int32_t>(world, 0x2d2d0);
    static std::uint8_t copy[40][40];
    for (std::int32_t x = 0; x < 40; ++x) {
        for (std::int32_t y = 0; y < 40; ++y) {
            copy[x][y] = At<std::uint8_t>(world, Tile(x, y) + kTileTerrain);
        }
    }
    for (std::int32_t x = 1; x < mapSize - 1 && x < 39; ++x) {
        for (std::int32_t y = 1; y < mapSize - 1 && y < 39; ++y) {
            if (copy[x][y] == terrain && (copy[x + 1][y] == neighbour || copy[x - 1][y] == neighbour ||
                                          copy[x][y - 1] == neighbour || copy[x][y + 1] == neighbour ||
                                          copy[x + 1][y + 1] == neighbour || copy[x - 1][y - 1] == neighbour)) {
                At<std::uint8_t>(world, Tile(x, y) + kTileTerrain) = static_cast<std::uint8_t>(replacement);
            }
        }
    }
}

}  // namespace

S3RE_TEST(GameWorld_ReplaceTerrainNextTo_MatchesOriginal) {
    CompareReplaceNextTo(ctx, &GameWorld_ReplaceTerrainNextTo, 1000);
}
S3RE_TEST(GameWorld_ReplaceTerrainNextTo_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareReplaceNextTo(probe, &WrongReplaceTerrainNextTo, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
