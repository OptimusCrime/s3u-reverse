// DirectX::CGameThread / CGameDrawThread members (DirectX\Gamebase.cpp;
// re/ui_classes.md, "CDrawThread"): the draw thread's start, its per-step
// frame and the drawer it owns.
//
// Layout (the full object; CWin32Handle, the virtual base, at +4):
// CGameThread +0x28 active (byte); CGameDrawThread +0x44 the drawer (a
// DirectX::CGameDrawer), +0x48 the game window, +0x4c frames drawn, +0x50
// full redraw flag. g_drawThread (0x7e8bec) is the draw thread.
//
// CGameThread::Start (0x005b6130): thiscall (): Win32::CThread::Start
//   (0x60e060) with the application's CWin32Handle part as the parameter
//   (g_app + 4 + the offset in its vbtable's second entry; 0 without an
//   application). EAX = Start's.
// CGameThread::OnActivate (0x005b6520): vtable slot 10, (active) ret 4:
//   +0x28 = active's low byte. EAX: AL = that byte (the upper bytes are the
//   caller's in the original; nobody reads them).
// CGameDrawThread::CGameDrawThread (0x005b6530): CGameThread's constructor
//   (0x5b5f60), +0x50 = 1, +0x44 / +0x48 / +0x4c = 0, vtables 0x65ca40 /
//   0x65ca34, g_drawThread = this. EAX = this.
// CGameDrawThread::~CGameDrawThread (0x005b65a0): ECX = the CWin32Handle
//   part (this + 4): vtables back to 0x65ca40 / 0x65ca34, g_drawThread = 0,
//   then (a tail jump) ~CGameThread (0x5b60a0). EAX = ~CGameThread's.
// CGameDrawThread::StartWithWindow (0x005b65c0): vtable slot 11, (window)
//   ret 4: +0x48 = window, then CGameThread::Start. EAX = Start's.
// CGameDrawThread::OnStart (0x005b65d0): vtable slot 4: CGameThread::OnStart
//   (0x5b6160); then, inside `try { } catch (...) { if (drawer) delete
//   drawer (slot 0 with 1, +0x44 not cleared); CGameThread::OnExit
//   (0x5b6470); throw; }`: slot 12 (CreateDrawer), Win32::ThrowOutOfMemory
//   ("lpDrawer", Gamebase.cpp line 506) when +0x44 is still null, and the
//   drawer's slot 7 (Initialize) with the window. EAX = slot 7's.
// CGameDrawThread::OnExit (0x005b6670): vtable slot 6: with a drawer, its
//   slot 8 (Shutdown), its deleting destructor (slot 0 with 1, if +0x44 is
//   still set), +0x44 = 0, CGameThread::OnExit. EAX: OnExit's; without a
//   drawer the caller's (ours: 0; CThread::Execute 0x60e2d0 ignores slot
//   6's EAX).
// CGameDrawThread::Step (0x005b66a0): vtable slot 9: +0x4c += 1; inside
//   `try { } catch (DirectX::CAppNotActiveException* e) { delete e (slot 0
//   with 1, if not null); }`: slot 13 (Draw) with the full redraw flag,
//   then the flag = 0 (skipped when the exception came). EAX: slot 13's;
//   after a caught exception the continuation address 0x5b66dd (MSVC's
//   catch returns it in EAX; nobody reads it, CGameThread::Main ignores
//   Step's EAX).
// CGameDrawThread::OnActivate (0x005b6710): vtable slot 10, (active) ret 4:
//   +0x50 = 1 (redraw completely), then CGameThread::OnActivate. EAX: its.
//
// Known difference: OnStart's catch (...) is an SEH filter (as in
// sound_effects.cpp): the cleanup runs before the inner frames are unwound
// (CDrawThread::CreateDrawer's unwind frees a drawer whose constructor
// threw), not after; Step's typed catch is a filter that matches like
// MSVC's handler (msvc6/catch_match.h) and an __except block.

#include "game/framework/framework_layout.h"
#include "game/globals.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/catch_match.h"
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

