// The player setups of the game settings (g_gameSettings, Dirk\GameParams.cpp):
// element access of the player-setup vector and saving the setup into a
// savegame. Layouts in game_settings_layout.h, chunks in
// fileformats/savegame_chunks.h; tests in game_settings_players_tests.cpp.
//
// GameSettings::PlayerSetup (0x004351e0): (index) ret 4 -> GamePlayerSetup*,
//   thiscall. Holds the settings' lock (Win32::CLock on +0, the game's
//   CLock functions are called) around the vector access below. Callers:
//   CGameData::LoadMap (the colours), CLogicThread::OnStart, the game
//   launch dialog and the lobby.
// GameSettings::SaveSetup (0x00433490): (CMapFile* file, tick, seed) ret 0xc,
//   thiscall; called by CLogicThread::SaveGame only. No lock. A local
//   GamePlayerSetup (constructor 0x49e8f0) gets each element of the vector in
//   turn (operator= 0x49edf0) and the saved values are read from it (the
//   getters 0x49f4d0, 0x49f590, 0x49f350, 0x49f510, 0x49f580, 0x49fed0,
//   inlined here); the local is destroyed at the end (0x49ec80). It writes
//   the chunks 0x14 (the players), 0x1a, 0x1b and 0x1e (WriteChunk
//   0x4a6540, sub index 0) and logs "CFG: Saving local player id %i"
//   between 0x14 and 0x1a.
//
// The vector access (inlined in both, the Watcom library's operator[]):
// an index in 0..length-1 (signed) is used as it is; on a non-empty
// vector an index below 0 becomes 0 and one above becomes length-1. On an
// empty vector element 0 is made: if the capacity is 0, a block of one
// element is allocated (operator new 0xcc), the first min(filled, 1)
// elements (signed) are copy-constructed into it (0x49eaf0), all `filled`
// old elements destroyed (0x49ec80), the old block freed, filled =
// min(filled, 1), capacity 1; if operator new fails, element 0 of the old
// block is returned. Then (also with a capacity) vtable slot 1 is called
// with 0 and the length set to 1. The library's error hooks the original
// calls on the way are the empty function 0x59f200 (not called here).
//
// GamePlayerSetup::GamePlayerSetup (0x0049e8f0): () thiscall, EAX = this:
//   +0x00, +0x04, +0x08 = 0; +0x10, +0x14 = -1; empty strings at +0x18 and
//   +0xbc and an empty 16-bit string at +0x28; the vector at +0x38
//   (WCBareVectorBase<unsigned short>(10) 0x462df0, then length +0x4c = 0,
//   +0x50 = 5, vtable WCValOrderedVector<unsigned short>); bytes +0x54,
//   +0x60, +0x68 = 0; +0x58, +0x5c, +0x74, +0x78, +0xa4, +0xb8 = 0; +0x64 =
//   0xff (random race); +0x6c, +0x70, +0xa8..+0xb4 = -1. Not written:
//   +0x0c..+0x0f and +0x7c..+0xa3.
//
// Known differences:
//  - The three strings' allocator bytes (+0x18, +0x28, +0xbc) are one
//    uninitialized local byte of the original constructor's frame; ours
//    writes 0 (docs/reimplementation_notes.md, case b; the setups live on).
//  - SaveSetup writes 0 into the bytes of its chunks the original leaves
//    as stack leftovers (chunk 0x14 byte +0xb of each record and the
//    records past the count, chunk 0x1a +0x22..+0x23).
//  - With more than 20 player setups the original writes records past its
//    20-record stack buffer (over its own frame); ours copies every element
//    into the local as well but saves only the first 20. The game has at
//    most 20 players.
//  - An exception thrown by a copy constructor during the growth (only out
//    of memory) is not unwound like the original's frame does (it frees the
//    new block).

#include "game/debug_log.h"
#include "game/fileformats/map_file.h"
#include "game/fileformats/savegame_chunks.h"
#include "game/framework/critical_section.h"
#include "game/texts.h"
#include "game/ui/game_settings_layout.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace s3re::game {

using fileformats::CMapFile;
using fileformats::SaveChunk;

