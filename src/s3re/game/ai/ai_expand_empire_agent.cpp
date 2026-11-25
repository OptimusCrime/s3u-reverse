// GLOBAL_EXPAND_EMPIRE_AGENT (kind 3; Rainer\CompGExpand.cpp): the agent
// that places small towers to expand the computer player's land
// (computer players A6, session 56; re/computer_players.md "The expand
// agent").
//
//   0x0056cd20 GLOBAL_EXPAND_EMPIRE_AGENT::Run(), slot 8: only when a small
//              tower was taken for building (g_aiPlayerData->best[57 + 15]
//              .score, the candidate StartBestBuilding 0x551a70 moved
//              there): clears that score, steps military_cursor (dword) to
//              the next garrisoned military building of the player
//              (AI::NextGarrisonedMilitaryBuilding 0x571ee0, & 0xffff;
//              0xffff = none: stop) and calls MakeBuildOrders(its x, its y,
//              AI::TerritoryRadius 0x58ed30 of its type). EAX is not used
//              (CComputerPlayer::RunAgents 0x551590 ignores slot 8's).
//   0x0056cd90 GLOBAL_EXPAND_EMPIRE_AGENT::MakeBuildOrders(x, y, radius),
//              ret 0xc: decides where a small tower goes, from the building
//              at (x, y) whose territory has `radius`:
//              1. area = AI::AreaAlongRow(x, y) (0xffff: AX = 0), region =
//                 the low byte of AI::WalkRegionAlongRow(x, y); a
//                 WrappedPos on the AI's map at (x, y).
//              2. If the area node's steps_since_expansion is above 4200:
//                 for each direction 0..5 the position radius + 5 steps
//                 away from (x, y) (WrappedPos::Advance; off the map:
//                 next direction) is taken if its area (AreaInDirection
//                 with direction 0xffff: no stepping) is another one than
//                 `area`, its walk region is `region`, the tile is not
//                 another player's (or AI::FewUnownedTilesNear says 0,
//                 which it never does) and AI::HasForeignCellAround is
//                 set. None: AX = 0.
//              3. Otherwise one random direction (CGameData::Random % 6),
//                 the same checks (AreaInDirection with that direction, no
//                 owner test); failing: AX = 0. Then a random choice
//                 between building the tower there at once and first
//                 looking for resources: every kind of site the area has
//                 none of (empty site trees: trees, stones, iron, coal,
//                 gold, fish, water, farmland; by race vineyards, gems,
//                 rice land and sulfur) is one more option (DebugLog per
//                 kind); Random % options == 0 builds at once, unless
//                 there are no tree or no stone sites. Looking for
//                 resources walks the grid cells cx0 + i, cy0 + j (i, j in
//                 -R..R, R = g_aiPlayerSetup +0xc2, cx0/cy0 the position's
//                 cell) on the grid, with another area and the same walk
//                 region at the cell centre, not another player's (see 2),
//                 for the first cell with the wanted resource (trees > 10
//                 when there are no tree sites, else stones > 10 when no
//                 stone sites, else water, fish, by race rice land /
//                 sulfur / gems / vineyard land, coal, iron, gold,
//                 farmland). Found: the tower is built; none: DebugLog
//                 with the area and AX = 0.
//              Building the tower: unless a small tower note is within 24
//              (AI::HasSiteNoteNear 0x571c40), a T_DO_BUILD_HOUSE_NOTE
//              (small tower, the position, value 32) is added as a root
//              note (CComputerPlayer::AddNote 0x551270, requester 0). AX =
//              100. Out of memory: Win32::ThrowOutOfMemory("notePtr",
//              CompGExpand.cpp, line 538).
//
// Quirks kept (re/computer_players.md): the resource search only decides
// whether to build; the tower goes to the advanced position, not to the
// cell found. The upper halves of the arguments the original pushes are
// register leftovers; every callee reads only the low words (checked), so
// ours pass the values.

#include "game/ai/ai_layout.h"
#include "game/debug_log.h"
#include "game/enums/buildings.h"
#include "game/enums/races.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "game/world.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using namespace ai;

