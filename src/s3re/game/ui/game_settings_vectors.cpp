// The Watcom vectors of the game setup's players and teams (Dirk\GameParams.cpp
// and Dirk\nethelp.cpp's units): WCValOrderedVector<Dirk::CPlayer> (our
// GamePlayerSetup, 0xcc bytes) and WCValSortedVector<Dirk::CTeam> (CTeam,
// 0x40 bytes) resize and insertAt, the team's copy constructor, operator=
// and player-id insert, and the player's "SLOT" log line. Layout of the
// vectors in watcom/vector_base.h, of the records in game_settings_layout.h;
// tests in game_settings_players_tests.cpp. All thiscall.
//
// Resize (n) ret 4, EAX 1 (0 if operator new failed; nothing changed then,
// after the library's empty error hook 0x59f200): keep = min(filled, n)
// (signed); for n != 0 a block of n * size bytes (32-bit product), the
// first `keep` elements copy-constructed into it (each destination
// null-checked); then every filled element destroyed (filled and data
// re-read), operator delete(data) (also of null), filled = keep, data =
// the block (null for n == 0), capacity = n. length is left alone.
//   0x00437820 CPlayer: copy 0x49eaf0, destroy ~GamePlayerSetup 0x49ec80
//   0x00437d00 CTeam:   copy 0x4a03a0, destroy ~CTeam 0x4a04d0
// InsertAt (index, const T& value) ret 8, EAX 1 (0: the vector is full and
// its growth step +0x18 is 0, or operator new failed): an index above the
// length (signed) becomes the length, one below 0 becomes 0 (error hook
// each). A full vector (length == capacity) is reallocated inline as by
// Resize(capacity + grow), but destroying with the scalar deleting
// destructor (flag 0), and its length clamped to the new capacity
// (unsigned). Then: at index == length the value is copy-constructed
// there (null-checked); otherwise the last element is copy-constructed
// one up (null-checked), elements length-2..index are assigned one up
// (operator=, data re-read) and the value is assigned to `index`. length
// and filled grow by one.
//   0x004379e0 CPlayer: copy 0x49eaf0, deleting dtor 0x437fc0, assign 0x49edf0
//   0x00438090 CTeam:   copy 0x4a03a0, deleting dtor 0x414b90, assign 0x4a05c0
// WCBareVectorBase<Dirk::CPlayer> copy of the contents (0x004a13d0; (const&
//   from) ret 4, void): unk04, filled and capacity from `from`; for a
//   positive capacity (signed) a new block (operator new failing: filled =
//   capacity = 0, error hook) with the `filled` elements copy-constructed
//   (filled re-read, destinations null-checked); else data = 0. The vtable,
//   length and grow are the caller's.
// Dirk::CTeam::CTeam(const CTeam& from) (0x004a03a0) ret 4, EAX = this: id;
//   the id vector as WCBareVectorBase<int>(0) 0x437730, length = grow = 0,
//   vtable WCValSortedVector<int>, then the contents as above with the ints
//   copied, grow, length = from's if there is a block else 0; the player
//   vector as WCBareVectorBase<CPlayer>(0) 0x4377a0, length = grow = 0,
//   vtable WCValOrderedVector<CPlayer>, 0x4a13d0, grow and length the same
//   way. unk04 is not copied.
// Dirk::CTeam::operator=(const CTeam& from) (0x004a05c0) ret 4, EAX = this:
//   nothing if from == this; id; each vector emptied by resize(0) (the
//   pointer vector's 0x429a00 for the ids, Resize 0x437820 for the players;
//   when that succeeds, a positive length becomes 0), then filled as in the
//   copy constructor. (Each member's own self-check is dead.)
// Dirk::CTeam::AddPlayerId (0x004a06d0; (int id) ret 4): inserts into the
//   sorted id vector at +8: the position from its virtual slot 3 (offset
//   0xc; (const int*, 3, int* index), only when the vector is not empty;
//   else 0), clamped like InsertAt; a full vector grows through the pointer
//   vector's resize 0x429a00 (capacity + grow; grow 0 or failure: EAX 0),
//   the length clamped to the new capacity; then the ids from the index up
//   move one up and the id is stored (each first slot null-checked as
//   placement new does). length and filled grow by one; EAX = the new
//   filled.
// GamePlayerSetup::LogSlot (0x004a01c0; Dirk::CPlayer, no arguments):
//   DebugLog("SLOT: Ids: %i %x Version: %i Team: %i Type: %i Nation: %i
//   Clan: %i %s", +0x10, +0x04, +0x08, +0x14, +0x5c, +0x64, +0xb8, the
//   text at +0xbc (the static empty string 0x6553a0 without buffer)). EAX:
//   DebugLog is a bare `ret`, so it is +0x10, the last value loaded.
//
// Known difference: an exception thrown by a copy constructor (only out of
// memory) is not unwound like the original's frames do.

