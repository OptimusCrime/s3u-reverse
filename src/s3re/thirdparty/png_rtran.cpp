// libpng 1.0.2's png_do_read_transformations (pngrtran.c), statically linked
// into S3.EXE (re/picture_code.md). cdecl (png_ptr).
//
// png_do_read_transformations (0x005a6c30): runs the transformations set in
//   png->transformations (+0x68) on the row just unfiltered (row_buf + 1,
//   described by row_info +0xf8), in libpng's order: expand (palette or
//   grey / RGB with tRNS), strip alpha, gray to RGB (before the background
//   unless flag 0x20000), background (with tRNS or alpha), gamma (not with
//   a background over transparency, not for palette images), strip 16
//   (png_do_chop, inlined: the high byte of each sample), dither (a row of
//   0 bytes afterwards is an error), invert mono, unshift, unpack, BGR,
//   packswap, gray to RGB (after the background), filler, invert alpha,
//   swap alpha, swap bytes, the user transform callback (+0x54). A null
//   row_buf is an error ("NULL row buffer for row %ld, pass %d").
// png_do_gamma (0x005a8900): (row_info, row, gamma_table, gamma_16_table,
//   gamma_shift): with the table for the depth set, every sample but alpha
//   through the table: 2-bit grey (each 2-bit sample spread to 8 bits as the
//   index), 4-bit grey, 8 bits directly, 16 bits as
//   gamma_16_table[low byte >> shift][high byte]. Kept: 2-bit grey rows go on
//   to the 4/8/16-bit checks (none match); RGB, RGBA and grey-alpha rows of
//   any depth but 8 take the 16-bit path (with only the 8-bit table set:
//   a null 16-bit table is read, as in the original).

#include "game/texts.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "thirdparty/png_calls.h"
#include "thirdparty/png_struct.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace s3re::thirdparty {

using PngRowFn = void(__cdecl*)(PngStruct* png);

using GammaRowFn = void(__cdecl*)(PngRowInfo* info, std::uint8_t* row, std::uint8_t* gammaTable,
                                 std::uint16_t** gamma16Table, std::int32_t gammaShift);

void __cdecl png_do_read_transformations(PngStruct* png);
void __cdecl png_do_gamma(PngRowInfo* info, std::uint8_t* row, std::uint8_t* gammaTable, std::uint16_t** gamma16Table,
                          std::int32_t gammaShift);

S3RE_HOOK(0x005a8900, png_do_gamma, GammaRowFn, Original_png_do_gamma, png_do_gamma);
S3RE_HOOK(0x005a6c30, png_do_read_transformations, PngRowFn, Original_png_do_read_transformations,
          png_do_read_transformations);

