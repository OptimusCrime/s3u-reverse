// Removing buildings and construction sites (callees of CrushBuilding
// 0x4d9250 and its helpers).
//
// CGameData::ClearBuildingFootprint (0x0051aae0): (building) thiscall,
//   ret 4. With (x, y) the building's position, type +0x6 and race +0x7:
//   tile (x, y).object = 0; then in the building's layout (S3.EXE 0x6aa170,
//   see building_sites.cpp), after the 0x80000000 marker: for each (dx, dy)
//   up to 0x80000001 (the footprint), tile (x+dx, y+dy): flag 0x08 cleared,
//   object 0x71 (a stake) -> 0, then UnblockTile(x+dx, y+dy); after it, up
//   to 0x80000003, skipping a 0x80000002 marker (the rest of the footprint
//   and the border): flag 0x08 cleared and stake 0x71 -> 0 only. Then, if
//   the building is still in buildup stage 1 (+0x21 == 1, a construction
//   site being levelled), every non-zero cell (i, j) of its levelling grid
//   (site cache of its owner, slot +0x22: grid[j * 31 + i]) clears flag
//   0x04 of tile (x - 15 + j, y - 15 + i), and the slot is freed (valid =
//   0). Positions are not range-checked (32-bit tile arithmetic as in the
//   original). EAX not used by the only caller, CrushBuilding.
// CGameData::BlockTileAndMoveSettler (0x004dbb10): (x, y) thiscall, ret 8.
//   If a settler stands on the tile: ResetSettlerToIdle(settler), tile
//   settler word = 0, BlockTile(x, y), object = 0, PlaceSettlerNear(settler,
//   x, y); else BlockTile(x, y), object = 0. EAX not used by its caller.
// CGameData::BlockBuildingLine (0x004dbb90): (building) thiscall, ret 4.
//   Only for a finished building (+0x21 == 0) whose word +0x22 is a
//   direction (not 0xffff): from the work tile (+0x30/+0x32) along that
//   direction (NeighbourX/Y), BlockTileAndMoveSettler on every tile until
//   one of terrain 0x30 (the shore, kTerrainShore); then +0x22 = 0xffff.
//   CrushBuilding calls it for shipyards and harbours, so probably the line
//   to the water their ships use. Callers CrushBuilding and ExecuteCommands;
//   EAX unused.
// The three "Drop...Pile" functions (pile) thiscall, ret 4, called by
// RemoveBuildingStorage 0x4de430 for a building's input, storage and output
// piles: settlers bound to the pile get ResetSettlerToIdle, the pile is
// taken off its tile (RemovePileFromTile) and, if it held goods, they are
// put back on the ground there as a new dropped pile (CreatePile(x, y, good,
// amount, 0xfe)). Settler scans run over 1..high water (re-read), skip
// records without owner, and test the job +0xe and either the building
// +0x22 (== pile) or the target +0x24/+0x26 (== the pile's position).
// CGameData::DropInputPile (0x00504a00): if units are coming (high nibble of
//   +0x2): carriers with jobs 0x4c..0x4f bringing to it are reset. DebugLog
//   "TROUBLE: cannot find all people that wanted to bring goods" (always).
//   If units are leaving (+0x2 != 0): ships' jobs 0x133/0x134 targeting it are
//   reset. Kind +0x4 = 0xfe, amount = 0 (after reading good and amount),
//   RemovePileFromTile, CreatePile if the amount was not 0.
// CGameData::DropStoragePile (0x00504b70): area = the pile tile's area. If
//   units are coming (+0x3): carriers with jobs 0x4c..0x4f bringing to it are
//   reset, the count goes down per reset, and if some are left DebugLog
//   "TROUBLE: ...". If units are leaving: fetchers (jobs 0xa, 0xb, 0xa9,
//   0x4c, 0x4d, 0x68, 0x69) targeting it are reset. Then amount times
//   PileIncAmountLeaving + TakeGoodFromPile (the amount read before); if the
//   good byte is not 0xff: DebugLog "RemoveStorage: Pushing pile in
//   EmptyList!" and MakePileEmpty. If kind, amount and coming are then all 0,
//   the pile is unlinked from its area's empty list (+0x8). Kind = 0xfe,
//   RemovePileFromTile, CreatePile with the amount read before.
// CGameData::DropOutputPile (0x00504e20): an empty pile (amount 0) is only
//   taken off its tile. Else: if units are leaving, fetchers targeting it are
//   reset; then amount (re-read) times PileIncAmountLeaving +
//   TakeGoodFromPile, RemovePileFromTile and CreatePile(x, y, good, amount,
//   0xfe).
// EAX of all three is not used by RemoveBuildingGoods.
// CGameData::RemoveBuildingGoods (0x004de430): (building) thiscall, ret 4,
//   only caller CrushBuilding; EAX unused. For its piles +0x10, +0x12 and
//   (only if finished, +0x21 == 0) +0x14: the units coming (+0x3) are summed;
//   a pile with goods: leaving = 0, SubtractPlayerCounter(owner, good,
//   amount), DropGoodNear(good, amount, x, y), amount = 0. DebugLog "%u Waren
//   hierher unterwegs" (the sum); every settler with an owner, building
//   +0x22 == it and job 0xa, 0xb, 0xa9, 0xaa, 0xc, 0xd or 0x50 gets
//   ResetSettlerToIdle and the sum goes down by one; DebugLog "Ergebnis nach
//   Reduzierung %u". Finished building: RemovePileFromTile for the three
//   piles; DropOutputPile(+0xe) for the producing types (2, 3, 4, 7..0xb,
//   0xe, 0x11, 0x14..0x19, 0x1d, 0x38, 0x1e, 0x24..0x28, 0x33, 0x34);
//   DropOutputPile on the piles of the tiles around the door for 0xc (door,
//   (-1,0), (1,0), (0,-1), (0,1), (-1,-1), (1,1)), 0xd (door, (-1,0),
//   (1,0)) and 0x35 ((1,0), (-1,0)); DropStoragePile around the position for
//   1 ((2,0), (-2,0), (0,2), (0,-2), (2,2), (-2,-2)); DropInputPile on the
//   six store tiles of 0x22/0x21 (tables 0x6594a4/0x659564 by the building's
//   race). A site: RemovePileFromTile for +0x10 and +0x12. Then
//   RemoveBuildingFromAreaRequests, and half of the construction materials
//   of the layout (header word 1: bits 8..15 good 1, bits 0..7 good 2; for a
//   site minus the still missing +0x19/+0x1a, 32-bit) go back on the ground
//   with DropGoodNear in portions of up to 8. (Was "RemoveBuildingStorage",
//   the CSV's proposal.)

#include "game/debug_log.h"
#include "game/enums/buildings.h"
#include "game/enums/goods.h"
#include "game/enums/job_ids.h"
#include "game/enums/objects.h"
#include "game/enums/piles.h"
#include "game/enums/settler_types.h"
#include "game/enums/terrain.h"
#include "game/globals.h"
#include "game/replacements.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/typed_fields.h"
#include "game/world.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

namespace s3re::game {

namespace {

using layout::Building;
using layout::PlayerSiteCache;
using layout::Tile;

using B = BuildingType;
using Job = SettlerJob;

constexpr std::uint32_t kLayoutTypes = kBuildingTypeCount;
constexpr std::uint32_t kMarkFootprint = 0x80000000;
constexpr std::uint32_t kMarkFootprintEnd = 0x80000001;
constexpr std::uint32_t kMarkBorder = 0x80000002;
constexpr std::uint32_t kMarkEnd = 0x80000003;

constexpr std::uint8_t kTileFootprint = 0x08;  // Tile::flags: part of a building
constexpr std::uint8_t kTileLevelling = 0x04;  // Tile::flags: being levelled
constexpr std::uint32_t kGrid = 31;
constexpr std::uint32_t kGridCentre = 15;
constexpr std::uint32_t kSlotsPerPlayer = 30;

Tile& TileAt(CGameData* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

const std::uint32_t* Layout(std::uint32_t race, std::uint32_t type) {
    return g_buildingLayouts[type + race * kLayoutTypes];
}

bool IsOneOf(Job job, std::initializer_list<Job> jobs) {
    for (const Job candidate : jobs) {
        if (candidate == job) {
            return true;
        }
    }
    return false;
}

bool IsOneOf(BuildingType type, std::initializer_list<BuildingType> types) {
    for (const BuildingType candidate : types) {
        if (candidate == type) {
            return true;
        }
    }
    return false;
}

// Clears the footprint mark and a stake on one tile.
void ClearFootprintTile(CGameData* world, std::uint32_t x, std::uint32_t y) {
    Tile& tile = TileAt(world, x, y);
    tile.flags &= static_cast<std::uint8_t>(~kTileFootprint);
    if (tile.object == kObjectStake) {
        tile.object = 0;
    }
}

// Callees reached through their S3.EXE addresses: verified ones (Detours
// sends the call to our replacement, behind its counting thunk) whose effects
// the tests replace by recording stubs, and those of other work packages.
using TileFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);
using SettlerFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t settler);
using PileFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t pile);
constexpr std::uintptr_t kUnblockTile = 0x0051b770;         // CGameData::UnblockTile (area_split.cpp)
constexpr std::uintptr_t kBlockTile = 0x0051b6f0;           // CGameData::BlockTile (area_split.cpp)
constexpr std::uintptr_t kResetSettlerToIdle = 0x0050cf50;  // package B
constexpr std::uintptr_t kTakeGoodFromPile = 0x00505820;    // package B

void ResetSettlerToIdle(CGameData* world, std::uint32_t settler) {
    reinterpret_cast<SettlerFn>(kResetSettlerToIdle)(world, nullptr, settler);
}

void TakeGoodFromPile(CGameData* world, std::uint32_t pile) {
    reinterpret_cast<PileFn>(kTakeGoodFromPile)(world, nullptr, pile);
}

constexpr std::uint8_t kFree = 0xff;         // Settler::owner / Building::owner of an unused record
constexpr std::uint8_t kGoodNone = Value(Good::Empty);
constexpr std::uint8_t kLineEndTerrain = kTerrainShore;  // a building's line ends at the shore (0x30)
constexpr std::uint16_t kNoDirection = 0xffff;

// Carriers bringing goods to a building pile (jobs 76..79, 0x4c..0x4f).
bool IsDeliveringJob(Job job) {
    return IsOneOf(job, {Job::CarrierWalkToSourcePile, Job::CarrierAtSourcePile, Job::CarrierWalkToTargetPile,
                         Job::CarrierAtTargetPile});
}

// Settlers on their way to take goods from a pile (its position is their target).
bool IsFetchingJob(Job job) {
    return IsOneOf(
        job, {Job::CarrierWalkToPile, Job::CarrierAtPile, Job::CarrierPickUp, Job::CarrierWalkToSourcePile,
              Job::CarrierAtSourcePile, Job::CarrierWalkToTool, Job::CarrierTakeTool});
}

// Resets every settler with an owner for which `match` is true.
template <typename Match>
void ResetSettlersWhere(CGameData* world, Match match) {
    for (std::uint32_t n = 1; n <= world->settler_high_water; ++n) {
        const layout::Settler& settler = world->settlers[n];
        if (settler.owner != kFree && match(settler)) {
            ResetSettlerToIdle(world, n);
        }
    }
}

// Resets the settlers about to fetch goods from the pile.
void ResetFetchers(CGameData* world, const layout::Pile& pile) {
    ResetSettlersWhere(world, [&](const layout::Settler& s) {
        return IsFetchingJob(s.job) && s.target_x == pile.x && s.target_y == pile.y;
    });
}

}  // namespace

