#pragma once

// The pathfinder of Dirk\Pathing.cpp (0x4a1ee0..0x4a4b20): its state record
// and the functions other files call. See pathing.cpp for what each function
// does.

#include "game/world.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game {

namespace layout {

#pragma pack(push, 1)

// The pathfinder's state: a 0x9c-byte local of Pathing::FindPath
// (0x4a1ee0), cleared and filled there and passed by pointer to every helper.
// One step of FindPath decides a single walking direction for one settler.
//
// Directions are the six hexagonal neighbours of a tile, numbered 0..5 with
// the steps of the tables at 0x65867c (dx) and 0x658694 (dy): 0 (+1, 0),
// 1 (+1, +1), 2 (0, +1), 3 (-1, 0), 4 (-1, -1), 5 (0, -1).
struct PathState {
    std::int32_t settler;     // 0x00 settler number
    std::int32_t x, y;        // 0x04 the settler's position
    std::int32_t dx, dy;      // 0x0c target - position (target = target_x/y)
    std::int32_t abs_dx;      // 0x14 |dx|
    std::int32_t abs_dy;      // 0x18 |dy|
    // (dx, dy) = along_a * axis A + along_b * axis B, for the two neighbour
    // directions (axis A, axis B) of the sextant the target lies in
    // (ChooseSextant 0x4a23e0); both >= 0.
    std::int32_t along_a;     // 0x1c
    std::int32_t along_b;     // 0x20
    std::int32_t probe_x;     // 0x24 position reached by StepOut (0x4a2ec0)
    std::int32_t probe_y;     // 0x28
    std::int32_t unk2c;       // 0x2c
    std::int32_t unk30;       // 0x30
    std::int32_t axis_a_x;    // 0x34 axis A (dx) of the sextant
    std::int32_t axis_b_x;    // 0x38 axis B (dx)
    std::int32_t axis_a_y;    // 0x3c axis A (dy)
    std::int32_t axis_b_y;    // 0x40 axis B (dy)
    std::int32_t sextant;     // 0x44 0..5: the direction of axis A
    // 0x48: the settler's direction (Settler::direction) when it is 0..5,
    // else 1000; ChooseSextant adds 1..5 to it (mod 6 only when it reaches 6).
    std::int32_t turn;
    std::int32_t art_cost;    // 0x4c per-settler-type value from 0x659024 (1 or 2)
    std::int32_t chosen;      // 0x50 direction picked by 0x4a2c70
    std::uint32_t walk_region;  // 0x54 Tile::walk_region of the start tile
    std::uint32_t area;         // 0x58 Tile::area of the start tile
    std::uint32_t wide_region;  // 0x5c Tile::wide_region of the start tile
    // 0x60: which region a path must stay in (GameWorld::SettlerGroup):
    // 1 area, 2 walk region, 3 wide region.
    std::uint32_t sector_type;
    std::int32_t target_x;    // 0x64
    std::int32_t target_y;    // 0x68
    std::int32_t step_a_x;    // 0x6c line steps (16.16, SetupLineStep 0x4a3d30)
    std::int32_t step_a_y;    // 0x70
    std::int32_t step_b_x;    // 0x74
    std::int32_t step_b_y;    // 0x78
    // 0x7c: the point TraceLine (0x4a3e30) starts from (16.16, the centre
    // of the target tile; moved past an obstacle when the line is blocked).
    std::int32_t trace_x;
    std::int32_t trace_y;     // 0x80
    std::int32_t hit_x;       // 0x84 last free point before the obstacle (16.16)
    std::int32_t hit_y;       // 0x88
    std::int32_t hit_direction;  // 0x8c direction of the blocked step (DirectionOfStep)
    std::int32_t angle_base;  // 0x90 subtracted from Angle() by 0x4a48f0
    std::int32_t step_toggle; // 0x94 TraceLine alternates step A and step B
    // 0x98: 1 if the target tile itself is passable (PlanLine); passed as
    // TileCost's blockBusy while tracing.
    std::uint8_t target_free;
    std::uint8_t pad99[3];
};
static_assert(sizeof(PathState) == 0x9c);
static_assert(offsetof(PathState, along_a) == 0x1c);
static_assert(offsetof(PathState, axis_a_x) == 0x34);
static_assert(offsetof(PathState, sextant) == 0x44);
static_assert(offsetof(PathState, walk_region) == 0x54);
static_assert(offsetof(PathState, sector_type) == 0x60);
static_assert(offsetof(PathState, step_a_x) == 0x6c);
static_assert(offsetof(PathState, trace_x) == 0x7c);
static_assert(offsetof(PathState, hit_direction) == 0x8c);
static_assert(offsetof(PathState, angle_base) == 0x90);
static_assert(offsetof(PathState, target_free) == 0x98);

#pragma pack(pop)

}  // namespace layout

// The pathfinder's progress marker (S3.EXE 0x7a9d98): (sector type << 8) |
// phase | (area << 16), set by every pathing helper on entry and exit (1 =
// back in FindPath), 0 when FindPath returns. Only read by DebugLog calls in
// CDrawThread::vf13 (0x42c746, 0x42c7e0, next to the global 0x7dfd28):
// probably a report of where the logic thread was. Each update is three stores (phase, then
// sector type, then area), done the same way here.
constexpr std::uintptr_t kPathPhaseAddress = 0x7a9d98;

}  // namespace s3re::game
