// Tests of CLogicThread::LoadSaveGame (load_save_game.cpp): the original
// against ours on two identical 22 MB worlds, with
//  - every savegame under Settlers3Ultimate\Save (read only: opened by the
//    original CMapFile::Open in read mode, GENERIC_READ, FILE_SHARE_READ),
//  - synthetic savegames built in memory (a memory-only CMapFile filled
//    with AddChunk): all version gates, random table sizes, tiles that
//    refer to economy sectors.
// Compared: the whole world, every writable byte of S3.EXE (the player
// names by their text: their buffers are heap pointers), the DebugLog
// calls, the CMapFile objects (cached chunks and reference counts) and the
// random state the chunk cache consumes. The original must write no
// global other than the known ones.

#include "game/debug_log.h"
#include "game/fileformats/map_file.h"
#include "game/fileformats/map_file_test_support.h"
#include "game/fileformats/savegame_chunks.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/logic/logic_thread_layout.h"
#include "game/world.h"
#include "log.h"
#include "msvc6/string.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <windows.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace s3re::game {

using fileformats::CMapFile;
using fileformats::SaveChunk;

void __fastcall CLogicThread_LoadSaveGame(CLogicThread* self, void* edx, CMapFile* file);
using LoadSaveGameFn = void(__fastcall*)(CLogicThread* self, void* edx, CMapFile* file);
extern LoadSaveGameFn Original_CLogicThread_LoadSaveGame;

using AddChunkFn = std::uint32_t(__fastcall*)(CMapFile* self, void* edx, std::uint32_t type, std::uint32_t subIndex,
                                              const std::uint8_t* data);
using MapFileVoidFn = void(__fastcall*)(CMapFile* self, void* edx);
extern AddChunkFn Original_CMapFile_AddChunk;
extern MapFileVoidFn Original_CMapFile_SetMemoryOnly;

namespace {

using namespace mapfile_test;

constexpr std::size_t kWorldSize = sizeof(CGameData);
constexpr std::uint32_t kPlayers = 20;

// ---------------------------------------------------------------------------
// The globals LoadSaveGame writes
// ---------------------------------------------------------------------------

struct KnownGlobals {
    std::uint32_t lastDrawnTick;
    std::uint32_t gameTick;
    std::uint32_t activePlayers;
    std::int32_t gameOutcome;
    std::int32_t campaignMission;
    std::uint16_t colours[kPlayers];
    std::uint8_t protectActive;
    struct Info {
        std::int32_t unk00, unk04;
        std::string name;
        std::uint32_t length;
        std::int32_t fields[11];
        bool operator==(const Info& o) const {
            return unk00 == o.unk00 && unk04 == o.unk04 && name == o.name && length == o.length &&
                   std::memcmp(fields, o.fields, sizeof(fields)) == 0;
        }
    } info[kPlayers];

    static KnownGlobals Save() {
        KnownGlobals g{};
        g.lastDrawnTick = g_lastDrawnTick;
        g.gameTick = g_gameTick;
        g.activePlayers = g_strengthDivisor;
        g.gameOutcome = g_gameOutcome;
        g.campaignMission = g_campaignMission;
        std::memcpy(g.colours, &g_playerColourIndex[0], sizeof(g.colours));
        g.protectActive = g_victoryProtectActive;
        for (std::uint32_t i = 0; i < kPlayers; ++i) {
            const PlayerInfo& p = g_playerInfo[i];
            g.info[i].unk00 = p.unk00;
            g.info[i].unk04 = p.unk04;
            g.info[i].name = p.name.ptr != nullptr ? std::string(p.name.ptr, p.name.len) : std::string();
            g.info[i].length = p.name.len;
            std::memcpy(g.info[i].fields, p.fields, sizeof(p.fields));
        }
        return g;
    }
    void Restore() const {
        g_lastDrawnTick = lastDrawnTick;
        g_gameTick = gameTick;
        g_strengthDivisor = activePlayers;
        g_gameOutcome = gameOutcome;
        g_campaignMission = campaignMission;
        std::memcpy(&g_playerColourIndex[0], colours, sizeof(colours));
        g_victoryProtectActive = protectActive;
        for (std::uint32_t i = 0; i < kPlayers; ++i) {
            PlayerInfo& p = g_playerInfo[i];
            p.unk00 = info[i].unk00;
            p.unk04 = info[i].unk04;
            msvc6::String_AssignTextOrEmpty(&p.name, nullptr, info[i].name.c_str());
            std::memcpy(p.fields, info[i].fields, sizeof(p.fields));
        }
    }
    // The first `colourCount` colour indexes are compared (see CompareLoad).
    bool Same(const KnownGlobals& o, std::uint32_t colourCount) const {
        bool same = lastDrawnTick == o.lastDrawnTick && gameTick == o.gameTick && activePlayers == o.activePlayers &&
                    gameOutcome == o.gameOutcome && campaignMission == o.campaignMission &&
                    std::memcmp(colours, o.colours, colourCount * sizeof(colours[0])) == 0 &&
                    protectActive == o.protectActive;
        for (std::uint32_t i = 0; i < kPlayers; ++i) {
            same = same && info[i] == o.info[i];
        }
        return same;
    }
};

// The writable sections of S3.EXE, as (address, bytes).
struct Sections {
    std::vector<std::uintptr_t> starts;
    std::vector<std::vector<std::uint8_t>> bytes;