constexpr std::uintptr_t kThreadStart = 0x0060e060;          // Win32::CThread::Start(parameter) ret 4
constexpr std::uintptr_t kGameThreadConstruct = 0x005b5f60;  // DirectX::CGameThread::CGameThread() ret
constexpr std::uintptr_t kGameThreadDestroy = 0x005b60a0;    // DirectX::CGameThread::~CGameThread (ECX = +4 part)
constexpr std::uintptr_t kGameThreadOnStart = 0x005b6160;    // DirectX::CGameThread::OnStart() ret
constexpr std::uintptr_t kGameThreadOnExit = 0x005b6470;     // DirectX::CGameThread::OnExit() ret
constexpr std::uintptr_t kGameThreadStart = 0x005b6130;
constexpr std::uintptr_t kGameThreadOnActivate = 0x005b6520;
constexpr std::uintptr_t kThrowOutOfMemory = 0x00610960;     // Win32::ThrowOutOfMemory(name, file; line)

constexpr std::uint32_t kGameDrawThreadVtable = vtables::kCGameDrawThread;
constexpr std::uint32_t kGameDrawThreadHandleVtable = vtables::kCGameDrawThread_sub4;  // its CWin32Handle part (+4)
constexpr std::uint32_t kStepContinuation = 0x5b66dd;            // EAX after Step's catch
constexpr std::uint32_t kDrawerCheckLine = 506;                  // Gamebase.cpp, the lpDrawer check

// Virtual slots (indexes).
constexpr std::uint32_t kSlotDelete = 0;          // scalar deleting destructor (1)
constexpr std::uint32_t kSlotCreateDrawer = 12;   // CGameDrawThread
constexpr std::uint32_t kSlotDraw = 13;           // CGameDrawThread (full redraw)
constexpr std::uint32_t kDrawerSlotInitialize = 7;   // CGameDrawer (window)
constexpr std::uint32_t kDrawerSlotShutdown = 8;     // CGameDrawer

constexpr char kAppNotActiveName[] = ".PAVCAppNotActiveException@DirectX@@";
constexpr std::uintptr_t kAppNotActiveDescriptor = type_descriptor::kCAppNotActiveExceptionPointer;

#pragma pack(push, 1)
struct GameDrawThread {
    void* const* vtable;            // +0x00
    std::uint32_t handle_vtable;    // +0x04 the CWin32Handle part (the virtual base)
    std::uint8_t unk08[0x28 - 0x08];
    std::uint8_t active;            // +0x28 CGameThread: the application is active
    std::uint8_t unk29[0x44 - 0x29];
    void* drawer;                   // +0x44 the DirectX::CGameDrawer
    void* window;                   // +0x48
    std::uint32_t frames;           // +0x4c Step count
    std::uint32_t full_redraw;      // +0x50 the next frame is drawn completely
};
#pragma pack(pop)
static_assert(offsetof(GameDrawThread, active) == 0x28);
static_assert(offsetof(GameDrawThread, drawer) == 0x44);
static_assert(offsetof(GameDrawThread, full_redraw) == 0x50);
static_assert(sizeof(GameDrawThread) == 0x54);

using MemberFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using Member1Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t argument);
using ThrowOutOfMemoryFn = void(__fastcall*)(const char* name, const char* file, std::uint32_t line);

template <typename Fn>
Fn Slot(void* object, std::uint32_t slot) {
    return reinterpret_cast<Fn>((*static_cast<void* const* const*>(object))[slot]);
}

std::uint32_t CallSlot(void* object, std::uint32_t slot) { return Slot<MemberFn>(object, slot)(object, nullptr); }
std::uint32_t CallSlot(void* object, std::uint32_t slot, std::uint32_t argument) {
    return Slot<Member1Fn>(object, slot)(object, nullptr, argument);
}

GameDrawThread* AsThread(void* self) { return static_cast<GameDrawThread*>(self); }

// 0x5b6641: catch (...) { delete drawer; CGameThread::OnExit; throw; }.
int CleanUpFailedStart(GameDrawThread* thread) {
    if (thread->drawer != nullptr) {
        CallSlot(thread->drawer, kSlotDelete, 1);
    }
    reinterpret_cast<MemberFn>(kGameThreadOnExit)(thread, nullptr);
    return EXCEPTION_CONTINUE_SEARCH;
}

