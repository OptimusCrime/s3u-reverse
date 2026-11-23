// The game-end check: CLogicThread::CheckGameEnd (0x00511c50), called by
// CLogicThread::Step (0x53db30) every step unless g_gameOutcome is already
// decided or g_worldUpdateDisabled is set.
//
// Every 50 game ticks (tick - this+0x24c >= 50, unsigned) and only while
// g_gameOutcome is -1, it stores the tick in this+0x24c and decides the game:
// - New game objectives on (world+0x1195dcc) and the game tick >= the
//   objectives' time (minutes at world+0x2d248, * 60000 >> 6 unsigned):
//   sets world+0x2d250 (counters_disabled) and g_gameOutcome =
//   CGameData::DecideNgoOutcome (0x4ffc80; 1 won, 0 lost, 2 drawn).
// - Otherwise per campaign mission (g_campaignMission, a jump table of 100
//   entries at 0x514424): no campaign (0) and several missions use the map's
//   victory conditions (CheckVictoryConditions 0x5145c0); the others have
//   hard-coded goals (defeat players, hold places, build temples, collect
//   goods, keep soldiers near a place, ...), written per mission below.
//   Missions 73..98 and above 99 decide nothing.
// The result is g_gameOutcome: 1 won, 0 lost, -1 not decided (Step then
// sends command 0xfd "won" or 0xfc "lost" for the local player); AL returns
// its low byte (0xff when the check didn't run; Step uses only AL).
// Some missions also act: missions 34 and 58 open a trade route for an
// allied marketplace, mission 51 creates a mage and two effect records and
// gives player 1 mana, missions 67 and 68 keep state in the player records,
// missions 68 and 69 set g_worldUpdateDisabled when won.
//
// The original keeps all point tables on its stack (filled at entry); here
// they are constant tables. The counts of CountMilitaryBuildings and
// SumGoldPerPlayer are written for every owner byte (the original has room
// for 20, real games have owners < 20).

#include "game/enums/settler_types.h"
#include "game/enums/buildings.h"
#include "game/enums/goods.h"
#include "game/globals.h"
#include "game/logic/logic_thread_layout.h"
#include "game/replacements.h"
#include "game/tables.h"
#include "game/typed_fields.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace s3re::game {

namespace {

using layout::Building;
using layout::Settler;
using layout::Tile;

constexpr std::int32_t kWon = 1;
constexpr std::int32_t kLost = 0;
constexpr std::int32_t kUndecided = -1;
constexpr std::uint8_t kNotChecked = 0xff;  // AL when the check didn't run

constexpr std::uint32_t kCheckInterval = 50;  // game ticks
constexpr std::uint32_t kMsPerMinute = 60000;
constexpr std::uint32_t kTickShift = 6;  // 64 ms per tick

// Missions without hard-coded goals decide nothing (jump table entries 73..98, and above 99).
constexpr std::uint32_t kLastMission = 99;

using Counts = std::int32_t[256];

struct Point {
    std::int32_t x;
    std::int32_t y;
};
static_assert(sizeof(Point) == 8);

const std::int32_t* Coordinates(const Point* points) {
    return &points[0].x;
}

// One mission check: the logic thread and the counts of CountMilitaryBuildings.
struct Check {
    CLogicThread* self;
    Counts military;

