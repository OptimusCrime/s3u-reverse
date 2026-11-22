// The per-step world maintenance GameWorld::UpdateEnvironment (0x4e7810) and
// its small helpers.
//
// GameWorld::DecayTraffic (0x00503980): (x, y): foot traffic (Tile +0x12)
//   decays: t -= (t + 7) >> 3 (byte arithmetic). On a building footprint
//   (flags & 8) a result of 7 becomes 8; elsewhere a result of 7 on a worn
//   path (terrain 0x1c) turns the tile back into grass (0x10) and marks it
//   for redrawing (draw_flags | 0x80). UpdateEnvironment calls it for one
//   tile per step (cursor world+0x2d2f4 / +0x2d2f8). ret 8, EAX not used.
// thiscall (ECX = world).

#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>

namespace s3re::game {

using layout::Tile;

using TileFn = void(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);

// Purpose: Lets the foot traffic on a tile fade, so that paths nobody walks any more grow back into grass.
void __fastcall GameWorld_DecayTraffic(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    Tile& tile = TileAtOffset(world, TileOffset(x, y));
    const std::uint8_t traffic = tile.traffic;
    if (traffic == 0) {
        return;
    }
    const auto decayed = static_cast<std::uint8_t>(traffic - ((traffic + 7) >> 3));
    tile.traffic = decayed;
    if (decayed != 7) {
        return;
    }
    if ((tile.flags & layout::kTileBuildingFootprint) != 0) {
        tile.traffic = 8;
    } else if (tile.terrain == 0x1c) {
        tile.terrain = 0x10;
        tile.draw_flags |= layout::kTileDrawRedraw;
    }
}

S3RE_HOOK(0x00503980, GameWorld_DecayTraffic, TileFn, Original_GameWorld_DecayTraffic, GameWorld_DecayTraffic);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kTestWorld = offsetof(GameWorld, tiles) + 0x40 * kTileRowBytes;

// Wrong: a footprint tile keeps 7.
void __fastcall WrongDecayTraffic(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    GameWorld_DecayTraffic(world, nullptr, x, y);
    Tile& tile = world->tiles[x][y];
    if (tile.traffic == 8 && (tile.flags & layout::kTileBuildingFootprint) != 0) {
        tile.traffic = 7;
    }
}

void CompareDecay(test::Context& ctx, TileFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t x = ctx.rng().Below(0x40);
        const std::uint32_t y = ctx.rng().Below(0x300);
        const auto traffic = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 7 + ctx.rng().Below(3) : ctx.rng().Below(256));
        const auto terrain = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0x1c : ctx.rng().Below(256));
        const auto flags = static_cast<std::uint8_t>(ctx.rng().Next32());
        worlds.Both([&](GameWorld* w) {
            w->tiles[x][y].traffic = traffic;
            w->tiles[x][y].terrain = terrain;
            w->tiles[x][y].flags = flags;
        });
        Original_GameWorld_DecayTraffic(worlds.original(), nullptr, x, y);
        candidate(worlds.ours(), nullptr, x, y);
        if (!worlds.Same(ctx, TileOffset(x, y), sizeof(Tile))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%u, %u) traffic %u", i, x, y, traffic);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

}  // namespace

S3RE_TEST(GameWorld_DecayTraffic_MatchesOriginal) {
    CompareDecay(ctx, &GameWorld_DecayTraffic, 50000);
}

S3RE_TEST(GameWorld_DecayTraffic_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDecay(probe, &WrongDecayTraffic, 5000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
