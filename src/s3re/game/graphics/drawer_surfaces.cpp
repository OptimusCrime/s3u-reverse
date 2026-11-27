// The in-game drawer's surfaces and its mouse cursor (Dirk\Draw.cpp;
// re/ui_classes.md, "CDrawer").
//
// CDrawer::SetMouseCursor (0x0042cb60): (id) thiscall, ret 4. Nothing
//   happens when `id` is the current cursor (+0x5f6dc; EAX = it). Else it
//   becomes the current one, and:
//   - software cursor (+0x5f6d5 set): the cursor animation 0x20014 (GFX
//     group 2, list 0x14; CGraphicsData::AnimationPointer 0x5faaf0 into the
//     argument's stack slot) gives frame `id` (frame 0 when id is negative
//     or not below the list's count, a byte at +7); the frame's drawing
//     offset (two int16 at +8) negated is the hotspot of the mouse surface
//     +0x5f6d8 (CMouseSurface::SetHotspot 0x469130 (-x, -y)); the surface
//     is cleared (CMouseSurface::Clear 0x469310) and locked
//     (CSurface::Lock 0x5bf050 (0, &desc, 0)); the frame is drawn with
//     GfxDword::DrawSprite 0x5fadb0 (desc, the negated offset
//     (Point16::Negate 0x5fdad0), clip (0, 0, 32, 32), brightness 255,
//     fraction 0x10000) at (0, 0), the offset cancelling the one DrawSprite
//     adds; then IDirectDrawSurface::Unlock(lpSurface) (COM slot 0x80 of the
//     surface's +8) checked by CSurface::CheckResult(hr,
//     "CSurface::DDUnlock") 0x5bf2e0, both once more when that returns
//     DDERR_SURFACELOST. EAX = the last CheckResult's. Negate and DrawSprite
//     are inside `try { } catch (...) { unlock as above; throw; }`.
//   - Windows cursor: the resource id of `id` from the table 0x656a58
//     (pairs (cursor, resource), 9 of them; when `id` is none of them, the
//     dword after the table is read: 0x42c310, the start of the next
//     vtable; kept) is posted to the game window (+0xc) as message 0x802
//     (wParam the resource, lParam 0). EAX = PostMessageA's.
// CDrawer::RestoreSurfaces (0x0042cb30): vtable slot 4, thiscall ():
//   DirectX::CGameDrawer::RestoreSurfaces 0x5b72e0, then the current cursor
//   (if not -1) is applied again (+0x5f6dc set to -1 first, so
//   SetMouseCursor does not skip it). EAX: the cursor id when it is -1,
//   else SetMouseCursor's.
// CDrawer::ReleaseSurfaces (0x0042caa0): vtable slot 5, (keepGraphics)
//   thiscall, ret 4: posts message 0x803 (0, 0) to the game window; deletes
//   the text surface +0x5f6e0 (slot 0 with 1, if any) and clears it;
//   DirectX::CGameDrawer::ReleaseSurfaces 0x5b7170 (keepGraphics); forgets
//   the four surfaces +0x5f6c0..+0x5f6cc and the mouse surface. Without
//   keepGraphics (its low byte 0) it waits for the graphics data lock
//   (g_graphicsDataLock 0x7a800c, slot 0 Wait(5000, 1)), closes the
//   graphics data (CGraphicsData::Close 0x5fa690 on +0x5f6b8) and clears
//   g_gfxData. EAX: with keepGraphics, the base's EAX with its low byte
//   replaced by keepGraphics' low byte; without, Close's.
// CDrawer::CreateSurfaces (0x0042ca90): vtable slot 11, (flag) thiscall,
//   ret 4: calls slot 12 (InitSurfaces) with the flag. EAX = slot 12's.
// CDrawer::SetConfiguredDisplayMode (0x0042ca70): vtable slot 10,
//   thiscall (): SetDisplayMode(g_displayWidth, g_displayHeight,
//   g_displayBitsPerPixel, 0, 0) (0x42ce90). EAX = SetDisplayMode's.
//
// Known difference: SetMouseCursor's catch (...) is an SEH filter that
// unlocks and lets the exception pass (as CSoundData::GetEffect's in
// sound_effects.cpp): the unlock runs before the frames of Negate and
// DrawSprite are unwound instead of after (neither has anything to
// unwind), and an exception thrown by that unlock (CheckResult's
// ThrowIfDDFailed) starts inside the filter.

