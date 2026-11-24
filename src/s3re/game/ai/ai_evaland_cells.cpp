// The land evaluation grid's per-cell summaries (computer players A5,
// session 54; Rainer\Cevaland.cpp, re/computer_players.md). The grid has one
// AiGridCell per 8 x 8 tiles; cell (cx, cy) covers the tiles cx * 8 ..
// cx * 8 + 7, cy * 8 .. cy * 8 + 7. All four are thiscall on the CEvaLand
// object (AiLandEvaluation) and read the map through g_aiWorld (0x7e3408).
//
//   0x005540f0 AI::CEvaLand::EvaluateCell(cx, cy), ret 8: tallies the cell's
//              tiles (y outer, x inner; coordinates are dwords, passed to
//              the callees as pointers to dword copies) and writes the
//              cell's bytes +0x00..+0x0e (AiGridCell): the flags word, and
//              by terrain class (Tile::terrain & 0xf0):
//              - water (0): the fish (AI::ResourceKindAt 0x5999e0 is 0:
//                AI::ResourceAmountAt 0x599990 is added);
//              - grass (0x10): the grass count; a stone tile adds its
//                AI::StoneAmountAt 0x599910 to the stones, else a tree
//                (AI::IsTreeAt 0x599950) counts as a tree; both then count
//                as grain land (CGameData::TileSlope 0x4faa70 < 7) or wine
//                land (>= 7); a tile without either is skipped if its flags
//                have 0x01 or 0x08 (blocked, building footprint), counts as
//                rice land if its terrain is 0x15 (swampy grass) with slope
//                < 7, else as grain or wine land like the others;
//              - mountain (0x20): ResourceAmountAt is added to the
//                resource of its ResourceKindAt (0x10 coal, 0x20 iron, 0x30
//                gold, 0x40 gems, 0x50 sulfur; jump table 0x5544c8);
//              - stream (0x60): the stream tiles.
//              The amounts (stones, the five mine resources, fish) are
//              stored as sum / 2, or 250 if the sum is above 250 (so 250
//              gives 125 and 251 gives 250: a quirk, kept); trees and
//              stream tiles as the count, at most 250; the four land
//              counts as count * 100 / tiles_per_cell (+0x14c, unsigned
//              division; truncated to a byte). The water and mountain tile
//              counts the original keeps are never stored. EAX = the last
//              quotient (grass percentage before truncation).
//   0x005540a0 AI::CEvaLand::EvaluateAllCells(): for x < width (outer), y <
//              height (inner): AI::CEvaLand::GridCell 0x554700(x, y) (its
//              result unused) and EvaluateCell(x, y); EAX = width.
//   0x005547a0 AI::CEvaLand::UpdateNextCells(): moves the cursor (+0x150,
//              +0x154) on by one cell at a time (x first, wrapping to the
//              next row and from the last row to the first), at most 64
//              cells, until 4 cells whose centre tile has an owner
//              (AI::CellCentreOwner 0x554850 is neither 0xff nor 0xffff)
//              were re-evaluated: EvaluateCell and GridSumAround 0x554530
//              on each. EAX: left over, not used by the caller.
//   0x005548b0 AI::CEvaLand::ReevaluateAroundTile(x, y), ret 8 (low words
//              used): EvaluateCell on the 3 x 3 cells around the tile's
//              cell (x / 8 - 1 .. + 1 outer, y / 8 - 1 .. + 1 inner, as
//              16-bit signed values; skipped if negative or not below
//              width / height), then GridSumAround on the same cells. EAX:
//              left over (depends on the caller's EBP), not used by any of
//              the callers (0x554980, 0x560e00, 0x59bc20 overwrite it).

#include "game/ai/ai_layout.h"
#include "game/enums/resources.h"
#include "game/enums/terrain.h"
#include "game/globals.h"
#include "game/layout.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace s3re::game {

using namespace ai;
using layout::kMapRowTiles;

