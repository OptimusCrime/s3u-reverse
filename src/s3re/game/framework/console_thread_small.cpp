// Small members of the console thread (CConsoleThread, the application
// object on WinMain's stack; vtable 0x6553fc; Dirk\Console.cpp) and two
// small helpers compiled into that file.
//
// CConsoleThread::vf06 (0x00405b00): (bool* out) ret 4 -> the dword +0x54,
//   *out = 0 (vtable slot 6).
// CConsoleThread::vf07 (0x00405b10): () -> the dword +0x94 when the state
//   +0x44 is 0, +0x8c when it is 1, else the Console.cpp assert "Invalid
//   GameState" (line 0x251; Win32::ThrowAssertmentFailed 0x6119b0 throws)
//   (vtable slot 7).
// App_DecrementIndex (0x00409e50): (n) ret 4 -> n - 1, ECX unused. Callers:
//   CConsoleThread::vf04 and CGameSetupDialog's constructor. Was
//   App_sub_409e50.
// WCValOrderedVector_String::Equals (0x00409bd0): (const std::string* a,
//   const std::string* b) ret 8 -> 1 when both have the same length and
//   characters (a's text pointer used as it is, b's null text read as the
//   static empty string 0x6553a0), else 0 (slot 2 of the vtables 0x655440
//   and 0x656d08: the vector's equality callback). Was App_sub_409bd0.
// ostream_ApplyManipulator (0x004063f0): (manipulator) ret 4 -> this: calls
//   manipulator(this) (cdecl), the inlined ostream::operator<<(ostream&
//   (*)(ostream&)) of MSVCIRT. Callers: App_sub_405e50. Was App_sub_4063f0.
//
// The state handling (the console thread's subobject at +0x104 receives
// the thread messages):
// CConsoleThread::OnThreadMessage (0x00405c20; ECX = the thread + 0x104):
//   (message, wParam, lParam) ret 0xc: 0x800 -> CountStartupStep(wParam);
//   0x801 -> SetGameState(wParam, lParam != 0); other messages go to
//   Win32::CWindowedApp::OnThreadMessage 0x60f940 on the thread + 0x4c.
//   EAX: the handler's. Was App_sub_405c20.
// CConsoleThread::CountStartupStep (0x00405d90): (value) ret 4: in state 1
//   with value == +0xb8, +0xb4 counts up; when it reaches 4, +0xb0 = 1 and
//   the window +0x54 gets Lobby_sub_45ae60(1, 1). EAX: 1, or that call's.
//   Was App_sub_405d90.
// CConsoleThread::SetGameState (0x00405de0): (state, flag) ret 8: state 2
//   posts message 16 (WM_CLOSE) to the window +0x54
//   (Win32::CWindowBase::PostMessage 0x60f9b0); otherwise, coming from
//   state 1, +0xbc = 14 with the flag, else 15; then the window gets
//   Lobby_sub_45ae60(state, 0), the thread's virtual slot 5 runs, +0x44 =
//   state and slot 4 runs. EAX: the last call's. Was App_sub_405de0.
// CConsoleThread::LeaveGame (0x00405990): () +0xb0 = 0, the window gets
//   Lobby_sub_45ae60(+0x44, 0), DirectX::CGameAppDraw::Shutdown 0x5b5cb0,
//   g_graphicsDataLock (0x7a800c) and the objects +0x4c and +0x50 deleted
//   through the vtable of their +4 subobject (slot 0, flags 1) and cleared,
//   the object +0x60 destroyed (App_sub_405a50), freed (operator delete)
//   and cleared, slot 1 of the embedded objects +0x7c and +0x68, then
//   Net_sub_470170 on +0x9c in a multiplayer game
//   (GameSettings::IsMultiplayer), else GameSettings::Reset. Called by
//   CConsoleThread::vf05. Was App_sub_405990.
// CConsoleThread::LeaveMenus (0x00405600): () DirectX::CGameAppDraw::
//   Shutdown, then the menus' shared data +0xa4 destroyed
//   (CMenuSharedData::~CMenuSharedData 0x467070), freed and cleared.
//   Called by CConsoleThread::vf05. Was App_sub_405600.

#include "game/tables.h"
#include "game/texts.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace s3re::game {