#include "game/globals.h"
#include "game/graphics/drawer_layout.h"
#include "game/tables.h"
#include "game/texts.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/fake_win32.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kAnimationPointer = 0x005faaf0;    // CGraphicsData::AnimationPointer(out, id) ret 8
constexpr std::uintptr_t kSetHotspot = 0x00469130;          // Dirk::CMouseSurface::SetHotspot(x, y) ret 8
constexpr std::uintptr_t kClearMouseSurface = 0x00469310;   // Dirk::CMouseSurface::Clear() ret
constexpr std::uintptr_t kSurfaceLock = 0x005bf050;         // DirectX::CSurface::Lock(rect, desc, flags) ret 0Ch
constexpr std::uintptr_t kNegate = 0x005fdad0;              // Point16::Negate(out) ret 4
constexpr std::uintptr_t kDrawSprite = 0x005fadb0;          // GfxDword::DrawSprite(desc, pos, clip, brightness, fraction) ret 14h
constexpr std::uintptr_t kCheckResult = 0x005bf2e0;         // DirectX::CSurface::CheckResult(hr, where) ret 8
constexpr std::uintptr_t kGameDrawerRestoreSurfaces = 0x005b72e0;  // DirectX::CGameDrawer::RestoreSurfaces() ret
constexpr std::uintptr_t kGameDrawerReleaseSurfaces = 0x005b7170;  // DirectX::CGameDrawer::ReleaseSurfaces(keep) ret 4
constexpr std::uintptr_t kGraphicsClose = 0x005fa690;       // CGraphicsData::Close() ret
constexpr std::uintptr_t kSetMouseCursor = 0x0042cb60;
constexpr std::uintptr_t kSetDisplayMode = 0x0042ce90;      // CDrawer::SetDisplayMode(w, h, bpp, refresh, flags) ret 14h

constexpr std::uint32_t kCursorAnimation = 0x20014;   // GFX group 2, animation list 0x14: the cursor images
constexpr std::uint32_t kSurfaceLost = 0x887601c2;    // DDERR_SURFACELOST
constexpr std::uint32_t kUnlockSlot = 0x80 / 4;       // IDirectDrawSurface::Unlock
constexpr std::int32_t kCursorSize = 32;              // the clip rectangle of the cursor image
constexpr std::uint32_t kFullBrightness = 255;
constexpr std::uint32_t kWholeSprite = 0x10000;       // DrawSprite's visible fraction (16.16)
constexpr std::int32_t kNoCursor = -1;
constexpr std::uint32_t kMessageSetCursor = 0x802;    // to the game window: wParam = cursor resource
constexpr std::uint32_t kMessageSurfacesReleased = 0x803;
constexpr std::uint32_t kGraphicsLockTimeout = 5000;  // ms, g_graphicsDataLock's Wait
constexpr std::uint32_t kSlotCreateSurfacesImpl = 12; // CDrawer::InitSurfaces(flag)

#pragma pack(push, 1)
// A list of images (an "animation" of CGraphicsData::animationTables):
// the frame count, then per frame the offset of its image from the list.
struct ImageList {
    std::uint8_t unk00[7];
    std::uint8_t count;        // +0x07 (SetMouseCursor reads this byte only)
    std::uint32_t offsets[1];  // +0x08 variable length
};
// The start of an image: its drawing offset at +8 (DrawSprite adds it to
// the position).
struct ImageHeader {
    std::uint8_t unk00[8];
    std::uint32_t offset;      // +0x08 Point16 (x low word, y high word)
};
struct SurfaceDesc {           // DDSURFACEDESC
    std::uint8_t unk00[0x24];
    void* surface;             // +0x24 lpSurface
    std::uint8_t unk28[0x6c - 0x28];
};
struct ClipRect {
    std::int32_t left, top, right, bottom;
};
// The fields of Dirk::CMouseSurface (a DirectX::CSurface) used here.
struct MouseSurfaceView {
    std::uint8_t unk00[0x08];
    void* interface;           // +0x08 the IDirectDrawSurface
};
#pragma pack(pop)
static_assert(offsetof(ImageList, count) == 7);
static_assert(offsetof(ImageList, offsets) == 8);
static_assert(offsetof(ImageHeader, offset) == 8);
static_assert(offsetof(SurfaceDesc, surface) == 0x24);
static_assert(sizeof(SurfaceDesc) == 0x6c);
static_assert(offsetof(MouseSurfaceView, interface) == 8);

using AnimationPointerFn = std::uint32_t*(__fastcall*)(void* graphics, void* edx, std::uint32_t* out, std::uint32_t id);
using SetHotspotFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::int32_t x, std::int32_t y);
using MemberFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using Member1Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t argument);
using SurfaceLockFn = std::uint32_t(__fastcall*)(void* self, void* edx, void* rect, SurfaceDesc* desc,
                                                 std::uint32_t flags);
using NegateFn = std::uint32_t*(__fastcall*)(const std::uint32_t* point, void* edx, std::uint32_t* out);
using DrawSpriteFn = void(__fastcall*)(const std::uint32_t* image, void* edx, const SurfaceDesc* desc,
                                       const std::uint32_t* pos, const ClipRect* clip, std::uint32_t brightness,
                                       std::uint32_t fraction);
using CheckResultFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t hr, const char* where);
using UnlockFn = std::uint32_t(__stdcall*)(void* self, void* rect);
using WaitFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t timeout, std::uint32_t flag);
using SetDisplayModeFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t width, std::uint32_t height,
                                                    std::uint32_t bpp, std::uint32_t refresh, std::uint32_t flags);

