// Small members of the framework's application, window and waitable
// classes (Win32\Classes.cpp, Win32\Synchronize.cpp, DirectX\Gamebase.cpp;
// RTTI names). All thiscall unless said otherwise.
//
// Window constructors, each the base's constructor and then its own vtable;
// EAX = this:
//   Win32::CWindow::CWindow (0x0060fe50): CWindowBase 0x60f960, 0x65d85c.
//   Win32::CMainWindow::CMainWindow (0x006100a0): CWindow, 0x65d870.
//   DirectX::CDirectXMainWindow (0x005b8fc0): CMainWindow, 0x65cc30.
//   DirectX::CGameMainWindow (0x005b5cf0): CDirectXMainWindow, 0x65c9c8,
//     and the game's main window pointer 0x7e8be8 = this.
// Win32::CSemaphore::CSemaphore (0x0060c770) / CMutex::CMutex (0x0060c9e0):
//   CWaitable(0, 0) 0x60bf50, then the vtables (0x65d6ac / +4 0x65d6a0;
//   0x65d6c8 / +4 0x65d6bc). EAX = this.
// Win32::CWin32Waitable::IsSame (0x0060c500): (other) ret 4: EAX = this ==
//   other.
// Win32::GetTimeout (0x0060df50) / DirectX::GetTimeout (0x005b8c10):
//   __fastcall (ECX = kind): the framework's / the DirectX layer's wait
//   time of a kind from a table of {normal, -debug} pairs (0x7a42b4: 5000 /
//   INFINITE, 30000 / INFINITE; 0x79e6f0: 25000 / INFINITE, 1000 / 1000),
//   the second column with the -debug switch (0x7e916c). Kinds past the two
//   entries read the data behind the tables (callers pass 0 and 1).
// Win32::CApplication::~CApplication (0x0060ed50, this = the object + 0x14):
//   the object's vtable 0x65d78c (this-0x14), the virtual base's two vtables
//   0x65d764 / 0x65d758 (through the vbptr this-0x10, displacement re-read
//   for each store) and its vtordisp (displacement - 0xc, just before it);
//   then the application pointer 0x7e9168 = 0. EAX = the displacement.
// Win32::CMessageQueue::OnIdle (0x0060d720), Win32::CApplication::Start
//   (0x0060ed90), Win32::CWindowedApp::Start (0x0060f860): only an assert
//   (Win32::ThrowAssertmentFailed 0x6119b0): "A nearly pure virtual
//   function" (synchronize.ipp line 550) / "Applications are created by
//   running them. This one is running. So what?" (Classes.cpp lines 274,
//   432).

#include "game/framework/framework_layout.h"
#include "game/globals.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kWindowBaseConstruct = 0x0060f960;  // Win32::CWindowBase::CWindowBase (ours)
constexpr std::uintptr_t kWaitableConstruct = 0x0060bf50;    // Win32::CWaitable::CWaitable (ours)
constexpr std::uintptr_t kThrowAssertmentFailed = 0x006119b0;  // Win32::ThrowAssertmentFailed (ours)

constexpr std::uint32_t kLineMessageQueueOnIdle = 550;  // Win32\synchronize.ipp
constexpr std::uint32_t kLineApplicationStart = 274;    // Win32\Classes.cpp
constexpr std::uint32_t kLineWindowedAppStart = 432;

using CtorFn = void*(__fastcall*)(void* self, void* edx);
using Ctor2Fn = void*(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b);
using AssertFn = void(__fastcall*)(const char* expression, const char* file, std::int32_t line);

std::uint32_t& Dword(void* self, std::ptrdiff_t offset) {  // raw-ok: vtable slots of the derived objects
    return *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(self) + offset);
}

void* ConstructWindowPart(std::uintptr_t baseConstructor, void* self, std::uint32_t vtable) {
    reinterpret_cast<CtorFn>(baseConstructor)(self, nullptr);
    Dword(self, 0) = vtable;
    return self;
}

