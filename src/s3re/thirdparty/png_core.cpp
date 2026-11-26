// libpng 1.0.2's small helpers (png.c, pngrio.c, pngget.c, pngrtran.c's
// setters, pngrutil.c's CRC handling), statically linked into S3.EXE for
// PaintLib's PNG decoder (re/picture_code.md). All cdecl.
//
// png_sig_cmp (0x005a4de0): (sig, start, num_to_check): memcmp of bytes
//   start.. of the PNG signature (-1 / 0 / 1); 0 for nothing to check.
// png_reset_crc (0x005a4ec0): crc (+0x108) = crc32(0, null, 0).
// png_calculate_crc (0x005a4ee0): (png_ptr, ptr, length): updates crc unless
//   the flags (+0x64) say to ignore it: for an ancillary chunk (lower-case
//   first letter) when both CRC_ANCILLARY_USE and _NOWARN are set, for a
//   critical one when CRC_CRITICAL_IGNORE is set.
// png_get_io_ptr (0x005a5020): io_ptr (+0x5c).
// png_read_data (0x005a9ac0): (png_ptr, data, length): read_data_fn (+0x50),
//   png_error("Call to NULL read function") without one.
// png_set_read_fn (0x005a9af0): (png_ptr, io_ptr, read_data_fn): a null
//   function becomes png_default_read_data; a set write_data_fn is cleared
//   with two warnings; output_flush_fn (+0x144) is cleared.
// png_default_read_data (0x005a9b50): fread from the FILE* io_ptr,
//   png_error("Read Error") on a short read.
// png_get_uint_32 (0x005aa8d0): a big-endian dword.
// png_crc_read (0x005aa900): png_read_data + png_calculate_crc.
// png_crc_error (0x005aaa80): reads the chunk's 4 CRC bytes; 1 if the CRC is
//   checked and differs from crc, else 0.
// png_crc_finish (0x005aa930): (png_ptr, skip): reads and checksums the rest
//   of a chunk through zbuf (+0xa4, size +0xa8), then png_crc_error
//   (inlined); a CRC error is a chunk warning (ancillary chunk without
//   CRC_ANCILLARY_NOWARN, or critical with CRC_CRITICAL_USE) or a chunk
//   error; returns 1 after a warning, else 0.
// png_set_bgr (0x005a5dc0), png_set_strip_16 (0x005a61f0), png_set_expand
//   (0x005a6200), png_set_gray_to_rgb (0x005a6210): set a transformation
//   bit (+0x68).
// png_set_packing (0x005a5dd0): below 8 bits per sample, PACK and
//   usr_bit_depth (+0x120) = 8.
// png_set_interlace_handling (0x005a5df0): 7 passes and INTERLACE for an
//   interlaced image (+0x11b), else 1.
// png_set_filler (0x005a5e20): (png_ptr, filler, location): FILLER, filler
//   (+0x126) = the low byte, flag FILLER_AFTER for location 1.
// png_get_IHDR (0x005a9990): copies the header fields of the info struct
//   through the pointers (all of png_ptr, info, width, height, bit_depth,
//   color_type must be set; the other three are optional) and warns when the
//   width is too large for a 2 GB row; 1, or 0 for a missing pointer.
// png_get_PLTE (0x005a9a80): the palette and its size, PNG_INFO_PLTE (8), or
//   0 without one.

#include "game/tables.h"
#include "game/texts.h"
#include "hooks.h"
#include "test/difftest.h"
#include "thirdparty/png_calls.h"
#include "thirdparty/png_struct.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace s3re::thirdparty {

using game::g_pngSignature;

