// zlib 1.1.3's decoder (inflate.c, infblock.c, infcodes.c, inffast.c,
// inftrees.c, infutil.c), statically linked into S3.EXE for libpng
// (re/picture_code.md). All cdecl. The state structs are in zlib_state.h.
//
// zlib::inflate (0x005aa4a0): (z, flush): the zlib header (method, window
//   size, check bits, preset dictionary), the blocks, the adler32 trailer.
//   Z_FINISH turns Z_OK into Z_BUF_ERROR.
// zlib::inflate_blocks (0x005b0660): (s, z, r): block headers, stored
//   blocks, the dynamic code lengths; the codes go to inflate_codes.
// zlib::inflate_codes (0x005b1b90): (s, z, r): literals and length /
//   distance pairs of one Huffman block, one symbol at a time;
//   inflate_fast while there are 258 bytes of window and 10 of input.
// zlib::inflate_fast (0x005b2be0): (bl, bd, tl, td, s, z): the same without
//   input/output checks; Z_OK, Z_STREAM_END or Z_DATA_ERROR.
// zlib::inflate_flush (0x005b2aa0): (s, z, r): copies the window to the
//   output (in two parts when it wrapped), updating the check value; r, or
//   Z_OK instead of Z_BUF_ERROR when something was copied.
// zlib::huft_build (0x005b2410): (b, n, s, d, e, t, m, hp, hn, v): the
//   multi-level Huffman decoding tables for n code lengths (inftrees.c).
// zlib::inflate_trees_bits (0x005b2360): (c, bb, tb, hp, z): the table of
//   the 19 code length codes.
// zlib::inflate_trees_dynamic (0x005b28e0): (nl, nd, c, bl, bd, tl, td, hp,
//   z): the literal/length and distance tables of a dynamic block.
//
// Quirk kept: huft_build copies a stack temporary into every table entry,
// so bytes 2 and 3 of each entry (the padding of a union, never read) hold
// whatever was on the original's stack; ours writes 0 there (tests and the
// emulator spec leave these bytes out).

#include "thirdparty/zlib_state.h"

#include "game/tables.h"
#include "game/texts.h"
#include "hooks.h"

#include <intrin.h>

#include <cstdint>

