#pragma once

// Race: the four peoples.
// Part of game/enums.h (the umbrella; see its header comment for the
// conventions of all enums). Evidence: re/enums.md.

#include <cstdint>

#include "game/enums/value.h"

namespace s3re::game {

// ---------------------------------------------------------------- Race
// Player::race (int32), Settler::race and Building::race (uint8; widened).
// Amazons = 3: a random race is Random() % 4 with the Amazons, else % 3
// (0x53cf96). 0..2 also fit the per-race buildings and goods: Romans offer
// wine at building 44, Egyptians beer at 42, Asians rice liquor at 46.
enum class Race : std::int32_t {
    Roman = 0,
    Egyptian = 1,
    Asian = 2,
    Amazon = 3,
};
constexpr std::int32_t kRaces = 4;

}  // namespace s3re::game
