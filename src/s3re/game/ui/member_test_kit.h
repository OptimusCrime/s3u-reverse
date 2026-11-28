#pragma once

// A small in-process test kit for UI members (Dirk\dialog.cpp, wip/dialog-r8).
//
// Each case runs the original (its trampoline) and ours on the same random
// buffer, set up alike before each run. `this` lies kThis bytes into the
// buffer, so fields on both sides of it (the virtual bases, the controls'
// own fields before their element part) are random bytes. Callees of
// S3.EXE are replaced for the test by recording stubs (test::PatchJump);
// virtual calls go to a fake vtable of recording stubs (FakeVtable writes
// it into the buffer; ElementVirtual... below). A stub records the callee,
// ECX and its arguments (pointers into the buffer as offsets, stack
// pointers as a tag), plus the first 16 bytes behind each of the first two
// arguments when they point into the buffer or the stack, and returns a
// value fixed by that call (or what the test's effect function returns).
// Compared: EAX (masked), the calls, the whole buffer.

#include "test/callee_stub.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace s3re::game::ui::kit {

constexpr std::size_t kBytes = 0x4000;
constexpr std::size_t kThis = 0x1800;
constexpr std::size_t kVtable = 0x3c00;    // 64 fake virtual slots
constexpr std::size_t kComVtable = 0x3d00;  // 64 fake COM slots (stdcall, interface + 1 argument)
constexpr std::size_t kVtable3 = 0x3e00;   // 64 fake virtual slots with 3 arguments each
constexpr std::size_t kVbTables = 0x3f00;  // room for vbtables (8 bytes each)
constexpr std::uint32_t kVirtualId = 0xf0000000u;  // callee id of slot s: kVirtualId + 4 * s
constexpr std::uint32_t kComId = 0xe0000000u;      // callee id of COM slot s: kComId + 4 * s
constexpr std::uint32_t kVirtual3Id = 0xd0000000u;  // callee id of 3-argument slot s: kVirtual3Id + 4 * s
constexpr std::uint32_t kStackTag = 0x57ac0000u;

struct Call {
    std::uint32_t callee;
    std::uint32_t self;
    std::uint32_t count;
    std::uint32_t args[8];
    std::uint8_t pointee[2][16];
    bool operator==(const Call& o) const { return std::memcmp(this, &o, sizeof(*this)) == 0; }
};

// (the call as recorded, raw arguments) -> EAX; null: a hash of the call.
using Effect = std::uint32_t (*)(const Call& call, const std::uint32_t* raw);

struct State {
    std::uintptr_t base = 0;
    std::vector<Call>* log = nullptr;
    Effect effect = nullptr;
};

inline State& Current() {
    static State state;
    return state;
}

inline bool OnStack(std::uint32_t value) {
    const NT_TIB* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    return value >= reinterpret_cast<std::uintptr_t>(tib->StackLimit) &&
           value < reinterpret_cast<std::uintptr_t>(tib->StackBase);
}

inline std::uint32_t Normalize(std::uint32_t value) {
    const std::uintptr_t base = Current().base;
    if (value - base < kBytes) {
        return 0xbe000000u + static_cast<std::uint32_t>(value - base);
    }
    return OnStack(value) ? kStackTag : value;
}

inline std::uint32_t Record(std::uint32_t callee, std::uint32_t self, const std::uint32_t* args, std::uint32_t count) {
    Call call{};
    call.callee = callee;
    call.self = Normalize(self);
    call.count = count;
    for (std::uint32_t i = 0; i < count && i < 8; ++i) {
        call.args[i] = Normalize(args[i]);
        if (i < 2 && (args[i] - Current().base < kBytes - 16 || (OnStack(args[i]) && OnStack(args[i] + 15)))) {
            std::memcpy(call.pointee[i], reinterpret_cast<const void*>(args[i]), 16);
        }
    }
    if (Current().log != nullptr) {
        Current().log->push_back(call);
    }
    if (Current().effect != nullptr) {
        return Current().effect(call, args);
    }
    std::uint32_t hash = 0x9e3779b9u ^ callee;
    hash = hash * 31 + call.self;
    for (std::uint32_t i = 0; i < count && i < 8; ++i) {
        hash = hash * 31 + call.args[i];
    }
    return hash ^ (hash >> 15);
}

template <std::size_t>
using Dword = std::uint32_t;

// thiscall callee (ECX recorded), N stack arguments popped.
template <std::uint32_t Id, std::size_t... I>
std::uint32_t __fastcall ThisStub(std::uint32_t self, void*, Dword<I>... args) {
    const std::uint32_t values[sizeof...(I) + 1] = {args..., 0};
    return Record(Id, self, values, sizeof...(I));
}

