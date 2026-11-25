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
//   0x00593040 AI::Army::GroupFightingShareAbove(player, group, percent)
//              thiscall (on g_aiGroupOrders, unused), ret 0xc: 1 if more
//              than `percent` % of the unit group's members
//              (CGameData::player_lists of g_aiMapWorld, index player * 10
//              + group, not range-checked) are fighting (job UnitFighting,
//              142), else 0; for an empty group AL = 0 with the index's
//              upper bytes in EAX (reproduced; the caller tests AL). The
//              share is x87 code: the members counted as a float sum of
//              1.0, divided by the length (as an unsigned 64-bit integer),
//              times 100.0, compared with the integer percent. At the
//              thread's default 53-bit precision every step is an IEEE
//              double operation, so double arithmetic in the same order
//              gives the same result. Was GroupJob142ShareAbove. Caller
//              AI::RunMilitary 0x58f990 with 22 (before "Gathering").
//   0x005934a0 AI::Army::PruneHunters(player) thiscall (on g_aiGroupOrders),
//              ret 4: walks the player's {hunter, target} vector
//              (g_aiHunterVectors) and erases each entry without a target
//              or whose target settler stands on a tile whose economy area
//              belongs to another player, clearing the hunter's bit in the
//              settler bitset (+0xca8; an index >= 32000 calls the bitset's
//              _Xran 0x596570 first). Original quirk, kept: after an erase
//              the walk still advances, so the entry moved into the erased
//              slot is skipped. EAX = the vector's end.

#include "game/ai/ai_layout.h"
#include "game/enums/job_ids.h"
#include "game/globals.h"
#include "game/layout.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using namespace ai;
using layout::kPlayers;

namespace {

constexpr std::int32_t kInitialGroupSize = 7;
constexpr std::uint32_t kGroupSlots = 100;   // settlers per unit group list (CGameData::player_lists)
constexpr std::uint32_t kOrderBits = 32000;  // the settler bitset's size
constexpr double kPercent = 100.0;

constexpr std::uintptr_t kBitsetXran = 0x00596570;  // std::bitset::_Xran (throws)

// An entry of a hunter vector (g_aiHunterVectors).
struct HunterEntry {
    std::uint32_t hunter;  // +0 the settler sent hunting
    std::uint32_t target;  // +4 the settler it hunts, 0 = none
};
static_assert(sizeof(HunterEntry) == 8);

// Where each group gathers relative to the gather position (0x591f13..
// 0x591f9a): groups 1..9; group 0's entry is left as it is.
constexpr AiGroupPosition kGatherOffsets[kUnitGroups] = {
    {0, 0}, {0, 0}, {9, 18}, {-9, -18}, {-18, 0}, {18, 0}, {-9, 18}, {27, 18}, {-27, -18}, {18, -27},
};

}  // namespace

using ArmyResetFn = AiGroupOrders*(__fastcall*)(AiGroupOrders* self, void* edx);
using FightingShareFn = std::uint32_t(__fastcall*)(AiGroupOrders* self, void* edx, std::uint32_t player,
                                                   std::uint32_t group, std::int32_t percent);
using PruneHuntersFn = std::uint32_t(__fastcall*)(AiGroupOrders* self, void* edx, std::uint32_t player);

AiGroupOrders* __fastcall AI_Army_Reset(AiGroupOrders* self, void* edx);
std::uint32_t __fastcall AI_Army_GroupFightingShareAbove(AiGroupOrders* self, void* edx, std::uint32_t player,
                                                         std::uint32_t group, std::int32_t percent);
std::uint32_t __fastcall AI_Army_PruneHunters(AiGroupOrders* self, void* edx, std::uint32_t player);

S3RE_HOOK(0x00591e00, AI_Army_Reset, ArmyResetFn, Original_AI_Army_Reset, AI_Army_Reset);
S3RE_HOOK(0x00593040, AI_Army_GroupFightingShareAbove, FightingShareFn, Original_AI_Army_GroupFightingShareAbove,
          AI_Army_GroupFightingShareAbove);