#pragma pack(push, 1)
// The fields of CConsoleThread these members read (the rest unknown).
struct ConsoleThreadFields {
    std::uint32_t vtable;       // +0x00 0x6553fc
    std::uint8_t unk004[0x44 - 4];
    std::uint32_t game_state;   // +0x44 0, 1 or 2 (vf07, SetGameState)
    std::uint8_t unk048[0x4c - 0x48];
    std::uint32_t object4c;     // +0x4c deleted by LeaveGame (through its +4 subobject's vtable)
    std::uint32_t object50;     // +0x50 the same
    std::uint32_t value54;      // +0x54 the main window (vf06 returns it; Lobby_sub_45ae60, PostMessage)
    std::uint8_t unk058[0x60 - 0x58];
    std::uint32_t object60;     // +0x60 destroyed by App_sub_405a50 and freed by LeaveGame
    std::uint8_t unk064[0x68 - 0x64];
    std::uint32_t embedded68;   // +0x68 an embedded object's vtable (slot 1 called by LeaveGame)
    std::uint8_t unk06c[0x7c - 0x6c];
    std::uint32_t embedded7c;   // +0x7c the same
    std::uint8_t unk080[0x8c - 0x80];
    std::uint32_t value8c;      // +0x8c (vf07, state 1)
    std::uint8_t unk090[0x94 - 0x90];
    std::uint32_t value94;      // +0x94 (vf07, state 0)
    std::uint8_t unk098[0x9c - 0x98];
    std::uint32_t net9c;        // +0x9c passed to Net_sub_470170 by LeaveGame
    std::uint8_t unk0a0[0xa4 - 0xa0];
    std::uint32_t menu_shared;  // +0xa4 a CMenuSharedData (LeaveMenus)
    std::uint8_t unk0a8[0xb0 - 0xa8];
    std::uint8_t ready_b0;      // +0xb0 set when CountStartupStep reaches 4, cleared by LeaveGame
    std::uint8_t unk0b1[3];
    std::uint32_t steps_b4;     // +0xb4 CountStartupStep's counter
    std::uint32_t step_key_b8;  // +0xb8 the value CountStartupStep counts
    std::uint32_t value_bc;     // +0xbc 14 or 15 (SetGameState from state 1)
};
#pragma pack(pop)
static_assert(offsetof(ConsoleThreadFields, object4c) == 0x4c);
static_assert(offsetof(ConsoleThreadFields, object60) == 0x60);
static_assert(offsetof(ConsoleThreadFields, embedded68) == 0x68);
static_assert(offsetof(ConsoleThreadFields, embedded7c) == 0x7c);
static_assert(offsetof(ConsoleThreadFields, net9c) == 0x9c);
static_assert(offsetof(ConsoleThreadFields, menu_shared) == 0xa4);
static_assert(offsetof(ConsoleThreadFields, ready_b0) == 0xb0);
static_assert(offsetof(ConsoleThreadFields, value_bc) == 0xbc);
static_assert(sizeof(ConsoleThreadFields) == 0xc0);
static_assert(offsetof(ConsoleThreadFields, game_state) == 0x44);
static_assert(offsetof(ConsoleThreadFields, value54) == 0x54);
static_assert(offsetof(ConsoleThreadFields, value8c) == 0x8c);
static_assert(offsetof(ConsoleThreadFields, value94) == 0x94);

namespace {
constexpr std::uintptr_t kThrowAssertmentFailed = 0x006119b0;  // Win32::ThrowAssertmentFailed (ours)
constexpr std::int32_t kInvalidGameStateLine = 0x251;
constexpr std::uint32_t kStateZero = 0;
constexpr std::uint32_t kStateOne = 1;
}  // namespace

// Purpose: A console-thread virtual returning a value of the thread and a
// "false" flag (meaning unknown).
std::uint32_t __fastcall CConsoleThread_vf06(const ConsoleThreadFields* self, void*, std::uint8_t* out) {
    *out = 0;
    return self->value54;
}

// Purpose: A console-thread virtual returning one of two values by the
// thread's state (meaning unknown).
std::uint32_t __fastcall CConsoleThread_vf07(const ConsoleThreadFields* self, void*) {
    switch (self->game_state) {
    case kStateZero:
        return self->value94;
    case kStateOne:
        return self->value8c;
    default: {
        using ThrowFn = void(__fastcall*)(const char* expression, const char* file, std::int32_t line);
        reinterpret_cast<ThrowFn>(kThrowAssertmentFailed)(texts::kInvalidGameState, texts::kConsoleFile,
                                                          kInvalidGameStateLine);
        return 0;  // (not reached: the assert throws)
    }
    }
}

