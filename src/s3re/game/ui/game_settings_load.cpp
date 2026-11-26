// Loading a savegame's setup into the game settings (Dirk\GameParams.cpp):
// GameSettings::LoadSaveGame, the first step of loading a game (the load
// dialogs call it; CLogicThread::OnStart then loads the world from the same
// file, LoadSaveGame in game/logic/). Layout in game_settings_layout.h,
// chunks in fileformats/savegame_chunks.h (written by GameSettings::SaveSetup
// 0x433490, game_settings_players.cpp); tests in game_settings_load_tests.cpp.
//
// GameSettings::LoadSaveGame (0x00432b00): (const std::string* path, bool
//   multiplayer) ret 8, thiscall on g_gameSettings. EAX 0 loaded, 1 the
//   file or a needed chunk is missing, 2 the savegame needs the Amazons and
//   they are not installed.
//   1. +0x28 = 1 (loading a savegame); +0x84 = a new Dirk::CMapFile
//      (operator new 0x134, constructor 0x4a5a70 with 0; null: ThrowOutOfMemory
//      "lpMapFile", GameParams.cpp line 442), opened for reading (0x4a5c80);
//      GetError < 0: result 1.
//   2. +0x78 = -1, +0x29 = multiplayer, +0x7c = 1, +0x2a = +0x2b = 0; the
//      team, player and net-id vectors emptied (resize(0) 0x437d00,
//      0x437820, 0x429a00; each length 0), +0xac = -1.
//   3. Chunk 0 (the header; missing: result 1): the map name +0x2c = its
//      text +0x20 (through a local string: assign 0x4021c0, assign(x, 0,
//      npos) 0x401d60); released. Chunks 0x14 (players; missing: result 1),
//      0x1a, 0x1b, 0x1e are fetched (GetChunk 0x4a60f0).
//   4. Chunk 0x1e with +8 set while GameConfig +0x158 (getter 0x413bd0) is
//      0: result 2.
//   5. 0x1a: +0xac = +8, the GUID +0xb0 = +0xc, +0xc0 = +0x1c (DebugLog "CFG:
//      Counter %i"), word +0xc4 = +0x20; without it +0xac = 0 and a new GUID
//      (CoCreateGuid; a failure throws through ThrowHResult "error check",
//      line 495, with a copy of "" as the third argument). 0x1b: +0xc8 = +8,
//      +0x74 = +0xc, else 0. 0x1e: bytes +0xcc/+0xcd = +8/+9, else 0.
//   6. For each of chunk 0x14's `count` records: a default GamePlayerSetup
//      (0x49e8f0) appended (InsertAt 0x4379e0 at the length) and destroyed.
//      Then per record (copied to a local first; bytes +4 net id, +5 slot,
//      +6 party, +7 type, +8 race, +0xa colour, dwords +0xc..+0x14):
//      teams appended while the party is not below the team count (a
//      CTeam_id(count) 0x4a02d0 inserted at the position of the vector's
//      virtual find, slot 3 mode 3, or 0 for an empty vector; InsertAt
//      0x438090; ~CTeam), the net id appended to +0x90 (InsertAt 0x428d10),
//      AddPlayerId(slot) 0x4a06d0 on the party's team, GamePlayerSetup::Set
//      0x49f120 on the slot's setup with (type 1 -> 0, 3 -> 2, else as saved;
//      type == 2; party; race; 1; the three dwords; colour), LogSlot 0x4a01c0.
//   7. The chunks released; in single player SetupLocalPlayer(0, 0) 0x433e50.
//   On a result 1 or 2 the file is closed (0x4a5f40), deleted (vtable slot
//   0 with 1) and +0x84 = 0. Last, on every path, DebugLog("NET: Mapping %i
//   to %i", id, i) for each entry of +0x90.
//   The vector accesses are the Watcom index checks (game_settings_vectors.h).
//
// Status of LoadSaveGame: in-process test on every savegame and synthetic
// ones passes; its emulator spec doesn't pass yet, so S3RE_HOOK_UNVERIFIED.
//
// GameConfig::RandomRaceAmazons (0x00413bd0; on g_config 0x7a7af0): () -> AL
//   = +0x158 (whether the Amazons are installed; the -map start passes it as
//   NewGame's argument 11).
//
// GameSettings::IsHost (0x004318a0) and GameSettings::IsMultiplayer
//   (0x004318d0): () -> AL = byte +0x7c / +0x29, read under the settings'
//   lock (Win32::CLock, the game's functions called). The rest of EAX is
//   whatever the lock's destructor left (callers test AL only).
//
// Known differences:
//  - The local strings' allocator bytes are uninitialized stack bytes of
//    the original's frame; ours are 0 (the strings are freed before the
//    function returns, docs/reimplementation_notes.md case b).
//  - The GUID failure path's string is passed to ThrowHResult like the
//    original does; the string is not freed when ThrowHResult throws (the
//    original's unwind would free it; it holds no buffer).