    static Sections Save() {
        Sections s;
        auto* base = reinterpret_cast<std::uint8_t*>(0x400000);
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
        const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            if ((section->Characteristics & IMAGE_SCN_MEM_WRITE) == 0) {
                continue;
            }
            const std::uint8_t* start = base + section->VirtualAddress;
            s.starts.push_back(reinterpret_cast<std::uintptr_t>(start));
            s.bytes.emplace_back(start, start + section->Misc.VirtualSize);
        }
        return s;
    }
};

struct Range {
    std::uintptr_t start;
    std::size_t size;
};

bool InRanges(std::uintptr_t address, const std::vector<Range>& ranges) {
    for (const Range& r : ranges) {
        if (address >= r.start && address < r.start + r.size) {
            return true;
        }
    }
    return false;
}

// The player names' string objects (their buffers are heap pointers;
// compared by text in KnownGlobals).
std::vector<Range> NameObjects() {
    std::vector<Range> ranges;
    for (std::uint32_t i = 0; i < kPlayers; ++i) {
        ranges.push_back({reinterpret_cast<std::uintptr_t>(&g_playerInfo[i].name), sizeof(msvc6::String)});
    }
    return ranges;
}

// Everything LoadSaveGame may write (directly or through the chunk cache).
std::vector<Range> KnownRanges() {
    return {
        // The CRT's small-block heap state (malloc 0x628xxx), changed by the
        // allocations of the chunk cache and of the player names.
        {g_crtSmallBlockHeap.address(), sizeof(g_crtSmallBlockHeap.get())},
        {g_lastDrawnTick.address(), 4},
        {g_gameTick.address(), 4},
        {g_strengthDivisor.address(), 4},
        {g_gameOutcome.address(), 4},
        {g_campaignMission.address(), 4},
        {g_playerColourIndex.address(), kPlayers * 2},
        {g_victoryProtectActive.address(), 1},
        {g_playerInfo.address(), sizeof(PlayerInfo) * kPlayers},
        {g_skipListRandomBits.address(), 4},
        {g_skipListRandomBitsLeft.address(), 4},
    };
}

// Compares two snapshots byte by byte outside `skip`; reports the first
// few differing addresses.
bool SameSections(test::Context& ctx, const Sections& a, const Sections& b, const std::vector<Range>& skip,
                  const char* what) {
    int reported = 0;
    for (std::size_t s = 0; s < a.bytes.size(); ++s) {
        for (std::size_t i = 0; i < a.bytes[s].size(); ++i) {
            if (a.bytes[s][i] == b.bytes[s][i] || InRanges(a.starts[s] + i, skip)) {
                continue;
            }
            ctx.Fail(__FILE__, __LINE__, "%s: S3.EXE byte 0x%08x differs (%02x, %02x)", what,
                     static_cast<unsigned>(a.starts[s] + i), a.bytes[s][i], b.bytes[s][i]);
            if (++reported >= 5) {
                return false;
            }
        }
    }
    return reported == 0;
}