// Purpose: Turns a one-based number into an index (n - 1).
std::int32_t __fastcall App_DecrementIndex(const void*, void*, std::int32_t n) {
    return n - 1;
}

// Purpose: The string vector's equality test (same length, same characters).
std::uint32_t __stdcall WCValOrderedVector_String_Equals(const msvc6::String* a, const msvc6::String* b) {
    const char* textB = b->ptr != nullptr ? b->ptr : g_stringNullBuffer.get();
    const std::uint32_t shorter = a->len < b->len ? a->len : b->len;
    if (std::memcmp(a->ptr, textB, shorter) != 0) {
        return 0;
    }
    return a->len == b->len ? 1 : 0;
}

// Purpose: Applies an output-stream manipulator (std::endl and the like) to
// a stream.
void* __fastcall ostream_ApplyManipulator(void* self, void*, void(__cdecl* manipulator)(void* stream)) {
    manipulator(self);
    return self;
}

namespace {

constexpr std::uintptr_t kWindowSetState = 0x0045ae60;      // Lobby_sub_45ae60 (on the main window)
constexpr std::uintptr_t kPostMessage = 0x0060f9b0;         // Win32::CWindowBase::PostMessage (verified)
constexpr std::uintptr_t kAppOnThreadMessage = 0x0060f940;  // Win32::CWindowedApp::OnThreadMessage
constexpr std::uintptr_t kAppShutdown = 0x005b5cb0;         // DirectX::CGameAppDraw::Shutdown
constexpr std::uintptr_t kDestroy405a50 = 0x00405a50;       // App_sub_405a50 (the +0x60 object's destructor)
constexpr std::uintptr_t kMenuSharedDestroy = 0x00467070;   // CMenuSharedData::~CMenuSharedData
constexpr std::uintptr_t kNetLeave = 0x00470170;            // Net_sub_470170 (network code)
constexpr std::uintptr_t kIsMultiplayer = 0x004318d0;       // GameSettings::IsMultiplayer (ours)
constexpr std::uintptr_t kSettingsReset = 0x00433810;       // GameSettings::Reset (ours)
constexpr std::uintptr_t kGameSettings = 0x007a7a18;        // g_gameSettings
constexpr std::uintptr_t kGraphicsDataLock = 0x007a800c;    // g_graphicsDataLock (a Win32::CSemaphore*)
constexpr std::uint32_t kThreadToMessageTarget = 0x104;     // OnThreadMessage's ECX: the thread + 0x104
constexpr std::uint32_t kThreadToWindowedApp = 0x4c;        // the CWindowedApp part: the thread + 0x4c
constexpr std::uint32_t kMessageStartupStep = 0x800;
constexpr std::uint32_t kMessageSetGameState = 0x801;
constexpr std::uint32_t kStepsUntilReady = 4;
constexpr std::uint32_t kStateClosing = 2;
constexpr std::uint32_t kWmClose = 16;
constexpr std::uint32_t kValueFromStateOneWithFlag = 14;
constexpr std::uint32_t kValueFromStateOne = 15;
constexpr std::uint32_t kSlotLeaveState = 5;  // the thread's virtual slots SetGameState calls
constexpr std::uint32_t kSlotEnterState = 4;

using MemberFn0 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);
using MemberFn1 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a);
using MemberFn2 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a, std::uint32_t b);
using MemberFn3 = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t a, std::uint32_t b,
                                             std::uint32_t c);

std::uintptr_t AddressOf(const void* p) {
    return reinterpret_cast<std::uintptr_t>(p);
}

std::uint32_t VirtualCall0(std::uintptr_t object, std::uint32_t slot) {
    const auto* vtable = *reinterpret_cast<const std::uintptr_t* const*>(object);
    return reinterpret_cast<MemberFn0>(vtable[slot])(object, nullptr);
}

// Deletes an object through the vtable of its subobject at +4 (slot 0, the
// scalar deleting destructor, flags 1).
void DeleteThroughSubobject4(std::uintptr_t object) {
    const std::uintptr_t subobject = object + 4;
    const auto* vtable = *reinterpret_cast<const std::uintptr_t* const*>(subobject);
    reinterpret_cast<MemberFn1>(vtable[0])(subobject, nullptr, 1);
}

}  // namespace

