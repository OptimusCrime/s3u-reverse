// The work-area finders of a computer player's new production buildings
// (Rainer\CompMapWorkerAgent.cpp; re/computer_players.md "Work areas"):
// each looks at the land evaluation grid cells (8 x 8 tiles) in square
// rings around the building's cell and picks a cell centre as the
// building's work area (*x, *y, EAX = 1; 0 if none). Dispatched by 0x573890
// after a building is finished and by CCentrumCheckAgent's run 0x560e00.
//
// The rings: ring r starts at (cell x + r, cell y - r) and walks four
// sides of 2r cells each: -x, +y, +x, -y (jump tables 0x59b014, 0x59b420,
// 0x59b76c, 0x59a708, 0x59ba80), visiting each cell before the step.
// Cells outside the grid (g_aiMapSize / 8) are skipped (not by the
// fishery, which has no bounds test).
//
//   0x0059ac50 AI::FindFarmWorkArea(const Building*, u16* x; stack u16* y)
//              fastcall, ret 4: rings 1..3; score = the cell's grain land
//              (CountResourceInOwnersLand 0x599a30, goods 9, low word) +
//              8 * (1 - |dx| - |dy| - ring - penalty), penalty 4 for the
//              cells straight below the building's cell, 2 for the cells
//              below that, else 0 (dx, dy: the building's cell minus the
//              cell; below = dy >= 0); TileCentre 0x554880 of the cell; a
//              score > 0 whose centre isn't near another farm's work area
//              (HasSameTypeWorkAreaNear 0x58e390, type 24, skipping the
//              building at the farm's position, AI::BuildingAt 0x599510)
//              goes into a std::map<u8 score, centre, greater<u8>> (unique
//              keys: the first cell of a score stays; the key is the score's
//              low byte). The map's first element (the highest key) is the
//              result.
//   0x0059b0a0 AI::FindRiceFarmWorkArea: the same with the score
//              CountPlayersResourcesAround 0x599f50 (goods 27 rice, the 3 x
//              3 cells around), used when its low word is not 0, no
//              distance term; type 36.
//   0x0059b430 AI::FindVineyardWorkArea: the same with
//              CountResourceInOwnersLand goods 21 (wine land); type 14.
//   0x0059a3a0 AI::FindWoodcutterWorkArea: rings 1..5; a cell counts when
//              its CountResourceInOwnersLand(goods 3, trees) low word is not
//              0. If the building's economy area (AI::BuildingRecordArea
//              0x599670) stores more than 10 logs (AI_AreaGoodsAmount
//              0x59dc10) the cells go into an AI::SiteTree (multiset,
//              ordered by the count's low byte, ascending) and the result
//              is its first element: the cell with the fewest trees (G:
//              plenty of logs, so thin out a sparse spot); otherwise the
//              first such cell is the result at once.
//   0x0059a8d0 AI::FindStonecutterWorkArea: the same with goods 2
//              (stones) for the count and the stock.
//   0x0059b780 AI::FindFisheryWorkArea: rings 1..7 without bounds test,
//              the first cell whose centre belongs to the building's owner
//              (AI::CellCentreOwner 0x554850) and whose 3 x 3 fish sum
//              (AiGridCell::resources_around[kGridFish]) is not 0. It also
//              builds and destroys an empty AI::SiteTree (a leftover; its
//              head comes from the library's _Buynode 0x59cf20).
//
// The maps of the farm, rice farm and vineyard are a second MSVC6 _Tree
// instance with its own nil node (g_aiFieldMapNil 0x7e80a8, references
// g_aiFieldMapNilRefs 0x7e80ac); its library functions (_Init 0x59cb70,
// insert 0x59c330, erase(first, last) 0x59c470) are called through their
// addresses. The map is destroyed inline (erase, delete the head, under the
// lock --refs and the nil node freed at 0), as in the original. A stored
// value's padding byte (+1, the node's +0xd) is an uninitialized byte of
// the original's frame (ours 0); the nodes are freed before the return.

#include "game/ai/ai_layout.h"
#include "game/enums/buildings.h"
#include "game/enums/goods.h"
#include "game/globals.h"
#include "game/layout.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/lockit.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace s3re::game {

using namespace ai;
using msvc6::GameLockit;

