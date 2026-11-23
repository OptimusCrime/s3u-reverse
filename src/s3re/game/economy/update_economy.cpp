// CGameData::UpdateEconomy (0x004e05e0; was "UpdatePhase2"): the second
// world update of every logic step (CLogicThread::Step, between
// UpdateEnvironment and UpdateSettlers). () thiscall, ret; EAX = the phase
// counter + 1 before it wraps (the caller ignores it; reproduced anyway).
//
// The economy is updated in 18 time slices, counted by the world dword
// +0x110a57c (counter_110a57c, 0..17; +1 per call, 18 -> 0):
//
// - Phase 0 (StartEconomyCycle): CountDownTimers 0x4fee00 and
//   RecountPlayerPopulation 0x5104c0; for areas 1..area high water (re-read)
//   the request lists (+0x52) and building slot lists A/B (+0x9a, +0xe2) are
//   cleared; the military presence map B (+0xdf2848) is copied to A
//   (+0xde7448) and cleared, for the map_size/16 (signed division) square of
//   16x16-tile blocks, players 0..presence_max_player (inclusive).
// - Phases 1..16: settlers number = phase, phase+16, ... up to the settler
//   high water: owned units (MarksPresence, jump table 0x4e492c/0x4e4934 on
//   type - 5) that are IsOnMap mark their team in block (x/16, y/16) of map
//   B. Then buildings number = phase, phase+16, ... up to the building high
//   water (re-read): construction sites (buildup +0x21 != 0,
//   UpdateConstructionSite) and finished buildings by type (jump table
//   0x4e495c on type - 2, 55 entries, UpdateFinishedBuilding).
// - Phase 17: for every used area, FetchGoodsToTradeBuilding for the slot
//   list B, then the goods distribution by priority rank (DistributeGoods),
//   then FetchGoodsToTradeBuilding for slot list A.
//
// Every callee is called through its S3.EXE address (in the game Detours
// sends the call to our replacement, which is counted; tests can stub them).
//
// Building fields as used here: layout::Building (re/structs.md "Building"):
//   the worker or garrison (+0x08), pile_ring (the output pile, or the first
//   of the six ring piles of the harbour and the marketplace), piles[3]
//   (inputs), the site slot bits +0x16/+0x17, request_flags (below), the
//   per-kind bytes +0x19..+0x20 (views site, military, water, trading,
//   temple, residence, recruiting) and hit_points (military buildings: up to
//   250/500/1000, S3.EXE 0x659664).
//
// Goods requests name a consumer slot: the index into the area's
// condition_enabled/counters_278 arrays and the consumer table 0x659264
// (the building type that consumes; 0 = construction sites).
//
// Known differences (only reachable with unrealistic data):
// - The harbour and the marketplace build a per-goods "wanted" table of 36
//   dwords on the stack but index it with pile goods and the cursor +0x1b up
//   to 255; entries 36..255 are stale stack of the original (ours: 0).
//   The outcome is the same unless a ring pile holds good 0 (a free pile
//   record) and the cursor passes 35 (see TradeWantedSlots).
// - Phase 17's highest priority rank is set only for races 0..3; for another
//   race the original keeps the previous area's value, or (first area of the
//   call) an uninitialized stack slot; ours starts at 0 (no ranks).
// - Phase 17 divides by the best pile's distance score, which is 0 if a
//   free-standing pile lies at the building's position: an integer division
//   by zero in both.

#include "game/debug_log.h"
#include "game/enums/races.h"
#include "game/enums/settler_types.h"
#include "game/enums/buildings.h"
#include "game/enums/goods.h"
#include "game/enums/piles.h"
#include "game/enums/job_ids.h"
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
#include <vector>

