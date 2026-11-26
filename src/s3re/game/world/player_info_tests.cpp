// Tests of CGameData::InitPlayerInfo (player_info.cpp): the original
// against ours on two identical random worlds (up to the end of the
// players), with the callees that leave the simulation replaced by
// recording stubs: GameSettings::PlayerSetup (fake setups of type 0..4),
// IsMultiplayer, the lobby's player name (writes a string into its
// argument), CGraphicsData::TextAddress (a text or null) and
// LogPlayerLandResources (verified on its own; it would scan the map).
// g_playerInfo is saved, its names emptied before each run, compared by
// text afterwards (and freed), and restored at the end; g_strengthDivisor
// and g_gameOutcome likewise. The string members run (ours).

#include "game/globals.h"
#include "game/layout.h"
#include "game/ui/game_settings_layout.h"
#include "game/world.h"
#include "msvc6/string.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace s3re::game {

using WorldFn = void(__fastcall*)(CGameData* world, void* edx);
void __fastcall CGameData_InitPlayerInfo(CGameData* world, void* edx);
extern WorldFn Original_CGameData_InitPlayerInfo;

namespace {

using msvc6::String;

constexpr std::size_t kTestWorld = offsetof(CGameData, players) + sizeof(CGameData::players) + 64;
constexpr std::uintptr_t kPlayerSetup = 0x004351e0;
constexpr std::uintptr_t kIsMultiplayer = 0x004318d0;
constexpr std::uintptr_t kLobbyPlayerName = 0x005db260;
constexpr std::uintptr_t kTextAddress = 0x005faaa0;
constexpr std::uintptr_t kLogPlayerLand = 0x00507f60;

struct Case {
    std::uint32_t types[layout::kPlayers];
    std::uint32_t multiplayer;
    bool textsMissing;
};

GamePlayerSetup g_setups[layout::kPlayers];
int g_lobbyCalls = 0;
const char kLocalName[] = "Local player";
const char kComputerName[] = "Computer";

struct Outcome {
    std::vector<std::string> names;
    std::vector<std::int32_t> fields;
    std::uint32_t divisor;
    std::int32_t outcome;
    std::vector<test::StubCall> calls;
};

std::vector<test::StubCall> RunWithStubs(WorldFn fn, CGameData* world, const Case& c) {
    for (std::uint32_t p = 0; p < layout::kPlayers; ++p) {
        g_setups[p].type = c.types[p];
    }
    test::CalleeStubs stubs;
    stubs.SetWorld(world);
    stubs.Install<1, kPlayerSetup>([](CGameData*, std::uint32_t, const std::uint32_t* args) {
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&g_setups[args[0] % layout::kPlayers]));
    });
    stubs.Install<0, kIsMultiplayer>([&c](CGameData*, std::uint32_t, const std::uint32_t*) { return c.multiplayer; });
    g_lobbyCalls = 0;
    stubs.Install<1, kLobbyPlayerName>([](CGameData*, std::uint32_t, const std::uint32_t* args) {
        auto* out = reinterpret_cast<String*>(static_cast<std::uintptr_t>(args[0]));
        const std::string name = "Lobby " + std::to_string(g_lobbyCalls++);
        msvc6::String_FromChars(out, nullptr, name.c_str(), static_cast<std::uint32_t>(name.size()));
        return args[0];
    });
    stubs.Install<2, kTextAddress>([&c](CGameData*, std::uint32_t, const std::uint32_t* args) {
        if (c.textsMissing) {
            return 0u;
        }
        const char* text = args[0] == 0x900c1 ? kLocalName : kComputerName;
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(text));
    });
    stubs.Install<1, kLogPlayerLand>();
    fn(world, nullptr);
    std::vector<test::StubCall> calls = stubs.Take();
    for (test::StubCall& call : calls) {
        // ECX of all but LogPlayerLand is outside the world (g_gameSettings,
        // g_gfxData, the fake setups); the lobby name's argument is a local.
        if (call.callee != kLogPlayerLand) {
            call.self = 0;
        }
        if (call.callee == kLobbyPlayerName) {
            call.args[0] = 0;
        }
    }
    return calls;
}

