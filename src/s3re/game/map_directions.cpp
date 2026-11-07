// The six directions of the map grid and tile neighbours.
//
// Directions 0..5 go round the six neighbours of a tile; as (dx, dy):
//   0: (+1, 0)   1: (+1, +1)   2: (0, +1)   3: (-1, 0)   4: (-1, -1)   5: (0, -1)
// (consistent with TileDistance, where (+1, +1) is one step).
//
// NeighbourX (0x00503700) and NeighbourY (0x00503730): (coordinate,
// direction) -> the neighbour's coordinate. Any other direction leaves the
// coordinate unchanged. 32-bit arithmetic, no range checks.
// PreviousDirection (0x005038a0) and NextDirection (0x005038c0): turn by one
// direction with wrap-around (0 <-> 5); other values just -1 / +1.
// DirectionFromDelta (0x005037f0): (dx, dy) -> the direction 0..5 closest
// to that vector (signed 32-bit compares). The six sectors are split by the
// lines |dy| = 2|dx|-ish ratios exactly as in the original; see the code.
// All five: two or one stack arguments, callee pops (ret 8 / ret 4), ECX not
// used, the full EAX is the result.

#include "hooks.h"
#include "test/difftest.h"

#include <cstdint>

namespace s3re::game {

using NeighbourFn = std::uint32_t(__stdcall*)(std::uint32_t coordinate, std::uint32_t direction);
using TurnFn = std::uint32_t(__stdcall*)(std::uint32_t direction);

std::uint32_t __stdcall NeighbourX(std::uint32_t x, std::uint32_t direction) {
    if (direction == 0 || direction == 1) {
        ++x;
    }
    if (direction == 3 || direction == 4) {
        --x;
    }
    return x;
}

std::uint32_t __stdcall NeighbourY(std::uint32_t y, std::uint32_t direction) {
    if (direction == 1 || direction == 2) {
        ++y;
    }
    if (direction == 4 || direction == 5) {
        --y;
    }
    return y;
}

std::uint32_t __stdcall PreviousDirection(std::uint32_t direction) {
    return direction == 0 ? 5 : direction - 1;
}

std::uint32_t __stdcall NextDirection(std::uint32_t direction) {
    return direction == 5 ? 0 : direction + 1;
}

std::uint32_t __stdcall DirectionFromDelta(std::int32_t dx, std::int32_t dy) {
    // Comparisons are signed; negations and doublings wrap like the
    // original's 32-bit registers.
    auto neg = [](std::int32_t v) {
        return static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(v));
    };
    auto twice = [](std::int32_t v) {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(v) * 2u);
    };
    if (dx > 0) {
        if (dy < 0) {
            return dx >= neg(dy) ? 0 : 5;  // setge, dec, and 5
        }
        if (dx > dy) {
            return twice(dy) >= dx ? 1 : 0;
        }
        return twice(dx) < dy ? 2 : 1;
    }
    const std::int32_t mx = neg(dx);
    if (dy > 0) {
        return mx > dy ? 3 : 2;
    }
    const std::int32_t my = neg(dy);
    if (mx > my) {
        return mx <= twice(my) ? 4 : 3;
    }
    return my > twice(mx) ? 5 : 4;
}

S3RE_HOOK(0x00503700, NeighbourX, NeighbourFn, Original_NeighbourX, NeighbourX);
S3RE_HOOK(0x00503730, NeighbourY, NeighbourFn, Original_NeighbourY, NeighbourY);
S3RE_HOOK(0x005038a0, PreviousDirection, TurnFn, Original_PreviousDirection, PreviousDirection);
S3RE_HOOK(0x005038c0, NextDirection, TurnFn, Original_NextDirection, NextDirection);
using DirectionFromDeltaFn = std::uint32_t(__stdcall*)(std::int32_t dx, std::int32_t dy);
S3RE_HOOK(0x005037f0, DirectionFromDelta, DirectionFromDeltaFn, Original_DirectionFromDelta,
          DirectionFromDelta);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

std::uint32_t RandomValue(test::Rng& rng) {
    switch (rng.Below(3)) {
    case 0:
        return rng.Below(800);
    case 1:
        return 0u - rng.Below(4);  // around the 32-bit wrap
    default:
        return rng.Next32();
    }
}

