// Tests of CLogicThread::SaveGame (save_game.cpp): the original and ours
// save the same world into two files, which must be the same.
//
// Output: SaveGame writes g_gameFolder + "Save\" + name (and creates
// g_gameFolder + "Save" and + "Save\Multi"). The tests point g_gameFolder
// (0x7a7c80, a std::string) at %TEMP%\s3re-savegame-test\ for the time of a
// test (TempGameFolder), so nothing is written under Settlers3Ultimate.
//
// Worlds:
//  - every savegame under Settlers3Ultimate\Save (read only, opened like
//    LoadSaveGame's tests do), loaded with LoadSaveGame;
//  - maps under Settlers3Ultimate\Map (read only), loaded with LoadMap into
//    a world built like a new game's;
//  - random worlds (random bytes, bounded map size and table sizes, random
//    player and map names of every length, computer players).
// Compared: the files chunk by chunk (keys, sizes and order; the encoded
// bytes of every chunk without uninitialized bytes; the decoded bytes of
// the others outside exactly those bytes, see "Known differences"), each
// file's checksum, the result, the DebugLog calls (GameSettings::SaveSetup
// logs), every writable byte of S3.EXE (except the CRT heap's state) and
// the world (SaveGame must not change it).
// Round trip: our file, loaded with LoadSaveGame over a copy of the world,
// gives the world back (savegame worlds: every byte; map and random worlds
// first go through one save and load, which sets the fields LoadSaveGame
// normalizes), with the globals LoadSaveGame sets.
//
// Known differences (bytes neither function writes, so they hold what the
// heap block or the stack held before; masked by the comparison):
//  - chunk 0: +0x1c the time (each must lie within its call), the bytes
//    after the map name's zero (heap);
//  - chunk 1: +0x22..+0x23 and each player name's bytes after its zero (heap);
//  - chunk 0x14 (GameSettings::SaveSetup, a stack buffer): byte +0xb of
//    each saved player record and the records past the setup count;
//  - chunk 0x1a (SaveSetup, stack): +0x22..+0x23.

#include "game/debug_log.h"
#include "game/fileformats/map_file.h"
#include "game/fileformats/map_file_test_support.h"
#include "game/fileformats/savegame_chunks.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/logic/logic_thread_layout.h"
#include "game/map/map_test_support.h"
#include "game/ui/game_settings_layout.h"
#include "game/world.h"
#include "log.h"
#include "msvc6/string.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace s3re::game {

using fileformats::ChunkHeader;
using fileformats::CMapFile;
using fileformats::SaveChunk;

bool __fastcall CLogicThread_SaveGame(CLogicThread* self, void* edx, const msvc6::String* name);
using SaveGameFn = bool(__fastcall*)(CLogicThread* self, void* edx, const msvc6::String* name);
extern SaveGameFn Original_CLogicThread_SaveGame;

void __fastcall CLogicThread_LoadSaveGame(CLogicThread* self, void* edx, CMapFile* file);
void __fastcall CGameData_LoadMap(CGameData* world, void* edx, CMapFile* file, const std::uint32_t* slotToPlayer);

namespace {

using namespace mapfile_test;
using map_test::Globals;

constexpr std::uint32_t kInfoPlayers = 20;
constexpr std::uint32_t kNoPlayer = 0xff;

// ---------------------------------------------------------------------------
// Set-up
// ---------------------------------------------------------------------------

// g_gameFolder pointed at a temporary folder (with a trailing backslash)
// while the object lives; the game's string object is put back afterwards.
class TempGameFolder {
    static constexpr const char* kMultiFolder = "Save\\Multi";

public:
    TempGameFolder() {
        char buffer[MAX_PATH] = {};
        GetTempPathA(MAX_PATH, buffer);
        folder_ = std::string(buffer) + "s3re-savegame-test\\";
        CreateDirectoryA(folder_.c_str(), nullptr);
        std::memcpy(&saved_, &g_gameFolder.get(), sizeof(saved_));
        msvc6::String_FromChars(&g_gameFolder.get(), nullptr, folder_.c_str(),
                                static_cast<std::uint32_t>(folder_.size()));
    }
    ~TempGameFolder() {
        msvc6::String_Destroy(&g_gameFolder.get(), nullptr);
        std::memcpy(&g_gameFolder.get(), &saved_, sizeof(saved_));
        RemoveDirectoryA((folder_ + kMultiFolder).c_str());
        RemoveDirectoryA((folder_ + "Save").c_str());
        RemoveDirectoryA(folder_.c_str());
    }
    TempGameFolder(const TempGameFolder&) = delete;
    TempGameFolder& operator=(const TempGameFolder&) = delete;

    std::string SavePath(const std::string& name) const { return folder_ + "Save\\" + name; }

