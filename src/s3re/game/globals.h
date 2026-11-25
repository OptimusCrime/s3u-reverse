#pragma once

// Global variables of S3.EXE (its .data section, plus a few import slots).
// One typed declaration per global, the only place its address appears
// (see game/global_access.h for the mechanism; tables are in game/tables.h,
// strings in game/texts.h). re/globals.csv records what is known about each
// one, with the evidence. Names and meanings are ours; "unk" or the address
// in a name means the purpose is not known yet.
//
// Record types used here are forward-declared; include their header
// (layout.h, ai_layout.h, gfx_layout.h, game_settings_layout.h,
// settler_groups.h) to use the
// fields.

#include "game/global_access.h"
#include "msvc6/string.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game {

struct GameSettings;  // game_settings_layout.h
struct CGameData;
struct SettlerGroups;

namespace ai {
struct AiAgentData;
struct AiArmyState;
struct AiCommandSender;
struct AiGroupOrders;
struct AiLandEvaluation;
struct AiPlayerData;
struct AiPlayerSetup;
struct AiVector;
struct ComputerPlayer;
}  // namespace ai

namespace gfx {
struct CGraphicsData;
}  // namespace gfx

#pragma pack(push, 1)

// Per-player tallies (g_playerTallies), probably end-of-game statistics.
struct PlayerTally {
    std::int32_t meals;      // +0x00 +1 per food a miner eats (MinerJobFood)
    std::int32_t ore_found;  // +0x04 +1 per successful dig (MinerJobDig)
    std::int32_t points;     // +0x08 +4 for resource 0x40 by race 1 (MinerJobDig); +2 in UpdateSettlers
    std::int32_t unk0c;      // +0x0c += 8 by BuildingDone for a finished temple-like building; UpdateEconomy
    std::int32_t unk10;
    std::int32_t unk14;      // +0x14 UpdateSoldier: +1 per settler a soldier kills, +5 for a seven-tile target
    std::uint8_t unk18[0x2c];
};
static_assert(sizeof(PlayerTally) == 0x44);
static_assert(offsetof(PlayerTally, unk0c) == 0x0c);
static_assert(offsetof(PlayerTally, unk14) == 0x14);

// One entry of the player info table (g_playerInfo): 20 entries of 0x44
// bytes. Savegames store it in chunk 1 (SavedPlayerInfo, the name as 0x100
// characters). g_playerTallies is the same memory from +0x2c on.
struct PlayerInfo {
    std::int32_t unk00;          // +0x00
    std::int32_t unk04;          // +0x04
    msvc6::String name;          // +0x08 the player's name
    std::int32_t fields[11];     // +0x18 (+0x18 probably the player's +0xc, +0x1c the player index; from
                                 //  +0x2c on the PlayerTally counters)
};
static_assert(offsetof(PlayerInfo, name) == 0x08);
static_assert(offsetof(PlayerInfo, fields) == 0x18);
static_assert(sizeof(PlayerInfo) == 0x44);

// The configuration read from the registry at startup (g_config;
// Dirk\DGlobals.cpp). Size not known.
struct GameConfig {
    std::uint8_t unk000[0x64];
    std::uint32_t notification_level;  // +0x64 notifications shown if their level >= 9 - this (GetMemberDword64)
    std::uint8_t unk068[0x158 - 0x68];
    std::uint8_t random_race_amazons;  // +0x158 copied to GameSettings +0xcc by 0x431ab0
    std::uint8_t unk159[0x189 - 0x159];
    std::uint8_t unk189;               // +0x189 weakens computer players in campaigns (GetMemberByte189)
};
static_assert(offsetof(GameConfig, notification_level) == 0x64);
static_assert(offsetof(GameConfig, random_race_amazons) == 0x158);
static_assert(offsetof(GameConfig, unk189) == 0x189);

// A colour as three uint32 channels (g_playerColours).
struct ColourChannels {
    std::uint32_t c0, c1, c2;
};
static_assert(sizeof(ColourChannels) == 12);

// The screen's pixel format: per colour channel the shift and the scale
// factor (Render::PackColour 0x4c7b00: (c * scale) >> 8 << shift).
struct PixelFormat {
    std::uint32_t shift[3];  // +0x00
    std::uint32_t scale[3];  // +0x0c
};
static_assert(sizeof(PixelFormat) == 0x18);

