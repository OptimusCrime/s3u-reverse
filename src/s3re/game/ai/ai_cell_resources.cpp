// Computer-player queries about the resources of the land evaluation's grid
// cells (computer players A2, session 53; Rainer\CompMapWorkerAgent.cpp,
// re/computer_players.md). A cell is a block of 8 x 8 tiles; cell (cx, cy)
// covers the tiles cx * 8 .. cx * 8 + 7, cy * 8 .. cy * 8 + 7.
//
//   0x00599df0 AI::OwnersLandHasResource(cx, cy, player, goods) fastcall
//              (ECX, EDX low words; stack dwords, low words used), ret 8:
//              walks the cell's tiles (y outer, x inner) and returns 1 at
//              the first tile owned by the player (AI::TileOwner 0x599570,
//              16-bit compare) that has the goods' resource: stone (2) if
//              AI::StoneAmountAt (0x599910, AX) is not 0 and the tile (x+2,
//              y+2) is also the player's; logs (3) if AI::IsTreeAt
//              (0x599950) and (x+1, y+1) is the player's; water (26) if the
//              tile's terrain class (Tile::terrain & 0xf0, g_aiWorld) is
//              0x60. Else 0 (other goods: always 0, after asking TileOwner
//              for every tile). The yes/no version of
//              CountResourceInOwnersLand 0x599a30. The callees get pointers
//              to dword copies of the coordinates, as in the original.
//   0x0059a380 AI::WorkCellHasTrees(building) fastcall: OwnersLandHasResource
//              (work_x / 8, work_y / 8, owner, 3) (16-bit shifts; the upper
//              halves of ECX, EDX and the player dword are the caller's
//              leftovers, which the callee masks off). EAX = its result.
//   0x0059a8b0 AI::WorkCellHasStones(building) fastcall: the same with
//              goods 2.
//   0x0059a120 AI::FindResourceCellAround(u16* x, u16* y, player, goods)
//              fastcall, ret 8: for goods 2 or 3, the cells around the
//              tile's cell (x / 8 - 1 .. + 1 outer, y / 8 - 1 .. + 1 inner;
//              16-bit signed, skipped if negative or not below the grid's
//              width / height of g_aiLandEvaluation): the first whose
//              presence byte (AiGridCell +3 stones, +4 trees; GridCell
//              0x554700 is asked for every cell on the grid, whatever the
//              goods) is set and where OwnersLandHasResource holds: its
//              centre goes to *x, *y (AI::TileCentre_554880) and EAX = 1.
//              Else EAX = 0.
//   0x00599a30 AI::CountResourceInOwnersLand(cx, cy, player, goods)
//              fastcall (ECX, EDX low words; stack dwords, low words used),
//              ret 8 (the original name, from its DebugLog text): walks the
//              cell's tiles (y outer, x inner) and, for each tile owned by
//              the player (AI::TileOwner), adds by goods: stone (2) the
//              StoneAmountAt if (x+2, y+2) is the player's too; logs (3) 1
//              for a tree if (x+1, y+1) is the player's; coal (5), gold ore
//              (6), iron ore (7), sulfur (25), gems (28) the
//              ResourceAmountAt where ResourceKindAt is 0x10, 0x30, 0x20,
//              0x50, 0x40; grain (9) 1 for a grass-class tile without tile
//              flags 0x01 / 0x08 whose TileSlope is at most 7, wine (21) the
//              same with slope at least 7 (slope 7 counts for both), rice
//              (27) the same as grain on terrain 0x15; water (26) 1 for a
//              stream tile (class 0x60). Any other goods: DebugLog
//              "RAINER - DEFAULT IN SWITCH CountResourceInOwnersLand" for
//              every owned tile (fish (8) included). AX = the sum's low
//              word; EAX's upper half is that of cx * 8 + 8 (the loop
//              bound left in EAX).
//   0x00599f50 AI::CountPlayersResourcesAround(const u16* cx, const u16*
//              cy, const u16* player, const u16* goods) fastcall, ret 8:
//              over the 3 x 3 cells around (cx - 1 .. + 1 outer, cy - 1 ..
//              + 1 inner, 16-bit signed, skipped if negative or not below
//              the grid's width / height, read from g_aiLandEvaluation at
//              the start): asks GridCell 0x554700 for the cell and, if the
//              cell's byte for the goods is not 0 (stone +3, logs +4, coal
//              +6, gold ore +7, iron ore +5, grain +0xb, wine +0xc, sulfur
//              +8, water +2, rice +0xd, gems +9), adds the low word of
//              CountResourceInOwnersLand(cell, player, goods). Other goods:
//              DebugLog "RAINER - DEFAULT IN SWITCH
//              CountPlayersResourcesAround" for each cell on the grid.
//              EAX = the sum (32 bits). The goods and player words are read
//              again for every cell, as in the original.

#include "game/ai/ai_layout.h"
#include "game/debug_log.h"
#include "game/enums/goods.h"
#include "game/enums/resources.h"
#include "game/enums/terrain.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/texts.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using namespace ai;
using layout::kMapRowTiles;

