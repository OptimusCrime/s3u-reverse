#pragma once

// Recording stubs for callees in differential tests.
//
// Some replacements call original functions that another work package
// owns and that are not verified yet (e.g. ResetSettlerToIdle 0x50cf50).
// Running those on random test worlds would crash or hide differences, so a
// test can replace them for its duration: CalleeStubs::Install writes a
// `jmp` to a recording stub over the callee's first five bytes (also over a
// Detours jump, if the callee is hooked) and the destructor restores the
// bytes. Both the original function (through its trampoline) and ours then
// call the stub, which records (callee, ECX relative to the current world,
// arguments) and returns what the test's effect function returns (it may
// also change the world, e.g. set a settler's job, so that both versions
// see the same side effects). Tests compare the two call logs.
//
// Only for thiscall callees with 0..10 stack arguments (ret 4*N). The tests
// run before the game starts, so no other thread calls the patched code.
//
// A crashing test skips these destructors (the SEH guard in difftest.cpp
// doesn't unwind C++ objects), so every patch is also listed in
// LivePatches() until it is restored, and the test runner calls
// RestoreLeftoverPatches() after a crash: one crashing test must not leave
// stubs behind that later tests run into. Other test patches of S3.EXE
// memory (FakeClock's import entry in screen_element.cpp) go through
// PatchBytes/UnpatchBytes for the same reason.

#include "game/world.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <utility>
#include <vector>

namespace s3re::test {

constexpr int kMaxStubArgs = 10;

// Writes `size` bytes over code (made writable for the write).
inline void WriteCode(std::uintptr_t address, const void* bytes, std::size_t size) {
    DWORD old = 0;
    VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(reinterpret_cast<void*>(address), bytes, size);
    VirtualProtect(reinterpret_cast<void*>(address), size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), size);
}

// Up to 5 bytes written over S3.EXE code or data (a jump, an import
// entry), with the bytes they replaced.
constexpr std::size_t kMaxPatchBytes = 5;
struct LivePatch {
    std::uintptr_t address;
    std::size_t size;
    std::uint8_t saved[kMaxPatchBytes];
};

// The patches of CalleeStubs, ScopedJump and PatchBytes users that are not
// restored yet, oldest first.
inline std::vector<LivePatch>& LivePatches() {
    static std::vector<LivePatch> patches;
    return patches;
}

// Writes `size` bytes (at most kMaxPatchBytes) at `address` and lists the
// patch, so it is restored even after a crashed test.
inline void PatchBytes(std::uintptr_t address, const void* bytes, std::size_t size) {
    LivePatch patch{address, size, {}};
    std::memcpy(patch.saved, reinterpret_cast<const void*>(address), size);
    WriteCode(address, bytes, size);
    LivePatches().push_back(patch);
}

// Restores the newest live patch at `address`.
inline void UnpatchBytes(std::uintptr_t address) {
    auto& patches = LivePatches();
    for (auto it = patches.rbegin(); it != patches.rend(); ++it) {
        if (it->address == address) {
            WriteCode(address, it->saved, it->size);
            patches.erase(std::next(it).base());
            return;
        }
    }
}

// Writes `jmp target` over the 5 bytes at `address` and lists the patch.
inline void PatchJump(std::uintptr_t address, const void* target) {
    std::uint8_t jump[5] = {0xe9};
    const std::uint32_t rel = reinterpret_cast<std::uint32_t>(target) - (static_cast<std::uint32_t>(address) + 5);
    std::memcpy(jump + 1, &rel, 4);
    PatchBytes(address, jump, sizeof(jump));
}

inline void UnpatchJump(std::uintptr_t address) { UnpatchBytes(address); }

class CalleeStubs;
// After a crashed test: restores every patch still live (newest first) and
// forgets the crashed test's CalleeStubs. Returns how many were restored.
inline int RestoreLeftoverPatches();

struct StubCall {
    std::uintptr_t callee;
    std::uint32_t self;  // ECX minus the current world
    std::uint32_t args[10];
    bool operator==(const StubCall& other) const {
        return callee == other.callee && self == other.self &&
               std::memcmp(args, other.args, sizeof(args)) == 0;
    }
};

