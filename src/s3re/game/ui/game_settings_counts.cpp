// Player counts and team access of the game settings (g_gameSettings,
// game_settings_layout.h; Dirk\GameParams.cpp). All thiscall, under the
// settings' lock (Win32::CLock), with the inlined vector operator[] of
// game_settings_vector_access.h.
//
// GameSettings::HumanPlayerCount (0x00435ac0): () -> how many player setups
//   (0..length-1, the length re-read per round) have type 0 or 1 (getter
//   0x49f350; human or local). Callers: CNetThread::vf09, the menus and
//   the network code. Was GameSetup_sub_435ac0.
// GameSettings::ComputerPlayerCount (0x00435ca0): () -> the same for type 2
//   (computer). Callers: the game launch dialog and Menu_sub_461410. Was
//   GameSetup_sub_435ca0.
// GameSettings::TeamAt (0x00435680): (index) ret 4 -> Dirk::CTeam*: the
//   team vector's (+0x3c) element through the inlined operator[] (CTeam's
//   copy constructor 0x4a03a0 and destructor 0x4a04d0 in its growth).
//   Caller: CGameLaunchDialog::PlayerIdForRow. Was GameSetup_sub_435680.

#include "game/framework/critical_section.h"
#include "game/ui/game_settings_layout.h"
#include "game/ui/game_settings_vector_access.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/difftest.h"
#include "watcom/vector_base.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kSetupCopyConstruct = 0x0049eaf0;  // GamePlayerSetup(const&) (verified)
constexpr std::uintptr_t kSetupDestruct = 0x0049ec80;       // ~GamePlayerSetup (verified)
constexpr std::uintptr_t kTeamCopyConstruct = 0x004a03a0;   // Dirk::CTeam(const&) (verified)
constexpr std::uintptr_t kTeamDestruct = 0x004a04d0;        // ~Dirk::CTeam (verified)
constexpr std::uintptr_t kSetupType = 0x0049f350;           // GamePlayerSetup's type getter (verified)
constexpr std::uint32_t kTypeHuman = 0;
constexpr std::uint32_t kTypeLocal = 1;
constexpr std::uint32_t kTypeComputer = 2;

const GamePlayerSetup* SetupAt(GameSettings* self, std::uint32_t index) {
    return reinterpret_cast<const GamePlayerSetup*>(
        InlinedVectorAt<sizeof(GamePlayerSetup), kSetupCopyConstruct, kSetupDestruct>(
            reinterpret_cast<watcom::VectorBase*>(&self->player_setups), static_cast<std::int32_t>(index)));
}

std::uint32_t SetupType(const GamePlayerSetup* setup) {
    using GetterFn = std::uint32_t(__fastcall*)(const GamePlayerSetup* self, void* edx);
    return reinterpret_cast<GetterFn>(kSetupType)(setup, nullptr);
}

}  // namespace

// Purpose: How many human players (the local one included) the game being
// set up has.
std::uint32_t __fastcall GameSettings_HumanPlayerCount(GameSettings* self, void*) {
    GameLock lock(self->lock);
    std::uint32_t count = 0;
    for (std::uint32_t i = 0; i < self->player_setups.length; ++i) {
        const std::uint32_t type = SetupType(SetupAt(self, i));
        if (type == kTypeHuman || type == kTypeLocal) {
            ++count;
        }
    }
    return count;
}

// Purpose: How many computer players the game being set up has.
std::uint32_t __fastcall GameSettings_ComputerPlayerCount(GameSettings* self, void*) {
    GameLock lock(self->lock);
    std::uint32_t count = 0;
    for (std::uint32_t i = 0; i < self->player_setups.length; ++i) {
        if (SetupType(SetupAt(self, i)) == kTypeComputer) {
            ++count;
        }
    }
    return count;
}

// Purpose: Gives one team of the game being set up (the game launch
// dialog's player rows).
CTeam* __fastcall GameSettings_TeamAt(GameSettings* self, void*, std::int32_t index) {
    GameLock lock(self->lock);
    return reinterpret_cast<CTeam*>(
        InlinedVectorAt<sizeof(CTeam), kTeamCopyConstruct, kTeamDestruct>(&self->teams, index));
}

