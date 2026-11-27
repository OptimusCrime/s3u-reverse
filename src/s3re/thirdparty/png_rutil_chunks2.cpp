// libpng 1.0.2's remaining chunk handlers (pngrutil.c) and png_set_pCAL
// (pngset.c), statically linked into S3.EXE (re/picture_code.md). Like the
// handlers in png_rutil_chunks.cpp: cdecl (png_ptr, info_ptr, length), the
// CRC checks (png_crc_read / png_crc_finish, inlined in the original) called.
//
// png_handle_cHRM (0x005abd00): the white point and primaries, eight values
//   of 100000 times the chromaticity. Each (x, y) pair is checked as soon as
//   it is read (x, y >= 0, <= 0.8, x + y <= 1); a bad pair skips the rest.
//   With sRGB present, values more than 0.001 from sRGB's are only reported
//   (a warning and two lines on stderr), else png_set_cHRM.
// png_handle_sRGB (0x005ac920): the rendering intent (0..3); warns when an
//   earlier gAMA (png->gamma) isn't 0.45 or a cHRM isn't sRGB's, then
//   png_set_sRGB_gAMA_and_cHRM.
// png_handle_zTXt (0x005aeec0): keyword, compression byte and the deflated
//   text, inflated through png->zstream into png->zbuf pieces and gathered in
//   one png_malloc block (keyword, 0, text); a decoding error keeps what was
//   decoded and appends "Error decoding zTXt chunk"; an unknown method byte
//   puts that message in place of the text. png_set_text.
// png_handle_pCAL (0x005ae430): purpose, X0, X1, equation type, parameter
//   count, units and the parameter strings; png_set_pCAL.
// png_set_pCAL (0x005b1750): copies purpose, units and each parameter into
//   png_malloc blocks (the parameter array null-terminated) and sets the
//   pCAL bit (0x400).
//
// Quirks kept (all libpng 1.0.2's): cHRM's missing-IHDR message says "sBIT";
// cHRM's blue point is compared with the float 0.8f, the others with the
// double 0.8; cHRM and sRGB read info_ptr->valid without a null check once
// the data is valid (a crash in both); sRGB's gamma check reads png->gamma,
// not the info's; zTXt's message length is computed from pointers into
// different blocks (key + length - 1 - text), so the copy can be short or
// up to 27 bytes (one past the message's terminator); a deflate stream that
// runs out of input before producing anything leaves key and text near null
// and png_set_text crashes (not tested).

#include "game/globals.h"
#include "game/tables.h"
#include "game/texts.h"
#include "hooks.h"
#include "log.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "thirdparty/png_calls.h"
#include "thirdparty/png_struct.h"

#include <excpt.h>

#include <intrin.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace s3re::thirdparty {

using ChunkHandlerFn = void(__cdecl*)(PngStruct* png, PngInfo* info, std::uint32_t length);
using SetPcalFn = void(__cdecl*)(PngStruct* png, PngInfo* info, char* purpose, std::int32_t x0, std::int32_t x1,
                                 std::int32_t type, std::int32_t nparams, char* units, char** params);

void __cdecl png_handle_cHRM(PngStruct* png, PngInfo* info, std::uint32_t length);
void __cdecl png_handle_sRGB(PngStruct* png, PngInfo* info, std::uint32_t length);
void __cdecl png_handle_zTXt(PngStruct* png, PngInfo* info, std::uint32_t length);
void __cdecl png_handle_pCAL(PngStruct* png, PngInfo* info, std::uint32_t length);
void __cdecl png_set_pCAL(PngStruct* png, PngInfo* info, char* purpose, std::int32_t x0, std::int32_t x1,
                          std::int32_t type, std::int32_t nparams, char* units, char** params);

S3RE_HOOK(0x005abd00, png_handle_cHRM, ChunkHandlerFn, Original_png_handle_cHRM, png_handle_cHRM);
S3RE_HOOK(0x005ac920, png_handle_sRGB, ChunkHandlerFn, Original_png_handle_sRGB, png_handle_sRGB);
S3RE_HOOK(0x005aeec0, png_handle_zTXt, ChunkHandlerFn, Original_png_handle_zTXt, png_handle_zTXt);
S3RE_HOOK(0x005ae430, png_handle_pCAL, ChunkHandlerFn, Original_png_handle_pCAL, png_handle_pCAL);
S3RE_HOOK(0x005b1750, png_set_pCAL, SetPcalFn, Original_png_set_pCAL, png_set_pCAL);

