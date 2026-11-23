// Helpers of the command handler CLogicThread::ExecuteCommands (0x535990,
// re/execute_commands.md): the CGameData functions it calls that nothing
// else had reimplemented yet. All thiscall (ECX = world).
//
// CGameData::RecallSiteCarriers (0x004dee20): (building) ret 4. Only for a
//   construction site (buildup != 0): every settler 1..high water (re-read)
//   with an owner whose building (+0x22) is this one and whose job is a
//   carrier job (0xa, 0xb, 0xa9, 0xaa, 0xc, 0xd, 0x50) gets
//   ResetSettlerToIdle. Then, for any building, RemoveBuildingFromAreaRequests.
//   Commands 4 (stop a site), 0xf6 (no priority), and 0x4df8e0.
// CGameData::RecallBuildingCarriers (0x004deec0): (building) ret 4. The same
//   without the site check (command 4 for a finished building). It also
//   reads the building's words +0x10..+0x14 into its argument slot
//   (dead stores, not reproduced).
// CGameData::RecallCarriersOfOtherSites (0x004df8e0): (building) ret 4. For
//   every building 1..high water (re-read) of the same owner whose door tile
//   is in the same area as this building's door tile, that is a site
//   (+0x21 != 0) with request flag 0x01 and none of the request bits 0x1e:
//   RecallSiteCarriers. Command 0xf5 (give a site priority) calls it after
//   setting all request bits of the chosen site, so the other sites of the
//   area lose their carriers. The owner is re-read per building, the area
//   once. EAX unused.
// CGameData::ChangePileGood (0x004dfb40): (pile, good) ret 8. Only for a pile
//   with amount 0 and nothing coming (+0x3): a pile that has a goods type
//   (not 0xff) becomes empty (MakePileEmpty); then, unless good is 0xff,
//   SetPileGood(pile, good). Command 0x1c (storehouse goods). EAX unused.
// CGameData::SetPromotionCost (0x00508450): (player) ret 4. The mana cost of
//   the player's next soldier promotion (Player +0xb4) from the number of
//   promotions so far (sum of the soldier levels +0xb8/+0xbc/+0xc0): 10, 20,
//   30, 50, 60, 30 for 0..5, else -1. EAX = that sum. Command 0x18.
// CGameData::PlaceDockLine (0x004db710): (building, x, y) ret 0xc. A shipyard
//   (0x20) or harbour (0x21) gets its dock line near the tile the player
//   chose: of the 91 offset tiles around (x, y) (MapOffset table), those on
//   the map with terrain 5 (water) and no line marker (object 0x72) within
//   the offsets of distance < 10 are start candidates; from each, in each of
//   the 6 directions, up to 7 steps: the first shore tile (terrain 0x30) in
//   the building's door area ends the walk and is a result (the nearest to
//   the door by TileDistance, unsigned, wins; ties keep the first), a shore
//   tile of another area ends the walk too. With a result: from the start
//   tile step on while the tile's area word is not 0, one more step gives the
//   work tile (+0x30/+0x32) and the direction (+0x22); from there step on to
//   the shore, and back from the shore to the work tile (opposite direction)
//   every tile gets OpenDockLineTile(x, y, owner). EAX unused.
// CGameData::OpenDockLineTile (0x004dbab0): (x, y, player) ret 0xc: if the
//   tile has an owner, ClearTileOwner; SetOwnerOfFreeTile(x, y, player);
//   UnblockTile; object = 0x72 (the line marker). Only PlaceDockLine.
// CGameData::ResetAreaConditionCounters (0x0051bc20): (area, kind) ret 8.
//   Command 0x11 switches an area setting (Area +0x245[kind], 1..50) and
//   then clears the area's counters (+0x278, words) of every kind in the same
//   group (table below). EAX = 0, or the kind if it is in no group.
//
// Callees go through their S3.EXE addresses (all verified replacements).

#include "game/enums.h"
#include "game/typed_fields.h"
#include "game/world.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <vector>