// One hit area of a drawn object (g_hitAreas): what was drawn where, so a
// click can be matched to it (CDrawer::AddHitArea 0x4d8240).
struct HitArea {
    std::uint16_t kind;    // +0x0 (e.g. 100)
    std::int16_t x;        // +0x2 screen position
    std::int16_t y;        // +0x4
    std::uint16_t value1;  // +0x6 PickHitArea stores these two into world+0x2d2e4/+0x2d2e8
    std::uint16_t value2;  // +0x8
};
static_assert(sizeof(HitArea) == 10);

#pragma pack(pop)

// ------------------------------------------------------------ simulation
// Simulation tick counter, copied from CLogicThread+0x70 at the end of each
// CLogicThread::Step; read by other threads too.
inline constexpr GameGlobal<volatile std::uint32_t> g_gameTick{0x7dfd48};
// Non-zero: UpdateEnvironment returns at once, Step skips the game-end check.
inline constexpr GameGlobal<std::uint8_t> g_worldUpdateDisabled{0x7deb55};
// Steps still to catch up (CLogicThread::Step); the single-player F12 key
// adds 882 (ProcessEvents 0x525cd2, the game's own fast-forward).
inline constexpr GameGlobal<volatile std::uint32_t> g_stepCountdown{0x7dfd2c};
// Set while CLogicThread::Step catches up (fog of war and minimap skipped).
inline constexpr GameGlobal<std::int32_t> g_fastForwarding{0x7dfd30};
// Non-zero while the whole map is shown (reveal spell, CLogicThread).
inline constexpr GameGlobal<std::int32_t> g_revealMap{0x7dfd84};
// Number of the running campaign mission, 0 outside campaigns.
inline constexpr GameGlobal<std::int32_t> g_campaignMission{0x7dfdb0};
// Non-zero: CLogicThread::Step skips the world updates; ExecuteCommands
// (0xf4 SyncCheck) counts it up after a checksum mismatch, so a desync
// stops the simulation.
inline constexpr GameGlobal<std::uint16_t> g_simulationPaused{0x7ace62};
// Lockstep checks (ExecuteCommands 0xf4): per player seven checksum words
// (world, economy, trade, -, pile, settler, buildings) and, at [7], the
// step stamp they belong to.
inline constexpr GameGlobal<std::uint32_t[20][8]> g_syncChecksums{0x7deb58};
// RNG a-words of past ticks (CLogicThread::Step stores one every 256
// ticks); ExecuteCommands 0xfb compares another player's word with it.
inline constexpr GameGlobal<std::uint16_t[]> g_syncRandomHistory{0x7dfb58};
// Set to 0 by ExecuteCommands 0xf8 after a committed multiplayer save
// (meaning unknown).
inline constexpr GameGlobal<std::int32_t> g_global7deb50{0x7deb50};
// Game tick of the last committed multiplayer save (ExecuteCommands 0xf8).
inline constexpr GameGlobal<std::uint32_t> g_lastSaveTick{0x7dfd50};
// The statically linked CRT's small-block heap state (__sbh_* globals of
// malloc/free, code 0x6284d2..0x628b6f); tests skip it when they compare
// S3.EXE's globals around code that allocates.
inline constexpr GameGlobal<std::uint8_t[0x1c]> g_crtSmallBlockHeap{0x7e9728};
// The application object (DirectX::CGameApp; returned by 0x53ba50).
inline constexpr GameGlobal<std::uint8_t*> g_app{0x7e9168};
// Outcome of the current game: -1 while undecided, then set by the game-end
// check 0x511c50 (1 or 0 by the mission's goals).
inline constexpr GameGlobal<std::int32_t> g_gameOutcome{0x730f64};
// Divisor of the soldier strength 0x600 / (value + 4) (map/savegame loaders).
inline constexpr GameGlobal<std::uint32_t> g_strengthDivisor{0x7acfa4};
// First landscape object that ages (0xd8 in the image), see UpdateEnvironment.
inline constexpr GameGlobal<std::uint32_t> g_objectAgeStart{0x730efc};
// Divisor of soldier damage (256 in the image), UpdateSoldier.
inline constexpr GameGlobal<std::uint32_t> g_damageDivisor{0x730eec};
// The building whose menu the UI shows (ExecuteCommands refreshes the
// building menu, 4, when a command changes it; DestroyBuilding clears it);
// also read by UpdateSettlers (job 0x50) and 0x4a99e8, 0x520415. Was
// g_global7dfd6c.
inline constexpr GameGlobal<std::int32_t> g_selectedBuilding{0x7dfd6c};
// A byte in .data read by UpdateSettlers (carrier job 0x4f) and 0x42f7a1;
// meaning still to be named.
inline constexpr GameGlobal<const std::uint8_t> g_byte730ef8{0x730ef8};
// Per-player tallies, probably end-of-game statistics.
inline constexpr GameGlobal<PlayerTally[20]> g_playerTallies{0x7acfd4};
// The player info table (names and per-player values; the savegame's
// chunk 1). g_playerTallies lies inside it (+0x2c).
inline constexpr GameGlobal<PlayerInfo[20]> g_playerInfo{0x7acfa8};
// The game tick at the last update of the draw data (0x5230e0 stores
// g_gameTick before calling 0x51f0e0); saved in chunk 1.
inline constexpr GameGlobal<std::uint32_t> g_lastDrawnTick{0x7dfd4c};
// Set to 1 when the loaded victory conditions protect players for a time
// (LoadMap 0x4fe503, LoadSaveGame 0x509738; also 0x4ffc6c), cleared by
// CLogicThread::OnStart 0x53caa3; read by 0x4b3f95 and 0x5260f1.
inline constexpr GameGlobal<std::uint8_t> g_victoryProtectActive{0x7deb54};
// Cursor 0..127 into the 128 pile claims at world+0x1195dd4 (pile_claims).
inline constexpr GameGlobal<std::uint16_t> g_pileClaimsCursor{0x7ace60};
// The ten settler groups (probably the player's selection groups).
inline constexpr GameGlobal<SettlerGroups> g_settlerGroups{0x7ad620};
// Per settler group: set to 1 when the group's list changed (ProcessEvents
// 0x5257f3 and ExecuteCommands 0x10 set it, the group UI 0x52ecd0 reads and
// clears it).
inline constexpr GameGlobal<std::int32_t[10]> g_settlerGroupChanged{0x7ad5f8};
// The settler group the UI shows (index into g_settlerGroups); ExecuteCommands
// 0x10 sets it to 0 when the local player's carriers become units.
inline constexpr GameGlobal<std::int32_t> g_currentSettlerGroup{0x7dfd68};
// The hit areas of the drawn objects (CDrawer::AddHitArea 0x4d8240,
// CGameData::PickHitArea 0x4ff0a0): two interleaved halves of 10,000
// entries (entry 2 * i + half); the draw thread fills half 1 -
// g_hitAreaReadHalf while the UI reads half g_hitAreaReadHalf (0x4b3f4b
// swaps them after a frame).
inline constexpr GameGlobal<HitArea[20000]> g_hitAreas{0x7ade08};
inline constexpr GameGlobal<std::int32_t> g_hitAreaReadCount{0x7dfd5c};   // entries in the half the UI reads
inline constexpr GameGlobal<std::int32_t> g_hitAreaDrawCount{0x7dfd60};   // entries drawn so far (at most 10000)
inline constexpr GameGlobal<std::int32_t> g_hitAreaReadHalf{0x7dfd64};    // 0 or 1
// Cleared by CGameData::FinishInitialising (0x4fce90), meaning unknown
// (0x7dfd34..0x7dfd3c are also set by 0x4b26c0 and ProcessEvents).
inline constexpr GameGlobal<std::int32_t> g_unk7dfd34{0x7dfd34};
inline constexpr GameGlobal<std::int32_t> g_unk7dfd38{0x7dfd38};
inline constexpr GameGlobal<std::int32_t> g_unk7dfd3c{0x7dfd3c};
inline constexpr GameGlobal<std::int32_t> g_unk7dfd40{0x7dfd40};
// The pathfinder's progress marker (pathing.h, PathPhase).
inline constexpr GameGlobal<volatile std::uint32_t> g_pathPhase{0x7a9d98};

