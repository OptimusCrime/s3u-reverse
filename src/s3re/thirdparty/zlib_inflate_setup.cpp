// zlib 1.1.3's inflate setup and teardown (inflate.c, infblock.c,
// infcodes.c, inftrees.c), statically linked into S3.EXE for libpng
// (re/picture_code.md). All cdecl; memory goes through the stream's zalloc /
// zfree (+0x20 / +0x24, opaque +0x28), the checksum through the blocks
// state's check function.
//
// zlib::inflate_trees_fixed (0x005b2a70): (bl, bd, tl, td, z): the fixed
//   Huffman tables of inffixed.h (9 / 5 bits, tables at 0x7348e8 / 0x7358e8);
//   Z_OK.
// zlib::inflate_codes_new (0x005b1b50): (bl, bd, tl, td, z): a 0x1c-byte
//   codes state (mode START, the two bit counts as bytes, the two trees), or
//   null.
// zlib::inflate_codes_free (0x005b2340): (c, z): ZFREE.
// zlib::inflate_blocks_reset (0x005b0520): (s, z, c): *c = check if c; frees
//   the bit-length table (modes BTREE / DTREE) or the codes state (CODES);
//   mode TYPE, no bits, read = write = window; with a check function,
//   check = z->adler = check(0, null, 0).
// zlib::inflate_blocks_new (0x005b05a0): (z, check, window size): the 0x40-
//   byte state, 1440 huft entries of 8 bytes and the window (freed again if a
//   later allocation fails); reset inlined.
// zlib::inflate_blocks_free (0x005b1360): reset inlined, then frees window,
//   hufts and the state; Z_OK.
// zlib::inflateReset (0x005aa270): Z_STREAM_ERROR without a stream or state;
//   totals and msg cleared, mode BLOCKS (no zlib header) or METHOD, blocks
//   reset.
// zlib::inflateEnd (0x005aa2c0): Z_STREAM_ERROR without stream, state or
//   zfree; frees the blocks and the state.
// zlib::inflateInit2_ (0x005aa310): (z, windowBits, version, stream_size):
//   Z_VERSION_ERROR unless the version's first character is '1' and the
//   size 0x38; default allocator (zcalloc / zcfree); negative windowBits:
//   no zlib header; outside 8..15: inflateEnd and Z_STREAM_ERROR; the blocks
//   state with adler32 (none without a header); Z_MEM_ERROR when memory runs
//   out; inflateEnd and inflateReset inlined.
// zlib::inflateInit_ (0x005aa480): inflateInit2_ with 15 window bits.

#include "game/tables.h"
#include "game/texts.h"
#include "hooks.h"
#include "test/difftest.h"
#include "thirdparty/png_struct.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace s3re::thirdparty {

#pragma pack(push, 1)

// inflate.c's internal_state, 0x18 bytes.
struct InflateState {
    std::int32_t mode;     // 0x00
    std::uint32_t sub[2];  // 0x04 method / check words / marker
    std::int32_t nowrap;   // 0x0c no zlib header and check
    std::uint32_t wbits;   // 0x10
    void* blocks;          // 0x14 inflate_blocks_state
};
static_assert(sizeof(InflateState) == 0x18);

// infblock.c's inflate_blocks_state, 0x40 bytes.
struct InflateBlocks {
    std::int32_t mode;      // 0x00
    void* codes;            // 0x04 sub.decode.codes (mode CODES)
    std::uint32_t sub08;    // 0x08
    void* blens;            // 0x0c sub.trees.blens (modes BTREE, DTREE)
    std::uint32_t sub10[3]; // 0x10
    std::uint32_t bitk;     // 0x1c
    std::uint32_t bitb;     // 0x20
    void* hufts;            // 0x24
    std::uint8_t* window;   // 0x28
    std::uint8_t* end;      // 0x2c
    std::uint8_t* read;     // 0x30
    std::uint8_t* write;    // 0x34
    void* checkfn;          // 0x38
    std::uint32_t check;    // 0x3c
};
static_assert(sizeof(InflateBlocks) == 0x40);

