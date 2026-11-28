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
// GameSettings::TeamLayoutKind (0x00436b90): () -> a bit naming the shape
//   of the teams, without the lock. Each test is GameSetup_sub_4a0950 on a
//   team (a CTeam query with four arguments, probably a check of the
//   players' clans; called as original code). With exactly two teams, for
//   each team i (the index check 0x437e20 picks it): the tests (1, 1, 0, 0),
//   (2, 2, 0, 0), (3, 4, 0, 0), (5, -1, 0, 0), and (1, -1, 0, 0) on team 0
//   when i is 0 or (0, 0, 1, -1) on team 1 when i is 1 (inlined operator[],
//   growth through 0x4382a0). The first test passing for both gives 1 when
//   Option2A 0x435aa0 is set, else 4; then 8, 0x10, 0x20 for the next
//   three, 0x80 for the pair, else 0x100. Otherwise: no teams gives 2;
//   else each team (inlined operator[], growth through resize 0x437d00) is
//   tested with (1, 1, 0, 0) and (1, -1, 0, 0): all passing the first gives
//   2, all passing the second 0x40, else 0x100. Callers: SetPlayerType,
//   Menu_sub_461410, CMenuSharedData::UnlinkFromLobbyGame,
//   CGameLaunchDialog::OnWarp. Was GameSetup_sub_436b90.
// GameSettings::SetPlayerType (0x004347f0): (index, type) ret 8, under the
//   lock: when the setup's type differs (the setup through the inlined
//   operator[] with growth through resize 0x437820, before each use):
//   leaving type 1 (the local player) first calls Net_sub_4717e0 on the
//   object +0x80 with the setup's network id (getter 0x49f4d0; network
//   code, called as original code); then GamePlayerSetup::SetType 0x49f360.
//   In both cases, unless TeamLayoutKind is 1, the bytes +0x2a and +0x2b are
//   cleared. Callers: the game launch dialog's remove-player and
//   close-slot cells. Was GameSetup_sub_4347f0.

#include "game/framework/critical_section.h"
#include "game/globals.h"
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

constexpr std::uintptr_t kTeamIndexCheck = 0x00437e20;   // WCValSortedVector<CTeam>::IndexCheck (verified)
constexpr std::uintptr_t kTeamResize = 0x00437d00;       // WCValSortedVector<CTeam>::resize (verified)
constexpr std::uintptr_t kTeamResizeClamp = 0x004382a0;  // GameSetup_sub_4382a0 (a team vector resize; original)
constexpr std::uintptr_t kPlayerResize = 0x00437820;     // WCValOrderedVector<CPlayer>::resize (verified)
constexpr std::uintptr_t kTeamTest = 0x004a0950;         // GameSetup_sub_4a0950 (a CTeam query; original)
constexpr std::uintptr_t kOption2A = 0x00435aa0;         // GameSettings::Option2A (ours)
constexpr std::uintptr_t kNetForget = 0x004717e0;        // Net_sub_4717e0 (network code; original)
constexpr std::uintptr_t kSetupNetId = 0x0049f4d0;       // GamePlayerSetup's +0x10 getter (verified)
constexpr std::uintptr_t kSetupSetType = 0x0049f360;     // GamePlayerSetup::SetType (verified)
constexpr std::uintptr_t kTeamLayoutKind = 0x00436b90;   // GameSettings::TeamLayoutKind (ours, through its hook)

bool TeamTest(std::uintptr_t team, std::int32_t a, std::int32_t b, std::int32_t c, std::int32_t d) {
    using TestFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::int32_t a, std::int32_t b, std::int32_t c,
                                              std::int32_t d);
    return (reinterpret_cast<TestFn>(kTeamTest)(reinterpret_cast<void*>(team), nullptr, a, b, c, d) & 0xff) != 0;
}

std::uintptr_t CheckedTeam(watcom::VectorBase* teams, std::int32_t index) {
    using IndexCheckFn = std::int32_t(__fastcall*)(watcom::VectorBase* self, void* edx, std::int32_t index);
    const std::int32_t checked = reinterpret_cast<IndexCheckFn>(kTeamIndexCheck)(teams, nullptr, index);
    return static_cast<std::uintptr_t>(teams->data + static_cast<std::uint32_t>(checked) * sizeof(CTeam));
}

