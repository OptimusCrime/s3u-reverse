// Tests of CGameData::LoadMap (load_map.cpp): the original against ours on
// two identical worlds built like a new game's (zeroed memory and the
// original CGameData constructor, map_test::ConstructWorld), with
//  - every map under Settlers3Ultimate\Map (read only: opened by the
//    original CMapFile::Open in read mode), with 2..4 players taking the
//    map's slots in different orders (so that buildings, settlers and land
//    of untaken slots are dropped), races 0..3, start goods 0..3;
//  - variants of real maps in memory-only files (CMapFile::AddChunk):
//    towers with listed garrisons, catapults and ships, pile records
//    without a kind (they retype the tile's pile), missing chunks, chunk
//    0x40, victory conditions with and without protect_players, and every
//    campaign mission LoadMap fixes up (49, 50, 53, 59, 61, 70; towers and
//    trees placed where the fix-ups look).
// Compared: the whole world, every writable byte of S3.EXE (except the CRT
// heap's state: the chunk buffers are allocated and freed), the DebugLog
// calls, the CRT rand() state the chunk cache consumes and the CMapFile
// objects (cached chunks, reference counts).

#include "game/debug_log.h"
#include "game/enums/buildings.h"
#include "game/enums/objects.h"
#include "game/enums/settler_types.h"
#include "game/fileformats/map_file.h"
#include "game/fileformats/map_file_test_support.h"
#include "game/globals.h"
#include "game/map/map_test_support.h"
#include "game/ui/game_settings_layout.h"
#include "game/world.h"
#include "log.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <cstring>
#include <string>
#include <vector>

namespace s3re::game {

using fileformats::CMapFile;

void __fastcall CGameData_LoadMap(CGameData* world, void* edx, CMapFile* file, const std::uint32_t* slotToPlayer);
using LoadMapFn = void(__fastcall*)(CGameData* world, void* edx, CMapFile* file, const std::uint32_t* slotToPlayer);
extern LoadMapFn Original_CGameData_LoadMap;

using AddChunkFn = std::uint32_t(__fastcall*)(CMapFile* self, void* edx, std::uint32_t type, std::uint32_t subIndex,
                                              const std::uint8_t* data);
using MapFileVoidFn = void(__fastcall*)(CMapFile* self, void* edx);
extern AddChunkFn Original_CMapFile_AddChunk;
extern MapFileVoidFn Original_CMapFile_SetMemoryOnly;

namespace {

using map_test::Globals;
using namespace mapfile_test;

constexpr std::uint32_t kPlayers = 20;
constexpr std::uint32_t kNoPlayer = 0xff;

// GameSettings::PlayerSetup (0x4351e0) grows an empty player-setup vector
// to one record on its first call (heap and settings change). LoadMap calls
// it for the colours; one call before the tests makes the later ones
// side-effect free, as they are in a game (the menus fill the vector).
void PrepareSettings() {
    using PlayerSetupFn = void*(__fastcall*)(GameSettings* self, void* edx, std::uint32_t index);
    reinterpret_cast<PlayerSetupFn>(0x004351e0)(&g_gameSettings.get(), nullptr, 0);
}

// The slot table CLogicThread::OnStart passes: 20 entries in the game; the
// rest of the 256 a slot byte can reach stays kNoPlayer here.
struct SlotTable {
    std::uint32_t slots[256];
    SlotTable() {
        for (std::uint32_t& slot : slots) {
            slot = kNoPlayer;
        }
    }
};

// The game's players, set like CLogicThread::OnStart does before LoadMap
// (Player +0 1 human / 2 computer, race, team, start position, the RNG
// seeded) and the slot table: player p takes map slot order[p].
struct Game {
    std::uint32_t players = 2;
    std::uint32_t order[4] = {0, 1, 2, 3};
    std::int32_t races[kPlayers] = {};
    std::uint16_t seed[3] = {};
    std::uint32_t startGoods = 2;
    std::int32_t campaignMission = 0;
    // The start positions of the map's slots (chunk 2, read by the menus
    // into the player setup; OnStart copies them to Player +0x10/+0x14);
    // -1 = none (AddPlayerTowns then tries random positions).
    std::int32_t slotStart[4][2] = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}};

