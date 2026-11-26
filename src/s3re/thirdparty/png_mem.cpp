// libpng 1.0.2's memory functions (pngmem.c, png.c), statically linked into
// S3.EXE for PaintLib's PNG decoder (re/picture_code.md). All cdecl; the
// memory comes from the C runtime's malloc/free (0x621784 / 0x621b7e).
//
// png_create_struct (0x005aa000): (type): a zeroed png_struct (type 1,
//   0x20c bytes) or png_info (type 2, 0xb8 bytes); null for other types or
//   when malloc fails.
// png_destroy_struct (0x005aa050): (ptr): free(ptr) unless null.
// png_malloc (0x005aa060): (png_ptr, size): null for a null png_ptr or size
//   0; otherwise malloc(size), png_error("Out of Memory") when that fails.
// png_memcpy_check (0x005aa0c0): (png_ptr, dst, src, size): memcpy, returns
//   dst (the 16-bit size check is compiled out).
// png_memset_check (0x005aa0f0): (png_ptr, dst, value, size): memset,
//   returns dst.
// png_zalloc (0x005a4e30): zlib's allocator for libpng: (png_ptr, items,
//   size): png_malloc(items * size), zeroed (in two parts above 32 KB, as
//   the 16-bit-safe original does).
// png_zfree (0x005a4ea0): (png_ptr, ptr): png_free.
// png_create_info_struct (0x005a4f30): (png_ptr): null for a null png_ptr,
//   else png_create_struct(2), zeroed again (png_info_init inlined).

#include "game/texts.h"
#include "hooks.h"
#include "test/difftest.h"
#include "thirdparty/png_calls.h"
#include "thirdparty/png_struct.h"

#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <vector>