// IDirectDrawSurface::Unlock of the surface's pixels, checked as
// "CSurface::DDUnlock"; repeated once when the surface was lost.
std::uint32_t UnlockChecked(void* mouseSurface, void* pixels) {
    auto* const surface = static_cast<MouseSurfaceView*>(mouseSurface);
    const auto check = reinterpret_cast<CheckResultFn>(kCheckResult);
    auto unlock = [&] {
        void* const object = surface->interface;
        return (*static_cast<UnlockFn* const*>(object))[kUnlockSlot](object, pixels);
    };
    std::uint32_t result = check(surface, nullptr, unlock(), texts::kDDUnlock);
    if (result == kSurfaceLost) {
        result = check(surface, nullptr, unlock(), texts::kDDUnlock);
    }
    return result;
}

// catch (...) { unlock; throw; }: unlocks and lets every exception pass.
int UnlockOnException(void* mouseSurface, void* pixels) {
    UnlockChecked(mouseSurface, pixels);
    return EXCEPTION_CONTINUE_SEARCH;
}

// 0x42cc23..0x42cc65, the try block: the image drawn at the negated offset.
void DrawCursorImage(void* mouseSurface, const std::uint32_t* image, const SurfaceDesc* desc) {
    const ClipRect clip{0, 0, kCursorSize, kCursorSize};
    const std::uint32_t offset = reinterpret_cast<const ImageHeader*>(*image)->offset;
    std::uint32_t negated = 0;
    __try {
        const std::uint32_t* position = reinterpret_cast<NegateFn>(kNegate)(&offset, nullptr, &negated);
        reinterpret_cast<DrawSpriteFn>(kDrawSprite)(image, nullptr, desc, position, &clip, kFullBrightness,
                                                    kWholeSprite);
    } __except (UnlockOnException(mouseSurface, desc->surface)) {
    }
}

// 0x42cbb3..0x42ccbb: the cursor image drawn into the mouse surface.
std::uint32_t DrawSoftwareCursor(CDrawer* self, std::int32_t id) {
    std::uint32_t slot = static_cast<std::uint32_t>(id);  // (the original passes the argument's stack slot)
    const auto* const list = reinterpret_cast<const ImageList*>(static_cast<std::uintptr_t>(
        *reinterpret_cast<AnimationPointerFn>(kAnimationPointer)(self->graphics, nullptr, &slot, kCursorAnimation)));
    const std::uint32_t frameOffset = (id >= 0 && id < list->count) ? list->offsets[id] : list->offsets[0];
    const std::uint32_t image = reinterpret_cast<std::uint32_t>(list) + frameOffset;  // a GfxDword
    const std::uint32_t offset = reinterpret_cast<const ImageHeader*>(image)->offset;
    const auto x = static_cast<std::int16_t>(offset & 0xffff);
    const auto y = static_cast<std::int16_t>(offset >> 16);
    reinterpret_cast<SetHotspotFn>(kSetHotspot)(self->mouse_surface, nullptr, -x, -y);
    reinterpret_cast<MemberFn>(kClearMouseSurface)(self->mouse_surface, nullptr);
    SurfaceDesc desc;
    reinterpret_cast<SurfaceLockFn>(kSurfaceLock)(self->mouse_surface, nullptr, nullptr, &desc, 0);
    DrawCursorImage(self->mouse_surface, &image, &desc);
    return UnlockChecked(self->mouse_surface, desc.surface);
}

// 0x42cd20..0x42cd48: the Windows cursor of `id`, set by the window thread.
std::uint32_t PostWindowsCursor(CDrawer* self, std::int32_t id) {
    const CursorResource* const table = g_cursorResources.get();
    std::size_t index = 0;
    while (index < kCursorResourceCount && table[index].cursor != id) {
        ++index;
    }
    // (index 9 when not found: the dword after the table is read; kept)
    return static_cast<std::uint32_t>(
        g_importPostMessageA.get()(self->window, kMessageSetCursor, table[index].resource, 0));
}

}  // namespace

std::uint32_t __fastcall CDrawer_SetMouseCursor(CDrawer* self, void* edx, std::int32_t id);
std::uint32_t __fastcall CDrawer_RestoreSurfaces(CDrawer* self, void* edx);
std::uint32_t __fastcall CDrawer_ReleaseSurfaces(CDrawer* self, void* edx, std::uint32_t keepGraphics);
std::uint32_t __fastcall CDrawer_CreateSurfaces(CDrawer* self, void* edx, std::uint32_t flag);
std::uint32_t __fastcall CDrawer_SetConfiguredDisplayMode(CDrawer* self, void* edx);

using SetMouseCursorFn = decltype(&CDrawer_SetMouseCursor);
using RestoreSurfacesFn = decltype(&CDrawer_RestoreSurfaces);
using ReleaseSurfacesFn = decltype(&CDrawer_ReleaseSurfaces);
using CreateSurfacesFn = decltype(&CDrawer_CreateSurfaces);
using SetConfiguredDisplayModeFn = decltype(&CDrawer_SetConfiguredDisplayMode);

