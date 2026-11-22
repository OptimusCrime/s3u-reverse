// Callees of GameWorld::UpdateEconomy (0x4e05e0): population counts and
// helpers of the economy step.
//
// GameWorld::RecountPlayerPopulation (0x005104c0): () thiscall, ret. For
//   each of the 20 players: population (+0xc4) = 0, population_limit
//   (+0xc8) = population_limit_base (+0xcc), carriers (+0xd0) = 0,
//   surplus_carriers (+0xd4) = 0. Then for settlers 1..high water (re-read
//   every step) with an owner (!= 0xff) whose type class (S3.EXE 0x658f64)
//   is 0 or 10..29 (signed): players[owner].population += 1, and for type 0
//   also carriers += 1. Then for buildings 1..high water with an owner and
//   buildup 0 (finished): type 0x1a/0x1b/0x1c adds 10/30/100 to the
//   owner's population_limit (residences, probably). Finally per player:
//   if population != 0 and population > limit (unsigned), surplus_carriers
//   = carriers - (((limit << 16) / population) * carriers >> 16) (all 32-bit
//   unsigned, truncating), else 0. The owner indexes the player records
//   without a bound check, as in the original. EAX: not used by the only
//   caller (UpdateEconomy).
// GameWorld::SetTowerSoldierAnimation (0x0050e200): (settler) thiscall,
//   ret 4. Animation kind 0x176, or 0x177..0x17e for settler types 0x20,
//   0x23, 8, 0x1e, 0x21, 0xf, 0x1f, 0x22 (the soldier types); + 9 if the
//   settler's building (+0x22) has type 0x10, + 0x12 for type 0x12; + 0x1b
//   / + 0x36 for the building's race 1 / 2. Then SetSettlerAnimation(settler,
//   kind); EAX = its result (the one caller overwrites AL at once). Called by
//   UpdateEconomy when a soldier enters a military building.
// GameWorld::RequestSettlerForBuilding (0x004dacd0): (building, type)
//   thiscall, ret 8; EAX = the settler sent, or 0 (25 call sites in
//   UpdateEconomy test it). Area = the area of the building's door tile
//   (+0xa/+0xc). 1) The nearest (TileDistance from the building's position
//   +0x2/+0x4, first of equals) settler in the area's idle list of `type`
//   (area +0x12a, linked by settler +0x10/+0x12) without flag 0x80 (+0x14)
//   is unlinked, SetSettlerTarget(door), job 3, counter 0, building +0x22 =
//   building. 2) Else, with civilians = CountAreaSettlersOfClass0(area) and
//   carriers = CountAreaSettlersOfType(area, 0): if settings_extra[0] (area
//   +0x23e, a percentage) * civilians / 100 >= carriers, no carrier can be
//   spared: NotifyLocalPlayer(owner, 0xc, x, y) unless type is 1 or 2;
//   return 0. Types 1 / 2 (diggers, builders) also need settings_extra[1] /
//   [2] * civilians / 100 > CountAreaSettlersOfType(area, type) and the tool
//   0xc / 0xd. Types 3, 4, 6, 0xa, 0xb, 0xe, 0x10, 0x11, 0x13 need the tool
//   0xe, 0xf, 0x10, 0xf, 0xd, 0xe, 0x20, 0x11, 0xd; types 7, 9, 0xc, 0xd,
//   0x12, 0x14..0x1d, 0x26, 0x2d..0x2f none; any other type: return 0.
//   Without a tool: the nearest carrier (area list 0, no flag 0x80) is
//   unlinked, building = it, SetSettlerTarget(door), job 3, counter 0, type
//   +0x1 = type. With a tool: the nearest pile in the area's list of that
//   good with amount != leaving (distance d; for kinds other than 0xff
//   2 * (d + 5 * amount) - 10 * (leaving + 1), 32-bit) is the source; none:
//   NotifyLocalPlayer(owner, 0x18..0x1e by tool 0xc..0x11/0x20) except tool
//   0xc for type 1 / 0xd for type 2; return 0. The carrier nearest to the
//   source (no flag 0x80): PileIncAmountLeaving(source), unlinked,
//   SetSettlerTarget(source), job 0x68, counter 0, building = it, good +0x1a
//   = tool, +0x18 = type.
// GameWorld::DispatchPileCarrier (0x005052c0): (pile) thiscall, ret 4. Area
//   = the area of the pile's tile. Only if the area's settings5[good] (+0x21a)
//   is set and its idle carrier list has at least four settlers. Target: the
//   pile in the area's list of the good with kind <= 0x32 (building piles),
//   the same good and amount + coming != 8 with the smallest TileDistance +
//   0x46 - 10 * (amount + coming); then the piles of the area's empty list
//   (+0x8) at TileDistance * 3 + 0x46 (all 32-bit, first of equals). The
//   carrier nearest to the pile (the flag 0x80 is not checked here):
//   PileIncAmountLeaving(pile), PileIncAmountComing(target), SetPileGood(
//   target, good) if its good differs, carrier unlinked, SetSettlerTarget(
//   pile), job 0x4c, counter 0, building = target, good = the pile's good.
//   Callers UpdateEconomy and UpdateEnvironment; EAX unused.