    CGameData* World() const { return self->world; }
    std::int32_t Local() const { return self->world->local_player; }
    void CountMilitary() { CLogicThread_CountMilitaryBuildings(self, nullptr, military); }
    std::int32_t LocalMilitary() const { return military[self->world->local_player]; }
    std::uint32_t TilesOwnedBy(std::uint32_t player, const Point* points, std::int32_t count) {
        return CLogicThread_CountTilesOwnedBy(self, nullptr, player, Coordinates(points), count);
    }
    bool BuildingNear(std::uint32_t x, std::uint32_t y, std::uint32_t radius, BuildingType type,
                      std::int32_t player) {
        return CLogicThread_IsBuildingNear(self, nullptr, x, y, radius, Value(type), player);
    }
    bool LocalBuildingNear(std::uint32_t x, std::uint32_t y, std::uint32_t radius, BuildingType type) {
        return BuildingNear(x, y, radius, type, self->world->local_player);
    }
};

void Win() {
    g_gameOutcome = kWon;
}
void Lose() {
    g_gameOutcome = kLost;
}
// The common ending of most missions: lost when the player has no military building left.
void LoseWithoutMilitary(std::int32_t count) {
    if (count == 0) {
        Lose();
    }
}

bool IsSoldier(std::uint8_t type) {
    switch (static_cast<SettlerType>(type)) {
    case SettlerType::Swordsman1:
    case SettlerType::Swordsman2:
    case SettlerType::Swordsman3:
    case SettlerType::Bowman1:
    case SettlerType::Bowman2:
    case SettlerType::Bowman3:
    case SettlerType::Pikeman1:
    case SettlerType::Pikeman2:
    case SettlerType::Pikeman3:
        return true;
    default:
        return false;
    }
}

bool IsLevel3Soldier(std::uint8_t type) {
    const auto t = static_cast<SettlerType>(type);
    return t == SettlerType::Swordsman3 || t == SettlerType::Bowman3 || t == SettlerType::Pikeman3;
}

bool IsTower(std::uint8_t art) {
    const auto t = static_cast<BuildingType>(art);
    return t == BuildingType::SmallTower || t == BuildingType::LargeTower;
}

// Settlers 1..count (count read once; none if 0) of `owner` passing `select`.
template <typename Select>
std::uint32_t CountSettlers(CGameData* world, std::uint32_t owner, Select select) {
    std::uint32_t result = 0;
    const std::uint32_t count = world->settler_high_water;
    for (std::uint32_t settler = 1; settler <= count && settler != 0; ++settler) {
        const Settler& record = world->settlers[settler];
        if (record.owner == owner && select(record.art)) {
            ++result;
        }
    }
    return result;
}

// Soldiers of `owner` standing within `radius` (offset table, signed
// distances) of (cx, cy); stops once more than `limit` are counted.
template <typename Select>
std::uint32_t CountSoldiersAround(CGameData* world, std::uint32_t cx, std::uint32_t cy, std::int32_t radius,
                                  std::uint32_t limit, std::uint32_t owner, Select select) {
    std::uint32_t count = 0;
    if (world->offsets[0].distance > radius) {
        return 0;
    }
    for (std::uint32_t k = 0; count <= limit; ++k) {
        const std::uint32_t x = static_cast<std::uint32_t>(world->offsets[k].dx) + cx;
        const std::uint32_t y = static_cast<std::uint32_t>(world->offsets[k].dy) + cy;
        if (x < world->map_size && y < world->map_size) {
            const std::uint32_t settler = TileAtOffset(world, TileOffset(x, y)).settler;
            if (settler != 0 && world->settlers[settler].owner == owner && select(world->settlers[settler].art)) {
                ++count;
            }
        }
        if (world->offsets[k + 1].distance > radius) {
            break;
        }
    }
    return count;
}

// The local player's areas: the summed amounts of the piles of one good.
std::uint32_t SumLocalAreaGoods(CGameData* world, Good good) {
    std::uint32_t sum = 0;
    const std::uint32_t count = world->area_high_water;
    for (std::uint32_t area = 1; area <= count; ++area) {
        if (static_cast<std::int32_t>(world->areas[area].player) != world->local_player) {
            continue;
        }
        for (std::uint32_t pile = world->areas[area].piles_by_goods[Value(good)]; pile != 0;
             pile = world->piles[pile].next) {
            sum += world->piles[pile].amount;
        }
    }
    return sum;
}

// Missions 34 and 58: an allied player's marketplace at a place starts
// trading with the local player: route ends at (endX, endY), 100 of `good`
// ordered. The original re-reads the building's trade number before every
// store.
void OpenAlliedMarketplace(CGameData* world, std::uint32_t x, std::uint32_t y, std::uint16_t endX,
                           std::uint16_t endY, Good good) {
    const Tile& tile = world->tiles[x][y];
    if ((tile.flags & layout::kTileHasBuilding) == 0) {
        return;
    }
    const std::uint32_t building = tile.object_number;
    if (TypeOf(world->buildings[building]) != BuildingType::Marketplace) {
        return;
    }
    const std::uint32_t owner = tile.owner;
    const std::int32_t local = world->local_player;
    if (world->players[owner].team != world->players[local].team || owner == static_cast<std::uint32_t>(local)) {
        return;
    }
    constexpr std::uint8_t kTradeToLocalPlayer = 2;
    constexpr std::uint16_t kUnset = 0xffff;
    constexpr std::uint8_t kOrdered = 100;
    auto trade = [&]() -> layout::Trade& { return world->trades[world->buildings[building].trade]; };
    trade().mode = kTradeToLocalPlayer;
    trade().route_x[0] = kUnset;
    trade().route_y[0] = kUnset;
    trade().route_x[1] = kUnset;
    trade().route_y[1] = kUnset;
    trade().route_x[2] = kUnset;
    trade().route_y[2] = kUnset;
    trade().route_x[3] = endX;
    trade().route_y[3] = endY;
    trade().ordered[Value(good)] = kOrdered;
}

// Missions 42 and 45: towers at the places, all still the player 0's.
// Lost when a place has no finished tower; undecided while another player
// holds one.
void HoldTowers(Check& m, const Point* points, std::uint32_t count) {
    CGameData* world = m.World();
    for (std::uint32_t k = 0; k < count; ++k) {
        const Tile& tile = world->tiles[points[k].x][points[k].y];
        if (tile.object_number == 0 || (tile.flags & layout::kTileHasBuilding) == 0) {
            Lose();
            return;
        }
        const Building& building = world->buildings[tile.object_number];
        if (building.buildup != 0 || !IsTower(building.art)) {
            Lose();
            return;
        }
        if (building.owner != 0) {
            return;
        }
    }
    Win();
}

// --- the places of the missions (x, y), from the original's stack tables
constexpr Point kMission2Places[] = {{115, 175}, {65, 150},  {95, 120},  {65, 100},  {130, 80}, {40, 60},
                                     {70, 35},   {190, 95},  {210, 40},  {260, 40},  {300, 40}, {330, 90},
                                     {270, 90},  {210, 90},  {80, 210},  {80, 260},  {115, 310}, {60, 295}};
constexpr Point kMission3Places[] = {{260, 290}, {320, 290}, {380, 290}, {440, 290},
                                     {280, 340}, {340, 340}, {400, 340}, {460, 340}};
constexpr Point kMission7Places[] = {{160, 250}, {220, 250}, {280, 250}, {320, 250},
                                     {180, 210}, {240, 210}, {300, 210}};
constexpr Point kMission12Places[] = {{230, 160}, {290, 160}, {220, 210}, {280, 210}, {380, 250}};
constexpr Point kMission13Places[] = {{50, 50},  {110, 50},  {60, 110}, {120, 110},
                                      {60, 170}, {120, 170}, {190, 190}};
constexpr Point kMission15Goals[] = {{310, 220}, {300, 180}, {360, 180}, {360, 140},
                                     {350, 100}, {400, 100}, {270, 20},  {380, 20}};
constexpr Point kMission15Threatened[] = {{310, 220}, {300, 180}, {360, 180}, {360, 140}, {350, 100}, {400, 100}};
constexpr Point kMission16Places[] = {{230, 100}};
constexpr Point kMission23Places[] = {{250, 70}, {280, 100}, {430, 320}};
constexpr Point kMission42Towers[] = {{196, 67}, {202, 53}, {252, 54}, {278, 70}};
constexpr Point kMission45Towers[] = {{480, 474}, {501, 494}, {525, 480}, {509, 466}, {129, 90}, {138, 95}};
constexpr Point kMission62Places[] = {{198, 138}, {283, 193}, {345, 209}, {371, 230}, {417, 218}, {276, 149}};

struct TowerGoal {
    std::uint32_t x;
    std::uint32_t y;
    BuildingType type;
};
constexpr TowerGoal kMission69Towers[] = {
    {267, 536, BuildingType::Castle},     {274, 522, BuildingType::LargeTower}, {293, 508, BuildingType::LargeTower},
    {310, 510, BuildingType::LargeTower}, {324, 511, BuildingType::LargeTower}, {344, 519, BuildingType::SmallTower},
    {359, 527, BuildingType::LargeTower}, {376, 533, BuildingType::LargeTower}, {404, 554, BuildingType::LargeTower},
    {373, 548, BuildingType::LargeTower}, {365, 588, BuildingType::LargeTower}, {347, 595, BuildingType::LargeTower},
    {330, 599, BuildingType::LargeTower}, {304, 592, BuildingType::LargeTower}, {296, 576, BuildingType::LargeTower},
    {331, 571, BuildingType::SmallTower}, {433, 538, BuildingType::LargeTower},
};

// --- missions with the shared pattern "defeat players, keep your own"

// 1, 8, 9, 11, 18..21, 99: defeat player 1.
void DefeatPlayer1(Check& m) {
    m.CountMilitary();
    if (m.military[1] == 0) {
        Win();
    }
    LoseWithoutMilitary(m.military[0]);
}

// 4, 17, 24: defeat players 1 and 2.
void DefeatPlayers1And2(Check& m) {
    m.CountMilitary();
    if (m.military[1] == 0 && m.military[2] == 0) {
        Win();
    }
    LoseWithoutMilitary(m.military[0]);
}

// 14: defeat player 2.
void DefeatPlayer2(Check& m) {
    m.CountMilitary();
    if (m.military[2] == 0) {
        Win();
    }
    LoseWithoutMilitary(m.military[0]);
}

// Lost if player 0 owns none of the places, or has no military.
void KeepAPlace(Check& m, const Point* places, std::int32_t count) {
    if (m.TilesOwnedBy(0, places, count) == 0) {
        Lose();
    } else {
        LoseWithoutMilitary(m.military[0]);
    }
}

// 2: defeat players 1 and 2, keep one of 18 places.
void Mission2(Check& m) {
    m.CountMilitary();
    if (m.military[1] == 0 && m.military[2] == 0) {
        Win();
    }
    KeepAPlace(m, kMission2Places, 18);
}

// 3: defeat players 2 and 3, keep one of 8 places.
void Mission3(Check& m) {
    m.CountMilitary();
    if (m.military[2] == 0 && m.military[3] == 0) {
        Win();
    }
    KeepAPlace(m, kMission3Places, 8);
}

// 5: defeat players 2..4; lost if one of them collects 70 gold.
void Mission5(Check& m) {
    m.CountMilitary();
    Counts gold;
    CLogicThread_SumGoldPerPlayer(m.self, nullptr, gold);
    if (m.military[2] == 0 && m.military[3] == 0 && m.military[4] == 0) {
        Win();
    }
    constexpr std::int32_t kRivalGold = 70;
    if (gold[2] >= kRivalGold || gold[3] >= kRivalGold || gold[4] >= kRivalGold) {
        Lose();
        return;
    }
    LoseWithoutMilitary(m.military[0]);
}

// 6: defeat player 2; lost if it collects 100 gold.
void Mission6(Check& m) {
    m.CountMilitary();
    Counts gold;
    CLogicThread_SumGoldPerPlayer(m.self, nullptr, gold);
    if (m.military[2] == 0) {
        Win();
    }
    constexpr std::int32_t kRivalGold = 100;
    if (gold[2] >= kRivalGold) {
        Lose();
        return;
    }
    LoseWithoutMilitary(m.military[0]);
}

// 7: take all 7 places from player 2.
void Mission7(Check& m) {
    m.CountMilitary();
    if (m.TilesOwnedBy(2, kMission7Places, 7) == 0) {
        Win();
    }
    LoseWithoutMilitary(m.military[0]);
}

// 10: defeat player 2; lost if player 0 or its ally player 1 is defeated.
void Mission10(Check& m) {
    m.CountMilitary();
    if (m.military[2] == 0) {
        Win();
    }
    if (m.military[0] == 0) {
        Lose();
        return;
    }
    LoseWithoutMilitary(m.military[1]);
}

// 12: defeat players 1 and 2, keep one of 5 places.
void Mission12(Check& m) {
    m.CountMilitary();
    if (m.military[1] == 0 && m.military[2] == 0) {
        Win();
    }
    KeepAPlace(m, kMission12Places, 5);
}

// 13: defeat player 2, keep one of 7 places.
void Mission13(Check& m) {
    m.CountMilitary();
    if (m.military[2] == 0) {
        Win();
    }
    KeepAPlace(m, kMission13Places, 7);
}

// 15: own all 8 places; lost if player 3 takes the first six.
void Mission15(Check& m) {
    m.CountMilitary();
    if (m.TilesOwnedBy(0, kMission15Goals, 8) == 8) {
        Win();
    }
    if (m.TilesOwnedBy(3, kMission15Threatened, 6) == 6) {
        Lose();
        return;
    }
    LoseWithoutMilitary(m.military[0]);
}

// 16: defeat players 1 and 2, keep the place.
void Mission16(Check& m) {
    m.CountMilitary();
    if (m.military[1] == 0 && m.military[2] == 0) {
        Win();
    }
    KeepAPlace(m, kMission16Places, 1);
}

// 22: defeat player 1; lost when player 0 has fewer than 4 military buildings.
void Mission22(Check& m) {
    m.CountMilitary();
    if (m.military[1] == 0) {
        Win();
    }
    constexpr std::int32_t kMinimumBuildings = 4;
    if (m.military[0] < kMinimumBuildings) {
        Lose();
    }
}

// 23: take the 3 places from players 1 and 2.
void Mission23(Check& m) {
    m.CountMilitary();
    if (CLogicThread_CountTilesOwnedByEither(m.self, nullptr, 1, 2, Coordinates(kMission23Places), 3) == 0) {
        Win();
    }
    LoseWithoutMilitary(m.military[0]);
}

// 25: with the place (122, 112) owned by player 0, defeat players 1 and 2.
void Mission25(Check& m) {
    m.CountMilitary();
    if (m.military[0] == 0) {
        Lose();
        return;
    }
    if (m.World()->tiles[122][112].owner != 0 || m.military[1] != 0) {
        return;
    }
    if (m.military[2] == 0) {
        Win();
    }
}

// 27: player 1 must keep military; won when player 1 has 100 soldiers.
void Mission27(Check& m) {
    m.CountMilitary();
    if (m.military[0] == 0 || m.military[1] == 0) {
        Lose();
        return;
    }
    CGameData* world = m.World();
    // The original skips the loop for a count of 0 or 0xffffffff (it tests count + 1 > 1).
    if (world->settler_high_water + 1 <= 1) {
        return;
    }
    constexpr std::uint32_t kSoldiersNeeded = 100;
    if (CountSettlers(world, 1, IsSoldier) >= kSoldiersNeeded) {
        Win();
    }
}

// 30: player 0 gathers 200 mana and builds a small temple near (332, 74).
void Mission30(Check& m) {
    m.CountMilitary();
    if (m.military[0] == 0) {
        Lose();
        return;
    }
    constexpr std::uint32_t kMana = 200;
    if (static_cast<std::uint32_t>(m.World()->players[0].mana) >= kMana &&
        m.LocalBuildingNear(332, 74, 12, BuildingType::SmallTemple)) {
        Win();
    }
}

// 32: the victory conditions, but a loss counts as undecided.
void Mission32(Check& m) {
    g_gameOutcome = CLogicThread_CheckVictoryConditions(m.self, nullptr);
    if (g_gameOutcome == kLost) {
        g_gameOutcome = kUndecided;
    }
}

// 33: four places owned by players 1 or 2: won when one of them is defeated, lost if both survive.
void Mission33(Check& m) {
    m.CountMilitary();
    CGameData* world = m.World();
    auto ally = [](std::uint8_t owner) { return owner == 1 || owner == 2; };
    const std::int32_t player1 = m.military[1];
    const std::int32_t player2 = m.military[2];
    if (ally(world->tiles[295][194].owner) && ally(world->tiles[314][208].owner) &&
        ally(world->tiles[300][222].owner) && ally(world->tiles[319][249].owner) && player1 != 0 &&
        player2 != 0) {
        Lose();
    }
    if (player1 == 0 || player2 == 0) {
        Win();
    }
    LoseWithoutMilitary(m.LocalMilitary());
}

// 34: build a pyramid, a sphinx and a large residence and defeat player 2;
// an allied marketplace at (412, 504) starts trading stone to the local player.
void Mission34(Check& m) {
    CGameData* world = m.World();
    OpenAlliedMarketplace(world, 412, 504, 121, 557, Good::Stone);
    world = m.World();
    bool pyramid = false;
    bool sphinx = false;
    bool residence = false;
    const std::uint32_t count = world->building_high_water;
    for (std::uint32_t b = 1; b <= count; ++b) {
        const Building& building = world->buildings[b];
        if (building.buildup != 0 || static_cast<std::int32_t>(building.owner) != world->local_player) {
            continue;
        }
        switch (TypeOf(building)) {
        case BuildingType::Pyramid:
            pyramid = true;
            break;
        case BuildingType::Sphinx:
            sphinx = true;
            break;
        case BuildingType::LargeResidence:
            residence = true;
            break;
        default:
            break;
        }
    }
    m.CountMilitary();
    if (pyramid && sphinx && residence && m.military[2] == 0) {
        Win();
    }
    if (m.LocalMilitary() == 0) {
        Lose();
        return;
    }
    LoseWithoutMilitary(m.military[1]);
}

// 35: bring 10 soldiers of player 3 near (178, 434) and defeat player 2;
// lost when player 3 has fewer than 10 soldiers.
void Mission35(Check& m) {
    CGameData* world = m.World();
    const std::uint32_t soldiers = CountSettlers(world, 3, IsSoldier);
    const std::uint32_t soldiersNear = CountSoldiersAround(world, 178, 434, 25, 10, 3, IsSoldier);
    m.CountMilitary();
    constexpr std::uint32_t kSoldiers = 10;
    if (soldiersNear >= kSoldiers && m.military[2] == 0) {
        Win();
    }
    if (m.LocalMilitary() == 0 || soldiers < kSoldiers) {
        Lose();
    }
}

// 36: destroy player 1's rice farms and distilleries and build a castle
// near (357, 292) before player 1 gathers 200 mana.
void Mission36(Check& m) {
    CGameData* world = m.World();
    std::uint32_t riceFarms = 0;
    std::uint32_t distilleries = 0;
    const std::uint32_t count = world->building_high_water;
    for (std::uint32_t b = 1; b <= count; ++b) {
        const Building& building = world->buildings[b];
        if (TypeOf(building) == BuildingType::RiceFarm && building.owner == 1) {
            ++riceFarms;
        }
        if (TypeOf(building) == BuildingType::Distillery && building.owner == 1) {
            ++distilleries;
        }
    }
    constexpr std::uint32_t kRivalMana = 200;
    if (riceFarms == 0 && distilleries == 0 && static_cast<std::uint32_t>(world->players[1].mana) < kRivalMana &&
        m.LocalBuildingNear(357, 292, 25, BuildingType::Castle)) {
        Win();
    }
    if (static_cast<std::uint32_t>(m.World()->players[1].mana) >= kRivalMana) {
        Lose();
    }
    m.CountMilitary();
    LoseWithoutMilitary(m.LocalMilitary());
}

// 37: 10 level-3 soldiers near (476, 37), soldier strength 230, players 1 and 2 defeated.
void Mission37(Check& m) {
    CGameData* world = m.World();
    const std::uint32_t soldiersNear = CountSoldiersAround(world, 476, 37, 10, 10, world->local_player, IsLevel3Soldier);
    m.CountMilitary();
    world = m.World();
    if (world->players[world->local_player].soldier_strength >= 230 && soldiersNear >= 10 && m.military[1] == 0 &&
        m.military[2] == 0) {
        Win();
    }
    LoseWithoutMilitary(m.LocalMilitary());
}

// 38: a fishery near (540, 47) while the large temple at (118, 541) is gone
// or the local player's.
void Mission38(Check& m) {
    CGameData* world = m.World();
    const Tile& temple = world->tiles[118][541];
    const bool templeHarmless = static_cast<std::int32_t>(temple.owner) == world->local_player ||
                                (temple.flags & layout::kTileHasBuilding) == 0 ||
                                TypeOf(world->buildings[temple.object_number]) != BuildingType::LargeTemple;
    if (m.LocalBuildingNear(540, 47, 40, BuildingType::Fishery) && templeHarmless) {
        Win();
    }
    m.CountMilitary();
    LoseWithoutMilitary(m.LocalMilitary());
}

// 39: one of players 1..4 defeated and small towers at four places.
void Mission39(Check& m) {
    m.CountMilitary();
    bool oneDefeated = false;
    for (std::uint32_t player = 1; player <= 4; ++player) {
        if (m.military[player] == 0) {
            oneDefeated = true;
        }
    }
    if (oneDefeated && m.LocalBuildingNear(251, 74, 10, BuildingType::SmallTower) &&
        m.LocalBuildingNear(568, 387, 10, BuildingType::SmallTower) &&
        m.LocalBuildingNear(340, 576, 10, BuildingType::SmallTower) &&
        m.LocalBuildingNear(67, 319, 10, BuildingType::SmallTower)) {
        Win();
    }
    LoseWithoutMilitary(m.LocalMilitary());
}

// 40: all other players defeated and pyramids near (603, 34) and (56, 607).
void Mission40(Check& m) {
    m.CountMilitary();
    bool allDefeated = true;
    for (std::uint32_t player = 1; player < layout::kPlayers; ++player) {
        if (m.military[player] != 0) {
            allDefeated = false;
        }
    }
    const bool pyramids = m.LocalBuildingNear(603, 34, 12, BuildingType::Pyramid) &&
                          m.LocalBuildingNear(56, 607, 12, BuildingType::Pyramid);
    if (allDefeated && pyramids) {
        Win();
    }
    LoseWithoutMilitary(m.LocalMilitary());
}

// 41: 16 fish and 16 iron lying near each of three places.
void Mission41(Check& m) {
    auto stocked = [&](std::uint32_t x, std::uint32_t y, std::uint32_t radius) {
        constexpr std::uint32_t kAmount = 16;
        return CLogicThread_SumGoodsNear(m.self, nullptr, x, y, radius, Value(Good::Fish), -1) >= kAmount &&
               CLogicThread_SumGoodsNear(m.self, nullptr, x, y, radius, Value(Good::Iron), -1) >= kAmount;
    };
    const bool first = stocked(93, 43, 23);
    const bool second = stocked(40, 250, 23);
    const bool third = stocked(103, 381, 43);
    if (first && second && third) {
        Win();
    }
    m.CountMilitary();
    LoseWithoutMilitary(m.LocalMilitary());
}

// 42: hold four towers.
void Mission42(Check& m) {
    m.CountMilitary();
    if (m.military[0] == 0) {
        Lose();
        return;
    }
    HoldTowers(m, kMission42Towers, 4);
}

// 43: destroy player 1's gold mines before it collects 120 gold.
void Mission43(Check& m) {
    m.CountMilitary();
    if (m.military[0] == 0) {
        Lose();
        return;
    }
    CGameData* world = m.World();
    bool goldMine = false;
    const std::uint32_t buildings = world->building_high_water;
    for (std::uint32_t b = 1; b <= buildings; ++b) {
        if (world->buildings[b].owner == 1 && TypeOf(world->buildings[b]) == BuildingType::GoldMine) {
            goldMine = true;
        }
    }
    if (!goldMine) {
        Win();
        return;
    }
    // Player 1's gold, summed over its areas (the sum carries on from area to area).
    constexpr std::uint32_t kRivalGold = 120;
    std::uint32_t gold = 0;
    for (std::uint32_t area = 1; area <= m.World()->area_high_water; ++area) {
        world = m.World();
        if (world->areas[area].player != 1) {
            continue;
        }
        for (std::uint32_t pile = world->areas[area].piles_by_goods[Value(Good::Gold)]; pile != 0;
             pile = world->piles[pile].next) {
            gold += world->piles[pile].amount;
            if (gold >= kRivalGold) {
                Lose();
                break;
            }
        }
    }
}

// 45: hold six towers; won at once if player 0 has no military.
void Mission45(Check& m) {
    m.CountMilitary();
    if (m.military[0] == 0) {
        Win();
        return;
    }
    HoldTowers(m, kMission45Towers, 6);
}

// 46: collect 120 rice liquor.
void Mission46(Check& m) {
    CGameData* world = m.World();
    constexpr std::uint32_t kLiquor = 120;
    std::uint32_t liquor = 0;
    for (std::uint32_t area = 1; area <= m.World()->area_high_water; ++area) {
        world = m.World();
        if (static_cast<std::int32_t>(world->areas[area].player) != world->local_player) {
            continue;
        }
        for (std::uint32_t pile = world->areas[area].piles_by_goods[Value(Good::RiceLiquor)]; pile != 0;
             pile = world->piles[pile].next) {
            liquor += world->piles[pile].amount;
            if (liquor >= kLiquor) {
                Win();
                break;
            }
        }
    }
    m.CountMilitary();
    LoseWithoutMilitary(m.LocalMilitary());
}

// 47: soldier strength 230 and 10 level-3 soldiers near (344, 430).
void Mission47(Check& m) {
    CGameData* world = m.World();
    const std::uint32_t soldiersNear = CountSoldiersAround(world, 344, 430, 25, 50, world->local_player, IsLevel3Soldier);
    m.CountMilitary();
    world = m.World();
    if (world->players[world->local_player].soldier_strength >= 230 && soldiersNear >= 10) {
        Win();
    }
    LoseWithoutMilitary(m.LocalMilitary());
}

// 48: 6 beer, 6 wine and 6 gems and a large pagoda near (468, 506).
void Mission48(Check& m) {
    m.CountMilitary();
    if (m.military[0] == 0) {
        Lose();
        return;
    }
    CGameData* world = m.World();
    if (world->area_high_water == 0) {
        return;
    }
    const std::uint32_t beer = SumLocalAreaGoods(world, Good::Beer);
    const std::uint32_t wine = SumLocalAreaGoods(world, Good::Wine);
    const std::uint32_t gems = SumLocalAreaGoods(world, Good::Gems);
    constexpr std::uint32_t kAmount = 6;
    if (beer >= kAmount && wine >= kAmount && gems >= kAmount &&
        m.LocalBuildingNear(468, 506, 60, BuildingType::LargePagoda)) {
        Win();
    }
}

// 51: from tick 65,625, while the terrain at (207, 358) is 0x80, each check
// gives player 1 30 mana and calls a mage of player 1 next to that place
// (with two effect records); won with 5 soldiers near (157, 394) and
// soldier strength 197.
void Mission51(Check& m) {
    m.CountMilitary();
    if (m.military[0] == 0) {
        Lose();
        return;
    }
    constexpr std::uint32_t kSummonTick = 65625;
    constexpr std::uint8_t kSummonTerrain = 0x80;
    constexpr std::int32_t kSummonRadius = 10;
    CGameData* world = m.World();
    if (g_gameTick >= kSummonTick && world->tiles[207][358].terrain == kSummonTerrain) {
        world->players[1].mana += 30;
        world = m.World();
        if (world->offsets[0].distance <= kSummonRadius) {
            bool created = false;
            std::uint32_t mage = 0;
            for (std::uint16_t k = 0; !created;) {
                const auto x = static_cast<std::uint16_t>(world->offsets[k].dx + 222);
                const auto y = static_cast<std::uint16_t>(world->offsets[k].dy + 342);
                if (CGameData_IsOnMap(world, nullptr, x, y) != 0) {
                    world = m.World();
                    const Tile& tile = world->tiles[x][y];
                    if ((tile.flags & layout::kTileBlocked) == 0 && tile.settler == 0) {
                        created = true;
                        mage = CGameData_CreateSettler(world, nullptr, x, y, Value(SettlerType::Mage), 1);
                    }
                }
                ++k;
                world = m.World();
                if (world->offsets[k].distance > kSummonRadius) {
                    break;
                }
            }
            if (created) {
                CGameData_AllocTable36700Record(m.World(), nullptr, 1, 1, 1, 207, 358, mage, 1, 0, 0, 0);
                const auto kind = static_cast<std::uint32_t>(g_table658ee4[0] + 1);
                CGameData_AllocTable36700Record(m.World(), nullptr, 2, kind, 1, 207, 358, mage, 1, 0, 0, 0);
            }
        }
    }
    // Won: 5 soldiers of the local player near (157, 394), soldier strength 197.
    world = m.World();
    if (world->offsets[0].distance > kSummonRadius) {
        return;
    }
    std::uint32_t soldiersNear = 0;
    for (std::uint16_t k = 0;;) {
        const auto x = static_cast<std::uint16_t>(world->offsets[k].dx + 157);
        const auto y = static_cast<std::uint16_t>(world->offsets[k].dy + 394);
        if (CGameData_IsOnMap(world, nullptr, x, y) != 0) {
            world = m.World();
            const std::uint32_t settler = world->tiles[x][y].settler;
            if (settler != 0 && static_cast<std::int32_t>(world->settlers[settler].owner) == world->local_player &&
                IsSoldier(world->settlers[settler].art)) {
                ++soldiersNear;
            }
        }
        ++k;
        world = m.World();
        if (world->offsets[k].distance > kSummonRadius) {
            break;
        }
    }
    if (static_cast<std::uint16_t>(soldiersNear) >= 5 &&
        world->players[world->local_player].soldier_strength >= 197) {
        Win();
    }
}

// 52: players 1..3 together lose 350 soldiers, or are defeated.
void Mission52(Check& m) {
    m.CountMilitary();
    if (m.military[0] == 0) {
        Lose();
        return;
    }
    const CGameData* world = m.World();
    const std::uint32_t total = static_cast<std::uint32_t>(world->players[3].soldiers_lost) +
                                static_cast<std::uint32_t>(world->players[1].soldiers_lost) +
                                static_cast<std::uint32_t>(world->players[2].soldiers_lost);
    if (total >= 350) {
        Win();
        return;
    }
    if (m.military[1] == 0 && m.military[2] == 0 && m.military[3] == 0) {
        Win();
    }
}

// 54: 30 level-3 soldiers and soldier strength 255.
void Mission54(Check& m) {
    m.CountMilitary();
    if (m.military[0] == 0) {
        Lose();
        return;
    }
    CGameData* world = m.World();
    if (world->settler_high_water == 0) {
        return;
    }
    const std::uint32_t soldiers = CountSettlers(world, world->local_player, IsLevel3Soldier);
    if (static_cast<std::uint16_t>(soldiers) >= 30 && world->players[world->local_player].soldier_strength >= 255) {
        Win();
    }
}

// 55: player 0's tally +0xc reaches 500 and no large pagoda stands near (508, 463).
void Mission55(Check& m) {
    m.CountMilitary();
    if (m.military[0] == 0) {
        Lose();
        return;
    }
    if (static_cast<std::uint32_t>(g_playerTallies[0].unk0c) < 500) {
        return;
    }
    if (!m.BuildingNear(508, 463, 5, BuildingType::LargePagoda, -1)) {
        Win();
    }
}

// 56: 5 large residences in two parts of the map (tiles x >= 262, y 0..270,
// and x >= 460, y 270..400; the column y = 270 of the second part is counted
// twice, as in the original).
void Mission56(Check& m) {
    m.CountMilitary();
    if (m.military[0] == 0) {
        Lose();
        return;
    }
    CGameData* world = m.World();
    std::uint32_t residences = 0;
    auto countIn = [&](std::uint32_t firstX, std::uint32_t firstY, std::uint32_t height) {
        const std::uint32_t lastX = world->map_size - 1;  // the original's unsigned guard
        if (lastX < firstX) {
            return;
        }
        for (std::uint32_t x = firstX; x <= lastX; ++x) {
            for (std::uint32_t y = firstY; y < firstY + height; ++y) {
                const Tile& tile = world->tiles[x][y];
                if (tile.object_number == 0 || (tile.flags & layout::kTileHasBuilding) == 0) {
                    continue;
                }
                const Building& building = world->buildings[tile.object_number];
                if (building.buildup == 0 && static_cast<std::int32_t>(building.owner) == world->local_player &&
                    TypeOf(building) == BuildingType::LargeResidence) {
                    ++residences;
                }
            }
        }
    };
    countIn(262, 0, 271);
    countIn(460, 270, 131);
    if (static_cast<std::uint16_t>(residences) >= 5) {
        Win();
    }
}

// 58: defeat players 2 and 3 (or survive until tick 112,500) with player 1;
// an allied marketplace at (171, 326) starts trading iron ore to the local player.
void Mission58(Check& m) {
    m.CountMilitary();
    if (m.military[0] == 0 || m.military[1] == 0) {
        Lose();
        return;
    }
    OpenAlliedMarketplace(m.World(), 171, 326, 351, 347, Good::IronOre);
    if ((m.military[2] == 0 && m.military[3] == 0) || g_gameTick >= 112500) {
        Win();
    }
}

// 59: destroy player 3's pyramid and four sphinxes.
void Mission59(Check& m) {
    m.CountMilitary();
    if (m.military[0] == 0) {
        Lose();
        return;
    }
    if (m.BuildingNear(256, 420, 5, BuildingType::Pyramid, 3) || m.BuildingNear(234, 425, 5, BuildingType::Sphinx, 3) ||
        m.BuildingNear(224, 438, 5, BuildingType::Sphinx, 3) || m.BuildingNear(258, 444, 5, BuildingType::Sphinx, 3) ||
        m.BuildingNear(250, 456, 5, BuildingType::Sphinx, 3)) {
        return;
    }
    Win();
}

// 62 (and 61 after the victory conditions): own all six places.
void Mission62(Check& m) {
    m.CountMilitary();
    if (m.military[0] == 0) {
        Lose();
        return;
    }
    CGameData* world = m.World();
    for (const Point& place : kMission62Places) {
        if (static_cast<std::int32_t>(world->tiles[place.x][place.y].owner) != world->local_player) {
            return;
        }
    }
    Win();
}

// 64: the local player's tally +0xc reaches 600; players 0 and 1 must survive.
void Mission64(Check& m) {
    m.CountMilitary();
    if (m.military[0] == 0 || m.military[1] == 0) {
        Lose();
        return;
    }
    if (static_cast<std::uint32_t>(g_playerTallies[m.Local()].unk0c) >= 600) {
        Win();
    }
}

// 66: the local player must have a settler; no large gong near (243, 540)
// or (260, 551) before tick 70,312 wins, one after it loses.
void Mission66(Check& m) {
    CGameData* world = m.World();
    const std::int32_t local = world->local_player;
    std::uint32_t settler = 0;
    while (static_cast<std::int32_t>(world->settlers[settler].owner) != local) {
        ++settler;
        if (settler > world->settler_high_water) {
            Lose();
            return;
        }
    }
    bool noGong = true;
    if (m.BuildingNear(243, 540, 1, BuildingType::LargeGong, -1) ||
        m.BuildingNear(260, 551, 1, BuildingType::LargeGong, -1)) {
        noGong = false;
    }
    constexpr std::uint32_t kDeadline = 70312;
    if (g_gameTick < kDeadline) {
        if (noGong) {
            Win();
        }
    } else if (!noGong) {
        Lose();
    }
}

// 67: defeat player 2, player 1 must survive, and the local player's
// mission counter (player +0x134, bits 0..11) must reach 10.
void Mission67(Check& m) {
    m.CountMilitary();
    if (m.military[0] == 0) {
        Lose();
        return;
    }
    if (m.military[2] != 0) {
        return;
    }
    constexpr std::int32_t kCounting = 0x1000;
    constexpr std::int32_t kCountMask = 0xfff;
    m.World()->players[m.Local()].mission_state |= kCounting;
    if ((m.World()->players[m.Local()].mission_state & kCountMask) < 10) {
        return;
    }
    if (m.military[1] == 0) {
        Win();
    }
}

// 68 and 69: when won, the world stops updating (g_worldUpdateDisabled).
void StopWorldIfWon() {
    if (g_gameOutcome == kWon) {
        g_worldUpdateDisabled = 1;
    }
}

// 68: hold three small towers of player 0 for 9,375 ticks (10 minutes);
// lost if they fall, or if they don't stand by tick 56,250.
void Mission68(Check& m) {
    m.CountMilitary();
    if (m.military[0] == 0) {
        Lose();
        return;
    }
    const bool towers = m.BuildingNear(180, 483, 1, BuildingType::SmallTower, 0) &&
                        m.BuildingNear(235, 497, 1, BuildingType::SmallTower, 0) &&
                        m.BuildingNear(238, 542, 1, BuildingType::SmallTower, 0);
    CGameData* world = m.World();
    const std::uint32_t since = static_cast<std::uint32_t>(world->players[0].mission_state);
    constexpr std::uint32_t kDeadline = 56250;
    constexpr std::uint32_t kHoldTicks = 9375;
    const std::uint32_t tick = g_gameTick;
    if (since == 0) {
        if (tick >= kDeadline) {
            Lose();
            return;
        }
        if (towers) {
            world->players[0].mission_state = static_cast<std::int32_t>(tick);
        }
    } else {
        if (!towers) {
            Lose();
            return;
        }
        if (g_gameTick - since >= kHoldTicks) {
            Win();
        }
    }
    StopWorldIfWon();
}

// 69: player 0 must hold 17 military buildings.
void Mission69(Check& m) {
    m.CountMilitary();
    if (m.military[0] == 0) {
        Lose();
        return;
    }
    for (const TowerGoal& goal : kMission69Towers) {
        if (!m.BuildingNear(goal.x, goal.y, 2, goal.type, 0)) {
            StopWorldIfWon();
            return;
        }
    }
    Win();
    StopWorldIfWon();
}

// 70: defeat player 2.
void Mission70(Check& m) {
    m.CountMilitary();
    if (m.military[0] == 0) {
        Lose();
        return;
    }
    if (m.military[2] == 0) {
        Win();
    }
}

// 72: players 0 and 1 must survive; with players 2 and 3 defeated, won
// when (370, 335) and (411, 266) are in the same walking region.
void Mission72(Check& m) {
    const bool connected = m.World()->tiles[370][335].walk_region == m.World()->tiles[411][266].walk_region;
    m.CountMilitary();
    if (m.military[0] == 0 || m.military[1] == 0) {
        Lose();
        return;
    }
    if (m.military[2] != 0 || m.military[3] != 0) {
        return;
    }
    if (connected) {
        Win();
    }
}

void UseVictoryConditions(Check& m) {
    g_gameOutcome = CLogicThread_CheckVictoryConditions(m.self, nullptr);
}

void CheckMission(Check& m, std::uint32_t mission) {
    switch (mission) {
    case 0:  case 26: case 28: case 29: case 31: case 44: case 49:
    case 50: case 53: case 57: case 60: case 63: case 65: case 71:
        UseVictoryConditions(m);
        return;
    case 1:  case 8:  case 9:  case 11: case 18: case 19: case 20: case 21: case 99:
        DefeatPlayer1(m);
        return;
    case 4:  case 17: case 24:
        DefeatPlayers1And2(m);
        return;
    case 14:
        DefeatPlayer2(m);
        return;
    case 2:  Mission2(m); return;
    case 3:  Mission3(m); return;
    case 5:  Mission5(m); return;
    case 6:  Mission6(m); return;
    case 7:  Mission7(m); return;
    case 10: Mission10(m); return;
    case 12: Mission12(m); return;
    case 13: Mission13(m); return;
    case 15: Mission15(m); return;
    case 16: Mission16(m); return;
    case 22: Mission22(m); return;
    case 23: Mission23(m); return;
    case 25: Mission25(m); return;
    case 27: Mission27(m); return;
    case 30: Mission30(m); return;
    case 32: Mission32(m); return;
    case 33: Mission33(m); return;
    case 34: Mission34(m); return;
    case 35: Mission35(m); return;
    case 36: Mission36(m); return;
    case 37: Mission37(m); return;
    case 38: Mission38(m); return;
    case 39: Mission39(m); return;
    case 40: Mission40(m); return;
    case 41: Mission41(m); return;
    case 42: Mission42(m); return;
    case 43: Mission43(m); return;
    case 45: Mission45(m); return;
    case 46: Mission46(m); return;
    case 47: Mission47(m); return;
    case 48: Mission48(m); return;
    case 51: Mission51(m); return;
    case 52: Mission52(m); return;
    case 54: Mission54(m); return;
    case 55: Mission55(m); return;
    case 56: Mission56(m); return;
    case 58: Mission58(m); return;
    case 59: Mission59(m); return;
    case 61:
        UseVictoryConditions(m);
        Mission62(m);
        return;
    case 62: Mission62(m); return;
    case 64: Mission64(m); return;
    case 66: Mission66(m); return;
    case 67: Mission67(m); return;
    case 68: Mission68(m); return;
    case 69: Mission69(m); return;
    case 70: Mission70(m); return;
    case 72: Mission72(m); return;
    default:  // 73..98 and above 99 (the jump table's last entry, 99, is mission 1's check)
        return;
    }
}

}  // namespace

