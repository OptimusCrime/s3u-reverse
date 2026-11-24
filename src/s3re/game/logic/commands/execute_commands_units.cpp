// MoveUnits, part of CLogicThread::ExecuteCommands (0x535990; see
// execute_commands.h): command ids 0x09 (mode 0), 0x0d (1), 0x0e (2) and
// 0x0f (3), handler 0x536045..0x536fbf.
//
// Payload: player, group, x, y (words). The settler group
// (player_lists[player][group]) is sent to the tile (x, y):
//   mode 0  a plain move; with a group it first looks for an attack:
//           a group of catapults (type 41) looks within distance 5 for an
//           enemy building (towers/castle; any building for the Amazons'
//           gongs, race 3: "GongAttack") or enemy catapult/ship, and attacks
//           it (mode 5, walk_state 2); otherwise any enemy unit within
//           distance 6 keeps mode 0, else the move becomes an attack-move
//           (mode 4: job 176).
//   mode 1  adds a waypoint (path_cache, three slots), for units already walking
//   mode 2  patrol (path_cache: from the unit's position to the tile and back)
//   mode 3  attack-move (job 176)
// Each unit of the player (soldiers, geologist, thief, mage, pioneer,
// catapult, ships) in a movable job gets the next free tile of the
// MapOffset table around the target (twice the offset, ferries once) that
// it can reach: the same walk region, wide region (catapults) or sea area
// (ships; a ferry also keeps 9 tiles from the other ships and, for a target
// on land, lies in water terrain 5). The offset index runs on over the whole
// group. A target tile in water (terrain class 0) makes walk_state 1.

#include "game/logic/commands/execute_commands.h"

#include "game/debug_log.h"
#include "game/globals.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/typed_fields.h"
#include "game/enums/settler_types.h"
#include "game/enums/buildings.h"
#include "game/enums/job_ids.h"
#include "game/enums/commands.h"
#include "game/enums/terrain.h"

#include <cstdint>

