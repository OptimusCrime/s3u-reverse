// The military AI's "army" object g_aiGroupOrders (Rainer\CompMapWorkerAgent.cpp;
// AiGroupOrders in ai_layout.h, re/computer_players.md).
//
//   0x00591e00 AI::Army::Reset() thiscall (on g_aiGroupOrders): clears the
//              settler bitset (+0xca8, 1000 dwords, from the top down);
//              per player the highest group (+0x1d10), the gather building
//              (+0x898), +0x8e8, the hunter vector (g_aiHunterVectors: end =
//              begin; the original's inlined erase(begin, end) copies
//              nothing) and per group the byte +0x1c48, the target
//              (+0x3e8), the home area (+0), +0x190, the steal flag
//              (+0x320) and the mage count (+0x988); the group size
//              (+0x1d60) becomes 7. Then the gather offsets of groups 1..9
//              (+0x20d4..+0x20f6; group 0's stays). EAX = this. Callers
//              AI::RunMilitary 0x58f990 (reset request), AI::Army::Army
//              0x591c10, AI::LoadState 0x596380.

#include "game/ai/ai_layout.h"
#include "game/globals.h"
#include "game/layout.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using namespace ai;
using layout::kPlayers;

namespace {

constexpr std::int32_t kInitialGroupSize = 7;

// Where each group gathers relative to the gather position (0x591f13..
// 0x591f9a): groups 1..9; group 0's entry is left as it is.
constexpr AiGroupPosition kGatherOffsets[kUnitGroups] = {
    {0, 0}, {0, 0}, {9, 18}, {-9, -18}, {-18, 0}, {18, 0}, {-9, 18}, {27, 18}, {-27, -18}, {18, -27},
};

}  // namespace

using ArmyResetFn = AiGroupOrders*(__fastcall*)(AiGroupOrders* self, void* edx);

AiGroupOrders* __fastcall AI_Army_Reset(AiGroupOrders* self, void* edx);

S3RE_HOOK(0x00591e00, AI_Army_Reset, ArmyResetFn, Original_AI_Army_Reset, AI_Army_Reset);

// Purpose: Resets the computer players' military orders (unit groups,
// targets, gather points, hunters) at game start, when the AI state is
// loaded and when the military AI asks for a fresh start.
AiGroupOrders* __fastcall AI_Army_Reset(AiGroupOrders* self, void* /*edx*/) {
    for (std::uint8_t& bits : self->settlers_with_orders) {
        bits = 0;
    }
    AiVector* hunters = g_aiHunterVectors;
    for (std::size_t p = 0; p < kPlayers; ++p) {
        self->highest_group[p] = 0;
        hunters[p].end = hunters[p].begin;  // the vector's clear()
        self->gather_buildings[p] = 0;
        self->per_player8e8[p] = 0;
        for (std::size_t group = 0; group < kUnitGroups; ++group) {
            self->group_flags1c48[p][group] = 0;
            self->group_targets[p][group] = AiGroupTarget{0, 0, 0};
            self->home_areas[p][group] = 0;
            self->unk190[p][group] = 0;
            self->group_flags[p][group] = 0;
            self->group_mages[p][group] = 0;
        }
        self->group_size[p] = kInitialGroupSize;
    }
    for (std::size_t group = 1; group < kUnitGroups; ++group) {
        self->gather_offsets[group] = kGatherOffsets[group];
    }
    return self;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kGuard = 32;

void CompareArmyReset(test::Context& ctx, ArmyResetFn candidate, int cases) {
    AiVector savedVectors[kAgentVectorCount];
    std::memcpy(savedVectors, &g_aiHunterVectors.get(), sizeof(savedVectors));
    std::vector<std::uint8_t> a(sizeof(AiGroupOrders) + kGuard), b(a.size());
    AiVector vectorsA[kAgentVectorCount], vectorsB[kAgentVectorCount];
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        ctx.rng().Fill(a.data(), a.size());
        b = a;
        ctx.rng().Fill(vectorsB, sizeof(vectorsB));
        std::memcpy(&g_aiHunterVectors.get(), vectorsB, sizeof(vectorsB));
        auto* ra = Original_AI_Army_Reset(reinterpret_cast<AiGroupOrders*>(a.data()), nullptr);
        std::memcpy(vectorsA, &g_aiHunterVectors.get(), sizeof(vectorsA));
        std::memcpy(&g_aiHunterVectors.get(), vectorsB, sizeof(vectorsB));
        auto* rb = candidate(reinterpret_cast<AiGroupOrders*>(b.data()), nullptr);
        bool same = S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size());
        same &= S3RE_EXPECT_MEM_EQ(vectorsA, &g_aiHunterVectors.get(), sizeof(vectorsA));
        same &= S3RE_EXPECT_EQ(reinterpret_cast<std::uint8_t*>(ra) - a.data(),
                               reinterpret_cast<std::uint8_t*>(rb) - b.data());
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
        }
    }
    std::memcpy(&g_aiHunterVectors.get(), savedVectors, sizeof(savedVectors));
}

AiGroupOrders* __fastcall WrongArmyResetOffsets(AiGroupOrders* self, void* edx) {
    // Wrong: also sets group 0's gather offset.
    AI_Army_Reset(self, edx);
    self->gather_offsets[0] = AiGroupPosition{0, 0};
    return self;
}

AiGroupOrders* __fastcall WrongArmyResetVectors(AiGroupOrders* self, void* edx) {
    // Wrong: leaves the last player's hunter vector alone.
    AiVector& last = g_aiHunterVectors.get()[kPlayers - 1];
    const AiVector kept = last;
    AI_Army_Reset(self, edx);
    last = kept;
    return self;
}

}  // namespace

S3RE_TEST(AiArmyReset_MatchesOriginal) {
    CompareArmyReset(ctx, &AI_Army_Reset, 200);
}

S3RE_TEST(AiArmyReset_ComparisonDetectsWrongImplementation) {
    for (ArmyResetFn wrong : {&WrongArmyResetOffsets, &WrongArmyResetVectors}) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareArmyReset(quiet, wrong, 20);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

}  // namespace s3re::game
