#pragma once

// Constant tables and constants in S3.EXE's data (.rdata, and .data values
// the code never writes). One typed declaration per table, the only place
// its address appears (game/global_access.h); re/globals.csv records them.
// The original reads them at their addresses, so our replacements do too
// (the values are not copied into our code).
//
// Where the original indexes a table past what looks like its end (no range
// checks), the declared bound is not a limit: indexing works by the same
// address arithmetic. A bound of [] means the size is not known.

#include "game/global_access.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game {

#pragma pack(push, 1)

// Per notification type (g_notificationKinds).
struct NotificationKind {
    std::int32_t list;     // 0x00 1..4 (0 for unused types)
    std::int32_t level;    // 0x04 shown if level >= 9 - setting
    std::int32_t icon;     // 0x08 graphics id (0x800c1...), probably the icon
    std::int32_t repeat;   // 0x0c repeat-timer start value
    std::uint32_t radius;  // 0x10 0 = no repeat timer
};
static_assert(sizeof(NotificationKind) == 20);

// The wanted amounts of the shipyard's inputs while it builds a ship
// (g_shipyardInputs); the original reads the low byte of each.
struct ShipyardInputs {
    std::int32_t first;   // +0x0 Building byte 0x1a
    std::int32_t second;  // +0x4 Building byte 0x1b
    std::int32_t third;   // +0x8 Building byte 0x1c
    std::int32_t unkc;
};
static_assert(sizeof(ShipyardInputs) == 0x10);

// Pixel offset of a soldier's place in a tower (g_towerSlotOffsets).
struct TowerSlotOffset {
    std::int32_t x;
    std::int32_t y;
    std::int32_t unk8;
};
static_assert(sizeof(TowerSlotOffset) == 12);

#pragma pack(pop)

// ------------------------------------------------------------ map and movement
// Pathing cost per terrain type (signed).
inline constexpr GameGlobal<const std::int8_t[256]> g_terrainCost{0x65857c};
// x and y steps of the six directions.
inline constexpr GameGlobal<const std::int32_t[6]> g_directionDx{0x65867c};
inline constexpr GameGlobal<const std::int32_t[6]> g_directionDy{0x658694};
// Per direction four values used to split a turn into two axes
// (pathing.cpp, AngleSetup), and the two axes.
inline constexpr GameGlobal<const std::int32_t[6][4]> g_pathSplitTable{0x6586ac};
inline constexpr GameGlobal<const std::int32_t[6][4]> g_pathAxisTable{0x65870c};
// Walking time added per terrain type.
inline constexpr GameGlobal<const std::uint8_t[256]> g_terrainSpeed{0x658c1c};

// ------------------------------------------------------------ settler types
// Per settler type (0x30 types): its class (0 = civilian; 9 and 1 are
// builders and diggers, 3 carriers...), the pathing turn cost, the start
// health and the walking speed (the original reads its low byte).
inline constexpr GameGlobal<const std::int32_t[0x30]> g_settlerTypeClass{0x658f64};
inline constexpr GameGlobal<const std::int32_t[0x30]> g_settlerTypeTurnCost{0x659024};
inline constexpr GameGlobal<const std::int32_t[0x30]> g_settlerTypeStartHealth{0x6590e4};
inline constexpr GameGlobal<const std::int32_t[0x30]> g_settlerTypeSpeed{0x6591a4};
// Animation ids per (kind * 4 + race).
inline constexpr GameGlobal<const std::uint32_t[]> g_settlerAnimations{0x6a8060};

// ------------------------------------------------------------ races
// Per race the eight spells it can cast.
inline constexpr GameGlobal<const std::uint32_t[][8]> g_raceSpells{0x658d64};
// Per spell (the numbers of g_raceSpells) the range in tiles around the
// priest (ExecuteCommands 0x15: TileDistance - 2 must be below it).
inline constexpr GameGlobal<const std::uint32_t[]> g_spellCastRange{0x658de4};
// Per race the number of settlers a ship carries.
inline constexpr GameGlobal<const std::int32_t[4]> g_raceShipCapacity{0x659644};
// Per race the length of the shooting animation (the original reads the
// low word).
inline constexpr GameGlobal<const std::int32_t[4]> g_raceShotTime{0x659dc0};
// Per race (28, 32, 36, 17): ExecuteCommands' MoveUnits stores its low word
// in path_cache[5] of a catapult sent to attack (mode 5, after the 0xfffd
// marker in path_cache[4]); meaning to be named.
inline constexpr GameGlobal<const std::int32_t[4]> g_raceAttackValue659db0{0x659db0};

