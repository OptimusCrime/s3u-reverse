// Terrain changes made by settler jobs (UpdateSettlers callees).
//
// CGameData::UpdateShadeAround (0x00519670): (x, y): tile byte +0x5
//   (shade) = CGameData::TileShade for the tile and its six neighbours, in
//   the order (x, y), (x-1, y), (x+1, y), (x, y-1), (x, y+1), (x-1, y-1),
//   (x+1, y+1), each computed after the previous one is stored. No map
//   check (the original's 32-bit tile arithmetic). Called after a tile's
//   height changes (diggers levelling a site: UpdateSettlers jobs 5/7/8/9;
//   0x506cf0). ret 8; EAX = the last TileShade's (callers ignore it).
// CGameData::SetTerrain16Patch (0x0051ba00): (x, y): only if the tile's
//   terrain (+0x6) is 0x10, 0x12, 0x13, 0x18 or 0x1c (grass and its
//   variants; 0x1c a worn path): the six neighbours are checked in the
//   order (x+1, y), (x-1, y), (x, y+1), (x, y-1), (x+1, y+1), (x-1, y-1).
//   A neighbour of terrain 0x10 or 0x16 is accepted as it is, one of
//   0x12/0x13/0x18/0x1c becomes 0x10 (redraw bit 0x80 of +0x9 set); any
//   other terrain stops the function at once (neighbours changed before it
//   stay changed). If all six were accepted, the tile itself becomes
//   terrain 0x16 (redraw bit set). Called by UpdateSettlers jobs 145, 230
//   and 247 (0x91, 0xe6, 0xf7: work chains of the farmer, the rice farmer
//   and the winegrower, settler types 16, 38 and 29), so 0x16 is a field
//   (grain, rice or vines); the CSV's proposal "RestoreGrassAt" was wrong
//   (the tile itself does not become grass). Terrain values have a class in
//   the high nibble and stay hex. ret 8; EAX = world + (x * 0x300 + y) * 24
//   (callers ignore it).
// Both thiscall (ECX = world).

#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <iterator>

namespace s3re::game {

namespace {

using layout::Tile;

constexpr std::uint32_t kStepX = kTileRowBytes;
constexpr std::uint32_t kStepY = kTileBytes;

constexpr std::uint8_t kTerrainGrass = 0x10;
constexpr std::uint8_t kTerrainField = 0x16;  // a farmer's, rice farmer's or winegrower's field

}  // namespace

using TileFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);

// Purpose: Recomputes how light the tile and its six neighbours are drawn after the tile's height
// changed (e.g. while diggers level a building site).
std::uint32_t __fastcall CGameData_UpdateShadeAround(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    struct Step {
        std::int32_t dx, dy;
    };
    static constexpr Step kOrder[] = {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, 1}};
    std::uint32_t shade = 0;
    for (const Step& step : kOrder) {
        const std::uint32_t tx = x + static_cast<std::uint32_t>(step.dx);
        const std::uint32_t ty = y + static_cast<std::uint32_t>(step.dy);
        shade = CGameData_TileShade(world, nullptr, tx, ty);
        TileAtOffset(world, TileOffset(tx, ty)).shade = static_cast<std::uint8_t>(shade);
    }
    return shade;
}

namespace {

bool IsGrassVariant(std::uint8_t terrain) {
    return terrain == 0x12 || terrain == 0x13 || terrain == 0x18 || terrain == 0x1c;
}

}  // namespace

// Purpose: A farmer, rice farmer or winegrower turns a grass tile whose surroundings are all grass into a
// field (terrain 0x16), making the grass variants around it plain grass.
std::uint32_t __fastcall CGameData_SetTerrain16Patch(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t tile = TileOffset(x, y);
    const std::uint32_t result = tile - static_cast<std::uint32_t>(offsetof(CGameData, tiles)) +
                                 static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));
    Tile& centre = TileAtOffset(world, tile);
    if (centre.terrain != kTerrainGrass && !IsGrassVariant(centre.terrain)) {
        return result;
    }
    static constexpr std::uint32_t kNeighbours[] = {kStepX, 0u - kStepX, kStepY, 0u - kStepY,
                                                    kStepX + kStepY, 0u - kStepX - kStepY};
    std::uint32_t accepted = 0;
    for (std::uint32_t k = 1; k <= std::size(kNeighbours); ++k) {
        Tile& neighbour = TileAtOffset(world, tile + kNeighbours[k - 1]);
        const std::uint8_t terrain = neighbour.terrain;
        if (terrain == kTerrainGrass || terrain == kTerrainField) {
            accepted = k;
        }
        if (IsGrassVariant(terrain)) {
            neighbour.terrain = kTerrainGrass;
            neighbour.draw_flags |= layout::kTileDrawRedraw;
            accepted = k;
        } else if (accepted != k) {
            return result;
        }
    }
    centre.terrain = kTerrainField;
    centre.draw_flags |= layout::kTileDrawRedraw;
    return result;
}

