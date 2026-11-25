// CCentrumCheckAgent::Run (0x00560e00; kind 10, vtable slot 8; computer
// players, session 56): the agent that looks after the work areas of the
// computer player's woodcutters, stonecutters and waterworks and tears down
// used-up ones and failing mines. EAX is not used (RunAgents 0x551590).
//
// Five word counters at +0xa..+0x12 count up every run; every 42nd run
// (+8, reset to 0) all buildings (AI::GetBuilding 0x58e170 until its error
// flag) of g_aiPlayerNumber that are finished (+0x21 buildup 0) and have a
// worker (+8) are checked by type (jump table 0x5611b0 / 0x5611c8):
// - woodcutter: with more than 10 logs (goods 3) in its area
//   (AI::AreaGoodsAmount), trees near own fields (AI::FindTreesNearOwnFields
//   from its position) that no other woodcutter works
//   (AI::HasSameTypeWorkAreaNear, its AL tested) become its work area.
//   Otherwise, while its work cell has trees (AI::WorkCellHasTrees) nothing
//   happens; else a new work area (AI::FindWoodcutterWorkArea), or, when
//   there is none and the place is farm land (AI::IsFarmLand) without a
//   forester within 30 (AI::HasBuildingNear), it is torn down. Each change
//   first re-evaluates the land grid around the position
//   (CEvaLand::ReevaluateAroundTile 0x5548b0) and is sent as SetWorkArea
//   0x58e220 / DestroyBuilding 0x58e5c0.
// - stonecutter: the same with stones (goods 2), FindStonesNearOwnFields,
//   WorkCellHasStones, FindStonecutterWorkArea; without a new work area it
//   is torn down at once.
// - forester: counter +0x12 one more; above 60 back to 0 (nothing else).
// - waterworks: counter +0xc one more; above 60: 0, and a found work area
//   (AI::FindWaterworksWorkArea == 1) is sent.
// - coal, gold, iron, sulfur and gem mines: torn down when
//   AI::MineSuccessPercent is 1..6.
// Pointer arguments the original passes point at its stack copies of words;
// the callees read words.

#include "game/ai/ai_layout.h"
#include "game/enums/buildings.h"
#include "game/globals.h"
#include "game/layout.h"
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

constexpr std::uintptr_t kGetBuilding = 0x0058e170;              // fastcall (id, u32* error)
constexpr std::uintptr_t kBuildingRecordArea = 0x00599670;       // fastcall (building)
constexpr std::uintptr_t kAreaGoodsAmount = 0x0059dc10;          // fastcall (const u8* goods, const u16* area)
constexpr std::uintptr_t kFindTreesNearOwnFields = 0x0059a260;   // fastcall (u16* x, u16* y)
constexpr std::uintptr_t kFindStonesNearOwnFields = 0x0059a7a0;  // fastcall (u16* x, u16* y)
constexpr std::uintptr_t kHasSameTypeWorkAreaNear = 0x0058e390;  // fastcall (skip, const u8* type; player*, x*, y*)
constexpr std::uintptr_t kReevaluateAroundTile = 0x005548b0;     // CEvaLand (thiscall x, y)
constexpr std::uintptr_t kSendSetWorkArea = 0x0058e220;          // fastcall (building*, player*; x*, y*)
constexpr std::uintptr_t kWorkCellHasTrees = 0x0059a380;         // fastcall (building)
constexpr std::uintptr_t kWorkCellHasStones = 0x0059a8b0;
constexpr std::uintptr_t kFindWoodcutterWorkArea = 0x0059a3a0;   // fastcall (building, u16* x; u16* y)
constexpr std::uintptr_t kFindStonecutterWorkArea = 0x0059a8d0;
constexpr std::uintptr_t kFindWaterworksWorkArea = 0x0059ba90;
constexpr std::uintptr_t kIsFarmLand = 0x0059a720;               // fastcall (const u8* player, const u16* x; const u16* y)
constexpr std::uintptr_t kHasBuildingNear = 0x00571d90;          // fastcall (type, x; y, distance, player)
constexpr std::uintptr_t kMineSuccessPercent = 0x00573810;       // fastcall (const u16* building)
constexpr std::uintptr_t kSendDestroyBuilding = 0x0058e5c0;      // fastcall (building*, player*)

