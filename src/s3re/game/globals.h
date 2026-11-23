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

#include <cstddef>
#include <cstdint>

namespace s3re::game {

struct GameSettings;  // game_settings_layout.h
struct CGameData;
struct SettlerGroups;

namespace ai {
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
// The pathfinder's progress marker (pathing.h, PathPhase).
inline constexpr GameGlobal<volatile std::uint32_t> g_pathPhase{0x7a9d98};

// ------------------------------------------------------------ settings
// The settings of the current game.
inline constexpr GameGlobal<GameSettings> g_gameSettings{0x7a7a18};
// The configuration from the registry.
inline constexpr GameGlobal<GameConfig> g_config{0x7a7af0};

// ------------------------------------------------------------ graphics and minimap
// The loaded graphics (gfx::CGraphicsData, game/gfx_layout.h).
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
// Per player the index of its colour in g_playerColours.
inline constexpr GameGlobal<std::uint16_t[]> g_playerColourIndex{0x7ace64};
// Player colours as three channels.
inline constexpr GameGlobal<ColourChannels[]> g_playerColours{0x7ace90};

// ------------------------------------------------------------ dialogs and UI
// Dialog-system values (Dialog_* getters in screen_element.cpp; meaning not
// known: probably fonts, colours or GDI objects).
inline constexpr GameGlobal<std::uint32_t[8]> g_dialogSettings{0x7a8020};
inline constexpr GameGlobal<std::uint32_t[14]> g_dialogTable7a8064{0x7a8064};
inline constexpr GameGlobal<std::uint32_t[]> g_dialogTable7a809c{0x7a809c};
inline constexpr GameGlobal<std::uint32_t> g_dialogValue7a8238{0x7a8238};
inline constexpr GameGlobal<std::uint32_t> g_dialogValue7a823c{0x7a823c};
inline constexpr GameGlobal<std::uint32_t> g_dialogGdiObject7a8240{0x7a8240};
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
// Cleared by AI_EvaLand_Destroy (meaning not known).
inline constexpr GameGlobal<std::uint32_t> g_aiUnk7e3134{0x7e3134};
// 20 vectors emptied by AI_ResetAgentData.
inline constexpr GameGlobal<ai::AiVector[20]> g_aiAgentVectors{0x7e7a24};
// Per population stage, the extra buildings wanted per settler (floats
// set by static initializers, 0x566xxx: (wanted[s + 1] - wanted[s]) /
// (stages[s + 1] - stages[s])). Stored in reverse: stage s at [3 - s].
inline constexpr GameGlobal<float[4]> g_aiWantedWeaponsmithSlopes{0x7e1290};
inline constexpr GameGlobal<float[4]> g_aiWantedGoldMineSlopes{0x7e12b0};

// ------------------------------------------------------------ imports
// S3.EXE's import address table slots (called through by the original;
// in-process tests may point one at a fake).
using SleepImport = void(__stdcall*)(std::uint32_t milliseconds);
using CloseHandleImport = int(__stdcall*)(void* handle);
using TimeGetTimeImport = std::uint32_t(__stdcall*)();
inline constexpr GameGlobal<SleepImport> g_importSleep{0x6550dc};
inline constexpr GameGlobal<CloseHandleImport> g_importCloseHandle{0x655180};
inline constexpr GameGlobal<TimeGetTimeImport> g_importTimeGetTime{0x655358};  // from _INMM.dll

}  // namespace s3re::game
