// The site lists of the CEvaLand's area records (Rainer\Cevaland.cpp;
// re/computer_players.md "The area site lists"): for every land grid cell
// (8 x 8 tiles) of an economy area, candidate places per building type,
// rated and kept in the record's site trees (AI::SiteTree, ordered by the
// rating's low byte; kSiteTreeOfType gives each type's tree).
//
//   0x00556630 CEvaLand::FillAreaSiteLists(AiArea* record, player)
//              thiscall, ret 8: empties all 39 trees (begin 0x559260 +
//              erase(first, last) 0x5593a0, tree 0 to 38), then visits the
//              grid cells (x outer, y inner, the grid's width / height read
//              once) whose centre tile the player owns (CellCentreOwner
//              0x554850, low words) and whose centre is in the record's
//              area (TileCentre 0x554880, AreaAlongRow 0x5996c0, low words)
//              and rates the cell's centre for every building type (see
//              RateResources and RateFromNeighbours). EAX: left over.
//   0x005579d0 CEvaLand::RefillAreaSiteList(player, area, type) thiscall,
//              ret 0xc: the record of `area` in lists[player] (none: return
//              at once); empties the type's tree (clear 0x5594d0 or begin +
//              erase, per type; types 0, 6, 15, 16, 18, 26..28, 35 none) and
//              rates the area's cells for that type only. It differs from
//              FillAreaSiteLists (quirks kept, see RefillCell): the mines use
//              CountPlayersResourcesAround instead of
//              CountResourceInOwnersLand, the waterworks' key is the count
//              instead of the stream tiles, a storehouse counts barracks
//              four times instead of twice, the charcoal burner, powder
//              maker and apiary are rated for every race, and the workshops
//              (types 31, 47, 48, 50) are rated into tree 34 (the large
//              temples') while tree 35 is the one emptied. EAX: left over.
//
// Both count, over the 3 x 3 cells around (inside the grid), the building
// types in the cells' per-player bitsets (AiGridCell::building_types,
// player & 0xffff); "military" = small towers + 2 * large towers + 3 *
// castles. A rating > 0 is inserted with its low byte as the key and the
// centre tile; the value's padding byte (+1) is an uninitialized stack
// byte in the original (ours 0).

#include "game/ai/ai_layout.h"
#include "game/debug_log.h"
#include "game/enums/buildings.h"
#include "game/enums/goods.h"
#include "game/enums/races.h"
#include "game/globals.h"
#include "game/layout.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/difftest.h"
#include "test/world_buffer.h"
#include "game/ai/ai_area_test_world.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