constexpr std::uint16_t kRunsPerCheck = 42;
constexpr std::uint16_t kSlowCheckSteps = 60;  // the forester's and waterworks' counters
constexpr std::uint32_t kPlentyInStock = 10;
constexpr std::uint8_t kLogs = 3;    // goods (AI::AreaGoodsAmount)
constexpr std::uint8_t kStones = 2;
constexpr std::uint32_t kForesterDistance = 30;
constexpr std::int32_t kWorstMinePercent = 6;  // a mine at 1..6 % is torn down

// CCentrumCheckAgent's counters (0x28 bytes; the constructor 0x560dd0 sets
// +8 to 140 and clears the rest).
#pragma pack(push, 1)
struct CentrumCheckCounters {
    std::uint32_t vtable;            // 0x00
    std::uint16_t kind;              // 0x04 10
    std::uint16_t unk06;
    std::uint16_t run_count;         // 0x08 checks every 42nd run
    std::uint16_t unk0a;             // 0x0a counts runs, not read here
    std::uint16_t waterworks_steps;  // 0x0c +1 per run and per waterworks; above 60: its work area is renewed
    std::uint16_t unk0e;             // 0x0e counts runs
    std::uint16_t unk10;             // 0x10 counts runs
    std::uint16_t forester_steps;    // 0x12 +1 per run and per forester, back to 0 above 60 (not read otherwise)
};
#pragma pack(pop)
static_assert(offsetof(CentrumCheckCounters, forester_steps) == 0x12);

// A building's position as the original keeps it (words, passed by address).
struct Position {
    std::uint16_t x, y;
};

const void* PlayerNumber() { return &g_aiPlayerNumber.get(); }

void Reevaluate(const Position& at) {
    using Fn = std::uint32_t(__fastcall*)(void*, void*, std::uint32_t, std::uint32_t);
    reinterpret_cast<Fn>(kReevaluateAroundTile)(reinterpret_cast<void*>(g_aiLandEvaluation.get()), nullptr, at.x, at.y);
}

void SendWorkArea(std::uint32_t id, Position& at) {
    Reevaluate(at);
    using Fn = std::uint32_t(__fastcall*)(const void*, const void*, const void*, const void*);
    reinterpret_cast<Fn>(kSendSetWorkArea)(&id, PlayerNumber(), &at.x, &at.y);
}

void Destroy(std::uint32_t id) {
    using Fn = std::uint32_t(__fastcall*)(const void*, const void*);
    reinterpret_cast<Fn>(kSendDestroyBuilding)(&id, PlayerNumber());
}

bool Query(std::uintptr_t function, const layout::Building* building) {
    return reinterpret_cast<std::uint32_t(__fastcall*)(const layout::Building*)>(function)(building) != 0;
}

std::uint32_t FindWorkArea(std::uintptr_t finder, const layout::Building* building, Position& at) {
    using Fn = std::uint32_t(__fastcall*)(const layout::Building*, std::uint16_t*, std::uint16_t*);
    return reinterpret_cast<Fn>(finder)(building, &at.x, &at.y);
}

// 0x560e90..0x560fe6 (woodcutter) and 0x560feb..0x561102 (stonecutter).
void CheckCutter(const layout::Building* building, std::uint32_t id, bool woodcutter, Position& at) {
    at = Position{building->x, building->y};
    const std::uint32_t area = reinterpret_cast<std::uint32_t(__fastcall*)(const layout::Building*)>(
        kBuildingRecordArea)(building);
    const std::uint8_t goods = woodcutter ? kLogs : kStones;
    using AmountFn = std::uint32_t(__fastcall*)(const std::uint8_t*, const void*);
    if (reinterpret_cast<AmountFn>(kAreaGoodsAmount)(&goods, &area) > kPlentyInStock) {
        using NearFieldsFn = std::uint32_t(__fastcall*)(std::uint16_t*, std::uint16_t*);
        const std::uintptr_t nearFields = woodcutter ? kFindTreesNearOwnFields : kFindStonesNearOwnFields;
        if (reinterpret_cast<NearFieldsFn>(nearFields)(&at.x, &at.y) != 0) {
            const std::uint8_t type = woodcutter ? Value(BuildingType::Woodcutter) : Value(BuildingType::Stonecutter);
            using SameFn = std::uint32_t(__fastcall*)(std::uint32_t, const std::uint8_t*, const void*, const void*,
                                                      const void*);
            const std::uint32_t taken =
                reinterpret_cast<SameFn>(kHasSameTypeWorkAreaNear)(id, &type, PlayerNumber(), &at.x, &at.y);
            if ((taken & 0xff) == 0) {  // (the original tests AL)
                SendWorkArea(id, at);
                return;
            }
        }
    }
    if (Query(woodcutter ? kWorkCellHasTrees : kWorkCellHasStones, building)) {
        return;
    }
    if (FindWorkArea(woodcutter ? kFindWoodcutterWorkArea : kFindStonecutterWorkArea, building, at) != 0) {
        SendWorkArea(id, at);
        return;
    }
    if (woodcutter) {
        using FarmFn = std::uint32_t(__fastcall*)(const void*, const std::uint16_t*, const std::uint16_t*);
        if (reinterpret_cast<FarmFn>(kIsFarmLand)(PlayerNumber(), &at.x, &at.y) == 0) {
            return;
        }
        using NearFn = std::uint32_t(__fastcall*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                                  std::uint32_t);
        if (reinterpret_cast<NearFn>(kHasBuildingNear)(Value(BuildingType::Forester), at.x, at.y, kForesterDistance,
                                                       g_aiPlayerNumber) != 0) {
            return;
        }
    }
    Reevaluate(at);
    Destroy(id);
}

}  // namespace

