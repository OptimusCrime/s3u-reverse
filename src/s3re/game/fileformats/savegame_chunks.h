#pragma once

// The chunks of a savegame (file type 11) as CLogicThread::LoadSaveGame
// (0x508fa0) reads them and CLogicThread::SaveGame (0x509980) writes them.
// Every chunk starts with the 8-byte ChunkHeader (map_file.h); the names
// in quotes are SaveGame's out-of-memory labels. Table in
// re/file_formats.md, "Savegame chunks".

#include "game/fileformats/map_file.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game::fileformats {

// Chunk types of a savegame (ChunkKey type; the sub index is 0 except for
// the events and the tile rows).
enum class SaveChunk : std::uint32_t {
    Header = 0,
    General = 1,            // "general"
    Events = 2,             // "events": sub 0 pending events, sub 1 events
    TileRow = 3,            // one per tile row, sub = row
    Sectors = 4,            // "sectors" (economy sectors and the region id tables)
    Player = 6,             // "player"
    Settlers = 7,           // "settler"
    Buildings = 8,          // "buildings"
    Piles = 9,              // "piles"
    DiggerTab = 10,         // "diggertab"
    Minimap = 11,
    ActiveSettlers = 12,    // "activesettlers"
    Trade = 13,             // "trade"
    Timer = 14,             // "timer"
    Messages = 15,          // "messages"
    WarMap = 16,            // "warmap"
    VictoryConditions = 0x18,
    PlayerColours = 0x19,
    ProdStat = 0x1c,
    NewGameObjective = 0x1d,
    NewGameObjectiveTimeout = 0x1f,
};

// Savegame versions (chunk 0 +8) from which a chunk exists; older
// savegames get default values (LoadSaveGame 0x5096b4..0x5098bc).
inline constexpr std::int32_t kVersionVictoryAndColours = 0x82;
inline constexpr std::int32_t kVersionProdStat = 0x86;
inline constexpr std::int32_t kVersionPlayerUnk008 = 0x89;
inline constexpr std::int32_t kVersionNewGameObjective = 0x96;
inline constexpr std::int32_t kVersionObjectiveTimeout = 0x99;

#pragma pack(push, 1)

struct SaveHeaderChunk {        // chunk 0, 0x120 bytes
    ChunkHeader header;
    std::int32_t version;       // +0x08 (0xa0 in the game's own savegames)
    std::uint8_t unk0c[0x120 - 0x0c];  // build info, mission, time, map name (not read by LoadSaveGame)
};
static_assert(offsetof(SaveHeaderChunk, version) == 0x08);
static_assert(sizeof(SaveHeaderChunk) == 0x120);

// One entry of the player info table (g_playerInfo 0x7acfa8) as saved.
struct SavedPlayerInfo {
    std::int32_t unk00;         // +0x000
    std::int32_t unk04;         // +0x004
    char name[0x100];           // +0x008 the player's name (assigned up to its first zero)
    std::int32_t fields[11];    // +0x108 -> PlayerInfo +0x18..+0x40
};
static_assert(offsetof(SavedPlayerInfo, fields) == 0x108);
static_assert(sizeof(SavedPlayerInfo) == 0x134);

struct SaveGeneralChunk {       // chunk 1
    ChunkHeader header;
    std::int32_t camera_drawn_x;      // +0x08 not read on load (forced to 10,000,000)
    std::int32_t camera_drawn_y;      // +0x0c not read on load
    std::int32_t camera_x;            // +0x10
    std::int32_t camera_y;            // +0x14
    std::uint32_t map_size;           // +0x18
    std::uint16_t random_state[3];    // +0x1c
    std::uint8_t pad22[2];
    std::uint32_t counter_110a57c;    // +0x24
    std::uint32_t fog_cycle;          // +0x28
    std::uint32_t minimap_cycle;      // +0x2c not read on load (forced to 63)
    std::uint32_t last_drawn_tick;    // +0x30 g_lastDrawnTick
    std::uint32_t game_tick;          // +0x34 g_gameTick
    SavedPlayerInfo players[20];      // +0x38 g_playerInfo
    std::uint32_t active_players;     // +0x1848 g_strengthDivisor (the number of active players)
    std::int32_t game_outcome;        // +0x184c g_gameOutcome
    std::int32_t mission_minus_one;   // +0x1850 g_campaignMission - 1
    std::int32_t unk2d304;            // +0x1854
    std::int32_t unk2d308;            // +0x1858
    std::uint32_t presence_max_player;  // +0x185c
};
static_assert(offsetof(SaveGeneralChunk, camera_x) == 0x10);
static_assert(offsetof(SaveGeneralChunk, map_size) == 0x18);
static_assert(offsetof(SaveGeneralChunk, random_state) == 0x1c);
static_assert(offsetof(SaveGeneralChunk, counter_110a57c) == 0x24);
static_assert(offsetof(SaveGeneralChunk, last_drawn_tick) == 0x30);
static_assert(offsetof(SaveGeneralChunk, players) == 0x38);
static_assert(offsetof(SaveGeneralChunk, active_players) == 0x1848);
static_assert(offsetof(SaveGeneralChunk, presence_max_player) == 0x185c);
static_assert(sizeof(SaveGeneralChunk) == 0x1860);

