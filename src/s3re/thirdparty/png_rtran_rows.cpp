// libpng 1.0.2's per-row read transformations (pngtrans.c / pngrtran.c),
// statically linked into S3.EXE for PaintLib's PNG decoder
// (re/picture_code.md). All cdecl, (row_info, row, ...); no null checks
// (PNG_USELESS_TESTS is off). They work in place: the expanding ones walk
// from the end of the row backwards.
//
// png_do_invert (0x005a5e60): 1-bit grey only: every byte of the row inverted.
// png_do_swap (0x005a5e90): 16-bit samples: byte order swapped.
// png_do_packswap (0x005a5ec0): 1/2/4-bit samples: pixel order within each
//   byte reversed through the tables at 0x733884 / 0x733984 / 0x733a84.
// png_do_strip_filler (0x005a5f10): (row_info, row, flags): drops the filler
//   or alpha channel of 4-channel (RGBX/XRGB) and 2-channel rows; anything
//   but 8 bits counts as 16.
// png_do_bgr (0x005a6120): RGB(A) to BGR(A), 8 and 16 bits.
// png_do_unpack (0x005a7030): 1/2/4-bit samples to one byte each.
// png_do_unshift (0x005a7160): (row_info, row, sig_bits): shifts samples
//   down to their significant bits (not for palette images).
// png_do_read_swap_alpha (0x005a7340): RGBA to ARGB, GA to AG.
// png_do_read_invert_alpha (0x005a7440): alpha inverted (255 - a).
// png_do_read_filler (0x005a7550): (row_info, row, filler, flags): adds a
//   filler channel to grey and RGB rows. Reproduced 1.0.2 bugs: the
//   FILLER_AFTER loops skip the first pixel's filler byte, and the 16-bit
//   cases address the row as if it had 8-bit samples (source at row +
//   width, resp. + 3 * width) and set rowbytes 2 * width / 4 * width and,
//   for grey before, a pixel depth of 16.
// png_do_gray_to_rgb (0x005a77b0): grey (8/16 bit, with or without alpha) to
//   RGB; the channel count, pixel depth and rowbytes are updated for every
//   colour type without the colour bit, also when no row loop ran.
// png_do_expand_palette (0x005a8cf0): (row_info, row, palette, trans,
//   num_trans): 1/2/4-bit indices unpacked, then indices to RGB, or to RGBA
//   with a transparency table.
// png_do_expand (0x005a8f20): (row_info, row, trans_value): grey below 8
//   bits scaled to 8 bits; with a transparent colour, grey to grey+alpha and
//   RGB to RGBA (alpha 0 where the pixel equals it, 255 elsewhere). The
//   fields are updated also for depths the loops do not handle.
// png_do_dither (0x005a92b0): (row_info, row, palette_lookup, dither_lookup):
//   8-bit RGB(A) to palette indices through a 5:5:5 lookup, or palette
//   indices remapped.

#include "game/tables.h"
#include "hooks.h"
#include "test/difftest.h"
#include "thirdparty/png_struct.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace s3re::thirdparty {

using game::g_pngFourBppSwap;
using game::g_pngOneBppSwap;
using game::g_pngTwoBppSwap;

namespace {

constexpr std::uint8_t kGray = 0;
constexpr std::uint8_t kRgb = 2;
constexpr std::uint8_t kPalette = 3;
constexpr std::uint8_t kGrayAlpha = 4;
constexpr std::uint8_t kRgbAlpha = 6;

struct PngColor {
    std::uint8_t red, green, blue;
};
static_assert(sizeof(PngColor) == 3);

// Unpacks 1/2/4-bit samples to bytes, last pixel first (png_do_unpack and
// png_do_expand_palette share the loops; `firstShift` is the bit position of
// the last pixel).
void UnpackBits(std::uint8_t* row, std::uint32_t width, std::uint32_t bits, std::uint32_t firstShift) {
    const std::uint32_t perByte = 8 / bits;
    const std::uint8_t mask = static_cast<std::uint8_t>((1u << bits) - 1);
    const std::uint32_t lastShift = 8 - bits;
    const std::uint8_t* sp = row + (width - 1) / perByte;
    std::uint8_t* dp = row + width - 1;
    std::uint32_t shift = firstShift;
    for (std::uint32_t i = 0; i < width; ++i) {
        *dp = static_cast<std::uint8_t>((*sp >> shift) & mask);
        if (shift == lastShift) {
            shift = 0;
            --sp;
        } else {
            shift += bits;
        }
        --dp;
    }
}

}  // namespace

using RowFn = void(__cdecl*)(PngRowInfo* info, std::uint8_t* row);
using RowFlagsFn = void(__cdecl*)(PngRowInfo* info, std::uint8_t* row, std::uint32_t flags);
using UnshiftFn = void(__cdecl*)(PngRowInfo* info, std::uint8_t* row, const PngColor8* sigBits);
using ReadFillerFn = void(__cdecl*)(PngRowInfo* info, std::uint8_t* row, std::uint32_t filler, std::uint32_t flags);
using ExpandFn = void(__cdecl*)(PngRowInfo* info, std::uint8_t* row, const PngColor16* trans);
using DitherFn = void(__cdecl*)(PngRowInfo* info, std::uint8_t* row, const std::uint8_t* paletteLookup,
                                const std::uint8_t* ditherLookup);
