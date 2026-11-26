// The framework's thin wrappers of Windows synchronization objects, threads
// and windows (Win32\Synchronize.cpp, synchronize.ipp, Classes.cpp; RTTI
// class names; session 58, wip/framework-errors). Each makes one Windows
// call and checks it with the framework's VERIFY macro: on failure
// Win32::ThrowWin32Error(expression, file, line, GetLastError(), location),
// where location is an empty string built from "" (0x7a7868) through two
// temporaries (_Tidy + assign(text, n), _Tidy + assign(that, 0, -1), then
// location.assign(copy, 0, npos)) before GetLastError is called. The code
// after the (never returning) throw destroys the location again.
//
// Object fields: CWin32Waitable / CEvent / CSemaphore / CMutex / CThread
// keep their handle in the CWin32Handle at +4 (handle at +8); CWindowBase
// has its HWND at +4; a CMessagedThread reaches its CThread (thread id at
// +0x14) through the virtual-base table at +0 (entry 1 = the offset).
//
// The synchronize.ipp members are inline, so the compiler emitted copies in
// other units: CEvent::Set 0x42cf80 (Draw.cpp's range), CEvent::Reset
// 0x523110, CEvent::Pulse 0x47fc80, CSemaphore::Release 0x53e830.
//   Set / Reset / Pulse (): VERIFY(SetEvent / ResetEvent / PulseEvent
//     (handle)), lines 271 / 280 / 289 of Win32\synchronize.ipp. EAX: the
//     Windows result.
//   CSemaphore::Release (count) ret 4: VERIFY(ReleaseSemaphore(handle,
//     count, NULL)), line 299. EAX: the result.
// CEvent::Create 0x60c530 (security, manualReset, initialState, name) ret
//   10h, CSemaphore::Create 0x60c7a0 (security, initial, maximum, name) ret
//   10h, CMutex::Create 0x60ca30 (security, initialOwner, name) ret 0Ch:
//   VERIFY(handle != NULL) (Synchronize.cpp lines 178, 207, 236), then
//   CWin32Handle::Set(this + 4, handle, 1) (0x60bf00), whose EAX they return.
// CWin32Waitable::Wait 0x60c060 (timeout, throwOnTimeout) ret 8:
//   WaitForSingleObject(handle, timeout); WAIT_TIMEOUT: with
//   throwOnTimeout (its low byte) Win32::ThrowTimeout("Win32::
//   CWin32Waitable::WaitFor", Synchronize.cpp, 110), else false (AL = 0;
//   EAX = 0x100, the rest of WAIT_TIMEOUT). Any other result but 0: if
//   GetLastError() is not 0, ThrowWin32Error("error check", line 116) with
//   a second GetLastError(). Then true (AL = 1 over GetLastError's EAX, or
//   over WAIT_OBJECT_0's 0).
// CThread::SetPriority 0x60e9d0 (priority) ret 4: VERIFY(SetThreadPriority
//   (handle, priority)) as "result != FALSE", Classes.cpp line 210.
// CMessagedThread::PostThreadMessage 0x60f480 (message, wParam, lParam)
//   ret 0Ch: PostThreadMessageA(thread id, ...); when it fails and
//   GetLastError() is not 0: ThrowWin32Error("Success", line 410) with a
//   second GetLastError(). EAX: result != 0.
// CWindowBase::PostMessage 0x60f9b0 (message, wParam, lParam) ret 0Ch:
//   VERIFY(PostMessageA(hwnd, ...)) as "res", line 504. EAX: the result.
// CWindowBase::Destroy 0x60fbf0 (): VERIFY(DestroyWindow(hwnd)), line 526;
//   then +4 = 0. EAX: the result.
//
// Allocator bytes: the location string's comes from a stack byte of the
// original (uninitialized, or an argument's); it is never copied (CHResult
// gives its own copy the low byte of mostDerived), ours use 0.

#include "game/framework/exception_strings.h"
#include "game/framework/framework_layout.h"
#include "game/globals.h"
#include "game/texts.h"
#include "hooks.h"
#include "msvc6/string.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace s3re::game {