    SlotTable Slots() const {
        SlotTable table;
        for (std::uint32_t p = 0; p < players; ++p) {
            table.slots[order[p]] = p;
        }
        return table;
    }

    void SetUp(CGameData* world) const {
        map_test::ConstructWorld(world);
        for (std::uint32_t p = 0; p < kPlayers; ++p) {
            layout::Player& player = world->players[p];
            player.active = p < players ? 1 + static_cast<std::int32_t>(p % 2) : 0;
            player.race = races[p];
            player.team = 0;
            player.start_x = p < players ? slotStart[order[p]][0] : -1;
            player.start_y = p < players ? slotStart[order[p]][1] : -1;
        }
        std::memcpy(world->random_state, seed, sizeof(seed));
    }
};

Game RandomGame(test::Rng& rng) {
    Game game;
    game.players = 2 + rng.Below(3);
    for (std::uint32_t i = 3; i > 0; --i) {  // a random slot order
        const std::uint32_t j = rng.Below(i + 1);
        const std::uint32_t t = game.order[i];
        game.order[i] = game.order[j];
        game.order[j] = t;
    }
    for (std::int32_t& race : game.races) {
        race = static_cast<std::int32_t>(rng.Below(4));
    }
    for (std::uint16_t& word : game.seed) {
        word = static_cast<std::uint16_t>(rng.Next32());
    }
    game.startGoods = rng.Below(4);
    return game;
}

// Chunk 2 of a map: per slot 45 bytes, +4 the start x, +8 the start y.
constexpr std::uint32_t kSlotRecordBytes = 45;

void TakeStartPositions(const std::vector<std::uint8_t>& raw, Game& game) {
    for (const ParsedChunk& chunk : ParseContainer(raw)) {
        if (chunk.key != 2) {
            continue;
        }
        for (std::uint32_t slot = 0; slot < 4; ++slot) {
            const std::size_t at = 8 + slot * kSlotRecordBytes;
            if (at + 12 <= chunk.bytes.size()) {
                std::memcpy(&game.slotStart[slot][0], chunk.bytes.data() + at + 4, 4);
                std::memcpy(&game.slotStart[slot][1], chunk.bytes.data() + at + 8, 4);
            }
        }
    }
}

// Loads `a` with the original into the first world and `b` with
// `candidate` into the second (both set up for `game`), from the same
// globals and random state, and compares everything. Leaves the globals as
// the original left them (except what the next case sets).
bool CompareLoad(test::Context& ctx, test::WorldPair& worlds, CMapFile* a, CMapFile* b, const Game& game,
                 LoadMapFn candidate, const char* what) {
    worlds.Both([&](CGameData* w) { game.SetUp(w); });
    GameSettings& settings = g_gameSettings;
    settings.start_goods = game.startGoods;
    g_campaignMission = game.campaignMission;
    const SlotTable slots = game.Slots();

    const RandomState random0 = RandomState::Save();
    DebugLogRecorder log;
    Globals globals;
    Original_CGameData_LoadMap(worlds.original(), nullptr, a, slots.slots);
    const std::vector<DebugLogCall> logA = log.Take();
    const RandomState randomA = RandomState::Save();
    const std::uintptr_t heap = g_crtSmallBlockHeap.address();
    const std::size_t heapSize = sizeof(g_crtSmallBlockHeap.get());
    const std::vector<std::uint8_t> globalsA = globals.TakeOriginalExcept(heap, heapSize);

    random0.Restore();
    candidate(worlds.ours(), nullptr, b, slots.slots);
    const std::vector<DebugLogCall> logB = log.Take();
    const RandomState randomB = RandomState::Save();
    const std::vector<std::uint8_t> globalsB = Globals::Now();

    const int failures = ctx.failures();
    worlds.Checkpoint(ctx);
    const std::ptrdiff_t heapOffset = Globals::OffsetOf(heap);
    const std::ptrdiff_t difference =
        Globals::FirstDifference(globalsA, globalsB, heapOffset, heapOffset + static_cast<std::ptrdiff_t>(heapSize));
    if (difference >= 0) {
        ctx.Fail(__FILE__, __LINE__, "  globals differ at 0x%08x (%02x, %02x)",
                 static_cast<unsigned>(Globals::AddressOf(difference)), globalsA[difference], globalsB[difference]);
    }
    S3RE_EXPECT(randomA == randomB);
    if (S3RE_EXPECT_EQ(logA.size(), logB.size())) {
        for (std::size_t k = 0; k < logA.size(); ++k) {
            if (!S3RE_EXPECT(logA[k] == logB[k])) {
                ctx.Fail(__FILE__, __LINE__, "  DebugLog call %u: format 0x%08x (first %u) and 0x%08x (%u)",
                         static_cast<unsigned>(k), logA[k].format, logA[k].args[0], logB[k].format, logB[k].args[0]);
                break;
            }
        }
    }
    CompareFiles(ctx, a, b);
    if (ctx.failures() != failures) {
        ctx.Fail(__FILE__, __LINE__, "  in %s (%u players, slots %u %u %u %u, mission %d)", what, game.players,
                 game.order[0], game.order[1], game.order[2], game.order[3], game.campaignMission);
        return false;
    }
    return true;
}