namespace {

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kTileOwner = 0x00599570;              // AI::TileOwner (fastcall, pointers)
constexpr std::uintptr_t kStoneAmountAt = 0x00599910;          // AI::StoneAmountAt (fastcall, pointers)
constexpr std::uintptr_t kIsTreeAt = 0x00599950;               // AI::IsTreeAt (fastcall, pointers)
constexpr std::uintptr_t kOwnersLandHasResource = 0x00599df0;  // AI::OwnersLandHasResource
constexpr std::uintptr_t kGridCell = 0x00554700;               // AI::CEvaLand::GridCell (thiscall)
constexpr std::uintptr_t kTileCentre = 0x00554880;             // AI::TileCentre_554880 (thiscall)

constexpr std::uint16_t kGoodsStone = 2;
constexpr std::uint16_t kGoodsLog = 3;
constexpr std::uint16_t kGoodsWater = 26;
constexpr std::int32_t kCellTiles = 8;
constexpr unsigned kCellShift = 3;  // tile -> cell: / 8

// The callees read words through these pointers; the original passes dword
// stack copies (same low word).
using CoordFn = std::uint32_t(__fastcall*)(const std::uint32_t* x, const std::uint32_t* y);
using HasResourceFn = std::uint32_t(__fastcall*)(std::uint32_t cellX, std::uint32_t cellY, std::uint32_t player,
                                                 std::uint32_t goods);
using GridCellFn = const AiGridCell*(__fastcall*)(std::uint32_t self, void* edx, std::int32_t x, std::int32_t y);
using TileCentreFn = std::uint32_t(__fastcall*)(std::uint32_t self, void* edx, std::int32_t x, std::int32_t y,
                                                std::uint16_t* outX, std::uint16_t* outY);

bool OwnedBy(std::int32_t x, std::int32_t y, std::uint16_t player) {
    const std::uint32_t copyX = static_cast<std::uint32_t>(x), copyY = static_cast<std::uint32_t>(y);
    return static_cast<std::uint16_t>(reinterpret_cast<CoordFn>(kTileOwner)(&copyX, &copyY)) == player;
}

// The resource test of one owned tile (0x599e5e..0x599f05).
bool TileHasResource(std::int32_t x, std::int32_t y, std::uint16_t player, std::uint16_t goods) {
    const std::uint32_t copyX = static_cast<std::uint32_t>(x), copyY = static_cast<std::uint32_t>(y);
    switch (goods) {
    case kGoodsStone:
        // the stone pile reaches over (x+2, y+2): that tile must be the player's too
        return static_cast<std::uint16_t>(reinterpret_cast<CoordFn>(kStoneAmountAt)(&copyX, &copyY)) != 0 &&
               OwnedBy(x + 2, y + 2, player);
    case kGoodsLog:
        return reinterpret_cast<CoordFn>(kIsTreeAt)(&copyX, &copyY) != 0 && OwnedBy(x + 1, y + 1, player);
    case kGoodsWater: {
        const CGameData* world = g_aiWorld;
        return ((&world->tiles[0][0])[x * kMapRowTiles + y].terrain & kTerrainClassMask) == kTerrainClassStream;
    }
    default:
        return false;
    }
}

}  // namespace

using WorkCellFn = std::uint32_t(__fastcall*)(const layout::Building* building);
using FindCellFn = std::uint32_t(__fastcall*)(std::uint16_t* x, std::uint16_t* y, std::uint32_t player,
                                              std::uint32_t goods);

std::uint32_t __fastcall AI_OwnersLandHasResource(std::uint32_t cellX, std::uint32_t cellY, std::uint32_t player,
                                                  std::uint32_t goods);
std::uint32_t __fastcall AI_WorkCellHasTrees(const layout::Building* building);
std::uint32_t __fastcall AI_WorkCellHasStones(const layout::Building* building);
std::uint32_t __fastcall AI_FindResourceCellAround(std::uint16_t* x, std::uint16_t* y, std::uint32_t player,
                                                   std::uint32_t goods);

S3RE_HOOK(0x00599df0, AI_OwnersLandHasResource, HasResourceFn, Original_AI_OwnersLandHasResource,
          AI_OwnersLandHasResource);
S3RE_HOOK(0x0059a380, AI_WorkCellHasTrees, WorkCellFn, Original_AI_WorkCellHasTrees, AI_WorkCellHasTrees);
S3RE_HOOK(0x0059a8b0, AI_WorkCellHasStones, WorkCellFn, Original_AI_WorkCellHasStones, AI_WorkCellHasStones);
using CountAroundFn = std::uint32_t(__fastcall*)(const std::uint16_t* cellX, const std::uint16_t* cellY,
                                                 const std::uint16_t* player, const std::uint16_t* goods);

std::uint32_t __fastcall AI_CountResourceInOwnersLand(std::uint32_t cellX, std::uint32_t cellY, std::uint32_t player,
                                                      std::uint32_t goods);
std::uint32_t __fastcall AI_CountPlayersResourcesAround(const std::uint16_t* cellX, const std::uint16_t* cellY,
                                                        const std::uint16_t* player, const std::uint16_t* goods);

S3RE_HOOK(0x00599a30, AI_CountResourceInOwnersLand, HasResourceFn, Original_AI_CountResourceInOwnersLand,
          AI_CountResourceInOwnersLand);