// The timeout tables: kind k's {normal, debug} pair at table + 8k.
std::uint32_t Timeout(std::uintptr_t table, std::uint32_t kind) {
    const std::uint32_t column = g_debugSwitch != 0 ? 1 : 0;
    return reinterpret_cast<const std::uint32_t*>(table + kind * 8)[column];  // raw-ok: reads past for kinds > 1
}

void Assert(const GameText& expression, const GameText& file, std::uint32_t line) {
    reinterpret_cast<AssertFn>(kThrowAssertmentFailed)(expression, file, static_cast<std::int32_t>(line));
}

}  // namespace

using IsSameFn = std::uint32_t(__fastcall*)(void* self, void* edx, void* other);
using TimeoutFn = std::uint32_t(__fastcall*)(std::uint32_t kind);
using DisplacementFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using VoidMemberFn = void(__fastcall*)(void* self, void* edx);

void* __fastcall Win32_CWindow_Construct(void* self, void* edx);
void* __fastcall Win32_CMainWindow_Construct(void* self, void* edx);
void* __fastcall DirectX_CDirectXMainWindow_Construct(void* self, void* edx);
void* __fastcall DirectX_CGameMainWindow_Construct(void* self, void* edx);
void* __fastcall Win32_CSemaphore_Construct(void* self, void* edx);
void* __fastcall Win32_CMutex_Construct(void* self, void* edx);
std::uint32_t __fastcall Win32_CWin32Waitable_IsSame(void* self, void* edx, void* other);
std::uint32_t __fastcall Win32_GetTimeout(std::uint32_t kind);
std::uint32_t __fastcall DirectX_GetTimeout(std::uint32_t kind);
std::uint32_t __fastcall Win32_CApplication_Destroy(void* self, void* edx);
void __fastcall Win32_CMessageQueue_OnIdle(void* self, void* edx);
void __fastcall Win32_CApplication_Start(void* self, void* edx);
void __fastcall Win32_CWindowedApp_Start(void* self, void* edx);

S3RE_HOOK(0x0060fe50, Win32_CWindow_Construct, CtorFn, Original_Win32_CWindow_Construct, Win32_CWindow_Construct);
S3RE_HOOK(0x006100a0, Win32_CMainWindow_Construct, CtorFn, Original_Win32_CMainWindow_Construct,
          Win32_CMainWindow_Construct);
S3RE_HOOK(0x005b8fc0, DirectX_CDirectXMainWindow_Construct, CtorFn, Original_DirectX_CDirectXMainWindow_Construct,
          DirectX_CDirectXMainWindow_Construct);
S3RE_HOOK(0x005b5cf0, DirectX_CGameMainWindow_Construct, CtorFn, Original_DirectX_CGameMainWindow_Construct,
          DirectX_CGameMainWindow_Construct);
S3RE_HOOK(0x0060c770, Win32_CSemaphore_Construct, CtorFn, Original_Win32_CSemaphore_Construct,
          Win32_CSemaphore_Construct);
S3RE_HOOK(0x0060c9e0, Win32_CMutex_Construct, CtorFn, Original_Win32_CMutex_Construct, Win32_CMutex_Construct);
S3RE_HOOK(0x0060c500, Win32_CWin32Waitable_IsSame, IsSameFn, Original_Win32_CWin32Waitable_IsSame,
          Win32_CWin32Waitable_IsSame);
S3RE_HOOK(0x0060df50, Win32_GetTimeout, TimeoutFn, Original_Win32_GetTimeout, Win32_GetTimeout);
S3RE_HOOK(0x005b8c10, DirectX_GetTimeout, TimeoutFn, Original_DirectX_GetTimeout, DirectX_GetTimeout);
S3RE_HOOK(0x0060ed50, Win32_CApplication_Destroy, DisplacementFn, Original_Win32_CApplication_Destroy,
          Win32_CApplication_Destroy);
S3RE_HOOK(0x0060d720, Win32_CMessageQueue_OnIdle, VoidMemberFn, Original_Win32_CMessageQueue_OnIdle,
          Win32_CMessageQueue_OnIdle);