// Every direction 0..7 and some random ones, with random coordinates.
void CompareNeighbour(test::Context& ctx, NeighbourFn original, NeighbourFn candidate,
                      int cases) {
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t coordinate = RandomValue(ctx.rng());
        const std::uint32_t direction = i % 4 == 3 ? ctx.rng().Next32() : i % 8;
        if (!S3RE_EXPECT_EQ(original(coordinate, direction), candidate(coordinate, direction))) {
            ctx.Fail(__FILE__, __LINE__, "  coordinate %u, direction %u", coordinate, direction);
            return;
        }
    }
}

void CompareTurn(test::Context& ctx, TurnFn original, TurnFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t direction = i < 16 ? static_cast<std::uint32_t>(i)
                                               : RandomValue(ctx.rng());
        if (!S3RE_EXPECT_EQ(original(direction), candidate(direction))) {
            ctx.Fail(__FILE__, __LINE__, "  direction %u", direction);
            return;
        }
    }
}

// Wrong: direction 4 moves y up instead of down.
std::uint32_t __stdcall WrongNeighbourY(std::uint32_t y, std::uint32_t direction) {
    if (direction == 1 || direction == 2 || direction == 4) {
        ++y;
    }
    if (direction == 5) {
        --y;
    }
    return y;
}

// Wrong: no wrap-around at 5.
std::uint32_t __stdcall WrongNextDirection(std::uint32_t direction) {
    return direction + 1;
}

void CompareDirectionFromDelta(test::Context& ctx, DirectionFromDeltaFn candidate,
                               std::int32_t radius, int randomCases) {
    // Every vector in a square around 0, then random ones (incl. extremes).
    for (std::int32_t dy = -radius; dy <= radius; ++dy) {
        for (std::int32_t dx = -radius; dx <= radius; ++dx) {
            if (!S3RE_EXPECT_EQ(Original_DirectionFromDelta(dx, dy), candidate(dx, dy))) {
                ctx.Fail(__FILE__, __LINE__, "  dx %d, dy %d", dx, dy);
                return;
            }
        }
    }
    for (int i = 0; i < randomCases; ++i) {
        const auto dx = static_cast<std::int32_t>(RandomValue(ctx.rng()));
        const auto dy = static_cast<std::int32_t>(RandomValue(ctx.rng()));
        if (!S3RE_EXPECT_EQ(Original_DirectionFromDelta(dx, dy), candidate(dx, dy))) {
            ctx.Fail(__FILE__, __LINE__, "  dx %d, dy %d", dx, dy);
            return;
        }
    }
}

// Wrong: the 1/2 boundary uses <= instead of <.
std::uint32_t __stdcall WrongDirectionFromDelta(std::int32_t dx, std::int32_t dy) {
    if (dx > 0 && dy >= 0 && dx <= dy) {
        return 2 * dx <= dy ? 2 : 1;
    }
    return DirectionFromDelta(dx, dy);
}

}  // namespace

S3RE_TEST(DirectionFromDelta_MatchesOriginal) {
    CompareDirectionFromDelta(ctx, &DirectionFromDelta, 300, 1000000);
}

S3RE_TEST(DirectionFromDelta_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDirectionFromDelta(probe, &WrongDirectionFromDelta, 50, 0);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(NeighbourX_MatchesOriginal) {
    CompareNeighbour(ctx, Original_NeighbourX, &NeighbourX, 200000);
}

S3RE_TEST(NeighbourY_MatchesOriginal) {
    CompareNeighbour(ctx, Original_NeighbourY, &NeighbourY, 200000);
}

S3RE_TEST(NeighbourY_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareNeighbour(probe, Original_NeighbourY, &WrongNeighbourY, 1000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(PreviousDirection_MatchesOriginal) {
    CompareTurn(ctx, Original_PreviousDirection, &PreviousDirection, 100000);
}

S3RE_TEST(NextDirection_MatchesOriginal) {
    CompareTurn(ctx, Original_NextDirection, &NextDirection, 100000);
}

S3RE_TEST(NextDirection_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareTurn(probe, Original_NextDirection, &WrongNextDirection, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