namespace {

constexpr std::uint32_t kSignatureBytes = 8;
constexpr std::uint32_t kCrcBytes = 4;
constexpr std::uint32_t kFillerAfter = 1;      // PNG_FILLER_AFTER
constexpr std::uint32_t kAdam7Passes = 7;
constexpr std::uint8_t kPackedDepth = 8;
constexpr std::uint32_t kMaxRowBytes = 0x7fffffff;  // PNG_MAX_UINT

std::uint32_t BigEndian32(const std::uint8_t* bytes) {
    return (((((static_cast<std::uint32_t>(bytes[0]) << 8) + bytes[1]) << 8) + bytes[2]) << 8) + bytes[3];
}

// Whether the current chunk's CRC is checked (png_calculate_crc and
// png_crc_error both start with this).
bool NeedsCrc(const PngStruct* png) {
    if ((png->chunk_name[0] & kPngChunkAncillary) != 0) {
        return (png->flags & kPngFlagCrcAncillaryMask) != kPngFlagCrcAncillaryMask;
    }
    return (png->flags & kPngFlagCrcCriticalIgnore) == 0;
}

// png_crc_read's body (inlined into png_crc_finish).
void ReadAndChecksum(PngStruct* png, std::uint8_t* data, std::uint32_t length) {
    calls::At<calls::PngDataFn>(calls::kPngReadData)(png, data, length);
    calls::At<calls::PngDataFn>(calls::kPngCalculateCrc)(png, data, length);
}

// png_crc_error's body (inlined into png_crc_finish).
bool CrcMismatch(PngStruct* png) {
    const bool need = NeedsCrc(png);
    std::uint8_t bytes[kCrcBytes];
    calls::At<calls::PngDataFn>(calls::kPngReadData)(png, bytes, kCrcBytes);
    return need && BigEndian32(bytes) != png->crc;
}

}  // namespace

using SigCmpFn = std::int32_t(__cdecl*)(const std::uint8_t* sig, std::uint32_t start, std::uint32_t count);
using PngVoidFn = void(__cdecl*)(PngStruct* png);
using PngDataFn = void(__cdecl*)(PngStruct* png, std::uint8_t* data, std::uint32_t length);
using GetIoPtrFn = void*(__cdecl*)(PngStruct* png);
using SetReadFn = void(__cdecl*)(PngStruct* png, void* io, void* readFn);
using GetUint32Fn = std::uint32_t(__cdecl*)(const std::uint8_t* bytes);
using CrcErrorFn = std::int32_t(__cdecl*)(PngStruct* png);
using CrcFinishFn = std::int32_t(__cdecl*)(PngStruct* png, std::uint32_t skip);
using InterlaceFn = std::int32_t(__cdecl*)(PngStruct* png);
using SetFillerFn = void(__cdecl*)(PngStruct* png, std::uint32_t filler, std::int32_t location);
using GetIhdrFn = std::uint32_t(__cdecl*)(PngStruct* png, const PngInfo* info, std::uint32_t* width,
                                          std::uint32_t* height, std::int32_t* bitDepth, std::int32_t* colorType,
                                          std::int32_t* interlace, std::int32_t* compression, std::int32_t* filter);
using GetPlteFn = std::uint32_t(__cdecl*)(PngStruct* png, const PngInfo* info, void** palette,
                                          std::int32_t* count);

std::int32_t __cdecl png_sig_cmp(const std::uint8_t* sig, std::uint32_t start, std::uint32_t count);
void __cdecl png_reset_crc(PngStruct* png);
void __cdecl png_calculate_crc(PngStruct* png, std::uint8_t* data, std::uint32_t length);
void* __cdecl png_get_io_ptr(PngStruct* png);
void __cdecl png_read_data(PngStruct* png, std::uint8_t* data, std::uint32_t length);
void __cdecl png_set_read_fn(PngStruct* png, void* io, void* readFn);
void __cdecl png_default_read_data(PngStruct* png, std::uint8_t* data, std::uint32_t length);
std::uint32_t __cdecl png_get_uint_32(const std::uint8_t* bytes);
void __cdecl png_crc_read(PngStruct* png, std::uint8_t* data, std::uint32_t length);
std::int32_t __cdecl png_crc_error(PngStruct* png);
std::int32_t __cdecl png_crc_finish(PngStruct* png, std::uint32_t skip);
void __cdecl png_set_bgr(PngStruct* png);
void __cdecl png_set_strip_16(PngStruct* png);
void __cdecl png_set_expand(PngStruct* png);
void __cdecl png_set_gray_to_rgb(PngStruct* png);
void __cdecl png_set_packing(PngStruct* png);
std::int32_t __cdecl png_set_interlace_handling(PngStruct* png);
void __cdecl png_set_filler(PngStruct* png, std::uint32_t filler, std::int32_t location);
std::uint32_t __cdecl png_get_IHDR(PngStruct* png, const PngInfo* info, std::uint32_t* width, std::uint32_t* height,
                                   std::int32_t* bitDepth, std::int32_t* colorType, std::int32_t* interlace,
                                   std::int32_t* compression, std::int32_t* filter);
