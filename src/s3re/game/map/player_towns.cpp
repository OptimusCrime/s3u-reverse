// The players' start towns on a map without buildings (maps without a
// buildings chunk 8, random maps): CGameData::LoadMap calls AddPlayerTowns.
//
// CGameData::PlaceTown (0x00506cf0): (player, x, y), ret 0xc:
//   DebugLog "placing town: %u at: %u,%u". The start building is a small
//   tower (type 15) of the player at (x, y): AddBuildingSite(x, y, player,
//   15). Its layout (0x6aa170, race from Player +4) from the footprint
//   marker 0x80000000 on: each (dx, dy) up to 0x80000001 gets the footprint
//   flag (tile +0xa | 8), BlockTile, object byte +7 = 0 and WearPath; the
//   rest up to 0x80000003 (a 0x80000002 marker where a pair would start is
//   skipped, and then the next pair is read without checking for the end)
//   gets the footprint flag and WearPath. Then the 31 x 31 heights of the
//   levelling window (footprint_scratch[0], cell i * 31 + j) go to the
//   tiles (x - 15 + i, y - 15 + j), and UpdateShadeAround runs on each of
//   them (j outer, i inner). BuildingDone(building), the building's
//   garrison word +8 = 0xffff, RecalculateTerritory(x, y), then a
//   Swordsman1 (type 5) is created at (x, y) and becomes the garrison: next
//   +0x10 = 0, job +0xe = 23 (SwordsmanInTower), building +0x22, counter
//   +0xc = 0, and the tile at its position loses it (settler word = 0).
//   The start goods (GameSettings::StartGoods 0x412300 on g_gameSettings:
//   1 low, 0 or 2 medium, anything else high) choose the start settlers
//   (SpawnSettlersAround: count, type at x-8/y-16, x+8/y+16, x+14/y+10,
//   x-4/y+10, and donkeys at x+3/y+6 for medium and high) and the goods
//   (DropGoodNear(good, amount, x, y)). A computer player (Player +0 == 2)
//   gets 10 more Swordsman1 at x+8/y+16. Finally Player +0x18 = 1, start_x
//   = x, start_y = y. EAX = world + player * 0x148; the only caller,
//   AddPlayerTowns, doesn't use it.
// CGameData::AddPlayerTowns (0x005074b0): (), plain ret, EAX = 20 (the
//   caller ignores it): for each player 0..19 that takes part (Player +0)
//   and has no town yet (+0x18 == 0): DebugLog "Placing player %u", then by
//   world+0x2d274 (the random map's symmetry; above 3: nothing):
//   - start position given (start_x != -1): the offset entries 0..9999
//     around (start_x, start_y) are tried; a candidate must be a free small
//     tower site (IsFreeSmallTowerSite) and, with symmetry, at distance >
//     60 from its mirror images (TileDistance). The town is placed there,
//     and the first unplaced player of team 1 (FindPlayerByField0C; 2 and 3
//     for symmetry 3) gets the mirrored position, after IsFreeSmallTowerSite
//     on it (the result is ignored). Symmetry 3 checks only team 1's player
//     before placing the teams 2 and 3 (0x507e71, 0x507e97), as the
//     original does: a missing team 2 or 3 player gets PlaceTown(-1, ...),
//     which writes outside the player table and the world (random maps,
//     the only symmetric ones, have no start positions, so the game never
//     does this). No candidate: "...keine Kreisposition finden" and the
//     player is taken out (Player +0 = 0).
//   - no start position: up to 1,000,000 attempts at random positions
//     (two CGameData::Random calls, x first: (r & 0xffff) * span / 65536 +
//     margin, signed; span size - 31 / margin 16 for symmetry 0 and 3, size
//     - 63 / 32 for 1 and 2) that pass IsGoodStartPosition(player, x, y,
//     attempt), the mirror distances and the mirror players' positions; the
//     towns are placed after a second IsGoodStartPosition call on each
//     position (it leaves the levelling plan PlaceTown uses). Mirrors: 1
//     (y, x); 2 (size - y - 1, size - x - 1); 3 (y, x), (size - y - 1,
//     size - x - 1) and (size - x - 1, size - y - 1) for teams 1, 2, 3.
//     DebugLog "Position gefunden" (no mirror player), "Mirror-",
//     "Triple-Mirror-" or "Quad-Mirror Position gefunden nach %u Versuchen".
//     No position: "...keine Zufallsposition finden", Player +0 = 0.
//   Symmetry 2 with a start position uses the mirror (y, x) like symmetry 1.
// All thiscall (ECX = world).