// infcodes.c's inflate_codes_state, 0x1c bytes.
struct InflateCodes {
    std::int32_t mode;      // 0x00
    std::uint8_t sub[0x0c]; // 0x04
    std::uint8_t lbits;     // 0x10
    std::uint8_t dbits;     // 0x11
    std::uint8_t pad[2];    // 0x12
    const void* ltree;      // 0x14
    const void* dtree;      // 0x18
};
static_assert(sizeof(InflateCodes) == 0x1c);

#pragma pack(pop)

namespace {

constexpr std::int32_t kZOk = 0;
constexpr std::int32_t kZStreamError = -2;
constexpr std::int32_t kZMemError = -4;
constexpr std::int32_t kZVersionError = -6;

constexpr std::int32_t kBlocksType = 0;    // infblock.c modes
constexpr std::int32_t kBlocksBtree = 4;
constexpr std::int32_t kBlocksDtree = 5;
constexpr std::int32_t kBlocksCodes = 6;
constexpr std::int32_t kInflateMethod = 0;  // inflate.c modes
constexpr std::int32_t kInflateBlocks = 7;

constexpr std::uint32_t kHuftEntries = 1440;  // MANY
constexpr std::uint32_t kHuftBytes = 8;
constexpr std::int32_t kDefaultWindowBits = 15;
constexpr std::int32_t kMinWindowBits = 8;
constexpr std::int32_t kMaxWindowBits = 15;
constexpr std::uint32_t kStreamSize = sizeof(ZStream);

constexpr std::uintptr_t kZcalloc = 0x005b1520;
constexpr std::uintptr_t kZcfree = 0x005b1540;
constexpr std::uintptr_t kAdler32 = 0x005b13f0;
constexpr std::uintptr_t kInflateCodesFree = 0x005b2340;
constexpr std::uintptr_t kInflateBlocksReset = 0x005b0520;
constexpr std::uintptr_t kInflateBlocksNew = 0x005b05a0;
constexpr std::uintptr_t kInflateBlocksFree = 0x005b1360;
constexpr std::uintptr_t kInflateInit2 = 0x005aa310;

using AllocFn = void*(__cdecl*)(void* opaque, std::uint32_t items, std::uint32_t size);
using FreeFn = void(__cdecl*)(void* opaque, void* block);
using CheckFn = std::uint32_t(__cdecl*)(std::uint32_t check, const std::uint8_t* buffer, std::uint32_t length);
using CodesFreeFn = void(__cdecl*)(void* codes, ZStream* z);
using BlocksResetFn = void(__cdecl*)(InflateBlocks* s, ZStream* z, std::uint32_t* check);
using BlocksNewFn = InflateBlocks*(__cdecl*)(ZStream* z, void* check, std::uint32_t windowSize);
using BlocksFreeFn = std::int32_t(__cdecl*)(InflateBlocks* s, ZStream* z);

void* Allocate(ZStream* z, std::uint32_t items, std::uint32_t size) {
    return reinterpret_cast<AllocFn>(z->zalloc)(z->opaque, items, size);
}

void Release(ZStream* z, void* block) {
    reinterpret_cast<FreeFn>(z->zfree)(z->opaque, block);
}

template <typename Fn>
Fn CallAt(std::uintptr_t address) {
    return reinterpret_cast<Fn>(address);
}

// The end of inflate_blocks_reset (also inlined into blocks_new and
// blocks_free): no bits, empty window, a fresh check value.
void ResetWindow(InflateBlocks* s, ZStream* z) {
    s->mode = kBlocksType;
    s->bitk = 0;
    s->bitb = 0;
    s->read = s->write = s->window;
    if (s->checkfn != nullptr) {
        z->adler = s->check = reinterpret_cast<CheckFn>(s->checkfn)(0, nullptr, 0);
    }
}

// inflate_blocks_reset without the *c store (inlined into blocks_free).
void FreeModeData(InflateBlocks* s, ZStream* z) {
    if (s->mode == kBlocksBtree || s->mode == kBlocksDtree) {
        Release(z, s->blens);
    }
    if (s->mode == kBlocksCodes) {
        CallAt<CodesFreeFn>(kInflateCodesFree)(s->codes, z);
    }
}

// inflateEnd's body (inlined into inflateInit2_).
std::int32_t EndStream(ZStream* z) {
    if (z == nullptr || z->state == nullptr || z->zfree == nullptr) {
        return kZStreamError;
    }
    auto* state = static_cast<InflateState*>(z->state);
    if (state->blocks != nullptr) {
        CallAt<BlocksFreeFn>(kInflateBlocksFree)(static_cast<InflateBlocks*>(state->blocks), z);
    }
    Release(z, z->state);
    z->state = nullptr;
    return kZOk;
}

// inflateReset's body (inlined into inflateInit2_).
std::int32_t ResetStream(ZStream* z) {
    if (z == nullptr || z->state == nullptr) {
        return kZStreamError;
    }
    z->total_in = z->total_out = 0;
    z->msg = nullptr;
    auto* state = static_cast<InflateState*>(z->state);
    state->mode = state->nowrap != 0 ? kInflateBlocks : kInflateMethod;
    CallAt<BlocksResetFn>(kInflateBlocksReset)(static_cast<InflateBlocks*>(state->blocks), z, nullptr);
    return kZOk;
}

}  // namespace

