// The victory conditions of a map (world+0x15394b1, layout::VictoryConditions),
// checked by the game-end check CLogicThread::CheckGameEnd (0x511c50) for
// games outside campaigns (g_campaignMission 0) and a few campaign missions.
//
// CLogicThread::CheckVictoryConditions (0x005145c0): thiscall on the logic
//   thread, no arguments. EAX = 1 (won), 0 (lost) or -1 (not decided yet);
//   the caller stores it in g_gameOutcome. The result starts as won; in this
//   order:
//   1. destroy_players: CountMilitaryBuildings; a marked slot whose player
//      still has military buildings -> -1, go to 5.
//   2. destroy_buildings: for each entry (player byte 0xff = unused; the
//      player and building type bytes are signed): if the player is active,
//      the first building 1..count (count read once) of that type and owner
//      -> -1 and the search stops. Not won -> go to 5.
//   3. hold_places: an active place whose tile owner is not the local
//      player -> -1, go to 5.
//   4. protect_players: CountMilitaryBuildings; for each slot with a time:
//      the player has no military building -> 0 (lost), go to 5; the game
//      tick is below minutes * 60000 / 64 (signed product and division,
//      unsigned compare) -> -1, and the loop goes on.
//   5. collect_goods (only if still won): over the areas 1..count (count read
//      once) of the local player, per goods entry with an amount: walks the
//      area's pile list of that good (signed good byte) and adds the pile
//      amounts until the sum (kept over all areas) reaches the amount. A sum
//      below its amount -> -1.
//   Then: lost (0) after a condition was on -> return 0. Otherwise (won,
//   undecided, or no condition on at all, which counts as -1):
//   CountMilitaryBuildings; the local player has none -> 0. If every player
//   with military buildings is in the local player's team and no condition
//   was on -> 1; else the result.
//   Player numbers in the conditions are colour slots outside campaigns
//   (CGameData::PlayerOfColour 0x4ffe00), player numbers in campaigns.
//   Calls CountMilitaryBuildings 0x514a00 up to three times.

#include "game/globals.h"
#include "game/logic/logic_thread_layout.h"
#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

using layout::Area;
using layout::Building;
using layout::VictoryConditions;

constexpr std::int32_t kWon = 1;
constexpr std::int32_t kLost = 0;
constexpr std::int32_t kUndecided = -1;

constexpr std::uint32_t kPlayers = layout::kPlayers;
constexpr std::int8_t kNoBuildingEntry = -1;

// 60,000 ms per minute, 64 ms per game tick (the logic thread's timer).
constexpr std::uint32_t kMsPerMinute = 60000;
constexpr std::int32_t kMsPerTick = 64;

// CountMilitaryBuildings writes out[owner] for any owner byte, so the
// buffer has room for all 255 (the original's 20 entries would spill into
// its other locals; real games have owners < 20).
using MilitaryCounts = std::int32_t[256];

// A player named by the victory conditions: a colour outside campaigns.
std::int32_t ConditionPlayer(CGameData* world, std::int32_t slot) {
    if (g_campaignMission == 0) {
        return CGameData_PlayerOfColour(world, nullptr, static_cast<std::uint32_t>(slot));
    }
    return slot;
}

// Step 1: a marked player still has military buildings.
bool MarkedPlayerSurvives(CLogicThread* self, MilitaryCounts& military) {
    CLogicThread_CountMilitaryBuildings(self, nullptr, military);
    for (std::uint32_t slot = 0; slot < kPlayers; ++slot) {
        if (self->world->victory_conditions.player_to_destroy[slot] == 0) {
            continue;
        }
        if (military[ConditionPlayer(self->world, static_cast<std::int32_t>(slot))] != 0) {
            return true;
        }
    }
    return false;
}

// Step 2: one of the listed buildings still stands.
bool ListedBuildingStands(CLogicThread* self) {
    for (std::uint32_t entry = 0; entry < 10; ++entry) {
        CGameData* world = self->world;
        const std::int8_t listed = world->victory_conditions.buildings[entry].player;
        if (listed == kNoBuildingEntry) {
            continue;
        }
        const std::int32_t player = ConditionPlayer(world, listed);
        world = self->world;
        if (world->players[player].active == 0) {
            continue;
        }
        const std::uint32_t count = world->building_high_water;
        for (std::uint32_t building = 1; building <= count; ++building) {
            const Building& record = world->buildings[building];
            if (static_cast<std::int32_t>(record.art) == world->victory_conditions.buildings[entry].building_type &&
                static_cast<std::int32_t>(record.owner) == player) {
                return true;
            }
        }
    }
    return false;
}