std::uint32_t __cdecl png_get_PLTE(PngStruct* png, const PngInfo* info, void** palette, std::int32_t* count);

S3RE_HOOK(0x005a4de0, png_sig_cmp, SigCmpFn, Original_png_sig_cmp, png_sig_cmp);
S3RE_HOOK(0x005a4ec0, png_reset_crc, PngVoidFn, Original_png_reset_crc, png_reset_crc);
S3RE_HOOK(0x005a4ee0, png_calculate_crc, PngDataFn, Original_png_calculate_crc, png_calculate_crc);
S3RE_HOOK(0x005a5020, png_get_io_ptr, GetIoPtrFn, Original_png_get_io_ptr, png_get_io_ptr);
S3RE_HOOK(0x005a9ac0, png_read_data, PngDataFn, Original_png_read_data, png_read_data);
S3RE_HOOK(0x005a9af0, png_set_read_fn, SetReadFn, Original_png_set_read_fn, png_set_read_fn);
S3RE_HOOK(0x005a9b50, png_default_read_data, PngDataFn, Original_png_default_read_data, png_default_read_data);
S3RE_HOOK(0x005aa8d0, png_get_uint_32, GetUint32Fn, Original_png_get_uint_32, png_get_uint_32);
S3RE_HOOK(0x005aa900, png_crc_read, PngDataFn, Original_png_crc_read, png_crc_read);
S3RE_HOOK(0x005aaa80, png_crc_error, CrcErrorFn, Original_png_crc_error, png_crc_error);
S3RE_HOOK(0x005aa930, png_crc_finish, CrcFinishFn, Original_png_crc_finish, png_crc_finish);
S3RE_HOOK(0x005a5dc0, png_set_bgr, PngVoidFn, Original_png_set_bgr, png_set_bgr);
S3RE_HOOK(0x005a61f0, png_set_strip_16, PngVoidFn, Original_png_set_strip_16, png_set_strip_16);
S3RE_HOOK(0x005a6200, png_set_expand, PngVoidFn, Original_png_set_expand, png_set_expand);
S3RE_HOOK(0x005a6210, png_set_gray_to_rgb, PngVoidFn, Original_png_set_gray_to_rgb, png_set_gray_to_rgb);
S3RE_HOOK(0x005a5dd0, png_set_packing, PngVoidFn, Original_png_set_packing, png_set_packing);
S3RE_HOOK(0x005a5df0, png_set_interlace_handling, InterlaceFn, Original_png_set_interlace_handling,
          png_set_interlace_handling);
S3RE_HOOK(0x005a5e20, png_set_filler, SetFillerFn, Original_png_set_filler, png_set_filler);
S3RE_HOOK(0x005a9990, png_get_IHDR, GetIhdrFn, Original_png_get_IHDR, png_get_IHDR);
S3RE_HOOK(0x005a9a80, png_get_PLTE, GetPlteFn, Original_png_get_PLTE, png_get_PLTE);

// Purpose: Checks bytes of a file against the PNG signature.
std::int32_t __cdecl png_sig_cmp(const std::uint8_t* sig, std::uint32_t start, std::uint32_t count) {
    if (count > kSignatureBytes) {
        count = kSignatureBytes;
    } else if (count < 1) {
        return 0;
    }
    if (start > kSignatureBytes - 1) {
        return 0;
    }
    if (start + count > kSignatureBytes) {
        count = kSignatureBytes - start;
    }
    for (std::uint32_t i = start; i < start + count; ++i) {  // memcmp: -1 / 1 at the first difference
        if (sig[i] != g_pngSignature[i]) {
            return sig[i] < g_pngSignature[i] ? -1 : 1;
        }
    }
    return 0;
}

// Purpose: Starts the CRC of a new chunk.
void __cdecl png_reset_crc(PngStruct* png) {
    png->crc = calls::At<calls::Crc32Fn>(calls::kCrc32)(0, nullptr, 0);
}

// Purpose: Adds chunk bytes to the running CRC (unless CRCs are ignored for
// this kind of chunk).
void __cdecl png_calculate_crc(PngStruct* png, std::uint8_t* data, std::uint32_t length) {
    if (NeedsCrc(png)) {
        png->crc = calls::At<calls::Crc32Fn>(calls::kCrc32)(png->crc, data, length);
    }
}