namespace {

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kBuildingAt = 0x00599510;                // AI::BuildingAt (fastcall)
constexpr std::uintptr_t kBuildingRecordArea = 0x00599670;        // AI::BuildingRecordArea (fastcall)
constexpr std::uintptr_t kAreaGoodsAmount = 0x0059dc10;           // AI_AreaGoodsAmount (fastcall)
constexpr std::uintptr_t kCountResourceInOwnersLand = 0x00599a30;  // (fastcall)
constexpr std::uintptr_t kCountPlayersResourcesAround = 0x00599f50;  // (fastcall, pointers)
constexpr std::uintptr_t kHasSameTypeWorkAreaNear = 0x0058e390;   // (fastcall)
constexpr std::uintptr_t kTileCentre = 0x00554880;                // AI::TileCentre_554880 (thiscall)
constexpr std::uintptr_t kCellCentreOwner = 0x00554850;           // AI::CellCentreOwner (thiscall)
constexpr std::uintptr_t kGridCell = 0x00554700;                  // AI::CEvaLand::GridCell (thiscall)
// AI::SiteTree (ai_site_tree.cpp).
constexpr std::uintptr_t kSiteTreeInit = 0x0055a970;
constexpr std::uintptr_t kSiteTreeDestruct = 0x00559170;
constexpr std::uintptr_t kSiteTreeInsert = 0x00559270;
constexpr std::uintptr_t kSiteTreeEraseRange = 0x005593a0;
// The library's map<u8, position, greater<u8>> and its _Buynode.
constexpr std::uintptr_t kFieldMapInit = 0x0059cb70;        // std::_Tree::_Init (thiscall)
constexpr std::uintptr_t kFieldMapInsert = 0x0059c330;      // insert(pair* out, const value*) thiscall
constexpr std::uintptr_t kFieldMapEraseRange = 0x0059c470;  // erase(out, first, last) thiscall
constexpr std::uintptr_t kBuyNode = 0x0059cf20;             // std::_Tree::_Buynode(parent, colour) thiscall

constexpr unsigned kCellShift = 3;
constexpr int kSides = 4;
constexpr std::int32_t kSideDx[kSides] = {-1, 0, 1, 0};
constexpr std::int32_t kSideDy[kSides] = {0, 1, 0, -1};
constexpr int kFieldRings = 3;
constexpr int kCutterRings = 5;
constexpr int kFisheryRings = 7;
constexpr std::uint32_t kPlentyOfGoods = 10;  // more in the area's stock: pick the sparsest cell
constexpr std::int32_t kFarmDistanceWeight = 8;
constexpr std::int32_t kFarmBelowPenalty = 2;
constexpr std::int32_t kFarmStraightBelowPenalty = 4;

using CellFn = std::uint32_t(__fastcall*)(std::uint32_t self, void* edx, std::int32_t x, std::int32_t y);

std::uint32_t CountResourceInOwnersLand(std::int32_t cellX, std::int32_t cellY, std::uint32_t owner,
                                        std::uint32_t goods) {
    using Fn = std::uint32_t(__fastcall*)(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t);
    return reinterpret_cast<Fn>(kCountResourceInOwnersLand)(cellX, cellY, owner, goods);
}

void TileCentre(std::int32_t cellX, std::int32_t cellY, std::uint16_t* x, std::uint16_t* y) {
    using Fn = std::uint32_t(__fastcall*)(std::uint32_t self, void* edx, std::int32_t, std::int32_t, std::uint16_t*,
                                          std::uint16_t*);
    reinterpret_cast<Fn>(kTileCentre)(g_aiLandEvaluation, nullptr, cellX, cellY, x, y);
}

bool InGrid(std::int32_t cellX, std::int32_t cellY) {
    const std::uint32_t width = g_aiMapSizeX >> kCellShift;
    const std::uint32_t height = g_aiMapSizeY >> kCellShift;
    return cellX >= 0 && static_cast<std::uint32_t>(cellX) < width && cellY >= 0 &&
           static_cast<std::uint32_t>(cellY) < height;
}

// Walks the rings around (cellX, cellY) and calls visit(cellX, cellY, ring)
// for every cell; visit returns true to stop.
template <typename Visit>
bool WalkRings(std::int32_t cellX, std::int32_t cellY, int rings, Visit visit) {
    for (int ring = 1; ring <= rings; ++ring) {
        ++cellX;
        --cellY;
        for (int side = 0; side < kSides; ++side) {
            for (int step = 0; step < 2 * ring; ++step) {
                if (visit(cellX, cellY, ring)) {
                    return true;
                }
                cellX += kSideDx[side];
                cellY += kSideDy[side];
            }
        }
    }
    return false;
}

// The farm-type finders' cell score: whether the cell counts, and its key.
struct FieldScore {
    bool counts;
    std::uint8_t key;
};

struct FieldCell {
    std::int32_t x, y;         // the cell
    std::int32_t centreX, centreY;  // the building's cell
    int ring;
    std::uint32_t owner;
};

using FieldScoreFn = FieldScore (*)(const FieldCell& cell);

FieldScore FarmScore(const FieldCell& cell) {
    const std::uint32_t grain = CountResourceInOwnersLand(cell.x, cell.y, cell.owner, Value(Good::Grain)) & 0xffff;
    std::int32_t penalty = 0;
    if (cell.centreY >= cell.y) {
        penalty = cell.centreX == cell.x ? kFarmStraightBelowPenalty : kFarmBelowPenalty;
    }
    const std::int32_t distance = std::abs(cell.centreX - cell.x) + penalty + std::abs(cell.centreY - cell.y);
    const std::int32_t score =
        static_cast<std::int32_t>(grain) + kFarmDistanceWeight * (1 - distance - cell.ring);
    return {score > 0, static_cast<std::uint8_t>(score)};
}

FieldScore RiceFarmScore(const FieldCell& cell) {
    // The original passes the addresses of dwords whose low words are the
    // values (the callee reads words).
    std::uint32_t x = static_cast<std::uint32_t>(cell.x);
    std::uint32_t y = static_cast<std::uint32_t>(cell.y);
    std::uint32_t player = cell.owner;
    std::uint32_t goods = Value(Good::Rice);
    using Fn = std::uint32_t(__fastcall*)(const std::uint32_t*, const std::uint32_t*, const std::uint32_t*,
                                          const std::uint32_t*);
    const std::uint32_t rice = reinterpret_cast<Fn>(kCountPlayersResourcesAround)(&x, &y, &player, &goods);
    return {(rice & 0xffff) != 0, static_cast<std::uint8_t>(rice)};
}

FieldScore VineyardScore(const FieldCell& cell) {
    const std::uint32_t wine = CountResourceInOwnersLand(cell.x, cell.y, cell.owner, Value(Good::Wine));
    return {(wine & 0xffff) != 0, static_cast<std::uint8_t>(wine)};
}

// 0x59ac50 / 0x59b0a0 / 0x59b430: the farm-type finders.
std::uint32_t FindFieldWorkArea(const layout::Building* building, std::uint16_t* x, std::uint16_t* y,
                                FieldScoreFn score, BuildingType type) {
    FieldCell cell{};
    cell.owner = building->owner;
    cell.centreX = building->x >> kCellShift;
    cell.centreY = building->y >> kCellShift;
    SiteTree map{};  // the same _Tree layout; allocator / comparator bytes are never read
    map.multi = 0;
    reinterpret_cast<void(__fastcall*)(SiteTree*, void*)>(kFieldMapInit)(&map, nullptr);
    using BuildingAtFn = std::uint32_t(__fastcall*)(const std::uint16_t*, const std::uint16_t*);
    const std::uint32_t skip = reinterpret_cast<BuildingAtFn>(kBuildingAt)(&building->x, &building->y);
    WalkRings(cell.centreX, cell.centreY, kFieldRings, [&](std::int32_t cx, std::int32_t cy, int ring) {
        if (!InGrid(cx, cy)) {
            return false;
        }
        cell.x = cx;
        cell.y = cy;
        cell.ring = ring;
        const FieldScore scored = score(cell);
        std::uint16_t centreX = 0;
        std::uint16_t centreY = 0;
        TileCentre(cx, cy, &centreX, &centreY);
        if (!scored.counts) {
            return false;
        }
        const auto player = static_cast<std::uint8_t>(cell.owner);
        const auto kind = static_cast<std::uint8_t>(Value(type));
        using SameTypeFn = std::uint32_t(__fastcall*)(std::uint32_t, const std::uint8_t*, const std::uint8_t*,
                                                      const std::uint16_t*, const std::uint16_t*);
        if ((reinterpret_cast<SameTypeFn>(kHasSameTypeWorkAreaNear)(skip, &kind, &player, &centreX, &centreY) &
             0xff) != 0) {
            return false;
        }
        const SiteValue value{scored.key, 0, centreX, centreY};
        SiteTreeInsertResult inserted;
        using InsertFn = SiteTreeInsertResult*(__fastcall*)(SiteTree*, void*, SiteTreeInsertResult*, const SiteValue*);
        reinterpret_cast<InsertFn>(kFieldMapInsert)(&map, nullptr, &inserted, &value);
        return false;
    });
    std::uint32_t found = 0;
    if (map.size != 0) {
        const SiteTreeNode* best = map.head->left;  // the highest key (greater<u8>)
        *x = best->value.x;
        *y = best->value.y;
        found = 1;
    }
    // The map's destructor, inlined.
    SiteTreeNode* next;
    using EraseRangeFn = SiteTreeNode**(__fastcall*)(SiteTree*, void*, SiteTreeNode**, SiteTreeNode*, SiteTreeNode*);
    reinterpret_cast<EraseRangeFn>(kFieldMapEraseRange)(&map, nullptr, &next, map.head->left, map.head);
    msvc6::GameOperatorDelete(map.head);
    map.head = nullptr;
    map.size = 0;
    GameLockit lock;
    if (--g_aiFieldMapNilRefs == 0) {
        msvc6::GameOperatorDelete(g_aiFieldMapNil);
        g_aiFieldMapNil = nullptr;
    }
    return found;
}

// The inlined destructor of an AI::SiteTree local (erase, head, nil node).
void DestroySiteTree(SiteTree& tree) {
    SiteTreeNode* next;
    using EraseRangeFn = SiteTreeNode**(__fastcall*)(SiteTree*, void*, SiteTreeNode**, SiteTreeNode*, SiteTreeNode*);
    reinterpret_cast<EraseRangeFn>(kSiteTreeEraseRange)(&tree, nullptr, &next, tree.head->left, tree.head);
    msvc6::GameOperatorDelete(tree.head);
    tree.head = nullptr;
    tree.size = 0;
    GameLockit lock;
    if (--g_aiSiteTreeNilRefs == 0) {
        msvc6::GameOperatorDelete(g_aiSiteTreeNil);
        g_aiSiteTreeNil = nullptr;
    }
}

// 0x59a3a0 / 0x59a8d0: the woodcutter and stonecutter finders.
std::uint32_t FindCutterWorkArea(const layout::Building* building, std::uint16_t* x, std::uint16_t* y, Good goods) {
    const std::uint32_t owner = building->owner;
    SiteTree tree{};
    tree.multi = 1;
    reinterpret_cast<void(__fastcall*)(SiteTree*, void*)>(kSiteTreeInit)(&tree, nullptr);
    using AreaFn = std::uint32_t(__fastcall*)(const layout::Building*);
    const std::uint32_t area = reinterpret_cast<AreaFn>(kBuildingRecordArea)(building);
    const auto stockGoods = static_cast<std::uint8_t>(Value(goods));
    using StockFn = std::uint32_t(__fastcall*)(const std::uint8_t*, const std::uint32_t*);
    const bool plenty = reinterpret_cast<StockFn>(kAreaGoodsAmount)(&stockGoods, &area) > kPlentyOfGoods;
    bool firstFound = false;
    WalkRings(building->x >> kCellShift, building->y >> kCellShift, kCutterRings,
              [&](std::int32_t cx, std::int32_t cy, int /*ring*/) {
                  if (!InGrid(cx, cy)) {
                      return false;
                  }
                  const std::uint32_t count = CountResourceInOwnersLand(cx, cy, owner, Value(goods));
                  if ((count & 0xffff) == 0) {
                      return false;
                  }
                  std::uint16_t centreX = 0;
                  std::uint16_t centreY = 0;
                  TileCentre(cx, cy, &centreX, &centreY);
                  if (!plenty) {
                      *x = centreX;
                      *y = centreY;
                      firstFound = true;
                      return true;
                  }
                  const SiteValue value{static_cast<std::uint8_t>(count), 0, centreX, centreY};
                  SiteTreeInsertResult inserted;
                  using InsertFn =
                      SiteTreeInsertResult*(__fastcall*)(SiteTree*, void*, SiteTreeInsertResult*, const SiteValue*);
                  reinterpret_cast<InsertFn>(kSiteTreeInsert)(&tree, nullptr, &inserted, &value);
                  return false;
              });
    if (firstFound) {
        reinterpret_cast<void(__fastcall*)(SiteTree*, void*)>(kSiteTreeDestruct)(&tree, nullptr);
        return 1;
    }
    std::uint32_t found = 0;
    if (tree.size != 0) {
        const SiteTreeNode* fewest = tree.head->left;  // the lowest key
        *x = fewest->value.x;
        *y = fewest->value.y;
        found = 1;
    }
    DestroySiteTree(tree);
    return found;
}

}  // namespace

