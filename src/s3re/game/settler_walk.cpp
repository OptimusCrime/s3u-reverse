// Walking settlers (UpdateSettlers' walking jobs) and the job lists the
// pathfinder uses.
//
// GameWorld::AddTileTraffic (0x00503940): (x, y): unless the tile's traffic
//   byte (+0x12) is 0xff it is increased by one; when it reaches 12 or more,
//   GameWorld::WearPath (0x51b7f0) is called for the tile (which adds 9 when
//   the tile becomes a path). thiscall, ret 8. EAX = the address of the
//   traffic byte, or WearPath's EAX when it was called (the only caller,
//   WalkSettler 0x500860, ignores it).
// GameWorld::IsWalkingJob (0x004fa6d0): (job) -> AL = 1 for the 47 jobs that
//   UpdateSettlers hands to WalkSettler (handler 0x4ead27 and the two jobs
//   0x35 and 0xb0 that call it too), else 0. The upper 24 bits of EAX are
//   those of the argument. ret 4, ECX unused. Called by 0x4a33d0.
// GameWorld::IsStationaryJob (0x004fa880, was Logic_sub_4fa880, proposed
//   "IsWalkingOrIdleJob"): (job) -> AL = 0 for the same 47 walking jobs and
//   the idle jobs 1 and 0x37, AL = 1 for every other job (the settler is
//   working at a place). Upper EAX bits as above. ret 4, ECX unused. Called
//   by Pathing::TileCost (0x4a36a0): a tile whose settler is busy counts as
//   blocked.
// GameWorld::WalkSettler (0x00500860, was Logic_sub_500860): (settler) ->
//   EAX = 1 when UpdateSettlers should handle the settler again at once
//   (it stores EAX at 0x4ead2f), else 0. thiscall, ret 4. One step of a
//   walking settler (the 47 walking jobs; jobs 0x35/0xb0 too):
//   1. animation frame +0x16 = (+0x16 + +0x19) % (type 0x29 ? 0x24 : 0xc).
//   2. Not at the target (+0x24/+0x26), and not within path_cache[5]
//      steps of (path_cache[2], [3]) when path_cache[4] is 0xfffd: a step.
//      A target off the map logs "Settler tries to reach spot out of
//      map". Type 0x29 (a seven-tile unit; part settlers chained through
//      +0x20): its six neighbour tiles are cleared where they are free,
//      FindPath, the step, and the six part settlers are written around
//      the (new) position. Ships (type 0x2c, or 0x2b with +0x3c != 0xff):
//      the areas of the six neighbour tiles are cleared towards settlers
//      within 7 rings (so FindPath avoids them) and FindPath runs before
//      and after; when the two disagree the walk counter in +0x3c's top
//      five bits grows (at 0xf8 the ship gives up: job 0x37, EAX = 1), the
//      low three bits hold the last direction; the areas are restored at
//      the end. Others: FindPath; 0xff -> ResetSettlerToIdle (not for
//      ships); | 8 -> wait facing the direction; | 0x10 -> swap places with
//      the settler in the way (a civilian on another player's tile instead
//      logs "TROUBLE: wollte auf illegals Feld wechseln" and goes idle);
//      else the step (AddTileTraffic once for civilians, three times for
//      jobs 0xc/0x4e). A step sets the step time +0x19 = terrain speed
//      (S3.EXE 0x658c1c) + type speed (0x6591a4; type 0x17 with +0x15 != 0:
//      + 1) and the counter +0xc = +0x19 - 1, and frame +0x16 = 1 if 0.
//   3. At the target: +0x16 = 0 and the job's next step: most jobs become
//      the next job with counter 0; 0x30/0x93/0xe8/0xf9/0x141/0x168 step
//      onto their building's tile A (MoveSettlerToBuildingTileA) with an
//      animation; 0xab (a soldier sent to a building area): own area ->
//      ClearSettlerJob, else health -= 2 and job 0xac, or with health < 3
//      it dies (a MapEffects effect, record freed); ships' jobs
//      0x135/0x148/0x149/0x14a take the next waypoint from the path cache
//      and stop where the region/area changes; jobs 0x35/0xb0 (soldiers)
//      by state +0x1a: 5/4/3 (type 0x17): idle / back to path_cache[0..1]
//      / job 0x152; 2 with arrows left (+0x15) near (path_cache[2], [3]):
//      "suche Ziel bei %u %u", a target within radius 5 (an enemy military
//      building not guarded by a job-0xae soldier, a catapult or a ship),
//      out of range (10 tiles, race 3: 3) -> a shot: per-race random
//      delay, direction, job 0x151, SpawnProjectile and an effect record
//      (AllocTable36700Record); 1 (land types): board a friendly ship
//      within radius 15 from a shore tile (terrain 0x30) of the walk
//      region, or walk to that tile. Then types 0x28/0x16 at a free tile
//      (jobs 0x12e / 0xd4, tile flag 0x2), and with nothing decided the
//      path cache gives the next target (0xfffe in [4]: patrol between two
//      points), or job 0x37 and EAX = 1.
//   Known difference: race 3 soldiers that shoot at a catapult or ship
//   with a delay roll above 0x4e20 take the delay from the building last
//   looked at in the target search (+0x38 + 1); when no building was
//   looked at, the original reads an uninitialized local; ours uses
//   building 0.

#include "game/debug_log.h"
#include "game/raw.h"
#include "game/replacements.h"
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

constexpr std::uint16_t kWalkingJobs[] = {
    0x002, 0x003, 0x006, 0x00a, 0x00c, 0x00f, 0x011, 0x01c, 0x01d, 0x02a, 0x02b, 0x02d,
    0x030, 0x035, 0x04c, 0x04e, 0x068, 0x090, 0x093, 0x098, 0x09c, 0x0ab, 0x0b0, 0x0b2,
    0x0b5, 0x0bb, 0x0bc, 0x0e5, 0x0e8, 0x0ed, 0x0f1, 0x0f6, 0x0f9, 0x0fe, 0x102, 0x133,
    0x135, 0x140, 0x141, 0x147, 0x148, 0x149, 0x14a, 0x165, 0x168, 0x16a, 0x16d};
static_assert(sizeof(kWalkingJobs) / sizeof(kWalkingJobs[0]) == 47);

bool InWalkingList(std::uint32_t job) {
    for (std::uint16_t walking : kWalkingJobs) {
        if (job == walking) {
            return true;
        }
    }
    return false;
}

}  // namespace

using TileFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);
using JobFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t job);