// 0x5b6606..0x5b662d, OnStart's try block.
std::uint32_t CreateAndInitializeDrawer(GameDrawThread* thread) {
    std::uint32_t result = 0;
    __try {
        CallSlot(thread, kSlotCreateDrawer);
        if (thread->drawer == nullptr) {
            reinterpret_cast<ThrowOutOfMemoryFn>(kThrowOutOfMemory)(texts::kDrawerNotNull, texts::kGamebaseFile,
                                                                    kDrawerCheckLine);
        }
        result = CallSlot(thread->drawer, kDrawerSlotInitialize, reinterpret_cast<std::uint32_t>(thread->window));
    } __except (CleanUpFailedStart(thread)) {
    }
    return result;
}

int AppNotActiveFilter(const EXCEPTION_RECORD* record, void** caught) {
    const msvc6::CatchMatch match = msvc6::MatchPointerCatch(record, kAppNotActiveDescriptor, kAppNotActiveName);
    if (!match.matched) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    *caught = match.object;
    return EXCEPTION_EXECUTE_HANDLER;
}

// 0x5b66d7..0x5b66da, Step's try block: true when it ended normally.
bool DrawFrameGuarded(GameDrawThread* thread, std::uint32_t* result, void** caught) {
    __try {
        *result = CallSlot(thread, kSlotDraw, thread->full_redraw);
        thread->full_redraw = 0;
        return true;
    } __except (AppNotActiveFilter(GetExceptionInformation()->ExceptionRecord, caught)) {
        return false;
    }
}

}  // namespace

std::uint32_t __fastcall DirectX_CGameThread_Start(void* self, void* edx);
std::uint32_t __fastcall DirectX_CGameThread_OnActivate(void* self, void* edx, std::uint32_t active);
void* __fastcall DirectX_CGameDrawThread_Construct(void* self, void* edx);
std::uint32_t __fastcall DirectX_CGameDrawThread_Destroy(void* handle, void* edx);
std::uint32_t __fastcall DirectX_CGameDrawThread_StartWithWindow(void* self, void* edx, void* window);
std::uint32_t __fastcall DirectX_CGameDrawThread_OnStart(void* self, void* edx);
std::uint32_t __fastcall DirectX_CGameDrawThread_OnExit(void* self, void* edx);
std::uint32_t __fastcall DirectX_CGameDrawThread_Step(void* self, void* edx);
std::uint32_t __fastcall DirectX_CGameDrawThread_OnActivate(void* self, void* edx, std::uint32_t active);

using ThreadFn = decltype(&DirectX_CGameThread_Start);
using ThreadArgFn = decltype(&DirectX_CGameThread_OnActivate);
using ConstructFn = decltype(&DirectX_CGameDrawThread_Construct);
using StartWithWindowFn = decltype(&DirectX_CGameDrawThread_StartWithWindow);

S3RE_HOOK(0x005b6130, DirectX_CGameThread_Start, ThreadFn, Original_DirectX_CGameThread_Start,
          DirectX_CGameThread_Start);
S3RE_HOOK(0x005b6520, DirectX_CGameThread_OnActivate, ThreadArgFn, Original_DirectX_CGameThread_OnActivate,
          DirectX_CGameThread_OnActivate);
S3RE_HOOK(0x005b6530, DirectX_CGameDrawThread_Construct, ConstructFn, Original_DirectX_CGameDrawThread_Construct,
          DirectX_CGameDrawThread_Construct);
S3RE_HOOK(0x005b65a0, DirectX_CGameDrawThread_Destroy, ThreadFn, Original_DirectX_CGameDrawThread_Destroy,
          DirectX_CGameDrawThread_Destroy);
S3RE_HOOK(0x005b65c0, DirectX_CGameDrawThread_StartWithWindow, StartWithWindowFn,
          Original_DirectX_CGameDrawThread_StartWithWindow, DirectX_CGameDrawThread_StartWithWindow);
S3RE_HOOK(0x005b65d0, DirectX_CGameDrawThread_OnStart, ThreadFn, Original_DirectX_CGameDrawThread_OnStart,
          DirectX_CGameDrawThread_OnStart);
