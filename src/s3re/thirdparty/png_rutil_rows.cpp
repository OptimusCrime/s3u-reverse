// libpng 1.0.2's row reading helpers (pngrutil.c), statically linked into
// S3.EXE (re/picture_code.md). All cdecl.
//
// png_read_filter_row (0x005afc40): (png_ptr, row_info, row, prev_row,
//   filter): undoes the row filter (none, sub, up, average, Paeth); another
//   filter type is a png_error "Bad adaptive filter type".
// png_combine_row (0x005af4d0): (png_ptr, row, mask): copies the pixels of
//   row_buf + 1 that the 8-bit mask selects (bit 7 first, repeating) into
//   row; mask 0xff copies the whole row.
// png_do_read_interlace (0x005af7f0): (row_info, row, pass, transformations):
//   spreads an interlace pass's pixels over the full width, in place, from
//   the right (each pixel repeated png_pass_inc[pass] times).
// png_read_finish_row (0x005afe20): after a row: the next row or pass; after
//   the last one, reads to the end of the zlib stream (one more byte must
//   not come out) and checks that the image data ends there.
// png_read_start_row (0x005b01c0): before the first row: the transformations,
//   the first pass's sizes, the row buffers (row_buf big enough for the
//   widest transformed pixel, prev_row cleared).
//
// Quirk kept: png_read_finish_row leaves zstream.next_out pointing at a
// local byte of its own stack frame (one byte of room so inflate can tell
// whether data follows); the tests leave that pointer out.

#include "game/tables.h"
#include "game/texts.h"
#include "hooks.h"
#include "test/difftest.h"
#include "thirdparty/png_calls.h"
#include "thirdparty/png_struct.h"

#include <intrin.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace s3re::thirdparty {

using FilterRowFn = void(__cdecl*)(PngStruct* png, PngRowInfo* rowInfo, std::uint8_t* row, const std::uint8_t* prevRow,
                                   std::int32_t filter);
using CombineRowFn = void(__cdecl*)(PngStruct* png, std::uint8_t* row, std::int32_t mask);
using ReadInterlaceFn = void(__cdecl*)(PngRowInfo* rowInfo, std::uint8_t* row, std::int32_t pass,
                                       std::uint32_t transformations);
using PngRowFn = void(__cdecl*)(PngStruct* png);

void __cdecl png_read_filter_row(PngStruct* png, PngRowInfo* rowInfo, std::uint8_t* row, const std::uint8_t* prevRow,
                                 std::int32_t filter);
void __cdecl png_combine_row(PngStruct* png, std::uint8_t* row, std::int32_t mask);
void __cdecl png_do_read_interlace(PngRowInfo* rowInfo, std::uint8_t* row, std::int32_t pass,
                                   std::uint32_t transformations);
void __cdecl png_read_finish_row(PngStruct* png);
void __cdecl png_read_start_row(PngStruct* png);

S3RE_HOOK(0x005afc40, png_read_filter_row, FilterRowFn, Original_png_read_filter_row, png_read_filter_row);
S3RE_HOOK(0x005af4d0, png_combine_row, CombineRowFn, Original_png_combine_row, png_combine_row);
S3RE_HOOK(0x005af7f0, png_do_read_interlace, ReadInterlaceFn, Original_png_do_read_interlace, png_do_read_interlace);
S3RE_HOOK_UNVERIFIED(0x005afe20, png_read_finish_row, PngRowFn, Original_png_read_finish_row, png_read_finish_row);
S3RE_HOOK_UNVERIFIED(0x005b01c0, png_read_start_row, PngRowFn, Original_png_read_start_row, png_read_start_row);

