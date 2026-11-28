// CLogicThread::LoadSaveGame (0x00508fa0), Volker\GameDataSave.cpp:
// (CMapFile* file) ret 4, thiscall on the logic thread (world = +0x3a0).
// CLogicThread::OnStart calls it for a savegame; the file was opened by the
// menus (GameSettings::LoadSaveGame 0x432b00). EAX is not used by the
// caller (asm_tools eax-use: clobbered), so it is void here.
//
// It copies the savegame chunks (savegame_chunks.h; table in
// re/file_formats.md, "Savegame chunks") into the world and a few globals,
// in this order: 0 (only the version; never released), 1, 2/0, 2/1, 3/row
// for each row, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, then by version
// 0x18 + 0x19, 0x1c, 0x1d, 0x1f (older savegames get defaults), clears
// Player +8 before version 0x89, unblocks the castle tiles and frees every
// economy sector no tile refers to. Each chunk is released after its copy.
//
// Quirks kept: the copies trust the chunks' counts and sizes (a short or
// missing chunk makes the original read past it or crash); the events
// chunk 2/1 fills only 12,000 of the event ring's 16,000 bytes; the drawn
// camera position is set to 10,000,000 instead of the saved one (forces a
// redraw) and minimap_cycle to 63.

#include "game/fileformats/map_file.h"
#include "game/fileformats/savegame_chunks.h"
#include "game/debug_log.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/logic/logic_thread_layout.h"
#include "game/texts.h"
#include "game/world.h"
#include "hooks.h"
#include "msvc6/string.h"

#include <cstddef>
#include <cstring>

