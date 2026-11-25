// "New game objectives" (NGO; DebugLog "*** NGO enabled *** TimeOut: %u
// min", "NGO Good: %u random: %u" in Volker\LogicInit.cpp).
//
// World byte +0x1195dcc is non-zero when they are enabled; the seven bytes
// +0x1195dcd..+0x1195dd3 are the chosen objectives: 0x4ffbc0 draws them
// with CGameData::Random as 1..34 (Random % 34 + 1), skipping ids already
// chosen and 21, 25, 27..31, 33, 34, 35.
//
// CGameData::ChooseNewGameObjectives (0x004ffbc0): () thiscall, called by CLogicThread::OnStart; EAX unused.
//   Nothing if the objectives are off. Else objective_timeout (+0x2d248) =
//   GameSettings::NgoTimeout 0x437470 of g_gameSettings, DebugLog "*** NGO
//   TimeOut: %u min (new) ***" with it, end_warning_minutes (+0x2d24c) =
//   10; then seven objectives: id = Random() % 34 + 1 (a byte), drawn again
//   while IsNgoObjectiveChosen(id) (called through its address; the upper
//   bytes of its argument are the caller's stack leftovers, it reads the
//   low byte only) or the id is 21, 25, 27..31, 33, 34 (35: never drawn);
//   finally g_victoryProtectActive (0x7deb54) = 1.
// CGameData::IsNgoObjectiveChosen (0x004ffb90): (id) -> AL = 1 if one of
//   the seven bytes equals the low byte of `id`, else 0; the upper bits of
//   EAX are the caller's (only AL is used). thiscall, ret 4.
// CGameData::DecideNgoOutcome (0x004ffc80): when the objectives' time is up
//   (CLogicThread::CheckGameEnd): for each of the seven objectives (goods
//   ids), sums the goods counter (player +0xdc, uint16 per good, indexed by
//   the objective byte without a bound) of the active players in the local
//   player's team and of all other active players (uint32 sums). The team
//   wins the objective if its sum is larger, loses it if smaller. EAX = 1
//   (won more objectives than lost), 0 (lost more), 2 (as many). thiscall,
//   no arguments.
// CGameData::PlayerOfColour (0x004ffe00): (colour) -> the first player
//   0..19 whose record is active and whose colour index (g_playerColourIndex,
//   uint16 per player) equals colour (32-bit compare), or 0 if there is
//   none. Quirk: a match only at player 19 also gives 0. Used by
//   CheckVictoryConditions outside campaigns, where the victory conditions
//   name players by colour. thiscall, ret 4.

#include "game/debug_log.h"
#include "game/globals.h"
#include "game/texts.h"
#include "game/ui/game_settings_layout.h"
#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <vector>

namespace s3re::game {

namespace {

// CGameData::objectives_enabled, then CGameData::objectives: the chosen objectives.
constexpr std::uint32_t kNgoObjectiveCount = 7;
static_assert(sizeof(CGameData::objectives) == kNgoObjectiveCount);

std::uint8_t& NgoEnabled(CGameData* world) {
    return world->objectives_enabled;
}

std::uint8_t* NgoObjectives(CGameData* world) {
    return world->objectives;
}

}  // namespace

using IsChosenFn = bool(__fastcall*)(CGameData* world, void* edx, std::uint32_t id);
using DecideNgoOutcomeFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx);
using PlayerOfColourFn = std::int32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t colour);

// Purpose: Whether an objective has already been chosen for this game's "new game objectives" (used while
// drawing the seven objectives at random, so none is picked twice).
bool __fastcall CGameData_IsNgoObjectiveChosen(CGameData* world, void*, std::uint32_t id) {
    const std::uint8_t* chosen = NgoObjectives(world);
    for (std::uint32_t i = 0; i < kNgoObjectiveCount; ++i) {
        if (chosen[i] == static_cast<std::uint8_t>(id)) {
            return true;
        }
    }
    return false;
}

// The outcomes of DecideNgoOutcome (also stored in g_gameOutcome).
constexpr std::uint32_t kNgoLost = 0;
constexpr std::uint32_t kNgoWon = 1;
constexpr std::uint32_t kNgoDraw = 2;