S3RE_HOOK(0x0060ed90, Win32_CApplication_Start, VoidMemberFn, Original_Win32_CApplication_Start,
          Win32_CApplication_Start);
S3RE_HOOK(0x0060f860, Win32_CWindowedApp_Start, VoidMemberFn, Original_Win32_CWindowedApp_Start,
          Win32_CWindowedApp_Start);

// Purpose: Constructor of a framework window (no window handle yet).
void* __fastcall Win32_CWindow_Construct(void* self, void* /*edx*/) {
    return ConstructWindowPart(kWindowBaseConstruct, self, vtables::kCWindow);
}

// Purpose: Constructor of the framework's main window class.
void* __fastcall Win32_CMainWindow_Construct(void* self, void* /*edx*/) {
    return ConstructWindowPart(0x0060fe50, self, vtables::kCMainWindow);
}

// Purpose: Constructor of the DirectX layer's main window class.
void* __fastcall DirectX_CDirectXMainWindow_Construct(void* self, void* /*edx*/) {
    return ConstructWindowPart(0x006100a0, self, vtables::kCDirectXMainWindow);
}

// Purpose: Constructor of the game's main window; remembers it globally.
void* __fastcall DirectX_CGameMainWindow_Construct(void* self, void* /*edx*/) {
    ConstructWindowPart(0x005b8fc0, self, vtables::kCGameMainWindow);
    g_gameMainWindow = static_cast<std::uint8_t*>(self);
    return self;
}

// Purpose: Constructor of a semaphore wrapper (no Windows object yet).
void* __fastcall Win32_CSemaphore_Construct(void* self, void* /*edx*/) {
    reinterpret_cast<Ctor2Fn>(kWaitableConstruct)(self, nullptr, 0, 0);
    Dword(self, 0) = vtables::kCSemaphore;
    Dword(self, 4) = vtables::kCSemaphore_sub4;
    return self;
}

// Purpose: Constructor of a mutex wrapper (no Windows object yet).
void* __fastcall Win32_CMutex_Construct(void* self, void* /*edx*/) {
    reinterpret_cast<Ctor2Fn>(kWaitableConstruct)(self, nullptr, 0, 0);
    Dword(self, 0) = vtables::kCMutex;
    Dword(self, 4) = vtables::kCMutex_sub4;
    return self;
}

// Purpose: Whether two waitable objects are the same object (used when
// waiting for one of several).
std::uint32_t __fastcall Win32_CWin32Waitable_IsSame(void* self, void* /*edx*/, void* other) {
    return self == other ? 1 : 0;
}

// Purpose: The framework's wait times (ms) for its thread waits.
std::uint32_t __fastcall Win32_GetTimeout(std::uint32_t kind) {
    return Timeout(g_win32Timeouts.address(), kind);
}

// Purpose: The DirectX layer's wait times (ms) for its thread waits.
std::uint32_t __fastcall DirectX_GetTimeout(std::uint32_t kind) {
    return Timeout(g_directXTimeouts.address(), kind);
}

// Purpose: Destructor of the framework's application object: restores the
// base vtables and forgets the global application pointer.
std::uint32_t __fastcall Win32_CApplication_Destroy(void* self, void* /*edx*/) {
    auto displacement = [self]() {
        return reinterpret_cast<const std::int32_t*>(Dword(self, -0x10))[1];
    };
    Dword(self, -0x14) = vtables::kCApplication;
    Dword(self, -0x10 + displacement()) = vtables::kCApplication_vbase;
    Dword(self, -0xc + displacement()) = vtables::kCApplication_vbase4;
    const std::int32_t last = displacement();
    Dword(self, -0x14 + last) = static_cast<std::uint32_t>(last - 0xc);  // the vtordisp
    g_app = nullptr;
    return static_cast<std::uint32_t>(last);
}

// Purpose: A message queue's idle handler that derived classes must
// replace (asserts).
void __fastcall Win32_CMessageQueue_OnIdle(void* /*self*/, void* /*edx*/) {
    Assert(texts::kNearlyPureVirtual, texts::kSynchronizeFile, kLineMessageQueueOnIdle);
}