S3RE_HOOK(0x0042cb60, CDrawer_SetMouseCursor, SetMouseCursorFn, Original_CDrawer_SetMouseCursor,
          CDrawer_SetMouseCursor);
S3RE_HOOK(0x0042cb30, CDrawer_RestoreSurfaces, RestoreSurfacesFn, Original_CDrawer_RestoreSurfaces,
          CDrawer_RestoreSurfaces);
S3RE_HOOK(0x0042caa0, CDrawer_ReleaseSurfaces, ReleaseSurfacesFn, Original_CDrawer_ReleaseSurfaces,
          CDrawer_ReleaseSurfaces);
S3RE_HOOK(0x0042ca90, CDrawer_CreateSurfaces, CreateSurfacesFn, Original_CDrawer_CreateSurfaces,
          CDrawer_CreateSurfaces);
S3RE_HOOK(0x0042ca70, CDrawer_SetConfiguredDisplayMode, SetConfiguredDisplayModeFn,
          Original_CDrawer_SetConfiguredDisplayMode, CDrawer_SetConfiguredDisplayMode);

// Purpose: changes the mouse cursor (pointer, wait, building placement,
// ...): drawn into the software cursor's surface, or as the Windows cursor
// the game window's thread sets.
std::uint32_t __fastcall CDrawer_SetMouseCursor(CDrawer* self, void* /*edx*/, std::int32_t id) {
    if (self->cursor_id == id) {
        return static_cast<std::uint32_t>(id);
    }
    self->cursor_id = id;
    if (self->software_cursor != 0) {
        return DrawSoftwareCursor(self, id);
    }
    return PostWindowsCursor(self, id);
}

// Purpose: after the surfaces were lost (e.g. the game was switched away
// from), restores them and draws the current cursor again.
std::uint32_t __fastcall CDrawer_RestoreSurfaces(CDrawer* self, void* /*edx*/) {
    reinterpret_cast<MemberFn>(kGameDrawerRestoreSurfaces)(self, nullptr);
    const std::int32_t cursor = self->cursor_id;
    if (cursor == kNoCursor) {
        return static_cast<std::uint32_t>(cursor);
    }
    self->cursor_id = kNoCursor;
    return reinterpret_cast<Member1Fn>(kSetMouseCursor)(self, nullptr, static_cast<std::uint32_t>(cursor));
}

// Purpose: releases the drawing surfaces (before a display mode change or
// at the end), and with them the graphics data unless they are kept.
std::uint32_t __fastcall CDrawer_ReleaseSurfaces(CDrawer* self, void* /*edx*/, std::uint32_t keepGraphics) {
    g_importPostMessageA.get()(self->window, kMessageSurfacesReleased, 0, 0);
    if (self->text_surface != nullptr) {
        void* const object = self->text_surface;
        reinterpret_cast<Member1Fn>((*static_cast<void* const* const*>(object))[0])(object, nullptr, 1);
    }
    self->text_surface = nullptr;
    std::uint32_t result = reinterpret_cast<Member1Fn>(kGameDrawerReleaseSurfaces)(self, nullptr, keepGraphics);
    self->surface_5f6cc = nullptr;
    self->draw_surface = nullptr;
    self->back_surface = nullptr;
    self->primary_surface = nullptr;
    self->mouse_surface = nullptr;
    const auto keep = static_cast<std::uint8_t>(keepGraphics);
    if (keep != 0) {
        return (result & 0xffffff00u) | keep;
    }
    void* const lock = g_graphicsDataLock.get();
    reinterpret_cast<WaitFn>((*static_cast<void* const* const*>(lock))[0])(lock, nullptr, kGraphicsLockTimeout, 1);
    result = reinterpret_cast<MemberFn>(kGraphicsClose)(self->graphics, nullptr);
    g_gfxData = nullptr;
    return result;
}

// Purpose: creates the drawing surfaces (slot 11 forwards to CDrawer's
// InitSurfaces).
std::uint32_t __fastcall CDrawer_CreateSurfaces(CDrawer* self, void* /*edx*/, std::uint32_t flag) {
    return reinterpret_cast<Member1Fn>(self->vtable[kSlotCreateSurfacesImpl])(self, nullptr, flag);
}

// Purpose: switches to the display mode of the game's settings.
std::uint32_t __fastcall CDrawer_SetConfiguredDisplayMode(CDrawer* self, void* /*edx*/) {
    return reinterpret_cast<SetDisplayModeFn>(kSetDisplayMode)(
        self, nullptr, static_cast<std::uint32_t>(g_displayWidth.get()),
        static_cast<std::uint32_t>(g_displayHeight.get()), static_cast<std::uint32_t>(g_displayBitsPerPixel.get()),
        0, 0);
}