#include "game/debug_log.h"
#include "game/fileformats/map_file.h"
#include "game/fileformats/savegame_chunks.h"
#include "game/framework/critical_section.h"
#include "game/globals.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/ui/game_settings_layout.h"
#include "game/ui/game_settings_vectors.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "watcom/ptr_vector.h"
#include "watcom/vector_base.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

using fileformats::CMapFile;
using fileformats::SaveChunk;

namespace {

using watcom::VectorBase;

constexpr std::uint32_t kLoaded = 0;
constexpr std::uint32_t kUnreadable = 1;
constexpr std::uint32_t kNeedsAmazons = 2;

constexpr std::uint32_t kMapFileSize = sizeof(CMapFile);
constexpr std::uint32_t kOutOfMemoryLine = 442;  // 0x1ba
constexpr std::uint32_t kGuidErrorLine = 495;    // 0x1ef
constexpr std::uint32_t kNoValue = 0xffffffff;

// chunk 0x14 record types and the setup types they become
constexpr std::uint8_t kSavedLocalPlayer = 1;  // -> 0 (human)
constexpr std::uint8_t kSavedType3 = 3;        // -> 2 (computer)
constexpr std::uint32_t kHuman = 0;
constexpr std::uint32_t kComputer = 2;
constexpr std::uint32_t kFindInsertPosition = 3;

constexpr std::uintptr_t kMapFileConstruct = 0x004a5a70;
constexpr std::uintptr_t kMapFileOpen = 0x004a5c80;
constexpr std::uintptr_t kMapFileGetError = 0x004a60d0;
constexpr std::uintptr_t kGetChunk = 0x004a60f0;
constexpr std::uintptr_t kReleaseChunk = 0x004a6310;
constexpr std::uintptr_t kMapFileClose = 0x004a5f40;
constexpr std::uintptr_t kThrowOutOfMemory = 0x00610960;  // Win32::ThrowOutOfMemory (never returns)
constexpr std::uintptr_t kThrowHResult = 0x00611050;      // Win32::ThrowHResult (never returns)
constexpr std::uintptr_t kTeamResize = 0x00437d00;
constexpr std::uintptr_t kPlayerResize = 0x00437820;
constexpr std::uintptr_t kIntResize = 0x00429a00;
constexpr std::uintptr_t kStringTidy = 0x00402170;
constexpr std::uintptr_t kStringAssignChars = 0x004021c0;
constexpr std::uintptr_t kStringAssignSubstr = 0x00401d60;
constexpr std::uintptr_t kAmazonsInstalled = 0x00413bd0;
constexpr std::uintptr_t kSetupConstruct = 0x0049e8f0;
constexpr std::uintptr_t kSetupDestroy = 0x0049ec80;
constexpr std::uintptr_t kPlayerInsertAt = 0x004379e0;
constexpr std::uintptr_t kTeamConstructId = 0x004a02d0;
constexpr std::uintptr_t kTeamInsertAt = 0x00438090;
constexpr std::uintptr_t kTeamDestroy = 0x004a04d0;
constexpr std::uintptr_t kIntInsertAt = 0x00428d10;
constexpr std::uintptr_t kAddPlayerId = 0x004a06d0;
constexpr std::uintptr_t kSetupSet = 0x0049f120;
constexpr std::uintptr_t kLogSlot = 0x004a01c0;
constexpr std::uintptr_t kSetupLocalPlayer = 0x00433e50;

template <typename R = std::uint32_t, typename... Args>
R CallThis(std::uintptr_t address, const void* self, Args... args) {
    using Fn = R(__fastcall*)(const void*, void*, decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(address)(self, nullptr, static_cast<std::uint32_t>(args)...);
}

template <typename T>
std::uint32_t AddressOf(const T* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
}

const std::uint8_t* GetChunk(CMapFile* file, SaveChunk type) {
    return CallThis<const std::uint8_t*>(kGetChunk, file, static_cast<std::uint32_t>(type), 0);
}

void ReleaseChunk(CMapFile* file, SaveChunk type) {
    CallThis<void>(kReleaseChunk, file, static_cast<std::uint32_t>(type), 0);
}

template <typename T>
T Read(const std::uint8_t* chunk, std::uint32_t offset) {
    T value;
    std::memcpy(&value, chunk + offset, sizeof(value));
    return value;
}

// A std::string local of the original's frame (the allocator byte is an
// uninitialized stack byte there; 0 here, the string is freed again).
struct LocalString {
    msvc6::String text{};
    LocalString() { CallThis<void>(kStringTidy, &text, 0); }
    void Free() { CallThis<void>(kStringTidy, &text, 1); }
};

// Step 3: the map name from the header's text (the null-pointer branch
// 0x432c8b is dead: the text is inside the chunk).
void TakeMapName(GameSettings* self, const char* name) {
    LocalString local;
    CallThis(kStringAssignChars, &local.text, AddressOf(name), static_cast<std::uint32_t>(std::strlen(name)));
    CallThis(kStringAssignSubstr, &self->map_name, AddressOf(&local.text), 0, static_cast<std::uint32_t>(g_stringNpos));
    local.Free();
}

// Step 5 without chunk 0x1a: a new GUID; a failure throws (0x432e36..0x432f37).
void CreateGameGuid(GameSettings* self) {
    if (g_importCoCreateGuid.get()(&self->game_guid) >= 0) {
        return;
    }
    LocalString error;
    {
        LocalString empty;
        CallThis(kStringAssignChars, &empty.text, AddressOf(texts::kEmpty.c_str()),
                 static_cast<std::uint32_t>(std::strlen(texts::kEmpty)));
        LocalString copy;
        CallThis(kStringAssignSubstr, &copy.text, AddressOf(&empty.text), 0, kNoValue);
        CallThis(kStringAssignSubstr, &error.text, AddressOf(&copy.text), 0, static_cast<std::uint32_t>(g_stringNpos));
        copy.Free();
        empty.Free();
    }
    const std::int32_t result = g_importCoCreateGuid.get()(&self->game_guid);
    using ThrowFn = void(__fastcall*)(const char* text, const char* file, std::uint32_t line, std::int32_t result,
                                      const msvc6::String* message);
    reinterpret_cast<ThrowFn>(kThrowHResult)(texts::kErrorCheck.c_str(), texts::kGameParamsFile.c_str(),
                                             kGuidErrorLine, result, &error.text);
}

// Step 5: chunks 0x1a, 0x1b and 0x1e.
void TakeGameValues(GameSettings* self, const std::uint8_t* game, const std::uint8_t* options,
                    const std::uint8_t* objectives) {
    if (game == nullptr) {
        self->local_net_id = 0;
        CreateGameGuid(self);
    } else {
        const auto* chunk = reinterpret_cast<const fileformats::SaveSetup1AChunk*>(game);
        self->local_net_id = chunk->local_player_id;
        for (int i = 0; i < 4; ++i) {
            self->game_guid[i] = chunk->game_guid[i];
        }
        self->saved_tick = chunk->tick;
        self->saved_seed = chunk->seed;
        GameDebugLog(texts::kCfgCounter, chunk->tick);
    }
    if (options != nullptr) {
        const auto* chunk = reinterpret_cast<const fileformats::SaveSetup1BChunk*>(options);
        self->valueC8 = chunk->valueC8;
        self->start_goods = chunk->start_goods;
    } else {
        self->valueC8 = 0;
        self->start_goods = 0;
    }
    if (objectives != nullptr) {
        const auto* chunk = reinterpret_cast<const fileformats::SaveSetup1EChunk*>(objectives);
        self->randomRaceAmazons = chunk->random_race_amazons;
        self->goods_objective = chunk->goods_objective;
    } else {
        self->randomRaceAmazons = 0;
        self->goods_objective = 0;
    }
}

GamePlayerSetup* SetupAt(VectorBase* players, std::uint32_t slot) {
    const std::int32_t i = PlayerVectorIndex(players, static_cast<std::int32_t>(slot));
    return reinterpret_cast<GamePlayerSetup*>(
        static_cast<std::uintptr_t>(players->data + static_cast<std::uint32_t>(i) * sizeof(GamePlayerSetup)));
}

// Step 6, one record of chunk 0x14.
void TakePlayerRecord(GameSettings* self, const std::uint8_t* stored) {
    std::uint8_t record[sizeof(fileformats::SavedPlayerSetup)];
    std::memcpy(record, stored, sizeof(record));
    const std::uint32_t netId = record[4];
    const std::uint32_t slot = record[5];
    const std::uint32_t party = record[6];
    const std::uint8_t savedType = record[7];
    VectorBase* teams = &self->teams;
    while (party >= teams->length) {
        CTeam team;
        CallThis(kTeamConstructId, &team, teams->length);
        std::int32_t position = 0;
        if (teams->length != 0) {
            using FindFn = void(__fastcall*)(VectorBase*, void*, const CTeam*, std::uint32_t, std::int32_t*);
            reinterpret_cast<const FindFn*>(static_cast<std::uintptr_t>(teams->vtable))[3](teams, nullptr, &team,
                                                                                            kFindInsertPosition, &position);
        }
        CallThis(kTeamInsertAt, teams, position, AddressOf(&team));
        CallThis(kTeamDestroy, &team);
    }
    std::uint32_t id = netId;
    CallThis(kIntInsertAt, &self->net_ids, self->net_ids.length, AddressOf(&id));
    const std::int32_t teamIndex = TeamVectorIndex(teams, static_cast<std::int32_t>(party));
    const auto* team = reinterpret_cast<const CTeam*>(
        static_cast<std::uintptr_t>(teams->data + static_cast<std::uint32_t>(teamIndex) * sizeof(CTeam)));
    CallThis(kAddPlayerId, team, slot);
    std::uint32_t type = savedType;
    if (savedType == kSavedLocalPlayer) {
        type = kHuman;
    } else if (savedType == kSavedType3) {
        type = kComputer;
    }
    VectorBase* players = reinterpret_cast<VectorBase*>(&self->player_setups);
    GamePlayerSetup* setup = SetupAt(players, slot);
    CallThis(kSetupSet, setup, type, type == kComputer ? 1 : 0, party, record[8], 1, Read<std::uint32_t>(record, 0xc),
             Read<std::uint32_t>(record, 0x10), Read<std::uint32_t>(record, 0x14), record[10]);
    CallThis(kLogSlot, SetupAt(players, slot));
}

// Step 6.
void TakePlayers(GameSettings* self, const std::uint8_t* players) {
    const auto* chunk = reinterpret_cast<const fileformats::SaveSetupPlayersChunk*>(players);
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(chunk->count); ++i) {
        GamePlayerSetup setup;
        CallThis(kSetupConstruct, &setup);
        CallThis(kPlayerInsertAt, &self->player_setups, self->player_setups.length, AddressOf(&setup));
        CallThis(kSetupDestroy, &setup);
    }
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(chunk->count); ++i) {
        TakePlayerRecord(self, players + offsetof(fileformats::SaveSetupPlayersChunk, players) +
                                   static_cast<std::uint32_t>(i) * sizeof(fileformats::SavedPlayerSetup));
    }
}

