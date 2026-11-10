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
using SettlerStateFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, Settler* settler, PathState* state);
using ResolveFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, PathState* state, std::uint32_t direction);
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

namespace {

std::int32_t Add(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}

std::int32_t Min(std::int32_t a, std::int32_t b) {
    return a < b ? a : b;
}

// direction + 1, wrapping only at exactly 6 (like the original).
std::int32_t NextDirection(std::int32_t direction) {
    ++direction;
    return direction == 6 ? 0 : direction;
}

// direction - 1, 0 becomes 5.
std::int32_t PreviousDirection(std::int32_t direction) {
    return direction == 0 ? 5 : direction - 1;
}

// The neighbour of (x, y) in `direction` (any value; the tables are read
// like the original reads them).
std::int32_t StepX(std::int32_t x, std::int32_t direction) {
    return Add(x, kDirX[direction]);
}
std::int32_t StepY(std::int32_t y, std::int32_t direction) {
    return Add(y, kDirY[direction]);
}

// TileCost(state, neighbour in `direction` of (x, y), ...).
std::int32_t NeighbourCost(GameWorld* world, PathState* state, std::int32_t x, std::int32_t y, std::int32_t direction,
                           std::uint32_t needEmpty, std::uint32_t blockBusy) {
    return Pathing_TileCost(world, nullptr, state, StepX(x, direction), StepY(y, direction), needEmpty, 0, blockBusy);
}

void FinishEdgeStep(Settler& settler) {
    if (--settler.path_steps == 0) {
        settler.path_mode = static_cast<std::uint8_t>((settler.path_mode & 0xc2) | layout::kPathWalkLine);
    }
}

}  // namespace

// Purpose: Checks whether the settler can get past an obstacle: steps out up to two tiles in the
// current sextant's direction and reports whether one of them is passable (0), all are blocked
// (0x20) or the walk left the map (0xff).
std::uint32_t __fastcall Pathing_StepOut(GameWorld* world, void*, PathState* state) {
    PathState& s = *state;
    const std::int32_t direction = s.sextant;
    std::uint32_t result = 0x20;
    s.probe_x = s.x;
    s.probe_y = s.y;
    std::int32_t count = 0;
    do {
        s.probe_x = StepX(s.probe_x, direction);
        s.probe_y = StepY(s.probe_y, direction);
        const auto mapSize = static_cast<std::int32_t>(world->map_size);
        if (s.probe_x < 0 || s.probe_x >= mapSize || s.probe_y < 0 || s.probe_y >= mapSize) {
            result = 0xff;
        }
        if (Pathing_TileCost(world, nullptr, state, s.probe_x, s.probe_y, 0, 0, 1) < kBlocked) {
            result = 0;
        }
        ++count;
    } while (count < 2 && result == 0x20);
    return result;
}

// Purpose: Walks around the obstacle in front of the settler along both of its edges at once (left
// and right hand), at most four steps each, to see which side reaches the free tile found by StepOut
// first; remembers the side and the number of steps in the settler.
std::uint32_t __fastcall Pathing_CompareEdges(GameWorld* world, void*, Settler* settler, PathState* state) {
    PathState& s = *state;
    std::int32_t count = 0;
    std::uint32_t result = 0xff;
    std::int32_t leftX = s.x, leftY = s.y;
    std::int32_t rightX = s.x, rightY = s.y;
    std::int32_t left = s.chosen == 0 ? 5 : s.chosen - 1;
    std::int32_t right = NextDirection(s.chosen);
    while (count <= 3) {
        // Left hand: the first free direction, turning clockwise.
        int tries = 0;
        std::int32_t direction = left;
        while (NeighbourCost(world, state, leftX, leftY, direction, 0, 1) >= kBlocked) {
            direction = PreviousDirection(direction);
            if (++tries >= 6) {
                direction = -1;
                break;
            }
        }
        left = direction;
        if (direction == -1) {
            count = 4;
        } else {
            leftX = StepX(leftX, direction);
            leftY = StepY(leftY, direction);
            left = NextDirection(direction);
            if (leftX == s.probe_x && leftY == s.probe_y) {
                result = 1;
            } else {
                // Right hand, turning the other way.
                tries = 0;
                direction = right;
                while (NeighbourCost(world, state, rightX, rightY, direction, 0, 1) >= kBlocked) {
                    direction = NextDirection(direction);
                    if (++tries >= 6) {
                        direction = -1;
                        break;
                    }
                }
                right = direction;
                if (direction == -1) {
                    count = 4;  // + 1 below: the original ends with 5 here (no "all blocked" result)
                } else {
                    rightX = StepX(rightX, direction);
                    rightY = StepY(rightY, direction);
                    right = direction == 0 ? 5 : direction - 1;
                    if (rightX == s.probe_x && rightY == s.probe_y) {
                        result = 0;
                    }
                }
            }
            ++count;
        }
        if (result != 0xff) {
            break;
        }
    }
    if (count == 4) {
        result = 0x20;
    }
    settler->path_direction = static_cast<std::uint8_t>(s.chosen);
    settler->path_steps = static_cast<std::uint8_t>(count - 1 < 0 ? 0 : count - 1);
    return result;
}

