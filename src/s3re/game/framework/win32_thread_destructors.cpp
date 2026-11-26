// The destructor chain of the game's threads (Win32\Classes.cpp,
// Win32\synchronize.ipp, DirectX\Gamebase.cpp): CLogicThread ->
// DirectX::CGameThread -> Win32::CThread -> Win32::CWin32Waitable ->
// Win32::CWin32Handle -> Win32::CObject. The classes have a virtual base
// (CWin32Handle at +4), so each destructor gets ECX = the object + 4 (the
// CWin32Handle part, "self" below) and the scalar deleting destructors
// free self - 4. Offsets in the comments are relative to the full object.
//
// Win32::CWin32Handle::~CWin32Handle (0x0060beb0): vtable 0x65d654, Close
//   0x60bf20 (CloseHandle when owned), then ~CObject 0x610330.
// Win32::CWin32Waitable::~CWin32Waitable (0x0060bfe0): vtables 0x65d660 at
//   +0 / 0x65d670 at +4; with a waitable bag (+0x10): its list (bag + 4)
//   clearAndDestroy 0x60b560, the bag's deleting destructor (slot 0, 1),
//   +0x10 = 0; then ~CWin32Handle on self (null if self - 4 were null, as
//   the compiler's adjustment does).
// Win32::CThread::~CThread (0x0060dff0): vtables 0x65d710 / 0x65d704;
//   with bit 2 of byte +0x18 set (the thread still runs), VERIFY failure
//   "Thread destruction before it's finished" (Classes.cpp line 63,
//   ThrowAssertmentFailed 0x6119b0); then ~CWin32Waitable. When that
//   throws, the original's unwinding still runs ~CWin32Waitable; ours does
//   the same with a guard.
// Win32::CCriticalSection::~CCriticalSection (0x0060d660): DeleteCriticalSection
//   (this) through S3.EXE's import; EAX: its result (nothing).
// DirectX::CGameThread::~CGameThread (0x005b60a0): vtables 0x65ca04 /
//   0x65c9f8; DebugLog "CGameThread::~CGameThread:: iCoInitOpen" when +0x40
//   is set; the object at +0x38: its +4 part's slot 0 (1) (a deleting
//   destructor); the critical section at +0x34: destroyed and freed (game
//   operator delete); then ~CThread.
// DirectX::CGameThread::scalar_deleting_destructor (0x005b6070): (flags)
//   ret 4: ~CGameThread, then with bit 0 operator delete(self - 4); EAX =
//   self - 4.

#include "game/debug_log.h"
#include "game/globals.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kHandleClose = 0x0060bf20;      // Win32::CWin32Handle::Close
constexpr std::uintptr_t kObjectDestroy = 0x00610330;    // Win32::CObject::~CObject
constexpr std::uintptr_t kListClearAndDestroy = 0x0060b560;
constexpr std::uintptr_t kHandleDestroy = 0x0060beb0;
constexpr std::uintptr_t kWaitableDestroy = 0x0060bfe0;
constexpr std::uintptr_t kThreadDestroy = 0x0060dff0;
constexpr std::uintptr_t kCriticalSectionDestroy = 0x0060d660;
constexpr std::uintptr_t kGameThreadDestroy = 0x005b60a0;
constexpr std::uintptr_t kThrowAssertmentFailed = 0x006119b0;  // fastcall (expression, file; line)
constexpr std::uintptr_t kOperatorDelete = 0x0061e4ef;

constexpr std::uint8_t kThreadRunning = 4;     // CThread +0x18 bit 2
constexpr std::uint32_t kThreadVerifyLine = 63;

