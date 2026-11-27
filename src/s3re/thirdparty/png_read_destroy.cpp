// libpng 1.0.2's teardown of a read struct (pngread.c, png.c), statically
// linked into S3.EXE (re/picture_code.md). All cdecl.
//
// png_info_destroy (0x005a4f60): (png_ptr, info_ptr): frees the texts' keys
//   and the text array, the pCAL purpose, units and parameters, then clears
//   the info structure (png_info_init).
// png_read_destroy (0x005a5b90): (png_ptr, info_ptr, end_info_ptr): destroys
//   both infos, frees every buffer and table of the read struct (the palette,
//   tRNS and hIST only when the struct allocated them), ends the inflate
//   stream and clears the struct, keeping its jmp_buf, error and warning
//   callbacks and error pointer.
// png_destroy_read_struct (0x005a5b00): (&png_ptr, &info_ptr, &end_info_ptr):
//   png_read_destroy, then the structures themselves; the pointers are set to
//   null.

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

using InfoDestroyFn = void(__cdecl*)(PngStruct* png, PngInfo* info);
using ReadDestroyFn = void(__cdecl*)(PngStruct* png, PngInfo* info, PngInfo* endInfo);
using DestroyReadStructFn = void(__cdecl*)(PngStruct** png, PngInfo** info, PngInfo** endInfo);

void __cdecl png_info_destroy(PngStruct* png, PngInfo* info);
void __cdecl png_read_destroy(PngStruct* png, PngInfo* info, PngInfo* endInfo);
void __cdecl png_destroy_read_struct(PngStruct** png, PngInfo** info, PngInfo** endInfo);

S3RE_HOOK(0x005a4f60, png_info_destroy, InfoDestroyFn, Original_png_info_destroy, png_info_destroy);
S3RE_HOOK(0x005a5b90, png_read_destroy, ReadDestroyFn, Original_png_read_destroy, png_read_destroy);
S3RE_HOOK(0x005a5b00, png_destroy_read_struct, DestroyReadStructFn, Original_png_destroy_read_struct,
          png_destroy_read_struct);

namespace {

using DestroyStructFn = void(__cdecl*)(void* block);
using StreamFn = std::int32_t(__cdecl*)(ZStream* z);

constexpr std::uintptr_t kPngInfoDestroy = 0x005a4f60;
constexpr std::uintptr_t kPngReadDestroy = 0x005a5b90;
constexpr std::uintptr_t kPngDestroyStruct = 0x005aa050;
constexpr std::uintptr_t kPngZfree = 0x005a4ea0;
constexpr std::uintptr_t kInflateEnd = 0x005aa2c0;
constexpr std::uint32_t kPngFlagFreePalette = 0x1000;

void Free(PngStruct* png, void* block) {
    calls::At<calls::PngFreeFn>(calls::kPngFree)(png, block);
}

// The fields png_read_destroy frees that png_struct.h doesn't name yet.
std::uint8_t*& Field(PngStruct* png, std::size_t offset) {
    return *reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uint8_t*>(png) + offset);  // raw-ok: pngread.c fields
}

constexpr std::size_t kGammaTable = 0x15c;      // gamma_table, gamma_from_1, gamma_to_1 (test)
constexpr std::size_t kGamma16Tables = 0x168;   // gamma_16_table, gamma_16_from_1, gamma_16_to_1 (test)
constexpr std::size_t kPaletteLookup = 0x1e4;   // palette_lookup, dither_index
constexpr std::size_t kSaveBuffer = 0x1a8;      // the progressive reader's save_buffer
constexpr std::size_t kTimeBuffer = 0x208;      // time_buffer (RFC 1123 text)

}  // namespace

// Purpose: Frees what an info structure owns (texts, pCAL) and clears it.
void __cdecl png_info_destroy(PngStruct* png, PngInfo* info) {
    if (info->text != nullptr) {
        auto* const texts = static_cast<PngText*>(info->text);
        for (std::int32_t i = 0; i < info->num_text; ++i) {
            Free(png, texts[i].key);  // the key's block holds the text too
        }
        Free(png, info->text);
    }
    Free(png, info->pcal_purpose);
    Free(png, info->pcal_units);
    if (info->pcal_params != nullptr) {
        for (std::int32_t i = 0; i < info->pcal_nparams; ++i) {
            Free(png, info->pcal_params[i]);
        }
        Free(png, info->pcal_params);
    }
    std::memset(info, 0, sizeof(PngInfo));  // png_info_init
}