#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/fake_graphics.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace s3re::game {

namespace {

using layout::Building;
using layout::Player;
using layout::Settler;

const std::int32_t* const kSettlerTypeClass = reinterpret_cast<const std::int32_t*>(0x00658f64);

constexpr std::uint8_t kFree = 0xff;

// The player record of an owner byte (no bound check, like the original).
Player& PlayerOf(GameWorld* world, std::uint8_t owner) {
    return *(world->players + owner);
}

}  // namespace

std::uint32_t __fastcall GameWorld_SetSettlerAnimation(GameWorld* world, void* edx, std::uint32_t settler,
                                                       std::uint32_t kind);

using RecountFn = void(__fastcall*)(GameWorld* world, void* edx);
using SettlerFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t settler);

// Purpose: Recounts each player's population (civilians and carriers) and the
// housing limit from the finished residences, and how many carriers are
// beyond that limit.
void __fastcall GameWorld_RecountPlayerPopulation(GameWorld* world, void*) {
    for (Player& player : world->players) {
        player.population = 0;
        player.population_limit = player.population_limit_base;
        player.carriers = 0;
        player.surplus_carriers = 0;
    }
    for (std::uint32_t n = 1; n <= world->settler_high_water; ++n) {
        const Settler& settler = world->settlers[n];
        if (settler.owner == kFree) {
            continue;
        }
        const std::int32_t settlerClass = kSettlerTypeClass[settler.art];
        if (settlerClass != 0 && (settlerClass < 10 || settlerClass > 29)) {
            continue;
        }
        ++PlayerOf(world, settler.owner).population;
        if (settler.art == 0) {
            ++PlayerOf(world, settler.owner).carriers;
        }
    }
    for (std::uint32_t n = 1; n <= world->building_high_water; ++n) {
        const Building& building = world->buildings[n];
        if (building.owner == kFree || building.buildup != 0) {
            continue;
        }
        if (building.art == 0x1a) {
            PlayerOf(world, building.owner).population_limit += 10;
        }
        if (building.art == 0x1b) {
            PlayerOf(world, building.owner).population_limit += 30;
        }
        if (building.art == 0x1c) {
            PlayerOf(world, building.owner).population_limit += 100;
        }
    }
    for (Player& player : world->players) {
        const std::uint32_t population = player.population;
        if (population != 0 && population > player.population_limit) {
            const std::uint32_t share = (player.population_limit << 16) / population;
            player.surplus_carriers = player.carriers - ((share * player.carriers) >> 16);
        } else {
            player.surplus_carriers = 0;
        }
    }
}

// Purpose: Gives a soldier who has just entered a military building the
// animation for his type, the building's type and the building's race.
std::uint32_t __fastcall GameWorld_SetTowerSoldierAnimation(GameWorld* world, void*, std::uint32_t settler) {
    std::uint32_t kind = 0x176;
    switch (world->settlers[settler].art) {
        case 0x20: kind = 0x177; break;
        case 0x23: kind = 0x178; break;
        case 0x08: kind = 0x179; break;
        case 0x1e: kind = 0x17a; break;
        case 0x21: kind = 0x17b; break;
        case 0x0f: kind = 0x17c; break;
        case 0x1f: kind = 0x17d; break;
        case 0x22: kind = 0x17e; break;
        default: break;
    }
    const Building& building = world->buildings[world->settlers[settler].building];
    if (building.art == 0x10) {
        kind += 9;
    }
    if (building.art == 0x12) {
        kind += 0x12;
    }
    if (building.race == 1) {
        kind += 0x1b;
    }
    if (building.race == 2) {
        kind += 0x36;
    }
    return GameWorld_SetSettlerAnimation(world, nullptr, settler, kind);
}

S3RE_HOOK(0x005104c0, GameWorld_RecountPlayerPopulation, RecountFn, Original_GameWorld_RecountPlayerPopulation,
          GameWorld_RecountPlayerPopulation);
S3RE_HOOK(0x0050e200, GameWorld_SetTowerSoldierAnimation, SettlerFn, Original_GameWorld_SetTowerSoldierAnimation,
          GameWorld_SetTowerSoldierAnimation);

// ---------------------------------------------------------------------------
// Workers and carriers for buildings and piles
// ---------------------------------------------------------------------------

namespace {

using layout::Area;
using layout::Pile;

constexpr std::uint8_t kSettlerFlagSurplus = 0x80;  // Settler::flags: surplus carrier (job 1)
constexpr std::uint8_t kPileArtFree = 0xff;
constexpr std::uint8_t kPileArtLastBuilding = 0x32;  // building piles have kinds up to here
constexpr std::uint16_t kJobGoToBuilding = 3;
constexpr std::uint16_t kJobFetchTool = 0x68;
constexpr std::uint16_t kJobFetchGood = 0x4c;
constexpr std::uint32_t kNoTool = 0;

// NotifyLocalPlayer and SetPileGood (package B, verified) through their
// S3.EXE addresses.
using NotifyFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t player, std::uint32_t kind,
                                            std::uint32_t x, std::uint32_t y);
using SetPileGoodFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t pile, std::uint32_t good);
constexpr std::uintptr_t kNotifyLocalPlayer = 0x004fec10;
constexpr std::uintptr_t kSetPileGood = 0x005061b0;

void Notify(GameWorld* world, std::uint32_t player, std::uint32_t kind, std::uint32_t x, std::uint32_t y) {
    reinterpret_cast<NotifyFn>(kNotifyLocalPlayer)(world, nullptr, player, kind, x, y);
}

layout::Tile& TileAt(GameWorld* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

// The settler of the list starting at `head` nearest to (x, y) by
// TileDistance(x, y, settler), the first of equals; settlers with the
// surplus flag are skipped if `skipSurplus`. `x`/`y` are re-read per
// settler through the callbacks, as the original reads them from memory.
template <typename X, typename Y>
std::uint32_t NearestInList(GameWorld* world, std::uint32_t head, bool skipSurplus, X x, Y y) {
    std::uint32_t best = 0xffffffffu;
    std::uint32_t found = 0;
    for (std::uint32_t n = head; n != 0; n = world->settlers[n].next) {
        const Settler& s = world->settlers[n];
        if (skipSurplus && (s.flags & kSettlerFlagSurplus) != 0) {
            continue;
        }
        const std::uint32_t d = TileDistance(x(), y(), s.x, s.y);
        if (d < best) {
            best = d;
            found = n;
        }
    }
    return found;
}

// Takes settler n out of the doubly linked list whose head is `head`.
void UnlinkSettler(GameWorld* world, std::uint16_t& head, std::uint32_t n) {
    Settler& s = world->settlers[n];
    if (s.prev != 0) {
        world->settlers[s.prev].next = s.next;
    } else {
        head = s.next;
    }
    if (s.next != 0) {
        world->settlers[s.next].prev = s.prev;
    }
}

// The list head of settler type `type` in an area (no bound check).
std::uint16_t& TypeList(Area& area, std::uint32_t type) {
    return *(area.settlers_by_type + type);
}

// The goods type (a tool) a carrier must fetch to become a worker of `type`,
// kNoTool if he can be converted at once, or -1 if the type can't be made.
// Types 1 and 2 also need their own share (settings_extra[1]/[2] percent of
// the civilians) to be above their count.
constexpr std::uint32_t kCannot = 0xffffffffu;

std::uint32_t ToolForType(std::uint32_t type) {
    switch (type) {
        case 3: return 0xe;
        case 4: return 0xf;
        case 6: return 0x10;
        case 7: case 9: return kNoTool;
        case 0xa: return 0xf;
        case 0xb: return 0xd;
        case 0xc: case 0xd: return kNoTool;
        case 0xe: return 0xe;
        case 0x10: return 0x20;
        case 0x11: return 0x11;
        case 0x12: case 0x1c: return kNoTool;
        case 0x13: return 0xd;
        case 0x14: case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1d:
        case 0x26: case 0x2d: case 0x2f: case 0x2e: return kNoTool;
        default: return kCannot;
    }
}

// The notification sent when no pile of a tool is found.
std::uint32_t MissingToolNotification(std::uint32_t tool) {
    switch (tool) {
        case 0xc: return 0x18;
        case 0xd: return 0x19;
        case 0xe: return 0x1a;
        case 0xf: return 0x1b;
        case 0x10: return 0x1c;
        case 0x11: return 0x1d;
        case 0x20: return 0x1e;
        default: return 0;
    }
}

}  // namespace

std::uint32_t __fastcall GameWorld_CountAreaSettlersOfClass0(GameWorld* world, void* edx, std::uint32_t area);
std::uint32_t __fastcall GameWorld_CountAreaSettlersOfType(GameWorld* world, void* edx, std::uint32_t area,
                                                           std::uint32_t type);
std::uint32_t __fastcall GameWorld_PileIncAmountComing(GameWorld* world, void* edx, std::uint32_t pile);

using RequestFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t building, std::uint32_t type);
using PileFn = void(__fastcall*)(GameWorld* world, void* edx, std::uint32_t pile);