namespace s3re::game {

namespace {

using layout::Area;
using layout::Building;
using layout::MapOffset;
using layout::Pile;
using layout::Player;
using layout::Settler;
using layout::Tile;

// Callees (S3.EXE addresses).
constexpr std::uintptr_t kCountDownTimers = 0x004fee00;
constexpr std::uintptr_t kRecountPlayerPopulation = 0x005104c0;
constexpr std::uintptr_t kIsOnMap = 0x005194e0;
constexpr std::uintptr_t kRequestSettlerForBuilding = 0x004dacd0;
constexpr std::uintptr_t kIsFreeOfCatapultsNearby = 0x005199e0;
constexpr std::uintptr_t kBlockTile = 0x0051b6f0;
constexpr std::uintptr_t kAddBuildingGoodsRequest = 0x004ddf30;
constexpr std::uintptr_t kGoodsRequestPriority = 0x004ff380;
constexpr std::uintptr_t kSendNearestWorkerToBuilding = 0x004db500;
constexpr std::uintptr_t kNotifyLocalPlayer = 0x004fec10;
constexpr std::uintptr_t kLeaveBuildingSlot = 0x0050cc10;
constexpr std::uintptr_t kClearSettlerJob = 0x00510440;
constexpr std::uintptr_t kSetTowerSoldierAnimation = 0x0050e200;
constexpr std::uintptr_t kCountAreaSettlersOfClass0 = 0x0050e750;
constexpr std::uintptr_t kCountAreaSettlersOfType = 0x0050e820;
constexpr std::uintptr_t kCreateSettler = 0x0050c9e0;
constexpr std::uintptr_t kSetSettlerAnimation = 0x0050e2c0;
constexpr std::uintptr_t kTakeBuildingExtraTiles = 0x004dd6f0;
constexpr std::uintptr_t kSetAreaBuildingSlotA = 0x004ddfe0;
constexpr std::uintptr_t kSetAreaBuildingSlotB = 0x004de040;
constexpr std::uintptr_t kSendNearestSettlerToBuilding = 0x004ddd70;
constexpr std::uintptr_t kSendNearestSettlerToPile = 0x004ddbd0;
constexpr std::uintptr_t kTakeGoodFromPile = 0x00505820;
constexpr std::uintptr_t kSubtractPlayerCounter = 0x004ffb40;
constexpr std::uintptr_t kPileIncAmountLeaving = 0x005058c0;
constexpr std::uintptr_t kPileIncAmountComing = 0x00505bc0;
constexpr std::uintptr_t kDispatchPileCarrier = 0x005052c0;
constexpr std::uintptr_t kFetchGoodsToTradeBuilding = 0x004de0a0;
constexpr std::uintptr_t kBuildingInputWord2A = 0x004df150;
constexpr std::uintptr_t kBuildingInputWord24 = 0x004df220;
constexpr std::uintptr_t kTileDistance = 0x004ff320;  // stdcall; called with ECX = world like the original
constexpr std::uintptr_t kSetSettlerTarget = 0x00503610;

constexpr std::uint8_t kFree = 0xff;  // owner of a free record
constexpr std::uint32_t kPhases = 18;
constexpr std::uint32_t kLastBuildingPhase = 16;
constexpr std::uint32_t kDistributionPhase = 17;

// Building::request_flags. Bit 0: the building wants inputs. Bits 1..4:
// probably "request anyway": the building ignores its player's request
// counter (Player::settler_lists[0..4], see layout.h) for that kind.
constexpr std::uint8_t kRequestInputs = 0x01;
constexpr std::uint8_t kRequestForceBoards = 0x02;
constexpr std::uint8_t kRequestForceStones = 0x04;
constexpr std::uint8_t kRequestForceBuilders = 0x08;
constexpr std::uint8_t kRequestForceDiggers = 0x10;
// The player's request counters (settler_lists[0..4]): boards, stones,
// (total), builders, diggers. Boards and stones are held back while bit 7
// of their counter is set.
constexpr std::uint32_t kBoardsCounter = 0;
constexpr std::uint32_t kStonesCounter = 1;
constexpr std::uint32_t kBuildersCounter = 3;
constexpr std::uint32_t kDiggersCounter = 4;
constexpr std::uint16_t kCounterHoldBack = 0x80;

// Building::buildup of a construction site.
constexpr std::uint8_t kSiteLevelling = 1;  // diggers wanted
constexpr std::uint8_t kSiteLevelled = 2;   // the footprint gets blocked once no ship is near
constexpr std::uint8_t kSiteBuilding = 3;

// Consumer slots of the construction sites' two materials.
constexpr std::uint32_t kConsumerSiteBoards = 13;
constexpr std::uint32_t kConsumerSiteStones = 16;

// A pile holds at most eight units; carriers are sent once five wait.
constexpr std::uint32_t kPileCapacity = 8;
constexpr std::uint32_t kUnitsWorthACarrier = 5;

constexpr std::uint8_t kSettlerFlagSurplus = 0x80;  // Settler::flags: a surplus carrier (not sent)
constexpr std::uint8_t kNoExtraTilesYet = 0xff;     // BuildingWorkshopBytes::extra_tiles
constexpr std::uint8_t kExtraTilesTaken = 0xfe;
constexpr std::uint16_t kNoDirection = 0xffff;       // Building::dock_direction
constexpr std::uint16_t kRouteUnset = 0xffff;        // Trade::route_x

// NotifyLocalPlayer kind 16: probably "military building without soldiers".
constexpr std::uint32_t kNotifyBuildingUnmanned = 16;

// The ring of six piles of the harbour and the marketplace.
constexpr std::uint32_t kRingPiles = 6;

// thiscall on the world with dword arguments.
template <typename... Args>
std::uint32_t Call(std::uintptr_t address, CGameData* world, Args... args) {
    using Fn = std::uint32_t(__fastcall*)(CGameData*, void*, decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(address)(world, nullptr, static_cast<std::uint32_t>(args)...);
}

// Record access by number (the numbers are not range-checked, as in the
// original).
Building& BuildingAt(CGameData* world, std::uint32_t n) { return *(world->buildings + n); }
Settler& SettlerAt(CGameData* world, std::uint32_t n) { return *(world->settlers + n); }
Pile& PileAt(CGameData* world, std::uint32_t n) { return *(world->piles + n); }
Player& PlayerAt(CGameData* world, std::uint32_t n) { return *(world->players + n); }
Area& AreaAt(CGameData* world, std::uint32_t n) { return *(world->areas + n); }
Tile& TileAt(CGameData* world, std::uint32_t x, std::uint32_t y) { return TileAtOffset(world, TileOffset(x, y)); }
Tile& DoorTile(CGameData* world, const Building& building) { return TileAt(world, building.door_x, building.door_y); }

// The pile on the tile `delta` bytes from the door tile (tile word +0x2).
std::uint32_t PileNextToDoor(CGameData* world, const Building& building, std::uint32_t delta) {
    return TileAtOffset(world, TileOffset(building.door_x, building.door_y) + delta).object_number;
}
constexpr std::uint32_t kStepX = kTileRowBytes;
constexpr std::uint32_t kStepY = kTileBytes;

// Units in a pile that no carrier has taken yet.
std::uint32_t Waiting(const Pile& pile) {
    return std::uint32_t{pile.amount} - std::uint32_t{pile.leaving};
}

// Building bytes +0x19..+0x20: the per-kind views of layout::Building.
std::uint8_t& Class1Coming(Building& b) { return b.military.class1_coming; }
std::uint8_t& Class1Wanted(Building& b) { return b.military.class1_wanted; }
std::uint8_t& Class2Coming(Building& b) { return b.military.class2_coming; }
std::uint8_t& Class2Wanted(Building& b) { return b.military.class2_wanted; }
std::uint8_t& Class3Coming(Building& b) { return b.military.class3_coming; }
std::uint8_t& Class3Wanted(Building& b) { return b.military.class3_wanted; }
std::uint8_t& SiteDiggers(Building& b) { return b.site.diggers; }
std::uint8_t& SiteBuilders(Building& b) { return b.site.builders; }

// --- callee wrappers ------------------------------------------------------

std::uint32_t RequestSettler(CGameData* world, std::uint32_t building, SettlerType type) {
    return Call(kRequestSettlerForBuilding, world, building, Value(type));
}
std::uint32_t Priority(CGameData* world, std::uint32_t amount, std::uint32_t coming) {
    return Call(kGoodsRequestPriority, world, amount, coming);
}
void AddRequest(CGameData* world, std::uint32_t building, Good good, std::uint32_t priority,
                std::uint32_t consumerSlot) {
    Call(kAddBuildingGoodsRequest, world, building, Value(good), priority, consumerSlot);
}
void Dispatch(CGameData* world, std::uint32_t pile) { Call(kDispatchPileCarrier, world, pile); }
std::uint32_t Distance(CGameData* world, std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1) {
    return Call(kTileDistance, world, x0, y0, x1, y1);
}

// --- shared building parts -------------------------------------------------

// No worker yet: ask for one of `type`; he becomes the building's worker.
void RequestWorker(CGameData* world, std::uint32_t n, SettlerType type) {
    const std::uint32_t settler = RequestSettler(world, n, type);
    if (settler != 0) {
        BuildingAt(world, n).worker = static_cast<std::uint16_t>(settler);
    }
}

// One input of a production building: the goods, the pile total at which
// it stops asking, and what the priority adds to the amount.
struct InputSpec {
    Good good;
    std::uint32_t full;
    std::uint32_t add;
    std::uint32_t consumerSlot;
};

// Input slot `slot` wants its goods while amount + coming != `full` and the
// building wants inputs; the priority counts the amount + `add`.
void RequestInput(CGameData* world, std::uint32_t n, std::uint32_t slot, const InputSpec& input) {
    Building& building = BuildingAt(world, n);
    const Pile& pile = PileAt(world, building.piles[slot]);
    const std::uint32_t amount = pile.amount;
    const std::uint32_t coming = pile.coming;
    if (amount + coming != input.full && (building.request_flags & kRequestInputs) != 0) {
        AddRequest(world, n, input.good, Priority(world, amount + input.add, coming), input.consumerSlot);
    }
}

// Input slot `slot` (workshops, shipyard) wants goods while amount + coming
// is below the building's wanted amount `limit` (and the pile is not full).
void RequestInputBelow(CGameData* world, std::uint32_t n, std::uint32_t slot, Good good, std::uint32_t limit,
                       std::uint32_t consumerSlot) {
    const Pile& pile = PileAt(world, BuildingAt(world, n).piles[slot]);
    const std::uint32_t amount = pile.amount;
    const std::uint32_t coming = pile.coming;
    const std::uint32_t total = amount + coming;
    if (total != kPileCapacity && total < limit) {
        AddRequest(world, n, good, Priority(world, amount, coming), consumerSlot);
    }
}

// The output pile (+0xe) gets a carrier once five or more units wait in it.
void CollectOutput(CGameData* world, std::uint32_t n) {
    const std::uint32_t pile = BuildingAt(world, n).pile_ring;
    if (static_cast<std::int32_t>(Waiting(PileAt(world, pile))) >= static_cast<std::int32_t>(kUnitsWorthACarrier)) {
        Dispatch(world, pile);
    }
}

// The common production building: inputs while it has a worker, else a
// worker request; then the output pile.
void Production(CGameData* world, std::uint32_t n, SettlerType worker, std::initializer_list<InputSpec> inputs) {
    if (BuildingAt(world, n).worker != 0) {
        std::uint32_t slot = 0;
        for (const InputSpec& input : inputs) {
            RequestInput(world, n, slot++, input);
        }
    } else {
        RequestWorker(world, n, worker);
    }
    CollectOutput(world, n);
}

// Carriers are sent to the piles next to the door once five units wait in
// them together (smiths, laboratory).
void CollectFromPiles(CGameData* world, const std::uint32_t* piles, std::uint32_t count) {
    std::uint32_t waiting = 0;
    for (std::uint32_t k = 0; k < count; ++k) {
        waiting += Waiting(PileAt(world, piles[k]));
    }
    if (waiting < kUnitsWorthACarrier) {
        return;
    }
    for (std::uint32_t k = 0; k < count; ++k) {
        const Pile& pile = PileAt(world, piles[k]);
        if (pile.amount != pile.leaving) {
            Dispatch(world, piles[k]);
        }
    }
}

// --- construction sites ------------------------------------------------------

// The site's slot bits `bits` (+0x16 diggers / +0x17 builders): for every
// slot below `count` whose bit is clear, a settler of `type` is requested.
void RequestSiteWorkers(CGameData* world, std::uint32_t n, std::uint8_t Building::*bits,
                        std::uint8_t& (*count)(Building&), SettlerType type) {
    Building& site = BuildingAt(world, n);
    for (std::uint32_t slot = 0; slot < 8; ++slot) {
        const auto bit = static_cast<std::uint8_t>(1u << slot);
        if ((site.*bits & bit) == 0 && count(site) >= slot + 1 && RequestSettler(world, n, type) != 0) {
            site.*bits = static_cast<std::uint8_t>(site.*bits + bit);
        }
    }
}

// The levelling list of a site: int32 (dx, dy) lists in S3.EXE 0x6aa170
// per race and type, after a first list ended by 0x80000000: the tiles to
// block (ended by 0x80000001), then tiles whose object is cleared (ended by
// 0x80000003; a 0x80000002 at a pair start is skipped). The original reads
// the second list from the first list's end marker on, i.e. one dword off
// the list's pairs: (0x80000001, dy) and (dx, 0x80000002) act as offsets 1
// and 2 through the 32-bit wrap of the tile offset (0x80000000 * 0x4800 and
// * 0x18 are multiples of 2^32), and no 0x80000002 of the game's table lands
// on a pair start. Reproduced as is.
constexpr std::int32_t kEndFirstList = static_cast<std::int32_t>(0x80000000u);
constexpr std::int32_t kEndBlockList = static_cast<std::int32_t>(0x80000001u);
constexpr std::int32_t kSkipEntry = static_cast<std::int32_t>(0x80000002u);
constexpr std::int32_t kEndClearList = static_cast<std::int32_t>(0x80000003u);

const std::int32_t* SiteTileList(const Building& site) {
    const auto* entry = reinterpret_cast<const std::int32_t*>(
        g_buildingLayouts[std::uint32_t{site.race} * kBuildingTypeCount + std::uint32_t{site.art}]);
    const std::int32_t* p = entry + 1;
    if (entry[0] != kEndFirstList) {
        std::int32_t value;
        do {
            value = *p++;
        } while (value != kEndFirstList);
    }
    return p;
}

// Once IsFreeOfCatapultsNearby holds for every tile of the footprint,
// the site's tiles are blocked and their objects cleared. (The site's
// position is read again for every tile, as in the original.)
void FinishLevelling(CGameData* world, Building& site) {
    const auto tileX = [&](const std::int32_t* p) { return std::uint32_t{site.x} + p[0]; };
    const auto tileY = [&](const std::int32_t* p) { return std::uint32_t{site.y} + p[1]; };
    bool blocked = false;
    for (const std::int32_t* p = SiteTileList(site); *p != kEndBlockList; p += 2) {
        if (Call(kIsFreeOfCatapultsNearby, world, tileX(p), tileY(p)) == 0) {
            blocked = true;  // the original tests every tile
        }
    }
    if (blocked) {
        return;
    }
    site.buildup = kSiteBuilding;
    const std::int32_t* p = SiteTileList(site);
    for (; *p != kEndBlockList; p += 2) {
        Call(kBlockTile, world, tileX(p), tileY(p));
        TileAt(world, tileX(p), tileY(p)).object = 0;
    }
    for (; *p != kEndClearList; p += 2) {
        if (*p == kSkipEntry) {
            ++p;
        }
        Tile& tile = TileAt(world, tileX(p), tileY(p));
        if (tile.object != 0xff) {
            tile.object = 0;
        }
    }
}

// A site requests a material (pile `slot`) until the pile holds `missing`
// units, unless the player holds that material back.
void RequestMaterial(CGameData* world, std::uint32_t n, std::uint32_t slot, std::uint8_t missing, Good material,
                     std::uint32_t counter, std::uint8_t forceFlag, std::uint32_t consumerSlot) {
    Building& site = BuildingAt(world, n);
    const Pile& pile = PileAt(world, site.piles[slot]);
    const std::uint32_t total = std::uint32_t{pile.amount} + std::uint32_t{pile.coming};
    if (missing == total || total == kPileCapacity) {
        return;
    }
    if ((PlayerAt(world, site.owner).settler_lists[counter] & kCounterHoldBack) == 0 ||
        (site.request_flags & forceFlag) != 0) {
        AddRequest(world, n, material, site.work_y, consumerSlot);
    }
}

// Purpose: Moves a building site along: asks for diggers while it is being
// levelled and for builders once it has material, finishes the levelling
// when its footprint is free (blocking the tiles and clearing their
// objects), and requests the two building materials.
void UpdateConstructionSite(CGameData* world, std::uint32_t n) {
    Building& site = BuildingAt(world, n);
    const auto playerCounter = [&](std::uint32_t counter) {
        return PlayerAt(world, site.owner).settler_lists[counter];
    };
    const auto wantsInputs = [&]() { return (site.request_flags & kRequestInputs) != 0; };

    if (((site.request_flags & kRequestForceDiggers) != 0 || playerCounter(kDiggersCounter) == 0) &&
        site.buildup == kSiteLevelling && wantsInputs()) {
        RequestSiteWorkers(world, n, &Building::digger_slots, &SiteDiggers, SettlerType::Digger);
    }
    // Builders come once material lies there and the site is levelled (or,
    // early, while diggers work and work_x, unknown here, is below 40).
    const std::uint32_t material = std::uint32_t{PileAt(world, site.piles[1]).amount} +
                                   std::uint32_t{PileAt(world, site.piles[0]).amount};
    if (((site.request_flags & kRequestForceBuilders) != 0 || playerCounter(kBuildersCounter) == 0) &&
        material != 0 && (site.buildup >= kSiteLevelled || (site.work_x < 40 && site.site.workers_at_work != 0)) &&
        wantsInputs()) {
        RequestSiteWorkers(world, n, &Building::builder_slots, &SiteBuilders, SettlerType::Builder);
    }
    if (site.buildup == kSiteLevelled) {
        FinishLevelling(world, site);
    }
    if ((site.site.workers_at_work != 0 || site.buildup >= kSiteLevelled) && wantsInputs()) {
        RequestMaterial(world, n, 0, site.site.boards_missing, Good::Board, kBoardsCounter, kRequestForceBoards,
                        kConsumerSiteBoards);
        RequestMaterial(world, n, 1, site.site.stones_missing, Good::Stone, kStonesCounter, kRequestForceStones,
                        kConsumerSiteStones);
    }
}

// --- finished buildings --------------------------------------------------------

// The soldier classes a military building holds, indexed 1..3.
constexpr std::int32_t kSoldierClasses = 3;
bool IsSoldierClass(SettlerClass c) {
    return c == SettlerClass::Swordsman || c == SettlerClass::Bowman || c == SettlerClass::Pikeman;
}

// Takes the first soldier of class `leaving` out of the garrison chain and
// puts it at the door.
void SendOutSoldier(CGameData* world, Building& building, SettlerClass leaving) {
    std::uint32_t soldier = building.garrison;
    if (ClassOf(SettlerAt(world, soldier)) == leaving) {
        building.garrison = SettlerAt(world, soldier).next;
    } else {
        std::uint32_t previous;
        do {
            previous = soldier;
            soldier = SettlerAt(world, soldier).next;
        } while (ClassOf(SettlerAt(world, soldier)) != leaving);
        SettlerAt(world, previous).next = SettlerAt(world, soldier).next;
    }
    Settler& record = SettlerAt(world, soldier);
    if (ClassOf(record) == SettlerClass::Bowman) {
        Call(kLeaveBuildingSlot, world, soldier);
    }
    Call(kClearSettlerJob, world, soldier);
    record.x = building.door_x;
    record.y = building.door_y;
    DoorTile(world, building).settler = static_cast<std::uint16_t>(soldier);
    Call(kSetTowerSoldierAnimation, world, soldier);
    record.counter = 0;
}

// Whether an enemy swordsman or pikeman stands within 10 tiles.
bool EnemyNear(CGameData* world, const Building& building) {
    for (const MapOffset* offset = world->offsets; offset->distance < 10; ++offset) {
        const std::uint32_t x = std::uint32_t{building.x} + static_cast<std::uint32_t>(offset->dx);
        const std::uint32_t y = std::uint32_t{building.y} + static_cast<std::uint32_t>(offset->dy);
        if (Call(kIsOnMap, world, x, y) == 0) {
            continue;
        }
        const std::uint32_t other = TileAt(world, x, y).settler;
        if (other == 0) {
            continue;
        }
        const Settler& settler = SettlerAt(world, other);
        const SettlerClass c = ClassOf(settler);
        if ((c == SettlerClass::Swordsman || c == SettlerClass::Pikeman) &&
            PlayerAt(world, settler.owner).team != PlayerAt(world, building.owner).team) {
            return true;
        }
    }
    return false;
}

// Purpose: A military building (tower, castle, ...): heals, lets one surplus
// soldier of a class leave when no enemy soldier is near, asks for soldiers
// of the three classes it lacks (nearest first, within 20, 40, 80 tiles) and
// warns its player when it is empty.
void UpdateMilitaryBuilding(CGameData* world, std::uint32_t n) {
    Building& building = BuildingAt(world, n);
    static constexpr BuildingType kMilitaryTypes[] = {BuildingType::SmallTower, BuildingType::LargeTower,
                                                      BuildingType::Castle};
    for (std::uint32_t k = 0; k < 3; ++k) {
        if (TypeOf(building) == kMilitaryTypes[k] &&
            std::uint32_t{building.hit_points} != static_cast<std::uint32_t>(g_militaryHitPoints[k])) {
            ++building.hit_points;
        }
    }
    std::uint32_t garrison[kSoldierClasses + 1] = {};  // per class 1..3
    for (std::uint32_t s = building.garrison; s != 0; s = SettlerAt(world, s).next) {
        const SettlerClass c = ClassOf(SettlerAt(world, s));
        if (IsSoldierClass(c)) {
            ++garrison[Value(c)];
        }
    }
    if (!EnemyNear(world, building)) {
        SettlerClass leaving = SettlerClass::Civilian;  // none
        if (garrison[1] > building.military.class1_wanted) {
            leaving = SettlerClass::Swordsman;
        } else if (garrison[2] > building.military.class2_wanted) {
            leaving = SettlerClass::Bowman;
        } else if (garrison[3] > building.military.class3_wanted) {
            leaving = SettlerClass::Pikeman;
        }
        if (leaving != SettlerClass::Civilian && DoorTile(world, building).settler == 0) {
            SendOutSoldier(world, building, leaving);
        }
    }
    // Per class: its coming / wanted bytes and the soldier types asked for,
    // the best level first.
    struct Wanted {
        std::uint8_t& (*coming)(Building&);
        std::uint8_t& (*wanted)(Building&);
        SettlerType types[3];
    };
    static const Wanted kWanted[kSoldierClasses] = {
        {&Class1Coming, &Class1Wanted, {SettlerType::Swordsman3, SettlerType::Swordsman2, SettlerType::Swordsman1}},
        {&Class2Coming, &Class2Wanted, {SettlerType::Bowman3, SettlerType::Bowman2, SettlerType::Bowman1}},
        {&Class3Coming, &Class3Wanted, {SettlerType::Pikeman3, SettlerType::Pikeman2, SettlerType::Pikeman1}}};
    for (std::uint32_t c = 0; c < kSoldierClasses; ++c) {
        const Wanted& wanted = kWanted[c];
        const std::uint32_t coming = wanted.coming(building);
        const std::uint32_t target = wanted.wanted(building);
        if (coming >= target || garrison[c + 1] >= target - coming) {
            continue;
        }
        // The original keeps calling with a "found" flag; only the first
        // success sends a soldier.
        bool sent = false;
        for (const std::uint32_t distance : {20u, 40u, 80u}) {
            for (const SettlerType type : wanted.types) {
                if (!sent && Call(kSendNearestWorkerToBuilding, world, n, Value(type), distance) != 0) {
                    sent = true;
                }
            }
        }
        if (sent) {
            ++wanted.coming(building);
        }
    }
    if (building.garrison == 0 && std::uint32_t{building.military.class2_coming} +
                                          std::uint32_t{building.military.class3_coming} +
                                          std::uint32_t{building.military.class1_coming} ==
                                      0) {
        Call(kNotifyLocalPlayer, world, building.owner, kNotifyBuildingUnmanned, building.x, building.y);
    }
}

// Purpose: The tool smith and the weapon smith, whose worker drops the
// products on the tiles around the door: requests coal and iron and sends
// carriers to those piles once five units wait there.
void UpdateSmith(CGameData* world, std::uint32_t n, std::uint32_t coalSlot, std::uint32_t ironSlot,
                 bool sevenTiles) {
    Building& building = BuildingAt(world, n);
    if (building.worker == 0) {
        RequestWorker(world, n, SettlerType::Smith);
    } else {
        RequestInput(world, n, 0, {Good::Coal, kPileCapacity, 0, coalSlot});
        RequestInput(world, n, 1, {Good::Iron, kPileCapacity, 0, ironSlot});
    }
    if (sevenTiles) {
        // Door tile, (x-1, y), (x+1, y), (x-1, y-1), (x+1, y+1), (x, y-1), (x, y+1).
        const std::uint32_t piles[7] = {
            PileNextToDoor(world, building, 0),
            PileNextToDoor(world, building, 0 - kStepX),
            PileNextToDoor(world, building, kStepX),
            PileNextToDoor(world, building, 0 - kStepX - kStepY),
            PileNextToDoor(world, building, kStepX + kStepY),
            PileNextToDoor(world, building, 0 - kStepY),
            PileNextToDoor(world, building, kStepY)};
        CollectFromPiles(world, piles, 7);
    } else {
        // Door tile, (x-1, y), (x+1, y).
        const std::uint32_t piles[3] = {PileNextToDoor(world, building, 0),
                                        PileNextToDoor(world, building, 0 - kStepX),
                                        PileNextToDoor(world, building, kStepX)};
        CollectFromPiles(world, piles, 3);
    }
}

// Purpose: The barracks: turns spare carriers into recruits (one per weapon
// in its input piles, limited by the area's recruit orders) while the area
// keeps enough carriers, and requests swords, bows and spears.
void UpdateBarracks(CGameData* world, std::uint32_t n) {
    Building& barracks = BuildingAt(world, n);
    const std::uint32_t areaNumber = DoorTile(world, barracks).area;
    Area& area = AreaAt(world, areaNumber);
    const std::uint32_t civilians = Call(kCountAreaSettlersOfClass0, world, areaNumber);
    // settings_extra[0]: the share of civilians (percent) kept as carriers.
    const std::uint32_t carriersKept = civilians * area.settings_extra[0] / 100 + 2;
    const std::uint32_t carriers = Call(kCountAreaSettlersOfType, world, areaNumber, Value(SettlerType::Carrier));
    if (carriersKept < carriers) {
        for (std::uint32_t slot = 0; slot < 3; ++slot) {
            std::uint8_t& recruits = barracks.recruiting.recruits[slot];
            const bool ordered = area.counters_242[slot] != 0 || area.setting_241 != 0;
            if (!ordered || recruits >= PileAt(world, barracks.piles[slot]).amount) {
                continue;
            }
            const std::uint32_t carrier = RequestSettler(world, n, SettlerType::Carrier);
            if (carrier == 0) {
                continue;
            }
            if (area.counters_242[slot] != 0) {
                --area.counters_242[slot];
            }
            SettlerAt(world, carrier).saved_counter = static_cast<std::uint16_t>(slot + 1);
            ++recruits;
        }
    }
    if ((barracks.request_flags & kRequestInputs) == 0) {
        return;
    }
    static constexpr Good kWeapons[3] = {Good::Sword, Good::Bow, Good::Spear};
    for (std::uint32_t slot = 0; slot < 3; ++slot) {
        const Pile& pile = PileAt(world, barracks.piles[slot]);
        const std::uint32_t amount = pile.amount;
        const std::uint32_t coming = pile.coming;
        // The flag is read again for the second and third weapon, as in the original.
        if (amount + coming != g_smithInputFull && (slot == 0 || (barracks.request_flags & kRequestInputs) != 0)) {
            AddRequest(world, n, kWeapons[slot], Priority(world, amount, coming), 0);
        }
    }
}

// Purpose: A residence: sends out one new settler (a carrier) per counted
// birth (+0x19) while the player's population is within its limit and the
// door is free.
void UpdateResidence(CGameData* world, std::uint32_t n, std::uint32_t animation) {
    Building& house = BuildingAt(world, n);
    if (house.residence.births == 0 || DoorTile(world, house).settler != 0) {
        return;
    }
    --house.residence.births;
    const std::uint32_t owner = house.owner;
    const Player& player = PlayerAt(world, owner);
    if (player.population > player.population_limit) {
        return;
    }
    const std::uint32_t settler =
        Call(kCreateSettler, world, house.door_x, house.door_y, Value(SettlerType::Carrier), owner);
    if (settler != 0) {
        SetJob(SettlerAt(world, settler), SettlerJob::LeaveHouse);
        Call(kSetSettlerAnimation, world, settler, animation);
    }
}

// Purpose: A war machine workshop (catapult, ballista and cannon workshops,
// the large gong; the shipyard is similar): once its extra tiles are taken
// (TakeBuildingExtraTiles), its state +0x1f fixes how much of its three
// inputs it wants (+0x1a..+0x1c, total in +0x20); requests inputs below that.
// (These are the bytes of Building::workshop, layout.h.)
struct WorkshopSpec {
    SettlerType worker;
    std::uint8_t stateA, wantA[3];  // state high nibble 0x10..0x40: the machine being built
    std::uint8_t stateB, wantB[3];
    InputSpec inputs[3];  // good and consumer slot used
    bool logGong;         // "Gong bauen - nach SecureProduction ..."
};

void SetWanted(Building& building, const std::uint8_t (&want)[3]) {
    building.workshop.wanted[0] = want[0];
    building.workshop.wanted[1] = want[1];
    building.workshop.wanted[2] = want[2];
}

std::uint8_t SumOfWanted(const Building& building) {
    return static_cast<std::uint8_t>(building.workshop.wanted[2] + building.workshop.wanted[0] +
                                     building.workshop.wanted[1]);
}

void UpdateWorkshop(CGameData* world, std::uint32_t n, const WorkshopSpec& spec) {
    Building& building = BuildingAt(world, n);
    if (building.worker == 0) {
        RequestWorker(world, n, spec.worker);
        return;
    }
    if (building.workshop.state != 0 && building.workshop.extra_tiles == kNoExtraTilesYet) {
        Call(kTakeBuildingExtraTiles, world, n);
        if (spec.logGong) {
            GameDebugLog(texts::kBuildGong, std::uint32_t{building.workshop.extra_tiles});
        }
        if (building.workshop.extra_tiles == kExtraTilesTaken) {
            if (building.workshop.state == spec.stateA) {
                SetWanted(building, spec.wantA);
            } else if (spec.stateB != 0 && building.workshop.state == spec.stateB) {
                SetWanted(building, spec.wantB);
            }
            building.workshop.extra_tiles = SumOfWanted(building);
        }
    }
    for (std::uint32_t slot = 0; slot < 3; ++slot) {
        RequestInputBelow(world, n, slot, spec.inputs[slot].good, building.workshop.wanted[slot],
                          spec.inputs[slot].consumerSlot);
    }
}

// Purpose: The shipyard: like a workshop, but the wanted amounts of its two
// inputs (boards, iron) come from tables by the ship being built (states
// 0x30..0x33: trade ship, 0x40..0x43: ferry).
void UpdateShipyard(CGameData* world, std::uint32_t n) {
    Building& shipyard = BuildingAt(world, n);
    if (shipyard.worker == 0) {
        RequestWorker(world, n, SettlerType::ShipyardWorker);
        return;
    }
    if (shipyard.workshop.state != 0 && shipyard.workshop.extra_tiles == kNoExtraTilesYet) {
        Call(kTakeBuildingExtraTiles, world, n);
        if (shipyard.workshop.extra_tiles == kExtraTilesTaken) {
            const std::uint8_t state = shipyard.workshop.state;
            int column = -1;
            if (state >= 0x30 && state <= 0x33) {
                column = 0;
            } else if (state >= 0x40 && state <= 0x43) {
                column = 1;
            }
            if (column >= 0) {
                const ShipyardInputs& inputs = g_shipyardInputs[shipyard.workshop.state & 0xfu][column];
                shipyard.workshop.wanted[0] = static_cast<std::uint8_t>(inputs.first);
                shipyard.workshop.wanted[1] = static_cast<std::uint8_t>(inputs.second);
                shipyard.workshop.wanted[2] = static_cast<std::uint8_t>(inputs.third);
            }
            shipyard.workshop.extra_tiles = SumOfWanted(shipyard);
        }
    }
    RequestInputBelow(world, n, 0, Good::Board, shipyard.workshop.wanted[0], 14);
    RequestInputBelow(world, n, 1, Good::Iron, shipyard.workshop.wanted[1], 9);
}

// The goods the ring of six piles of a harbour or marketplace (from +0xe,
// linked by pile +0xc) can take: goods of piles with room are wanted; then
// each free pile (good Empty) claims the next goods type at the cursor
// +0x1b (wrapping after 35) that nothing claims yet. `wanted` has 256
// entries: the original's table has 36 and reads stale stack beyond (see
// the header).
void TradeWantedSlots(CGameData* world, Building& building, std::uint32_t (&wanted)[256]) {
    std::uint32_t freePiles = 0;
    std::uint32_t pileNumber = building.pile_ring;
    for (std::uint32_t k = 0; k < kRingPiles; ++k) {
        const Pile& pile = PileAt(world, pileNumber);
        if (GoodOf(pile) == Good::Empty) {
            ++freePiles;
        } else if (static_cast<std::int32_t>((std::uint32_t{pile.leaving} >> 4) + std::uint32_t{pile.amount}) <
                   static_cast<std::int32_t>(kPileCapacity)) {
            wanted[pile.good] = 1;
        }
        pileNumber = pile.next;
    }
    while (freePiles != 0) {
        const std::uint8_t cursor = building.trading.goods_cursor;
        if (wanted[cursor] == 0) {
            wanted[cursor] = 1;
            if (cursor >= kGoodCount) {
                building.trading.goods_cursor = 0;
            }
            --freePiles;
        }
        ++building.trading.goods_cursor;
    }
}

// Purpose: The harbour and the marketplace (was "0x21 market-like, 0x22
// port-like": the harbour is 0x21, it has a landing direction): offer the
// goods types their ring piles can take to the area (slot list A or B by the
// trade record's mode); then the harbour calls a settler to the door every
// 7th visit when a ring pile holds goods, the marketplace sends settlers to
// fetch the goods of its ring piles.
void UpdateTradeBuilding(CGameData* world, std::uint32_t n, bool isMarketplace) {
    Building& building = BuildingAt(world, n);
    const layout::Trade& trade = *(world->trades + building.trade);
    if (trade.route_x[3] == kRouteUnset || trade.mode == 0 ||
        (!isMarketplace && building.dock_direction == kNoDirection)) {
        return;
    }
    std::uint32_t wanted[256] = {};
    TradeWantedSlots(world, building, wanted);
    for (std::uint32_t good = 1; good < kGoodCount; ++good) {
        const std::uint8_t ordered = trade.ordered[good];
        if (wanted[good] != 0 && ordered != 0 && ordered != trade.coming[good]) {
            if (trade.mode == 1) {
                Call(kSetAreaBuildingSlotA, world, n, good);
            } else if (trade.mode == 2) {
                Call(kSetAreaBuildingSlotB, world, n, good);
            }
        }
    }
    if (!isMarketplace) {
        if (building.trading.call_countdown != 0) {
            --building.trading.call_countdown;
            return;
        }
        bool holdsGoods = false;
        std::uint32_t pile = building.pile_ring;
        for (std::uint32_t k = 0; k < kRingPiles; ++k) {
            if (PileAt(world, pile).amount != 0) {
                holdsGoods = true;
            }
            pile = PileAt(world, pile).next;
        }
        if (holdsGoods) {
            Call(kSendNearestSettlerToBuilding, world, n);
            building.trading.call_countdown = 6;
        }
        return;
    }
    std::uint32_t pileNumber = building.pile_ring;
    for (std::uint32_t k = 0; k < kRingPiles; ++k) {
        const Pile& pile = PileAt(world, pileNumber);
        if (pile.amount != 0 && pile.amount != (pile.leaving & 0xf)) {
            Call(kSendNearestSettlerToPile, world, pileNumber, building.owner, n);
        }
        pileNumber = PileAt(world, pileNumber).next;
    }
}

// Purpose: A big temple (pyramid, large temple, large pagoda, Amazon large
// temple): every 250 visits one point (+0x19); with two points and a free
// door it creates a mage, which raises the player's population limit base.
void UpdateTemple(CGameData* world, std::uint32_t n) {
    Building& temple = BuildingAt(world, n);
    if (temple.temple.countdown == 0) {
        temple.temple.countdown = 250;
    } else if (--temple.temple.countdown == 0) {
        ++temple.temple.points;
    }
    if (temple.temple.points < 2 || DoorTile(world, temple).settler != 0) {
        return;
    }
    const std::uint32_t settler =
        Call(kCreateSettler, world, temple.door_x, temple.door_y, Value(SettlerType::Mage), temple.owner);
    if (settler != 0) {
        Settler& mage = SettlerAt(world, settler);
        SetJob(mage, SettlerJob::LeaveHouse);
        Call(kSetSettlerAnimation, world, settler, 281u);
        ++PlayerAt(world, mage.owner).population_limit_base;
    }
    temple.temple.points = static_cast<std::uint8_t>(temple.temple.points - 2);
}

// Purpose: A small temple (sphinx, small temple, small pagoda, Amazon small
// temple): requests its race's drink; every 30 visits one unit of its input
// pile is offered (taken from the pile and the player's goods count) and
// turns into mana (twice at the small pagoda).
void UpdateOffering(CGameData* world, std::uint32_t n, Good drink, bool twice) {
    Building& temple = BuildingAt(world, n);
    RequestInput(world, n, 0, {drink, kPileCapacity, 0, 0});
    if (temple.temple.countdown == 0) {
        if (PileAt(world, temple.piles[0]).amount != 0) {
            temple.temple.countdown = 30;
            Call(kPileIncAmountLeaving, world, temple.piles[0]);
        }
        return;
    }
    if (--temple.temple.countdown != 0) {
        return;
    }
    Call(kTakeGoodFromPile, world, temple.piles[0]);
    Call(kSubtractPlayerCounter, world, temple.owner, PileAt(world, temple.piles[0]).good, 1u);
    const int offerings = twice ? 2 : 1;
    for (int k = 0; k < offerings; ++k) {
        PlayerAt(world, temple.owner).mana += static_cast<std::int32_t>(g_manaPerOffering.get());
        PlayerAt(world, temple.owner).mana_total += static_cast<std::int32_t>(g_manaPerOffering.get());
    }
    g_playerTallies[std::uint32_t{temple.owner}].unk0c += offerings;
}

// Purpose: The laboratory: requests gems and sulfur and collects its output
// from the two piles beside the door.
void UpdateLaboratory(CGameData* world, std::uint32_t n) {
    Building& laboratory = BuildingAt(world, n);
    if (laboratory.worker == 0) {
        RequestWorker(world, n, SettlerType::Alchemist);
    } else {
        RequestInput(world, n, 0, {Good::Gems, kPileCapacity, 0, 0});
        RequestInput(world, n, 1, {Good::Sulfur, kPileCapacity, 0, 0});
    }
    // (x+1, y) and (x-1, y) of the door.
    const std::uint32_t right = PileNextToDoor(world, laboratory, kStepX);
    const std::uint32_t left = PileNextToDoor(world, laboratory, 0 - kStepX);
    const std::uint32_t waiting = std::uint32_t{PileAt(world, left).amount} - std::uint32_t{PileAt(world, left).leaving} -
                                  std::uint32_t{PileAt(world, right).leaving} + std::uint32_t{PileAt(world, right).amount};
    if (waiting < kUnitsWorthACarrier) {
        return;
    }
    if (PileAt(world, right).amount != PileAt(world, right).leaving) {
        Dispatch(world, right);
    }
    if (PileAt(world, left).amount != PileAt(world, left).leaving) {
        Dispatch(world, left);
    }
}

const WorkshopSpec kCatapultWorkshop = {SettlerType::Smith, 0x10, {6, 3, 6}, 0x20, {0, 0, 6},
                                        {{Good::Board, 0, 0, 43}, {Good::Iron, 0, 0, 10}, {Good::Stone, 0, 0, 17}},
                                        false};
const WorkshopSpec kBallistaWorkshop = {SettlerType::Smith, 0x11, {5, 6, 6}, 0x21, {0, 3, 6},
                                        {{Good::Board, 0, 0, 44}, {Good::Iron, 0, 0, 11}, {Good::Log, 0, 0, 19}},
                                        false};
const WorkshopSpec kCannonWorkshop = {SettlerType::Smith, 0x12, {3, 8, 6}, 0x22, {0, 3, 6},
                                      {{Good::Board, 0, 0, 45}, {Good::Iron, 0, 0, 12}, {Good::Gunpowder, 0, 0, 0}},
                                      false};
const WorkshopSpec kLargeGong = {SettlerType::Smith, 0x13, {8, 4, 6}, 0, {0, 0, 0},
                                 {{Good::Board, 0, 0, 46}, {Good::Iron, 0, 0, 47}, {Good::Gold, 0, 0, 0}},
                                 true};

// Purpose: The per-type economy work of a finished building (jump table
// 0x4e495c): worker and input requests, output collection and the special
// buildings above. Inputs: {goods, pile total that stops requests, added to
// the amount for the priority, consumer slot}.
void UpdateFinishedBuilding(CGameData* world, std::uint32_t n) {
    using B = BuildingType;
    using S = SettlerType;
    using G = Good;
    constexpr std::uint32_t kFull = kPileCapacity;
    const Building& building = BuildingAt(world, n);
    switch (TypeOf(building)) {
    case B::Woodcutter: Production(world, n, S::Woodcutter, {}); break;
    case B::Stonecutter: Production(world, n, S::Stonecutter, {}); break;
    case B::Sawmill: Production(world, n, S::Sawmiller, {{G::Log, kFull, 0, 18}}); break;
    case B::Forester:
        if (building.worker == 0) {
            RequestWorker(world, n, S::Forester);
        }
        break;
    // Mines: three foods, each asked for until 2 or 4 units are there.
    case B::CoalMine:
        Production(world, n, S::Miner, {{G::Fish, 2, 6, 40}, {G::Meat, 2, 6, 36}, {G::Bread, 4, 4, 30}});
        break;
    case B::GoldMine:
        Production(world, n, S::Miner, {{G::Fish, 4, 4, 39}, {G::Meat, 2, 6, 37}, {G::Bread, 2, 6, 32}});
        break;
    case B::IronMine:
        Production(world, n, S::Miner, {{G::Fish, 2, 6, 41}, {G::Meat, 4, 4, 35}, {G::Bread, 2, 6, 31}});
        break;
    case B::GoldSmelter: Production(world, n, S::Smelter, {{G::Coal, kFull, 0, 2}, {G::GoldOre, kFull, 0, 0}}); break;
    case B::IronSmelter: Production(world, n, S::Smelter, {{G::Coal, kFull, 0, 1}, {G::IronOre, kFull, 0, 0}}); break;
    case B::Toolsmith: UpdateSmith(world, n, 4, 8, true); break;
    case B::Weaponsmith: UpdateSmith(world, n, 3, 7, false); break;
    case B::Vineyard: Production(world, n, S::Winegrower, {}); break;
    case B::SmallTower:
    case B::LargeTower:
    case B::Castle: UpdateMilitaryBuilding(world, n); break;
    case B::Mill: Production(world, n, S::Miller, {{G::Grain, kFull, 0, 22}}); break;
    case B::Barracks: UpdateBarracks(world, n); break;
    case B::Bakery: Production(world, n, S::Baker, {{G::Flour, kFull, 0, 0}, {G::Water, kFull, 0, 26}}); break;
    case B::Slaughterhouse: Production(world, n, S::Butcher, {{G::Pig, kFull, 0, 0}}); break;
    case B::Distillery: Production(world, n, S::Distiller, {{G::Rice, kFull, 0, 21}, {G::Coal, kFull, 0, 5}}); break;
    case B::PigFarm: Production(world, n, S::PigFarmer, {{G::Grain, kFull, 0, 23}, {G::Water, kFull, 0, 27}}); break;
    case B::Farm: Production(world, n, S::Farmer, {}); break;
    case B::Fishery: Production(world, n, S::Fisher, {}); break;
    // Residences: the animation of the settler coming out.
    case B::SmallResidence: UpdateResidence(world, n, 113); break;
    case B::MediumResidence: UpdateResidence(world, n, 114); break;
    case B::LargeResidence: UpdateResidence(world, n, 115); break;
    case B::SulfurMine: Production(world, n, S::Miner, {{G::Rice, 5, 3, 20}, {G::Bread, 3, 5, 34}}); break;
    case B::Waterworks: Production(world, n, S::Waterworker, {}); break;
    case B::CatapultWorkshop: UpdateWorkshop(world, n, kCatapultWorkshop); break;
    case B::Shipyard: UpdateShipyard(world, n); break;
    case B::Harbour: UpdateTradeBuilding(world, n, false); break;
    case B::Marketplace: UpdateTradeBuilding(world, n, true); break;
    case B::Hospital:
        if (building.worker == 0) {
            RequestWorker(world, n, S::Healer);
        }
        break;
    case B::RiceFarm: Production(world, n, S::RiceFarmer, {}); break;
    case B::GemMine:
        Production(world, n, S::Miner, {{G::Fish, 4, 4, 42}, {G::Meat, 2, 6, 38}, {G::Bread, 2, 6, 33}});
        break;
    case B::Brewery: Production(world, n, S::Brewer, {{G::Grain, kFull, 0, 25}, {G::Water, kFull, 0, 29}}); break;
    case B::CharcoalBurner: Production(world, n, S::CharcoalBurner, {{G::Board, kFull, 0, 15}}); break;
    case B::PowderMaker: Production(world, n, S::PowderMaker, {{G::Coal, kFull, 0, 6}, {G::Sulfur, kFull, 0, 0}}); break;
    case B::Pyramid:
    case B::LargeTemple:
    case B::LargePagoda:
    case B::AmazonLargeTemple: UpdateTemple(world, n); break;
    case B::Sphinx: UpdateOffering(world, n, G::Beer, false); break;
    case B::SmallTemple: UpdateOffering(world, n, G::Wine, false); break;
    case B::SmallPagoda: UpdateOffering(world, n, G::RiceLiquor, true); break;
    case B::BallistaWorkshop: UpdateWorkshop(world, n, kBallistaWorkshop); break;
    case B::CannonWorkshop: UpdateWorkshop(world, n, kCannonWorkshop); break;
    case B::DonkeyFarm: Production(world, n, S::PigFarmer, {{G::Grain, kFull, 0, 24}, {G::Water, kFull, 0, 28}}); break;
    case B::LargeGong: UpdateWorkshop(world, n, kLargeGong); break;
    case B::Apiary:
        if (building.worker == 0) {
            RequestWorker(world, n, S::Beekeeper);
        }
        CollectOutput(world, n);
        break;
    case B::Meadery: Production(world, n, S::MeadMaker, {{G::Water, kFull, 0, 0}, {G::Honey, kFull, 0, 0}}); break;
    case B::Laboratory: UpdateLaboratory(world, n); break;
    case B::AmazonSmallTemple: UpdateOffering(world, n, G::Mead, false); break;
    case B::AmazonSulfurMine: Production(world, n, S::Miner, {{G::Honey, 5, 3, 0}, {G::Bread, 3, 5, 34}}); break;
    default: break;  // the lookout tower, the storehouse and types outside 2..56: nothing
    }
}

// Purpose: Phase 0: starts a new cycle of the economy update.
void StartEconomyCycle(CGameData* world) {
    Call(kCountDownTimers, world);
    Call(kRecountPlayerPopulation, world);
    for (std::uint32_t a = 1; a <= world->area_high_water; ++a) {
        Area& area = AreaAt(world, a);
        for (std::uint32_t good = 0; good < layout::kGoodsTypes; ++good) {
            area.requests_by_goods[good] = 0;
            area.buildings_a[good] = 0;
            area.buildings_b[good] = 0;
        }
    }
    // map_size / 16 as a signed division (re-read every time, as the
    // original's loop conditions do).
    auto blocks = [world]() -> std::uint32_t {
        const auto size = static_cast<std::int32_t>(world->map_size);
        return static_cast<std::uint32_t>((size + ((size >> 31) & 0xf)) >> 4);
    };
    for (std::uint32_t bx = 0; bx < blocks(); ++bx) {
        for (std::uint32_t by = 0; by < blocks(); ++by) {
            std::uint8_t* const fresh = world->presence_b[bx][by].player;
            std::uint8_t* const shown = world->presence_a[bx][by].player;
            for (std::uint32_t p = 0; p <= world->presence_max_player; ++p) {
                shown[p] = fresh[p];
                fresh[p] = 0;
            }
        }
    }
}

// Settler types that mark their team's presence (jump table
// 0x4e492c/0x4e4934): soldiers and the special units.
bool MarksPresence(SettlerType type) {
    using S = SettlerType;
    switch (type) {
    case S::Swordsman1: case S::Bowman1: case S::Pikeman1: case S::Geologist: case S::Thief:
    case S::Bowman2: case S::Pikeman2: case S::Swordsman2: case S::Bowman3: case S::Pikeman3:
    case S::Swordsman3: case S::Mage: case S::Type37: case S::Donkey: case S::Pioneer: case S::Catapult:
    case S::TradeShip: case S::Ferry:
        return true;
    default:
        return false;
    }
}

// Purpose: Phases 1..16: marks the military presence of one sixteenth of the
// settlers.
void MarkMilitaryPresence(CGameData* world, std::uint32_t phase) {
    for (std::uint32_t n = phase; n <= world->settler_high_water; n += 16) {
        const Settler& settler = SettlerAt(world, n);
        if (settler.owner == kFree || !MarksPresence(TypeOf(settler))) {
            continue;
        }
        if (Call(kIsOnMap, world, settler.x, settler.y) != 0) {
            std::uint8_t* const block = world->presence_b[std::uint32_t{settler.x} >> 4][std::uint32_t{settler.y} >> 4].player;
            block[PlayerAt(world, settler.owner).team] = 1;
        }
    }
}

// The consumer slots of goods `good` (the original's inline lists): each
// slot names a consuming building type (table 0x659264) and has a share
// weight per area.
std::uint32_t ConsumersOf(std::uint32_t good, std::uint32_t (&slots)[7]) {
    auto set = [&slots](std::initializer_list<std::uint32_t> list) {
        std::uint32_t k = 0;
        for (const std::uint32_t slot : list) {
            slots[k++] = slot;
        }
        return k;
    };
    // `good` can exceed 255 (DistributeAreaGoods' rank search is not
    // bounded), so the switch is on the full value.
    switch (good) {
    case Value(Good::Coal): return set({1, 2, 3, 4, 5, 6});
    case Value(Good::Iron): return set({7, 8, 9, 10, 11, 12, 47});
    case Value(Good::Board): return set({13, 14, 15, 43, 44, 45, 46});
    case Value(Good::Stone): return set({16, 17});
    case Value(Good::Log): return set({18, 19});
    case Value(Good::Sulfur): return set({20, 21});
    case Value(Good::Grain): return set({22, 23, 24, 25});
    case Value(Good::Water): return set({26, 27, 28, 29, 50});
    case Value(Good::Bread): return set({30, 31, 32, 33, 34});
    case Value(Good::Meat): return set({35, 36, 37, 38});
    case Value(Good::Fish): return set({39, 40, 41, 42});
    case Value(Good::Honey): return set({48, 49});
    default: return 0;
    }
}

// Whether request-list building q is of a consumer's building type:
// construction sites (0) or finished buildings of that type.
bool IsConsumerBuilding(CGameData* world, std::uint32_t q, std::uint32_t buildingType) {
    const Building& building = BuildingAt(world, q);
    if (buildingType == Value(BuildingType::None)) {
        return building.buildup != 0;
    }
    return building.buildup == 0 && std::uint32_t{building.art} == buildingType;
}

constexpr std::uint32_t kAnyConsumer = 0xffffffffu;  // no consumer slot chosen: every building

bool FitsConsumer(CGameData* world, std::uint32_t q, std::uint32_t requirement) {
    return requirement == kAnyConsumer || IsConsumerBuilding(world, q, requirement);
}

constexpr std::uint32_t kMaxCandidates = 100;

// Which consumer slot of `good` gets the next unit: the one with the lowest
// share (deliveries counted in +0x278, divided by its weight +0x245) that
// has a requesting building. Returns its building type (kAnyConsumer if
// none) and sets `chosenSlot`.
std::uint32_t ChooseConsumer(CGameData* world, Area& area, std::uint32_t good, std::uint32_t& chosenSlot) {
    std::uint32_t slots[7];
    const std::uint32_t slotCount = ConsumersOf(good, slots);
    std::uint32_t requirement = kAnyConsumer;
    std::uint32_t best = 0xffffffffu;
    for (std::uint32_t k = 0; k < slotCount; ++k) {
        const std::uint32_t slot = slots[k];
        std::uint32_t share = 0xffffffffu;
        const std::uint8_t weight = area.condition_enabled[slot];
        if (weight != 0) {
            share = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(std::uint32_t{area.counters_278[slot]} * 256 + 128) /
                static_cast<std::int32_t>(weight));
        }
        if (share >= best) {
            continue;
        }
        const std::uint32_t wantedType = g_consumerBuildingType[slot];
        bool found = false;
        for (std::uint32_t q = area.requests_by_goods[good]; q != 0 && !found;) {
            if (IsConsumerBuilding(world, q, wantedType)) {
                found = true;
            }
            q = Call(kBuildingInputWord2A, world, q, good);
        }
        if (found) {
            best = share;
            requirement = wantedType;
            chosenSlot = slot;
        }
    }
    return requirement;
}

// The nearest pile of `good` with units left for building `b` (stocked
// piles count their amount and claimed units), 0 if none; `score` gets the
// distance score.
std::uint32_t BestSource(CGameData* world, const Area& area, std::uint32_t good, const Building& b,
                         std::uint32_t& score) {
    std::uint32_t nearest = 0xffffffffu;
    std::uint32_t source = 0;
    for (std::uint32_t p = area.piles_by_goods[good]; p != 0; p = PileAt(world, p).next) {
        const Pile& pile = PileAt(world, p);
        if (pile.amount == pile.leaving) {
            continue;
        }
        std::uint32_t d = Distance(world, b.x, b.y, pile.x, pile.y);
        if (KindOf(pile) != PileKind::Output) {  // output piles: by distance only
            d = 2 * (d + 5 * std::uint32_t{pile.amount}) - 10 * (std::uint32_t{pile.leaving} + 1);
        }
        if (d < nearest) {
            nearest = d;
            source = p;
        }
    }
    score = nearest;
    return source;
}

// The nearest carrier of the area that is not marked surplus, 0 if none.
std::uint32_t NearestCarrier(CGameData* world, const Area& area, const Pile& source) {
    std::uint32_t nearest = 0xffffffffu;
    std::uint32_t carrier = 0;
    for (std::uint32_t s = area.settlers_by_type[Value(SettlerType::Carrier)]; s != 0; s = SettlerAt(world, s).next) {
        const Settler& settler = SettlerAt(world, s);
        if ((settler.flags & kSettlerFlagSurplus) != 0) {
            continue;
        }
        const std::uint32_t d = Distance(world, source.x, source.y, settler.x, settler.y);
        if (d < nearest) {
            nearest = d;
            carrier = s;
        }
    }
    return carrier;
}

// Sends `carrierNumber` (taken off the area's carrier list) to fetch one
// unit of `good` from pile `source` for building `target`.
void SendCarrier(CGameData* world, Area& area, std::uint32_t carrierNumber, std::uint32_t source, std::uint32_t target,
                 std::uint32_t good) {
    Call(kPileIncAmountLeaving, world, source);
    Settler& carrier = SettlerAt(world, carrierNumber);
    if (carrier.prev != 0) {
        SettlerAt(world, carrier.prev).next = carrier.next;
    } else {
        area.settlers_by_type[Value(SettlerType::Carrier)] = carrier.next;
    }
    if (carrier.next != 0) {
        SettlerAt(world, carrier.next).prev = carrier.prev;
    }
    Call(kSetSettlerTarget, world, carrierNumber, PileAt(world, source).x, PileAt(world, source).y);
    SetJob(carrier, SettlerJob::CarrierWalkToPile);
    carrier.counter = 0;
    carrier.building = static_cast<std::uint16_t>(target);
    carrier.good = static_cast<std::uint8_t>(good);
    Building& building = BuildingAt(world, target);
    if (building.buildup != 0) {
        if (good == Value(Good::Board)) {
            Call(kPileIncAmountComing, world, building.piles[0]);
        } else if (good == Value(Good::Stone)) {
            Call(kPileIncAmountComing, world, building.piles[1]);
        }
        return;
    }
    for (std::uint32_t slot = 0; slot < 3; ++slot) {
        if (good == PileAt(world, building.piles[slot]).good) {
            Call(kPileIncAmountComing, world, building.piles[slot]);
            return;
        }
    }
}

// Purpose: Phase 17 for one goods type of an area: picks the consumer slot
// whose share is lowest and that has a waiting building, then sends a
// carrier with one unit from the best pile to the requesting building that
// profits most (requested amount / distance). Coal gets a second delivery.
void DistributeGoods(CGameData* world, std::uint32_t areaNumber, std::uint32_t good) {
    Area& area = AreaAt(world, areaNumber);
    std::uint32_t chosenSlot = 0;
    const std::uint32_t requirement = ChooseConsumer(world, area, good, chosenSlot);
    std::uint32_t candidates[kMaxCandidates];
    std::uint32_t count = 0;
    for (std::uint32_t q = area.requests_by_goods[good]; q != 0; q = Call(kBuildingInputWord2A, world, q, good)) {
        if (count == kMaxCandidates) {
            break;
        }
        if (FitsConsumer(world, q, requirement)) {
            candidates[count++] = q;
        }
    }
    std::uint32_t scores[kMaxCandidates];
    std::uint32_t sources[kMaxCandidates];
    const std::uint32_t passes = good == Value(Good::Coal) ? 2 : 1;
    for (std::uint32_t pass = 0; pass < passes && (pass == 0 || count != 0); ++pass) {
        for (std::uint32_t k = 0; k < count; ++k) {
            const std::uint32_t q = candidates[k];
            if (q == 0) {
                scores[k] = 0;
                continue;
            }
            const Building& building = BuildingAt(world, q);
            std::uint32_t nearest = 0;
            const std::uint32_t source = BestSource(world, area, good, building, nearest);
            if (source == 0) {
                scores[k] = 0;
                continue;
            }
            const std::uint32_t requested = Call(kBuildingInputWord24, world, q, good) << 16;
            // Construction sites are not weighted by distance.
            scores[k] = building.buildup == 0 ? requested / nearest : requested;
            sources[k] = source;
        }
        std::uint32_t best = 0;
        std::uint32_t bestIndex = 0;
        std::uint32_t target = 0;
        std::uint32_t source = 0;
        for (std::uint32_t k = 0; k < count; ++k) {
            if (scores[k] > best) {
                best = scores[k];
                target = candidates[k];
                bestIndex = k;
                source = sources[k];
            }
        }
        if (best == 0) {
            count = 0;
            continue;
        }
        const std::uint32_t carrier = NearestCarrier(world, area, PileAt(world, source));
        if (carrier != 0) {
            SendCarrier(world, area, carrier, source, target, good);
            ++area.counters_278[chosenSlot];
        }
        // The chosen building leaves the candidates.
        const std::uint32_t last = count - 1;
        if (bestIndex != last && bestIndex <= count - 2) {
            for (std::uint32_t k = bestIndex; k < count - 1; ++k) {
                candidates[k] = candidates[k + 1];
            }
        }
        count = last;
    }
}

// The number of priority ranks (+0x1f6) a race's areas distribute.
std::uint32_t PriorityRanks(Race race, std::uint32_t previous) {
    switch (race) {
    case Race::Roman: return 26;
    case Race::Egyptian: return 27;
    case Race::Asian:
    case Race::Amazon: return 29;
    default: return previous;  // see the header
    }
}

// Purpose: Phase 17: goods distribution in every area (harbour and
// marketplace orders, then the goods by the area's priority ranks +0x1f6).
void DistributeAreaGoods(CGameData* world) {
    std::uint32_t ranks = 0;  // see the header: stale in the original for races above 3
    for (std::uint32_t a = 1; a <= world->area_high_water; ++a) {
        Area& area = AreaAt(world, a);
        if (area.player == kFree) {
            continue;
        }
        for (std::uint32_t good = 1; good < kGoodCount; ++good) {
            const std::uint32_t building = area.buildings_b[good];
            if (building != 0) {
                Call(kFetchGoodsToTradeBuilding, world, a, building, good);
            }
        }
        ranks = PriorityRanks(RaceOf(PlayerAt(world, area.player)), ranks);
        for (std::uint32_t rank = 1; rank <= ranks; ++rank) {
            std::uint32_t good = 1;
            while (area.settings4[good] != rank) {
                ++good;
            }
            DistributeGoods(world, a, good);
        }
        for (std::uint32_t good = 1; good < kGoodCount; ++good) {
            const std::uint32_t building = area.buildings_a[good];
            if (building != 0) {
                Call(kFetchGoodsToTradeBuilding, world, a, building, good);
            }
        }
    }
}

}  // namespace

using UpdateEconomyFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx);

