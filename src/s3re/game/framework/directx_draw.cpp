// The DirectDraw wrapper classes (DirectX\Ddraw.cpp; class names from the
// RTTI, error names from the strings each call passes to ThrowIfDDFailed /
// CSurface::CheckResult; re/directx_framework.md "Surface classes").
//
// DirectX::CDirectDraw (vtable 0x65cd80; framework::CDirectDraw): +4 the
// IDirectDraw2, +8 "owns it". Its virtual slots used here: 3 (0x0c)
// RestoreSurfaces, 4 (0x10) restore every registered surface
// (CGameDrawer::RestoreSurfaces), 5 (0x14) ReleaseSurfaces(bool),
// 6 (0x18) ReleasePalettes(bool).
//   CDirectDraw(other) 0x005bead0: CObject; +4 = 0; owns = (other == 0); the
//     vtable; +4 = other's IDirectDraw2 when other is given. ret 4.
//   ~CDirectDraw 0x005beb30: the vtable; Disconnect when +4 is set; ~CObject.
//   scalar deleting destructor 0x005beb10.
//   DDDirectDrawEnumerate(callback, context) 0x005beb90: LoadLibraryA
//     ("ddraw.dll") and GetProcAddress("DirectDrawEnumerateA"); a null
//     result is reported as E_NOTIMPL (ThrowIfDDFailed .1 / .2, which
//     throws); calls it, ThrowIfDDFailed(.3). ret 8.
//   Connect(device GUID*) 0x005bec00: CoCreateInstance(CLSID_DirectDraw, 0,
//     CLSCTX_ALL, IID_IDirectDraw2, &+4), IDirectDraw2::Initialize(device).
//   Disconnect 0x005bec50: slots 5 and 6 with 0; IUnknown::Release of +4 if
//     owned; +4 = 0. EAX: Release's result, otherwise slot 6's result with
//     the low byte replaced by the (zero) owns flag.
//   DDCreateSurface(desc, out) 0x005bec80: IDirectDraw2::CreateSurface,
//     then QueryInterface for IDirectDrawSurface3: E_NOINTERFACE keeps the
//     first interface (AddRef, version 1), otherwise version 3 (failures
//     throw); *out = that interface; the created one is released. EAX = the
//     version. The out locals start as the original's stack slots: the
//     created surface as the `desc` argument (its stack slot), the version 3
//     interface as `this` (the pushed ECX).
//   DDEnumDisplayModes, DDRestoreDisplayMode, DDGetDisplayMode (zeroes the
//     DDSURFACEDESC, dwSize 0x6c), DDGetCaps (zeroes both DDCAPS, dwSize
//     0x13c each), DDWaitForVerticalBlank: one IDirectDraw2 call each,
//     ThrowIfDDFailed with the method's name; EAX = 0.
//   RestoreSurfaces 0x005bee30 (slot 3): the application active -> slot 4;
//     otherwise throws CAppNotActiveException.
//
// DirectX::CSimpleSurface / CSurface (framework::CSurface): +4 the created
// interface, +8 the drawing interface, +0xc the owning CDirectDraw, +0x10
// the interface version, +0x14 caps, +0x18 pixel format, +0x38 RECT.
//   DDLock(rect, desc, flags, event) 0x005bf010: IDirectDrawSurface::Lock on
//     +8; results other than DD_OK and DDERR_WASSTILLDRAWING go through
//     CheckResult. EAX = the result.
//   Lock(rect, desc, flags) 0x005bf050: zeroes the DDSURFACEDESC (dwSize
//     0x6c), flags 0 -> DDLOCK_WAIT, masked to 0x33 for interface versions
//     1 and 2 and 0x833 for 3; repeats DDLock (Sleep(0) while still drawing)
//     until it returns DD_OK. EAX = 0.
//   DDGetBltStatus(flags) 0x005bf1a0: GetBltStatus; DD_OK,
//     DDERR_WASSTILLDRAWING and DDERR_NOBLTHW are returned as they are,
//     others through CheckResult.
//   GetInterface 0x005bf1e0: AddRef of +4 (if set); returns +4.
//   UseOwnInterfaceForDrawing 0x005bf2a0: +8 = +4. SetDirectDraw 0x005bf2b0,
//   SetCaps 0x005bf3d0, SetPixelFormat 0x005bf3e0 (32 bytes): setters.
//   DDGetSurfaceDesc 0x005bf310: GetSurfaceDesc into a local (once more
//     after DDERR_SURFACELOST), then SetRect(+0x38, 0, 0, width, height);
//     EAX = SetRect's result.
//   RestoreIfLost 0x005bf390 (slot 2): IsLost on +4: DD_OK -> 0; otherwise
//     (errors other than SURFACELOST through CheckResult "CSurface::IsLost")
//     DDRestore and 1.
//   DDRestore 0x005bf400: Restore on +4; DDERR_WRONGMODE and
//     DDERR_NOEXCLUSIVEMODE -> DebugLog and CAppNotActiveException; other
//     failures -> CDirectDrawException("CSurface::DDRestore"). EAX = result.
//
// The classes with a virtual CSurface base (CFlipSurface, CPrimarySurface,
// CPrimaryFlipSurface) find it through vbtable[1]; their deleting
// destructors and destructors are called with the CSurface part.
//   CFlipSurface(backBuffers, mostDerived) 0x005bf5c0; ~CFlipSurface
//     0x005bf640: deletes the objects at +8 and +0xc (slot 0 with 1);
//     deleting destructor 0x005bf610. GetBackBuffer 0x005bf670: once, a new
//     CSimpleSurface(owner, +8, +8, version) of the base (ThrowOutOfMemory
//     "lpBackBuffer" if new fails). DDFlip(target, flags) 0x005bf740:
//     IDirectDrawSurface::Flip on the base's +4 (errors other than still
//     drawing through CheckResult; EAX = the Flip result); Flip(target)
//     0x005bf710 repeats DDFlip(target, 0) until DD_OK.
//   CPrimarySurface(mostDerived) 0x005bf790, deleting destructor 0x005bf7d0,
//     Create 0x005bf800 (DDSCAPS_PRIMARYSURFACE).
//   CPrimaryFlipSurface(backBuffers, mostDerived) 0x005bf890, destructor
//     0x005bf960, deleting destructor 0x005bf930, Create 0x005bf9b0
//     (PRIMARY|FLIP|COMPLEX with one back buffer, then GetAttachedSurface
//     into the base's +8: for one back buffer with DDSCAPS_BACKBUFFER, for
//     two with no caps; more asserts "mehr als Triplebuffering...").
//   The primary Creates fill only the DDSURFACEDESC fields they use; the
//     rest of the original's stack local is uninitialized (ours is zero;
//     DirectDraw ignores fields dwFlags doesn't name).
// COffscreenSurface (0x005bfab0, deleting 0x005bfad0, Create(width, height)
// 0x005bfaf0 DDSCAPS_OFFSCREENPLAIN, RestoreIfLost 0x005bfb80 clears
// +0x4c after a restore), COverlaySurface (0x005bfba0, Create 0x005bfbc0
// OVERLAY|VIDEOMEMORY, Hide 0x005bfc50, DDUpdateOverlay 0x005bfc70),
// CPalette::Release 0x005bfd60.

#include "game/framework/directx_draw.h"

#include "game/framework/app_not_active.h"
#include "game/framework/framework_layout.h"
#include "game/globals.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

using framework::CDirectDraw;
using framework::CFlipSurface;
using framework::COffscreenSurface;
using framework::CPalette;
using framework::CSimpleSurface;
using framework::CSurface;

