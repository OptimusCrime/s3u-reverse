// Walking settlers (UpdateSettlers' walking jobs) and the job lists the
// pathfinder uses.
//
// CGameData::AddTileTraffic (0x00503940): (x, y): unless the tile's traffic
//   byte (+0x12) is 255 it is increased by one; when it reaches 12 or more,
//   CGameData::WearPath (0x51b7f0) is called for the tile (which adds 9 when
//   the tile becomes a path). thiscall, ret 8. EAX = the address of the
//   traffic byte, or WearPath's EAX when it was called (the only caller,
//   WalkSettler 0x500860, ignores it).
// CGameData::IsWalkingJob (0x004fa6d0): (job) -> AL = 1 for the 47 jobs that
//   UpdateSettlers hands to WalkSettler (handler 0x4ead27 and the two jobs
//   UnitWalk and UnitWalkCountdown that call it too), else 0. The upper 24 bits of EAX
//   are those of the argument. ret 4, ECX unused. Called by 0x4a33d0.
// CGameData::IsStationaryJob (0x004fa880, was Logic_sub_4fa880, proposed
//   "IsWalkingOrIdleJob"): (job) -> AL = 0 for the same 47 walking jobs and
//   the idle jobs Nothing and UnitIdle, AL = 1 for every other job (the
//   settler is working at a place). Upper EAX bits as above. ret 4, ECX
//   unused. Called by Pathing::TileCost (0x4a36a0): a tile whose settler is
//   busy counts as blocked.
// CGameData::WalkSettler (0x00500860, was Logic_sub_500860): (settler) ->
//   EAX = 1 when UpdateSettlers should handle the settler again at once
//   (it stores EAX at 0x4ead2f), else 0. thiscall, ret 4. One step of a
//   walking settler (the 47 walking jobs):
//   1. animation frame +0x16 = (+0x16 + +0x19) % (catapult ? 36 : 12).
//   2. Not at the target (+0x24/+0x26), and not within path_cache[5]
//      steps of (path_cache[2], [3]) when path_cache[4] is kNearMark: a
//      step (WalkStep). A target off the map logs "Settler tries to reach
//      spot out of map". The catapult stands on seven tiles (part settlers
//      chained through +0x20): WalkSevenTileUnit. Ferries, and trade ships
//      unless +0x3c is 255: WalkShip. Everyone else: WalkOne.
//   3. At the target: +0x16 = 0 and the job's next step (Arrive): most jobs
//      become the next job; some step onto their building's tile A with an
//      animation; WalkToOwnArea, the ships' waypoint legs and the
//      UnitWalk / UnitWalkCountdown walk states have their own handlers below.
//   Known difference: Amazon soldiers that shoot at a catapult or ship
//   with a delay roll above 20000 take the delay from the building last
//   looked at in the target search (+0x38 + 1); when no building was
//   looked at, the original reads an uninitialized local; ours uses
//   building 0.

#include "game/debug_log.h"
#include "game/enums/races.h"
#include "game/enums/settler_types.h"
#include "game/enums/buildings.h"
#include "game/enums/job_ids.h"
#include "game/enums/terrain.h"
#include "game/enums/directions.h"
#include "game/globals.h"
#include "game/pathing/pathing.h"
#include "game/raw.h"
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
#include <vector>

namespace s3re::game {

namespace {

using layout::Tile;
using Job = SettlerJob;

// UpdateSettlers' walking jobs (IsWalkingJob's list, 0x4fa6d0).
constexpr Job kWalkingJobs[] = {Job::StepAside,
                                Job::WalkToWork,
                                Job::DiggerWalkToSpot,
                                Job::CarrierWalkToPile,
                                Job::CarrierWalkToBuilding,
                                Job::BuilderWalkToWait,
                                Job::BuilderWalkToSpot,
                                Job::WoodcutterWalkToTree,
                                Job::StonecutterWalkToStone,
                                Job::WoodcutterWalkHome,
                                Job::StonecutterWalkHome,
                                Job::ForesterWalkToSpot,
                                Job::ForesterWalkHome,
                                Job::UnitWalk,
                                Job::CarrierWalkToSourcePile,
                                Job::CarrierWalkToTargetPile,
                                Job::CarrierWalkToTool,
                                Job::FarmerWalkToSow,
                                Job::FarmerWalkHome,
                                Job::FarmerWalkHomeLoaded,
                                Job::FarmerWalkToHarvest,
                                Job::WalkToOwnArea,
                                Job::UnitWalkCountdown,
                                Job::FisherWalkToWater,
                                Job::FisherWalkHome,
                                Job::WaterworkerWalkToWater,
                                Job::WaterworkerWalkHome,
                                Job::RiceFarmerWalkToPlant,
                                Job::RiceFarmerWalkHome,
                                Job::RiceFarmerWalkHomeLoaded,
                                Job::RiceFarmerWalkToHarvest,
                                Job::WinegrowerWalkToPlant,
                                Job::WinegrowerWalkHome,
                                Job::WinegrowerWalkHomeLoaded,
                                Job::WinegrowerWalkToHarvest,
                                Job::DonkeyWalkToPile,
                                Job::DonkeyWalkToMarket,
                                Job::ShipyardWalkToSlipway,
                                Job::ShipyardWalkHome,
                                Job::ShipWalkToHarbour,
                                Job::ShipSailWithCargo,
                                Job::ShipSailHome,
                                Job::DonkeyWalkHome,
                                Job::BeekeeperWalkToHiveSpot,
                                Job::BeekeeperWalkHome,
                                Job::BeekeeperWalkToHive,
                                Job::BeekeeperWalkHomeLoaded};
static_assert(sizeof(kWalkingJobs) / sizeof(kWalkingJobs[0]) == 47);

// The callers pass the job as a dword and the original compares all 32 bits.
bool InWalkingList(std::uint32_t job) {
    for (const Job walking : kWalkingJobs) {
        if (job == Value(walking)) {
            return true;
        }
    }
    return false;
}

// The upper 24 bits of EAX are the argument's (the original sets only AL).
std::uint32_t WithFlagInAl(std::uint32_t job, bool flag) {
    return (job & 0xffffff00u) | (flag ? 1u : 0u);
}

constexpr std::uint8_t kTrafficSaturated = 255;
constexpr std::uint8_t kTrafficMakesPath = 12;

}  // namespace

using TileFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);
using JobFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t job);

// Purpose: Counts a settler's step onto a tile; tiles walked on often enough become paths.
std::uint32_t __fastcall CGameData_AddTileTraffic(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    Tile& tile = TileAtOffset(world, TileOffset(x, y));
    const std::uint32_t address = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&tile.traffic));
    if (tile.traffic == kTrafficSaturated) {
        return address;
    }
    ++tile.traffic;
    if (tile.traffic < kTrafficMakesPath) {
        return address;
    }
    return CGameData_WearPath(world, nullptr, x, y);
}

// Purpose: Whether a job is one of the walking jobs (the settler is on its way somewhere).
std::uint32_t __fastcall CGameData_IsWalkingJob(CGameData*, void*, std::uint32_t job) {
    return WithFlagInAl(job, InWalkingList(job));
}

// Purpose: Whether a settler with this job stays where it is (it is neither walking nor idle), so
// the pathfinder treats its tile as blocked.
std::uint32_t __fastcall CGameData_IsStationaryJob(CGameData*, void*, std::uint32_t job) {
    const bool moving = InWalkingList(job) || job == Value(Job::Nothing) || job == Value(Job::UnitIdle);
    return WithFlagInAl(job, !moving);
}

S3RE_HOOK(0x00503940, CGameData_AddTileTraffic, TileFn, Original_CGameData_AddTileTraffic,
          CGameData_AddTileTraffic);
S3RE_HOOK(0x004fa6d0, CGameData_IsWalkingJob, JobFn, Original_CGameData_IsWalkingJob, CGameData_IsWalkingJob);
S3RE_HOOK(0x004fa880, CGameData_IsStationaryJob, JobFn, Original_CGameData_IsStationaryJob,
          CGameData_IsStationaryJob);

// ---------------------------------------------------------------------------
// WalkSettler
// ---------------------------------------------------------------------------