// Purpose: Frees everything a read struct owns and clears it (keeping the
// error handling).
void __cdecl png_read_destroy(PngStruct* png, PngInfo* info, PngInfo* endInfo) {
    const auto infoDestroy = calls::At<InfoDestroyFn>(kPngInfoDestroy);
    if (info != nullptr) {
        infoDestroy(png, info);
    }
    if (endInfo != nullptr) {
        infoDestroy(png, endInfo);
    }
    Free(png, png->zbuf);
    Free(png, png->row_buf);
    Free(png, png->prev_row);
    Free(png, Field(png, kPaletteLookup));
    Free(png, Field(png, kPaletteLookup + 4));
    Free(png, png->gamma_table);
    Free(png, png->gamma_from_1);
    Free(png, png->gamma_to_1);
    if ((png->flags & kPngFlagFreePalette) != 0) {
        calls::At<calls::PngFreeFn>(kPngZfree)(png, png->palette);
    }
    if ((png->flags & kPngFlagFreeTrans) != 0) {
        Free(png, png->trans);
    }
    if ((png->flags & kPngFlagFreeHist) != 0) {
        Free(png, png->hist);
    }
    // The 16-bit gamma tables: 1 << (8 - gamma_shift) rows each.
    for (std::uint16_t** const table : {png->gamma_16_table, png->gamma_16_from_1, png->gamma_16_to_1}) {
        if (table != nullptr) {
            const std::int32_t rows = static_cast<std::int32_t>(1u << ((8 - png->gamma_shift) & 31));  // (x86 shift count)
            for (std::int32_t i = 0; i < rows; ++i) {
                Free(png, table[i]);
            }
        }
        Free(png, table);
    }
    Free(png, Field(png, kTimeBuffer));
    calls::At<StreamFn>(kInflateEnd)(&png->zstream);
    Free(png, Field(png, kSaveBuffer));

    // Clear the struct but its jmp_buf, callbacks and error pointer.
    std::uint8_t jmpbuf[sizeof(png->jmpbuf)];
    std::memcpy(jmpbuf, png->jmpbuf, sizeof(jmpbuf));
    void* const errorFn = png->error_fn;
    void* const warningFn = png->warning_fn;
    void* const errorPtr = png->error_ptr;
    std::memset(png, 0, sizeof(PngStruct));
    png->error_fn = errorFn;
    png->warning_fn = warningFn;
    png->error_ptr = errorPtr;
    std::memcpy(png->jmpbuf, jmpbuf, sizeof(jmpbuf));
}