namespace s3re::game {

using fileformats::CMapFile;
using fileformats::SaveChunk;

namespace {

// The callees, at their S3.EXE addresses (all replaced and verified).
using GetChunkFn = std::uint8_t*(__fastcall*)(CMapFile* self, void* edx, std::uint32_t type, std::uint32_t subIndex);
using ReleaseChunkFn = std::uint32_t(__fastcall*)(CMapFile* self, void* edx, std::uint32_t type,
                                                  std::uint32_t subIndex);
using WorldFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx);
using AssignTextOrEmptyFn = msvc6::String*(__fastcall*)(msvc6::String* self, void* edx, const char* text);
constexpr std::uintptr_t kGetChunk = 0x004a60f0;
constexpr std::uintptr_t kReleaseChunk = 0x004a6310;
constexpr std::uintptr_t kUnblockCastleTiles = 0x004dfa60;
constexpr std::uintptr_t kAssignTextOrEmpty = 0x00413d70;

// The drawn camera position after loading: far off, so the next frame
// redraws everything (0x508fd9).
constexpr std::int32_t kCameraNotDrawn = 10000000;
constexpr std::uint32_t kMinimapCycleAfterLoad = 63;
constexpr std::int32_t kDefaultObjectiveTimeout = 60;
constexpr std::uint32_t kAreaCount = 4096;
constexpr std::uint8_t kFreeArea = 0xff;

template <typename Chunk>
const Chunk* GetChunk(CMapFile* file, SaveChunk type, std::uint32_t subIndex = 0) {
    return reinterpret_cast<const Chunk*>(
        reinterpret_cast<GetChunkFn>(kGetChunk)(file, nullptr, static_cast<std::uint32_t>(type), subIndex));
}

void ReleaseChunk(CMapFile* file, SaveChunk type, std::uint32_t subIndex = 0) {
    reinterpret_cast<ReleaseChunkFn>(kReleaseChunk)(file, nullptr, static_cast<std::uint32_t>(type), subIndex);
}

// Copies `bytes` from a chunk into a world field (the original's rep movs).
void CopyFrom(void* target, const void* source, std::uint32_t bytes) {
    std::memcpy(target, source, bytes);
}

// A table chunk's records 0..high water (high water + 1 records); the byte
// count wraps at 32 bits like the original's.
std::uint32_t TableBytes(std::uint32_t highWater, std::uint32_t recordSize) {
    return (highWater + 1) * recordSize;
}

// 0x508fd1..0x509175: chunk 1, "general".
void LoadGeneral(CGameData* world, CMapFile* file) {
    const auto* general = GetChunk<fileformats::SaveGeneralChunk>(file, SaveChunk::General);
    world->camera_drawn_x = kCameraNotDrawn;
    world->camera_drawn_y = kCameraNotDrawn;
    world->camera_x = general->camera_x;
    world->camera_y = general->camera_y;
    world->map_size = general->map_size;
    world->random_state[0] = general->random_state[0];
    world->random_state[1] = general->random_state[1];
    world->random_state[2] = general->random_state[2];
    world->counter_110a57c = general->counter_110a57c;
    world->fog_cycle = general->fog_cycle;
    world->minimap_cycle = kMinimapCycleAfterLoad;
    g_lastDrawnTick = general->last_drawn_tick;
    g_gameTick = general->game_tick;
    // 0x50909d..0x509113: the player info table.
    PlayerInfo* info = &g_playerInfo[0];
    for (const fileformats::SavedPlayerInfo& saved : general->players) {
        info->unk00 = saved.unk00;
        info->unk04 = saved.unk04;
        reinterpret_cast<AssignTextOrEmptyFn>(kAssignTextOrEmpty)(&info->name, nullptr, saved.name);
        std::memcpy(info->fields, saved.fields, sizeof(info->fields));
        ++info;
    }
    g_strengthDivisor = general->active_players;
    g_gameOutcome = general->game_outcome;
    g_campaignMission = general->mission_minus_one + 1;
    world->tutorial_wait = general->unk2d304;
    world->tutorial_step = general->unk2d308;
    world->presence_max_player = general->presence_max_player;
    ReleaseChunk(file, SaveChunk::General);
}

// 0x50917a..0x50920e: chunks 2/0 (pending events) and 2/1 (events).
void LoadEvents(CGameData* world, CMapFile* file) {
    const auto* pending = GetChunk<fileformats::SaveEventsChunk>(file, SaveChunk::Events, 0);
    world->pending_event_index.read = pending->read;
    world->pending_event_index.write = pending->write;
    CopyFrom(world->pending_events, pending->records, fileformats::kSavedEventBytes);
    ReleaseChunk(file, SaveChunk::Events, 0);
    const auto* events = GetChunk<fileformats::SaveEventsChunk>(file, SaveChunk::Events, 1);
    world->event_index.read = events->read;
    world->event_index.write = events->write;
    // Only 750 of the 1000 events (the original copies 3000 dwords).
    CopyFrom(world->events, events->records, fileformats::kSavedEventBytes);
    ReleaseChunk(file, SaveChunk::Events, 1);
}

// 0x50920e..0x509293: one chunk per tile row (the map size is re-read
// each row, as the original does).
void LoadTiles(CGameData* world, CMapFile* file) {
    for (std::int32_t row = 0; row < static_cast<std::int32_t>(world->map_size); ++row) {
        const auto* chunk = GetChunk<fileformats::SaveDataChunk>(file, SaveChunk::TileRow,
                                                                    static_cast<std::uint32_t>(row));
        CopyFrom(world->tiles[row], chunk->data, world->map_size * sizeof(layout::Tile));
        ReleaseChunk(file, SaveChunk::TileRow, static_cast<std::uint32_t>(row));
    }
}

// 0x509293..0x50935f: chunk 4, "sectors".
void LoadSectors(CGameData* world, CMapFile* file) {
    const auto* sectors = GetChunk<fileformats::SaveSectorsChunk>(file, SaveChunk::Sectors);
    world->walk_region_high_water = sectors->walk_region_high_water;
    world->area_high_water = sectors->area_high_water;
    world->ids4096_high_water = sectors->ids4096_high_water;
    world->wide_region_high_water = sectors->wide_region_high_water;
    CopyFrom(world->walk_region_used, sectors->walk_region_used, sizeof(sectors->walk_region_used));
    CopyFrom(world->ids4096_used, sectors->ids4096_used, sizeof(sectors->ids4096_used));
    CopyFrom(world->wide_region_used, sectors->wide_region_used, sizeof(sectors->wide_region_used));
    CopyFrom(world->areas, sectors->areas, TableBytes(world->area_high_water, sizeof(layout::Area)));
    ReleaseChunk(file, SaveChunk::Sectors);
}

// 0x50935f..0x5093b0: chunk 6, "player".
void LoadPlayers(CGameData* world, CMapFile* file) {
    const auto* players = GetChunk<fileformats::SavePlayerChunk>(file, SaveChunk::Player);
    world->local_player = players->local_player;
    CopyFrom(world->player_races, players->player_races, sizeof(players->player_races));
    static_assert(sizeof(players->players) == sizeof(world->players));
    CopyFrom(world->players, players->players, sizeof(players->players));
    ReleaseChunk(file, SaveChunk::Player);
}

// Chunks 7, 8, 9, 13, 14: a table's high-water mark and its records.
template <typename Record>
void LoadTable(CMapFile* file, SaveChunk type, std::uint32_t& highWater, Record* records) {
    const auto* table = GetChunk<fileformats::SaveTableChunk>(file, type);
    highWater = table->high_water;
    CopyFrom(records, table->records, TableBytes(highWater, sizeof(Record)));
    ReleaseChunk(file, type);
}

// 0x5094a7..0x509507: chunk 10, "diggertab".
void LoadDiggerTab(CGameData* world, CMapFile* file) {
    const auto* digger = GetChunk<fileformats::SaveDiggerTabChunk>(file, SaveChunk::DiggerTab);
    static_assert(sizeof(digger->footprint_scratch) == sizeof(world->footprint_scratch));
    static_assert(sizeof(digger->site_cache) == sizeof(world->site_cache));
    CopyFrom(world->footprint_scratch[0], digger->footprint_scratch[0], sizeof(digger->footprint_scratch[0]));
    CopyFrom(world->footprint_scratch[1], digger->footprint_scratch[1], sizeof(digger->footprint_scratch[1]));
    CopyFrom(world->site_cache, digger->site_cache, sizeof(digger->site_cache));
    ReleaseChunk(file, SaveChunk::DiggerTab);
}

// 0x509507..0x509578: chunks 11 (minimap) and 12, "activesettlers".
void LoadMinimapAndLists(CGameData* world, CMapFile* file) {
    const auto* minimap = GetChunk<fileformats::SaveDataChunk>(file, SaveChunk::Minimap);
    CopyFrom(world->minimap, minimap->data, sizeof(world->minimap));
    ReleaseChunk(file, SaveChunk::Minimap);
    const auto* lists = GetChunk<fileformats::SaveActiveSettlersChunk>(file, SaveChunk::ActiveSettlers);
    static_assert(sizeof(lists->player_lists) == sizeof(world->player_lists));
    static_assert(sizeof(lists->player_list_lengths) == sizeof(world->player_list_lengths));
    CopyFrom(world->player_lists, lists->player_lists, sizeof(lists->player_lists));
    CopyFrom(world->player_list_lengths, lists->player_list_lengths, sizeof(lists->player_list_lengths));
    ReleaseChunk(file, SaveChunk::ActiveSettlers);
}

// 0x509618..0x5096a2: chunks 15, "messages", and 16, "warmap".
void LoadMessagesAndWarMap(CGameData* world, CMapFile* file) {
    const auto* messages = GetChunk<fileformats::SaveMessagesChunk>(file, SaveChunk::Messages);
    static_assert(sizeof(messages->notifications) == sizeof(world->notifications));
    static_assert(sizeof(messages->notification_timers) == sizeof(world->notification_timers));
    CopyFrom(world->notifications, messages->notifications, sizeof(messages->notifications));
    CopyFrom(world->notification_timers, messages->notification_timers, sizeof(messages->notification_timers));
    ReleaseChunk(file, SaveChunk::Messages);
    const auto* warMap = GetChunk<fileformats::SaveWarMapChunk>(file, SaveChunk::WarMap);
    static_assert(sizeof(warMap->presence_a) == sizeof(world->presence_a));
    CopyFrom(world->presence_a, warMap->presence_a, sizeof(warMap->presence_a));
    CopyFrom(world->presence_b, warMap->presence_b, sizeof(warMap->presence_b));
    ReleaseChunk(file, SaveChunk::WarMap);
}

// 0x5096b4..0x5098bc: the chunks newer savegames have, or their defaults.
void LoadVersionedChunks(CGameData* world, CMapFile* file, std::int32_t version) {
    constexpr std::uint32_t kColourEntries = 20;
    if (version >= fileformats::kVersionVictoryAndColours) {
        GameDebugLog(texts::kLoadingVictoryConditions);
        const auto* victory = GetChunk<fileformats::SaveDataChunk>(file, SaveChunk::VictoryConditions);
        CopyFrom(&world->victory_conditions, victory->data, sizeof(world->victory_conditions));
        ReleaseChunk(file, SaveChunk::VictoryConditions);
        GameDebugLog(texts::kLoadingPlayerColors);
        const auto* colours = GetChunk<fileformats::SaveDataChunk>(file, SaveChunk::PlayerColours);
        CopyFrom(&g_playerColourIndex[0], colours->data, kColourEntries * sizeof(std::uint16_t));
        if (world->victory_conditions.protect_players != 0) {
            g_victoryProtectActive = 1;
        }
        ReleaseChunk(file, SaveChunk::PlayerColours);
    } else {
        GameDebugLog(texts::kClearingPlayerColors);
        for (std::uint32_t i = 0; i < kColourEntries; ++i) {
            g_playerColourIndex[i] = static_cast<std::uint16_t>(i);
        }
    }

    if (version >= fileformats::kVersionProdStat) {
        GameDebugLog(texts::kLoadingProdStat);
        static_assert(sizeof(world->statistics) == 0x2d000);
        const auto* prodStat = GetChunk<fileformats::SaveDataChunk>(file, SaveChunk::ProdStat);
        CopyFrom(world->statistics, prodStat->data, sizeof(world->statistics));
        ReleaseChunk(file, SaveChunk::ProdStat);
    } else {
        GameDebugLog(texts::kClearingProdStat);
        std::memset(world->statistics, 0, sizeof(world->statistics));
    }

    // objectives_enabled and the 7 objective ids, two dwords in the original.
    static_assert(offsetof(CGameData, objectives) == offsetof(CGameData, objectives_enabled) + 1);
    constexpr std::uint32_t kObjectiveBytes = 1 + sizeof(world->objectives);
    if (version >= fileformats::kVersionNewGameObjective) {
        GameDebugLog(texts::kLoadingObjective);
        const auto* objective = GetChunk<fileformats::SaveObjectiveChunk>(file, SaveChunk::NewGameObjective);
        CopyFrom(&world->objectives_enabled, objective->objectives, kObjectiveBytes);
        ReleaseChunk(file, SaveChunk::NewGameObjective);
    } else {
        GameDebugLog(texts::kClearingObjective);
        std::memset(&world->objectives_enabled, 0, kObjectiveBytes);
    }

    if (version >= fileformats::kVersionObjectiveTimeout) {
        GameDebugLog(texts::kLoadingObjectiveTimeout);
        const auto* timeout = GetChunk<fileformats::SaveTimeoutChunk>(file, SaveChunk::NewGameObjectiveTimeout);
        world->objective_timeout = timeout->objective_timeout;
        ReleaseChunk(file, SaveChunk::NewGameObjectiveTimeout);
    } else {
        GameDebugLog(texts::kClearingObjectiveTimeout);
        world->objective_timeout = kDefaultObjectiveTimeout;
    }

    if (version < fileformats::kVersionPlayerUnk008) {
        for (layout::Player& player : world->players) {
            player.unk008 = 0;
        }
    }
}

// 0x5098c7..0x509962: every economy sector that no tile of the map refers
// to is freed ("LOAD: Dropped eco %i" when it still had a player).
void DropUnusedSectors(CGameData* world) {
    std::uint8_t used[kAreaCount] = {};
    const std::int32_t size = static_cast<std::int32_t>(world->map_size);
    for (std::int32_t x = 0; x < size; ++x) {
        for (std::int32_t y = 0; y < size; ++y) {
            const std::uint16_t area = world->tiles[x][y].area;
            if (area != 0 && area < kAreaCount) {
                used[area] = 1;
            }
        }
    }
    for (std::uint32_t area = 0; area < kAreaCount; ++area) {
        if (used[area] != 0) {
            continue;
        }
        if (world->areas[area].player != kFreeArea) {
            GameDebugLog(texts::kDroppedEco, area);
        }
        world->areas[area].player = kFreeArea;
    }
}

}  // namespace