using WorkAreaFinderFn = std::uint32_t(__fastcall*)(const layout::Building* building, std::uint16_t* x,
                                                    std::uint16_t* y);

std::uint32_t __fastcall AI_FindFarmWorkArea(const layout::Building* building, std::uint16_t* x, std::uint16_t* y);
std::uint32_t __fastcall AI_FindRiceFarmWorkArea(const layout::Building* building, std::uint16_t* x,
                                                 std::uint16_t* y);
std::uint32_t __fastcall AI_FindVineyardWorkArea(const layout::Building* building, std::uint16_t* x,
                                                 std::uint16_t* y);
std::uint32_t __fastcall AI_FindWoodcutterWorkArea(const layout::Building* building, std::uint16_t* x,
                                                   std::uint16_t* y);
std::uint32_t __fastcall AI_FindStonecutterWorkArea(const layout::Building* building, std::uint16_t* x,
                                                    std::uint16_t* y);
std::uint32_t __fastcall AI_FindFisheryWorkArea(const layout::Building* building, std::uint16_t* x,
                                                std::uint16_t* y);

S3RE_HOOK(0x0059ac50, AI_FindFarmWorkArea, WorkAreaFinderFn, Original_AI_FindFarmWorkArea, AI_FindFarmWorkArea);
S3RE_HOOK(0x0059b0a0, AI_FindRiceFarmWorkArea, WorkAreaFinderFn, Original_AI_FindRiceFarmWorkArea,
          AI_FindRiceFarmWorkArea);
