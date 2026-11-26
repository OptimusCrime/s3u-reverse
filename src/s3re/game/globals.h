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
struct AiToolAndWorker;
struct AiVector;
struct ComputerPlayer;
struct SiteTreeNode;
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
    std::uint8_t unk000[4];
    msvc6::String string04;            // +0x04 copied out by 0x411a90
    msvc6::String string14;            // +0x14 set by 0x412130, its address from 0x412120
    std::uint8_t byte24;               // +0x24 (0x412270 / 0x412280)
    std::uint8_t unk025[3];
    std::uint32_t dword28;             // +0x28 (0x4120a0 / 0x4120b0; 14 readers)
    std::uint32_t dword2c;             // +0x2c (set by 0x4120c0)
    std::uint8_t byte30;               // +0x30 (0x4120d0, checked by CConsoleThread::OnStart)
    std::uint8_t unk031[3];
    std::uint32_t directplay;          // +0x34 non-zero when DirectPlay 6 is installed (0x4120e0)
    std::uint8_t byte38;               // +0x38 (0x4120f0, checked by CConsoleThread::OnStart)
    std::uint8_t byte39;               // +0x39 (0x412100)
    std::uint8_t unk03a[2];
    std::uint8_t object3c[0x58 - 0x3c];  // +0x3c its address from 0x412110
    std::uint8_t bytes58[8];           // +0x58..+0x5f flags (0x412290 .. 0x412820; +0x5c set by 0x4122f0)
    std::uint8_t unk060[4];
    std::uint32_t notification_level;  // +0x64 notifications shown if their level >= 9 - this (GetMemberDword64)
    std::uint32_t lobby_value68;       // +0x68 read and set by the lobby dialogs (0x413b60 / 0x413b70)
    std::uint8_t byte6c;               // +0x6c (0x412800)
    std::uint8_t unk06d[3];
    std::uint32_t screen_layout;       // +0x70 0/1/2 = 640x480/800x600/1024x768 (CDrawer::ApplyScreenLayout 0x4c6d40)
    std::uint32_t dwords74[9];         // +0x74..+0x94 (getters 0x412300 .. 0x412370; +0x88 has none)
    std::uint8_t unk098[0xc0 - 0x98];  // +0x98 a byte and a string (+0x9c), set by 0x412380 / 0x4125c0
    msvc6::String paths[6];            // +0xc0..+0x110 the data paths GameConfig::PathString 0x412b00 copies out
    std::uint8_t unk120[0x148 - 0x120];
    std::uint32_t lobby_pair148[2];    // +0x148 two values the lobby dialog reads and stores (0x413b80 / 0x413ba0)
    std::uint8_t byte150;              // +0x150 read by the main menu and the load dialog (0x413bc0)
    std::uint8_t unk151[0x158 - 0x151];
    std::uint8_t random_race_amazons;  // +0x158 copied to GameSettings +0xcc by 0x431ab0
    std::uint8_t gfx_group61;          // +0x159 CGraphicsData::Open loads GFX group 61 only when set (getter 0x413be0)
    std::uint8_t byte15a;              // +0x15a (0x414020)
    std::uint8_t unk15b;
    msvc6::String start_map;           // +0x15c probably the map to start from the command line (0x413c20 / 0x413d50)
    std::uint8_t unk16c[0x189 - 0x16c];
    std::uint8_t unk189;               // +0x189 weakens computer players in campaigns (GetMemberByte189; set by 0x414040)
};
static_assert(offsetof(GameConfig, string04) == 0x04);
static_assert(offsetof(GameConfig, byte24) == 0x24);
static_assert(offsetof(GameConfig, directplay) == 0x34);
static_assert(offsetof(GameConfig, object3c) == 0x3c);
static_assert(offsetof(GameConfig, bytes58) == 0x58);
static_assert(offsetof(GameConfig, notification_level) == 0x64);
static_assert(offsetof(GameConfig, lobby_value68) == 0x68);
static_assert(offsetof(GameConfig, byte6c) == 0x6c);
static_assert(offsetof(GameConfig, dwords74) == 0x74);
static_assert(offsetof(GameConfig, paths) == 0xc0);
static_assert(offsetof(GameConfig, lobby_pair148) == 0x148);
static_assert(offsetof(GameConfig, byte150) == 0x150);
static_assert(offsetof(GameConfig, random_race_amazons) == 0x158);
static_assert(offsetof(GameConfig, byte15a) == 0x15a);
static_assert(offsetof(GameConfig, start_map) == 0x15c);
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