// ------------------------------------------------------------ settings
// The settings of the current game.
inline constexpr GameGlobal<GameSettings> g_gameSettings{0x7a7a18};
// The configuration from the registry.
inline constexpr GameGlobal<GameConfig> g_config{0x7a7af0};

// ------------------------------------------------------------ graphics and minimap
// The loaded graphics (gfx::CGraphicsData, game/graphics/gfx_layout.h).
inline constexpr GameGlobal<gfx::CGraphicsData*> g_gfxData{0x7a8010};
// The screen's pixel format (shifts at 0x7dfba0, scales at 0x7dfbac).
inline constexpr GameGlobal<PixelFormat> g_pixelFormat{0x7dfba0};
// Width and height of the game view in pixels.
inline constexpr GameGlobal<std::uint32_t> g_screenWidth{0x730f18};
inline constexpr GameGlobal<std::uint32_t> g_screenHeight{0x730f1c};
// Origin added to drawing positions (Render helpers, graphics_helpers.cpp).
inline constexpr GameGlobal<std::uint32_t> g_drawOriginX{0x730f3c};
inline constexpr GameGlobal<std::uint32_t> g_drawOriginY{0x730f40};
// Minimap: width (and height) in pixels, colour mode, what it shows, and
// which of its 64 slices were recomputed.
inline constexpr GameGlobal<std::int32_t> g_minimapWidth{0x730f28};
inline constexpr GameGlobal<std::int32_t> g_minimapMode{0x730f58};
inline constexpr GameGlobal<std::int32_t> g_minimapShowBuildings{0x730f5c};
inline constexpr GameGlobal<std::int32_t> g_minimapShowSettlers{0x730f60};
inline constexpr GameGlobal<std::int32_t[64]> g_minimapSliceDirty{0x7ad4f8};
// The screen layout of the display mode, set by Render::SetScreenLayout
// (0x4c6d70) for mode 0/1/2 (640x480, 800x600, 1024x768); the values per
// mode are in screen_layout.cpp. Names from that function and a few uses.
inline constexpr GameGlobal<std::int32_t> g_displayMode{0x730f08};       // 1, 2, 3 (451 uses)
inline constexpr GameGlobal<std::int32_t> g_displayWidth{0x730f0c};      // 640, 800, 1024
inline constexpr GameGlobal<std::int32_t> g_displayHeight{0x730f10};     // 480, 600, 768
inline constexpr GameGlobal<std::int32_t> g_displayBitsPerPixel{0x730f14};  // 16 (SetConfiguredDisplayMode)
inline constexpr GameGlobal<std::int32_t> g_layout730f20{0x730f20};      // 437, 540, 697
inline constexpr GameGlobal<std::int32_t> g_sidebarWidth{0x730f24};      // 136, 176, 216 (display - view width)
inline constexpr GameGlobal<std::int32_t> g_layout730f2c{0x730f2c};      // 5, 8, 6
inline constexpr GameGlobal<std::int32_t> g_layout730f30{0x730f30};      // 10, 5, 8
inline constexpr GameGlobal<std::int32_t> g_layout730f34{0x730f34};      // 142, 170, 224
inline constexpr GameGlobal<std::int32_t> g_uiCellSize{0x730f38};        // 3, 4, 5 (CDrawer::DrawStatisticsBar)
inline constexpr GameGlobal<std::int32_t> g_layout7dfd58{0x7dfd58};      // 0 in every mode
// Per player the index of its colour in g_playerColours.
inline constexpr GameGlobal<std::uint16_t[]> g_playerColourIndex{0x7ace64};
// Player colours as three channels.
inline constexpr GameGlobal<ColourChannels[]> g_playerColours{0x7ace90};

