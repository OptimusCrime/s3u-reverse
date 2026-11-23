#pragma once

// Settler types and their classes.
// Part of game/enums.h (the umbrella; see its header comment for the
// conventions of all enums). Evidence: re/enums.md.

#include <cstdint>

#include "game/enums/value.h"

namespace s3re::game {

// ---------------------------------------------------------------- SettlerType
// Settler::art. The worker a building requests (UpdateEconomy) and the class,
// health and speed tables (0x658f64, 0x6590e4, 0x6591a4) confirm the names.
enum class SettlerType : std::uint8_t {
    Carrier = 0,          // carries goods; recruits and new workers start as carriers
    Digger = 1,           // levels building sites ("digger!")
    Builder = 2,          // builds sites (job BuilderBuild calls BuildingDone)
    Woodcutter = 3,       // building 2
    Stonecutter = 4,      // building 3
    Swordsman1 = 5,       // class 1, health 100
    Sawmiller = 6,        // building 4 (input logs)
    Forester = 7,         // building 5
    Bowman1 = 8,          // class 2, health 100
    Smelter = 9,          // buildings 10 and 11 (two products)
    Miner = 10,           // the mines 7, 8, 9, 29, 37, 56
    Smith = 11,           // tool and weapon smiths 12, 13; workshops 31, 47, 48, 50
    Miller = 12,          // building 17
    Baker = 13,           // building 20
    Butcher = 14,         // building 21
    Pikeman1 = 15,        // class 3, health 100
    Farmer = 16,          // building 24
    Fisher = 17,          // building 25
    Waterworker = 18,     // building 30
    ShipyardWorker = 19,  // building 32
    Type20 = 20,
    Healer = 21,          // building 35 (hospital)
    Geologist = 22,       // class 20; works on mountain tiles (job GeologistWork)
    Thief = 23,           // class 10; jobs ThiefChoosePile / ThiefSteal
    CharcoalBurner = 24,  // building 39
    Distiller = 25,       // building 22 (rice liquor)
    Brewer = 26,          // building 38
    PowderMaker = 27,     // building 40 (gunpowder)
    PigFarmer = 28,       // buildings 23 and 49 (pig and donkey farm)
    Winegrower = 29,      // building 14
    Bowman2 = 30,         // class 2, health 120
    Pikeman2 = 31,        // class 3, health 120
    Swordsman2 = 32,      // class 1, health 120
    Bowman3 = 33,         // class 2, health 150
    Pikeman3 = 34,        // class 3, health 150
    Swordsman3 = 35,      // class 1, health 150
    Mage = 36,            // class 22; created by the big temples (UpdateEconomy)
    Type37 = 37,          // class 99
    RiceFarmer = 38,      // building 36
    Donkey = 39,          // class 30
    Pioneer = 40,         // class 21; claims land (job PioneerWork)
    Catapult = 41,        // class 40, health 250; stands on seven tiles (WalkSettler)
    Type42 = 42,          // class 41, health 0
    TradeShip = 43,       // class 50, health 250; carries cargo (Settler cargo fields)
    Ferry = 44,           // class 51, health 250; carries settlers (passengers)
    Beekeeper = 45,       // building 51
    MeadMaker = 46,       // building 52
    Alchemist = 47,       // building 53
};
constexpr std::uint32_t kSettlerTypeCount = 48;

// ---------------------------------------------------------------- SettlerClass
// The class of a settler type (int32 table g_settlerTypeClass, S3.EXE
// 0x658f64). Soldier classes 1..3 fill military buildings; 10..29 count as
// civilians in RecountPlayerPopulation.
enum class SettlerClass : std::int32_t {
    Civilian = 0,
    Swordsman = 1,
    Bowman = 2,
    Pikeman = 3,
    Thief = 10,
    Geologist = 20,
    Pioneer = 21,
    Mage = 22,
    Donkey = 30,
    Catapult = 40,
    Class41 = 41,  // type 42
    TradeShip = 50,
    Ferry = 51,
    Class99 = 99,  // type 37
};

}  // namespace s3re::game