// Purpose: The economy update of a logic step, one of 18 time slices: phase
// 0 starts a cycle (timers, population, request lists, military presence),
// phases 1..16 each handle a sixteenth of the settlers (military presence)
// and of the buildings (workers, goods requests, output), phase 17
// distributes goods in every area.
std::uint32_t __fastcall CGameData_UpdateEconomy(CGameData* world, void*) {
    if (world->counter_110a57c == 0) {
        StartEconomyCycle(world);
    }
    const std::uint32_t phase = world->counter_110a57c;
    if (phase >= 1 && phase <= kLastBuildingPhase) {
        if (phase <= world->settler_high_water) {
            MarkMilitaryPresence(world, phase);
        }
        for (std::uint32_t n = world->counter_110a57c; n <= world->building_high_water; n += 16) {
            const Building& building = BuildingAt(world, n);
            if (building.owner == kFree) {
                continue;
            }
            if (building.buildup != 0) {
                UpdateConstructionSite(world, n);
            } else {
                UpdateFinishedBuilding(world, n);
            }
        }
    }
    if (world->counter_110a57c == kDistributionPhase) {
        DistributeAreaGoods(world);
    }
    const std::uint32_t next = world->counter_110a57c + 1;
    world->counter_110a57c = next;
    if (next == kPhases) {
        world->counter_110a57c = 0;
    }
    return next;
}

