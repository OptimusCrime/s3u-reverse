// Differential tests of zlib_inflate.cpp (png-r6 batch).
//
// The streams are made here: a small deflate encoder (stored, fixed and
// dynamic Huffman blocks over greedy LZ77 matches, all window sizes, with
// and without the zlib header and adler32 trailer), sometimes corrupted
// (flipped bits, truncation, a wrong window size or check, a preset
// dictionary) so that every error path is reached. Decoding feeds the
// input and output in random chunks (1 byte up to everything, Z_FINISH or
// not), so the decoder stops and resumes in every state.
//
// All memory lives in one arena: the allocator hands out its blocks (and
// fails on a chosen call), records every call by offset, and the input and
// output buffers sit at fixed offsets in it, so the original and ours see
// the same addresses and their memory can be compared byte for byte. Bytes
// 2..3 of every huft entry are left out (huft_build stores the original's
// stack garbage there, see zlib_inflate.cpp).
//
// - ZlibInflate_FullChainMatchesOriginal: the whole original decoder (every
//   zlib function jumped to its Original_* trampoline) against all of ours,
//   call for call: return values and the z_stream after each call, the
//   allocator log, and the arena at the end.
// - ZlibInflate_FunctionsMatchOriginal: decoding with ours, and at random
//   points one function (inflate, inflate_blocks, inflate_codes,
//   inflate_fast, inflate_flush) run as the original and as ours from the
//   same state.
// - ZlibTrees_MatchOriginal: huft_build, inflate_trees_bits and
//   inflate_trees_dynamic on valid, oversubscribed, incomplete and empty
//   code lengths, with the huft space nearly full or the allocator failing.

#include "thirdparty/zlib_state.h"

#include "game/tables.h"
#include "game/texts.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

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
// zlib_inflate_setup.cpp / zlib_checksums.cpp
using TreesFixedFn = std::int32_t(__cdecl*)(std::uint32_t* bl, std::uint32_t* bd, const void** tl, const void** td,
                                            ZStream* z);
using CodesNewFn = InflateCodes*(__cdecl*)(std::uint32_t bl, std::uint32_t bd, const void* tl, const void* td,
                                           ZStream* z);
using CodesFreeFn = void(__cdecl*)(void* codes, ZStream* z);
using BlocksResetFn = void(__cdecl*)(InflateBlocks* s, ZStream* z, std::uint32_t* check);
using BlocksNewFn = InflateBlocks*(__cdecl*)(ZStream* z, void* check, std::uint32_t windowSize);
using BlocksFreeFn = std::int32_t(__cdecl*)(InflateBlocks* s, ZStream* z);
using StreamFn = std::int32_t(__cdecl*)(ZStream* z);
using Init2Fn = std::int32_t(__cdecl*)(ZStream* z, std::int32_t windowBits, const char* version,
                                       std::int32_t streamSize);
using InitFn = std::int32_t(__cdecl*)(ZStream* z, const char* version, std::int32_t streamSize);
using Adler32Fn = std::uint32_t(__cdecl*)(std::uint32_t adler, const std::uint8_t* buffer, std::uint32_t length);

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

extern InflateFn Original_zlib_inflate;
extern BlocksFn Original_zlib_inflate_blocks;
extern CodesFn Original_zlib_inflate_codes;
extern FastFn Original_zlib_inflate_fast;
extern FlushFn Original_zlib_inflate_flush;
extern HuftBuildFn Original_zlib_huft_build;
extern TreesBitsFn Original_zlib_inflate_trees_bits;
extern TreesDynamicFn Original_zlib_inflate_trees_dynamic;
extern TreesFixedFn Original_zlib_inflate_trees_fixed;
extern CodesNewFn Original_zlib_inflate_codes_new;
extern CodesFreeFn Original_zlib_inflate_codes_free;
extern BlocksResetFn Original_zlib_inflate_blocks_reset;
extern BlocksNewFn Original_zlib_inflate_blocks_new;
extern BlocksFreeFn Original_zlib_inflate_blocks_free;
extern StreamFn Original_zlib_inflateReset;
extern StreamFn Original_zlib_inflateEnd;
extern Init2Fn Original_zlib_inflateInit2_;
extern InitFn Original_zlib_inflateInit_;
extern Adler32Fn Original_zlib_adler32;