// Purpose: Decides a "new game objectives" game when its time is up: the local player's team wins an
// objective if its players together hold more of that good than everyone else, and the game is won if the
// team wins more objectives than it loses (drawn if as many).
std::uint32_t __fastcall CGameData_DecideNgoOutcome(CGameData* world, void*) {
    std::uint32_t won = 0;
    std::uint32_t lost = 0;
    for (std::uint32_t k = 0; k < kNgoObjectiveCount; ++k) {
        const std::uint8_t good = NgoObjectives(world)[k];
        std::uint32_t team = 0;
        std::uint32_t others = 0;
        for (std::uint32_t player = 0; player < layout::kPlayers; ++player) {
            const layout::Player& record = world->players[player];
            if (record.active == 0) {
                continue;
            }
            // Indexed by the objective byte without a bound check, as the original.
            const std::uint16_t amount = record.goods[good];
            if (world->players[world->local_player].team == record.team) {
                team += amount;
            } else {
                others += amount;
            }
        }
        if (others < team) {
            ++won;
        } else if (others > team) {
            ++lost;
        }
    }
    if (won > lost) {
        return kNgoWon;
    }
    return won < lost ? kNgoLost : kNgoDraw;
}

// Purpose: Finds the player who plays a given colour (maps use colours to name players in their victory
// conditions); 0 if no active player has it.
std::int32_t __fastcall CGameData_PlayerOfColour(CGameData* world, void*, std::uint32_t colour) {
    std::int32_t found = -1;
    std::uint32_t player = 0;
    while (player != layout::kPlayers) {
        if (world->players[player].active != 0 && g_playerColourIndex[player] == colour) {
            found = static_cast<std::int32_t>(player);
        }
        ++player;
        if (found != -1) {
            // A match at the last player is lost (the original tests the counter after the increment).
            return player == layout::kPlayers ? 0 : found;
        }
    }
    return 0;
}

namespace {

constexpr std::uint32_t kNgoObjectiveIds = 34;     // ids 1..34 are drawn
constexpr std::int32_t kEndWarningMinutes = 10;
// Drawn ids that are never used as objectives.
constexpr std::uint8_t kExcludedObjectives[] = {21, 25, 27, 28, 29, 30, 31, 33, 34, 35};

// CGameData::Random through its address.
std::uint32_t Random(CGameData* world) {
    return reinterpret_cast<std::uint16_t(__fastcall*)(CGameData*, void*)>(0x004feb70)(world, nullptr);
}

bool IsExcludedObjective(std::uint8_t id) {
    for (const std::uint8_t excluded : kExcludedObjectives) {
        if (id == excluded) {
            return true;
        }
    }
    return false;
}

}  // namespace

using ChooseObjectivesFn = void(__fastcall*)(CGameData* world, void* edx);
void __fastcall CGameData_ChooseNewGameObjectives(CGameData* world, void* edx);

// Purpose: At the start of a game with "new game objectives", sets their
// time limit and draws the seven goods the teams compete for.
void __fastcall CGameData_ChooseNewGameObjectives(CGameData* world, void*) {
    if (NgoEnabled(world) == 0) {
        return;
    }
    using GetterFn = std::uint32_t(__fastcall*)(const GameSettings*, void*);
    const std::uint32_t timeout = reinterpret_cast<GetterFn>(0x00437470)(&g_gameSettings.get(), nullptr);
    world->objective_timeout = static_cast<std::int32_t>(timeout);
    GameDebugLog(texts::kNgoTimeoutNew, timeout);
    world->end_warning_minutes = kEndWarningMinutes;
    for (std::uint32_t k = 0; k < kNgoObjectiveCount;) {
        const auto id = static_cast<std::uint8_t>(Random(world) % kNgoObjectiveIds + 1);
        if (reinterpret_cast<IsChosenFn>(0x004ffb90)(world, nullptr, id) || IsExcludedObjective(id)) {
            continue;
        }
        NgoObjectives(world)[k] = id;
        ++k;
    }
    g_victoryProtectActive = 1;
}

S3RE_HOOK(0x004ffbc0, CGameData_ChooseNewGameObjectives, ChooseObjectivesFn,
          Original_CGameData_ChooseNewGameObjectives, CGameData_ChooseNewGameObjectives);
S3RE_HOOK(0x004ffb90, CGameData_IsNgoObjectiveChosen, IsChosenFn, Original_CGameData_IsNgoObjectiveChosen,
          CGameData_IsNgoObjectiveChosen);
S3RE_HOOK(0x004ffc80, CGameData_DecideNgoOutcome, DecideNgoOutcomeFn, Original_CGameData_DecideNgoOutcome,
          CGameData_DecideNgoOutcome);