// (world, ECX, arguments) -> EAX.
using StubEffect = std::function<std::uint32_t(game::CGameData* world, std::uint32_t self, const std::uint32_t* args)>;

class CalleeStubs {
public:
    CalleeStubs() { Current() = this; }
    ~CalleeStubs() {
        for (auto it = patches_.rbegin(); it != patches_.rend(); ++it) {
            UnpatchJump(it->address);
        }
        Current() = nullptr;
    }
    CalleeStubs(const CalleeStubs&) = delete;
    CalleeStubs& operator=(const CalleeStubs&) = delete;

    // Replaces the thiscall function at `address` (N stack arguments) by a
    // recording stub; `effect` (optional) gives EAX and side effects.
    template <int N, std::uintptr_t Address>
    void Install(StubEffect effect = nullptr) {
        static_assert(N >= 0 && N <= kMaxStubArgs);
        PatchJump(Address, StubFor<Address>(std::make_index_sequence<N>{}));
        patches_.push_back(Patch{Address, std::move(effect)});
    }

    // The world the next calls run on (ECX is recorded relative to it, and
    // effects get it).
    void SetWorld(game::CGameData* world) { world_ = world; }
    std::vector<StubCall>& calls() { return calls_; }
    // Moves the recorded calls out (and clears the log).
    std::vector<StubCall> Take() {
        std::vector<StubCall> result;
        result.swap(calls_);
        return result;
    }

private:
    friend int RestoreLeftoverPatches();

    struct Patch {
        std::uintptr_t address;
        StubEffect effect;
    };

    static CalleeStubs*& Current() {
        static CalleeStubs* current = nullptr;
        return current;
    }

    static std::uint32_t Record(std::uintptr_t address, void* self, const std::uint32_t* args, std::size_t count) {
        CalleeStubs* stubs = Current();
        StubCall call{address,
                      static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(self) -
                                                 reinterpret_cast<std::uintptr_t>(stubs->world_)),
                      {}};
        for (std::size_t i = 0; i < count; ++i) {
            call.args[i] = args[i];
        }
        stubs->calls_.push_back(call);
        for (Patch& patch : stubs->patches_) {
            if (patch.address == address && patch.effect) {
                return patch.effect(stubs->world_, call.self, call.args);
            }
        }
        return 0;
    }

    // A __fastcall stub (ECX = this, EDX unused) with sizeof...(I) dword
    // stack arguments, which it pops like the thiscall callee.
    template <std::size_t>
    using Dword = std::uint32_t;
    template <std::uintptr_t Address, std::size_t... I>
    static std::uint32_t __fastcall Stub(void* self, void*, Dword<I>... args) {
        const std::uint32_t values[sizeof...(I) + 1] = {args..., 0};
        return Record(Address, self, values, sizeof...(I));
    }
    template <std::uintptr_t Address, std::size_t... I>
    static void* StubFor(std::index_sequence<I...>) {
        return reinterpret_cast<void*>(&Stub<Address, I...>);
    }

    std::vector<Patch> patches_;
    std::vector<StubCall> calls_;
    game::CGameData* world_ = nullptr;
};

// Sends calls of the function at `address` to `target` while it lives (a
// `jmp` over the first five bytes, restored afterwards). Used to run a chain
// of hooked functions entirely as original code: point each hooked address
// at its Original_* trampoline.
class ScopedJump {
public:
    ScopedJump(std::uintptr_t address, const void* target) : address_(address) { PatchJump(address, target); }
    ~ScopedJump() { UnpatchJump(address_); }
    ScopedJump(const ScopedJump&) = delete;
    ScopedJump& operator=(const ScopedJump&) = delete;

private:
    std::uintptr_t address_;
};

inline int RestoreLeftoverPatches() {
    auto& patches = LivePatches();
    const int count = static_cast<int>(patches.size());
    for (auto it = patches.rbegin(); it != patches.rend(); ++it) {
        WriteCode(it->address, it->saved, it->size);
    }
    patches.clear();
    // The crashed test's CalleeStubs is gone with its stack frame (its
    // vectors leak); calls of a stub address now reach the restored code.
    CalleeStubs::Current() = nullptr;
    return count;
}

}  // namespace s3re::test
