// libpng 1.0.2's chunk handlers (pngrutil.c), statically linked into S3.EXE
// (re/picture_code.md). Each is called by png_read_info / png_read_end with
// the chunk's length after its name was read (and the CRC started over it);
// it reads the data, checks the CRC (png_crc_finish, which the original has
// inlined everywhere) and stores the values with the matching png_set_*.
// All cdecl: (png_ptr, info_ptr, length).
//
// png_handle_IHDR (0x005aab20): the image header: size, bit depth, colour
//   type, interlacing; channels, pixel depth and rowbytes; png_set_IHDR.
// png_handle_IEND (0x005ab300): the end; an error without IHDR and IDAT.
// png_handle_sBIT (0x005ab8b0), png_handle_tRNS (0x005ace60),
// png_handle_bKGD (0x005ad3b0), png_handle_hIST (0x005ad860),
// png_handle_pHYs (0x005adcb0), png_handle_oFFs (0x005ae070),
// png_handle_tIME (0x005ae970): the ancillary chunks of those names. Out of
//   place, duplicate or wrongly sized ones are skipped with a warning (a
//   missing IHDR is an error).
// png_handle_tEXt (0x005aed30): a keyword, a 0 byte and the text, kept in one
//   png_malloc block (the text points into it), added with png_set_text.
// png_handle_PLTE (0x005aaea0): the palette, in a png_zalloc block (3 bytes
//   per entry); its CRC result is ignored (the palette is used either way);
//   a tRNS read before it with more entries is cut to the palette size.
// png_handle_gAMA (0x005ab470): the file gamma, 100000 times the value; 0 is
//   ignored, and with sRGB anything but 45000 (a warning and a line on
//   stderr). png->gamma gets the float, png_set_gAMA the double product
//   (x87: fild * 1e-5f, as the original); info_ptr is not checked for null
//   there (a crash in both).
// png_handle_unknown (0x005af300): any other chunk: a name with a non-letter
//   or an unknown critical chunk is an error, an ancillary one is skipped.
//
// Quirks kept: after png_error (which never returns) the code goes on as if
// it had (IEND: `if (info_ptr == NULL) return;`, the others: the length
// checks); bKGD reads the palette entry of the index without a range check;
// tIME's png_time on the stack has an uninitialized padding byte, which
// png_set_tIME copies into info_ptr->mod_time[7] (ours: 0; the tests leave
// that byte out); IHDR has no "palette with more than 8 bits" check, so
// 16-bit palette images pass (later libpng versions reject them).

#include "game/globals.h"
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

using ChunkHandlerFn = void(__cdecl*)(PngStruct* png, PngInfo* info, std::uint32_t length);
using SetTextFn = void(__cdecl*)(PngStruct* png, PngInfo* info, PngText* text, std::int32_t count);
using SetPlteFn = void(__cdecl*)(PngStruct* png, PngInfo* info, void* palette, std::int32_t count);
using ZallocFn = void*(__cdecl*)(PngStruct* png, std::uint32_t items, std::uint32_t size);
using SetGamaFn = void(__cdecl*)(PngStruct* png, PngInfo* info, double gamma);

void __cdecl png_handle_IHDR(PngStruct* png, PngInfo* info, std::uint32_t length);
void __cdecl png_handle_IEND(PngStruct* png, PngInfo* info, std::uint32_t length);
void __cdecl png_handle_sBIT(PngStruct* png, PngInfo* info, std::uint32_t length);
void __cdecl png_handle_tRNS(PngStruct* png, PngInfo* info, std::uint32_t length);
void __cdecl png_handle_bKGD(PngStruct* png, PngInfo* info, std::uint32_t length);
void __cdecl png_handle_hIST(PngStruct* png, PngInfo* info, std::uint32_t length);
void __cdecl png_handle_pHYs(PngStruct* png, PngInfo* info, std::uint32_t length);
void __cdecl png_handle_oFFs(PngStruct* png, PngInfo* info, std::uint32_t length);
void __cdecl png_handle_tIME(PngStruct* png, PngInfo* info, std::uint32_t length);
void __cdecl png_handle_tEXt(PngStruct* png, PngInfo* info, std::uint32_t length);
void __cdecl png_handle_unknown(PngStruct* png, PngInfo* info, std::uint32_t length);
void __cdecl png_handle_PLTE(PngStruct* png, PngInfo* info, std::uint32_t length);
void __cdecl png_handle_gAMA(PngStruct* png, PngInfo* info, std::uint32_t length);

S3RE_HOOK(0x005aab20, png_handle_IHDR, ChunkHandlerFn, Original_png_handle_IHDR, png_handle_IHDR);
S3RE_HOOK(0x005ab300, png_handle_IEND, ChunkHandlerFn, Original_png_handle_IEND, png_handle_IEND);
S3RE_HOOK(0x005ab8b0, png_handle_sBIT, ChunkHandlerFn, Original_png_handle_sBIT, png_handle_sBIT);
S3RE_HOOK(0x005ace60, png_handle_tRNS, ChunkHandlerFn, Original_png_handle_tRNS, png_handle_tRNS);
S3RE_HOOK(0x005ad3b0, png_handle_bKGD, ChunkHandlerFn, Original_png_handle_bKGD, png_handle_bKGD);
S3RE_HOOK(0x005ad860, png_handle_hIST, ChunkHandlerFn, Original_png_handle_hIST, png_handle_hIST);
S3RE_HOOK(0x005adcb0, png_handle_pHYs, ChunkHandlerFn, Original_png_handle_pHYs, png_handle_pHYs);
S3RE_HOOK(0x005ae070, png_handle_oFFs, ChunkHandlerFn, Original_png_handle_oFFs, png_handle_oFFs);
S3RE_HOOK(0x005ae970, png_handle_tIME, ChunkHandlerFn, Original_png_handle_tIME, png_handle_tIME);
S3RE_HOOK(0x005aed30, png_handle_tEXt, ChunkHandlerFn, Original_png_handle_tEXt, png_handle_tEXt);
S3RE_HOOK(0x005af300, png_handle_unknown, ChunkHandlerFn, Original_png_handle_unknown, png_handle_unknown);
S3RE_HOOK(0x005aaea0, png_handle_PLTE, ChunkHandlerFn, Original_png_handle_PLTE, png_handle_PLTE);
S3RE_HOOK(0x005ab470, png_handle_gAMA, ChunkHandlerFn, Original_png_handle_gAMA, png_handle_gAMA);

