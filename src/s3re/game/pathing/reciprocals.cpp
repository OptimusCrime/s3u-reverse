// CGameData::InitReciprocals (0x004a1eb0, was Logic_sub_4a1eb0; Dirk\Pathing.cpp
//   by its place, between nethelp.cpp and Pathing.cpp): () thiscall, from
//   CGameData::CGameData only. world->reciprocal[i] = 0x10000 / i (signed
//   division) for i = 1..767; [0] is left alone. FindPath's direction
//   estimate multiplies by these instead of dividing. EAX not used (it
//   holds 0x10000 / 767 = 85).

#include "game/layout.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game {

namespace {

constexpr std::int32_t kOne = 0x10000;  // 1.0 in 16.16 fixed point
constexpr std::int32_t kReciprocals = 768;

}  // namespace

using InitReciprocalsFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx);

// Purpose: Fills the world's table of reciprocals (65536 / i), used by the
// path finder to avoid divisions.
std::uint32_t __fastcall CGameData_InitReciprocals(CGameData* world, void*) {
    std::int32_t value = 0;
    for (std::int32_t i = 1; i < kReciprocals; ++i) {
        value = kOne / i;
        world->reciprocal[i] = value;
    }
    return static_cast<std::uint32_t>(value);
}

S3RE_HOOK(0x004a1eb0, CGameData_InitReciprocals, InitReciprocalsFn, Original_CGameData_InitReciprocals,
          CGameData_InitReciprocals);

namespace {

void CompareReciprocals(test::Context& ctx, InitReciprocalsFn candidate, int cases) {
    constexpr std::size_t kSize = offsetof(CGameData, reciprocal) + sizeof(CGameData::reciprocal) + 64;
    constexpr std::size_t kFrom = offsetof(CGameData, reciprocal) - 64;
    for (int i = 0; i < cases; ++i) {
        test::WorldPair worlds(ctx, kSize);
        const std::uint32_t a = Original_CGameData_InitReciprocals(worlds.original(), nullptr);
        const std::uint32_t b = candidate(worlds.ours(), nullptr);
        if (!S3RE_EXPECT_EQ(a, b) || !worlds.Same(ctx, kFrom, kSize - kFrom)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

// Wrong: [0] gets 0x10000 too.
std::uint32_t __fastcall WrongInitReciprocals(CGameData* world, void* edx) {
    const std::uint32_t result = CGameData_InitReciprocals(world, edx);
    world->reciprocal[0] = kOne;
    return result;
}

}  // namespace

S3RE_TEST(CGameData_InitReciprocals_MatchesOriginal) {
    CompareReciprocals(ctx, &CGameData_InitReciprocals, 3);
}

S3RE_TEST(CGameData_InitReciprocals_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareReciprocals(probe, &WrongInitReciprocals, 1);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