// The loading proper (steps 2..7); the result.
std::uint32_t ReadSetup(GameSettings* self, std::uint32_t multiplayer) {
    CMapFile* file = self->map_file;
    self->local_player_id = kNoValue;
    self->multiplayer = static_cast<std::uint8_t>(multiplayer);
    self->is_host = 1;
    self->option2a = 0;
    self->by_clan = 0;
    CallThis(kTeamResize, &self->teams, 0);
    self->teams.length = 0;
    CallThis(kPlayerResize, &self->player_setups, 0);
    self->player_setups.length = 0;
    CallThis(kIntResize, &self->net_ids, 0);
    self->net_ids.length = 0;
    self->local_net_id = kNoValue;

    const std::uint8_t* header = GetChunk(file, SaveChunk::Header);
    if (header == nullptr) {
        return kUnreadable;
    }
    TakeMapName(self, reinterpret_cast<const char*>(header + offsetof(fileformats::SaveHeaderChunk, map_name)));
    ReleaseChunk(file, SaveChunk::Header);
    const std::uint8_t* players = GetChunk(file, SaveChunk::SetupPlayers);
    const std::uint8_t* game = GetChunk(file, SaveChunk::Setup1A);
    const std::uint8_t* options = GetChunk(file, SaveChunk::Setup1B);
    const std::uint8_t* objectives = GetChunk(file, SaveChunk::Setup1E);
    if (players == nullptr) {
        return kUnreadable;
    }
    if (objectives != nullptr && objectives[8] != 0 && (CallThis(kAmazonsInstalled, &g_config.get()) & 0xff) == 0) {
        return kNeedsAmazons;
    }
    TakeGameValues(self, game, options, objectives);
    TakePlayers(self, players);
    ReleaseChunk(file, SaveChunk::SetupPlayers);
    if (game != nullptr) {
        ReleaseChunk(file, SaveChunk::Setup1A);
    }
    if (options != nullptr) {
        ReleaseChunk(file, SaveChunk::Setup1B);
    }
    if (objectives != nullptr) {
        ReleaseChunk(file, SaveChunk::Setup1E);
    }
    if (self->multiplayer == 0) {
        CallThis(kSetupLocalPlayer, self, 0, 0);
    }
    return kLoaded;
}

}  // namespace