namespace s3re::game::commands {

namespace {

using layout::Building;
using layout::MapOffset;
using layout::Player;
using layout::Settler;
using layout::Tile;
using Type = SettlerType;
using Job = SettlerJob;

constexpr std::uintptr_t kSetSettlerTarget = 0x00503610;

// Modes of the four ids, and the two the attack search turns mode 0 into.
enum class MoveMode : std::uint32_t {
    Move = 0,
    Waypoint = 1,
    Patrol = 2,
    AttackMove = 3,
    AttackMoveFound = 4,   // mode 0 of a group, no attack target, no enemy near
    AttackTarget = 5,      // catapults attacking a building or unit
};

// walk_state given to the units (Settler +0x1a).
constexpr std::uint8_t kWalkNormal = 0;
constexpr std::uint8_t kWalkToWater = 1;     // the target tile is water: board a ship
constexpr std::uint8_t kWalkAttack = 2;      // attack (shoot at) the target
constexpr std::uint8_t kWalkThiefPatrol = 3;  // a thief's patrol

constexpr std::int32_t kAttackSearchDistance = 5;
constexpr std::int32_t kEnemySearchDistance = 6;
constexpr std::uint32_t kMaxOffsets = 5000;   // offsets tried for the whole group
constexpr std::uint32_t kOffsetClamp = 9900;  // 0x536faa, never reached (the loop stops at 5000)
constexpr std::uint32_t kShipSpacing = 9;     // ferries keep this distance from the other ships' tiles
constexpr std::uint32_t kMaxShipTiles = 1024;  // our array; the original's grows on its stack
constexpr std::uint16_t kNoWaypoint = 0xffff;
constexpr std::uint16_t kPatrolMarker = 0xfffe;
constexpr std::uint16_t kAttackMarker = 0xfffd;
constexpr std::uint32_t kNoWorkTile = 0xffff;  // geologist / pioneer: no work tile (saved_animation)
constexpr std::uint8_t kTileReserved = 0x02;   // Tile::flags: a geologist's / pioneer's work tile
constexpr std::uint8_t kAttackMoveCountdown = 40;  // Settler +0x3c of a unit sent by mode 4
constexpr std::int32_t kPlayerComputerTakeover = 2;  // Player::active of a dropped player (ExecuteCommands 0xfe)
constexpr std::int32_t kRaceAmazon = 3;

Settler& SettlerAt(CGameData* world, std::uint32_t n) {
    return *(world->settlers + n);
}
Building& BuildingAt(CGameData* world, std::uint32_t n) {
    return *(world->buildings + n);
}
const Player& PlayerAt(CGameData* world, std::uint32_t n) {
    return *(world->players + n);
}
const MapOffset& OffsetAt(CGameData* world, std::uint32_t k) {
    return *(world->offsets + k);
}
Tile& TileAt(CGameData* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}
bool SameTeam(CGameData* world, std::uint32_t ownerA, std::uint32_t ownerB) {
    return PlayerAt(world, ownerA).team == PlayerAt(world, ownerB).team;
}

bool IsShip(std::uint8_t type) {
    return type == Value(Type::TradeShip) || type == Value(Type::Ferry);
}
bool IsWorkTileUnit(std::uint8_t type) {
    return type == Value(Type::Pioneer) || type == Value(Type::Geologist);
}

// The units the group command moves (0x53654c), without the ships the enemy search (0x536458) looks for.
bool IsFightingUnit(std::uint8_t type) {
    switch (static_cast<Type>(type)) {
    case Type::Swordsman1:
    case Type::Bowman1:
    case Type::Pikeman1:
    case Type::Bowman2:
    case Type::Pikeman2:
    case Type::Swordsman2:
    case Type::Bowman3:
    case Type::Pikeman3:
    case Type::Swordsman3:
    case Type::Geologist:
    case Type::Thief:
    case Type::Mage:
    case Type::Pioneer:
    case Type::Catapult:
        return true;
    default:
        return false;
    }
}
bool IsMovableUnit(std::uint8_t type) {
    return IsFightingUnit(type) || IsShip(type);
}

// Jobs in which a unit takes orders (0x536597).
bool IsMovableJob(Job job) {
    switch (job) {
    case Job::GeologistWork:
    case Job::GeologistDone:
    case Job::PioneerWork:
    case Job::PioneerDone:
    case Job::UnitIdle:
    case Job::UnitWalk:
    case Job::UnitFighting:
    case Job::UnitWalkCountdown: return true;
    default:
        return false;
    }
}

bool IsMilitaryBuilding(std::uint8_t type) {
    return type == Value(BuildingType::SmallTower) || type == Value(BuildingType::LargeTower) ||
           type == Value(BuildingType::Castle);
}

struct MoveOrder {
    MoveMode mode;
    std::uint32_t player;
    std::uint32_t group;
    std::int32_t count;     // group length when the command came ([ebp-0x58])
    std::uint32_t targetX;  // [ebp-0x48]
    std::uint32_t targetY;  // [ebp-0x40]
    bool targetOnLand;      // [ebp-0x50]: terrain class of the commanded tile != water
    std::uint8_t walkState;  // [ebp-0x38]
    bool gongs;             // [ebp-0x15]: the group is Amazon catapults (gongs)
};

std::uint16_t* GroupSettlers(CGameData* world, std::uint32_t player, std::uint32_t group) {
    return &world->player_lists[0][0][0] + (player * 10 + group) * 100;
}
std::int32_t& GroupLength(CGameData* world, std::uint32_t player, std::uint32_t group) {
    return *(&world->player_list_lengths[0][0] + (player * 10 + group));
}

// 0x536127..0x5361b8: whether every unit of the group is a catapult; the
// gong flag says whether the last one checked belongs to an Amazon.
bool GroupIsCatapults(Run& run, MoveOrder& order) {
    order.gongs = false;
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(GroupLength(run.world(), order.player, order.group));
         ++i) {
        CGameData* world = run.world();
        const Settler& unit = SettlerAt(world, GroupSettlers(world, order.player, order.group)[i]);
        if (unit.art != Value(Type::Catapult)) {
            order.gongs = false;
            return false;
        }
        order.gongs = PlayerAt(world, unit.owner).race == kRaceAmazon;
        if (order.gongs) {
            GameDebugLog(texts::kGongAttack);
        }
    }
    return true;
}

// 0x5361c5..0x5363a8: catapults look for an enemy building or catapult/ship
// within distance 5 of the target; found: they attack it.
void FindAttackTarget(Run& run, MoveOrder& order) {
    bool found = false;
    for (std::uint32_t k = 0; OffsetAt(run.world(), k).distance < kAttackSearchDistance && !found; ++k) {
        CGameData* world = run.world();
        const std::uint32_t x = order.targetX + static_cast<std::uint32_t>(OffsetAt(world, k).dx);
        const std::uint32_t y = order.targetY + static_cast<std::uint32_t>(OffsetAt(world, k).dy);
        if (x >= world->map_size || y >= world->map_size) {
            continue;
        }
        const std::uint32_t tile = TileOffset(x, y);
        if ((TileAtOffset(world, tile).flags & layout::kTileHasBuilding) != 0) {
            const Building& building = BuildingAt(world, TileAtOffset(world, tile).object_number);
            if (!SameTeam(world, building.owner, order.player) && (IsMilitaryBuilding(building.art) || order.gongs)) {
                found = true;
                order.walkState = kWalkAttack;
                order.targetX = building.x;
                order.targetY = building.y;
                GameDebugLog(texts::kAttackBuilding);
            }
        }
        world = run.world();
        const std::uint32_t number = TileAtOffset(world, tile).settler;
        if (number == 0 || order.gongs) {
            continue;
        }
        const Settler& unit = SettlerAt(world, number);
        if (SameTeam(world, unit.owner, order.player)) {
            continue;
        }
        if (unit.art == Value(Type::Catapult) || IsShip(unit.art)) {
            found = true;
            order.walkState = kWalkAttack;
            order.targetX = unit.x;
            order.targetY = unit.y;
        }
    }
    if (found) {
        order.mode = MoveMode::AttackTarget;
    }
}

// 0x5363af..0x5364a7: an enemy unit within distance 6 of the target makes it a plain move.
void CheckEnemiesNear(Run& run, MoveOrder& order) {
    for (std::uint32_t k = 0;
         OffsetAt(run.world(), k).distance < kEnemySearchDistance && order.mode == MoveMode::AttackMoveFound; ++k) {
        CGameData* world = run.world();
        const std::uint32_t x = order.targetX + static_cast<std::uint32_t>(OffsetAt(world, k).dx);
        const std::uint32_t y = order.targetY + static_cast<std::uint32_t>(OffsetAt(world, k).dy);
        if (x >= world->map_size || y >= world->map_size) {
            continue;
        }
        const std::uint32_t number = TileAt(world, x, y).settler;
        if (number == 0) {
            continue;
        }
        const Settler& unit = SettlerAt(world, number);
        if (!SameTeam(world, unit.owner, order.player) && IsFightingUnit(unit.art)) {
            order.mode = MoveMode::Move;
        }
    }
}

// 0x5364b3..0x5364e4: a target inside the map in water (terrain class 0) is boarded.
void CheckWaterTarget(Run& run, MoveOrder& order) {
    CGameData* world = run.world();
    const auto size = static_cast<std::int32_t>(world->map_size);  // signed compares here
    const auto x = static_cast<std::int32_t>(order.targetX);
    const auto y = static_cast<std::int32_t>(order.targetY);
    if (x >= 0 && x < size && y >= 0 && y < size &&
        (TileAt(world, order.targetX, order.targetY).terrain & kTerrainClassMask) == kTerrainClassWater) {
        order.walkState = kWalkToWater;
    }
}

// The ships' tiles given so far ([ebp-0xb44], count [ebp-0x30]).
struct ShipTiles {
    std::uint32_t count = 0;
    std::uint32_t x[kMaxShipTiles];
    std::uint32_t y[kMaxShipTiles];
};

// 0x536972..0x5369dc: a ferry's tile must be water (terrain 5) when the
// command's tile is on land, and 9 tiles away from the other ships' tiles.
bool FerryTileFree(Run& run, const MoveOrder& order, const ShipTiles& ships, std::uint32_t x, std::uint32_t y) {
    bool free = true;
    if (order.targetOnLand && TileAt(run.world(), x, y).terrain != kTerrainWater5) {
        free = false;
    }
    for (std::uint32_t j = 0; j < ships.count; ++j) {
        if (Call<callee::kTileDistance>(run.world(), x, y, ships.x[j], ships.y[j]) < kShipSpacing) {
            free = false;
        }
    }
    return free;
}

// 0x53667e..0x5367a0: whether the unit can reach the tile.
bool CanReach(CGameData* world, std::uint32_t number, std::uint32_t x, std::uint32_t y, bool shipTileFree) {
    const Settler& unit = SettlerAt(world, number);
    const Tile& to = TileAt(world, x, y);
    const Tile& from = TileAt(world, unit.x, unit.y);
    if (IsShip(unit.art) && from.area == to.area && to.area != 0 && shipTileFree) {
        return true;  // the same sea
    }
    if (unit.art == Value(Type::Catapult) && from.wide_region == to.wide_region && to.wide_region != 0) {
        return true;
    }
    if (unit.art == Value(Type::Catapult) || IsShip(unit.art)) {
        return false;
    }
    return from.walk_region == to.walk_region;
}

// 0x5367fe..0x5368db (and its copies at 0x536aa1, 0x536cc7): a geologist or
// pioneer with a work tile gives up that tile's reservation.
void ReleaseWorkTile(Run& run, std::uint32_t number) {
    CGameData* world = run.world();
    Settler& unit = SettlerAt(world, number);
    if (!IsWorkTileUnit(unit.art) || unit.saved_animation == kNoWorkTile) {
        return;
    }
    if (unit.job == Job::UnitWalk) {
        TileAt(world, unit.target_x, unit.target_y).flags &= static_cast<std::uint8_t>(~kTileReserved);
    }
    if (unit.job == Job::PioneerWork || unit.job == Job::PioneerDone) {
        TileAt(world, unit.x, unit.y).flags &= static_cast<std::uint8_t>(~kTileReserved);
    }
    if (unit.job == Job::GeologistWork || unit.job == Job::GeologistDone) {
        TileAt(world, unit.x, unit.y).flags &= static_cast<std::uint8_t>(~kTileReserved);
    }
}

void SetTarget(Run& run, std::uint32_t number, std::uint32_t x, std::uint32_t y) {
    Call<kSetSettlerTarget>(run.world(), number, x, y);
}

// The common end of every order: walking, not holding position.
void StartWalking(Settler& unit, std::uint8_t walkState) {
    unit.job = Job::UnitWalk;
    unit.walk_state = walkState;
    unit.flags &= static_cast<std::uint8_t>(~layout::kSettlerNoFight);
}

// 0x5367fe..0x536a93: mode 1, a waypoint.
void GiveWaypoint(Run& run, std::uint32_t number, std::uint32_t x, std::uint32_t y) {
    ReleaseWorkTile(run, number);
    Settler& unit = SettlerAt(run.world(), number);
    switch (unit.job) {
    case Job::GeologistWork:
    case Job::GeologistDone:
    case Job::PioneerWork:
    case Job::PioneerDone:
    case Job::UnitIdle:
    case Job::UnitFighting:
        SetTarget(run, number, x, y);  // not walking yet: walk there
        break;
    default:  // walking (jobs 53, 176): queue it in the first free of three slots
        if (unit.art == Value(Type::Thief) && unit.walk_state != 0) {
            break;
        }
        for (std::size_t slot = 0; slot < 6; slot += 2) {
            if (unit.path_cache[slot] == kNoWaypoint) {
                unit.path_cache[slot] = static_cast<std::uint16_t>(x);
                unit.path_cache[slot + 1] = static_cast<std::uint16_t>(y);
                break;
            }
        }
        break;
    }
    Settler& after = SettlerAt(run.world(), number);
    if (after.art == Value(Type::Thief) && after.walk_state != 0) {
        return;  // a thief at work keeps its state
    }
    StartWalking(after, kWalkNormal);
}

// 0x536aa1..0x536cc2: mode 2, patrol between the unit's position and the tile.
void GivePatrol(Run& run, std::uint32_t number, std::uint32_t x, std::uint32_t y) {
    ReleaseWorkTile(run, number);
    SetTarget(run, number, x, y);
    Settler& unit = SettlerAt(run.world(), number);
    if (IsWorkTileUnit(unit.art)) {
        unit.saved_animation = x;  // the new work tile
        unit.saved_counter = static_cast<std::uint16_t>(y);
        StartWalking(unit, kWalkNormal);
        return;
    }
    unit.path_cache[0] = unit.x;
    unit.path_cache[1] = unit.y;
    unit.path_cache[2] = static_cast<std::uint16_t>(x);
    unit.path_cache[3] = static_cast<std::uint16_t>(y);
    unit.path_cache[4] = kPatrolMarker;
    unit.path_cache[5] = kPatrolMarker;
    StartWalking(unit, unit.art == Value(Type::Thief) ? kWalkThiefPatrol : kWalkNormal);
}

// 0x536cc7..0x536f89: modes 0, 3, 4, 5.
void GiveMove(Run& run, const MoveOrder& order, std::uint32_t number, std::uint32_t x, std::uint32_t y) {
    ReleaseWorkTile(run, number);
    SetTarget(run, number, x, y);
    Settler& unit = SettlerAt(run.world(), number);
    if (IsWorkTileUnit(unit.art)) {
        unit.saved_animation = kNoWorkTile;
        unit.saved_counter = static_cast<std::uint16_t>(kNoWorkTile);
    }
    const bool attackMove = order.mode == MoveMode::AttackMove ||
                            (order.mode == MoveMode::AttackMoveFound &&
                             PlayerAt(run.world(), unit.owner).active != kPlayerComputerTakeover);
    if (attackMove) {
        if (IsShip(unit.art)) {
            unit.job = Job::UnitWalk;
        } else {
            unit.job = Job::UnitWalkCountdown;
            unit.ship_walk = order.mode == MoveMode::AttackMove ? 0 : kAttackMoveCountdown;
        }
        unit.walk_state = order.walkState;
        unit.flags &= static_cast<std::uint8_t>(~layout::kSettlerNoFight);
        return;
    }
    StartWalking(unit, order.walkState);
    if (order.mode == MoveMode::AttackTarget) {
        unit.path_cache[4] = kAttackMarker;
        unit.path_cache[5] =
            static_cast<std::uint16_t>(g_raceAttackValue659db0[PlayerAt(run.world(), unit.owner).race]);
        unit.path_cache[2] = static_cast<std::uint16_t>(order.targetX);
        unit.path_cache[3] = static_cast<std::uint16_t>(order.targetY);
    }
}

// 0x5365da..0x536fb1: tries the next offsets around the target until the unit
// can reach one; `next` is the offset index, shared by the whole group.
void PlaceUnit(Run& run, const MoveOrder& order, ShipTiles& ships, std::uint32_t number, std::uint32_t& next) {
    bool placed = false;
    while (!placed && next < kMaxOffsets) {
        const std::uint32_t k = next;
        next = next + 1 > kOffsetClamp ? kOffsetClamp : next + 1;
        CGameData* world = run.world();
        const std::uint8_t type = SettlerAt(world, number).art;
        const MapOffset& offset = OffsetAt(world, k);
        // Ferries stand one offset apart, everything else two.
        const std::uint32_t spread = type == Value(Type::Ferry) ? 1 : 2;
        const std::uint32_t x = order.targetX + static_cast<std::uint32_t>(offset.dx) * spread;
        const std::uint32_t y = order.targetY + static_cast<std::uint32_t>(offset.dy) * spread;
        if (x >= world->map_size || y >= world->map_size) {
            continue;
        }
        bool shipTileFree = false;  // only read for ships, which always set it
        if (type == Value(Type::TradeShip)) {
            shipTileFree = true;
        } else if (type == Value(Type::Ferry)) {
            shipTileFree = FerryTileFree(run, order, ships, x, y);
        }
        if (!CanReach(run.world(), number, x, y, shipTileFree)) {
            continue;
        }
        if (IsShip(SettlerAt(run.world(), number).art) && ships.count < kMaxShipTiles) {
            ships.x[ships.count] = x;
            ships.y[ships.count] = y;
            ++ships.count;
        }
        switch (order.mode) {
        case MoveMode::Waypoint: GiveWaypoint(run, number, x, y); break;
        case MoveMode::Patrol: GivePatrol(run, number, x, y); break;
        default: GiveMove(run, order, number, x, y); break;
        }
        placed = true;
    }
}

MoveMode ModeOf(std::uint8_t id) {
    switch (static_cast<CommandId>(id)) {
    case CommandId::MoveUnitsMode1: return MoveMode::Waypoint;
    case CommandId::MoveUnitsMode2: return MoveMode::Patrol;
    case CommandId::MoveUnitsMode3: return MoveMode::AttackMove;
    default: return MoveMode::Move;
    }
}

}  // namespace