using ExpandPaletteFn = void(__cdecl*)(PngRowInfo* info, std::uint8_t* row, const void* palette,
                                       const std::uint8_t* trans, std::int32_t numTrans);

void __cdecl png_do_invert(PngRowInfo* info, std::uint8_t* row);
void __cdecl png_do_swap(PngRowInfo* info, std::uint8_t* row);
void __cdecl png_do_packswap(PngRowInfo* info, std::uint8_t* row);
void __cdecl png_do_strip_filler(PngRowInfo* info, std::uint8_t* row, std::uint32_t flags);
void __cdecl png_do_bgr(PngRowInfo* info, std::uint8_t* row);
void __cdecl png_do_unpack(PngRowInfo* info, std::uint8_t* row);
void __cdecl png_do_unshift(PngRowInfo* info, std::uint8_t* row, const PngColor8* sigBits);
void __cdecl png_do_read_swap_alpha(PngRowInfo* info, std::uint8_t* row);
void __cdecl png_do_read_invert_alpha(PngRowInfo* info, std::uint8_t* row);
void __cdecl png_do_read_filler(PngRowInfo* info, std::uint8_t* row, std::uint32_t filler, std::uint32_t flags);
void __cdecl png_do_gray_to_rgb(PngRowInfo* info, std::uint8_t* row);
void __cdecl png_do_expand_palette(PngRowInfo* info, std::uint8_t* row, const void* palette,
                                   const std::uint8_t* trans, std::int32_t numTrans);
void __cdecl png_do_expand(PngRowInfo* info, std::uint8_t* row, const PngColor16* trans);
void __cdecl png_do_dither(PngRowInfo* info, std::uint8_t* row, const std::uint8_t* paletteLookup,
                           const std::uint8_t* ditherLookup);

S3RE_HOOK(0x005a5e60, png_do_invert, RowFn, Original_png_do_invert, png_do_invert);
S3RE_HOOK(0x005a5e90, png_do_swap, RowFn, Original_png_do_swap, png_do_swap);
S3RE_HOOK(0x005a5ec0, png_do_packswap, RowFn, Original_png_do_packswap, png_do_packswap);
S3RE_HOOK(0x005a5f10, png_do_strip_filler, RowFlagsFn, Original_png_do_strip_filler, png_do_strip_filler);
S3RE_HOOK(0x005a6120, png_do_bgr, RowFn, Original_png_do_bgr, png_do_bgr);
S3RE_HOOK(0x005a7030, png_do_unpack, RowFn, Original_png_do_unpack, png_do_unpack);
S3RE_HOOK(0x005a7160, png_do_unshift, UnshiftFn, Original_png_do_unshift, png_do_unshift);
S3RE_HOOK(0x005a7340, png_do_read_swap_alpha, RowFn, Original_png_do_read_swap_alpha, png_do_read_swap_alpha);
S3RE_HOOK(0x005a7440, png_do_read_invert_alpha, RowFn, Original_png_do_read_invert_alpha, png_do_read_invert_alpha);
S3RE_HOOK(0x005a7550, png_do_read_filler, ReadFillerFn, Original_png_do_read_filler, png_do_read_filler);
S3RE_HOOK(0x005a77b0, png_do_gray_to_rgb, RowFn, Original_png_do_gray_to_rgb, png_do_gray_to_rgb);
S3RE_HOOK(0x005a8cf0, png_do_expand_palette, ExpandPaletteFn, Original_png_do_expand_palette, png_do_expand_palette);
S3RE_HOOK(0x005a8f20, png_do_expand, ExpandFn, Original_png_do_expand, png_do_expand);
S3RE_HOOK(0x005a92b0, png_do_dither, DitherFn, Original_png_do_dither, png_do_dither);

// Purpose: Inverts a black-and-white row (PNG_INVERT_MONO).
void __cdecl png_do_invert(PngRowInfo* info, std::uint8_t* row) {
    if (info->bit_depth != 1 || info->color_type != kGray) {
        return;
    }
    for (std::uint32_t i = 0; i < info->rowbytes; ++i) {
        row[i] = static_cast<std::uint8_t>(~row[i]);
    }
}

// Purpose: Swaps the bytes of 16-bit samples (PNG_SWAP_BYTES).
void __cdecl png_do_swap(PngRowInfo* info, std::uint8_t* row) {
    if (info->bit_depth != 16) {
        return;
    }
    const std::uint32_t samples = info->width * info->channels;
    for (std::uint32_t i = 0; i < samples; ++i, row += 2) {
        const std::uint8_t first = row[0];
        row[0] = row[1];
        row[1] = first;
    }
}

// Purpose: Reverses the pixel order inside each byte of a 1/2/4-bit row
// (PNG_PACKSWAP).
void __cdecl png_do_packswap(PngRowInfo* info, std::uint8_t* row) {
    if (info->bit_depth >= 8) {
        return;
    }
    const std::uint8_t* table = nullptr;
    switch (info->bit_depth) {
        case 1: table = g_pngOneBppSwap; break;
        case 2: table = g_pngTwoBppSwap; break;
        case 4: table = g_pngFourBppSwap; break;
        default: return;
    }
    for (std::uint8_t* end = row + info->rowbytes; row < end; ++row) {
        *row = table[*row];
    }
}