using TreesFixedFn = std::int32_t(__cdecl*)(std::uint32_t* bl, std::uint32_t* bd, const void** tl, const void** td,
                                            ZStream* z);
using CodesNewFn = InflateCodes*(__cdecl*)(std::uint32_t bl, std::uint32_t bd, const void* tl, const void* td,
                                           ZStream* z);
using StreamFn = std::int32_t(__cdecl*)(ZStream* z);
using Init2Fn = std::int32_t(__cdecl*)(ZStream* z, std::int32_t windowBits, const char* version,
                                       std::int32_t streamSize);
using InitFn = std::int32_t(__cdecl*)(ZStream* z, const char* version, std::int32_t streamSize);

std::int32_t __cdecl zlib_inflate_trees_fixed(std::uint32_t* bl, std::uint32_t* bd, const void** tl, const void** td,
                                              ZStream* z);
InflateCodes* __cdecl zlib_inflate_codes_new(std::uint32_t bl, std::uint32_t bd, const void* tl, const void* td,
                                             ZStream* z);
void __cdecl zlib_inflate_codes_free(void* codes, ZStream* z);
void __cdecl zlib_inflate_blocks_reset(InflateBlocks* s, ZStream* z, std::uint32_t* check);
InflateBlocks* __cdecl zlib_inflate_blocks_new(ZStream* z, void* check, std::uint32_t windowSize);
std::int32_t __cdecl zlib_inflate_blocks_free(InflateBlocks* s, ZStream* z);
std::int32_t __cdecl zlib_inflateReset(ZStream* z);
std::int32_t __cdecl zlib_inflateEnd(ZStream* z);
std::int32_t __cdecl zlib_inflateInit2_(ZStream* z, std::int32_t windowBits, const char* version,
                                        std::int32_t streamSize);
std::int32_t __cdecl zlib_inflateInit_(ZStream* z, const char* version, std::int32_t streamSize);

S3RE_HOOK(0x005b2a70, zlib_inflate_trees_fixed, TreesFixedFn, Original_zlib_inflate_trees_fixed,
          zlib_inflate_trees_fixed);
S3RE_HOOK(0x005b1b50, zlib_inflate_codes_new, CodesNewFn, Original_zlib_inflate_codes_new, zlib_inflate_codes_new);
S3RE_HOOK(0x005b2340, zlib_inflate_codes_free, CodesFreeFn, Original_zlib_inflate_codes_free, zlib_inflate_codes_free);
S3RE_HOOK(0x005b0520, zlib_inflate_blocks_reset, BlocksResetFn, Original_zlib_inflate_blocks_reset,
          zlib_inflate_blocks_reset);