// Purpose: Asserts: an application starts by running it, not by Start.
void __fastcall Win32_CApplication_Start(void* /*self*/, void* /*edx*/) {
    Assert(texts::kApplicationsAreRun, texts::kClassesFile, kLineApplicationStart);
}

// Purpose: Asserts (the windowed application's copy of the above).
void __fastcall Win32_CWindowedApp_Start(void* /*self*/, void* /*edx*/) {
    Assert(texts::kApplicationsAreRun, texts::kClassesFile, kLineWindowedAppStart);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Each operation on a random 0x60-byte object with guard bytes (this 0x20
// in, so ~CApplication's negative offsets stay inside; its vbtable lies in
// the buffer), the same for both versions. The base constructors run as
// they are (ours, verified); the assert is replaced by a recorder that
// returns. The globals the functions write (the main window and the
// application pointer) and -debug are set before and restored after each
// case. Compared: EAX (as an offset when it points into the buffer), the
// buffer, the written globals, the assert calls.

namespace {

constexpr std::size_t kBytes = 0x60;
constexpr std::size_t kThis = 0x20;

struct AssertCall {
    std::uint32_t expression, file, line;
    bool operator==(const AssertCall& o) const {
        return expression == o.expression && file == o.file && line == o.line;
    }
};
std::vector<AssertCall>* g_asserts = nullptr;

void __fastcall AssertRecorder(const char* expression, const char* file, std::int32_t line) {
    g_asserts->push_back(AssertCall{static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(expression)),
                                    static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(file)),
                                    static_cast<std::uint32_t>(line)});
}

struct Functions {
    CtorFn window, mainWindow, directXWindow, gameWindow, semaphore, mutex;
    IsSameFn isSame;
    TimeoutFn win32Timeout, directXTimeout;
    DisplacementFn applicationDestroy;
    VoidMemberFn onIdle, applicationStart, windowedStart;
};

Functions Originals() {
    return {Original_Win32_CWindow_Construct,     Original_Win32_CMainWindow_Construct,
            Original_DirectX_CDirectXMainWindow_Construct, Original_DirectX_CGameMainWindow_Construct,
            Original_Win32_CSemaphore_Construct,  Original_Win32_CMutex_Construct,
            Original_Win32_CWin32Waitable_IsSame, Original_Win32_GetTimeout,
            Original_DirectX_GetTimeout,          Original_Win32_CApplication_Destroy,
            Original_Win32_CMessageQueue_OnIdle,  Original_Win32_CApplication_Start,
            Original_Win32_CWindowedApp_Start};
}

Functions Ours() {
    return {&Win32_CWindow_Construct,     &Win32_CMainWindow_Construct, &DirectX_CDirectXMainWindow_Construct,
            &DirectX_CGameMainWindow_Construct, &Win32_CSemaphore_Construct, &Win32_CMutex_Construct,
            &Win32_CWin32Waitable_IsSame, &Win32_GetTimeout,           &DirectX_GetTimeout,
            &Win32_CApplication_Destroy,  &Win32_CMessageQueue_OnIdle, &Win32_CApplication_Start,
            &Win32_CWindowedApp_Start};
}

struct Outcome {
    std::uint32_t eax = 0;
    std::uint8_t bytes[kBytes] = {};
    std::uint32_t mainWindow = 0;
    std::uint32_t app = 0;
    std::vector<AssertCall> asserts;
};

std::uint32_t RunOp(const Functions& f, int op, std::uint8_t* self, std::uint32_t arg) {
    auto pointer = [](void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); };
    switch (op) {
        case 0: return pointer(f.window(self, nullptr));
        case 1: return pointer(f.mainWindow(self, nullptr));
        case 2: return pointer(f.directXWindow(self, nullptr));
        case 3: return pointer(f.gameWindow(self, nullptr));
        case 4: return pointer(f.semaphore(self, nullptr));
        case 5: return pointer(f.mutex(self, nullptr));
        case 6: return f.isSame(self, nullptr, (arg & 1) != 0 ? self : reinterpret_cast<void*>(arg));
        case 7: return f.win32Timeout(arg & 1);
        case 8: return f.directXTimeout(arg & 1);
        case 9: return f.applicationDestroy(self, nullptr);
        case 10: f.onIdle(self, nullptr); return 0;
        case 11: f.applicationStart(self, nullptr); return 0;
        default: f.windowedStart(self, nullptr); return 0;
    }
}

