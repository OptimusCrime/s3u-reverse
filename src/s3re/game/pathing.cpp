// The pathfinder (E:\Siedler3\Dirk\Pathing.cpp, the "Pathing" source file;
// 0x4a1ee0..0x4a4b20). Pathing::FindPath (0x4a1ee0) is called by
// GameWorld::WalkSettler for one settler and returns the direction of its
// next step; it keeps its state in a layout::PathState (pathing.h) that it
// passes to the helpers below. Every helper is thiscall (ECX = world) unless
// noted; "state" is the PathState pointer, the first stack argument.
// Assertions call Win32::ThrowAssertmentFailed (0x6119b0) with the file name
// 0x69f9dc and never return.
//
// Pathing::TileCost (0x004a36a0): (state, x, y, needEmpty, withCost,
//   blockBusy; the last three are bytes) ret 0x18. EAX = 0x10000 (blocked)
//   if the tile is outside the state's region (sector type 1: Tile::area ==
//   state.area, 2: Tile::walk_region == state.walk_region, 3:
//   Tile::wide_region == state.wide_region; compared as 32-bit values), or
//   needEmpty and a settler stands there, or blockBusy and a settler stands
//   there whose job IsStationaryJob (0x4fa880). Else withCost ? the terrain
//   cost (signed byte table 0x65857c by Tile::terrain) : 0. Another sector
//   type: assertion "Unsupported SectorType", line 0x409.
// Pathing::SectorTileCost (0x004a3870): (state, x, y) ret 0xc: the region
//   test of TileCost only; EAX = terrain cost or 0x10000. Assertion line
//   0x426.
// Pathing::FreeTileCost (0x004a3940): (state, x, y) ret 0xc: 0x10000 if a
//   settler stands there or the region differs, else the terrain cost.
//   Assertion line 0x449 (before anything is read).
// Pathing::DirectionOfDelta (0x004a3a40): (dx, dy) ret 8, ECX unused: the
//   direction 0..5 whose sextant holds (dx, dy) (pathing.h numbering): dx > 0,
//   dy >= 0: 0 if dx > dy else 1; dx <= 0, dy > 0: 2; dx < 0, dy <= 0: 3 if
//   dx < dy else 4; dx >= 0, dy < 0: 5. (0, 0): assertion "Das solltest du
//   schon schaffen" (you should be able to manage that), line 0x493.
// Pathing::DirectionOfStep (0x004a4b00): (dx, dy) ret 8, ECX unused: the
//   direction of a unit step (each -1..1): (dy != 0) + (dx == 0 ? 1 + (dy ==
//   -1 ? 3 : 0) : dx == -1 ? 3 : 0). No check of the input.
// Pathing::DistanceAlong (0x004a4860, the CSV's "AreaDirection"): (state, a,
//   b) ret 0xc, ECX unused: by state.sextant (jump table 0x4a48d0): 0, 1:
//   max(a, b); 2: b - a; 3, 4: max(-a, -b); 5: a - b. Other values:
//   assertion "Invalid area", line 0x716 (EAX would be a).
// Pathing::Angle (0x004a49d0): (dx, dy) ret 8: a pseudo angle of (dx, dy)
//   in 0..0x80000 (0x20000 per quadrant) from the reciprocal table at world
//   +0x1539528 (0x10000 / i): quadrant base plus or minus min/max *
//   reciprocal[max]; see the code. 32-bit wrapping products. (0, 0):
//   assertion "Invalid angle selection", line 0x76c.
// Pathing::TrackAngle (0x004a48f0): (state, dx, dy, int* last, int* turns)
//   ret 0x14, ECX = world (passed on to Angle): a = Angle(dx, dy) -
//   state.angle_base, + 0x80000 if negative; if *last != 0x7fffffff and the
//   angle crossed the 0 line (last < 0x30000 and a > 0x50000: *turns -=
//   0x80000; last > 0x50000 and a < 0x30000: *turns += 0x80000); *last = a;
//   EAX = *turns + a: the angle accumulated over whole turns. The original
//   wraps it in try/catch(...) whose handler logs "PATH: %i %i %i %i %i"
//   (DebugLog, silent in the release build) and rethrows; ours has no
//   handler, so only that log line is missing when Angle asserts.
// Pathing::ChooseSextant (0x004a23e0): (state) ret 4: from state.dx/dy:
//   the sextant (DirectionOfDelta's numbering), its axes A = that direction
//   and B = the next one (axis_a/b_x/y), the components along_a/along_b
//   ((dx, dy) = along_a * A + along_b * B), state.turn advanced by 5, 4, 3, 2,
//   1, 0 for sextants 0..5 (each +1 wraps to 0 only at exactly 6), state
//   .sextant = the sextant. EAX = the sextant | 0x20 when the target is
//   near: sextants 0, 1: dx < 3 and dy < 3; 2: dy - dx < 3; 3, 4: abs_dx < 3
//   or abs_dy < 3 (the caller's fields); 5: dx - dy < 3. (0, 0): assertion
//   line 0x140 (same text as DirectionOfDelta). Phases 3 -> 1.
// Pathing::SetupLineStep (0x004a3d30): (state) ret 4, stdcall (ECX not
//   used): with a = along_a, b = along_b: if a > b: f = (b << 16) / a and
//   step_a = 0x10000 * axis A, step_b = f * axis B; else f = (a << 16) / b,
//   step_a = 0x10000 * axis B, step_b = f * axis A (x and y each, 32-bit
//   products; signed division, b = 0 there divides by zero like the
//   original); unk94 = 0. Phases 0x64 -> 0xa; EAX = the final marker.
//
// Phase marker (kPathPhaseAddress, see pathing.h): written on entry and exit
// of the helpers that do so in the original, with the same three stores.