// Purpose: Finds a worker of a type for a building: the nearest idle one of
// that type in the building's economy area, else a carrier who becomes one
// (directly, or after fetching the tool the job needs), if enough carriers
// are left. Notifies the player when carriers or a tool are missing.
// Returns the settler, or 0.
std::uint32_t __fastcall GameWorld_RequestSettlerForBuilding(GameWorld* world, void*, std::uint32_t number,
                                                             std::uint32_t type) {
    Building& b = world->buildings[number];
    const std::uint32_t areaNumber = TileAt(world, b.door_x, b.door_y).area;
    Area& area = world->areas[areaNumber];
    auto bx = [&] { return static_cast<std::uint32_t>(b.x); };
    auto by = [&] { return static_cast<std::uint32_t>(b.y); };

    std::uint16_t& idle = TypeList(area, type);
    const std::uint32_t worker = NearestInList(world, idle, true, bx, by);
    if (worker != 0) {
        Settler& s = world->settlers[worker];
        UnlinkSettler(world, idle, worker);
        GameWorld_SetSettlerTarget(world, nullptr, worker, b.door_x, b.door_y);
        s.job = kJobGoToBuilding;
        s.counter = 0;
        s.building = static_cast<std::uint16_t>(number);
        return worker;
    }

    const std::uint32_t civilians = GameWorld_CountAreaSettlersOfClass0(world, nullptr, areaNumber);
    const std::uint32_t carriers = GameWorld_CountAreaSettlersOfType(world, nullptr, areaNumber, 0);
    if (area.settings_extra[0] * civilians / 100 >= carriers) {
        // Not enough carriers to spare one.
        if (type != 1 && type != 2) {
            Notify(world, b.owner, 0xc, b.x, b.y);
        }
        return 0;
    }
    std::uint32_t tool = 0;
    if (type == 1 || type == 2) {
        const std::uint32_t count = GameWorld_CountAreaSettlersOfType(world, nullptr, areaNumber, type);
        if (area.settings_extra[type] * civilians / 100 <= count) {
            return 0;
        }
        tool = type == 1 ? 0xc : 0xd;
    } else {
        tool = ToolForType(type);
        if (tool == kCannot) {
            return 0;
        }
    }
    std::uint16_t& carrierList = area.settlers_by_type[0];

    if (tool == kNoTool) {
        const std::uint32_t carrier = NearestInList(world, carrierList, true, bx, by);
        if (carrier == 0) {
            return 0;
        }
        Settler& s = world->settlers[carrier];
        UnlinkSettler(world, carrierList, carrier);
        s.building = static_cast<std::uint16_t>(number);
        GameWorld_SetSettlerTarget(world, nullptr, carrier, b.door_x, b.door_y);
        s.job = kJobGoToBuilding;
        s.counter = 0;
        s.art = static_cast<std::uint8_t>(type);
        return carrier;
    }

    // The nearest pile with the tool that still has a unit not already taken
    // (building piles count with their fill and reservations).
    std::uint32_t best = 0xffffffffu;
    std::uint32_t source = 0;
    for (std::uint32_t q = *(area.piles_by_goods + tool); q != 0; q = world->piles[q].next) {
        const Pile& p = world->piles[q];
        if (p.amount == p.leaving) {
            continue;
        }
        std::uint32_t d = TileDistance(b.x, b.y, p.x, p.y);
        if (p.art != kPileArtFree) {
            d = 2 * (d + 5 * p.amount) - 10 * (static_cast<std::uint32_t>(p.leaving) + 1);
        }
        if (d < best) {
            best = d;
            source = q;
        }
    }
    if (source == 0) {
        if ((tool == 0xc && type == 1) || (tool == 0xd && type == 2)) {
            return 0;
        }
        const std::uint32_t kind = MissingToolNotification(tool);
        if (kind != 0) {
            Notify(world, b.owner, kind, b.x, b.y);
        }
        return 0;
    }
    const Pile& pile = world->piles[source];
    const std::uint32_t carrier =
        NearestInList(world, carrierList, true, [&] { return static_cast<std::uint32_t>(pile.x); },
                      [&] { return static_cast<std::uint32_t>(pile.y); });
    if (carrier == 0) {
        return 0;
    }
    GameWorld_PileIncAmountLeaving(world, nullptr, source);
    Settler& s = world->settlers[carrier];
    UnlinkSettler(world, carrierList, carrier);
    GameWorld_SetSettlerTarget(world, nullptr, carrier, pile.x, pile.y);
    s.job = kJobFetchTool;
    s.counter = 0;
    s.building = static_cast<std::uint16_t>(number);
    s.good = static_cast<std::uint8_t>(tool);
    s.unk18 = static_cast<std::uint8_t>(type);
    return carrier;
}