// ---------------------------------------------------------------------------
// Tests: a drawer image (random bytes, with the fields these functions read
// chosen per case); the callees are recording stubs (their stack-address
// arguments left out, what they point to recorded instead); the mouse
// surface's IDirectDrawSurface a fake whose Unlock returns DD_OK,
// DDERR_SURFACELOST or another error; PostMessageA's import a recording
// fake. DrawSprite's stub throws a game exception (Win32::ThrowOutOfMemory)
// in some cases: compared are the calls made before it passes through
// (catch (...): the unlock) and the exception itself.
// ---------------------------------------------------------------------------

namespace {

constexpr std::uintptr_t kThrowOutOfMemory = 0x00610960;  // Win32::ThrowOutOfMemory(name, file) + line

struct Script {
    test::Rng rng{0};
    std::vector<std::uint32_t> log;
    std::uint32_t base = 0;  // the fixture stub arguments are recorded relative to
    bool throwInDraw = false;
};
Script* g_script = nullptr;

std::uint32_t Rel(std::uint32_t address) { return address - g_script->base; }

std::uint32_t ScriptedResult() {
    const std::uint32_t r = g_script->rng.Below(4);
    return r == 0 ? kSurfaceLost : r == 1 ? 0x88760005u : 0;
}

std::uint32_t __stdcall FakeUnlock(void*, void* pixels) {
    g_script->log.insert(g_script->log.end(), {0x80u, Rel(reinterpret_cast<std::uint32_t>(pixels))});
    return ScriptedResult();
}

int __stdcall FakePostMessage(void* window, std::uint32_t message, std::uint32_t wParam, std::uint32_t lParam) {
    g_script->log.insert(g_script->log.end(),
                         {0x9057u, reinterpret_cast<std::uint32_t>(window), message, wParam, lParam});
    return static_cast<int>(g_script->rng.Next32());
}

std::uint32_t __fastcall FakeDeletingDestructor(void* self, void*, std::uint32_t flags) {
    g_script->log.insert(g_script->log.end(), {0xde1u, Rel(reinterpret_cast<std::uint32_t>(self)), flags});
    return g_script->rng.Next32();
}

std::uint32_t __fastcall FakeWait(void* self, void*, std::uint32_t timeout, std::uint32_t flag) {
    g_script->log.insert(g_script->log.end(), {0xa17u, Rel(reinterpret_cast<std::uint32_t>(self)), timeout, flag});
    return g_script->rng.Next32();
}

std::uint32_t __fastcall FakeSlot12(void*, void*, std::uint32_t flag) {
    g_script->log.insert(g_script->log.end(), {0x12u, flag});
    return g_script->rng.Next32();
}

// The test's objects next to the drawer: the cursor image list, a pixel
// buffer, the mouse surface and the fake objects' vtable pointers.
struct Fixture {
    std::uint8_t images[0x400];
    std::uint8_t pixels[0x40];
    MouseSurfaceView mouse;
    std::uint8_t mouseRest[0x100];
    const void* interface;    // the fake IDirectDrawSurface (its vtable pointer)
    const void* textSurface;  // an object with FakeDeletingDestructor in slot 0
    const void* lock;         // an object with FakeWait in slot 0
};

struct Case {
    std::vector<std::uint8_t> drawer;
    Fixture fixture;
    std::uint64_t seed;
    std::int32_t id;
    std::uint32_t argument;
    bool throwInDraw;
    std::int32_t display[3];
};

struct Outcome {
    std::uint32_t eax = 0;
    std::vector<std::uint8_t> drawer;
    Fixture fixture;
    std::vector<std::uint32_t> log;
    std::vector<test::StubCall> calls;
    test::CaughtException caught;
    std::uint32_t gfxData = 0;
};

enum class Target { SetMouseCursor, RestoreSurfaces, ReleaseSurfaces, CreateSurfaces, SetConfiguredDisplayMode };

struct Functions {
    SetMouseCursorFn setMouseCursor;
    RestoreSurfacesFn restoreSurfaces;
    ReleaseSurfacesFn releaseSurfaces;
    CreateSurfacesFn createSurfaces;
    SetConfiguredDisplayModeFn setConfiguredDisplayMode;
};

struct Guarded {
    const Functions* f;
    Target target;
    CDrawer* drawer;
    std::int32_t id;
    std::uint32_t argument;
};

std::uint32_t CallTarget(const Guarded* g) {
    switch (g->target) {
        case Target::SetMouseCursor: return g->f->setMouseCursor(g->drawer, nullptr, g->id);
        case Target::RestoreSurfaces: return g->f->restoreSurfaces(g->drawer, nullptr);
        case Target::ReleaseSurfaces: return g->f->releaseSurfaces(g->drawer, nullptr, g->argument);
        case Target::CreateSurfaces: return g->f->createSurfaces(g->drawer, nullptr, g->argument);
        case Target::SetConfiguredDisplayMode: return g->f->setConfiguredDisplayMode(g->drawer, nullptr);
    }
    return 0;
}

void GuardedCall(const Guarded* g, std::uint32_t* eax, test::CaughtException* caught) {
    __try {
        *eax = CallTarget(g);
    } __except (test::CatchGameException(GetExceptionInformation(), caught)) {
    }
}

std::uint32_t Word(std::uint32_t address) { return *reinterpret_cast<const std::uint32_t*>(address); }

Outcome Run(const Case& c, Target target, const Functions& f) {
    Outcome out;
    Script script;
    script.rng = test::Rng(c.seed);
    script.throwInDraw = c.throwInDraw;
    g_script = &script;
    out.drawer = c.drawer;
    out.fixture = c.fixture;
    CDrawer* const drawer = reinterpret_cast<CDrawer*>(out.drawer.data());
    Fixture& fx = out.fixture;
    script.base = reinterpret_cast<std::uint32_t>(&fx);

    static const void* unlockVtable[kUnlockSlot + 1] = {};
    unlockVtable[kUnlockSlot] = reinterpret_cast<const void*>(&FakeUnlock);
    static const void* deleteVtable[1] = {reinterpret_cast<const void*>(&FakeDeletingDestructor)};
    static const void* waitVtable[1] = {reinterpret_cast<const void*>(&FakeWait)};
    static void* drawerVtable[16] = {};
    drawerVtable[kSlotCreateSurfacesImpl] = reinterpret_cast<void*>(&FakeSlot12);
    fx.interface = unlockVtable;
    fx.mouse.interface = &fx.interface;
    fx.textSurface = deleteVtable;
    fx.lock = waitVtable;
    drawer->vtable = drawerVtable;
    // The drawer's pointers that the case leaves non-null point into the fixture.
    if (drawer->mouse_surface != nullptr) {
        drawer->mouse_surface = &fx.mouse;
    }
    if (drawer->text_surface != nullptr) {
        drawer->text_surface = &fx.textSurface;
    }

    void* const savedLock = g_graphicsDataLock;
    gfx::CGraphicsData* const savedGfx = g_gfxData;
    const std::int32_t savedDisplay[3] = {g_displayWidth, g_displayHeight, g_displayBitsPerPixel};
    g_graphicsDataLock = static_cast<void*>(&fx.lock);
    g_gfxData = reinterpret_cast<gfx::CGraphicsData*>(0x5a5a5a5a);
    g_displayWidth = c.display[0];
    g_displayHeight = c.display[1];
    g_displayBitsPerPixel = c.display[2];
    const void* fake = reinterpret_cast<const void*>(&FakePostMessage);
    test::PatchBytes(g_importPostMessageA.address(), &fake, sizeof(fake));
    {
        test::CalleeStubs stubs;
        stubs.SetWorld(reinterpret_cast<CGameData*>(&fx));
        stubs.Install<2, kAnimationPointer>([&](CGameData*, std::uint32_t, const std::uint32_t* a) {
            *reinterpret_cast<std::uint32_t*>(a[0]) = reinterpret_cast<std::uint32_t>(fx.images);
            return a[0];
        });
        stubs.Install<2, kSetHotspot>(
            [&](CGameData*, std::uint32_t, const std::uint32_t*) { return script.rng.Next32(); });
        stubs.Install<0, kClearMouseSurface>(
            [&](CGameData*, std::uint32_t, const std::uint32_t*) { return script.rng.Next32(); });
        stubs.Install<3, kSurfaceLock>([&](CGameData*, std::uint32_t, const std::uint32_t* a) {
            auto* desc = reinterpret_cast<SurfaceDesc*>(a[1]);
            script.rng.Fill(desc, sizeof(SurfaceDesc));
            desc->surface = fx.pixels + script.rng.Below(16);
            return script.rng.Next32();
        });
        stubs.Install<1, kNegate>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
            const std::uint32_t point = Word(self + script.base);
            script.log.insert(script.log.end(), {0xe9u, point});
            const auto x = static_cast<std::uint16_t>(0 - (point & 0xffff));
            const auto y = static_cast<std::uint16_t>(0 - (point >> 16));
            *reinterpret_cast<std::uint32_t*>(a[0]) = static_cast<std::uint32_t>(y) << 16 | x;
            return a[0];
        });
        stubs.Install<5, kDrawSprite>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
            const auto* desc = reinterpret_cast<const SurfaceDesc*>(a[0]);
            const auto* clip = reinterpret_cast<const ClipRect*>(a[2]);
            script.log.insert(script.log.end(),
                              {0xd5u, Rel(Word(self + script.base)), Rel(reinterpret_cast<std::uint32_t>(desc->surface)),
                               Word(a[1]), static_cast<std::uint32_t>(clip->left), static_cast<std::uint32_t>(clip->top),
                               static_cast<std::uint32_t>(clip->right), static_cast<std::uint32_t>(clip->bottom)});
            if (script.throwInDraw) {
                using ThrowFn = void(__fastcall*)(const char*, const char*, std::uint32_t);
                reinterpret_cast<ThrowFn>(kThrowOutOfMemory)("cursor", "drawer_surfaces.cpp", 7);
            }
            return 0u;
        });
        stubs.Install<2, kCheckResult>([&](CGameData*, std::uint32_t, const std::uint32_t* a) {
            return script.rng.Chance(7, 8) ? a[0] : script.rng.Next32();
        });
        stubs.Install<0, kGameDrawerRestoreSurfaces>(
            [&](CGameData*, std::uint32_t, const std::uint32_t*) { return script.rng.Next32(); });
        stubs.Install<1, kGameDrawerReleaseSurfaces>(
            [&](CGameData*, std::uint32_t, const std::uint32_t*) { return script.rng.Next32(); });
        stubs.Install<0, kGraphicsClose>(
            [&](CGameData*, std::uint32_t, const std::uint32_t*) { return script.rng.Next32(); });
        if (target == Target::RestoreSurfaces) {
            stubs.Install<1, kSetMouseCursor>(
                [&](CGameData*, std::uint32_t, const std::uint32_t*) { return script.rng.Next32(); });
        }
        stubs.Install<5, kSetDisplayMode>(
            [&](CGameData*, std::uint32_t, const std::uint32_t*) { return script.rng.Next32(); });
        const Guarded g{&f, target, drawer, c.id, c.argument};
        GuardedCall(&g, &out.eax, &out.caught);
        out.calls = stubs.Take();
    }
    test::UnpatchBytes(g_importPostMessageA.address());
    const std::uint32_t drawerRel = reinterpret_cast<std::uint32_t>(drawer) - script.base;
    for (test::StubCall& call : out.calls) {
        if (call.self == drawerRel) {
            call.self = 0xd0;  // (the drawer copy of each run)
        }
        if (call.callee == kAnimationPointer || call.callee == kGraphicsClose) {
            call.self += script.base;  // (the drawer's graphics pointer: random, not in the fixture)
        }
        if (call.callee == kAnimationPointer) {
            call.args[0] = 0;  // (a stack slot)
        }
        if (call.callee == kSurfaceLock) {
            call.args[1] = 0;
        }
        if (call.callee == kNegate) {
            call.self = 0;
            call.args[0] = 0;
        }
        if (call.callee == kDrawSprite) {
            call.self = 0;
            call.args[0] = call.args[1] = call.args[2] = 0;
        }
    }
    out.gfxData = reinterpret_cast<std::uint32_t>(g_gfxData.get());
    g_graphicsDataLock = savedLock;
    g_gfxData = savedGfx;
    g_displayWidth = savedDisplay[0];
    g_displayHeight = savedDisplay[1];
    g_displayBitsPerPixel = savedDisplay[2];
    // Pointers back to fixture-independent values, so the drawers compare.
    if (drawer->mouse_surface == &fx.mouse) {
        drawer->mouse_surface = reinterpret_cast<void*>(1);
    }
    if (drawer->text_surface == &fx.textSurface) {
        drawer->text_surface = reinterpret_cast<void*>(1);
    }
    drawer->vtable = nullptr;
    fx.interface = fx.textSurface = fx.lock = nullptr;
    fx.mouse.interface = nullptr;
    out.log = script.log;
    g_script = nullptr;
    return out;
}