// The pixel-format masks of the 16-bit shading code (Dirk\graphicsdata.cpp),
// set at the end of CGraphicsData::Open (0x5fa011..0x5fa0fe): per k = 1..5
// the mask that keeps, after x >> k, only the bits that stayed inside their
// colour channel. RGB565: 0x7bef, 0x39e7, 0x18e3, 0x861, 0x20; RGB555:
// 0x3def, 0x1ce7, 0xc63, 0x421, 0.
struct ShadeMasks {
    std::uint32_t pixel[5];  // +0x00 one 16-bit pixel, for x >> 1 .. x >> 5
    std::uint32_t pair[5];   // +0x14 the same for two pixels in a dword (0x7bef7bef, ...)
    std::uint32_t shadow[2]; // +0x28 pair[0] again; CGraphicsData::SetShadowMethod passes them on
};
static_assert(sizeof(ShadeMasks) == 0x30);

// The shadow drawing state, set by SetShadowSpan (0x5fcd30).
struct ShadowSpanState {
    std::uint32_t method;      // +0x00 0 = halve the destination, 1 = black checkerboard
    std::uint32_t halfMask16;  // +0x04 copies of the masks it was given (not read by the spans)
    std::uint32_t halfMask32;  // +0x08
    std::uint32_t unk0c;       // +0x0c not written by SetShadowSpan
    std::uint32_t mask18;      // +0x10
    std::uint32_t mask1c;      // +0x14
    std::uint32_t span;        // +0x18 the span function the shadow blitters call (0x5fd0c0 or 0x5fd080)
};
static_assert(sizeof(ShadowSpanState) == 0x1c);

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
// CLogicThread::Step does one-time setup when this is 250, then sets it to
// 0 (the display mode change 0x4c7490 sets it to 250).
inline constexpr GameGlobal<std::uint32_t> g_logicStartState{0x7dfd44};
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
// The CRT time()'s cache (0x61f6a7): +0 the last result, +8 the SYSTEMTIME
// of GetLocalTime it came from; depends on the clock, so tests skip it.
inline constexpr GameGlobal<std::uint8_t[0x18]> g_crtTimeCache{0x7e93b0};
// The application object (DirectX::CGameApp; returned by 0x53ba50).
inline constexpr GameGlobal<std::uint8_t*> g_app{0x7e9168};
// 1 when the command line has "-debug" (set by the parser 0x405e50); picks the
// debug column of App::WaitTime 0x40d0e0 (game/framework/wait_times.cpp).
inline constexpr GameGlobal<std::uint32_t> g_debugSwitch{0x7e916c};
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
// The installed CD (3 the mission CD, 4 Gold Edition CD 1, 5 Gold CD 2,
// anything else the original CD); written by 0x493af0 / 0x4976ad, chooses
// the music track table (CLogicThread::PlayNextMusicTrack).
inline constexpr GameGlobal<std::int32_t> g_installedCd{0x7a7c18};
// The game's draw thread (DirectX::CGameDrawThread; RequestFullRedraw
// 0x5b6730); null before it exists.
inline constexpr GameGlobal<void*> g_drawThread{0x7e8bec};
// The CD audio player (Win32::CCDAudio, Play 0x615ea0 / Stop 0x615f70).
inline constexpr GameGlobal<std::uint8_t[0x10]> g_cdAudio{0x7a7a00};
inline constexpr GameGlobal<GameConfig> g_config{0x7a7af0};
// The folder the game's files are found in (a prefix of "Save\" and
// "Save\Multi"; empty or ending in a backslash). CLogicThread::SaveGame
// builds its paths from it; the tests point it at a temporary folder.
inline constexpr GameGlobal<msvc6::String> g_gameFolder{0x7a7c80};
// The game's version x100 (0xa0: 1.60), written as the savegame version.
inline constexpr GameGlobal<std::int32_t> g_versionNumber{0x6970f0};
// Two build values (CConsoleThread::OnStart from 0x60eff0), saved in chunk 0.
inline constexpr GameGlobal<std::uint32_t[2]> g_buildInfo{0x7a8000};

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
// The 16-bit shading masks (ShadeMasks) and the shadow state.
inline constexpr GameGlobal<ShadeMasks> g_shadeMasks{0x7e8dd8};
inline constexpr GameGlobal<ShadowSpanState> g_shadowSpan{0x7e8e08};
// The span functions the 16-bit sprite blitters call, picked by brightness
// (SelectBrightnessSpans 0x5fb4c0 from g_shadeSpans / g_maskedSpans).
inline constexpr GameGlobal<std::uint32_t> g_maskedSpan{0x7e8c70};
inline constexpr GameGlobal<std::uint32_t> g_shadeSpan{0x7e8c74};
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
// The nil node shared by every AI::SiteTree (MSVC6 _Tree::_Nil; black, made
// by the first tree, freed with the last) and the number of trees using it.
inline constexpr GameGlobal<ai::SiteTreeNode*> g_aiSiteTreeNil{0x7e0570};
inline constexpr GameGlobal<std::uint32_t> g_aiSiteTreeNilRefs{0x7e0574};
// The nil node and its reference count of the farm, rice farm and vineyard
// finders' std::map<u8, position, greater<u8>> (a second MSVC6 _Tree
// instance; library code 0x59cb70.., ai_work_area_finders.cpp).
inline constexpr GameGlobal<ai::SiteTreeNode*> g_aiFieldMapNil{0x7e80a8};
inline constexpr GameGlobal<std::uint32_t> g_aiFieldMapNilRefs{0x7e80ac};
// 1 until the first computer player's set-up (0x550a30) has run
// AI::MarkBuildingTypesInLandGrid (0x5723a0); set by InitAiGlobals.
inline constexpr GameGlobal<std::uint32_t> g_aiMarkBuildingTypesPending{0x7312ac};
// AI::CountPlayersTakingPart at the set-up (InitAiGlobals; no reader found).
inline constexpr GameGlobal<std::uint32_t> g_aiPlayersTakingPart{0x7e02f0};
// Cleared by InitAiGlobals; no other access found.
inline constexpr GameGlobal<std::uint32_t> g_aiUnk7e340c{0x7e340c};
// Per player a std::vector<u32> of the settler ids of its catapults (filled
// by AI::Census::CountSettlers 0x55b880, read by AI::Army::AttackEnemyCatapults
// 0x593180; emptied by AI_ResetAgentData). Was g_aiAgentVectors.
inline constexpr GameGlobal<ai::AiVector[20]> g_aiCatapultVectors{0x7e7a24};
// Per player a std::vector of {hunter settler, target settler} pairs (the
// 16-byte vector objects start at 0x7e3688; like g_aiCatapultVectors this
// array starts at the first vector's begin pointer). AI::Army::Reset
// (0x591e00) empties them.
inline constexpr GameGlobal<ai::AiVector[20]> g_aiHunterVectors{0x7e368c};
// Per population stage, the extra buildings wanted per settler (floats
// set by static initializers, 0x566xxx: (wanted[s + 1] - wanted[s]) /
// (stages[s + 1] - stages[s])). Stored in reverse: stage s at [3 - s].
inline constexpr GameGlobal<float[4]> g_aiWantedWeaponsmithSlopes{0x7e1290};
inline constexpr GameGlobal<float[4]> g_aiWantedGoldMineSlopes{0x7e12b0};
// The extra carriers wanted per settler for the residences condition
// (0x568890; static initializers 0x566130.., 0.5, 0.5, 0.375, 0.2 from
// g_aiWantedCarriers), stored in reverse like the tables above.
inline constexpr GameGlobal<float[4]> g_aiResidenceSlopes{0x7e1380};
// Per building type, the tool its worker needs and the worker's settler type
// (a constant table in .rdata; AiToolAndWorker; AI::OrderMissingTools
// 0x59bf70, CheckWorkerOrTool 0x57abc0; the type is not range-checked).
inline constexpr GameGlobal<const ai::AiToolAndWorker[57]> g_aiBuildingToolAndWorker{0x732804};
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
// AI::Census::CountSettlers 0x55b880: the step 0..8 of its slice state
// machine and the last settler id counted so far.
inline constexpr GameGlobal<std::uint32_t> g_aiCensusStep{0x7e06e0};
inline constexpr GameGlobal<std::uint32_t> g_aiCensusCursor{0x7e0578};
// AI::RunMilitary 0x58f990: per player the steps since the last
// AttackEnemyCatapults 0x593180 (runs above 50; was g_aiPerPlayer7e79d0).
inline constexpr GameGlobal<std::uint32_t[20]> g_aiCatapultHuntSteps{0x7e79d0};
// AI::RunMilitary: 1 asks the upkeep to run RebuildAssignedSettlers at once
// (AI::LoadState 0x596380 sets it); 1 also skips the next reset request
// (g_aiSkipMilitaryReset).
inline constexpr GameGlobal<std::uint8_t> g_aiRebuildSettlersPending{0x7e7cc8};
// 1 asks T_ATTACK_AGENT::Run 0x55ec10 to pass the reset request to
// AI::RunMilitary (set by the CComputerPlayer constructor 0x550900, cleared
// after each Run).
inline constexpr GameGlobal<std::uint8_t> g_aiMilitaryResetRequest{0x7e3410};
inline constexpr GameGlobal<std::uint8_t> g_aiSkipMilitaryReset{0x7e7cc9};
// Cleared by RunMilitary's reset (no other access found).
inline constexpr GameGlobal<std::uint32_t> g_aiUnk7e7ccc{0x7e7ccc};
inline constexpr GameGlobal<std::uint32_t> g_aiUnk7e7cd0{0x7e7cd0};
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
using GetDriveTypeAImport = std::uint32_t(__stdcall*)(const char* root);
inline constexpr GameGlobal<SleepImport> g_importSleep{0x6550dc};
inline constexpr GameGlobal<CloseHandleImport> g_importCloseHandle{0x655180};
inline constexpr GameGlobal<TimeGetTimeImport> g_importTimeGetTime{0x655358};  // from _INMM.dll
inline constexpr GameGlobal<OutputDebugStringAImport> g_importOutputDebugStringA{0x6550b0};
inline constexpr GameGlobal<GetDriveTypeAImport> g_importGetDriveTypeA{0x6550a4};
using UuidToStringAImport = std::int32_t(__stdcall*)(const void* uuid, char** text);
using RpcStringFreeAImport = std::int32_t(__stdcall*)(char** text);
inline constexpr GameGlobal<UuidToStringAImport> g_importUuidToStringA{0x655298};    // RPCRT4.dll
inline constexpr GameGlobal<RpcStringFreeAImport> g_importRpcStringFreeA{0x65529c};  // RPCRT4.dll
// ---- wip/settings-load (session 58): GameSettings::LoadSaveGame's GUID for a savegame without chunk 0x1a.
using CoCreateGuidImport = std::int32_t(__stdcall*)(void* guid);
inline constexpr GameGlobal<CoCreateGuidImport> g_importCoCreateGuid{0x655378};  // ole32.dll
using MciSendCommandAImport = std::uint32_t(__stdcall*)(std::uint32_t device, std::uint32_t message,
                                                         std::uint32_t flags, void* parameters);
