#pragma once

// The game's categories as named values: settler jobs and types, settler
// classes, building types, goods, races, and a few terrain values.
//
// Each enum's underlying type is the width of the record field that holds
// it (layout.h), so a field can later take the enum type without changing
// the layout. Until the fields do, game/typed_fields.h converts (JobOf,
// TypeOf, GoodOf, RaceOf, ...). Values are decimal; the comment gives the
// hex form the disassembly shows.
//
// Names come from evidence in S3.EXE: its German DebugLog strings, what the
// code does with a value (which worker a building requests, which goods it
// asks for, ...), and the type tables at 0x658f64 (class), 0x6590e4 (start
// health) and 0x659264 (consumer building types). Public Settlers 3 tools
// list the same building, settler and goods ids; they were used as hints
// and every name used here agrees with the binary's behaviour. Evidence and
// confidence per value: re/enums.md. A value whose meaning is not known yet
// is a placeholder with its decimal number (SettlerJob::Job309), so it can
// be found and renamed later.

#include <cstdint>
#include <type_traits>

namespace s3re::game {

// The value of an enum as its underlying integer (table indexes, calls of
// original functions, comparisons with raw fields).
template <typename E>
constexpr std::underlying_type_t<E> Value(E value) {
    return static_cast<std::underlying_type_t<E>>(value);
}

// ---------------------------------------------------------------- Race
// Player::race (int32), Settler::race and Building::race (uint8; widened).
// Amazons = 3: a random race is Random() % 4 with the Amazons, else % 3
// (0x53cf96). 0..2 also fit the per-race buildings and goods: Romans offer
// wine at building 44, Egyptians beer at 42, Asians rice liquor at 46.
enum class Race : std::int32_t {
    Roman = 0,
    Egyptian = 1,
    Asian = 2,
    Amazon = 3,
};
constexpr std::int32_t kRaces = 4;

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
    Spell10 = 10,          // Roman; 10, 12, 16, 17, 24, 25 share the handler 0x549f40
    Spell11 = 11,          // Roman; handler 0x54a370
    Spell12 = 12,          // Egyptian
    Spell13 = 13,          // Egyptian; handler 0x54a7c0
    Spell14 = 14,          // Asian; handler 0x54a540
    Spell15 = 15,          // Egyptian; handler 0x54a1e0
    Spell16 = 16,          // Asian
    Spell17 = 17,          // Asian
    ConvertGoodsRoman = 18,     // "EventConvertGoods" (handler 0x549bd0)
    ConvertGoodsEgyptian = 19,
    ConvertGoodsAsian = 20,
    GodlyGoodsRoman = 21,       // "EventGodlyGoods" (handler 0x5499b0)
    GodlyGoodsEgyptian = 22,
    GodlyGoodsAsian = 23,
    Spell24 = 24,          // Amazon
    Spell25 = 25,          // Amazon
    GodlyGoodsAmazon = 26,
    ConvertGoodsAmazon = 27,
    GoodsBan = 28,         // Amazon: "EventGoodsBan" (handler 0x54aa40)
    GoodsSummon = 29,      // Amazon: "EventGoodsSummon" (handler 0x54acd0)
    TreeCurse = 30,        // Amazon: "EventTreeCurse" (handler 0x5492a0)
    RevealMap = 31,        // Amazon: "EventRevealMap", the whole map for the local player
};
constexpr std::uint32_t kSpellCount = 32;