namespace {

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kGridCell = 0x00554700;          // AI::CEvaLand::GridCell (thiscall)
constexpr std::uintptr_t kEvaluateCell = 0x005540f0;      // AI::CEvaLand::EvaluateCell (thiscall)
constexpr std::uintptr_t kGridSumAround = 0x00554530;     // AI::CEvaLand::GridSumAround (thiscall)
constexpr std::uintptr_t kCellCentreOwner = 0x00554850;   // AI::CellCentreOwner (thiscall)
constexpr std::uintptr_t kStoneAmountAt = 0x00599910;     // AI::StoneAmountAt (fastcall, pointers)
constexpr std::uintptr_t kIsTreeAt = 0x00599950;          // AI::IsTreeAt (fastcall, pointers)
constexpr std::uintptr_t kResourceAmountAt = 0x00599990;  // AI::ResourceAmountAt (fastcall, pointers)
constexpr std::uintptr_t kResourceKindAt = 0x005999e0;    // AI::ResourceKindAt (fastcall, pointers)
constexpr std::uintptr_t kTileSlope = 0x004faa70;         // CGameData::TileSlope (thiscall)

using CoordFn = std::uint32_t(__fastcall*)(const std::uint32_t* x, const std::uint32_t* y);
using TileSlopeFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);
using CellCallFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t x, std::uint32_t y);

constexpr std::uint32_t kCellTiles = 8;
constexpr unsigned kCellShift = 3;           // tile -> cell: / 8
constexpr std::uint32_t kSteepSlope = 7;     // TileSlope from here on: wine land, below: grain land
constexpr std::uint32_t kStoredMaximum = 250;
constexpr std::uint8_t kTileNotFree = layout::kTileBlocked | layout::kTileBuildingFootprint;
constexpr std::uint16_t kCellFlagsStart = kGridCellNoWater | kGridCellNoLand;
constexpr std::uint32_t kMaxCellsVisited = 64;  // UpdateNextCells
constexpr std::uint32_t kMaxCellsUpdated = 4;
constexpr std::uint16_t kNoOwner8 = 0xff;
constexpr std::uint16_t kNoOwner16 = 0xffff;    // CellCentreOwner off the map

// The callees read words through these pointers; the original passes
// pointers to dword copies on its stack.
std::uint32_t AskTile(std::uintptr_t query, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t copyX = x, copyY = y;
    return reinterpret_cast<CoordFn>(query)(&copyX, &copyY);
}

// What EvaluateCell counts over a cell's 64 tiles.
struct CellTally {
    std::uint32_t resources[6] = {};  // AiGridCell::resources order (iron, coal, gold, sulfur, gems, fish)
    std::uint32_t stones = 0;
    std::uint32_t trees = 0;
    std::uint32_t stream_tiles = 0;
    std::uint32_t grain_land = 0;
    std::uint32_t wine_land = 0;
    std::uint32_t rice_land = 0;
    std::uint32_t grass = 0;
};

bool IsSteep(std::uint32_t x, std::uint32_t y) {
    return reinterpret_cast<TileSlopeFn>(kTileSlope)(g_aiWorld, nullptr, x, y) >= kSteepSlope;
}

void CountGrainOrWineLand(CellTally& tally, std::uint32_t x, std::uint32_t y) {
    if (IsSteep(x, y)) {
        ++tally.wine_land;
    } else {
        ++tally.grain_land;
    }
}

// 0x5541ab..0x5542f8: a grass-class tile.
void CountGrassTile(CellTally& tally, const layout::Tile& tile, std::uint32_t x, std::uint32_t y) {
    ++tally.grass;
    const auto stones = static_cast<std::uint16_t>(AskTile(kStoneAmountAt, x, y));
    if (stones != 0) {
        tally.stones += stones;
    } else if (AskTile(kIsTreeAt, x, y) != 0) {
        ++tally.trees;
    } else if ((tile.flags & kTileNotFree) != 0) {
        return;
    } else if (tile.terrain == kTerrainSwampyGrass) {
        if (!IsSteep(x, y)) {
            ++tally.rice_land;
        }
        return;
    }
    // stone and tree tiles count as grain or wine land too
    CountGrainOrWineLand(tally, x, y);
}