namespace {

using RowFn = void(__cdecl*)(PngRowInfo* info, std::uint8_t* row);
using RowFlagsFn = void(__cdecl*)(PngRowInfo* info, std::uint8_t* row, std::uint32_t flags);
using UnshiftFn = void(__cdecl*)(PngRowInfo* info, std::uint8_t* row, const PngColor8* sigBits);
using ReadFillerFn = void(__cdecl*)(PngRowInfo* info, std::uint8_t* row, std::uint32_t filler, std::uint32_t flags);
using ExpandFn = void(__cdecl*)(PngRowInfo* info, std::uint8_t* row, const PngColor16* trans);
using DitherFn = void(__cdecl*)(PngRowInfo* info, std::uint8_t* row, const std::uint8_t* paletteLookup,
                                const std::uint8_t* ditherIndex);
using ExpandPaletteFn = void(__cdecl*)(PngRowInfo* info, std::uint8_t* row, const void* palette,
                                       const std::uint8_t* trans, std::uint32_t numTrans);
using GammaFn = void(__cdecl*)(PngRowInfo* info, std::uint8_t* row, std::uint8_t* gammaTable,
                               std::uint16_t** gamma16Table, std::int32_t gammaShift);
using BackgroundFn = void(__cdecl*)(PngRowInfo* info, std::uint8_t* row, const PngColor16* transValues,
                                    const PngColor16* background, const PngColor16* backgroundGamma,
                                    std::uint8_t* gammaTable, std::uint8_t* gammaFrom1, std::uint8_t* gammaTo1,
                                    std::uint16_t** gamma16, std::uint16_t** gamma16From1,
                                    std::uint16_t** gamma16To1, std::int32_t gammaShift);
using UserTransformFn = void(__cdecl*)(PngStruct* png, PngRowInfo* info, std::uint8_t* row);
using SprintfFn = int(__cdecl*)(char* buffer, const char* format, ...);

constexpr std::uintptr_t kSprintf = 0x0061f27e;
constexpr std::uintptr_t kDoExpandPalette = 0x005a8cf0;
constexpr std::uintptr_t kDoExpand = 0x005a8f20;
constexpr std::uintptr_t kDoStripFiller = 0x005a5f10;
constexpr std::uintptr_t kDoGrayToRgb = 0x005a77b0;
constexpr std::uintptr_t kDoBackground = 0x005a7900;
constexpr std::uintptr_t kDoGamma = 0x005a8900;
constexpr std::uintptr_t kDoDither = 0x005a92b0;
constexpr std::uintptr_t kDoInvert = 0x005a5e60;
constexpr std::uintptr_t kDoUnshift = 0x005a7160;
constexpr std::uintptr_t kDoUnpack = 0x005a7030;
constexpr std::uintptr_t kDoBgr = 0x005a6120;
constexpr std::uintptr_t kDoPackswap = 0x005a5ec0;
constexpr std::uintptr_t kDoReadFiller = 0x005a7550;
constexpr std::uintptr_t kDoInvertAlpha = 0x005a7440;
constexpr std::uintptr_t kDoSwapAlpha = 0x005a7340;
constexpr std::uintptr_t kDoSwap = 0x005a5e90;

// png_struct.transformations bits not in png_struct.h.
constexpr std::uint32_t kPngSwapBytes = 0x0010;
constexpr std::uint32_t kPngInvertMono = 0x0020;
constexpr std::uint32_t kPngDither = 0x0040;
constexpr std::uint32_t kPngBackground = 0x0080;
constexpr std::uint32_t kPngShift = 0x0008;
constexpr std::uint32_t kPngGamma = 0x2000;
constexpr std::uint32_t kPngSwapAlpha = 0x20000;
constexpr std::uint32_t kPngStripAlpha = 0x40000;      // (flag bits, not offsets: raw-ok)
constexpr std::uint32_t kPngInvertAlpha = 0x80000;     // raw-ok
constexpr std::uint32_t kPngUserTransform = 0x100000;  // raw-ok
constexpr std::uint32_t kPngFlagBackgroundIsGray = 0x20000;  // gray to RGB after the background
constexpr std::uint32_t kStripFillerAfter = 0x80;            // png_do_strip_filler's flags argument
constexpr std::uint8_t kSixteenBits = 16;

template <typename Fn, typename... Args>
void Call(std::uintptr_t address, Args... args) {
    calls::At<Fn>(address)(args...);
}

// png_do_chop: 16-bit samples to their high byte.
void Chop(PngRowInfo& info, std::uint8_t* row) {
    if (info.bit_depth != kSixteenBits) {
        return;
    }
    const std::uint8_t* sp = row;
    std::uint8_t* dp = row;
    for (std::uint32_t i = info.channels * info.width; i != 0; --i) {
        *dp++ = *sp;
        sp += 2;
    }
    info.bit_depth = 8;
    info.pixel_depth = static_cast<std::uint8_t>(info.channels << 3);
    info.rowbytes = info.channels * info.width;
}

}  // namespace