constexpr std::uintptr_t kThrowIfDDFailed = 0x00405d10;
constexpr std::uintptr_t kCheckResult = 0x005bf2e0;       // CSurface::CheckResult
constexpr std::uintptr_t kDebugLog = 0x00428720;
constexpr std::uintptr_t kGetApplication = 0x005b8c00;
constexpr std::uintptr_t kApplicationIsActive = 0x005b8c40;
constexpr std::uintptr_t kCObjectConstruct = 0x00610320;
constexpr std::uintptr_t kCObjectDestroy = 0x00610330;
constexpr std::uintptr_t kSurfaceConstruct = 0x005bf200;  // CSurface::CSurface
constexpr std::uintptr_t kSurfaceDestroy = 0x005bf240;    // CSurface::~CSurface
constexpr std::uintptr_t kSimpleSurfaceConstruct4 = 0x005befe0;
constexpr std::uintptr_t kSurfaceDestroyThunk = 0x0054be60;  // a jump to ~CSurface
constexpr std::uintptr_t kSurfaceRestoreIfLost = 0x005bf390;
constexpr std::uintptr_t kPrimarySurfaceConstruct = 0x005bf790;
constexpr std::uintptr_t kFlipSurfaceConstruct = 0x005bf5c0;
constexpr std::uintptr_t kThrowOutOfMemory = 0x00610960;
constexpr std::uintptr_t kThrowAssertmentFailed = 0x006119b0;

// HRESULTs (ddraw.h, winerror.h).
constexpr std::int32_t kOk = 0;
constexpr std::int32_t kNotImplemented = static_cast<std::int32_t>(0x80004001);  // E_NOTIMPL
constexpr std::int32_t kNoInterface = static_cast<std::int32_t>(0x80004002);     // E_NOINTERFACE
constexpr std::int32_t kSurfaceLost = static_cast<std::int32_t>(0x887601c2);     // DDERR_SURFACELOST
constexpr std::int32_t kWasStillDrawing = static_cast<std::int32_t>(0x8876021c);  // DDERR_WASSTILLDRAWING
constexpr std::int32_t kNoBltHardware = static_cast<std::int32_t>(0x8876023f);   // DDERR_NOBLTHW
constexpr std::int32_t kWrongMode = static_cast<std::int32_t>(0x8876024b);       // DDERR_WRONGMODE
constexpr std::int32_t kNoExclusiveMode = static_cast<std::int32_t>(0x887600e1);  // DDERR_NOEXCLUSIVEMODE

constexpr std::uint32_t kAllContexts = 7;  // CLSCTX_ALL

// COM methods (byte offsets into the interface vtables).
constexpr std::uint32_t kComQueryInterface = 0x00;
constexpr std::uint32_t kComAddRef = 0x04;
constexpr std::uint32_t kComRelease = 0x08;
// IDirectDraw2
constexpr std::uint32_t kDDCreateSurface = 0x18;
constexpr std::uint32_t kDDEnumDisplayModes = 0x20;
constexpr std::uint32_t kDDGetCaps = 0x2c;
constexpr std::uint32_t kDDGetDisplayMode = 0x30;
constexpr std::uint32_t kDDInitialize = 0x48;
constexpr std::uint32_t kDDRestoreDisplayMode = 0x4c;
constexpr std::uint32_t kDDWaitForVerticalBlank = 0x58;
// IDirectDrawSurface
constexpr std::uint32_t kSurfFlip = 0x2c;
constexpr std::uint32_t kSurfGetAttachedSurface = 0x30;
constexpr std::uint32_t kSurfGetBltStatus = 0x34;
constexpr std::uint32_t kSurfGetSurfaceDesc = 0x58;
constexpr std::uint32_t kSurfIsLost = 0x60;
constexpr std::uint32_t kSurfLock = 0x64;
constexpr std::uint32_t kSurfRestore = 0x6c;
constexpr std::uint32_t kSurfUpdateOverlay = 0x84;

// CDirectDraw's own virtual slots (byte offsets).
constexpr std::uint32_t kSlotRestoreAllSurfaces = 0x10;  // slot 4
constexpr std::uint32_t kSlotReleaseSurfaces = 0x14;     // slot 5 (bool)
constexpr std::uint32_t kSlotReleasePalettes = 0x18;     // slot 6 (bool)
constexpr std::uint32_t kSlotDelete = 0x00;              // scalar deleting destructor

// DDSURFACEDESC (0x6c bytes) and DDCAPS (0x13c of the 0x16c the game zeroes).
constexpr std::uint32_t kSurfaceDescBytes = 0x6c;
constexpr std::uint32_t kCapsZeroBytes = 0x16c;
constexpr std::uint32_t kCapsSize = 0x13c;
constexpr std::uint32_t kPixelFormatBytes = 32;
// DDSURFACEDESC fields (dword index) and flags.
constexpr std::size_t kDescSize = 0;
constexpr std::size_t kDescFlags = 1;
constexpr std::size_t kDescHeight = 2;
constexpr std::size_t kDescWidth = 3;
constexpr std::size_t kDescBackBufferCount = 5;
constexpr std::size_t kDescPixelFormat = 0x48 / 4;
constexpr std::size_t kDescCaps = 0x68 / 4;
constexpr std::uint32_t kSdCaps = 0x1;              // DDSD_CAPS
constexpr std::uint32_t kSdSize = 0x6;              // DDSD_HEIGHT | DDSD_WIDTH
constexpr std::uint32_t kSdBackBufferCount = 0x20;  // DDSD_BACKBUFFERCOUNT
constexpr std::uint32_t kSdPixelFormat = 0x1000;    // DDSD_PIXELFORMAT
constexpr std::uint32_t kCapsBackBuffer = 0x4;          // DDSCAPS_BACKBUFFER
constexpr std::uint32_t kCapsFlip = 0x10;               // DDSCAPS_FLIP
constexpr std::uint32_t kCapsComplex = 0x8;             // DDSCAPS_COMPLEX
constexpr std::uint32_t kCapsOffscreenPlain = 0x40;     // DDSCAPS_OFFSCREENPLAIN
constexpr std::uint32_t kCapsOverlay = 0x80;            // DDSCAPS_OVERLAY
constexpr std::uint32_t kCapsPrimarySurface = 0x200;    // DDSCAPS_PRIMARYSURFACE
constexpr std::uint32_t kCapsVideoMemory = 0x4000;      // DDSCAPS_VIDEOMEMORY
constexpr std::uint32_t kLockWait = 0x1;                // DDLOCK_WAIT
constexpr std::uint32_t kLockFlagsVersion1 = 0x33;      // the DDLOCK flags of IDirectDrawSurface
constexpr std::uint32_t kLockFlagsVersion3 = 0x833;     // ... and of IDirectDrawSurface3
constexpr std::uint32_t kOverlayHide = 0x200;           // DDOVER_HIDE
// Ddraw.cpp lines of the error reports.
constexpr std::uint32_t kLineBackBuffer = 627;      // 0x273
constexpr std::uint32_t kLineTripleBuffering = 734;  // 0x2de

template <typename Fn>
Fn CallAt(std::uintptr_t address) {
    return reinterpret_cast<Fn>(address);
}

template <typename Fn>
Fn Method(const void* object, std::uint32_t offset) {
    return *reinterpret_cast<Fn const*>(*static_cast<const std::uint8_t* const*>(object) + offset);
}

using ThisFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using ThisArgFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a);
using ComFn = std::int32_t(__stdcall*)(void* object);
using ComArgFn = std::int32_t(__stdcall*)(void* object, std::uint32_t a);
using ComPtrFn = std::int32_t(__stdcall*)(void* object, const void* a);
using ComTwoFn = std::int32_t(__stdcall*)(void* object, const void* a, const void* b);
using ComThreeFn = std::int32_t(__stdcall*)(void* object, const void* a, const void* b, const void* c);
using ComFourFn = std::int32_t(__stdcall*)(void* object, const void* a, const void* b, const void* c,
                                           const void* d);
using ComFiveFn = std::int32_t(__stdcall*)(void* object, const void* a, const void* b, const void* c,
                                           const void* d, const void* e);

// A call of one of our replacements through its S3.EXE address (the hook),
// as the original calls it.
template <typename Fn>
Fn ViaGame(std::uintptr_t address, Fn /*replacement*/) {
    return reinterpret_cast<Fn>(address);
}

std::int32_t ComCall(void* object, std::uint32_t offset) {
    return Method<ComFn>(object, offset)(object);
}

std::uint32_t Address(const void* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
}

void* AsPointer(std::uint32_t value) {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(value));
}

const void* AsConst(std::uint32_t value) {
    return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(value));
}