namespace {

using framework::CWin32Handle;
using msvc6::String;

constexpr std::uintptr_t kThrowWin32Error = 0x00610de0;
constexpr std::uintptr_t kThrowTimeout = 0x00610ba0;
constexpr std::uintptr_t kHandleSet = 0x0060bf00;  // Win32::CWin32Handle::Set(handle, owned)

constexpr std::uint32_t kWaitTimeout = 0x102;  // WAIT_TIMEOUT
constexpr std::uint32_t kOwned = 1;            // CWin32Handle::Set's second argument

// Source lines of the VERIFY checks.
constexpr std::uint32_t kLineSetEvent = 271;  // Win32\synchronize.ipp
constexpr std::uint32_t kLineResetEvent = 280;
constexpr std::uint32_t kLinePulseEvent = 289;
constexpr std::uint32_t kLineReleaseSemaphore = 299;
constexpr std::uint32_t kLineWaitTimeout = 110;  // Win32\Synchronize.cpp
constexpr std::uint32_t kLineWaitError = 116;
constexpr std::uint32_t kLineCreateEvent = 178;
constexpr std::uint32_t kLineCreateSemaphore = 207;
constexpr std::uint32_t kLineCreateMutex = 236;
constexpr std::uint32_t kLineSetPriority = 210;  // Win32\Classes.cpp
constexpr std::uint32_t kLinePostThreadMessage = 410;
constexpr std::uint32_t kLinePostMessage = 504;
constexpr std::uint32_t kLineDestroyWindow = 526;

using ThrowWin32ErrorFn = void(__fastcall*)(const char* expression, const char* file, std::uint32_t line,
                                            std::uint32_t error, const String* location);
using ThrowTimeoutFn = void(__fastcall*)(const char* expression, const char* file, std::uint32_t line);
using HandleSetFn = std::uint32_t(__fastcall*)(void* self, void* edx, void* handle, std::uint32_t owned);

// The handle of a CWin32Handle subobject at +4.
void* HandleOf(void* self) {
    return reinterpret_cast<void*>(reinterpret_cast<CWin32Handle*>(static_cast<std::uint8_t*>(self) + 4)->handle);
}

// The window of a CWindowBase.
void* WindowOf(void* self) {
    return reinterpret_cast<void*>(static_cast<framework::CWindowBase*>(self)->window);
}

// VERIFY failed: ThrowWin32Error with GetLastError() and the empty location.
void ThrowLastError(const char* expression, const char* file, std::uint32_t line) {
    String location;
    framework::MakeEmpty(&location, 0);
    framework::AssignThroughAssigns(&location, texts::kEmpty.c_str(), 0);
    const std::uint32_t error = g_importGetLastError.get()();
    reinterpret_cast<ThrowWin32ErrorFn>(kThrowWin32Error)(expression, file, line, error, &location);
    framework::EraseAndFree(&location);  // not reached: ThrowWin32Error throws
}

// The VERIFY of a call that returns its success.
std::uint32_t Verify(std::uint32_t result, const char* expression, const char* file, std::uint32_t line) {
    if (result == 0) {
        ThrowLastError(expression, file, line);
    }
    return result;
}

// The Create members: VERIFY(handle != NULL), then the handle is kept.
std::uint32_t KeepHandle(void* self, void* handle, const GameText& expression, std::uint32_t line) {
    if (handle == nullptr) {
        ThrowLastError(expression.c_str(), texts::kSynchronizeFile.c_str(), line);
    }
    return reinterpret_cast<HandleSetFn>(kHandleSet)(static_cast<std::uint8_t*>(self) + 4, nullptr, handle, kOwned);
}

}  // namespace

using MemberFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using MemberArgFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t value);
using Member2Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b);
using Member3Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b,
                                             std::uint32_t c);
using Member4Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b,
                                             std::uint32_t c, std::uint32_t d);

std::uint32_t __fastcall Win32_CEvent_Set(void* self, void* edx);
std::uint32_t __fastcall Win32_CEvent_Reset(void* self, void* edx);
std::uint32_t __fastcall Win32_CEvent_Pulse(void* self, void* edx);
std::uint32_t __fastcall Win32_CSemaphore_Release(void* self, void* edx, std::uint32_t count);
std::uint32_t __fastcall Win32_CEvent_Create(void* self, void* edx, std::uint32_t security, std::uint32_t manualReset,
                                             std::uint32_t initialState, std::uint32_t name);
