// RLE row handling and the 16-bit sprite blitters (Dirk\graphicsdata.cpp).
//
// RLE rows (re/graphics_formats.md): each row is a list of runs, a byte
// `count`, a byte `flags` (bits 0..6: transparent pixels before the run,
// bit 7: last run of the row), then `count` pixels (2 bytes each for 16-bit
// sprites, 1 byte for colour-indexed ones, none for shadows).
//
// The surface is a DDSURFACEDESC copied by value (0x6c bytes; lPitch +0x10,
// lpSurface +0x24). A sprite's top-left pixel is at lpSurface +
// (((pitch * y) >> 1) + x) * 2 (32-bit product, logical shift; x and y the
// signed 16-bit position); each row adds (pitch >> 1) * 2.
//
// Rle16VerticalClip (0x5fb050): fastcall (ECX = visible, EDX = &skipTop,
//   stack: pos, size (Point16 by value), clip rect (4 ints), &rowsAfter,
//   &masked, &maskStart), ret 0x24. Splits the sprite's rows into `masked`
//   dissolving rows at the top (up to 8, drawn through the dither mask
//   rows maskStart.., for a building under construction: visible < height)
//   and `rowsAfter` normal rows, after clipping `skipTop` rows at the clip
//   top and the rows below the clip bottom. Memory is read back where the
//   original reads it (the out pointers may alias).
// ColorIndexedVerticalClip (0x5fc7d0) / ShadowVerticalClip (0x5fcff0):
//   fastcall (ECX = &skipTop, EDX = &rows, stack: pos, size, clip rect),
//   ret 0x18: rows = height minus the rows above the clip top and below
//   the clip bottom (the shadow version adds to *skipTop after zeroing it
//   and subtracts the new value: the same unless the pointers alias).
// Rle16SkipRows (0x5fb130): fastcall (ECX = surface desc*, EDX = rowsA,
//   stack: rowsB, &rle, &dst), ret 0xc: skips rowsA + rowsB RLE rows
//   (unsigned count, 0 = none) and moves dst down by rowsA rows only
//   ((pitch * rowsA) >> 1) * 2; dst stored first, then rle.
// ColorIndexedSkipRows (0x5fc810): fastcall (ECX = desc*, EDX = rows,
//   stack: &rle, &dst), ret 8: the same for 1-byte pixels (signed count).
// ShadowSkipRows (0x5fd040): like Rle16SkipRows for shadow runs (2 bytes).
//
// The blitters (fastcall ECX = visible rows, EDX = hidden rows (skipped at
// the top, not moving dst), stack: surface desc by value, pos, size, RLE
// rows, clip rect by value), ret 0x88. Each calls Rle16VerticalClip and
// Rle16SkipRows, then draws the masked rows (dither mask row maskStart +
// row, phase reset per row and advanced by every transparent and drawn
// pixel) with g_maskedSpan and the normal rows with g_shadeSpan (pairs of
// pixels plus an odd one), both chosen by SelectBrightnessSpans:
//   BlitRle16_Shaded (0x5fc160): the sprite lies inside the clip's columns.
//   BlitRle16_FullBright (0x5fc350): the same with the spans inlined as
//     copies (brightness 248..255).
//   BlitRle16_ClipLeft (0x5fb4f0), ClipRight (0x5fbdf0), ClipBoth
//     (0x5fb850): runs are cut at clip left / right (edge = row +
//     (clip - x) * 2). Partly visible runs skip (pixels) or stop at the
//     edge. Quirk kept: in ClipRight a run starting at or past the right
//     edge advances the RLE pointer but not dst (the next runs are past the
//     edge anyway).
// The dither masks: g_ditherMasks (0x7a2b58), 8 rows of 16 bytes.

#include "game/globals.h"
#include "game/graphics/gfx_layout.h"
#include "game/graphics/gfx_test_sprites.h"
#include "game/tables.h"
#include "hooks.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace s3re::game {


using gfx::ClipRect;
using gfx::SurfaceDesc;