// Purpose: loads a savegame: copies its chunks back into the world and the
// game's globals, so the game continues where it was saved.
void __fastcall CLogicThread_LoadSaveGame(CLogicThread* self, void* /*edx*/, CMapFile* file) {
    const auto* header = GetChunk<fileformats::SaveHeaderChunk>(file, SaveChunk::Header);
    LoadGeneral(self->world, file);
    LoadEvents(self->world, file);
    LoadTiles(self->world, file);
    LoadSectors(self->world, file);
    LoadPlayers(self->world, file);
    CGameData* world = self->world;
    LoadTable(file, SaveChunk::Settlers, world->settler_high_water, world->settlers);
    LoadTable(file, SaveChunk::Buildings, world->building_high_water, world->buildings);
    LoadTable(file, SaveChunk::Piles, world->pile_high_water, world->piles);
    LoadDiggerTab(world, file);
    LoadMinimapAndLists(world, file);
    LoadTable(file, SaveChunk::Trade, world->trade_high_water, world->trades);
    LoadTable(file, SaveChunk::Timer, world->effects_high_water, world->effects);
    LoadMessagesAndWarMap(world, file);

    GameDebugLog(texts::kSavefileVersion, header->version);
    LoadVersionedChunks(world, file, header->version);
    reinterpret_cast<WorldFn>(kUnblockCastleTiles)(world, nullptr);
    DropUnusedSectors(world);
    world->minimap_ready = 1;
}

using LoadSaveGameFn = void(__fastcall*)(CLogicThread* self, void* edx, CMapFile* file);
S3RE_HOOK(0x00508fa0, CLogicThread_LoadSaveGame, LoadSaveGameFn, Original_CLogicThread_LoadSaveGame,
          CLogicThread_LoadSaveGame);

}  // namespace s3re::game