using CentrumRunFn = std::uint32_t(__fastcall*)(void* self, void* edx);
std::uint32_t __fastcall CCentrumCheckAgent_Run(void* self, void* edx);
S3RE_HOOK(0x00560e00, CCentrumCheckAgent_Run, CentrumRunFn, Original_CCentrumCheckAgent_Run, CCentrumCheckAgent_Run);

// Purpose: Every 42nd run the computer player looks after its resource
// buildings: woodcutters and stonecutters get new work areas or are torn
// down when their surroundings are used up, waterworks get new work areas
// now and then, and mines that hardly find anything are torn down.
std::uint32_t __fastcall CCentrumCheckAgent_Run(void* selfArg, void* /*edx*/) {
    auto* self = static_cast<CentrumCheckCounters*>(selfArg);
    ++self->unk0a;
    ++self->waterworks_steps;
    ++self->unk0e;
    ++self->unk10;
    ++self->forester_steps;
    ++self->run_count;
    if (self->run_count < kRunsPerCheck) {
        return 0;  // (EAX is not used)
    }
    self->run_count = 0;
    std::uint32_t error = 0;
    std::uint32_t id = 0;
    // One position for the whole loop, like the original's stack slots: the
    // waterworks' finder writes it when it finds a work area (returns 1).
    Position at{};
    do {
        ++id;
        using GetFn = const layout::Building*(__fastcall*)(std::uint32_t, std::uint32_t*);
        const layout::Building* building = reinterpret_cast<GetFn>(kGetBuilding)(id, &error);
        if (building == nullptr || building->owner != g_aiPlayerNumber || building->buildup != 0 ||
            building->worker == 0) {
            continue;
        }
        using B = BuildingType;
        switch (building->art) {
        case Value(B::Woodcutter): CheckCutter(building, id, true, at); break;
        case Value(B::Stonecutter): CheckCutter(building, id, false, at); break;
        case Value(B::Forester):
            if (++self->forester_steps > kSlowCheckSteps) {
                self->forester_steps = 0;
            }
            break;
        case Value(B::Waterworks): {
            if (++self->waterworks_steps <= kSlowCheckSteps) {
                break;
            }
            self->waterworks_steps = 0;
            if (FindWorkArea(kFindWaterworksWorkArea, building, at) == 1) {
                SendWorkArea(id, at);
            }
            break;
        }
        case Value(B::CoalMine):
        case Value(B::GoldMine):
        case Value(B::IronMine):
        case Value(B::SulfurMine):
        case Value(B::GemMine): {
            const std::uint32_t building16 = id;
            const auto percent = static_cast<std::int32_t>(
                reinterpret_cast<std::uint32_t(__fastcall*)(const void*)>(kMineSuccessPercent)(&building16));
            if (percent > 0 && percent <= kWorstMinePercent) {
                Destroy(id);
            }
            break;
        }
        default: break;
        }
    } while (error == 0);
    return 0;
}

}  // namespace s3re::game

// ---------------------------------------------------------------------------
// Tests: a zeroed world with 1..30 buildings of resource types (GetBuilding
// runs on it); every other callee is stubbed, records what its ECX and
// stack pointers point at (the original's are stack copies; EDX can't be
// seen by the stubs) and answers from a sequence replayed for the second
// run. Compared: the calls and the agent.
// ---------------------------------------------------------------------------

