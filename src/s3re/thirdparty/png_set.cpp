// libpng 1.0.2's info setters (pngset.c), statically linked into S3.EXE for
// PaintLib's PNG decoder (re/picture_code.md); the chunk handlers store what
// they read through them. All cdecl, (png_ptr, info_ptr, ...); every one
// does nothing when png_ptr or info_ptr is null, else stores the values and
// sets the chunk's bit in info->valid (+0x08).
//
// png_set_bKGD (0x005b1550): background colour (10 bytes) to +0x5a.
// png_set_cHRM (0x005b1590): eight doubles stored as floats at +0x80.
// png_set_gAMA (0x005b1600): gamma as float at +0x28.
// png_set_hIST (0x005b1630): histogram pointer to +0x7c.
// png_set_IHDR (0x005b1660): (png, info, width, height, bit_depth,
//   color_type, interlace, compression, filter): the header fields, channels
//   and pixel depth; rowbytes 0 and a warning when the width is too large.
// png_set_oFFs (0x005b1720), png_set_pHYs (0x005b1890): two dwords and a
//   unit byte.
// png_set_PLTE (0x005b18c0): palette pointer and its 16-bit count.
// png_set_sBIT (0x005b18f0): significant bits (5 bytes) to +0x44.
// png_set_sRGB_gAMA_and_cHRM (0x005b1920): rendering intent, gamma 0.45 and
//   the sRGB chromaticities (sRGB, gAMA and cHRM valid).
// png_set_tIME (0x005b1ab0): the 8-byte png_time to +0x3c.
// png_set_tRNS (0x005b1ae0): (png, info, trans, num_trans, trans_values):
//   the table pointer if not null, the colour if not null (then a count of 0
//   becomes 1), the count.
// png_set_text (0x005b19a0): (png, info, text_ptr, num_text): appends the
//   png_text entries (16 bytes: compression, key, text, text_length) to
//   info->text, growing it by png_malloc (8 spare entries) and copying the
//   old ones; a null text becomes "", an empty one gets compression NONE
//   (-1) and length 0. The texts themselves are not copied. Tested with the
//   chunk handlers (png_rutil_chunks.cpp).

#include "game/texts.h"
#include "hooks.h"
#include "test/difftest.h"
#include "thirdparty/png_calls.h"
#include "thirdparty/png_struct.h"

#include <intrin.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace s3re::thirdparty {