// Purpose: The settler is blocked: checks whether it can get past the obstacle and, if so, starts
// walking along the obstacle's edge on the shorter side; otherwise gives up and plans a new line.
std::uint32_t __fastcall Pathing_StartEdgeWalk(GameWorld* world, void*, Settler* settler, PathState* state) {
    SetPathPhase(*state, 6);
    if (Pathing_StepOut(world, nullptr, state) != 0) {
        settler->path_mode = 0xc1;
    } else {
        const std::uint32_t edges = Pathing_CompareEdges(world, nullptr, settler, state);
        if ((edges & 0x20) != 0) {
            settler->path_mode = 0xc1;
        } else {
            settler->path_mode = static_cast<std::uint8_t>((((edges & 1) << 4) & 0xff) | (settler->path_mode & 0xc8) |
                                                           layout::kPathFollowEdge);
        }
    }
    SetPathPhase(*state, 1);
    return 0xff;
}

// Purpose: The step in `direction` is taken by another settler: if that settler is idle in the same
// area, or a queue of walking settlers leads back to this one, they swap or give way (0x10); if
// another settler waits for this one, a random free direction is chosen (0x40); otherwise the settler
// has to wait (8).
std::uint32_t __fastcall Pathing_ResolveBlocker(GameWorld* world, void*, PathState* state, std::uint32_t direction) {
    PathState& s = *state;
    const auto first = static_cast<std::int32_t>(direction);
    const std::uint16_t ownArea = TileAt(world, s.x, s.y).area;
    const Tile& next = TileAt(world, StepX(s.x, first), StepY(s.y, first));
    if (next.settler != 0 &&
        static_cast<std::uint8_t>(GameWorld_IsSettlerJob01Or37(world, nullptr, SettlerAt(world, next.settler).job)) != 0 &&
        next.area == ownArea) {
        return direction | 0x10;
    }
    std::int32_t current = s.settler;
    std::int32_t count = 0;
    std::int32_t x = s.x, y = s.y;
    std::int32_t step = first;
    do {
        x = StepX(x, step);
        y = StepY(y, step);
        const std::uint16_t number = TileAt(world, x, y).settler;
        if (number == 0) {
            return direction | 8;
        }
        const Settler& other = SettlerAt(world, number);
        if (static_cast<std::uint8_t>(GameWorld_IsWalkingJob(world, nullptr, other.job)) == 0) {
            return direction | 8;
        }
        if ((other.path_mode & layout::kPathWaiting) == 0 && number != static_cast<std::uint32_t>(s.settler)) {
            return direction | 8;
        }
        current = number;
        step = other.path_direction;
        ++count;
    } while (current != s.settler && count < 20000);
    if (count < 2) {
        return direction | 8;
    }
    if (count == 2) {
        const Tile& neighbour = TileAt(world, StepX(s.x, first), StepY(s.y, first));
        if (neighbour.area == ownArea) {
            Settler& other = SettlerAt(world, neighbour.settler);
            const std::uint8_t mode = other.path_mode;
            if ((mode & layout::kPathFollowEdge) != 0) {
                const std::int32_t turned =
                    (mode & layout::kPathEdgeLeft) != 0 ? NextDirection(first) : PreviousDirection(first);
                other.path_direction = static_cast<std::uint8_t>(turned);
                FinishEdgeStep(other);
            }
            return direction | 0x10;
        }
    }
    std::int32_t random = static_cast<std::int32_t>(GameWorld_RandomBelow6(world, nullptr));
    for (int i = 0; i < 6; ++i) {
        if (NeighbourCost(world, state, s.x, s.y, random, 1, 1) < kBlocked) {
            return static_cast<std::uint32_t>(random) | 0x40;
        }
        random = PreviousDirection(random);
    }
    return direction | 8;
}