S3RE_HOOK(0x00599f50, AI_CountPlayersResourcesAround, CountAroundFn, Original_AI_CountPlayersResourcesAround,
          AI_CountPlayersResourcesAround);
S3RE_HOOK(0x0059a120, AI_FindResourceCellAround, FindCellFn, Original_AI_FindResourceCellAround,
          AI_FindResourceCellAround);

// Purpose: Whether a computer player's land in a grid cell holds stone,
// trees or water, so that a stonecutter, woodcutter or well placed there
// would have work.
std::uint32_t __fastcall AI_OwnersLandHasResource(std::uint32_t cellX, std::uint32_t cellY, std::uint32_t player,
                                                  std::uint32_t goods) {
    const std::int32_t left = static_cast<std::int32_t>(cellX & 0xffff) * kCellTiles;
    const std::int32_t top = static_cast<std::int32_t>(cellY & 0xffff) * kCellTiles;
    const auto owner = static_cast<std::uint16_t>(player);
    const auto kind = static_cast<std::uint16_t>(goods);
    for (std::int32_t y = top; y < top + kCellTiles; ++y) {
        for (std::int32_t x = left; x < left + kCellTiles; ++x) {
            if (OwnedBy(x, y, owner) && TileHasResource(x, y, owner, kind)) {
                return 1;
            }
        }
    }
    return 0;
}

namespace {

std::uint32_t WorkCellHas(const layout::Building* building, std::uint32_t goods) {
    const std::uint32_t cellX = static_cast<std::uint16_t>(building->work_x >> kCellShift);
    const std::uint32_t cellY = static_cast<std::uint16_t>(building->work_y >> kCellShift);
    return reinterpret_cast<HasResourceFn>(kOwnersLandHasResource)(cellX, cellY, building->owner, goods);
}

}  // namespace

// Purpose: Whether a computer player's woodcutter still has trees on its
// own land in its work area's cell (before the AI looks for a new area).
std::uint32_t __fastcall AI_WorkCellHasTrees(const layout::Building* building) {
    return WorkCellHas(building, kGoodsLog);
}

// Purpose: Whether a computer player's stonecutter still has stone on its
// own land in its work area's cell.
std::uint32_t __fastcall AI_WorkCellHasStones(const layout::Building* building) {
    return WorkCellHas(building, kGoodsStone);
}

// Purpose: Finds a grid cell next to a tile where the computer player's land
// has stone or trees, and moves the tile to that cell's centre (probably a
// new work area for a stonecutter or woodcutter).
std::uint32_t __fastcall AI_FindResourceCellAround(std::uint16_t* x, std::uint16_t* y, std::uint32_t player,
                                                   std::uint32_t goods) {
    const std::uint32_t land = g_aiLandEvaluation;
    const auto* evaluation = reinterpret_cast<const AiLandEvaluation*>(land);
    const std::uint32_t width = evaluation->width, height = evaluation->height;
    const auto firstX = static_cast<std::uint16_t>((*x >> kCellShift) - 1);
    const auto firstY = static_cast<std::uint16_t>((*y >> kCellShift) - 1);
    for (std::uint16_t i = 0; i < 3; ++i) {
        const auto cellX = static_cast<std::int16_t>(firstX + i);
        for (std::uint16_t j = 0; j < 3; ++j) {
            const auto cellY = static_cast<std::int16_t>(firstY + j);
            if (cellX < 0 || static_cast<std::uint32_t>(cellX) >= width || cellY < 0 ||
                static_cast<std::uint32_t>(cellY) >= height) {
                continue;
            }
            const AiGridCell* cell = reinterpret_cast<GridCellFn>(kGridCell)(land, nullptr, cellX, cellY);
            std::uint8_t present = 0;
            switch (static_cast<std::uint16_t>(goods)) {
            case kGoodsStone: present = cell->stones; break;
            case kGoodsLog: present = cell->trees; break;
            default: continue;
            }
            if (present == 0) {
                continue;
            }
            if (reinterpret_cast<HasResourceFn>(kOwnersLandHasResource)(static_cast<std::uint16_t>(cellX),
                                                                        static_cast<std::uint16_t>(cellY), player,
                                                                        goods) != 0) {
                reinterpret_cast<TileCentreFn>(kTileCentre)(g_aiLandEvaluation, nullptr, cellX, cellY, x, y);
                return 1;
            }
        }
    }
    return 0;
}

namespace {

constexpr std::uintptr_t kResourceAmountAt = 0x00599990;  // AI::ResourceAmountAt (fastcall, pointers)
constexpr std::uintptr_t kResourceKindAt = 0x005999e0;    // AI::ResourceKindAt (fastcall, pointers)
constexpr std::uintptr_t kTileSlope = 0x004faa70;         // CGameData::TileSlope (thiscall)
constexpr std::uintptr_t kCountResourceInOwnersLand = 0x00599a30;

using TileSlopeFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);

constexpr std::uint32_t kFieldSlope = 7;  // grain and rice up to 7, wine from 7
constexpr std::uint8_t kTileNotFree = layout::kTileBlocked | layout::kTileBuildingFootprint;