namespace {

using layout::Settler;

// Callees, called through their S3.EXE addresses (Detours sends the calls
// to our replacements in the game; tests and emulator specs replace some).
constexpr std::uintptr_t kFindPath = 0x004a1ee0;
constexpr std::uintptr_t kIsSettlerJob01Or37 = 0x004fa6b0;
constexpr std::uintptr_t kJobAfter01Or37 = 0x004faa40;
constexpr std::uintptr_t kRandom = 0x004feb70;
constexpr std::uintptr_t kAllocTable36700Record = 0x004fefd0;
constexpr std::uintptr_t kTileDistance = 0x004ff320;
constexpr std::uintptr_t kSetSettlerTarget = 0x00503610;
constexpr std::uintptr_t kNeighbourX = 0x00503700;
constexpr std::uintptr_t kNeighbourY = 0x00503730;
constexpr std::uintptr_t kDirectionFromDelta = 0x005037f0;
constexpr std::uintptr_t kAddTileTraffic = 0x00503940;
constexpr std::uintptr_t kResetSettlerToIdle = 0x0050cf50;
constexpr std::uintptr_t kMoveSettlerToBuildingTileA = 0x0050de90;
constexpr std::uintptr_t kRemoveSettlerFromLists = 0x0050dff0;
constexpr std::uintptr_t kSetSettlerAnimation = 0x0050e2c0;
constexpr std::uintptr_t kClearSettlerJob = 0x00510440;
constexpr std::uintptr_t kIsOnMap = 0x005194e0;
constexpr std::uintptr_t kTileScreenX = 0x00519610;
constexpr std::uintptr_t kTileScreenY = 0x00519640;
constexpr std::uintptr_t kSpawnDeathEffect = 0x00544e70;  // MapEffects::Spawn_544e70
constexpr std::uintptr_t kSpawnProjectile = 0x00545b90;   // MapEffects::SpawnProjectile

// thiscall on the world (or another object) with dword arguments.
template <std::uintptr_t Address, typename Self, typename... Args>
std::uint32_t Call(Self* self, Args... args) {
    using Fn = std::uint32_t(__fastcall*)(Self*, void*, decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(Address)(self, nullptr, static_cast<std::uint32_t>(args)...);
}

// stdcall helpers (the original passes the world in ECX, unused).
template <std::uintptr_t Address, typename... Args>
std::uint32_t CallStd(Args... args) {
    using Fn = std::uint32_t(__stdcall*)(decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(Address)(static_cast<std::uint32_t>(args)...);
}

std::uint32_t NeighbourXOf(std::uint32_t x, std::uint32_t direction) {
    return CallStd<kNeighbourX>(x, direction);
}
std::uint32_t NeighbourYOf(std::uint32_t y, std::uint32_t direction) {
    return CallStd<kNeighbourY>(y, direction);
}

// Settler::path_cache: up to three waypoints (x, y); [4] can instead mark
// how the walk ends.
constexpr std::uint16_t kNoWaypoint = 0xffff;
constexpr std::uint16_t kPatrolMark = 0xfffe;   // path_cache[4]: swap the two waypoints
constexpr std::uint16_t kNearMark = 0xfffd;     // path_cache[4]: (pc[2], pc[3]) within pc[5] steps is enough

constexpr std::uint8_t kShipWalksLikeSettler = 0xff;  // Settler::ship_walk of a trade ship on a route
constexpr std::uint8_t kShipWalkDirection = 0x07;     // its low three bits: the last direction
constexpr std::uint8_t kShipWalkCountStep = 0x08;     // the rest: disagreeing path searches
constexpr std::uint8_t kShipWalkGiveUp = 0xf8;        // 31 disagreements

constexpr std::uint32_t kWalkFrames = 12;           // Settler::frame cycles through 12 frames ...
constexpr std::uint32_t kCatapultWalkFrames = 36;   // ... the catapult's through 36
constexpr std::uint16_t kShipWaitTicks = 7;

// Settler::walk_state (+0x1a) of the jobs UnitWalk / UnitWalkCountdown at their target.
constexpr std::uint8_t kWalkStateBoardShip = 1;     // land units: board a ferry
constexpr std::uint8_t kWalkStateShoot = 2;         // soldiers at their post
constexpr std::uint8_t kWalkStateThiefArrived = 3;  // probably: the thief is at the enemy's piles
constexpr std::uint8_t kWalkStateThiefStole = 4;    // probably: going back with the stolen good
constexpr std::uint8_t kWalkStateThiefBack = 5;

// Tile flags used here beyond layout.h's (re/structs.md "Tile" +0x0a).
constexpr std::uint8_t kTileClaimed = layout::kTileFlag02;  // probably "a settler will work here"
constexpr std::uint8_t kTileLevelling = 0x04;               // a building site changes its height
constexpr std::uint8_t kTileNotForPioneers =
    layout::kTileBlocked | kTileLevelling | layout::kTileBuildingFootprint | layout::kTileOwned;  // 0x4d
constexpr std::uint8_t kTileNotForGeologists =
    layout::kTileBlocked | kTileLevelling | layout::kTileBuildingFootprint;  // 0x0d
constexpr std::uint8_t kNoOwner = 0xff;
constexpr std::uint32_t kNoSavedAnimation = 0xffff;  // Settler::saved_animation of a unit sent without work

// Effect records (AllocTable36700Record) of a shot, by what it hits.
constexpr std::uint32_t kShotAtBuilding = 5;
constexpr std::uint32_t kShotAtCatapult = 6;
constexpr std::uint32_t kShotAtShip = 7;

// Animations started on arrival (SetSettlerAnimation ids).
constexpr std::uint32_t kPioneerWorkAnimation = 271;    // 0x10f
constexpr std::uint32_t kGeologistWorkAnimation = 212;  // 0xd4

bool IsShip(SettlerType type) {
    return type == SettlerType::Ferry || type == SettlerType::TradeShip;
}

Tile& TileAt(CGameData* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

// The tile (dx, dy) away from the tile at `offset` (the original's
// +-0x4800 / +-0x18 steps).
Tile& TileNear(CGameData* world, std::uint32_t offset, std::int32_t dx, std::int32_t dy) {
    return TileAtOffset(world, offset + static_cast<std::uint32_t>(dx) * kTileRowBytes +
                                   static_cast<std::uint32_t>(dy) * kTileBytes);
}

// The six neighbours in direction order 0..5.
constexpr std::int32_t kSixDx[kDirectionCount] = {1, 1, 0, -1, -1, 0};
constexpr std::int32_t kSixDy[kDirectionCount] = {0, 1, 1, 0, -1, -1};

// Ship jobs whose walk follows waypoints (ShipWalkToHarbour..ShipSailHome).
bool IsShipLegJob(Job job) {
    return job == Job::ShipWalkToHarbour || job == Job::ShipSailWithCargo || job == Job::ShipSailHome;
}

// The step time of a settler entering `tile`: terrain speed plus type
// speed, one tick more for a thief carrying a stolen good; counter = time - 1.
void SetStepTime(Settler& settler, const Tile& tile) {
    const auto terrainTime = static_cast<std::uint8_t>(g_terrainSpeed[tile.terrain]);
    if (TypeOf(settler) == SettlerType::Thief && settler.slot != 0) {
        settler.step_time = static_cast<std::uint8_t>(
            terrainTime + static_cast<std::uint8_t>(g_settlerTypeSpeed[Value(SettlerType::Thief)]) + 1);
    } else {
        settler.step_time = static_cast<std::uint8_t>(terrainTime + static_cast<std::uint8_t>(g_settlerTypeSpeed[settler.art]));
    }
    settler.counter = static_cast<std::uint16_t>(settler.step_time - 1u);
}

void StartFrames(Settler& settler) {
    if (settler.frame == 0) {
        settler.frame = 1;
    }
}

void StopFrames(Settler& settler) {
    settler.frame = 0;
    settler.step_time = 0;
}

void SetJobNow(Settler& settler, Job job) {
    SetJob(settler, job);
    settler.counter = 0;
}

// Takes the next waypoint from the path cache into the target.
void PopWaypoint(Settler& settler) {
    settler.target_x = settler.path_cache[0];
    settler.target_y = settler.path_cache[1];
    settler.path_cache[0] = settler.path_cache[2];
    settler.path_cache[1] = settler.path_cache[3];
    settler.path_cache[2] = settler.path_cache[4];
    settler.path_cache[3] = settler.path_cache[5];
    settler.path_cache[4] = kNoWaypoint;
    settler.path_cache[5] = kNoWaypoint;
}

struct Walk {
    CGameData* world;
    std::uint32_t n;
    Settler& s;
    std::uint32_t again;  // EAX
};

// Swaps the walker with the settler `other` on tile (nx, ny) (step
// direction d): the other gets the walker's tile, the opposite direction
// and the step time of the walker's new tile.
Settler& SwapPlaces(Walk& w, std::uint32_t d, std::uint32_t nx, std::uint32_t ny, Tile& next,
                    std::uint32_t other) {
    Settler& s = w.s;
    TileAt(w.world, s.x, s.y).settler = static_cast<std::uint16_t>(other);
    next.settler = static_cast<std::uint16_t>(w.n);
    Settler& o = w.world->settlers[other];
    o.direction = (d + 3) % kDirectionCount;
    o.x = s.x;
    o.y = s.y;
    s.x = static_cast<std::uint16_t>(nx);
    s.y = static_cast<std::uint16_t>(ny);
    SetStepTime(s, next);
    StartFrames(s);
    SetStepTime(o, TileAt(w.world, s.x, s.y));
    StartFrames(o);
    o.path_mode = static_cast<std::uint8_t>(o.path_mode & ~layout::kPathWaiting);
    return o;
}

// The step onto the neighbour in direction d.
Tile& StepTo(Walk& w, std::uint32_t d, std::uint32_t* nxOut, std::uint32_t* nyOut) {
    Settler& s = w.s;
    const std::uint32_t nx = NeighbourXOf(s.x, d);
    const std::uint32_t ny = NeighbourYOf(s.y, d);
    s.direction = d;
    TileAt(w.world, s.x, s.y).settler = 0;
    Tile& next = TileAt(w.world, nx, ny);
    next.settler = static_cast<std::uint16_t>(w.n);
    *nxOut = nx;
    *nyOut = ny;
    return next;
}

// The full step: onto the neighbour, with the new step time.
void Step(Walk& w, std::uint32_t d) {
    std::uint32_t nx = 0, ny = 0;
    Tile& next = StepTo(w, d, &nx, &ny);
    w.s.x = static_cast<std::uint16_t>(nx);
    w.s.y = static_cast<std::uint16_t>(ny);
    SetStepTime(w.s, next);
    StartFrames(w.s);
}

void EnsureOnTile(Walk& w) {
    Tile& tile = TileAt(w.world, w.s.x, w.s.y);
    if (tile.settler != w.n) {
        tile.settler = static_cast<std::uint16_t>(w.n);
    }
}

// The catapult: a unit on seven tiles; its six part settlers follow it.
std::uint32_t WalkSevenTileUnit(Walk& w) {
    CGameData* world = w.world;
    Settler& s = w.s;
    const std::uint32_t at = TileOffset(s.x, s.y);
    auto isFree = [&](std::int32_t dx, std::int32_t dy) {
        const Tile& t = TileNear(world, at, dx, dy);
        return t.settler == 0 && t.object_number == 0;
    };
    // A part tile is cleared when the three tiles outside it are free.
    struct Outer {
        std::int32_t dx[3], dy[3];
    };
    static constexpr Outer kOuter[kDirectionCount] = {
        {{2, 2, 1}, {0, 1, -1}},    {{2, 2, 1}, {2, 1, 2}},     {{0, -1, 1}, {2, 1, 2}},
        {{-2, -2, -1}, {0, -1, 1}}, {{-2, -2, -1}, {-2, -1, -2}}, {{0, -1, 1}, {-2, -2, -1}}};
    for (std::uint32_t d = 0; d < kDirectionCount; ++d) {
        if (isFree(kOuter[d].dx[0], kOuter[d].dy[0]) && isFree(kOuter[d].dx[1], kOuter[d].dy[1]) &&
            isFree(kOuter[d].dx[2], kOuter[d].dy[2])) {
            TileNear(world, at, kSixDx[d], kSixDy[d]).settler = 0;
        }
    }
    EnsureOnTile(w);
    const std::uint32_t path = Call<kFindPath>(world, w.n);
    if (IsPlainStep(path)) {
        for (std::uint32_t d = 0; d < kDirectionCount; ++d) {
            TileNear(world, at, kSixDx[d], kSixDy[d]).settler = 0;
        }
        Step(w, PathDirection(path));
    } else {
        if (path != kPathNoWay) {
            s.direction = PathDirection(path);
        }
        s.counter = 0;
        StopFrames(s);
    }
    const std::uint32_t here = TileOffset(s.x, s.y);
    std::uint32_t part = w.n;
    for (std::uint32_t d = 0; d < kDirectionCount; ++d) {
        part = world->settlers[part].saved_counter;  // the next part settler
        TileNear(world, here, kSixDx[d], kSixDy[d]).settler = static_cast<std::uint16_t>(part);
    }
    return w.again;
}

// Whether a ferry's way in direction d is crowded: a wedge of 7 rings
// ahead (one side: any settler; the other: settlers on water).
bool CrowdedAhead(CGameData* world, const Settler& s, std::uint32_t d) {
    bool crowded = false;
    for (std::uint32_t ring = 1; ring <= 7; ++ring) {
        std::uint32_t sx = s.x, sy = s.y;
        for (std::uint32_t i = ring; i != 0; --i) {
            sx = NeighbourXOf(sx, d);
            sy = NeighbourYOf(sy, d);
        }
        std::uint32_t px = sx, py = sy;
        const std::uint32_t side1 = (d + 4 - 2) % kDirectionCount;
        for (std::uint32_t i = ring + 1; i != 0; --i) {
            const std::uint32_t size = world->map_size;
            if (px < size && py < size && TileAt(world, px, py).settler != 0) {
                crowded = true;
            }
            px = NeighbourXOf(px, side1);
            py = NeighbourYOf(py, side1);
        }
        const std::uint32_t side2 = (d + 4) % kDirectionCount;
        px = sx;
        py = sy;
        for (std::uint32_t i = ring + 1; i != 0; --i) {
            const std::uint32_t size = world->map_size;
            if (px < size && py < size) {
                const Tile& t = TileAt(world, px, py);
                if ((t.terrain & kTerrainClassMask) == kTerrainClassWater && t.settler != 0) {
                    crowded = true;
                }
            }
            px = NeighbourXOf(px, side2);
            py = NeighbourYOf(py, side2);
        }
    }
    return crowded;
}

// The ship's path search with crowded directions closed (their tiles'
// areas cleared) is compared with a search without; returns the direction
// to take, kPathNoWay or a wait. Updates the disagreement count in
// ship_walk.
std::uint32_t ShipDecision(Walk& w, std::uint32_t first, std::uint32_t path) {
    Settler& s = w.s;
    const bool legJob = TypeOf(s) != SettlerType::Ferry && IsShipLegJob(JobOf(s));
    auto back = [&]() { return static_cast<std::uint32_t>(((s.ship_walk & kShipWalkDirection) + 3) % kDirectionCount); };
    auto disagree = [&]() { s.ship_walk = static_cast<std::uint8_t>(s.ship_walk + kShipWalkCountStep); };
    auto agree = [&]() {
        if (legJob) {
            disagree();  // on a waypoint leg every step counts
        } else {
            s.ship_walk = static_cast<std::uint8_t>(s.ship_walk & kShipWalkDirection);
        }
    };
    if (path == first) {
        if (path >= kDirectionCount) {
            disagree();
        } else if (!legJob) {
            s.ship_walk = static_cast<std::uint8_t>(s.ship_walk & kShipWalkDirection);
        }
        return path;
    }
    if (path >= kDirectionCount) {
        disagree();
        return path;
    }
    // A first result of 6, 7 or above 13 (no wait with a direction): no way.
    if (first >= kDirectionCount && (first < kPathWait || first > (kPathWait | 5))) {
        disagree();
        return kPathNoWay;
    }
    // Accepted: the first search's direction or one of its neighbours,
    // unless that is straight back.
    const std::uint32_t c = PathDirection(first);
    const std::uint32_t left = (c - 1) % kDirectionCount;
    const std::uint32_t right = (c + 1) % kDirectionCount;
    if ((path == c || path == left || path == right) && path != back()) {
        agree();
        return path;
    }
    const std::uint32_t wait = (s.ship_walk & kShipWalkDirection) | kPathWait;
    disagree();
    return wait;
}

// Ferries, and trade ships while ship_walk != 255.
std::uint32_t WalkShip(Walk& w) {
    CGameData* world = w.world;
    Settler& s = w.s;
    const std::uint32_t at = TileOffset(s.x, s.y);
    std::uint16_t savedAreas[kDirectionCount];
    for (std::uint32_t d = 0; d < kDirectionCount; ++d) {
        savedAreas[d] = TileNear(world, at, kSixDx[d], kSixDy[d]).area;
    }
    const std::uint32_t first = Call<kFindPath>(world, w.n);
    for (std::uint32_t d = 0; d < kDirectionCount; ++d) {
        if (CrowdedAhead(world, s, d)) {
            const std::uint32_t nx = NeighbourXOf(s.x, d);
            const std::uint32_t ny = NeighbourYOf(s.y, d);
            TileAt(world, nx, ny).area = 0;
        }
    }
    EnsureOnTile(w);
    std::uint32_t path = Call<kFindPath>(world, w.n);
    {
        const std::uint8_t count = s.ship_walk;
        const std::uint32_t direction = count & kShipWalkDirection;
        if ((direction == 6 || direction == 7) && count != kShipWalksLikeSettler) {
            GameDebugLog(texts::kIllegalWalkCount, static_cast<std::uint32_t>(count), w.n);
            s.ship_walk = static_cast<std::uint8_t>(s.ship_walk & ~kShipWalkDirection);
        }
    }
    path = ShipDecision(w, first, path);
    bool wait = false;
    const std::uint8_t count = s.ship_walk;
    if (count >= kShipWalkGiveUp) {
        if (TypeOf(s) == SettlerType::TradeShip && IsShipLegJob(JobOf(s))) {
            s.ship_walk = kShipWalksLikeSettler;
        } else {
            s.ship_walk = static_cast<std::uint8_t>(count & kShipWalkDirection);
            w.again = 1;
            SetJobNow(s, Job::UnitIdle);
            s.frame = 0;
        }
        wait = true;
    } else if (path == kPathNoWay) {
        wait = true;
    } else if (IsPlainStep(path)) {
        const std::uint32_t d = PathDirection(path);
        if (TypeOf(s) != SettlerType::Ferry && IsShipLegJob(JobOf(s))) {
            s.ship_walk = static_cast<std::uint8_t>((count & ~kShipWalkDirection) | d);
        } else {
            s.ship_walk = static_cast<std::uint8_t>(d);
        }
        Step(w, d);
    } else if ((path & kPathWait) != 0) {
        s.direction = PathDirection(path);
        wait = true;
    } else if ((path & kPathSwap) != 0) {
        const std::uint32_t d = PathDirection(path);
        const std::uint32_t nx = NeighbourXOf(s.x, d);
        const std::uint32_t ny = NeighbourYOf(s.y, d);
        s.direction = d;
        Tile& next = TileAt(world, nx, ny);
        SwapPlaces(w, d, nx, ny, next, next.settler);
    }
    if (wait) {
        s.counter = kShipWaitTicks;
        StopFrames(s);
    }
    for (std::uint32_t d = 0; d < kDirectionCount; ++d) {
        TileNear(world, at, kSixDx[d], kSixDy[d]).area = savedAreas[d];
    }
    return w.again;
}

// Every other walker.
std::uint32_t WalkOne(Walk& w) {
    CGameData* world = w.world;
    Settler& s = w.s;
    if (TileAt(world, s.x, s.y).settler != w.n) {
        GameDebugLog(texts::kNotThere, w.n, static_cast<std::uint32_t>(s.owner), static_cast<std::uint32_t>(s.art),
                     static_cast<std::uint32_t>(s.job));
        TileAt(world, s.x, s.y).settler = static_cast<std::uint16_t>(w.n);
    }
    const std::uint32_t path = Call<kFindPath>(world, w.n);
    if (path == kPathNoWay) {
        if (!IsShip(TypeOf(s))) {
            Call<kResetSettlerToIdle>(world, w.n);
        }
        return w.again;
    }
    if (IsPlainStep(path)) {
        const std::uint32_t d = PathDirection(path);
        std::uint32_t nx = 0, ny = 0;
        Tile& next = StepTo(w, d, &nx, &ny);
        // Carriers with goods wear paths three times as fast as other civilians.
        if (JobOf(s) == Job::CarrierWalkToBuilding || JobOf(s) == Job::CarrierWalkToTargetPile) {
            Call<kAddTileTraffic>(world, nx, ny);
            Call<kAddTileTraffic>(world, nx, ny);
            Call<kAddTileTraffic>(world, nx, ny);
        } else if (ClassOf(s) == SettlerClass::Civilian) {
            Call<kAddTileTraffic>(world, nx, ny);
        }
        s.x = static_cast<std::uint16_t>(nx);
        s.y = static_cast<std::uint16_t>(ny);
        SetStepTime(s, next);
        StartFrames(s);
        return w.again;
    }
    if ((path & kPathWait) != 0) {
        s.direction = PathDirection(path);
        s.counter = 0;
        StopFrames(s);
        return w.again;
    }
    if ((path & kPathSwap) == 0) {
        return w.again;
    }
    // Swap places with the settler in the way, unless that is a civilian of
    // another player at work there.
    const std::uint32_t d = PathDirection(path);
    const std::uint32_t nx = NeighbourXOf(s.x, d);
    const std::uint32_t ny = NeighbourYOf(s.y, d);
    s.direction = d;
    Tile& next = TileAt(world, nx, ny);
    const std::uint32_t other = next.settler;
    const Settler& blocker = world->settlers[other];
    if (ClassOf(blocker) == SettlerClass::Civilian && JobOf(blocker) != Job::WalkToOwnArea &&
        JobOf(blocker) != Job::SeekOwnArea) {
        const Tile& here = TileAt(world, s.x, s.y);
        if (here.owner != blocker.owner) {
            GameDebugLog(texts::kIllegalSwap, static_cast<std::uint32_t>(here.owner),
                         static_cast<std::uint32_t>(blocker.owner), static_cast<std::uint32_t>(here.area),
                         static_cast<std::uint32_t>(next.area));
            s.counter = 0;
            Call<kResetSettlerToIdle>(world, other);
            return w.again;
        }
    }
    Settler& o = SwapPlaces(w, d, nx, ny, next, other);
    // An idle settler pushed aside starts stepping aside (job 1 -> 2, 55 -> 53).
    if ((Call<kIsSettlerJob01Or37>(world, static_cast<std::uint32_t>(o.job)) & 0xff) != 0) {
        o.target_x = o.x;
        o.target_y = o.y;
        o.job = static_cast<std::uint16_t>(Call<kJobAfter01Or37>(world, static_cast<std::uint32_t>(o.job)));
    }
    return w.again;
}

std::uint32_t WalkStep(Walk& w) {
    CGameData* world = w.world;
    Settler& s = w.s;
    const auto size = static_cast<std::int32_t>(world->map_size);
    if (static_cast<std::int32_t>(s.target_x) >= size || static_cast<std::int32_t>(s.target_y) >= size) {
        GameDebugLog(texts::kSpotOutOfMap);
        GameDebugLog(texts::kFiveNumbers, static_cast<std::uint32_t>(s.target_x), static_cast<std::uint32_t>(s.target_y),
                     static_cast<std::uint32_t>(s.art), static_cast<std::uint32_t>(s.job), w.n);
    }
    if (TypeOf(s) == SettlerType::Catapult) {
        return WalkSevenTileUnit(w);
    }
    if (TypeOf(s) == SettlerType::Ferry ||
        (TypeOf(s) == SettlerType::TradeShip && s.ship_walk != kShipWalksLikeSettler)) {
        return WalkShip(w);
    }
    return WalkOne(w);
}

bool IsMilitaryBuilding(const layout::Building& building) {
    const BuildingType type = TypeOf(building);
    return type == BuildingType::SmallTower || type == BuildingType::LargeTower || type == BuildingType::Castle;
}

// Whether a guarding soldier (job GuardBuilding) stands within 7 tiles of (x, y).
bool Guarded(CGameData* world, std::uint32_t x, std::uint32_t y) {
    for (std::uint32_t j = 0; world->offsets[j].distance < 7; ++j) {
        const std::uint32_t qx = static_cast<std::uint32_t>(world->offsets[j].dx) + x;
        const std::uint32_t qy = static_cast<std::uint32_t>(world->offsets[j].dy) + y;
        if (Call<kIsOnMap>(world, qx, qy) == 0) {
            continue;
        }
        const std::uint32_t guard = TileAt(world, qx, qy).settler;
        if (guard != 0 && ClassOf(world->settlers[guard]) != SettlerClass::Civilian &&
            JobOf(world->settlers[guard]) == Job::GuardBuilding) {
            return true;
        }
    }
    return false;
}

// A random value below `limit` from a 16-bit roll.
std::uint32_t Scale(std::uint32_t roll, std::uint32_t limit) {
    return (roll * limit) >> 16;
}

// A soldier (state kWalkStateShoot) at its post looks for something to shoot
// at around (path_cache[2], [3]). Returns whether it shot.
bool SoldierShoot(Walk& w) {
    CGameData* world = w.world;
    Settler& s = w.s;
    const std::uint32_t cx = s.path_cache[2], cy = s.path_cache[3];
    GameDebugLog(texts::kSearchTarget, cx, cy);
    std::uint32_t kind = 0;    // kShotAt*, 0 = nothing found
    std::uint32_t target = 0;  // its record number
    std::uint32_t tx = 0, ty = 0;
    // The building tile looked at last (the original keeps it in a local
    // that is only set on building tiles; see the known difference above).
    std::uint32_t lastBuilding = 0;
    const layout::Player& me = world->players[s.owner];
    const bool amazon = RaceOf(me) == Race::Amazon;
    for (std::uint32_t i = 0; world->offsets[i].distance < 5 && kind == 0; ++i) {
        const std::uint32_t px = static_cast<std::uint32_t>(world->offsets[i].dx) + cx;
        const std::uint32_t py = static_cast<std::uint32_t>(world->offsets[i].dy) + cy;
        if (Call<kIsOnMap>(world, px, py) == 0) {
            continue;
        }
        const Tile& t = TileAt(world, px, py);
        if ((t.flags & layout::kTileHasBuilding) != 0) {
            const std::uint32_t number = t.object_number;
            lastBuilding = number;
            const layout::Building& b = world->buildings[number];
            if (world->players[b.owner].team == me.team) {
                continue;
            }
            // Amazons shoot at every building, the others at military ones.
            if (!IsMilitaryBuilding(b) && !amazon) {
                continue;
            }
            if (Guarded(world, px, py)) {
                continue;
            }
            tx = b.x;
            ty = b.y;
            GameDebugLog(texts::kAttackBuilding);
            kind = kShotAtBuilding;
            target = number;
        } else if (t.settler != 0) {
            const std::uint32_t number = t.settler;
            const Settler& o = world->settlers[number];
            if (world->players[o.owner].team == me.team) {
                continue;
            }
            if (TypeOf(o) == SettlerType::Catapult) {
                tx = o.x;
                ty = o.y;
                GameDebugLog(texts::kAttackCatapult);
                kind = kShotAtCatapult;
                target = number;
            } else if (IsShip(TypeOf(o)) && JobOf(o) != Job::ShipBeingBuilt) {
                kind = kShotAtShip;
                target = number;
                tx = o.x;
                ty = o.y;
            }
        }
    }
    if (kind == 0) {
        return false;
    }
    // Shoot only at targets out of reach on foot.
    const std::uint32_t minimumRange = amazon ? 3 : 10;
    if (Call<kTileDistance>(world, s.x, s.y, tx, ty) < minimumRange) {
        return false;
    }
    if (!amazon) {
        --s.arrows;
    }
    // The delay before the shot lands, a random share of shots per race
    // (rolls are 0..65535; the order of the Random calls matters).
    std::uint32_t delay = 0;
    auto roll = [&]() { return static_cast<std::uint32_t>(Call<kRandom>(world) & 0xffff); };
    switch (RaceOf(me)) {
    case Race::Roman: delay = roll() > 32000 ? Scale(roll(), 60) + 60 : 0; break;
    case Race::Egyptian: delay = roll() > 20000 ? Scale(roll(), 80) + 80 : 0; break;
    case Race::Asian: delay = roll() > 15000 ? Scale(roll(), 100) + 100 : 0; break;
    case Race::Amazon: delay = roll() > 20000 ? world->buildings[lastBuilding].hit_points + 1u : 0; break;
    default: break;
    }
    if (delay == 0) {
        // Aim a little off: one of the offsets 36..98 (rings 3..5).
        const std::uint32_t k = Scale(roll(), 63) + 36;
        const layout::MapOffset& off = world->offsets[k];
        if (Call<kIsOnMap>(world, tx + static_cast<std::uint32_t>(off.dx), ty + static_cast<std::uint32_t>(off.dy)) !=
            0) {
            tx += static_cast<std::uint32_t>(world->offsets[k].dx);
            ty += static_cast<std::uint32_t>(world->offsets[k].dy);
        }
    }
    s.counter = static_cast<std::uint16_t>(static_cast<std::uint16_t>(g_raceShotTime[me.race]) - 1u);
    // Bit 15 on top of the direction: unknown, probably "shooting".
    s.direction = CallStd<kDirectionFromDelta>(tx - s.x, ty - s.y) + 0x8000;
    SetJob(s, Job::Shoot);
    if (amazon) {
        s.arrows = 6;
    }
    const std::uint32_t x = s.x, y = s.y;
    const std::uint32_t targetScreenY = Call<kTileScreenY>(world, tx, ty);
    const std::uint32_t targetScreenX = Call<kTileScreenX>(world, tx, ty);
    const std::uint32_t screenY = Call<kTileScreenY>(world, x, y);
    const std::uint32_t screenX = Call<kTileScreenX>(world, s.x, s.y);
    const std::uint32_t projectile =
        Call<kSpawnProjectile>(&world->map_effects, static_cast<std::uint32_t>(g_gameTick),
                               static_cast<std::uint32_t>(me.race), screenX, screenY, x, y, targetScreenX,
                               targetScreenY, tx, ty);
    Call<kAllocTable36700Record>(world, kind, projectile, target, delay, 0u, 0u, 0u, 0u, 0u, 0u);
    return true;
}

// A land unit (state kWalkStateBoardShip) at the shore looks for a friendly
// ferry within radius 15 with room, and a shore tile (same walk region)
// next to it: there it boards, otherwise it walks there. Returns whether
// it found one.
bool BoardShip(Walk& w) {
    CGameData* world = w.world;
    Settler& s = w.s;
    bool found = false;
    std::uint32_t ship = 0, bx = 0, by = 0, side = 0;
    for (std::uint32_t i = 0; world->offsets[i].distance < 15 && !found; ++i) {
        const std::uint32_t px = s.x + static_cast<std::uint32_t>(world->offsets[i].dx);
        const std::uint32_t py = s.y + static_cast<std::uint32_t>(world->offsets[i].dy);
        if (px >= world->map_size || py >= world->map_size) {
            continue;
        }
        ship = TileAt(world, px, py).settler;
        if (ship == 0) {
            continue;
        }
        const Settler& boat = world->settlers[ship];
        if (TypeOf(boat) != SettlerType::Ferry || JobOf(boat) == Job::ShipBeingBuilt ||
            world->players[boat.owner].team != world->players[s.owner].team) {
            continue;
        }
        const std::uint8_t water = TileAt(world, boat.x, boat.y).terrain;
        if (water != kTerrainWater5 && water != kTerrainWater6) {
            continue;
        }
        if (static_cast<std::int32_t>(boat.passengers) >= g_raceShipCapacity[world->players[boat.owner].race]) {
            continue;
        }
        // Rays in the six directions, up to eight tiles from the ferry.
        std::uint32_t xs[kDirectionCount], ys[kDirectionCount];
        for (std::uint32_t d = 0; d < kDirectionCount; ++d) {
            xs[d] = boat.x;
            ys[d] = boat.y;
        }
        for (std::uint32_t ring = 0; ring < 8 && !found; ++ring) {
            for (std::uint32_t d = 0; d < kDirectionCount; ++d) {
                xs[d] = NeighbourXOf(xs[d], d);
                ys[d] = NeighbourYOf(ys[d], d);
                if (xs[d] < world->map_size && ys[d] < world->map_size) {
                    const Tile& shore = TileAt(world, xs[d], ys[d]);
                    if (shore.terrain == kTerrainShore && shore.walk_region == TileAt(world, s.x, s.y).walk_region) {
                        found = true;
                        bx = xs[d];
                        by = ys[d];
                        side = d;
                    }
                }
            }
        }
    }
    if (!found) {
        return false;
    }
    Settler& boat = world->settlers[ship];
    boat.direction = side;
    if (s.x != bx || s.y != by) {
        s.target_x = static_cast<std::uint16_t>(bx);
        s.target_y = static_cast<std::uint16_t>(by);
        SetJobNow(s, Job::UnitWalk);
        return true;
    }
    // Board: leave the player's idle list, join the ferry's passenger list.
    if (s.prev != 0) {
        world->settlers[s.prev].next = s.next;
    } else {
        world->players[s.owner].settler_lists[s.art] = s.next;
    }
    if (s.next != 0) {
        world->settlers[s.next].prev = s.prev;
    }
    Call<kRemoveSettlerFromLists>(world, w.n);
    ++boat.passengers;
    s.next = boat.next;
    boat.next = static_cast<std::uint16_t>(w.n);
    TileAt(world, s.x, s.y).settler = 0;
    s.walk_state = 0;
    SetJobNow(s, Job::OnBoard);
    return true;
}

// The land units that can board a ferry.
bool CanBoardShip(SettlerType type) {
    using S = SettlerType;
    static constexpr S kLandUnits[] = {S::Swordsman1, S::Bowman1,    S::Pikeman1, S::Swordsman2, S::Bowman2,
                                       S::Pikeman2,   S::Swordsman3, S::Bowman3,  S::Pikeman3,   S::Geologist,
                                       S::Pioneer,    S::Thief,      S::Mage};
    for (const S unit : kLandUnits) {
        if (type == unit) {
            return true;
        }
    }
    return false;
}

// A pioneer that walked to a tile claims it if it is free and not its
// player's; returns whether it started working.
bool PioneerArrives(Walk& w) {
    Settler& s = w.s;
    Tile& t = TileAt(w.world, s.x, s.y);
    const std::uint8_t flags = t.flags;
    if ((flags & kTileNotForPioneers) == 0 && (t.owner == kNoOwner || t.owner != s.owner)) {
        SetJob(s, Job::PioneerWork);
        Call<kSetSettlerAnimation>(w.world, w.n, kPioneerWorkAnimation);
        return true;
    }
    t.flags = static_cast<std::uint8_t>(flags & ~kTileClaimed);
    return false;
}

// A geologist that walked to a tile examines it if it is a free mountain
// tile without objects on it and its six neighbours.
bool GeologistArrives(Walk& w) {
    Settler& s = w.s;
    const std::uint32_t at = TileOffset(s.x, s.y);
    Tile& t = TileAtOffset(w.world, at);
    const std::uint8_t flags = t.flags;
    if ((flags & kTileNotForGeologists) == 0 && (t.terrain & kTerrainClassMask) == kTerrainClassMountain &&
        t.object == 0 && TileNear(w.world, at, -1, 0).object == 0 && TileNear(w.world, at, 1, 0).object == 0 &&
        TileNear(w.world, at, 0, -1).object == 0 && TileNear(w.world, at, 0, 1).object == 0 &&
        TileNear(w.world, at, -1, -1).object == 0 && TileNear(w.world, at, 1, 1).object == 0) {
        SetJob(s, Job::GeologistWork);
        Call<kSetSettlerAnimation>(w.world, w.n, kGeologistWorkAnimation);
        return true;
    }
    t.flags = static_cast<std::uint8_t>(flags & ~kTileClaimed);
    return false;
}

// Jobs UnitWalk and UnitWalkCountdown at their target: the walk states (+0x1a), then
// pioneers and geologists, then the path cache.
std::uint32_t ArriveWaiting(Walk& w) {
    CGameData* world = w.world;
    Settler& s = w.s;
    bool handled = false;
    const std::uint8_t state = s.walk_state;
    const bool thief = TypeOf(s) == SettlerType::Thief;
    if (state == kWalkStateThiefBack && thief) {
        SetJobNow(s, Job::UnitIdle);
        w.again = 1;
        handled = true;
    } else if (state == kWalkStateThiefStole && thief) {
        Call<kSetSettlerTarget>(world, w.n, static_cast<std::uint32_t>(s.path_cache[0]),
                                static_cast<std::uint32_t>(s.path_cache[1]));
        s.counter = 0;
        SetJob(s, Job::UnitWalk);
        s.walk_state = kWalkStateThiefBack;
        handled = true;
    } else if (state == kWalkStateThiefArrived && thief) {
        SetJobNow(s, Job::ThiefChoosePile);
        handled = true;
    } else if (state == kWalkStateShoot && s.arrows != 0 && s.path_cache[4] == kNearMark &&
               Call<kTileDistance>(world, s.x, s.y, s.path_cache[2], s.path_cache[3]) < s.path_cache[5]) {
        handled = SoldierShoot(w);
    } else if (state == kWalkStateBoardShip && CanBoardShip(TypeOf(s))) {
        handled = BoardShip(w);
        if (!handled) {
            s.walk_state = 0;
        }
    }
    if (TypeOf(s) == SettlerType::Pioneer && s.saved_animation != kNoSavedAnimation && PioneerArrives(w)) {
        handled = true;
    }
    if (TypeOf(s) == SettlerType::Geologist && s.saved_animation != kNoSavedAnimation && GeologistArrives(w)) {
        return w.again;
    }
    if (handled) {
        return w.again;
    }
    if (s.path_cache[0] != kNoWaypoint) {
        s.target_x = s.path_cache[0];
        if (s.path_cache[4] == kPatrolMark) {
            // Patrol: the two waypoints swap places.
            s.target_y = s.path_cache[1];
            s.path_cache[0] = s.path_cache[2];
            s.path_cache[1] = s.path_cache[3];
            s.path_cache[2] = s.target_x;
            s.path_cache[3] = s.target_y;
        } else {
            PopWaypoint(s);
        }
        s.counter = 0;
        SetJob(s, Job::UnitWalk);
        s.walk_state = 0;
        return w.again;
    }
    SetJobNow(s, Job::UnitIdle);
    s.frame = 0;
    return 1;
}

// WalkToOwnArea: a unit reaches the area it was sent to; outside its own
// area it loses 2 health per try, and dies below 3.
std::uint32_t ArriveSoldierAtArea(Walk& w) {
    CGameData* world = w.world;
    Settler& s = w.s;
    const std::uint32_t x = s.x, y = s.y;
    const std::uint32_t area = TileAt(world, x, y).area;
    if (area != 0 && area < layout::kTileAreaWater) {
        if (world->areas[area].player == s.owner) {
            Call<kClearSettlerJob>(world, w.n);
            s.counter = 0;
            return w.again;
        }
        if (s.health < 3) {
            // The original passes the race with the upper bytes of
            // area * 0x2e0 (left in EDX); only the low byte is used.
            const std::uint32_t raceWord = ((area * 0x2e0u) & 0xffffff00u) | s.race;
            const std::uint32_t colour = g_playerColourIndex[s.owner];
            const std::uint32_t screenY = Call<kTileScreenY>(world, x, y);
            const std::uint32_t screenX = Call<kTileScreenX>(world, s.x, s.y);
            Call<kSpawnDeathEffect>(&world->map_effects, static_cast<std::uint32_t>(g_gameTick), screenX, screenY,
                                    x, y, colour, raceWord);
            s.owner = kNoOwner;  // the record is free again
            TileAt(world, s.x, s.y).settler = 0;
            s.counter = 0;
            return w.again;
        }
        s.health = static_cast<std::uint8_t>(s.health - 2);
    }
    SetJobNow(s, Job::SeekOwnArea);
    return w.again;
}

// Ships' waypoint jobs: the next waypoint, unless the cache is empty or
// it lies in another region/area (then `stop` becomes the job).
template <typename Same>
std::uint32_t ArriveShipLeg(Walk& w, Job stop, Same same) {
    Settler& s = w.s;
    if (s.path_cache[0] == kNoWaypoint) {
        SetJob(s, stop);
    } else {
        PopWaypoint(s);
        if (!same(TileAt(w.world, s.x, s.y), TileAt(w.world, s.target_x, s.target_y))) {
            SetJob(s, stop);
        }
    }
    s.counter = 0;
    return w.again;
}

std::uint32_t MoveIntoBuilding(Walk& w, Job job, std::uint32_t animation) {
    Call<kMoveSettlerToBuildingTileA>(w.world, w.n);
    SetJob(w.s, job);
    Call<kSetSettlerAnimation>(w.world, w.n, animation);
    return w.again;
}

// The walking job's next step at the target.
std::uint32_t Arrive(Walk& w) {
    Settler& s = w.s;
    auto next = [&](Job job) {
        SetJobNow(s, job);
        return w.again;
    };
    auto sameRegion = [](const Tile& a, const Tile& b) { return a.walk_region == b.walk_region; };
    auto sameArea = [](const Tile& a, const Tile& b) { return a.area == b.area; };
    switch (JobOf(s)) {
    case Job::StepAside:
        SetJobNow(s, Job::Nothing);
        return 1;
    case Job::WalkToWork: return next(Job::ArriveAtWork);
    case Job::DiggerWalkToSpot: return next(Job::DiggerLevel);
    case Job::CarrierWalkToPile: return next(Job::CarrierAtPile);
    case Job::CarrierWalkToBuilding: return next(Job::CarrierAtBuilding);
    case Job::BuilderWalkToWait: return next(Job::BuilderStart);
    case Job::BuilderWalkToSpot: return next(Job::BuilderBuild);
    case Job::WoodcutterWalkToTree: return next(Job::WoodcutterAtTree);
    case Job::StonecutterWalkToStone: return next(Job::StonecutterAtStone);
    case Job::WoodcutterWalkHome: return next(Job::WoodcutterHome);
    case Job::StonecutterWalkHome: return next(Job::StonecutterHome);
    case Job::ForesterWalkToSpot: return next(Job::ForesterAtSpot);
    case Job::ForesterWalkHome: return MoveIntoBuilding(w, Job::ForesterBackIn, 27);
    case Job::UnitWalk:
    case Job::UnitWalkCountdown: return ArriveWaiting(w);
    case Job::CarrierWalkToSourcePile: return next(Job::CarrierAtSourcePile);
    case Job::CarrierWalkToTargetPile: return next(Job::CarrierAtTargetPile);
    case Job::CarrierWalkToTool: return next(Job::CarrierTakeTool);
    case Job::FarmerWalkToSow: return next(Job::FarmerSow);
    case Job::FarmerWalkHome: return MoveIntoBuilding(w, Job::FarmerBackIn, 157);
    case Job::FarmerWalkHomeLoaded: return next(Job::FarmerHome);
    case Job::FarmerWalkToHarvest: return next(Job::FarmerAtField);
    case Job::WalkToOwnArea: return ArriveSoldierAtArea(w);
    case Job::FisherWalkToWater: return next(Job::FisherCast);
    case Job::FisherWalkHome: return next(Job::FisherHome);
    case Job::WaterworkerWalkToWater: return next(Job::WaterworkerDraw);
    case Job::WaterworkerWalkHome: return next(Job::WaterworkerHome);
    case Job::RiceFarmerWalkToPlant: return next(Job::RiceFarmerPlant);
    case Job::RiceFarmerWalkHome: return MoveIntoBuilding(w, Job::RiceFarmerBackIn, 225);
    case Job::RiceFarmerWalkHomeLoaded: return next(Job::RiceFarmerHome);
    case Job::RiceFarmerWalkToHarvest: return next(Job::RiceFarmerAtField);
    case Job::WinegrowerWalkToPlant: return next(Job::WinegrowerPlant);
    case Job::WinegrowerWalkHome: return MoveIntoBuilding(w, Job::WinegrowerBackIn, 234);
    case Job::WinegrowerWalkHomeLoaded: return next(Job::WinegrowerHome);
    case Job::WinegrowerWalkToHarvest: return next(Job::WinegrowerAtField);
    case Job::DonkeyWalkToPile: return next(Job::DonkeyLoad);
    case Job::DonkeyWalkToMarket: return ArriveShipLeg(w, Job::DonkeyUnload, sameRegion);
    case Job::ShipyardWalkToSlipway: return next(Job::ShipyardBuild);
    case Job::ShipyardWalkHome: return MoveIntoBuilding(w, Job::ShipyardBackIn, 276);
    case Job::OnBoard:
        s.counter = 0;
        return w.again;
    case Job::ShipWalkToHarbour: return next(Job::ShipLoad);
    case Job::ShipSailWithCargo: return ArriveShipLeg(w, Job::ShipUnload, sameArea);
    case Job::ShipSailHome: return ArriveShipLeg(w, Job::New, sameArea);
    case Job::DonkeyWalkHome: return ArriveShipLeg(w, Job::New, sameRegion);
    case Job::BeekeeperWalkToHiveSpot: return next(Job::BeekeeperPlaceHive);
    case Job::BeekeeperWalkHome: return MoveIntoBuilding(w, Job::BeekeeperGoIn, 509);
    case Job::BeekeeperWalkToHive: return next(Job::BeekeeperHarvest);
    case Job::BeekeeperWalkHomeLoaded: return next(Job::BeekeeperHome);
    default: return w.again;
    }
}

}  // namespace

// Purpose: One step of a walking settler: it moves a tile towards its target (finding the way,
// waiting for or swapping with settlers in the way), or, when it has arrived, goes on with the
// next part of its job (soldiers look for targets to shoot at, settlers board ships, ships follow
// their waypoints).
std::uint32_t __fastcall CGameData_WalkSettler(CGameData* world, void*, std::uint32_t settler) {
    Settler& s = world->settlers[settler];
    Walk w{world, settler, s, 0};
    const std::uint32_t frames = TypeOf(s) == SettlerType::Catapult ? kCatapultWalkFrames : kWalkFrames;
    s.frame = static_cast<std::uint8_t>((static_cast<std::uint32_t>(s.frame) + s.step_time) % frames);
    if (s.x != s.target_x || s.y != s.target_y) {
        if (s.path_cache[4] != kNearMark ||
            Call<kTileDistance>(world, s.x, s.y, s.path_cache[2], s.path_cache[3]) >= s.path_cache[5]) {
            return WalkStep(w);
        }
    }
    s.frame = 0;
    return Arrive(w);
}

using WalkFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t settler);

S3RE_HOOK(0x00500860, CGameData_WalkSettler, WalkFn, Original_CGameData_WalkSettler, CGameData_WalkSettler);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestMapSize = 64;
constexpr std::size_t kMapWorld = offsetof(CGameData, tiles) + kTestMapSize * kTileRowBytes;

std::uint32_t Relative(std::uint32_t value, CGameData* world) {
    return value - static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));
}

