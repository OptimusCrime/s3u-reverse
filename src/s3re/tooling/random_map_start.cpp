// Starting a random-map game without menus (`s3re_launcher --random-map
// SIZE[,SYMMETRY[,FIELD0C]] [--goods G] [--computers N] [--race R]`,
// S3RE_RANDOM_MAP), for whole-game checks of the random map generator
// CGameData 0x5166d0 (CGameData::GenerateRandomMap) and for recording whole
// games played by a person (docs/recording_a_game.md). Also applies --goods,
// --computers and --race to a -map file's own map (kSetupMapOptions,
// 2026-11-24: the launcher passes flags 2; the map file is kept).
//
// Tooling, not a reimplementation. How the game starts a random map:
//
// - The map-selection dialog (constructor 0x47fea0, result 0x481e90) keeps a
//   Dirk::CMapFile (0x134 bytes, constructor 0x4a5a70) at this+0x2650. When
//   its random entry is chosen it switches the file to memory only (0x4a5f40
//   close, 0x4a60c0 sets +4 = 1, so chunk lookups 0x4a60f0 never read a
//   file) and, when the dialog closes with the random type (this+0x2660 ==
//   2), adds two chunks with CMapFile::AddChunk 0x4a63f0 (thiscall: type,
//   sub-index, data; data dword 1 is the chunk size including the 8-byte
//   header):
//     chunk 5 (0x18 bytes): word 5, word 0, dword 0x18, map size
//       ((combo index + 6) * 64, default 384), dword from the second combo
//       (only read by GameSettings 0x436f80 for CNetSession and the game
//       info screen; neither the generator nor the simulation reads it),
//       symmetry (third combo), seed (atoi of the seed field; the default
//       text is rand()).
//     chunk 1 (0x20 bytes): word 1, word 0, dword 0x20, dword 2 (random
//       map), number of start positions min(((size - 32)^2 >> 14) + 1, 20),
//       four zero dwords (+0x10 would be the map's start goods).
//   The free-game dialog (0x4878b0 -> 0x488300) then calls GameSettings::
//   NewGame 0x431ab0 (thiscall, 13 stack arguments, ret 0x34) on the global
//   GameSettings 0x7a7a18. Its arguments (re/game_setup.md): 4 = players
//   per party, 5 = computer players per party (two Watcom int vectors),
//   6 = the map file, 9 = start goods (GameSettings+0x74; 0 = the map's,
//   1..3 = low, medium, high).
// - CLogicThread::OnStart (0x53c9e0) finds chunk 5 (0x53cdfe) and calls
//   Create(word seed, size, symmetry) before the map loader 0x4fd540, which
//   finds no landscape chunks (6..10) in such a file.
//
// The menu thread's `-map` start (Menus::CDrawThread::Update 0x493880, state
// 0x14) builds the player setup from a map file (0x4a67c0 reads its chunk 3,
// which a random map doesn't have: players per party, and computers per
// party = the same minus one human in party 0) and then calls NewGame with
// that file (start goods argument 0). This pass-through hook on NewGame,
// only for that call site (return address 0x493a56) and only once:
// - builds a random map file exactly like the dialog and passes it instead
//   of the -map file;
// - with --goods, passes the start goods like the dialog's goods combo;
// - with --computers N, rewrites the two party vectors to N + 1 parties of
//   one player each: you alone in party 0, one computer player in each other
//   party (what the free-game dialog produces for that choice);
// - with --race, passes the human player's race: NewGame gives every player
//   race 0xFF (random, drawn by OnStart with CGameData::Random) when the
//   map has no chunk 2; a pass-through on the player record's setter
//   (GamePlayerSetup::Set 0x49f120, only during this NewGame call) replaces
//   race and race-given flag of the human player's record, as a chunk 2
//   race would.
// The menu thread doesn't read the setup afterwards; the -map file (default
// Map\MULTI\384-2-Lakes.map) only supplies what the -map start reads besides.
// The game has no computer-player difficulty (re/game_setup.md).
//
// The seed: GenerateRandomMap seeds the RNG through CGameData::SeedRandom, so with
// `--seed N` (S3RE_SEED) the map is generated from N as well.

#include "tooling/random_map_start.h"

#include "hooks.h"
#include "log.h"

#include <windows.h>
#include <intrin.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <atomic>

