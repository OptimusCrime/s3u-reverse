#pragma once

// Spells (god events).
// Part of game/enums.h (the umbrella; see its header comment for the
// conventions of all enums). Evidence: re/enums.md.

#include <cstdint>

#include "game/enums/value.h"

namespace s3re::game {

// ---------------------------------------------------------------- Spell
// The spells ("god events", Volker\GodlyEvents.cpp): the spell argument of
// ExecuteSpell (0x5480b0, a dword), HasManaForSpell and PayForSpell. Each
// race has eight (table 0x658d64): Romans 5, 3, 9, 10, 21, 18, 8, 11;
// Egyptians 2, 0, 19, 6, 22, 12, 13, 15; Asians 1, 4, 7, 23, 20, 16, 17, 14;
// Amazons 31, 30, 29, 28, 26, 27, 24, 25. Names from ExecuteSpell's
// handlers and its DebugLog texts ("xxx about to execute EventConvertGoods",
// ...); evidence in re/enums.md.
enum class Spell : std::uint32_t {
    Spell0 = 0,            // Egyptian; 0..3 share the handler 0x548340
    Spell1 = 1,            // Asian
    Spell2 = 2,            // Egyptian
    Spell3 = 3,            // Roman
    Spell4 = 4,            // Asian; handler 0x548fb0
    RevealSpot = 5,        // Roman: a reveal point for a while
    BurnTrees = 6,         // Egyptian: trees nearby catch fire
    AddFish = 7,           // Asian: fish in the waters nearby
    HalveResources = 8,    // Roman: halves the resources nearby
    GrowTrees = 9,         // Roman: young trees grow three stages
    Spell10 = 10,          // Roman: enemy soldiers get flag 1 (half damage); 10, 12, 16, 17, 24, 25: SpellMarkSoldiers 0x549f40
    Spell11 = 11,          // Roman: enemy units change sides (SpellConvertSettlers 0x54a370)
    Spell12 = 12,          // Egyptian: own soldiers get flag 2 (double damage)
    Spell13 = 13,          // Egyptian: enemy units near a settler move to a spot (SpellTeleportEnemies 0x54a7c0)
    Spell14 = 14,          // Asian: own units near a spot gather at a settler (SpellGatherUnits 0x54a540)
    Spell15 = 15,          // Egyptian: enemy bowmen become pioneers (SpellDisarmBowmen 0x54a1e0)
    Spell16 = 16,          // Asian: own soldiers get flag 8 (damage * 3/4)
    Spell17 = 17,          // Asian: own soldiers get flag 4 (the strong hit)
    ConvertGoodsRoman = 18,     // "EventConvertGoods" (handler 0x549bd0)
    ConvertGoodsEgyptian = 19,
    ConvertGoodsAsian = 20,
    GodlyGoodsRoman = 21,       // "EventGodlyGoods" (handler 0x5499b0)
    GodlyGoodsEgyptian = 22,
    GodlyGoodsAsian = 23,
    Spell24 = 24,          // Amazon: enemy soldiers get flag 0x80 (meaning unknown)
    Spell25 = 25,          // Amazon: own soldiers get flag 0x10 (meaning for soldiers unknown)
    GodlyGoodsAmazon = 26,
    ConvertGoodsAmazon = 27,
    GoodsBan = 28,         // Amazon: "EventGoodsBan" (handler 0x54aa40)
    GoodsSummon = 29,      // Amazon: "EventGoodsSummon" (handler 0x54acd0)
    TreeCurse = 30,        // Amazon: "EventTreeCurse" (handler 0x5492a0)
    RevealMap = 31,        // Amazon: "EventRevealMap", the whole map for the local player
};
constexpr std::uint32_t kSpellCount = 32;

}  // namespace s3re::game
