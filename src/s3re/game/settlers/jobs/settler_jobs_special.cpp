// CGameData::UpdateSettlers (0x004e9670), job handlers 0x4f5f6b..0x4f91fb of
// the original (settler_jobs.h): the shipyard worker (jobs 314..324), the
// trade ships and ferries at the harbour and the marketplace (308, 310, 331,
// 332), the healer (333..336), a shooting soldier's reload (337), the thief
// (338, 403, 404), the beekeeper (353..369), the alchemist (339..352), the
// mead maker (380..389), the war machine workshops' gong order (400..402),
// the plain "wait ten ticks" handler of jobs 23, 141, 325, 326 (0x4f8b7c)
// and job 304.
//
// Shared tails of this range: 0x4f8b7c (counter 10, the next step in ten
// ticks: WaitTen), 0x4f8f44 (an ordered unit done: building byte +0x19
// counts up, the settler leaves its tile and waits: CountUpAndLeaveTile).
// Tails of other ranges duplicated here: 0x4ece3b (job 353), 0x4ecf12 (jobs
// 369, 314, 339, 380, 400), 0x4eb3d2 (job 366), 0x4f323b and 0x4f4331 (back
// to the door, jobs 358..363).
//
// Frame slots (settler_jobs.h FrameSlots) written here: slot20 / slot14
// (route and search coordinates, as the original's scratch x / y), slot30
// (job 308's pile, job 331's ring piles, job 403's pile) and the low byte of
// slot5c (job 403's good). Read before written: job 331 reads slot30 when
// its cargo loop ends before the first ring scan (only for a race whose
// cargo count in S3.EXE 0x659654 is 0, i.e. reading past the table).

#include "game/settlers/jobs/settler_jobs.h"
#include "game/settlers/jobs/settler_jobs_test.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace s3re::game::settler_jobs {