#include "game/debug_log.h"
#include "game/enums/buildings.h"
#include "game/enums/goods.h"
#include "game/enums/job_ids.h"
#include "game/enums/settler_types.h"
#include "game/globals.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/ui/game_settings_layout.h"
#include "game/world.h"
#include "hooks.h"

#include <cstdint>

namespace s3re::game {

namespace {

using layout::Building;
using layout::Player;
using layout::Settler;
using layout::Tile;

using B = BuildingType;
using G = Good;
using S = SettlerType;

// The callees, at their S3.EXE addresses (all replaced; called there so
// that tests and the emulator can probe them).
constexpr std::uintptr_t kStartGoods = 0x00412300;           // GameSettings, () -> level
constexpr std::uintptr_t kBuildingDone = 0x004dbc50;         // (building)
constexpr std::uintptr_t kAddBuildingSite = 0x004dd280;      // (x, y, player, type) -> building
constexpr std::uintptr_t kDropGoodNear = 0x005046b0;         // (good, amount, x, y)
constexpr std::uintptr_t kCreateSettler = 0x0050c9e0;        // (x, y, type, player) -> settler
constexpr std::uintptr_t kSpawnSettlersAround = 0x0050cb20;  // (count, type, player, x, y)
constexpr std::uintptr_t kUpdateShadeAround = 0x00519670;    // (x, y)
constexpr std::uintptr_t kRecalculateTerritory = 0x00519a90; // (x, y)
constexpr std::uintptr_t kBlockTile = 0x0051b6f0;            // (x, y)
constexpr std::uintptr_t kWearPath = 0x0051b7f0;             // (x, y)
constexpr std::uintptr_t kRandom = 0x004feb70;               // () -> 0..0x7fff
constexpr std::uintptr_t kTileDistance = 0x004ff320;         // stdcall (x1, y1, x2, y2), ECX = world
constexpr std::uintptr_t kFindPlayerByTeam = 0x00507f10;     // FindPlayerByField0C (team) -> player or -1
constexpr std::uintptr_t kIsGoodStartPosition = 0x005081a0;  // (player, x, y, attempt)
constexpr std::uintptr_t kIsFreeSmallTowerSite = 0x00508420; // (player, x, y)
constexpr std::uintptr_t kPlaceTown = 0x00506cf0;            // (player, x, y)

// thiscall with dword arguments.
template <std::uintptr_t Address, typename Self, typename... Args>
std::uint32_t Call(Self* self, Args... args) {
    using Fn = std::uint32_t(__fastcall*)(Self*, void*, decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(Address)(self, nullptr, static_cast<std::uint32_t>(args)...);
}

// TileDistance: stdcall, called with ECX = world. A __fastcall with the
// world in ECX and the four values on the stack (callee-popped) is the same
// call.
std::uint32_t Distance(CGameData* world, std::uint32_t x1, std::uint32_t y1, std::uint32_t x2, std::uint32_t y2) {
    using Fn = std::uint32_t(__fastcall*)(CGameData*, void*, std::uint32_t, std::uint32_t, std::uint32_t,
                                          std::uint32_t);
    return reinterpret_cast<Fn>(kTileDistance)(world, nullptr, x1, y1, x2, y2);
}

// The start building: a small tower (the castle would be 18).
constexpr B kStartBuilding = B::SmallTower;
constexpr std::uint32_t kLayoutTypes = kBuildingTypeCount;

// Building layout markers (building_sites.cpp).
constexpr std::uint32_t kMarkFootprint = 0x80000000;
constexpr std::uint32_t kMarkNoStakes = 0x80000001;
constexpr std::uint32_t kMarkBorder = 0x80000002;
constexpr std::uint32_t kMarkEnd = 0x80000003;

// The levelling window (footprint_scratch[0]): 31 x 31 heights around the
// building, cell i * 31 + j for tile (x - 15 + i, y - 15 + j).
constexpr std::int32_t kGrid = 31;
constexpr std::int32_t kGridCentre = 15;

constexpr std::uint16_t kNoGarrison = 0xffff;
constexpr std::uint32_t kComputerPlayer = 2;  // Player::active

// GameSettings::StartGoods.
constexpr std::uint32_t kGoodsLow = 1;
constexpr std::uint32_t kGoodsMedium = 2;
constexpr std::uint32_t kGoodsMapOwn = 0;  // the map's own setting; placed like medium

struct SettlerGroup {
    std::uint32_t count;
    S type;
    std::int32_t dx, dy;
};

struct GoodsDrop {
    G good;
    std::uint32_t amount;
};

// 0x506f68..0x507055: low start goods.
constexpr SettlerGroup kLowSettlers[] = {
    {1, S::Smith, -8, -16},   {2, S::Miner, -8, -16},   {16, S::Carrier, -8, -16},
    {6, S::Swordsman1, 8, 16}, {1, S::Bowman1, 14, 10}, {1, S::Pikeman1, -4, 10},
};
constexpr GoodsDrop kLowGoods[] = {
    {G::Board, 8}, {G::Board, 4}, {G::Stone, 8}, {G::Stone, 4}, {G::Shovel, 5},
    {G::Hammer, 6}, {G::Axe, 3}, {G::Pickaxe, 2}, {G::Saw, 1},
};

// 0x5072af..0x507461: medium start goods (also the map's own setting).
constexpr SettlerGroup kMediumSettlers[] = {
    {2, S::Smith, -8, -16},     {4, S::Miner, -8, -16},  {32, S::Carrier, -8, -16},
    {10, S::Swordsman1, 8, 16}, {2, S::Bowman1, 14, 10}, {2, S::Pikeman1, -4, 10},
};
constexpr GoodsDrop kMediumGoods[] = {
    {G::Board, 8},  {G::Board, 7},  {G::Board, 5},      {G::Board, 5},  {G::Stone, 8},   {G::Stone, 5},
    {G::Stone, 7},  {G::Stone, 5},  {G::Shovel, 6},     {G::Shovel, 4}, {G::Hammer, 8},  {G::Hammer, 7},
    {G::Axe, 6},    {G::Pickaxe, 4}, {G::Saw, 2},       {G::FishingRod, 1}, {G::Scythe, 1}, {G::Fish, 3},
    {G::Bread, 7},  {G::Meat, 5},   {G::Coal, 8},       {G::Coal, 7},   {G::IronOre, 5},
};
constexpr SettlerGroup kMediumDonkeys = {4, S::Donkey, 3, 6};

// 0x50706b..0x5072aa: high start goods.
constexpr SettlerGroup kHighSettlers[] = {
    {3, S::Smith, -8, -16},     {6, S::Miner, -8, -16},  {50, S::Carrier, -8, -16},
    {12, S::Swordsman1, 8, 16}, {4, S::Bowman1, 14, 10}, {4, S::Pikeman1, -4, 10},
};
constexpr GoodsDrop kHighGoods[] = {
    {G::Board, 8},   {G::Board, 7},    {G::Board, 5},   {G::Board, 6},      {G::Board, 4},   {G::Stone, 8},
    {G::Stone, 5},   {G::Stone, 7},    {G::Stone, 6},   {G::Stone, 5},      {G::Shovel, 8},  {G::Shovel, 5},
    {G::Shovel, 2},  {G::Hammer, 8},   {G::Hammer, 7},  {G::Hammer, 6},     {G::Hammer, 4},  {G::Axe, 8},
    {G::Pickaxe, 5}, {G::Saw, 3},      {G::FishingRod, 2}, {G::Scythe, 3},  {G::Coal, 8},    {G::Coal, 7},
    {G::Coal, 6},    {G::Coal, 5},     {G::IronOre, 7}, {G::IronOre, 5},    {G::Fish, 3},    {G::Bread, 7},
    {G::Meat, 5},    {G::Fish, 4},     {G::Bread, 8},   {G::Meat, 3},
};
constexpr SettlerGroup kHighDonkeys = {6, S::Donkey, 3, 6};

// 0x507473: a computer player's extra soldiers.
constexpr SettlerGroup kComputerSoldiers = {10, S::Swordsman1, 8, 16};

void Spawn(CGameData* world, std::uint32_t player, std::uint32_t x, std::uint32_t y, const SettlerGroup& group) {
    Call<kSpawnSettlersAround>(world, group.count, Value(group.type), player, x + group.dx, y + group.dy);
}

template <std::size_t SettlerCount, std::size_t GoodsCount>
void PlaceStartGoods(CGameData* world, std::uint32_t player, std::uint32_t x, std::uint32_t y,
                     const SettlerGroup (&settlers)[SettlerCount], const GoodsDrop (&goods)[GoodsCount]) {
    for (const SettlerGroup& group : settlers) {
        Spawn(world, player, x, y, group);
    }
    for (const GoodsDrop& drop : goods) {
        Call<kDropGoodNear>(world, Value(drop.good), drop.amount, x, y);
    }
}

// 0x506d43..0x506e38: the tower's footprint becomes a building's ground:
// the first part is blocked and loses its landscape objects, the rest only
// gets the footprint flag; all of it is walked on.
void MarkStartFootprint(CGameData* world, std::uint32_t race, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t* p = g_buildingLayouts[race * kLayoutTypes + Value(kStartBuilding)];
    while (*p++ != kMarkFootprint) {
    }
    for (; *p != kMarkNoStakes; p += 2) {
        const std::uint32_t tx = p[0] + x;
        const std::uint32_t ty = p[1] + y;
        TileAtOffset(world, TileOffset(tx, ty)).flags |= layout::kTileBuildingFootprint;
        Call<kBlockTile>(world, tx, ty);
        TileAtOffset(world, TileOffset(tx, ty)).object = 0;
        Call<kWearPath>(world, tx, ty);
    }
    ++p;
    while (*p != kMarkEnd) {
        if (*p == kMarkBorder) {
            ++p;  // the pair after it is read without an end check (0x506df1)
        }
        const std::uint32_t tx = p[0] + x;
        const std::uint32_t ty = p[1] + y;
        TileAtOffset(world, TileOffset(tx, ty)).flags |= layout::kTileBuildingFootprint;
        Call<kWearPath>(world, tx, ty);
        p += 2;
    }
}

// 0x506e3a..0x506eba: the levelled heights from the window, then the
// shading around every tile of it.
void ApplyLevelledHeights(CGameData* world, std::uint32_t x, std::uint32_t y) {
    for (std::int32_t j = 0; j < kGrid; ++j) {
        for (std::int32_t i = 0; i < kGrid; ++i) {
            TileAtOffset(world, TileOffset(x - kGridCentre + i, y - kGridCentre + j)).height =
                world->footprint_scratch[0][i * kGrid + j];
        }
    }
    for (std::int32_t j = 0; j < kGrid; ++j) {
        for (std::int32_t i = 0; i < kGrid; ++i) {
            Call<kUpdateShadeAround>(world, x + i - kGridCentre, y + j - kGridCentre);
        }
    }
}

// 0x506eed..0x506f4e: the first soldier sits in the tower.
void GarrisonStartTower(CGameData* world, std::uint32_t building, std::uint32_t player, std::uint32_t x,
                        std::uint32_t y) {
    const std::uint32_t settler = Call<kCreateSettler>(world, x, y, Value(S::Swordsman1), player);
    world->buildings[building].garrison = static_cast<std::uint16_t>(settler);
    Settler& soldier = world->settlers[settler];
    soldier.next = 0;
    soldier.job = SettlerJob::SwordsmanInTower;
    soldier.building = static_cast<std::uint16_t>(building);
    TileAtOffset(world, TileOffset(soldier.x, soldier.y)).settler = 0;
    soldier.counter = 0;
}

}  // namespace

using PlaceTownFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t player, std::uint32_t x,
                                               std::uint32_t y);

// Purpose: Builds a player's start town at a start position: the first tower with its soldier, the start
// settlers and the start goods (by the game's start-goods setting).
std::uint32_t __fastcall CGameData_PlaceTown(CGameData* world, void*, std::uint32_t player, std::uint32_t x,
                                             std::uint32_t y) {
    GameDebugLog(texts::kPlacingTown, player, x, y);
    const std::uint32_t building = Call<kAddBuildingSite>(world, x, y, player, Value(kStartBuilding));
    Player& record = world->players[player];
    MarkStartFootprint(world, static_cast<std::uint32_t>(record.race), x, y);
    ApplyLevelledHeights(world, x, y);
    Call<kBuildingDone>(world, building);
    world->buildings[building].garrison = kNoGarrison;
    Call<kRecalculateTerritory>(world, x, y);
    GarrisonStartTower(world, building, player, x, y);

    const std::uint32_t level = Call<kStartGoods>(&g_gameSettings);
    if (level == kGoodsLow) {
        PlaceStartGoods(world, player, x, y, kLowSettlers, kLowGoods);
    } else if (level == kGoodsMapOwn || level == kGoodsMedium) {
        PlaceStartGoods(world, player, x, y, kMediumSettlers, kMediumGoods);
        Spawn(world, player, x, y, kMediumDonkeys);
    } else {
        PlaceStartGoods(world, player, x, y, kHighSettlers, kHighGoods);
        Spawn(world, player, x, y, kHighDonkeys);
    }
    if (static_cast<std::uint32_t>(record.active) == kComputerPlayer) {
        Spawn(world, player, x, y, kComputerSoldiers);
    }
    record.unk018 = 1;
    record.start_x = static_cast<std::int32_t>(x);
    record.start_y = static_cast<std::int32_t>(y);
    return 0;  // the caller doesn't use EAX
}

namespace {

constexpr std::uint32_t kPlayers = 20;
constexpr std::int32_t kNoStartPosition = -1;
constexpr std::uint32_t kNoPlayer = 0xffffffff;  // FindPlayerByField0C: none
constexpr std::uint32_t kRandomAttempts = 1000000;
constexpr std::uint32_t kCircleEntries = 10000;  // offset entries tried around a given start position
constexpr std::uint32_t kMirrorDistance = 60;    // a town and its mirror image must be further apart

// The mirror teams (Player::team): FindPlayerByField0C(1..3) finds the
// player that gets the first, second and third mirror image.
constexpr std::uint32_t kMirrorTeam1 = 1;
constexpr std::uint32_t kMirrorTeam2 = 2;
constexpr std::uint32_t kMirrorTeam3 = 3;

// world+0x2d274, the random map's symmetry.
constexpr std::int32_t kSymmetryNone = 0;
constexpr std::int32_t kSymmetryDiagonal = 1;      // (x, y) <-> (y, x)
constexpr std::int32_t kSymmetryAntiDiagonal = 2;  // (x, y) <-> (size - y - 1, size - x - 1)
constexpr std::int32_t kSymmetryFour = 3;          // both, and their combination

// Random positions: margin + (r & 0xffff) * (size - 2 * margin + 1) / 65536.
constexpr std::int32_t kSmallMargin = 16;  // span size - 31
constexpr std::int32_t kLargeMargin = 32;  // span size - 63

struct Position {
    std::uint32_t x, y;
};

std::uint32_t FindMirrorPlayer(CGameData* world, std::uint32_t team) {
    return Call<kFindPlayerByTeam>(world, team);
}

bool IsGoodStart(CGameData* world, std::uint32_t player, Position at, std::uint32_t attempt) {
    return Call<kIsGoodStartPosition>(world, player, at.x, at.y, attempt) != 0;
}

bool IsFreeSite(CGameData* world, std::uint32_t player, Position at) {
    return Call<kIsFreeSmallTowerSite>(world, player, at.x, at.y) != 0;
}

void PlaceTownAt(CGameData* world, std::uint32_t player, Position at) {
    Call<kPlaceTown>(world, player, at.x, at.y);
}

// 0x50753c..0x507565: one random coordinate (signed arithmetic as the
// original's imul / cdq / sar).
std::uint32_t RandomCoordinate(CGameData* world, std::int32_t margin) {
    const std::uint32_t r = Call<kRandom>(world) & 0xffff;
    const std::int32_t span = static_cast<std::int32_t>(world->map_size) - 2 * margin + 1;
    const auto product = static_cast<std::int32_t>(r * static_cast<std::uint32_t>(span));
    return static_cast<std::uint32_t>(product / 65536 + margin);
}

Position RandomPosition(CGameData* world, std::int32_t margin) {
    const std::uint32_t x = RandomCoordinate(world, margin);
    const std::uint32_t y = RandomCoordinate(world, margin);
    return {x, y};
}

Position Swapped(Position at) {
    return {at.y, at.x};
}

// (size - y - 1, size - x - 1).
Position AntiDiagonal(CGameData* world, Position at) {
    return {world->map_size - at.y - 1, world->map_size - at.x - 1};
}

bool FarFromMirror(CGameData* world, Position at, Position mirror) {
    return Distance(world, at.x, at.y, mirror.x, mirror.y) > kMirrorDistance;
}

// Places the players with their positions, each after a second
// IsGoodStartPosition on its position (that leaves the levelling plan
// PlaceTown uses).
void PlaceRandomTowns(CGameData* world, std::uint32_t attempt, const std::uint32_t* players, const Position* at,
                      int count) {
    for (int k = 0; k < count; ++k) {
        IsGoodStart(world, players[k], at[k], attempt);
        PlaceTownAt(world, players[k], at[k]);
    }
}

// 0x50752e..0x5075d1 (symmetry 0), 0x50766b..0x507793 (1), 0x50786b..0x5079e6
// (2), 0x507abe..0x507d77 (3): random start positions. Returns false when
// none was found.
bool PlaceAtRandomPosition(CGameData* world, std::uint32_t player, std::int32_t symmetry) {
    const std::int32_t margin = symmetry == kSymmetryNone || symmetry == kSymmetryFour ? kSmallMargin : kLargeMargin;
    for (std::uint32_t attempt = 0; attempt != kRandomAttempts; ++attempt) {
        const Position at = RandomPosition(world, margin);
        if (symmetry == kSymmetryNone) {
            if (!IsGoodStart(world, player, at, attempt)) {
                continue;
            }
            GameDebugLog(texts::kPositionFound, attempt);
            PlaceTownAt(world, player, at);
            return true;
        }
        // The mirror images, in the order the original checks their distances.
        const Position second = symmetry == kSymmetryAntiDiagonal ? AntiDiagonal(world, at) : Swapped(at);
        const Position third = AntiDiagonal(world, at);
        const Position fourth = Swapped(third);
        if (!FarFromMirror(world, at, second)) {
            continue;
        }
        if (symmetry == kSymmetryFour && !FarFromMirror(world, at, third)) {
            continue;
        }
        if (!IsGoodStart(world, player, at, attempt)) {
            continue;
        }
        std::uint32_t players[4] = {player};
        Position positions[4] = {at};
        int count = 1;
        const std::uint32_t team1 = FindMirrorPlayer(world, kMirrorTeam1);
        if (team1 == kNoPlayer) {
            GameDebugLog(texts::kPositionFound, attempt);
            PlaceRandomTowns(world, attempt, players, positions, count);
            return true;
        }
        if (!IsGoodStart(world, team1, second, attempt)) {
            continue;
        }
        players[count] = team1;
        positions[count++] = second;
        if (symmetry != kSymmetryFour) {
            GameDebugLog(texts::kMirrorPositionFound, attempt);
            PlaceRandomTowns(world, attempt, players, positions, count);
            return true;
        }
        const std::uint32_t team2 = FindMirrorPlayer(world, kMirrorTeam2);
        if (team2 == kNoPlayer) {
            GameDebugLog(texts::kMirrorPositionFound, attempt);
            PlaceRandomTowns(world, attempt, players, positions, count);
            return true;
        }
        if (!IsGoodStart(world, team2, third, attempt)) {
            continue;
        }
        players[count] = team2;
        positions[count++] = third;
        const std::uint32_t team3 = FindMirrorPlayer(world, kMirrorTeam3);
        if (team3 == kNoPlayer) {
            GameDebugLog(texts::kTripleMirrorPositionFound, attempt);
            PlaceRandomTowns(world, attempt, players, positions, count);
            return true;
        }
        if (!IsGoodStart(world, team3, fourth, attempt)) {
            continue;
        }
        players[count] = team3;
        positions[count++] = fourth;
        GameDebugLog(texts::kQuadMirrorPositionFound, attempt);
        PlaceRandomTowns(world, attempt, players, positions, count);
        return true;
    }
    return false;
}

// A town near a given start position, or its mirror player's town:
// IsFreeSmallTowerSite once more (its result ignored), then PlaceTown.
void PlaceCheckedTown(CGameData* world, std::uint32_t player, Position at) {
    IsFreeSite(world, player, at);
    PlaceTownAt(world, player, at);
}

// 0x5075d6..0x50764f (symmetry 0), 0x5077ae..0x50784f (1), 0x5079eb..0x507aa2
// (2), 0x507d7c..0x507ebe (3): a site near the given start position.
// Returns false when none was found.
bool PlaceNearStartPosition(CGameData* world, std::uint32_t player, std::int32_t symmetry, Position start) {
    for (std::uint32_t k = 0; k != kCircleEntries; ++k) {
        const layout::MapOffset& offset = world->offsets[k];
        const Position at = {start.x + offset.dx, start.y + offset.dy};
        if (symmetry == kSymmetryNone) {
            if (!IsFreeSite(world, player, at)) {
                continue;
            }
            PlaceTownAt(world, player, at);
            return true;
        }
        // Symmetry 2 uses the diagonal mirror here too.
        const Position swapped = Swapped(at);
        const Position third = AntiDiagonal(world, at);
        if (!FarFromMirror(world, at, swapped)) {
            continue;
        }
        if (symmetry == kSymmetryFour && !FarFromMirror(world, at, third)) {
            continue;
        }
        if (!IsFreeSite(world, player, at)) {
            continue;
        }
        PlaceCheckedTown(world, player, at);
        const std::uint32_t team1 = FindMirrorPlayer(world, kMirrorTeam1);
        if (team1 != kNoPlayer) {
            PlaceCheckedTown(world, team1, swapped);
        }
        if (symmetry == kSymmetryFour) {
            // The teams 2 and 3 are placed whenever team 1's player exists (0x507e71, 0x507e97: the
            // original checks team 1's result, not their own).
            const std::uint32_t team2 = FindMirrorPlayer(world, kMirrorTeam2);
            if (team1 != kNoPlayer) {
                PlaceCheckedTown(world, team2, third);
            }
            const std::uint32_t team3 = FindMirrorPlayer(world, kMirrorTeam3);
            if (team1 != kNoPlayer) {
                PlaceCheckedTown(world, team3, Swapped(third));
            }
        }
        return true;
    }
    return false;
}

}  // namespace

using AddPlayerTownsFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx);