using CheckGameEndFn = std::uint8_t(__fastcall*)(CLogicThread* self, void* edx);

// Purpose: Decides whether the game is won or lost, every 50 ticks: by the "new game objectives" when their
// time is up, else by the map's victory conditions or the campaign mission's own goals. The result goes to
// g_gameOutcome, from which the logic thread announces the end of the game.
std::uint8_t __fastcall CLogicThread_CheckGameEnd(CLogicThread* self, void*) {
    const std::uint32_t tick = g_gameTick;
    if (tick - self->last_game_end_check < kCheckInterval || g_gameOutcome != kUndecided) {
        return kNotChecked;
    }
    CGameData* world = self->world;
    self->last_game_end_check = tick;
    if (world->objectives_enabled != 0 &&
        g_gameTick >= (static_cast<std::uint32_t>(world->objective_timeout) * kMsPerMinute) >> kTickShift) {
        world->counters_disabled = 1;
        g_gameOutcome = static_cast<std::int32_t>(CGameData_DecideNgoOutcome(self->world, nullptr));
        return static_cast<std::uint8_t>(g_gameOutcome);
    }
    const auto mission = static_cast<std::uint32_t>(g_campaignMission.get());
    if (mission <= kLastMission) {
        Check m{self, {}};
        CheckMission(m, mission);
    }
    return static_cast<std::uint8_t>(g_gameOutcome);
}