// The fields, relative to self (the CWin32Handle part at +4).
#pragma pack(push, 1)
struct ThreadParts {
    std::uint32_t vtable_minus4;  // self - 4: the class's own vtable
    std::uint32_t handle_vtable;  // self + 0x00
    std::uint32_t handle;         // self + 0x04
    std::uint32_t owned;          // self + 0x08
    std::uint32_t bag;            // self + 0x0c (+0x10) a Win32::CWin32WaitableBag* or 0
    std::uint32_t thread_id;      // self + 0x10 (+0x14)
    std::uint8_t flags;           // self + 0x14 (+0x18) bit 2: running
    std::uint8_t unk19[0x34 - 0x19];
    std::uint32_t critical_section;  // self + 0x30 (+0x34) a Win32::CCriticalSection* (heap) or 0
    std::uint32_t owned_object;      // self + 0x34 (+0x38) an object whose +4 part has a deleting destructor
    std::uint8_t unk3c[0x40 - 0x3c];
    std::uint32_t co_init_open;      // self + 0x3c (+0x40) non-zero: CoInitialize without CoUninitialize
};
#pragma pack(pop)
static_assert(offsetof(ThreadParts, bag) == 0x10);
static_assert(offsetof(ThreadParts, flags) == 0x18);
static_assert(offsetof(ThreadParts, critical_section) == 0x34);
static_assert(offsetof(ThreadParts, co_init_open) == 0x40);

ThreadParts* PartsOf(void* self) {
    return reinterpret_cast<ThreadParts*>(static_cast<std::uint8_t*>(self) - 4);
}

template <typename R = std::uint32_t, typename... Args>
R CallThis(std::uintptr_t address, const void* self, Args... args) {
    using Fn = R(__fastcall*)(const void*, void*, decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(address)(self, nullptr, static_cast<std::uint32_t>(args)...);
}

// Slot 0 (the deleting destructor) of the object at `address`, flags 1.
void DeleteVirtual(std::uint32_t address) {
    const auto* object = reinterpret_cast<const std::uint32_t* const*>(static_cast<std::uintptr_t>(address));
    using Fn = std::uint32_t(__fastcall*)(std::uint32_t, void*, std::uint32_t);
    reinterpret_cast<Fn>((*object)[0])(address, nullptr, 1);
}

void GameDelete(std::uint32_t address) {
    using Fn = void(__cdecl*)(std::uint32_t);
    reinterpret_cast<Fn>(kOperatorDelete)(address);
}

// Runs a base destructor when it goes out of scope, also during unwinding
// (as the original's exception state does).
struct BaseDestructor {
    std::uintptr_t address;
    void* self;
    ~BaseDestructor() { CallThis(address, self); }
};

}  // namespace

using DestroyFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using DeletingDestroyFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t flags);
using CriticalSectionDestroyFn = std::uint32_t(__fastcall*)(void* self, void* edx);

// Purpose: Destroys a Windows handle wrapper: closes the handle if the
// object owns it.
std::uint32_t __fastcall Win32_CWin32Handle_Destroy(void* self, void*) {
    *static_cast<std::uint32_t*>(self) = vtables::kCWin32Handle;
    CallThis(kHandleClose, self);
    return CallThis(kObjectDestroy, self);
}

// Purpose: Destroys a waitable object (event, thread...): frees the list
// of objects waited for together with it, then the handle.
std::uint32_t __fastcall Win32_CWin32Waitable_Destroy(void* self, void*) {
    ThreadParts* parts = PartsOf(self);
    parts->vtable_minus4 = vtables::kCWin32Waitable;
    parts->handle_vtable = vtables::kCWin32Waitable_sub4;
    if (parts->bag != 0) {
        CallThis(kListClearAndDestroy, reinterpret_cast<void*>(static_cast<std::uintptr_t>(parts->bag + 4)));
        if (parts->bag != 0) {
            DeleteVirtual(parts->bag);
        }
        parts->bag = 0;
    }
    // (self - 4 == 0 would pass null, as the compiler's adjustment does.)
    void* const handle = reinterpret_cast<std::uintptr_t>(self) - 4 != 0 ? self : nullptr;
    return CallThis(kHandleDestroy, handle);
}

// Purpose: Destroys a thread object; destroying a thread that still runs
// is a programming error (an assertion).
std::uint32_t __fastcall Win32_CThread_Destroy(void* self, void*) {
    ThreadParts* parts = PartsOf(self);
    parts->vtable_minus4 = vtables::kCThread;
    parts->handle_vtable = vtables::kCThread_sub4;
    const BaseDestructor base{kWaitableDestroy, self};
    if ((parts->flags & kThreadRunning) != 0) {
        using ThrowFn = void(__fastcall*)(const char*, const char*, std::uint32_t);
        reinterpret_cast<ThrowFn>(kThrowAssertmentFailed)(texts::kThreadDestroyedRunning.c_str(),
                                                          texts::kClassesFile.c_str(), kThreadVerifyLine);
    }
    return 0;  // (EAX: ~CWin32Waitable's, set by the guard after this; callers ignore it)
}