std::uint32_t AskTile(std::uintptr_t query, std::int32_t x, std::int32_t y) {
    const std::uint32_t copyX = static_cast<std::uint32_t>(x), copyY = static_cast<std::uint32_t>(y);
    return reinterpret_cast<CoordFn>(query)(&copyX, &copyY);
}

// The amount of a mine resource of the given kind on the tile, else 0.
std::uint32_t ResourceOfKind(std::int32_t x, std::int32_t y, std::uint8_t kind) {
    if (static_cast<std::uint16_t>(AskTile(kResourceKindAt, x, y)) != kind) {
        return 0;
    }
    return static_cast<std::uint16_t>(AskTile(kResourceAmountAt, x, y));
}

// Field land (0x599cd5..0x599d43): a grass-class tile (or swampy grass
// 0x15 for rice) without tile flags 0x01 / 0x08 whose slope is at most 7
// (grain, rice) or at least 7 (wine).
bool IsFieldLand(std::int32_t x, std::int32_t y, bool swampyGrass, bool steep) {
    const CGameData* world = g_aiWorld;
    const layout::Tile& tile = (&world->tiles[0][0])[x * kMapRowTiles + y];
    const bool ground = swampyGrass ? tile.terrain == kTerrainSwampyGrass
                                    : (tile.terrain & kTerrainClassMask) == kTerrainClassGrass;
    if (!ground || (tile.flags & kTileNotFree) != 0) {
        return false;
    }
    const std::uint32_t slope = reinterpret_cast<TileSlopeFn>(kTileSlope)(
        g_aiWorld, nullptr, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    return steep ? slope >= kFieldSlope : slope <= kFieldSlope;
}

// What one tile owned by the player adds (the switch at 0x599aab, jump
// table 0x599d9c).
std::uint32_t TileResource(std::int32_t x, std::int32_t y, std::uint16_t player, std::uint16_t goods) {
    switch (goods) {
    case Value(Good::Stone): {
        // the stone pile reaches over (x+2, y+2): that tile must be the player's too
        const auto amount = static_cast<std::uint16_t>(AskTile(kStoneAmountAt, x, y));
        return amount != 0 && OwnedBy(x + 2, y + 2, player) ? amount : 0;
    }
    case Value(Good::Log): return AskTile(kIsTreeAt, x, y) != 0 && OwnedBy(x + 1, y + 1, player) ? 1 : 0;
    case Value(Good::Coal): return ResourceOfKind(x, y, kResourceCoal);
    case Value(Good::GoldOre): return ResourceOfKind(x, y, kResourceGold);
    case Value(Good::IronOre): return ResourceOfKind(x, y, kResourceIron);
    case Value(Good::Sulfur): return ResourceOfKind(x, y, kResourceSulfur);
    case Value(Good::Gems): return ResourceOfKind(x, y, kResourceGems);
    case Value(Good::Grain): return IsFieldLand(x, y, /*swampyGrass=*/false, /*steep=*/false) ? 1 : 0;
    case Value(Good::Wine): return IsFieldLand(x, y, /*swampyGrass=*/false, /*steep=*/true) ? 1 : 0;
    case Value(Good::Rice): return IsFieldLand(x, y, /*swampyGrass=*/true, /*steep=*/false) ? 1 : 0;
    case Value(Good::Water): {
        const CGameData* world = g_aiWorld;
        const std::uint8_t terrain = (&world->tiles[0][0])[x * kMapRowTiles + y].terrain;
        return (terrain & kTerrainClassMask) == kTerrainClassStream ? 1 : 0;
    }
    default: GameDebugLog(texts::kAiCountResourceDefault); return 0;
    }
}

}  // namespace

// Purpose: How much of a resource (stone, trees, ore, field land, water) a
// computer player's own land in a grid cell holds, to rate places for the
// buildings that use it.
std::uint32_t __fastcall AI_CountResourceInOwnersLand(std::uint32_t cellX, std::uint32_t cellY, std::uint32_t player,
                                                      std::uint32_t goods) {
    const std::int32_t left = static_cast<std::int32_t>(cellX & 0xffff) * kCellTiles;
    const std::int32_t top = static_cast<std::int32_t>(cellY & 0xffff) * kCellTiles;
    const auto owner = static_cast<std::uint16_t>(player);
    const auto kind = static_cast<std::uint16_t>(goods);
    std::uint32_t sum = 0;
    for (std::int32_t y = top; y < top + kCellTiles; ++y) {
        for (std::int32_t x = left; x < left + kCellTiles; ++x) {
            if (OwnedBy(x, y, owner)) {
                sum += TileResource(x, y, owner, kind);
            }
        }
    }
    // AX = the sum; the upper half is that of the inner loop's bound (left + 8)
    const auto bound = static_cast<std::uint32_t>(left + kCellTiles);
    return (bound & 0xffff0000u) | (sum & 0xffff);
}

