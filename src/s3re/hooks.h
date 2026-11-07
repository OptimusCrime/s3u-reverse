#pragma once

// Hook registry.
//
// Each reimplemented function registers itself with S3RE_HOOK. At startup all
// registered hooks are installed in one Detours transaction: calls to the
// original address are redirected to our replacement, and the "original"
// pointer is updated to a trampoline that still runs the original code (used
// for pass-through hooks and for differential testing).
//
// S3.EXE has no relocation table, so it always loads at its preferred base
// 0x400000 and the addresses from Ghidra can be used directly.

#include <cstdint>

namespace s3re::hooks {

struct HookRegistrar {
    HookRegistrar(const char* name, std::uintptr_t address, void** original,
                  void* replacement);
};

// Installs every registered hook. Returns false if any of them failed, in which
// case none are installed.
bool InstallAll();
void UninstallAll();

}  // namespace s3re::hooks

// Declares `original` as a pointer to the original function at `address` and
// registers `replacement` to be called instead.
//
//   S3RE_HOOK(0x0045a120, WinMain, WinMainFn, Original_WinMain, Hooked_WinMain);
#define S3RE_HOOK(address, name, FnType, original, replacement)                    \
    FnType original = reinterpret_cast<FnType>(address);                            \
    static ::s3re::hooks::HookRegistrar s3re_hook_registrar_##name{                 \
        #name, address, reinterpret_cast<void**>(&original),                        \
        reinterpret_cast<void*>(&replacement)}
