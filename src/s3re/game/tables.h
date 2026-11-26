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

// DirectX::DSErrorText (0x5c1c10): the DirectSound result codes and their
// names ("DS_OK", "DSERR_ALLOCATED", ...), 16 pairs.
struct DSErrorName {
    std::uint32_t code;  // 0x00 HRESULT
    const char* text;    // 0x04
};
static_assert(sizeof(DSErrorName) == 8);
inline constexpr GameGlobal<const DSErrorName[16]> g_dsErrorNames{0x7a0b80};

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
// Per spell (the numbers of g_raceSpells) the value a player's spell_mana
// slot starts with (CGameData::InitAfterMapLoad 0x4fd020; probably the
// spell's mana cost): 15, 10, 20, 4, 12, ...
inline constexpr GameGlobal<const std::uint32_t[]> g_spellInitialMana{0x658e64};
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

// The axis scales of the statistics chart (CGameData::BuildStatisticsChart
// 0x4ff550): 14 entries of five 16.16 values (times 100: the top of the
// axis and four grid lines, 100/20/40/60/80 .. 10000/2000/../8000). The
// first that fits the chart is chosen; past the last one the function reads
// the 20 bytes after the table (the CGameData vtable's RTTI pointer and
// first slots), as the original does.
inline constexpr GameGlobal<const std::int32_t[14][5]> g_chartAxisScales{0x658afc};
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
// The agent kinds a computer player makes, in order (0..10, ends at 0xffff;
// CComputerPlayer::Init 0x550a30).
inline constexpr GameGlobal<const std::uint16_t[]> g_aiAgentKinds{0x7312b0};
// Per building type: the minimum distance a computer player keeps between
// two buildings of that type (0 = none; woodcutter 4, mines 8, fields 24;
// HOUSE_GROUND_REQUEST_NOTE::CheckSite 0x5760d0).
inline constexpr GameGlobal<const std::uint16_t[]> g_aiSameTypeDistance{0x731a88};
// Per building type (58 words, 0 for type 0 and past the last): the cap
// and the step by which CComputerPlayer::RaiseBuildPriorities (0x551b10)
// raises a non-zero build priority (AiPlayerData::best[type].score).
inline constexpr GameGlobal<const std::uint16_t[58]> g_aiBuildPriorityCaps{0x7328e8};
inline constexpr GameGlobal<const std::uint16_t[58]> g_aiBuildPrioritySteps{0x73295c};
// The T_ECONOMY_AGENT's population stages (EconomyAgent::population):
// stage s while population < stages[s + 1] (0, 40, 100, 500, 2000).
inline constexpr GameGlobal<const std::uint16_t[5]> g_aiPopulationStages{0x65a85c};
// Per population stage 0..3 the weapon shares (Area::settings_extra, sword /
// bow / spear, G) the economy agent moves an area towards
// (T_ECONOMY_AGENT::AdjustSoldierShares 0x56a840): {10,35,30} {25,30,25}
// {35,25,20} {45,20,15}.
inline constexpr GameGlobal<const std::uint8_t[4][3]> g_aiWeaponShares{0x65a87c};
// Carriers wanted at the start of each population stage (0, 20, 50, 200,
// 500; the residences condition 0x568890); the static initializers derive
// g_aiResidenceSlopes from them and the stages.
inline constexpr GameGlobal<const std::uint16_t[5]> g_aiWantedCarriers{0x65a868};
// Buildings wanted at the start of each population stage (0x5696d0 gold
// mines 0, 2, 3, 5, 7; 0x5691b0 weaponsmiths 0, 1, 2, 3, 4); the static
// initializers derive the per-stage slopes (g_aiWantedGoldMineSlopes,
// g_aiWantedWeaponsmithSlopes) from them and the stages.
inline constexpr GameGlobal<const std::uint8_t[5]> g_aiWantedGoldMines{0x65a8c8};
inline constexpr GameGlobal<const std::uint8_t[5]> g_aiWantedWeaponsmiths{0x65a8d2};
// Per race (0..3) and population stage, what the woodcutter and
// stonecutter conditions (0x567e00, 0x568420) want at the start of the
// stage: wood in stock (logs + boards), woodcutters, stones in stock,
// stonecutters. The stonecutters' table is the one at 0x65a8c8
// (g_aiWantedGoldMines is its Roman row, g_aiWantedWeaponsmiths its Asian
// row; those conditions use just that row). Index race * 5 + stage, not
// range-checked.
inline constexpr GameGlobal<const std::uint8_t[4][5]> g_aiWantedWoodStock{0x65a88c};
inline constexpr GameGlobal<const std::uint8_t[4][5]> g_aiWantedWoodcutters{0x65a8a0};
inline constexpr GameGlobal<const std::uint8_t[4][5]> g_aiWantedStoneStock{0x65a8b4};
// The default slope of those conditions for a race above 3 (0.0f).
inline constexpr GameGlobal<const float> g_aiDefaultSlope{0x65a57c};