namespace {

// AiGridCell's byte that says whether the cell has any of the goods'
// resource (jump table 0x59a0d4); nullptr for goods without one.
const std::uint8_t* PresenceByte(const AiGridCell& cell, std::uint16_t goods) {
    switch (goods) {
    case Value(Good::Stone): return &cell.stones;
    case Value(Good::Log): return &cell.trees;
    case Value(Good::Coal): return &cell.resources[kGridCoal];
    case Value(Good::GoldOre): return &cell.resources[kGridGold];
    case Value(Good::IronOre): return &cell.resources[kGridIron];
    case Value(Good::Grain): return &cell.grain_land_percent;
    case Value(Good::Wine): return &cell.wine_land_percent;
    case Value(Good::Sulfur): return &cell.resources[kGridSulfur];
    case Value(Good::Water): return &cell.stream_tiles;
    case Value(Good::Rice): return &cell.rice_land_percent;
    case Value(Good::Gems): return &cell.resources[kGridGems];
    default: return nullptr;
    }
}

}  // namespace

// Purpose: How much of a resource a computer player's land holds in and
// around a grid cell (the 3 x 3 cells), skipping cells the land evaluation
// says have none; used to rate building places.
std::uint32_t __fastcall AI_CountPlayersResourcesAround(const std::uint16_t* cellX, const std::uint16_t* cellY,
                                                        const std::uint16_t* player, const std::uint16_t* goods) {
    const auto* evaluation = reinterpret_cast<const AiLandEvaluation*>(g_aiLandEvaluation.get());
    const std::uint32_t width = evaluation->width, height = evaluation->height;
    const auto firstX = static_cast<std::uint16_t>(*cellX - 1);
    const auto firstY = static_cast<std::uint16_t>(*cellY - 1);
    std::uint32_t sum = 0;
    for (std::uint16_t i = 0; i < 3; ++i) {
        const auto x = static_cast<std::int16_t>(firstX + i);
        for (std::uint16_t j = 0; j < 3; ++j) {
            const auto y = static_cast<std::int16_t>(firstY + j);
            if (x < 0 || static_cast<std::uint32_t>(x) >= width || y < 0 || static_cast<std::uint32_t>(y) >= height) {
                continue;
            }
            const AiGridCell* cell = reinterpret_cast<GridCellFn>(kGridCell)(g_aiLandEvaluation, nullptr, x, y);
            const std::uint16_t kind = *goods;  // read again for every cell, as the original does
            const std::uint8_t* present = PresenceByte(*cell, kind);
            if (present == nullptr) {
                GameDebugLog(texts::kAiCountAroundDefault);
                continue;
            }
            if (*present == 0) {
                continue;
            }
            sum += static_cast<std::uint16_t>(reinterpret_cast<HasResourceFn>(kCountResourceInOwnersLand)(
                static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y), *player, kind));
        }
    }
    return sum;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// A 64 x 64 test map (AI map size 40..72, so some cells run off it) whose
// tiles belong to players 0..2 or nobody, with trees, stones and water
// terrain; a 9 x 9 grid of cells with random presence bytes. Arguments with
// random upper halves where the original ignores them.

namespace {

constexpr std::uint32_t kTestMap = 80;
constexpr std::uint32_t kGridSide = 9;

struct CellQueries {
    HasResourceFn hasResource;
    WorkCellFn trees, stones;
    FindCellFn findCell;
};

CellQueries OriginalCellQueries() {
    return {Original_AI_OwnersLandHasResource, Original_AI_WorkCellHasTrees, Original_AI_WorkCellHasStones,
            Original_AI_FindResourceCellAround};
}

CellQueries OurCellQueries() {
    return {&AI_OwnersLandHasResource, &AI_WorkCellHasTrees, &AI_WorkCellHasStones, &AI_FindResourceCellAround};
}

struct SavedGlobals {
    CGameData* world = g_aiWorld;
    std::uint32_t land = g_aiLandEvaluation;
    std::uint16_t sizeX = g_aiMapSizeX, sizeY = g_aiMapSizeY;
    ~SavedGlobals() {
        g_aiWorld = world;
        g_aiLandEvaluation = land;
        g_aiMapSizeX = sizeX;
        g_aiMapSizeY = sizeY;
    }
};

void SetUpMap(test::Rng& rng, CGameData* world, std::vector<AiGridCell>& cells) {
    const std::uint32_t owned = 50 + rng.Below(50);  // percent of tiles owned by player 0..2
    for (std::uint32_t x = 0; x < kTestMap; ++x) {
        for (std::uint32_t y = 0; y < kTestMap; ++y) {
            layout::Tile& tile = world->tiles[x][y];
            tile.owner = static_cast<std::uint8_t>(rng.Below(100) < owned ? rng.Below(3) : 0xff);
            const std::uint32_t roll = rng.Below(30);
            tile.object = static_cast<std::uint8_t>(roll == 0   ? 0x44 + rng.Below(16)    // tree
                                                    : roll == 1 ? 0x73 + rng.Below(12)    // stones
                                                                : rng.Below(256));
            tile.terrain = static_cast<std::uint8_t>(rng.Chance(1, 30) ? 0x60 + rng.Below(16) : rng.Below(256));
        }
    }
    for (AiGridCell& cell : cells) {
        cell.stones = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : rng.Next32());
        cell.trees = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : rng.Next32());
    }
}