// Purpose: Frees a Windows critical section.
std::uint32_t __fastcall Win32_CCriticalSection_Destroy(void* self, void*) {
    g_importDeleteCriticalSection.get()(self);
    return 0;
}

// Purpose: Destroys one of the game's threads (logic, draw, network): its
// helper objects and critical section, then the thread.
std::uint32_t __fastcall DirectX_CGameThread_Destroy(void* self, void*) {
    ThreadParts* parts = PartsOf(self);
    parts->vtable_minus4 = vtables::kCGameThread;
    parts->handle_vtable = vtables::kCGameThread_sub4;
    const BaseDestructor base{kThreadDestroy, self};
    if (parts->co_init_open != 0) {
        GameDebugLog(texts::kGameThreadCoInitOpen);
    }
    if (parts->owned_object != 0) {
        DeleteVirtual(parts->owned_object + 4);
    }
    const std::uint32_t section = parts->critical_section;
    if (section != 0) {
        CallThis(kCriticalSectionDestroy, reinterpret_cast<void*>(static_cast<std::uintptr_t>(section)));
        GameDelete(section);
    }
    return 0;
}

// Purpose: The game thread's deleting destructor (delete through the
// vtable).
std::uint32_t __fastcall DirectX_CGameThread_DeletingDestroy(void* self, void*, std::uint32_t flags) {
    CallThis(kGameThreadDestroy, self);
    const std::uint32_t object = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(self)) - 4;
    if ((flags & 1) != 0) {
        GameDelete(object);
    }
    return object;
}

S3RE_HOOK(0x0060beb0, Win32_CWin32Handle_Destroy, DestroyFn, Original_Win32_CWin32Handle_Destroy,
          Win32_CWin32Handle_Destroy);
S3RE_HOOK(0x0060bfe0, Win32_CWin32Waitable_Destroy, DestroyFn, Original_Win32_CWin32Waitable_Destroy,
          Win32_CWin32Waitable_Destroy);
S3RE_HOOK(0x0060dff0, Win32_CThread_Destroy, DestroyFn, Original_Win32_CThread_Destroy, Win32_CThread_Destroy);
S3RE_HOOK(0x0060d660, Win32_CCriticalSection_Destroy, CriticalSectionDestroyFn,
          Original_Win32_CCriticalSection_Destroy, Win32_CCriticalSection_Destroy);
S3RE_HOOK(0x005b60a0, DirectX_CGameThread_Destroy, DestroyFn, Original_DirectX_CGameThread_Destroy,
          DirectX_CGameThread_Destroy);
S3RE_HOOK(0x005b6070, DirectX_CGameThread_DeletingDestroy, DeletingDestroyFn,
          Original_DirectX_CGameThread_DeletingDestroy, DirectX_CGameThread_DeletingDestroy);

// ---------------------------------------------------------------------------
// Tests: each destructor on random object images, with the next base
// destructor and the other callees as recording stubs (thiscall stubs;
// operator delete, DeleteCriticalSection and the assertion thrower as
// recording fakes that return), virtual slots of a recording vtable.
// Compared: the object, EAX where callers use it (the deleting destructor),
// the calls in order (addresses relative to the object image).
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kImageBytes = 0x80;

struct Recorder {
    std::vector<std::uint32_t> log;
    std::uintptr_t base = 0;
    std::uint32_t Rel(std::uintptr_t address) const {
        return static_cast<std::uint32_t>(address - base);
    }
};
Recorder* g_recorder = nullptr;