void ThrowIfDDFailed(std::int32_t hr, const char* where) {
    using Fn = std::uint32_t(__stdcall*)(std::int32_t hr, const char* where);
    CallAt<Fn>(kThrowIfDDFailed)(hr, where);
}

// CSurface::CheckResult: restores lost surfaces, throws for failures;
// returns hr.
std::int32_t CheckResult(void* surface, std::int32_t hr, const char* where) {
    using Fn = std::int32_t(__fastcall*)(void* self, void* edx, std::int32_t hr, const char* where);
    return CallAt<Fn>(kCheckResult)(surface, nullptr, hr, where);
}

CDirectDraw* AsDirectDraw(void* self) { return static_cast<CDirectDraw*>(self); }
CSurface* AsSurface(void* self) { return static_cast<CSurface*>(self); }

// The CSurface part of an object with a virtual CSurface base: its vbptr
// is at `part`, vbtable[1] the offset from there.
CSurface* VirtualBase(void* part) {
    const auto* const vbtable = reinterpret_cast<const std::int32_t*>(
        static_cast<std::uintptr_t>(*static_cast<const std::uint32_t*>(part)));
    return reinterpret_cast<CSurface*>(static_cast<std::uint8_t*>(part) + vbtable[1]);
}

void SetVirtualBaseVtable(void* part, std::uint32_t vtable) {
    VirtualBase(part)->simple.vtable = vtable;
}

// The DDSURFACEDESC the Creates pass to CDirectDraw::DDCreateSurface (the
// fields set; see the file comment for the primary surfaces' others).
struct SurfaceDesc {
    std::uint32_t words[kSurfaceDescBytes / 4] = {};
};

// The common tail of every Create: the owner creates the surface into +4,
// the version goes to +0x10.
void CreateWithOwner(CSurface* surface, SurfaceDesc& desc) {
    CSimpleSurface& simple = surface->simple;
    simple.version = ViaGame(0x005bec80, DirectX_CDirectDraw_DDCreateSurface)(simple.owner, nullptr, desc.words,
                                                                               &simple.createdInterface);
}