// Purpose: Tries to take one step in the sextant towards the waypoint (the better of its two
// directions first); a step blocked by another settler goes to ResolveBlocker; 0x20 when both
// directions are blocked for good.
std::uint32_t __fastcall Pathing_TryStep(GameWorld* world, void*, PathState* state) {
    PathState& s = *state;
    SetPathPhase(s, 5);
    std::int32_t first, second;
    if (s.along_a > s.along_b) {
        second = s.sextant;
        first = NextDirection(s.sextant);
    } else {
        first = s.sextant;
        second = NextDirection(s.sextant);
    }
    s.chosen = first;
    const auto uFirst = static_cast<std::uint32_t>(first);
    const auto uSecond = static_cast<std::uint32_t>(second);
    std::uint32_t result;
    if (NeighbourCost(world, state, s.x, s.y, first, 1, 1) < 0xffff) {
        result = uFirst;
    } else if (s.along_a == 0) {
        result = NeighbourCost(world, state, s.x, s.y, first, 0, 1) < 0xffff ? (uFirst | 8) : (uFirst | 0x20);
    } else if (NeighbourCost(world, state, s.x, s.y, second, 1, 1) < 0xffff) {
        result = uSecond;
    } else if (NeighbourCost(world, state, s.x, s.y, first, 0, 1) < 0xffff) {
        result = uFirst | 8;
    } else if (NeighbourCost(world, state, s.x, s.y, second, 0, 1) < 0xffff) {
        result = uSecond | 8;
    } else {
        result = uFirst | 0x20;
    }
    if ((result & 8) != 0) {
        result = Pathing_ResolveBlocker(world, nullptr, state, result & 7);
    }
    SetPathPhase(s, 1);
    return result;
}

namespace {

// Tile (x, y) + i * axis A + j * axis B of the state.
std::int32_t AxisX(const PathState& s, std::int32_t i, std::int32_t j) {
    return Add(Add(s.x, Mul(s.axis_a_x, i)), Mul(s.axis_b_x, j));
}
std::int32_t AxisY(const PathState& s, std::int32_t i, std::int32_t j) {
    return Add(Add(s.y, Mul(s.axis_a_y, i)), Mul(s.axis_b_y, j));
}

}  // namespace