// Step 3: a place is not the local player's.
bool PlaceNotHeld(CGameData* world) {
    for (const layout::VictoryPlace& place : world->victory_conditions.places) {
        if (place.active != 0 &&
            static_cast<std::int32_t>(TileAtOffset(world, TileOffset(place.x, place.y)).owner) != world->local_player) {
            return true;
        }
    }
    return false;
}

// Step 4: the protected players. Returns the new result.
std::int32_t CheckProtectedPlayers(CLogicThread* self, MilitaryCounts& military, std::int32_t result) {
    CLogicThread_CountMilitaryBuildings(self, nullptr, military);
    for (std::uint32_t slot = 0; slot < kPlayers; ++slot) {
        if (self->world->victory_conditions.protect_minutes[slot] == 0) {
            continue;
        }
        const std::int32_t player = ConditionPlayer(self->world, static_cast<std::int32_t>(slot));
        if (military[player] == 0) {
            return kLost;
        }
        const std::uint32_t minutes = self->world->victory_conditions.protect_minutes[slot];
        const auto ms = static_cast<std::int32_t>(minutes * kMsPerMinute);  // wraps like the original's imul
        if (g_gameTick < static_cast<std::uint32_t>(ms / kMsPerTick)) {
            result = kUndecided;
        }
    }
    return result;
}

// Step 5: whether the local player's areas hold the goods.
bool GoodsCollected(CGameData* world) {
    const VictoryConditions& goals = world->victory_conditions;
    std::uint32_t sums[3] = {};
    const std::uint32_t count = world->area_high_water;
    for (std::uint32_t area = 1; area <= count; ++area) {
        const Area& record = world->areas[area];
        if (static_cast<std::int32_t>(record.player) != world->local_player) {
            continue;
        }
        for (std::uint32_t k = 0; k < 3; ++k) {
            const std::uint32_t needed = goals.goods[k].amount;
            if (needed == 0) {
                continue;
            }
            // Signed good byte, no bound check (as the original).
            std::uint32_t pile = record.piles_by_goods[goals.goods[k].good];
            while (pile != 0) {
                sums[k] += world->piles[pile].amount;
                pile = world->piles[pile].next;
                if (sums[k] >= needed) {
                    break;
                }
            }
        }
    }
    for (std::uint32_t k = 0; k < 3; ++k) {
        const std::uint32_t needed = goals.goods[k].amount;
        if (needed != 0 && sums[k] < needed) {
            return false;
        }
    }
    return true;
}

// The end: the local player must still have military buildings; with no
// condition on, the game is won when only the local team has some left.
std::int32_t DecideByTeams(CLogicThread* self, MilitaryCounts& military, std::int32_t result, bool anyCondition) {
    CLogicThread_CountMilitaryBuildings(self, nullptr, military);
    CGameData* world = self->world;
    const std::int32_t local = world->local_player;
    if (military[local] == 0) {
        return kLost;
    }
    constexpr std::uint32_t kOwnTeam = 1;
    constexpr std::uint32_t kOtherTeam = 2;
    std::uint32_t teams = 0;
    for (std::uint32_t player = 0; player < kPlayers; ++player) {
        if (military[player] == 0) {
            continue;
        }
        teams |= world->players[player].team == world->players[local].team ? kOwnTeam : kOtherTeam;
    }
    if (teams == kOwnTeam && !anyCondition) {
        return kWon;
    }
    return result;
}

}  // namespace

using CheckVictoryConditionsFn = std::int32_t(__fastcall*)(CLogicThread* self, void* edx);

