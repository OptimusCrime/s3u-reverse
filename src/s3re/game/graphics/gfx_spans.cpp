// The span functions of the 16-bit sprite and shadow drawing
// (Dirk\graphicsdata.cpp): each draws one horizontal run of pixels from an
// RLE sprite row into the surface, scaled for the brightness the sprite is
// drawn at, and the small functions that pick them.
//
// Shade spans (32, table g_shadeSpans 0x7a2bd8, 0x5f48e0..0x5f59e0):
//   fastcall (ECX = &dst, EDX = dstEnd, stack: &src, odd), ret 8. Copies
//   dwords (two pixels) from src to dst until dst == dstEnd (checked first;
//   steps of 4), then, if the low byte of `odd` is set, one more 16-bit
//   pixel; stores the advanced dst and src back (dst first). Level i (the
//   table index, brightness >> 3) scales each channel by (i + 1) / 32 as a
//   sum of shifted, masked copies: bit 4 of i + 1 adds x >> 1, bit 3 x >> 2,
//   ..., bit 0 x >> 5, each masked with g_shadeMasks (pair[k - 1] for two
//   pixels, pixel[k - 1] for the odd one). Exceptions: level 31 copies,
//   level 0 writes black (0) instead of 1/32 and doesn't read src.
//   The original has one unrolled function per level; the sum is the same
//   in any order (wrapping 32-bit adds), so ours is one template.
// Masked spans (8, table g_maskedSpans 0x7a2c58, 0x5fb180..0x5fb470):
//   fastcall (ECX = &dst, EDX = dstEnd, stack: &src, mask, &maskIndex),
//   ret 0xc. Pixel by pixel until dst == dstEnd (checked first): if
//   mask[*maskIndex] != 0 the pixel is written; *maskIndex = (*maskIndex +
//   1) & 15 after every pixel (read again from memory each time). Level i
//   (brightness >> 5) scales by (i + 1) / 8 with pixel[0..2] (x >> 1, >> 2,
//   >> 3), level 7 copies, level 0 writes black. Then dst, src stored back.
//   Used for the dissolving top rows of a building under construction
//   (Rle16VerticalClip 0x5fb050).
// Shadow spans (g_shadowSpan.span): fastcall (ECX = &dst, EDX = dstEnd,
//   stack: &phase, parity), ret 8.
//   ShadowSpan_Dither (0x5fd080, method 1): if dst < dstEnd (unsigned), the
//     pixels whose (phase + parity) is odd become 0 (black), phase = (phase
//     + 1) & 31 per pixel (phase read once); then dst and phase stored back.
//     Nothing is stored when dst >= dstEnd.
//   ShadowSpan_Halve (0x5fd0c0, method 0): if dst < dstEnd, halves every
//     pixel of the destination (x >> 1 & mask): one pixel to reach a
//     4-byte boundary, dwords up to dstEnd & ~3, one pixel more if dstEnd
//     isn't aligned; dst stored back. The stack arguments are unused.
// SelectBrightnessSpans (0x5fb4c0): fastcall (ECX = brightness):
//   g_shadeSpan = g_shadeSpans[b >> 3], g_maskedSpan = g_maskedSpans[b >> 5]
//   (no range check).
// SetShadowSpan (0x5fcd30): fastcall (ECX, EDX, stack: a, b, method), ret
//   0xc: stores the four values and the method into g_shadowSpan, then the
//   span for method 1 (dither) or 0 (halve); any other method is an
//   assertion (Win32::ThrowAssertmentFailed "GFX: Invalid shadow method",
//   graphicsdata.cpp line 2847), after the stores.
// CGraphicsData::SetShadowMethod (0x5faa70): thiscall (method), ret 4:
//   SetShadowSpan(pixel[0], pair[0], shadow[0], shadow[1], method).
//
// Return values: the spans are called only through the function pointers
// above by the blitters (0x5fb4f0..0x5fc540, 0x5fd120..0x5fd7d0), which
// don't read EAX afterwards; ours return nothing (the emulator specs don't
// compare EAX).

#include "game/globals.h"
#include "game/raw.h"
#include "game/tables.h"
#include "game/texts.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

// Addresses of the shadow spans (stored into g_shadowSpan.span).
constexpr std::uint32_t kShadowSpanDither = 0x005fd080;
constexpr std::uint32_t kShadowSpanHalve = 0x005fd0c0;
constexpr std::uintptr_t kSetShadowSpan = 0x005fcd30;
constexpr std::uintptr_t kThrowAssertmentFailed = 0x006119b0;
constexpr std::int32_t kInvalidShadowMethodLine = 2847;

constexpr std::uint32_t kShadowMethodHalve = 0;
constexpr std::uint32_t kShadowMethodDither = 1;

// The dither mask index runs over 16 entries, the shadow phase over 32.
constexpr std::uint32_t kMaskIndexWrap = 15;
constexpr std::uint32_t kShadowPhaseWrap = 31;

using AssertFn = void(__fastcall*)(const char* expression, const char* file, std::int32_t line);

