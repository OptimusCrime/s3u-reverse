// CDrawThread (Dirk\Draw.cpp; re/ui_classes.md, "CDrawThread"): the
// in-game draw thread, a DirectX::CGameDrawThread (directx_draw_thread.cpp)
// that takes the draw data the logic thread prepares and draws it with its
// CDrawer.
//
// Layout (the full object): the CGameDrawThread part (0x54 bytes; +0x44
// the drawer), then +0x54 the CDirectDraw the drawer is built on (passed to
// CGameDrawer's constructor), +0x58 the
// shared draw-data slot (exchanged with InterlockedExchange), +0x5c the
// thread's own draw data, +0x60 the waitable the logic thread signals
// (slot 0: Wait(timeout, flag)), +0x64 the "frame taken" event, +0x68 no
// drawer (a debug switch from byte 0x7a8008), +0x69 initialized, +0x6c the
// wParam of the start message, +0x70 the lobby flag.
//
// CDrawThread::CDrawThread (0x0042c2d0): CGameDrawThread's constructor
//   (0x5b6530), vtables 0x656ab0 / 0x656aa4, +0x54..+0x64 = 0, the bytes
//   +0x68 / +0x69 / +0x70 = 0, CGameThread's +0x29 = 1. EAX = this.
// CDrawThread::Setup (0x0042c350): (slot, producer, frameDone, drawerArg,
//   noDrawer, startWParam, lobby) ret 1Ch: stores them (+0x58, +0x60, +0x64,
//   +0x54, byte +0x68, +0x6c, byte +0x70). EAX: frameDone with its low byte
//   replaced by lobby's (the registers it moved through).
// CDrawThread::OnExit (0x0042c670): vtable slot 6: the own draw data (if
//   any) destroyed (0x405a50) and freed (game operator delete), +0x5c = 0,
//   then CGameDrawThread::OnExit (0x5b6670). EAX = its.
// CDrawThread::CreateDrawer (0x0042c6a0): vtable slot 12: a CDrawer
//   (operator new 0x5f700, CDrawer::CDrawer 0x42c820 (thread, +0x54, the
//   lobby byte: pushed as ECX after `mov cl`, so its upper bytes are what
//   operator new left in ECX; the constructor reads only the byte; ours
//   passes zeros)) into +0x44, 0 when new fails; when the constructor
//   throws, the block is freed (the original's unwind action 0x633040).
//   EAX = the drawer.
// CDrawThread::Draw (0x0042c710): vtable slot 13, (fullRedraw) ret 4: slot
//   4 (OnStart) first if not initialized; with a drawer: FetchDrawData, then
//   the drawer's frame (0x4b26c0 (data, fullRedraw)) if there is draw data,
//   else DebugLog "DRAW: Kein Daten %i %i" (g_unk7dfd28, g_pathPhase);
//   without one (the debug switch): FetchDrawData, SignalFrameDone
//   (0x42c810), Sleep(60). EAX is not used (CGameDrawThread::Step returns
//   it, CGameThread::Main ignores it); ours: 0x4b26c0's, else 0.
// CDrawThread::FetchDrawData (0x0042c780): inside `try { } catch
//   (Win32::CTimeout* e) { DebugLog "DRAW: Kein Daten %i %i"; delete e
//   (slot 0 with 1, if not null); }`: waits on +0x60 (slot 0 (App::WaitTime
//   (3), 1)), then swaps its draw data with the shared slot
//   (InterlockedExchange(+0x58, +0x5c) into +0x5c). EAX: the exchange's;
//   after a timeout the continuation address 0x42c7cf.
//
// Known difference: CreateDrawer's unwind runs as a __finally (during the
// unwinding, as the original's).