// ------------------------------------------------------------ buildings
// Building layouts: per (race * 57 + type) 2400 bytes of uint32 lists
// (footprint, land, levelling tiles...), each ended by a marker.
inline constexpr GameGlobal<const std::uint32_t[][600]> g_buildingLayouts{0x6aa170};
// Per building type its three input goods.
inline constexpr GameGlobal<const std::int32_t[][3]> g_buildingInputGoods{0x6a6824};
// Per ((race * 57 + type) * 3 + input) the pile offset (dx, dy).
inline constexpr GameGlobal<const std::int32_t[][2]> g_buildingInputOffsets{0x6a6ad0};
// Per race six pile offsets (dx, dy) of the storehouses 0x22 and 0x21.
inline constexpr GameGlobal<const std::int32_t[4][6][2]> g_storePileOffsets22{0x6594a4};
inline constexpr GameGlobal<const std::int32_t[4][6][2]> g_storePileOffsets21{0x659564};
// Offset (dx, dy) from the building to the centre of its seven extra tiles,
// for the types 0x1f, 0x2f, 0x30 and 0x32.
inline constexpr GameGlobal<const std::int32_t[4][2]> g_extraTilesCentre{0x659624};
// Hit points of the military buildings 0xf, 0x10, 0x12.
inline constexpr GameGlobal<const std::int32_t[3]> g_militaryHitPoints{0x659664};
// Shipyard inputs per ship (state & 0xf): [ship][0] for the states
// 0x30..0x33, [ship][1] for 0x40..0x43.
inline constexpr GameGlobal<const ShipyardInputs[4][2]> g_shipyardInputs{0x659670};
// Per tower slot index the pixel offset of the soldier.
inline constexpr GameGlobal<const TowerSlotOffset[]> g_towerSlotOffsets{0x659738};
// The goods type of each line of the trade menu (ExecuteCommands 0x12,
// kinds 8..11: the orders of a harbour or marketplace; 34 entries).
inline constexpr GameGlobal<const std::int32_t[]> g_tradeMenuGoods{0x659414};
// The building type whose goods a consumer type wants (UpdateEconomy).
inline constexpr GameGlobal<const std::uint32_t[]> g_consumerBuildingType{0x659264};
// Single constants in .data: the output good of building type 0xb (11),
// mana per offering (1), the residence capacity unit (10) and the smith's
// full input amount (8).
inline constexpr GameGlobal<const std::int32_t> g_goodType0B{0x730ef0};
inline constexpr GameGlobal<const std::uint32_t> g_manaPerOffering{0x730ef4};
inline constexpr GameGlobal<const std::int8_t> g_residenceUnit{0x730f00};
inline constexpr GameGlobal<const std::uint32_t> g_smithInputFull{0x730f04};

// UpdateSettlers: two int32 tables of four values each (0x46, 0x46, 0x46,
// 0x46 and 0x54, 0x54, 0x60, 0x54 in the image), read per race; meaning
// still to be named (settler_jobs_food.cpp).
inline constexpr GameGlobal<const std::int32_t[8]> g_table658adc{0x658adc};
// UpdateSettlers: per race (3, 4, 2, 3 in the image), compared in the ship
// jobs (settler_jobs_special.cpp); meaning still to be named.
inline constexpr GameGlobal<const std::int32_t[4]> g_table659654{0x659654};
// int32 per index (20 in every entry of the image); ExecuteCommands
// (0x5389f4) and campaign mission 51 of CheckGameEnd (entry 0) pass entry + 1
// as the second value of the second effect record (AllocTable36700Record);
// meaning still to be named.
inline constexpr GameGlobal<const std::int32_t[]> g_table658ee4{0x658ee4};