S3RE_HOOK(0x00519670, CGameData_UpdateShadeAround, TileFn, Original_CGameData_UpdateShadeAround,
          CGameData_UpdateShadeAround);
S3RE_HOOK(0x0051ba00, CGameData_SetTerrain16Patch, TileFn, Original_CGameData_SetTerrain16Patch,
          CGameData_SetTerrain16Patch);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kTestWorld = offsetof(CGameData, tiles) + 40 * sizeof(CGameData::tiles[0]);

std::uint32_t Relative(std::uint32_t value, CGameData* world) {
    return value - static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));
}

// Tiles (1..30, 1..30): heights near each other (UpdateShadeAround) and
// terrains mostly from the accepted set (SetTerrain16Patch).
void CompareTileFn(test::Context& ctx, TileFn original, TileFn candidate, int cases, bool relativeEax) {
    test::WorldPair worlds(ctx, kTestWorld);
    static constexpr std::uint8_t kTerrains[] = {kTerrainGrass, kTerrainField, 0x12, 0x13, 0x18, 0x1c};
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t x = 2 + ctx.rng().Below(30);
        const std::uint32_t y = 2 + ctx.rng().Below(30);
        const std::uint32_t base = ctx.rng().Below(256);
        const bool likely = ctx.rng().Chance(3, 4);
        for (std::uint32_t dx = 0; dx < 5; ++dx) {
            for (std::uint32_t dy = 0; dy < 5; ++dy) {
                const auto h = static_cast<std::uint8_t>(base + ctx.rng().Below(25));
                const auto terrain = (likely && !ctx.rng().Chance(1, 30)) ? kTerrains[ctx.rng().Below(6)]
                                                                         : static_cast<std::uint8_t>(ctx.rng().Below(256));
                worlds.Both([&](CGameData* w) {
                    w->tiles[x + dx - 2][y + dy - 2].height = h;
                    w->tiles[x + dx - 2][y + dy - 2].terrain = terrain;
                });
            }
        }
        std::uint32_t expected = original(worlds.original(), nullptr, x, y);
        std::uint32_t actual = candidate(worlds.ours(), nullptr, x, y);
        if (relativeEax) {
            expected = Relative(expected, worlds.original());
            actual = Relative(actual, worlds.ours());
        }
        if (!S3RE_EXPECT_EQ(expected, actual) || (i % 512 == 511 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 511 before it): (%u, %u)", i, x, y);
            return;
        }
    }
    S3RE_EXPECT(worlds.Checkpoint(ctx));
}

// Wrong: leaves out the last neighbour (x+1, y+1).
std::uint32_t __fastcall WrongUpdateShadeAround(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    const std::uint8_t saved = world->tiles[x + 1][y + 1].shade;
    const std::uint32_t result = CGameData_UpdateShadeAround(world, nullptr, x, y);
    world->tiles[x + 1][y + 1].shade = saved;
    return result;
}

// Wrong: the centre tile becomes grass (the CSV's first guess).
std::uint32_t __fastcall WrongSetTerrain16Patch(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t result = CGameData_SetTerrain16Patch(world, nullptr, x, y);
    if (world->tiles[x][y].terrain == kTerrainField) {
        world->tiles[x][y].terrain = kTerrainGrass;
    }
    return result;
}

}  // namespace

S3RE_TEST(CGameData_UpdateShadeAround_MatchesOriginal) {
    CompareTileFn(ctx, Original_CGameData_UpdateShadeAround, &CGameData_UpdateShadeAround, 20000, false);
}
S3RE_TEST(CGameData_UpdateShadeAround_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareTileFn(probe, Original_CGameData_UpdateShadeAround, &WrongUpdateShadeAround, 2000, false);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_SetTerrain16Patch_MatchesOriginal) {
    CompareTileFn(ctx, Original_CGameData_SetTerrain16Patch, &CGameData_SetTerrain16Patch, 20000, true);
}
S3RE_TEST(CGameData_SetTerrain16Patch_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareTileFn(probe, Original_CGameData_SetTerrain16Patch, &WrongSetTerrain16Patch, 20000, true);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
