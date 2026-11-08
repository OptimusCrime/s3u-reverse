// DebugLog (0x00428720): the game's printf-style debug log, compiled to a
// bare `ret` in the release build. 688 call sites pass German or English
// messages about unexpected states (e.g. "GFX: ShadowAnimation not found
// %u:%u" from the sprite lookup 0x5fabb0, whose bounds check does nothing
// else). This passthrough writes them to s3re.log, at most kPerSite times
// per call site, with the first few return addresses on the stack (the
// callers of the function that logs).
//
// The same bytes are also CThread::OnExit (linker folding): those calls are
// thiscall without a format string. Only calls whose first stack argument
// points into S3.EXE's data are formatted, and formatting is guarded by SEH
// (a %s argument could be a bad pointer).

#include "game/debug_log.h"
#include "hooks.h"
#include "log.h"

#include <windows.h>
#include <intrin.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kGameCodeBegin = 0x00401000;
constexpr std::uintptr_t kGameCodeEnd = 0x0063a000;
constexpr std::uintptr_t kGameDataBegin = 0x0063a000;  // .rdata and .data
constexpr std::uintptr_t kGameDataEnd = 0x007ed000;
constexpr int kPerSite = 5;
constexpr int kSites = 1024;

SRWLOCK g_lock = SRWLOCK_INIT;
std::uintptr_t g_siteAddress[kSites];
int g_siteCount[kSites];

// Returns how many times this site has logged before (and counts this one).
int CountSite(std::uintptr_t site) {
    AcquireSRWLockExclusive(&g_lock);
    int previous = -1;
    for (int i = (site >> 2) % kSites, probes = 0; probes < kSites; i = (i + 1) % kSites, ++probes) {
        if (g_siteAddress[i] == site || g_siteAddress[i] == 0) {
            g_siteAddress[i] = site;
            previous = g_siteCount[i]++;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return previous < 0 ? kPerSite : previous;  // table full: don't log
}

bool Format(char* buffer, std::size_t size, const char* format, va_list args) {
    __try {
        _vsnprintf_s(buffer, size, _TRUNCATE, format, args);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Return-address candidates above our frame (dword before is a call rel32).
// Stops at the end of the committed stack (a new thread's stack is short).
void LogCallers(const std::uint32_t* stack) {
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(stack, &info, sizeof(info)) == 0) {
        return;
    }
    const auto end = reinterpret_cast<const std::uint32_t*>(
        static_cast<const std::uint8_t*>(info.BaseAddress) + info.RegionSize);
    char text[160];
    int length = 0;
    int found = 0;
    for (const std::uint32_t* slot = stack; slot < end && slot < stack + 64 && found < 4; ++slot) {
        const std::uint32_t value = *slot;
        if (value >= kGameCodeBegin + 5 && value < kGameCodeEnd &&
            *reinterpret_cast<const std::uint8_t*>(value - 5) == 0xE8) {
            length += std::snprintf(text + length, sizeof(text) - length, " %08x", value);
            ++found;
        }
    }
    if (found > 0) {
        S3RE_LOG("DebugLog:   stack:%s", text);
    }
}

}  // namespace

using DebugLogFn = void(__cdecl*)(const char* format, ...);

namespace {

std::vector<DebugLogCall>* g_capture = nullptr;

// How many arguments a printf format consumes (conversions other than %%).
std::uint32_t CountConversions(const char* format) {
    std::uint32_t count = 0;
    for (const char* p = format; *p != 0; ++p) {
        if (*p != '%') {
            continue;
        }
        ++p;
        while (*p != 0 && std::strchr("-+ #0123456789.*hlL", *p) != nullptr) {
            ++p;
        }
        if (*p == 0) {
            break;
        }
        if (*p != '%') {
            ++count;
        }
    }
    return count;
}

// `stack` is ESP at entry: [0] return address, [1] format, [2...] arguments.
void __stdcall LogMessage(const std::uint32_t* stack) {
    const std::uintptr_t site = stack[0];
    const std::uintptr_t formatAddress = stack[1];
    if (g_capture != nullptr && formatAddress >= kGameDataBegin && formatAddress < kGameDataEnd) {
        DebugLogCall call{};
        call.format = static_cast<std::uint32_t>(formatAddress);
        call.count = CountConversions(reinterpret_cast<const char*>(formatAddress));
        if (call.count > kDebugLogCaptureArgs) {
            call.count = kDebugLogCaptureArgs;
        }
        for (std::uint32_t i = 0; i < call.count; ++i) {
            call.args[i] = stack[2 + i];
        }
        g_capture->push_back(call);
    }
    if (site < kGameCodeBegin || site >= kGameCodeEnd || formatAddress < kGameDataBegin ||
        formatAddress >= kGameDataEnd) {
        return;
    }
    const int previous = CountSite(site);
    if (previous >= kPerSite) {
        return;
    }
    const char* format = reinterpret_cast<const char*>(formatAddress);
    char message[512];
    const bool ok = Format(message, sizeof(message), format,
                           reinterpret_cast<va_list>(const_cast<std::uint32_t*>(stack + 2)));
    S3RE_LOG("DebugLog from %08x: %s%s", static_cast<unsigned>(site),
             ok ? message : "(could not format) ", ok ? "" : format);
    if (previous == 0) {
        LogCallers(stack + 1);
    }
}

}  // namespace

// The original is a bare `ret` that leaves every register as it was; the
// hook does the same (saves and restores all registers and flags), so no
// caller can notice it, whatever it assumes.
__declspec(naked) void __cdecl Hooked_DebugLog(const char* /*format*/, ...) {
    __asm {
        pushfd
        pushad
        lea eax, [esp + 36]  // ESP at entry (8 registers + flags)
        push eax
        call LogMessage
        popad
        popfd
        ret
    }
}

S3RE_PASSTHROUGH(0x00428720, DebugLog, DebugLogFn, Original_DebugLog, Hooked_DebugLog);

void SetDebugLogCapture(std::vector<DebugLogCall>* calls) {
    g_capture = calls;
}

}  // namespace s3re::game
