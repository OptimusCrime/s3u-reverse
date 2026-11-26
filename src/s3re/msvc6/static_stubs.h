// The compiler-generated static-initializer stubs (stubs-r5).
//
// MSVC 6 turns every global with a dynamic initializer into small
// functions and puts one of them per global into the CRT initializer
// table __xc_a..__xc_z (0x692000..0x695970), which _initterm (0x61f075)
// runs at start-up, before WinMain. For a global with a destructor there
// are four, each padded to 16 bytes:
//
//   entry   call init; jmp reg          (the table entry)
//   init    the constructor call or the initializer expression
//   reg     push dtor; call atexit; pop ecx; ret
//   dtor    mov ecx, global; jmp ~Class (run by exit() at shutdown)
//
// A global without a destructor has `jmp init` as its entry (a trivial
// forwarder, tools/trivial.py). tools/static_inits.py finds every stub
// from the table, classifies it by byte pattern and generates the
// static_stub_*.cpp files: one replacement and hook per stub, each a call
// of one of the helpers below with the operands read from its bytes.
// The helpers and the in-process tests are in static_stubs.cpp; each
// class's emulator specs are generated into tools/emutest/specs/msvc6.py.
//
// Our hooks are installed by DllMain (Detours loads s3re.dll before
// S3.EXE's entry point), so S3.EXE's _initterm already runs these.

#pragma once

#include <cstddef>
#include <cstdint>

namespace s3re::msvc6 {

// A stub as the hooks see it: cdecl, no arguments.
using StubFn = std::uint32_t(__cdecl*)();
using VoidStubFn = void(__cdecl*)();

constexpr std::uintptr_t kAtexit = 0x0061e812;            // atexit(void (*)()), cdecl
constexpr std::uintptr_t kStringFromChars = 0x00401a10;   // std::string(const char*, n), thiscall
constexpr std::uintptr_t kStringDestructor = 0x00401b60;  // std::string::~string, thiscall
constexpr std::uint32_t kNpos = 0xFFFFFFFF;               // the -1 the string stubs push as n

// entry: `call init; jmp reg`. Runs the initializer, then (tail call) the
// atexit registration; returns what the registration returns.
std::uint32_t CallThenJump(std::uintptr_t init, std::uintptr_t reg);

// reg: `push function; call atexit; pop ecx; ret`. Returns atexit's result.
std::uint32_t RegisterAtexit(std::uintptr_t function);

// dtor (and a few inits): `mov ecx, self; jmp function`, a tail call of a
// thiscall function without arguments.
std::uint32_t JumpWithThis(std::uintptr_t function, std::uintptr_t self);

// init: `push args (last first); mov ecx, self; call function; ret`, a
// constructor call with constant arguments; returns the constructor's EAX.
std::uint32_t CallWithThis(std::uintptr_t function, std::uintptr_t self);
std::uint32_t CallWithThis(std::uintptr_t function, std::uintptr_t self, std::uint32_t a1, std::uint32_t a2);

// init: `static std::string g = "text";` (the push -1; push text; mov ecx,
// g; call string(const char*, n) stub).
std::uint32_t StringFromChars(std::uintptr_t self, std::uintptr_t text);

// entry of a function-local static with an empty destructor: sets bit 0
// of the guard byte (the byte is stored only when the bit was clear),
// then tail-calls the registration.
std::uint32_t GuardThenJump(std::uintptr_t guard, std::uintptr_t reg);

// One unsigned 8- or 16-bit load of a float ratio initializer.
struct RatioLoad {
    std::uintptr_t address;
    std::uint32_t size;  // 1 or 2 bytes
};

// init: `static const float g = float(a1 - a0) / float(b1 - b0);` as x87
// code: fild of the int difference a1 - a0, fidiv by the int difference
// b1 - b0, fstp to the float. Done in double (CLAUDE.md "Floating point":
// x87 at 53 bits, then rounded to float on the store).
void FloatRatio(std::uintptr_t destination, RatioLoad a1, RatioLoad a0, RatioLoad b1, RatioLoad b0);

// init: `static std::string g = left + "text";` (static_string_inits.cpp).
void ConcatInit(std::uintptr_t destination, std::uintptr_t left, std::uintptr_t text);

// A generated stub, for the in-process tests: its name and address, the
// original (trampoline) and our replacement, and its operands (per class:
// entry init/reg, reg function, dtor function/self, string self/text,
// float destination, concat destination/left, guard guard/reg).
struct StubEntry {
    const char* name;
    std::uintptr_t address;
    const void* original;  // the Original_ pointer (StubFn* or VoidStubFn*)
    const void* ours;      // our replacement (StubFn or VoidStubFn)
    std::uintptr_t operand0;
    std::uintptr_t operand1;
};

struct StubTable {
    const StubEntry* entries;
    std::size_t count;
};

// The generated tables (static_stub_*.cpp).
extern const StubTable kEntryStubs;     // call_jump
extern const StubTable kAtexitStubs;    // atexit_reg
extern const StubTable kDtorStubs;      // this_jump, role dtor
extern const StubTable kStringStubs;    // this_call of std::string(const char*, n)
extern const StubTable kFloatStubs;     // float_ratio
extern const StubTable kConcatStubs;    // concat_init (the ones wip/logic-r4 did not do)
extern const StubTable kGuardStubs;     // guarded_reg

}  // namespace s3re::msvc6
