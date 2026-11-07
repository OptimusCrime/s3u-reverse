#pragma once

// Global variables of S3.EXE (see re/globals.csv for what is known about
// each). S3.EXE always loads at 0x400000, so their addresses are fixed.

#include <cstdint>

namespace s3re::game {

namespace global_address {
// int32: width and height of the game view in pixels (read by
// GameWorld::ScreenCenterTileX/Y).
constexpr std::uintptr_t kScreenWidth = 0x730f18;
constexpr std::uintptr_t kScreenHeight = 0x730f1c;
// int32: outcome of the current game: -1 while it is undecided, then set by
// the game-end check (0x511c50), e.g. 1 or 0 depending on the mission's
// goals.
constexpr std::uintptr_t kGameOutcome = 0x730f64;
}  // namespace global_address

template <typename T>
T& GameGlobal(std::uintptr_t address) {
    return *reinterpret_cast<T*>(address);
}

}  // namespace s3re::game
