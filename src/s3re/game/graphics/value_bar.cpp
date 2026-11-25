// Render::DrawValueBar (0x004d79c0, was Logic_sub_4d79c0): (left, top,
// right, bottom, value, limit) thiscall on the drawing object of
// Render::PutPixel16, ret 0x18; EAX not used. Its only caller is the big
// draw function 0x4b4d00 (0x4c1f6e), which draws eight such bars side by
// side from a table of values (probably a statistics screen).
//
// All coordinates and values are unsigned. It draws:
//  - a white frame (PackColour(255, 255, 255)): the rows `top` and `bottom`
//    from `left` to `right`, then the columns `left` and `right` from `top`
//    to `bottom`, pixel by pixel (Render::PutPixel16 0x4c7ac0);
//  - inside it (columns from left + 1, rows top + 1 .. bottom - 1; width =
//    right - left - 1):
//    - value > limit: a gradient of (width - 1) * limit / value + 1 columns
//      (the product wraps at 32 bits), column i in PackColour(0, c, c) with
//      c = 40 + i * 180 / width;
//    - value <= limit: all `width` columns striped in two cyan shades
//      (PackColour(0, 180, 180) and (0, 220, 220), starting with the
//      lighter one): a 16.16 position advances by (limit << 16) / width per
//      column, and each time it passes mark << 16 (mark starts at `value`)
//      the shade changes and the mark moves on by a step that starts at
//      `value` and grows to step + (step - 1) / 4 + 1 each time. The shade
//      test compares only the low 16 bits of the current colour with the
//      darker shade.
// A width of 0 divides by zero (an integer division fault), as in the
// original. The pixel colours come from Render::PackColour 0x4c7b00 (the
// colours are packed once, in the original's order: dark, light, white).

#include "game/globals.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace s3re::game {

namespace {

using PackColourFn = std::uint32_t(__stdcall*)(std::uint32_t c1, std::uint32_t c2, std::uint32_t c3);
using PutPixelFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t x, std::uint32_t y,
                                              std::uint32_t colour);
constexpr std::uintptr_t kPackColour = 0x004c7b00;  // Render::PackColour (verified)
constexpr std::uintptr_t kPutPixel = 0x004c7ac0;    // Render::PutPixel16 (verified)

constexpr std::uint32_t kDarkShade = 180;     // 0xb4
constexpr std::uint32_t kLightShade = 220;    // 0xdc
constexpr std::uint32_t kWhite = 255;
constexpr std::uint32_t kGradientStart = 40;  // 0x28
constexpr std::uint32_t kGradientRange = 180;  // 0xb4 added per column, divided by the width

std::uint32_t PackColour(std::uint32_t c1, std::uint32_t c2, std::uint32_t c3) {
    return reinterpret_cast<PackColourFn>(kPackColour)(c1, c2, c3);
}

void PutPixel(void* target, std::uint32_t x, std::uint32_t y, std::uint32_t colour) {
    reinterpret_cast<PutPixelFn>(kPutPixel)(target, nullptr, x, y, colour);
}

void DrawColumn(void* target, std::uint32_t x, std::uint32_t top, std::uint32_t bottom, std::uint32_t colour) {
    for (std::uint32_t y = top; y <= bottom; ++y) {
        PutPixel(target, x, y, colour);
    }
}

}  // namespace

using DrawValueBarFn = void(__fastcall*)(void* target, void* edx, std::uint32_t left, std::uint32_t top,
                                         std::uint32_t right, std::uint32_t bottom, std::uint32_t value,
                                         std::uint32_t limit);
void __fastcall Render_DrawValueBar(void* target, void* edx, std::uint32_t left, std::uint32_t top,
                                    std::uint32_t right, std::uint32_t bottom, std::uint32_t value,
                                    std::uint32_t limit);

S3RE_HOOK(0x004d79c0, Render_DrawValueBar, DrawValueBarFn, Original_Render_DrawValueBar, Render_DrawValueBar);

// Purpose: Draws a framed bar for a value against a limit: a gradient filled
// in proportion when the value is above the limit, otherwise stripes
// (probably one bar of a statistics screen).
void __fastcall Render_DrawValueBar(void* target, void* /*edx*/, std::uint32_t left, std::uint32_t top,
                                    std::uint32_t right, std::uint32_t bottom, std::uint32_t value,
                                    std::uint32_t limit) {
    const std::uint32_t dark = PackColour(0, kDarkShade, kDarkShade);
    const std::uint32_t light = PackColour(0, kLightShade, kLightShade);
    const std::uint32_t white = PackColour(kWhite, kWhite, kWhite);

    // The frame (0x4d7a1d..0x4d7a67).
    for (std::uint32_t x = left; x <= right; ++x) {
        PutPixel(target, x, top, white);
        PutPixel(target, x, bottom, white);
    }
    for (std::uint32_t y = top; y <= bottom; ++y) {
        PutPixel(target, left, y, white);
        PutPixel(target, right, y, white);
    }

    const std::uint32_t innerLeft = left + 1;
    const std::uint32_t innerTop = top + 1;
    const std::uint32_t innerBottom = bottom - 1;
    const std::uint32_t width = right - innerLeft;

    if (value > limit) {
        // The gradient (0x4d7a96..0x4d7b14).
        const std::uint32_t last = (width - 1) * limit / value;
        std::uint32_t shade = 0;
        for (std::uint32_t i = 0; i <= last; ++i, shade += kGradientRange) {
            const std::uint32_t c = shade / width + kGradientStart;
            DrawColumn(target, innerLeft + i, innerTop, innerBottom, PackColour(0, c, c));
        }
        return;
    }

    // The stripes (0x4d7b20..0x4d7bc3).
    const std::uint32_t advance = (limit << 16) / width;
    std::uint32_t colour = light;
    std::uint32_t position = 0;
    std::uint32_t mark = value;
    std::uint32_t step = value;
    for (std::uint32_t i = 0; i < width; ++i) {
        position += advance;
        if (position >= mark << 16) {
            step = step + (step - 1) / 4 + 1;
            mark += step;
            colour = static_cast<std::uint16_t>(colour) == static_cast<std::uint16_t>(dark) ? light : dark;
        }
        DrawColumn(target, innerLeft + i, innerTop, innerBottom, colour);
    }
}

