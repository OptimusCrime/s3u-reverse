// Random scattering of map features, used by CGameData::GenerateRandomMap (0x5166d0)
// when it builds a world (each is called several times there, 0x51b010 94
// times). All draw from CGameData::Random, so the order of the draws below
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
// CGameData::ScatterTileByte11 (0x0051ad40): (base, density): rounds while
//   round < (blocks*blocks*density) >> 3 (unsigned). Each round draws a
//   centre (x = coord(mapSize), y = coord(mapSize)), a count k = (Random &
//   0x3f) + 32 and a threshold t = (Random & 0x7fff) + 0x8000; then for the
//   first k offsets, a tile strictly inside the map (signed 1 <= x, y <
//   mapSize-1) whose terrain & 0xf0 is 0x20 or 0x80 and whose byte +0x11 is
//   0 gets, if (Random & 0xffff) < t, byte +0x11 = (Random & 0xffff) % 15 +
//   base + 1 (low byte). ret 8.
// CGameData::ScatterTerrainNear (0x0051aed0): (required, terrain, density):
//   rounds while round < (blocks*blocks*density) / 8 (signed, truncated).
//   Each round draws a centre as above, then 8 times a tile (x + (Random &
//   0x1f) - 16, y + (Random & 0x1f) - 16): strictly inside the map, byte +0x7
//   == 0 and CGameData::IsNeighbourhoodTerrain(tile, required) -> terrain
//   = `terrain` (low byte). ret 0xc.
// CGameData::ScatterObjects (0x0051b010): (required, object, density,
//   blockMode, extra): rounds while round < (blocks*blocks*density) >> 1
//   (unsigned). Each round draws a tile (coord(mapSize-2) + 1, twice); if
//   IsNeighbourhoodTerrain(tile, required) and CGameData::AreOffsetTilesClear
//   (tile, blockMode + extra == 3 ? 19 : 7): byte +0x7 = object (low byte),
//   and with blockMode 1 the tile, with blockMode 2 the tile and its six
//   neighbours, get flag bit 0 of +0xa. ret 0x14.
// CGameData::ScatterObjectGroups (0x0051b1a0): (required, low, high,
//   density, blockMode, extra): rounds while round < (blocks*blocks*density)
//   >> 4 (unsigned). Each round draws a centre (coord(mapSize-2) + 1, twice)
//   and a count k = (Random & 0xf) + 4; then k times a random offset entry
//   (Random & 0x1ff) relative to the centre; a tile on the map (signed 0 <=
//   x, y < mapSize) that passes the same two checks as in ScatterObjects
//   gets byte +0x7 = low + ((Random & 0xffff) * (high - low + 1)) >> 16
//   (unsigned shift, low byte) and the same blocking. ret 0x18.
// All thiscall (ECX = world); callers ignore EAX.

#include "game/enums/terrain.h"
#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <initializer_list>

