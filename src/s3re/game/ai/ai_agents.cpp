// The computer players' agents (BLACK_BOARD_AGENT subclasses, RTTI) and
// small helpers around them (computer players, batch 1;
// re/computer_players.md).
//
// The factory 0x56f6d0 (fastcall kind 0..10) allocates an agent, stores the
// subclass vtable and then calls one of these constructors (thiscall, no
// arguments). Each stores the kind with AI_SetWord4 (0x55ca60, word +4)
// and clears the subclass's fields. EAX: not used (the factory returns the
// object), so none of them is compared.
//   0x0056cd00 GLOBAL_EXPAND_EMPIRE_AGENT  kind 3, dword +8 = 0
//   0x00584680 COMPUTER_PLAYER_AGENT       kind 5, word +8 = 0
//   0x0057cda0 T_MAP_WORKER_AGENT          kind 6, dwords +8, +0xc and word +0x10 = 0
//   0x0055ebe0 T_ATTACK_AGENT              kind 7, dwords +8, +0xc = 0
//   0x00567850 T_ECONOMY_AGENT             kind 8, dwords +0x10..+0x28 = 0 (+8..+0xf untouched)
//   0x00586830 T_SAFETY_AGENT              kind 9, dwords +8..+0x18 = 0
//   0x00560dd0 CCentrumCheckAgent          kind 10, dwords +8..+0x24 = 0, then word +8 = 140
//
// Helpers:
//   std_multimap_end_580220 (0x00580220; was AI::List::begin_580220):
//     (iterator* out) thiscall, ret 4: *out = dword +4 (the MSVC6 _Tree's
//     _Head, i.e. end()); EAX = out. The container is the build-order tree
//     at AiPlayerData value +0x68c: MakeBuildOrders 0x57ce90 does end(),
//     -- (0x580270), * (0x580260), erase (0x580240), taking the last
//     (highest-key, best) entry.
//   std_multimap_empty_580230 (0x00580230; was AI::List::empty_580230):
//     () thiscall: EAX = (dword +0xc == 0) (_Size).
//   AI_Pair5_Construct (0x0059c5a0): (const u32* a, const u8* b) thiscall,
//     ret 8: dword +0 = *a, byte +4 = *b; EAX = this.
//   AI_CountPlayersTakingPart (0x00572370): () the number of the 20 player
//     records of g_aiWorld with Player::active != 0 (0x551b80 stores it in
//     0x7e02f0).

#include "game/ai/ai_layout.h"
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

// ai_helpers.cpp
std::uint16_t __fastcall AI_SetWord4(void* self, void* edx, std::uint32_t value);

using namespace ai;
using layout::kPlayers;

namespace {

// The agent kinds: ai::AgentKind (ai_layout.h).

constexpr std::uint16_t kCentrumCheckInitial = 140;  // 0x560dd0: word +8 after the clearing

// The subclasses' fields these constructors clear (meanings unknown).
struct GlobalExpandEmpireAgent {
    AiTaggedRecord base;
    std::uint16_t unk06;
    std::uint32_t unk08;
};
struct ComputerPlayerAgent {
    AiTaggedRecord base;
    std::uint16_t unk06;
    std::uint16_t unk08;
};
struct MapWorkerAgent {
    AiTaggedRecord base;
    std::uint16_t unk06;
    std::uint32_t unk08;
    std::uint32_t unk0c;
    std::uint16_t unk10;
};
struct AttackAgent {
    AiTaggedRecord base;
    std::uint16_t unk06;
    std::uint32_t unk08;
    std::uint32_t unk0c;
};
struct SafetyAgentDwords {  // ai::SafetyAgent names the fields (ai_safety_agent.cpp)
    AiTaggedRecord base;
    std::uint16_t unk06;
    std::uint32_t unk08[5];
};
struct CentrumCheckAgent {
    AiTaggedRecord base;
    std::uint16_t unk06;
    std::uint16_t unk08;     // 140 after the construction
    std::uint16_t unk0a;
    std::uint32_t unk0c[7];
};
static_assert(offsetof(GlobalExpandEmpireAgent, unk08) == 8);
static_assert(offsetof(MapWorkerAgent, unk10) == 0x10);
static_assert(sizeof(SafetyAgentDwords) == 0x1c);
static_assert(sizeof(CentrumCheckAgent) == 0x28);

void SetKind(void* self, AgentKind kind) { AI_SetWord4(self, nullptr, static_cast<std::uint16_t>(kind)); }

// An MSVC6 std::_Tree (map/multimap) object: the build-order tree.
struct TreeHeader {
    std::uint32_t unk0;   // +0 comparator and allocator (empty objects)
    std::uint32_t head;   // +4 _Head (end())
    std::uint32_t unk8;   // +8 _Multi
    std::uint32_t count;  // +0xc _Size
};
static_assert(offsetof(TreeHeader, count) == 0xc);

struct Pair5 {
    std::uint32_t first;
    std::uint8_t second;  // +4
};
static_assert(offsetof(Pair5, second) == 4);

}  // namespace

using ThisFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using ThisArgFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t* out);
using PairFn = std::uint32_t(__fastcall*)(void* self, void* edx, const std::uint32_t* a, const std::uint8_t* b);
using CountFn = std::uint32_t(__cdecl*)();

std::uint32_t __fastcall GLOBAL_EXPAND_EMPIRE_AGENT_Construct(void* self, void* edx);
std::uint32_t __fastcall COMPUTER_PLAYER_AGENT_Construct(void* self, void* edx);
std::uint32_t __fastcall T_MAP_WORKER_AGENT_Construct(void* self, void* edx);
std::uint32_t __fastcall T_ATTACK_AGENT_Construct(void* self, void* edx);
std::uint32_t __fastcall T_ECONOMY_AGENT_Construct(void* self, void* edx);
std::uint32_t __fastcall T_SAFETY_AGENT_Construct(void* self, void* edx);
std::uint32_t __fastcall CCentrumCheckAgent_Construct(void* self, void* edx);
std::uint32_t __fastcall std_multimap_end_580220(void* self, void* edx, std::uint32_t* out);
std::uint32_t __fastcall std_multimap_empty_580230(void* self, void* edx);
std::uint32_t __fastcall AI_Pair5_Construct(void* self, void* edx, const std::uint32_t* a, const std::uint8_t* b);
std::uint32_t __cdecl AI_CountPlayersTakingPart();

// ---------------------------------------------------------------- agent constructors

S3RE_HOOK(0x0056cd00, GLOBAL_EXPAND_EMPIRE_AGENT_Construct, ThisFn, Original_GLOBAL_EXPAND_EMPIRE_AGENT_Construct,
          GLOBAL_EXPAND_EMPIRE_AGENT_Construct);

// Purpose: Constructor of the agent that expands a computer player's land
// (kind 3).
std::uint32_t __fastcall GLOBAL_EXPAND_EMPIRE_AGENT_Construct(void* self, void* /*edx*/) {
    SetKind(self, AgentKind::GlobalExpandEmpire);
    static_cast<GlobalExpandEmpireAgent*>(self)->unk08 = 0;
    return 0;
}

S3RE_HOOK(0x00584680, COMPUTER_PLAYER_AGENT_Construct, ThisFn, Original_COMPUTER_PLAYER_AGENT_Construct,
          COMPUTER_PLAYER_AGENT_Construct);

// Purpose: Constructor of the computer player's general agent (kind 5).
std::uint32_t __fastcall COMPUTER_PLAYER_AGENT_Construct(void* self, void* /*edx*/) {
    SetKind(self, AgentKind::ComputerPlayer);
    static_cast<ComputerPlayerAgent*>(self)->unk08 = 0;
    return 0;
}

S3RE_HOOK(0x0057cda0, T_MAP_WORKER_AGENT_Construct, ThisFn, Original_T_MAP_WORKER_AGENT_Construct,
          T_MAP_WORKER_AGENT_Construct);

// Purpose: Constructor of the map worker agent (patrols, towers; kind 6).
std::uint32_t __fastcall T_MAP_WORKER_AGENT_Construct(void* self, void* /*edx*/) {
    SetKind(self, AgentKind::MapWorker);
    auto* agent = static_cast<MapWorkerAgent*>(self);
    agent->unk08 = 0;
    agent->unk0c = 0;
    agent->unk10 = 0;
    return 0;
}

S3RE_HOOK(0x0055ebe0, T_ATTACK_AGENT_Construct, ThisFn, Original_T_ATTACK_AGENT_Construct, T_ATTACK_AGENT_Construct);

// Purpose: Constructor of the attack agent (kind 7).
std::uint32_t __fastcall T_ATTACK_AGENT_Construct(void* self, void* /*edx*/) {
    SetKind(self, AgentKind::Attack);
    auto* agent = static_cast<AttackAgent*>(self);
    agent->unk08 = 0;
    agent->unk0c = 0;
    return 0;
}

S3RE_HOOK(0x00567850, T_ECONOMY_AGENT_Construct, ThisFn, Original_T_ECONOMY_AGENT_Construct,
          T_ECONOMY_AGENT_Construct);

// Purpose: Constructor of the economy agent (kind 8).
std::uint32_t __fastcall T_ECONOMY_AGENT_Construct(void* self, void* /*edx*/) {
    SetKind(self, AgentKind::Economy);
    // The seven dwords +0x10..+0x2b (EconomyAgent in ai_layout.h).
    auto* agent = static_cast<EconomyAgent*>(self);
    agent->run_count = 0;
    agent->unk12 = 0;
    for (std::uint32_t& value : agent->unk14) {
        value = 0;
    }
    return 0;
}