namespace {

using CrcFinishFn = std::int32_t(__cdecl*)(PngStruct* png, std::uint32_t skip);
using SetChrmFn = void(__cdecl*)(PngStruct* png, PngInfo* info, double whiteX, double whiteY, double redX,
                                 double redY, double greenX, double greenY, double blueX, double blueY);
using SetSrgbFn = void(__cdecl*)(PngStruct* png, PngInfo* info, std::int32_t intent);
using SetTextFn = void(__cdecl*)(PngStruct* png, PngInfo* info, PngText* text, std::int32_t count);
using SprintfFn = int(__cdecl*)(char* buffer, const char* format, ...);
using StreamFn = std::int32_t(__cdecl*)(ZStream* z);
using InflateFn = std::int32_t(__cdecl*)(ZStream* z, std::int32_t flush);

constexpr std::uintptr_t kPngCrcFinish = 0x005aa930;
constexpr std::uintptr_t kPngCrcRead = 0x005aa900;
constexpr std::uintptr_t kPngSetChrm = 0x005b1590;
constexpr std::uintptr_t kPngSetSrgb = 0x005b1920;  // png_set_sRGB_gAMA_and_cHRM
constexpr std::uintptr_t kPngSetText = 0x005b19a0;
constexpr std::uintptr_t kPngSetPcal = 0x005b1750;
constexpr std::uintptr_t kSprintf = 0x0061f27e;
constexpr std::uintptr_t kInflate = 0x005aa4a0;
constexpr std::uintptr_t kInflateReset = 0x005aa270;
constexpr std::uint32_t kPngInfoPcal = 0x0400;
constexpr std::uint32_t kChrmLength = 32;
constexpr std::uint32_t kSrgbLength = 1;
constexpr std::int32_t kSrgbIntents = 4;
constexpr std::int32_t kSrgbGammaTimes100000 = 45000;
constexpr std::int32_t kZPartialFlush = 1;
constexpr std::int32_t kZOk = 0;
constexpr std::int32_t kZStreamEnd = 1;
constexpr std::uint32_t kZtxtMessageSize = 26;  // sizeof "Error decoding zTXt chunk"
constexpr std::uint32_t kPcalFixedBytes = 12;   // the purpose's 0, X0, X1, type, count, a byte of units
constexpr std::uint8_t kPcalEquationTypes = 4;  // linear, base-e, arbitrary base, hyperbolic

// The chromaticities of sRGB (float constants 0x65c5f0..0x65c5cc) and the
// tolerance (the double 0.001 at 0x65c5e8) of the cHRM / sRGB checks.
constexpr float kSrgbChromaticities[8] = {0.3127f, 0.329f, 0.64f, 0.33f, 0.3f, 0.6f, 0.15f, 0.06f};
constexpr double kChrmTolerance = 0.001;
constexpr double kChrmLimit = 0.8;          // 0x65c5f8 (double)
constexpr float kChrmBlueLimit = 0.8f;      // 0x65c5f4 (float)

void Error(PngStruct* png, const char* message) {
    calls::At<calls::PngMessageFn>(calls::kPngError)(png, message);
}

void Warning(PngStruct* png, const char* message) {
    calls::At<calls::PngMessageFn>(calls::kPngWarning)(png, message);
}

bool CrcFinish(PngStruct* png, std::uint32_t skip) {
    return calls::At<CrcFinishFn>(kPngCrcFinish)(png, skip) != 0;
}

void CrcRead(PngStruct* png, std::uint8_t* buffer, std::uint32_t length) {
    calls::At<calls::PngDataFn>(kPngCrcRead)(png, buffer, length);
}

void SkipChunk(PngStruct* png, std::uint32_t length, const char* message) {
    Warning(png, message);
    CrcFinish(png, length);
}

std::uint32_t Uint32(const std::uint8_t* b) {  // png_get_uint_32, inlined
    return (((((static_cast<std::uint32_t>(b[0]) << 8) + b[1]) << 8) + b[2]) << 8) + b[3];
}

bool HasInfo(const PngInfo* info, std::uint32_t valid) {
    return info != nullptr && (info->valid & valid) != 0;
}

// The CRT's _ftol (0x61fe98): truncation through a 64-bit integer, EAX the
// low dword (NaN and values outside the int64 range give 0).
std::int32_t Ftol(double value) {
    if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0)) {
        return 0;
    }
    return static_cast<std::int32_t>(static_cast<std::int64_t>(value));
}

// A chromaticity as cHRM reads it: fild of the 32-bit value times 1e-5f,
// rounded to double (x87 at 53 bits), stored as a float.
float ReadChromaticity(PngStruct* png) {
    std::uint8_t buf[4];
    CrcRead(png, buf, 4);
    return static_cast<float>(static_cast<double>(Uint32(buf)) * static_cast<double>(game::g_pngGammaScale.get()));
}

// (x, y) inside what cHRM accepts: both in [0, limit], x + y <= 1 (the sum
// rounded to double, as on the x87; NaN never occurs, the values are >= 0).
bool ValidPoint(float x, float y, double limit) {
    return !(x < 0.0f) && !(static_cast<double>(x) > limit) && !(y < 0.0f) && !(static_cast<double>(y) > limit) &&
           !(static_cast<double>(y) + static_cast<double>(x) > 1.0);
}

bool FarFromSrgb(float value, int k) {
    return std::fabs(static_cast<double>(value) - static_cast<double>(kSrgbChromaticities[k])) > kChrmTolerance;
}

// memcpy as MSVC 6 inlines it: rep movsd, then rep movsb.
void CopyBytes(void* dst, const void* src, std::uint32_t count) {
    __movsd(static_cast<unsigned long*>(dst), static_cast<const unsigned long*>(src), count / 4);
    __movsb(static_cast<unsigned char*>(dst) + (count & ~3u),
            static_cast<const unsigned char*>(src) + (count & ~3u), count % 4);
}

// A copy of a C string in a png_malloc block (strlen + 1 bytes), stored
// through `slot` before the copy (the order matters when a block aliases).
void CopyStringTo(PngStruct* png, char** slot, const char* text) {
    const std::uint32_t size = static_cast<std::uint32_t>(std::strlen(text)) + 1;
    *slot = static_cast<char*>(calls::At<calls::PngMallocFn>(calls::kPngMalloc)(png, size));
    CopyBytes(*slot, text, size);
}

}  // namespace

