// AI::OnBuildingFinished (0x00573890; Rainer\compGlobs.cpp; computer
// players, session 56): fastcall (building id), called by the land
// evaluation's event processing 0x554980 when a building is finished. EAX is
// not used (0x554980 overwrites it).
//
// A building that no longer exists (AI::BuildingIfUsed 0x58e1c0) only logs
// "RAINER: Haus das fertig sein soll gibt es nicht mehr!". Otherwise, only
// for computer players (Player::active == 2):
// 1. Every node of the owner's area list (CEvaLand::lists) with the
//    building's area (AI::BuildingArea 0x599610): type_blocked[type] = 0
//    and one construction site less (AiPlayerData::sites, not below 0).
// 2. By type (jump table 0x573b8c / 0x573bb8):
//    - farm, waterworks, fishery, rice farm, vineyard: the work area its
//      finder (0x59ac50, 0x59ba90, 0x59b780, 0x59b0a0, 0x59b430) picks,
//      starting from the building's position, is sent (AI::SendSetWorkArea
//      0x58e220);
//    - toolsmith: AI::ClearToolGoodsSettings 0x58e930;
//    - medium residence: AI::EnsureGoodsSupply(13); large residence: (13),
//      then (12);
//    - large tower, castle: AI::SendChangeGarrison(player, building, 1);
//    - gem mine: every gold mine and gold smelter of the owner is torn down
//      (AI::SendDestroyBuilding 0x58e5c0; all buildings through
//      AI::GetBuilding 0x58e170 until its error flag).

#include "game/ai/ai_layout.h"
#include "game/enums/buildings.h"
#include "game/debug_log.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/texts.h"
#include "game/world.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using namespace ai;

namespace {

constexpr std::uintptr_t kBuildingIfUsed = 0x0058e1c0;         // fastcall (id)
constexpr std::uintptr_t kBuildingArea = 0x00599610;           // fastcall (const u16* building)
constexpr std::uintptr_t kFindFarmWorkArea = 0x0059ac50;       // fastcall (building, u16* x; u16* y)
constexpr std::uintptr_t kFindWaterworksWorkArea = 0x0059ba90;
constexpr std::uintptr_t kFindFisheryWorkArea = 0x0059b780;
constexpr std::uintptr_t kFindRiceFarmWorkArea = 0x0059b0a0;
constexpr std::uintptr_t kFindVineyardWorkArea = 0x0059b430;
constexpr std::uintptr_t kSendSetWorkArea = 0x0058e220;        // fastcall (building*, player*; x*, y*)
constexpr std::uintptr_t kClearToolGoodsSettings = 0x0058e930;  // fastcall (const u16* building)
constexpr std::uintptr_t kEnsureGoodsSupply = 0x00573760;      // fastcall (goods*, player*; area*)
constexpr std::uintptr_t kSendChangeGarrison = 0x0058e630;     // fastcall (player, building; op)
constexpr std::uintptr_t kGetBuilding = 0x0058e170;            // fastcall (id, u32* error)
constexpr std::uintptr_t kSendDestroyBuilding = 0x0058e5c0;    // fastcall (building*, player*)

constexpr std::int32_t kComputerControlled = 2;  // Player::active of a computer player
constexpr std::uint32_t kGarrisonOp = 1;
constexpr std::uint8_t kGoods13 = 13;  // the goods the residences ask for (EnsureGoodsSupply)
constexpr std::uint8_t kGoods12 = 12;

// 0x57390d..0x57394c: the owner's nodes of the building's area.
void UnblockType(std::uint8_t owner, std::uint16_t area, std::uint8_t type) {
    // (the list head is read from g_aiLandEvaluation again for each step)
    const auto head = [owner] {
        return reinterpret_cast<std::uint32_t>(
            reinterpret_cast<const AiLandEvaluation*>(g_aiLandEvaluation.get())->lists[owner].head);
    };
    for (std::uint32_t at = reinterpret_cast<const AiListNode*>(head())->next; at != head();
         at = reinterpret_cast<const AiPlayerData*>(at)->next) {
        auto* node = reinterpret_cast<AiPlayerData*>(at);
        if (node->area != area) {
            continue;
        }
        node->type_blocked[type] = 0;
        if (node->sites > 0) {
            --node->sites;
        }
    }
}

// 0x573970.. (and the four like it): the work area the finder picks, sent.
void SendWorkArea(std::uintptr_t finder, const layout::Building* building, std::uint32_t id, std::uint8_t owner,
                  std::uint16_t x, std::uint16_t y) {
    using FindFn = std::uint32_t(__fastcall*)(const layout::Building*, std::uint16_t*, std::uint16_t*);
    reinterpret_cast<FindFn>(finder)(building, &x, &y);
    const std::uint32_t building16 = id & 0xffff;
    using SendFn = std::uint32_t(__fastcall*)(const void*, const void*, const void*, const void*);
    reinterpret_cast<SendFn>(kSendSetWorkArea)(&building16, &owner, &x, &y);
}

void EnsureSupply(std::uint8_t goods, std::uint8_t owner, const std::uint32_t* area) {
    using Fn = std::uint32_t(__fastcall*)(const std::uint8_t*, const std::uint8_t*, const void*);
    reinterpret_cast<Fn>(kEnsureGoodsSupply)(&goods, &owner, area);
}

// 0x573b38..0x573b83: the gem mine replaces the owner's gold industry.
void DestroyGoldIndustry(std::uint8_t owner) {
    std::uint32_t error = 0;
    std::uint32_t id = 0;
    do {
        ++id;
        using GetFn = const layout::Building*(__fastcall*)(std::uint32_t, std::uint32_t*);
        const layout::Building* other = reinterpret_cast<GetFn>(kGetBuilding)(id, &error);
        if (other != nullptr && other->owner == owner &&
            (other->art == Value(BuildingType::GoldMine) || other->art == Value(BuildingType::GoldSmelter))) {
            const std::uint32_t building = id;
            using SendFn = std::uint32_t(__fastcall*)(const void*, const void*);
            reinterpret_cast<SendFn>(kSendDestroyBuilding)(&building, &owner);
        }
    } while (error == 0);
}

}  // namespace