// ------------------------------------------------------------ dialogs and UI
// The dialog system's drawing resources, filled by Dialog::Init (0x416190),
// freed by Dialog::Shutdown (0x416760); read through the Dialog_* getters in
// screen_element.cpp.
inline constexpr GameGlobal<std::uint32_t[8]> g_dialogPixelFormat{0x7a8020};  // the GDI surface's DDPIXELFORMAT
inline constexpr GameGlobal<std::uint32_t[14]> g_dialogColors{0x7a8064};      // colour table
inline constexpr GameGlobal<std::uint32_t[]> g_dialogFonts{0x7a809c};         // font table (HFONTs)
inline constexpr GameGlobal<std::uint32_t> g_dialogGraphicsData{0x7a8238};    // CGraphicsData* (texts, GUI images)
inline constexpr GameGlobal<std::uint32_t> g_dialogSurface{0x7a823c};         // the DirectDraw surface used for GDI
inline constexpr GameGlobal<std::uint32_t> g_dialogPen{0x7a8240};             // HPEN (cursor and frames)
// An object in .data whose address the credits screen stores as a text
// style's default (probably a font or palette; contents not known).
inline constexpr GameGlobal<std::uint8_t[]> g_creditsDefaultStyle{0x69fff0};

// ------------------------------------------------------------ computer players
// The AI code reaches the game through globals set before each player's
// turn (AI_SetCurrentPlayer 0x551a30 and 0x551b80).
inline constexpr GameGlobal<CGameData*> g_aiWorld{0x7e3408};
inline constexpr GameGlobal<ai::ComputerPlayer*> g_aiPlayer{0x7e3130};
inline constexpr GameGlobal<std::uint8_t> g_aiPlayerNumber{0x7e2eb8};
inline constexpr GameGlobal<ai::AiPlayerSetup*> g_aiPlayerSetup{0x7e2eb4};
inline constexpr GameGlobal<ai::AiPlayerData*> g_aiPlayerData{0x7e2eb0};
// The settings table g_aiPlayerSetup points into (0x180-byte entries).
inline constexpr GameGlobal<ai::AiPlayerSetup[]> g_aiPlayerSetups{0x731488};
// The map size as the AI sees it (copied from the world by 0x551b80).
inline constexpr GameGlobal<std::uint16_t> g_aiMapSizeY{0x7e3412};
inline constexpr GameGlobal<std::uint16_t> g_aiMapSizeX{0x7e3414};
// A second copy of the AI's world pointer (0x58fa7b).
inline constexpr GameGlobal<CGameData*> g_aiMapWorld{0x7e7b60};
// The shared land evaluation object: reference count and instance.
inline constexpr GameGlobal<std::uint32_t> g_aiEvaLandRefCount{0x7e0568};
inline constexpr GameGlobal<ai::AiLandEvaluation*> g_aiEvaLand{0x7e056c};
// The CEvaLand in use (the same object as g_aiEvaLand): set to `this` at the
// end of CEvaLand::Init (0x553ea0), cleared by AI_CEvaLand_Destroy and by
// CComputerPlayer::InitAiGlobals (0x551b80); the AI code reads the land grid
// and the per-player area lists through it.
inline constexpr GameGlobal<std::uint32_t> g_aiLandEvaluation{0x7e3134};
// 1 until the first computer player's set-up (0x550a30) has run
// AI::MarkBuildingTypesInLandGrid (0x5723a0); set by InitAiGlobals.
inline constexpr GameGlobal<std::uint32_t> g_aiMarkBuildingTypesPending{0x7312ac};
// AI::CountPlayersTakingPart at the set-up (InitAiGlobals; no reader found).
inline constexpr GameGlobal<std::uint32_t> g_aiPlayersTakingPart{0x7e02f0};
// Cleared by InitAiGlobals; no other access found.
inline constexpr GameGlobal<std::uint32_t> g_aiUnk7e340c{0x7e340c};
// 20 vectors emptied by AI_ResetAgentData.
inline constexpr GameGlobal<ai::AiVector[20]> g_aiAgentVectors{0x7e7a24};
// Per player a std::vector of {hunter settler, target settler} pairs (the
// 16-byte vector objects start at 0x7e3688; like g_aiAgentVectors this
// array starts at the first vector's begin pointer). AI::Army::Reset
// (0x591e00) empties them.
inline constexpr GameGlobal<ai::AiVector[20]> g_aiHunterVectors{0x7e368c};
// Per population stage, the extra buildings wanted per settler (floats
// set by static initializers, 0x566xxx: (wanted[s + 1] - wanted[s]) /
// (stages[s + 1] - stages[s])). Stored in reverse: stage s at [3 - s].
inline constexpr GameGlobal<float[4]> g_aiWantedWeaponsmithSlopes{0x7e1290};
inline constexpr GameGlobal<float[4]> g_aiWantedGoldMineSlopes{0x7e12b0};
// The same slopes per race and stage for the stonecutter (0x568420) and
// woodcutter (0x567e00) conditions: [3 - race][3 - stage] the slope of the
// wanted cutters, [7 - race][3 - stage] that of the wanted stock (set by
// static initializers; g_aiWantedWeaponsmithSlopes / GoldMineSlopes are
// rows 2 and 0 of the stonecutters' table).
inline constexpr GameGlobal<float[8][4]> g_aiStonecutterSlopes{0x7e1280};
inline constexpr GameGlobal<float[8][4]> g_aiWoodcutterSlopes{0x7e1300};
// Rainer\CompMapWorkerAgent.cpp's global objects (ai_layout.h) and a
// per-player array cleared with the army state (0x593f50; meaning unknown).
inline constexpr GameGlobal<ai::AiGroupOrders> g_aiGroupOrders{0x7e37d0};
inline constexpr GameGlobal<ai::AiArmyState> g_aiArmyState{0x7e58c8};
// The message queue (MessageQueue*) the military AI's commands go to: the
// current computer player's, set by AI::RunMilitary 0x58f990.
inline constexpr GameGlobal<std::uint32_t> g_aiMilitaryQueue{0x7e37c8};
// The thiscall `this` of the military AI's enemy counts near a tile (0x58a2c0,
// 0x58a610, ...); dword +0 is a MapOffset search limit (0x595050 sets 5000
// or 500).
inline constexpr GameGlobal<std::uint32_t> g_aiEnemySearch{0x7e5de4};
// The military AI's command sender (the thiscall `this` of 0x590990..
// 0x590c80): battle-spell slots and the command buffer (ai_layout.h).
inline constexpr GameGlobal<ai::AiCommandSender> g_aiCommandSender{0x7e5de8};
// The computer players' census (savegame chunk 0x16; AI_ResetAgentData,
// the AI_Census functions).
inline constexpr GameGlobal<ai::AiAgentData> g_aiCensus{0x7e5ef8};
inline constexpr GameGlobal<std::uint32_t[20]> g_aiPerPlayer7e79d0{0x7e79d0};
// std::bitset<4000> of buildings (Rainer\CompMapWorkerAgent.cpp): a computer
// player's military building whose bit is set has a garrison or one on the
// way (set by AI::RunMilitary 0x58f990, read by
// AI::FirstUnassignedMilitaryBuilding 0x58ae90; cleared by
// AI::ProcessWorldEvents 0x590da0). Index checks call _Xran 0x58b520.
inline constexpr GameGlobal<std::uint32_t[125]> g_aiGarrisonedBuildings{0x7e77dc};