// Purpose: Removes the filler (or alpha) channel from a row.
void __cdecl png_do_strip_filler(PngRowInfo* info, std::uint8_t* row, std::uint32_t flags) {
    const std::uint32_t width = info->width;
    const bool after = (flags & kPngFlagFillerAfter) != 0;
    const std::uint8_t* sp = row;
    std::uint8_t* dp = row;
    if (info->channels == 4) {
        if (info->bit_depth == 8) {
            if (after) {  // RGBX to RGB: the first pixel stays where it is
                sp += 4;
                dp += 3;
                for (std::uint32_t i = 1; i < width; ++i, ++sp) {
                    *dp++ = *sp++;
                    *dp++ = *sp++;
                    *dp++ = *sp++;
                }
            } else {  // XRGB to RGB
                for (std::uint32_t i = 0; i < width; ++i) {
                    ++sp;
                    *dp++ = *sp++;
                    *dp++ = *sp++;
                    *dp++ = *sp++;
                }
            }
            info->pixel_depth = 24;
            info->rowbytes = width * 3;
        } else {  // 16 bits (any other depth)
            if (after) {
                sp += 8;
                dp += 6;
                for (std::uint32_t i = 1; i < width; ++i, sp += 2) {
                    for (int k = 0; k < 6; ++k) {
                        *dp++ = *sp++;
                    }
                }
            } else {
                for (std::uint32_t i = 0; i < width; ++i) {
                    sp += 2;
                    for (int k = 0; k < 6; ++k) {
                        *dp++ = *sp++;
                    }
                }
            }
            info->pixel_depth = 48;
            info->rowbytes = width * 6;
        }
        info->channels = 3;
        info->color_type &= static_cast<std::uint8_t>(~kPngColorMaskAlpha);
    } else if (info->channels == 2) {
        if (info->bit_depth == 8) {
            if (after) {  // GX to G
                for (std::uint32_t i = 0; i < width; ++i, ++sp) {
                    *dp++ = *sp++;
                }
            } else {  // XG to G
                for (std::uint32_t i = 0; i < width; ++i) {
                    ++sp;
                    *dp++ = *sp++;
                }
            }
            info->pixel_depth = 8;
            info->rowbytes = width;
        } else {
            if (after) {
                sp += 4;
                dp += 2;
                for (std::uint32_t i = 1; i < width; ++i, sp += 2) {
                    *dp++ = *sp++;
                    *dp++ = *sp++;
                }
            } else {
                for (std::uint32_t i = 0; i < width; ++i) {
                    sp += 2;
                    *dp++ = *sp++;
                    *dp++ = *sp++;
                }
            }
            info->pixel_depth = 16;
            info->rowbytes = width * 2;
        }
        info->channels = 1;
        info->color_type &= static_cast<std::uint8_t>(~kPngColorMaskAlpha);
    }
}

// Purpose: Swaps red and blue (PNG_BGR, for the Windows bitmap order).
void __cdecl png_do_bgr(PngRowInfo* info, std::uint8_t* row) {
    if ((info->color_type & kPngColorMaskColor) == 0) {
        return;
    }
    const std::uint32_t width = info->width;
    const std::uint8_t type = info->color_type;
    std::uint32_t step = 0;
    if (info->bit_depth == 8) {
        if (type == kRgb) {
            step = 3;
        } else if (type == kRgbAlpha) {
            step = 4;
        } else {
            return;
        }
        for (std::uint32_t i = 0; i < width; ++i, row += step) {
            const std::uint8_t red = row[0];
            row[0] = row[2];
            row[2] = red;
        }
    } else if (info->bit_depth == 16) {
        if (type == kRgb) {
            step = 6;
        } else if (type == kRgbAlpha) {
            step = 8;
        } else {
            return;
        }
        for (std::uint32_t i = 0; i < width; ++i, row += step) {
            // The original's store order: byte 0, byte 4, byte 1, byte 5.
            const std::uint8_t red0 = row[0];
            row[0] = row[4];
            row[4] = red0;
            const std::uint8_t red1 = row[1];
            row[1] = row[5];
            row[5] = red1;
        }
    }
}

// Purpose: Unpacks 1/2/4-bit samples to a byte each (PNG_PACK).
void __cdecl png_do_unpack(PngRowInfo* info, std::uint8_t* row) {
    if (info->bit_depth >= 8) {
        return;
    }
    const std::uint32_t width = info->width;
    switch (info->bit_depth) {
        case 1: UnpackBits(row, width, 1, 7 - ((width - 1) & 7)); break;
        case 2: UnpackBits(row, width, 2, (3 - ((width - 1) & 3)) * 2); break;
        case 4: UnpackBits(row, width, 4, 4 - ((width - 1) & 1) * 4); break;
        default: break;  // other depths: only the fields below change
    }
    info->bit_depth = 8;
    info->pixel_depth = static_cast<std::uint8_t>(info->channels * 8);
    info->rowbytes = info->channels * width;
}