namespace s3re::tooling {

namespace {

struct CGameSettings;  // GameSettings at 0x7a7a18, opaque
struct CMapFile;       // Dirk::CMapFile, opaque, 0x134 bytes
struct GamePlayerSetup;  // one player's record in the game setup (0xcc bytes), opaque

#pragma pack(push, 1)
// Watcom WCValVector<int> as WCIntVector_Construct 0x4376b0 builds it
// (framework_small.cpp); NewGame reads `data` and `count`, the dialog's
// insert (0x488c5d..0x488d49) increments `entries` and `count` together.
struct WCIntVector {
    void* vtable;            // +0x00
    std::uint32_t unk04;     // +0x04
    std::int32_t* data;      // +0x08
    std::uint32_t capacity;  // +0x0c
    std::uint32_t entries;   // +0x10
    std::uint32_t count;     // +0x14
    std::uint32_t grow;      // +0x18
};
#pragma pack(pop)
static_assert(offsetof(WCIntVector, data) == 0x08);
static_assert(offsetof(WCIntVector, capacity) == 0x0c);
static_assert(offsetof(WCIntVector, count) == 0x14);

using NewGameFn = void(__fastcall*)(CGameSettings* self, void* edx, std::uint32_t a1,
                                    std::uint32_t a2, std::uint32_t a3,
                                    WCIntVector* playersPerParty,
                                    WCIntVector* computersPerParty, CMapFile* mapFile, void* a7,
                                    std::uint32_t a8, std::uint32_t startGoods,
                                    std::uint32_t a10, std::uint32_t a11, std::uint32_t a12,
                                    std::uint32_t a13);
// GamePlayerSetup::Set 0x49f120 (thiscall, 9 stack arguments, ret 0x24):
// +0x5c = type (0 human, 2 computer), +0x60 = byte, +0x14 = party, +0x64 =
// race (0xff random), +0x68 = race given (byte), start position and three
// more values (+0x6c/+0x70 only if still -1, +0xa8..+0xb4).
using PlayerSetFn = void(__fastcall*)(GamePlayerSetup* self, void* edx, std::uint32_t type,
                                      std::uint32_t b, std::uint32_t party, std::uint32_t race,
                                      std::uint32_t raceGiven, std::uint32_t start,
                                      std::uint32_t a7, std::uint32_t a8, std::uint32_t a9);
using OperatorNewFn = void*(__cdecl*)(std::uint32_t size);
using MapFileCtorFn = void(__fastcall*)(CMapFile* self, void* edx, std::uint32_t mode);
using MapFileMemoryOnlyFn = void(__fastcall*)(CMapFile* self, void* edx);
using MapFileAddChunkFn = void(__fastcall*)(CMapFile* self, void* edx, std::uint32_t type,
                                            std::uint32_t subIndex, const void* data);

void __fastcall Hooked_GameSettings_NewGame(CGameSettings* self, void* edx, std::uint32_t a1,
                                            std::uint32_t a2, std::uint32_t a3,
                                            WCIntVector* playersPerParty,
                                            WCIntVector* computersPerParty, CMapFile* mapFile,
                                            void* a7, std::uint32_t a8, std::uint32_t startGoods,
                                            std::uint32_t a10, std::uint32_t a11,
                                            std::uint32_t a12, std::uint32_t a13);
void __fastcall Hooked_GamePlayerSetup_Set(GamePlayerSetup* self, void* edx, std::uint32_t type,
                                           std::uint32_t b, std::uint32_t party,
                                           std::uint32_t race, std::uint32_t raceGiven,
                                           std::uint32_t start, std::uint32_t a7,
                                           std::uint32_t a8, std::uint32_t a9);

S3RE_PASSTHROUGH(0x00431ab0, GameSettings_NewGame, NewGameFn, Original_GameSettings_NewGame,
                 Hooked_GameSettings_NewGame);
S3RE_PASSTHROUGH(0x0049f120, GamePlayerSetup_Set, PlayerSetFn, Original_GamePlayerSetup_Set,
                 Hooked_GamePlayerSetup_Set);

const auto GameOperatorNew = reinterpret_cast<OperatorNewFn>(0x0061efb9);
const auto GameOperatorDelete = reinterpret_cast<void(__cdecl*)(void*)>(0x0061e4ef);
const auto MapFile_Construct = reinterpret_cast<MapFileCtorFn>(0x004a5a70);
const auto MapFile_SetMemoryOnly = reinterpret_cast<MapFileMemoryOnlyFn>(0x004a60c0);
const auto MapFile_AddChunk = reinterpret_cast<MapFileAddChunkFn>(0x004a63f0);

// Return address of the NewGame call in Menus::CDrawThread::Update (the
// -map start).
constexpr std::uintptr_t kStartupMapCallReturn = 0x00493a56;
constexpr std::uint32_t kMapFileSize = 0x134;
constexpr std::uint32_t kHumanType = 0;  // GamePlayerSetup type of a human player
constexpr std::uint32_t kMaxStartPositions = 20;

#pragma pack(push, 1)
struct RandomMapChunk {  // chunk 5
    std::uint16_t type = 5;
    std::uint16_t subIndex = 0;
    std::uint32_t size = 0x18;
    std::uint32_t mapSize = 0;
    std::uint32_t field0c = 0;
    std::uint32_t symmetry = 0;
    std::uint32_t seed = 0;
};
struct MapInfoChunk {  // chunk 1
    std::uint16_t type = 1;
    std::uint16_t subIndex = 0;
    std::uint32_t size = 0x20;
    std::uint32_t kind = 2;  // random map
    std::uint32_t startPositions = 0;
    std::uint32_t unused[4] = {};
};
#pragma pack(pop)
static_assert(sizeof(RandomMapChunk) == 0x18);
static_assert(sizeof(MapInfoChunk) == 0x20);

// S3RE_RANDOM_MAP = "size,symmetry,field0c,seed,goods,race,computers,flags"
// (the launcher fills in all eight; goods 0 = the map's, race 0xff = random,
// computers 0 = the -map file's player setup; flags kSetupRandomMap, or
// kSetupMapOptions for options on the -map file's own map; without flags: a
// random map).
recording::GameSetup ReadParams() {
    recording::GameSetup params;
    char text[160] = {};
    const DWORD length = GetEnvironmentVariableA("S3RE_RANDOM_MAP", text, sizeof(text));
    if (length == 0 || length >= sizeof(text)) {
        return params;
    }
    std::uint32_t values[8] = {0, 0, 0, 0, 0, recording::kRandomRace, 0,
                               recording::kSetupRandomMap};
    const char* cursor = text;
    for (std::uint32_t& value : values) {
        char* end = nullptr;
        value = std::strtoul(cursor, &end, 0);
        if (*end != ',') {
            break;
        }
        cursor = end + 1;
    }
    params.flags = values[7] == recording::kSetupMapOptions ? recording::kSetupMapOptions
                                                            : recording::kSetupRandomMap;
    params.size = values[0];
    params.symmetry = values[1];
    params.field0c = values[2];
    params.mapSeed = values[3];
    params.goods = values[4];
    params.race = values[5];
    params.computers = values[6];
    return params;
}

// What the map dialog builds for a random map (0x4808d0, 0x483816, 0x481fa6).
CMapFile* MakeRandomMapFile(const recording::GameSetup& params) {
    void* memory = GameOperatorNew(kMapFileSize);
    if (memory == nullptr) {
        return nullptr;
    }
    std::memset(memory, 0, kMapFileSize);
    CMapFile* file = static_cast<CMapFile*>(memory);
    MapFile_Construct(file, nullptr, 0);
    MapFile_SetMemoryOnly(file, nullptr);

    RandomMapChunk random;
    random.mapSize = params.size;
    random.field0c = params.field0c;
    random.symmetry = params.symmetry;
    random.seed = params.mapSeed;
    MapFile_AddChunk(file, nullptr, 5, 0, &random);

    MapInfoChunk info;
    info.startPositions = StartPositions(params.size);
    MapFile_AddChunk(file, nullptr, 1, 0, &info);
    return file;
}

// Makes room for `count` entries: the vectors come from 0x4a67c0 sized
// for the -map file's parties. The buffer belongs to the vector, whose
// destructor (WCIntVector_Destroy 0x414590) frees it with the game's
// operator delete, so it is allocated with the game's operator new.
bool Reserve(WCIntVector* vector, std::uint32_t count) {
    if (vector->data != nullptr && vector->capacity >= count) {
        return true;
    }
    auto* data = static_cast<std::int32_t*>(GameOperatorNew(count * sizeof(std::int32_t)));
    if (data == nullptr) {
        return false;
    }
    std::memset(data, 0, count * sizeof(std::int32_t));
    if (vector->data != nullptr) {
        std::memcpy(data, vector->data,
                    (vector->count < count ? vector->count : count) * sizeof(std::int32_t));
        GameOperatorDelete(vector->data);
    }
    vector->data = data;
    vector->capacity = count;
    return true;
}

// Rewrites the two party vectors of the -map start to `computers` + 1
// parties of one player each: the human alone in party 0, a computer
// player in every other one. False if out of memory.
bool SetOnePlayerParties(WCIntVector* players, WCIntVector* computers,
                         std::uint32_t computerCount) {
    const std::uint32_t parties = computerCount + 1;
    S3RE_LOG("random-map: party vectors from the -map file: %u parties (capacity %u), "
             "%u (capacity %u)",
             players->count, players->capacity, computers->count, computers->capacity);
    if (!Reserve(players, parties) || !Reserve(computers, parties)) {
        return false;
    }
    for (std::uint32_t party = 0; party < parties; ++party) {
        players->data[party] = 1;
        computers->data[party] = party == 0 ? 0 : 1;
    }
    players->count = players->entries = parties;
    computers->count = computers->entries = parties;
    return true;
}

bool g_handled = false;  // only touched by the menu thread
// The race to give the human player while NewGame runs (menu thread only);
// kRandomRace: leave the records alone.
std::uint32_t g_humanRaceDuringNewGame = recording::kRandomRace;
// What was applied, published to the logic thread (the recorder).
recording::GameSetup g_applied;
std::atomic<bool> g_appliedReady{false};

void __fastcall Hooked_GamePlayerSetup_Set(GamePlayerSetup* self, void* edx, std::uint32_t type,
                                           std::uint32_t b, std::uint32_t party,
                                           std::uint32_t race, std::uint32_t raceGiven,
                                           std::uint32_t start, std::uint32_t a7,
                                           std::uint32_t a8, std::uint32_t a9) {
    if (g_humanRaceDuringNewGame != recording::kRandomRace && type == kHumanType) {
        S3RE_LOG("random-map: human player (party %u) gets race %u instead of %u", party,
                 g_humanRaceDuringNewGame, race);
        race = g_humanRaceDuringNewGame;
        raceGiven = 1;  // as NewGame passes (race != 0xff) for a chunk 2 race
    }
    Original_GamePlayerSetup_Set(self, edx, type, b, party, race, raceGiven, start, a7, a8, a9);
}

void __fastcall Hooked_GameSettings_NewGame(CGameSettings* self, void* edx, std::uint32_t a1,
                                            std::uint32_t a2, std::uint32_t a3,
                                            WCIntVector* playersPerParty,
                                            WCIntVector* computersPerParty, CMapFile* mapFile,
                                            void* a7, std::uint32_t a8, std::uint32_t startGoods,
                                            std::uint32_t a10, std::uint32_t a11,
                                            std::uint32_t a12, std::uint32_t a13) {
    bool applied = false;
    recording::GameSetup params;
    if (!g_handled &&
        reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == kStartupMapCallReturn) {
        g_handled = true;
        params = ReadParams();
        if (params.Applies()) {
            if (params.IsRandomMap()) {
                CMapFile* random = MakeRandomMapFile(params);
                if (random == nullptr) {
                    S3RE_LOG("random-map: out of memory; ending the game");
                    TerminateProcess(GetCurrentProcess(), 4);
                    return;
                }
                S3RE_LOG("random-map: size %u, symmetry %u, field0c %u, seed %u", params.size,
                         params.symmetry, params.field0c, params.mapSeed);
                // The -map file stays allocated (the menu thread still reads its
                // chunk 0x40 after this call); it is not freed, a small leak.
                mapFile = random;
            } else {
                S3RE_LOG("map-options: the -map file's own map with the options below");
            }
            if (params.goods != 0) {
                startGoods = params.goods;
            }
            if (params.computers != 0) {
                if (!SetOnePlayerParties(playersPerParty, computersPerParty,
                                         params.computers)) {
                    S3RE_LOG("random-map: can't set up %u computer players (out of "
                             "memory); ending the game",
                             params.computers);
                    TerminateProcess(GetCurrentProcess(), 4);
                    return;
                }
            }
            g_humanRaceDuringNewGame = params.race;
            S3RE_LOG("random-map: start goods %u (0 = the map's, 1 low, 2 medium, 3 high), "
                     "human race %u (255 = random), %u computer players (0 = players from "
                     "the -map file)",
                     startGoods, params.race, params.computers);
            applied = true;
        }
    }
    Original_GameSettings_NewGame(self, edx, a1, a2, a3, playersPerParty, computersPerParty,
                                  mapFile, a7, a8, startGoods, a10, a11, a12, a13);
    g_humanRaceDuringNewGame = recording::kRandomRace;
    if (applied) {
        g_applied = params;
        g_appliedReady.store(true, std::memory_order_release);
    }
}

}  // namespace

std::uint32_t StartPositions(std::uint32_t size) {
    const std::uint32_t distance = size - 0x20;
    const std::uint32_t positions = ((distance * distance) >> 14) + 1;
    return positions < kMaxStartPositions ? positions : kMaxStartPositions;
}

recording::GameSetup AppliedGameSetup() {
    return g_appliedReady.load(std::memory_order_acquire) ? g_applied : recording::GameSetup{};
}

}  // namespace s3re::tooling