// A cursor image list: count 0..12 (sometimes any byte), each frame offset
// to an image somewhere in the buffer whose drawing offset (+8) is random.
void MakeImages(test::Rng& rng, Fixture& fx) {
    rng.Fill(fx.images, sizeof(fx.images));
    auto* list = reinterpret_cast<ImageList*>(fx.images);
    list->count = static_cast<std::uint8_t>(rng.Chance(1, 8) ? rng.Below(256) : rng.Below(13));
    for (std::uint32_t i = 0; i < 64; ++i) {
        list->offsets[i] = 0x108 + rng.Below((sizeof(fx.images) - 0x108 - 16) / 4) * 4;
    }
}

Case MakeCase(test::Rng& rng, const std::vector<std::uint8_t>& drawerBytes) {
    Case c;
    c.drawer = drawerBytes;
    rng.Fill(&c.fixture, sizeof(c.fixture));
    MakeImages(rng, c.fixture);
    CDrawer* d = reinterpret_cast<CDrawer*>(c.drawer.data());
    auto pickId = [&]() -> std::int32_t {
        return rng.Chance(1, 8) ? static_cast<std::int32_t>(rng.Next32()) : rng.Range(-2, 12);
    };
    d->cursor_id = rng.Chance(1, 6) ? kNoCursor : pickId();
    c.id = rng.Chance(1, 6) ? d->cursor_id : pickId();
    d->software_cursor = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : (rng.Chance(1, 4) ? rng.Below(256) : 1));
    d->mouse_surface = reinterpret_cast<void*>(1);  // (pointed at the fixture's by Run)
    d->text_surface = rng.Chance(1, 3) ? nullptr : reinterpret_cast<void*>(1);
    c.seed = rng.Next64();
    c.argument = rng.Chance(1, 2) ? rng.Below(2) : rng.Next32();
    if (rng.Chance(1, 4)) {
        c.argument &= 0xffffff00u;  // (keepGraphics is tested by its low byte only)
    }
    c.throwInDraw = rng.Chance(1, 5);
    for (std::int32_t& value : c.display) {
        value = static_cast<std::int32_t>(rng.Next32());
    }
    return c;
}

