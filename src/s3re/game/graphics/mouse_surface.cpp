// Dirk::CMouseSurface (Dirk\MenuShared.cpp): the software mouse cursor's
// surface.
//
// CMouseSurface::SetHotspot (0x00469130): thiscall (x, y) ret 8: under the
//   surface's critical section (+0x4c; Enter 0x60d670, Leave 0x60d680)
//   stores +0x6c = y, +0x68 = x: the offsets SetPosition 0x4691e0
//   subtracts from the mouse position. EAX = Leave's.
// CMouseSurface::Clear (0x00469310): thiscall (): under a CLock of +0x4c
//   (0x60da00 / 0x60da20): a zeroed DDSURFACEDESC (dwSize 0x6c) filled by
//   IDirectDrawSurface::GetSurfaceDesc (COM slot 0x58 of +8) and checked
//   by CSurface::CheckResult(hr, "CSurface::DDGetSurfaceDesc") (0x5bf2e0;
//   once more when that returns DDERR_SURFACELOST); CSurface::Lock(0,
//   &desc, 0) (0x5bf050); every 16-bit pixel of width x height zeroed row
//   by row (rows lPitch / 2 pixels apart, the width re-read per pixel);
//   IDirectDrawSurface::Unlock(lpSurface) (slot 0x80) checked as
//   "CSurface::DDUnlock" (retried once when lost); the DDOVERLAYFX at
//   +0x8c zeroed with dwSize 0x38 (and its +0xb4 / +0xb8 zeroed once more).
//   EAX = ~CLock's.

#include "game/texts.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kEnter = 0x0060d670;  // Win32::CCriticalSection::Enter
constexpr std::uintptr_t kLeave = 0x0060d680;  // Win32::CCriticalSection::Leave
constexpr std::uintptr_t kLockConstruct = 0x0060da00;  // Win32::CLock::CLock(section)
constexpr std::uintptr_t kLockDestroy = 0x0060da20;    // Win32::CLock::~CLock
constexpr std::uintptr_t kCheckResult = 0x005bf2e0;    // DirectX::CSurface::CheckResult(hr, where)
constexpr std::uintptr_t kSurfaceLock = 0x005bf050;    // DirectX::CSurface::Lock(rect, desc, flags)
constexpr std::uint32_t kSurfaceLost = 0x887601c2;     // DDERR_SURFACELOST
constexpr std::uint32_t kGetSurfaceDescSlot = 0x58 / 4;
constexpr std::uint32_t kUnlockSlot = 0x80 / 4;
constexpr std::uint32_t kSurfaceDescSize = 0x6c;       // sizeof(DDSURFACEDESC)
constexpr std::uint32_t kOverlayFxSize = 0x38;         // sizeof(DDOVERLAYFX)

#pragma pack(push, 1)
struct SurfaceDesc {             // DDSURFACEDESC
    std::uint32_t size;          // +0x00
    std::uint32_t flags;
    std::uint32_t height;        // +0x08
    std::uint32_t width;         // +0x0c
    std::uint32_t pitch;         // +0x10
    std::uint8_t unk14[0x24 - 0x14];
    std::uint8_t* surface;       // +0x24
    std::uint8_t unk28[0x6c - 0x28];
};
static_assert(sizeof(SurfaceDesc) == 0x6c);
static_assert(offsetof(SurfaceDesc, surface) == 0x24);

struct MouseSurfaceView {
    std::uint8_t unk00[0x08];
    void* interface;                 // +0x08 the IDirectDrawSurface
    std::uint8_t unk0c[0x4c - 0x0c];
    std::uint8_t lock[0x68 - 0x4c];  // +0x4c the CCriticalSection guarding the cursor
    std::int32_t hotspot_x;          // +0x68
    std::int32_t hotspot_y;          // +0x6c
    std::uint8_t unk70[0x8c - 0x70];
    std::uint32_t overlay_fx[14];    // +0x8c a DDOVERLAYFX (dwSize first)
};
#pragma pack(pop)
static_assert(offsetof(MouseSurfaceView, lock) == 0x4c);
static_assert(offsetof(MouseSurfaceView, hotspot_x) == 0x68);
static_assert(offsetof(MouseSurfaceView, overlay_fx) == 0x8c);
static_assert(sizeof(MouseSurfaceView) == 0xc4);
constexpr std::size_t kOverlayFxB4 = (0xb4 - 0x8c) / 4;  // two of its dwords Clear zeroes again

using MemberFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using LockConstructFn = void*(__fastcall*)(void* self, void* edx, void* section);
using CheckResultFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t hr, const char* where);
using SurfaceLockFn = std::uint32_t(__fastcall*)(void* self, void* edx, void* rect, SurfaceDesc* desc,
                                                 std::uint32_t flags);
using ComFn = std::uint32_t(__stdcall*)(void* self, const void* argument);

// A COM method of the surface's IDirectDrawSurface.
std::uint32_t CallSurface(MouseSurfaceView* surface, std::uint32_t slot, const void* argument) {
    void* const object = surface->interface;
    const auto* const vtable = *static_cast<ComFn* const*>(object);
    return vtable[slot](object, argument);
}

// The COM call and its check, repeated once when the surface was lost.
void CheckedSurfaceCall(MouseSurfaceView* surface, std::uint32_t slot, const void* argument, const char* where) {
    const auto check = reinterpret_cast<CheckResultFn>(kCheckResult);
    if (check(surface, nullptr, CallSurface(surface, slot, argument), where) == kSurfaceLost) {
        check(surface, nullptr, CallSurface(surface, slot, argument), where);
    }
}

}  // namespace

std::uint32_t __fastcall CMouseSurface_SetHotspot(void* self, void* edx, std::int32_t x, std::int32_t y);
using SetHotspotFn = decltype(&CMouseSurface_SetHotspot);

S3RE_HOOK(0x00469130, CMouseSurface_SetHotspot, SetHotspotFn, Original_CMouseSurface_SetHotspot,
          CMouseSurface_SetHotspot);

// Purpose: sets the cursor image's hotspot (the point of the image that is
// at the mouse position).
std::uint32_t __fastcall CMouseSurface_SetHotspot(void* self, void* /*edx*/, std::int32_t x, std::int32_t y) {
    auto* const surface = static_cast<MouseSurfaceView*>(self);
    reinterpret_cast<MemberFn>(kEnter)(surface->lock, nullptr);
    surface->hotspot_y = y;
    surface->hotspot_x = x;
    return reinterpret_cast<MemberFn>(kLeave)(surface->lock, nullptr);
}

std::uint32_t __fastcall CMouseSurface_Clear(void* self, void* edx);
using ClearFn = decltype(&CMouseSurface_Clear);

S3RE_HOOK(0x00469310, CMouseSurface_Clear, ClearFn, Original_CMouseSurface_Clear, CMouseSurface_Clear);

// Purpose: blanks the cursor surface (all pixels 0) before a new cursor
// image is drawn into it.
std::uint32_t __fastcall CMouseSurface_Clear(void* self, void* /*edx*/) {
    auto* const surface = static_cast<MouseSurfaceView*>(self);
    void* lock = nullptr;  // the scoped Win32::CLock
    reinterpret_cast<LockConstructFn>(kLockConstruct)(&lock, nullptr, surface->lock);
    SurfaceDesc desc{};
    desc.size = kSurfaceDescSize;
    CheckedSurfaceCall(surface, kGetSurfaceDescSlot, &desc, texts::kDDGetSurfaceDesc);
    reinterpret_cast<SurfaceLockFn>(kSurfaceLock)(surface, nullptr, nullptr, &desc, 0);
    auto* row = reinterpret_cast<std::uint16_t*>(desc.surface);
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {  // (the width is re-read per pixel)
            row[x] = 0;
        }
        row += desc.pitch >> 1;
    }
    CheckedSurfaceCall(surface, kUnlockSlot, desc.surface, texts::kDDUnlock);
    for (std::uint32_t& value : surface->overlay_fx) {
        value = 0;
    }
    surface->overlay_fx[0] = kOverlayFxSize;
    // (+0xb4 and +0xb8, inside the structure just cleared, are zeroed again.)
    surface->overlay_fx[kOverlayFxB4] = 0;
    surface->overlay_fx[kOverlayFxB4 + 1] = 0;
    return reinterpret_cast<MemberFn>(kLockDestroy)(&lock, nullptr);
}