namespace {

using B = BuildingType;
using G = Good;
using layout::MapOffset;
using layout::Trade;

constexpr std::uint16_t kRouteUnset = 0xffff;   // Trade::route_x / route_y
constexpr std::uint8_t kFreeOwner = 0xff;       // Tile::owner
constexpr std::uint8_t kPileGoodNone = 0xff;    // Pile::good of an empty building pile
constexpr std::uint8_t kTileNoThief = 0x20;     // Tile::flags: a thief takes nothing here (probably)
constexpr std::uint8_t kTileBlocking = 0x09;    // Tile::flags bits 0 and 3: occupied
constexpr std::uint8_t kRequestInputs = 0x01;   // Building::request_flags bit 0: working
constexpr std::uint8_t kGongOrdered = 0x13;     // Building kind byte +0x1f: a gong is ordered (UpdateEconomy)
constexpr std::uint32_t kWaitTicks = 10;

// Building kind bytes +0x19..+0x20 (Building::kind_bytes) as used here.
constexpr int kUnitsDone = 0;       // +0x19: units finished (counted up by CountUpAndLeaveTile)
constexpr int kBoardsWanted = 1;    // +0x1a: shipyard / workshop: boards still to fetch
constexpr int kIronWanted = 2;      // +0x1b: iron still to fetch
constexpr int kGoldWanted = 3;      // +0x1c: gold still to fetch
constexpr int kOrderState = 6;      // +0x1f
constexpr int kStepsLeft = 7;       // +0x20: work steps left (1..250)

Trade& TradeAt(CGameData* world, std::uint32_t n) { return *(world->trades + n); }
MapOffset& OffsetAt(CGameData* world, std::uint32_t n) { return *(world->offsets + n); }
Building& BuildingOf(SettlerContext& ctx) { return BuildingAt(ctx.world, ctx.settler.building); }
Tile& SettlerTile(SettlerContext& ctx) { return TileAt(ctx.world, ctx.settler.x, ctx.settler.y); }
// The tile `rows` x-steps from (x, y) (one row = 0x300 tiles), by byte offset like the original.
Tile& TileRows(CGameData* world, std::uint32_t x, std::uint32_t y, std::int32_t rows) {
    return TileAtOffset(world, TileOffset(x, y) + static_cast<std::uint32_t>(rows) * kTileRowBytes);
}
// The tile `steps` y-steps from (x, y).
Tile& TileSteps(CGameData* world, std::uint32_t x, std::uint32_t y, std::int32_t steps) {
    return TileAtOffset(world, TileOffset(x, y) + static_cast<std::uint32_t>(steps) * kTileBytes);
}
std::int32_t TeamOf(CGameData* world, std::uint32_t player) { return PlayerAt(world, player).team; }

void Animate(SettlerContext& ctx, std::uint32_t animation) {
    Call<kSetSettlerAnimation>(ctx.world, ctx.number, animation);
}
bool HasGoods(SettlerContext& ctx, std::uint32_t building, std::uint32_t slot) {
    return (Call<kBuildingPileHasGoods>(ctx.world, building, slot) & 0xff) != 0;
}
void SetTargetToDoor(SettlerContext& ctx) {
    const Building& b = BuildingOf(ctx);
    Call<kSetSettlerTarget>(ctx.world, ctx.number, b.door_x, b.door_y);
}

// 0x4f8b7c: try again in ten ticks.
JobResult WaitTen(SettlerContext& ctx) {
    ctx.settler.counter = kWaitTicks;
    return JobResult::Next;
}

// 0x4f8f44 (+ 0x4ecf12, 0x4f91fb with DX = 0): the building counts one more
// unit done; the settler leaves its tile.
JobResult CountUpAndLeaveTile(SettlerContext& ctx) {
    ++BuildingOf(ctx).kind_bytes[kUnitsDone];
    SettlerTile(ctx).settler = 0;
    ctx.settler.counter = 0;
    return JobResult::Next;
}

// 0x4ecf12 + 0x4f91fb (DX = 0): leaves its tile, next step at once.
JobResult LeaveTile(SettlerContext& ctx, Job next) {
    SetJob(ctx.settler, next);
    SettlerTile(ctx).settler = 0;
    ctx.settler.counter = 0;
    return JobResult::Next;
}

// ---------------------------------------------------------------- trade routes
// A route has up to four points (route_x/route_y, 0xffff = unset); the last
// one is the destination harbour or marketplace.

// Whether a point of the trade's route lies in another region than the
// settler's tile: the walk region (jobs 308, 310) or the economy area (jobs
// 331, 332). The original keeps each point in its scratch slots 20 / 14.
bool RouteLeavesRegion(SettlerContext& ctx, const Trade& trade, bool byArea) {
    bool elsewhere = false;
    for (int k = 0; k < 4; ++k) {
        if (trade.route_x[k] == kRouteUnset) {
            continue;
        }
        const std::uint32_t x = trade.route_x[k];
        const std::uint32_t y = trade.route_y[k];
        ctx.frame.slot20 = x;
        ctx.frame.slot14 = y;
        const Tile& here = SettlerTile(ctx);
        const Tile& point = TileAt(ctx.world, x, y);
        if (byArea ? here.area != point.area : here.walk_region != point.walk_region) {
            elsewhere = true;
        }
    }
    return elsewhere;
}

// The count-th point of a walk: the first is the target, the next three go
// into the path cache.
void SetRoutePoint(SettlerContext& ctx, std::uint32_t count, std::uint32_t x, std::uint32_t y) {
    Settler& s = ctx.settler;
    switch (count) {
    case 0: Call<kSetSettlerTarget>(ctx.world, ctx.number, x, y); break;
    case 1:
        s.path_cache[0] = static_cast<std::uint16_t>(x);
        s.path_cache[1] = static_cast<std::uint16_t>(y);
        break;
    case 2:
        s.path_cache[2] = static_cast<std::uint16_t>(x);
        s.path_cache[3] = static_cast<std::uint16_t>(y);
        break;
    case 3:
        s.path_cache[4] = static_cast<std::uint16_t>(x);
        s.path_cache[5] = static_cast<std::uint16_t>(y);
        break;
    default: break;
    }
}

// Along the route, points 0..3.
void FollowRoute(SettlerContext& ctx, const Trade& trade) {
    std::uint32_t count = 0;
    for (int k = 0; k < 4; ++k) {
        if (trade.route_x[k] == kRouteUnset) {
            continue;
        }
        SetRoutePoint(ctx, count, trade.route_x[k], trade.route_y[k]);
        ++count;
    }
}

void ClearRoute(Trade& trade) {
    for (int k = 0; k < 4; ++k) {
        trade.route_x[k] = kRouteUnset;
        trade.route_y[k] = kRouteUnset;
    }
}

// Back along the route: points 2, 1, 0 (those set), then `home()` (step 3).
// Every step sets the job (and, for ships, resets the walk counter).
template <typename Home>
void ReturnAlongRoute(SettlerContext& ctx, const Trade& trade, Job job, bool ship, Home home) {
    Settler& s = ctx.settler;
    std::uint32_t count = 0;
    for (int k = 0; k < 4; ++k) {
        if (k <= 2 && trade.route_x[2 - k] == kRouteUnset) {
            SetJob(s, job);
            if (ship) {
                s.ship_walk = 0;
            }
            continue;
        }
        std::uint32_t x, y;
        if (k <= 2) {
            x = trade.route_x[2 - k];
            y = trade.route_y[2 - k];
        } else {
            home(x, y);
        }
        SetRoutePoint(ctx, count, x, y);
        ++count;
        SetJob(s, job);
        if (ship) {
            s.ship_walk = 0;
        }
    }
}

// 0x4f6e6a: the trip ends here (no usable route or building).
JobResult EndTrip(SettlerContext& ctx) {
    SetJob(ctx.settler, Job::New);
    ctx.settler.building = 0;
    ctx.settler.counter = 0;
    return JobResult::Next;
}

// Whether the trade building of the settler (owned by its player, of `type`,
// finished) has a route back that stays in this walk region (marketplace) or
// economy area (harbour) and ends here.
Trade* RouteBackFromHere(SettlerContext& ctx, BuildingType type, bool byArea) {
    Settler& s = ctx.settler;
    const Building& b = BuildingOf(ctx);
    if (b.owner != s.owner || TypeOf(b) != type || b.buildup != 0) {
        return nullptr;
    }
    Trade& trade = TradeAt(ctx.world, b.trade);
    if (RouteLeavesRegion(ctx, trade, byArea)) {
        return nullptr;
    }
    if (trade.route_x[3] != s.x || trade.route_y[3] != s.y) {
        return nullptr;
    }
    return &trade;
}

// 0x4f5f79..0x4f6250, job 308 (0x134): a donkey (probably; it unloads at
// the marketplace, job 310) loaded at a pile (the pile under it; pile +0xa
// is its building) sets off along the trade route, or gives the route up
// when a point is in another walk region. +0x17 set: it takes a second unit.
JobResult DonkeyLoad(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& s = ctx.settler;
    const std::uint32_t pileNumber = SettlerTile(ctx).object_number;
    ctx.frame.slot30 = pileNumber;
    Pile& pile = PileAt(world, pileNumber);
    Trade& trade = TradeAt(world, BuildingAt(world, pile.prev).trade);
    if (RouteLeavesRegion(ctx, trade, false) || trade.route_x[3] == kRouteUnset) {
        ClearRoute(trade);
        SetJob(s, Job::New);
        s.counter = 0;
        --pile.leaving;
        return JobResult::Next;
    }
    FollowRoute(ctx, trade);
    SetJob(s, Job::Job309);
    s.counter = 0;
    s.slot = s.good;
    --pile.amount;
    --pile.leaving;
    if (s.cargo_amounts01 != 0) {  // a second unit
        --pile.amount;
        --pile.leaving;
    }
    if (pile.amount == 0 && pile.leaving == 0) {
        PileAt(world, pileNumber).good = kPileGoodNone;
    }
    return JobResult::Next;
}

// 0x4f6255..0x4f650f, job 310 (0x136): a donkey (probably) unloads at the
// marketplace and walks the route back.
JobResult DonkeyUnload(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& s = ctx.settler;
    Call<kDropGoodNear>(world, s.slot, 1, s.x, s.y);
    Call<kSubtractPlayerCounter>(world, s.owner, s.slot, 1);
    if (s.cargo_amounts01 != 0) {
        Call<kDropGoodNear>(world, s.slot, 1, s.x, s.y);
        Call<kSubtractPlayerCounter>(world, s.owner, s.slot, 1);
        s.cargo_amounts01 = 0;
    }
    SetJob(s, Job::New);
    s.slot = 0;
    Trade* trade = RouteBackFromHere(ctx, B::Marketplace, false);
    if (trade == nullptr) {
        return EndTrip(ctx);
    }
    ReturnAlongRoute(ctx, *trade, Job::Job330, false, [&](std::uint32_t& x, std::uint32_t& y) {
        const Building& b = BuildingOf(ctx);
        x = b.door_x;
        y = b.door_y;
    });
    s.counter = 0;
    return JobResult::Next;
}

// 0x4f6514..0x4f6a13, job 331 (0x14b): a trade ship at the harbour takes the
// fullest ring piles as cargo (as many as its race's ships carry, S3.EXE
// 0x659654) and sets off; without cargo the route is given up.
JobResult ShipLoadAtHarbour(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& s = ctx.settler;
    Building& b = BuildingOf(ctx);
    if (TypeOf(b) != B::Harbour || b.buildup != 0) {
        return JobResult::Next;
    }
    Trade& trade = TradeAt(world, b.trade);
    if (RouteLeavesRegion(ctx, trade, true) || trade.route_x[3] == kRouteUnset) {
        return JobResult::Next;
    }
    // The last ring pile looked at (the original's slot 30, which keeps its
    // value from an earlier settler if no ring was scanned).
    std::uint32_t lastPile = ctx.frame.slot30;
    std::uint32_t loads = 0;
    for (;; ++loads) {
        const std::int32_t race = PlayerAt(world, s.owner).race;
        if (loads == static_cast<std::uint32_t>((&g_table659654[0])[race])) {
            break;
        }
        std::uint32_t best = 0;
        std::uint32_t bestAmount = 0;
        std::uint32_t pileNumber = b.pile_ring;
        for (int k = 0; k < 6; ++k) {
            const Pile& p = PileAt(world, pileNumber);
            if (p.amount != 0 && p.amount > bestAmount) {
                best = pileNumber;
                bestAmount = p.amount;
            }
            pileNumber = p.next;
        }
        lastPile = pileNumber;
        ctx.frame.slot30 = pileNumber;
        if (best == 0) {
            break;
        }
        Pile& cargo = PileAt(world, best);
        switch (loads) {
        case 0:
            s.saved_animation |= cargo.good;
            s.cargo_amounts01 |= cargo.amount;
            break;
        case 1:
            s.saved_animation |= static_cast<std::uint32_t>(cargo.good) << 8;
            s.cargo_amounts01 |= static_cast<std::uint8_t>(cargo.amount << 4);
            break;
        case 2:
            s.saved_counter |= cargo.good;
            s.cargo_amounts23 |= cargo.amount;
            break;
        case 3:
            s.saved_counter |= static_cast<std::uint16_t>(cargo.good << 8);
            s.cargo_amounts23 |= static_cast<std::uint8_t>(cargo.amount << 4);
            break;
        default: break;
        }
        cargo.amount = 0;
        if (cargo.leaving == 0) {
            cargo.good = kPileGoodNone;
        }
    }
    if (loads == 0) {
        ClearRoute(trade);
        SetJob(s, Job::New);
        s.counter = 0;
        --PileAt(world, lastPile).leaving;  // the last ring pile looked at (as the original)
        return JobResult::Next;
    }
    FollowRoute(ctx, trade);
    SetJob(s, Job::Job328);
    s.ship_walk = 0;
    s.counter = 0;
    Pile& last = PileAt(world, lastPile);
    if (last.amount == 0 && last.leaving == 0) {
        last.good = kPileGoodNone;
    }
    return JobResult::Next;
}

// 0x4f6a18..0x4f6e7f, job 332 (0x14c, "Schiff laedt ab"): a trade ship
// unloads its up to four cargo goods at the harbour and sails the route back
// to two tiles off its home harbour's landing.
JobResult ShipUnload(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& s = ctx.settler;
    GameDebugLog(texts::kShipUnloads);
    if ((s.cargo_amounts01 & 0x0f) != 0) {
        Call<kDropGoodNear>(world, s.saved_animation & 0xff, s.cargo_amounts01 & 0x0f, s.x, s.y);
        Call<kSubtractPlayerCounter>(world, s.owner, s.saved_animation & 0xff, s.cargo_amounts01 & 0x0f);
    }
    if ((s.cargo_amounts01 & 0xf0) != 0) {
        Call<kDropGoodNear>(world, (s.saved_animation >> 8) & 0xff, s.cargo_amounts01 >> 4, s.x, s.y);
        Call<kSubtractPlayerCounter>(world, s.owner, (s.saved_animation >> 8) & 0xff, s.cargo_amounts01 >> 4);
    }
    if ((s.cargo_amounts23 & 0x0f) != 0) {
        Call<kDropGoodNear>(world, s.saved_counter & 0xff, s.cargo_amounts23 & 0x0f, s.x, s.y);
        Call<kSubtractPlayerCounter>(world, s.owner, s.saved_counter & 0xff, s.cargo_amounts23 & 0x0f);
    }
    if ((s.cargo_amounts23 & 0xf0) != 0) {
        Call<kDropGoodNear>(world, s.saved_counter >> 8, s.cargo_amounts23 >> 4, s.x, s.y);
        Call<kSubtractPlayerCounter>(world, s.owner, s.saved_counter >> 8, s.cargo_amounts23 >> 4);
    }
    s.saved_animation = 0;
    s.cargo_amounts01 = 0;
    s.saved_counter = 0;
    s.cargo_amounts23 = 0;
    Trade* trade = RouteBackFromHere(ctx, B::Harbour, true);
    if (trade == nullptr) {
        return EndTrip(ctx);
    }
    ReturnAlongRoute(ctx, *trade, Job::Job329, true, [&](std::uint32_t& x, std::uint32_t& y) {
        // Two tiles from the landing's work point, away from the water.
        const Building& b = BuildingOf(ctx);
        const std::uint32_t away = (static_cast<std::uint32_t>(b.dock_direction) + 3) % 6;
        const std::uint32_t x1 = CallStd<kNeighbourX>(b.work_x, away);
        const std::uint32_t y1 = CallStd<kNeighbourY>(b.work_y, away);
        x = CallStd<kNeighbourX>(x1, away);
        y = CallStd<kNeighbourY>(y1, away);
    });
    s.counter = 0;
    return JobResult::Next;
}

// ---------------------------------------------------------------- shipyard (worker type 19)

// 0x4f6e92..0x4f6fef, job 315 (0x13b): fetch boards or iron for the ship
// (+0x20 steps left), go to the work point.
JobResult ShipyardFetch(SettlerContext& ctx) {
    Settler& s = ctx.settler;
    const std::uint32_t number = s.building;
    Building& b = BuildingAt(ctx.world, number);
    const std::uint8_t steps = b.kind_bytes[kStepsLeft];
    if (steps == 0 || steps > 250) {
        return WaitTen(ctx);
    }
    std::uint32_t animation;
    if (HasGoods(ctx, number, 1) && b.kind_bytes[kBoardsWanted] != 0) {
        Call<kPileIncAmountLeaving>(ctx.world, BuildingOf(ctx).piles[0]);
        --b.kind_bytes[kBoardsWanted];
        --b.kind_bytes[kStepsLeft];
        SetJob(s, Job::Job316);
        animation = 0x115;
    } else if (HasGoods(ctx, number, 2) && b.kind_bytes[kIronWanted] != 0) {
        Call<kPileIncAmountLeaving>(ctx.world, BuildingOf(ctx).piles[1]);
        --b.kind_bytes[kIronWanted];
        --b.kind_bytes[kStepsLeft];
        SetJob(s, Job::Job318);
        animation = 0x117;
    } else {
        return WaitTen(ctx);
    }
    Animate(ctx, animation);
    SettlerTile(ctx).settler = static_cast<std::uint16_t>(ctx.number);
    Call<kSetSettlerTarget>(ctx.world, ctx.number, b.work_x, b.work_y);
    return JobResult::Next;
}

// 0x4f6ff4..0x4f704c, job 316 (0x13c) / 0x4f7051..0x4f70a9, job 318 (0x13e):
// takes the board / the iron.
JobResult ShipyardTake(SettlerContext& ctx, int pileSlot, Good good, Job next, std::uint32_t animation) {
    Settler& s = ctx.settler;
    Call<kTakeGoodFromPile>(ctx.world, BuildingOf(ctx).piles[pileSlot]);
    Call<kSubtractPlayerCounter>(ctx.world, s.owner, Value(good), 1);
    s.slot = Value(good);
    SetJob(s, next);
    Animate(ctx, animation);
    return JobResult::Next;
}

// 0x4f70ae..0x4f70cf, jobs 317 / 319: to the slipway, for ten work steps
// (+0x1a, counted down by job 323).
constexpr std::uint8_t kShipyardWorkSteps = 10;

JobResult ShipyardToSlipway(SettlerContext& ctx) {
    Settler& s = ctx.settler;
    Call<kMoveSettlerToBuildingTileB>(ctx.world, ctx.number);
    s.counter = 0;
    SetJob(s, Job::Job320);
    s.good = kShipyardWorkSteps;
    return JobResult::Again;
}

// The tile two steps from the shipyard's work point, away from the water
// (where the ship being built stands). Slots 20 / 14 hold the coordinates.
std::uint32_t ShipTileSettler(SettlerContext& ctx, const Building& b) {
    const std::uint32_t away = (static_cast<std::uint32_t>(b.dock_direction) + 3) % 6;
    const std::uint32_t workY = b.work_y;
    ctx.frame.slot14 = workY;
    const std::uint32_t x1 = CallStd<kNeighbourX>(b.work_x, away);
    ctx.frame.slot20 = x1;
    const std::uint32_t y1 = CallStd<kNeighbourY>(workY, away);
    ctx.frame.slot14 = y1;
    const std::uint32_t x2 = CallStd<kNeighbourX>(x1, away);
    ctx.frame.slot20 = x2;
    const std::uint32_t y2 = CallStd<kNeighbourY>(y1, away);
    ctx.frame.slot14 = y2;
    return TileAt(ctx.world, x2, y2).settler;
}

// 0x4f70d4..0x4f7359, job 323 (0x143): works on the ship; the ship (job 325)
// counts the work in +0x1c. After the last step of the last unit the ship is
// launched (its records cleared, the player notified).
JobResult ShipyardBuild(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& s = ctx.settler;
    {
        Settler& ship = SettlerAt(world, ShipTileSettler(ctx, BuildingOf(ctx)));
        if (JobOf(ship) == Job::Job325) {
            ++ship.saved_animation;
        }
    }
    if (--s.good == 0) {
        if (BuildingOf(ctx).kind_bytes[kStepsLeft] == 0) {
            Settler& ship = SettlerAt(world, ShipTileSettler(ctx, BuildingOf(ctx)));
            ship.saved_animation = 0;
            ship.saved_counter = 0;
            ship.cargo_amounts01 = 0;
            ship.cargo_amounts23 = 0;
            SetJob(ship, Job::New);
            BuildingOf(ctx).kind_bytes[kOrderState] = 0;
            ship.slot = 0;
            if (TypeOf(ship) == SettlerType::TradeShip) {
                Call<kNotifyLocalPlayer>(world, s.owner, 21, s.x, s.y);
            }
            if (TypeOf(ship) == SettlerType::Ferry) {
                Call<kNotifyLocalPlayer>(world, s.owner, 22, s.x, s.y);
            }
        }
        SetJob(s, Job::Job324);
    }
    const std::uint32_t away = (static_cast<std::uint32_t>(BuildingOf(ctx).dock_direction) + 3) % 6;
    Animate(ctx, away + 0x11a);
    return JobResult::Next;
}

// ---------------------------------------------------------------- healer (type 21, hospital)

// 0x4f745b..0x4f776a, job 333 (0x14d): heals a wounded soldier of its team
// at the door, or looks (ten offsets per turn, every other turn) for one
// around the work point and calls it in.
JobResult HealerWait(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& s = ctx.settler;
    {
        const Building& b = BuildingOf(ctx);
        const std::uint32_t patient = TileAt(world, b.door_x, b.door_y).settler;
        if (patient != 0) {
            const Settler& p = SettlerAt(world, patient);
            const std::int32_t pClass = (&g_settlerTypeClass[0])[p.art];
            if (TeamOf(world, p.owner) == TeamOf(world, s.owner) && pClass >= 1 && pClass <= 39 &&
                static_cast<std::uint16_t>(Call<kSettlerStartHealth>(world, patient)) != p.health) {
                s.prev = static_cast<std::uint16_t>(patient);
                SettlerTile(ctx).settler = static_cast<std::uint16_t>(ctx.number);
                SetJob(s, Job::Job334);
                Animate(ctx, 0x121);
                return JobResult::Next;
            }
        }
    }
    if (s.good != 0) {
        --s.good;
        s.counter = 1;
        return JobResult::Next;
    }
    if ((BuildingOf(ctx).request_flags & kRequestInputs) == 0) {
        s.counter = 1;
        return JobResult::Next;
    }
    bool found = false;
    std::uint32_t patient = 0;
    std::uint32_t k = 0;
    while (!found && k < 10) {
        const MapOffset& o = OffsetAt(world, static_cast<std::uint32_t>(s.next) + k);
        const Building& b = BuildingOf(ctx);
        const std::uint32_t x = b.work_x + static_cast<std::uint32_t>(o.dx);
        ctx.frame.slot20 = x;
        const std::uint32_t y = b.work_y + static_cast<std::uint32_t>(o.dy);
        ctx.frame.slot14 = y;
        const std::uint32_t edge = world->map_size - 1;
        if (x >= 1 && x < edge && y >= 1 && y < edge) {
            const std::uint32_t candidate = TileAt(world, x, y).settler;
            if (candidate != 0) {
                const Settler& p = SettlerAt(world, candidate);
                const std::int32_t pClass = (&g_settlerTypeClass[0])[p.art];
                if (TeamOf(world, p.owner) == TeamOf(world, s.owner) && pClass >= 1 && pClass <= 39 &&
                    JobOf(p) == Job::UnitIdle &&
                    static_cast<std::uint16_t>(Call<kSettlerStartHealth>(world, candidate)) != p.health) {
                    found = true;
                    patient = candidate;
                    continue;  // k stays
                }
            }
        }
        ++k;
    }
    s.next = static_cast<std::uint16_t>(s.next + k);
    if (k == 0) {
        ++s.next;
    }
    if (s.next >= 2000) {
        s.next = 0;
    }
    s.counter = 0;
    if (found) {
        const Building& b = BuildingOf(ctx);
        Call<kSetSettlerTarget>(world, patient, b.door_x, b.door_y);
        SetJob(SettlerAt(world, patient), Job::UnitWalk);
        s.good = 200;
    }
    s.counter = 1;
    return JobResult::Next;
}

// 0x4f776f..0x4f780a, job 334 (0x14e): the patient (+0x12) gets its full
// health back.
JobResult HealerHeal(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& s = ctx.settler;
    const std::uint32_t patient = s.prev;
    if (patient != 0) {
        Settler& p = SettlerAt(world, patient);
        if (TeamOf(world, p.owner) == TeamOf(world, s.owner) && (&g_settlerTypeClass[0])[p.art] != 0 &&
            static_cast<std::uint16_t>(Call<kSettlerStartHealth>(world, patient)) != p.health) {
            p.health = static_cast<std::uint8_t>(Call<kSettlerStartHealth>(world, patient));
        }
    }
    SetJob(s, Job::Job335);
    Animate(ctx, 0x122);
    return JobResult::Next;
}

// ---------------------------------------------------------------- soldiers, thief

// 0x4f7842..0x4f78ab, job 337 (Shoot): race 3 bowmen shoot on while they
// have arrows (slot >= 4); then back to idle.
JobResult ShootReload(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& s = ctx.settler;
    if (PlayerAt(world, s.owner).race == 3 && s.arrows >= 4) {
        --s.arrows;
        const std::int32_t race = PlayerAt(world, s.owner).race;
        s.counter = static_cast<std::uint16_t>((&g_raceShotTime[0])[race] - 1);
        return JobResult::Next;
    }
    SetJob(s, Job::UnitIdle);
    s.counter = 0;
    s.frame = 0;
    return JobResult::Again;
}

// 0x4f78b0..0x4f7a7f, job 338 (ThiefChoosePile): looks (offsets 0..1000)
// for a pile of another team that other thieves have not claimed fully,
// claims it and walks there.
JobResult ThiefChoosePile(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& s = ctx.settler;
    if (s.slot != 0) {  // already carries something
        SetJob(s, Job::UnitWalk);
        s.walk_state = 4;
        s.counter = 0;
        return JobResult::Next;
    }
    std::uint32_t best = 0;
    for (std::uint32_t k = 0; k <= 1000 && best == 0; ++k) {
        const MapOffset& o = OffsetAt(world, k);
        const std::uint32_t x = s.x + static_cast<std::uint32_t>(o.dx);
        ctx.frame.slot20 = x;
        const std::uint32_t y = s.y + static_cast<std::uint32_t>(o.dy);
        ctx.frame.slot14 = y;
        if (Call<kIsOnMap>(world, x, y) == 0) {
            continue;
        }
        const Tile& t = TileAt(world, x, y);
        const std::uint32_t pileNumber = t.object_number;
        if (pileNumber == 0 || (t.flags & kTileNoThief) != 0) {
            continue;
        }
        if (t.owner != kFreeOwner && TeamOf(world, t.owner) == TeamOf(world, s.owner)) {
            continue;
        }
        if (PileAt(world, pileNumber).amount == 0) {
            continue;
        }
        best = pileNumber;
        const std::uint32_t claims = Call<kCountOtherPileClaims>(world, ctx.number, pileNumber);
        if (static_cast<std::uint16_t>(claims) >= PileAt(world, pileNumber).amount) {
            best = 0;
        }
    }
    if (best == 0) {
        SetJob(s, Job::UnitWalk);
        s.walk_state = 4;
        s.counter = 0;
        return JobResult::Next;
    }
    Call<kAddPileClaim>(world, ctx.number, best);
    const Pile& pile = PileAt(world, best);
    if (s.x == pile.x && s.y == pile.y) {
        s.counter = 0;
        SetJob(s, Job::ThiefSteal);
        return JobResult::Next;
    }
    s.target_x = pile.x;
    s.target_y = pile.y;
    SetJob(s, Job::UnitWalk);
    s.walk_state = 3;
    s.counter = 0;
    return JobResult::Next;
}

// 0x4f7a84..0x4f7bda, job 403 (ThiefSteal): steals one unit of the pile it
// stands on and releases its claims on it; then walks home (walk state 4).
JobResult ThiefSteal(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& s = ctx.settler;
    const std::uint32_t pileNumber = SettlerTile(ctx).object_number;
    if (pileNumber == 0) {
        SetJob(s, Job::UnitWalk);
        s.walk_state = 3;
        s.counter = 0;
        GameDebugLog(texts::kThiefNoPile, ctx.number, static_cast<std::uint32_t>(s.x),
                     static_cast<std::uint32_t>(s.y));
    } else {
        ctx.frame.slot30 = pileNumber;
        const std::uint8_t good = PileAt(world, pileNumber).good;
        ctx.frame.slot5c = (ctx.frame.slot5c & 0xffffff00u) | good;
        if ((Call<kStealGoodFromPile>(world, pileNumber) & 0xff) == 0) {
            GameDebugLog(texts::kThiefStealFail, ctx.number, static_cast<std::uint32_t>(s.x),
                         static_cast<std::uint32_t>(s.y));
        } else {
            s.slot = good;
            Call<kAddPlayerCounter>(world, s.owner, good, 1);
            Call<kSubtractPlayerCounter>(world, SettlerTile(ctx).owner, good, 1);
            for (layout::PileClaim& claim : world->pile_claims) {
                if (claim.pile == pileNumber && claim.settler == ctx.number) {
                    claim.pile = 0;
                    claim.settler = 0;
                }
            }
        }
    }
    SetJob(s, Job::UnitWalk);
    s.walk_state = 4;
    s.counter = 0;
    return JobResult::Next;
}

// 0x4f7bdf..0x4f7c46, job 404 (0x194): drops what it carries and is idle.
JobResult DropAndIdle(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& s = ctx.settler;
    Call<kDropGoodNear>(world, s.slot, 1, s.x, s.y);
    Call<kSubtractPlayerCounter>(world, s.owner, s.slot, 1);
    s.slot = 0;
    s.counter = 0;
    SetJob(s, Job::UnitIdle);
    s.direction = Call<kRandomBelow6>(world);
    return JobResult::Next;
}

// ---------------------------------------------------------------- beekeeper (type 45, apiary)

// 0x4f7c59..0x4f8060, job 355 (0x163): looks (up to 50 offsets around the
// work point per turn) for a flower field to harvest (objects 250..253 with
// the tile two below in the building's area: state 2) or a free spot below
// flowers 0x44..0x52 for a hive (state 1, nothing of 247..253 within 61
// offsets).
JobResult BeekeeperSearch(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& s = ctx.settler;
    if ((BuildingOf(ctx).request_flags & kRequestInputs) == 0) {
        s.counter = kWaitTicks;
        return JobResult::Next;
    }
    std::uint32_t state = 0;
    std::uint32_t k = 0;
    for (;;) {
        if (state != 0) {
            break;
        }
        const MapOffset& o = OffsetAt(world, static_cast<std::uint32_t>(s.next) + k);
        const Building& b = BuildingOf(ctx);
        const std::uint32_t x = b.work_x + static_cast<std::uint32_t>(o.dx);
        ctx.frame.slot20 = x;
        const std::uint32_t y = b.work_y + static_cast<std::uint32_t>(o.dy);
        ctx.frame.slot14 = y;
        bool counted = true;
        if (Call<kIsOnMap>(world, x, y) != 0 && Call<kIsOnMap>(world, x, y + 1) != 0 &&
            Call<kIsOnMap>(world, x, y + 3) != 0) {
            const Building& home = BuildingOf(ctx);
            const std::uint16_t homeArea = TileAt(world, home.door_x, home.door_y).area;
            const Tile& t = TileAt(world, x, y);
            if (PileAt(world, home.pile_ring).amount <= 7 && t.object >= 250 && t.object <= 253 &&
                TileSteps(world, x, y, 2).area == homeArea) {
                state = 2;
                counted = false;
            } else if (TileSteps(world, x, y, 1).area == homeArea && t.object >= 0x44 && t.object <= 0x52 &&
                       TileSteps(world, x, y, 1).object == 0 && (TileSteps(world, x, y, 1).flags & kTileBlocking) == 0 &&
                       (TileSteps(world, x, y, 3).flags & kTileBlocking) == 0 &&
                       Call<kIsFreeOfCatapultsNearby>(world, x, y + 1) != 0) {
                state = 1;
                ctx.frame.slot14 = y + 1;
                for (std::uint32_t i = 0; i < 61; ++i) {
                    const MapOffset& n = OffsetAt(world, i);
                    const std::uint32_t xx = ctx.frame.slot20 + static_cast<std::uint32_t>(n.dx);
                    const std::uint32_t yy = ctx.frame.slot14 + static_cast<std::uint32_t>(n.dy);
                    if (Call<kIsOnMap>(world, xx, yy) == 0) {
                        continue;
                    }
                    const std::uint8_t object = TileAt(world, xx, yy).object;
                    if (object >= 247 && object <= 253) {
                        state = 0;
                        --ctx.frame.slot14;  // as the original: the next offsets are looked at one row up
                    }
                }
                counted = state == 0;
            }
        }
        if (counted) {
            ++k;
        }
        if (k >= 50) {
            break;
        }
    }
    s.next = static_cast<std::uint16_t>(s.next + k);
    if (k == 0) {
        ++s.next;
    }
    if (s.next >= 1500) {
        s.next = 0;
    }
    if (state == 0) {
        return WaitTen(ctx);
    }
    s.counter = 1;
    s.next = 0;
    const std::uint32_t x = ctx.frame.slot20;
    const std::uint32_t y = ctx.frame.slot14 + 2;
    if (Call<kCanSettlerEnterTile>(world, x, y, s.owner, s.art) == 0) {
        return JobResult::Next;
    }
    SettlerTile(ctx).settler = static_cast<std::uint16_t>(ctx.number);
    Call<kSetSettlerTarget>(world, ctx.number, x, y);
    if (state == 2) {
        SetJob(s, Job::Job361);
        s.slot = 2;
        Animate(ctx, 0x201);
    } else {
        SetJob(s, Job::Job356);
        s.slot = 1;
        Animate(ctx, 0x200);
    }
    return JobResult::Next;
}

// 0x4f8065 / 0x4f81c4, jobs 356 / 361: to the spot.
JobResult BeekeeperGo(SettlerContext& ctx, Job next) {
    Call<kMoveSettlerToBuildingTileB>(ctx.world, ctx.number);
    ctx.settler.counter = 0;
    SetJob(ctx.settler, next);
    return JobResult::Again;
}

// 0x4f8084..0x4f817c, job 358 (0x166): sets up a hive two tiles above if
// the spot is free (BlockTile), else back to the door.
JobResult BeekeeperPlaceHive(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& s = ctx.settler;
    const std::uint32_t x = s.x;
    const std::uint32_t y = s.y;
    ctx.frame.slot20 = x;
    ctx.frame.slot14 = y;
    const Building& b = BuildingOf(ctx);
    const Tile& hive = TileSteps(world, x, y, -2);
    const Tile& here = TileAt(world, x, y);
    if (hive.area == TileAt(world, b.door_x, b.door_y).area && hive.object == 0 && here.object_number == 0 &&
        (here.flags & kTileBlocking) == 0 && (hive.flags & kTileBlocking) == 0 &&
        Call<kIsFreeOfCatapultsNearby>(world, x, y - 2) != 0) {
        Call<kBlockTile>(world, static_cast<std::uint32_t>(s.x), static_cast<std::uint32_t>(s.y) - 2);
        SetJob(s, Job::Job359);
        Animate(ctx, 0x202);
        return JobResult::Next;
    }
    SetJob(s, Job::Job360);
    s.counter = 0;  // 0x4f323b
    SetTargetToDoor(ctx);
    return JobResult::Next;
}

// 0x4f8181..0x4f81bf, job 359 (0x167): the hive (object 247) is set up.
JobResult BeekeeperHiveDone(SettlerContext& ctx) {
    Settler& s = ctx.settler;
    SetJob(s, Job::Job360);
    s.slot = 0;
    s.counter = 0;
    TileSteps(ctx.world, s.x, s.y, -2).object = 247;
    SetTargetToDoor(ctx);  // 0x4f4331
    return JobResult::Next;
}

// 0x4f81e3..0x4f8241, job 363 (0x16b): harvests the hive two tiles above
// (objects 250..253) or goes home.
JobResult BeekeeperHarvest(SettlerContext& ctx) {
    Settler& s = ctx.settler;
    const std::uint8_t object = TileSteps(ctx.world, s.x, s.y, -2).object;
    if (object >= 250 && object <= 253) {
        SetJob(s, Job::Job364);
        Animate(ctx, 0x203);
        s.slot = Value(G::Honey);
        return JobResult::Next;
    }
    SetJob(s, Job::Job365);
    s.slot = 2;
    SetTargetToDoor(ctx);  // 0x4f4331
    return JobResult::Next;
}

// 0x4f8246..0x4f82bc, job 364 (0x16c): the hive is emptied by three steps.
JobResult BeekeeperHarvested(SettlerContext& ctx) {
    Settler& s = ctx.settler;
    TileSteps(ctx.world, s.x, s.y, -2).object -= 3;
    SetJob(s, Job::Job365);
    s.counter = 0;
    SetTargetToDoor(ctx);
    return JobResult::Again;
}

// 0x4f82c1..0x4f82e5, job 366 (0x16e): home; with honey (slot != 0) on to
// store it, else into the building (0x4eb3d2).
JobResult BeekeeperHome(SettlerContext& ctx) {
    Settler& s = ctx.settler;
    if (s.slot == 0) {
        Call<kMoveSettlerToBuildingTileA>(ctx.world, ctx.number);
        SetJob(s, Job::Job353);
        Animate(ctx, 0x1fd);
        return JobResult::Next;
    }
    SetJob(s, Job::Job367);
    Animate(ctx, 0x204);
    return JobResult::Next;
}

// 0x4f82ea..0x4f8386, job 367 (0x16f): one honey onto the output pile.
JobResult BeekeeperStore(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& s = ctx.settler;
    Call<kPileIncAmountComing>(world, BuildingOf(ctx).pile_ring);
    Call<kAddGoodsToPile>(world, BuildingOf(ctx).pile_ring);
    Call<kAddStatistic>(world, s.owner, Value(G::Honey), 1);
    Call<kAddPlayerCounter>(world, s.owner, Value(G::Honey), 1);
    s.slot = 0;
    SetJob(s, Job::Job368);
    Animate(ctx, 0x204);
    s.counter = 0;
    return JobResult::Next;
}

// ---------------------------------------------------------------- alchemist (type 47, laboratory)

// The piles beside the door: one row before (iron) and after (gold).
Pile& DoorPile(SettlerContext& ctx, std::int32_t side) {
    const Building& b = BuildingOf(ctx);
    return PileAt(ctx.world, TileRows(ctx.world, b.door_x, b.door_y, side).object_number);
}
std::uint32_t DoorPileNumber(SettlerContext& ctx, std::int32_t side) {
    const Building& b = BuildingOf(ctx);
    return TileRows(ctx.world, b.door_x, b.door_y, side).object_number;
}

// 0x4f83a7..0x4f84f3, job 341 (0x155): starts when both inputs are there
// and a door pile has room.
JobResult AlchemistStart(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& s = ctx.settler;
    if ((BuildingOf(ctx).request_flags & kRequestInputs) == 0) {
        s.counter = kWaitTicks;
        return JobResult::Next;
    }
    if (!HasGoods(ctx, s.building, 1) || !HasGoods(ctx, s.building, 2)) {
        return WaitTen(ctx);
    }
    const Building& b = BuildingOf(ctx);
    if (DoorPile(ctx, -1).amount > 5 && DoorPile(ctx, 1).amount > 5) {
        s.counter = kWaitTicks;
        return JobResult::Next;
    }
    Call<kPileIncAmountLeaving>(world, b.piles[0]);
    Call<kPileIncAmountLeaving>(world, BuildingOf(ctx).piles[1]);
    SettlerTile(ctx).settler = static_cast<std::uint16_t>(ctx.number);
    SetJob(s, Job::Job342);
    Animate(ctx, 0x1f2);
    return JobResult::Next;
}

// Takes one unit of input `pileSlot` (the good is read after the take).
void TakeInput(SettlerContext& ctx, int pileSlot) {
    CGameData* world = ctx.world;
    Call<kTakeGoodFromPile>(world, BuildingOf(ctx).piles[pileSlot]);
    const std::uint8_t good = PileAt(world, BuildingOf(ctx).piles[pileSlot]).good;
    Call<kSubtractPlayerCounter>(world, ctx.settler.owner, good, 1);
}

// A step that only sets the next job and an animation.
JobResult AnimateStep(SettlerContext& ctx, Job next, std::uint32_t animation) {
    SetJob(ctx.settler, next);
    Animate(ctx, animation);
    return JobResult::Next;
}

// 0x4f85b5..0x4f8766, job 346 (0x15a): rolls how much iron (+0x17) and gold
// (+0x18) come out, each 0..3 (20% 0, 50% 1, 20% 2, 10% 3), capped by the
// room in the door piles (8 units).
std::uint32_t RollYield(std::uint32_t roll) {
    if (roll <= 0x3333) {
        return 0;
    }
    if (roll <= 0xb333) {
        return 1;
    }
    return roll > 0xe665 ? 3 : 2;
}

JobResult AlchemistRoll(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& s = ctx.settler;
    const std::uint32_t roll1 = Call<kRandom>(world) & 0xffff;
    const std::uint32_t roll2 = Call<kRandom>(world) & 0xffff;
    std::uint32_t iron = RollYield(roll1);
    const std::uint8_t ironRoom = DoorPile(ctx, -1).amount;
    if (static_cast<std::int32_t>(ironRoom + (iron & 0xffff)) > 8) {
        iron = 8u - ironRoom;
    }
    std::uint32_t gold = RollYield(roll2);
    const std::uint8_t goldRoom = DoorPile(ctx, 1).amount;
    if (static_cast<std::int32_t>(goldRoom + (gold & 0xffff)) > 8) {
        gold = 8u - goldRoom;
    }
    s.cargo_amounts01 = static_cast<std::uint8_t>(iron);
    s.cargo_amounts23 = static_cast<std::uint8_t>(gold);
    const bool someIron = static_cast<std::uint16_t>(iron) != 0;
    const bool someGold = static_cast<std::uint16_t>(gold) != 0;
    if (someIron) {
        if (someGold) {
            Animate(ctx, 0x1f9);
            SetJob(s, Job::Job349);
        } else {
            Animate(ctx, 0x1f8);
            SetJob(s, Job::Job348);
        }
    } else if (someGold) {
        Animate(ctx, 0x1f7);
        SetJob(s, Job::Job347);
    } else {
        Animate(ctx, 0x1fa);
        SetJob(s, Job::Job350);
    }
    return JobResult::Next;
}

// 0x4f876b..0x4f8a90, jobs 347..350: puts the gold (+0x18) and the iron
// (+0x17) onto the door piles, counts them, and starts again if it can.
JobResult AlchemistStore(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& s = ctx.settler;
    if (s.cargo_amounts23 >= 1) {
        for (std::uint32_t unit = 1;; ++unit) {
            Call<kPileIncAmountComing>(world, DoorPileNumber(ctx, 1));
            Call<kAddGoodsToPile>(world, DoorPileNumber(ctx, 1));
            if (static_cast<std::uint16_t>(unit + 1) > s.cargo_amounts23) {
                break;
            }
        }
    }
    Call<kAddPlayerCounter>(world, s.owner, Value(G::Gold), s.cargo_amounts23);
    Call<kAddStatistic>(world, s.owner, Value(G::Gold), s.cargo_amounts23);
    (&g_playerTallies[0] + s.owner)->points += 2 * static_cast<std::int32_t>(s.cargo_amounts23);
    if (s.cargo_amounts01 >= 1) {
        for (std::uint32_t unit = 1;; ++unit) {
            Call<kPileIncAmountComing>(world, DoorPileNumber(ctx, -1));
            Call<kAddGoodsToPile>(world, DoorPileNumber(ctx, -1));
            if (static_cast<std::uint16_t>(unit + 1) > s.cargo_amounts01) {
                break;
            }
        }
    }
    Call<kAddPlayerCounter>(world, s.owner, Value(G::Iron), s.cargo_amounts01);
    Call<kAddStatistic>(world, s.owner, Value(G::Iron), s.cargo_amounts01);
    s.cargo_amounts01 = 0;
    s.cargo_amounts23 = 0;
    if (HasGoods(ctx, s.building, 1) && HasGoods(ctx, s.building, 2) &&
        (BuildingOf(ctx).request_flags & kRequestInputs) != 0 &&
        (DoorPile(ctx, -1).amount <= 5 || DoorPile(ctx, 1).amount <= 5)) {
        const Building& b = BuildingOf(ctx);
        Call<kPileIncAmountLeaving>(world, b.piles[0]);
        Call<kPileIncAmountLeaving>(world, BuildingOf(ctx).piles[1]);
        return AnimateStep(ctx, Job::Job351, 0x1fb);
    }
    return AnimateStep(ctx, Job::Job352, 0x1fc);
}

// ---------------------------------------------------------------- mead maker (type 46, meadery)

// 0x4f8b5d..0x4f8c57, job 382 (0x17e): starts when both inputs are there
// and the output pile has room.
JobResult MeadStart(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& s = ctx.settler;
    if ((BuildingOf(ctx).request_flags & kRequestInputs) == 0 || !HasGoods(ctx, s.building, 1) ||
        !HasGoods(ctx, s.building, 2)) {
        return WaitTen(ctx);
    }
    const Building& b = BuildingOf(ctx);
    if (PileAt(world, b.pile_ring).amount > 7) {
        return WaitTen(ctx);
    }
    Call<kPileIncAmountLeaving>(world, b.piles[0]);
    Call<kPileIncAmountLeaving>(world, BuildingOf(ctx).piles[1]);
    SettlerTile(ctx).settler = static_cast<std::uint16_t>(ctx.number);
    SetJob(s, Job::Job383);
    Animate(ctx, 0x208);
    return JobResult::Next;
}

// 0x4f8da0..0x4f8ef5, job 387 (0x183): one mead onto the output pile, and
// again if it can.
JobResult MeadStore(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& s = ctx.settler;
    Call<kPileIncAmountComing>(world, BuildingOf(ctx).pile_ring);
    Call<kAddGoodsToPile>(world, BuildingOf(ctx).pile_ring);
    Call<kAddPlayerCounter>(world, s.owner, Value(G::Mead), 1);
    Call<kAddStatistic>(world, s.owner, Value(G::Mead), 1);
    if (HasGoods(ctx, s.building, 1) && HasGoods(ctx, s.building, 2)) {
        const Building& b = BuildingOf(ctx);
        if ((b.request_flags & kRequestInputs) != 0 && PileAt(world, b.pile_ring).amount <= 7) {
            Call<kPileIncAmountLeaving>(world, b.piles[0]);
            Call<kPileIncAmountLeaving>(world, BuildingOf(ctx).piles[1]);
            return AnimateStep(ctx, Job::Job389, 0x20e);
        }
    }
    return AnimateStep(ctx, Job::Job388, 0x20d);
}

// ---------------------------------------------------------------- gong order (war machine workshops)

// 0x4f8f70..0x4f911d, job 401 (0x191): fetches boards, iron or gold for the
// order (+0x20 steps left).
JobResult GongFetch(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& s = ctx.settler;
    const std::uint32_t number = s.building;
    Building& b = BuildingAt(world, number);
    const std::uint8_t steps = b.kind_bytes[kStepsLeft];
    if (steps == 0 || steps > 250) {
        return WaitTen(ctx);
    }
    struct Input {
        int slot;
        int wanted;
        Good good;
    };
    static constexpr Input kInputs[] = {{0, kBoardsWanted, G::Board}, {1, kIronWanted, G::Iron}, {2, kGoldWanted, G::Gold}};
    bool fetched = false;
    for (const Input& input : kInputs) {
        if (HasGoods(ctx, number, input.slot + 1) && b.kind_bytes[input.wanted] != 0) {
            Call<kPileIncAmountLeaving>(world, BuildingOf(ctx).piles[input.slot]);
            Call<kTakeGoodFromPile>(world, b.piles[input.slot]);
            Call<kSubtractPlayerCounter>(world, s.owner, Value(input.good), 1);
            --b.kind_bytes[input.wanted];
            fetched = true;
            break;
        }
    }
    if (!fetched) {
        return WaitTen(ctx);
    }
    --b.kind_bytes[kStepsLeft];
    Animate(ctx, 0x113);
    SetJob(s, Job::GongProduction);
    return JobResult::Next;
}

// 0x4f9122..0x4f91f7, job 402 (GongProduction, "Gongproduktion"): after the
// last step of an ordered gong (+0x1f = 0x13) the gong appears as a
// seven-tile unit at the extra tiles' centre.
JobResult GongProduction(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& s = ctx.settler;
    const std::uint32_t number = s.building;
    Building& b = BuildingAt(world, number);
    if (b.kind_bytes[kStepsLeft] == 0 && b.kind_bytes[kOrderState] != 0 && b.kind_bytes[kOrderState] == kGongOrdered) {
        Call<kReleaseBuildingExtraTiles>(world, number);
        const std::uint32_t cx = static_cast<std::uint32_t>(g_extraTilesCentre[3][0]);
        const std::uint32_t cy = static_cast<std::uint32_t>(g_extraTilesCentre[3][1]);
        Call<kCreateSevenTileUnit>(world, Value(SettlerType::Catapult), s.owner, b.x + cx, b.y + cy);
        GameDebugLog(texts::kGongProduction, static_cast<std::uint32_t>(s.owner));
        b.kind_bytes[kOrderState] = 0;
        const std::uint32_t y = b.y + static_cast<std::uint32_t>(g_extraTilesCentre[3][1]);
        const std::uint32_t x = b.x + static_cast<std::uint32_t>(g_extraTilesCentre[3][0]);
        Call<kNotifyLocalPlayer>(world, s.owner, 41, x, y);
    }
    SetJob(s, Job::Job401);
    s.counter = 0;
    return JobResult::Next;
}

}  // namespace