// Purpose: Counts the startup steps the other threads report; after four
// the main window is told the game is ready (probably).
std::uint32_t __fastcall CConsoleThread_CountStartupStep(ConsoleThreadFields* self, void*, std::uint32_t value) {
    if (self->game_state != kStateOne || value != self->step_key_b8) {
        return 1;
    }
    ++self->steps_b4;
    if (self->steps_b4 != kStepsUntilReady) {
        return 1;
    }
    self->ready_b0 = 1;
    return reinterpret_cast<MemberFn2>(kWindowSetState)(self->value54, nullptr, 1, 1);
}

// Purpose: Switches the console thread between its states (0 and 1 are
// probably the menus and the game; 2 closes the window).
std::uint32_t __fastcall CConsoleThread_SetGameState(ConsoleThreadFields* self, void*, std::uint32_t state,
                                                     std::uint32_t flag) {
    if (state == kStateClosing) {
        return reinterpret_cast<MemberFn3>(kPostMessage)(self->value54, nullptr, kWmClose, 0, 0);
    }
    if (self->game_state == kStateOne) {
        self->value_bc = (flag & 0xff) != 0 ? kValueFromStateOneWithFlag : kValueFromStateOne;
    }
    reinterpret_cast<MemberFn2>(kWindowSetState)(self->value54, nullptr, state, 0);
    VirtualCall0(AddressOf(self), kSlotLeaveState);
    self->game_state = state;
    return VirtualCall0(AddressOf(self), kSlotEnterState);
}

// Purpose: The console thread's handler of its thread messages (startup
// steps and state changes; the rest goes to the application base class).
std::uint32_t __fastcall CConsoleThread_OnThreadMessage(std::uintptr_t target, void*, std::uint32_t message,
                                                        std::uint32_t wParam, std::uint32_t lParam) {
    using CountStepFn = std::uint32_t(__fastcall*)(ConsoleThreadFields* self, void* edx, std::uint32_t value);
    using SetStateFn = std::uint32_t(__fastcall*)(ConsoleThreadFields* self, void* edx, std::uint32_t state,
                                                  std::uint32_t flag);
    constexpr std::uintptr_t kCountStartupStep = 0x00405d90;  // ours, through its hook
    constexpr std::uintptr_t kSetGameState = 0x00405de0;      // ours, through its hook
    auto* thread = reinterpret_cast<ConsoleThreadFields*>(target - kThreadToMessageTarget);
    if (message == kMessageStartupStep) {
        return reinterpret_cast<CountStepFn>(kCountStartupStep)(thread, nullptr, wParam);
    }
    if (message == kMessageSetGameState) {
        // 0x405c49: `setne dl` on the loaded lParam, then the whole EDX is
        // pushed (the callee reads only the low byte).
        const std::uint32_t flag = (lParam & 0xffffff00u) | (lParam != 0 ? 1u : 0u);
        return reinterpret_cast<SetStateFn>(kSetGameState)(thread, nullptr, wParam, flag);
    }
    return reinterpret_cast<MemberFn3>(kAppOnThreadMessage)(AddressOf(thread) + kThreadToWindowedApp, nullptr,
                                                            message, wParam, lParam);
}

// Purpose: Tears down what a running game needed when the console thread
// leaves the game (threads, graphics lock, game objects; resets the game
// setup of a single-player game).
void __fastcall CConsoleThread_LeaveGame(ConsoleThreadFields* self, void*) {
    self->ready_b0 = 0;
    reinterpret_cast<MemberFn2>(kWindowSetState)(self->value54, nullptr, self->game_state, 0);
    reinterpret_cast<MemberFn0>(kAppShutdown)(AddressOf(self), nullptr);
    auto& graphicsLock = *reinterpret_cast<std::uint32_t*>(kGraphicsDataLock);  // raw-ok: g_graphicsDataLock
    if (graphicsLock != 0) {
        DeleteThroughSubobject4(graphicsLock);
    }
    graphicsLock = 0;
    if (self->object4c != 0) {
        DeleteThroughSubobject4(self->object4c);
    }
    self->object4c = 0;
    if (self->object50 != 0) {
        DeleteThroughSubobject4(self->object50);
    }
    self->object50 = 0;
    if (self->object60 != 0) {
        reinterpret_cast<MemberFn0>(kDestroy405a50)(self->object60, nullptr);
        msvc6::GameOperatorDelete(reinterpret_cast<void*>(static_cast<std::uintptr_t>(self->object60)));
    }
    self->object60 = 0;
    VirtualCall0(AddressOf(&self->embedded7c), 1);
    VirtualCall0(AddressOf(&self->embedded68), 1);
    if ((reinterpret_cast<MemberFn0>(kIsMultiplayer)(kGameSettings, nullptr) & 0xff) != 0) {
        reinterpret_cast<MemberFn0>(kNetLeave)(self->net9c, nullptr);
    } else {
        reinterpret_cast<MemberFn0>(kSettingsReset)(kGameSettings, nullptr);
    }
}