namespace s3re::game {

namespace {

using layout::Area;
using layout::Building;
using layout::Pile;
using layout::Player;
using layout::Settler;
using layout::Tile;
using Job = SettlerJob;

constexpr std::uint8_t kFree = 0xff;     // owner byte of an unused record / unowned tile
constexpr std::uint8_t kNoGood = 0xff;   // Pile::good of a pile without a goods type
constexpr std::uint8_t kObjectDockLine = 114;  // 0x72: a tile of a shipyard's or harbour's dock line
constexpr std::uint8_t kTerrainWater = kTerrainWater5;

// Request bits of Building::request_flags (building_flags.cpp).
constexpr std::uint8_t kRequestsEnabled = 0x01;
constexpr std::uint8_t kRequestBits = 0x1e;

constexpr std::uintptr_t kResetSettlerToIdle = 0x0050cf50;
constexpr std::uintptr_t kRemoveBuildingFromAreaRequests = 0x004def80;
constexpr std::uintptr_t kRecallSiteCarriers = 0x004dee20;
constexpr std::uintptr_t kMakePileEmpty = 0x00506080;
constexpr std::uintptr_t kSetPileGood = 0x005061b0;
constexpr std::uintptr_t kNeighbourX = 0x00503700;
constexpr std::uintptr_t kNeighbourY = 0x00503730;
constexpr std::uintptr_t kTileDistance = 0x004ff320;
constexpr std::uintptr_t kClearTileOwner = 0x0051b640;
constexpr std::uintptr_t kSetOwnerOfFreeTile = 0x0051b5d0;
constexpr std::uintptr_t kUnblockTile = 0x0051b770;
constexpr std::uintptr_t kOpenDockLineTile = 0x004dbab0;

template <std::uintptr_t Address, typename... Args>
std::uint32_t Call(CGameData* world, Args... args) {
    using Fn = std::uint32_t(__fastcall*)(CGameData*, void*, decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(Address)(world, nullptr, static_cast<std::uint32_t>(args)...);
}
// NeighbourX/Y and TileDistance are stdcall with ECX = world.
template <std::uintptr_t Address, typename... Args>
std::uint32_t CallStd(CGameData* world, Args... args) {
    using Fn = std::uint32_t(__fastcall*)(CGameData*, void*, decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(Address)(world, nullptr, static_cast<std::uint32_t>(args)...);
}

Tile& TileAt(CGameData* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

// Carrier jobs that bring goods to or fetch them from a building.
bool IsCarrierJob(std::uint16_t job) {
    switch (static_cast<Job>(job)) {
    case Job::CarrierWalkToPile:
    case Job::CarrierAtPile:
    case Job::CarrierPickUp:
    case Job::CarrierLeavePile:
    case Job::CarrierWalkToBuilding:
    case Job::CarrierAtBuilding:
    case Job::CarrierDeliver:
        return true;
    default:
        return false;
    }
}

// 0x4dee4f..0x4deeb2: carriers of the building go idle, then the building leaves the area's request lists.
std::uint32_t RecallCarriers(CGameData* world, std::uint32_t building) {
    for (std::uint32_t n = 1; n <= world->settler_high_water; ++n) {
        const Settler& settler = world->settlers[n];
        if (settler.owner != kFree && settler.building == building && IsCarrierJob(settler.job)) {
            Call<kResetSettlerToIdle>(world, n);
        }
    }
    return Call<kRemoveBuildingFromAreaRequests>(world, building);
}

}  // namespace

// Purpose: Sends the carriers working for a construction site back to idle and withdraws its goods requests
// (the player stopped the site, or another site got priority).
std::uint32_t __fastcall CGameData_RecallSiteCarriers(CGameData* world, void*, std::uint32_t building) {
    if (world->buildings[building].buildup == 0) {
        return Call<kRemoveBuildingFromAreaRequests>(world, building);
    }
    return RecallCarriers(world, building);
}

// Purpose: Sends the carriers working for a building back to idle and withdraws its goods requests (the
// player stopped the building's work).
std::uint32_t __fastcall CGameData_RecallBuildingCarriers(CGameData* world, void*, std::uint32_t building) {
    return RecallCarriers(world, building);
}

// Purpose: When the player gives one construction site priority, the other waiting sites of the same economy
// area lose their carriers, so the materials go to the chosen site first (probably).
void __fastcall CGameData_RecallCarriersOfOtherSites(CGameData* world, void*, std::uint32_t building) {
    const Building& chosen = world->buildings[building];
    const std::uint16_t area = world->tiles[chosen.door_x][chosen.door_y].area;
    for (std::uint32_t n = 1; n <= world->building_high_water; ++n) {
        const Building& other = world->buildings[n];
        if (other.owner == chosen.owner && world->tiles[other.door_x][other.door_y].area == area &&
            other.buildup != 0 && (other.request_flags & kRequestsEnabled) != 0 &&
            (other.request_flags & kRequestBits) == 0) {
            Call<kRecallSiteCarriers>(world, n);
        }
    }
}

// Purpose: Changes which goods an empty storehouse pile takes (or makes it take none), as the player set it
// in the storehouse menu (probably).
void __fastcall CGameData_ChangePileGood(CGameData* world, void*, std::uint32_t pile, std::uint32_t good) {
    const Pile& record = world->piles[pile];
    if (record.amount != 0 || record.coming != 0) {
        return;
    }
    const auto newGood = static_cast<std::uint8_t>(good);
    if (record.good != kNoGood) {
        Call<kMakePileEmpty>(world, pile);
    }
    if (newGood != kNoGood) {
        Call<kSetPileGood>(world, pile, good);
    }
}

// Purpose: Sets the mana price of the player's next soldier promotion, which rises with the promotions bought.
std::int32_t __fastcall CGameData_SetPromotionCost(CGameData* world, void*, std::uint32_t player) {
    static constexpr std::int32_t kCostByPromotions[] = {10, 20, 30, 50, 60, 30};
    constexpr std::int32_t kNoMorePromotions = -1;
    Player& record = world->players[player];
    // The soldier levels (swords, bows, pikes) and the price.
    const auto promotions = static_cast<std::int32_t>(static_cast<std::uint32_t>(record.soldier_levels[2]) +
                                                      static_cast<std::uint32_t>(record.soldier_levels[0]) +
                                                      static_cast<std::uint32_t>(record.soldier_levels[1]));
    const bool listed = promotions >= 0 && promotions < static_cast<std::int32_t>(std::size(kCostByPromotions));
    record.promotion_cost = listed ? kCostByPromotions[promotions] : kNoMorePromotions;
    return promotions;
}

// Purpose: Lays one tile of a shipyard's or harbour's dock line: the tile becomes the player's, walkable, and
// gets the line marker.
void __fastcall CGameData_OpenDockLineTile(CGameData* world, void*, std::uint32_t x, std::uint32_t y,
                                           std::uint32_t player) {
    Tile& tile = TileAt(world, x, y);
    if (tile.owner != kFree) {
        Call<kClearTileOwner>(world, x, y);
    }
    Call<kSetOwnerOfFreeTile>(world, x, y, player);
    Call<kUnblockTile>(world, x, y);
    tile.object = kObjectDockLine;
}

namespace {

constexpr std::uint32_t kDockSearchOffsets = 91;  // MapOffset entries searched around the chosen tile
constexpr std::int32_t kMarkerClearance = 10;      // no line marker at offsets of a smaller distance
constexpr std::uint32_t kDirections = 6;
constexpr std::uint32_t kMaxWalk = 7;              // steps from a water tile towards the shore

struct DockResult {
    bool found = false;
    std::uint32_t direction = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t distance = 0xffffffff;
};

bool OnMap(const CGameData* world, std::uint32_t x, std::uint32_t y) {
    return x < world->map_size && y < world->map_size;
}

// 0x4db793..0x4db7e5: a line marker on one of the near offset tiles around (x, y).
bool MarkerNear(CGameData* world, std::uint32_t x, std::uint32_t y) {
    bool found = false;
    for (std::uint32_t k = 0; world->offsets[k].distance < kMarkerClearance; ++k) {
        const std::uint32_t nx = static_cast<std::uint32_t>(world->offsets[k].dx) + x;
        const std::uint32_t ny = static_cast<std::uint32_t>(world->offsets[k].dy) + y;
        if (OnMap(world, nx, ny) && world->tiles[nx][ny].object == kObjectDockLine) {
            found = true;
        }
    }
    return found;
}

// 0x4db7eb..0x4db90e: from the water tile (x, y), the shore tiles of the building's area reachable in a
// straight line; keeps the one nearest to the door.
void WalkToShores(CGameData* world, const Building& building, std::uint32_t x, std::uint32_t y,
                  DockResult& best) {
    for (std::uint32_t direction = 0; direction < kDirections; ++direction) {
        std::uint32_t wx = x;
        std::uint32_t wy = y;
        for (std::uint32_t step = 0; step < kMaxWalk; ++step) {
            wx = CallStd<kNeighbourX>(world, wx, direction);
            wy = CallStd<kNeighbourY>(world, wy, direction);
            if (!OnMap(world, wx, wy) || world->tiles[wx][wy].terrain != kTerrainShore) {
                continue;
            }
            // The door is re-read per shore tile (it doesn't change).
            if (world->tiles[wx][wy].area != world->tiles[building.door_x][building.door_y].area) {
                break;  // a shore of another area ends this direction
            }
            const std::uint32_t distance =
                CallStd<kTileDistance>(world, wx, wy, building.door_x, building.door_y);
            if (distance < best.distance) {
                best = {true, direction, x, y, distance};
            }
            break;  // the first shore tile of the area ends this direction
        }
    }
}

std::uint32_t StepX(CGameData* world, std::uint32_t x, std::uint32_t direction) {
    return CallStd<kNeighbourX>(world, x, direction);
}
std::uint32_t StepY(CGameData* world, std::uint32_t y, std::uint32_t direction) {
    return CallStd<kNeighbourY>(world, y, direction);
}

}  // namespace

// Purpose: Places a shipyard's or harbour's dock line where the player clicked: finds the water tile next to
// the shore of the building's area nearest to its door and lays the line from there to the shore.
void __fastcall CGameData_PlaceDockLine(CGameData* world, void*, std::uint32_t number, std::uint32_t x,
                                        std::uint32_t y) {
    Building& building = world->buildings[number];
    DockResult best;
    // 0x4db74b..0x4db928: the start candidates.
    for (std::uint32_t k = 0; k < kDockSearchOffsets; ++k) {
        const std::uint32_t cx = static_cast<std::uint32_t>(world->offsets[k].dx) + x;
        const std::uint32_t cy = static_cast<std::uint32_t>(world->offsets[k].dy) + y;
        if (!OnMap(world, cx, cy) || world->tiles[cx][cy].terrain != kTerrainWater) {
            continue;
        }
        if (!MarkerNear(world, cx, cy)) {
            WalkToShores(world, building, cx, cy, best);
        }
    }
    if (!best.found) {
        return;
    }
    // 0x4db93a..0x4db9c0: off the start tile's water body, then one step more.
    std::uint32_t lx = best.x;
    std::uint32_t ly = best.y;
    while (TileAt(world, lx, ly).area != 0) {
        lx = StepX(world, lx, best.direction);
        ly = StepY(world, ly, best.direction);
    }
    std::uint32_t wx = StepX(world, lx, best.direction);
    std::uint32_t wy = StepY(world, ly, best.direction);
    building.work_x = static_cast<std::uint16_t>(wx);
    building.work_y = static_cast<std::uint16_t>(wy);
    building.dock_direction = static_cast<std::uint16_t>(best.direction);
    // 0x4db9fd..0x4dba43: on to the shore.
    while (TileAt(world, wx, wy).terrain != kTerrainShore) {
        wx = StepX(world, wx, best.direction);
        wy = StepY(world, wy, best.direction);
    }
    // 0x4dba45..0x4dbaa3: back to the work tile, laying the line.
    const std::uint32_t back = (best.direction + kDirections / 2) % kDirections;
    while (wx != building.work_x || wy != building.work_y) {
        wx = StepX(world, wx, back);
        wy = StepY(world, wy, back);
        Call<kOpenDockLineTile>(world, wx, wy, building.owner);
    }
}

namespace {

// The kinds whose counters ResetAreaConditionCounters clears together (0x51bc20's compare chain).
constexpr std::uint8_t kConditionGroups[][8] = {
    {1, 2, 3, 4, 5, 6},
    {7, 8, 9, 10, 11, 12, 47},
    {13, 14, 15, 43, 44, 45, 46},
    {16, 17},
    {18, 19},
    {20, 21},
    {22, 23, 24, 25},
    {26, 27, 28, 29, 50},
    {30, 31, 32, 33, 34},
    {35, 36, 37, 38},
    {39, 40, 41, 42},
    {48, 49},
};

}  // namespace

// Purpose: When the player changes one of an economy area's settings (menu 3), the counters that belong to
// that group of settings start again from 0 (probably the production counts the settings refer to).
std::uint32_t __fastcall CGameData_ResetAreaConditionCounters(CGameData* world, void*, std::uint32_t area,
                                                              std::uint32_t kind) {
    for (const auto& group : kConditionGroups) {
        bool member = false;
        for (std::uint8_t k : group) {
            member = member || (k != 0 && k == kind);
        }
        if (!member) {
            continue;
        }
        Area& record = world->areas[area];
        for (std::uint8_t k : group) {
            if (k != 0) {
                record.counters_278[k] = 0;
            }
        }
        return 0;
    }
    return kind;
}

using OneArgFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t a);
using VoidOneArgFn = void(__fastcall*)(CGameData* world, void* edx, std::uint32_t a);
using PileGoodFn = void(__fastcall*)(CGameData* world, void* edx, std::uint32_t pile, std::uint32_t good);
using PromotionFn = std::int32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t player);
using ThreeArgFn = void(__fastcall*)(CGameData* world, void* edx, std::uint32_t a, std::uint32_t b,
                                     std::uint32_t c);
using TwoArgFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t a, std::uint32_t b);

S3RE_HOOK(0x004dee20, CGameData_RecallSiteCarriers, OneArgFn, Original_CGameData_RecallSiteCarriers,
          CGameData_RecallSiteCarriers);
S3RE_HOOK(0x004deec0, CGameData_RecallBuildingCarriers, OneArgFn, Original_CGameData_RecallBuildingCarriers,
          CGameData_RecallBuildingCarriers);
S3RE_HOOK(0x004df8e0, CGameData_RecallCarriersOfOtherSites, VoidOneArgFn,
          Original_CGameData_RecallCarriersOfOtherSites, CGameData_RecallCarriersOfOtherSites);
S3RE_HOOK(0x004dfb40, CGameData_ChangePileGood, PileGoodFn, Original_CGameData_ChangePileGood,
          CGameData_ChangePileGood);
S3RE_HOOK(0x00508450, CGameData_SetPromotionCost, PromotionFn, Original_CGameData_SetPromotionCost,
          CGameData_SetPromotionCost);
S3RE_HOOK(0x004dbab0, CGameData_OpenDockLineTile, ThreeArgFn, Original_CGameData_OpenDockLineTile,
          CGameData_OpenDockLineTile);
S3RE_HOOK(0x004db710, CGameData_PlaceDockLine, ThreeArgFn, Original_CGameData_PlaceDockLine,
          CGameData_PlaceDockLine);
S3RE_HOOK(0x0051bc20, CGameData_ResetAreaConditionCounters, TwoArgFn,
          Original_CGameData_ResetAreaConditionCounters, CGameData_ResetAreaConditionCounters);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// Runs `call(world)` on both worlds with the callees stubbed (recorded), then compares the stub calls and
// the whole world (every `checkpoint` cases) or the given checks.
struct StubbedPair {
    test::WorldPair worlds;
    test::CalleeStubs stubs;