void MoveUnits(Run& run) {
    MoveOrder order{};
    order.mode = ModeOf(run.Id());
    order.player = run.Byte(0);
    order.group = run.Byte(1);
    order.targetX = run.Word(2);
    order.targetY = run.Word(4);
    order.walkState = kWalkNormal;
    order.gongs = false;
    CGameData* world = run.world();
    order.count = GroupLength(world, order.player, order.group);
    order.targetOnLand =
        (TileAt(world, order.targetX, order.targetY).terrain & kTerrainClassMask) != kTerrainClassWater;

    // 0x536111: a plain move of a group looks for something to attack.
    if (order.mode == MoveMode::Move && order.count != 0) {
        order.mode = MoveMode::AttackMoveFound;
        if (GroupIsCatapults(run, order)) {
            FindAttackTarget(run, order);
        }
        CheckEnemiesNear(run, order);
    }
    if (order.walkState == kWalkNormal) {
        CheckWaterTarget(run, order);
    }

    // 0x5364eb..0x536fb6: each unit of the group.
    ShipTiles ships;
    std::uint32_t next = 0;
    for (std::int32_t i = 0; i < order.count; ++i) {
        world = run.world();
        const std::uint32_t number = GroupSettlers(world, order.player, order.group)[i];
        if (number > world->settler_high_water) {
            continue;
        }
        const Settler& unit = SettlerAt(world, number);
        if (unit.owner != order.player || !IsMovableUnit(unit.art) || !IsMovableJob(unit.job)) {
            continue;
        }
        PlaceUnit(run, order, ships, number, next);
    }
}

}  // namespace s3re::game::commands