using PostMessageAImport = int(__stdcall*)(void* window, std::uint32_t message, std::uint32_t wParam,
                                           std::uint32_t lParam);
inline constexpr GameGlobal<MciSendCommandAImport> g_importMciSendCommandA{0x65536c};  // from _INMM.dll
inline constexpr GameGlobal<PostMessageAImport> g_importPostMessageA{0x6552f0};        // USER32.dll
using InterlockedExchangeImport = std::int32_t(__stdcall*)(volatile std::int32_t* target, std::int32_t value);
inline constexpr GameGlobal<InterlockedExchangeImport> g_importInterlockedExchange{0x6550e4};  // KERNEL32.dll
// session 58 (wip/framework-errors): the exception descriptions
// (CHResult::Describe 0x6134d0, CMCIException::Describe 0x614250).
using FormatMessageAImport = std::uint32_t(__stdcall*)(std::uint32_t flags, const void* source, std::uint32_t id,
                                                        std::uint32_t language, char* buffer, std::uint32_t size,
                                                        void* arguments);
using LocalFreeImport = void*(__stdcall*)(void* memory);
using MciGetErrorStringAImport = int(__stdcall*)(std::uint32_t error, char* text, std::uint32_t size);
inline constexpr GameGlobal<FormatMessageAImport> g_importFormatMessageA{0x6551ec};
inline constexpr GameGlobal<LocalFreeImport> g_importLocalFree{0x6551e8};
inline constexpr GameGlobal<MciGetErrorStringAImport> g_importMciGetErrorStringA{0x655354};  // from _INMM.dll
// CMCIException::Describe copies this word (zero) to the start of its
// 1024-byte text buffer (the compiler's `char text[1024] = ""`).
inline constexpr GameGlobal<std::uint16_t> g_emptyTextWord{0x7e9178};
// The synchronization objects and windows of the framework
// (win32_synchronize.cpp).
using HandleCallImport = int(__stdcall*)(void* handle);  // SetEvent, ResetEvent, PulseEvent, DestroyWindow
using ReleaseSemaphoreImport = int(__stdcall*)(void* semaphore, std::int32_t count, std::int32_t* previous);
using CreateEventAImport = void*(__stdcall*)(void* security, std::uint32_t manualReset, std::uint32_t initialState,
                                             const char* name);
