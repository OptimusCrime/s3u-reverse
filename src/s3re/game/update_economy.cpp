// GameWorld::UpdateEconomy (0x004e05e0; was "UpdatePhase2"): the second
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
//   high water: owned settlers of types 5 8 F 16 17 1E..25 27 28 29 2B 2C
//   (jump table 0x4e492c/0x4e4934 on type - 5) that are IsOnMap mark their
//   player's presence index in block (x/16, y/16) of map B. Then buildings
//   number = phase, phase+16, ... up to the building high water (re-read):
//   construction sites (buildup +0x21 != 0, UpdateConstructionSite) and
//   finished buildings by type (jump table 0x4e495c on type - 2, 0x37
//   entries), see the handlers below.
// - Phase 17: for every used area, FetchGoodsToTradeBuilding for the slot
//   list B, then the goods distribution by priority rank (DistributeGoods),
//   then FetchGoodsToTradeBuilding for slot list A.
//
// Every callee is called through its S3.EXE address (in the game Detours
// sends the call to our replacement, which is counted; tests can stub them).
//
// Building fields as used here: layout::Building (re/structs.md "Building"):
//   the worker or garrison (+0x08), pile_ring (the output pile or the first
//   of six ring piles of trade buildings 0x21/0x22), piles[3] (inputs), the
//   site slot bits +0x16/+0x17, request_flags (bit 0 "inputs wanted", bits
//   1..4 override the player's request counters), the per-kind bytes
//   +0x19..+0x20 (views site, military, water, trading, temple, residence,
//   recruiting) and hit_points (military: up to 250/500/1000, S3.EXE 0x659664).
//
// Known differences (only reachable with unrealistic data):
// - Trade buildings 0x21/0x22 build a per-goods "wanted" table of 0x24
//   dwords on the stack but index it with pile goods and the cursor +0x1b up
//   to 0xff; entries 0x24..0xff are stale stack of the original (ours: 0).
//   The outcome is the same unless a ring pile holds good 0 (a free pile
//   record) and the cursor passes 0x23 (see TradeWantedSlots).
// - Phase 17's highest priority rank is set only for races 0..3; for another
//   race the original keeps the previous area's value, or (first area of the
//   call) an uninitialized stack slot; ours starts at 0 (no ranks).
// - Phase 17 divides by the best pile's distance score, which is 0 if a
//   free-standing pile lies at the building's position: an integer division
//   by zero in both.

#include "game/debug_log.h"
#include "game/replacements.h"
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
constexpr std::uintptr_t kIsFreeOfSettlerType29Nearby = 0x005199e0;
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

// S3.EXE data.
const std::int32_t* const kSettlerTypeClass = reinterpret_cast<const std::int32_t*>(0x00658f64);
const std::uint32_t* const kMilitaryMaxHitPoints = reinterpret_cast<const std::uint32_t*>(0x00659664);  // 0xf 0x10 0x12
const std::uint32_t* const kManaPerOffering = reinterpret_cast<const std::uint32_t*>(0x00730ef4);       // 1
const std::uint32_t* const kSmithInputFull = reinterpret_cast<const std::uint32_t*>(0x00730f04);       // 8
const std::uint32_t* const kConsumerBuildingType = reinterpret_cast<const std::uint32_t*>(0x00659264);
constexpr std::uintptr_t kSiteLevellingTable = 0x006aa170;  // int32 lists per (race * 0x39 + type), 0x960 bytes each
constexpr std::uintptr_t kShipyardTable30 = 0x00659670;     // states 0x30..0x33: 3 bytes at +0/+4/+8, stride 0x20
constexpr std::uintptr_t kShipyardTable40 = 0x00659680;     // states 0x40..0x43
constexpr std::uintptr_t kFormatTempleExtraTiles = 0x006a5a6c;  // "Gong bauen - nach SecureProduction (%u..."
std::int32_t* const kPlayerTallies = reinterpret_cast<std::int32_t*>(0x007acfe0);  // int32[20][17], g_playerCounters7acfe0

constexpr std::uint8_t kFree = 0xff;
constexpr std::uint32_t kPhases = 18;
constexpr std::uint32_t kLastBuildingPhase = 16;
constexpr std::uint32_t kDistributionPhase = 17;

template <typename... A>
std::uint32_t Call(std::uintptr_t address, GameWorld* world, A... args) {
    using Fn = std::uint32_t(__fastcall*)(GameWorld*, void*, A...);
    return reinterpret_cast<Fn>(address)(world, nullptr, args...);
}

std::uint32_t U(std::uint32_t v) { return v; }

Building& BuildingAt(GameWorld* w, std::uint32_t n) { return *(w->buildings + n); }
Settler& SettlerAt(GameWorld* w, std::uint32_t n) { return *(w->settlers + n); }
Pile& PileAt(GameWorld* w, std::uint32_t n) { return *(w->piles + n); }
Player& PlayerAt(GameWorld* w, std::uint32_t n) { return *(w->players + n); }
Area& AreaAt(GameWorld* w, std::uint32_t n) { return *(w->areas + n); }
std::int32_t SettlerClass(std::uint32_t type) { return kSettlerTypeClass[type]; }
Tile& TileAt(GameWorld* w, std::uint32_t x, std::uint32_t y) { return TileAtOffset(w, TileOffset(x, y)); }
Tile& DoorTile(GameWorld* w, const Building& b) { return TileAt(w, b.door_x, b.door_y); }
// The pile on the tile `delta` bytes from the door tile (tile word +0x2).
std::uint32_t PileNextToDoor(GameWorld* w, const Building& b, std::uint32_t delta) {
    return TileAtOffset(w, TileOffset(b.door_x, b.door_y) + delta).object_number;
}
constexpr std::uint32_t kStepX = kTileRowBytes;
constexpr std::uint32_t kStepY = kTileBytes;

// Building bytes +0x19..+0x20: the per-kind views of layout::Building
// (site, military, water, trading, temple, residence, recruiting).
std::uint8_t& Class1Coming(Building& b) { return b.military.class1_coming; }
std::uint8_t& Class1Wanted(Building& b) { return b.military.class1_wanted; }
std::uint8_t& Class2Coming(Building& b) { return b.military.class2_coming; }
std::uint8_t& Class2Wanted(Building& b) { return b.military.class2_wanted; }
std::uint8_t& Class3Coming(Building& b) { return b.military.class3_coming; }
std::uint8_t& Class3Wanted(Building& b) { return b.military.class3_wanted; }
std::uint8_t& SiteDiggers(Building& b) { return b.site.diggers; }
std::uint8_t& SiteBuilders(Building& b) { return b.site.builders; }

// --- callee wrappers ------------------------------------------------------

std::uint32_t RequestSettler(GameWorld* w, std::uint32_t n, std::uint32_t type) {
    return Call(kRequestSettlerForBuilding, w, n, type);
}
std::uint32_t Priority(GameWorld* w, std::uint32_t amount, std::uint32_t coming) {
    return Call(kGoodsRequestPriority, w, amount, coming);
}
void AddRequest(GameWorld* w, std::uint32_t n, std::uint32_t good, std::uint32_t amount, std::uint32_t condition) {
    Call(kAddBuildingGoodsRequest, w, n, good, amount, condition);
}
void Dispatch(GameWorld* w, std::uint32_t pile) { Call(kDispatchPileCarrier, w, pile); }
std::uint32_t Distance(GameWorld* w, std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1) {
    return Call(kTileDistance, w, x0, y0, x1, y1);
}

// --- shared building parts -------------------------------------------------

// No worker yet: ask for one of `type`; he becomes the building's worker.
void RequestWorker(GameWorld* w, std::uint32_t n, std::uint32_t type) {
    const std::uint32_t settler = RequestSettler(w, n, type);
    if (settler != 0) {
        BuildingAt(w, n).worker = static_cast<std::uint16_t>(settler);
    }
}