namespace {

constexpr std::uint32_t kMaxRowBytes = 0x7fffffff;  // PNG_MAX_UINT

// The floats png_set_sRGB_gAMA_and_cHRM stores (its constants are float
// literals; the bit patterns are those of the binary).
float FloatBits(std::uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

bool Usable(const PngStruct* png, const PngInfo* info) {
    return png != nullptr && info != nullptr;
}

}  // namespace

using SetColor16Fn = void(__cdecl*)(PngStruct* png, PngInfo* info, const PngColor16* colour);
using SetChrmFn = void(__cdecl*)(PngStruct* png, PngInfo* info, double whiteX, double whiteY, double redX,
                                 double redY, double greenX, double greenY, double blueX, double blueY);
using SetGamaFn = void(__cdecl*)(PngStruct* png, PngInfo* info, double gamma);
using SetPointerFn = void(__cdecl*)(PngStruct* png, PngInfo* info, void* pointer);
using SetIhdrFn = void(__cdecl*)(PngStruct* png, PngInfo* info, std::uint32_t width, std::uint32_t height,
                                 std::int32_t bitDepth, std::int32_t colorType, std::int32_t interlace,
                                 std::int32_t compression, std::int32_t filter);
using SetPairFn = void(__cdecl*)(PngStruct* png, PngInfo* info, std::uint32_t x, std::uint32_t y,
                                 std::int32_t unit);
using SetPlteFn = void(__cdecl*)(PngStruct* png, PngInfo* info, void* palette, std::int32_t count);
using SetSbitFn = void(__cdecl*)(PngStruct* png, PngInfo* info, const PngColor8* bits);
using SetSrgbFn = void(__cdecl*)(PngStruct* png, PngInfo* info, std::int32_t intent);
using SetTimeFn = void(__cdecl*)(PngStruct* png, PngInfo* info, const std::uint8_t* time);
using SetTrnsFn = void(__cdecl*)(PngStruct* png, PngInfo* info, std::uint8_t* trans, std::int32_t count,
                                 const PngColor16* values);

void __cdecl png_set_bKGD(PngStruct* png, PngInfo* info, const PngColor16* colour);
void __cdecl png_set_cHRM(PngStruct* png, PngInfo* info, double whiteX, double whiteY, double redX, double redY,
                          double greenX, double greenY, double blueX, double blueY);
void __cdecl png_set_gAMA(PngStruct* png, PngInfo* info, double gamma);
void __cdecl png_set_hIST(PngStruct* png, PngInfo* info, void* hist);
void __cdecl png_set_IHDR(PngStruct* png, PngInfo* info, std::uint32_t width, std::uint32_t height,
                          std::int32_t bitDepth, std::int32_t colorType, std::int32_t interlace,
                          std::int32_t compression, std::int32_t filter);
void __cdecl png_set_oFFs(PngStruct* png, PngInfo* info, std::uint32_t x, std::uint32_t y, std::int32_t unit);
void __cdecl png_set_pHYs(PngStruct* png, PngInfo* info, std::uint32_t x, std::uint32_t y, std::int32_t unit);
void __cdecl png_set_PLTE(PngStruct* png, PngInfo* info, void* palette, std::int32_t count);
void __cdecl png_set_sBIT(PngStruct* png, PngInfo* info, const PngColor8* bits);
void __cdecl png_set_sRGB_gAMA_and_cHRM(PngStruct* png, PngInfo* info, std::int32_t intent);
void __cdecl png_set_tIME(PngStruct* png, PngInfo* info, const std::uint8_t* time);
void __cdecl png_set_tRNS(PngStruct* png, PngInfo* info, std::uint8_t* trans, std::int32_t count,
                          const PngColor16* values);

S3RE_HOOK(0x005b1550, png_set_bKGD, SetColor16Fn, Original_png_set_bKGD, png_set_bKGD);
S3RE_HOOK(0x005b1590, png_set_cHRM, SetChrmFn, Original_png_set_cHRM, png_set_cHRM);
S3RE_HOOK(0x005b1600, png_set_gAMA, SetGamaFn, Original_png_set_gAMA, png_set_gAMA);
S3RE_HOOK(0x005b1630, png_set_hIST, SetPointerFn, Original_png_set_hIST, png_set_hIST);
S3RE_HOOK(0x005b1660, png_set_IHDR, SetIhdrFn, Original_png_set_IHDR, png_set_IHDR);
S3RE_HOOK(0x005b1720, png_set_oFFs, SetPairFn, Original_png_set_oFFs, png_set_oFFs);
S3RE_HOOK(0x005b1890, png_set_pHYs, SetPairFn, Original_png_set_pHYs, png_set_pHYs);
S3RE_HOOK(0x005b18c0, png_set_PLTE, SetPlteFn, Original_png_set_PLTE, png_set_PLTE);
S3RE_HOOK(0x005b18f0, png_set_sBIT, SetSbitFn, Original_png_set_sBIT, png_set_sBIT);
S3RE_HOOK(0x005b1920, png_set_sRGB_gAMA_and_cHRM, SetSrgbFn, Original_png_set_sRGB_gAMA_and_cHRM,
          png_set_sRGB_gAMA_and_cHRM);
S3RE_HOOK(0x005b1ab0, png_set_tIME, SetTimeFn, Original_png_set_tIME, png_set_tIME);
S3RE_HOOK(0x005b1ae0, png_set_tRNS, SetTrnsFn, Original_png_set_tRNS, png_set_tRNS);

using SetTextFn = void(__cdecl*)(PngStruct* png, PngInfo* info, PngText* text, std::int32_t count);
void __cdecl png_set_text(PngStruct* png, PngInfo* info, PngText* text, std::int32_t count);
S3RE_HOOK(0x005b19a0, png_set_text, SetTextFn, Original_png_set_text, png_set_text);

// Purpose: Adds the texts of tEXt / zTXt chunks to the info structure.
void __cdecl png_set_text(PngStruct* png, PngInfo* info, PngText* text, std::int32_t count) {
    if (png == nullptr || info == nullptr || count == 0) {
        return;
    }
    if (info->num_text + count > info->max_text) {
        const auto malloc = calls::At<calls::PngMallocFn>(calls::kPngMalloc);
        if (info->text != nullptr) {
            const std::int32_t oldMax = info->max_text;
            void* const oldText = info->text;
            info->max_text = info->num_text + count + 8;
            info->text = malloc(png, static_cast<std::uint32_t>(info->max_text) * sizeof(PngText));
            // rep movsd / rep movsb, as MSVC 6 inlines memcpy
            const std::uint32_t bytes = static_cast<std::uint32_t>(oldMax) * sizeof(PngText);
            auto* dst = static_cast<unsigned char*>(info->text);
            const auto* src = static_cast<const unsigned char*>(oldText);
            __movsd(reinterpret_cast<unsigned long*>(dst), reinterpret_cast<const unsigned long*>(src), bytes / 4);
            __movsb(dst + (bytes & ~3u), src + (bytes & ~3u), bytes % 4);
            calls::At<calls::PngFreeFn>(calls::kPngFree)(png, oldText);
        } else {
            info->max_text = count + 8;
            info->num_text = 0;
            info->text = malloc(png, static_cast<std::uint32_t>(info->max_text) * sizeof(PngText));
        }
    }
    for (std::int32_t i = 0; i < count; ++i) {
        PngText& entry = static_cast<PngText*>(info->text)[info->num_text];
        if (text[i].text == nullptr) {
            text[i].text = const_cast<char*>(game::texts::kEmpty.c_str());
        }
        if (text[i].text[0] == 0) {
            entry.text_length = 0;
            entry.compression = kPngTextCompressionNone;
        } else {
            entry.text_length = static_cast<std::uint32_t>(std::strlen(text[i].text));
            entry.compression = text[i].compression;
        }
        entry.text = text[i].text;
        entry.key = text[i].key;
        ++info->num_text;
    }
}

// Purpose: Stores the background colour of a bKGD chunk.
void __cdecl png_set_bKGD(PngStruct* png, PngInfo* info, const PngColor16* colour) {
    if (!Usable(png, info)) {
        return;
    }
    std::memcpy(&info->background, colour, sizeof(PngColor16));
    info->valid |= kPngInfoBkgd;
}

// Purpose: Stores the chromaticities of a cHRM chunk.
void __cdecl png_set_cHRM(PngStruct* png, PngInfo* info, double whiteX, double whiteY, double redX, double redY,
                          double greenX, double greenY, double blueX, double blueY) {
    if (!Usable(png, info)) {
        return;
    }
    const double values[8] = {whiteX, whiteY, redX, redY, greenX, greenY, blueX, blueY};
    for (int k = 0; k < 8; ++k) {
        info->chrm[k] = static_cast<float>(values[k]);
    }
    info->valid |= kPngInfoChrm;
}

// Purpose: Stores the gamma of a gAMA chunk.
void __cdecl png_set_gAMA(PngStruct* png, PngInfo* info, double gamma) {
    if (!Usable(png, info)) {
        return;
    }
    info->gamma = static_cast<float>(gamma);
    info->valid |= kPngInfoGama;
}

// Purpose: Stores the histogram of a hIST chunk.
void __cdecl png_set_hIST(PngStruct* png, PngInfo* info, void* hist) {
    if (!Usable(png, info)) {
        return;
    }
    info->hist = hist;
    info->valid |= kPngInfoHist;
}

// Purpose: Stores the image header (size, depth, colour type) of IHDR.
void __cdecl png_set_IHDR(PngStruct* png, PngInfo* info, std::uint32_t width, std::uint32_t height,
                          std::int32_t bitDepth, std::int32_t colorType, std::int32_t interlace,
                          std::int32_t compression, std::int32_t filter) {
    if (!Usable(png, info)) {
        return;
    }
    info->width = width;
    info->height = height;
    info->bit_depth = static_cast<std::uint8_t>(bitDepth);
    info->color_type = static_cast<std::uint8_t>(colorType);
    info->compression_type = static_cast<std::uint8_t>(compression);
    info->filter_type = static_cast<std::uint8_t>(filter);
    info->interlace_type = static_cast<std::uint8_t>(interlace);
    const std::uint8_t type = info->color_type;
    if (type == kPngColorTypePalette || (type & kPngColorMaskColor) == 0) {
        info->channels = 1;
    } else {
        info->channels = 3;
    }
    if ((type & kPngColorMaskAlpha) != 0) {
        ++info->channels;
    }
    info->pixel_depth = static_cast<std::uint8_t>(info->channels * info->bit_depth);
    const std::int32_t bytesPerPixel = (info->pixel_depth + 7) >> 3;
    if (width > kMaxRowBytes / static_cast<std::uint32_t>(bytesPerPixel)) {
        calls::At<calls::PngMessageFn>(calls::kPngWarning)(png, game::texts::kPngRowbytesOverflow);
        info->rowbytes = 0;
    } else {
        info->rowbytes = (width * info->pixel_depth + 7) >> 3;
    }
}

// Purpose: Stores the image offset of an oFFs chunk.
void __cdecl png_set_oFFs(PngStruct* png, PngInfo* info, std::uint32_t x, std::uint32_t y, std::int32_t unit) {
    if (!Usable(png, info)) {
        return;
    }
    info->x_offset = x;
    info->y_offset = y;
    info->offset_unit_type = static_cast<std::uint8_t>(unit);
    info->valid |= kPngInfoOffs;
}

// Purpose: Stores the pixel size of a pHYs chunk.
void __cdecl png_set_pHYs(PngStruct* png, PngInfo* info, std::uint32_t x, std::uint32_t y, std::int32_t unit) {
    if (!Usable(png, info)) {
        return;
    }
    info->x_pixels_per_unit = x;
    info->y_pixels_per_unit = y;
    info->phys_unit_type = static_cast<std::uint8_t>(unit);
    info->valid |= kPngInfoPhys;
}

// Purpose: Stores the palette of a PLTE chunk.
void __cdecl png_set_PLTE(PngStruct* png, PngInfo* info, void* palette, std::int32_t count) {
    if (!Usable(png, info)) {
        return;
    }
    info->palette = palette;
    info->num_palette = static_cast<std::uint16_t>(count);
    info->valid |= kPngInfoPlte;
}

// Purpose: Stores the significant bits of an sBIT chunk.
void __cdecl png_set_sBIT(PngStruct* png, PngInfo* info, const PngColor8* bits) {
    if (!Usable(png, info)) {
        return;
    }
    std::memcpy(&info->sig_bit, bits, sizeof(PngColor8));
    info->valid |= kPngInfoSbit;
}

// Purpose: An sRGB chunk: its intent plus the standard gamma and
// chromaticities.
void __cdecl png_set_sRGB_gAMA_and_cHRM(PngStruct* png, PngInfo* info, std::int32_t intent) {
    if (!Usable(png, info)) {
        return;
    }
    // .45, .3127, .3290, .64, .33, .30, .60, .15, .06 as floats.
    static constexpr std::uint32_t kChrm[8] = {0x3ea01a37, 0x3ea872b0, 0x3f23d70a, 0x3ea8f5c3,
                                               0x3e99999a, 0x3f19999a, 0x3e19999a, 0x3d75c28f};
    info->gamma = FloatBits(0x3ee66666);
    info->srgb_intent = static_cast<std::uint8_t>(intent);
    for (int k = 0; k < 8; ++k) {
        info->chrm[k] = FloatBits(kChrm[k]);
    }
    info->valid |= kPngInfoSrgb | kPngInfoGama | kPngInfoChrm;
}

// Purpose: Stores the modification time of a tIME chunk.
void __cdecl png_set_tIME(PngStruct* png, PngInfo* info, const std::uint8_t* time) {
    if (!Usable(png, info)) {
        return;
    }
    std::memcpy(info->mod_time, time, sizeof(info->mod_time));
    info->valid |= kPngInfoTime;
}

// Purpose: Stores the transparency of a tRNS chunk.
void __cdecl png_set_tRNS(PngStruct* png, PngInfo* info, std::uint8_t* trans, std::int32_t count,
                          const PngColor16* values) {
    if (!Usable(png, info)) {
        return;
    }
    if (trans != nullptr) {
        info->trans = trans;
    }
    if (values != nullptr) {
        std::memcpy(&info->trans_values, values, sizeof(PngColor16));
        if (count == 0) {
            count = 1;
        }
    }
    info->num_trans = static_cast<std::uint16_t>(count);
    info->valid |= kPngInfoTrns;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

int g_warnings = 0;

void __cdecl CountWarning(PngStruct* /*png*/, const char* /*message*/) {
    ++g_warnings;
}

struct SetFunctions {
    SetColor16Fn bkgd;
    SetChrmFn chrm;
    SetGamaFn gama;
    SetPointerFn hist;
    SetIhdrFn ihdr;
    SetPairFn offs, phys;
    SetPlteFn plte;
    SetSbitFn sbit;
    SetSrgbFn srgb;
    SetTimeFn time;
    SetTrnsFn trns;
};

struct SetCase {
    PngStruct png;
    PngInfo info;
    std::uint8_t source[16];
};

double RandomDouble(test::Rng& rng) {
    if (rng.Chance(1, 4)) {
        std::uint64_t bits = (static_cast<std::uint64_t>(rng.Next32()) << 32) | rng.Next32();
        double value;
        std::memcpy(&value, &bits, 8);
        return value;  // anything, NaNs and denormals included
    }
    return static_cast<double>(static_cast<std::int32_t>(rng.Next32())) / static_cast<double>(rng.Range(1, 100000));
}

int RunSet(const SetFunctions& f, int op, SetCase& c, bool nullPng, bool nullInfo, const std::uint32_t* v,
           const double* d) {
    g_warnings = 0;
    PngStruct* png = nullPng ? nullptr : &c.png;
    PngInfo* info = nullInfo ? nullptr : &c.info;
    auto* colour = reinterpret_cast<const PngColor16*>(c.source);
    switch (op) {
        case 0: f.bkgd(png, info, colour); break;
        case 1: f.chrm(png, info, d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]); break;
        case 2: f.gama(png, info, d[0]); break;
        case 3: f.hist(png, info, reinterpret_cast<void*>(static_cast<std::uintptr_t>(v[0]))); break;
        case 4:
            f.ihdr(png, info, v[0], v[1], static_cast<std::int32_t>(v[2]), static_cast<std::int32_t>(v[3]),
                   static_cast<std::int32_t>(v[4]), static_cast<std::int32_t>(v[5]), static_cast<std::int32_t>(v[6]));
            break;
        case 5: f.offs(png, info, v[0], v[1], static_cast<std::int32_t>(v[2])); break;
        case 6: f.phys(png, info, v[0], v[1], static_cast<std::int32_t>(v[2])); break;
        case 7: f.plte(png, info, reinterpret_cast<void*>(static_cast<std::uintptr_t>(v[0])), static_cast<std::int32_t>(v[1])); break;
        case 8: f.sbit(png, info, reinterpret_cast<const PngColor8*>(c.source)); break;
        case 9: f.srgb(png, info, static_cast<std::int32_t>(v[0])); break;
        case 10: f.time(png, info, c.source); break;
        default:
            f.trns(png, info, (v[3] & 1) != 0 ? reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(v[0])) : nullptr,
                   (v[3] & 4) != 0 ? 0 : static_cast<std::int32_t>(v[1]), (v[3] & 2) != 0 ? colour : nullptr);
            break;
    }
    return g_warnings;
}