using CreateSemaphoreAImport = void*(__stdcall*)(void* security, std::int32_t initial, std::int32_t maximum,
                                                 const char* name);
using CreateMutexAImport = void*(__stdcall*)(void* security, std::uint32_t initialOwner, const char* name);
using SetThreadPriorityImport = int(__stdcall*)(void* thread, std::int32_t priority);
using PostThreadMessageAImport = int(__stdcall*)(std::uint32_t thread, std::uint32_t message, std::uint32_t wParam,
                                                 std::uint32_t lParam);
using WaitForSingleObjectImport = std::uint32_t(__stdcall*)(void* handle, std::uint32_t milliseconds);
inline constexpr GameGlobal<HandleCallImport> g_importSetEvent{0x6550e0};
inline constexpr GameGlobal<HandleCallImport> g_importResetEvent{0x6550bc};
inline constexpr GameGlobal<HandleCallImport> g_importPulseEvent{0x6550d4};
inline constexpr GameGlobal<ReleaseSemaphoreImport> g_importReleaseSemaphore{0x6550d0};
inline constexpr GameGlobal<CreateEventAImport> g_importCreateEventA{0x6551c4};
inline constexpr GameGlobal<CreateSemaphoreAImport> g_importCreateSemaphoreA{0x6550f0};
inline constexpr GameGlobal<CreateMutexAImport> g_importCreateMutexA{0x6551cc};
inline constexpr GameGlobal<SetThreadPriorityImport> g_importSetThreadPriority{0x6551e0};
inline constexpr GameGlobal<HandleCallImport> g_importDestroyWindow{0x655348};               // USER32.dll
inline constexpr GameGlobal<PostThreadMessageAImport> g_importPostThreadMessageA{0x6552e0};  // USER32.dll
inline constexpr GameGlobal<WaitForSingleObjectImport> g_importWaitForSingleObject{0x6551c0};