namespace s3re::game {

namespace {

using layout::kTileBlocked;
using layout::Tile;

// Tiles are carried as 32-bit byte offsets from the world (TileOffset) and
// stepped to their neighbours like in the original.
constexpr std::int32_t kRow = static_cast<std::int32_t>(kTileRowBytes);  // x + 1
constexpr std::int32_t kColumn = static_cast<std::int32_t>(kTileBytes);   // y + 1

// ScatterTileByte11: resources go into mountains and terrain class 0x80
// (unknown); each cluster covers 32..95 offset entries and fills a tile with
// a chance of (32768..65535) / 65536.
constexpr std::uint8_t kTerrainClass80 = 0x80;
constexpr std::uint32_t kMinClusterEntries = 32;
constexpr std::uint32_t kMinFillThreshold = 32768;
constexpr std::uint32_t kResourceAmounts = 15;  // amount 1..15 in the low nibble

std::uint32_t TileOffsetXY(std::int32_t x, std::int32_t y) {
    return TileOffset(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
}

Tile& TileAt(CGameData* world, std::uint32_t tile) {
    return TileAtOffset(world, tile);
}

std::int32_t MapSize(CGameData* world) {
    return static_cast<std::int32_t>(world->map_size);
}

// (blocks * blocks * density) as the original's 32-bit imul chain.
std::uint32_t BlockArea(CGameData* world, std::uint32_t density) {
    const std::int32_t blocks = MapSize(world) / 64;
    return static_cast<std::uint32_t>(blocks) * static_cast<std::uint32_t>(blocks) * density;
}

std::uint32_t Random16(CGameData* world) {
    return CGameData_Random(world, nullptr) & 0xffffu;
}

// ((Random & 0xffff) * range) / 65536, signed 32-bit, truncated toward zero.
std::int32_t RandomCoordinate(CGameData* world, std::int32_t range) {
    const auto product = static_cast<std::int32_t>(Random16(world) * static_cast<std::uint32_t>(range));
    return product / 65536;
}

bool StrictlyInside(CGameData* world, std::int32_t x, std::int32_t y) {
    const std::int32_t last = MapSize(world) - 1;
    return x >= 1 && x < last && y >= 1 && y < last;
}

std::int32_t OffsetX(CGameData* world, std::uint32_t entry) {
    return world->offsets[entry].dx;
}
std::int32_t OffsetY(CGameData* world, std::uint32_t entry) {
    return world->offsets[entry].dy;
}

// The tile check shared by ScatterObjects and ScatterObjectGroups.
bool CanPlaceObject(CGameData* world, std::int32_t x, std::int32_t y, std::uint32_t required,
                    std::uint32_t blockMode, std::uint32_t extra) {
    const auto ux = static_cast<std::uint32_t>(x);
    const auto uy = static_cast<std::uint32_t>(y);
    return CGameData_IsNeighbourhoodTerrain(world, nullptr, ux, uy, required) != 0 &&
           CGameData_AreOffsetTilesClear(world, nullptr, ux, uy, blockMode + extra == 3 ? 19 : 7) != 0;
}

void BlockTiles(CGameData* world, std::uint32_t tile, std::uint32_t blockMode) {
    if (blockMode == 1) {
        TileAt(world, tile).flags |= kTileBlocked;
    } else if (blockMode == 2) {
        for (const std::int32_t step : {0, kRow, -kRow, kColumn, -kColumn, kRow + kColumn, -kRow - kColumn}) {
            TileAt(world, tile + step).flags |= kTileBlocked;
        }
    }
}

}  // namespace

using Scatter2Fn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t a, std::uint32_t b);
using Scatter3Fn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t a, std::uint32_t b,
                                              std::uint32_t c);
using Scatter5Fn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t a, std::uint32_t b,
                                              std::uint32_t c, std::uint32_t d, std::uint32_t e);
using Scatter6Fn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t a, std::uint32_t b,
                                              std::uint32_t c, std::uint32_t d, std::uint32_t e, std::uint32_t f);

// Purpose: Map generation: scatters clusters of a resource value over suitable land (probably the hidden
// resources a geologist finds: coal, iron, gold, stone).
std::uint32_t __fastcall CGameData_ScatterTileByte11(CGameData* world, void*, std::uint32_t base,
                                                     std::uint32_t density) {
    for (std::uint32_t round = 0; round < BlockArea(world, density) >> 3; ++round) {
        const std::int32_t x = RandomCoordinate(world, MapSize(world));
        const std::int32_t y = RandomCoordinate(world, MapSize(world));
        const std::uint32_t count = (CGameData_Random(world, nullptr) & 0x3fu) + kMinClusterEntries;
        const std::uint32_t threshold = (CGameData_Random(world, nullptr) & 0x7fffu) + kMinFillThreshold;
        for (std::uint32_t entry = 0; entry < count; ++entry) {
            const std::int32_t tx = OffsetX(world, entry) + x;
            const std::int32_t ty = OffsetY(world, entry) + y;
            if (!StrictlyInside(world, tx, ty)) {
                continue;
            }
            const std::uint32_t tile = TileOffsetXY(tx, ty);
            const std::uint8_t terrainClass = TileAt(world, tile).terrain & kTerrainClassMask;
            if ((terrainClass != kTerrainClassMountain && terrainClass != kTerrainClass80) ||
                TileAt(world, tile).resources != 0) {
                continue;
            }
            if (Random16(world) < threshold) {
                TileAt(world, tile).resources =
                    static_cast<std::uint8_t>(Random16(world) % kResourceAmounts + base + 1);
            }
        }
    }
    return 0;
}

