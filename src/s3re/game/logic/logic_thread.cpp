// CLogicThread (see "Runtime structure" in CLAUDE.md).
//
// CLogicThread::Step (0x0053db30): one simulation step; vtable slot 9 of the
// thread vtable, called repeatedly by DirectX::CGameThread::Main. thiscall,
// no stack arguments, plain `ret` (EAX is not meaningful to the caller).
//
// The hook provides tooling, so it is S3RE_HOOK_ALWAYS (installed in
// original mode too): the determinism trace, the command recorder (it
// registers the logic thread object) and S3RE_EXIT_AFTER_TICKS
// (`s3re_launcher --exit-after-ticks N`), which shuts the game down once the
// game tick reaches N (when the game was loaded from a savegame with
// `--load-save`, N ticks after the tick the savegame starts at). It runs the
// original Step in original mode and ours (CLogicThread_Step_Impl,
// logic_step.cpp, wip/logic-r8) otherwise.

#include "crash_log.h"
#include "game/globals.h"
#include "game/logic/logic_step.h"
#include "game/logic/logic_thread_layout.h"
#include "hooks.h"
#include "log.h"
#include "tooling/replay/command_log.h"
#include "tooling/trace/determinism.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>

namespace s3re::game {


namespace {

using StepFn = void(__fastcall*)(CLogicThread* self, void* edx);

void __fastcall Hooked_CLogicThread_Step(CLogicThread* self, void* edx);

S3RE_HOOK_ALWAYS(0x0053db30, CLogicThread_Step, StepFn, Original_CLogicThread_Step,
                 Hooked_CLogicThread_Step);

std::atomic<bool> g_loggedFirstStep{false};


// S3RE_EXIT_AFTER_TICKS, or 0 if unset.
unsigned long ExitAfterTicks() {
    static const unsigned long value = [] {
        char text[32] = {};
        const DWORD length = GetEnvironmentVariableA("S3RE_EXIT_AFTER_TICKS", text, sizeof(text));
        return (length > 0 && length < sizeof(text)) ? std::strtoul(text, nullptr, 10) : 0UL;
    }();
    return value;
}

bool g_exitRequested = false;  // only touched by the logic thread

// The tick --exit-after-ticks counts from: 0 for a new game, the first tick
// seen for a game loaded from a savegame (S3RE_LOAD_SAVE).
std::uint32_t ExitTickBase(std::uint32_t tick) {
    static const std::uint32_t base =
        GetEnvironmentVariableA("S3RE_LOAD_SAVE", nullptr, 0) > 0 ? tick : 0;
    return base;
}

BOOL CALLBACK CloseProcessWindow(HWND window, LPARAM) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == GetCurrentProcessId()) {
        PostMessageA(window, WM_CLOSE, 0, 0);
    }
    return TRUE;
}

DWORD WINAPI ShutdownWatchdog(LPVOID) {
    Sleep(15000);
    S3RE_LOG("clean shutdown did not finish within 15 s; terminating");
    crash::LogAllThreadStacks("hang");
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}

// Ask the game to quit the normal way (WM_CLOSE to its windows), so its
// threads run their OnExit handlers and finish their files (e.g. the
// `-record` event file). A watchdog kills the process if that hangs.
void RequestShutdown() {
    EnumWindows(CloseProcessWindow, 0);
    if (HANDLE thread = CreateThread(nullptr, 0, ShutdownWatchdog, nullptr, 0, nullptr)) {
        CloseHandle(thread);
    }
}

void __fastcall Hooked_CLogicThread_Step(CLogicThread* self, void* edx) {
    replay::SetLogicThread(self);
    if (!g_loggedFirstStep.exchange(true)) {
        S3RE_LOG("CLogicThread::Step first call (this=%p)", static_cast<void*>(self));
    }
    const std::uint32_t base =
        ExitTickBase(g_gameTick);
    if (hooks::OriginalMode()) {
        Original_CLogicThread_Step(self, edx);
    } else {
        CLogicThread_Step_Impl(self, edx);
    }
    trace::OnLogicStep(self);

    const unsigned long exitAfter = ExitAfterTicks();
    const std::uint32_t tick = g_gameTick;
    if (exitAfter != 0 && !g_exitRequested && tick - base >= exitAfter) {
        g_exitRequested = true;
        S3RE_LOG("tick %lu reached (%lu ticks after %lu); ending the game (--exit-after-ticks)",
                 static_cast<unsigned long>(tick), exitAfter, static_cast<unsigned long>(base));
        trace::CloseDeterminismTrace();
        replay::CloseCommandLog();
        RequestShutdown();
    }
}

}  // namespace

}  // namespace s3re::game