using BuildingFn = void(__fastcall*)(CGameData* world, void* edx, std::uint32_t building);
using PositionFn = void(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);
using DropPileFn = void(__fastcall*)(CGameData* world, void* edx, std::uint32_t pile);

std::uint32_t __fastcall CGameData_PlaceSettlerNear(CGameData* world, void* edx, std::uint32_t settler, std::uint32_t x,
                                                    std::uint32_t y);
std::uint32_t __fastcall CGameData_RemovePileFromTile(CGameData* world, void* edx, std::uint32_t pile);
std::uint32_t __fastcall CGameData_MakePileEmpty(CGameData* world, void* edx, std::uint32_t pile);

// Purpose: Takes a building (or construction site) off the map: clears its
// footprint marks and stakes, unblocks the tiles it stood on and, for a site
// still being levelled, the levelling marks and its slot in the site cache.
// CGameData::ClearBuildingFootprint (0x51aae0).
void __fastcall CGameData_ClearBuildingFootprint(CGameData* world, void*, std::uint32_t number) {
    Building& building = world->buildings[number];
    const std::uint32_t x = building.x;
    const std::uint32_t y = building.y;
    TileAt(world, x, y).object = 0;
    const std::uint32_t* p = Layout(building.race, building.art);
    while (*p++ != kMarkFootprint) {
    }
    // The part of the footprint that was blocked: also unblocked.
    for (; *p != kMarkFootprintEnd; p += 2) {
        const std::uint32_t tx = x + p[0];
        const std::uint32_t ty = y + p[1];
        ClearFootprintTile(world, tx, ty);
        reinterpret_cast<TileFn>(kUnblockTile)(world, nullptr, tx, ty);
    }
    // The rest of the footprint and the border.
    ++p;
    while (*p != kMarkEnd) {
        if (*p == kMarkBorder) {
            ++p;
        }
        ClearFootprintTile(world, x + p[0], y + p[1]);
        p += 2;
    }
    if (building.buildup != 1) {  // only a site in its levelling stage
        return;
    }
    // The original indexes the site caches as one flat array (owner * 30 + slot), re-reading both per cell.
    for (std::uint32_t i = 0; i < kGrid; ++i) {
        for (std::uint32_t j = 0; j < kGrid; ++j) {
            const PlayerSiteCache& cache = world->site_cache[0][building.owner * kSlotsPerPlayer + building.levelling_slot];
            if (cache.grid[j * kGrid + i] != 0) {
                TileAt(world, building.x + j - kGridCentre, building.y + i - kGridCentre).flags &=
                    static_cast<std::uint8_t>(~kTileLevelling);
            }
        }
    }
    world->site_cache[0][building.owner * kSlotsPerPlayer + building.levelling_slot].valid = 0;
}

// Purpose: Blocks a tile for a building, removing its landscape object and
// sending a settler standing there to the nearest free tile.
// CGameData::BlockTileAndMoveSettler (0x4dbb10).
void __fastcall CGameData_BlockTileAndMoveSettler(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    Tile& tile = TileAt(world, x, y);
    const std::uint32_t settler = tile.settler;
    if (settler != 0) {
        ResetSettlerToIdle(world, settler);
        tile.settler = 0;
        reinterpret_cast<TileFn>(kBlockTile)(world, nullptr, x, y);
        tile.object = 0;
        CGameData_PlaceSettlerNear(world, nullptr, settler, x, y);
    } else {
        reinterpret_cast<TileFn>(kBlockTile)(world, nullptr, x, y);
        tile.object = 0;
    }
}

// Purpose: Blocks the straight line of tiles a finished building keeps from
// its work tile in one direction down to the shore (probably a shipyard's
// slipway or a harbour's landing) and forgets the direction.
// CGameData::BlockBuildingLine (0x4dbb90).
void __fastcall CGameData_BlockBuildingLine(CGameData* world, void*, std::uint32_t number) {
    Building& building = world->buildings[number];
    if (building.buildup != 0 || building.dock_direction == kNoDirection) {
        return;
    }
    const std::uint32_t direction = building.dock_direction;
    std::uint32_t x = building.work_x;
    std::uint32_t y = building.work_y;
    while (TileAt(world, x, y).terrain != kLineEndTerrain) {
        CGameData_BlockTileAndMoveSettler(world, nullptr, x, y);
        x = NeighbourX(x, direction);
        y = NeighbourY(y, direction);
    }
    building.dock_direction = kNoDirection;
}

// Purpose: Gives up a building's input pile: carriers bringing goods to it
// and ships loading from it stop, and its goods are left on the ground.
// CGameData::DropInputPile (0x504a00).
void __fastcall CGameData_DropInputPile(CGameData* world, void*, std::uint32_t number) {
    layout::Pile& pile = world->piles[number];
    if ((pile.leaving >> 4) != 0) {  // units coming, in the high nibble
        ResetSettlersWhere(world, [&](const layout::Settler& s) {
            return s.building == number && IsDeliveringJob(s.job);
        });
    }
    GameDebugLog(texts::kCannotFindBringers);  // always, as in the original
    if (pile.leaving != 0) {
        // Jobs 307/308 (0x133/0x134): donkeys fetching from this pile (DonkeyWalkToPile, DonkeyLoad).
        ResetSettlersWhere(world, [&](const layout::Settler& s) {
            return IsOneOf(s.job, {Job::DonkeyWalkToPile, Job::DonkeyLoad}) && s.target_x == pile.x &&
                   s.target_y == pile.y;
        });
    }
    const std::uint32_t x = pile.x;
    const std::uint32_t y = pile.y;
    const std::uint32_t good = pile.good;
    const std::uint32_t amount = pile.amount;
    pile.art = Value(PileKind::Dropped);
    pile.amount = 0;
    CGameData_RemovePileFromTile(world, nullptr, number);
    if (amount != 0) {
        CGameData_CreatePile(world, nullptr, x, y, good, amount, Value(PileKind::Dropped));
    }
}

// Purpose: Gives up a storage pile of a building: deliveries to it and
// fetches from it are cancelled and its goods are left on the ground.
// CGameData::DropStoragePile (0x504b70).
void __fastcall CGameData_DropStoragePile(CGameData* world, void*, std::uint32_t number) {
    layout::Pile& pile = world->piles[number];
    const std::uint32_t area = TileAt(world, pile.x, pile.y).area;
    std::uint32_t coming = pile.coming;
    if (coming != 0) {
        for (std::uint32_t n = 1; n <= world->settler_high_water; ++n) {
            const layout::Settler& settler = world->settlers[n];
            if (settler.owner != kFree && settler.building == number && IsDeliveringJob(settler.job)) {
                ResetSettlerToIdle(world, n);
                --coming;
            }
        }
        if (coming != 0) {
            GameDebugLog(texts::kCannotFindBringers);
        }
    }
    if (pile.leaving != 0) {
        ResetFetchers(world, pile);
    }
    const std::uint32_t amount = pile.amount;
    const std::uint32_t good = pile.good;
    const std::uint32_t x = pile.x;
    const std::uint32_t y = pile.y;
    for (std::uint32_t k = amount; k != 0; --k) {
        CGameData_PileIncAmountLeaving(world, nullptr, number);
        TakeGoodFromPile(world, number);
    }
    if (pile.good != kGoodNone) {
        GameDebugLog(texts::kPushingPileInEmptyList);
        CGameData_MakePileEmpty(world, nullptr, number);
    }
    // An empty pile record leaves its area's list of empty piles.
    if (pile.art == 0 && pile.amount == 0 && pile.coming == 0) {
        if (pile.prev != 0) {
            world->piles[pile.prev].next = pile.next;
        } else {
            world->areas[area].pile_list = pile.next;
        }
        if (pile.next != 0) {
            world->piles[pile.next].prev = pile.prev;
        }
    }
    pile.art = Value(PileKind::Dropped);
    CGameData_RemovePileFromTile(world, nullptr, number);
    if (amount != 0) {
        CGameData_CreatePile(world, nullptr, x, y, good, amount, Value(PileKind::Dropped));
    }
}

// Purpose: Gives up a building's output pile: settlers about to fetch from it
// stop, and its goods are left on the ground.
// CGameData::DropOutputPile (0x504e20).
void __fastcall CGameData_DropOutputPile(CGameData* world, void*, std::uint32_t number) {
    layout::Pile& pile = world->piles[number];
    if (pile.amount == 0) {
        CGameData_RemovePileFromTile(world, nullptr, number);
        return;
    }
    if (pile.leaving != 0) {
        ResetFetchers(world, pile);
    }
    const std::uint32_t x = pile.x;
    const std::uint32_t y = pile.y;
    const std::uint32_t good = pile.good;
    const std::uint32_t amount = pile.amount;
    for (std::uint32_t k = amount; k != 0; --k) {
        CGameData_PileIncAmountLeaving(world, nullptr, number);
        TakeGoodFromPile(world, number);
    }
    CGameData_RemovePileFromTile(world, nullptr, number);
    CGameData_CreatePile(world, nullptr, x, y, good, amount, Value(PileKind::Dropped));
}

S3RE_HOOK(0x0051aae0, CGameData_ClearBuildingFootprint, BuildingFn, Original_CGameData_ClearBuildingFootprint,
          CGameData_ClearBuildingFootprint);
S3RE_HOOK(0x004dbb10, CGameData_BlockTileAndMoveSettler, PositionFn, Original_CGameData_BlockTileAndMoveSettler,
          CGameData_BlockTileAndMoveSettler);
S3RE_HOOK(0x004dbb90, CGameData_BlockBuildingLine, BuildingFn, Original_CGameData_BlockBuildingLine,
          CGameData_BlockBuildingLine);
S3RE_HOOK(0x00504a00, CGameData_DropInputPile, DropPileFn, Original_CGameData_DropInputPile, CGameData_DropInputPile);
S3RE_HOOK(0x00504b70, CGameData_DropStoragePile, DropPileFn, Original_CGameData_DropStoragePile,
          CGameData_DropStoragePile);
S3RE_HOOK(0x00504e20, CGameData_DropOutputPile, DropPileFn, Original_CGameData_DropOutputPile,
          CGameData_DropOutputPile);

// --- RemoveBuildingGoods ----------------------------------------------------

namespace {

// Callees of RemoveBuildingGoods through their S3.EXE addresses, so that the
// tests can record them: DropGoodNear (package B) and the three Drop*Pile
// functions above (verified, own tests).
using DropGoodFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t good, std::uint32_t amount,
                                              std::uint32_t x, std::uint32_t y);
constexpr std::uintptr_t kDropGoodNear = 0x005046b0;
constexpr std::uintptr_t kDropInputPile = 0x00504a00;
constexpr std::uintptr_t kDropStoragePile = 0x00504b70;
constexpr std::uintptr_t kDropOutputPile = 0x00504e20;

constexpr std::uint32_t kGoodBoards = Value(Good::Board);  // the construction materials
constexpr std::uint32_t kGoodStones = Value(Good::Stone);
constexpr std::uint32_t kDropPortion = 8;  // DropGoodNear takes at most a full pile
constexpr std::uint32_t kInputPiles = 3;   // Building::piles
constexpr std::uint32_t kStorePiles = 6;   // around a harbour or marketplace

void DropGoodNear(CGameData* world, std::uint32_t good, std::uint32_t amount, std::uint32_t x, std::uint32_t y) {
    reinterpret_cast<DropGoodFn>(kDropGoodNear)(world, nullptr, good, amount, x, y);
}

