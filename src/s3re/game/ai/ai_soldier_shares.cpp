// T_ECONOMY_AGENT::AdjustSoldierShares (0x0056a840; computer players,
// session 56): thiscall, no arguments; EAX not used (its caller, the
// economy agent's snapshot 0x567910, clobbers it).
//
// The population (EconomyAgent::population, a word) chooses a stage: above
// g_aiPopulationStages[3] / 2 (250) stage 3, above [2] (100) stage 2, above
// [1] (40) stage 1, else 0. Nothing happens while the area node's
// share_stage already holds that stage; at or below 40 also nothing when
// the population is exactly 40 and share_stage was set before (not 0xff:
// the original's `jae` on the equal case). Otherwise share_stage = stage
// and each of the area's three weapon shares (Area::settings_extra, kinds
// 1..3) moves towards g_aiWeaponShares[stage]: (target - current) / 5
// (signed, truncated) ChangeToolSettings commands (AI::SendChangeToolSettings
// 0x58e740 with the player, the area, the kind and the direction byte 1 up /
// 0 down), the current value read once per share.

#include "game/ai/ai_layout.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/tables.h"
#include "game/world.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace s3re::game {

using namespace ai;

namespace {

constexpr std::uintptr_t kSendChangeToolSettings = 0x0058e740;  // fastcall (player*, area*; kind*, up*)
constexpr std::uint8_t kNoStage = 0xff;
constexpr std::size_t kShares = 3;
constexpr std::int32_t kShareStep = 5;  // one command moves a share by 5

// 0x56a842..0x56a8cc: the stage for the population, or -1 when the shares
// stay as they are.
std::int32_t NewStage(std::uint16_t population, std::uint8_t current) {
    const auto& stages = g_aiPopulationStages;
    std::uint8_t stage = 0;
    if (population > static_cast<std::uint16_t>(stages[3] >> 1)) {
        stage = 3;
    } else if (population > stages[2]) {
        stage = 2;
    } else if (population > stages[1]) {
        stage = 1;
    } else if (current != kNoStage && population >= stages[1]) {
        return -1;  // (exactly 40, quirk kept)
    }
    return stage == current ? -1 : stage;
}

}  // namespace

using AdjustFn = void(__fastcall*)(EconomyAgent* self, void* edx);
void __fastcall T_ECONOMY_AGENT_AdjustSoldierShares(EconomyAgent* self, void* edx);
S3RE_HOOK(0x0056a840, T_ECONOMY_AGENT_AdjustSoldierShares, AdjustFn, Original_T_ECONOMY_AGENT_AdjustSoldierShares,
          T_ECONOMY_AGENT_AdjustSoldierShares);

// Purpose: The economy agent's weapon split: as the area's population grows
// through the stages, the computer player sends ChangeToolSettings commands
// that move the area's three weapon shares towards the stage's split.
void __fastcall T_ECONOMY_AGENT_AdjustSoldierShares(EconomyAgent* self, void* /*edx*/) {
    AiPlayerData* data = g_aiPlayerData;
    const std::int32_t stage = NewStage(self->population, data->share_stage);
    if (stage < 0) {
        return;
    }
    data->share_stage = static_cast<std::uint8_t>(stage);
    const std::uint8_t* targets = g_aiWeaponShares[g_aiPlayerData.get()->share_stage];
    for (std::size_t k = 0; k < kShares; ++k) {
        // The area is not range-checked (the original's address arithmetic).
        const layout::Area* area = g_aiWorld.get()->areas + self->area;
        const std::uint8_t current = area->settings_extra[k];
        if (current == targets[k]) {
            continue;
        }
        const std::int32_t steps = (static_cast<std::int32_t>(targets[k]) - current) / kShareStep;
        std::uint8_t up = steps > 0 ? 1 : 0;
        for (std::int32_t n = std::abs(steps); n > 0; --n) {
            std::uint8_t kind = static_cast<std::uint8_t>(k + 1);
            using SendFn = std::uint32_t(__fastcall*)(const void*, const void*, const void*, const void*);
            reinterpret_cast<SendFn>(kSendChangeToolSettings)(&g_aiPlayerNumber.get(), &self->area, &kind, &up);
        }
    }
}

// ---------------------------------------------------------------------------
// Tests: SendChangeToolSettings stubbed (it records the four bytes it is
// given); the area records in a zeroed world.
// ---------------------------------------------------------------------------