// Purpose: The user pointer of the read callback (PaintLib's CDataSource).
void* __cdecl png_get_io_ptr(PngStruct* png) {
    return png->io_ptr;
}

// Purpose: Reads bytes of the PNG file through the installed read callback.
void __cdecl png_read_data(PngStruct* png, std::uint8_t* data, std::uint32_t length) {
    if (png->read_data_fn == nullptr) {
        calls::At<calls::PngMessageFn>(calls::kPngError)(png, game::texts::kPngNullReadFn);
        return;
    }
    reinterpret_cast<calls::PngDataFn>(png->read_data_fn)(png, data, length);
}

// Purpose: Installs the read callback and its user pointer (PaintLib reads
// from its CDataSource).
void __cdecl png_set_read_fn(PngStruct* png, void* io, void* readFn) {
    png->io_ptr = io;
    png->read_data_fn = readFn != nullptr ? readFn : reinterpret_cast<void*>(calls::kPngDefaultReadData);
    if (png->write_data_fn != nullptr) {
        png->write_data_fn = nullptr;
        calls::At<calls::PngMessageFn>(calls::kPngWarning)(png, game::texts::kPngBothRwFn1);
        calls::At<calls::PngMessageFn>(calls::kPngWarning)(png, game::texts::kPngBothRwFn2);
    }
    png->output_flush_fn = nullptr;
}

// Purpose: libpng's default read callback, reading from a C FILE.
void __cdecl png_default_read_data(PngStruct* png, std::uint8_t* data, std::uint32_t length) {
    const std::size_t read = calls::At<calls::FreadFn>(calls::kFread)(data, 1, length, png->io_ptr);
    if (read != length) {
        calls::At<calls::PngMessageFn>(calls::kPngError)(png, game::texts::kPngReadError);
    }
}

// Purpose: Reads a big-endian dword of the PNG format.
std::uint32_t __cdecl png_get_uint_32(const std::uint8_t* bytes) {
    return BigEndian32(bytes);
}

// Purpose: Reads chunk data and adds it to the chunk's CRC.
void __cdecl png_crc_read(PngStruct* png, std::uint8_t* data, std::uint32_t length) {
    ReadAndChecksum(png, data, length);
}

// Purpose: Reads a chunk's stored CRC and says whether it doesn't match.
std::int32_t __cdecl png_crc_error(PngStruct* png) {
    return CrcMismatch(png) ? 1 : 0;
}

// Purpose: Skips the rest of a chunk (still checksummed) and checks its CRC.
std::int32_t __cdecl png_crc_finish(PngStruct* png, std::uint32_t skip) {
    const std::uint32_t step = png->zbuf_size;
    std::uint32_t left = skip;
    for (; left > step; left -= step) {
        ReadAndChecksum(png, png->zbuf, png->zbuf_size);  // re-read per round, as the original
    }
    if (left != 0) {
        ReadAndChecksum(png, png->zbuf, left);
    }
    if (!CrcMismatch(png)) {
        return 0;
    }
    const bool ancillary = (png->chunk_name[0] & kPngChunkAncillary) != 0;
    if ((ancillary && (png->flags & kPngFlagCrcAncillaryNoWarn) == 0) ||
        (!ancillary && (png->flags & kPngFlagCrcCriticalUse) != 0)) {
        calls::At<calls::PngMessageFn>(calls::kPngChunkWarning)(png, game::texts::kPngCrcError);
    } else {
        calls::At<calls::PngMessageFn>(calls::kPngChunkError)(png, game::texts::kPngCrcError);
    }
    return 1;
}

// Purpose: Asks for BGR instead of RGB output.
void __cdecl png_set_bgr(PngStruct* png) {
    png->transformations |= kPngBgr;
}

// Purpose: Asks for 16-bit samples to be cut to 8 bits.
void __cdecl png_set_strip_16(PngStruct* png) {
    png->transformations |= kPngStrip16;
}

// Purpose: Asks for palette and low-depth images to be expanded.
void __cdecl png_set_expand(PngStruct* png) {
    png->transformations |= kPngExpand;
}

// Purpose: Asks for grey images to be turned into RGB.
void __cdecl png_set_gray_to_rgb(PngStruct* png) {
    png->transformations |= kPngGrayToRgb;
}