// Purpose: Gamma-corrects a row through the gamma tables (8-bit table for
// up to 8 bits per sample, 16-bit tables indexed by the low byte shifted).
void __cdecl png_do_gamma(PngRowInfo* info, std::uint8_t* row, std::uint8_t* gammaTable,
                          std::uint16_t** gamma16Table, std::int32_t gammaShift) {
    const std::uint8_t depth = info->bit_depth;
    std::uint32_t width = info->width;
    if (!((depth <= 8 && gammaTable != nullptr) || (depth == 16 && gamma16Table != nullptr))) {
        return;
    }
    const std::uint32_t shift = static_cast<std::uint32_t>(gammaShift) & 31;  // (x86 shift count)
    // A 16-bit sample through the 16-bit tables: the high byte first in the
    // row; the low byte is written first.
    const auto gamma16 = [&](std::uint8_t* sample) {
        const std::uint16_t v = gamma16Table[sample[1] >> shift][sample[0]];
        sample[1] = static_cast<std::uint8_t>(v);
        sample[0] = static_cast<std::uint8_t>(v >> 8);
    };
    const auto g = [gammaTable](std::uint32_t index) { return static_cast<std::uint32_t>(gammaTable[index]); };
    std::uint8_t* sp = row;
    switch (info->color_type) {
        case kPngColorTypeGray:
            // (The byte loops count (width + 3) / 4 and (width + 1) / 2 bytes,
            // as do-while loops: the counts wrap for widths near 2^32.)
            if (depth == 2 && width != 0) {
                std::uint32_t bytes = (width + 3) >> 2;  // four pixels a byte
                do {
                    const std::uint32_t a = *sp & 0xc0, b = *sp & 0x30, c = *sp & 0x0c, d = *sp & 0x03;
                    *sp = static_cast<std::uint8_t>((g(a | (a >> 2) | (a >> 4) | (a >> 6)) & 0xc0) |
                                                    ((g((b << 2) | b | (b >> 2) | (b >> 4)) >> 2) & 0x30) |
                                                    ((g((c << 4) | (c << 2) | c | (c >> 2)) >> 4) & 0x0c) |
                                                    (g((d << 6) | (d << 4) | (d << 2) | d) >> 6));
                    ++sp;
                } while (--bytes != 0);
            }
            if (info->bit_depth == 4) {
                if (width != 0) {
                    std::uint32_t bytes = (width + 1) >> 1;
                    do {
                        const std::uint32_t high = *sp & 0xf0, low = *sp & 0x0f;
                        *sp = static_cast<std::uint8_t>((g(high | (high >> 4)) & 0xf0) | (g((low << 4) | low) >> 4));
                        ++sp;
                    } while (--bytes != 0);
                }
            } else if (info->bit_depth == 8) {
                for (; width != 0; --width, ++sp) {
                    *sp = gammaTable[*sp];
                }
            } else if (info->bit_depth == 16) {
                for (; width != 0; --width, sp += 2) {
                    gamma16(sp);
                }
            }
            break;
        case kPngColorTypeRgb:
            for (; width != 0; --width) {
                if (depth == 8) {
                    sp[0] = gammaTable[sp[0]];
                    sp[1] = gammaTable[sp[1]];
                    sp[2] = gammaTable[sp[2]];
                    sp += 3;
                } else {  // (any other depth: the 16-bit tables)
                    gamma16(sp);
                    gamma16(sp + 2);
                    gamma16(sp + 4);
                    sp += 6;
                }
            }
            break;
        case kPngColorTypeRgbAlpha:  // the alpha sample untouched
            for (; width != 0; --width) {
                if (depth == 8) {
                    sp[0] = gammaTable[sp[0]];
                    sp[1] = gammaTable[sp[1]];
                    sp[2] = gammaTable[sp[2]];
                    sp += 4;
                } else {
                    gamma16(sp);
                    gamma16(sp + 2);
                    gamma16(sp + 4);
                    sp += 8;
                }
            }
            break;
        case kPngColorTypeGrayAlpha:
            for (; width != 0; --width) {
                if (depth == 8) {
                    *sp = gammaTable[*sp];
                    sp += 2;
                } else {
                    gamma16(sp);
                    sp += 4;
                }
            }
            break;
        default: break;
    }
}