// Purpose: Sends the nearest idle carrier of the area to a pile whose goods
// must go elsewhere, choosing their destination: the nearest building pile
// that wants that good and has room (fuller ones count as farther), or an
// empty pile place (counted three times as far). Only if the area allows
// moving that good and keeps at least four idle carriers.
void __fastcall GameWorld_DispatchPileCarrier(GameWorld* world, void*, std::uint32_t number) {
    Pile& pile = world->piles[number];
    const std::uint32_t areaNumber = TileAt(world, pile.x, pile.y).area;
    Area& area = world->areas[areaNumber];
    if (*(area.settings5 + pile.good) == 0) {
        return;
    }
    std::uint16_t& carrierList = area.settlers_by_type[0];
    std::uint32_t idle = 0;
    for (std::uint32_t n = carrierList; n != 0 && idle != 4; n = world->settlers[n].next) {
        ++idle;
    }
    if (idle != 4) {
        return;
    }

    std::uint32_t best = 0xffffffffu;
    std::uint32_t target = 0;
    for (std::uint32_t q = *(area.piles_by_goods + pile.good); q != 0; q = world->piles[q].next) {
        const Pile& p = world->piles[q];
        if (p.art > kPileArtLastBuilding || p.good != pile.good) {
            continue;
        }
        const std::uint32_t filled = static_cast<std::uint32_t>(p.amount) + p.coming;
        if (filled == 8) {
            continue;
        }
        const std::uint32_t d = TileDistance(pile.x, pile.y, p.x, p.y) + 0x46 - filled * 10;
        if (d < best) {
            best = d;
            target = q;
        }
    }
    for (std::uint32_t q = area.pile_list; q != 0; q = world->piles[q].next) {
        const Pile& p = world->piles[q];
        const std::uint32_t d = TileDistance(pile.x, pile.y, p.x, p.y) * 3 + 0x46;
        if (d < best) {
            best = d;
            target = q;
        }
    }
    if (target == 0) {
        return;
    }
    const std::uint32_t carrier = NearestInList(world, carrierList, false, [&] { return static_cast<std::uint32_t>(pile.x); },
                                                [&] { return static_cast<std::uint32_t>(pile.y); });
    if (carrier == 0) {
        return;
    }
    GameWorld_PileIncAmountLeaving(world, nullptr, number);
    GameWorld_PileIncAmountComing(world, nullptr, target);
    if (world->piles[target].good != pile.good) {
        // The original loads only CL with the good, so the upper three bytes
        // of the argument are whatever PileIncAmountComing left in ECX;
        // SetPileGood uses only the low byte.
        reinterpret_cast<SetPileGoodFn>(kSetPileGood)(world, nullptr, target, pile.good);
    }
    Settler& s = world->settlers[carrier];
    UnlinkSettler(world, carrierList, carrier);
    GameWorld_SetSettlerTarget(world, nullptr, carrier, pile.x, pile.y);
    s.job = kJobFetchGood;
    s.counter = 0;
    s.building = static_cast<std::uint16_t>(target);
    s.good = pile.good;
}

S3RE_HOOK(0x004dacd0, GameWorld_RequestSettlerForBuilding, RequestFn, Original_GameWorld_RequestSettlerForBuilding,
          GameWorld_RequestSettlerForBuilding);
S3RE_HOOK(0x005052c0, GameWorld_DispatchPileCarrier, PileFn, Original_GameWorld_DispatchPileCarrier,
          GameWorld_DispatchPileCarrier);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kPopulationWorld = offsetof(GameWorld, buildings) + 400 * sizeof(Building);