    StubbedPair(test::Context& ctx, std::size_t size) : worlds(ctx, size) {}

    template <typename Original, typename Ours>
    bool Run(test::Context& ctx, int i, bool checkpoint, Original original, Ours ours) {
        stubs.SetWorld(worlds.original());
        const auto expected = original(worlds.original());
        auto expectedCalls = stubs.Take();
        stubs.SetWorld(worlds.ours());
        const auto actual = ours(worlds.ours());
        auto actualCalls = stubs.Take();
        bool same = expected == actual && expectedCalls == actualCalls;
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: result %u / %u, calls %zu / %zu", i,
                     static_cast<unsigned>(expected), static_cast<unsigned>(actual), expectedCalls.size(),
                     actualCalls.size());
            return false;
        }
        if (checkpoint && !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: world differs", i);
            return false;
        }
        return true;
    }
};

constexpr std::uint32_t kTestBuildings = 60;
constexpr std::size_t kBuildingWorld = offsetof(CGameData, buildings) + (kTestBuildings + 2) * sizeof(Building);
constexpr std::uint16_t kTestJobs[] = {10, 11, 12, 13, 80, 169, 170, 9, 14, 79, 81, 168, 171, 0x10a};

// Settlers with carrier jobs around a few buildings; a stub ResetSettlerToIdle changes the job (so a
// second pass could see it) and returns a value, RemoveBuildingFromAreaRequests returns a value.
void FillCarriers(test::Context& ctx, test::WorldPair& worlds, std::uint32_t settlers) {
    worlds.Both([&](CGameData* w) { w->settler_high_water = settlers; });
    for (std::uint32_t n = 0; n <= settlers + 1; ++n) {
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Chance(1, 8) ? kFree : ctx.rng().Below(4));
        const auto building = static_cast<std::uint16_t>(ctx.rng().Below(kTestBuildings));
        const auto job = kTestJobs[ctx.rng().Below(static_cast<std::uint32_t>(std::size(kTestJobs)))];
        worlds.Both([&](CGameData* w) {
            w->settlers[n].owner = owner;
            w->settlers[n].building = building;
            w->settlers[n].job = job;
        });
    }
}

