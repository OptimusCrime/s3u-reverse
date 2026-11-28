// CLogicThread::SaveGame (0x00509980), Volker\GameDataSave.cpp:
// (const std::string* name) ret 4, thiscall on the logic thread (world =
// +0x3a0). Returns a bool in AL (true: no file error); its only caller,
// CLogicThread::SaveGameToSlot (0x50ac40), reads AL (asm_tools eax-use).
//
// It creates the folders g_gameFolder + "Save" and + "Save\Multi" (_mkdir,
// result ignored), opens g_gameFolder + "Save\" + name for writing (a
// CMapFile on its stack, mode 2: type 11, checksum on Close) and writes the
// savegame chunks (savegame_chunks.h; table in re/file_formats.md,
// "Savegame chunks") in this order: 0 ("desc"), the game setup
// (GameSettings::SaveSetup 0x433490: 0x14, 0x1a, 0x1b, 0x1e), 1, 2/0, 2/1,
// 3/row for each row, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, the
// computer players' state (AI::SaveGlobals 0x551220 once per computer
// player: chunks 0x15..0x17 again for each), 0x18, 0x19, 0x1c, 0x1d, 0x1f.
// Then it closes the file; false if the file had an error before or after
// closing.
//
// Each chunk is built in a heap buffer (the game's operator new, freed after
// the write; "out of memory" with the chunk's label and the source line
// otherwise). Quirks kept:
//  - Fields the original doesn't write keep the heap block's old bytes:
//    chunk 0 after the map name's zero, chunk 1 +0x22..+0x23 and each player
//    name after its zero. (The original SaveSetup leaves stack bytes in its
//    chunks 0x14 and 0x1a; ours, game_settings_players.cpp, writes 0 there.)
//  - The map name and the player names are copied with strcpy: a map name
//    of 256 characters or more runs past the 0x120-byte header, a player
//    name of 256 or more into the fields after it (overwritten afterwards;
//    past the chunk for the last player).
//  - The events chunk 2/1 holds the whole event ring (16,000 bytes), of
//    which LoadSaveGame reads 12,000.
//  - Chunk 0x19's size is 40 bytes including its header, so the file holds
//    only the first 16 of the 20 colour indexes copied into the buffer.
//  - The computer players' chunks are written once per computer player,
//    the same three global tables each time (the loader finds the first).

#include "game/fileformats/map_file.h"
#include "game/fileformats/savegame_chunks.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/logic/logic_thread_layout.h"
#include "game/texts.h"
#include "game/ui/game_settings_layout.h"
#include "game/world.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace s3re::game {

using fileformats::ChunkHeader;
using fileformats::CMapFile;
using fileformats::SaveChunk;