namespace s3re::game {

using namespace ai;

namespace {

using B = BuildingType;

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kGridCell = 0x00554700;                    // CEvaLand::GridCell (thiscall)
constexpr std::uintptr_t kCellCentreOwner = 0x00554850;             // AI::CellCentreOwner (stdcall)
constexpr std::uintptr_t kTileCentre = 0x00554880;                  // AI::TileCentre_554880 (stdcall)
constexpr std::uintptr_t kAreaAlongRow = 0x005996c0;                // AI::AreaAlongRow (fastcall)
constexpr std::uintptr_t kCountResourceInOwnersLand = 0x00599a30;   // (fastcall)
constexpr std::uintptr_t kCountPlayersResourcesAround = 0x00599f50;  // (fastcall, pointers)
constexpr std::uintptr_t kSiteTreeBegin = 0x00559260;
constexpr std::uintptr_t kSiteTreeInsert = 0x00559270;
constexpr std::uintptr_t kSiteTreeEraseRange = 0x005593a0;
constexpr std::uintptr_t kSiteTreeClear = 0x005594d0;

constexpr std::uint32_t kRichAround = 5;      // an amount in the 3 x 3 cells above this is worth a mine / cutter
constexpr std::uint8_t kGrainLandPercent = 20;  // farms: more grain land in the cell than this
constexpr std::uint8_t kWineLandPercent = 10;   // vineyards
constexpr std::uint8_t kRiceLandAround = 5;     // rice farms

using GridCellFn = const AiGridCell*(__fastcall*)(AiLandEvaluation* self, void* edx, std::int32_t x, std::int32_t y);
using CellOwnerFn = std::uint32_t(__stdcall*)(std::uint32_t x, std::uint32_t y);
using TileCentreFn = std::uint32_t(__stdcall*)(std::uint32_t x, std::uint32_t y, std::uint16_t* outX,
                                               std::uint16_t* outY);
using AreaAlongRowFn = std::uint32_t(__fastcall*)(std::uint32_t x, std::uint32_t y);
using CountInOwnersLandFn = std::uint32_t(__fastcall*)(std::uint32_t cellX, std::uint32_t cellY, std::uint32_t player,
                                                       std::uint32_t goods);
using CountAroundFn = std::uint32_t(__fastcall*)(const std::uint32_t* cellX, const std::uint32_t* cellY,
                                                 const std::uint32_t* player, const std::uint32_t* goods);
using BeginFn = SiteTreeNode**(__fastcall*)(SiteTree* self, void* edx, SiteTreeNode** out);
using InsertFn = SiteTreeInsertResult*(__fastcall*)(SiteTree* self, void* edx, SiteTreeInsertResult* out,
                                                    const SiteValue* value);
using EraseRangeFn = SiteTreeNode**(__fastcall*)(SiteTree* self, void* edx, SiteTreeNode** out, SiteTreeNode* first,
                                                 SiteTreeNode* last);
using ClearFn = SiteTreeNode**(__fastcall*)(SiteTree* self, void* edx);

SiteTree& TreeOf(AiArea& record, B type) {
    return record.site_trees[kSiteTreeOfType[Value(type)]];
}

// erase(begin(), end()) as the original calls it (the end read first).
void EraseAll(SiteTree& tree) {
    SiteTreeNode* const end = tree.head;
    SiteTreeNode* first;
    SiteTreeNode* next;
    reinterpret_cast<BeginFn>(kSiteTreeBegin)(&tree, nullptr, &first);
    reinterpret_cast<EraseRangeFn>(kSiteTreeEraseRange)(&tree, nullptr, &next, first, end);
}

// A grid cell of the area being rated.
struct CellScan {
    AiLandEvaluation* land;
    std::uint32_t player;  // the argument as passed (CountResourceInOwnersLand gets all 32 bits)
    std::int32_t cellX, cellY;
    std::uint16_t x, y;    // the cell's centre tile
    const AiGridCell* cell;
};

void Insert(SiteTree& tree, const CellScan& scan, std::uint8_t key) {
    const SiteValue value{key, 0, scan.x, scan.y};
    SiteTreeInsertResult result;
    reinterpret_cast<InsertFn>(kSiteTreeInsert)(&tree, nullptr, &result, &value);
}

// A rating > 0 goes in with its low byte as the key.
void InsertRating(SiteTree& tree, const CellScan& scan, std::int32_t rating) {
    if (rating > 0) {
        Insert(tree, scan, static_cast<std::uint8_t>(rating));
    }
}

std::uint32_t CountInOwnersLand(const CellScan& scan, Good goods) {
    return reinterpret_cast<CountInOwnersLandFn>(kCountResourceInOwnersLand)(
        static_cast<std::uint32_t>(scan.cellX), static_cast<std::uint32_t>(scan.cellY), scan.player,
        static_cast<std::uint32_t>(Value(goods)));
}

std::uint32_t CountAround(const CellScan& scan, Good goods) {
    const auto x = static_cast<std::uint32_t>(scan.cellX);
    const auto y = static_cast<std::uint32_t>(scan.cellY);
    const auto kind = static_cast<std::uint32_t>(Value(goods));
    return reinterpret_cast<CountAroundFn>(kCountPlayersResourcesAround)(&x, &y, &scan.player, &kind);
}

// A count (the callees' low word) that isn't 0 goes in with its low byte.
void InsertCount(SiteTree& tree, const CellScan& scan, std::uint32_t count) {
    if (static_cast<std::uint16_t>(count) != 0) {
        Insert(tree, scan, static_cast<std::uint8_t>(count));
    }
}

std::uint16_t RaceOfPlayer(std::uint32_t player) {
    // The low word of Player::race (a word read).
    return static_cast<std::uint16_t>(g_aiWorld.get()->players[player & 0xffff].race);
}

bool RaceIs(std::uint16_t race, Race which) {
    return race == static_cast<std::uint16_t>(Value(which));
}

// The building types in the 3 x 3 cells around (0x557229..0x5573ed in
// FillAreaSiteLists): per type the number of cells whose bitset has it.
struct NearbyBuildings {
    std::int32_t count[64] = {};
    std::int32_t military = 0;  // small towers + 2 * large towers + 3 * castles
    std::int32_t operator[](B type) const { return count[Value(type)]; }
};

NearbyBuildings CountNearbyBuildings(const CellScan& scan) {
    NearbyBuildings nearby;
    const auto width = static_cast<std::int32_t>(scan.land->width);
    const auto height = static_cast<std::int32_t>(scan.land->height);
    for (std::int32_t i = 0; i < 3; ++i) {
        const auto x = static_cast<std::int16_t>(scan.cellX - 1 + i);
        for (std::int32_t j = 0; j < 3; ++j) {
            const auto y = static_cast<std::int16_t>(scan.cellY - 1 + j);
            if (x < 0 || x >= width || y < 0 || y >= height) {
                continue;
            }
            const AiGridCell* cell = reinterpret_cast<GridCellFn>(kGridCell)(scan.land, nullptr, x, y);
            // The player's bitset<57> (not range-checked: player & 0xffff).
            const std::uint32_t* bits = &cell->building_types[0][0] + 2 * (scan.player & 0xffff);
            for (std::uint32_t type = 0; type < 64; ++type) {
                if ((bits[type / 32] >> (type % 32)) & 1) {
                    ++nearby.count[type];
                }
            }
        }
    }
    nearby.military = nearby[B::SmallTower] + 2 * nearby[B::LargeTower] + 3 * nearby[B::Castle];
    return nearby;
}

// ---- the ratings from the buildings around (both functions)

std::int32_t SawmillRating(const NearbyBuildings& n) {
    return n.military + (n[B::Woodcutter] * 4 - n[B::Sawmill]) * 2;
}

// Foresters keep away from farms, the iron industry and coal mines.
std::int32_t ForesterRating(const NearbyBuildings& n) {
    std::int32_t s = n[B::Woodcutter] - n[B::Farm];
    s = s * 2 - n[B::IronSmelter];
    s = s * 2 - n[B::IronMine] - n[B::CoalMine];
    s = s * 2 - n[B::Forester];
    return n.military + s * 2;
}

std::int32_t IronSmelterRating(const NearbyBuildings& n) {
    return n.military + (n[B::IronMine] + n[B::CoalMine]) * 8;
}

std::int32_t SmithRating(const NearbyBuildings& n) {  // toolsmith and weaponsmith
    return n.military + (n[B::IronSmelter] + n[B::CoalMine]) * 8;
}

std::int32_t GoldSmelterRating(const NearbyBuildings& n) {
    return n.military + (n[B::GoldMine] + n[B::CoalMine]) * 8;
}

std::int32_t MillRating(const NearbyBuildings& n) {
    return n.military + (n[B::Bakery] + n[B::Farm]) * 8;
}

std::int32_t BakeryRating(const NearbyBuildings& n) {
    return n.military + (n[B::Farm] + (n[B::Waterworks] + n[B::Mill] * 2 + n[B::CoalMine]) * 2) * 2;
}

std::int32_t PigFarmRating(const NearbyBuildings& n) {
    return n.military + (n[B::Waterworks] + n[B::Farm] * 2) * 4;
}

std::int32_t SlaughterhouseRating(const NearbyBuildings& n) {
    return n.military + n[B::PigFarm] * 8;
}

std::int32_t BarracksRating(const NearbyBuildings& n) {
    return n.military + (n[B::IronSmelter] + n[B::Weaponsmith] * 2) * 4;
}

std::int32_t DonkeyFarmRating(const NearbyBuildings& n) {
    return n.military + (n[B::Farm] + n[B::Waterworks]) * 4;
}

std::int32_t ShipyardRating(const NearbyBuildings& n) {
    return n.military + n[B::Harbour] * 4;
}

std::int32_t WorkshopRating(const NearbyBuildings& n) {  // catapult / ballista / cannon workshops, gong
    return n.military + n[B::Sawmill] + n[B::IronSmelter];
}

std::int32_t CharcoalBurnerRating(const NearbyBuildings& n) {
    return n.military + n[B::Sawmill] * 4;
}

std::int32_t PowderMakerRating(const NearbyBuildings& n) {
    return n.military + (n[B::SulfurMine] + n[B::CoalMine]) * 4;
}

std::int32_t ApiaryRating(const NearbyBuildings& n) {
    return n.military + ((n[B::Forester] * 4 - n[B::Woodcutter]) + n[B::Meadery]) * 2;
}

// The race's drink (brewery, distillery, meadery; tree 31); Romans none.
void InsertDrinkRating(AiArea& record, const CellScan& scan, const NearbyBuildings& n, std::uint16_t race) {
    std::int32_t suppliers;
    if (RaceIs(race, Race::Egyptian)) {
        suppliers = n[B::Farm] + n[B::Waterworks] * 2;
    } else if (RaceIs(race, Race::Asian)) {
        suppliers = n[B::CoalMine] + n[B::RiceFarm] * 2;
    } else if (RaceIs(race, Race::Amazon)) {
        suppliers = n[B::Waterworks] + n[B::Apiary] * 2;
    } else {
        return;
    }
    InsertRating(TreeOf(record, B::Distillery), scan, n.military + suppliers * 4);
}

// ---- FillAreaSiteLists

// 0x556e3d..0x5571c5: the cell's own resources (the trees in this order).
void RateResources(AiArea& record, const CellScan& scan, std::uint16_t race) {
    const AiGridCell& cell = *scan.cell;
    if (cell.trees_around > kRichAround) {
        InsertCount(TreeOf(record, B::Woodcutter), scan, CountAround(scan, Good::Log));
    }
    if (cell.stones_around > kRichAround) {
        InsertCount(TreeOf(record, B::Stonecutter), scan, CountAround(scan, Good::Stone));
    }
    if (cell.resources_around[kGridIron] > kRichAround) {
        InsertCount(TreeOf(record, B::IronMine), scan, CountInOwnersLand(scan, Good::IronOre));
    }
    if (cell.resources_around[kGridCoal] > kRichAround) {
        InsertCount(TreeOf(record, B::CoalMine), scan, CountInOwnersLand(scan, Good::Coal));
    }
    if (cell.resources_around[kGridGold] > kRichAround) {
        InsertCount(TreeOf(record, B::GoldMine), scan, CountInOwnersLand(scan, Good::GoldOre));
    }
    if (RaceIs(race, Race::Asian)) {
        if (cell.resources_around[kGridSulfur] > kRichAround) {
            InsertCount(TreeOf(record, B::SulfurMine), scan, CountInOwnersLand(scan, Good::Sulfur));
        }
    } else if (RaceIs(race, Race::Egyptian)) {
        if (cell.resources_around[kGridGems] > kRichAround) {
            InsertCount(TreeOf(record, B::GemMine), scan, CountInOwnersLand(scan, Good::Gems));
        }
    }
    if (cell.resources_around[kGridFish] > kRichAround && cell.resources[kGridFish] != 0) {
        Insert(TreeOf(record, B::Fishery), scan, static_cast<std::uint8_t>(cell.resources_around[kGridFish]));
    }
    if (cell.grain_land_percent > kGrainLandPercent) {
        Insert(TreeOf(record, B::Farm), scan, cell.grain_land_percent);
    }
    if (cell.rice_land_around > kRiceLandAround) {
        Insert(TreeOf(record, B::RiceFarm), scan, cell.rice_land_around);
    }
    if (cell.wine_land_percent > kWineLandPercent) {
        Insert(TreeOf(record, B::Vineyard), scan, cell.wine_land_percent);
    }
    // The key is the number of stream tiles (RefillAreaSiteList: the count).
    if (cell.stream_tiles != 0 && static_cast<std::uint16_t>(CountInOwnersLand(scan, Good::Water)) != 0) {
        Insert(TreeOf(record, B::Waterworks), scan, cell.stream_tiles);
    }
}

// 0x5573f3..0x55798d: the ratings from the buildings around.
void RateFromNeighbours(AiArea& record, const CellScan& scan, const NearbyBuildings& n, std::uint16_t race) {
    InsertRating(TreeOf(record, B::Sawmill), scan, SawmillRating(n));
    InsertRating(TreeOf(record, B::Forester), scan, ForesterRating(n));
    InsertRating(TreeOf(record, B::IronSmelter), scan, IronSmelterRating(n));
    const std::int32_t smith = SmithRating(n);
    InsertRating(TreeOf(record, B::Toolsmith), scan, smith);
    InsertRating(TreeOf(record, B::Weaponsmith), scan, smith);
    InsertRating(TreeOf(record, B::GoldSmelter), scan, GoldSmelterRating(n));
    InsertRating(TreeOf(record, B::Mill), scan, MillRating(n));
    InsertRating(TreeOf(record, B::Bakery), scan, BakeryRating(n));
    InsertRating(TreeOf(record, B::PigFarm), scan, PigFarmRating(n));
    InsertRating(TreeOf(record, B::Slaughterhouse), scan, SlaughterhouseRating(n));
    InsertRating(TreeOf(record, B::Barracks), scan, BarracksRating(n));
    InsertRating(TreeOf(record, B::DonkeyFarm), scan, DonkeyFarmRating(n));
    const std::int32_t nearBarracks = n.military + n[B::Barracks] * 2;
    InsertRating(TreeOf(record, B::Storehouse), scan, nearBarracks);
    InsertRating(TreeOf(record, B::Shipyard), scan, ShipyardRating(n));
    InsertRating(TreeOf(record, B::Marketplace), scan, n.military);
    InsertRating(TreeOf(record, B::Hospital), scan, nearBarracks);
    InsertDrinkRating(record, scan, n, race);
    InsertRating(TreeOf(record, B::SmallTemple), scan, n.military);
    InsertRating(TreeOf(record, B::LargeTemple), scan, n.military);
    InsertRating(TreeOf(record, B::CatapultWorkshop), scan, WorkshopRating(n));
    if (RaceIs(race, Race::Roman)) {
        InsertRating(TreeOf(record, B::CharcoalBurner), scan, CharcoalBurnerRating(n));
    } else if (RaceIs(race, Race::Asian)) {
        InsertRating(TreeOf(record, B::PowderMaker), scan, PowderMakerRating(n));
    } else if (RaceIs(race, Race::Amazon)) {
        InsertRating(TreeOf(record, B::Apiary), scan, ApiaryRating(n));
    }
}

// The cells of the record's area (0x556d89..0x5579b9): calls visit(scan)
// for each, in the original's order and with its callee calls.
template <typename Visit>
void ForEachAreaCell(AiLandEvaluation* land, std::uint32_t player, std::uint16_t area, Visit visit) {
    const auto width = static_cast<std::int32_t>(land->width);
    const auto height = static_cast<std::int32_t>(land->height);
    for (std::int32_t cellX = 0; cellX < width; ++cellX) {
        for (std::int32_t cellY = 0; cellY < height; ++cellY) {
            const auto owner = reinterpret_cast<CellOwnerFn>(kCellCentreOwner)(static_cast<std::uint32_t>(cellX),
                                                                               static_cast<std::uint32_t>(cellY));
            if (static_cast<std::uint16_t>(owner) != static_cast<std::uint16_t>(player)) {
                continue;
            }
            std::uint16_t x;
            std::uint16_t y;
            reinterpret_cast<TileCentreFn>(kTileCentre)(static_cast<std::uint32_t>(cellX),
                                                        static_cast<std::uint32_t>(cellY), &x, &y);
            if (static_cast<std::uint16_t>(reinterpret_cast<AreaAlongRowFn>(kAreaAlongRow)(x, y)) != area) {
                continue;
            }
            const AiGridCell* cell = reinterpret_cast<GridCellFn>(kGridCell)(land, nullptr, cellX, cellY);
            visit(CellScan{land, player, cellX, cellY, x, y, cell});
        }
    }
}

// ---- RefillAreaSiteList

// How RefillAreaSiteList empties a type's tree (jump table 0x558cac).
enum class Emptying : std::uint8_t { None, Clear, EraseAll };

Emptying EmptyingOf(std::uint32_t type) {
    switch (type) {
    case Value(B::Woodcutter): case Value(B::Stonecutter): case Value(B::Sawmill): case Value(B::Forester):
    case Value(B::CoalMine): case Value(B::GoldMine): case Value(B::IronMine): case Value(B::IronSmelter):
    case Value(B::Toolsmith): case Value(B::Vineyard): case Value(B::Farm): case Value(B::Fishery):
    case Value(B::SulfurMine): case Value(B::RiceFarm): case Value(B::GemMine): case Value(B::AmazonSulfurMine):
        return Emptying::Clear;
    case Value(B::None): case Value(B::LookoutTower): case Value(B::SmallTower): case Value(B::LargeTower):
    case Value(B::Castle): case Value(B::SmallResidence): case Value(B::MediumResidence):
    case Value(B::LargeResidence): case Value(B::Hospital):
        return Emptying::None;
    default:
        return type < kBuildingTypeCount ? Emptying::EraseAll : Emptying::None;
    }
}

// 0x557f40..0x5583c9: the type's own resources in the cell.
void RefillResources(AiArea& record, const CellScan& scan, std::uint32_t type) {
    const AiGridCell& cell = *scan.cell;
    switch (type) {
    case Value(B::Woodcutter):
        if (cell.trees_around > kRichAround) {
            InsertCount(TreeOf(record, B::Woodcutter), scan, CountAround(scan, Good::Log));
        }
        break;
    case Value(B::Stonecutter):
        if (cell.stones_around > kRichAround) {
            InsertCount(TreeOf(record, B::Stonecutter), scan, CountAround(scan, Good::Stone));
        }
        break;
    // The mines count the 3 x 3 cells (FillAreaSiteLists: the cell's owned
    // tiles).
    case Value(B::CoalMine):
        if (cell.resources_around[kGridCoal] > kRichAround) {
            InsertCount(TreeOf(record, B::CoalMine), scan, CountAround(scan, Good::Coal));
        }
        break;
    case Value(B::GoldMine):
        if (cell.resources_around[kGridGold] > kRichAround) {
            InsertCount(TreeOf(record, B::GoldMine), scan, CountAround(scan, Good::GoldOre));
        }
        break;
    case Value(B::IronMine):
        if (cell.resources_around[kGridIron] > kRichAround) {
            InsertCount(TreeOf(record, B::IronMine), scan, CountAround(scan, Good::IronOre));
        }
        break;
    case Value(B::Vineyard):
        if (cell.wine_land_percent > kWineLandPercent) {
            Insert(TreeOf(record, B::Vineyard), scan, cell.wine_land_percent);
        }
        break;
    case Value(B::Farm):
        if (cell.grain_land_percent > kGrainLandPercent) {
            Insert(TreeOf(record, B::Farm), scan, cell.grain_land_percent);
        }
        break;
    case Value(B::Fishery):
        if (cell.resources_around[kGridFish] > kRichAround && cell.resources[kGridFish] != 0) {
            Insert(TreeOf(record, B::Fishery), scan, static_cast<std::uint8_t>(cell.resources_around[kGridFish]));
        }
        break;
    case Value(B::SulfurMine):
        if (cell.resources_around[kGridSulfur] > kRichAround) {
            InsertCount(TreeOf(record, B::SulfurMine), scan, CountAround(scan, Good::Sulfur));
        }
        break;
    case Value(B::Waterworks):
        // The key is the count (FillAreaSiteLists: the stream tiles).
        if (cell.stream_tiles != 0) {
            InsertCount(TreeOf(record, B::Waterworks), scan, CountInOwnersLand(scan, Good::Water));
        }
        break;
    case Value(B::RiceFarm):
        if (cell.rice_land_around > kRiceLandAround) {
            Insert(TreeOf(record, B::RiceFarm), scan, cell.rice_land_around);
        }
        break;
    case Value(B::GemMine):
        if (cell.resources_around[kGridGems] > kRichAround) {
            InsertCount(TreeOf(record, B::GemMine), scan, CountAround(scan, Good::Gems));
        }
        break;
    default:
        break;
    }
}

// 0x558604..0x558c69: the type's rating from the buildings around.
void RefillFromNeighbours(AiArea& record, const CellScan& scan, const NearbyBuildings& n, std::uint32_t type,
                          std::uint16_t race) {
    switch (type) {
    case Value(B::Storehouse):
        // Barracks count four times (FillAreaSiteLists: twice).
        InsertRating(TreeOf(record, B::Storehouse), scan, n.military + n[B::Barracks] * 4);
        break;
    case Value(B::Sawmill): InsertRating(TreeOf(record, B::Sawmill), scan, SawmillRating(n)); break;
    case Value(B::Forester): InsertRating(TreeOf(record, B::Forester), scan, ForesterRating(n)); break;
    case Value(B::GoldSmelter): InsertRating(TreeOf(record, B::GoldSmelter), scan, GoldSmelterRating(n)); break;
    case Value(B::IronSmelter): InsertRating(TreeOf(record, B::IronSmelter), scan, IronSmelterRating(n)); break;
    case Value(B::Toolsmith): InsertRating(TreeOf(record, B::Toolsmith), scan, SmithRating(n)); break;
    case Value(B::Weaponsmith): InsertRating(TreeOf(record, B::Weaponsmith), scan, SmithRating(n)); break;
    case Value(B::Mill): InsertRating(TreeOf(record, B::Mill), scan, MillRating(n)); break;
    case Value(B::Barracks): InsertRating(TreeOf(record, B::Barracks), scan, BarracksRating(n)); break;
    case Value(B::Bakery): InsertRating(TreeOf(record, B::Bakery), scan, BakeryRating(n)); break;
    case Value(B::Slaughterhouse):
        InsertRating(TreeOf(record, B::Slaughterhouse), scan, SlaughterhouseRating(n));
        break;
    case Value(B::Distillery): case Value(B::Brewery): case Value(B::Meadery):
        InsertDrinkRating(record, scan, n, race);
        break;
    case Value(B::PigFarm): InsertRating(TreeOf(record, B::PigFarm), scan, PigFarmRating(n)); break;
    case Value(B::CatapultWorkshop): case Value(B::BallistaWorkshop): case Value(B::CannonWorkshop):
    case Value(B::LargeGong):
        // Into the large temples' tree (quirk kept; the workshops' tree 35
        // is the one emptied above).
        InsertRating(TreeOf(record, B::LargeTemple), scan, WorkshopRating(n));
        break;
    case Value(B::Shipyard): InsertRating(TreeOf(record, B::Shipyard), scan, ShipyardRating(n)); break;
    case Value(B::Marketplace): InsertRating(TreeOf(record, B::Marketplace), scan, n.military); break;
    case Value(B::Hospital):
        InsertRating(TreeOf(record, B::Hospital), scan, n.military + n[B::Barracks] * 2);
        break;
    // Rated for every race (FillAreaSiteLists: only the race's own).
    case Value(B::CharcoalBurner):
        InsertRating(TreeOf(record, B::CharcoalBurner), scan, CharcoalBurnerRating(n));
        break;
    case Value(B::PowderMaker): InsertRating(TreeOf(record, B::PowderMaker), scan, PowderMakerRating(n)); break;
    case Value(B::Apiary): InsertRating(TreeOf(record, B::Apiary), scan, ApiaryRating(n)); break;
    case Value(B::Pyramid): case Value(B::LargeTemple): case Value(B::LargePagoda): case Value(B::AmazonLargeTemple):
        InsertRating(TreeOf(record, B::LargeTemple), scan, n.military);
        break;
    case Value(B::Sphinx): case Value(B::SmallTemple): case Value(B::SmallPagoda): case Value(B::AmazonSmallTemple):
        InsertRating(TreeOf(record, B::SmallTemple), scan, n.military);
        break;
    case Value(B::DonkeyFarm): InsertRating(TreeOf(record, B::DonkeyFarm), scan, DonkeyFarmRating(n)); break;
    default:
        break;
    }
}

}  // namespace

using FillAreaSiteListsFn = void(__fastcall*)(AiLandEvaluation* self, void* edx, AiArea* record, std::uint32_t player);
using RefillAreaSiteListFn = void(__fastcall*)(AiLandEvaluation* self, void* edx, std::uint32_t player,
                                               std::uint32_t area, std::uint32_t type);

void __fastcall AI_CEvaLand_FillAreaSiteLists(AiLandEvaluation* self, void* edx, AiArea* record, std::uint32_t player);
void __fastcall AI_CEvaLand_RefillAreaSiteList(AiLandEvaluation* self, void* edx, std::uint32_t player,
                                               std::uint32_t area, std::uint32_t type);

S3RE_HOOK(0x00556630, AI_CEvaLand_FillAreaSiteLists, FillAreaSiteListsFn, Original_AI_CEvaLand_FillAreaSiteLists,
          AI_CEvaLand_FillAreaSiteLists);
S3RE_HOOK(0x005579d0, AI_CEvaLand_RefillAreaSiteList, RefillAreaSiteListFn, Original_AI_CEvaLand_RefillAreaSiteList,
          AI_CEvaLand_RefillAreaSiteList);

// Purpose: Rates every land cell of a computer player's economy area as a
// place for each building type and keeps the candidates per type, best
// first, in the area record (for a new area, and when an area changes
// owner).
void __fastcall AI_CEvaLand_FillAreaSiteLists(AiLandEvaluation* self, void* /*edx*/, AiArea* record,
                                              std::uint32_t player) {
    const std::uint16_t area = record->area;
    const std::uint16_t race = RaceOfPlayer(player);
    for (SiteTree& tree : record->site_trees) {
        EraseAll(tree);
    }
    ForEachAreaCell(self, player, area, [&](const CellScan& scan) {
        RateResources(*record, scan, race);
        RateFromNeighbours(*record, scan, CountNearbyBuildings(scan), race);
    });
}

// Purpose: Re-rates the cells of one economy area for one building type
// (after a building of a related type was placed there, the candidates
// change).
void __fastcall AI_CEvaLand_RefillAreaSiteList(AiLandEvaluation* self, void* /*edx*/, std::uint32_t player,
                                               std::uint32_t area, std::uint32_t type) {
    auto* const head = reinterpret_cast<AiAreaNode*>(self->lists[player & 0xffff].head);
    AiAreaNode* node = head->next;
    while (node != head && node->value.area != static_cast<std::uint16_t>(area)) {
        node = node->next;
    }
    if (node == head) {
        return;
    }
    AiArea& record = node->value;
    const std::uint16_t race = RaceOfPlayer(player);
    type &= 0xffff;
    switch (EmptyingOf(type)) {
    case Emptying::Clear:
        reinterpret_cast<ClearFn>(kSiteTreeClear)(&record.site_trees[kSiteTreeOfType[type]], nullptr);
        break;
    case Emptying::EraseAll:
        EraseAll(record.site_trees[kSiteTreeOfType[type]]);
        break;
    case Emptying::None:
        break;
    }
    ForEachAreaCell(self, player, static_cast<std::uint16_t>(area), [&](const CellScan& scan) {
        RefillResources(record, scan, type);
        RefillFromNeighbours(record, scan, CountNearbyBuildings(scan), type, race);
    });
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// A test map (up to 9 x 9 cells, map 72 tiles; tiles' owners and areas in
// patches, cells with random resource bytes, sums and per-player building
// bitsets); two CEvaLand objects whose lists[player] hold the same records
// (copies, with random site trees). The original runs on one, ours on the
// other; compared: every tree of every record in order (key, x, y and
// colour of each node; not the value's padding byte, see the header), the
// records' other bytes and the DebugLog calls. Players 0..2 of races 0..3
// (sometimes with high bits), areas 0..3, every building type and some
// above.

namespace {

using namespace ai_test;

// The records of a list: their bytes before the trees and every tree.
std::vector<std::uint8_t> ListContents(const AiList& list) {
    std::vector<std::uint8_t> out;
    const auto* head = reinterpret_cast<const AiAreaNode*>(list.head);
    for (const AiAreaNode* node = head->next; node != head; node = node->next) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&node->value);
        const std::size_t start = out.size();
        out.insert(out.end(), bytes, bytes + offsetof(AiArea, site_tree_of_type));
        // (the padding the copy constructor skips: heap leftovers)
        for (std::size_t pad : {offsetof(AiArea, unk002), offsetof(AiArea, unk002) + 1, offsetof(AiArea, unk015),
                                offsetof(AiArea, unk015) + 1, offsetof(AiArea, unk015) + 2, offsetof(AiArea, unk3e7)}) {
            out[start + pad] = 0;
        }
        for (const SiteTree& tree : node->value.site_trees) {
            const std::vector<std::uint8_t> values = TreeValues(tree);
            out.insert(out.end(), values.begin(), values.end());
        }
    }
    return out;
}

struct SiteListFns {
    FillAreaSiteListsFn fill;
    RefillAreaSiteListFn refill;
};

std::uint32_t RandomType(test::Rng& rng) {
    std::uint32_t type = rng.Chance(1, 20) ? rng.Below(80) : rng.Below(kBuildingTypeCount);
    return type | (rng.Chance(1, 5) ? rng.Next32() & 0xffff0000u : 0);
}

void CompareSiteLists(test::Context& ctx, const SiteListFns& ours, int cases, int* insertedOut = nullptr) {
    const SiteListFns original{Original_AI_CEvaLand_FillAreaSiteLists, Original_AI_CEvaLand_RefillAreaSiteList};
    SavedGlobals saved;
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    std::vector<AiGridCell> cells(kGridSide * kGridSide);
    g_aiWorld = world;
    g_aiMapSizeX = static_cast<std::uint16_t>(kTestMap);
    g_aiMapSizeY = static_cast<std::uint16_t>(kTestMap);
    DebugLogRecorder logs;
    int inserted = 0;
    const std::uint32_t refs = g_aiSiteTreeNilRefs;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        if (i % 10 == 0) {
            SetUpMap(rng, world, cells);
        }
        for (layout::Player& p : world->players) {
            p.race = static_cast<std::int32_t>(rng.Chance(1, 10) ? rng.Next32() : rng.Below(4));
        }
        TestLand a(cells), b(cells);
        a.land.width = b.land.width = rng.Chance(1, 10) ? rng.Below(kGridSide + 1) : kGridSide;
        a.land.height = b.land.height = rng.Chance(1, 10) ? rng.Below(kGridSide + 1) : kGridSide;
        const std::uint32_t index = rng.Below(3);
        const std::uint32_t player = index | (rng.Chance(1, 5) ? rng.Next32() & 0xffff0000u : 0);
        auto* record = static_cast<AiArea*>(msvc6::GameOperatorNew(sizeof(AiArea)));
        const std::uint32_t records = 1 + rng.Below(3);
        for (std::uint32_t k = 0; k < records; ++k) {
            MakeRecord(rng, record, static_cast<std::uint16_t>(rng.Below(4)));
            a.Append(index, record);
            b.Append(index, record);
            DestroyRecord(record);
        }
        msvc6::GameOperatorDelete(record);
        const bool refill = rng.Chance(1, 2);
        const std::uint32_t area = rng.Below(5) | (rng.Chance(1, 5) ? rng.Next32() & 0xffff0000u : 0);
        const std::uint32_t type = RandomType(rng);
        const std::uint32_t which = rng.Below(records);
        auto nth = [&](TestLand& land) {
            auto* node = reinterpret_cast<AiAreaNode*>(land.land.lists[index].head)->next;
            for (std::uint32_t k = 0; k < which; ++k) {
                node = node->next;
            }
            return &node->value;
        };
        g_aiLandEvaluation = reinterpret_cast<std::uint32_t>(&a.land);
        if (refill) {
            original.refill(&a.land, nullptr, player, area, type);
        } else {
            original.fill(&a.land, nullptr, nth(a), player);
        }
        const std::vector<DebugLogCall> logsA = logs.Take();
        g_aiLandEvaluation = reinterpret_cast<std::uint32_t>(&b.land);
        if (refill) {
            ours.refill(&b.land, nullptr, player, area, type);
        } else {
            ours.fill(&b.land, nullptr, nth(b), player);
        }
        const std::vector<DebugLogCall> logsB = logs.Take();
        const std::vector<std::uint8_t> contentsA = ListContents(a.land.lists[index]);
        const std::vector<std::uint8_t> contentsB = ListContents(b.land.lists[index]);
        const auto* headA = reinterpret_cast<const AiAreaNode*>(a.land.lists[index].head);
        for (const AiAreaNode* node = headA->next; node != headA; node = node->next) {
            for (const SiteTree& tree : node->value.site_trees) {
                inserted += static_cast<int>(tree.size);
            }
        }
        bool same = S3RE_EXPECT(contentsA == contentsB);
        if (!same) {
            std::size_t k = 0;
            while (k < contentsA.size() && k < contentsB.size() && contentsA[k] == contentsB[k]) {
                ++k;
            }
            ctx.Fail(__FILE__, __LINE__, "  sizes %u / %u, first difference at %u", static_cast<unsigned>(contentsA.size()),
                     static_cast<unsigned>(contentsB.size()), static_cast<unsigned>(k));
        }
        same &= S3RE_EXPECT(logsA == logsB);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, %s, player %08x, area %08x, type %08x", i,
                     refill ? "refill" : "fill", player, area, type);
        }
    }
    S3RE_EXPECT_EQ(g_aiSiteTreeNilRefs.get(), refs);
    if (insertedOut != nullptr) {
        *insertedOut = inserted;
    }
}