void ComparePopulation(test::Context& ctx, RecountFn candidate, int cases) {
    test::WorldPair worlds(ctx, kPopulationWorld);
    static constexpr std::uint8_t kResidences[] = {0x1a, 0x1b, 0x1c};
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t settlers = ctx.rng().Below(300);
        const std::uint32_t buildings = ctx.rng().Below(390);
        const bool wildOwners = ctx.rng().Chance(1, 10);
        auto owner = [&]() -> std::uint8_t {
            if (ctx.rng().Chance(1, 6)) {
                return kFree;
            }
            return static_cast<std::uint8_t>(wildOwners && ctx.rng().Chance(1, 20) ? ctx.rng().Below(255)
                                                                                    : ctx.rng().Below(20));
        };
        worlds.Both([&](GameWorld* w) { w->settler_high_water = settlers; });
        worlds.Both([&](GameWorld* w) { w->building_high_water = buildings; });
        for (std::uint32_t n = 1; n <= settlers; ++n) {
            const std::uint8_t o = owner();
            const auto art = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Below(0x30));
            worlds.Both([&](GameWorld* w) {
                w->settlers[n].owner = o;
                w->settlers[n].art = art;
            });
        }
        for (std::uint32_t n = 1; n <= buildings; ++n) {
            const std::uint8_t o = owner();
            const auto art = ctx.rng().Chance(1, 2) ? kResidences[ctx.rng().Below(3)]
                                                    : static_cast<std::uint8_t>(ctx.rng().Below(0x39));
            const auto buildup = static_cast<std::uint8_t>(ctx.rng().Chance(3, 4) ? 0 : ctx.rng().Below(256));
            worlds.Both([&](GameWorld* w) {
                w->buildings[n].owner = o;
                w->buildings[n].art = art;
                w->buildings[n].buildup = buildup;
            });
        }
        for (std::uint32_t p = 0; p < 20; ++p) {
            const std::uint32_t base = ctx.rng().Chance(1, 4) ? ctx.rng().Next32() : ctx.rng().Below(200);
            worlds.Both([&](GameWorld* w) { w->players[p].population_limit_base = base; });
        }
        Original_GameWorld_RecountPlayerPopulation(worlds.original(), nullptr);
        candidate(worlds.ours(), nullptr);
        if (!worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

// Wrong: rounds the share up instead of truncating.
void __fastcall WrongRecountPlayerPopulation(GameWorld* world, void* edx) {
    GameWorld_RecountPlayerPopulation(world, edx);
    for (Player& player : world->players) {
        if (player.surplus_carriers != 0) {
            --player.surplus_carriers;
        }
    }
}

void CompareTowerAnimation(test::Context& ctx, SettlerFn candidate, int cases) {
    // Every group's sprite table points at the same random sprite pointers.
    test::FakeGraphics gfx;
    std::vector<std::uint8_t> frames(0x2000);
    ctx.rng().Fill(frames.data(), frames.size());
    gfx.ShareTable();
    for (std::uint32_t k = 0; k < test::FakeGraphics::kSprites; ++k) {
        gfx.table().sprites[k] = reinterpret_cast<std::uint32_t>(frames.data() + ctx.rng().Below(0x1f00));
    }

    test::WorldPair worlds(ctx, offsetof(GameWorld, buildings) + 0x200 * sizeof(Building));
    static constexpr std::uint8_t kSoldiers[] = {0x20, 0x23, 8, 0x1e, 0x21, 0xf, 0x1f, 0x22};
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t settler = 1 + ctx.rng().Below(0x7cfe);
        const auto art = ctx.rng().Chance(3, 4) ? kSoldiers[ctx.rng().Below(8)]
                                                : static_cast<std::uint8_t>(ctx.rng().Below(256));
        const auto race = static_cast<std::uint8_t>(ctx.rng().Below(4));
        const auto building = static_cast<std::uint16_t>(ctx.rng().Below(0x200));
        static constexpr std::uint8_t kTypes[] = {0x10, 0x12, 0xf, 0x11};
        const std::uint8_t type = ctx.rng().Chance(4, 5) ? kTypes[ctx.rng().Below(4)]
                                                        : static_cast<std::uint8_t>(ctx.rng().Below(256));
        const auto buildingRace = static_cast<std::uint8_t>(ctx.rng().Chance(9, 10) ? ctx.rng().Below(4)
                                                                                   : ctx.rng().Below(256));
        worlds.Both([&](GameWorld* w) {
            w->settlers[settler].art = art;
            w->settlers[settler].race = race;
            w->settlers[settler].building = building;
            w->buildings[building].art = type;
            w->buildings[building].race = buildingRace;
        });
        const std::uint32_t expected = Original_GameWorld_SetTowerSoldierAnimation(worlds.original(), nullptr, settler);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, settler);
        if (!S3RE_EXPECT_EQ(expected, actual) ||
            !worlds.SameField(ctx, [&](GameWorld* w) -> auto& { return w->settlers[settler]; }) ||
            (i % 256 == 255 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: settler %u art %u building type %u race %u", i, settler, art,
                     type, buildingRace);
            break;
        }
    }
    S3RE_EXPECT(worlds.Checkpoint(ctx));
}

// Wrong: ignores the building's race 2.
std::uint32_t __fastcall WrongTowerSoldierAnimation(GameWorld* world, void* edx, std::uint32_t settler) {
    Building& building = world->buildings[world->settlers[settler].building];
    const std::uint8_t race = building.race;
    if (race == 2) {
        building.race = 0;
    }
    const std::uint32_t result = GameWorld_SetTowerSoldierAnimation(world, edx, settler);
    building.race = race;
    return result;
}

}  // namespace

S3RE_TEST(GameWorld_RecountPlayerPopulation_MatchesOriginal) {
    ComparePopulation(ctx, &GameWorld_RecountPlayerPopulation, 300);
}
S3RE_TEST(GameWorld_RecountPlayerPopulation_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePopulation(probe, &WrongRecountPlayerPopulation, 50);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_SetTowerSoldierAnimation_MatchesOriginal) {
    CompareTowerAnimation(ctx, &GameWorld_SetTowerSoldierAnimation, 20000);
}
S3RE_TEST(GameWorld_SetTowerSoldierAnimation_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareTowerAnimation(probe, &WrongTowerSoldierAnimation, 5000);
    S3RE_EXPECT(probe.failures() > 0);
}


// --- RequestSettlerForBuilding, DispatchPileCarrier --------------------------

