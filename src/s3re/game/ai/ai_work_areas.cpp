// Where a computer player's new building should work (computer players,
// session 55; Rainer\CompMapWorkerAgent.cpp, re/computer_players.md "Work
// areas"). The work-area finders are dispatched by 0x573890 after a
// building is finished (farm, fishery, vineyard, rice farm, waterworks) and
// by CCentrumCheckAgent's run 0x560e00; the result goes out as SetWorkArea.
//
//   0x0058e390 AI::HasSameTypeWorkAreaNear(skip, const u8* type; stack
//              const u8* player, const u16* x, const u16* y) fastcall, ret
//              0xc: the area of (x, y) (AI::AreaAlongRow; 0xffff: AL = 0);
//              then over the buildings (AI_GetBuilding until its error
//              flag) other than `skip` (low word): a finished building of
//              the player and type in that area (AI_BuildingArea, words)
//              whose work point is closer to (x, y) (TileDistance) than
//              twice its type's work radius: AL = 1. The radius (jump
//              tables 0x58e4e8 / 0x58e4d4): woodcutter and stonecutter 6,
//              vineyard MapOffset[150].distance + 2, farm MapOffset[300]
//              .distance + 2, rice farm MapOffset[400].distance / 4 (the
//              map offsets of g_aiWorld). Any other type keeps the radius
//              register's previous value: the x pointer's address before
//              the first such building (an original quirk, reproduced; the
//              callers only ask for the five types). AL = 0 at the end; the
//              callers test AL.
//   0x0059ba90 AI::FindWaterworksWorkArea(const Building*, u16* x; stack u16*
//              y) fastcall, ret 4: rings 1..7 around the building's cell
//              (position / 8): each starts one cell further by (+1, -1)
//              and walks four sides of 2 * ring steps (-x, +y, +x, -y),
//              checking each cell before the step. The first cell
//              whose centre belongs to the building's owner
//              (AI::CellCentreOwner 0x554850), with stream tiles
//              (AiGridCell::stream_tiles) and owned water
//              (AI::OwnersLandHasResource, goods 26): its centre
//              (TileCentre 0x554880) goes to *x, *y, EAX = 1. Else 0.

#include "game/ai/ai_layout.h"
#include "game/enums/buildings.h"
#include "game/enums/goods.h"
#include "game/globals.h"
#include "game/layout.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using namespace ai;
using B = BuildingType;

namespace {

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kAreaAlongRow = 0x005996c0;           // AI::AreaAlongRow (fastcall)
constexpr std::uintptr_t kGetBuilding = 0x0058e170;            // AI_GetBuilding (fastcall)
constexpr std::uintptr_t kBuildingArea = 0x00599610;           // AI_BuildingArea (fastcall)
constexpr std::uintptr_t kTileDistance = 0x004ff320;           // TileDistance (stdcall)
constexpr std::uintptr_t kCellCentreOwner = 0x00554850;        // AI::CellCentreOwner (stdcall)
constexpr std::uintptr_t kGridCell = 0x00554700;               // AI::CEvaLand::GridCell (thiscall)
constexpr std::uintptr_t kOwnersLandHasResource = 0x00599df0;  // AI::OwnersLandHasResource (fastcall)
constexpr std::uintptr_t kTileCentre = 0x00554880;             // AI::TileCentre_554880 (thiscall)

constexpr std::uint16_t kNoArea = 0xffff;
constexpr std::uint32_t kCutterRadius = 6;
constexpr std::size_t kVineyardOffset = 150;  // the map offsets whose distance gives the work radius
constexpr std::size_t kFarmOffset = 300;
constexpr std::size_t kRiceFarmOffset = 400;
constexpr std::int32_t kRadiusMargin = 2;
constexpr std::int32_t kRiceRadiusDivisor = 4;

constexpr int kWaterworksRings = 7;
constexpr int kWaterworksSides = 4;
constexpr unsigned kCellShift = 3;

}  // namespace

using SameTypeFn = std::uint32_t(__fastcall*)(std::uint32_t skip, const std::uint8_t* type, const std::uint8_t* player,
                                              const std::uint16_t* x, const std::uint16_t* y);
using WaterworksFn = std::uint32_t(__fastcall*)(const layout::Building* building, std::uint16_t* x,
                                                std::uint16_t* y);

std::uint32_t __fastcall AI_HasSameTypeWorkAreaNear(std::uint32_t skip, const std::uint8_t* type,
                                                    const std::uint8_t* player, const std::uint16_t* x,
                                                    const std::uint16_t* y);