void InstallCarrierStubs(test::CalleeStubs& stubs) {
    stubs.Install<1, kResetSettlerToIdle>([](CGameData* w, std::uint32_t, const std::uint32_t* args) {
        w->settlers[args[0]].job = 0xac;
        return args[0] * 3;
    });
    stubs.Install<1, kRemoveBuildingFromAreaRequests>(
        [](CGameData*, std::uint32_t, const std::uint32_t* args) { return args[0] + 0x1000; });
}

template <typename Fn>
void CompareRecall(test::Context& ctx, Fn original, Fn ours, int cases) {
    StubbedPair pair(ctx, kBuildingWorld);
    InstallCarrierStubs(pair.stubs);
    for (int i = 0; i < cases; ++i) {
        FillCarriers(ctx, pair.worlds, ctx.rng().Below(200));
        const std::uint32_t building = ctx.rng().Below(kTestBuildings);
        const auto buildup = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Below(256));
        pair.worlds.Both([&](CGameData* w) { w->buildings[building].buildup = buildup; });
        if (!pair.Run(
                ctx, i, i % 64 == 63, [&](CGameData* w) { return original(w, nullptr, building); },
                [&](CGameData* w) { return ours(w, nullptr, building); })) {
            return;
        }
    }
    pair.worlds.Checkpoint(ctx);
}