std::uint32_t Load32(std::uint32_t address) {
    return *reinterpret_cast<const std::uint32_t*>(address);
}
std::uint16_t Load16(std::uint32_t address) {
    return *reinterpret_cast<const std::uint16_t*>(address);
}
void Store32(std::uint32_t address, std::uint32_t value) {
    *reinterpret_cast<std::uint32_t*>(address) = value;
}
void Store16(std::uint32_t address, std::uint32_t value) {
    *reinterpret_cast<std::uint16_t*>(address) = static_cast<std::uint16_t>(value);
}

// x scaled by fraction/32 (fraction 2..31) as the original's sum of
// shifted, masked copies; masks = g_shadeMasks pair (dwords) or pixel.
std::uint32_t ScaleBy32nds(std::uint32_t x, std::uint32_t fraction, const std::uint32_t (&masks)[5]) {
    std::uint32_t sum = 0;
    for (std::uint32_t shift = 1; shift <= 5; ++shift) {
        if (fraction & (16u >> (shift - 1))) {
            sum += (x >> shift) & masks[shift - 1];
        }
    }
    return sum;
}

// x scaled by fraction/8 (fraction 2..7) with the single-pixel masks.
std::uint32_t ScaleBy8ths(std::uint32_t x, std::uint32_t fraction) {
    std::uint32_t sum = 0;
    for (std::uint32_t shift = 1; shift <= 3; ++shift) {
        if (fraction & (4u >> (shift - 1))) {
            sum += (x >> shift) & g_shadeMasks->pixel[shift - 1];
        }
    }
    return sum;
}

constexpr int kShadeBlack = 0;
constexpr int kShadeCopy = 31;

template <int kLevel>
void ShadeSpan(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell, std::uint32_t odd) {
    static_assert(kLevel >= 0 && kLevel < 32);
    constexpr std::uint32_t kFraction = kLevel + 1;
    std::uint32_t dst = *dstCell;
    std::uint32_t src = *srcCell;
    for (; dst != dstEnd; dst += 4, src += 4) {
        if constexpr (kLevel == kShadeBlack) {
            Store32(dst, 0);
        } else if constexpr (kLevel == kShadeCopy) {
            Store32(dst, Load32(src));
        } else {
            Store32(dst, ScaleBy32nds(Load32(src), kFraction, g_shadeMasks->pair));
        }
    }
    if (static_cast<std::uint8_t>(odd) != 0) {
        if constexpr (kLevel == kShadeBlack) {
            Store16(dst, 0);
        } else if constexpr (kLevel == kShadeCopy) {
            Store16(dst, Load16(src));
        } else {
            Store16(dst, ScaleBy32nds(Load16(src), kFraction, g_shadeMasks->pixel));
        }
        dst += 2;
        src += 2;
    }
    *dstCell = dst;
    *srcCell = src;
}

constexpr int kMaskedBlack = 0;
constexpr int kMaskedCopy = 7;

template <int kLevel>
void MaskedSpan(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell, const std::uint8_t* mask,
                std::uint32_t* maskIndex) {
    static_assert(kLevel >= 0 && kLevel < 8);
    std::uint32_t dst = *dstCell;
    std::uint32_t src = *srcCell;
    for (; dst != dstEnd; dst += 2, src += 2) {
        if (mask[*maskIndex] != 0) {
            if constexpr (kLevel == kMaskedBlack) {
                Store16(dst, 0);
            } else if constexpr (kLevel == kMaskedCopy) {
                Store16(dst, Load16(src));
            } else {
                Store16(dst, ScaleBy8ths(Load16(src), kLevel + 1));
            }
        }
        // Read again: the pixel just written may be the index itself.
        *maskIndex = (*maskIndex + 1) & kMaskIndexWrap;
    }
    *dstCell = dst;
    *srcCell = src;
}

}  // namespace

using ShadeSpanFn = void(__fastcall*)(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                      std::uint32_t odd);
using MaskedSpanFn = void(__fastcall*)(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                       const std::uint8_t* mask, std::uint32_t* maskIndex);
using ShadowSpanFn = void(__fastcall*)(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* phaseCell,
                                       std::uint32_t parity);
using SelectSpansFn = void(__fastcall*)(std::uint32_t brightness);
using SetShadowSpanFn = void(__fastcall*)(std::uint32_t halfMask16, std::uint32_t halfMask32, std::uint32_t mask18,
                                          std::uint32_t mask1c, std::uint32_t method);
using SetShadowMethodFn = void(__fastcall*)(void* self, void* edx, std::uint32_t method);