// The maps of the installation (file type 10), as raw bytes.
struct MapFile {
    std::string path;
    std::vector<std::uint8_t> raw;
};

std::vector<MapFile> InstalledMaps() {
    std::vector<std::string> paths;
    ListFiles(GameFolder() + "\\Map", paths);
    std::vector<MapFile> maps;
    for (const std::string& path : paths) {
        MapFile map{path, ReadWholeFile(path)};
        if (StoredWord(map.raw, 4) == fileformats::kFileTypeMap) {
            maps.push_back(std::move(map));
        }
    }
    return maps;
}

// ---------------------------------------------------------------------------
// Variants in memory
// ---------------------------------------------------------------------------

struct Chunk {
    std::uint32_t key;
    std::vector<std::uint8_t> bytes;  // header included
};

std::vector<Chunk> ChunksOf(const std::vector<std::uint8_t>& raw) {
    std::vector<Chunk> chunks;
    for (ParsedChunk& parsed : ParseContainer(raw)) {
        if (parsed.bytes.size() >= 8 && parsed.key != 0) {
            chunks.push_back({parsed.key, std::move(parsed.bytes)});
        }
    }
    return chunks;
}

Chunk* Find(std::vector<Chunk>& chunks, std::uint32_t type) {
    for (Chunk& chunk : chunks) {
        if (chunk.key == type) {
            return &chunk;
        }
    }
    return nullptr;
}

void Remove(std::vector<Chunk>& chunks, std::uint32_t type) {
    for (auto it = chunks.begin(); it != chunks.end(); ++it) {
        if (it->key == type) {
            chunks.erase(it);
            return;
        }
    }
}

void SetSize(Chunk& chunk) {
    const auto size = static_cast<std::uint32_t>(chunk.bytes.size());
    std::memcpy(chunk.bytes.data() + 4, &size, 4);
}

std::uint32_t Count(const Chunk& chunk) {
    std::uint32_t count = 0;
    std::memcpy(&count, chunk.bytes.data() + 8, 4);
    return count;
}

void AddRecord(Chunk& chunk, const std::vector<std::uint8_t>& record) {
    chunk.bytes.insert(chunk.bytes.end(), record.begin(), record.end());
    const std::uint32_t count = Count(chunk) + 1;
    std::memcpy(chunk.bytes.data() + 8, &count, 4);
    SetSize(chunk);
}

Chunk& Ensure(std::vector<Chunk>& chunks, std::uint32_t type) {
    if (Chunk* chunk = Find(chunks, type)) {
        return *chunk;
    }
    chunks.push_back({type, std::vector<std::uint8_t>(12, 0)});
    std::memcpy(chunks.back().bytes.data(), &type, 4);
    SetSize(chunks.back());
    return chunks.back();
}

std::vector<std::uint8_t> Record12(std::uint32_t slot, std::uint32_t type, std::uint32_t x, std::uint32_t y,
                                   std::uint8_t mode, const std::uint8_t counts[5]) {
    return {static_cast<std::uint8_t>(slot), static_cast<std::uint8_t>(type), static_cast<std::uint8_t>(x),
            static_cast<std::uint8_t>(x >> 8), static_cast<std::uint8_t>(y), static_cast<std::uint8_t>(y >> 8),
            mode, counts[0], counts[1], counts[2], counts[3], counts[4]};
}