void CompareAddTileTraffic(test::Context& ctx, TileFn candidate, int cases) {
    test::WorldPair worlds(ctx, kMapWorld);
    static constexpr std::uint8_t kTerrains[] = {0x10, 0x12, 0x13, 0x18, 0x1c, 0x16, 0x05};
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t x = 2 + ctx.rng().Below(kTestMapSize - 4);
        const std::uint32_t y = 2 + ctx.rng().Below(kTestMapSize - 4);
        const std::uint32_t pick = ctx.rng().Below(8);
        const std::uint8_t traffic = pick < 3   ? static_cast<std::uint8_t>(11)
                                     : pick < 4 ? static_cast<std::uint8_t>(0xff)
                                     : pick < 5 ? static_cast<std::uint8_t>(0xfe)
                                                : static_cast<std::uint8_t>(ctx.rng().Below(256));
        worlds.Both([&](CGameData* w) { w->tiles[x][y].traffic = traffic; });
        for (std::int32_t dx = -1; dx <= 1; ++dx) {
            for (std::int32_t dy = -1; dy <= 1; ++dy) {
                const std::uint8_t terrain = kTerrains[ctx.rng().Below(7)];
                worlds.Both([&](CGameData* w) { w->tiles[x + dx][y + dy].terrain = terrain; });
            }
        }
        const std::uint32_t expected =
            Relative(Original_CGameData_AddTileTraffic(worlds.original(), nullptr, x, y), worlds.original());
        const std::uint32_t actual = Relative(candidate(worlds.ours(), nullptr, x, y), worlds.ours());
        if (!S3RE_EXPECT_EQ(expected, actual) || (i % 512 == 511 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 511 before it): (%u, %u)", i, x, y);
            return;
        }
    }
    S3RE_EXPECT(worlds.Checkpoint(ctx));
}