// Purpose: Decides a game on a map with victory conditions (destroy players or buildings, hold places,
// protect players for a time, collect goods): won, lost or not yet; without conditions the game is won when
// only the local player's team has military buildings left, and lost when the local player has none.
std::int32_t __fastcall CLogicThread_CheckVictoryConditions(CLogicThread* self, void*) {
    MilitaryCounts military;
    std::int32_t result = kWon;
    bool anyCondition = false;
    bool goToGoods = false;  // a check decided "not won": skip the later ones
    if (self->world->victory_conditions.destroy_players != 0) {
        anyCondition = true;
        if (MarkedPlayerSurvives(self, military)) {
            result = kUndecided;
            goToGoods = true;
        }
    }
    if (!goToGoods && self->world->victory_conditions.destroy_buildings != 0) {
        anyCondition = true;
        if (ListedBuildingStands(self)) {
            result = kUndecided;
            goToGoods = true;
        }
    }
    if (!goToGoods && self->world->victory_conditions.hold_places != 0) {
        anyCondition = true;
        if (PlaceNotHeld(self->world)) {
            result = kUndecided;
            goToGoods = true;
        }
    }
    if (!goToGoods && self->world->victory_conditions.protect_players != 0) {
        anyCondition = true;
        result = CheckProtectedPlayers(self, military, result);
    }
    if (result == kWon && self->world->victory_conditions.collect_goods != 0) {
        anyCondition = true;
        if (!GoodsCollected(self->world)) {
            result = kUndecided;
        }
    } else if (!anyCondition) {
        result = kUndecided;
    }
    if (result != kUndecided && result != kWon) {
        return result;
    }
    return DecideByTeams(self, military, result, anyCondition);
}

S3RE_HOOK(0x005145c0, CLogicThread_CheckVictoryConditions, CheckVictoryConditionsFn,
          Original_CLogicThread_CheckVictoryConditions, CLogicThread_CheckVictoryConditions);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestBuildings = 40;
constexpr std::uint32_t kTestAreas = 12;
constexpr std::size_t kTestWorld = sizeof(CGameData);

struct SavedGlobals {
    SavedGlobals() : mission(g_campaignMission), tick(g_gameTick) {
        for (std::uint32_t p = 0; p < kPlayers; ++p) {
            colours[p] = g_playerColourIndex[p];
        }
    }
    ~SavedGlobals() {
        g_campaignMission = mission;
        g_gameTick = tick;
        for (std::uint32_t p = 0; p < kPlayers; ++p) {
            g_playerColourIndex[p] = colours[p];
        }
    }
    std::int32_t mission;
    std::uint32_t tick;
    std::uint16_t colours[kPlayers];
};

std::uint8_t SmallPlayer(test::Context& ctx) {
    return static_cast<std::uint8_t>(ctx.rng().Chance(1, 10) ? 0xff : ctx.rng().Below(5));
}