namespace {

using CrcFinishFn = std::int32_t(__cdecl*)(PngStruct* png, std::uint32_t skip);
using SetIhdrFn = void(__cdecl*)(PngStruct* png, PngInfo* info, std::uint32_t width, std::uint32_t height,
                                 std::int32_t bitDepth, std::int32_t colorType, std::int32_t interlace,
                                 std::int32_t compression, std::int32_t filter);
using SetColor8Fn = void(__cdecl*)(PngStruct* png, PngInfo* info, const PngColor8* value);
using SetColor16Fn = void(__cdecl*)(PngStruct* png, PngInfo* info, const PngColor16* value);
using SetTrnsFn = void(__cdecl*)(PngStruct* png, PngInfo* info, std::uint8_t* trans, std::int32_t numTrans,
                                 PngColor16* values);
using SetHistFn = void(__cdecl*)(PngStruct* png, PngInfo* info, std::uint16_t* hist);
using SetPhysFn = void(__cdecl*)(PngStruct* png, PngInfo* info, std::uint32_t x, std::uint32_t y, std::int32_t unit);
using SetTimeFn = void(__cdecl*)(PngStruct* png, PngInfo* info, const void* time);

constexpr std::uintptr_t kPngCrcFinish = 0x005aa930;
constexpr std::uintptr_t kPngCrcRead = 0x005aa900;
constexpr std::uintptr_t kPngSetIhdr = 0x005b1660;
constexpr std::uintptr_t kPngSetSbit = 0x005b18f0;
constexpr std::uintptr_t kPngSetTrns = 0x005b1ae0;
constexpr std::uintptr_t kPngSetBkgd = 0x005b1550;
constexpr std::uintptr_t kPngSetHist = 0x005b1630;
constexpr std::uintptr_t kPngSetPhys = 0x005b1890;
constexpr std::uintptr_t kPngSetOffs = 0x005b1720;
constexpr std::uintptr_t kPngSetTime = 0x005b1ab0;
constexpr std::uintptr_t kPngSetText = 0x005b19a0;
constexpr std::uintptr_t kPngSetPlte = 0x005b18c0;
constexpr std::uintptr_t kPngZalloc = 0x005a4e30;
constexpr std::uintptr_t kPngSetGama = 0x005b1600;
constexpr std::uint32_t kSrgbGamma = 45000;  // 1.0.2 accepts exactly 45000 (0.45) with sRGB, not 45455
constexpr std::uint32_t kPngFlagFreePalette = 0x1000;

constexpr std::uint32_t kIhdrLength = 13;
constexpr std::uint32_t kPhysLength = 9;   // pHYs and oFFs: two 32-bit values and a unit
constexpr std::uint32_t kTimeLength = 7;
constexpr std::uint32_t kMaxDimension = 0x7fffffff;
constexpr std::uint32_t kInterlaceLast = 2;  // PNG_INTERLACE_LAST

void Error(PngStruct* png, const char* message) {
    calls::At<calls::PngMessageFn>(calls::kPngError)(png, message);
}

void Warning(PngStruct* png, const char* message) {
    calls::At<calls::PngMessageFn>(calls::kPngWarning)(png, message);
}

// png_crc_finish: skips the rest of the chunk and checks its CRC; true when
// the CRC was wrong (and only warned about).
bool CrcFinish(PngStruct* png, std::uint32_t skip) {
    return calls::At<CrcFinishFn>(kPngCrcFinish)(png, skip) != 0;
}

void CrcRead(PngStruct* png, std::uint8_t* buffer, std::uint32_t length) {
    calls::At<calls::PngDataFn>(kPngCrcRead)(png, buffer, length);
}

// A warning and the chunk skipped.
void SkipChunk(PngStruct* png, std::uint32_t length, const char* message) {
    Warning(png, message);
    CrcFinish(png, length);
}

std::uint32_t Uint32(const std::uint8_t* b) {  // png_get_uint_32, inlined
    return (((((static_cast<std::uint32_t>(b[0]) << 8) + b[1]) << 8) + b[2]) << 8) + b[3];
}

std::uint16_t Uint16(const std::uint8_t* b) {  // png_get_uint_16, inlined
    return static_cast<std::uint16_t>((b[0] << 8) + b[1]);
}

bool HasInfo(const PngInfo* info, std::uint32_t valid) {
    return info != nullptr && (info->valid & valid) != 0;
}

// pHYs and oFFs: two 32-bit values and a unit byte.
void HandleTwoValues(PngStruct* png, PngInfo* info, std::uint32_t length, std::uint32_t valid,
                     const char* missingIhdr, const char* afterIdat, const char* duplicate, const char* badLength,
                     std::uintptr_t setter) {
    if ((png->mode & kPngHaveIhdr) == 0) {
        Error(png, missingIhdr);
    } else if ((png->mode & kPngHaveIdat) != 0) {
        SkipChunk(png, length, afterIdat);
        return;
    } else if (HasInfo(info, valid)) {
        SkipChunk(png, length, duplicate);
        return;
    }
    if (length != kPhysLength) {
        SkipChunk(png, length, badLength);
        return;
    }
    std::uint8_t buf[kPhysLength];
    CrcRead(png, buf, kPhysLength);
    if (CrcFinish(png, 0)) {
        return;
    }
    calls::At<SetPhysFn>(setter)(png, info, Uint32(buf), Uint32(buf + 4), buf[8]);
}

}  // namespace

