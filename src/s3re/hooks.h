#pragma once

// Hook registry.
//
// Each reimplemented function registers itself with S3RE_HOOK (our version
// replaces the original) or S3RE_PASSTHROUGH (a wrapper that logs and calls the
// original). At startup all registered hooks are installed in one Detours
// transaction: calls to the original address go to our function, and the
// "original" pointer is updated to a trampoline that still runs the original
// code. Tests use that pointer to compare the original against our version.
//
// S3.EXE has no relocation table, so it always loads at its preferred base
// 0x400000 and the addresses from Ghidra can be used directly.
//
// Calling conventions (MSVC 6.0):
//   - free functions: __cdecl or __stdcall (WINAPI), as in the original.
//   - member functions are __thiscall (this in ECX, arguments on the stack,
//     callee pops). Write replacements, and declare the original pointer, as
//     __fastcall with a dummy second parameter, which then lands in the unused
//     EDX register:
//       void __fastcall Foo_Bar(Foo* self, void* /*edx*/, int arg);
//
// Every hook needs a row in re/functions.csv with a matching hook column;
// `python tools/inventory.py check` enforces this.

#include <cstdint>

namespace s3re::hooks {

enum class HookKind { Replaced, Passthrough };

struct HookRegistrar {
    HookRegistrar(const char* name, std::uintptr_t address, HookKind kind, void** original,
                  void* replacement);
};

// Installs every registered hook. Returns false if any of them failed, in which
// case none are installed.
bool InstallAll();
void UninstallAll();

}  // namespace s3re::hooks

#define S3RE_DETAIL_HOOK(address, name, kind, FnType, original, replacement)       \
    FnType original = reinterpret_cast<FnType>(address);                            \
    static ::s3re::hooks::HookRegistrar s3re_hook_registrar_##name{                 \
        #name, address, kind, reinterpret_cast<void**>(&original),                  \
        reinterpret_cast<void*>(&replacement)}

// Declares `original` as a pointer to the original function at `address` and
// registers our `replacement` to run instead of it.
//   S3RE_HOOK(0x00401f60, String_Eos, EosFn, Original_String_Eos, String_Eos);
#define S3RE_HOOK(address, name, FnType, original, replacement)                    \
    S3RE_DETAIL_HOOK(address, name, ::s3re::hooks::HookKind::Replaced, FnType,      \
                     original, replacement)

// Same, for a wrapper that observes the original (for example logs its
// arguments) and then calls it. Not a reimplementation.
#define S3RE_PASSTHROUGH(address, name, FnType, original, wrapper)                 \
    S3RE_DETAIL_HOOK(address, name, ::s3re::hooks::HookKind::Passthrough, FnType,   \
                     original, wrapper)