namespace {

using MemsetCheckFn = void*(__cdecl*)(PngStruct* png, void* dst, std::int32_t value, std::uint32_t size);
using CrcFinishFn = std::int32_t(__cdecl*)(PngStruct* png, std::uint32_t skip);
using PngVoidFn = void(__cdecl*)(PngStruct* png);
using GetUint32Fn = std::uint32_t(__cdecl*)(const std::uint8_t* buffer);
using InflateFn = std::int32_t(__cdecl*)(ZStream* z, std::int32_t flush);
using StreamFn = std::int32_t(__cdecl*)(ZStream* z);

constexpr std::uintptr_t kPngMemsetCheck = 0x005aa0f0;
constexpr std::uintptr_t kPngCrcFinish = 0x005aa930;
constexpr std::uintptr_t kPngCrcRead = 0x005aa900;
constexpr std::uintptr_t kPngResetCrc = 0x005a4ec0;
constexpr std::uintptr_t kPngGetUint32 = 0x005aa8d0;
constexpr std::uintptr_t kPngInitReadTransformations = 0x005a6220;
constexpr std::uintptr_t kInflate = 0x005aa4a0;
constexpr std::uintptr_t kInflateReset = 0x005aa270;

constexpr std::int32_t kFilterNone = 0;
constexpr std::int32_t kFilterSub = 1;
constexpr std::int32_t kFilterUp = 2;
constexpr std::int32_t kFilterAverage = 3;
constexpr std::int32_t kFilterPaeth = 4;
constexpr std::int32_t kMaskAllPixels = 0xff;
constexpr std::uint8_t kPasses = 7;
constexpr std::int32_t kZPartialFlush = 1;
constexpr std::int32_t kZOk = 0;
constexpr std::int32_t kZStreamEnd = 1;

void Error(PngStruct* png, const char* message) {
    calls::At<calls::PngMessageFn>(calls::kPngError)(png, message);
}

// memcpy as MSVC 6 inlines it: rep movsd, then rep movsb.
void CopyBytes(std::uint8_t* dst, const std::uint8_t* src, std::uint32_t count) {
    __movsd(reinterpret_cast<unsigned long*>(dst), reinterpret_cast<const unsigned long*>(src), count / 4);
    __movsb(dst + (count & ~3u), src + (count & ~3u), count % 4);
}

std::uint32_t BytesPerPixel(std::uint8_t pixelDepth) {
    return (pixelDepth + 7u) / 8;
}

std::uint32_t PassWidth(std::uint32_t width, std::uint8_t pass) {
    return (width + game::g_pngPassInc[pass] - 1 - game::g_pngPassStart[pass]) / game::g_pngPassInc[pass];
}

std::uint32_t PassRows(std::uint32_t height, std::uint8_t pass) {
    return (height + game::g_pngPassYinc[pass] - 1 - game::g_pngPassYstart[pass]) / game::g_pngPassYinc[pass];
}

// png_combine_row / png_do_read_interlace for 1, 2 and 4 bits per pixel:
// where a sample sits in its byte and how the shift moves to the next one
// (PNG_PACKSWAP: the first pixel in the low bits).
struct SubBytePixels {
    std::int32_t start, end, step;
};

// The byte with a 1/2/4-bit sample replaced: `bits` wide at `shift`.
std::uint8_t PutSample(std::uint8_t byte, std::uint32_t value, std::int32_t shift, std::int32_t bits) {
    // (0x7f7f >> (7 - shift)) etc.: the other bits of the byte.
    const std::int32_t keep = (bits == 1 ? 0x7f7f : bits == 2 ? 0x3f3f : 0x0f0f) >> ((8 - bits) - shift);
    return static_cast<std::uint8_t>((byte & keep) | (value << shift));
}

}  // namespace