namespace {

constexpr std::uintptr_t kRle16VerticalClip = 0x005fb050;
constexpr std::uintptr_t kRle16SkipRows = 0x005fb130;

// The run header: count, flags.
constexpr std::uint8_t kLastRun = 0x80;
constexpr std::uint8_t kSkipMask = 0x7f;
constexpr std::uint32_t kMaskPhaseWrap = 15;
constexpr std::uint32_t kDitherRowBytes = 16;
// Dissolving rows at the top of a partly built sprite.
constexpr std::int32_t kMaxMaskedRows = 8;

std::int16_t PointX(std::uint32_t point) {
    return static_cast<std::int16_t>(point & 0xffff);
}
std::int16_t PointY(std::uint32_t point) {
    return static_cast<std::int16_t>(point >> 16);
}
std::uint8_t ByteAt(std::uint32_t address) {
    return *reinterpret_cast<const std::uint8_t*>(address);
}
std::uint16_t WordAt(std::uint32_t address) {
    return *reinterpret_cast<const std::uint16_t*>(address);
}
std::uint32_t DwordAt(std::uint32_t address) {
    return *reinterpret_cast<const std::uint32_t*>(address);
}

// lpSurface + ((pitch * y) >> 1 + x) * 2 (0x5fc168..0x5fc18f).
std::uint32_t SpriteRowStart(const SurfaceDesc& desc, std::uint32_t pos) {
    const std::uint32_t product = static_cast<std::uint32_t>(desc.pitch * PointY(pos));
    return desc.surface + ((product >> 1) + static_cast<std::uint32_t>(PointX(pos))) * 2;
}

std::uint32_t RowStep(const SurfaceDesc& desc) {
    return (static_cast<std::uint32_t>(desc.pitch) >> 1) * 2;
}

std::uint32_t DitherMaskRow(std::int32_t row) {
    return static_cast<std::uint32_t>(g_ditherMasks.address()) + static_cast<std::uint32_t>(row) * kDitherRowBytes;
}

using MaskedSpanFn = void(__fastcall*)(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                       std::uint32_t mask, std::uint32_t* maskIndex);
using ShadeSpanFn = void(__fastcall*)(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                      std::uint32_t odd);

void MaskedSpan(std::uint32_t* dst, std::uint32_t end, std::uint32_t* src, std::uint32_t mask, std::uint32_t* phase) {
    reinterpret_cast<MaskedSpanFn>(static_cast<std::uint32_t>(g_maskedSpan))(dst, end, src, mask, phase);
}

// The shade span for `pixels` pixels: pairs, then the odd one.
void ShadeSpan(std::uint32_t* dst, std::uint32_t pixels, std::uint32_t* src) {
    reinterpret_cast<ShadeSpanFn>(static_cast<std::uint32_t>(g_shadeSpan))(dst, *dst + (pixels & 0xfe) * 2, src,
                                                                          pixels & 1);
}

// The rows of a sprite after vertical clipping (Rle16VerticalClip's outputs).
struct Rows {
    std::int32_t skipTop;
    std::int32_t rowsAfter;
    std::int32_t masked;
    std::int32_t maskStart;
};

using VerticalClipFn = void(__fastcall*)(std::int32_t visible, std::int32_t* skipTop, std::uint32_t pos,
                                         std::uint32_t size, std::int32_t left, std::int32_t top, std::int32_t right,
                                         std::int32_t bottom, std::int32_t* rowsAfter, std::int32_t* masked,
                                         std::int32_t* maskStart);
using SkipRowsFn = void(__fastcall*)(const SurfaceDesc* desc, std::int32_t rowsA, std::int32_t rowsB,
                                     std::uint32_t* rle, std::uint32_t* dst);

// The common start of every blitter: dst row, vertical clip, skipped rows.
Rows StartSprite(std::int32_t visible, std::int32_t hidden, const SurfaceDesc& desc, std::uint32_t pos,
                 std::uint32_t size, const ClipRect& clip, std::uint32_t* rle, std::uint32_t* dstRow) {
    *dstRow = SpriteRowStart(desc, pos);
    Rows rows{};
    reinterpret_cast<VerticalClipFn>(kRle16VerticalClip)(visible, &rows.skipTop, pos, size, clip.left, clip.top,
                                                          clip.right, clip.bottom, &rows.rowsAfter, &rows.masked,
                                                          &rows.maskStart);
    reinterpret_cast<SkipRowsFn>(kRle16SkipRows)(&desc, rows.skipTop, hidden, rle, dstRow);
    return rows;
}

// One run's header, read and stepped over.
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

// The masked rows: a run is clipped to [left, right) (0 = no edge) and its
// drawn part goes through the masked span; the phase counts every pixel.
// Which pixels are drawn follows the order of the original's branches.
enum class Edges { None, Left, Right, Both };

}  // namespace

void __fastcall BlitRle16_ClipLeft(std::int32_t visible, std::int32_t hidden, SurfaceDesc desc, std::uint32_t pos,
                                   std::uint32_t size, std::uint32_t rle, ClipRect clip);
void __fastcall BlitRle16_ClipBoth(std::int32_t visible, std::int32_t hidden, SurfaceDesc desc, std::uint32_t pos,
                                   std::uint32_t size, std::uint32_t rle, ClipRect clip);
void __fastcall BlitRle16_ClipRight(std::int32_t visible, std::int32_t hidden, SurfaceDesc desc, std::uint32_t pos,
                                    std::uint32_t size, std::uint32_t rle, ClipRect clip);
void __fastcall BlitRle16_Shaded(std::int32_t visible, std::int32_t hidden, SurfaceDesc desc, std::uint32_t pos,
                                 std::uint32_t size, std::uint32_t rle, ClipRect clip);
void __fastcall BlitRle16_FullBright(std::int32_t visible, std::int32_t hidden, SurfaceDesc desc, std::uint32_t pos,
                                     std::uint32_t size, std::uint32_t rle, ClipRect clip);

using BlitFn = void(__fastcall*)(std::int32_t visible, std::int32_t hidden, SurfaceDesc desc, std::uint32_t pos,
                                 std::uint32_t size, std::uint32_t rle, ClipRect clip);

S3RE_HOOK(0x005fc160, BlitRle16_Shaded, BlitFn, Original_BlitRle16_Shaded, BlitRle16_Shaded);

// Purpose: draws a 16-bit sprite that lies inside the clip rectangle's
// columns, at a brightness below full.
void __fastcall BlitRle16_Shaded(std::int32_t visible, std::int32_t hidden, SurfaceDesc desc, std::uint32_t pos,
                                 std::uint32_t size, std::uint32_t rle, ClipRect clip) {
    std::uint32_t dstRow = 0;
    const Rows rows = StartSprite(visible, hidden, desc, pos, size, clip, &rle, &dstRow);
    for (std::int32_t row = 0; row < rows.masked; ++row) {
        std::uint32_t dst = dstRow;
        std::uint32_t phase = 0;
        Run run;
        do {
            run = ReadRun(&rle);
            dst += run.skip * 2;
            phase = (phase + run.skip) & kMaskPhaseWrap;
            MaskedSpan(&dst, dst + run.count * 2, &rle, DitherMaskRow(rows.maskStart + row), &phase);
        } while (!run.last);
        dstRow += RowStep(desc);
    }
    for (std::int32_t row = 0; row < rows.rowsAfter; ++row) {
        std::uint32_t dst = dstRow;
        Run run;
        do {
            run = ReadRun(&rle);
            dst += run.skip * 2;
            ShadeSpan(&dst, run.count, &rle);
        } while (!run.last);
        dstRow += RowStep(desc);
    }
}

S3RE_HOOK(0x005fc350, BlitRle16_FullBright, BlitFn, Original_BlitRle16_FullBright, BlitRle16_FullBright);