// ------------------------------------------------------------ Watcom skip lists
// WCSkipNonTempBase::base_random_level (0x60bca0) draws node levels two bits
// at a time from a cached CRT rand() value; every skip-list insert (e.g.
// CMapFile's chunk cache) consumes them.
inline constexpr GameGlobal<std::uint32_t> g_skipListRandomBits{0x7e8e24};
inline constexpr GameGlobal<std::int32_t> g_skipListRandomBitsLeft{0x7a4030};

// ------------------------------------------------------------ imports
// S3.EXE's import address table slots (called through by the original;
// in-process tests may point one at a fake).
using SleepImport = void(__stdcall*)(std::uint32_t milliseconds);
using CloseHandleImport = int(__stdcall*)(void* handle);
using TimeGetTimeImport = std::uint32_t(__stdcall*)();
using OutputDebugStringAImport = void(__stdcall*)(const char* text);
inline constexpr GameGlobal<SleepImport> g_importSleep{0x6550dc};
inline constexpr GameGlobal<CloseHandleImport> g_importCloseHandle{0x655180};
inline constexpr GameGlobal<TimeGetTimeImport> g_importTimeGetTime{0x655358};  // from _INMM.dll
inline constexpr GameGlobal<OutputDebugStringAImport> g_importOutputDebugStringA{0x6550b0};

}  // namespace s3re::game
