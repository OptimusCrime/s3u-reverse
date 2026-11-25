// Drawing a sprite (Dirk\graphicsdata.cpp): the GfxDword draw methods that
// position, clip and pick a blitter, and the blitters of the colour-indexed
// (player colour) and shadow sprites. The 16-bit blitters and the row
// helpers are in gfx_rle.cpp; formats in re/graphics_formats.md.
//
// GfxDword::DrawSprite (0x5fadb0): thiscall (desc*, pos*, clip*, brightness,
//   fraction), ret 0x14. SelectBrightnessSpans(brightness); the position is
//   pos + the sprite's offset (Point16::Add); size from PointAt4. A fraction
//   other than 0 and 0x10000 (16.16, construction progress) keeps the
//   bottom rows only: visible = ((h + 8) * fraction) >> 16 (signed 32-bit
//   product, logical shift), kept = min(h, visible) (unsigned compare),
//   hidden = h - kept (16 bits, sign-extended), y += hidden, h = kept;
//   otherwise visible = h + 8, hidden 0. Nothing is drawn if x >= right,
//   x + w < left, y >= bottom, y + h < top or the fraction is 0. Blitter:
//   x < left: ClipLeft (x + w < right) or ClipBoth; x + w >= right:
//   ClipRight; else FullBright for brightness 248..255 (the whole dword
//   & ~7 == 0xf8), Shaded otherwise. Arguments (visible, hidden, *desc,
//   pos, size, SpriteRleData16(), *clip).
// GfxDword::DrawGuiImage (0x5f64c0): thiscall (desc*, pos, clip (by
//   value), brightness), ret 0x1c: the same for a GUI image (offset +4,
//   size PointAt0, rows GuiRleData), visible = h + 8, hidden 0, no
//   fraction.
// GfxDword::DrawColorIndexed (0x5fc5f0): thiscall (desc*, pos*, clip*,
//   palette), ret 0x10: position and rejection as DrawSprite (no fraction);
//   x < left: BlitColorIndexed_ClipLeft (even when the sprite also crosses
//   the right edge: no ClipBoth, so such a sprite is drawn past the clip's
//   right edge; kept), x + w >= right: ClipRight, else Unclipped.
//   Arguments (SpriteRleData(), palette, *desc, pos, size, *clip).
// GfxDword::DrawShadow (0x5fcdc0): thiscall (desc*, pos*, clip*, shift,
//   fraction), ret 0x14: position pos + offset; a fraction other than 0 and
//   0x10000 keeps kept = (h * fraction) >> 16 rows (no +8), hidden = h -
//   kept, y += hidden; rejection as above (and fraction 0); the dither
//   origin is the position + (shift, 0). Blitter by x as DrawColorIndexed:
//   (hidden, SpriteRleData(), *desc, pos, size, *clip, dither origin).
//
// Colour-indexed blitters: fastcall (ECX = rle, EDX = palette (256 16-bit
//   colours), stack: desc, pos, size, clip), ret 0x84; vertical clip and
//   skipped rows by ColorIndexedVerticalClip / ColorIndexedSkipRows; each
//   pixel byte is looked up in the palette.
//   BlitColorIndexed_Unclipped (0x5fc860), _ClipRight (0x5fc9a0), _ClipLeft
//   (0x5fcb30). In ClipRight a run starting at or past the edge doesn't
//   advance dst (as in the 16-bit ClipRight).
// Shadow blitters: fastcall (ECX = hidden, EDX = rle, stack: desc, pos,
//   size, clip, dither origin), ret 0x88: ShadowVerticalClip /
//   ShadowSkipRows, then per run g_shadowSpan.span(&dst, end, &phase,
//   parity) with phase = origin.x mod 32 at each row start (+ every pixel
//   passed) and parity = (origin.y mod 32 + skipTop) % 32, + 1 per row (C
//   remainders; negative origins made positive).
//   BlitShadow_Unclipped (0x5fd120), _ClipRight (0x5fd310), _ClipLeft
//   (0x5fd570).

#include "game/globals.h"
#include "game/graphics/gfx_layout.h"
#include "game/graphics/gfx_test_sprites.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

using gfx::ClipRect;
using gfx::SurfaceDesc;

constexpr std::uintptr_t kPoint16Construct = 0x005fdab0;
constexpr std::uintptr_t kPoint16Add = 0x005fdb00;
constexpr std::uintptr_t kPointAt4 = 0x005f6430;
constexpr std::uintptr_t kPointAt0 = 0x005f6470;
constexpr std::uintptr_t kSpriteRleData16 = 0x005f6440;
constexpr std::uintptr_t kSpriteRleData = 0x005fcda0;
constexpr std::uintptr_t kGuiRleData = 0x005f64a0;
constexpr std::uintptr_t kSelectBrightnessSpans = 0x005fb4c0;
constexpr std::uintptr_t kBlitRle16ClipLeft = 0x005fb4f0;
constexpr std::uintptr_t kBlitRle16ClipBoth = 0x005fb850;
constexpr std::uintptr_t kBlitRle16ClipRight = 0x005fbdf0;
constexpr std::uintptr_t kBlitRle16Shaded = 0x005fc160;
constexpr std::uintptr_t kBlitRle16FullBright = 0x005fc350;
constexpr std::uintptr_t kBlitColorUnclipped = 0x005fc860;
constexpr std::uintptr_t kBlitColorClipRight = 0x005fc9a0;
constexpr std::uintptr_t kBlitColorClipLeft = 0x005fcb30;
constexpr std::uintptr_t kBlitShadowUnclipped = 0x005fd120;
constexpr std::uintptr_t kBlitShadowClipRight = 0x005fd310;
constexpr std::uintptr_t kBlitShadowClipLeft = 0x005fd570;
constexpr std::uintptr_t kColorIndexedVerticalClip = 0x005fc7d0;
constexpr std::uintptr_t kColorIndexedSkipRows = 0x005fc810;
constexpr std::uintptr_t kShadowVerticalClip = 0x005fcff0;
constexpr std::uintptr_t kShadowSkipRows = 0x005fd040;