// Purpose: draws a 16-bit sprite inside the clip's columns at full
// brightness (plain copies).
void __fastcall BlitRle16_FullBright(std::int32_t visible, std::int32_t hidden, SurfaceDesc desc, std::uint32_t pos,
                                     std::uint32_t size, std::uint32_t rle, ClipRect clip) {
    std::uint32_t dstRow = 0;
    const Rows rows = StartSprite(visible, hidden, desc, pos, size, clip, &rle, &dstRow);
    std::uint32_t src = rle;
    for (std::int32_t row = 0; row < rows.masked; ++row) {
        std::uint32_t dst = dstRow;
        std::uint32_t phase = 0;
        const std::uint32_t mask = DitherMaskRow(rows.maskStart + row);
        Run run;
        do {
            run = ReadRun(&src);
            phase = (phase + run.skip) & kMaskPhaseWrap;
            dst += run.skip * 2;
            // 0x5fc457: the inlined level-7 masked span.
            for (const std::uint32_t end = dst + run.count * 2; dst != end; dst += 2, src += 2) {
                if (ByteAt(mask + phase) != 0) {
                    *reinterpret_cast<std::uint16_t*>(dst) = WordAt(src);
                }
                phase = (phase + 1) & kMaskPhaseWrap;
            }
        } while (!run.last);
        dstRow += RowStep(desc);
    }
    for (std::int32_t row = 0; row < rows.rowsAfter; ++row) {
        std::uint32_t dst = dstRow;
        Run run;
        do {
            run = ReadRun(&src);
            dst += run.skip * 2;
            // 0x5fc4e9: the inlined copy span (pairs, then the odd pixel).
            for (const std::uint32_t end = dst + (run.count & 0xfe) * 2; dst != end; dst += 4, src += 4) {
                *reinterpret_cast<std::uint32_t*>(dst) = DwordAt(src);
            }
            if (run.count & 1) {
                *reinterpret_cast<std::uint16_t*>(dst) = WordAt(src);
                dst += 2;
                src += 2;
            }
        } while (!run.last);
        dstRow += RowStep(desc);
    }
}