std::uint32_t MapSizeOf(std::vector<Chunk>& chunks) {
    const Chunk* landscape = Find(chunks, 6);
    std::uint32_t size = 0;
    if (landscape != nullptr) {
        std::memcpy(&size, landscape->bytes.data() + 8, 4);
    }
    return size;
}

// The landscape record of tile (x, y).
std::uint8_t* LandscapeRecord(std::vector<Chunk>& chunks, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t size = MapSizeOf(chunks);
    return Find(chunks, 6)->bytes.data() + 12 + (y * size + x) * 6;
}

// Towers of every kind with listed garrisons (counts 0..15 per kind) or
// mode 2 (no garrison) at the map's own building positions, some of
// another slot; catapults and ships among the settlers; pile records
// without a kind on the tiles of the map's piles (land: their areas are
// economy areas).
void Mutate(test::Rng& rng, std::vector<Chunk>& chunks) {
    if (Chunk* buildings = Find(chunks, 8)) {
        const std::uint32_t count = Count(*buildings);
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint8_t* record = buildings->bytes.data() + 12 + i * 12;
            const std::uint8_t type = record[1];
            if (type == 15 || type == 16 || type == 18) {
                record[6] = rng.Chance(1, 4) ? 2 : static_cast<std::uint8_t>(rng.Below(2));
                for (int b = 7; b < 12; ++b) {
                    record[b] = static_cast<std::uint8_t>(rng.Chance(1, 2) ? rng.Below(4) * 17 : rng.Next32());
                }
            }
            if (rng.Chance(1, 10)) {
                record[0] = static_cast<std::uint8_t>(rng.Below(4));
            }
        }
    }
    if (Chunk* settlers = Find(chunks, 7)) {
        const std::uint32_t count = Count(*settlers);
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint8_t* record = settlers->bytes.data() + 12 + i * 6;
            if (rng.Chance(1, 40)) {
                const std::uint8_t types[] = {41, 42, 43, 44, 45};
                record[1] = types[rng.Below(5)];
            }
        }
    }
    if (Chunk* piles = Find(chunks, 9)) {
        const std::uint32_t count = Count(*piles);
        std::vector<std::vector<std::uint8_t>> extra;
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint8_t* record = piles->bytes.data() + 12 + i * 8;
            if (rng.Chance(1, 8)) {
                std::vector<std::uint8_t> copy(record, record + 8);
                copy[4] = static_cast<std::uint8_t>(rng.Below(36));
                copy[5] = static_cast<std::uint8_t>(rng.Below(9));
                copy[6] = 0;
                extra.push_back(copy);
            }
        }
        for (const auto& record : extra) {
            AddRecord(*piles, record);
        }
    }
}

// Places what the mission fix-ups look for: towers at mission 70's
// positions (some of the other tower type), trees near mission 59's points.
void PlaceMissionTargets(test::Rng& rng, std::vector<Chunk>& chunks) {
    struct Point {
        std::uint32_t x, y, type;
    };
    constexpr Point kTowers[] = {{266, 533, 16}, {293, 512, 16}, {342, 515, 15}, {360, 528, 16},
                                 {404, 555, 16}, {331, 570, 15}, {429, 542, 16}};
    const std::uint8_t noSoldiers[5] = {};
    Chunk& buildings = Ensure(chunks, 8);
    for (const Point& tower : kTowers) {
        const std::uint32_t type = rng.Chance(1, 4) ? (tower.type == 16 ? 15 : 16) : tower.type;
        AddRecord(buildings, Record12(rng.Below(2), type, tower.x, tower.y, 0, noSoldiers));
    }
    constexpr Point kTrees[] = {{313, 64, 0}, {351, 113, 0}, {392, 152, 0}, {535, 277, 0}};
    for (const Point& point : kTrees) {
        for (std::uint32_t k = 0; k < 3; ++k) {
            const std::uint32_t x = point.x - 1 + rng.Below(3);
            const std::uint32_t y = point.y - 1 + rng.Below(3);
            LandscapeRecord(chunks, x, y)[2] = static_cast<std::uint8_t>(kObjectFirstTree + rng.Below(16));
        }
    }
}