// Purpose: Map generation: sprinkles patches of a terrain type into another terrain, to vary the landscape.
std::uint32_t __fastcall CGameData_ScatterTerrainNear(CGameData* world, void*, std::uint32_t required,
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
            const std::int32_t tx = static_cast<std::int32_t>(CGameData_Random(world, nullptr) & 0x1fu) + x - 16;
            const std::int32_t ty = static_cast<std::int32_t>(CGameData_Random(world, nullptr) & 0x1fu) + y - 16;
            if (!StrictlyInside(world, tx, ty)) {
                continue;
            }
            const std::uint32_t tile = TileOffsetXY(tx, ty);
            if (TileAt(world, tile).object == 0 &&
                CGameData_IsNeighbourhoodTerrain(world, nullptr, static_cast<std::uint32_t>(tx),
                                                 static_cast<std::uint32_t>(ty), required) != 0) {
                TileAt(world, tile).terrain = static_cast<std::uint8_t>(terrain);
            }
        }
        ++round;
    } while (round < rounds());
    return 0;
}

// Purpose: Map generation: places single objects (probably trees, stones and decorations) on random
// suitable tiles, optionally making them block movement.
std::uint32_t __fastcall CGameData_ScatterObjects(CGameData* world, void*, std::uint32_t required,
                                                  std::uint32_t object, std::uint32_t density,
                                                  std::uint32_t blockMode, std::uint32_t extra) {
    for (std::uint32_t round = 0; round < BlockArea(world, density) >> 1; ++round) {
        const std::int32_t x = RandomCoordinate(world, MapSize(world) - 2) + 1;
        const std::int32_t y = RandomCoordinate(world, MapSize(world) - 2) + 1;
        if (!CanPlaceObject(world, x, y, required, blockMode, extra)) {
            continue;
        }
        const std::uint32_t tile = TileOffsetXY(x, y);
        TileAt(world, tile).object = static_cast<std::uint8_t>(object);
        BlockTiles(world, tile, blockMode);
    }
    return 0;
}

// Purpose: Map generation: places groups of objects around random centres (probably forests or rock
// fields), each object picked from a range of types.
std::uint32_t __fastcall CGameData_ScatterObjectGroups(CGameData* world, void*, std::uint32_t required,
                                                       std::uint32_t low, std::uint32_t high, std::uint32_t density,
                                                       std::uint32_t blockMode, std::uint32_t extra) {
    for (std::uint32_t round = 0; round < BlockArea(world, density) >> 4; ++round) {
        const std::int32_t x = RandomCoordinate(world, MapSize(world) - 2) + 1;
        const std::int32_t y = RandomCoordinate(world, MapSize(world) - 2) + 1;
        const std::uint32_t count = (CGameData_Random(world, nullptr) & 0xfu) + 4u;
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t entry = CGameData_Random(world, nullptr) & 0x1ffu;
            const std::int32_t tx = OffsetX(world, entry) + x;
            const std::int32_t ty = OffsetY(world, entry) + y;
            const std::int32_t size = MapSize(world);
            if (tx < 0 || tx >= size || ty < 0 || ty >= size) {
                continue;
            }
            if (!CanPlaceObject(world, tx, ty, required, blockMode, extra)) {
                continue;
            }
            const std::uint32_t tile = TileOffsetXY(tx, ty);
            const std::uint32_t spread = (Random16(world) * (high - low + 1u)) >> 16;
            TileAt(world, tile).object = static_cast<std::uint8_t>(spread + low);
            BlockTiles(world, tile, blockMode);
        }
    }
    return 0;
}

S3RE_HOOK(0x0051ad40, CGameData_ScatterTileByte11, Scatter2Fn, Original_CGameData_ScatterTileByte11,
          CGameData_ScatterTileByte11);
S3RE_HOOK(0x0051aed0, CGameData_ScatterTerrainNear, Scatter3Fn, Original_CGameData_ScatterTerrainNear,
          CGameData_ScatterTerrainNear);