// ---------------------------------------------------------------------------
// Tests: Enter / Leave stubbed (they record the section and the hotspot
// fields at the time of the call, so the stores must happen inside).
// ---------------------------------------------------------------------------

namespace {

std::vector<std::uint32_t> RunHotspot(SetHotspotFn fn, MouseSurfaceView* surface, std::int32_t x, std::int32_t y) {
    std::vector<std::uint32_t> log;
    test::CalleeStubs stubs;
    stubs.SetWorld(reinterpret_cast<CGameData*>(surface));
    auto record = [&](std::uint32_t tag, std::uint32_t self) {
        log.insert(log.end(), {tag, self, static_cast<std::uint32_t>(surface->hotspot_x),
                               static_cast<std::uint32_t>(surface->hotspot_y)});
        return tag + self;
    };
    stubs.Install<0, kEnter>([&](CGameData*, std::uint32_t self, const std::uint32_t*) { return record(1, self); });
    stubs.Install<0, kLeave>([&](CGameData*, std::uint32_t self, const std::uint32_t*) { return record(2, self); });
    log.push_back(fn(surface, nullptr, x, y));
    std::uint32_t words[sizeof(MouseSurfaceView) / 4];
    std::memcpy(words, surface, sizeof(words));
    log.insert(log.end(), words, words + sizeof(words) / 4);
    return log;
}

void CompareHotspot(test::Context& ctx, SetHotspotFn candidate, int cases) {
    for (int n = 0; n < cases; ++n) {
        test::Rng& rng = ctx.rng();
        MouseSurfaceView a;
        rng.Fill(&a, sizeof(a));
        MouseSurfaceView b = a;
        const auto x = static_cast<std::int32_t>(rng.Next32());
        const auto y = static_cast<std::int32_t>(rng.Next32());
        if (!S3RE_EXPECT(RunHotspot(Original_CMouseSurface_SetHotspot, &a, x, y) == RunHotspot(candidate, &b, x, y))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", n);
            break;
        }
    }
}

// Clear: CLock's constructor / destructor, CheckResult and CSurface::Lock
// stubbed (Lock writes a random description: up to 24 x 24 pixels of a
// test buffer, pitch at least the row), the surface's IDirectDrawSurface a
// fake whose GetSurfaceDesc / Unlock return scripted results (DD_OK or
// DDERR_SURFACELOST mostly). Compared: the calls, the surface object, the
// pixel buffer and EAX.
std::vector<std::uint32_t>* g_comLog = nullptr;
std::uint32_t g_comResults = 0;
std::uint32_t g_pixelBase = 0;

std::uint32_t NextComResult() {
    const std::uint32_t r = g_comResults & 3;
    g_comResults >>= 2;
    return r == 0 ? kSurfaceLost : r == 3 ? 0x88760005u : 0;
}

std::uint32_t __stdcall FakeGetSurfaceDesc(void* self, const void* desc) {
    std::uint32_t words[kSurfaceDescSize / 4];
    std::memcpy(words, desc, sizeof(words));
    g_comLog->push_back(0x58);
    g_comLog->insert(g_comLog->end(), words, words + kSurfaceDescSize / 4);
    (void)self;
    return NextComResult();
}

std::uint32_t __stdcall FakeUnlock(void* self, const void* rect) {
    g_comLog->insert(g_comLog->end(), {0x80, reinterpret_cast<std::uint32_t>(rect) - g_pixelBase});
    (void)self;
    return NextComResult();
}

struct FakeInterface {
    const void* const* vtable;
};

std::vector<std::uint32_t> RunClear(ClearFn fn, MouseSurfaceView* surface, std::uint16_t* pixels,
                                    std::uint32_t results, const std::uint32_t desc[4]) {
    std::vector<std::uint32_t> log;
    static const void* vtable[0x84 / 4] = {};
    vtable[kGetSurfaceDescSlot] = reinterpret_cast<const void*>(&FakeGetSurfaceDesc);
    vtable[kUnlockSlot] = reinterpret_cast<const void*>(&FakeUnlock);
    FakeInterface iface{vtable};
    surface->interface = &iface;
    g_comLog = &log;
    g_comResults = results;
    g_pixelBase = reinterpret_cast<std::uint32_t>(pixels);
    const auto base = reinterpret_cast<std::uint32_t>(surface);
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    stubs.Install<1, kLockConstruct>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        *reinterpret_cast<std::uint32_t*>(self) = a[0];
        log.insert(log.end(), {0xc1, a[0] - base});
        return self;
    });
    stubs.Install<0, kLockDestroy>([&](CGameData*, std::uint32_t self, const std::uint32_t*) {
        log.insert(log.end(), {0xd1, *reinterpret_cast<std::uint32_t*>(self) - base});
        return 0x1eaeu;
    });
    stubs.Install<2, kCheckResult>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.insert(log.end(), {0xcc, self - base, a[0], a[1]});
        return a[0];
    });
    stubs.Install<3, kSurfaceLock>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        auto* const d = reinterpret_cast<SurfaceDesc*>(a[1]);
        log.insert(log.end(), {0x10c, self - base, a[0], a[2], d->size});
        d->height = desc[0];
        d->width = desc[1];
        d->pitch = desc[2];
        d->surface = reinterpret_cast<std::uint8_t*>(pixels) + desc[3];
        return 0u;
    });
    log.push_back(fn(surface, nullptr));
    surface->interface = nullptr;
    std::uint32_t words[sizeof(MouseSurfaceView) / 4];
    std::memcpy(words, surface, sizeof(words));
    log.insert(log.end(), words, words + sizeof(words) / 4);
    g_comLog = nullptr;
    return log;
}

