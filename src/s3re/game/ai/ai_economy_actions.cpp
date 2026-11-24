// What the T_ECONOMY_AGENT does besides asking for buildings (computer
// players; re/computer_players.md "The economy agent"). Both run from the
// agent's Run 0x567890 (thiscall on the agent, no arguments; EAX is not
// used).
//
//   0x0056a000 T_ECONOMY_AGENT::DemolishSurplusResidence(): with more than
//              10 free beds (Player::population_limit - population of
//              g_aiPlayerNumber in g_aiWorld, signed) a residence is torn
//              down (SendDestroyBuilding 0x58e5c0): above 100 free beds a
//              large one (the first found; else the last medium one, else
//              the last small one), above 30 a medium one (the first; else
//              the last small one), else a small one (the first). Only
//              finished residences of the player (owner byte) with no
//              births pending (BuildingResidenceBytes::births) count; the
//              building high-water mark is read once.
//   0x0056a0f0 T_ECONOMY_AGENT::PromoteSoldiers(): when the computer
//              player's step count (ComputerPlayer +0x764, signed) is above
//              14 times its setup's promotion interval (AiPlayerSetup
//              +0xc4), it has made fewer than 3 promotions (+0x768) and
//              the agent's area has barracks and a small temple: the step
//              count restarts, the promotion count goes up and
//              SendPromoteSoldiers 0x58e850 is sent for the kinds 0x81,
//              0x82, 0x83 (probably the three soldier kinds with bit 7
//              set; ExecuteCommands' PromoteSoldiers handler reads them).

#include "game/ai/ai_layout.h"
#include "game/enums/buildings.h"
#include "game/globals.h"
#include "game/layout.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using namespace ai;

namespace {

constexpr std::uintptr_t kSendDestroyBuilding = 0x0058e5c0;  // AI::SendDestroyBuilding (fastcall pointers)
constexpr std::uintptr_t kSendPromoteSoldiers = 0x0058e850;  // AI::SendPromoteSoldiers (fastcall pointers)

constexpr std::int32_t kSpareBeds = 10;        // more free beds than this: a residence goes
constexpr std::int32_t kManySpareBeds = 30;    // more: a medium one
constexpr std::int32_t kVeryManySpareBeds = 100;  // more: a large one
constexpr std::uint32_t kStepsPerInterval = 14;
constexpr std::uint8_t kMostPromotions = 3;
constexpr std::uint8_t kPromotionKinds[] = {0x81, 0x82, 0x83};

enum class ResidenceSize { Small = 1, Medium = 2, Large = 3 };

void DestroyBuilding(std::uint32_t building) {
    using Fn = std::uint32_t(__fastcall*)(const void* building, const void* player);
    reinterpret_cast<Fn>(kSendDestroyBuilding)(&building, &g_aiPlayerNumber.get());
}

}  // namespace

using AgentFn = void(__fastcall*)(EconomyAgent* self, void* edx);

void __fastcall T_ECONOMY_AGENT_DemolishSurplusResidence(EconomyAgent* self, void* edx);
void __fastcall T_ECONOMY_AGENT_PromoteSoldiers(EconomyAgent* self, void* edx);

S3RE_HOOK(0x0056a000, T_ECONOMY_AGENT_DemolishSurplusResidence, AgentFn,
          Original_T_ECONOMY_AGENT_DemolishSurplusResidence, T_ECONOMY_AGENT_DemolishSurplusResidence);
S3RE_HOOK(0x0056a0f0, T_ECONOMY_AGENT_PromoteSoldiers, AgentFn, Original_T_ECONOMY_AGENT_PromoteSoldiers,
          T_ECONOMY_AGENT_PromoteSoldiers);

// Purpose: Tears down one of a computer player's residences when there are
// many more beds than settlers (the larger the surplus, the larger the
// house).
void __fastcall T_ECONOMY_AGENT_DemolishSurplusResidence(EconomyAgent* /*self*/, void* /*edx*/) {
    const CGameData* world = g_aiWorld;
    const layout::Player& player = world->players[g_aiPlayerNumber];
    const auto spare = static_cast<std::int32_t>(player.population_limit - player.population);
    if (spare <= kSpareBeds) {
        return;
    }
    const ResidenceSize wanted = spare > kVeryManySpareBeds ? ResidenceSize::Large
                                 : spare > kManySpareBeds   ? ResidenceSize::Medium
                                                            : ResidenceSize::Small;
    std::uint32_t small = 0;
    std::uint32_t medium = 0;
    const std::uint32_t end = world->building_high_water + 1;
    bool found = false;  // the first residence of the wanted size ends the search
    for (std::uint32_t id = 1; id < end && !found; ++id) {
        const layout::Building& building = (&world->buildings[0])[id];
        if (building.owner != g_aiPlayerNumber || building.buildup != 0 || building.residence.births != 0) {
            continue;
        }
        switch (static_cast<BuildingType>(building.art)) {
        case BuildingType::SmallResidence:
            small = id;
            found = wanted == ResidenceSize::Small;
            break;
        case BuildingType::MediumResidence:
            if (wanted >= ResidenceSize::Medium) {
                medium = id;
                found = wanted == ResidenceSize::Medium;
            }
            break;
        case BuildingType::LargeResidence:
            if (wanted == ResidenceSize::Large) {
                DestroyBuilding(id);
                return;
            }
            break;
        default:
            break;
        }
    }
    const std::uint32_t chosen = medium != 0 ? medium : small;
    if (chosen != 0) {
        DestroyBuilding(chosen);
    }
}