// Purpose: Shifts samples down to their significant bits (sBIT).
void __cdecl png_do_unshift(PngRowInfo* info, std::uint8_t* row, const PngColor8* sigBits) {
    if (info->color_type == kPalette) {
        return;
    }
    const std::int32_t depth = info->bit_depth;
    std::int32_t shift[4];
    std::uint32_t channels = 0;
    if ((info->color_type & kPngColorMaskColor) != 0) {
        shift[channels++] = depth - sigBits->red;
        shift[channels++] = depth - sigBits->green;
        shift[channels++] = depth - sigBits->blue;
    } else {
        shift[channels++] = depth - sigBits->gray;
    }
    if ((info->color_type & kPngColorMaskAlpha) != 0) {
        shift[channels++] = depth - sigBits->alpha;
    }
    bool any = false;
    for (std::uint32_t c = 0; c < channels; ++c) {
        if (shift[c] <= 0) {
            shift[c] = 0;
        } else {
            any = true;
        }
    }
    if (!any) {
        return;
    }
    switch (info->bit_depth) {
        case 2:
            for (std::uint32_t i = 0; i < info->rowbytes; ++i) {
                row[i] = static_cast<std::uint8_t>((row[i] >> 1) & 0x55);
            }
            break;
        case 4: {
            const std::uint8_t mask =
                static_cast<std::uint8_t>(((0xf0 >> shift[0]) & 0xf0) | (0x0f >> shift[0]));
            for (std::uint32_t i = 0; i < info->rowbytes; ++i) {
                row[i] = static_cast<std::uint8_t>((row[i] >> (shift[0] & 0xff)) & mask);
            }
            break;
        }
        case 8: {
            const std::uint32_t samples = info->width * channels;
            for (std::uint32_t i = 0; i < samples; ++i) {
                row[i] = static_cast<std::uint8_t>(row[i] >> (shift[i % channels] & 0xff));
            }
            break;
        }
        case 16: {
            const std::uint32_t samples = channels * info->width;
            for (std::uint32_t i = 0; i < samples; ++i, row += 2) {
                std::uint16_t value = static_cast<std::uint16_t>((row[0] << 8) + row[1]);
                value = static_cast<std::uint16_t>(value >> (shift[i % channels] & 0xff));
                row[0] = static_cast<std::uint8_t>(value >> 8);
                row[1] = static_cast<std::uint8_t>(value & 0xff);
            }
            break;
        }
        default: break;
    }
}

// Purpose: Moves the alpha channel in front of the colour (PNG_SWAP_ALPHA).
void __cdecl png_do_read_swap_alpha(PngRowInfo* info, std::uint8_t* row) {
    const std::uint32_t width = info->width;
    std::uint8_t* sp = row + info->rowbytes;
    std::uint8_t* dp = sp;
    if (info->color_type == kRgbAlpha) {
        if (info->bit_depth == 8) {  // RGBA to ARGB
            for (std::uint32_t i = 0; i < width; ++i) {
                const std::uint8_t save = *--sp;
                *--dp = *--sp;
                *--dp = *--sp;
                *--dp = *--sp;
                *--dp = save;
            }
        } else {  // RRGGBBAA to AARRGGBB
            for (std::uint32_t i = 0; i < width; ++i) {
                const std::uint8_t save0 = *--sp;
                const std::uint8_t save1 = *--sp;
                for (int k = 0; k < 6; ++k) {
                    *--dp = *--sp;
                }
                *--dp = save0;
                *--dp = save1;
            }
        }
    } else if (info->color_type == kGrayAlpha) {
        if (info->bit_depth == 8) {  // GA to AG
            for (std::uint32_t i = 0; i < width; ++i) {
                const std::uint8_t save = *--sp;
                *--dp = *--sp;
                *--dp = save;
            }
        } else {  // GGAA to AAGG
            for (std::uint32_t i = 0; i < width; ++i) {
                const std::uint8_t save0 = *--sp;
                const std::uint8_t save1 = *--sp;
                *--dp = *--sp;
                *--dp = *--sp;
                *--dp = save0;
                *--dp = save1;
            }
        }
    }
}

// Purpose: Inverts the alpha channel (PNG_INVERT_ALPHA).
void __cdecl png_do_read_invert_alpha(PngRowInfo* info, std::uint8_t* row) {
    const std::uint32_t width = info->width;
    std::uint8_t* sp = row + info->rowbytes;
    std::uint8_t* dp = sp;
    // The colour bytes are copied onto themselves, as the original does.
    if (info->color_type == kRgbAlpha) {
        const int alphaBytes = info->bit_depth == 8 ? 1 : 2;
        const int colourBytes = info->bit_depth == 8 ? 3 : 6;
        for (std::uint32_t i = 0; i < width; ++i) {
            for (int k = 0; k < alphaBytes; ++k) {
                *--dp = static_cast<std::uint8_t>(255 - *--sp);
            }
            for (int k = 0; k < colourBytes; ++k) {
                *--dp = *--sp;
            }
        }
    } else if (info->color_type == kGrayAlpha) {
        const int alphaBytes = info->bit_depth == 8 ? 1 : 2;
        for (std::uint32_t i = 0; i < width; ++i) {
            for (int k = 0; k < alphaBytes; ++k) {
                *--dp = static_cast<std::uint8_t>(255 - *--sp);
            }
            for (int k = 0; k < alphaBytes; ++k) {
                *--dp = *--sp;
            }
        }
    }
}

