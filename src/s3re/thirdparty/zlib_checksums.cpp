// zlib 1.1.3's checksums and default allocator (crc32.c, adler32.c, zutil.c),
// statically linked into S3.EXE for libpng (re/picture_code.md). All cdecl.
//
// zlib::crc32 (0x005aa130): crc32(crc, buf, len). 0 for a null buffer;
//   otherwise the table-driven CRC-32 over len bytes (eight per round, then
//   the rest), with the table at 0x65c1c4.
// zlib::adler32 (0x005b13f0): adler32(adler, buf, len). 1 for a null buffer;
//   otherwise both sums are reduced modulo 65521 after every 5552 bytes
//   (NMAX), sixteen bytes per inner round.
// zlib::zcalloc (0x005b1520): zcalloc(opaque, items, size) = calloc(items,
//   size) (the C runtime's calloc 0x622199).
// zlib::zcfree (0x005b1540): zcfree(opaque, ptr) = free(ptr) (0x621b7e).

#include "game/tables.h"
#include "hooks.h"
#include "test/difftest.h"
#include "thirdparty/png_calls.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::thirdparty {

using game::g_zlibCrcTable;

namespace {

constexpr std::uint32_t kAdlerBase = 65521;  // the largest prime below 65536
constexpr std::uint32_t kAdlerNmax = 5552;   // bytes before the sums could overflow

}  // namespace

using Crc32Fn = std::uint32_t(__cdecl*)(std::uint32_t crc, const std::uint8_t* buffer, std::uint32_t length);
using Adler32Fn = std::uint32_t(__cdecl*)(std::uint32_t adler, const std::uint8_t* buffer, std::uint32_t length);
using ZcallocFn = void*(__cdecl*)(void* opaque, std::uint32_t items, std::uint32_t size);
using ZcfreeFn = void(__cdecl*)(void* opaque, void* block);

std::uint32_t __cdecl zlib_crc32(std::uint32_t crc, const std::uint8_t* buffer, std::uint32_t length);
std::uint32_t __cdecl zlib_adler32(std::uint32_t adler, const std::uint8_t* buffer, std::uint32_t length);
void* __cdecl zlib_zcalloc(void* opaque, std::uint32_t items, std::uint32_t size);
void __cdecl zlib_zcfree(void* opaque, void* block);

S3RE_HOOK(0x005aa130, zlib_crc32, Crc32Fn, Original_zlib_crc32, zlib_crc32);
S3RE_HOOK(0x005b13f0, zlib_adler32, Adler32Fn, Original_zlib_adler32, zlib_adler32);
S3RE_HOOK(0x005b1520, zlib_zcalloc, ZcallocFn, Original_zlib_zcalloc, zlib_zcalloc);
S3RE_HOOK(0x005b1540, zlib_zcfree, ZcfreeFn, Original_zlib_zcfree, zlib_zcfree);

// Purpose: The CRC-32 of a block of bytes, continuing a running value
// (libpng checks every PNG chunk with it).
std::uint32_t __cdecl zlib_crc32(std::uint32_t crc, const std::uint8_t* buffer, std::uint32_t length) {
    if (buffer == nullptr) {
        return 0;
    }
    crc = ~crc;
    for (; length > 0; --length) {  // the original unrolls eight bytes per round
        crc = g_zlibCrcTable[(crc ^ *buffer++) & 0xff] ^ (crc >> 8);
    }
    return ~crc;
}

// Purpose: The Adler-32 checksum of a block of bytes, continuing a running
// value (zlib checks the inflated data of a PNG image with it).
std::uint32_t __cdecl zlib_adler32(std::uint32_t adler, const std::uint8_t* buffer, std::uint32_t length) {
    std::uint32_t low = adler & 0xffff;
    std::uint32_t high = adler >> 16;
    if (buffer == nullptr) {
        return 1;
    }
    while (length > 0) {
        const std::uint32_t chunk = length < kAdlerNmax ? length : kAdlerNmax;
        length -= chunk;
        for (std::uint32_t i = 0; i < chunk; ++i) {  // sixteen per round in the original
            low += *buffer++;
            high += low;
        }
        low %= kAdlerBase;
        high %= kAdlerBase;
    }
    return (high << 16) | low;
}