// Purpose: Compares the terrain costs of the next few tiles on the two (or, near the sextant's edge,
// three) candidate directions and returns the cheapest; 0x20 when every way is blocked.
std::uint32_t __fastcall Pathing_ChooseStepByCost(GameWorld* world, void*, PathState* state) {
    PathState& s = *state;
    SetPathPhase(s, 4);
    auto freeCost = [&](std::int32_t i, std::int32_t j) {
        return Pathing_FreeTileCost(world, nullptr, state, AxisX(s, i, j), AxisY(s, i, j));
    };
    auto cost = [&](std::int32_t i, std::int32_t j) {
        return Pathing_SectorTileCost(world, nullptr, state, AxisX(s, i, j), AxisY(s, i, j));
    };
    std::int32_t biasA = 0, biasB = 0, biasC = 0;
    std::uint32_t result;
    std::int32_t best;
    const std::int32_t a3 = Mul(s.along_a, 3);
    if (a3 > Mul(s.along_b, 2) || a3 > s.along_b) {
        // Two candidates. `mirrored`: the target lies closer to axis B, so
        // the roles of the axes swap.
        const bool mirrored = !(a3 > Mul(s.along_b, 2));
        std::int32_t f1, f2, c3, c4, c5, c6, c7, c8;
        if (!mirrored) {
            if (s.turn == 0) biasA = Negate(s.art_cost);
            if (s.turn == 5) biasB = Negate(s.art_cost);
            f1 = freeCost(1, 0);
            f2 = freeCost(0, 1);
            c3 = cost(1, 1);
            c4 = cost(2, 0);
            c5 = cost(0, 2);
            c6 = cost(2, 1);
            c7 = cost(3, 0);
            c8 = cost(1, 2);
        } else {
            if (s.turn == 0) biasB = Negate(s.art_cost);
            if (s.turn == 5) biasA = Negate(s.art_cost);
            f1 = freeCost(0, 1);
            f2 = freeCost(1, 0);
            c3 = cost(1, 1);
            c4 = cost(0, 2);
            c5 = cost(2, 0);
            c6 = cost(1, 2);
            c7 = cost(0, 3);
            c8 = cost(2, 1);
        }
        const std::int32_t straightA = Add(c7, c4);
        const std::int32_t bentA = Add(Add(Min(c4, c3), c6), -3);
        const std::int32_t pathA = straightA < bentA ? straightA : bentA;
        const std::int32_t scoreA = Add(Add(f1, pathA), biasA);
        const std::int32_t bentB = Add(Add(c6, c3), -3);
        const std::int32_t straightB = Add(Min(c3, c5), c8);
        const std::int32_t pathB = bentB < straightB ? bentB : straightB;
        const std::int32_t scoreB = Add(Add(pathB, f2), biasB);
        if (mirrored) {
            if (scoreA <= scoreB) {
                result = static_cast<std::uint32_t>(s.sextant);
                best = scoreA;
            } else {
                result = static_cast<std::uint32_t>(NextDirection(s.sextant));
                best = scoreB;
            }
        } else if (scoreA > scoreB) {
            result = static_cast<std::uint32_t>(s.sextant);
            best = scoreB;
        } else {
            result = static_cast<std::uint32_t>(NextDirection(s.sextant));
            best = scoreA;
        }
    } else {
        // Close to axis B: the directions of B - A (the one before the
        // sextant), axis A and axis B.
        if (s.turn == 0) biasC = Negate(s.art_cost);
        if (s.turn == 5) biasA = Negate(s.art_cost);
        if (s.turn == 4) biasB = Negate(s.art_cost);
        const std::int32_t g1 = freeCost(0, 1);
        const std::int32_t g2 = freeCost(-1, 1);
        const std::int32_t g3 = freeCost(1, 0);
        const std::int32_t h1 = cost(0, 2);
        const std::int32_t h2 = cost(-1, 2);
        const std::int32_t h3 = cost(1, 1);
        const std::int32_t h4 = cost(0, 3);
        const std::int32_t h5 = cost(1, 3);
        const std::int32_t h6 = cost(1, 2);
        const std::int32_t scoreR = Add(Add(Add(h5, h2), g2), biasB);
        const std::int32_t viaR = Add(Min(h1, h2), h5);
        const std::int32_t viaL = Add(Min(h1, h3), h6);
        const std::int32_t bent = viaR < viaL ? viaR : viaL;
        const std::int32_t straight = Add(Add(h4, h1), -3);
        const std::int32_t path = bent < straight ? bent : straight;
        const std::int32_t scoreA = Add(Add(path, g1), biasA);
        const std::int32_t scoreC = Add(Add(Add(h3, h6), g3), biasC);
        if (scoreA > scoreR) {
            if (scoreR < scoreC) {
                result = static_cast<std::uint32_t>(PreviousDirection(s.sextant));
                best = scoreR;
            } else {
                result = static_cast<std::uint32_t>(NextDirection(s.sextant));
                best = scoreC;
            }
        } else if (scoreA > scoreC) {
            result = static_cast<std::uint32_t>(NextDirection(s.sextant));
            best = scoreC;
        } else {
            result = static_cast<std::uint32_t>(s.sextant);
            best = scoreA;
        }
    }
    if (best >= 0xf000) {
        result |= 0x20;
    }
    SetPathPhase(s, 1);
    return result;
}

