#pragma once

// The data structures of libpng 1.0.2 and zlib 1.1.3 as S3.EXE has them
// (re/picture_code.md). Both libraries are linked into S3.EXE statically and
// used only by PaintLib's PNG decoder (the online lobby's banner image).
//
// Only the fields our replacements use are named; every offset was read off
// the binary's code (the comment names a function that uses it) and matches
// the published 1.0.2 / 1.1.3 headers for this configuration (read-only
// build with the write-flush, user-transform, dither and gamma fields).
// Everything is plain C (cdecl).

#include <cstddef>
#include <cstdint>

namespace s3re::thirdparty {

#pragma pack(push, 1)

// zlib's z_stream (zlib.h 1.1.3), 0x38 bytes.
struct ZStream {
    const std::uint8_t* next_in;  // 0x00
    std::uint32_t avail_in;       // 0x04
    std::uint32_t total_in;       // 0x08
    std::uint8_t* next_out;       // 0x0c
    std::uint32_t avail_out;      // 0x10
    std::uint32_t total_out;      // 0x14
    const char* msg;              // 0x18
    void* state;                  // 0x1c struct internal_state (inflate.c)
    void* zalloc;                 // 0x20 alloc_func
    void* zfree;                  // 0x24 free_func
    void* opaque;                 // 0x28
    std::int32_t data_type;       // 0x2c
    std::uint32_t adler;          // 0x30
    std::uint32_t reserved;       // 0x34
};
static_assert(sizeof(ZStream) == 0x38);

// png_row_info, 12 bytes.
struct PngRowInfo {
    std::uint32_t width;        // 0x00
    std::uint32_t rowbytes;     // 0x04
    std::uint8_t color_type;    // 0x08
    std::uint8_t bit_depth;     // 0x09
    std::uint8_t channels;      // 0x0a
    std::uint8_t pixel_depth;   // 0x0b
};
static_assert(sizeof(PngRowInfo) == 12);

// png_color_16, 10 bytes.
struct PngColor16 {
    std::uint8_t index;    // 0x00
    std::uint8_t pad;      // 0x01
    std::uint16_t red;     // 0x02
    std::uint16_t green;   // 0x04
    std::uint16_t blue;    // 0x06
    std::uint16_t gray;    // 0x08
};
static_assert(sizeof(PngColor16) == 10);

// png_color_8, 5 bytes.
struct PngColor8 {
    std::uint8_t red, green, blue, gray, alpha;
};
static_assert(sizeof(PngColor8) == 5);

// png_struct (png.h 1.0.2), 0x20c bytes (png_create_struct 0x5aa000).
struct PngStruct {
    std::uint8_t jmpbuf[0x40];          // 0x000 MSVC 6 jmp_buf (png_error longjmps here)
    void* error_fn;                     // 0x040 png_error 0x5a9e20
    void* warning_fn;                   // 0x044 png_warning 0x5a9e60
    void* error_ptr;                    // 0x048 png_set_error_fn 0x5a9fe0
    void* write_data_fn;                // 0x04c png_set_read_fn 0x5a9af0
    void* read_data_fn;                 // 0x050 png_read_data 0x5a9ac0
    void* read_user_transform_fn;       // 0x054
    void* write_user_transform_fn;      // 0x058
    void* io_ptr;                       // 0x05c png_get_io_ptr 0x5a5020
    std::uint32_t mode;                 // 0x060
    std::uint32_t flags;                // 0x064 PNG_FLAG_* (png_calculate_crc 0x5a4ee0)
    std::uint32_t transformations;      // 0x068 PNG_BGR... (png_set_bgr 0x5a5dc0)
    ZStream zstream;                    // 0x06c
    std::uint8_t* zbuf;                 // 0x0a4 png_crc_finish 0x5aa930
    std::uint32_t zbuf_size;            // 0x0a8
    std::int32_t zlib_settings[5];      // 0x0ac zlib level, method, window bits, mem level, strategy (write side)
    std::uint32_t width;                // 0x0c0 png_read_start_row 0x5b01c0
    std::uint32_t height;               // 0x0c4
    std::uint32_t num_rows;             // 0x0c8 rows in the current pass
    std::uint32_t usr_width;            // 0x0cc
    std::uint32_t rowbytes;             // 0x0d0
    std::uint32_t irowbytes;            // 0x0d4 bytes of the current (interlaced) row, filter byte included
    std::uint32_t iwidth;               // 0x0d8 pixels of the current (interlaced) row
    std::uint32_t row_number;           // 0x0dc in the current pass (png_read_finish_row 0x5afe20)
    std::uint8_t* prev_row;             // 0x0e0
    std::uint8_t* row_buf;              // 0x0e4 png_combine_row 0x5af4d0
    std::uint8_t* sub_row;              // 0x0e8 (write side)
    std::uint8_t* up_row;               // 0x0ec
    std::uint8_t* avg_row;              // 0x0f0
    std::uint8_t* paeth_row;            // 0x0f4
    PngRowInfo row_info;                // 0x0f8
    std::uint32_t idat_size;            // 0x104 bytes left in the current IDAT chunk
    std::uint32_t crc;                  // 0x108 png_reset_crc 0x5a4ec0
    void* palette;                      // 0x10c
    std::uint16_t num_palette;          // 0x110
    std::uint16_t num_trans;            // 0x112 png_read_start_row 0x5b01c0
    std::uint8_t chunk_name[5];         // 0x114 png_format_buffer 0x5a9ef0
    std::uint8_t compression;           // 0x119
    std::uint8_t filter;                // 0x11a
    std::uint8_t interlaced;            // 0x11b png_set_interlace_handling 0x5a5df0
    std::uint8_t pass;                  // 0x11c
    std::uint8_t do_filter;             // 0x11d
    std::uint8_t color_type;            // 0x11e
    std::uint8_t bit_depth;             // 0x11f png_set_packing 0x5a5dd0
    std::uint8_t usr_bit_depth;         // 0x120
    std::uint8_t pixel_depth;           // 0x121
    std::uint8_t channels;              // 0x122
    std::uint8_t usr_channels;          // 0x123
    std::uint8_t sig_bytes;             // 0x124
    std::uint8_t pad125;                // 0x125
    std::uint16_t filler;               // 0x126 png_set_filler 0x5a5e20
    std::uint8_t unk128[0x130 - 0x128]; // 0x128 background gamma type and value
    PngColor16 background;              // 0x130 png_handle_bKGD 0x5ad3b0
    PngColor16 background_1;            // 0x13a
    void* output_flush_fn;              // 0x144 png_set_read_fn 0x5a9af0
    std::uint32_t flush_dist;           // 0x148 (write side)
    std::uint32_t flush_rows;           // 0x14c
    std::int32_t gamma_shift;           // 0x150 png_read_destroy 0x5a5b90
    float gamma;                        // 0x154 png_handle_gAMA 0x5ab470
    float screen_gamma;                 // 0x158
    std::uint8_t* gamma_table;          // 0x15c
    std::uint8_t* gamma_from_1;         // 0x160
    std::uint8_t* gamma_to_1;           // 0x164
    std::uint16_t** gamma_16_table;     // 0x168
    std::uint16_t** gamma_16_from_1;    // 0x16c
    std::uint16_t** gamma_16_to_1;      // 0x170
    PngColor8 sig_bit;                  // 0x174 png_handle_sBIT 0x5ab8b0
    PngColor8 shift;                    // 0x179
    std::uint8_t pad17e[2];             // 0x17e
    std::uint8_t* trans;                // 0x180 png_handle_tRNS 0x5ace60
    PngColor16 trans_values;            // 0x184
    std::uint8_t unk18e[0x190 - 0x18e]; // 0x18e
    void* read_row_fn;                  // 0x190 called after each row (png_read_row 0x5a5440)
    std::uint8_t unk194[0x1e4 - 0x194]; // 0x194
    std::uint8_t* palette_lookup;       // 0x1e4 png_do_dither's (png_do_read_transformations 0x5a6c30)
    std::uint8_t* dither_index;         // 0x1e8
    std::uint16_t* hist;                // 0x1ec png_handle_hIST 0x5ad860
    std::uint8_t unk1f0[0x20c - 0x1f0]; // 0x1f0
};
static_assert(offsetof(PngStruct, error_fn) == 0x40);
static_assert(offsetof(PngStruct, io_ptr) == 0x5c);
static_assert(offsetof(PngStruct, zstream) == 0x6c);
static_assert(offsetof(PngStruct, zbuf) == 0xa4);
static_assert(offsetof(PngStruct, width) == 0xc0);
static_assert(offsetof(PngStruct, prev_row) == 0xe0);
static_assert(offsetof(PngStruct, row_info) == 0xf8);
static_assert(offsetof(PngStruct, idat_size) == 0x104);
static_assert(offsetof(PngStruct, crc) == 0x108);
static_assert(offsetof(PngStruct, num_trans) == 0x112);
static_assert(offsetof(PngStruct, chunk_name) == 0x114);
static_assert(offsetof(PngStruct, filler) == 0x126);
static_assert(offsetof(PngStruct, background) == 0x130);
static_assert(offsetof(PngStruct, output_flush_fn) == 0x144);
static_assert(offsetof(PngStruct, gamma) == 0x154);
static_assert(offsetof(PngStruct, sig_bit) == 0x174);
static_assert(offsetof(PngStruct, trans) == 0x180);
static_assert(offsetof(PngStruct, trans_values) == 0x184);
static_assert(offsetof(PngStruct, read_row_fn) == 0x190);
static_assert(offsetof(PngStruct, hist) == 0x1ec);
static_assert(sizeof(PngStruct) == 0x20c);

// png_info (png.h 1.0.2), 0xb8 bytes (png_create_info_struct 0x5a4f30).
struct PngInfo {
    std::uint32_t width;               // 0x00 png_get_IHDR 0x5a9990
    std::uint32_t height;              // 0x04
    std::uint32_t valid;               // 0x08 PNG_INFO_* (png_get_PLTE 0x5a9a80)
    std::uint32_t rowbytes;            // 0x0c
    void* palette;                     // 0x10
    std::uint16_t num_palette;         // 0x14
    std::uint16_t num_trans;           // 0x16
    std::uint8_t bit_depth;            // 0x18
    std::uint8_t color_type;           // 0x19
    std::uint8_t compression_type;     // 0x1a
    std::uint8_t filter_type;          // 0x1b
    std::uint8_t interlace_type;       // 0x1c
    std::uint8_t channels;             // 0x1d
    std::uint8_t pixel_depth;          // 0x1e
    std::uint8_t spare_byte;           // 0x1f
    std::uint8_t signature[8];         // 0x20
    float gamma;                       // 0x28 png_set_gAMA 0x5b1600
    std::uint8_t srgb_intent;          // 0x2c
    std::uint8_t pad2d[3];             // 0x2d
    std::int32_t num_text;             // 0x30
    std::int32_t max_text;             // 0x34
    void* text;                        // 0x38
    std::uint8_t mod_time[8];          // 0x3c png_time (7 bytes, padded; png_set_tIME copies 8)
    PngColor8 sig_bit;                 // 0x44 png_set_sBIT 0x5b18f0
    std::uint8_t pad49[3];             // 0x49
    std::uint8_t* trans;               // 0x4c png_set_tRNS 0x5b1ae0
    PngColor16 trans_values;           // 0x50
    PngColor16 background;             // 0x5a png_set_bKGD 0x5b1550
    std::uint32_t x_offset;            // 0x64 png_set_oFFs 0x5b1720
    std::uint32_t y_offset;            // 0x68
    std::uint8_t offset_unit_type;     // 0x6c
    std::uint8_t pad6d[3];             // 0x6d
    std::uint32_t x_pixels_per_unit;   // 0x70 png_set_pHYs 0x5b1890
    std::uint32_t y_pixels_per_unit;   // 0x74
    std::uint8_t phys_unit_type;       // 0x78
    std::uint8_t pad79[3];             // 0x79
    void* hist;                        // 0x7c png_set_hIST 0x5b1630
    float chrm[8];                     // 0x80 x/y of white, red, green, blue (png_set_cHRM 0x5b1590)
    char* pcal_purpose;                // 0xa0 png_info_destroy 0x5a4f60
    std::int32_t pcal_X0;              // 0xa4
    std::int32_t pcal_X1;              // 0xa8
    char* pcal_units;                  // 0xac
    char** pcal_params;                // 0xb0
    std::uint8_t pcal_type;            // 0xb4
    std::uint8_t pcal_nparams;         // 0xb5
    std::uint8_t padb6[2];             // 0xb6
};
static_assert(offsetof(PngInfo, pcal_params) == 0xb0);
static_assert(offsetof(PngInfo, bit_depth) == 0x18);
static_assert(offsetof(PngInfo, gamma) == 0x28);
static_assert(offsetof(PngInfo, mod_time) == 0x3c);
static_assert(offsetof(PngInfo, sig_bit) == 0x44);
static_assert(offsetof(PngInfo, trans) == 0x4c);
static_assert(offsetof(PngInfo, background) == 0x5a);
static_assert(offsetof(PngInfo, x_offset) == 0x64);
static_assert(offsetof(PngInfo, x_pixels_per_unit) == 0x70);
static_assert(offsetof(PngInfo, hist) == 0x7c);
static_assert(offsetof(PngInfo, chrm) == 0x80);
static_assert(sizeof(PngInfo) == 0xb8);

// png_text (png.h 1.0.2), 16 bytes.
struct PngText {
    std::int32_t compression;   // 0x00 -1: none, 0: zTXt deflate
    char* key;                  // 0x04
    char* text;                 // 0x08
    std::uint32_t text_length;  // 0x0c
};
static_assert(sizeof(PngText) == 16);

#pragma pack(pop)

inline constexpr std::int32_t kPngTextCompressionNone = -1;
inline constexpr std::int32_t kPngTextCompressionZtxt = 0;

// png_struct.transformations bits (png.h 1.0.2).
inline constexpr std::uint32_t kPngBgr = 0x0001;
inline constexpr std::uint32_t kPngInterlace = 0x0002;
inline constexpr std::uint32_t kPngPack = 0x0004;
inline constexpr std::uint32_t kPngStrip16 = 0x0400;
inline constexpr std::uint32_t kPngExpand = 0x1000;
inline constexpr std::uint32_t kPngGrayToRgb = 0x4000;
inline constexpr std::uint32_t kPngFiller = 0x8000;
inline constexpr std::uint32_t kPngPackswap = 0x10000;

// png_struct.mode bits.
inline constexpr std::uint32_t kPngHaveIhdr = 0x01;
inline constexpr std::uint32_t kPngHavePlte = 0x02;
inline constexpr std::uint32_t kPngHaveIdat = 0x04;
inline constexpr std::uint32_t kPngAfterIdat = 0x08;
inline constexpr std::uint32_t kPngHaveIend = 0x10;

// png_struct.flags bits.
inline constexpr std::uint32_t kPngFlagZlibFinished = 0x0020;
inline constexpr std::uint32_t kPngFlagRowInit = 0x0040;
inline constexpr std::uint32_t kPngFlagFillerAfter = 0x0080;
inline constexpr std::uint32_t kPngFlagCrcAncillaryUse = 0x0100;
inline constexpr std::uint32_t kPngFlagCrcAncillaryNoWarn = 0x0200;
inline constexpr std::uint32_t kPngFlagCrcCriticalUse = 0x0400;
inline constexpr std::uint32_t kPngFlagCrcCriticalIgnore = 0x0800;
inline constexpr std::uint32_t kPngFlagFreeTrans = 0x2000;
inline constexpr std::uint32_t kPngFlagFreeHist = 0x4000;
inline constexpr std::uint32_t kPngFlagCrcAncillaryMask = kPngFlagCrcAncillaryUse | kPngFlagCrcAncillaryNoWarn;

// png_info.valid bits.
inline constexpr std::uint32_t kPngInfoGama = 0x0001;
inline constexpr std::uint32_t kPngInfoSbit = 0x0002;
inline constexpr std::uint32_t kPngInfoChrm = 0x0004;
inline constexpr std::uint32_t kPngInfoPlte = 0x0008;
inline constexpr std::uint32_t kPngInfoTrns = 0x0010;
inline constexpr std::uint32_t kPngInfoBkgd = 0x0020;
inline constexpr std::uint32_t kPngInfoHist = 0x0040;
inline constexpr std::uint32_t kPngInfoPhys = 0x0080;
inline constexpr std::uint32_t kPngInfoOffs = 0x0100;
inline constexpr std::uint32_t kPngInfoTime = 0x0200;
inline constexpr std::uint32_t kPngInfoSrgb = 0x0800;

// Colour types and the ancillary bit of a chunk name's first letter.
inline constexpr std::uint32_t kPngColorTypeGray = 0;
inline constexpr std::uint32_t kPngColorTypeRgb = 2;
inline constexpr std::uint32_t kPngColorTypePalette = 3;
inline constexpr std::uint32_t kPngColorTypeGrayAlpha = 4;
inline constexpr std::uint32_t kPngColorTypeRgbAlpha = 6;
inline constexpr std::uint32_t kPngColorMaskColor = 2;
inline constexpr std::uint32_t kPngColorMaskAlpha = 4;
inline constexpr std::uint8_t kPngChunkAncillary = 0x20;

// png_create_struct's type argument.
inline constexpr std::int32_t kPngStructPng = 1;
inline constexpr std::int32_t kPngStructInfo = 2;

}  // namespace s3re::thirdparty