#include "game/debug_log.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/ui/game_settings_layout.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "watcom/vector_base.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

using watcom::VectorBase;

constexpr std::uint32_t kPlayerSize = sizeof(GamePlayerSetup);
constexpr std::uint32_t kTeamSize = sizeof(CTeam);
constexpr std::uint32_t kFindInsertPosition = 3;  // the mode argument of the sorted vector's find (slot 3)

// The element members, called through their S3.EXE addresses (ours in the
// game).
using CopyFn = void (*)(std::uint32_t to, std::uint32_t from);
using DestroyFn = void (*)(std::uint32_t element);

template <std::uintptr_t Address>
void CallWithElement(std::uint32_t self, std::uint32_t argument) {
    reinterpret_cast<void(__fastcall*)(std::uint32_t, void*, std::uint32_t)>(Address)(self, nullptr, argument);
}
template <std::uintptr_t Address>
void CallDestructor(std::uint32_t self) {
    reinterpret_cast<void(__fastcall*)(std::uint32_t, void*)>(Address)(self, nullptr);
}
template <std::uintptr_t Address>
void CallDeletingDestructor(std::uint32_t self) {
    CallWithElement<Address>(self, 0);  // flag 0: destroy, don't free
}

struct ElementOps {
    std::uint32_t size;
    CopyFn copy;
    CopyFn assign;
    DestroyFn destroy;          // in Resize
    DestroyFn destroyInInsert;  // in InsertAt's inlined reallocation
};

constexpr ElementOps kPlayerOps{kPlayerSize, &CallWithElement<0x0049eaf0>, &CallWithElement<0x0049edf0>,
                                &CallDestructor<0x0049ec80>, &CallDeletingDestructor<0x00437fc0>};
constexpr ElementOps kTeamOps{kTeamSize, &CallWithElement<0x004a03a0>, &CallWithElement<0x004a05c0>,
                              &CallDestructor<0x004a04d0>, &CallDeletingDestructor<0x00414b90>};

std::uint32_t ElementAt(std::uint32_t data, std::int32_t index, std::uint32_t size) {
    return data + static_cast<std::uint32_t>(index) * size;
}

// The reallocation of Resize and of InsertAt's growth; false if operator
// new failed (nothing changed).
bool Reallocate(VectorBase* v, std::uint32_t n, const ElementOps& ops, DestroyFn destroy) {
    const std::int32_t keep = v->filled < static_cast<std::int32_t>(n) ? v->filled : static_cast<std::int32_t>(n);
    std::uint32_t block = 0;
    if (n != 0) {
        block = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(msvc6::GameOperatorNew(n * ops.size)));
        if (block == 0) {
            return false;  // (after the empty error hook 0x59f200)
        }
        for (std::int32_t i = 0; i < keep; ++i) {
            const std::uint32_t to = ElementAt(block, i, ops.size);
            if (to != 0) {
                ops.copy(to, ElementAt(v->data, i, ops.size));
            }
        }
    }
    for (std::int32_t i = 0; i < v->filled; ++i) {
        destroy(ElementAt(v->data, i, ops.size));
    }
    msvc6::GameOperatorDelete(reinterpret_cast<void*>(static_cast<std::uintptr_t>(v->data)));
    v->filled = keep;
    v->data = block;
    v->capacity = n;
    return true;
}

// The index clamp of the inserts: above the length (signed) the length,
// below 0 zero.
std::int32_t ClampInsertIndex(const VectorBase* v, std::int32_t index) {
    if (index > static_cast<std::int32_t>(v->length)) {
        return static_cast<std::int32_t>(v->length);
    }
    return index < 0 ? 0 : index;
}