namespace {

// Masked rows of the clipping blitters: per run, the part between the
// edges (left / right = 0 means that edge isn't clipped).
void MaskedClippedRun(const Run& run, std::uint32_t* dst, std::uint32_t* rle, std::uint32_t* phase,
                      std::uint32_t mask, bool clipLeft, std::uint32_t left, bool clipRight, std::uint32_t right) {
    const std::uint32_t end = *dst + run.count * 2;
    const bool beforeLeft = clipLeft && end <= left;
    const bool pastRight = clipRight && *dst >= right;
    if (clipLeft && clipRight) {
        // ClipBoth 0x5fb994..
        if (beforeLeft || pastRight) {
            *dst = end;
            *rle += run.count * 2;
            *phase = (*phase + run.count) & kMaskPhaseWrap;
        } else if (*dst >= left) {
            if (end <= right) {
                MaskedSpan(dst, end, rle, mask, phase);
            } else {
                const std::uint32_t cut = static_cast<std::uint32_t>(static_cast<std::int32_t>(right - *dst) >> 1);
                MaskedSpan(dst, *dst + cut * 2, rle, mask, phase);
                *rle += (run.count - cut) * 2;
                *phase = (*phase + run.count - cut) & kMaskPhaseWrap;
            }
        } else if (end <= right) {
            const std::uint32_t cut = static_cast<std::uint32_t>(static_cast<std::int32_t>(end - left) >> 1);
            const std::uint32_t hiddenPixels = run.count - cut;
            *phase = (hiddenPixels + *phase) & kMaskPhaseWrap;
            *dst += hiddenPixels * 2;
            *rle += hiddenPixels * 2;
            MaskedSpan(dst, *dst + cut * 2, rle, mask, phase);
        } else {
            const std::uint32_t leftCut = static_cast<std::uint32_t>(static_cast<std::int32_t>(left - *dst) >> 1);
            const std::uint32_t rightCut = static_cast<std::uint32_t>(static_cast<std::int32_t>(end - right) >> 1);
            *dst += leftCut * 2;
            *rle += leftCut * 2;
            *phase = (leftCut + *phase) & kMaskPhaseWrap;
            const std::uint32_t width = static_cast<std::uint32_t>(static_cast<std::int32_t>(right - left) >> 1);
            MaskedSpan(dst, *dst + width * 2, rle, mask, phase);
            *rle += rightCut * 2;
            *phase = (rightCut + *phase) & kMaskPhaseWrap;
        }
    } else if (clipLeft) {
        // ClipLeft 0x5fb623..
        if (end <= left) {
            *rle += run.count * 2;
            *dst = end;
            *phase = (*phase + run.count) & kMaskPhaseWrap;
        } else if (*dst >= left) {
            MaskedSpan(dst, end, rle, mask, phase);
        } else {
            const std::uint32_t cut = static_cast<std::uint32_t>(static_cast<std::int32_t>(end - left) >> 1);
            const std::uint32_t hiddenPixels = run.count - cut;
            *phase = (hiddenPixels + *phase) & kMaskPhaseWrap;
            *dst += hiddenPixels * 2;
            *rle += hiddenPixels * 2;
            MaskedSpan(dst, *dst + cut * 2, rle, mask, phase);
        }
    } else {
        // ClipRight 0x5fbf15..
        if (end < right) {
            MaskedSpan(dst, end, rle, mask, phase);
        } else if (*dst >= right) {
            *rle += run.count * 2;  // dst is not advanced (quirk)
            *phase = (*phase + run.count) & kMaskPhaseWrap;
        } else {
            const std::uint32_t cut = static_cast<std::uint32_t>(static_cast<std::int32_t>(right - *dst) >> 1);
            MaskedSpan(dst, *dst + cut * 2, rle, mask, phase);
            *phase = (run.count - cut + *phase) & kMaskPhaseWrap;
            *rle += (run.count - cut) * 2;
        }
    }
}

// Normal rows of the clipping blitters (shade spans, no phase).
void ShadeClippedRun(const Run& run, std::uint32_t* dst, std::uint32_t* rle, bool clipLeft, std::uint32_t left,
                     bool clipRight, std::uint32_t right) {
    const std::uint32_t end = *dst + run.count * 2;
    if (clipLeft && clipRight) {
        // ClipBoth 0x5fbbff..
        if (end <= left || *dst >= right) {
            *dst = end;
            *rle += run.count * 2;
        } else if (*dst >= left) {
            if (end <= right) {
                ShadeSpan(dst, run.count, rle);
            } else {
                const std::uint32_t cut = static_cast<std::uint32_t>(static_cast<std::int32_t>(right - *dst) >> 1);
                ShadeSpan(dst, cut, rle);
                *rle += (run.count - cut) * 2;
            }
        } else if (end <= right) {
            const std::uint32_t cut = static_cast<std::uint32_t>(static_cast<std::int32_t>(end - left) >> 1);
            const std::uint32_t hiddenPixels = run.count - cut;
            *dst += hiddenPixels * 2;
            *rle += hiddenPixels * 2;
            ShadeSpan(dst, cut, rle);
        } else {
            const std::uint32_t rightCut = static_cast<std::uint32_t>(static_cast<std::int32_t>(end - right) >> 1);
            const std::uint32_t leftCut = static_cast<std::uint32_t>(static_cast<std::int32_t>(left - *dst) >> 1);
            const std::uint32_t width = static_cast<std::uint32_t>(static_cast<std::int32_t>(right - left) >> 1);
            *dst += leftCut * 2;
            *rle += leftCut * 2;
            ShadeSpan(dst, width, rle);
            *rle += rightCut * 2;
        }
    } else if (clipLeft) {
        // ClipLeft 0x5fb793..
        if (end <= left) {
            *rle += run.count * 2;
            *dst = end;
        } else if (*dst >= left) {
            ShadeSpan(dst, run.count, rle);
        } else {
            const std::uint32_t cut = static_cast<std::uint32_t>(static_cast<std::int32_t>(end - left) >> 1);
            const std::uint32_t hiddenPixels = run.count - cut;
            *dst += hiddenPixels * 2;
            *rle += hiddenPixels * 2;
            ShadeSpan(dst, cut, rle);
        }
    } else {
        // ClipRight 0x5fc081..
        if (end < right) {
            ShadeSpan(dst, run.count, rle);
        } else if (*dst >= right) {
            *rle += run.count * 2;  // dst is not advanced (quirk)
        } else {
            const std::uint32_t cut = static_cast<std::uint32_t>(static_cast<std::int32_t>(right - *dst) >> 1);
            ShadeSpan(dst, cut, rle);
            *rle += (run.count - cut) * 2;
        }
    }
}

void BlitClipped(std::int32_t visible, std::int32_t hidden, const SurfaceDesc& desc, std::uint32_t pos,
                 std::uint32_t size, std::uint32_t rle, const ClipRect& clip, bool clipLeft, bool clipRight) {
    std::uint32_t dstRow = 0;
    const Rows rows = StartSprite(visible, hidden, desc, pos, size, clip, &rle, &dstRow);
    const std::int32_t x = PointX(pos);
    const std::uint32_t leftOffset = static_cast<std::uint32_t>(clip.left - x) * 2;
    const std::uint32_t rightOffset = static_cast<std::uint32_t>(clip.right - x) * 2;
    for (std::int32_t row = 0; row < rows.masked; ++row) {
        std::uint32_t dst = dstRow;
        std::uint32_t phase = 0;
        const std::uint32_t mask = DitherMaskRow(rows.maskStart + row);
        Run run;
        do {
            run = ReadRun(&rle);
            phase = (phase + run.skip) & kMaskPhaseWrap;
            dst += run.skip * 2;
            MaskedClippedRun(run, &dst, &rle, &phase, mask, clipLeft, dstRow + leftOffset, clipRight,
                             dstRow + rightOffset);
        } while (!run.last);
        dstRow += RowStep(desc);
    }
    for (std::int32_t row = 0; row < rows.rowsAfter; ++row) {
        std::uint32_t dst = dstRow;
        Run run;
        do {
            run = ReadRun(&rle);
            dst += run.skip * 2;
            ShadeClippedRun(run, &dst, &rle, clipLeft, dstRow + leftOffset, clipRight, dstRow + rightOffset);
        } while (!run.last);
        dstRow += RowStep(desc);
    }
}

}  // namespace

S3RE_HOOK(0x005fb4f0, BlitRle16_ClipLeft, BlitFn, Original_BlitRle16_ClipLeft, BlitRle16_ClipLeft);

// Purpose: draws a 16-bit sprite cut off by the clip rectangle's left edge.
void __fastcall BlitRle16_ClipLeft(std::int32_t visible, std::int32_t hidden, SurfaceDesc desc, std::uint32_t pos,
                                   std::uint32_t size, std::uint32_t rle, ClipRect clip) {
    BlitClipped(visible, hidden, desc, pos, size, rle, clip, true, false);
}

S3RE_HOOK(0x005fb850, BlitRle16_ClipBoth, BlitFn, Original_BlitRle16_ClipBoth, BlitRle16_ClipBoth);

// Purpose: draws a 16-bit sprite cut off at both sides of the clip rectangle.
void __fastcall BlitRle16_ClipBoth(std::int32_t visible, std::int32_t hidden, SurfaceDesc desc, std::uint32_t pos,
                                   std::uint32_t size, std::uint32_t rle, ClipRect clip) {
    BlitClipped(visible, hidden, desc, pos, size, rle, clip, true, true);
}

S3RE_HOOK(0x005fbdf0, BlitRle16_ClipRight, BlitFn, Original_BlitRle16_ClipRight, BlitRle16_ClipRight);