namespace {

// GamePlayerSetup's members the vector code calls (they stay the original,
// except the destructor, framework_helpers.cpp).
using SetupConstructFn = GamePlayerSetup*(__fastcall*)(GamePlayerSetup* self, void* edx);
using SetupCopyFn = GamePlayerSetup*(__fastcall*)(GamePlayerSetup* self, void* edx, const GamePlayerSetup* from);
using SetupDestroyFn = void(__fastcall*)(GamePlayerSetup* self, void* edx);
using VectorNotifyFn = void(__fastcall*)(PlayerSetupVector* self, void* edx, std::uint32_t argument);
using WriteChunkFn = void(__fastcall*)(CMapFile* self, void* edx, std::uint32_t type, std::uint32_t subIndex,
                                       const void* chunk);
constexpr std::uintptr_t kSetupConstruct = 0x0049e8f0;    // GamePlayerSetup::GamePlayerSetup
constexpr std::uintptr_t kSetupCopyConstruct = 0x0049eaf0;  // GamePlayerSetup::GamePlayerSetup(const&)
constexpr std::uintptr_t kSetupAssign = 0x0049edf0;       // GamePlayerSetup::operator=
constexpr std::uintptr_t kSetupDestroy = 0x0049ec80;      // GamePlayerSetup::~GamePlayerSetup (ours)
constexpr std::uintptr_t kWriteChunk = 0x004a6540;        // Dirk::CMapFile::WriteChunk (verified)

// The destructor through its S3.EXE address (our hook in the game; the
// emulator tests probe the address).
void DestroySetup(GamePlayerSetup* setup) {
    reinterpret_cast<SetupDestroyFn>(kSetupDestroy)(setup, nullptr);
}

GamePlayerSetup* ElementAt(GamePlayerSetup* data, std::int32_t index) {
    return reinterpret_cast<GamePlayerSetup*>(reinterpret_cast<std::uintptr_t>(data) +
                                              static_cast<std::uint32_t>(index) * sizeof(GamePlayerSetup));
}

// The growth of an empty vector without capacity to one element
// (0x435231..0x43530d); false if operator new failed (nothing changed).
bool GrowToOneElement(PlayerSetupVector* vector) {
    const std::int32_t keep = vector->filled < 1 ? vector->filled : 1;
    auto* block = static_cast<GamePlayerSetup*>(msvc6::GameOperatorNew(sizeof(GamePlayerSetup)));
    if (block == nullptr) {
        return false;
    }
    for (std::int32_t i = 0; i < keep; ++i) {
        GamePlayerSetup* to = ElementAt(block, i);
        if (to != nullptr) {  // (placement new's check)
            reinterpret_cast<SetupCopyFn>(kSetupCopyConstruct)(to, nullptr, ElementAt(vector->data, i));
        }
    }
    for (std::int32_t i = 0; i < vector->filled; ++i) {
        DestroySetup(ElementAt(vector->data, i));
    }
    msvc6::GameOperatorDelete(vector->data);
    vector->filled = keep;
    vector->data = block;
    vector->capacity = 1;
    return true;
}

// Vtable slot 1, called with 0 when the length changes from 0 to 1.
void NotifyLengthChanged(PlayerSetupVector* vector) {
    const auto* vtable = reinterpret_cast<const VectorNotifyFn*>(vector->vtable);
    vtable[1](vector, nullptr, 0);
}

// The inlined operator[] (see the header comment).
GamePlayerSetup* PlayerSetupAt(PlayerSetupVector* vector, std::int32_t index) {
    const std::uint32_t length = vector->length;
    const std::int32_t last = static_cast<std::int32_t>(length - 1);
    if (index >= 0 && index <= last) {
        return ElementAt(vector->data, index);
    }
    if (length != 0) {
        return ElementAt(vector->data, index < 0 ? 0 : last);
    }
    if (vector->capacity == 0) {
        if (!GrowToOneElement(vector)) {
            return ElementAt(vector->data, 0);
        }
        if (vector->length > 1) {  // (0x435313; the length is 0 here, so never)
            vector->length = 1;
        }
    }
    NotifyLengthChanged(vector);
    vector->length = 1;
    return ElementAt(vector->data, 0);
}

// One record of chunk 0x14 from the local copy of a player setup
// (0x433673..0x4336c8).
void FillSavedPlayer(fileformats::SavedPlayerSetup& record, const GamePlayerSetup& setup, std::uint32_t index) {
    record.zero = 0;
    record.value10 = static_cast<std::uint8_t>(setup.value10);
    record.index = static_cast<std::uint8_t>(index);
    record.party = static_cast<std::uint8_t>(setup.party);
    record.type = static_cast<std::uint8_t>(setup.type);
    record.race = static_cast<std::uint8_t>(setup.race);
    record.race_byte = setup.race_byte;
    record.zero_a = 0;
    for (std::uint32_t k = 0; k < 3; ++k) {
        record.saved_values[k] = setup.saved_values[k];
    }
}

template <typename Chunk>
void InitChunkHeader(Chunk& chunk, SaveChunk type) {
    chunk.header.key = static_cast<std::uint32_t>(type);  // sub index 0
    chunk.header.size = sizeof(Chunk);
}

void WriteChunk(CMapFile* file, SaveChunk type, const void* chunk) {
    reinterpret_cast<WriteChunkFn>(kWriteChunk)(file, nullptr, static_cast<std::uint32_t>(type), 0, chunk);
}

}  // namespace

