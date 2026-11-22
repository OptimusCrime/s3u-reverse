#pragma once

// The simulation's random number generator (see random.cpp).

namespace s3re::game {

// The seed most recently given to CGameData::SeedRandom (after --seed was
// applied), i.e. once a game runs, the seed it started from; -1 before any
// call. Only the low 16 bits are used by the game.
long LastSeed();

}  // namespace s3re::game