    // Removes the (empty) Save and Save\Multi folders, so that a save has to
    // create them again.
    void RemoveSaveFolders() const {
        RemoveDirectoryA((folder_ + kMultiFolder).c_str());
        RemoveDirectoryA((folder_ + "Save").c_str());
    }
    bool SaveFoldersExist() const {
        const DWORD save = GetFileAttributesA((folder_ + "Save").c_str());
        const DWORD multi = GetFileAttributesA((folder_ + kMultiFolder).c_str());
        return save != INVALID_FILE_ATTRIBUTES && (save & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
               multi != INVALID_FILE_ATTRIBUTES && (multi & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

private:
    std::string folder_;
    msvc6::String saved_;
};

// GameSettings::PlayerSetup (0x4351e0) grows an empty player-setup vector
// to one record on its first call; SaveSetup then saves that record.
void PrepareSettings() {
    using PlayerSetupFn = void*(__fastcall*)(GameSettings* self, void* edx, std::uint32_t index);
    reinterpret_cast<PlayerSetupFn>(0x004351e0)(&g_gameSettings.get(), nullptr, 0);
}

// A logic thread object with the fields SaveGame reads.
struct FakeLogicThread {
    alignas(4) std::uint8_t bytes[0x700] = {};
    CLogicThread* get() { return reinterpret_cast<CLogicThread*>(bytes); }
};

void SetUpThread(test::Rng& rng, FakeLogicThread& thread, CGameData* world) {
    CLogicThread* t = thread.get();
    t->world = world;
    t->tick = rng.Next32();
    const std::uint32_t computers = rng.Below(4);
    for (std::uint32_t& computer : t->computer_players) {
        computer = 0;
    }
    for (std::uint32_t i = 0; i < computers; ++i) {
        t->computer_players[rng.Below(kInfoPlayers)] = 0x10000000u | rng.Next32();  // never dereferenced
    }
}

// Random text of `length` printable characters.
std::string RandomText(test::Rng& rng, std::uint32_t length) {
    std::string text(length, 'a');
    for (char& c : text) {
        c = static_cast<char>(' ' + rng.Below(95));
    }
    return text;
}

void AssignText(msvc6::String* s, const std::string& text) {
    msvc6::String_AssignChars(s, nullptr, text.c_str(), static_cast<std::uint32_t>(text.size()));
}

std::string TextOf(const msvc6::String& s) {
    return s.ptr != nullptr ? std::string(s.ptr, s.len) : std::string();
}

// The globals the loaders, SaveGame and these tests change, put back at the
// end of a test (the names by their text: their buffers are heap blocks).
class SavedGlobals {
public:
    SavedGlobals() : bytes_(Globals::Now()) {
        mapName_ = TextOf(g_gameSettings->map_name);
        for (std::uint32_t i = 0; i < kInfoPlayers; ++i) {
            names_.push_back(TextOf(g_playerInfo[i].name));
        }
    }
    ~SavedGlobals() {
        AssignText(&g_gameSettings->map_name, mapName_);
        for (std::uint32_t i = 0; i < kInfoPlayers; ++i) {
            AssignText(&g_playerInfo[i].name, names_[i]);
        }
        Put(g_lastDrawnTick.address(), 4);
        Put(g_gameTick.address(), 4);
        Put(g_strengthDivisor.address(), 4);
        Put(g_gameOutcome.address(), 4);
        Put(g_campaignMission.address(), 4);
        Put(g_playerColourIndex.address(), kInfoPlayers * 2);
        Put(g_victoryProtectActive.address(), 1);
        Put(g_worldUpdateDisabled.address(), 1);
        for (std::uint32_t i = 0; i < kInfoPlayers; ++i) {
            Put(reinterpret_cast<std::uintptr_t>(&g_playerInfo[i].unk00), 8);
            Put(reinterpret_cast<std::uintptr_t>(&g_playerInfo[i].fields[0]), sizeof(PlayerInfo::fields));
        }
        Put(reinterpret_cast<std::uintptr_t>(&g_gameSettings->start_goods), 4);
    }
    SavedGlobals(const SavedGlobals&) = delete;
    SavedGlobals& operator=(const SavedGlobals&) = delete;

private:
    void Put(std::uintptr_t address, std::size_t size) const {
        const std::ptrdiff_t offset = Globals::OffsetOf(address);
        std::memcpy(reinterpret_cast<void*>(address), bytes_.data() + offset, size);
    }

    std::vector<std::uint8_t> bytes_;
    std::string mapName_;
    std::vector<std::string> names_;
};

// ---------------------------------------------------------------------------
// Saving and comparing
// ---------------------------------------------------------------------------

using TimeFn = std::uint32_t(__cdecl*)(std::uint32_t* result);
const auto kTime = reinterpret_cast<TimeFn>(0x0061f6a7);  // the CRT's time, as SaveGame calls it

struct Saved {
    bool ok = false;
    std::vector<std::uint8_t> bytes;
    std::vector<DebugLogCall> log;
    std::uint32_t timeFrom = 0;
    std::uint32_t timeTo = 0;
    bool foldersMade = false;
};

Saved Save(SaveGameFn save, CLogicThread* thread, const TempGameFolder& folder, const std::string& name) {
    const std::string path = folder.SavePath(name);
    DeleteFileA(path.c_str());
    folder.RemoveSaveFolders();
    msvc6::String text;
    msvc6::String_FromChars(&text, nullptr, name.c_str(), static_cast<std::uint32_t>(name.size()));
    Saved saved;
    DebugLogRecorder log;
    saved.timeFrom = kTime(nullptr);
    saved.ok = save(thread, nullptr, &text);
    saved.timeTo = kTime(nullptr);
    saved.log = log.Take();
    msvc6::String_Destroy(&text, nullptr);
    saved.foldersMade = folder.SaveFoldersExist();
    saved.bytes = ReadWholeFile(path);
    DeleteFileA(path.c_str());
    return saved;
}

// The chunks of a container in file order (duplicates included).
struct FileChunk {
    std::uint32_t key;
    std::uint32_t size;
    std::size_t at;
};

std::vector<FileChunk> ChunksInOrder(const std::vector<std::uint8_t>& file) {
    std::vector<FileChunk> chunks;
    std::size_t at = 8;
    while (at + sizeof(ChunkHeader) <= file.size()) {
        ChunkHeader header;
        std::memcpy(&header, file.data() + at, sizeof(header));
        chunks.push_back({header.key, header.size, at});
        if (header.size < sizeof(ChunkHeader) || header.size > file.size() - at) {
            break;
        }
        at += header.size;
    }
    return chunks;
}

// What the masks need: the texts SaveGame copies and the setup count.
struct MaskInputs {
    std::uint32_t mapNameLength;
    std::uint32_t nameLengths[kInfoPlayers];
    std::uint32_t setupCount;

    static MaskInputs Now() {
        MaskInputs m{};
        m.mapNameLength = g_gameSettings->map_name.len;
        for (std::uint32_t i = 0; i < kInfoPlayers; ++i) {
            m.nameLengths[i] = g_playerInfo[i].name.len;
        }
        m.setupCount = g_gameSettings->player_setups.length;
        return m;
    }
};

constexpr std::uint32_t kSetupRecordsAt = 0x0c;
constexpr std::uint32_t kSetupRecordBytes = 0x18;
constexpr std::uint32_t kSetupRecords = 20;
constexpr std::uint32_t kSetupRecordUnwritten = 0x0b;

// The decoded bytes of `key` that neither function writes.
std::vector<bool> UnwrittenBytes(std::uint32_t key, std::uint32_t size, const MaskInputs& in) {
    std::vector<bool> mask(size, false);
    auto maskRange = [&](std::size_t from, std::size_t to) {
        for (std::size_t i = from; i < to && i < size; ++i) {
            mask[i] = true;
        }
    };
    switch (key) {
    case static_cast<std::uint32_t>(SaveChunk::Header):
        maskRange(offsetof(fileformats::SaveHeaderChunk, save_time), offsetof(fileformats::SaveHeaderChunk, map_name));
        maskRange(offsetof(fileformats::SaveHeaderChunk, map_name) + in.mapNameLength + 1, size);
        break;
    case static_cast<std::uint32_t>(SaveChunk::General):
        maskRange(offsetof(fileformats::SaveGeneralChunk, pad22), offsetof(fileformats::SaveGeneralChunk, pad22) + 2);
        for (std::uint32_t i = 0; i < kInfoPlayers; ++i) {
            const std::size_t name = offsetof(fileformats::SaveGeneralChunk, players) +
                                     i * sizeof(fileformats::SavedPlayerInfo) +
                                     offsetof(fileformats::SavedPlayerInfo, name);
            if (in.nameLengths[i] < sizeof(fileformats::SavedPlayerInfo::name)) {
                maskRange(name + in.nameLengths[i] + 1, name + sizeof(fileformats::SavedPlayerInfo::name));
            }
        }
        break;
    case static_cast<std::uint32_t>(SaveChunk::SetupPlayers):
        for (std::uint32_t r = 0; r < kSetupRecords; ++r) {
            const std::size_t record = kSetupRecordsAt + r * kSetupRecordBytes;
            if (r < in.setupCount) {
                maskRange(record + kSetupRecordUnwritten, record + kSetupRecordUnwritten + 1);
            } else {
                maskRange(record, record + kSetupRecordBytes);
            }
        }
        break;
    case static_cast<std::uint32_t>(SaveChunk::Setup1A):
        maskRange(0x22, 0x24);
        break;
    default:
        break;
    }
    return mask;
}

bool HasUnwrittenBytes(std::uint32_t key) {
    return key == static_cast<std::uint32_t>(SaveChunk::Header) ||
           key == static_cast<std::uint32_t>(SaveChunk::General) ||
           key == static_cast<std::uint32_t>(SaveChunk::SetupPlayers) ||
           key == static_cast<std::uint32_t>(SaveChunk::Setup1A);
}

std::uint32_t DecodedWord(const std::vector<std::uint8_t>& chunk, std::size_t at) {
    std::uint32_t word = 0;
    std::memcpy(&word, chunk.data() + at, 4);
    return word;
}

// How many bytes the comparisons left out because neither function writes
// them, and how many of those differed (logged per test).
struct MaskStats {
    std::uint64_t chunks = 0;
    std::uint64_t bytes = 0;
    std::uint64_t masked = 0;
    std::uint64_t maskedDiffering = 0;
    std::uint64_t differingByChunk[4] = {};  // chunks 0, 1, 0x14, 0x1a
};
MaskStats g_maskStats;

void LogMaskStats(const char* test) {
    S3RE_LOG("%s: %llu chunks (%llu bytes) compared; %llu bytes left out as unwritten, %llu of them differed "
             "(chunk 0: %llu, 1: %llu, 0x14: %llu, 0x1a: %llu)\n",
             test, g_maskStats.chunks, g_maskStats.bytes, g_maskStats.masked, g_maskStats.maskedDiffering,
             g_maskStats.differingByChunk[0], g_maskStats.differingByChunk[1], g_maskStats.differingByChunk[2],
             g_maskStats.differingByChunk[3]);
    g_maskStats = MaskStats{};
}

// Compares the two files; `what` names the case on failure.
bool CompareFilesOf(test::Context& ctx, const Saved& a, const Saved& b, const MaskInputs& in) {
    const int failures = ctx.failures();
    S3RE_EXPECT_EQ(a.ok, b.ok);
    S3RE_EXPECT(a.foldersMade);
    S3RE_EXPECT(b.foldersMade);
    S3RE_EXPECT(!a.bytes.empty());
    if (!S3RE_EXPECT_EQ(a.bytes.size(), b.bytes.size())) {
        return false;
    }
    // Each file's own checksum and the file type.
    S3RE_EXPECT_EQ(StoredWord(a.bytes, 0), ComputeChecksum(a.bytes));
    S3RE_EXPECT_EQ(StoredWord(b.bytes, 0), ComputeChecksum(b.bytes));
    S3RE_EXPECT_EQ(StoredWord(a.bytes, 4), fileformats::kFileTypeSaveGame);
    S3RE_EXPECT_EQ(StoredWord(b.bytes, 4), fileformats::kFileTypeSaveGame);

    const std::vector<FileChunk> ca = ChunksInOrder(a.bytes);
    const std::vector<FileChunk> cb = ChunksInOrder(b.bytes);
    if (!S3RE_EXPECT_EQ(ca.size(), cb.size())) {
        return false;
    }
    std::uint32_t reported = 0;
    for (std::size_t k = 0; k < ca.size() && reported < 5; ++k) {
        if (ca[k].key != cb[k].key || ca[k].size != cb[k].size || ca[k].at != cb[k].at) {
            ctx.Fail(__FILE__, __LINE__, "chunk %u: key 0x%x size %u at %u, ours key 0x%x size %u at %u",
                     static_cast<unsigned>(k), ca[k].key, ca[k].size, static_cast<unsigned>(ca[k].at), cb[k].key,
                     cb[k].size, static_cast<unsigned>(cb[k].at));
            ++reported;
            continue;
        }
        const std::uint8_t* ra = a.bytes.data() + ca[k].at;
        const std::uint8_t* rb = b.bytes.data() + cb[k].at;
        ++g_maskStats.chunks;
        g_maskStats.bytes += ca[k].size;
        if (!HasUnwrittenBytes(ca[k].key)) {
            if (std::memcmp(ra, rb, ca[k].size) != 0) {
                ctx.Fail(__FILE__, __LINE__, "chunk %u (key 0x%x, %u bytes) differs", static_cast<unsigned>(k),
                         ca[k].key, ca[k].size);
                ++reported;
            }
            continue;
        }
        std::vector<std::uint8_t> da(ra, ra + ca[k].size);
        std::vector<std::uint8_t> db(rb, rb + cb[k].size);
        fileformats::DecodeChunkData(da.data(), ca[k].size, static_cast<std::uint8_t>(ca[k].key));
        fileformats::DecodeChunkData(db.data(), cb[k].size, static_cast<std::uint8_t>(cb[k].key));
        const std::vector<bool> mask = UnwrittenBytes(ca[k].key, ca[k].size, in);
        for (std::size_t i = 0; i < da.size(); ++i) {
            if (mask[i]) {
                const std::uint32_t key = ca[k].key;
                const int slot = key == 0 ? 0 : key == 1 ? 1 : key == 0x14 ? 2 : 3;
                ++g_maskStats.masked;
                g_maskStats.maskedDiffering += da[i] != db[i] ? 1 : 0;
                g_maskStats.differingByChunk[slot] += da[i] != db[i] ? 1 : 0;
            }
        }
        for (std::size_t i = 0; i < da.size(); ++i) {
            if (!mask[i] && da[i] != db[i]) {
                ctx.Fail(__FILE__, __LINE__, "chunk %u (key 0x%x) byte +0x%x differs (%02x, ours %02x)",
                         static_cast<unsigned>(k), ca[k].key, static_cast<unsigned>(i), da[i], db[i]);
                ++reported;
                break;
            }
        }
        if (ca[k].key == static_cast<std::uint32_t>(SaveChunk::Header)) {
            const std::size_t at = offsetof(fileformats::SaveHeaderChunk, save_time);
            S3RE_EXPECT(DecodedWord(da, at) >= a.timeFrom && DecodedWord(da, at) <= a.timeTo);
            S3RE_EXPECT(DecodedWord(db, at) >= b.timeFrom && DecodedWord(db, at) <= b.timeTo);
        }
    }
    if (S3RE_EXPECT_EQ(a.log.size(), b.log.size())) {
        for (std::size_t k = 0; k < a.log.size(); ++k) {
            S3RE_EXPECT(a.log[k] == b.log[k]);
        }
    }
    return ctx.failures() == failures;
}

// FNV-1a over the world (SaveGame must leave it alone).
std::uint64_t WorldHash(const CGameData* world) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(world);  // raw-ok: a hash of the whole buffer
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t i = 0; i < sizeof(CGameData); i += 8) {
        std::uint64_t word;
        std::memcpy(&word, bytes + i, 8);
        hash = (hash ^ word) * 1099511628211ull;
    }
    return hash;
}

// Saves `world` with the original and with `candidate` and compares the
// files, the globals and the world. Returns our file (empty on failure).
std::vector<std::uint8_t> CompareSave(test::Context& ctx, const TempGameFolder& folder, CLogicThread* thread,
                                      SaveGameFn candidate, const std::string& name, const char* what) {
    const int failures = ctx.failures();
    const std::uint64_t worldBefore = WorldHash(thread->world);
    const MaskInputs in = MaskInputs::Now();
    const RandomState random0 = RandomState::Save();
    Globals globals;
    const Saved a = Save(Original_CLogicThread_SaveGame, thread, folder, name);
    const RandomState randomA = RandomState::Save();
    const std::uintptr_t heap = g_crtSmallBlockHeap.address();
    const std::size_t heapSize = sizeof(g_crtSmallBlockHeap.get());
    const std::vector<std::uint8_t> globalsA = globals.TakeOriginalExcept(heap, heapSize);
    S3RE_EXPECT_EQ(WorldHash(thread->world), worldBefore);

    random0.Restore();
    const Saved b = Save(candidate, thread, folder, name);
    const RandomState randomB = RandomState::Save();
    const std::vector<std::uint8_t> globalsB = Globals::Now();
    S3RE_EXPECT_EQ(WorldHash(thread->world), worldBefore);

    CompareFilesOf(ctx, a, b, in);
    S3RE_EXPECT(randomA == randomB);
    // Outside the CRT heap's state and time()'s cache (the clock).
    const std::ptrdiff_t heapOffset = Globals::OffsetOf(heap);
    const std::ptrdiff_t timeOffset = Globals::OffsetOf(g_crtTimeCache.address());
    const std::ptrdiff_t timeSize = static_cast<std::ptrdiff_t>(sizeof(g_crtTimeCache.get()));
    std::vector<std::uint8_t> globalsB2 = globalsB;
    std::memcpy(globalsB2.data() + timeOffset, globalsA.data() + timeOffset, static_cast<std::size_t>(timeSize));
    const std::ptrdiff_t difference =
        Globals::FirstDifference(globalsA, globalsB2, heapOffset, heapOffset + static_cast<std::ptrdiff_t>(heapSize));
    if (difference >= 0) {
        ctx.Fail(__FILE__, __LINE__, "  globals differ at 0x%08x (%02x, %02x)",
                 static_cast<unsigned>(Globals::AddressOf(difference)), globalsA[difference], globalsB2[difference]);
    }
    if (ctx.failures() != failures) {
        ctx.Fail(__FILE__, __LINE__, "  in %s", what);
        return {};
    }
    return b.bytes;
}

// ---------------------------------------------------------------------------
// Round trip
// ---------------------------------------------------------------------------

// The globals LoadSaveGame sets (16 colour indexes: chunk 0x19 holds 16).
struct LoadedGlobals {
    std::uint32_t lastDrawnTick, gameTick, activePlayers;
    std::int32_t gameOutcome, campaignMission;
    std::uint16_t colours[16];
    std::string names[kInfoPlayers];
    std::int32_t unk[kInfoPlayers][2];
    std::int32_t fields[kInfoPlayers][11];