// Purpose: Undoes the filter of one decompressed row (PNG filter types 0..4).
void __cdecl png_read_filter_row(PngStruct* png, PngRowInfo* rowInfo, std::uint8_t* row, const std::uint8_t* prevRow,
                                 std::int32_t filter) {
    switch (filter) {
        case kFilterNone:
            break;
        case kFilterSub: {
            const std::uint32_t bpp = BytesPerPixel(rowInfo->pixel_depth);
            for (std::uint32_t i = bpp; i < rowInfo->rowbytes; ++i) {
                row[i] = static_cast<std::uint8_t>(row[i] + row[i - bpp]);
            }
            break;
        }
        case kFilterUp:
            for (std::uint32_t i = 0; i < rowInfo->rowbytes; ++i) {
                row[i] = static_cast<std::uint8_t>(row[i] + prevRow[i]);
            }
            break;
        case kFilterAverage: {
            const std::uint32_t bpp = BytesPerPixel(rowInfo->pixel_depth);
            const std::uint32_t rest = rowInfo->rowbytes - bpp;  // (wraps if rowbytes < bpp, as the original)
            std::uint8_t* rp = row;
            const std::uint8_t* pp = prevRow;
            const std::uint8_t* lp = row;
            for (std::uint32_t i = 0; i < bpp; ++i, ++rp) {
                *rp = static_cast<std::uint8_t>(*rp + (*pp++ >> 1));
            }
            for (std::uint32_t i = 0; i < rest; ++i, ++rp) {
                *rp = static_cast<std::uint8_t>(*rp + ((*pp++ + *lp++) >> 1));
            }
            break;
        }
        case kFilterPaeth: {
            const std::uint32_t bpp = BytesPerPixel(rowInfo->pixel_depth);
            const std::uint32_t rest = rowInfo->rowbytes - bpp;
            std::uint8_t* rp = row;
            const std::uint8_t* pp = prevRow;
            const std::uint8_t* lp = row;
            const std::uint8_t* cp = prevRow;
            for (std::uint32_t i = 0; i < bpp; ++i, ++rp) {
                *rp = static_cast<std::uint8_t>(*rp + *pp++);
            }
            for (std::uint32_t i = 0; i < rest; ++i, ++rp) {
                const std::int32_t a = *lp++;  // left
                const std::int32_t b = *pp++;  // above
                const std::int32_t c = *cp++;  // above left
                const std::int32_t p = b - c;
                const std::int32_t q = a - c;
                const std::int32_t pa = p < 0 ? -p : p;
                const std::int32_t pb = q < 0 ? -q : q;
                const std::int32_t pc = p + q < 0 ? -(p + q) : p + q;
                const std::int32_t predictor = (pa <= pb && pa <= pc) ? a : (pb <= pc) ? b : c;
                *rp = static_cast<std::uint8_t>(*rp + predictor);
            }
            break;
        }
        default:
            Error(png, game::texts::kPngBadFilterType);
            break;
    }
}

// Purpose: Copies the pixels of the row just decoded that the interlace
// pass's mask selects into the caller's row.
void __cdecl png_combine_row(PngStruct* png, std::uint8_t* row, std::int32_t mask) {
    const std::uint8_t* sp = png->row_buf + 1;
    const std::uint8_t depth = png->row_info.pixel_depth;
    if (mask == kMaskAllPixels) {
        CopyBytes(row, sp, (png->width * depth + 7) >> 3);
        return;
    }
    std::uint8_t* dp = row;
    if (depth == 1 || depth == 2 || depth == 4) {
        const std::int32_t bits = depth;
        const std::int32_t last = 8 - bits;
        const SubBytePixels order = (png->transformations & kPngPackswap) != 0 ? SubBytePixels{0, last, bits}
                                                                               : SubBytePixels{last, 0, -bits};
        std::int32_t shift = order.start;
        std::int32_t m = 0x80;
        for (std::uint32_t i = 0; i < png->width; ++i) {
            if ((m & mask) != 0) {
                const std::uint32_t value = (*sp >> shift) & ((1u << bits) - 1);
                *dp = PutSample(*dp, value, shift, bits);
            }
            if (shift == order.end) {
                shift = order.start;
                ++sp;
                ++dp;
            } else {
                shift += order.step;
            }
            m = m == 1 ? 0x80 : m >> 1;
        }
        return;
    }
    const std::uint32_t pixelBytes = depth >> 3;
    std::uint8_t m = 0x80;
    for (std::uint32_t i = 0; i < png->width; ++i) {
        if ((m & mask) != 0) {
            CopyBytes(dp, sp, pixelBytes);
        }
        sp += pixelBytes;
        dp += pixelBytes;
        m = m == 1 ? 0x80 : static_cast<std::uint8_t>(m >> 1);
    }
}