S3RE_HOOK(0x00511c50, CLogicThread_CheckGameEnd, CheckGameEndFn, Original_CLogicThread_CheckGameEnd,
          CLogicThread_CheckGameEnd);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestBuildings = 48;
constexpr std::uint32_t kTestSettlers = 64;
constexpr std::uint32_t kTestAreas = 10;
constexpr std::uint32_t kTestTrades = 64;
constexpr std::uint32_t kTestOffsets = 48;

// The game globals the check reads or writes, restored after the test.
struct SavedGlobals {
    SavedGlobals()
        : outcome(g_gameOutcome), mission(g_campaignMission), tick(g_gameTick), disabled(g_worldUpdateDisabled) {
        std::memcpy(tallies, &g_playerTallies, sizeof(tallies));
    }
    ~SavedGlobals() {
        g_gameOutcome = outcome;
        g_campaignMission = mission;
        g_gameTick = tick;
        g_worldUpdateDisabled = disabled;
        std::memcpy(&g_playerTallies, tallies, sizeof(tallies));
    }
    std::int32_t outcome;
    std::int32_t mission;
    std::uint32_t tick;
    std::uint8_t disabled;
    PlayerTally tallies[layout::kPlayers];
};

// Places the missions look at (tiles around them get set up per case).
constexpr Point kTestCentres[] = {
    {122, 112}, {295, 194}, {314, 208}, {300, 222}, {319, 249}, {412, 504}, {171, 326}, {118, 541}, {207, 358},
    {370, 335}, {411, 266}, {332, 74},  {357, 292}, {540, 47},  {251, 74},  {568, 387}, {340, 576}, {67, 319},
    {603, 34},  {56, 607},  {93, 43},   {40, 250},  {103, 381}, {468, 506}, {222, 342}, {157, 394}, {508, 463},
    {256, 420}, {234, 425}, {224, 438}, {258, 444}, {250, 456}, {243, 540}, {260, 551}, {180, 483}, {235, 497},
    {238, 542}, {178, 434}, {476, 37},  {344, 430}, {267, 536}, {344, 519}, {433, 538}, {331, 571}, {196, 67},
    {480, 474}, {129, 90},  {198, 138}, {283, 193}, {230, 100}, {250, 70},  {310, 220}, {115, 175}, {260, 290},
};

