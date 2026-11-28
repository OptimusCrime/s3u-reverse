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
// GameSettings::PlayerSetupById (0x00435390): (id) ret 4 -> GamePlayerSetup*,
//   thiscall, under the lock: a temporary setup with that network id
//   (GamePlayerSetup(id) 0x49e9f0) is looked for in the vector (its virtual
//   find, slot 3, occurrence 1; not called on an empty vector); the index
//   found, or -1, goes through the same inlined operator[] as PlayerSetup's
//   (so an unknown id gives the first setup). The temporary is destroyed
//   (0x49ec80) before the access. 20 callers (the drawer's peer messages,
//   the lobby and network code, the menus). Was GameSetup_sub_435390.
// GameSettings::HasPlayerId (0x00435590): (id) ret 4 -> AL, thiscall, under
//   the lock: whether the vector's find (slot 3, occurrence 0) finds a
//   temporary setup with that network id (not called on an empty vector).
//   Callers: CNetSession::vf04 and the network code. Was
//   GameSetup_sub_435590.
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
// GamePlayerSetup::GamePlayerSetup_id (0x0049e9f0; was named as a
//   WCValOrderedVector<unsigned short> constructor): (id) ret 4, EAX =
//   this: the same with +0x10 = id, and the three allocator bytes the low
//   byte of id (the compiler let the empty allocator objects share the
//   argument's stack slot; docs/reimplementation_notes.md, case a).
//
// GamePlayerSetup::GamePlayerSetup(const GamePlayerSetup& from)
//   (0x0049eaf0) ret 4, EAX = this: the plain fields (below), the strings
//   +0x18 and +0xbc (allocator byte = the low byte of `from`, the
//   argument's stack slot; then std::string::assign(x, 0, -1) 0x401d60), the
//   16-bit string +0x28 (allocator byte = from's; std::wstring::assign(x, 0,
//   npos) 0x428400), and a new empty vector at +0x38 as in the
//   constructor (not copied).
// GamePlayerSetup::operator=(const GamePlayerSetup& from) (0x0049edf0)
//   ret 4, EAX = this: nothing if from == this; else the same plain fields
//   and the three strings (inlined assign(x): a full copy shares the buffer
//   below count 0xfe; the 16-bit one first, then +0x18, then +0xbc); the
//   vector and the allocator bytes stay.
//   Plain fields of both: +0x00..+0x0c, byte +0x0c, +0x10..+0x17, byte
//   +0x54, +0x58..+0x60, byte +0x60, +0x64..+0x67, byte +0x68,
//   +0x6c..+0xbb. Not copied: +0x0d..+0x0f, +0x55..+0x57, +0x61..+0x63,
//   +0x69..+0x6b (padding of byte fields).
// GamePlayerSetup::Set (0x0049f120): (type, byte60, party, race, raceByte,
//   a, b, c, colour) ret 0x24: +0x60 = byte60 (low byte), +0x5c = type,
//   +0x14 = party, +0x70 = party if it was -1, +0x64 = race, +0x6c = race if
//   it was -1, +0x68 = raceByte (low byte), +0xa8/+0xac/+0xb0 = a/b/c, +0xb4
//   = colour. EAX = colour. The installed hook is S3RE_HOOK_ALWAYS: it first
//   applies the --race override of the --random-map tooling
//   (tooling/random_map_start.cpp), then runs ours (the original in
//   original mode).
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
#include "game/tables.h"
#include "game/texts.h"
#include "game/ui/game_settings_layout.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/wstring.h"
#include "tooling/random_map_start.h"

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

GamePlayerSetup* __fastcall GamePlayerSetup_ConstructId(GamePlayerSetup* self, void* edx, std::uint32_t id);
using SetupConstructIdFn = GamePlayerSetup*(__fastcall*)(GamePlayerSetup* self, void* edx, std::uint32_t id);
S3RE_HOOK(0x0049e9f0, GamePlayerSetup_ConstructId, SetupConstructIdFn, Original_GamePlayerSetup_ConstructId,
          GamePlayerSetup_ConstructId);