// Purpose: Counts a settler's step onto a tile; tiles walked on often enough become paths.
std::uint32_t __fastcall GameWorld_AddTileTraffic(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    Tile& tile = TileAtOffset(world, TileOffset(x, y));
    const std::uint32_t address = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&tile.traffic));
    if (tile.traffic == 0xff) {
        return address;
    }
    ++tile.traffic;
    if (tile.traffic < 12) {
        return address;
    }
    return GameWorld_WearPath(world, nullptr, x, y);
}

// Purpose: Whether a job is one of the walking jobs (the settler is on its way somewhere).
std::uint32_t __fastcall GameWorld_IsWalkingJob(GameWorld*, void*, std::uint32_t job) {
    return (job & 0xffffff00u) | (InWalkingList(job) ? 1u : 0u);
}

// Purpose: Whether a settler with this job stays where it is (it is neither walking nor idle), so
// the pathfinder treats its tile as blocked.
std::uint32_t __fastcall GameWorld_IsStationaryJob(GameWorld*, void*, std::uint32_t job) {
    const bool moving = InWalkingList(job) || job == 1 || job == 0x37;
    return (job & 0xffffff00u) | (moving ? 0u : 1u);
}

S3RE_HOOK(0x00503940, GameWorld_AddTileTraffic, TileFn, Original_GameWorld_AddTileTraffic,
          GameWorld_AddTileTraffic);
S3RE_HOOK(0x004fa6d0, GameWorld_IsWalkingJob, JobFn, Original_GameWorld_IsWalkingJob, GameWorld_IsWalkingJob);
S3RE_HOOK(0x004fa880, GameWorld_IsStationaryJob, JobFn, Original_GameWorld_IsStationaryJob,
          GameWorld_IsStationaryJob);

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

// S3.EXE data tables.
const std::uint8_t* const kTerrainSpeed = reinterpret_cast<const std::uint8_t*>(0x00658c1c);  // per terrain
const std::uint8_t* const kTypeSpeed = reinterpret_cast<const std::uint8_t*>(0x006591a4);     // int32 per type
const std::int32_t* const kWalkTypeClass = reinterpret_cast<const std::int32_t*>(0x00658f64);  // 0 = civilian
constexpr std::uintptr_t kRaceShotTime = 0x00659dc0;  // u16 per race (stride 4): shot animation length
const std::int32_t* const kRaceShipCapacity = reinterpret_cast<const std::int32_t*>(0x00659644);
const std::uint16_t* const kPlayerColours = reinterpret_cast<const std::uint16_t*>(0x007ace64);
constexpr std::uintptr_t kGameTick = 0x007dfd48;  // g_gameTick

constexpr std::uintptr_t kOutOfMapText = 0x006a5ffc;      // "Settler tries to reach spot out of map"
constexpr std::uintptr_t kFiveNumbersText = 0x006a5fec;   // "%u %u %u %u %u"
constexpr std::uintptr_t kNotThereText = 0x006a5fbc;      // "SnotThere nr: %u  owner %u   art %u   job  %u"
constexpr std::uintptr_t kIllegalSwapText = 0x006a5f84;   // "TROUBLE: wollte auf illegals Feld wechseln ..."
constexpr std::uintptr_t kWalkCountText = 0x006a5f5c;     // "Illegal walkcount vorher %u, Ship %u"
constexpr std::uintptr_t kSearchTargetText = 0x006a6048;  // "suche Ziel bei %u %u"
constexpr std::uintptr_t kAttackBuildingText = 0x006a6038;  // "Angriff Gebaeude"
constexpr std::uintptr_t kAttackCatapultText = 0x006a6024;  // "Angriff Katapult"

constexpr std::uint16_t kNoWaypoint = 0xffff;
constexpr std::uint16_t kPatrolMark = 0xfffe;   // path_cache[4]: swap the two waypoints
constexpr std::uint16_t kNearMark = 0xfffd;     // path_cache[4]: (pc[2], pc[3]) within pc[5] steps is enough
constexpr std::uint32_t kPathNone = 0xff;       // FindPath: no way
constexpr std::uint8_t kTypeSevenTile = 0x29;  // catapult-like unit on seven tiles
constexpr std::uint8_t kTypeShip = 0x2c;
constexpr std::uint8_t kTypeShip2b = 0x2b;

