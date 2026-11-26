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

#include "game/globals.h"

#include <atomic>
#include <cstdint>

namespace s3re::hooks {

// Replaced: our reimplementation runs instead of the original (verified;
// inventory status `verified`).
// Unverified: a reimplementation without verification evidence yet
// (inventory status `reimplemented`, hook `unverified`). Installed only with
// `s3re_launcher --unverified` (S3RE_UNVERIFIED), never in original mode, so
// default runs and whole-game checks use verified code only.
// Passthrough: observes and calls the original (tooling).
// ReplacedAlways: a replacement that also provides tooling (e.g. --seed), so
// it stays installed in original mode and must itself call the original
// code when OriginalMode() is true.
enum class HookKind { Replaced, Passthrough, ReplacedAlways, Unverified };

// Call statistics of a counted hook: how often it was called, and the game
// tick (g_gameTick) of its first call (kNotCalled if never).
struct CallStats {
    static constexpr std::uint32_t kNotCalled = 0xFFFFFFFFu;
    std::atomic<unsigned long> calls{0};
    std::atomic<std::uint32_t> firstTick{kNotCalled};
};

struct HookRegistrar {
    HookRegistrar(const char* name, std::uintptr_t address, HookKind kind, void** original,
                  void* replacement, CallStats* stats);
};

// Writes how often each installed hook was called to s3re.log ("calls
// <name> <count>"), so a whole-game check can tell which replacements the
// game actually exercised, and for each called one the tick of its first
// call ("first-call <name> <tick>"), which shows how long a run must be.
void LogCallCounts();

namespace detail {

// The game tick counter g_gameTick.
inline std::uint32_t CurrentGameTick() {
    return game::g_gameTick;
}

inline void CountCall(CallStats& stats) {
    if (stats.calls.fetch_add(1, std::memory_order_relaxed) == 0) {
        stats.firstTick.store(CurrentGameTick(), std::memory_order_relaxed);
    }
}

// Installed in place of the replacement: counts the call, then calls it.
// One specialization per calling convention; the thunk has exactly the
// replacement's signature, so registers and stack are passed through.
template <typename Fn, Fn Target>
struct Counting;

template <typename R, typename... A, R(__fastcall* Target)(A...)>
struct Counting<R(__fastcall*)(A...), Target> {
    static inline CallStats stats;
    static R __fastcall Call(A... args) {
        CountCall(stats);
        return Target(args...);
    }
};

template <typename R, typename... A, R(__stdcall* Target)(A...)>
struct Counting<R(__stdcall*)(A...), Target> {
    static inline CallStats stats;
    static R __stdcall Call(A... args) {
        CountCall(stats);
        return Target(args...);
    }
};

template <typename R, typename... A, R(__cdecl* Target)(A...)>
struct Counting<R(__cdecl*)(A...), Target> {
    static inline CallStats stats;
    static R __cdecl Call(A... args) {
        CountCall(stats);
        return Target(args...);
    }
};

// A variadic cdecl replacement (e.g. CTextSurface::DrawTextF 0x4a87d0):
// the thunk can't know how many variable arguments there are, so it passes
// on the first 16 dwords after the fixed ones. The caller pops its
// arguments (cdecl), and dwords past the real ones are only read from the
// caller's frame, so the replacement's va_list sees exactly the values the
// caller pushed.
template <typename R, typename... A, R(__cdecl* Target)(A..., ...)>
struct Counting<R(__cdecl*)(A..., ...), Target> {
    static inline CallStats stats;
    using W = std::uint32_t;
    static R __cdecl Call(A... args, W v0, W v1, W v2, W v3, W v4, W v5, W v6, W v7, W v8, W v9, W v10, W v11,
                          W v12, W v13, W v14, W v15) {
        CountCall(stats);
        return Target(args..., v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15);
    }
};

}  // namespace detail

// Installs every registered hook. Returns false if any of them failed, in which
// case none are installed. In original mode, Replaced hooks are skipped.
bool InstallAll();

// True when the game runs with the original code instead of our
// reimplementations (`s3re_launcher --original`, S3RE_ORIGINAL), for
// comparing whole games: replay the same recording in both modes and compare
// the determinism traces.
bool OriginalMode();

// True when unverified reimplementations are installed as well
// (`s3re_launcher --unverified`, S3RE_UNVERIFIED).
bool UnverifiedMode();

// Whether a hook of this kind is installed in the current mode.
bool IsInstalled(HookKind kind);

void UninstallAll();

// The hook registered as `name`: its address and its original pointer (the
// trampoline once installed), for tests that run a chain of hooked
// functions as original code. False if there is none.
bool FindHook(const char* name, std::uintptr_t* address, const void** original);

}  // namespace s3re::hooks

// Registers `installed` (the function Detours redirects to) for the hook;
// `stats` is its call statistics or nullptr.
#define S3RE_DETAIL_HOOK(address, name, kind, FnType, original, installed, stats)  \
    FnType original = reinterpret_cast<FnType>(address);                            \
    static ::s3re::hooks::HookRegistrar s3re_hook_registrar_##name{                 \
        #name, address, kind, reinterpret_cast<void**>(&original),                  \
        reinterpret_cast<void*>(installed), stats}

// Declares `original` as a pointer to the original function at `address` and
// registers our `replacement` to run instead of it, behind a counting thunk
// (detail::Counting).
//   S3RE_HOOK(0x00401f60, String_Eos, EosFn, Original_String_Eos, String_Eos);
#define S3RE_HOOK(address, name, FnType, original, replacement)                    \
    S3RE_DETAIL_HOOK(address, name, ::s3re::hooks::HookKind::Replaced, FnType,      \
                     original,                                                      \
                     (&::s3re::hooks::detail::Counting<FnType, &replacement>::Call), \
                     (&::s3re::hooks::detail::Counting<FnType, &replacement>::stats))

// A reimplementation that has no verification evidence yet: same as
// S3RE_HOOK, but only installed with --unverified. Switch it to S3RE_HOOK
// (and the inventory row to verified) once its tests exist and pass.
#define S3RE_HOOK_UNVERIFIED(address, name, FnType, original, replacement)         \
    S3RE_DETAIL_HOOK(address, name, ::s3re::hooks::HookKind::Unverified, FnType,    \
                     original,                                                      \
                     (&::s3re::hooks::detail::Counting<FnType, &replacement>::Call), \
                     (&::s3re::hooks::detail::Counting<FnType, &replacement>::stats))

// A replacement that also provides tooling and therefore stays installed in
// original mode; it must call `original` itself when OriginalMode() is true.
// Not counted: tooling hooks may log their caller with _ReturnAddress(),
// which a thunk would change.
#define S3RE_HOOK_ALWAYS(address, name, FnType, original, replacement)             \
    S3RE_DETAIL_HOOK(address, name, ::s3re::hooks::HookKind::ReplacedAlways, FnType, \
                     original, &replacement, nullptr)

// Same, for a wrapper that observes the original (for example logs its
// arguments) and then calls it. Not a reimplementation. Not counted.
#define S3RE_PASSTHROUGH(address, name, FnType, original, wrapper)                 \
    S3RE_DETAIL_HOOK(address, name, ::s3re::hooks::HookKind::Passthrough, FnType,   \
                     original, &wrapper, nullptr)