#include "game/pathing.h"

#include "game/globals.h"
#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace s3re::game {

using layout::PathState;

namespace {

using layout::Settler;
using layout::Tile;

// Tables in S3.EXE's read-only data.
const std::int32_t* const kDirX = reinterpret_cast<const std::int32_t*>(0x0065867c);
const std::int32_t* const kDirY = reinterpret_cast<const std::int32_t*>(0x00658694);
const std::int8_t* const kTerrainCost = reinterpret_cast<const std::int8_t*>(0x0065857c);

// Assertion texts in S3.EXE.
const char* const kPathingFile = reinterpret_cast<const char*>(0x0069f9dc);  // E:\Siedler3\Dirk\Pathing.cpp
const char* const kTextShouldManage = reinterpret_cast<const char*>(0x0069f9bc);  // "Das solltest du schon schaffen"
const char* const kTextSectorType = reinterpret_cast<const char*>(0x0069f9fc);   // "Unsupported SectorType"
const char* const kTextInvalidArea = reinterpret_cast<const char*>(0x0069fb04);  // "Invalid area"
const char* const kTextInvalidAngle = reinterpret_cast<const char*>(0x0069fb2c);  // "Invalid angle selection"

constexpr std::int32_t kBlocked = 0x10000;

// Returns the final value (some helpers leave it in EAX).
std::uint32_t SetPathPhase(const PathState& state, std::uint32_t phase) {
    volatile std::uint32_t& marker = *reinterpret_cast<volatile std::uint32_t*>(kPathPhaseAddress);
    marker = phase;
    const std::uint32_t withType = (state.sector_type << 8) | phase;
    marker = withType;
    const std::uint32_t value = withType | (state.area << 16);
    marker = value;
    return value;
}

Tile& TileAt(GameWorld* world, std::int32_t x, std::int32_t y) {
    return TileAtOffset(world, TileOffset(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)));
}

// The settler record `number` (a 16-bit tile field, so it may lie past the
// 32000 records like in the original).
Settler& SettlerAt(GameWorld* world, std::uint32_t number) {
    return *(world->settlers + number);
}

bool IsValidSectorType(const PathState& state) {
    return state.sector_type == 1 || state.sector_type == 2 || state.sector_type == 3;
}

// The region test; the sector type must be valid.
bool InSector(const PathState& state, const Tile& tile) {
    switch (state.sector_type) {
        case 2:
            return tile.walk_region == state.walk_region;
        case 1:
            return tile.area == state.area;
        default:
            return tile.wide_region == state.wide_region;
    }
}

std::int32_t TerrainCost(const Tile& tile) {
    return kTerrainCost[tile.terrain];
}

std::int32_t Negate(std::int32_t value) {
    return static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(value));
}

std::int32_t Abs(std::int32_t value) {
    return value < 0 ? Negate(value) : value;
}

// world->reciprocal[index] with the original's 32-bit address arithmetic
// (the index comes from |dx|, |dy| and is not checked).
std::int32_t Reciprocal(GameWorld* world, std::int32_t index) {
    const std::uint32_t address = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world)) +
                                  static_cast<std::uint32_t>(offsetof(GameWorld, reciprocal)) +
                                  static_cast<std::uint32_t>(index) * 4u;
    return *reinterpret_cast<const std::int32_t*>(static_cast<std::uintptr_t>(address));
}

std::int32_t Mul(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

}  // namespace

using TileCostFn = std::int32_t(__fastcall*)(GameWorld* world, void* edx, PathState* state, std::int32_t x,
                                             std::int32_t y, std::uint32_t needEmpty, std::uint32_t withCost,
                                             std::uint32_t blockBusy);