    static LoadedGlobals Now() {
        LoadedGlobals g{};
        g.lastDrawnTick = g_lastDrawnTick;
        g.gameTick = g_gameTick;
        g.activePlayers = g_strengthDivisor;
        g.gameOutcome = g_gameOutcome;
        g.campaignMission = g_campaignMission;
        std::memcpy(g.colours, &g_playerColourIndex[0], sizeof(g.colours));
        for (std::uint32_t i = 0; i < kInfoPlayers; ++i) {
            const PlayerInfo& p = g_playerInfo[i];
            g.names[i] = TextOf(p.name);
            g.unk[i][0] = p.unk00;
            g.unk[i][1] = p.unk04;
            std::memcpy(g.fields[i], p.fields, sizeof(p.fields));
        }
        return g;
    }
    bool operator==(const LoadedGlobals& o) const {
        bool same = lastDrawnTick == o.lastDrawnTick && gameTick == o.gameTick && activePlayers == o.activePlayers &&
                    gameOutcome == o.gameOutcome && campaignMission == o.campaignMission &&
                    std::memcmp(colours, o.colours, sizeof(colours)) == 0 && std::memcmp(unk, o.unk, sizeof(unk)) == 0 &&
                    std::memcmp(fields, o.fields, sizeof(fields)) == 0;
        for (std::uint32_t i = 0; i < kInfoPlayers; ++i) {
            same = same && names[i] == o.names[i];
        }
        return same;
    }
};

// Loads the savegame `bytes` (written to a temporary file) into `world`.
void LoadInto(CGameData* world, const std::string& path, const std::vector<std::uint8_t>& bytes) {
    WriteWholeFile(path, bytes);
    CMapFile* file = OpenMapFile(path, fileformats::kOpenRead);
    FakeLogicThread thread;
    thread.get()->world = world;
    CLogicThread_LoadSaveGame(thread.get(), nullptr, file);
    DeleteMapFile(file);
    DeleteFileA(path.c_str());
}

// Our file loaded over a copy of `world` must give `world` and the globals
// back.
void CheckRoundTrip(test::Context& ctx, CGameData* world, test::WorldBuffer& copy, const std::string& path,
                    const std::vector<std::uint8_t>& bytes, const char* what) {
    const LoadedGlobals before = LoadedGlobals::Now();
    std::memcpy(copy.data(), world, sizeof(CGameData));  // raw-ok: the whole test buffer
    LoadInto(copy.world(), path, bytes);
    const int failures = ctx.failures();
    S3RE_EXPECT_MEM_EQ(world, copy.data(), sizeof(CGameData));
    S3RE_EXPECT(LoadedGlobals::Now() == before);
    if (ctx.failures() != failures) {
        ctx.Fail(__FILE__, __LINE__, "  round trip of %s", what);
    }
}

// ---------------------------------------------------------------------------
// Worlds
// ---------------------------------------------------------------------------

// Every savegame of the installation (read only).
void CompareSaveGames(test::Context& ctx, SaveGameFn candidate, bool roundTrip) {
    SavedGlobals savedGlobals;
    TempGameFolder folder;
    PrepareSettings();
    std::vector<std::string> files;
    ListFiles(GameFolder() + "\\Save", files);
    test::WorldBuffer world(sizeof(CGameData));
    test::WorldBuffer copy(sizeof(CGameData));
    std::uint32_t saved = 0;
    for (const std::string& path : files) {
        if (StoredWord(ReadWholeFile(path), 4) != fileformats::kFileTypeSaveGame) {
            continue;
        }
        ctx.rng().Fill(world.data(), world.size());
        CMapFile* file = OpenMapFile(path, fileformats::kOpenRead);
        FakeLogicThread loader;
        loader.get()->world = world.world();
        CLogicThread_LoadSaveGame(loader.get(), nullptr, file);
        DeleteMapFile(file);

        FakeLogicThread thread;
        SetUpThread(ctx.rng(), thread, world.world());
        const std::vector<std::uint8_t> ours =
            CompareSave(ctx, folder, thread.get(), candidate, "s3re test.sav", path.c_str());
        if (ours.empty()) {
            return;
        }
        if (roundTrip) {
            CheckRoundTrip(ctx, world.world(), copy, folder.SavePath("roundtrip.sav"), ours, path.c_str());
        }
        ++saved;
    }
    S3RE_LOG("CLogicThread_SaveGame_AllSaveGames: %u savegames under Save loaded, saved by both and compared\n",
             saved);
    LogMaskStats("CLogicThread_SaveGame_AllSaveGames");
    S3RE_EXPECT(saved > 0);
}

// Maps (every `step`-th, read only), loaded like a new game's.
void CompareMaps(test::Context& ctx, SaveGameFn candidate, std::uint32_t step) {
    SavedGlobals savedGlobals;
    TempGameFolder folder;
    PrepareSettings();
    std::vector<std::string> paths;
    ListFiles(GameFolder() + "\\Map", paths);
    test::WorldBuffer world(sizeof(CGameData));
    test::WorldBuffer copy(sizeof(CGameData));
    std::uint32_t saved = 0;
    std::uint32_t index = 0;
    for (const std::string& path : paths) {
        const std::vector<std::uint8_t> raw = ReadWholeFile(path);
        if (StoredWord(raw, 4) != fileformats::kFileTypeMap || index++ % step != 0) {
            continue;
        }
        // The players: 2..4 taking slots 0..n-1, the start positions of
        // chunk 2 (as the menus pass them), random races and seed.
        test::Rng& rng = ctx.rng();
        const std::uint32_t players = 2 + rng.Below(3);
        std::int32_t starts[4][2] = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}};
        for (const ParsedChunk& chunk : ParseContainer(raw)) {
            for (std::uint32_t slot = 0; chunk.key == 2 && slot < 4; ++slot) {
                const std::size_t at = 8 + slot * 45;
                if (at + 12 <= chunk.bytes.size()) {
                    std::memcpy(&starts[slot][0], chunk.bytes.data() + at + 4, 4);
                    std::memcpy(&starts[slot][1], chunk.bytes.data() + at + 8, 4);
                }
            }
        }
        map_test::ConstructWorld(world.world());
        std::uint32_t slots[256];
        for (std::uint32_t& slot : slots) {
            slot = kNoPlayer;
        }
        for (std::uint32_t p = 0; p < layout::kPlayers; ++p) {
            layout::Player& player = world.world()->players[p];
            player.active = p < players ? (p == 0 ? 1 : 2) : 0;
            player.race = static_cast<std::int32_t>(rng.Below(4));
            player.team = 0;
            player.start_x = p < players ? starts[p][0] : -1;
            player.start_y = p < players ? starts[p][1] : -1;
            if (p < players) {
                slots[p] = p;
            }
        }
        for (std::uint16_t& word : world.world()->random_state) {
            word = static_cast<std::uint16_t>(rng.Next32());
        }
        g_gameSettings->start_goods = rng.Below(4);
        g_campaignMission = 0;
        CMapFile* file = OpenMapFile(path, fileformats::kOpenRead);
        CGameData_LoadMap(world.world(), nullptr, file, slots);
        DeleteMapFile(file);

