#pragma once

// zlib 1.1.3's internal inflate state (inflate.c, infblock.h, infcodes.c,
// inftrees.h), as S3.EXE has it (re/picture_code.md). Shared by
// zlib_inflate_setup.cpp (allocation and reset) and zlib_inflate.cpp (the
// decoder itself). Offsets read off the binary's code (inflate 0x5aa4a0,
// inflate_blocks 0x5b0660, inflate_codes 0x5b1b90).

#include "thirdparty/png_struct.h"

#include <cstddef>
#include <cstdint>

namespace s3re::thirdparty {

#pragma pack(push, 1)

// inftrees.h's inflate_huft, 8 bytes: one Huffman table entry.
struct InflateHuft {
    std::uint8_t exop;    // 0x00 extra bits or operation
    std::uint8_t bits;    // 0x01 bits in this code or subcode
    std::uint8_t pad[2];  // 0x02 padding of the union; huft_build leaves stack garbage here
    std::uint32_t base;   // 0x04 literal, length / distance base, or table offset
};
static_assert(sizeof(InflateHuft) == 8);

// inflate.c's internal_state, 0x18 bytes.
struct InflateState {
    std::int32_t mode;     // 0x00 kInflate* below
    std::uint32_t sub[2];  // 0x04 sub.method, sub.check.was / need, sub.marker
    std::int32_t nowrap;   // 0x0c no zlib header and check
    std::uint32_t wbits;   // 0x10
    void* blocks;          // 0x14 inflate_blocks_state
};
static_assert(sizeof(InflateState) == 0x18);

// infblock.h's inflate_blocks_state, 0x40 bytes.
struct InflateBlocks {
    std::int32_t mode;      // 0x00 kBlocks* below
    void* codes;            // 0x04 sub.left (STORED), sub.trees.table (TABLE..DTREE), sub.decode.codes (CODES)
    std::uint32_t sub08;    // 0x08 sub.trees.index
    void* blens;            // 0x0c sub.trees.blens (modes BTREE, DTREE)
    std::uint32_t sub10[2]; // 0x10 sub.trees.bb, sub.trees.tb
    std::uint32_t last;     // 0x18 the current block is the last one
    std::uint32_t bitk;     // 0x1c bits in the bit buffer
    std::uint32_t bitb;     // 0x20 bit buffer
    void* hufts;            // 0x24 space for the dynamic trees (1440 entries)
    std::uint8_t* window;   // 0x28 sliding window
    std::uint8_t* end;      // 0x2c one past the window
    std::uint8_t* read;     // 0x30 window read pointer
    std::uint8_t* write;    // 0x34 window write pointer
    void* checkfn;          // 0x38 adler32 or null
    std::uint32_t check;    // 0x3c check value of the output so far
};
static_assert(sizeof(InflateBlocks) == 0x40);

// infcodes.c's inflate_codes_state, 0x1c bytes.
struct InflateCodes {
    std::int32_t mode;          // 0x00 kCodes* below
    std::uint32_t len;          // 0x04 length of the string to copy
    std::uint32_t sub08;        // 0x08 sub.code.tree, sub.lit, sub.copy.get
    std::uint32_t sub0c;        // 0x0c sub.code.need, sub.copy.dist
    std::uint8_t lbits;         // 0x10
    std::uint8_t dbits;         // 0x11
    std::uint8_t pad[2];        // 0x12
    const InflateHuft* ltree;   // 0x14
    const InflateHuft* dtree;   // 0x18
};
static_assert(sizeof(InflateCodes) == 0x1c);

#pragma pack(pop)

// zlib's return codes.
inline constexpr std::int32_t kZOk = 0;
inline constexpr std::int32_t kZStreamEnd = 1;
inline constexpr std::int32_t kZNeedDict = 2;
inline constexpr std::int32_t kZStreamError = -2;
inline constexpr std::int32_t kZDataError = -3;
inline constexpr std::int32_t kZMemError = -4;
inline constexpr std::int32_t kZBufError = -5;
inline constexpr std::int32_t kZVersionError = -6;
inline constexpr std::int32_t kZFinish = 4;  // inflate's flush argument

// inflate.c's modes.
inline constexpr std::int32_t kInflateMethod = 0;
inline constexpr std::int32_t kInflateFlag = 1;
inline constexpr std::int32_t kInflateDict4 = 2;
inline constexpr std::int32_t kInflateDict3 = 3;
inline constexpr std::int32_t kInflateDict2 = 4;
inline constexpr std::int32_t kInflateDict1 = 5;
inline constexpr std::int32_t kInflateDict0 = 6;
inline constexpr std::int32_t kInflateBlocks = 7;
inline constexpr std::int32_t kInflateCheck4 = 8;
inline constexpr std::int32_t kInflateCheck3 = 9;
inline constexpr std::int32_t kInflateCheck2 = 10;
inline constexpr std::int32_t kInflateCheck1 = 11;
inline constexpr std::int32_t kInflateDone = 12;
inline constexpr std::int32_t kInflateBad = 13;

// infblock.c's modes.
inline constexpr std::int32_t kBlocksType = 0;
inline constexpr std::int32_t kBlocksLens = 1;
inline constexpr std::int32_t kBlocksStored = 2;
inline constexpr std::int32_t kBlocksTable = 3;
inline constexpr std::int32_t kBlocksBtree = 4;
inline constexpr std::int32_t kBlocksDtree = 5;
inline constexpr std::int32_t kBlocksCodes = 6;
inline constexpr std::int32_t kBlocksDry = 7;
inline constexpr std::int32_t kBlocksDone = 8;
inline constexpr std::int32_t kBlocksBad = 9;

// infcodes.c's modes.
inline constexpr std::int32_t kCodesStart = 0;
inline constexpr std::int32_t kCodesLen = 1;
inline constexpr std::int32_t kCodesLenExt = 2;
inline constexpr std::int32_t kCodesDist = 3;
inline constexpr std::int32_t kCodesDistExt = 4;
inline constexpr std::int32_t kCodesCopy = 5;
inline constexpr std::int32_t kCodesLit = 6;
inline constexpr std::int32_t kCodesWash = 7;
inline constexpr std::int32_t kCodesEnd = 8;
inline constexpr std::int32_t kCodesBadCode = 9;

inline constexpr std::uint32_t kHuftEntries = 1440;  // inftrees.h MANY

// S3.EXE addresses of the zlib functions (calls go through them, see png_calls.h).
namespace zcalls {
inline constexpr std::uintptr_t kInflate = 0x005aa4a0;
inline constexpr std::uintptr_t kInflateBlocksReset = 0x005b0520;
inline constexpr std::uintptr_t kInflateBlocks = 0x005b0660;
inline constexpr std::uintptr_t kInflateCodesNew = 0x005b1b50;
inline constexpr std::uintptr_t kInflateCodes = 0x005b1b90;
inline constexpr std::uintptr_t kInflateCodesFree = 0x005b2340;
inline constexpr std::uintptr_t kInflateTreesBits = 0x005b2360;
inline constexpr std::uintptr_t kHuftBuild = 0x005b2410;
inline constexpr std::uintptr_t kInflateTreesDynamic = 0x005b28e0;
inline constexpr std::uintptr_t kInflateTreesFixed = 0x005b2a70;
inline constexpr std::uintptr_t kInflateFlush = 0x005b2aa0;
inline constexpr std::uintptr_t kInflateFast = 0x005b2be0;
}  // namespace zcalls

}  // namespace s3re::thirdparty