// Purpose: The chromaticities of the image's white point and primaries.
void __cdecl png_handle_cHRM(PngStruct* png, PngInfo* info, std::uint32_t length) {
    namespace t = game::texts;
    if ((png->mode & kPngHaveIhdr) == 0) {
        Error(png, t::kPngSbitMissingIhdr);  // (1.0.2's copy-paste: "before sBIT")
    } else if ((png->mode & kPngHaveIdat) != 0) {
        SkipChunk(png, length, t::kPngChrmAfterIdat);
        return;
    } else if ((png->mode & kPngHavePlte) != 0) {
        Warning(png, t::kPngChrmMissingPlte);  // should be an error, but can be coped with
    } else if (HasInfo(info, kPngInfoChrm) && (info->valid & kPngInfoSrgb) == 0) {
        SkipChunk(png, length, t::kPngChrmDuplicate);
        return;
    }
    if (length != kChrmLength) {
        SkipChunk(png, length, t::kPngChrmLength);
        return;
    }

    const float whiteX = ReadChromaticity(png);
    const float whiteY = ReadChromaticity(png);
    if (!ValidPoint(whiteX, whiteY, kChrmLimit)) {
        SkipChunk(png, 24, t::kPngChrmWhite);
        return;
    }
    const float redX = ReadChromaticity(png);
    const float redY = ReadChromaticity(png);
    if (!ValidPoint(redX, redY, kChrmLimit)) {
        SkipChunk(png, 16, t::kPngChrmRed);
        return;
    }
    const float greenX = ReadChromaticity(png);
    const float greenY = ReadChromaticity(png);
    if (!ValidPoint(greenX, greenY, kChrmLimit)) {
        SkipChunk(png, 8, t::kPngChrmGreen);
        return;
    }
    const float blueX = ReadChromaticity(png);
    const float blueY = ReadChromaticity(png);
    if (!ValidPoint(blueX, blueY, static_cast<double>(kChrmBlueLimit))) {
        SkipChunk(png, 0, t::kPngChrmBlue);
        return;
    }
    if (CrcFinish(png, 0)) {
        return;
    }

    if ((info->valid & kPngInfoSrgb) != 0) {  // (info not checked for null)
        const float values[8] = {whiteX, whiteY, redX, redY, greenX, greenY, blueX, blueY};
        for (int k = 0; k < 8; ++k) {
            if (FarFromSrgb(values[k], k)) {
                Warning(png, t::kPngChrmWithSrgb);
                const auto fprintf = calls::At<calls::FprintfFn>(calls::kFprintf);
                fprintf(&game::g_crtStderr, t::kPngChrmPrintWhiteRed, static_cast<double>(whiteX),
                        static_cast<double>(whiteY), static_cast<double>(redX), static_cast<double>(redY));
                fprintf(&game::g_crtStderr, t::kPngChrmPrintGreenBlue, static_cast<double>(greenX),
                        static_cast<double>(greenY), static_cast<double>(blueX), static_cast<double>(blueY));
                return;
            }
        }
        return;
    }
    calls::At<SetChrmFn>(kPngSetChrm)(png, info, whiteX, whiteY, redX, redY, greenX, greenY, blueX, blueY);
}

// Purpose: The image is sRGB: stores the rendering intent with sRGB's gamma
// and chromaticities.
void __cdecl png_handle_sRGB(PngStruct* png, PngInfo* info, std::uint32_t length) {
    namespace t = game::texts;
    if ((png->mode & kPngHaveIhdr) == 0) {
        Error(png, t::kPngSrgbMissingIhdr);
    } else if ((png->mode & kPngHaveIdat) != 0) {
        SkipChunk(png, length, t::kPngSrgbAfterIdat);
        return;
    } else if ((png->mode & kPngHavePlte) != 0) {
        Warning(png, t::kPngSrgbOutOfPlace);  // should be an error, but can be coped with
    } else if (HasInfo(info, kPngInfoSrgb)) {
        SkipChunk(png, length, t::kPngSrgbDuplicate);
        return;
    }
    if (length != kSrgbLength) {
        SkipChunk(png, length, t::kPngSrgbLength);
        return;
    }
    std::uint8_t buf[1];
    CrcRead(png, buf, kSrgbLength);
    if (CrcFinish(png, 0)) {
        return;
    }
    const std::int32_t intent = buf[0];
    if (intent >= kSrgbIntents) {
        Warning(png, t::kPngSrgbUnknownIntent);
        return;
    }
    // (info not checked for null; png->gamma is compared, not the info's.)
    if ((info->valid & kPngInfoGama) != 0 &&
        Ftol(static_cast<double>(png->gamma) * 100000.0 + 0.5) != kSrgbGammaTimes100000) {
        Warning(png, t::kPngGamaWithSrgb);
        calls::At<calls::FprintfFn>(calls::kFprintf)(&game::g_crtStderr, t::kPngSrgbPrintGamma,
                                                     static_cast<double>(png->gamma));
    }
    if ((info->valid & kPngInfoChrm) != 0) {
        for (int k = 0; k < 8; ++k) {
            if (FarFromSrgb(info->chrm[k], k)) {
                Warning(png, t::kPngChrmWithSrgb);
                break;
            }
        }
    }
    calls::At<SetSrgbFn>(kPngSetSrgb)(png, info, intent);
}