namespace s3re::thirdparty {

using InflateFn = std::int32_t(__cdecl*)(ZStream* z, std::int32_t flush);
using BlocksFn = std::int32_t(__cdecl*)(InflateBlocks* s, ZStream* z, std::int32_t r);
using CodesFn = std::int32_t(__cdecl*)(InflateBlocks* s, ZStream* z, std::int32_t r);
using FastFn = std::int32_t(__cdecl*)(std::uint32_t bl, std::uint32_t bd, const InflateHuft* tl,
                                      const InflateHuft* td, InflateBlocks* s, ZStream* z);
using FlushFn = std::int32_t(__cdecl*)(InflateBlocks* s, ZStream* z, std::int32_t r);
using HuftBuildFn = std::int32_t(__cdecl*)(const std::uint32_t* b, std::uint32_t n, std::uint32_t s,
                                           const std::uint32_t* d, const std::uint32_t* e, InflateHuft** t,
                                           std::uint32_t* m, InflateHuft* hp, std::uint32_t* hn, std::uint32_t* v);
using TreesBitsFn = std::int32_t(__cdecl*)(std::uint32_t* c, std::uint32_t* bb, InflateHuft** tb, InflateHuft* hp,
                                           ZStream* z);
using TreesDynamicFn = std::int32_t(__cdecl*)(std::uint32_t nl, std::uint32_t nd, std::uint32_t* c, std::uint32_t* bl,
                                              std::uint32_t* bd, InflateHuft** tl, InflateHuft** td,
                                              InflateHuft* hp, ZStream* z);

std::int32_t __cdecl zlib_inflate(ZStream* z, std::int32_t flush);
std::int32_t __cdecl zlib_inflate_blocks(InflateBlocks* s, ZStream* z, std::int32_t r);
std::int32_t __cdecl zlib_inflate_codes(InflateBlocks* s, ZStream* z, std::int32_t r);
std::int32_t __cdecl zlib_inflate_fast(std::uint32_t bl, std::uint32_t bd, const InflateHuft* tl,
                                       const InflateHuft* td, InflateBlocks* s, ZStream* z);
std::int32_t __cdecl zlib_inflate_flush(InflateBlocks* s, ZStream* z, std::int32_t r);
std::int32_t __cdecl zlib_huft_build(const std::uint32_t* b, std::uint32_t n, std::uint32_t s,
                                     const std::uint32_t* d, const std::uint32_t* e, InflateHuft** t,
                                     std::uint32_t* m, InflateHuft* hp, std::uint32_t* hn, std::uint32_t* v);
std::int32_t __cdecl zlib_inflate_trees_bits(std::uint32_t* c, std::uint32_t* bb, InflateHuft** tb, InflateHuft* hp,
                                             ZStream* z);
std::int32_t __cdecl zlib_inflate_trees_dynamic(std::uint32_t nl, std::uint32_t nd, std::uint32_t* c,
                                                std::uint32_t* bl, std::uint32_t* bd, InflateHuft** tl,
                                                InflateHuft** td, InflateHuft* hp, ZStream* z);

S3RE_HOOK(0x005aa4a0, zlib_inflate, InflateFn, Original_zlib_inflate, zlib_inflate);
S3RE_HOOK(0x005b0660, zlib_inflate_blocks, BlocksFn, Original_zlib_inflate_blocks, zlib_inflate_blocks);
S3RE_HOOK(0x005b1b90, zlib_inflate_codes, CodesFn, Original_zlib_inflate_codes, zlib_inflate_codes);
S3RE_HOOK(0x005b2be0, zlib_inflate_fast, FastFn, Original_zlib_inflate_fast, zlib_inflate_fast);
S3RE_HOOK(0x005b2aa0, zlib_inflate_flush, FlushFn, Original_zlib_inflate_flush, zlib_inflate_flush);
S3RE_HOOK(0x005b2410, zlib_huft_build, HuftBuildFn, Original_zlib_huft_build, zlib_huft_build);
S3RE_HOOK(0x005b2360, zlib_inflate_trees_bits, TreesBitsFn, Original_zlib_inflate_trees_bits,
          zlib_inflate_trees_bits);
S3RE_HOOK(0x005b28e0, zlib_inflate_trees_dynamic, TreesDynamicFn, Original_zlib_inflate_trees_dynamic,
          zlib_inflate_trees_dynamic);

namespace {

using AllocFn = void*(__cdecl*)(void* opaque, std::uint32_t items, std::uint32_t size);
using ReleaseFn = void(__cdecl*)(void* opaque, void* block);
using CheckFn = std::uint32_t(__cdecl*)(std::uint32_t check, const std::uint8_t* buffer, std::uint32_t length);
using TreesFixedFn = std::int32_t(__cdecl*)(std::uint32_t* bl, std::uint32_t* bd, const InflateHuft** tl,
                                            const InflateHuft** td, ZStream* z);
using CodesNewFn = InflateCodes*(__cdecl*)(std::uint32_t bl, std::uint32_t bd, const InflateHuft* tl,
                                           const InflateHuft* td, ZStream* z);
using CodesFreeFn = void(__cdecl*)(InflateCodes* c, ZStream* z);
using BlocksResetFn = void(__cdecl*)(InflateBlocks* s, ZStream* z, std::uint32_t* check);

template <typename Fn>
Fn CallAt(std::uintptr_t address) {
    return reinterpret_cast<Fn>(address);
}

void* Allocate(ZStream* z, std::uint32_t items, std::uint32_t size) {
    return reinterpret_cast<AllocFn>(z->zalloc)(z->opaque, items, size);
}

void Release(ZStream* z, void* block) {
    reinterpret_cast<ReleaseFn>(z->zfree)(z->opaque, block);
}

std::int32_t Flush(InflateBlocks* s, ZStream* z, std::int32_t r) {
    return CallAt<FlushFn>(zcalls::kInflateFlush)(s, z, r);
}

// x86 shifts use the count mod 32 (only reachable with malformed tables).
std::uint32_t Shr(std::uint32_t value, std::uint32_t count) {
    return value >> (count & 31);
}

std::uint32_t Shl(std::uint32_t value, std::uint32_t count) {
    return value << (count & 31);
}

// inflate_mask[n]; read from the game's table like the original (an entry's
// extra-bit count of a malformed table can index past its 17 entries).
std::uint32_t Mask(std::uint32_t bits) {
    const std::uint32_t* const table = game::g_zlibInflateMask.get();
    return table[bits];
}

// zmemcpy: rep movsd then rep movsb, as MSVC 6 inlines memcpy.
void CopyBytes(std::uint8_t* dst, const std::uint8_t* src, std::uint32_t count) {
    __movsd(reinterpret_cast<unsigned long*>(dst), reinterpret_cast<const unsigned long*>(src), count / 4);
    __movsb(dst + (count & ~3u), src + (count & ~3u), count % 4);
}

std::uint32_t Distance(const void* from, const void* to) {
    return static_cast<std::uint32_t>(static_cast<const std::uint8_t*>(to) - static_cast<const std::uint8_t*>(from));
}

// The local copies of infutil.h's macros: input pointer and count, bit
// buffer, window write pointer and the room in front of it.
struct Window {
    InflateBlocks* s;
    ZStream* z;
    const std::uint8_t* p;  // input data pointer
    std::uint32_t n;        // bytes available there
    std::uint32_t b;        // bit buffer
    std::uint32_t k;        // bits in the bit buffer
    std::uint8_t* q;        // output window write pointer
    std::uint32_t m;        // bytes to the end of the window or the read pointer

    Window(InflateBlocks* blocks, ZStream* stream) : s(blocks), z(stream) { Load(); }