// Chunks 2/0 and 2/1: a ring's read and write index, then 3000 dwords of
// records (the whole pending-event ring of 1000 x 12 bytes, but only the
// first 12,000 of the event ring's 16,000 bytes).
inline constexpr std::uint32_t kSavedEventBytes = 3000 * 4;
struct SaveEventsChunk {
    ChunkHeader header;
    std::uint32_t read;               // +0x08
    std::uint32_t write;              // +0x0c
    std::uint8_t records[kSavedEventBytes];  // +0x10
};
static_assert(offsetof(SaveEventsChunk, records) == 0x10);

// A chunk that is copied as raw bytes: 3/row (the tile records of one
// row, map_size * 24 bytes), 11 (the minimap), 0x18 (the victory
// conditions), 0x19 (20 player colour indexes), 0x1c (the production
// statistics).
struct SaveDataChunk {
    ChunkHeader header;
    std::uint8_t data[1];             // +0x08
};
static_assert(offsetof(SaveDataChunk, data) == 0x08);

struct SaveSectorsChunk {             // chunk 4
    ChunkHeader header;
    std::uint32_t walk_region_high_water;  // +0x08
    std::uint32_t area_high_water;         // +0x0c
    std::uint32_t ids4096_high_water;      // +0x10
    std::uint32_t wide_region_high_water;  // +0x14
    std::uint8_t walk_region_used[256];    // +0x18
    std::uint8_t ids4096_used[4096];       // +0x118
    std::uint8_t wide_region_used[8192];   // +0x1118
    std::uint8_t areas[1];                 // +0x3118 (area_high_water + 1) area records
};
static_assert(offsetof(SaveSectorsChunk, walk_region_used) == 0x18);
static_assert(offsetof(SaveSectorsChunk, ids4096_used) == 0x118);
static_assert(offsetof(SaveSectorsChunk, wide_region_used) == 0x1118);
static_assert(offsetof(SaveSectorsChunk, areas) == 0x3118);

struct SavePlayerChunk {              // chunk 6
    ChunkHeader header;
    std::int32_t local_player;        // +0x08
    std::int32_t player_races[20];    // +0x0c
    std::uint8_t players[20 * 0x148];  // +0x5c the Player records
};
static_assert(offsetof(SavePlayerChunk, player_races) == 0x0c);
static_assert(offsetof(SavePlayerChunk, players) == 0x5c);

// Chunks 7, 8, 9, 13, 14: a table's high-water mark, then its records
// 0..high water (high water + 1 records).
struct SaveTableChunk {
    ChunkHeader header;
    std::uint32_t high_water;         // +0x08
    std::uint8_t records[1];          // +0x0c
};
static_assert(offsetof(SaveTableChunk, records) == 0x0c);

struct SaveDiggerTabChunk {           // chunk 10
    ChunkHeader header;
    std::uint8_t footprint_scratch[2][31 * 31];  // +0x08
    std::uint8_t site_cache[0x8ceb0];            // +0x78a
};
static_assert(offsetof(SaveDiggerTabChunk, site_cache) == 0x78a);

struct SaveActiveSettlersChunk {      // chunk 12
    ChunkHeader header;
    std::uint16_t player_lists[20][10][100];     // +0x08
    std::int32_t player_list_lengths[20][10];    // +0x9c48
};
static_assert(offsetof(SaveActiveSettlersChunk, player_list_lengths) == 0x9c48);

struct SaveMessagesChunk {            // chunk 15
    ChunkHeader header;
    std::uint8_t notifications[0x12c0];          // +0x08
    std::uint8_t notification_timers[0x13b0];    // +0x12c8
};
static_assert(offsetof(SaveMessagesChunk, notification_timers) == 0x12c8);

struct SaveWarMapChunk {              // chunk 16
    ChunkHeader header;
    std::uint8_t presence_a[0xb400];  // +0x08
    std::uint8_t presence_b[0xb400];  // +0xb408
};
static_assert(offsetof(SaveWarMapChunk, presence_b) == 0xb408);

struct SaveObjectiveChunk {           // chunk 0x1d
    ChunkHeader header;
    std::uint8_t objectives[8];       // +0x08 objectives_enabled and the 7 objective ids
};

struct SaveTimeoutChunk {             // chunk 0x1f
    ChunkHeader header;
    std::int32_t objective_timeout;   // +0x08
};

#pragma pack(pop)

}  // namespace s3re::game::fileformats