// Purpose: A compressed text chunk (keyword and deflated text), kept
// uncompressed in the info's text list.
void __cdecl png_handle_zTXt(PngStruct* png, PngInfo* info, std::uint32_t length) {
    namespace t = game::texts;
    std::int32_t compression = kPngTextCompressionNone;
    if ((png->mode & kPngHaveIhdr) == 0) {
        Error(png, t::kPngZtxtMissingIhdr);
    }
    if ((png->mode & kPngHaveIdat) != 0) {
        png->mode |= kPngAfterIdat;
    }
    const auto malloc = calls::At<calls::PngMallocFn>(calls::kPngMalloc);
    const auto free = calls::At<calls::PngFreeFn>(calls::kPngFree);
    char* key = static_cast<char*>(malloc(png, length + 1));
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
    // How much of the message goes into the text: key + length - 1 - to
    // (also when `to` is another block, as in the original), at most 26.
    const char* const keyBlock = key;
    const auto messageBytes = [keyBlock, length](const char* to) {
        const auto room = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(keyBlock) + length - 1 -
                                                     reinterpret_cast<std::uintptr_t>(to));
        return room > kZtxtMessageSize - 1 ? kZtxtMessageSize : room;
    };

    if (text == key + length) {
        Warning(png, t::kPngZtxtZeroLength);
    } else {
        ++text;
        compression = static_cast<std::int8_t>(*text);  // (sign-extended)
        if (compression != kPngTextCompressionZtxt) {
            char message[52];
            calls::At<SprintfFn>(kSprintf)(message, t::kPngZtxtUnknownType, compression);
            Warning(png, message);
            CopyBytes(text, t::kPngZtxtError.c_str(), messageBytes(text) + 1);
        } else {
            ++text;
            ZStream& z = png->zstream;
            z.next_in = reinterpret_cast<const std::uint8_t*>(text);
            z.avail_in = static_cast<std::uint32_t>(key + length - text);
            z.next_out = png->zbuf;
            z.avail_out = png->zbuf_size;
            const auto keySize = static_cast<std::uint32_t>(text - key);
            std::uint32_t textSize = 0;
            char* out = nullptr;
            while (z.avail_in != 0) {
                const std::int32_t ret = calls::At<InflateFn>(kInflate)(&z, kZPartialFlush);
                if (ret != kZOk && ret != kZStreamEnd) {
                    Warning(png, z.msg != nullptr ? z.msg : t::kPngZtxtError.c_str());
                    calls::At<StreamFn>(kInflateReset)(&z);
                    z.avail_in = 0;
                    if (out == nullptr) {
                        textSize = keySize + kZtxtMessageSize + 1;
                        out = static_cast<char*>(malloc(png, textSize));
                        CopyBytes(out, key, keySize);
                    }
                    out[textSize - 1] = 0;
                    CopyBytes(out + keySize, t::kPngZtxtError.c_str(), messageBytes(out) + 1);
                    break;
                }
                if (z.avail_out != 0 && ret != kZStreamEnd) {
                    continue;
                }
                const std::uint32_t produced = png->zbuf_size - z.avail_out;
                if (out == nullptr) {
                    out = static_cast<char*>(malloc(png, produced + 1 + keySize));
                    CopyBytes(out + keySize, png->zbuf, produced);
                    CopyBytes(out, key, keySize);
                    textSize = produced + keySize;
                } else {
                    char* const grown = static_cast<char*>(malloc(png, produced + 1 + textSize));
                    CopyBytes(grown, out, textSize);
                    free(png, out);
                    out = grown;
                    CopyBytes(out + textSize, png->zbuf, png->zbuf_size - z.avail_out);
                    textSize += png->zbuf_size - z.avail_out;
                }
                out[textSize] = 0;
                if (ret == kZStreamEnd) {
                    break;
                }
                z.next_out = png->zbuf;
                z.avail_out = png->zbuf_size;
            }
            calls::At<StreamFn>(kInflateReset)(&z);
            z.avail_in = 0;
            free(png, key);
            key = out;
            text = out + keySize;
        }
    }

    auto* const entry = static_cast<PngText*>(malloc(png, sizeof(PngText)));
    entry->compression = compression;
    entry->key = key;
    entry->text = text;
    calls::At<SetTextFn>(kPngSetText)(png, info, entry, 1);
    free(png, entry);
}

// Purpose: The pixel calibration chunk: purpose, value range, equation type
// and its parameters, as strings.
void __cdecl png_handle_pCAL(PngStruct* png, PngInfo* info, std::uint32_t length) {
    namespace t = game::texts;
    if ((png->mode & kPngHaveIhdr) == 0) {
        Error(png, t::kPngPcalMissingIhdr);
    } else if ((png->mode & kPngHaveIdat) != 0) {
        SkipChunk(png, length, t::kPngPcalAfterIdat);
        return;
    } else if (HasInfo(info, kPngInfoPcal)) {
        SkipChunk(png, length, t::kPngPcalDuplicate);
        return;
    }
    const auto malloc = calls::At<calls::PngMallocFn>(calls::kPngMalloc);
    const auto free = calls::At<calls::PngFreeFn>(calls::kPngFree);
    char* const purpose = static_cast<char*>(malloc(png, length + 1));
    CrcRead(png, reinterpret_cast<std::uint8_t*>(purpose), length);
    if (CrcFinish(png, 0)) {
        free(png, purpose);
        return;
    }
    char* const end = purpose + length;
    *end = 0;
    char* buf = purpose;
    while (*buf != 0) {
        ++buf;
    }
    if (end <= buf + kPcalFixedBytes) {
        Warning(png, t::kPngPcalInvalid);
        free(png, purpose);
        return;
    }
    const auto* const fixed = reinterpret_cast<const std::uint8_t*>(buf);
    const std::uint32_t x0 = Uint32(fixed + 1);
    const std::uint32_t x1 = Uint32(fixed + 5);
    const std::uint8_t type = fixed[9];
    const std::uint8_t nparams = fixed[10];
    char* const units = buf + 11;
    // Linear: 2 parameters, base-e and arbitrary-base exponential: 3,
    // hyperbolic: 4. Unknown types pass with a warning.
    static constexpr std::uint8_t kParamsOfType[kPcalEquationTypes] = {2, 3, 3, 4};
    if (type < kPcalEquationTypes && nparams != kParamsOfType[type]) {
        Warning(png, t::kPngPcalBadParams);
        free(png, purpose);
        return;
    }
    if (type >= kPcalEquationTypes) {
        Warning(png, t::kPngPcalUnknownType);
    }
    buf = units;
    while (*buf != 0) {
        ++buf;
    }
    char** const params = static_cast<char**>(malloc(png, nparams * 4u));
    for (std::int32_t i = 0; i < nparams; ++i) {
        ++buf;  // past the previous string's 0
        params[i] = buf;
        while (*buf != 0 && buf <= end) {
            ++buf;
        }
        if (buf > end) {
            Warning(png, t::kPngPcalInvalid);
            free(png, purpose);
            free(png, params);
            return;
        }
    }
    calls::At<SetPcalFn>(kPngSetPcal)(png, info, purpose, static_cast<std::int32_t>(x0),
                                      static_cast<std::int32_t>(x1), type, nparams, units, params);
    free(png, purpose);
    free(png, params);
}