void CompareJobFn(test::Context& ctx, JobFn original, JobFn candidate) {
    for (std::uint32_t i = 0; i < 0x20000; ++i) {
        const std::uint32_t job = i < 0x400 ? i : (i < 0x800 ? (ctx.rng().Next32() & 0xffff0000u) | (i & 0x1ff)
                                                             : ctx.rng().Next32());
        const std::uint32_t expected = original(nullptr, nullptr, job);
        const std::uint32_t actual = candidate(nullptr, nullptr, job);
        if (!S3RE_EXPECT_EQ(expected, actual)) {
            ctx.Fail(__FILE__, __LINE__, "  job 0x%x", job);
            return;
        }
    }
}

// Wrong: the traffic limit is checked before the increment.
std::uint32_t __fastcall WrongAddTileTraffic(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    Tile& tile = world->tiles[x][y];
    if (tile.traffic == 11) {
        ++tile.traffic;
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&tile.traffic));
    }
    return CGameData_AddTileTraffic(world, nullptr, x, y);
}

// Wrong: forgets the idle job UnitIdle.
std::uint32_t __fastcall WrongIsStationaryJob(CGameData* world, void*, std::uint32_t job) {
    return job == Value(Job::UnitIdle) ? 1 : CGameData_IsStationaryJob(world, nullptr, job);
}

}  // namespace