// Purpose: draws a 16-bit sprite cut off by the clip rectangle's right edge.
void __fastcall BlitRle16_ClipRight(std::int32_t visible, std::int32_t hidden, SurfaceDesc desc, std::uint32_t pos,
                                    std::uint32_t size, std::uint32_t rle, ClipRect clip) {
    BlitClipped(visible, hidden, desc, pos, size, rle, clip, false, true);
}

// ---------------------------------------------------------------------------
// Row helpers
// ---------------------------------------------------------------------------

void __fastcall Rle16VerticalClip(std::int32_t visible, std::int32_t* skipTop, std::uint32_t pos, std::uint32_t size,
                                  std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom,
                                  std::int32_t* rowsAfter, std::int32_t* masked, std::int32_t* maskStart);
void __fastcall Rle16SkipRows(const SurfaceDesc* desc, std::int32_t rowsA, std::int32_t rowsB, std::uint32_t* rle,
                              std::uint32_t* dst);

using ClipFn = void(__fastcall*)(std::int32_t* skipTop, std::int32_t* rows, std::uint32_t pos, std::uint32_t size,
                                 std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom);
using SkipRows8Fn = void(__fastcall*)(const SurfaceDesc* desc, std::int32_t rows, std::uint32_t* rle,
                                      std::uint32_t* dst);

void __fastcall ColorIndexedVerticalClip(std::int32_t* skipTop, std::int32_t* rows, std::uint32_t pos,
                                         std::uint32_t size, std::int32_t left, std::int32_t top, std::int32_t right,
                                         std::int32_t bottom);
void __fastcall ShadowVerticalClip(std::int32_t* skipTop, std::int32_t* rows, std::uint32_t pos, std::uint32_t size,
                                   std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom);
void __fastcall ColorIndexedSkipRows(const SurfaceDesc* desc, std::int32_t rows, std::uint32_t* rle,
                                     std::uint32_t* dst);
void __fastcall ShadowSkipRows(const SurfaceDesc* desc, std::int32_t rowsA, std::int32_t rowsB, std::uint32_t* rle,
                               std::uint32_t* dst);

S3RE_HOOK(0x005fb050, Rle16VerticalClip, VerticalClipFn, Original_Rle16VerticalClip, Rle16VerticalClip);

// Purpose: works out which rows of a sprite to draw: the rows cut by the
// clip rectangle's top and bottom, and the dissolving rows at the top of a
// building that is still being built.
void __fastcall Rle16VerticalClip(std::int32_t visible, std::int32_t* skipTop, std::uint32_t pos, std::uint32_t size,
                                  std::int32_t /*left*/, std::int32_t top, std::int32_t /*right*/,
                                  std::int32_t bottom, std::int32_t* rowsAfter, std::int32_t* masked,
                                  std::int32_t* maskStart) {
    const std::int32_t height = PointY(size);
    const std::int32_t y = PointY(pos);
    *skipTop = 0;
    if (visible == height) {
        *masked = static_cast<std::uint32_t>(visible) < static_cast<std::uint32_t>(kMaxMaskedRows) ? visible
                                                                                                    : kMaxMaskedRows;
        *maskStart = 0;
    } else {
        const std::int32_t dissolving = height - visible + kMaxMaskedRows;
        *masked = dissolving;
        *maskStart = kMaxMaskedRows - dissolving;
        const std::int32_t current = *masked;
        *masked = current < height ? current : height;
    }
    *rowsAfter = height - *masked;
    if (y < top) {
        const std::int32_t above = top - y;
        *skipTop = above;
        const std::int32_t maskedNow = *masked;
        if (above < maskedNow) {
            *masked = maskedNow - above;
            *maskStart = *maskStart + *skipTop;
        } else {
            *rowsAfter = *rowsAfter + (maskedNow - above);
            *masked = 0;
            *maskStart = 0;
        }
    }
    if (y + height >= bottom) {
        const std::int32_t below = y - bottom + height;
        const std::int32_t after = *rowsAfter;
        if (below >= after) {
            *masked = *masked + (after - below);
            *rowsAfter = 0;
        } else {
            *rowsAfter = after - below;
        }
    }
}

S3RE_HOOK(0x005fb130, Rle16SkipRows, SkipRowsFn, Original_Rle16SkipRows, Rle16SkipRows);

// Purpose: steps over the RLE rows of a 16-bit sprite that aren't drawn.
void __fastcall Rle16SkipRows(const SurfaceDesc* desc, std::int32_t rowsA, std::int32_t rowsB, std::uint32_t* rle,
                              std::uint32_t* dst) {
    std::uint32_t p = *rle;
    for (std::uint32_t rows = static_cast<std::uint32_t>(rowsA + rowsB); rows != 0; --rows) {
        std::uint8_t flags;
        do {
            flags = ByteAt(p + 1);
            p += ByteAt(p) * 2 + 2;
        } while (!(flags & kLastRun));
    }
    *dst += (static_cast<std::uint32_t>(desc->pitch * rowsA) >> 1) * 2;
    *rle = p;
}

S3RE_HOOK(0x005fc7d0, ColorIndexedVerticalClip, ClipFn, Original_ColorIndexedVerticalClip, ColorIndexedVerticalClip);

// Purpose: the rows of a player-colour sprite inside the clip rectangle.
void __fastcall ColorIndexedVerticalClip(std::int32_t* skipTop, std::int32_t* rows, std::uint32_t pos,
                                         std::uint32_t size, std::int32_t /*left*/, std::int32_t top,
                                         std::int32_t /*right*/, std::int32_t bottom) {
    const std::int32_t y = PointY(pos);
    const std::int32_t height = PointY(size);
    *skipTop = 0;
    *rows = height;
    if (y < top) {
        const std::int32_t above = top - y;
        *skipTop = above;
        *rows = *rows - above;
    }
    if (y + height >= bottom) {
        *rows = *rows + (bottom - y - height);
    }
}

S3RE_HOOK(0x005fcff0, ShadowVerticalClip, ClipFn, Original_ShadowVerticalClip, ShadowVerticalClip);