void CallPileFn(CGameData* world, std::uintptr_t address, std::uint32_t pile) {
    reinterpret_cast<PileFn>(address)(world, nullptr, pile);
}

// The pile number on the tile `dx`/`dy` from (x, y) (32-bit tile arithmetic).
std::uint32_t PileOn(CGameData* world, std::uint32_t x, std::uint32_t y, std::int32_t dx, std::int32_t dy) {
    return TileAt(world, x + static_cast<std::uint32_t>(dx), y + static_cast<std::uint32_t>(dy)).object_number;
}

// Settlers busy with the building's goods: carriers on their way with or for goods.
bool IsBuildingGoodsJob(Job job) {
    return IsOneOf(job, {Job::CarrierWalkToPile, Job::CarrierAtPile, Job::CarrierPickUp, Job::CarrierLeavePile,
                         Job::CarrierWalkToBuilding, Job::CarrierAtBuilding, Job::CarrierDeliver});
}

// The types with one output pile at the door (Building::pile_ring, created by BuildingDone).
bool HasOutputPile(BuildingType type) {
    return IsOneOf(type, {B::Woodcutter, B::Stonecutter, B::Sawmill, B::CoalMine, B::GoldMine, B::IronMine,
                          B::GoldSmelter, B::IronSmelter, B::Vineyard, B::Mill, B::Bakery, B::Slaughterhouse,
                          B::Distillery, B::PigFarm, B::Farm, B::Fishery, B::SulfurMine, B::AmazonSulfurMine,
                          B::Waterworks, B::RiceFarm, B::GemMine, B::Brewery, B::CharcoalBurner, B::PowderMaker,
                          B::Apiary, B::Meadery});
}

// Puts `amount` units of `good` near (x, y) in portions of at most 8.
void DropInPortions(CGameData* world, std::uint32_t good, std::uint32_t amount, std::uint32_t x, std::uint32_t y) {
    while (amount != 0) {
        if (amount <= kDropPortion) {
            DropGoodNear(world, good, amount, x, y);
            return;
        }
        DropGoodNear(world, good, kDropPortion, x, y);
        amount -= kDropPortion;
    }
}

// A finished building's piles are given up (the original's part for buildup 0).
void DropFinishedBuildingPiles(CGameData* world, const Building& b) {
    if (b.piles[0] != 0) {
        CGameData_RemovePileFromTile(world, nullptr, b.piles[0]);
    }
    if (b.piles[1] != 0) {
        CGameData_RemovePileFromTile(world, nullptr, b.piles[1]);
    }
    if (b.piles[2] != 0) {
        CGameData_RemovePileFromTile(world, nullptr, b.piles[2]);
    }
    const BuildingType type = TypeOf(b);
    if (HasOutputPile(type)) {
        CallPileFn(world, kDropOutputPile, b.pile_ring);
    }
    // The smiths' and the laboratory's piles around the door (BuildingDone), in the original's order.
    if (type == B::Toolsmith) {
        static constexpr std::int32_t kTools[7][2] = {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, 1}};
        for (const auto& d : kTools) {
            CallPileFn(world, kDropOutputPile, PileOn(world, b.door_x, b.door_y, d[0], d[1]));
        }
    }
    if (type == B::Weaponsmith) {
        static constexpr std::int32_t kWeapons[3][2] = {{0, 0}, {-1, 0}, {1, 0}};
        for (const auto& d : kWeapons) {
            CallPileFn(world, kDropOutputPile, PileOn(world, b.door_x, b.door_y, d[0], d[1]));
        }
    }
    if (type == B::Laboratory) {
        CallPileFn(world, kDropOutputPile, PileOn(world, b.door_x, b.door_y, 1, 0));
        CallPileFn(world, kDropOutputPile, PileOn(world, b.door_x, b.door_y, -1, 0));
    }
    if (type == B::Storehouse) {
        static constexpr std::int32_t kStores[kStorePiles][2] = {{2, 0}, {-2, 0}, {0, 2}, {0, -2}, {2, 2}, {-2, -2}};
        for (const auto& d : kStores) {
            CallPileFn(world, kDropStoragePile, PileOn(world, b.x, b.y, d[0], d[1]));
        }
    }
    // The ring of store piles of a marketplace or harbour (S3.EXE tables 0x6594a4, 0x659564, by race).
    for (const auto& store : {std::pair<B, const std::int32_t*>{B::Marketplace, &g_storePileOffsets22[0][0][0]},
                              std::pair<B, const std::int32_t*>{B::Harbour, &g_storePileOffsets21[0][0][0]}}) {
        if (type != store.first) {
            continue;
        }
        const std::int32_t* offsets = store.second + b.race * (kStorePiles * 2);
        for (std::uint32_t k = 0; k < kStorePiles; ++k) {
            CallPileFn(world, kDropInputPile, PileOn(world, b.x, b.y, offsets[2 * k], offsets[2 * k + 1]));
        }
    }
}

}  // namespace

std::uint32_t __fastcall CGameData_SubtractPlayerCounter(CGameData* world, void* edx, std::uint32_t player,
                                                         std::uint32_t index, std::uint32_t amount);
std::uint32_t __fastcall CGameData_RemoveBuildingFromAreaRequests(CGameData* world, void* edx, std::uint32_t building);

// Purpose: Empties a building that is being removed (or a construction
// site): the goods in its piles go on the ground nearby, carriers and
// workers busy with them stop, its piles are given up, and half of its
// construction materials (of what was delivered, for a site) are left on the
// ground. CGameData::RemoveBuildingGoods (0x4de430).
void __fastcall CGameData_RemoveBuildingGoods(CGameData* world, void*, std::uint32_t number) {
    Building& b = world->buildings[number];
    // The input piles' goods go on the ground (the third only for a finished building); the pile fields
    // are re-read after each call, as in the original.
    std::uint32_t coming = 0;
    for (std::uint32_t k = 0; k < kInputPiles; ++k) {
        if (b.piles[k] == 0 || (k == 2 && b.buildup != 0)) {
            continue;
        }
        const layout::Pile& pile = world->piles[b.piles[k]];
        coming += pile.coming;
        if (pile.amount == 0) {
            continue;
        }
        world->piles[b.piles[k]].leaving = 0;
        CGameData_SubtractPlayerCounter(world, nullptr, b.owner, world->piles[b.piles[k]].good,
                                        world->piles[b.piles[k]].amount);
        DropGoodNear(world, world->piles[b.piles[k]].good, world->piles[b.piles[k]].amount, b.x, b.y);
        world->piles[b.piles[k]].amount = 0;
    }
    GameDebugLog(texts::kGoodsOnTheirWay, coming);
    for (std::uint32_t n = 1; n <= world->settler_high_water; ++n) {
        const layout::Settler& settler = world->settlers[n];
        if (settler.owner != kFree && settler.building == number && IsBuildingGoodsJob(settler.job)) {
            ResetSettlerToIdle(world, n);
            --coming;
        }
    }
    GameDebugLog(texts::kResultAfterReduction, coming);

    if (b.buildup == 0) {
        DropFinishedBuildingPiles(world, b);
    } else {
        if (b.piles[0] != 0) {
            CGameData_RemovePileFromTile(world, nullptr, b.piles[0]);
        }
        if (b.piles[1] != 0) {
            CGameData_RemovePileFromTile(world, nullptr, b.piles[1]);
        }
    }
    CGameData_RemoveBuildingFromAreaRequests(world, nullptr, number);

    // Half of the construction materials (layout header word 1: boards in
    // bits 8..15, stones in bits 0..7); for a site only what was delivered
    // (the still missing amounts +0x19/+0x1a subtracted, 32-bit).
    const std::uint32_t materials = Layout(b.race, b.art)[1];
    std::uint32_t boards = (materials >> 8) & 0xff;
    std::uint32_t stones = materials & 0xff;
    if (b.buildup != 0) {
        boards -= b.site.boards_missing;
        stones -= b.site.stones_missing;
    }
    DropInPortions(world, kGoodBoards, boards / 2, b.x, b.y);
    DropInPortions(world, kGoodStones, stones / 2, b.x, b.y);
}

S3RE_HOOK(0x004de430, CGameData_RemoveBuildingGoods, BuildingFn, Original_CGameData_RemoveBuildingGoods,
          CGameData_RemoveBuildingGoods);

// --- The destruction cycle: CrushBuilding, DestroyBuildingAt, ReleaseSiteWorkers
// (RecalculateTerritory is in territory.cpp) ----------------------------------

namespace {

// Callees of the cycle through their S3.EXE addresses (Detours sends them to
// our replacements; tests replace single ones by recording stubs).
constexpr std::uintptr_t kCrushBuilding = 0x004d9250;
constexpr std::uintptr_t kRecalculateTerritory = 0x00519a90;
constexpr std::uintptr_t kReleaseSiteWorkers = 0x0050e3b0;
constexpr std::uintptr_t kRemoveBuildingGoods = 0x004de430;
constexpr std::uintptr_t kClearBuildingFootprint = 0x0051aae0;
constexpr std::uintptr_t kRemoveBuildingFromTiles = 0x004d9940;
constexpr std::uintptr_t kBlockBuildingLine = 0x004dbb90;
constexpr std::uintptr_t kReleaseBuildingExtraTiles = 0x004dda10;
constexpr std::uintptr_t kSpawnEffect = 0x00544f20;        // MapEffects::Spawn_544f20
constexpr std::uintptr_t kRemoveMatchingEffects = 0x00545c80;  // MapEffects::RemoveMatching (package B)

using Effects5Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a1, std::uint32_t a2,
                                              std::uint32_t a3, std::uint32_t a4, std::uint32_t a5);
using Effects4Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a1, std::uint32_t a2,
                                              std::uint32_t a3, std::uint32_t a4);
using CrushFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t building);

// The events CrushBuilding queues (meaning of the numbers not known beyond their arguments).
constexpr std::uint32_t kPendingEventCrushedAt = 3;    // QueuePendingEvent(3, x, y)
constexpr std::uint32_t kPendingEventCrushedType = 6;  // QueuePendingEvent(6, type, owner)
constexpr std::uint32_t kEventBuildingCrushed = 2;     // QueueEvent(2, building, type, owner)

// A garrisoned military building can't be crushed with an enemy soldier this close (offset table distance).
constexpr std::int32_t kEnemyProtectionDistance = 15;
constexpr std::int32_t kFirstSoldierClass = 1;  // classes 1..9 count as soldiers here
constexpr std::int32_t kLastSoldierClass = 9;

constexpr std::int32_t kProtectedMission = 70;  // g_campaignMission
constexpr std::uint32_t kSearchWarning = 9900;  // DestroyBuildingAt logs past this many offsets (0x26ac)

bool IsMilitary(BuildingType type) {
    return IsOneOf(type, {B::SmallTower, B::LargeTower, B::Castle});
}

// Positions of mission 70 whose buildings must not be crushed.
constexpr std::uint32_t kCampaign70Protected[17][2] = {
    {266, 533}, {276, 524}, {293, 512}, {312, 511}, {323, 510}, {342, 515}, {360, 528}, {377, 532}, {404, 555},
    {372, 544}, {365, 586}, {348, 594}, {326, 596}, {305, 591}, {297, 578}, {331, 570}, {429, 542},
};

void CallBuildingFn(CGameData* world, std::uintptr_t address, std::uint32_t building) {
    reinterpret_cast<PileFn>(address)(world, nullptr, building);
}