bool Compare(test::Context& ctx, Target target, const Functions& f, int cases) {
    std::vector<std::uint8_t> drawerBytes(sizeof(CDrawer));
    ctx.rng().Fill(drawerBytes.data(), drawerBytes.size());
    static const Functions kOriginals{Original_CDrawer_SetMouseCursor, Original_CDrawer_RestoreSurfaces,
                                      Original_CDrawer_ReleaseSurfaces, Original_CDrawer_CreateSurfaces,
                                      Original_CDrawer_SetConfiguredDisplayMode};
    for (int i = 0; i < cases; ++i) {
        const Case c = MakeCase(ctx.rng(), drawerBytes);
        const Outcome expected = Run(c, target, kOriginals);
        const Outcome actual = Run(c, target, f);
        bool same = S3RE_EXPECT_EQ(expected.eax, actual.eax);
        same &= S3RE_EXPECT_MEM_EQ(expected.drawer.data(), actual.drawer.data(), sizeof(CDrawer));
        same &= S3RE_EXPECT_MEM_EQ(&expected.fixture, &actual.fixture, sizeof(Fixture));
        same &= S3RE_EXPECT(expected.log == actual.log);
        same &= S3RE_EXPECT(expected.calls == actual.calls);
        same &= S3RE_EXPECT(expected.caught == actual.caught);
        same &= S3RE_EXPECT_EQ(expected.gfxData, actual.gfxData);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (target %d, id %d)", i, static_cast<int>(target), c.id);
            return false;
        }
    }
    return true;
}