// Purpose: One step along an obstacle's edge (keeping a hand on the wall): the first free direction
// turning from the remembered one; when the counted steps are used up, the settler goes back to
// walking towards its waypoint.
std::uint32_t __fastcall Pathing_FollowEdge(GameWorld* world, void*, Settler* settler, PathState* state) {
    PathState& s = *state;
    SetPathPhase(s, 8);
    const std::int32_t x = settler->x;
    const std::int32_t y = settler->y;
    std::int32_t direction = settler->path_direction;
    std::int32_t next;
    if ((settler->path_mode & layout::kPathEdgeLeft) != 0) {
        int tries = 0;
        while (NeighbourCost(world, state, x, y, direction, 0, 1) >= kBlocked) {
            direction = PreviousDirection(direction);
            if (++tries >= 6) {
                direction = -1;
                break;
            }
        }
        next = NextDirection(direction);
    } else {
        int tries = 0;
        while (NeighbourCost(world, state, x, y, direction, 0, 1) >= kBlocked) {
            direction = NextDirection(direction);
            if (++tries >= 6) {
                direction = -1;
                break;
            }
        }
        next = direction == 0 ? 5 : direction - 1;
    }
    std::uint32_t result;
    if (direction == -1 || NeighbourCost(world, state, s.x, s.y, direction, 0, 1) != 0) {
        result = 0xff;
        settler->path_mode = static_cast<std::uint8_t>((settler->path_mode & 0xc2) | layout::kPathWalkLine);
    } else if (NeighbourCost(world, state, s.x, s.y, direction, 1, 1) < kBlocked) {
        result = static_cast<std::uint32_t>(direction);
        settler->path_direction = static_cast<std::uint8_t>(next);
        FinishEdgeStep(*settler);
    } else {
        result = Pathing_ResolveBlocker(world, nullptr, state, static_cast<std::uint32_t>(direction) & 7);
        if ((result & 0x40) != 0) {
            settler->path_mode = 0xc1;
            result &= 7;
        }
        if ((result & 0x10) != 0) {
            settler->path_direction = static_cast<std::uint8_t>(next);
            FinishEdgeStep(*settler);
        }
        if ((result & 8) != 0) {
            settler->path_mode |= layout::kPathWaiting;
            settler->path_direction = static_cast<std::uint8_t>(result & 7);
        }
    }
    SetPathPhase(s, 1);
    return result;
}

// Purpose: One step of walking towards the settler's waypoint: picks the direction (by terrain cost
// when the waypoint is still far), tries it, and switches to walking around an obstacle when the way
// is blocked.
std::uint32_t __fastcall Pathing_WalkLine(GameWorld* world, void*, Settler* settler, PathState* state) {
    PathState& s = *state;
    SetPathPhase(s, 2);
    s.dx = static_cast<std::int32_t>(settler->waypoint_x) - static_cast<std::int32_t>(settler->x);
    s.dy = static_cast<std::int32_t>(settler->waypoint_y) - static_cast<std::int32_t>(settler->y);
    s.abs_dx = Abs(s.dx);
    s.abs_dy = Abs(s.dy);
    std::uint32_t result = Pathing_ChooseSextant(world, nullptr, state);
    if ((result & 0x20) == 0) {
        result = Pathing_ChooseStepByCost(world, nullptr, state);
    }
    if ((result & 0x20) != 0) {
        result = Pathing_TryStep(world, nullptr, state);
    }
    if ((result & 0x40) != 0) {
        result &= 7;
    } else if ((result & 0x20) != 0) {
        settler->path_mode = static_cast<std::uint8_t>((settler->path_mode & 0xc4) | layout::kPathStepOut);
        result = 0xff;
    } else if ((result & 8) != 0) {
        settler->path_mode |= layout::kPathWaiting;
        settler->path_direction = static_cast<std::uint8_t>(result & 7);
    }
    SetPathPhase(s, 1);
    return result;
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
S3RE_HOOK(0x004a2ec0, Pathing_StepOut, StateFn, Original_Pathing_StepOut, Pathing_StepOut);
S3RE_HOOK(0x004a2f70, Pathing_CompareEdges, SettlerStateFn, Original_Pathing_CompareEdges, Pathing_CompareEdges);
S3RE_HOOK(0x004a2e20, Pathing_StartEdgeWalk, SettlerStateFn, Original_Pathing_StartEdgeWalk, Pathing_StartEdgeWalk);
S3RE_HOOK(0x004a33d0, Pathing_ResolveBlocker, ResolveFn, Original_Pathing_ResolveBlocker, Pathing_ResolveBlocker);
S3RE_HOOK(0x004a2c70, Pathing_TryStep, StateFn, Original_Pathing_TryStep, Pathing_TryStep);
S3RE_HOOK(0x004a2660, Pathing_ChooseStepByCost, StateFn, Original_Pathing_ChooseStepByCost,
          Pathing_ChooseStepByCost);
S3RE_HOOK(0x004a3170, Pathing_FollowEdge, SettlerStateFn, Original_Pathing_FollowEdge, Pathing_FollowEdge);
S3RE_HOOK(0x004a22f0, Pathing_WalkLine, SettlerStateFn, Original_Pathing_WalkLine, Pathing_WalkLine);

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

// Batch 2: the walking helpers on a prepared neighbourhood.

// Every tile's settler number below 32000 and every settler's path
// direction 0..5 (both worlds equally), so that chains of settlers
// (ResolveBlocker) stay inside the test world.
void SanitizeWorld(test::Context& ctx, test::WorldPair& worlds) {
    GameWorld* a = worlds.original();
    GameWorld* b = worlds.ours();
    for (std::uint32_t x = 0; x < layout::kMapRowTiles; ++x) {
        for (std::uint32_t y = 0; y < layout::kMapRowTiles; ++y) {
            const auto settler = static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Below(32000));
            a->tiles[x][y].settler = settler;
            b->tiles[x][y].settler = settler;
        }
    }
    for (std::uint32_t n = 0; n < 32000; ++n) {
        const auto direction = static_cast<std::uint8_t>(ctx.rng().Below(6));
        a->settlers[n].path_direction = direction;
        b->settlers[n].path_direction = direction;
    }
}