S3RE_HOOK(0x0051b010, CGameData_ScatterObjects, Scatter5Fn, Original_CGameData_ScatterObjects,
          CGameData_ScatterObjects);
S3RE_HOOK(0x0051b1a0, CGameData_ScatterObjectGroups, Scatter6Fn, Original_CGameData_ScatterObjectGroups,
          CGameData_ScatterObjectGroups);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestOffsets = 512;
constexpr std::size_t kTestWorld = offsetof(CGameData, offsets) + kTestOffsets * sizeof(layout::MapOffset);
constexpr std::size_t kTilesOffset = offsetof(CGameData, tiles);

// A random landscape: few terrain types (so the neighbourhood checks pass
// often), mostly empty object and resource bytes, few blocked tiles; the
// offset table with small offsets; a random RNG state and map size.
void SetUpLandscape(test::Context& ctx, test::WorldPair& worlds) {
    const std::uint32_t size = ctx.rng().Chance(1, 2) ? 384 : ctx.rng().Below(400);
    worlds.Both([&](CGameData* w) { w->map_size = size; });
    static constexpr std::uint8_t kTerrains[] = {0x20, 0x21, 0x80, 0x10};
    const std::uint32_t limit = size < 384 ? size : 384;
    for (std::uint32_t x = 0; x < limit; ++x) {
        for (std::uint32_t y = 0; y < limit; ++y) {
            // Patches of 8 x 8 tiles of one terrain, some noise.
            const std::uint32_t patch = ((x >> 3) + (y >> 3) * 3) & 1;
            const std::uint8_t terrain = kTerrains[ctx.rng().Chance(15, 16) ? patch : ctx.rng().Below(4)];
            const auto object = static_cast<std::uint8_t>(ctx.rng().Chance(31, 32) ? 0 : 1);
            const auto resources = static_cast<std::uint8_t>(ctx.rng().Chance(3, 4) ? 0 : 5);
            const auto flags =
                static_cast<std::uint8_t>(ctx.rng().Next32() & (ctx.rng().Chance(31, 32) ? 0xfe : 0xff));
            worlds.Both([&](CGameData* w) {
                Tile& tile = w->tiles[x][y];
                tile.terrain = terrain;
                tile.object = object;
                tile.resources = resources;
                tile.flags = flags;
            });
        }
    }
    for (std::uint32_t entry = 0; entry < kTestOffsets; ++entry) {
        const std::int32_t reach = entry < 96 ? 5 : 12;
        const std::int32_t dx = ctx.rng().Range(-reach, reach);
        const std::int32_t dy = ctx.rng().Range(-reach, reach);
        worlds.Both([&](CGameData* w) {
            w->offsets[entry].dx = dx;
            w->offsets[entry].dy = dy;
        });
    }
    const std::uint32_t ab = ctx.rng().Next32();
    const auto c = static_cast<std::uint16_t>(ctx.rng().Next32());
    worlds.Both([&](CGameData* w) {
        w->random_state[0] = static_cast<std::uint16_t>(ab);
        w->random_state[1] = static_cast<std::uint16_t>(ab >> 16);
        w->random_state[2] = c;
    });
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
std::uint32_t __fastcall WrongScatterObjects(CGameData* world, void* edx, std::uint32_t required,
                                             std::uint32_t object, std::uint32_t density, std::uint32_t blockMode,
                                             std::uint32_t extra) {
    return CGameData_ScatterObjects(world, edx, required, object, density, blockMode,
                                    blockMode + extra == 3 ? extra + 1 : extra);
}

void CompareScatterObjects(test::Context& ctx, Scatter5Fn candidate, int cases) {
    CompareScatter(ctx, cases, [&](test::WorldPair& worlds) {
        const std::uint32_t required = Terrain(ctx);
        const std::uint32_t object = ctx.rng().Next32();
        const std::uint32_t density = Density(ctx) & 0xfff;
        const std::uint32_t blockMode = ctx.rng().Below(4);
        const std::uint32_t extra = ctx.rng().Chance(1, 2) ? 3 - blockMode : ctx.rng().Below(4);
        Original_CGameData_ScatterObjects(worlds.original(), nullptr, required, object, density, blockMode, extra);
        candidate(worlds.ours(), nullptr, required, object, density, blockMode, extra);
    });
}

}  // namespace