S3RE_HOOK(0x004e05e0, CGameData_UpdateEconomy, UpdateEconomyFn, Original_CGameData_UpdateEconomy,
          CGameData_UpdateEconomy);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// One call per case at a chosen phase, on a world with the bounded lists the
// function walks itself (as the emulator spec's presets). The callees that
// walk lists, flood or change settlers are recording stubs (calls compared)
// returning small numbers derived from their arguments and position in the
// call sequence, with the side effects the function reads back;
// GoodsRequestPriority, TileDistance, IsOnMap, CountDownTimers and
// RecountPlayerPopulation run (our replacements, the same on both sides).

namespace {

constexpr std::size_t kTestWorld = offsetof(CGameData, buildings) + 128 * sizeof(Building);
constexpr std::uint32_t kTestRecords = 60;

std::uint32_t g_stubSequence = 0;

std::uint32_t Mix(std::uint32_t a, const std::uint32_t* args) {
    std::uint64_t h = 0x9e3779b97f4a7c15ull ^ (static_cast<std::uint64_t>(a) << 32) ^ g_stubSequence++;
    for (int k = 0; k < 4; ++k) {
        h = (h ^ args[k]) * 0xbf58476d1ce4e5b9ull;
        h ^= h >> 31;
    }
    return static_cast<std::uint32_t>(h >> 16);
}

// 0 with chance zero/256, else 1..n.
std::uint32_t SmallOrZero(std::uint32_t h, std::uint32_t n, std::uint32_t zero) {
    return (h & 0xff) < zero ? 0 : 1 + (h >> 8) % n;
}

template <std::uintptr_t Address, int N>
void Probe(test::CalleeStubs& stubs, std::uint32_t (*value)(std::uint32_t h) = nullptr) {
    stubs.Install<N, Address>([value](CGameData*, std::uint32_t, const std::uint32_t* args) {
        const std::uint32_t h = Mix(static_cast<std::uint32_t>(Address), args);
        return value != nullptr ? value(h) : h & 1;
    });
}

void InstallEconomyStubs(test::CalleeStubs& stubs) {
    Probe<kRequestSettlerForBuilding, 2>(stubs, [](std::uint32_t h) { return SmallOrZero(h, 60, 128); });
    Probe<kIsFreeOfCatapultsNearby, 2>(stubs,
                                       [](std::uint32_t h) { return static_cast<std::uint32_t>((h & 0x7f) != 0); });
    Probe<kBlockTile, 2>(stubs);
    Probe<kAddBuildingGoodsRequest, 4>(stubs);
    Probe<kSendNearestWorkerToBuilding, 3>(stubs, [](std::uint32_t h) { return SmallOrZero(h, 60, 205); });
    Probe<kNotifyLocalPlayer, 4>(stubs);
    Probe<kLeaveBuildingSlot, 1>(stubs);
    Probe<kClearSettlerJob, 1>(stubs);
    Probe<kSetTowerSoldierAnimation, 1>(stubs);
    Probe<kCountAreaSettlersOfClass0, 1>(stubs, [](std::uint32_t h) { return h % 60; });
    Probe<kCountAreaSettlersOfType, 2>(stubs, [](std::uint32_t h) { return h % 40; });
    Probe<kCreateSettler, 4>(stubs, [](std::uint32_t h) { return SmallOrZero(h, 60, 80); });
    Probe<kSetSettlerAnimation, 2>(stubs);
    stubs.Install<1, kTakeBuildingExtraTiles>([](CGameData* w, std::uint32_t, const std::uint32_t* args) {
        if ((Mix(static_cast<std::uint32_t>(kTakeBuildingExtraTiles), args) & 1) != 0) {
            BuildingAt(w, args[0]).workshop.extra_tiles = 0xfe;
        }
        return 0u;
    });
    Probe<kSetAreaBuildingSlotA, 2>(stubs);
    Probe<kSetAreaBuildingSlotB, 2>(stubs);
    Probe<kSendNearestSettlerToBuilding, 1>(stubs);
    Probe<kSendNearestSettlerToPile, 3>(stubs);
    stubs.Install<1, kTakeGoodFromPile>([](CGameData* w, std::uint32_t, const std::uint32_t* args) {
        const std::uint32_t h = Mix(static_cast<std::uint32_t>(kTakeGoodFromPile), args);
        if ((h & 1) != 0) {
            PileAt(w, args[0] & 0xffff).good = static_cast<std::uint8_t>((h >> 8) % 36);
        }
        return 0u;
    });
    Probe<kSubtractPlayerCounter, 3>(stubs);
    Probe<kPileIncAmountLeaving, 1>(stubs);
    Probe<kPileIncAmountComing, 1>(stubs);
    stubs.Install<1, kDispatchPileCarrier>([](CGameData* w, std::uint32_t, const std::uint32_t* args) {
        if ((Mix(static_cast<std::uint32_t>(kDispatchPileCarrier), args) & 1) != 0) {
            ++PileAt(w, args[0] & 0xffff).leaving;
        }
        return 0u;
    });
    Probe<kFetchGoodsToTradeBuilding, 3>(stubs);
    Probe<kBuildingInputWord2A, 2>(stubs, [](std::uint32_t h) { return SmallOrZero(h, kTestRecords, 115); });
    Probe<kBuildingInputWord24, 2>(stubs, [](std::uint32_t h) { return h % 31; });
    Probe<kSetSettlerTarget, 3>(stubs);
}

// Writes the same values into both worlds.
class Setup {
public:
    Setup(test::Context& ctx, test::WorldPair& worlds) : ctx_(ctx), worlds_(worlds) {}
    test::Rng& rng() { return ctx_.rng(); }
    template <typename F>
    void Both(F f) { worlds_.Both(f); }