// Types whose worker in +0x8 is sent away when the finished building goes.
bool HasWorker(BuildingType type) {
    return IsOneOf(type, {B::Woodcutter, B::Stonecutter, B::Sawmill, B::Forester, B::LookoutTower, B::CoalMine,
                          B::IronMine, B::GoldMine, B::GoldSmelter, B::IronSmelter, B::Toolsmith, B::Weaponsmith,
                          B::Vineyard, B::Mill, B::Bakery, B::Slaughterhouse, B::Distillery, B::PigFarm, B::Farm,
                          B::Fishery, B::SulfurMine, B::AmazonSulfurMine, B::Waterworks, B::CatapultWorkshop,
                          B::Hospital, B::RiceFarm, B::GemMine, B::Brewery, B::CharcoalBurner, B::PowderMaker,
                          B::BallistaWorkshop, B::CannonWorkshop, B::DonkeyFarm, B::Shipyard, B::Apiary, B::Meadery,
                          B::Laboratory, B::LargeGong});
}

// Jobs of the construction workers and helpers bound to a building.
bool IsSiteWorker(const layout::Settler& s) {
    // A carrier fetching a shovel or hammer to become a digger or builder (tool_type = the type it becomes).
    const bool digOrBuild = s.tool_type == Value(SettlerType::Digger) || s.tool_type == Value(SettlerType::Builder);
    const bool diggerOrBuilder = TypeOf(s) == SettlerType::Digger || TypeOf(s) == SettlerType::Builder;
    switch (s.job) {
        case Job::CarrierWalkToTool:
        case Job::CarrierTakeTool: return digOrBuild;
        case Job::WalkToWork:
        case Job::ArriveAtWork: return diggerOrBuilder;
        case Job::DiggerDig:
        case Job::DiggerWalkToSpot:
        case Job::DiggerLevel:
        case Job::DiggerRaise:
        case Job::DiggerLower:
        case Job::BuilderStart:
        case Job::BuilderWalkToSpot:
        case Job::BuilderBuild:
        case Job::BuilderWalkToWait: return true;
        default: return false;
    }
}

}  // namespace

std::uint32_t __fastcall CGameData_TileScreenX(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall CGameData_TileScreenY(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall CGameData_LeaveBuildingSlot(CGameData* world, void* edx, std::uint32_t settler);
void __fastcall CGameData_ClearBuildingFlag02(CGameData* world, void* edx, std::uint32_t b);
void __fastcall CGameData_ClearBuildingFlag04(CGameData* world, void* edx, std::uint32_t b);
void __fastcall CGameData_ClearBuildingFlag08(CGameData* world, void* edx, std::uint32_t b);
void __fastcall CGameData_ClearBuildingFlag10(CGameData* world, void* edx, std::uint32_t b);

// Purpose: Sends away everybody working at or for a building that is being
// removed: diggers, builders and tool fetchers of a site, the worker of a
// finished building, and the garrison of a military building, whose
// territory is then recalculated without it. CGameData::ReleaseSiteWorkers (0x50e3b0).
void __fastcall CGameData_ReleaseSiteWorkers(CGameData* world, void*, std::uint32_t number) {
    Building& b = world->buildings[number];
    // Only logged: the occupied digger and builder slots.
    constexpr std::uint32_t kSiteSlots = 8;
    for (std::uint32_t i = 0; i < kSiteSlots; ++i) {
        const std::uint8_t bit = static_cast<std::uint8_t>(1u << i);
        if ((b.digger_slots & bit) != 0) {
            GameDebugLog(texts::kDigger);
        }
        if ((b.builder_slots & bit) != 0) {
            GameDebugLog(texts::kBuilder);
        }
    }
    ResetSettlersWhere(world, [&](const layout::Settler& s) { return s.building == number && IsSiteWorker(s); });
    if (TypeOf(b) == B::Barracks) {
        if (b.buildup != 0) {
            return;
        }
        // Carriers on their way to become recruits.
        ResetSettlersWhere(world, [&](const layout::Settler& s) {
            return s.building == number && s.job == Job::WalkToWork && TypeOf(s) == SettlerType::Carrier;
        });
    }
    if (b.buildup != 0) {
        return;
    }
    if (b.worker != 0 && HasWorker(TypeOf(b))) {
        ResetSettlerToIdle(world, b.worker);
    }
    if (!IsMilitary(TypeOf(b))) {
        return;
    }
    // The garrison leaves (soldiers in job 140 sit in a slot of the building) and the territory is
    // recalculated without it.
    std::uint32_t soldier = b.garrison;
    if (soldier != 0) {
        while (soldier != 0) {
            if (world->settlers[soldier].job == Job::BowmanInTower) {
                CGameData_LeaveBuildingSlot(world, nullptr, soldier);
            }
            ResetSettlerToIdle(world, soldier);
            soldier = world->settlers[soldier].next;
        }
        b.garrison = 0;
        reinterpret_cast<TileFn>(kRecalculateTerritory)(world, nullptr, b.x, b.y);
    }
    // Non-civilians on their way to it.
    ResetSettlersWhere(world, [&](const layout::Settler& s) {
        return ClassOf(s) != SettlerClass::Civilian && s.building == number &&
               IsOneOf(s.job, {Job::WalkToWork, Job::ArriveAtWork, Job::SwordsmanEnterTower,
                               Job::BowmanEnterTower, Job::PikemanEnterTower});
    });
}

namespace {

// Part of CrushBuilding: a garrisoned military building is protected while an enemy soldier stands within the
// protection distance; the original checks its "protected" flag at the top of every step.
bool EnemySoldierNear(CGameData* world, const Building& b, std::uint32_t protectedBuilding) {
    const layout::MapOffset* entry = world->offsets;
    do {
        if (protectedBuilding != 0) {
            return true;
        }
        const std::uint32_t tx = b.x + static_cast<std::uint32_t>(entry->dx);
        const std::uint32_t ty = b.y + static_cast<std::uint32_t>(entry->dy);
        if (CGameData_IsOnMap(world, nullptr, tx, ty) != 0) {
            const std::uint32_t settler = TileAt(world, tx, ty).settler;
            if (settler != 0) {
                const layout::Settler& s = world->settlers[settler];
                const std::int32_t theirs = (world->players + s.owner)->team;
                const std::int32_t ours = (world->players + b.owner)->team;
                const std::int32_t settlerClass = g_settlerTypeClass[s.art];
                if (theirs != ours && settlerClass >= kFirstSoldierClass && settlerClass <= kLastSoldierClass) {
                    protectedBuilding = 1;
                }
            }
        }
        ++entry;
    } while (entry->distance < kEnemyProtectionDistance);
    return protectedBuilding != 0;
}

}  // namespace

// Purpose: Destroys a building (or construction site), unless it is a
// protected one: in mission 70 the listed positions, and a garrisoned
// military building with an enemy soldier within 14 steps. Logs the stages,
// queues the events for the UI and statistics, shows the destruction effect,
// and removes footprint, workers, goods and the building record. EAX = 1 if
// it was destroyed, 0 if protected. CGameData::CrushBuilding (0x4d9250).
std::uint32_t __fastcall CGameData_CrushBuilding(CGameData* world, void*, std::uint32_t number) {
    Building& b = world->buildings[number];
    GameDebugLog(texts::kStartCrushing, number, static_cast<std::uint32_t>(b.art));
    std::uint32_t protectedBuilding = 0;
    if (b.owner == kFree) {
        // The original leaves its "protected" flag uninitialized on this path
        // (EAX = whether that stack slot was 0); 1 is returned here, as for a
        // zeroed stack (known difference, see the inventory).
        GameDebugLog(texts::kEndOfCrushing);
        return 1;
    }
    if (g_campaignMission == kProtectedMission) {
        for (const auto& position : kCampaign70Protected) {
            if (b.x == position[0] && b.y == position[1]) {
                GameDebugLog(texts::kCampaign70NoCrush);
                protectedBuilding = 1;
            }
        }
    }
    if (b.buildup == 0 && IsMilitary(TypeOf(b)) && b.garrison != 0 &&
        world->offsets[0].distance < kEnemyProtectionDistance) {
        if (EnemySoldierNear(world, b, protectedBuilding)) {
            GameDebugLog(texts::kEndOfCrushing);
            return 0;
        }
    }
    if (protectedBuilding != 0) {
        GameDebugLog(texts::kEndOfCrushing);
        return 0;
    }
    if (b.buildup != 0) {
        CGameData_ClearBuildingFlag02(world, nullptr, number);
        CGameData_ClearBuildingFlag04(world, nullptr, number);
        CGameData_ClearBuildingFlag10(world, nullptr, number);
        CGameData_ClearBuildingFlag08(world, nullptr, number);
    }
    if (b.buildup == 0 && IsMilitary(TypeOf(b))) {
        // The guard settler (BuildingDone) disappears with the building.
        layout::Settler& guard = world->settlers[b.guard];
        guard.owner = kFree;
        TileAt(world, guard.x, guard.y).settler = 0;
    }
    CGameData_QueuePendingEvent(world, nullptr, kPendingEventCrushedAt, b.x, b.y);
    CGameData_QueuePendingEvent(world, nullptr, kPendingEventCrushedType, b.art, b.owner);
    CGameData_QueueEvent(world, nullptr, kEventBuildingCrushed, number, b.art, b.owner);
    void* effects = &world->map_effects;
    {
        // The destruction effect; the screen y is computed before the screen x, as in the original.
        const std::uint32_t x = b.x;
        const std::uint32_t y = b.y;
        const std::uint32_t sy = CGameData_TileScreenY(world, nullptr, x, y);
        const std::uint32_t sx = CGameData_TileScreenX(world, nullptr, x, y);
        reinterpret_cast<Effects5Fn>(kSpawnEffect)(effects, nullptr, g_gameTick, sx, sy, x, y);
    }
    // The war machine workshops, the shipyard and the gong give back their extra tiles (state nibble 0x20
    // and up, extra_tiles set).
    if (IsOneOf(TypeOf(b), {B::Shipyard, B::CatapultWorkshop, B::BallistaWorkshop, B::CannonWorkshop, B::LargeGong}) &&
        b.buildup == 0) {
        const std::uint8_t state = static_cast<std::uint8_t>(b.workshop.state & 0xf0);
        if (state != 0 && state != 0x10 && b.workshop.extra_tiles != 0xff) {
            CallBuildingFn(world, kReleaseBuildingExtraTiles, number);
        }
    }
    if (IsOneOf(TypeOf(b), {B::Shipyard, B::Harbour}) && b.dock_direction != kNoDirection) {
        CallBuildingFn(world, kBlockBuildingLine, number);
    }
    {
        const std::uint32_t sy = CGameData_TileScreenY(world, nullptr, b.x, b.y);
        const std::uint32_t sx = CGameData_TileScreenX(world, nullptr, b.x, b.y);
        reinterpret_cast<Effects4Fn>(kRemoveMatchingEffects)(effects, nullptr, 0, number, sx, sy);
    }
    GameDebugLog(texts::kCrush1);
    CallBuildingFn(world, kClearBuildingFootprint, number);
    GameDebugLog(texts::kCrush2);
    CallBuildingFn(world, kReleaseSiteWorkers, number);
    GameDebugLog(texts::kCrush3);
    CallBuildingFn(world, kRemoveBuildingGoods, number);
    GameDebugLog(texts::kCrush4);
    CallBuildingFn(world, kRemoveBuildingFromTiles, number);
    GameDebugLog(texts::kCrush5);
    GameDebugLog(texts::kEndOfCrushing);
    return 1;
}

// Purpose: Destroys the building standing on tile (x, y): searches outwards
// over the offset table for a building whose layout covers the tile and
// crushes it. Logs every step past 9900 entries and keeps searching.
// CGameData::DestroyBuildingAt (0x4d97b0).
std::uint32_t __fastcall CGameData_DestroyBuildingAt(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    std::uint32_t found = 0;
    std::uint32_t number = y;  // the original's register starts with y (never used unless found)
    for (std::uint32_t k = 0; found == 0; ++k) {
        const layout::MapOffset& entry = *(world->offsets + k);
        const std::uint32_t tx = x + static_cast<std::uint32_t>(entry.dx);
        const std::uint32_t ty = y + static_cast<std::uint32_t>(entry.dy);
        if (CGameData_IsOnMap(world, nullptr, tx, ty) != 0 &&
            (TileAt(world, tx, ty).flags & layout::kTileHasBuilding) != 0) {
            number = TileAt(world, tx, ty).object_number;
            const Building& b = world->buildings[number];
            // Whether (x, y) is one of its footprint or border tiles (the whole layout is scanned).
            const std::uint32_t* p = Layout(b.race, b.art);
            while (*p++ != kMarkFootprint) {
            }
            for (; *p != kMarkFootprintEnd; p += 2) {
                if (p[0] + tx == x && p[1] + ty == y) {
                    found = 1;
                }
            }
            ++p;
            while (*p != kMarkEnd) {
                if (*p == kMarkBorder) {
                    ++p;
                }
                if (p[0] + tx == x && p[1] + ty == y) {
                    found = 1;
                }
                p += 2;
            }
        }
        if (k + 1 >= kSearchWarning) {
            GameDebugLog(texts::kBuildingNotInRange, x, y);
        }
    }
    return reinterpret_cast<CrushFn>(kCrushBuilding)(world, nullptr, number);
}

S3RE_HOOK(0x0050e3b0, CGameData_ReleaseSiteWorkers, BuildingFn, Original_CGameData_ReleaseSiteWorkers,
          CGameData_ReleaseSiteWorkers);
S3RE_HOOK(0x004d9250, CGameData_CrushBuilding, CrushFn, Original_CGameData_CrushBuilding, CGameData_CrushBuilding);
S3RE_HOOK(0x004d97b0, CGameData_DestroyBuildingAt, TileFn, Original_CGameData_DestroyBuildingAt,
          CGameData_DestroyBuildingAt);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kTestWorld = offsetof(CGameData, buildings) + 64 * sizeof(Building);

// A building at (20..300, 20..300) of a random type/race with a layout, its
// footprint tiles marked (flags, stakes, blocked bits as UnblockTile needs
// them random anyway), sometimes a levelling site with a random grid.
void CompareFootprint(test::Context& ctx, BuildingFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    // UnblockTile (verified, own tests) floods regions over the random map;
    // here it only clears the blocked bit, and its calls are compared.
    test::CalleeStubs stubs;
    stubs.Install<2, kUnblockTile>([](CGameData* w, std::uint32_t, const std::uint32_t* args) {
        TileAt(w, args[0], args[1]).flags &= 0xfe;
        return 0u;
    });
    worlds.Both([&](CGameData* w) { w->map_size = 384; });
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t number = 1 + ctx.rng().Below(60);
        const std::uint32_t x = 20 + ctx.rng().Below(280);
        const std::uint32_t y = 20 + ctx.rng().Below(280);
        const auto type = static_cast<std::uint8_t>(1 + ctx.rng().Below(56));
        const auto race = static_cast<std::uint8_t>(ctx.rng().Below(4));
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Below(20));
        const auto slot = static_cast<std::uint16_t>(ctx.rng().Below(30));
        const auto buildup = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 1 : ctx.rng().Below(4));
        worlds.Both([&](CGameData* w) {
            Building& b = w->buildings[number];
            b.x = static_cast<std::uint16_t>(x);
            b.y = static_cast<std::uint16_t>(y);
            b.art = type;
            b.race = race;
            b.owner = owner;
            b.levelling_slot = slot;
            b.buildup = buildup;
        });
        for (std::uint32_t dx = 0; dx < 31; ++dx) {
            for (std::uint32_t dy = 0; dy < 31; ++dy) {
                const auto flags = static_cast<std::uint8_t>(ctx.rng().Next32());
                const auto object = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? kObjectStake : ctx.rng().Below(256));
                const bool cell = ctx.rng().Chance(1, 2);
                worlds.Both([&](CGameData* w) {
                    Tile& t = w->tiles[x - 15 + dx][y - 15 + dy];
                    t.flags = flags;
                    t.object = object;
                    w->site_cache[owner][slot].grid[dx * 31 + dy] = cell ? 1 : 0;
                });
            }
        }
        stubs.SetWorld(worlds.original());
        Original_CGameData_ClearBuildingFootprint(worlds.original(), nullptr, number);
        const std::vector<test::StubCall> expected = stubs.Take();
        stubs.SetWorld(worlds.ours());
        candidate(worlds.ours(), nullptr, number);
        const std::vector<test::StubCall> actual = stubs.Take();
        if (!S3RE_EXPECT(expected == actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: building %u type %u race %u buildup %u", i, number, type, race,
                     buildup);
            return;
        }
    }
}