using StateTileFn = std::int32_t(__fastcall*)(GameWorld* world, void* edx, PathState* state, std::int32_t x,
                                              std::int32_t y);
using DeltaFn = std::int32_t(__fastcall*)(GameWorld* world, void* edx, std::int32_t dx, std::int32_t dy);
using StateFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, PathState* state);
using TrackAngleFn = std::int32_t(__fastcall*)(GameWorld* world, void* edx, PathState* state, std::int32_t dx,
                                               std::int32_t dy, std::int32_t* last, std::int32_t* turns);

// Purpose: The pathfinder's test of one tile: whether a settler may step there (same region, maybe
// free of others) and, if wanted, how expensive the tile's terrain is to walk on.
std::int32_t __fastcall Pathing_TileCost(GameWorld* world, void*, PathState* state, std::int32_t x, std::int32_t y,
                                         std::uint32_t needEmpty, std::uint32_t withCost, std::uint32_t blockBusy) {
    if (!IsValidSectorType(*state)) {
        Win32_ThrowAssertmentFailed(kTextSectorType, kPathingFile, 0x409);
        return kBlocked;
    }
    const Tile& tile = TileAt(world, x, y);
    if (!InSector(*state, tile)) {
        return kBlocked;
    }
    if (static_cast<std::uint8_t>(needEmpty) != 0 && tile.settler != 0) {
        return kBlocked;
    }
    if (static_cast<std::uint8_t>(blockBusy) != 0 && tile.settler != 0 &&
        static_cast<std::uint8_t>(GameWorld_IsStationaryJob(world, nullptr, SettlerAt(world, tile.settler).job)) != 0) {
        return kBlocked;
    }
    if (static_cast<std::uint8_t>(withCost) == 0) {
        return 0;
    }
    return TerrainCost(tile);
}

// Purpose: The terrain cost of a tile for the pathfinder, or "blocked" when the tile lies outside the
// region the settler must stay in.
std::int32_t __fastcall Pathing_SectorTileCost(GameWorld* world, void*, PathState* state, std::int32_t x,
                                               std::int32_t y) {
    if (!IsValidSectorType(*state)) {
        Win32_ThrowAssertmentFailed(kTextSectorType, kPathingFile, 0x426);
        return kBlocked;
    }
    const Tile& tile = TileAt(world, x, y);
    return InSector(*state, tile) ? TerrainCost(tile) : kBlocked;
}

// Purpose: Like SectorTileCost, but a tile with a settler on it also counts as blocked.
std::int32_t __fastcall Pathing_FreeTileCost(GameWorld* world, void*, PathState* state, std::int32_t x,
                                             std::int32_t y) {
    if (!IsValidSectorType(*state)) {
        Win32_ThrowAssertmentFailed(kTextSectorType, kPathingFile, 0x449);
        return kBlocked;
    }
    const Tile& tile = TileAt(world, x, y);
    if (tile.settler != 0 || !InSector(*state, tile)) {
        return kBlocked;
    }
    return TerrainCost(tile);
}

// Purpose: The hexagonal direction (0..5) in which an offset (dx, dy) lies.
std::int32_t __fastcall Pathing_DirectionOfDelta(GameWorld*, void*, std::int32_t dx, std::int32_t dy) {
    if (dx > 0) {
        if (dy >= 0) {
            return dx <= dy ? 1 : 0;
        }
    } else if (dy > 0) {
        return 2;
    }
    if (dx < 0) {
        if (dy <= 0) {
            return dx >= dy ? 4 : 3;
        }
    } else if (dy < 0) {
        return 5;
    }
    Win32_ThrowAssertmentFailed(kTextShouldManage, kPathingFile, 0x493);
    return 0xff;
}

// Purpose: The direction number of a single step to a neighbouring tile.
std::int32_t __fastcall Pathing_DirectionOfStep(GameWorld*, void*, std::int32_t dx, std::int32_t dy) {
    std::int32_t direction = dy != 0 ? 1 : 0;
    if (dx == 0) {
        ++direction;
        if (dy == -1) {
            direction += 3;
        }
    } else if (dx == -1) {
        direction += 3;
    }
    return direction;
}

// Purpose: How far an offset reaches in the direction the pathfinder is currently heading (a hexagonal
// distance measure along the current sextant).
std::int32_t __fastcall Pathing_DistanceAlong(GameWorld*, void*, PathState* state, std::int32_t a, std::int32_t b) {
    switch (static_cast<std::uint32_t>(state->sextant)) {
        case 0:
        case 1:
            return a > b ? a : b;
        case 2:
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(b) - static_cast<std::uint32_t>(a));
        case 3:
        case 4: {
            const std::int32_t na = Negate(a);
            const std::int32_t nb = Negate(b);
            return na > nb ? na : nb;
        }
        case 5:
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
        default:
            Win32_ThrowAssertmentFailed(kTextInvalidArea, kPathingFile, 0x716);
            return a;
    }
}

