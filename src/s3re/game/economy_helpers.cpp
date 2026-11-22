// Small helpers of the settler and economy updates (UpdateSettlers,
// UpdateEconomy).
//
// GameWorld::CountOtherPileClaims (0x00510800): (settler, pile) -> how many
//   of the 128 entries at world+0x1195dd4 have word 0 == pile and word 1 !=
//   settler (both compared as 16-bit values). The only writer of that list
//   is GameWorld::AddPileClaim (0x510770; was AddToRecentPositions), called once, by
//   UpdateSettlers job 0x152, as (settler, pile): it stores {pile,
//   settler} (layout::PileClaim). So the list holds pile claims, not positions (the logic
//   thread clears one slot per step, so a claim expires after 128 steps);
//   job 0x152 compares this count with the pile's amount. ECX = world,
//   ret 8, EAX = the count.
// thiscall. Name ours. (0x50e200 is analysed in LOGBOOK.md, not done yet.)

#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game {

using ClaimsFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t settler, std::uint32_t pile);

// Purpose: Counts how many other settlers have recently claimed a pile, so a settler (job 0x152) does
// not head for a pile that others will already empty.
std::uint32_t __fastcall GameWorld_CountOtherPileClaims(GameWorld* world, void*, std::uint32_t settler,
                                                        std::uint32_t pile) {
    const auto pile16 = static_cast<std::uint16_t>(pile);
    const auto settler16 = static_cast<std::uint16_t>(settler);
    std::uint32_t count = 0;
    for (const auto& entry : world->pile_claims) {
        if (entry.pile == pile16 && entry.settler != settler16) {
            ++count;
        }
    }
    return count;
}

S3RE_HOOK(0x00510800, GameWorld_CountOtherPileClaims, ClaimsFn, Original_GameWorld_CountOtherPileClaims,
          GameWorld_CountOtherPileClaims);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kClaimsWorld = offsetof(GameWorld, pile_claims) + sizeof(GameWorld::pile_claims);

void CompareClaims(test::Context& ctx, ClaimsFn candidate, int cases) {
    test::WorldPair worlds(ctx, kClaimsWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t pile = ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(6);
        const std::uint32_t settler = ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(6);
        for (std::uint32_t slot = 0; slot < 128; ++slot) {
            const auto a = static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? pile : ctx.rng().Below(6));
            const auto b = static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? settler : ctx.rng().Below(6));
            worlds.Both([&](GameWorld* w) {
                w->pile_claims[slot].pile = a;
                w->pile_claims[slot].settler = b;
            });
        }
        const std::uint32_t expected = Original_GameWorld_CountOtherPileClaims(worlds.original(), nullptr, settler, pile);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, settler, pile);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: settler 0x%x pile 0x%x", i, settler, pile);
            return;
        }
    }
}

// Wrong: counts the settler's own claims too.
std::uint32_t __fastcall WrongCountOtherPileClaims(GameWorld* world, void*, std::uint32_t, std::uint32_t pile) {
    std::uint32_t count = 0;
    for (const auto& entry : world->pile_claims) {
        count += entry.pile == static_cast<std::uint16_t>(pile) ? 1u : 0u;
    }
    return count;
}

}  // namespace

S3RE_TEST(GameWorld_CountOtherPileClaims_MatchesOriginal) {
    CompareClaims(ctx, &GameWorld_CountOtherPileClaims, 5000);
}
S3RE_TEST(GameWorld_CountOtherPileClaims_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareClaims(probe, &WrongCountOtherPileClaims, 500);
    S3RE_EXPECT(probe.failures() > 0);
}
}  // namespace s3re::game