// Purpose: Reads the image header (size, bit depth, colour type, interlacing).
void __cdecl png_handle_IHDR(PngStruct* png, PngInfo* info, std::uint32_t length) {
    namespace t = game::texts;
    if (png->mode != 0) {  // PNG_BEFORE_IHDR
        Error(png, t::kPngOutOfPlaceIhdr);
    }
    if (length != kIhdrLength) {
        Error(png, t::kPngInvalidIhdrChunk);
    }
    png->mode |= kPngHaveIhdr;

    std::uint8_t buf[kIhdrLength];
    CrcRead(png, buf, kIhdrLength);
    CrcFinish(png, 0);

    const std::uint32_t width = Uint32(buf);
    const std::uint32_t height = Uint32(buf + 4);
    const std::int32_t bitDepth = buf[8];
    const std::int32_t colorType = buf[9];
    const std::int32_t compression = buf[10];
    const std::int32_t filter = buf[11];
    const std::int32_t interlace = buf[12];

    if (width == 0 || width > kMaxDimension || height == 0 || height > kMaxDimension) {
        Error(png, t::kPngInvalidImageSize);
    }
    if (bitDepth != 1 && bitDepth != 2 && bitDepth != 4 && bitDepth != 8 && bitDepth != 16) {
        Error(png, t::kPngInvalidBitDepth);
    }
    if (colorType == 1 || colorType == 5 || colorType > 6) {
        Error(png, t::kPngInvalidColorType);
    }
    // (Unlike later libpng versions, 16-bit palette images pass.)
    if ((colorType == static_cast<std::int32_t>(kPngColorTypeRgb) ||
         colorType == static_cast<std::int32_t>(kPngColorTypeGrayAlpha) ||
         colorType == static_cast<std::int32_t>(kPngColorTypeRgbAlpha)) &&
        bitDepth < 8) {
        Error(png, t::kPngInvalidColorDepth);
    }
    if (interlace >= static_cast<std::int32_t>(kInterlaceLast)) {
        Error(png, t::kPngUnknownInterlace);
    }
    if (compression != 0) {
        Error(png, t::kPngUnknownCompression);
    }
    if (filter != 0) {
        Error(png, t::kPngUnknownFilter);
    }

    png->width = width;
    png->height = height;
    png->bit_depth = static_cast<std::uint8_t>(bitDepth);
    png->interlaced = static_cast<std::uint8_t>(interlace);
    png->color_type = static_cast<std::uint8_t>(colorType);
    switch (png->color_type) {
        case kPngColorTypeGray:
        case kPngColorTypePalette: png->channels = 1; break;
        case kPngColorTypeRgb: png->channels = 3; break;
        case kPngColorTypeGrayAlpha: png->channels = 2; break;
        case kPngColorTypeRgbAlpha: png->channels = 4; break;
        default: break;  // (only after png_error)
    }
    png->pixel_depth = static_cast<std::uint8_t>(png->bit_depth * png->channels);
    png->rowbytes = (png->width * png->pixel_depth + 7) >> 3;
    calls::At<SetIhdrFn>(kPngSetIhdr)(png, info, width, height, bitDepth, colorType, interlace, compression, filter);
}

// Purpose: The end of the PNG file.
void __cdecl png_handle_IEND(PngStruct* png, PngInfo* info, std::uint32_t length) {
    if ((png->mode & kPngHaveIhdr) == 0 || (png->mode & kPngHaveIdat) == 0) {
        Error(png, game::texts::kPngNoImageInFile);
        if (info == nullptr) {  // (quiets a compiler warning in libpng)
            return;
        }
    }
    png->mode |= kPngAfterIdat | kPngHaveIend;
    if (length != 0) {
        Warning(png, game::texts::kPngIncorrectIendLength);
    }
    CrcFinish(png, length);
}

// Purpose: The significant bits per channel.
void __cdecl png_handle_sBIT(PngStruct* png, PngInfo* info, std::uint32_t length) {
    namespace t = game::texts;
    std::uint8_t buf[4] = {0, 0, 0, 0};
    if ((png->mode & kPngHaveIhdr) == 0) {
        Error(png, t::kPngSbitMissingIhdr);
    } else if ((png->mode & kPngHaveIdat) != 0) {
        SkipChunk(png, length, t::kPngSbitAfterIdat);
        return;
    } else if ((png->mode & kPngHavePlte) != 0) {
        Warning(png, t::kPngSbitOutOfPlace);  // should be an error, but can be coped with
    } else if (HasInfo(info, kPngInfoSbit)) {
        SkipChunk(png, length, t::kPngSbitDuplicate);
        return;
    }
    const std::uint32_t trueLength = png->color_type == kPngColorTypePalette ? 3 : png->channels;
    if (length != trueLength) {
        SkipChunk(png, length, t::kPngSbitLength);
        return;
    }
    CrcRead(png, buf, trueLength);
    if (CrcFinish(png, 0)) {
        return;
    }
    if ((png->color_type & kPngColorMaskColor) != 0) {
        png->sig_bit.red = buf[0];
        png->sig_bit.green = buf[1];
        png->sig_bit.blue = buf[2];
        png->sig_bit.alpha = buf[3];
    } else {
        png->sig_bit.gray = buf[0];
        png->sig_bit.alpha = buf[1];
    }
    calls::At<SetColor8Fn>(kPngSetSbit)(png, info, &png->sig_bit);
}