struct WalkCase {
    PathState state;
    std::uint32_t settler;
};

// A settler at a random position with a prepared neighbourhood (radius 7)
// and a matching state, as FindPath would set it up.
WalkCase PrepareWalkCase(test::Context& ctx, test::WorldPair& worlds) {
    WalkCase c;
    PathState& s = c.state;
    s = RandomState(ctx);
    s.x = ctx.rng().Range(16, 750);
    s.y = ctx.rng().Range(16, 750);
    s.walk_region = ctx.rng().Chance(1, 30) ? 1 : 0;
    s.area = ctx.rng().Chance(1, 30) ? 1 : 0;
    s.wide_region = ctx.rng().Chance(1, 30) ? 1 : 0;
    PrepareTiles(ctx, worlds, s.x, s.y, 7);
    c.settler = 1 + ctx.rng().Below(31998);
    s.settler = static_cast<std::int32_t>(c.settler);
    const std::int32_t sextant = static_cast<std::int32_t>(ctx.rng().Below(6));
    s.sextant = ctx.rng().Chance(1, 50) ? static_cast<std::int32_t>(ctx.rng().Below(8)) : sextant;
    const std::int32_t axisA = (sextant + 1) % 6;
    s.axis_a_x = ctx.rng().Chance(1, 20) ? ctx.rng().Range(-1, 1) : kDirX[axisA];
    s.axis_a_y = ctx.rng().Chance(1, 20) ? ctx.rng().Range(-1, 1) : kDirY[axisA];
    s.axis_b_x = kDirX[sextant];
    s.axis_b_y = kDirY[sextant];
    s.along_a = ctx.rng().Chance(1, 5) ? 0 : ctx.rng().Range(0, 30);
    s.along_b = ctx.rng().Chance(1, 5) ? 0 : ctx.rng().Range(0, 30);
    s.chosen = static_cast<std::int32_t>(ctx.rng().Below(6));
    s.probe_x = s.x + ctx.rng().Range(-3, 3);
    s.probe_y = s.y + ctx.rng().Range(-3, 3);
    s.art_cost = 1 + static_cast<std::int32_t>(ctx.rng().Below(2));
    // Never the position itself (ChooseSextant asserts on (0, 0); FindPath
    // plans a new line before that).
    std::int32_t wdx = ctx.rng().Range(-9, 9);
    const std::int32_t wdy = ctx.rng().Range(-9, 9);
    if (wdx == 0 && wdy == 0) {
        wdx = 1;
    }
    const auto waypointX = static_cast<std::uint16_t>(s.x + wdx);
    const auto waypointY = static_cast<std::uint16_t>(s.y + wdy);
    const auto mode = static_cast<std::uint8_t>(ctx.rng().Below(256));
    const auto steps = static_cast<std::uint8_t>(ctx.rng().Below(5));
    const auto direction = static_cast<std::uint8_t>(ctx.rng().Below(6));
    const std::int32_t x = s.x, y = s.y;
    const std::uint32_t n = c.settler;
    // Sometimes a queue of walking settlers: two neighbours that lead back
    // to this settler (count 2 in ResolveBlocker) or a ring of three.
    const std::uint32_t queue = ctx.rng().Below(5);
    const std::int32_t qd = static_cast<std::int32_t>(ctx.rng().Below(6));
    const auto a = static_cast<std::uint16_t>(1 + ctx.rng().Below(31998));
    const auto b = static_cast<std::uint16_t>(1 + ctx.rng().Below(31998));
    const auto modeA = static_cast<std::uint8_t>(ctx.rng().Below(256) | 0x20);
    const auto modeB = static_cast<std::uint8_t>(ctx.rng().Below(256) | 0x20);
    const auto areaA = static_cast<std::uint16_t>(ctx.rng().Chance(1, 4) ? 1 : 0);
    const auto stepsA = static_cast<std::uint8_t>(ctx.rng().Below(3));
    worlds.Both([&](GameWorld* w) {
        Settler& settler = w->settlers[n];
        settler.x = static_cast<std::uint16_t>(x);
        settler.y = static_cast<std::uint16_t>(y);
        settler.waypoint_x = waypointX;
        settler.waypoint_y = waypointY;
        settler.path_mode = mode;
        settler.path_steps = steps;
        settler.path_direction = direction;
        settler.job = 2;
        w->tiles[x][y].settler = static_cast<std::uint16_t>(n);
        if (queue == 1 && a != n) {
            // Neighbour in direction qd walks back to (x, y).
            Tile& t = w->tiles[x + kDirX[qd]][y + kDirY[qd]];
            t.settler = a;
            t.area = areaA;
            w->settlers[a].job = 2;
            w->settlers[a].path_mode = modeA;
            w->settlers[a].path_direction = static_cast<std::uint8_t>((qd + 3) % 6);
            w->settlers[a].path_steps = stepsA;
        } else if (queue == 3) {
            // The settler itself on the neighbour tile (a queue of length 1).
            w->tiles[x + kDirX[qd]][y + kDirY[qd]].settler = static_cast<std::uint16_t>(n);
        } else if (queue == 2 && a != n && b != n && a != b) {
            // A ring of three: (x, y) -> qd -> qd + 2 -> qd + 4 = back.
            const std::int32_t ax = x + kDirX[qd], ay = y + kDirY[qd];
            const std::int32_t d2 = (qd + 2) % 6;
            Tile& ta = w->tiles[ax][ay];
            ta.settler = a;
            w->settlers[a].job = 0x16d;
            w->settlers[a].path_mode = modeA;
            w->settlers[a].path_direction = static_cast<std::uint8_t>(d2);
            Tile& tb = w->tiles[ax + kDirX[d2]][ay + kDirY[d2]];
            tb.settler = b;
            w->settlers[b].job = 2;
            w->settlers[b].path_mode = modeB;
            w->settlers[b].path_direction = static_cast<std::uint8_t>((qd + 4) % 6);
        }
    });
    return c;
}