// Purpose: the rows of a shadow inside the clip rectangle.
void __fastcall ShadowVerticalClip(std::int32_t* skipTop, std::int32_t* rows, std::uint32_t pos, std::uint32_t size,
                                   std::int32_t /*left*/, std::int32_t top, std::int32_t /*right*/,
                                   std::int32_t bottom) {
    const std::int32_t y = PointY(pos);
    const std::int32_t height = PointY(size);
    *skipTop = 0;
    *rows = height;
    if (y < top) {
        const std::int32_t skip = *skipTop + (top - y);
        *skipTop = skip;
        *rows = *rows - skip;
    }
    if (y + height >= bottom) {
        *rows = *rows + (bottom - y - height);
    }
}

S3RE_HOOK(0x005fc810, ColorIndexedSkipRows, SkipRows8Fn, Original_ColorIndexedSkipRows, ColorIndexedSkipRows);

// Purpose: steps over the RLE rows of a player-colour sprite that aren't drawn.
void __fastcall ColorIndexedSkipRows(const SurfaceDesc* desc, std::int32_t rows, std::uint32_t* rle,
                                     std::uint32_t* dst) {
    std::uint32_t p = *rle;
    for (std::int32_t left = rows; left > 0; --left) {
        std::uint8_t flags;
        do {
            flags = ByteAt(p + 1);
            p += ByteAt(p) + 2;
        } while (!(flags & kLastRun));
    }
    *dst += (static_cast<std::uint32_t>(desc->pitch * rows) >> 1) * 2;
    *rle = p;
}

S3RE_HOOK(0x005fd040, ShadowSkipRows, SkipRowsFn, Original_ShadowSkipRows, ShadowSkipRows);

