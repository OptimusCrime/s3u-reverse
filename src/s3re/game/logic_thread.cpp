// CLogicThread (see "Runtime structure" in CLAUDE.md).
//
// CLogicThread::Step (0x0053db30): one simulation step; vtable slot 9 of the
// thread vtable, called repeatedly by DirectX::CGameThread::Main. thiscall,
// no stack arguments, plain `ret` (EAX is not meaningful to the caller).
//
// Status: pass-through hook, used for the determinism trace and for
// S3RE_EXIT_AFTER_TICKS (`s3re_launcher --exit-after-ticks N`), which ends
// the process once the game tick reaches N. Not reimplemented.

#include "hooks.h"
#include "log.h"
#include "trace/determinism.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>

namespace s3re::game {

struct CLogicThread;  // opaque

namespace {

using StepFn = void(__fastcall*)(CLogicThread* self, void* edx);

void __fastcall Hooked_CLogicThread_Step(CLogicThread* self, void* edx);

S3RE_PASSTHROUGH(0x0053db30, CLogicThread_Step, StepFn, Original_CLogicThread_Step,
                 Hooked_CLogicThread_Step);

std::atomic<bool> g_loggedFirstStep{false};

constexpr std::uintptr_t kGameTickAddress = 0x007dfd48;  // g_gameTick

// S3RE_EXIT_AFTER_TICKS, or 0 if unset.
unsigned long ExitAfterTicks() {
    static const unsigned long value = [] {
        char text[32] = {};
        const DWORD length = GetEnvironmentVariableA("S3RE_EXIT_AFTER_TICKS", text, sizeof(text));
        return (length > 0 && length < sizeof(text)) ? std::strtoul(text, nullptr, 10) : 0UL;
    }();
    return value;
}

void __fastcall Hooked_CLogicThread_Step(CLogicThread* self, void* edx) {
    if (!g_loggedFirstStep.exchange(true)) {
        S3RE_LOG("CLogicThread::Step first call (this=%p)", static_cast<void*>(self));
    }
    Original_CLogicThread_Step(self, edx);
    trace::OnLogicStep(self);

    const unsigned long exitAfter = ExitAfterTicks();
    if (exitAfter != 0 &&
        *reinterpret_cast<const volatile std::uint32_t*>(kGameTickAddress) >= exitAfter) {
        // A clean shutdown from inside the logic thread isn't possible, so
        // finish our files and end the process.
        S3RE_LOG("tick %lu reached; ending the game (--exit-after-ticks)", exitAfter);
        trace::CloseDeterminismTrace();
        TerminateProcess(GetCurrentProcess(), 0);
    }
}

}  // namespace

}  // namespace s3re::game
