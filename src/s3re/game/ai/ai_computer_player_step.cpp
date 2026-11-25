// A computer player's step (the unnamed source file before
// Rainer\Cevaland.cpp; re/computer_players.md "The per-step entry").
//
//   0x00550f80 CComputerPlayer::Step() thiscall, called once per computer
//              player and logic step by CLogicThread::Step 0x53db30. With p
//              = player (+0x76c, the whole dword in every comparison and
//              index), the three step timers are staggered by p:
//              ++steps; ++timer274 and, once it passes p + 14 (unsigned),
//              timer274 = p and every node of CEvaLand::lists[p] lowers its
//              57 candidate_values by one (not below 0; the loop re-reads
//              the list head each round). Then the land evaluation's
//              UpdateNextCells 0x5547a0 and ProcessPendingEvents 0x554980
//              (through land_evaluation +0x774, re-read for each call);
//              non-zero: done. A player without nodes (list count 0):
//              SetCurrentPlayer 0x551a30 and agents[7] (T_ATTACK_AGENT)
//              slot 8 (run), done. Otherwise the current node moves on:
//              with area != 0xffffffff and areas_changed[p] == 0 to
//              data->next (the head: its first node), else to the first
//              node and areas_changed[p] = 0; area = the node's area word.
//              SetCurrentPlayer; ++timer278 past p + 71: timer278 = p + 1,
//              DiscountExploitedResources 0x59bc20 (ECX: p's low word, the
//              upper half of p + 71's), SetForestersWorking 0x59c0f0
//              (&area, &p as words in stack dwords whose upper halves are
//              leftovers), done. Else UpdateBlackboard 0x5517d0,
//              StartBestBuilding 0x551a70; ++timer27c past p + 844:
//              timer27c = p + 4, OrderMissingTools 0x59bf70 (&p, &area),
//              COMP_FoodForMines 0x5724e0 (&p, &area), CheckBasicEconomy
//              0x5720c0 (&p, data). Last RaiseBuildPriorities 0x551b10 and
//              ++data->steps_since_expansion (data re-read). EAX: the last
//              callee's or ProcessPendingEvents' result; CLogicThread::Step
//              doesn't use it (void here).

#include "game/ai/ai_layout.h"
#include "game/globals.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"
#include "game/ai/ai_area_test_world.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using namespace ai;