S3RE_TEST(CGameData_AddTileTraffic_MatchesOriginal) {
    CompareAddTileTraffic(ctx, &CGameData_AddTileTraffic, 20000);
}
S3RE_TEST(CGameData_AddTileTraffic_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareAddTileTraffic(probe, &WrongAddTileTraffic, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_IsWalkingJob_MatchesOriginal) {
    CompareJobFn(ctx, Original_CGameData_IsWalkingJob, &CGameData_IsWalkingJob);
}
S3RE_TEST(CGameData_IsStationaryJob_MatchesOriginal) {
    CompareJobFn(ctx, Original_CGameData_IsStationaryJob, &CGameData_IsStationaryJob);
}
S3RE_TEST(CGameData_IsStationaryJob_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareJobFn(probe, Original_CGameData_IsStationaryJob, &WrongIsStationaryJob);
    S3RE_EXPECT(probe.failures() > 0);
}

// ---------------------------------------------------------------------------
// WalkSettler tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kWalkSettlers = 400;   // settlers 1..400 take part
constexpr std::uint32_t kWalkBuildings = 200;  // buildings 0..199
constexpr std::uint32_t kWalkMapSize = 384;
constexpr std::uint32_t kWalkRings = 16;       // offset table rings 0..15, then an end mark

// Walking jobs, a few that follow them, and some that are not walking.
constexpr Job kWalkJobs[] = {Job::StepAside,
                             Job::WalkToWork,
                             Job::DiggerWalkToSpot,
                             Job::CarrierWalkToPile,
                             Job::CarrierWalkToBuilding,
                             Job::BuilderWalkToWait,
                             Job::BuilderWalkToSpot,
                             Job::WoodcutterWalkToTree,
                             Job::StonecutterWalkToStone,
                             Job::WoodcutterWalkHome,
                             Job::StonecutterWalkHome,
                             Job::ForesterWalkToSpot,
                             Job::ForesterWalkHome,
                             Job::UnitWalk,
                             Job::CarrierWalkToSourcePile,
                             Job::CarrierWalkToTargetPile,
                             Job::CarrierWalkToTool,
                             Job::FarmerWalkToSow,
                             Job::FarmerWalkHome,
                             Job::FarmerWalkHomeLoaded,
                             Job::FarmerWalkToHarvest,
                             Job::WalkToOwnArea,
                             Job::UnitWalkCountdown,
                             Job::FisherWalkToWater,
                             Job::FisherWalkHome,
                             Job::WaterworkerWalkToWater,
                             Job::WaterworkerWalkHome,
                             Job::RiceFarmerWalkToPlant,
                             Job::RiceFarmerWalkHome,
                             Job::RiceFarmerWalkHomeLoaded,
                             Job::RiceFarmerWalkToHarvest,
                             Job::WinegrowerWalkToPlant,
                             Job::WinegrowerWalkHome,
                             Job::WinegrowerWalkHomeLoaded,
                             Job::WinegrowerWalkToHarvest,
                             Job::DonkeyWalkToPile,
                             Job::DonkeyWalkToMarket,
                             Job::ShipyardWalkToSlipway,
                             Job::ShipyardWalkHome,
                             Job::OnBoard,
                             Job::ShipWalkToHarbour,
                             Job::ShipSailWithCargo,
                             Job::ShipSailHome,
                             Job::DonkeyWalkHome,
                             Job::BeekeeperWalkToHiveSpot,
                             Job::BeekeeperWalkHome,
                             Job::BeekeeperWalkToHive,
                             Job::BeekeeperWalkHomeLoaded,
                             Job::Nothing,
                             Job::UnitIdle,
                             Job::SeekOwnArea,
                             Job::ArriveAtWork,
                             Job::HealerGoIn};
constexpr std::uint32_t kWalkJobCount = sizeof(kWalkJobs) / sizeof(kWalkJobs[0]);
constexpr SettlerType kWalkTypes[] = {
    SettlerType::Catapult, SettlerType::Ferry,      SettlerType::TradeShip, SettlerType::Thief,
    SettlerType::Pioneer,  SettlerType::Geologist,  SettlerType::Swordsman1, SettlerType::Bowman1,
    SettlerType::Pikeman1, SettlerType::Mage,       SettlerType::Carrier,   SettlerType::Digger,
    SettlerType::Swordsman2, SettlerType::ShipyardWorker, SettlerType::Ferry, SettlerType::TradeShip};
constexpr std::uint8_t kWalkTerrains[] = {0x10, 0x30, 0x05, 0x06, 0x20, 0x21, 0x2f, 0x00, 0x12, 0x1c, 0x40, 0x08};

// FindPath results: directions, waits, swaps, "no way" and odd values.
std::uint32_t PathResult(test::Rng& rng) {
    switch (rng.Below(10)) {
    case 0:
    case 1:
    case 2: return rng.Below(6);
    case 3: return rng.Below(8);
    case 4: return kPathWait | rng.Below(8);
    case 5: return kPathSwap | rng.Below(8);
    case 6: return kPathNoWay;
    case 7: return 8 + rng.Below(8);
    case 8: return rng.Below(0x20);
    default: return rng.Chance(1, 2) ? rng.Next32() & 0x1ff : rng.Below(6);
    }
}

void FillOffsetRings(test::WorldPair& worlds) {
    std::uint32_t k = 0;
    for (std::int32_t r = 0; r < static_cast<std::int32_t>(kWalkRings); ++r) {
        for (std::int32_t dx = -r; dx <= r; ++dx) {
            for (std::int32_t dy = -r; dy <= r; ++dy) {
                if (TileDistance(0, 0, static_cast<std::uint32_t>(dx), static_cast<std::uint32_t>(dy)) !=
                    static_cast<std::uint32_t>(r)) {
                    continue;
                }
                worlds.Both([&](CGameData* w) {
                    w->offsets[k].dx = dx;
                    w->offsets[k].dy = dy;
                    w->offsets[k].distance = r;
                });
                ++k;
            }
        }
    }
    worlds.Both([&](CGameData* w) { w->offsets[k].distance = 1000; });
}

// One-time setup: every record a walk can reach has sane numbers and
// coordinates (random words would index far outside the world).
void SanitizeWalkWorld(test::Context& ctx, test::WorldPair& worlds) {
    auto& rng = ctx.rng();
    FillOffsetRings(worlds);
    worlds.Both([&](CGameData* w) { w->map_size = kWalkMapSize; });
    for (std::uint32_t x = 0; x < 400; ++x) {
        for (std::uint32_t y = 0; y < 400; ++y) {
            const auto settler = static_cast<std::uint16_t>(rng.Chance(2, 3) ? 0 : 1 + rng.Below(kWalkSettlers));
            const auto object = static_cast<std::uint16_t>(rng.Below(kWalkBuildings));
            const std::uint32_t pick = rng.Below(10);
            const auto area = static_cast<std::uint16_t>(pick == 0   ? 0
                                                         : pick == 1 ? 0x8000 | rng.Below(0x10000)
                                                                     : 1 + rng.Below(0xffe));
            const std::uint8_t terrain = kWalkTerrains[rng.Below(sizeof(kWalkTerrains))];
            const auto region = static_cast<std::uint8_t>(1 + rng.Below(3));
            const auto owner = static_cast<std::uint8_t>(rng.Chance(1, 5) ? 0xff : rng.Below(4));
            const auto landscape = static_cast<std::uint8_t>(rng.Chance(3, 4) ? 0 : rng.Next32());
            const auto flags = static_cast<std::uint8_t>(rng.Next32() & (rng.Chance(1, 2) ? 0xff : 0xd2));
            worlds.Both([&](CGameData* w) {
                Tile& t = w->tiles[x][y];
                t.settler = settler;
                t.object_number = object;
                t.area = area;
                t.terrain = terrain;
                t.walk_region = region;
                t.owner = owner;
                t.object = landscape;
                t.flags = flags;
            });
        }
    }
    for (std::uint32_t n = 0; n <= kWalkSettlers; ++n) {
        const auto owner = static_cast<std::uint8_t>(rng.Below(4));
        const auto race = static_cast<std::uint8_t>(rng.Below(4));
        const auto x = static_cast<std::uint16_t>(30 + rng.Below(320));
        const auto y = static_cast<std::uint16_t>(30 + rng.Below(320));
        const std::uint8_t art = Value(kWalkTypes[rng.Below(sizeof(kWalkTypes))]);
        const auto job = rng.Chance(1, 4) ? Value(Job::GuardBuilding) : Value(kWalkJobs[rng.Below(kWalkJobCount)]);
        const auto part = static_cast<std::uint16_t>(rng.Below(kWalkSettlers + 1));
        const auto next = static_cast<std::uint16_t>(rng.Below(kWalkSettlers + 1));
        const auto prev = static_cast<std::uint16_t>(rng.Below(kWalkSettlers + 1));
        const auto slot = static_cast<std::uint8_t>(rng.Below(8));
        worlds.Both([&](CGameData* w) {
            Settler& s = w->settlers[n];
            s.owner = owner;
            s.race = race;
            s.x = x;
            s.y = y;
            s.art = art;
            s.job = job;
            s.saved_counter = part;
            s.next = next;
            s.prev = prev;
            s.slot = slot;
        });
    }
    for (std::uint32_t b = 0; b < kWalkBuildings; ++b) {
        const auto owner = static_cast<std::uint8_t>(rng.Below(4));
        const auto x = static_cast<std::uint16_t>(30 + rng.Below(320));
        const auto y = static_cast<std::uint16_t>(30 + rng.Below(320));
        const auto art = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0xf + rng.Below(4) : rng.Below(0x40));
        worlds.Both([&](CGameData* w) {
            layout::Building& r = w->buildings[b];
            r.owner = owner;
            r.x = x;
            r.y = y;
            r.art = art;
        });
    }
    for (std::uint32_t a = 0; a < 0x1000; ++a) {
        const auto player = static_cast<std::uint8_t>(rng.Below(5));
        worlds.Both([&](CGameData* w) { w->areas[a].player = player; });
    }
}