namespace s3re::thirdparty {

namespace {

constexpr std::uint32_t kPngStructBytes = sizeof(PngStruct);
constexpr std::uint32_t kPngInfoBytes = sizeof(PngInfo);
constexpr std::uint32_t kZallocChunk = 0x8000;  // png_zalloc clears 32 KB at a time

}  // namespace

using CreateStructFn = void*(__cdecl*)(std::int32_t type);
using DestroyStructFn = void(__cdecl*)(void* block);
using PngMallocFn = void*(__cdecl*)(PngStruct* png, std::uint32_t size);
using MemcpyCheckFn = void*(__cdecl*)(PngStruct* png, void* dst, const void* src, std::uint32_t size);
using MemsetCheckFn = void*(__cdecl*)(PngStruct* png, void* dst, std::int32_t value, std::uint32_t size);
using ZallocFn = void*(__cdecl*)(PngStruct* png, std::uint32_t items, std::uint32_t size);
using ZfreeFn = void(__cdecl*)(PngStruct* png, void* block);
using CreateInfoFn = PngInfo*(__cdecl*)(PngStruct* png);

void* __cdecl png_create_struct(std::int32_t type);
void __cdecl png_destroy_struct(void* block);
void* __cdecl png_malloc(PngStruct* png, std::uint32_t size);
void* __cdecl png_memcpy_check(PngStruct* png, void* dst, const void* src, std::uint32_t size);
void* __cdecl png_memset_check(PngStruct* png, void* dst, std::int32_t value, std::uint32_t size);
void* __cdecl png_zalloc(PngStruct* png, std::uint32_t items, std::uint32_t size);
void __cdecl png_zfree(PngStruct* png, void* block);
PngInfo* __cdecl png_create_info_struct(PngStruct* png);

S3RE_HOOK(0x005aa000, png_create_struct, CreateStructFn, Original_png_create_struct, png_create_struct);
S3RE_HOOK(0x005aa050, png_destroy_struct, DestroyStructFn, Original_png_destroy_struct, png_destroy_struct);
S3RE_HOOK(0x005aa060, png_malloc, PngMallocFn, Original_png_malloc, png_malloc);
S3RE_HOOK(0x005aa0c0, png_memcpy_check, MemcpyCheckFn, Original_png_memcpy_check, png_memcpy_check);
S3RE_HOOK(0x005aa0f0, png_memset_check, MemsetCheckFn, Original_png_memset_check, png_memset_check);
S3RE_HOOK(0x005a4e30, png_zalloc, ZallocFn, Original_png_zalloc, png_zalloc);
S3RE_HOOK(0x005a4ea0, png_zfree, ZfreeFn, Original_png_zfree, png_zfree);
S3RE_HOOK(0x005a4f30, png_create_info_struct, CreateInfoFn, Original_png_create_info_struct, png_create_info_struct);

// Purpose: Allocates libpng's main or info structure, zeroed.
void* __cdecl png_create_struct(std::int32_t type) {
    std::uint32_t size = 0;
    if (type == kPngStructInfo) {
        size = kPngInfoBytes;
    } else if (type == kPngStructPng) {
        size = kPngStructBytes;
    } else {
        return nullptr;
    }
    void* const block = calls::At<calls::MallocFn>(calls::kMalloc)(size);
    if (block != nullptr) {
        std::memset(block, 0, size);
    }
    return block;
}

// Purpose: Frees a structure made by png_create_struct.
void __cdecl png_destroy_struct(void* block) {
    if (block != nullptr) {
        calls::At<calls::FreeFn>(calls::kFree)(block);
    }
}

// Purpose: libpng's allocator; running out of memory is a fatal PNG error.
void* __cdecl png_malloc(PngStruct* png, std::uint32_t size) {
    if (png == nullptr || size == 0) {
        return nullptr;
    }
    void* const block = calls::At<calls::MallocFn>(calls::kMalloc)(size);
    if (block == nullptr) {
        calls::At<calls::PngMessageFn>(calls::kPngError)(png, game::texts::kPngOutOfMemory);
    }
    return block;
}

// Purpose: libpng's memcpy (copies decoded rows).
void* __cdecl png_memcpy_check(PngStruct* /*png*/, void* dst, const void* src, std::uint32_t size) {
    // rep movsd, then rep movsb, as the original: a forward copy, which
    // gives the same result for overlapping blocks too (the compiler would
    // turn a plain loop into memcpy).
    auto* out = static_cast<unsigned char*>(dst);
    const auto* in = static_cast<const unsigned char*>(src);
    __movsd(reinterpret_cast<unsigned long*>(out), reinterpret_cast<const unsigned long*>(in), size / 4);
    __movsb(out + (size & ~3u), in + (size & ~3u), size % 4);
    return dst;
}

// Purpose: libpng's memset (clears row buffers).
void* __cdecl png_memset_check(PngStruct* /*png*/, void* dst, std::int32_t value, std::uint32_t size) {
    std::memset(dst, value, size);
    return dst;
}

// Purpose: The allocator libpng hands to zlib: zeroed memory from png_malloc.
void* __cdecl png_zalloc(PngStruct* png, std::uint32_t items, std::uint32_t size) {
    const std::uint32_t bytes = items * size;
    auto* const block = static_cast<std::uint8_t*>(calls::At<calls::PngMallocFn>(calls::kPngMalloc)(png, bytes));
    if (bytes > kZallocChunk) {  // two memsets in the original
        std::memset(block, 0, kZallocChunk);
        std::memset(block + kZallocChunk, 0, bytes - kZallocChunk);
    } else {
        std::memset(block, 0, bytes);
    }
    return block;
}

// Purpose: The deallocator libpng hands to zlib.
void __cdecl png_zfree(PngStruct* png, void* block) {
    calls::At<calls::PngFreeFn>(calls::kPngFree)(png, block);
}

// Purpose: Allocates the info structure that receives a PNG's header data.
PngInfo* __cdecl png_create_info_struct(PngStruct* png) {
    if (png == nullptr) {
        return nullptr;
    }
    auto* const info = static_cast<PngInfo*>(calls::At<calls::PngCreateStructFn>(calls::kPngCreateStruct)(kPngStructInfo));
    if (info != nullptr) {
        std::memset(info, 0, sizeof(PngInfo));  // png_info_init
    }
    return info;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

void FreeBlock(void* block) {
    calls::At<calls::FreeFn>(calls::kFree)(block);
}

bool IsZero(const void* block, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(block);
    for (std::size_t i = 0; i < size; ++i) {
        if (bytes[i] != 0) {
            return false;
        }
    }
    return true;
}

struct CopyFunctions {
    MemcpyCheckFn memcpyCheck;
    MemsetCheckFn memsetCheck;
};

void CompareCopies(test::Context& ctx, const CopyFunctions& candidate, int cases) {
    const CopyFunctions original{Original_png_memcpy_check, Original_png_memset_check};
    std::vector<std::uint8_t> a(700), b(700);  // src + size stays inside
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(a.data(), a.size());
        b = a;
        const std::uint32_t size = ctx.rng().Below(256);
        const std::uint32_t dst = ctx.rng().Below(64);
        const std::uint32_t src = 300 + ctx.rng().Below(64);
        const std::int32_t value = static_cast<std::int32_t>(ctx.rng().Next32());
        const bool copy = ctx.rng().Chance(1, 2);
        void* ra = copy ? original.memcpyCheck(nullptr, a.data() + dst, a.data() + src, size)
                        : original.memsetCheck(nullptr, a.data() + dst, value, size);
        void* rb = copy ? candidate.memcpyCheck(nullptr, b.data() + dst, b.data() + src, size)
                        : candidate.memsetCheck(nullptr, b.data() + dst, value, size);
        bool same = S3RE_EXPECT_EQ(static_cast<std::uint8_t*>(ra) - a.data(), static_cast<std::uint8_t*>(rb) - b.data());
        same &= S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size());
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, copy %d, dst %u, src %u, size %u", i, copy ? 1 : 0, dst, src, size);
            return;
        }
    }
}