// Wrong: forgets the levelling grid and the site cache slot.
void __fastcall WrongClearBuildingFootprint(CGameData* world, void* edx, std::uint32_t number) {
    Building& building = world->buildings[number];
    const std::uint8_t buildup = building.buildup;
    building.buildup = 0;  // skip the grid part: the site cache stays valid
    CGameData_ClearBuildingFootprint(world, edx, number);
    building.buildup = buildup;
}

}  // namespace

S3RE_TEST(CGameData_ClearBuildingFootprint_MatchesOriginal) {
    CompareFootprint(ctx, &CGameData_ClearBuildingFootprint, 600);
}
S3RE_TEST(CGameData_ClearBuildingFootprint_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareFootprint(probe, &WrongClearBuildingFootprint, 100);
    S3RE_EXPECT(probe.failures() > 0);
}


// --- BlockTileAndMoveSettler, BlockBuildingLine, Drop*Pile ------------------

namespace {

// Recording stubs: ResetSettlerToIdle (job becomes 1, so a scan doesn't find
// the settler twice), TakeGoodFromPile (amount and leaving - 1, as the
// original does for a pile that keeps goods), BlockTile (sets the blocked
// bit only; the real one floods regions over the random map).
void InstallSettlerStubs(test::CalleeStubs& stubs) {
    stubs.Install<1, kResetSettlerToIdle>([](CGameData* w, std::uint32_t, const std::uint32_t* args) {
        w->settlers[args[0] & 0xffff].job = Job::Nothing;
        return 0x5a5a0001u;
    });
    stubs.Install<1, kTakeGoodFromPile>([](CGameData* w, std::uint32_t, const std::uint32_t* args) {
        layout::Pile& pile = w->piles[args[0] & 0xffff];
        --pile.amount;
        --pile.leaving;
        return 0u;
    });
    stubs.Install<2, kBlockTile>([](CGameData* w, std::uint32_t, const std::uint32_t* args) {
        TileAt(w, args[0], args[1]).flags |= 1;
        return 0u;
    });
}

constexpr std::size_t kSettlerWorld = offsetof(CGameData, buildings) + 64 * sizeof(Building);
constexpr std::uint32_t kSettlers = 150;

// The parts of the world every case needs sane: map size, small offset
// table (PlaceSettlerNear), small record counts (PileIncAmountLeaving scans
// the buildings), some free pile records (CreatePile).
void SetUpWorld(test::Context& ctx, test::WorldPair& worlds) {
    worlds.Both([&](CGameData* w) {
        w->map_size = 384;
        w->building_high_water = 50;
    });
    for (std::uint32_t k = 0; k < 1000; ++k) {
        const std::int32_t dx = ctx.rng().Range(-4, 4);
        const std::int32_t dy = ctx.rng().Range(-4, 4);
        worlds.Both([&](CGameData* w) {
            w->offsets[k].dx = dx;
            w->offsets[k].dy = dy;
        });
    }
}

// Settlers 1..150 with jobs and positions around the pile or tile.
void SetUpSettlers(test::Context& ctx, test::WorldPair& worlds, std::uint32_t pile, std::uint16_t px,
                   std::uint16_t py) {
    const std::uint32_t count = ctx.rng().Below(kSettlers);
    worlds.Both([&](CGameData* w) { w->settler_high_water = count; });
    static constexpr std::uint16_t kJobs[] = {0x0a, 0x0b, 0xa9, 0x4c, 0x4d, 0x4e, 0x4f, 0x68, 0x69, 0x133, 0x134, 0x1};
    for (std::uint32_t n = 1; n <= kSettlers; ++n) {
        const auto job = ctx.rng().Chance(4, 5) ? kJobs[ctx.rng().Below(12)] : static_cast<std::uint16_t>(ctx.rng().Below(0x195));
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Chance(1, 6) ? kFree : ctx.rng().Below(8));
        const auto tx = static_cast<std::uint16_t>(ctx.rng().Chance(2, 3) ? px : ctx.rng().Below(384));
        const auto ty = static_cast<std::uint16_t>(ctx.rng().Chance(2, 3) ? py : ctx.rng().Below(384));
        const auto building = static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? pile : ctx.rng().Below(100));
        worlds.Both([&](CGameData* w) {
            layout::Settler& s = w->settlers[n];
            s.job = static_cast<SettlerJob>(job);
            s.owner = owner;
            s.target_x = tx;
            s.target_y = ty;
            s.building = building;
        });
    }
}

std::uint32_t SetUpPile(test::Context& ctx, test::WorldPair& worlds) {
    const std::uint32_t number = 1 + ctx.rng().Below(100);
    const auto x = static_cast<std::uint16_t>(10 + ctx.rng().Below(360));
    const auto y = static_cast<std::uint16_t>(10 + ctx.rng().Below(360));
    static constexpr std::uint8_t kKinds[] = {0, 0xfe, 0xfd, 0xff, 1};
    const std::uint8_t art = ctx.rng().Chance(4, 5) ? kKinds[ctx.rng().Below(5)] : static_cast<std::uint8_t>(ctx.rng().Below(256));
    const auto good = static_cast<std::uint8_t>(ctx.rng().Chance(1, 5) ? 0xff : 1 + ctx.rng().Below(35));
    const auto amount = static_cast<std::uint8_t>(ctx.rng().Chance(1, 4) ? 0 : ctx.rng().Below(9));
    const auto leaving = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Below(256));
    const auto coming = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Below(9));
    const auto prev = static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? 0 : 101 + ctx.rng().Below(50));
    const auto next = static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? 0 : 101 + ctx.rng().Below(50));
    const auto area = static_cast<std::uint16_t>(1 + ctx.rng().Below(50));
    worlds.Both([&](CGameData* w) {
        layout::Pile& p = w->piles[number];
        p.x = x;
        p.y = y;
        p.art = art;
        p.good = good;
        p.amount = amount;
        p.leaving = leaving;
        p.coming = coming;
        p.prev = prev;
        p.next = next;
        w->tiles[x][y].object_number = static_cast<std::uint16_t>(number);
        w->tiles[x][y].area = area;
        w->pile_high_water = 160;
    });
    // A few free records for CreatePile.
    for (std::uint32_t k = 0; k < 3; ++k) {
        const std::uint32_t free = 1 + ctx.rng().Below(160);
        if (free != number) {
            worlds.Both([&](CGameData* w) { w->piles[free].good = 0; });
        }
    }
    SetUpSettlers(ctx, worlds, number, x, y);
    return number;
}