// ---------------------------------------------------------------- SettlerJob
// Settler::job: the case of UpdateSettlers' job switch (0x4e9670, table
// 0x4f9270, jobs 0..0x194). The comment gives the hex value, whether
// WalkSettler handles it (walking) and the handler address; jobs without a
// handler go straight to the loop end. Evidence for the names: re/enums.md.
enum class SettlerJob : std::uint16_t {
    New = 0,  // 0x0 handler 0x4e971c
    Nothing = 1,  // 0x1 handler 0x4ea855
    StepAside = 2,  // 0x2 walking, handler 0x4ead27
    WalkToWork = 3,  // 0x3 walking, handler 0x4ead27
    ArriveAtWork = 4,  // 0x4 handler 0x4ead38
    DiggerDig = 5,  // 0x5 handler 0x4eb43b
    DiggerWalkToSpot = 6,  // 0x6 walking, handler 0x4ead27
    DiggerLevel = 7,  // 0x7 handler 0x4eb8ae
    Job8 = 8,  // 0x8 handler 0x4ebab8
    Job9 = 9,  // 0x9 handler 0x4ebab8
    CarrierWalkToPile = 10,  // 0xa walking, handler 0x4ead27
    CarrierAtPile = 11,  // 0xb handler 0x4ec00f
    CarrierWalkToBuilding = 12,  // 0xc walking, handler 0x4ead27
    CarrierAtBuilding = 13,  // 0xd handler 0x4ec27e
    BuilderStart = 14,  // 0xe handler 0x4ec58d
    Job15 = 15,  // 0xf walking, handler 0x4ead27
    Job16 = 16,  // 0x10 handler 0x4ecc6a
    Job17 = 17,  // 0x11 walking, handler 0x4ead27
    BuilderBuild = 18,  // 0x12 handler 0x4ec960
    Job19 = 19,  // 0x13 handler 0x4ece32
    Job20 = 20,  // 0x14 handler 0x4ecea1
    Job21 = 21,  // 0x15 handler 0x4ecf3c
    Job22 = 22,  // 0x16 handler 0x4ecf66
    Job23 = 23,  // 0x17 handler 0x4f8b7c
    Job24 = 24,  // 0x18 handler 0x4ecf82
    Job25 = 25,  // 0x19 handler 0x4ed213
    Job26 = 26,  // 0x1a handler 0x4edffb
    Job27 = 27,  // 0x1b handler 0x4edadd
    Job28 = 28,  // 0x1c walking, handler 0x4ead27
    Job29 = 29,  // 0x1d walking, handler 0x4ead27
    Job30 = 30,  // 0x1e handler 0x4ed4a4
    Job31 = 31,  // 0x1f handler 0x4ed4c3
    Job32 = 32,  // 0x20 handler 0x4ed4e2
    Job33 = 33,  // 0x21 handler 0x4ed58e
    Job34 = 34,  // 0x22 handler 0x4ed53b
    Job35 = 35,  // 0x23 handler 0x4ed7c7
    Job36 = 36,  // 0x24 handler 0x4ed92f
    Job37 = 37,  // 0x25 handler 0x4ed971
    Job38 = 38,  // 0x26 handler 0x4ed951
    Job39 = 39,  // 0x27 handler 0x4eda06
    Job40 = 40,  // 0x28 handler 0x4eda48
    Job41 = 41,  // 0x29 handler 0x4eda28
    Job42 = 42,  // 0x2a walking, handler 0x4ead27
    Job43 = 43,  // 0x2b walking, handler 0x4ead27
    Job44 = 44,  // 0x2c handler 0x4eddbe
    Job45 = 45,  // 0x2d walking, handler 0x4ead27
    Job46 = 46,  // 0x2e handler 0x4edde4
    Job47 = 47,  // 0x2f handler 0x4edf93
    Job48 = 48,  // 0x30 walking, handler 0x4ead27
    Job49 = 49,  // 0x31 handler 0x4ee0c1
    Job50 = 50,  // 0x32 handler 0x4ee114
    Job51 = 51,  // 0x33 handler 0x4ee21f
    Job52 = 52,  // 0x34 handler 0x4ee0c1
    UnitWalk = 53,  // 0x35 walking, handler 0x4eacfc
    Job54 = 54,  // 0x36 handler 0x4eef33
    UnitIdle = 55,  // 0x37 handler 0x4e98be
    Job56 = 56,  // 0x38 handler 0x4ecf4a
    Job57 = 57,  // 0x39 no-op
    Job58 = 58,  // 0x3a handler 0x4ee35a
    Job59 = 59,  // 0x3b handler 0x4ee3de
    Job60 = 60,  // 0x3c handler 0x4ee4cf
    Job61 = 61,  // 0x3d handler 0x4ee4e7
    Job62 = 62,  // 0x3e handler 0x4ee4ff
    Job63 = 63,  // 0x3f handler 0x4ee698
    Job64 = 64,  // 0x40 handler 0x4ee35a
    Job65 = 65,  // 0x41 handler 0x4ee252
    Job66 = 66,  // 0x42 handler 0x4ecf58
    Job67 = 67,  // 0x43 no-op
    Job68 = 68,  // 0x44 handler 0x4ee7d3
    Job69 = 69,  // 0x45 handler 0x4ee857
    Job70 = 70,  // 0x46 handler 0x4ee948
    Job71 = 71,  // 0x47 handler 0x4ee960
    Job72 = 72,  // 0x48 handler 0x4ee978
    Job73 = 73,  // 0x49 handler 0x4eeaca
    Job74 = 74,  // 0x4a handler 0x4ee7d3
    Job75 = 75,  // 0x4b handler 0x4ee6cb
    Job76 = 76,  // 0x4c walking, handler 0x4ead27
    Job77 = 77,  // 0x4d handler 0x4ec1f6
    Job78 = 78,  // 0x4e walking, handler 0x4ead27
    Job79 = 79,  // 0x4f handler 0x4ec3fb
    CarrierDeliver = 80,  // 0x50 handler 0x4ec2a0
    Job81 = 81,  // 0x51 handler 0x4ec67f
    MinerEnterCoalMine = 82,  // 0x52 handler 0x4eeafd
    MinerEnterGoldMine = 83,  // 0x53 handler 0x4eeafd
    MinerEnterIronMine = 84,  // 0x54 handler 0x4eeafd
    Job85 = 85,  // 0x55 handler 0x4eeb0a
    Job86 = 86,  // 0x56 handler 0x4eeb0a
    Job87 = 87,  // 0x57 handler 0x4eeb0a
    Job88 = 88,  // 0x58 handler 0x4eeb31
    Job89 = 89,  // 0x59 handler 0x4eeb3e
    Job90 = 90,  // 0x5a handler 0x4eeb71
    Job91 = 91,  // 0x5b handler 0x4eeb3e
    Job92 = 92,  // 0x5c handler 0x4eeb31
    Job93 = 93,  // 0x5d handler 0x4eebff
    Job94 = 94,  // 0x5e handler 0x4eec32
    Job95 = 95,  // 0x5f handler 0x4eebff
    Job96 = 96,  // 0x60 handler 0x4eeb31
    Job97 = 97,  // 0x61 handler 0x4eecc0
    Job98 = 98,  // 0x62 handler 0x4eecf3
    Job99 = 99,  // 0x63 handler 0x4eecc0
    LeaveHouse = 100,  // 0x64 handler 0x4eef09
    Job101 = 101,  // 0x65 handler 0x4ecf74
    Job102 = 102,  // 0x66 handler 0x4ece93
    Job103 = 103,  // 0x67 handler 0x4ecf09
    CarrierWalkToTool = 104,  // 0x68 walking, handler 0x4ead27
    CarrierTakeTool = 105,  // 0x69 handler 0x4ebe0f
    Job106 = 106,  // 0x6a handler 0x4ecc94
    Job107 = 107,  // 0x6b handler 0x4ef162
    Job108 = 108,  // 0x6c handler 0x4f10be
    Job109 = 109,  // 0x6d handler 0x4f10cc
    Job110 = 110,  // 0x6e handler 0x4f10da
    Job111 = 111,  // 0x6f handler 0x4f17ad
    Job112 = 112,  // 0x70 handler 0x4f1a39
    Job113 = 113,  // 0x71 handler 0x4f22d2
    Job114 = 114,  // 0x72 handler 0x4f2734
    Job115 = 115,  // 0x73 handler 0x4f2aff
    Job116 = 116,  // 0x74 handler 0x4ef170
    Job117 = 117,  // 0x75 handler 0x4f0816
    Job118 = 118,  // 0x76 handler 0x4f2b0d
    Job119 = 119,  // 0x77 handler 0x4f10e8
    Job120 = 120,  // 0x78 handler 0x4f17bb
    Job121 = 121,  // 0x79 handler 0x4f1a47
    Job122 = 122,  // 0x7a handler 0x4f22e0
    Job123 = 123,  // 0x7b handler 0x4f2742
    Job124 = 124,  // 0x7c handler 0x4f2fa0
    Job125 = 125,  // 0x7d handler 0x4ef41b
    Job126 = 126,  // 0x7e handler 0x4ef46e
    Job127 = 127,  // 0x7f handler 0x4ef486
    Job128 = 128,  // 0x80 handler 0x4ef53d
    Job129 = 129,  // 0x81 handler 0x4f0036
    Job130 = 130,  // 0x82 handler 0x4ef41b
    Job131 = 131,  // 0x83 handler 0x4f09f7
    Job132 = 132,  // 0x84 handler 0x4f0a4d
    Job133 = 133,  // 0x85 handler 0x4f0a68
    Job134 = 134,  // 0x86 handler 0x4f0b1f
    Job135 = 135,  // 0x87 handler 0x4f108b
    Job136 = 136,  // 0x88 handler 0x4f09f7
    Job137 = 137,  // 0x89 no-op
    RecruitAtBarracks = 138,  // 0x8a handler 0x4ebac6
    Job139 = 139,  // 0x8b handler 0x4ecd51
    Job140 = 140,  // 0x8c handler 0x4ece16
    Job141 = 141,  // 0x8d handler 0x4f8b7c
    Job142 = 142,  // 0x8e handler 0x4e9884
    Job143 = 143,  // 0x8f handler 0x4f1e6d
    Job144 = 144,  // 0x90 walking, handler 0x4ead27
    Job145 = 145,  // 0x91 handler 0x4f1e93
    Job146 = 146,  // 0x92 handler 0x4f20d5
    Job147 = 147,  // 0x93 walking, handler 0x4ead27
    Job148 = 148,  // 0x94 handler 0x4f2108
    Job149 = 149,  // 0x95 handler 0x4f2116
    Job150 = 150,  // 0x96 handler 0x4f213c
    Job151 = 151,  // 0x97 handler 0x4f218a
    Job152 = 152,  // 0x98 walking, handler 0x4ead27
    Job153 = 153,  // 0x99 handler 0x4f21f5
    Job154 = 154,  // 0x9a handler 0x4f223a
    Job155 = 155,  // 0x9b handler 0x4f221a
    Job156 = 156,  // 0x9c walking, handler 0x4ead27
    Job157 = 157,  // 0x9d handler 0x4f2108
    Job158 = 158,  // 0x9e handler 0x4f23a9
    Job159 = 159,  // 0x9f handler 0x4f24bf
    Job160 = 160,  // 0xa0 handler 0x4f24da
    Job161 = 161,  // 0xa1 handler 0x4f25ec
    Job162 = 162,  // 0xa2 handler 0x4f261f
    Job163 = 163,  // 0xa3 handler 0x4f284d
    Job164 = 164,  // 0xa4 handler 0x4f28a3
    Job165 = 165,  // 0xa5 handler 0x4f295a
    Job166 = 166,  // 0xa6 handler 0x4f2975
    Job167 = 167,  // 0xa7 handler 0x4f2acc
    Job168 = 168,  // 0xa8 handler 0x4f284d
    CarrierPickUp = 169,  // 0xa9 handler 0x4ec031
    CarrierLeavePile = 170,  // 0xaa handler 0x4ec086
    WalkToOwnArea = 171,  // 0xab walking, handler 0x4ead27
    SeekOwnArea = 172,  // 0xac handler 0x4ea55d
    Job173 = 173,  // 0xad handler 0x4ea531
    GuardBuilding = 174,  // 0xae handler 0x4ea314
    Job175 = 175,  // 0xaf handler 0x4ef005
    Job176 = 176,  // 0xb0 walking, handler 0x4eacd2
    Job177 = 177,  // 0xb1 handler 0x4f2d69
    Job178 = 178,  // 0xb2 walking, handler 0x4ead27
    Job179 = 179,  // 0xb3 handler 0x4f2d88
    Job180 = 180,  // 0xb4 handler 0x4f2de1
    Job181 = 181,  // 0xb5 walking, handler 0x4ead27
    Job182 = 182,  // 0xb6 handler 0x4f2eb5
    Job183 = 183,  // 0xb7 handler 0x4f2efa
    Job184 = 184,  // 0xb8 handler 0x4f2eda
    Job185 = 185,  // 0xb9 handler 0x4f2f92
    Job186 = 186,  // 0xba handler 0x4f31c5
    Job187 = 187,  // 0xbb walking, handler 0x4ead27
    Job188 = 188,  // 0xbc walking, handler 0x4ead27
    Job189 = 189,  // 0xbd handler 0x4f31e4
    Job190 = 190,  // 0xbe handler 0x4f3261
    Job191 = 191,  // 0xbf handler 0x4f326f
    Job192 = 192,  // 0xc0 handler 0x4f32b4
    Job193 = 193,  // 0xc1 handler 0x4f3294
    Job194 = 194,  // 0xc2 handler 0x4f334c
    Job195 = 195,  // 0xc3 handler 0x4f1884
    Job196 = 196,  // 0xc4 handler 0x4f18da
    Job197 = 197,  // 0xc5 handler 0x4f18f5
    Job198 = 198,  // 0xc6 handler 0x4f1a06
    Job199 = 199,  // 0xc7 handler 0x4f1884
    Job200 = 200,  // 0xc8 handler 0x4f125f
    Job201 = 201,  // 0xc9 handler 0x4f12b5
    Job202 = 202,  // 0xca handler 0x4f130b
    Job203 = 203,  // 0xcb handler 0x4f1352
    Job204 = 204,  // 0xcc handler 0x4f1394
    Job205 = 205,  // 0xcd handler 0x4f1425
    Job206 = 206,  // 0xce handler 0x4f15bf
    Job207 = 207,  // 0xcf handler 0x4f1615
    Job208 = 208,  // 0xd0 handler 0x4f166b
    Job209 = 209,  // 0xd1 handler 0x4f16df
    Job210 = 210,  // 0xd2 handler 0x4f1721
    Job211 = 211,  // 0xd3 handler 0x4f177a
    GeologistWork = 212,  // 0xd4 handler 0x4f335a
    Job213 = 213,  // 0xd5 handler 0x4f376b
    MinerEnterGemMine = 214,  // 0xd6 handler 0x4eeafd
    MinerEnterSulfurMine = 215,  // 0xd7 handler 0x4eeafd
    Job216 = 216,  // 0xd8 handler 0x4eeb0a
    Job217 = 217,  // 0xd9 handler 0x4eeb0a
    Job218 = 218,  // 0xda handler 0x4eeb31
    Job219 = 219,  // 0xdb handler 0x4eed81
    Job220 = 220,  // 0xdc handler 0x4eedb4
    Job221 = 221,  // 0xdd handler 0x4eed81
    Job222 = 222,  // 0xde handler 0x4eeb31
    Job223 = 223,  // 0xdf handler 0x4eee45
    Job224 = 224,  // 0xe0 handler 0x4eee78
    Job225 = 225,  // 0xe1 handler 0x4eee45
    Job226 = 226,  // 0xe2 handler 0x4f3d45
    Job227 = 227,  // 0xe3 handler 0x4f3d53
    Job228 = 228,  // 0xe4 handler 0x4f4071
    Job229 = 229,  // 0xe5 walking, handler 0x4ead27
    Job230 = 230,  // 0xe6 handler 0x4f4097
    Job231 = 231,  // 0xe7 handler 0x4f4226
    Job232 = 232,  // 0xe8 walking, handler 0x4ead27
    Job233 = 233,  // 0xe9 handler 0x4f4259
    Job234 = 234,  // 0xea handler 0x4f4267
    Job235 = 235,  // 0xeb handler 0x4f428d
    Job236 = 236,  // 0xec handler 0x4f42db
    Job237 = 237,  // 0xed walking, handler 0x4ead27
    Job238 = 238,  // 0xee handler 0x4f4383
    Job239 = 239,  // 0xef handler 0x4f43cb
    Job240 = 240,  // 0xf0 handler 0x4f43a8
    Job241 = 241,  // 0xf1 walking, handler 0x4ead27
    Job242 = 242,  // 0xf2 handler 0x4f4259
    Job243 = 243,  // 0xf3 handler 0x4f4463
    Job244 = 244,  // 0xf4 handler 0x4f4471
    Job245 = 245,  // 0xf5 handler 0x4f49f3
    Job246 = 246,  // 0xf6 walking, handler 0x4ead27
    Job247 = 247,  // 0xf7 handler 0x4f4a19
    Job248 = 248,  // 0xf8 handler 0x4f4d54
    Job249 = 249,  // 0xf9 walking, handler 0x4ead27
    Job250 = 250,  // 0xfa handler 0x4f4d87
    Job251 = 251,  // 0xfb handler 0x4f4d95
    Job252 = 252,  // 0xfc handler 0x4f4dbb
    Job253 = 253,  // 0xfd handler 0x4f4e09
    Job254 = 254,  // 0xfe walking, handler 0x4ead27
    Job255 = 255,  // 0xff handler 0x4f4e5f
    Job256 = 256,  // 0x100 handler 0x4f4e90
    Job257 = 257,  // 0x101 handler 0x4f4eb3
    Job258 = 258,  // 0x102 walking, handler 0x4ead27
    Job259 = 259,  // 0x103 handler 0x4f4d87
    Job260 = 260,  // 0x104 handler 0x4f4f4b
    Job261 = 261,  // 0x105 handler 0x4f4f6e
    Job262 = 262,  // 0x106 no-op
    Job263 = 263,  // 0x107 handler 0x4f5087
    Job264 = 264,  // 0x108 handler 0x4f50dd
    Job265 = 265,  // 0x109 handler 0x4f5133
    Job266 = 266,  // 0x10a handler 0x4f528a
    Job267 = 267,  // 0x10b handler 0x4f5087
    Job268 = 268,  // 0x10c handler 0x4f560c
    Job269 = 269,  // 0x10d no-op
    Job270 = 270,  // 0x10e handler 0x4f5725
    Job271 = 271,  // 0x10f handler 0x4f577b
    Job272 = 272,  // 0x110 handler 0x4f57d1
    Job273 = 273,  // 0x111 handler 0x4f5928
    Job274 = 274,  // 0x112 handler 0x4f5725
    Job275 = 275,  // 0x113 handler 0x4f52bd
    Job276 = 276,  // 0x114 no-op
    Job277 = 277,  // 0x115 handler 0x4f53d6
    Job278 = 278,  // 0x116 handler 0x4f542c
    Job279 = 279,  // 0x117 handler 0x4f5482
    Job280 = 280,  // 0x118 handler 0x4f55d9
    Job281 = 281,  // 0x119 handler 0x4f53d6
    Job282 = 282,  // 0x11a handler 0x4f595b
    Job283 = 283,  // 0x11b no-op
    Job284 = 284,  // 0x11c handler 0x4f5abe
    Job285 = 285,  // 0x11d handler 0x4f5b73
    Job286 = 286,  // 0x11e handler 0x4f5e81
    Job287 = 287,  // 0x11f handler 0x4f5eb4
    Job288 = 288,  // 0x120 handler 0x4f4f7c
    Job289 = 289,  // 0x121 handler 0x4f561a
    Job290 = 290,  // 0x122 handler 0x4f52cb
    Job291 = 291,  // 0x123 handler 0x4f5969
    Job292 = 292,  // 0x124 handler 0x4f5bc9
    Job293 = 293,  // 0x125 handler 0x4f5c1f
    Job294 = 294,  // 0x126 handler 0x4f5c75
    Job295 = 295,  // 0x127 handler 0x4f5ccb
    Job296 = 296,  // 0x128 handler 0x4f0069
    Job297 = 297,  // 0x129 handler 0x4f0077
    Job298 = 298,  // 0x12a handler 0x4f034e
    Job299 = 299,  // 0x12b handler 0x4f035c
    Job300 = 300,  // 0x12c handler 0x4f05b2
    Job301 = 301,  // 0x12d handler 0x4f05c0
    PioneerWork = 302,  // 0x12e handler 0x4f37bd
    Job303 = 303,  // 0x12f handler 0x4f3cf3
    Job304 = 304,  // 0x130 handler 0x4f5f6b
    Job305 = 305,  // 0x131 handler 0x4f1458
    Job306 = 306,  // 0x132 handler 0x4f1466
    Job307 = 307,  // 0x133 walking, handler 0x4ead27
    Job308 = 308,  // 0x134 handler 0x4f5f79
    Job309 = 309,  // 0x135 walking, handler 0x4ead27
    Job310 = 310,  // 0x136 handler 0x4f6255
    CatapultProduction = 311,  // 0x137 handler 0x4f0229
    Job312 = 312,  // 0x138 handler 0x4f04cc
    Job313 = 313,  // 0x139 handler 0x4f0730
    Job314 = 314,  // 0x13a handler 0x4f6e84
    Job315 = 315,  // 0x13b handler 0x4f6e92
    Job316 = 316,  // 0x13c handler 0x4f6ff4
    Job317 = 317,  // 0x13d handler 0x4f70ae
    Job318 = 318,  // 0x13e handler 0x4f7051
    Job319 = 319,  // 0x13f handler 0x4f70ae
    Job320 = 320,  // 0x140 walking, handler 0x4ead27
    Job321 = 321,  // 0x141 walking, handler 0x4ead27
    Job322 = 322,  // 0x142 handler 0x4f73b2
    Job323 = 323,  // 0x143 handler 0x4f70d4
    Job324 = 324,  // 0x144 handler 0x4f735e
    Job325 = 325,  // 0x145 handler 0x4f8b7c
    OnBoard = 326,  // 0x146 handler 0x4f8b7c
    Job327 = 327,  // 0x147 walking, handler 0x4ead27
    Job328 = 328,  // 0x148 walking, handler 0x4ead27
    Job329 = 329,  // 0x149 walking, handler 0x4ead27
    Job330 = 330,  // 0x14a walking, handler 0x4ead27
    Job331 = 331,  // 0x14b handler 0x4f6514
    ShipUnload = 332,  // 0x14c handler 0x4f6a18
    Job333 = 333,  // 0x14d handler 0x4f745b
    Job334 = 334,  // 0x14e handler 0x4f776f
    Job335 = 335,  // 0x14f handler 0x4f780f
    Job336 = 336,  // 0x150 handler 0x4f73ec
    Shoot = 337,  // 0x151 handler 0x4f7842
    ThiefChoosePile = 338,  // 0x152 handler 0x4f78b0
    Job339 = 339,  // 0x153 handler 0x4f8399
    Job340 = 340,  // 0x154 no-op
    Job341 = 341,  // 0x155 handler 0x4f83a7
    Job342 = 342,  // 0x156 handler 0x4f8a95
    Job343 = 343,  // 0x157 handler 0x4f84f8
    Job344 = 344,  // 0x158 handler 0x4f8513
    Job345 = 345,  // 0x159 handler 0x4f859a
    Job346 = 346,  // 0x15a handler 0x4f85b5
    Job347 = 347,  // 0x15b handler 0x4f876b
    Job348 = 348,  // 0x15c handler 0x4f876b
    Job349 = 349,  // 0x15d handler 0x4f876b
    Job350 = 350,  // 0x15e handler 0x4f876b
    Job351 = 351,  // 0x15f handler 0x4f8a95
    Job352 = 352,  // 0x160 handler 0x4f8b1c
    Job353 = 353,  // 0x161 handler 0x4f7c4b
    Job354 = 354,  // 0x162 no-op
    Job355 = 355,  // 0x163 handler 0x4f7c59
    Job356 = 356,  // 0x164 handler 0x4f8065
    Job357 = 357,  // 0x165 walking, handler 0x4ead27
    Job358 = 358,  // 0x166 handler 0x4f8084
    Job359 = 359,  // 0x167 handler 0x4f8181
    Job360 = 360,  // 0x168 walking, handler 0x4ead27
    Job361 = 361,  // 0x169 handler 0x4f81c4
    Job362 = 362,  // 0x16a walking, handler 0x4ead27
    Job363 = 363,  // 0x16b handler 0x4f81e3
    Job364 = 364,  // 0x16c handler 0x4f8246
    Job365 = 365,  // 0x16d walking, handler 0x4ead27
    Job366 = 366,  // 0x16e handler 0x4f82c1
    Job367 = 367,  // 0x16f handler 0x4f82ea
    Job368 = 368,  // 0x170 handler 0x4eb3d2
    Job369 = 369,  // 0x171 handler 0x4f838b
    Job370 = 370,  // 0x172 no-op
    Job371 = 371,  // 0x173 no-op
    Job372 = 372,  // 0x174 no-op
    Job373 = 373,  // 0x175 no-op
    Job374 = 374,  // 0x176 no-op
    Job375 = 375,  // 0x177 no-op
    Job376 = 376,  // 0x178 no-op
    Job377 = 377,  // 0x179 no-op
    Job378 = 378,  // 0x17a no-op
    Job379 = 379,  // 0x17b no-op
    Job380 = 380,  // 0x17c handler 0x4f8b4f
    Job381 = 381,  // 0x17d no-op
    Job382 = 382,  // 0x17e handler 0x4f8b5d
    Job383 = 383,  // 0x17f handler 0x4f8c5c
    Job384 = 384,  // 0x180 handler 0x4f8c77
    Job385 = 385,  // 0x181 handler 0x4f8cfe
    Job386 = 386,  // 0x182 handler 0x4f8d85
    Job387 = 387,  // 0x183 handler 0x4f8da0
    Job388 = 388,  // 0x184 handler 0x4f8efa
    Job389 = 389,  // 0x185 handler 0x4f8f2d
    Job390 = 390,  // 0x186 handler 0x4eeb17
    Job391 = 391,  // 0x187 handler 0x4eeb17
    Job392 = 392,  // 0x188 handler 0x4eeb17
    Job393 = 393,  // 0x189 handler 0x4eeb17
    Job394 = 394,  // 0x18a handler 0x4eeb17
    Job395 = 395,  // 0x18b handler 0x4eeb24
    Job396 = 396,  // 0x18c handler 0x4eeb24
    Job397 = 397,  // 0x18d handler 0x4eeb24
    Job398 = 398,  // 0x18e handler 0x4eeb24
    Job399 = 399,  // 0x18f handler 0x4eeb24
    Job400 = 400,  // 0x190 handler 0x4f8f3b
    Job401 = 401,  // 0x191 handler 0x4f8f70
    GongProduction = 402,  // 0x192 handler 0x4f9122
    ThiefSteal = 403,  // 0x193 handler 0x4f7a84
    Job404 = 404,  // 0x194 handler 0x4f7bdf
};
constexpr std::uint32_t kSettlerJobCount = 405;

// ---------------------------------------------------------------- terrain
// Tile::terrain: the high nibble is the terrain class (re/structs.md "Tile").
constexpr std::uint8_t kTerrainClassMask = 0xf0;
constexpr std::uint8_t kTerrainClassWater = 0x00;     // 5..7 water (the water bodies' flood fill)
constexpr std::uint8_t kTerrainClassMountain = 0x20;  // geologists work here; no wide regions
constexpr std::uint8_t kTerrainWater5 = 5;            // water a ferry can lie in (with 6)
constexpr std::uint8_t kTerrainWater6 = 6;
constexpr std::uint8_t kTerrainShore = 48;            // 0x30: where settlers board a ferry

// ---------------------------------------------------------------- directions
// The six neighbours of a tile, numbered 0..5 (pathing.h).
constexpr std::uint32_t kDirectionCount = 6;

}  // namespace s3re::game