// One named function per level (the emulator specs find them by name).
// Purpose (all levels): draw one run of a sprite's pixels at one of 32
// brightness levels (probably the darkening of sprites with the landscape's
// light: night, fog of war, shaded slopes).
void __fastcall ShadeSpan16_Level00(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<0>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f48e0, ShadeSpan16_Level00, ShadeSpanFn, Original_ShadeSpan16_Level00, ShadeSpan16_Level00);
void __fastcall ShadeSpan16_Level01(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<1>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f4920, ShadeSpan16_Level01, ShadeSpanFn, Original_ShadeSpan16_Level01, ShadeSpan16_Level01);
void __fastcall ShadeSpan16_Level02(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<2>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f4980, ShadeSpan16_Level02, ShadeSpanFn, Original_ShadeSpan16_Level02, ShadeSpan16_Level02);
void __fastcall ShadeSpan16_Level03(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<3>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f4a00, ShadeSpan16_Level03, ShadeSpanFn, Original_ShadeSpan16_Level03, ShadeSpan16_Level03);
void __fastcall ShadeSpan16_Level04(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<4>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f4a60, ShadeSpan16_Level04, ShadeSpanFn, Original_ShadeSpan16_Level04, ShadeSpan16_Level04);
void __fastcall ShadeSpan16_Level05(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<5>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f4ae0, ShadeSpan16_Level05, ShadeSpanFn, Original_ShadeSpan16_Level05, ShadeSpan16_Level05);
void __fastcall ShadeSpan16_Level06(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<6>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f4b60, ShadeSpan16_Level06, ShadeSpanFn, Original_ShadeSpan16_Level06, ShadeSpan16_Level06);
void __fastcall ShadeSpan16_Level07(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<7>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f4c00, ShadeSpan16_Level07, ShadeSpanFn, Original_ShadeSpan16_Level07, ShadeSpan16_Level07);
void __fastcall ShadeSpan16_Level08(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<8>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f4c60, ShadeSpan16_Level08, ShadeSpanFn, Original_ShadeSpan16_Level08, ShadeSpan16_Level08);
void __fastcall ShadeSpan16_Level09(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<9>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f4ce0, ShadeSpan16_Level09, ShadeSpanFn, Original_ShadeSpan16_Level09, ShadeSpan16_Level09);
void __fastcall ShadeSpan16_Level10(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<10>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f4d60, ShadeSpan16_Level10, ShadeSpanFn, Original_ShadeSpan16_Level10, ShadeSpan16_Level10);
void __fastcall ShadeSpan16_Level11(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<11>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f4e00, ShadeSpan16_Level11, ShadeSpanFn, Original_ShadeSpan16_Level11, ShadeSpan16_Level11);
void __fastcall ShadeSpan16_Level12(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<12>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f4e80, ShadeSpan16_Level12, ShadeSpanFn, Original_ShadeSpan16_Level12, ShadeSpan16_Level12);
void __fastcall ShadeSpan16_Level13(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<13>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f4f20, ShadeSpan16_Level13, ShadeSpanFn, Original_ShadeSpan16_Level13, ShadeSpan16_Level13);
void __fastcall ShadeSpan16_Level14(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<14>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f4fc0, ShadeSpan16_Level14, ShadeSpanFn, Original_ShadeSpan16_Level14, ShadeSpan16_Level14);
void __fastcall ShadeSpan16_Level15(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<15>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f5070, ShadeSpan16_Level15, ShadeSpanFn, Original_ShadeSpan16_Level15, ShadeSpan16_Level15);
void __fastcall ShadeSpan16_Level16(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<16>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f50d0, ShadeSpan16_Level16, ShadeSpanFn, Original_ShadeSpan16_Level16, ShadeSpan16_Level16);
void __fastcall ShadeSpan16_Level17(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<17>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f5140, ShadeSpan16_Level17, ShadeSpanFn, Original_ShadeSpan16_Level17, ShadeSpan16_Level17);
void __fastcall ShadeSpan16_Level18(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<18>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f51b0, ShadeSpan16_Level18, ShadeSpanFn, Original_ShadeSpan16_Level18, ShadeSpan16_Level18);
void __fastcall ShadeSpan16_Level19(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<19>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f5250, ShadeSpan16_Level19, ShadeSpanFn, Original_ShadeSpan16_Level19, ShadeSpan16_Level19);
void __fastcall ShadeSpan16_Level20(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<20>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f52c0, ShadeSpan16_Level20, ShadeSpanFn, Original_ShadeSpan16_Level20, ShadeSpan16_Level20);
void __fastcall ShadeSpan16_Level21(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<21>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f5360, ShadeSpan16_Level21, ShadeSpanFn, Original_ShadeSpan16_Level21, ShadeSpan16_Level21);
void __fastcall ShadeSpan16_Level22(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<22>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f5400, ShadeSpan16_Level22, ShadeSpanFn, Original_ShadeSpan16_Level22, ShadeSpan16_Level22);
void __fastcall ShadeSpan16_Level23(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<23>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f54b0, ShadeSpan16_Level23, ShadeSpanFn, Original_ShadeSpan16_Level23, ShadeSpan16_Level23);
void __fastcall ShadeSpan16_Level24(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<24>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f5520, ShadeSpan16_Level24, ShadeSpanFn, Original_ShadeSpan16_Level24, ShadeSpan16_Level24);
void __fastcall ShadeSpan16_Level25(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<25>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f55c0, ShadeSpan16_Level25, ShadeSpanFn, Original_ShadeSpan16_Level25, ShadeSpan16_Level25);
void __fastcall ShadeSpan16_Level26(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<26>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f5660, ShadeSpan16_Level26, ShadeSpanFn, Original_ShadeSpan16_Level26, ShadeSpan16_Level26);
void __fastcall ShadeSpan16_Level27(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<27>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f5710, ShadeSpan16_Level27, ShadeSpanFn, Original_ShadeSpan16_Level27, ShadeSpan16_Level27);
void __fastcall ShadeSpan16_Level28(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<28>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f57b0, ShadeSpan16_Level28, ShadeSpanFn, Original_ShadeSpan16_Level28, ShadeSpan16_Level28);
void __fastcall ShadeSpan16_Level29(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<29>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f5860, ShadeSpan16_Level29, ShadeSpanFn, Original_ShadeSpan16_Level29, ShadeSpan16_Level29);
void __fastcall ShadeSpan16_Level30(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<30>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f5910, ShadeSpan16_Level30, ShadeSpanFn, Original_ShadeSpan16_Level30, ShadeSpan16_Level30);
void __fastcall ShadeSpan16_Level31(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    std::uint32_t odd) {
    ShadeSpan<31>(dstCell, dstEnd, srcCell, odd);
}
S3RE_HOOK(0x005f59e0, ShadeSpan16_Level31, ShadeSpanFn, Original_ShadeSpan16_Level31, ShadeSpan16_Level31);