// Fractions that mean "the whole sprite" (16.16).
constexpr std::uint32_t kWholeSprite = 0x10000;
// Brightness 248..255 draws with plain copies.
constexpr std::uint32_t kFullBrightness = 0xf8;
// The dissolving rows a partly built 16-bit sprite gets in addition.
constexpr std::int32_t kDissolveRows = 8;

constexpr std::uint8_t kLastRun = 0x80;
constexpr std::uint8_t kSkipMask = 0x7f;
constexpr std::uint32_t kShadowPhaseWrap = 31;

using Getter0Fn = std::uint32_t(__fastcall*)(const void* self, void* edx);
using Getter1Fn = std::uint32_t(__fastcall*)(const void* self, void* edx, std::uint32_t a);
using Getter2Fn = std::uint32_t(__fastcall*)(const void* self, void* edx, std::uint32_t a, std::uint32_t b);
using SelectFn = void(__fastcall*)(std::uint32_t brightness);
using Blit16Fn = void(__fastcall*)(std::int32_t visible, std::int32_t hidden, SurfaceDesc desc, std::uint32_t pos,
                                   std::uint32_t size, std::uint32_t rle, ClipRect clip);
using ColorBlitFn = void(__fastcall*)(std::uint32_t rle, std::uint32_t palette, SurfaceDesc desc, std::uint32_t pos,
                                      std::uint32_t size, ClipRect clip);
using ShadowBlitFn = void(__fastcall*)(std::int32_t hidden, std::uint32_t rle, SurfaceDesc desc, std::uint32_t pos,
                                       std::uint32_t size, ClipRect clip, std::uint32_t origin);
using ClipFn = void(__fastcall*)(std::int32_t* skipTop, std::int32_t* rows, std::uint32_t pos, std::uint32_t size,
                                 std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom);
using SkipRows8Fn = void(__fastcall*)(const SurfaceDesc* desc, std::int32_t rows, std::uint32_t* rle,
                                      std::uint32_t* dst);
using SkipRowsFn = void(__fastcall*)(const SurfaceDesc* desc, std::int32_t rowsA, std::int32_t rowsB,
                                     std::uint32_t* rle, std::uint32_t* dst);
using ShadowSpanFn = void(__fastcall*)(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* phaseCell,
                                       std::uint32_t parity);

std::int16_t PointX(std::uint32_t point) {
    return static_cast<std::int16_t>(point & 0xffff);
}
std::int16_t PointY(std::uint32_t point) {
    return static_cast<std::int16_t>(point >> 16);
}
std::uint32_t WithY(std::uint32_t point, std::uint32_t y) {
    return (point & 0xffff) | (y & 0xffff) << 16;
}
std::uint8_t ByteAt(std::uint32_t address) {
    return *reinterpret_cast<const std::uint8_t*>(address);
}

// The GfxDword whose methods these are: one image address.
struct GfxDword {
    std::uint32_t image;
};

// Point16::Point16 (0x5fdab0) into a local.
std::uint32_t MakePoint(std::uint32_t x, std::uint32_t y) {
    std::uint32_t point;
    reinterpret_cast<Getter2Fn>(kPoint16Construct)(&point, nullptr, x, y);
    return point;
}

// Point16::Add (0x5fdb00): *out = *self + *other.
void AddPoints(const std::uint32_t* self, std::uint32_t* out, const std::uint32_t* other) {
    reinterpret_cast<Getter2Fn>(kPoint16Add)(self, nullptr, reinterpret_cast<std::uint32_t>(out),
                                              reinterpret_cast<std::uint32_t>(other));
}

std::uint32_t CallGetter1(std::uintptr_t function, const void* self, std::uint32_t* out) {
    reinterpret_cast<Getter1Fn>(function)(self, nullptr, reinterpret_cast<std::uint32_t>(out));
    return *out;
}

std::uint32_t CallGetter0(std::uintptr_t function, const void* self) {
    return reinterpret_cast<Getter0Fn>(function)(self, nullptr);
}

// The sprite's top-left pixel (as gfx_rle.cpp's blitters).
std::uint32_t SpriteRowStart(const SurfaceDesc& desc, std::uint32_t pos) {
    const std::uint32_t product = static_cast<std::uint32_t>(desc.pitch * PointY(pos));
    return desc.surface + ((product >> 1) + static_cast<std::uint32_t>(PointX(pos))) * 2;
}

std::uint32_t RowStep(const SurfaceDesc& desc) {
    return (static_cast<std::uint32_t>(desc.pitch) >> 1) * 2;
}

struct Run {
    std::uint32_t count;
    std::uint32_t skip;
    bool last;
};

Run ReadRun(std::uint32_t* rle) {
    const Run run{ByteAt(*rle), static_cast<std::uint32_t>(ByteAt(*rle + 1) & kSkipMask),
                  (ByteAt(*rle + 1) & kLastRun) != 0};
    *rle += 2;
    return run;
}

// Whether a sprite at x/y of size w/h touches the clip rectangle.
bool OverlapsClip(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height, const ClipRect& clip) {
    return x < clip.right && x + width >= clip.left && y < clip.bottom && y + height >= clip.top;
}

}  // namespace

// ---------------------------------------------------------------------------
// Draw methods
// ---------------------------------------------------------------------------

void __fastcall GfxDword_DrawSprite(const void* self, void* edx, const SurfaceDesc* desc, const std::uint32_t* pos,
                                    const ClipRect* clip, std::uint32_t brightness, std::uint32_t fraction);
void __fastcall GfxDword_DrawGuiImage(const void* self, void* edx, const SurfaceDesc* desc, std::uint32_t pos,
                                      ClipRect clip, std::uint32_t brightness);
void __fastcall GfxDword_DrawColorIndexed(const void* self, void* edx, const SurfaceDesc* desc,
                                          const std::uint32_t* pos, const ClipRect* clip, std::uint32_t palette);
void __fastcall GfxDword_DrawShadow(const void* self, void* edx, const SurfaceDesc* desc, const std::uint32_t* pos,
                                    const ClipRect* clip, std::int32_t shift, std::uint32_t fraction);

using DrawSpriteFn = void(__fastcall*)(const void* self, void* edx, const SurfaceDesc* desc, const std::uint32_t* pos,
                                       const ClipRect* clip, std::uint32_t brightness, std::uint32_t fraction);