// Purpose: A cheap angle of the offset (dx, dy) (0..0x80000 for a full turn), used when the pathfinder
// follows a wall around an obstacle.
std::int32_t __fastcall Pathing_Angle(GameWorld* world, void*, std::int32_t dx, std::int32_t dy) {
    const std::int32_t ax = Abs(dx);
    const std::int32_t ay = Abs(dy);
    if (dx >= 0) {
        if (dy < 0) {
            if (ay > ax) {
                return Mul(Reciprocal(world, ay), ax);
            }
            return static_cast<std::int32_t>(0x20000u - static_cast<std::uint32_t>(Mul(Reciprocal(world, ax), ay)));
        }
        if (dx > 0) {
            if (ax > ay) {
                return static_cast<std::int32_t>(static_cast<std::uint32_t>(Mul(Reciprocal(world, ax), ay)) + 0x20000u);
            }
            return static_cast<std::int32_t>(0x40000u - static_cast<std::uint32_t>(Mul(Reciprocal(world, ay), ax)));
        }
    }
    if (dy > 0) {
        if (ay > ax) {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(Mul(Reciprocal(world, ay), ax)) + 0x40000u);
        }
        return static_cast<std::int32_t>(0x60000u - static_cast<std::uint32_t>(Mul(Reciprocal(world, ax), ay)));
    }
    if (dx < 0 && dy <= 0) {
        if (ax > ay) {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(Mul(Reciprocal(world, ax), ay)) + 0x60000u);
        }
        return static_cast<std::int32_t>(0x80000u - static_cast<std::uint32_t>(Mul(Reciprocal(world, ay), ax)));
    }
    Win32_ThrowAssertmentFailed(kTextInvalidAngle, kPathingFile, 0x76c);
    return 0;
}

// Purpose: Follows the angle to a point while walking around an obstacle, counting whole turns, so
// that the pathfinder notices when it has circled the obstacle.
std::int32_t __fastcall Pathing_TrackAngle(GameWorld* world, void*, PathState* state, std::int32_t dx,
                                           std::int32_t dy, std::int32_t* last, std::int32_t* turns) {
    std::int32_t angle = static_cast<std::int32_t>(static_cast<std::uint32_t>(Pathing_Angle(world, nullptr, dx, dy)) -
                                                   static_cast<std::uint32_t>(state->angle_base));
    if (angle < 0) {
        angle = static_cast<std::int32_t>(static_cast<std::uint32_t>(angle) + 0x80000u);
    }
    const std::int32_t previous = *last;
    if (previous != 0x7fffffff) {
        if (previous < 0x30000 && angle > 0x50000) {
            *turns = static_cast<std::int32_t>(static_cast<std::uint32_t>(*turns) - 0x80000u);
        } else if (previous > 0x50000 && angle < 0x30000) {
            *turns = static_cast<std::int32_t>(static_cast<std::uint32_t>(*turns) + 0x80000u);
        }
    }
    *last = angle;
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(*turns) + static_cast<std::uint32_t>(angle));
}

namespace {

void AdvanceTurn(PathState& state, int steps) {
    std::int32_t turn = state.turn;
    for (int i = 0; i < steps; ++i) {
        ++turn;
        if (turn == 6) {
            turn = 0;
        }
    }
    state.turn = turn;
}

void SetAxes(PathState& state, std::int32_t ax, std::int32_t bx, std::int32_t ay, std::int32_t by) {
    state.axis_a_x = ax;
    state.axis_b_x = bx;
    state.axis_a_y = ay;
    state.axis_b_y = by;
}

}  // namespace

