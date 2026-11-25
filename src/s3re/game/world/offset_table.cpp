// The world's table of map offsets sorted by distance (CGameData::offsets,
// layout::MapOffset at world+0x110a588; re/structs.md "MapOffset").
//
// CGameData::BuildOffsetTable (0x00516530; thiscall, no arguments, called by
//   the CGameData constructor; EAX unused): on the stack, 5000 hex points
//   (ring r = 1, 2, ..., step a = 0..r-1, in that order) with the distance
//   2500 * (2r - a)^2 + 7569 * a^2 (50^2 and 87^2: a hex grid's x and y
//   spacing, 0.87 ~ sqrt(3) / 2). Entry 0 of the table becomes (0, 0, 0)
//   (its sector is not written). Then 3333 times: the nearest point not
//   taken yet (the first of equal ones) is taken and written as six
//   entries, one per 60-degree rotation: (r, a), (r - a, r), (-a, r - a),
//   (-r, -a), (a - r, -r), (a, a - r), each with the ring counter as its
//   distance and sectors 0..5, or 1..5, 0 when 2a > r. The ring counter
//   starts at 1 and grows after each point with a = 0 (the first point of
//   the next ring comes later, as it is farther than this ring's others).
//   Entries 1..19998 are written; entry 19999 is not.

#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace s3re::game {

namespace {

constexpr int kPoints = 5000;
constexpr int kTakenPoints = 3333;
constexpr int kRotations = 6;
constexpr std::int32_t kTaken = 0x7FFFFFFF;  // a taken point's distance (and the search's start)
constexpr std::int32_t kXSpacingSquared = 2500;  // 50^2
constexpr std::int32_t kYSpacingSquared = 7569;  // 87^2

struct HexPoint {
    std::int32_t distance;
    std::int32_t ring;
    std::int32_t step;
};

}  // namespace

void __fastcall CGameData_BuildOffsetTable(CGameData* world, void* edx);
using BuildOffsetTableFn = void(__fastcall*)(CGameData* world, void* edx);
S3RE_HOOK(0x00516530, CGameData_BuildOffsetTable, BuildOffsetTableFn, Original_CGameData_BuildOffsetTable,
          CGameData_BuildOffsetTable);

// Purpose: Builds the table of map offsets sorted by distance from a tile,
// which the game's searches walk ring by ring ("the nearest X around here").
void __fastcall CGameData_BuildOffsetTable(CGameData* world, void*) {
    HexPoint points[kPoints];
    std::int32_t ring = 1;
    std::int32_t step = 0;
    for (HexPoint& point : points) {
        const std::int32_t x = 2 * ring - step;
        point = {kXSpacingSquared * x * x + kYSpacingSquared * step * step, ring, step};
        if (++step == ring) {
            step = 0;
            ++ring;
        }
    }

    layout::MapOffset& centre = world->offsets[0];
    centre.dx = 0;
    centre.dy = 0;
    centre.distance = 0;

    layout::MapOffset* out = &world->offsets[1];
    std::int32_t ringCounter = 1;
    for (int n = 0; n < kTakenPoints; ++n) {
        std::int32_t best = kTaken;
        int nearest = 0;
        for (int k = 0; k < kPoints; ++k) {
            if (points[k].distance < best) {
                best = points[k].distance;
                nearest = k;
            }
        }
        points[nearest].distance = kTaken;
        const std::int32_t r = points[nearest].ring;
        const std::int32_t a = points[nearest].step;
        const std::int32_t rotations[kRotations][2] = {{r, a}, {r - a, r}, {-a, r - a},
                                                       {-r, -a}, {a - r, -r}, {a, a - r}};
        const std::int32_t firstSector = 2 * a > r ? 1 : 0;
        for (int k = 0; k < kRotations; ++k) {
            out[k].dx = rotations[k][0];
            out[k].dy = rotations[k][1];
            out[k].distance = ringCounter;
            out[k].sector = (firstSector + k) % kRotations;
        }
        out += kRotations;
        if (a == 0) {
            ++ringCounter;
        }
    }
}

// ---------------------------------------------------------------------------
// Tests: random worlds up to past the table; the whole range compared.
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kTestWorld = offsetof(CGameData, offsets) + sizeof(CGameData::offsets) + 64;

void CompareBuildOffsetTable(test::Context& ctx, BuildOffsetTableFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        test::WorldPair worlds(ctx, kTestWorld);
        Original_CGameData_BuildOffsetTable(worlds.original(), nullptr);
        candidate(worlds.ours(), nullptr);
        if (!worlds.Same(ctx, 0, kTestWorld)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

// Wrong: two entries of the second ring swapped.
void __fastcall WrongBuildOffsetTable(CGameData* world, void* edx) {
    CGameData_BuildOffsetTable(world, edx);
    std::swap(world->offsets[7], world->offsets[13]);
}

}  // namespace

S3RE_TEST(CGameData_BuildOffsetTable_MatchesOriginal) {
    CompareBuildOffsetTable(ctx, &CGameData_BuildOffsetTable, 2);
}

S3RE_TEST(CGameData_BuildOffsetTable_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareBuildOffsetTable(probe, &WrongBuildOffsetTable, 1);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