    // WAVAIL
    std::uint32_t Room() const { return q < s->read ? Distance(q, s->read) - 1 : Distance(q, s->end); }
    void LoadIn() {  // LOADIN
        p = z->next_in;
        n = z->avail_in;
        b = s->bitb;
        k = s->bitk;
    }
    void LoadOut() {  // LOADOUT
        q = s->write;
        m = Room();
    }
    void Load() {
        LoadIn();
        LoadOut();
    }
    void Update() {  // UPDATE
        s->bitb = b;
        s->bitk = k;
        z->avail_in = n;
        z->total_in += Distance(z->next_in, p);
        z->next_in = p;
        s->write = q;
    }
    // LEAVE
    std::int32_t Leave(std::int32_t r) {
        Update();
        return Flush(s, z, r);
    }
    // NEEDBITS(j): false when the input ran out (the caller leaves).
    bool NeedBits(std::uint32_t j, std::int32_t& r) {
        while (k < j) {
            if (n == 0) {
                return false;
            }
            r = kZOk;
            --n;
            b |= Shl(*p++, k);
            k += 8;
        }
        return true;
    }
    void DumpBits(std::uint32_t j) {
        b = Shr(b, j);
        k -= j;
    }
    // WRAP
    void Wrap() {
        if (q == s->end && s->read != s->window) {
            q = s->window;
            m = Room();
        }
    }
    // FLUSH
    void FlushWindow(std::int32_t& r) {
        s->write = q;
        r = Flush(s, z, r);
        LoadOut();
    }
    // NEEDOUT: false when the window is full even after flushing.
    bool NeedOut(std::int32_t& r) {
        if (m == 0) {
            Wrap();
            if (m == 0) {
                FlushWindow(r);
                Wrap();
                if (m == 0) {
                    return false;
                }
            }
        }
        r = kZOk;
        return true;
    }
    void Output(std::uint8_t byte) {  // OUTBYTE
        *q++ = byte;
        --m;
    }
};

constexpr std::uint32_t kMaxBits = 15;           // BMAX
constexpr std::uint32_t kBitLengthCodes = 19;
constexpr std::uint32_t kLiteralCodes = 257;     // 0..255 and end of block
constexpr std::uint32_t kEndOfBlock = 256;
constexpr std::uint32_t kMaxLengthCodes = 288;
constexpr std::uint8_t kExopInvalid = 128 + 64;
constexpr std::uint8_t kExopEndOfBlock = 32 + 64;
constexpr std::uint8_t kExopBase = 16 + 64;      // + extra bits
constexpr std::uint32_t kExopLength = 16;        // exop bits
constexpr std::uint32_t kExopEnd = 32;
constexpr std::uint32_t kExopError = 64;
constexpr std::uint32_t kMinFastWindow = 258;    // inflate_codes calls inflate_fast with this much room...
constexpr std::uint32_t kMinFastInput = 10;      // ...and input
constexpr std::uint32_t kLiteralLookupBits = 20; // GRABBITS before a literal/length code
constexpr std::uint32_t kDistanceLookupBits = 15;
constexpr std::uint32_t kDeflated = 8;           // Z_DEFLATED
constexpr std::uint32_t kPresetDict = 0x20;
constexpr std::uint32_t kHeaderCheck = 31;
constexpr std::uint32_t kNoSyncMarker = 5;       // sub.marker: can't try inflateSync

}  // namespace

// Purpose: Copies decoded bytes from the sliding window to the caller's
// output buffer.
std::int32_t __cdecl zlib_inflate_flush(InflateBlocks* s, ZStream* z, std::int32_t r) {
    std::uint8_t* p = z->next_out;
    std::uint8_t* q = s->read;
    const auto copy = [&](std::uint8_t* upTo) {
        std::uint32_t n = Distance(q, upTo);
        if (n > z->avail_out) {
            n = z->avail_out;
        }
        if (n != 0 && r == kZBufError) {
            r = kZOk;
        }
        z->avail_out -= n;
        z->total_out += n;
        if (s->checkfn != nullptr) {
            z->adler = s->check = reinterpret_cast<CheckFn>(s->checkfn)(s->check, q, n);
        }
        CopyBytes(p, q, n);
        p += n;
        q += n;
    };
    // As far as the write pointer, or the end of the window if it wrapped.
    copy(q <= s->write ? s->write : s->end);
    if (q == s->end) {  // the rest from the start of the window
        q = s->window;
        if (s->write == s->end) {
            s->write = s->window;
        }
        copy(s->write);
    }
    z->next_out = p;
    s->read = q;
    return r;
}

// Purpose: Builds the lookup tables of a canonical Huffman code from its code
// lengths (the bit length code, or a dynamic block's literal/length and
// distance codes).
std::int32_t __cdecl zlib_huft_build(const std::uint32_t* b, std::uint32_t n, std::uint32_t s,
                                     const std::uint32_t* d, const std::uint32_t* e, InflateHuft** t,
                                     std::uint32_t* m, InflateHuft* hp, std::uint32_t* hn, std::uint32_t* v) {
    std::uint32_t c[kMaxBits + 1] = {};  // bit length count table
    std::uint32_t x[kMaxBits + 1];       // bit offsets, then code stack
    InflateHuft* u[kMaxBits];            // table stack

    // Counts for each bit length (all lengths are at most 15).
    for (std::uint32_t i = 0; i < n; ++i) {
        ++c[b[i]];
    }
    if (c[0] == n) {  // all codes have length 0
        *t = nullptr;
        *m = 0;
        return kZOk;
    }

    // Minimum and maximum length, *m bounded by them.
    std::int32_t l = static_cast<std::int32_t>(*m);
    std::uint32_t j = 1;
    while (j <= kMaxBits && c[j] == 0) {
        ++j;
    }
    std::int32_t k = static_cast<std::int32_t>(j);  // minimum code length
    if (static_cast<std::uint32_t>(l) < j) {
        l = static_cast<std::int32_t>(j);
    }
    std::uint32_t i = kMaxBits;
    while (i != 0 && c[i] == 0) {
        --i;
    }
    const std::int32_t g = static_cast<std::int32_t>(i);  // maximum code length
    if (static_cast<std::uint32_t>(l) > i) {
        l = static_cast<std::int32_t>(i);
    }
    *m = static_cast<std::uint32_t>(l);

    // Fill out the last length count; an oversubscribed code is an error.
    std::int32_t y = 1 << j;  // dummy codes added
    for (; j < i; ++j, y <<= 1) {
        y -= static_cast<std::int32_t>(c[j]);
        if (y < 0) {
            return kZDataError;
        }
    }
    y -= static_cast<std::int32_t>(c[i]);
    if (y < 0) {
        return kZDataError;
    }
    c[i] += static_cast<std::uint32_t>(y);

    // Starting offsets into the value table for each length.
    x[1] = j = 0;
    for (std::uint32_t len = 1; len < i; ++len) {
        j += c[len];
        x[len + 1] = j;
    }

    // The values in order of bit lengths.
    for (std::uint32_t value = 0; value < n; ++value) {
        if ((j = b[value]) != 0) {
            v[x[j]++] = value;
        }
    }
    n = x[g];  // the number of values in v

    // Generate the Huffman codes and the table entries for each.
    x[0] = i = 0;                // the first code is 0
    const std::uint32_t* p = v;  // the values in bit order
    std::int32_t h = -1;         // table level: no tables yet
    std::int32_t w = -l;         // bits decoded == l * h
    u[0] = nullptr;
    InflateHuft* q = nullptr;    // the current table
    std::uint32_t z = 0;         // entries in the current table
    InflateHuft r{};             // the entry being made (padding: see the file header)

    for (; k <= g; ++k) {
        std::uint32_t a = c[k];  // codes of length k
        while (a-- != 0) {
            // Here i is the Huffman code of length k for the value *p: make
            // the tables up to the level it needs.
            while (k > w + l) {
                ++h;
                w += l;  // the previous table always has l bits

                // The smallest table of at most l bits.
                z = static_cast<std::uint32_t>(g - w);
                if (z > static_cast<std::uint32_t>(l)) {
                    z = static_cast<std::uint32_t>(l);
                }
                j = static_cast<std::uint32_t>(k - w);
                std::uint32_t f = 1u << j;
                if (f > a + 1) {  // too few codes for a (k - w)-bit table
                    f -= a + 1;
                    const std::uint32_t* xp = c + k;
                    if (j < z) {
                        while (++j < z) {  // try larger tables up to z bits
                            if ((f <<= 1) <= *++xp) {
                                break;  // enough codes to use up j bits
                            }
                            f -= *xp;
                        }
                    }
                }
                z = 1u << j;  // entries of the j-bit table

                // Allocate it from the huft space.
                if (*hn + z > kHuftEntries) {
                    return kZMemError;
                }
                u[h] = q = hp + *hn;
                *hn += z;

                // Link it from the previous table, if there is one.
                if (h != 0) {
                    x[h] = i;  // pattern for backing up
                    r.bits = static_cast<std::uint8_t>(l);
                    r.exop = static_cast<std::uint8_t>(j);
                    j = i >> (w - l);
                    r.base = static_cast<std::uint32_t>(q - u[h - 1]) - j;
                    u[h - 1][j] = r;
                } else {
                    *t = q;  // the first table is the result
                }
            }

            // The entry for this code.
            r.bits = static_cast<std::uint8_t>(k - w);
            if (p >= v + n) {
                r.exop = kExopInvalid;  // out of values: an invalid code
            } else if (*p < s) {
                r.exop = *p < kEndOfBlock ? 0 : kExopEndOfBlock;  // a literal or the end of the block
                r.base = *p++;
            } else {
                r.exop = static_cast<std::uint8_t>(e[*p - s] + kExopBase);  // extra bits from the lists
                r.base = d[*p++ - s];
            }

            // Fill every entry whose low bits are this code.
            const std::uint32_t f = 1u << (k - w);
            for (j = i >> w; j < z; j += f) {
                q[j] = r;
            }

            // Increment the k-bit code i backwards.
            for (j = 1u << (k - 1); (i & j) != 0; j >>= 1) {
                i ^= j;
            }
            i ^= j;

            // Back up over the finished tables.
            std::uint32_t mask = (1u << w) - 1;
            while ((i & mask) != x[h]) {
                --h;
                w -= l;
                mask = (1u << w) - 1;
            }
        }
    }

    // Z_BUF_ERROR for an incomplete code (unless it has a single length).
    return y != 0 && g != 1 ? kZBufError : kZOk;
}

// Purpose: The decoding table of a dynamic block's code length code.
std::int32_t __cdecl zlib_inflate_trees_bits(std::uint32_t* c, std::uint32_t* bb, InflateHuft** tb, InflateHuft* hp,
                                             ZStream* z) {
    std::uint32_t hn = 0;  // hufts used
    auto* const v = static_cast<std::uint32_t*>(Allocate(z, kBitLengthCodes, sizeof(std::uint32_t)));
    if (v == nullptr) {
        return kZMemError;
    }
    std::int32_t r = CallAt<HuftBuildFn>(zcalls::kHuftBuild)(c, kBitLengthCodes, kBitLengthCodes, nullptr, nullptr,
                                                              tb, bb, hp, &hn, v);
    if (r == kZDataError) {
        z->msg = game::texts::kZlibOversubscribedBits;
    } else if (r == kZBufError || *bb == 0) {
        z->msg = game::texts::kZlibIncompleteBits;
        r = kZDataError;
    }
    Release(z, v);
    return r;
}

// Purpose: The literal/length and distance decoding tables of a dynamic
// block.
std::int32_t __cdecl zlib_inflate_trees_dynamic(std::uint32_t nl, std::uint32_t nd, std::uint32_t* c,
                                                std::uint32_t* bl, std::uint32_t* bd, InflateHuft** tl,
                                                InflateHuft** td, InflateHuft* hp, ZStream* z) {
    std::uint32_t hn = 0;  // hufts used
    auto* const v = static_cast<std::uint32_t*>(Allocate(z, kMaxLengthCodes, sizeof(std::uint32_t)));
    if (v == nullptr) {
        return kZMemError;
    }
    const auto build = CallAt<HuftBuildFn>(zcalls::kHuftBuild);

    std::int32_t r = build(c, nl, kLiteralCodes, game::g_zlibCplens.get(), game::g_zlibCplext.get(), tl, bl, hp, &hn, v);
    if (r != kZOk || *bl == 0) {
        if (r == kZDataError) {
            z->msg = game::texts::kZlibOversubscribedLiterals;
        } else if (r != kZMemError) {
            z->msg = game::texts::kZlibIncompleteLiterals;
            r = kZDataError;
        }
        Release(z, v);
        return r;
    }

    r = build(c + nl, nd, 0, game::g_zlibCpdist.get(), game::g_zlibCpdext.get(), td, bd, hp, &hn, v);
    if (r != kZOk || (*bd == 0 && nl > kLiteralCodes)) {
        if (r == kZDataError) {
            z->msg = game::texts::kZlibOversubscribedDistances;
        } else if (r == kZBufError) {
            z->msg = game::texts::kZlibIncompleteDistances;
            r = kZDataError;
        } else if (r != kZMemError) {
            z->msg = game::texts::kZlibEmptyDistances;
            r = kZDataError;
        }
        Release(z, v);
        return r;
    }
    Release(z, v);
    return kZOk;
}

// Purpose: Decodes literals and length/distance pairs as fast as possible
// while the window has room for the longest match and the input holds the
// longest code.
std::int32_t __cdecl zlib_inflate_fast(std::uint32_t bl, std::uint32_t bd, const InflateHuft* tl,
                                       const InflateHuft* td, InflateBlocks* s, ZStream* z) {
    Window io(s, z);
    const std::uint32_t ml = Mask(bl);  // mask for the literal/length tree
    const std::uint32_t md = Mask(bd);  // mask for the distance tree

    const auto grabBits = [&io](std::uint32_t j) {  // GRABBITS: no input check
        while (io.k < j) {
            --io.n;
            io.b |= Shl(*io.p++, io.k);
            io.k += 8;
        }
    };
    // UNGRAB, UPDATE: give back the whole bytes still in the bit buffer.
    const auto leave = [&io](std::int32_t r) {
        std::uint32_t c = io.z->avail_in - io.n;
        if ((io.k >> 3) < c) {
            c = io.k >> 3;
        }
        io.n += c;
        io.p -= c;
        io.k -= c << 3;
        io.Update();
        return r;
    };

    do {  // called with m >= 258 and n >= 10
        grabBits(kLiteralLookupBits);
        const InflateHuft* t = tl + (io.b & ml);
        std::uint32_t e = t->exop;
        if (e == 0) {  // a literal
            io.DumpBits(t->bits);
            *io.q++ = static_cast<std::uint8_t>(t->base);
            --io.m;
            continue;
        }
        for (;;) {
            io.DumpBits(t->bits);
            if ((e & kExopLength) != 0) {
                // The length with its extra bits.
                e &= 15;
                std::uint32_t c = t->base + (io.b & Mask(e));
                io.DumpBits(e);

                // The distance code.
                grabBits(kDistanceLookupBits);
                t = td + (io.b & md);
                e = t->exop;
                for (;;) {
                    io.DumpBits(t->bits);
                    if ((e & kExopLength) != 0) {
                        e &= 15;
                        grabBits(e);
                        const std::uint32_t dist = t->base + (io.b & Mask(e));
                        io.DumpBits(e);

                        // Copy the string.
                        io.m -= c;
                        const std::uint8_t* r;
                        if (Distance(s->window, io.q) >= dist) {  // the source is before q in the window
                            r = io.q - dist;
                            *io.q++ = *r++;  // a match has at least three bytes
                            *io.q++ = *r++;
                            c -= 2;
                        } else {  // the source is at the end of the window
                            std::uint32_t toEnd = dist - Distance(s->window, io.q);
                            r = s->end - toEnd;
                            if (c > toEnd) {  // copy up to the end, the rest from the start
                                c -= toEnd;
                                do {
                                    *io.q++ = *r++;
                                } while (--toEnd != 0);
                                r = s->window;
                            }
                        }
                        do {
                            *io.q++ = *r++;
                        } while (--c != 0);
                        break;
                    }
                    if ((e & kExopError) != 0) {
                        z->msg = game::texts::kZlibInvalidDistanceCode;
                        return leave(kZDataError);
                    }
                    t += t->base;  // a second-level table
                    t += io.b & Mask(e);
                    e = t->exop;
                }
                break;
            }
            if ((e & kExopError) == 0) {  // a second-level table
                t += t->base;
                t += io.b & Mask(e);
                e = t->exop;
                if (e == 0) {
                    io.DumpBits(t->bits);
                    *io.q++ = static_cast<std::uint8_t>(t->base);
                    --io.m;
                    break;
                }
            } else if ((e & kExopEnd) != 0) {
                return leave(kZStreamEnd);
            } else {
                z->msg = game::texts::kZlibInvalidLiteralCode;
                return leave(kZDataError);
            }
        }
    } while (io.m >= kMinFastWindow && io.n >= kMinFastInput);

    return leave(kZOk);
}

// Purpose: Decodes the codes of one Huffman block into the window, resuming
// where the input or output ran out last time.
std::int32_t __cdecl zlib_inflate_codes(InflateBlocks* s, ZStream* z, std::int32_t r) {
    auto* const c = static_cast<InflateCodes*>(s->codes);
    Window io(s, z);
    const auto tree = [c]() { return reinterpret_cast<const InflateHuft*>(static_cast<std::uintptr_t>(c->sub08)); };
    const auto setTree = [c](const InflateHuft* t) {
        c->sub08 = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(t));
    };