#include "game/debug_log.h"
#include "game/globals.h"
#include "game/graphics/drawer_layout.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/catch_match.h"
#include "msvc6/game_heap.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/fake_win32.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kGameDrawThreadConstruct = 0x005b6530;  // DirectX::CGameDrawThread::CGameDrawThread
constexpr std::uintptr_t kGameDrawThreadOnExit = 0x005b6670;     // DirectX::CGameDrawThread::OnExit
constexpr std::uintptr_t kDrawDataDestroy = 0x00405a50;          // the draw data's destructor (thiscall, ret)
constexpr std::uintptr_t kDrawerConstruct = 0x0042c820;          // CDrawer::CDrawer(thread, arg, lobby) ret 0Ch
constexpr std::uintptr_t kDrawFrame = 0x004b26c0;                // CDrawer: draws a frame (data, fullRedraw) ret 8
constexpr std::uintptr_t kFetchDrawData = 0x0042c780;
constexpr std::uintptr_t kSignalFrameDone = 0x0042c810;
constexpr std::uintptr_t kWaitTime = 0x0040d0e0;                 // App::WaitTime (fastcall, ECX = kind)

constexpr std::uint32_t kDrawThreadVtable = vtables::kCDrawThread;
constexpr std::uint32_t kDrawThreadHandleVtable = vtables::kCDrawThread_sub4;
constexpr std::uint32_t kDrawerSize = sizeof(CDrawer);  // 0x5f700
constexpr std::uint32_t kDrawDataWaitKind = 3;      // App::WaitTime's kind for the draw data
constexpr std::uint32_t kNoDrawerSleep = 60;        // ms per step without a drawer
constexpr std::uint32_t kFetchContinuation = 0x42c7cf;  // EAX after FetchDrawData's catch
constexpr std::uint32_t kSlotOnStart = 4;

constexpr char kTimeoutName[] = ".PAVCTimeout@Win32@@";

#pragma pack(push, 1)
struct DrawThread {
    void* const* vtable;              // +0x00
    std::uint32_t handle_vtable;      // +0x04 the CWin32Handle part
    std::uint8_t unk08[0x29 - 0x08];
    std::uint8_t unk29;               // +0x29 CGameThread's, set to 1 by the constructor
    std::uint8_t unk2a[0x44 - 0x2a];
    void* drawer;                     // +0x44 the CDrawer
    std::uint8_t unk48[0x54 - 0x48];
    std::uint32_t drawer_arg;         // +0x54 a CDirectDraw for the drawer (CDrawer::CDrawer passes it to CGameDrawer)
    volatile std::int32_t* shared_slot;  // +0x58 where the logic thread puts the next draw data
    void* draw_data;                  // +0x5c the draw data being drawn
    void* producer;                   // +0x60 signalled when new draw data is there
    void* frame_done;                 // +0x64 the CEvent "draw data taken"
    std::uint8_t no_drawer;           // +0x68
    std::uint8_t initialized;         // +0x69
    std::uint8_t unk6a[2];
    std::uint32_t start_wparam;       // +0x6c
    std::uint8_t lobby;               // +0x70
};
#pragma pack(pop)
static_assert(offsetof(DrawThread, unk29) == 0x29);
static_assert(offsetof(DrawThread, drawer) == 0x44);
static_assert(offsetof(DrawThread, drawer_arg) == 0x54);
static_assert(offsetof(DrawThread, frame_done) == 0x64);
static_assert(offsetof(DrawThread, start_wparam) == 0x6c);
static_assert(offsetof(DrawThread, lobby) == 0x70);
static_assert(sizeof(DrawThread) == 0x71);

using MemberFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using WaitFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t timeout, std::uint32_t flag);
using WaitTimeFn = std::uint32_t(__fastcall*)(std::uint32_t kind);
using DrawerConstructFn = void*(__fastcall*)(void* self, void* edx, void* thread, std::uint32_t arg,
                                             std::uint32_t lobby);
using DrawFrameFn = std::uint32_t(__fastcall*)(void* self, void* edx, void* data, std::uint32_t fullRedraw);

DrawThread* AsThread(void* self) { return static_cast<DrawThread*>(self); }

void LogNoDrawData() { GameDebugLog(texts::kDrawNoData, g_unk7dfd28.get(), g_pathPhase.get()); }

