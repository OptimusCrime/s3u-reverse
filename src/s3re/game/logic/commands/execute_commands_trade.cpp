// Part of CLogicThread::ExecuteCommands (0x535990), see execute_commands.h:
// the trade route of harbours and marketplaces (command 0x12) and the
// production of the war machine workshops and the shipyard (0x13).
//
// SetTradeRoute (0x12, 0x5377cf..0x538589). Payload: player, kind,
// building (word), then per kind. A finished harbour (33) or marketplace
// (34), of any owner (the player only decides the UI refresh), changes its
// Trade record (Building +0x34):
//   2      mode (+0x01) = payload byte 4
//   3      the route is cleared (all four points 0xffff)
//   4..6   route point kind - 4 = (x, y) (words at payload 4, 6), unless
//          another point already is that tile. A marketplace needs the tile
//          in the walk region (Tile +0x10) of its door; a harbour (with a
//          landing direction, +0x22) needs it in the area (Tile +0x14) of
//          the water tile two steps from its work tile against the landing
//          direction.
//   7      the end point: marketplace as above (the three points cleared);
//          a harbour takes the first tile around (x, y) (MapOffset table,
//          distance up to 20) on the map in that water area, if (x, y) has
//          a walk region at all.
//   8..11  orders of the trade menu line payload byte 4 (goods type from
//          table 0x659414; Trade +0x12): 8 = 100, 9 = 0, 10 += byte 5 (up to
//          100), 11 -= byte 5 (down to 0).
// Then, if the player is the local player with the building menu (4) on a
// trade page (special menu 4..13), the UI is refreshed, and for kinds 3..7
// on the selected building its route overlay is drawn again
// (DrawBuildingRouteOverlay 0x5236b0).
//
// SetWorkshopProduction (0x13, 0x53858a..0x53888d). Payload: player, value,
// building (word). Value 0 stops: the state (+0x1f), the extra-tiles byte
// (+0x20) and the wanted inputs (+0x1a..+0x1c) are cleared, after
// ReleaseBuildingExtraTiles (0x4dda10) if the production was running with
// its tiles taken. Otherwise value = kind (high nibble) + race (low
// nibble, must be the player's race): a finished workshop of the player
// (31, 47, 48, 50) starts kind 0x10 or 0x20, a finished shipyard (32, of
// any owner) with a slipway (+0x22) kind 0x30 or 0x40 (the ship type), if
// idle: state = kind + race, extra tiles 0xff. Then a UI refresh for the
// local player in the building menu on special menus 14..16.

#include "game/logic/commands/execute_commands.h"

#include "game/globals.h"
#include "game/tables.h"
#include "game/typed_fields.h"