// Purpose: steps over the rows of a shadow that aren't drawn.
void __fastcall ShadowSkipRows(const SurfaceDesc* desc, std::int32_t rowsA, std::int32_t rowsB, std::uint32_t* rle,
                               std::uint32_t* dst) {
    std::uint32_t p = *rle;
    for (std::uint32_t rows = static_cast<std::uint32_t>(rowsB + rowsA); rows != 0; --rows) {
        std::uint8_t flags;
        do {
            flags = ByteAt(p + 1);
            p += 2;
        } while (!(flags & kLastRun));
    }
    *dst += (static_cast<std::uint32_t>(desc->pitch * rowsA) >> 1) * 2;
    *rle = p;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Row helpers: random arguments, out cells (sometimes aliasing) and RLE
// rows. Blitters: every 16-bit sprite of the real GFX files (siedler3_*,
// RGB565 set, mapped read-only) and random synthetic sprites, drawn by the
// original and by ours into two copies of a random 16-bit surface with
// guard bytes, with random clip rectangles, positions classified the way
// GfxDword::DrawSprite (0x5fadb0) picks the blitter, brightness and
// construction progress; the whole surfaces are compared.

namespace {

using gfx_test::GfxSprites;
using gfx_test::LoadGfxSprites;
using gfx_test::MakeRle;

std::uint32_t Address(const void* p) {
    return reinterpret_cast<std::uint32_t>(p);
}

// --- surfaces ------------------------------------------------------------------

constexpr std::int32_t kSurfaceWidth = 800;
constexpr std::int32_t kSurfaceHeight = 600;
constexpr std::uint32_t kGuardBytes = 0x4000;
constexpr std::uint32_t kSurfaceBytes = kGuardBytes * 2 + (kSurfaceWidth + 8) * 2 * kSurfaceHeight;

std::vector<std::uint8_t>& SurfaceA() {
    static std::vector<std::uint8_t> bytes(kSurfaceBytes);
    return bytes;
}
std::vector<std::uint8_t>& SurfaceB() {
    static std::vector<std::uint8_t> bytes(kSurfaceBytes);
    return bytes;
}

// The parameters DrawSprite derives for a sprite (0x5fadc2..0x5fae3b).
struct SpriteCall {
    std::int32_t visible;
    std::int32_t hidden;
    std::uint32_t pos;
    std::uint32_t size;
};

SpriteCall DeriveCall(test::Rng& rng, std::int16_t x, std::int16_t y, std::uint16_t width, std::uint16_t height) {
    SpriteCall call{};
    std::uint16_t h = height;
    std::int16_t yy = y;
    if (rng.Chance(1, 2)) {
        call.visible = height + 8;
        call.hidden = 0;
    } else {
        const std::uint32_t fraction = 1 + rng.Below(0xffff);
        const std::uint32_t visible = ((static_cast<std::int32_t>(height) + 8) * fraction) >> 16;
        const std::uint32_t kept = height < visible ? height : visible;
        call.visible = static_cast<std::int32_t>(visible);
        call.hidden = static_cast<std::int16_t>(height - kept);
        yy = static_cast<std::int16_t>(y + (height - kept));
        h = static_cast<std::uint16_t>(kept);
    }
    call.pos = static_cast<std::uint16_t>(x) | static_cast<std::uint32_t>(static_cast<std::uint16_t>(yy)) << 16;
    call.size = width | static_cast<std::uint32_t>(h) << 16;
    return call;
}

enum class Blitter { ClipLeft, ClipBoth, ClipRight, Shaded, FullBright };

// Draws one sprite with both versions of the blitter DrawSprite would pick
// for a random position; returns false on a difference.
bool CompareOneSprite(test::Context& ctx, std::uint32_t rle, std::uint16_t width, std::uint16_t height,
                      const BlitFn (&ours)[5]) {
    static const BlitFn kOriginals[5] = {Original_BlitRle16_ClipLeft, Original_BlitRle16_ClipBoth,
                                         Original_BlitRle16_ClipRight, Original_BlitRle16_Shaded,
                                         Original_BlitRle16_FullBright};
    test::Rng& rng = ctx.rng();
    ClipRect clip;
    clip.left = static_cast<std::int32_t>(rng.Below(200));
    clip.top = static_cast<std::int32_t>(rng.Below(150));
    clip.right = clip.left + 1 + static_cast<std::int32_t>(rng.Below(kSurfaceWidth - clip.left));
    clip.bottom = clip.top + 1 + static_cast<std::int32_t>(rng.Below(kSurfaceHeight - clip.top));
    // A position overlapping the clip, the way DrawSprite filters (x < right,
    // x + w >= left, y < bottom, y + h >= top), kept inside the surface.
    const std::int32_t x = clip.left - static_cast<std::int32_t>(width) +
                           static_cast<std::int32_t>(rng.Below(clip.right - clip.left + width));
    const std::int32_t y = clip.top - static_cast<std::int32_t>(height) +
                           static_cast<std::int32_t>(rng.Below(clip.bottom - clip.top + height));
    if (x < 0 || x + width > kSurfaceWidth || y < 0 || y + height > kSurfaceHeight) {
        return true;
    }
    const SpriteCall call = DeriveCall(rng, static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), width, height);
    const std::int32_t px = PointX(call.pos);
    const std::uint32_t brightness = rng.Chance(1, 4) ? 248 + rng.Below(8) : rng.Below(256);
    Blitter blitter;
    if (px < clip.left) {
        blitter = px + width < clip.right ? Blitter::ClipLeft : Blitter::ClipBoth;
    } else if (px + width >= clip.right) {
        blitter = Blitter::ClipRight;
    } else {
        blitter = (brightness & ~7u) == 0xf8 ? Blitter::FullBright : Blitter::Shaded;
    }
    const std::int32_t pitch = (kSurfaceWidth + 2 * static_cast<std::int32_t>(rng.Below(4))) * 2;
    SurfaceDesc desc{};
    rng.Fill(&desc, sizeof(desc));
    desc.pitch = pitch;
    const std::uint32_t fill = rng.Next32();
    std::memset(SurfaceA().data(), static_cast<int>(fill & 0xff), kSurfaceBytes);
    std::memset(SurfaceB().data(), static_cast<int>(fill & 0xff), kSurfaceBytes);
    using SelectFn = void(__fastcall*)(std::uint32_t);
    reinterpret_cast<SelectFn>(0x005fb4c0)(brightness);
    desc.surface = Address(SurfaceA().data()) + kGuardBytes;
    kOriginals[static_cast<int>(blitter)](call.visible, call.hidden, desc, call.pos, call.size, rle, clip);
    desc.surface = Address(SurfaceB().data()) + kGuardBytes;
    ours[static_cast<int>(blitter)](call.visible, call.hidden, desc, call.pos, call.size, rle, clip);
    if (!S3RE_EXPECT(std::memcmp(SurfaceA().data(), SurfaceB().data(), kSurfaceBytes) == 0)) {
        ctx.Fail(__FILE__, __LINE__, "  blitter %d, sprite %ux%u at (%d, %d), clip (%d %d %d %d), visible %d hidden %d",
                 static_cast<int>(blitter), width, height, px, PointY(call.pos), clip.left, clip.top, clip.right,
                 clip.bottom, call.visible, call.hidden);
        return false;
    }
    return true;
}

constexpr BlitFn kOurBlitters[5] = {BlitRle16_ClipLeft, BlitRle16_ClipBoth, BlitRle16_ClipRight, BlitRle16_Shaded,
                                    BlitRle16_FullBright};

void CompareBlitters(test::Context& ctx, const BlitFn (&ours)[5], std::uint32_t syntheticCases, bool realSprites) {
    const std::uint32_t savedShade = g_shadeSpan;
    const std::uint32_t savedMasked = g_maskedSpan;
    const ShadeMasks savedMasks = g_shadeMasks;
    static constexpr std::uint32_t k565[5] = {0x7bef, 0x39e7, 0x18e3, 0x861, 0x20};
    for (int k = 0; k < 5; ++k) {
        g_shadeMasks->pixel[k] = k565[k];
        g_shadeMasks->pair[k] = k565[k] << 16 | k565[k];
    }
    bool ok = true;
    if (realSprites) {
        const GfxSprites& sprites = LoadGfxSprites();
        S3RE_EXPECT(sprites.images[0].size() > 1000);
        for (std::size_t i = 0; ok && i < sprites.images[0].size(); ++i) {
            const std::uint32_t image = sprites.images[0][i];
            const std::uint16_t width = *reinterpret_cast<const std::uint16_t*>(image + 4);
            const std::uint16_t height = *reinterpret_cast<const std::uint16_t*>(image + 6);
            const std::uint32_t rle = (image + 0xd + ByteAt(image + 0xc) + 1) & ~1u;
            if (width == 0 || height == 0 || width > 600 || height > 500) {
                continue;
            }
            for (int repeat = 0; ok && repeat < 2; ++repeat) {
                ok = CompareOneSprite(ctx, rle, width, height, ours);
            }
        }
    }
    for (std::uint32_t i = 0; ok && i < syntheticCases; ++i) {
        const std::uint16_t width = static_cast<std::uint16_t>(1 + ctx.rng().Below(120));
        const std::uint16_t height = static_cast<std::uint16_t>(1 + ctx.rng().Below(60));
        const std::vector<std::uint8_t> data = MakeRle(ctx.rng(), width, height, 2);
        ok = CompareOneSprite(ctx, Address(data.data()), width, height, ours);
    }
    g_shadeSpan = savedShade;
    g_maskedSpan = savedMasked;
    g_shadeMasks = savedMasks;
}

// Wrong: ClipRight advances dst past a skipped run (the original doesn't).
void __fastcall WrongClipBoth(std::int32_t visible, std::int32_t hidden, SurfaceDesc desc, std::uint32_t pos,
                              std::uint32_t size, std::uint32_t rle, ClipRect clip) {
    clip.right -= 1;
    BlitClipped(visible, hidden, desc, pos, size, rle, clip, true, true);
}

// --- row helpers ---------------------------------------------------------------

struct HelperFunctions {
    VerticalClipFn rle16Clip;
    ClipFn colorClip;
    ClipFn shadowClip;
    SkipRowsFn rle16Skip;
    SkipRows8Fn colorSkip;
    SkipRowsFn shadowSkip;
};

void CompareHelpers(test::Context& ctx, const HelperFunctions& candidate, int cases) {
    const HelperFunctions original{Original_Rle16VerticalClip, Original_ColorIndexedVerticalClip,
                                   Original_ShadowVerticalClip,  Original_Rle16SkipRows,
                                   Original_ColorIndexedSkipRows, Original_ShadowSkipRows};
    test::Rng& rng = ctx.rng();
    for (int i = 0; i < cases; ++i) {
        const int op = i % 6;
        std::int32_t cellsA[8];
        std::int32_t cellsB[8];
        rng.Fill(cellsA, sizeof(cellsA));
        std::memcpy(cellsB, cellsA, sizeof(cellsA));
        // Out pointers: distinct cells, sometimes two the same.
        std::uint32_t slots[4] = {0, 1, 2, 3};
        if (rng.Chance(1, 10)) {
            slots[rng.Below(4)] = slots[rng.Below(4)];
        }
        const auto smallSigned = [&rng](std::int32_t range) {
            return static_cast<std::int32_t>(rng.Below(2 * range)) - range;
        };
        bool same = true;
        if (op <= 2) {
            const std::uint32_t pos = static_cast<std::uint16_t>(smallSigned(300)) |
                                      static_cast<std::uint32_t>(static_cast<std::uint16_t>(smallSigned(300))) << 16;
            const std::uint32_t size = rng.Next32() & 0xffff | (rng.Below(120) << 16);
            const std::int32_t top = smallSigned(200);
            const std::int32_t bottom = top + smallSigned(300);
            const std::int32_t visible = rng.Chance(1, 3) ? static_cast<std::int32_t>(size >> 16)
                                                          : static_cast<std::int32_t>(rng.Below(140)) - 4;
            if (op == 0) {
                original.rle16Clip(visible, &cellsA[slots[0]], pos, size, 1, top, 2, bottom, &cellsA[slots[1]],
                                   &cellsA[slots[2]], &cellsA[slots[3]]);
                candidate.rle16Clip(visible, &cellsB[slots[0]], pos, size, 1, top, 2, bottom, &cellsB[slots[1]],
                                    &cellsB[slots[2]], &cellsB[slots[3]]);
            } else {
                const ClipFn a = op == 1 ? original.colorClip : original.shadowClip;
                const ClipFn b = op == 1 ? candidate.colorClip : candidate.shadowClip;
                a(&cellsA[slots[0]], &cellsA[slots[1]], pos, size, 1, top, 2, bottom);
                b(&cellsB[slots[0]], &cellsB[slots[1]], pos, size, 1, top, 2, bottom);
            }
        } else {
            const std::uint32_t pixelBytes = op == 3 ? 2 : op == 4 ? 1 : 0;
            const std::vector<std::uint8_t> data = MakeRle(rng, 1 + rng.Below(60), 40, pixelBytes);
            SurfaceDesc desc{};
            desc.pitch = static_cast<std::int32_t>(rng.Chance(1, 4) ? rng.Next32() : rng.Below(4000));
            const std::int32_t rowsA = static_cast<std::int32_t>(rng.Below(20)) - (op == 4 ? 3 : 0);
            const std::int32_t rowsB = static_cast<std::int32_t>(rng.Below(20));
            std::uint32_t rleA = Address(data.data());
            std::uint32_t rleB = rleA;
            std::uint32_t dstA = rng.Next32();
            std::uint32_t dstB = dstA;
            if (op == 3) {
                original.rle16Skip(&desc, rowsA, rowsB, &rleA, &dstA);
                candidate.rle16Skip(&desc, rowsA, rowsB, &rleB, &dstB);
            } else if (op == 4) {
                original.colorSkip(&desc, rowsA, &rleA, &dstA);
                candidate.colorSkip(&desc, rowsA, &rleB, &dstB);
            } else {
                original.shadowSkip(&desc, rowsA, rowsB, &rleA, &dstA);
                candidate.shadowSkip(&desc, rowsA, rowsB, &rleB, &dstB);
            }
            same &= S3RE_EXPECT_EQ(rleA, rleB);
            same &= S3RE_EXPECT_EQ(dstA, dstB);
        }
        same &= S3RE_EXPECT_MEM_EQ(cellsA, cellsB, sizeof(cellsA));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
            break;
        }
    }
}