namespace {

// ---------------------------------------------------------------------------
// A small deflate encoder (RFC 1951), only for making test streams.
// ---------------------------------------------------------------------------

constexpr std::uint32_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                        31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::uint32_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                         2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::uint32_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                         33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                         1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::uint32_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                          6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
constexpr std::uint32_t kBorder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

class BitWriter {
public:
    void Bits(std::uint32_t value, std::uint32_t count) {  // LSB first
        for (std::uint32_t i = 0; i < count; ++i) {
            Put((value >> i) & 1);
        }
    }
    void Code(std::uint32_t code, std::uint32_t length) {  // Huffman codes: MSB first
        for (std::uint32_t i = length; i-- > 0;) {
            Put((code >> i) & 1);
        }
    }
    void Align() {
        while (bit_ != 0) {
            Put(0);
        }
    }
    std::vector<std::uint8_t> bytes;

private:
    void Put(std::uint32_t bit) {
        if (bit_ == 0) {
            bytes.push_back(0);
        }
        bytes.back() |= static_cast<std::uint8_t>(bit << bit_);
        bit_ = (bit_ + 1) & 7;
    }
    std::uint32_t bit_ = 0;
};

struct Token {
    std::uint16_t length;    // 0: a literal
    std::uint16_t distance;
    std::uint8_t literal;
};

std::vector<Token> Lz77(const std::vector<std::uint8_t>& data, std::uint32_t maxDistance, test::Rng& rng) {
    std::vector<Token> tokens;
    std::vector<std::int32_t> head(4096, -1), prev(data.size(), -1);
    const auto hash = [&data](std::size_t i) {
        return ((data[i] << 4) ^ (data[i + 1] << 2) ^ data[i + 2]) & 4095;
    };
    const auto insert = [&](std::size_t i) {
        if (i + 2 < data.size()) {
            const std::uint32_t h = hash(i);
            prev[i] = head[h];
            head[h] = static_cast<std::int32_t>(i);
        }
    };
    const bool literalsOnly = rng.Chance(1, 10);
    std::size_t pos = 0;
    while (pos < data.size()) {
        std::uint32_t bestLength = 0, bestDistance = 0;
        if (!literalsOnly && pos + 2 < data.size()) {
            std::int32_t candidate = head[hash(pos)];
            for (int chain = 0; candidate >= 0 && chain < 24; ++chain, candidate = prev[candidate]) {
                const std::uint32_t distance = static_cast<std::uint32_t>(pos - candidate);
                if (distance > maxDistance) {
                    break;
                }
                std::uint32_t length = 0;
                while (length < 258 && pos + length < data.size() && data[candidate + length] == data[pos + length]) {
                    ++length;
                }
                if (length > bestLength) {
                    bestLength = length;
                    bestDistance = distance;
                }
            }
        }
        if (bestLength >= 3) {
            if (rng.Chance(1, 8) && bestLength > 3) {
                bestLength = 3 + rng.Below(bestLength - 2);  // shorter matches too
            }
            tokens.push_back({static_cast<std::uint16_t>(bestLength), static_cast<std::uint16_t>(bestDistance), 0});
            for (std::uint32_t i = 0; i < bestLength; ++i) {
                insert(pos + i);
            }
            pos += bestLength;
        } else {
            tokens.push_back({0, 0, data[pos]});
            insert(pos);
            ++pos;
        }
    }
    return tokens;
}

std::uint32_t LengthSymbol(std::uint32_t length) {
    std::uint32_t i = 28;
    while (kLenBase[i] > length) {
        --i;
    }
    return i;
}

std::uint32_t DistanceSymbol(std::uint32_t distance) {
    std::uint32_t i = 29;
    while (kDistBase[i] > distance) {
        --i;
    }
    return i;
}

// Huffman code lengths of at most maxBits (frequencies flattened until they fit).
std::vector<std::uint32_t> HuffmanLengths(std::vector<std::uint32_t> freq, std::uint32_t maxBits) {
    const std::size_t n = freq.size();
    for (;;) {
        std::vector<std::uint32_t> lengths(n, 0);
        struct Node {
            std::uint64_t weight;
            int parent;
        };
        std::vector<Node> nodes;
        std::vector<int> active;
        for (std::size_t i = 0; i < n; ++i) {
            if (freq[i] != 0) {
                active.push_back(static_cast<int>(nodes.size()));
                nodes.push_back({freq[i], -1});
            }
        }
        if (nodes.empty()) {
            return lengths;
        }
        if (nodes.size() == 1) {
            for (std::size_t i = 0; i < n; ++i) {
                if (freq[i] != 0) {
                    lengths[i] = 1;
                }
            }
            return lengths;
        }
        const std::size_t leaves = nodes.size();
        while (active.size() > 1) {
            std::sort(active.begin(), active.end(),
                      [&nodes](int a, int b) { return nodes[a].weight > nodes[b].weight; });
            const int a = active.back();
            active.pop_back();
            const int b = active.back();
            active.pop_back();
            nodes[a].parent = nodes[b].parent = static_cast<int>(nodes.size());
            active.push_back(static_cast<int>(nodes.size()));
            nodes.push_back({nodes[a].weight + nodes[b].weight, -1});
        }
        std::uint32_t maxDepth = 0;
        std::size_t leaf = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (freq[i] == 0) {
                continue;
            }
            std::uint32_t depth = 0;
            for (int node = static_cast<int>(leaf); nodes[node].parent >= 0; node = nodes[node].parent) {
                ++depth;
            }
            lengths[i] = depth;
            maxDepth = std::max(maxDepth, depth);
            ++leaf;
        }
        (void)leaves;
        if (maxDepth <= maxBits) {
            return lengths;
        }
        for (auto& f : freq) {
            if (f != 0) {
                f = (f >> 1) + 1;
            }
        }
    }
}

std::vector<std::uint32_t> CanonicalCodes(const std::vector<std::uint32_t>& lengths) {
    std::uint32_t count[16] = {}, next[16] = {};
    for (const std::uint32_t l : lengths) {
        ++count[l];
    }
    count[0] = 0;
    std::uint32_t code = 0;
    for (int bits = 1; bits < 16; ++bits) {
        code = (code + count[bits - 1]) << 1;
        next[bits] = code;
    }
    std::vector<std::uint32_t> codes(lengths.size(), 0);
    for (std::size_t i = 0; i < lengths.size(); ++i) {
        if (lengths[i] != 0) {
            codes[i] = next[lengths[i]]++;
        }
    }
    return codes;
}

struct HuffmanCode {
    std::vector<std::uint32_t> lengths, codes;
};