int TimeoutFilter(const EXCEPTION_RECORD* record, void** caught) {
    const msvc6::CatchMatch match = msvc6::MatchPointerCatch(record, type_descriptor::kCTimeoutPointer, kTimeoutName);
    if (!match.matched) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    *caught = match.object;
    return EXCEPTION_EXECUTE_HANDLER;
}

// 0x42c7a0..0x42c7cc, FetchDrawData's try block: true when it ended normally.
bool FetchGuarded(DrawThread* thread, std::uint32_t* result, void** caught) {
    __try {
        const std::uint32_t timeout = reinterpret_cast<WaitTimeFn>(kWaitTime)(kDrawDataWaitKind);
        void* const producer = thread->producer;
        reinterpret_cast<WaitFn>((*static_cast<void* const* const*>(producer))[0])(producer, nullptr, timeout, 1);
        const std::int32_t previous = g_importInterlockedExchange.get()(
            thread->shared_slot, static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(thread->draw_data)));
        thread->draw_data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(previous));
        *result = static_cast<std::uint32_t>(previous);
        return true;
    } __except (TimeoutFilter(GetExceptionInformation()->ExceptionRecord, caught)) {
        return false;
    }
}

// 0x42c6e1: the constructor call; the block is freed when it throws.
void* ConstructDrawer(void* block, DrawThread* thread) {
    // (the original pushes ECX after `mov cl, [esi+0x70]`: its upper bytes
    // are what operator new left in ECX; the constructor reads the byte only)
    const std::uint32_t lobby = thread->lobby;
    bool constructed = false;
    void* drawer = nullptr;
    __try {
        drawer = reinterpret_cast<DrawerConstructFn>(kDrawerConstruct)(block, nullptr, thread, thread->drawer_arg,
                                                                       lobby);
        constructed = true;
    } __finally {
        if (!constructed) {
            msvc6::GameOperatorDelete(block);
        }
    }
    return drawer;
}

}  // namespace

void* __fastcall CDrawThread_Construct(void* self, void* edx);
std::uint32_t __fastcall CDrawThread_Setup(void* self, void* edx, void* slot, void* producer, void* frameDone,
                                           std::uint32_t drawerArg, std::uint32_t noDrawer,
                                           std::uint32_t startWParam, std::uint32_t lobby);
std::uint32_t __fastcall CDrawThread_OnExit(void* self, void* edx);
void* __fastcall CDrawThread_CreateDrawer(void* self, void* edx);
std::uint32_t __fastcall CDrawThread_Draw(void* self, void* edx, std::uint32_t fullRedraw);
std::uint32_t __fastcall CDrawThread_FetchDrawData(void* self, void* edx);

using ConstructFn = decltype(&CDrawThread_Construct);
using SetupFn = decltype(&CDrawThread_Setup);
using ThreadFn = decltype(&CDrawThread_OnExit);
using DrawFn = decltype(&CDrawThread_Draw);

S3RE_HOOK(0x0042c2d0, CDrawThread_Construct, ConstructFn, Original_CDrawThread_Construct, CDrawThread_Construct);
S3RE_HOOK(0x0042c350, CDrawThread_Setup, SetupFn, Original_CDrawThread_Setup, CDrawThread_Setup);
S3RE_HOOK(0x0042c670, CDrawThread_OnExit, ThreadFn, Original_CDrawThread_OnExit, CDrawThread_OnExit);
S3RE_HOOK(0x0042c6a0, CDrawThread_CreateDrawer, ConstructFn, Original_CDrawThread_CreateDrawer,
          CDrawThread_CreateDrawer);
S3RE_HOOK(0x0042c710, CDrawThread_Draw, DrawFn, Original_CDrawThread_Draw, CDrawThread_Draw);
S3RE_HOOK(0x0042c780, CDrawThread_FetchDrawData, ThreadFn, Original_CDrawThread_FetchDrawData,
          CDrawThread_FetchDrawData);

