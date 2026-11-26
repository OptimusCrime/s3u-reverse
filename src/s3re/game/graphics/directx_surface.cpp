// DirectDraw error handling and DirectX::CSurface::DDBlt (DirectX\*.cpp;
// RTTI and string names).
//
// DirectX::ThrowIfDDFailed (0x00405d10): (hr, where) stdcall, ret 8. For a
//   failed HRESULT (hr < 0) it builds a DirectX::CDirectDrawException (0x20
//   bytes from the game's operator new; constructor 0x5be180 with (hr,
//   where, 1)) and throws it with _CxxThrowException (ThrowInfo 0x66a698).
//   If operator new returns null the null pointer is thrown. Otherwise EAX
//   = 0. `where` is the name of the failing call (e.g. "CSurface::DDBlt").
// DirectX::CSurface::CheckResult (0x005bf2e0): (hr, where) thiscall, ret 8.
//   DDERR_SURFACELOST (0x887601c2): calls virtual slot 0x0c of the object
//   at this+0xc (no arguments; probably the DirectDraw owner restoring its
//   surfaces); otherwise ThrowIfDDFailed(hr, where). EAX = hr.
// DirectX::CSurface::DDBlt (0x005bf0c0): (dstRect, src CSurface* or null,
//   srcRect, flags, fx) thiscall, ret 0x14. Calls IDirectDrawSurface::Blt
//   (COM slot 0x14 of the interface at this+8) with src's interface (+8),
//   flags | DDBLT_WAIT | 0x200, until it returns DD_OK: on
//   DDERR_WASSTILLDRAWING (0x8876021c) after Sleep(0), on any other result
//   after CheckResult (which throws for failures and restores lost
//   surfaces). EAX = 0.
//
// DirectX::CSurface::DDBltFast (0x005bf130): (x, y, src CSurface*, srcRect,
//   flags) thiscall, ret 0x14: the same loop around IDirectDrawSurface::
//   BltFast (COM slot 0x1c) with src's interface (src is not checked for
//   null) and flags | DDBLTFAST_WAIT (0x10); errors named
//   "CSurface::DDBltFast". EAX = 0.
// DirectX::ThrowIfDPFailed (0x005bb890): (hr, where) stdcall, ret 8: like
//   ThrowIfDDFailed for DirectPlay: any hr other than 0 throws a
//   DirectX::CDirectPlayException (constructor 0x5bfdb0, ThrowInfo
//   0x681e60). EAX = 0.
//
// CSurface fields used: +0x08 the IDirectDrawSurface, +0x0c the owner
// (framework::CSimpleSurface in game/framework/framework_layout.h).
//
// The original ThrowIfDDFailed has an exception frame that frees the block
// if the constructor throws; ours has none (such a throw would mean the
// error report itself failed, and the block leaks).

#include "game/framework/framework_layout.h"
#include "game/globals.h"
#include "game/raw.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <windows.h>
#include <ddraw.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::int32_t kSurfaceLost = static_cast<std::int32_t>(0x887601c2);
constexpr std::int32_t kWasStillDrawing = static_cast<std::int32_t>(0x8876021c);
constexpr std::uint32_t kDDBltWaitAsync = 0x01000200;  // raw-ok: DDBLT flag bits, not an offset
constexpr std::uintptr_t kDirectDrawExceptionCtor = 0x005be180;
constexpr std::uintptr_t kCxxThrowException = 0x0061f027;
constexpr std::uint32_t kDDBltFastWait = 0x10;
// IDirectDrawSurface methods (byte offsets into the COM vtable).
constexpr std::uint32_t kSurfaceBlt = 0x14;      // slot 5
constexpr std::uint32_t kSurfaceBltFast = 0x1c;  // slot 7
// CDirectDraw slot 3 (byte offset 0xc): restores the lost surfaces.
constexpr std::uint32_t kOwnerRestoreSurfaces = 0x0c;
// The game's DirectDraw/DirectPlay exception objects are 0x20 bytes.
constexpr std::uint32_t kGameExceptionBytes = 0x20;
constexpr std::uintptr_t kDirectPlayExceptionCtor = 0x005bfdb0;

using framework::CLandscapeSurface;
using framework::CSimpleSurface;

CSimpleSurface* AsSurface(void* object) { return static_cast<CSimpleSurface*>(object); }
CLandscapeSurface* AsLandscape(void* object) { return static_cast<CLandscapeSurface*>(object); }

}  // namespace

using ThrowIfFailedFn = std::uint32_t(__stdcall*)(std::int32_t hr, const char* where);
using CheckResultFn = std::int32_t(__fastcall*)(void* self, void* edx, std::int32_t hr, const char* where);
using DDBltFn = std::uint32_t(__fastcall*)(void* self, void* edx, const RECT* dst, void* src, const RECT* srcRect,
                                           std::uint32_t flags, void* fx);

using DDBltFastFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t x, std::uint32_t y, void* src,
                                               const RECT* srcRect, std::uint32_t flags);

std::uint32_t __stdcall DirectX_ThrowIfDDFailed(std::int32_t hr, const char* where);
std::uint32_t __stdcall DirectX_ThrowIfDPFailed(std::int32_t hr, const char* where);
std::uint32_t __fastcall DirectX_CSurface_DDBltFast(void* self, void* edx, std::uint32_t x, std::uint32_t y, void* src,
                                                    const RECT* srcRect, std::uint32_t flags);
