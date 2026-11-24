// The computer players' global state: its set-up when a computer player is
// made and its saving into the savegame (computer players, session 54;
// re/computer_players.md).
//
//   0x00551b80 CComputerPlayer::InitAiGlobals() thiscall, from the
//              constructor 0x550900: g_aiWorld = this->world (+0x75c),
//              g_aiMapSizeX and g_aiMapSizeY = the low word of its map size
//              (+0x2d2d0), g_aiPlayer = g_aiLandEvaluation = 0,
//              AI::CEvaLand::ResetGlobals 0x553e00,
//              g_aiMarkBuildingTypesPending = 1, g_aiPlayersTakingPart =
//              AI::CountPlayersTakingPart 0x572370, g_aiUnk7e340c = 0. EAX =
//              the count (the caller keeps EAX's upper bytes).
//   0x00596330 AI::SaveState(CMapFile*) thiscall (Rainer\CompMapWorkerAgent.cpp),
//              through the wrapper 0x551220 from the savegame writer
//              0x509980: Dirk::CMapFile::WriteChunkData 0x4a66d0 (type, 0,
//              size, data) for chunk 0x15 g_aiGroupOrders (0x20f8 bytes),
//              0x16 g_aiCensus (0x1ad8) and 0x17 g_aiArmyState (0x51c). EAX:
//              WriteChunkData's leftover, not used (the caller's loop
//              overwrites it).

#include "game/ai/ai_layout.h"
#include "game/globals.h"
#include "game/layout.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <vector>