// A non-zero pixel format goes into the description (DDSD_PIXELFORMAT).
void AddPixelFormat(const CSurface* surface, SurfaceDesc& desc, std::uint32_t flagsWithFormat) {
    if (surface->pixelFormat[0] != 0) {
        desc.words[kDescFlags] = flagsWithFormat;
        std::memcpy(&desc.words[kDescPixelFormat], surface->pixelFormat, kPixelFormatBytes);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// DirectX::CDirectDraw
// ---------------------------------------------------------------------------

S3RE_HOOK(0x005bead0, DirectX_CDirectDraw_Construct, decltype(&DirectX_CDirectDraw_Construct),
          Original_DirectX_CDirectDraw_Construct, DirectX_CDirectDraw_Construct);

// Purpose: Constructor of the DirectDraw wrapper; constructed from another
// one it shares (but does not own) that one's DirectDraw object.
void* __fastcall DirectX_CDirectDraw_Construct(void* self, void* /*edx*/, const void* other) {
    CallAt<ThisFn>(kCObjectConstruct)(self, nullptr);
    CDirectDraw* const draw = AsDirectDraw(self);
    draw->directDraw = nullptr;
    draw->owns = other == nullptr ? 1 : 0;
    draw->vtable = vtables::kCDirectDraw;
    if (other != nullptr) {
        draw->directDraw = static_cast<const CDirectDraw*>(other)->directDraw;
    }
    return self;
}

S3RE_HOOK(0x005beb30, DirectX_CDirectDraw_Destroy, decltype(&DirectX_CDirectDraw_Destroy),
          Original_DirectX_CDirectDraw_Destroy, DirectX_CDirectDraw_Destroy);

// Purpose: Destructor of the DirectDraw wrapper: disconnects from
// DirectDraw if still connected.
std::uint32_t __fastcall DirectX_CDirectDraw_Destroy(void* self, void* /*edx*/) {
    CDirectDraw* const draw = AsDirectDraw(self);
    draw->vtable = vtables::kCDirectDraw;
    if (draw->directDraw != nullptr) {
        ViaGame(0x005bec50, DirectX_CDirectDraw_Disconnect)(self, nullptr);
    }
    return CallAt<ThisFn>(kCObjectDestroy)(self, nullptr);
}

S3RE_HOOK(0x005beb10, DirectX_CDirectDraw_DeletingDestroy, decltype(&DirectX_CDirectDraw_DeletingDestroy),
          Original_DirectX_CDirectDraw_DeletingDestroy, DirectX_CDirectDraw_DeletingDestroy);

// Purpose: The compiler's deleting destructor of the DirectDraw wrapper.
void* __fastcall DirectX_CDirectDraw_DeletingDestroy(void* self, void* /*edx*/, std::uint32_t flags) {
    ViaGame(0x005beb30, DirectX_CDirectDraw_Destroy)(self, nullptr);
    if ((flags & 1) != 0) {
        msvc6::GameOperatorDelete(self);
    }
    return self;
}

S3RE_HOOK(0x005beb90, DirectX_CDirectDraw_DDDirectDrawEnumerate,
          decltype(&DirectX_CDirectDraw_DDDirectDrawEnumerate), Original_DirectX_CDirectDraw_DDDirectDrawEnumerate,
          DirectX_CDirectDraw_DDDirectDrawEnumerate);

// Purpose: Lists the DirectDraw devices (graphics cards) through ddraw.dll's
// DirectDrawEnumerateA, calling `callback` for each (the game collects them
// for the device choice).
std::uint32_t __fastcall DirectX_CDirectDraw_DDDirectDrawEnumerate(void* /*self*/, void* /*edx*/, void* callback,
                                                                   void* context) {
    void* const library = g_importLoadLibraryA.get()(texts::kDDrawDll.c_str());
    if (library == nullptr) {
        ThrowIfDDFailed(kNotImplemented, texts::kDDEnumerate1.c_str());
    }
    void* const procedure = g_importGetProcAddress.get()(library, texts::kDirectDrawEnumerateA.c_str());
    if (procedure == nullptr) {
        ThrowIfDDFailed(kNotImplemented, texts::kDDEnumerate2.c_str());
    }
    using EnumerateFn = std::int32_t(__stdcall*)(void* callback, void* context);
    const std::int32_t hr = reinterpret_cast<EnumerateFn>(procedure)(callback, context);
    ThrowIfDDFailed(hr, texts::kDDEnumerate3.c_str());
    return 0;
}

S3RE_HOOK(0x005bec00, DirectX_CDirectDraw_Connect, decltype(&DirectX_CDirectDraw_Connect),
          Original_DirectX_CDirectDraw_Connect, DirectX_CDirectDraw_Connect);

// Purpose: Creates the DirectDraw object (through COM) and initializes it
// for a device (null: the primary display driver).
std::uint32_t __fastcall DirectX_CDirectDraw_Connect(void* self, void* /*edx*/, const void* device) {
    CDirectDraw* const draw = AsDirectDraw(self);
    std::int32_t hr = g_importCoCreateInstance.get()(&g_clsidDirectDraw, nullptr, kAllContexts, &g_iidDirectDraw2,
                                                     &draw->directDraw);
    ThrowIfDDFailed(hr, texts::kDDConnect1.c_str());
    void* const directDraw = draw->directDraw;
    hr = Method<ComPtrFn>(directDraw, kDDInitialize)(directDraw, device);
    ThrowIfDDFailed(hr, texts::kDDConnect2.c_str());
    return 0;
}

S3RE_HOOK(0x005bec50, DirectX_CDirectDraw_Disconnect, decltype(&DirectX_CDirectDraw_Disconnect),
          Original_DirectX_CDirectDraw_Disconnect, DirectX_CDirectDraw_Disconnect);

// Purpose: Releases the surfaces and palettes (through the virtual slots)
// and then the DirectDraw object, if this wrapper owns it.
std::uint32_t __fastcall DirectX_CDirectDraw_Disconnect(void* self, void* /*edx*/) {
    CDirectDraw* const draw = AsDirectDraw(self);
    Method<ThisArgFn>(self, kSlotReleaseSurfaces)(self, nullptr, 0);
    std::uint32_t eax = Method<ThisArgFn>(self, kSlotReleasePalettes)(self, nullptr, 0);
    // The original tests the flag in AL, so EAX keeps slot 6's upper bytes.
    eax = (eax & 0xffffff00u) | draw->owns;
    if (draw->owns != 0) {
        void* const directDraw = draw->directDraw;
        eax = static_cast<std::uint32_t>(ComCall(directDraw, kComRelease));
    }
    draw->directDraw = nullptr;
    return eax;
}

S3RE_HOOK(0x005bec80, DirectX_CDirectDraw_DDCreateSurface, decltype(&DirectX_CDirectDraw_DDCreateSurface),
          Original_DirectX_CDirectDraw_DDCreateSurface, DirectX_CDirectDraw_DDCreateSurface);

// Purpose: Creates a DirectDraw surface from a description and returns it
// as an IDirectDrawSurface3 if the driver has that interface (version 3),
// else as the plain IDirectDrawSurface (version 1).
std::uint32_t __fastcall DirectX_CDirectDraw_DDCreateSurface(void* self, void* /*edx*/, void* desc, void** out) {
    void* const directDraw = AsDirectDraw(self)->directDraw;
    // Out locals as the original's stack slots start (file comment).
    void* created = desc;
    void* surface = self;
    std::int32_t hr = Method<ComThreeFn>(directDraw, kDDCreateSurface)(directDraw, desc, &created, nullptr);
    ThrowIfDDFailed(hr, texts::kDDCreateSurface1.c_str());
    hr = Method<ComTwoFn>(created, kComQueryInterface)(created, &g_iidDirectDrawSurface3, &surface);
    std::uint32_t version;
    if (hr == kNoInterface) {
        ComCall(created, kComAddRef);
        version = 1;
        surface = created;
    } else {
        ThrowIfDDFailed(hr, texts::kDDCreateSurface3.c_str());
        version = 3;
    }
    *out = surface;
    ComCall(created, kComRelease);
    return version;
}

S3RE_HOOK(0x005bed10, DirectX_CDirectDraw_DDEnumDisplayModes, decltype(&DirectX_CDirectDraw_DDEnumDisplayModes),
          Original_DirectX_CDirectDraw_DDEnumDisplayModes, DirectX_CDirectDraw_DDEnumDisplayModes);

// Purpose: Lists the display modes of the device (the game collects them
// for the resolution check).
std::uint32_t __fastcall DirectX_CDirectDraw_DDEnumDisplayModes(void* self, void* /*edx*/, std::uint32_t flags,
                                                                void* desc, void* context, void* callback) {
    void* const directDraw = AsDirectDraw(self)->directDraw;
    const std::int32_t hr = Method<ComFourFn>(directDraw, kDDEnumDisplayModes)(directDraw, AsConst(flags), desc,
                                                                              context, callback);
    ThrowIfDDFailed(hr, texts::kDDEnumDisplayModes.c_str());
    return 0;
}

S3RE_HOOK(0x005bed50, DirectX_CDirectDraw_DDRestoreDisplayMode,
          decltype(&DirectX_CDirectDraw_DDRestoreDisplayMode), Original_DirectX_CDirectDraw_DDRestoreDisplayMode,
          DirectX_CDirectDraw_DDRestoreDisplayMode);

// Purpose: Restores the desktop's display mode (when the game leaves full
// screen).
std::uint32_t __fastcall DirectX_CDirectDraw_DDRestoreDisplayMode(void* self, void* /*edx*/) {
    void* const directDraw = AsDirectDraw(self)->directDraw;
    ThrowIfDDFailed(ComCall(directDraw, kDDRestoreDisplayMode), texts::kDDRestoreDisplayMode.c_str());
    return 0;
}

S3RE_HOOK(0x005bed70, DirectX_CDirectDraw_DDGetDisplayMode, decltype(&DirectX_CDirectDraw_DDGetDisplayMode),
          Original_DirectX_CDirectDraw_DDGetDisplayMode, DirectX_CDirectDraw_DDGetDisplayMode);

// Purpose: Reads the current display mode (size, pixel format, refresh
// rate) into a surface description.
std::uint32_t __fastcall DirectX_CDirectDraw_DDGetDisplayMode(void* self, void* /*edx*/, void* desc) {
    std::memset(desc, 0, kSurfaceDescBytes);
    static_cast<std::uint32_t*>(desc)[kDescSize] = kSurfaceDescBytes;
    void* const directDraw = AsDirectDraw(self)->directDraw;
    const std::int32_t hr = Method<ComPtrFn>(directDraw, kDDGetDisplayMode)(directDraw, desc);
    ThrowIfDDFailed(hr, texts::kDDGetDisplayMode.c_str());
    return 0;
}

S3RE_HOOK(0x005bedb0, DirectX_CDirectDraw_DDGetCaps, decltype(&DirectX_CDirectDraw_DDGetCaps),
          Original_DirectX_CDirectDraw_DDGetCaps, DirectX_CDirectDraw_DDGetCaps);

// Purpose: Reads the capabilities of the driver and of the emulation layer.
std::uint32_t __fastcall DirectX_CDirectDraw_DDGetCaps(void* self, void* /*edx*/, void* driverCaps, void* helCaps) {
    std::memset(driverCaps, 0, kCapsZeroBytes);
    std::memset(helCaps, 0, kCapsZeroBytes);
    static_cast<std::uint32_t*>(driverCaps)[0] = kCapsSize;
    static_cast<std::uint32_t*>(helCaps)[0] = kCapsSize;
    void* const directDraw = AsDirectDraw(self)->directDraw;
    const std::int32_t hr = Method<ComTwoFn>(directDraw, kDDGetCaps)(directDraw, driverCaps, helCaps);
    ThrowIfDDFailed(hr, texts::kDDGetCaps.c_str());
    return 0;
}

S3RE_HOOK(0x005bee00, DirectX_CDirectDraw_DDWaitForVerticalBlank,
          decltype(&DirectX_CDirectDraw_DDWaitForVerticalBlank), Original_DirectX_CDirectDraw_DDWaitForVerticalBlank,
          DirectX_CDirectDraw_DDWaitForVerticalBlank);

// Purpose: Waits for the monitor's vertical blank (before presenting a
// frame).
std::uint32_t __fastcall DirectX_CDirectDraw_DDWaitForVerticalBlank(void* self, void* /*edx*/, std::uint32_t flags,
                                                                    void* event) {
    void* const directDraw = AsDirectDraw(self)->directDraw;
    const std::int32_t hr = Method<ComTwoFn>(directDraw, kDDWaitForVerticalBlank)(directDraw, AsConst(flags), event);
    ThrowIfDDFailed(hr, texts::kDDWaitForVerticalBlank.c_str());
    return 0;
}

S3RE_HOOK(0x005bee30, DirectX_CDirectDraw_RestoreSurfaces, decltype(&DirectX_CDirectDraw_RestoreSurfaces),
          Original_DirectX_CDirectDraw_RestoreSurfaces, DirectX_CDirectDraw_RestoreSurfaces);

// Purpose: Called when a surface was lost (e.g. after Alt+Tab): restores
// every surface if the game is the active application, otherwise throws
// "not active" (the draw thread waits until it is activated again).
std::uint32_t __fastcall DirectX_CDirectDraw_RestoreSurfaces(void* self, void* /*edx*/) {
    void* const application = AsPointer(CallAt<ThisFn>(kGetApplication)(nullptr, nullptr));
    if ((CallAt<ThisFn>(kApplicationIsActive)(application, nullptr) & 0xff) == 0) {
        framework::ThrowAppNotActive();
    }
    return Method<ThisFn>(self, kSlotRestoreAllSurfaces)(self, nullptr);
}

// ---------------------------------------------------------------------------
// DirectX::CSimpleSurface / DirectX::CSurface
// ---------------------------------------------------------------------------

S3RE_HOOK(0x005bf010, DirectX_CSurface_DDLock, decltype(&DirectX_CSurface_DDLock), Original_DirectX_CSurface_DDLock,
          DirectX_CSurface_DDLock);

// Purpose: One attempt to lock the surface's memory for drawing.
std::int32_t __fastcall DirectX_CSurface_DDLock(void* self, void* /*edx*/, void* rect, void* desc,
                                                std::uint32_t flags, void* event) {
    void* const surface = AsSurface(self)->simple.surface;
    std::int32_t hr = Method<ComFourFn>(surface, kSurfLock)(surface, rect, desc, AsConst(flags), event);
    if (hr != kOk && hr != kWasStillDrawing) {
        hr = CheckResult(self, hr, texts::kDDLock.c_str());
    }
    return hr;
}

S3RE_HOOK(0x005bf050, DirectX_CSurface_Lock, decltype(&DirectX_CSurface_Lock), Original_DirectX_CSurface_Lock,
          DirectX_CSurface_Lock);

// Purpose: Locks the surface's memory (or a rectangle of it) so the game can
// draw into it directly, retrying until DirectDraw grants it.
std::uint32_t __fastcall DirectX_CSurface_Lock(void* self, void* /*edx*/, void* rect, void* desc,
                                               std::uint32_t flags) {
    std::memset(desc, 0, kSurfaceDescBytes);
    static_cast<std::uint32_t*>(desc)[kDescSize] = kSurfaceDescBytes;
    if (flags == 0) {
        flags = kLockWait;
    }
    switch (AsSurface(self)->simple.version) {
        case 1:
        case 2: flags &= kLockFlagsVersion1; break;
        case 3: flags &= kLockFlagsVersion3; break;
        default: break;
    }
    for (;;) {
        const std::int32_t hr = ViaGame(0x005bf010, DirectX_CSurface_DDLock)(self, nullptr, rect, desc, flags, nullptr);
        if (hr == kWasStillDrawing) {
            g_importSleep.get()(0);
            continue;
        }
        if (hr == kOk) {
            return 0;
        }
    }
}

S3RE_HOOK(0x005bf1a0, DirectX_CSimpleSurface_DDGetBltStatus, decltype(&DirectX_CSimpleSurface_DDGetBltStatus),
          Original_DirectX_CSimpleSurface_DDGetBltStatus, DirectX_CSimpleSurface_DDGetBltStatus);

// Purpose: Asks whether a blit to or from the surface can be started or is
// done (the surface-copy benchmark waits on it).
std::int32_t __fastcall DirectX_CSimpleSurface_DDGetBltStatus(void* self, void* /*edx*/, std::uint32_t flags) {
    void* const surface = AsSurface(self)->simple.surface;
    std::int32_t hr = Method<ComArgFn>(surface, kSurfGetBltStatus)(surface, flags);
    if (hr != kWasStillDrawing && hr != kOk && hr != kNoBltHardware) {
        hr = CheckResult(self, hr, texts::kDDGetBltStatus.c_str());
    }
    return hr;
}

S3RE_HOOK(0x005bf1e0, DirectX_CSimpleSurface_GetInterface, decltype(&DirectX_CSimpleSurface_GetInterface),
          Original_DirectX_CSimpleSurface_GetInterface, DirectX_CSimpleSurface_GetInterface);

// Purpose: Hands out the surface's DirectDraw interface with a reference of
// its own (the caller releases it).
void* __fastcall DirectX_CSimpleSurface_GetInterface(void* self, void* /*edx*/) {
    CSimpleSurface& simple = AsSurface(self)->simple;
    if (simple.createdInterface != nullptr) {
        ComCall(simple.createdInterface, kComAddRef);
    }
    return simple.createdInterface;
}

S3RE_HOOK(0x005bf2a0, DirectX_CSurface_UseOwnInterfaceForDrawing,
          decltype(&DirectX_CSurface_UseOwnInterfaceForDrawing), Original_DirectX_CSurface_UseOwnInterfaceForDrawing,
          DirectX_CSurface_UseOwnInterfaceForDrawing);

// Purpose: After creating a surface: draw into the surface itself (a flip
// chain draws into its back buffer instead).
std::uint32_t __fastcall DirectX_CSurface_UseOwnInterfaceForDrawing(void* self, void* /*edx*/) {
    CSimpleSurface& simple = AsSurface(self)->simple;
    simple.surface = simple.createdInterface;
    return Address(simple.createdInterface);
}

S3RE_HOOK(0x005bf2b0, DirectX_CSimpleSurface_SetDirectDraw, decltype(&DirectX_CSimpleSurface_SetDirectDraw),
          Original_DirectX_CSimpleSurface_SetDirectDraw, DirectX_CSimpleSurface_SetDirectDraw);

// Purpose: Sets the DirectDraw wrapper that creates and restores the
// surface.
std::uint32_t __fastcall DirectX_CSimpleSurface_SetDirectDraw(void* self, void* /*edx*/, void* owner) {
    AsSurface(self)->simple.owner = owner;
    return Address(owner);
}

S3RE_HOOK(0x005bf310, DirectX_CSurface_DDGetSurfaceDesc, decltype(&DirectX_CSurface_DDGetSurfaceDesc),
          Original_DirectX_CSurface_DDGetSurfaceDesc, DirectX_CSurface_DDGetSurfaceDesc);

// Purpose: Reads the surface's size after creating it and keeps it as the
// rectangle (0, 0, width, height).
std::uint32_t __fastcall DirectX_CSurface_DDGetSurfaceDesc(void* self, void* /*edx*/) {
    CSurface* const object = AsSurface(self);
    SurfaceDesc desc;
    desc.words[kDescSize] = kSurfaceDescBytes;
    void* surface = object->simple.surface;
    std::int32_t hr = Method<ComPtrFn>(surface, kSurfGetSurfaceDesc)(surface, desc.words);
    if (CheckResult(self, hr, texts::kDDGetSurfaceDesc.c_str()) == kSurfaceLost) {
        surface = object->simple.surface;  // re-read after the restore
        hr = Method<ComPtrFn>(surface, kSurfGetSurfaceDesc)(surface, desc.words);
        CheckResult(self, hr, texts::kDDGetSurfaceDesc.c_str());
    }
    return static_cast<std::uint32_t>(g_importSetRect.get()(object->rect, 0, 0,
                                                            static_cast<std::int32_t>(desc.words[kDescWidth]),
                                                            static_cast<std::int32_t>(desc.words[kDescHeight])));
}

S3RE_HOOK(0x005bf390, DirectX_CSurface_RestoreIfLost, decltype(&DirectX_CSurface_RestoreIfLost),
          Original_DirectX_CSurface_RestoreIfLost, DirectX_CSurface_RestoreIfLost);

// Purpose: Restores the surface if DirectDraw lost it (display mode change,
// Alt+Tab); 1 if it had to be restored (its contents are gone).
std::uint32_t __fastcall DirectX_CSurface_RestoreIfLost(void* self, void* /*edx*/) {
    void* const surface = AsSurface(self)->simple.createdInterface;
    const std::int32_t hr = ComCall(surface, kSurfIsLost);
    if (hr == kOk) {
        return 0;
    }
    if (hr != kSurfaceLost) {
        CheckResult(self, hr, texts::kDDIsLost.c_str());
    }
    ViaGame(0x005bf400, DirectX_CSurface_DDRestore)(self, nullptr);
    return 1;
}

S3RE_HOOK(0x005bf3d0, DirectX_CSurface_SetCaps, decltype(&DirectX_CSurface_SetCaps), Original_DirectX_CSurface_SetCaps,
          DirectX_CSurface_SetCaps);

// Purpose: Sets extra DirectDraw capability bits for the next Create (e.g.
// system memory for the window's back buffer).
std::uint32_t __fastcall DirectX_CSurface_SetCaps(void* self, void* /*edx*/, std::uint32_t caps) {
    AsSurface(self)->caps = caps;
    return caps;
}

S3RE_HOOK(0x005bf3e0, DirectX_CSurface_SetPixelFormat, decltype(&DirectX_CSurface_SetPixelFormat),
          Original_DirectX_CSurface_SetPixelFormat, DirectX_CSurface_SetPixelFormat);

// Purpose: Sets the pixel format for the next Create.
std::uint32_t __fastcall DirectX_CSurface_SetPixelFormat(void* self, void* /*edx*/, const void* format) {
    std::memcpy(AsSurface(self)->pixelFormat, format, kPixelFormatBytes);
    return 0;  // the original leaves EAX as the caller had it (both callers ignore it)
}

S3RE_HOOK(0x005bf400, DirectX_CSurface_DDRestore, decltype(&DirectX_CSurface_DDRestore),
          Original_DirectX_CSurface_DDRestore, DirectX_CSurface_DDRestore);

// Purpose: Restores a lost surface's memory; a display mode that isn't the
// game's (another program in full screen) counts as "not active".
std::int32_t __fastcall DirectX_CSurface_DDRestore(void* self, void* /*edx*/) {
    void* const surface = AsSurface(self)->simple.createdInterface;
    const std::int32_t hr = ComCall(surface, kSurfRestore);
    if (hr == kWrongMode || hr == kNoExclusiveMode) {
        using LogFn = void(__cdecl*)(const char* text);
        CallAt<LogFn>(kDebugLog)(texts::kDDWrongMode.c_str());
        framework::ThrowAppNotActive();
    }
    if (hr < 0) {
        // new CDirectDrawException(hr, "CSurface::DDRestore", 1), thrown: what
        // ThrowIfDDFailed does for a failure.
        ThrowIfDDFailed(hr, texts::kDDRestore.c_str());
    }
    return hr;
}

// ---------------------------------------------------------------------------
// DirectX::CFlipSurface
// ---------------------------------------------------------------------------

S3RE_HOOK(0x005bf5c0, DirectX_CFlipSurface_Construct, decltype(&DirectX_CFlipSurface_Construct),
          Original_DirectX_CFlipSurface_Construct, DirectX_CFlipSurface_Construct);

// Purpose: Constructor of the flip-chain part of a surface (the number of
// back buffers); the most derived object also builds the CSurface base.
void* __fastcall DirectX_CFlipSurface_Construct(void* self, void* /*edx*/, std::uint32_t backBuffers,
                                                std::uint32_t mostDerived) {
    auto* const flip = static_cast<CFlipSurface*>(self);
    if (mostDerived != 0) {
        flip->vbtable = vtables::kCFlipSurface_vbtable;
        CallAt<ThisFn>(kSurfaceConstruct)(&static_cast<framework::CFlipSurfaceObject*>(self)->base, nullptr);
    }
    flip->backBufferCount = backBuffers;
    flip->unk08 = nullptr;
    flip->backBuffer = nullptr;
    SetVirtualBaseVtable(self, vtables::kCFlipSurface_base);
    return self;
}

S3RE_HOOK(0x005bf640, DirectX_CFlipSurface_Destroy, decltype(&DirectX_CFlipSurface_Destroy),
          Original_DirectX_CFlipSurface_Destroy, DirectX_CFlipSurface_Destroy);

// Purpose: Destructor of the flip-chain part: deletes the back-buffer
// wrapper (and the object at +8). `base` is the CSurface part, the flip part
// sits 0x10 before it.
void __fastcall DirectX_CFlipSurface_Destroy(void* base, void* /*edx*/) {
    auto* const flip = reinterpret_cast<CFlipSurface*>(static_cast<std::uint8_t*>(base) - sizeof(CFlipSurface));
    SetVirtualBaseVtable(flip, vtables::kCFlipSurface_base);
    using DeleteFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t flags);
    if (void* const object = flip->unk08; object != nullptr) {
        Method<DeleteFn>(object, kSlotDelete)(object, nullptr, 1);
    }
    if (void* const backBuffer = flip->backBuffer; backBuffer != nullptr) {
        Method<DeleteFn>(backBuffer, kSlotDelete)(backBuffer, nullptr, 1);
    }
}

S3RE_HOOK(0x005bf610, DirectX_CFlipSurface_DeletingDestroy, decltype(&DirectX_CFlipSurface_DeletingDestroy),
          Original_DirectX_CFlipSurface_DeletingDestroy, DirectX_CFlipSurface_DeletingDestroy);

// Purpose: The compiler's deleting destructor of a flip surface (called with
// its CSurface part).
void* __fastcall DirectX_CFlipSurface_DeletingDestroy(void* base, void* /*edx*/, std::uint32_t flags) {
    void* const object = static_cast<std::uint8_t*>(base) - sizeof(CFlipSurface);
    ViaGame(0x005bf640, DirectX_CFlipSurface_Destroy)(base, nullptr);
    CallAt<ThisFn>(kSurfaceDestroy)(base, nullptr);
    if ((flags & 1) != 0) {
        msvc6::GameOperatorDelete(object);
    }
    return object;
}

S3RE_HOOK(0x005bf670, DirectX_CFlipSurface_GetBackBuffer, decltype(&DirectX_CFlipSurface_GetBackBuffer),
          Original_DirectX_CFlipSurface_GetBackBuffer, DirectX_CFlipSurface_GetBackBuffer);

// Purpose: The back buffer of a flip chain as a surface wrapper the game
// draws into (made on first use: same owner, the attached back buffer as
// both interfaces).
void* __fastcall DirectX_CFlipSurface_GetBackBuffer(void* self, void* /*edx*/) {
    auto* const flip = static_cast<CFlipSurface*>(self);
    if (flip->backBuffer == nullptr) {
        void* backBuffer = msvc6::GameOperatorNew(sizeof(CSimpleSurface));
        if (backBuffer != nullptr) {
            const CSimpleSurface& base = VirtualBase(self)->simple;
            using Construct4Fn = void*(__fastcall*)(void* self, void* edx, std::uint32_t owner, std::uint32_t created,
                                                   std::uint32_t surface, std::uint32_t version);
            backBuffer = CallAt<Construct4Fn>(kSimpleSurfaceConstruct4)(
                backBuffer, nullptr, Address(base.owner), Address(base.surface), Address(base.surface), base.version);
        }
        flip->backBuffer = backBuffer;
        if (backBuffer == nullptr) {
            using ThrowFn = void(__fastcall*)(const char* what, const char* file, std::uint32_t line);
            CallAt<ThrowFn>(kThrowOutOfMemory)(texts::kLpBackBuffer.c_str(), texts::kDdrawCppFile.c_str(),
                                               kLineBackBuffer);
        }
    }
    return flip->backBuffer;
}

S3RE_HOOK(0x005bf740, DirectX_CFlipSurface_DDFlip, decltype(&DirectX_CFlipSurface_DDFlip),
          Original_DirectX_CFlipSurface_DDFlip, DirectX_CFlipSurface_DDFlip);

// Purpose: One attempt to flip the chain (show the back buffer).
std::int32_t __fastcall DirectX_CFlipSurface_DDFlip(void* self, void* /*edx*/, void* target, std::uint32_t flags) {
    void* const front = VirtualBase(self)->simple.createdInterface;
    const std::int32_t hr = Method<ComTwoFn>(front, kSurfFlip)(front, target, AsConst(flags));
    if (hr != kOk && hr != kWasStillDrawing) {
        CheckResult(VirtualBase(self), hr, texts::kDDFlip.c_str());
    }
    return hr;
}

S3RE_HOOK(0x005bf710, DirectX_CFlipSurface_Flip, decltype(&DirectX_CFlipSurface_Flip),
          Original_DirectX_CFlipSurface_Flip, DirectX_CFlipSurface_Flip);

// Purpose: Presents the frame in full screen: flips until DirectDraw has
// done it.
std::int32_t __fastcall DirectX_CFlipSurface_Flip(void* self, void* /*edx*/, void* target) {
    for (;;) {
        const std::int32_t hr = ViaGame(0x005bf740, DirectX_CFlipSurface_DDFlip)(self, nullptr, target, 0);
        if (hr == kWasStillDrawing) {
            g_importSleep.get()(0);
            continue;
        }
        if (hr == kOk) {
            return hr;
        }
    }
}

// ---------------------------------------------------------------------------
// DirectX::CPrimarySurface / DirectX::CPrimaryFlipSurface
// ---------------------------------------------------------------------------

S3RE_HOOK(0x005bf790, DirectX_CPrimarySurface_Construct, decltype(&DirectX_CPrimarySurface_Construct),
          Original_DirectX_CPrimarySurface_Construct, DirectX_CPrimarySurface_Construct);

// Purpose: Constructor of the primary (visible screen) surface wrapper.
void* __fastcall DirectX_CPrimarySurface_Construct(void* self, void* /*edx*/, std::uint32_t mostDerived) {
    auto* const primary = static_cast<framework::CPrimarySurfaceObject*>(self);
    if (mostDerived != 0) {
        primary->vbtable = vtables::kCPrimarySurface_vbtable;
        CallAt<ThisFn>(kSurfaceConstruct)(&primary->base, nullptr);
    }
    SetVirtualBaseVtable(self, vtables::kCPrimarySurface_base);
    return self;
}

S3RE_HOOK(0x005bf7d0, DirectX_CPrimarySurface_DeletingDestroy, decltype(&DirectX_CPrimarySurface_DeletingDestroy),
          Original_DirectX_CPrimarySurface_DeletingDestroy, DirectX_CPrimarySurface_DeletingDestroy);

// Purpose: The compiler's deleting destructor of the primary surface
// (called with its CSurface part, 4 bytes in).
void* __fastcall DirectX_CPrimarySurface_DeletingDestroy(void* base, void* /*edx*/, std::uint32_t flags) {
    CallAt<ThisFn>(kSurfaceDestroy)(base, nullptr);
    void* const object = static_cast<std::uint8_t*>(base) - 4;
    if ((flags & 1) != 0) {
        msvc6::GameOperatorDelete(object);
    }
    return object;
}

S3RE_HOOK(0x005bf800, DirectX_CPrimarySurface_Create, decltype(&DirectX_CPrimarySurface_Create),
          Original_DirectX_CPrimarySurface_Create, DirectX_CPrimarySurface_Create);

// Purpose: Creates the primary surface (the screen) for windowed drawing.
std::uint32_t __fastcall DirectX_CPrimarySurface_Create(void* self, void* /*edx*/) {
    CSurface* const base = VirtualBase(self);
    SurfaceDesc desc;
    desc.words[kDescSize] = kSurfaceDescBytes;
    desc.words[kDescFlags] = kSdCaps;
    desc.words[kDescCaps] = base->caps | kCapsPrimarySurface;
    AddPixelFormat(base, desc, kSdCaps | kSdPixelFormat);
    CreateWithOwner(base, desc);
    ViaGame(0x005bf2a0, DirectX_CSurface_UseOwnInterfaceForDrawing)(base, nullptr);
    return ViaGame(0x005bf310, DirectX_CSurface_DDGetSurfaceDesc)(base, nullptr);
}

S3RE_HOOK(0x005bf890, DirectX_CPrimaryFlipSurface_Construct, decltype(&DirectX_CPrimaryFlipSurface_Construct),
          Original_DirectX_CPrimaryFlipSurface_Construct, DirectX_CPrimaryFlipSurface_Construct);

// Purpose: Constructor of the full-screen flip surface (primary surface with
// attached back buffers).
void* __fastcall DirectX_CPrimaryFlipSurface_Construct(void* self, void* /*edx*/, std::uint32_t backBuffers,
                                                       std::uint32_t mostDerived) {
    auto* const object = static_cast<framework::CPrimaryFlipSurfaceObject*>(self);
    if (mostDerived != 0) {
        object->vbtable = vtables::kCPrimaryFlipSurface_vbtable;
        object->flip.vbtable = vtables::kCPrimaryFlipSurface_flipVbtable;
        CallAt<ThisFn>(kSurfaceConstruct)(&object->base, nullptr);
    }
    using PrimaryFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t mostDerived);
    using FlipFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t backBuffers, std::uint32_t mostDerived);
    CallAt<PrimaryFn>(kPrimarySurfaceConstruct)(self, nullptr, 0);
    CallAt<FlipFn>(kFlipSurfaceConstruct)(&object->flip, nullptr, backBuffers, 0);
    SetVirtualBaseVtable(self, vtables::kCPrimaryFlipSurface_base);
    return self;
}