std::int32_t __fastcall DirectX_CSurface_CheckResult(void* self, void* edx, std::int32_t hr, const char* where);
std::uint32_t __fastcall DirectX_CSurface_DDBlt(void* self, void* edx, const RECT* dst, void* src,
                                                const RECT* srcRect, std::uint32_t flags, void* fx);

namespace {

// new + constructor(hr, where, 1) + _CxxThrowException, as both throw
// helpers do.
void ThrowGameException(std::uintptr_t ctor, std::uintptr_t throwInfo, std::int32_t hr, const char* where) {
    void* exception = msvc6::GameOperatorNew(kGameExceptionBytes);
    if (exception != nullptr) {
        using CtorFn = void*(__fastcall*)(void* self, void* edx, std::int32_t hr, const char* where, std::uint32_t c);
        exception = reinterpret_cast<CtorFn>(ctor)(exception, nullptr, hr, where, 1);
    }
    using ThrowFn = void(__stdcall*)(void* object, const void* info);
    reinterpret_cast<ThrowFn>(kCxxThrowException)(&exception, reinterpret_cast<const void*>(throwInfo));
}

}  // namespace

S3RE_HOOK(0x00405d10, DirectX_ThrowIfDDFailed, ThrowIfFailedFn, Original_DirectX_ThrowIfDDFailed,
          DirectX_ThrowIfDDFailed);

// Purpose: Turns a failed DirectDraw call into the game's C++ exception
// (CDirectDrawException with the error code and the name of the call); the
// framework catches it and reports the error.
std::uint32_t __stdcall DirectX_ThrowIfDDFailed(std::int32_t hr, const char* where) {
    if (hr < 0) {
        ThrowGameException(kDirectDrawExceptionCtor, throw_info::kDirectDrawException, hr, where);
    }
    return 0;
}

S3RE_HOOK(0x005bb890, DirectX_ThrowIfDPFailed, ThrowIfFailedFn, Original_DirectX_ThrowIfDPFailed,
          DirectX_ThrowIfDPFailed);

// Purpose: Turns a failed DirectPlay call (network play) into the game's
// C++ exception (CDirectPlayException); unlike the DirectDraw check, any
// result other than 0 counts as failed.
std::uint32_t __stdcall DirectX_ThrowIfDPFailed(std::int32_t hr, const char* where) {
    if (hr != 0) {
        ThrowGameException(kDirectPlayExceptionCtor, throw_info::kDirectPlayException, hr, where);
    }
    return 0;
}

S3RE_HOOK(0x005bf2e0, DirectX_CSurface_CheckResult, CheckResultFn, Original_DirectX_CSurface_CheckResult,
          DirectX_CSurface_CheckResult);

// Purpose: Checks the result of a DirectDraw call on a surface: a lost
// surface is restored through its owner, other failures become the game's
// DirectDraw exception.
std::int32_t __fastcall DirectX_CSurface_CheckResult(void* self, void* /*edx*/, std::int32_t hr, const char* where) {
    if (hr == kSurfaceLost) {
        void* owner = AsSurface(self)->owner;
        using Fn = void(__fastcall*)(void* self, void* edx);
        (*reinterpret_cast<Fn*>(*static_cast<std::uint8_t**>(owner) + kOwnerRestoreSurfaces))(owner, nullptr);
        return hr;
    }
    DirectX_ThrowIfDDFailed(hr, where);
    return hr;
}

S3RE_HOOK(0x005bf0c0, DirectX_CSurface_DDBlt, DDBltFn, Original_DirectX_CSurface_DDBlt, DirectX_CSurface_DDBlt);

// Purpose: Copies or fills a rectangle on a DirectDraw surface (the game's
// basic drawing call for the menus and the map), waiting while the card is
// busy and restoring lost surfaces.
std::uint32_t __fastcall DirectX_CSurface_DDBlt(void* self, void* /*edx*/, const RECT* dst, void* src,
                                                const RECT* srcRect, std::uint32_t flags, void* fx) {
    using BltFn = std::int32_t(__stdcall*)(void* surface, const RECT* dst, void* src, const RECT* srcRect,
                                           std::uint32_t flags, void* fx);
    std::int32_t hr;
    do {
        void* source = src != nullptr ? AsSurface(src)->surface : nullptr;
        void* surface = AsSurface(self)->surface;
        hr = (*reinterpret_cast<BltFn*>(*static_cast<std::uint8_t**>(surface) + kSurfaceBlt))(
            surface, dst, source, srcRect, flags | kDDBltWaitAsync, fx);
        if (hr != 0 && hr != kWasStillDrawing) {
            hr = DirectX_CSurface_CheckResult(self, nullptr, hr, texts::kDDBltName.c_str());
        }
        if (hr == kWasStillDrawing) {
            g_importSleep.get()(0);
        }
    } while (hr != 0);
    return 0;
}

S3RE_HOOK(0x005bf130, DirectX_CSurface_DDBltFast, DDBltFastFn, Original_DirectX_CSurface_DDBltFast,
          DirectX_CSurface_DDBltFast);

