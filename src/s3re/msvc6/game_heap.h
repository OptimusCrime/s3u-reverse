#pragma once

// The game's own operator new (0x61efb9) and operator delete (0x61e4ef),
// statically linked MSVC 6.0 CRT code in S3.EXE. Replacements call them
// where the original does, so memory comes from (and goes back to) the
// game's heap; the emulator tests model them as a bump allocator and
// compare the calls.

#include <cstdint>

namespace s3re::msvc6 {

inline constexpr std::uintptr_t kOperatorNew = 0x0061efb9;
inline constexpr std::uintptr_t kOperatorDelete = 0x0061e4ef;

inline void* GameOperatorNew(std::uint32_t size) {
    return reinterpret_cast<void*(__cdecl*)(std::uint32_t)>(kOperatorNew)(size);
}

// EAX after the call (some originals return it unchanged).
inline std::uint32_t GameOperatorDelete(void* block) {
    return reinterpret_cast<std::uint32_t(__cdecl*)(void*)>(kOperatorDelete)(block);
}

}  // namespace s3re::msvc6