// Purpose: Gives every player of a map without buildings a start town: at its given start position or a
// random good one, mirrored for the other teams on a symmetric random map.
std::uint32_t __fastcall CGameData_AddPlayerTowns(CGameData* world, void*) {
    for (std::uint32_t player = 0; player < kPlayers; ++player) {
        Player& record = world->players[player];
        if (record.active == 0 || record.unk018 != 0) {  // +0x18: the town is placed
            continue;
        }
        GameDebugLog(texts::kPlacingPlayer, player);
        const std::int32_t symmetry = world->random_map_symmetry;
        if (static_cast<std::uint32_t>(symmetry) > static_cast<std::uint32_t>(kSymmetryFour)) {
            continue;
        }
        if (record.start_x == kNoStartPosition) {
            if (!PlaceAtRandomPosition(world, player, symmetry)) {
                GameDebugLog(texts::kNoRandomPosition);
                record.active = 0;
            }
        } else {
            const Position start = {static_cast<std::uint32_t>(record.start_x),
                                    static_cast<std::uint32_t>(record.start_y)};
            if (!PlaceNearStartPosition(world, player, symmetry, start)) {
                GameDebugLog(texts::kNoCirclePosition);
                record.active = 0;
            }
        }
    }
    return kPlayers;  // EAX: the loop counter
}

S3RE_HOOK(0x00506cf0, CGameData_PlaceTown, PlaceTownFn, Original_CGameData_PlaceTown, CGameData_PlaceTown);
S3RE_HOOK(0x005074b0, CGameData_AddPlayerTowns, AddPlayerTownsFn, Original_CGameData_AddPlayerTowns,
          CGameData_AddPlayerTowns);

}  // namespace s3re::game