// stdcall callee (ECX not recorded).
template <std::uint32_t Id, std::size_t... I>
std::uint32_t __stdcall StdStub(Dword<I>... args) {
    const std::uint32_t values[sizeof...(I) + 1] = {args..., 0};
    return Record(Id, 0, values, sizeof...(I));
}

// cdecl callee (the caller pops).
template <std::uint32_t Id, std::size_t... I>
std::uint32_t __cdecl CdeclStub(Dword<I>... args) {
    const std::uint32_t values[sizeof...(I) + 1] = {args..., 0};
    return Record(Id, 0, values, sizeof...(I));
}

// fastcall callee (ECX, EDX recorded as the first two arguments).
template <std::uint32_t Id, std::size_t... I>
std::uint32_t __fastcall FastStub(std::uint32_t ecx, std::uint32_t edx, Dword<I>... args) {
    const std::uint32_t values[sizeof...(I) + 3] = {ecx, edx, args..., 0};
    return Record(Id, 0, values, sizeof...(I) + 2);
}

template <std::uint32_t Id, std::size_t... I>
const void* ThisStubFor(std::index_sequence<I...>) { return reinterpret_cast<const void*>(&ThisStub<Id, I...>); }
template <std::uint32_t Id, std::size_t... I>
const void* StdStubFor(std::index_sequence<I...>) { return reinterpret_cast<const void*>(&StdStub<Id, I...>); }
template <std::uint32_t Id, std::size_t... I>
const void* CdeclStubFor(std::index_sequence<I...>) { return reinterpret_cast<const void*>(&CdeclStub<Id, I...>); }
template <std::uint32_t Id, std::size_t... I>
const void* FastStubFor(std::index_sequence<I...>) { return reinterpret_cast<const void*>(&FastStub<Id, I...>); }

// The callee patches of one comparison; restored by the destructor.
class Stubs {
public:
    Stubs() = default;
    Stubs(const Stubs&) = delete;
    Stubs& operator=(const Stubs&) = delete;
    ~Stubs() {
        for (auto it = patched_.rbegin(); it != patched_.rend(); ++it) {
            test::UnpatchJump(*it);
        }
    }
    template <std::uint32_t Address, int N>
    Stubs& This() { return Patch(Address, ThisStubFor<Address>(std::make_index_sequence<N>{})); }
    template <std::uint32_t Address, int N>
    Stubs& Std() { return Patch(Address, StdStubFor<Address>(std::make_index_sequence<N>{})); }
    template <std::uint32_t Address, int N>
    Stubs& Cdecl() { return Patch(Address, CdeclStubFor<Address>(std::make_index_sequence<N>{})); }
    template <std::uint32_t Address, int N>
    Stubs& Fast() { return Patch(Address, FastStubFor<Address>(std::make_index_sequence<N>{})); }

private:
    Stubs& Patch(std::uint32_t address, const void* stub) {
        test::PatchJump(address, stub);
        patched_.push_back(address);
        return *this;
    }
    std::vector<std::uint32_t> patched_;
};

// Stack arguments of the CScreenElement virtual slots (by slot number;
// re/ui_classes.md): messages by value are 5 dwords.
inline constexpr int kSlotArgs[64] = {
    1, 1, 5, 1, 0, 0, 1, 0, 1, 5, 3, 2, 5, 5, 5, 5,  // 0..15
    5, 5, 5, 5, 5, 0, 5, 5, 5, 1, 1, 1, 2, 2, 0, 5,  // 16..31
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 32..63: unused, 1 argument
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

template <std::size_t Slot, std::size_t... I>
const void* VirtualStubN(std::index_sequence<I...>) {
    return reinterpret_cast<const void*>(&ThisStub<kVirtualId + 4 * Slot, I...>);
}

template <std::size_t... S>
void FillVtable(std::uint32_t* table, std::index_sequence<S...>) {
    ((table[S] = reinterpret_cast<std::uint32_t>(
          VirtualStubN<S>(std::make_index_sequence<static_cast<std::size_t>(kSlotArgs[S])>{}))),
     ...);
}

template <std::size_t... S>
void FillComVtable(std::uint32_t* table, std::index_sequence<S...>) {
    ((table[S] = reinterpret_cast<std::uint32_t>(&StdStub<kComId + 4 * S, 0, 1>)), ...);
}

template <std::size_t... S>
void FillVtable3(std::uint32_t* table, std::index_sequence<S...>) {
    ((table[S] = reinterpret_cast<std::uint32_t>(&ThisStub<kVirtual3Id + 4 * S, 0, 1, 2>)), ...);
}

// Writes the fake vtables (64 recording slots each) at kVtable, kComVtable
// and kVtable3.
inline void FakeVtable(std::uint8_t* buffer) {
    FillVtable(reinterpret_cast<std::uint32_t*>(buffer + kVtable), std::make_index_sequence<64>{});
    FillComVtable(reinterpret_cast<std::uint32_t*>(buffer + kComVtable), std::make_index_sequence<64>{});
    FillVtable3(reinterpret_cast<std::uint32_t*>(buffer + kVtable3), std::make_index_sequence<64>{});
}

// An object at `offset` whose virtual functions all take 3 arguments.
inline void FakeObject3(std::uint8_t* buffer, std::size_t offset);

// Writes a pointer to `target` (an offset in the buffer) at `offset`.
inline void Link(std::uint8_t* buffer, std::size_t offset, std::size_t target) {
    const auto p = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(buffer + target));
    std::memcpy(buffer + offset, &p, 4);
}