S3RE_HOOK(0x005bf960, DirectX_CPrimaryFlipSurface_Destroy, decltype(&DirectX_CPrimaryFlipSurface_Destroy),
          Original_DirectX_CPrimaryFlipSurface_Destroy, DirectX_CPrimaryFlipSurface_Destroy);

// Purpose: Destructor of the full-screen flip surface (called with its
// CSurface part): the flip part's destructor.
void __fastcall DirectX_CPrimaryFlipSurface_Destroy(void* base, void* /*edx*/) {
    // The original converts to the CFlipSurface part with a null check
    // (null when base - 0x14 is null) and back.
    auto* const bytes = static_cast<std::uint8_t*>(base);
    std::uint8_t* const flipPart = bytes - 0x14 != nullptr ? bytes - 0x10 : nullptr;
    ViaGame(0x005bf640, DirectX_CFlipSurface_Destroy)(flipPart + 0x10, nullptr);
}

S3RE_HOOK(0x005bf930, DirectX_CPrimaryFlipSurface_DeletingDestroy,
          decltype(&DirectX_CPrimaryFlipSurface_DeletingDestroy), Original_DirectX_CPrimaryFlipSurface_DeletingDestroy,
          DirectX_CPrimaryFlipSurface_DeletingDestroy);

// Purpose: The compiler's deleting destructor of the full-screen flip
// surface (called with its CSurface part, 0x14 bytes in).
void* __fastcall DirectX_CPrimaryFlipSurface_DeletingDestroy(void* base, void* /*edx*/, std::uint32_t flags) {
    void* const object = static_cast<std::uint8_t*>(base) - offsetof(framework::CPrimaryFlipSurfaceObject, base);
    ViaGame(0x005bf960, DirectX_CPrimaryFlipSurface_Destroy)(base, nullptr);
    CallAt<ThisFn>(kSurfaceDestroy)(base, nullptr);
    if ((flags & 1) != 0) {
        msvc6::GameOperatorDelete(object);
    }
    return object;
}