using FinishedFn = void(__fastcall*)(std::uint32_t id);
void __fastcall AI_OnBuildingFinished(std::uint32_t id);
S3RE_HOOK(0x00573890, AI_OnBuildingFinished, FinishedFn, Original_AI_OnBuildingFinished, AI_OnBuildingFinished);

// Purpose: A computer player's building was finished: its type may be
// planned again in that area, and some types need a follow-up order (a work
// area, tool settings, goods, a garrison, or tearing down the gold industry
// for a gem mine).
void __fastcall AI_OnBuildingFinished(std::uint32_t id) {
    using LookupFn = const layout::Building*(__fastcall*)(std::uint32_t);
    const layout::Building* building = reinterpret_cast<LookupFn>(kBuildingIfUsed)(id);
    if (building == nullptr) {
        GameDebugLog(texts::kAiFinishedHouseGone);
        return;
    }
    const std::uint8_t owner = building->owner;
    if (g_aiWorld.get()->players[owner].active != kComputerControlled) {
        return;
    }
    const std::uint16_t x = building->x;
    const std::uint16_t y = building->y;
    const std::uint8_t type = building->art;
    const std::uint32_t area = [&] {
        const std::uint32_t building32 = id;
        using AreaFn = std::uint32_t(__fastcall*)(const void*);
        return reinterpret_cast<AreaFn>(kBuildingArea)(&building32);
    }();
    UnblockType(owner, static_cast<std::uint16_t>(area), type);
    using B = BuildingType;
    switch (type) {
    case Value(B::Farm): SendWorkArea(kFindFarmWorkArea, building, id, owner, x, y); break;
    case Value(B::Waterworks): SendWorkArea(kFindWaterworksWorkArea, building, id, owner, x, y); break;
    case Value(B::Fishery): SendWorkArea(kFindFisheryWorkArea, building, id, owner, x, y); break;
    case Value(B::RiceFarm): SendWorkArea(kFindRiceFarmWorkArea, building, id, owner, x, y); break;
    case Value(B::Vineyard): SendWorkArea(kFindVineyardWorkArea, building, id, owner, x, y); break;
    case Value(B::Toolsmith): {
        const std::uint32_t building32 = id;
        reinterpret_cast<std::uint32_t(__fastcall*)(const void*)>(kClearToolGoodsSettings)(&building32);
        break;
    }
    case Value(B::MediumResidence): EnsureSupply(kGoods13, owner, &area); break;
    case Value(B::LargeResidence):
        EnsureSupply(kGoods13, owner, &area);
        EnsureSupply(kGoods12, owner, &area);
        break;
    case Value(B::LargeTower):
    case Value(B::Castle): {
        using GarrisonFn = std::uint32_t(__fastcall*)(std::uint32_t, std::uint32_t, std::uint32_t);
        reinterpret_cast<GarrisonFn>(kSendChangeGarrison)(owner, id & 0xffff, kGarrisonOp);
        break;
    }
    case Value(B::GemMine): DestroyGoldIndustry(owner); break;
    default: break;
    }
}

}  // namespace s3re::game

