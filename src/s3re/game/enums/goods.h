#pragma once

// Goods.
// Part of game/enums.h (the umbrella; see its header comment for the
// conventions of all enums). Evidence: re/enums.md.

#include <cstdint>

#include "game/enums/value.h"

namespace s3re::game {

// ---------------------------------------------------------------- Good
// Pile::good, Settler::good, goods indexes of the area and player arrays.
// Confirmed by the buildings' inputs (UpdateEconomy) and the coal consumers
// table (0x659264).
enum class Good : std::uint8_t {
    Nothing = 0,      // a free pile record
    Board = 1,        // building material
    Stone = 2,        // building material
    Log = 3,          // sawmill input
    Bread = 4,        // mine food
    Coal = 5,
    GoldOre = 6,
    IronOre = 7,
    Fish = 8,         // mine food
    Grain = 9,
    Gold = 10,
    Iron = 11,
    Shovel = 12,
    Hammer = 13,
    Axe = 14,
    Pickaxe = 15,
    Saw = 16,
    FishingRod = 17,
    Sword = 18,       // barracks input 1
    Bow = 19,         // barracks input 2
    Spear = 20,       // barracks input 3
    Wine = 21,        // Roman offering
    Flour = 22,
    Pig = 23,
    Meat = 24,        // mine food
    Sulfur = 25,
    Water = 26,
    Rice = 27,
    Gems = 28,
    Beer = 29,        // Egyptian offering
    Gunpowder = 30,   // cannon workshop input
    Good31 = 31,
    Scythe = 32,
    RiceLiquor = 33,  // Asian offering
    Mead = 34,        // Amazon offering
    Honey = 35,
    Empty = 255,      // 0xff: a building's pile that holds no goods type yet
};
constexpr std::uint32_t kGoodCount = 36;

}  // namespace s3re::game