    for (;;) {
        switch (c->mode) {
            case kCodesStart:
                if (io.m >= kMinFastWindow && io.n >= kMinFastInput) {
                    io.Update();
                    r = CallAt<FastFn>(zcalls::kInflateFast)(c->lbits, c->dbits, c->ltree, c->dtree, s, z);
                    io.Load();
                    if (r != kZOk) {
                        c->mode = r == kZStreamEnd ? kCodesWash : kCodesBadCode;
                        break;
                    }
                }
                c->sub0c = c->lbits;  // need
                setTree(c->ltree);
                c->mode = kCodesLen;
                [[fallthrough]];
            case kCodesLen: {  // a literal/length code
                const std::uint32_t j = c->sub0c;
                if (!io.NeedBits(j, r)) {
                    return io.Leave(r);
                }
                const InflateHuft* t = tree() + (io.b & Mask(j));
                io.DumpBits(t->bits);
                const std::uint32_t e = t->exop;
                if (e == 0) {  // a literal
                    c->sub08 = t->base;
                    c->mode = kCodesLit;
                    break;
                }
                if ((e & kExopLength) != 0) {  // a length
                    c->sub08 = e & 15;  // extra bits to get
                    c->len = t->base;
                    c->mode = kCodesLenExt;
                    break;
                }
                if ((e & kExopError) == 0) {  // the next table
                    c->sub0c = e;
                    setTree(t + t->base);
                    break;
                }
                if ((e & kExopEnd) != 0) {  // end of the block
                    c->mode = kCodesWash;
                    break;
                }
                c->mode = kCodesBadCode;
                z->msg = game::texts::kZlibInvalidLiteralCode;
                return io.Leave(kZDataError);
            }
            case kCodesLenExt: {  // the length's extra bits
                const std::uint32_t j = c->sub08;
                if (!io.NeedBits(j, r)) {
                    return io.Leave(r);
                }
                c->len += io.b & Mask(j);
                io.DumpBits(j);
                c->sub0c = c->dbits;
                setTree(c->dtree);
                c->mode = kCodesDist;
                [[fallthrough]];
            }
            case kCodesDist: {  // a distance code
                const std::uint32_t j = c->sub0c;
                if (!io.NeedBits(j, r)) {
                    return io.Leave(r);
                }
                const InflateHuft* t = tree() + (io.b & Mask(j));
                io.DumpBits(t->bits);
                const std::uint32_t e = t->exop;
                if ((e & kExopLength) != 0) {  // a distance
                    c->sub08 = e & 15;
                    c->sub0c = t->base;
                    c->mode = kCodesDistExt;
                    break;
                }
                if ((e & kExopError) == 0) {  // the next table
                    c->sub0c = e;
                    setTree(t + t->base);
                    break;
                }
                c->mode = kCodesBadCode;
                z->msg = game::texts::kZlibInvalidDistanceCode;
                return io.Leave(kZDataError);
            }
            case kCodesDistExt: {  // the distance's extra bits
                const std::uint32_t j = c->sub08;
                if (!io.NeedBits(j, r)) {
                    return io.Leave(r);
                }
                c->sub0c += io.b & Mask(j);
                io.DumpBits(j);
                c->mode = kCodesCopy;
                [[fallthrough]];
            }
            case kCodesCopy: {  // copy the string, waiting for room in the window
                const std::uint32_t dist = c->sub0c;
                const std::uint8_t* f = Distance(s->window, io.q) < dist
                                            ? s->end - (dist - Distance(s->window, io.q))
                                            : io.q - dist;
                while (c->len != 0) {
                    if (!io.NeedOut(r)) {
                        return io.Leave(r);
                    }
                    io.Output(*f++);
                    if (f == s->end) {
                        f = s->window;
                    }
                    --c->len;
                }
                c->mode = kCodesStart;
                break;
            }
            case kCodesLit:  // a literal, waiting for room in the window
                if (!io.NeedOut(r)) {
                    return io.Leave(r);
                }
                io.Output(static_cast<std::uint8_t>(c->sub08));
                c->mode = kCodesStart;
                break;
            case kCodesWash:  // end of block: flush the window
                if (io.k > 7) {  // give back the unused byte
                    io.k -= 8;
                    ++io.n;
                    --io.p;
                }
                io.FlushWindow(r);
                if (s->read != s->write) {
                    return io.Leave(r);
                }
                c->mode = kCodesEnd;
                [[fallthrough]];
            case kCodesEnd:
                return io.Leave(kZStreamEnd);
            case kCodesBadCode:
                return io.Leave(kZDataError);
            default:
                return io.Leave(kZStreamError);
        }
    }
}

