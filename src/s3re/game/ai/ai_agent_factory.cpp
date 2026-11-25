// The computer players' agent factory (Rainer\compGlobs.cpp;
// re/computer_players.md "Classes").
//
//   0x0056f6d0 AI::CreateAgent(kind) fastcall (ECX, low word; jump table
//              0x56fa68): operator new of the agent's size, the vtable
//              stored, the constructor called (thiscall; it stores the
//              kind and the fields); null: Win32::ThrowOutOfMemory
//              ("agentPtr", "E:\Siedler3\Rainer\compGlobs.cpp", the line of
//              the kind). EAX = the agent; 0 for a kind above 10.

#include "game/ai/ai_layout.h"
#include "game/globals.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/difftest.h"

#include <cstdint>
#include <iterator>
#include <cstring>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kThrowOutOfMemory = 0x00610960;  // Win32::ThrowOutOfMemory (never returns)

struct AgentClass {
    std::uint32_t size;
    std::uint32_t vtable;
    std::uintptr_t constructor;
    std::uint32_t line;  // of the out-of-memory check in compGlobs.cpp
};

// By kind (ai::AgentKind), from the jump table 0x56fa68.
constexpr AgentClass kAgentClasses[] = {
    {8, vtables::kLocalWareRequestAgent, 0x0057ab50, 169},
    {8, vtables::kHouseGroundRequestAgent, 0x005764f0, 177},
    {8, vtables::kHouseRequestAgent, 0x005787a0, 185},
    {12, vtables::kGlobalExpandEmpireAgent, 0x0056cd00, 193},
    {8, vtables::kDoBuildHouseAgent, 0x005654b0, 201},
    {16, vtables::kComputerPlayerAgent, 0x00584680, 209},
    {20, vtables::kMapWorkerAgent, 0x0057cda0, 217},
    {16, vtables::kAttackAgent, 0x0055ebe0, 225},
    {160, vtables::kEconomyAgent, 0x00567850, 233},
    {28, vtables::kSafetyAgent, 0x00586830, 240},
    {40, vtables::kCentrumCheckAgent, 0x00560dd0, 248},
};

}  // namespace

using CreateAgentFn = void*(__fastcall*)(std::uint32_t kind);

void* __fastcall AI_CreateAgent(std::uint32_t kind);

S3RE_HOOK(0x0056f6d0, AI_CreateAgent, CreateAgentFn, Original_AI_CreateAgent, AI_CreateAgent);

// Purpose: Makes a computer player's agent of a kind (economy, attack,
// safety, map worker, the request handlers, ...).
void* __fastcall AI_CreateAgent(std::uint32_t kind) {
    const std::uint32_t index = kind & 0xffff;
    if (index >= std::size(kAgentClasses)) {
        return nullptr;
    }
    const AgentClass& agentClass = kAgentClasses[index];
    void* agent = msvc6::GameOperatorNew(agentClass.size);
    if (agent != nullptr) {
        *static_cast<std::uint32_t*>(agent) = agentClass.vtable;
        reinterpret_cast<void(__fastcall*)(void*, void*)>(agentClass.constructor)(agent, nullptr);
    } else {
        using Fn = void(__fastcall*)(const char* text, const char* file, std::uint32_t line);
        reinterpret_cast<Fn>(kThrowOutOfMemory)(texts::kAgentPtr.c_str(), texts::kCompGlobsSourceFile.c_str(),
                                               agentClass.line);
    }
    return agent;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Every kind (and kinds with high bits or above 10) made by the original
// and by ours; compared: null or not, the vtable and the kind word (+4,
// stored by the constructors). The other fields of a new heap block may be
// left uninitialized by a constructor, so they are compared by the
// emulator (whose heap is the same for both).

namespace {

void CompareCreateAgent(test::Context& ctx, CreateAgentFn create, int cases) {
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        const std::uint32_t kind = rng.Below(14) | (rng.Chance(1, 4) ? rng.Next32() << 16 : 0);
        auto* a = static_cast<std::uint8_t*>(Original_AI_CreateAgent(kind));
        auto* b = static_cast<std::uint8_t*>(create(kind));
        bool same = S3RE_EXPECT_EQ(a == nullptr, b == nullptr);
        if (a != nullptr && b != nullptr) {
            same &= S3RE_EXPECT_MEM_EQ(a, b, 6);
            msvc6::GameOperatorDelete(a);
            msvc6::GameOperatorDelete(b);
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, kind 0x%x", i, kind);
        }
    }
}

void* __fastcall WrongCreateAgent(std::uint32_t kind) {
    // Wrong: the safety and economy kinds swapped.
    const std::uint32_t index = kind & 0xffff;
    return AI_CreateAgent(index == 8 ? 9 : index == 9 ? 8 : kind);
}

}  // namespace

S3RE_TEST(AiCreateAgent_MatchOriginal) {
    CompareCreateAgent(ctx, &AI_CreateAgent, 500);
}

S3RE_TEST(AiCreateAgent_ComparisonDetectsWrongImplementation) {
    test::Context quiet(ctx.name(), 1, /*quiet=*/true);
    CompareCreateAgent(quiet, &WrongCreateAgent, 500);
    S3RE_EXPECT(quiet.failures() > 0);
}

}  // namespace s3re::game
