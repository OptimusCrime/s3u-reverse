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
//   0x00596380 AI::LoadState(CMapFile*) thiscall, through the wrapper
//              0x551210 from CLogicThread::OnStart 0x53c9e0: chunk 0x15
//              (Dirk::CMapFile::GetChunk 0x4a60f0) missing (a new game):
//              g_aiSkipMilitaryReset = 0, AI::Army::Reset 0x591e00,
//              AI::ArmyState_Reset 0x593f50, AI::ResetAgentData 0x55b770
//              (the census), AI::InitBattleSpellSlots 0x590870; otherwise
//              the three chunks' data (+8) are copied into g_aiGroupOrders,
//              g_aiCensus and g_aiArmyState, each chunk released
//              (ReleaseChunk 0x4a6310; chunks 0x16 / 0x17 not null-checked)
//              and per player the catapult hunt steps and thief cursors
//              cleared. Both: the 20 hunter and catapult vectors emptied
//              (inlined vector::erase(begin, end): a copy loop over [end,
//              end), then end = begin), g_aiRebuildSettlersPending =
//              g_aiSkipMilitaryReset = 1. AL = 1; EAX's upper bytes: 0x100
//              (loaded) or those of catapult vector 19's old end (new game).
//   0x00551210 AI::LoadGlobals(CMapFile*) stdcall: LoadState; EAX its.
//   0x00551220 AI::SaveGlobals(CMapFile*) stdcall: SaveState; EAX its.

#include "game/ai/ai_layout.h"
#include "game/globals.h"
#include "game/layout.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace s3re::game {

using namespace ai;