namespace {

constexpr std::size_t kEconomyWorld = offsetof(GameWorld, buildings) + 64 * sizeof(Building);
constexpr std::uint32_t kEconomySettlers = 60;
constexpr std::uint32_t kEconomyPiles = 60;

struct EconomyCase {
    std::uint32_t building;
    std::uint32_t type;
    std::uint32_t pile;
};

// Links the given records into a doubly linked list (next +0x10 / prev
// +0x12 for settlers, +0xc / +0xa for piles) and returns its head.
template <typename Record>
std::uint16_t Chain(Record* records, const std::vector<std::uint32_t>& members) {
    for (std::size_t k = 0; k < members.size(); ++k) {
        Record& r = records[members[k]];
        r.prev = static_cast<std::uint16_t>(k == 0 ? 0 : members[k - 1]);
        r.next = static_cast<std::uint16_t>(k + 1 == members.size() ? 0 : members[k + 1]);
    }
    return static_cast<std::uint16_t>(members.empty() ? 0 : members[0]);
}

// An area 1..30 with idle settlers of the requested type and carriers
// (well-formed lists), tool and goods piles in its lists, a building whose
// door lies in the area and a pile on a tile of the area.
EconomyCase SetUpEconomy(test::Context& ctx, test::WorldPair& worlds) {
    EconomyCase c{};
    c.building = 1 + ctx.rng().Below(50);
    static constexpr std::uint8_t kTypes[] = {1, 2, 3, 4, 6, 7, 9, 0xa, 0xb, 0xc, 0xe, 0x10, 0x11, 0x12, 0x13, 0x14,
                                              0x1d, 0x26, 0x2e, 0x5, 0x8, 0xf, 0};
    c.type = ctx.rng().Chance(9, 10) ? kTypes[ctx.rng().Below(sizeof(kTypes))] : ctx.rng().Below(0x30);
    c.pile = 1 + ctx.rng().Below(kEconomyPiles);
    const std::uint32_t area = 1 + ctx.rng().Below(30);
    const auto base = static_cast<std::uint16_t>(40 + ctx.rng().Below(300));
    auto nearby = [&] { return static_cast<std::uint16_t>(base + ctx.rng().Below(20)); };

    // Settlers 1..60: each in at most one list (type list, carrier list or none).
    std::vector<std::uint32_t> typeMembers, carrierMembers;
    for (std::uint32_t n = 1; n <= kEconomySettlers; ++n) {
        const std::uint32_t which = ctx.rng().Below(ctx.rng().Chance(1, 4) ? 2 : 3);  // sometimes no idle workers
        if (which == 1) {
            carrierMembers.push_back(n);
        } else if (which == 2 && c.type != 0) {
            typeMembers.push_back(n);
        }
        const auto flags = static_cast<std::uint8_t>(ctx.rng().Chance(1, 4) ? 0x80 | ctx.rng().Below(256) : ctx.rng().Below(0x80));
        const auto x = nearby(), y = nearby();
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Below(3));
        const auto art = static_cast<std::uint8_t>(which == 1 ? 0 : which == 2 ? c.type : ctx.rng().Below(0x30));
        worlds.Both([&](GameWorld* w) {
            Settler& s = w->settlers[n];
            s.flags = flags;
            s.x = x;
            s.y = y;
            s.owner = owner;
            s.art = art;
            w->tiles[x][y].settler = static_cast<std::uint16_t>(n);
            w->tiles[x][y].area = static_cast<std::uint16_t>(area);
        });
    }
    if (ctx.rng().Chance(1, 3)) {
        carrierMembers.resize(carrierMembers.size() * ctx.rng().Below(4) / 4);
    }
    // Piles 1..60: half in lists of goods 0xc..0x11/0x20 or the pile's good,
    // some in the area's empty list.
    static constexpr std::uint8_t kGoods[] = {0xc, 0xd, 0xe, 0xf, 0x10, 0x11, 0x20, 3, 7};
    std::vector<std::uint32_t> goodsMembers[sizeof(kGoods)];
    std::vector<std::uint32_t> emptyMembers;
    for (std::uint32_t q = 1; q <= kEconomyPiles; ++q) {
        const std::uint32_t g = ctx.rng().Below(sizeof(kGoods));
        const std::uint32_t where = ctx.rng().Below(4);
        if (where < 2 && q != c.pile) {
            goodsMembers[g].push_back(q);
        } else if (where == 2 && q != c.pile) {
            emptyMembers.push_back(q);
        }
        const auto amount = static_cast<std::uint8_t>(ctx.rng().Below(9));
        const auto leaving = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3) ? amount : ctx.rng().Below(amount + 1));
        const auto coming = static_cast<std::uint8_t>(ctx.rng().Below(9 - amount));
        static constexpr std::uint8_t kArts[] = {0xff, 0xfe, 0xfd, 1, 0x20, 0x32, 0x33};
        const std::uint8_t art = kArts[ctx.rng().Below(sizeof(kArts))];
        const std::uint8_t good = ctx.rng().Chance(4, 5) ? kGoods[g] : static_cast<std::uint8_t>(ctx.rng().Below(36));
        const auto x = nearby(), y = nearby();
        worlds.Both([&](GameWorld* w) {
            Pile& p = w->piles[q];
            p.amount = amount;
            p.leaving = leaving;
            p.coming = coming;
            p.art = art;
            p.good = good;
            p.x = x;
            p.y = y;
            w->tiles[x][y].area = static_cast<std::uint16_t>(area);
        });
    }
    const auto doorX = nearby(), doorY = nearby(), bx = nearby(), by = nearby();
    const auto owner = static_cast<std::uint8_t>(ctx.rng().Below(3));
    const auto pileGood = kGoods[ctx.rng().Below(sizeof(kGoods))];
    std::uint8_t extras[3];
    for (std::uint8_t& e : extras) {
        e = static_cast<std::uint8_t>(ctx.rng().Chance(1, 4) ? ctx.rng().Below(256) : ctx.rng().Below(60));
    }
    const auto allowed = static_cast<std::uint8_t>(ctx.rng().Chance(1, 8) ? 0 : 1 + ctx.rng().Below(255));
    worlds.Both([&](GameWorld* w) {
        w->settler_high_water = kEconomySettlers;
        w->building_high_water = 50;
        w->pile_high_water = kEconomyPiles;
        Area& a = w->areas[area];
        a.player = owner;
        TypeList(a, c.type) = Chain(w->settlers, typeMembers);
        a.settlers_by_type[0] = Chain(w->settlers, carrierMembers);
        for (std::size_t g = 0; g < sizeof(kGoods); ++g) {
            a.piles_by_goods[kGoods[g]] = Chain(w->piles, goodsMembers[g]);
        }
        a.pile_list = Chain(w->piles, emptyMembers);
        for (int k = 0; k < 3; ++k) {
            a.settings_extra[k] = extras[k];
        }
        a.settings5[pileGood] = allowed;
        Building& b = w->buildings[c.building];
        b.door_x = doorX;
        b.door_y = doorY;
        b.x = bx;
        b.y = by;
        b.owner = owner;
        w->tiles[doorX][doorY].area = static_cast<std::uint16_t>(area);
        Pile& p = w->piles[c.pile];
        p.good = pileGood;
        w->tiles[p.x][p.y].area = static_cast<std::uint16_t>(area);
    });
    return c;
}