void __cdecl FakeDelete(std::uint32_t address) {
    g_recorder->log.insert(g_recorder->log.end(), {0xde1u, g_recorder->Rel(address)});
}
void __stdcall FakeDeleteCriticalSection(void* section) {
    g_recorder->log.insert(g_recorder->log.end(), {0xc5u, g_recorder->Rel(reinterpret_cast<std::uintptr_t>(section))});
}
void __fastcall FakeThrowAssertment(const char* expression, const char* file, std::uint32_t line) {
    g_recorder->log.insert(g_recorder->log.end(),
                           {0xa55u, reinterpret_cast<std::uint32_t>(expression), reinterpret_cast<std::uint32_t>(file), line});
}
std::uint32_t __fastcall FakeBase(void* self, void*) {
    g_recorder->log.insert(g_recorder->log.end(), {0xba5eu, g_recorder->Rel(reinterpret_cast<std::uintptr_t>(self))});
    return 0x1234;
}
std::uint32_t __fastcall FakeSlot0(void* self, void*, std::uint32_t flags) {
    g_recorder->log.insert(g_recorder->log.end(), {0x510u, g_recorder->Rel(reinterpret_cast<std::uintptr_t>(self)), flags});
    return 0;
}

struct Outcome {
    std::uint32_t eax;
    std::vector<std::uint8_t> image;
    std::vector<std::uint32_t> log;
    std::vector<test::StubCall> calls;
    std::vector<DebugLogCall> logs;
};

using Caller = std::uint32_t (*)(void* self, bool original, std::uint32_t flags);

Outcome Run(const std::vector<std::uint8_t>& start, Caller call, bool original, std::uint32_t flags,
            std::uint32_t pointerMask) {
    Outcome out;
    out.image = start;
    std::uint8_t* image = out.image.data();
    Recorder recorder;
    recorder.base = reinterpret_cast<std::uintptr_t>(image);
    g_recorder = &recorder;
    // A fake vtable for the bag (+0x10), the owned object's part (+0x38 + 4)
    // and the critical section (+0x34): their pointers lead into the image.
    void* vtable[1] = {reinterpret_cast<void*>(&FakeSlot0)};
    auto setPointer = [&](std::size_t at, std::size_t target, std::uint32_t bit) {
        const std::uint32_t value = (pointerMask & bit) != 0 ? static_cast<std::uint32_t>(recorder.base + target) : 0;
        std::memcpy(image + at, &value, 4);
    };
    setPointer(0x10, 0x48, 1);   // bag -> +0x48 (vtable there)
    setPointer(0x34, 0x58, 2);   // critical section -> +0x58
    setPointer(0x38, 0x60, 4);   // owned object -> +0x60 (part at +0x64 has the vtable)
    void* const table = vtable;
    std::memcpy(image + 0x48, &table, 4);
    std::memcpy(image + 0x64, &table, 4);
    const void* fakeDcs = reinterpret_cast<const void*>(&FakeDeleteCriticalSection);
    test::PatchBytes(g_importDeleteCriticalSection.address(), &fakeDcs, sizeof(fakeDcs));
    test::PatchJump(kOperatorDelete, reinterpret_cast<const void*>(&FakeDelete));
    test::PatchJump(kThrowAssertmentFailed, reinterpret_cast<const void*>(&FakeThrowAssertment));
    {
        test::CalleeStubs stubs;
        stubs.SetWorld(reinterpret_cast<CGameData*>(image));
        stubs.Install<0, kHandleClose>();
        stubs.Install<0, kObjectDestroy>();
        stubs.Install<0, kListClearAndDestroy>();
        if (call != nullptr) {
            DebugLogRecorder logs;
            out.eax = call(image + 4, original, flags);
            out.logs = logs.Take();
        }
        out.calls = stubs.Take();
    }
    test::UnpatchJump(kThrowAssertmentFailed);
    test::UnpatchJump(kOperatorDelete);
    test::UnpatchBytes(g_importDeleteCriticalSection.address());
    // The fake vtable's address (a local of each run) is not compared.
    std::memset(image + 0x48, 0, 4);
    std::memset(image + 0x64, 0, 4);
    for (std::size_t at : {std::size_t{0x10}, std::size_t{0x34}, std::size_t{0x38}}) {
        std::uint32_t value = 0;
        std::memcpy(&value, image + at, 4);
        if (value != 0) {
            value = static_cast<std::uint32_t>(value - recorder.base);
        }
        std::memcpy(image + at, &value, 4);
    }
    out.log = recorder.log;
    g_recorder = nullptr;
    return out;
}