// Purpose: constructor of the in-game draw thread.
void* __fastcall CDrawThread_Construct(void* self, void* /*edx*/) {
    reinterpret_cast<MemberFn>(kGameDrawThreadConstruct)(self, nullptr);
    DrawThread* const thread = AsThread(self);
    thread->vtable = reinterpret_cast<void* const*>(kDrawThreadVtable);
    thread->drawer_arg = 0;
    thread->shared_slot = nullptr;
    thread->draw_data = nullptr;
    thread->producer = nullptr;
    thread->frame_done = nullptr;
    thread->no_drawer = 0;
    thread->initialized = 0;
    thread->lobby = 0;
    thread->handle_vtable = kDrawThreadHandleVtable;
    thread->unk29 = 1;
    return self;
}

// Purpose: hands the draw thread the objects it shares with the logic
// thread (the draw-data slot and the two events) and its options.
std::uint32_t __fastcall CDrawThread_Setup(void* self, void* /*edx*/, void* slot, void* producer, void* frameDone,
                                           std::uint32_t drawerArg, std::uint32_t noDrawer,
                                           std::uint32_t startWParam, std::uint32_t lobby) {
    DrawThread* const thread = AsThread(self);
    thread->shared_slot = static_cast<volatile std::int32_t*>(slot);
    thread->producer = producer;
    thread->frame_done = frameDone;
    thread->drawer_arg = drawerArg;
    thread->no_drawer = static_cast<std::uint8_t>(noDrawer);
    thread->start_wparam = startWParam;
    thread->lobby = static_cast<std::uint8_t>(lobby);
    return (reinterpret_cast<std::uint32_t>(frameDone) & 0xffffff00u) | thread->lobby;
}

// Purpose: the draw thread's shutdown: frees its draw data, then the
// framework part's shutdown (the drawer).
std::uint32_t __fastcall CDrawThread_OnExit(void* self, void* /*edx*/) {
    DrawThread* const thread = AsThread(self);
    void* const data = thread->draw_data;
    if (data != nullptr) {
        reinterpret_cast<MemberFn>(kDrawDataDestroy)(data, nullptr);
        msvc6::GameOperatorDelete(data);
    }
    thread->draw_data = nullptr;
    return reinterpret_cast<MemberFn>(kGameDrawThreadOnExit)(self, nullptr);
}

// Purpose: creates the thread's drawer (the in-game CDrawer).
void* __fastcall CDrawThread_CreateDrawer(void* self, void* /*edx*/) {
    DrawThread* const thread = AsThread(self);
    void* const block = msvc6::GameOperatorNew(kDrawerSize);
    if (block == nullptr) {
        thread->drawer = nullptr;
        return nullptr;
    }
    thread->drawer = ConstructDrawer(block, thread);
    return thread->drawer;
}

// Purpose: one frame of the draw thread: takes the newest draw data and
// draws it (or, without a drawer, only takes it).
std::uint32_t __fastcall CDrawThread_Draw(void* self, void* /*edx*/, std::uint32_t fullRedraw) {
    DrawThread* const thread = AsThread(self);
    if (thread->initialized == 0) {
        reinterpret_cast<MemberFn>(thread->vtable[kSlotOnStart])(self, nullptr);
    }
    if (thread->no_drawer != 0) {
        reinterpret_cast<MemberFn>(kFetchDrawData)(self, nullptr);
        reinterpret_cast<MemberFn>(kSignalFrameDone)(self, nullptr);
        g_importSleep.get()(kNoDrawerSleep);
        return 0;
    }
    reinterpret_cast<MemberFn>(kFetchDrawData)(self, nullptr);
    if (thread->draw_data != nullptr) {
        return reinterpret_cast<DrawFrameFn>(kDrawFrame)(thread->drawer, nullptr, thread->draw_data, fullRedraw);
    }
    LogNoDrawData();
    return 0;
}