// Purpose: Decodes deflate blocks (stored, fixed or dynamic Huffman) into the
// sliding window, resuming where the input or output ran out last time.
std::int32_t __cdecl zlib_inflate_blocks(InflateBlocks* s, ZStream* z, std::int32_t r) {
    Window io(s, z);
    // sub.left / sub.trees.table share the codes pointer's word.
    const auto word = [s]() { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(s->codes)); };
    const auto setWord = [s](std::uint32_t value) {
        s->codes = reinterpret_cast<void*>(static_cast<std::uintptr_t>(value));
    };
    // 258 + the numbers of literal/length and distance codes beyond the minimum.
    const auto lengthCount = [&word]() { return 258 + (word() & 0x1f) + ((word() >> 5) & 0x1f); };
    std::uint32_t*& blens = reinterpret_cast<std::uint32_t*&>(s->blens);
    std::uint32_t& index = s->sub08;
    std::uint32_t& bb = s->sub10[0];
    InflateHuft*& tb = reinterpret_cast<InflateHuft*&>(s->sub10[1]);

    for (;;) {
        switch (s->mode) {
            case kBlocksType: {
                if (!io.NeedBits(3, r)) {
                    return io.Leave(r);
                }
                const std::uint32_t t = io.b & 7;
                s->last = t & 1;
                switch (t >> 1) {
                    case 0:  // stored: skip to a byte boundary
                        io.DumpBits(3);
                        io.DumpBits(io.k & 7);
                        s->mode = kBlocksLens;
                        break;
                    case 1: {  // fixed Huffman codes
                        std::uint32_t bl, bd;
                        const InflateHuft *tl, *td;
                        CallAt<TreesFixedFn>(zcalls::kInflateTreesFixed)(&bl, &bd, &tl, &td, z);
                        s->codes = CallAt<CodesNewFn>(zcalls::kInflateCodesNew)(bl, bd, tl, td, z);
                        if (s->codes == nullptr) {
                            return io.Leave(kZMemError);
                        }
                        io.DumpBits(3);
                        s->mode = kBlocksCodes;
                        break;
                    }
                    case 2:  // dynamic Huffman codes
                        io.DumpBits(3);
                        s->mode = kBlocksTable;
                        break;
                    case 3:
                        io.DumpBits(3);
                        s->mode = kBlocksBad;
                        z->msg = game::texts::kZlibInvalidBlockType;
                        return io.Leave(kZDataError);
                }
                break;
            }
            case kBlocksLens:  // a stored block's length and its complement
                if (!io.NeedBits(32, r)) {
                    return io.Leave(r);
                }
                if (((~io.b >> 16) & 0xffff) != (io.b & 0xffff)) {
                    s->mode = kBlocksBad;
                    z->msg = game::texts::kZlibInvalidStoredLengths;
                    return io.Leave(kZDataError);
                }
                setWord(io.b & 0xffff);
                io.b = io.k = 0;
                s->mode = word() != 0 ? kBlocksStored : (s->last != 0 ? kBlocksDry : kBlocksType);
                break;
            case kBlocksStored: {  // copy a stored block
                if (io.n == 0) {
                    return io.Leave(r);
                }
                if (!io.NeedOut(r)) {
                    return io.Leave(r);
                }
                std::uint32_t t = word();
                if (t > io.n) {
                    t = io.n;
                }
                if (t > io.m) {
                    t = io.m;
                }
                CopyBytes(io.q, io.p, t);
                io.p += t;
                io.n -= t;
                io.q += t;
                io.m -= t;
                setWord(word() - t);
                if (word() != 0) {
                    break;
                }
                s->mode = s->last != 0 ? kBlocksDry : kBlocksType;
                break;
            }
            case kBlocksTable: {  // the numbers of codes of a dynamic block
                if (!io.NeedBits(14, r)) {
                    return io.Leave(r);
                }
                const std::uint32_t t = io.b & 0x3fff;
                setWord(t);
                if ((t & 0x1f) > 29 || ((t >> 5) & 0x1f) > 29) {
                    s->mode = kBlocksBad;
                    z->msg = game::texts::kZlibTooManySymbols;
                    return io.Leave(kZDataError);
                }
                blens = static_cast<std::uint32_t*>(Allocate(z, lengthCount(), sizeof(std::uint32_t)));
                if (blens == nullptr) {
                    return io.Leave(kZMemError);
                }
                io.DumpBits(14);
                index = 0;
                s->mode = kBlocksBtree;
                [[fallthrough]];
            }
            case kBlocksBtree: {  // the code length code's lengths
                while (index < 4 + (word() >> 10)) {
                    if (!io.NeedBits(3, r)) {
                        return io.Leave(r);
                    }
                    blens[game::g_zlibBorder[index++]] = io.b & 7;
                    io.DumpBits(3);
                }
                while (index < kBitLengthCodes) {
                    blens[game::g_zlibBorder[index++]] = 0;
                }
                bb = 7;
                const std::int32_t t = CallAt<TreesBitsFn>(zcalls::kInflateTreesBits)(blens, &bb, &tb,
                                                                                    static_cast<InflateHuft*>(s->hufts), z);
                if (t != kZOk) {
                    Release(z, blens);
                    if (t == kZDataError) {
                        s->mode = kBlocksBad;
                    }
                    return io.Leave(t);
                }
                index = 0;
                s->mode = kBlocksDtree;
                [[fallthrough]];
            }
            case kBlocksDtree: {  // the literal/length and distance code lengths
                while (index < lengthCount()) {
                    std::uint32_t t = bb;
                    if (!io.NeedBits(t, r)) {
                        return io.Leave(r);
                    }
                    const InflateHuft* const h = tb + (io.b & Mask(t));
                    t = h->bits;
                    std::uint32_t c = h->base;
                    if (c < 16) {  // a length
                        io.DumpBits(t);
                        blens[index++] = c;
                        continue;
                    }
                    // 16: repeat the previous length 3..6 times, 17 / 18:
                    // zeros 3..10 / 11..138 times.
                    std::uint32_t i = c == 18 ? 7 : c - 14;
                    std::uint32_t j = c == 18 ? 11 : 3;
                    if (!io.NeedBits(t + i, r)) {
                        return io.Leave(r);
                    }
                    io.DumpBits(t);
                    j += io.b & Mask(i);
                    io.DumpBits(i);
                    i = index;
                    if (i + j > lengthCount() || (c == 16 && i < 1)) {
                        Release(z, blens);
                        s->mode = kBlocksBad;
                        z->msg = game::texts::kZlibInvalidRepeat;
                        return io.Leave(kZDataError);
                    }
                    c = c == 16 ? blens[i - 1] : 0;
                    do {
                        blens[i++] = c;
                    } while (--j != 0);
                    index = i;
                }
                tb = nullptr;
                std::uint32_t bl = 9;  // at most 9 for inflate_fast's lookahead
                std::uint32_t bd = 6;
                InflateHuft *tl, *td;
                const std::uint32_t t = word();
                const std::int32_t result = CallAt<TreesDynamicFn>(zcalls::kInflateTreesDynamic)(
                    257 + (t & 0x1f), 1 + ((t >> 5) & 0x1f), blens, &bl, &bd, &tl, &td,
                    static_cast<InflateHuft*>(s->hufts), z);
                Release(z, blens);
                if (result != kZOk) {
                    if (result == kZDataError) {
                        s->mode = kBlocksBad;
                    }
                    return io.Leave(result);
                }
                auto* const codes = CallAt<CodesNewFn>(zcalls::kInflateCodesNew)(bl, bd, tl, td, z);
                if (codes == nullptr) {
                    return io.Leave(kZMemError);
                }
                s->codes = codes;
                s->mode = kBlocksCodes;
                [[fallthrough]];
            }
            case kBlocksCodes:
                io.Update();
                r = CallAt<CodesFn>(zcalls::kInflateCodes)(s, z, r);
                if (r != kZStreamEnd) {
                    return Flush(s, z, r);
                }
                r = kZOk;
                CallAt<CodesFreeFn>(zcalls::kInflateCodesFree)(static_cast<InflateCodes*>(s->codes), z);
                io.Load();
                if (s->last == 0) {
                    s->mode = kBlocksType;
                    break;
                }
                s->mode = kBlocksDry;
                [[fallthrough]];
            case kBlocksDry:  // the last block: flush the window
                io.FlushWindow(r);
                if (s->read != s->write) {
                    return io.Leave(r);
                }
                s->mode = kBlocksDone;
                [[fallthrough]];
            case kBlocksDone:
                return io.Leave(kZStreamEnd);
            case kBlocksBad:
                return io.Leave(kZDataError);
            default:
                return io.Leave(kZStreamError);
        }
    }
}