std::uint32_t __fastcall Win32_CSemaphore_Create(void* self, void* edx, std::uint32_t security, std::uint32_t initial,
                                                 std::uint32_t maximum, std::uint32_t name);
std::uint32_t __fastcall Win32_CMutex_Create(void* self, void* edx, std::uint32_t security,
                                             std::uint32_t initialOwner, std::uint32_t name);
std::uint32_t __fastcall Win32_CWin32Waitable_Wait(void* self, void* edx, std::uint32_t timeout,
                                                   std::uint32_t throwOnTimeout);
std::uint32_t __fastcall Win32_CThread_SetPriority(void* self, void* edx, std::uint32_t priority);
std::uint32_t __fastcall Win32_CMessagedThread_PostThreadMessage(void* self, void* edx, std::uint32_t message,
                                                                 std::uint32_t wParam, std::uint32_t lParam);
std::uint32_t __fastcall Win32_CWindowBase_PostMessage(void* self, void* edx, std::uint32_t message,
                                                       std::uint32_t wParam, std::uint32_t lParam);
std::uint32_t __fastcall Win32_CWindowBase_Destroy(void* self, void* edx);

S3RE_HOOK(0x0042cf80, Win32_CEvent_Set, MemberFn, Original_Win32_CEvent_Set, Win32_CEvent_Set);

// Purpose: signals an event (wakes the threads waiting for it).
std::uint32_t __fastcall Win32_CEvent_Set(void* self, void* /*edx*/) {
    const auto result = static_cast<std::uint32_t>(g_importSetEvent.get()(HandleOf(self)));
    return Verify(result, texts::kVerifySetEvent.c_str(), texts::kSynchronizeIpp.c_str(), kLineSetEvent);
}

S3RE_HOOK(0x00523110, Win32_CEvent_Reset, MemberFn, Original_Win32_CEvent_Reset, Win32_CEvent_Reset);

// Purpose: clears an event's signal.
std::uint32_t __fastcall Win32_CEvent_Reset(void* self, void* /*edx*/) {
    const auto result = static_cast<std::uint32_t>(g_importResetEvent.get()(HandleOf(self)));
    return Verify(result, texts::kVerifyResetEvent.c_str(), texts::kSynchronizeIpp.c_str(), kLineResetEvent);
}

S3RE_HOOK(0x0047fc80, Win32_CEvent_Pulse, MemberFn, Original_Win32_CEvent_Pulse, Win32_CEvent_Pulse);

// Purpose: signals an event once (releases the threads waiting right now).
std::uint32_t __fastcall Win32_CEvent_Pulse(void* self, void* /*edx*/) {
    const auto result = static_cast<std::uint32_t>(g_importPulseEvent.get()(HandleOf(self)));
    return Verify(result, texts::kVerifyPulseEvent.c_str(), texts::kSynchronizeIpp.c_str(), kLinePulseEvent);
}

S3RE_HOOK(0x0053e830, Win32_CSemaphore_Release, MemberArgFn, Original_Win32_CSemaphore_Release,
          Win32_CSemaphore_Release);

// Purpose: releases a semaphore `count` times.
std::uint32_t __fastcall Win32_CSemaphore_Release(void* self, void* /*edx*/, std::uint32_t count) {
    const auto result = static_cast<std::uint32_t>(
        g_importReleaseSemaphore.get()(HandleOf(self), static_cast<std::int32_t>(count), nullptr));
    return Verify(result, texts::kVerifyReleaseSemaphore.c_str(), texts::kSynchronizeIpp.c_str(),
                  kLineReleaseSemaphore);
}

S3RE_HOOK(0x0060c530, Win32_CEvent_Create, Member4Fn, Original_Win32_CEvent_Create, Win32_CEvent_Create);

// Purpose: creates the Windows event of a CEvent.
std::uint32_t __fastcall Win32_CEvent_Create(void* self, void* /*edx*/, std::uint32_t security,
                                             std::uint32_t manualReset, std::uint32_t initialState,
                                             std::uint32_t name) {
    void* const handle = g_importCreateEventA.get()(reinterpret_cast<void*>(security), manualReset, initialState,
                                                     reinterpret_cast<const char*>(name));
    return KeepHandle(self, handle, texts::kVerifyEvent, kLineCreateEvent);
}