Outcome Run(WorldFn fn, CGameData* world, const Case& c) {
    for (PlayerInfo& info : g_playerInfo.get()) {
        info.name = String{};
    }
    g_strengthDivisor = 0x5a5a;
    g_gameOutcome = 0x77;
    Outcome out;
    out.calls = RunWithStubs(fn, world, c);
    for (PlayerInfo& info : g_playerInfo.get()) {
        out.names.push_back(info.name.ptr != nullptr ? std::string(info.name.ptr, info.name.len) + "|" +
                                                           std::to_string(static_cast<std::uint8_t>(info.name.ptr[-1]))
                                                     : std::string("<null>"));
        out.fields.push_back(info.unk00);
        out.fields.push_back(info.unk04);
        for (std::int32_t f : info.fields) {
            out.fields.push_back(f);
        }
        msvc6::String_Destroy(&info.name, nullptr);
    }
    out.divisor = g_strengthDivisor;
    out.outcome = g_gameOutcome;
    return out;
}

void CompareInit(test::Context& ctx, WorldFn candidate, int cases) {
    PlayerInfo saved[layout::kPlayers];
    std::memcpy(saved, &g_playerInfo.get(), sizeof(saved));
    const std::uint32_t savedDivisor = g_strengthDivisor;
    const std::int32_t savedOutcome = g_gameOutcome;
    for (GamePlayerSetup& setup : g_setups) {
        std::memset(&setup, 0, sizeof(setup));
    }
    for (int i = 0; i < cases; ++i) {
        test::Rng& rng = ctx.rng();
        test::WorldPair worlds(ctx, kTestWorld);
        Case c{};
        for (std::uint32_t p = 0; p < layout::kPlayers; ++p) {
            c.types[p] = rng.Chance(1, 8) ? rng.Next32() : rng.Below(5);
            const std::int32_t active = rng.Chance(1, 3) ? 0 : static_cast<std::int32_t>(rng.Next32() | 1);
            worlds.Both([&](CGameData* w) { w->players[p].active = active; });
        }
        c.multiplayer = rng.Chance(1, 2) ? 0 : (rng.Chance(1, 2) ? 1 : 0x100);
        c.textsMissing = rng.Chance(1, 4);
        const Outcome a = Run(Original_CGameData_InitPlayerInfo, worlds.original(), c);
        const Outcome b = Run(candidate, worlds.ours(), c);
        const bool same = S3RE_EXPECT(a.names == b.names) & S3RE_EXPECT(a.fields == b.fields) &
                          S3RE_EXPECT_EQ(a.divisor, b.divisor) & S3RE_EXPECT_EQ(a.outcome, b.outcome) &
                          S3RE_EXPECT(a.calls == b.calls) & worlds.Same(ctx, 0, kTestWorld);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            break;
        }
    }
    std::memcpy(&g_playerInfo.get(), saved, sizeof(saved));
    g_strengthDivisor = savedDivisor;
    g_gameOutcome = savedOutcome;
}

// Wrong: a computer player's index field changed.
void __fastcall WrongInitPlayerInfo(CGameData* world, void* edx) {
    CGameData_InitPlayerInfo(world, edx);
    for (std::uint32_t p = 0; p < layout::kPlayers; ++p) {
        if (g_setups[p].type == 2) {
            g_playerInfo[p].fields[1] ^= 1;
        }
    }
}

}  // namespace

S3RE_TEST(CGameData_InitPlayerInfo_MatchesOriginal) {
    CompareInit(ctx, &CGameData_InitPlayerInfo, 60);
}

S3RE_TEST(CGameData_InitPlayerInfo_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareInit(probe, &WrongInitPlayerInfo, 10);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