// Purpose: Adds a filler channel to grey and RGB rows (PNG_FILLER, e.g. RGB
// to 32-bit pixels).
void __cdecl png_do_read_filler(PngRowInfo* info, std::uint8_t* row, std::uint32_t filler, std::uint32_t flags) {
    const std::uint32_t width = info->width;
    const std::uint8_t high = static_cast<std::uint8_t>(filler >> 8);
    const std::uint8_t low = static_cast<std::uint8_t>(filler);
    const bool after = (flags & kPngFlagFillerAfter) != 0;
    if (info->color_type == kGray) {
        // Both depths start at row + width (1.0.2 bug for 16 bits).
        const std::uint8_t* sp = row + width;
        std::uint8_t* dp = row + 2 * width;
        if (info->bit_depth == 8) {
            if (after) {  // G to GX; the first pixel's filler is never written (1.0.2 bug)
                for (std::uint32_t i = 1; i < width; ++i) {
                    *--dp = low;
                    *--dp = *--sp;
                }
            } else {  // G to XG
                for (std::uint32_t i = 0; i < width; ++i) {
                    *--dp = *--sp;
                    *--dp = low;
                }
            }
            info->channels = 2;
            info->pixel_depth = 16;
            info->rowbytes = width * 2;
        } else if (info->bit_depth == 16) {
            if (after) {
                for (std::uint32_t i = 1; i < width; ++i) {
                    *--dp = high;
                    *--dp = low;
                    *--dp = *--sp;
                    *--dp = *--sp;
                }
                info->pixel_depth = 32;
            } else {
                for (std::uint32_t i = 0; i < width; ++i) {
                    *--dp = *--sp;
                    *--dp = *--sp;
                    *--dp = high;
                    *--dp = low;
                }
                info->pixel_depth = 16;  // 1.0.2 bug (32 for the other order)
            }
            info->channels = 2;
            info->rowbytes = width * 2;
        }
    } else if (info->color_type == kRgb) {
        // Both depths start at row + 3 * width (1.0.2 bug for 16 bits).
        const std::uint8_t* sp = row + 3 * width;
        std::uint8_t* dp = row + 4 * width;
        if (info->bit_depth == 8) {
            if (after) {  // RGB to RGBX
                for (std::uint32_t i = 1; i < width; ++i) {
                    *--dp = low;
                    *--dp = *--sp;
                    *--dp = *--sp;
                    *--dp = *--sp;
                }
            } else {  // RGB to XRGB
                for (std::uint32_t i = 0; i < width; ++i) {
                    *--dp = *--sp;
                    *--dp = *--sp;
                    *--dp = *--sp;
                    *--dp = low;
                }
            }
            info->channels = 4;
            info->pixel_depth = 32;
            info->rowbytes = width * 4;
        } else if (info->bit_depth == 16) {
            if (after) {
                for (std::uint32_t i = 1; i < width; ++i) {
                    *--dp = high;
                    *--dp = low;
                    for (int k = 0; k < 6; ++k) {
                        *--dp = *--sp;
                    }
                }
            } else {
                for (std::uint32_t i = 0; i < width; ++i) {
                    for (int k = 0; k < 6; ++k) {
                        *--dp = *--sp;
                    }
                    *--dp = high;
                    *--dp = low;
                }
            }
            info->channels = 4;
            info->pixel_depth = 64;
            info->rowbytes = width * 4;  // 1.0.2 bug (8 * width)
        }
    }
}

// Purpose: Expands grey rows to RGB (PNG_GRAY_TO_RGB).
void __cdecl png_do_gray_to_rgb(PngRowInfo* info, std::uint8_t* row) {
    if (info->bit_depth < 8 || (info->color_type & kPngColorMaskColor) != 0) {
        return;
    }
    const std::uint32_t width = info->width;
    if (info->color_type == kGray) {
        if (info->bit_depth == 8) {
            const std::uint8_t* sp = row + width - 1;
            std::uint8_t* dp = row + width - 1 + width * 2;
            for (std::uint32_t i = 0; i < width; ++i, --sp) {
                *dp-- = *sp;
                *dp-- = *sp;
                *dp-- = *sp;
            }
        } else {
            const std::uint8_t* sp = row + width * 2 - 1;
            std::uint8_t* dp = row + width * 2 - 1 + width * 4;
            for (std::uint32_t i = 0; i < width; ++i, sp -= 2) {
                for (int k = 0; k < 3; ++k) {
                    *dp-- = sp[0];
                    *dp-- = sp[-1];
                }
            }
        }
    } else if (info->color_type == kGrayAlpha) {
        if (info->bit_depth == 8) {
            const std::uint8_t* sp = row + width * 2 - 1;
            std::uint8_t* dp = row + width * 2 - 1 + width * 2;
            for (std::uint32_t i = 0; i < width; ++i) {
                *dp-- = *sp--;
                *dp-- = *sp;
                *dp-- = *sp;
                *dp-- = *sp--;
            }
        } else {
            const std::uint8_t* sp = row + width * 4 - 1;
            std::uint8_t* dp = row + width * 4 - 1 + width * 4;
            for (std::uint32_t i = 0; i < width; ++i) {
                *dp-- = *sp--;
                *dp-- = *sp--;
                for (int k = 0; k < 3; ++k) {
                    *dp-- = sp[0];
                    *dp-- = sp[-1];
                }
                sp -= 2;
            }
        }
    }
    info->channels = static_cast<std::uint8_t>(info->channels + 2);
    info->color_type |= kPngColorMaskColor;
    info->pixel_depth = static_cast<std::uint8_t>(info->channels * info->bit_depth);
    info->rowbytes = (width * info->pixel_depth + 7) >> 3;
}