// Wrong: job 0x50 (CarrierDeliver) is not recalled.
std::uint32_t __fastcall WrongRecallBuildingCarriers(CGameData* world, void*, std::uint32_t building) {
    for (std::uint32_t n = 1; n <= world->settler_high_water; ++n) {
        const Settler& settler = world->settlers[n];
        if (settler.owner != kFree && settler.building == building && IsCarrierJob(settler.job) &&
            settler.job != Value(Job::CarrierDeliver)) {
            Call<kResetSettlerToIdle>(world, n);
        }
    }
    return Call<kRemoveBuildingFromAreaRequests>(world, building);
}

}  // namespace

S3RE_TEST(CGameData_RecallSiteCarriers_MatchesOriginal) {
    CompareRecall<OneArgFn>(ctx, Original_CGameData_RecallSiteCarriers, &CGameData_RecallSiteCarriers, 3000);
}
S3RE_TEST(CGameData_RecallBuildingCarriers_MatchesOriginal) {
    CompareRecall<OneArgFn>(ctx, Original_CGameData_RecallBuildingCarriers, &CGameData_RecallBuildingCarriers,
                            3000);
}
S3RE_TEST(CGameData_RecallBuildingCarriers_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareRecall<OneArgFn>(probe, Original_CGameData_RecallBuildingCarriers, &WrongRecallBuildingCarriers, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

void CompareOtherSites(test::Context& ctx, VoidOneArgFn original, VoidOneArgFn ours, int cases) {
    StubbedPair pair(ctx, kBuildingWorld);
    pair.stubs.Install<1, kRecallSiteCarriers>();
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t count = ctx.rng().Below(kTestBuildings);
        pair.worlds.Both([&](CGameData* w) { w->building_high_water = count; });
        for (std::uint32_t n = 0; n <= kTestBuildings; ++n) {
            const auto owner = static_cast<std::uint8_t>(ctx.rng().Below(3));
            const auto doorX = static_cast<std::uint16_t>(ctx.rng().Below(4));
            const auto doorY = static_cast<std::uint16_t>(ctx.rng().Below(4));
            const auto buildup = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3) ? 0 : 1 + ctx.rng().Below(9));
            const auto flags = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? ctx.rng().Below(2) : ctx.rng().Below(256));
            pair.worlds.Both([&](CGameData* w) {
                Building& b = w->buildings[n];
                b.owner = owner;
                b.door_x = doorX;
                b.door_y = doorY;
                b.buildup = buildup;
                b.request_flags = flags;
            });
        }
        for (std::uint16_t x = 0; x < 4; ++x) {
            for (std::uint16_t y = 0; y < 4; ++y) {
                const auto area = static_cast<std::uint16_t>(ctx.rng().Below(3));
                pair.worlds.Both([&](CGameData* w) { w->tiles[x][y].area = area; });
            }
        }
        const std::uint32_t building = 1 + ctx.rng().Below(kTestBuildings);
        if (!pair.Run(
                ctx, i, i % 64 == 63,
                [&](CGameData* w) {
                    original(w, nullptr, building);
                    return 0;
                },
                [&](CGameData* w) {
                    ours(w, nullptr, building);
                    return 0;
                })) {
            return;
        }
    }
}

