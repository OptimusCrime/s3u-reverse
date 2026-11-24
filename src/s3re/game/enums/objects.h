#pragma once

// Landscape objects: the values of Tile::object.
// Part of game/enums.h (the umbrella; see its header comment for the
// conventions of all enums). Evidence: re/enums.md ("Landscape objects").

#include <cstdint>

#include "game/enums/value.h"

namespace s3re::game {

// ---------------------------------------------------------------- Tile::object
// Constants rather than an enum: most objects are ranges whose value counts
// a stage (a tree's kind and variant, a crop's growth, a felled tree's
// decay), and the code steps them with + and -. Hex, as the ranges read
// better that way; decimal in the comment where the code compares decimal.
constexpr std::uint8_t kObjectNone = 0;

// Trees: 8 kinds x 2 variants. A young tree becomes (stage + 0x4a) * 2 + a
// random bit (UpdateEnvironment), which lands in 0x44..0x53.
constexpr std::uint8_t kObjectFirstTree = 0x44;
constexpr std::uint8_t kObjectLastTree = 0x53;
// The woodcutter, the beekeeper and the hives' growth (UpdateEnvironment)
// only look at 0x44..0x52: the second variant of the last kind is never cut
// (as the original).
constexpr std::uint8_t kObjectLastCuttableTree = 0x52;
constexpr std::uint8_t kObjectSapling = 0x54;  // planted by the forester and TreeCurse

// Crops: planted, growing one stage per landscape pass, ripe, then done
// (harvested or withered; the landscape pass clears it). SpellGrowTrees
// grows these three ranges.
constexpr std::uint8_t kObjectGrainSown = 0x55;
constexpr std::uint8_t kObjectGrainRipeFirst = 0x5a;
constexpr std::uint8_t kObjectGrainRipeLast = 0x5c;
constexpr std::uint8_t kObjectGrainDone = 0x5d;
constexpr std::uint8_t kObjectVinePlanted = 0x5e;
constexpr std::uint8_t kObjectVineRipeFirst = 0x63;
constexpr std::uint8_t kObjectVineRipeLast = 0x65;
constexpr std::uint8_t kObjectVineDone = 0x66;
constexpr std::uint8_t kObjectRicePlanted = 0x67;
constexpr std::uint8_t kObjectRiceRipeFirst = 0x6c;
constexpr std::uint8_t kObjectRiceRipeLast = 0x6e;
constexpr std::uint8_t kObjectRiceDone = 0x6f;

// Building sites.
constexpr std::uint8_t kObjectSiteCentre = 112;  // 0x70: the site's own tile
constexpr std::uint8_t kObjectStake = 113;       // 0x71: the outermost footprint tile per direction
constexpr std::uint8_t kObjectDockLine = 114;    // 0x72: a tile of a shipyard's or harbour's dock line

// Stones: 0x73..0x7e, each cut takes one step; 0x7f is used up.
constexpr std::uint8_t kObjectFirstStone = 0x73;
constexpr std::uint8_t kObjectLastStone = 0x7e;
constexpr std::uint8_t kObjectStoneUsedUp = 0x7f;

// Felled trees: (tree - 0x44) / 2 + 0x80 when the woodcutter fells it, + 8
// per cutting step; from 0xb8 the landscape pass adds 8 until 0xd0..0xd7,
// which it clears.
constexpr std::uint8_t kObjectFirstFelledTree = 0x80;
constexpr std::uint8_t kObjectFirstRottingTree = 0xb8;
constexpr std::uint8_t kObjectLastRottingTree = 0xcf;
constexpr std::uint8_t kObjectFirstFelledTreeGone = 0xd0;
constexpr std::uint8_t kObjectLastFelledTreeGone = 0xd7;

// Young trees: a sapling becomes 0xe0..0xe6, which steps down by 8; from
// g_objectAgeStart..0xdf it grows into a tree.
constexpr std::uint8_t kObjectLastGrowingTree = 0xdf;
constexpr std::uint8_t kObjectFirstYoungTree = 0xe0;
constexpr std::uint8_t kObjectLastYoungTree = 0xe7;

// Geologist signs: 0xe8 nothing found, 0xe9..0xf2 a resource (two per
// kind, small and large); the landscape pass removes them by chance.
constexpr std::uint8_t kObjectSignNothing = 0xe8;
constexpr std::uint8_t kObjectLastSign = 0xf2;

// Fire: 0xf3 catches, 0xf4 burns (SpellOnArea kind 32), 0xf5, then 0xf6
// burnt out (cleared and unblocked).
constexpr std::uint8_t kObjectFireStart = 0xf3;
constexpr std::uint8_t kObjectFireBurning = 0xf4;
constexpr std::uint8_t kObjectFireLast = 0xf5;
constexpr std::uint8_t kObjectFireOut = 0xf6;

// Beehives: set up at 247 (0xf7); with trees near they fill up to 253
// (0xfd), without they empty and vanish; 250..253 can be harvested.
constexpr std::uint8_t kObjectHive = 247;
constexpr std::uint8_t kObjectHiveFirstHarvest = 250;
constexpr std::uint8_t kObjectHiveFull = 253;

constexpr std::uint8_t kObjectCleared = 0xff;  // tiles after the door of some types (AddBuildingSite)

}  // namespace s3re::game