Outcome Run(const Functions& f, int op, const std::uint8_t* initial, std::uint32_t arg, std::uint32_t debug) {
    Outcome out;
    std::memcpy(out.bytes, initial, kBytes);
    std::uint8_t* const self = out.bytes + kThis;
    // ~CApplication's vbtable inside the buffer (entry 1 = a displacement
    // that keeps the virtual base inside).
    const std::int32_t displacement = 0x10 + 4 * static_cast<std::int32_t>(arg % 4);
    const auto table = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(out.bytes + 0x50));
    std::memcpy(self - 0x10, &table, 4);
    std::memcpy(out.bytes + 0x54, &displacement, 4);
    std::uint8_t* const savedWindow = g_gameMainWindow;
    std::uint8_t* const savedApp = g_app;
    const std::uint32_t savedDebug = g_debugSwitch;
    g_gameMainWindow = nullptr;
    g_app = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(0x5a5a0000u));
    g_debugSwitch = debug;
    g_asserts = &out.asserts;
    {
        test::ScopedJump assertJump(kThrowAssertmentFailed, reinterpret_cast<const void*>(&AssertRecorder));
        out.eax = RunOp(f, op, self, arg);
    }
    g_asserts = nullptr;
    out.mainWindow = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_gameMainWindow.get()));
    out.app = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_app.get()));
    g_gameMainWindow = savedWindow;
    g_app = savedApp;
    g_debugSwitch = savedDebug;
    // Pointers into the buffer as offsets.
    const auto base = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(out.bytes));
    for (std::uint32_t* value : {&out.eax, &out.mainWindow}) {
        if (*value - base < kBytes) {
            *value = 0xbeef0000u + (*value - base);
        }
    }
    std::memset(out.bytes + 0x50, 0, 4);  // the vbtable pointer's target differs per run
    std::memset(self - 0x10, 0, 4);
    return out;
}

void Compare(test::Context& ctx, const Functions& ours, int cases) {
    const Functions originals = Originals();
    for (int i = 0; i < cases; ++i) {
        std::uint8_t initial[kBytes];
        ctx.rng().Fill(initial, kBytes);
        const int op = static_cast<int>(ctx.rng().Below(13));
        const std::uint32_t arg = ctx.rng().Next32();
        const std::uint32_t debug = ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Next32();
        const Outcome a = Run(originals, op, initial, arg, debug);
        const Outcome b = Run(ours, op, initial, arg, debug);
        if (!S3RE_EXPECT_EQ(a.eax, b.eax) || !S3RE_EXPECT_MEM_EQ(a.bytes, b.bytes, kBytes) ||
            !S3RE_EXPECT_EQ(a.mainWindow, b.mainWindow) || !S3RE_EXPECT_EQ(a.app, b.app) ||
            !S3RE_EXPECT(a.asserts == b.asserts)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: op %d", i, op);
            return;
        }
    }
}

std::uint32_t __fastcall WrongTimeout(std::uint32_t kind) {
    return Timeout(g_win32Timeouts.address(), kind ^ 1);  // wrong: the other kind
}

}  // namespace

S3RE_TEST(Win32AppMembers_MatchOriginal) {
    Compare(ctx, Ours(), 20000);
}

S3RE_TEST(Win32AppMembers_ComparisonDetectsWrongImplementation) {
    Functions wrong = Ours();
    wrong.win32Timeout = &WrongTimeout;
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Compare(probe, wrong, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
