#pragma once

// Calls the game's DebugLog (0x00428720, see debug_log.cpp) the way an
// original function does, so that replacements keep the original's
// diagnostics: in the release build it is a bare `ret`; with our DLL its
// passthrough writes the message to s3re.log. `format` is the address of
// the original's format string in S3.EXE (declared in game/texts.h).

#include "game/global_access.h"

#include <cstdint>
#include <vector>

namespace s3re::game {

template <typename... Args>
void GameDebugLog(const GameText& format, Args... args) {
    using DebugLogFn = void(__cdecl*)(const char* format, ...);
    reinterpret_cast<DebugLogFn>(0x00428720)(format.c_str(), args...);
}

// For tests: while a capture is set, every DebugLog call with a format in
// S3.EXE's data (from original code or ours) is appended to it: the format
// address and as many argument dwords as the format has conversions. Not
// thread-safe; only for --test mode.
constexpr std::uint32_t kDebugLogCaptureArgs = 16;

struct DebugLogCall {
    std::uint32_t format;
    std::uint32_t count;
    std::uint32_t args[kDebugLogCaptureArgs];

    bool operator==(const DebugLogCall& other) const {
        if (format != other.format || count != other.count) {
            return false;
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            if (args[i] != other.args[i]) {
                return false;
            }
        }
        return true;
    }
    bool operator!=(const DebugLogCall& other) const { return !(*this == other); }
};

void SetDebugLogCapture(std::vector<DebugLogCall>* calls);

// Captures the DebugLog calls made during its lifetime.
class DebugLogRecorder {
public:
    DebugLogRecorder() { SetDebugLogCapture(&calls_); }
    ~DebugLogRecorder() { SetDebugLogCapture(nullptr); }
    DebugLogRecorder(const DebugLogRecorder&) = delete;
    DebugLogRecorder& operator=(const DebugLogRecorder&) = delete;

    // The calls since the last Take.
    std::vector<DebugLogCall> Take() {
        std::vector<DebugLogCall> calls;
        calls.swap(calls_);
        return calls;
    }

private:
    std::vector<DebugLogCall> calls_;
};

}  // namespace s3re::game