// Wrong: memset with only the low nibble of the value.
void* __cdecl WrongMemset(PngStruct* png, void* dst, std::int32_t value, std::uint32_t size) {
    return png_memset_check(png, dst, value & 0x0f, size);
}

}  // namespace

S3RE_TEST(PngMemory_CopiesMatchOriginal) {
    CompareCopies(ctx, {&png_memcpy_check, &png_memset_check}, 3000);
}

S3RE_TEST(PngMemory_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareCopies(probe, {&png_memcpy_check, &WrongMemset}, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}

// The allocators: same null results, zeroed blocks of the same size (the
// heap blocks themselves differ; the emulator spec compares the calls).
S3RE_TEST(PngMemory_AllocatorsMatchOriginal) {
    for (std::int32_t type = -1; type <= 3; ++type) {
        void* a = Original_png_create_struct(type);
        void* b = png_create_struct(type);
        const bool valid = type == kPngStructPng || type == kPngStructInfo;
        S3RE_EXPECT_EQ(a != nullptr, valid);
        S3RE_EXPECT_EQ(b != nullptr, valid);
        if (valid && a != nullptr && b != nullptr) {
            const std::size_t size = type == kPngStructPng ? kPngStructBytes : kPngInfoBytes;
            S3RE_EXPECT(IsZero(a, size));
            S3RE_EXPECT(IsZero(b, size));
            Original_png_destroy_struct(a);
            png_destroy_struct(b);
        }
    }
    Original_png_destroy_struct(nullptr);
    png_destroy_struct(nullptr);

    PngStruct png{};
    S3RE_EXPECT(Original_png_malloc(nullptr, 10) == nullptr);
    S3RE_EXPECT(png_malloc(nullptr, 10) == nullptr);
    S3RE_EXPECT(Original_png_malloc(&png, 0) == nullptr);
    S3RE_EXPECT(png_malloc(&png, 0) == nullptr);
    S3RE_EXPECT(Original_png_create_info_struct(nullptr) == nullptr);
    S3RE_EXPECT(png_create_info_struct(nullptr) == nullptr);
    for (int i = 0; i < 100; ++i) {
        const std::uint32_t items = ctx.rng().Range(1, 40);
        const std::uint32_t size = ctx.rng().Chance(1, 4) ? ctx.rng().Range(800, 2000) : ctx.rng().Range(1, 64);
        void* a = Original_png_zalloc(&png, items, size);
        void* b = png_zalloc(&png, items, size);
        S3RE_EXPECT(a != nullptr && b != nullptr && IsZero(a, items * size) && IsZero(b, items * size));
        Original_png_zfree(&png, a);
        png_zfree(&png, b);
        void* c = Original_png_malloc(&png, size);
        void* d = png_malloc(&png, size);
        S3RE_EXPECT(c != nullptr && d != nullptr);
        FreeBlock(c);
        FreeBlock(d);
        PngInfo* e = Original_png_create_info_struct(&png);
        PngInfo* f = png_create_info_struct(&png);
        S3RE_EXPECT(e != nullptr && f != nullptr && IsZero(e, sizeof(PngInfo)) && IsZero(f, sizeof(PngInfo)));
        FreeBlock(e);
        FreeBlock(f);
    }
}

}  // namespace s3re::thirdparty