S3RE_TEST(CGameData_ScatterTileByte11_MatchesOriginal) {
    CompareScatter(ctx, 40, [&](test::WorldPair& worlds) {
        const std::uint32_t base = ctx.rng().Next32();
        const std::uint32_t density = Density(ctx) & 0xfff;
        Original_CGameData_ScatterTileByte11(worlds.original(), nullptr, base, density);
        CGameData_ScatterTileByte11(worlds.ours(), nullptr, base, density);
    });
}

S3RE_TEST(CGameData_ScatterTerrainNear_MatchesOriginal) {
    CompareScatter(ctx, 40, [&](test::WorldPair& worlds) {
        const std::uint32_t required = Terrain(ctx);
        const std::uint32_t terrain = ctx.rng().Next32();
        // Sometimes negative: no rounds (signed count).
        const std::uint32_t density = ctx.rng().Chance(1, 8) ? 0u - ctx.rng().Below(100) : ctx.rng().Below(40);
        Original_CGameData_ScatterTerrainNear(worlds.original(), nullptr, required, terrain, density);
        CGameData_ScatterTerrainNear(worlds.ours(), nullptr, required, terrain, density);
    });
}

S3RE_TEST(CGameData_ScatterObjects_MatchesOriginal) {
    CompareScatterObjects(ctx, &CGameData_ScatterObjects, 40);
}
S3RE_TEST(CGameData_ScatterObjects_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareScatterObjects(probe, &WrongScatterObjects, 20);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CGameData_ScatterObjectGroups_MatchesOriginal) {
    CompareScatter(ctx, 40, [&](test::WorldPair& worlds) {
        const std::uint32_t required = Terrain(ctx);
        const std::uint32_t low = ctx.rng().Chance(1, 4) ? ctx.rng().Next32() : ctx.rng().Below(8);
        const std::uint32_t high = ctx.rng().Chance(1, 4) ? ctx.rng().Next32() : low + ctx.rng().Below(8);
        const std::uint32_t density = Density(ctx) & 0xfff;
        const std::uint32_t blockMode = ctx.rng().Below(4);
        const std::uint32_t extra = ctx.rng().Chance(1, 2) ? 3 - blockMode : ctx.rng().Below(4);
        Original_CGameData_ScatterObjectGroups(worlds.original(), nullptr, required, low, high, density, blockMode,
                                               extra);
        CGameData_ScatterObjectGroups(worlds.ours(), nullptr, required, low, high, density, blockMode, extra);
    });
}

// ---------------------------------------------------------------------------
// CGameData::ReplaceTerrainNextTo (0x00519270): (terrain, neighbour,
// replacement): for x = 1 .. mapSize-2, inner y = 1 .. mapSize-2 (signed
// compares; mapSize at world+0x2d2d0 re-read per step), a tile whose
// terrain byte +0x6 equals `terrain` (zero-extended, 32-bit compare) and
// that has one of the six neighbours (x+1, y), (x-1, y), (x, y-1),
// (x, y+1), (x+1, y+1), (x-1, y-1) with terrain `neighbour` gets terrain
// `replacement` (low byte). Works in place, so a replaced tile can make the
// next one match. Called four times by CGameData::GenerateRandomMap (map generation,
// probably to put transition terrain such as beaches along borders).
// thiscall, ret 0xc; EAX is clobbered by every caller (void here).
// ---------------------------------------------------------------------------

using ReplaceNextToFn = void(__fastcall*)(CGameData* world, void* edx, std::uint32_t terrain, std::uint32_t neighbour,
                                          std::uint32_t replacement);