struct WalkFns {
    StateFn stepOut;
    SettlerStateFn compareEdges;
    SettlerStateFn startEdgeWalk;
    ResolveFn resolveBlocker;
    StateFn tryStep;
    StateFn chooseStepByCost;
    SettlerStateFn followEdge;
    SettlerStateFn walkLine;
};

void CompareWalkFns(test::Context& ctx, const WalkFns& fns, int cases) {
    test::WorldPair worlds(ctx, kFullWorld);
    worlds.Both([](GameWorld* w) { w->map_size = 768; });
    SanitizeWorld(ctx, worlds);
    const std::size_t settlersOffset = offsetof(GameWorld, settlers);
    for (int i = 0; i < cases; ++i) {
        const WalkCase c = PrepareWalkCase(ctx, worlds);
        if (ctx.rng().Chance(1, 20)) {
            const std::uint32_t size = ctx.rng().Chance(1, 2) ? 768 : static_cast<std::uint32_t>(c.state.x + 1);
            worlds.Both([&](GameWorld* w) { w->map_size = size; });
        }
        const int which = static_cast<int>(ctx.rng().Below(8));
        const std::uint32_t direction = ctx.rng().Below(6);
        const std::uint32_t n = c.settler;
        const bool ok = RunPathCase(
            ctx, worlds, c.state,
            [&](bool original, GameWorld* w, PathState* state) -> std::int32_t {
                Settler* settler = &w->settlers[n];
                std::uint32_t r = 0;
                switch (which) {
                    case 0:
                        r = (original ? Original_Pathing_StepOut : fns.stepOut)(w, nullptr, state);
                        break;
                    case 1:
                        r = (original ? Original_Pathing_CompareEdges : fns.compareEdges)(w, nullptr, settler, state);
                        break;
                    case 2:
                        r = (original ? Original_Pathing_StartEdgeWalk : fns.startEdgeWalk)(w, nullptr, settler, state);
                        break;
                    case 3:
                        r = (original ? Original_Pathing_ResolveBlocker : fns.resolveBlocker)(w, nullptr, state, direction);
                        break;
                    case 4:
                        r = (original ? Original_Pathing_TryStep : fns.tryStep)(w, nullptr, state);
                        break;
                    case 5:
                        r = (original ? Original_Pathing_ChooseStepByCost : fns.chooseStepByCost)(w, nullptr, state);
                        break;
                    case 6:
                        r = (original ? Original_Pathing_FollowEdge : fns.followEdge)(w, nullptr, settler, state);
                        break;
                    default:
                        r = (original ? Original_Pathing_WalkLine : fns.walkLine)(w, nullptr, settler, state);
                        break;
                }
                return static_cast<std::int32_t>(r);
            },
            i, 1000);
        bool same = worlds.SameField(ctx, [&](GameWorld* w) -> auto& { return w->settlers[n]; });
        for (std::int32_t dx = -3; dx <= 3 && same; ++dx) {
            for (std::int32_t dy = -3; dy <= 3 && same; ++dy) {
                const std::uint16_t other = worlds.original()->tiles[c.state.x + dx][c.state.y + dy].settler;
                same = worlds.SameField(ctx, [&](GameWorld* w) -> auto& { return w->settlers[other]; });
            }
        }
        if (!ok || !same || (i % 64 == 63 && !worlds.Same(ctx, settlersOffset, 32000 * sizeof(Settler))) ||
            !worlds.SameField(ctx, [](GameWorld* w) -> auto& { return w->random_state; })) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, function %d, settler %u", i, which, n);
            return;
        }
    }
    S3RE_EXPECT(worlds.Checkpoint(ctx));
}