std::uint32_t __fastcall AI_FindWaterworksWorkArea(const layout::Building* building, std::uint16_t* x,
                                                   std::uint16_t* y);

S3RE_HOOK(0x0058e390, AI_HasSameTypeWorkAreaNear, SameTypeFn, Original_AI_HasSameTypeWorkAreaNear,
          AI_HasSameTypeWorkAreaNear);
S3RE_HOOK(0x0059ba90, AI_FindWaterworksWorkArea, WaterworksFn, Original_AI_FindWaterworksWorkArea,
          AI_FindWaterworksWorkArea);

// Purpose: Whether another finished building of the same type and player
// already works near a place (its work area within twice the type's work
// radius), so a new work area there would overlap.
std::uint32_t __fastcall AI_HasSameTypeWorkAreaNear(std::uint32_t skip, const std::uint8_t* type,
                                                    const std::uint8_t* player, const std::uint16_t* x,
                                                    const std::uint16_t* y) {
    using AreaFn = std::uint32_t(__fastcall*)(std::uint32_t x, std::uint32_t y);
    const auto area = static_cast<std::uint16_t>(reinterpret_cast<AreaFn>(kAreaAlongRow)(*x, *y));
    if (area == kNoArea) {
        return 0;
    }
    const CGameData* world = g_aiWorld;
    // The radius register starts as the x pointer (see the header).
    auto radius = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(x));
    std::uint32_t error = 0;
    std::uint32_t id = 0;
    do {
        ++id;
        using GetBuildingFn = const layout::Building*(__fastcall*)(std::uint32_t id, std::uint32_t* error);
        const layout::Building* building = reinterpret_cast<GetBuildingFn>(kGetBuilding)(id, &error);
        if (building == nullptr || (skip & 0xffff) == id || *player != building->owner || *type != building->art ||
            building->buildup != 0) {
            continue;
        }
        const std::uint32_t number = id;
        using BuildingAreaFn = std::uint32_t(__fastcall*)(const std::uint32_t* building);
        if (static_cast<std::uint16_t>(reinterpret_cast<BuildingAreaFn>(kBuildingArea)(&number)) != area) {
            continue;
        }
        switch (building->art) {  // jump tables 0x58e4e8 / 0x58e4d4
        case Value(B::Woodcutter):
        case Value(B::Stonecutter): radius = kCutterRadius; break;
        case Value(B::Vineyard): radius = world->offsets[kVineyardOffset].distance + kRadiusMargin; break;
        case Value(B::Farm): radius = world->offsets[kFarmOffset].distance + kRadiusMargin; break;
        case Value(B::RiceFarm): radius = world->offsets[kRiceFarmOffset].distance / kRiceRadiusDivisor; break;
        default: break;  // the radius stays
        }
        using DistanceFn = std::uint32_t(__stdcall*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
        const std::uint32_t distance =
            reinterpret_cast<DistanceFn>(kTileDistance)(building->work_x, building->work_y, *x, *y);
        if (distance < static_cast<std::uint32_t>(radius + radius)) {
            return 1;
        }
    } while (error == 0);
    return 0;
}

