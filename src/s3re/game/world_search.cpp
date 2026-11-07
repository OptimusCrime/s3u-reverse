// Small helpers of the world's searches.
//
// TileDistance (0x004ff320): distance between two tiles in steps on the
// game's map grid, where the diagonal (+1, +1) / (-1, -1) is a single step
// and (+1, -1) / (-1, +1) are two. All four arguments are compared and
// subtracted as unsigned 32-bit values (callers pass valid coordinates).
// __stdcall-compatible: four stack arguments, ret 0x10, ECX not used.
//
//   x0 <  x1, y0 <  y1:  max(x1 - x0, y1 - y0)
//   x0 <  x1, y0 >= y1:  (x1 - x0) + (y0 - y1)
//   x0 >= x1, y0 <  y1:  (x0 - x1) + (y1 - y0)
//   x0 >= x1, y0 >= y1:  max(x0 - x1, y0 - y1)
//
// GameWorld::NextSearchGeneration (0x004ff3f0): searches mark visited tiles
// by writing the current generation into tile byte 15, so starting a search
// is just "generation + 1" instead of clearing the whole map. When the 8-bit
// generation wraps to 0, every tile's mark is cleared and the generation
// restarts at 1. Also resets the byte after the generation (a counter that
// CLogicThread::PrepareDrawData increments). thiscall (ECX = world), no
// arguments, plain ret; callers don't use EAX.

#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using TileDistanceFn = std::uint32_t(__stdcall*)(std::uint32_t x0, std::uint32_t y0,
                                                 std::uint32_t x1, std::uint32_t y1);

std::uint32_t __stdcall TileDistance(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1,
                                     std::uint32_t y1) {
    if (x0 < x1) {
        const std::uint32_t dx = x1 - x0;
        if (y0 < y1) {
            const std::uint32_t dy = y1 - y0;
            return dy > dx ? dy : dx;
        }
        return dx + (y0 - y1);
    }
    const std::uint32_t dx = x0 - x1;
    if (y0 < y1) {
        return dx + (y1 - y0);
    }
    const std::uint32_t dy = y0 - y1;
    return dy > dx ? dy : dx;
}

S3RE_HOOK(0x004ff320, TileDistance, TileDistanceFn, Original_TileDistance, TileDistance);

using NextSearchGenerationFn = void(__fastcall*)(GameWorld* world, void* edx);

void __fastcall GameWorld_NextSearchGeneration(GameWorld* world, void* /*edx*/) {
    using namespace world_offset;
    auto& generation = WorldField<std::uint8_t>(world, kSearchGeneration);
    WorldField<std::uint8_t>(world, kSearchGenerationAux) = 0;
    ++generation;
    if (generation != 0) {
        return;
    }
    // Wrapped around: forget all marks. mapSize is a signed int in the
    // original's loop conditions.
    const auto mapSize = WorldField<std::int32_t>(world, kMapSize);
    for (std::int32_t y = 0; y < mapSize; ++y) {
        for (std::int32_t x = 0; x < mapSize; ++x) {
            WorldField<std::uint8_t>(world, kMapTiles + x * kMapRowBytes + y * kTileBytes +
                                                kTileSearchMark) = 0;
        }
    }
    generation = 1;
}

S3RE_HOOK(0x004ff3f0, GameWorld_NextSearchGeneration, NextSearchGenerationFn,
          Original_GameWorld_NextSearchGeneration, GameWorld_NextSearchGeneration);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// Mostly small map coordinates (the real use), sometimes anything.
std::uint32_t RandomCoordinate(test::Rng& rng) {
    switch (rng.Below(4)) {
    case 0:
        return rng.Next32();
    case 1:
        return 0xFFFFFFFFu - rng.Below(4);
    default:
        return rng.Below(800);
    }
}

