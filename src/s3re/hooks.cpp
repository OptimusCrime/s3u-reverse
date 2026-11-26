#include "hooks.h"

#include "log.h"

#include <windows.h>
#include <detours.h>

#include <cstring>
#include <string>
#include <vector>

namespace s3re::hooks {
namespace {

struct Hook {
    const char* name;
    std::uintptr_t address;
    HookKind kind;
    void** original;
    void* replacement;
    CallStats* stats;
};

// Function-local static so registration from other translation units' static
// initializers is safe regardless of initialization order.
std::vector<Hook>& Registry() {
    static std::vector<Hook> hooks;
    return hooks;
}

bool g_installed = false;

// Replacements left out by name (`s3re_launcher --skip-hooks A,B`,
// S3RE_SKIP_HOOKS): for finding which replacement makes a whole game
// diverge, and for letting the original code call our replacements of its
// callees (e.g. the original GenerateRandomMap calling our ExpandTerrain, so those
// calls are counted). Only applies to Replaced and Unverified hooks.
bool SkippedByName(const char* name) {
    static const std::string list = [] {
        char text[4096] = {};
        const DWORD length = GetEnvironmentVariableA("S3RE_SKIP_HOOKS", text, sizeof(text));
        return (length > 0 && length < sizeof(text)) ? "," + std::string(text, length) + ","
                                                     : std::string();
    }();
    if (list.empty()) {
        return false;
    }
    if (list.find("," + std::string(name) + ",") != std::string::npos) {
        return true;
    }
    // An entry ending in '*' leaves out every hook whose name starts with
    // the text before it (e.g. "StaticInit_*": the static-initializer stubs).
    for (std::size_t start = 1; start < list.size();) {
        const std::size_t end = list.find(',', start);
        if (end == std::string::npos) {
            break;
        }
        if (end > start && list[end - 1] == '*' &&
            std::strncmp(name, list.c_str() + start, end - 1 - start) == 0) {
            return true;
        }
        start = end + 1;
    }
    return false;
}

bool Installed(const Hook& hook) {
    if (!IsInstalled(hook.kind)) {
        return false;
    }
    return !((hook.kind == HookKind::Replaced || hook.kind == HookKind::Unverified) &&
             SkippedByName(hook.name));
}

}  // namespace

HookRegistrar::HookRegistrar(const char* name, std::uintptr_t address, HookKind kind,
                             void** original, void* replacement,
                             CallStats* stats) {
    Registry().push_back({name, address, kind, original, replacement, stats});
}

bool FindHook(const char* name, std::uintptr_t* address, const void** original) {
    for (const Hook& hook : Registry()) {
        if (std::strcmp(hook.name, name) == 0) {
            *address = hook.address;
            *original = *hook.original;
            return true;
        }
    }
    return false;
}

void LogCallCounts() {
    if (!g_installed) {
        return;
    }
    for (const Hook& hook : Registry()) {
        if (hook.stats == nullptr || !Installed(hook)) {
            continue;  // not counted, or not installed
        }
        S3RE_LOG("calls %s %lu", hook.name, hook.stats->calls.load(std::memory_order_relaxed));
    }
    for (const Hook& hook : Registry()) {
        if (hook.stats == nullptr || !Installed(hook)) {
            continue;
        }
        const std::uint32_t tick = hook.stats->firstTick.load(std::memory_order_relaxed);
        if (tick != CallStats::kNotCalled) {
            S3RE_LOG("first-call %s %lu", hook.name, static_cast<unsigned long>(tick));
        }
    }
}

bool OriginalMode() {
    static const bool value = GetEnvironmentVariableA("S3RE_ORIGINAL", nullptr, 0) > 0;
    return value;
}

bool UnverifiedMode() {
    static const bool value = GetEnvironmentVariableA("S3RE_UNVERIFIED", nullptr, 0) > 0;
    return value;
}

bool IsInstalled(HookKind kind) {
    switch (kind) {
        case HookKind::Replaced:
            return !OriginalMode();
        case HookKind::Unverified:
            return !OriginalMode() && UnverifiedMode();
        default:
            return true;
    }
}

bool InstallAll() {
    std::vector<Hook> hooks;
    unsigned unverifiedLeftOut = 0;
    for (const Hook& hook : Registry()) {
        if (Installed(hook)) {
            hooks.push_back(hook);
        } else if (IsInstalled(hook.kind)) {
            S3RE_LOG("skipped %s at 0x%08x (--skip-hooks)", hook.name,
                     static_cast<unsigned>(hook.address));
        } else if (hook.kind == HookKind::Unverified && !OriginalMode()) {
            ++unverifiedLeftOut;
        }
    }
    if (OriginalMode()) {
        S3RE_LOG("original mode: %u replacement(s) left out",
                 static_cast<unsigned>(Registry().size() - hooks.size()));
    } else if (unverifiedLeftOut > 0) {
        S3RE_LOG("%u unverified replacement(s) left out (use --unverified to install them)",
                 unverifiedLeftOut);
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    for (const Hook& hook : hooks) {
        const LONG error = DetourAttach(hook.original, hook.replacement);
        if (error != NO_ERROR) {
            S3RE_LOG("hook %s at 0x%08x: DetourAttach failed (error %ld)", hook.name,
                     static_cast<unsigned>(hook.address), error);
            DetourTransactionAbort();
            return false;
        }
    }
    const LONG error = DetourTransactionCommit();
    if (error != NO_ERROR) {
        S3RE_LOG("hook transaction commit failed (error %ld)", error);
        return false;
    }

    for (const Hook& hook : hooks) {
        S3RE_LOG("hooked %s at 0x%08x (%s)", hook.name, static_cast<unsigned>(hook.address),
                 hook.kind == HookKind::Passthrough  ? "passthrough"
                 : hook.kind == HookKind::Unverified ? "replaced, unverified"
                 : OriginalMode()                    ? "replaced, calls the original"
                                                     : "replaced");
    }
    S3RE_LOG("%u hook(s) installed", static_cast<unsigned>(hooks.size()));
    g_installed = true;
    return true;
}

void UninstallAll() {
    if (!g_installed) {
        return;
    }
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    for (const Hook& hook : Registry()) {
        if (Installed(hook)) {
            DetourDetach(hook.original, hook.replacement);
        }
    }
    DetourTransactionCommit();
    g_installed = false;
}

}  // namespace s3re::hooks