namespace {

// The callees, at their S3.EXE addresses.
using ConstructFn = CMapFile*(__fastcall*)(CMapFile* self, void* edx, std::uint32_t rethrow);
using OpenFn = void(__fastcall*)(CMapFile* self, void* edx, const msvc6::String* path, std::uint32_t mode);
using MapFileVoidFn = void(__fastcall*)(CMapFile* self, void* edx);
using GetErrorFn = std::int32_t(__fastcall*)(CMapFile* self, void* edx);
using WriteChunkFn = void(__fastcall*)(CMapFile* self, void* edx, std::uint32_t type, std::uint32_t subIndex,
                                       const void* chunk);
using WriteChunkWithTailFn = void(__fastcall*)(CMapFile* self, void* edx, std::uint32_t type, std::uint32_t subIndex,
                                               const void* chunk, std::uint32_t tailBytes, const void* tail);
using WriteChunkDataFn = void(__fastcall*)(CMapFile* self, void* edx, std::uint32_t type, std::uint32_t subIndex,
                                           std::uint32_t bytes, const void* data);
using MakeDirectoryFn = int(__cdecl*)(const char* path);
using TimeFn = std::uint32_t(__cdecl*)(std::uint32_t* result);
using MapNameFn = msvc6::String*(__fastcall*)(const GameSettings* self, void* edx, msvc6::String* result);
using SaveSetupFn = void(__fastcall*)(GameSettings* self, void* edx, CMapFile* file, std::uint32_t tick,
                                      std::uint32_t seed);
using AiSaveFn = void(__fastcall*)(std::uint32_t computerPlayer, void* edx, CMapFile* file);
using ThrowOutOfMemoryFn = void(__fastcall*)(const char* text, const char* file, std::int32_t line);
constexpr std::uintptr_t kConstruct = 0x004a5a70;            // Dirk::CMapFile::CMapFile (verified)
constexpr std::uintptr_t kDestruct = 0x004a5b50;             // Dirk::CMapFile::~CMapFile (original)
constexpr std::uintptr_t kOpen = 0x004a5c80;                 // verified
constexpr std::uintptr_t kClose = 0x004a5f40;                // verified
constexpr std::uintptr_t kGetError = 0x004a60d0;             // verified
constexpr std::uintptr_t kWriteChunk = 0x004a6540;           // verified
constexpr std::uintptr_t kWriteChunkWithTail = 0x004a6600;   // verified
constexpr std::uintptr_t kWriteChunkData = 0x004a66d0;       // verified
constexpr std::uintptr_t kMakeDirectory = 0x00621556;        // the CRT's _mkdir
constexpr std::uintptr_t kTime = 0x0061f6a7;                 // the CRT's time
constexpr std::uintptr_t kMapName = 0x00436a60;              // GameSettings::MapName
constexpr std::uintptr_t kSaveSetup = 0x00433490;            // GameSettings::SaveSetup (ours, verified)
constexpr std::uintptr_t kAiSave = 0x00551220;               // AI::SaveGlobals (AI range)
constexpr std::uintptr_t kThrowOutOfMemory = 0x00610960;     // Win32::ThrowOutOfMemory, never returns

// The source lines of the out-of-memory reports, one per chunk buffer.
constexpr std::int32_t kLineHeader = 603;
constexpr std::int32_t kLineGeneral = 621;
constexpr std::int32_t kLineEvents = 666;
constexpr std::int32_t kLineSectors = 689;
constexpr std::int32_t kLinePlayer = 705;
constexpr std::int32_t kLineSettlers = 717;
constexpr std::int32_t kLineBuildings = 727;
constexpr std::int32_t kLinePiles = 737;
constexpr std::int32_t kLineDiggerTab = 747;
constexpr std::int32_t kLineActiveSettlers = 762;
constexpr std::int32_t kLineTrade = 773;
constexpr std::int32_t kLineTimer = 783;
constexpr std::int32_t kLineMessages = 793;
constexpr std::int32_t kLineWarMap = 804;
constexpr std::int32_t kLineVictoryConditions = 824;
constexpr std::int32_t kLinePlayerColours = 834;
constexpr std::int32_t kLineProdStat = 844;
constexpr std::int32_t kLineObjective = 854;
constexpr std::int32_t kLineObjectiveTimeout = 864;

constexpr std::uint32_t kColourEntries = 20;

// The chunk header of a table chunk (+8 the high-water mark, then the
// records, which go out as WriteChunkWithTail's tail).
struct TableHeader {
    ChunkHeader header;
    std::uint32_t high_water;  // +0x08
};
static_assert(sizeof(TableHeader) == 0x0c);

// The header part of an events chunk (2/0, 2/1).
struct EventsHeader {
    ChunkHeader header;
    std::uint32_t read;   // +0x08
    std::uint32_t write;  // +0x0c
};
static_assert(sizeof(EventsHeader) == 0x10);

// The fixed part of chunk 4 ("sectors"); the areas are the tail.
constexpr std::uint32_t kSectorsHeaderBytes = offsetof(fileformats::SaveSectorsChunk, areas);

// A chunk buffer from the game's heap (operator new 0x61efb9); out of
// memory: Win32::ThrowOutOfMemory(label, "...GameDataSave.cpp", line).
// The buffer is not cleared.
template <typename Chunk>
Chunk* NewChunk(std::uint32_t bytes, const GameText& label, std::int32_t line) {
    void* buffer = msvc6::GameOperatorNew(bytes);
    if (buffer == nullptr) {
        reinterpret_cast<ThrowOutOfMemoryFn>(kThrowOutOfMemory)(label.c_str(), texts::kGameDataSaveFile.c_str(), line);
    }
    return static_cast<Chunk*>(buffer);
}

// The chunk header the original writes as two words and the size.
void SetHeader(ChunkHeader& header, SaveChunk type, std::uint32_t subIndex, std::uint32_t size) {
    header.key = fileformats::ChunkKey(static_cast<std::uint32_t>(type), subIndex);
    header.size = size;
}

void WriteChunk(CMapFile* file, SaveChunk type, const void* chunk) {
    reinterpret_cast<WriteChunkFn>(kWriteChunk)(file, nullptr, static_cast<std::uint32_t>(type), 0, chunk);
}

void WriteChunkWithTail(CMapFile* file, SaveChunk type, std::uint32_t subIndex, const void* chunk,
                        std::uint32_t tailBytes, const void* tail) {
    reinterpret_cast<WriteChunkWithTailFn>(kWriteChunkWithTail)(file, nullptr, static_cast<std::uint32_t>(type),
                                                                subIndex, chunk, tailBytes, tail);
}

void WriteChunkData(CMapFile* file, SaveChunk type, std::uint32_t subIndex, std::uint32_t bytes, const void* data) {
    reinterpret_cast<WriteChunkDataFn>(kWriteChunkData)(file, nullptr, static_cast<std::uint32_t>(type), subIndex,
                                                        bytes, data);
}

void FreeChunk(void* chunk) {
    msvc6::GameOperatorDelete(chunk);
}

// strcpy, as the original's inlined repne scasb / rep movs: no bound.
void CopyText(char* target, const char* text) {
    const std::size_t bytes = std::strlen(text) + 1;
    std::memcpy(target, text, bytes);
}

// _mkdir(g_gameFolder + folder), result ignored (0x5099b0..0x509a9a and
// 0x509a9a..0x509b79); the temporary string is destroyed right after.
void MakeFolder(const GameText& folder) {
    msvc6::String path;
    msvc6::String_ConcatChars(&path, &g_gameFolder.get(), folder.c_str());
    reinterpret_cast<MakeDirectoryFn>(kMakeDirectory)(msvc6::detail::CStr(&path));
    msvc6::String_Destroy(&path, nullptr);
}

// 0x509b79..0x509e26: opens g_gameFolder + "Save\" + name for writing.
void OpenSaveFile(CMapFile* file, const msvc6::String* name) {
    msvc6::String folder;
    msvc6::String_ConcatChars(&folder, &g_gameFolder.get(), texts::kSaveFolderPrefix.c_str());
    msvc6::String path;
    msvc6::String_ConcatString(&path, &folder, name);
    reinterpret_cast<OpenFn>(kOpen)(file, nullptr, &path, fileformats::kOpenWrite);
    msvc6::String_Destroy(&path, nullptr);
    msvc6::String_Destroy(&folder, nullptr);
}

// 0x509e26..0x50a03c: chunk 0 ("desc").
void WriteHeader(CGameData* world, CMapFile* file) {
    auto* header = NewChunk<fileformats::SaveHeaderChunk>(sizeof(fileformats::SaveHeaderChunk), texts::kSaveLabelDesc,
                                                          kLineHeader);
    SetHeader(header->header, SaveChunk::Header, 0, sizeof(fileformats::SaveHeaderChunk));
    header->version = g_versionNumber;
    header->build_info[0] = g_buildInfo[0];
    header->build_info[1] = g_buildInfo[1];
    header->mission_minus_one = g_campaignMission - 1;
    header->local_race = world->players[world->local_player].race;
    header->save_time = reinterpret_cast<TimeFn>(kTime)(nullptr);
    msvc6::String mapName;
    reinterpret_cast<MapNameFn>(kMapName)(&g_gameSettings.get(), nullptr, &mapName);
    CopyText(header->map_name, msvc6::detail::CStr(&mapName));
    msvc6::String_Destroy(&mapName, nullptr);
    WriteChunk(file, SaveChunk::Header, header);
    FreeChunk(header);
}

// 0x50a060..0x50a269: chunk 1 ("general").
void WriteGeneral(CGameData* world, CMapFile* file) {
    auto* general = NewChunk<fileformats::SaveGeneralChunk>(sizeof(fileformats::SaveGeneralChunk),
                                                            texts::kSaveLabelGeneral, kLineGeneral);
    SetHeader(general->header, SaveChunk::General, 0, sizeof(fileformats::SaveGeneralChunk));
    general->camera_drawn_x = world->camera_drawn_x;
    general->camera_drawn_y = world->camera_drawn_y;
    general->camera_x = world->camera_x;
    general->camera_y = world->camera_y;
    general->map_size = world->map_size;
    general->random_state[0] = world->random_state[0];
    general->random_state[1] = world->random_state[1];
    general->random_state[2] = world->random_state[2];
    general->counter_110a57c = world->counter_110a57c;
    general->fog_cycle = world->fog_cycle;
    general->minimap_cycle = world->minimap_cycle;
    general->last_drawn_tick = g_lastDrawnTick;
    general->game_tick = g_gameTick;
    // 0x50a163..0x50a1f0: the player info table.
    for (std::uint32_t i = 0; i < layout::kPlayers; ++i) {
        const PlayerInfo& info = g_playerInfo[i];
        fileformats::SavedPlayerInfo& saved = general->players[i];
        saved.unk00 = info.unk00;
        saved.unk04 = info.unk04;
        CopyText(saved.name, msvc6::detail::CStr(&info.name));
        std::memcpy(saved.fields, info.fields, sizeof(saved.fields));
    }
    general->active_players = g_strengthDivisor;
    general->game_outcome = g_gameOutcome;
    general->mission_minus_one = g_campaignMission - 1;
    general->unk2d304 = world->tutorial_wait;
    general->unk2d308 = world->tutorial_step;
    general->presence_max_player = world->presence_max_player;
    WriteChunk(file, SaveChunk::General, general);
    FreeChunk(general);
}

// 0x50a269..0x50a32f: chunks 2/0 (pending events) and 2/1 (events), one
// header buffer for both.
void WriteEvents(CGameData* world, CMapFile* file) {
    auto* events = NewChunk<EventsHeader>(sizeof(EventsHeader), texts::kSaveLabelEvents, kLineEvents);
    SetHeader(events->header, SaveChunk::Events, 0, sizeof(EventsHeader) + sizeof(world->pending_events));
    events->read = world->pending_event_index.read;
    events->write = world->pending_event_index.write;
    WriteChunkWithTail(file, SaveChunk::Events, 0, events, sizeof(world->pending_events), world->pending_events);
    SetHeader(events->header, SaveChunk::Events, 1, sizeof(EventsHeader) + sizeof(world->events));
    events->read = world->event_index.read;
    events->write = world->event_index.write;
    WriteChunkWithTail(file, SaveChunk::Events, 1, events, sizeof(world->events), world->events);
    FreeChunk(events);
}

// 0x50a32f..0x50a376: one chunk per tile row (the map size is re-read
// each row, as the original does).
void WriteTiles(CGameData* world, CMapFile* file) {
    for (std::int32_t row = 0; row < static_cast<std::int32_t>(world->map_size); ++row) {
        WriteChunkData(file, SaveChunk::TileRow, static_cast<std::uint32_t>(row),
                       world->map_size * sizeof(layout::Tile), world->tiles[row]);
    }
}

// 0x50a376..0x50a480: chunk 4 ("sectors"); the size counts area_high_water
// + 1 areas, which go out as the tail.
void WriteSectors(CGameData* world, CMapFile* file) {
    auto* sectors = NewChunk<fileformats::SaveSectorsChunk>(kSectorsHeaderBytes, texts::kSaveLabelSectors,
                                                            kLineSectors);
    const std::uint32_t areaBytes = (world->area_high_water + 1) * sizeof(layout::Area);
    SetHeader(sectors->header, SaveChunk::Sectors, 0, kSectorsHeaderBytes + areaBytes);
    sectors->walk_region_high_water = world->walk_region_high_water;
    sectors->area_high_water = world->area_high_water;
    sectors->ids4096_high_water = world->ids4096_high_water;
    sectors->wide_region_high_water = world->wide_region_high_water;
    std::memcpy(sectors->walk_region_used, world->walk_region_used, sizeof(sectors->walk_region_used));
    std::memcpy(sectors->ids4096_used, world->ids4096_used, sizeof(sectors->ids4096_used));
    std::memcpy(sectors->wide_region_used, world->wide_region_used, sizeof(sectors->wide_region_used));
    // The size is computed again from the world (the same value).
    WriteChunkWithTail(file, SaveChunk::Sectors, 0, sectors, (world->area_high_water + 1) * sizeof(layout::Area),
                       world->areas);
    FreeChunk(sectors);
}

// 0x50a480..0x50a508: chunk 6 ("player").
void WritePlayers(CGameData* world, CMapFile* file) {
    auto* players = NewChunk<fileformats::SavePlayerChunk>(sizeof(fileformats::SavePlayerChunk),
                                                           texts::kSaveLabelPlayer, kLinePlayer);
    SetHeader(players->header, SaveChunk::Player, 0, sizeof(fileformats::SavePlayerChunk));
    players->local_player = world->local_player;
    std::memcpy(players->player_races, world->player_races, sizeof(players->player_races));
    static_assert(sizeof(players->players) == sizeof(world->players));
    std::memcpy(players->players, world->players, sizeof(players->players));
    WriteChunk(file, SaveChunk::Player, players);
    FreeChunk(players);
}

// Chunks 7, 8, 9, 13, 14: the high-water mark in a 12-byte header buffer,
// the records 0..high water as the tail. The original reads the mark from
// the world for the size, the header field and the tail size.
template <typename Record>
void WriteTable(CMapFile* file, SaveChunk type, const std::uint32_t& highWater, const Record* records,
                const GameText& label, std::int32_t line) {
    auto* table = NewChunk<TableHeader>(sizeof(TableHeader), label, line);
    SetHeader(table->header, type, 0, highWater * sizeof(Record) + sizeof(TableHeader) + sizeof(Record));
    table->high_water = highWater;
    WriteChunkWithTail(file, type, 0, table, (highWater + 1) * sizeof(Record), records);
    FreeChunk(table);
}

// 0x50a6a5..0x50a73c: chunk 10 ("diggertab").
void WriteDiggerTab(CGameData* world, CMapFile* file) {
    auto* digger = NewChunk<fileformats::SaveDiggerTabChunk>(sizeof(fileformats::SaveDiggerTabChunk),
                                                             texts::kSaveLabelDiggerTab, kLineDiggerTab);
    SetHeader(digger->header, SaveChunk::DiggerTab, 0, sizeof(fileformats::SaveDiggerTabChunk));
    static_assert(sizeof(digger->footprint_scratch) == sizeof(world->footprint_scratch));
    static_assert(sizeof(digger->site_cache) == sizeof(world->site_cache));
    std::memcpy(digger->footprint_scratch[0], world->footprint_scratch[0], sizeof(digger->footprint_scratch[0]));
    std::memcpy(digger->footprint_scratch[1], world->footprint_scratch[1], sizeof(digger->footprint_scratch[1]));
    std::memcpy(digger->site_cache, world->site_cache, sizeof(digger->site_cache));
    WriteChunk(file, SaveChunk::DiggerTab, digger);
    FreeChunk(digger);
}

// 0x50a75d..0x50a7d9: chunk 12 ("activesettlers").
void WriteActiveSettlers(CGameData* world, CMapFile* file) {
    auto* lists = NewChunk<fileformats::SaveActiveSettlersChunk>(sizeof(fileformats::SaveActiveSettlersChunk),
                                                                 texts::kSaveLabelActiveSettlers, kLineActiveSettlers);
    SetHeader(lists->header, SaveChunk::ActiveSettlers, 0, sizeof(fileformats::SaveActiveSettlersChunk));
    static_assert(sizeof(lists->player_lists) == sizeof(world->player_lists));
    static_assert(sizeof(lists->player_list_lengths) == sizeof(world->player_list_lengths));
    std::memcpy(lists->player_lists, world->player_lists, sizeof(lists->player_lists));
    std::memcpy(lists->player_list_lengths, world->player_list_lengths, sizeof(lists->player_list_lengths));
    WriteChunk(file, SaveChunk::ActiveSettlers, lists);
    FreeChunk(lists);
}

// 0x50a8e3..0x50a95f: chunk 15 ("messages").
void WriteMessages(CGameData* world, CMapFile* file) {
    auto* messages = NewChunk<fileformats::SaveMessagesChunk>(sizeof(fileformats::SaveMessagesChunk),
                                                              texts::kSaveLabelMessages, kLineMessages);
    SetHeader(messages->header, SaveChunk::Messages, 0, sizeof(fileformats::SaveMessagesChunk));
    static_assert(sizeof(messages->notifications) == sizeof(world->notifications));
    static_assert(sizeof(messages->notification_timers) == sizeof(world->notification_timers));
    std::memcpy(messages->notifications, world->notifications, sizeof(messages->notifications));
    std::memcpy(messages->notification_timers, world->notification_timers, sizeof(messages->notification_timers));
    WriteChunk(file, SaveChunk::Messages, messages);
    FreeChunk(messages);
}

// 0x50a95f..0x50a9db: chunk 16 ("warmap").
void WriteWarMap(CGameData* world, CMapFile* file) {
    auto* warMap = NewChunk<fileformats::SaveWarMapChunk>(sizeof(fileformats::SaveWarMapChunk),
                                                          texts::kSaveLabelWarMap, kLineWarMap);
    SetHeader(warMap->header, SaveChunk::WarMap, 0, sizeof(fileformats::SaveWarMapChunk));
    static_assert(sizeof(warMap->presence_a) == sizeof(world->presence_a));
    static_assert(sizeof(warMap->presence_b) == sizeof(world->presence_b));
    std::memcpy(warMap->presence_a, world->presence_a, sizeof(warMap->presence_a));
    std::memcpy(warMap->presence_b, world->presence_b, sizeof(warMap->presence_b));
    WriteChunk(file, SaveChunk::WarMap, warMap);
    FreeChunk(warMap);
}

// A chunk of `bufferBytes` whose data are `bytes` copied from `source`,
// with `size` in its header (0x18, 0x19, 0x1c).
void WriteCopy(CMapFile* file, SaveChunk type, std::uint32_t size, const void* source, std::uint32_t bytes,
               const GameText& label, std::int32_t line) {
    auto* chunk = NewChunk<fileformats::SaveDataChunk>(sizeof(ChunkHeader) + bytes, label, line);
    SetHeader(chunk->header, type, 0, size);
    std::memcpy(chunk->data, source, bytes);
    WriteChunk(file, type, chunk);
    FreeChunk(chunk);
}

// 0x50a9ff..0x50abd3: the chunks the loader reads by version.
void WriteVersionedChunks(CGameData* world, CMapFile* file) {
    WriteCopy(file, SaveChunk::VictoryConditions, sizeof(ChunkHeader) + sizeof(world->victory_conditions),
              &world->victory_conditions, sizeof(world->victory_conditions), texts::kSaveLabelVictoryConditions,
              kLineVictoryConditions);
    // The colours: 20 indexes (40 bytes) copied, but the header's size is
    // 40 bytes *with* the header, so the file holds only the first 16
    // (LoadSaveGame then reads 8 bytes past the chunk).
    constexpr std::uint32_t kColourBytes = kColourEntries * sizeof(std::uint16_t);
    WriteCopy(file, SaveChunk::PlayerColours, kColourBytes, &g_playerColourIndex[0], kColourBytes,
              texts::kSaveLabelPlayerColors, kLinePlayerColours);
    static_assert(sizeof(world->statistics) == 0x2d000);
    WriteCopy(file, SaveChunk::ProdStat, sizeof(ChunkHeader) + sizeof(world->statistics), world->statistics,
              sizeof(world->statistics), texts::kSaveLabelProdStat, kLineProdStat);

    // objectives_enabled and the 7 objective ids, two dwords in the original.
    static_assert(offsetof(CGameData, objectives) == offsetof(CGameData, objectives_enabled) + 1);
    auto* objective = NewChunk<fileformats::SaveObjectiveChunk>(sizeof(fileformats::SaveObjectiveChunk),
                                                                texts::kSaveLabelObjective, kLineObjective);
    SetHeader(objective->header, SaveChunk::NewGameObjective, 0, sizeof(fileformats::SaveObjectiveChunk));
    std::memcpy(objective->objectives, &world->objectives_enabled, sizeof(objective->objectives));
    WriteChunk(file, SaveChunk::NewGameObjective, objective);
    FreeChunk(objective);

    auto* timeout = NewChunk<fileformats::SaveTimeoutChunk>(sizeof(fileformats::SaveTimeoutChunk),
                                                            texts::kSaveLabelObjectiveTimeout, kLineObjectiveTimeout);
    SetHeader(timeout->header, SaveChunk::NewGameObjectiveTimeout, 0, sizeof(fileformats::SaveTimeoutChunk));
    timeout->objective_timeout = world->objective_timeout;
    WriteChunk(file, SaveChunk::NewGameObjectiveTimeout, timeout);
    FreeChunk(timeout);
}

}  // namespace

