// CLandscapeSurface::BlitVisibleArea (0x0054c9d0, Volker\landscape.cpp):
// the part of the landscape surface that the screen shows, as up to four
// rectangles (the surface wraps around in both directions), handed to the
// surface's blit function.
//
// thiscall (), no arguments. The surface (fields below; the C++ layout of
// the whole object is framework_layout.h's CLandscapeSurface) is drawn
// 8 pixels right and 31 down of its scroll position (+0x68, +0x6c), with
// a 40-pixel band at the bottom left out: the source top is taken modulo
// height - 40 (by repeated adding of 40 - height while it is at least
// height - 40), the bottom is top + g_screenHeight, clamped to height - 40;
// the left is taken modulo the width by repeated subtraction, and every
// subtraction also moves the top and bottom one row down (a quirk; kept),
// the right is left + g_screenWidth, clamped to the width. The first
// rectangle goes to (g_sidebarWidth, g_layout7dfd58). If its height isn't
// the screen height, a second one below it: source rows from the number
// of horizontal wraps (as the original computes them) for the rest of the
// screen height. If its width isn't the screen width, the strip(s) to the
// right from source column 0 (rows top + 1 .. bottom + 1, and for the
// lower one the wraps + 1 .. rest + wraps + 1). All compares are unsigned.
// Then blit(ECX = this, EDX = +0x70, count, source rects, destination
// rects) through the function pointer +0x84 (it pops its arguments). EAX =
// what it returns. Rects are (left, top, right, bottom).

#include "game/globals.h"
#include "hooks.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game {

namespace {

#pragma pack(push, 1)
struct BlitRect {
    std::uint32_t left, top, right, bottom;
};

struct LandscapeBlitView {
    std::uint8_t unk00[0x50];
    std::uint32_t width;           // +0x50 the surface's size in pixels
    std::uint32_t height;          // +0x54
    std::uint8_t unk58[0x68 - 0x58];
    std::uint32_t scroll_x;        // +0x68
    std::uint32_t scroll_y;        // +0x6c
    std::uint32_t blit_context;    // +0x70 passed to the blit function in EDX
    std::uint8_t unk74[0x84 - 0x74];
    void* blit;                    // +0x84 the blit function (this, context, count, source, destination)
};
#pragma pack(pop)
static_assert(offsetof(LandscapeBlitView, width) == 0x50);
static_assert(offsetof(LandscapeBlitView, scroll_x) == 0x68);
static_assert(offsetof(LandscapeBlitView, blit_context) == 0x70);
static_assert(offsetof(LandscapeBlitView, blit) == 0x84);

constexpr std::uint32_t kLeftMargin = 8;     // the surface is drawn from 8 pixels right of the scroll position
constexpr std::uint32_t kTopMargin = 31;     // ... and 31 pixels down
constexpr std::uint32_t kBottomBand = 40;    // pixels at the bottom of the surface never shown
constexpr std::size_t kMaxRects = 4;

using BlitFn = std::uint32_t(__fastcall*)(void* self, std::uint32_t context, std::uint32_t count,
                                          const BlitRect* source, const BlitRect* destination);

}  // namespace

std::uint32_t __fastcall CLandscapeSurface_BlitVisibleArea(void* self, void* edx);
using BlitVisibleAreaFn = decltype(&CLandscapeSurface_BlitVisibleArea);

S3RE_HOOK(0x0054c9d0, CLandscapeSurface_BlitVisibleArea, BlitVisibleAreaFn,
          Original_CLandscapeSurface_BlitVisibleArea, CLandscapeSurface_BlitVisibleArea);

// Purpose: copies the visible part of the landscape (the scrolled map
// view) onto the screen, splitting it where the surface wraps around.
std::uint32_t __fastcall CLandscapeSurface_BlitVisibleArea(void* self, void* /*edx*/) {
    auto* const view = static_cast<LandscapeBlitView*>(self);
    const std::uint32_t shownHeight = view->height - kBottomBand;
    std::uint32_t top = view->scroll_y + kTopMargin;
    while (top >= shownHeight) {
        top += kBottomBand - view->height;
    }
    std::uint32_t bottom = g_screenHeight + top;
    if (bottom > shownHeight) {
        bottom = view->height - kBottomBand;
    }
    std::uint32_t left = view->scroll_x + kLeftMargin;
    std::uint32_t wraps = 0;
    while (left >= view->width) {  // each wrap also moves the rows down (as the original)
        left -= view->width;
        ++top;
        ++bottom;
        ++wraps;
    }
    std::uint32_t right = g_screenWidth + left;
    if (right > view->width) {
        right = view->width;
    }
    const auto sidebar = static_cast<std::uint32_t>(g_sidebarWidth.get());
    const auto originY = static_cast<std::uint32_t>(g_layout7dfd58.get());
    const std::uint32_t width = right - left;
    const std::uint32_t height = bottom - top;

    BlitRect source[kMaxRects] = {};
    BlitRect destination[kMaxRects] = {};
    source[0] = {left, top, right, bottom};
    destination[0] = {sidebar, originY, sidebar + width, originY + height};
    std::uint32_t count = 1;
    if (height != g_screenHeight) {
        const std::uint32_t rest = g_screenHeight - height;
        source[1] = {left, wraps, right, rest + wraps};
        destination[1] = {sidebar, originY + height, sidebar + width, g_screenHeight + originY};
        count = 2;
    }
    if (width != g_screenWidth) {
        const std::uint32_t strip = g_screenWidth - width;
        const std::uint32_t stripLeft = width + sidebar;
        const std::uint32_t stripRight = sidebar + g_screenWidth;
        source[count] = {0, top + 1, strip, bottom + 1};
        destination[count] = {stripLeft, originY, stripRight, originY + height};
        ++count;
        if (height != g_screenHeight) {
            const std::uint32_t rest = g_screenHeight - height;
            source[count] = {0, wraps + 1, strip, rest + wraps + 1};
            destination[count] = {stripLeft, height + originY, stripRight, originY + g_screenHeight};
            ++count;
        }
    }
    return reinterpret_cast<BlitFn>(view->blit)(self, view->blit_context, count, source, destination);
}

}  // namespace s3re::game