// Purpose: Frees a read struct and its info structures (PaintLib, after
// decoding or on an error).
void __cdecl png_destroy_read_struct(PngStruct** pngPtr, PngInfo** infoPtr, PngInfo** endInfoPtr) {
    PngStruct* const png = pngPtr != nullptr ? *pngPtr : nullptr;
    PngInfo* const info = infoPtr != nullptr ? *infoPtr : nullptr;
    PngInfo* const endInfo = endInfoPtr != nullptr ? *endInfoPtr : nullptr;
    calls::At<ReadDestroyFn>(kPngReadDestroy)(png, info, endInfo);
    const auto destroy = calls::At<DestroyStructFn>(kPngDestroyStruct);
    if (info != nullptr) {
        Free(png, info->text);
        destroy(info);
        *infoPtr = nullptr;
    }
    if (endInfo != nullptr) {
        Free(png, endInfo->text);
        destroy(endInfo);
        *endInfoPtr = nullptr;
    }
    if (png != nullptr) {
        destroy(png);
        *pngPtr = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// A png_struct and two infos with random contents, the pointers they free
// either null or blocks of a test area (texts with keys, pCAL parameters,
// 16-bit gamma tables of 1 << (8 - gamma_shift) rows, gamma_shift 0..8),
// an inflate stream without state (inflateEnd returns at once). free
// (0x621b7e) jumps to a test function that logs the offsets, so the frees
// are compared in order; also compared: the structs and the pointers.

namespace {

constexpr std::size_t kArea = 0x8000;

struct DestroyWorld {
    PngStruct png;
    PngInfo info[2];
    std::uint8_t area[kArea];  // blocks to free, texts, tables
    PngStruct* pngPtr;
    PngInfo* infoPtrs[2];
};

DestroyWorld* g_destroyWorld = nullptr;
std::vector<std::uint32_t>* g_freeLog = nullptr;

void __cdecl LogFree(void* block) {
    const auto* base = reinterpret_cast<const std::uint8_t*>(g_destroyWorld);
    g_freeLog->push_back(static_cast<std::uint32_t>(static_cast<const std::uint8_t*>(block) - base));
}

void* Block(test::Rng& rng, DestroyWorld& w, std::size_t& next) {
    if (rng.Chance(1, 4)) {
        return nullptr;
    }
    void* p = w.area + next;
    next = (next + 16) % 0x1000;
    return p;
}

void FillInfo(test::Rng& rng, DestroyWorld& w, PngInfo& info, std::size_t& next, std::size_t textAt) {
    info.num_text = static_cast<std::int32_t>(rng.Below(4));
    if (rng.Chance(2, 3)) {
        auto* texts = reinterpret_cast<PngText*>(w.area + textAt);
        for (std::int32_t i = 0; i < info.num_text; ++i) {
            texts[i].key = static_cast<char*>(Block(rng, w, next));
        }
        info.text = texts;
    } else {
        info.text = nullptr;
    }
    info.pcal_purpose = static_cast<char*>(Block(rng, w, next));
    info.pcal_units = static_cast<char*>(Block(rng, w, next));
    info.pcal_nparams = static_cast<std::uint8_t>(rng.Below(4));
    if (rng.Chance(2, 3)) {
        auto** params = reinterpret_cast<char**>(w.area + textAt + 0x100);
        for (std::int32_t i = 0; i < info.pcal_nparams; ++i) {
            params[i] = static_cast<char*>(Block(rng, w, next));
        }
        info.pcal_params = params;
    } else {
        info.pcal_params = nullptr;
    }
}

struct DestroyFunctions {
    InfoDestroyFn infoDestroy;
    ReadDestroyFn readDestroy;
    DestroyReadStructFn destroyReadStruct;
};

void CompareDestroy(test::Context& ctx, const DestroyFunctions& candidate, int cases) {
    const DestroyFunctions original{Original_png_info_destroy, Original_png_read_destroy,
                                    Original_png_destroy_read_struct};
    auto a = std::make_unique<DestroyWorld>();
    auto initial = std::make_unique<DestroyWorld>();
    auto afterOriginal = std::make_unique<DestroyWorld>();
    test::ScopedJump freeJump(calls::kFree, reinterpret_cast<const void*>(&LogFree));
    for (int i = 0; i < cases; ++i) {
        test::Rng& rng = ctx.rng();
        rng.Fill(a.get(), sizeof(DestroyWorld));
        const int op = static_cast<int>(rng.Below(3));
        std::size_t next = 0;
        PngStruct& png = a->png;
        png.zbuf = static_cast<std::uint8_t*>(Block(rng, *a, next));
        png.row_buf = static_cast<std::uint8_t*>(Block(rng, *a, next));
        png.prev_row = static_cast<std::uint8_t*>(Block(rng, *a, next));
        for (std::size_t off : {kPaletteLookup, kPaletteLookup + 4, kGammaTable, kGammaTable + 4, kGammaTable + 8,
                                kTimeBuffer, kSaveBuffer}) {
            Field(&png, off) = static_cast<std::uint8_t*>(Block(rng, *a, next));
        }
        png.palette = Block(rng, *a, next);
        png.trans = static_cast<std::uint8_t*>(Block(rng, *a, next));
        png.hist = static_cast<std::uint16_t*>(Block(rng, *a, next));
        png.flags = rng.Next32();
        const std::int32_t shift = static_cast<std::int32_t>(rng.Below(9));
        png.gamma_shift = shift;
        for (std::size_t k = 0; k < 3; ++k) {
            if (rng.Chance(1, 3)) {
                Field(&png, kGamma16Tables + 4 * k) = nullptr;
                continue;
            }
            auto** table = reinterpret_cast<std::uint8_t**>(a->area + 0x2000 + 0x400 * k);
            for (std::int32_t r = 0; r < (1 << (8 - shift)); ++r) {
                table[r] = static_cast<std::uint8_t*>(Block(rng, *a, next));
            }
            Field(&png, kGamma16Tables + 4 * k) = reinterpret_cast<std::uint8_t*>(table);
        }
        png.zstream.state = nullptr;
        FillInfo(rng, *a, a->info[0], next, 0x1000);
        FillInfo(rng, *a, a->info[1], next, 0x1400);
        a->pngPtr = &a->png;
        a->infoPtrs[0] = rng.Chance(1, 4) ? nullptr : &a->info[0];
        a->infoPtrs[1] = rng.Chance(1, 2) ? nullptr : &a->info[1];
        const bool nullArgs = rng.Chance(1, 10);

        *initial = *a;
        std::vector<std::uint32_t> logs[2];
        for (int run = 0; run < 2; ++run) {
            if (run == 1) {
                *afterOriginal = *a;
                *a = *initial;
            }
            const DestroyFunctions& f = run == 0 ? original : candidate;
            g_destroyWorld = a.get();
            g_freeLog = &logs[run];
            switch (op) {
                case 0: f.infoDestroy(&a->png, &a->info[0]); break;
                case 1: f.readDestroy(&a->png, a->infoPtrs[0], a->infoPtrs[1]); break;
                default:
                    f.destroyReadStruct(&a->pngPtr, nullArgs ? nullptr : &a->infoPtrs[0],
                                        nullArgs ? nullptr : &a->infoPtrs[1]);
                    break;
            }
        }
        bool same = S3RE_EXPECT(logs[0] == logs[1]);
        same &= S3RE_EXPECT_MEM_EQ(afterOriginal.get(), a.get(), sizeof(DestroyWorld));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
            break;
        }
    }
    g_destroyWorld = nullptr;
    g_freeLog = nullptr;
}

// Wrong: png_read_destroy keeps the warning callback but not the error pointer.
void __cdecl WrongReadDestroy(PngStruct* png, PngInfo* info, PngInfo* endInfo) {
    png_read_destroy(png, info, endInfo);
    png->error_ptr = nullptr;
}

}  // namespace

S3RE_TEST(PngReadDestroy_MatchOriginal) {
    CompareDestroy(ctx, {&png_info_destroy, &png_read_destroy, &png_destroy_read_struct}, 3000);
}

S3RE_TEST(PngReadDestroy_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDestroy(probe, {&png_info_destroy, &WrongReadDestroy, &png_destroy_read_struct}, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::thirdparty
