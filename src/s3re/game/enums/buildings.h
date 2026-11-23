#pragma once

// Building types.
// Part of game/enums.h (the umbrella; see its header comment for the
// conventions of all enums). Evidence: re/enums.md.

#include <cstdint>

#include "game/enums/value.h"

namespace s3re::game {

// ---------------------------------------------------------------- BuildingType
// Building::art. UpdateEconomy's handler per type requests the matching
// worker and input goods, which confirms each name (re/enums.md).
enum class BuildingType : std::uint8_t {
    None = 0,
    Storehouse = 1,
    Woodcutter = 2,
    Stonecutter = 3,
    Sawmill = 4,
    Forester = 5,
    LookoutTower = 6,       // no economy handler
    CoalMine = 7,
    GoldMine = 8,
    IronMine = 9,
    GoldSmelter = 10,       // coal + gold ore
    IronSmelter = 11,       // coal + iron ore
    Toolsmith = 12,         // coal + iron; drops tools on seven tiles at the door
    Weaponsmith = 13,       // coal + iron; drops weapons on three tiles
    Vineyard = 14,
    SmallTower = 15,        // military, 250 hit points
    LargeTower = 16,        // military, 500 hit points
    Mill = 17,
    Castle = 18,            // military, 1000 hit points
    Barracks = 19,          // swords, bows, spears; turns carriers into soldiers
    Bakery = 20,
    Slaughterhouse = 21,
    Distillery = 22,        // rice + coal (Asian rice liquor)
    PigFarm = 23,
    Farm = 24,
    Fishery = 25,
    SmallResidence = 26,    // 1 settler (BuildingDone), +10 population limit
    MediumResidence = 27,   // 3 settlers, +30
    LargeResidence = 28,    // 10 settlers, +100
    SulfurMine = 29,
    Waterworks = 30,
    CatapultWorkshop = 31,
    Shipyard = 32,
    Harbour = 33,           // has a landing direction (Building::dock_direction)
    Marketplace = 34,
    Hospital = 35,
    RiceFarm = 36,
    GemMine = 37,
    Brewery = 38,
    CharcoalBurner = 39,
    PowderMaker = 40,
    Pyramid = 41,           // Egyptian big temple (makes mages)
    Sphinx = 42,            // Egyptian small temple (offering beer)
    LargeTemple = 43,       // Roman big temple
    SmallTemple = 44,       // Roman small temple (offering wine)
    LargePagoda = 45,       // Asian big temple
    SmallPagoda = 46,       // Asian small temple (offering rice liquor, twice the mana)
    BallistaWorkshop = 47,
    CannonWorkshop = 48,
    DonkeyFarm = 49,
    LargeGong = 50,         // "Gong bauen" (UpdateEconomy)
    Apiary = 51,
    Meadery = 52,
    Laboratory = 53,
    AmazonSmallTemple = 54,  // offering mead
    AmazonLargeTemple = 55,
    AmazonSulfurMine = 56,
};
constexpr std::uint32_t kBuildingTypeCount = 57;  // the layout table has 0x39 entries per race

}  // namespace s3re::game