// Purpose: Stores the pixel calibration in the info structure (copies of
// all its strings).
void __cdecl png_set_pCAL(PngStruct* png, PngInfo* info, char* purpose, std::int32_t x0, std::int32_t x1,
                          std::int32_t type, std::int32_t nparams, char* units, char** params) {
    if (png == nullptr || info == nullptr) {
        return;
    }
    CopyStringTo(png, &info->pcal_purpose, purpose);
    info->pcal_X0 = x0;
    info->pcal_type = static_cast<std::uint8_t>(type);
    info->pcal_nparams = static_cast<std::uint8_t>(nparams);
    info->pcal_X1 = x1;
    CopyStringTo(png, &info->pcal_units, units);
    info->pcal_params = static_cast<char**>(
        calls::At<calls::PngMallocFn>(calls::kPngMalloc)(png, static_cast<std::uint32_t>(nparams) * 4 + 4));
    info->pcal_params[nparams] = nullptr;
    for (std::int32_t i = 0; i < nparams; ++i) {
        CopyStringTo(png, &info->pcal_params[i], params[i]);
    }
    info->valid |= kPngInfoPcal;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// As for png_rutil_chunks.cpp: a png_struct / png_info in a random but
// plausible state (mode, CRC flags, chunk name or its ancillary twin,
// png->crc = the CRC of the name), the chunk's data made for the handler
// (values near the limits and near sRGB's, wrong lengths, right or wrong
// CRC) read through a test callback; malloc / free jump to a logging arena
// allocator, fprintf to a logger (format and its double arguments);
// png_error's callback throws, warnings are logged by text. zTXt streams are
// zlib streams of stored blocks (inflate is ours, verified), sometimes with a
// broken header or block length (decoding errors before and after some
// output), with the output buffer 1..64 bytes so the text grows in pieces.
// png_set_pCAL is also called directly on random strings. Compared: the
// structures, the arena, the log and how far the stream was read.

namespace {

struct Chunk2ErrorThrown {};

using InflateInitFn = std::int32_t(__cdecl*)(ZStream* z, const char* version, std::int32_t streamSize);
constexpr std::uintptr_t kInflateInit = 0x005aa480;  // inflateInit_
constexpr const char kZlibVersion[] = "1.1.3";

constexpr std::size_t kArena = 0x10000;

struct Chunk2World {
    PngStruct png;
    PngInfo info;
    std::uint8_t zbuf[64];
    std::uint8_t arena[kArena];
};

Chunk2World* g_world = nullptr;
const std::vector<std::uint8_t>* g_stream = nullptr;
std::size_t g_pos = 0;
std::size_t g_used = 0;
std::vector<std::uint32_t>* g_log = nullptr;

void LogText(std::uint32_t tag, const char* text) {
    g_log->push_back(tag);
    for (; *text != 0; ++text) {
        g_log->push_back(static_cast<std::uint8_t>(*text));
    }
}

void __cdecl TestRead(PngStruct* /*png*/, std::uint8_t* data, std::uint32_t length) {
    g_log->push_back(0x4ead0000u + length);
    for (std::uint32_t i = 0; i < length; ++i, ++g_pos) {
        data[i] = g_pos < g_stream->size() ? (*g_stream)[g_pos] : 0;
    }
}

void __cdecl TestError(PngStruct* /*png*/, const char* message) {
    LogText(0xe0000000u, message);
    throw Chunk2ErrorThrown{};
}

void __cdecl TestWarning(PngStruct* /*png*/, const char* message) {
    LogText(0xf0000000u, message);
}

void* __cdecl TestMalloc(std::size_t size) {
    g_log->push_back(0xa1100000u + static_cast<std::uint32_t>(size));
    if (g_used + size > kArena) {
        return nullptr;
    }
    void* block = g_world->arena + g_used;
    g_used += (size + 15) & ~std::size_t{15};
    return block;
}

void* __cdecl TestCalloc(std::size_t count, std::size_t size) {
    void* block = TestMalloc(count * size);
    if (block != nullptr) {
        std::memset(block, 0, count * size);
    }
    return block;
}

void __cdecl TestFree(void* block) {
    g_log->push_back(0xf7ee0000u + static_cast<std::uint32_t>(static_cast<std::uint8_t*>(block) - g_world->arena));
}

// fprintf(stderr, format, doubles...): the format and one pair of dwords
// per '%' (only %f is used here).
int __cdecl TestFprintf(void* /*file*/, const char* format, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2,
                        std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6, std::uint32_t a7) {
    LogText(0xf9000000u, format);
    const std::uint32_t args[8] = {a0, a1, a2, a3, a4, a5, a6, a7};
    int count = 0;
    for (const char* c = format; *c != 0; ++c) {
        count += *c == '%' ? 2 : 0;
    }
    for (int k = 0; k < count && k < 8; ++k) {
        g_log->push_back(args[k]);
    }
    return 0;
}

std::uint32_t Crc(const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0) {
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
        }
    }
    return ~crc;
}