S3RE_HOOK(0x005934a0, AI_Army_PruneHunters, PruneHuntersFn, Original_AI_Army_PruneHunters, AI_Army_PruneHunters);

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
        self->thief_cursors[p] = 0;
        for (std::size_t group = 0; group < kUnitGroups; ++group) {
            self->group_flags1c48[p][group] = 0;
            self->group_targets[p][group] = AiGroupTarget{0, 0, 0};
            self->home_areas[p][group] = 0;
            self->home_buildings[p][group] = 0;
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

// Purpose: Whether more than a given share of a computer player's unit
// group is fighting, for the military AI's decision to gather or attack.
std::uint32_t __fastcall AI_Army_GroupFightingShareAbove(AiGroupOrders* /*self*/, void* /*edx*/,
                                                         std::uint32_t player, std::uint32_t group,
                                                         std::int32_t percent) {
    const CGameData* world = g_aiMapWorld;
    const std::uint32_t index = player * kUnitGroups + group;
    const auto length = static_cast<std::uint32_t>((&world->player_list_lengths[0][0])[index]);
    if (length == 0) {
        return index & 0xffffff00u;  // AL = 0 on top of the index arithmetic
    }
    const std::uint16_t* members = &world->player_lists[0][0][0] + index * kGroupSlots;
    double fighting = 0.0;
    for (std::uint32_t i = 0; i < length; ++i) {
        if (world->settlers[members[i]].job == SettlerJob::UnitFighting) {
            fighting += 1.0;
        }
    }
    const double share = fighting / static_cast<double>(length) * kPercent;
    return static_cast<double>(percent) < share ? 1 : 0;
}

// Purpose: Drops the hunters of a computer player whose prey is gone or
// has left the player's land, so that they can get new orders.
std::uint32_t __fastcall AI_Army_PruneHunters(AiGroupOrders* self, void* /*edx*/, std::uint32_t player) {
    AiVector& hunters = g_aiHunterVectors.get()[player];
    auto* entry = reinterpret_cast<HunterEntry*>(hunters.begin);
    if (hunters.begin >= hunters.end) {
        return hunters.end;
    }
    do {
        if (entry->target != 0) {
            const CGameData* world = g_aiMapWorld;
            const layout::Settler& prey = world->settlers[entry->target];  // (not range-checked)
            const std::uint16_t area = (&world->tiles[0][0])[prey.x * layout::kMapRowTiles + prey.y].area;
            if (world->areas[area].player == player) {
                ++entry;
                continue;  // still on the player's land
            }
        }
        const std::uint32_t hunter = entry->hunter;
        if (hunter >= kOrderBits) {
            using XranFn = void(__fastcall*)(void* self, void* edx);
            reinterpret_cast<XranFn>(kBitsetXran)(self->settlers_with_orders, nullptr);
        }
        self->settlers_with_orders[hunter >> 3] &= static_cast<std::uint8_t>(~(1u << (hunter & 7)));
        // The vector's erase(entry): the rest moves down one place.
        const auto* end = reinterpret_cast<const HunterEntry*>(hunters.end);
        for (HunterEntry* moved = entry + 1; moved != end; ++moved) {
            moved[-1] = *moved;
        }
        hunters.end -= sizeof(HunterEntry);
        ++entry;  // original quirk: skips the entry that moved into this slot
    } while (reinterpret_cast<std::uint32_t>(entry) < hunters.end);
    return hunters.end;
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

// --- GroupFightingShareAbove and PruneHunters ------------------------------

struct ArmyQueries {
    FightingShareFn fightingShare;
    PruneHuntersFn pruneHunters;
};

ArmyQueries OriginalArmyQueries() {
    return {Original_AI_Army_GroupFightingShareAbove, Original_AI_Army_PruneHunters};
}

ArmyQueries OurArmyQueries() {
    return {&AI_Army_GroupFightingShareAbove, &AI_Army_PruneHunters};
}

constexpr std::uint32_t kTestTiles = 64;   // prey stand on tiles 0..63 x 0..63
constexpr std::uint16_t kTestAreas = 8;
constexpr std::uint32_t kMaxHunters = 24;

void SetUpUnitGroup(test::Rng& rng, CGameData* world, std::uint32_t player, std::uint32_t group) {
    const std::uint32_t length = rng.Chance(1, 8) ? 0 : 1 + rng.Below(rng.Chance(1, 2) ? 10 : kGroupSlots);
    world->player_list_lengths[player][group] = static_cast<std::int32_t>(length);
    const std::uint32_t share = rng.Below(101);  // fighting members in percent (roughly)
    for (std::uint32_t i = 0; i < length; ++i) {
        const auto settler = static_cast<std::uint16_t>(1 + player * 1000 + group * 100 + i);
        world->player_lists[player][group][i] = settler;
        world->settlers[settler].job = rng.Below(100) < share ? SettlerJob::UnitFighting
                                                              : static_cast<SettlerJob>(rng.Below(300));
    }
}

void SetUpPrey(test::Rng& rng, CGameData* world) {
    for (std::uint32_t x = 0; x < kTestTiles; ++x) {
        for (std::uint32_t y = 0; y < kTestTiles; ++y) {
            world->tiles[x][y].area = static_cast<std::uint16_t>(rng.Below(kTestAreas));
        }
    }
    for (std::uint16_t area = 0; area < kTestAreas; ++area) {
        world->areas[area].player = static_cast<std::uint8_t>(rng.Chance(1, 8) ? 0xff : rng.Below(3));
    }
    for (std::uint32_t settler = 1; settler < 200; ++settler) {
        world->settlers[settler].x = static_cast<std::uint16_t>(rng.Below(kTestTiles));
        world->settlers[settler].y = static_cast<std::uint16_t>(rng.Below(kTestTiles));
    }
}

void CompareArmyQueries(test::Context& ctx, const ArmyQueries& candidate, int cases) {
    const ArmyQueries original = OriginalArmyQueries();
    CGameData* savedMapWorld = g_aiMapWorld;
    AiVector savedVectors[kPlayers];
    std::memcpy(savedVectors, &g_aiHunterVectors.get(), sizeof(savedVectors));
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    g_aiMapWorld = world;
    std::vector<std::uint8_t> armyA(sizeof(AiGroupOrders)), armyB(armyA.size());
    HunterEntry huntersA[kMaxHunters + 1], huntersB[kMaxHunters + 1];
    auto* selfA = reinterpret_cast<AiGroupOrders*>(armyA.data());
    auto* selfB = reinterpret_cast<AiGroupOrders*>(armyB.data());
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        const std::uint32_t player = rng.Below(3);
        std::uint32_t ra = 0, rb = 0;
        bool same = true;
        if (rng.Chance(1, 2)) {
            const std::uint32_t group = rng.Below(static_cast<std::uint32_t>(kUnitGroups));
            SetUpUnitGroup(rng, world, player, group);
            const std::int32_t percent = rng.Chance(9, 10) ? static_cast<std::int32_t>(rng.Below(101))
                                                           : static_cast<std::int32_t>(rng.Next32());
            ra = original.fightingShare(selfA, nullptr, player, group, percent);
            rb = candidate.fightingShare(selfB, nullptr, player, group, percent);
            same &= S3RE_EXPECT_EQ(ra, rb);
        } else {
            if (i % 50 == 0) {
                SetUpPrey(rng, world);
            }
            ctx.rng().Fill(armyA.data(), armyA.size());
            armyB = armyA;
            const std::uint32_t count = rng.Chance(1, 10) ? 0 : 1 + rng.Below(kMaxHunters);
            for (std::uint32_t k = 0; k <= kMaxHunters; ++k) {
                huntersA[k].hunter = rng.Below(kOrderBits);
                huntersA[k].target = rng.Chance(1, 5) ? 0 : 1 + rng.Below(199);
            }
            std::memcpy(huntersB, huntersA, sizeof(huntersA));
            AiVector& vector = g_aiHunterVectors.get()[player];
            vector.begin = reinterpret_cast<std::uint32_t>(&huntersA[0]);
            vector.end = reinterpret_cast<std::uint32_t>(&huntersA[count]);
            ra = original.pruneHunters(selfA, nullptr, player) - reinterpret_cast<std::uint32_t>(&huntersA[0]);
            const std::uint32_t endA = vector.end - reinterpret_cast<std::uint32_t>(&huntersA[0]);
            vector.begin = reinterpret_cast<std::uint32_t>(&huntersB[0]);
            vector.end = reinterpret_cast<std::uint32_t>(&huntersB[count]);
            rb = candidate.pruneHunters(selfB, nullptr, player) - reinterpret_cast<std::uint32_t>(&huntersB[0]);
            const std::uint32_t endB = vector.end - reinterpret_cast<std::uint32_t>(&huntersB[0]);
            same &= S3RE_EXPECT_EQ(ra, rb);
            same &= S3RE_EXPECT_EQ(endA, endB);
            same &= S3RE_EXPECT_MEM_EQ(huntersA, huntersB, sizeof(huntersA));
            same &= S3RE_EXPECT_MEM_EQ(armyA.data(), armyB.data(), armyA.size());
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, player %u", i, player);
        }
    }
    g_aiMapWorld = savedMapWorld;
    std::memcpy(&g_aiHunterVectors.get(), savedVectors, sizeof(savedVectors));
}

std::uint32_t __fastcall WrongFightingShare(AiGroupOrders* /*self*/, void* /*edx*/, std::uint32_t player,
                                            std::uint32_t group, std::int32_t percent) {
    // Wrong: "at least" instead of "more than".
    const CGameData* world = g_aiMapWorld;
    const auto length = static_cast<std::uint32_t>(world->player_list_lengths[player][group]);
    if (length == 0) {
        return (player * kUnitGroups + group) & 0xffffff00u;
    }
    double fighting = 0.0;
    for (std::uint32_t i = 0; i < length; ++i) {
        if (world->settlers[world->player_lists[player][group][i]].job == SettlerJob::UnitFighting) {
            fighting += 1.0;
        }
    }
    return static_cast<double>(percent) <= fighting / length * kPercent ? 1 : 0;
}

std::uint32_t __fastcall WrongPruneHunters(AiGroupOrders* self, void* /*edx*/, std::uint32_t player) {
    // Wrong: doesn't skip the entry that moved into an erased slot.
    AiVector& hunters = g_aiHunterVectors.get()[player];
    auto* entry = reinterpret_cast<HunterEntry*>(hunters.begin);
    while (reinterpret_cast<std::uint32_t>(entry) < hunters.end) {
        const CGameData* world = g_aiMapWorld;
        if (entry->target != 0) {
            const layout::Settler& prey = world->settlers[entry->target];
            if (world->areas[world->tiles[prey.x][prey.y].area].player == player) {
                ++entry;
                continue;
            }
        }
        self->settlers_with_orders[entry->hunter >> 3] &= static_cast<std::uint8_t>(~(1u << (entry->hunter & 7)));
        const auto* end = reinterpret_cast<const HunterEntry*>(hunters.end);
        for (HunterEntry* moved = entry + 1; moved != end; ++moved) {
            moved[-1] = *moved;
        }
        hunters.end -= sizeof(HunterEntry);
    }
    return hunters.end;
}

}  // namespace

S3RE_TEST(AiArmyQueries_MatchOriginal) {
    CompareArmyQueries(ctx, OurArmyQueries(), 3000);
}

S3RE_TEST(AiArmyQueries_ComparisonDetectsWrongImplementation) {
    const ArmyQueries ours = OurArmyQueries();
    for (const ArmyQueries& wrong : {ArmyQueries{&WrongFightingShare, ours.pruneHunters},
                                     ArmyQueries{ours.fightingShare, &WrongPruneHunters}}) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareArmyQueries(quiet, wrong, 3000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

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