namespace {

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kNextGarrisonedMilitaryBuilding = 0x00571ee0;  // fastcall (start, player)
constexpr std::uintptr_t kBuildingIfUsed = 0x0058e1c0;                  // fastcall (id)
constexpr std::uintptr_t kTerritoryRadius = 0x0058ed30;                 // fastcall (const u16* type)
constexpr std::uintptr_t kMakeBuildOrders = 0x0056cd90;
constexpr std::uintptr_t kAreaAlongRow = 0x005996c0;         // fastcall (x, y)
constexpr std::uintptr_t kWalkRegionAlongRow = 0x00599870;   // fastcall (x, y)
constexpr std::uintptr_t kWrappedPosConstruct = 0x00597090;  // thiscall (x, y, width, height)
constexpr std::uintptr_t kWrappedPosSetPosition = 0x005970c0;  // thiscall (x, y)
constexpr std::uintptr_t kWrappedPosAdvance = 0x00597300;    // thiscall (direction, count)
constexpr std::uintptr_t kAreaInDirection = 0x00599760;      // fastcall (const u16* x, const u16* y, const u16* direction)
constexpr std::uintptr_t kTileOwner = 0x00599570;            // fastcall (const u16* x, const u16* y)
constexpr std::uintptr_t kFewUnownedTilesNear = 0x0059c2c0;  // fastcall (const u16* x, const u16* y)
constexpr std::uintptr_t kHasForeignCellAround = 0x0059c200;  // fastcall (const u16* x, const u16* y)
constexpr std::uintptr_t kRandom = 0x004feb70;               // CGameData::Random (thiscall)
constexpr std::uintptr_t kCellCentreOwner = 0x00554850;      // stdcall (cell x, cell y)
constexpr std::uintptr_t kGridCell = 0x00554700;             // CEvaLand::GridCell (thiscall x, y)
constexpr std::uintptr_t kHasSiteNoteNear = 0x00571c40;      // fastcall (type, x; y, distance)
constexpr std::uintptr_t kDoBuildHouseNoteInit = 0x00565470;  // T_DO_BUILD_HOUSE_NOTE::Init (thiscall)
constexpr std::uintptr_t kThrowOutOfMemory = 0x00610960;     // Win32::ThrowOutOfMemory (never returns)
constexpr std::uintptr_t kAddNote = 0x00551270;              // CComputerPlayer::AddNote (thiscall)

constexpr std::uint16_t kNone = 0xffff;
constexpr std::uint8_t kNoOwner = 0xff;
constexpr std::uint16_t kBuildOrdered = 100;  // AX once the tower is ordered (or a note is near)
constexpr std::uint32_t kLongNotExpanded = 4200;  // steps_since_expansion above this: look in every direction
constexpr std::uint32_t kDirections = 6;
constexpr std::uint32_t kExtraSteps = 5;         // beyond the territory radius
constexpr std::uint16_t kNoStepping = 0xffff;   // AreaInDirection's direction: no step (above 5)
constexpr std::uint32_t kTowerNoteDistance = 24;
constexpr std::uint32_t kTowerNoteValue = 32;
constexpr std::uint32_t kNoteBytes = 0x28;
constexpr std::uint32_t kNoteLine = 538;  // CompGExpand.cpp
constexpr std::uint16_t kRootNote = kNoNote;
constexpr std::uint32_t kNoRequester = 0;
constexpr std::int32_t kTilesPerCellShift = 3;  // 8 x 8 tiles per grid cell
constexpr std::int32_t kCellCentre = 4;
// Resource amounts a cell needs (GridSumAround's sums, AiGridCell).
constexpr std::uint8_t kManyTrees = 10;
constexpr std::uint16_t kManyStones = 10;
constexpr std::uint16_t kSomeResource = 3;
constexpr std::uint8_t kSomeLand = 3;
constexpr std::uint8_t kMuchFarmland = 20;

// AiArea::site_trees by the kind of site they hold (the map worker's
// T_MAP_WORKER_AGENT::MakeBuildOrders 0x57ce90 takes the sites of these
// building types from them).
enum SiteKind : std::size_t {
    kTreeSites = 0,       // woodcutter
    kStoneSites = 1,      // stonecutter
    kIronSites = 2,       // iron mine
    kCoalSites = 3,       // coal mine
    kGoldSites = 4,       // gold mine
    kSulfurSites = 5,     // sulfur mine
    kGemSites = 6,        // gem mine
    kFishSites = 7,       // fishery
    kWaterSites = 8,      // waterworks
    kFarmlandSites = 15,  // farm
    kVineyardSites = 24,  // vineyard
    kRiceSites = 29,      // rice farm
};

std::uint32_t SiteCount(const AiPlayerData* data, SiteKind kind) {
    return reinterpret_cast<const AiAreaNode*>(data)->value.site_trees[kind].size;
}

std::uint32_t AreaAlongRow(std::uint32_t x, std::uint32_t y) {
    return reinterpret_cast<std::uint32_t(__fastcall*)(std::uint32_t, std::uint32_t)>(kAreaAlongRow)(x, y);
}

std::uint32_t WalkRegionAlongRow(std::uint32_t x, std::uint32_t y) {
    return reinterpret_cast<std::uint32_t(__fastcall*)(std::uint32_t, std::uint32_t)>(kWalkRegionAlongRow)(x, y);
}

using PointerQueryFn = std::uint32_t(__fastcall*)(const std::uint16_t* x, const std::uint16_t* y);

std::uint32_t PointerQuery(std::uintptr_t function, std::uint16_t x, std::uint16_t y) {
    return reinterpret_cast<PointerQueryFn>(function)(&x, &y);
}

std::uint32_t Advance(WrappedPos* pos, std::uint32_t direction, std::uint32_t count) {
    using Fn = std::uint32_t(__fastcall*)(WrappedPos*, void*, std::uint32_t, std::uint32_t);
    return reinterpret_cast<Fn>(kWrappedPosAdvance)(pos, nullptr, direction, count);
}

std::uint16_t AreaInDirection(const WrappedPos& pos, std::uint16_t direction) {
    const auto x = static_cast<std::uint16_t>(pos.x);
    const auto y = static_cast<std::uint16_t>(pos.y);
    using Fn = std::uint32_t(__fastcall*)(const std::uint16_t*, const std::uint16_t*, const std::uint16_t*);
    return static_cast<std::uint16_t>(reinterpret_cast<Fn>(kAreaInDirection)(&x, &y, &direction));
}

std::uint32_t Random() {
    using Fn = std::uint32_t(__fastcall*)(CGameData*, void*);
    return reinterpret_cast<Fn>(kRandom)(g_aiWorld, nullptr);
}

// The tile owner is another player's (not free, not ours) and
// FewUnownedTilesNear lets that pass (0x56ceab..0x56cee2, 0x56d2e6..0x56d315;
// FewUnownedTilesNear always says 1, a bug kept in its replacement).
bool BlockedByOtherOwner(std::uint16_t owner, std::uint16_t x, std::uint16_t y) {
    if (owner == kNoOwner || owner == g_aiPlayerNumber) {
        return false;
    }
    return PointerQuery(kFewUnownedTilesNear, x, y) != 0;
}

// The candidate position: another area, the same walk region, and
// (step 2 only) not another player's tile; then the foreign cell check.
// 0x56ce61..0x56cf03 (every direction) and 0x56cf6e..0x56cfc1 (random).
bool IsExpansionSite(const WrappedPos& pos, std::uint16_t direction, std::uint16_t area, std::uint8_t region,
                     bool checkOwner) {
    const std::uint16_t areaThere = AreaInDirection(pos, direction);
    if (areaThere == kNone || areaThere == area) {
        return false;
    }
    const auto x = static_cast<std::uint16_t>(pos.x);
    const auto y = static_cast<std::uint16_t>(pos.y);
    // The original passes (x | y << 16, y | width << 16); the callee reads words.
    if (static_cast<std::uint16_t>(WalkRegionAlongRow(x, y)) != region) {
        return false;
    }
    if (checkOwner) {
        const auto owner = static_cast<std::uint16_t>(PointerQuery(kTileOwner, x, y));
        if (BlockedByOtherOwner(owner, x, y)) {
            return false;
        }
    }
    return PointerQuery(kHasForeignCellAround, x, y) != 0;
}

// What the resource search looks for (0x56cfc7..0x56d19d): the kinds of
// sites the area has none of.
struct WantedResources {
    bool trees = false, stones = false, water = false, fish = false, farmland = false;
    bool rice = false, sulfur = false, gems = false, vineyard = false;
    std::uint32_t options = 1;  // building at once, plus one per missing kind
};

void Ask(bool missing, const GameText& question, WantedResources& wanted, bool* flag) {
    if (!missing) {
        return;
    }
    ++wanted.options;
    if (flag != nullptr) {
        *flag = true;
    }
    GameDebugLog(question);
}

WantedResources FindMissingSites(const AiPlayerData* data, std::uint16_t race) {
    WantedResources wanted;
    Ask(SiteCount(data, kTreeSites) == 0, texts::kExpandTreesQ, wanted, &wanted.trees);
    Ask(SiteCount(data, kStoneSites) == 0, texts::kExpandStonesQ, wanted, &wanted.stones);
    Ask(SiteCount(data, kIronSites) == 0, texts::kExpandIronQ, wanted, nullptr);
    Ask(SiteCount(data, kCoalSites) == 0, texts::kExpandCoalQ, wanted, nullptr);
    Ask(SiteCount(data, kGoldSites) == 0, texts::kExpandGoldQ, wanted, nullptr);
    Ask(SiteCount(data, kFishSites) == 0, texts::kExpandFishQ, wanted, &wanted.fish);
    Ask(SiteCount(data, kWaterSites) == 0, texts::kExpandWaterQ, wanted, &wanted.water);
    Ask(SiteCount(data, kFarmlandSites) == 0, texts::kExpandFarmlandQ, wanted, &wanted.farmland);
    switch (race) {
    case Value(Race::Roman): Ask(SiteCount(data, kVineyardSites) == 0, texts::kExpandVineyardQ, wanted, &wanted.vineyard); break;
    case Value(Race::Egyptian): Ask(SiteCount(data, kGemSites) == 0, texts::kExpandGemsQ, wanted, &wanted.gems); break;
    case Value(Race::Asian):
        Ask(SiteCount(data, kRiceSites) == 0, texts::kExpandSwampQ, wanted, &wanted.rice);
        Ask(SiteCount(data, kSulfurSites) == 0, texts::kExpandSulfurQ, wanted, &wanted.sulfur);
        break;
    default: break;
    }
    return wanted;
}

// The resource the cell offers, as the original tests them (0x56d328..0x56d40c);
// nullptr = none of the wanted ones.
const GameText* ResourceFound(const AiGridCell& cell, const WantedResources& wanted, std::uint16_t race) {
    if (wanted.trees) {
        return cell.trees_around > kManyTrees ? &texts::kExpandTreesFound : nullptr;
    }
    if (wanted.stones) {
        return cell.stones_around > kManyStones ? &texts::kExpandStonesFound : nullptr;
    }
    if (wanted.water && cell.stream_tiles != 0) {
        return &texts::kExpandWaterFound;
    }
    if (wanted.fish && cell.resources_around[kGridFish] > kSomeResource) {
        return &texts::kExpandFishFound;
    }
    if (race == Value(Race::Asian)) {
        if (wanted.rice && cell.rice_land_around > kSomeLand) {
            return &texts::kExpandSwampFound;
        }
        if (wanted.sulfur && cell.resources_around[kGridSulfur] > kSomeResource) {
            return &texts::kExpandSulfurFound;
        }
    } else if (race == Value(Race::Egyptian)) {
        if (wanted.gems && cell.resources_around[kGridGems] > kSomeResource) {
            return &texts::kExpandGemsFound;
        }
    } else if (race == Value(Race::Roman)) {
        if (wanted.vineyard && cell.wine_land_around > kSomeLand) {
            return &texts::kExpandVineyardFound;
        }
    }
    if (cell.resources_around[kGridCoal] > kSomeResource) {
        return &texts::kExpandCoalFound;
    }
    if (cell.resources_around[kGridIron] > kSomeResource) {
        return &texts::kExpandIronFound;
    }
    if (cell.resources_around[kGridGold] > kSomeResource) {
        return &texts::kExpandGoldFound;
    }
    if (wanted.farmland && cell.grain_land_around > kMuchFarmland) {
        return &texts::kExpandFarmlandFound;
    }
    return nullptr;
}

// Signed division by 8 rounding toward zero (cdq / and 7 / add / sar 3).
std::int32_t CellOf(std::int16_t tile) { return tile / (1 << kTilesPerCellShift); }

// 0x56d1cc..0x56d466: looks for a grid cell around the position with a
// wanted resource. Returns true when one is found (logged).
bool FindResourceCell(const WrappedPos& pos, const WantedResources& wanted, std::uint16_t race, std::uint16_t area,
                      std::uint8_t region) {
    GameDebugLog(texts::kExpandResources);
    const std::int32_t cellX0 = CellOf(pos.x);
    const std::int32_t cellY0 = CellOf(pos.y);
    // The radius word; the loops count in dwords and compare their low
    // words, signed, with it (0x56d1f1..0x56d44f).
    const auto radius = static_cast<std::int16_t>(g_aiPlayerSetup.get()->expand_search_radius);
    const std::int32_t start = -static_cast<std::int32_t>(static_cast<std::uint16_t>(radius));
    auto* land = reinterpret_cast<AiLandEvaluation*>(g_aiLandEvaluation.get());
    for (std::int32_t j = start; static_cast<std::int16_t>(j) <= radius; ++j) {
        for (std::int32_t i = start; static_cast<std::int16_t>(i) <= radius; ++i) {
            const std::int32_t cellX = cellX0 + i;
            const std::int32_t cellY = cellY0 + j;
            const auto cx = static_cast<std::int16_t>(cellX);
            const auto cy = static_cast<std::int16_t>(cellY);
            if (cx < 0 || static_cast<std::uint32_t>(cx) >= land->width || cy < 0 ||
                static_cast<std::uint32_t>(cy) >= land->height) {
                continue;
            }
            const std::uint32_t tileX = static_cast<std::uint32_t>(cellX) * 8 + kCellCentre;
            const std::uint32_t tileY = static_cast<std::uint32_t>(cellY) * 8 + kCellCentre;
            if (static_cast<std::uint16_t>(AreaAlongRow(tileX, tileY)) == area) {
                continue;
            }
            if (static_cast<std::uint16_t>(WalkRegionAlongRow(tileX, tileY)) != region) {
                continue;
            }
            using OwnerFn = std::uint32_t(__stdcall*)(std::uint32_t, std::uint32_t);
            const auto owner = static_cast<std::uint16_t>(
                reinterpret_cast<OwnerFn>(kCellCentreOwner)(static_cast<std::uint32_t>(cx), static_cast<std::uint32_t>(cy)));
            if (BlockedByOtherOwner(owner, static_cast<std::uint16_t>(tileX), static_cast<std::uint16_t>(tileY))) {
                continue;
            }
            using CellFn = const AiGridCell*(__fastcall*)(AiLandEvaluation*, void*, std::uint32_t, std::uint32_t);
            const AiGridCell* cell = reinterpret_cast<CellFn>(kGridCell)(land, nullptr, static_cast<std::uint32_t>(cx),
                                                                         static_cast<std::uint32_t>(cy));
            if (const GameText* found = ResourceFound(*cell, wanted, race)) {
                GameDebugLog(*found);
                return true;
            }
        }
    }
    GameDebugLog(texts::kExpandNothingFound, static_cast<std::uint32_t>(area));
    return false;
}

// 0x56d4db..0x56d567: orders a small tower at the position unless one is
// already ordered near it.
std::uint16_t OrderSmallTower(const WrappedPos& pos) {
    const auto x = static_cast<std::uint16_t>(pos.x);
    const auto y = static_cast<std::uint16_t>(pos.y);
    using NearFn = std::uint32_t(__fastcall*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
    if (reinterpret_cast<NearFn>(kHasSiteNoteNear)(Value(BuildingType::SmallTower), x, y, kTowerNoteDistance) != 0) {
        return kBuildOrdered;
    }
    void* note = msvc6::GameOperatorNew(kNoteBytes);
    if (note != nullptr) {
        *static_cast<std::uint32_t*>(note) = vtables::kDoBuildHouseNote;
        using InitFn = std::uint32_t(__fastcall*)(void*, void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
        reinterpret_cast<InitFn>(kDoBuildHouseNoteInit)(note, nullptr, Value(BuildingType::SmallTower), x, y,
                                                        kTowerNoteValue);
    } else {
        using ThrowFn = void(__fastcall*)(const char*, const char*, std::uint32_t);
        reinterpret_cast<ThrowFn>(kThrowOutOfMemory)(texts::kNotePtr.c_str(), texts::kCompGExpandSourceFile.c_str(),
                                                     kNoteLine);
    }
    using AddFn = std::uint32_t(__fastcall*)(ComputerPlayer*, void*, std::uint32_t, std::uint32_t, std::uint32_t);
    reinterpret_cast<AddFn>(kAddNote)(g_aiPlayer, nullptr, reinterpret_cast<std::uint32_t>(note), kRootNote,
                                      kNoRequester);
    return kBuildOrdered;
}

}  // namespace

using ExpandRunFn = std::uint32_t(__fastcall*)(ExpandEmpireAgent* self, void* edx);
using ExpandOrdersFn = std::uint32_t(__fastcall*)(ExpandEmpireAgent* self, void* edx, std::uint32_t x, std::uint32_t y,
                                                  std::uint32_t radius);

std::uint32_t __fastcall GLOBAL_EXPAND_EMPIRE_AGENT_Run(ExpandEmpireAgent* self, void* edx);
std::uint32_t __fastcall GLOBAL_EXPAND_EMPIRE_AGENT_MakeBuildOrders(ExpandEmpireAgent* self, void* edx, std::uint32_t x,
                                                                    std::uint32_t y, std::uint32_t radius);

S3RE_HOOK(0x0056cd20, GLOBAL_EXPAND_EMPIRE_AGENT_Run, ExpandRunFn, Original_GLOBAL_EXPAND_EMPIRE_AGENT_Run,
          GLOBAL_EXPAND_EMPIRE_AGENT_Run);
S3RE_HOOK(0x0056cd90, GLOBAL_EXPAND_EMPIRE_AGENT_MakeBuildOrders, ExpandOrdersFn,
          Original_GLOBAL_EXPAND_EMPIRE_AGENT_MakeBuildOrders, GLOBAL_EXPAND_EMPIRE_AGENT_MakeBuildOrders);

// Purpose: When the computer player has taken a small tower for building,
// the expand agent picks its next garrisoned military building and looks
// for a place to expand beyond that building's territory.
std::uint32_t __fastcall GLOBAL_EXPAND_EMPIRE_AGENT_Run(ExpandEmpireAgent* self, void* /*edx*/) {
    AiPlayerData* data = g_aiPlayerData;
    AiCandidate& tower = data->best[kOrderedCandidates + Value(BuildingType::SmallTower)];
    if (tower.score == 0) {
        return reinterpret_cast<std::uint32_t>(data);  // (EAX is not used)
    }
    tower.score = 0;
    using NextFn = std::uint32_t(__fastcall*)(std::uint32_t start, std::uint32_t player);
    const std::uint32_t id =
        reinterpret_cast<NextFn>(kNextGarrisonedMilitaryBuilding)(self->military_cursor, g_aiPlayerNumber) & 0xffff;
    self->military_cursor = id;
    if (id == kNone) {
        return id;
    }
    using BuildingFn = const layout::Building*(__fastcall*)(std::uint32_t id);
    const layout::Building* building = reinterpret_cast<BuildingFn>(kBuildingIfUsed)(id);
    const std::uint16_t type = building->art;
    const std::uint32_t radius = reinterpret_cast<std::uint32_t(__fastcall*)(const std::uint16_t*)>(kTerritoryRadius)(&type);
    return reinterpret_cast<ExpandOrdersFn>(kMakeBuildOrders)(self, nullptr, building->x, building->y, radius);
}

// Purpose: The expand agent's decision where to put a small tower beyond a
// military building's territory: in a direction where the land is another
// area of the same walkable region next to foreign land, after a long time
// without expanding in any such direction, otherwise in a random one, and
// then (randomly) only when a resource the player lacks sites for is near.
std::uint32_t __fastcall GLOBAL_EXPAND_EMPIRE_AGENT_MakeBuildOrders(ExpandEmpireAgent* /*self*/, void* /*edx*/,
                                                                    std::uint32_t x, std::uint32_t y,
                                                                    std::uint32_t radius) {
    const auto area = static_cast<std::uint16_t>(AreaAlongRow(x, y));
    if (area == kNone) {
        return 0;
    }
    const auto region = static_cast<std::uint8_t>(WalkRegionAlongRow(x, y));
    WrappedPos pos;  // (the constructor sets every field)
    using ConstructFn = WrappedPos*(__fastcall*)(WrappedPos*, void*, std::uint32_t, std::uint32_t, std::uint32_t,
                                                  std::uint32_t);
    reinterpret_cast<ConstructFn>(kWrappedPosConstruct)(&pos, nullptr, x, y, g_aiMapSizeX, g_aiMapSizeY);
    const std::uint32_t steps = radius + kExtraSteps;
    GameDebugLog(texts::kExpandStart);
    AiPlayerData* data = g_aiPlayerData;
    if (data->steps_since_expansion > kLongNotExpanded) {
        // 0x56ce1c..0x56cf19: every direction from the building.
        for (std::uint32_t direction = 0; direction < kDirections; ++direction) {
            using SetFn = std::uint32_t(__fastcall*)(WrappedPos*, void*, std::uint32_t, std::uint32_t);
            reinterpret_cast<SetFn>(kWrappedPosSetPosition)(&pos, nullptr, x, y);
            if (Advance(&pos, direction, steps) != 0 && IsExpansionSite(pos, kNoStepping, area, region, true)) {
                GameDebugLog(texts::kExpandLongNotExpanded);
                return OrderSmallTower(pos);
            }
        }
        return 0;
    }
    // 0x56cf1e..0x56d1c8: one random direction.
    const auto direction = static_cast<std::uint16_t>((Random() & 0xffff) % kDirections);
    if (Advance(&pos, direction, steps) == 0 || !IsExpansionSite(pos, direction, area, region, false)) {
        return 0;
    }
    const auto race = static_cast<std::uint16_t>(g_aiWorld.get()->players[g_aiPlayerNumber].race);
    const WantedResources wanted = FindMissingSites(data, race);
    const auto choice = static_cast<std::uint16_t>((Random() & 0xffff) % wanted.options);
    if (choice == 0 && !wanted.trees && !wanted.stones) {
        return OrderSmallTower(pos);
    }
    if (!FindResourceCell(pos, wanted, race, area, region)) {
        return 0;
    }
    return OrderSmallTower(pos);
}

}  // namespace s3re::game

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// A 64 x 64 map (8 x 8 grid cells) in a zeroed world: per 8 x 8 block a
// random area (0 sometimes), walk region and owner; random grid cells; an
// area node with random site counts and steps; a computer player with a few
// small tower notes. CComputerPlayer::AddNote is stubbed (it records the new
// note's bytes and frees it); CGameData::Random runs on the world, whose RNG
// state is reset before each version runs. Compared: AX, the stub calls with
// the notes, the DebugLog calls and the RNG state. Run: the garrison
// search, the building lookup and MakeBuildOrders stubbed; compared EAX,
// the agent, the area node and the stub calls.

namespace s3re::game {
namespace {

constexpr std::uint16_t kTestMap = 64;
constexpr std::uint32_t kTestGrid = kTestMap / 8;
constexpr std::size_t kTestNotes = 12;
constexpr std::size_t kNoteCompareBytes = 0x28;

struct SavedExpandGlobals {
    CGameData* world = g_aiWorld;
    ComputerPlayer* player = g_aiPlayer;
    AiPlayerSetup* setup = g_aiPlayerSetup;
    AiPlayerData* data = g_aiPlayerData;
    std::uint8_t number = g_aiPlayerNumber;
    std::uint16_t sizeX = g_aiMapSizeX, sizeY = g_aiMapSizeY;
    std::uint32_t land = g_aiLandEvaluation;
    ~SavedExpandGlobals() {
        g_aiWorld = world;
        g_aiPlayer = player;
        g_aiPlayerSetup = setup;
        g_aiPlayerData = data;
        g_aiPlayerNumber = number;
        g_aiMapSizeX = sizeX;
        g_aiMapSizeY = sizeY;
        g_aiLandEvaluation = land;
    }
};

// The AI's view of a small random map (shared by both versions).
struct ExpandScene {
    test::WorldBuffer worldBuffer{sizeof(CGameData)};
    std::vector<AiGridCell> cells = std::vector<AiGridCell>(kTestGrid * kTestGrid);
    AiLandEvaluation land{};
    std::vector<std::uint8_t> setupBytes = std::vector<std::uint8_t>(sizeof(AiPlayerSetup));
    std::vector<std::uint8_t> playerBytes = std::vector<std::uint8_t>(sizeof(ComputerPlayer));
    std::vector<RequestNote> notes = std::vector<RequestNote>(kTestNotes);
    std::vector<std::uint8_t> dataBytes = std::vector<std::uint8_t>(sizeof(AiAreaNode));