template <std::uintptr_t Resize, bool ClampLength>
std::uintptr_t TeamAtGrowing(watcom::VectorBase* teams, std::int32_t index) {
    return InlinedVectorAtResize<sizeof(CTeam), Resize, ClampLength>(teams, index);
}

GamePlayerSetup* GrowingSetupAt(GameSettings* self, std::int32_t index) {
    return reinterpret_cast<GamePlayerSetup*>(InlinedVectorAtResize<sizeof(GamePlayerSetup), kPlayerResize, true>(
        reinterpret_cast<watcom::VectorBase*>(&self->player_setups), index));
}

// TeamLayoutKind's results.
namespace team_layout {
constexpr std::uint32_t kPairsWithOption = 0x1;
constexpr std::uint32_t kSinglesOrNone = 0x2;
constexpr std::uint32_t kPairs = 0x4;
constexpr std::uint32_t kTwoTwo = 0x8;
constexpr std::uint32_t kThreeFour = 0x10;
constexpr std::uint32_t kFiveUp = 0x20;
constexpr std::uint32_t kAllOneUp = 0x40;
constexpr std::uint32_t kSplit = 0x80;
constexpr std::uint32_t kOther = 0x100;
}  // namespace team_layout

}  // namespace

// Purpose: Classifies how the players are split into teams (probably which
// team modes the game launch dialog and the lobby can offer).
std::uint32_t __fastcall GameSettings_TeamLayoutKind(GameSettings* self, void*) {
    watcom::VectorBase* teams = &self->teams;
    if (teams->length == 2) {
        bool pairs = true;
        bool twoTwo = true;
        bool threeFour = true;
        bool fiveUp = true;
        bool split = true;
        for (std::uint32_t i = 0; i < teams->length; ++i) {
            const auto index = static_cast<std::int32_t>(i);
            pairs = TeamTest(CheckedTeam(teams, index), 1, 1, 0, 0) && pairs;
            twoTwo = TeamTest(CheckedTeam(teams, index), 2, 2, 0, 0) && twoTwo;
            threeFour = TeamTest(CheckedTeam(teams, index), 3, 4, 0, 0) && threeFour;
            fiveUp = TeamTest(CheckedTeam(teams, index), 5, -1, 0, 0) && fiveUp;
            if (i == 0) {
                split = TeamTest(TeamAtGrowing<kTeamResizeClamp, false>(teams, 0), 1, -1, 0, 0) && split;
            } else if (i == 1) {
                split = TeamTest(TeamAtGrowing<kTeamResizeClamp, false>(teams, 1), 0, 0, 1, -1) && split;
            }
        }
        if (pairs) {
            using OptionFn = std::uint32_t(__fastcall*)(GameSettings* self, void* edx);
            const bool option = (reinterpret_cast<OptionFn>(kOption2A)(self, nullptr) & 0xff) != 0;
            return option ? team_layout::kPairsWithOption : team_layout::kPairs;
        }
        if (twoTwo) {
            return team_layout::kTwoTwo;
        }
        if (threeFour) {
            return team_layout::kThreeFour;
        }
        if (fiveUp) {
            return team_layout::kFiveUp;
        }
        return split ? team_layout::kSplit : team_layout::kOther;
    }
    if (teams->length == 0) {
        return team_layout::kSinglesOrNone;
    }
    bool singles = true;
    bool allOneUp = true;
    for (std::uint32_t i = 0; i < teams->length; ++i) {
        const auto index = static_cast<std::int32_t>(i);
        singles = TeamTest(TeamAtGrowing<kTeamResize, true>(teams, index), 1, 1, 0, 0) && singles;
        allOneUp = TeamTest(TeamAtGrowing<kTeamResize, true>(teams, index), 1, -1, 0, 0) && allOneUp;
    }
    if (singles) {
        return team_layout::kSinglesOrNone;
    }
    return allOneUp ? team_layout::kAllOneUp : team_layout::kOther;
}