// Purpose: Expands one interlace pass's row to the full image width.
void __cdecl png_do_read_interlace(PngRowInfo* rowInfo, std::uint8_t* row, std::int32_t pass,
                                   std::uint32_t transformations) {
    if (row == nullptr || rowInfo == nullptr) {
        return;
    }
    const std::int32_t repeat = static_cast<std::int32_t>(game::g_pngPassInc[pass]);
    const std::uint32_t width = rowInfo->width;
    const std::uint32_t finalWidth = width * static_cast<std::uint32_t>(repeat);
    const std::uint8_t depth = rowInfo->pixel_depth;
    if (depth == 1 || depth == 2 || depth == 4) {
        const std::int32_t bits = depth;
        const std::int32_t perByte = 8 / bits;
        const std::int32_t last = 8 - bits;
        const std::int32_t lastIndex = perByte - 1;
        const std::uint8_t* sp = row + (width - 1) / static_cast<std::uint32_t>(perByte);
        std::uint8_t* dp = row + (finalWidth - 1) / static_cast<std::uint32_t>(perByte);
        // The shift of the last pixel of each row (walking backwards).
        const std::int32_t sIndex = static_cast<std::int32_t>((width + lastIndex) & lastIndex);
        const std::int32_t dIndex = static_cast<std::int32_t>((finalWidth + lastIndex) & lastIndex);
        std::int32_t sshift, dshift;
        SubBytePixels order;
        if ((transformations & kPngPackswap) != 0) {
            sshift = sIndex * bits;
            dshift = dIndex * bits;
            order = {last, 0, -bits};
        } else {
            sshift = (lastIndex - sIndex) * bits;
            dshift = (lastIndex - dIndex) * bits;
            order = {0, last, bits};
        }
        for (std::uint32_t i = 0; i < width; ++i) {
            const std::uint32_t value = (*sp >> sshift) & ((1u << bits) - 1);
            for (std::int32_t j = 0; j < repeat; ++j) {
                *dp = PutSample(*dp, value, dshift, bits);
                if (dshift == order.end) {
                    dshift = order.start;
                    --dp;
                } else {
                    dshift += order.step;
                }
            }
            if (sshift == order.end) {
                sshift = order.start;
                --sp;
            } else {
                sshift += order.step;
            }
        }
    } else {
        const std::uint32_t pixelBytes = depth >> 3;
        const std::uint8_t* sp = row + (width - 1) * pixelBytes;
        std::uint8_t* dp = row + (finalWidth - 1) * pixelBytes;
        for (std::uint32_t i = 0; i < width; ++i) {
            std::uint8_t pixel[32];  // the original's buffer has 8 bytes (64-bit pixels, the widest PNG has)
            CopyBytes(pixel, sp, pixelBytes);
            for (std::int32_t j = 0; j < repeat; ++j) {
                CopyBytes(dp, pixel, pixelBytes);
                dp -= pixelBytes;
            }
            sp -= pixelBytes;
        }
    }
    rowInfo->width = finalWidth;
    rowInfo->rowbytes = (finalWidth * depth + 7) >> 3;
}