using DrawGuiFn = void(__fastcall*)(const void* self, void* edx, const SurfaceDesc* desc, std::uint32_t pos,
                                    ClipRect clip, std::uint32_t brightness);
using DrawColorFn = void(__fastcall*)(const void* self, void* edx, const SurfaceDesc* desc, const std::uint32_t* pos,
                                      const ClipRect* clip, std::uint32_t palette);
using DrawShadowFn = void(__fastcall*)(const void* self, void* edx, const SurfaceDesc* desc, const std::uint32_t* pos,
                                       const ClipRect* clip, std::int32_t shift, std::uint32_t fraction);

S3RE_HOOK(0x005fadb0, GfxDword_DrawSprite, DrawSpriteFn, Original_GfxDword_DrawSprite, GfxDword_DrawSprite);

// Purpose: draws a 16-bit sprite (buildings, settlers, objects) at a
// position and brightness, clipped, optionally only partly (a building
// under construction rising from the ground).
void __fastcall GfxDword_DrawSprite(const void* self, void* /*edx*/, const SurfaceDesc* desc, const std::uint32_t* pos,
                                    const ClipRect* clip, std::uint32_t brightness, std::uint32_t fraction) {
    reinterpret_cast<SelectFn>(kSelectBrightnessSpans)(brightness);
    const std::uint32_t image = static_cast<const GfxDword*>(self)->image;
    std::uint32_t offset = *reinterpret_cast<const std::uint32_t*>(image + 8);
    std::uint32_t position;
    AddPoints(pos, &position, &offset);
    std::uint32_t sizeCell;
    std::uint32_t size = CallGetter1(kPointAt4, self, &sizeCell);
    const std::int32_t height = PointY(size);
    std::int32_t visible;
    std::int32_t hidden;
    if (fraction != 0 && fraction != kWholeSprite) {
        // The signed 32-bit product's low bits, logically shifted (0x5fae0d).
        const std::uint32_t scaled = (static_cast<std::uint32_t>(height + kDissolveRows) * fraction) >> 16;
        const std::uint32_t kept = static_cast<std::uint32_t>(height) < scaled ? static_cast<std::uint32_t>(height) : scaled;
        const std::uint32_t hiddenRows = static_cast<std::uint32_t>(height) - kept;
        size = WithY(size, kept);
        position = WithY(position, static_cast<std::uint32_t>(PointY(position)) + hiddenRows);
        visible = static_cast<std::int32_t>(scaled);
        hidden = static_cast<std::int16_t>(hiddenRows & 0xffff);
    } else {
        visible = height + kDissolveRows;
        hidden = 0;
    }
    const std::int32_t x = PointX(position);
    const std::int32_t width = PointX(size);
    if (!OverlapsClip(x, PointY(position), width, PointY(size), *clip) || fraction == 0) {
        return;
    }
    std::uintptr_t blitter;
    if (x < clip->left) {
        blitter = x + width < clip->right ? kBlitRle16ClipLeft : kBlitRle16ClipBoth;
    } else if (x + width >= clip->right) {
        blitter = kBlitRle16ClipRight;
    } else {
        blitter = (brightness & ~7u) == kFullBrightness ? kBlitRle16FullBright : kBlitRle16Shaded;
    }
    const ClipRect clipCopy = *clip;
    const std::uint32_t rle = CallGetter0(kSpriteRleData16, self);
    reinterpret_cast<Blit16Fn>(blitter)(visible, hidden, *desc, position, size, rle, clipCopy);
}

S3RE_HOOK(0x005f64c0, GfxDword_DrawGuiImage, DrawGuiFn, Original_GfxDword_DrawGuiImage, GfxDword_DrawGuiImage);

// Purpose: draws a GUI image (menus, buttons, panels) at a position and
// brightness, clipped.
void __fastcall GfxDword_DrawGuiImage(const void* self, void* /*edx*/, const SurfaceDesc* desc, std::uint32_t pos,
                                      ClipRect clip, std::uint32_t brightness) {
    MakePoint(0, 0);
    std::uint32_t offset;
    CallGetter1(kPointAt4, self, &offset);
    AddPoints(&pos, &offset, &offset);
    const std::uint32_t position = offset;
    reinterpret_cast<SelectFn>(kSelectBrightnessSpans)(brightness);
    const std::int32_t x = PointX(position);
    const std::int32_t y = PointY(position);
    std::uint32_t cell;
    if (x >= clip.right || x + PointX(CallGetter1(kPointAt0, self, &cell)) < clip.left || y >= clip.bottom ||
        y + PointY(CallGetter1(kPointAt0, self, &cell)) < clip.top) {
        return;
    }
    std::uintptr_t blitter;
    const std::int32_t width = PointX(CallGetter1(kPointAt0, self, &cell));
    if (x < clip.left) {
        blitter = x + width < clip.right ? kBlitRle16ClipLeft : kBlitRle16ClipBoth;
    } else if (x + width >= clip.right) {
        blitter = kBlitRle16ClipRight;
    } else {
        blitter = (brightness & ~7u) == kFullBrightness ? kBlitRle16FullBright : kBlitRle16Shaded;
    }
    const ClipRect clipCopy = clip;
    const std::uint32_t rle = CallGetter0(kGuiRleData, self);
    const std::int32_t visible = PointY(CallGetter1(kPointAt0, self, &cell)) + kDissolveRows;
    const std::uint32_t size = CallGetter1(kPointAt0, self, &cell);
    reinterpret_cast<Blit16Fn>(blitter)(visible, 0, *desc, position, size, rle, clipCopy);
}

S3RE_HOOK(0x005fc5f0, GfxDword_DrawColorIndexed, DrawColorFn, Original_GfxDword_DrawColorIndexed,
          GfxDword_DrawColorIndexed);