// Purpose (all levels): draw one run of a sprite's pixels through a
// 16-entry dither mask, at one of 8 brightness levels (the "dissolving"
// top rows of a building that is being built).
void __fastcall MaskedSpan16_Level0(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    const std::uint8_t* mask, std::uint32_t* maskIndex) {
    MaskedSpan<0>(dstCell, dstEnd, srcCell, mask, maskIndex);
}
S3RE_HOOK(0x005fb470, MaskedSpan16_Level0, MaskedSpanFn, Original_MaskedSpan16_Level0, MaskedSpan16_Level0);
void __fastcall MaskedSpan16_Level1(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    const std::uint8_t* mask, std::uint32_t* maskIndex) {
    MaskedSpan<1>(dstCell, dstEnd, srcCell, mask, maskIndex);
}
S3RE_HOOK(0x005fb410, MaskedSpan16_Level1, MaskedSpanFn, Original_MaskedSpan16_Level1, MaskedSpan16_Level1);
void __fastcall MaskedSpan16_Level2(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    const std::uint8_t* mask, std::uint32_t* maskIndex) {
    MaskedSpan<2>(dstCell, dstEnd, srcCell, mask, maskIndex);
}
S3RE_HOOK(0x005fb3a0, MaskedSpan16_Level2, MaskedSpanFn, Original_MaskedSpan16_Level2, MaskedSpan16_Level2);
void __fastcall MaskedSpan16_Level3(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    const std::uint8_t* mask, std::uint32_t* maskIndex) {
    MaskedSpan<3>(dstCell, dstEnd, srcCell, mask, maskIndex);
}
S3RE_HOOK(0x005fb340, MaskedSpan16_Level3, MaskedSpanFn, Original_MaskedSpan16_Level3, MaskedSpan16_Level3);
void __fastcall MaskedSpan16_Level4(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    const std::uint8_t* mask, std::uint32_t* maskIndex) {
    MaskedSpan<4>(dstCell, dstEnd, srcCell, mask, maskIndex);
}
S3RE_HOOK(0x005fb2d0, MaskedSpan16_Level4, MaskedSpanFn, Original_MaskedSpan16_Level4, MaskedSpan16_Level4);
void __fastcall MaskedSpan16_Level5(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    const std::uint8_t* mask, std::uint32_t* maskIndex) {
    MaskedSpan<5>(dstCell, dstEnd, srcCell, mask, maskIndex);
}
S3RE_HOOK(0x005fb260, MaskedSpan16_Level5, MaskedSpanFn, Original_MaskedSpan16_Level5, MaskedSpan16_Level5);
void __fastcall MaskedSpan16_Level6(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    const std::uint8_t* mask, std::uint32_t* maskIndex) {
    MaskedSpan<6>(dstCell, dstEnd, srcCell, mask, maskIndex);
}
S3RE_HOOK(0x005fb1d0, MaskedSpan16_Level6, MaskedSpanFn, Original_MaskedSpan16_Level6, MaskedSpan16_Level6);
void __fastcall MaskedSpan16_Level7(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                    const std::uint8_t* mask, std::uint32_t* maskIndex) {
    MaskedSpan<7>(dstCell, dstEnd, srcCell, mask, maskIndex);
}
S3RE_HOOK(0x005fb180, MaskedSpan16_Level7, MaskedSpanFn, Original_MaskedSpan16_Level7, MaskedSpan16_Level7);

void __fastcall ShadowSpan_Dither(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* phaseCell,
                                  std::uint32_t parity);
void __fastcall ShadowSpan_Halve(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* phaseCell,
                                 std::uint32_t parity);
void __fastcall SelectBrightnessSpans(std::uint32_t brightness);
void __fastcall SetShadowSpan(std::uint32_t halfMask16, std::uint32_t halfMask32, std::uint32_t mask18,
                              std::uint32_t mask1c, std::uint32_t method);
void __fastcall CGraphicsData_SetShadowMethod(void* self, void* edx, std::uint32_t method);

S3RE_HOOK(0x005fd080, ShadowSpan_Dither, ShadowSpanFn, Original_ShadowSpan_Dither, ShadowSpan_Dither);

// Purpose: draws one run of a shadow as a black checkerboard (shadow method
// 1, probably for slow machines).
void __fastcall ShadowSpan_Dither(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* phaseCell,
                                  std::uint32_t parity) {
    std::uint32_t dst = *dstCell;
    std::uint32_t phase = *phaseCell;  // read once (the original keeps it in a register)
    if (dst >= dstEnd) {
        return;
    }
    do {
        if ((phase + parity) & 1) {
            Store16(dst, 0);
        }
        dst += 2;
        phase = (phase + 1) & kShadowPhaseWrap;
    } while (dst != dstEnd);
    *dstCell = dst;
    *phaseCell = phase;
}