Tile& TileAt(GameWorld* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

// The tile (dx, dy) away from the tile at `offset` (the original's
// +-0x4800 / +-0x18 steps).
Tile& TileNear(GameWorld* world, std::uint32_t offset, std::int32_t dx, std::int32_t dy) {
    return TileAtOffset(world, offset + static_cast<std::uint32_t>(dx) * kTileRowBytes +
                                   static_cast<std::uint32_t>(dy) * kTileBytes);
}

// The six neighbours in direction order 0..5.
constexpr std::int32_t kSixDx[6] = {1, 1, 0, -1, -1, 0};
constexpr std::int32_t kSixDy[6] = {0, 1, 1, 0, -1, -1};

bool IsShipLegJob(std::uint16_t job) {
    return job == 0x147 || job == 0x148 || job == 0x149;
}

// The step time of a settler entering `tile`; counter = time - 1.
void SetStepTime(Settler& s, const Tile& tile) {
    if (s.art == 0x17 && s.slot != 0) {
        s.step_time = static_cast<std::uint8_t>(kTerrainSpeed[tile.terrain] + kTypeSpeed[0x17 * 4] + 1);
    } else {
        s.step_time = static_cast<std::uint8_t>(kTerrainSpeed[tile.terrain] + kTypeSpeed[s.art * 4u]);
    }
    s.counter = static_cast<std::uint16_t>(s.step_time - 1u);
}

void StartFrames(Settler& s) {
    if (s.frame == 0) {
        s.frame = 1;
    }
}

void SetJob(Settler& s, std::uint16_t job) {
    s.job = job;
    s.counter = 0;
}

// Takes the next waypoint from the path cache into the target.
void PopWaypoint(Settler& s) {
    s.target_x = s.path_cache[0];
    s.target_y = s.path_cache[1];
    s.path_cache[0] = s.path_cache[2];
    s.path_cache[1] = s.path_cache[3];
    s.path_cache[2] = s.path_cache[4];
    s.path_cache[3] = s.path_cache[5];
    s.path_cache[4] = kNoWaypoint;
    s.path_cache[5] = kNoWaypoint;
}

struct Walk {
    GameWorld* world;
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
    o.direction = (d + 3) % 6;
    o.x = s.x;
    o.y = s.y;
    s.x = static_cast<std::uint16_t>(nx);
    s.y = static_cast<std::uint16_t>(ny);
    SetStepTime(s, next);
    StartFrames(s);
    SetStepTime(o, TileAt(w.world, s.x, s.y));
    StartFrames(o);
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

void EnsureOnTile(Walk& w) {
    Tile& tile = TileAt(w.world, w.s.x, w.s.y);
    if (tile.settler != w.n) {
        tile.settler = static_cast<std::uint16_t>(w.n);
    }
}

// Type 0x29: a unit on seven tiles; its six part settlers follow it.
std::uint32_t WalkSevenTileUnit(Walk& w) {
    GameWorld* world = w.world;
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
    static constexpr Outer kOuter[6] = {
        {{2, 2, 1}, {0, 1, -1}},    {{2, 2, 1}, {2, 1, 2}},     {{0, -1, 1}, {2, 1, 2}},
        {{-2, -2, -1}, {0, -1, 1}}, {{-2, -2, -1}, {-2, -1, -2}}, {{0, -1, 1}, {-2, -2, -1}}};
    for (int k = 0; k < 6; ++k) {
        if (isFree(kOuter[k].dx[0], kOuter[k].dy[0]) && isFree(kOuter[k].dx[1], kOuter[k].dy[1]) &&
            isFree(kOuter[k].dx[2], kOuter[k].dy[2])) {
            TileNear(world, at, kSixDx[k], kSixDy[k]).settler = 0;
        }
    }
    EnsureOnTile(w);
    const std::uint32_t path = Call<kFindPath>(world, w.n);
    if ((path & 0x18) == 0) {
        for (int k = 0; k < 6; ++k) {
            TileNear(world, at, kSixDx[k], kSixDy[k]).settler = 0;
        }
        std::uint32_t nx = 0, ny = 0;
        Tile& next = StepTo(w, path & 7, &nx, &ny);
        s.x = static_cast<std::uint16_t>(nx);
        s.y = static_cast<std::uint16_t>(ny);
        SetStepTime(s, next);
        StartFrames(s);
    } else {
        if (path != kPathNone) {
            s.direction = path & 7;
        }
        s.counter = 0;
        s.frame = 0;
        s.step_time = 0;
    }
    const std::uint32_t here = TileOffset(s.x, s.y);
    std::uint32_t part = w.n;
    for (int k = 0; k < 6; ++k) {
        part = world->settlers[part].saved_counter;  // the next part settler
        TileNear(world, here, kSixDx[k], kSixDy[k]).settler = static_cast<std::uint16_t>(part);
    }
    return w.again;
}

// Ships (type 0x2c, and 0x2b while +0x3c != 0xff).
std::uint32_t WalkShip(Walk& w) {
    GameWorld* world = w.world;
    Settler& s = w.s;
    const std::uint32_t x0 = s.x, y0 = s.y;
    const std::uint32_t at = TileOffset(x0, y0);
    std::uint16_t savedAreas[6];
    for (int k = 0; k < 6; ++k) {
        savedAreas[k] = TileNear(world, at, kSixDx[k], kSixDy[k]).area;
    }
    const std::uint32_t first = Call<kFindPath>(world, w.n);
    // Close the directions with settlers ahead: a wedge of 7 rings per
    // direction (one side: any settler; the other: settlers on terrain
    // below 0x10).
    for (std::uint32_t d = 0; d < 6; ++d) {
        bool crowded = false;
        for (std::uint32_t k = 1; k <= 7; ++k) {
            std::uint32_t sx = s.x, sy = s.y;
            for (std::uint32_t i = k; i != 0; --i) {
                sx = NeighbourXOf(sx, d);
                sy = NeighbourYOf(sy, d);
            }
            std::uint32_t px = sx, py = sy;
            const std::uint32_t side1 = (d + 4 - 2) % 6;
            for (std::uint32_t i = k + 1; i != 0; --i) {
                const std::uint32_t size = world->map_size;
                if (px < size && py < size && TileAt(world, px, py).settler != 0) {
                    crowded = true;
                }
                px = NeighbourXOf(px, side1);
                py = NeighbourYOf(py, side1);
            }
            const std::uint32_t side2 = (d + 4) % 6;
            px = sx;
            py = sy;
            for (std::uint32_t i = k + 1; i != 0; --i) {
                const std::uint32_t size = world->map_size;
                if (px < size && py < size) {
                    const Tile& t = TileAt(world, px, py);
                    if ((t.terrain & 0xf0) == 0 && t.settler != 0) {
                        crowded = true;
                    }
                }
                px = NeighbourXOf(px, side2);
                py = NeighbourYOf(py, side2);
            }
        }
        if (crowded) {
            const std::uint32_t nx = NeighbourXOf(s.x, d);
            const std::uint32_t ny = NeighbourYOf(s.y, d);
            TileAt(world, nx, ny).area = 0;
        }
    }
    EnsureOnTile(w);
    std::uint32_t path = Call<kFindPath>(world, w.n);
    {
        const std::uint8_t count = s.ship_walk;
        if (((count & 7) == 6 || (count & 7) == 7) && count != 0xff) {
            GameDebugLog(kWalkCountText, static_cast<std::uint32_t>(count), w.n);
            s.ship_walk = static_cast<std::uint8_t>(s.ship_walk & 0xf8);
        }
    }
    // +0x3c: low three bits the last direction, the rest a counter of
    // steps where the two searches disagreed.
    auto back = [&]() { return static_cast<std::uint32_t>(((s.ship_walk & 7) + 3) % 6); };
    const bool legJob = s.art != kTypeShip && IsShipLegJob(s.job);
    if (path == first) {
        if (path > 5) {
            s.ship_walk = static_cast<std::uint8_t>(s.ship_walk + 8);
        } else if (!legJob) {
            s.ship_walk = static_cast<std::uint8_t>(s.ship_walk & 7);
        }
    } else if (path > 5) {
        s.ship_walk = static_cast<std::uint8_t>(s.ship_walk + 8);
    } else if (first > 5 && (first < 8 || first > 0xd)) {
        path = kPathNone;
        s.ship_walk = static_cast<std::uint8_t>(s.ship_walk + 8);
    } else {
        const std::uint32_t c = first & 7;
        const std::uint32_t left = (c - 1) % 6;
        const std::uint32_t right = (c + 1) % 6;
        // Accepted: the first search's direction or one of its neighbours,
        // unless that is straight back.
        bool accepted = false;
        if (path == c && path != back()) {
            accepted = true;
        } else if (path == left && path != back()) {
            accepted = true;
        } else if (path == right && path != back()) {
            accepted = true;
        }
        if (accepted) {
            if (legJob) {
                s.ship_walk = static_cast<std::uint8_t>(s.ship_walk + 8);
            } else {
                s.ship_walk = static_cast<std::uint8_t>(s.ship_walk & 7);
            }
        } else {
            path = static_cast<std::uint32_t>((s.ship_walk & 7) | 8);  // wait
            s.ship_walk = static_cast<std::uint8_t>(s.ship_walk + 8);
        }
    }
    bool wait = false;
    const std::uint8_t count = s.ship_walk;
    if (count >= 0xf8) {
        if (s.art == kTypeShip2b && IsShipLegJob(s.job)) {
            s.ship_walk = 0xff;
        } else {
            s.ship_walk = static_cast<std::uint8_t>(count & 7);
            w.again = 1;
            SetJob(s, 0x37);
            s.frame = 0;
        }
        wait = true;
    } else if (path == kPathNone) {
        wait = true;
    } else if ((path & 0x18) == 0) {
        const std::uint32_t d = path & 7;
        if (s.art != kTypeShip && IsShipLegJob(s.job)) {
            s.ship_walk = static_cast<std::uint8_t>((count & 0xf8) | d);
        } else {
            s.ship_walk = static_cast<std::uint8_t>(d);
        }
        std::uint32_t nx = 0, ny = 0;
        Tile& next = StepTo(w, d, &nx, &ny);
        s.x = static_cast<std::uint16_t>(nx);
        s.y = static_cast<std::uint16_t>(ny);
        SetStepTime(s, next);
        StartFrames(s);
    } else if ((path & 8) != 0) {
        s.direction = path & 7;
        wait = true;
    } else if ((path & 0x10) != 0) {
        const std::uint32_t d = path & 7;
        const std::uint32_t nx = NeighbourXOf(s.x, d);
        const std::uint32_t ny = NeighbourYOf(s.y, d);
        s.direction = d;
        Tile& next = TileAt(world, nx, ny);
        Settler& o = SwapPlaces(w, d, nx, ny, next, next.settler);
        o.path_mode &= 0xdf;
    }
    if (wait) {
        s.counter = 7;
        s.frame = 0;
        s.step_time = 0;
    }
    for (int k = 0; k < 6; ++k) {
        TileNear(world, at, kSixDx[k], kSixDy[k]).area = savedAreas[k];
    }
    return w.again;
}

// Every other walker.
std::uint32_t WalkOne(Walk& w) {
    GameWorld* world = w.world;
    Settler& s = w.s;
    Tile& tile = TileAt(world, s.x, s.y);
    if (tile.settler != w.n) {
        GameDebugLog(kNotThereText, w.n, static_cast<std::uint32_t>(s.owner), static_cast<std::uint32_t>(s.art),
                     static_cast<std::uint32_t>(s.job));
        TileAt(world, s.x, s.y).settler = static_cast<std::uint16_t>(w.n);
    }
    const std::uint32_t path = Call<kFindPath>(world, w.n);
    if (path == kPathNone) {
        if (s.art != kTypeShip && s.art != kTypeShip2b) {
            Call<kResetSettlerToIdle>(world, w.n);
        }
        return w.again;
    }
    if ((path & 0x18) == 0) {
        const std::uint32_t d = path & 7;
        std::uint32_t nx = 0, ny = 0;
        Tile& next = StepTo(w, d, &nx, &ny);
        if (s.job == 0xc || s.job == 0x4e) {
            Call<kAddTileTraffic>(world, nx, ny);
            Call<kAddTileTraffic>(world, nx, ny);
            Call<kAddTileTraffic>(world, nx, ny);
        } else if (kWalkTypeClass[s.art] == 0) {
            Call<kAddTileTraffic>(world, nx, ny);
        }
        s.x = static_cast<std::uint16_t>(nx);
        s.y = static_cast<std::uint16_t>(ny);
        SetStepTime(s, next);
        StartFrames(s);
        return w.again;
    }
    if ((path & 8) != 0) {
        s.direction = path & 7;
        s.counter = 0;
        s.frame = 0;
        s.step_time = 0;
        return w.again;
    }
    if ((path & 0x10) == 0) {
        return w.again;
    }
    // Swap places with the settler in the way.
    const std::uint32_t d = path & 7;
    const std::uint32_t nx = NeighbourXOf(s.x, d);
    const std::uint32_t ny = NeighbourYOf(s.y, d);
    s.direction = d;
    Tile& next = TileAt(world, nx, ny);
    const std::uint32_t other = next.settler;
    const Settler& blocker = world->settlers[other];
    if (kWalkTypeClass[blocker.art] == 0 && blocker.job != 0xab && blocker.job != 0xac) {
        const Tile& here = TileAt(world, s.x, s.y);
        if (here.owner != blocker.owner) {
            GameDebugLog(kIllegalSwapText, static_cast<std::uint32_t>(here.owner),
                         static_cast<std::uint32_t>(blocker.owner), static_cast<std::uint32_t>(here.area),
                         static_cast<std::uint32_t>(next.area));
            s.counter = 0;
            Call<kResetSettlerToIdle>(world, other);
            return w.again;
        }
    }
    Settler& o = SwapPlaces(w, d, nx, ny, next, other);
    o.path_mode &= 0xdf;
    if ((Call<kIsSettlerJob01Or37>(world, static_cast<std::uint32_t>(o.job)) & 0xff) != 0) {
        o.target_x = o.x;
        o.target_y = o.y;
        o.job = static_cast<std::uint16_t>(Call<kJobAfter01Or37>(world, static_cast<std::uint32_t>(o.job)));
    }
    return w.again;
}

std::uint32_t WalkStep(Walk& w) {
    GameWorld* world = w.world;
    Settler& s = w.s;
    const auto size = static_cast<std::int32_t>(world->map_size);
    if (static_cast<std::int32_t>(s.target_x) >= size || static_cast<std::int32_t>(s.target_y) >= size) {
        GameDebugLog(kOutOfMapText);
        GameDebugLog(kFiveNumbersText, static_cast<std::uint32_t>(s.target_x), static_cast<std::uint32_t>(s.target_y),
                     static_cast<std::uint32_t>(s.art), static_cast<std::uint32_t>(s.job), w.n);
    }
    if (s.art == kTypeSevenTile) {
        return WalkSevenTileUnit(w);
    }
    if (s.art == kTypeShip || (s.art == kTypeShip2b && s.ship_walk != 0xff)) {
        return WalkShip(w);
    }
    return WalkOne(w);
}

// A soldier (state 2) at its post looks for something to shoot at around
// (path_cache[2], [3]). Returns whether it shot.
bool SoldierShoot(Walk& w) {
    GameWorld* world = w.world;
    Settler& s = w.s;
    const std::uint32_t cx = s.path_cache[2], cy = s.path_cache[3];
    GameDebugLog(kSearchTargetText, cx, cy);
    std::uint32_t kind = 0;   // 5 building, 6 catapult, 7 ship
    std::uint32_t target = 0;  // its record number
    std::uint32_t tx = 0, ty = 0;
    // The building tile looked at last (the original keeps it in a local
    // that is only set on building tiles; see the known difference above).
    std::uint32_t lastBuilding = 0;
    const layout::Player& me = world->players[s.owner];
    for (std::uint32_t i = 0; world->offsets[i].distance < 5; ++i) {
        if (kind != 0) {
            break;
        }
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
            if (b.art != 0xf && b.art != 0x10 && b.art != 0x12 && me.race != 3) {
                continue;
            }
            bool guarded = false;
            for (std::uint32_t j = 0; world->offsets[j].distance < 7; ++j) {
                if (guarded) {
                    break;
                }
                const std::uint32_t qx = static_cast<std::uint32_t>(world->offsets[j].dx) + px;
                const std::uint32_t qy = static_cast<std::uint32_t>(world->offsets[j].dy) + py;
                if (Call<kIsOnMap>(world, qx, qy) == 0) {
                    continue;
                }
                const std::uint32_t guard = TileAt(world, qx, qy).settler;
                if (guard != 0 && kWalkTypeClass[world->settlers[guard].art] != 0 &&
                    world->settlers[guard].job == 0xae) {
                    guarded = true;
                }
            }
            if (guarded) {
                continue;
            }
            tx = b.x;
            ty = b.y;
            GameDebugLog(kAttackBuildingText);
            kind = 5;
            target = number;
        } else if (t.settler != 0) {
            const std::uint32_t number = t.settler;
            const Settler& o = world->settlers[number];
            if (world->players[o.owner].team == me.team) {
                continue;
            }
            if (o.art == kTypeSevenTile) {
                tx = o.x;
                ty = o.y;
                GameDebugLog(kAttackCatapultText);
                kind = 6;
                target = number;
            } else if ((o.art == kTypeShip2b || o.art == kTypeShip) && o.job != 0x145) {
                kind = 7;
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
    if (me.race != 3) {
        if (Call<kTileDistance>(world, s.x, s.y, tx, ty) < 10) {
            return false;
        }
    } else if (Call<kTileDistance>(world, s.x, s.y, tx, ty) < 3) {
        return false;
    }
    if (me.race != 3) {
        --s.arrows;
    }
    std::uint32_t delay = 0;
    auto roll = [&]() { return static_cast<std::uint32_t>(Call<kRandom>(world) & 0xffff); };
    if (me.race == 0) {
        delay = roll() > 0x7d00 ? ((roll() * 0x3c) >> 16) + 0x3c : 0;
    }
    if (me.race == 1) {
        delay = roll() > 0x4e20 ? ((roll() * 0x50) >> 16) + 0x50 : 0;
    }
    if (me.race == 2) {
        delay = roll() > 0x3a98 ? ((roll() * 0x64) >> 16) + 0x64 : 0;
    }
    if (me.race == 3) {
        delay = roll() > 0x4e20 ? world->buildings[lastBuilding].hit_points + 1u : 0;
    }
    if (delay == 0) {
        // Aim a little off.
        const std::uint32_t k = ((roll() * 0x3f) >> 16) + 0x24;
        const layout::MapOffset& off = world->offsets[k];
        if (Call<kIsOnMap>(world, tx + static_cast<std::uint32_t>(off.dx), ty + static_cast<std::uint32_t>(off.dy)) !=
            0) {
            tx += static_cast<std::uint32_t>(world->offsets[k].dx);
            ty += static_cast<std::uint32_t>(world->offsets[k].dy);
        }
    }
    s.counter = static_cast<std::uint16_t>(
        RawAtAddress<std::uint16_t>(kRaceShotTime + static_cast<std::uint32_t>(me.race) * 4u) - 1u);
    s.direction = CallStd<kDirectionFromDelta>(tx - s.x, ty - s.y) + 0x8000;
    s.job = 0x151;
    if (me.race == 3) {
        s.arrows = 6;
    }
    const std::uint32_t x = s.x, y = s.y;
    const std::uint32_t targetScreenY = Call<kTileScreenY>(world, tx, ty);
    const std::uint32_t targetScreenX = Call<kTileScreenX>(world, tx, ty);
    const std::uint32_t screenY = Call<kTileScreenY>(world, x, y);
    const std::uint32_t screenX = Call<kTileScreenX>(world, s.x, s.y);
    const std::uint32_t projectile =
        Call<kSpawnProjectile>(&world->map_effects, RawAtAddress<std::uint32_t>(kGameTick),
                               static_cast<std::uint32_t>(me.race), screenX, screenY, x, y, targetScreenX,
                               targetScreenY, tx, ty);
    Call<kAllocTable36700Record>(world, kind, projectile, target, delay, 0u, 0u, 0u, 0u, 0u, 0u);
    return true;
}

// A settler (state 1) at the shore looks for a friendly ship within
// radius 15 with room, and a shore tile (terrain 0x30, same walk region)
// next to it: there it boards, otherwise it walks there. Returns whether
// it found one.
bool BoardShip(Walk& w) {
    GameWorld* world = w.world;
    Settler& s = w.s;
    bool found = false;
    std::uint32_t ship = 0, bx = 0, by = 0, side = 0;
    for (std::uint32_t i = 0; world->offsets[i].distance < 15; ++i) {
        if (found) {
            break;
        }
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
        if (boat.art != kTypeShip || boat.job == 0x145 ||
            world->players[boat.owner].team != world->players[s.owner].team) {
            continue;
        }
        const std::uint8_t water = TileAt(world, boat.x, boat.y).terrain;
        if (water != 5 && water != 6) {
            continue;
        }
        if (static_cast<std::int32_t>(boat.passengers) >= kRaceShipCapacity[world->players[boat.owner].race]) {
            continue;
        }
        std::uint32_t xs[6], ys[6];
        for (int d = 0; d < 6; ++d) {
            xs[d] = boat.x;
            ys[d] = boat.y;
        }
        for (std::uint32_t ring = 0; ring < 8 && !found; ++ring) {
            for (std::uint32_t d = 0; d < 6; ++d) {
                xs[d] = NeighbourXOf(xs[d], d);
                ys[d] = NeighbourYOf(ys[d], d);
                if (xs[d] < world->map_size && ys[d] < world->map_size) {
                    const Tile& shore = TileAt(world, xs[d], ys[d]);
                    if (shore.terrain == 0x30 && shore.walk_region == TileAt(world, s.x, s.y).walk_region) {
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
        SetJob(s, 0x35);
        return true;
    }
    // Board: leave the player's idle list, join the ship's passenger list.
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
    SetJob(s, 0x146);
    return true;
}

// Jobs 0x35 and 0xb0 at their target: soldiers' states (+0x1a), then the
// types 0x28/0x16 and the path cache.
std::uint32_t ArriveWaiting(Walk& w) {
    GameWorld* world = w.world;
    Settler& s = w.s;
    bool handled = false;
    const std::uint8_t state = s.walk_state;
    if (state == 5 && s.art == 0x17) {
        SetJob(s, 0x37);
        w.again = 1;
        handled = true;
    } else if (state == 4 && s.art == 0x17) {
        Call<kSetSettlerTarget>(world, w.n, static_cast<std::uint32_t>(s.path_cache[0]),
                                static_cast<std::uint32_t>(s.path_cache[1]));
        s.counter = 0;
        s.job = 0x35;
        s.walk_state = 5;
        handled = true;
    } else if (state == 3 && s.art == 0x17) {
        SetJob(s, 0x152);
        handled = true;
    } else if (state == 2 && s.arrows != 0 && s.path_cache[4] == kNearMark &&
               Call<kTileDistance>(world, s.x, s.y, s.path_cache[2], s.path_cache[3]) < s.path_cache[5]) {
        handled = SoldierShoot(w);
    } else if (state == 1) {
        static constexpr std::uint8_t kLandTypes[] = {5,    8,    0xf,  0x20, 0x1e, 0x1f, 0x23,
                                                      0x21, 0x22, 0x16, 0x28, 0x17, 0x24};
        bool land = false;
        for (std::uint8_t type : kLandTypes) {
            land = land || s.art == type;
        }
        if (land) {
            handled = BoardShip(w);
            if (!handled) {
                s.walk_state = 0;
            }
        }
    }
    if (s.art == 0x28 && s.saved_animation != 0xffff) {
        Tile& t = TileAt(world, s.x, s.y);
        const std::uint8_t flags = t.flags;
        if ((flags & 0x4d) == 0 && (t.owner == 0xff || t.owner != s.owner)) {
            s.job = 0x12e;
            Call<kSetSettlerAnimation>(world, w.n, 0x10fu);
            handled = true;
        } else {
            t.flags = static_cast<std::uint8_t>(flags & 0xfd);
        }
    }
    if (s.art == 0x16 && s.saved_animation != 0xffff) {
        const std::uint32_t at = TileOffset(s.x, s.y);
        Tile& t = TileAtOffset(world, at);
        const std::uint8_t flags = t.flags;
        if ((flags & 0xd) == 0 && (t.terrain & 0xf0) == 0x20 && t.object == 0 && TileNear(world, at, -1, 0).object == 0 &&
            TileNear(world, at, 1, 0).object == 0 && TileNear(world, at, 0, -1).object == 0 &&
            TileNear(world, at, 0, 1).object == 0 && TileNear(world, at, -1, -1).object == 0 &&
            TileNear(world, at, 1, 1).object == 0) {
            s.job = 0xd4;
            Call<kSetSettlerAnimation>(world, w.n, 0xd4u);
            return w.again;
        }
        t.flags = static_cast<std::uint8_t>(flags & 0xfd);
    }
    if (handled) {
        return w.again;
    }
    if (s.path_cache[0] != kNoWaypoint) {
        s.target_x = s.path_cache[0];
        if (s.path_cache[4] == kPatrolMark) {
            s.target_y = s.path_cache[1];
            s.path_cache[0] = s.path_cache[2];
            s.path_cache[1] = s.path_cache[3];
            s.path_cache[2] = s.target_x;
            s.path_cache[3] = s.target_y;
        } else {
            PopWaypoint(s);
        }
        s.counter = 0;
        s.job = 0x35;
        s.walk_state = 0;
        return w.again;
    }
    SetJob(s, 0x37);
    s.frame = 0;
    return 1;
}

// Job 0xab: a soldier reaches the area it was sent to.
std::uint32_t ArriveSoldierAtArea(Walk& w) {
    GameWorld* world = w.world;
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
            const std::uint32_t colour = kPlayerColours[s.owner];
            const std::uint32_t screenY = Call<kTileScreenY>(world, x, y);
            const std::uint32_t screenX = Call<kTileScreenX>(world, s.x, s.y);
            Call<kSpawnDeathEffect>(&world->map_effects, RawAtAddress<std::uint32_t>(kGameTick), screenX, screenY,
                                    x, y, colour, raceWord);
            s.owner = 0xff;
            TileAt(world, s.x, s.y).settler = 0;
            s.counter = 0;
            return w.again;
        }
        s.health = static_cast<std::uint8_t>(s.health - 2);
    }
    SetJob(s, 0xac);
    return w.again;
}

// Ships' waypoint jobs: the next waypoint, unless the cache is empty or
// it lies in another region/area (then `stop` becomes the job).
template <typename Same>
std::uint32_t ArriveShipLeg(Walk& w, std::uint16_t stop, Same same) {
    Settler& s = w.s;
    if (s.path_cache[0] == kNoWaypoint) {
        s.job = stop;
    } else {
        PopWaypoint(s);
        if (!same(TileAt(w.world, s.x, s.y), TileAt(w.world, s.target_x, s.target_y))) {
            s.job = stop;
        }
    }
    s.counter = 0;
    return w.again;
}

std::uint32_t MoveIntoBuilding(Walk& w, std::uint16_t job, std::uint32_t animation) {
    Call<kMoveSettlerToBuildingTileA>(w.world, w.n);
    w.s.job = job;
    Call<kSetSettlerAnimation>(w.world, w.n, animation);
    return w.again;
}

std::uint32_t Arrive(Walk& w) {
    Settler& s = w.s;
    auto next = [&](std::uint16_t job) {
        SetJob(s, job);
        return w.again;
    };
    auto sameRegion = [](const Tile& a, const Tile& b) { return a.walk_region == b.walk_region; };
    auto sameArea = [](const Tile& a, const Tile& b) { return a.area == b.area; };
    switch (s.job) {
    case 0x002:
        SetJob(s, 1);
        return 1;
    case 0x003: return next(0x004);
    case 0x006: return next(0x007);
    case 0x00a: return next(0x00b);
    case 0x00c: return next(0x00d);
    case 0x00f: return next(0x00e);
    case 0x011: return next(0x012);
    case 0x01c: return next(0x020);
    case 0x01d: return next(0x022);
    case 0x02a: return next(0x024);
    case 0x02b: return next(0x027);
    case 0x02d: return next(0x02e);
    case 0x030: return MoveIntoBuilding(w, 0x065, 0x1b);
    case 0x035:
    case 0x0b0: return ArriveWaiting(w);
    case 0x04c: return next(0x04d);
    case 0x04e: return next(0x04f);
    case 0x068: return next(0x069);
    case 0x090: return next(0x091);
    case 0x093: return MoveIntoBuilding(w, 0x094, 0x9d);
    case 0x098: return next(0x099);
    case 0x09c: return next(0x096);
    case 0x0ab: return ArriveSoldierAtArea(w);
    case 0x0b2: return next(0x0b3);
    case 0x0b5: return next(0x0b6);
    case 0x0bb: return next(0x0bd);
    case 0x0bc: return next(0x0bf);
    case 0x0e5: return next(0x0e6);
    case 0x0e8: return MoveIntoBuilding(w, 0x0e9, 0xe1);
    case 0x0ed: return next(0x0ee);
    case 0x0f1: return next(0x0eb);
    case 0x0f6: return next(0x0f7);
    case 0x0f9: return MoveIntoBuilding(w, 0x0fa, 0xea);
    case 0x0fe: return next(0x0ff);
    case 0x102: return next(0x0fc);
    case 0x133: return next(0x134);
    case 0x135: return ArriveShipLeg(w, 0x136, sameRegion);
    case 0x140: return next(0x143);
    case 0x141: return MoveIntoBuilding(w, 0x142, 0x114);
    case 0x146:
        s.counter = 0;
        return w.again;
    case 0x147: return next(0x14b);
    case 0x148: return ArriveShipLeg(w, 0x14c, sameArea);
    case 0x149: return ArriveShipLeg(w, 0, sameArea);
    case 0x14a: return ArriveShipLeg(w, 0, sameRegion);
    case 0x165: return next(0x166);
    case 0x168: return MoveIntoBuilding(w, 0x161, 0x1fd);
    case 0x16a: return next(0x16b);
    case 0x16d: return next(0x16e);
    default: return w.again;
    }
}

}  // namespace

// Purpose: One step of a walking settler: it moves a tile towards its target (finding the way,
// waiting for or swapping with settlers in the way), or, when it has arrived, goes on with the
// next part of its job (soldiers look for targets to shoot at, settlers board ships, ships follow
// their waypoints).
std::uint32_t __fastcall GameWorld_WalkSettler(GameWorld* world, void*, std::uint32_t settler) {
    Settler& s = world->settlers[settler];
    Walk w{world, settler, s, 0};
    const std::uint32_t frames = s.art == kTypeSevenTile ? 0x24u : 0xcu;
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

using WalkFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t settler);

S3RE_HOOK(0x00500860, GameWorld_WalkSettler, WalkFn, Original_GameWorld_WalkSettler, GameWorld_WalkSettler);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestMapSize = 64;
constexpr std::size_t kMapWorld = offsetof(GameWorld, tiles) + kTestMapSize * kTileRowBytes;

std::uint32_t Relative(std::uint32_t value, GameWorld* world) {
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
        worlds.Both([&](GameWorld* w) { w->tiles[x][y].traffic = traffic; });
        for (std::int32_t dx = -1; dx <= 1; ++dx) {
            for (std::int32_t dy = -1; dy <= 1; ++dy) {
                const std::uint8_t terrain = kTerrains[ctx.rng().Below(7)];
                worlds.Both([&](GameWorld* w) { w->tiles[x + dx][y + dy].terrain = terrain; });
            }
        }
        const std::uint32_t expected =
            Relative(Original_GameWorld_AddTileTraffic(worlds.original(), nullptr, x, y), worlds.original());
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
std::uint32_t __fastcall WrongAddTileTraffic(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    Tile& tile = world->tiles[x][y];
    if (tile.traffic == 11) {
        ++tile.traffic;
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&tile.traffic));
    }
    return GameWorld_AddTileTraffic(world, nullptr, x, y);
}

// Wrong: forgets the idle job 0x37.
std::uint32_t __fastcall WrongIsStationaryJob(GameWorld* world, void*, std::uint32_t job) {
    return job == 0x37 ? 1 : GameWorld_IsStationaryJob(world, nullptr, job);
}

}  // namespace

S3RE_TEST(GameWorld_AddTileTraffic_MatchesOriginal) {
    CompareAddTileTraffic(ctx, &GameWorld_AddTileTraffic, 20000);
}
S3RE_TEST(GameWorld_AddTileTraffic_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareAddTileTraffic(probe, &WrongAddTileTraffic, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_IsWalkingJob_MatchesOriginal) {
    CompareJobFn(ctx, Original_GameWorld_IsWalkingJob, &GameWorld_IsWalkingJob);
}
S3RE_TEST(GameWorld_IsStationaryJob_MatchesOriginal) {
    CompareJobFn(ctx, Original_GameWorld_IsStationaryJob, &GameWorld_IsStationaryJob);
}
S3RE_TEST(GameWorld_IsStationaryJob_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareJobFn(probe, Original_GameWorld_IsStationaryJob, &WrongIsStationaryJob);
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

constexpr std::uint16_t kWalkJobs[] = {
    0x002, 0x003, 0x006, 0x00a, 0x00c, 0x00f, 0x011, 0x01c, 0x01d, 0x02a, 0x02b, 0x02d, 0x030, 0x035,
    0x04c, 0x04e, 0x068, 0x090, 0x093, 0x098, 0x09c, 0x0ab, 0x0b0, 0x0b2, 0x0b5, 0x0bb, 0x0bc, 0x0e5,
    0x0e8, 0x0ed, 0x0f1, 0x0f6, 0x0f9, 0x0fe, 0x102, 0x133, 0x135, 0x140, 0x141, 0x146, 0x147, 0x148,
    0x149, 0x14a, 0x165, 0x168, 0x16a, 0x16d, 0x001, 0x037, 0x0ac, 0x004, 0x150};
constexpr std::uint8_t kWalkTypes[] = {0x29, 0x2c, 0x2b, 0x17, 0x28, 0x16, 0x05, 0x08, 0x0f,
                                       0x24, 0x00, 0x01, 0x20, 0x13, 0x2c, 0x2b};
constexpr std::uint8_t kWalkTerrains[] = {0x10, 0x30, 0x05, 0x06, 0x20, 0x21, 0x2f, 0x00, 0x12, 0x1c, 0x40, 0x08};

// FindPath results: directions, waits, swaps, "no way" and odd values.
std::uint32_t PathResult(test::Rng& rng) {
    switch (rng.Below(10)) {
    case 0:
    case 1:
    case 2: return rng.Below(6);
    case 3: return rng.Below(8);
    case 4: return 8 | rng.Below(8);
    case 5: return 0x10 | rng.Below(8);
    case 6: return 0xff;
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
                worlds.Both([&](GameWorld* w) {
                    w->offsets[k].dx = dx;
                    w->offsets[k].dy = dy;
                    w->offsets[k].distance = r;
                });
                ++k;
            }
        }
    }
    worlds.Both([&](GameWorld* w) { w->offsets[k].distance = 1000; });
}

// One-time setup: every record a walk can reach has sane numbers and
// coordinates (random words would index far outside the world).
void SanitizeWalkWorld(test::Context& ctx, test::WorldPair& worlds) {
    auto& rng = ctx.rng();
    FillOffsetRings(worlds);
    worlds.Both([&](GameWorld* w) { w->map_size = kWalkMapSize; });
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
            worlds.Both([&](GameWorld* w) {
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
        const std::uint8_t art = kWalkTypes[rng.Below(sizeof(kWalkTypes))];
        const auto job = static_cast<std::uint16_t>(rng.Chance(1, 4) ? 0xae : kWalkJobs[rng.Below(sizeof(kWalkJobs) / 2)]);
        const auto part = static_cast<std::uint16_t>(rng.Below(kWalkSettlers + 1));
        const auto next = static_cast<std::uint16_t>(rng.Below(kWalkSettlers + 1));
        const auto prev = static_cast<std::uint16_t>(rng.Below(kWalkSettlers + 1));
        const auto slot = static_cast<std::uint8_t>(rng.Below(8));
        worlds.Both([&](GameWorld* w) {
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
        worlds.Both([&](GameWorld* w) {
            layout::Building& r = w->buildings[b];
            r.owner = owner;
            r.x = x;
            r.y = y;
            r.art = art;
        });
    }
    for (std::uint32_t a = 0; a < 0x1000; ++a) {
        const auto player = static_cast<std::uint8_t>(rng.Below(5));
        worlds.Both([&](GameWorld* w) { w->areas[a].player = player; });
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
    const std::uint8_t art = kWalkTypes[rng.Below(sizeof(kWalkTypes))];
    const std::uint32_t jobPick = rng.Below(10);
    const auto job = static_cast<std::uint16_t>(jobPick < 3   ? (rng.Chance(1, 2) ? 0x35 : 0xb0)
                                                : jobPick < 4 ? 0x147 + rng.Below(4)
                                                : jobPick < 5 ? (rng.Chance(1, 2) ? 0x135 : 0xab)
                                                              : kWalkJobs[rng.Below(sizeof(kWalkJobs) / 2)]);
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
    worlds.Both([&](GameWorld* w) {
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
    test::WorldPair worlds(ctx, sizeof(GameWorld));
    SanitizeWalkWorld(ctx, worlds);
    test::CalleeStubs stubs;
    WalkCase current{};
    std::uint32_t pathCalls = 0;
    stubs.Install<1, kFindPath>([&](GameWorld*, std::uint32_t, const std::uint32_t*) {
        const std::uint32_t result = current.paths[pathCalls % 2];
        ++pathCalls;
        return result;
    });
    stubs.Install<1, kResetSettlerToIdle>();
    stubs.Install<10, kSpawnProjectile>([](GameWorld*, std::uint32_t, const std::uint32_t* args) {
        return args[0] ^ 0x5a5a;  // a "projectile" number
    });
    stubs.Install<7, kSpawnDeathEffect>();
    stubs.Install<10, kAllocTable36700Record>();
    stubs.Install<1, kRemoveSettlerFromLists>();
    stubs.Install<1, kMoveSettlerToBuildingTileA>();
    stubs.Install<2, kSetSettlerAnimation>();
    static_assert(sizeof(kStubbed) / sizeof(kStubbed[0]) == 8);
    DebugLogRecorder logs;
    const std::uint32_t savedTick = RawAtAddress<std::uint32_t>(kGameTick);
    struct RestoreTick {
        std::uint32_t value;
        ~RestoreTick() { RawAtAddress<std::uint32_t>(kGameTick) = value; }
    } restoreTick{savedTick};
    for (int i = 0; i < cases; ++i) {
        current = SetUpWalkCase(ctx, worlds);
        const std::uint32_t tick = ctx.rng().Next32();
        RawAtAddress<std::uint32_t>(kGameTick) = tick;
        stubs.SetWorld(worlds.original());
        pathCalls = 0;
        const std::uint32_t expected = Original_GameWorld_WalkSettler(worlds.original(), nullptr, current.n);
        const std::vector<test::StubCall> expectedCalls = stubs.Take();
        const std::vector<DebugLogCall> expectedLogs = logs.Take();
        stubs.SetWorld(worlds.ours());
        pathCalls = 0;
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, current.n);
        const std::vector<test::StubCall> actualCalls = stubs.Take();
        const std::vector<DebugLogCall> actualLogs = logs.Take();
        const bool settlerSame =
            worlds.SameField(ctx, [&](GameWorld* w) -> auto& { return w->settlers[current.n]; });
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
        worlds.Both([&](GameWorld* w) {
            if (w->settlers[current.n].owner == 0xff) {
                w->settlers[current.n].owner = 0;
            }
        });
    }
}

// Wrong: a shot faces one direction off.
std::uint32_t __fastcall WrongWalkSettlerShot(GameWorld* world, void* edx, std::uint32_t settler) {
    const std::uint32_t result = GameWorld_WalkSettler(world, edx, settler);
    if (world->settlers[settler].job == 0x151) {
        world->settlers[settler].direction ^= 1;
    }
    return result;
}

// Wrong: ships count disagreements in the low bits.
std::uint32_t __fastcall WrongWalkSettlerShip(GameWorld* world, void* edx, std::uint32_t settler) {
    const bool ship = world->settlers[settler].art == kTypeShip &&
                      (world->settlers[settler].x != world->settlers[settler].target_x ||
                       world->settlers[settler].y != world->settlers[settler].target_y);
    const std::uint32_t result = GameWorld_WalkSettler(world, edx, settler);
    if (ship) {
        world->settlers[settler].ship_walk ^= 1;
    }
    return result;
}

}  // namespace

S3RE_TEST(GameWorld_WalkSettler_MatchesOriginal) {
    CompareWalkSettler(ctx, &GameWorld_WalkSettler, 40000);
}
S3RE_TEST(GameWorld_WalkSettler_ComparisonDetectsWrongShot) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareWalkSettler(probe, &WrongWalkSettlerShot, 20000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_WalkSettler_ComparisonDetectsWrongShip) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareWalkSettler(probe, &WrongWalkSettlerShip, 4000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