// Purpose: Tears down the menus when the console thread leaves them.
void __fastcall CConsoleThread_LeaveMenus(ConsoleThreadFields* self, void*) {
    reinterpret_cast<MemberFn0>(kAppShutdown)(AddressOf(self), nullptr);
    if (self->menu_shared != 0) {
        reinterpret_cast<MemberFn0>(kMenuSharedDestroy)(self->menu_shared, nullptr);
        msvc6::GameOperatorDelete(reinterpret_cast<void*>(static_cast<std::uintptr_t>(self->menu_shared)));
    }
    self->menu_shared = 0;
}

using CountStepHookFn = std::uint32_t(__fastcall*)(ConsoleThreadFields* self, void* edx, std::uint32_t value);
using SetStateHookFn = std::uint32_t(__fastcall*)(ConsoleThreadFields* self, void* edx, std::uint32_t state,
                                                  std::uint32_t flag);
using OnMessageFn = std::uint32_t(__fastcall*)(std::uintptr_t target, void* edx, std::uint32_t message,
                                               std::uint32_t wParam, std::uint32_t lParam);
using LeaveFn = void(__fastcall*)(ConsoleThreadFields* self, void* edx);
S3RE_HOOK(0x00405d90, CConsoleThread_CountStartupStep, CountStepHookFn, Original_CConsoleThread_CountStartupStep,
          CConsoleThread_CountStartupStep);
S3RE_HOOK(0x00405de0, CConsoleThread_SetGameState, SetStateHookFn, Original_CConsoleThread_SetGameState,
          CConsoleThread_SetGameState);
S3RE_HOOK(0x00405c20, CConsoleThread_OnThreadMessage, OnMessageFn, Original_CConsoleThread_OnThreadMessage,
          CConsoleThread_OnThreadMessage);
S3RE_HOOK(0x00405990, CConsoleThread_LeaveGame, LeaveFn, Original_CConsoleThread_LeaveGame,
          CConsoleThread_LeaveGame);
S3RE_HOOK(0x00405600, CConsoleThread_LeaveMenus, LeaveFn, Original_CConsoleThread_LeaveMenus,
          CConsoleThread_LeaveMenus);

using Vf06Fn = std::uint32_t(__fastcall*)(const ConsoleThreadFields* self, void* edx, std::uint8_t* out);
using Vf07Fn = std::uint32_t(__fastcall*)(const ConsoleThreadFields* self, void* edx);
using DecrementFn = std::int32_t(__fastcall*)(const void* self, void* edx, std::int32_t n);
using EqualsFn = std::uint32_t(__stdcall*)(const msvc6::String* a, const msvc6::String* b);
using ManipulatorFn = void(__cdecl*)(void* stream);
using ApplyFn = void*(__fastcall*)(void* self, void* edx, ManipulatorFn manipulator);
S3RE_HOOK(0x00405b00, CConsoleThread_vf06, Vf06Fn, Original_CConsoleThread_vf06, CConsoleThread_vf06);
S3RE_HOOK(0x00405b10, CConsoleThread_vf07, Vf07Fn, Original_CConsoleThread_vf07, CConsoleThread_vf07);
S3RE_HOOK(0x00409e50, App_DecrementIndex, DecrementFn, Original_App_DecrementIndex, App_DecrementIndex);
S3RE_HOOK(0x00409bd0, WCValOrderedVector_String_Equals, EqualsFn, Original_WCValOrderedVector_String_Equals,
          WCValOrderedVector_String_Equals);
S3RE_HOOK(0x004063f0, ostream_ApplyManipulator, ApplyFn, Original_ostream_ApplyManipulator,
          ostream_ApplyManipulator);

