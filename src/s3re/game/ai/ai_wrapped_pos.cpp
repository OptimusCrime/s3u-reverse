// AI::WrappedPos (our name; Rainer\CompMapWorkerAgent.cpp): a position on a
// width x height grid that wraps around at the edges, used by the computer
// players' site searches (a spiral around a start position,
// HOUSE_GROUND_REQUEST_NOTE::NextSpiralPosition 0x5763a0; MakeBuildOrders
// 0x56cd90; 0x571c40, 0x571fa0). All thiscall.
//
//   0x00597070 WrappedPos() : SetSize(1, 1), SetPosition(0, 0). EAX = this.
//   0x00597090 WrappedPos(x, y, width, height), ret 0x10: SetSize(width,
//              height), SetPosition(x, y). EAX = this.
//   0x005970c0 SetPosition(x, y), ret 8: words +0/+2, dwords +8/+0xc = 0,
//              then Wrap. EAX = Wrap's.
//   0x005970f0 SetSize(width, height), ret 8: words +4/+6, then Wrap.
//   0x00597110 Step(direction), ret 4: one step with wrap-around: 0 x+1,
//              1 x+1 and y+1, 2 y+1, 3 x-1, 4 x-1 and y-1, 5 y-1 (jump table
//              0x5971d8); other directions do nothing. EAX is left over
//              (the callers don't use it).
//   0x005971f0 Distance(x, y), ret 8: |dx| + |dy| on the torus (a delta
//              above half the size counts as size - delta; 16-bit signed
//              compares). The upper half of EAX is left over (reproduced):
//              that of the sign extension of dx and of `this`.
//   0x00597250 Wrap(): WrapCoords(&x, &y) on its own fields.
//   0x00597260 WrapCoords(s16* x, s16* y), ret 8: x into [0, width), y
//              into [0, height): a value >= size by the remainder, a
//              negative one as size - (-v mod size), 0 when that equals the
//              size. EAX: the y step's quotient (y >= height) or y's new
//              value, sign-extended.
//   0x00597300 Advance(direction, count), ret 8: count steps in a
//              direction (as Step) without wrapping; EAX = 0 if the
//              position left the grid (the second coordinate isn't moved
//              then), else 1 (also for other directions).

#include "game/ai/ai_layout.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace s3re::game {

using namespace ai;