const WalkFns kOurWalkFns = {&Pathing_StepOut,      &Pathing_CompareEdges,      &Pathing_StartEdgeWalk,
                             &Pathing_ResolveBlocker, &Pathing_TryStep,          &Pathing_ChooseStepByCost,
                             &Pathing_FollowEdge,   &Pathing_WalkLine};

// Wrong: the edge walk's step counter is not decreased.
std::uint32_t __fastcall WrongFollowEdge(GameWorld* world, void*, Settler* settler, PathState* state) {
    const std::uint8_t steps = settler->path_steps;
    const std::uint8_t mode = settler->path_mode;
    const std::uint32_t result = Pathing_FollowEdge(world, nullptr, settler, state);
    if (settler->path_steps == static_cast<std::uint8_t>(steps - 1)) {
        settler->path_steps = steps;
        settler->path_mode = mode;
    }
    return result;
}

// Wrong: a ring of three settlers is treated like a pair.
std::uint32_t __fastcall WrongResolveBlocker(GameWorld* world, void*, PathState* state, std::uint32_t direction) {
    const std::uint32_t result = Pathing_ResolveBlocker(world, nullptr, state, direction);
    return (result & 0x40) != 0 ? (direction | 0x10) : result;
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

S3RE_TEST(Pathing_WalkFunctions_MatchOriginal) {
    CompareWalkFns(ctx, kOurWalkFns, 40000);
}
S3RE_TEST(Pathing_WalkFunctions_ComparisonDetectsWrongImplementation) {
    WalkFns wrongEdge = kOurWalkFns;
    wrongEdge.followEdge = &WrongFollowEdge;
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareWalkFns(probe, wrongEdge, 4000);
    S3RE_EXPECT(probe.failures() > 0);
    WalkFns wrongBlocker = kOurWalkFns;
    wrongBlocker.resolveBlocker = &WrongResolveBlocker;
    test::Context probe2(ctx.name(), 2, /*quiet=*/true);
    CompareWalkFns(probe2, wrongBlocker, 4000);
    S3RE_EXPECT(probe2.failures() > 0);
}

}  // namespace s3re::game
