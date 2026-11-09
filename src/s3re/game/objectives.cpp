// "New game objectives" (NGO; DebugLog "*** NGO enabled *** TimeOut: %u
// min", "NGO Good: %u random: %u" in Volker\LogicInit.cpp).
//
// World byte +0x1195dcc is non-zero when they are enabled; the seven bytes
// +0x1195dcd..+0x1195dd3 are the chosen objectives: 0x4ffbc0 draws them
// with GameWorld::Random as 1..34 (Random % 34 + 1), skipping ids already
// chosen and 21, 25, 27..31, 33, 34, 35.
//
// GameWorld::IsNgoObjectiveChosen (0x004ffb90): (id) -> AL = 1 if one of
//   the seven bytes equals the low byte of `id`, else 0; the upper bits of
//   EAX are the caller's (only AL is used). thiscall, ret 4.

#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>

namespace s3re::game {

namespace {

constexpr std::uint32_t kNgoEnabled = 0x1195dcc;
constexpr std::uint32_t kNgoObjectives = 0x1195dcd;
constexpr std::uint32_t kNgoObjectiveCount = 7;

}  // namespace

using IsChosenFn = bool(__fastcall*)(GameWorld* world, void* edx, std::uint32_t id);

// Purpose: Whether an objective has already been chosen for this game's "new game objectives" (used while
// drawing the seven objectives at random, so none is picked twice).
bool __fastcall GameWorld_IsNgoObjectiveChosen(GameWorld* world, void*, std::uint32_t id) {
    const std::uint8_t* chosen = reinterpret_cast<const std::uint8_t*>(world) + kNgoObjectives;
    for (std::uint32_t i = 0; i < kNgoObjectiveCount; ++i) {
        if (chosen[i] == static_cast<std::uint8_t>(id)) {
            return true;
        }
    }
    return false;
}

S3RE_HOOK(0x004ffb90, GameWorld_IsNgoObjectiveChosen, IsChosenFn, Original_GameWorld_IsNgoObjectiveChosen,
          GameWorld_IsNgoObjectiveChosen);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kTestWorld = kNgoObjectives + 0x100;

void CompareIsChosen(test::Context& ctx, IsChosenFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        for (std::uint32_t k = 0; k < kNgoObjectiveCount + 1; ++k) {  // one byte past the list too
            worlds.Set<std::uint8_t>(kNgoObjectives + k, static_cast<std::uint8_t>(ctx.rng().Below(40)));
        }
        worlds.Set<std::uint8_t>(kNgoEnabled, static_cast<std::uint8_t>(ctx.rng().Below(2)));
        const std::uint32_t id = (ctx.rng().Chance(1, 4) ? ctx.rng().Next32() & ~0xffu : 0) | ctx.rng().Below(40);
        const bool expected = Original_GameWorld_IsNgoObjectiveChosen(worlds.original(), nullptr, id);
        const bool actual = candidate(worlds.ours(), nullptr, id);
        if (!S3RE_EXPECT_EQ(expected, actual) || (i % 1024 == 1023 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: id 0x%x", i, id);
            return;
        }
    }
}

// Wrong: looks at the first six objectives only.
bool __fastcall WrongIsNgoObjectiveChosen(GameWorld* world, void*, std::uint32_t id) {
    const std::uint8_t* chosen = reinterpret_cast<const std::uint8_t*>(world) + kNgoObjectives;
    for (std::uint32_t i = 0; i < kNgoObjectiveCount - 1; ++i) {
        if (chosen[i] == static_cast<std::uint8_t>(id)) {
            return true;
        }
    }
    return false;
}

}  // namespace

S3RE_TEST(GameWorld_IsNgoObjectiveChosen_MatchesOriginal) {
    CompareIsChosen(ctx, &GameWorld_IsNgoObjectiveChosen, 20000);
}
S3RE_TEST(GameWorld_IsNgoObjectiveChosen_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareIsChosen(probe, &WrongIsNgoObjectiveChosen, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