std::uint32_t InsertElement(VectorBase* v, std::int32_t index, std::uint32_t value, const ElementOps& ops) {
    index = ClampInsertIndex(v, index);
    if (v->length == v->capacity) {
        if (v->grow == 0) {
            return 0;
        }
        const std::uint32_t capacity = v->capacity + v->grow;
        if (!Reallocate(v, capacity, ops, ops.destroyInInsert)) {
            return 0;
        }
        if (v->length > capacity) {
            v->length = capacity;
        }
    }
    const std::uint32_t length = v->length;
    if (index == static_cast<std::int32_t>(length)) {
        const std::uint32_t to = ElementAt(v->data, index, ops.size);
        if (to != 0) {
            ops.copy(to, value);
        }
    } else {
        const std::uint32_t last = ElementAt(v->data, static_cast<std::int32_t>(length), ops.size);
        if (last != 0) {
            ops.copy(last, last - ops.size);
        }
        for (std::int32_t k = static_cast<std::int32_t>(length) - 2; k >= index; --k) {
            ops.assign(ElementAt(v->data, k + 1, ops.size), ElementAt(v->data, k, ops.size));
        }
        ops.assign(ElementAt(v->data, index, ops.size), value);
    }
    ++v->length;
    ++v->filled;
    return 1;
}

// WCBareVectorBase<int>(0) 0x437730 and WCBareVectorBase<Dirk::CPlayer>(0)
// 0x4377a0 (ours).
void ConstructEmptyBareVector(VectorBase* v, std::uintptr_t constructor) {
    reinterpret_cast<void(__fastcall*)(VectorBase*, void*, std::uint32_t)>(constructor)(v, nullptr, 0);
}

// The id vector's contents from `from` (the inlined copy of the copy
// constructor and operator=; the ints copied).
void CopyIds(VectorBase* v, const VectorBase* from) {
    v->unk04 = from->unk04;
    v->filled = from->filled;
    v->capacity = from->capacity;
    if (static_cast<std::int32_t>(v->capacity) > 0) {
        v->data = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(msvc6::GameOperatorNew(v->capacity * sizeof(std::int32_t))));
        if (v->data == 0) {
            v->filled = 0;
            v->capacity = 0;  // (then the empty error hook 0x59f200)
            return;
        }
        for (std::int32_t i = 0; i < v->filled; ++i) {
            auto* to = reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(ElementAt(v->data, i, 4)));
            if (to != nullptr) {
                *to = *reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(ElementAt(from->data, i, 4)));
            }
        }
    } else {
        v->data = 0;
    }
}

// After a vector's contents were copied: grow, and the length when there
// is a block.
void CopyGrowAndLength(VectorBase* v, const VectorBase* from) {
    v->grow = from->grow;
    v->length = v->data != 0 ? from->length : 0;
}

void CopyPlayers(VectorBase* v, const VectorBase* from) {
    using CopyContentsFn = void(__fastcall*)(VectorBase*, void*, const VectorBase*);
    reinterpret_cast<CopyContentsFn>(0x004a13d0)(v, nullptr, from);
}

// resize(0), and a positive length cleared when that succeeded.
void EmptyForAssign(VectorBase* v, std::uintptr_t resize) {
    if (reinterpret_cast<std::uint32_t(__fastcall*)(VectorBase*, void*, std::uint32_t)>(resize)(v, nullptr, 0) != 0 &&
        v->length > 0) {
        v->length = 0;
    }
}

}  // namespace

using ResizeFn = std::uint32_t(__fastcall*)(VectorBase* self, void* edx, std::uint32_t n);
using InsertAtFn = std::uint32_t(__fastcall*)(VectorBase* self, void* edx, std::int32_t index, const void* value);
using CopyContentsFn = void(__fastcall*)(VectorBase* self, void* edx, const VectorBase* from);
using TeamCopyFn = CTeam*(__fastcall*)(CTeam* self, void* edx, const CTeam* from);
using AddPlayerIdFn = std::uint32_t(__fastcall*)(CTeam* self, void* edx, std::int32_t id);
using LogSlotFn = std::uint32_t(__fastcall*)(const GamePlayerSetup* self, void* edx);