// Input slot `slot` wants goods `good` while amount + coming != `full` and
// the building's input flag is set; the priority counts the amount + `add`.
void RequestInput(GameWorld* w, std::uint32_t n, std::uint32_t slot, std::uint32_t good, std::uint32_t full,
                  std::uint32_t add, std::uint32_t condition) {
    Building& b = BuildingAt(w, n);
    const Pile& p = PileAt(w, b.piles[slot]);
    const std::uint32_t amount = p.amount;
    const std::uint32_t coming = p.coming;
    if (amount + coming != full && (b.request_flags & 1) != 0) {
        AddRequest(w, n, good, Priority(w, amount + add, coming), condition);
    }
}

// Input slot `slot` (water buildings, shipyard) wants goods while amount +
// coming is below the building's wanted amount `limit` (and not 8).
void RequestInputBelow(GameWorld* w, std::uint32_t n, std::uint32_t slot, std::uint32_t good, std::uint32_t limit,
                       std::uint32_t condition) {
    const Pile& p = PileAt(w, BuildingAt(w, n).piles[slot]);
    const std::uint32_t amount = p.amount;
    const std::uint32_t coming = p.coming;
    const std::uint32_t sum = amount + coming;
    if (sum != 8 && sum < limit) {
        AddRequest(w, n, good, Priority(w, amount, coming), condition);
    }
}

// The output pile (+0xe) gets a carrier once five or more units wait in it.
void CollectOutput(GameWorld* w, std::uint32_t n) {
    const std::uint32_t pile = BuildingAt(w, n).pile_ring;
    const Pile& p = PileAt(w, pile);
    if (static_cast<std::int32_t>(U(p.amount) - U(p.leaving)) >= 5) {
        Dispatch(w, pile);
    }
}

struct InputSpec {
    std::uint32_t good, full, add, condition;
};

// The common production building: inputs while it has a worker, else a
// worker request; then the output pile.
void Production(GameWorld* w, std::uint32_t n, std::uint32_t workerType, std::initializer_list<InputSpec> inputs) {
    if (BuildingAt(w, n).worker != 0) {
        std::uint32_t slot = 0;
        for (const InputSpec& input : inputs) {
            RequestInput(w, n, slot++, input.good, input.full, input.add, input.condition);
        }
    } else {
        RequestWorker(w, n, workerType);
    }
    CollectOutput(w, n);
}

// --- construction sites ------------------------------------------------------

