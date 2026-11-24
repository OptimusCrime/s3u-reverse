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

#include "game/ai/ai_layout.h"
#include "game/globals.h"
#include "game/layout.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
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
constexpr std::uint8_t kTerrainClassMask = 0xf0;
constexpr std::uint8_t kTerrainClassWater = 0x60;
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
        return ((&world->tiles[0][0])[x * kMapRowTiles + y].terrain & kTerrainClassMask) == kTerrainClassWater;
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
            case kGoodsStone: present = cell->value03; break;
            case kGoodsLog: present = cell->value04; break;
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
        cell.value03 = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : rng.Next32());
        cell.value04 = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : rng.Next32());
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

}  // namespace s3re::game