S3RE_HOOK(0x004ffe00, CGameData_PlayerOfColour, PlayerOfColourFn, Original_CGameData_PlayerOfColour,
          CGameData_PlayerOfColour);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// 0x100 bytes past the start of the chosen objectives.
constexpr std::size_t kTestWorld = offsetof(CGameData, objectives) + 0x100;

void CompareIsChosen(test::Context& ctx, IsChosenFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        for (std::uint32_t k = 0; k < kNgoObjectiveCount + 1; ++k) {  // one byte past the list too
            const auto value = static_cast<std::uint8_t>(ctx.rng().Below(40));
            // k == 7 is the byte after the list (the first byte of pile_claims).
            worlds.Both([&](CGameData* w) { NgoObjectives(w)[k] = value; });
        }
        const auto enabled = static_cast<std::uint8_t>(ctx.rng().Below(2));
        worlds.Both([&](CGameData* w) { NgoEnabled(w) = enabled; });
        const std::uint32_t id = (ctx.rng().Chance(1, 4) ? ctx.rng().Next32() & ~0xffu : 0) | ctx.rng().Below(40);
        const bool expected = Original_CGameData_IsNgoObjectiveChosen(worlds.original(), nullptr, id);
        const bool actual = candidate(worlds.ours(), nullptr, id);
        if (!S3RE_EXPECT_EQ(expected, actual) || (i % 1024 == 1023 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: id 0x%x", i, id);
            return;
        }
    }
}

// Wrong: looks at the first six objectives only.
bool __fastcall WrongIsNgoObjectiveChosen(CGameData* world, void*, std::uint32_t id) {
    const std::uint8_t* chosen = NgoObjectives(world);
    for (std::uint32_t i = 0; i < kNgoObjectiveCount - 1; ++i) {
        if (chosen[i] == static_cast<std::uint8_t>(id)) {
            return true;
        }
    }
    return false;
}

}  // namespace

namespace {

// ChooseNewGameObjectives on random worlds (objectives on or off, random
// RNG states and objective bytes), with a random NGO time in g_gameSettings
// (restored). Compared: the world up past the objectives, the DebugLog
// calls and g_victoryProtectActive.
void CompareChooseObjectives(test::Context& ctx, ChooseObjectivesFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    const std::uint32_t savedTimeout = g_gameSettings->ngo_timeout;
    const std::uint8_t savedFlag = g_victoryProtectActive;
    for (int i = 0; i < cases; ++i) {
        const auto enabled = static_cast<std::uint8_t>(ctx.rng().Chance(1, 8) ? 0 : 1 + ctx.rng().Below(255));
        worlds.Both([&](CGameData* w) { NgoEnabled(w) = enabled; });
        g_gameSettings->ngo_timeout = ctx.rng().Next32();
        const auto flag = static_cast<std::uint8_t>(ctx.rng().Below(2));
        std::vector<DebugLogCall> logA, logB;
        std::uint8_t flagA, flagB;
        {
            DebugLogRecorder recorder;
            g_victoryProtectActive = flag;
            Original_CGameData_ChooseNewGameObjectives(worlds.original(), nullptr);
            flagA = g_victoryProtectActive;
            logA = recorder.Take();
            g_victoryProtectActive = flag;
            candidate(worlds.ours(), nullptr);
            flagB = g_victoryProtectActive;
            logB = recorder.Take();
        }
        if (!S3RE_EXPECT_EQ(flagA, flagB) || !S3RE_EXPECT(logA == logB) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            break;
        }
    }
    g_gameSettings->ngo_timeout = savedTimeout;
    g_victoryProtectActive = savedFlag;
}

// Wrong: id 32 is excluded too.
void __fastcall WrongChooseNewGameObjectives(CGameData* world, void* edx) {
    CGameData_ChooseNewGameObjectives(world, edx);
    for (std::uint32_t k = 0; k < kNgoObjectiveCount; ++k) {
        if (NgoObjectives(world)[k] == 32) {
            NgoObjectives(world)[k] = 20;
        }
    }
}

}  // namespace