// Purpose: Splits the way to the target into the two neighbour directions that span it (the sextant),
// and says whether the target is already close.
std::uint32_t __fastcall Pathing_ChooseSextant(GameWorld*, void*, PathState* state) {
    PathState& s = *state;
    SetPathPhase(s, 3);
    const std::int32_t dx = s.dx;
    const std::int32_t dy = s.dy;
    std::uint32_t result;
    auto diff = [](std::int32_t a, std::int32_t b) {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
    };
    if (dx > 0 && dy >= 0) {
        if (dx > dy) {
            result = 0;
            s.along_a = dy;
            s.along_b = diff(dx, dy);
            SetAxes(s, 1, 1, 1, 0);
            AdvanceTurn(s, 5);
        } else {
            result = 1;
            s.along_b = dx;
            s.along_a = diff(dy, dx);
            SetAxes(s, 0, 1, 1, 1);
            AdvanceTurn(s, 4);
        }
        if (dx < 3 && dy < 3) {
            result |= 0x20;
        }
    } else if (dx <= 0 && dy > 0) {
        result = 2;
        s.along_a = Negate(dx);
        s.along_b = dy;
        SetAxes(s, -1, 0, 0, 1);
        AdvanceTurn(s, 3);
        if (diff(dy, dx) < 3) {
            result = 0x22;
        }
    } else if (dx < 0 && dy <= 0) {
        if (dx < dy) {
            result = 3;
            s.along_b = diff(dy, dx);
            SetAxes(s, -1, -1, -1, 0);
            s.along_a = Negate(dy);
            AdvanceTurn(s, 2);
        } else {
            result = 4;
            SetAxes(s, 0, -1, -1, -1);
            s.along_b = Negate(dx);
            s.along_a = diff(dx, dy);
            AdvanceTurn(s, 1);
        }
        if (s.abs_dx < 3 || s.abs_dy < 3) {
            result |= 0x20;
        }
    } else if (dx >= 0 && dy < 0) {
        result = 5;
        s.along_a = dx;
        s.along_b = Negate(dy);
        SetAxes(s, 1, 0, 0, -1);
        if (diff(dx, dy) < 3) {
            result = 0x25;
        }
    } else {
        Win32_ThrowAssertmentFailed(kTextShouldManage, kPathingFile, 0x140);
        result = 0xff;
    }
    s.sextant = static_cast<std::int32_t>(result & 0x1f);
    SetPathPhase(s, 1);
    return result;
}

// Purpose: Prepares the fixed-point steps for walking a straight line towards the target inside the
// current sextant.
std::uint32_t __fastcall Pathing_SetupLineStep(GameWorld*, void*, PathState* state) {
    PathState& s = *state;
    SetPathPhase(s, 0x64);
    const std::int32_t a = s.along_a;
    const std::int32_t b = s.along_b;
    std::int32_t majorA, majorB, minorA, minorB;  // weights of axes A and B
    if (a > b) {
        const std::int32_t f = static_cast<std::int32_t>(static_cast<std::uint32_t>(b) << 16) / a;
        majorA = 0x10000;
        majorB = 0;
        minorA = 0;
        minorB = f;
    } else {
        const std::int32_t f = static_cast<std::int32_t>(static_cast<std::uint32_t>(a) << 16) / b;
        majorA = 0;
        majorB = 0x10000;
        minorA = f;
        minorB = 0;
    }
    auto sum = [](std::int32_t p, std::int32_t q) {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(p) + static_cast<std::uint32_t>(q));
    };
    s.step_a_x = sum(Mul(s.axis_a_x, majorA), Mul(s.axis_b_x, majorB));
    s.unk94 = 0;
    s.step_a_y = sum(Mul(s.axis_a_y, majorA), Mul(s.axis_b_y, majorB));
    s.step_b_x = sum(Mul(s.axis_a_x, minorA), Mul(s.axis_b_x, minorB));
    s.step_b_y = sum(Mul(s.axis_a_y, minorA), Mul(s.axis_b_y, minorB));
    return SetPathPhase(s, 0xa);
}

S3RE_HOOK(0x004a36a0, Pathing_TileCost, TileCostFn, Original_Pathing_TileCost, Pathing_TileCost);
S3RE_HOOK(0x004a3870, Pathing_SectorTileCost, StateTileFn, Original_Pathing_SectorTileCost, Pathing_SectorTileCost);
S3RE_HOOK(0x004a3940, Pathing_FreeTileCost, StateTileFn, Original_Pathing_FreeTileCost, Pathing_FreeTileCost);
S3RE_HOOK(0x004a3a40, Pathing_DirectionOfDelta, DeltaFn, Original_Pathing_DirectionOfDelta,
          Pathing_DirectionOfDelta);
S3RE_HOOK(0x004a4b00, Pathing_DirectionOfStep, DeltaFn, Original_Pathing_DirectionOfStep, Pathing_DirectionOfStep);
S3RE_HOOK(0x004a4860, Pathing_DistanceAlong, StateTileFn, Original_Pathing_DistanceAlong, Pathing_DistanceAlong);
S3RE_HOOK(0x004a49d0, Pathing_Angle, DeltaFn, Original_Pathing_Angle, Pathing_Angle);
S3RE_HOOK(0x004a48f0, Pathing_TrackAngle, TrackAngleFn, Original_Pathing_TrackAngle, Pathing_TrackAngle);
S3RE_HOOK(0x004a23e0, Pathing_ChooseSextant, StateFn, Original_Pathing_ChooseSextant, Pathing_ChooseSextant);
S3RE_HOOK(0x004a3d30, Pathing_SetupLineStep, StateFn, Original_Pathing_SetupLineStep, Pathing_SetupLineStep);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// The whole world (settler records and the reciprocal table).
constexpr std::size_t kFullWorld = sizeof(GameWorld);
constexpr std::int32_t kTestMin = 8;
constexpr std::int32_t kTestMax = 760;