// ---------------------------------------------------------------------------
// Tests: a zeroed world with 1..40 buildings near the origin (the lookups
// AI::BuildingIfUsed, AI::GetBuilding and AI::BuildingArea run on it), the
// owners' area list in a CEvaLand; the senders, finders and
// EnsureGoodsSupply stubbed: they record the values their ECX / stack
// pointers point at (the original's are stack copies), a finder sometimes
// writes a new y. Compared: the calls and the area nodes.
// ---------------------------------------------------------------------------

namespace s3re::game {
namespace {

constexpr std::size_t kTestNodes = 4;

struct FinishedCall {
    std::uintptr_t callee;
    std::uint32_t values[3];
    bool operator==(const FinishedCall& other) const {
        return callee == other.callee && std::memcmp(values, other.values, sizeof(values)) == 0;
    }
};

void CompareFinished(test::Context& ctx, FinishedFn candidate, int cases) {
    CGameData* const savedWorld = g_aiWorld;
    const std::uint32_t savedLand = g_aiLandEvaluation;
    const std::uint16_t savedX = g_aiMapSizeX, savedY = g_aiMapSizeY;
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    g_aiWorld = world;
    g_aiMapSizeX = 64;
    g_aiMapSizeY = 64;
    AiLandEvaluation land{};
    g_aiLandEvaluation = reinterpret_cast<std::uint32_t>(&land);
    std::vector<AiAreaNode> nodes(kTestNodes), before(kTestNodes), after(kTestNodes);
    AiListNode head{};
    std::vector<FinishedCall>* log = nullptr;
    test::Rng* stubRng = nullptr;
    std::vector<std::uint32_t> answers;
    std::size_t answerIndex = 0;
    const auto answer = [&]() {
        if (answerIndex == answers.size()) {
            answers.push_back(stubRng->Next32());
        }
        return answers[answerIndex++];
    };
    const auto word = [](std::uint32_t pointer) { return *reinterpret_cast<const std::uint16_t*>(pointer); };
    const auto byte = [](std::uint32_t pointer) { return *reinterpret_cast<const std::uint8_t*>(pointer); };
    const auto finderEffect = [&](std::uintptr_t finder) {
        return [&, finder](CGameData*, std::uint32_t self, const std::uint32_t* args) {
            log->push_back({finder, {self, word(args[0]), 0}});
            const std::uint32_t pick = answer();
            if ((pick & 1) != 0) {
                *reinterpret_cast<std::uint16_t*>(args[0]) = static_cast<std::uint16_t>(pick >> 8 & 63);
            }
            return 1u;
        };
    };
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    stubs.Install<1, kFindFarmWorkArea>(finderEffect(kFindFarmWorkArea));
    stubs.Install<1, kFindWaterworksWorkArea>(finderEffect(kFindWaterworksWorkArea));
    stubs.Install<1, kFindFisheryWorkArea>(finderEffect(kFindFisheryWorkArea));
    stubs.Install<1, kFindRiceFarmWorkArea>(finderEffect(kFindRiceFarmWorkArea));
    stubs.Install<1, kFindVineyardWorkArea>(finderEffect(kFindVineyardWorkArea));
    stubs.Install<2, kSendSetWorkArea>([&](CGameData*, std::uint32_t self, const std::uint32_t* args) {
        log->push_back({kSendSetWorkArea, {word(self), word(args[0]), word(args[1])}});
        return 0u;
    });
    stubs.Install<0, kClearToolGoodsSettings>([&](CGameData*, std::uint32_t self, const std::uint32_t*) {
        log->push_back({kClearToolGoodsSettings, {word(self), 0, 0}});
        return 0u;
    });
    stubs.Install<1, kEnsureGoodsSupply>([&](CGameData*, std::uint32_t self, const std::uint32_t* args) {
        log->push_back({kEnsureGoodsSupply, {byte(self), word(args[0]), 0}});
        return 0u;
    });
    stubs.Install<1, kSendChangeGarrison>([&](CGameData*, std::uint32_t self, const std::uint32_t* args) {
        log->push_back({kSendChangeGarrison, {self, args[0], 0}});
        return 0u;
    });
    stubs.Install<0, kSendDestroyBuilding>([&](CGameData*, std::uint32_t self, const std::uint32_t*) {
        log->push_back({kSendDestroyBuilding, {word(self), 0, 0}});
        return 0u;
    });
    const std::uint8_t types[] = {12, 14, 16, 18, 24, 25, 27, 28, 30, 36, 37, 2, 8, 10, 0};
    int followUps = 0;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        stubRng = &rng;
        answers.clear();
        const std::uint32_t count = 1 + rng.Below(40);
        world->building_high_water = count;
        for (std::uint32_t b = 0; b <= count + 1; ++b) {
            layout::Building& record = world->buildings[b];
            rng.Fill(&record, sizeof(record));
            record.owner = static_cast<std::uint8_t>(rng.Chance(1, 8) ? 0xff : rng.Below(3));
            record.art = types[rng.Below(sizeof(types))];
            record.door_x = static_cast<std::uint16_t>(rng.Below(64));
            record.door_y = static_cast<std::uint16_t>(rng.Below(64));
            world->tiles[record.door_x][record.door_y].area = static_cast<std::uint16_t>(rng.Below(4));
        }
        for (std::uint32_t p = 0; p < 3; ++p) {
            world->players[p].active = rng.Chance(5, 6) ? 2 : static_cast<std::int32_t>(rng.Below(3));
        }
        const std::uint32_t id = (rng.Chance(1, 20) ? count + 1 + rng.Below(4) : 1 + rng.Below(count)) |
                                 (rng.Chance(1, 5) ? rng.Next32() & 0xffff0000u : 0);
        // every player's area list: head -> nodes -> head
        const std::uint32_t used = rng.Below(kTestNodes + 1);
        rng.Fill(nodes.data(), nodes.size() * sizeof(AiAreaNode));
        std::uint32_t* link = &head.next;
        for (std::uint32_t k = 0; k < used; ++k) {
            nodes[k].value.area = static_cast<std::uint16_t>(rng.Below(4));
            nodes[k].value.sites = static_cast<std::uint16_t>(rng.Chance(1, 3) ? 0 : rng.Below(4));
            *link = reinterpret_cast<std::uint32_t>(&nodes[k]);
            link = reinterpret_cast<std::uint32_t*>(&nodes[k].next);
        }
        *link = reinterpret_cast<std::uint32_t>(&head);
        for (AiList& list : land.lists) {
            list.head = &head;
        }
        before = nodes;
        std::vector<FinishedCall> logA, logB;
        log = &logA;
        answerIndex = 0;
        Original_AI_OnBuildingFinished(id);
        after = nodes;
        nodes = before;  // (same buffer: the links stay valid)
        log = &logB;
        answerIndex = 0;
        candidate(id);
        stubs.Take();
        followUps += logA.empty() ? 0 : 1;
        bool same = S3RE_EXPECT(logA == logB);
        same &= S3RE_EXPECT_MEM_EQ(after.data(), nodes.data(), nodes.size() * sizeof(AiAreaNode));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (%u vs %u calls)", i, static_cast<unsigned>(logA.size()),
                     static_cast<unsigned>(logB.size()));
        }
    }
    if (cases >= 1000) {
        S3RE_EXPECT(followUps > cases / 5);
    }
    g_aiWorld = savedWorld;
    g_aiLandEvaluation = savedLand;
    g_aiMapSizeX = savedX;
    g_aiMapSizeY = savedY;
}

void __fastcall WrongFinished(std::uint32_t id) {
    // Wrong: the castle gets no garrison order.
    const auto* building =
        reinterpret_cast<const layout::Building*(__fastcall*)(std::uint32_t)>(kBuildingIfUsed)(id);
    if (building != nullptr && building->art == Value(BuildingType::Castle) &&
        g_aiWorld.get()->players[building->owner].active == kComputerControlled) {
        return;
    }
    AI_OnBuildingFinished(id);
}

}  // namespace

S3RE_TEST(AiBuildingFinished_MatchOriginal) { CompareFinished(ctx, &AI_OnBuildingFinished, 4000); }

S3RE_TEST(AiBuildingFinished_ComparisonDetectsWrongImplementation) {
    test::Context quiet(ctx.name(), 1, /*quiet=*/true);
    CompareFinished(quiet, &WrongFinished, 4000);
    S3RE_EXPECT(quiet.failures() > 0);
}

}  // namespace s3re::game