// Purpose: Moves on after a decoded row: the next row of the pass, the next
// non-empty pass, or (after the last row) the end of the image data.
void __cdecl png_read_finish_row(PngStruct* png) {
    ++png->row_number;
    if (png->row_number < png->num_rows) {
        return;
    }

    if (png->interlaced != 0) {
        png->row_number = 0;
        calls::At<MemsetCheckFn>(kPngMemsetCheck)(png, png->prev_row, 0, png->rowbytes + 1);
        do {
            ++png->pass;
            if (png->pass >= kPasses) {
                break;
            }
            png->iwidth = PassWidth(png->width, png->pass);
            png->irowbytes = ((png->iwidth * png->pixel_depth + 7) >> 3) + 1;
            if ((png->transformations & kPngInterlace) != 0) {
                break;
            }
            png->num_rows = PassRows(png->height, png->pass);  // (an empty pass is skipped by its width only)
        } while (png->iwidth == 0);
        if (png->pass < kPasses) {
            return;
        }
    }

    if ((png->flags & kPngFlagZlibFinished) == 0) {
        std::uint8_t extra;  // inflate must not produce this byte
        ZStream& z = png->zstream;
        z.next_out = &extra;
        z.avail_out = 1;
        for (;;) {
            if (z.avail_in == 0) {
                while (png->idat_size == 0) {  // the next IDAT chunk
                    std::uint8_t chunkLength[4];
                    calls::At<CrcFinishFn>(kPngCrcFinish)(png, 0);
                    calls::At<calls::PngDataFn>(calls::kPngReadData)(png, chunkLength, 4);
                    png->idat_size = calls::At<GetUint32Fn>(kPngGetUint32)(chunkLength);
                    calls::At<PngVoidFn>(kPngResetCrc)(png);
                    calls::At<calls::PngDataFn>(kPngCrcRead)(png, png->chunk_name, 4);
                    if (std::memcmp(png->chunk_name, &game::g_pngIdat, 4) != 0) {
                        Error(png, game::texts::kPngNotEnoughImageData);
                    }
                }
                z.avail_in = png->zbuf_size;
                z.next_in = png->zbuf;
                if (png->zbuf_size > png->idat_size) {
                    z.avail_in = png->idat_size;
                }
                calls::At<calls::PngDataFn>(kPngCrcRead)(png, png->zbuf, z.avail_in);
                png->idat_size -= z.avail_in;
            }
            const std::int32_t ret = calls::At<InflateFn>(kInflate)(&z, kZPartialFlush);
            if (ret == kZStreamEnd) {
                if (z.avail_out == 0 || z.avail_in != 0 || png->idat_size != 0) {
                    Error(png, game::texts::kPngExtraCompressedData);
                }
                png->mode |= kPngAfterIdat;
                png->flags |= kPngFlagZlibFinished;
                break;
            }
            if (ret != kZOk) {
                Error(png, z.msg != nullptr ? z.msg : game::texts::kPngDecompressionError.c_str());
            }
            if (z.avail_out == 0) {
                Error(png, game::texts::kPngExtraCompressedData);
            }
        }
        z.avail_out = 0;
    }

    if (png->idat_size != 0 || png->zstream.avail_in != 0) {
        Error(png, game::texts::kPngExtraCompressionData);
    }
    calls::At<StreamFn>(kInflateReset)(&png->zstream);
    png->mode |= kPngAfterIdat;
}