S3RE_HOOK(0x005b6670, DirectX_CGameDrawThread_OnExit, ThreadFn, Original_DirectX_CGameDrawThread_OnExit,
          DirectX_CGameDrawThread_OnExit);
S3RE_HOOK(0x005b66a0, DirectX_CGameDrawThread_Step, ThreadFn, Original_DirectX_CGameDrawThread_Step,
          DirectX_CGameDrawThread_Step);
S3RE_HOOK(0x005b6710, DirectX_CGameDrawThread_OnActivate, ThreadArgFn, Original_DirectX_CGameDrawThread_OnActivate,
          DirectX_CGameDrawThread_OnActivate);

// Purpose: starts a game thread, with the application as its parent.
std::uint32_t __fastcall DirectX_CGameThread_Start(void* self, void* /*edx*/) {
    std::uint8_t* const app = g_app;
    std::uint32_t parent = 0;
    if (app != nullptr) {
        // The application's virtual base: its vbtable (at +4) holds the offset.
        const auto* const vbtable = *reinterpret_cast<const std::int32_t* const*>(app + 4);
        parent = reinterpret_cast<std::uint32_t>(app + 4 + vbtable[1]);
    }
    return reinterpret_cast<Member1Fn>(kThreadStart)(self, nullptr, parent);
}

// Purpose: records whether the application is active (the game threads
// pause while it is not).
std::uint32_t __fastcall DirectX_CGameThread_OnActivate(void* self, void* /*edx*/, std::uint32_t active) {
    const auto value = static_cast<std::uint8_t>(active);
    AsThread(self)->active = value;
    return value;
}

// Purpose: constructor of the draw thread's framework part; registers it
// as the game's draw thread.
void* __fastcall DirectX_CGameDrawThread_Construct(void* self, void* /*edx*/) {
    reinterpret_cast<MemberFn>(kGameThreadConstruct)(self, nullptr);
    GameDrawThread* const thread = AsThread(self);
    thread->full_redraw = 1;
    thread->drawer = nullptr;
    thread->window = nullptr;
    thread->frames = 0;
    thread->vtable = reinterpret_cast<void* const*>(kGameDrawThreadVtable);
    thread->handle_vtable = kGameDrawThreadHandleVtable;
    g_drawThread = self;
    return self;
}

// Purpose: destructor of the draw thread's framework part (ECX is its
// CWin32Handle part, 4 bytes in).
std::uint32_t __fastcall DirectX_CGameDrawThread_Destroy(void* handle, void* /*edx*/) {
    auto* const thread = reinterpret_cast<GameDrawThread*>(static_cast<std::uint8_t*>(handle) - 4);
    thread->vtable = reinterpret_cast<void* const*>(kGameDrawThreadVtable);
    thread->handle_vtable = kGameDrawThreadHandleVtable;
    g_drawThread = nullptr;
    return reinterpret_cast<MemberFn>(kGameThreadDestroy)(handle, nullptr);
}

// Purpose: starts the draw thread for the game window.
std::uint32_t __fastcall DirectX_CGameDrawThread_StartWithWindow(void* self, void* /*edx*/, void* window) {
    AsThread(self)->window = window;
    return reinterpret_cast<MemberFn>(kGameThreadStart)(self, nullptr);
}

// Purpose: the draw thread's start-up: creates its drawer and initializes
// it for the game window (on failure, cleans up and passes the error on).
std::uint32_t __fastcall DirectX_CGameDrawThread_OnStart(void* self, void* /*edx*/) {
    reinterpret_cast<MemberFn>(kGameThreadOnStart)(self, nullptr);
    return CreateAndInitializeDrawer(AsThread(self));
}

// Purpose: the draw thread's shutdown: shuts the drawer down and deletes it.
std::uint32_t __fastcall DirectX_CGameDrawThread_OnExit(void* self, void* /*edx*/) {
    GameDrawThread* const thread = AsThread(self);
    if (thread->drawer == nullptr) {
        return 0;
    }
    CallSlot(thread->drawer, kDrawerSlotShutdown);
    if (thread->drawer != nullptr) {
        CallSlot(thread->drawer, kSlotDelete, 1);
    }
    thread->drawer = nullptr;
    return reinterpret_cast<MemberFn>(kGameThreadOnExit)(self, nullptr);
}