// The animation ids of the draw data's cell objects (CDrawer::QueueCellObject
// 0x4d5a50; also read by 0x4d1690 / 0x4d1945): 8 per object type, 4 shadow
// animations and then 4 sprite animations, each chosen by Render::Entry204
// of the owner (probably the race). Indexed with 32-bit arithmetic.
inline constexpr GameGlobal<const std::uint32_t[8]> g_cellObjectAnimations{0x6a36b8};

// Landscape objects of the draw cells (CDrawer::QueueLandscapeObject
// 0x4d3050): {sprite, shadow} animation ids of the trees (objects
// 0x44..0x51; the original indexes 0x6a33f0 + object * 8) and of the eight
// tree kinds of objects 0x80..0xe7 (index object & 7; the eighth pair is 0).
inline constexpr GameGlobal<const std::uint32_t[14][2]> g_treeAnimations{0x6a3610};
inline constexpr GameGlobal<const std::uint32_t[8][2]> g_growingTreeAnimations{0x6a3680};
// Sprite animation ids of objects 0xf7..0xfd (the original indexes
// 0x6a3218 + object * 4).
inline constexpr GameGlobal<const std::uint32_t[7]> g_objectF7Animations{0x6a35f4};
// Animation ids per slot and race (4 per slot: the settler GFX groups 13,
// 23, 33, 43 of the four races), chosen by Render::Entry204 of the owner.
// Slots 12..14: a marker's shadow / colour layer / sprite (object 0xff),
// 15..17 the same for 0xfe; other slots: the selection sprites of
// QueueLandscapeObject, settlers (0x4c85a0), buildings (0x4a9960).
inline constexpr GameGlobal<const std::uint32_t[][4]> g_raceAnimations{0x6a3b38};

// Buildings of the draw cells (CDrawer::QueueBuilding 0x4a9960):
// the worker animation's position {dx, dy} per race + type * 4;
inline constexpr GameGlobal<const std::int32_t[][2]> g_buildingWorkOffsets{0x6a159c};
// the extra workers {dx, dy, frame} per race, 36 entries (types 0xf from 0,
// 0x10 from 12, 0x12 from 24);
inline constexpr GameGlobal<const std::int32_t[4][36][3]> g_buildingWorkerSprites{0x6596f0};
// smoke puff positions {dx, dy} per race of types 0xf (3), 0x10 (6), 0x12 (10);
inline constexpr GameGlobal<const std::int32_t[4][3][2]> g_smokePositionsSmall{0x658834};
inline constexpr GameGlobal<const std::int32_t[4][6][2]> g_smokePositionsMedium{0x658894};
inline constexpr GameGlobal<const std::int32_t[4][10][2]> g_smokePositionsLarge{0x658954};
// sequence animation positions {dx, dy} per race of types 0x17 (5) and 0x31 (3);
inline constexpr GameGlobal<const std::int32_t[4][5][2]> g_buildingSequencePositions17{0x6a1cbc};
inline constexpr GameGlobal<const std::int32_t[4][3][2]> g_buildingSequencePositions31{0x6a1d5c};
// the extra sprite's position {dx, dy, ...} (24 bytes) per type + race * 57.
inline constexpr GameGlobal<const std::int32_t[][6]> g_buildingExtraSpritePositions{0x6a1dbc};