CMapFile* MemoryFile(const std::vector<Chunk>& chunks) {
    CMapFile* file = NewMapFile();
    Original_CMapFile_SetMemoryOnly(file, nullptr);
    for (const Chunk& chunk : chunks) {
        Original_CMapFile_AddChunk(file, nullptr, chunk.key & 0xffff, chunk.key >> 16, chunk.bytes.data());
    }
    return file;
}

bool CompareChunks(test::Context& ctx, test::WorldPair& worlds, const std::vector<Chunk>& chunks, const Game& game,
                   LoadMapFn candidate, const char* what) {
    const RandomState random = RandomState::Save();
    CMapFile* a = MemoryFile(chunks);
    random.Restore();
    CMapFile* b = MemoryFile(chunks);
    const bool same = CompareLoad(ctx, worlds, a, b, game, candidate, what);
    DeleteMapFile(a);
    DeleteMapFile(b);
    return same;
}

// A map with all of chunks 6..9 and at least 640 tiles (the missions' fix-ups
// reach x 535, y 596).
const MapFile* BigMapWithEverything(const std::vector<MapFile>& maps) {
    for (const MapFile& map : maps) {
        std::vector<Chunk> chunks = ChunksOf(map.raw);
        if (Find(chunks, 7) && Find(chunks, 8) && Find(chunks, 9) && MapSizeOf(chunks) >= 640) {
            return &map;
        }
    }
    return nullptr;
}

constexpr std::int32_t kMissions[] = {0, 49, 50, 51, 53, 59, 61, 70, 71};

// Variant cases on real maps: mutated records, chunks taken out or added,
// every mission number.
void CompareVariants(test::Context& ctx, LoadMapFn candidate, std::uint32_t cases) {
    PrepareSettings();
    const std::vector<MapFile> maps = InstalledMaps();
    const MapFile* big = BigMapWithEverything(maps);
    if (!S3RE_EXPECT(big != nullptr)) {
        return;
    }
    test::WorldPair worlds(ctx, sizeof(CGameData));
    for (std::uint32_t i = 0; i < cases; ++i) {
        // Every other case on the big map (with a mission), else a random map.
        const MapFile& map = i % 2 == 0 ? *big : maps[ctx.rng().Below(static_cast<std::uint32_t>(maps.size()))];
        std::vector<Chunk> chunks = ChunksOf(map.raw);
        Game game = RandomGame(ctx.rng());
        TakeStartPositions(map.raw, game);
        Mutate(ctx.rng(), chunks);
        if (i % 2 == 0) {
            game.campaignMission = kMissions[(i / 2) % std::size(kMissions)];
            PlaceMissionTargets(ctx.rng(), chunks);
        }
        switch (ctx.rng().Below(8)) {
        case 0:
            Remove(chunks, 8);  // the start towns instead
            break;
        case 1:
            Remove(chunks, 7);
            Remove(chunks, 9);
            break;
        case 2:
            chunks.push_back({0x40, std::vector<std::uint8_t>(8, 0)});
            std::memcpy(chunks.back().bytes.data(), &chunks.back().key, 4);
            SetSize(chunks.back());
            break;
        case 3: {
            Chunk victory{10, std::vector<std::uint8_t>(8 + sizeof(layout::VictoryConditions))};
            ctx.rng().Fill(victory.bytes.data() + 8, sizeof(layout::VictoryConditions));
            victory.bytes[8 + offsetof(layout::VictoryConditions, protect_players)] =
                static_cast<std::uint8_t>(ctx.rng().Below(2));
            std::memcpy(victory.bytes.data(), &victory.key, 4);
            SetSize(victory);
            Remove(chunks, 10);
            chunks.push_back(std::move(victory));
            break;
        }
        default:
            break;
        }
        char what[300];
        std::snprintf(what, sizeof(what), "variant %u of %s", i, map.path.c_str());
        if (!CompareChunks(ctx, worlds, chunks, game, candidate, what)) {
            return;
        }
    }
}

