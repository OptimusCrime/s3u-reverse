// Loading a map file into the world (the map path of CLogicThread::OnStart).
//
// CGameData::PrepareLoadedTerrain (0x00519540): (): every terrain byte
//   (tile +0x6) of the map whose class (high nibble) is 0, i.e. every water
//   variant 0..15, becomes 0; then the water gets its depth from the shore:
//   ReplaceTerrainNextTo(0, 0x30, 0xff) marks the water next to the shore
//   (0x30) with the marker 0xff, (0, 0xff, 1) turns the water next to the
//   marks into depth 1, then (0, 1, 2) .. (0, 5, 6) the further bands;
//   ReplaceTerrainType(0, 7) makes the remaining water the deepest (7) and
//   ReplaceTerrainType(0xff, 0) turns the marks back into 0. So water is 0
//   along the shore, then 1..6 band by band, 7 further out. The first loop
//   is unsigned, the map size re-read per step. EAX = the last ReplaceTerrainType's (LoadMap ignores it). Called
//   by LoadMap after "Setting all sectors for loaded Map".
// All thiscall (ECX = world).

#include "game/enums/terrain.h"
#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>

namespace s3re::game {

namespace {

// Marker for the water next to the shore while the depth bands are made
// (a marker byte, so hex).
constexpr std::uint32_t kShoreWaterMark = 0xff;
constexpr std::uint32_t kShallowWater = 0;  // water along the shore, and every water before the bands
constexpr std::uint32_t kDeepestBand = 6;   // the last band ReplaceTerrainNextTo makes
constexpr std::uint32_t kDeepWater = 7;     // everything further out

}  // namespace

using PrepareTerrainFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx);

// Purpose: After a map's landscape is loaded, turns all water into plain water and then gives it depth
// levels by its distance from the shore (shallow along the coast, deep further out).
std::uint32_t __fastcall CGameData_PrepareLoadedTerrain(CGameData* world, void*) {
    for (std::uint32_t x = 0; x < world->map_size; ++x) {
        for (std::uint32_t y = 0; y < world->map_size; ++y) {
            // The original walks y in the outer loop; the writes are independent.
            std::uint8_t& terrain = world->tiles[x][y].terrain;
            if ((terrain & kTerrainClassMask) == kTerrainClassWater) {
                terrain = 0;
            }
        }
    }
    CGameData_ReplaceTerrainNextTo(world, nullptr, kShallowWater, kTerrainShore, kShoreWaterMark);
    CGameData_ReplaceTerrainNextTo(world, nullptr, kShallowWater, kShoreWaterMark, 1);
    for (std::uint32_t band = 1; band < kDeepestBand; ++band) {
        CGameData_ReplaceTerrainNextTo(world, nullptr, kShallowWater, band, band + 1);
    }
    CGameData_ReplaceTerrainType(world, nullptr, kShallowWater, kDeepWater);
    return CGameData_ReplaceTerrainType(world, nullptr, kShoreWaterMark, kShallowWater);
}

S3RE_HOOK(0x00519540, CGameData_PrepareLoadedTerrain, PrepareTerrainFn, Original_CGameData_PrepareLoadedTerrain,
          CGameData_PrepareLoadedTerrain);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// Maps of 0..80 tiles (sometimes 384) of land, shore and water patches; the
// whole world is compared.
void ComparePrepareTerrain(test::Context& ctx, PrepareTerrainFn candidate, int cases) {
    test::WorldPair worlds(ctx, sizeof(CGameData));
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = ctx.rng().Chance(1, 10)   ? ctx.rng().Below(4)
                                   : ctx.rng().Chance(1, 10) ? 384
                                                             : 8 + ctx.rng().Below(72);
        const std::uint32_t patch = 2 + ctx.rng().Below(8);
        for (std::uint32_t x = 0; x < size + 2; ++x) {
            for (std::uint32_t y = 0; y < size + 2; ++y) {
                const std::uint32_t kind = ((x / patch) * 7 + (y / patch) * 3) % 5;
                std::uint8_t terrain;
                if (ctx.rng().Chance(1, 20)) {
                    terrain = static_cast<std::uint8_t>(ctx.rng().Next32());
                } else if (kind < 2) {
                    terrain = static_cast<std::uint8_t>(ctx.rng().Below(16));  // water variants
                } else if (kind == 2) {
                    terrain = kTerrainShore;
                } else {
                    terrain = static_cast<std::uint8_t>(0x10 + ctx.rng().Below(0x30));
                }
                worlds.Both([&](CGameData* w) { w->tiles[x][y].terrain = terrain; });
            }
        }
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        const std::uint32_t expected = Original_CGameData_PrepareLoadedTerrain(worlds.original(), nullptr);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (size %u)", i, size);
            return;
        }
    }
}

// Wrong: never makes band 6 (that water becomes 7).
std::uint32_t __fastcall WrongPrepareLoadedTerrain(CGameData* world, void* edx) {
    for (std::uint32_t x = 0; x < world->map_size; ++x) {
        for (std::uint32_t y = 0; y < world->map_size; ++y) {
            std::uint8_t& terrain = world->tiles[x][y].terrain;
            if ((terrain & kTerrainClassMask) == kTerrainClassWater) {
                terrain = 0;
            }
        }
    }
    CGameData_ReplaceTerrainNextTo(world, edx, 0, kTerrainShore, kShoreWaterMark);
    CGameData_ReplaceTerrainNextTo(world, edx, 0, kShoreWaterMark, 1);
    for (std::uint32_t band = 1; band < kDeepestBand - 1; ++band) {
        CGameData_ReplaceTerrainNextTo(world, edx, 0, band, band + 1);
    }
    CGameData_ReplaceTerrainType(world, edx, 0, kDeepWater);
    return CGameData_ReplaceTerrainType(world, edx, kShoreWaterMark, 0);
}

}  // namespace

S3RE_TEST(CGameData_PrepareLoadedTerrain_MatchesOriginal) {
    ComparePrepareTerrain(ctx, &CGameData_PrepareLoadedTerrain, 150);
}
S3RE_TEST(CGameData_PrepareLoadedTerrain_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePrepareTerrain(probe, &WrongPrepareLoadedTerrain, 30);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
