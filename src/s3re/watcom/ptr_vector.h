#pragma once

// A vector of pointers from the Watcom container library (WCValVector /
// WCPtrVector style), as the game's UI code uses it for the children of a
// screen group. Layout (0x18 bytes):
//   +0x00 vtable; slot 0x04 is called with 0 after the vector grew on an
//         out-of-range access (probably the library's "length changed"
//         notification; its meaning doesn't matter here)
//   +0x04 (not used by this code)
//   +0x08 data: the entries (allocated with the game's operator new)
//   +0x0c capacity: entries allocated
//   +0x10 filled: entries that hold a value (copied on resize; signed)
//   +0x14 length: entries in use (the loops in the game run up to it)
// The exception hooks the library calls on errors are the shared empty
// function 0x59f200 (`ret`) in this build, so errors only change what the
// access returns: see Index.
//
// Functions (thiscall on the vector):
//   0x00429a00 resize(n): a new block of n entries (none if n == 0) gets the
//     first min(filled, n) entries (signed comparison), the old block is
//     freed, filled = that count, capacity = n. Returns 1, or 0 (and changes
//     nothing) if operator new returned null. `length` is left alone.
//   0x00437fe0 the same instantiation for another element type, which also
//     clamps `length` to n (unsigned comparison) afterwards.
//   0x00428ee0 index check for operator[](i): returns the index to use. In
//     range (0 <= i <= length - 1, signed): i. Out of range on a non-empty
//     vector: 0 if i < 0, else length - 1. On an empty vector: grows it to
//     one entry if its capacity is 0 (as resize(1); on failure returns 0 at
//     once), clamps length to 1, calls vtable slot 0x04 with 0, sets
//     length = 1 and returns 0. The game's UI code has the same check
//     inlined in many loops (see screen_element.cpp); all copies behave
//     like this one.

#include <cstdint>

namespace s3re::watcom {

struct PtrVector {
    std::uint32_t vtable;
    std::uint32_t unknown04;
    std::uint32_t* data;
    std::uint32_t capacity;
    std::int32_t filled;
    std::uint32_t length;
};
static_assert(sizeof(PtrVector) == 0x18, "must match the game's layout");

using ResizeFn = std::uint32_t(__fastcall*)(PtrVector* self, void* edx, std::uint32_t n);
using IndexFn = std::uint32_t(__fastcall*)(PtrVector* self, void* edx, std::int32_t index);

std::uint32_t __fastcall PtrVector_Resize(PtrVector* self, void* edx, std::uint32_t n);
std::uint32_t __fastcall PtrVector_ResizeClampLength(PtrVector* self, void* edx, std::uint32_t n);
std::uint32_t __fastcall PtrVector_Index(PtrVector* self, void* edx, std::int32_t index);

// The entry operator[](index) returns, after the index check above (which
// may grow the vector). The address is computed with 32-bit wrap-around, as
// the original's [data + index * 4] does.
std::uint32_t PtrVector_At(PtrVector* self, std::int32_t index);

}  // namespace s3re::watcom