// ------------------------------------------------------------ library data
// std::string::npos of S3.EXE's statically linked library (read at run
// time, like the original).
inline constexpr GameGlobal<const volatile std::uint32_t> g_stringNpos{0x655390};
// The shared empty buffer of S3.EXE's std::string (a zero byte).
inline constexpr GameGlobal<char[1]> g_stringNullBuffer{0x6553a0};
// The same for std::wstring (16-bit characters): the shared empty buffer
// (a zero word; its "count byte" 0x655dc7 is read and can be incremented
// by assign) and npos.
inline constexpr GameGlobal<std::uint16_t[1]> g_wstringNullBuffer{0x655dc8};
// The message texts' rects in cells (CDrawer::RenderMessageTexts 0x4d82b0):
// left, top, right, bottom; tops and bottoms count from the display's
// bottom edge. The header {50, -10, 195, -5}; the first line {50, -21, 195,
// -15}, each further line 6 cells higher.
inline constexpr GameGlobal<const std::int32_t[4]> g_messageHeaderCells{0x658ab8};
inline constexpr GameGlobal<const std::int32_t[4]> g_messageLineCells{0x658ac8};
inline constexpr GameGlobal<const volatile std::uint32_t> g_wstringNpos{0x655dcc};
// GUID_NULL (16 zero bytes).
inline constexpr GameGlobal<const std::uint32_t[4]> g_guidNull{0x65c0b8};
// The 16-bit span functions by brightness (SelectBrightnessSpans 0x5fb4c0):
// 32 shade spans (index brightness >> 3; level i scales a pixel by about
// (i + 1) / 32, level 0 writes black) and 8 masked "dissolve" spans (index
// brightness >> 5). Addresses of the functions in S3.EXE.
inline constexpr GameGlobal<const std::uint32_t[32]> g_shadeSpans{0x7a2bd8};
inline constexpr GameGlobal<const std::uint32_t[8]> g_maskedSpans{0x7a2c58};
// The dither masks of the masked spans: 8 rows of 16 bytes (non-zero = draw),
// row maskStart + row for the dissolving top rows of a sprite (gfx_rle.cpp).
inline constexpr GameGlobal<const std::uint8_t[8][16]> g_ditherMasks{0x7a2b58};

// The GFX group names CGraphicsData::Open (0x5f6fd0) builds its file names
// from ("61", "60", "48", ... "01", "00": the 45 groups that exist, from the
// highest down, 4 bytes apart; re/graphics_formats.md).
inline constexpr std::size_t kGfxGroupNameCount = 45;
inline constexpr GameGlobal<const char[kGfxGroupNameCount][4]> g_gfxGroupNames{0x7a2dbc};

// CSoundData::Open (0x5fe760): the primary buffer's WAVEFORMATEX per sound
// format (Open's last argument 1, 2, 4 -> entry 0, 1, 2; 0x18 bytes apart):
// PCM stereo 16-bit at 22050, 22050 and 11025 Hz.
inline constexpr GameGlobal<const std::uint8_t[3][0x18]> g_primaryFormats{0x7a2fb8};