// Wrong: the area is not compared.
void __fastcall WrongRecallCarriersOfOtherSites(CGameData* world, void*, std::uint32_t building) {
    const Building& chosen = world->buildings[building];
    for (std::uint32_t n = 1; n <= world->building_high_water; ++n) {
        const Building& other = world->buildings[n];
        if (other.owner == chosen.owner && other.buildup != 0 && (other.request_flags & kRequestsEnabled) != 0 &&
            (other.request_flags & kRequestBits) == 0) {
            Call<kRecallSiteCarriers>(world, n);
        }
    }
}

}  // namespace

S3RE_TEST(CGameData_RecallCarriersOfOtherSites_MatchesOriginal) {
    CompareOtherSites(ctx, Original_CGameData_RecallCarriersOfOtherSites, &CGameData_RecallCarriersOfOtherSites,
                      3000);
}
S3RE_TEST(CGameData_RecallCarriersOfOtherSites_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareOtherSites(probe, Original_CGameData_RecallCarriersOfOtherSites, &WrongRecallCarriersOfOtherSites, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

constexpr std::uint32_t kTestPiles = 100;
constexpr std::size_t kPileWorld = offsetof(CGameData, piles) + (kTestPiles + 2) * sizeof(Pile);

void ComparePileGood(test::Context& ctx, PileGoodFn original, PileGoodFn ours, int cases) {
    StubbedPair pair(ctx, kPileWorld);
    pair.stubs.Install<1, kMakePileEmpty>([](CGameData* w, std::uint32_t, const std::uint32_t* args) {
        w->piles[args[0]].good = kNoGood;
        return 7u;
    });
    pair.stubs.Install<2, kSetPileGood>([](CGameData* w, std::uint32_t, const std::uint32_t* args) {
        w->piles[args[0]].good = static_cast<std::uint8_t>(args[1]);
        return 9u;
    });
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t pile = ctx.rng().Below(kTestPiles);
        const auto amount = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Below(9));
        const auto coming = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Below(9));
        const auto good = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3) ? kNoGood : ctx.rng().Below(40));
        pair.worlds.Both([&](CGameData* w) {
            w->piles[pile].amount = amount;
            w->piles[pile].coming = coming;
            w->piles[pile].good = good;
        });
        // Garbage in the upper bytes: only the low byte is compared with 0xff.
        std::uint32_t newGood = ctx.rng().Chance(1, 3) ? 0xff : ctx.rng().Below(40);
        if (ctx.rng().Chance(1, 4)) {
            newGood |= ctx.rng().Next32() & 0xffffff00;
        }
        if (!pair.Run(
                ctx, i, i % 128 == 127,
                [&](CGameData* w) {
                    original(w, nullptr, pile, newGood);
                    return 0;
                },
                [&](CGameData* w) {
                    ours(w, nullptr, pile, newGood);
                    return 0;
                })) {
            return;
        }
    }
}

// Wrong: a pile with goods coming is changed too.
void __fastcall WrongChangePileGood(CGameData* world, void*, std::uint32_t pile, std::uint32_t good) {
    const Pile& record = world->piles[pile];
    if (record.amount != 0) {
        return;
    }
    if (record.good != kNoGood) {
        Call<kMakePileEmpty>(world, pile);
    }
    if (static_cast<std::uint8_t>(good) != kNoGood) {
        Call<kSetPileGood>(world, pile, good);
    }
}

}  // namespace