S3RE_HOOK(0x0060c7a0, Win32_CSemaphore_Create, Member4Fn, Original_Win32_CSemaphore_Create,
          Win32_CSemaphore_Create);

// Purpose: creates the Windows semaphore of a CSemaphore.
std::uint32_t __fastcall Win32_CSemaphore_Create(void* self, void* /*edx*/, std::uint32_t security,
                                                 std::uint32_t initial, std::uint32_t maximum, std::uint32_t name) {
    void* const handle = g_importCreateSemaphoreA.get()(reinterpret_cast<void*>(security),
                                                         static_cast<std::int32_t>(initial),
                                                         static_cast<std::int32_t>(maximum),
                                                         reinterpret_cast<const char*>(name));
    return KeepHandle(self, handle, texts::kVerifySemaphore, kLineCreateSemaphore);
}

S3RE_HOOK(0x0060ca30, Win32_CMutex_Create, Member3Fn, Original_Win32_CMutex_Create, Win32_CMutex_Create);

// Purpose: creates the Windows mutex of a CMutex.
std::uint32_t __fastcall Win32_CMutex_Create(void* self, void* /*edx*/, std::uint32_t security,
                                             std::uint32_t initialOwner, std::uint32_t name) {
    void* const handle = g_importCreateMutexA.get()(reinterpret_cast<void*>(security), initialOwner,
                                                     reinterpret_cast<const char*>(name));
    return KeepHandle(self, handle, texts::kVerifyMutex, kLineCreateMutex);
}

S3RE_HOOK(0x0060c060, Win32_CWin32Waitable_Wait, Member2Fn, Original_Win32_CWin32Waitable_Wait,
          Win32_CWin32Waitable_Wait);

// Purpose: waits for an event, thread or other handle; a timeout either
// throws CTimeout or returns false.
std::uint32_t __fastcall Win32_CWin32Waitable_Wait(void* self, void* /*edx*/, std::uint32_t timeout,
                                                   std::uint32_t throwOnTimeout) {
    const std::uint32_t result = g_importWaitForSingleObject.get()(HandleOf(self), timeout);
    if (result == kWaitTimeout) {
        if ((throwOnTimeout & 0xFF) == 0) {
            return result & 0xFFFFFF00u;  // AL = false over WAIT_TIMEOUT
        }
        reinterpret_cast<ThrowTimeoutFn>(kThrowTimeout)(texts::kWaitForTimeout.c_str(),
                                                        texts::kSynchronizeFile.c_str(), kLineWaitTimeout);
        return 1;  // not reached (the original returns true after the throw call)
    }
    std::uint32_t eax = result;
    if (result != 0) {
        eax = g_importGetLastError.get()();
        if (eax != 0) {
            ThrowLastError(texts::kErrorCheck.c_str(), texts::kSynchronizeFile.c_str(), kLineWaitError);
        }
    }
    return (eax & 0xFFFFFF00u) | 1;  // AL = true
}

S3RE_HOOK(0x0060e9d0, Win32_CThread_SetPriority, MemberArgFn, Original_Win32_CThread_SetPriority,
          Win32_CThread_SetPriority);

// Purpose: sets a thread's scheduling priority.
std::uint32_t __fastcall Win32_CThread_SetPriority(void* self, void* /*edx*/, std::uint32_t priority) {
    const auto result = static_cast<std::uint32_t>(
        g_importSetThreadPriority.get()(HandleOf(self), static_cast<std::int32_t>(priority)));
    return Verify(result, texts::kVerifyResult.c_str(), texts::kClassesFile.c_str(), kLineSetPriority);
}

S3RE_HOOK(0x0060f480, Win32_CMessagedThread_PostThreadMessage, Member3Fn,
          Original_Win32_CMessagedThread_PostThreadMessage, Win32_CMessagedThread_PostThreadMessage);