        FakeLogicThread thread;
        SetUpThread(rng, thread, world.world());
        std::vector<std::uint8_t> ours =
            CompareSave(ctx, folder, thread.get(), candidate, "s3re map test.sav", path.c_str());
        if (ours.empty()) {
            return;
        }
        // One load normalizes the fields LoadSaveGame sets; then save and
        // load must give the world back.
        LoadInto(world.world(), folder.SavePath("normalize.sav"), ours);
        ours = CompareSave(ctx, folder, thread.get(), candidate, "s3re map test 2.sav", path.c_str());
        if (ours.empty()) {
            return;
        }
        CheckRoundTrip(ctx, world.world(), copy, folder.SavePath("roundtrip.sav"), ours, path.c_str());
        ++saved;
    }
    S3RE_LOG("CLogicThread_SaveGame_Maps: %u maps under Map loaded, saved by both and compared\n", saved);
    LogMaskStats("CLogicThread_SaveGame_Maps");
    S3RE_EXPECT(saved > 0);
}

// Random worlds: random bytes, bounded sizes, names of every length.
void CompareRandomWorlds(test::Context& ctx, SaveGameFn candidate, std::uint32_t cases, bool roundTrip) {
    SavedGlobals savedGlobals;
    TempGameFolder folder;
    PrepareSettings();
    test::WorldBuffer world(sizeof(CGameData));
    test::WorldBuffer copy(sizeof(CGameData));
    constexpr std::uint32_t kMapSizes[] = {0, 1, 7, 64, 130, 256};
    for (std::uint32_t c = 0; c < cases; ++c) {
        test::Rng& rng = ctx.rng();
        rng.Fill(world.data(), world.size());
        CGameData* w = world.world();
        w->map_size = kMapSizes[rng.Below(static_cast<std::uint32_t>(std::size(kMapSizes)))];
        w->local_player = static_cast<std::int32_t>(rng.Below(layout::kPlayers));
        w->area_high_water = rng.Chance(1, 4) ? 4095 : rng.Below(4096);
        w->settler_high_water = rng.Below(32000);
        w->building_high_water = rng.Below(4000);
        w->pile_high_water = rng.Below(32000);
        w->trade_high_water = rng.Below(1000);
        w->effects_high_water = rng.Below(10000);
        // Names of every length: up to 255 characters (they fit), for players
        // 0..18 sometimes up to 299 (strcpy runs into the record's fields,
        // which are written after it).
        for (std::uint32_t i = 0; i < kInfoPlayers; ++i) {
            const std::uint32_t longest = i + 1 < kInfoPlayers && rng.Chance(1, 4) ? 300 : 256;
            AssignText(&g_playerInfo[i].name, RandomText(rng, rng.Below(longest)));
            rng.Fill(&g_playerInfo[i].unk00, 8);
            rng.Fill(g_playerInfo[i].fields, sizeof(PlayerInfo::fields));
        }
        AssignText(&g_gameSettings->map_name, RandomText(rng, rng.Below(256)));
        rng.Fill(&g_playerColourIndex[0], kInfoPlayers * 2);
        g_lastDrawnTick = rng.Next32();
        g_gameTick = rng.Next32();
        g_strengthDivisor = rng.Next32();
        g_gameOutcome = static_cast<std::int32_t>(rng.Next32());
        g_campaignMission = static_cast<std::int32_t>(rng.Next32());

        FakeLogicThread thread;
        SetUpThread(rng, thread, w);
        char what[64];
        std::snprintf(what, sizeof(what), "random world %u (map size %u)", c, w->map_size);
        std::vector<std::uint8_t> ours = CompareSave(ctx, folder, thread.get(), candidate, "random.sav", what);
        if (ours.empty()) {
            return;
        }
        if (roundTrip && w->map_size >= 16) {
            // No castles: UnblockCastleTiles on random buildings would write
            // around random positions (the round trip is about the chunks).
            for (std::uint32_t b = 0; b <= w->building_high_water; ++b) {
                w->buildings[b].art = 0;
            }
            for (std::uint32_t y = 0; y < w->map_size; ++y) {
                for (std::uint32_t x = 0; x < w->map_size; ++x) {
                    w->tiles[x][y].area = static_cast<std::uint16_t>(rng.Below(w->area_high_water + 1));
                }
            }
            LoadInto(w, folder.SavePath("normalize.sav"), ours);
            ours = CompareSave(ctx, folder, thread.get(), candidate, "random 2.sav", what);
            if (ours.empty()) {
                return;
            }
            CheckRoundTrip(ctx, w, copy, folder.SavePath("roundtrip.sav"), ours, what);
        }
    }
    LogMaskStats("CLogicThread_SaveGame_RandomWorlds");
}