// Purpose: Looks for owned land with water in the grid cells near a
// computer player's waterworks, as its work area.
std::uint32_t __fastcall AI_FindWaterworksWorkArea(const layout::Building* building, std::uint16_t* x,
                                                   std::uint16_t* y) {
    const std::uint16_t owner = building->owner;
    auto cellX = static_cast<std::uint16_t>(building->x >> kCellShift);
    auto cellY = static_cast<std::uint16_t>(building->y >> kCellShift);
    const std::uint32_t land = g_aiLandEvaluation;
    for (int ring = 1; ring <= kWaterworksRings; ++ring) {
        ++cellX;
        --cellY;
        for (int side = 0; side < kWaterworksSides; ++side) {
            constexpr std::int32_t kDx[kWaterworksSides] = {-1, 0, 1, 0};  // jump table 0x59bc0c
            constexpr std::int32_t kDy[kWaterworksSides] = {0, 1, 0, -1};
            for (std::int32_t step = 0; step < 2 * ring; ++step) {
                const auto cx = static_cast<std::int32_t>(static_cast<std::int16_t>(cellX));
                const auto cy = static_cast<std::int32_t>(static_cast<std::int16_t>(cellY));
                using OwnerFn = std::uint32_t(__fastcall*)(std::uint32_t self, void* edx, std::int32_t x,
                                                           std::int32_t y);
                if (static_cast<std::uint16_t>(reinterpret_cast<OwnerFn>(kCellCentreOwner)(land, nullptr, cx, cy)) ==
                    owner) {
                    using CellFn = const AiGridCell*(__fastcall*)(std::uint32_t self, void* edx, std::int32_t x,
                                                                  std::int32_t y);
                    const AiGridCell* cell = reinterpret_cast<CellFn>(kGridCell)(g_aiLandEvaluation, nullptr, cx, cy);
                    using HasFn = std::uint32_t(__fastcall*)(std::uint32_t cellX, std::uint32_t cellY,
                                                             std::uint32_t player, std::uint32_t goods);
                    if (cell->stream_tiles != 0 &&
                        reinterpret_cast<HasFn>(kOwnersLandHasResource)(cellX, cellY, owner,
                                                                        Value(Good::Water)) != 0) {
                        std::uint16_t centreX = 0, centreY = 0;
                        using CentreFn = std::uint32_t(__fastcall*)(std::uint32_t self, void* edx, std::int32_t x,
                                                                    std::int32_t y, std::uint16_t* outX,
                                                                    std::uint16_t* outY);
                        reinterpret_cast<CentreFn>(kTileCentre)(g_aiLandEvaluation, nullptr, cx, cy, &centreX,
                                                                &centreY);
                        *x = centreX;
                        *y = centreY;
                        return 1;
                    }
                }
                cellX = static_cast<std::uint16_t>(cellX + kDx[side]);
                cellY = static_cast<std::uint16_t>(cellY + kDy[side]);
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// An 80 x 80 map: tile owners in patches, stream terrain, areas 1..3 (some
// 0); a 10 x 10 grid with random stream_tiles; map offsets with random
// distances at 150 / 300 / 400; up to 30 buildings of the work-area types
// and others. Compared: AL / EAX and the output words.

namespace {

constexpr std::uint32_t kTestMap = 80;
constexpr std::uint32_t kGridSide = 10;

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

constexpr std::uint8_t kTypes[] = {Value(B::Woodcutter), Value(B::Stonecutter), Value(B::Vineyard),
                                   Value(B::Farm),       Value(B::RiceFarm),    Value(B::Waterworks),
                                   Value(B::Fishery)};

void SetUpWorld(test::Rng& rng, CGameData* world, std::vector<AiGridCell>& cells) {
    world->map_size = kTestMap;
    const std::uint32_t patch = 8 + rng.Below(16);
    for (std::uint32_t x = 0; x < kTestMap; ++x) {
        for (std::uint32_t y = 0; y < kTestMap; ++y) {
            layout::Tile& tile = world->tiles[x][y];
            tile.owner = static_cast<std::uint8_t>(((x / patch) * 7 + (y / patch) * 3) % 4 == 3
                                                       ? 0xff
                                                       : ((x / patch) * 7 + (y / patch) * 3) % 4);
            tile.terrain = static_cast<std::uint8_t>(rng.Chance(1, 6) ? 0x60 + rng.Below(16) : rng.Below(0x60));
            tile.area = static_cast<std::uint16_t>(rng.Chance(1, 10) ? 0 : 1 + rng.Below(3));
        }
    }
    for (AiGridCell& cell : cells) {
        cell.stream_tiles = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : 1 + rng.Below(60));
    }
    for (std::size_t k : {kVineyardOffset, kFarmOffset, kRiceFarmOffset}) {
        world->offsets[k].distance = static_cast<std::int32_t>(rng.Chance(1, 8) ? rng.Next32() : rng.Below(40));
    }
    const std::uint32_t count = rng.Below(30);
    world->building_high_water = count;
    for (std::uint32_t id = 1; id <= count; ++id) {
        layout::Building& building = world->buildings[id];
        building.owner = static_cast<std::uint8_t>(rng.Chance(1, 8) ? 0xff : rng.Below(3));
        building.art = kTypes[rng.Below(sizeof(kTypes))];
        building.buildup = static_cast<std::uint8_t>(rng.Chance(1, 5) ? 1 : 0);
        building.x = building.door_x = static_cast<std::uint16_t>(rng.Below(kTestMap));
        building.y = building.door_y = static_cast<std::uint16_t>(rng.Below(kTestMap));
        building.work_x = static_cast<std::uint16_t>(rng.Below(kTestMap));
        building.work_y = static_cast<std::uint16_t>(rng.Below(kTestMap));
    }
}

void CompareWorkAreas(test::Context& ctx, SameTypeFn sameType, WaterworksFn waterworks, int cases) {
    SavedGlobals saved;
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    std::vector<AiGridCell> cells(kGridSide * kGridSide);
    AiLandEvaluation land{};
    land.cells = cells.data();
    land.width = kGridSide;
    land.height = kGridSide;
    g_aiWorld = world;
    g_aiLandEvaluation = reinterpret_cast<std::uint32_t>(&land);
    g_aiMapSizeX = static_cast<std::uint16_t>(kTestMap);
    g_aiMapSizeY = static_cast<std::uint16_t>(kTestMap);
    int hits = 0;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        if (i % 20 == 0) {
            SetUpWorld(rng, world, cells);
        }
        bool same = true;
        if (rng.Chance(1, 2)) {
            const std::uint32_t skip = rng.Below(30) | (rng.Chance(1, 4) ? rng.Next32() & 0xffff0000u : 0);
            const std::uint8_t type = kTypes[rng.Below(5)];
            const auto player = static_cast<std::uint8_t>(rng.Below(3));
            const auto x = static_cast<std::uint16_t>(rng.Chance(1, 20) ? rng.Next32() : rng.Below(kTestMap));
            const auto y = static_cast<std::uint16_t>(rng.Below(kTestMap));
            const std::uint32_t ra = Original_AI_HasSameTypeWorkAreaNear(skip, &type, &player, &x, &y) & 0xff;
            const std::uint32_t rb = sameType(skip, &type, &player, &x, &y) & 0xff;  // the callers test AL
            hits += ra != 0 ? 1 : 0;
            same &= S3RE_EXPECT_EQ(ra, rb);
        } else {
            const std::uint32_t id = 1 + rng.Below(30);
            layout::Building& building = world->buildings[id];
            const std::uint16_t startX = static_cast<std::uint16_t>(rng.Next32());
            std::uint16_t xa = startX, ya = static_cast<std::uint16_t>(rng.Next32());
            std::uint16_t xb = xa, yb = ya;
            const std::uint32_t ra = Original_AI_FindWaterworksWorkArea(&building, &xa, &ya);
            const std::uint32_t rb = waterworks(&building, &xb, &yb);
            hits += ra != 0 ? 1 : 0;
            same &= S3RE_EXPECT_EQ(ra, rb);
            same &= S3RE_EXPECT_EQ(xa, xb);
            same &= S3RE_EXPECT_EQ(ya, yb);
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
        }
    }
    if (cases >= 1000 && !S3RE_EXPECT(hits >= cases / 10)) {
        ctx.Fail(__FILE__, __LINE__, "  only %d hits", hits);
    }
}

std::uint32_t __fastcall WrongSameType(std::uint32_t skip, const std::uint8_t* type, const std::uint8_t* player,
                                       const std::uint16_t* x, const std::uint16_t* y) {
    // Wrong: the farm's margin is 8.
    CGameData* world = g_aiWorld;
    std::int32_t& distance = world->offsets[kFarmOffset].distance;
    distance += 6;
    const std::uint32_t result = AI_HasSameTypeWorkAreaNear(skip, type, player, x, y);
    distance -= 6;
    return result;
}

std::uint32_t __fastcall WrongWaterworks(const layout::Building* building, std::uint16_t* x, std::uint16_t* y) {
    // Wrong: starts one cell further (+2, -2).
    layout::Building moved = *building;
    moved.x = static_cast<std::uint16_t>(moved.x + 8);
    moved.y = static_cast<std::uint16_t>(moved.y - 8);
    return AI_FindWaterworksWorkArea(&moved, x, y);
}

}  // namespace

S3RE_TEST(AiWorkAreas_MatchOriginal) {
    CompareWorkAreas(ctx, &AI_HasSameTypeWorkAreaNear, &AI_FindWaterworksWorkArea, 6000);
}

S3RE_TEST(AiWorkAreas_ComparisonDetectsWrongImplementation) {
    for (int k = 0; k < 2; ++k) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareWorkAreas(quiet, k == 0 ? &WrongSameType : &AI_HasSameTypeWorkAreaNear,
                         k == 1 ? &WrongWaterworks : &AI_FindWaterworksWorkArea, 6000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

}  // namespace s3re::game