namespace s3re::game {

using namespace ai;

namespace {

constexpr std::uintptr_t kResetEvaLandGlobals = 0x00553e00;    // AI::CEvaLand::ResetGlobals (cdecl)
constexpr std::uintptr_t kCountPlayersTakingPart = 0x00572370;  // AI::CountPlayersTakingPart (cdecl)
constexpr std::uintptr_t kWriteChunkData = 0x004a66d0;          // Dirk::CMapFile::WriteChunkData (thiscall)

using GlobalFn = std::uint32_t(__cdecl*)();
using WriteChunkFn = void(__fastcall*)(void* file, void* edx, std::uint32_t type, std::uint32_t subIndex,
                                       std::uint32_t size, const void* data);

// The savegame chunks of the computer players' state.
constexpr std::uint32_t kChunkGroupOrders = 0x15;
constexpr std::uint32_t kChunkCensus = 0x16;
constexpr std::uint32_t kChunkArmyState = 0x17;
// The sizes the original writes (the structs in ai_layout.h are these sizes).
constexpr std::uint32_t kGroupOrdersBytes = 0x20f8;
constexpr std::uint32_t kCensusBytes = 0x1ad8;
constexpr std::uint32_t kArmyStateBytes = 0x51c;

}  // namespace

using ThisFn = std::uint32_t(__fastcall*)(void* self, void* edx);

std::uint32_t __fastcall CComputerPlayer_InitAiGlobals(void* self, void* edx);
std::uint32_t __fastcall AI_SaveState(void* file, void* edx);

S3RE_HOOK(0x00551b80, CComputerPlayer_InitAiGlobals, ThisFn, Original_CComputerPlayer_InitAiGlobals,
          CComputerPlayer_InitAiGlobals);
S3RE_HOOK(0x00596330, AI_SaveState, ThisFn, Original_AI_SaveState, AI_SaveState);

// Purpose: Points the computer players' globals at the game's world when a
// computer player is made (map size, no current player or land evaluation
// yet) and resets the shared land evaluation's bookkeeping.
std::uint32_t __fastcall CComputerPlayer_InitAiGlobals(void* self, void* /*edx*/) {
    const auto* computer = static_cast<const ComputerPlayer*>(self);
    g_aiWorld = reinterpret_cast<CGameData*>(computer->world);
    const auto size = static_cast<std::uint16_t>(reinterpret_cast<const CGameData*>(computer->world)->map_size);
    g_aiMapSizeX = size;
    g_aiPlayer = nullptr;
    g_aiMapSizeY = size;
    g_aiLandEvaluation = 0;
    reinterpret_cast<GlobalFn>(kResetEvaLandGlobals)();
    g_aiMarkBuildingTypesPending = 1;
    const std::uint32_t players = reinterpret_cast<GlobalFn>(kCountPlayersTakingPart)();
    g_aiPlayersTakingPart = players;
    g_aiUnk7e340c = 0;
    return players;
}

// Purpose: Writes the computer players' military orders, census and army
// state into a savegame.
std::uint32_t __fastcall AI_SaveState(void* file, void* /*edx*/) {
    const auto write = reinterpret_cast<WriteChunkFn>(kWriteChunkData);
    static_assert(sizeof(AiGroupOrders) == kGroupOrdersBytes && sizeof(AiAgentData) == kCensusBytes &&
                  sizeof(AiArmyState) == kArmyStateBytes);
    write(file, nullptr, kChunkGroupOrders, 0, kGroupOrdersBytes, &g_aiGroupOrders.get());
    write(file, nullptr, kChunkCensus, 0, kCensusBytes, &g_aiCensus.get());
    write(file, nullptr, kChunkArmyState, 0, kArmyStateBytes, &g_aiArmyState.get());
    return 0;  // EAX is left over in the original; the caller doesn't use it
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// InitAiGlobals on a computer player whose world is a test world with
// random map size and player records; compared: EAX and every global it
// writes (including the CEvaLand reference count and instance). SaveState
// with WriteChunkData replaced by a recording stub; compared: the calls.

namespace {

struct InitGlobals {
    CGameData* world;
    ComputerPlayer* player;
    std::uint32_t land, refCount, pending, players, unk;
    AiLandEvaluation* evaLand;
    std::uint16_t sizeX, sizeY;
};

InitGlobals ReadInitGlobals() {
    return {g_aiWorld,           g_aiPlayer,     g_aiLandEvaluation, g_aiEvaLandRefCount, g_aiMarkBuildingTypesPending,
            g_aiPlayersTakingPart, g_aiUnk7e340c, g_aiEvaLand,        g_aiMapSizeX,        g_aiMapSizeY};
}

void WriteInitGlobals(const InitGlobals& g) {
    g_aiWorld = g.world;
    g_aiPlayer = g.player;
    g_aiLandEvaluation = g.land;
    g_aiEvaLandRefCount = g.refCount;
    g_aiMarkBuildingTypesPending = g.pending;
    g_aiPlayersTakingPart = g.players;
    g_aiUnk7e340c = g.unk;
    g_aiEvaLand = g.evaLand;
    g_aiMapSizeX = g.sizeX;
    g_aiMapSizeY = g.sizeY;
}

bool SameInitGlobals(test::Context& ctx, const InitGlobals& a, const InitGlobals& b) {
    bool same = S3RE_EXPECT(a.world == b.world);
    same &= S3RE_EXPECT(a.player == b.player);
    same &= S3RE_EXPECT_EQ(a.land, b.land);
    same &= S3RE_EXPECT_EQ(a.refCount, b.refCount);
    same &= S3RE_EXPECT_EQ(a.pending, b.pending);
    same &= S3RE_EXPECT_EQ(a.players, b.players);
    same &= S3RE_EXPECT_EQ(a.unk, b.unk);
    same &= S3RE_EXPECT(a.evaLand == b.evaLand);
    same &= S3RE_EXPECT_EQ(a.sizeX, b.sizeX);
    same &= S3RE_EXPECT_EQ(a.sizeY, b.sizeY);
    return same;
}

void CompareState(test::Context& ctx, ThisFn init, ThisFn save, int cases) {
    const InitGlobals saved = ReadInitGlobals();
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    std::vector<std::uint8_t> computerBytes(sizeof(ComputerPlayer));
    auto* computer = reinterpret_cast<ComputerPlayer*>(computerBytes.data());
    computer->world = reinterpret_cast<std::uint32_t>(world);
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        bool same = true;
        if (rng.Chance(1, 2)) {
            world->map_size = rng.Chance(1, 4) ? rng.Next32() : 64 + rng.Below(400);
            for (layout::Player& player : world->players) {
                player.active = rng.Chance(1, 2) ? 0 : static_cast<std::int32_t>(rng.Next32());
            }
            InitGlobals start{};
            rng.Fill(&start, sizeof(start));
            WriteInitGlobals(start);
            const std::uint32_t ra = Original_CComputerPlayer_InitAiGlobals(computer, nullptr);
            const InitGlobals a = ReadInitGlobals();
            WriteInitGlobals(start);
            const std::uint32_t rb = init(computer, nullptr);
            const InitGlobals b = ReadInitGlobals();
            same &= S3RE_EXPECT_EQ(ra, rb);
            same &= SameInitGlobals(ctx, a, b);
        } else {
            test::CalleeStubs stubs;
            stubs.SetWorld(nullptr);
            stubs.Install<4, kWriteChunkData>();
            void* file = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x10000 + rng.Below(0x10000)) * 16);
            Original_AI_SaveState(file, nullptr);
            const std::vector<test::StubCall> callsA = stubs.Take();
            save(file, nullptr);
            const std::vector<test::StubCall> callsB = stubs.Take();
            same &= S3RE_EXPECT(callsA == callsB);
            same &= S3RE_EXPECT_EQ(callsA.size(), static_cast<std::size_t>(3));
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
        }
    }
    WriteInitGlobals(saved);
}

std::uint32_t __fastcall WrongInit(void* self, void* edx) {
    const std::uint32_t result = CComputerPlayer_InitAiGlobals(self, edx);
    g_aiMapSizeY = static_cast<std::uint16_t>(g_aiMapSizeY - 1);  // wrong: y size one smaller
    return result;
}

std::uint32_t __fastcall WrongSave(void* file, void* /*edx*/) {
    // wrong: the army state chunk left out
    const auto write = reinterpret_cast<WriteChunkFn>(kWriteChunkData);
    write(file, nullptr, kChunkGroupOrders, 0, kGroupOrdersBytes, &g_aiGroupOrders.get());
    write(file, nullptr, kChunkCensus, 0, kCensusBytes, &g_aiCensus.get());
    return 0;
}

}  // namespace

S3RE_TEST(AiState_MatchOriginal) {
    CompareState(ctx, &CComputerPlayer_InitAiGlobals, &AI_SaveState, 400);
}

S3RE_TEST(AiState_ComparisonDetectsWrongImplementation) {
    for (int k = 0; k < 2; ++k) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareState(quiet, k == 0 ? &WrongInit : &CComputerPlayer_InitAiGlobals,
                     k == 0 ? &AI_SaveState : &WrongSave, 400);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

}  // namespace s3re::game