// Purpose: waits (with a timeout) for the logic thread's next draw data
// and takes it, leaving the old one in the shared slot for refilling.
std::uint32_t __fastcall CDrawThread_FetchDrawData(void* self, void* /*edx*/) {
    std::uint32_t result = 0;
    void* caught = nullptr;
    if (FetchGuarded(AsThread(self), &result, &caught)) {
        return result;
    }
    LogNoDrawData();
    if (caught != nullptr) {
        msvc6::DeleteCaughtException(caught);
    }
    return kFetchContinuation;
}

// ---------------------------------------------------------------------------
// Tests: a thread image (random bytes; vtable slot 4 a recording fake), the
// callees as recording stubs (the drawer's constructor may throw, the
// producer's Wait (a fake object) may throw a CTimeout or another game
// exception), Sleep's and InterlockedExchange's imports recording fakes,
// DebugLog captured. Draw data and drawer blocks are real game-heap
// blocks (freed by the code under test or afterwards); their addresses
// are recorded as "the block".
// ---------------------------------------------------------------------------

namespace {

constexpr std::uintptr_t kCxxThrowException = 0x0061f027;
constexpr std::uint32_t kBlockTag = 0xb10c0000u;

struct Script {
    test::Rng rng{0};
    std::vector<std::uint32_t> log;
    std::uint32_t base = 0;
    std::uint32_t block = 0;  // the heap block of this run
    std::uintptr_t throwInfo = 0;  // thrown by the producer's Wait / the drawer's constructor
    void* thrownObject = nullptr;
};
Script* g_script = nullptr;

std::uint32_t Rel(std::uint32_t address) {
    if (address == g_script->block && address != 0) {
        return kBlockTag;
    }
    return address - g_script->base;
}

void MaybeThrow() {
    if (g_script->throwInfo == 0) {
        return;
    }
    void* object = g_script->thrownObject;
    using ThrowFn = void(__stdcall*)(void* object, std::uintptr_t info);
    reinterpret_cast<ThrowFn>(kCxxThrowException)(&object, g_script->throwInfo);
}

std::uint32_t __fastcall FakeOnStart(void* self, void*) {
    g_script->log.insert(g_script->log.end(), {0x0545u, Rel(reinterpret_cast<std::uint32_t>(self))});
    return g_script->rng.Next32();
}
std::uint32_t __fastcall FakeWait(void* self, void*, std::uint32_t timeout, std::uint32_t flag) {
    g_script->log.insert(g_script->log.end(), {0xa17u, Rel(reinterpret_cast<std::uint32_t>(self)), timeout, flag});
    MaybeThrow();
    return g_script->rng.Next32();
}
std::uint32_t __fastcall FakeExceptionDelete(void* self, void*, std::uint32_t flags) {
    g_script->log.insert(g_script->log.end(), {0xe0de1u, Rel(reinterpret_cast<std::uint32_t>(self)), flags});
    return 0;
}
void __stdcall FakeSleep(std::uint32_t milliseconds) {
    g_script->log.insert(g_script->log.end(), {0x51ee9u, milliseconds});
}
std::int32_t __stdcall FakeExchange(volatile std::int32_t* target, std::int32_t value) {
    // (the value is the fixture's random draw-data pointer: recorded as it is)
    g_script->log.insert(g_script->log.end(), {0xec5u, Rel(reinterpret_cast<std::uint32_t>(target)),
                                               static_cast<std::uint32_t>(value)});
    return static_cast<std::int32_t>(g_script->rng.Next32());
}

struct Fixture {
    DrawThread thread;
    std::uint8_t threadRest[0x20];
    const void* producerVtable;
    const void* exceptionVtable;
    std::uint8_t exceptionRest[0x1c];  // (CatchGameException reads a CHResult's code at +0x18)
    std::int32_t slot;
};

enum class Target { Construct, Setup, OnExit, CreateDrawer, Draw, FetchDrawData };

struct Functions {
    ConstructFn construct;
    SetupFn setup;
    ThreadFn onExit;
    ConstructFn createDrawer;
    DrawFn draw;
    ThreadFn fetchDrawData;
};

struct Case {
    Fixture fixture;
    std::uint64_t seed;
    std::uint32_t args[7];
    bool hasData;
    std::uintptr_t throwInfo;
};

struct Outcome {
    std::uint32_t eax = 0;
    Fixture fixture;
    std::vector<std::uint32_t> log;
    std::vector<test::StubCall> calls;
    std::vector<DebugLogCall> logs;
    test::CaughtException caught;
};

struct Guarded {
    const Functions* f;
    Target target;
    void* thread;
    const std::uint32_t* args;
};

std::uint32_t CallTarget(const Guarded* g) {
    const std::uint32_t* a = g->args;
    switch (g->target) {
        case Target::Construct: return Rel(reinterpret_cast<std::uint32_t>(g->f->construct(g->thread, nullptr)));
        case Target::Setup:
            return g->f->setup(g->thread, nullptr, reinterpret_cast<void*>(a[0]), reinterpret_cast<void*>(a[1]),
                               reinterpret_cast<void*>(a[2]), a[3], a[4], a[5], a[6]);
        case Target::OnExit: return g->f->onExit(g->thread, nullptr);
        case Target::CreateDrawer: return Rel(reinterpret_cast<std::uint32_t>(g->f->createDrawer(g->thread, nullptr)));
        case Target::Draw: g->f->draw(g->thread, nullptr, a[0]); return 0;  // (EAX not used, see above)
        case Target::FetchDrawData: return g->f->fetchDrawData(g->thread, nullptr);
    }
    return 0;
}

void GuardedCall(const Guarded* g, std::uint32_t* eax, test::CaughtException* caught) {
    __try {
        *eax = CallTarget(g);
    } __except (test::CatchGameException(GetExceptionInformation(), caught)) {
    }
}

Outcome Run(const Case& c, Target target, const Functions& f) {
    Outcome out;
    Script script;
    script.rng = test::Rng(c.seed);
    script.throwInfo = c.throwInfo;
    g_script = &script;
    out.fixture = c.fixture;
    Fixture& fx = out.fixture;
    script.base = reinterpret_cast<std::uint32_t>(&fx);
    script.thrownObject = &fx.exceptionVtable;

    static void* threadVtable[16] = {};
    threadVtable[kSlotOnStart] = reinterpret_cast<void*>(&FakeOnStart);
    static void* producerVtable[1] = {reinterpret_cast<void*>(&FakeWait)};
    static void* exceptionVtable[1] = {reinterpret_cast<void*>(&FakeExceptionDelete)};
    fx.thread.vtable = threadVtable;
    fx.producerVtable = producerVtable;
    fx.exceptionVtable = exceptionVtable;
    fx.thread.producer = &fx.producerVtable;
    fx.thread.shared_slot = &fx.slot;
    if (target == Target::OnExit) {
        fx.thread.draw_data = c.hasData ? msvc6::GameOperatorNew(16) : nullptr;
        script.block = reinterpret_cast<std::uint32_t>(fx.thread.draw_data);
    }

    const void* sleep = reinterpret_cast<const void*>(&FakeSleep);
    const void* exchange = reinterpret_cast<const void*>(&FakeExchange);
    test::PatchBytes(g_importSleep.address(), &sleep, sizeof(sleep));
    test::PatchBytes(g_importInterlockedExchange.address(), &exchange, sizeof(exchange));
    {
        test::CalleeStubs stubs;
        stubs.SetWorld(reinterpret_cast<CGameData*>(&fx));
        auto value = [&](CGameData*, std::uint32_t, const std::uint32_t*) { return script.rng.Next32(); };
        stubs.Install<0, kGameDrawThreadConstruct>(value);
        stubs.Install<0, kGameDrawThreadOnExit>(value);
        stubs.Install<0, kDrawDataDestroy>(value);
        stubs.Install<3, kDrawerConstruct>([&](CGameData*, std::uint32_t self, const std::uint32_t*) {
            script.block = self + script.base;
            MaybeThrow();
            return self + script.base;
        });
        stubs.Install<2, kDrawFrame>(value);
        stubs.Install<0, kSignalFrameDone>(value);
        stubs.Install<0, kWaitTime>([&](CGameData*, std::uint32_t, const std::uint32_t*) {
            return script.rng.Below(5000);
        });
        if (target == Target::Draw) {
            stubs.Install<0, kFetchDrawData>([&](CGameData*, std::uint32_t, const std::uint32_t*) {
                // (FetchDrawData's effect: sometimes no draw data)
                fx.thread.draw_data = script.rng.Chance(1, 4) ? nullptr : reinterpret_cast<void*>(0xda7a);
                return script.rng.Next32();
            });
        }
        DebugLogRecorder logs;
        const Guarded g{&f, target, &fx.thread, c.args};
        GuardedCall(&g, &out.eax, &out.caught);
        out.logs = logs.Take();
        out.calls = stubs.Take();
    }
    test::UnpatchBytes(g_importInterlockedExchange.address());
    test::UnpatchBytes(g_importSleep.address());
    for (test::StubCall& call : out.calls) {
        if (call.callee == kDrawerConstruct) {
            call.self = kBlockTag;
            call.args[0] -= script.base;                  // the thread
            call.args[2] &= 0xff;  // the lobby byte (the upper bytes: ECX as operator new left it)
        } else if (call.self + script.base == script.block && script.block != 0) {
            call.self = kBlockTag;
        } else if (call.callee == kWaitTime || call.callee == kDrawFrame) {
            call.self += script.base;  // (ECX: the wait kind; the drawer pointer, random)
        }
    }
    if (target == Target::CreateDrawer && out.caught.threw == false && script.block != 0) {
        msvc6::GameOperatorDelete(reinterpret_cast<void*>(script.block));  // (the stub constructed nothing)
    }
    auto unpoint = [&](auto& field) {
        const auto value = reinterpret_cast<std::uint32_t>(field);
        if (value != 0 && value == script.block) {
            field = reinterpret_cast<std::remove_reference_t<decltype(field)>>(kBlockTag);
        } else if (value - script.base < sizeof(Fixture)) {
            field = reinterpret_cast<std::remove_reference_t<decltype(field)>>(0xf1000000u + (value - script.base));
        }
    };
    unpoint(fx.thread.drawer);
    unpoint(fx.thread.draw_data);
    unpoint(fx.thread.producer);
    unpoint(fx.thread.shared_slot);
    unpoint(fx.thread.frame_done);
    if (fx.thread.vtable == threadVtable) {
        fx.thread.vtable = nullptr;
    }
    fx.producerVtable = nullptr;
    fx.exceptionVtable = nullptr;
    if (out.eax == script.block && out.eax != 0) {
        out.eax = kBlockTag;
    }
    out.log = script.log;
    g_script = nullptr;
    return out;
}

Case MakeCase(test::Rng& rng, Target target) {
    Case c;
    rng.Fill(&c.fixture, sizeof(c.fixture));
    c.seed = rng.Next64();
    for (std::uint32_t& a : c.args) {
        a = rng.Next32();
    }
    c.args[0] = rng.Chance(1, 2) ? rng.Below(2) : c.args[0];
    c.hasData = rng.Chance(2, 3);
    c.fixture.thread.no_drawer = static_cast<std::uint8_t>(rng.Chance(2, 3) ? 0 : rng.Below(256));
    c.fixture.thread.initialized = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 1 : rng.Below(2));
    c.throwInfo = 0;
    if ((target == Target::FetchDrawData || target == Target::CreateDrawer) && rng.Chance(1, 3)) {
        static constexpr std::uintptr_t kInfos[] = {throw_info::kCTimeoutPointer, throw_info::kCTimeoutPointer,
                                                    throw_info::kCHResultPointer, throw_info::kAppNotActiveException};
        c.throwInfo = kInfos[rng.Below(4)];
    }
    return c;
}