// Purpose: Sets up an empty player setup record for a given player id (the
// network and game settings code).
GamePlayerSetup* __fastcall GamePlayerSetup_ConstructId(GamePlayerSetup* self, void* edx, std::uint32_t id) {
    GamePlayerSetup_Construct(self, edx);
    self->value10 = id;
    const auto allocator = static_cast<std::uint8_t>(id);  // (id's stack slot)
    self->text18.allocator = allocator;
    reinterpret_cast<msvc6::String*>(reinterpret_cast<std::uint8_t*>(self) + 0x28)->allocator = allocator;
    self->textBC.allocator = allocator;
    return self;
}

namespace {

// The plain fields both copy functions copy ([begin, end) byte ranges).
constexpr std::size_t kCopiedFields[][2] = {{0x00, 0x0d}, {0x10, 0x18}, {0x54, 0x55},
                                            {0x58, 0x61}, {0x64, 0x69}, {0x6c, 0xbc}};
constexpr std::size_t kWideName = 0x28;  // the 16-bit string (no name known)
constexpr std::size_t kNamesVector = 0x38;

void CopyPlainFields(GamePlayerSetup* self, const GamePlayerSetup* from) {
    for (const auto& range : kCopiedFields) {
        std::memcpy(reinterpret_cast<std::uint8_t*>(self) + range[0],  // raw-ok: unnamed fields of the record
                    reinterpret_cast<const std::uint8_t*>(from) + range[0], range[1] - range[0]);
    }
}

msvc6::WString* WideNameOf(GamePlayerSetup* setup) {
    return reinterpret_cast<msvc6::WString*>(reinterpret_cast<std::uint8_t*>(setup) + kWideName);
}
const msvc6::WString* WideNameOf(const GamePlayerSetup* setup) {
    return reinterpret_cast<const msvc6::WString*>(reinterpret_cast<const std::uint8_t*>(setup) + kWideName);
}

// std::string::assign(x, 0, n) 0x401d60 and std::wstring::assign(x, 0, n)
// 0x428400 through their S3.EXE addresses (ours in the game).
void AssignString(msvc6::String* self, const msvc6::String* x, std::uint32_t n) {
    reinterpret_cast<msvc6::StringAssignSubstrFn>(0x00401d60)(self, nullptr, x, 0, n);
}
void AssignWide(msvc6::WString* self, const msvc6::WString* x, std::uint32_t n) {
    reinterpret_cast<msvc6::WStringAssignSubstrFn>(0x00428400)(self, nullptr, x, 0, n);
}

}  // namespace

using SetupCopyHookFn = GamePlayerSetup*(__fastcall*)(GamePlayerSetup* self, void* edx, const GamePlayerSetup* from);
GamePlayerSetup* __fastcall GamePlayerSetup_CopyConstruct(GamePlayerSetup* self, void* edx,
                                                          const GamePlayerSetup* from);
GamePlayerSetup* __fastcall GamePlayerSetup_Assign(GamePlayerSetup* self, void* edx, const GamePlayerSetup* from);
S3RE_HOOK(0x0049eaf0, GamePlayerSetup_CopyConstruct, SetupCopyHookFn, Original_GamePlayerSetup_CopyConstruct,
          GamePlayerSetup_CopyConstruct);
S3RE_HOOK(0x0049edf0, GamePlayerSetup_Assign, SetupCopyHookFn, Original_GamePlayerSetup_Assign,
          GamePlayerSetup_Assign);

// Purpose: Copies a player setup into a new record (the player-setup
// vector's growth, the game settings dialogs); the strings are shared or
// copied, the vector at +0x38 starts empty.
GamePlayerSetup* __fastcall GamePlayerSetup_CopyConstruct(GamePlayerSetup* self, void* /*edx*/,
                                                          const GamePlayerSetup* from) {
    // The allocator bytes of the two std::strings: the low byte of `from`
    // (the compiler let the empty allocators share the argument's stack slot).
    const auto allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(from));
    CopyPlainFields(self, from);
    EmptyString(&self->text18);
    self->text18.allocator = allocator;
    AssignString(&self->text18, &from->text18, 0xFFFFFFFFu);  // (a literal -1, not npos)
    EmptyString(WideNameOf(self));
    WideNameOf(self)->allocator = WideNameOf(from)->allocator;
    AssignWide(WideNameOf(self), WideNameOf(from), g_wstringNpos);
    auto* bytes = reinterpret_cast<std::uint8_t*>(self);
    reinterpret_cast<UShortVectorConstructFn>(kUShortVectorConstruct)(bytes + kNamesVector, nullptr,
                                                                     kSetupVectorCapacity);
    const std::uint32_t vectorFields[3] = {vtables::kWCValOrderedVector_ushort, 0, kSetupVectorField18};
    std::memcpy(bytes + kNamesVector, &vectorFields[0], 4);          // raw-ok: the vector's vtable
    std::memcpy(bytes + kNamesVector + 0x14, &vectorFields[1], 8);  // raw-ok: its +0x14 and +0x18
    EmptyString(&self->textBC);
    self->textBC.allocator = allocator;
    AssignString(&self->textBC, &from->textBC, 0xFFFFFFFFu);
    return self;
}