// Purpose: Map generation: turns tiles of one terrain into another where they border a third (probably to
// add transition terrain, e.g. shore along water).
void __fastcall CGameData_ReplaceTerrainNextTo(CGameData* world, void*, std::uint32_t terrain, std::uint32_t neighbour,
                                               std::uint32_t replacement) {
    // mapSize re-read per step.
    for (std::int32_t x = 1; x < MapSize(world) - 1; ++x) {
        for (std::int32_t y = 1; y < MapSize(world) - 1; ++y) {
            const std::uint32_t tile = TileOffsetXY(x, y);
            if (TileAt(world, tile).terrain != terrain) {
                continue;
            }
            static constexpr std::int32_t kNeighbours[] = {kRow, -kRow, -kColumn, kColumn, kRow + kColumn, -kRow - kColumn};
            for (const std::int32_t step : kNeighbours) {
                if (TileAt(world, tile + static_cast<std::uint32_t>(step)).terrain == neighbour) {
                    TileAt(world, tile).terrain = static_cast<std::uint8_t>(replacement);
                    break;
                }
            }
        }
    }
}

S3RE_HOOK(0x00519270, CGameData_ReplaceTerrainNextTo, ReplaceNextToFn, Original_CGameData_ReplaceTerrainNextTo,
          CGameData_ReplaceTerrainNextTo);

// ---------------------------------------------------------------------------
// CGameData::ExpandTerrain (0x00519440): (terrain, into): one growth step:
// ReplaceTerrainNextTo(into, terrain, 0xf0), then ReplaceTerrainType(0xf0,
// terrain). Every tile of `into` that touches `terrain` becomes `terrain`;
// the temporary 0xf0 keeps a converted tile from spreading further in the
// same step. EAX = ReplaceTerrainType's (mapSize - 1). thiscall, ret 8.
// Called 29 times by CGameData::GenerateRandomMap (e.g. (0x40, 0x10): mountains grow
// into grass).
//
// CGameData::MarkTerrainEdge (0x0051a2e0): (terrain): for x = 1 ..
// mapSize-2, inner y = 1 .. mapSize-2 (signed, mapSize re-read), a tile of
// `terrain` (32-bit compare of the zero-extended byte) with one of its six
// neighbours (x, y-1), (x, y+1), (x-1, y), (x+1, y), (x+1, y+1), (x-1, y-1)
// neither `terrain` nor 0xf3 gets terrain 0xf3. In place, so a marked tile
// no longer counts as a foreign neighbour. CGameData::ReplaceTerrainF3
// (0x51a400) later turns the marks into a border terrain. EAX = mapSize - 1.
// thiscall, ret 4. Called 4 times by CGameData::GenerateRandomMap, always with 0x10.
// ---------------------------------------------------------------------------

using ExpandTerrainFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t terrain,
                                                   std::uint32_t into);
using MarkTerrainEdgeFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t terrain);

namespace {
constexpr std::uint32_t kGrowthMark = 0xf0;
constexpr std::uint32_t kEdgeMark = 0xf3;
constexpr std::int32_t kSixNeighbours[] = {-kRow, kRow, -kColumn, kColumn, kRow + kColumn, -kRow - kColumn};
}  // namespace

// Purpose: Map generation: lets a terrain (e.g. mountains, desert) grow by one tile into a neighbouring
// terrain all along its border.
std::uint32_t __fastcall CGameData_ExpandTerrain(CGameData* world, void*, std::uint32_t terrain, std::uint32_t into) {
    CGameData_ReplaceTerrainNextTo(world, nullptr, into, terrain, kGrowthMark);
    return CGameData_ReplaceTerrainType(world, nullptr, kGrowthMark, terrain);
}

// Purpose: Map generation: marks the outer edge of a terrain (tiles bordering another terrain) so that a
// later step can turn the edge into transition terrain.
std::uint32_t __fastcall CGameData_MarkTerrainEdge(CGameData* world, void*, std::uint32_t terrain) {
    // mapSize re-read per step.
    for (std::int32_t x = 1; x < MapSize(world) - 1; ++x) {
        for (std::int32_t y = 1; y < MapSize(world) - 1; ++y) {
            const std::uint32_t tile = TileOffsetXY(x, y);
            if (TileAt(world, tile).terrain != terrain) {
                continue;
            }
            for (const std::int32_t step : kSixNeighbours) {
                const std::uint32_t neighbour = TileAt(world, tile + static_cast<std::uint32_t>(step)).terrain;
                if (neighbour != terrain && neighbour != kEdgeMark) {
                    TileAt(world, tile).terrain = static_cast<std::uint8_t>(kEdgeMark);
                    break;
                }
            }
        }
    }
    return static_cast<std::uint32_t>(MapSize(world) - 1);
}