struct WalkCase {
    std::uint32_t n;
    std::uint32_t paths[2];
};

// A walking settler n with a random job and type, near or at its target,
// with path cache, soldier state, ship counter and the players' races and
// alliances picked to reach every branch.
WalkCase SetUpWalkCase(test::Context& ctx, test::WorldPair& worlds) {
    auto& rng = ctx.rng();
    WalkCase c{1 + rng.Below(kWalkSettlers), {PathResult(rng), PathResult(rng)}};
    if (rng.Chance(1, 4)) {
        c.paths[1] = c.paths[0];
    }
    const std::uint8_t art = Value(kWalkTypes[rng.Below(sizeof(kWalkTypes))]);
    const std::uint32_t jobPick = rng.Below(10);
    // Mostly the jobs with their own arrival code: the walk states, the ship legs (327..330).
    const auto job = static_cast<std::uint16_t>(
        jobPick < 3   ? Value(rng.Chance(1, 2) ? Job::UnitWalk : Job::UnitWalkCountdown)
        : jobPick < 4 ? Value(Job::ShipWalkToHarbour) + rng.Below(4)
        : jobPick < 5 ? Value(rng.Chance(1, 2) ? Job::DonkeyWalkToMarket : Job::WalkToOwnArea)
                      : Value(kWalkJobs[rng.Below(kWalkJobCount)]));
    const auto owner = static_cast<std::uint8_t>(rng.Below(4));
    std::int32_t races[4], presence[4];
    for (int p = 0; p < 4; ++p) {
        races[p] = static_cast<std::int32_t>(rng.Chance(1, 8) ? 4 + rng.Below(3) : rng.Below(4));
        presence[p] = static_cast<std::int32_t>(rng.Below(3));
    }
    const auto x = static_cast<std::uint16_t>(40 + rng.Below(300));
    const auto y = static_cast<std::uint16_t>(40 + rng.Below(300));
    const std::uint32_t where = rng.Below(10);  // 0..3 at the target, 4..5 near, else walking
    std::uint16_t tx = x, ty = y;
    if (where >= 4) {
        tx = static_cast<std::uint16_t>(x + rng.Below(21) - 10);
        ty = static_cast<std::uint16_t>(y + rng.Below(21) - 10);
        if (rng.Chance(1, 20)) {
            tx = static_cast<std::uint16_t>(rng.Chance(1, 2) ? kWalkMapSize + rng.Below(5) : rng.Next32());
        }
    }
    std::uint16_t cache[6];
    for (std::uint16_t& word : cache) {
        word = static_cast<std::uint16_t>(rng.Chance(1, 2) ? kNoWaypoint : 30 + rng.Below(320));
    }
    if (rng.Chance(1, 2)) {
        cache[0] = static_cast<std::uint16_t>(30 + rng.Below(320));
        cache[1] = static_cast<std::uint16_t>(30 + rng.Below(320));
    }
    const std::uint32_t mark = rng.Below(4);
    cache[4] = static_cast<std::uint16_t>(mark == 0 ? kNearMark : mark == 1 ? kPatrolMark : cache[4]);
    if (mark == 0 || rng.Chance(1, 3)) {
        // A post (pc[2], pc[3]) some way off, often within pc[5] steps.
        cache[2] = static_cast<std::uint16_t>(x + rng.Below(41) - 20);
        cache[3] = static_cast<std::uint16_t>(y + rng.Below(41) - 20);
        cache[5] = static_cast<std::uint16_t>(rng.Chance(3, 4) ? 40 : rng.Below(20));
    }
    if (where >= 4 && where < 6) {
        cache[4] = kNearMark;
        cache[5] = static_cast<std::uint16_t>(rng.Below(30));
    }
    const auto state = static_cast<std::uint8_t>(rng.Below(7));
    const auto slot = static_cast<std::uint8_t>(rng.Chance(1, 4) ? 0 : rng.Below(8));
    const auto health = static_cast<std::uint8_t>(rng.Below(8));
    const auto frame = static_cast<std::uint8_t>(rng.Next32());
    const auto stepTime = static_cast<std::uint8_t>(rng.Chance(1, 4) ? 0 : rng.Next32());
    const std::uint32_t countPick = rng.Below(5);
    const auto walkCount = static_cast<std::uint8_t>(countPick == 0   ? 0xff
                                                     : countPick == 1 ? 0xf0 + rng.Below(16)
                                                     : countPick == 2 ? 6 + rng.Below(2) + 8 * rng.Below(4)
                                                                      : rng.Next32());
    const std::uint32_t savedDirection = rng.Chance(1, 2) ? 0xffff : rng.Below(6);
    const auto pathMode = static_cast<std::uint8_t>(rng.Next32());
    const bool onTile = rng.Chance(7, 8);
    const auto race = static_cast<std::uint8_t>(rng.Below(4));
    worlds.Both([&](CGameData* w) {
        for (int p = 0; p < 4; ++p) {
            w->players[p].race = races[p];
            w->players[p].team = presence[p];
        }
        Settler& s = w->settlers[c.n];
        s.art = art;
        s.job = job;
        s.owner = owner;
        s.race = race;
        s.x = x;
        s.y = y;
        s.target_x = tx;
        s.target_y = ty;
        for (int k = 0; k < 6; ++k) {
            s.path_cache[k] = cache[k];
        }
        s.walk_state = state;
        s.slot = slot;
        s.health = health;
        s.frame = frame;
        s.step_time = stepTime;
        s.ship_walk = walkCount;
        s.saved_animation = savedDirection;
        s.path_mode = pathMode;
        if (onTile) {
            w->tiles[x][y].settler = static_cast<std::uint16_t>(c.n);
        }
        // Race 3 soldiers: a building on the post tile, so the target
        // search always looks at one first (see the known difference).
        if (races[owner] == 3 && state == 2 && cache[2] < 400 && cache[3] < 400) {
            w->tiles[cache[2]][cache[3]].flags |= layout::kTileHasBuilding;
            w->tiles[cache[2]][cache[3]].object_number %= kWalkBuildings;
        }
    });
    return c;
}