S3RE_HOOK(0x005bf9b0, DirectX_CPrimaryFlipSurface_Create, decltype(&DirectX_CPrimaryFlipSurface_Create),
          Original_DirectX_CPrimaryFlipSurface_Create, DirectX_CPrimaryFlipSurface_Create);

// Purpose: Creates the full-screen flip chain: the primary surface with one
// back buffer, and gets the back buffer as the surface to draw into.
std::uint32_t __fastcall DirectX_CPrimaryFlipSurface_Create(void* self, void* /*edx*/) {
    auto* const object = static_cast<framework::CPrimaryFlipSurfaceObject*>(self);
    CSurface* const base = VirtualBase(self);
    SurfaceDesc desc;
    desc.words[kDescSize] = kSurfaceDescBytes;
    desc.words[kDescFlags] = kSdCaps | kSdBackBufferCount;
    desc.words[kDescBackBufferCount] = 1;
    desc.words[kDescCaps] = base->caps | kCapsPrimarySurface | kCapsFlip | kCapsComplex;
    AddPixelFormat(base, desc, kSdCaps | kSdBackBufferCount | kSdPixelFormat);
    CreateWithOwner(base, desc);
    std::uint32_t attachedCaps;
    switch (object->flip.backBufferCount) {
        case 1: attachedCaps = kCapsBackBuffer; break;
        case 2: attachedCaps = 0; break;
        default: {
            using AssertFn = void(__fastcall*)(const char* text, const char* file, std::uint32_t line);
            CallAt<AssertFn>(kThrowAssertmentFailed)(texts::kNoMoreThanTripleBuffering.c_str(),
                                                    texts::kDdrawCppFile.c_str(), kLineTripleBuffering);
            return ViaGame(0x005bf310, DirectX_CSurface_DDGetSurfaceDesc)(VirtualBase(self), nullptr);
        }
    }
    CSurface* const target = VirtualBase(self);
    void* const front = target->simple.createdInterface;
    const std::int32_t hr =
        Method<ComTwoFn>(front, kSurfGetAttachedSurface)(front, &attachedCaps, &target->simple.surface);
    CheckResult(target, hr, texts::kDDGetAttachedSurface.c_str());
    return ViaGame(0x005bf310, DirectX_CSurface_DDGetSurfaceDesc)(VirtualBase(self), nullptr);
}