constexpr Functions kOurs{CDrawThread_Construct, CDrawThread_Setup, CDrawThread_OnExit,
                          CDrawThread_CreateDrawer, CDrawThread_Draw, CDrawThread_FetchDrawData};

void Compare(test::Context& ctx, Target target, const Functions& f, int cases) {
    const Functions originals{Original_CDrawThread_Construct, Original_CDrawThread_Setup,
                              Original_CDrawThread_OnExit,    Original_CDrawThread_CreateDrawer,
                              Original_CDrawThread_Draw,      Original_CDrawThread_FetchDrawData};
    const std::int32_t saved7dfd28 = g_unk7dfd28;
    for (int i = 0; i < cases; ++i) {
        const Case c = MakeCase(ctx.rng(), target);
        g_unk7dfd28 = static_cast<std::int32_t>(ctx.rng().Next32());
        const Outcome expected = Run(c, target, originals);
        const Outcome actual = Run(c, target, f);
        bool same = S3RE_EXPECT_EQ(expected.eax, actual.eax);
        same &= S3RE_EXPECT_MEM_EQ(&expected.fixture, &actual.fixture, sizeof(Fixture));
        same &= S3RE_EXPECT(expected.log == actual.log);
        same &= S3RE_EXPECT(expected.calls == actual.calls);
        same &= S3RE_EXPECT(expected.logs == actual.logs);
        same &= S3RE_EXPECT(expected.caught == actual.caught);
        if (!same) {
            for (const Outcome* o : {&expected, &actual}) {
                for (const test::StubCall& call : o->calls) {
                    ctx.Fail(__FILE__, __LINE__, "  call %x self %x args %x %x %x", call.callee, call.self,
                             call.args[0], call.args[1], call.args[2]);
                }
                for (std::uint32_t v : o->log) {
                    ctx.Fail(__FILE__, __LINE__, "  log %x", v);
                }
            }
            ctx.Fail(__FILE__, __LINE__, "  case %d (target %d)", i, static_cast<int>(target));
            break;
        }
    }
    g_unk7dfd28 = saved7dfd28;
}

