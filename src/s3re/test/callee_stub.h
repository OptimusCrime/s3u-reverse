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
// Only for thiscall callees with 1..10 stack arguments (ret 4*N). The tests
// run before the game starts, so no other thread calls the patched code.

#include "game/world.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

namespace s3re::test {

constexpr int kMaxStubArgs = 10;

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
            Write(it->address, it->saved, 5);
        }
        Current() = nullptr;
    }
    CalleeStubs(const CalleeStubs&) = delete;
    CalleeStubs& operator=(const CalleeStubs&) = delete;

    // Replaces the thiscall function at `address` (N stack arguments) by a
    // recording stub; `effect` (optional) gives EAX and side effects.
    template <int N, std::uintptr_t Address>
    void Install(StubEffect effect = nullptr) {
        static_assert(N >= 1 && N <= kMaxStubArgs);
        Patch patch{Address, {}, std::move(effect)};
        std::memcpy(patch.saved, reinterpret_cast<const void*>(Address), 5);
        void* target = StubFor<Address>(std::make_index_sequence<N>{});
        std::uint8_t jump[5] = {0xe9};
        const std::uint32_t rel = reinterpret_cast<std::uint32_t>(target) - (static_cast<std::uint32_t>(Address) + 5);
        std::memcpy(jump + 1, &rel, 4);
        Write(Address, jump, 5);
        patches_.push_back(std::move(patch));
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
    struct Patch {
        std::uintptr_t address;
        std::uint8_t saved[5];
        StubEffect effect;
    };

    static CalleeStubs*& Current() {
        static CalleeStubs* current = nullptr;
        return current;
    }

    static void Write(std::uintptr_t address, const void* bytes, std::size_t size) {
        DWORD old = 0;
        VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(reinterpret_cast<void*>(address), bytes, size);
        VirtualProtect(reinterpret_cast<void*>(address), size, old, &old);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), size);
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
        const std::uint32_t values[] = {args...};
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
    ScopedJump(std::uintptr_t address, const void* target) : address_(address) {
        std::memcpy(saved_, reinterpret_cast<const void*>(address), 5);
        std::uint8_t jump[5] = {0xe9};
        const std::uint32_t rel = reinterpret_cast<std::uint32_t>(target) - (static_cast<std::uint32_t>(address) + 5);
        std::memcpy(jump + 1, &rel, 4);
        Write(jump);
    }
    ~ScopedJump() { Write(saved_); }
    ScopedJump(const ScopedJump&) = delete;
    ScopedJump& operator=(const ScopedJump&) = delete;

private:
    void Write(const void* bytes) {
        DWORD old = 0;
        VirtualProtect(reinterpret_cast<void*>(address_), 5, PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(reinterpret_cast<void*>(address_), bytes, 5);
        VirtualProtect(reinterpret_cast<void*>(address_), 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address_), 5);
    }

    std::uintptr_t address_;
    std::uint8_t saved_[5];
};

}  // namespace s3re::test