S3RE_HOOK(0x005b05a0, zlib_inflate_blocks_new, BlocksNewFn, Original_zlib_inflate_blocks_new, zlib_inflate_blocks_new);
S3RE_HOOK(0x005b1360, zlib_inflate_blocks_free, BlocksFreeFn, Original_zlib_inflate_blocks_free,
          zlib_inflate_blocks_free);
S3RE_HOOK(0x005aa270, zlib_inflateReset, StreamFn, Original_zlib_inflateReset, zlib_inflateReset);
S3RE_HOOK(0x005aa2c0, zlib_inflateEnd, StreamFn, Original_zlib_inflateEnd, zlib_inflateEnd);
S3RE_HOOK(0x005aa310, zlib_inflateInit2_, Init2Fn, Original_zlib_inflateInit2_, zlib_inflateInit2_);
S3RE_HOOK(0x005aa480, zlib_inflateInit_, InitFn, Original_zlib_inflateInit_, zlib_inflateInit_);

// Purpose: The fixed Huffman tables of a "fixed codes" deflate block.
std::int32_t __cdecl zlib_inflate_trees_fixed(std::uint32_t* bl, std::uint32_t* bd, const void** tl, const void** td,
                                              ZStream* /*z*/) {
    *bl = game::g_zlibFixedBl;
    *bd = game::g_zlibFixedBd;
    *tl = &game::g_zlibFixedTl;
    *td = &game::g_zlibFixedTd;
    return kZOk;
}

// Purpose: Allocates the decoder state of one compressed block.
InflateCodes* __cdecl zlib_inflate_codes_new(std::uint32_t bl, std::uint32_t bd, const void* tl, const void* td,
                                             ZStream* z) {
    auto* c = static_cast<InflateCodes*>(Allocate(z, 1, sizeof(InflateCodes)));
    if (c != nullptr) {
        c->mode = 0;  // START
        c->lbits = static_cast<std::uint8_t>(bl);
        c->dbits = static_cast<std::uint8_t>(bd);
        c->ltree = tl;
        c->dtree = td;
    }
    return c;
}

// Purpose: Frees a block's decoder state.
void __cdecl zlib_inflate_codes_free(void* codes, ZStream* z) {
    Release(z, codes);
}

// Purpose: Resets the block decoder (between streams or on errors).
void __cdecl zlib_inflate_blocks_reset(InflateBlocks* s, ZStream* z, std::uint32_t* check) {
    if (check != nullptr) {
        *check = s->check;
    }
    FreeModeData(s, z);
    ResetWindow(s, z);
}

// Purpose: Allocates the block decoder with its Huffman table space and
// sliding window.
InflateBlocks* __cdecl zlib_inflate_blocks_new(ZStream* z, void* check, std::uint32_t windowSize) {
    auto* s = static_cast<InflateBlocks*>(Allocate(z, 1, sizeof(InflateBlocks)));
    if (s == nullptr) {
        return nullptr;
    }
    s->hufts = Allocate(z, kHuftBytes, kHuftEntries);
    if (s->hufts == nullptr) {
        Release(z, s);
        return nullptr;
    }
    s->window = static_cast<std::uint8_t*>(Allocate(z, 1, windowSize));
    if (s->window == nullptr) {
        Release(z, s->hufts);
        Release(z, s);
        return nullptr;
    }
    s->end = s->window + windowSize;
    s->checkfn = check;
    ResetWindow(s, z);  // inflate_blocks_reset(s, z, Z_NULL), mode TYPE
    return s;
}

// Purpose: Frees the block decoder.
std::int32_t __cdecl zlib_inflate_blocks_free(InflateBlocks* s, ZStream* z) {
    FreeModeData(s, z);
    ResetWindow(s, z);
    Release(z, s->window);
    Release(z, s->hufts);
    Release(z, s);
    return kZOk;
}

// Purpose: Restarts an inflate stream.
std::int32_t __cdecl zlib_inflateReset(ZStream* z) {
    return ResetStream(z);
}