// Purpose: Prepares reading the image rows: transformations, the first
// pass's row sizes and the row buffers.
void __cdecl png_read_start_row(PngStruct* png) {
    png->zstream.avail_in = 0;
    calls::At<PngVoidFn>(kPngInitReadTransformations)(png);
    std::uint32_t irowbytes;
    if (png->interlaced != 0) {
        png->num_rows = (png->transformations & kPngInterlace) == 0 ? PassRows(png->height, 0) : png->height;
        png->iwidth = PassWidth(png->width, png->pass);
        irowbytes = (png->iwidth * png->pixel_depth + 7) >> 3;
    } else {
        png->num_rows = png->height;
        png->iwidth = png->width;
        irowbytes = png->rowbytes;
    }
    png->irowbytes = irowbytes + 1;

    // The widest pixel the transformations make.
    const std::uint32_t transformations = png->transformations;
    const bool expand = (transformations & kPngExpand) != 0;
    std::int32_t maxPixelDepth = png->pixel_depth;
    if ((transformations & kPngPack) != 0 && png->bit_depth < 8) {
        maxPixelDepth = 8;
    }
    if (expand) {
        if (png->color_type == kPngColorTypePalette) {
            maxPixelDepth = png->num_trans != 0 ? 32 : 24;
        } else if (png->color_type == kPngColorTypeGray) {
            if (maxPixelDepth < 8) {
                maxPixelDepth = 8;
            }
            if (png->num_trans != 0) {
                maxPixelDepth *= 2;
            }
        } else if (png->color_type == kPngColorTypeRgb && png->num_trans != 0) {
            maxPixelDepth = maxPixelDepth * 4 / 3;
        }
    }
    if ((transformations & kPngFiller) != 0) {
        if (png->color_type == kPngColorTypePalette) {
            maxPixelDepth = 32;
        } else if (png->color_type == kPngColorTypeGray) {
            maxPixelDepth = maxPixelDepth <= 8 ? 16 : 32;
        } else if (png->color_type == kPngColorTypeRgb) {
            maxPixelDepth = maxPixelDepth <= 32 ? 32 : 64;
        }
    }
    if ((transformations & kPngGrayToRgb) != 0) {
        if ((png->num_trans != 0 && expand) || png->color_type == kPngColorTypeGrayAlpha) {
            if (maxPixelDepth <= 16) {
                maxPixelDepth = 32;
            } else if (maxPixelDepth <= 32) {
                maxPixelDepth = 64;
            }
        } else {
            if (maxPixelDepth <= 8) {
                maxPixelDepth = 24;
            } else if (maxPixelDepth <= 16) {
                maxPixelDepth = 48;
            }
        }
    }

    // The width rounded up to 8 pixels (interlacing), plus a byte and a
    // pixel for safety.
    const std::uint32_t alignedWidth = (png->width + 7) & ~7u;
    const std::uint32_t rowBytes = ((alignedWidth * static_cast<std::uint32_t>(maxPixelDepth) + 7) >> 3) + 1 +
                                   static_cast<std::uint32_t>((maxPixelDepth + 7) >> 3);
    const auto malloc = calls::At<calls::PngMallocFn>(calls::kPngMalloc);
    png->row_buf = static_cast<std::uint8_t*>(malloc(png, rowBytes));
    png->prev_row = static_cast<std::uint8_t*>(malloc(png, png->rowbytes + 1));
    calls::At<MemsetCheckFn>(kPngMemsetCheck)(png, png->prev_row, 0, png->rowbytes + 1);
    png->flags |= kPngFlagRowInit;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// png_read_filter_row, png_combine_row and png_do_read_interlace on random
// rows of every pixel depth PNG has (1..64 bits; rowbytes at least one pixel,
// as libpng's rows always are), every filter type (an invalid one reaches
// png_error, whose error callback here throws), random masks and passes,
// with and without PNG_PACKSWAP. Compared: the row buffers with guard
// bytes, row_info, the png_struct and the error messages.
// png_read_start_row and png_read_finish_row are compared within whole PNG
// decodes (png_read_tests.cpp).

namespace {

struct RowsErrorThrown {};

int g_rowErrors = 0;

void __cdecl RowsTestError(PngStruct* /*png*/, const char* message) {
    g_rowErrors = g_rowErrors * 31 + static_cast<std::uint8_t>(message[0]);
    throw RowsErrorThrown{};
}

struct RowsFunctions {
    FilterRowFn filter;
    CombineRowFn combine;
    ReadInterlaceFn interlace;
};

constexpr std::size_t kRowArea = 4096;

struct RowsCase {
    PngStruct png;
    PngRowInfo info;
    std::uint8_t row[kRowArea];
    std::uint8_t prev[kRowArea];
    std::uint8_t buf[kRowArea];
};

constexpr std::uint8_t kDepths[] = {1, 2, 4, 8, 16, 24, 32, 48, 64};

int RunRows(const RowsFunctions& f, int op, RowsCase& c, std::int32_t arg, std::uint32_t transformations) {
    g_rowErrors = 0;
    try {
        switch (op) {
            case 0: f.filter(&c.png, &c.info, c.row, c.prev, arg); break;
            case 1: f.combine(&c.png, c.row, arg); break;
            default: f.interlace(arg < 0 ? nullptr : &c.info, c.row, arg < 0 ? 0 : arg, transformations); break;
        }
    } catch (const RowsErrorThrown&) {
        return g_rowErrors | 0x40000000;
    }
    return g_rowErrors;
}

void CompareRowHelpers(test::Context& ctx, const RowsFunctions& candidate, int cases) {
    const RowsFunctions original{Original_png_read_filter_row, Original_png_combine_row, Original_png_do_read_interlace};
    auto a = std::make_unique<RowsCase>();
    auto b = std::make_unique<RowsCase>();
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(3));
        ctx.rng().Fill(a.get(), sizeof(RowsCase));
        const std::uint8_t depth = kDepths[ctx.rng().Below(sizeof(kDepths))];
        const std::uint32_t bpp = (depth + 7u) / 8;
        std::uint32_t width = ctx.rng().Chance(1, 4) ? ctx.rng().Below(10) : 1 + ctx.rng().Below(200);
        std::int32_t arg = 0;
        std::uint32_t transformations = ctx.rng().Next32() & ~kPngPackswap;
        if (ctx.rng().Chance(1, 2)) {
            transformations |= kPngPackswap;
        }
        a->png.error_fn = reinterpret_cast<void*>(&RowsTestError);
        a->png.transformations = transformations;
        a->info.pixel_depth = depth;
        if (op == 0) {
            a->info.rowbytes = std::max<std::uint32_t>(bpp, (width * depth + 7) / 8);
            arg = ctx.rng().Chance(1, 12) ? static_cast<std::int32_t>(ctx.rng().Next32()) : static_cast<std::int32_t>(ctx.rng().Below(5));
        } else if (op == 1) {
            a->png.width = width;
            a->png.row_info.pixel_depth = depth;
            static const std::int32_t kMasks[] = {0xff, 0x80, 0x08, 0x88, 0x22, 0xaa, 0x55, 0x33, 0xcc, 0x0f};
            arg = ctx.rng().Chance(1, 5) ? static_cast<std::int32_t>(ctx.rng().Next32()) : kMasks[ctx.rng().Below(10)];
            a->png.row_buf = a->buf;
        } else {
            arg = ctx.rng().Chance(1, 30) ? -1 : static_cast<std::int32_t>(ctx.rng().Below(7));
            width = std::min<std::uint32_t>(width, (kRowArea * 8 / 64) / 8);  // final width * 64 bits fits
            a->info.width = ctx.rng().Chance(1, 20) ? 0 : std::max<std::uint32_t>(width, 1);
        }
        *b = *a;
        const int ea = RunRows(original, op, *a, arg, transformations);
        const int eb = RunRows(candidate, op, *b, arg, transformations);
        bool same = S3RE_EXPECT_EQ(ea, eb);
        same &= S3RE_EXPECT_MEM_EQ(a.get(), b.get(), sizeof(RowsCase));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d, depth %u, width %u, arg %d", i, op, depth, width, arg);
            return;
        }
    }
}

