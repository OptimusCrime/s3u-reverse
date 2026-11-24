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
//
// Our replacements call DebugLog too, with the original's format string
// (game/debug_log.h). Those call sites lie in s3re.dll; they are logged as
// "s3re.dll+RVA", and the stack callers (return addresses in S3.EXE) tell
// which original call site reached the replacement.

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
constexpr std::uintptr_t kGameCodeEnd = 0x0063a000;    // raw-ok: section bound
constexpr std::uintptr_t kGameDataBegin = 0x0063a000;  // .rdata and .data; raw-ok: section bound
constexpr std::uintptr_t kGameDataEnd = 0x007ed000;
constexpr int kPerSite = 5;
constexpr int kSites = 1024;

SRWLOCK g_lock = SRWLOCK_INIT;

// Our own image (s3re.dll), found from an address inside it.
struct OwnImage {
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;
};

OwnImage FindOwnImage() {
    OwnImage image;
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&FindOwnImage), &module) &&
        module != nullptr) {
        const auto base = reinterpret_cast<const std::uint8_t*>(module);
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        image.begin = reinterpret_cast<std::uintptr_t>(base);
        image.end = image.begin + nt->OptionalHeader.SizeOfImage;
    }
    return image;
}

const OwnImage& Own() {
    static const OwnImage image = FindOwnImage();
    return image;
}
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

// Calls of the folded `ret` that are not DebugLog: the Watcom containers'
// element destroyers call an empty destructor (thiscall, ECX = the
// element) that the linker folded into DebugLog's bytes; their first stack
// slot is a saved register that may point into S3.EXE's data (e.g. the
// chunk cache's node deallocator 0x4a8f10, reached from
// CLogicThread::LoadSaveGame with EDI = 0x7ad4fc). Return addresses.
constexpr std::uintptr_t kFoldedDestructorSites[] = {
    0x00428310, 0x00429013, 0x00472ec6, 0x004a8f26, 0x005b8003, 0x005dfe53,
};

bool IsFoldedDestructorCall(std::uintptr_t site) {
    for (const std::uintptr_t folded : kFoldedDestructorSites) {
        if (site == folded) {
            return true;
        }
    }
    return false;
}

// `stack` is ESP at entry: [0] return address, [1] format, [2...] arguments.
void __stdcall LogMessage(const std::uint32_t* stack) {
    const std::uintptr_t site = stack[0];
    const std::uintptr_t formatAddress = stack[1];
    if (IsFoldedDestructorCall(site)) {
        return;
    }
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
    const bool fromGame = site >= kGameCodeBegin && site < kGameCodeEnd;
    const bool fromOurs = site >= Own().begin && site < Own().end;
    if ((!fromGame && !fromOurs) || formatAddress < kGameDataBegin ||
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
    if (fromGame) {
        S3RE_LOG("DebugLog from %08x: %s%s", static_cast<unsigned>(site),
                 ok ? message : "(could not format) ", ok ? "" : format);
    } else {
        S3RE_LOG("DebugLog from s3re.dll+%x (replacement): %s%s",
                 static_cast<unsigned>(site - Own().begin), ok ? message : "(could not format) ",
                 ok ? "" : format);
    }
    // A replacement is reached from many original call sites: log the
    // callers every time (the per-site limit still applies).
    if (previous == 0 || fromOurs) {
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