    CGameData* world() { return worldBuffer.world(); }
    AiPlayerSetup* setup() { return reinterpret_cast<AiPlayerSetup*>(setupBytes.data()); }
    ComputerPlayer* player() { return reinterpret_cast<ComputerPlayer*>(playerBytes.data()); }
    AiPlayerData* data() { return reinterpret_cast<AiPlayerData*>(dataBytes.data()); }

    void Install() {
        g_aiWorld = world();
        g_aiMapSizeX = kTestMap;
        g_aiMapSizeY = kTestMap;
        g_aiLandEvaluation = reinterpret_cast<std::uint32_t>(&land);
        g_aiPlayerSetup = setup();
        g_aiPlayer = player();
        g_aiPlayerData = data();
    }

    void RandomizeMap(test::Rng& rng) {
        const auto me = static_cast<std::uint8_t>(rng.Below(3));
        g_aiPlayerNumber = me;
        world()->players[me].race = static_cast<std::int32_t>(rng.Below(4));
        for (std::uint32_t bx = 0; bx < kTestGrid; ++bx) {
            for (std::uint32_t by = 0; by < kTestGrid; ++by) {
                const auto area = static_cast<std::uint16_t>(rng.Chance(1, 8) ? 0 : 1 + rng.Below(4));
                const auto region = static_cast<std::uint8_t>(1 + rng.Below(2));
                const std::uint32_t pick = rng.Below(4);
                const auto owner = static_cast<std::uint8_t>(pick == 0 ? 0xff : pick == 1 ? (me + 1) % 3 : me);
                for (std::uint32_t x = bx * 8; x < bx * 8 + 8; ++x) {
                    for (std::uint32_t y = by * 8; y < by * 8 + 8; ++y) {
                        layout::Tile& tile = world()->tiles[x][y];
                        tile.area = rng.Chance(1, 16) ? static_cast<std::uint16_t>(rng.Below(5)) : area;
                        tile.walk_region = rng.Chance(1, 32) ? static_cast<std::uint8_t>(rng.Below(3)) : region;
                        tile.owner = rng.Chance(1, 16) ? static_cast<std::uint8_t>(rng.Below(3)) : owner;
                    }
                }
            }
        }
        rng.Fill(cells.data(), cells.size() * sizeof(AiGridCell));
        for (AiGridCell& cell : cells) {
            // Mostly small sums, so that every resource test decides sometimes.
            cell.trees_around = static_cast<std::uint8_t>(rng.Below(14));
            cell.stones_around = static_cast<std::uint16_t>(rng.Below(14));
            cell.stream_tiles = static_cast<std::uint8_t>(rng.Chance(3, 4) ? 0 : rng.Below(4));
            for (std::uint16_t& amount : cell.resources_around) {
                amount = static_cast<std::uint16_t>(rng.Chance(3, 4) ? rng.Below(4) : rng.Below(8));
            }
            cell.grain_land_around = static_cast<std::uint8_t>(rng.Below(24));
            cell.wine_land_around = static_cast<std::uint8_t>(rng.Below(6));
            cell.rice_land_around = static_cast<std::uint8_t>(rng.Below(6));
        }
        land.width = kTestGrid;
        land.height = kTestGrid;
        land.cell_count = kTestGrid * kTestGrid;
        land.tiles_per_cell = 64;
        land.cells = cells.data();
        rng.Fill(setupBytes.data(), setupBytes.size());
        setup()->expand_search_radius =
            static_cast<std::uint16_t>(rng.Chance(1, 20) ? rng.Next32() : rng.Chance(1, 10) ? 0 : rng.Below(4));
        std::memset(playerBytes.data(), 0, playerBytes.size());
        rng.Fill(notes.data(), notes.size() * sizeof(RequestNote));
        for (RequestNote& note : notes) {
            note.base.type =
                static_cast<std::uint16_t>(rng.Chance(3, 4) ? Value(NoteType::DoBuildHouse) : rng.Below(5));
            note.building_type =
                static_cast<std::uint16_t>(rng.Chance(3, 4) ? Value(BuildingType::SmallTower) : rng.Below(20));
            note.x = static_cast<std::uint16_t>(rng.Below(kTestMap));
            note.y = static_cast<std::uint16_t>(rng.Below(kTestMap));
            if (rng.Chance(1, 2)) {
                player()->notes[rng.Below(kMaxNotes)] = &note.base;
            }
        }
        rng.Fill(world()->random_state, sizeof(world()->random_state));
    }