// Purpose: Transparency: alpha values of palette entries, or the one
// transparent grey / RGB colour.
void __cdecl png_handle_tRNS(PngStruct* png, PngInfo* info, std::uint32_t length) {
    namespace t = game::texts;
    if ((png->mode & kPngHaveIhdr) == 0) {
        Error(png, t::kPngTrnsMissingIhdr);
    } else if ((png->mode & kPngHaveIdat) != 0) {
        SkipChunk(png, length, t::kPngTrnsAfterIdat);
        return;
    } else if (HasInfo(info, kPngInfoTrns)) {
        SkipChunk(png, length, t::kPngTrnsDuplicate);
        return;
    }

    if (png->color_type == kPngColorTypePalette) {
        if ((png->mode & kPngHavePlte) == 0) {
            Warning(png, t::kPngTrnsMissingPlte);  // should be an error, but can be coped with
        } else if (length > png->num_palette) {
            SkipChunk(png, length, t::kPngTrnsLength);
            return;
        }
        if (length == 0) {
            SkipChunk(png, length, t::kPngTrnsZeroLength);
            return;
        }
        png->trans = static_cast<std::uint8_t*>(calls::At<calls::PngMallocFn>(calls::kPngMalloc)(png, length));
        png->flags |= kPngFlagFreeTrans;
        CrcRead(png, png->trans, length);
        png->num_trans = static_cast<std::uint16_t>(length);
    } else if (png->color_type == kPngColorTypeRgb) {
        std::uint8_t buf[6];
        if (length != 6) {
            SkipChunk(png, length, t::kPngTrnsLength);
            return;
        }
        CrcRead(png, buf, length);
        png->num_trans = 1;
        png->trans_values.red = Uint16(buf);
        png->trans_values.green = Uint16(buf + 2);
        png->trans_values.blue = Uint16(buf + 4);
    } else if (png->color_type == kPngColorTypeGray) {
        std::uint8_t buf[6];
        if (length != 2) {
            SkipChunk(png, length, t::kPngTrnsLength);
            return;
        }
        CrcRead(png, buf, 2);
        png->num_trans = 1;
        png->trans_values.gray = Uint16(buf);
    } else {
        SkipChunk(png, length, t::kPngTrnsWithAlpha);
        return;
    }

    if (CrcFinish(png, 0)) {
        return;
    }
    calls::At<SetTrnsFn>(kPngSetTrns)(png, info, png->trans, png->num_trans, &png->trans_values);
}

// Purpose: The suggested background colour.
void __cdecl png_handle_bKGD(PngStruct* png, PngInfo* info, std::uint32_t length) {
    namespace t = game::texts;
    if ((png->mode & kPngHaveIhdr) == 0) {
        Error(png, t::kPngBkgdMissingIhdr);
    } else if ((png->mode & kPngHaveIdat) != 0) {
        SkipChunk(png, length, t::kPngBkgdAfterIdat);
        return;
    } else if (png->color_type == kPngColorTypePalette && (png->mode & kPngHavePlte) == 0) {
        SkipChunk(png, length, t::kPngBkgdMissingPlte);
        return;
    } else if (HasInfo(info, kPngInfoBkgd)) {
        SkipChunk(png, length, t::kPngBkgdDuplicate);
        return;
    }
    std::uint32_t trueLength;
    if (png->color_type == kPngColorTypePalette) {
        trueLength = 1;
    } else if ((png->color_type & kPngColorMaskColor) != 0) {
        trueLength = 6;
    } else {
        trueLength = 2;
    }
    if (length != trueLength) {
        SkipChunk(png, length, t::kPngBkgdLength);
        return;
    }
    std::uint8_t buf[6];
    CrcRead(png, buf, trueLength);
    if (CrcFinish(png, 0)) {
        return;
    }
    PngColor16& bg = png->background;
    if (png->color_type == kPngColorTypePalette) {
        bg.index = buf[0];
        const std::uint8_t* entry = static_cast<const std::uint8_t*>(png->palette) + 3 * buf[0];  // (no range check)
        bg.red = entry[0];
        bg.green = entry[1];
        bg.blue = entry[2];
    } else if ((png->color_type & kPngColorMaskColor) == 0) {
        bg.red = bg.green = bg.blue = bg.gray = Uint16(buf);
    } else {
        bg.red = Uint16(buf);
        bg.green = Uint16(buf + 2);
        bg.blue = Uint16(buf + 4);
    }
    calls::At<SetColor16Fn>(kPngSetBkgd)(png, info, &png->background);
}

// Purpose: The palette histogram (how often each entry is used).
void __cdecl png_handle_hIST(PngStruct* png, PngInfo* info, std::uint32_t length) {
    namespace t = game::texts;
    if ((png->mode & kPngHaveIhdr) == 0) {
        Error(png, t::kPngHistMissingIhdr);
    } else if ((png->mode & kPngHaveIdat) != 0) {
        SkipChunk(png, length, t::kPngHistAfterIdat);
        return;
    } else if ((png->mode & kPngHavePlte) == 0) {
        SkipChunk(png, length, t::kPngHistMissingPlte);
        return;
    } else if (HasInfo(info, kPngInfoHist)) {
        SkipChunk(png, length, t::kPngHistDuplicate);
        return;
    }
    if (length != 2u * png->num_palette) {
        SkipChunk(png, length, t::kPngHistLength);
        return;
    }
    const std::int32_t count = static_cast<std::int32_t>(length) / 2;
    png->hist = static_cast<std::uint16_t*>(
        calls::At<calls::PngMallocFn>(calls::kPngMalloc)(png, static_cast<std::uint32_t>(count * 2)));
    png->flags |= kPngFlagFreeHist;
    for (std::int32_t i = 0; i < count; ++i) {
        std::uint8_t buf[2];
        CrcRead(png, buf, 2);
        png->hist[i] = Uint16(buf);
    }
    if (CrcFinish(png, 0)) {
        return;
    }
    calls::At<SetHistFn>(kPngSetHist)(png, info, png->hist);
}

// Purpose: The physical pixel size (pixels per unit).
void __cdecl png_handle_pHYs(PngStruct* png, PngInfo* info, std::uint32_t length) {
    namespace t = game::texts;
    HandleTwoValues(png, info, length, kPngInfoPhys, t::kPngPhysMissingIhdr, t::kPngPhysAfterIdat,
                    t::kPngPhysDuplicate, t::kPngPhysLength, kPngSetPhys);
}

// Purpose: The image's offset on a page.
void __cdecl png_handle_oFFs(PngStruct* png, PngInfo* info, std::uint32_t length) {
    namespace t = game::texts;
    HandleTwoValues(png, info, length, kPngInfoOffs, t::kPngOffsMissingIhdr, t::kPngOffsAfterIdat,
                    t::kPngOffsDuplicate, t::kPngOffsLength, kPngSetOffs);
}