// Purpose: Copies one player setup over another (GameSettings::SaveSetup's
// local copy, the setup dialogs); the vector at +0x38 is not copied.
GamePlayerSetup* __fastcall GamePlayerSetup_Assign(GamePlayerSetup* self, void* /*edx*/, const GamePlayerSetup* from) {
    if (self == from) {
        return self;
    }
    CopyPlainFields(self, from);
    AssignWide(WideNameOf(self), WideNameOf(from), g_wstringNpos);
    AssignString(&self->text18, &from->text18, g_stringNpos);
    AssignString(&self->textBC, &from->textBC, g_stringNpos);
    return self;
}

using SetupSetFn = std::uint32_t(__fastcall*)(GamePlayerSetup* self, void* edx, std::uint32_t type,
                                             std::uint32_t byte60, std::uint32_t party, std::uint32_t race,
                                             std::uint32_t raceByte, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                             std::uint32_t colour);
std::uint32_t __fastcall GamePlayerSetup_Set(GamePlayerSetup* self, void* edx, std::uint32_t type, std::uint32_t byte60,
                                             std::uint32_t party, std::uint32_t race, std::uint32_t raceByte,
                                             std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t colour);
std::uint32_t __fastcall GamePlayerSetup_SetHook(GamePlayerSetup* self, void* edx, std::uint32_t type,
                                                 std::uint32_t byte60, std::uint32_t party, std::uint32_t race,
                                                 std::uint32_t raceByte, std::uint32_t a, std::uint32_t b,
                                                 std::uint32_t c, std::uint32_t colour);
// Stays installed in original mode, for the --race override of --random-map.
S3RE_HOOK_ALWAYS(0x0049f120, GamePlayerSetup_Set, SetupSetFn, Original_GamePlayerSetup_Set, GamePlayerSetup_SetHook);

// Purpose: Fills in a player setup (type, party, race, start position
// values, colour) when the game setup dialogs or the map loader set a
// player up; the "initial" party and race (+0x70, +0x6c) are only set once.
std::uint32_t __fastcall GamePlayerSetup_Set(GamePlayerSetup* self, void*, std::uint32_t type, std::uint32_t byte60,
                                             std::uint32_t party, std::uint32_t race, std::uint32_t raceByte,
                                             std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t colour) {
    auto* bytes = reinterpret_cast<std::uint8_t*>(self);
    auto dword = [bytes](std::size_t offset) -> std::uint32_t& {
        return *reinterpret_cast<std::uint32_t*>(bytes + offset);  // raw-ok: unnamed fields of the record
    };
    bytes[0x60] = static_cast<std::uint8_t>(byte60);  // raw-ok: unnamed byte of the record
    self->type = type;
    self->party = party;
    if (dword(0x70) == kUnset) {
        dword(0x70) = party;
    }
    self->race = race;
    if (dword(0x6c) == kUnset) {
        dword(0x6c) = race;
    }
    self->race_byte = static_cast<std::uint8_t>(raceByte);
    self->saved_values[0] = a;
    self->saved_values[1] = b;
    self->saved_values[2] = c;
    self->colour = colour;
    return colour;
}