JobResult RunSpecialJobs(SettlerContext& ctx) {
    Settler& s = ctx.settler;
    switch (static_cast<Job>(ctx.job)) {
    case Job::Job23:
    case Job::Job141:
    case Job::Job325:
    case Job::OnBoard: return WaitTen(ctx);  // 0x4f8b7c
    case Job::Job304: s.counter = 100; return JobResult::Next;  // 0x4f5f6b
    case Job::Job308: return DonkeyLoad(ctx);
    case Job::Job310: return DonkeyUnload(ctx);
    case Job::Job331: return ShipLoadAtHarbour(ctx);
    case Job::ShipUnload: return ShipUnload(ctx);
    // Shipyard.
    case Job::Job314: SetJob(s, Job::Job315); return CountUpAndLeaveTile(ctx);  // 0x4f6e84
    case Job::Job315: return ShipyardFetch(ctx);
    case Job::Job316: return ShipyardTake(ctx, 0, G::Board, Job::Job317, 0x116);
    case Job::Job318: return ShipyardTake(ctx, 1, G::Iron, Job::Job319, 0x118);
    case Job::Job317:
    case Job::Job319: return ShipyardToSlipway(ctx);
    case Job::Job323: return ShipyardBuild(ctx);
    case Job::Job324:  // 0x4f735e
        s.slot = 0;
        SetJob(s, Job::Job321);
        SetTargetToDoor(ctx);
        s.counter = 0;
        return JobResult::Again;
    case Job::Job322:  // 0x4f73b2
        SetJob(s, Job::Job315);
        SettlerTile(ctx).settler = 0;
        s.counter = 0;
        return JobResult::Again;
    // Healer.
    case Job::Job336:  // 0x4f73ec
        SetJob(s, Job::Job333);
        ++BuildingOf(ctx).kind_bytes[kUnitsDone];
        SettlerTile(ctx).settler = 0;
        s.good = 0;
        s.counter = 0;
        s.next = 0;
        return JobResult::Next;
    case Job::Job333: return HealerWait(ctx);
    case Job::Job334: return HealerHeal(ctx);
    case Job::Job335:  // 0x4f780f
        SettlerTile(ctx).settler = 0;
        SetJob(s, Job::Job333);
        s.counter = 0;
        return JobResult::Next;
    case Job::Shoot: return ShootReload(ctx);
    case Job::ThiefChoosePile: return ThiefChoosePile(ctx);
    case Job::ThiefSteal: return ThiefSteal(ctx);
    case Job::Job404: return DropAndIdle(ctx);
    // Beekeeper.
    case Job::Job353:  // 0x4f7c4b + 0x4ece3b
        SetJob(s, Job::Job355);
        ++BuildingOf(ctx).kind_bytes[kUnitsDone];
        SettlerTile(ctx).settler = 0;
        s.next = 0;
        s.counter = 0;
        return JobResult::Next;
    case Job::Job355: return BeekeeperSearch(ctx);
    case Job::Job356: return BeekeeperGo(ctx, Job::Job357);
    case Job::Job358: return BeekeeperPlaceHive(ctx);
    case Job::Job359: return BeekeeperHiveDone(ctx);
    case Job::Job361: return BeekeeperGo(ctx, Job::Job362);
    case Job::Job363: return BeekeeperHarvest(ctx);
    case Job::Job364: return BeekeeperHarvested(ctx);
    case Job::Job366: return BeekeeperHome(ctx);
    case Job::Job367: return BeekeeperStore(ctx);
    case Job::Job369: return LeaveTile(ctx, Job::Job355);  // 0x4f838b
    // Alchemist.
    case Job::Job339: SetJob(s, Job::Job341); return CountUpAndLeaveTile(ctx);  // 0x4f8399
    case Job::Job341: return AlchemistStart(ctx);
    case Job::Job342:
    case Job::Job351:  // 0x4f8a95
        TakeInput(ctx, 0);
        return AnimateStep(ctx, Job::Job343, 0x1f3);
    case Job::Job343: return AnimateStep(ctx, Job::Job344, 0x1f4);
    case Job::Job344:  // 0x4f8513
        TakeInput(ctx, 1);
        return AnimateStep(ctx, Job::Job345, 0x1f5);
    case Job::Job345: return AnimateStep(ctx, Job::Job346, 0x1f6);
    case Job::Job346: return AlchemistRoll(ctx);
    case Job::Job347:
    case Job::Job348:
    case Job::Job349:
    case Job::Job350: return AlchemistStore(ctx);
    case Job::Job352: return LeaveTile(ctx, Job::Job341);  // 0x4f8b1c
    // Mead maker.
    case Job::Job380: SetJob(s, Job::Job382); return CountUpAndLeaveTile(ctx);  // 0x4f8b4f
    case Job::Job382: return MeadStart(ctx);
    case Job::Job383: return AnimateStep(ctx, Job::Job384, 0x209);
    case Job::Job384:  // 0x4f8c77
        TakeInput(ctx, 0);
        return AnimateStep(ctx, Job::Job385, 0x20a);
    case Job::Job385:  // 0x4f8cfe
        TakeInput(ctx, 1);
        return AnimateStep(ctx, Job::Job386, 0x20b);
    case Job::Job386: return AnimateStep(ctx, Job::Job387, 0x20c);
    case Job::Job387: return MeadStore(ctx);
    case Job::Job388: return LeaveTile(ctx, Job::Job382);  // 0x4f8efa
    case Job::Job389: SetJob(s, Job::Job384); return JobResult::Next;  // 0x4f8f2d
    // Gong order.
    case Job::Job400: SetJob(s, Job::Job401); return CountUpAndLeaveTile(ctx);  // 0x4f8f3b
    case Job::Job401: return GongFetch(ctx);
    case Job::GongProduction: return GongProduction(ctx);
    default: return JobResult::Next;  // not a job of this family (kJobFamily)
    }
}

}  // namespace s3re::game::settler_jobs