S3RE_HOOK(0x00519440, CGameData_ExpandTerrain, ExpandTerrainFn, Original_CGameData_ExpandTerrain,
          CGameData_ExpandTerrain);
S3RE_HOOK(0x0051a2e0, CGameData_MarkTerrainEdge, MarkTerrainEdgeFn, Original_CGameData_MarkTerrainEdge,
          CGameData_MarkTerrainEdge);

namespace {

// Small maps (sometimes 0..3 or negative sizes) with terrains 0..3 (so
// matches and chains are common); arguments mostly 0..3, sometimes with
// high bits (must never match).
void CompareReplaceNextTo(test::Context& ctx, ReplaceNextToFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::int32_t n = ctx.rng().Chance(1, 10) ? static_cast<std::int32_t>(ctx.rng().Below(8)) - 4
                                                       : static_cast<std::int32_t>(4 + ctx.rng().Below(30));
        worlds.Both([&](CGameData* w) { w->map_size = static_cast<std::uint32_t>(n); });
        for (std::int32_t x = 0; x < 36; ++x) {
            for (std::int32_t y = 0; y < 36; ++y) {
                const auto terrain = static_cast<std::uint8_t>(ctx.rng().Below(4));
                worlds.Both([&](CGameData* w) { w->tiles[x][y].terrain = terrain; });
            }
        }
        auto argument = [&]() { return ctx.rng().Chance(1, 10) ? ctx.rng().Next32() : ctx.rng().Below(4); };
        const std::uint32_t terrain = argument();
        const std::uint32_t neighbour = argument();
        const std::uint32_t replacement = ctx.rng().Chance(1, 4) ? ctx.rng().Next32() : ctx.rng().Below(4);
        Original_CGameData_ReplaceTerrainNextTo(worlds.original(), nullptr, terrain, neighbour, replacement);
        candidate(worlds.ours(), nullptr, terrain, neighbour, replacement);
        if (!worlds.Same(ctx, kTilesOffset, 36 * kTileRowBytes) || (i % 64 == 63 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: size %d, (%u, %u, %u)", i, n, terrain, neighbour, replacement);
            return;
        }
    }
}

// Wrong: decides on the terrains before any replacement (not in place).
void __fastcall WrongReplaceTerrainNextTo(CGameData* world, void*, std::uint32_t terrain, std::uint32_t neighbour,
                                          std::uint32_t replacement) {
    const std::int32_t mapSize = MapSize(world);
    static std::uint8_t copy[40][40];
    for (std::int32_t x = 0; x < 40; ++x) {
        for (std::int32_t y = 0; y < 40; ++y) {
            copy[x][y] = world->tiles[x][y].terrain;
        }
    }
    for (std::int32_t x = 1; x < mapSize - 1 && x < 39; ++x) {
        for (std::int32_t y = 1; y < mapSize - 1 && y < 39; ++y) {
            if (copy[x][y] == terrain && (copy[x + 1][y] == neighbour || copy[x - 1][y] == neighbour ||
                                          copy[x][y - 1] == neighbour || copy[x][y + 1] == neighbour ||
                                          copy[x + 1][y + 1] == neighbour || copy[x - 1][y - 1] == neighbour)) {
                world->tiles[x][y].terrain = static_cast<std::uint8_t>(replacement);
            }
        }
    }
}

}  // namespace

