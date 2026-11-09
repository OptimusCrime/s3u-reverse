#include "hooks.h"

#include "log.h"

#include <windows.h>
#include <detours.h>

#include <vector>

namespace s3re::hooks {
namespace {

struct Hook {
    const char* name;
    std::uintptr_t address;
    HookKind kind;
    void** original;
    void* replacement;
    std::atomic<unsigned long>* calls;
};

// Function-local static so registration from other translation units' static
// initializers is safe regardless of initialization order.
std::vector<Hook>& Registry() {
    static std::vector<Hook> hooks;
    return hooks;
}

bool g_installed = false;

}  // namespace

HookRegistrar::HookRegistrar(const char* name, std::uintptr_t address, HookKind kind,
                             void** original, void* replacement,
                             std::atomic<unsigned long>* calls) {
    Registry().push_back({name, address, kind, original, replacement, calls});
}

void LogCallCounts() {
    if (!g_installed) {
        return;
    }
    for (const Hook& hook : Registry()) {
        if (hook.calls == nullptr || !IsInstalled(hook.kind)) {
            continue;  // not counted, or not installed
        }
        S3RE_LOG("calls %s %lu", hook.name, hook.calls->load(std::memory_order_relaxed));
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
        if (IsInstalled(hook.kind)) {
            hooks.push_back(hook);
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
        if (IsInstalled(hook.kind)) {
            DetourDetach(hook.original, hook.replacement);
        }
    }
    DetourTransactionCommit();
    g_installed = false;
}

}  // namespace s3re::hooks