// ---------------------------------------------------------------------------
// Tests (harness: settler_jobs_test.h)
// ---------------------------------------------------------------------------

namespace s3re::game {

namespace {

using settler_jobs::BuildingAt;
using settler_jobs::PileAt;
using settler_jobs::SettlerAt;
using settler_jobs::testing::CaseSetup;
using settler_jobs::testing::FamilyTest;
using Job = SettlerJob;

constexpr Job kSpecialJobs[] = {
    Job::Job23,  Job::Job141, Job::Job325, Job::OnBoard, Job::Job304, Job::Job308, Job::Job310, Job::Job331,
    Job::ShipUnload, Job::Job314, Job::Job315, Job::Job316, Job::Job318, Job::Job317, Job::Job319, Job::Job323,
    Job::Job324, Job::Job322, Job::Job336, Job::Job333, Job::Job334, Job::Job335, Job::Shoot,
    Job::ThiefChoosePile, Job::ThiefSteal, Job::Job404, Job::Job353, Job::Job355, Job::Job356, Job::Job358,
    Job::Job359, Job::Job361, Job::Job363, Job::Job364, Job::Job366, Job::Job367, Job::Job369, Job::Job339,
    Job::Job341, Job::Job342, Job::Job351, Job::Job343, Job::Job344, Job::Job345, Job::Job346, Job::Job347,
    Job::Job348, Job::Job349, Job::Job350, Job::Job352, Job::Job380, Job::Job382, Job::Job383, Job::Job384,
    Job::Job385, Job::Job386, Job::Job387, Job::Job388, Job::Job389, Job::Job400, Job::Job401,
    Job::GongProduction};
static_assert(sizeof(kSpecialJobs) / sizeof(kSpecialJobs[0]) == 62);
// The jobs drawn by the test: all, plus the long handlers once more.
constexpr Job kSpecialJobsDrawn[] = {
    Job::Job23,  Job::Job141, Job::Job325, Job::OnBoard, Job::Job304, Job::Job308, Job::Job310, Job::Job331,
    Job::ShipUnload, Job::Job314, Job::Job315, Job::Job316, Job::Job318, Job::Job317, Job::Job319, Job::Job323,
    Job::Job324, Job::Job322, Job::Job336, Job::Job333, Job::Job334, Job::Job335, Job::Shoot,
    Job::ThiefChoosePile, Job::ThiefSteal, Job::Job404, Job::Job353, Job::Job355, Job::Job356, Job::Job358,
    Job::Job359, Job::Job361, Job::Job363, Job::Job364, Job::Job366, Job::Job367, Job::Job369, Job::Job339,
    Job::Job341, Job::Job342, Job::Job351, Job::Job343, Job::Job344, Job::Job345, Job::Job346, Job::Job347,
    Job::Job348, Job::Job349, Job::Job350, Job::Job352, Job::Job380, Job::Job382, Job::Job383, Job::Job384,
    Job::Job385, Job::Job386, Job::Job387, Job::Job388, Job::Job389, Job::Job400, Job::Job401,
    Job::GongProduction,
    Job::Job308, Job::Job310, Job::Job310, Job::Job331, Job::Job331, Job::ShipUnload, Job::ShipUnload,
    Job::Job333, Job::Job333, Job::Job355, Job::Job358, Job::Job358, Job::Job323, Job::GongProduction,
    Job::GongProduction, Job::Job346, Job::Job347};

constexpr SettlerType kSpecialTypes[] = {
    SettlerType::ShipyardWorker, SettlerType::Healer,    SettlerType::Thief,     SettlerType::Beekeeper,
    SettlerType::Alchemist,      SettlerType::MeadMaker, SettlerType::TradeShip, SettlerType::Ferry,
    SettlerType::Donkey,         SettlerType::Bowman1,   SettlerType::Bowman3};

// A coordinate within r of v.
std::uint16_t Near(test::Rng& rng, std::uint32_t v, std::uint32_t r) {
    return static_cast<std::uint16_t>(v - r + rng.Below(2 * r + 1));
}

// The settler's building with a trade route (often ending here, its points
// in the settler's region), ring piles, door piles, the pile under the
// settler (whose +0xa names the building), flowers / hives / free spots
// around the work point; players 0..3 in two teams; small map offsets.
void CustomizeSpecial(CaseSetup& setup, std::uint32_t n) {
    auto& rng = setup.rng();
    std::uint16_t sx = 0, sy = 0, building = 0;
    std::uint8_t owner = 0;
    setup.Both([&](CGameData* w) {
        const layout::Settler& s = SettlerAt(w, n);
        sx = s.x;
        sy = s.y;
        building = s.building;
        owner = s.owner;
    });
    static constexpr std::uint8_t kArts[] = {0x21, 0x22, 0x21, 0x22, 0x20, 0x33, 0x34, 0x35, 0x23};
    const std::uint8_t art =
        rng.Chance(4, 5) ? kArts[rng.Below(sizeof(kArts))] : static_cast<std::uint8_t>(rng.Below(0x39));
    const auto buildingOwner = static_cast<std::uint8_t>(rng.Chance(4, 5) ? owner : rng.Below(4));
    const auto buildup = static_cast<std::uint8_t>(rng.Chance(4, 5) ? 0 : 1);
    const auto trade = static_cast<std::uint16_t>(1 + rng.Below(20));
    const auto requests = static_cast<std::uint8_t>(rng.Next32());
    std::uint8_t kinds[8];
    for (std::uint8_t& k : kinds) {
        static constexpr std::uint8_t kValues[] = {0, 1, 2, 3, 0x13, 250, 251, 0x10};
        k = kValues[rng.Below(sizeof(kValues))];
    }
    if (rng.Chance(1, 3)) {  // an ordered gong, all steps done
        kinds[6] = 0x13;
        kinds[7] = 0;
    }
    const auto dock = static_cast<std::uint16_t>(rng.Chance(9, 10) ? rng.Below(6) : rng.Next32());
    const std::uint16_t bx = Near(rng, sx, 4), by = Near(rng, sy, 4);
    const std::uint16_t workX = Near(rng, sx, 3), workY = Near(rng, sy, 3);
    const std::uint16_t doorX = Near(rng, sx, 3), doorY = Near(rng, sy, 3);
    const auto next = static_cast<std::uint16_t>(rng.Chance(9, 10) ? rng.Below(2100) : rng.Next32());
    std::uint16_t ring[6];
    std::uint8_t ringAmounts[6], ringLeaving[6];
    for (int k = 0; k < 6; ++k) {
        ring[k] = static_cast<std::uint16_t>(1 + rng.Below(settler_jobs::testing::kPiles));
        ringAmounts[k] = static_cast<std::uint8_t>(rng.Chance(1, 3) ? 0 : rng.Below(9));
        ringLeaving[k] = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : rng.Below(4));
    }
    std::uint16_t route[2][4];
    const bool atEnd = rng.Chance(3, 4);
    for (int k = 0; k < 4; ++k) {
        const bool unset = rng.Chance(1, 4) && !(k == 3 && atEnd);
        route[0][k] = unset ? 0xffff : Near(rng, sx, 6);
        route[1][k] = unset ? 0xffff : Near(rng, sy, 6);
    }
    if (atEnd) {
        route[0][3] = sx;
        route[1][3] = sy;
    }
    const bool sameRegion = rng.Chance(4, 5);
    const bool pileHere = rng.Chance(2, 3);
    const bool hiveSpot = rng.Chance(1, 2);
    const auto hiveObject = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : 250 + rng.Below(4));
    const std::uint16_t tilePile = static_cast<std::uint16_t>(1 + rng.Below(settler_jobs::testing::kPiles));
    const auto tilePileAmount = static_cast<std::uint8_t>(rng.Below(5));
    const auto tilePileLeaving = static_cast<std::uint8_t>(rng.Below(3));
    const std::uint16_t doorPileA = setup.PileNumber(), doorPileB = setup.PileNumber();
    const auto amountA = static_cast<std::uint8_t>(rng.Below(9)), amountB = static_cast<std::uint8_t>(rng.Below(9));
    const auto slot = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : rng.Below(8));
    const auto good = static_cast<std::uint8_t>(rng.Chance(1, 3) ? 0 : rng.Chance(1, 2) ? 1 : rng.Below(12));
    struct Spot {
        std::uint16_t x, y;
        std::uint8_t object;
    };
    Spot spots[6];
    for (Spot& spot : spots) {
        static constexpr std::uint8_t kObjects[] = {250, 251, 252, 253, 0x44, 0x4a, 0x52, 0, 247, 0xf8};
        spot = {Near(rng, workX, 3), Near(rng, workY, 3), kObjects[rng.Below(sizeof(kObjects))]};
    }
    // Soldiers of the settler's player that a healer may take in (at the
    // door and around the work point), and a ship on the shipyard's ship
    // tile (two steps from the work point, away from the landing).
    const std::uint32_t away = (static_cast<std::uint32_t>(dock) + 3) % 6;
    const std::uint32_t shipX = settler_jobs::CallStd<settler_jobs::kNeighbourX>(
        settler_jobs::CallStd<settler_jobs::kNeighbourX>(workX, away), away);
    const std::uint32_t shipY = settler_jobs::CallStd<settler_jobs::kNeighbourY>(
        settler_jobs::CallStd<settler_jobs::kNeighbourY>(workY, away), away);
    struct Patient {
        std::uint16_t number, x, y;
        std::uint8_t health;
        bool idle;
    };
    Patient patients[4];
    for (int k = 0; k < 4; ++k) {
        patients[k].number = static_cast<std::uint16_t>(7 + rng.Below(settler_jobs::testing::kSettlers - 6));
        // On one of the ten offsets the healer looks at (offset table below).
        const std::uint32_t j = next + rng.Below(10);
        patients[k].x = k == 0 && rng.Chance(1, 2) ? doorX : static_cast<std::uint16_t>(workX + static_cast<std::int32_t>(j % 11) - 5);
        patients[k].y = k == 0 ? doorY : static_cast<std::uint16_t>(workY + static_cast<std::int32_t>((j / 11) % 11) - 5);
        patients[k].health = static_cast<std::uint8_t>(rng.Chance(1, 4) ? 100 : rng.Below(100));
        patients[k].idle = rng.Chance(3, 4);
    }
    const auto ship = static_cast<std::uint16_t>(7 + rng.Below(settler_jobs::testing::kSettlers - 6));
    const std::uint8_t shipType = rng.Chance(1, 2) ? Value(SettlerType::TradeShip) : Value(SettlerType::Ferry);
    const bool shipThere = rng.Chance(3, 4);
    const bool shipBuilt = rng.Chance(3, 4);
    setup.Both([&](CGameData* w) {
        for (std::uint32_t p = 0; p < 4; ++p) {
            w->players[p].team = static_cast<std::int32_t>(p & 1);
        }
        for (const Patient& p : patients) {
            layout::Settler& m = SettlerAt(w, p.number);
            m.owner = owner;
            m.art = Value(SettlerType::Swordsman1);
            m.health = p.health;
            SetJob(m, p.idle ? Job::UnitIdle : Job::Nothing);
            w->tiles[p.x][p.y].settler = p.number;
        }
        if (shipThere && shipX < 768 && shipY < 768) {
            layout::Settler& m = SettlerAt(w, ship);
            m.art = shipType;
            SetJob(m, shipBuilt ? Job::Job325 : Job::Job304);
            w->tiles[shipX][shipY].settler = ship;
        }
        for (std::uint32_t k = 0; k < 2200; ++k) {
            layout::MapOffset& o = w->offsets[k];
            o.dx = static_cast<std::int32_t>(k % 11) - 5;
            o.dy = static_cast<std::int32_t>((k / 11) % 11) - 5;
        }
        layout::Settler& s = SettlerAt(w, n);
        s.next = next;
        s.slot = slot;
        s.good = good;
        layout::Building& b = BuildingAt(w, building);
        b.art = art;
        b.owner = buildingOwner;
        b.buildup = buildup;
        b.trade = trade;
        b.request_flags = requests;
        for (int k = 0; k < 8; ++k) {
            b.kind_bytes[k] = kinds[k];
        }
        b.dock_direction = dock;
        b.x = bx;
        b.y = by;
        b.work_x = workX;
        b.work_y = workY;
        b.door_x = doorX;
        b.door_y = doorY;
        b.pile_ring = ring[0];
        for (int k = 0; k < 6; ++k) {
            layout::Pile& p = PileAt(w, ring[k]);
            p.next = ring[(k + 1) % 6];
            p.amount = ringAmounts[k];
            p.leaving = ringLeaving[k];
        }
        layout::Trade& t = w->trades[trade];
        const std::uint16_t area = w->tiles[sx][sy].area;
        const std::uint8_t region = w->tiles[sx][sy].walk_region;
        for (int k = 0; k < 4; ++k) {
            t.route_x[k] = route[0][k];
            t.route_y[k] = route[1][k];
            if (route[0][k] != 0xffff && sameRegion) {
                w->tiles[route[0][k]][route[1][k]].area = area;
                w->tiles[route[0][k]][route[1][k]].walk_region = region;
            }
        }
        w->tiles[sx][sy].object_number = pileHere ? tilePile : 0;
        if (hiveSpot) {
            w->tiles[sx][sy].flags &= 0xf6;
            w->tiles[sx][sy - 2].flags &= 0xf6;
            w->tiles[sx][sy - 2].object = hiveObject;
            w->tiles[sx][sy - 2].area = w->tiles[doorX][doorY].area;
        }
        layout::Pile& tp = PileAt(w, tilePile);
        tp.prev = building;
        PileAt(w, 0).prev = building;  // pile 0 (no pile here) names a building with a sane route too
        tp.amount = tilePileAmount;
        tp.leaving = tilePileLeaving;
        w->tiles[doorX - 1][doorY].object_number = doorPileA;
        w->tiles[doorX + 1][doorY].object_number = doorPileB;
        PileAt(w, doorPileA).amount = amountA;
        PileAt(w, doorPileB).amount = amountB;
        for (const Spot& spot : spots) {
            w->tiles[spot.x][spot.y].object = spot.object;
            w->tiles[spot.x][spot.y].flags &= 0xf6;
            w->tiles[spot.x][spot.y + 1].object = 0;
            w->tiles[spot.x][spot.y + 1].flags &= 0xf6;
            w->tiles[spot.x][spot.y + 1].area = w->tiles[doorX][doorY].area;
            w->tiles[spot.x][spot.y + 2].area = w->tiles[doorX][doorY].area;
            w->tiles[spot.x][spot.y + 3].flags &= 0xf6;
        }
    });
}