// Purpose: draws the player-colour layer of a sprite (8-bit indexes into a
// palette of the player's colours).
void __fastcall GfxDword_DrawColorIndexed(const void* self, void* /*edx*/, const SurfaceDesc* desc,
                                          const std::uint32_t* pos, const ClipRect* clip, std::uint32_t palette) {
    MakePoint(0, 0);
    const std::uint32_t image = static_cast<const GfxDword*>(self)->image;
    std::uint32_t offset = *reinterpret_cast<const std::uint32_t*>(image + 8);
    std::uint32_t position;
    AddPoints(pos, &position, &offset);
    const std::int32_t x = PointX(position);
    const std::int32_t y = PointY(position);
    std::uint32_t cell;
    if (x >= clip->right || PointX(CallGetter1(kPointAt4, self, &cell)) + x < clip->left || y >= clip->bottom ||
        PointY(CallGetter1(kPointAt4, self, &cell)) + y < clip->top) {
        return;
    }
    std::uintptr_t blitter;
    if (x < clip->left) {
        blitter = kBlitColorClipLeft;
    } else if (PointX(CallGetter1(kPointAt4, self, &cell)) + x >= clip->right) {
        blitter = kBlitColorClipRight;
    } else {
        blitter = kBlitColorUnclipped;
    }
    const ClipRect clipCopy = *clip;
    const std::uint32_t rle = CallGetter0(kSpriteRleData, self);
    const std::uint32_t size = CallGetter1(kPointAt4, self, &cell);
    reinterpret_cast<ColorBlitFn>(blitter)(rle, palette, *desc, position, size, clipCopy);
}

S3RE_HOOK(0x005fcdc0, GfxDword_DrawShadow, DrawShadowFn, Original_GfxDword_DrawShadow, GfxDword_DrawShadow);

// Purpose: draws a sprite's shadow (darkens what is below it), clipped,
// optionally only its bottom part (construction).
void __fastcall GfxDword_DrawShadow(const void* self, void* /*edx*/, const SurfaceDesc* desc, const std::uint32_t* pos,
                                    const ClipRect* clip, std::int32_t shift, std::uint32_t fraction) {
    MakePoint(0, 0);
    MakePoint(0, 0);
    MakePoint(0, 0);
    const std::uint32_t image = static_cast<const GfxDword*>(self)->image;
    std::uint32_t offset = *reinterpret_cast<const std::uint32_t*>(image + 8);
    std::uint32_t position;
    AddPoints(pos, &position, &offset);
    std::uint32_t sizeCell;
    std::uint32_t size = CallGetter1(kPointAt4, self, &sizeCell);
    std::int32_t hidden = 0;
    if (fraction != kWholeSprite && fraction != 0) {
        const std::uint32_t kept = (static_cast<std::uint32_t>(static_cast<std::int32_t>(PointY(size))) * fraction) >> 16;
        const std::uint32_t hiddenRows = static_cast<std::uint32_t>(PointY(size)) - kept;
        position = WithY(position, static_cast<std::uint32_t>(PointY(position)) + hiddenRows);
        size = WithY(size, kept);
        hidden = static_cast<std::int16_t>(hiddenRows & 0xffff);
    }
    std::uint32_t shiftPoint = MakePoint(static_cast<std::uint32_t>(shift), 0);
    std::uint32_t origin;
    AddPoints(&position, &origin, &shiftPoint);
    const std::int32_t x = PointX(position);
    const std::int32_t width = PointX(size);
    if (!OverlapsClip(x, PointY(position), width, PointY(size), *clip) || fraction == 0) {
        return;
    }
    std::uintptr_t blitter;
    if (x < clip->left) {
        blitter = kBlitShadowClipLeft;
    } else if (x + width >= clip->right) {
        blitter = kBlitShadowClipRight;
    } else {
        blitter = kBlitShadowUnclipped;
    }
    const ClipRect clipCopy = *clip;
    const std::uint32_t rle = CallGetter0(kSpriteRleData, self);
    reinterpret_cast<ShadowBlitFn>(blitter)(hidden, rle, *desc, position, size, clipCopy, origin);
}

// ---------------------------------------------------------------------------
// Colour-indexed blitters
// ---------------------------------------------------------------------------

namespace {

enum class Clip { None, Left, Right };

void BlitColorIndexed(std::uint32_t rle, std::uint32_t palette, const SurfaceDesc& desc, std::uint32_t pos,
                      std::uint32_t size, const ClipRect& clip, Clip side) {
    std::uint32_t dstRow = SpriteRowStart(desc, pos);
    std::int32_t skipTop;
    std::int32_t rows;
    reinterpret_cast<ClipFn>(kColorIndexedVerticalClip)(&skipTop, &rows, pos, size, clip.left, clip.top, clip.right,
                                                        clip.bottom);
    reinterpret_cast<SkipRows8Fn>(kColorIndexedSkipRows)(&desc, skipTop, &rle, &dstRow);
    const std::int32_t x = PointX(pos);
    const std::uint32_t edgeOffset = static_cast<std::uint32_t>((side == Clip::Left ? clip.left : clip.right) - x) * 2;
    const auto copy = [palette, &rle](std::uint32_t& dst, std::uint32_t pixels) {
        for (std::uint32_t i = 0; i < pixels; ++i, dst += 2, ++rle) {
            *reinterpret_cast<std::uint16_t*>(dst) =
                *reinterpret_cast<const std::uint16_t*>(palette + ByteAt(rle) * 2);
        }
    };
    for (std::int32_t row = 0; row < rows; ++row) {
        std::uint32_t dst = dstRow;
        const std::uint32_t edge = dstRow + edgeOffset;
        Run run;
        do {
            run = ReadRun(&rle);
            dst += run.skip * 2;
            const std::uint32_t end = dst + run.count * 2;
            if (side == Clip::None) {
                copy(dst, run.count);
            } else if (side == Clip::Right) {
                // 0x5fca8a..
                if (end < edge) {
                    copy(dst, run.count);
                } else if (dst >= edge) {
                    rle += run.count;  // dst is not advanced (quirk)
                } else {
                    const std::uint32_t cut = static_cast<std::uint32_t>(static_cast<std::int32_t>(edge - dst) >> 1);
                    copy(dst, cut);
                    rle += run.count - cut;
                }
            } else {
                // 0x5fcc1e..
                if (end <= edge) {
                    rle += run.count;
                    dst = end;
                } else if (dst >= edge) {
                    copy(dst, run.count);
                } else {
                    const std::uint32_t cut = static_cast<std::uint32_t>(static_cast<std::int32_t>(end - edge) >> 1);
                    const std::uint32_t hiddenPixels = run.count - cut;
                    rle += hiddenPixels;
                    dst += hiddenPixels * 2;
                    copy(dst, cut);
                }
            }
        } while (!run.last);
        dstRow += RowStep(desc);
    }
}

}  // namespace