// Win32\Mapping.cpp, Win32\Detach.cpp and the graphics loader.
using CreateFileAImport = void*(__stdcall*)(const char* name, std::uint32_t access, std::uint32_t share,
                                            void* security, std::uint32_t disposition, std::uint32_t flags,
                                            void* templateFile);
using ReadFileImport = int(__stdcall*)(void* file, void* buffer, std::uint32_t size, std::uint32_t* done,
                                       void* overlapped);
using WriteFileImport = int(__stdcall*)(void* file, const void* buffer, std::uint32_t size, std::uint32_t* done,
                                        void* overlapped);
using SetFilePointerImport = std::uint32_t(__stdcall*)(void* file, std::int32_t low, std::int32_t* high,
                                                       std::uint32_t method);
using GetFileSizeImport = std::uint32_t(__stdcall*)(void* file, std::uint32_t* high);
using CreateFileMappingAImport = void*(__stdcall*)(void* file, void* security, std::uint32_t protect,
                                                   std::uint32_t sizeHigh, std::uint32_t sizeLow, const char* name);
using MapViewOfFileImport = void*(__stdcall*)(void* mapping, std::uint32_t access, std::uint32_t offsetHigh,
                                              std::uint32_t offsetLow, std::uint32_t size);
using UnmapViewOfFileImport = int(__stdcall*)(const void* view);
using GetLastErrorImport = std::uint32_t(__stdcall*)();
using GetSystemInfoImport = void(__stdcall*)(void* info);
using VirtualQueryImport = std::uint32_t(__stdcall*)(const void* address, void* info, std::uint32_t size);
using VirtualProtectImport = int(__stdcall*)(void* address, std::uint32_t size, std::uint32_t protect,
                                             std::uint32_t* old);