// 0x5542fd..0x554389: a mountain tile (both queries are made first).
void CountMountainTile(CellTally& tally, std::uint32_t x, std::uint32_t y) {
    const auto kind = static_cast<std::uint16_t>(AskTile(kResourceKindAt, x, y));
    const auto amount = static_cast<std::uint16_t>(AskTile(kResourceAmountAt, x, y));
    switch (kind) {
    case kResourceIron: tally.resources[kGridIron] += amount; break;
    case kResourceCoal: tally.resources[kGridCoal] += amount; break;
    case kResourceGold: tally.resources[kGridGold] += amount; break;
    case kResourceSulfur: tally.resources[kGridSulfur] += amount; break;
    case kResourceGems: tally.resources[kGridGems] += amount; break;
    default: break;  // 0xffff off the map
    }
}

// 0x5541e6..0x554233: a water tile.
void CountWaterTile(CellTally& tally, std::uint32_t x, std::uint32_t y) {
    if (static_cast<std::uint16_t>(AskTile(kResourceKindAt, x, y)) == kResourceFish) {
        tally.resources[kGridFish] += static_cast<std::uint16_t>(AskTile(kResourceAmountAt, x, y));
    }
}

// Amounts: halved, but a sum above 250 is stored as 250 (not 125).
std::uint8_t StoredAmount(std::uint32_t sum) {
    return static_cast<std::uint8_t>(sum > kStoredMaximum ? kStoredMaximum : sum / 2);
}

std::uint8_t StoredCount(std::uint32_t count) {
    return static_cast<std::uint8_t>(count > kStoredMaximum ? kStoredMaximum : count);
}

}  // namespace

using EvaluateCellFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t cellX, std::uint32_t cellY);
using EvaluationFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using AroundTileFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t x, std::uint32_t y);

std::uint32_t __fastcall AI_CEvaLand_EvaluateCell(void* self, void* edx, std::uint32_t cellX, std::uint32_t cellY);
std::uint32_t __fastcall AI_CEvaLand_EvaluateAllCells(void* self, void* edx);
std::uint32_t __fastcall AI_CEvaLand_UpdateNextCells(void* self, void* edx);
std::uint32_t __fastcall AI_CEvaLand_ReevaluateAroundTile(void* self, void* edx, std::uint32_t x, std::uint32_t y);

S3RE_HOOK(0x005540f0, AI_CEvaLand_EvaluateCell, EvaluateCellFn, Original_AI_CEvaLand_EvaluateCell,
          AI_CEvaLand_EvaluateCell);
S3RE_HOOK(0x005540a0, AI_CEvaLand_EvaluateAllCells, EvaluationFn, Original_AI_CEvaLand_EvaluateAllCells,
          AI_CEvaLand_EvaluateAllCells);
S3RE_HOOK(0x005547a0, AI_CEvaLand_UpdateNextCells, EvaluationFn, Original_AI_CEvaLand_UpdateNextCells,
          AI_CEvaLand_UpdateNextCells);
S3RE_HOOK(0x005548b0, AI_CEvaLand_ReevaluateAroundTile, AroundTileFn, Original_AI_CEvaLand_ReevaluateAroundTile,
          AI_CEvaLand_ReevaluateAroundTile);