void __fastcall BlitColorIndexed_Unclipped(std::uint32_t rle, std::uint32_t palette, SurfaceDesc desc,
                                           std::uint32_t pos, std::uint32_t size, ClipRect clip);
void __fastcall BlitColorIndexed_ClipRight(std::uint32_t rle, std::uint32_t palette, SurfaceDesc desc,
                                           std::uint32_t pos, std::uint32_t size, ClipRect clip);
void __fastcall BlitColorIndexed_ClipLeft(std::uint32_t rle, std::uint32_t palette, SurfaceDesc desc,
                                          std::uint32_t pos, std::uint32_t size, ClipRect clip);

S3RE_HOOK(0x005fc860, BlitColorIndexed_Unclipped, ColorBlitFn, Original_BlitColorIndexed_Unclipped,
          BlitColorIndexed_Unclipped);

// Purpose: draws a player-colour layer that lies inside the clip's columns.
void __fastcall BlitColorIndexed_Unclipped(std::uint32_t rle, std::uint32_t palette, SurfaceDesc desc,
                                           std::uint32_t pos, std::uint32_t size, ClipRect clip) {
    BlitColorIndexed(rle, palette, desc, pos, size, clip, Clip::None);
}

S3RE_HOOK(0x005fc9a0, BlitColorIndexed_ClipRight, ColorBlitFn, Original_BlitColorIndexed_ClipRight,
          BlitColorIndexed_ClipRight);

// Purpose: draws a player-colour layer cut off by the clip's right edge.
void __fastcall BlitColorIndexed_ClipRight(std::uint32_t rle, std::uint32_t palette, SurfaceDesc desc,
                                           std::uint32_t pos, std::uint32_t size, ClipRect clip) {
    BlitColorIndexed(rle, palette, desc, pos, size, clip, Clip::Right);
}

S3RE_HOOK(0x005fcb30, BlitColorIndexed_ClipLeft, ColorBlitFn, Original_BlitColorIndexed_ClipLeft,
          BlitColorIndexed_ClipLeft);

// Purpose: draws a player-colour layer cut off by the clip's left edge.
void __fastcall BlitColorIndexed_ClipLeft(std::uint32_t rle, std::uint32_t palette, SurfaceDesc desc,
                                          std::uint32_t pos, std::uint32_t size, ClipRect clip) {
    BlitColorIndexed(rle, palette, desc, pos, size, clip, Clip::Left);
}

// ---------------------------------------------------------------------------
// Shadow blitters
// ---------------------------------------------------------------------------

namespace {

// C's x % 32, made non-negative (0x5fd1bb..0x5fd21c).
std::int32_t PositiveMod32(std::int32_t value) {
    std::int32_t rest = value % 32;
    if (rest < 0) {
        rest += 32;
    }
    return rest % 32;
}

void ShadowSpan(std::uint32_t* dst, std::uint32_t end, std::uint32_t* phase, std::uint32_t parity) {
    reinterpret_cast<ShadowSpanFn>(static_cast<std::uint32_t>(g_shadowSpan->span))(dst, end, phase, parity);
}

void BlitShadow(std::int32_t hidden, std::uint32_t rle, const SurfaceDesc& desc, std::uint32_t pos, std::uint32_t size,
                const ClipRect& clip, std::uint32_t origin, Clip side) {
    std::uint32_t dstRow = SpriteRowStart(desc, pos);
    std::int32_t skipTop;
    std::int32_t rows;
    reinterpret_cast<ClipFn>(kShadowVerticalClip)(&skipTop, &rows, pos, size, clip.left, clip.top, clip.right,
                                                  clip.bottom);
    reinterpret_cast<SkipRowsFn>(kShadowSkipRows)(&desc, skipTop, hidden, &rle, &dstRow);
    const std::int32_t rowPhase = PositiveMod32(PointY(origin));
    const std::uint32_t startPhase = static_cast<std::uint32_t>(PositiveMod32(PointX(origin)));
    std::int32_t parity = (rowPhase + skipTop) % 32;
    const std::int32_t x = PointX(pos);
    const std::uint32_t edgeOffset = static_cast<std::uint32_t>((side == Clip::Left ? clip.left : clip.right) - x) * 2;
    for (std::int32_t row = 0; row < rows; ++row) {
        std::uint32_t dst = dstRow;
        std::uint32_t phase = startPhase;
        const std::uint32_t edge = dstRow + edgeOffset;
        const std::uint32_t parityArg = static_cast<std::uint32_t>(parity);
        Run run;
        do {
            run = ReadRun(&rle);
            dst += run.skip * 2;
            phase = (run.skip + phase) & kShadowPhaseWrap;
            const std::uint32_t end = dst + run.count * 2;
            if (side == Clip::None) {
                ShadowSpan(&dst, end, &phase, parityArg);
            } else if (side == Clip::Right) {
                // 0x5fd487..
                if (end < edge) {
                    ShadowSpan(&dst, end, &phase, parityArg);
                } else if (dst >= edge) {
                    phase = (run.count + phase) & kShadowPhaseWrap;  // dst is not advanced (quirk)
                } else {
                    const std::uint32_t cut = static_cast<std::uint32_t>(static_cast<std::int32_t>(edge - dst) >> 1);
                    ShadowSpan(&dst, dst + cut * 2, &phase, parityArg);
                    phase = (run.count - cut + phase) & kShadowPhaseWrap;
                }
            } else {
                // 0x5fd6ee..
                if (end <= edge) {
                    dst = end;
                    phase = (phase + run.count) & kShadowPhaseWrap;
                } else if (dst >= edge) {
                    ShadowSpan(&dst, end, &phase, parityArg);
                } else {
                    const std::uint32_t cut = static_cast<std::uint32_t>(static_cast<std::int32_t>(end - edge) >> 1);
                    const std::uint32_t hiddenPixels = run.count - cut;
                    phase = (hiddenPixels + phase) & kShadowPhaseWrap;
                    dst += hiddenPixels * 2;
                    ShadowSpan(&dst, dst + cut * 2, &phase, parityArg);
                }
            }
        } while (!run.last);
        dstRow += RowStep(desc);
        parity = (parity + 1) % 32;
    }
}

}  // namespace