// Purpose: The last modification time.
void __cdecl png_handle_tIME(PngStruct* png, PngInfo* info, std::uint32_t length) {
    namespace t = game::texts;
    if ((png->mode & kPngHaveIhdr) == 0) {
        Error(png, t::kPngTimeOutOfPlace);
    } else if (HasInfo(info, kPngInfoTime)) {
        SkipChunk(png, length, t::kPngTimeDuplicate);
        return;
    }
    if ((png->mode & kPngHaveIdat) != 0) {
        png->mode |= kPngAfterIdat;
    }
    if (length != kTimeLength) {
        SkipChunk(png, length, t::kPngTimeLength);
        return;
    }
    std::uint8_t buf[kTimeLength];
    CrcRead(png, buf, kTimeLength);
    if (CrcFinish(png, 0)) {
        return;
    }
#pragma pack(push, 1)
    struct PngTime {
        std::uint16_t year;
        std::uint8_t month, day, hour, minute, second;
        std::uint8_t pad;  // garbage in the original
    } modTime{};
#pragma pack(pop)
    modTime.second = buf[6];
    modTime.minute = buf[5];
    modTime.hour = buf[4];
    modTime.day = buf[3];
    modTime.month = buf[2];
    modTime.year = Uint16(buf);
    calls::At<SetTimeFn>(kPngSetTime)(png, info, &modTime);
}

// Purpose: The palette.
void __cdecl png_handle_PLTE(PngStruct* png, PngInfo* info, std::uint32_t length) {
    namespace t = game::texts;
    if ((png->mode & kPngHaveIhdr) == 0) {
        Error(png, t::kPngPlteMissingIhdr);
    } else if ((png->mode & kPngHaveIdat) != 0) {
        SkipChunk(png, length, t::kPngPlteAfterIdat);
        return;
    } else if ((png->mode & kPngHavePlte) != 0) {
        Error(png, t::kPngPlteDuplicate);
    }
    png->mode |= kPngHavePlte;
    if (length % 3 != 0) {
        if (png->color_type != kPngColorTypePalette) {
            SkipChunk(png, length, t::kPngInvalidPalette);
            return;
        }
        Error(png, t::kPngInvalidPalette);
    }
    const std::int32_t count = static_cast<std::int32_t>(length) / 3;
    auto* const palette = static_cast<std::uint8_t*>(
        calls::At<ZallocFn>(kPngZalloc)(png, static_cast<std::uint32_t>(count), 3));
    png->flags |= kPngFlagFreePalette;
    for (std::int32_t i = 0; i < count; ++i) {
        std::uint8_t buf[3];
        CrcRead(png, buf, 3);
        palette[3 * i] = buf[0];
        palette[3 * i + 1] = buf[1];
        palette[3 * i + 2] = buf[2];
    }
    CrcFinish(png, 0);  // (a CRC error that only warns still uses the palette)
    png->palette = palette;
    png->num_palette = static_cast<std::uint16_t>(count);
    calls::At<SetPlteFn>(kPngSetPlte)(png, info, palette, count);
    if (png->color_type == kPngColorTypePalette && HasInfo(info, kPngInfoTrns) && png->num_trans > png->num_palette) {
        Warning(png, t::kPngTruncatingTrns);
        png->num_trans = png->num_palette;
    }
}

// Purpose: The image's gamma.
void __cdecl png_handle_gAMA(PngStruct* png, PngInfo* info, std::uint32_t length) {
    namespace t = game::texts;
    if ((png->mode & kPngHaveIhdr) == 0) {
        Error(png, t::kPngGamaMissingIhdr);
    } else if ((png->mode & kPngHaveIdat) != 0) {
        SkipChunk(png, length, t::kPngGamaAfterIdat);
        return;
    } else if ((png->mode & kPngHavePlte) != 0) {
        Warning(png, t::kPngGamaOutOfPlace);  // should be an error, but can be coped with
    } else if (HasInfo(info, kPngInfoGama) && (info->valid & kPngInfoSrgb) == 0) {
        SkipChunk(png, length, t::kPngGamaDuplicate);
        return;
    }
    if (length != 4) {
        SkipChunk(png, length, t::kPngGamaLength);
        return;
    }
    std::uint8_t buf[4];
    CrcRead(png, buf, 4);
    if (CrcFinish(png, 0)) {
        return;
    }
    const std::uint32_t igamma = Uint32(buf);
    if (igamma == 0) {
        return;
    }
    if ((info->valid & kPngInfoSrgb) != 0 && igamma != kSrgbGamma) {  // (info not checked for null)
        Warning(png, t::kPngGamaWithSrgb);
        calls::At<calls::FprintfFn>(calls::kFprintf)(&game::g_crtStderr, t::kPngGamaPrint, igamma);
        return;
    }
    const double fileGamma = static_cast<double>(igamma) * static_cast<double>(game::g_pngGammaScale.get());
    png->gamma = static_cast<float>(fileGamma);
    calls::At<SetGamaFn>(kPngSetGama)(png, info, fileGamma);
}

// Purpose: A text chunk (keyword and text).
void __cdecl png_handle_tEXt(PngStruct* png, PngInfo* info, std::uint32_t length) {
    if ((png->mode & kPngHaveIhdr) == 0) {
        Error(png, game::texts::kPngTextMissingIhdr);
    }
    if ((png->mode & kPngHaveIdat) != 0) {
        png->mode |= kPngAfterIdat;
    }
    const auto malloc = calls::At<calls::PngMallocFn>(calls::kPngMalloc);
    const auto free = calls::At<calls::PngFreeFn>(calls::kPngFree);
    auto* const key = static_cast<char*>(malloc(png, length + 1));
    CrcRead(png, reinterpret_cast<std::uint8_t*>(key), length);
    if (CrcFinish(png, 0)) {
        free(png, key);
        return;
    }
    key[length] = 0;
    char* text = key;
    while (*text != 0) {  // the end of the keyword
        ++text;
    }
    if (text != key + length) {
        ++text;
    }
    auto* const entry = static_cast<PngText*>(malloc(png, sizeof(PngText)));
    entry->compression = kPngTextCompressionNone;
    entry->key = key;
    entry->text = text;
    calls::At<SetTextFn>(kPngSetText)(png, info, entry, 1);
    free(png, entry);
}