// Purpose: Frees an inflate stream's state.
std::int32_t __cdecl zlib_inflateEnd(ZStream* z) {
    return EndStream(z);
}

// Purpose: Sets up an inflate stream (libpng: one per PNG's image data).
std::int32_t __cdecl zlib_inflateInit2_(ZStream* z, std::int32_t windowBits, const char* version,
                                        std::int32_t streamSize) {
    if (version == nullptr || version[0] != game::texts::kZlibVersion.c_str()[0] ||
        streamSize != static_cast<std::int32_t>(kStreamSize)) {
        return kZVersionError;
    }
    if (z == nullptr) {
        return kZStreamError;
    }
    z->msg = nullptr;
    if (z->zalloc == nullptr) {
        z->zalloc = reinterpret_cast<void*>(kZcalloc);
        z->opaque = nullptr;
    }
    if (z->zfree == nullptr) {
        z->zfree = reinterpret_cast<void*>(kZcfree);
    }
    z->state = Allocate(z, 1, sizeof(InflateState));
    if (z->state == nullptr) {
        return kZMemError;
    }
    auto* state = static_cast<InflateState*>(z->state);
    state->blocks = nullptr;
    state->nowrap = 0;
    if (windowBits < 0) {
        windowBits = -windowBits;
        state->nowrap = 1;
    }
    if (windowBits < kMinWindowBits || windowBits > kMaxWindowBits) {
        EndStream(z);
        return kZStreamError;
    }
    state->wbits = static_cast<std::uint32_t>(windowBits);
    void* const check = state->nowrap != 0 ? nullptr : reinterpret_cast<void*>(kAdler32);
    state->blocks = CallAt<BlocksNewFn>(kInflateBlocksNew)(z, check, 1u << windowBits);
    if (static_cast<InflateState*>(z->state)->blocks == nullptr) {
        EndStream(z);
        return kZMemError;
    }
    ResetStream(z);
    return kZOk;
}