// Purpose: posts a message to a framework thread's message queue (e.g. the
// unhandled-exception report to the parent thread).
std::uint32_t __fastcall Win32_CMessagedThread_PostThreadMessage(void* self, void* /*edx*/, std::uint32_t message,
                                                                 std::uint32_t wParam, std::uint32_t lParam) {
    // The CThread part through the virtual-base table (entry 1: its offset).
    const auto* vbtable = *static_cast<const std::int32_t* const*>(self);
    const auto* thread =
        reinterpret_cast<const framework::CThread*>(static_cast<const std::uint8_t*>(self) + vbtable[1]);
    const int result = g_importPostThreadMessageA.get()(thread->threadId, message, wParam, lParam);
    if (result == 0 && g_importGetLastError.get()() != 0) {
        ThrowLastError(texts::kVerifySuccess.c_str(), texts::kClassesFile.c_str(), kLinePostThreadMessage);
    }
    return result != 0 ? 1 : 0;
}

S3RE_HOOK(0x0060f9b0, Win32_CWindowBase_PostMessage, Member3Fn, Original_Win32_CWindowBase_PostMessage,
          Win32_CWindowBase_PostMessage);

// Purpose: posts a message to a framework window.
std::uint32_t __fastcall Win32_CWindowBase_PostMessage(void* self, void* /*edx*/, std::uint32_t message,
                                                       std::uint32_t wParam, std::uint32_t lParam) {
    const auto result = static_cast<std::uint32_t>(g_importPostMessageA.get()(WindowOf(self), message, wParam, lParam));
    return Verify(result, texts::kVerifyRes.c_str(), texts::kClassesFile.c_str(), kLinePostMessage);
}

S3RE_HOOK(0x0060fbf0, Win32_CWindowBase_Destroy, MemberFn, Original_Win32_CWindowBase_Destroy,
          Win32_CWindowBase_Destroy);

// Purpose: destroys a framework window and forgets its handle.
std::uint32_t __fastcall Win32_CWindowBase_Destroy(void* self, void* /*edx*/) {
    const auto result = static_cast<std::uint32_t>(g_importDestroyWindow.get()(WindowOf(self)));
    Verify(result, texts::kVerifyDestroyWindow.c_str(), texts::kClassesFile.c_str(), kLineDestroyWindow);
    static_cast<framework::CWindowBase*>(self)->window = 0;
    return result;
}

// ---------------------------------------------------------------------------
// wip/menus-r5: CThread::Start, CWindowBase::Show, HasPendingMessage,
// CCriticalSection's constructor, DirectX::CGameThread::OnExit
// ---------------------------------------------------------------------------
//
// Win32::CThread::Start (0x0060e060): (parameter) ret 4: +0x24 = parameter;
//   the thread is created suspended with the CRT's _beginthreadex 0x62279a
//   (no security, 0x1000 bytes of stack, the thread procedure 0x60ec20 with
//   this as its argument, the thread id into +0x14); VERIFY(hThread !=
//   NULL) (Classes.cpp line 85); the handle is kept (CWin32Handle::Set
//   0x60bf00 on +4, owned), then ResumeThread(handle). EAX = ResumeThread's.
// Win32::CWindowBase::Show (0x0060fe40): ShowWindow(window +4, SW_SHOW).
//   EAX = its.
// Win32::HasPendingMessage (0x0060d9e0, no arguments): PeekMessageA(&msg,
//   0, 0, 0, PM_NOREMOVE); AL = result != 0 (the rest of EAX is the result's).
// Win32::CCriticalSection::CCriticalSection (0x0060d650):
//   InitializeCriticalSection(this) (the object is the CRITICAL_SECTION).
//   EAX = this.
// DirectX::CGameThread::OnExit (0x005b6470): CoUninitialize(), then the
//   counter +0x40 decremented (probably the COM initializations of the
//   thread). EAX = the new count.

namespace {

constexpr std::uintptr_t kBeginThreadEx = 0x0062279a;  // the CRT's _beginthreadex
constexpr std::uintptr_t kThreadProcedure = 0x0060ec20;  // Win32::CThread's thread procedure
constexpr std::uint32_t kThreadStackSize = 0x1000;
constexpr std::uint32_t kCreateSuspended = 4;   // CREATE_SUSPENDED
constexpr std::int32_t kShowWindow = 5;          // SW_SHOW
constexpr std::uint32_t kLineThreadStart = 85;   // Win32\Classes.cpp

using BeginThreadExFn = std::uint32_t(__cdecl*)(void* security, std::uint32_t stackSize, std::uintptr_t start,
                                                void* argument, std::uint32_t flags, std::uint32_t* threadId);

#pragma pack(push, 1)
struct GameThreadCounter {
    std::uint8_t unk00[0x40];
    std::int32_t comCount;  // +0x40 decremented by OnExit
};
#pragma pack(pop)
static_assert(offsetof(GameThreadCounter, comCount) == 0x40);

}  // namespace