// Runs one case of both versions; compares stub calls, DebugLogs and (every
// `every` cases) the whole world.
template <typename Fn, typename... Args>
bool RunCase(test::Context& ctx, test::WorldPair& worlds, test::CalleeStubs& stubs, DebugLogRecorder& logs,
             Fn original, Fn candidate, bool checkpoint, Args... args) {
    stubs.SetWorld(worlds.original());
    original(worlds.original(), nullptr, args...);
    const std::vector<test::StubCall> expectedCalls = stubs.Take();
    const std::vector<DebugLogCall> expectedLogs = logs.Take();
    stubs.SetWorld(worlds.ours());
    candidate(worlds.ours(), nullptr, args...);
    const std::vector<test::StubCall> actualCalls = stubs.Take();
    const std::vector<DebugLogCall> actualLogs = logs.Take();
    return S3RE_EXPECT(expectedCalls == actualCalls) && S3RE_EXPECT(expectedLogs == actualLogs) &&
           (!checkpoint || worlds.Checkpoint(ctx));
}

void CompareDropPile(test::Context& ctx, DropPileFn original, DropPileFn candidate, int cases) {
    test::WorldPair worlds(ctx, kSettlerWorld);
    test::CalleeStubs stubs;
    InstallSettlerStubs(stubs);
    DebugLogRecorder logs;
    SetUpWorld(ctx, worlds);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t pile = SetUpPile(ctx, worlds);
        if (!RunCase(ctx, worlds, stubs, logs, original, candidate, i % 32 == 31 || i == cases - 1, pile)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 31 before it): pile %u", i, pile);
            return;
        }
    }
}

void CompareBlockTile(test::Context& ctx, PositionFn candidate, int cases) {
    test::WorldPair worlds(ctx, kSettlerWorld);
    test::CalleeStubs stubs;
    InstallSettlerStubs(stubs);
    DebugLogRecorder logs;
    SetUpWorld(ctx, worlds);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t x = 20 + ctx.rng().Below(340);
        const std::uint32_t y = 20 + ctx.rng().Below(340);
        const auto settler = static_cast<std::uint16_t>(ctx.rng().Chance(1, 3) ? 0 : 1 + ctx.rng().Below(kSettlers));
        worlds.Both([&](CGameData* w) { w->tiles[x][y].settler = settler; });
        // Neighbourhood for PlaceSettlerNear: some free, walkable tiles.
        for (std::int32_t dx = -4; dx <= 4; ++dx) {
            for (std::int32_t dy = -4; dy <= 4; ++dy) {
                const auto other = static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? 0 : 1 + ctx.rng().Below(kSettlers));
                const auto walk = static_cast<std::uint8_t>(ctx.rng().Chance(1, 4) ? 0 : 1 + ctx.rng().Below(200));
                if (dx == 0 && dy == 0) {
                    continue;
                }
                worlds.Both([&](CGameData* w) {
                    w->tiles[x + dx][y + dy].settler = other;
                    w->tiles[x + dx][y + dy].walk_region = walk;
                });
            }
        }
        SetUpSettlers(ctx, worlds, 0, static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y));
        if (!RunCase(ctx, worlds, stubs, logs, Original_CGameData_BlockTileAndMoveSettler, candidate,
                     i % 32 == 31 || i == cases - 1, x, y)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 31 before it): (%u, %u)", i, x, y);
            return;
        }
    }
}

void CompareBlockLine(test::Context& ctx, BuildingFn candidate, int cases) {
    test::WorldPair worlds(ctx, kSettlerWorld);
    test::CalleeStubs stubs;
    InstallSettlerStubs(stubs);
    DebugLogRecorder logs;
    SetUpWorld(ctx, worlds);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t number = 1 + ctx.rng().Below(50);
        const auto buildup = static_cast<std::uint8_t>(ctx.rng().Chance(4, 5) ? 0 : ctx.rng().Below(256));
        const auto direction = static_cast<std::uint16_t>(ctx.rng().Chance(1, 8) ? kNoDirection : ctx.rng().Below(6));
        const auto wx = static_cast<std::uint16_t>(30 + ctx.rng().Below(320));
        const auto wy = static_cast<std::uint16_t>(30 + ctx.rng().Below(320));
        worlds.Both([&](CGameData* w) {
            Building& b = w->buildings[number];
            b.buildup = buildup;
            b.dock_direction = direction;
            b.work_x = wx;
            b.work_y = wy;
        });
        if (direction != kNoDirection) {
            const std::uint32_t length = ctx.rng().Below(10);
            std::uint32_t x = wx;
            std::uint32_t y = wy;
            for (std::uint32_t k = 0; k <= length; ++k) {
                const std::uint8_t terrain = k == length ? kLineEndTerrain
                                                          : static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0x10 : ctx.rng().Below(0x30));
                const auto settler = static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? 0 : 1 + ctx.rng().Below(kSettlers));
                worlds.Both([&](CGameData* w) {
                    TileAt(w, x, y).terrain = terrain;
                    TileAt(w, x, y).settler = settler;
                });
                x = NeighbourX(x, direction);
                y = NeighbourY(y, direction);
            }
        }
        SetUpSettlers(ctx, worlds, 0, wx, wy);
        if (!RunCase(ctx, worlds, stubs, logs, Original_CGameData_BlockBuildingLine, candidate,
                     i % 32 == 31 || i == cases - 1, number)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 31 before it): building %u", i, number);
            return;
        }
    }
}

// Wrong: leaves the settler word on the tile.
void __fastcall WrongBlockTileAndMoveSettler(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y) {
    const std::uint16_t settler = TileAt(world, x, y).settler;
    CGameData_BlockTileAndMoveSettler(world, edx, x, y);
    if (TileAt(world, x, y).settler == 0) {
        TileAt(world, x, y).settler = settler;
    }
}

// Wrong: keeps the direction.
void __fastcall WrongBlockBuildingLine(CGameData* world, void* edx, std::uint32_t number) {
    const std::uint16_t direction = world->buildings[number].dock_direction;
    CGameData_BlockBuildingLine(world, edx, number);
    world->buildings[number].dock_direction = direction;
}

// Wrong: no "TROUBLE" log when nobody was coming.
void __fastcall WrongDropInputPile(CGameData* world, void* edx, std::uint32_t number) {
    if ((world->piles[number].leaving >> 4) == 0) {
        world->piles[number].leaving = static_cast<std::uint8_t>(world->piles[number].leaving | 0x10);
        CGameData_DropInputPile(world, edx, number);
        return;
    }
    CGameData_DropInputPile(world, edx, number);
}

// Wrong: the dropped pile gets the amount left after taking the goods.
void __fastcall WrongDropStoragePile(CGameData* world, void* edx, std::uint32_t number) {
    layout::Pile& pile = world->piles[number];
    if (pile.amount > 1) {
        --pile.amount;
    }
    CGameData_DropStoragePile(world, edx, number);
}

// Wrong: fetchers with job 0x68 keep going.
void __fastcall WrongDropOutputPile(CGameData* world, void* edx, std::uint32_t number) {
    std::vector<std::uint32_t> changed;
    for (std::uint32_t n = 1; n <= world->settler_high_water && n <= kSettlers; ++n) {
        if (world->settlers[n].job == Job::CarrierWalkToTool) {
            world->settlers[n].job = Job::WalkToWork;
            changed.push_back(n);
        }
    }
    CGameData_DropOutputPile(world, edx, number);
    for (const std::uint32_t n : changed) {
        if (world->settlers[n].job == Job::WalkToWork) {
            world->settlers[n].job = Job::CarrierWalkToTool;
        }
    }
}

}  // namespace

S3RE_TEST(CGameData_BlockTileAndMoveSettler_MatchesOriginal) {
    CompareBlockTile(ctx, &CGameData_BlockTileAndMoveSettler, 2000);
}
S3RE_TEST(CGameData_BlockTileAndMoveSettler_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareBlockTile(probe, &WrongBlockTileAndMoveSettler, 500);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_BlockBuildingLine_MatchesOriginal) {
    CompareBlockLine(ctx, &CGameData_BlockBuildingLine, 2000);
}
S3RE_TEST(CGameData_BlockBuildingLine_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareBlockLine(probe, &WrongBlockBuildingLine, 500);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_DropInputPile_MatchesOriginal) {
    CompareDropPile(ctx, Original_CGameData_DropInputPile, &CGameData_DropInputPile, 2000);
}
S3RE_TEST(CGameData_DropInputPile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDropPile(probe, Original_CGameData_DropInputPile, &WrongDropInputPile, 500);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_DropStoragePile_MatchesOriginal) {
    CompareDropPile(ctx, Original_CGameData_DropStoragePile, &CGameData_DropStoragePile, 2000);
}
S3RE_TEST(CGameData_DropStoragePile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDropPile(probe, Original_CGameData_DropStoragePile, &WrongDropStoragePile, 500);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_DropOutputPile_MatchesOriginal) {
    CompareDropPile(ctx, Original_CGameData_DropOutputPile, &CGameData_DropOutputPile, 2000);
}
S3RE_TEST(CGameData_DropOutputPile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDropPile(probe, Original_CGameData_DropOutputPile, &WrongDropOutputPile, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

// --- RemoveBuildingGoods ----------------------------------------------------

namespace {

void CompareRemoveGoods(test::Context& ctx, BuildingFn candidate, int cases) {
    test::WorldPair worlds(ctx, kSettlerWorld);
    test::CalleeStubs stubs;
    InstallSettlerStubs(stubs);
    stubs.Install<4, kDropGoodNear>();
    stubs.Install<1, kDropInputPile>();
    stubs.Install<1, kDropStoragePile>();
    stubs.Install<1, kDropOutputPile>();
    DebugLogRecorder logs;
    SetUpWorld(ctx, worlds);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t number = 1 + ctx.rng().Below(50);
        static constexpr std::uint8_t kTypes[] = {1, 0xc, 0xd, 0x35, 0x21, 0x22, 2, 3, 0x1d, 0x38, 0x34, 0xf, 0x1a};
        const std::uint8_t type = ctx.rng().Chance(2, 3) ? kTypes[ctx.rng().Below(sizeof(kTypes))]
                                                        : static_cast<std::uint8_t>(1 + ctx.rng().Below(0x38));
        const auto race = static_cast<std::uint8_t>(ctx.rng().Below(4));
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Below(4));
        const auto buildup = static_cast<std::uint8_t>(ctx.rng().Chance(3, 5) ? 0 : 1 + ctx.rng().Below(255));
        const auto x = static_cast<std::uint16_t>(30 + ctx.rng().Below(320));
        const auto y = static_cast<std::uint16_t>(30 + ctx.rng().Below(320));
        // A site misses at most its materials (more would make the original
        // drop about 2^31 units in portions of 8).
        const std::uint32_t materials = Layout(race, type)[1];
        const auto missing1 = static_cast<std::uint8_t>(ctx.rng().Below(((materials >> 8) & 0xff) + 1));
        const auto missing2 = static_cast<std::uint8_t>(ctx.rng().Below((materials & 0xff) + 1));
        const auto ring = static_cast<std::uint16_t>(ctx.rng().Next32());
        std::uint16_t piles[3];
        for (std::uint16_t& p : piles) {
            p = static_cast<std::uint16_t>(ctx.rng().Chance(1, 4) ? 0 : 1 + ctx.rng().Below(150));
        }
        worlds.Both([&](CGameData* w) {
            Building& b = w->buildings[number];
            b.art = type;
            b.race = race;
            b.owner = owner;
            b.buildup = buildup;
            b.x = x;
            b.y = y;
            b.door_x = static_cast<std::uint16_t>(x + 1);
            b.door_y = static_cast<std::uint16_t>(y + 2);
            b.site.boards_missing = missing1;
            b.site.stones_missing = missing2;
            b.pile_ring = ring;
            for (int k = 0; k < 3; ++k) {
                b.piles[k] = piles[k];
            }
            // The door's area: no goods request lists (RemoveBuildingFromAreaRequests
            // walks them; random links would loop).
            const auto area = static_cast<std::uint16_t>(1 + (x % 50));
            w->tiles[x + 1][y + 2].area = area;
            for (std::uint16_t& head : w->areas[area].requests_by_goods) {
                head = 0;
            }
        });
        for (const std::uint16_t p : piles) {
            const auto px = static_cast<std::uint16_t>(20 + ctx.rng().Below(340));
            const auto py = static_cast<std::uint16_t>(20 + ctx.rng().Below(340));
            const auto amount = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Below(9));
            const auto coming = static_cast<std::uint8_t>(ctx.rng().Below(9));
            const auto good = static_cast<std::uint8_t>(1 + ctx.rng().Below(35));
            worlds.Both([&](CGameData* w) {
                layout::Pile& pile = w->piles[p];
                pile.x = px;
                pile.y = py;
                pile.amount = amount;
                pile.coming = coming;
                pile.good = good;
            });
        }
        SetUpSettlers(ctx, worlds, number, x, y);
        // Jobs of settlers busy with the building's goods.
        for (std::uint32_t n = 1; n <= kSettlers; ++n) {
            static constexpr std::uint16_t kJobs[] = {0x0a, 0x0b, 0xa9, 0xaa, 0x0c, 0x0d, 0x50, 0x4c, 0x1};
            if (ctx.rng().Chance(1, 2)) {
                const std::uint16_t job = kJobs[ctx.rng().Below(9)];
                worlds.Both([&](CGameData* w) { w->settlers[n].job = static_cast<SettlerJob>(job); });
            }
        }
        if (!RunCase(ctx, worlds, stubs, logs, Original_CGameData_RemoveBuildingGoods, candidate,
                     i % 32 == 31 || i == cases - 1, number)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 31 before it): building %u type 0x%x buildup %u", i,
                     number, type, buildup);
            return;
        }
    }
}