std::uint32_t& PathPhase() {
    return GameGlobal<std::uint32_t>(kPathPhaseAddress);
}

// A random state for a position in kTestMin..kTestMax; region values small
// so that the tiles prepared by PrepareTiles match them often.
PathState RandomState(test::Context& ctx) {
    PathState s;
    ctx.rng().Fill(&s, sizeof(s));
    s.x = ctx.rng().Range(kTestMin, kTestMax);
    s.y = ctx.rng().Range(kTestMin, kTestMax);
    s.walk_region = ctx.rng().Chance(1, 20) ? ctx.rng().Next32() : ctx.rng().Below(3);
    s.area = ctx.rng().Chance(1, 20) ? ctx.rng().Next32() : ctx.rng().Below(3);
    s.wide_region = ctx.rng().Chance(1, 20) ? ctx.rng().Next32() : ctx.rng().Below(3);
    s.sector_type = 1 + ctx.rng().Below(3);
    s.sextant = static_cast<std::int32_t>(ctx.rng().Below(6));
    s.turn = ctx.rng().Chance(1, 5) ? 1000 : static_cast<std::int32_t>(ctx.rng().Below(6));
    return s;
}

// Tiles within `radius` of (x, y): regions 0..2, settlers mostly absent,
// some with walking, idle or other jobs.
void PrepareTiles(test::Context& ctx, test::WorldPair& worlds, std::int32_t x, std::int32_t y, std::int32_t radius) {
    static constexpr std::uint16_t kJobs[] = {1, 0x37, 2, 0x16d, 4, 5, 0x100};
    for (std::int32_t dx = -radius; dx <= radius; ++dx) {
        for (std::int32_t dy = -radius; dy <= radius; ++dy) {
            const std::int32_t tx = x + dx;
            const std::int32_t ty = y + dy;
            if (tx < 0 || ty < 0 || tx >= 768 || ty >= 768) {
                continue;
            }
            const auto walk = static_cast<std::uint8_t>(ctx.rng().Below(3) == 0 ? 1 : 0);
            const auto area = static_cast<std::uint16_t>(ctx.rng().Below(3) == 0 ? 1 : 0);
            const auto wide = static_cast<std::uint16_t>(ctx.rng().Below(3) == 0 ? 1 : 0);
            // Settler numbers stay below 32000 (larger ones would lie past the
            // test world's end).
            const std::uint16_t settler =
                ctx.rng().Chance(2, 3) ? 0 : static_cast<std::uint16_t>(1 + ctx.rng().Below(31999));
            const std::uint16_t job = kJobs[ctx.rng().Below(7)];
            worlds.Both([&](GameWorld* w) {
                Tile& tile = w->tiles[tx][ty];
                tile.walk_region = walk;
                tile.area = area;
                tile.wide_region = wide;
                tile.settler = settler;
                if (settler != 0) {
                    SettlerAt(w, settler).job = job;
                }
            });
        }
    }
}

// Runs `call(world, state)` with the original's and our function on equal
// worlds and states; compares EAX, the state, the phase marker and (every
// `checkEvery` cases) the worlds.
template <typename Call>
bool RunPathCase(test::Context& ctx, test::WorldPair& worlds, const PathState& start, Call call, int i,
                 int checkEvery) {
    PathState a = start;
    PathState b = start;
    std::uint32_t& phase = PathPhase();
    const std::uint32_t saved = phase;
    phase = 0x5a5a5a5a;
    const std::int32_t expected = call(true, worlds.original(), &a);
    const std::uint32_t phaseA = phase;
    phase = 0x5a5a5a5a;
    const std::int32_t actual = call(false, worlds.ours(), &b);
    const std::uint32_t phaseB = phase;
    phase = saved;
    bool ok = S3RE_EXPECT_EQ(expected, actual);
    ok = ok && S3RE_EXPECT_EQ(phaseA, phaseB);
    ok = ok && ctx.ExpectMemEq(&a, &b, sizeof(a), "original state", "our state", __FILE__, __LINE__);
    ok = ok && (i % checkEvery != checkEvery - 1 || worlds.Checkpoint(ctx));
    if (!ok) {
        ctx.Fail(__FILE__, __LINE__, "  case %d: position (%d, %d), sector type %u", i, start.x, start.y,
                 start.sector_type);
    }
    return ok;
}