S3RE_HOOK(0x0059b430, AI_FindVineyardWorkArea, WorkAreaFinderFn, Original_AI_FindVineyardWorkArea,
          AI_FindVineyardWorkArea);
S3RE_HOOK(0x0059a3a0, AI_FindWoodcutterWorkArea, WorkAreaFinderFn, Original_AI_FindWoodcutterWorkArea,
          AI_FindWoodcutterWorkArea);
S3RE_HOOK(0x0059a8d0, AI_FindStonecutterWorkArea, WorkAreaFinderFn, Original_AI_FindStonecutterWorkArea,
          AI_FindStonecutterWorkArea);
S3RE_HOOK(0x0059b780, AI_FindFisheryWorkArea, WorkAreaFinderFn, Original_AI_FindFisheryWorkArea,
          AI_FindFisheryWorkArea);

// Purpose: Picks the work area (where the farmer sows) of a computer
// player's new farm: the nearby cell with the most free grain land, not
// overlapping another farm's.
std::uint32_t __fastcall AI_FindFarmWorkArea(const layout::Building* building, std::uint16_t* x, std::uint16_t* y) {
    return FindFieldWorkArea(building, x, y, &FarmScore, BuildingType::Farm);
}

// Purpose: The same for a rice farm (the most rice land around the cell).
std::uint32_t __fastcall AI_FindRiceFarmWorkArea(const layout::Building* building, std::uint16_t* x,
                                                 std::uint16_t* y) {
    return FindFieldWorkArea(building, x, y, &RiceFarmScore, BuildingType::RiceFarm);
}