void CompareSetters(test::Context& ctx, const SetFunctions& candidate, int cases) {
    const SetFunctions original{Original_png_set_bKGD, Original_png_set_cHRM, Original_png_set_gAMA,
                                Original_png_set_hIST, Original_png_set_IHDR, Original_png_set_oFFs,
                                Original_png_set_pHYs, Original_png_set_PLTE, Original_png_set_sBIT,
                                Original_png_set_sRGB_gAMA_and_cHRM, Original_png_set_tIME, Original_png_set_tRNS};
    auto a = std::make_unique<SetCase>();
    auto b = std::make_unique<SetCase>();
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(12));
        ctx.rng().Fill(a.get(), sizeof(SetCase));
        a->png.warning_fn = reinterpret_cast<void*>(&CountWarning);
        const bool nullPng = ctx.rng().Chance(1, 16);
        const bool nullInfo = ctx.rng().Chance(1, 16);
        std::uint32_t v[7];
        for (auto& value : v) {
            value = ctx.rng().Next32();
        }
        if (op == 4) {
            static const std::uint32_t kWidths[] = {1, 100, 0x7fffffff / 3, 0x7fffffff / 3 + 1, 0x7fffffff / 8,
                                                    0x7fffffff / 8 + 1, 0x20000000};
            if (ctx.rng().Chance(1, 2)) {
                v[0] = kWidths[ctx.rng().Below(7)];
            }
            v[2] = ctx.rng().Chance(1, 2) ? ctx.rng().Range(1, 16) : v[2];
            v[3] = ctx.rng().Chance(1, 2) ? ctx.rng().Below(8) : v[3];
            // The pixel depth must not be 0 mod 256 (the original divides by it).
            const std::uint32_t type = v[3] & 0xff;
            const std::uint32_t channels = (type == 3 || (type & 2) == 0 ? 1 : 3) + ((type & 4) != 0 ? 1 : 0);
            if (((channels * (v[2] & 0xff)) & 0xff) == 0) {
                v[2] = 1;
            }
        }
        double d[8];
        for (auto& value : d) {
            value = RandomDouble(ctx.rng());
        }
        std::memcpy(b.get(), a.get(), sizeof(SetCase));
        const int wa = RunSet(original, op, *a, nullPng, nullInfo, v, d);
        const int wb = RunSet(candidate, op, *b, nullPng, nullInfo, v, d);
        bool same = S3RE_EXPECT_EQ(wa, wb);
        same &= S3RE_EXPECT_MEM_EQ(a.get(), b.get(), sizeof(SetCase));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
            return;
        }
    }
}