void CompareDestructor(test::Context& ctx, Caller call, std::uintptr_t nextBase, int cases) {
    for (int i = 0; i < cases; ++i) {
        std::vector<std::uint8_t> start(kImageBytes);
        ctx.rng().Fill(start.data(), start.size());
        const std::uint32_t pointerMask = ctx.rng().Below(8);
        if (ctx.rng().Chance(1, 2)) {
            std::memset(start.data() + 0x40, 0, 4);  // co_init_open
        }
        const std::uint32_t flags = ctx.rng().Below(4);
        Outcome expected, actual;
        if (nextBase != 0) {
            test::PatchJump(nextBase, reinterpret_cast<const void*>(&FakeBase));
        }
        expected = Run(start, call, true, flags, pointerMask);
        actual = Run(start, call, false, flags, pointerMask);
        if (nextBase != 0) {
            test::UnpatchJump(nextBase);
        }
        bool same = S3RE_EXPECT_EQ(expected.eax, actual.eax);
        same &= S3RE_EXPECT_MEM_EQ(expected.image.data(), actual.image.data(), kImageBytes);
        same &= S3RE_EXPECT(expected.log == actual.log);
        same &= S3RE_EXPECT(expected.calls == actual.calls);
        same &= S3RE_EXPECT(expected.logs == actual.logs);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (pointers %u, flags %u)", i, pointerMask, flags);
            return;
        }
    }
}

std::uint32_t CallHandle(void* self, bool original, std::uint32_t) {
    return (original ? Original_Win32_CWin32Handle_Destroy : &Win32_CWin32Handle_Destroy)(self, nullptr);
}
std::uint32_t CallWaitable(void* self, bool original, std::uint32_t) {
    return (original ? Original_Win32_CWin32Waitable_Destroy : &Win32_CWin32Waitable_Destroy)(self, nullptr);
}
std::uint32_t CallThread(void* self, bool original, std::uint32_t) {
    (original ? Original_Win32_CThread_Destroy : &Win32_CThread_Destroy)(self, nullptr);
    return 0;  // (EAX not used by the callers)
}
std::uint32_t CallCriticalSection(void* self, bool original, std::uint32_t) {
    (original ? Original_Win32_CCriticalSection_Destroy : &Win32_CCriticalSection_Destroy)(self, nullptr);
    return 0;
}
std::uint32_t CallGameThread(void* self, bool original, std::uint32_t) {
    (original ? Original_DirectX_CGameThread_Destroy : &DirectX_CGameThread_Destroy)(self, nullptr);
    return 0;
}
std::uint32_t CallGameThreadDeleting(void* self, bool original, std::uint32_t flags) {
    const std::uint32_t eax = (original ? Original_DirectX_CGameThread_DeletingDestroy
                                        : &DirectX_CGameThread_DeletingDestroy)(self, nullptr, flags);
    return eax - static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(self));
}

// Wrong: the bag pointer is left set.
std::uint32_t CallWrongWaitable(void* self, bool original, std::uint32_t flags) {
    if (original) {
        return CallWaitable(self, true, flags);
    }
    const std::uint32_t bag = PartsOf(self)->bag;
    const std::uint32_t eax = Win32_CWin32Waitable_Destroy(self, nullptr);
    PartsOf(self)->bag = bag;
    return eax;
}

}  // namespace

S3RE_TEST(ThreadDestructors_MatchOriginal) {
    CompareDestructor(ctx, &CallHandle, 0, 300);
    CompareDestructor(ctx, &CallWaitable, kHandleDestroy, 300);
    CompareDestructor(ctx, &CallThread, kWaitableDestroy, 300);
    CompareDestructor(ctx, &CallCriticalSection, 0, 100);
    CompareDestructor(ctx, &CallGameThread, kThreadDestroy, 300);
    CompareDestructor(ctx, &CallGameThreadDeleting, kGameThreadDestroy, 300);
}

S3RE_TEST(ThreadDestructors_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDestructor(probe, &CallWrongWaitable, kHandleDestroy, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