// Wrong: the timeout's DebugLog is skipped.
std::uint32_t __fastcall WrongFetchDrawData(void* self, void* /*edx*/) {
    std::uint32_t result = 0;
    void* caught = nullptr;
    if (FetchGuarded(AsThread(self), &result, &caught)) {
        return result;
    }
    if (caught != nullptr) {
        msvc6::DeleteCaughtException(caught);
    }
    return kFetchContinuation;
}

// Wrong: OnStart is called even when the thread is initialized.
std::uint32_t __fastcall WrongDraw(void* self, void* edx, std::uint32_t fullRedraw) {
    DrawThread* const thread = AsThread(self);
    if (thread->initialized != 0) {
        reinterpret_cast<MemberFn>(thread->vtable[kSlotOnStart])(self, nullptr);
    }
    return CDrawThread_Draw(self, edx, fullRedraw);
}

}  // namespace

S3RE_TEST(CDrawThread_Construct_MatchesOriginal) { Compare(ctx, Target::Construct, kOurs, 200); }
S3RE_TEST(CDrawThread_Setup_MatchesOriginal) { Compare(ctx, Target::Setup, kOurs, 300); }
S3RE_TEST(CDrawThread_OnExit_MatchesOriginal) { Compare(ctx, Target::OnExit, kOurs, 300); }
S3RE_TEST(CDrawThread_CreateDrawer_MatchesOriginal) { Compare(ctx, Target::CreateDrawer, kOurs, 300); }
S3RE_TEST(CDrawThread_Draw_MatchesOriginal) { Compare(ctx, Target::Draw, kOurs, 1000); }
S3RE_TEST(CDrawThread_Draw_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Functions wrong = kOurs;
    wrong.draw = &WrongDraw;
    Compare(probe, Target::Draw, wrong, 300);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CDrawThread_FetchDrawData_MatchesOriginal) { Compare(ctx, Target::FetchDrawData, kOurs, 1000); }
S3RE_TEST(CDrawThread_FetchDrawData_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Functions wrong = kOurs;
    wrong.fetchDrawData = &WrongFetchDrawData;
    Compare(probe, Target::FetchDrawData, wrong, 300);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