// ---------------------------------------------------------------------------
// Tests: MoveUnits through the whole ExecuteCommands (execute_commands_test.h).
// ---------------------------------------------------------------------------

#include "game/logic/commands/execute_commands_test.h"

#include <cstring>

namespace s3re::game::commands {

namespace {

using bench::Bench;
using bench::MessageBuilder;

constexpr std::uint32_t kUnitsMap = 384;
constexpr std::uint32_t kUnitsBuildings = 40;
constexpr std::uint32_t kUnitsSettlers = 120;
constexpr std::uint32_t kUnitsOffsets = 600;

void InstallUnitStubs(test::CalleeStubs& stubs) {
    // SetSettlerTarget: records; sets the target like the real one (the handler doesn't read it back).
    stubs.Install<3, kSetSettlerTarget>([](CGameData* w, std::uint32_t, const std::uint32_t* a) {
        w->settlers[a[0]].target_x = static_cast<std::uint16_t>(a[1]);
        w->settlers[a[0]].target_y = static_cast<std::uint16_t>(a[2]);
        return 0u;
    });
    stubs.Install<4, callee::kTileDistance>([](CGameData*, std::uint32_t, const std::uint32_t* a) {
        const std::int32_t dx = static_cast<std::int32_t>(a[0] - a[2]);
        const std::int32_t dy = static_cast<std::int32_t>(a[1] - a[3]);
        return static_cast<std::uint32_t>((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy));
    });
}

std::uint8_t UnitType(test::Rng& rng) {
    const std::uint8_t types[] = {5, 8, 15, 30, 31, 32, 33, 34, 35, 22, 23, 36, 40, 41, 41, 41, 43, 44, 44, 0, 1, 42};
    return types[rng.Below(static_cast<std::uint32_t>(sizeof(types)))];
}

void SetUpUnitWorld(test::Context& ctx, Bench& bench) {
    test::Rng& rng = ctx.rng();
    test::WorldPair& worlds = bench.worlds;
    worlds.Both([&](CGameData* w) {
        w->map_size = kUnitsMap;
        w->building_high_water = kUnitsBuildings;
        w->settler_high_water = kUnitsSettlers;
    });
    for (std::uint32_t x = 0; x < layout::kMapRowTiles; ++x) {
        for (std::uint32_t y = 0; y < layout::kMapRowTiles; ++y) {
            Tile t;
            rng.Fill(&t, sizeof(t));
            t.settler = static_cast<std::uint16_t>(rng.Chance(1, 3) ? rng.Below(kUnitsSettlers + 1) : 0);
            t.object_number = static_cast<std::uint16_t>(rng.Below(kUnitsBuildings + 1));
            const std::uint8_t terrains[] = {5, 5, 6, 16, 16, 32, 48, 0x10, 0x11};
            t.terrain = terrains[rng.Below(9)];
            t.walk_region = static_cast<std::uint8_t>(rng.Below(3));
            t.area = static_cast<std::uint16_t>(rng.Below(3));
            t.wide_region = static_cast<std::uint16_t>(rng.Below(3));
            worlds.Both([&](CGameData* w) { w->tiles[x][y] = t; });
        }
    }
    for (std::uint32_t n = 0; n <= kUnitsBuildings + 1; ++n) {
        const auto owner = static_cast<std::uint8_t>(rng.Below(5));
        const std::uint8_t arts[] = {15, 16, 18, 2, 1, 33};
        const std::uint8_t art = arts[rng.Below(6)];
        const auto bx = static_cast<std::uint16_t>(rng.Below(kUnitsMap));
        const auto by = static_cast<std::uint16_t>(rng.Below(kUnitsMap));
        worlds.Both([&](CGameData* w) {
            w->buildings[n].owner = owner;
            w->buildings[n].art = art;
            w->buildings[n].x = bx;
            w->buildings[n].y = by;
        });
    }
    for (std::uint32_t n = 0; n <= kUnitsSettlers + 1; ++n) {
        Settler s;
        rng.Fill(&s, sizeof(s));
        s.owner = static_cast<std::uint8_t>(rng.Below(5));
        s.art = UnitType(rng);
        s.x = static_cast<std::uint16_t>(rng.Below(kUnitsMap));
        s.y = static_cast<std::uint16_t>(rng.Below(kUnitsMap));
        s.target_x = static_cast<std::uint16_t>(rng.Below(kUnitsMap));
        s.target_y = static_cast<std::uint16_t>(rng.Below(kUnitsMap));
        const std::uint16_t jobs[] = {212, 213, 302, 303, 55, 53, 142, 176, 0, 1};
        s.job = static_cast<SettlerJob>(jobs[rng.Below(10)]);
        s.saved_animation = rng.Chance(1, 2) ? 0xffff : rng.Below(kUnitsMap);
        s.walk_state = static_cast<std::uint8_t>(rng.Chance(2, 3) ? 0 : 1);
        for (auto& c : s.path_cache) {
            c = static_cast<std::uint16_t>(rng.Chance(1, 2) ? 0xffff : rng.Below(kUnitsMap));
        }
        worlds.Both([&](CGameData* w) { w->settlers[n] = s; });
    }
    for (std::uint32_t p = 0; p < layout::kPlayers; ++p) {
        const auto team = static_cast<std::int32_t>(rng.Below(3));
        const auto race = static_cast<std::int32_t>(rng.Below(4));
        const auto active = static_cast<std::int32_t>(rng.Chance(1, 4) ? 2 : 1);
        worlds.Both([&](CGameData* w) {
            w->players[p].team = team;
            w->players[p].race = race;
            w->players[p].active = active;
        });
    }
    for (std::uint32_t k = 0; k < kUnitsOffsets; ++k) {
        const layout::MapOffset offset{rng.Range(-8, 8), rng.Range(-8, 8), static_cast<std::int32_t>(k / 6), 0};
        worlds.Both([&](CGameData* w) { w->offsets[k] = offset; });
    }
}

// The group to move: 0..12 units, some of them catapults of one owner (the attack search).
void RandomizeGroup(test::Context& ctx, Bench& bench, std::uint32_t player, std::uint32_t group) {
    test::Rng& rng = ctx.rng();
    const auto length = static_cast<std::int32_t>(rng.Chance(1, 8) ? 0 : rng.Below(13));
    const bool catapults = rng.Chance(1, 3);
    std::uint16_t members[100];
    for (auto& m : members) {
        m = static_cast<std::uint16_t>(rng.Chance(1, 20) ? kUnitsSettlers + 1 + rng.Below(5) : rng.Below(kUnitsSettlers + 1));
    }
    bench.worlds.Both([&](CGameData* w) {
        w->player_list_lengths[player][group] = length;
        std::memcpy(w->player_lists[player][group], members, sizeof(members));
    });
    for (std::int32_t i = 0; i < length; ++i) {
        const std::uint32_t n = members[i];
        if (n > kUnitsSettlers) {
            continue;
        }
        const auto owner = static_cast<std::uint8_t>(rng.Chance(7, 8) ? player : rng.Below(4));
        const std::uint8_t art = catapults && rng.Chance(15, 16) ? 41 : UnitType(rng);
        const std::uint16_t jobs[] = {212, 213, 302, 303, 55, 53, 142, 176, 0};
        const std::uint16_t job = jobs[rng.Below(9)];
        bench.worlds.Both([&](CGameData* w) {
            w->settlers[n].owner = owner;
            w->settlers[n].art = art;
            w->settlers[n].job = static_cast<SettlerJob>(job);
        });
    }
}

void CompareMoveUnits(test::Context& ctx, ExecuteCommandsFn candidate, int cases) {
    Bench bench(ctx);
    SetUpUnitWorld(ctx, bench);
    InstallUnitStubs(bench.stubs);
    test::Rng& rng = ctx.rng();
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t step = 1 + rng.Below(1000);
        bench.SetStep(step);
        MessageBuilder messages;
        const std::uint32_t count = 1 + rng.Below(2);
        for (std::uint32_t c = 0; c < count; ++c) {
            const std::uint32_t player = rng.Below(4);
            const std::uint32_t group = rng.Below(10);
            RandomizeGroup(ctx, bench, player, group);
            const CommandId ids[] = {CommandId::MoveUnits, CommandId::MoveUnits, CommandId::MoveUnitsMode1,
                                     CommandId::MoveUnitsMode2, CommandId::MoveUnitsMode3};
            const std::uint32_t x = rng.Below(kUnitsMap + (rng.Chance(1, 20) ? 40 : 0));
            const std::uint32_t y = rng.Below(kUnitsMap + (rng.Chance(1, 20) ? 40 : 0));
            messages.Command(ids[rng.Below(5)],
                             {static_cast<std::uint8_t>(player), static_cast<std::uint8_t>(group),
                              static_cast<std::uint8_t>(x >> 8), static_cast<std::uint8_t>(x),
                              static_cast<std::uint8_t>(y >> 8), static_cast<std::uint8_t>(y)});
        }
        messages.Grant(step + 1);
        if (!bench.Run(ctx, i, messages, -1, candidate)) {
            return;
        }
    }
    bench.Finish(ctx);
}

// Wrong: every unit is placed one tile further in x (offset 0 shifted while ours runs).
std::uint32_t __fastcall WrongMoveUnits(CLogicThread* self, void* edx) {
    CGameData* world = self->world;
    world->offsets[0].dx += 1;
    const std::uint32_t result = CLogicThread_ExecuteCommands(self, edx);
    world->offsets[0].dx -= 1;
    return result;
}

}  // namespace

S3RE_TEST(CLogicThread_ExecuteCommands_MoveUnits_MatchesOriginal) {
    CompareMoveUnits(ctx, &CLogicThread_ExecuteCommands, 4000);
}
S3RE_TEST(CLogicThread_ExecuteCommands_MoveUnits_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMoveUnits(probe, &WrongMoveUnits, 300);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game::commands