// Purpose: Copies a rectangle of one surface to a position on another
// without scaling (DirectDraw's fast blit), with the same waiting and error
// handling as DDBlt.
std::uint32_t __fastcall DirectX_CSurface_DDBltFast(void* self, void* /*edx*/, std::uint32_t x, std::uint32_t y,
                                                    void* src, const RECT* srcRect, std::uint32_t flags) {
    using BltFastFn = std::int32_t(__stdcall*)(void* surface, std::uint32_t x, std::uint32_t y, void* src,
                                               const RECT* srcRect, std::uint32_t flags);
    std::int32_t hr;
    do {
        void* surface = AsSurface(self)->surface;
        hr = (*reinterpret_cast<BltFastFn*>(*static_cast<std::uint8_t**>(surface) + kSurfaceBltFast))(
            surface, x, y, AsSurface(src)->surface, srcRect, flags | kDDBltFastWait);
        if (hr != 0 && hr != kWasStillDrawing) {
            hr = DirectX_CSurface_CheckResult(self, nullptr, hr, texts::kDDBltFastName.c_str());
        }
        if (hr == kWasStillDrawing) {
            g_importSleep.get()(0);
        }
    } while (hr != 0);
    return 0;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// A fake CSurface whose +8 is a fake COM surface (slot 0x14 Blt records its
// arguments and returns scripted results) and whose +0xc is a fake owner
// (slot 0x0c records the call). Failures throw the game's C++ exception;
// the test catches it with SEH and compares the thrown object's vtable and
// ThrowInfo. Both versions must make the same calls in the same order.

namespace {

struct BltLog {
    std::vector<std::uint32_t> words;
    std::vector<std::int32_t> results;  // what Blt returns, in order
    std::size_t next = 0;
    std::uint8_t* base = nullptr;       // pointers are logged relative to it
};
BltLog* g_bltLog = nullptr;

std::uint32_t Relative(const void* p) {
    return p == nullptr ? 0xffffffffu
                        : static_cast<std::uint32_t>(static_cast<const std::uint8_t*>(p) - g_bltLog->base);
}

std::int32_t __stdcall FakeBlt(void* surface, const RECT* dst, void* src, const RECT* srcRect, std::uint32_t flags,
                               void* fx) {
    BltLog& log = *g_bltLog;
    log.words.insert(log.words.end(), {0xb17u, Relative(surface), Relative(dst), Relative(src), Relative(srcRect),
                                       flags, Relative(fx)});
    if (fx != nullptr) {
        const auto* const blt = static_cast<const DDBLTFX*>(fx);
        log.words.push_back(blt->dwSize);
        log.words.push_back(blt->dwFillColor);
    }
    return log.next < log.results.size() ? log.results[log.next++] : 0;
}

std::int32_t __stdcall FakeBltFast(void* surface, std::uint32_t x, std::uint32_t y, void* src, const RECT* srcRect,
                                   std::uint32_t flags) {
    BltLog& log = *g_bltLog;
    log.words.insert(log.words.end(), {0xb1f7u, Relative(surface), x, y, Relative(src), Relative(srcRect), flags});
    return log.next < log.results.size() ? log.results[log.next++] : 0;
}

void __fastcall FakeRestore(void* owner, void*) {
    g_bltLog->words.insert(g_bltLog->words.end(), {0x7e57u, Relative(owner)});
}

void* g_comVtable[8] = {};
void* g_ownerVtable[8] = {};

struct Outcome {
    std::uint32_t eax = 0;
    bool threw = false;
    std::uint32_t thrownVtable = 0;
    std::uint32_t throwInfo = 0;
    std::uint32_t thrownNull = 0;
};

int Filter(EXCEPTION_POINTERS* info, Outcome* out) {
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    if (record->ExceptionCode != 0xE06D7363 || record->NumberParameters < 3) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    out->threw = true;
    const auto* object = reinterpret_cast<void* const*>(record->ExceptionInformation[1]);
    // The thrown value is a pointer (CDirectDrawException*): the record
    // holds the address of that pointer.
    const auto* thrown = object != nullptr ? static_cast<const std::uint32_t*>(*object) : nullptr;
    out->thrownNull = thrown == nullptr;
    out->thrownVtable = thrown != nullptr ? *thrown : 0;
    out->throwInfo = static_cast<std::uint32_t>(record->ExceptionInformation[2]);
    return EXCEPTION_EXECUTE_HANDLER;
}

// Calls through plain function pointers (no C++ objects in the __try frame).
struct Args {
    int op;
    void* self;
    std::int32_t hr;
    const char* where;
    const RECT* dst;
    void* src;
    const RECT* srcRect;
    std::uint32_t flags;
    void* fx;
};

std::uint32_t RunThrow(ThrowIfFailedFn fn, const Args* a) { return fn(a->hr, a->where); }
std::uint32_t RunCheck(CheckResultFn fn, const Args* a) {
    return static_cast<std::uint32_t>(fn(a->self, nullptr, a->hr, a->where));
}
std::uint32_t RunBlt(DDBltFn fn, const Args* a) {
    return fn(a->self, nullptr, a->dst, a->src, a->srcRect, a->flags, a->fx);
}
std::uint32_t RunBltFast(DDBltFastFn fn, const Args* a) {
    return fn(a->self, nullptr, a->flags >> 16, a->flags & 0xffff, a->src, a->srcRect, a->flags);
}

Outcome GuardedThrow(ThrowIfFailedFn fn, const Args* a) {
    Outcome out;
    __try {
        out.eax = RunThrow(fn, a);
    } __except (Filter(GetExceptionInformation(), &out)) {
    }
    return out;
}
Outcome GuardedCheck(CheckResultFn fn, const Args* a) {
    Outcome out;
    __try {
        out.eax = RunCheck(fn, a);
    } __except (Filter(GetExceptionInformation(), &out)) {
    }
    return out;
}
Outcome GuardedBlt(DDBltFn fn, const Args* a) {
    Outcome out;
    __try {
        out.eax = RunBlt(fn, a);
    } __except (Filter(GetExceptionInformation(), &out)) {
    }
    return out;
}
Outcome GuardedBltFast(DDBltFastFn fn, const Args* a) {
    Outcome out;
    __try {
        out.eax = RunBltFast(fn, a);
    } __except (Filter(GetExceptionInformation(), &out)) {
    }
    return out;
}

struct Functions {
    ThrowIfFailedFn throwIfFailed;
    CheckResultFn checkResult;
    DDBltFn ddblt;
    DDBltFastFn ddbltFast;
    ThrowIfFailedFn throwIfDPFailed;
};

Outcome Run(const Functions& f, int op, const Args* args) {
    switch (op) {
        case 0: return GuardedThrow(f.throwIfFailed, args);
        case 1: return GuardedCheck(f.checkResult, args);
        case 2: return GuardedBlt(f.ddblt, args);
        case 3: return GuardedBltFast(f.ddbltFast, args);
        default: return GuardedThrow(f.throwIfDPFailed, args);
    }
}

std::int32_t RandomResult(test::Rng& rng, bool allowFailure) {
    switch (rng.Below(allowFailure ? 6 : 4)) {
        case 0: return 0;
        case 1: return kWasStillDrawing;
        case 2: return kSurfaceLost;
        case 3: return static_cast<std::int32_t>(1 + rng.Below(0x7fffffff));
        case 4: return static_cast<std::int32_t>(0x80000000u | rng.Next32());
        default: return static_cast<std::int32_t>(0x88760000u | rng.Below(0x10000));
    }
}

void CompareSurface(test::Context& ctx, const Functions& candidate, int cases) {
    const Functions original{Original_DirectX_ThrowIfDDFailed, Original_DirectX_CSurface_CheckResult,
                             Original_DirectX_CSurface_DDBlt, Original_DirectX_CSurface_DDBltFast,
                             Original_DirectX_ThrowIfDPFailed};
    for (int i = 0; i < 8; ++i) {
        g_comVtable[i] = i == 7 ? reinterpret_cast<void*>(&FakeBltFast) : reinterpret_cast<void*>(&FakeBlt);
        g_ownerVtable[i] = reinterpret_cast<void*>(&FakeRestore);
    }
    std::vector<std::uint8_t> buffer(0x400);
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(5));
        ctx.rng().Fill(buffer.data(), buffer.size());
        std::uint8_t* b = buffer.data();
        AsSurface(b)->surface = b + 0x100;  // the surface's COM interface
        AsSurface(b)->owner = b + 0x140;    // its owner
        *reinterpret_cast<void**>(b + 0x100) = g_comVtable;   // fake objects: vtable at +0
        *reinterpret_cast<void**>(b + 0x140) = g_ownerVtable;
        AsSurface(b + 0x40)->surface = b + 0x180;  // a source surface's interface
        const bool allowFailure = ctx.rng().Chance(1, 3);
        BltLog logA, logB;
        logA.base = logB.base = b;
        const int results = static_cast<int>(ctx.rng().Below(6));
        for (int k = 0; k < results; ++k) {
            logA.results.push_back(RandomResult(ctx.rng(), allowFailure && k == results - 1));
        }
        logB.results = logA.results;
        Args args{};
        args.op = op;
        args.self = b;
        args.hr = RandomResult(ctx.rng(), true);
        args.where = texts::kDDBltName.c_str();
        args.dst = ctx.rng().Chance(1, 4) ? nullptr : reinterpret_cast<const RECT*>(b + 0x200);
        args.src = ctx.rng().Chance(1, 3) && op != 3 ? nullptr : b + 0x40;
        args.srcRect = ctx.rng().Chance(1, 4) ? nullptr : reinterpret_cast<const RECT*>(b + 0x220);
        args.flags = ctx.rng().Chance(1, 2) ? 0x400 : ctx.rng().Next32();
        args.fx = ctx.rng().Chance(1, 3) ? nullptr : b + 0x280;
        const std::vector<std::uint8_t> snapshot = buffer;
        g_bltLog = &logA;
        const Outcome a = Run(original, op, &args);
        const std::vector<std::uint8_t> afterA = buffer;
        buffer = snapshot;
        g_bltLog = &logB;
        const Outcome o = Run(candidate, op, &args);
        g_bltLog = nullptr;
        bool same = S3RE_EXPECT_EQ(a.threw, o.threw);
        if (!a.threw) {
            same &= S3RE_EXPECT_EQ(a.eax, o.eax);
        } else {
            same &= S3RE_EXPECT_EQ(a.thrownNull, o.thrownNull);
            same &= S3RE_EXPECT_EQ(a.thrownVtable, o.thrownVtable);
            same &= S3RE_EXPECT_EQ(a.throwInfo, o.throwInfo);
        }
        same &= S3RE_EXPECT(logA.words == logB.words);
        same &= S3RE_EXPECT_MEM_EQ(afterA.data(), buffer.data(), buffer.size());
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d, hr 0x%x", i, op, static_cast<unsigned>(args.hr));
            return;
        }
    }
}