// ---------------------------------------------------------------------------
// Tests: the original and ours draw the same bar into two pixel buffers
// (drawing objects of their own, the drawing origin and pixel format set
// per case, the buffers prefilled with random pixels); the buffers must be
// the same.
// ---------------------------------------------------------------------------

namespace {

#pragma pack(push, 1)
struct TestTarget {  // the fields Render::PutPixel16 reads
    std::uint8_t unk000[0x90];
    std::uint32_t pitch;   // +0x90 bytes per row
    std::uint8_t unk094[0xa4 - 0x94];
    std::uint32_t pixels;  // +0xa4
};
#pragma pack(pop)
static_assert(offsetof(TestTarget, pitch) == 0x90);
static_assert(offsetof(TestTarget, pixels) == 0xa4);

constexpr std::uint32_t kTestWidth = 160;
constexpr std::uint32_t kTestHeight = 96;

struct BarCase {
    std::uint32_t left, top, right, bottom, value, limit;
};

std::vector<std::uint16_t> Draw(DrawValueBarFn fn, const BarCase& c, std::uint64_t fillSeed) {
    std::vector<std::uint16_t> pixels(kTestWidth * kTestHeight);
    test::Rng fill(fillSeed);
    fill.Fill(pixels.data(), pixels.size() * 2);
    TestTarget target{};
    target.pitch = kTestWidth * 2;
    target.pixels = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pixels.data()));
    fn(&target, nullptr, c.left, c.top, c.right, c.bottom, c.value, c.limit);
    return pixels;
}

std::uint32_t ValueAround(test::Rng& rng, std::uint32_t limit) {
    switch (rng.Below(5)) {
    case 0: return limit;
    case 1: return limit + 1;
    case 2: return rng.Below(limit + 1);
    case 3: return limit + 1 + rng.Below(4 * limit + 10);
    default: return rng.Next32();
    }
}

void CompareBars(test::Context& ctx, DrawValueBarFn candidate, int cases) {
    const std::uint32_t savedX = g_drawOriginX, savedY = g_drawOriginY;
    const PixelFormat savedFormat = g_pixelFormat;
    for (int i = 0; i < cases; ++i) {
        test::Rng& rng = ctx.rng();
        g_drawOriginX = rng.Below(16);
        g_drawOriginY = rng.Below(16);
        rng.Fill(&g_pixelFormat.get(), sizeof(PixelFormat));
        BarCase c{};
        c.left = rng.Below(60);
        c.right = c.left + 2 + rng.Below(kTestWidth - 16 - 2 - c.left);  // width >= 1
        c.top = 1 + rng.Below(30);  // (bottom 0 would make the inner rows run to 0xffffffff)
        c.bottom = rng.Chance(1, 10) ? c.top + rng.Below(2) : c.top + rng.Below(kTestHeight - 16 - c.top);
        c.limit = rng.Chance(1, 3) ? rng.Below(20) : rng.Chance(1, 2) ? rng.Below(5000) : rng.Next32() >> 15;
        c.value = ValueAround(rng, c.limit);
        const std::uint64_t seed = rng.Next64();
        const std::vector<std::uint16_t> a = Draw(Original_Render_DrawValueBar, c, seed);
        const std::vector<std::uint16_t> b = Draw(candidate, c, seed);
        if (!S3RE_EXPECT(a == b)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: %u,%u..%u,%u value %u limit %u", i, c.left, c.top, c.right,
                     c.bottom, c.value, c.limit);
            break;
        }
    }
    g_drawOriginX = savedX;
    g_drawOriginY = savedY;
    g_pixelFormat = savedFormat;
}

// Wrong: the stripes' step grows by a third instead of a quarter.
void __fastcall WrongDrawValueBar(void* target, void* edx, std::uint32_t left, std::uint32_t top,
                                  std::uint32_t right, std::uint32_t bottom, std::uint32_t value,
                                  std::uint32_t limit) {
    Render_DrawValueBar(target, edx, left, top, right, bottom, value, limit);
    if (value <= limit && value > 2) {
        Render_DrawValueBar(target, edx, left, top, right, bottom, value + value / 3, limit);
    }
}

}  // namespace

S3RE_TEST(Render_DrawValueBar_MatchesOriginal) {
    CompareBars(ctx, &Render_DrawValueBar, 3000);
}
S3RE_TEST(Render_DrawValueBar_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareBars(probe, &WrongDrawValueBar, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
