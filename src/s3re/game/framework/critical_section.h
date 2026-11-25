#pragma once

// The game's scoped lock on a Win32::CCriticalSection (a CRITICAL_SECTION):
// Win32::CLock::CLock (0x0060da00, thiscall (cs) ret 4: stores cs in the
// lock object and enters it through CCriticalSection::Enter 0x60d670) and
// Win32::CLock::~CLock (0x0060da20: leaves it through
// CCriticalSection::Leave 0x60d680). Replacements of functions that hold
// such a lock call the game's two functions, as the original does, so the
// game's other threads see the same locking.

#include <cstdint>

namespace s3re::game {

class GameLock {
public:
    explicit GameLock(void* criticalSection) {
        using ConstructFn = GameLock*(__fastcall*)(GameLock* self, void* edx, void* cs);
        reinterpret_cast<ConstructFn>(kConstruct)(this, nullptr, criticalSection);
    }
    ~GameLock() {
        using DestructFn = void(__fastcall*)(GameLock* self, void* edx);
        reinterpret_cast<DestructFn>(kDestruct)(this, nullptr);
    }
    GameLock(const GameLock&) = delete;
    GameLock& operator=(const GameLock&) = delete;

private:
    static constexpr std::uintptr_t kConstruct = 0x0060da00;  // Win32::CLock::CLock
    static constexpr std::uintptr_t kDestruct = 0x0060da20;   // Win32::CLock::~CLock
    void* criticalSection_ = nullptr;  // +0 (the game's CLock object: the critical section)
};
static_assert(sizeof(GameLock) == 4, "the game's CLock object is one pointer");

}  // namespace s3re::game