std::uint32_t RandomGoods(test::Rng& rng) {
    constexpr std::uint16_t kGoods[] = {kGoodsStone, kGoodsLog, kGoodsWater, 9, 0};
    const std::uint32_t upper = rng.Chance(1, 4) ? rng.Next32() & 0xffff0000u : 0;
    return kGoods[rng.Below(sizeof(kGoods) / sizeof(kGoods[0]))] | upper;
}

void CompareCellQueries(test::Context& ctx, const CellQueries& candidate, int cases) {
    const CellQueries original = OriginalCellQueries();
    SavedGlobals saved;
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    std::vector<AiGridCell> cells(kGridSide * kGridSide);
    AiLandEvaluation land{};
    land.cells = cells.data();
    g_aiWorld = world;
    g_aiLandEvaluation = reinterpret_cast<std::uint32_t>(&land);
    int found = 0;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        if (i % 25 == 0) {
            SetUpMap(rng, world, cells);
        }
        g_aiMapSizeX = static_cast<std::uint16_t>(40 + rng.Below(33));
        g_aiMapSizeY = static_cast<std::uint16_t>(40 + rng.Below(33));
        land.width = rng.Below(kGridSide + 1);
        land.height = rng.Below(kGridSide + 1);
        const std::uint32_t player = rng.Below(3) | (rng.Chance(1, 4) ? rng.Next32() & 0xffff0000u : 0);
        const std::uint32_t goods = RandomGoods(rng);
        const int op = static_cast<int>(rng.Below(4));
        std::uint32_t ra = 0, rb = 0;
        bool same = true;
        switch (op) {
        case 0: {
            const std::uint32_t cellX = rng.Below(kGridSide) | (rng.Chance(1, 4) ? rng.Next32() & 0xffff0000u : 0);
            const std::uint32_t cellY = rng.Below(kGridSide) | (rng.Chance(1, 4) ? rng.Next32() & 0xffff0000u : 0);
            ra = original.hasResource(cellX, cellY, player, goods);
            rb = candidate.hasResource(cellX, cellY, player, goods);
            break;
        }
        case 1:
        case 2: {
            layout::Building building{};
            building.owner = static_cast<std::uint8_t>(rng.Below(4));
            building.work_x = static_cast<std::uint16_t>(rng.Below(kTestMap));
            building.work_y = static_cast<std::uint16_t>(rng.Below(kTestMap));
            const WorkCellFn fa = op == 1 ? original.trees : original.stones;
            const WorkCellFn fb = op == 1 ? candidate.trees : candidate.stones;
            ra = fa(&building);
            rb = fb(&building);
            break;
        }
        default: {
            std::uint16_t xa = static_cast<std::uint16_t>(rng.Chance(1, 20) ? rng.Next32() : rng.Below(kTestMap));
            std::uint16_t ya = static_cast<std::uint16_t>(rng.Chance(1, 20) ? rng.Next32() : rng.Below(kTestMap));
            std::uint16_t xb = xa, yb = ya;
            ra = original.findCell(&xa, &ya, player, goods);
            rb = candidate.findCell(&xb, &yb, player, goods);
            found += ra != 0 ? 1 : 0;
            same &= S3RE_EXPECT_EQ(xa, xb);
            same &= S3RE_EXPECT_EQ(ya, yb);
            break;
        }
        }
        same &= S3RE_EXPECT_EQ(ra, rb);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d, player %08x, goods %08x", i, op, player, goods);
        }
    }
    if (cases >= 1000 && !S3RE_EXPECT(found >= 20)) {
        ctx.Fail(__FILE__, __LINE__, "  FindResourceCellAround found only %d cells", found);
    }
}

std::uint32_t __fastcall WrongHasResource(std::uint32_t cellX, std::uint32_t cellY, std::uint32_t player,
                                          std::uint32_t goods) {
    // Wrong: stone without the (x+2, y+2) ownership check.
    if (static_cast<std::uint16_t>(goods) != kGoodsStone) {
        return AI_OwnersLandHasResource(cellX, cellY, player, goods);
    }
    const std::int32_t left = static_cast<std::int32_t>(cellX & 0xffff) * kCellTiles;
    const std::int32_t top = static_cast<std::int32_t>(cellY & 0xffff) * kCellTiles;
    for (std::int32_t y = top; y < top + kCellTiles; ++y) {
        for (std::int32_t x = left; x < left + kCellTiles; ++x) {
            const std::uint32_t copyX = static_cast<std::uint32_t>(x), copyY = static_cast<std::uint32_t>(y);
            if (OwnedBy(x, y, static_cast<std::uint16_t>(player)) &&
                static_cast<std::uint16_t>(reinterpret_cast<CoordFn>(kStoneAmountAt)(&copyX, &copyY)) != 0) {
                return 1;
            }
        }
    }
    return 0;
}