namespace {

constexpr std::uintptr_t kResetEvaLandGlobals = 0x00553e00;    // AI::CEvaLand::ResetGlobals (cdecl)
constexpr std::uintptr_t kCountPlayersTakingPart = 0x00572370;  // AI::CountPlayersTakingPart (cdecl)
constexpr std::uintptr_t kWriteChunkData = 0x004a66d0;          // Dirk::CMapFile::WriteChunkData (thiscall)
constexpr std::uintptr_t kGetChunk = 0x004a60f0;                // Dirk::CMapFile::GetChunk (thiscall)
constexpr std::uintptr_t kReleaseChunk = 0x004a6310;            // Dirk::CMapFile::ReleaseChunk (thiscall)
constexpr std::uintptr_t kArmyReset = 0x00591e00;               // AI::Army::Reset (thiscall)
constexpr std::uintptr_t kArmyStateReset = 0x00593f50;          // AI::ArmyState_Reset (thiscall)
constexpr std::uintptr_t kResetAgentData = 0x0055b770;          // AI::ResetAgentData (thiscall)
constexpr std::uintptr_t kInitBattleSpellSlots = 0x00590870;    // AI::InitBattleSpellSlots (thiscall)
constexpr std::uintptr_t kLoadState = 0x00596380;
constexpr std::uintptr_t kSaveState = 0x00596330;
constexpr std::uint32_t kChunkHeaderBytes = 8;  // the data follows the chunk header

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

namespace {

const std::uint8_t* GetChunk(void* file, std::uint32_t type) {
    using Fn = const std::uint8_t*(__fastcall*)(void* file, void* edx, std::uint32_t type, std::uint32_t subIndex);
    return reinterpret_cast<Fn>(kGetChunk)(file, nullptr, type, 0);
}

void ReleaseChunk(void* file, std::uint32_t type) {
    using Fn = std::uint32_t(__fastcall*)(void* file, void* edx, std::uint32_t type, std::uint32_t subIndex);
    reinterpret_cast<Fn>(kReleaseChunk)(file, nullptr, type, 0);
}

// Copies a chunk's data into a global and releases the chunk.
void LoadChunk(void* file, std::uint32_t type, const std::uint8_t* chunk, void* to, std::uint32_t size) {
    std::memcpy(to, chunk + kChunkHeaderBytes, size);
    ReleaseChunk(file, type);
}

// 0x5963c7 / 0x5964ab: the inlined vector::erase(begin, end) of the 20
// hunter and catapult vectors (its copy loop runs over [end, end)).
void EmptyMilitaryVectors() {
    for (std::size_t p = 0; p < layout::kPlayers; ++p) {
        g_aiHunterVectors[p].end = g_aiHunterVectors[p].begin;
        g_aiCatapultVectors[p].end = g_aiCatapultVectors[p].begin;
    }
}

}  // namespace

std::uint32_t __fastcall AI_LoadState(void* file, void* edx);
std::uint32_t __stdcall AI_LoadGlobals(void* file);
std::uint32_t __stdcall AI_SaveGlobals(void* file);
using GlobalsFn = std::uint32_t(__stdcall*)(void* file);

S3RE_HOOK(0x00596380, AI_LoadState, ThisFn, Original_AI_LoadState, AI_LoadState);
S3RE_HOOK(0x00551210, AI_LoadGlobals, GlobalsFn, Original_AI_LoadGlobals, AI_LoadGlobals);
S3RE_HOOK(0x00551220, AI_SaveGlobals, GlobalsFn, Original_AI_SaveGlobals, AI_SaveGlobals);

// Purpose: Restores the computer players' military orders, census and
// army state from a savegame, or resets them for a new game.
std::uint32_t __fastcall AI_LoadState(void* file, void* /*edx*/) {
    const std::uint8_t* chunk = GetChunk(file, kChunkGroupOrders);
    std::uint32_t eax = 0x100;  // (the loop counter's leftover, see the header)
    if (chunk == nullptr) {
        g_aiSkipMilitaryReset = 0;
        reinterpret_cast<ThisFn>(kArmyReset)(&g_aiGroupOrders.get(), nullptr);
        reinterpret_cast<ThisFn>(kArmyStateReset)(&g_aiArmyState.get(), nullptr);
        reinterpret_cast<ThisFn>(kResetAgentData)(&g_aiCensus.get(), nullptr);
        reinterpret_cast<ThisFn>(kInitBattleSpellSlots)(&g_aiCommandSender.get(), nullptr);
        eax = g_aiCatapultVectors[layout::kPlayers - 1].end & 0xffffff00u;
        EmptyMilitaryVectors();
    } else {
        LoadChunk(file, kChunkGroupOrders, chunk, &g_aiGroupOrders.get(), kGroupOrdersBytes);
        LoadChunk(file, kChunkCensus, GetChunk(file, kChunkCensus), &g_aiCensus.get(), kCensusBytes);
        LoadChunk(file, kChunkArmyState, GetChunk(file, kChunkArmyState), &g_aiArmyState.get(), kArmyStateBytes);
        EmptyMilitaryVectors();
        for (std::size_t p = 0; p < layout::kPlayers; ++p) {
            g_aiCatapultHuntSteps[p] = 0;
            g_aiGroupOrders.get().thief_cursors[p] = 0;
        }
    }
    g_aiRebuildSettlersPending = 1;
    g_aiSkipMilitaryReset = 1;
    return eax | 1;
}

// Purpose: Loads the computer players' global state at a game's start
// (wrapper).
std::uint32_t __stdcall AI_LoadGlobals(void* file) {
    return reinterpret_cast<ThisFn>(kLoadState)(file, nullptr);
}

// Purpose: Saves the computer players' global state into a savegame
// (wrapper).
std::uint32_t __stdcall AI_SaveGlobals(void* file) {
    return reinterpret_cast<ThisFn>(kSaveState)(file, nullptr);
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
            const bool wrapper = rng.Chance(1, 4);  // AI::SaveGlobals 0x551220
            wrapper ? Original_AI_SaveGlobals(file) : Original_AI_SaveState(file, nullptr);
            const std::vector<test::StubCall> callsA = stubs.Take();
            wrapper ? AI_SaveGlobals(file) : save(file, nullptr);
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

// ---- LoadState: GetChunk / ReleaseChunk are recording stubs; GetChunk
// returns a buffer of random data per chunk type (chunk 0x15 sometimes
// missing). The resets run on the real globals; compared: EAX, the calls
// and every global LoadState writes (saved and restored around the test).

struct AiStateGlobals {
    AiGroupOrders orders;
    AiAgentData census;
    AiArmyState army;
    AiCommandSender sender;
    AiVector hunters[20];
    AiVector catapults[20];
    std::uint32_t hunt_steps[20];
    std::uint8_t rebuild, skip;
};

void ReadStateGlobals(AiStateGlobals& g) {
    g.orders = g_aiGroupOrders;
    g.census = g_aiCensus;
    g.army = g_aiArmyState;
    g.sender = g_aiCommandSender;
    std::memcpy(g.hunters, &g_aiHunterVectors.get(), sizeof(g.hunters));
    std::memcpy(g.catapults, &g_aiCatapultVectors.get(), sizeof(g.catapults));
    std::memcpy(g.hunt_steps, &g_aiCatapultHuntSteps.get(), sizeof(g.hunt_steps));
    g.rebuild = g_aiRebuildSettlersPending;
    g.skip = g_aiSkipMilitaryReset;
}

void WriteStateGlobals(const AiStateGlobals& g) {
    g_aiGroupOrders.get() = g.orders;
    g_aiCensus.get() = g.census;
    g_aiArmyState.get() = g.army;
    g_aiCommandSender.get() = g.sender;
    std::memcpy(&g_aiHunterVectors.get(), g.hunters, sizeof(g.hunters));
    std::memcpy(&g_aiCatapultVectors.get(), g.catapults, sizeof(g.catapults));
    std::memcpy(&g_aiCatapultHuntSteps.get(), g.hunt_steps, sizeof(g.hunt_steps));
    g_aiRebuildSettlersPending = g.rebuild;
    g_aiSkipMilitaryReset = g.skip;
}

void CompareLoadState(test::Context& ctx, ThisFn load, GlobalsFn loadWrapper, int cases) {
    auto saved = std::make_unique<AiStateGlobals>();
    ReadStateGlobals(*saved);
    auto start = std::make_unique<AiStateGlobals>();
    auto a = std::make_unique<AiStateGlobals>();
    auto b = std::make_unique<AiStateGlobals>();
    std::vector<std::uint8_t> chunks[3];
    for (auto& chunk : chunks) {
        chunk.resize(0x2200);
    }
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        rng.Fill(start.get(), sizeof(AiStateGlobals));
        for (auto& chunk : chunks) {
            rng.Fill(chunk.data(), chunk.size());
        }
        const bool fresh = rng.Chance(1, 3);
        test::CalleeStubs stubs;
        stubs.SetWorld(nullptr);
        stubs.Install<2, kGetChunk>([&](CGameData*, std::uint32_t, const std::uint32_t* args) -> std::uint32_t {
            const std::uint32_t type = args[0];
            if (type == kChunkGroupOrders && fresh) {
                return 0;
            }
            return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(chunks[(type - kChunkGroupOrders) % 3].data()));
        });
        stubs.Install<2, kReleaseChunk>();
        void* file = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x10000 + rng.Below(0x10000)) * 16);
        const bool wrapper = rng.Chance(1, 4);
        WriteStateGlobals(*start);
        const std::uint32_t ra = wrapper ? Original_AI_LoadGlobals(file) : Original_AI_LoadState(file, nullptr);
        ReadStateGlobals(*a);
        const std::vector<test::StubCall> callsA = stubs.Take();
        WriteStateGlobals(*start);
        const std::uint32_t rb = wrapper ? loadWrapper(file) : load(file, nullptr);
        ReadStateGlobals(*b);
        const std::vector<test::StubCall> callsB = stubs.Take();
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT(callsA == callsB);
        same &= S3RE_EXPECT_MEM_EQ(a.get(), b.get(), sizeof(AiStateGlobals));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, new game %d", i, fresh ? 1 : 0);
        }
    }
    WriteStateGlobals(*saved);
}

std::uint32_t __fastcall WrongLoadState(void* file, void* edx) {
    // Wrong: the thief cursors are kept.
    std::uint32_t cursors[20];
    std::memcpy(cursors, g_aiGroupOrders.get().thief_cursors, sizeof(cursors));
    const std::uint32_t result = AI_LoadState(file, edx);
    if (result == 0x101) {
        std::memcpy(g_aiGroupOrders.get().thief_cursors, cursors, sizeof(cursors));
    }
    return result;
}

}  // namespace

S3RE_TEST(AiState_MatchOriginal) {
    CompareState(ctx, &CComputerPlayer_InitAiGlobals, &AI_SaveState, 400);
    CompareLoadState(ctx, &AI_LoadState, &AI_LoadGlobals, 200);
}

S3RE_TEST(AiLoadState_ComparisonDetectsWrongImplementation) {
    test::Context quiet(ctx.name(), 1, /*quiet=*/true);
    CompareLoadState(quiet, &WrongLoadState, &AI_LoadGlobals, 200);
    S3RE_EXPECT(quiet.failures() > 0);
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