// Purpose: Asks for samples below 8 bits to be unpacked to a byte each.
void __cdecl png_set_packing(PngStruct* png) {
    if (png->bit_depth < kPackedDepth) {
        png->transformations |= kPngPack;
        png->usr_bit_depth = kPackedDepth;
    }
}

// Purpose: Asks libpng to deinterlace; returns how many passes to read.
std::int32_t __cdecl png_set_interlace_handling(PngStruct* png) {
    if (png->interlaced == 0) {
        return 1;
    }
    png->transformations |= kPngInterlace;
    return kAdam7Passes;
}

// Purpose: Asks for a filler byte to be added to each pixel (RGB to 32 bits).
void __cdecl png_set_filler(PngStruct* png, std::uint32_t filler, std::int32_t location) {
    png->transformations |= kPngFiller;
    png->filler = static_cast<std::uint8_t>(filler);
    if (location == static_cast<std::int32_t>(kFillerAfter)) {
        png->flags |= kPngFlagFillerAfter;
    } else {
        png->flags &= ~kPngFlagFillerAfter;
    }
}

// Purpose: Returns a PNG's header fields (size, depth, colour type...).
std::uint32_t __cdecl png_get_IHDR(PngStruct* png, const PngInfo* info, std::uint32_t* width, std::uint32_t* height,
                                   std::int32_t* bitDepth, std::int32_t* colorType, std::int32_t* interlace,
                                   std::int32_t* compression, std::int32_t* filter) {
    if (png == nullptr || info == nullptr || width == nullptr || height == nullptr || bitDepth == nullptr ||
        colorType == nullptr) {
        return 0;
    }
    *width = info->width;
    *height = info->height;
    *bitDepth = info->bit_depth;
    *colorType = info->color_type;
    if (compression != nullptr) {
        *compression = info->compression_type;
    }
    if (filter != nullptr) {
        *filter = info->filter_type;
    }
    if (interlace != nullptr) {
        *interlace = info->interlace_type;
    }
    // Read back through the pointers, as the original.
    const std::int32_t type = *colorType;
    std::int32_t channels = type == static_cast<std::int32_t>(kPngColorTypePalette)
                                ? 1
                                : static_cast<std::int32_t>((type & kPngColorMaskColor) | 1);
    if ((type & kPngColorMaskAlpha) != 0) {
        ++channels;
    }
    const std::int32_t bytesPerPixel = (channels * *bitDepth + 7) >> 3;
    if (*width > kMaxRowBytes / static_cast<std::uint32_t>(bytesPerPixel)) {
        calls::At<calls::PngMessageFn>(calls::kPngWarning)(png, game::texts::kPngWidthTooLarge);
    }
    return 1;
}