S3RE_HOOK(0x005fd0c0, ShadowSpan_Halve, ShadowSpanFn, Original_ShadowSpan_Halve, ShadowSpan_Halve);

// Purpose: draws one run of a shadow by halving the brightness of what is
// already on the screen (shadow method 0, the default).
void __fastcall ShadowSpan_Halve(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* /*phaseCell*/,
                                 std::uint32_t /*parity*/) {
    std::uint32_t dst = *dstCell;
    if (dst >= dstEnd) {
        return;
    }
    if (dst & 2) {  // one pixel up to a 4-byte boundary
        Store16(dst, (Load16(dst) >> 1) & g_shadeMasks->pixel[0]);
        dst += 2;
    }
    const std::uint32_t alignedEnd = dstEnd & ~3u;
    for (; dst != alignedEnd; dst += 4) {
        Store32(dst, (Load32(dst) >> 1) & g_shadeMasks->pair[0]);
    }
    if (alignedEnd != dstEnd) {
        Store16(dst, (Load16(dst) >> 1) & g_shadeMasks->pixel[0]);
        dst += 2;
    }
    *dstCell = dst;
}

S3RE_HOOK(0x005fb4c0, SelectBrightnessSpans, SelectSpansFn, Original_SelectBrightnessSpans, SelectBrightnessSpans);

// Purpose: picks the two span functions for drawing sprites at a
// brightness (0..255).
void __fastcall SelectBrightnessSpans(std::uint32_t brightness) {
    const std::uint32_t shade = g_shadeSpans[brightness >> 3];
    const std::uint32_t masked = g_maskedSpans[brightness >> 5];
    g_shadeSpan = shade;
    g_maskedSpan = masked;
}

S3RE_HOOK(0x005fcd30, SetShadowSpan, SetShadowSpanFn, Original_SetShadowSpan, SetShadowSpan);

// Purpose: chooses how shadows are drawn (halved or dithered) and keeps the
// pixel-format masks for them.
void __fastcall SetShadowSpan(std::uint32_t halfMask16, std::uint32_t halfMask32, std::uint32_t mask18,
                              std::uint32_t mask1c, std::uint32_t method) {
    g_shadowSpan->halfMask16 = halfMask16;
    g_shadowSpan->mask18 = mask18;
    g_shadowSpan->halfMask32 = halfMask32;
    g_shadowSpan->mask1c = mask1c;
    g_shadowSpan->method = method;
    if (method == kShadowMethodDither) {
        g_shadowSpan->span = kShadowSpanDither;
    } else if (method == kShadowMethodHalve) {
        g_shadowSpan->span = kShadowSpanHalve;
    } else {
        reinterpret_cast<AssertFn>(kThrowAssertmentFailed)(texts::kInvalidShadowMethod, texts::kGraphicsDataFile,
                                                            kInvalidShadowMethodLine);
    }
}

S3RE_HOOK(0x005faa70, CGraphicsData_SetShadowMethod, SetShadowMethodFn, Original_CGraphicsData_SetShadowMethod,
          CGraphicsData_SetShadowMethod);