inline void Put(std::uint8_t* buffer, std::size_t offset, std::uint32_t value) {
    std::memcpy(buffer + offset, &value, 4);
}

// An object at `offset` whose vtable is the fake one.
inline void FakeObject(std::uint8_t* buffer, std::size_t offset) { Link(buffer, offset, kVtable); }
inline void FakeObject3(std::uint8_t* buffer, std::size_t offset) { Link(buffer, offset, kVtable3); }

// A COM interface at `offset` (its methods: interface + 1 argument, recorded).
inline void FakeComObject(std::uint8_t* buffer, std::size_t offset) { Link(buffer, offset, kComVtable); }

// A vbptr at `offset` leading to a vbtable (number `slot`) whose entry [1]
// puts the virtual base at `offset` + displacement (inside the buffer),
// with the fake vtable at that base.
inline std::size_t VbPtr(std::uint8_t* buffer, std::size_t offset, std::int32_t displacement, std::size_t slot) {
    Link(buffer, offset, kVbTables + 8 * slot);
    Put(buffer, kVbTables + 8 * slot + 4, static_cast<std::uint32_t>(displacement));
    const std::size_t base = offset + displacement;
    FakeObject(buffer, base);
    return base;
}

struct Run {
    std::uint32_t eax;
    std::vector<Call> calls;
};

template <typename CallFn>
Run RunOn(std::uint8_t* buffer, Effect effect, CallFn call) {
    Run run{};
    Current().base = reinterpret_cast<std::uintptr_t>(buffer);
    Current().log = &run.calls;
    Current().effect = effect;
    run.eax = Normalize(call(reinterpret_cast<std::uintptr_t>(buffer)));
    Current().log = nullptr;
    Current().effect = nullptr;
    return run;
}

// One member's comparison: setup(rng, buffer) prepares the buffer (after a
// random fill and the fake vtable), call(base, argumentRng) runs it.
template <typename Setup, typename CallA, typename CallB>
bool Compare(test::Context& ctx, const char* name, int cases, Setup setup, CallA original, CallB ours,
             std::uint32_t eaxMask = 0xffffffffu, Effect effect = nullptr) {
    static std::uint8_t buffer[kBytes], afterOriginal[kBytes];
    bool ok = true;
    for (int i = 0; ok && i < cases; ++i) {
        const std::uint64_t seed = ctx.rng().Next64();
        const std::uint64_t argumentSeed = ctx.rng().Next64();
        auto prepare = [&] {
            test::Rng fill(seed);
            fill.Fill(buffer, kBytes);
            FakeVtable(buffer);
            setup(fill, buffer);
        };
        prepare();
        const Run a = RunOn(buffer, effect, [&](std::uintptr_t base) {
            test::Rng args(argumentSeed);
            return original(base, args);
        });
        std::memcpy(afterOriginal, buffer, kBytes);
        prepare();
        const Run b = RunOn(buffer, effect, [&](std::uintptr_t base) {
            test::Rng args(argumentSeed);
            return ours(base, args);
        });
        if (!S3RE_EXPECT_EQ(a.eax & eaxMask, b.eax & eaxMask) || !S3RE_EXPECT_EQ(a.calls.size(), b.calls.size()) ||
            !S3RE_EXPECT(a.calls == b.calls) || !S3RE_EXPECT_MEM_EQ(afterOriginal, buffer, kBytes)) {
            ctx.Fail(__FILE__, __LINE__, "  %s case %d", name, i);
            ok = false;
        }
    }
    return ok;
}

inline std::uint32_t At(std::uintptr_t base, std::size_t offset) {
    return static_cast<std::uint32_t>(base + offset);
}

}  // namespace s3re::game::ui::kit