using SetupConstructHookFn = GamePlayerSetup*(__fastcall*)(GamePlayerSetup* self, void* edx);
GamePlayerSetup* __fastcall GamePlayerSetup_Construct(GamePlayerSetup* self, void* edx);
S3RE_HOOK(0x0049e8f0, GamePlayerSetup_Construct, SetupConstructHookFn, Original_GamePlayerSetup_Construct,
          GamePlayerSetup_Construct);

namespace {

using UShortVectorConstructFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t n);
constexpr std::uintptr_t kUShortVectorConstruct = 0x00462df0;  // WCBareVectorBase<unsigned short> (ours)
constexpr std::uint32_t kSetupVectorCapacity = 10;
constexpr std::uint32_t kSetupVectorField18 = 5;  // +0x18 of the vector (probably its growth step)
constexpr std::uint32_t kRandomRace = 0xff;
constexpr std::uint32_t kUnset = 0xffffffff;

// An empty string: allocator byte 0 (see above), no text; the three
// padding bytes after the allocator byte are left alone, as in the original.
void EmptyString(void* string) {
    auto* s = static_cast<msvc6::String*>(string);
    s->allocator = 0;
    s->ptr = nullptr;
    s->len = 0;
    s->res = 0;
}

}  // namespace

// Purpose: Sets up an empty player setup record (no strings, random race,
// no start position).
GamePlayerSetup* __fastcall GamePlayerSetup_Construct(GamePlayerSetup* self, void* /*edx*/) {
    auto* bytes = reinterpret_cast<std::uint8_t*>(self);
    auto dword = [bytes](std::size_t offset, std::uint32_t value) {
        std::memcpy(bytes + offset, &value, sizeof(value));  // raw-ok: unnamed fields of the record
    };
    dword(0x00, 0);
    dword(0x04, 0);
    dword(0x08, 0);
    self->value10 = kUnset;
    self->party = kUnset;
    EmptyString(&self->text18);
    EmptyString(bytes + 0x28);  // the 16-bit string (same layout)
    reinterpret_cast<UShortVectorConstructFn>(kUShortVectorConstruct)(bytes + 0x38, nullptr, kSetupVectorCapacity);
    dword(0x38 + 0x14, 0);
    dword(0x38 + 0x18, kSetupVectorField18);
    dword(0x38, vtables::kWCValOrderedVector_ushort);
    bytes[0x54] = 0;
    dword(0x58, 0);
    self->type = 0;
    bytes[0x60] = 0;
    self->race = kRandomRace;
    self->race_byte = 0;
    dword(0x6c, kUnset);
    dword(0x70, kUnset);
    dword(0x74, 0);
    dword(0x78, 0);
    dword(0xa4, 0);
    self->saved_values[0] = kUnset;
    self->saved_values[1] = kUnset;
    self->saved_values[2] = kUnset;
    self->colour = kUnset;
    dword(0xb8, 0);
    EmptyString(&self->textBC);
    return self;
}

// For the emulator's self-test and the wrong-implementation check: also
// writes the padding byte after the allocator byte at +0x18 (which the
// comparisons leave out).
GamePlayerSetup* __fastcall WrongGamePlayerSetupConstruct(GamePlayerSetup* self, void* edx) {
    GamePlayerSetup_Construct(self, edx);
    reinterpret_cast<std::uint8_t*>(self)[0x19] = 0x5a;  // raw-ok: deliberately wrong
    return self;
}

