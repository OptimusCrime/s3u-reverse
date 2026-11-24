// Lookups in the computer players' per-area data (computer players A2,
// session 53; Rainer\compGlobs.cpp; re/computer_players.md). The land
// evaluation (g_aiLandEvaluation) keeps one circular list per player
// (AiLandEvaluation::lists, sentinel head node) of AiPlayerData nodes, one
// per economy area, keyed by AiPlayerData::area.
//
//   0x00573be0 AI::FindNode(AiPlayerData** out, player, area) fastcall
//              (ECX = out, DX = player; stack dword, its low word the area),
//              ret 4: *out = the first node of the player's list (from the
//              head's next) whose area word equals `area`, else 0 (the
//              player is not range-checked). EAX = out.
//   0x00572fa0 AI::IsForceProduction(player, area) fastcall: FindNode's
//              node -> its force_production (+0x18, set by
//              COMP_ForceProduction 0x572fc0). No null check: an area
//              without a node faults, in ours as in the original.

#include "game/ai/ai_layout.h"
#include "game/globals.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace s3re::game {

using namespace ai;

namespace {

constexpr std::uintptr_t kFindNode = 0x00573be0;  // AI::FindNode (fastcall)

}  // namespace

using FindNodeFn = AiPlayerData** (__fastcall*)(AiPlayerData** out, std::uint32_t player, std::uint32_t area);
using ForceProductionFn = std::uint32_t(__fastcall*)(std::uint32_t player, std::uint32_t area);

AiPlayerData** __fastcall AI_FindNode(AiPlayerData** out, std::uint32_t player, std::uint32_t area);
std::uint32_t __fastcall AI_IsForceProduction(std::uint32_t player, std::uint32_t area);

S3RE_HOOK(0x00573be0, AI_FindNode, FindNodeFn, Original_AI_FindNode, AI_FindNode);
S3RE_HOOK(0x00572fa0, AI_IsForceProduction, ForceProductionFn, Original_AI_IsForceProduction,
          AI_IsForceProduction);

// Purpose: Finds a computer player's data for one economy area (its build
// priorities, building counts and flags).
AiPlayerData** __fastcall AI_FindNode(AiPlayerData** out, std::uint32_t player, std::uint32_t area) {
    const auto* land = reinterpret_cast<const AiLandEvaluation*>(g_aiLandEvaluation.get());
    const AiListNode* head = land->lists[player & 0xffff].head;  // (not range-checked)
    for (auto* node = reinterpret_cast<AiPlayerData*>(head->next); node != reinterpret_cast<const void*>(head);
         node = reinterpret_cast<AiPlayerData*>(node->next)) {
        if (node->area == static_cast<std::uint16_t>(area)) {
            *out = node;
            return out;
        }
    }
    *out = nullptr;
    return out;
}

