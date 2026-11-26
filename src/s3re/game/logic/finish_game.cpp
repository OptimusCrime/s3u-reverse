// CLogicThread::FinishGame (0x0053e720; the original name, from its
// DebugLog; Volker\LogicInit.cpp): () thiscall, EAX not used. Called by
// ProcessEvents (a button of menu 0xb) and 0x525369.
//
//   Only once: with +0x234 set it logs "*** FinishGame cancelled (already
//   called)" and returns. Otherwise it logs "*** FinishGame() called ***",
//   sets +0x234 and computes each of the 20 player info records'
//   (g_playerInfo) final score +0x20 = +0x28 + +0x2c + +0x30 + +0x34 + +0x38
//   + 2 * (+0x24 + +0x3c) + 5 * +0x40 (32-bit), then halves +0x34
//   (unsigned shift, after adding it). Then the ranking: for rank 1..20 the
//   unranked (+0x04 == 0) playing (world player +0x00 non-zero) player with
//   the highest score (unsigned; on equal scores the later player) gets
//   +0x04 = rank; once nobody is left, the remaining ranks are skipped.
//   Last it posts message 0x801 to the application thread
//   (Win32::CMessagedThread::PostThreadMessage 0x60f480 on g_app + 0xc).

#include "game/debug_log.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/logic/logic_thread_layout.h"
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

namespace {

constexpr std::uintptr_t kPostThreadMessage = 0x0060f480;  // thiscall (message, wParam, lParam)
constexpr std::uint32_t kAppThreadOffset = 0xc;            // the messaged thread inside g_app
constexpr std::uint32_t kMessageGameFinished = 0x801;      // probably "show the end-of-game screen"
constexpr std::int32_t kRanks = 20;

// PlayerInfo::fields indexes (+0x18 + 4 * index).
constexpr std::size_t kScore = 2;      // +0x20
constexpr std::size_t kField24 = 3;    // +0x24 (counts twice)
constexpr std::size_t kField28 = 4;    // +0x28
constexpr std::size_t kMeals = 5;      // +0x2c (PlayerTally::meals)
constexpr std::size_t kOreFound = 6;   // +0x30 (PlayerTally::ore_found)
constexpr std::size_t kPoints = 7;     // +0x34 (PlayerTally::points; halved afterwards)
constexpr std::size_t kTally0c = 8;    // +0x38 (PlayerTally::unk0c)
constexpr std::size_t kTally10 = 9;    // +0x3c (PlayerTally::unk10, counts twice)
constexpr std::size_t kKills = 10;     // +0x40 (PlayerTally::unk14, counts five times)

std::uint32_t Field(const PlayerInfo& info, std::size_t index) {
    return static_cast<std::uint32_t>(info.fields[index]);
}

void ComputeScores() {
    for (PlayerInfo& info : g_playerInfo.get()) {
        const std::uint32_t score = 5 * Field(info, kKills) + 2 * (Field(info, kTally10) + Field(info, kField24)) +
                                    Field(info, kTally0c) + Field(info, kField28) + Field(info, kOreFound) +
                                    Field(info, kMeals) + Field(info, kPoints);
        info.fields[kScore] = static_cast<std::int32_t>(score);
        info.fields[kPoints] = static_cast<std::int32_t>(Field(info, kPoints) >> 1);
    }
}

void RankPlayers(const CGameData* world) {
    for (std::int32_t rank = 1; rank <= kRanks; ++rank) {
        std::uint32_t best = 0;
        std::int32_t winner = -1;
        for (std::uint32_t p = 0; p < layout::kPlayers; ++p) {
            const PlayerInfo& info = g_playerInfo[p];
            if (info.unk04 == 0 && world->players[p].active != 0 && Field(info, kScore) >= best) {
                best = Field(info, kScore);
                winner = static_cast<std::int32_t>(p);
            }
        }
        if (winner != -1) {
            g_playerInfo[winner].unk04 = rank;
        }
    }
}

void PostToApp(std::uint32_t message) {
    using Fn = std::uint32_t(__fastcall*)(void*, void*, std::uint32_t, std::uint32_t, std::uint32_t);
    reinterpret_cast<Fn>(kPostThreadMessage)(g_app.get() + kAppThreadOffset, nullptr, message, 0, 0);
}

}  // namespace

using FinishGameFn = void(__fastcall*)(CLogicThread* self, void* edx);

// Purpose: Ends the game: computes every player's final score and ranking
// (for the statistics screen) and tells the application to leave the game.
void __fastcall CLogicThread_FinishGame(CLogicThread* self, void*) {
    if (self->game_finished != 0) {
        GameDebugLog(texts::kFinishGameCancelled);
        return;
    }
    GameDebugLog(texts::kFinishGameCalled);
    self->game_finished = 1;
    ComputeScores();
    RankPlayers(self->world);
    PostToApp(kMessageGameFinished);
}

S3RE_HOOK(0x0053e720, CLogicThread_FinishGame, FinishGameFn, Original_CLogicThread_FinishGame,
          CLogicThread_FinishGame);

// ---------------------------------------------------------------------------
// Tests: the player info table (S3.EXE data) gets random ranks and fields
// (the names are left alone and the table is restored afterwards), the
// world's players random active flags; PostThreadMessage is a recording
// stub, DebugLog is captured. Compared: the table, the thread, the calls,
// the logs.
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kThreadBytes = sizeof(CLogicThread);

