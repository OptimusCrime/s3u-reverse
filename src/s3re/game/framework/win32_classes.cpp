// Constructors and destructors of the small Win32/DirectX framework base
// classes (Win32\Classes.cpp and neighbours, RTTI names). They only store
// vtables and zero or copy plain fields; all are thiscall (ECX = the object).
//
// Win32::CObject (vtable 0x65d67c, 4 bytes): the root of the framework
// classes (threads, handles, windows, exceptions, CGameData...).
//   CObject()              0x00610320  vtable; EAX = this. ret.
//   ~CObject()             0x00610330  vtable (EAX unchanged). ret. Ghidra's
//                                      function also takes in the unrelated
//                                      code at 0x610340.
//   scalar deleting dtor   0x0060bfc0  (flags) ~CObject, operator delete if
//                                      flags bit 0; EAX = this. ret 4.
// Win32::CWin32Handle (vtable 0x65d654): +4 handle, +8 a second dword
//   (probably "close on destruction").
//   CWin32Handle(a, b)     0x0060be60  CObject(); +4 = a, +8 = b; EAX = this.
//                                      ret 8.
// Win32::CWaitable (vtable 0x65d3c8 during construction, then 0x65d660)
//   with a CWin32Handle at +4 (its vtable then set to 0x65d670) and +0x10:
//   CWaitable(a, b)        0x0060bf50  +0 = 0x65d3c8, CWin32Handle(+4; a, b),
//                                      +4 = 0x65d670, +0x10 = 0, +0 =
//                                      0x65d660; EAX = this. ret 8.
// Win32::CWindowBase (vtable 0x65d854): +4 the window handle.
//   CWindowBase()          0x0060f960  CObject(); +4 = 0; EAX = this. ret.
// Win32::CMessageQueue (vtable 0x65d6f4): +4 the HWND filter its message
//   loop passes to GetMessageA (0 = all windows; re/win32_framework.md).
//   CMessageQueue(a)       0x0060d690  CObject(); +4 = a; EAX = this. ret 4.
// DirectX::CSimpleSurface (vtable 0x65cda0): +4..+0x10.
//   CSimpleSurface()       0x005befb0  CObject(); +4, +8, +0xc = 0 (+0x10 is
//                                      left alone); EAX = this. ret.
//   CSimpleSurface(a,b,c,d) 0x005befe0 CObject(); +0xc = a, +4 = b, +8 = c,
//                                      +0x10 = d; EAX = this. ret 0x10.
// DirectX::CSurface (vtable 0x65cda8), derived from CSimpleSurface:
//   CSurface()             0x005bf200  CSimpleSurface(); +0x14, +0x48, +0x18 =
//                                      0; EAX = this. ret.
// DirectX::CDirectSoundBuffer (vtable 0x65cecc):
//   CDirectSoundBuffer()   0x005c1f70  +4, +8 = 0, vtable (no CObject);
//                                      EAX = this. ret.

#include "game/framework/framework_layout.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

using framework::CDirectSoundBuffer;
using framework::CMessageQueue;
using framework::CObject;
using framework::CSimpleSurface;
using framework::CSurface;
using framework::CWaitable;
using framework::CWin32Handle;
using framework::CWindowBase;

template <typename T>
T* As(void* self) {
    return static_cast<T*>(self);
}

void* AsPointer(std::uint32_t value) {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(value));
}

}  // namespace

using CtorFn = void*(__fastcall*)(void* self, void* edx);
using DtorFn = void(__fastcall*)(void* self, void* edx);
using DeletingDtorFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t flags);
using Ctor1Fn = void*(__fastcall*)(void* self, void* edx, std::uint32_t a);
using Ctor2Fn = void*(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b);
using Ctor4Fn = void*(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                   std::uint32_t d);

void* __fastcall Win32_CObject_Construct(void* self, void* edx);
void __fastcall Win32_CObject_Destroy(void* self, void* edx);
void* __fastcall Win32_CObject_DeletingDestroy(void* self, void* edx, std::uint32_t flags);
void* __fastcall Win32_CWin32Handle_Construct(void* self, void* edx, std::uint32_t handle, std::uint32_t b);
void* __fastcall Win32_CWaitable_Construct(void* self, void* edx, std::uint32_t handle, std::uint32_t b);
void* __fastcall Win32_CWindowBase_Construct(void* self, void* edx);
void* __fastcall Win32_CMessageQueue_Construct(void* self, void* edx, std::uint32_t window);
void* __fastcall DirectX_CSimpleSurface_Construct(void* self, void* edx);
void* __fastcall DirectX_CSimpleSurface_Construct4(void* self, void* edx, std::uint32_t a, std::uint32_t b,
                                                   std::uint32_t c, std::uint32_t d);