void CompareTileCosts(test::Context& ctx, TileCostFn tileCost, StateTileFn sectorCost, StateTileFn freeCost,
                      int cases) {
    test::WorldPair worlds(ctx, kFullWorld);
    for (int i = 0; i < cases; ++i) {
        const PathState s = RandomState(ctx);
        if (i % 4 == 0) {
            PrepareTiles(ctx, worlds, s.x, s.y, 2);
        }
        const std::int32_t x = s.x + ctx.rng().Range(-2, 2);
        const std::int32_t y = s.y + ctx.rng().Range(-2, 2);
        const std::uint32_t flags = ctx.rng().Next32();
        const int which = static_cast<int>(ctx.rng().Below(3));
        const bool ok = RunPathCase(
            ctx, worlds, s,
            [&](bool original, GameWorld* w, PathState* state) {
                switch (which) {
                    case 0:
                        return (original ? Original_Pathing_TileCost : tileCost)(
                            w, nullptr, state, x, y, flags & 0x1ff, (flags >> 9) & 0x1ff, (flags >> 18) & 0x1ff);
                    case 1:
                        return (original ? Original_Pathing_SectorTileCost : sectorCost)(w, nullptr, state, x, y);
                    default:
                        return (original ? Original_Pathing_FreeTileCost : freeCost)(w, nullptr, state, x, y);
                }
            },
            i, 4096);
        if (!ok) {
            return;
        }
    }
    S3RE_EXPECT(worlds.Checkpoint(ctx));
}

std::int32_t RandomDelta(test::Context& ctx) {
    switch (ctx.rng().Below(4)) {
        case 0:
            return ctx.rng().Range(-3, 3);
        case 1:
            return ctx.rng().Range(-40, 40);
        case 2:
            return ctx.rng().Range(-767, 767);
        default:
            return ctx.rng().Range(-200000, 200000);
    }
}

// Angle's index into the 768-entry reciprocal table is max(|dx|, |dy|).
std::int32_t RandomAngleDelta(test::Context& ctx) {
    return ctx.rng().Chance(1, 2) ? ctx.rng().Range(-40, 40) : ctx.rng().Range(-767, 767);
}

// The direction functions and Angle (no state; (0, 0) asserts and is left
// out).
void CompareDeltaFns(test::Context& ctx, DeltaFn ofDelta, DeltaFn ofStep, DeltaFn angle, int cases) {
    test::WorldBuffer world(kFullWorld);
    ctx.rng().Fill(world.data(), world.size());
    GameWorld* w = world.world();
    for (std::int32_t i = 1; i < 768; ++i) {
        w->reciprocal[i] = 0x10000 / i;
    }
    for (int i = 0; i < cases; ++i) {
        std::int32_t dx = RandomDelta(ctx);
        std::int32_t dy = RandomDelta(ctx);
        if (dx == 0 && dy == 0) {
            dx = 1;
        }
        std::int32_t ax = RandomAngleDelta(ctx);
        const std::int32_t ay = RandomAngleDelta(ctx);
        if (ax == 0 && ay == 0) {
            ax = -1;
        }
        const std::int32_t sx = ctx.rng().Range(-1, 1);
        const std::int32_t sy = ctx.rng().Range(-1, 1);
        const bool ok = S3RE_EXPECT_EQ(Original_Pathing_DirectionOfDelta(w, nullptr, dx, dy), ofDelta(w, nullptr, dx, dy)) &&
                        S3RE_EXPECT_EQ(Original_Pathing_DirectionOfStep(w, nullptr, sx, sy), ofStep(w, nullptr, sx, sy)) &&
                        S3RE_EXPECT_EQ(Original_Pathing_Angle(w, nullptr, ax, ay), angle(w, nullptr, ax, ay));
        if (!ok) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%d, %d), step (%d, %d)", i, dx, dy, sx, sy);
            return;
        }
    }
}