std::uint8_t TestPlayer(test::Context& ctx) {
    return static_cast<std::uint8_t>(ctx.rng().Chance(1, 8) ? 0xff : ctx.rng().Below(5));
}

void SetUpTile(test::Context& ctx, test::WorldPair& worlds, std::uint32_t x, std::uint32_t y) {
    const std::uint8_t owner = TestPlayer(ctx);
    const auto flags = static_cast<std::uint8_t>(ctx.rng().Next32() & ~(layout::kTileHasBuilding | layout::kTileBlocked) |
                                                 (ctx.rng().Chance(1, 2) ? layout::kTileHasBuilding : 0) |
                                                 (ctx.rng().Chance(1, 4) ? layout::kTileBlocked : 0));
    const auto object = static_cast<std::uint16_t>(ctx.rng().Chance(1, 6) ? 0 : 1 + ctx.rng().Below(kTestBuildings));
    const auto settler = static_cast<std::uint16_t>(ctx.rng().Chance(1, 3) ? 0 : 1 + ctx.rng().Below(kTestSettlers));
    const auto terrain = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0x80 : ctx.rng().Below(256));
    const auto region = static_cast<std::uint8_t>(ctx.rng().Below(3));
    worlds.Both([&](CGameData* w) {
        Tile& tile = w->tiles[x][y];
        tile.owner = owner;
        tile.flags = flags;
        tile.object_number = object;
        tile.settler = settler;
        tile.terrain = terrain;
        tile.walk_region = region;
    });
}

