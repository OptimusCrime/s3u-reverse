// Map coordinate helpers.
//
// CGameData::IsOnMap (0x005194e0): (x, y) -> 1 if both are below mapSize
// (unsigned), else 0.
// CGameData::IsInsideMapBorder (0x00519510): (x, y) -> 1 if both are in
// [1, mapSize - 1) (unsigned), i.e. on the map and not on its outer edge.
// CGameData::TileScreenX (0x00519610): (x, y) -> 8 * (2x - y + mapSize + 22),
// the tile's horizontal position in the isometric map image (32-bit wrap).
// CGameData::TileScreenY (0x00519640): (x, y) -> 9y - height + 0xb6, with
// the height from tile byte +0x8 (tile base 0x67440; Tile::height at the
// real base 0x67444).
// All: thiscall (ECX = world), two stack arguments, ret 8, full EAX result.

#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>

namespace s3re::game {

namespace {

std::uint32_t MapSize(CGameData* world) {
    return world->map_size;
}

}  // namespace

using MapXYFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x,
                                           std::uint32_t y);

// Purpose: Checks that a tile coordinate is inside the map at all. Guards almost every map access.
std::uint32_t __fastcall CGameData_IsOnMap(CGameData* world, void* /*edx*/, std::uint32_t x,
                                           std::uint32_t y) {
    const std::uint32_t size = MapSize(world);
    return (x < size && y < size) ? 1 : 0;
}

// Purpose: Checks that a tile is inside the map and not on its outermost ring, so that all six of its
// neighbours exist too.
std::uint32_t __fastcall CGameData_IsInsideMapBorder(CGameData* world, void* /*edx*/,
                                                     std::uint32_t x, std::uint32_t y) {
    const std::uint32_t last = MapSize(world) - 1;
    return (x >= 1 && x < last && y >= 1 && y < last) ? 1 : 0;
}

// Purpose: Where a tile is drawn on screen horizontally (the map is drawn as a slanted grid). Used by
// drawing and mouse picking.
std::uint32_t __fastcall CGameData_TileScreenX(CGameData* world, void* /*edx*/, std::uint32_t x,
                                               std::uint32_t y) {
    return (2u * x - y + MapSize(world) + 0x16u) << 3;
}

// Purpose: Where a tile is drawn on screen vertically, raised by the tile's terrain height. Used by drawing
// and mouse picking.
std::uint32_t __fastcall CGameData_TileScreenY(CGameData* world, void* /*edx*/, std::uint32_t x,
                                               std::uint32_t y) {
    // Tile offset in 32-bit arithmetic, like the original.
    const std::uint8_t height = TileAtOffset(world, TileOffset(x, y)).height;
    return 9u * y - height + 0xb6u;
}

S3RE_HOOK(0x005194e0, CGameData_IsOnMap, MapXYFn, Original_CGameData_IsOnMap, CGameData_IsOnMap);
S3RE_HOOK(0x00519510, CGameData_IsInsideMapBorder, MapXYFn, Original_CGameData_IsInsideMapBorder,
          CGameData_IsInsideMapBorder);
S3RE_HOOK(0x00519610, CGameData_TileScreenX, MapXYFn, Original_CGameData_TileScreenX,
          CGameData_TileScreenX);
S3RE_HOOK(0x00519640, CGameData_TileScreenY, MapXYFn, Original_CGameData_TileScreenY,
          CGameData_TileScreenY);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestMapSize = 384;
constexpr std::size_t kMapWorld = offsetof(CGameData, tiles) + kTestMapSize * kTileRowBytes;

// `onMap`: coordinates must be valid tiles (for functions that read the
// tile); otherwise anything, mostly around the map edges.
void CompareMapXY(test::Context& ctx, MapXYFn original, MapXYFn candidate, bool onMap,
                  int cases) {
    test::WorldPair worlds(ctx, kMapWorld);
    for (int i = 0; i < cases; ++i) {
        std::uint32_t mapSize = onMap ? kTestMapSize : ctx.rng().Below(kTestMapSize + 1);
        if (!onMap && ctx.rng().Chance(1, 20)) {
            mapSize = ctx.rng().Next32();
        }
        worlds.Both([&](CGameData* w) { w->map_size = mapSize; });
        auto coordinate = [&]() -> std::uint32_t {
            if (onMap) {
                return ctx.rng().Below(kTestMapSize);
            }
            switch (ctx.rng().Below(4)) {
            case 0:
                return ctx.rng().Next32();
            case 1:  // around the edges
                return mapSize + static_cast<std::uint32_t>(ctx.rng().Range(-2, 2));
            case 2:
                return ctx.rng().Below(3);
            default:
                return ctx.rng().Below(kTestMapSize + 8);
            }
        };
        const std::uint32_t x = coordinate();
        const std::uint32_t y = coordinate();
        const std::uint32_t expected = original(worlds.original(), nullptr, x, y);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, x, y);
        if (!S3RE_EXPECT_EQ(expected, actual) ||
            (i % 4096 == 4095 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%u, %u), mapSize %u", i, x, y, mapSize);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

// Wrong: <= instead of < at the far border.
std::uint32_t __fastcall WrongIsInsideMapBorder(CGameData* world, void* /*edx*/, std::uint32_t x,
                                                std::uint32_t y) {
    const std::uint32_t last = MapSize(world) - 1;
    return (x >= 1 && x <= last && y >= 1 && y <= last) ? 1 : 0;
}

// Wrong: ignores the height.
std::uint32_t __fastcall WrongTileScreenY(CGameData* /*world*/, void* /*edx*/,
                                          std::uint32_t /*x*/, std::uint32_t y) {
    return 9u * y + 0xb6u;
}

}  // namespace

S3RE_TEST(CGameData_IsOnMap_MatchesOriginal) {
    CompareMapXY(ctx, Original_CGameData_IsOnMap, &CGameData_IsOnMap, false, 200000);
}

S3RE_TEST(CGameData_IsInsideMapBorder_MatchesOriginal) {
    CompareMapXY(ctx, Original_CGameData_IsInsideMapBorder, &CGameData_IsInsideMapBorder, false,
                 200000);
}

S3RE_TEST(CGameData_IsInsideMapBorder_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMapXY(probe, Original_CGameData_IsInsideMapBorder, &WrongIsInsideMapBorder, false,
                 20000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CGameData_TileScreenX_MatchesOriginal) {
    CompareMapXY(ctx, Original_CGameData_TileScreenX, &CGameData_TileScreenX, false, 200000);
}

S3RE_TEST(CGameData_TileScreenY_MatchesOriginal) {
    CompareMapXY(ctx, Original_CGameData_TileScreenY, &CGameData_TileScreenY, true, 200000);
}

S3RE_TEST(CGameData_TileScreenY_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMapXY(probe, Original_CGameData_TileScreenY, &WrongTileScreenY, true, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