// Purpose: one step of the draw thread: draws a frame (completely when a
// full redraw was asked for); a frame interrupted because the game lost
// the screen is dropped.
std::uint32_t __fastcall DirectX_CGameDrawThread_Step(void* self, void* /*edx*/) {
    GameDrawThread* const thread = AsThread(self);
    ++thread->frames;
    std::uint32_t result = 0;
    void* caught = nullptr;
    if (DrawFrameGuarded(thread, &result, &caught)) {
        return result;
    }
    if (caught != nullptr) {
        msvc6::DeleteCaughtException(caught);
    }
    return kStepContinuation;
}

// Purpose: when the application is activated or deactivated: the next
// frame is drawn completely.
std::uint32_t __fastcall DirectX_CGameDrawThread_OnActivate(void* self, void* /*edx*/, std::uint32_t active) {
    AsThread(self)->full_redraw = 1;
    return reinterpret_cast<Member1Fn>(kGameThreadOnActivate)(self, nullptr, active);
}

// ---------------------------------------------------------------------------
// Tests: a thread image (random bytes; its vtable a recording fake whose
// slots 12 and 13 may throw, slot 12 may set the drawer), a fake drawer
// (slots 0, 7 and 8 recording; slot 8 may clear +0x44), the callees as
// recording stubs; g_app null or a fake application with a vbtable;
// g_drawThread compared. Thrown game exceptions (a fake object with a
// recording deleting destructor; throw infos CAppNotActiveException*,
// CTimeout* and Win32::CHResult*) are caught by the test and compared.
// ---------------------------------------------------------------------------