using LoadSaveGameFn = std::uint32_t(__fastcall*)(GameSettings* self, void* edx, const msvc6::String* path,
                                                  std::uint32_t multiplayer);
using ConfigFlagFn = std::uint32_t(__fastcall*)(const GameConfig* self, void* edx);

std::uint32_t __fastcall GameSettings_LoadSaveGame(GameSettings* self, void* edx, const msvc6::String* path,
                                                   std::uint32_t multiplayer);
// GameConfig::RandomRaceAmazons 0x413bd0 is hooked in framework/game_config.cpp.
std::uint32_t __fastcall GameConfig_RandomRaceAmazons(const GameConfig* self, void* edx);
using SettingsFlagFn = std::uint32_t(__fastcall*)(GameSettings* self, void* edx);
std::uint32_t __fastcall GameSettings_IsHost(GameSettings* self, void* edx);
std::uint32_t __fastcall GameSettings_IsMultiplayer(GameSettings* self, void* edx);

S3RE_HOOK_UNVERIFIED(0x00432b00, GameSettings_LoadSaveGame, LoadSaveGameFn, Original_GameSettings_LoadSaveGame,
          GameSettings_LoadSaveGame);
S3RE_HOOK(0x004318a0, GameSettings_IsHost, SettingsFlagFn, Original_GameSettings_IsHost, GameSettings_IsHost);
S3RE_HOOK(0x004318d0, GameSettings_IsMultiplayer, SettingsFlagFn, Original_GameSettings_IsMultiplayer,
          GameSettings_IsMultiplayer);