// ---------------------------------------------------------------------------
// Wrong implementations
// ---------------------------------------------------------------------------

// The tick passed to SaveSetup off by one (chunk 0x1a).
bool __fastcall WrongSaveTick(CLogicThread* self, void* edx, const msvc6::String* name) {
    ++self->tick;
    const bool ok = CLogicThread_SaveGame(self, edx, name);
    --self->tick;
    return ok;
}

// One player name character different (chunk 1, next to masked bytes).
bool __fastcall WrongSaveName(CLogicThread* self, void* edx, const msvc6::String* name) {
    const std::string text = TextOf(g_playerInfo[3].name);
    AssignText(&g_playerInfo[3].name, text + "x");
    const bool ok = CLogicThread_SaveGame(self, edx, name);
    AssignText(&g_playerInfo[3].name, text);
    return ok;
}

// One event of the second ring different (bytes the loader doesn't read).
bool __fastcall WrongSaveEvents(CLogicThread* self, void* edx, const msvc6::String* name) {
    self->world->events[999].c ^= 1;
    const bool ok = CLogicThread_SaveGame(self, edx, name);
    self->world->events[999].c ^= 1;
    return ok;
}

// The result inverted.
bool __fastcall WrongSaveResult(CLogicThread* self, void* edx, const msvc6::String* name) {
    return !CLogicThread_SaveGame(self, edx, name);
}

}  // namespace

S3RE_TEST(CLogicThread_SaveGame_AllSaveGames) {
    CompareSaveGames(ctx, &CLogicThread_SaveGame, true);
}

S3RE_TEST(CLogicThread_SaveGame_Maps) {
    CompareMaps(ctx, &CLogicThread_SaveGame, 12);
}

S3RE_TEST(CLogicThread_SaveGame_RandomWorlds_MatchOriginal) {
    CompareRandomWorlds(ctx, &CLogicThread_SaveGame, 12, true);
}

S3RE_TEST(CLogicThread_SaveGame_ComparisonDetectsWrongImplementation) {
    const SaveGameFn wrongs[] = {&WrongSaveTick, &WrongSaveName, &WrongSaveEvents, &WrongSaveResult};
    for (SaveGameFn wrong : wrongs) {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareRandomWorlds(probe, wrong, 1, false);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