// Purpose: Summarises an 8 x 8 tile block of the map for the computer
// players: whether it has land and water, how much stone, trees, ore and
// fish it holds, and how much of it suits farms, vineyards and rice fields.
std::uint32_t __fastcall AI_CEvaLand_EvaluateCell(void* self, void* /*edx*/, std::uint32_t cellX,
                                                  std::uint32_t cellY) {
    const auto* evaluation = static_cast<const AiLandEvaluation*>(self);
    auto* cell = reinterpret_cast<AiGridCell*>(reinterpret_cast<CellCallFn>(kGridCell)(self, nullptr, cellX, cellY));
    CellTally tally;
    std::uint16_t flags = kCellFlagsStart;
    const std::uint32_t left = cellX << kCellShift, top = cellY << kCellShift;
    for (std::uint32_t y = top; y < top + kCellTiles; ++y) {
        for (std::uint32_t x = left; x < left + kCellTiles; ++x) {
            const CGameData* world = g_aiWorld;
            const layout::Tile& tile = (&world->tiles[0][0])[x * kMapRowTiles + y];
            const std::uint8_t terrainClass = tile.terrain & kTerrainClassMask;
            if (terrainClass == kTerrainClassWater) {
                flags = static_cast<std::uint16_t>((flags & ~kGridCellNoWater) | kGridCellHasWater);
                CountWaterTile(tally, x, y);
                continue;
            }
            flags = static_cast<std::uint16_t>((flags & ~kGridCellNoLand) | kGridCellHasLand);
            switch (terrainClass) {
            case kTerrainClassGrass: CountGrassTile(tally, tile, x, y); break;
            case kTerrainClassMountain: CountMountainTile(tally, x, y); break;
            case kTerrainClassStream: ++tally.stream_tiles; break;
            default: break;
            }
        }
    }
    // 0x55425f..0x5544ba: the stores, in the original's order
    cell->flags = flags;
    for (std::size_t k = 0; k < 6; ++k) {
        cell->resources[k] = StoredAmount(tally.resources[k]);
    }
    cell->stones = StoredAmount(tally.stones);
    cell->trees = StoredCount(tally.trees);
    cell->stream_tiles = StoredCount(tally.stream_tiles);
    const std::uint32_t divisor = evaluation->tiles_per_cell;
    cell->grain_land_percent = static_cast<std::uint8_t>(tally.grain_land * 100 / divisor);
    cell->wine_land_percent = static_cast<std::uint8_t>(tally.wine_land * 100 / divisor);
    cell->rice_land_percent = static_cast<std::uint8_t>(tally.rice_land * 100 / divisor);
    const std::uint32_t grassPercent = tally.grass * 100 / divisor;
    cell->grass_percent = static_cast<std::uint8_t>(grassPercent);
    return grassPercent;
}

// Purpose: Evaluates every cell of the land evaluation grid (when the grid
// is made at the start of a game).
std::uint32_t __fastcall AI_CEvaLand_EvaluateAllCells(void* self, void* /*edx*/) {
    const auto* evaluation = static_cast<const AiLandEvaluation*>(self);
    for (std::uint32_t x = 0; x < evaluation->width; ++x) {
        for (std::uint32_t y = 0; y < evaluation->height; ++y) {
            reinterpret_cast<CellCallFn>(kGridCell)(self, nullptr, x, y);  // result unused
            reinterpret_cast<CellCallFn>(kEvaluateCell)(self, nullptr, x, y);
        }
    }
    return evaluation->width;
}

// Purpose: Keeps the land evaluation up to date a little at a time: each
// computer player step re-evaluates the next few owned cells of the grid.
std::uint32_t __fastcall AI_CEvaLand_UpdateNextCells(void* self, void* /*edx*/) {
    auto* evaluation = static_cast<AiLandEvaluation*>(self);
    std::uint32_t visited = 0, updated = 0;
    do {
        if (visited >= kMaxCellsVisited) {
            break;
        }
        ++visited;
        if (++evaluation->cursor_x == evaluation->width) {
            evaluation->cursor_x = 0;
            if (++evaluation->cursor_y == evaluation->height) {
                evaluation->cursor_y = 0;
            }
        }
        const auto owner = static_cast<std::uint16_t>(
            reinterpret_cast<CellCallFn>(kCellCentreOwner)(self, nullptr, evaluation->cursor_x, evaluation->cursor_y));
        if (owner == kNoOwner16 || owner == kNoOwner8) {
            continue;
        }
        ++updated;
        reinterpret_cast<CellCallFn>(kEvaluateCell)(self, nullptr, evaluation->cursor_x, evaluation->cursor_y);
        // GridSumAround gets the cursor's low words (the original pushes
        // them with leftover upper halves; the callee masks them off)
        reinterpret_cast<CellCallFn>(kGridSumAround)(self, nullptr, evaluation->cursor_x & 0xffff,
                                                     evaluation->cursor_y & 0xffff);
    } while (updated < kMaxCellsUpdated);
    return 0;  // EAX is left over in the original; the caller doesn't use it
}