    void Pile(std::uint32_t n, int good = -1) {
        const auto amount = static_cast<std::uint8_t>(rng().Below(9));
        const auto g = static_cast<std::uint8_t>(good >= 0 ? good : rng().Chance(1, 4) ? 0xff : 1 + rng().Below(35));
        const auto leaving = static_cast<std::uint8_t>(rng().Chance(1, 4)   ? amount
                                                       : rng().Chance(4, 5) ? rng().Below(amount + 1u)
                                                                            : rng().Below(256));
        const auto coming = static_cast<std::uint8_t>(rng().Chance(9, 10) ? rng().Below(9u - amount) : rng().Below(9));
        Both([&](CGameData* w) {
            layout::Pile& p = *(w->piles + n);
            p.good = g;
            p.amount = amount;
            p.leaving = leaving;
            p.coming = coming;
        });
    }

    void Building(std::uint32_t n) {
        static constexpr std::uint8_t kSpecial[] = {0xf, 0x10, 0x12, 0x21, 0x22, 0x13, 0xc, 0xd, 0x35, 0x1f,
                                                    0x2f, 0x30, 0x32, 0x20, 0x1a, 0x29, 0x2a, 0x2e, 0x36};
        static constexpr std::uint8_t kBytes[] = {0, 0xff, 0xfe, 0x10, 0x11, 0x12, 0x13, 0x20, 0x21, 0x22,
                                                  0x23, 0x24, 0x30, 0x33, 0x40, 0x43, 1, 2, 0xfa};
        const auto owner = static_cast<std::uint8_t>(rng().Chance(1, 12) ? 0xff : rng().Below(20));
        const bool site = rng().Chance(3, 10);
        // Sites use races 0..3 and types 1..0x38 (levelling lists exist for them).
        const auto race = static_cast<std::uint8_t>(rng().Below(4));
        auto art = static_cast<std::uint8_t>(1 + rng().Below(site ? 0x38 : 0x39));
        if (!site && rng().Chance(3, 10)) {
            art = kSpecial[rng().Below(sizeof(kSpecial))];
        }
        const auto x = static_cast<std::uint16_t>(20 + rng().Below(340));
        const auto y = static_cast<std::uint16_t>(20 + rng().Below(340));
        const auto dx = static_cast<std::uint16_t>(x + rng().Range(-2, 2));
        const auto dy = static_cast<std::uint16_t>(y + rng().Range(-2, 2));
        static constexpr std::uint8_t kBuildups[] = {1, 1, 2, 2, 3};
        const auto buildup = static_cast<std::uint8_t>(!site ? 0 : rng().Chance(5, 6) ? kBuildups[rng().Below(5)] : rng().Below(256));
        std::uint8_t bytes[0x21] = {};
        for (std::uint32_t k = 0x16; k < 0x21; ++k) {
            bytes[k] = static_cast<std::uint8_t>(rng().Chance(3, 5) ? rng().Below(9) : kBytes[rng().Below(sizeof(kBytes))]);
        }
        bytes[0x18] = static_cast<std::uint8_t>(rng().Next32());
        const auto workX = static_cast<std::uint16_t>(rng().Below(0x50));
        const auto workY = static_cast<std::uint16_t>(rng().Next32());
        const auto dock = static_cast<std::uint16_t>(rng().Chance(3, 20) ? 0xffff : rng().Below(6));
        // Water buildings and the shipyard: states that fix their wanted inputs.
        static constexpr std::uint8_t kStates[] = {0x10, 0x20, 0x11, 0x21, 0x12, 0x22, 0x13, 0x23,
                                                   0x30, 0x31, 0x33, 0x34, 0x40, 0x43, 0x44, 0x2f};
        if ((art == 0x1f || art == 0x2f || art == 0x30 || art == 0x32 || art == 0x20) && rng().Chance(7, 10)) {
            bytes[0x1f] = kStates[rng().Below(sizeof(kStates))];
            bytes[0x20] = static_cast<std::uint8_t>(rng().Chance(7, 10) ? 0xff : rng().Below(9));
        }
        const std::uint32_t maxHp = art == 0x10 ? 500 : art == 0x12 ? 1000 : 250;
        const auto hp = static_cast<std::uint16_t>(rng().Chance(7, 10) ? maxHp - rng().Below(2) : rng().Next32());
        std::uint16_t piles[4];
        for (std::uint16_t& p : piles) {
            p = static_cast<std::uint16_t>(1 + rng().Below(300));
            Pile(p);
        }
        std::uint16_t worker = static_cast<std::uint16_t>(rng().Chance(7, 20) ? 0 : 1 + rng().Below(kTestRecords));
        Both([&](CGameData* w) {
            layout::Building& b = *(w->buildings + n);
            b.owner = owner;
            b.x = x;
            b.y = y;
            b.art = art;
            b.race = race;
            b.door_x = dx;
            b.door_y = dy;
            b.buildup = buildup;
            b.digger_slots = bytes[0x16];
            b.builder_slots = bytes[0x17];
            b.request_flags = bytes[0x18];
            for (int k = 0; k < 8; ++k) {
                b.kind_bytes[k] = bytes[0x19 + k];
            }
            b.work_x = workX;
            b.work_y = workY;
            b.dock_direction = dock;
            b.hit_points = hp;
            b.piles[0] = piles[0];
            b.piles[1] = piles[1];
            b.piles[2] = piles[2];
            b.pile_ring = piles[3];
        });
        if (art == 0x21 || art == 0x22) {
            std::uint16_t ring[6] = {piles[3]};
            for (int k = 1; k < 6; ++k) {
                ring[k] = static_cast<std::uint16_t>(301 + rng().Below(300));
            }
            for (int k = 0; k < 6; ++k) {
                Pile(ring[k], rng().Chance(7, 20) ? 0xff : static_cast<int>(1 + rng().Below(35)));
                Both([&](CGameData* w) { (w->piles + ring[k])->next = ring[(k + 1) % 6]; });
            }
            const auto trade = static_cast<std::uint16_t>(1 + rng().Below(20));
            static constexpr std::uint8_t kKinds[] = {0, 1, 2, 2, 1, 3};
            const std::uint8_t kind = kKinds[rng().Below(6)];
            const auto route = static_cast<std::uint16_t>(rng().Chance(1, 10) ? 0xffff : rng().Below(384));
            std::uint8_t offered[0x24], coming[0x24];
            for (std::uint32_t g = 1; g < 0x24; ++g) {
                offered[g] = static_cast<std::uint8_t>(rng().Chance(3, 10) ? 0 : rng().Below(4));
                coming[g] = static_cast<std::uint8_t>(rng().Chance(3, 10) ? offered[g] : rng().Below(4));
            }
            Both([&](CGameData* w) {
                (w->buildings + n)->trade = trade;
                layout::Trade& t = *(w->trades + trade);
                t.mode = kind;
                t.route_x[3] = route;
                for (std::uint32_t g = 1; g < 0x24; ++g) {
                    t.ordered[g] = offered[g];
                    t.coming[g] = coming[g];
                }
            });
        }
        if ((art == 0xf || art == 0x10 || art == 0x12) && !site) {
            static constexpr std::uint8_t kSoldiers[] = {5, 0x20, 0x23, 8, 0x1e, 0x21, 0xf, 0x1f, 0x22};
            const std::uint32_t length = rng().Below(6);
            std::vector<std::uint16_t> members;
            while (members.size() < length) {
                // Settlers of its own (chains of two buildings must not share any).
                const auto s = static_cast<std::uint16_t>(100 + 8 * n + rng().Below(8));
                bool used = false;
                for (const std::uint16_t m : members) {
                    used = used || m == s;
                }
                if (!used) {
                    members.push_back(s);
                }
            }
            worker = members.empty() ? 0 : members[0];
            for (std::size_t k = 0; k < members.size(); ++k) {
                const auto type = static_cast<std::uint8_t>(rng().Chance(17, 20) ? kSoldiers[rng().Below(9)] : rng().Below(0x30));
                const std::uint16_t next = k + 1 < members.size() ? members[k + 1] : 0;
                Both([&](CGameData* w) {
                    (w->settlers + members[k])->art = type;
                    (w->settlers + members[k])->next = next;
                });
            }
        }
        Both([&](CGameData* w) { (w->buildings + n)->worker = worker; });
        // The tiles around the building (soldiers near, piles next to the door).
        for (std::int32_t ox = -4; ox <= 4; ++ox) {
            for (std::int32_t oy = -4; oy <= 4; ++oy) {
                const auto s = static_cast<std::uint16_t>(rng().Chance(1, 2) ? 0 : 1 + rng().Below(kTestRecords));
                const auto pile = static_cast<std::uint16_t>(rng().Chance(1, 5) ? 0 : 1 + rng().Below(300));
                Both([&](CGameData* w) {
                    w->tiles[x + ox][y + oy].settler = s;
                    w->tiles[dx + ox][dy + oy].object_number = pile;
                });
                if (pile != 0) {
                    Pile(pile);
                }
            }
        }
        const auto doorSettler = static_cast<std::uint16_t>(rng().Chance(3, 5) ? 0 : 1 + rng().Below(kTestRecords));
        const auto area = static_cast<std::uint16_t>(1 + rng().Below(8));
        const auto share = static_cast<std::uint8_t>(rng().Below(120));
        const auto any = static_cast<std::uint8_t>(rng().Below(2));
        std::uint8_t counters[3];
        for (std::uint8_t& c : counters) {
            static constexpr std::uint8_t kCounters[] = {0, 0, 1, 2};
            c = kCounters[rng().Below(4)];
        }
        Both([&](CGameData* w) {
            w->tiles[dx][dy].settler = doorSettler;
            w->tiles[dx][dy].area = area;
            layout::Area& a = *(w->areas + area);
            a.settings_extra[0] = share;
            a.setting_241 = any;
            for (int k = 0; k < 3; ++k) {
                a.counters_242[k] = counters[k];
            }
        });
    }