// Purpose: The same for a vineyard (the most wine land).
std::uint32_t __fastcall AI_FindVineyardWorkArea(const layout::Building* building, std::uint16_t* x,
                                                 std::uint16_t* y) {
    return FindFieldWorkArea(building, x, y, &VineyardScore, BuildingType::Vineyard);
}

// Purpose: Picks a computer player's woodcutter's work area: a nearby cell
// with trees on the player's land.
std::uint32_t __fastcall AI_FindWoodcutterWorkArea(const layout::Building* building, std::uint16_t* x,
                                                   std::uint16_t* y) {
    return FindCutterWorkArea(building, x, y, Good::Log);
}

// Purpose: The same for a stonecutter (a nearby cell with stones).
std::uint32_t __fastcall AI_FindStonecutterWorkArea(const layout::Building* building, std::uint16_t* x,
                                                    std::uint16_t* y) {
    return FindCutterWorkArea(building, x, y, Good::Stone);
}

// Purpose: Picks a computer player's fishery's work area: the nearest cell
// of the player's with fish around.
std::uint32_t __fastcall AI_FindFisheryWorkArea(const layout::Building* building, std::uint16_t* x,
                                                std::uint16_t* y) {
    const std::uint16_t owner = building->owner;
    // An unused AI::SiteTree, constructed inline (0x59b7c9..0x59b856).
    SiteTree tree{};
    tree.multi = 1;
    {
        GameLockit lock;
        if (g_aiSiteTreeNil.get() == nullptr) {
            auto* nil = static_cast<SiteTreeNode*>(msvc6::GameOperatorNew(sizeof(SiteTreeNode)));
            nil->parent = nullptr;
            nil->color = kTreeBlack;
            g_aiSiteTreeNil = nil;
            nil->left = nullptr;
            nil->right = nullptr;
        }
        ++g_aiSiteTreeNilRefs;
        using BuyNodeFn = SiteTreeNode*(__fastcall*)(SiteTree*, void*, SiteTreeNode* parent, std::uint32_t colour);
        SiteTreeNode* head = reinterpret_cast<BuyNodeFn>(kBuyNode)(&tree, nullptr, g_aiSiteTreeNil, kTreeRed);
        tree.head = head;
        head->left = head;
        head->right = head;
        tree.size = 0;
    }
    // 16-bit cell coordinates, sign-extended for the calls.
    auto cellX = static_cast<std::uint16_t>(building->x >> kCellShift);
    auto cellY = static_cast<std::uint16_t>(building->y >> kCellShift);
    std::int32_t foundX = 0;
    std::int32_t foundY = 0;
    const bool found = WalkRings(0, 0, kFisheryRings, [&](std::int32_t dx, std::int32_t dy, int /*ring*/) {
        const auto cx = static_cast<std::int16_t>(cellX + dx);
        const auto cy = static_cast<std::int16_t>(cellY + dy);
        if (static_cast<std::uint16_t>(reinterpret_cast<CellFn>(kCellCentreOwner)(g_aiLandEvaluation, nullptr, cx,
                                                                                   cy)) != owner) {
            return false;
        }
        using GridCellFn = const AiGridCell*(__fastcall*)(std::uint32_t, void*, std::int32_t, std::int32_t);
        const AiGridCell* grid = reinterpret_cast<GridCellFn>(kGridCell)(g_aiLandEvaluation, nullptr, cx, cy);
        if (grid->resources_around[kGridFish] == 0) {
            return false;
        }
        foundX = cx;
        foundY = cy;
        return true;
    });
    if (found) {
        std::uint16_t centreX;
        std::uint16_t centreY;
        TileCentre(foundX, foundY, &centreX, &centreY);
        *x = centreX;
        *y = centreY;
    }
    DestroySiteTree(tree);
    return found ? 1 : 0;
}


// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// An 80 x 80 map (owners in patches, grass / swampy grass / stream / other
// terrain, heights for the slopes, trees and stones, tile areas 0..3), a
// 10 x 10 land grid with random goods bytes and fish sums, up to 30
// buildings of the work-area types (for HasSameTypeWorkAreaNear) and pile
// lists of logs and stones in the areas (for the cutters' stock test).
// Each case runs one finder on a building of the map; compared: EAX, *x,
// *y and the nil nodes' reference counts and presence afterwards. The
// callees run (verified; in-process the tree functions are ours on both
// sides).

namespace {

constexpr std::uint32_t kTestMap = 80;
constexpr std::uint32_t kGridSide = 10;
constexpr std::uint32_t kTestBuildings = 30;
constexpr std::uint32_t kTestPiles = 200;

struct SavedFinderGlobals {
    CGameData* world = g_aiWorld;
    std::uint32_t land = g_aiLandEvaluation;
    std::uint16_t sizeX = g_aiMapSizeX, sizeY = g_aiMapSizeY;
    SiteTreeNode* fieldNil = g_aiFieldMapNil;
    std::uint32_t fieldRefs = g_aiFieldMapNilRefs;
    SiteTreeNode* siteNil = g_aiSiteTreeNil;
    std::uint32_t siteRefs = g_aiSiteTreeNilRefs;
    ~SavedFinderGlobals() {
        g_aiWorld = world;
        g_aiLandEvaluation = land;
        g_aiMapSizeX = sizeX;
        g_aiMapSizeY = sizeY;
        g_aiFieldMapNil = fieldNil;
        g_aiFieldMapNilRefs = fieldRefs;
        g_aiSiteTreeNil = siteNil;
        g_aiSiteTreeNilRefs = siteRefs;
    }
};

void SetUpFinderWorld(test::Rng& rng, CGameData* world, std::vector<AiGridCell>& cells) {
    world->map_size = kTestMap;
    const std::uint32_t patch = 6 + rng.Below(20);
    const std::uint32_t phase = rng.Below(4);
    const bool flat = rng.Chance(2, 3);  // grain land needs gentle slopes, wine land steep ones
    for (std::uint32_t x = 0; x < kTestMap; ++x) {
        for (std::uint32_t y = 0; y < kTestMap; ++y) {
            layout::Tile& tile = world->tiles[x][y];
            const std::uint32_t k = (x / patch) * 7 + (y / patch) * 3 + phase;
            tile.owner = static_cast<std::uint8_t>(k % 4 == 3 ? 0xff : k % 4);
            const std::uint32_t roll = rng.Below(10);
            tile.terrain = static_cast<std::uint8_t>(roll < 5    ? 0x10 + rng.Below(16)
                                                     : roll == 5 ? 0x15
                                                     : roll == 6 ? 0x60 + rng.Below(16)
                                                                 : rng.Below(256));
            tile.height = static_cast<std::uint8_t>(rng.Below(flat ? 2 : 30));
            tile.flags = static_cast<std::uint8_t>(rng.Chance(1, 10) ? rng.Below(256) : 0);
            const std::uint32_t object = rng.Below(12);
            tile.object = static_cast<std::uint8_t>(object == 0   ? 0x44 + rng.Below(16)  // tree
                                                    : object == 1 ? 0x73 + rng.Below(12)  // stones
                                                                  : rng.Below(0x40));
            tile.area = static_cast<std::uint16_t>(rng.Chance(1, 10) ? 0 : 1 + rng.Below(3));
            tile.object_number = 0;
        }
    }
    for (AiGridCell& cell : cells) {
        std::uint8_t* bytes = reinterpret_cast<std::uint8_t*>(&cell);
        for (std::size_t b = 2; b < 0x0f; ++b) {
            bytes[b] = static_cast<std::uint8_t>(rng.Chance(1, 3) ? 0 : 1 + rng.Below(255));
        }
        cell.resources_around[kGridFish] = static_cast<std::uint16_t>(rng.Chance(2, 3) ? 0 : 1 + rng.Below(300));
    }
    constexpr std::uint8_t kTypes[] = {Value(BuildingType::Farm),     Value(BuildingType::RiceFarm),
                                       Value(BuildingType::Vineyard), Value(BuildingType::Woodcutter),
                                       Value(BuildingType::Fishery),  Value(BuildingType::Stonecutter)};
    world->building_high_water = kTestBuildings;
    for (std::uint32_t id = 1; id <= kTestBuildings; ++id) {
        layout::Building& building = world->buildings[id];
        building.owner = static_cast<std::uint8_t>(rng.Chance(1, 8) ? 0xff : rng.Below(3));
        building.art = kTypes[rng.Below(sizeof(kTypes))];
        building.buildup = static_cast<std::uint8_t>(rng.Chance(1, 5) ? 1 : 0);
        // (on the smallest test map: an area of 0xffff would read past the areas)
        building.x = building.door_x = static_cast<std::uint16_t>(rng.Below(kTestMap - 40));
        building.y = building.door_y = static_cast<std::uint16_t>(rng.Below(kTestMap - 40));
        building.work_x = static_cast<std::uint16_t>(rng.Below(kTestMap));
        building.work_y = static_cast<std::uint16_t>(rng.Below(kTestMap));
        layout::Tile& tile = world->tiles[building.x][building.y];
        tile.object_number = static_cast<std::uint16_t>(id);
        tile.flags = static_cast<std::uint8_t>(tile.flags | layout::kTileHasBuilding);
    }
    for (std::uint32_t area = 0; area < 4; ++area) {
        for (Good goods : {Good::Log, Good::Stone}) {
            std::uint16_t head = 0;
            const std::uint32_t piles = rng.Below(5);
            for (std::uint32_t k = 0; k < piles; ++k) {
                const auto pile = static_cast<std::uint16_t>(1 + rng.Below(kTestPiles));
                if (pile == head || world->piles[pile].next != 0) {
                    continue;
                }
                world->piles[pile].amount = static_cast<std::uint8_t>(rng.Below(9));
                world->piles[pile].next = head;
                head = pile;
            }
            world->areas[area].piles_by_goods[Value(goods)] = head;
        }
    }
}

struct FinderOutcome {
    std::uint32_t result;
    std::uint16_t x, y;
    std::uint32_t fieldRefs, siteRefs;
    bool fieldNil, siteNil;
};

FinderOutcome RunFinder(WorkAreaFinderFn finder, const layout::Building* building, std::uint16_t x, std::uint16_t y,
                        std::uint32_t refs) {
    g_aiFieldMapNil = nullptr;
    g_aiFieldMapNilRefs = refs;  // > 0: another map is alive (its nil node made here)
    g_aiSiteTreeNil = nullptr;
    g_aiSiteTreeNilRefs = refs;
    FinderOutcome out{};
    out.x = x;
    out.y = y;
    out.result = finder(building, &out.x, &out.y);
    out.fieldRefs = g_aiFieldMapNilRefs;
    out.siteRefs = g_aiSiteTreeNilRefs;
    out.fieldNil = g_aiFieldMapNil.get() != nullptr;
    out.siteNil = g_aiSiteTreeNil.get() != nullptr;
    // A nil node left alive by refs > 0 (the same on both sides) goes back.
    if (g_aiFieldMapNil.get() != nullptr) {
        msvc6::GameOperatorDelete(g_aiFieldMapNil);
    }
    if (g_aiSiteTreeNil.get() != nullptr) {
        msvc6::GameOperatorDelete(g_aiSiteTreeNil);
    }
    return out;
}

struct FinderSet {
    WorkAreaFinderFn farm, riceFarm, vineyard, woodcutter, stonecutter, fishery;
};

void CompareFinders(test::Context& ctx, const FinderSet& ours, int cases) {
    const FinderSet original{Original_AI_FindFarmWorkArea,       Original_AI_FindRiceFarmWorkArea,
                             Original_AI_FindVineyardWorkArea,   Original_AI_FindWoodcutterWorkArea,
                             Original_AI_FindStonecutterWorkArea, Original_AI_FindFisheryWorkArea};
    SavedFinderGlobals saved;
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    std::vector<AiGridCell> cells(kGridSide * kGridSide);
    AiLandEvaluation land{};
    land.cells = cells.data();
    land.width = kGridSide;
    land.height = kGridSide;
    g_aiWorld = world;
    g_aiLandEvaluation = reinterpret_cast<std::uint32_t>(&land);
    int found[6] = {};
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        if (i % 20 == 0) {
            SetUpFinderWorld(rng, world, cells);
        }
        g_aiMapSizeX = static_cast<std::uint16_t>(kTestMap - rng.Below(40));
        g_aiMapSizeY = static_cast<std::uint16_t>(kTestMap - rng.Below(40));
        const int which = static_cast<int>(rng.Below(6));
        const WorkAreaFinderFn a[] = {original.farm,        original.riceFarm,    original.vineyard,
                                      original.woodcutter,  original.stonecutter, original.fishery};
        const WorkAreaFinderFn b[] = {ours.farm,       ours.riceFarm,    ours.vineyard,
                                      ours.woodcutter, ours.stonecutter, ours.fishery};
        const layout::Building* building = &world->buildings[1 + rng.Below(kTestBuildings)];
        const auto x = static_cast<std::uint16_t>(rng.Next32());
        const auto y = static_cast<std::uint16_t>(rng.Next32());
        const std::uint32_t refs = rng.Chance(1, 4) ? 1 + rng.Below(3) : 0;
        const FinderOutcome ra = RunFinder(a[which], building, x, y, refs);
        const FinderOutcome rb = RunFinder(b[which], building, x, y, refs);
        found[which] += ra.result != 0 ? 1 : 0;
        bool same = S3RE_EXPECT_EQ(ra.result, rb.result);
        same &= S3RE_EXPECT_EQ(ra.x, rb.x);
        same &= S3RE_EXPECT_EQ(ra.y, rb.y);
        same &= S3RE_EXPECT_EQ(ra.fieldRefs, rb.fieldRefs);
        same &= S3RE_EXPECT_EQ(ra.siteRefs, rb.siteRefs);
        same &= S3RE_EXPECT_EQ(ra.fieldNil, rb.fieldNil);
        same &= S3RE_EXPECT_EQ(ra.siteNil, rb.siteNil);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, finder %d", i, which);
        }
    }
    if (cases >= 1000) {
        for (int which = 0; which < 6; ++which) {
            if (!S3RE_EXPECT(found[which] >= cases / 60)) {
                ctx.Fail(__FILE__, __LINE__, "  finder %d found only %d", which, found[which]);
            }
        }
    }
}