// Wrong: a lost surface is not restored.
std::int32_t __fastcall WrongCheckResult(void* self, void* edx, std::int32_t hr, const char* where) {
    if (hr == kSurfaceLost) {
        return hr;
    }
    return DirectX_CSurface_CheckResult(self, edx, hr, where);
}

}  // namespace

S3RE_TEST(DirectXSurface_MatchOriginal) {
    CompareSurface(ctx, {&DirectX_ThrowIfDDFailed, &DirectX_CSurface_CheckResult, &DirectX_CSurface_DDBlt,
                         &DirectX_CSurface_DDBltFast, &DirectX_ThrowIfDPFailed},
                   20000);
}

S3RE_TEST(DirectXSurface_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSurface(probe, {&DirectX_ThrowIfDDFailed, &WrongCheckResult, &DirectX_CSurface_DDBlt,
                           &DirectX_CSurface_DDBltFast, &DirectX_ThrowIfDPFailed},
                   2000);
    S3RE_EXPECT(probe.failures() > 0);
}

// ---------------------------------------------------------------------------
// Batch 26: releasing and destroying surfaces
// ---------------------------------------------------------------------------
//
// DirectX::CSurface::vf01 (0x005bf2c0), our DirectX_CSurface_Release: ()
//   IUnknown::Release (COM slot 8) of the interface at +4 (not checked for
//   null; the destructor checks), then +4, +8, +0x18, +0x14 = 0. EAX = 0.
//   (+4 is the interface the surface was created with, +8 the one DDBlt
//   uses; probably IDirectDrawSurface and a later version of it.)
// DirectX::CSurface::~CSurface (0x005bf240): vtable 0x65cda8; if +4 is not
//   0, vf01 (called directly); ~CObject. EAX = 0 (vf01's result or the null
//   it tested; the original ~CObject leaves EAX alone, ours may not;
//   callers don't use it).
// DirectX::CSurface scalar deleting destructor (0x005bf220): (flags)
//   ~CSurface, operator delete if flags bit 0; EAX = this. ret 4.
// CLandscapeSurface::~CLandscapeSurface (0x0054be70): vtable 0x65a520;
//   operator delete of the blocks at +0x94, +0x9c, +0xa0, +0xa4 (not
//   +0x98); ~CSurface. EAX = 0. (The map's terrain surface; probably its
//   shading tables.)
// CLandscapeSurface scalar deleting destructor (0x0054be40): the same shape.