FamilyTest SpecialFamily() {
    FamilyTest family{kSpecialJobsDrawn, sizeof(kSpecialJobsDrawn) / sizeof(kSpecialJobsDrawn[0])};
    family.types = kSpecialTypes;
    family.typeCount = sizeof(kSpecialTypes) / sizeof(kSpecialTypes[0]);
    family.customize = &CustomizeSpecial;
    family.minCompared = 2000;
    return family;
}

// Wrong: the alchemist's iron yield (+0x17) is one off after its roll.
std::uint32_t __fastcall WrongUpdateSettlersYield(CGameData* world, void* edx) {
    std::vector<std::uint32_t> rolling;
    for (std::uint32_t n = 1; n <= world->settler_high_water; ++n) {
        if (JobOf(world->settlers[n]) == Job::Job346) {
            rolling.push_back(n);
        }
    }
    const std::uint32_t result = CGameData_UpdateSettlers(world, edx);
    for (const std::uint32_t n : rolling) {
        world->settlers[n].food ^= 1;
    }
    return result;
}

}  // namespace

S3RE_TEST(CGameData_UpdateSettlers_Special_MatchesOriginal) {
    settler_jobs::testing::CompareUpdateSettlers(ctx, &CGameData_UpdateSettlers, SpecialFamily(), 4000);
}

S3RE_TEST(CGameData_UpdateSettlers_Special_ComparisonDetectsWrongYield) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    FamilyTest family = SpecialFamily();
    family.minCompared = 0;
    settler_jobs::testing::CompareUpdateSettlers(probe, &WrongUpdateSettlersYield, family, 4000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