namespace {

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kUpdateNextCells = 0x005547a0;        // CEvaLand::UpdateNextCells (thiscall)
constexpr std::uintptr_t kProcessPendingEvents = 0x00554980;   // CEvaLand::ProcessPendingEvents (thiscall)
constexpr std::uintptr_t kSetCurrentPlayer = 0x00551a30;       // AI::SetCurrentPlayer (fastcall)
constexpr std::uintptr_t kDiscountExploited = 0x0059bc20;      // AI::DiscountExploitedResources (fastcall)
constexpr std::uintptr_t kSetForestersWorking = 0x0059c0f0;    // AI::SetForestersWorking (fastcall)
constexpr std::uintptr_t kUpdateBlackboard = 0x005517d0;       // CComputerPlayer::UpdateBlackboard (thiscall)
constexpr std::uintptr_t kStartBestBuilding = 0x00551a70;      // CComputerPlayer::StartBestBuilding (thiscall)
constexpr std::uintptr_t kOrderMissingTools = 0x0059bf70;      // AI::OrderMissingTools (fastcall)
constexpr std::uintptr_t kFoodForMines = 0x005724e0;           // COMP_FoodForMines (fastcall)
constexpr std::uintptr_t kCheckBasicEconomy = 0x005720c0;      // AI::CheckBasicEconomy (fastcall)
constexpr std::uintptr_t kRaiseBuildPriorities = 0x00551b10;   // CComputerPlayer::RaiseBuildPriorities (thiscall)

// The three timers: each runs its work when it passes player + its period
// and restarts at player + its start (so the computer players take turns).
constexpr std::uint32_t kCountdownPeriod = 14;   // timer274: candidate countdowns (every 15 steps)
constexpr std::uint32_t kResourcePeriod = 71;    // timer278: exploited resources, foresters
constexpr std::uint32_t kResourceStart = 1;
constexpr std::uint32_t kEconomyPeriod = 844;    // timer27c: tools, mine food, basic economy
constexpr std::uint32_t kEconomyStart = 4;
constexpr std::uint32_t kNoArea = 0xffffffff;    // area before the first step (Init)
constexpr std::size_t kRunSlot = 8;              // BlackBoardAgent vtable slot 8 (+0x20): run

using ThisFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using WordsFn = std::uint32_t(__fastcall*)(const std::uint32_t* a, const std::uint32_t* b);

AiLandEvaluation* LandOf(const ComputerPlayer* self) {
    return reinterpret_cast<AiLandEvaluation*>(self->land_evaluation);
}

AiPlayerData* FirstNode(const AiList& list) {
    return reinterpret_cast<AiPlayerData*>(list.head->next);
}

// 0x550fb4: every node's countdowns one lower.
void CountDownCandidates(const ComputerPlayer* self) {
    auto* node = FirstNode(LandOf(self)->lists[self->player]);
    while (reinterpret_cast<AiListNode*>(node) != LandOf(self)->lists[self->player].head) {
        for (std::uint16_t& value : node->candidate_values) {
            if (value != 0) {
                --value;
            }
        }
        node = reinterpret_cast<AiPlayerData*>(node->next);
    }
}

// 0x551052: the next node, round robin (the first one after a change of
// the player's areas).
void MoveToNextNode(ComputerPlayer* self) {
    AiLandEvaluation* land = LandOf(self);
    const std::uint32_t p = self->player;
    if (self->area != kNoArea && land->areas_changed[p] == 0) {
        self->data = reinterpret_cast<AiPlayerData*>(self->data)->next;
        const auto* head = LandOf(self)->lists[self->player].head;
        if (self->data == reinterpret_cast<std::uint32_t>(head)) {
            self->data = head->next;
        }
        self->area = reinterpret_cast<AiPlayerData*>(self->data)->area;
    } else {
        land->areas_changed[p] = 0;
        AiList& list = LandOf(self)->lists[self->player];
        self->area = FirstNode(list)->area;
        self->data = list.head->next;
    }
}

// A word passed by address in a stack dword, as the original does.
std::uint32_t WordSlot(std::uint32_t value) {
    return value & 0xffff;
}

}  // namespace

using StepFn = void(__fastcall*)(ComputerPlayer* self, void* edx);

void __fastcall CComputerPlayer_Step(ComputerPlayer* self, void* edx);

S3RE_HOOK(0x00550f80, CComputerPlayer_Step, StepFn, Original_CComputerPlayer_Step, CComputerPlayer_Step);