// Done once: every tile's building, pile and settler numbers and every
// building's trade number point into the small test tables.
void SanitizeWorld(test::Context& ctx, test::WorldPair& worlds) {
    for (std::uint32_t x = 0; x < layout::kMapRowTiles; ++x) {
        for (std::uint32_t y = 0; y < layout::kMapRowTiles; ++y) {
            const auto object = static_cast<std::uint16_t>(ctx.rng().Below(kTestBuildings + 1));
            const auto settler = static_cast<std::uint16_t>(ctx.rng().Below(kTestSettlers + 1));
            worlds.Both([&](CGameData* w) {
                w->tiles[x][y].object_number = object;
                w->tiles[x][y].settler = settler;
            });
        }
    }
    for (std::uint32_t b = 0; b < 4000; ++b) {
        const auto trade = static_cast<std::uint16_t>(ctx.rng().Below(kTestTrades));
        worlds.Both([&](CGameData* w) { w->buildings[b].trade = trade; });
    }
}

void SetUpGameEndCase(test::Context& ctx, test::WorldPair& worlds, CLogicThread& a, CLogicThread& b) {
    // Globals.
    g_campaignMission = ctx.rng().Chance(1, 40) ? static_cast<std::int32_t>(ctx.rng().Next32())
                                                : static_cast<std::int32_t>(ctx.rng().Below(101));
    static constexpr std::uint32_t kThresholds[] = {56250, 65625, 70312, 112500};
    std::uint32_t tick = ctx.rng().Chance(1, 2) ? ctx.rng().Below(130000) : kThresholds[ctx.rng().Below(4)];
    if (ctx.rng().Chance(1, 3)) {
        tick += static_cast<std::uint32_t>(ctx.rng().Range(-2, 2));
    }
    g_gameTick = tick;
    g_gameOutcome = ctx.rng().Chance(1, 20) ? ctx.rng().Range(0, 2) : kUndecided;
    g_worldUpdateDisabled = 0;
    for (std::uint32_t p = 0; p < layout::kPlayers; ++p) {
        g_playerTallies[p].unk0c = ctx.rng().Chance(1, 2) ? static_cast<std::int32_t>(450 + ctx.rng().Below(200))
                                                          : static_cast<std::int32_t>(ctx.rng().Next32());
    }
    const std::uint32_t last = ctx.rng().Chance(1, 10) ? tick - ctx.rng().Below(50) : tick - 50 - ctx.rng().Below(100);
    a.last_game_end_check = last;
    b.last_game_end_check = last;

    // Players.
    const std::int32_t local = static_cast<std::int32_t>(ctx.rng().Below(4));
    const auto ngo = static_cast<std::uint8_t>(ctx.rng().Chance(1, 8));
    const std::int32_t timeout = static_cast<std::int32_t>(ctx.rng().Below(150));
    worlds.Both([&](CGameData* w) {
        w->local_player = local;
        w->objectives_enabled = ngo;
        w->objective_timeout = timeout;
        w->map_size = 768;
        w->building_high_water = 0;
    });
    for (std::uint32_t p = 0; p < layout::kPlayers; ++p) {
        const std::int32_t active = ctx.rng().Chance(1, 5) ? 0 : 1;
        const std::int32_t team = static_cast<std::int32_t>(ctx.rng().Below(3));
        const std::int32_t mana = static_cast<std::int32_t>(150 + ctx.rng().Below(100));
        const std::uint32_t strength = 180 + ctx.rng().Below(100);
        const std::int32_t lost = static_cast<std::int32_t>(ctx.rng().Below(200));
        const std::int32_t state = ctx.rng().Chance(1, 2) ? 0 : static_cast<std::int32_t>(ctx.rng().Next32() & 0x1fff);
        worlds.Both([&](CGameData* w) {
            layout::Player& player = w->players[p];
            player.active = active;
            player.team = team;
            player.mana = mana;
            player.soldier_strength = strength;
            player.soldiers_lost = lost;
            player.mission_state = state;
        });
    }

    // Buildings, settlers.
    static constexpr std::uint8_t kArts[] = {8, 15, 16, 18, 22, 25, 28, 34, 36, 41, 42, 43, 44, 45, 50};
    const std::uint32_t buildings = ctx.rng().Below(kTestBuildings + 1);
    worlds.Both([&](CGameData* w) { w->building_high_water = buildings; });
    for (std::uint32_t n = 1; n <= kTestBuildings; ++n) {
        const std::uint8_t owner = TestPlayer(ctx);
        const std::uint8_t art = ctx.rng().Chance(4, 5) ? kArts[ctx.rng().Below(15)]
                                                        : static_cast<std::uint8_t>(ctx.rng().Below(60));
        const auto buildup = static_cast<std::uint8_t>(ctx.rng().Chance(4, 5) ? 0 : 1 + ctx.rng().Below(200));
        worlds.Both([&](CGameData* w) {
            w->buildings[n].owner = owner;
            w->buildings[n].art = art;
            w->buildings[n].buildup = buildup;
        });
    }
    static constexpr std::uint8_t kTypes[] = {5, 8, 15, 30, 31, 32, 33, 34, 35, 0, 1, 36};
    const std::uint32_t settlers = ctx.rng().Chance(1, 10) ? 0 : ctx.rng().Below(kTestSettlers + 1);
    worlds.Both([&](CGameData* w) { w->settler_high_water = settlers; });
    for (std::uint32_t n = 0; n <= kTestSettlers + 1; ++n) {
        const std::uint8_t owner = TestPlayer(ctx);
        const std::uint8_t art = kTypes[ctx.rng().Below(12)];
        worlds.Both([&](CGameData* w) {
            w->settlers[n].owner = owner;
            w->settlers[n].art = art;
        });
    }

    // Areas with pile chains for every good.
    const std::uint32_t areas = ctx.rng().Below(kTestAreas + 1);
    worlds.Both([&](CGameData* w) { w->area_high_water = areas; });
    std::uint32_t nextPile = 1;
    for (std::uint32_t n = 1; n <= kTestAreas; ++n) {
        const std::uint8_t owner = TestPlayer(ctx);
        worlds.Both([&](CGameData* w) { w->areas[n].player = owner; });
        for (std::uint32_t good = 0; good < layout::kGoodsTypes; ++good) {
            std::uint32_t first = 0;
            const std::uint32_t length = ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Below(5);
            for (std::uint32_t k = 0; k < length; ++k, ++nextPile) {
                const auto amount = static_cast<std::uint8_t>(ctx.rng().Below(40));
                worlds.Both([&](CGameData* w) {
                    w->piles[nextPile].amount = amount;
                    w->piles[nextPile].next = static_cast<std::uint16_t>(first);
                });
                first = nextPile;
            }
            worlds.Both([&](CGameData* w) { w->areas[n].piles_by_goods[good] = static_cast<std::uint16_t>(first); });
        }
    }

    // The offset table: rings of growing distance, then the end (-1).
    std::int32_t distance = ctx.rng().Chance(1, 10) ? ctx.rng().Range(-1, 30) : 0;
    for (std::uint32_t k = 0; k <= kTestOffsets; ++k) {
        const std::int32_t dx = ctx.rng().Range(-5, 5);
        const std::int32_t dy = ctx.rng().Range(-5, 5);
        distance += ctx.rng().Chance(1, 3) ? 1 : 0;
        const std::int32_t stored = k == kTestOffsets ? -1 : distance;
        worlds.Both([&](CGameData* w) {
            w->offsets[k].dx = dx;
            w->offsets[k].dy = dy;
            w->offsets[k].distance = stored;
        });
    }

    // Tiles at and around the missions' places.
    for (const Point& centre : kTestCentres) {
        for (std::int32_t dx = -5; dx <= 5; ++dx) {
            for (std::int32_t dy = -5; dy <= 5; ++dy) {
                if (dx != 0 && dy != 0 && ctx.rng().Chance(1, 2)) {
                    continue;
                }
                SetUpTile(ctx, worlds, static_cast<std::uint32_t>(centre.x + dx),
                          static_cast<std::uint32_t>(centre.y + dy));
            }
        }
    }
    for (const Point* table : {kMission2Places, kMission3Places, kMission7Places, kMission12Places, kMission13Places,
                              kMission15Goals, kMission16Places, kMission23Places, kMission42Towers,
                              kMission45Towers, kMission62Places}) {
        for (std::uint32_t k = 0; k < 4; ++k) {
            SetUpTile(ctx, worlds, static_cast<std::uint32_t>(table[k].x), static_cast<std::uint32_t>(table[k].y));
        }
    }
    for (const Point* table : {kMission2Places, kMission3Places, kMission15Goals}) {
        for (std::uint32_t k = 4; k < 8; ++k) {
            SetUpTile(ctx, worlds, static_cast<std::uint32_t>(table[k].x), static_cast<std::uint32_t>(table[k].y));
        }
    }
    for (std::uint32_t k = 4; k < 6; ++k) {
        SetUpTile(ctx, worlds, kMission45Towers[k].x, kMission45Towers[k].y);
        SetUpTile(ctx, worlds, kMission62Places[k].x, kMission62Places[k].y);
    }
    for (const TowerGoal& goal : kMission69Towers) {
        SetUpTile(ctx, worlds, goal.x, goal.y);
    }
    // Victory conditions (their own test is in victory_conditions.cpp): places on the map.
    layout::VictoryConditions goals = {};
    goals.destroy_players = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3));
    goals.player_to_destroy[ctx.rng().Below(layout::kPlayers)] = 1;
    goals.destroy_buildings = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3));
    for (auto& entry : goals.buildings) {
        entry.player = static_cast<std::int8_t>(ctx.rng().Chance(1, 2) ? -1 : ctx.rng().Below(5));
        entry.building_type = static_cast<std::int8_t>(kArts[ctx.rng().Below(15)]);
    }
    goals.hold_places = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3));
    for (auto& place : goals.places) {
        place.active = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2));
        const Point& centre = kTestCentres[ctx.rng().Below(sizeof(kTestCentres) / sizeof(kTestCentres[0]))];
        place.x = static_cast<std::uint16_t>(centre.x);
        place.y = static_cast<std::uint16_t>(centre.y);
    }
    goals.protect_players = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3));
    goals.protect_minutes[ctx.rng().Below(layout::kPlayers)] = static_cast<std::uint16_t>(ctx.rng().Below(200));
    goals.collect_goods = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3));
    for (auto& goods : goals.goods) {
        goods.amount = static_cast<std::uint16_t>(ctx.rng().Below(40));
        goods.good = static_cast<std::int8_t>(ctx.rng().Below(layout::kGoodsTypes));
    }
    worlds.Both([&](CGameData* w) { w->victory_conditions = goals; });
    // Most places owned by the local player or player 0, so that "all places" goals are reached.
    if (ctx.rng().Chance(1, 2)) {
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? local : 0);
        worlds.Both([&](CGameData* w) {
            for (const Point& place : kMission62Places) {
                w->tiles[place.x][place.y].owner = owner;
            }
            for (const Point& place : kMission15Goals) {
                w->tiles[place.x][place.y].owner = owner;
            }
        });
    }
}