using GetCurrentDirectoryAImport = std::uint32_t(__stdcall*)(std::uint32_t size, char* buffer);
using SetCurrentDirectoryAImport = int(__stdcall*)(const char* path);
using FindFirstFileAImport = void*(__stdcall*)(const char* name, void* data);
using FindCloseImport = int(__stdcall*)(void* find);
inline constexpr GameGlobal<CreateFileAImport> g_importCreateFileA{0x655194};
inline constexpr GameGlobal<ReadFileImport> g_importReadFile{0x6551f4};
inline constexpr GameGlobal<WriteFileImport> g_importWriteFile{0x6551a0};
inline constexpr GameGlobal<SetFilePointerImport> g_importSetFilePointer{0x6551f8};
inline constexpr GameGlobal<GetFileSizeImport> g_importGetFileSize{0x655190};
inline constexpr GameGlobal<CreateFileMappingAImport> g_importCreateFileMappingA{0x65518c};
inline constexpr GameGlobal<MapViewOfFileImport> g_importMapViewOfFile{0x655188};
inline constexpr GameGlobal<UnmapViewOfFileImport> g_importUnmapViewOfFile{0x655184};
inline constexpr GameGlobal<GetLastErrorImport> g_importGetLastError{0x6550e8};
inline constexpr GameGlobal<GetSystemInfoImport> g_importGetSystemInfo{0x655200};
inline constexpr GameGlobal<VirtualQueryImport> g_importVirtualQuery{0x6551fc};
inline constexpr GameGlobal<VirtualProtectImport> g_importVirtualProtect{0x6550a8};
inline constexpr GameGlobal<GetCurrentDirectoryAImport> g_importGetCurrentDirectoryA{0x6550b8};
inline constexpr GameGlobal<SetCurrentDirectoryAImport> g_importSetCurrentDirectoryA{0x6550c8};
inline constexpr GameGlobal<FindFirstFileAImport> g_importFindFirstFileA{0x65508c};
inline constexpr GameGlobal<FindCloseImport> g_importFindClose{0x655098};