// A map without a landscape: the world keeps its map size (0 after the
// constructor) and the regions are not built; buildings, settlers and piles
// still come.
void CompareWithoutLandscape(test::Context& ctx, LoadMapFn candidate) {
    PrepareSettings();
    const std::vector<MapFile> maps = InstalledMaps();
    test::WorldPair worlds(ctx, sizeof(CGameData));
    for (const MapFile& map : maps) {
        std::vector<Chunk> chunks = ChunksOf(map.raw);
        if (Find(chunks, 8) == nullptr || MapSizeOf(chunks) > 256) {
            continue;
        }
        Remove(chunks, 6);
        Game game = RandomGame(ctx.rng());
        CompareChunks(ctx, worlds, chunks, game, candidate, map.path.c_str());
        return;
    }
    S3RE_EXPECT(false);  // no small map with buildings found
}

// Every installed map, read only.
void CompareAllMaps(test::Context& ctx, LoadMapFn candidate, std::uint32_t step) {
    PrepareSettings();
    const std::vector<MapFile> maps = InstalledMaps();
    test::WorldPair worlds(ctx, sizeof(CGameData));
    std::uint32_t loaded = 0;
    for (std::size_t i = 0; i < maps.size(); i += step) {
        const MapFile& map = maps[i];
        CMapFile* a = OpenMapFile(map.path, fileformats::kOpenRead);
        CMapFile* b = OpenMapFile(map.path, fileformats::kOpenRead);
        Game game = RandomGame(ctx.rng());
        TakeStartPositions(map.raw, game);
        const bool same = CompareLoad(ctx, worlds, a, b, game, candidate, map.path.c_str());
        DeleteMapFile(a);
        DeleteMapFile(b);
        if (!same) {
            return;
        }
        ++loaded;
    }
    S3RE_LOG("CGameData_LoadMap_AllMaps: %u of %u maps under Map loaded and compared\n", loaded,
             static_cast<unsigned>(maps.size()));
    S3RE_EXPECT(loaded > 0);
}

// Wrong implementations.
// The search marks of untaken slots kept (one tile byte).
void __fastcall WrongLoadMapMarks(CGameData* world, void* edx, CMapFile* file, const std::uint32_t* slotToPlayer) {
    CGameData_LoadMap(world, edx, file, slotToPlayer);
    world->tiles[3][5].search_mark ^= 1;
}

// A colour index off by one.
void __fastcall WrongLoadMapColour(CGameData* world, void* edx, CMapFile* file, const std::uint32_t* slotToPlayer) {
    CGameData_LoadMap(world, edx, file, slotToPlayer);
    g_playerColourIndex[7] = static_cast<std::uint16_t>(g_playerColourIndex[7] + 1);
}

// The landscape chunk left cached (one GetChunk too many).
void __fastcall WrongLoadMapChunk(CGameData* world, void* edx, CMapFile* file, const std::uint32_t* slotToPlayer) {
    CGameData_LoadMap(world, edx, file, slotToPlayer);
    using GetChunkFn = std::uint8_t*(__fastcall*)(CMapFile*, void*, std::uint32_t, std::uint32_t);
    reinterpret_cast<GetChunkFn>(0x004a60f0)(file, nullptr, 6, 0);
}

}  // namespace

S3RE_TEST(CGameData_LoadMap_AllMaps) {
    CompareAllMaps(ctx, &CGameData_LoadMap, 1);
}
S3RE_TEST(CGameData_LoadMap_Variants_MatchOriginal) {
    CompareVariants(ctx, &CGameData_LoadMap, 2 * static_cast<std::uint32_t>(std::size(kMissions)) + 12);
}
S3RE_TEST(CGameData_LoadMap_WithoutLandscape_MatchesOriginal) {
    CompareWithoutLandscape(ctx, &CGameData_LoadMap);
}
S3RE_TEST(CGameData_LoadMap_ComparisonDetectsWrongImplementation) {
    const LoadMapFn wrongs[] = {&WrongLoadMapMarks, &WrongLoadMapColour, &WrongLoadMapChunk};
    for (LoadMapFn wrong : wrongs) {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareVariants(probe, wrong, 1);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