void __fastcall WrongFillSiteLists(AiLandEvaluation* self, void* edx, AiArea* record, std::uint32_t player) {
    // Wrong: a Roman player's coal mine places are rated twice.
    AI_CEvaLand_FillAreaSiteLists(self, edx, record, player);
    const std::uint16_t race = RaceOfPlayer(player);
    if (!RaceIs(race, Race::Roman)) {
        return;
    }
    ForEachAreaCell(self, player, record->area, [&](const CellScan& scan) {
        if (scan.cell->resources_around[kGridCoal] > kRichAround) {
            InsertCount(TreeOf(*record, B::CoalMine), scan, CountInOwnersLand(scan, Good::Coal));
        }
    });
}

void __fastcall WrongRefillSiteList(AiLandEvaluation* self, void* edx, std::uint32_t player, std::uint32_t area,
                            std::uint32_t type) {
    // Wrong: the workshops' ratings go into their own tree.
    const std::uint32_t t = type & 0xffff;
    if (t == Value(B::CatapultWorkshop) || t == Value(B::CannonWorkshop)) {
        type = (type & 0xffff0000u) | Value(B::LargeGong);
    }
    AI_CEvaLand_RefillAreaSiteList(self, edx, player, area, type);
    if (t != Value(B::CatapultWorkshop)) {
        return;
    }
    auto* const head = reinterpret_cast<AiAreaNode*>(self->lists[player & 0xffff].head);
    for (AiAreaNode* node = head->next; node != head; node = node->next) {
        if (node->value.area == static_cast<std::uint16_t>(area)) {
            EraseAll(node->value.site_trees[35]);
            ForEachAreaCell(self, player, static_cast<std::uint16_t>(area), [&](const CellScan& scan) {
                InsertRating(node->value.site_trees[35], scan, WorkshopRating(CountNearbyBuildings(scan)));
            });
            return;
        }
    }
}

}  // namespace

S3RE_TEST(AiAreaSiteLists_MatchOriginal) {
    int inserted = 0;
    CompareSiteLists(ctx, {&AI_CEvaLand_FillAreaSiteLists, &AI_CEvaLand_RefillAreaSiteList}, 1500, &inserted);
    S3RE_EXPECT(inserted > 1500 * 100);  // (site trees not empty: the ratings are reached)
}

S3RE_TEST(AiAreaSiteLists_ComparisonDetectsWrongImplementation) {
    {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareSiteLists(quiet, {&WrongFillSiteLists, &AI_CEvaLand_RefillAreaSiteList}, 600);
        S3RE_EXPECT(quiet.failures() > 0);
    }
    {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareSiteLists(quiet, {&AI_CEvaLand_FillAreaSiteLists, &WrongRefillSiteList}, 600);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

}  // namespace s3re::game