std::uint32_t __fastcall PlayerVector_Resize(VectorBase* self, void* edx, std::uint32_t n);
std::uint32_t __fastcall TeamVector_Resize(VectorBase* self, void* edx, std::uint32_t n);
std::uint32_t __fastcall PlayerVector_InsertAt(VectorBase* self, void* edx, std::int32_t index, const void* value);
std::uint32_t __fastcall TeamVector_InsertAt(VectorBase* self, void* edx, std::int32_t index, const void* value);
void __fastcall PlayerBareVector_CopyContents(VectorBase* self, void* edx, const VectorBase* from);
CTeam* __fastcall CTeam_CopyConstruct(CTeam* self, void* edx, const CTeam* from);
CTeam* __fastcall CTeam_Assign(CTeam* self, void* edx, const CTeam* from);
std::uint32_t __fastcall CTeam_AddPlayerId(CTeam* self, void* edx, std::int32_t id);
std::uint32_t __fastcall GamePlayerSetup_LogSlot(const GamePlayerSetup* self, void* edx);

S3RE_HOOK(0x00437820, PlayerVector_Resize, ResizeFn, Original_PlayerVector_Resize, PlayerVector_Resize);
S3RE_HOOK(0x00437d00, TeamVector_Resize, ResizeFn, Original_TeamVector_Resize, TeamVector_Resize);
S3RE_HOOK(0x004379e0, PlayerVector_InsertAt, InsertAtFn, Original_PlayerVector_InsertAt, PlayerVector_InsertAt);
S3RE_HOOK(0x00438090, TeamVector_InsertAt, InsertAtFn, Original_TeamVector_InsertAt, TeamVector_InsertAt);
S3RE_HOOK(0x004a13d0, PlayerBareVector_CopyContents, CopyContentsFn, Original_PlayerBareVector_CopyContents,
          PlayerBareVector_CopyContents);
S3RE_HOOK(0x004a03a0, CTeam_CopyConstruct, TeamCopyFn, Original_CTeam_CopyConstruct, CTeam_CopyConstruct);
S3RE_HOOK(0x004a05c0, CTeam_Assign, TeamCopyFn, Original_CTeam_Assign, CTeam_Assign);
S3RE_HOOK(0x004a06d0, CTeam_AddPlayerId, AddPlayerIdFn, Original_CTeam_AddPlayerId, CTeam_AddPlayerId);
S3RE_HOOK(0x004a01c0, GamePlayerSetup_LogSlot, LogSlotFn, Original_GamePlayerSetup_LogSlot, GamePlayerSetup_LogSlot);

// Purpose: Changes how many player setups a vector of them has room for
// (the game settings' setup and load code), keeping the ones that fit.
std::uint32_t __fastcall PlayerVector_Resize(VectorBase* self, void*, std::uint32_t n) {
    return Reallocate(self, n, kPlayerOps, kPlayerOps.destroy) ? 1 : 0;
}

// Purpose: The same for a vector of teams.
std::uint32_t __fastcall TeamVector_Resize(VectorBase* self, void*, std::uint32_t n) {
    return Reallocate(self, n, kTeamOps, kTeamOps.destroy) ? 1 : 0;
}

// Purpose: Inserts a copy of a player setup at a position of a vector of
// them (NewGame and LoadSaveGame build the settings' list of players with it).
std::uint32_t __fastcall PlayerVector_InsertAt(VectorBase* self, void*, std::int32_t index, const void* value) {
    return InsertElement(self, index, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(value)),
                         kPlayerOps);
}

// Purpose: Inserts a copy of a team at a position of the sorted vector of
// teams (LoadSaveGame and the lobby).
std::uint32_t __fastcall TeamVector_InsertAt(VectorBase* self, void*, std::int32_t index, const void* value) {
    return InsertElement(self, index, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(value)), kTeamOps);
}

// Purpose: Copies the player setups of one team's player vector into
// another's (the team's copy constructor and operator=).
void __fastcall PlayerBareVector_CopyContents(VectorBase* self, void*, const VectorBase* from) {
    self->unk04 = from->unk04;
    self->filled = from->filled;
    self->capacity = from->capacity;
    if (static_cast<std::int32_t>(self->capacity) <= 0) {
        self->data = 0;
        return;
    }
    self->data = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(msvc6::GameOperatorNew(self->capacity * kPlayerSize)));
    if (self->data == 0) {
        self->filled = 0;
        self->capacity = 0;  // (then the empty error hook 0x59f200)
        return;
    }
    for (std::int32_t i = 0; i < self->filled; ++i) {
        const std::uint32_t to = ElementAt(self->data, i, kPlayerSize);
        if (to != 0) {
            kPlayerOps.copy(to, ElementAt(from->data, i, kPlayerSize));
        }
    }
}