S3RE_TEST(CGameData_ChangePileGood_MatchesOriginal) {
    ComparePileGood(ctx, Original_CGameData_ChangePileGood, &CGameData_ChangePileGood, 5000);
}
S3RE_TEST(CGameData_ChangePileGood_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePileGood(probe, Original_CGameData_ChangePileGood, &WrongChangePileGood, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CGameData_SetPromotionCost_MatchesOriginal) {
    constexpr std::size_t kSize = offsetof(CGameData, players) + sizeof(CGameData::players);
    test::WorldPair worlds(ctx, kSize);
    for (int i = 0; i < 20000; ++i) {
        const std::uint32_t player = ctx.rng().Below(20);
        for (int k = 1; k <= 3; ++k) {
            const auto level = static_cast<std::int32_t>(ctx.rng().Chance(1, 8) ? ctx.rng().Next32()
                                                                                  : ctx.rng().Below(5) - 1);
            worlds.Both([&](CGameData* w) { w->players[player].soldier_levels[k - 1] = level; });
        }
        const auto expected = static_cast<std::int32_t>(
            Original_CGameData_SetPromotionCost(worlds.original(), nullptr, player));
        const std::int32_t actual = CGameData_SetPromotionCost(worlds.ours(), nullptr, player);
        if (expected != actual || !worlds.SameField(ctx, [&](CGameData* w) -> auto& { return w->players[player]; })) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: player %u, EAX %d / %d", i, player, expected, actual);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

namespace {

void CompareConditionCounters(test::Context& ctx, TwoArgFn original, TwoArgFn ours, int cases) {
    constexpr std::uint32_t kAreas = 8;
    constexpr std::size_t kSize = offsetof(CGameData, areas) + (kAreas + 1) * sizeof(Area);
    test::WorldPair worlds(ctx, kSize);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t area = ctx.rng().Below(kAreas);
        const std::uint32_t kind = ctx.rng().Chance(1, 10) ? ctx.rng().Next32() : ctx.rng().Below(53);
        if (ctx.rng().Chance(1, 4)) {
            std::uint16_t counters[51];
            ctx.rng().Fill(counters, sizeof(counters));
            worlds.Both([&](CGameData* w) { std::memcpy(w->areas[area].counters_278, counters, sizeof(counters)); });
        }
        const std::uint32_t expected = original(worlds.original(), nullptr, area, kind);
        const std::uint32_t actual = ours(worlds.ours(), nullptr, area, kind);
        if (expected != actual || !worlds.SameField(ctx, [&](CGameData* w) -> auto& { return w->areas[area]; })) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: area %u, kind %u, EAX %u / %u", i, area, kind, expected, actual);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

// Wrong: kind 47 is not in the group of 7..12.
std::uint32_t __fastcall WrongResetAreaConditionCounters(CGameData* world, void* edx, std::uint32_t area,
                                                         std::uint32_t kind) {
    if (kind == 47) {
        return kind;
    }
    return CGameData_ResetAreaConditionCounters(world, edx, area, kind);
}

}  // namespace

S3RE_TEST(CGameData_ResetAreaConditionCounters_MatchesOriginal) {
    CompareConditionCounters(ctx, Original_CGameData_ResetAreaConditionCounters,
                             &CGameData_ResetAreaConditionCounters, 20000);
}
S3RE_TEST(CGameData_ResetAreaConditionCounters_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareConditionCounters(probe, Original_CGameData_ResetAreaConditionCounters, &WrongResetAreaConditionCounters,
                             5000);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

// A world with the whole tile array (walks may leave the map) and the offset table.
constexpr std::size_t kDockWorld = offsetof(CGameData, buildings) + 12 * sizeof(Building);
constexpr std::uint32_t kDockMap = 128;

// Terrain on and around the map: water, shore and land in patches (shore often enough that every walk ends
// soon), areas 0 or small, line markers here and there; the offset table rings around (0, 0) like the
// game's (distance = ring number).
void FillDockMap(test::Context& ctx, test::WorldPair& worlds) {
    std::vector<std::uint8_t> terrain(0x300 * 0x300);
    std::vector<std::uint16_t> area(0x300 * 0x300);
    for (std::size_t i = 0; i < terrain.size(); ++i) {
        const std::uint32_t r = ctx.rng().Below(10);
        terrain[i] = r < 4 ? kTerrainWater : r < 7 ? kTerrainShore : static_cast<std::uint8_t>(ctx.rng().Below(64));
        area[i] = static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Below(3));
    }
    worlds.Both([&](CGameData* w) {
        w->map_size = kDockMap;
        for (std::uint32_t x = 0; x < 0x300; ++x) {
            for (std::uint32_t y = 0; y < 0x300; ++y) {
                w->tiles[x][y].terrain = terrain[x * 0x300 + y];
                w->tiles[x][y].area = area[x * 0x300 + y];
            }
        }
    });
    for (std::uint32_t x = 0; x < kDockMap + 16; ++x) {
        for (std::uint32_t y = 0; y < kDockMap + 16; ++y) {
            const auto object = static_cast<std::uint8_t>(ctx.rng().Chance(1, 60) ? kObjectDockLine : ctx.rng().Below(100));
            worlds.Both([&](CGameData* w) { w->tiles[x][y].object = object; });
        }
    }
    // Offsets: ring by ring (hexagonal distance), enough entries for 91 + the marker check.
    std::uint32_t k = 0;
    for (std::int32_t ring = 0; ring < 8 && k < 200; ++ring) {
        for (std::int32_t dx = -ring; dx <= ring; ++dx) {
            for (std::int32_t dy = -ring; dy <= ring; ++dy) {
                const std::int32_t d = std::max({dx < 0 ? -dx : dx, dy < 0 ? -dy : dy, (dx - dy) < 0 ? dy - dx : dx - dy});
                if (d != ring || k >= 200) {
                    continue;
                }
                worlds.Both([&](CGameData* w) { w->offsets[k] = {dx, dy, ring, 0}; });
                ++k;
            }
        }
    }
}

void CompareDockLine(test::Context& ctx, ThreeArgFn original, ThreeArgFn ours, int cases) {
    StubbedPair pair(ctx, kDockWorld);
    pair.stubs.Install<3, kOpenDockLineTile>();
    FillDockMap(ctx, pair.worlds);
    for (int i = 0; i < cases; ++i) {
        if (i % 50 == 49) {
            FillDockMap(ctx, pair.worlds);
        }
        const std::uint32_t building = 1 + ctx.rng().Below(10);
        const auto doorX = static_cast<std::uint16_t>(ctx.rng().Below(kDockMap));
        const auto doorY = static_cast<std::uint16_t>(ctx.rng().Below(kDockMap));
        pair.worlds.Both([&](CGameData* w) {
            w->buildings[building].door_x = doorX;
            w->buildings[building].door_y = doorY;
        });
        // Positions near the map edges too (candidates off the map).
        const std::uint32_t x = ctx.rng().Chance(1, 10) ? ctx.rng().Below(kDockMap) : 8 + ctx.rng().Below(kDockMap - 16);
        const std::uint32_t y = ctx.rng().Chance(1, 10) ? ctx.rng().Below(kDockMap) : 8 + ctx.rng().Below(kDockMap - 16);
        if (!pair.Run(
                ctx, i, i % 100 == 99,
                [&](CGameData* w) {
                    original(w, nullptr, building, x, y);
                    return 0;
                },
                [&](CGameData* w) {
                    ours(w, nullptr, building, x, y);
                    return 0;
                })) {
            return;
        }
        if (!pair.worlds.SameField(ctx, [&](CGameData* w) -> auto& { return w->buildings[building]; })) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: building %u differs", i, building);
            return;
        }
    }
}

// Wrong: ties go to the last candidate.
void __fastcall WrongPlaceDockLine(CGameData* world, void*, std::uint32_t number, std::uint32_t x, std::uint32_t y) {
    Building& building = world->buildings[number];
    DockResult best;
    for (std::uint32_t k = 0; k < kDockSearchOffsets; ++k) {
        const std::uint32_t cx = static_cast<std::uint32_t>(world->offsets[k].dx) + x;
        const std::uint32_t cy = static_cast<std::uint32_t>(world->offsets[k].dy) + y;
        if (!OnMap(world, cx, cy) || world->tiles[cx][cy].terrain != kTerrainWater || MarkerNear(world, cx, cy)) {
            continue;
        }
        DockResult candidate = best;
        candidate.distance = best.found ? best.distance + 1 : best.distance;
        WalkToShores(world, building, cx, cy, candidate);
        if (candidate.found && (!best.found || candidate.distance <= best.distance)) {
            best = candidate;
        }
    }
    if (!best.found) {
        return;
    }
    building.work_x = static_cast<std::uint16_t>(best.x);
    building.work_y = static_cast<std::uint16_t>(best.y);
    building.dock_direction = static_cast<std::uint16_t>(best.direction);
}

}  // namespace

S3RE_TEST(CGameData_PlaceDockLine_MatchesOriginal) {
    CompareDockLine(ctx, Original_CGameData_PlaceDockLine, &CGameData_PlaceDockLine, 1500);
}
S3RE_TEST(CGameData_PlaceDockLine_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDockLine(probe, Original_CGameData_PlaceDockLine, &WrongPlaceDockLine, 300);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CGameData_OpenDockLineTile_MatchesOriginal) {
    StubbedPair pair(ctx, offsetof(CGameData, presence_max_player));
    pair.stubs.Install<2, kClearTileOwner>();
    pair.stubs.Install<3, kSetOwnerOfFreeTile>([](CGameData* w, std::uint32_t, const std::uint32_t* args) {
        w->tiles[args[0]][args[1]].owner = static_cast<std::uint8_t>(args[2]);
        return 1u;
    });
    pair.stubs.Install<2, kUnblockTile>();
    for (int i = 0; i < 5000; ++i) {
        const std::uint32_t x = ctx.rng().Below(0x300);
        const std::uint32_t y = ctx.rng().Below(0x300);
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? kFree : ctx.rng().Below(20));
        pair.worlds.Both([&](CGameData* w) { w->tiles[x][y].owner = owner; });
        const std::uint32_t player = ctx.rng().Chance(1, 4) ? ctx.rng().Next32() : ctx.rng().Below(20);
        if (!pair.Run(
                ctx, i, i % 500 == 499,
                [&](CGameData* w) {
                    Original_CGameData_OpenDockLineTile(w, nullptr, x, y, player);
                    return 0;
                },
                [&](CGameData* w) {
                    CGameData_OpenDockLineTile(w, nullptr, x, y, player);
                    return 0;
                })) {
            return;
        }
    }
}

}  // namespace s3re::game