    void RandomizeData(test::Rng& rng) {
        rng.Fill(dataBytes.data(), dataBytes.size());
        data()->steps_since_expansion =
            rng.Chance(1, 3) ? kLongNotExpanded + rng.Below(3) - 1 : rng.Below(2 * kLongNotExpanded);
        for (SiteTree& tree : reinterpret_cast<AiAreaNode*>(data())->value.site_trees) {
            tree.size = rng.Chance(1, 3) ? 0 : rng.Below(5);
        }
    }
};

struct NoteRecord {
    std::uint32_t player, parent, requester;
    std::uint8_t bytes[kNoteCompareBytes];
    bool operator==(const NoteRecord& other) const {
        return player == other.player && parent == other.parent && requester == other.requester &&
               std::memcmp(bytes, other.bytes, sizeof(bytes)) == 0;
    }
};

// AddNote stubbed: records the note (unk1a is never written: heap bytes)
// and frees it.
void InstallAddNoteStub(test::CalleeStubs& stubs, std::vector<NoteRecord>*& log) {
    stubs.Install<3, kAddNote>([&log](CGameData*, std::uint32_t self, const std::uint32_t* args) {
        NoteRecord record{self, args[1], args[2], {}};
        std::memcpy(record.bytes, reinterpret_cast<const void*>(args[0]), kNoteCompareBytes);
        reinterpret_cast<RequestNote*>(record.bytes)->unk1a = 0;
        log->push_back(record);
        msvc6::GameOperatorDelete(reinterpret_cast<void*>(args[0]));
        return 0u;
    });
}

void CompareMakeBuildOrders(test::Context& ctx, ExpandOrdersFn candidate, int cases) {
    SavedExpandGlobals saved;
    ExpandScene scene;
    std::vector<NoteRecord>* log = nullptr;
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    InstallAddNoteStub(stubs, log);
    DebugLogRecorder recorder;
    int ordered = 0, searches = 0, longWaits = 0;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        if (i % 8 == 0) {
            scene.RandomizeMap(rng);
        }
        scene.RandomizeData(rng);
        scene.Install();
        const std::uint32_t x = rng.Chance(1, 20) ? rng.Next32() : rng.Below(kTestMap) | (rng.Next32() & 0xffff0000u);
        const std::uint32_t y = rng.Chance(1, 20) ? rng.Next32() : rng.Below(kTestMap) | (rng.Next32() & 0xffff0000u);
        const std::uint32_t radius = rng.Below(12);
        rng.Fill(scene.world()->random_state, sizeof(scene.world()->random_state));
        std::uint16_t randomState[3];
        std::memcpy(randomState, scene.world()->random_state, sizeof(randomState));
        std::vector<NoteRecord> notesA, notesB;
        ExpandEmpireAgent agent{};
        log = &notesA;
        const std::uint32_t ra =
            Original_GLOBAL_EXPAND_EMPIRE_AGENT_MakeBuildOrders(&agent, nullptr, x, y, radius) & 0xffff;
        const std::vector<DebugLogCall> logA = recorder.Take();
        std::uint16_t stateA[3];
        std::memcpy(stateA, scene.world()->random_state, sizeof(stateA));
        std::memcpy(scene.world()->random_state, randomState, sizeof(randomState));
        log = &notesB;
        const std::uint32_t rb = candidate(&agent, nullptr, x, y, radius) & 0xffff;
        const std::vector<DebugLogCall> logB = recorder.Take();
        stubs.Take();
        ordered += notesA.empty() ? 0 : 1;
        for (const DebugLogCall& call : logA) {
            searches += call.format == texts::kExpandResources.address() ? 1 : 0;
            longWaits += call.format == texts::kExpandLongNotExpanded.address() ? 1 : 0;
        }
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT(notesA == notesB);
        same &= S3RE_EXPECT(logA == logB);
        same &= S3RE_EXPECT_MEM_EQ(stateA, scene.world()->random_state, sizeof(stateA));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (%u vs %u log lines)", i, static_cast<unsigned>(logA.size()),
                     static_cast<unsigned>(logB.size()));
        }
    }
    if (cases >= 1000) {
        S3RE_EXPECT(ordered > cases / 20);
        S3RE_EXPECT(searches > cases / 20);
        S3RE_EXPECT(longWaits > 0);
    }
}