// Purpose: Expands palette indices to RGB or RGBA pixels (PNG_EXPAND).
void __cdecl png_do_expand_palette(PngRowInfo* info, std::uint8_t* row, const void* palette,
                                   const std::uint8_t* trans, std::int32_t numTrans) {
    if (info->color_type != kPalette) {
        return;
    }
    const std::uint32_t width = info->width;
    if (info->bit_depth < 8) {
        switch (info->bit_depth) {
            case 1: UnpackBits(row, width, 1, 7 - ((width - 1) & 7)); break;
            case 2: UnpackBits(row, width, 2, (3 - ((width - 1) & 3)) * 2); break;
            case 4: UnpackBits(row, width, 4, (width & 1) << 2); break;
            default: break;
        }
        info->bit_depth = 8;
        info->pixel_depth = 8;
        info->rowbytes = width;
    }
    if (info->bit_depth != 8) {
        return;
    }
    const auto* colours = static_cast<const PngColor*>(palette);
    const std::uint8_t* sp = row + width - 1;
    if (trans != nullptr) {
        std::uint8_t* dp = row + width * 4 - 1;
        for (std::uint32_t i = 0; i < width; ++i, --sp) {
            *dp-- = static_cast<std::int32_t>(*sp) >= numTrans ? 0xff : trans[*sp];
            *dp-- = colours[*sp].blue;
            *dp-- = colours[*sp].green;
            *dp-- = colours[*sp].red;
        }
        info->rowbytes = width * 4;
        info->bit_depth = 8;
        info->pixel_depth = 32;
        info->color_type = kRgbAlpha;
        info->channels = 4;
    } else {
        std::uint8_t* dp = row + width * 3 - 1;
        for (std::uint32_t i = 0; i < width; ++i, --sp) {
            *dp-- = colours[*sp].blue;
            *dp-- = colours[*sp].green;
            *dp-- = colours[*sp].red;
        }
        info->bit_depth = 8;
        info->pixel_depth = 24;
        info->rowbytes = width * 3;
        info->color_type = kRgb;
        info->channels = 3;
    }
}

// Purpose: Expands low-depth grey to 8 bits and adds an alpha channel from
// the transparent colour (PNG_EXPAND with tRNS).
void __cdecl png_do_expand(PngRowInfo* info, std::uint8_t* row, const PngColor16* trans) {
    const std::uint32_t width = info->width;
    if (info->color_type == kGray) {
        std::uint16_t gray = trans != nullptr ? trans->gray : 0;
        if (info->bit_depth < 8) {
            std::uint8_t* dp = row + width - 1;
            switch (info->bit_depth) {
                case 1: {
                    gray = static_cast<std::uint16_t>(gray * 0xff);
                    const std::uint8_t* sp = row + ((width - 1) >> 3);
                    std::uint32_t shift = 7 - ((width - 1) & 7);
                    for (std::uint32_t i = 0; i < width; ++i, --dp) {
                        *dp = ((*sp >> shift) & 1) != 0 ? 0xff : 0;
                        if (shift == 7) {
                            shift = 0;
                            --sp;
                        } else {
                            ++shift;
                        }
                    }
                    break;
                }
                case 2: {
                    gray = static_cast<std::uint16_t>(gray * 0x55);
                    const std::uint8_t* sp = row + ((width - 1) >> 2);
                    std::uint32_t shift = (3 - ((width - 1) & 3)) * 2;
                    for (std::uint32_t i = 0; i < width; ++i, --dp) {
                        const std::uint8_t value = static_cast<std::uint8_t>((*sp >> shift) & 3);
                        *dp = static_cast<std::uint8_t>(value | (value << 2) | (value << 4) | (value << 6));
                        if (shift == 6) {
                            shift = 0;
                            --sp;
                        } else {
                            shift += 2;
                        }
                    }
                    break;
                }
                case 4: {
                    gray = static_cast<std::uint16_t>(gray * 0x11);
                    const std::uint8_t* sp = row + ((width - 1) >> 1);
                    std::uint32_t shift = 4 - ((width - 1) & 1) * 4;
                    for (std::uint32_t i = 0; i < width; ++i, --dp) {
                        const std::uint8_t value = static_cast<std::uint8_t>((*sp >> shift) & 0x0f);
                        *dp = static_cast<std::uint8_t>(value | (value << 4));
                        if (shift == 4) {
                            shift = 0;
                            --sp;
                        } else {
                            shift = 4;
                        }
                    }
                    break;
                }
                default: break;
            }
            info->bit_depth = 8;
            info->pixel_depth = 8;
            info->rowbytes = width;
        }
        if (trans == nullptr) {
            return;
        }
        if (info->bit_depth == 8) {
            const std::uint8_t* sp = row + width - 1;
            std::uint8_t* dp = row + width * 2 - 1;
            for (std::uint32_t i = 0; i < width; ++i) {
                *dp-- = *sp == gray ? 0 : 0xff;
                *dp-- = *sp--;
            }
        } else if (info->bit_depth == 16) {
            const std::uint8_t* sp = row + info->rowbytes - 1;
            std::uint8_t* dp = row + info->rowbytes * 2 - 1;
            for (std::uint32_t i = 0; i < width; ++i) {
                const std::uint8_t alpha = ((sp[-1] << 8) | sp[0]) == gray ? 0 : 0xff;
                *dp-- = alpha;
                *dp-- = alpha;
                *dp-- = *sp--;
                *dp-- = *sp--;
            }
        }
        info->color_type = kGrayAlpha;
        info->channels = 2;
        info->pixel_depth = static_cast<std::uint8_t>(info->bit_depth << 1);
        info->rowbytes = (width * info->pixel_depth) >> 3;
    } else if (info->color_type == kRgb && trans != nullptr) {
        if (info->bit_depth == 8) {
            const std::uint8_t* sp = row + info->rowbytes - 1;
            std::uint8_t* dp = row + width * 4 - 1;
            for (std::uint32_t i = 0; i < width; ++i) {
                const bool transparent = sp[-2] == trans->red && sp[-1] == trans->green && sp[0] == trans->blue;
                *dp-- = transparent ? 0 : 0xff;
                *dp-- = *sp--;
                *dp-- = *sp--;
                *dp-- = *sp--;
            }
        } else if (info->bit_depth == 16) {
            const std::uint8_t* sp = row + info->rowbytes - 1;
            std::uint8_t* dp = row + width * 8 - 1;
            for (std::uint32_t i = 0; i < width; ++i) {
                const bool transparent = ((sp[-5] << 8) | sp[-4]) == trans->red &&
                                         ((sp[-3] << 8) | sp[-2]) == trans->green &&
                                         ((sp[-1] << 8) | sp[0]) == trans->blue;
                const std::uint8_t alpha = transparent ? 0 : 0xff;
                *dp-- = alpha;
                *dp-- = alpha;
                for (int k = 0; k < 6; ++k) {
                    *dp-- = *sp--;
                }
            }
        }
        info->color_type = kRgbAlpha;
        info->channels = 4;
        info->pixel_depth = static_cast<std::uint8_t>(info->bit_depth << 2);
        info->rowbytes = (width * info->pixel_depth) >> 3;
    }
}