void __fastcall Win32_CObject_Destroy(void* self, void* edx);  // win32_classes.cpp

using SurfaceFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using SurfaceDeletingFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t flags);

std::uint32_t __fastcall DirectX_CSurface_Release(void* self, void* edx);
std::uint32_t __fastcall DirectX_CSurface_Destroy(void* self, void* edx);
void* __fastcall DirectX_CSurface_DeletingDestroy(void* self, void* edx, std::uint32_t flags);
std::uint32_t __fastcall CLandscapeSurface_Destroy(void* self, void* edx);
void* __fastcall CLandscapeSurface_DeletingDestroy(void* self, void* edx, std::uint32_t flags);

namespace {

}  // namespace

S3RE_HOOK(0x005bf2c0, DirectX_CSurface_Release, SurfaceFn, Original_DirectX_CSurface_Release,
          DirectX_CSurface_Release);

// Purpose: Releases a surface's DirectDraw interface and forgets it (when
// the surface is destroyed or recreated, e.g. after a mode change).
std::uint32_t __fastcall DirectX_CSurface_Release(void* self, void* /*edx*/) {
    framework::CSurface* const s = static_cast<framework::CSurface*>(self);
    void* surface = s->simple.createdInterface;
    using ReleaseFn = std::uint32_t(__stdcall*)(void* surface);
    (*reinterpret_cast<ReleaseFn*>(*static_cast<std::uint8_t**>(surface) + 8))(surface);
    s->simple.createdInterface = nullptr;
    s->simple.surface = nullptr;
    s->pixelFormat[0] = 0;  // no pixel format
    s->caps = 0;
    return 0;
}

S3RE_HOOK(0x005bf240, DirectX_CSurface_Destroy, SurfaceFn, Original_DirectX_CSurface_Destroy,
          DirectX_CSurface_Destroy);

// Purpose: Destructor of a DirectDraw surface wrapper: releases the
// interface if there is one.
std::uint32_t __fastcall DirectX_CSurface_Destroy(void* self, void* /*edx*/) {
    AsSurface(self)->vtable = vtables::kCSurface;
    if (AsSurface(self)->createdInterface != nullptr) {
        DirectX_CSurface_Release(self, nullptr);
    }
    Win32_CObject_Destroy(self, nullptr);
    return 0;
}