void Put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int k = 3; k >= 0; --k) {
        out.push_back(static_cast<std::uint8_t>(value >> (8 * k)));
    }
}

void PutText(test::Rng& rng, std::vector<std::uint8_t>& out, std::uint32_t maxLength) {
    for (std::uint32_t k = rng.Below(maxLength + 1); k-- > 0;) {
        out.push_back(static_cast<std::uint8_t>(0x61 + rng.Below(26)));
    }
}

// sRGB's chromaticities times 100000, the values the files usually hold.
constexpr std::uint32_t kSrgbValues[8] = {31270, 32900, 64000, 33000, 30000, 60000, 15000, 6000};

std::vector<std::uint8_t> ChrmData(test::Rng& rng) {
    std::vector<std::uint8_t> d;
    const bool srgb = rng.Chance(1, 2);
    for (int k = 0; k < 8; ++k) {
        std::uint32_t v = srgb ? kSrgbValues[k] + (rng.Chance(1, 4) ? rng.Below(250) - 120 : 0) : rng.Below(80001);
        if (rng.Chance(1, 30)) {
            static const std::uint32_t kEdges[] = {0, 80000, 80001, 79999, 100000, 50000, 0x80000000u, 0xffffffffu};
            v = kEdges[rng.Below(8)];
        }
        Put32(d, v);
    }
    if (rng.Chance(1, 6)) {  // a pair summing to just over / at 1
        const int pair = static_cast<int>(rng.Below(4));
        const std::uint32_t x = 20000 + rng.Below(60000);
        const std::uint32_t y = 100000 - x + rng.Below(3);
        for (int k = 0; k < 4; ++k) {
            d[8 * pair + k] = static_cast<std::uint8_t>(x >> (24 - 8 * k));
            d[8 * pair + 4 + k] = static_cast<std::uint8_t>(y >> (24 - 8 * k));
        }
    }
    return d;
}

// A zlib stream of stored blocks of `text`, sometimes broken.
std::vector<std::uint8_t> StoredZlib(test::Rng& rng, const std::vector<std::uint8_t>& text) {
    std::vector<std::uint8_t> z = {0x78, 0x01};
    if (rng.Chance(1, 12)) {
        z[rng.Below(2)] ^= static_cast<std::uint8_t>(1u << rng.Below(8));  // bad header
    }
    std::size_t at = 0;
    do {
        const std::size_t part = std::min<std::size_t>(text.size() - at, rng.Chance(1, 2) ? 1 + rng.Below(40) : 300);
        const bool last = at + part == text.size();
        z.push_back(last ? 1 : 0);
        std::uint16_t len = static_cast<std::uint16_t>(part);
        std::uint16_t nlen = static_cast<std::uint16_t>(~len);
        if (rng.Chance(1, 25)) {
            nlen ^= 1;  // invalid stored block lengths
        }
        z.push_back(static_cast<std::uint8_t>(len));
        z.push_back(static_cast<std::uint8_t>(len >> 8));
        z.push_back(static_cast<std::uint8_t>(nlen));
        z.push_back(static_cast<std::uint8_t>(nlen >> 8));
        z.insert(z.end(), text.begin() + static_cast<std::ptrdiff_t>(at),
                 text.begin() + static_cast<std::ptrdiff_t>(at + part));
        at += part;
    } while (at < text.size());
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t c : text) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    Put32(z, (b << 16) | a);
    if (rng.Chance(1, 10)) {
        PutText(rng, z, 6);  // trailing bytes
    }
    return z;
}

std::vector<std::uint8_t> ZtxtData(test::Rng& rng) {
    std::vector<std::uint8_t> d;
    PutText(rng, d, 10);
    if (rng.Chance(1, 10)) {
        return d;  // no 0: zero length
    }
    d.push_back(0);
    const std::uint32_t r = rng.Below(10);
    if (r == 0) {
        return d;  // the 0 last: zero length too
    }
    d.push_back(r == 1 ? static_cast<std::uint8_t>(1 + rng.Below(255)) : 0);  // compression method
    if (r == 1) {
        PutText(rng, d, rng.Chance(1, 2) ? 3 : 40);
        return d;
    }
    std::vector<std::uint8_t> text;
    PutText(rng, text, rng.Chance(1, 3) ? 300 : 30);
    if (text.empty()) {
        text.push_back(0x7a);  // (an empty text decodes to nothing: key and text stay null in both, a crash)
    }
    const std::vector<std::uint8_t> z = StoredZlib(rng, text);
    d.insert(d.end(), z.begin(), z.end());
    return d;
}