// Purpose: zlib's default allocator (zeroed memory from the C runtime).
void* __cdecl zlib_zcalloc(void* /*opaque*/, std::uint32_t items, std::uint32_t size) {
    return calls::At<calls::CallocFn>(calls::kCalloc)(items, size);
}

// Purpose: zlib's default deallocator.
void __cdecl zlib_zcfree(void* /*opaque*/, void* block) {
    calls::At<calls::FreeFn>(calls::kFree)(block);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

struct Checksums {
    Crc32Fn crc32;
    Adler32Fn adler32;
};

void CompareChecksums(test::Context& ctx, const Checksums& candidate, int cases) {
    const Checksums original{Original_zlib_crc32, Original_zlib_adler32};
    std::vector<std::uint8_t> data(20000);
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(data.data(), data.size());
        const std::uint32_t start = ctx.rng().Chance(1, 4) ? 0 : ctx.rng().Next32();
        std::uint32_t length = ctx.rng().Below(ctx.rng().Chance(1, 2) ? 40 : 20000);
        if (ctx.rng().Chance(1, 8)) {
            length = kAdlerNmax * ctx.rng().Range(1, 3) + ctx.rng().Range(-1, 1);
        }
        const std::uint8_t* buffer = ctx.rng().Chance(1, 16) ? nullptr : data.data() + (20000 - length);
        const bool crc = ctx.rng().Chance(1, 2);
        const std::uint32_t a = crc ? original.crc32(start, buffer, length) : original.adler32(start, buffer, length);
        const std::uint32_t b = crc ? candidate.crc32(start, buffer, length) : candidate.adler32(start, buffer, length);
        if (!S3RE_EXPECT_EQ(a, b)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, %s, length %u", i, crc ? "crc32" : "adler32", length);
            return;
        }
    }
}

// Wrong: reduces the Adler sums only at the end (overflows past 5552 bytes of 0xff).
std::uint32_t __cdecl WrongAdler32(std::uint32_t adler, const std::uint8_t* buffer, std::uint32_t length) {
    if (buffer == nullptr) {
        return 1;
    }
    std::uint32_t low = adler & 0xffff, high = adler >> 16;
    for (std::uint32_t i = 0; i < length; ++i) {
        low += buffer[i];
        high += low;
    }
    return ((high % kAdlerBase) << 16) | (low % kAdlerBase);
}

}  // namespace

S3RE_TEST(ZlibChecksums_MatchOriginal) {
    CompareChecksums(ctx, {&zlib_crc32, &zlib_adler32}, 3000);
    // Known values: the CRC-32 and Adler-32 of "123456789".
    const auto* digits = reinterpret_cast<const std::uint8_t*>("123456789");
    S3RE_EXPECT_EQ(zlib_crc32(0, digits, 9), 0xcbf43926u);
    S3RE_EXPECT_EQ(zlib_adler32(1, digits, 9), 0x091e01deu);
}

S3RE_TEST(ZlibChecksums_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareChecksums(probe, {&zlib_crc32, &WrongAdler32}, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}

// zcalloc / zcfree: both allocate zeroed blocks from the game's heap and give
// them back (the calls themselves are compared by the emulator spec).
S3RE_TEST(ZlibAllocator_MatchOriginal) {
    for (int i = 0; i < 200; ++i) {
        const std::uint32_t items = ctx.rng().Range(1, 300);
        const std::uint32_t size = ctx.rng().Range(1, 64);
        auto* a = static_cast<std::uint8_t*>(Original_zlib_zcalloc(nullptr, items, size));
        auto* b = static_cast<std::uint8_t*>(zlib_zcalloc(nullptr, items, size));
        S3RE_EXPECT(a != nullptr && b != nullptr);
        if (a == nullptr || b == nullptr) {
            return;
        }
        std::vector<std::uint8_t> zero(items * size, 0);
        S3RE_EXPECT_MEM_EQ(a, zero.data(), zero.size());
        S3RE_EXPECT_MEM_EQ(b, zero.data(), zero.size());
        if (ctx.rng().Chance(1, 2)) {
            Original_zlib_zcfree(nullptr, a);
            zlib_zcfree(nullptr, b);
        } else {
            zlib_zcfree(nullptr, a);
            Original_zlib_zcfree(nullptr, b);
        }
    }
}

}  // namespace s3re::thirdparty
