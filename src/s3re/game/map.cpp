// Map coordinate helpers.
//
// GameWorld::IsOnMap (0x005194e0): (x, y) -> 1 if both are below mapSize
// (unsigned), else 0.
// GameWorld::IsInsideMapBorder (0x00519510): (x, y) -> 1 if both are in
// [1, mapSize - 1) (unsigned), i.e. on the map and not on its outer edge.
// GameWorld::TileScreenX (0x00519610): (x, y) -> 8 * (2x - y + mapSize + 22),
// the tile's horizontal position in the isometric map image (32-bit wrap).
// GameWorld::TileScreenY (0x00519640): (x, y) -> 9y - height + 0xb6, with
// the height from tile byte +0x8.
// All: thiscall (ECX = world), two stack arguments, ret 8, full EAX result.

#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>

namespace s3re::game {

namespace {

constexpr std::size_t kTileHeight = 0x8;  // uint8, offset in the tile record

std::uint32_t MapSize(GameWorld* world) {
    return WorldField<std::uint32_t>(world, world_offset::kMapSize);
}

}  // namespace

using MapXYFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x,
                                           std::uint32_t y);

std::uint32_t __fastcall GameWorld_IsOnMap(GameWorld* world, void* /*edx*/, std::uint32_t x,
                                           std::uint32_t y) {
    const std::uint32_t size = MapSize(world);
    return (x < size && y < size) ? 1 : 0;
}

std::uint32_t __fastcall GameWorld_IsInsideMapBorder(GameWorld* world, void* /*edx*/,
                                                     std::uint32_t x, std::uint32_t y) {
    const std::uint32_t last = MapSize(world) - 1;
    return (x >= 1 && x < last && y >= 1 && y < last) ? 1 : 0;
}

std::uint32_t __fastcall GameWorld_TileScreenX(GameWorld* world, void* /*edx*/, std::uint32_t x,
                                               std::uint32_t y) {
    return (2u * x - y + MapSize(world) + 0x16u) << 3;
}

std::uint32_t __fastcall GameWorld_TileScreenY(GameWorld* world, void* /*edx*/, std::uint32_t x,
                                               std::uint32_t y) {
    const std::uint32_t tile = (x * world_offset::kMapRowTiles + y) * world_offset::kTileBytes;
    const std::uint8_t height = *(reinterpret_cast<const std::uint8_t*>(world) +
                                  world_offset::kMapTiles + tile + kTileHeight);
    return 9u * y - height + 0xb6u;
}

S3RE_HOOK(0x005194e0, GameWorld_IsOnMap, MapXYFn, Original_GameWorld_IsOnMap, GameWorld_IsOnMap);
S3RE_HOOK(0x00519510, GameWorld_IsInsideMapBorder, MapXYFn, Original_GameWorld_IsInsideMapBorder,
          GameWorld_IsInsideMapBorder);
S3RE_HOOK(0x00519610, GameWorld_TileScreenX, MapXYFn, Original_GameWorld_TileScreenX,
          GameWorld_TileScreenX);
S3RE_HOOK(0x00519640, GameWorld_TileScreenY, MapXYFn, Original_GameWorld_TileScreenY,
          GameWorld_TileScreenY);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestMapSize = 384;
constexpr std::size_t kMapWorld =
    world_offset::kMapTiles + kTestMapSize * world_offset::kMapRowBytes;

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
        worlds.Set<std::uint32_t>(world_offset::kMapSize, mapSize);
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
std::uint32_t __fastcall WrongIsInsideMapBorder(GameWorld* world, void* /*edx*/, std::uint32_t x,
                                                std::uint32_t y) {
    const std::uint32_t last = MapSize(world) - 1;
    return (x >= 1 && x <= last && y >= 1 && y <= last) ? 1 : 0;
}

// Wrong: ignores the height.
std::uint32_t __fastcall WrongTileScreenY(GameWorld* /*world*/, void* /*edx*/,
                                          std::uint32_t /*x*/, std::uint32_t y) {
    return 9u * y + 0xb6u;
}

}  // namespace

S3RE_TEST(GameWorld_IsOnMap_MatchesOriginal) {
    CompareMapXY(ctx, Original_GameWorld_IsOnMap, &GameWorld_IsOnMap, false, 200000);
}

S3RE_TEST(GameWorld_IsInsideMapBorder_MatchesOriginal) {
    CompareMapXY(ctx, Original_GameWorld_IsInsideMapBorder, &GameWorld_IsInsideMapBorder, false,
                 200000);
}

S3RE_TEST(GameWorld_IsInsideMapBorder_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMapXY(probe, Original_GameWorld_IsInsideMapBorder, &WrongIsInsideMapBorder, false,
                 20000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_TileScreenX_MatchesOriginal) {
    CompareMapXY(ctx, Original_GameWorld_TileScreenX, &GameWorld_TileScreenX, false, 200000);
}

S3RE_TEST(GameWorld_TileScreenY_MatchesOriginal) {
    CompareMapXY(ctx, Original_GameWorld_TileScreenY, &GameWorld_TileScreenY, true, 200000);
}

S3RE_TEST(GameWorld_TileScreenY_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMapXY(probe, Original_GameWorld_TileScreenY, &WrongTileScreenY, true, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