// Purpose: Changes the type of one player slot (human, computer, open...)
// from the game launch dialog, telling the network code when the local
// player leaves its slot.
void __fastcall GameSettings_SetPlayerType(GameSettings* self, void*, std::int32_t index, std::uint32_t type) {
    using SetupGetterFn = std::uint32_t(__fastcall*)(const GamePlayerSetup* self, void* edx);
    using NetForgetFn = void(__fastcall*)(std::uintptr_t object, void* edx, std::uint32_t netId, std::uint32_t zero);
    using SetTypeFn = void(__fastcall*)(GamePlayerSetup* self, void* edx, std::uint32_t type);
    constexpr std::uint32_t kLocal = 1;
    GameLock lock(self->lock);
    const std::uint32_t current = SetupType(GrowingSetupAt(self, index));
    if (current != type) {
        if (current == kLocal) {
            const std::uint32_t netId =
                reinterpret_cast<SetupGetterFn>(kSetupNetId)(GrowingSetupAt(self, index), nullptr);
            reinterpret_cast<NetForgetFn>(kNetForget)(self->value80, nullptr, netId, 0);
        }
        reinterpret_cast<SetTypeFn>(kSetupSetType)(GrowingSetupAt(self, index), nullptr, type);
    }
    using LayoutFn = std::uint32_t(__fastcall*)(GameSettings* self, void* edx);
    if (reinterpret_cast<LayoutFn>(kTeamLayoutKind)(self, nullptr) != team_layout::kPairsWithOption) {
        self->option2a = 0;
        self->by_clan = 0;
    }
}

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
using SetPlayerTypeFn = void(__fastcall*)(GameSettings* self, void* edx, std::int32_t index, std::uint32_t type);
S3RE_HOOK(0x00436b90, GameSettings_TeamLayoutKind, CountFn, Original_GameSettings_TeamLayoutKind,
          GameSettings_TeamLayoutKind);
S3RE_HOOK(0x004347f0, GameSettings_SetPlayerType, SetPlayerTypeFn, Original_GameSettings_SetPlayerType,
          GameSettings_SetPlayerType);

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

namespace {

using TeamConstructFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t id);
using AddPlayerIdFn = void(__fastcall*)(void* self, void* edx, std::uint32_t id);
using TeamDestroyFn = void(__fastcall*)(void* self, void* edx);
constexpr std::uintptr_t kTeamConstructId = 0x004a02d0;  // Dirk::CTeam::CTeam(id) (verified)
constexpr std::uintptr_t kAddPlayerId = 0x004a06d0;      // Dirk::CTeam::AddPlayerId (verified)

// Two settings objects with the same 0..3 real teams (CTeam(id) and
// AddPlayerId with indexes into 0..6 player setups of random types). The
// team test GameSetup_sub_4a0950 reads the players through g_gameSettings:
// while a function runs, g_gameSettings' player vector is the test
// object's (its IsHost byte held at 0, so SetType calls no network code).
class LayoutSettings {
public:
    LayoutSettings(test::Rng& rng, std::uint32_t teamCount, const std::vector<std::vector<std::uint32_t>>& members,
                   std::vector<GamePlayerSetup>& setups)
        : teams_(teamCount) {
        rng.Fill(bytes_, sizeof(bytes_));
        InitializeCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(bytes_));
        for (std::uint32_t t = 0; t < teamCount; ++t) {
            reinterpret_cast<TeamConstructFn>(kTeamConstructId)(&teams_[t], nullptr, t);
            for (const std::uint32_t id : members[t]) {
                reinterpret_cast<AddPlayerIdFn>(kAddPlayerId)(&teams_[t], nullptr, id);
            }
        }
        GameSettings* s = get();
        s->teams.vtable = g_gameSettings->teams.vtable;
        s->teams.data = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(teams_.data()));
        s->teams.capacity = teamCount;
        s->teams.filled = static_cast<std::int32_t>(teamCount);
        s->teams.length = teamCount;
        s->player_setups.vtable = g_gameSettings->player_setups.vtable;
        s->player_setups.data = setups.data();
        s->player_setups.capacity = static_cast<std::uint32_t>(setups.size());
        s->player_setups.filled = static_cast<std::int32_t>(setups.size());
        s->player_setups.length = static_cast<std::uint32_t>(setups.size());
    }
    ~LayoutSettings() {
        for (CTeam& team : teams_) {
            reinterpret_cast<TeamDestroyFn>(kTeamDestruct)(&team, nullptr);
        }
        DeleteCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(bytes_));
    }
    LayoutSettings(const LayoutSettings&) = delete;
    LayoutSettings& operator=(const LayoutSettings&) = delete;
    GameSettings* get() { return reinterpret_cast<GameSettings*>(bytes_); }

    template <typename F>
    std::uint32_t WithGlobalPlayers(F call) {
        PlayerSetupVector saved;
        std::memcpy(&saved, &g_gameSettings->player_setups, sizeof(saved));
        const std::uint8_t savedHost = g_gameSettings->is_host;
        g_gameSettings->is_host = 0;
        std::memcpy(&g_gameSettings->player_setups, &get()->player_setups, sizeof(saved));
        const std::uint32_t result = call();
        std::memcpy(&g_gameSettings->player_setups, &saved, sizeof(saved));
        g_gameSettings->is_host = savedHost;
        return result;
    }

