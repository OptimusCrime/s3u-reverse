// libpng 1.0.2's reading API (pngread.c), statically linked into S3.EXE
// (re/picture_code.md). All cdecl.
//
// png_read_info (0x005a5180): (png_ptr, info_ptr): the rest of the 8-byte
//   signature (sig_bytes +0x124 already read; a mismatch is "Not a PNG file"
//   or, past the first four bytes, "PNG file corrupted by ASCII
//   conversion"), then chunk after chunk (length, name; png_reset_crc,
//   png_crc_read of the name) handed to its png_handle_* (unknown names to
//   png_handle_unknown) up to the first IDAT, which needs IHDR (and PLTE for
//   palette images): idat_size = its length, mode |= HAVE_IDAT.
// png_read_row (0x005a5440): (png_ptr, row, dsp_row): png_read_start_row
//   the first time; with Adam7 handled here, rows a pass doesn't touch only
//   get dsp_row combined; else the next row inflated from the IDAT chunks
//   into row_buf (irowbytes bytes), unfiltered, kept as prev_row,
//   transformed, de-interlaced and combined into row / dsp_row through the
//   pass masks; png_read_finish_row; the read_row_fn callback (+0x190).
// png_read_image (0x005a5890): (png_ptr, image): every row of every pass
//   (png_set_interlace_handling) into the row pointers.
// png_read_end (0x005a58f0): (png_ptr, info_ptr): finishes the IDAT chunk,
//   then hands the chunks after the image to their handlers up to IEND; an
//   IDAT with data (or after other chunks) is "Too many IDAT's found".
//
// The chunk names are compared as dwords (the original's inlined memcmp).

#include "game/tables.h"
#include "game/texts.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "thirdparty/png_calls.h"
#include "thirdparty/png_struct.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace s3re::thirdparty {

using PngInfoFn = void(__cdecl*)(PngStruct* png, PngInfo* info);
using ReadRowFn = void(__cdecl*)(PngStruct* png, std::uint8_t* row, std::uint8_t* dspRow);
using ReadImageFn = void(__cdecl*)(PngStruct* png, std::uint8_t** image);

void __cdecl png_read_info(PngStruct* png, PngInfo* info);
void __cdecl png_read_row(PngStruct* png, std::uint8_t* row, std::uint8_t* dspRow);
void __cdecl png_read_image(PngStruct* png, std::uint8_t** image);
void __cdecl png_read_end(PngStruct* png, PngInfo* info);

S3RE_HOOK_UNVERIFIED(0x005a5180, png_read_info, PngInfoFn, Original_png_read_info, png_read_info);
S3RE_HOOK_UNVERIFIED(0x005a5440, png_read_row, ReadRowFn, Original_png_read_row, png_read_row);
S3RE_HOOK_UNVERIFIED(0x005a5890, png_read_image, ReadImageFn, Original_png_read_image, png_read_image);
S3RE_HOOK_UNVERIFIED(0x005a58f0, png_read_end, PngInfoFn, Original_png_read_end, png_read_end);