// Wrong: tRNS without the count-of-0 rule.
void __cdecl WrongSetTrns(PngStruct* png, PngInfo* info, std::uint8_t* trans, std::int32_t count,
                          const PngColor16* values) {
    png_set_tRNS(png, info, trans, count, values);
    if (Usable(png, info) && count == 0) {
        info->num_trans = 0;
    }
}

}  // namespace

S3RE_TEST(PngSetters_MatchOriginal) {
    CompareSetters(ctx,
                   {&png_set_bKGD, &png_set_cHRM, &png_set_gAMA, &png_set_hIST, &png_set_IHDR, &png_set_oFFs,
                    &png_set_pHYs, &png_set_PLTE, &png_set_sBIT, &png_set_sRGB_gAMA_and_cHRM, &png_set_tIME,
                    &png_set_tRNS},
                   12000);
}

S3RE_TEST(PngSetters_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSetters(probe,
                   {&png_set_bKGD, &png_set_cHRM, &png_set_gAMA, &png_set_hIST, &png_set_IHDR, &png_set_oFFs,
                    &png_set_pHYs, &png_set_PLTE, &png_set_sBIT, &png_set_sRGB_gAMA_and_cHRM, &png_set_tIME,
                    &WrongSetTrns},
                   12000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::thirdparty