// Ours started from `restored` (the globals put back after the original
// ran; putting the names back allocates, which changes the CRT heap's
// globals) and the original from `before`. A byte the original left alone
// must be left alone by ours; a byte it wrote must end up the same.
bool SameEffect(test::Context& ctx, const Sections& before, const Sections& afterA, const Sections& restored,
                const Sections& afterB, const std::vector<Range>& skip) {
    int reported = 0;
    for (std::size_t s = 0; s < before.bytes.size(); ++s) {
        for (std::size_t i = 0; i < before.bytes[s].size(); ++i) {
            const bool originalWrote = before.bytes[s][i] != afterA.bytes[s][i];
            const bool same = originalWrote ? afterA.bytes[s][i] == afterB.bytes[s][i]
                                            : restored.bytes[s][i] == afterB.bytes[s][i];
            if (same || InRanges(before.starts[s] + i, skip)) {
                continue;
            }
            ctx.Fail(__FILE__, __LINE__, "globals: S3.EXE byte 0x%08x: original %02x -> %02x, ours %02x -> %02x",
                     static_cast<unsigned>(before.starts[s] + i), before.bytes[s][i], afterA.bytes[s][i],
                     restored.bytes[s][i], afterB.bytes[s][i]);
            if (++reported >= 5) {
                return false;
            }
        }
    }
    return reported == 0;
}

// ---------------------------------------------------------------------------
// One comparison
// ---------------------------------------------------------------------------

// A logic thread object with only the world pointer set.
struct FakeLogicThread {
    alignas(4) std::uint8_t bytes[0x700] = {};
    CLogicThread* get(CGameData* world) {
        auto* thread = reinterpret_cast<CLogicThread*>(bytes);
        thread->world = world;
        return thread;
    }
};

