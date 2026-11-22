#pragma once

// The game setup of a game started with `s3re_launcher --random-map`
// (random_map_start.cpp), for the command recorder (recording version 3).

#include "common/recording_format.h"

#include <cstdint>

namespace s3re::tooling {

// Number of start positions the map dialog gives a random map of this size
// (chunk 1, 0x4820fb): min(((size - 32)^2 >> 14) + 1, 20).
std::uint32_t StartPositions(std::uint32_t size);

// The setup the random-map start applied to the running game; flags 0 if
// the game was not started that way (or not yet).
recording::GameSetup AppliedGameSetup();

}  // namespace s3re::tooling