namespace {

// Calls of the other replaced members through their S3.EXE addresses.
constexpr std::uintptr_t kSetPosition = 0x005970c0;
constexpr std::uintptr_t kSetSize = 0x005970f0;
constexpr std::uintptr_t kWrap = 0x00597250;
constexpr std::uintptr_t kWrapCoords = 0x00597260;

// The directions of Step and Advance (jump tables 0x5971d8, 0x5973b8).
enum class GridDirection : std::uint16_t {
    Right = 0,      // x + 1
    DownRight = 1,  // x + 1, y + 1
    Down = 2,       // y + 1
    Left = 3,       // x - 1
    UpLeft = 4,     // x - 1, y - 1
    Up = 5,         // y - 1
};

using PairFn = std::uint32_t(__fastcall*)(WrappedPos* self, void* edx, std::uint32_t a, std::uint32_t b);
using WrapFn = std::uint32_t(__fastcall*)(WrappedPos* self, void* edx);
using WrapCoordsFn = std::uint32_t(__fastcall*)(WrappedPos* self, void* edx, std::int16_t* x, std::int16_t* y);

std::uint32_t CallPair(std::uintptr_t address, WrappedPos* self, std::uint32_t a, std::uint32_t b) {
    return reinterpret_cast<PairFn>(address)(self, nullptr, a, b);
}

// One coordinate of WrapCoords (0x597267..0x5972a8, 0x5972a8..0x5972f1).
// Returns what the original leaves in EAX.
std::uint32_t WrapCoordinate(std::int16_t& value, std::uint16_t size) {
    const std::int32_t v = value;
    const std::int32_t limit = size;
    if (v >= limit) {
        value = static_cast<std::int16_t>(v % limit);
        return static_cast<std::uint32_t>(v / limit);
    }
    if (value < 0) {
        value = static_cast<std::int16_t>(size - (-v) % limit);
        // EAX keeps the value from before this correction.
        const auto wrapped = static_cast<std::uint32_t>(static_cast<std::int32_t>(value));
        if (static_cast<std::int32_t>(value) == limit) {
            value = 0;
        }
        return wrapped;
    }
    return static_cast<std::uint32_t>(v);
}

// Advances a coordinate by one and wraps it to 0 at the size (equality
// only, as the original compares).
void StepUp(std::int16_t& value, std::uint16_t size) {
    ++value;
    if (static_cast<std::int32_t>(value) == static_cast<std::int32_t>(size)) {
        value = 0;
    }
}

void StepDown(std::int16_t& value, std::uint16_t size) {
    --value;
    if (value < 0) {
        value = static_cast<std::int16_t>(size - 1);
    }
}

// |delta| of one axis, folded at half the size (0x5971f2..0x597218); `high`
// is the upper half the original's register holds for the folded value.
std::uint32_t FoldedDelta(std::int16_t delta, std::uint16_t size, std::uint32_t high) {
    const auto magnitude = static_cast<std::uint32_t>(delta < 0 ? -static_cast<std::int32_t>(delta) : delta);
    const auto half = static_cast<std::uint16_t>(size >> 1);
    if (static_cast<std::int16_t>(magnitude) > static_cast<std::int16_t>(half)) {
        return ((high & 0xffff0000u) | size) - magnitude;
    }
    return magnitude;
}

}  // namespace

WrappedPos* __fastcall AI_WrappedPos_Construct(WrappedPos* self, void* edx);
WrappedPos* __fastcall AI_WrappedPos_ConstructXywh(WrappedPos* self, void* edx, std::uint32_t x, std::uint32_t y,
                                                   std::uint32_t width, std::uint32_t height);