// Purpose: Lets a computer player promote its soldiers now and then (up to
// three times a game), once it has barracks and a small temple.
void __fastcall T_ECONOMY_AGENT_PromoteSoldiers(EconomyAgent* self, void* /*edx*/) {
    ComputerPlayer* computer = g_aiPlayer;
    const std::uint32_t interval = g_aiPlayerSetup.get()->promotion_interval * kStepsPerInterval;
    if (static_cast<std::int32_t>(computer->steps) <= static_cast<std::int32_t>(interval) ||
        computer->promotions >= kMostPromotions || self->barracks == 0 || self->small_temples == 0) {
        return;
    }
    computer->steps = 0;
    g_aiPlayer.get()->promotions += 1;
    for (const std::uint8_t kind : kPromotionKinds) {
        using Fn = std::uint32_t(__fastcall*)(const void* player, const void* kind);
        reinterpret_cast<Fn>(kSendPromoteSoldiers)(&g_aiPlayerNumber.get(), &kind);
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// The command senders run (verified) with GameSettings::IsHost and
// MessageQueue::Push as stubs: IsHost says "host", Push logs the queue and
// the message bytes.

namespace {

constexpr std::uintptr_t kIsHost = 0x004318a0;
constexpr std::uintptr_t kPush = 0x004309c0;
constexpr std::uint32_t kQueue = 0x5152a000;  // a fake queue (never dereferenced)
constexpr std::uint32_t kTestBuildings = 60;

using Message = std::array<std::uint8_t, 12>;

struct ActionFunctions {
    AgentFn demolish, promote;
};

void SetUpActions(test::Rng& rng, CGameData* world, ComputerPlayer* computer, AiPlayerSetup* setup,
                  EconomyAgent* agent) {
    g_aiPlayerNumber = static_cast<std::uint8_t>(rng.Below(3));
    layout::Player& player = world->players[g_aiPlayerNumber];
    player.population = rng.Below(300);
    switch (rng.Below(5)) {
    case 0: player.population_limit = player.population + rng.Below(12); break;
    case 1: player.population_limit = player.population + 10 + rng.Below(25); break;
    case 2: player.population_limit = player.population + 28 + rng.Below(80); break;
    case 3: player.population_limit = player.population + 95 + rng.Below(100); break;
    default: player.population_limit = rng.Next32(); break;
    }
    world->building_high_water = rng.Chance(1, 20) ? 0xffffffffu : rng.Below(kTestBuildings);
    constexpr std::uint8_t kTypes[] = {26, 27, 28, 26, 27, 28, 15, 1};
    for (std::uint32_t id = 1; id < kTestBuildings; ++id) {
        layout::Building& building = world->buildings[id];
        building.owner = static_cast<std::uint8_t>(rng.Chance(1, 3) ? rng.Below(3) : g_aiPlayerNumber);
        building.art = kTypes[rng.Below(sizeof(kTypes))];
        building.buildup = static_cast<std::uint8_t>(rng.Chance(1, 6) ? 1 + rng.Below(200) : 0);
        building.residence.births = static_cast<std::uint8_t>(rng.Chance(1, 6) ? 1 + rng.Below(10) : 0);
    }
    computer->steps = rng.Chance(1, 2) ? rng.Below(2000) : rng.Next32();
    computer->promotions = static_cast<std::uint8_t>(rng.Below(5));
    setup->promotion_interval = static_cast<std::uint16_t>(rng.Chance(1, 2) ? rng.Below(150) : rng.Next32());
    if (rng.Chance(1, 4)) {  // right at the threshold
        computer->steps = setup->promotion_interval * kStepsPerInterval + rng.Below(2);
    }
    agent->barracks = static_cast<std::uint16_t>(rng.Chance(1, 4) ? 0 : 1 + rng.Below(3));
    agent->small_temples = static_cast<std::uint16_t>(rng.Chance(1, 4) ? 0 : 1 + rng.Below(3));
}

void CompareActions(test::Context& ctx, const ActionFunctions& candidate, int cases) {
    const ActionFunctions original{Original_T_ECONOMY_AGENT_DemolishSurplusResidence,
                                   Original_T_ECONOMY_AGENT_PromoteSoldiers};
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    CGameData* savedWorld = g_aiWorld;
    ComputerPlayer* savedPlayer = g_aiPlayer;
    AiPlayerSetup* savedSetup = g_aiPlayerSetup;
    const std::uint8_t savedNumber = g_aiPlayerNumber;
    std::vector<std::uint8_t> computerBytes(sizeof(ComputerPlayer)), setupBytes(sizeof(AiPlayerSetup)),
        agentBytes(sizeof(EconomyAgent));
    auto* computer = reinterpret_cast<ComputerPlayer*>(computerBytes.data());
    auto* setup = reinterpret_cast<AiPlayerSetup*>(setupBytes.data());
    auto* agent = reinterpret_cast<EconomyAgent*>(agentBytes.data());
    computer->message_queue = kQueue;
    g_aiWorld = world;
    g_aiPlayer = computer;
    g_aiPlayerSetup = setup;

    std::vector<Message>* log = nullptr;
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    stubs.Install<0, kIsHost>([](CGameData*, std::uint32_t, const std::uint32_t*) { return 1u; });
    stubs.Install<3, kPush>([&](CGameData*, std::uint32_t self, const std::uint32_t* args) {
        Message message{};
        std::memcpy(message.data(), reinterpret_cast<const void*>(args[0]), 4 * (args[1] < 3 ? args[1] : 3));
        message[11] ^= static_cast<std::uint8_t>(self == kQueue ? 0 : 0xff);
        log->push_back(message);
        return 0u;
    });
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        SetUpActions(rng, world, computer, setup, agent);
        const bool promote = rng.Chance(1, 2);
        const std::vector<std::uint8_t> start = computerBytes;
        std::vector<Message> logA, logB;
        log = &logA;
        (promote ? original.promote : original.demolish)(agent, nullptr);
        const std::vector<std::uint8_t> afterA = computerBytes;
        computerBytes = start;
        log = &logB;
        (promote ? candidate.promote : candidate.demolish)(agent, nullptr);
        bool same = S3RE_EXPECT(afterA == computerBytes);
        same &= S3RE_EXPECT(logA == logB);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (%s)", i, promote ? "promote" : "demolish");
        }
        stubs.Take();
    }
    g_aiWorld = savedWorld;
    g_aiPlayer = savedPlayer;
    g_aiPlayerSetup = savedSetup;
    g_aiPlayerNumber = savedNumber;
}

void __fastcall WrongDemolish(EconomyAgent* self, void* edx) {
    // Wrong: a medium surplus also takes the last medium residence (not the first).
    const CGameData* world = g_aiWorld;
    const layout::Player& player = world->players[g_aiPlayerNumber];
    const auto spare = static_cast<std::int32_t>(player.population_limit - player.population);
    if (spare > kManySpareBeds && spare <= kVeryManySpareBeds) {
        std::uint32_t medium = 0;
        for (std::uint32_t id = 1; id < world->building_high_water + 1; ++id) {
            const layout::Building& b = world->buildings[id];
            if (b.owner == g_aiPlayerNumber && b.buildup == 0 && b.residence.births == 0 &&
                b.art == Value(BuildingType::MediumResidence)) {
                medium = id;
            }
        }
        if (medium != 0) {
            DestroyBuilding(medium);
            return;
        }
    }
    T_ECONOMY_AGENT_DemolishSurplusResidence(self, edx);
}

void __fastcall WrongPromote(EconomyAgent* self, void* edx) {
    // Wrong: "at least" 14 intervals.
    ComputerPlayer* computer = g_aiPlayer;
    if (computer->steps == g_aiPlayerSetup.get()->promotion_interval * kStepsPerInterval) {
        computer->steps += 1;
    }
    T_ECONOMY_AGENT_PromoteSoldiers(self, edx);
}

}  // namespace

S3RE_TEST(AiEconomyActions_MatchOriginal) {
    CompareActions(ctx, {&T_ECONOMY_AGENT_DemolishSurplusResidence, &T_ECONOMY_AGENT_PromoteSoldiers}, 4000);
}

S3RE_TEST(AiEconomyActions_ComparisonDetectsWrongImplementation) {
    for (const ActionFunctions& wrong : {ActionFunctions{&WrongDemolish, &T_ECONOMY_AGENT_PromoteSoldiers},
                                         ActionFunctions{&T_ECONOMY_AGENT_DemolishSurplusResidence, &WrongPromote}}) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareActions(quiet, wrong, 4000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

}  // namespace s3re::game