// Purpose: Returns a PNG's palette, if it has one.
std::uint32_t __cdecl png_get_PLTE(PngStruct* png, const PngInfo* info, void** palette, std::int32_t* count) {
    if (png == nullptr || info == nullptr || (info->valid & kPngInfoPlte) == 0 || palette == nullptr) {
        return 0;
    }
    *palette = info->palette;
    *count = info->num_palette;
    return kPngInfoPlte;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// One random png_struct per case; the read callback serves bytes from a
// buffer and the error / warning callbacks record (the error one throws).

namespace {

struct ErrorThrown {};

struct ReadLog {
    std::vector<std::uint8_t> source;
    std::size_t cursor = 0;
    std::vector<std::uint32_t> events;  // lengths read, 0xe0000000 + message markers
};

ReadLog* g_log = nullptr;

void __cdecl TestRead(PngStruct* /*png*/, std::uint8_t* data, std::uint32_t length) {
    g_log->events.push_back(length);
    for (std::uint32_t i = 0; i < length; ++i) {
        data[i] = g_log->source[(g_log->cursor++) % g_log->source.size()];
    }
}

void __cdecl TestError(PngStruct* /*png*/, const char* message) {
    g_log->events.push_back(0xe0000000u + static_cast<std::uint8_t>(message[0]));
    throw ErrorThrown{};
}

void __cdecl TestWarning(PngStruct* /*png*/, const char* message) {
    g_log->events.push_back(0xf0000000u + static_cast<std::uint8_t>(message[0]));
}

struct CoreFunctions {
    SigCmpFn sigCmp;
    PngVoidFn resetCrc;
    PngDataFn calculateCrc;
    GetIoPtrFn getIoPtr;
    PngDataFn readData;
    SetReadFn setReadFn;
    GetUint32Fn getUint32;
    PngDataFn crcRead;
    CrcErrorFn crcError;
    CrcFinishFn crcFinish;
    PngVoidFn setBgr, setStrip16, setExpand, setGrayToRgb, setPacking;
    InterlaceFn interlace;
    SetFillerFn setFiller;
    GetIhdrFn getIhdr;
    GetPlteFn getPlte;
};

constexpr int kCoreOps = 19;

struct CoreState {
    PngStruct png;
    PngInfo info;
    std::uint8_t buffer[0x200];
    std::uint32_t outputs[10];
};

std::uint32_t RunCore(const CoreFunctions& f, int op, CoreState& s, ReadLog& log, std::uint32_t x, std::uint32_t y,
                      std::uint32_t mask) {
    g_log = &log;
    std::uint32_t result = 0xdeadbeef;
    std::int32_t* out = reinterpret_cast<std::int32_t*>(s.outputs);
    try {
        switch (op) {
            case 0: result = static_cast<std::uint32_t>(f.sigCmp(s.buffer, x % 10, y % 10)); break;
            case 1: f.resetCrc(&s.png); break;
            case 2: f.calculateCrc(&s.png, s.buffer, x % 0x200); break;
            case 3: result = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(f.getIoPtr(&s.png))); break;
            case 4: f.readData(&s.png, s.buffer, x % 0x200); break;
            case 5:
                f.setReadFn(&s.png, reinterpret_cast<void*>(static_cast<std::uintptr_t>(y)),
                            (x & 1) != 0 ? reinterpret_cast<void*>(&TestRead) : nullptr);
                break;
            case 6: result = f.getUint32(s.buffer + x % 0x100); break;
            case 7: f.crcRead(&s.png, s.buffer, x % 0x200); break;
            case 8: result = static_cast<std::uint32_t>(f.crcError(&s.png)); break;
            case 9: result = static_cast<std::uint32_t>(f.crcFinish(&s.png, x % 0x400)); break;
            case 10: f.setBgr(&s.png); break;
            case 11: f.setStrip16(&s.png); break;
            case 12: f.setExpand(&s.png); break;
            case 13: f.setGrayToRgb(&s.png); break;
            case 14: f.setPacking(&s.png); break;
            case 15: result = static_cast<std::uint32_t>(f.interlace(&s.png)); break;
            case 16: f.setFiller(&s.png, x, static_cast<std::int32_t>(y % 3)); break;
            case 17: {
                // mask: which pointers are null.
                auto ptr = [&](int k) { return (mask >> k) & 1 ? nullptr : out + k; };
                result = f.getIhdr((mask & 0x200) ? nullptr : &s.png, (mask & 0x400) ? nullptr : &s.info,
                                   reinterpret_cast<std::uint32_t*>(ptr(0)), reinterpret_cast<std::uint32_t*>(ptr(1)),
                                   ptr(2), ptr(3), ptr(4), ptr(5), ptr(6));
                break;
            }
            default:
                result = f.getPlte((mask & 0x200) ? nullptr : &s.png, (mask & 0x400) ? nullptr : &s.info,
                                   (mask & 1) ? nullptr : reinterpret_cast<void**>(out), out + 1);
                break;
        }
    } catch (const ErrorThrown&) {
        result = 0xe0e0e0e0;
    }
    g_log = nullptr;
    return result;
}

void CompareCore(test::Context& ctx, const CoreFunctions& candidate, int cases) {
    const CoreFunctions original{Original_png_sig_cmp,      Original_png_reset_crc,     Original_png_calculate_crc,
                                 Original_png_get_io_ptr,   Original_png_read_data,     Original_png_set_read_fn,
                                 Original_png_get_uint_32,  Original_png_crc_read,      Original_png_crc_error,
                                 Original_png_crc_finish,   Original_png_set_bgr,       Original_png_set_strip_16,
                                 Original_png_set_expand,   Original_png_set_gray_to_rgb, Original_png_set_packing,
                                 Original_png_set_interlace_handling, Original_png_set_filler,
                                 Original_png_get_IHDR,     Original_png_get_PLTE};
    static const std::uint32_t kFlagChoices[] = {0, 0x100, 0x200, 0x300, 0x400, 0x800, 0xc00, 0xf00};
    auto a = std::make_unique<CoreState>();
    auto b = std::make_unique<CoreState>();
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(kCoreOps));
        ctx.rng().Fill(a.get(), sizeof(CoreState));
        PngStruct& png = a->png;
        png.read_data_fn = ctx.rng().Chance(1, 10) ? nullptr : reinterpret_cast<void*>(&TestRead);
        png.error_fn = reinterpret_cast<void*>(&TestError);
        png.warning_fn = reinterpret_cast<void*>(&TestWarning);
        if (ctx.rng().Chance(1, 3)) {
            png.write_data_fn = nullptr;
        }
        png.flags = (png.flags & ~0xf00u) | kFlagChoices[ctx.rng().Below(8)];
        png.zbuf = a->buffer;
        png.zbuf_size = ctx.rng().Range(1, 0x200);
        png.bit_depth = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? ctx.rng().Range(1, 16) : ctx.rng().Next32());
        png.interlaced = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Next32());
        if (ctx.rng().Chance(1, 2)) {
            std::memcpy(a->buffer + (ctx.rng().Below(4) == 0 ? 0 : ctx.rng().Below(4)), &g_pngSignature, 8);
        }
        PngInfo& info = a->info;
        if (info.bit_depth == 0) {
            info.bit_depth = 1;  // the original divides by the bytes per pixel
        }
        if (ctx.rng().Chance(1, 2)) {
            info.color_type = static_cast<std::uint8_t>(ctx.rng().Below(8));
        }
        if (ctx.rng().Chance(1, 2)) {
            info.width = ctx.rng().Next32() >> ctx.rng().Below(8);
        }
        const std::uint32_t mask = ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Next32() & ctx.rng().Next32() & 0x7ff;
        const std::uint32_t x = ctx.rng().Next32(), y = ctx.rng().Next32();
        ReadLog logA;
        logA.source.resize(ctx.rng().Range(1, 64));
        ctx.rng().Fill(logA.source.data(), logA.source.size());
        if (op == 8 && ctx.rng().Chance(1, 2)) {
            png.crc = BigEndian32(logA.source.data());  // the stored CRC matches (read from the start)
            logA.source.resize(4);
        }
        std::memcpy(b.get(), a.get(), sizeof(CoreState));
        b->png.zbuf = b->buffer;
        ReadLog logB = logA;
        const std::uint32_t ra = RunCore(original, op, *a, logA, x, y, mask);
        const std::uint32_t rb = RunCore(candidate, op, *b, logB, x, y, mask);
        b->png.zbuf = a->buffer;
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT(logA.events == logB.events);
        same &= S3RE_EXPECT_MEM_EQ(a.get(), b.get(), sizeof(CoreState));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
            return;
        }
    }
}