namespace {

using ChunkHandlerFn = void(__cdecl*)(PngStruct* png, PngInfo* info, std::uint32_t length);
using SigCmpFn = std::int32_t(__cdecl*)(const std::uint8_t* sig, std::uint32_t start, std::uint32_t count);
using GetUint32Fn = std::uint32_t(__cdecl*)(const std::uint8_t* buffer);
using PngVoidFn = void(__cdecl*)(PngStruct* png);
using CrcFinishFn = std::int32_t(__cdecl*)(PngStruct* png, std::uint32_t skip);
using InflateFn = std::int32_t(__cdecl*)(ZStream* z, std::int32_t flush);
using FilterRowFn = void(__cdecl*)(PngStruct* png, PngRowInfo* rowInfo, std::uint8_t* row, const std::uint8_t* prevRow,
                                   std::int32_t filter);
using MemcpyCheckFn = void*(__cdecl*)(PngStruct* png, void* dst, const void* src, std::uint32_t size);
using CombineRowFn = void(__cdecl*)(PngStruct* png, std::uint8_t* row, std::int32_t mask);
using ReadInterlaceFn = void(__cdecl*)(PngRowInfo* rowInfo, std::uint8_t* row, std::int32_t pass,
                                       std::uint32_t transformations);
using InterlaceHandlingFn = std::int32_t(__cdecl*)(PngStruct* png);
using RowCallbackFn = void(__cdecl*)(PngStruct* png, std::uint32_t rowNumber, std::int32_t pass);

constexpr std::uintptr_t kPngSigCmp = 0x005a4de0;
constexpr std::uintptr_t kPngGetUint32 = 0x005aa8d0;
constexpr std::uintptr_t kPngResetCrc = 0x005a4ec0;
constexpr std::uintptr_t kPngCrcRead = 0x005aa900;
constexpr std::uintptr_t kPngCrcFinish = 0x005aa930;
constexpr std::uintptr_t kPngReadStartRow = 0x005b01c0;
constexpr std::uintptr_t kPngReadFinishRow = 0x005afe20;
constexpr std::uintptr_t kPngReadFilterRow = 0x005afc40;
constexpr std::uintptr_t kPngMemcpyCheck = 0x005aa0c0;
constexpr std::uintptr_t kPngDoReadTransformations = 0x005a6c30;
constexpr std::uintptr_t kPngDoReadInterlace = 0x005af7f0;
constexpr std::uintptr_t kPngCombineRow = 0x005af4d0;
constexpr std::uintptr_t kPngSetInterlaceHandling = 0x005a5df0;
constexpr std::uintptr_t kPngReadRow = 0x005a5440;
constexpr std::uintptr_t kInflate = 0x005aa4a0;

// The chunk handlers (png_rutil_chunks.cpp, png_rutil_chunks2.cpp).
constexpr std::uintptr_t kHandleIhdr = 0x005aab20;
constexpr std::uintptr_t kHandlePlte = 0x005aaea0;
constexpr std::uintptr_t kHandleIend = 0x005ab300;
constexpr std::uintptr_t kHandleBkgd = 0x005ad3b0;
constexpr std::uintptr_t kHandleChrm = 0x005abd00;
constexpr std::uintptr_t kHandleGama = 0x005ab470;
constexpr std::uintptr_t kHandleHist = 0x005ad860;
constexpr std::uintptr_t kHandleOffs = 0x005ae070;
constexpr std::uintptr_t kHandlePcal = 0x005ae430;
constexpr std::uintptr_t kHandlePhys = 0x005adcb0;
constexpr std::uintptr_t kHandleSbit = 0x005ab8b0;
constexpr std::uintptr_t kHandleSrgb = 0x005ac920;
constexpr std::uintptr_t kHandleText = 0x005aed30;
constexpr std::uintptr_t kHandleTime = 0x005ae970;
constexpr std::uintptr_t kHandleTrns = 0x005ace60;
constexpr std::uintptr_t kHandleZtxt = 0x005aeec0;
constexpr std::uintptr_t kHandleUnknown = 0x005af300;

constexpr std::uint8_t kSignatureBytes = 8;
constexpr std::uint8_t kAsciiCheckBytes = 4;  // a mismatch here: not a PNG; after: line-end conversion
constexpr std::int32_t kZPartialFlush = 1;
constexpr std::int32_t kZOk = 0;
constexpr std::int32_t kZStreamEnd = 1;
constexpr std::int32_t kWholeRow = 0xff;
constexpr std::uint8_t kLastPass = 6;
constexpr std::uint8_t kSparsePasses = 6;  // png_do_read_interlace for passes 0..5

void Error(PngStruct* png, const char* message) {
    calls::At<calls::PngMessageFn>(calls::kPngError)(png, message);
}

std::uint32_t ChunkName(const PngStruct* png) {
    std::uint32_t name;
    std::memcpy(&name, png->chunk_name, 4);
    return name;
}

bool IsIdat(std::uint32_t name) {
    return std::memcmp(&name, &game::g_pngIdat, 4) == 0;
}

// The next chunk's length and name (png_reset_crc before the name).
std::uint32_t ReadChunkHeader(PngStruct* png) {
    std::uint8_t lengthBytes[4];
    calls::At<calls::PngDataFn>(calls::kPngReadData)(png, lengthBytes, 4);
    const std::uint32_t length = calls::At<GetUint32Fn>(kPngGetUint32)(lengthBytes);
    calls::At<PngVoidFn>(kPngResetCrc)(png);
    calls::At<calls::PngDataFn>(kPngCrcRead)(png, png->chunk_name, 4);
    return length;
}

// The handler of an ancillary or known critical chunk other than IDAT (the
// order of png_read_info's comparisons; only one can match).
std::uintptr_t HandlerOf(std::uint32_t name) {
    using namespace game;
    const std::pair<std::uint32_t, std::uintptr_t> handlers[] = {
        {g_pngIhdrName, kHandleIhdr}, {g_pngPlteName, kHandlePlte}, {g_pngIendName, kHandleIend},
        {g_pngBkgdName, kHandleBkgd}, {g_pngChrmName, kHandleChrm}, {g_pngGamaName, kHandleGama},
        {g_pngHistName, kHandleHist}, {g_pngOffsName, kHandleOffs}, {g_pngPcalName, kHandlePcal},
        {g_pngPhysName, kHandlePhys}, {g_pngSbitName, kHandleSbit}, {g_pngSrgbName, kHandleSrgb},
        {g_pngTextName, kHandleText}, {g_pngTimeName, kHandleTime}, {g_pngTrnsName, kHandleTrns},
        {g_pngZtxtName, kHandleZtxt},
    };
    for (const auto& [chunk, handler] : handlers) {
        if (name == chunk) {
            return handler;
        }
    }
    return kHandleUnknown;
}

void CombineRow(PngStruct* png, std::uint8_t* row, std::int32_t mask) {
    calls::At<CombineRowFn>(kPngCombineRow)(png, row, mask);
}

void FinishRow(PngStruct* png) {
    calls::At<PngVoidFn>(kPngReadFinishRow)(png);
}

// With Adam7 handled by libpng: true when the current pass has no pixels in
// this row (dsp_row still gets the pass's pixels of the rows it covers).
bool SkipRowOfPass(PngStruct* png, std::uint8_t* dspRow) {
    const std::uint32_t row = png->row_number;
    const auto dspMask = [png] { return game::g_pngPassDspMask[png->pass]; };
    switch (png->pass) {
        case 0:
            if ((row & 7) == 0) {
                return false;
            }
            break;
        case 1:
            if ((row & 7) == 0 && png->width >= 5) {
                return false;
            }
            break;
        case 2:
            if ((row & 7) == 4) {
                return false;
            }
            if (dspRow != nullptr && (row & 4) != 0) {
                CombineRow(png, dspRow, dspMask());
            }
            FinishRow(png);
            return true;
        case 3:
            if ((row & 3) == 0 && png->width >= 3) {
                return false;
            }
            break;
        case 4:
            if ((row & 3) == 2) {
                return false;
            }
            if (dspRow != nullptr && (row & 2) != 0) {
                CombineRow(png, dspRow, dspMask());
            }
            FinishRow(png);
            return true;
        case 5:
            if ((row & 1) == 0 && png->width >= 2) {
                return false;
            }
            break;
        case 6:
            if ((row & 1) != 0) {
                return false;
            }
            FinishRow(png);
            return true;
        default: return false;
    }
    if (dspRow != nullptr) {
        CombineRow(png, dspRow, dspMask());
    }
    FinishRow(png);
    return true;
}

// Inflates the next row (irowbytes bytes, filter byte first) into row_buf.
void InflateRow(PngStruct* png) {
    ZStream& z = png->zstream;
    z.next_out = png->row_buf;
    z.avail_out = png->irowbytes;
    do {
        if (z.avail_in == 0) {
            while (png->idat_size == 0) {  // the next IDAT chunk
                calls::At<CrcFinishFn>(kPngCrcFinish)(png, 0);
                std::uint8_t lengthBytes[4];
                calls::At<calls::PngDataFn>(calls::kPngReadData)(png, lengthBytes, 4);
                png->idat_size = calls::At<GetUint32Fn>(kPngGetUint32)(lengthBytes);
                calls::At<PngVoidFn>(kPngResetCrc)(png);
                calls::At<calls::PngDataFn>(kPngCrcRead)(png, png->chunk_name, 4);
                if (!IsIdat(ChunkName(png))) {
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
            if (z.avail_out != 0 || z.avail_in != 0 || png->idat_size != 0) {
                Error(png, game::texts::kPngExtraCompressedData);
            }
            png->mode |= kPngAfterIdat;
            png->flags |= kPngFlagZlibFinished;
            break;
        }
        if (ret != kZOk) {
            Error(png, z.msg != nullptr ? z.msg : game::texts::kPngRowDecompressionError.c_str());
        }
    } while (z.avail_out != 0);
}

}  // namespace

// Purpose: Reads the PNG signature and the chunks before the image data
// (header, palette, ancillary chunks) into the info structure.
void __cdecl png_read_info(PngStruct* png, PngInfo* info) {
    namespace t = game::texts;
    if (png->sig_bytes < kSignatureBytes) {
        const std::uint32_t checked = png->sig_bytes;
        const std::uint32_t toCheck = kSignatureBytes - checked;
        calls::At<calls::PngDataFn>(calls::kPngReadData)(png, info->signature + checked, toCheck);
        png->sig_bytes = kSignatureBytes;
        const auto sigCmp = calls::At<SigCmpFn>(kPngSigCmp);
        if (sigCmp(info->signature, checked, toCheck) != 0) {
            if (checked < kAsciiCheckBytes && sigCmp(info->signature, checked, kAsciiCheckBytes - checked) != 0) {
                Error(png, t::kPngNotAPngFile);
            } else {
                Error(png, t::kPngAsciiCorrupted);
            }
        }
    }
    for (;;) {
        const std::uint32_t length = ReadChunkHeader(png);
        const std::uint32_t name = ChunkName(png);
        if (IsIdat(name)) {
            if ((png->mode & kPngHaveIhdr) == 0) {
                Error(png, t::kPngIdatMissingIhdr);
            } else if (png->color_type == kPngColorTypePalette && (png->mode & kPngHavePlte) == 0) {
                Error(png, t::kPngIdatMissingPlte);
            }
            png->idat_size = length;
            png->mode |= kPngHaveIdat;
            return;
        }
        calls::At<ChunkHandlerFn>(HandlerOf(name))(png, info, length);
    }
}

// Purpose: Reads, unfilters and transforms the next image row into `row`
// (the final pixels) and/or `dsp_row` (a progressive display row).
void __cdecl png_read_row(PngStruct* png, std::uint8_t* row, std::uint8_t* dspRow) {
    if ((png->flags & kPngFlagRowInit) == 0) {
        calls::At<PngVoidFn>(kPngReadStartRow)(png);
    }
    const bool adam7 = png->interlaced != 0 && (png->transformations & kPngInterlace) != 0;
    if (adam7 && SkipRowOfPass(png, dspRow)) {
        return;
    }
    if ((png->mode & kPngHaveIdat) == 0) {
        Error(png, game::texts::kPngInvalidRowRead);
    }
    InflateRow(png);

    PngRowInfo& info = png->row_info;
    info.color_type = png->color_type;
    info.width = png->iwidth;
    info.channels = png->channels;
    info.bit_depth = png->bit_depth;
    info.pixel_depth = png->pixel_depth;
    info.rowbytes = (info.width * info.pixel_depth + 7) >> 3;
    calls::At<FilterRowFn>(kPngReadFilterRow)(png, &info, png->row_buf + 1, png->prev_row + 1, png->row_buf[0]);
    calls::At<MemcpyCheckFn>(kPngMemcpyCheck)(png, png->prev_row, png->row_buf, png->rowbytes + 1);
    if (png->transformations != 0) {
        calls::At<PngVoidFn>(kPngDoReadTransformations)(png);
    }
    if (png->interlaced != 0 && (png->transformations & kPngInterlace) != 0) {
        if (png->pass < kSparsePasses) {
            calls::At<ReadInterlaceFn>(kPngDoReadInterlace)(&info, png->row_buf + 1, png->pass, png->transformations);
        }
        if (dspRow != nullptr) {
            CombineRow(png, dspRow, game::g_pngPassDspMask[png->pass]);
        }
        if (row != nullptr) {
            CombineRow(png, row, game::g_pngPassMask[png->pass]);
        }
    } else {
        if (row != nullptr) {
            CombineRow(png, row, kWholeRow);
        }
        if (dspRow != nullptr) {
            CombineRow(png, dspRow, kWholeRow);
        }
    }
    FinishRow(png);
    if (png->read_row_fn != nullptr) {
        reinterpret_cast<RowCallbackFn>(png->read_row_fn)(png, png->row_number, png->pass);
    }
}

// Purpose: Reads the whole image into the row pointers (all passes).
void __cdecl png_read_image(PngStruct* png, std::uint8_t** image) {
    const std::int32_t passes = calls::At<InterlaceHandlingFn>(kPngSetInterlaceHandling)(png);
    const std::uint32_t height = png->height;
    png->num_rows = height;
    for (std::int32_t pass = 0; pass < passes; ++pass) {
        std::uint8_t** rows = image;
        for (std::uint32_t i = 0; i < height; ++i) {
            calls::At<ReadRowFn>(kPngReadRow)(png, *rows, nullptr);
            ++rows;
        }
    }
}

// Purpose: Reads the chunks after the image data up to IEND.
void __cdecl png_read_end(PngStruct* png, PngInfo* info) {
    calls::At<CrcFinishFn>(kPngCrcFinish)(png, 0);  // the end of the last IDAT chunk
    do {
        const std::uint32_t length = ReadChunkHeader(png);
        const std::uint32_t name = ChunkName(png);
        if (IsIdat(name)) {
            // Empty IDATs are allowed right after the image data, nothing else.
            if (length > 0 || (png->mode & kPngAfterIdat) != 0) {
                Error(png, game::texts::kPngTooManyIdats);
            } else {
                calls::At<CrcFinishFn>(kPngCrcFinish)(png, 0);
            }
        } else {
            calls::At<ChunkHandlerFn>(HandlerOf(name))(png, info, length);
        }
    } while ((png->mode & kPngHaveIend) == 0);
}

}  // namespace s3re::thirdparty