S3RE_TEST(CGameData_ChooseNewGameObjectives_MatchesOriginal) {
    CompareChooseObjectives(ctx, &CGameData_ChooseNewGameObjectives, 300);
}
S3RE_TEST(CGameData_ChooseNewGameObjectives_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareChooseObjectives(probe, &WrongChooseNewGameObjectives, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CGameData_IsNgoObjectiveChosen_MatchesOriginal) {
    CompareIsChosen(ctx, &CGameData_IsNgoObjectiveChosen, 20000);
}
S3RE_TEST(CGameData_IsNgoObjectiveChosen_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareIsChosen(probe, &WrongIsNgoObjectiveChosen, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

// The players, the local player's team and the objectives, from a few values
// so that teams and sums often tie.
void SetUpNgo(test::Context& ctx, test::WorldPair& worlds) {
    for (std::uint32_t player = 0; player < layout::kPlayers; ++player) {
        const std::int32_t active = ctx.rng().Chance(1, 3) ? 0 : static_cast<std::int32_t>(ctx.rng().Next32() | 1);
        const std::int32_t team = static_cast<std::int32_t>(ctx.rng().Below(3));
        worlds.Both([&](CGameData* w) {
            w->players[player].active = active;
            w->players[player].team = team;
        });
        // The goods counters and the bytes after them (objective bytes up to 255 read on).
        for (std::uint32_t word = 0; word < 0x148 / 2 - 0xdc / 2; ++word) {
            const auto amount = static_cast<std::uint16_t>(ctx.rng().Chance(1, 8) ? ctx.rng().Next32()
                                                                                  : ctx.rng().Below(4));
            worlds.Both([&](CGameData* w) { w->players[player].goods[word] = amount; });
        }
    }
    const std::int32_t local = static_cast<std::int32_t>(ctx.rng().Below(layout::kPlayers));
    worlds.Both([&](CGameData* w) { w->local_player = local; });
    for (std::uint32_t k = 0; k < kNgoObjectiveCount; ++k) {
        const auto good = static_cast<std::uint8_t>(ctx.rng().Chance(1, 10) ? ctx.rng().Below(256) : ctx.rng().Below(36));
        worlds.Both([&](CGameData* w) { NgoObjectives(w)[k] = good; });
    }
}

// Wrong: a tie on an objective counts as lost.
std::uint32_t __fastcall WrongDecideNgoOutcome(CGameData* world, void* edx) {
    const std::uint32_t result = CGameData_DecideNgoOutcome(world, edx);
    return result == kNgoDraw ? kNgoLost : result;
}

void CompareDecideNgoOutcome(test::Context& ctx, DecideNgoOutcomeFn candidate, int cases) {
    test::WorldPair worlds(ctx, offsetof(CGameData, site_cache));  // goods[255] of player 19 reads up to 0x1195f7e
    for (int i = 0; i < cases; ++i) {
        SetUpNgo(ctx, worlds);
        const std::uint32_t expected = Original_CGameData_DecideNgoOutcome(worlds.original(), nullptr);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr);
        if (!S3RE_EXPECT_EQ(expected, actual) || (i % 256 == 255 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(CGameData_DecideNgoOutcome_MatchesOriginal) {
    CompareDecideNgoOutcome(ctx, &CGameData_DecideNgoOutcome, 5000);
}
S3RE_TEST(CGameData_DecideNgoOutcome_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDecideNgoOutcome(probe, &WrongDecideNgoOutcome, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CGameData_PlayerOfColour_MatchesOriginal) {
    test::WorldPair worlds(ctx, offsetof(CGameData, per_player_1195d7c));
    std::uint16_t saved[layout::kPlayers];
    for (std::uint32_t player = 0; player < layout::kPlayers; ++player) {
        saved[player] = g_playerColourIndex[player];
    }
    for (int i = 0; i < 20000; ++i) {
        for (std::uint32_t player = 0; player < layout::kPlayers; ++player) {
            const std::int32_t active = ctx.rng().Chance(1, 3) ? 0 : static_cast<std::int32_t>(ctx.rng().Next32() | 1);
            worlds.Both([&](CGameData* w) { w->players[player].active = active; });
            g_playerColourIndex[player] = static_cast<std::uint16_t>(ctx.rng().Below(24));
        }
        const std::uint32_t colour = ctx.rng().Chance(1, 10) ? (ctx.rng().Next32() & ~0xffu) | ctx.rng().Below(24)
                                                             : ctx.rng().Below(24);
        const std::int32_t expected = Original_CGameData_PlayerOfColour(worlds.original(), nullptr, colour);
        const std::int32_t actual = CGameData_PlayerOfColour(worlds.ours(), nullptr, colour);
        if (!S3RE_EXPECT_EQ(expected, actual)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: colour 0x%x", i, colour);
            break;
        }
    }
    for (std::uint32_t player = 0; player < layout::kPlayers; ++player) {
        g_playerColourIndex[player] = saved[player];
    }
    worlds.Checkpoint(ctx);
}
}  // namespace s3re::game