// ---------------------------------------------------------------------------
// DirectX::COffscreenSurface / DirectX::COverlaySurface / DirectX::CPalette
// ---------------------------------------------------------------------------

S3RE_HOOK(0x005bfab0, DirectX_COffscreenSurface_Construct, decltype(&DirectX_COffscreenSurface_Construct),
          Original_DirectX_COffscreenSurface_Construct, DirectX_COffscreenSurface_Construct);

// Purpose: Constructor of an off-screen surface wrapper (back buffers,
// the landscape and static images are drawn into such surfaces).
void* __fastcall DirectX_COffscreenSurface_Construct(void* self, void* /*edx*/) {
    CallAt<ThisFn>(kSurfaceConstruct)(self, nullptr);
    auto* const surface = static_cast<COffscreenSurface*>(self);
    surface->contentsValid = 0;
    surface->surface.simple.vtable = vtables::kCOffscreenSurface;
    return self;
}

S3RE_HOOK(0x005bfad0, DirectX_COffscreenSurface_DeletingDestroy,
          decltype(&DirectX_COffscreenSurface_DeletingDestroy), Original_DirectX_COffscreenSurface_DeletingDestroy,
          DirectX_COffscreenSurface_DeletingDestroy);

// Purpose: The compiler's deleting destructor of the off-screen and overlay
// surfaces (one folded function; it destroys the CSurface).
void* __fastcall DirectX_COffscreenSurface_DeletingDestroy(void* self, void* /*edx*/, std::uint32_t flags) {
    CallAt<ThisFn>(kSurfaceDestroyThunk)(self, nullptr);
    if ((flags & 1) != 0) {
        msvc6::GameOperatorDelete(self);
    }
    return self;
}