namespace {

void* g_manipulated = nullptr;
std::uint32_t g_manipulations = 0;

void __cdecl RecordManipulator(void* stream) {
    g_manipulated = stream;
    ++g_manipulations;
}

std::string RandomText(test::Rng& rng) {
    std::string text(rng.Below(6), 'a');
    for (char& c : text) {
        c = static_cast<char>('a' + rng.Below(3));
    }
    return text;
}

// Wrong: compares only the lengths.
std::uint32_t __stdcall WrongEquals(const msvc6::String* a, const msvc6::String* b) {
    return a->len == b->len ? 1 : 0;
}

int CountEqualsDifferences(test::Context& ctx, EqualsFn candidate, int cases) {
    int differences = 0;
    for (int i = 0; i < cases; ++i) {
        const std::string ta = RandomText(ctx.rng());
        const std::string tb = ctx.rng().Chance(1, 3) ? ta : RandomText(ctx.rng());
        msvc6::String a;
        msvc6::String b;
        msvc6::String_FromChars(&a, nullptr, ta.c_str(), static_cast<std::uint32_t>(ta.size()));
        if (tb.empty() && ctx.rng().Chance(1, 2)) {
            msvc6::detail::Clear(&b);
        } else {
            msvc6::String_FromChars(&b, nullptr, tb.c_str(), static_cast<std::uint32_t>(tb.size()));
        }
        if ((Original_WCValOrderedVector_String_Equals(&a, &b) & 0xff) != (candidate(&a, &b) & 0xff)) {
            ++differences;
        }
        msvc6::String_Destroy(&a, nullptr);
        msvc6::String_Destroy(&b, nullptr);
    }
    return differences;
}

}  // namespace

S3RE_TEST(ConsoleThreadSmall_MatchOriginal) {
    for (int i = 0; i < 500; ++i) {
        ConsoleThreadFields fields;
        ctx.rng().Fill(&fields, sizeof(fields));
        fields.game_state = ctx.rng().Below(2);
        std::uint8_t outA = 0x5a;
        std::uint8_t outB = 0x5a;
        const std::int32_t n = static_cast<std::int32_t>(ctx.rng().Next32());
        if (!S3RE_EXPECT_EQ(Original_CConsoleThread_vf06(&fields, nullptr, &outA),
                            CConsoleThread_vf06(&fields, nullptr, &outB)) ||
            !S3RE_EXPECT_EQ(outA, outB) ||
            !S3RE_EXPECT_EQ(Original_CConsoleThread_vf07(&fields, nullptr), CConsoleThread_vf07(&fields, nullptr)) ||
            !S3RE_EXPECT_EQ(Original_App_DecrementIndex(nullptr, nullptr, n), App_DecrementIndex(nullptr, nullptr, n))) {
            return;
        }
    }
    S3RE_EXPECT_EQ(0, CountEqualsDifferences(ctx, &WCValOrderedVector_String_Equals, 3000));
    std::uint8_t stream[16] = {};
    g_manipulations = 0;
    S3RE_EXPECT(Original_ostream_ApplyManipulator(stream, nullptr, &RecordManipulator) == stream);
    S3RE_EXPECT(ostream_ApplyManipulator(stream, nullptr, &RecordManipulator) == stream);
    S3RE_EXPECT(g_manipulations == 2 && g_manipulated == stream);
}