// ---- wip/ddraw-render: COM identifiers DirectX::CDirectDraw uses
// (GUIDs in .rdata, 16 bytes each).
inline constexpr GameGlobal<const std::uint8_t[16]> g_clsidDirectDraw{0x65a908};       // CLSID_DirectDraw
inline constexpr GameGlobal<const std::uint8_t[16]> g_iidDirectDraw2{0x65a938};        // IID_IDirectDraw2
inline constexpr GameGlobal<const std::uint8_t[16]> g_iidDirectDrawSurface3{0x65a978};  // IID_IDirectDrawSurface3
// ---- wip/settings-load (session 58)
// A 16-bit text GamePlayerSetup::SetType 0x49f360 and AttachNetPlayer
// 0x49f190 reset the setup's 16-bit string +0x28 to (zero in the image;
// written elsewhere: 0x419065, 0x48d7a0 and the lobby).
inline constexpr GameGlobal<const std::uint16_t[1]> g_setupWideText{0x7a8248};
// wip/ui-gdi batch: the serial number check digit (GameConfig::SerialCheckDigit
// 0x411fd0, a Verhoeff-like scheme over 12 positions). The tables are read
// as one dword array each; the original indexes the position table with the
// digit's character code ('0' = 48), so it reads past it into the tables
// after it (deliberately reproduced).
inline constexpr GameGlobal<const std::int32_t> g_serialMultiply{0x65545c};    // 8 x 10 (a dihedral group table)
inline constexpr GameGlobal<const std::int32_t> g_serialPermute{0x655554};     // 12 x 10, indexed (pos % 12) * 10 + char
inline constexpr GameGlobal<const std::int32_t> g_serialInverse{0x6555ec};     // 10
inline constexpr GameGlobal<const std::int32_t> g_serialFinal{0x6557f4};       // indexed (pos % 12) * 10 + value

// wip/images-r4 batch: libpng 1.0.2 / zlib 1.1.3 constants (src/s3re/thirdparty/).
inline constexpr GameGlobal<const std::uint32_t[256]> g_zlibCrcTable{0x65c1c4};  // zlib's crc_table (crc32 0x5aa130)
inline constexpr GameGlobal<const char[16]> g_pngHexDigits{0x65c1b4};           // "0123456789ABCDEF" (png_format_buffer)
inline constexpr GameGlobal<const std::uint8_t[8]> g_pngSignature{0x7335e0};     // 89 50 4e 47 0d 0a 1a 0a (png_sig_cmp)
inline constexpr GameGlobal<const std::uint8_t[256]> g_pngOneBppSwap{0x733884};  // png_do_packswap: bit order reversed
inline constexpr GameGlobal<const std::uint8_t[256]> g_pngTwoBppSwap{0x733984};  // 2-bit pixels reversed
inline constexpr GameGlobal<const std::uint8_t[256]> g_pngFourBppSwap{0x733a84}; // nibbles swapped
inline constexpr GameGlobal<const std::uint32_t> g_zlibFixedBl{0x7348e0};         // 9 (inffixed.h fixed_bl)
inline constexpr GameGlobal<const std::uint32_t> g_zlibFixedBd{0x7348e4};         // 5 (fixed_bd)
inline constexpr GameGlobal<const std::uint8_t[0x1000]> g_zlibFixedTl{0x7348e8};  // fixed_tl: 512 inflate_huft
inline constexpr GameGlobal<const std::uint8_t[0x100]> g_zlibFixedTd{0x7358e8};   // fixed_td: 32 inflate_huft
// wip/logic-r4: the CD music (CLogicThread::PlayNextMusicTrack 0x53f5f0).
// Four playlists per CD version (one per race; the Amazons' CD has four
// tracks per list, the others three), and the playlist lengths
// [version][list]. Indexed as the original does (playlist * stride +
// track, no range checks), so declared as single dwords.
inline constexpr GameGlobal<const std::int32_t> g_musicTracksOriginal{0x730ac0};  // [4][3]
inline constexpr GameGlobal<const std::int32_t> g_musicTracksAmazons{0x730af0};   // [4][4] (the mission CD)
inline constexpr GameGlobal<const std::int32_t> g_musicTracksGold1{0x730b30};     // [4][3]
inline constexpr GameGlobal<const std::int32_t> g_musicTracksGold2{0x730b60};     // [4][3]
inline constexpr GameGlobal<const std::int32_t> g_musicListLengths{0x730b90};     // [4][4]: ORG, AMZ, Gold 1, Gold 2

}  // namespace s3re::game