void* __fastcall DirectX_CDirectSoundBuffer_Construct(void* self, void* edx);
void* __fastcall DirectX_CSurface_Construct(void* self, void* edx);

S3RE_HOOK(0x00610320, Win32_CObject_Construct, CtorFn, Original_Win32_CObject_Construct,
          Win32_CObject_Construct);

// Purpose: Constructor of the root class of the game's Win32 framework
// (threads, handles, windows, exceptions and the world object derive from it).
void* __fastcall Win32_CObject_Construct(void* self, void* /*edx*/) {
    As<CObject>(self)->vtable = vtables::kCObject;
    return self;
}

S3RE_HOOK(0x00610330, Win32_CObject_Destroy, DtorFn, Original_Win32_CObject_Destroy, Win32_CObject_Destroy);

// Purpose: Destructor of the framework root class; the last step of every
// framework object's destruction.
void __fastcall Win32_CObject_Destroy(void* self, void* /*edx*/) {
    As<CObject>(self)->vtable = vtables::kCObject;
}

S3RE_HOOK(0x0060bfc0, Win32_CObject_DeletingDestroy, DeletingDtorFn, Original_Win32_CObject_DeletingDestroy,
          Win32_CObject_DeletingDestroy);

// Purpose: The compiler-generated "delete this object" entry of the root
// class's vtable: destroys the object and frees it if asked to.
void* __fastcall Win32_CObject_DeletingDestroy(void* self, void* /*edx*/, std::uint32_t flags) {
    Win32_CObject_Destroy(self, nullptr);
    if ((flags & 1) != 0) {
        msvc6::GameOperatorDelete(self);
    }
    return self;
}

S3RE_HOOK(0x0060be60, Win32_CWin32Handle_Construct, Ctor2Fn, Original_Win32_CWin32Handle_Construct,
          Win32_CWin32Handle_Construct);

// Purpose: Constructor of the wrapper around a Windows handle (events,
// threads, files).
void* __fastcall Win32_CWin32Handle_Construct(void* self, void* /*edx*/, std::uint32_t handle, std::uint32_t b) {
    Win32_CObject_Construct(self, nullptr);
    CWin32Handle* const object = As<CWin32Handle>(self);
    object->handle = handle;
    object->owned = b;
    object->vtable = vtables::kCWin32Handle;
    return self;
}

S3RE_HOOK(0x0060bf50, Win32_CWaitable_Construct, Ctor2Fn, Original_Win32_CWaitable_Construct,
          Win32_CWaitable_Construct);

// Purpose: Constructor of a waitable object (something a thread can wait
// on, built around a Windows handle).
void* __fastcall Win32_CWaitable_Construct(void* self, void* /*edx*/, std::uint32_t handle, std::uint32_t b) {
    CWaitable* const object = As<CWaitable>(self);
    object->vtable = vtables::kCWaitable;
    Win32_CWin32Handle_Construct(&object->handle, nullptr, handle, b);
    object->handle.vtable = vtables::kCWin32Waitable_sub4;
    object->unk10 = 0;
    object->vtable = vtables::kCWin32Waitable;
    return self;
}

S3RE_HOOK(0x0060f960, Win32_CWindowBase_Construct, CtorFn, Original_Win32_CWindowBase_Construct,
          Win32_CWindowBase_Construct);

// Purpose: Constructor of the base class of the game's windows (no window
// handle yet).
void* __fastcall Win32_CWindowBase_Construct(void* self, void* /*edx*/) {
    Win32_CObject_Construct(self, nullptr);
    As<CWindowBase>(self)->window = 0;
    As<CWindowBase>(self)->vtable = vtables::kCWindowBase;
    return self;
}

S3RE_HOOK(0x0060d690, Win32_CMessageQueue_Construct, Ctor1Fn, Original_Win32_CMessageQueue_Construct,
          Win32_CMessageQueue_Construct);

// Purpose: Constructor of a thread's Windows message queue wrapper.
void* __fastcall Win32_CMessageQueue_Construct(void* self, void* /*edx*/, std::uint32_t window) {
    Win32_CObject_Construct(self, nullptr);
    As<CMessageQueue>(self)->vtable = vtables::kCMessageQueue;
    As<CMessageQueue>(self)->window = window;
    return self;
}