// The functions on a state alone: DistanceAlong, TrackAngle, ChooseSextant,
// SetupLineStep.
void CompareStateFns(test::Context& ctx, StateTileFn distanceAlong, TrackAngleFn trackAngle, StateFn chooseSextant,
                     StateFn setupLineStep, int cases) {
    test::WorldPair worlds(ctx, kFullWorld);
    worlds.Both([](GameWorld* w) {
        for (std::int32_t i = 1; i < 768; ++i) {
            w->reciprocal[i] = 0x10000 / i;
        }
    });
    for (int i = 0; i < cases; ++i) {
        PathState s = RandomState(ctx);
        s.dx = RandomDelta(ctx);
        s.dy = RandomDelta(ctx);
        if (s.dx == 0 && s.dy == 0) {
            s.dy = -1;
        }
        s.abs_dx = ctx.rng().Chance(1, 2) ? Abs(s.dx) : ctx.rng().Range(-1, 5);
        s.abs_dy = ctx.rng().Chance(1, 2) ? Abs(s.dy) : ctx.rng().Range(-1, 5);
        s.along_a = ctx.rng().Range(0, 300);
        s.along_b = ctx.rng().Range(0, 300);
        if (ctx.rng().Chance(1, 4)) {
            s.along_a = s.along_b;
        }
        if (s.along_a == 0 && s.along_b == 0) {
            s.along_b = 1;
        }
        const int which = static_cast<int>(ctx.rng().Below(4));
        s.angle_base = ctx.rng().Chance(1, 2) ? 0 : static_cast<std::int32_t>(ctx.rng().Below(0x80000));
        const bool tracking = which == 1;
        const std::int32_t a = tracking ? RandomAngleDelta(ctx) : RandomDelta(ctx);
        const std::int32_t b = tracking ? RandomAngleDelta(ctx) : RandomDelta(ctx);
        std::int32_t lastStart = ctx.rng().Chance(1, 5) ? 0x7fffffff : static_cast<std::int32_t>(ctx.rng().Below(0x80000));
        std::int32_t turnsStart = static_cast<std::int32_t>(ctx.rng().Below(5)) * 0x80000 - 0x100000;
        std::int32_t lastA = lastStart, lastB = lastStart, turnsA = turnsStart, turnsB = turnsStart;
        const bool ok = RunPathCase(
            ctx, worlds, s,
            [&](bool original, GameWorld* w, PathState* state) -> std::int32_t {
                switch (which) {
                    case 0:
                        return (original ? Original_Pathing_DistanceAlong : distanceAlong)(w, nullptr, state, a, b);
                    case 1:
                        return (original ? Original_Pathing_TrackAngle : trackAngle)(
                            w, nullptr, state, a == 0 && b == 0 ? 1 : a, b, original ? &lastA : &lastB,
                            original ? &turnsA : &turnsB);
                    case 2:
                        return static_cast<std::int32_t>(
                            (original ? Original_Pathing_ChooseSextant : chooseSextant)(w, nullptr, state));
                    default:
                        (original ? Original_Pathing_SetupLineStep : setupLineStep)(w, nullptr, state);
                        return 0;
                }
            },
            i, 100000);
        if (!ok || !S3RE_EXPECT_EQ(lastA, lastB) || !S3RE_EXPECT_EQ(turnsA, turnsB)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, function %d", i, which);
            return;
        }
    }
}

// Wrong: a settler on the tile does not block when needEmpty is set.
std::int32_t __fastcall WrongTileCost(GameWorld* world, void*, PathState* state, std::int32_t x, std::int32_t y,
                                      std::uint32_t, std::uint32_t withCost, std::uint32_t blockBusy) {
    return Pathing_TileCost(world, nullptr, state, x, y, 0, withCost, blockBusy);
}

// Wrong: the boundary between sextants 0 and 1 on the diagonal.
std::int32_t __fastcall WrongDirectionOfDelta(GameWorld* world, void*, std::int32_t dx, std::int32_t dy) {
    if (dx > 0 && dx == dy) {
        return 0;
    }
    return Pathing_DirectionOfDelta(world, nullptr, dx, dy);
}

// Wrong: the turn of sextant 4 is not advanced.
std::uint32_t __fastcall WrongChooseSextant(GameWorld* world, void*, PathState* state) {
    const std::int32_t turn = state->turn;
    const std::uint32_t result = Pathing_ChooseSextant(world, nullptr, state);
    if ((result & 0x1f) == 4) {
        state->turn = turn;
    }
    return result;
}

}  // namespace

S3RE_TEST(Pathing_TileCosts_MatchOriginal) {
    CompareTileCosts(ctx, &Pathing_TileCost, &Pathing_SectorTileCost, &Pathing_FreeTileCost, 60000);
}
S3RE_TEST(Pathing_TileCosts_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareTileCosts(probe, &WrongTileCost, &Pathing_SectorTileCost, &Pathing_FreeTileCost, 20000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(Pathing_Directions_MatchOriginal) {
    CompareDeltaFns(ctx, &Pathing_DirectionOfDelta, &Pathing_DirectionOfStep, &Pathing_Angle, 200000);
}
S3RE_TEST(Pathing_Directions_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDeltaFns(probe, &WrongDirectionOfDelta, &Pathing_DirectionOfStep, &Pathing_Angle, 200000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(Pathing_StateFunctions_MatchOriginal) {
    CompareStateFns(ctx, &Pathing_DistanceAlong, &Pathing_TrackAngle, &Pathing_ChooseSextant, &Pathing_SetupLineStep,
                    200000);
}
S3RE_TEST(Pathing_StateFunctions_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareStateFns(probe, &Pathing_DistanceAlong, &Pathing_TrackAngle, &WrongChooseSextant, &Pathing_SetupLineStep,
                    20000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
