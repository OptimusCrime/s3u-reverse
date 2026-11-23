// Coarse map of which players are present where.
//
// The map is divided into blocks of 16 x 16 tiles; block (bx, by) has a
// 20-byte record at world+0xde7448 + bx*960 + by*20 (48 blocks per row of
// 0x300 tiles) with one byte per player, non-zero where that player is
// present. The uint32 at world+0xde7444 is the highest player index the
// scans look at. Player records (0x148 bytes at world+0x11943dc): int32
// +0xc is the player's own index in these bytes (or a team index; see
// CGameData::FindPlayerByField0C).
//
// CGameData::IsOtherPlayerNear (0x00510650): (x, y, radius, player) -> 1 if
//   (x, y) is on the map (CGameData::IsOnMap) and some block overlapping
//   [x-radius, x+radius] x [y-radius, y+radius] (clamped to the map, unsigned
//   arithmetic: x - radius only if x >= radius, x + radius only if x <
//   mapSize - radius) has a non-zero byte for an index 0..max other than
//   the player's +0xc value; else 0. Callers 0x50e9f0 (twice). ret 0x10.
// thiscall (ECX = world).

#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>

namespace s3re::game {

namespace {

// presence_a[bx][by]: 48 blocks per row of 0x300 tiles.
static_assert(sizeof(CGameData::presence_a[0]) == 960);

}  // namespace

using NearFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y,
                                          std::uint32_t radius, std::uint32_t player);

// Purpose: Whether any other player is present near a tile, using a coarse map of who is where. Probably
// used for military or territory decisions.
std::uint32_t __fastcall CGameData_IsOtherPlayerNear(CGameData* world, void*, std::uint32_t x, std::uint32_t y,
                                                     std::uint32_t radius, std::uint32_t player) {
    if (!CGameData_IsOnMap(world, nullptr, x, y)) {
        return 0;
    }
    const std::uint32_t size = world->map_size;
    const std::uint32_t x0 = x >= radius ? x - radius : 0;
    const std::uint32_t x1 = x < size - radius ? x + radius : size - 1;
    const std::uint32_t y0 = y >= radius ? y - radius : 0;
    const std::uint32_t y1 = y < size - radius ? y + radius : size - 1;
    const auto own = static_cast<std::uint32_t>(world->players[player].team);
    // The original walks the blocks with y outermost and checks for a find
    // before each block; the scan has no side effects, so any order gives
    // the same answer.
    for (std::uint32_t by = y0 >> 4; by <= y1 >> 4; ++by) {
        for (std::uint32_t bx = x0 >> 4; bx <= x1 >> 4; ++bx) {
            // index runs up to presence_max_player (re-read each step), which
            // may lie past the block's 20 bytes; the original reads on.
            const std::uint8_t* block = world->presence_a[bx][by].player;
            for (std::uint32_t index = 0; index <= world->presence_max_player; ++index) {
                if (index != own && block[index] != 0) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

S3RE_HOOK(0x00510650, CGameData_IsOtherPlayerNear, NearFn, Original_CGameData_IsOtherPlayerNear,
          CGameData_IsOtherPlayerNear);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kTestWorld = offsetof(CGameData, players) + 21 * sizeof(layout::Player);

// Wrong: does not clamp the far edge to the map.
std::uint32_t __fastcall WrongIsOtherPlayerNear(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                std::uint32_t radius, std::uint32_t player) {
    std::uint32_t& size = world->map_size;
    const std::uint32_t saved = size;
    if (!CGameData_IsOnMap(world, edx, x, y)) {
        return 0;
    }
    size = saved + 32;  // the far edge may now reach blocks beyond the map
    const std::uint32_t result = CGameData_IsOtherPlayerNear(world, edx, x, y, radius, player);
    size = saved;
    return result;
}

void CompareIsOtherPlayerNear(test::Context& ctx, NearFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = ctx.rng().Chance(1, 2) ? 384 : 16 + ctx.rng().Below(368);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        const std::uint32_t players = ctx.rng().Below(20);
        worlds.Both([&](CGameData* w) { w->presence_max_player = players; });
        // Sparse presence: mostly zero bytes, a few players here and there.
        const std::uint32_t density = ctx.rng().Below(64);
        for (std::uint32_t bx = 0; bx < 26; ++bx) {
            for (std::uint32_t by = 0; by < 26; ++by) {
                for (std::uint32_t index = 0; index < 20; ++index) {
                    const auto value = static_cast<std::uint8_t>(ctx.rng().Below(1024) < density ? 1 : 0);
                    worlds.Both([&](CGameData* w) { w->presence_a[bx][by].player[index] = value; });
                }
            }
        }
        const std::uint32_t player = ctx.rng().Below(20);
        const std::uint32_t ownIndex = ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(20);
        worlds.Both([&](CGameData* w) { w->players[player].team = static_cast<std::int32_t>(ownIndex); });
        std::uint32_t x = ctx.rng().Below(size);
        std::uint32_t y = ctx.rng().Below(size);
        if (ctx.rng().Chance(1, 16)) {
            x = size + ctx.rng().Below(10);  // off the map
        }
        const std::uint32_t radius = ctx.rng().Below(40);
        const std::uint32_t expected = Original_CGameData_IsOtherPlayerNear(worlds.original(), nullptr, x, y, radius, player);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, x, y, radius, player);
        if (!S3RE_EXPECT_EQ(expected, actual)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%u, %u) radius %u player %u, map %u", i, x, y, radius, player, size);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

}  // namespace

S3RE_TEST(CGameData_IsOtherPlayerNear_MatchesOriginal) {
    CompareIsOtherPlayerNear(ctx, &CGameData_IsOtherPlayerNear, 3000);
}
S3RE_TEST(CGameData_IsOtherPlayerNear_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareIsOtherPlayerNear(probe, &WrongIsOtherPlayerNear, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