template <typename Run>
void CompareEconomy(test::Context& ctx, int cases, Run run) {
    test::WorldPair worlds(ctx, kEconomyWorld);
    worlds.Both([&](GameWorld* w) { w->map_size = 384; });
    for (int i = 0; i < cases; ++i) {
        const EconomyCase c = SetUpEconomy(ctx, worlds);
        std::uint32_t expected = 0, actual = 0;
        run(worlds, c, expected, actual);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 16 == 15 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 15 before it): building %u type %u pile %u", i,
                     c.building, c.type, c.pile);
            return;
        }
    }
}

void CompareRequest(test::Context& ctx, RequestFn candidate, int cases) {
    CompareEconomy(ctx, cases, [&](test::WorldPair& worlds, const EconomyCase& c, std::uint32_t& expected,
                                   std::uint32_t& actual) {
        expected = Original_GameWorld_RequestSettlerForBuilding(worlds.original(), nullptr, c.building, c.type);
        actual = candidate(worlds.ours(), nullptr, c.building, c.type);
    });
}

void CompareDispatch(test::Context& ctx, PileFn candidate, int cases) {
    CompareEconomy(ctx, cases, [&](test::WorldPair& worlds, const EconomyCase& c, std::uint32_t&, std::uint32_t&) {
        Original_GameWorld_DispatchPileCarrier(worlds.original(), nullptr, c.pile);
        candidate(worlds.ours(), nullptr, c.pile);
    });
}

// Wrong: surplus carriers may become workers too.
std::uint32_t __fastcall WrongRequestSettlerForBuilding(GameWorld* world, void* edx, std::uint32_t number,
                                                        std::uint32_t type) {
    std::vector<std::uint32_t> flagged;
    for (std::uint32_t n = 1; n <= kEconomySettlers; ++n) {
        if ((world->settlers[n].flags & kSettlerFlagSurplus) != 0) {
            world->settlers[n].flags &= static_cast<std::uint8_t>(~kSettlerFlagSurplus);
            flagged.push_back(n);
        }
    }
    const std::uint32_t result = GameWorld_RequestSettlerForBuilding(world, edx, number, type);
    for (const std::uint32_t n : flagged) {
        world->settlers[n].flags |= kSettlerFlagSurplus;
    }
    return result;
}

// Wrong: ignores the area's setting that forbids moving the good.
void __fastcall WrongDispatchPileCarrier(GameWorld* world, void* edx, std::uint32_t number) {
    Pile& pile = world->piles[number];
    Area& area = world->areas[TileAt(world, pile.x, pile.y).area];
    std::uint8_t& allowed = area.settings5[pile.good];
    const std::uint8_t saved = allowed;
    allowed = 1;
    GameWorld_DispatchPileCarrier(world, edx, number);
    allowed = saved;
}

}  // namespace

S3RE_TEST(GameWorld_RequestSettlerForBuilding_MatchesOriginal) {
    CompareRequest(ctx, &GameWorld_RequestSettlerForBuilding, 3000);
}
S3RE_TEST(GameWorld_RequestSettlerForBuilding_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareRequest(probe, &WrongRequestSettlerForBuilding, 1000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_DispatchPileCarrier_MatchesOriginal) {
    CompareDispatch(ctx, &GameWorld_DispatchPileCarrier, 3000);
}
S3RE_TEST(GameWorld_DispatchPileCarrier_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDispatch(probe, &WrongDispatchPileCarrier, 1000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
