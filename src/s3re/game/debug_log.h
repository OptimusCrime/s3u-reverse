#pragma once

// Calls the game's DebugLog (0x00428720, see debug_log.cpp) the way an
// original function does, so that replacements keep the original's
// diagnostics: in the release build it is a bare `ret`; with our DLL its
// passthrough writes the message to s3re.log. `format` is the address of
// the original's format string in S3.EXE.

#include <cstdint>

namespace s3re::game {

template <typename... Args>
void GameDebugLog(std::uintptr_t format, Args... args) {
    using DebugLogFn = void(__cdecl*)(const char* format, ...);
    reinterpret_cast<DebugLogFn>(0x00428720)(reinterpret_cast<const char*>(format), args...);
}

}  // namespace s3re::game