// Wrong: returns all construction materials, not half.
void __fastcall WrongRemoveBuildingGoods(CGameData* world, void* edx, std::uint32_t number) {
    CGameData_RemoveBuildingGoods(world, edx, number);
    Building& b = world->buildings[number];
    const std::uint32_t materials = Layout(b.race, b.art)[1];
    DropGoodNear(world, kGoodBoards, 1 + ((materials >> 8) & 0xff), b.x, b.y);
}

}  // namespace

S3RE_TEST(CGameData_RemoveBuildingGoods_MatchesOriginal) {
    CompareRemoveGoods(ctx, &CGameData_RemoveBuildingGoods, 3000);
}
S3RE_TEST(CGameData_RemoveBuildingGoods_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareRemoveGoods(probe, &WrongRemoveBuildingGoods, 300);
    S3RE_EXPECT(probe.failures() > 0);
}

// --- ReleaseSiteWorkers, CrushBuilding, DestroyBuildingAt ----------------------

namespace {

// The offset table as the game builds it: rings of growing hex distance
// around (0, 0), `entries` long (distance in +8).
void SetRingOffsets(test::WorldPair& worlds, std::uint32_t entries) {
    std::vector<layout::MapOffset> table;
    for (std::int32_t ring = 0; table.size() < entries; ++ring) {
        for (std::int32_t dx = -ring; dx <= ring; ++dx) {
            for (std::int32_t dy = -ring; dy <= ring; ++dy) {
                const bool same = (dx >= 0) == (dy >= 0);
                const std::int32_t ax = dx < 0 ? -dx : dx;
                const std::int32_t ay = dy < 0 ? -dy : dy;
                const std::int32_t d = same ? (ax > ay ? ax : ay) : ax + ay;
                if (d == ring) {
                    table.push_back({dx, dy, ring, 0});
                }
            }
        }
    }
    worlds.Both([&](CGameData* w) {
        for (std::uint32_t k = 0; k < entries; ++k) {
            w->offsets[k] = table[k];
        }
    });
}

// Settlers 1..count: jobs of site and garrison work, buildings and types
// often the given building, a well-formed `next` chain of up to 4 soldiers
// starting at the returned settler.
std::uint16_t SetUpWorkers(test::Context& ctx, test::WorldPair& worlds, std::uint32_t building) {
    const std::uint32_t count = 40 + ctx.rng().Below(60);
    worlds.Both([&](CGameData* w) { w->settler_high_water = count; });
    static constexpr std::uint16_t kJobs[] = {0x68, 0x69, 3, 4, 5, 6, 7, 8, 9, 0xe, 0xf, 0x11, 0x12, 0x10, 0x6a,
                                              0x8b, 0x8c, 1, 0x37};
    for (std::uint32_t n = 1; n <= count; ++n) {
        const std::uint16_t job = kJobs[ctx.rng().Below(sizeof(kJobs) / 2)];
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Chance(1, 8) ? kFree : ctx.rng().Below(4));
        const auto art = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? ctx.rng().Below(3) : ctx.rng().Below(0x30));
        const auto task = static_cast<std::uint8_t>(ctx.rng().Below(4));
        const auto b = static_cast<std::uint16_t>(ctx.rng().Chance(2, 3) ? building : ctx.rng().Below(60));
        const auto flags = static_cast<std::uint8_t>(ctx.rng().Below(256) & ~0x40u);
        const auto sx = static_cast<std::uint16_t>(30 + ctx.rng().Below(320));
        const auto sy = static_cast<std::uint16_t>(30 + ctx.rng().Below(320));
        worlds.Both([&](CGameData* w) {
            layout::Settler& s = w->settlers[n];
            s.job = static_cast<SettlerJob>(job);
            s.owner = owner;
            s.art = art;
            s.tool_type = task;
            s.building = b;
            s.flags = flags;
            s.x = sx;
            s.y = sy;
            s.next = 0;
        });
    }
    const std::uint32_t soldiers = ctx.rng().Below(5);
    std::uint16_t head = 0;
    for (std::uint32_t k = 0; k < soldiers; ++k) {
        const auto n = static_cast<std::uint16_t>(1 + ctx.rng().Below(count));
        bool used = false;
        for (std::uint16_t m = head; m != 0; m = worlds.original()->settlers[m].next) {
            used = used || m == n;
        }
        if (used) {
            continue;
        }
        const std::uint16_t next = head;
        // Soldiers of the building, some still in a slot (flag 0x40).
        const auto flags = static_cast<std::uint8_t>(ctx.rng().Below(256));
        worlds.Both([&](CGameData* w) {
            w->settlers[n].next = next;
            w->settlers[n].building = static_cast<std::uint16_t>(building);
            w->settlers[n].flags = flags;
        });
        head = n;
    }
    return head;
}

void SetUpBuilding(test::Context& ctx, test::WorldPair& worlds, std::uint32_t number, std::uint8_t type,
                   std::uint16_t x, std::uint16_t y) {
    const auto owner = static_cast<std::uint8_t>(ctx.rng().Below(4));
    const auto race = static_cast<std::uint8_t>(ctx.rng().Below(4));
    const auto buildup = static_cast<std::uint8_t>(ctx.rng().Chance(3, 5) ? 0 : 1 + ctx.rng().Below(10));
    const auto bits16 = static_cast<std::uint8_t>(ctx.rng().Below(256));
    const auto bits17 = static_cast<std::uint8_t>(ctx.rng().Below(256));
    const auto state = static_cast<std::uint8_t>(ctx.rng().Below(256));
    const auto extra = static_cast<std::uint8_t>(ctx.rng().Chance(1, 4) ? 0xff : ctx.rng().Below(256));
    const auto line = static_cast<std::uint16_t>(ctx.rng().Chance(1, 3) ? 0xffff : ctx.rng().Below(100));
    worlds.Both([&](CGameData* w) {
        Building& b = w->buildings[number];
        b.art = type;
        b.owner = owner;
        b.race = race;
        b.buildup = buildup;
        b.x = x;
        b.y = y;
        b.door_x = static_cast<std::uint16_t>(x + 1);
        b.door_y = static_cast<std::uint16_t>(y + 1);
        b.digger_slots = bits16;
        b.builder_slots = bits17;
        b.workshop.state = state;
        b.workshop.extra_tiles = extra;
        b.dock_direction = line;
    });
}

template <typename Fn, typename... Args>
bool RunCycleCase(test::Context& ctx, test::WorldPair& worlds, test::CalleeStubs& stubs, DebugLogRecorder& logs,
                  Fn original, Fn candidate, bool compareEax, bool checkpoint, Args... args) {
    stubs.SetWorld(worlds.original());
    const std::uint32_t expected = original(worlds.original(), nullptr, args...);
    const std::vector<test::StubCall> expectedCalls = stubs.Take();
    const std::vector<DebugLogCall> expectedLogs = logs.Take();
    stubs.SetWorld(worlds.ours());
    const std::uint32_t actual = candidate(worlds.ours(), nullptr, args...);
    const std::vector<test::StubCall> actualCalls = stubs.Take();
    const std::vector<DebugLogCall> actualLogs = logs.Take();
    return (!compareEax || S3RE_EXPECT_EQ(expected, actual)) && S3RE_EXPECT(expectedCalls == actualCalls) &&
           S3RE_EXPECT(expectedLogs == actualLogs) && (!checkpoint || worlds.Checkpoint(ctx));
}

using ReleaseTestFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t building);

void CompareReleaseSiteWorkers(test::Context& ctx, BuildingFn candidate, int cases) {
    test::WorldPair worlds(ctx, kSettlerWorld);
    test::CalleeStubs stubs;
    InstallSettlerStubs(stubs);
    stubs.Install<2, kRecalculateTerritory>();
    DebugLogRecorder logs;
    SetUpWorld(ctx, worlds);
    // ReleaseSiteWorkers scans settlers 1..settler_high_water. Until the
    // first case with workers sets the count (SetUpWorkers), it is random
    // and the original read far past the world (crash at 0x50e443 with
    // S3RE_TEST_SEED 18266603132645735647): start without settlers.
    worlds.Both([](CGameData* w) { w->settler_high_water = 0; });
    static BuildingFn original = nullptr;
    static BuildingFn ours = nullptr;
    original = Original_CGameData_ReleaseSiteWorkers;
    ours = candidate;
    // Adapters with an EAX (not compared) for RunCycleCase.
    ReleaseTestFn runOriginal = [](CGameData* w, void* e, std::uint32_t b) -> std::uint32_t {
        original(w, e, b);
        return 0;
    };
    ReleaseTestFn runOurs = [](CGameData* w, void* e, std::uint32_t b) -> std::uint32_t {
        ours(w, e, b);
        return 0;
    };
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t number = 1 + ctx.rng().Below(50);
        static constexpr std::uint8_t kTypes[] = {0xf, 0x10, 0x12, 0x13, 2, 3, 0x1d, 0x21, 0x22, 1};
        const std::uint8_t type = ctx.rng().Chance(3, 4) ? kTypes[ctx.rng().Below(sizeof(kTypes))]
                                                        : static_cast<std::uint8_t>(ctx.rng().Below(0x39));
        SetUpBuilding(ctx, worlds, number, type, static_cast<std::uint16_t>(30 + ctx.rng().Below(300)),
                      static_cast<std::uint16_t>(30 + ctx.rng().Below(300)));
        const std::uint16_t garrison = ctx.rng().Chance(1, 5) ? 0 : SetUpWorkers(ctx, worlds, number);
        worlds.Both([&](CGameData* w) { w->buildings[number].garrison = garrison; });
        if (!RunCycleCase(ctx, worlds, stubs, logs, runOriginal, runOurs, false, i % 32 == 31 || i == cases - 1,
                          number)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or before): building %u type 0x%x", i, number, type);
            return;
        }
    }
}