// Loads `a` with the original into the first world and `b` with
// `candidate` into the second, from the same globals and random state,
// and compares everything. Leaves the globals as they were before.
//
// `colourCount`: the colour indexes that come from the file. The game's
// own savegames have a 40-byte chunk 0x19 (16 colours), but LoadSaveGame
// copies 20 (40 bytes of data): colours 16..19 come from the 8 bytes after
// the chunk buffer, heap memory whose contents depend on earlier
// allocations, which differ between the two runs (a known difference that
// no code can reproduce; the synthetic savegames have 20 and compare all).
bool CompareLoad(test::Context& ctx, test::WorldPair& worlds, CMapFile* a, CMapFile* b, LoadSaveGameFn candidate,
                 const char* what, std::uint32_t colourCount = kPlayers) {
    FakeLogicThread threadA;
    FakeLogicThread threadB;
    const KnownGlobals known0 = KnownGlobals::Save();
    const RandomState random0 = RandomState::Save();
    const Sections before = Sections::Save();

    DebugLogRecorder log;
    Original_CLogicThread_LoadSaveGame(threadA.get(worlds.original()), nullptr, a);
    const std::vector<DebugLogCall> logA = log.Take();
    const KnownGlobals knownA = KnownGlobals::Save();
    const RandomState randomA = RandomState::Save();
    const Sections afterA = Sections::Save();

    known0.Restore();
    random0.Restore();
    const Sections restored = Sections::Save();
    candidate(threadB.get(worlds.ours()), nullptr, b);
    const std::vector<DebugLogCall> logB = log.Take();
    const KnownGlobals knownB = KnownGlobals::Save();
    const RandomState randomB = RandomState::Save();
    const Sections afterB = Sections::Save();
    known0.Restore();
    random0.Restore();

    const int failures = ctx.failures();
    std::vector<Range> knownAndNames = KnownRanges();
    for (const Range& r : NameObjects()) {
        knownAndNames.push_back(r);
    }
    SameSections(ctx, before, afterA, knownAndNames, "the original wrote an unexpected global");
    std::vector<Range> namesAndHeap = NameObjects();
    namesAndHeap.push_back({g_crtSmallBlockHeap.address(), sizeof(g_crtSmallBlockHeap.get())});
    namesAndHeap.push_back({g_playerColourIndex.address() + colourCount * 2, (kPlayers - colourCount) * 2});
    SameEffect(ctx, before, afterA, restored, afterB, namesAndHeap);
    S3RE_EXPECT(knownA.Same(knownB, colourCount));
    S3RE_EXPECT(randomA == randomB);
    if (!S3RE_EXPECT_EQ(logA.size(), logB.size())) {
        for (std::size_t k = 0; k < logA.size() && k < 12; ++k) {
            ctx.Fail(__FILE__, __LINE__, "  original DebugLog %u: 0x%08x (%u args: %x %x %x)", static_cast<unsigned>(k),
                     logA[k].format, logA[k].count, logA[k].args[0], logA[k].args[1], logA[k].args[2]);
        }
        for (std::size_t k = 0; k < logB.size() && k < 12; ++k) {
            ctx.Fail(__FILE__, __LINE__, "  our DebugLog %u: 0x%08x (%u args: %x)", static_cast<unsigned>(k),
                     logB[k].format, logB[k].count, logB[k].args[0]);
        }
    }
    for (std::size_t k = 0; k < logA.size() && k < logB.size(); ++k) {
        if (!S3RE_EXPECT(logA[k] == logB[k])) {
            ctx.Fail(__FILE__, __LINE__, "  DebugLog call %u: format 0x%08x (%u args, first %u) and 0x%08x (%u, %u)",
                     static_cast<unsigned>(k), logA[k].format, logA[k].count, logA[k].args[0], logB[k].format,
                     logB[k].count, logB[k].args[0]);
            break;
        }
    }
    worlds.Checkpoint(ctx);
    CompareFiles(ctx, a, b);
    if (ctx.failures() != failures) {
        ctx.Fail(__FILE__, __LINE__, "  in %s", what);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Synthetic savegames
// ---------------------------------------------------------------------------

struct Chunk {
    std::uint32_t type;
    std::uint32_t sub;
    std::vector<std::uint8_t> bytes;  // header included
};

Chunk MakeChunk(test::Rng& rng, SaveChunk type, std::uint32_t sub, std::size_t size) {
    Chunk chunk{static_cast<std::uint32_t>(type), sub, std::vector<std::uint8_t>(size)};
    rng.Fill(chunk.bytes.data(), size);
    const fileformats::ChunkHeader header{fileformats::ChunkKey(chunk.type, sub), static_cast<std::uint32_t>(size)};
    std::memcpy(chunk.bytes.data(), &header, sizeof(header));
    return chunk;
}

template <typename T>
T& ChunkAs(Chunk& chunk) {
    return *reinterpret_cast<T*>(chunk.bytes.data());
}

// A table chunk with `count` records (high water count - 1).
Chunk MakeTable(test::Rng& rng, SaveChunk type, std::uint32_t count, std::size_t recordSize) {
    Chunk chunk = MakeChunk(rng, type, 0, offsetof(fileformats::SaveTableChunk, records) + count * recordSize);
    ChunkAs<fileformats::SaveTableChunk>(chunk).high_water = count - 1;
    return chunk;
}

constexpr std::int32_t kVersions[] = {0x50, 0x81, 0x82, 0x85, 0x86, 0x88, 0x89, 0x95, 0x96, 0x98, 0x99, 0xa0, 0xa1};
constexpr std::uint32_t kMapSizes[] = {0, 1, 7, 64, 128, 256, 384, 768};

std::vector<Chunk> SyntheticSave(test::Rng& rng, std::uint32_t caseIndex) {
    std::vector<Chunk> chunks;
    const std::int32_t version = kVersions[caseIndex % std::size(kVersions)];
    const std::uint32_t mapSize = kMapSizes[rng.Below(static_cast<std::uint32_t>(std::size(kMapSizes)))];

    Chunk header = MakeChunk(rng, SaveChunk::Header, 0, sizeof(fileformats::SaveHeaderChunk));
    ChunkAs<fileformats::SaveHeaderChunk>(header).version = version;
    chunks.push_back(std::move(header));

    Chunk general = MakeChunk(rng, SaveChunk::General, 0, sizeof(fileformats::SaveGeneralChunk));
    auto& g = ChunkAs<fileformats::SaveGeneralChunk>(general);
    g.map_size = mapSize;
    for (fileformats::SavedPlayerInfo& p : g.players) {
        p.name[rng.Below(sizeof(p.name))] = 0;  // names of every length up to 255
    }
    chunks.push_back(std::move(general));

    for (std::uint32_t sub = 0; sub < 2; ++sub) {
        chunks.push_back(MakeChunk(rng, SaveChunk::Events, sub, sizeof(fileformats::SaveEventsChunk)));
    }

    // Tile rows; the areas the tiles refer to: none, below 4096 or not.
    const std::uint32_t sectorCount = 1 + rng.Below(4096);
    for (std::uint32_t row = 0; row < mapSize; ++row) {
        Chunk tiles = MakeChunk(rng, SaveChunk::TileRow, row, 8 + mapSize * sizeof(layout::Tile));
        auto* records = reinterpret_cast<layout::Tile*>(tiles.bytes.data() + 8);
        for (std::uint32_t y = 0; y < mapSize; ++y) {
            const std::uint32_t pick = rng.Below(8);
            records[y].area = pick == 0 ? 0 : pick == 1 ? static_cast<std::uint16_t>(rng.Next32()) :
                                                          static_cast<std::uint16_t>(rng.Below(sectorCount));
        }
        chunks.push_back(std::move(tiles));
    }

    Chunk sectors = MakeChunk(rng, SaveChunk::Sectors, 0,
                              offsetof(fileformats::SaveSectorsChunk, areas) + sectorCount * sizeof(layout::Area));
    ChunkAs<fileformats::SaveSectorsChunk>(sectors).area_high_water = sectorCount - 1;
    auto* areas = reinterpret_cast<layout::Area*>(sectors.bytes.data() + offsetof(fileformats::SaveSectorsChunk, areas));
    for (std::uint32_t i = 0; i < sectorCount; ++i) {
        if (rng.Chance(1, 2)) {
            areas[i].player = 0xff;
        }
    }
    chunks.push_back(std::move(sectors));

    chunks.push_back(MakeChunk(rng, SaveChunk::Player, 0, sizeof(fileformats::SavePlayerChunk)));
    chunks.push_back(MakeTable(rng, SaveChunk::Settlers, 1 + rng.Below(3000), sizeof(layout::Settler)));
    // Buildings: castles only where UnblockCastleTiles stays on the map.
    Chunk buildings = MakeTable(rng, SaveChunk::Buildings, 1 + rng.Below(400), sizeof(layout::Building));
    auto* records = reinterpret_cast<layout::Building*>(buildings.bytes.data() + 12);
    const std::uint32_t buildingCount = ChunkAs<fileformats::SaveTableChunk>(buildings).high_water + 1;
    for (std::uint32_t i = 0; i < buildingCount; ++i) {
        constexpr std::uint8_t kCastle = 18;
        if (rng.Chance(1, 8) && mapSize >= 16) {
            records[i].art = kCastle;
            records[i].owner = static_cast<std::uint8_t>(rng.Below(kPlayers));
            records[i].x = static_cast<std::uint16_t>(2 + rng.Below(mapSize - 10));
            records[i].y = static_cast<std::uint16_t>(rng.Below(mapSize - 10));
        } else if (records[i].art == kCastle) {
            records[i].art = 0;
        }
    }
    chunks.push_back(std::move(buildings));
    chunks.push_back(MakeTable(rng, SaveChunk::Piles, 1 + rng.Below(3000), sizeof(layout::Pile)));
    chunks.push_back(MakeChunk(rng, SaveChunk::DiggerTab, 0, sizeof(fileformats::SaveDiggerTabChunk)));
    chunks.push_back(MakeChunk(rng, SaveChunk::Minimap, 0, 8 + 0x15200));
    chunks.push_back(MakeChunk(rng, SaveChunk::ActiveSettlers, 0, sizeof(fileformats::SaveActiveSettlersChunk)));
    chunks.push_back(MakeTable(rng, SaveChunk::Trade, 1 + rng.Below(1000), sizeof(layout::Trade)));
    chunks.push_back(MakeTable(rng, SaveChunk::Timer, 1 + rng.Below(10000), sizeof(layout::Effect)));
    chunks.push_back(MakeChunk(rng, SaveChunk::Messages, 0, sizeof(fileformats::SaveMessagesChunk)));
    chunks.push_back(MakeChunk(rng, SaveChunk::WarMap, 0, sizeof(fileformats::SaveWarMapChunk)));
    // The versioned chunks are always there; old versions must not ask for them.
    Chunk victory = MakeChunk(rng, SaveChunk::VictoryConditions, 0, 8 + sizeof(layout::VictoryConditions));
    victory.bytes[8 + offsetof(layout::VictoryConditions, protect_players)] = rng.Chance(1, 2) ? 0 : 1;
    chunks.push_back(std::move(victory));
    chunks.push_back(MakeChunk(rng, SaveChunk::PlayerColours, 0, 8 + kPlayers * 2));
    chunks.push_back(MakeChunk(rng, SaveChunk::ProdStat, 0, 8 + 0x2d000));
    chunks.push_back(MakeChunk(rng, SaveChunk::NewGameObjective, 0, sizeof(fileformats::SaveObjectiveChunk)));
    chunks.push_back(MakeChunk(rng, SaveChunk::NewGameObjectiveTimeout, 0, sizeof(fileformats::SaveTimeoutChunk)));
    return chunks;
}

CMapFile* MemoryFile(const std::vector<Chunk>& chunks) {
    CMapFile* file = NewMapFile();
    Original_CMapFile_SetMemoryOnly(file, nullptr);
    for (const Chunk& chunk : chunks) {
        Original_CMapFile_AddChunk(file, nullptr, chunk.type, chunk.sub, chunk.bytes.data());
    }
    return file;
}

void CompareSynthetic(test::Context& ctx, LoadSaveGameFn candidate, std::uint32_t cases) {
    test::WorldPair worlds(ctx, kWorldSize);
    for (std::uint32_t i = 0; i < cases; ++i) {
        const std::vector<Chunk> chunks = SyntheticSave(ctx.rng(), i);
        // Both files from the same random state (inserting draws node levels).
        const RandomState random = RandomState::Save();
        CMapFile* a = MemoryFile(chunks);
        random.Restore();
        CMapFile* b = MemoryFile(chunks);
        char what[64];
        std::snprintf(what, sizeof(what), "synthetic case %u", i);
        const bool same = CompareLoad(ctx, worlds, a, b, candidate, what);
        DeleteMapFile(a);
        DeleteMapFile(b);
        if (!same) {
            return;
        }
    }
}

// Wrong: one more world byte (the eco drop of sector 4095 forgotten).
void __fastcall WrongLoadSector(CLogicThread* self, void* edx, CMapFile* file) {
    const std::uint8_t player = self->world->areas[4095].player;
    CLogicThread_LoadSaveGame(self, edx, file);
    self->world->areas[4095].player = player ^ 1;
}

// Wrong: the drawn-tick global off by one.
void __fastcall WrongLoadGlobal(CLogicThread* self, void* edx, CMapFile* file) {
    CLogicThread_LoadSaveGame(self, edx, file);
    g_lastDrawnTick = g_lastDrawnTick + 1;
}

// Wrong: chunk 0 released (the original keeps it cached).
void __fastcall WrongLoadRelease(CLogicThread* self, void* edx, CMapFile* file) {
    CLogicThread_LoadSaveGame(self, edx, file);
    using ReleaseChunkFn = std::uint32_t(__fastcall*)(CMapFile*, void*, std::uint32_t, std::uint32_t);
    reinterpret_cast<ReleaseChunkFn>(0x004a6310)(file, nullptr, 0, 0);
}

}  // namespace

S3RE_TEST(CLogicThread_LoadSaveGame_Synthetic_MatchesOriginal) {
    CompareSynthetic(ctx, &CLogicThread_LoadSaveGame, 2 * static_cast<std::uint32_t>(std::size(kVersions)));
}

// Every savegame of the installation, opened read only, into a world of
// random bytes.
S3RE_TEST(CLogicThread_LoadSaveGame_AllSaveGames) {
    std::vector<std::string> files;
    ListFiles(GameFolder() + "\\Save", files);
    test::WorldPair worlds(ctx, kWorldSize);
    std::uint32_t loaded = 0;
    std::uint32_t colourCounts = 0;
    for (const std::string& path : files) {
        CMapFile* a = OpenMapFile(path, fileformats::kOpenRead);
        CMapFile* b = OpenMapFile(path, fileformats::kOpenRead);
        using GetErrorFn = std::uint32_t(__fastcall*)(CMapFile*, void*);
        const bool opened = reinterpret_cast<GetErrorFn>(0x004a60d0)(a, nullptr) == 0;
        const std::vector<std::uint8_t> raw = ReadWholeFile(path);
        if (opened && StoredWord(raw, 4) == fileformats::kFileTypeSaveGame) {
            std::uint32_t colourCount = kPlayers;
            for (const ParsedChunk& chunk : ParseContainer(raw)) {
                if (chunk.key == static_cast<std::uint32_t>(SaveChunk::PlayerColours) && chunk.bytes.size() >= 8 &&
                    chunk.bytes.size() < 8 + kPlayers * 2) {
                    colourCount = static_cast<std::uint32_t>((chunk.bytes.size() - 8) / 2);
                }
            }
            CompareLoad(ctx, worlds, a, b, &CLogicThread_LoadSaveGame, path.c_str(), colourCount);
            colourCounts += colourCount;
            ++loaded;
        }
        DeleteMapFile(a);
        DeleteMapFile(b);
    }
    S3RE_LOG("CLogicThread_LoadSaveGame_AllSaveGames: %u of %u files under Save loaded and compared "
             "(%u colour indexes from the files)\n",
             loaded, static_cast<unsigned>(files.size()), colourCounts);
    S3RE_EXPECT(loaded > 0);
}

S3RE_TEST(CLogicThread_LoadSaveGame_ComparisonDetectsWrongImplementation) {
    const LoadSaveGameFn wrongs[] = {&WrongLoadSector, &WrongLoadGlobal, &WrongLoadRelease};
    for (LoadSaveGameFn wrong : wrongs) {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareSynthetic(probe, wrong, 1);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