void __fastcall BlitShadow_Unclipped(std::int32_t hidden, std::uint32_t rle, SurfaceDesc desc, std::uint32_t pos,
                                     std::uint32_t size, ClipRect clip, std::uint32_t origin);
void __fastcall BlitShadow_ClipRight(std::int32_t hidden, std::uint32_t rle, SurfaceDesc desc, std::uint32_t pos,
                                     std::uint32_t size, ClipRect clip, std::uint32_t origin);
void __fastcall BlitShadow_ClipLeft(std::int32_t hidden, std::uint32_t rle, SurfaceDesc desc, std::uint32_t pos,
                                    std::uint32_t size, ClipRect clip, std::uint32_t origin);

S3RE_HOOK(0x005fd120, BlitShadow_Unclipped, ShadowBlitFn, Original_BlitShadow_Unclipped, BlitShadow_Unclipped);

// Purpose: draws a shadow that lies inside the clip's columns.
void __fastcall BlitShadow_Unclipped(std::int32_t hidden, std::uint32_t rle, SurfaceDesc desc, std::uint32_t pos,
                                     std::uint32_t size, ClipRect clip, std::uint32_t origin) {
    BlitShadow(hidden, rle, desc, pos, size, clip, origin, Clip::None);
}

S3RE_HOOK(0x005fd310, BlitShadow_ClipRight, ShadowBlitFn, Original_BlitShadow_ClipRight, BlitShadow_ClipRight);

// Purpose: draws a shadow cut off by the clip's right edge.
void __fastcall BlitShadow_ClipRight(std::int32_t hidden, std::uint32_t rle, SurfaceDesc desc, std::uint32_t pos,
                                     std::uint32_t size, ClipRect clip, std::uint32_t origin) {
    BlitShadow(hidden, rle, desc, pos, size, clip, origin, Clip::Right);
}

S3RE_HOOK(0x005fd570, BlitShadow_ClipLeft, ShadowBlitFn, Original_BlitShadow_ClipLeft, BlitShadow_ClipLeft);

