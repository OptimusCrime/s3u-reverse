#pragma once

// Grid directions.
// Part of game/enums.h (the umbrella; see its header comment for the
// conventions of all enums). Evidence: re/enums.md.

#include <cstdint>

#include "game/enums/value.h"

namespace s3re::game {

// ---------------------------------------------------------------- directions
// The six neighbours of a tile, numbered 0..5 (pathing.h).
constexpr std::uint32_t kDirectionCount = 6;

}  // namespace s3re::game
