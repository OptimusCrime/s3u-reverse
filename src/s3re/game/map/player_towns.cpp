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

// thiscall with dword arguments.
template <std::uintptr_t Address, typename Self, typename... Args>
std::uint32_t Call(Self* self, Args... args) {
    using Fn = std::uint32_t(__fastcall*)(Self*, void*, decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(Address)(self, nullptr, static_cast<std::uint32_t>(args)...);
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

S3RE_HOOK(0x00506cf0, CGameData_PlaceTown, PlaceTownFn, Original_CGameData_PlaceTown, CGameData_PlaceTown);

}  // namespace s3re::game