namespace {

bool NotALetter(std::uint8_t c) {  // libpng's isnonalpha: also the six bytes between Z and a pass
    return c < 41 || c > 122 || (c > 90 && c < 97);
}

}  // namespace

// Purpose: A chunk the decoder doesn't know: skipped if ancillary.
void __cdecl png_handle_unknown(PngStruct* png, PngInfo* info, std::uint32_t length) {
    const std::uint8_t* name = png->chunk_name;
    if (NotALetter(name[0]) || NotALetter(name[1]) || NotALetter(name[2]) || NotALetter(name[3])) {
        calls::At<calls::PngMessageFn>(calls::kPngChunkError)(png, game::texts::kPngInvalidChunkType);
    }
    if ((name[0] & kPngChunkAncillary) == 0) {
        calls::At<calls::PngMessageFn>(calls::kPngChunkError)(png, game::texts::kPngUnknownCriticalChunk);
        if (info == nullptr) {  // (quiets a compiler warning in libpng)
            return;
        }
    }
    if ((png->mode & kPngHaveIdat) != 0) {
        png->mode |= kPngAfterIdat;
    }
    CrcFinish(png, length);
}

// png_set.cpp
void __cdecl png_set_text(PngStruct* png, PngInfo* info, PngText* text, std::int32_t count);
extern SetTextFn Original_png_set_text;

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Each case: a png_struct / png_info in a consistent-enough state (mode,
// colour type and depth, palette size, CRC flags, the chunk name or its
// twin with the ancillary bit flipped, png->crc = the CRC of the name as
// png_read_info leaves it), the chunk's data made for the handler (sometimes
// of another length or with bad values) with a right or wrong CRC, read
// through a test read callback. malloc and free (0x621784 / 0x621b7e) jump
// to an arena allocator that logs its calls, so both runs get the same
// blocks, fprintf (0x6214d6, gAMA's stderr line) to a logger; png_error's
// callback throws, warnings are logged. Compared:
// png_struct, png_info (but mod_time[7], see above), the palette, the arena,
// the log and how far the stream was read.

namespace {

struct ChunkErrorThrown {};

constexpr std::size_t kChunkArena = 0x10000;

struct ChunkWorld {
    PngStruct png;
    PngInfo info;
    std::uint8_t palette[768];
    std::uint8_t zbuf[64];
    std::uint8_t arena[kChunkArena];
};

ChunkWorld* g_chunkWorld = nullptr;
const std::vector<std::uint8_t>* g_chunkStream = nullptr;
std::size_t g_chunkPos = 0;
std::size_t g_chunkUsed = 0;
std::vector<std::uint32_t>* g_chunkLog = nullptr;

std::uint32_t MessageTag(const char* message) {
    return static_cast<std::uint8_t>(message[0]) * 256u + static_cast<std::uint8_t>(message[1]);
}

void __cdecl ChunkRead(PngStruct* /*png*/, std::uint8_t* data, std::uint32_t length) {
    g_chunkLog->push_back(0x4ead0000u + length);
    for (std::uint32_t i = 0; i < length; ++i, ++g_chunkPos) {
        data[i] = g_chunkPos < g_chunkStream->size() ? (*g_chunkStream)[g_chunkPos] : 0;
    }
}

void __cdecl ChunkError(PngStruct* /*png*/, const char* message) {
    g_chunkLog->push_back(0xe0000000u + MessageTag(message));
    throw ChunkErrorThrown{};
}

void __cdecl ChunkWarning(PngStruct* /*png*/, const char* message) {
    g_chunkLog->push_back(0xf0000000u + MessageTag(message));
}

void* __cdecl ChunkMalloc(std::size_t size) {
    g_chunkLog->push_back(0xa1100000u + static_cast<std::uint32_t>(size));
    if (g_chunkUsed + size > kChunkArena) {
        return nullptr;
    }
    void* block = g_chunkWorld->arena + g_chunkUsed;
    g_chunkUsed += (size + 15) & ~std::size_t{15};
    return block;
}

// gAMA's fprintf(stderr, "igamma = %lu...", igamma): logged.
int __cdecl ChunkFprintf(void* /*file*/, const char* format, std::uint32_t value) {
    g_chunkLog->push_back(0xf9000000u + MessageTag(format));
    g_chunkLog->push_back(value);
    return 0;
}

void __cdecl ChunkFree(void* block) {
    g_chunkLog->push_back(0xf7ee0000u +
                          static_cast<std::uint32_t>(static_cast<std::uint8_t*>(block) - g_chunkWorld->arena));
}

std::uint32_t Crc32Of(const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0) {
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
        }
    }
    return ~crc;
}

std::vector<std::uint8_t> RandomBytes(test::Rng& rng, std::size_t n) {
    std::vector<std::uint8_t> v(n);
    if (n != 0) {
        rng.Fill(v.data(), n);
    }
    return v;
}

void PutUint32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int k = 3; k >= 0; --k) {
        out.push_back(static_cast<std::uint8_t>(value >> (8 * k)));
    }
}