S3RE_TEST(CGameData_ReplaceTerrainNextTo_MatchesOriginal) {
    CompareReplaceNextTo(ctx, &CGameData_ReplaceTerrainNextTo, 1000);
}
S3RE_TEST(CGameData_ReplaceTerrainNextTo_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareReplaceNextTo(probe, &WrongReplaceTerrainNextTo, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

// Small maps of terrains 0..3 plus some 0xf0/0xf3 tiles (the marks); sizes
// as in CompareReplaceNextTo. Arguments mostly 0..3, sometimes 0xf3 or with
// high bits (must never match).
template <typename Call>
void CompareTerrainPass(test::Context& ctx, int cases, Call call) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::int32_t n = ctx.rng().Chance(1, 10) ? static_cast<std::int32_t>(ctx.rng().Below(8)) - 4
                                                       : static_cast<std::int32_t>(4 + ctx.rng().Below(30));
        worlds.Both([&](CGameData* w) { w->map_size = static_cast<std::uint32_t>(n); });
        for (std::int32_t x = 0; x < 36; ++x) {
            for (std::int32_t y = 0; y < 36; ++y) {
                const std::uint32_t pick = ctx.rng().Below(20);
                const std::uint8_t terrain =
                    pick == 0 ? 0xf0 : pick == 1 ? 0xf3 : static_cast<std::uint8_t>(pick & 3);
                worlds.Both([&](CGameData* w) { w->tiles[x][y].terrain = terrain; });
            }
        }
        auto argument = [&]() {
            return ctx.rng().Chance(1, 10)   ? ctx.rng().Next32()
                   : ctx.rng().Chance(1, 10) ? 0xf3u
                                             : ctx.rng().Below(4);
        };
        const std::uint32_t a = argument();
        const std::uint32_t b = argument();
        if (!call(worlds, a, b) || !worlds.Same(ctx, kTilesOffset, 36 * kTileRowBytes) ||
            (i % 64 == 63 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: size %d, (0x%x, 0x%x)", i, n, a, b);
            return;
        }
    }
}

void CompareExpandTerrain(test::Context& ctx, ExpandTerrainFn candidate, int cases) {
    CompareTerrainPass(ctx, cases, [&](test::WorldPair& worlds, std::uint32_t terrain, std::uint32_t into) {
        const std::uint32_t expected = Original_CGameData_ExpandTerrain(worlds.original(), nullptr, terrain, into);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, terrain, into);
        return S3RE_EXPECT_EQ(expected, actual);
    });
}

void CompareMarkTerrainEdge(test::Context& ctx, MarkTerrainEdgeFn candidate, int cases) {
    CompareTerrainPass(ctx, cases, [&](test::WorldPair& worlds, std::uint32_t terrain, std::uint32_t) {
        const std::uint32_t expected = Original_CGameData_MarkTerrainEdge(worlds.original(), nullptr, terrain);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, terrain);
        return S3RE_EXPECT_EQ(expected, actual);
    });
}

// Wrong: grows without the temporary mark, so a converted tile spreads on.
std::uint32_t __fastcall WrongExpandTerrain(CGameData* world, void*, std::uint32_t terrain, std::uint32_t into) {
    CGameData_ReplaceTerrainNextTo(world, nullptr, into, terrain, terrain);
    return static_cast<std::uint32_t>(MapSize(world) - 1);
}

// Wrong: a neighbour already marked 0xf3 counts as foreign.
std::uint32_t __fastcall WrongMarkTerrainEdge(CGameData* world, void*, std::uint32_t terrain) {
    const std::int32_t mapSize = MapSize(world);
    for (std::int32_t x = 1; x < mapSize - 1; ++x) {
        for (std::int32_t y = 1; y < mapSize - 1; ++y) {
            const std::uint32_t tile = TileOffsetXY(x, y);
            if (TileAt(world, tile).terrain != terrain) {
                continue;
            }
            for (const std::int32_t step : kSixNeighbours) {
                if (TileAt(world, tile + static_cast<std::uint32_t>(step)).terrain != terrain) {
                    TileAt(world, tile).terrain = static_cast<std::uint8_t>(kEdgeMark);
                    break;
                }
            }
        }
    }
    return static_cast<std::uint32_t>(mapSize - 1);
}

}  // namespace

S3RE_TEST(CGameData_ExpandTerrain_MatchesOriginal) {
    CompareExpandTerrain(ctx, &CGameData_ExpandTerrain, 1000);
}
S3RE_TEST(CGameData_ExpandTerrain_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareExpandTerrain(probe, &WrongExpandTerrain, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CGameData_MarkTerrainEdge_MatchesOriginal) {
    CompareMarkTerrainEdge(ctx, &CGameData_MarkTerrainEdge, 1000);
}
S3RE_TEST(CGameData_MarkTerrainEdge_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMarkTerrainEdge(probe, &WrongMarkTerrainEdge, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
