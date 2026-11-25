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

// Called by the GamePlayerSetup::Set hook (0x49f120, game_settings_players.cpp)
// before the record is set: while the -map start's NewGame runs with
// --race, a human player's (type 0) race becomes the chosen one and its
// race-given flag 1, as a chunk 2 race would; otherwise nothing changes.
void ApplyHumanRaceOverride(std::uint32_t type, std::uint32_t party, std::uint32_t& race,
                            std::uint32_t& raceGiven);

}  // namespace s3re::tooling