struct GameEndResult {
    std::uint8_t al;
    std::int32_t outcome;
    std::uint8_t disabled;
    std::uint32_t last;
};

// Wrong: mission 3 checks player 1 instead of player 3.
std::uint8_t __fastcall WrongCheckGameEnd(CLogicThread* self, void* edx) {
    if (g_campaignMission != 3) {
        return CLogicThread_CheckGameEnd(self, edx);
    }
    const std::int32_t saved = g_campaignMission;
    g_campaignMission = 2;
    const std::uint8_t result = CLogicThread_CheckGameEnd(self, edx);
    g_campaignMission = saved;
    return result;
}

void CompareCheckGameEnd(test::Context& ctx, CheckGameEndFn candidate, int cases) {
    SavedGlobals saved;
    test::WorldPair worlds(ctx, sizeof(CGameData));
    SanitizeWorld(ctx, worlds);
    CLogicThread threadA = {};
    CLogicThread threadB = {};
    threadA.world = worlds.original();
    threadB.world = worlds.ours();
    for (int i = 0; i < cases; ++i) {
        SetUpGameEndCase(ctx, worlds, threadA, threadB);
        const std::int32_t outcome = g_gameOutcome;
        const std::int32_t mission = g_campaignMission;
        GameEndResult expected{};
        expected.al = Original_CLogicThread_CheckGameEnd(&threadA, nullptr);
        expected.outcome = g_gameOutcome;
        expected.disabled = g_worldUpdateDisabled;
        expected.last = threadA.last_game_end_check;
        g_gameOutcome = outcome;
        g_worldUpdateDisabled = 0;
        GameEndResult actual{};
        actual.al = candidate(&threadB, nullptr);
        actual.outcome = g_gameOutcome;
        actual.disabled = g_worldUpdateDisabled;
        actual.last = threadB.last_game_end_check;
        const bool same = S3RE_EXPECT_EQ(expected.al, actual.al) && S3RE_EXPECT_EQ(expected.outcome, actual.outcome) &&
                          S3RE_EXPECT_EQ(expected.disabled, actual.disabled) &&
                          S3RE_EXPECT_EQ(expected.last, actual.last);
        if (!same || (i % 100 == 99 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: mission %d", i, mission);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

}  // namespace

S3RE_TEST(CLogicThread_CheckGameEnd_MatchesOriginal) {
    CompareCheckGameEnd(ctx, &CLogicThread_CheckGameEnd, 6000);
}
S3RE_TEST(CLogicThread_CheckGameEnd_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareCheckGameEnd(probe, &WrongCheckGameEnd, 1500);
    S3RE_EXPECT(probe.failures() > 0);
}
}  // namespace s3re::game