HuffmanCode FixedLiteralCode() {
    HuffmanCode h;
    h.lengths.resize(288);
    for (std::uint32_t i = 0; i < 288; ++i) {
        h.lengths[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
    }
    h.codes = CanonicalCodes(h.lengths);
    return h;
}

HuffmanCode FixedDistanceCode() {
    HuffmanCode h;
    h.lengths.assign(30, 5);
    h.codes = CanonicalCodes(h.lengths);
    return h;
}

void WriteTokens(BitWriter& w, const Token* tokens, std::size_t count, const HuffmanCode& lit, const HuffmanCode& dist) {
    for (std::size_t i = 0; i < count; ++i) {
        const Token& t = tokens[i];
        if (t.length == 0) {
            w.Code(lit.codes[t.literal], lit.lengths[t.literal]);
            continue;
        }
        const std::uint32_t ls = LengthSymbol(t.length);
        w.Code(lit.codes[257 + ls], lit.lengths[257 + ls]);
        w.Bits(t.length - kLenBase[ls], kLenExtra[ls]);
        const std::uint32_t ds = DistanceSymbol(t.distance);
        w.Code(dist.codes[ds], dist.lengths[ds]);
        w.Bits(t.distance - kDistBase[ds], kDistExtra[ds]);
    }
    w.Code(lit.codes[256], lit.lengths[256]);
}

void WriteDynamicBlock(BitWriter& w, bool final, const Token* tokens, std::size_t count, test::Rng& rng) {
    std::vector<std::uint32_t> litFreq(286, 0), distFreq(30, 0);
    litFreq[256] = 1;
    for (std::size_t i = 0; i < count; ++i) {
        if (tokens[i].length == 0) {
            ++litFreq[tokens[i].literal];
        } else {
            ++litFreq[257 + LengthSymbol(tokens[i].length)];
            ++distFreq[DistanceSymbol(tokens[i].distance)];
        }
    }
    if (rng.Chance(1, 4)) {  // unused symbols with codes too
        for (int k = 0; k < 8; ++k) {
            ++litFreq[rng.Below(286)];
            ++distFreq[rng.Below(30)];
        }
    }
    HuffmanCode lit{HuffmanLengths(litFreq, 15), {}};
    HuffmanCode dist{HuffmanLengths(distFreq, 15), {}};
    if (std::all_of(dist.lengths.begin(), dist.lengths.end(), [](std::uint32_t l) { return l == 0; })) {
        dist.lengths[0] = 1;  // as zlib's encoder: one distance code even if unused
    }
    lit.codes = CanonicalCodes(lit.lengths);
    dist.codes = CanonicalCodes(dist.lengths);
    std::uint32_t nlit = 286, ndist = 30;
    while (nlit > 257 && lit.lengths[nlit - 1] == 0) {
        --nlit;
    }
    while (ndist > 1 && dist.lengths[ndist - 1] == 0) {
        --ndist;
    }
    std::vector<std::uint32_t> all(lit.lengths.begin(), lit.lengths.begin() + nlit);
    all.insert(all.end(), dist.lengths.begin(), dist.lengths.begin() + ndist);

    // Run-length code the lengths (16 / 17 / 18), sometimes not at all.
    struct Symbol {
        std::uint32_t symbol, extra;
    };
    std::vector<Symbol> symbols;
    const bool rle = !rng.Chance(1, 6);
    for (std::size_t i = 0; i < all.size();) {
        const std::uint32_t v = all[i];
        std::size_t run = 1;
        while (i + run < all.size() && all[i + run] == v) {
            ++run;
        }
        if (rle && v == 0 && run >= 3) {
            const std::size_t r = std::min<std::size_t>(run, run >= 11 ? 138 : 10);
            symbols.push_back(r >= 11 ? Symbol{18, static_cast<std::uint32_t>(r - 11)}
                                      : Symbol{17, static_cast<std::uint32_t>(r - 3)});
            i += r;
            continue;
        }
        symbols.push_back({v, 0});
        ++i;
        --run;
        while (rle && v != 0 && run >= 3) {
            const std::size_t r = std::min<std::size_t>(run, 6);
            symbols.push_back({16, static_cast<std::uint32_t>(r - 3)});
            i += r;
            run -= r;
        }
    }
    std::vector<std::uint32_t> clFreq(19, 0);
    for (const auto& s : symbols) {
        ++clFreq[s.symbol];
    }
    HuffmanCode cl{HuffmanLengths(clFreq, 7), {}};
    cl.codes = CanonicalCodes(cl.lengths);
    std::uint32_t hclen = 19;
    while (hclen > 4 && cl.lengths[kBorder[hclen - 1]] == 0) {
        --hclen;
    }
    w.Bits(final ? 1 : 0, 1);
    w.Bits(2, 2);
    w.Bits(nlit - 257, 5);
    w.Bits(ndist - 1, 5);
    w.Bits(hclen - 4, 4);
    for (std::uint32_t i = 0; i < hclen; ++i) {
        w.Bits(cl.lengths[kBorder[i]], 3);
    }
    for (const auto& s : symbols) {
        w.Code(cl.codes[s.symbol], cl.lengths[s.symbol]);
        if (s.symbol == 16) {
            w.Bits(s.extra, 2);
        } else if (s.symbol == 17) {
            w.Bits(s.extra, 3);
        } else if (s.symbol == 18) {
            w.Bits(s.extra, 7);
        }
    }
    WriteTokens(w, tokens, count, lit, dist);
}

// Raw deflate data of `data` in random blocks (tokens never reach back
// further than maxDistance).
std::vector<std::uint8_t> Deflate(const std::vector<std::uint8_t>& data, std::uint32_t maxDistance, test::Rng& rng) {
    const std::vector<Token> tokens = Lz77(data, maxDistance, rng);
    static const HuffmanCode fixedLit = FixedLiteralCode();
    static const HuffmanCode fixedDist = FixedDistanceCode();
    BitWriter w;
    std::size_t at = 0, dataAt = 0;
    do {
        std::size_t count = tokens.size() - at;
        if (rng.Chance(2, 3)) {
            count = std::min<std::size_t>(count, rng.Below(static_cast<std::uint32_t>(count) + 1));
        }
        std::size_t bytes = 0;
        for (std::size_t i = at; i < at + count; ++i) {
            bytes += tokens[i].length == 0 ? 1 : tokens[i].length;
        }
        const bool final = at + count == tokens.size() && rng.Chance(7, 8);
        const std::uint32_t type = rng.Below(bytes <= 65535 ? 3 : 2) == 2 ? 0 : 1 + rng.Below(2);
        if (type == 0) {  // stored
            w.Bits(final ? 1 : 0, 1);
            w.Bits(0, 2);
            w.Align();
            w.Bits(static_cast<std::uint32_t>(bytes), 16);
            w.Bits(static_cast<std::uint32_t>(~bytes) & 0xffff, 16);
            for (std::size_t i = 0; i < bytes; ++i) {
                w.Bits(data[dataAt + i], 8);
            }
        } else if (type == 1) {
            w.Bits(final ? 1 : 0, 1);
            w.Bits(1, 2);
            WriteTokens(w, tokens.data() + at, count, fixedLit, fixedDist);
        } else {
            WriteDynamicBlock(w, final, tokens.data() + at, count, rng);
        }
        at += count;
        dataAt += bytes;
        if (final) {
            break;
        }
        if (at == tokens.size()) {  // an empty last block
            w.Bits(1, 1);
            w.Bits(1, 2);
            w.Code(fixedLit.codes[256], fixedLit.lengths[256]);
            break;
        }
    } while (true);
    if (rng.Chance(1, 4)) {
        w.Bits(rng.Next32(), 13);  // trailing junk
    }
    return w.bytes;
}

std::uint32_t Adler32(const std::vector<std::uint8_t>& data) {
    std::uint32_t a = 1, b = 0;
    for (const std::uint8_t byte : data) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

std::vector<std::uint8_t> TestData(test::Rng& rng) {
    const std::uint32_t size = rng.Chance(1, 8) ? rng.Below(40) : rng.Chance(1, 3) ? 40000 + rng.Below(30000)
                                                                                  : rng.Below(9000);
    std::vector<std::uint8_t> data;
    data.reserve(size);
    const std::uint32_t alphabet = rng.Chance(1, 2) ? 256 : 2 + rng.Below(20);
    while (data.size() < size) {
        switch (rng.Below(4)) {
            case 0:  // random bytes
                for (std::uint32_t i = rng.Below(64); i-- > 0 && data.size() < size;) {
                    data.push_back(static_cast<std::uint8_t>(rng.Below(alphabet)));
                }
                break;
            case 1:  // a run
                for (std::uint32_t i = rng.Below(400), v = rng.Below(alphabet); i-- > 0 && data.size() < size;) {
                    data.push_back(static_cast<std::uint8_t>(v));
                }
                break;
            default:  // a repeat of earlier data, near or far
                if (!data.empty()) {
                    const std::size_t back = 1 + rng.Below(static_cast<std::uint32_t>(std::min<std::size_t>(data.size(), 40000)));
                    for (std::uint32_t i = rng.Below(300); i-- > 0 && data.size() < size;) {
                        data.push_back(data[data.size() - back]);
                    }
                }
                break;
        }
    }
    return data;
}

// ---------------------------------------------------------------------------
// The arena and the allocator
// ---------------------------------------------------------------------------

// (Offsets into the test arena, not the world: raw-ok.)
constexpr std::size_t kArenaSize = 0x100000;  // raw-ok
constexpr std::size_t kAllocStart = 0x10000;   // garbage before it: reads before the window stay inside
constexpr std::size_t kAllocEnd = 0xa0000;  // raw-ok
constexpr std::size_t kInputAt = 0xa0000;  // raw-ok
constexpr std::size_t kOutputAt = 0xc0000;  // raw-ok
constexpr std::size_t kOutputMax = kArenaSize - kOutputAt;

struct Arena {
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(kArenaSize);
    std::size_t used = kAllocStart;
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
    if (g_arena->calls++ == g_arena->failAt || g_arena->used + items * size > kAllocEnd) {
        return nullptr;
    }
    void* p = g_arena->bytes.data() + g_arena->used;
    g_arena->used += (items * size + 15) & ~15u;
    return p;
}

void __cdecl TestFree(void* /*opaque*/, void* block) {
    g_arena->log.insert(g_arena->log.end(), {0xf7ee, Offset(block)});
}

// Clears bytes 2..3 of every huft entry (stack garbage in the original's).
void MaskHuftPadding(std::uint8_t* bytes, std::size_t huftsAt) {
    for (std::uint32_t k = 0; k < kHuftEntries; ++k) {
        bytes[huftsAt + 8 * k + 2] = 0;
        bytes[huftsAt + 8 * k + 3] = 0;
    }
}

// Every zlib function of S3.EXE jumped to its original code, so the whole
// original decoder runs (calls between them included).
class OriginalChain {
public:
    OriginalChain() {
        Add(zcalls::kInflate, Original_zlib_inflate);
        Add(zcalls::kInflateBlocks, Original_zlib_inflate_blocks);
        Add(zcalls::kInflateCodes, Original_zlib_inflate_codes);
        Add(zcalls::kInflateFast, Original_zlib_inflate_fast);
        Add(zcalls::kInflateFlush, Original_zlib_inflate_flush);
        Add(zcalls::kHuftBuild, Original_zlib_huft_build);
        Add(zcalls::kInflateTreesBits, Original_zlib_inflate_trees_bits);
        Add(zcalls::kInflateTreesDynamic, Original_zlib_inflate_trees_dynamic);
        Add(zcalls::kInflateTreesFixed, Original_zlib_inflate_trees_fixed);
        Add(zcalls::kInflateCodesNew, Original_zlib_inflate_codes_new);
        Add(zcalls::kInflateCodesFree, Original_zlib_inflate_codes_free);
        Add(zcalls::kInflateBlocksReset, Original_zlib_inflate_blocks_reset);
        Add(0x005b05a0, Original_zlib_inflate_blocks_new);
        Add(0x005b1360, Original_zlib_inflate_blocks_free);
        Add(0x005aa270, Original_zlib_inflateReset);
        Add(0x005aa2c0, Original_zlib_inflateEnd);
        Add(0x005aa310, Original_zlib_inflateInit2_);
        Add(0x005aa480, Original_zlib_inflateInit_);
        Add(0x005b13f0, Original_zlib_adler32);
    }

private:
    template <typename Fn>
    void Add(std::uintptr_t address, Fn original) {
        if (reinterpret_cast<std::uintptr_t>(original) != address) {  // hooked: a trampoline
            jumps_.push_back(std::make_unique<test::ScopedJump>(address, reinterpret_cast<const void*>(original)));
        }
    }
    std::vector<std::unique_ptr<test::ScopedJump>> jumps_;
};

// ---------------------------------------------------------------------------
// Test streams and their decoding
// ---------------------------------------------------------------------------

struct StreamCase {
    std::vector<std::uint8_t> stream;
    std::int32_t windowBits = 15;  // inflateInit2_'s argument (negative: raw deflate)
    std::uint32_t outCapacity = 0;
    int failAt = -1;
    std::uint64_t chunkSeed = 0;
};

StreamCase MakeCase(test::Rng& rng) {
    StreamCase c;
    const std::vector<std::uint8_t> data = TestData(rng);
    const std::int32_t bits = 8 + static_cast<std::int32_t>(rng.Below(8));
    const bool raw = rng.Chance(1, 4);
    c.windowBits = raw ? -bits : bits;
    std::vector<std::uint8_t> deflated = Deflate(data, 1u << bits, rng);
    if (raw) {
        c.stream = deflated;
    } else {
        std::uint32_t cinfo = static_cast<std::uint32_t>(bits - 8);
        if (rng.Chance(1, 20)) {
            cinfo = rng.Below(16);  // perhaps larger than the window
        }
        const std::uint32_t cmf = (cinfo << 4) | (rng.Chance(1, 30) ? rng.Below(16) : 8);
        const bool dict = rng.Chance(1, 25);
        std::uint32_t flg = (rng.Below(4) << 6) | (dict ? 0x20 : 0);
        flg |= (31 - (cmf * 256 + flg) % 31) % 31;
        if (rng.Chance(1, 30)) {
            flg ^= 1u << rng.Below(5);  // a wrong header check
        }
        c.stream = {static_cast<std::uint8_t>(cmf), static_cast<std::uint8_t>(flg)};
        if (dict) {
            for (int k = 0; k < 4; ++k) {
                c.stream.push_back(static_cast<std::uint8_t>(rng.Next32()));
            }
        }
        c.stream.insert(c.stream.end(), deflated.begin(), deflated.end());
        std::uint32_t adler = Adler32(data);
        if (rng.Chance(1, 20)) {
            adler ^= 1u << rng.Below(32);
        }
        for (int k = 3; k >= 0; --k) {
            c.stream.push_back(static_cast<std::uint8_t>(adler >> (8 * k)));
        }
    }
    if (rng.Chance(1, 4) && !c.stream.empty()) {  // corrupt it
        for (std::uint32_t k = 1 + rng.Below(3); k-- > 0;) {
            c.stream[rng.Below(static_cast<std::uint32_t>(c.stream.size()))] ^= static_cast<std::uint8_t>(1u << rng.Below(8));
        }
    }
    if (rng.Chance(1, 10) && !c.stream.empty()) {
        c.stream.resize(rng.Below(static_cast<std::uint32_t>(c.stream.size())));
    }
    if (rng.Chance(1, 30)) {  // garbage
        c.stream.resize(rng.Below(400));
        rng.Fill(c.stream.data(), c.stream.size());
    }
    c.stream.resize(std::min<std::size_t>(c.stream.size(), kOutputAt - kInputAt));
    c.outCapacity = static_cast<std::uint32_t>(std::min<std::size_t>(
        kOutputMax, data.size() + (rng.Chance(1, 5) ? rng.Below(64) : 300)));
    c.failAt = rng.Chance(1, 6) ? static_cast<int>(rng.Below(12)) : -1;
    c.chunkSeed = rng.Next64();
    return c;
}

std::uint32_t PickChunk(test::Rng& rng) {
    static const std::uint32_t kChunks[] = {0, 1, 1, 2, 3, 7, 10, 17, 64, 300, 1000, 5000, 0x7fffffff};
    return kChunks[rng.Below(sizeof(kChunks) / sizeof(kChunks[0]))];
}

// Everything a call leaves that is compared besides the arena.
struct StepRecord {
    std::int32_t ret;
    ZStream z;
};

ZStream* g_stream = nullptr;  // the case's stream, in the arena

// Fills the arena (garbage before and in the allocation space, the input)
// and starts the stream with inflateInit2_ (through S3.EXE).
bool StartStream(Arena& arena, const StreamCase& c, test::Rng& garbage) {
    garbage.Fill(arena.bytes.data(), kAllocEnd);
    std::memset(arena.bytes.data() + kInputAt, 0xee, kArenaSize - kInputAt);
    if (!c.stream.empty()) {
        std::memcpy(arena.bytes.data() + kInputAt, c.stream.data(), c.stream.size());
    }
    arena.used = kAllocStart;
    arena.calls = 0;
    arena.failAt = -1;
    arena.log.clear();
    g_arena = &arena;
    auto* z = reinterpret_cast<ZStream*>(arena.bytes.data() + 0x100);
    std::memset(z, 0, sizeof(*z));
    z->zalloc = reinterpret_cast<void*>(&TestAlloc);
    z->zfree = reinterpret_cast<void*>(&TestFree);
    z->next_in = arena.bytes.data() + kInputAt;
    z->next_out = arena.bytes.data() + kOutputAt;
    g_stream = z;
    const std::int32_t ret = reinterpret_cast<Init2Fn>(0x005aa310)(z, c.windowBits, game::texts::kZlibVersion,
                                                                   sizeof(ZStream));
    arena.failAt = c.failAt < 0 ? -1 : arena.calls + c.failAt;
    return ret == kZOk;
}

InflateBlocks* BlocksOf(ZStream* z) {
    return static_cast<InflateBlocks*>(static_cast<InflateState*>(z->state)->blocks);
}

// Gives the next input and output chunks.
void FeedChunks(Arena& arena, const StreamCase& c, ZStream* z, test::Rng& chunks) {
    const std::uint32_t inPos = static_cast<std::uint32_t>(z->next_in - (arena.bytes.data() + kInputAt));
    const std::uint32_t outPos = static_cast<std::uint32_t>(z->next_out - (arena.bytes.data() + kOutputAt));
    const std::uint32_t inLeft = static_cast<std::uint32_t>(c.stream.size()) - std::min<std::uint32_t>(
                                                                                   inPos, static_cast<std::uint32_t>(c.stream.size()));
    z->avail_in = std::min(PickChunk(chunks), inLeft);
    z->avail_out = std::min(PickChunk(chunks), c.outCapacity - std::min(outPos, c.outCapacity));
}

bool Terminal(std::int32_t ret) {
    return ret != kZOk && ret != kZBufError;
}

// Decodes the case with inflate (whichever functions S3.EXE's addresses lead
// to), recording every call.
void Decode(Arena& arena, const StreamCase& c, std::vector<StepRecord>& records) {
    test::Rng chunks(c.chunkSeed);
    ZStream* const z = g_stream;
    int idle = 0, afterEnd = 0;
    for (int step = 0; step < 3000 && idle < 6 && afterEnd < 2; ++step) {
        FeedChunks(arena, c, z, chunks);
        const std::uint32_t before = z->total_in + z->total_out;
        const std::int32_t flush = chunks.Chance(1, 3) ? kZFinish : 0;
        const std::int32_t ret = reinterpret_cast<InflateFn>(zcalls::kInflate)(z, flush);
        records.push_back({ret, *z});
        idle = z->total_in + z->total_out == before ? idle + 1 : 0;
        if (Terminal(ret)) {
            ++afterEnd;
        }
    }
}

bool SameRecords(test::Context& ctx, const std::vector<StepRecord>& a, const std::vector<StepRecord>& b) {
    bool same = S3RE_EXPECT_EQ(a.size(), b.size());
    for (std::size_t i = 0; same && i < a.size(); ++i) {
        same &= S3RE_EXPECT_EQ(a[i].ret, b[i].ret);
        same &= S3RE_EXPECT_MEM_EQ(&a[i].z, &b[i].z, sizeof(ZStream));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  at call %d", static_cast<int>(i));
        }
    }
    return same;
}

// The functions compared one at a time from the same state.
struct DecoderFunctions {
    InflateFn inflate;
    BlocksFn blocks;
    CodesFn codes;
    FastFn fast;
    FlushFn flush;
};

const DecoderFunctions kOurs{&zlib_inflate, &zlib_inflate_blocks, &zlib_inflate_codes, &zlib_inflate_fast,
                             &zlib_inflate_flush};

std::int32_t RunProbe(const DecoderFunctions& f, int which, ZStream* z, std::int32_t r, std::int32_t flush) {
    InflateBlocks* const s = BlocksOf(z);
    switch (which) {
        case 0: return f.inflate(z, flush);
        case 1: return f.blocks(s, z, r);
        case 2: return f.codes(s, z, r);
        case 3: {
            const auto* c = static_cast<const InflateCodes*>(s->codes);
            return f.fast(c->lbits, c->dbits, c->ltree, c->dtree, s, z);
        }
        default: return f.flush(s, z, r);
    }
}

// Which probes the state allows (inflate_fast only with 258 bytes of room
// and 10 of input, as inflate_codes calls it).
bool ProbeAllowed(int which, ZStream* z) {
    if (which == 0) {
        return true;
    }
    const auto* state = static_cast<const InflateState*>(z->state);
    if (state->mode != kInflateBlocks) {
        return false;
    }
    const InflateBlocks* s = BlocksOf(z);
    if (which == 4 || which == 1) {
        return true;
    }
    if (s->mode != kBlocksCodes || s->codes == nullptr) {
        return false;
    }
    if (which == 2) {
        return true;
    }
    const auto* c = static_cast<const InflateCodes*>(s->codes);
    const std::uint32_t room = s->write < s->read ? static_cast<std::uint32_t>(s->read - s->write) - 1
                                                  : static_cast<std::uint32_t>(s->end - s->write);
    return c->mode == kCodesStart && room >= 258 && z->avail_in >= 10;
}

// Decodes with ours; at random calls, one function as the original and as
// `candidate` from the same state, compared.
void DecodeWithProbes(test::Context& ctx, Arena& arena, const StreamCase& c, const DecoderFunctions& candidate,
                      std::uint64_t probeSeed, int& probes) {
    test::Rng chunks(c.chunkSeed), pick(probeSeed);
    ZStream* const z = g_stream;
    const DecoderFunctions original{Original_zlib_inflate, Original_zlib_inflate_blocks, Original_zlib_inflate_codes,
                                    Original_zlib_inflate_fast, Original_zlib_inflate_flush};
    const std::size_t huftsAt = Offset(BlocksOf(z)->hufts);
    std::vector<std::uint8_t> before(kArenaSize), afterOriginal(kArenaSize);
    int idle = 0, afterEnd = 0, probesHere = 0;
    for (int step = 0; step < 3000 && idle < 6 && afterEnd < 2; ++step) {
        FeedChunks(arena, c, z, chunks);
        const std::uint32_t total = z->total_in + z->total_out;
        const std::int32_t flush = chunks.Chance(1, 3) ? kZFinish : 0;
        const int which = static_cast<int>(pick.Below(5));
        std::int32_t ret;
        if (probesHere < 10 && pick.Chance(1, 4) && ProbeAllowed(which, z)) {
            ++probesHere;
            const std::int32_t r = pick.Chance(1, 2) ? kZBufError : kZOk;
            std::memcpy(before.data(), arena.bytes.data(), kArenaSize);
            const std::size_t used = arena.used;
            const int calls = arena.calls;
            const std::size_t logSize = arena.log.size();
            const std::int32_t ra = RunProbe(original, which, z, r, flush);
            const std::vector<std::uint32_t> logA(arena.log.begin() + logSize, arena.log.end());
            std::memcpy(afterOriginal.data(), arena.bytes.data(), kArenaSize);
            std::memcpy(arena.bytes.data(), before.data(), kArenaSize);
            arena.used = used;
            arena.calls = calls;
            arena.log.resize(logSize);
            ret = RunProbe(candidate, which, z, r, flush);
            const std::vector<std::uint32_t> logB(arena.log.begin() + logSize, arena.log.end());
            std::vector<std::uint8_t>& mine = before;  // reuse: ours, masked
            std::memcpy(mine.data(), arena.bytes.data(), kArenaSize);
            MaskHuftPadding(afterOriginal.data(), huftsAt);
            MaskHuftPadding(mine.data(), huftsAt);
            bool same = S3RE_EXPECT_EQ(ra, ret);
            same &= S3RE_EXPECT(logA == logB);
            same &= S3RE_EXPECT_MEM_EQ(afterOriginal.data(), mine.data(), kArenaSize);
            ++probes;
            if (!same) {
                ctx.Fail(__FILE__, __LINE__, "  probe of function %d at call %d", which, step);
                return;
            }
        } else {
            ret = zlib_inflate(z, flush);
        }
        idle = z->total_in + z->total_out == total ? idle + 1 : 0;
        if (Terminal(ret)) {
            ++afterEnd;
        }
    }
}

void CompareProbes(test::Context& ctx, const DecoderFunctions& candidate, int cases) {
    auto arena = std::make_unique<Arena>();
    int probes = 0;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        const StreamCase c = MakeCase(ctx.rng());
        test::Rng garbage(ctx.rng().Next64());
        if (!StartStream(*arena, c, garbage)) {
            continue;
        }
        DecodeWithProbes(ctx, *arena, c, candidate, ctx.rng().Next64(), probes);
        if (ctx.failures() != 0) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (stream of %d bytes, window bits %d)", i,
                     static_cast<int>(c.stream.size()), c.windowBits);
        }
    }
    g_arena = nullptr;
    S3RE_EXPECT(probes > cases);
}