// Wrong: png_set_filler stores the whole filler word.
void __cdecl WrongSetFiller(PngStruct* png, std::uint32_t filler, std::int32_t location) {
    png_set_filler(png, filler, location);
    png->filler = static_cast<std::uint16_t>(filler);
}

}  // namespace

S3RE_TEST(PngCore_MatchOriginal) {
    CompareCore(ctx,
                {&png_sig_cmp, &png_reset_crc, &png_calculate_crc, &png_get_io_ptr, &png_read_data, &png_set_read_fn,
                 &png_get_uint_32, &png_crc_read, &png_crc_error, &png_crc_finish, &png_set_bgr, &png_set_strip_16,
                 &png_set_expand, &png_set_gray_to_rgb, &png_set_packing, &png_set_interlace_handling,
                 &png_set_filler, &png_get_IHDR, &png_get_PLTE},
                20000);
}

S3RE_TEST(PngCore_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareCore(probe,
                {&png_sig_cmp, &png_reset_crc, &png_calculate_crc, &png_get_io_ptr, &png_read_data, &png_set_read_fn,
                 &png_get_uint_32, &png_crc_read, &png_crc_error, &png_crc_finish, &png_set_bgr, &png_set_strip_16,
                 &png_set_expand, &png_set_gray_to_rgb, &png_set_packing, &png_set_interlace_handling,
                 &WrongSetFiller, &png_get_IHDR, &png_get_PLTE},
                20000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::thirdparty