std::vector<std::uint8_t> PcalData(test::Rng& rng) {
    std::vector<std::uint8_t> d;
    PutText(rng, d, 12);
    d.push_back(0);
    Put32(d, rng.Next32());
    Put32(d, rng.Next32());
    static const std::uint8_t kParams[] = {2, 3, 3, 4};
    const std::uint8_t type = static_cast<std::uint8_t>(rng.Chance(1, 6) ? rng.Below(256) : rng.Below(4));
    std::uint8_t count = type < 4 ? kParams[type] : static_cast<std::uint8_t>(rng.Below(5));
    if (rng.Chance(1, 8)) {
        count = static_cast<std::uint8_t>(rng.Below(6));
    }
    d.push_back(type);
    d.push_back(count);
    PutText(rng, d, 6);  // units
    for (std::uint32_t k = count + (rng.Chance(1, 4) ? 1 : 0); k-- > 0;) {
        d.push_back(0);
        PutText(rng, d, 8);
    }
    if (rng.Chance(1, 4) && !d.empty()) {
        d.resize(rng.Below(static_cast<std::uint32_t>(d.size())) + 1);  // cut short
    }
    return d;
}

std::vector<std::uint8_t> HandlerData(test::Rng& rng, int handler) {
    switch (handler) {
        case 0: return ChrmData(rng);
        case 1: {
            std::vector<std::uint8_t> d(1, static_cast<std::uint8_t>(rng.Chance(1, 5) ? rng.Below(256) : rng.Below(4)));
            return d;
        }
        case 2: return ZtxtData(rng);
        default: return PcalData(rng);
    }
}

constexpr int kHandlers = 4;  // cHRM, sRGB, zTXt, pCAL; then png_set_pCAL alone

struct Functions {
    ChunkHandlerFn handlers[kHandlers];
    SetPcalFn setPcal;
};

// png_set_pCAL's arguments: strings in the arena.
struct PcalArgs {
    char* purpose;
    std::int32_t x0, x1, type, nparams;
    char* units;
    char** params;
};

PcalArgs MakePcalArgs(test::Rng& rng, std::uint8_t* arena) {
    constexpr std::size_t kStrings = 0xe000;
    char* base = reinterpret_cast<char*>(arena + kStrings);
    for (int k = 0; k < 8; ++k) {
        base[0x80 * k + rng.Below(30)] = 0;
    }
    auto** params = reinterpret_cast<char**>(arena + kStrings + 0x800);
    PcalArgs a{base, static_cast<std::int32_t>(rng.Next32()), static_cast<std::int32_t>(rng.Next32()),
               static_cast<std::int32_t>(rng.Next32()), static_cast<std::int32_t>(rng.Below(6)), base + 0x80, params};
    for (std::int32_t k = 0; k < a.nparams; ++k) {
        params[k] = base + 0x100 + 0x80 * k;
    }
    return a;
}

struct RunArgs {
    const Functions* f;
    int handler;
    Chunk2World* world;
    bool noInfo, noPng;
    std::uint32_t length;
    const PcalArgs* pcal;
    std::vector<std::uint32_t>* log;
};

void RunOne(RunArgs* r) {
    try {
        PngInfo* const infoArg = r->noInfo ? nullptr : &r->world->info;
        if (r->handler == kHandlers) {
            const PcalArgs& pcal = *r->pcal;
            r->f->setPcal(r->noPng ? nullptr : &r->world->png, infoArg, pcal.purpose, pcal.x0, pcal.x1, pcal.type,
                          pcal.nparams, pcal.units, pcal.params);
        } else {
            r->f->handlers[r->handler](&r->world->png, infoArg, r->length);
        }
    } catch (const Chunk2ErrorThrown&) {
        r->log->push_back(0xdead);
    }
}