// DirectX::CDirectSound (DirectX\*.cpp): LoadLibraryA / GetProcAddress
// load dsound.dll in CDirectSound::Connect; the DirectSound object
// registers itself in g_directSound (constructor 0x5c1cc0, cleared by the
// destructor), where the buffers find it.
using LoadLibraryAImport = void*(__stdcall*)(const char* name);
using GetProcAddressImport = void*(__stdcall*)(void* module, const char* name);
inline constexpr GameGlobal<LoadLibraryAImport> g_importLoadLibraryA{0x65519c};
inline constexpr GameGlobal<GetProcAddressImport> g_importGetProcAddress{0x6551b4};
inline constexpr GameGlobal<void*> g_directSound{0x7e8c0c};
// DirectX::CSoundEffect::Play (0x5c2fc0): the next key of a duplicate buffer.
inline constexpr GameGlobal<std::uint32_t> g_soundDuplicateCounter{0x7a0c00};

// Win32::DetachMemory (0x61ab60): the page size, read once with
// GetSystemInfo (dwPageSize) and kept (0 until the first call).
inline constexpr GameGlobal<std::uint32_t> g_detachPageSize{0x7e9220};

// ---- wip/ddraw-render: the DirectDraw wrappers (game/framework/directx_draw*.cpp)
using SetRectImport = int(__stdcall*)(void* rect, std::int32_t left, std::int32_t top, std::int32_t right,
                                      std::int32_t bottom);
using SendMessageAImport = std::int32_t(__stdcall*)(void* window, std::uint32_t message, std::uint32_t wParam,
                                                    std::uint32_t lParam);
using CoCreateInstanceImport = std::int32_t(__stdcall*)(const void* clsid, void* outer, std::uint32_t context,
                                                        const void* iid, void** out);
inline constexpr GameGlobal<SetRectImport> g_importSetRect{0x655330};                    // USER32.dll
inline constexpr GameGlobal<SendMessageAImport> g_importSendMessageA{0x6552f4};          // USER32.dll
inline constexpr GameGlobal<CoCreateInstanceImport> g_importCoCreateInstance{0x655384};  // ole32.dll
// The DirectX::CGameDrawer (in the game the CDrawer); set by its
// constructor 0x5b6c80, cleared by the destructor.
inline constexpr GameGlobal<void*> g_drawer{0x7e8bf0};
// The primary surface's RECT (0, 0, width, height), copied by
// CGameDrawer::SetPrimarySurface 0x5b7040, zeroed when it goes.
inline constexpr GameGlobal<std::int32_t[4]> g_primarySurfaceRect{0x7e8bf8};
// wip/ui-gdi batch: GDI32 imports of the UI text code (CControl::ShowTooltip
// 0x418550 measures the tooltip text; test/fake_gdi.h fakes them).
using SelectObjectImport = void*(__stdcall*)(void* dc, void* object);
using GetTextExtentPoint32AImport = int(__stdcall*)(void* dc, const char* text, std::int32_t length, void* size);
using SetBkModeImport = std::int32_t(__stdcall*)(void* dc, std::int32_t mode);
using SetTextColorImport = std::uint32_t(__stdcall*)(void* dc, std::uint32_t colour);
using SetBkColorImport = std::uint32_t(__stdcall*)(void* dc, std::uint32_t colour);
using TextOutAImport = int(__stdcall*)(void* dc, std::int32_t x, std::int32_t y, const char* text, std::int32_t length);
inline constexpr GameGlobal<SelectObjectImport> g_importSelectObject{0x655030};
inline constexpr GameGlobal<GetTextExtentPoint32AImport> g_importGetTextExtentPoint32A{0x655038};
inline constexpr GameGlobal<SetBkModeImport> g_importSetBkMode{0x655054};
inline constexpr GameGlobal<SetTextColorImport> g_importSetTextColor{0x655058};
inline constexpr GameGlobal<SetBkColorImport> g_importSetBkColor{0x655078};
inline constexpr GameGlobal<TextOutAImport> g_importTextOutA{0x655068};
// wip/ui-gdi batch: the dropdown whose list is open (CDropdown::OnArrowClicked
// 0x422590 sets it; OnFocusChanged and Draw close other open lists).
inline constexpr GameGlobal<std::uint32_t> g_openDropdown{0x7a8244};

}  // namespace s3re::game