void CompareCrushBuilding(test::Context& ctx, CrushFn candidate, int cases) {
    test::WorldPair worlds(ctx, kSettlerWorld);
    test::CalleeStubs stubs;
    InstallSettlerStubs(stubs);
    stubs.Install<1, kReleaseSiteWorkers>();
    stubs.Install<1, kRemoveBuildingGoods>();
    stubs.Install<1, kClearBuildingFootprint>();
    stubs.Install<1, kRemoveBuildingFromTiles>();
    stubs.Install<1, kBlockBuildingLine>();
    stubs.Install<1, kReleaseBuildingExtraTiles>();
    stubs.Install<5, kSpawnEffect>();
    stubs.Install<4, kRemoveMatchingEffects>();
    DebugLogRecorder logs;
    SetUpWorld(ctx, worlds);
    SetRingOffsets(worlds, 1000);
    std::int32_t& mission = g_campaignMission;
    const std::int32_t savedMission = mission;
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t number = 1 + ctx.rng().Below(50);
        static constexpr std::uint8_t kTypes[] = {0xf, 0x10, 0x12, 0x20, 0x21, 0x1f, 0x2f, 0x30, 0x32, 3};
        const std::uint8_t type = ctx.rng().Chance(3, 4) ? kTypes[ctx.rng().Below(sizeof(kTypes))]
                                                        : static_cast<std::uint8_t>(ctx.rng().Below(0x39));
        const bool campaign = ctx.rng().Chance(1, 4);
        std::uint16_t x = static_cast<std::uint16_t>(30 + ctx.rng().Below(320));
        std::uint16_t y = static_cast<std::uint16_t>(30 + ctx.rng().Below(320));
        if (campaign && ctx.rng().Chance(1, 2)) {
            const auto& position = kCampaign70Protected[ctx.rng().Below(17)];
            x = static_cast<std::uint16_t>(position[0] - 0x80);
            y = static_cast<std::uint16_t>(position[1] - 0x120);
            if (ctx.rng().Chance(2, 3)) {
                x = static_cast<std::uint16_t>(position[0]);
                y = static_cast<std::uint16_t>(position[1]);
            }
        }
        SetUpBuilding(ctx, worlds, number, type, x, y);
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Chance(1, 20) ? kFree : ctx.rng().Below(4));
        const auto garrison = static_cast<std::uint16_t>(ctx.rng().Chance(1, 4) ? 0 : 1 + ctx.rng().Below(50));
        const auto soldier = static_cast<std::uint16_t>(1 + ctx.rng().Below(50));
        std::int32_t teams[4];
        for (std::int32_t& team : teams) {
            team = static_cast<std::int32_t>(ctx.rng().Below(3));
        }
        worlds.Both([&](CGameData* w) {
            w->buildings[number].owner = owner;
            w->buildings[number].garrison = garrison;
            if (IsMilitary(static_cast<BuildingType>(type))) {
                w->buildings[number].guard = soldier;
            }
            for (int p = 0; p < 4; ++p) {
                w->players[p].team = teams[p];
            }
            w->pending_event_index.read = 0;
            w->pending_event_index.write = 0;
            w->event_index.read = 0;
            w->event_index.write = 0;
        });
        // Settlers around the building (enemy soldiers block the crush).
        for (std::int32_t dx = -16; dx <= 16; ++dx) {
            for (std::int32_t dy = -16; dy <= 16; ++dy) {
                const auto s = static_cast<std::uint16_t>(ctx.rng().Chance(1, 40) ? 1 + ctx.rng().Below(60) : 0);
                worlds.Both([&](CGameData* w) { w->tiles[x + dx][y + dy].settler = s; });
            }
        }
        for (std::uint32_t n = 1; n <= 60; ++n) {
            const auto sOwner = static_cast<std::uint8_t>(ctx.rng().Below(4));
            const auto art = static_cast<std::uint8_t>(ctx.rng().Below(0x30));
            const auto sx = static_cast<std::uint16_t>(x - 5 + ctx.rng().Below(10));
            const auto sy = static_cast<std::uint16_t>(y - 5 + ctx.rng().Below(10));
            worlds.Both([&](CGameData* w) {
                w->settlers[n].owner = sOwner;
                w->settlers[n].art = art;
                w->settlers[n].x = sx;
                w->settlers[n].y = sy;
            });
        }
        mission = campaign ? 70 : 3;
        const bool compareEax = owner != kFree;  // see CrushBuilding
        const bool ok = RunCycleCase(ctx, worlds, stubs, logs, Original_CGameData_CrushBuilding, candidate, compareEax,
                                     i % 32 == 31 || i == cases - 1, number);
        mission = savedMission;
        if (!ok) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or before): building %u type 0x%x at (%u, %u)", i, number, type,
                     x, y);
            return;
        }
    }
}

// Puts building `number` (type, race) at (x, y) and marks its layout tiles
// (footprint and border) as building tiles of it.
void PlaceOnTiles(test::WorldPair& worlds, std::uint32_t number, std::uint8_t type, std::uint8_t race,
                  std::uint16_t x, std::uint16_t y) {
    worlds.Both([&](CGameData* w) {
        Building& b = w->buildings[number];
        b.art = type;
        b.race = race;
        b.x = x;
        b.y = y;
        const std::uint32_t* p = Layout(race, type);
        while (*p++ != kMarkFootprint) {
        }
        for (; *p != kMarkEnd; p += 2) {
            if (*p == kMarkFootprintEnd || *p == kMarkBorder) {
                --p;
                continue;
            }
            Tile& t = TileAt(w, x + p[0], y + p[1]);
            t.flags |= layout::kTileHasBuilding;
            t.object_number = static_cast<std::uint16_t>(number);
        }
    });
}

void CompareDestroyBuildingAt(test::Context& ctx, TileFn candidate, int cases) {
    test::WorldPair worlds(ctx, kSettlerWorld);
    test::CalleeStubs stubs;
    stubs.Install<1, kCrushBuilding>([](CGameData*, std::uint32_t, const std::uint32_t* args) { return args[0] & 1; });
    DebugLogRecorder logs;
    SetUpWorld(ctx, worlds);
    SetRingOffsets(worlds, 1200);
    for (int i = 0; i < cases; ++i) {
        const auto x = static_cast<std::uint16_t>(40 + ctx.rng().Below(300));
        const auto y = static_cast<std::uint16_t>(40 + ctx.rng().Below(300));
        // No other building tiles nearby.
        for (std::int32_t dx = -14; dx <= 14; ++dx) {
            for (std::int32_t dy = -14; dy <= 14; ++dy) {
                worlds.Both([&](CGameData* w) {
                    w->tiles[x + dx][y + dy].flags &= static_cast<std::uint8_t>(~layout::kTileHasBuilding);
                });
            }
        }
        // One to three buildings around; the target tile is one of the
        // first one's layout tiles (or anywhere near it).
        const std::uint32_t count = 1 + ctx.rng().Below(3);
        for (std::uint32_t k = 0; k < count; ++k) {
            const std::uint32_t number = 1 + ctx.rng().Below(50);
            const auto type = static_cast<std::uint8_t>(1 + ctx.rng().Below(56));
            const auto race = static_cast<std::uint8_t>(ctx.rng().Below(4));
            PlaceOnTiles(worlds, number, type, race, static_cast<std::uint16_t>(x - 3 + ctx.rng().Below(7)),
                         static_cast<std::uint16_t>(y - 3 + ctx.rng().Below(7)));
        }
        // The target: a building tile near (x, y).
        std::uint32_t tx = x;
        std::uint32_t ty = y;
        for (int attempt = 0; attempt < 200; ++attempt) {
            const std::uint32_t cx = x - 6 + ctx.rng().Below(13);
            const std::uint32_t cy = y - 6 + ctx.rng().Below(13);
            if ((TileAt(worlds.original(), cx, cy).flags & layout::kTileHasBuilding) != 0) {
                tx = cx;
                ty = cy;
                break;
            }
        }
        if ((TileAt(worlds.original(), tx, ty).flags & layout::kTileHasBuilding) == 0) {
            continue;  // (would search past the table)
        }
        if (!RunCycleCase(ctx, worlds, stubs, logs, Original_CGameData_DestroyBuildingAt, candidate, true,
                          i % 32 == 31 || i == cases - 1, tx, ty)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or before): (%u, %u)", i, tx, ty);
            return;
        }
    }
}

// Wrong: a builder does not count as a site worker.
void __fastcall WrongReleaseSiteWorkers(CGameData* world, void* edx, std::uint32_t number) {
    std::vector<std::uint32_t> changed;
    for (std::uint32_t n = 1; n <= world->settler_high_water && n < 200; ++n) {
        if (world->settlers[n].job == Job::WalkToWork && world->settlers[n].art == 2) {
            world->settlers[n].job = Job::UnitIdle;
            changed.push_back(n);
        }
    }
    CGameData_ReleaseSiteWorkers(world, edx, number);
    for (const std::uint32_t n : changed) {
        if (world->settlers[n].job == Job::UnitIdle) {
            world->settlers[n].job = Job::WalkToWork;
        }
    }
}

// Wrong: enemy soldiers of class 9 do not protect.
std::uint32_t __fastcall WrongCrushBuilding(CGameData* world, void* edx, std::uint32_t number) {
    std::vector<std::uint32_t> changed;
    for (std::uint32_t n = 1; n <= 60; ++n) {
        if (g_settlerTypeClass[world->settlers[n].art] == 9 || g_settlerTypeClass[world->settlers[n].art] == 1) {
            changed.push_back(n);
        }
    }
    std::vector<std::uint8_t> arts;
    for (const std::uint32_t n : changed) {
        arts.push_back(world->settlers[n].art);
        world->settlers[n].art = 0;
    }
    const std::uint32_t result = CGameData_CrushBuilding(world, edx, number);
    for (std::size_t k = 0; k < changed.size(); ++k) {
        world->settlers[changed[k]].art = arts[k];
    }
    return result;
}

// Wrong: ignores the border list.
std::uint32_t __fastcall WrongDestroyBuildingAt(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t result = CGameData_DestroyBuildingAt(world, edx, x, y);
    return result ^ 2;
}

}  // namespace

S3RE_TEST(CGameData_ReleaseSiteWorkers_MatchesOriginal) {
    CompareReleaseSiteWorkers(ctx, &CGameData_ReleaseSiteWorkers, 2000);
}
S3RE_TEST(CGameData_ReleaseSiteWorkers_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareReleaseSiteWorkers(probe, &WrongReleaseSiteWorkers, 500);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_CrushBuilding_MatchesOriginal) {
    CompareCrushBuilding(ctx, &CGameData_CrushBuilding, 2000);
}
S3RE_TEST(CGameData_CrushBuilding_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareCrushBuilding(probe, &WrongCrushBuilding, 1000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_DestroyBuildingAt_MatchesOriginal) {
    CompareDestroyBuildingAt(ctx, &CGameData_DestroyBuildingAt, 2000);
}
S3RE_TEST(CGameData_DestroyBuildingAt_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDestroyBuildingAt(probe, &WrongDestroyBuildingAt, 300);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