FieldScore WrongFarmCountsScore(const FieldCell& cell) {
    // Wrong: a score of 0 counts too.
    std::int32_t grain = static_cast<std::int32_t>(
        CountResourceInOwnersLand(cell.x, cell.y, cell.owner, Value(Good::Grain)) & 0xffff);
    FieldScore score = FarmScore(cell);
    score.counts = score.counts || grain == 0;
    return score;
}

std::uint32_t __fastcall WrongFarmCounts(const layout::Building* building, std::uint16_t* x, std::uint16_t* y) {
    return FindFieldWorkArea(building, x, y, &WrongFarmCountsScore, BuildingType::Farm);
}

FieldScore WrongFarmKeyScore(const FieldCell& cell) {
    FieldScore score = FarmScore(cell);
    ++score.key;
    return score;
}

// Wrong: every key one higher, the same order (scores stay below 255), so
// only the freed map nodes' key bytes differ: the emulator self-test's
// pairing next to their ignored padding bytes (+0xd).
std::uint32_t __fastcall WrongFarmKeys(const layout::Building* building, std::uint16_t* x, std::uint16_t* y) {
    return FindFieldWorkArea(building, x, y, &WrongFarmKeyScore, BuildingType::Farm);
}

std::uint32_t __fastcall WrongWoodcutter(const layout::Building* building, std::uint16_t* x, std::uint16_t* y) {
    // Wrong: the stone stock and count.
    return FindCutterWorkArea(building, x, y, Good::Stone);
}