// Purpose: inflateInit with the default 32 KB window.
std::int32_t __cdecl zlib_inflateInit_(ZStream* z, const char* version, std::int32_t streamSize) {
    return CallAt<Init2Fn>(kInflateInit2)(z, kDefaultWindowBits, version, streamSize);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// The stream's allocator hands out blocks of a per-run arena (null on a
// chosen call) and records every call as an offset into it, so both runs can
// be compared byte for byte.

namespace {

constexpr std::size_t kArena = 192 * 1024;  // enough for a stream state with a 32 KB window

struct Arena {
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(kArena);
    std::size_t used = 0;
    int calls = 0;
    int failAt = -1;
    std::vector<std::uint32_t> log;
};

Arena* g_arena = nullptr;

std::uint32_t Offset(const void* p) {
    if (p == nullptr) {
        return 0xffffffff;
    }
    return static_cast<std::uint32_t>(static_cast<const std::uint8_t*>(p) - g_arena->bytes.data());
}

void* __cdecl TestAlloc(void* /*opaque*/, std::uint32_t items, std::uint32_t size) {
    g_arena->log.insert(g_arena->log.end(), {0xa110c, items, size});
    if (g_arena->calls++ == g_arena->failAt) {
        return nullptr;
    }
    void* p = g_arena->bytes.data() + g_arena->used;
    g_arena->used += (items * size + 15) & ~15u;
    return p;
}

void __cdecl TestFree(void* /*opaque*/, void* block) {
    g_arena->log.insert(g_arena->log.end(), {0xf7ee, Offset(block)});
}

std::uint32_t __cdecl TestCheck(std::uint32_t check, const std::uint8_t* buffer, std::uint32_t length) {
    g_arena->log.insert(g_arena->log.end(), {0xc4ec, check, buffer == nullptr ? 0u : 1u, length});
    return 0x12345 + g_arena->calls;
}

struct SetupFunctions {
    TreesFixedFn treesFixed;
    CodesNewFn codesNew;
    CodesFreeFn codesFree;
    BlocksResetFn blocksReset;
    BlocksNewFn blocksNew;
    BlocksFreeFn blocksFree;
    StreamFn reset, end;
    Init2Fn init2;
    InitFn init;
};

std::uint32_t RunSetup(const SetupFunctions& f, int op, Arena& arena, ZStream& z, std::uint32_t* outs,
                       const std::uint32_t* v, const char* version) {
    g_arena = &arena;
    auto* blocks = reinterpret_cast<InflateBlocks*>(arena.bytes.data() + 0x100);
    std::uint32_t result = 0;
    switch (op) {
        case 0:
            result = static_cast<std::uint32_t>(f.treesFixed(outs, outs + 1, reinterpret_cast<const void**>(outs + 2),
                                                             reinterpret_cast<const void**>(outs + 3), &z));
            break;
        case 1:
            result = Offset(f.codesNew(v[0], v[1], reinterpret_cast<const void*>(static_cast<std::uintptr_t>(v[2])),
                                       reinterpret_cast<const void*>(static_cast<std::uintptr_t>(v[3])), &z));
            break;
        case 2: f.codesFree(arena.bytes.data() + 0x40, &z); break;
        case 3: f.blocksReset(blocks, &z, (v[0] & 1) != 0 ? outs : nullptr); break;
        case 4:
            result = Offset(f.blocksNew(&z, (v[0] & 1) != 0 ? reinterpret_cast<void*>(&TestCheck) : nullptr,
                                        v[1] % 0x8000 + 1));
            break;
        case 5: result = static_cast<std::uint32_t>(f.blocksFree(blocks, &z)); break;
        case 6: result = static_cast<std::uint32_t>(f.reset((v[0] & 7) == 0 ? nullptr : &z)); break;
        case 7: result = static_cast<std::uint32_t>(f.end((v[0] & 7) == 0 ? nullptr : &z)); break;
        case 8:
            result = static_cast<std::uint32_t>(f.init2((v[0] & 15) == 0 ? nullptr : &z,
                                                        static_cast<std::int32_t>(v[1] % 40) - 20, version,
                                                        (v[2] & 7) == 0 ? 0x34 : 0x38));
            break;
        default:
            result = static_cast<std::uint32_t>(f.init((v[0] & 15) == 0 ? nullptr : &z, version,
                                                       (v[2] & 7) == 0 ? 0x34 : 0x38));
            break;
    }
    g_arena = nullptr;
    return result;
}

// Both functions run on the same arena (its starting contents restored in
// between), so the pointers they store are the same.
void CompareSetup(test::Context& ctx, const SetupFunctions& candidate, int cases) {
    const SetupFunctions original{Original_zlib_inflate_trees_fixed, Original_zlib_inflate_codes_new,
                                  Original_zlib_inflate_codes_free,  Original_zlib_inflate_blocks_reset,
                                  Original_zlib_inflate_blocks_new,  Original_zlib_inflate_blocks_free,
                                  Original_zlib_inflateReset,        Original_zlib_inflateEnd,
                                  Original_zlib_inflateInit2_,       Original_zlib_inflateInit_};
    auto arena = std::make_unique<Arena>();
    std::vector<std::uint8_t> initial(kArena), afterOriginal(kArena);
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(10));
        ctx.rng().Fill(arena->bytes.data(), 0x400);
        std::memset(arena->bytes.data() + 0x400, 0, kArena - 0x400);
        // A blocks state at 0x100 (mode, blens, codes, window...) and a
        // stream state at 0x200, pointing into the arena.
        auto* blocks = reinterpret_cast<InflateBlocks*>(arena->bytes.data() + 0x100);
        static const std::int32_t kModes[] = {0, 1, 4, 5, 6, 7, 9};
        blocks->mode = kModes[ctx.rng().Below(7)];
        blocks->blens = arena->bytes.data() + 0x300;
        blocks->codes = arena->bytes.data() + 0x340;
        blocks->window = arena->bytes.data() + 0x380;
        blocks->hufts = arena->bytes.data() + 0x3c0;
        blocks->checkfn = ctx.rng().Chance(1, 2) ? reinterpret_cast<void*>(&TestCheck) : nullptr;
        auto* state = reinterpret_cast<InflateState*>(arena->bytes.data() + 0x200);
        // (inflateReset needs the blocks state: both versions would crash without one.)
        state->blocks = op != 6 && ctx.rng().Chance(1, 4) ? nullptr : blocks;
        state->nowrap = ctx.rng().Chance(1, 2) ? 0 : static_cast<std::int32_t>(ctx.rng().Next32());
        ZStream za{};
        ctx.rng().Fill(&za, sizeof(za));
        // (A null zalloc / zfree makes inflateInit2_ use the game's zcalloc /
        // zcfree; the emulator spec covers that.)
        za.zalloc = reinterpret_cast<void*>(&TestAlloc);
        za.zfree = ctx.rng().Chance(1, 10) && op == 7 ? nullptr : reinterpret_cast<void*>(&TestFree);
        za.state = ctx.rng().Chance(1, 8) ? nullptr : state;
        const int failAt = ctx.rng().Chance(1, 3) ? static_cast<int>(ctx.rng().Below(4)) : -1;
        std::uint32_t v[4] = {ctx.rng().Next32(), ctx.rng().Next32(), ctx.rng().Next32(), ctx.rng().Next32()};
        const char* version = ctx.rng().Chance(1, 6) ? (ctx.rng().Chance(1, 2) ? "2.0" : nullptr) : "1.1.3";
        std::memcpy(initial.data(), arena->bytes.data(), kArena);
        ZStream zb = za;
        std::uint32_t outsA[4] = {}, outsB[4] = {};

        arena->used = 0x400;
        arena->calls = 0;
        arena->failAt = failAt;
        arena->log.clear();
        const std::uint32_t ra = RunSetup(original, op, *arena, za, outsA, v, version);
        const std::vector<std::uint32_t> logA = arena->log;
        std::memcpy(afterOriginal.data(), arena->bytes.data(), kArena);

        std::memcpy(arena->bytes.data(), initial.data(), kArena);
        arena->used = 0x400;
        arena->calls = 0;
        arena->failAt = failAt;
        arena->log.clear();
        const std::uint32_t rb = RunSetup(candidate, op, *arena, zb, outsB, v, version);

        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT(logA == arena->log);
        same &= S3RE_EXPECT_MEM_EQ(&za, &zb, sizeof(za));
        same &= S3RE_EXPECT_MEM_EQ(afterOriginal.data(), arena->bytes.data(), kArena);
        same &= S3RE_EXPECT_MEM_EQ(outsA, outsB, sizeof(outsA));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
            return;
        }
    }
}