// ------------------------------------------------------------ effects and spells
// Per variant 0..3 the x and y offsets of MapEffects_Spawn_544dc0.
inline constexpr GameGlobal<const std::uint32_t[4]> g_spawnOffsetsX{0x65a19c};
inline constexpr GameGlobal<const std::uint32_t[4]> g_spawnOffsetsY{0x65a1ac};
// Flight-time factors of projectiles (sqrt(distance) * factor).
inline constexpr GameGlobal<const double> g_flightFactor05{0x65a1c0};
inline constexpr GameGlobal<const double> g_flightFactor2{0x65a1c8};
inline constexpr GameGlobal<const double> g_flightFactor0{0x65a1d0};
// Per spell 0..31 the number of ring offsets (world+0x110a588) its area
// covers (200, 200, 200, 200, 500, 0, 127, ...; TreeCurse 1000), and at 32
// that of the burning tree's terrain transform (7). Read again every loop
// step by the original (the array is not const in its source).
inline constexpr GameGlobal<const std::int32_t[]> g_spellRingCounts{0x65a1d8};
// Per transform kind 0..32 the side of TransformTerrainAround's grid (32
// for spells 0..3 and the burning tree).
inline constexpr GameGlobal<const std::int32_t[]> g_spellGridSides{0x65a25c};
// Per spell (0..31) the most objects (settlers, goods) it affects
// ("iEventMaxObjects" in the DebugLog texts).
inline constexpr GameGlobal<const std::int32_t[32]> g_spellMaxObjects{0x65a2e0};
// Per soldier spell (SpellMarkSoldiers 0x549f40, index 0..5 for spells 10,
// 12, 17, 16, 24, 25) the ticks until its settler flag is taken back
// (375, 375, 375, 375, 188, 375).
inline constexpr GameGlobal<const std::int32_t[6]> g_soldierSpellTicks{0x65a364};
// SpellGodlyGoods (0x5499b0): per race 8 goods (race * 8 + k), their
// cumulative chances out of 65536 and their largest amounts.
inline constexpr GameGlobal<const std::int32_t[32]> g_godlyGoods{0x65a37c};
inline constexpr GameGlobal<const std::int32_t[32]> g_godlyGoodsChance{0x65a3fc};
inline constexpr GameGlobal<const std::int32_t[32]> g_godlyGoodsMaxAmount{0x65a47c};
// SpellConvertGoods (0x549bd0): per race the good taken (iron, fish, stone,
// gold) and the good dropped instead (gold, meat, iron, stone).
inline constexpr GameGlobal<const std::int32_t[4]> g_convertGoodsFrom{0x65a4fc};
inline constexpr GameGlobal<const std::int32_t[4]> g_convertGoodsTo{0x65a50c};
// Per notification type.
inline constexpr GameGlobal<const NotificationKind[]> g_notificationKinds{0x659dd0};

// ------------------------------------------------------------ computer players
// Default per-candidate value (AI_RecordBestCandidate).
inline constexpr GameGlobal<const std::uint16_t[]> g_aiCandidateDefaults{0x7329d0};
// Per building type (58 words, 0 for type 0 and past the last): the cap
// and the step by which CComputerPlayer::RaiseBuildPriorities (0x551b10)
// raises a non-zero build priority (AiPlayerData::best[type].score).
inline constexpr GameGlobal<const std::uint16_t[58]> g_aiBuildPriorityCaps{0x7328e8};
inline constexpr GameGlobal<const std::uint16_t[58]> g_aiBuildPrioritySteps{0x73295c};
// The T_ECONOMY_AGENT's population stages (EconomyAgent::population):
// stage s while population < stages[s + 1] (0, 40, 100, 500, 2000).
inline constexpr GameGlobal<const std::uint16_t[5]> g_aiPopulationStages{0x65a85c};
// Buildings wanted at the start of each population stage (0x5696d0 gold
// mines 0, 2, 3, 5, 7; 0x5691b0 weaponsmiths 0, 1, 2, 3, 4); the static
// initializers derive the per-stage slopes (g_aiWantedGoldMineSlopes,
// g_aiWantedWeaponsmithSlopes) from them and the stages.
inline constexpr GameGlobal<const std::uint8_t[5]> g_aiWantedGoldMines{0x65a8c8};
inline constexpr GameGlobal<const std::uint8_t[5]> g_aiWantedWeaponsmiths{0x65a8d2};

// ------------------------------------------------------------ library data
// std::string::npos of S3.EXE's statically linked library (read at run
// time, like the original).
inline constexpr GameGlobal<const volatile std::uint32_t> g_stringNpos{0x655390};
// The shared empty buffer of S3.EXE's std::string (a zero byte).
inline constexpr GameGlobal<char[1]> g_stringNullBuffer{0x6553a0};
// GUID_NULL (16 zero bytes).
inline constexpr GameGlobal<const std::uint32_t[4]> g_guidNull{0x65c0b8};

}  // namespace s3re::game
