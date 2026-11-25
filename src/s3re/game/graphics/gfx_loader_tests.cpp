// Tests of gfx_loader.cpp (CGraphicsData's constructor, destructors,
// FindDirectoryContainingFile, DetachFromMedium and Open).
//
// Constructor / destructors: two objects over the same random bytes, the
//   original on one and ours on the other; compared: the object (except
//   the name's allocator byte, a known difference) and each mapping object
//   (they are separate allocations); the destructors with views mapped or
//   not, under scripted imports (UnmapViewOfFile, CloseHandle), logs
//   compared.
// FindDirectoryContainingFile: scripted imports (GetCurrentDirectoryA,
//   SetCurrentDirectoryA, FindFirstFileA, FindClose, GetLastError: random
//   failures, so the swallowed CHResult, the Win32 errors and
//   CFileNotFound all happen) on real folder vectors of 0..6 names
//   (an empty one grows in the index check); compared: the result's text,
//   the vector, the logs and the exception.
// DetachFromMedium: random views inside a private buffer, file sizes, a
//   scripted drive type and memory walk; logs and the DebugLog calls
//   compared.
// Open: the real GFX folder, both pixel formats (RGB565 and RGB555 sets),
//   every group set 0..4 with g_config's group-61 byte on and off, detach
//   modes 0, 2 and 3, and an unsupported format (asserts after mapping):
//   the original on one constructed object, ours on another; compared: the
//   pixel format, the name, the file sizes, each table as an offset from
//   its view, the mapped bytes' first dwords, the language count, the
//   shading masks and the shadow span state, the DebugLog calls and the
//   exception. Then both are destroyed by the scalar deleting destructor.

#include "game/debug_log.h"
#include "game/framework/framework_layout.h"
#include "game/globals.h"
#include "game/graphics/gfx_layout.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "test/difftest.h"
#include "test/fake_win32.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace s3re::game {

using msvc6::String;

void* __fastcall CGraphicsData_Construct(void* self, void* edx);
void* __fastcall CGraphicsData_DeletingDestroy(void* self, void* edx, std::uint32_t flags);
void __fastcall CGraphicsData_Destroy(void* self, void* edx);
String* __fastcall CGraphicsData_FindDirectoryContainingFile(void* self, void* edx, String* result, void* folders,
                                                            const String* file);
std::uint32_t __fastcall CGraphicsData_DetachFromMedium(void* self, void* edx, std::uint32_t otherwise);
void __fastcall CGraphicsData_Open(void* self, void* edx, const void* format, const String* basePath,
                                   std::uint32_t detachMode, std::uint32_t groupSet);

using GfxConstructFn = decltype(&CGraphicsData_Construct);
using GfxDeletingFn = decltype(&CGraphicsData_DeletingDestroy);
using GfxDestroyFn = decltype(&CGraphicsData_Destroy);
using GfxFindDirectoryFn = decltype(&CGraphicsData_FindDirectoryContainingFile);
using GfxDetachFn = decltype(&CGraphicsData_DetachFromMedium);
using GfxOpenFn = decltype(&CGraphicsData_Open);

extern GfxConstructFn Original_CGraphicsData_Construct;
extern GfxDeletingFn Original_CGraphicsData_DeletingDestroy;
extern GfxDestroyFn Original_CGraphicsData_Destroy;
extern GfxFindDirectoryFn Original_CGraphicsData_FindDirectoryContainingFile;
extern GfxDetachFn Original_CGraphicsData_DetachFromMedium;
extern GfxOpenFn Original_CGraphicsData_Open;

