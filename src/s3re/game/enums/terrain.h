#pragma once

// Terrain values of Tile::terrain.
// Part of game/enums.h (the umbrella; see its header comment for the
// conventions of all enums). Evidence: re/enums.md.

#include <cstdint>

#include "game/enums/value.h"

namespace s3re::game {

// ---------------------------------------------------------------- terrain
// Tile::terrain: the high nibble is the terrain class (re/structs.md "Tile"),
// so the values stay hex. Constants rather than an enum: the code masks the
// byte and compares classes and single values alike.
constexpr std::uint8_t kTerrainClassMask = 0xf0;

// Terrain classes (terrain & kTerrainClassMask). Desert, swamp and snow: the
// terrain chains of the land spells (NextTransformTerrain, spells_land.cpp)
// lead grass 0x10 -> 0x14 -> 0x41 -> desert 0x40, grass 0x10 -> 0x15 -> 0x51
// -> swamp 0x50 and mountain 0x20 -> 0x23 -> 0x81 -> snow 0x80.
constexpr std::uint8_t kTerrainClassWater = 0x00;     // 5..7 water (the water bodies' flood fill)
constexpr std::uint8_t kTerrainClassGrass = 0x10;     // building ground of every race; fields are sown here
constexpr std::uint8_t kTerrainClassMountain = 0x20;  // geologists work here; no wide regions
constexpr std::uint8_t kTerrainClassShore = 0x30;     // never levelled; the fisher stands here
constexpr std::uint8_t kTerrainClassDesert = 0x40;    // Egyptians may build here too
constexpr std::uint8_t kTerrainClassSwamp = 0x50;
constexpr std::uint8_t kTerrainClassStream = 0x60;    // the waterworker draws water here
constexpr std::uint8_t kTerrainClassSnow = 0x80;      // snowy mountains; resources like mountains (map generator)
constexpr std::uint8_t kTerrainClass90 = 0x90;        // Asians may build here too

// Single terrain values.
constexpr std::uint8_t kTerrainHarbourWater = 4;  // shipyard and harbour need it within 20 tiles
constexpr std::uint8_t kTerrainWater5 = 5;        // water a ferry can lie in (with 6)
constexpr std::uint8_t kTerrainWater6 = 6;
constexpr std::uint8_t kTerrainGrass = 0x10;     // what fields and worn paths turn back into
constexpr std::uint8_t kTerrainGrass18 = 0x12;   // probably grass variants: a worn path smooths
constexpr std::uint8_t kTerrainGrass19 = 0x13;   // them to plain grass (WearPath); with 0x18
constexpr std::uint8_t kTerrainSwampyGrass = 0x15;  // grass at a swamp's edge; rice is planted here
constexpr std::uint8_t kTerrainField = 0x16;     // a farmer's, rice farmer's or winegrower's field
constexpr std::uint8_t kTerrainGrass24 = 0x18;
constexpr std::uint8_t kTerrainWornPath = 0x1c;  // grass walked on often (WearPath); fades back to grass
constexpr std::uint8_t kTerrainShore = 48;       // 0x30: where settlers board a ferry

}  // namespace s3re::game