using PlayerSetupFn = GamePlayerSetup*(__fastcall*)(GameSettings* self, void* edx, std::int32_t index);
using SaveSetupFn = void(__fastcall*)(GameSettings* self, void* edx, CMapFile* file, std::uint32_t tick,
                                      std::uint32_t seed);
GamePlayerSetup* __fastcall GameSettings_PlayerSetup(GameSettings* self, void* edx, std::int32_t index);
void __fastcall GameSettings_SaveSetup(GameSettings* self, void* edx, CMapFile* file, std::uint32_t tick,
                                       std::uint32_t seed);

S3RE_HOOK(0x004351e0, GameSettings_PlayerSetup, PlayerSetupFn, Original_GameSettings_PlayerSetup,
          GameSettings_PlayerSetup);
S3RE_HOOK(0x00433490, GameSettings_SaveSetup, SaveSetupFn, Original_GameSettings_SaveSetup, GameSettings_SaveSetup);

// Purpose: Gives one player's setup of the game being started (colour,
// race, party...); the map loading, the game start and the setup dialogs
// read the players through it.
GamePlayerSetup* __fastcall GameSettings_PlayerSetup(GameSettings* self, void* /*edx*/, std::int32_t index) {
    GameLock lock(self->lock);
    return PlayerSetupAt(&self->player_setups, index);
}

// Purpose: Writes the game setup (the players, the local player, the game's
// id, the start goods and the other options) into a savegame.
void __fastcall GameSettings_SaveSetup(GameSettings* self, void* /*edx*/, CMapFile* file, std::uint32_t tick,
                                       std::uint32_t seed) {
    alignas(4) std::uint8_t localBytes[sizeof(GamePlayerSetup)];
    GamePlayerSetup* local = reinterpret_cast<SetupConstructFn>(kSetupConstruct)(
        reinterpret_cast<GamePlayerSetup*>(localBytes), nullptr);

    // Chunk 0x14: the count, then one record per player setup.
    fileformats::SaveSetupPlayersChunk players;
    std::memset(&players, 0, sizeof(players));
    InitChunkHeader(players, SaveChunk::SetupPlayers);
    players.count = self->player_setups.length;
    for (std::uint32_t i = 0; i < self->player_setups.length; ++i) {  // the length is re-read each time
        GamePlayerSetup* setup = PlayerSetupAt(&self->player_setups, static_cast<std::int32_t>(i));
        reinterpret_cast<SetupCopyFn>(kSetupAssign)(local, nullptr, setup);
        if (i < fileformats::kSavedPlayerSetups) {
            FillSavedPlayer(players.players[i], *local, i);
        }
    }
    WriteChunk(file, SaveChunk::SetupPlayers, &players);
    GameDebugLog(texts::kSavingLocalPlayerId, self->local_player_id);

    fileformats::SaveSetup1AChunk setup1A;
    std::memset(&setup1A, 0, sizeof(setup1A));
    InitChunkHeader(setup1A, SaveChunk::Setup1A);
    setup1A.local_player_id = self->local_player_id;
    for (std::uint32_t k = 0; k < 4; ++k) {
        setup1A.game_guid[k] = self->game_guid[k];
    }
    setup1A.tick = tick;
    setup1A.seed = static_cast<std::uint16_t>(seed);
    WriteChunk(file, SaveChunk::Setup1A, &setup1A);

    fileformats::SaveSetup1BChunk setup1B;
    InitChunkHeader(setup1B, SaveChunk::Setup1B);
    setup1B.valueC8 = self->valueC8;
    setup1B.start_goods = self->start_goods;
    WriteChunk(file, SaveChunk::Setup1B, &setup1B);

    fileformats::SaveSetup1EChunk setup1E;
    InitChunkHeader(setup1E, SaveChunk::Setup1E);
    setup1E.random_race_amazons = self->randomRaceAmazons;
    setup1E.goods_objective = self->goods_objective;
    WriteChunk(file, SaveChunk::Setup1E, &setup1E);

    DestroySetup(local);
}

}  // namespace s3re::game
