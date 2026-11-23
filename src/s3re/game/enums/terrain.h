#pragma once

// Terrain values of Tile::terrain.
// Part of game/enums.h (the umbrella; see its header comment for the
// conventions of all enums). Evidence: re/enums.md.

#include <cstdint>

#include "game/enums/value.h"

namespace s3re::game {

// ---------------------------------------------------------------- terrain
// Tile::terrain: the high nibble is the terrain class (re/structs.md "Tile").
constexpr std::uint8_t kTerrainClassMask = 0xf0;
constexpr std::uint8_t kTerrainClassWater = 0x00;     // 5..7 water (the water bodies' flood fill)
constexpr std::uint8_t kTerrainClassMountain = 0x20;  // geologists work here; no wide regions
constexpr std::uint8_t kTerrainWater5 = 5;            // water a ferry can lie in (with 6)
constexpr std::uint8_t kTerrainWater6 = 6;
constexpr std::uint8_t kTerrainShore = 48;            // 0x30: where settlers board a ferry

}  // namespace s3re::game