// Players, buildings, areas with piles, tiles of the places, and conditions,
// drawn from small value sets so that every branch is reached.
void SetUpVictoryCase(test::Context& ctx, test::WorldPair& worlds) {
    g_campaignMission = ctx.rng().Chance(1, 2) ? 0 : 1 + static_cast<std::int32_t>(ctx.rng().Below(72));
    g_gameTick = ctx.rng().Chance(1, 2) ? ctx.rng().Below(3 * 937) : ctx.rng().Next32();
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        g_playerColourIndex[p] = static_cast<std::uint16_t>(ctx.rng().Below(8));
        const std::int32_t active = ctx.rng().Chance(1, 4) ? 0 : 1;
        const std::int32_t team = static_cast<std::int32_t>(ctx.rng().Below(3));
        worlds.Both([&](CGameData* w) {
            w->players[p].active = active;
            w->players[p].team = team;
        });
    }
    const std::int32_t local = static_cast<std::int32_t>(ctx.rng().Below(4));
    worlds.Both([&](CGameData* w) { w->local_player = local; });
    const std::uint32_t buildings = ctx.rng().Below(kTestBuildings);
    worlds.Both([&](CGameData* w) { w->building_high_water = buildings; });
    for (std::uint32_t b = 1; b <= kTestBuildings; ++b) {
        const std::uint8_t owner = SmallPlayer(ctx);
        const auto art = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 15 + ctx.rng().Below(4) : ctx.rng().Below(8));
        const auto buildup = static_cast<std::uint8_t>(ctx.rng().Chance(4, 5) ? 0 : 1);
        worlds.Both([&](CGameData* w) {
            w->buildings[b].owner = owner;
            w->buildings[b].art = art;
            w->buildings[b].buildup = buildup;
        });
    }
    const std::uint32_t areas = ctx.rng().Below(kTestAreas);
    worlds.Both([&](CGameData* w) { w->area_high_water = areas; });
    std::uint32_t nextPile = 1;
    for (std::uint32_t a = 1; a <= kTestAreas; ++a) {
        const std::uint8_t owner = SmallPlayer(ctx);
        worlds.Both([&](CGameData* w) { w->areas[a].player = owner; });
        for (std::uint32_t good = 0; good < 4; ++good) {
            std::uint32_t first = 0;
            const std::uint32_t length = ctx.rng().Below(4);
            for (std::uint32_t k = 0; k < length; ++k, ++nextPile) {
                const auto amount = static_cast<std::uint8_t>(ctx.rng().Below(8));
                worlds.Both([&](CGameData* w) {
                    w->piles[nextPile].amount = amount;
                    w->piles[nextPile].next = static_cast<std::uint16_t>(first);
                });
                first = nextPile;
            }
            worlds.Both([&](CGameData* w) { w->areas[a].piles_by_goods[good] = static_cast<std::uint16_t>(first); });
        }
    }
    VictoryConditions goals = {};
    goals.destroy_players = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3));
    for (auto& marked : goals.player_to_destroy) {
        marked = static_cast<std::uint8_t>(ctx.rng().Chance(1, 6));
    }
    goals.destroy_buildings = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3));
    for (auto& entry : goals.buildings) {
        entry.player = static_cast<std::int8_t>(ctx.rng().Chance(1, 3) ? -1 : ctx.rng().Range(-2, 5));
        entry.building_type = static_cast<std::int8_t>(ctx.rng().Chance(1, 10) ? -1 : ctx.rng().Below(8));
    }
    goals.hold_places = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3));
    for (auto& place : goals.places) {
        place.active = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2));
        place.x = static_cast<std::uint16_t>(ctx.rng().Below(400));
        place.y = static_cast<std::uint16_t>(ctx.rng().Below(400));
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Chance(2, 3) ? local : ctx.rng().Below(4));
        worlds.Both([&](CGameData* w) { w->tiles[place.x][place.y].owner = owner; });
    }
    goals.protect_players = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3));
    for (auto& minutes : goals.protect_minutes) {
        minutes = static_cast<std::uint16_t>(ctx.rng().Chance(4, 5) ? 0
                                             : ctx.rng().Chance(1, 8) ? ctx.rng().Below(0x10000)
                                                                      : ctx.rng().Below(4));
    }
    goals.collect_goods = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3));
    for (auto& goods : goals.goods) {
        goods.amount = static_cast<std::uint16_t>(ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Below(20));
        goods.good = static_cast<std::int8_t>(ctx.rng().Below(4));
    }
    worlds.Both([&](CGameData* w) { w->victory_conditions = goals; });
}

struct Threads {
    explicit Threads(test::WorldPair& worlds) {
        original.world = worlds.original();
        ours.world = worlds.ours();
    }
    CLogicThread original = {};
    CLogicThread ours = {};
};

// Wrong: ignores the hold_places condition.
std::int32_t __fastcall WrongCheckVictoryConditions(CLogicThread* self, void* edx) {
    VictoryConditions& goals = self->world->victory_conditions;
    const std::uint8_t saved = goals.hold_places;
    goals.hold_places = 0;
    const std::int32_t result = CLogicThread_CheckVictoryConditions(self, edx);
    goals.hold_places = saved;
    return result;
}

void CompareCheckVictoryConditions(test::Context& ctx, CheckVictoryConditionsFn candidate, int cases) {
    SavedGlobals saved;
    test::WorldPair worlds(ctx, kTestWorld);
    Threads threads(worlds);
    for (int i = 0; i < cases; ++i) {
        SetUpVictoryCase(ctx, worlds);
        const std::int32_t expected = Original_CLogicThread_CheckVictoryConditions(&threads.original, nullptr);
        const std::int32_t actual = candidate(&threads.ours, nullptr);
        if (!S3RE_EXPECT_EQ(expected, actual)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: mission %d", i, static_cast<std::int32_t>(g_campaignMission));
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

}  // namespace

S3RE_TEST(CLogicThread_CheckVictoryConditions_MatchesOriginal) {
    CompareCheckVictoryConditions(ctx, &CLogicThread_CheckVictoryConditions, 20000);
}
S3RE_TEST(CLogicThread_CheckVictoryConditions_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareCheckVictoryConditions(probe, &WrongCheckVictoryConditions, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