using CountFn = std::uint32_t(__fastcall*)(GameSettings* self, void* edx);
using TeamAtFn = CTeam*(__fastcall*)(GameSettings* self, void* edx, std::int32_t index);
S3RE_HOOK(0x00435ac0, GameSettings_HumanPlayerCount, CountFn, Original_GameSettings_HumanPlayerCount,
          GameSettings_HumanPlayerCount);
S3RE_HOOK(0x00435ca0, GameSettings_ComputerPlayerCount, CountFn, Original_GameSettings_ComputerPlayerCount,
          GameSettings_ComputerPlayerCount);
S3RE_HOOK(0x00435680, GameSettings_TeamAt, TeamAtFn, Original_GameSettings_TeamAt, GameSettings_TeamAt);

namespace {

// Settings whose player vector holds 0..6 setups (plain records: the
// counts read only the type) and whose team vector holds 1..4 teams with
// in-range indexes; the out-of-range and growth paths of the shared
// operator[] are tested with PlayerSetup's (GameSettings_PlayerSetup_*).
struct CountSettings {
    alignas(8) std::uint8_t bytes[sizeof(GameSettings)];
    std::vector<GamePlayerSetup> setups;
    std::vector<CTeam> teams;
    CountSettings(test::Rng& rng, std::uint32_t players, std::uint32_t teamCount) : setups(players), teams(teamCount) {
        rng.Fill(bytes, sizeof(bytes));
        InitializeCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(bytes));
        for (GamePlayerSetup& setup : setups) {
            rng.Fill(&setup, sizeof(setup));
            setup.type = rng.Below(5);
        }
        rng.Fill(teams.data(), teams.size() * sizeof(CTeam));
        PlayerSetupVector& v = get()->player_setups;
        v.data = setups.empty() ? nullptr : setups.data();
        v.capacity = players;
        v.filled = static_cast<std::int32_t>(players);
        v.length = players;
        watcom::VectorBase& t = get()->teams;
        t.data = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(teams.data()));
        t.capacity = teamCount;
        t.filled = static_cast<std::int32_t>(teamCount);
        t.length = teamCount;
    }
    ~CountSettings() { DeleteCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(bytes)); }
    CountSettings(const CountSettings&) = delete;
    CountSettings& operator=(const CountSettings&) = delete;
    GameSettings* get() { return reinterpret_cast<GameSettings*>(bytes); }
};

struct CountFunctions {
    CountFn humans;
    CountFn computers;
    TeamAtFn teamAt;
};

void CompareCounts(test::Context& ctx, const CountFunctions& candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        CountSettings settings(ctx.rng(), ctx.rng().Below(7), 1 + ctx.rng().Below(4));
        const auto index = static_cast<std::int32_t>(ctx.rng().Below(8)) - 2;
        if (!S3RE_EXPECT_EQ(Original_GameSettings_HumanPlayerCount(settings.get(), nullptr),
                            candidate.humans(settings.get(), nullptr)) ||
            !S3RE_EXPECT_EQ(Original_GameSettings_ComputerPlayerCount(settings.get(), nullptr),
                            candidate.computers(settings.get(), nullptr)) ||
            !S3RE_EXPECT(Original_GameSettings_TeamAt(settings.get(), nullptr, index) ==
                         candidate.teamAt(settings.get(), nullptr, index))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: index %d", i, index);
            return;
        }
    }
}

// Wrong: counts the local player as a computer.
std::uint32_t __fastcall WrongComputerPlayerCount(GameSettings* self, void*) {
    GameLock lock(self->lock);
    std::uint32_t count = 0;
    for (std::uint32_t i = 0; i < self->player_setups.length; ++i) {
        const std::uint32_t type = SetupType(SetupAt(self, i));
        if (type == kTypeComputer || type == kTypeLocal) {
            ++count;
        }
    }
    return count;
}

}  // namespace

S3RE_TEST(GameSettingsCounts_MatchOriginal) {
    CompareCounts(ctx, {&GameSettings_HumanPlayerCount, &GameSettings_ComputerPlayerCount, &GameSettings_TeamAt},
                  2000);
}

S3RE_TEST(GameSettingsCounts_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareCounts(probe, {&GameSettings_HumanPlayerCount, &WrongComputerPlayerCount, &GameSettings_TeamAt}, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