namespace {

using framework::CFileMapping;
using gfx::CGraphicsData;
using test::CaughtException;
using test::FakeWin32;
using test::FakeWin32Config;

struct Functions {
    GfxConstructFn construct;
    GfxDeletingFn deleting;
    GfxDestroyFn destroy;
    GfxFindDirectoryFn find;
    GfxDetachFn detach;
    GfxOpenFn open;
};

Functions Originals() {
    return {Original_CGraphicsData_Construct,
            Original_CGraphicsData_DeletingDestroy,
            Original_CGraphicsData_Destroy,
            Original_CGraphicsData_FindDirectoryContainingFile,
            Original_CGraphicsData_DetachFromMedium,
            Original_CGraphicsData_Open};
}

Functions Ours() {
    return {&CGraphicsData_Construct,      &CGraphicsData_DeletingDestroy,
            &CGraphicsData_Destroy,        &CGraphicsData_FindDirectoryContainingFile,
            &CGraphicsData_DetachFromMedium, &CGraphicsData_Open};
}

constexpr std::uintptr_t kStringVectorConstruct = 0x005fda00;
constexpr std::uintptr_t kStringVectorInsertAt = 0x004148a0;
constexpr std::uintptr_t kStringVectorDestroy = 0x00409d50;

struct StringVector {
    std::uint32_t vtable;
    std::uint32_t unk04;
    String* data;
    std::uint32_t capacity;
    std::int32_t filled;
    std::uint32_t length;
    std::uint32_t grow;
};

// --------------------------------------------------------------------------
// Guarded calls (no C++ objects in the __try frames)
// --------------------------------------------------------------------------

enum class Op { Find, Detach, Open };

struct Call {
    Op op;
    void* self;
    String* result;
    void* folders;
    const String* file;
    std::uint32_t a;
    const void* format;
    const String* basePath;
    std::uint32_t b;
    std::uint32_t c;
};

std::uint32_t Dispatch(const Functions* f, const Call* call) {
    switch (call->op) {
    case Op::Find:
        return reinterpret_cast<std::uint32_t>(f->find(call->self, nullptr, call->result, call->folders, call->file));
    case Op::Detach: return f->detach(call->self, nullptr, call->a);
    case Op::Open: f->open(call->self, nullptr, call->format, call->basePath, call->b, call->c); return 0;
    }
    return 0;
}

struct Outcome {
    std::uint32_t eax = 0;
    CaughtException caught;
};

Outcome Run(const Functions* f, const Call* call) {
    Outcome out;
    __try {
        out.eax = Dispatch(f, call);
    } __except (test::CatchGameException(GetExceptionInformation(), &out.caught)) {
    }
    return out;
}

std::string Text(const String& s) {
    return s.ptr != nullptr ? std::string(s.ptr, s.len) : std::string();
}

// --------------------------------------------------------------------------
// Constructor and destructors
// --------------------------------------------------------------------------

constexpr std::size_t kNameAllocator = offsetof(CGraphicsData, name);

bool SameMappings(test::Context& ctx, const CGraphicsData* a, const CGraphicsData* b) {
    bool same = true;
    for (std::size_t g = 0; g < gfx::kGroups && same; ++g) {
        same &= S3RE_EXPECT_EQ(a->mappings[g] != 0, b->mappings[g] != 0);
        if (a->mappings[g] == 0 || b->mappings[g] == 0) {
            continue;
        }
        CFileMapping ma = *reinterpret_cast<const CFileMapping*>(a->mappings[g]);
        CFileMapping mb = *reinterpret_cast<const CFileMapping*>(b->mappings[g]);
        same &= S3RE_EXPECT_EQ(ma.views.data != 0, mb.views.data != 0);
        ma.views.data = mb.views.data = 0;
        same &= S3RE_EXPECT_MEM_EQ(&ma, &mb, sizeof(ma));
    }
    return same;
}

bool SameObject(test::Context& ctx, const CGraphicsData* a, const CGraphicsData* b) {
    CGraphicsData ca;
    CGraphicsData cb;
    std::memcpy(&ca, a, sizeof(ca));
    std::memcpy(&cb, b, sizeof(cb));
    reinterpret_cast<std::uint8_t*>(&ca)[kNameAllocator] = 0;  // known difference
    reinterpret_cast<std::uint8_t*>(&cb)[kNameAllocator] = 0;
    for (std::size_t g = 0; g < gfx::kGroups; ++g) {
        ca.mappings[g] = ca.mappings[g] != 0;
        cb.mappings[g] = cb.mappings[g] != 0;
    }
    return S3RE_EXPECT_MEM_EQ(&ca, &cb, sizeof(ca));
}

bool CompareLifetime(test::Context& ctx, const Functions& ours) {
    test::Rng& rng = ctx.rng();
    auto* a = static_cast<CGraphicsData*>(msvc6::GameOperatorNew(sizeof(CGraphicsData)));
    auto* b = static_cast<CGraphicsData*>(msvc6::GameOperatorNew(sizeof(CGraphicsData)));
    rng.Fill(a, sizeof(*a));
    std::memcpy(b, a, sizeof(*a));
    Original_CGraphicsData_Construct(a, nullptr);
    ours.construct(b, nullptr);
    bool same = SameObject(ctx, a, b);
    same &= SameMappings(ctx, a, b);
    // Views (fake addresses, which the scripted UnmapViewOfFile accepts) and
    // a name with a buffer or not.
    for (std::size_t g = 0; g < gfx::kGroups; ++g) {
        const bool mapped = g == 0 ? rng.Chance(2, 3) : rng.Chance(1, 2);
        a->views[g] = b->views[g] = mapped ? (rng.Next32() | 1) : 0;
    }
    if (rng.Chance(1, 2)) {
        const char* const text = rng.Chance(1, 2) ? "C:\\Games\\S3\\Gfx\\" : "";
        msvc6::String_AssignText(&a->name, nullptr, text);
        msvc6::String_AssignText(&b->name, nullptr, text);
    }
    const std::uint64_t seed = rng.Next64();
    const std::uint32_t imports = test::kFakeUnmapView | test::kFakeCloseHandle;
    FakeWin32 fa(seed, imports);
    FakeWin32 fb(seed, imports);
    const bool deleting = rng.Chance(1, 2);
    fa.Install();
    if (deleting) {
        Original_CGraphicsData_DeletingDestroy(a, nullptr, 0);
    } else {
        Original_CGraphicsData_Destroy(a, nullptr);
    }
    fa.Uninstall();
    fb.Install();
    if (deleting) {
        same &= S3RE_EXPECT(ours.deleting(b, nullptr, 0) == b);
    } else {
        ours.destroy(b, nullptr);
    }
    fb.Uninstall();
    same &= S3RE_EXPECT(fa.log_ == fb.log_);
    same &= SameObject(ctx, a, b);
    msvc6::GameOperatorDelete(a);
    msvc6::GameOperatorDelete(b);
    return same;
}

// --------------------------------------------------------------------------
// FindDirectoryContainingFile
// --------------------------------------------------------------------------

void BuildFolders(StringVector* folders, test::Rng& rng, int count) {
    reinterpret_cast<void*(__fastcall*)(void*, void*, std::uint32_t, std::uint32_t)>(kStringVectorConstruct)(
        folders, nullptr, 10, 5);
    static const char* const kFolders[] = {".", "Gfx", ".\\Gfx", "", "..", "D:\\S3\\Gfx", "C:\\x"};
    for (int i = 0; i < count; ++i) {
        String folder{};
        msvc6::String_AssignText(&folder, nullptr, kFolders[rng.Below(7)]);
        reinterpret_cast<std::uint32_t(__fastcall*)(void*, void*, std::uint32_t, const String*)>(
            kStringVectorInsertAt)(folders, nullptr, folders->length, &folder);
        msvc6::String_Destroy(&folder, nullptr);
    }
}

void DestroyFolders(StringVector* folders) {
    reinterpret_cast<void(__fastcall*)(void*, void*)>(kStringVectorDestroy)(folders, nullptr);
}

bool CompareFind(test::Context& ctx, const Functions& ours) {
    test::Rng& rng = ctx.rng();
    const int count = static_cast<int>(rng.Below(7));
    const std::uint64_t folderSeed = rng.Next64();
    test::Rng ra(folderSeed);
    test::Rng rb(folderSeed);
    StringVector fa{};
    StringVector fb{};
    BuildFolders(&fa, ra, count);
    BuildFolders(&fb, rb, count);
    String file{};
    msvc6::String_AssignText(&file, nullptr, rng.Chance(1, 8) ? "" : "siedler3_00.f8007e01f.dat");
    String resultA{};
    String resultB{};
    FakeWin32Config config;
    config.failChance = rng.Below(4);
    config.currentDirectory = rng.Chance(1, 6) ? "" : "C:\\Games\\S3\\Gfx";
    const std::uint64_t seed = rng.Next64();
    // GetLastError stays real: the CRT's exception dispatch of the
    // original's catch block calls it too (_getptd), so a scripted one would
    // log calls ours (SEH) doesn't make. The CHResult codes are left out.
    const std::uint32_t imports = test::kFakeGetCurrentDirectory | test::kFakeSetCurrentDirectory |
                                  test::kFakeFindFirstFile | test::kFakeFindClose;
    FakeWin32 wa(seed, imports, config);
    FakeWin32 wb(seed, imports, config);
    const Functions originals = Originals();
    Call ca{Op::Find, nullptr, &resultA, &fa, &file, 0, nullptr, nullptr, 0, 0};
    Call cb{Op::Find, nullptr, &resultB, &fb, &file, 0, nullptr, nullptr, 0, 0};
    wa.Install();
    const Outcome oa = Run(&originals, &ca);
    wa.Uninstall();
    wb.Install();
    const Outcome ob = Run(&ours, &cb);
    wb.Uninstall();
    CaughtException caughtA = oa.caught;
    CaughtException caughtB = ob.caught;
    caughtA.code = caughtB.code = 0;
    bool same = S3RE_EXPECT(caughtA == caughtB);
    same &= S3RE_EXPECT(wa.log_ == wb.log_);
    if (!oa.caught.threw) {
        same &= S3RE_EXPECT_EQ(oa.eax == reinterpret_cast<std::uint32_t>(&resultA),
                               ob.eax == reinterpret_cast<std::uint32_t>(&resultB));
        same &= S3RE_EXPECT(Text(resultA) == Text(resultB));
        same &= S3RE_EXPECT_EQ(resultA.res, resultB.res);
        same &= S3RE_EXPECT_EQ(resultA.ptr != nullptr, resultB.ptr != nullptr);
        msvc6::String_Destroy(&resultA, nullptr);
        msvc6::String_Destroy(&resultB, nullptr);
    }
    same &= S3RE_EXPECT_EQ(fa.length, fb.length);
    same &= S3RE_EXPECT_EQ(fa.capacity, fb.capacity);
    same &= S3RE_EXPECT_EQ(fa.filled, fb.filled);
    for (std::uint32_t i = 0; i < fa.length && i < fb.length && same; ++i) {
        same &= S3RE_EXPECT(Text(fa.data[i]) == Text(fb.data[i]));
    }
    if (!same) {
        ctx.Fail(__FILE__, __LINE__, "  folders %d, fail chance %u", count, config.failChance);
        for (std::size_t i = 0; i < wa.log_.size() || i < wb.log_.size(); ++i) {
            ctx.Fail(__FILE__, __LINE__, "  log[%u]: %#x %#x", static_cast<unsigned>(i),
                     i < wa.log_.size() ? wa.log_[i] : 0xdead, i < wb.log_.size() ? wb.log_[i] : 0xdead);
        }
    }
    DestroyFolders(&fa);
    DestroyFolders(&fb);
    msvc6::String_Destroy(&file, nullptr);
    return same;
}

// --------------------------------------------------------------------------
// DetachFromMedium
// --------------------------------------------------------------------------


bool CompareDetach(test::Context& ctx, const Functions& ours) {
    test::Rng& rng = ctx.rng();
    static void* const area = VirtualAlloc(nullptr, 0x40000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    const std::uint32_t low = reinterpret_cast<std::uint32_t>(area);
    static CGraphicsData a;
    static CGraphicsData b;
    rng.Fill(&a, sizeof(a));
    for (std::size_t g = 0; g < gfx::kGroups; ++g) {
        a.views[g] = rng.Chance(1, 3) ? low + rng.Below(0x20000) : 0;
        a.fileSizes[g] = rng.Below(0x18000);
    }
    std::memcpy(&b, &a, sizeof(a));
    // Each its own name buffer.
    const char* const folder = rng.Chance(1, 2) ? "D:\\Gfx\\" : "C:\\S3\\Gfx\\";
    msvc6::detail::Clear(&a.name);
    msvc6::detail::Clear(&b.name);
    msvc6::String_AssignText(&a.name, nullptr, folder);
    msvc6::String_AssignText(&b.name, nullptr, folder);
    FakeWin32Config config;
    config.regionLow = low;
    config.regionHigh = low + 0x40000;
    config.failChance = 0;
    static const std::uint32_t kDriveTypes[] = {2, 3, 4, 5, 0};
    config.driveType = kDriveTypes[rng.Below(5)];
    const std::uint32_t otherwise = rng.Next32();
    const std::uint64_t seed = rng.Next64();
    const std::uint32_t imports = test::kFakeGetDriveType | test::kFakeGetSystemInfo | test::kFakeVirtualQuery |
                                  test::kFakeVirtualProtect | test::kFakeGetLastError;
    FakeWin32 wa(seed, imports, config);
    FakeWin32 wb(seed, imports, config);
    const Functions originals = Originals();
    const std::uint32_t savedPageSize = g_detachPageSize.get();
    Call ca{Op::Detach, &a, nullptr, nullptr, nullptr, otherwise, nullptr, nullptr, 0, 0};
    Call cb = ca;
    cb.self = &b;
    std::vector<DebugLogCall> logA;
    std::vector<DebugLogCall> logB;
    g_detachPageSize.get() = 0;
    wa.Install();
    SetDebugLogCapture(&logA);
    const Outcome oa = Run(&originals, &ca);
    SetDebugLogCapture(nullptr);
    wa.Uninstall();
    g_detachPageSize.get() = 0;
    wb.Install();
    SetDebugLogCapture(&logB);
    const Outcome ob = Run(&ours, &cb);
    SetDebugLogCapture(nullptr);
    wb.Uninstall();
    g_detachPageSize.get() = savedPageSize;
    bool same = S3RE_EXPECT(oa.caught == ob.caught);
    // EAX: the check's result or the end of the views array (per object).
    const std::uint32_t end = reinterpret_cast<std::uint32_t>(&a.views[gfx::kGroups]);
    same &= S3RE_EXPECT_EQ(oa.eax == end ? 1u : oa.eax,
                           ob.eax == reinterpret_cast<std::uint32_t>(&b.views[gfx::kGroups]) ? 1u : ob.eax);
    same &= S3RE_EXPECT(wa.log_ == wb.log_);
    same &= S3RE_EXPECT(logA == logB);
    msvc6::String_Destroy(&a.name, nullptr);
    msvc6::String_Destroy(&b.name, nullptr);
    return same;
}

// --------------------------------------------------------------------------
// Open on the real files
// --------------------------------------------------------------------------

struct OpenState {
    Outcome outcome;
    std::vector<DebugLogCall> logs;
    ShadeMasks masks{};
    ShadowSpanState shadow{};
};

// The DDPIXELFORMAT of a 16-bit RGB surface.
struct PixelFormat16 {
    std::uint32_t size = 0x20;
    std::uint32_t flags = 0x40;  // DDPF_RGB
    std::uint32_t fourCC = 0;
    std::uint32_t bits = 16;
    std::uint32_t red;
    std::uint32_t green;
    std::uint32_t blue;
    std::uint32_t alpha = 0;
};

OpenState RunOpen(const Functions& f, CGraphicsData* gfx, const PixelFormat16* format, const String* basePath,
                  std::uint32_t detachMode, std::uint32_t groupSet) {
    OpenState state;
    std::memset(&g_shadeMasks.get(), 0, sizeof(ShadeMasks));
    std::memset(&g_shadowSpan.get(), 0, sizeof(ShadowSpanState));
    Call call{Op::Open, gfx, nullptr, nullptr, nullptr, 0, format, basePath, detachMode, groupSet};
    SetDebugLogCapture(&state.logs);
    state.outcome = Run(&f, &call);
    SetDebugLogCapture(nullptr);
    state.masks = g_shadeMasks.get();
    state.shadow = g_shadowSpan.get();
    return state;
}

bool SameOpen(test::Context& ctx, const CGraphicsData* a, const CGraphicsData* b, const OpenState& sa,
              const OpenState& sb) {
    bool same = S3RE_EXPECT(sa.outcome.caught == sb.outcome.caught);
    same &= S3RE_EXPECT(sa.logs == sb.logs);
    same &= S3RE_EXPECT_MEM_EQ(&sa.masks, &sb.masks, sizeof(sa.masks));
    same &= S3RE_EXPECT_MEM_EQ(&sa.shadow, &sb.shadow, sizeof(sa.shadow));
    same &= S3RE_EXPECT_MEM_EQ(a->pixelFormat, b->pixelFormat, sizeof(a->pixelFormat));
    same &= S3RE_EXPECT(Text(a->name) == Text(b->name));
    same &= S3RE_EXPECT_EQ(a->languageCount, b->languageCount);
    same &= S3RE_EXPECT_EQ(a->language, b->language);
    for (std::size_t g = 0; g < gfx::kGroups && same; ++g) {
        const std::uint32_t va = a->views[g];
        const std::uint32_t vb = b->views[g];
        same &= S3RE_EXPECT_EQ(va != 0, vb != 0);
        same &= S3RE_EXPECT_EQ(a->fileSizes[g], b->fileSizes[g]);
        if (va == 0 || vb == 0) {
            continue;
        }
        same &= S3RE_EXPECT_EQ(*reinterpret_cast<const std::uint32_t*>(va),
                               *reinterpret_cast<const std::uint32_t*>(vb));
        // Every table as an offset from its view.
        const std::uint32_t tablesA[] = {
            reinterpret_cast<std::uint32_t>(a->textTables[g]), reinterpret_cast<std::uint32_t>(a->animationTables[g]),
            reinterpret_cast<std::uint32_t>(a->guiImageTables[g]),
            reinterpret_cast<std::uint32_t>(a->landscapeTextureTables[g]),
            reinterpret_cast<std::uint32_t>(a->colorIndexedTables[g]), reinterpret_cast<std::uint32_t>(a->shadowTables[g]),
            reinterpret_cast<std::uint32_t>(a->paletteTables[g]), reinterpret_cast<std::uint32_t>(a->sequenceTables[g])};
        const std::uint32_t tablesB[] = {
            reinterpret_cast<std::uint32_t>(b->textTables[g]), reinterpret_cast<std::uint32_t>(b->animationTables[g]),
            reinterpret_cast<std::uint32_t>(b->guiImageTables[g]),
            reinterpret_cast<std::uint32_t>(b->landscapeTextureTables[g]),
            reinterpret_cast<std::uint32_t>(b->colorIndexedTables[g]), reinterpret_cast<std::uint32_t>(b->shadowTables[g]),
            reinterpret_cast<std::uint32_t>(b->paletteTables[g]), reinterpret_cast<std::uint32_t>(b->sequenceTables[g])};
        for (int t = 0; t < 8; ++t) {
            same &= S3RE_EXPECT_EQ(tablesA[t] - va, tablesB[t] - vb);
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  group %u", static_cast<unsigned>(g));
        }
    }
    return same;
}

// Returns the number of groups the original mapped (0: nothing found).
int CompareOpenOnce(test::Context& ctx, const Functions& ours, const PixelFormat16& format, std::uint32_t detachMode,
                    std::uint32_t groupSet, std::uint8_t group61) {
    auto* a = static_cast<CGraphicsData*>(msvc6::GameOperatorNew(sizeof(CGraphicsData)));
    auto* b = static_cast<CGraphicsData*>(msvc6::GameOperatorNew(sizeof(CGraphicsData)));
    ctx.rng().Fill(a, sizeof(*a));
    std::memcpy(b, a, sizeof(*a));
    Original_CGraphicsData_Construct(a, nullptr);
    Original_CGraphicsData_Construct(b, nullptr);
    String basePath{};
    const std::uint8_t savedFlag = g_config->gfx_group61;
    g_config->gfx_group61 = group61;
    const OpenState sa = RunOpen(Originals(), a, &format, &basePath, detachMode, groupSet);
    const OpenState sb = RunOpen(ours, b, &format, &basePath, detachMode, groupSet);
    g_config->gfx_group61 = savedFlag;
    if (!SameOpen(ctx, a, b, sa, sb)) {
        ctx.Fail(__FILE__, __LINE__, "  format %#x, detach %u, group set %u, group 61 %u", format.red, detachMode,
                 groupSet, group61);
    }
    int mapped = 0;
    for (std::size_t g = 0; g < gfx::kGroups; ++g) {
        mapped += a->views[g] != 0 ? 1 : 0;
    }
    Original_CGraphicsData_DeletingDestroy(a, nullptr, 1);
    Original_CGraphicsData_DeletingDestroy(b, nullptr, 1);
    return mapped;
}

void CompareOpenReal(test::Context& ctx, const Functions& ours) {
    const PixelFormat16 formats[] = {{0x20, 0x40, 0, 16, 0xf800, 0x7e0, 0x1f, 0},
                                     {0x20, 0x40, 0, 16, 0x7c00, 0x3e0, 0x1f, 0},
                                     {0x20, 0x40, 0, 16, 0xff0000, 0xff00, 0xff, 0}};  // no such files
    for (const PixelFormat16& format : formats) {
        for (std::uint32_t detachMode : {0u, 2u, 3u}) {
            for (std::uint32_t groupSet = 0; groupSet <= 4; ++groupSet) {
                for (std::uint8_t group61 = 0; group61 <= 1; ++group61) {
                    const int mapped = CompareOpenOnce(ctx, ours, format, detachMode, groupSet, group61);
                    if (format.red != 0xff0000) {
                        S3RE_EXPECT(mapped > 30);
                    }
                }
            }
        }
    }
}

}  // namespace

S3RE_TEST(GfxLoader_OpenMatchesOriginal) {
    CompareOpenReal(ctx, Ours());
}

namespace {

void CompareScripted(test::Context& ctx, const Functions& ours, int cases) {
    for (int i = 0; i < cases; ++i) {
        bool same;
        switch (i % 3) {
        case 0: same = CompareLifetime(ctx, ours); break;
        case 1: same = CompareFind(ctx, ours); break;
        default: same = CompareDetach(ctx, ours); break;
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            break;
        }
    }
}

// Wrong: the folder without the trailing backslash.
String* __fastcall WrongFind(void* self, void* edx, String* result, void* folders, const String* file) {
    CGraphicsData_FindDirectoryContainingFile(self, edx, result, folders, file);
    if (result->len > 0) {
        msvc6::String_Erase(result, nullptr, result->len - 1, 1);
    }
    return result;
}

// Wrong: detaches only on removable drives, not on CDs.
std::uint32_t __fastcall WrongDetach(void* self, void* edx, std::uint32_t otherwise) {
    const CGraphicsData* const gfx = static_cast<const CGraphicsData*>(self);
    const char* const text = gfx->name.ptr != nullptr ? gfx->name.ptr : "";
    char root[4] = {text[0], text[0] != 0 ? text[1] : static_cast<char>(0), 0, 0};
    root[2] = root[1] != 0 ? text[2] : static_cast<char>(0);
    if (g_importGetDriveTypeA.get()(root) == DRIVE_CDROM) {
        return 0;
    }
    return CGraphicsData_DetachFromMedium(self, edx, otherwise);
}

// Wrong: the language count from the last text table, not the first.
void __fastcall WrongOpen(void* self, void* edx, const void* format, const String* basePath, std::uint32_t detachMode,
                          std::uint32_t groupSet) {
    CGraphicsData_Open(self, edx, format, basePath, detachMode, groupSet);
    CGraphicsData* const gfx = static_cast<CGraphicsData*>(self);
    for (std::size_t g = 0; g < gfx::kGroups; ++g) {
        if (gfx->views[g] != 0 && gfx->textTables[g]->languages != 0) {
            gfx->languageCount = gfx->textTables[g]->languages + 1;
        }
    }
}

}  // namespace

S3RE_TEST(GfxLoader_MatchOriginal) {
    CompareScripted(ctx, Ours(), 1500);
}

S3RE_TEST(GfxLoader_ComparisonDetectsWrongImplementation) {
    Functions wrong = Ours();
    wrong.find = &WrongFind;
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareScripted(probe, wrong, 300);
    S3RE_EXPECT(probe.failures() > 0);
    wrong = Ours();
    wrong.detach = &WrongDetach;
    test::Context probe2(ctx.name(), 1, /*quiet=*/true);
    CompareScripted(probe2, wrong, 600);
    S3RE_EXPECT(probe2.failures() > 0);
    wrong = Ours();
    wrong.open = &WrongOpen;
    test::Context probe3(ctx.name(), 1, /*quiet=*/true);
    CompareOpenReal(probe3, wrong);
    S3RE_EXPECT(probe3.failures() > 0);
}

}  // namespace s3re::game

namespace s3re::game {

// For the emulator's self-test (tools/emutest/specs/self_test.py): the
// constructor with a wrong name length (+0x2c, near the ignored
// allocator byte +0x24).
void* __fastcall WrongGfxConstruct(void* self, void* edx) {
    CGraphicsData_Construct(self, edx);
    static_cast<gfx::CGraphicsData*>(self)->name.len = 1;
    return self;
}

// The search without the trailing backslash (self-test of the result's
// ignored allocator byte).
msvc6::String* __fastcall WrongFindDirectory(void* self, void* edx, msvc6::String* result, void* folders,
                                             const msvc6::String* file) {
    CGraphicsData_FindDirectoryContainingFile(self, edx, result, folders, file);
    if (result->len > 0) {
        msvc6::String_Erase(result, nullptr, result->len - 1, 1);
    }
    return result;
}

}  // namespace s3re::game