S3RE_HOOK(0x005bf220, DirectX_CSurface_DeletingDestroy, SurfaceDeletingFn,
          Original_DirectX_CSurface_DeletingDestroy, DirectX_CSurface_DeletingDestroy);

// Purpose: The compiler's deleting destructor of a surface wrapper
// (destroys it and frees it when bit 0 of flags is set).
void* __fastcall DirectX_CSurface_DeletingDestroy(void* self, void* /*edx*/, std::uint32_t flags) {
    DirectX_CSurface_Destroy(self, nullptr);
    if ((flags & 1) != 0) {
        msvc6::GameOperatorDelete(self);
    }
    return self;
}

S3RE_HOOK(0x0054be70, CLandscapeSurface_Destroy, SurfaceFn, Original_CLandscapeSurface_Destroy,
          CLandscapeSurface_Destroy);

// Purpose: Destructor of the landscape surface (the map terrain drawing
// surface): frees its four tables, then the surface.
std::uint32_t __fastcall CLandscapeSurface_Destroy(void* self, void* /*edx*/) {
    CLandscapeSurface* const landscape = AsLandscape(self);
    landscape->surface.simple.vtable = vtables::kCLandscapeSurface;
    msvc6::GameOperatorDelete(landscape->table94);
    msvc6::GameOperatorDelete(landscape->table9c);
    msvc6::GameOperatorDelete(landscape->colours);
    msvc6::GameOperatorDelete(landscape->tableA4);
    return DirectX_CSurface_Destroy(self, nullptr);
}

S3RE_HOOK(0x0054be40, CLandscapeSurface_DeletingDestroy, SurfaceDeletingFn,
          Original_CLandscapeSurface_DeletingDestroy, CLandscapeSurface_DeletingDestroy);

// Purpose: The compiler's deleting destructor of the landscape surface.
void* __fastcall CLandscapeSurface_DeletingDestroy(void* self, void* /*edx*/, std::uint32_t flags) {
    CLandscapeSurface_Destroy(self, nullptr);
    if ((flags & 1) != 0) {
        msvc6::GameOperatorDelete(self);
    }
    return self;
}

namespace {

// Tests: a fake COM interface whose Release logs the call; the surface's
// blocks are fresh game heap blocks per side (so both free real memory);
// the deleting destructors get heap-allocated objects when they delete.

std::vector<std::uint32_t>* g_releaseLog = nullptr;
std::uint8_t* g_releaseBase = nullptr;

std::uint32_t __stdcall FakeRelease(void* surface) {
    g_releaseLog->push_back(0x2e1u);
    g_releaseLog->push_back(static_cast<std::uint32_t>(static_cast<std::uint8_t*>(surface) - g_releaseBase));
    return 7;
}

void* g_releaseVtable[4] = {};

struct DestroyFunctions {
    SurfaceFn release;
    SurfaceFn destroy;
    SurfaceDeletingFn deleting;
    SurfaceFn landscape;
    SurfaceDeletingFn landscapeDeleting;
};

constexpr std::size_t kSurfaceObject = 0xb0;

// Runs op on a surface object at `object` (heap memory when it is deleted).
std::uint32_t RunDestroy(const DestroyFunctions& f, int op, std::uint8_t* object, std::uint32_t flags) {
    switch (op) {
        case 0: return f.release(object, nullptr);
        case 1: return f.destroy(object, nullptr);
        case 2: return static_cast<std::uint32_t>(static_cast<std::uint8_t*>(f.deleting(object, nullptr, flags)) - object);
        case 3: return f.landscape(object, nullptr);
        default: return static_cast<std::uint32_t>(static_cast<std::uint8_t*>(f.landscapeDeleting(object, nullptr, flags)) - object);
    }
}

void CompareDestroy(test::Context& ctx, const DestroyFunctions& candidate, int cases) {
    const DestroyFunctions original{Original_DirectX_CSurface_Release, Original_DirectX_CSurface_Destroy,
                                    Original_DirectX_CSurface_DeletingDestroy, Original_CLandscapeSurface_Destroy,
                                    Original_CLandscapeSurface_DeletingDestroy};
    for (void*& slot : g_releaseVtable) {
        slot = reinterpret_cast<void*>(&FakeRelease);
    }
    std::uint8_t pattern[kSurfaceObject];
    std::uint8_t iface[2][16];
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(5));
        const std::uint32_t flags = ctx.rng().Next32();
        const bool deletes = (op == 2 || op == 4) && (flags & 1) != 0;
        const bool noInterface = op != 0 && ctx.rng().Chance(1, 3);
        const bool nullBlocks = ctx.rng().Chance(1, 4);
        ctx.rng().Fill(pattern, sizeof(pattern));
        std::uint8_t* objects[2];
        std::vector<std::uint8_t> stackObjects[2];
        std::vector<std::uint32_t> logs[2];
        for (int side = 0; side < 2; ++side) {
            if (deletes) {
                objects[side] = static_cast<std::uint8_t*>(msvc6::GameOperatorNew(kSurfaceObject));
            } else {
                stackObjects[side].resize(kSurfaceObject);
                objects[side] = stackObjects[side].data();
            }
            std::memcpy(objects[side], pattern, kSurfaceObject);
            *reinterpret_cast<void**>(iface[side]) = g_releaseVtable;  // fake COM object: vtable at +0
            CLandscapeSurface* const object = AsLandscape(objects[side]);
            object->surface.simple.createdInterface = noInterface ? nullptr : iface[side];
            for (void** table : {&object->table94, &object->table9c, &object->colours, &object->tableA4}) {
                *table = nullBlocks ? nullptr : msvc6::GameOperatorNew(16);
            }
        }
        std::uint32_t results[2];
        for (int side = 0; side < 2; ++side) {
            g_releaseLog = &logs[side];
            g_releaseBase = iface[side];
            results[side] = RunDestroy(side == 0 ? original : candidate, op, objects[side], flags);
        }
        g_releaseLog = nullptr;
        if (op == 1 || op == 3) {
            // The destructors' EAX is what the (hooked) ~CObject leaves;
            // callers don't use it.
            results[0] = results[1] = 0;
        }
        bool same = S3RE_EXPECT_EQ(results[0], results[1]);
        same &= S3RE_EXPECT(logs[0] == logs[1]);
        if (!deletes) {
            for (int side = 0; side < 2; ++side) {
                CLandscapeSurface* const object = AsLandscape(objects[side]);
                object->surface.simple.createdInterface = nullptr;  // interface pointers differ per side
                for (void** table : {&object->table94, &object->table9c, &object->colours, &object->tableA4}) {
                    *table = nullptr;  // blocks differ per side
                }
            }
            same &= S3RE_EXPECT_MEM_EQ(objects[0], objects[1], kSurfaceObject);
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
            return;
        }
    }
}