// Purpose: Reduces RGB to palette indices (PaintLib asks for 8-bit output).
void __cdecl png_do_dither(PngRowInfo* info, std::uint8_t* row, const std::uint8_t* paletteLookup,
                           const std::uint8_t* ditherLookup) {
    const std::uint32_t width = info->width;
    const bool rgb = info->color_type == kRgb && paletteLookup != nullptr && info->bit_depth == 8;
    const bool rgba = !rgb && info->color_type == kRgbAlpha && paletteLookup != nullptr && info->bit_depth == 8;
    if (rgb || rgba) {
        const std::uint8_t* sp = row;
        std::uint8_t* dp = row;
        for (std::uint32_t i = 0; i < width; ++i) {
            const std::uint32_t red = *sp++;
            const std::uint32_t green = *sp++;
            const std::uint32_t blue = *sp++;
            if (rgba) {
                ++sp;  // alpha
            }
            const std::uint32_t index = (((red >> 3) & 0x1f) << 10) | (((green >> 3) & 0x1f) << 5) | ((blue >> 3) & 0x1f);
            *dp++ = paletteLookup[index];
        }
        info->color_type = kPalette;
        info->channels = 1;
        info->pixel_depth = info->bit_depth;
        info->rowbytes = (width * info->pixel_depth + 7) >> 3;
    } else if (info->color_type == kPalette && ditherLookup != nullptr && info->bit_depth == 8) {
        for (std::uint32_t i = 0; i < width; ++i) {
            row[i] = ditherLookup[row[i]];
        }
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Random row infos (mostly valid colour type / depth / channel combinations,
// sometimes anything) and random rows; every function runs on a copy and the
// row info and the whole buffer are compared.

namespace {

constexpr std::size_t kRowBytes = 8192;
constexpr std::uint32_t kMaxWidth = 300;  // 16-bit RGBA: 8 bytes per pixel, expanded in place
// The row starts inside the buffer: with inconsistent row infos the
// backward loops reach up to 8 * width bytes before row + rowbytes.
constexpr std::size_t kRowStart = 3000;

struct RowFunctions {
    RowFn invert, swap, packswap;
    RowFlagsFn stripFiller;
    RowFn bgr, unpack;
    UnshiftFn unshift;
    RowFn swapAlpha, invertAlpha;
    ReadFillerFn readFiller;
    RowFn grayToRgb;
    ExpandPaletteFn expandPalette;
    ExpandFn expand;
    DitherFn dither;
};

constexpr int kRowOps = 14;

struct RowCase {
    PngRowInfo info;
    std::uint8_t row[kRowBytes];
};

void RandomRowInfo(test::Rng& rng, PngRowInfo& info) {
    static const std::uint8_t kTypes[] = {kGray, kRgb, kPalette, kGrayAlpha, kRgbAlpha};
    static const std::uint8_t kDepths[] = {1, 2, 4, 8, 16};
    info.color_type = rng.Chance(1, 10) ? static_cast<std::uint8_t>(rng.Below(8)) : kTypes[rng.Below(5)];
    info.bit_depth = rng.Chance(1, 10) ? static_cast<std::uint8_t>(rng.Range(0, 17)) : kDepths[rng.Below(5)];
    static const std::uint8_t kChannels[] = {1, 3, 1, 2, 0, 0, 4, 0};
    info.channels = rng.Chance(1, 8) ? static_cast<std::uint8_t>(rng.Range(1, 4)) : kChannels[info.color_type & 7];
    if (info.channels == 0) {
        info.channels = 1;
    }
    info.width = rng.Chance(1, 20) ? rng.Below(2) : rng.Range(1, kMaxWidth);
    info.pixel_depth = static_cast<std::uint8_t>(info.channels * info.bit_depth);
    info.rowbytes = (info.width * info.pixel_depth + 7) >> 3;
    if (rng.Chance(1, 20)) {
        info.rowbytes = rng.Below(2048);
    }
}

void RunRow(const RowFunctions& f, int op, RowCase& c, const std::uint8_t* extra, std::uint32_t x, std::uint32_t y) {
    std::uint8_t* const row = c.row + kRowStart;
    switch (op) {
        case 0: f.invert(&c.info, row); break;
        case 1: f.swap(&c.info, row); break;
        case 2: f.packswap(&c.info, row); break;
        case 3: f.stripFiller(&c.info, row, x); break;
        case 4: f.bgr(&c.info, row); break;
        case 5: f.unpack(&c.info, row); break;
        case 6: f.unshift(&c.info, row, reinterpret_cast<const PngColor8*>(extra)); break;
        case 7: f.swapAlpha(&c.info, row); break;
        case 8: f.invertAlpha(&c.info, row); break;
        case 9: f.readFiller(&c.info, row, x, y); break;
        case 10: f.grayToRgb(&c.info, row); break;
        case 11:
            f.expandPalette(&c.info, row, extra, (x & 1) != 0 ? extra + 768 : nullptr,
                            static_cast<std::int32_t>(y % 300) - 20);
            break;
        case 12:
            f.expand(&c.info, row, (x & 1) != 0 ? reinterpret_cast<const PngColor16*>(extra + 0x8000) : nullptr);
            break;
        default:
            f.dither(&c.info, row, (x & 1) != 0 ? extra : nullptr, (x & 2) != 0 ? extra + 0x8000 : nullptr);
            break;
    }
}

void CompareRows(test::Context& ctx, const RowFunctions& candidate, int cases) {
    const RowFunctions original{Original_png_do_invert,         Original_png_do_swap,
                                Original_png_do_packswap,       Original_png_do_strip_filler,
                                Original_png_do_bgr,            Original_png_do_unpack,
                                Original_png_do_unshift,        Original_png_do_read_swap_alpha,
                                Original_png_do_read_invert_alpha, Original_png_do_read_filler,
                                Original_png_do_gray_to_rgb,    Original_png_do_expand_palette,
                                Original_png_do_expand,         Original_png_do_dither};
    auto a = std::make_unique<RowCase>();
    auto b = std::make_unique<RowCase>();
    std::vector<std::uint8_t> extra(0x8000 + 256);  // palette / lookup tables, colours at 0x8000
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(kRowOps));
        ctx.rng().Fill(a.get(), sizeof(RowCase));
        RandomRowInfo(ctx.rng(), a->info);
        ctx.rng().Fill(extra.data(), extra.size());
        if (op == 6) {
            // Significant bits from 1 to the depth (0 would shift 16-bit samples by 16).
            for (int k = 0; k < 5; ++k) {
                extra[k] = static_cast<std::uint8_t>(ctx.rng().Range(1, a->info.bit_depth > 0 ? a->info.bit_depth : 1));
            }
        }
        const std::uint32_t x = ctx.rng().Next32(), y = ctx.rng().Next32();
        if (op == 12 && ctx.rng().Chance(1, 2)) {
            // A transparent colour that some pixels have.
            const std::uint8_t* pixel = a->row + kRowStart + ctx.rng().Below(16);
            auto* colour = reinterpret_cast<PngColor16*>(extra.data() + 0x8000);
            colour->gray = ctx.rng().Chance(1, 2) ? pixel[0] : static_cast<std::uint16_t>((pixel[0] << 8) | pixel[1]);
            colour->red = pixel[0];
            colour->green = pixel[1];
            colour->blue = pixel[2];
            std::memcpy(a->row + kRowStart + 3 * ctx.rng().Below(40), pixel, 3);
        }
        std::memcpy(b.get(), a.get(), sizeof(RowCase));
        RunRow(original, op, *a, extra.data(), x, y);
        RunRow(candidate, op, *b, extra.data(), x, y);
        if (!S3RE_EXPECT_MEM_EQ(a.get(), b.get(), sizeof(RowCase))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d, type %u, depth %u, channels %u, width %u", i, op,
                     b->info.color_type, b->info.bit_depth, b->info.channels, b->info.width);
            return;
        }
    }
}

// Wrong: png_do_read_filler writes the first pixel's filler byte too (the
// 1.0.2 bug fixed).
void __cdecl WrongReadFiller(PngRowInfo* info, std::uint8_t* row, std::uint32_t filler, std::uint32_t flags) {
    const bool fixable = info->color_type == kGray && info->bit_depth == 8 && (flags & kPngFlagFillerAfter) != 0;
    png_do_read_filler(info, row, filler, flags);
    if (fixable && info->width > 0) {
        row[1] = static_cast<std::uint8_t>(filler);
    }
}

}  // namespace

S3RE_TEST(PngRowTransforms_MatchOriginal) {
    CompareRows(ctx,
                {&png_do_invert, &png_do_swap, &png_do_packswap, &png_do_strip_filler, &png_do_bgr, &png_do_unpack,
                 &png_do_unshift, &png_do_read_swap_alpha, &png_do_read_invert_alpha, &png_do_read_filler,
                 &png_do_gray_to_rgb, &png_do_expand_palette, &png_do_expand, &png_do_dither},
                14000);
}

S3RE_TEST(PngRowTransforms_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareRows(probe,
                {&png_do_invert, &png_do_swap, &png_do_packswap, &png_do_strip_filler, &png_do_bgr, &png_do_unpack,
                 &png_do_unshift, &png_do_read_swap_alpha, &png_do_read_invert_alpha, &WrongReadFiller,
                 &png_do_gray_to_rgb, &png_do_expand_palette, &png_do_expand, &png_do_dither},
                14000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::thirdparty
