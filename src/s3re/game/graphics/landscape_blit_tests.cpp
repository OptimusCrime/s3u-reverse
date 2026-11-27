// Tests of landscape_blit.cpp (CLandscapeSurface::BlitVisibleArea).
//
// A fake surface whose blit function pointer (+0x84) is a recorder; random
// sizes (height above the 40-pixel band, width above 0: otherwise both
// versions loop forever), scroll positions up to a few wraps, screen and
// sidebar sizes. Compared: the blit's ECX (relative), EDX, the count, the
// first `count` source and destination rects and EAX.

#include "game/globals.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

std::uint32_t __fastcall CLandscapeSurface_BlitVisibleArea(void* self, void* edx);
using BlitVisibleAreaFn = decltype(&CLandscapeSurface_BlitVisibleArea);
extern BlitVisibleAreaFn Original_CLandscapeSurface_BlitVisibleArea;

namespace {

constexpr std::size_t kSurfaceBytes = 0xa8;

std::vector<std::uint32_t>* g_blitLog = nullptr;
std::uint32_t g_blitBase = 0;

std::uint32_t __fastcall RecordBlit(void* self, std::uint32_t context, std::uint32_t count, const std::uint32_t* source,
                                    const std::uint32_t* destination) {
    g_blitLog->insert(g_blitLog->end(), {reinterpret_cast<std::uint32_t>(self) - g_blitBase, context, count});
    for (std::uint32_t i = 0; i < count && i < 4; ++i) {
        g_blitLog->insert(g_blitLog->end(), source + 4 * i, source + 4 * i + 4);
        g_blitLog->insert(g_blitLog->end(), destination + 4 * i, destination + 4 * i + 4);
    }
    return count * 0x1111u + context;
}

std::vector<std::uint32_t> Run(BlitVisibleAreaFn fn, const std::uint8_t* bytes) {
    std::uint8_t surface[kSurfaceBytes];
    std::memcpy(surface, bytes, sizeof(surface));
    const auto blit = reinterpret_cast<std::uint32_t>(&RecordBlit);
    std::memcpy(surface + 0x84, &blit, 4);
    std::vector<std::uint32_t> log;
    g_blitLog = &log;
    g_blitBase = reinterpret_cast<std::uint32_t>(surface);
    const std::uint32_t eax = fn(surface, nullptr);
    log.push_back(eax);
    log.insert(log.end(), reinterpret_cast<const std::uint32_t*>(surface),
               reinterpret_cast<const std::uint32_t*>(surface) + kSurfaceBytes / 4);
    g_blitLog = nullptr;
    return log;
}

void Compare(test::Context& ctx, BlitVisibleAreaFn candidate, int cases) {
    const std::uint32_t savedWidth = g_screenWidth;
    const std::uint32_t savedHeight = g_screenHeight;
    const std::int32_t savedSidebar = g_sidebarWidth;
    const std::int32_t savedY = g_layout7dfd58;
    std::uint8_t bytes[kSurfaceBytes];
    for (int n = 0; n < cases; ++n) {
        test::Rng& rng = ctx.rng();
        rng.Fill(bytes, sizeof(bytes));
        const std::uint32_t width = rng.Chance(1, 4) ? 1 + rng.Below(200) : 400 + rng.Below(4000);
        const std::uint32_t height = rng.Chance(1, 4) ? 41 + rng.Below(200) : 400 + rng.Below(4000);
        const std::uint32_t scrollX = rng.Chance(1, 8) ? rng.Below(8) : rng.Below(width * 3 + 1);
        const std::uint32_t scrollY = rng.Chance(1, 8) ? rng.Below(32) : rng.Below(height * 3 + 1);
        std::memcpy(bytes + 0x50, &width, 4);
        std::memcpy(bytes + 0x54, &height, 4);
        std::memcpy(bytes + 0x68, &scrollX, 4);
        std::memcpy(bytes + 0x6c, &scrollY, 4);
        g_screenWidth = rng.Chance(1, 4) ? rng.Below(5000) : 504 + rng.Below(600);
        g_screenHeight = rng.Chance(1, 4) ? rng.Below(5000) : 480 + rng.Below(300);
        g_sidebarWidth = rng.Chance(1, 4) ? static_cast<std::int32_t>(rng.Next32()) : rng.Range(136, 216);
        g_layout7dfd58 = rng.Chance(1, 4) ? static_cast<std::int32_t>(rng.Next32()) : 0;
        const std::vector<std::uint32_t> a = Run(Original_CLandscapeSurface_BlitVisibleArea, bytes);
        const std::vector<std::uint32_t> b = Run(candidate, bytes);
        if (!S3RE_EXPECT(a == b)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: size %u x %u, scroll %u, %u", n, width, height, scrollX, scrollY);
            break;
        }
    }
    g_screenWidth = savedWidth;
    g_screenHeight = savedHeight;
    g_sidebarWidth = savedSidebar;
    g_layout7dfd58 = savedY;
}

// Wrong: the wrapped rows don't move down.
std::uint32_t __fastcall WrongBlitVisibleArea(void* self, void* edx) {
    auto* const bytes = static_cast<std::uint8_t*>(self);
    std::uint32_t width = 0;
    std::uint32_t scrollX = 0;
    std::memcpy(&width, bytes + 0x50, 4);
    std::memcpy(&scrollX, bytes + 0x68, 4);
    if (scrollX + 8 >= width && width != 0) {
        const std::uint32_t reduced = (scrollX + 8) % width - 8;
        std::memcpy(bytes + 0x68, &reduced, 4);
        const std::uint32_t eax = CLandscapeSurface_BlitVisibleArea(self, edx);
        std::memcpy(bytes + 0x68, &scrollX, 4);
        return eax;
    }
    return CLandscapeSurface_BlitVisibleArea(self, edx);
}

}  // namespace

S3RE_TEST(LandscapeBlit_MatchesOriginal) {
    Compare(ctx, &CLandscapeSurface_BlitVisibleArea, 20000);
}

S3RE_TEST(LandscapeBlit_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Compare(probe, &WrongBlitVisibleArea, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