std::uint32_t __fastcall WrongRelease(void* self, void* edx) {
    const std::uint32_t r = DirectX_CSurface_Release(self, edx);
    static_cast<framework::CSurface*>(self)->pixelFormat[0] = 1;  // wrong: +0x18 not cleared
    return r;
}

}  // namespace

S3RE_TEST(DirectXSurfaceDestroy_MatchOriginal) {
    CompareDestroy(ctx, {&DirectX_CSurface_Release, &DirectX_CSurface_Destroy, &DirectX_CSurface_DeletingDestroy,
                         &CLandscapeSurface_Destroy, &CLandscapeSurface_DeletingDestroy},
                   5000);
}

S3RE_TEST(DirectXSurfaceDestroy_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDestroy(probe, {&WrongRelease, &DirectX_CSurface_Destroy, &DirectX_CSurface_DeletingDestroy,
                           &CLandscapeSurface_Destroy, &CLandscapeSurface_DeletingDestroy},
                   500);
    S3RE_EXPECT(probe.failures() > 0);
}

// ---------------------------------------------------------------------------
// wip/logic-r4: CLandscapeSurface::CLandscapeSurface (0x0054bd30)
// ---------------------------------------------------------------------------
//
// (graphics) thiscall, ret 4, EAX = this: the COffscreenSurface base
//   (0x5bfab0); +0x50, +0x54, +0x84, +0x88, +0x8c, +0x9c = 0, +0x90 = the
//   CGraphicsData; vtable 0x65a520; then its four tables from operator new,
//   each checked with ThrowOutOfMemory (Volker\landscape.cpp): +0x94 0x364
//   bytes ("patternpointer", line 31), +0x9c 1,000,000 ("patterndata", 33),
//   +0xa0 0x202 ("palette", 35), +0xa4 0x10000 ("palettenarray", 37).
//   When a check throws, the original's unwinding destroys the base
//   (CSurface::~CSurface through the thunk 0x54be60); ours does the same
//   with a guard.

namespace {

constexpr std::uintptr_t kOffscreenSurfaceConstruct = 0x005bfab0;  // DirectX::COffscreenSurface::COffscreenSurface
constexpr std::uintptr_t kLandscapeBaseDestroy = 0x0054be60;       // jmp CSurface::~CSurface
constexpr std::uintptr_t kThrowOutOfMemory = 0x00610960;           // fastcall (expression, file; line)
constexpr std::uintptr_t kOperatorNewAddress = 0x0061efb9;

struct LandscapeTable {
    std::size_t field;    // offset in CLandscapeSurface
    std::uint32_t bytes;
    std::uintptr_t name;  // GameText address
    std::uint32_t line;
};

void* GameNew(std::uint32_t bytes) {
    using Fn = void*(__cdecl*)(std::uint32_t);
    return reinterpret_cast<Fn>(kOperatorNewAddress)(bytes);
}

// Destroys the base when the constructor is left by an exception.
struct BaseGuard {
    void* self;
    bool done = false;
    ~BaseGuard() {
        if (!done) {
            reinterpret_cast<SurfaceFn>(kLandscapeBaseDestroy)(self, nullptr);
        }
    }
};

}  // namespace

using LandscapeConstructFn = void*(__fastcall*)(void* self, void* edx, void* graphics);