namespace {

constexpr std::uintptr_t kCxxThrowException = 0x0061f027;

struct Script {
    test::Rng rng{0};
    std::vector<std::uint32_t> log;
    std::uint32_t base = 0;
    std::uint32_t throwSlot = 0xffffffffu;  // vtable slot (12 or 13) that throws
    std::uintptr_t throwInfo = 0;
    bool createSetsDrawer = true;
    bool shutdownClearsDrawer = false;
    void* thrownObject = nullptr;
    GameDrawThread* thread = nullptr;
    void* drawer = nullptr;
};
Script* g_script = nullptr;

std::uint32_t Rel(const void* p) { return reinterpret_cast<std::uint32_t>(p) - g_script->base; }

void MaybeThrow(std::uint32_t slot) {
    if (g_script->throwSlot != slot) {
        return;
    }
    void* object = g_script->thrownObject;
    using ThrowFn = void(__stdcall*)(void* object, std::uintptr_t info);
    reinterpret_cast<ThrowFn>(kCxxThrowException)(&object, g_script->throwInfo);
}

template <std::uint32_t Slot>
std::uint32_t __fastcall FakeSlot0(void* self, void*) {
    g_script->log.insert(g_script->log.end(), {0x5100u + Slot, Rel(self)});
    if (Slot == kSlotCreateDrawer && g_script->createSetsDrawer) {
        g_script->thread->drawer = g_script->drawer;
    }
    if (Slot == 0x100 + kDrawerSlotShutdown && g_script->shutdownClearsDrawer) {
        g_script->thread->drawer = nullptr;
    }
    MaybeThrow(Slot);
    return g_script->rng.Next32();
}

template <std::uint32_t Slot>
std::uint32_t __fastcall FakeSlot1(void* self, void*, std::uint32_t argument) {
    g_script->log.insert(g_script->log.end(), {0x5100u + Slot, Rel(self), argument});
    MaybeThrow(Slot);
    return g_script->rng.Next32();
}

struct Fixture {
    GameDrawThread thread;
    std::uint8_t threadRest[0x30];
    const void* drawerVtable;      // the fake drawer object
    std::uint8_t drawerRest[0x40];
    const void* exceptionVtable;   // the fake thrown object
    std::uint8_t app[0x10];        // a fake application: +4 its vbtable pointer
    std::int32_t appVbtable[2];
};

enum class Target { Start, GameOnActivate, Construct, Destroy, StartWithWindow, OnStart, OnExit, Step, OnActivate };

struct Functions {
    ThreadFn start;
    ThreadArgFn gameOnActivate;
    ConstructFn construct;
    ThreadFn destroy;
    StartWithWindowFn startWithWindow;
    ThreadFn onStart;
    ThreadFn onExit;
    ThreadFn step;
    ThreadArgFn onActivate;
};

struct Case {
    Fixture fixture;
    std::uint64_t seed;
    std::uint32_t argument;
    bool hasApp;
    bool hasDrawer;
    Script plan;
};

struct Outcome {
    std::uint32_t eax = 0;
    Fixture fixture;
    std::vector<std::uint32_t> log;
    std::vector<test::StubCall> calls;
    test::CaughtException caught;
    std::uint32_t drawThread = 0;
};

struct Guarded {
    const Functions* f;
    Target target;
    GameDrawThread* thread;
    std::uint32_t argument;
};

std::uint32_t CallTarget(const Guarded* g) {
    void* const self = g->thread;
    switch (g->target) {
        case Target::Start: return g->f->start(self, nullptr);
        case Target::GameOnActivate: return g->f->gameOnActivate(self, nullptr, g->argument) & 0xff;
        case Target::Construct: return Rel(g->f->construct(self, nullptr));
        case Target::Destroy: return g->f->destroy(static_cast<std::uint8_t*>(self) + 4, nullptr);
        case Target::StartWithWindow:
            return g->f->startWithWindow(self, nullptr, reinterpret_cast<void*>(g->argument));
        case Target::OnStart: return g->f->onStart(self, nullptr);
        case Target::OnExit: return g->f->onExit(self, nullptr);
        case Target::Step: return g->f->step(self, nullptr);
        case Target::OnActivate: return g->f->onActivate(self, nullptr, g->argument);
    }
    return 0;
}

void GuardedCall(const Guarded* g, std::uint32_t* eax, test::CaughtException* caught) {
    __try {
        *eax = CallTarget(g);
    } __except (test::CatchGameException(GetExceptionInformation(), caught)) {
    }
}

std::uint32_t __fastcall FakeExceptionDelete(void* self, void*, std::uint32_t flags) {
    g_script->log.insert(g_script->log.end(), {0xe0de1u, Rel(self), flags});
    return 0;
}

Outcome Run(const Case& c, Target target, const Functions& f) {
    Outcome out;
    Script script = c.plan;
    script.rng = test::Rng(c.seed);
    g_script = &script;
    out.fixture = c.fixture;
    Fixture& fx = out.fixture;
    script.base = reinterpret_cast<std::uint32_t>(&fx);
    script.thread = &fx.thread;
    script.drawer = &fx.drawerVtable;
    script.thrownObject = &fx.exceptionVtable;

    static void* threadVtable[16] = {};
    for (void*& slot : threadVtable) {
        slot = nullptr;
    }
    threadVtable[kSlotCreateDrawer] = reinterpret_cast<void*>(&FakeSlot0<kSlotCreateDrawer>);
    threadVtable[kSlotDraw] = reinterpret_cast<void*>(&FakeSlot1<kSlotDraw>);
    static void* drawerVtable[16] = {};
    drawerVtable[kSlotDelete] = reinterpret_cast<void*>(&FakeSlot1<0x100 + kSlotDelete>);
    drawerVtable[kDrawerSlotInitialize] = reinterpret_cast<void*>(&FakeSlot1<0x100 + kDrawerSlotInitialize>);
    drawerVtable[kDrawerSlotShutdown] = reinterpret_cast<void*>(&FakeSlot0<0x100 + kDrawerSlotShutdown>);
    static void* exceptionVtable[1] = {reinterpret_cast<void*>(&FakeExceptionDelete)};
    if (target != Target::Construct && target != Target::Destroy) {
        fx.thread.vtable = threadVtable;
    }
    fx.drawerVtable = drawerVtable;
    fx.exceptionVtable = exceptionVtable;
    fx.thread.drawer = c.hasDrawer ? &fx.drawerVtable : nullptr;
    const std::int32_t* vbtable = fx.appVbtable;
    std::memcpy(fx.app + 4, &vbtable, 4);

    std::uint8_t* const savedApp = g_app;
    void* const savedDrawThread = g_drawThread;
    g_app = c.hasApp ? fx.app : nullptr;
    g_drawThread = reinterpret_cast<void*>(0x0d7a3d00);
    {
        test::CalleeStubs stubs;
        stubs.SetWorld(reinterpret_cast<CGameData*>(&fx));
        auto value = [&](CGameData*, std::uint32_t, const std::uint32_t*) { return script.rng.Next32(); };
        stubs.Install<1, kThreadStart>(value);
        stubs.Install<0, kGameThreadConstruct>(value);
        stubs.Install<0, kGameThreadDestroy>(value);
        stubs.Install<0, kGameThreadOnStart>(value);
        stubs.Install<0, kGameThreadOnExit>(value);
        if (target == Target::StartWithWindow) {
            stubs.Install<0, kGameThreadStart>(value);
        }
        if (target == Target::OnActivate) {
            stubs.Install<1, kGameThreadOnActivate>(value);
        }
        const Guarded g{&f, target, &fx.thread, c.argument};
        GuardedCall(&g, &out.eax, &out.caught);
        out.calls = stubs.Take();
    }
    for (test::StubCall& call : out.calls) {
        if (call.callee == kThreadStart && call.args[0] != 0) {
            call.args[0] -= script.base;
        }
    }
    out.drawThread = g_drawThread.get() == &fx.thread ? 1 : reinterpret_cast<std::uint32_t>(g_drawThread.get());
    g_app = savedApp;
    g_drawThread = savedDrawThread;
    // Pointers back to fixture-independent values.
    auto unpoint = [&](void*& p) {
        const auto value = reinterpret_cast<std::uint32_t>(p);
        if (value - script.base < sizeof(Fixture)) {
            p = reinterpret_cast<void*>(0xf1000000u + (value - script.base));
        }
    };
    unpoint(fx.thread.drawer);
    if (fx.thread.vtable == reinterpret_cast<void* const*>(threadVtable)) {
        fx.thread.vtable = nullptr;
    }
    fx.drawerVtable = nullptr;
    fx.exceptionVtable = nullptr;
    std::memset(fx.app + 4, 0, 4);
    out.log = script.log;
    g_script = nullptr;
    return out;
}

Case MakeCase(test::Rng& rng, Target target) {
    Case c;
    rng.Fill(&c.fixture, sizeof(c.fixture));
    c.seed = rng.Next64();
    c.argument = rng.Chance(1, 2) ? rng.Below(2) : rng.Next32();
    c.hasApp = rng.Chance(3, 4);
    c.hasDrawer = rng.Chance(2, 3);
    c.fixture.appVbtable[1] = rng.Range(-8, 64);
    c.plan.createSetsDrawer = rng.Chance(3, 4);
    c.plan.shutdownClearsDrawer = rng.Chance(1, 4);
    if (rng.Chance(1, 3)) {
        c.plan.throwSlot = target == Target::Step ? kSlotDraw
                                                  : (rng.Chance(1, 2) ? kSlotCreateDrawer : 0x100 + kDrawerSlotInitialize);
        static constexpr std::uintptr_t kInfos[] = {throw_info::kAppNotActiveException, throw_info::kCTimeoutPointer,
                                                    throw_info::kCHResultPointer};
        c.plan.throwInfo = kInfos[rng.Below(3)];
    }
    return c;
}

constexpr Functions kOurs{DirectX_CGameThread_Start,        DirectX_CGameThread_OnActivate,
                          DirectX_CGameDrawThread_Construct, DirectX_CGameDrawThread_Destroy,
                          DirectX_CGameDrawThread_StartWithWindow, DirectX_CGameDrawThread_OnStart,
                          DirectX_CGameDrawThread_OnExit,     DirectX_CGameDrawThread_Step,
                          DirectX_CGameDrawThread_OnActivate};

void Compare(test::Context& ctx, Target target, const Functions& f, int cases) {
    const Functions originals{Original_DirectX_CGameThread_Start,           Original_DirectX_CGameThread_OnActivate,
                              Original_DirectX_CGameDrawThread_Construct,   Original_DirectX_CGameDrawThread_Destroy,
                              Original_DirectX_CGameDrawThread_StartWithWindow, Original_DirectX_CGameDrawThread_OnStart,
                              Original_DirectX_CGameDrawThread_OnExit,      Original_DirectX_CGameDrawThread_Step,
                              Original_DirectX_CGameDrawThread_OnActivate};
    for (int i = 0; i < cases; ++i) {
        const Case c = MakeCase(ctx.rng(), target);
        const Outcome expected = Run(c, target, originals);
        const Outcome actual = Run(c, target, f);
        // (OnExit without a drawer leaves the caller's EAX; not compared then)
        const bool eaxDefined = target != Target::OnExit || c.hasDrawer;
        bool same = !eaxDefined || S3RE_EXPECT_EQ(expected.eax, actual.eax);
        same &= S3RE_EXPECT_MEM_EQ(&expected.fixture, &actual.fixture, sizeof(Fixture));
        same &= S3RE_EXPECT(expected.log == actual.log);
        same &= S3RE_EXPECT(expected.calls == actual.calls);
        same &= S3RE_EXPECT(expected.caught == actual.caught);
        same &= S3RE_EXPECT_EQ(expected.drawThread, actual.drawThread);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (target %d)", i, static_cast<int>(target));
            return;
        }
    }
}