// A crash in either run is logged with the case (and compared: both must
// crash alike).
bool RunGuarded(void (*fn)(RunArgs*), RunArgs* args) {
    __try {
        fn(args);
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
}

void CompareHandlers(test::Context& ctx, const Functions& candidate, int cases) {
    static const char* const kNames[] = {"cHRM", "sRGB", "zTXt", "pCAL", "pCAL"};
    const Functions original{{Original_png_handle_cHRM, Original_png_handle_sRGB, Original_png_handle_zTXt,
                              Original_png_handle_pCAL},
                             Original_png_set_pCAL};
    auto a = std::make_unique<Chunk2World>();
    auto initial = std::make_unique<Chunk2World>();
    auto results = std::make_unique<Chunk2World>();
    test::ScopedJump mallocJump(calls::kMalloc, reinterpret_cast<const void*>(&TestMalloc));
    test::ScopedJump freeJump(calls::kFree, reinterpret_cast<const void*>(&TestFree));
    test::ScopedJump callocJump(calls::kCalloc, reinterpret_cast<const void*>(&TestCalloc));
    test::ScopedJump fprintfJump(calls::kFprintf, reinterpret_cast<const void*>(&TestFprintf));
    for (int i = 0; i < cases; ++i) {
        test::Rng& rng = ctx.rng();
        const int handler = static_cast<int>(rng.Below(kHandlers + 1));
        rng.Fill(a.get(), sizeof(Chunk2World));
        PngStruct& png = a->png;
        static const std::uint32_t kModes[] = {0, 1, 3, 5, 7, 1, 1, 1};
        static const std::uint32_t kFlags[] = {0, 0x100, 0x200, 0x300, 0x400, 0x800, 0xc00, 0xf00};
        png.mode = rng.Chance(1, 8) ? rng.Below(32) : kModes[rng.Below(8)];
        png.zbuf = a->zbuf;
        png.zbuf_size = 1 + rng.Below(64);
        png.flags = (png.flags & ~0xf00u) | kFlags[rng.Below(8)];
        png.error_fn = reinterpret_cast<void*>(&TestError);
        png.warning_fn = reinterpret_cast<void*>(&TestWarning);
        png.read_data_fn = reinterpret_cast<void*>(&TestRead);
        std::memset(&png.zstream, 0, sizeof(png.zstream));
        std::memcpy(png.chunk_name, kNames[handler], 4);
        if (rng.Chance(1, 3)) {
            png.chunk_name[0] ^= kPngChunkAncillary;
        }
        png.crc = Crc(png.chunk_name, 4);
        static const float kGammas[] = {0.45f, 0.45455f, 0.449995f, 0.450005f, 1.0f, 0.0f};
        png.gamma = rng.Chance(1, 5) ? png.gamma : kGammas[rng.Below(6)];
        PngInfo& info = a->info;
        info.valid = rng.Chance(1, 2) ? rng.Next32() : (rng.Chance(1, 2) ? 0 : 0x804u & rng.Next32());
        for (int k = 0; k < 8; ++k) {
            if (rng.Chance(3, 4)) {
                info.chrm[k] = kSrgbChromaticities[k] + (rng.Chance(1, 5) ? (static_cast<float>(rng.Below(40)) - 20.0f) * 1e-4f : 0.0f);
            }
        }
        info.num_text = 0;
        info.max_text = 0;
        info.text = nullptr;
        const PcalArgs pcal = MakePcalArgs(rng, a->arena);

        const std::vector<std::uint8_t> data = handler < kHandlers ? HandlerData(rng, handler) : std::vector<std::uint8_t>{};
        std::uint32_t length = static_cast<std::uint32_t>(data.size());
        // (A zTXt stream that is cut short or missing decodes to nothing: key and
        // text stay null and png_set_text crashes in both; zTXt keeps its length.)
        if (handler != 2 && rng.Chance(1, 8)) {
            length = rng.Chance(1, 2) ? rng.Below(40) : length + 1;
        }
        std::vector<std::uint8_t> stream(data);
        stream.resize(length);
        if (rng.Chance(2, 3)) {
            Put32(stream, Crc(stream.data(), stream.size(), png.crc));
        }
        for (int k = 0; k < 8; ++k) {
            stream.push_back(static_cast<std::uint8_t>(rng.Next32()));
        }
        // cHRM and sRGB read info->valid without a null check: never null there.
        const bool noInfo = handler >= 2 && rng.Chance(1, 20);
        const bool noPng = handler == kHandlers && rng.Chance(1, 20);

        *initial = *a;
        std::vector<std::uint32_t> logs[2];
        std::size_t positions[2] = {};
        bool crashes[2] = {};
        for (int run = 0; run < 2; ++run) {
            if (run == 1) {
                *results = *a;
                *a = *initial;
            }
            g_world = a.get();
            g_stream = &stream;
            g_pos = 0;
            g_used = 0;
            g_log = &logs[run];
            const Functions& f = run == 0 ? original : candidate;
            if (handler == 2) {  // png_read_info's inflateInit_ (its calloc: the arena)
                calls::At<InflateInitFn>(kInflateInit)(&a->png.zstream, kZlibVersion,
                                                       static_cast<std::int32_t>(sizeof(ZStream)));
            }
            RunArgs args{&f, handler, a.get(), noInfo, noPng, length, &pcal, &logs[run]};
            if (RunGuarded(&RunOne, &args)) {
                crashes[run] = true;
                logs[run].push_back(0xc4a5);
            }
            positions[run] = g_pos;
        }
        if (crashes[0] || crashes[1]) {
            std::string hex;
            for (std::uint8_t c : stream) {
                char buf[4];
                std::snprintf(buf, sizeof(buf), "%02x", c);
                hex += buf;
            }
            S3RE_LOG("[test] PngChunkHandlers2 case %d (%s, length %u, zbuf %u) crashed: original %d, ours %d: %s", i,
                     kNames[handler], length, a->png.zbuf_size, crashes[0], crashes[1], hex.c_str());
        }
        bool same = S3RE_EXPECT(logs[0] == logs[1]);
        same &= S3RE_EXPECT_EQ(positions[0], positions[1]);
        same &= S3RE_EXPECT_MEM_EQ(results.get(), a.get(), sizeof(Chunk2World));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, %s, length %u", i, kNames[handler], length);
            break;
        }
    }
    g_world = nullptr;
    g_log = nullptr;
}

const Functions kOurs{{&png_handle_cHRM, &png_handle_sRGB, &png_handle_zTXt, &png_handle_pCAL}, &png_set_pCAL};

// Wrong: the blue point compared with the double 0.8 like the others.
void __cdecl WrongChrm(PngStruct* png, PngInfo* info, std::uint32_t length) {
    png_handle_cHRM(png, info, length);
    if (info != nullptr && (info->valid & kPngInfoChrm) != 0) {
        info->chrm[6] = info->chrm[7];
    }
}

}  // namespace

S3RE_TEST(PngChunkHandlers2_MatchOriginal) {
    CompareHandlers(ctx, kOurs, 12000);
}

S3RE_TEST(PngChunkHandlers2_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Functions wrong = kOurs;
    wrong.handlers[0] = &WrongChrm;
    CompareHandlers(probe, wrong, 12000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::thirdparty