std::uint32_t __fastcall WrongFishery(const layout::Building* building, std::uint16_t* x, std::uint16_t* y) {
    // Wrong: starts one cell further right.
    layout::Building moved = *building;
    moved.x = static_cast<std::uint16_t>(moved.x + 8);
    return AI_FindFisheryWorkArea(&moved, x, y);
}

FinderSet OurFinders() {
    return {&AI_FindFarmWorkArea,       &AI_FindRiceFarmWorkArea,    &AI_FindVineyardWorkArea,
            &AI_FindWoodcutterWorkArea, &AI_FindStonecutterWorkArea, &AI_FindFisheryWorkArea};
}

}  // namespace

S3RE_TEST(AiWorkAreaFinders_MatchOriginal) {
    CompareFinders(ctx, OurFinders(), 3000);
}

S3RE_TEST(AiWorkAreaFinders_ComparisonDetectsWrongImplementation) {
    FinderSet wrongs[3] = {OurFinders(), OurFinders(), OurFinders()};
    wrongs[0].farm = &WrongFarmCounts;
    wrongs[1].woodcutter = &WrongWoodcutter;
    wrongs[2].fishery = &WrongFishery;
    for (const FinderSet& wrong : wrongs) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareFinders(quiet, wrong, 3000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
    // WrongFarmKeys is invisible here (results unchanged); run to keep it
    // in the DLL for the emulator's self-test.
    FinderSet keys = OurFinders();
    keys.farm = &WrongFarmKeys;
    test::Context quiet(ctx.name(), 1, /*quiet=*/true);
    CompareFinders(quiet, keys, 300);
    S3RE_EXPECT(quiet.failures() == 0);
}

}  // namespace s3re::game