// Wrong: the average filter rounds up.
void __cdecl WrongFilterRow(PngStruct* png, PngRowInfo* rowInfo, std::uint8_t* row, const std::uint8_t* prevRow,
                            std::int32_t filter) {
    if (filter != kFilterAverage) {
        png_read_filter_row(png, rowInfo, row, prevRow, filter);
        return;
    }
    const std::uint32_t bpp = BytesPerPixel(rowInfo->pixel_depth);
    for (std::uint32_t i = 0; i < rowInfo->rowbytes; ++i) {
        const std::uint32_t left = i >= bpp ? row[i - bpp] : 0;
        row[i] = static_cast<std::uint8_t>(row[i] + ((left + prevRow[i] + 1) >> 1));
    }
}

}  // namespace

S3RE_TEST(PngRowHelpers_MatchOriginal) {
    CompareRowHelpers(ctx, {&png_read_filter_row, &png_combine_row, &png_do_read_interlace}, 6000);
}

S3RE_TEST(PngRowHelpers_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareRowHelpers(probe, {&WrongFilterRow, &png_combine_row, &png_do_read_interlace}, 6000);
    S3RE_EXPECT(probe.failures() > 0);
}

// For the emulator's self-test (tools/emutest/specs/self_test.py): the row
// step leaving avail_out at 1 (next to the ignored zstream.next_out).
void __cdecl WrongFinishRow(PngStruct* png) {
    png_read_finish_row(png);
    png->zstream.avail_out = 1;
}

}  // namespace s3re::thirdparty
