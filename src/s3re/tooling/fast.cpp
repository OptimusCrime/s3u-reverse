// --fast: let the logic thread tick as fast as it can (S3RE_FAST).
//
// How the original paces the simulation (S3.EXE, verified by reading the
// code):
//   - CLogicThread::OnStart (0x53c9e0) starts the Win32::CTimer at
//     CLogicThread+0x3bc as a periodic 64 ms timer (0x60d3a0 with 0x40; a
//     waitable timer, or timeSetEvent on systems without one). That gives
//     the normal 15.6 ticks a second.
//   - CLogicThread::Step (0x53db30), when no fast-forward countdown is
//     running (g_stepCountdown 0x7dfd2c == 0), waits on its wait set at
//     CLogicThread+0x3dc (Win32::CWin32WaitableBag::WaitFor 0x463100,
//     thiscall, (timeout 500 ms, throw on timeout 1), returns the signalled
//     object). Only if the timer
//     object (+0x3bc) was signalled does it run the tick (ExecuteCommands,
//     then the world update). Other objects in the set (+0x3d8) wake it
//     without a tick; that is why Step runs about 240 times a second.
//   - The network side (CNetBounceThread::Step 0x46f6f0 in single player)
//     answers every step message at once; its "run up to step+5" allowance
//     only stops the logic if it gets ahead of the network thread.
//   - The game's own fast-forward (g_stepCountdown += 882 in ProcessEvents
//     0x524fa0, on some input event) is not used: while it runs, 0x7dfd30 is
//     set and UpdateEnvironment and others skip work (e.g. 0x4e82a2), so the
//     simulation would differ.
//
// What --fast changes: only the wait at the call in CLogicThread::Step
// (return address 0x53dc6d). It polls the wait set (timeout 0) instead of
// blocking; if nothing is signalled it reports the timer object as
// signalled, so Step runs a tick right away. If something is signalled, the
// original result is returned unchanged. Everything a tick does is the
// original code; only the number of non-tick steps between ticks changes.
// The draw thread keeps its own timer and simply draws fewer frames per
// tick.

#include "hooks.h"
#include "log.h"

#include <windows.h>

#include <intrin.h>

#include <cstdint>

namespace s3re::tooling {
namespace {

// Win32::CWin32WaitableBag<Win32::CWin32Waitable> (opaque): the wait set at
// CLogicThread+0x3dc.
struct CWin32WaitableBag;

using WaitFn = void*(__fastcall*)(CWin32WaitableBag* self, void* edx, unsigned timeoutMs,
                                  int throwOnTimeout);

void* __fastcall Hooked_CWin32WaitableBag_WaitFor(CWin32WaitableBag* self, void* edx, unsigned timeoutMs,
                                                  int throwOnTimeout);

S3RE_PASSTHROUGH(0x00463100, CWin32WaitableBag_WaitFor, WaitFn, Original_CWin32WaitableBag_WaitFor,
                 Hooked_CWin32WaitableBag_WaitFor);

constexpr std::uintptr_t kStepWaitReturn = 0x0053dc6d;  // after the call in Step
constexpr std::uintptr_t kWaitSetOffset = 0x3dc;        // CLogicThread -> wait set
constexpr std::uintptr_t kTimerOffset = 0x3bc;          // CLogicThread -> CTimer*

bool FastMode() {
    static const bool value = GetEnvironmentVariableA("S3RE_FAST", nullptr, 0) > 0;
    return value;
}

bool g_logged = false;  // only touched by the logic thread

void* __fastcall Hooked_CWin32WaitableBag_WaitFor(CWin32WaitableBag* self, void* edx, unsigned timeoutMs,
                                                  int throwOnTimeout) {
    if (!FastMode() ||
        reinterpret_cast<std::uintptr_t>(_ReturnAddress()) != kStepWaitReturn) {
        return Original_CWin32WaitableBag_WaitFor(self, edx, timeoutMs, throwOnTimeout);
    }
    const auto logic = reinterpret_cast<std::uintptr_t>(self) - kWaitSetOffset;
    if (!g_logged) {
        g_logged = true;
        S3RE_LOG("fast: logic thread %p ticks without waiting for its 64 ms timer",
                 reinterpret_cast<void*>(logic));
    }
    // Poll; a timeout returns null (no throw with throwOnTimeout = 0).
    void* signalled = Original_CWin32WaitableBag_WaitFor(self, edx, 0, 0);
    if (signalled != nullptr) {
        return signalled;
    }
    return *reinterpret_cast<void**>(logic + kTimerOffset);
}

}  // namespace
}  // namespace s3re::tooling