    // `firstUsed`: no used area came before it. Its player is one of 0..18,
    // whose races are 0..3 (for another race the original's highest rank
    // would be uninitialized stack, see the header); player 19 may have
    // races 4..7.
    void AreaRecord(std::uint32_t area, bool& firstUsed) {
        const auto player = static_cast<std::uint8_t>(rng().Chance(3, 20) ? 0xff : rng().Below(firstUsed ? 19 : 20));
        if (player != 0xff) {
            firstUsed = false;
        }
        Both([&](CGameData* w) { (w->areas + area)->player = player; });
        std::uint8_t ranks[36];
        for (std::uint32_t g = 1; g < 36; ++g) {
            ranks[g] = static_cast<std::uint8_t>(g);
        }
        for (std::uint32_t g = 35; g > 1; --g) {
            const std::uint32_t k = 1 + rng().Below(g);
            const std::uint8_t t = ranks[g];
            ranks[g] = ranks[k];
            ranks[k] = t;
        }
        for (std::uint32_t g = 1; g < 36; ++g) {
            const auto b = static_cast<std::uint16_t>(rng().Chance(4, 5) ? 0 : 1 + rng().Below(kTestRecords));
            const auto a = static_cast<std::uint16_t>(rng().Chance(4, 5) ? 0 : 1 + rng().Below(kTestRecords));
            const auto r = static_cast<std::uint16_t>(rng().Chance(3, 10) ? 0 : 1 + rng().Below(kTestRecords));
            // Up to four piles of the goods near (200, 200), at distances that
            // are no multiples of 5 (no score of 0, the original divides by it).
            const std::uint32_t count = rng().Below(5);
            std::uint16_t members[4];
            for (std::uint32_t k = 0; k < count; ++k) {
                members[k] = static_cast<std::uint16_t>(1000 + 300 * area + 8 * g + 2 * k + rng().Below(2));
                static constexpr std::uint16_t kDistances[] = {1, 2, 3, 4, 6, 7, 8, 9, 11, 12, 13, 14};
                const std::uint16_t d = kDistances[rng().Below(12)];
                const auto px = static_cast<std::uint16_t>(rng().Chance(1, 2) ? 200 + d : 200 - d);
                static constexpr std::uint8_t kArts[] = {0, 0xfe, 0xfd, 1};
                const std::uint8_t art = rng().Chance(1, 2) ? 0xff : kArts[rng().Below(4)];
                Pile(members[k], static_cast<int>(g));
                Both([&](CGameData* w) {
                    layout::Pile& p = *(w->piles + members[k]);
                    p.x = px;
                    p.y = 200;
                    p.art = art;
                });
            }
            for (std::uint32_t k = 0; k < count; ++k) {
                const std::uint16_t next = k + 1 < count ? members[k + 1] : 0;
                Both([&](CGameData* w) { (w->piles + members[k])->next = next; });
            }
            const std::uint16_t head = count != 0 ? members[0] : 0;
            Both([&](CGameData* w) {
                layout::Area& ar = *(w->areas + area);
                ar.settings4[g] = ranks[g];
                ar.buildings_b[g] = b;
                ar.buildings_a[g] = a;
                ar.requests_by_goods[g] = r;
                ar.piles_by_goods[g] = head;
            });
        }
        for (std::uint32_t t = 0; t < 0x33; ++t) {
            const auto weight = static_cast<std::uint8_t>(rng().Chance(1, 5) ? 0 : 1 + rng().Below(255));
            const auto counter = static_cast<std::uint16_t>(rng().Below(300));
            Both([&](CGameData* w) {
                (w->areas + area)->condition_enabled[t] = weight;
                (w->areas + area)->counters_278[t] = counter;
            });
        }
        const std::uint32_t carriers = rng().Below(6);
        std::uint16_t list[6];
        for (std::uint32_t k = 0; k < carriers; ++k) {
            list[k] = static_cast<std::uint16_t>(1 + 10 * k + rng().Below(10));
        }
        for (std::uint32_t k = 0; k < carriers; ++k) {
            const auto flags = static_cast<std::uint8_t>(rng().Chance(1, 5) ? 0x80 : rng().Below(0x80));
            const auto sx = static_cast<std::uint16_t>(150 + rng().Below(100));
            const auto sy = static_cast<std::uint16_t>(150 + rng().Below(100));
            Both([&](CGameData* w) {
                layout::Settler& s = *(w->settlers + list[k]);
                s.prev = k == 0 ? 0 : list[k - 1];
                s.next = k + 1 < carriers ? list[k + 1] : 0;
                s.flags = flags;
                s.x = sx;
                s.y = sy;
            });
        }
        Both([&](CGameData* w) { (w->areas + area)->settlers_by_type[0] = carriers != 0 ? list[0] : 0; });
    }