struct Outcome {
    std::vector<std::uint8_t> table;
    std::vector<std::uint8_t> thread;
    std::vector<test::StubCall> calls;
    std::vector<DebugLogCall> logs;
};

void RandomTable(test::Rng& rng) {
    for (PlayerInfo& info : g_playerInfo.get()) {
        info.unk00 = static_cast<std::int32_t>(rng.Next32());
        info.unk04 = rng.Chance(2, 3) ? 0 : (rng.Chance(1, 2) ? rng.Range(1, 20) : static_cast<std::int32_t>(rng.Next32()));
        for (std::int32_t& field : info.fields) {
            field = rng.Chance(1, 3) ? static_cast<std::int32_t>(rng.Next32()) : rng.Range(0, 50);
        }
        if (rng.Chance(1, 4)) {
            info.fields[kScore] = 0;
        }
    }
    if (rng.Chance(1, 3)) {  // equal scores
        for (PlayerInfo& info : g_playerInfo.get()) {
            for (std::size_t k = kField24; k <= kKills; ++k) {
                info.fields[k] = 1;
            }
        }
    }
}

Outcome Run(FinishGameFn function, const std::vector<std::uint8_t>& table, const std::vector<std::uint8_t>& thread,
            CGameData* world) {
    Outcome out;
    std::memcpy(&g_playerInfo.get(), table.data(), table.size());
    out.thread = thread;
    CLogicThread* t = reinterpret_cast<CLogicThread*>(out.thread.data());
    t->world = world;
    test::CalleeStubs stubs;
    stubs.Install<3, kPostThreadMessage>();
    DebugLogRecorder logs;
    function(t, nullptr);
    out.calls = stubs.Take();
    out.logs = logs.Take();
    out.table.resize(table.size());
    std::memcpy(out.table.data(), &g_playerInfo.get(), table.size());
    return out;
}

void CompareFinishGame(test::Context& ctx, FinishGameFn candidate, int cases) {
    constexpr std::size_t kTableBytes = sizeof(PlayerInfo) * layout::kPlayers;
    std::vector<std::uint8_t> saved(kTableBytes);
    std::memcpy(saved.data(), &g_playerInfo.get(), kTableBytes);
    test::WorldPair worlds(ctx, offsetof(CGameData, players) + sizeof(layout::Player) * layout::kPlayers);
    for (int i = 0; i < cases; ++i) {
        for (std::uint32_t p = 0; p < layout::kPlayers; ++p) {
            worlds.original()->players[p].active =
                ctx.rng().Chance(1, 3) ? 0 : static_cast<std::int32_t>(ctx.rng().Next32() | 1);
        }
        RandomTable(ctx.rng());
        std::vector<std::uint8_t> table(kTableBytes);
        std::memcpy(table.data(), &g_playerInfo.get(), kTableBytes);
        std::vector<std::uint8_t> thread(kThreadBytes);
        ctx.rng().Fill(thread.data(), kThreadBytes);
        reinterpret_cast<CLogicThread*>(thread.data())->game_finished =
            static_cast<std::uint8_t>(ctx.rng().Chance(1, 5) ? ctx.rng().Below(256) : 0);
        const Outcome expected = Run(Original_CLogicThread_FinishGame, table, thread, worlds.original());
        const Outcome actual = Run(candidate, table, thread, worlds.original());
        bool same = S3RE_EXPECT_MEM_EQ(expected.table.data(), actual.table.data(), kTableBytes);
        same &= S3RE_EXPECT_MEM_EQ(expected.thread.data(), actual.thread.data(), kThreadBytes);
        same &= S3RE_EXPECT(expected.calls == actual.calls);
        same &= S3RE_EXPECT(expected.logs == actual.logs);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            break;
        }
    }
    std::memcpy(&g_playerInfo.get(), saved.data(), kTableBytes);
}

// Wrong: on equal scores the first player wins.
void __fastcall WrongFinishGame(CLogicThread* self, void* edx) {
    if (self->game_finished != 0) {
        CLogicThread_FinishGame(self, edx);
        return;
    }
    GameDebugLog(texts::kFinishGameCalled);
    self->game_finished = 1;
    ComputeScores();
    for (std::int32_t rank = 1; rank <= kRanks; ++rank) {
        std::uint32_t best = 0;
        std::int32_t winner = -1;
        for (std::uint32_t p = 0; p < layout::kPlayers; ++p) {
            const PlayerInfo& info = g_playerInfo[p];
            if (info.unk04 == 0 && self->world->players[p].active != 0 &&
                (winner == -1 ? Field(info, kScore) >= best : Field(info, kScore) > best)) {
                best = Field(info, kScore);
                winner = static_cast<std::int32_t>(p);
            }
        }
        if (winner != -1) {
            g_playerInfo[winner].unk04 = rank;
        }
    }
    PostToApp(kMessageGameFinished);
}

}  // namespace

S3RE_TEST(CLogicThread_FinishGame_MatchesOriginal) {
    CompareFinishGame(ctx, &CLogicThread_FinishGame, 2000);
}

S3RE_TEST(CLogicThread_FinishGame_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareFinishGame(probe, &WrongFinishGame, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