S3RE_HOOK(0x005befb0, DirectX_CSimpleSurface_Construct, CtorFn, Original_DirectX_CSimpleSurface_Construct,
          DirectX_CSimpleSurface_Construct);

// Purpose: Default constructor of a DirectDraw surface wrapper (no surface
// yet).
void* __fastcall DirectX_CSimpleSurface_Construct(void* self, void* /*edx*/) {
    Win32_CObject_Construct(self, nullptr);
    CSimpleSurface* const object = As<CSimpleSurface>(self);
    object->vtable = vtables::kCSimpleSurface;
    object->createdInterface = nullptr;
    object->surface = nullptr;
    object->owner = nullptr;
    return self;
}

S3RE_HOOK(0x005befe0, DirectX_CSimpleSurface_Construct4, Ctor4Fn, Original_DirectX_CSimpleSurface_Construct4,
          DirectX_CSimpleSurface_Construct4);

// Purpose: Constructor of a DirectDraw surface wrapper from its four fields
// (probably the surface interface and its size).
void* __fastcall DirectX_CSimpleSurface_Construct4(void* self, void* /*edx*/, std::uint32_t a, std::uint32_t b,
                                                   std::uint32_t c, std::uint32_t d) {
    Win32_CObject_Construct(self, nullptr);
    CSimpleSurface* const object = As<CSimpleSurface>(self);
    object->createdInterface = AsPointer(b);
    object->surface = AsPointer(c);
    object->unk10 = d;
    object->owner = AsPointer(a);
    object->vtable = vtables::kCSimpleSurface;
    return self;
}

S3RE_HOOK(0x005c1f70, DirectX_CDirectSoundBuffer_Construct, CtorFn,
          Original_DirectX_CDirectSoundBuffer_Construct, DirectX_CDirectSoundBuffer_Construct);

// Purpose: Constructor of a DirectSound buffer wrapper (no buffer yet).
void* __fastcall DirectX_CDirectSoundBuffer_Construct(void* self, void* /*edx*/) {
    CDirectSoundBuffer* const object = As<CDirectSoundBuffer>(self);
    object->buffer = nullptr;
    object->createFlags = 0;
    object->vtable = vtables::kCDirectSoundBuffer;
    return self;
}

S3RE_HOOK(0x005bf200, DirectX_CSurface_Construct, CtorFn, Original_DirectX_CSurface_Construct,
          DirectX_CSurface_Construct);