// Wrong: inflate_flush keeps Z_BUF_ERROR after copying something.
std::int32_t __cdecl WrongFlush(InflateBlocks* s, ZStream* z, std::int32_t r) {
    const std::int32_t result = zlib_inflate_flush(s, z, r);
    return r == kZBufError ? r : result;
}

}  // namespace

S3RE_TEST(ZlibInflate_FullChainMatchesOriginal) {
    auto arena = std::make_unique<Arena>();
    std::vector<std::uint8_t> initial(kArenaSize), afterOriginal(kArenaSize);
    int decoded = 0, errors = 0;
    for (int i = 0; i < 400; ++i) {
        const StreamCase c = MakeCase(ctx.rng());
        const std::uint64_t garbageSeed = ctx.rng().Next64();
        std::vector<StepRecord> recordsA, recordsB;
        std::vector<std::uint32_t> logA;
        std::size_t huftsA = 0, huftsB = 0;
        bool startedA, startedB;
        {
            OriginalChain chain;
            test::Rng garbage(garbageSeed);
            startedA = StartStream(*arena, c, garbage);
            if (startedA) {
                huftsA = Offset(BlocksOf(g_stream)->hufts);
                Decode(*arena, c, recordsA);
            }
        }
        logA = arena->log;
        std::memcpy(afterOriginal.data(), arena->bytes.data(), kArenaSize);
        {
            test::Rng garbage(garbageSeed);
            startedB = StartStream(*arena, c, garbage);
            if (startedB) {
                huftsB = Offset(BlocksOf(g_stream)->hufts);
                Decode(*arena, c, recordsB);
            }
        }
        bool same = S3RE_EXPECT_EQ(startedA, startedB);
        same &= S3RE_EXPECT_EQ(huftsA, huftsB);
        same &= SameRecords(ctx, recordsA, recordsB);
        same &= S3RE_EXPECT(logA == arena->log);
        if (startedA) {
            MaskHuftPadding(afterOriginal.data(), huftsA);
            MaskHuftPadding(arena->bytes.data(), huftsA);
        }
        same &= S3RE_EXPECT_MEM_EQ(afterOriginal.data(), arena->bytes.data(), kArenaSize);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (stream of %d bytes, window bits %d)", i,
                     static_cast<int>(c.stream.size()), c.windowBits);
            break;
        }
        if (!recordsB.empty()) {
            const std::int32_t last = recordsB.back().ret;
            decoded += last == kZStreamEnd;
            errors += last == kZDataError;
        }
    }
    g_arena = nullptr;
    // Both outcomes must be common, or the cases don't test much.
    S3RE_EXPECT(decoded > 100);
    S3RE_EXPECT(errors > 40);
}