// Purpose: draws a shadow cut off by the clip's left edge.
void __fastcall BlitShadow_ClipLeft(std::int32_t hidden, std::uint32_t rle, SurfaceDesc desc, std::uint32_t pos,
                                    std::uint32_t size, ClipRect clip, std::uint32_t origin) {
    BlitShadow(hidden, rle, desc, pos, size, clip, origin, Clip::Left);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Every colour-indexed, shadow, 16-bit and GUI image of the real GFX files
// (gfx_test_sprites.h) plus synthetic sprites, drawn by the original and by
// ours into two copies of a random 16-bit surface (with guard bytes) with
// random clip rectangles, positions, brightness, fractions, palettes,
// shadow methods and dither origins; the whole surfaces are compared. The
// blitter tests call the blitters directly with positions classified the
// way the Draw methods would; the Draw tests call the Draw methods (whose
// blitters are the installed ones on both sides).

namespace {

using gfx_test::LoadGfxSprites;
using gfx_test::MakeRle;

std::uint32_t Address(const void* p) {
    return reinterpret_cast<std::uint32_t>(p);
}

constexpr std::int32_t kSurfaceWidth = 800;
constexpr std::int32_t kSurfaceHeight = 600;
constexpr std::uint32_t kGuardBytes = 0x4000;
constexpr std::uint32_t kSurfaceBytes = kGuardBytes * 2 + (kSurfaceWidth + 8) * 2 * kSurfaceHeight;

struct Surfaces {
    std::vector<std::uint8_t> a = std::vector<std::uint8_t>(kSurfaceBytes);
    std::vector<std::uint8_t> b = std::vector<std::uint8_t>(kSurfaceBytes);
};

Surfaces& TheSurfaces() {
    static Surfaces surfaces;
    return surfaces;
}

// A random clip rectangle and a position whose sprite (w x h) overlaps it
// and stays inside the surface; false if the draw falls outside.
bool RandomPlacement(test::Rng& rng, std::int32_t width, std::int32_t height, ClipRect* clip, std::int32_t* x,
                     std::int32_t* y) {
    clip->left = static_cast<std::int32_t>(rng.Below(200));
    clip->top = static_cast<std::int32_t>(rng.Below(150));
    clip->right = clip->left + 1 + static_cast<std::int32_t>(rng.Below(kSurfaceWidth - clip->left));
    clip->bottom = clip->top + 1 + static_cast<std::int32_t>(rng.Below(kSurfaceHeight - clip->top));
    *x = clip->left - width + static_cast<std::int32_t>(rng.Below(clip->right - clip->left + width));
    *y = clip->top - height + static_cast<std::int32_t>(rng.Below(clip->bottom - clip->top + height));
    return *x >= 0 && *x + width <= kSurfaceWidth && *y >= 0 && *y + height <= kSurfaceHeight;
}

std::uint32_t MakePos(std::int32_t x, std::int32_t y) {
    return static_cast<std::uint16_t>(x) | static_cast<std::uint32_t>(static_cast<std::uint16_t>(y)) << 16;
}

SurfaceDesc RandomDesc(test::Rng& rng) {
    SurfaceDesc desc{};
    rng.Fill(&desc, sizeof(desc));
    desc.pitch = (kSurfaceWidth + 2 * static_cast<std::int32_t>(rng.Below(4))) * 2;
    return desc;
}

void ResetSurfaces(test::Rng& rng) {
    const int fill = static_cast<int>(rng.Below(256));
    std::memset(TheSurfaces().a.data(), fill, kSurfaceBytes);
    std::memset(TheSurfaces().b.data(), fill, kSurfaceBytes);
}

std::uint32_t SurfaceA() {
    return Address(TheSurfaces().a.data()) + kGuardBytes;
}
std::uint32_t SurfaceB() {
    return Address(TheSurfaces().b.data()) + kGuardBytes;
}

bool SameSurfaces(test::Context& ctx) {
    return S3RE_EXPECT(std::memcmp(TheSurfaces().a.data(), TheSurfaces().b.data(), kSurfaceBytes) == 0);
}

struct SavedGraphicsState {
    std::uint32_t shade = g_shadeSpan;
    std::uint32_t masked = g_maskedSpan;
    ShadeMasks masks = g_shadeMasks;
    ShadowSpanState shadow = g_shadowSpan;
    SavedGraphicsState() {
        static constexpr std::uint32_t k565[5] = {0x7bef, 0x39e7, 0x18e3, 0x861, 0x20};
        for (int k = 0; k < 5; ++k) {
            g_shadeMasks->pixel[k] = k565[k];
            g_shadeMasks->pair[k] = k565[k] << 16 | k565[k];
        }
    }
    ~SavedGraphicsState() {
        g_shadeSpan = shade;
        g_maskedSpan = masked;
        g_shadeMasks = masks;
        g_shadowSpan = shadow;
    }
};

std::uint16_t ImageWidth(std::uint32_t image, std::uint32_t sizeOffset) {
    return *reinterpret_cast<const std::uint16_t*>(image + sizeOffset);
}
std::uint16_t ImageHeight(std::uint32_t image, std::uint32_t sizeOffset) {
    return *reinterpret_cast<const std::uint16_t*>(image + sizeOffset + 2);
}
std::uint32_t SpriteRle(std::uint32_t image) {
    return (image + 0xd + ByteAt(image + 0xc) + 1) & ~1u;
}

// --- colour-indexed and shadow blitters ----------------------------------------

struct LayerBlitters {
    ColorBlitFn color[3];   // Unclipped, ClipRight, ClipLeft
    ShadowBlitFn shadow[3];
};

constexpr LayerBlitters kOurLayerBlitters{
    {BlitColorIndexed_Unclipped, BlitColorIndexed_ClipRight, BlitColorIndexed_ClipLeft},
    {BlitShadow_Unclipped, BlitShadow_ClipRight, BlitShadow_ClipLeft}};

int PickSide(std::int32_t x, std::int32_t width, const ClipRect& clip) {
    if (x < clip.left) {
        return 2;
    }
    return x + width >= clip.right ? 1 : 0;
}

bool CompareLayerSprite(test::Context& ctx, bool shadow, std::uint32_t rle, std::int32_t width, std::int32_t height,
                        const LayerBlitters& ours) {
    static const LayerBlitters kOriginals{
        {Original_BlitColorIndexed_Unclipped, Original_BlitColorIndexed_ClipRight, Original_BlitColorIndexed_ClipLeft},
        {Original_BlitShadow_Unclipped, Original_BlitShadow_ClipRight, Original_BlitShadow_ClipLeft}};
    test::Rng& rng = ctx.rng();
    ClipRect clip;
    std::int32_t x;
    std::int32_t y;
    if (!RandomPlacement(rng, width, height, &clip, &x, &y)) {
        return true;
    }
    const int side = PickSide(x, width, clip);
    SurfaceDesc desc = RandomDesc(rng);
    ResetSurfaces(rng);
    if (!shadow) {
        static std::uint16_t palette[256];
        rng.Fill(palette, sizeof(palette));
        const std::uint32_t pos = MakePos(x, y);
        const std::uint32_t size = static_cast<std::uint32_t>(width) | static_cast<std::uint32_t>(height) << 16;
        desc.surface = SurfaceA();
        kOriginals.color[side](rle, Address(palette), desc, pos, size, clip);
        desc.surface = SurfaceB();
        ours.color[side](rle, Address(palette), desc, pos, size, clip);
    } else {
        // Construction: the kept rows, the hidden ones skipped.
        std::int32_t hidden = 0;
        std::int32_t kept = height;
        if (rng.Chance(1, 3)) {
            kept = static_cast<std::int32_t>(rng.Below(static_cast<std::uint32_t>(height) + 1));
            hidden = height - kept;
        }
        const std::uint32_t pos = MakePos(x, y + hidden);
        const std::uint32_t size = static_cast<std::uint32_t>(width) | static_cast<std::uint32_t>(kept) << 16;
        const std::uint32_t origin = MakePos(x + static_cast<std::int32_t>(rng.Below(80)) - 40, y + hidden);
        g_shadowSpan->span = rng.Chance(1, 2) ? 0x005fd080u : 0x005fd0c0u;
        desc.surface = SurfaceA();
        kOriginals.shadow[side](hidden, rle, desc, pos, size, clip, origin);
        desc.surface = SurfaceB();
        ours.shadow[side](hidden, rle, desc, pos, size, clip, origin);
    }
    if (!SameSurfaces(ctx)) {
        ctx.Fail(__FILE__, __LINE__, "  %s side %d, %dx%d at (%d, %d), clip (%d %d %d %d)", shadow ? "shadow" : "colour",
                 side, width, height, x, y, clip.left, clip.top, clip.right, clip.bottom);
        return false;
    }
    return true;
}

void CompareLayerBlitters(test::Context& ctx, const LayerBlitters& ours, std::uint32_t synthetic, bool real) {
    SavedGraphicsState saved;
    bool ok = true;
    if (real) {
        const auto& sprites = LoadGfxSprites();
        S3RE_EXPECT(sprites.images[1].size() > 500);
        S3RE_EXPECT(sprites.images[2].size() > 500);
        for (int kind = 1; ok && kind <= 2; ++kind) {
            for (std::size_t i = 0; ok && i < sprites.images[kind].size(); ++i) {
                const std::uint32_t image = sprites.images[kind][i];
                const std::int32_t width = ImageWidth(image, 4);
                const std::int32_t height = ImageHeight(image, 4);
                if (width == 0 || height == 0 || width > 600 || height > 500) {
                    continue;
                }
                ok = CompareLayerSprite(ctx, kind == 2, SpriteRle(image), width, height, ours);
            }
        }
    }
    for (std::uint32_t i = 0; ok && i < synthetic; ++i) {
        const bool shadow = (i & 1) != 0;
        const std::uint32_t width = 1 + ctx.rng().Below(120);
        const std::uint32_t height = 1 + ctx.rng().Below(60);
        const std::vector<std::uint8_t> data = MakeRle(ctx.rng(), width, height, shadow ? 0 : 1);
        ok = CompareLayerSprite(ctx, shadow, Address(data.data()), static_cast<std::int32_t>(width),
                                static_cast<std::int32_t>(height), ours);
    }
}

// Wrong: the shadow ClipLeft starts the dither phase at 0 on every row.
void __fastcall WrongShadowClipLeft(std::int32_t hidden, std::uint32_t rle, SurfaceDesc desc, std::uint32_t pos,
                                    std::uint32_t size, ClipRect clip, std::uint32_t origin) {
    BlitShadow(hidden, rle, desc, pos, size, clip, origin & 0xffff0000u, Clip::Left);
}

// --- the Draw methods ----------------------------------------------------------

struct DrawFunctions {
    DrawSpriteFn sprite;
    DrawGuiFn gui;
    DrawColorFn color;
    DrawShadowFn shadow;
};

constexpr DrawFunctions kOurDraws{GfxDword_DrawSprite, GfxDword_DrawGuiImage, GfxDword_DrawColorIndexed,
                                  GfxDword_DrawShadow};

std::uint32_t RandomFraction(test::Rng& rng) {
    // The game passes 0..0x10000 (larger values would draw past the sprite).
    switch (rng.Below(4)) {
    case 0: return 0;
    case 1: return 0x10000;
    default: return rng.Below(0x10001);
    }
}

// kind: 0 sprite, 1 colour-indexed, 2 shadow, 3 GUI.
bool CompareDraw(test::Context& ctx, int kind, std::uint32_t image, const DrawFunctions& ours) {
    static const DrawFunctions kOriginals{Original_GfxDword_DrawSprite, Original_GfxDword_DrawGuiImage,
                                          Original_GfxDword_DrawColorIndexed, Original_GfxDword_DrawShadow};
    test::Rng& rng = ctx.rng();
    const std::uint32_t sizeOffset = kind == 3 ? 0 : 4;
    const std::uint32_t offsetOffset = kind == 3 ? 4 : 8;
    const std::int32_t width = ImageWidth(image, sizeOffset);
    const std::int32_t height = ImageHeight(image, sizeOffset);
    if (width == 0 || height == 0 || width > 600 || height > 500) {
        return true;
    }
    ClipRect clip;
    std::int32_t x;
    std::int32_t y;
    if (!RandomPlacement(rng, width, height, &clip, &x, &y)) {
        return true;
    }
    // The drawn position is pos + the image's offset.
    const std::int16_t offsetX = *reinterpret_cast<const std::int16_t*>(image + offsetOffset);
    const std::int16_t offsetY = *reinterpret_cast<const std::int16_t*>(image + offsetOffset + 2);
    const std::uint32_t pos = MakePos(x - offsetX, y - offsetY);
    const GfxDword ref{image};
    SurfaceDesc descA = RandomDesc(rng);
    SurfaceDesc descB = descA;
    descA.surface = SurfaceA();
    descB.surface = SurfaceB();
    ResetSurfaces(rng);
    const std::uint32_t brightness = rng.Chance(1, 4) ? 248 + rng.Below(8) : rng.Below(256);
    const std::uint32_t fraction = RandomFraction(rng);
    switch (kind) {
    case 0:
        kOriginals.sprite(&ref, nullptr, &descA, &pos, &clip, brightness, fraction);
        ours.sprite(&ref, nullptr, &descB, &pos, &clip, brightness, fraction);
        break;
    case 1: {
        static std::uint16_t palette[256];
        rng.Fill(palette, sizeof(palette));
        kOriginals.color(&ref, nullptr, &descA, &pos, &clip, Address(palette));
        ours.color(&ref, nullptr, &descB, &pos, &clip, Address(palette));
        break;
    }
    case 2: {
        g_shadowSpan->span = rng.Chance(1, 2) ? 0x005fd080u : 0x005fd0c0u;
        const std::int32_t shift = static_cast<std::int32_t>(rng.Below(200)) - 100;
        kOriginals.shadow(&ref, nullptr, &descA, &pos, &clip, shift, fraction);
        ours.shadow(&ref, nullptr, &descB, &pos, &clip, shift, fraction);
        break;
    }
    default:
        kOriginals.gui(&ref, nullptr, &descA, pos, clip, brightness);
        ours.gui(&ref, nullptr, &descB, pos, clip, brightness);
        break;
    }
    if (!SameSurfaces(ctx)) {
        ctx.Fail(__FILE__, __LINE__, "  kind %d, %dx%d at (%d, %d), clip (%d %d %d %d), brightness %u, fraction 0x%x",
                 kind, width, height, x, y, clip.left, clip.top, clip.right, clip.bottom, brightness, fraction);
        return false;
    }
    return true;
}

void CompareDraws(test::Context& ctx, const DrawFunctions& ours, std::size_t stride) {
    SavedGraphicsState saved;
    const auto& sprites = LoadGfxSprites();
    S3RE_EXPECT(sprites.images[3].size() > 100);
    bool ok = true;
    for (int kind = 0; ok && kind < 4; ++kind) {
        for (std::size_t i = 0; ok && i < sprites.images[kind].size(); i += stride) {
            ok = CompareDraw(ctx, kind, sprites.images[kind][i], ours);
        }
    }
}

// Wrong: DrawSprite without the 8 dissolving rows.
void __fastcall WrongDrawSprite(const void* self, void* edx, const SurfaceDesc* desc, const std::uint32_t* pos,
                                const ClipRect* clip, std::uint32_t brightness, std::uint32_t fraction) {
    GfxDword_DrawSprite(self, edx, desc, pos, clip, brightness, fraction == 0 || fraction == 0x10000 ? 0xffff : fraction);
}

}  // namespace

S3RE_TEST(GfxLayerBlitters_MatchOriginal) {
    CompareLayerBlitters(ctx, kOurLayerBlitters, 20000, true);
}

S3RE_TEST(GfxLayerBlitters_ComparisonDetectsWrongImplementation) {
    LayerBlitters wrong = kOurLayerBlitters;
    wrong.shadow[2] = WrongShadowClipLeft;
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareLayerBlitters(probe, wrong, 3000, false);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GfxDraw_MatchOriginal) {
    CompareDraws(ctx, kOurDraws, 1);
}

S3RE_TEST(GfxDraw_ComparisonDetectsWrongImplementation) {
    DrawFunctions wrong = kOurDraws;
    wrong.sprite = WrongDrawSprite;
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDraws(probe, wrong, 7);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