// Purpose: the graphics object's way to choose the shadow drawing method
// (from the game's settings).
void __fastcall CGraphicsData_SetShadowMethod(void* /*self*/, void* /*edx*/, std::uint32_t method) {
    reinterpret_cast<SetShadowSpanFn>(kSetShadowSpan)(g_shadeMasks->pixel[0], g_shadeMasks->pair[0],
                                                      g_shadeMasks->shadow[0], g_shadeMasks->shadow[1], method);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Each span runs on two identical random buffers (pixels, the cells holding
// the dst/src pointers and the mask index, guard bytes around everything),
// one with the original and one with ours; every byte is compared. The
// masks are the real RGB565 and RGB555 values or random ones.

namespace {

constexpr std::uint32_t kBufferBytes = 0x800;

// One side's memory: dst pixels, src pixels, the cells and the dither mask.
struct SpanBuffers {
    std::uint8_t dst[kBufferBytes];
    std::uint8_t src[kBufferBytes];
    std::uint32_t cells[8];  // [2] = dst cell, [4] = src/phase cell, [6] = mask index
    std::uint8_t mask[64];
};

SpanBuffers g_spanA;
SpanBuffers g_spanB;

void SetRandomMasks(test::Context& ctx) {
    static constexpr std::uint32_t k565[5] = {0x7bef, 0x39e7, 0x18e3, 0x861, 0x20};
    static constexpr std::uint32_t k555[5] = {0x3def, 0x1ce7, 0xc63, 0x421, 0};
    const std::uint32_t kind = ctx.rng().Below(3);
    for (int k = 0; k < 5; ++k) {
        const std::uint32_t pixel = kind == 0 ? k565[k] : kind == 1 ? k555[k] : ctx.rng().Next32();
        g_shadeMasks->pixel[k] = pixel;
        g_shadeMasks->pair[k] = kind < 2 ? (pixel << 16 | pixel) : ctx.rng().Next32();
    }
    g_shadeMasks->shadow[0] = ctx.rng().Next32();
    g_shadeMasks->shadow[1] = ctx.rng().Next32();
}

// A dst run: start (even, sometimes odd-aligned to 2) and a length.
struct Run {
    std::uint32_t start;  // offset in dst
    std::uint32_t units;  // dwords (shade), pixels (masked, shadow)
};

Run RandomRun(test::Context& ctx, std::uint32_t unitBytes) {
    const std::uint32_t units = ctx.rng().Chance(1, 8) ? 0 : ctx.rng().Below(200);
    const std::uint32_t start = 16 + 2 * ctx.rng().Below(64);
    (void)unitBytes;
    return {start, units};
}

void FillSides(test::Context& ctx) {
    ctx.rng().Fill(&g_spanA, sizeof(g_spanA));
    std::memcpy(&g_spanB, &g_spanA, sizeof(g_spanA));
}

bool CompareSides(test::Context& ctx) {
    return S3RE_EXPECT_MEM_EQ(&g_spanA, &g_spanB, sizeof(g_spanA));
}

std::uint32_t Address(const void* p) {
    return reinterpret_cast<std::uint32_t>(p);
}

// Sets the cells of one side for a run; returns dstEnd.
std::uint32_t PrepareSide(SpanBuffers& side, const Run& run, std::uint32_t unitBytes, std::uint32_t srcStart,
                          std::uint32_t maskIndex) {
    side.cells[2] = Address(side.dst + run.start);
    side.cells[4] = Address(side.src + srcStart);
    side.cells[6] = maskIndex;
    return Address(side.dst + run.start + run.units * unitBytes);
}

void CompareShadeSpans(test::Context& ctx, const ShadeSpanFn (&candidates)[32], int cases) {
    static const ShadeSpanFn kOriginals[32] = {
        Original_ShadeSpan16_Level00, Original_ShadeSpan16_Level01, Original_ShadeSpan16_Level02,
        Original_ShadeSpan16_Level03, Original_ShadeSpan16_Level04, Original_ShadeSpan16_Level05,
        Original_ShadeSpan16_Level06, Original_ShadeSpan16_Level07, Original_ShadeSpan16_Level08,
        Original_ShadeSpan16_Level09, Original_ShadeSpan16_Level10, Original_ShadeSpan16_Level11,
        Original_ShadeSpan16_Level12, Original_ShadeSpan16_Level13, Original_ShadeSpan16_Level14,
        Original_ShadeSpan16_Level15, Original_ShadeSpan16_Level16, Original_ShadeSpan16_Level17,
        Original_ShadeSpan16_Level18, Original_ShadeSpan16_Level19, Original_ShadeSpan16_Level20,
        Original_ShadeSpan16_Level21, Original_ShadeSpan16_Level22, Original_ShadeSpan16_Level23,
        Original_ShadeSpan16_Level24, Original_ShadeSpan16_Level25, Original_ShadeSpan16_Level26,
        Original_ShadeSpan16_Level27, Original_ShadeSpan16_Level28, Original_ShadeSpan16_Level29,
        Original_ShadeSpan16_Level30, Original_ShadeSpan16_Level31};
    const ShadeMasks saved = g_shadeMasks;
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t level = static_cast<std::uint32_t>(i) % 32;
        SetRandomMasks(ctx);
        FillSides(ctx);
        Run run = RandomRun(ctx, 4);
        run.units = run.units % 160;
        const std::uint32_t srcStart = 2 * ctx.rng().Below(64);
        // Sometimes src and dst are the same buffer region (overlap).
        const bool same = ctx.rng().Chance(1, 10);
        const std::uint32_t odd = ctx.rng().Chance(1, 2) ? (ctx.rng().Next32() | 1) & 0xff
                                                         : (ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Next32() & ~0xffu);
        const std::uint32_t endA = PrepareSide(g_spanA, run, 4, srcStart, 0);
        const std::uint32_t endB = PrepareSide(g_spanB, run, 4, srcStart, 0);
        if (same) {
            g_spanA.cells[4] = g_spanA.cells[2] + 2 * ctx.rng().Below(3);
            g_spanB.cells[4] = g_spanB.cells[2] + (g_spanA.cells[4] - g_spanA.cells[2]);
        }
        kOriginals[level](&g_spanA.cells[2], endA, &g_spanA.cells[4], odd);
        candidates[level](&g_spanB.cells[2], endB, &g_spanB.cells[4], odd);
        // The cells hold pointers into each side's own buffers.
        g_spanB.cells[2] = g_spanB.cells[2] - Address(&g_spanB) + Address(&g_spanA);
        g_spanB.cells[4] = g_spanB.cells[4] - Address(&g_spanB) + Address(&g_spanA);
        if (!CompareSides(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, level %u, %u dwords, odd %u", i, level, run.units, odd);
            break;
        }
    }
    g_shadeMasks = saved;
}

void CompareMaskedSpans(test::Context& ctx, const MaskedSpanFn (&candidates)[8], int cases) {
    static const MaskedSpanFn kOriginals[8] = {
        Original_MaskedSpan16_Level0, Original_MaskedSpan16_Level1, Original_MaskedSpan16_Level2,
        Original_MaskedSpan16_Level3, Original_MaskedSpan16_Level4, Original_MaskedSpan16_Level5,
        Original_MaskedSpan16_Level6, Original_MaskedSpan16_Level7};
    const ShadeMasks saved = g_shadeMasks;
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t level = static_cast<std::uint32_t>(i) % 8;
        SetRandomMasks(ctx);
        FillSides(ctx);
        // Mask entries: mostly 0/1, sometimes any byte.
        for (std::uint8_t& entry : g_spanA.mask) {
            entry = ctx.rng().Chance(1, 8) ? static_cast<std::uint8_t>(ctx.rng().Next32())
                                           : static_cast<std::uint8_t>(ctx.rng().Below(2));
        }
        std::memcpy(g_spanB.mask, g_spanA.mask, sizeof(g_spanA.mask));
        const Run run = RandomRun(ctx, 2);
        const std::uint32_t srcStart = 2 * ctx.rng().Below(64);
        const std::uint32_t maskIndex = ctx.rng().Below(16);
        const std::uint32_t endA = PrepareSide(g_spanA, run, 2, srcStart, maskIndex);
        const std::uint32_t endB = PrepareSide(g_spanB, run, 2, srcStart, maskIndex);
        kOriginals[level](&g_spanA.cells[2], endA, &g_spanA.cells[4], g_spanA.mask, &g_spanA.cells[6]);
        candidates[level](&g_spanB.cells[2], endB, &g_spanB.cells[4], g_spanB.mask, &g_spanB.cells[6]);
        g_spanB.cells[2] = g_spanB.cells[2] - Address(&g_spanB) + Address(&g_spanA);
        g_spanB.cells[4] = g_spanB.cells[4] - Address(&g_spanB) + Address(&g_spanA);
        if (!CompareSides(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, level %u, %u pixels", i, level, run.units);
            break;
        }
    }
    g_shadeMasks = saved;
}

struct ShadowFunctions {
    ShadowSpanFn dither;
    ShadowSpanFn halve;
    SelectSpansFn select;
    SetShadowSpanFn setSpan;
    SetShadowMethodFn setMethod;
};

void CompareShadowAndSelect(test::Context& ctx, const ShadowFunctions& candidate, int cases) {
    const ShadowFunctions original{Original_ShadowSpan_Dither, Original_ShadowSpan_Halve,
                                   Original_SelectBrightnessSpans, Original_SetShadowSpan,
                                   Original_CGraphicsData_SetShadowMethod};
    const ShadeMasks savedMasks = g_shadeMasks;
    const ShadowSpanState savedShadow = g_shadowSpan;
    const std::uint32_t savedShade = g_shadeSpan;
    const std::uint32_t savedMasked = g_maskedSpan;
    for (int i = 0; i < cases; ++i) {
        const int op = i % 4;
        SetRandomMasks(ctx);
        bool same = true;
        if (op <= 1) {
            FillSides(ctx);
            const Run run = RandomRun(ctx, 2);
            const std::uint32_t phase = ctx.rng().Chance(1, 4) ? ctx.rng().Next32() : ctx.rng().Below(32);
            const std::uint32_t parity = ctx.rng().Chance(1, 4) ? ctx.rng().Next32() : ctx.rng().Below(2);
            std::uint32_t endA = PrepareSide(g_spanA, run, 2, 0, 0);
            std::uint32_t endB = PrepareSide(g_spanB, run, 2, 0, 0);
            g_spanA.cells[4] = g_spanB.cells[4] = phase;
            if (ctx.rng().Chance(1, 10)) {  // dst past the end: nothing happens
                endA -= 2 * run.units + 2;
                endB -= 2 * run.units + 2;
            }
            if (op == 0) {
                original.dither(&g_spanA.cells[2], endA, &g_spanA.cells[4], parity);
                candidate.dither(&g_spanB.cells[2], endB, &g_spanB.cells[4], parity);
            } else {
                original.halve(&g_spanA.cells[2], endA, &g_spanA.cells[4], parity);
                candidate.halve(&g_spanB.cells[2], endB, &g_spanB.cells[4], parity);
            }
            g_spanB.cells[2] = g_spanB.cells[2] - Address(&g_spanB) + Address(&g_spanA);
            same &= CompareSides(ctx);
        } else if (op == 2) {
            const std::uint32_t brightness = ctx.rng().Below(256);
            g_shadeSpan = 1;
            g_maskedSpan = 2;
            original.select(brightness);
            const std::uint32_t shadeA = g_shadeSpan, maskedA = g_maskedSpan;
            g_shadeSpan = 1;
            g_maskedSpan = 2;
            candidate.select(brightness);
            same &= S3RE_EXPECT_EQ(shadeA, static_cast<std::uint32_t>(g_shadeSpan));
            same &= S3RE_EXPECT_EQ(maskedA, static_cast<std::uint32_t>(g_maskedSpan));
        } else {
            // Methods 0 and 1 only: other values throw (the emulator spec covers that path).
            const std::uint32_t method = ctx.rng().Below(2);
            const bool viaMethod = ctx.rng().Chance(1, 2);
            const std::uint32_t args[4] = {ctx.rng().Next32(), ctx.rng().Next32(), ctx.rng().Next32(),
                                           ctx.rng().Next32()};
            ShadowSpanState results[2];
            for (int side = 0; side < 2; ++side) {
                std::memset(&g_shadowSpan.get(), 0x5a, sizeof(ShadowSpanState));
                if (viaMethod) {
                    (side == 0 ? original.setMethod : candidate.setMethod)(nullptr, nullptr, method);
                } else {
                    (side == 0 ? original.setSpan : candidate.setSpan)(args[0], args[1], args[2], args[3], method);
                }
                results[side] = g_shadowSpan;
            }
            same &= S3RE_EXPECT_MEM_EQ(&results[0], &results[1], sizeof(ShadowSpanState));
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
            break;
        }
    }
    g_shadeMasks = savedMasks;
    g_shadowSpan = savedShadow;
    g_shadeSpan = savedShade;
    g_maskedSpan = savedMasked;
}

constexpr ShadeSpanFn kOurShadeSpans[32] = {
    ShadeSpan16_Level00, ShadeSpan16_Level01, ShadeSpan16_Level02, ShadeSpan16_Level03, ShadeSpan16_Level04,
    ShadeSpan16_Level05, ShadeSpan16_Level06, ShadeSpan16_Level07, ShadeSpan16_Level08, ShadeSpan16_Level09,
    ShadeSpan16_Level10, ShadeSpan16_Level11, ShadeSpan16_Level12, ShadeSpan16_Level13, ShadeSpan16_Level14,
    ShadeSpan16_Level15, ShadeSpan16_Level16, ShadeSpan16_Level17, ShadeSpan16_Level18, ShadeSpan16_Level19,
    ShadeSpan16_Level20, ShadeSpan16_Level21, ShadeSpan16_Level22, ShadeSpan16_Level23, ShadeSpan16_Level24,
    ShadeSpan16_Level25, ShadeSpan16_Level26, ShadeSpan16_Level27, ShadeSpan16_Level28, ShadeSpan16_Level29,
    ShadeSpan16_Level30, ShadeSpan16_Level31};

constexpr MaskedSpanFn kOurMaskedSpans[8] = {MaskedSpan16_Level0, MaskedSpan16_Level1, MaskedSpan16_Level2,
                                             MaskedSpan16_Level3, MaskedSpan16_Level4, MaskedSpan16_Level5,
                                             MaskedSpan16_Level6, MaskedSpan16_Level7};

// Wrong: level 17 uses level 16's fraction (one term missing).
void __fastcall WrongShadeSpan17(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                 std::uint32_t odd) {
    ShadeSpan<16>(dstCell, dstEnd, srcCell, odd);
}

// Wrong: the mask index wraps at 32 instead of 16.
void __fastcall WrongMaskedSpan3(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* srcCell,
                                 const std::uint8_t* mask, std::uint32_t* maskIndex) {
    std::uint32_t dst = *dstCell;
    std::uint32_t src = *srcCell;
    for (; dst != dstEnd; dst += 2, src += 2) {
        if (mask[*maskIndex] != 0) {
            Store16(dst, (Load16(src) >> 1) & g_shadeMasks->pixel[0]);
        }
        *maskIndex = (*maskIndex + 1) & 31;
    }
    *dstCell = dst;
    *srcCell = src;
}

// Wrong: the dither phase wraps at 16 (an odd parity sum differs after 16).
void __fastcall WrongDither(std::uint32_t* dstCell, std::uint32_t dstEnd, std::uint32_t* phaseCell,
                            std::uint32_t parity) {
    std::uint32_t dst = *dstCell;
    std::uint32_t phase = *phaseCell;
    if (dst >= dstEnd) {
        return;
    }
    do {
        if ((phase + parity) & 1) {
            Store16(dst, 0);
        }
        dst += 2;
        phase = (phase + 1) & 15;
    } while (dst != dstEnd);
    *dstCell = dst;
    *phaseCell = phase;
}

}  // namespace

S3RE_TEST(ShadeSpans_MatchOriginal) {
    CompareShadeSpans(ctx, kOurShadeSpans, 32 * 400);
}

S3RE_TEST(ShadeSpans_ComparisonDetectsWrongImplementation) {
    ShadeSpanFn wrong[32];
    std::memcpy(wrong, kOurShadeSpans, sizeof(wrong));
    wrong[17] = WrongShadeSpan17;
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareShadeSpans(probe, wrong, 32 * 50);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(MaskedSpans_MatchOriginal) {
    CompareMaskedSpans(ctx, kOurMaskedSpans, 8 * 1000);
}

S3RE_TEST(MaskedSpans_ComparisonDetectsWrongImplementation) {
    MaskedSpanFn wrong[8];
    std::memcpy(wrong, kOurMaskedSpans, sizeof(wrong));
    wrong[3] = WrongMaskedSpan3;
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMaskedSpans(probe, wrong, 8 * 100);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(ShadowSpans_MatchOriginal) {
    CompareShadowAndSelect(ctx, {ShadowSpan_Dither, ShadowSpan_Halve, SelectBrightnessSpans, SetShadowSpan,
                                 CGraphicsData_SetShadowMethod},
                           8000);
}

S3RE_TEST(ShadowSpans_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareShadowAndSelect(probe, {WrongDither, ShadowSpan_Halve, SelectBrightnessSpans, SetShadowSpan,
                                   CGraphicsData_SetShadowMethod},
                           800);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