// Wrong: blocks_reset frees the codes state in mode DTREE too.
void __cdecl WrongBlocksReset(InflateBlocks* s, ZStream* z, std::uint32_t* check) {
    const bool dtree = s->mode == kBlocksDtree;
    zlib_inflate_blocks_reset(s, z, check);
    if (dtree) {
        Release(z, s->codes);
    }
}

}  // namespace

S3RE_TEST(ZlibInflateSetup_MatchOriginal) {
    CompareSetup(ctx,
                 {&zlib_inflate_trees_fixed, &zlib_inflate_codes_new, &zlib_inflate_codes_free,
                  &zlib_inflate_blocks_reset, &zlib_inflate_blocks_new, &zlib_inflate_blocks_free, &zlib_inflateReset,
                  &zlib_inflateEnd, &zlib_inflateInit2_, &zlib_inflateInit_},
                 1500);
}

S3RE_TEST(ZlibInflateSetup_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSetup(probe,
                 {&zlib_inflate_trees_fixed, &zlib_inflate_codes_new, &zlib_inflate_codes_free, &WrongBlocksReset,
                  &zlib_inflate_blocks_new, &zlib_inflate_blocks_free, &zlib_inflateReset, &zlib_inflateEnd,
                  &zlib_inflateInit2_, &zlib_inflateInit_},
                 1500);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::thirdparty