// Purpose: Default constructor of DirectX::CSurface, a CSimpleSurface with
// more state (+0x14, +0x18 and +0x48 start at 0; meaning unknown).
void* __fastcall DirectX_CSurface_Construct(void* self, void* /*edx*/) {
    DirectX_CSimpleSurface_Construct(self, nullptr);
    CSurface* const object = As<CSurface>(self);
    object->simple.vtable = vtables::kCSurface;
    object->unk14 = 0;
    object->unk48 = 0;
    object->unk18 = 0;
    return self;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

struct Win32Functions {
    CtorFn objectCtor;
    DtorFn objectDtor;
    DeletingDtorFn objectDeletingDtor;
    Ctor2Fn handleCtor;
    Ctor2Fn waitableCtor;
    CtorFn windowBaseCtor;
    Ctor1Fn messageQueueCtor;
    CtorFn simpleSurfaceCtor;
    Ctor4Fn simpleSurfaceCtor4;
    CtorFn soundBufferCtor;
    CtorFn surfaceCtor;
};

Win32Functions OriginalWin32() {
    return {Original_Win32_CObject_Construct,      Original_Win32_CObject_Destroy,
            Original_Win32_CObject_DeletingDestroy, Original_Win32_CWin32Handle_Construct,
            Original_Win32_CWaitable_Construct,     Original_Win32_CWindowBase_Construct,
            Original_Win32_CMessageQueue_Construct, Original_DirectX_CSimpleSurface_Construct,
            Original_DirectX_CSimpleSurface_Construct4, Original_DirectX_CDirectSoundBuffer_Construct,
            Original_DirectX_CSurface_Construct};
}

Win32Functions OurWin32() {
    return {&Win32_CObject_Construct,      &Win32_CObject_Destroy,
            &Win32_CObject_DeletingDestroy, &Win32_CWin32Handle_Construct,
            &Win32_CWaitable_Construct,     &Win32_CWindowBase_Construct,
            &Win32_CMessageQueue_Construct, &DirectX_CSimpleSurface_Construct,
            &DirectX_CSimpleSurface_Construct4, &DirectX_CDirectSoundBuffer_Construct,
            &DirectX_CSurface_Construct};
}

// Runs operation `op` of `f` on `object` (with a guard area around it);
// returns EAX as an offset from the object (or the value for void ones).
std::uintptr_t RunWin32(const Win32Functions& f, int op, std::uint8_t* object, const std::uint32_t* args) {
    switch (op) {
        case 0: return reinterpret_cast<std::uintptr_t>(f.objectCtor(object, nullptr)) - reinterpret_cast<std::uintptr_t>(object);
        case 1: f.objectDtor(object, nullptr); return 0;
        case 2: return reinterpret_cast<std::uintptr_t>(f.objectDeletingDtor(object, nullptr, args[0] & ~1u)) - reinterpret_cast<std::uintptr_t>(object);
        case 3: return reinterpret_cast<std::uintptr_t>(f.handleCtor(object, nullptr, args[0], args[1])) - reinterpret_cast<std::uintptr_t>(object);
        case 4: return reinterpret_cast<std::uintptr_t>(f.waitableCtor(object, nullptr, args[0], args[1])) - reinterpret_cast<std::uintptr_t>(object);
        case 5: return reinterpret_cast<std::uintptr_t>(f.windowBaseCtor(object, nullptr)) - reinterpret_cast<std::uintptr_t>(object);
        case 6: return reinterpret_cast<std::uintptr_t>(f.messageQueueCtor(object, nullptr, args[0])) - reinterpret_cast<std::uintptr_t>(object);
        case 7: return reinterpret_cast<std::uintptr_t>(f.simpleSurfaceCtor(object, nullptr)) - reinterpret_cast<std::uintptr_t>(object);
        case 8: return reinterpret_cast<std::uintptr_t>(f.simpleSurfaceCtor4(object, nullptr, args[0], args[1], args[2], args[3])) - reinterpret_cast<std::uintptr_t>(object);
        case 9: return reinterpret_cast<std::uintptr_t>(f.soundBufferCtor(object, nullptr)) - reinterpret_cast<std::uintptr_t>(object);
        default: return reinterpret_cast<std::uintptr_t>(f.surfaceCtor(object, nullptr)) - reinterpret_cast<std::uintptr_t>(object);
    }
}

void CompareWin32(test::Context& ctx, const Win32Functions& candidate, int cases) {
    const Win32Functions original = OriginalWin32();
    constexpr std::size_t kGuard = 16;
    constexpr std::size_t kObject = 0x50;
    std::vector<std::uint8_t> a(kGuard * 2 + kObject), b(a.size());
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(11));
        std::uint32_t args[4];
        for (auto& arg : args) {
            arg = ctx.rng().Next32();
        }
        ctx.rng().Fill(a.data(), a.size());
        b = a;
        const std::uintptr_t ra = RunWin32(original, op, a.data() + kGuard, args);
        const std::uintptr_t rb = RunWin32(candidate, op, b.data() + kGuard, args);
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size());
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
            return;
        }
    }
    // The deleting destructor with the delete flag: both free a fresh block
    // from the game's heap and return it.
    for (int i = 0; i < 200; ++i) {
        void* blockA = msvc6::GameOperatorNew(8);
        void* blockB = msvc6::GameOperatorNew(8);
        const std::uint32_t flags = ctx.rng().Next32() | 1u;
        void* ra = original.objectDeletingDtor(blockA, nullptr, flags);
        void* rb = candidate.objectDeletingDtor(blockB, nullptr, flags);
        if (!S3RE_EXPECT(ra == blockA && rb == blockB)) {
            return;
        }
    }
}

void* __fastcall WrongWaitable(void* self, void* /*edx*/, std::uint32_t handle, std::uint32_t b) {
    Win32_CWaitable_Construct(self, nullptr, handle, b);
    As<CWaitable>(self)->handle.owned = handle;  // wrong: the handle object's second field gets the handle
    return self;
}

}  // namespace

S3RE_TEST(Win32Classes_MatchOriginal) {
    CompareWin32(ctx, OurWin32(), 20000);
}

S3RE_TEST(Win32Classes_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Win32Functions wrong = OurWin32();
    wrong.waitableCtor = &WrongWaitable;
    CompareWin32(probe, wrong, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