std::uint32_t __fastcall WrongMakeBuildOrders(ExpandEmpireAgent* self, void* edx, std::uint32_t x, std::uint32_t y,
                                              std::uint32_t radius) {
    // Wrong: one step less beyond the territory.
    return GLOBAL_EXPAND_EMPIRE_AGENT_MakeBuildOrders(self, edx, x, y, radius == 0 ? 0 : radius - 1);
}

void CompareRun(test::Context& ctx, ExpandRunFn candidate, int cases) {
    SavedExpandGlobals saved;
    test::WorldBuffer world(sizeof(CGameData));  // (AI::TerritoryRadius reads the world's map offsets)
    g_aiWorld = world.world();
    std::vector<std::uint8_t> dataA(sizeof(AiPlayerData)), dataB(sizeof(AiPlayerData));
    layout::Building building{};
    std::vector<std::uint32_t> answers;
    std::size_t answerIndex = 0;
    test::Rng* stubRng = nullptr;
    const auto answer = [&]() {
        if (answerIndex == answers.size()) {
            answers.push_back(stubRng->Chance(1, 4) ? 0xffff : stubRng->Next32());
        }
        return answers[answerIndex++];
    };
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    stubs.Install<0, kNextGarrisonedMilitaryBuilding>(
        [&](CGameData*, std::uint32_t, const std::uint32_t*) { return answer(); });
    stubs.Install<0, kBuildingIfUsed>(
        [&](CGameData*, std::uint32_t, const std::uint32_t*) { return reinterpret_cast<std::uint32_t>(&building); });
    stubs.Install<3, kMakeBuildOrders>([&](CGameData*, std::uint32_t, const std::uint32_t*) { return answer(); });
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        stubRng = &rng;
        answers.clear();
        rng.Fill(dataA.data(), dataA.size());
        if (rng.Chance(1, 3)) {
            reinterpret_cast<AiPlayerData*>(dataA.data())->best[kOrderedCandidates + Value(BuildingType::SmallTower)].score = 0;
        }
        dataB = dataA;
        g_aiPlayerNumber = static_cast<std::uint8_t>(rng.Below(20));
        rng.Fill(&building, sizeof(building));
        building.art = static_cast<std::uint8_t>(rng.Chance(3, 4) ? 15 + rng.Below(4) : rng.Below(60));
        ExpandEmpireAgent agentA{}, agentB{};
        rng.Fill(&agentA, sizeof(agentA));
        agentB = agentA;
        answerIndex = 0;
        g_aiPlayerData = reinterpret_cast<AiPlayerData*>(dataA.data());
        const std::uint32_t ra = Original_GLOBAL_EXPAND_EMPIRE_AGENT_Run(&agentA, nullptr);
        std::vector<test::StubCall> callsA = stubs.Take();
        answerIndex = 0;
        g_aiPlayerData = reinterpret_cast<AiPlayerData*>(dataB.data());
        std::uint32_t rb = candidate(&agentB, nullptr);
        std::vector<test::StubCall> callsB = stubs.Take();
        // The original passes the ids and coordinates in registers or
        // pushes whose upper halves are leftovers (the callees read words):
        // compare the low words, and `this` relative to each agent.
        const auto normalize = [](std::vector<test::StubCall>& calls, const ExpandEmpireAgent* agent) {
            for (test::StubCall& call : calls) {
                if (call.callee == kMakeBuildOrders) {
                    call.self -= reinterpret_cast<std::uint32_t>(agent);
                    call.args[0] &= 0xffff;
                    call.args[1] &= 0xffff;
                } else {
                    call.self &= 0xffff;
                }
            }
        };
        normalize(callsA, &agentA);
        normalize(callsB, &agentB);
        // (EAX is the node's address when no tower was taken)
        if (ra == reinterpret_cast<std::uint32_t>(dataA.data()) && rb == reinterpret_cast<std::uint32_t>(dataB.data())) {
            rb = ra;
        }
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT_MEM_EQ(&agentA, &agentB, sizeof(agentA));
        same &= S3RE_EXPECT_MEM_EQ(dataA.data(), dataB.data(), dataA.size());
        same &= S3RE_EXPECT(callsA == callsB);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
        }
    }
}

std::uint32_t __fastcall WrongRun(ExpandEmpireAgent* self, void* edx) {
    // Wrong: the cursor keeps its upper half.
    const std::uint32_t high = self->military_cursor & 0xffff0000u;
    const std::uint32_t result = GLOBAL_EXPAND_EMPIRE_AGENT_Run(self, edx);
    self->military_cursor |= high;
    return result;
}

}  // namespace

S3RE_TEST(AiExpandEmpireAgent_MatchOriginal) {
    CompareMakeBuildOrders(ctx, &GLOBAL_EXPAND_EMPIRE_AGENT_MakeBuildOrders, 3000);
    CompareRun(ctx, &GLOBAL_EXPAND_EMPIRE_AGENT_Run, 2000);
}

S3RE_TEST(AiExpandEmpireAgent_ComparisonDetectsWrongImplementation) {
    {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareMakeBuildOrders(quiet, &WrongMakeBuildOrders, 3000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
    {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareRun(quiet, &WrongRun, 2000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

}  // namespace s3re::game