// Purpose: Applies the requested transformations (expansion, gamma,
// background, packing, ...) to the row just read.
void __cdecl png_do_read_transformations(PngStruct* png) {
    if (png->row_buf == nullptr) {
        char message[52];
        calls::At<SprintfFn>(kSprintf)(message, game::texts::kPngNullRowBuffer, png->row_number,
                                       static_cast<std::int32_t>(png->pass));
        calls::At<calls::PngMessageFn>(calls::kPngError)(png, message);
    }
    PngRowInfo* const info = &png->row_info;
    const auto row = [png] { return png->row_buf + 1; };
    const std::uint32_t t = png->transformations;
    // (The bits are re-read before each step in the original; nothing here
    // changes them.)
    if ((t & kPngExpand) != 0) {
        if (info->color_type == kPngColorTypePalette) {
            Call<ExpandPaletteFn>(kDoExpandPalette, info, row(), static_cast<const void*>(png->palette),
                                  static_cast<const std::uint8_t*>(png->trans),
                                  static_cast<std::uint32_t>(png->num_trans));
        } else {
            Call<ExpandFn>(kDoExpand, info, row(),
                           static_cast<const PngColor16*>(png->num_trans != 0 ? &png->trans_values : nullptr));
        }
    }
    if ((t & kPngStripAlpha) != 0) {
        Call<RowFlagsFn>(kDoStripFiller, info, row(), kStripFillerAfter);
    }
    const bool grayFirst = (png->flags & kPngFlagBackgroundIsGray) == 0;
    if ((t & kPngGrayToRgb) != 0 && grayFirst) {
        Call<RowFn>(kDoGrayToRgb, info, row());
    }
    const bool transparency = png->num_trans != 0 || (png->color_type & kPngColorMaskAlpha) != 0;
    if ((t & kPngBackground) != 0 && transparency) {
        Call<BackgroundFn>(kDoBackground, info, row(), static_cast<const PngColor16*>(&png->trans_values),
                           static_cast<const PngColor16*>(&png->background),
                           static_cast<const PngColor16*>(&png->background_1), png->gamma_table, png->gamma_from_1,
                           png->gamma_to_1, png->gamma_16_table, png->gamma_16_from_1, png->gamma_16_to_1,
                           png->gamma_shift);
    }
    if ((t & kPngGamma) != 0 && ((t & kPngBackground) == 0 || !transparency) &&
        png->color_type != kPngColorTypePalette) {
        Call<GammaFn>(kDoGamma, info, row(), png->gamma_table, png->gamma_16_table, png->gamma_shift);
    }
    if ((t & kPngStrip16) != 0) {
        Chop(*info, row());
    }
    if ((t & kPngDither) != 0) {
        Call<DitherFn>(kDoDither, info, row(), static_cast<const std::uint8_t*>(png->palette_lookup),
                       static_cast<const std::uint8_t*>(png->dither_index));
        if (info->rowbytes == 0) {
            calls::At<calls::PngMessageFn>(calls::kPngError)(png, game::texts::kPngDitherRowbytesZero);
        }
    }
    if ((t & kPngInvertMono) != 0) {
        Call<RowFn>(kDoInvert, info, row());
    }
    if ((t & kPngShift) != 0) {
        Call<UnshiftFn>(kDoUnshift, info, row(), static_cast<const PngColor8*>(&png->shift));
    }
    if ((t & kPngPack) != 0) {
        Call<RowFn>(kDoUnpack, info, row());
    }
    if ((t & kPngBgr) != 0) {
        Call<RowFn>(kDoBgr, info, row());
    }
    if ((t & kPngPackswap) != 0) {
        Call<RowFn>(kDoPackswap, info, row());
    }
    if ((t & kPngGrayToRgb) != 0 && !grayFirst) {
        Call<RowFn>(kDoGrayToRgb, info, row());
    }
    if ((t & kPngFiller) != 0) {
        Call<ReadFillerFn>(kDoReadFiller, info, row(), static_cast<std::uint32_t>(png->filler), png->flags);
    }
    if ((t & kPngInvertAlpha) != 0) {
        Call<RowFn>(kDoInvertAlpha, info, row());
    }
    if ((t & kPngSwapAlpha) != 0) {
        Call<RowFn>(kDoSwapAlpha, info, row());
    }
    if ((t & kPngSwapBytes) != 0) {
        Call<RowFn>(kDoSwap, info, row());
    }
    if ((t & kPngUserTransform) != 0 && png->read_user_transform_fn != nullptr) {
        reinterpret_cast<UserTransformFn>(png->read_user_transform_fn)(png, info, row());
    }
}

// ---------------------------------------------------------------------------
// Tests: a random png_struct (transformation bits, flags, colour type, tRNS
// count, row_info, a row buffer or none); every callee jumps to a logger
// that records which it was and its arguments (pointers into the struct
// relative to it); the dither logger sets rowbytes to 0 now and then;
// png_error's callback throws; png_do_chop runs. Compared: the struct, the
// row and the log.
// ---------------------------------------------------------------------------