// Purpose: One logic step of a computer player: keeps its land evaluation
// current, then works on one of its economy areas in turn (the blackboard
// agents, the next building), with slower periodic checks (resources,
// foresters, tools, mine food) staggered between the computer players.
void __fastcall CComputerPlayer_Step(ComputerPlayer* self, void* /*edx*/) {
    ++self->steps;
    ++self->timer274;
    if (self->timer274 > self->player + kCountdownPeriod) {
        self->timer274 = static_cast<std::uint16_t>(self->player);
        CountDownCandidates(self);
    }
    reinterpret_cast<ThisFn>(kUpdateNextCells)(LandOf(self), nullptr);
    if (reinterpret_cast<ThisFn>(kProcessPendingEvents)(LandOf(self), nullptr) != 0) {
        return;
    }
    if (LandOf(self)->lists[self->player].count == 0) {
        // 0x55103a: without land a computer player can only fight.
        reinterpret_cast<std::uint32_t(__fastcall*)(void*)>(kSetCurrentPlayer)(self);
        BlackBoardAgent* attack = self->agents[Value(AgentKind::Attack)];
        reinterpret_cast<ThisFn>(attack->vtable[kRunSlot])(attack, nullptr);
        return;
    }
    MoveToNextNode(self);
    reinterpret_cast<std::uint32_t(__fastcall*)(void*)>(kSetCurrentPlayer)(self);

    ++self->timer278;
    if (self->timer278 > self->player + kResourcePeriod) {
        // 0x551108
        self->timer278 = static_cast<std::uint16_t>(self->player + kResourceStart);
        const std::uint32_t ecx = ((self->player + kResourcePeriod) & 0xffff0000) | (self->player & 0xffff);
        reinterpret_cast<std::uint32_t(__fastcall*)(std::uint32_t)>(kDiscountExploited)(ecx);
        const std::uint32_t area = WordSlot(self->area);
        const std::uint32_t player = WordSlot(self->player);
        reinterpret_cast<WordsFn>(kSetForestersWorking)(&area, &player);
        return;
    }
    reinterpret_cast<ThisFn>(kUpdateBlackboard)(self, nullptr);
    reinterpret_cast<ThisFn>(kStartBestBuilding)(self, nullptr);
    ++self->timer27c;
    if (self->timer27c > self->player + kEconomyPeriod) {
        // 0x55117e
        self->timer27c = static_cast<std::uint16_t>(self->player + kEconomyStart);
        const std::uint32_t player = WordSlot(self->player);
        const std::uint32_t area = WordSlot(self->area);
        reinterpret_cast<WordsFn>(kOrderMissingTools)(&player, &area);
        reinterpret_cast<WordsFn>(kFoodForMines)(&player, &area);
        using BasicEconomyFn = std::uint32_t(__fastcall*)(const std::uint32_t* player, void* edx, std::uint32_t data);
        reinterpret_cast<BasicEconomyFn>(kCheckBasicEconomy)(&player, nullptr, self->data);
    }
    reinterpret_cast<ThisFn>(kRaiseBuildPriorities)(self, nullptr);
    ++reinterpret_cast<AiPlayerData*>(self->data)->steps_since_expansion;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// A computer player (random bytes) with a test CEvaLand (ai_area_test_world.h)
// whose list for the player holds 0..4 area records; every callee but
// SetCurrentPlayer is replaced by a recorder (ScopedJump), ProcessPendingEvents
// answers 0 or 1 per case, agents[7] is a fake agent whose run slot records.
// The timers start near their limits so that every branch is taken.
// Compared: the object's bytes (land_evaluation and data as "own land" and
// node index), the records' countdowns and steps_since_expansion,
// areas_changed, the calls and the AI globals.

namespace {

using namespace ai_test;

const AiListNode* NextNode(const AiListNode* node) {
    return reinterpret_cast<const AiListNode*>(node->next);
}

struct Call {
    std::uintptr_t callee;
    std::uint32_t a;
    std::uint32_t b;
    bool operator==(const Call& other) const { return callee == other.callee && a == other.a && b == other.b; }
};

struct StepRecorder {
    std::vector<Call>* calls = nullptr;
    const ComputerPlayer* self = nullptr;
    std::uint32_t pendingResult = 0;
    std::uint32_t NodeIndex(std::uint32_t node) const {
        const AiList& list = LandOf(self)->lists[self->player];
        std::uint32_t index = 0;
        for (auto* n = NextNode(list.head); n != list.head; n = NextNode(n), ++index) {
            if (reinterpret_cast<std::uint32_t>(n) == node) {
                return index;
            }
        }
        return node == reinterpret_cast<std::uint32_t>(list.head) ? 0xfffe : 0xffff;
    }
    void Add(std::uintptr_t callee, std::uint32_t a, std::uint32_t b = 0) { calls->push_back(Call{callee, a, b}); }
};
StepRecorder g_recorder;

std::uint32_t IsSelf(const void* p) { return p == g_recorder.self ? 1 : 0; }
std::uint32_t IsLand(const void* p) { return p == LandOf(g_recorder.self) ? 1 : 0; }

template <std::uintptr_t Callee>
std::uint32_t __fastcall RecordThis(void* self, void*) {
    g_recorder.Add(Callee, Callee == kUpdateNextCells || Callee == kProcessPendingEvents ? IsLand(self) : IsSelf(self));
    return Callee == kProcessPendingEvents ? g_recorder.pendingResult : 0;
}

template <std::uintptr_t Callee>
std::uint32_t __fastcall RecordWords(const std::uint16_t* a, const std::uint16_t* b) {
    g_recorder.Add(Callee, *a, *b);
    return 0;
}

std::uint32_t __fastcall RecordDiscount(std::uint32_t ecx) {
    g_recorder.Add(kDiscountExploited, ecx);
    return 0;
}

std::uint32_t __fastcall RecordBasicEconomy(const std::uint16_t* player, void*, std::uint32_t data) {
    g_recorder.Add(kCheckBasicEconomy, *player, g_recorder.NodeIndex(data));
    return 0;
}

constexpr std::uintptr_t kAttackRun = 0xa77ac4;  // (a tag for the fake agent's run call)
std::uint32_t __fastcall RecordAttackRun(void* self, void*) {
    g_recorder.Add(kAttackRun, self == g_recorder.self->agents[Value(AgentKind::Attack)] ? 1 : 0);
    return 0;
}

struct Outcome {
    std::vector<std::uint8_t> object;
    std::vector<std::uint8_t> nodes;
    std::vector<std::uint32_t> changed;
    std::vector<Call> calls;
    std::array<std::uint32_t, 4> globals{};
    bool operator==(const Outcome& other) const {
        return object == other.object && nodes == other.nodes && changed == other.changed && calls == other.calls &&
               globals == other.globals;
    }
};

std::vector<std::uint8_t> NodeFields(const AiList& list) {
    std::vector<std::uint8_t> out;
    for (auto* n = NextNode(list.head); n != list.head; n = NextNode(n)) {
        const auto* data = reinterpret_cast<const AiPlayerData*>(n);
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&data->candidate_values);
        out.insert(out.end(), bytes, bytes + sizeof(data->candidate_values));
        const auto* steps = reinterpret_cast<const std::uint8_t*>(&data->steps_since_expansion);
        out.insert(out.end(), steps, steps + 4);
    }
    return out;
}

void CompareStep(test::Context& ctx, StepFn ours, int cases) {
    SavedGlobals saved;
    ComputerPlayer* const savedPlayer = g_aiPlayer;
    AiPlayerData* const savedData = g_aiPlayerData;
    AiPlayerSetup* const savedSetup = g_aiPlayerSetup;
    const std::uint8_t savedNumber = g_aiPlayerNumber;
    std::vector<AiGridCell> cells(kGridSide * kGridSide);
    test::ScopedJump j1(kUpdateNextCells, reinterpret_cast<void*>(&RecordThis<kUpdateNextCells>));
    test::ScopedJump j2(kProcessPendingEvents, reinterpret_cast<void*>(&RecordThis<kProcessPendingEvents>));
    test::ScopedJump j3(kUpdateBlackboard, reinterpret_cast<void*>(&RecordThis<kUpdateBlackboard>));
    test::ScopedJump j4(kStartBestBuilding, reinterpret_cast<void*>(&RecordThis<kStartBestBuilding>));
    test::ScopedJump j5(kRaiseBuildPriorities, reinterpret_cast<void*>(&RecordThis<kRaiseBuildPriorities>));
    test::ScopedJump j6(kDiscountExploited, reinterpret_cast<void*>(&RecordDiscount));
    test::ScopedJump j7(kSetForestersWorking, reinterpret_cast<void*>(&RecordWords<kSetForestersWorking>));
    test::ScopedJump j8(kOrderMissingTools, reinterpret_cast<void*>(&RecordWords<kOrderMissingTools>));
    test::ScopedJump j9(kFoodForMines, reinterpret_cast<void*>(&RecordWords<kFoodForMines>));
    test::ScopedJump j10(kCheckBasicEconomy, reinterpret_cast<void*>(&RecordBasicEconomy));
    const void* fakeVtable[16] = {};
    fakeVtable[kRunSlot] = reinterpret_cast<const void*>(&RecordAttackRun);
    BlackBoardAgent attack{fakeVtable, Value(AgentKind::Attack)};
    int paths[5] = {};  // pending, no land, resources, economy, plain
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        const std::uint32_t index = rng.Below(3);
        const std::uint32_t nodes = rng.Chance(1, 8) ? 0 : 1 + rng.Below(4);
        std::vector<std::uint8_t> start(sizeof(ComputerPlayer));
        rng.Fill(start.data(), start.size());
        const std::uint32_t pending = rng.Chance(1, 8) ? 1 + rng.Below(3) : 0;
        const bool changed = rng.Chance(1, 4);
        const std::uint32_t current = nodes == 0 ? 0 : rng.Below(nodes);
        const bool noArea = rng.Chance(1, 6);
        const auto nearLimit = [&](std::uint32_t limit) {
            return static_cast<std::uint16_t>(rng.Chance(1, 2) ? index + limit + rng.Below(2)
                                                               : rng.Below(index + limit + 1));
        };
        const std::uint16_t t274 = nearLimit(kCountdownPeriod);
        const std::uint16_t t278 = rng.Chance(1, 3) ? static_cast<std::uint16_t>(index + kResourcePeriod)
                                                    : static_cast<std::uint16_t>(rng.Below(index + kResourcePeriod));
        const std::uint16_t t27c = rng.Chance(1, 3) ? static_cast<std::uint16_t>(index + kEconomyPeriod)
                                                    : static_cast<std::uint16_t>(rng.Below(index + kEconomyPeriod));
        TestLand lands[2] = {TestLand(cells), TestLand(cells)};
        auto* record = static_cast<AiArea*>(msvc6::GameOperatorNew(sizeof(AiArea)));
        for (std::uint32_t k = 0; k < nodes; ++k) {
            MakeRecord(rng, record, static_cast<std::uint16_t>(rng.Next32()));
            for (std::uint16_t& value : record->candidate_values) {
                value = static_cast<std::uint16_t>(rng.Chance(1, 3) ? 0 : rng.Chance(1, 2) ? 1 : rng.Next32());
            }
            for (TestLand& land : lands) {
                land.Append(index, record);
            }
            DestroyRecord(record);
        }
        msvc6::GameOperatorDelete(record);
        Outcome outcome[2];
        const StepFn fns[2] = {Original_CComputerPlayer_Step, ours};
        for (int side = 0; side < 2; ++side) {
            AiLandEvaluation& land = lands[side].land;
            land.areas_changed[index] = changed ? 1 : 0;
            std::vector<std::uint8_t> bytes = start;
            auto* object = reinterpret_cast<ComputerPlayer*>(bytes.data());
            object->player = index;
            object->setup_index = start[0] % 3u;
            object->land_evaluation = reinterpret_cast<std::uint32_t>(&land);
            object->timer274 = t274;
            object->timer278 = t278;
            object->timer27c = t27c;
            object->agents[Value(AgentKind::Attack)] = &attack;
            auto* node = reinterpret_cast<AiListNode*>(land.lists[index].head->next);
            for (std::uint32_t k = 0; k < current; ++k) {
                node = reinterpret_cast<AiListNode*>(node->next);
            }
            object->data = reinterpret_cast<std::uint32_t>(node);
            if (noArea) {
                object->area = kNoArea;
            }
            // (globals a pending-event step leaves alone)
            g_aiPlayer = nullptr;
            g_aiPlayerData = nullptr;
            g_aiPlayerSetup = &g_aiPlayerSetups[0];
            g_aiPlayerNumber = 0xee;
            Outcome& out = outcome[side];
            g_recorder = StepRecorder{&out.calls, object, pending};
            fns[side](object, nullptr);
            out.globals = {g_aiPlayer.get() == object ? 1u : 0u, g_aiPlayerNumber,
                           static_cast<std::uint32_t>(g_aiPlayerSetup.get() - &g_aiPlayerSetups[0]),
                           g_recorder.NodeIndex(reinterpret_cast<std::uint32_t>(g_aiPlayerData.get()))};
            const std::uint32_t dataIndex = g_recorder.NodeIndex(object->data);
            object->data = dataIndex;
            object->land_evaluation = object->land_evaluation == reinterpret_cast<std::uint32_t>(&land) ? 1 : 0;
            object->agents[Value(AgentKind::Attack)] = nullptr;
            out.object = bytes;
            out.nodes = NodeFields(land.lists[index]);
            out.changed.assign(std::begin(land.areas_changed), std::end(land.areas_changed));
        }
        const std::vector<Call>& calls = outcome[0].calls;
        const auto called = [&](std::uintptr_t callee) {
            for (const Call& call : calls) {
                if (call.callee == callee) {
                    return true;
                }
            }
            return false;
        };
        ++paths[pending != 0               ? 0
                : called(kAttackRun)       ? 1
                : called(kDiscountExploited) ? 2
                : called(kFoodForMines)    ? 3
                                           : 4];
        if (!S3RE_EXPECT(outcome[0] == outcome[1])) {
            std::size_t k = 0;
            while (k < outcome[0].object.size() && outcome[0].object[k] == outcome[1].object[k]) {
                ++k;
            }
            ctx.Fail(__FILE__, __LINE__,
                     "  case %d, player %u, %u nodes: object byte %u, nodes %d, changed %d, calls %u/%u, globals %d", i,
                     index, nodes, static_cast<unsigned>(k), outcome[0].nodes == outcome[1].nodes ? 1 : 0,
                     outcome[0].changed == outcome[1].changed ? 1 : 0,
                     static_cast<unsigned>(outcome[0].calls.size()), static_cast<unsigned>(outcome[1].calls.size()),
                     outcome[0].globals == outcome[1].globals ? 1 : 0);
        }
    }
    g_aiPlayer = savedPlayer;
    g_aiPlayerData = savedData;
    g_aiPlayerSetup = savedSetup;
    g_aiPlayerNumber = savedNumber;
    if (cases >= 500) {
        for (int path : paths) {
            if (!S3RE_EXPECT(path > cases / 40)) {
                ctx.Fail(__FILE__, __LINE__, "  paths %d %d %d %d %d", paths[0], paths[1], paths[2], paths[3],
                         paths[4]);
                break;
            }
        }
    }
}

void __fastcall WrongStep(ComputerPlayer* self, void* edx) {
    // Wrong: the resource timer restarts at the player (not player + 1).
    const bool resources = self->timer278 + 1u > self->player + kResourcePeriod &&
                           LandOf(self)->lists[self->player].count != 0;
    CComputerPlayer_Step(self, edx);
    if (resources && self->timer278 == static_cast<std::uint16_t>(self->player + kResourceStart)) {
        self->timer278 = static_cast<std::uint16_t>(self->player);
    }
}

}  // namespace

S3RE_TEST(AiComputerPlayerStep_MatchOriginal) {
    CompareStep(ctx, &CComputerPlayer_Step, 1500);
}

S3RE_TEST(AiComputerPlayerStep_ComparisonDetectsWrongImplementation) {
    test::Context quiet(ctx.name(), 1, /*quiet=*/true);
    CompareStep(quiet, &WrongStep, 500);
    S3RE_EXPECT(quiet.failures() > 0);
}

}  // namespace s3re::game