namespace {

// COffscreenSurface::Create and COverlaySurface::Create: a surface of the
// given size with these caps.
std::uint32_t CreateSized(CSurface* surface, std::uint32_t width, std::uint32_t height, std::uint32_t caps) {
    SurfaceDesc desc;
    desc.words[kDescCaps] = surface->caps | caps;
    desc.words[kDescSize] = kSurfaceDescBytes;
    desc.words[kDescFlags] = kSdCaps | kSdSize;
    desc.words[kDescWidth] = width;
    desc.words[kDescHeight] = height;
    AddPixelFormat(surface, desc, kSdCaps | kSdSize | kSdPixelFormat);
    CreateWithOwner(surface, desc);
    ViaGame(0x005bf2a0, DirectX_CSurface_UseOwnInterfaceForDrawing)(surface, nullptr);
    return ViaGame(0x005bf310, DirectX_CSurface_DDGetSurfaceDesc)(surface, nullptr);
}

}  // namespace

S3RE_HOOK(0x005bfaf0, DirectX_COffscreenSurface_Create, decltype(&DirectX_COffscreenSurface_Create),
          Original_DirectX_COffscreenSurface_Create, DirectX_COffscreenSurface_Create);

// Purpose: Creates the off-screen surface with the given size.
std::uint32_t __fastcall DirectX_COffscreenSurface_Create(void* self, void* /*edx*/, std::uint32_t width,
                                                          std::uint32_t height) {
    const std::uint32_t result = CreateSized(AsSurface(self), width, height, kCapsOffscreenPlain);
    static_cast<COffscreenSurface*>(self)->contentsValid = 1;
    return result;
}

S3RE_HOOK(0x005bfb80, DirectX_COffscreenSurface_RestoreIfLost, decltype(&DirectX_COffscreenSurface_RestoreIfLost),
          Original_DirectX_COffscreenSurface_RestoreIfLost, DirectX_COffscreenSurface_RestoreIfLost);

// Purpose: Restores a lost off-screen surface and marks its contents as
// gone, so the game redraws them.
std::uint32_t __fastcall DirectX_COffscreenSurface_RestoreIfLost(void* self, void* /*edx*/) {
    const std::uint32_t restored = CallAt<ThisFn>(kSurfaceRestoreIfLost)(self, nullptr);
    if (restored == 1) {
        static_cast<COffscreenSurface*>(self)->contentsValid = 0;
    }
    return restored;
}

S3RE_HOOK(0x005bfba0, DirectX_COverlaySurface_Construct, decltype(&DirectX_COverlaySurface_Construct),
          Original_DirectX_COverlaySurface_Construct, DirectX_COverlaySurface_Construct);

// Purpose: Constructor of an overlay surface wrapper (the hardware mouse
// cursor).
void* __fastcall DirectX_COverlaySurface_Construct(void* self, void* /*edx*/) {
    CallAt<ThisFn>(kSurfaceConstruct)(self, nullptr);
    AsSurface(self)->simple.vtable = vtables::kCOverlaySurface;
    return self;
}

S3RE_HOOK(0x005bfbc0, DirectX_COverlaySurface_Create, decltype(&DirectX_COverlaySurface_Create),
          Original_DirectX_COverlaySurface_Create, DirectX_COverlaySurface_Create);

// Purpose: Creates the overlay surface (in video memory) with the given
// size.
std::uint32_t __fastcall DirectX_COverlaySurface_Create(void* self, void* /*edx*/, std::uint32_t width,
                                                        std::uint32_t height) {
    return CreateSized(AsSurface(self), width, height, kCapsOverlay | kCapsVideoMemory);
}

S3RE_HOOK(0x005bfc70, DirectX_COverlaySurface_DDUpdateOverlay, decltype(&DirectX_COverlaySurface_DDUpdateOverlay),
          Original_DirectX_COverlaySurface_DDUpdateOverlay, DirectX_COverlaySurface_DDUpdateOverlay);

// Purpose: Shows, moves or hides the overlay on a destination surface (the
// screen); retried once after a lost surface.
std::uint32_t __fastcall DirectX_COverlaySurface_DDUpdateOverlay(void* self, void* /*edx*/, void* sourceRect,
                                                                 void* destination, void* destinationRect,
                                                                 std::uint32_t flags, void* fx) {
    const auto update = [&](void* destinationInterface) {
        void* const overlay = AsSurface(self)->simple.createdInterface;
        const std::int32_t hr = Method<ComFiveFn>(overlay, kSurfUpdateOverlay)(
            overlay, sourceRect, destinationInterface, destinationRect, AsConst(flags), fx);
        return CheckResult(self, hr, texts::kDDUpdateOverlay.c_str());
    };
    if (destination == nullptr) {
        std::int32_t hr = update(nullptr);
        if (hr == kSurfaceLost) {
            hr = update(nullptr);
        }
        return static_cast<std::uint32_t>(hr);
    }
    void* const destinationInterface = ViaGame(0x005bf1e0, DirectX_CSimpleSurface_GetInterface)(destination, nullptr);
    if (update(destinationInterface) == kSurfaceLost) {
        update(destinationInterface);
    }
    return static_cast<std::uint32_t>(ComCall(destinationInterface, kComRelease));
}

S3RE_HOOK(0x005bfc50, DirectX_COverlaySurface_Hide, decltype(&DirectX_COverlaySurface_Hide),
          Original_DirectX_COverlaySurface_Hide, DirectX_COverlaySurface_Hide);

// Purpose: Hides the overlay (the hardware mouse cursor).
std::uint32_t __fastcall DirectX_COverlaySurface_Hide(void* self, void* /*edx*/, void* destination) {
    return ViaGame(0x005bfc70, DirectX_COverlaySurface_DDUpdateOverlay)(self, nullptr, nullptr, destination, nullptr,
                                                                        kOverlayHide, nullptr);
}

S3RE_HOOK(0x005bfd60, DirectX_CPalette_Release, decltype(&DirectX_CPalette_Release), Original_DirectX_CPalette_Release,
          DirectX_CPalette_Release);

// Purpose: Releases a palette's DirectDraw palette object (when the
// surfaces are released).
std::uint32_t __fastcall DirectX_CPalette_Release(void* self, void* /*edx*/) {
    auto* const palette = static_cast<CPalette*>(self);
    std::uint32_t eax = 0;
    if (palette->palette != nullptr) {
        eax = static_cast<std::uint32_t>(ComCall(palette->palette, kComRelease));
        palette->palette = nullptr;
    }
    return eax;
}

}  // namespace s3re::game