S3RE_HOOK(0x00586830, T_SAFETY_AGENT_Construct, ThisFn, Original_T_SAFETY_AGENT_Construct, T_SAFETY_AGENT_Construct);

// Purpose: Constructor of the safety (defence) agent (kind 9).
std::uint32_t __fastcall T_SAFETY_AGENT_Construct(void* self, void* /*edx*/) {
    SetKind(self, AgentKind::Safety);
    for (std::uint32_t& value : static_cast<SafetyAgentDwords*>(self)->unk08) {
        value = 0;
    }
    return 0;
}

S3RE_HOOK(0x00560dd0, CCentrumCheckAgent_Construct, ThisFn, Original_CCentrumCheckAgent_Construct,
          CCentrumCheckAgent_Construct);

// Purpose: Constructor of the agent that checks the computer player's
// centre (kind 10; "Centrum", probably its castle).
std::uint32_t __fastcall CCentrumCheckAgent_Construct(void* self, void* /*edx*/) {
    SetKind(self, AgentKind::CentrumCheck);
    auto* agent = static_cast<CentrumCheckAgent*>(self);
    agent->unk0a = 0;  // the original clears the eight dwords from +8 (rep stos), then sets +8
    for (std::uint32_t& value : agent->unk0c) {
        value = 0;
    }
    agent->unk08 = kCentrumCheckInitial;
    return 0;
}

// ---------------------------------------------------------------- helpers

S3RE_HOOK(0x00580220, std_multimap_end_580220, ThisArgFn, Original_std_multimap_end_580220,
          std_multimap_end_580220);

// Purpose: end() of the computer player's build-order tree (an iterator at
// its head node, returned through a pointer; inlined container code).
std::uint32_t __fastcall std_multimap_end_580220(void* self, void* /*edx*/, std::uint32_t* out) {
    *out = static_cast<const TreeHeader*>(self)->head;
    return reinterpret_cast<std::uint32_t>(out);
}

S3RE_HOOK(0x00580230, std_multimap_empty_580230, ThisFn, Original_std_multimap_empty_580230,
          std_multimap_empty_580230);

// Purpose: Whether the build-order tree is empty (inlined container code).
std::uint32_t __fastcall std_multimap_empty_580230(void* self, void* /*edx*/) {
    return static_cast<const TreeHeader*>(self)->count == 0 ? 1 : 0;
}

S3RE_HOOK(0x0059c5a0, AI_Pair5_Construct, PairFn, Original_AI_Pair5_Construct, AI_Pair5_Construct);

// Purpose: Constructs a (dword, byte) pair from two values (inlined
// container code; used by 0x59c330).
std::uint32_t __fastcall AI_Pair5_Construct(void* self, void* /*edx*/, const std::uint32_t* a, const std::uint8_t* b) {
    auto* pair = static_cast<Pair5*>(self);
    pair->first = *a;
    pair->second = *b;
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x00572370, AI_CountPlayersTakingPart, CountFn, Original_AI_CountPlayersTakingPart,
          AI_CountPlayersTakingPart);