// Purpose: Whether the computer player has ordered forced production in an
// economy area (probably to get goods it urgently needs).
std::uint32_t __fastcall AI_IsForceProduction(std::uint32_t player, std::uint32_t area) {
    AiPlayerData* node = nullptr;
    reinterpret_cast<FindNodeFn>(kFindNode)(&node, player, area);
    return node->force_production;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Three players' lists of 0..6 nodes with areas 0..9 (duplicates possible:
// the first counts), player and area arguments with random upper halves.
// IsForceProduction is only asked for areas that have a node.

namespace {

constexpr std::uint32_t kTestPlayers = 3;
constexpr std::uint32_t kMaxNodes = 6;

struct NodeLookups {
    FindNodeFn findNode;
    ForceProductionFn forceProduction;
};

void CompareNodeLookups(test::Context& ctx, const NodeLookups& candidate, int cases) {
    const NodeLookups original{Original_AI_FindNode, Original_AI_IsForceProduction};
    const std::uint32_t savedLand = g_aiLandEvaluation;
    AiLandEvaluation land{};
    std::vector<AiPlayerData> nodes(kTestPlayers * kMaxNodes);
    AiListNode heads[kTestPlayers] = {};
    g_aiLandEvaluation = reinterpret_cast<std::uint32_t>(&land);
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        std::uint16_t areas[kTestPlayers][kMaxNodes] = {};
        std::uint32_t counts[kTestPlayers] = {};
        for (std::uint32_t p = 0; p < kTestPlayers; ++p) {
            land.lists[p].head = &heads[p];
            counts[p] = rng.Below(kMaxNodes + 1);
            std::uint32_t previous = reinterpret_cast<std::uint32_t>(&heads[p]);
            for (std::uint32_t k = 0; k < counts[p]; ++k) {
                AiPlayerData& node = nodes[p * kMaxNodes + k];
                node.area = areas[p][k] = static_cast<std::uint16_t>(rng.Below(10));
                node.force_production = rng.Next32();
                node.next = reinterpret_cast<std::uint32_t>(&heads[p]);
                *reinterpret_cast<std::uint32_t*>(previous) = reinterpret_cast<std::uint32_t>(&node);
                previous = reinterpret_cast<std::uint32_t>(&node);
            }
            if (counts[p] == 0) {
                heads[p].next = reinterpret_cast<std::uint32_t>(&heads[p]);
            }
        }
        const std::uint32_t player = rng.Below(kTestPlayers);
        const std::uint32_t noise = rng.Chance(1, 4) ? rng.Next32() & 0xffff0000u : 0;
        bool same = true;
        if (rng.Chance(1, 2) || counts[player] == 0) {
            const std::uint32_t area = rng.Below(10) | (rng.Chance(1, 4) ? rng.Next32() & 0xffff0000u : 0);
            AiPlayerData* outA = reinterpret_cast<AiPlayerData*>(rng.Next32());
            AiPlayerData* outB = outA;
            AiPlayerData** ra = original.findNode(&outA, player | noise, area);
            AiPlayerData** rb = candidate.findNode(&outB, player | noise, area);
            same &= S3RE_EXPECT(ra == &outA);
            same &= S3RE_EXPECT(rb == &outB);
            same &= S3RE_EXPECT_EQ(reinterpret_cast<std::uintptr_t>(outA), reinterpret_cast<std::uintptr_t>(outB));
        } else {
            const std::uint32_t area = areas[player][rng.Below(counts[player])];
            same &= S3RE_EXPECT_EQ(original.forceProduction(player | noise, area),
                                   candidate.forceProduction(player | noise, area));
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, player %u", i, player);
        }
    }
    g_aiLandEvaluation = savedLand;
}

AiPlayerData** __fastcall WrongFindNode(AiPlayerData** out, std::uint32_t player, std::uint32_t area) {
    // Wrong: the last matching node.
    const auto* land = reinterpret_cast<const AiLandEvaluation*>(g_aiLandEvaluation.get());
    const AiListNode* head = land->lists[player & 0xffff].head;
    *out = nullptr;
    for (auto* node = reinterpret_cast<AiPlayerData*>(head->next); node != reinterpret_cast<const void*>(head);
         node = reinterpret_cast<AiPlayerData*>(node->next)) {
        if (node->area == static_cast<std::uint16_t>(area)) {
            *out = node;
        }
    }
    return out;
}

std::uint32_t __fastcall WrongForceProduction(std::uint32_t player, std::uint32_t area) {
    // Wrong: reads the dword after the flag.
    AiPlayerData* node = nullptr;
    AI_FindNode(&node, player, area);
    return (&node->force_production)[1];
}

}  // namespace

S3RE_TEST(AiPlayerNodes_MatchOriginal) {
    CompareNodeLookups(ctx, {&AI_FindNode, &AI_IsForceProduction}, 4000);
}

S3RE_TEST(AiPlayerNodes_ComparisonDetectsWrongImplementation) {
    for (const NodeLookups& wrong : {NodeLookups{&WrongFindNode, &AI_IsForceProduction},
                                     NodeLookups{&AI_FindNode, &WrongForceProduction}}) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareNodeLookups(quiet, wrong, 2000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

}  // namespace s3re::game