std::vector<std::uint8_t> HandlerData(test::Rng& rng, int handler, std::uint8_t ct, std::uint8_t channels,
                                      std::uint16_t numPalette) {
    switch (handler) {
        case 0: {  // IHDR: valid, now and then one field wrong
            static const std::uint8_t kTypes[] = {0, 2, 3, 4, 6};
            static const std::uint8_t kDepths[] = {1, 2, 4, 8, 16};
            const std::uint8_t type = kTypes[rng.Below(5)];
            const std::uint8_t depth = type == 0 || type == 3 ? kDepths[rng.Below(5)] : (rng.Chance(1, 2) ? 8 : 16);
            std::uint32_t width = 1 + rng.Below(1000), height = 1 + rng.Below(1000);
            std::uint8_t fields[5] = {depth, type, 0, 0, static_cast<std::uint8_t>(rng.Below(2))};
            if (rng.Chance(2, 5)) {
                switch (rng.Below(6)) {
                    case 0: width = rng.Chance(1, 2) ? 0 : 0x80000000u + rng.Below(100); break;
                    case 1: height = rng.Chance(1, 2) ? 0 : 0xffffffffu; break;
                    case 2: fields[0] = static_cast<std::uint8_t>(rng.Below(20)); break;
                    case 3: fields[1] = static_cast<std::uint8_t>(rng.Below(9)); break;
                    default: fields[2 + rng.Below(3)] = static_cast<std::uint8_t>(1 + rng.Below(255)); break;
                }
            }
            std::vector<std::uint8_t> d;
            PutUint32(d, width);
            PutUint32(d, height);
            d.insert(d.end(), fields, fields + 5);
            return d;
        }
        case 1: return rng.Chance(4, 5) ? std::vector<std::uint8_t>{} : RandomBytes(rng, 1 + rng.Below(30));  // IEND
        case 2: return RandomBytes(rng, ct == 3 ? 3 : channels);                                              // sBIT
        case 3:                                                                                                // tRNS
            if (ct == 3) {
                return RandomBytes(rng, rng.Chance(1, 2) ? numPalette : rng.Below(300));
            }
            return RandomBytes(rng, ct == 2 ? 6 : 2);
        case 4: return RandomBytes(rng, ct == 3 ? 1 : (ct & 2) != 0 ? 6 : 2);  // bKGD
        case 5: return RandomBytes(rng, 2u * numPalette);                   // hIST
        case 6:
        case 7: return RandomBytes(rng, 9);  // pHYs, oFFs
        case 8: return RandomBytes(rng, 7);  // tIME
        case 9: {  // tEXt: keyword, 0, text (sometimes no 0, or nothing)
            std::vector<std::uint8_t> d;
            for (std::uint32_t k = rng.Below(12); k-- > 0;) {
                d.push_back(static_cast<std::uint8_t>(0x61 + rng.Below(26)));
            }
            if (!rng.Chance(1, 6)) {
                d.push_back(0);
            }
            for (std::uint32_t k = rng.Below(rng.Chance(1, 2) ? 3 : 60); k-- > 0;) {
                d.push_back(static_cast<std::uint8_t>(rng.Chance(1, 20) ? 0 : 32 + rng.Below(90)));
            }
            return d;
        }
        case 11: return RandomBytes(rng, rng.Chance(4, 5) ? 3 * rng.Below(260) : rng.Below(40));  // PLTE
        case 12: {  // gAMA: sRGB's value, 0, or anything
            const std::uint32_t g = rng.Chance(1, 4) ? 45000 : rng.Chance(1, 8) ? 0 : rng.Next32() >> rng.Below(32);
            std::vector<std::uint8_t> d;
            PutUint32(d, g);
            return d;
        }
        default: return RandomBytes(rng, rng.Below(40));  // unknown chunks
    }
}

// png_set_text's arguments: 0..3 entries in the arena, keys and texts
// (some null or empty) after them.
void MakeTextEntries(test::Rng& rng, std::uint8_t* arena, std::int32_t& count) {
    constexpr std::size_t kEntries = 0xc000, kStrings = 0xd000;
    count = static_cast<std::int32_t>(rng.Below(4));
    auto* entries = reinterpret_cast<PngText*>(arena + kEntries);
    for (std::int32_t k = 0; k < count; ++k) {
        char* str = reinterpret_cast<char*>(arena + kStrings + 0x100 * k);
        str[rng.Below(40)] = 0;
        entries[k].compression = static_cast<std::int32_t>(rng.Below(3)) - 1;
        entries[k].key = str + 0x80;
        entries[k].text = rng.Chance(1, 5) ? nullptr : rng.Chance(1, 5) ? str + rng.Below(40) : str;
        if (entries[k].text != nullptr && rng.Chance(1, 4)) {
            entries[k].text[0] = 0;
        }
    }
}

// A text array already in the info structure (or none).
void MakeInfoText(test::Rng& rng, PngInfo& info, std::uint8_t* arena) {
    constexpr std::size_t kOldText = 0x8000;
    info.num_text = static_cast<std::int32_t>(rng.Below(4));
    if (rng.Chance(1, 3)) {
        info.text = nullptr;  // (with room left, png_set_text would write through the null pointer)
        info.max_text = rng.Chance(1, 2) ? 0 : static_cast<std::int32_t>(rng.Below(8)) - 8;
    } else {
        info.text = arena + kOldText;
        info.max_text = info.num_text + static_cast<std::int32_t>(rng.Below(3));
    }
}

constexpr int kHandlerCount = 13;