// The installed hook: the tooling's race override, then ours (or the
// original in original mode).
std::uint32_t __fastcall GamePlayerSetup_SetHook(GamePlayerSetup* self, void* edx, std::uint32_t type,
                                                 std::uint32_t byte60, std::uint32_t party, std::uint32_t race,
                                                 std::uint32_t raceByte, std::uint32_t a, std::uint32_t b,
                                                 std::uint32_t c, std::uint32_t colour) {
    tooling::ApplyHumanRaceOverride(type, party, race, raceByte);
    if (hooks::OriginalMode()) {
        return Original_GamePlayerSetup_Set(self, edx, type, byte60, party, race, raceByte, a, b, c, colour);
    }
    return GamePlayerSetup_Set(self, edx, type, byte60, party, race, raceByte, a, b, c, colour);
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
GamePlayerSetup* __fastcall GameSettings_PlayerSetupById(GameSettings* self, void* edx, std::uint32_t id);
using PlayerSetupByIdFn = GamePlayerSetup*(__fastcall*)(GameSettings* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall GameSettings_HasPlayerId(GameSettings* self, void* edx, std::uint32_t id);
using HasPlayerIdFn = std::uint32_t(__fastcall*)(GameSettings* self, void* edx, std::uint32_t id);
S3RE_HOOK(0x00435590, GameSettings_HasPlayerId, HasPlayerIdFn, Original_GameSettings_HasPlayerId,
          GameSettings_HasPlayerId);
S3RE_HOOK(0x00435390, GameSettings_PlayerSetupById, PlayerSetupByIdFn, Original_GameSettings_PlayerSetupById,
          GameSettings_PlayerSetupById);

// Purpose: Gives one player's setup of the game being started (colour,
// race, party...); the map loading, the game start and the setup dialogs
// read the players through it.
GamePlayerSetup* __fastcall GameSettings_PlayerSetup(GameSettings* self, void* /*edx*/, std::int32_t index) {
    GameLock lock(self->lock);
    return PlayerSetupAt(&self->player_setups, index);
}

namespace {

using SetupConstructIdFn = GamePlayerSetup*(__fastcall*)(GamePlayerSetup* self, void* edx, std::uint32_t id);
using SetupFindFn = std::uint32_t(__fastcall*)(PlayerSetupVector* self, void* edx, const GamePlayerSetup* value,
                                               std::int32_t occurrence, std::int32_t* index);
constexpr std::uintptr_t kSetupConstructId = 0x0049e9f0;  // GamePlayerSetup::GamePlayerSetup(id) (ours)
constexpr std::uint32_t kSetupFindSlot = 3;

// The vector's virtual find (slot 3) for a temporary setup with network id
// `id` (destroyed again before returning); false on an empty vector.
bool FindSetupById(PlayerSetupVector* vector, std::uint32_t id, std::int32_t occurrence, std::int32_t* index) {
    alignas(4) std::uint8_t keyBytes[sizeof(GamePlayerSetup)];
    auto* key = reinterpret_cast<SetupConstructIdFn>(kSetupConstructId)(reinterpret_cast<GamePlayerSetup*>(keyBytes),
                                                                         nullptr, id);
    bool found = false;
    if (vector->length != 0) {
        const auto* vtable = reinterpret_cast<const std::uintptr_t*>(static_cast<std::uintptr_t>(vector->vtable));
        found = reinterpret_cast<SetupFindFn>(vtable[kSetupFindSlot])(vector, nullptr, key, occurrence, index) != 0;
    }
    DestroySetup(key);
    return found;
}

}  // namespace

// Purpose: Finds the setup of the player with a network id (the first
// setup when there is none).
GamePlayerSetup* __fastcall GameSettings_PlayerSetupById(GameSettings* self, void* /*edx*/, std::uint32_t id) {
    constexpr std::int32_t kFirstOccurrence = 1;
    constexpr std::int32_t kNotFound = -1;
    GameLock lock(self->lock);
    std::int32_t index = 0;
    const std::int32_t found = FindSetupById(&self->player_setups, id, kFirstOccurrence, &index) ? index : kNotFound;
    return PlayerSetupAt(&self->player_setups, found);
}

// Purpose: Whether a player with this network id is in the game setup.
std::uint32_t __fastcall GameSettings_HasPlayerId(GameSettings* self, void* /*edx*/, std::uint32_t id) {
    constexpr std::int32_t kAnyOccurrence = 0;
    GameLock lock(self->lock);
    std::int32_t index = 0;
    return FindSetupById(&self->player_setups, id, kAnyOccurrence, &index) ? 1 : 0;
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