// Purpose: Creates the landscape surface (the off-screen surface the map's
// terrain is drawn into) and allocates its pattern and palette tables.
void* __fastcall CLandscapeSurface_Construct(void* self, void* /*edx*/, void* graphics) {
    reinterpret_cast<SurfaceFn>(kOffscreenSurfaceConstruct)(self, nullptr);
    BaseGuard guard{self};
    CLandscapeSurface* const landscape = AsLandscape(self);
    landscape->unk50 = 0;
    landscape->unk54 = 0;
    landscape->unk84 = 0;
    landscape->unk88 = 0;
    landscape->unk8c = 0;
    landscape->graphics = graphics;
    landscape->table9c = nullptr;
    landscape->surface.simple.vtable = vtables::kCLandscapeSurface;
    static constexpr LandscapeTable kTables[] = {
        {offsetof(CLandscapeSurface, table94), 0x364, texts::kPatternPointer.address(), 31},
        {offsetof(CLandscapeSurface, table9c), 1000000, texts::kPatternData.address(), 33},
        {offsetof(CLandscapeSurface, colours), 0x202, texts::kPalette.address(), 35},
        {offsetof(CLandscapeSurface, tableA4), 0x10000, texts::kPalettenArray.address(), 37},
    };
    for (const LandscapeTable& table : kTables) {
        void* const block = GameNew(table.bytes);
        RawAt<void*>(landscape, table.field) = block;
        if (block == nullptr) {
            using ThrowFn = void(__fastcall*)(const char*, const char*, std::uint32_t);
            reinterpret_cast<ThrowFn>(kThrowOutOfMemory)(GameText(table.name).c_str(), texts::kLandscapeFile.c_str(), table.line);
        }
    }
    guard.done = true;
    return self;
}

S3RE_HOOK(0x0054bd30, CLandscapeSurface_Construct, LandscapeConstructFn, Original_CLandscapeSurface_Construct,
          CLandscapeSurface_Construct);

namespace {

// Tests: operator new is a fake that hands out static blocks (null for the
// chosen calls) and records the sizes; ThrowOutOfMemory a recording fake
// that returns (so both sides go on); the base constructor runs (verified).
// Compared: the object (block pointers as "which call", null or not), the
// log, EAX.

std::vector<std::uint32_t>* g_landscapeLog = nullptr;
std::uint32_t g_failMask = 0;
std::uint32_t g_newCalls = 0;
alignas(16) std::uint8_t g_landscapeBlocks[4][16];

void* __cdecl FakeLandscapeNew(std::uint32_t bytes) {
    const std::uint32_t call = g_newCalls++;
    g_landscapeLog->insert(g_landscapeLog->end(), {0x4e3u, bytes});
    if ((g_failMask >> call & 1) != 0 || call >= 4) {
        return nullptr;
    }
    return g_landscapeBlocks[call];
}

void __fastcall FakeThrowOutOfMemory(const char* expression, const char* file, std::uint32_t line) {
    g_landscapeLog->insert(g_landscapeLog->end(), {0x7e0u, reinterpret_cast<std::uint32_t>(expression),
                                                    reinterpret_cast<std::uint32_t>(file), line});
}

void CompareLandscapeConstruct(test::Context& ctx, LandscapeConstructFn candidate, int cases) {
    std::uint8_t pattern[kSurfaceObject];
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(pattern, sizeof(pattern));
        const std::uint32_t failMask = ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Below(16);
        void* const graphics = reinterpret_cast<void*>(static_cast<std::uintptr_t>(ctx.rng().Next32()));
        std::uint8_t objects[2][kSurfaceObject];
        std::vector<std::uint32_t> logs[2];
        std::uint32_t results[2];
        test::PatchJump(kOperatorNewAddress, reinterpret_cast<const void*>(&FakeLandscapeNew));
        test::PatchJump(kThrowOutOfMemory, reinterpret_cast<const void*>(&FakeThrowOutOfMemory));
        for (int side = 0; side < 2; ++side) {
            std::memcpy(objects[side], pattern, kSurfaceObject);
            g_landscapeLog = &logs[side];
            g_failMask = failMask;
            g_newCalls = 0;
            const LandscapeConstructFn f = side == 0 ? Original_CLandscapeSurface_Construct : candidate;
            results[side] = static_cast<std::uint32_t>(static_cast<std::uint8_t*>(f(objects[side], nullptr, graphics)) -
                                                       objects[side]);
        }
        test::UnpatchJump(kThrowOutOfMemory);
        test::UnpatchJump(kOperatorNewAddress);
        g_landscapeLog = nullptr;
        bool same = S3RE_EXPECT_EQ(results[0], results[1]);
        same &= S3RE_EXPECT(logs[0] == logs[1]);
        same &= S3RE_EXPECT_MEM_EQ(objects[0], objects[1], kSurfaceObject);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, fail mask %u", i, failMask);
            return;
        }
    }
}

// Wrong: +0x9c is not cleared before the allocations (shows when they fail).
void* __fastcall WrongLandscapeConstruct(void* self, void* edx, void* graphics) {
    void* const result = CLandscapeSurface_Construct(self, edx, graphics);
    AsLandscape(self)->unk8c = 1;
    return result;
}

}  // namespace

S3RE_TEST(CLandscapeSurface_Construct_MatchesOriginal) {
    CompareLandscapeConstruct(ctx, &CLandscapeSurface_Construct, 2000);
}

S3RE_TEST(CLandscapeSurface_Construct_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareLandscapeConstruct(probe, &WrongLandscapeConstruct, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