std::uint32_t __fastcall Win32_CThread_Start(void* self, void* edx, std::uint32_t parameter);
std::uint32_t __fastcall Win32_CWindowBase_Show(void* self, void* edx);
std::uint32_t __cdecl Win32_HasPendingMessage();
void* __fastcall Win32_CCriticalSection_Construct(void* self, void* edx);
std::uint32_t __fastcall DirectX_CGameThread_OnExit(void* self, void* edx);

using HasPendingMessageFn = std::uint32_t(__cdecl*)();
using ConstructFn = void*(__fastcall*)(void* self, void* edx);

S3RE_HOOK(0x0060e060, Win32_CThread_Start, MemberArgFn, Original_Win32_CThread_Start, Win32_CThread_Start);
S3RE_HOOK(0x0060fe40, Win32_CWindowBase_Show, MemberFn, Original_Win32_CWindowBase_Show, Win32_CWindowBase_Show);
S3RE_HOOK(0x0060d9e0, Win32_HasPendingMessage, HasPendingMessageFn, Original_Win32_HasPendingMessage,
          Win32_HasPendingMessage);
S3RE_HOOK(0x0060d650, Win32_CCriticalSection_Construct, ConstructFn, Original_Win32_CCriticalSection_Construct,
          Win32_CCriticalSection_Construct);
S3RE_HOOK(0x005b6470, DirectX_CGameThread_OnExit, MemberFn, Original_DirectX_CGameThread_OnExit,
          DirectX_CGameThread_OnExit);

// Purpose: starts a framework thread (the game's logic, draw and network
// threads): creates it suspended, keeps its handle, then lets it run.
std::uint32_t __fastcall Win32_CThread_Start(void* self, void* /*edx*/, std::uint32_t parameter) {
    auto* thread = static_cast<framework::CThread*>(self);
    thread->unk24 = parameter;
    const std::uint32_t handle = reinterpret_cast<BeginThreadExFn>(kBeginThreadEx)(
        nullptr, kThreadStackSize, kThreadProcedure, self, kCreateSuspended, &thread->threadId);
    if (handle == 0) {
        ThrowLastError(texts::kThreadNotNull.c_str(), texts::kClassesFile.c_str(), kLineThreadStart);
    }
    reinterpret_cast<HandleSetFn>(kHandleSet)(&thread->waitable.handle, nullptr, reinterpret_cast<void*>(handle),
                                              kOwned);
    return g_importResumeThread.get()(reinterpret_cast<void*>(handle));
}

// Purpose: shows a framework window.
std::uint32_t __fastcall Win32_CWindowBase_Show(void* self, void* /*edx*/) {
    return static_cast<std::uint32_t>(g_importShowWindow.get()(WindowOf(self), kShowWindow));
}

// Purpose: whether the calling thread has a Windows message waiting (the
// message stays in the queue).
std::uint32_t __cdecl Win32_HasPendingMessage() {
    MSG message;
    const auto result = static_cast<std::uint32_t>(g_importPeekMessageA.get()(&message, nullptr, 0, 0, PM_NOREMOVE));
    return (result & 0xFFFFFF00u) | (result != 0 ? 1u : 0u);
}

// Purpose: constructor of the framework's critical section wrapper.
void* __fastcall Win32_CCriticalSection_Construct(void* self, void* /*edx*/) {
    g_importInitializeCriticalSection.get()(self);
    return self;
}

// Purpose: end of a DirectX game thread: undoes its COM initialization.
std::uint32_t __fastcall DirectX_CGameThread_OnExit(void* self, void* /*edx*/) {
    g_importCoUninitialize.get()();
    auto* thread = static_cast<GameThreadCounter*>(self);
    --thread->comCount;
    return static_cast<std::uint32_t>(thread->comCount);
}

}  // namespace s3re::game