namespace {

struct ShareCall {
    std::uint8_t player, kind, up;
    std::uint16_t area;
    bool operator==(const ShareCall& other) const {
        return player == other.player && kind == other.kind && up == other.up && area == other.area;
    }
};

void CompareShares(test::Context& ctx, AdjustFn candidate, int cases) {
    CGameData* const savedWorld = g_aiWorld;
    AiPlayerData* const savedData = g_aiPlayerData;
    const std::uint8_t savedNumber = g_aiPlayerNumber;
    test::WorldBuffer world(sizeof(CGameData));
    g_aiWorld = world.world();
    std::vector<std::uint8_t> dataA(sizeof(AiPlayerData)), dataB(sizeof(AiPlayerData));
    std::vector<ShareCall>* log = nullptr;
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    stubs.Install<2, kSendChangeToolSettings>([&](CGameData*, std::uint32_t self, const std::uint32_t* args) {
        // fastcall: ECX = &player; EDX (the area) is the agent's +0xc, read from the agent.
        log->push_back({*reinterpret_cast<const std::uint8_t*>(self), *reinterpret_cast<const std::uint8_t*>(args[0]),
                        *reinterpret_cast<const std::uint8_t*>(args[1]), 0});
        return 0u;
    });
    int sent = 0;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        g_aiPlayerNumber = static_cast<std::uint8_t>(rng.Below(20));
        rng.Fill(dataA.data(), dataA.size());
        auto* data = reinterpret_cast<AiPlayerData*>(dataA.data());
        data->share_stage = static_cast<std::uint8_t>(rng.Chance(1, 4) ? kNoStage : rng.Below(5));
        dataB = dataA;
        EconomyAgent agentA{};
        rng.Fill(&agentA, sizeof(agentA));
        const std::uint16_t thresholds[] = {39, 40, 41, 99, 100, 101, 249, 250, 251};
        agentA.population = rng.Chance(1, 2) ? thresholds[rng.Below(9)] : static_cast<std::uint16_t>(rng.Below(400));
        agentA.area = static_cast<std::uint16_t>(rng.Below(4096));
        for (std::uint8_t& share : g_aiWorld.get()->areas[agentA.area].settings_extra) {
            share = static_cast<std::uint8_t>(rng.Chance(1, 3) ? rng.Below(256) : rng.Below(60));
        }
        EconomyAgent agentB = agentA;
        std::vector<ShareCall> logA, logB;
        log = &logA;
        g_aiPlayerData = reinterpret_cast<AiPlayerData*>(dataA.data());
        Original_T_ECONOMY_AGENT_AdjustSoldierShares(&agentA, nullptr);
        log = &logB;
        g_aiPlayerData = reinterpret_cast<AiPlayerData*>(dataB.data());
        candidate(&agentB, nullptr);
        stubs.Take();
        sent += logA.empty() ? 0 : 1;
        bool same = S3RE_EXPECT(logA == logB);
        same &= S3RE_EXPECT_MEM_EQ(dataA.data(), dataB.data(), dataA.size());
        same &= S3RE_EXPECT_MEM_EQ(&agentA, &agentB, sizeof(agentA));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (%u vs %u commands)", i, static_cast<unsigned>(logA.size()),
                     static_cast<unsigned>(logB.size()));
        }
    }
    if (cases >= 1000) {
        S3RE_EXPECT(sent > cases / 10);
    }
    g_aiWorld = savedWorld;
    g_aiPlayerData = savedData;
    g_aiPlayerNumber = savedNumber;
}

void __fastcall WrongShares(EconomyAgent* self, void* edx) {
    // Wrong: exactly 40 counts as stage 1.
    if (self->population == 40) {
        ++self->population;
        T_ECONOMY_AGENT_AdjustSoldierShares(self, edx);
        --self->population;
        return;
    }
    T_ECONOMY_AGENT_AdjustSoldierShares(self, edx);
}

}  // namespace

S3RE_TEST(AiSoldierShares_MatchOriginal) { CompareShares(ctx, &T_ECONOMY_AGENT_AdjustSoldierShares, 4000); }

S3RE_TEST(AiSoldierShares_ComparisonDetectsWrongImplementation) {
    test::Context quiet(ctx.name(), 1, /*quiet=*/true);
    CompareShares(quiet, &WrongShares, 4000);
    S3RE_EXPECT(quiet.failures() > 0);
}

}  // namespace s3re::game