// Purpose: Decompresses a zlib stream (libpng: a PNG's IDAT and zTXt data).
std::int32_t __cdecl zlib_inflate(ZStream* z, std::int32_t flush) {
    if (z == nullptr || z->state == nullptr || z->next_in == nullptr) {
        return kZStreamError;
    }
    auto* const state = static_cast<InflateState*>(z->state);
    std::uint32_t& method = state->sub[0];
    std::uint32_t& marker = state->sub[0];
    std::uint32_t& was = state->sub[0];
    std::uint32_t& need = state->sub[1];
    const std::int32_t f = flush == kZFinish ? kZBufError : kZOk;
    std::int32_t r = kZBufError;
    // NEEDBYTE / NEXTBYTE
    const auto haveByte = [z, &r, f]() {
        if (z->avail_in == 0) {
            return false;
        }
        r = f;
        return true;
    };
    const auto nextByte = [z]() -> std::uint32_t {
        --z->avail_in;
        ++z->total_in;
        return *z->next_in++;
    };
    const auto bad = [z, state, &marker](const char* message) {
        state->mode = kInflateBad;
        z->msg = message;
        marker = kNoSyncMarker;
    };

    for (;;) {
        switch (state->mode) {
            case kInflateMethod:
                if (!haveByte()) {
                    return r;
                }
                method = nextByte();
                if ((method & 0xf) != kDeflated) {
                    bad(game::texts::kZlibUnknownMethod);
                    break;
                }
                if ((method >> 4) + 8 > state->wbits) {
                    bad(game::texts::kZlibInvalidWindowSize);
                    break;
                }
                state->mode = kInflateFlag;
                [[fallthrough]];
            case kInflateFlag: {
                if (!haveByte()) {
                    return r;
                }
                const std::uint32_t b = nextByte();
                if (((method << 8) + b) % kHeaderCheck != 0) {
                    bad(game::texts::kZlibIncorrectHeader);
                    break;
                }
                if ((b & kPresetDict) == 0) {
                    state->mode = kInflateBlocks;
                    break;
                }
                state->mode = kInflateDict4;
                [[fallthrough]];
            }
            case kInflateDict4:
                if (!haveByte()) {
                    return r;
                }
                need = nextByte() << 24;
                state->mode = kInflateDict3;
                [[fallthrough]];
            case kInflateDict3:
                if (!haveByte()) {
                    return r;
                }
                need += nextByte() << 16;
                state->mode = kInflateDict2;
                [[fallthrough]];
            case kInflateDict2:
                if (!haveByte()) {
                    return r;
                }
                need += nextByte() << 8;
                state->mode = kInflateDict1;
                [[fallthrough]];
            case kInflateDict1:
                if (!haveByte()) {
                    return r;
                }
                need += nextByte();
                z->adler = need;
                state->mode = kInflateDict0;
                return kZNeedDict;
            case kInflateDict0:
                state->mode = kInflateBad;
                z->msg = game::texts::kZlibNeedDictionary;
                marker = 0;  // can try inflateSync
                return kZStreamError;
            case kInflateBlocks:
                r = CallAt<BlocksFn>(zcalls::kInflateBlocks)(static_cast<InflateBlocks*>(state->blocks), z, r);
                if (r == kZDataError) {
                    state->mode = kInflateBad;
                    marker = 0;
                    break;
                }
                if (r == kZOk) {
                    r = f;
                }
                if (r != kZStreamEnd) {
                    return r;
                }
                r = f;
                CallAt<BlocksResetFn>(zcalls::kInflateBlocksReset)(static_cast<InflateBlocks*>(state->blocks), z,
                                                                    &was);
                if (state->nowrap != 0) {
                    state->mode = kInflateDone;
                    break;
                }
                state->mode = kInflateCheck4;
                [[fallthrough]];
            case kInflateCheck4:
                if (!haveByte()) {
                    return r;
                }
                need = nextByte() << 24;
                state->mode = kInflateCheck3;
                [[fallthrough]];
            case kInflateCheck3:
                if (!haveByte()) {
                    return r;
                }
                need += nextByte() << 16;
                state->mode = kInflateCheck2;
                [[fallthrough]];
            case kInflateCheck2:
                if (!haveByte()) {
                    return r;
                }
                need += nextByte() << 8;
                state->mode = kInflateCheck1;
                [[fallthrough]];
            case kInflateCheck1:
                if (!haveByte()) {
                    return r;
                }
                need += nextByte();
                if (was != need) {
                    bad(game::texts::kZlibIncorrectCheck);
                    break;
                }
                state->mode = kInflateDone;
                [[fallthrough]];
            case kInflateDone:
                return kZStreamEnd;
            case kInflateBad:
                return kZDataError;
            default:
                return kZStreamError;
        }
    }
}

}  // namespace s3re::thirdparty