S3RE_TEST(ZlibInflate_FunctionsMatchOriginal) {
    CompareProbes(ctx, kOurs, 300);
}

S3RE_TEST(ZlibInflate_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    DecoderFunctions wrong = kOurs;
    wrong.flush = &WrongFlush;
    CompareProbes(probe, wrong, 300);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

// Code lengths: valid (from frequencies), with one length changed
// (oversubscribed or incomplete), random, all zero or a single code.
std::vector<std::uint32_t> TestLengths(test::Rng& rng, std::size_t n, std::uint32_t maxBits) {
    std::vector<std::uint32_t> lengths(n, 0);
    switch (rng.Below(6)) {
        case 0:
        case 1:
        case 2: {
            std::vector<std::uint32_t> freq(n);
            for (auto& f : freq) {
                f = rng.Chance(1, 4) ? 0 : 1 + rng.Below(rng.Chance(1, 2) ? 4 : 5000);
            }
            lengths = HuffmanLengths(freq, maxBits);
            if (rng.Chance(1, 3)) {
                const std::size_t k = rng.Below(static_cast<std::uint32_t>(n));
                lengths[k] = rng.Below(maxBits + 1);
            }
            break;
        }
        case 3:
            for (auto& l : lengths) {
                l = rng.Chance(1, 3) ? 0 : rng.Below(maxBits + 1);
            }
            break;
        case 4:
            break;
        default:
            lengths[rng.Below(static_cast<std::uint32_t>(n))] = 1 + rng.Below(maxBits);
            break;
    }
    return lengths;
}

struct TreeFunctions {
    HuftBuildFn build;
    TreesBitsFn bits;
    TreesDynamicFn dynamic;
};

void CompareTrees(test::Context& ctx, const TreeFunctions& candidate, int cases) {
    const TreeFunctions original{Original_zlib_huft_build, Original_zlib_inflate_trees_bits,
                                 Original_zlib_inflate_trees_dynamic};
    auto arena = std::make_unique<Arena>();
    constexpr std::size_t kHp = 0x1000, kV = 0x8000, kLengths = 0x9000, kOuts = 0xa000, kStream = 0xa100;
    constexpr std::size_t kCompare = 0x10000;
    std::vector<std::uint8_t> initial(kCompare), afterOriginal(kCompare);
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(3));
        ctx.rng().Fill(arena->bytes.data(), kCompare);
        std::uint32_t n = 19, s = 19;
        const std::uint32_t* d = nullptr;
        const std::uint32_t* e = nullptr;
        std::vector<std::uint32_t> lengths;
        std::uint32_t nl = 0, nd = 0;
        if (op == 0) {
            switch (ctx.rng().Below(3)) {
                case 0:  // bit length codes
                    lengths = TestLengths(ctx.rng(), 19, 7);
                    break;
                case 1:  // literal/length codes
                    n = 257 + ctx.rng().Below(32);
                    s = 257;
                    d = game::g_zlibCplens.get();
                    e = game::g_zlibCplext.get();
                    lengths = TestLengths(ctx.rng(), n, 15);
                    break;
                default:  // distance codes
                    n = 1 + ctx.rng().Below(30);
                    s = 0;
                    d = game::g_zlibCpdist.get();
                    e = game::g_zlibCpdext.get();
                    lengths = TestLengths(ctx.rng(), n, 15);
                    break;
            }
        } else if (op == 1) {
            lengths = TestLengths(ctx.rng(), 19, 7);
        } else {
            nl = 257 + ctx.rng().Below(30);
            nd = 1 + ctx.rng().Below(30);
            lengths = TestLengths(ctx.rng(), nl, 15);
            const std::vector<std::uint32_t> dist = TestLengths(ctx.rng(), nd, 15);
            lengths.insert(lengths.end(), dist.begin(), dist.end());
        }
        std::memcpy(arena->bytes.data() + kLengths, lengths.data(), lengths.size() * 4);
        auto* outs = reinterpret_cast<std::uint32_t*>(arena->bytes.data() + kOuts);
        outs[0] = ctx.rng().Chance(1, 4) ? ctx.rng().Below(16) : 9;  // *m / *bb / *bl
        outs[1] = 6;                                                 // *bd
        outs[4] = ctx.rng().Chance(1, 5) ? kHuftEntries - ctx.rng().Below(600) : ctx.rng().Below(40);  // *hn
        auto* z = reinterpret_cast<ZStream*>(arena->bytes.data() + kStream);
        std::memset(z, 0, sizeof(*z));
        z->zalloc = reinterpret_cast<void*>(&TestAlloc);
        z->zfree = reinterpret_cast<void*>(&TestFree);
        const int failAt = ctx.rng().Chance(1, 8) ? 0 : -1;
        std::memcpy(initial.data(), arena->bytes.data(), kCompare);

        const auto run = [&](const TreeFunctions& f) {
            arena->used = kCompare;
            arena->calls = 0;
            arena->failAt = failAt;
            arena->log.clear();
            g_arena = arena.get();
            auto* hp = reinterpret_cast<InflateHuft*>(arena->bytes.data() + kHp);
            auto* c = reinterpret_cast<std::uint32_t*>(arena->bytes.data() + kLengths);
            auto** t1 = reinterpret_cast<InflateHuft**>(outs + 2);
            auto** t2 = reinterpret_cast<InflateHuft**>(outs + 3);
            switch (op) {
                case 0:
                    return f.build(c, n, s, d, e, t1, outs, hp, outs + 4,
                                   reinterpret_cast<std::uint32_t*>(arena->bytes.data() + kV));
                case 1: return f.bits(c, outs, t1, hp, z);
                default: return f.dynamic(nl, nd, c, outs, outs + 1, t1, t2, hp, z);
            }
        };
        const std::int32_t ra = run(original);
        const std::vector<std::uint32_t> logA = arena->log;
        MaskHuftPadding(arena->bytes.data(), kHp);
        std::memcpy(afterOriginal.data(), arena->bytes.data(), kCompare);
        std::memcpy(arena->bytes.data(), initial.data(), kCompare);
        const std::int32_t rb = run(candidate);
        MaskHuftPadding(arena->bytes.data(), kHp);
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT(logA == arena->log);
        same &= S3RE_EXPECT_MEM_EQ(afterOriginal.data(), arena->bytes.data(), kCompare);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
            break;
        }
    }
    g_arena = nullptr;
}

// Wrong: huft_build reports incomplete codes of one length as complete too.
std::int32_t __cdecl WrongHuftBuild(const std::uint32_t* b, std::uint32_t n, std::uint32_t s, const std::uint32_t* d,
                                    const std::uint32_t* e, InflateHuft** t, std::uint32_t* m, InflateHuft* hp,
                                    std::uint32_t* hn, std::uint32_t* v) {
    const std::int32_t r = zlib_huft_build(b, n, s, d, e, t, m, hp, hn, v);
    return r == kZBufError ? kZOk : r;
}

}  // namespace

S3RE_TEST(ZlibTrees_MatchOriginal) {
    CompareTrees(ctx, {&zlib_huft_build, &zlib_inflate_trees_bits, &zlib_inflate_trees_dynamic}, 4000);
}

S3RE_TEST(ZlibTrees_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareTrees(probe, {&WrongHuftBuild, &zlib_inflate_trees_bits, &zlib_inflate_trees_dynamic}, 4000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::thirdparty