// Wrong: the shadow clip re-reads nothing (uses the plain difference).
void __fastcall WrongRle16Clip(std::int32_t visible, std::int32_t* skipTop, std::uint32_t pos, std::uint32_t size,
                               std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom,
                               std::int32_t* rowsAfter, std::int32_t* masked, std::int32_t* maskStart) {
    Rle16VerticalClip(visible, skipTop, pos, size, left, top, right, bottom, rowsAfter, masked, maskStart);
    if (*masked > 7) {
        *masked = 7;
    }
}

}  // namespace

S3RE_TEST(GfxRleRows_MatchOriginal) {
    CompareHelpers(ctx, {Rle16VerticalClip, ColorIndexedVerticalClip, ShadowVerticalClip, Rle16SkipRows,
                         ColorIndexedSkipRows, ShadowSkipRows},
                   60000);
}

S3RE_TEST(GfxRleRows_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareHelpers(probe, {WrongRle16Clip, ColorIndexedVerticalClip, ShadowVerticalClip, Rle16SkipRows,
                           ColorIndexedSkipRows, ShadowSkipRows},
                   6000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GfxBlitRle16_MatchOriginal) {
    CompareBlitters(ctx, kOurBlitters, 20000, true);
}

S3RE_TEST(GfxBlitRle16_ComparisonDetectsWrongImplementation) {
    BlitFn wrong[5];
    std::memcpy(wrong, kOurBlitters, sizeof(wrong));
    wrong[1] = WrongClipBoth;
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareBlitters(probe, wrong, 3000, false);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