// Purpose: The first step of loading a savegame: opens the file and takes
// the game's setup from it (map name, players, teams, network ids, options),
// so that the game can then be started from it.
std::uint32_t __fastcall GameSettings_LoadSaveGame(GameSettings* self, void*, const msvc6::String* path,
                                                   std::uint32_t multiplayer) {
    self->is_savegame = 1;
    void* block = msvc6::GameOperatorNew(kMapFileSize);
    CMapFile* file = block != nullptr ? CallThis<CMapFile*>(kMapFileConstruct, block, 0) : nullptr;
    self->map_file = file;
    if (file == nullptr) {
        using ThrowFn = void(__fastcall*)(const char* text, const char* file, std::uint32_t line);
        reinterpret_cast<ThrowFn>(kThrowOutOfMemory)(texts::kLpMapFile.c_str(), texts::kGameParamsFile.c_str(),
                                                     kOutOfMemoryLine);
    }
    CallThis<void>(kMapFileOpen, self->map_file, AddressOf(path), fileformats::kOpenRead);
    std::uint32_t result = kUnreadable;
    if (CallThis<std::int32_t>(kMapFileGetError, self->map_file) >= 0) {
        result = ReadSetup(self, multiplayer);
    }
    if (result != kLoaded) {
        CallThis<void>(kMapFileClose, self->map_file);
        if (CMapFile* failed = self->map_file; failed != nullptr) {
            using DeleteFn = void*(__fastcall*)(CMapFile*, void*, std::uint32_t);
            reinterpret_cast<const DeleteFn*>(static_cast<std::uintptr_t>(failed->vtable))[0](failed, nullptr, 1);
        }
        self->map_file = nullptr;
    }
    for (std::uint32_t i = 0; i < self->net_ids.length; ++i) {
        const std::uint32_t id = watcom::PtrVector_At(reinterpret_cast<watcom::PtrVector*>(&self->net_ids),
                                                      static_cast<std::int32_t>(i));
        GameDebugLog(texts::kNetMapping, id, i);
    }
    return result;
}

// Purpose: Whether this computer hosts the game being set up (savegames
// loaded here are always hosted).
std::uint32_t __fastcall GameSettings_IsHost(GameSettings* self, void*) {
    GameLock lock(self->lock);
    return self->is_host;
}

// Purpose: Whether the game being set up is a multiplayer game (single
// player allows F12 fast-forward and names the local player "you").
std::uint32_t __fastcall GameSettings_IsMultiplayer(GameSettings* self, void*) {
    GameLock lock(self->lock);
    return self->multiplayer;
}

}  // namespace s3re::game