std::uint32_t __fastcall WrongFindCell(std::uint16_t* x, std::uint16_t* y, std::uint32_t player,
                                       std::uint32_t goods) {
    // Wrong: ignores the presence bytes.
    const auto* evaluation = reinterpret_cast<const AiLandEvaluation*>(g_aiLandEvaluation.get());
    for (std::int32_t i = -1; i <= 1; ++i) {
        for (std::int32_t j = -1; j <= 1; ++j) {
            const std::int32_t cellX = (*x >> kCellShift) + i, cellY = (*y >> kCellShift) + j;
            if (cellX < 0 || cellY < 0 || static_cast<std::uint32_t>(cellX) >= evaluation->width ||
                static_cast<std::uint32_t>(cellY) >= evaluation->height) {
                continue;
            }
            const auto kind = static_cast<std::uint16_t>(goods);
            if ((kind == kGoodsStone || kind == kGoodsLog) &&
                AI_OwnersLandHasResource(static_cast<std::uint32_t>(cellX), static_cast<std::uint32_t>(cellY),
                                         player, goods) != 0) {
                *x = static_cast<std::uint16_t>(cellX * kCellTiles + 4);
                *y = static_cast<std::uint16_t>(cellY * kCellTiles + 4);
                return 1;
            }
        }
    }
    return 0;
}

std::uint32_t __fastcall WrongWorkCellHasTrees(const layout::Building* building) {
    // Wrong: the stone test.
    return WorkCellHas(building, kGoodsStone);
}

}  // namespace

S3RE_TEST(AiCellResources_MatchOriginal) {
    CompareCellQueries(ctx, OurCellQueries(), 4000);
}