void CompareTileDistance(test::Context& ctx, TileDistanceFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t x0 = RandomCoordinate(ctx.rng());
        const std::uint32_t y0 = ctx.rng().Chance(1, 8) ? x0 : RandomCoordinate(ctx.rng());
        const std::uint32_t x1 = ctx.rng().Chance(1, 8) ? x0 : RandomCoordinate(ctx.rng());
        const std::uint32_t y1 = ctx.rng().Chance(1, 8) ? y0 : RandomCoordinate(ctx.rng());
        const std::uint32_t expected = Original_TileDistance(x0, y0, x1, y1);
        const std::uint32_t actual = candidate(x0, y0, x1, y1);
        if (!S3RE_EXPECT_EQ(expected, actual)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%u, %u) -> (%u, %u)", i, x0, y0, x1, y1);
            return;
        }
    }
}

// Wrong: treats every direction like the "max" case.
std::uint32_t __stdcall WrongTileDistance(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1,
                                          std::uint32_t y1) {
    const std::uint32_t dx = x0 < x1 ? x1 - x0 : x0 - x1;
    const std::uint32_t dy = y0 < y1 ? y1 - y0 : y0 - y1;
    return dx > dy ? dx : dy;
}

}  // namespace

S3RE_TEST(TileDistance_MatchesOriginal) {
    CompareTileDistance(ctx, &TileDistance, 1000000);
}

S3RE_TEST(TileDistance_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareTileDistance(probe, &WrongTileDistance, 100000);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

// A world buffer reaching to the end of the tile rows of a map of `mapSize`
// (at least past the generation bytes). Byte 0 of the buffer is world+0.
std::size_t WorldBytesForMap(std::int32_t mapSize) {
    using namespace world_offset;
    const std::size_t rows = mapSize > 0 ? static_cast<std::size_t>(mapSize) : 0;
    return kMapTiles + (rows + 1) * kMapRowBytes;
}

void CompareNextSearchGeneration(test::Context& ctx, NextSearchGenerationFn candidate,
                                 int cases, std::int32_t maxMapSize, int bigMapCases) {
    using namespace world_offset;
    for (int i = 0; i < cases + bigMapCases; ++i) {
        std::int32_t mapSize;
        if (i >= cases) {
            mapSize = static_cast<std::int32_t>(kMapRowTiles);  // the largest map
        } else if (ctx.rng().Chance(1, 10)) {
            mapSize = ctx.rng().Range(-3, 0);  // empty / invalid
        } else {
            mapSize = ctx.rng().Range(1, maxMapSize);
        }
        const std::size_t size = WorldBytesForMap(mapSize);
        std::vector<std::uint8_t> a(size);
        ctx.rng().Fill(a.data(), size);
        // Half the cases at the wrap-around.
        a[kSearchGeneration] = ctx.rng().Chance(1, 2) ? 0xFF
                                                      : static_cast<std::uint8_t>(
                                                            ctx.rng().Below(256));
        std::memcpy(&a[kMapSize], &mapSize, sizeof(mapSize));
        std::vector<std::uint8_t> b = a;

        Original_GameWorld_NextSearchGeneration(reinterpret_cast<GameWorld*>(a.data()), nullptr);
        candidate(reinterpret_cast<GameWorld*>(b.data()), nullptr);
        if (!S3RE_EXPECT_MEM_EQ(a.data(), b.data(), size)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: mapSize %d", i, mapSize);
            return;
        }
    }
}

// Wrong: after the wrap-around the generation restarts at 0 instead of 1.
void __fastcall WrongNextSearchGeneration(GameWorld* world, void* edx) {
    GameWorld_NextSearchGeneration(world, edx);
    auto& generation = WorldField<std::uint8_t>(world, world_offset::kSearchGeneration);
    if (generation == 1) {
        generation = 0;
    }
}

}  // namespace

S3RE_TEST(GameWorld_NextSearchGeneration_MatchesOriginal) {
    CompareNextSearchGeneration(ctx, &GameWorld_NextSearchGeneration, 400, 48, 2);
}

S3RE_TEST(GameWorld_NextSearchGeneration_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareNextSearchGeneration(probe, &WrongNextSearchGeneration, 100, 8, 0);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