void CompareHandlers(test::Context& ctx, const ChunkHandlerFn* candidates, SetTextFn setText, int cases) {
    static const char* const kNames[] = {"IHDR", "IEND", "sBIT", "tRNS", "bKGD", "hIST", "pHYs",
                                         "oFFs", "tIME", "tEXt", "abCd", "PLTE", "gAMA", "text"};
    const ChunkHandlerFn originals[] = {Original_png_handle_IHDR, Original_png_handle_IEND, Original_png_handle_sBIT,
                                        Original_png_handle_tRNS, Original_png_handle_bKGD, Original_png_handle_hIST,
                                        Original_png_handle_pHYs, Original_png_handle_oFFs, Original_png_handle_tIME,
                                        Original_png_handle_tEXt, Original_png_handle_unknown,
                                        Original_png_handle_PLTE, Original_png_handle_gAMA};
    auto a = std::make_unique<ChunkWorld>();
    auto initial = std::make_unique<ChunkWorld>();
    auto results = std::make_unique<ChunkWorld>();
    test::ScopedJump mallocJump(calls::kMalloc, reinterpret_cast<const void*>(&ChunkMalloc));
    test::ScopedJump freeJump(calls::kFree, reinterpret_cast<const void*>(&ChunkFree));
    test::ScopedJump fprintfJump(calls::kFprintf, reinterpret_cast<const void*>(&ChunkFprintf));
    for (int i = 0; i < cases; ++i) {
        test::Rng& rng = ctx.rng();
        const int handler = static_cast<int>(rng.Below(kHandlerCount + 1));  // the last: png_set_text itself
        rng.Fill(a.get(), sizeof(ChunkWorld));
        PngStruct& png = a->png;
        static const std::uint8_t kTypes[] = {0, 2, 3, 4, 6, 0, 3};
        static const std::uint8_t kChannels[] = {1, 1, 3, 1, 2, 1, 4, 1};
        static const std::uint32_t kModes[] = {0, 1, 3, 5, 7, 1, 3, 1};
        static const std::uint16_t kPalettes[] = {0, 1, 16, 256};
        static const std::uint32_t kFlags[] = {0, 0x100, 0x200, 0x300, 0x400, 0x800, 0xc00, 0xf00};
        const std::uint8_t ct = rng.Chance(1, 10) ? static_cast<std::uint8_t>(rng.Below(8)) : kTypes[rng.Below(7)];
        const std::uint8_t channels = kChannels[ct];
        png.mode = rng.Chance(1, 8) ? rng.Below(32) : kModes[rng.Below(8)];
        png.color_type = ct;
        png.channels = channels;
        png.bit_depth = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 8 : 1 + rng.Below(16));
        png.num_palette = rng.Chance(1, 4) ? static_cast<std::uint16_t>(rng.Below(300)) : kPalettes[rng.Below(4)];
        png.palette = a->palette;
        png.zbuf = a->zbuf;
        png.zbuf_size = 1 + rng.Below(64);
        png.flags = (png.flags & ~0xf00u) | kFlags[rng.Below(8)];
        png.error_fn = reinterpret_cast<void*>(&ChunkError);
        png.warning_fn = reinterpret_cast<void*>(&ChunkWarning);
        png.read_data_fn = reinterpret_cast<void*>(&ChunkRead);
        std::memcpy(png.chunk_name, kNames[handler], 4);
        if (handler == 10) {  // unknown: letters mostly, the ancillary bit either way
            for (int k = 0; k < 4; ++k) {
                png.chunk_name[k] = static_cast<std::uint8_t>(
                    rng.Chance(1, 10) ? rng.Below(256) : (rng.Chance(1, 2) ? 0x61 : 0x41) + rng.Below(26));
            }
        }
        if (rng.Chance(1, 3)) {
            png.chunk_name[0] ^= kPngChunkAncillary;
        }
        MakeInfoText(rng, a->info, a->arena);
        std::int32_t textCount = 0;
        MakeTextEntries(rng, a->arena, textCount);
        const bool noPng = rng.Chance(1, 20);
        png.crc = Crc32Of(png.chunk_name, 4);
        a->info.valid = rng.Chance(3, 10) ? rng.Next32() : 0;

        const std::vector<std::uint8_t> data = HandlerData(rng, handler, ct, channels, png.num_palette);
        std::uint32_t length = static_cast<std::uint32_t>(data.size());
        if (rng.Chance(1, 5)) {
            length = rng.Chance(1, 2) ? rng.Below(40) : length + 1;
        }
        std::vector<std::uint8_t> stream(data);
        stream.resize(length);
        if (rng.Chance(1, 2)) {
            PutUint32(stream, Crc32Of(stream.data(), stream.size(), png.crc));
        }
        const std::vector<std::uint8_t> tail = RandomBytes(rng, 8);
        stream.insert(stream.end(), tail.begin(), tail.end());
        const bool noInfo = handler != 12 && rng.Chance(1, 20);  // (gAMA would crash on a null info)

        *initial = *a;
        std::vector<std::uint32_t> logs[2];
        std::size_t positions[2] = {};
        for (int run = 0; run < 2; ++run) {
            if (run == 1) {
                *results = *a;
                *a = *initial;
            }
            g_chunkWorld = a.get();
            g_chunkStream = &stream;
            g_chunkPos = 0;
            g_chunkUsed = 0;
            g_chunkLog = &logs[run];
            try {
                PngInfo* const info = noInfo ? nullptr : &a->info;
                if (handler == kHandlerCount) {
                    auto* const entries = reinterpret_cast<PngText*>(a->arena + 0xc000);
                    (run == 0 ? Original_png_set_text : setText)(noPng ? nullptr : &a->png, info, entries, textCount);
                } else {
                    (run == 0 ? originals[handler] : candidates[handler])(&a->png, info, length);
                }
            } catch (const ChunkErrorThrown&) {
                logs[run].push_back(0xdead);
            }
            positions[run] = g_chunkPos;
        }
        results->info.mod_time[7] = a->info.mod_time[7];  // stack garbage in the original
        bool same = S3RE_EXPECT(logs[0] == logs[1]);
        same &= S3RE_EXPECT_EQ(positions[0], positions[1]);
        same &= S3RE_EXPECT_MEM_EQ(results.get(), a.get(), sizeof(ChunkWorld));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, handler %s, length %u", i, kNames[handler], length);
            break;
        }
    }
    g_chunkWorld = nullptr;
    g_chunkLog = nullptr;
}

const ChunkHandlerFn kOurHandlers[] = {&png_handle_IHDR, &png_handle_IEND, &png_handle_sBIT, &png_handle_tRNS,
                                       &png_handle_bKGD, &png_handle_hIST, &png_handle_pHYs, &png_handle_oFFs,
                                       &png_handle_tIME, &png_handle_tEXt, &png_handle_unknown,
                                       &png_handle_PLTE, &png_handle_gAMA};

// Wrong: tIME stores the month as the day too.
void __cdecl WrongTime(PngStruct* png, PngInfo* info, std::uint32_t length) {
    png_handle_tIME(png, info, length);
    if (info != nullptr && (info->valid & kPngInfoTime) != 0) {
        info->mod_time[3] = info->mod_time[2];
    }
}

}  // namespace

S3RE_TEST(PngChunkHandlers_MatchOriginal) {
    CompareHandlers(ctx, kOurHandlers, &png_set_text, 8000);
}

S3RE_TEST(PngChunkHandlers_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ChunkHandlerFn wrong[kHandlerCount];
    std::memcpy(wrong, kOurHandlers, sizeof(wrong));
    wrong[8] = &WrongTime;
    CompareHandlers(probe, wrong, &png_set_text, 8000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::thirdparty