namespace {

struct TransformThrown {};

struct TransformWorld {
    PngStruct png;
    std::uint8_t row[512];
};

TransformWorld* g_world = nullptr;
std::vector<std::uint32_t>* g_log = nullptr;
std::uint32_t g_ditherZero = 0;

std::uint32_t Relative(std::uint32_t value) {
    const auto base = reinterpret_cast<std::uint32_t>(g_world);
    return value - base < sizeof(TransformWorld) ? 0x40000000u + (value - base) : value;
}

template <int Id, int N>
void __cdecl Logger(std::uint32_t a0, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3, std::uint32_t a4,
                    std::uint32_t a5, std::uint32_t a6, std::uint32_t a7, std::uint32_t a8, std::uint32_t a9,
                    std::uint32_t a10, std::uint32_t a11) {
    const std::uint32_t args[12] = {a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11};
    g_log->push_back(0xca110000u + Id);
    for (int k = 0; k < N; ++k) {
        g_log->push_back(Relative(args[k]));
    }
    if (Id == 6 && (g_ditherZero++ % 5) == 0) {  // png_do_dither: a row of no bytes now and then
        reinterpret_cast<PngRowInfo*>(a0)->rowbytes = 0;
    }
}

void __cdecl TransformError(PngStruct* /*png*/, const char* message) {
    g_log->push_back(0xe0000000u + static_cast<std::uint8_t>(message[0]));
    throw TransformThrown{};
}

struct Callee {
    std::uintptr_t address;
    const void* logger;
};

const Callee kCallees[] = {
    {kDoExpandPalette, reinterpret_cast<const void*>(&Logger<0, 5>)},
    {kDoExpand, reinterpret_cast<const void*>(&Logger<1, 3>)},
    {kDoStripFiller, reinterpret_cast<const void*>(&Logger<2, 3>)},
    {kDoGrayToRgb, reinterpret_cast<const void*>(&Logger<3, 2>)},
    {kDoBackground, reinterpret_cast<const void*>(&Logger<4, 12>)},
    {kDoGamma, reinterpret_cast<const void*>(&Logger<5, 5>)},
    {kDoDither, reinterpret_cast<const void*>(&Logger<6, 4>)},
    {kDoInvert, reinterpret_cast<const void*>(&Logger<7, 2>)},
    {kDoUnshift, reinterpret_cast<const void*>(&Logger<8, 3>)},
    {kDoUnpack, reinterpret_cast<const void*>(&Logger<9, 2>)},
    {kDoBgr, reinterpret_cast<const void*>(&Logger<10, 2>)},
    {kDoPackswap, reinterpret_cast<const void*>(&Logger<11, 2>)},
    {kDoReadFiller, reinterpret_cast<const void*>(&Logger<12, 4>)},
    {kDoInvertAlpha, reinterpret_cast<const void*>(&Logger<13, 2>)},
    {kDoSwapAlpha, reinterpret_cast<const void*>(&Logger<14, 2>)},
    {kDoSwap, reinterpret_cast<const void*>(&Logger<15, 2>)},
};

void __cdecl UserTransform(PngStruct* png, PngRowInfo* info, std::uint8_t* row) {
    Logger<16, 3>(reinterpret_cast<std::uint32_t>(png), reinterpret_cast<std::uint32_t>(info),
                  reinterpret_cast<std::uint32_t>(row), 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

bool CompareTransformations(test::Context& ctx, PngRowFn candidate, int cases) {
    auto world = std::make_unique<TransformWorld>();
    auto initial = std::make_unique<TransformWorld>();
    auto first = std::make_unique<TransformWorld>();
    std::vector<std::unique_ptr<test::ScopedJump>> jumps;
    for (const Callee& c : kCallees) {
        jumps.push_back(std::make_unique<test::ScopedJump>(c.address, c.logger));
    }
    for (int i = 0; i < cases; ++i) {
        test::Rng& rng = ctx.rng();
        rng.Fill(world.get(), sizeof(TransformWorld));
        PngStruct& png = world->png;
        png.transformations = rng.Next32() & 0x1fffff;
        if (rng.Chance(1, 2)) {
            png.transformations &= rng.Next32();
        }
        png.error_fn = reinterpret_cast<void*>(&TransformError);
        png.row_buf = rng.Chance(1, 20) ? nullptr : world->row;
        static const std::uint8_t kTypes[] = {0, 2, 3, 4, 6};
        png.row_info.color_type = kTypes[rng.Below(5)];
        png.color_type = rng.Chance(1, 2) ? png.row_info.color_type : kTypes[rng.Below(5)];
        png.row_info.bit_depth = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 16 : 8);
        png.row_info.channels = static_cast<std::uint8_t>(1 + rng.Below(4));
        png.row_info.width = rng.Below(60);
        png.num_trans = static_cast<std::uint16_t>(rng.Chance(1, 2) ? 0 : rng.Below(300));
        png.read_user_transform_fn = rng.Chance(1, 2) ? reinterpret_cast<void*>(&UserTransform) : nullptr;
        *initial = *world;
        std::vector<std::uint32_t> logs[2];
        for (int run = 0; run < 2; ++run) {
            if (run == 1) {
                *first = *world;
                *world = *initial;
            }
            g_world = world.get();
            g_log = &logs[run];
            g_ditherZero = static_cast<std::uint32_t>(i);
            try {
                (run == 0 ? Original_png_do_read_transformations : candidate)(&world->png);
            } catch (const TransformThrown&) {
                logs[run].push_back(0xdead);
            }
        }
        if (!S3RE_EXPECT(logs[0] == logs[1]) || !S3RE_EXPECT_MEM_EQ(first.get(), world.get(), sizeof(TransformWorld))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, transformations %x", i, initial->png.transformations);
            return false;
        }
    }
    return true;
}

// Wrong: png_do_chop keeps the low byte of each sample.
void __cdecl WrongTransformations(PngStruct* png) {
    if ((png->transformations & kPngStrip16) != 0 && png->row_buf != nullptr && png->row_info.bit_depth == 16) {
        ++png->row_buf;
        png_do_read_transformations(png);
        --png->row_buf;
        return;
    }
    png_do_read_transformations(png);
}

// png_do_gamma: random rows of every colour type and depth (sometimes
// inconsistent), random 8-bit and 16-bit tables (or none, but never a
// missing 16-bit table where the 16-bit path runs: a null read in both),
// shifts 0..8 and others. Compared: the row with guard bytes.
struct GammaCase {
    PngRowInfo info;
    std::uint8_t row[1200];
    std::uint8_t table[256];
    std::uint16_t rows16[16][256];
    std::uint16_t* table16[256];
};

bool CompareGamma(test::Context& ctx, GammaRowFn candidate, int cases) {
    auto a = std::make_unique<GammaCase>();
    auto b = std::make_unique<GammaCase>();
    static const std::uint8_t kTypes[] = {0, 2, 3, 4, 6};
    static const std::uint8_t kDepths[] = {1, 2, 4, 8, 16};
    for (int i = 0; i < cases; ++i) {
        test::Rng& rng = ctx.rng();
        rng.Fill(a.get(), sizeof(GammaCase));
        a->info.color_type = rng.Chance(1, 10) ? static_cast<std::uint8_t>(rng.Below(8)) : kTypes[rng.Below(5)];
        a->info.bit_depth = rng.Chance(1, 10) ? static_cast<std::uint8_t>(rng.Below(20)) : kDepths[rng.Below(5)];
        a->info.width = rng.Below(100);
        const bool path16 = a->info.bit_depth == 16 ||
                            (a->info.bit_depth != 8 && a->info.color_type != 0 && a->info.color_type != 3);
        const bool noTable = rng.Chance(1, 8);
        const bool noTable16 = rng.Chance(1, 8) && !path16;
        const std::int32_t shift = rng.Chance(1, 8) ? static_cast<std::int32_t>(rng.Next32()) : rng.Range(0, 8);
        *b = *a;
        for (int k = 0; k < 256; ++k) {
            a->table16[k] = a->rows16[k % 16];
            b->table16[k] = b->rows16[k % 16];
        }
        Original_png_do_gamma(&a->info, a->row + 8, noTable ? nullptr : a->table, noTable16 ? nullptr : a->table16,
                              shift);
        candidate(&b->info, b->row + 8, noTable ? nullptr : b->table, noTable16 ? nullptr : b->table16, shift);
        if (!S3RE_EXPECT_MEM_EQ(a->row, b->row, sizeof(a->row)) ||
            !S3RE_EXPECT_MEM_EQ(&a->info, &b->info, sizeof(a->info))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: colour type %u, depth %u, width %u", i, a->info.color_type,
                     a->info.bit_depth, a->info.width);
            return false;
        }
    }
    return true;
}

// Wrong: 4-bit grey rows get their first byte changed before the lookup.
void __cdecl WrongGamma(PngRowInfo* info, std::uint8_t* row, std::uint8_t* gammaTable, std::uint16_t** gamma16Table,
                        std::int32_t gammaShift) {
    if (info->color_type == 0 && info->bit_depth == 4 && gammaTable != nullptr && info->width != 0) {
        *row ^= 1;
    }
    png_do_gamma(info, row, gammaTable, gamma16Table, gammaShift);
}

}  // namespace

S3RE_TEST(PngReadTransformations_MatchOriginal) {
    CompareTransformations(ctx, &png_do_read_transformations, 20000);
    CompareGamma(ctx, &png_do_gamma, 20000);
}

S3RE_TEST(PngReadTransformations_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareTransformations(probe, &WrongTransformations, 20000);
    S3RE_EXPECT(probe.failures() > 0);
    test::Context probe2(ctx.name(), 1, /*quiet=*/true);
    CompareGamma(probe2, &WrongGamma, 20000);
    S3RE_EXPECT(probe2.failures() > 0);
}

}  // namespace s3re::thirdparty