S3RE_TEST(AiCellResources_ComparisonDetectsWrongImplementation) {
    const CellQueries ours = OurCellQueries();
    const CellQueries wrongs[] = {
        {&WrongHasResource, ours.trees, ours.stones, ours.findCell},
        {ours.hasResource, &WrongWorkCellHasTrees, ours.stones, ours.findCell},
        {ours.hasResource, ours.trees, ours.stones, &WrongFindCell},
    };
    for (const CellQueries& wrong : wrongs) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareCellQueries(quiet, wrong, 4000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

// CountResourceInOwnersLand and CountPlayersResourcesAround: the same map
// with heights (TileSlope), tile flags, resources, swampy grass and stream
// terrain added, and random presence bytes in every cell. Compared: EAX and
// the DebugLog calls.

namespace {

struct ResourceCounts {
    HasResourceFn count;
    CountAroundFn around;
};

void AddFieldDetails(test::Rng& rng, CGameData* world, std::vector<AiGridCell>& cells) {
    world->map_size = kTestMap;
    for (std::uint32_t x = 0; x < kTestMap; ++x) {
        for (std::uint32_t y = 0; y < kTestMap; ++y) {
            layout::Tile& tile = world->tiles[x][y];
            const std::uint32_t roll = rng.Below(8);
            if (roll < 3) {
                tile.terrain = static_cast<std::uint8_t>(kTerrainClassGrass | rng.Below(16));
            } else if (roll == 3) {
                tile.terrain = kTerrainSwampyGrass;
            } else if (roll == 4) {
                tile.terrain = static_cast<std::uint8_t>(kTerrainClassStream | rng.Below(16));
            }
            tile.flags = static_cast<std::uint8_t>(rng.Chance(1, 3) ? rng.Below(256) : 0);
            tile.resources = static_cast<std::uint8_t>(rng.Below(256));
            tile.height = static_cast<std::uint8_t>(rng.Below(rng.Chance(1, 2) ? 8 : 30));
        }
    }
    for (AiGridCell& cell : cells) {
        auto* bytes = reinterpret_cast<std::uint8_t*>(&cell);
        for (std::size_t k = 2; k < offsetof(AiGridCell, trees_around); ++k) {
            bytes[k] = static_cast<std::uint8_t>(rng.Chance(1, 3) ? 0 : rng.Next32());
        }
    }
}

std::uint32_t RandomCountGoods(test::Rng& rng) {
    constexpr std::uint16_t kGoods[] = {2, 3, 5, 6, 7, 9, 21, 25, 26, 27, 28, 8, 0, 4, 29, 0x102};
    const std::uint32_t upper = rng.Chance(1, 4) ? rng.Next32() & 0xffff0000u : 0;
    return kGoods[rng.Below(sizeof(kGoods) / sizeof(kGoods[0]))] | upper;
}

void CompareResourceCounts(test::Context& ctx, const ResourceCounts& candidate, int cases) {
    const ResourceCounts original = {Original_AI_CountResourceInOwnersLand, Original_AI_CountPlayersResourcesAround};
    SavedGlobals saved;
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    std::vector<AiGridCell> cells(kGridSide * kGridSide);
    AiLandEvaluation land{};
    land.cells = cells.data();
    g_aiWorld = world;
    g_aiLandEvaluation = reinterpret_cast<std::uint32_t>(&land);
    DebugLogRecorder logs;
    int nonZero = 0;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        if (i % 25 == 0) {
            SetUpMap(rng, world, cells);
            AddFieldDetails(rng, world, cells);
        }
        g_aiMapSizeX = static_cast<std::uint16_t>(40 + rng.Below(41));
        g_aiMapSizeY = static_cast<std::uint16_t>(40 + rng.Below(41));
        land.width = rng.Below(kGridSide + 1);
        land.height = rng.Below(kGridSide + 1);
        const std::uint32_t player = rng.Below(3) | (rng.Chance(1, 4) ? rng.Next32() & 0xffff0000u : 0);
        const std::uint32_t goods = RandomCountGoods(rng);
        const bool around = rng.Chance(1, 2);
        std::uint32_t ra = 0, rb = 0;
        if (!around) {
            const std::uint32_t cellX = rng.Below(kGridSide) | (rng.Chance(1, 4) ? rng.Next32() & 0xffff0000u : 0);
            const std::uint32_t cellY = rng.Below(kGridSide) | (rng.Chance(1, 4) ? rng.Next32() & 0xffff0000u : 0);
            ra = original.count(cellX, cellY, player, goods);
            const std::vector<DebugLogCall> logsA = logs.Take();
            rb = candidate.count(cellX, cellY, player, goods);
            const std::vector<DebugLogCall> logsB = logs.Take();
            if (!S3RE_EXPECT(logsA == logsB)) {
                ctx.Fail(__FILE__, __LINE__, "  DebugLog calls: %u vs %u", static_cast<unsigned>(logsA.size()),
                         static_cast<unsigned>(logsB.size()));
            }
        } else {
            const auto cellX = static_cast<std::uint16_t>(rng.Chance(1, 20) ? rng.Next32() : rng.Below(kGridSide + 1));
            const auto cellY = static_cast<std::uint16_t>(rng.Chance(1, 20) ? rng.Next32() : rng.Below(kGridSide + 1));
            const auto owner = static_cast<std::uint16_t>(player);
            const auto kind = static_cast<std::uint16_t>(goods);
            ra = original.around(&cellX, &cellY, &owner, &kind);
            const std::vector<DebugLogCall> logsA = logs.Take();
            rb = candidate.around(&cellX, &cellY, &owner, &kind);
            const std::vector<DebugLogCall> logsB = logs.Take();
            if (!S3RE_EXPECT(logsA == logsB)) {
                ctx.Fail(__FILE__, __LINE__, "  DebugLog calls: %u vs %u", static_cast<unsigned>(logsA.size()),
                         static_cast<unsigned>(logsB.size()));
            }
        }
        nonZero += (ra & 0xffff) != 0 ? 1 : 0;
        if (!S3RE_EXPECT_EQ(ra, rb)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, around %d, player %08x, goods %08x", i, around ? 1 : 0, player,
                     goods);
        }
    }
    if (cases >= 1000 && !S3RE_EXPECT(nonZero >= cases / 5)) {
        ctx.Fail(__FILE__, __LINE__, "  only %d non-zero counts", nonZero);
    }
}

// Wrong: slope 7 doesn't count as grain land.
std::uint32_t __fastcall WrongCount(std::uint32_t cellX, std::uint32_t cellY, std::uint32_t player,
                                    std::uint32_t goods) {
    if (static_cast<std::uint16_t>(goods) != Value(Good::Grain)) {
        return AI_CountResourceInOwnersLand(cellX, cellY, player, goods);
    }
    const std::int32_t left = static_cast<std::int32_t>(cellX & 0xffff) * kCellTiles;
    const std::int32_t top = static_cast<std::int32_t>(cellY & 0xffff) * kCellTiles;
    std::uint32_t sum = 0;
    for (std::int32_t y = top; y < top + kCellTiles; ++y) {
        for (std::int32_t x = left; x < left + kCellTiles; ++x) {
            if (OwnedBy(x, y, static_cast<std::uint16_t>(player)) && IsFieldLand(x, y, false, false) &&
                reinterpret_cast<TileSlopeFn>(kTileSlope)(g_aiWorld, nullptr, x, y) < kFieldSlope) {
                ++sum;
            }
        }
    }
    return ((static_cast<std::uint32_t>(left) + kCellTiles) & 0xffff0000u) | sum;
}

// Wrong: ignores the presence bytes.
std::uint32_t __fastcall WrongAround(const std::uint16_t* cellX, const std::uint16_t* cellY,
                                     const std::uint16_t* player, const std::uint16_t* goods) {
    auto* evaluation = reinterpret_cast<AiLandEvaluation*>(g_aiLandEvaluation.get());
    std::vector<AiGridCell> saved(evaluation->cells, evaluation->cells + kGridSide * kGridSide);
    for (std::size_t k = 0; k < kGridSide * kGridSide; ++k) {
        std::memset(&evaluation->cells[k].stream_tiles, 1, offsetof(AiGridCell, trees_around) - 2);
    }
    const std::uint32_t result = AI_CountPlayersResourcesAround(cellX, cellY, player, goods);
    std::copy(saved.begin(), saved.end(), evaluation->cells);
    return result;
}

}  // namespace

S3RE_TEST(AiResourceCounts_MatchOriginal) {
    CompareResourceCounts(ctx, {&AI_CountResourceInOwnersLand, &AI_CountPlayersResourcesAround}, 3000);
}

S3RE_TEST(AiResourceCounts_ComparisonDetectsWrongImplementation) {
    const ResourceCounts wrongs[] = {
        {&WrongCount, &AI_CountPlayersResourcesAround},
        {&AI_CountResourceInOwnersLand, &WrongAround},
    };
    for (const ResourceCounts& wrong : wrongs) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareResourceCounts(quiet, wrong, 3000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}


}  // namespace s3re::game
