#pragma once

// Resources in the ground: the values of Tile::resources.
// Part of game/enums.h (the umbrella; see its header comment for the
// conventions of all enums). Evidence: re/enums.md ("Resources").

#include <cstdint>

#include "game/enums/value.h"

namespace s3re::game {

// ---------------------------------------------------------------- Tile::resources
// The high nibble is the kind, the low nibble the amount (0..15). Fish are
// kind 0 (they live in water tiles, UpdateEnvironment moves them); the
// others are what the mines dig (MinerJobDig) and the geologist finds.
constexpr std::uint8_t kResourceKindMask = 0xf0;
constexpr std::uint8_t kResourceAmountMask = 0x0f;
constexpr std::uint8_t kResourceMaxAmount = 15;

constexpr std::uint8_t kResourceFish = 0x00;
constexpr std::uint8_t kResourceCoal = 0x10;
constexpr std::uint8_t kResourceIron = 0x20;
constexpr std::uint8_t kResourceGold = 0x30;
constexpr std::uint8_t kResourceGems = 0x40;
constexpr std::uint8_t kResourceSulfur = 0x50;

}  // namespace s3re::game