private:
    alignas(8) std::uint8_t bytes_[sizeof(GameSettings)];
    std::vector<CTeam> teams_;
};

struct LayoutFunctions {
    CountFn layout;
    SetPlayerTypeFn setType;
};

void CompareLayout(test::Context& ctx, const LayoutFunctions& candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        test::Rng& rng = ctx.rng();
        const std::uint32_t players = rng.Below(7);
        std::vector<GamePlayerSetup> setupsA(players);
        for (GamePlayerSetup& setup : setupsA) {
            std::memset(&setup, 0, sizeof(setup));
            setup.type = rng.Below(4);
        }
        std::vector<GamePlayerSetup> setupsB = setupsA;
        const std::uint32_t teamCount = rng.Chance(2, 3) ? 2 : rng.Below(4);
        std::vector<std::vector<std::uint32_t>> members(teamCount);
        for (auto& team : members) {
            const std::uint32_t n = players == 0 ? 0 : rng.Below(4);
            for (std::uint32_t k = 0; k < n; ++k) {
                team.push_back(rng.Below(players));
            }
        }
        const test::Rng saved = rng;
        LayoutSettings a(rng, teamCount, members, setupsA);
        rng = saved;
        LayoutSettings b(rng, teamCount, members, setupsB);
        const std::uint32_t option = rng.Below(2);
        a.get()->option2a = static_cast<std::uint8_t>(option);
        b.get()->option2a = static_cast<std::uint8_t>(option);
        const std::uint32_t kindA = a.WithGlobalPlayers([&] { return Original_GameSettings_TeamLayoutKind(a.get(), nullptr); });
        const std::uint32_t kindB = b.WithGlobalPlayers([&] { return candidate.layout(b.get(), nullptr); });
        if (!S3RE_EXPECT_EQ(kindA, kindB)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: %u teams, %u players", i, teamCount, players);
            return;
        }
        if (players == 0) {
            continue;
        }
        // SetPlayerType: never away from type 1 (that calls network code).
        const auto index = static_cast<std::int32_t>(rng.Below(players));
        const std::uint32_t type = setupsA[index].type == 1 ? 1 : rng.Below(4);
        a.WithGlobalPlayers([&] {
            Original_GameSettings_SetPlayerType(a.get(), nullptr, index, type);
            return 0u;
        });
        b.WithGlobalPlayers([&] {
            candidate.setType(b.get(), nullptr, index, type);
            return 0u;
        });
        if (!S3RE_EXPECT(a.get()->option2a == b.get()->option2a && a.get()->by_clan == b.get()->by_clan) ||
            !S3RE_EXPECT_MEM_EQ(setupsA.data(), setupsB.data(), players * sizeof(GamePlayerSetup))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: SetPlayerType(%d, %u)", i, index, type);
            return;
        }
    }
}

// Wrong: a pair of teams of one human each is never "with option".
std::uint32_t __fastcall WrongTeamLayoutKind(GameSettings* self, void* edx) {
    const std::uint32_t kind = GameSettings_TeamLayoutKind(self, edx);
    return kind == team_layout::kPairsWithOption ? team_layout::kPairs : kind;
}

}  // namespace

S3RE_TEST(GameSettingsTeamLayout_MatchesOriginal) {
    CompareLayout(ctx, {&GameSettings_TeamLayoutKind, &GameSettings_SetPlayerType}, 2000);
}

S3RE_TEST(GameSettingsTeamLayout_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareLayout(probe, {&WrongTeamLayoutKind, &GameSettings_SetPlayerType}, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