std::uint32_t __fastcall AI_WrappedPos_SetPosition(WrappedPos* self, void* edx, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall AI_WrappedPos_SetSize(WrappedPos* self, void* edx, std::uint32_t width,
                                               std::uint32_t height);
void __fastcall AI_WrappedPos_Step(WrappedPos* self, void* edx, std::uint32_t direction);
std::uint32_t __fastcall AI_WrappedPos_Distance(WrappedPos* self, void* edx, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall AI_WrappedPos_Wrap(WrappedPos* self, void* edx);
std::uint32_t __fastcall AI_WrappedPos_WrapCoords(WrappedPos* self, void* edx, std::int16_t* x, std::int16_t* y);
std::uint32_t __fastcall AI_WrappedPos_Advance(WrappedPos* self, void* edx, std::uint32_t direction,
                                               std::uint32_t count);

using ConstructFn = WrappedPos*(__fastcall*)(WrappedPos* self, void* edx);
using ConstructXywhFn = WrappedPos*(__fastcall*)(WrappedPos* self, void* edx, std::uint32_t x, std::uint32_t y,
                                                 std::uint32_t width, std::uint32_t height);
using StepFn = void(__fastcall*)(WrappedPos* self, void* edx, std::uint32_t direction);

S3RE_HOOK(0x00597070, AI_WrappedPos_Construct, ConstructFn, Original_AI_WrappedPos_Construct,
          AI_WrappedPos_Construct);
S3RE_HOOK(0x00597090, AI_WrappedPos_ConstructXywh, ConstructXywhFn, Original_AI_WrappedPos_ConstructXywh,
          AI_WrappedPos_ConstructXywh);
S3RE_HOOK(0x005970c0, AI_WrappedPos_SetPosition, PairFn, Original_AI_WrappedPos_SetPosition,
          AI_WrappedPos_SetPosition);
S3RE_HOOK(0x005970f0, AI_WrappedPos_SetSize, PairFn, Original_AI_WrappedPos_SetSize, AI_WrappedPos_SetSize);
S3RE_HOOK(0x00597110, AI_WrappedPos_Step, StepFn, Original_AI_WrappedPos_Step, AI_WrappedPos_Step);
S3RE_HOOK(0x005971f0, AI_WrappedPos_Distance, PairFn, Original_AI_WrappedPos_Distance, AI_WrappedPos_Distance);
S3RE_HOOK(0x00597250, AI_WrappedPos_Wrap, WrapFn, Original_AI_WrappedPos_Wrap, AI_WrappedPos_Wrap);
S3RE_HOOK(0x00597260, AI_WrappedPos_WrapCoords, WrapCoordsFn, Original_AI_WrappedPos_WrapCoords,
          AI_WrappedPos_WrapCoords);
S3RE_HOOK(0x00597300, AI_WrappedPos_Advance, PairFn, Original_AI_WrappedPos_Advance, AI_WrappedPos_Advance);

// Purpose: Makes a 1 x 1 wrapping position at (0, 0) (a site search's
// cursor before it gets its real size).
WrappedPos* __fastcall AI_WrappedPos_Construct(WrappedPos* self, void* /*edx*/) {
    CallPair(kSetSize, self, 1, 1);
    CallPair(kSetPosition, self, 0, 0);
    return self;
}

// Purpose: Makes a wrapping position on a grid of the given size (a site
// search's cursor, starting at the search's centre).
WrappedPos* __fastcall AI_WrappedPos_ConstructXywh(WrappedPos* self, void* /*edx*/, std::uint32_t x, std::uint32_t y,
                                                   std::uint32_t width, std::uint32_t height) {
    CallPair(kSetSize, self, width, height);
    CallPair(kSetPosition, self, x, y);
    return self;
}

// Purpose: Moves a wrapping position to a new place (wrapped onto the grid).
std::uint32_t __fastcall AI_WrappedPos_SetPosition(WrappedPos* self, void* /*edx*/, std::uint32_t x,
                                                   std::uint32_t y) {
    self->x = static_cast<std::int16_t>(x);
    self->y = static_cast<std::int16_t>(y);
    self->unk08 = 0;
    self->unk0c = 0;
    return reinterpret_cast<WrapFn>(kWrap)(self, nullptr);
}

// Purpose: Sets the grid size a wrapping position lives on.
std::uint32_t __fastcall AI_WrappedPos_SetSize(WrappedPos* self, void* /*edx*/, std::uint32_t width,
                                               std::uint32_t height) {
    self->width = static_cast<std::uint16_t>(width);
    self->height = static_cast<std::uint16_t>(height);
    return reinterpret_cast<WrapFn>(kWrap)(self, nullptr);
}

// Purpose: Moves a wrapping position one step in one of six directions
// (the steps of a spiral search around a site).
void __fastcall AI_WrappedPos_Step(WrappedPos* self, void* /*edx*/, std::uint32_t direction) {
    switch (static_cast<GridDirection>(static_cast<std::uint16_t>(direction))) {  // the low word, as the original
    case GridDirection::Right: StepUp(self->x, self->width); break;
    case GridDirection::DownRight:
        StepUp(self->x, self->width);
        StepUp(self->y, self->height);
        break;
    case GridDirection::Down: StepUp(self->y, self->height); break;
    case GridDirection::Left: StepDown(self->x, self->width); break;
    case GridDirection::UpLeft:
        StepDown(self->x, self->width);
        StepDown(self->y, self->height);
        break;
    case GridDirection::Up: StepDown(self->y, self->height); break;
    default: break;
    }
}

// Purpose: The walking distance (in grid steps, around the edges where that
// is shorter) from a wrapping position to a point.
std::uint32_t __fastcall AI_WrappedPos_Distance(WrappedPos* self, void* /*edx*/, std::uint32_t x, std::uint32_t y) {
    const auto dx = static_cast<std::int16_t>(static_cast<std::uint16_t>(x) - self->x);
    const auto dy = static_cast<std::int16_t>(static_cast<std::uint16_t>(y) - self->y);
    // The folded x delta keeps dx's sign extension in its upper half, the
    // folded y delta that of `this` (left over in the original's registers).
    const std::uint32_t across = FoldedDelta(dx, self->width, static_cast<std::uint32_t>(static_cast<std::int32_t>(dx)));
    const std::uint32_t along = FoldedDelta(dy, self->height, reinterpret_cast<std::uint32_t>(self));
    return across + along;
}

// Purpose: Wraps the position onto its grid.
std::uint32_t __fastcall AI_WrappedPos_Wrap(WrappedPos* self, void* /*edx*/) {
    using Fn = std::uint32_t(__fastcall*)(WrappedPos*, void*, std::int16_t*, std::int16_t*);
    return reinterpret_cast<Fn>(kWrapCoords)(self, nullptr, &self->x, &self->y);
}

// Purpose: Wraps a pair of coordinates onto the position's grid.
std::uint32_t __fastcall AI_WrappedPos_WrapCoords(WrappedPos* self, void* /*edx*/, std::int16_t* x, std::int16_t* y) {
    WrapCoordinate(*x, self->width);
    return WrapCoordinate(*y, self->height);
}

// Purpose: Moves a position several steps in one direction without
// wrapping and tells whether it is still on the grid (e.g. whether a
// building's other end fits on the map).
std::uint32_t __fastcall AI_WrappedPos_Advance(WrappedPos* self, void* /*edx*/, std::uint32_t direction,
                                               std::uint32_t countArg) {
    const auto count = static_cast<std::int16_t>(countArg);
    const auto add = [count](std::int16_t& value) { value = static_cast<std::int16_t>(value + count); };
    const auto subtract = [count](std::int16_t& value) { value = static_cast<std::int16_t>(value - count); };
    const auto below = [](std::int16_t value, std::uint16_t size) {
        return static_cast<std::int32_t>(value) < static_cast<std::int32_t>(size);
    };
    switch (static_cast<GridDirection>(static_cast<std::uint16_t>(direction))) {  // the low word, as the original
    case GridDirection::Right: add(self->x); return below(self->x, self->width) ? 1 : 0;
    case GridDirection::DownRight:
        add(self->x);
        if (!below(self->x, self->width)) {
            return 0;
        }
        add(self->y);
        return below(self->y, self->height) ? 1 : 0;
    case GridDirection::Down: add(self->y); return below(self->y, self->height) ? 1 : 0;
    case GridDirection::Left: subtract(self->x); return self->x >= 0 ? 1 : 0;
    case GridDirection::UpLeft:
        subtract(self->x);
        if (self->x < 0) {
            return 0;
        }
        subtract(self->y);
        return self->y >= 0 ? 1 : 0;
    case GridDirection::Up: subtract(self->y); return self->y >= 0 ? 1 : 0;
    default: return 1;
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

struct WrappedPosFns {
    ConstructFn construct;
    ConstructXywhFn constructXywh;
    PairFn setPosition, setSize;
    StepFn step;
    PairFn distance;
    WrapFn wrap;
    WrapCoordsFn wrapCoords;
    PairFn advance;
};

WrappedPosFns OriginalWrappedPos() {
    return {Original_AI_WrappedPos_Construct, Original_AI_WrappedPos_ConstructXywh,
            Original_AI_WrappedPos_SetPosition, Original_AI_WrappedPos_SetSize,
            Original_AI_WrappedPos_Step, Original_AI_WrappedPos_Distance,
            Original_AI_WrappedPos_Wrap, Original_AI_WrappedPos_WrapCoords,
            Original_AI_WrappedPos_Advance};
}

WrappedPosFns OurWrappedPos() {
    return {&AI_WrappedPos_Construct, &AI_WrappedPos_ConstructXywh, &AI_WrappedPos_SetPosition,
            &AI_WrappedPos_SetSize,   &AI_WrappedPos_Step,          &AI_WrappedPos_Distance,
            &AI_WrappedPos_Wrap,      &AI_WrappedPos_WrapCoords,    &AI_WrappedPos_Advance};
}

std::uint32_t Coordinate(test::Rng& rng) {
    switch (rng.Below(4)) {
    case 0: return rng.Below(60);
    case 1: return static_cast<std::uint32_t>(-static_cast<std::int32_t>(rng.Below(200)));
    case 2: return rng.Below(1000);
    default: return rng.Next32();
    }
}

// Sizes above 0 (the original divides by them); sometimes 0x8000 and up,
// where its signed and unsigned compares differ.
std::uint16_t Size(test::Rng& rng) {
    if (rng.Chance(1, 10)) {
        return static_cast<std::uint16_t>(0x7ff0 + rng.Below(0x20));
    }
    if (rng.Chance(1, 10)) {
        return static_cast<std::uint16_t>(1 + rng.Below(0xffff));
    }
    return static_cast<std::uint16_t>(1 + rng.Below(50));
}

void CompareWrappedPos(test::Context& ctx, const WrappedPosFns& candidate, int cases) {
    const WrappedPosFns original = OriginalWrappedPos();
    // Guard bytes around the object and the coordinates.
    struct Buffer {
        std::uint8_t before[8];
        WrappedPos pos;
        std::int16_t coords[2];
        std::uint8_t after[8];
    };
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        Buffer a{};
        ctx.rng().Fill(&a, sizeof(a));
        a.pos.width = Size(ctx.rng());
        a.pos.height = Size(ctx.rng());
        Buffer b = a;
        const int op = static_cast<int>(ctx.rng().Below(9));
        const std::uint32_t p = Coordinate(ctx.rng()), q = Coordinate(ctx.rng());
        std::uint32_t ra = 0, rb = 0;
        const auto relative = [](std::uint32_t value, const Buffer& buffer) {
            return value - reinterpret_cast<std::uint32_t>(&buffer.pos);
        };
        switch (op) {
        case 0:
            ra = relative(reinterpret_cast<std::uint32_t>(original.construct(&a.pos, nullptr)), a);
            rb = relative(reinterpret_cast<std::uint32_t>(candidate.construct(&b.pos, nullptr)), b);
            break;
        case 1: {
            const std::uint32_t w = Size(ctx.rng()) | (ctx.rng().Chance(1, 4) ? ctx.rng().Next32() << 16 : 0);
            const std::uint32_t h = Size(ctx.rng()) | (ctx.rng().Chance(1, 4) ? ctx.rng().Next32() << 16 : 0);
            ra = relative(reinterpret_cast<std::uint32_t>(original.constructXywh(&a.pos, nullptr, p, q, w, h)), a);
            rb = relative(reinterpret_cast<std::uint32_t>(candidate.constructXywh(&b.pos, nullptr, p, q, w, h)), b);
            break;
        }
        case 2:
            ra = original.setPosition(&a.pos, nullptr, p, q);
            rb = candidate.setPosition(&b.pos, nullptr, p, q);
            break;
        case 3: {
            const std::uint32_t w = Size(ctx.rng()), h = Size(ctx.rng());
            ra = original.setSize(&a.pos, nullptr, w, h);
            rb = candidate.setSize(&b.pos, nullptr, w, h);
            break;
        }
        case 4: {
            const std::uint32_t direction = ctx.rng().Below(8) | (ctx.rng().Chance(1, 4) ? ctx.rng().Next32() << 16 : 0);
            if (ctx.rng().Chance(1, 2)) {  // often on the edges
                a.pos.x = static_cast<std::int16_t>(ctx.rng().Chance(1, 2) ? 0 : a.pos.width - 1);
                a.pos.y = static_cast<std::int16_t>(ctx.rng().Chance(1, 2) ? 0 : a.pos.height - 1);
                b = a;
            }
            original.step(&a.pos, nullptr, direction);  // EAX: not used by the callers
            candidate.step(&b.pos, nullptr, direction);
            break;
        }
        case 5:
            // On the same object: the result can hold `this`'s upper half.
            ra = original.distance(&a.pos, nullptr, p, q);
            rb = candidate.distance(&a.pos, nullptr, p, q);
            break;
        case 6:
            ra = original.wrap(&a.pos, nullptr);
            rb = candidate.wrap(&b.pos, nullptr);
            break;
        case 7:
            a.coords[0] = static_cast<std::int16_t>(p);
            a.coords[1] = static_cast<std::int16_t>(q);
            b = a;
            ra = original.wrapCoords(&a.pos, nullptr, &a.coords[0], &a.coords[1]);
            rb = candidate.wrapCoords(&b.pos, nullptr, &b.coords[0], &b.coords[1]);
            break;
        default: {
            const std::uint32_t direction = ctx.rng().Below(8) | (ctx.rng().Chance(1, 4) ? ctx.rng().Next32() << 16 : 0);
            const std::uint32_t count = ctx.rng().Below(40) | (ctx.rng().Chance(1, 4) ? ctx.rng().Next32() << 16 : 0);
            ra = original.advance(&a.pos, nullptr, direction, count);
            rb = candidate.advance(&b.pos, nullptr, direction, count);
            break;
        }
        }
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT_MEM_EQ(&a, &b, sizeof(a));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
        }
    }
}

void __fastcall WrongStep(WrappedPos* self, void* edx, std::uint32_t direction) {
    // Wrong: a step left from 0 stays at 0.
    if ((direction & 0xffff) == 3 && self->x == 0) {
        return;
    }
    AI_WrappedPos_Step(self, edx, direction);
}

std::uint32_t __fastcall WrongDistance(WrappedPos* self, void* edx, std::uint32_t x, std::uint32_t y) {
    // Wrong: folds at the full size.
    const std::uint32_t result = AI_WrappedPos_Distance(self, edx, x, y);
    const auto dx = static_cast<std::int16_t>(static_cast<std::uint16_t>(x) - self->x);
    return dx > static_cast<std::int16_t>(self->width >> 1) && dx < static_cast<std::int16_t>(self->width)
               ? result + 1
               : result;
}

std::uint32_t __fastcall WrongWrapCoords(WrappedPos* self, void* edx, std::int16_t* x, std::int16_t* y) {
    // Wrong: a negative x becomes 0.
    if (*x < 0) {
        *x = 0;
    }
    return AI_WrappedPos_WrapCoords(self, edx, x, y);
}

std::uint32_t __fastcall WrongAdvance(WrappedPos* self, void* edx, std::uint32_t direction, std::uint32_t count) {
    // Wrong: unknown directions report "off the grid".
    if ((direction & 0xffff) > 5) {
        return 0;
    }
    return AI_WrappedPos_Advance(self, edx, direction, count);
}

}  // namespace

S3RE_TEST(AiWrappedPos_MatchesOriginal) {
    CompareWrappedPos(ctx, OurWrappedPos(), 20000);
}

S3RE_TEST(AiWrappedPos_ComparisonDetectsWrongImplementation) {
    const auto probe = [&](auto change) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        WrappedPosFns wrong = OurWrappedPos();
        change(wrong);
        CompareWrappedPos(quiet, wrong, 5000);
        S3RE_EXPECT(quiet.failures() > 0);
    };
    probe([](WrappedPosFns& f) { f.step = &WrongStep; });
    probe([](WrappedPosFns& f) { f.distance = &WrongDistance; });
    probe([](WrappedPosFns& f) { f.wrapCoords = &WrongWrapCoords; });
    probe([](WrappedPosFns& f) { f.advance = &WrongAdvance; });
}

}  // namespace s3re::game
