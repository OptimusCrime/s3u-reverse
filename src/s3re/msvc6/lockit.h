#pragma once

// std::_Lockit of S3.EXE's statically linked MSVC 6.0 library: the
// constructor (0x61e6e3) enters, the destructor (0x61e77f) leaves the one
// critical section that guards the shared _Nil nodes of every std::_Tree
// (made on first use; 0x7e9340 is its init state). MSVC 6's <xtree> takes
// it in most tree operations, also nested (_Min / _Max / _Lrotate /
// _Rrotate each take it again inside the caller's lock).
//
// Replacements of tree code call the game's own functions, in the
// original's order, so they share the lock with the original code still
// running in other threads. The emulator records the two calls as callee
// probes (specs/common.py LOCKIT_PROBES) and compares their order.

#include <cstdint>

namespace s3re::msvc6 {

inline constexpr std::uintptr_t kLockitEnter = 0x0061e6e3;  // std::_Lockit::_Lockit (thiscall)
inline constexpr std::uintptr_t kLockitLeave = 0x0061e77f;  // std::_Lockit::~_Lockit (thiscall)

// RAII like the original's local `_Lockit _Lk;`.
class GameLockit {
public:
    GameLockit() { reinterpret_cast<void(__fastcall*)(void*, void*)>(kLockitEnter)(this, nullptr); }
    ~GameLockit() { reinterpret_cast<void(__fastcall*)(void*, void*)>(kLockitLeave)(this, nullptr); }
    GameLockit(const GameLockit&) = delete;
    GameLockit& operator=(const GameLockit&) = delete;

private:
    std::uint8_t object_ = 0;  // the empty class's byte (never read)
};

}  // namespace s3re::msvc6