constexpr std::uintptr_t kStubbed[] = {kFindPath, kResetSettlerToIdle, kSpawnProjectile, kSpawnDeathEffect,
                                       kAllocTable36700Record, kRemoveSettlerFromLists,
                                       kMoveSettlerToBuildingTileA, kSetSettlerAnimation};

void CompareWalkSettler(test::Context& ctx, WalkFn candidate, int cases) {
    test::WorldPair worlds(ctx, sizeof(CGameData));
    SanitizeWalkWorld(ctx, worlds);
    test::CalleeStubs stubs;
    WalkCase current{};
    std::uint32_t pathCalls = 0;
    stubs.Install<1, kFindPath>([&](CGameData*, std::uint32_t, const std::uint32_t*) {
        const std::uint32_t result = current.paths[pathCalls % 2];
        ++pathCalls;
        return result;
    });
    stubs.Install<1, kResetSettlerToIdle>();
    stubs.Install<10, kSpawnProjectile>([](CGameData*, std::uint32_t, const std::uint32_t* args) {
        return args[0] ^ 0x5a5a;  // a "projectile" number
    });
    stubs.Install<7, kSpawnDeathEffect>();
    stubs.Install<10, kAllocTable36700Record>();
    stubs.Install<1, kRemoveSettlerFromLists>();
    stubs.Install<1, kMoveSettlerToBuildingTileA>();
    stubs.Install<2, kSetSettlerAnimation>();
    static_assert(sizeof(kStubbed) / sizeof(kStubbed[0]) == 8);
    DebugLogRecorder logs;
    const std::uint32_t savedTick = g_gameTick;
    struct RestoreTick {
        std::uint32_t value;
        ~RestoreTick() { g_gameTick = value; }
    } restoreTick{savedTick};
    for (int i = 0; i < cases; ++i) {
        current = SetUpWalkCase(ctx, worlds);
        const std::uint32_t tick = ctx.rng().Next32();
        g_gameTick = tick;
        stubs.SetWorld(worlds.original());
        pathCalls = 0;
        const std::uint32_t expected = Original_CGameData_WalkSettler(worlds.original(), nullptr, current.n);
        const std::vector<test::StubCall> expectedCalls = stubs.Take();
        const std::vector<DebugLogCall> expectedLogs = logs.Take();
        stubs.SetWorld(worlds.ours());
        pathCalls = 0;
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, current.n);
        const std::vector<test::StubCall> actualCalls = stubs.Take();
        const std::vector<DebugLogCall> actualLogs = logs.Take();
        const bool settlerSame =
            worlds.SameField(ctx, [&](CGameData* w) -> auto& { return w->settlers[current.n]; });
        if (!S3RE_EXPECT_EQ(expected, actual) || !S3RE_EXPECT(expectedCalls == actualCalls) ||
            !S3RE_EXPECT(expectedLogs == actualLogs) || !S3RE_EXPECT(settlerSame) ||
            ((i % 64 == 63 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            const Settler& s = worlds.original()->settlers[current.n];
            ctx.Fail(__FILE__, __LINE__,
                     "  case %d (or one of the 63 before it): settler %u art 0x%x job 0x%x paths 0x%x 0x%x", i,
                     current.n, s.art, s.job, current.paths[0], current.paths[1]);
            return;
        }
        // A settler that died here keeps a player for later cases.
        worlds.Both([&](CGameData* w) {
            if (w->settlers[current.n].owner == 0xff) {
                w->settlers[current.n].owner = 0;
            }
        });
    }
}

// Wrong: a shot faces one direction off.
std::uint32_t __fastcall WrongWalkSettlerShot(CGameData* world, void* edx, std::uint32_t settler) {
    const std::uint32_t result = CGameData_WalkSettler(world, edx, settler);
    if (JobOf(world->settlers[settler]) == Job::Shoot) {
        world->settlers[settler].direction ^= 1;
    }
    return result;
}

// Wrong: ships count disagreements in the low bits.
std::uint32_t __fastcall WrongWalkSettlerShip(CGameData* world, void* edx, std::uint32_t settler) {
    const bool ship = TypeOf(world->settlers[settler]) == SettlerType::Ferry &&
                      (world->settlers[settler].x != world->settlers[settler].target_x ||
                       world->settlers[settler].y != world->settlers[settler].target_y);
    const std::uint32_t result = CGameData_WalkSettler(world, edx, settler);
    if (ship) {
        world->settlers[settler].ship_walk ^= 1;
    }
    return result;
}

}  // namespace

S3RE_TEST(CGameData_WalkSettler_MatchesOriginal) {
    CompareWalkSettler(ctx, &CGameData_WalkSettler, 40000);
}
S3RE_TEST(CGameData_WalkSettler_ComparisonDetectsWrongShot) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareWalkSettler(probe, &WrongWalkSettlerShot, 20000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_WalkSettler_ComparisonDetectsWrongShip) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareWalkSettler(probe, &WrongWalkSettlerShip, 4000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
