#pragma once

// Kinds of goods piles.
// Part of game/enums.h (the umbrella; see its header comment for the
// conventions of all enums). Evidence: re/enums.md.

#include <cstdint>

#include "game/enums/value.h"

namespace s3re::game {

// ---------------------------------------------------------------- PileKind
// Pile::art: what a goods pile belongs to. The CreatePile calls of
// BuildingDone, CrushBuilding and DropGoodNear and the DebugLog text of
// StealGoodFromPile ("OrderPile") give the names (re/enums.md).
enum class PileKind : std::uint8_t {
    Storehouse = 0,   // the six piles of a storehouse; kinds up to kLastStoragePileKind act alike
    TradeRing = 252,  // 0xfc: the six piles of a marketplace or harbour
    Order = 253,      // 0xfd: a building's input pile, a site's boards and stones ("OrderPile")
    Dropped = 254,    // 0xfe: goods lying on the ground
    Output = 255,     // 0xff: a building's output pile at its door
};
// CreatePile, StealGoodFromPile and DispatchPileCarrier treat every kind up
// to 50 (0x32) like a storehouse pile; only 0 is known to be created.
constexpr std::uint8_t kLastStoragePileKind = 50;

}  // namespace s3re::game