void CompareClear(test::Context& ctx, ClearFn candidate, int cases) {
    constexpr std::uint32_t kPixels = 24 * 48 + 64;
    std::vector<std::uint16_t> pa(kPixels), pb(kPixels);
    for (int n = 0; n < cases; ++n) {
        test::Rng& rng = ctx.rng();
        MouseSurfaceView a;
        rng.Fill(&a, sizeof(a));
        MouseSurfaceView b = a;
        rng.Fill(pa.data(), pa.size() * 2);
        pb = pa;
        const std::uint32_t width = rng.Below(25);
        const std::uint32_t desc[4] = {rng.Below(25), width, 2 * (width + rng.Below(24)) + rng.Below(2),
                                       2 * rng.Below(16)};
        const std::uint32_t results = rng.Chance(1, 2) ? 0xaaaaaaaau ^ rng.Next32() : rng.Next32() & 0x55555555u;
        const std::vector<std::uint32_t> la = RunClear(Original_CMouseSurface_Clear, &a, pa.data(), results, desc);
        const std::vector<std::uint32_t> lb = RunClear(candidate, &b, pb.data(), results, desc);
        bool same = S3RE_EXPECT(la == lb);
        same = S3RE_EXPECT(pa == pb) && same;
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: %u x %u, pitch %u", n, desc[1], desc[0], desc[2]);
            break;
        }
    }
}

// Wrong: the rows one pixel apart too many.
std::uint32_t __fastcall WrongClear(void* self, void* edx) {
    auto* const surface = static_cast<MouseSurfaceView*>(self);
    const std::uint32_t eax = CMouseSurface_Clear(self, edx);
    surface->overlay_fx[kOverlayFxB4] = 1;
    return eax;
}

// Wrong: stores after leaving the section.
std::uint32_t __fastcall WrongSetHotspot(void* self, void* /*edx*/, std::int32_t x, std::int32_t y) {
    auto* const surface = static_cast<MouseSurfaceView*>(self);
    reinterpret_cast<MemberFn>(kEnter)(surface->lock, nullptr);
    const std::uint32_t eax = reinterpret_cast<MemberFn>(kLeave)(surface->lock, nullptr);
    surface->hotspot_y = y;
    surface->hotspot_x = x;
    return eax;
}

}  // namespace

S3RE_TEST(MouseHotspot_MatchesOriginal) {
    CompareHotspot(ctx, &CMouseSurface_SetHotspot, 500);
}

S3RE_TEST(MouseClear_MatchesOriginal) {
    CompareClear(ctx, &CMouseSurface_Clear, 3000);
}

S3RE_TEST(MouseClear_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareClear(probe, &WrongClear, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(MouseHotspot_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareHotspot(probe, &WrongSetHotspot, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