namespace s3re::game {
namespace {

struct CentrumCall {
    std::uintptr_t callee;
    std::uint32_t values[4];
    bool operator==(const CentrumCall& other) const {
        return callee == other.callee && std::memcmp(values, other.values, sizeof(values)) == 0;
    }
};

void CompareCentrum(test::Context& ctx, CentrumRunFn candidate, int cases) {
    CGameData* const savedWorld = g_aiWorld;
    const std::uint8_t savedNumber = g_aiPlayerNumber;
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    g_aiWorld = world;
    std::vector<CentrumCall>* log = nullptr;
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
    // kind: what ECX is (0 its value, 1 / 2 a byte / word it points at, 3 / 4
    // its low word / byte);
    // stack: the same per stack argument; then the answer.
    const auto recorder = [&](std::uintptr_t callee, int ecxKind, std::initializer_list<int> stackKinds,
                              std::uint32_t modulo) {
        std::vector<int> kinds(stackKinds);
        return [&, callee, ecxKind, kinds, modulo](CGameData*, std::uint32_t self, const std::uint32_t* args) {
            CentrumCall call{callee, {0, 0, 0, 0}};
            const auto value = [&](std::uint32_t raw, int kind) {
                switch (kind) {
                case 0: return raw;
                case 1: return static_cast<std::uint32_t>(byte(raw));
                case 2: return static_cast<std::uint32_t>(word(raw));
                case 3: return raw & 0xffff;  // a value pushed with a leftover upper half
                default: return raw & 0xff;
                }
            };
            call.values[0] = value(self, ecxKind);
            for (std::size_t k = 0; k < kinds.size(); ++k) {
                call.values[k + 1] = value(args[k], kinds[k]);
            }
            log->push_back(call);
            return modulo == 0 ? 0u : answer() % modulo;
        };
    };
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    stubs.Install<0, kBuildingRecordArea>(recorder(kBuildingRecordArea, 0, {}, 4));
    stubs.Install<0, kAreaGoodsAmount>(recorder(kAreaGoodsAmount, 1, {}, 14));
    stubs.Install<0, kFindTreesNearOwnFields>(recorder(kFindTreesNearOwnFields, 2, {}, 2));
    stubs.Install<0, kFindStonesNearOwnFields>(recorder(kFindStonesNearOwnFields, 2, {}, 2));
    stubs.Install<3, kHasSameTypeWorkAreaNear>(recorder(kHasSameTypeWorkAreaNear, 0, {1, 2, 2}, 0x202));
    stubs.Install<2, kReevaluateAroundTile>(recorder(kReevaluateAroundTile, 0, {3, 3}, 0));
    stubs.Install<2, kSendSetWorkArea>(recorder(kSendSetWorkArea, 2, {2, 2}, 0));
    stubs.Install<0, kWorkCellHasTrees>(recorder(kWorkCellHasTrees, 0, {}, 2));
    stubs.Install<0, kWorkCellHasStones>(recorder(kWorkCellHasStones, 0, {}, 2));
    stubs.Install<1, kFindWoodcutterWorkArea>(recorder(kFindWoodcutterWorkArea, 0, {2}, 2));
    stubs.Install<1, kFindStonecutterWorkArea>(recorder(kFindStonecutterWorkArea, 0, {2}, 2));
    // The waterworks' finder answers 0 or 2 here: on success the real one
    // writes x through EDX, which a stub can't see (the emulator spec
    // covers the found path with both words written).
    stubs.Install<1, kFindWaterworksWorkArea>([&](CGameData* w, std::uint32_t self, const std::uint32_t* args) {
        return recorder(kFindWaterworksWorkArea, 0, {}, 2)(w, self, args) * 2;
    });
    stubs.Install<1, kIsFarmLand>(recorder(kIsFarmLand, 1, {2}, 2));
    stubs.Install<3, kHasBuildingNear>(recorder(kHasBuildingNear, 0, {3, 0, 4}, 2));
    stubs.Install<0, kMineSuccessPercent>(recorder(kMineSuccessPercent, 2, {}, 9));
    stubs.Install<0, kSendDestroyBuilding>(recorder(kSendDestroyBuilding, 2, {}, 0));
    const std::uint8_t types[] = {2, 3, 5, 30, 7, 8, 9, 29, 37, 4, 15};
    int checks = 0;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        stubRng = &rng;
        answers.clear();
        const auto player = static_cast<std::uint8_t>(rng.Below(3));
        g_aiPlayerNumber = player;
        const std::uint32_t count = 1 + rng.Below(30);
        world->building_high_water = count;
        for (std::uint32_t b = 0; b <= count + 1; ++b) {
            layout::Building& record = world->buildings[b];
            rng.Fill(&record, sizeof(record));
            record.owner = static_cast<std::uint8_t>(rng.Chance(4, 5) ? player : rng.Below(3));
            record.art = types[rng.Below(sizeof(types))];
            record.x = static_cast<std::uint16_t>(rng.Below(60));
            record.y = static_cast<std::uint16_t>(rng.Below(60));
            record.worker = static_cast<std::uint16_t>(rng.Chance(1, 6) ? 0 : 1 + rng.Below(100));
            record.buildup = static_cast<std::uint8_t>(rng.Chance(5, 6) ? 0 : 1 + rng.Below(200));
        }
        CentrumCheckCounters agentA{};
        rng.Fill(&agentA, sizeof(agentA));
        agentA.run_count = static_cast<std::uint16_t>(rng.Chance(3, 4) ? 41 : rng.Below(0x10000));
        agentA.waterworks_steps = static_cast<std::uint16_t>(rng.Chance(3, 4) ? 58 + rng.Below(4) : rng.Below(0x10000));
        agentA.forester_steps = static_cast<std::uint16_t>(rng.Chance(3, 4) ? 58 + rng.Below(4) : rng.Below(0x10000));
        CentrumCheckCounters agentB = agentA;
        std::vector<CentrumCall> logA, logB;
        log = &logA;
        answerIndex = 0;
        Original_CCentrumCheckAgent_Run(&agentA, nullptr);
        log = &logB;
        answerIndex = 0;
        candidate(&agentB, nullptr);
        stubs.Take();
        checks += logA.empty() ? 0 : 1;
        bool same = S3RE_EXPECT(logA == logB);
        same &= S3RE_EXPECT_MEM_EQ(&agentA, &agentB, sizeof(agentA));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (%u vs %u calls)", i, static_cast<unsigned>(logA.size()),
                     static_cast<unsigned>(logB.size()));
            for (std::size_t k = 0; k < logA.size() && k < logB.size(); ++k) {
                if (!(logA[k] == logB[k])) {
                    ctx.Fail(__FILE__, __LINE__, "  call %u: %08x %08x %08x %08x %08x vs %08x %08x %08x %08x",
                             static_cast<unsigned>(k), static_cast<unsigned>(logA[k].callee), logA[k].values[0],
                             logA[k].values[1], logA[k].values[2], logA[k].values[3], logB[k].values[0],
                             logB[k].values[1], logB[k].values[2], logB[k].values[3]);
                    break;
                }
            }
        }
    }
    if (cases >= 1000) {
        S3RE_EXPECT(checks > cases / 2);
    }
    g_aiWorld = savedWorld;
    g_aiPlayerNumber = savedNumber;
}