// Wrong: the full redraw flag is cleared also when the frame was dropped.
std::uint32_t __fastcall WrongStep(void* self, void* edx) {
    const std::uint32_t result = DirectX_CGameDrawThread_Step(self, edx);
    AsThread(self)->full_redraw = 0;
    return result;
}

// Wrong: the cleanup also clears the drawer pointer.
std::uint32_t __fastcall WrongOnStart(void* self, void* edx) {
    struct Guard {
        static int Filter(GameDrawThread* thread) {
            thread->drawer = nullptr;
            return EXCEPTION_CONTINUE_SEARCH;
        }
    };
    __try {
        return DirectX_CGameDrawThread_OnStart(self, edx);
    } __except (Guard::Filter(AsThread(self))) {
    }
    return 0;
}

}  // namespace

S3RE_TEST(DirectX_CGameThread_Start_MatchesOriginal) { Compare(ctx, Target::Start, kOurs, 300); }
S3RE_TEST(DirectX_CGameThread_OnActivate_MatchesOriginal) { Compare(ctx, Target::GameOnActivate, kOurs, 300); }
S3RE_TEST(DirectX_CGameDrawThread_Construct_MatchesOriginal) { Compare(ctx, Target::Construct, kOurs, 200); }
S3RE_TEST(DirectX_CGameDrawThread_Destroy_MatchesOriginal) { Compare(ctx, Target::Destroy, kOurs, 200); }
S3RE_TEST(DirectX_CGameDrawThread_StartWithWindow_MatchesOriginal) {
    Compare(ctx, Target::StartWithWindow, kOurs, 200);
}
S3RE_TEST(DirectX_CGameDrawThread_OnStart_MatchesOriginal) { Compare(ctx, Target::OnStart, kOurs, 1000); }
S3RE_TEST(DirectX_CGameDrawThread_OnStart_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Functions wrong = kOurs;
    wrong.onStart = &WrongOnStart;
    Compare(probe, Target::OnStart, wrong, 1000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(DirectX_CGameDrawThread_OnExit_MatchesOriginal) { Compare(ctx, Target::OnExit, kOurs, 500); }
S3RE_TEST(DirectX_CGameDrawThread_Step_MatchesOriginal) { Compare(ctx, Target::Step, kOurs, 1000); }
S3RE_TEST(DirectX_CGameDrawThread_Step_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Functions wrong = kOurs;
    wrong.step = &WrongStep;
    Compare(probe, Target::Step, wrong, 1000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(DirectX_CGameDrawThread_OnActivate_MatchesOriginal) { Compare(ctx, Target::OnActivate, kOurs, 300); }

}  // namespace s3re::game