namespace {

// The 3 x 3 cells around a tile's cell, on the grid (0x5548d7..0x554921 and
// 0x554929..0x554973 are the same double loop).
template <typename Visit>
void ForCellsAroundTile(const AiLandEvaluation* evaluation, std::uint32_t x, std::uint32_t y, Visit visit) {
    const auto firstX = static_cast<std::uint16_t>((static_cast<std::uint16_t>(x) >> kCellShift) - 1);
    const auto firstY = static_cast<std::uint16_t>((static_cast<std::uint16_t>(y) >> kCellShift) - 1);
    for (std::uint16_t i = 0; i < 3; ++i) {
        const auto cellX = static_cast<std::int16_t>(firstX + i);
        for (std::uint16_t j = 0; j < 3; ++j) {
            const auto cellY = static_cast<std::int16_t>(firstY + j);
            if (cellX < 0 || static_cast<std::uint32_t>(cellX) >= evaluation->width || cellY < 0 ||
                static_cast<std::uint32_t>(cellY) >= evaluation->height) {
                continue;
            }
            visit(static_cast<std::uint32_t>(cellX), static_cast<std::uint32_t>(cellY));
        }
    }
}

}  // namespace

// Purpose: Re-evaluates the land around a tile (after a building there was
// finished or removed, or its work area changed), so the computer players
// see the changed resources.
std::uint32_t __fastcall AI_CEvaLand_ReevaluateAroundTile(void* self, void* /*edx*/, std::uint32_t x,
                                                          std::uint32_t y) {
    const auto* evaluation = static_cast<const AiLandEvaluation*>(self);
    ForCellsAroundTile(evaluation, x, y, [self](std::uint32_t cellX, std::uint32_t cellY) {
        reinterpret_cast<CellCallFn>(kEvaluateCell)(self, nullptr, cellX, cellY);
    });
    ForCellsAroundTile(evaluation, x, y, [self](std::uint32_t cellX, std::uint32_t cellY) {
        reinterpret_cast<CellCallFn>(kGridSumAround)(self, nullptr, cellX, cellY);
    });
    return 0;  // EAX is left over in the original; no caller uses it
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// An 80 x 80 tile test map (the AI map size 40..80, so some cells run off
// it) with a dominant terrain class per 8 x 8 block (so that sums pass the
// cap of 250), random objects (trees, stones), resources, tile flags and
// heights, and a 10 x 10 grid. Each side gets its own CEvaLand object and
// grid copy; compared: the whole grid, the cursor and EAX where the original
// sets it on purpose.

namespace {

constexpr std::uint32_t kTestMap = 80;
constexpr std::uint32_t kTestGrid = 10;

struct CellFunctions {
    EvaluateCellFn evaluateCell;
    EvaluationFn evaluateAll;
    EvaluationFn updateNext;
    AroundTileFn aroundTile;
};

CellFunctions OriginalCellFunctions() {
    return {Original_AI_CEvaLand_EvaluateCell, Original_AI_CEvaLand_EvaluateAllCells,
            Original_AI_CEvaLand_UpdateNextCells, Original_AI_CEvaLand_ReevaluateAroundTile};
}

CellFunctions OurCellFunctions() {
    return {&AI_CEvaLand_EvaluateCell, &AI_CEvaLand_EvaluateAllCells, &AI_CEvaLand_UpdateNextCells,
            &AI_CEvaLand_ReevaluateAroundTile};
}

struct SavedGlobals {
    CGameData* world = g_aiWorld;
    std::uint16_t sizeX = g_aiMapSizeX, sizeY = g_aiMapSizeY;
    ~SavedGlobals() {
        g_aiWorld = world;
        g_aiMapSizeX = sizeX;
        g_aiMapSizeY = sizeY;
    }
};

void SetUpTestMap(test::Rng& rng, CGameData* world) {
    world->map_size = kTestMap;
    constexpr std::uint8_t kClasses[] = {0x00, 0x10, 0x10, 0x20, 0x20, 0x30, 0x60};
    for (std::uint32_t bx = 0; bx < kTestMap; bx += kCellTiles) {
        for (std::uint32_t by = 0; by < kTestMap; by += kCellTiles) {
            const std::uint8_t dominant = kClasses[rng.Below(sizeof(kClasses))];
            const std::uint32_t mixed = rng.Below(60);  // percent of tiles of another class
            const std::uint8_t resourceKind = static_cast<std::uint8_t>(rng.Below(6) << 4);
            const std::uint32_t stoneRolls = rng.Chance(1, 4) ? 7 : 1;  // of 8: a rocky block or not
            for (std::uint32_t x = bx; x < bx + kCellTiles; ++x) {
                for (std::uint32_t y = by; y < by + kCellTiles; ++y) {
                    layout::Tile& tile = world->tiles[x][y];
                    const std::uint8_t terrainClass =
                        rng.Below(100) < mixed ? static_cast<std::uint8_t>(rng.Below(10) << 4) : dominant;
                    tile.terrain = static_cast<std::uint8_t>(
                        terrainClass | (rng.Chance(1, 4) ? kTerrainSwampyGrass & 0x0f : rng.Below(16)));
                    const std::uint32_t roll = rng.Below(8);
                    tile.object = static_cast<std::uint8_t>(roll < stoneRolls ? 0x73 + rng.Below(12)       // stones
                                                            : roll == stoneRolls ? 0x44 + rng.Below(16)    // tree
                                                                                 : rng.Below(256));
                    tile.flags = static_cast<std::uint8_t>(rng.Chance(1, 3) ? rng.Below(256) : 0);
                    tile.resources = static_cast<std::uint8_t>(
                        (rng.Chance(3, 4) ? resourceKind : rng.Below(16) << 4) | rng.Below(16));
                    tile.height = static_cast<std::uint8_t>(rng.Below(rng.Chance(1, 2) ? 6 : 40));
                    tile.owner = static_cast<std::uint8_t>(rng.Chance(1, 3) ? 0xff : rng.Below(4));
                }
            }
        }
    }
}

// One CEvaLand object with its own grid per side.
struct TestEvaluation {
    AiLandEvaluation object{};
    std::vector<AiGridCell> cells = std::vector<AiGridCell>(kTestGrid * kTestGrid);
};

bool SameEvaluation(test::Context& ctx, const TestEvaluation& a, const TestEvaluation& b) {
    bool same = S3RE_EXPECT_MEM_EQ(&a.object, &b.object, offsetof(AiLandEvaluation, cells));
    same &= S3RE_EXPECT_MEM_EQ(a.cells.data(), b.cells.data(), a.cells.size() * sizeof(AiGridCell));
    return same;
}

void CompareCellFunctions(test::Context& ctx, const CellFunctions& candidate, int cases) {
    const CellFunctions original = OriginalCellFunctions();
    SavedGlobals saved;
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    g_aiWorld = world;
    TestEvaluation a, b;
    int capped = 0;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        if (i % 50 == 0) {
            SetUpTestMap(rng, world);
        }
        g_aiMapSizeX = static_cast<std::uint16_t>(40 + rng.Below(41));
        g_aiMapSizeY = static_cast<std::uint16_t>(40 + rng.Below(41));
        rng.Fill(&a.object, sizeof(a.object));
        rng.Fill(a.cells.data(), a.cells.size() * sizeof(AiGridCell));
        a.object.width = 1 + rng.Below(kTestGrid);
        a.object.height = 1 + rng.Below(kTestGrid);
        a.object.tiles_per_cell = rng.Chance(3, 4) ? 64 : 1 + rng.Below(200);
        a.object.cursor_x = rng.Below(a.object.width);
        a.object.cursor_y = rng.Below(a.object.height);
        // EvaluateCell / GridCell index with width, the cells array is 10 wide
        a.object.width = rng.Chance(1, 2) ? kTestGrid : a.object.width;
        b.object = a.object;
        b.cells = a.cells;
        a.object.cells = a.cells.data();
        b.object.cells = b.cells.data();
        const int op = static_cast<int>(rng.Below(4));
        std::uint32_t ra = 0, rb = 0;
        bool compareEax = false;
        switch (op) {
        case 0: {
            const std::uint32_t cellX = rng.Below(a.object.width), cellY = rng.Below(a.object.height);
            ra = original.evaluateCell(&a.object, nullptr, cellX, cellY);
            rb = candidate.evaluateCell(&b.object, nullptr, cellX, cellY);
            const AiGridCell& cell = a.cells[cellY * a.object.width + cellX];
            for (const std::uint8_t amount : {cell.stones, cell.resources[0], cell.resources[1], cell.resources[2],
                                              cell.resources[3], cell.resources[4], cell.resources[5]}) {
                capped += amount == kStoredMaximum ? 1 : 0;
            }
            compareEax = true;
            break;
        }
        case 1:
            ra = original.evaluateAll(&a.object, nullptr);
            rb = candidate.evaluateAll(&b.object, nullptr);
            compareEax = true;
            break;
        case 2:
            original.updateNext(&a.object, nullptr);
            candidate.updateNext(&b.object, nullptr);
            break;
        default: {
            const std::uint32_t upper = rng.Chance(1, 4) ? rng.Next32() & 0xffff0000u : 0;
            const std::uint32_t x = (rng.Chance(1, 20) ? rng.Next32() & 0xffff : rng.Below(kTestMap + 8)) | upper;
            const std::uint32_t y = (rng.Chance(1, 20) ? rng.Next32() & 0xffff : rng.Below(kTestMap + 8)) | upper;
            original.aroundTile(&a.object, nullptr, x, y);
            candidate.aroundTile(&b.object, nullptr, x, y);
            break;
        }
        }
        bool same = SameEvaluation(ctx, a, b);
        if (compareEax) {
            same &= S3RE_EXPECT_EQ(ra, rb);
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
        }
    }
    if (cases >= 2000 && !S3RE_EXPECT(capped >= 5)) {
        ctx.Fail(__FILE__, __LINE__, "  only %d evaluated cells reached the cap", capped);
    }
}

// Wrong: amounts above 250 halved like the others.
std::uint32_t __fastcall WrongEvaluateCell(void* self, void* edx, std::uint32_t cellX, std::uint32_t cellY) {
    const std::uint32_t result = AI_CEvaLand_EvaluateCell(self, edx, cellX, cellY);
    auto* cell = reinterpret_cast<AiGridCell*>(reinterpret_cast<CellCallFn>(kGridCell)(self, nullptr, cellX, cellY));
    if (cell->stones == kStoredMaximum) {
        cell->stones = 125;
    }
    return result;
}

// Wrong: the cursor moves down a column instead of along a row.
std::uint32_t __fastcall WrongUpdateNextCells(void* self, void* edx) {
    auto* evaluation = static_cast<AiLandEvaluation*>(self);
    std::swap(evaluation->width, evaluation->height);
    std::swap(evaluation->cursor_x, evaluation->cursor_y);
    AI_CEvaLand_UpdateNextCells(self, edx);
    std::swap(evaluation->width, evaluation->height);
    std::swap(evaluation->cursor_x, evaluation->cursor_y);
    return 0;
}

// Wrong: the cells around without the bottom-right one.
std::uint32_t __fastcall WrongAroundTile(void* self, void* edx, std::uint32_t x, std::uint32_t y) {
    return AI_CEvaLand_ReevaluateAroundTile(self, edx, x - 1, y - 1);
}

}  // namespace

S3RE_TEST(AiEvaLandCells_MatchOriginal) {
    CompareCellFunctions(ctx, OurCellFunctions(), 3000);
}

S3RE_TEST(AiEvaLandCells_ComparisonDetectsWrongImplementation) {
    const CellFunctions ours = OurCellFunctions();
    const CellFunctions wrongs[] = {
        {&WrongEvaluateCell, ours.evaluateAll, ours.updateNext, ours.aroundTile},
        {ours.evaluateCell, ours.evaluateAll, &WrongUpdateNextCells, ours.aroundTile},
        {ours.evaluateCell, ours.evaluateAll, ours.updateNext, &WrongAroundTile},
    };
    for (const CellFunctions& wrong : wrongs) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareCellFunctions(quiet, wrong, 3000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

}  // namespace s3re::game