namespace {

// The state handling on a fake console thread: a buffer whose vtable,
// embedded objects (+0x68, +0x7c) and deletable objects (+0x4c, +0x50 and
// g_graphicsDataLock, through their +4 subobject) point at recording fake
// vtables; the callees (the window's 45ae60, PostMessage, the base class's
// OnThreadMessage, Shutdown, 405a50, ~CMenuSharedData, Net_sub_470170,
// IsMultiplayer, Reset) are recording stubs and operator delete records
// instead of freeing. Compared: the buffer (fake pointers made relative),
// EAX, the stub calls and the fake vtable calls in order.
constexpr std::size_t kFakeThreadBytes = 0x180;
std::vector<std::uint32_t>* g_stateLog = nullptr;
std::uintptr_t g_stateBase = 0;

std::uint32_t Rel(std::uintptr_t p) {
    return static_cast<std::uint32_t>(p - g_stateBase);
}

template <std::uint32_t Tag>
std::uint32_t __fastcall FakeSlot0(std::uintptr_t self, void*) {
    g_stateLog->insert(g_stateLog->end(), {Tag, Rel(self)});
    return Tag;
}
template <std::uint32_t Tag>
std::uint32_t __fastcall FakeSlot1(std::uintptr_t self, void*, std::uint32_t a) {
    g_stateLog->insert(g_stateLog->end(), {Tag, Rel(self), a});
    return Tag;
}
void __cdecl FakeOperatorDelete(std::uintptr_t block) {
    g_stateLog->insert(g_stateLog->end(), {0xde1u, Rel(block)});
}

// Thread vtable: slots 4 and 5; subobject vtables: slot 0 (deleting
// destructor); embedded objects: slot 1.
const std::uintptr_t kThreadVtable[6] = {0, 0, 0, 0, reinterpret_cast<std::uintptr_t>(&FakeSlot0<0x54>),
                                         reinterpret_cast<std::uintptr_t>(&FakeSlot0<0x55>)};
const std::uintptr_t kSubobjectVtable[1] = {reinterpret_cast<std::uintptr_t>(&FakeSlot1<0xd0>)};
const std::uintptr_t kEmbeddedVtable[2] = {0, reinterpret_cast<std::uintptr_t>(&FakeSlot0<0xe1>)};

constexpr std::uintptr_t kOperatorDeleteAddress = 0x0061e4ef;

struct StateOutcome {
    std::uint32_t eax;
    std::vector<std::uint8_t> image;
    std::vector<std::uint32_t> log;
    std::vector<test::StubCall> calls;
};

using StateCall = std::function<std::uint32_t(std::uint8_t* image, bool original)>;

StateOutcome RunState(const std::vector<std::uint8_t>& start, std::uint32_t multiplayer, const StateCall& call,
                      bool original) {
    StateOutcome out;
    out.image = start;
    std::uint8_t* image = out.image.data();
    g_stateBase = reinterpret_cast<std::uintptr_t>(image);
    g_stateLog = &out.log;
    auto put = [image](std::size_t at, std::uintptr_t value) {
        const auto v = static_cast<std::uint32_t>(value);
        std::memcpy(image + at, &v, 4);
    };
    put(0x00, reinterpret_cast<std::uintptr_t>(kThreadVtable));
    put(0x68, reinterpret_cast<std::uintptr_t>(kEmbeddedVtable));
    put(0x7c, reinterpret_cast<std::uintptr_t>(kEmbeddedVtable));
    // Deletable objects at +0x100 / +0x110 / +0x120 (their +4 is a subobject),
    // the +0x60 and +0xa4 objects at +0x130 / +0x140; some pointers null.
    const std::uint8_t mask = start[0x17f];
    put(0x4c, (mask & 1) != 0 ? 0 : g_stateBase + 0x100);
    put(0x50, (mask & 2) != 0 ? 0 : g_stateBase + 0x110);
    put(0x60, (mask & 4) != 0 ? 0 : g_stateBase + 0x130);
    put(0xa4, (mask & 8) != 0 ? 0 : g_stateBase + 0x140);
    for (std::size_t at : {std::size_t{0x104}, std::size_t{0x114}, std::size_t{0x124}}) {
        put(at, reinterpret_cast<std::uintptr_t>(kSubobjectVtable));
    }
    auto& lock = *reinterpret_cast<std::uint32_t*>(kGraphicsDataLock);  // raw-ok: g_graphicsDataLock
    const std::uint32_t savedLock = lock;
    lock = (mask & 16) != 0 ? 0 : static_cast<std::uint32_t>(g_stateBase + 0x120);
    test::PatchJump(kOperatorDeleteAddress, reinterpret_cast<const void*>(&FakeOperatorDelete));
    {
        test::CalleeStubs stubs;
        stubs.SetWorld(reinterpret_cast<CGameData*>(image));
        stubs.Install<2, kWindowSetState>();
        stubs.Install<3, kPostMessage>();
        stubs.Install<3, kAppOnThreadMessage>();
        stubs.Install<0, kAppShutdown>();
        stubs.Install<0, kDestroy405a50>();
        stubs.Install<0, kMenuSharedDestroy>();
        stubs.Install<0, kNetLeave>();
        stubs.Install<0, kIsMultiplayer>([multiplayer](CGameData*, std::uint32_t, const std::uint32_t*) {
            return multiplayer;
        });
        stubs.Install<0, kSettingsReset>();
        out.eax = call(image, original);
        out.calls = stubs.Take();
    }
    // The stubs record ECX relative to the buffer: absolute again unless
    // it points into the buffer (the two runs' buffers differ).
    for (test::StubCall& c : out.calls) {
        const std::uintptr_t self = g_stateBase + c.self;
        if (self < g_stateBase || self >= g_stateBase + kFakeThreadBytes) {
            c.self = static_cast<std::uint32_t>(self);
        }
    }
    test::UnpatchJump(kOperatorDeleteAddress);
    out.log.push_back(lock == 0 ? 0 : Rel(lock));
    lock = savedLock;
    g_stateLog = nullptr;
    // Fake pointers relative to the buffer.
    for (std::size_t at = 0; at + 4 <= kFakeThreadBytes; at += 4) {
        std::uint32_t v = 0;
        std::memcpy(&v, image + at, 4);
        if (v >= g_stateBase && v < g_stateBase + kFakeThreadBytes) {
            v = Rel(v);
            std::memcpy(image + at, &v, 4);
        }
    }
    return out;
}

struct StateFunctions {
    CountStepHookFn countStep;
    SetStateHookFn setState;
    OnMessageFn onMessage;
    LeaveFn leaveGame;
    LeaveFn leaveMenus;
};

const StateFunctions& OriginalStateFunctions() {
    static const StateFunctions functions{Original_CConsoleThread_CountStartupStep,
                                          Original_CConsoleThread_SetGameState,
                                          Original_CConsoleThread_OnThreadMessage, Original_CConsoleThread_LeaveGame,
                                          Original_CConsoleThread_LeaveMenus};
    return functions;
}

void CompareState(test::Context& ctx, const StateFunctions& candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        std::vector<std::uint8_t> start(kFakeThreadBytes);
        ctx.rng().Fill(start.data(), start.size());
        auto put = [&start](std::size_t at, std::uint32_t v) { std::memcpy(start.data() + at, &v, 4); };
        put(0x44, ctx.rng().Below(3));
        put(0xb4, ctx.rng().Below(5));
        const std::uint32_t key = ctx.rng().Below(3);
        put(0xb8, key);
        const std::uint32_t which = ctx.rng().Below(5);
        const std::uint32_t a = ctx.rng().Chance(1, 2) ? key : ctx.rng().Below(4);
        const std::uint32_t b = ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Next32();
        const std::uint32_t message = 0x7ff + ctx.rng().Below(4);
        const std::uint32_t multiplayer = ctx.rng().Below(2) * (ctx.rng().Chance(1, 2) ? 1 : 0x301);
        auto run = [&](const StateFunctions& f) {
            return RunState(start, multiplayer, [&](std::uint8_t* image, bool) -> std::uint32_t {
                auto* thread = reinterpret_cast<ConsoleThreadFields*>(image);
                switch (which) {
                case 0:
                    return f.countStep(thread, nullptr, a);
                case 1:
                    return f.setState(thread, nullptr, a, b);
                case 2:
                    return f.onMessage(reinterpret_cast<std::uintptr_t>(image) + kThreadToMessageTarget, nullptr,
                                       message, a, b);
                case 3:
                    f.leaveGame(thread, nullptr);
                    return 0;
                default:
                    f.leaveMenus(thread, nullptr);
                    return 0;
                }
            }, false);
        };
        const StateOutcome expected = run(OriginalStateFunctions());
        const StateOutcome actual = run(candidate);
        bool same = S3RE_EXPECT_EQ(expected.eax, actual.eax);
        same = S3RE_EXPECT_MEM_EQ(expected.image.data(), actual.image.data(), kFakeThreadBytes) && same;
        same = S3RE_EXPECT(expected.log == actual.log) && same;
        same = S3RE_EXPECT(expected.calls == actual.calls) && same;
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: function %u, a %u, b %u, message 0x%x", i, which, a, b, message);
            return;
        }
    }
}

// Wrong: counts one step too few before the window is told.
std::uint32_t __fastcall WrongCountStartupStep(ConsoleThreadFields* self, void* edx, std::uint32_t value) {
    if (self->game_state == kStateOne && value == self->step_key_b8 && self->steps_b4 == 2) {
        self->steps_b4 = 3;
    }
    return CConsoleThread_CountStartupStep(self, edx, value);
}

}  // namespace

S3RE_TEST(ConsoleThreadState_MatchOriginal) {
    CompareState(ctx,
                 {&CConsoleThread_CountStartupStep, &CConsoleThread_SetGameState, &CConsoleThread_OnThreadMessage,
                  &CConsoleThread_LeaveGame, &CConsoleThread_LeaveMenus},
                 2000);
}

S3RE_TEST(ConsoleThreadState_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareState(probe,
                 {&WrongCountStartupStep, &CConsoleThread_SetGameState, &CConsoleThread_OnThreadMessage,
                  &CConsoleThread_LeaveGame, &CConsoleThread_LeaveMenus},
                 500);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(ConsoleThreadSmall_ComparisonDetectsWrongImplementation) {
    S3RE_EXPECT(CountEqualsDifferences(ctx, &WrongEquals, 3000) > 0);
}

}  // namespace s3re::game