// Purpose: Copies a team (its id, player ids and player setups) into a new
// record, e.g. when the vector of teams grows.
CTeam* __fastcall CTeam_CopyConstruct(CTeam* self, void*, const CTeam* from) {
    self->id = from->id;
    ConstructEmptyBareVector(&self->player_ids, 0x00437730);
    self->player_ids.length = 0;
    self->player_ids.grow = 0;
    self->player_ids.vtable = vtables::kWCValSortedVector_int;
    CopyIds(&self->player_ids, &from->player_ids);
    CopyGrowAndLength(&self->player_ids, &from->player_ids);
    ConstructEmptyBareVector(&self->players, 0x004377a0);
    self->players.length = 0;
    self->players.grow = 0;
    self->players.vtable = vtables::kWCValOrderedVector_CPlayer;
    CopyPlayers(&self->players, &from->players);
    CopyGrowAndLength(&self->players, &from->players);
    return self;
}

// Purpose: Copies one team over another (the vector of teams moves its
// elements with it).
CTeam* __fastcall CTeam_Assign(CTeam* self, void*, const CTeam* from) {
    if (self == from) {
        return self;
    }
    self->id = from->id;
    EmptyForAssign(&self->player_ids, 0x00429a00);  // watcom::PtrVector::resize
    CopyIds(&self->player_ids, &from->player_ids);
    CopyGrowAndLength(&self->player_ids, &from->player_ids);
    EmptyForAssign(&self->players, 0x00437820);  // PlayerVector_Resize
    CopyPlayers(&self->players, &from->players);
    CopyGrowAndLength(&self->players, &from->players);
    return self;
}

// Purpose: Adds a player id to a team's sorted list of player ids (the game
// settings when they build the teams).
std::uint32_t __fastcall CTeam_AddPlayerId(CTeam* self, void*, std::int32_t id) {
    VectorBase* ids = &self->player_ids;
    std::int32_t index = 0;
    if (ids->length > 0) {
        using FindFn = void(__fastcall*)(VectorBase*, void*, const std::int32_t*, std::uint32_t, std::int32_t*);
        const auto* vtable = reinterpret_cast<const FindFn*>(static_cast<std::uintptr_t>(ids->vtable));
        vtable[3](ids, nullptr, &id, kFindInsertPosition, &index);
    }
    index = ClampInsertIndex(ids, index);
    if (ids->length == ids->capacity) {
        if (ids->grow == 0) {
            return 0;
        }
        const std::uint32_t capacity = ids->capacity + ids->grow;
        using PtrResizeFn = std::uint32_t(__fastcall*)(VectorBase*, void*, std::uint32_t);
        if (reinterpret_cast<PtrResizeFn>(0x00429a00)(ids, nullptr, capacity) == 0) {
            return 0;
        }
        if (ids->length > capacity) {
            ids->length = capacity;
        }
    }
    auto slot = [ids](std::int32_t k) {
        return reinterpret_cast<std::int32_t*>(static_cast<std::uintptr_t>(ElementAt(ids->data, k, 4)));
    };
    const auto length = static_cast<std::int32_t>(ids->length);
    if (index == length) {
        if (std::int32_t* to = slot(index); to != nullptr) {
            *to = id;
        }
    } else {
        if (std::int32_t* last = slot(length); last != nullptr) {
            *last = last[-1];
        }
        for (std::int32_t k = length - 2; k >= index; --k) {
            *slot(k + 1) = *slot(k);
        }
        *slot(index) = id;
    }
    ++ids->length;
    ++ids->filled;
    return static_cast<std::uint32_t>(ids->filled);
}

// Purpose: Writes one player's setup to the debug log ("SLOT: Ids: ...")
// when the game settings are built or loaded.
std::uint32_t __fastcall GamePlayerSetup_LogSlot(const GamePlayerSetup* self, void*) {
    std::uint32_t clan;
    std::memcpy(&clan, self->unk0b8, sizeof(clan));
    const char* name = self->textBC.ptr != nullptr ? self->textBC.ptr : g_stringNullBuffer.get();
    GameDebugLog(texts::kSlotLog, self->value10, self->id04, self->version, self->party, self->type, self->race,
                 clan, name);
    return self->value10;  // (DebugLog is a bare `ret`)
}

}  // namespace s3re::game