// Purpose: saves the running game: writes the world, the game setup and the
// computer players into a savegame file under Save\ (used by the save menu,
// probably also by the automatic saves).
bool __fastcall CLogicThread_SaveGame(CLogicThread* self, void* /*edx*/, const msvc6::String* name) {
    // The CMapFile lives on the original's stack (constructed with rethrow 0).
    alignas(4) std::uint8_t fileBytes[sizeof(CMapFile)];
    auto* file = reinterpret_cast<CMapFile*>(fileBytes);
    reinterpret_cast<ConstructFn>(kConstruct)(file, nullptr, 0);

    MakeFolder(texts::kSaveFolder);
    MakeFolder(texts::kSaveMultiFolder);
    OpenSaveFile(file, name);

    WriteHeader(self->world, file);
    // 0x50a03c..0x50a060: chunks 0x14, 0x1a, 0x1b, 0x1e (the original pushes
    // the seed as a word; SaveSetup reads only that word).
    reinterpret_cast<SaveSetupFn>(kSaveSetup)(&g_gameSettings.get(), nullptr, file, self->tick,
                                              self->world->random_state[0]);
    CGameData* world = self->world;
    WriteGeneral(world, file);
    WriteEvents(world, file);
    WriteTiles(world, file);
    WriteSectors(world, file);
    WritePlayers(world, file);
    WriteTable(file, SaveChunk::Settlers, world->settler_high_water, world->settlers, texts::kSaveLabelSettler,
               kLineSettlers);
    WriteTable(file, SaveChunk::Buildings, world->building_high_water, world->buildings, texts::kSaveLabelBuildings,
               kLineBuildings);
    WriteTable(file, SaveChunk::Piles, world->pile_high_water, world->piles, texts::kSaveLabelPiles, kLinePiles);
    WriteDiggerTab(world, file);
    // 0x50a73c..0x50a75d: chunk 11, the minimap.
    WriteChunkData(file, SaveChunk::Minimap, 0, sizeof(world->minimap), world->minimap);
    WriteActiveSettlers(world, file);
    WriteTable(file, SaveChunk::Trade, world->trade_high_water, world->trades, texts::kSaveLabelTrade, kLineTrade);
    WriteTable(file, SaveChunk::Timer, world->effects_high_water, world->effects, texts::kSaveLabelTimer, kLineTimer);
    WriteMessages(world, file);
    WriteWarMap(world, file);
    // 0x50a9de..0x50a9ff: the computer players' state.
    for (const std::uint32_t computerPlayer : self->computer_players) {
        if (computerPlayer != 0) {
            reinterpret_cast<AiSaveFn>(kAiSave)(computerPlayer, nullptr, file);
        }
    }
    WriteVersionedChunks(self->world, file);

    // 0x50abd6..0x50ac15: the result: no file error before or after Close.
    bool ok = true;
    if (reinterpret_cast<GetErrorFn>(kGetError)(file, nullptr) < 0) {
        ok = false;
    }
    reinterpret_cast<MapFileVoidFn>(kClose)(file, nullptr);
    if (reinterpret_cast<GetErrorFn>(kGetError)(file, nullptr) < 0) {
        ok = false;
    }
    reinterpret_cast<MapFileVoidFn>(kDestruct)(file, nullptr);
    return ok;
}

using SaveGameFn = bool(__fastcall*)(CLogicThread* self, void* edx, const msvc6::String* name);
S3RE_HOOK(0x00509980, CLogicThread_SaveGame, SaveGameFn, Original_CLogicThread_SaveGame,
          CLogicThread_SaveGame);

}  // namespace s3re::game