// The site's slot bits `bits` (+0x16 diggers / +0x17 builders): for every
// slot below `count` whose bit is clear, a settler of `type` is requested.
void RequestSiteWorkers(GameWorld* w, std::uint32_t n, std::uint8_t Building::*bits, std::uint8_t& (*count)(Building&),
                        std::uint32_t type) {
    Building& b = BuildingAt(w, n);
    for (std::uint32_t k = 0; k < 8; ++k) {
        const std::uint8_t bit = static_cast<std::uint8_t>(1u << k);
        if ((b.*bits & bit) == 0 && count(b) >= k + 1 && RequestSettler(w, n, type) != 0) {
            b.*bits = static_cast<std::uint8_t>(b.*bits + bit);
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
const std::int32_t* SiteTileList(const Building& b) {
    const auto* entry = reinterpret_cast<const std::int32_t*>(
        kSiteLevellingTable + (U(b.race) * 0x39 + U(b.art)) * 0x960);
    const std::int32_t* p = entry + 1;
    if (entry[0] != static_cast<std::int32_t>(0x80000000u)) {
        std::int32_t v;
        do {
            v = *p++;
        } while (v != static_cast<std::int32_t>(0x80000000u));
    }
    return p;
}

constexpr std::int32_t kEndBlockList = static_cast<std::int32_t>(0x80000001u);
constexpr std::int32_t kSkipEntry = static_cast<std::int32_t>(0x80000002u);
constexpr std::int32_t kEndClearList = static_cast<std::int32_t>(0x80000003u);

// Purpose: Moves a building site along: asks for diggers while it is being
// levelled and for builders once it has material, finishes the levelling
// when no ship blocks the footprint (blocking the tiles and clearing their
// objects), and requests the two building materials.
void UpdateConstructionSite(GameWorld* w, std::uint32_t n) {
    Building& b = BuildingAt(w, n);
    {
        const std::uint8_t buildup = b.buildup;
        const std::uint8_t flags = b.request_flags;
        if (((flags & 0x10) != 0 || PlayerAt(w, b.owner).settler_lists[4] == 0) && buildup == 1 &&
            (flags & 1) != 0) {
            RequestSiteWorkers(w, n, &Building::digger_slots, &SiteDiggers, 1);
        }
    }
    {
        const std::uint8_t flags = b.request_flags;
        if (((flags & 8) != 0 || PlayerAt(w, b.owner).settler_lists[3] == 0) &&
            U(PileAt(w, b.piles[1]).amount) + U(PileAt(w, b.piles[0]).amount) != 0 &&
            (b.buildup >= 2 || (b.work_x < 0x28 && b.site.workers_at_work != 0)) && (flags & 1) != 0) {
            RequestSiteWorkers(w, n, &Building::builder_slots, &SiteBuilders, 2);
        }
    }
    if (b.buildup == 2) {
        bool blocked = false;
        for (const std::int32_t* p = SiteTileList(b); *p != kEndBlockList; p += 2) {
            if (Call(kIsFreeOfSettlerType29Nearby, w, U(b.x) + p[0], U(b.y) + p[1]) == 0) {
                blocked = true;
            }
        }
        if (!blocked) {
            b.buildup = 3;
            const std::int32_t* p = SiteTileList(b);
            for (; *p != kEndBlockList; p += 2) {
                Call(kBlockTile, w, U(b.x) + p[0], U(b.y) + p[1]);
                TileAt(w, U(b.x) + p[0], U(b.y) + p[1]).object = 0;
            }
            for (; *p != kEndClearList; p += 2) {
                if (*p == kSkipEntry) {
                    ++p;
                }
                Tile& t = TileAt(w, U(b.x) + p[0], U(b.y) + p[1]);
                if (t.object != 0xff) {
                    t.object = 0;
                }
            }
        }
    }
    if ((b.site.workers_at_work != 0 || b.buildup >= 2) && (b.request_flags & 1) != 0) {
        const std::uint8_t flags = b.request_flags;
        const Pile& p0 = PileAt(w, b.piles[0]);
        const std::uint32_t sum0 = U(p0.amount) + U(p0.coming);
        if (b.site.boards_missing != sum0 && sum0 != 8) {
            if ((PlayerAt(w, b.owner).settler_lists[0] & 0x80) == 0 || (flags & 2) != 0) {
                AddRequest(w, n, 1, b.work_y, 0xd);
            }
        }
        const Pile& p1 = PileAt(w, b.piles[1]);
        const std::uint32_t sum1 = U(p1.amount) + U(p1.coming);
        if (b.site.stones_missing != sum1 && sum1 != 8) {
            if ((PlayerAt(w, b.owner).settler_lists[1] & 0x80) == 0 || (b.request_flags & 4) != 0) {
                AddRequest(w, n, 2, b.work_y, 0x10);
            }
        }
    }
}

// --- finished buildings --------------------------------------------------------

// Purpose: A military building (tower, castle, ...): heals, lets one surplus
// soldier of a class leave when no enemy soldier is near, asks for soldiers
// of the three classes it lacks (nearest first, within 20, 40, 80 tiles) and
// warns its player when it is empty.
void UpdateMilitaryBuilding(GameWorld* w, std::uint32_t n) {
    Building& b = BuildingAt(w, n);
    for (std::uint32_t k = 0; k < 3; ++k) {
        static constexpr std::uint8_t kTypes[] = {0xf, 0x10, 0x12};
        if (b.art == kTypes[k] && U(b.hit_points) != kMilitaryMaxHitPoints[k]) {
            ++b.hit_points;
        }
    }
    std::uint32_t count[4] = {};
    for (std::uint32_t s = b.garrison; s != 0; s = SettlerAt(w, s).next) {
        const std::int32_t c = SettlerClass(SettlerAt(w, s).art);
        if (c >= 1 && c <= 3) {
            ++count[c];
        }
    }
    bool enemyNear = false;
    for (const MapOffset* o = w->offsets; o->distance < 10 && !enemyNear; ++o) {
        const std::uint32_t x = U(b.x) + static_cast<std::uint32_t>(o->dx);
        const std::uint32_t y = U(b.y) + static_cast<std::uint32_t>(o->dy);
        if (Call(kIsOnMap, w, x, y) == 0) {
            continue;
        }
        const std::uint32_t other = TileAt(w, x, y).settler;
        if (other == 0) {
            continue;
        }
        const Settler& s = SettlerAt(w, other);
        const std::int32_t c = SettlerClass(s.art);
        if ((c == 1 || c == 3) && PlayerAt(w, s.owner).team != PlayerAt(w, b.owner).team) {
            enemyNear = true;
        }
    }
    if (!enemyNear) {
        std::int32_t leaving = 0;
        if (count[1] > b.military.class1_wanted) {
            leaving = 1;
        } else if (count[2] > b.military.class2_wanted) {
            leaving = 2;
        } else if (count[3] > b.military.class3_wanted) {
            leaving = 3;
        }
        if (leaving != 0 && DoorTile(w, b).settler == 0) {
            // Take the first soldier of that class out of the garrison chain.
            std::uint32_t s = b.garrison;
            if (SettlerClass(SettlerAt(w, s).art) == leaving) {
                b.garrison = SettlerAt(w, s).next;
            } else {
                std::uint32_t previous;
                do {
                    previous = s;
                    s = SettlerAt(w, s).next;
                } while (SettlerClass(SettlerAt(w, s).art) != leaving);
                SettlerAt(w, previous).next = SettlerAt(w, s).next;
            }
            Settler& soldier = SettlerAt(w, s);
            if (SettlerClass(soldier.art) == 2) {
                Call(kLeaveBuildingSlot, w, s);
            }
            Call(kClearSettlerJob, w, s);
            soldier.x = b.door_x;
            soldier.y = b.door_y;
            DoorTile(w, b).settler = static_cast<std::uint16_t>(s);
            Call(kSetTowerSoldierAnimation, w, s);
            soldier.counter = 0;
        }
    }
    struct Wanted {
        std::uint8_t& (*coming)(Building&);
        std::uint8_t& (*wanted)(Building&);
        std::uint32_t lists[3];
    };
    static const Wanted kWanted[3] = {{&Class1Coming, &Class1Wanted, {0x23, 0x20, 5}},
                                      {&Class2Coming, &Class2Wanted, {0x21, 0x1e, 8}},
                                      {&Class3Coming, &Class3Wanted, {0x22, 0x1f, 0xf}}};
    for (std::uint32_t c = 0; c < 3; ++c) {
        const Wanted& k = kWanted[c];
        if (k.coming(b) < k.wanted(b) && count[c + 1] < U(k.wanted(b)) - U(k.coming(b))) {
            bool sent = false;
            for (const std::uint32_t distance : {20u, 40u, 80u}) {
                for (const std::uint32_t list : k.lists) {
                    if (!sent && Call(kSendNearestWorkerToBuilding, w, n, list, distance) != 0) {
                        sent = true;
                    }
                }
            }
            if (sent) {
                ++k.coming(b);
            }
        }
    }
    if (b.garrison == 0 && U(b.military.class2_coming) + U(b.military.class3_coming) + U(b.military.class1_coming) == 0) {
        Call(kNotifyLocalPlayer, w, U(b.owner), 0x10u, U(b.x), U(b.y));
    }
}

// Purpose: Probably the stonecutter-like building 0xc / 0xd, whose worker
// drops goods on the tiles around the door: requests its two inputs and
// sends carriers to those piles once five units wait there.
void UpdateTileDropBuilding(GameWorld* w, std::uint32_t n, std::uint32_t condition0, std::uint32_t condition1,
                            bool sevenTiles) {
    Building& b = BuildingAt(w, n);
    if (b.worker == 0) {
        RequestWorker(w, n, 0xb);
    } else {
        RequestInput(w, n, 0, 5, 8, 0, condition0);
        RequestInput(w, n, 1, 0xb, 8, 0, condition1);
    }
    if (sevenTiles) {
        // Door tile, (x-1, y), (x+1, y), (x-1, y-1), (x+1, y+1), (x, y-1), (x, y+1).
        const std::uint32_t piles[7] = {
            PileNextToDoor(w, b, 0),          PileNextToDoor(w, b, 0 - kStepX),
            PileNextToDoor(w, b, kStepX),     PileNextToDoor(w, b, 0 - kStepX - kStepY),
            PileNextToDoor(w, b, kStepX + kStepY), PileNextToDoor(w, b, 0 - kStepY),
            PileNextToDoor(w, b, kStepY)};
        std::uint32_t waiting = 0;
        for (const std::uint32_t pile : piles) {
            waiting += U(PileAt(w, pile).amount) - U(PileAt(w, pile).leaving);
        }
        if (waiting < 5) {
            return;
        }
        for (const std::uint32_t pile : piles) {
            if (PileAt(w, pile).amount != PileAt(w, pile).leaving) {
                Dispatch(w, pile);
            }
        }
    } else {
        // Door tile, (x-1, y), (x+1, y).
        const std::uint32_t piles[3] = {PileNextToDoor(w, b, 0), PileNextToDoor(w, b, 0 - kStepX),
                                        PileNextToDoor(w, b, kStepX)};
        std::uint32_t waiting = 0;
        for (const std::uint32_t pile : piles) {
            waiting += U(PileAt(w, pile).amount) - U(PileAt(w, pile).leaving);
        }
        if (waiting < 5) {
            return;
        }
        for (const std::uint32_t pile : piles) {
            if (PileAt(w, pile).amount != PileAt(w, pile).leaving) {
                Dispatch(w, pile);
            }
        }
    }
}

// Purpose: Probably the barracks-like building 0x13: turns spare carriers
// into recruits (one per input unit, limited by the area's recruit
// counters) while the area keeps enough carriers, and requests its three
// inputs (goods 0x12..0x14).
void UpdateBuilding13(GameWorld* w, std::uint32_t n) {
    Building& b = BuildingAt(w, n);
    const std::uint32_t areaNumber = DoorTile(w, b).area;
    Area& a = AreaAt(w, areaNumber);
    const std::uint32_t civilians = Call(kCountAreaSettlersOfClass0, w, areaNumber);
    const std::uint32_t limit = civilians * a.settings_extra[0] / 100 + 2;
    const std::uint32_t carriers = Call(kCountAreaSettlersOfType, w, areaNumber, 0u);
    if (limit < carriers) {
        std::uint8_t* const made[3] = {&b.recruiting.recruits[0], &b.recruiting.recruits[1], &b.recruiting.recruits[2]};
        for (std::uint32_t k = 0; k < 3; ++k) {
            if ((a.counters_242[k] != 0 || a.setting_241 != 0) && *made[k] < PileAt(w, b.piles[k]).amount) {
                const std::uint32_t settler = RequestSettler(w, n, 0);
                if (settler != 0) {
                    if (a.counters_242[k] != 0) {
                        --a.counters_242[k];
                    }
                    SettlerAt(w, settler).saved_counter = static_cast<std::uint16_t>(k + 1);
                    ++*made[k];
                }
            }
        }
    }
    if ((b.request_flags & 1) != 0) {
        for (std::uint32_t k = 0; k < 3; ++k) {
            const Pile& p = PileAt(w, b.piles[k]);
            const std::uint32_t amount = p.amount;
            const std::uint32_t coming = p.coming;
            if (amount + coming != *kSmithInputFull && (k == 0 || (b.request_flags & 1) != 0)) {
                AddRequest(w, n, 0x12 + k, Priority(w, amount, coming), 0);
            }
        }
    }
}

// Purpose: A residence (0x1a/0x1b/0x1c): sends out one new settler (a
// carrier, job 100) per counted birth (+0x19) while the player's population
// is within its limit and the door is free.
void UpdateResidence(GameWorld* w, std::uint32_t n, std::uint32_t animation) {
    Building& b = BuildingAt(w, n);
    if (b.residence.births == 0 || DoorTile(w, b).settler != 0) {
        return;
    }
    --b.residence.births;
    const std::uint32_t owner = b.owner;
    const Player& player = PlayerAt(w, owner);
    if (player.population > player.population_limit) {
        return;
    }
    const std::uint32_t settler = Call(kCreateSettler, w, U(b.door_x), U(b.door_y), 0u, owner);
    if (settler != 0) {
        SettlerAt(w, settler).job = 100;
        Call(kSetSettlerAnimation, w, settler, animation);
    }
}

// Purpose: A water building (well/fisher-like types 0x1f, 0x2f, 0x30, 0x32;
// the shipyard 0x20 is similar): once its extra tiles are taken
// (TakeBuildingExtraTiles), its state +0x1f fixes how much of its three
// inputs it wants (+0x1a..+0x1c, total in +0x20); requests inputs below that.
struct WaterSpec {
    std::uint32_t workerType;
    std::uint8_t stateA, wantA[3];
    std::uint8_t stateB, wantB[3];
    InputSpec inputs[3];  // good, -, -, condition
    bool log;
};

void SetWanted(Building& b, const std::uint8_t (&want)[3]) {
    b.water.wanted[0] = want[0];
    b.water.wanted[1] = want[1];
    b.water.wanted[2] = want[2];
}

void UpdateWaterBuilding(GameWorld* w, std::uint32_t n, const WaterSpec& spec) {
    Building& b = BuildingAt(w, n);
    if (b.worker == 0) {
        RequestWorker(w, n, spec.workerType);
        return;
    }
    if (b.water.state != 0 && b.water.extra_tiles == 0xff) {
        Call(kTakeBuildingExtraTiles, w, n);
        if (spec.log) {
            GameDebugLog(kFormatTempleExtraTiles, U(b.water.extra_tiles));
        }
        if (b.water.extra_tiles == 0xfe) {
            if (b.water.state == spec.stateA) {
                SetWanted(b, spec.wantA);
            } else if (spec.stateB != 0 && b.water.state == spec.stateB) {
                SetWanted(b, spec.wantB);
            }
            b.water.extra_tiles = static_cast<std::uint8_t>(b.water.wanted[2] + b.water.wanted[0] + b.water.wanted[1]);
        }
    }
    RequestInputBelow(w, n, 0, spec.inputs[0].good, b.water.wanted[0], spec.inputs[0].condition);
    RequestInputBelow(w, n, 1, spec.inputs[1].good, b.water.wanted[1], spec.inputs[1].condition);
    RequestInputBelow(w, n, 2, spec.inputs[2].good, b.water.wanted[2], spec.inputs[2].condition);
}

// Purpose: The shipyard (0x20): like a water building, but the wanted
// amounts of its two inputs come from tables by the ship being built
// (states 0x30..0x33 and 0x40..0x43).
void UpdateShipyard(GameWorld* w, std::uint32_t n) {
    Building& b = BuildingAt(w, n);
    if (b.worker == 0) {
        RequestWorker(w, n, 0x13);
        return;
    }
    if (b.water.state != 0 && b.water.extra_tiles == 0xff) {
        Call(kTakeBuildingExtraTiles, w, n);
        if (b.water.extra_tiles == 0xfe) {
            const std::uint8_t state = b.water.state;
            std::uintptr_t table = 0;
            if (state >= 0x30 && state <= 0x33) {
                table = kShipyardTable30;
            } else if (state >= 0x40 && state <= 0x43) {
                table = kShipyardTable40;
            }
            if (table != 0) {
                b.water.wanted[0] = *reinterpret_cast<const std::uint8_t*>(table + (b.water.state & 0xfu) * 0x20);
                b.water.wanted[1] = *reinterpret_cast<const std::uint8_t*>(table + 4 + (b.water.state & 0xfu) * 0x20);
                b.water.wanted[2] = *reinterpret_cast<const std::uint8_t*>(table + 8 + (b.water.state & 0xfu) * 0x20);
            }
            b.water.extra_tiles = static_cast<std::uint8_t>(b.water.wanted[2] + b.water.wanted[0] + b.water.wanted[1]);
        }
    }
    RequestInputBelow(w, n, 0, 1, b.water.wanted[0], 0xe);
    RequestInputBelow(w, n, 1, 0xb, b.water.wanted[1], 9);
}

// The goods a trade building's ring of six piles (from +0xe, linked by pile
// +0xc) can take: goods of piles with room are wanted; then each free pile
// (good 0xff) claims the next goods type at the cursor +0x1b (wrapping after
// 0x23) that nothing claims yet. `wanted` has 0x100 entries: the original's
// table has 0x24 and reads stale stack beyond (see the header).
void TradeWantedSlots(GameWorld* w, Building& b, std::uint32_t (&wanted)[0x100]) {
    std::uint32_t freePiles = 0;
    std::uint32_t pile = b.pile_ring;
    for (std::uint32_t k = 0; k < 6; ++k) {
        const Pile& p = PileAt(w, pile);
        if (p.good == 0xff) {
            ++freePiles;
        } else if (static_cast<std::int32_t>((U(p.leaving) >> 4) + U(p.amount)) < 8) {
            wanted[p.good] = 1;
        }
        pile = p.next;
    }
    while (freePiles != 0) {
        const std::uint8_t cursor = b.trading.goods_cursor;
        if (wanted[cursor] == 0) {
            wanted[cursor] = 1;
            if (cursor >= 0x24) {
                b.trading.goods_cursor = 0;
            }
            --freePiles;
        }
        ++b.trading.goods_cursor;
    }
}

// Purpose: A trade building (0x21 market-like, 0x22 port-like): offers the
// goods types its ring piles can take to the area (slot list A or B by the
// trade record's kind), then 0x21 calls a settler to the door every 7th
// visit when a ring pile holds goods, 0x22 sends settlers to fetch the goods
// of its ring piles.
void UpdateTradeBuilding(GameWorld* w, std::uint32_t n, bool port) {
    Building& b = BuildingAt(w, n);
    const layout::Trade& trade = *(w->trades + b.trade);
    if (trade.route_x[3] == 0xffff || trade.mode == 0 || (!port && b.dock_direction == 0xffff)) {
        return;
    }
    std::uint32_t wanted[0x100] = {};
    TradeWantedSlots(w, b, wanted);
    for (std::uint32_t good = 1; good < 0x24; ++good) {
        const std::uint8_t offered = trade.ordered[good];
        if (wanted[good] != 0 && offered != 0 && offered != trade.coming[good]) {
            if (trade.mode == 1) {
                Call(kSetAreaBuildingSlotA, w, n, good);
            } else if (trade.mode == 2) {
                Call(kSetAreaBuildingSlotB, w, n, good);
            }
        }
    }
    if (!port) {
        if (b.trading.call_countdown != 0) {
            --b.trading.call_countdown;
            return;
        }
        bool goods = false;
        std::uint32_t pile = b.pile_ring;
        for (std::uint32_t k = 0; k < 6; ++k) {
            if (PileAt(w, pile).amount != 0) {
                goods = true;
            }
            pile = PileAt(w, pile).next;
        }
        if (goods) {
            Call(kSendNearestSettlerToBuilding, w, n);
            b.trading.call_countdown = 6;
        }
        return;
    }
    std::uint32_t pile = b.pile_ring;
    for (std::uint32_t k = 0; k < 6; ++k) {
        const Pile& p = PileAt(w, pile);
        if (p.amount != 0 && p.amount != (p.leaving & 0xf)) {
            Call(kSendNearestSettlerToPile, w, pile, U(b.owner), n);
        }
        pile = PileAt(w, pile).next;
    }
}

// Purpose: Probably a temple-like building (0x29, 0x2b, 0x2d, 0x37): every
// 250 visits one point (+0x19); with two points and a free door it creates
// a settler of type 0x24 (job 100), which raises the player's population
// limit base.
void UpdateTemple(GameWorld* w, std::uint32_t n) {
    Building& b = BuildingAt(w, n);
    if (b.temple.countdown == 0) {
        b.temple.countdown = 0xfa;
    } else if (--b.temple.countdown == 0) {
        ++b.temple.points;
    }
    if (b.temple.points < 2 || DoorTile(w, b).settler != 0) {
        return;
    }
    const std::uint32_t settler = Call(kCreateSettler, w, U(b.door_x), U(b.door_y), 0x24u, U(b.owner));
    if (settler != 0) {
        Settler& s = SettlerAt(w, settler);
        s.job = 100;
        Call(kSetSettlerAnimation, w, settler, 0x119u);
        ++PlayerAt(w, s.owner).population_limit_base;
    }
    b.temple.points = static_cast<std::uint8_t>(b.temple.points - 2);
}

// Purpose: Probably an altar-like building (0x2a, 0x2c, 0x2e, 0x36): requests
// one goods type; every 30 visits one unit of its input pile is consumed
// (taken from the pile and the player's goods count) and turns into mana
// (twice for 0x2e).
void UpdateOffering(GameWorld* w, std::uint32_t n, std::uint32_t good, bool twice) {
    Building& b = BuildingAt(w, n);
    RequestInput(w, n, 0, good, 8, 0, 0);
    if (b.temple.countdown == 0) {
        if (PileAt(w, b.piles[0]).amount != 0) {
            b.temple.countdown = 0x1e;
            Call(kPileIncAmountLeaving, w, U(b.piles[0]));
        }
        return;
    }
    if (--b.temple.countdown != 0) {
        return;
    }
    Call(kTakeGoodFromPile, w, U(b.piles[0]));
    Call(kSubtractPlayerCounter, w, U(b.owner), U(PileAt(w, b.piles[0]).good), 1u);
    for (int k = 0; k < (twice ? 2 : 1); ++k) {
        PlayerAt(w, b.owner).mana += static_cast<std::int32_t>(*kManaPerOffering);
        PlayerAt(w, b.owner).mana_total += static_cast<std::int32_t>(*kManaPerOffering);
    }
    kPlayerTallies[U(b.owner) * 17] += twice ? 2 : 1;
}

const WaterSpec kWater1F = {0xb, 0x10, {6, 3, 6}, 0x20, {0, 0, 6}, {{1, 0, 0, 0x2b}, {0xb, 0, 0, 10}, {2, 0, 0, 0x11}}, false};
const WaterSpec kWater2F = {0xb, 0x11, {5, 6, 6}, 0x21, {0, 3, 6}, {{1, 0, 0, 0x2c}, {0xb, 0, 0, 0xb}, {3, 0, 0, 0x13}}, false};
const WaterSpec kWater30 = {0xb, 0x12, {3, 8, 6}, 0x22, {0, 3, 6}, {{1, 0, 0, 0x2d}, {0xb, 0, 0, 0xc}, {0x1e, 0, 0, 0}}, false};
const WaterSpec kWater32 = {0xb, 0x13, {8, 4, 6}, 0, {0, 0, 0}, {{1, 0, 0, 0x2e}, {0xb, 0, 0, 0x2f}, {10, 0, 0, 0}}, true};

// Purpose: The per-type economy work of a finished building (jump table
// 0x4e495c): worker and input requests, output collection and the special
// buildings above.
void UpdateFinishedBuilding(GameWorld* w, std::uint32_t n) {
    Building& b = BuildingAt(w, n);
    switch (b.art) {
        case 0x02: Production(w, n, 3, {}); break;
        case 0x03: Production(w, n, 4, {}); break;
        case 0x04: Production(w, n, 6, {{3, 8, 0, 0x12}}); break;
        case 0x05:
            if (b.worker == 0) {
                RequestWorker(w, n, 7);
            }
            break;
        case 0x07: Production(w, n, 10, {{8, 2, 6, 0x28}, {0x18, 2, 6, 0x24}, {4, 4, 4, 0x1e}}); break;
        case 0x08: Production(w, n, 10, {{8, 4, 4, 0x27}, {0x18, 2, 6, 0x25}, {4, 2, 6, 0x20}}); break;
        case 0x09: Production(w, n, 10, {{8, 2, 6, 0x29}, {0x18, 4, 4, 0x23}, {4, 2, 6, 0x1f}}); break;
        case 0x0a: Production(w, n, 9, {{5, 8, 0, 2}, {6, 8, 0, 0}}); break;
        case 0x0b: Production(w, n, 9, {{5, 8, 0, 1}, {7, 8, 0, 0}}); break;
        case 0x0c: UpdateTileDropBuilding(w, n, 4, 8, true); break;
        case 0x0d: UpdateTileDropBuilding(w, n, 3, 7, false); break;
        case 0x0e: Production(w, n, 0x1d, {}); break;
        case 0x0f:
        case 0x10:
        case 0x12: UpdateMilitaryBuilding(w, n); break;
        case 0x11: Production(w, n, 0xc, {{9, 8, 0, 0x16}}); break;
        case 0x13: UpdateBuilding13(w, n); break;
        case 0x14: Production(w, n, 0xd, {{0x16, 8, 0, 0}, {0x1a, 8, 0, 0x1a}}); break;
        case 0x15: Production(w, n, 0xe, {{0x17, 8, 0, 0}}); break;
        case 0x16: Production(w, n, 0x19, {{0x1b, 8, 0, 0x15}, {5, 8, 0, 5}}); break;
        case 0x17: Production(w, n, 0x1c, {{9, 8, 0, 0x17}, {0x1a, 8, 0, 0x1b}}); break;
        case 0x18: Production(w, n, 0x10, {}); break;
        case 0x19: Production(w, n, 0x11, {}); break;
        case 0x1a: UpdateResidence(w, n, 0x71); break;
        case 0x1b: UpdateResidence(w, n, 0x72); break;
        case 0x1c: UpdateResidence(w, n, 0x73); break;
        case 0x1d: Production(w, n, 10, {{0x1b, 5, 3, 0x14}, {4, 3, 5, 0x22}}); break;
        case 0x1e: Production(w, n, 0x12, {}); break;
        case 0x1f: UpdateWaterBuilding(w, n, kWater1F); break;
        case 0x20: UpdateShipyard(w, n); break;
        case 0x21: UpdateTradeBuilding(w, n, false); break;
        case 0x22: UpdateTradeBuilding(w, n, true); break;
        case 0x23:
            if (b.worker == 0) {
                RequestWorker(w, n, 0x15);
            }
            break;
        case 0x24: Production(w, n, 0x26, {}); break;
        case 0x25: Production(w, n, 10, {{8, 4, 4, 0x2a}, {0x18, 2, 6, 0x26}, {4, 2, 6, 0x21}}); break;
        case 0x26: Production(w, n, 0x1a, {{9, 8, 0, 0x19}, {0x1a, 8, 0, 0x1d}}); break;
        case 0x27: Production(w, n, 0x18, {{1, 8, 0, 0xf}}); break;
        case 0x28: Production(w, n, 0x1b, {{5, 8, 0, 6}, {0x19, 8, 0, 0}}); break;
        case 0x29:
        case 0x2b:
        case 0x2d:
        case 0x37: UpdateTemple(w, n); break;
        case 0x2a: UpdateOffering(w, n, 0x1d, false); break;
        case 0x2c: UpdateOffering(w, n, 0x15, false); break;
        case 0x2e: UpdateOffering(w, n, 0x21, true); break;
        case 0x2f: UpdateWaterBuilding(w, n, kWater2F); break;
        case 0x30: UpdateWaterBuilding(w, n, kWater30); break;
        case 0x31: Production(w, n, 0x1c, {{9, 8, 0, 0x18}, {0x1a, 8, 0, 0x1c}}); break;
        case 0x32: UpdateWaterBuilding(w, n, kWater32); break;
        case 0x33:
            if (b.worker == 0) {
                RequestWorker(w, n, 0x2d);
            }
            CollectOutput(w, n);
            break;
        case 0x34: Production(w, n, 0x2e, {{0x1a, 8, 0, 0}, {0x23, 8, 0, 0}}); break;
        case 0x35:
            if (b.worker == 0) {
                RequestWorker(w, n, 0x2f);
            } else {
                RequestInput(w, n, 0, 0x1c, 8, 0, 0);
                RequestInput(w, n, 1, 0x19, 8, 0, 0);
            }
            {
                // (x+1, y) and (x-1, y) of the door.
                const std::uint32_t right = PileNextToDoor(w, b, kStepX);
                const std::uint32_t left = PileNextToDoor(w, b, 0 - kStepX);
                const std::uint32_t waiting = U(PileAt(w, left).amount) - U(PileAt(w, left).leaving) -
                                              U(PileAt(w, right).leaving) + U(PileAt(w, right).amount);
                if (waiting >= 5) {
                    if (PileAt(w, right).amount != PileAt(w, right).leaving) {
                        Dispatch(w, right);
                    }
                    if (PileAt(w, left).amount != PileAt(w, left).leaving) {
                        Dispatch(w, left);
                    }
                }
            }
            break;
        case 0x36: UpdateOffering(w, n, 0x22, false); break;
        case 0x38: Production(w, n, 10, {{0x23, 5, 3, 0}, {4, 3, 5, 0x22}}); break;
        default: break;  // 0x06 and types outside 2..0x38: nothing
    }
}

// Purpose: Phase 0: starts a new cycle of the economy update.
void StartEconomyCycle(GameWorld* w) {
    Call(kCountDownTimers, w);
    Call(kRecountPlayerPopulation, w);
    for (std::uint32_t a = 1; a <= w->area_high_water; ++a) {
        Area& area = AreaAt(w, a);
        for (std::uint32_t k = 0; k < layout::kGoodsTypes; ++k) {
            area.requests_by_goods[k] = 0;
            area.buildings_a[k] = 0;
            area.buildings_b[k] = 0;
        }
    }
    auto blocks = [w]() -> std::uint32_t {
        const auto size = static_cast<std::int32_t>(w->map_size);
        return static_cast<std::uint32_t>((size + ((size >> 31) & 0xf)) >> 4);
    };
    for (std::uint32_t bx = 0; bx < blocks(); ++bx) {
        for (std::uint32_t by = 0; by < blocks(); ++by) {
            std::uint8_t* const b = w->presence_b[bx][by].player;
            std::uint8_t* const a = w->presence_a[bx][by].player;
            for (std::uint32_t p = 0; p <= w->presence_max_player; ++p) {
                a[p] = b[p];
                b[p] = 0;
            }
        }
    }
}

// Settler types that mark military presence (jump table 0x4e492c/0x4e4934).
bool MarksPresence(std::uint32_t type) {
    switch (type) {
        case 0x05: case 0x08: case 0x0f: case 0x16: case 0x17: case 0x1e: case 0x1f: case 0x20: case 0x21:
        case 0x22: case 0x23: case 0x24: case 0x25: case 0x27: case 0x28: case 0x29: case 0x2b: case 0x2c:
            return true;
        default:
            return false;
    }
}

// Purpose: Phases 1..16: marks the military presence of one sixteenth of the
// settlers.
void MarkMilitaryPresence(GameWorld* w, std::uint32_t phase) {
    for (std::uint32_t n = phase; n <= w->settler_high_water; n += 16) {
        const Settler& s = SettlerAt(w, n);
        if (s.owner == kFree || !MarksPresence(s.art)) {
            continue;
        }
        if (Call(kIsOnMap, w, U(s.x), U(s.y)) != 0) {
            std::uint8_t* const block = w->presence_b[U(s.x) >> 4][U(s.y) >> 4].player;
            block[PlayerAt(w, s.owner).team] = 1;
        }
    }
}

// The building types that consume goods `good` (per priority rank), as the
// original's inline lists.
std::uint32_t ConsumersOf(std::uint32_t good, std::uint32_t (&types)[7]) {
    auto set = [&types](std::initializer_list<std::uint32_t> list) {
        std::uint32_t k = 0;
        for (const std::uint32_t t : list) {
            types[k++] = t;
        }
        return k;
    };
    switch (good) {
        case 5: return set({1, 2, 3, 4, 5, 6});
        case 0xb: return set({7, 8, 9, 10, 0xb, 0xc, 0x2f});
        case 1: return set({0xd, 0xe, 0xf, 0x2b, 0x2c, 0x2d, 0x2e});
        case 2: return set({0x10, 0x11});
        case 3: return set({0x12, 0x13});
        case 0x19: return set({0x14, 0x15});
        case 9: return set({0x16, 0x17, 0x18, 0x19});
        case 0x1a: return set({0x1a, 0x1b, 0x1c, 0x1d, 0x32});
        case 4: return set({0x1e, 0x1f, 0x20, 0x21, 0x22});
        case 0x18: return set({0x23, 0x24, 0x25, 0x26});
        case 8: return set({0x27, 0x28, 0x29, 0x2a});
        case 0x23: return set({0x30, 0x31});
        default: return 0;
    }
}

// Whether request-list building q is of a consumer's building type:
// construction sites (0) or finished buildings of that type.
bool IsConsumerBuilding(GameWorld* w, std::uint32_t q, std::uint32_t buildingType) {
    const Building& b = BuildingAt(w, q);
    if (buildingType == 0) {
        return b.buildup != 0;
    }
    return b.buildup == 0 && U(b.art) == buildingType;
}

// The same for the chosen consumer, where 0xffffffff (none chosen) takes all.
bool FitsConsumer(GameWorld* w, std::uint32_t q, std::uint32_t requirement) {
    return requirement == 0xffffffffu || IsConsumerBuilding(w, q, requirement);
}

constexpr std::uint32_t kMaxCandidates = 100;

// Purpose: Phase 17 for one goods type of an area: picks the consumer type
// whose share (counter +0x278 per type, weighted by the area's setting
// +0x245) is lowest and that has a waiting building, then sends a carrier
// with one unit from the best pile to the requesting building that profits
// most (requested amount / distance). Goods 5 get a second delivery.
void DistributeGoods(GameWorld* w, std::uint32_t areaNumber, std::uint32_t good) {
    Area& area = AreaAt(w, areaNumber);
    std::uint32_t types[7];
    const std::uint32_t typeCount = ConsumersOf(good, types);
    std::uint32_t requirement = 0xffffffffu;
    std::uint32_t chosenType = 0;
    if (typeCount != 0) {
        std::uint32_t best = 0xffffffffu;
        for (std::uint32_t k = 0; k < typeCount; ++k) {
            const std::uint32_t t = types[k];
            std::uint32_t share = 0xffffffffu;
            const std::uint8_t weight = area.condition_enabled[t];
            if (weight != 0) {
                share = static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(U(area.counters_278[t]) * 0x100 + 0x80) / static_cast<std::int32_t>(weight));
            }
            if (share >= best) {
                continue;
            }
            const std::uint32_t wantedType = kConsumerBuildingType[t];
            bool found = false;
            for (std::uint32_t q = area.requests_by_goods[good]; q != 0 && !found;) {
                if (IsConsumerBuilding(w, q, wantedType)) {
                    found = true;
                }
                q = Call(kBuildingInputWord2A, w, q, good);
            }
            if (found) {
                best = share;
                requirement = wantedType;
                chosenType = t;
            }
        }
    }
    std::uint32_t candidates[kMaxCandidates];
    std::uint32_t count = 0;
    for (std::uint32_t q = area.requests_by_goods[good]; q != 0; q = Call(kBuildingInputWord2A, w, q, good)) {
        if (count == kMaxCandidates) {
            break;
        }
        if (FitsConsumer(w, q, requirement)) {
            candidates[count++] = q;
        }
    }
    std::uint32_t scores[kMaxCandidates];
    std::uint32_t sources[kMaxCandidates];
    for (std::uint32_t pass = 0; pass == 0 || (pass == 1 && good == 5 && count != 0);) {
        ++pass;
        for (std::uint32_t k = 0; k < count; ++k) {
            const std::uint32_t q = candidates[k];
            if (q == 0) {
                scores[k] = 0;
                continue;
            }
            const Building& b = BuildingAt(w, q);
            std::uint32_t nearest = 0xffffffffu;
            std::uint32_t source = 0;
            for (std::uint32_t p = area.piles_by_goods[good]; p != 0; p = PileAt(w, p).next) {
                const Pile& pile = PileAt(w, p);
                if (pile.amount == pile.leaving) {
                    continue;
                }
                std::uint32_t d = Distance(w, U(b.x), U(b.y), U(pile.x), U(pile.y));
                if (pile.art != 0xff) {
                    d = 2 * (d + 5 * U(pile.amount)) - 10 * (U(pile.leaving) + 1);
                }
                if (d < nearest) {
                    nearest = d;
                    source = p;
                }
            }
            if (source == 0) {
                scores[k] = 0;
                continue;
            }
            if (b.buildup == 0) {
                scores[k] = (Call(kBuildingInputWord24, w, q, good) << 16) / nearest;
            } else {
                scores[k] = Call(kBuildingInputWord24, w, q, good) << 16;
            }
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
        std::uint32_t nearest = 0xffffffffu;
        std::uint32_t carrier = 0;
        for (std::uint32_t s = area.settlers_by_type[0]; s != 0; s = SettlerAt(w, s).next) {
            const Settler& settler = SettlerAt(w, s);
            if ((settler.flags & 0x80) != 0) {
                continue;
            }
            const std::uint32_t d =
                Distance(w, U(PileAt(w, source).x), U(PileAt(w, source).y), U(settler.x), U(settler.y));
            if (d < nearest) {
                nearest = d;
                carrier = s;
            }
        }
        if (carrier != 0) {
            Call(kPileIncAmountLeaving, w, source);
            Settler& c = SettlerAt(w, carrier);
            if (c.prev != 0) {
                SettlerAt(w, c.prev).next = c.next;
            } else {
                area.settlers_by_type[0] = c.next;
            }
            if (c.next != 0) {
                SettlerAt(w, c.next).prev = c.prev;
            }
            Call(kSetSettlerTarget, w, carrier, U(PileAt(w, source).x), U(PileAt(w, source).y));
            c.job = 10;
            c.counter = 0;
            c.building = static_cast<std::uint16_t>(target);
            c.good = static_cast<std::uint8_t>(good);
            Building& t = BuildingAt(w, target);
            if (t.buildup != 0) {
                if (good == 1) {
                    Call(kPileIncAmountComing, w, U(t.piles[0]));
                } else if (good == 2) {
                    Call(kPileIncAmountComing, w, U(t.piles[1]));
                }
            } else {
                for (std::uint32_t slot = 0; slot < 3; ++slot) {
                    if (good == PileAt(w, t.piles[slot]).good) {
                        Call(kPileIncAmountComing, w, U(t.piles[slot]));
                        break;
                    }
                }
            }
            ++area.counters_278[chosenType];
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

// Purpose: Phase 17: goods distribution in every area (trade buildings,
// then the goods by the area's priority ranks +0x1f6).
void DistributeAreaGoods(GameWorld* w) {
    std::uint32_t maxRank = 0;  // see the header: stale in the original for races above 3
    for (std::uint32_t a = 1; a <= w->area_high_water; ++a) {
        Area& area = AreaAt(w, a);
        if (area.player == kFree) {
            continue;
        }
        for (std::uint32_t good = 1; good < 0x24; ++good) {
            const std::uint32_t q = area.buildings_b[good];
            if (q != 0) {
                Call(kFetchGoodsToTradeBuilding, w, a, q, good);
            }
        }
        const std::int32_t race = PlayerAt(w, area.player).race;
        if (race == 0) {
            maxRank = 0x1a;
        }
        if (race == 1) {
            maxRank = 0x1b;
        }
        if (race == 2 || race == 3) {
            maxRank = 0x1d;
        }
        for (std::uint32_t rank = 1; rank <= maxRank; ++rank) {
            std::uint32_t good = 1;
            while (area.settings4[good] != rank) {
                ++good;
            }
            DistributeGoods(w, a, good);
        }
        for (std::uint32_t good = 1; good < 0x24; ++good) {
            const std::uint32_t q = area.buildings_a[good];
            if (q != 0) {
                Call(kFetchGoodsToTradeBuilding, w, a, q, good);
            }
        }
    }
}

}  // namespace

using UpdateEconomyFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx);

// Purpose: The economy update of a logic step, one of 18 time slices: phase
// 0 starts a cycle (timers, population, request lists, military presence),
// phases 1..16 each handle a sixteenth of the settlers (military presence)
// and of the buildings (workers, goods requests, output), phase 17
// distributes goods in every area.
std::uint32_t __fastcall GameWorld_UpdateEconomy(GameWorld* world, void*) {
    if (world->counter_110a57c == 0) {
        StartEconomyCycle(world);
    }
    const std::uint32_t phase = world->counter_110a57c;
    if (phase >= 1 && phase <= kLastBuildingPhase) {
        if (phase <= world->settler_high_water) {
            MarkMilitaryPresence(world, phase);
        }
        for (std::uint32_t n = world->counter_110a57c; n <= world->building_high_water; n += 16) {
            const Building& b = BuildingAt(world, n);
            if (b.owner == kFree) {
                continue;
            }
            if (b.buildup != 0) {
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

S3RE_HOOK(0x004e05e0, GameWorld_UpdateEconomy, UpdateEconomyFn, Original_GameWorld_UpdateEconomy,
          GameWorld_UpdateEconomy);

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

constexpr std::size_t kTestWorld = offsetof(GameWorld, buildings) + 128 * sizeof(Building);
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
    stubs.Install<N, Address>([value](GameWorld*, std::uint32_t, const std::uint32_t* args) {
        const std::uint32_t h = Mix(static_cast<std::uint32_t>(Address), args);
        return value != nullptr ? value(h) : h & 1;
    });
}

void InstallEconomyStubs(test::CalleeStubs& stubs) {
    Probe<kRequestSettlerForBuilding, 2>(stubs, [](std::uint32_t h) { return SmallOrZero(h, 60, 128); });
    Probe<kIsFreeOfSettlerType29Nearby, 2>(stubs, [](std::uint32_t h) { return static_cast<std::uint32_t>((h & 0x7f) != 0); });
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
    stubs.Install<1, kTakeBuildingExtraTiles>([](GameWorld* w, std::uint32_t, const std::uint32_t* args) {
        if ((Mix(static_cast<std::uint32_t>(kTakeBuildingExtraTiles), args) & 1) != 0) {
            BuildingAt(w, args[0]).water.extra_tiles = 0xfe;
        }
        return 0u;
    });
    Probe<kSetAreaBuildingSlotA, 2>(stubs);
    Probe<kSetAreaBuildingSlotB, 2>(stubs);
    Probe<kSendNearestSettlerToBuilding, 1>(stubs);
    Probe<kSendNearestSettlerToPile, 3>(stubs);
    stubs.Install<1, kTakeGoodFromPile>([](GameWorld* w, std::uint32_t, const std::uint32_t* args) {
        const std::uint32_t h = Mix(static_cast<std::uint32_t>(kTakeGoodFromPile), args);
        if ((h & 1) != 0) {
            PileAt(w, args[0] & 0xffff).good = static_cast<std::uint8_t>((h >> 8) % 36);
        }
        return 0u;
    });
    Probe<kSubtractPlayerCounter, 3>(stubs);
    Probe<kPileIncAmountLeaving, 1>(stubs);
    Probe<kPileIncAmountComing, 1>(stubs);
    stubs.Install<1, kDispatchPileCarrier>([](GameWorld* w, std::uint32_t, const std::uint32_t* args) {
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
        Both([&](GameWorld* w) {
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
        Both([&](GameWorld* w) {
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
                Both([&](GameWorld* w) { (w->piles + ring[k])->next = ring[(k + 1) % 6]; });
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
            Both([&](GameWorld* w) {
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
                Both([&](GameWorld* w) {
                    (w->settlers + members[k])->art = type;
                    (w->settlers + members[k])->next = next;
                });
            }
        }
        Both([&](GameWorld* w) { (w->buildings + n)->worker = worker; });
        // The tiles around the building (soldiers near, piles next to the door).
        for (std::int32_t ox = -4; ox <= 4; ++ox) {
            for (std::int32_t oy = -4; oy <= 4; ++oy) {
                const auto s = static_cast<std::uint16_t>(rng().Chance(1, 2) ? 0 : 1 + rng().Below(kTestRecords));
                const auto pile = static_cast<std::uint16_t>(rng().Chance(1, 5) ? 0 : 1 + rng().Below(300));
                Both([&](GameWorld* w) {
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
        Both([&](GameWorld* w) {
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
        Both([&](GameWorld* w) { (w->areas + area)->player = player; });
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
                Both([&](GameWorld* w) {
                    layout::Pile& p = *(w->piles + members[k]);
                    p.x = px;
                    p.y = 200;
                    p.art = art;
                });
            }
            for (std::uint32_t k = 0; k < count; ++k) {
                const std::uint16_t next = k + 1 < count ? members[k + 1] : 0;
                Both([&](GameWorld* w) { (w->piles + members[k])->next = next; });
            }
            const std::uint16_t head = count != 0 ? members[0] : 0;
            Both([&](GameWorld* w) {
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
            Both([&](GameWorld* w) {
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
            Both([&](GameWorld* w) {
                layout::Settler& s = *(w->settlers + list[k]);
                s.prev = k == 0 ? 0 : list[k - 1];
                s.next = k + 1 < carriers ? list[k + 1] : 0;
                s.flags = flags;
                s.x = sx;
                s.y = sy;
            });
        }
        Both([&](GameWorld* w) { (w->areas + area)->settlers_by_type[0] = carriers != 0 ? list[0] : 0; });
    }

    // One case; returns the phase.
    std::uint32_t Case() {
        const std::uint32_t r = rng().Below(100);
        const std::uint32_t phase = r < 12 ? 0 : r < 35 ? 17 : 1 + rng().Below(16);
        static constexpr std::uint32_t kMaxPlayers[] = {0, 1, 3, 19};
        const std::uint32_t maxPlayer = kMaxPlayers[rng().Below(4)];
        Both([&](GameWorld* w) {
            w->counter_110a57c = phase;
            w->map_size = 384;
            w->presence_max_player = maxPlayer;
            w->settler_high_water = 0;
            w->building_high_water = 0;
            w->area_high_water = 0;
        });
        for (std::uint32_t p = 0; p < 20; ++p) {
            const auto index = static_cast<std::int32_t>(rng().Below(20));
            Both([&](GameWorld* w) { w->players[p].team = index; });
        }
        if (phase == 0) {
            static constexpr std::int32_t kSizes[] = {0, 15, 16, 17, 33, 48, 64};
            const std::int32_t size = rng().Chance(1, 4) ? static_cast<std::int32_t>(rng().Below(70)) : kSizes[rng().Below(7)];
            const std::uint32_t areas = rng().Below(6);
            const std::uint32_t settlers = rng().Below(40);
            const std::uint32_t buildings = rng().Below(40);
            Both([&](GameWorld* w) {
                w->map_size = static_cast<std::uint32_t>(size);
                w->area_high_water = areas;
                w->settler_high_water = settlers;
                w->building_high_water = buildings;
            });
        } else if (phase == 17) {
            const std::uint32_t areas = rng().Below(5);
            Both([&](GameWorld* w) { w->area_high_water = areas; });
            for (std::uint32_t p = 0; p < 20; ++p) {
                const auto race = static_cast<std::int32_t>(p == 19 && rng().Chance(1, 2) ? 4 + rng().Below(4) : rng().Below(4));
                Both([&](GameWorld* w) { w->players[p].race = race; });
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
                Both([&](GameWorld* w) {
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
            Both([&](GameWorld* w) {
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
                Both([&](GameWorld* w) {
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
                Both([&](GameWorld* w) {
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

std::int32_t* const kTallies = kPlayerTallies;
constexpr std::size_t kTallyCount = 20 * 17;

void CompareUpdateEconomy(test::Context& ctx, UpdateEconomyFn candidate, int cases) {
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
        const std::uint32_t expected = Original_GameWorld_UpdateEconomy(worlds.original(), nullptr);
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
            !worlds.SameField(ctx, [](GameWorld* w) -> auto& { return w->counter_110a57c; }) ||
            (checkpoint && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 15 before it): phase %u, %u/%u stub calls", i,
                     phase, static_cast<unsigned>(expectedCalls.size()), static_cast<unsigned>(actualCalls.size()));
            break;
        }
    }
    std::copy(saved.begin(), saved.end(), kTallies);
}

// Wrong: every building counts as wanting inputs (request flag bit 0).
std::uint32_t __fastcall WrongUpdateEconomy(GameWorld* world, void* edx) {
    std::vector<std::uint8_t> flags;
    for (std::uint32_t n = 0; n < 64; ++n) {
        flags.push_back(world->buildings[n].request_flags);
        world->buildings[n].request_flags |= 1;
    }
    const std::uint32_t result = GameWorld_UpdateEconomy(world, edx);
    for (std::uint32_t n = 0; n < 64; ++n) {
        world->buildings[n].request_flags = flags[n];
    }
    return result;
}

}  // namespace

S3RE_TEST(GameWorld_UpdateEconomy_MatchesOriginal) {
    CompareUpdateEconomy(ctx, &GameWorld_UpdateEconomy, 4000);
}

S3RE_TEST(GameWorld_UpdateEconomy_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareUpdateEconomy(probe, &WrongUpdateEconomy, 400);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