namespace s3re::game::commands {

namespace {

using layout::Building;
using layout::Trade;
using layout::Tile;

constexpr std::uintptr_t kDrawBuildingRouteOverlay = 0x005236b0;  // ECX = the thread
constexpr std::uintptr_t kReleaseBuildingExtraTiles = 0x004dda10;

constexpr std::uint16_t kNoPoint = 0xffff;
constexpr std::uint16_t kNoDirection = 0xffff;
constexpr std::uint32_t kRoutePoints = 4;
constexpr std::uint32_t kEndPoint = 3;
constexpr std::uint8_t kFullOrder = 100;
// MapOffset distance up to which a harbour's end point may move (kind 7).
constexpr std::int32_t kEndPointRange = 20;

// SetTradeRoute kinds.
constexpr std::uint32_t kSetMode = 2;
constexpr std::uint32_t kClearRoute = 3;
constexpr std::uint32_t kSetPoint1 = 4;
constexpr std::uint32_t kSetEndPoint = 7;
constexpr std::uint32_t kOrderFull = 8;
constexpr std::uint32_t kOrderNone = 9;
constexpr std::uint32_t kOrderMore = 10;
constexpr std::uint32_t kOrderLess = 11;

// UI pages of the building menu.
constexpr std::int32_t kMenuBuilding = 4;
constexpr std::uint32_t kFirstTradePage = 4;
constexpr std::uint32_t kLastTradePage = 13;
constexpr std::uint32_t kFirstWorkshopPage = 14;
constexpr std::uint32_t kLastWorkshopPage = 16;

// Workshop state (Building +0x1f) high nibbles.
constexpr std::uint8_t kStateMask = 0xf0;
constexpr std::uint8_t kRaceMask = 0x0f;
constexpr std::uint8_t kWorkshopKindA = 0x10;
constexpr std::uint8_t kWorkshopKindB = 0x20;
constexpr std::uint8_t kShipKindA = 0x30;
constexpr std::uint8_t kShipKindB = 0x40;
constexpr std::uint8_t kExtraTilesNotTaken = 0xff;

Building& BuildingAt(CGameData* world, std::uint32_t n) {
    return *(world->buildings + n);
}
Trade& TradeAt(CGameData* world, std::uint32_t n) {
    return *(world->trades + n);
}
Tile& TileAt(CGameData* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

bool IsWarMachineWorkshop(BuildingType type) {
    return type == BuildingType::CatapultWorkshop || type == BuildingType::BallistaWorkshop ||
           type == BuildingType::CannonWorkshop || type == BuildingType::LargeGong;
}

// 0x537859..0x5378ed (and kind 7): the route points.
void SetPoint(CGameData* world, std::uint32_t trade, std::uint32_t point, std::uint32_t x, std::uint32_t y) {
    TradeAt(world, trade).route_x[point] = static_cast<std::uint16_t>(x);
    TradeAt(world, trade).route_y[point] = static_cast<std::uint16_t>(y);
}

void ClearRoute(Run& run, std::uint32_t trade) {
    for (std::uint32_t point = 0; point < kRoutePoints; ++point) {
        SetPoint(run.world(), trade, point, kNoPoint, kNoPoint);
    }
}

// True if a route point other than `point` already is (x, y).
bool IsOtherPoint(CGameData* world, std::uint32_t trade, std::uint32_t point, std::uint32_t x, std::uint32_t y) {
    const Trade& record = TradeAt(world, trade);
    for (std::uint32_t other = 0; other < kRoutePoints; ++other) {
        if (other != point && record.route_x[other] == x && record.route_y[other] == y) {
            return true;
        }
    }
    return false;
}

// A marketplace's route tile must be in the walk region of its door.
bool InDoorRegion(CGameData* world, const Building& building, std::uint32_t x, std::uint32_t y) {
    return TileAt(world, building.door_x, building.door_y).walk_region == TileAt(world, x, y).walk_region;
}

// 0x537a65..0x537ad3: the water tile two steps from a harbour's work tile,
// against its landing direction (whose area a route tile must share).
void HarbourWater(Run& run, std::uint32_t number, std::uint32_t& waterX, std::uint32_t& waterY) {
    const Building& building = BuildingAt(run.world(), number);
    const std::uint32_t direction = (static_cast<std::uint32_t>(building.dock_direction) + 3) % kDirectionCount;
    const std::uint32_t workX = building.work_x;
    const std::uint32_t workY = building.work_y;
    const std::uint32_t x1 = Call<callee::kNeighbourX>(run.world(), workX, direction);
    const std::uint32_t y1 = Call<callee::kNeighbourY>(run.world(), workY, direction);
    waterX = Call<callee::kNeighbourX>(run.world(), x1, direction);
    waterY = Call<callee::kNeighbourY>(run.world(), y1, direction);
}

// Kinds 4..6 (0x5378fb, 0x537bab, 0x537e5b): route point 0..2.
void SetRoutePoint(Run& run, BuildingType type, std::uint32_t number, std::uint32_t trade, std::uint32_t point) {
    const std::uint32_t x = run.Word(4);
    const std::uint32_t y = run.Word(6);
    if (type == BuildingType::Marketplace) {
        CGameData* world = run.world();
        if (!InDoorRegion(world, BuildingAt(world, number), x, y) || IsOtherPoint(world, trade, point, x, y)) {
            return;
        }
        SetPoint(world, trade, point, x, y);
        return;
    }
    if (BuildingAt(run.world(), number).dock_direction == kNoDirection) {
        return;
    }
    std::uint32_t waterX = 0;
    std::uint32_t waterY = 0;
    HarbourWater(run, number, waterX, waterY);
    CGameData* world = run.world();
    if (TileAt(world, waterX, waterY).area != TileAt(world, x, y).area || IsOtherPoint(world, trade, point, x, y)) {
        return;
    }
    SetPoint(world, trade, point, x, y);
}

// Kind 7 (0x53810b): the end point; the three points are cleared with it.
void SetEndPoint(Run& run, BuildingType type, std::uint32_t number, std::uint32_t trade) {
    const std::uint32_t x = run.Word(4);
    const std::uint32_t y = run.Word(6);
    if (type == BuildingType::Marketplace) {
        CGameData* world = run.world();
        if (!InDoorRegion(world, BuildingAt(world, number), x, y)) {
            return;
        }
        for (std::uint32_t point = 0; point < kEndPoint; ++point) {
            SetPoint(run.world(), trade, point, kNoPoint, kNoPoint);
        }
        SetPoint(run.world(), trade, kEndPoint, x, y);
        return;
    }
    if (BuildingAt(run.world(), number).dock_direction == kNoDirection ||
        TileAt(run.world(), x, y).walk_region == 0) {
        return;
    }
    std::uint32_t waterX = 0;
    std::uint32_t waterY = 0;
    HarbourWater(run, number, waterX, waterY);
    // 0x5382da: the nearest tile around (x, y) in the water's area.
    bool found = false;
    for (std::uint32_t i = 0; !found; ++i) {
        CGameData* world = run.world();
        const layout::MapOffset& offset = *(world->offsets + i);
        if (offset.distance > kEndPointRange) {
            return;
        }
        const std::uint32_t tx = x + static_cast<std::uint32_t>(offset.dx);
        const std::uint32_t ty = y + static_cast<std::uint32_t>(offset.dy);
        if (tx >= world->map_size || ty >= world->map_size) {
            continue;
        }
        if (TileAt(world, waterX, waterY).area != TileAt(world, tx, ty).area) {
            continue;
        }
        found = true;
        for (std::uint32_t point = 0; point < kEndPoint; ++point) {
            SetPoint(run.world(), trade, point, kNoPoint, kNoPoint);
        }
        SetPoint(run.world(), trade, kEndPoint, tx, ty);
    }
}

// Kinds 8..11 (0x538406..0x538506): the order of a trade menu line.
void ChangeOrder(Run& run, std::uint32_t kind, std::uint32_t trade) {
    const std::int32_t good = g_tradeMenuGoods[run.Byte(4)];
    std::uint8_t& ordered = *(TradeAt(run.world(), trade).ordered + good);
    const std::uint8_t amount = run.Byte(5);
    switch (kind) {
    case kOrderFull:
        ordered = kFullOrder;
        return;
    case kOrderNone:
        ordered = 0;
        return;
    case kOrderMore:
        ordered = amount + ordered < kFullOrder ? static_cast<std::uint8_t>(amount + ordered) : kFullOrder;
        return;
    default:  // kOrderLess
        ordered = ordered > amount ? static_cast<std::uint8_t>(ordered - amount) : 0;
        return;
    }
}

bool OnPage(CGameData* world, std::uint32_t player, std::uint32_t first, std::uint32_t last) {
    if (static_cast<std::uint32_t>(world->local_player) != player || world->ui_selection_mode != kMenuBuilding) {
        return false;
    }
    const auto page = static_cast<std::uint32_t>(world->ui_special_menu);
    return page >= first && page <= last;
}

}  // namespace

void SetTradeRoute(Run& run) {
    const std::uint32_t player = run.Byte(0);
    const std::uint32_t kind = run.Byte(1);
    const std::uint32_t number = run.Word(2);
    const Building& building = BuildingAt(run.world(), number);
    const BuildingType type = TypeOf(building);
    if ((type != BuildingType::Marketplace && type != BuildingType::Harbour) || building.buildup != 0) {
        return;
    }
    const std::uint32_t trade = building.trade;
    if (kind == kSetMode) {
        TradeAt(run.world(), trade).mode = run.Byte(4);
    } else if (kind == kClearRoute) {
        ClearRoute(run, trade);
    } else if (kind >= kSetPoint1 && kind < kSetEndPoint) {
        SetRoutePoint(run, type, number, trade, kind - kSetPoint1);
    } else if (kind == kSetEndPoint) {
        SetEndPoint(run, type, number, trade);
    } else if (kind >= kOrderFull && kind <= kOrderLess) {
        ChangeOrder(run, kind, trade);
    }
    // 0x538509: refresh the trade pages; redraw the route of the selected building.
    CGameData* world = run.world();
    if (!OnPage(world, player, kFirstTradePage, kLastTradePage)) {
        return;
    }
    world->refresh_ui = 1;
    if (kind >= kClearRoute && kind <= kSetEndPoint && static_cast<std::uint32_t>(g_selectedBuilding) == number) {
        CallThread<kDrawBuildingRouteOverlay>(run.thread, static_cast<std::uint32_t>(g_selectedBuilding));
    }
}

namespace {

// 0x53861e / 0x538730: stop the production.
void StopProduction(Run& run, std::uint32_t number, bool running) {
    if (running && BuildingAt(run.world(), number).workshop.extra_tiles != kExtraTilesNotTaken) {
        Call<kReleaseBuildingExtraTiles>(run.world(), number);
    }
    BuildingAt(run.world(), number).workshop.state = 0;
    BuildingAt(run.world(), number).workshop.extra_tiles = 0;
    for (std::uint32_t slot = 0; slot < 3; ++slot) {
        BuildingAt(run.world(), number).workshop.wanted[slot] = 0;
    }
}

// 0x5386cc / 0x5387f4: an idle building starts production kind `kind` for its race.
void StartProduction(Run& run, std::uint32_t number, std::uint32_t player, std::uint8_t kind) {
    Building& building = BuildingAt(run.world(), number);
    building.workshop.state =
        static_cast<std::uint8_t>(static_cast<std::uint8_t>((run.world()->players + player)->race) + kind);
    BuildingAt(run.world(), number).workshop.extra_tiles = kExtraTilesNotTaken;
}

}  // namespace

void SetWorkshopProduction(Run& run) {
    const std::uint32_t player = run.Byte(0);
    const std::uint32_t value = run.Byte(1);
    const std::uint32_t number = run.Word(2);
    CGameData* world = run.world();
    Building& building = BuildingAt(world, number);
    const BuildingType type = TypeOf(building);
    const bool playerRace =
        (value & kRaceMask) == static_cast<std::uint32_t>((world->players + player)->race);  // dword compare
    if (IsWarMachineWorkshop(type) && building.buildup == 0 && building.owner == player) {
        if (value == 0) {
            StopProduction(run, number, (building.workshop.state & kStateMask) == kWorkshopKindA);
        } else if (playerRace) {
            const std::uint32_t kind = value & kStateMask;
            if ((kind == kWorkshopKindA || kind == kWorkshopKindB) && building.workshop.state == 0) {
                StartProduction(run, number, player, static_cast<std::uint8_t>(kind));
            }
        }
    } else if (type == BuildingType::Shipyard && building.buildup == 0) {
        if (value == 0) {
            StopProduction(run, number, (building.workshop.state & kStateMask) != 0);
        } else if (playerRace) {
            const std::uint32_t kind = value & kStateMask;
            if ((kind == kShipKindA || kind == kShipKindB) && building.workshop.state == 0 &&
                building.dock_direction != kNoDirection) {
                StartProduction(run, number, player, static_cast<std::uint8_t>(kind));
            }
        }
    }
    // 0x538848
    if (OnPage(run.world(), player, kFirstWorkshopPage, kLastWorkshopPage)) {
        run.world()->refresh_ui = 1;
    }
}

}  // namespace s3re::game::commands

// ---------------------------------------------------------------------------
// Tests: commands 0x12 and 0x13 (execute_commands_test.h).
// ---------------------------------------------------------------------------

#include "game/logic/commands/execute_commands_test.h"

#include <cstring>

namespace s3re::game::commands {

namespace {

using bench::Bench;
using bench::MessageBuilder;

constexpr std::uint32_t kTradeBuildings = 40;
constexpr std::uint32_t kTradeRecords = 32;
constexpr std::uint32_t kTradeMap = 384;

struct SavedSelection {
    std::int32_t selected = g_selectedBuilding;
    ~SavedSelection() { g_selectedBuilding = selected; }
};

void InstallTradeStubs(test::CalleeStubs& stubs) {
    stubs.Install<2, callee::kNeighbourX>([](CGameData*, std::uint32_t, const std::uint32_t* a) {
        return a[0] + (a[1] % 3) - 1;
    });
    stubs.Install<2, callee::kNeighbourY>([](CGameData*, std::uint32_t, const std::uint32_t* a) {
        return a[0] + (a[1] & 1) * 2 - 1;
    });
    stubs.Install<1, kDrawBuildingRouteOverlay>();
    stubs.Install<1, kReleaseBuildingExtraTiles>([](CGameData* w, std::uint32_t, const std::uint32_t* a) {
        w->buildings[a[0] % 4000].workshop.wanted[0] = 7;  // overwritten after the call
        return 0u;
    });
}

void SetUpTradeWorld(test::Context& ctx, Bench& bench) {
    test::Rng& rng = ctx.rng();
    test::WorldPair& worlds = bench.worlds;
    worlds.Both([&](CGameData* w) { w->map_size = kTradeMap; });
    for (std::uint32_t x = 0; x < layout::kMapRowTiles; ++x) {
        for (std::uint32_t y = 0; y < layout::kMapRowTiles; ++y) {
            const auto region = static_cast<std::uint8_t>(rng.Chance(1, 10) ? 0 : rng.Below(3));
            const auto area = static_cast<std::uint16_t>(rng.Chance(1, 3) ? 0x8000 + rng.Below(2) : rng.Below(3));
            worlds.Both([&](CGameData* w) {
                w->tiles[x][y].walk_region = region;
                w->tiles[x][y].area = area;
            });
        }
    }
    const std::uint8_t types[] = {33, 33, 34, 34, 31, 47, 48, 50, 32, 32, 2, 1};
    for (std::uint32_t n = 0; n <= kTradeBuildings + 1; ++n) {
        Building b;
        rng.Fill(&b, sizeof(b));
        b.owner = static_cast<std::uint8_t>(rng.Chance(1, 6) ? 0xff : rng.Below(4));
        b.art = types[rng.Below(static_cast<std::uint32_t>(sizeof(types)))];
        b.x = static_cast<std::uint16_t>(rng.Range(10, kTradeMap - 11));
        b.y = static_cast<std::uint16_t>(rng.Range(10, kTradeMap - 11));
        b.door_x = static_cast<std::uint16_t>(rng.Range(10, kTradeMap - 11));
        b.door_y = static_cast<std::uint16_t>(rng.Range(10, kTradeMap - 11));
        b.work_x = static_cast<std::uint16_t>(rng.Range(10, kTradeMap - 11));
        b.work_y = static_cast<std::uint16_t>(rng.Range(10, kTradeMap - 11));
        b.buildup = static_cast<std::uint8_t>(rng.Chance(3, 4) ? 0 : rng.Below(256));
        b.trade = static_cast<std::uint16_t>(rng.Below(kTradeRecords));
        b.dock_direction = static_cast<std::uint16_t>(rng.Chance(1, 5) ? kNoDirection : rng.Below(6));
        worlds.Both([&](CGameData* w) { w->buildings[n] = b; });
    }
    for (std::uint32_t n = 0; n < kTradeRecords; ++n) {
        Trade t;
        rng.Fill(&t, sizeof(t));
        worlds.Both([&](CGameData* w) { w->trades[n] = t; });
    }
    for (std::uint32_t p = 0; p < layout::kPlayers; ++p) {
        const auto race = static_cast<std::int32_t>(rng.Chance(1, 10) ? rng.Below(20) : rng.Below(4));
        worlds.Both([&](CGameData* w) { w->players[p].race = race; });
    }
    for (std::uint32_t i = 0; i < 120; ++i) {
        const layout::MapOffset offset{rng.Range(-6, 6), rng.Range(-6, 6), static_cast<std::int32_t>(i / 5), 0};
        worlds.Both([&](CGameData* w) { w->offsets[i] = offset; });
    }
}

// Per case: UI state, building states (workshops idle or running).
void RandomizeTradeCase(test::Context& ctx, Bench& bench) {
    test::Rng& rng = ctx.rng();
    const auto local = static_cast<std::int32_t>(rng.Below(4));
    const auto menu = static_cast<std::int32_t>(rng.Chance(1, 2) ? kMenuBuilding : static_cast<std::int32_t>(rng.Below(6)));
    const auto page = static_cast<std::int32_t>(rng.Below(18));
    bench.worlds.Both([&](CGameData* w) {
        w->local_player = local;
        w->ui_selection_mode = menu;
        w->ui_special_menu = page;
        w->refresh_ui = 0;
    });
    g_selectedBuilding = static_cast<std::int32_t>(rng.Below(kTradeBuildings + 2));
    for (int k = 0; k < 4; ++k) {
        const std::uint32_t n = rng.Below(kTradeBuildings + 1);
        const std::uint8_t states[] = {0, 0, 0x10, 0x21, 0x32, 0x40, 0x13};
        const auto state = states[rng.Below(7)];
        const auto extra = static_cast<std::uint8_t>(rng.Chance(1, 2) ? kExtraTilesNotTaken : rng.Below(256));
        bench.worlds.Both([&](CGameData* w) {
            w->buildings[n].workshop.state = state;
            w->buildings[n].workshop.extra_tiles = extra;
        });
    }
}

// Route points of one record: some on the map, some unset.
void RandomizeRoutes(test::Context& ctx, Bench& bench) {
    test::Rng& rng = ctx.rng();
    const std::uint32_t n = rng.Below(kTradeRecords);
    std::uint16_t xs[4];
    std::uint16_t ys[4];
    for (std::uint32_t p = 0; p < 4; ++p) {
        const bool unset = rng.Chance(1, 4);
        xs[p] = static_cast<std::uint16_t>(unset ? kNoPoint : rng.Below(kTradeMap));
        ys[p] = static_cast<std::uint16_t>(unset ? kNoPoint : rng.Below(kTradeMap));
    }
    bench.worlds.Both([&](CGameData* w) {
        std::memcpy(w->trades[n].route_x, xs, sizeof(xs));
        std::memcpy(w->trades[n].route_y, ys, sizeof(ys));
    });
}

std::uint8_t TradeHi(std::uint32_t v) {
    return static_cast<std::uint8_t>(v >> 8);
}
std::uint8_t TradeLo(std::uint32_t v) {
    return static_cast<std::uint8_t>(v);
}

void AddTradeCommand(test::Context& ctx, Bench& bench, MessageBuilder& m) {
    test::Rng& rng = ctx.rng();
    const auto player = static_cast<std::uint8_t>(rng.Chance(1, 8) ? rng.Below(20) : rng.Below(4));
    const std::uint32_t building = rng.Below(kTradeBuildings + 1);
    if (rng.Chance(1, 3)) {
        const std::uint32_t race = static_cast<std::uint32_t>(bench.worlds.original()->players[player].race) & 0xf;
        const std::uint8_t kinds[] = {0x10, 0x20, 0x30, 0x40, 0x50, 0};
        auto value = static_cast<std::uint8_t>(kinds[rng.Below(6)] | (rng.Chance(4, 5) ? race : rng.Below(16)));
        if (rng.Chance(1, 4)) {
            value = 0;
        }
        m.Command(CommandId::SetWorkshopProduction, {player, value, TradeHi(building), TradeLo(building)});
        return;
    }
    const auto kind = static_cast<std::uint8_t>(rng.Chance(1, 10) ? rng.Below(256) : rng.Below(13));
    std::uint32_t x = rng.Below(kTradeMap);
    std::uint32_t y = rng.Below(kTradeMap);
    if (rng.Chance(1, 2)) {
        // A tile already on the route.
        const Building& b = bench.worlds.original()->buildings[building];
        const Trade& t = bench.worlds.original()->trades[b.trade % kTradeRecords];
        const std::uint32_t point = rng.Below(4);
        if (t.route_x[point] < kTradeMap && t.route_y[point] < kTradeMap) {
            x = t.route_x[point];
            y = t.route_y[point];
        }
    }
    const bool route = kind >= 4 && kind <= 7;
    const auto line = static_cast<std::uint8_t>(rng.Chance(1, 10) ? rng.Below(40) : rng.Below(34));
    const std::uint8_t byte4 = route ? TradeHi(x) : (rng.Chance(1, 2) ? line : TradeHi(x));
    const std::uint8_t byte5 = route ? TradeLo(x) : static_cast<std::uint8_t>(rng.Below(256));
    m.Command(CommandId::SetTradeRoute,
              {player, kind, TradeHi(building), TradeLo(building), byte4, byte5, TradeHi(y), TradeLo(y)});
}

void CompareTradeCommands(test::Context& ctx, ExecuteCommandsFn candidate, int cases) {
    SavedSelection saved;
    Bench bench(ctx);
    SetUpTradeWorld(ctx, bench);
    InstallTradeStubs(bench.stubs);
    test::Rng& rng = ctx.rng();
    for (int i = 0; i < cases; ++i) {
        RandomizeTradeCase(ctx, bench);
        RandomizeRoutes(ctx, bench);
        const std::uint32_t step = 1 + rng.Below(1000);
        bench.SetStep(step);
        MessageBuilder messages;
        const std::uint32_t count = 1 + rng.Below(4);
        for (std::uint32_t k = 0; k < count; ++k) {
            AddTradeCommand(ctx, bench, messages);
        }
        messages.Grant(step + 1);
        if (!bench.Run(ctx, i, messages, -1, candidate)) {
            return;
        }
    }
    bench.Finish(ctx);
}

// Wrong: full orders stop at 99.
std::uint32_t __fastcall WrongTradeCommands(CLogicThread* self, void* edx) {
    const std::uint32_t result = CLogicThread_ExecuteCommands(self, edx);
    for (std::uint32_t n = 0; n < kTradeRecords; ++n) {
        for (std::uint8_t& order : self->world->trades[n].ordered) {
            if (order == kFullOrder) {
                order = kFullOrder - 1;
            }
        }
    }
    return result;
}

}  // namespace

S3RE_TEST(CLogicThread_ExecuteCommands_Trade_MatchesOriginal) {
    CompareTradeCommands(ctx, &CLogicThread_ExecuteCommands, 4000);
}
S3RE_TEST(CLogicThread_ExecuteCommands_Trade_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareTradeCommands(probe, &WrongTradeCommands, 800);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game::commands