std::uint32_t __fastcall WrongCentrum(void* self, void* edx) {
    // Wrong: a mine at 6 % stays.
    auto* counters = static_cast<CentrumCheckCounters*>(self);
    if (counters->run_count + 1 < kRunsPerCheck) {
        return CCentrumCheckAgent_Run(self, edx);
    }
    std::vector<std::uint8_t> types;
    for (std::uint32_t id = 1; id <= g_aiWorld.get()->building_high_water; ++id) {
        layout::Building& building = g_aiWorld.get()->buildings[id];
        types.push_back(building.art);
        if (building.art == Value(BuildingType::GemMine)) {
            building.art = Value(BuildingType::Sawmill);  // (one kind of mine is skipped)
        }
    }
    const std::uint32_t result = CCentrumCheckAgent_Run(self, edx);
    for (std::uint32_t id = 1; id <= g_aiWorld.get()->building_high_water; ++id) {
        g_aiWorld.get()->buildings[id].art = types[id - 1];
    }
    return result;
}

}  // namespace

S3RE_TEST(AiCentrumCheck_MatchOriginal) { CompareCentrum(ctx, &CCentrumCheckAgent_Run, 3000); }

S3RE_TEST(AiCentrumCheck_ComparisonDetectsWrongImplementation) {
    test::Context quiet(ctx.name(), 1, /*quiet=*/true);
    CompareCentrum(quiet, &WrongCentrum, 3000);
    S3RE_EXPECT(quiet.failures() > 0);
}

}  // namespace s3re::game