// Purpose: How many players take part in the game (set up once for the
// computer players, 0x551b80).
std::uint32_t __cdecl AI_CountPlayersTakingPart() {
    const CGameData* world = g_aiWorld;
    std::uint32_t count = 0;
    for (std::size_t p = 0; p < kPlayers; ++p) {
        if (world->players[p].active != 0) {
            ++count;
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Objects are random 0x40-byte buffers; the player count runs on a world
// pair with g_aiWorld pointed at each side.

namespace {

struct AgentFunctions {
    ThisFn constructors[7];
    ThisArgFn begin;
    ThisFn empty;
    PairFn pair;
    CountFn count;
};

AgentFunctions OriginalAgents() {
    return {{Original_GLOBAL_EXPAND_EMPIRE_AGENT_Construct, Original_COMPUTER_PLAYER_AGENT_Construct,
             Original_T_MAP_WORKER_AGENT_Construct, Original_T_ATTACK_AGENT_Construct,
             Original_T_ECONOMY_AGENT_Construct, Original_T_SAFETY_AGENT_Construct,
             Original_CCentrumCheckAgent_Construct},
            Original_std_multimap_end_580220,
            Original_std_multimap_empty_580230,
            Original_AI_Pair5_Construct,
            Original_AI_CountPlayersTakingPart};
}

AgentFunctions OurAgents() {
    return {{&GLOBAL_EXPAND_EMPIRE_AGENT_Construct, &COMPUTER_PLAYER_AGENT_Construct, &T_MAP_WORKER_AGENT_Construct,
             &T_ATTACK_AGENT_Construct, &T_ECONOMY_AGENT_Construct, &T_SAFETY_AGENT_Construct,
             &CCentrumCheckAgent_Construct},
            &std_multimap_end_580220,
            &std_multimap_empty_580230,
            &AI_Pair5_Construct,
            &AI_CountPlayersTakingPart};
}

constexpr std::size_t kObjectBytes = 0x40;
constexpr std::size_t kWorldBytes = offsetof(CGameData, players) + sizeof(CGameData::players);

void CompareAgents(test::Context& ctx, const AgentFunctions& candidate, int cases) {
    const AgentFunctions original = OriginalAgents();
    test::WorldPair worlds(ctx, kWorldBytes);
    CGameData* const savedWorld = g_aiWorld;
    std::vector<std::uint8_t> a(kObjectBytes), b(kObjectBytes);
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(11));
        ctx.rng().Fill(a.data(), a.size());
        if (ctx.rng().Chance(1, 3)) {
            reinterpret_cast<TreeHeader*>(a.data())->count = 0;
        }
        b = a;
        std::uint32_t ra = 0, rb = 0;
        if (op < 7) {
            // EAX is not used by the factory.
            original.constructors[op](a.data(), nullptr);
            candidate.constructors[op](b.data(), nullptr);
        } else if (op == 7) {
            ra = original.begin(a.data(), nullptr, reinterpret_cast<std::uint32_t*>(a.data() + 0x21)) -
                 reinterpret_cast<std::uint32_t>(a.data());
            rb = candidate.begin(b.data(), nullptr, reinterpret_cast<std::uint32_t*>(b.data() + 0x21)) -
                 reinterpret_cast<std::uint32_t>(b.data());
        } else if (op == 8) {
            ra = original.empty(a.data(), nullptr);
            rb = candidate.empty(b.data(), nullptr);
        } else if (op == 9) {
            ra = original.pair(a.data(), nullptr, reinterpret_cast<std::uint32_t*>(a.data() + 0x13), a.data() + 0x31) -
                 reinterpret_cast<std::uint32_t>(a.data());
            rb = candidate.pair(b.data(), nullptr, reinterpret_cast<std::uint32_t*>(b.data() + 0x13), b.data() + 0x31) -
                 reinterpret_cast<std::uint32_t>(b.data());
        } else {
            for (std::size_t p = 0; p < kPlayers; ++p) {
                const std::int32_t active = ctx.rng().Chance(1, 2) ? 0 : static_cast<std::int32_t>(ctx.rng().Next32());
                worlds.Both([&](CGameData* w) { w->players[p].active = active; });
            }
            g_aiWorld = worlds.original();
            ra = original.count();
            g_aiWorld = worlds.ours();
            rb = candidate.count();
            g_aiWorld = savedWorld;
        }
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size());
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
        }
    }
    g_aiWorld = savedWorld;
}

std::uint32_t __fastcall WrongCentrumCheck(void* self, void* /*edx*/) {
    // Wrong: the initial value goes into the second word.
    CCentrumCheckAgent_Construct(self, nullptr);
    auto* agent = static_cast<CentrumCheckAgent*>(self);
    agent->unk08 = 0;
    agent->unk0a = kCentrumCheckInitial;
    return 0;
}

std::uint32_t __fastcall WrongEconomy(void* self, void* /*edx*/) {
    // Wrong: clears from +8 like the other agents.
    SetKind(self, AgentKind::Economy);
    std::memset(static_cast<std::uint8_t*>(self) + 8, 0, 7 * 4);
    return 0;
}

std::uint32_t __cdecl WrongCount() {
    // Wrong: counts only players with active == 1.
    std::uint32_t count = 0;
    for (std::size_t p = 0; p < kPlayers; ++p) {
        count += g_aiWorld.get()->players[p].active == 1 ? 1 : 0;
    }
    return count;
}

}  // namespace

S3RE_TEST(AiAgents_MatchOriginal) {
    CompareAgents(ctx, OurAgents(), 20000);
}

S3RE_TEST(AiAgents_ComparisonDetectsWrongImplementation) {
    const auto probe = [&](auto change) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        AgentFunctions wrong = OurAgents();
        change(wrong);
        CompareAgents(quiet, wrong, 5000);
        S3RE_EXPECT(quiet.failures() > 0);
    };
    probe([](AgentFunctions& f) { f.constructors[6] = &WrongCentrumCheck; });
    probe([](AgentFunctions& f) { f.constructors[4] = &WrongEconomy; });
    probe([](AgentFunctions& f) { f.count = &WrongCount; });
    probe([](AgentFunctions& f) { f.constructors[3] = f.constructors[0]; });
}

}  // namespace s3re::game