const Functions kOurs{CDrawer_SetMouseCursor, CDrawer_RestoreSurfaces, CDrawer_ReleaseSurfaces,
                      CDrawer_CreateSurfaces, CDrawer_SetConfiguredDisplayMode};

// Wrong: only an odd software-cursor byte counts as set.
std::uint32_t __fastcall WrongSetMouseCursor(CDrawer* self, void* edx, std::int32_t id) {
    const std::uint8_t flag = self->software_cursor;
    if ((flag & 1) == 0) {
        self->software_cursor = 0;
    }
    const std::uint32_t result = CDrawer_SetMouseCursor(self, edx, id);
    self->software_cursor = flag;
    return result;
}

// Wrong: the mouse surface is kept.
std::uint32_t __fastcall WrongReleaseSurfaces(CDrawer* self, void* edx, std::uint32_t keepGraphics) {
    void* const mouse = self->mouse_surface;
    const std::uint32_t result = CDrawer_ReleaseSurfaces(self, edx, keepGraphics);
    self->mouse_surface = mouse;
    return result;
}

// Wrong: the cursor is re-applied without forgetting it first.
std::uint32_t __fastcall WrongRestoreSurfaces(CDrawer* self, void* edx) {
    const std::int32_t cursor = self->cursor_id;
    const std::uint32_t result = CDrawer_RestoreSurfaces(self, edx);
    if (cursor != kNoCursor) {
        self->cursor_id = cursor;
    }
    return result;
}

}  // namespace

S3RE_TEST(CDrawer_SetMouseCursor_MatchesOriginal) { Compare(ctx, Target::SetMouseCursor, kOurs, 1500); }
S3RE_TEST(CDrawer_SetMouseCursor_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Functions wrong = kOurs;
    wrong.setMouseCursor = &WrongSetMouseCursor;
    Compare(probe, Target::SetMouseCursor, wrong, 1500);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CDrawer_RestoreSurfaces_MatchesOriginal) { Compare(ctx, Target::RestoreSurfaces, kOurs, 300); }
S3RE_TEST(CDrawer_RestoreSurfaces_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Functions wrong = kOurs;
    wrong.restoreSurfaces = &WrongRestoreSurfaces;
    Compare(probe, Target::RestoreSurfaces, wrong, 300);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CDrawer_ReleaseSurfaces_MatchesOriginal) { Compare(ctx, Target::ReleaseSurfaces, kOurs, 500); }
S3RE_TEST(CDrawer_ReleaseSurfaces_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Functions wrong = kOurs;
    wrong.releaseSurfaces = &WrongReleaseSurfaces;
    Compare(probe, Target::ReleaseSurfaces, wrong, 300);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CDrawer_CreateSurfaces_MatchesOriginal) { Compare(ctx, Target::CreateSurfaces, kOurs, 200); }
S3RE_TEST(CDrawer_SetConfiguredDisplayMode_MatchesOriginal) {
    Compare(ctx, Target::SetConfiguredDisplayMode, kOurs, 200);
}

}  // namespace s3re::game