    // One case; returns the phase.
    std::uint32_t Case() {
        const std::uint32_t r = rng().Below(100);
        const std::uint32_t phase = r < 12 ? 0 : r < 35 ? 17 : 1 + rng().Below(16);
        static constexpr std::uint32_t kMaxPlayers[] = {0, 1, 3, 19};
        const std::uint32_t maxPlayer = kMaxPlayers[rng().Below(4)];
        Both([&](CGameData* w) {
            w->counter_110a57c = phase;
            w->map_size = 384;
            w->presence_max_player = maxPlayer;
            w->settler_high_water = 0;
            w->building_high_water = 0;
            w->area_high_water = 0;
        });
        for (std::uint32_t p = 0; p < 20; ++p) {
            const auto index = static_cast<std::int32_t>(rng().Below(20));
            Both([&](CGameData* w) { w->players[p].team = index; });
        }
        if (phase == 0) {
            static constexpr std::int32_t kSizes[] = {0, 15, 16, 17, 33, 48, 64};
            const std::int32_t size = rng().Chance(1, 4) ? static_cast<std::int32_t>(rng().Below(70)) : kSizes[rng().Below(7)];
            const std::uint32_t areas = rng().Below(6);
            const std::uint32_t settlers = rng().Below(40);
            const std::uint32_t buildings = rng().Below(40);
            Both([&](CGameData* w) {
                w->map_size = static_cast<std::uint32_t>(size);
                w->area_high_water = areas;
                w->settler_high_water = settlers;
                w->building_high_water = buildings;
            });
        } else if (phase == 17) {
            const std::uint32_t areas = rng().Below(5);
            Both([&](CGameData* w) { w->area_high_water = areas; });
            for (std::uint32_t p = 0; p < 20; ++p) {
                const auto race = static_cast<std::int32_t>(p == 19 && rng().Chance(1, 2) ? 4 + rng().Below(4) : rng().Below(4));
                Both([&](CGameData* w) { w->players[p].race = race; });
            }
            bool firstUsed = true;
            for (std::uint32_t a = 1; a <= areas; ++a) {
                AreaRecord(a, firstUsed);
            }
            static constexpr std::uint8_t kConsumers[] = {0, 0xb, 0xa, 0xd, 0xc, 0x16, 0x28, 0x20, 0x1f, 0x2f, 0x30, 0x27, 0x4,
                                                          0x1d, 0x11, 0x17, 0x31, 0x26, 0x14, 0x7, 0x9, 0x8, 0x25, 0x32, 0x38, 0x34};
            for (std::uint32_t n = 1; n <= kTestRecords; ++n) {
                const bool site = rng().Chance(3, 10);
                const auto buildup = static_cast<std::uint8_t>(site ? 1 + rng().Below(3) : 0);
                const auto art = static_cast<std::uint8_t>(rng().Chance(4, 5) ? kConsumers[rng().Below(sizeof(kConsumers))] : rng().Below(0x39));
                std::uint8_t goods[3];
                for (std::uint8_t& g : goods) {
                    g = static_cast<std::uint8_t>(1 + rng().Below(35));
                }
                Both([&](CGameData* w) {
                    layout::Building& b = *(w->buildings + n);
                    b.x = 200;
                    b.y = 200;
                    b.buildup = buildup;
                    b.art = art;
                    for (std::uint32_t k = 0; k < 3; ++k) {
                        b.piles[k] = static_cast<std::uint16_t>(3000 + n * 3 + k);
                        (w->piles + b.piles[k])->good = goods[k];
                    }
                });
            }
        } else {
            const std::uint32_t settlers = rng().Chance(9, 10) ? phase + 16 * rng().Below(4) + rng().Below(16) : rng().Below(phase);
            const std::uint32_t buildings = rng().Chance(9, 10) ? phase + 16 * rng().Below(6) + rng().Below(16) : rng().Below(phase);
            Both([&](CGameData* w) {
                w->settler_high_water = settlers;
                w->building_high_water = buildings;
            });
            static constexpr std::uint8_t kPresence[] = {5, 8, 0xf, 0x16, 0x17, 0x1e, 0x1f, 0x20, 0x21,
                                                         0x22, 0x23, 0x24, 0x25, 0x27, 0x28, 0x29, 0x2b, 0x2c};
            for (std::uint32_t n = phase; n <= settlers; n += 16) {
                const auto owner = static_cast<std::uint8_t>(rng().Chance(3, 20) ? 0xff : rng().Below(20));
                const auto art = static_cast<std::uint8_t>(rng().Chance(7, 10) ? kPresence[rng().Below(18)] : rng().Below(0x30));
                const auto x = static_cast<std::uint16_t>(rng().Below(400));
                const auto y = static_cast<std::uint16_t>(rng().Below(400));
                Both([&](CGameData* w) {
                    layout::Settler& s = *(w->settlers + n);
                    s.owner = owner;
                    s.art = art;
                    s.x = x;
                    s.y = y;
                });
            }
            const std::uint32_t rings = rng().Below(14);
            for (std::uint32_t k = 0; k <= rings; ++k) {
                const std::int32_t dx = rng().Range(-3, 3);
                const std::int32_t dy = rng().Range(-3, 3);
                const std::int32_t distance = k < rings ? rng().Range(-2, 9) : rng().Range(10, 20);
                Both([&](CGameData* w) {
                    w->offsets[k].dx = dx;
                    w->offsets[k].dy = dy;
                    w->offsets[k].distance = distance;
                });
            }
            for (std::uint32_t n = phase; n <= buildings; n += 16) {
                Building(n);
            }
        }
        return phase;
    }

private:
    test::Context& ctx_;
    test::WorldPair& worlds_;
};

constexpr std::size_t kTallyCount = 20 * 17;

void CompareUpdateEconomy(test::Context& ctx, UpdateEconomyFn candidate, int cases) {
    // The tallies from PlayerTally::unk0c on, as int32 (to 12 bytes past the end, as before).
    std::int32_t* const kTallies = &g_playerTallies[0].unk0c;
    test::WorldPair worlds(ctx, kTestWorld);
    test::CalleeStubs stubs;
    InstallEconomyStubs(stubs);
    DebugLogRecorder logs;
    Setup setup(ctx, worlds);
    std::vector<std::int32_t> saved(kTallies, kTallies + kTallyCount);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t phase = setup.Case();
        const std::vector<std::int32_t> before(kTallies, kTallies + kTallyCount);
        stubs.SetWorld(worlds.original());
        g_stubSequence = 0;
        const std::uint32_t expected = Original_CGameData_UpdateEconomy(worlds.original(), nullptr);
        const std::vector<test::StubCall> expectedCalls = stubs.Take();
        const std::vector<DebugLogCall> expectedLogs = logs.Take();
        const std::vector<std::int32_t> expectedTallies(kTallies, kTallies + kTallyCount);
        std::copy(before.begin(), before.end(), kTallies);
        stubs.SetWorld(worlds.ours());
        g_stubSequence = 0;
        const std::uint32_t actual = candidate(worlds.ours(), nullptr);
        const std::vector<test::StubCall> actualCalls = stubs.Take();
        const std::vector<DebugLogCall> actualLogs = logs.Take();
        const std::vector<std::int32_t> actualTallies(kTallies, kTallies + kTallyCount);
        const bool checkpoint = i % 16 == 15 || i == cases - 1;
        if (!S3RE_EXPECT_EQ(expected, actual) || !S3RE_EXPECT(expectedCalls.size() == actualCalls.size()) ||
            !S3RE_EXPECT(expectedCalls == actualCalls) || !S3RE_EXPECT(expectedLogs == actualLogs) ||
            !S3RE_EXPECT(expectedTallies == actualTallies) ||
            !worlds.SameField(ctx, [](CGameData* w) -> auto& { return w->counter_110a57c; }) ||
            (checkpoint && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 15 before it): phase %u, %u/%u stub calls", i,
                     phase, static_cast<unsigned>(expectedCalls.size()), static_cast<unsigned>(actualCalls.size()));
            break;
        }
    }
    std::copy(saved.begin(), saved.end(), kTallies);
}

// Wrong: every building counts as wanting inputs (request flag bit 0).
std::uint32_t __fastcall WrongUpdateEconomy(CGameData* world, void* edx) {
    std::vector<std::uint8_t> flags;
    for (std::uint32_t n = 0; n < 64; ++n) {
        flags.push_back(world->buildings[n].request_flags);
        world->buildings[n].request_flags |= 1;
    }
    const std::uint32_t result = CGameData_UpdateEconomy(world, edx);
    for (std::uint32_t n = 0; n < 64; ++n) {
        world->buildings[n].request_flags = flags[n];
    }
    return result;
}

}  // namespace

S3RE_TEST(CGameData_UpdateEconomy_MatchesOriginal) {
    CompareUpdateEconomy(ctx, &CGameData_UpdateEconomy, 4000);
}

S3RE_TEST(CGameData_UpdateEconomy_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareUpdateEconomy(probe, &WrongUpdateEconomy, 400);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
