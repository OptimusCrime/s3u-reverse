// CComputerPlayer and its blackboard notes (computer players, batch 1 of
// the AI; re/computer_players.md). The CComputerPlayer methods are in an
// unnamed source file before Rainer\Cevaland.cpp (object unit 57,
// 0x550900..0x553d30); the BLACK_BOARD_NOTE helpers sit between
// Rainer\CompHouseReq.cpp and Rainer\CompMapWorkerAgent.cpp.
//
// A computer player keeps its notes (BLACK_BOARD_NOTE and the derived
// request notes, RTTI) by id in ComputerPlayer::notes and four id lists (ai_layout.h):
// root notes, pending notes, active notes and a fourth one (list200).
// The list functions are thiscall with one argument (ret 4):
//   0x5517f0 AddRootNote(id)       AI_WordListInsert(id, root_notes, 100), on success ++root_note_count
//   0x551820 RemoveRootNote(id)    AI_WordListRemove(...), on success --root_note_count
//   0x551850 AddPendingNote(id)    pending_notes (50), pending_note_count
//   0x551880 RemovePendingNote(id)
//   0x5518b0 AddActiveNote(id)     active_notes (100), active_note_count
//   0x5518e0 RemoveActiveNote(id)
//   0x551940 RemoveFromList200(id)  list200 (50), list200_count (the add is
//                                   AI_List32Add 0x551910, ai_helpers.cpp)
//   EAX = the insert/remove result (1/0).
// 0x5515e0 ActivateNote(id): AI_SetCurrentPlayer(this), AddActiveNote(id),
//   then BLACK_BOARD_NOTE::SetFlags(notes[id], 4). EAX = 4 (SetFlags puts
//   4 into AX; the upper half is AddActiveNote's 0/1 result's, 0).
// 0x5515c0 ActivateNoteOf(note): ActivateNote(note->id), the id loaded
//   with `mov dx`, so the upper half of the pushed dword is the caller's
//   EDX (ours gets it as the EDX parameter; only the low word is used).
// 0x551630 DeactivateNote(id): if notes[id] has flag 4: RemoveActiveNote(id)
//   and ClearFlags(4), EAX = ~4; else EAX = 0. (Ghidra put a false function
//   start at 0x551637, inside it.)
// 0x551610 DeactivateNoteOf(note): the same with note->id (`mov dx`).
// 0x551b10 RaiseBuildPriorities(): for each building type 0..56 of the
//   current AiPlayerData node (+0x778): a non-zero priority (best[type].score)
//   below cap - step (signed) grows by step (g_aiBuildPrioritySteps /
//   g_aiBuildPriorityCaps). EAX: not used (it flows into 0x550f80's return,
//   which CLogicThread::Step ignores).
//
// BLACK_BOARD_NOTE helpers (thiscall):
//   0x582400 ResetLinks(): flags, type, +0x0a, +0x0c = 0; id, parent, next
//     sibling, first child, +0x16, +0x18 = 0xffff. EAX = 0xffff.
//   0x5823e0 Init(type): ResetLinks, type = the low word. EAX = type & 0xffff.
//   0x582430 MarkDispatched(): SetFlags(1).
//   0x582440 SetFlags(bits): flags |= bits (low word). EAX: AX = bits, the
//     upper half is the caller's EAX (callers use AX; ours: 0).
//   0x582450 ClearFlags(bits): flags &= ~bits. EAX = ~bits.
//   0x582460 HasFlags(bits): (flags & bits) == bits on the low words; EAX 1/0.
//   0x5824d0 FindChildOfType(type): walks the children (first_child, then
//     next_sibling, ids into g_aiPlayer->notes) for one whose type equals
//     the low word; EAX = it, or 0.

#include "game/ai_layout.h"
#include "game/enums.h"
#include "game/globals.h"
#include "game/tables.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

// ai_helpers.cpp
std::uint32_t __fastcall AI_SetCurrentPlayer(void* player);
std::uint32_t __stdcall AI_WordListInsert(std::uint32_t value, std::uint16_t* list, std::uint32_t count);
std::uint32_t __stdcall AI_WordListRemove(std::uint32_t value, std::uint16_t* list, std::uint32_t count);

using namespace ai;

using NoteIdFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t id);
using NoteFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using NoteVoidFn = void(__fastcall*)(void* self, void* edx);

std::uint32_t __fastcall CComputerPlayer_AddRootNote(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_RemoveRootNote(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_AddPendingNote(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_RemovePendingNote(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_AddActiveNote(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_RemoveActiveNote(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_RemoveFromList200(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_ActivateNote(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_ActivateNoteOf(void* self, void* edx, std::uint32_t note);
std::uint32_t __fastcall CComputerPlayer_DeactivateNote(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_DeactivateNoteOf(void* self, void* edx, std::uint32_t note);
void __fastcall CComputerPlayer_RaiseBuildPriorities(void* self, void* edx);
std::uint32_t __fastcall BLACK_BOARD_NOTE_ResetLinks(void* self, void* edx);
std::uint32_t __fastcall BLACK_BOARD_NOTE_Init(void* self, void* edx, std::uint32_t type);
std::uint32_t __fastcall BLACK_BOARD_NOTE_MarkDispatched(void* self, void* edx);
std::uint32_t __fastcall BLACK_BOARD_NOTE_SetFlags(void* self, void* edx, std::uint32_t bits);
std::uint32_t __fastcall BLACK_BOARD_NOTE_ClearFlags(void* self, void* edx, std::uint32_t bits);
std::uint32_t __fastcall BLACK_BOARD_NOTE_HasFlags(void* self, void* edx, std::uint32_t bits);
std::uint32_t __fastcall BLACK_BOARD_NOTE_FindChildOfType(void* self, void* edx, std::uint32_t type);

namespace {

ComputerPlayer* AsPlayer(void* self) { return static_cast<ComputerPlayer*>(self); }
BlackBoardNote* AsNote(void* self) { return static_cast<BlackBoardNote*>(self); }

// The shape of the list methods 0x5517f0..0x551940: the word list
// function, then the count follows its result.
std::uint32_t AddToList(std::uint32_t id, std::uint16_t* list, std::size_t slots, std::uint16_t& count) {
    const std::uint32_t added = AI_WordListInsert(id, list, static_cast<std::uint32_t>(slots));
    if (added != 0) {
        ++count;
    }
    return added;
}

std::uint32_t RemoveFromList(std::uint32_t id, std::uint16_t* list, std::size_t slots, std::uint16_t& count) {
    const std::uint32_t removed = AI_WordListRemove(id, list, static_cast<std::uint32_t>(slots));
    if (removed != 0) {
        --count;
    }
    return removed;
}

// The id of an note as 0x5515c0 / 0x551610 push it: `mov dx, [note+8]`
// keeps the caller's EDX in the upper half.
std::uint32_t NoteIdWithCallerEdx(std::uint32_t note, void* edx) {
    const std::uint16_t id = reinterpret_cast<const BlackBoardNote*>(note)->id;
    return (reinterpret_cast<std::uint32_t>(edx) & 0xffff0000u) | id;
}

}  // namespace

// ---------------------------------------------------------------- id lists

S3RE_HOOK(0x005517f0, CComputerPlayer_AddRootNote, NoteIdFn, Original_CComputerPlayer_AddRootNote,
          CComputerPlayer_AddRootNote);

// Purpose: Records an note without a parent as one of the computer
// player's root notes (its top-level goals, probably).
std::uint32_t __fastcall CComputerPlayer_AddRootNote(void* self, void* /*edx*/, std::uint32_t id) {
    ComputerPlayer* player = AsPlayer(self);
    return AddToList(id, player->root_notes, kRootNoteSlots, player->root_note_count);
}

S3RE_HOOK(0x00551820, CComputerPlayer_RemoveRootNote, NoteIdFn, Original_CComputerPlayer_RemoveRootNote,
          CComputerPlayer_RemoveRootNote);

// Purpose: Forgets a root note (when it is deleted, 0x551330).
std::uint32_t __fastcall CComputerPlayer_RemoveRootNote(void* self, void* /*edx*/, std::uint32_t id) {
    ComputerPlayer* player = AsPlayer(self);
    return RemoveFromList(id, player->root_notes, kRootNoteSlots, player->root_note_count);
}

S3RE_HOOK(0x00551850, CComputerPlayer_AddPendingNote, NoteIdFn, Original_CComputerPlayer_AddPendingNote,
          CComputerPlayer_AddPendingNote);

// Purpose: Queues a new note (note) for its handler, which 0x5514c0 calls
// on the next step.
std::uint32_t __fastcall CComputerPlayer_AddPendingNote(void* self, void* /*edx*/, std::uint32_t id) {
    ComputerPlayer* player = AsPlayer(self);
    return AddToList(id, player->pending_notes, kPendingNoteSlots, player->pending_note_count);
}

S3RE_HOOK(0x00551880, CComputerPlayer_RemovePendingNote, NoteIdFn, Original_CComputerPlayer_RemovePendingNote,
          CComputerPlayer_RemovePendingNote);

// Purpose: Takes a note off the queue of new notes (handled or deleted).
std::uint32_t __fastcall CComputerPlayer_RemovePendingNote(void* self, void* /*edx*/, std::uint32_t id) {
    ComputerPlayer* player = AsPlayer(self);
    return RemoveFromList(id, player->pending_notes, kPendingNoteSlots, player->pending_note_count);
}

S3RE_HOOK(0x005518b0, CComputerPlayer_AddActiveNote, NoteIdFn, Original_CComputerPlayer_AddActiveNote,
          CComputerPlayer_AddActiveNote);

// Purpose: Adds an note to the computer player's active notes.
std::uint32_t __fastcall CComputerPlayer_AddActiveNote(void* self, void* /*edx*/, std::uint32_t id) {
    ComputerPlayer* player = AsPlayer(self);
    return AddToList(id, player->active_notes, kActiveNoteSlots, player->active_note_count);
}

S3RE_HOOK(0x005518e0, CComputerPlayer_RemoveActiveNote, NoteIdFn, Original_CComputerPlayer_RemoveActiveNote,
          CComputerPlayer_RemoveActiveNote);

// Purpose: Removes an note from the active notes.
std::uint32_t __fastcall CComputerPlayer_RemoveActiveNote(void* self, void* /*edx*/, std::uint32_t id) {
    ComputerPlayer* player = AsPlayer(self);
    return RemoveFromList(id, player->active_notes, kActiveNoteSlots, player->active_note_count);
}

S3RE_HOOK(0x00551940, CComputerPlayer_RemoveFromList200, NoteIdFn, Original_CComputerPlayer_RemoveFromList200,
          CComputerPlayer_RemoveFromList200);

// Purpose: Removes an id from the computer player's fourth id list (the one
// AI_List32Add fills; its meaning is not known yet).
std::uint32_t __fastcall CComputerPlayer_RemoveFromList200(void* self, void* /*edx*/, std::uint32_t id) {
    ComputerPlayer* player = AsPlayer(self);
    return RemoveFromList(id, player->list200, kList200Slots, player->list200_count);
}

// ---------------------------------------------------------------- activation

S3RE_HOOK(0x005515e0, CComputerPlayer_ActivateNote, NoteIdFn, Original_CComputerPlayer_ActivateNote,
          CComputerPlayer_ActivateNote);

// Purpose: Makes an note active: listed among the active notes and
// marked with the active flag.
std::uint32_t __fastcall CComputerPlayer_ActivateNote(void* self, void* /*edx*/, std::uint32_t id) {
    AI_SetCurrentPlayer(self);
    CComputerPlayer_AddActiveNote(self, nullptr, id);
    BlackBoardNote* note = AsPlayer(self)->notes[id & 0xffff];
    return BLACK_BOARD_NOTE_SetFlags(note, nullptr, kNoteActive);
}

S3RE_HOOK(0x005515c0, CComputerPlayer_ActivateNoteOf, NoteIdFn, Original_CComputerPlayer_ActivateNoteOf,
          CComputerPlayer_ActivateNoteOf);

// Purpose: ActivateNote for an note given by pointer.
std::uint32_t __fastcall CComputerPlayer_ActivateNoteOf(void* self, void* edx, std::uint32_t note) {
    return CComputerPlayer_ActivateNote(self, nullptr, NoteIdWithCallerEdx(note, edx));
}

S3RE_HOOK(0x00551630, CComputerPlayer_DeactivateNote, NoteIdFn, Original_CComputerPlayer_DeactivateNote,
          CComputerPlayer_DeactivateNote);

// Purpose: Makes an active note inactive again (nothing happens to an
// inactive one).
std::uint32_t __fastcall CComputerPlayer_DeactivateNote(void* self, void* /*edx*/, std::uint32_t id) {
    BlackBoardNote* note = AsPlayer(self)->notes[id & 0xffff];
    if (BLACK_BOARD_NOTE_HasFlags(note, nullptr, kNoteActive) == 0) {
        return 0;
    }
    CComputerPlayer_RemoveActiveNote(self, nullptr, id);
    return BLACK_BOARD_NOTE_ClearFlags(note, nullptr, kNoteActive);
}

S3RE_HOOK(0x00551610, CComputerPlayer_DeactivateNoteOf, NoteIdFn, Original_CComputerPlayer_DeactivateNoteOf,
          CComputerPlayer_DeactivateNoteOf);

// Purpose: DeactivateNote for an note given by pointer.
std::uint32_t __fastcall CComputerPlayer_DeactivateNoteOf(void* self, void* edx, std::uint32_t note) {
    return CComputerPlayer_DeactivateNote(self, nullptr, NoteIdWithCallerEdx(note, edx));
}

// ---------------------------------------------------------------- build priorities

S3RE_HOOK(0x00551b10, CComputerPlayer_RaiseBuildPriorities, NoteVoidFn,
          Original_CComputerPlayer_RaiseBuildPriorities, CComputerPlayer_RaiseBuildPriorities);

// Purpose: Lets every wanted building type become more urgent over time:
// each non-zero build priority grows by its type's step until it nears the
// type's cap (called at the end of 0x550f80 once its last counter wraps).
void __fastcall CComputerPlayer_RaiseBuildPriorities(void* self, void* /*edx*/) {
    for (std::uint32_t type = 0; type < kBuildingTypeCount; ++type) {
        // 0x551b1d: the node pointer is read again for every type.
        auto* data = reinterpret_cast<AiPlayerData*>(AsPlayer(self)->data);
        const std::uint16_t priority = data->best[type].score;
        if (priority == 0) {
            continue;
        }
        const std::uint16_t step = g_aiBuildPrioritySteps[type];
        const std::uint16_t cap = g_aiBuildPriorityCaps[type];
        if (static_cast<std::int32_t>(priority) < static_cast<std::int32_t>(cap) - static_cast<std::int32_t>(step)) {
            data->best[type].score = static_cast<std::uint16_t>(priority + step);
        }
    }
}

// ---------------------------------------------------------------- BLACK_BOARD_NOTE

S3RE_HOOK(0x00582400, BLACK_BOARD_NOTE_ResetLinks, NoteFn, Original_BLACK_BOARD_NOTE_ResetLinks,
          BLACK_BOARD_NOTE_ResetLinks);

// Purpose: Clears an note's flags and type and unlinks it (no id, parent,
// sibling or children yet); part of setting up a new note.
std::uint32_t __fastcall BLACK_BOARD_NOTE_ResetLinks(void* self, void* /*edx*/) {
    BlackBoardNote* note = AsNote(self);
    note->flags = 0;
    note->type = 0;
    note->unk0a = 0;
    note->unk0c = 0;
    note->first_child = kNoNote;
    note->next_sibling = kNoNote;
    note->parent = kNoNote;
    note->id = kNoNote;
    note->agent18 = kNoNote;
    note->agent16 = kNoNote;
    return kNoNote;
}

S3RE_HOOK(0x005823e0, BLACK_BOARD_NOTE_Init, NoteIdFn, Original_BLACK_BOARD_NOTE_Init, BLACK_BOARD_NOTE_Init);

// Purpose: Sets up a new note of a given type (called by the derived
// notes' constructors).
std::uint32_t __fastcall BLACK_BOARD_NOTE_Init(void* self, void* /*edx*/, std::uint32_t type) {
    BLACK_BOARD_NOTE_ResetLinks(self, nullptr);
    AsNote(self)->type = static_cast<std::uint16_t>(type);
    return type & 0xffff;
}

S3RE_HOOK(0x00582430, BLACK_BOARD_NOTE_MarkDispatched, NoteFn, Original_BLACK_BOARD_NOTE_MarkDispatched,
          BLACK_BOARD_NOTE_MarkDispatched);

// Purpose: Marks a note as handed to its handler (0x551510).
std::uint32_t __fastcall BLACK_BOARD_NOTE_MarkDispatched(void* self, void* /*edx*/) {
    return BLACK_BOARD_NOTE_SetFlags(self, nullptr, kNoteDispatched);
}

S3RE_HOOK(0x00582440, BLACK_BOARD_NOTE_SetFlags, NoteIdFn, Original_BLACK_BOARD_NOTE_SetFlags,
          BLACK_BOARD_NOTE_SetFlags);

// Purpose: Sets flag bits of an note.
std::uint32_t __fastcall BLACK_BOARD_NOTE_SetFlags(void* self, void* /*edx*/, std::uint32_t bits) {
    const auto low = static_cast<std::uint16_t>(bits);
    AsNote(self)->flags |= low;
    return low;  // AX; the original keeps the caller's upper half of EAX
}

S3RE_HOOK(0x00582450, BLACK_BOARD_NOTE_ClearFlags, NoteIdFn, Original_BLACK_BOARD_NOTE_ClearFlags,
          BLACK_BOARD_NOTE_ClearFlags);

// Purpose: Clears flag bits of an note.
std::uint32_t __fastcall BLACK_BOARD_NOTE_ClearFlags(void* self, void* /*edx*/, std::uint32_t bits) {
    AsNote(self)->flags &= static_cast<std::uint16_t>(~bits);
    return ~bits;
}

S3RE_HOOK(0x00582460, BLACK_BOARD_NOTE_HasFlags, NoteIdFn, Original_BLACK_BOARD_NOTE_HasFlags,
          BLACK_BOARD_NOTE_HasFlags);

// Purpose: Whether all the given flag bits of an note are set.
std::uint32_t __fastcall BLACK_BOARD_NOTE_HasFlags(void* self, void* /*edx*/, std::uint32_t bits) {
    const auto low = static_cast<std::uint16_t>(bits);
    return (AsNote(self)->flags & low) == low ? 1 : 0;
}

S3RE_HOOK(0x005824d0, BLACK_BOARD_NOTE_FindChildOfType, NoteIdFn, Original_BLACK_BOARD_NOTE_FindChildOfType,
          BLACK_BOARD_NOTE_FindChildOfType);

// Purpose: Finds a child note of a given type (e.g. whether a house
// request already has a building order; callers 0x5656e0, 0x5789b0).
std::uint32_t __fastcall BLACK_BOARD_NOTE_FindChildOfType(void* self, void* /*edx*/, std::uint32_t type) {
    std::uint16_t child = AsNote(self)->first_child;
    if (child == kNoNote) {
        return 0;
    }
    ComputerPlayer* player = g_aiPlayer;
    do {
        BlackBoardNote* candidate = player->notes[child];
        if (candidate->type == static_cast<std::uint16_t>(type)) {
            return reinterpret_cast<std::uint32_t>(candidate);
        }
        child = candidate->next_sibling;
    } while (child != kNoNote);
    return 0;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// One buffer per side holds a computer player, a pool of 16 notes (ids
// 0..15; ids 16..299 are null) and an AiPlayerData node. Pointers are set
// up per side and turned back into buffer offsets before the comparison.
// The globals AI_SetCurrentPlayer writes are saved and restored; after each
// call g_aiPlayer must point at that side's player and the other three
// globals must agree.

namespace {

struct ComputerPlayerFunctions {
    NoteIdFn list[7];
    NoteIdFn activate;
    NoteIdFn activateOf;
    NoteIdFn deactivate;
    NoteIdFn deactivateOf;
    NoteVoidFn raise;
    NoteFn resetLinks;
    NoteIdFn init;
    NoteFn markDispatched;
    NoteIdFn setFlags;
    NoteIdFn clearFlags;
    NoteIdFn hasFlags;
    NoteIdFn findChild;
};

ComputerPlayerFunctions OriginalComputerPlayer() {
    return {{Original_CComputerPlayer_AddRootNote, Original_CComputerPlayer_RemoveRootNote,
             Original_CComputerPlayer_AddPendingNote, Original_CComputerPlayer_RemovePendingNote,
             Original_CComputerPlayer_AddActiveNote, Original_CComputerPlayer_RemoveActiveNote,
             Original_CComputerPlayer_RemoveFromList200},
            Original_CComputerPlayer_ActivateNote,
            Original_CComputerPlayer_ActivateNoteOf,
            Original_CComputerPlayer_DeactivateNote,
            Original_CComputerPlayer_DeactivateNoteOf,
            Original_CComputerPlayer_RaiseBuildPriorities,
            Original_BLACK_BOARD_NOTE_ResetLinks,
            Original_BLACK_BOARD_NOTE_Init,
            Original_BLACK_BOARD_NOTE_MarkDispatched,
            Original_BLACK_BOARD_NOTE_SetFlags,
            Original_BLACK_BOARD_NOTE_ClearFlags,
            Original_BLACK_BOARD_NOTE_HasFlags,
            Original_BLACK_BOARD_NOTE_FindChildOfType};
}

ComputerPlayerFunctions OurComputerPlayer() {
    return {{&CComputerPlayer_AddRootNote, &CComputerPlayer_RemoveRootNote, &CComputerPlayer_AddPendingNote,
             &CComputerPlayer_RemovePendingNote, &CComputerPlayer_AddActiveNote,
             &CComputerPlayer_RemoveActiveNote, &CComputerPlayer_RemoveFromList200},
            &CComputerPlayer_ActivateNote,
            &CComputerPlayer_ActivateNoteOf,
            &CComputerPlayer_DeactivateNote,
            &CComputerPlayer_DeactivateNoteOf,
            &CComputerPlayer_RaiseBuildPriorities,
            &BLACK_BOARD_NOTE_ResetLinks,
            &BLACK_BOARD_NOTE_Init,
            &BLACK_BOARD_NOTE_MarkDispatched,
            &BLACK_BOARD_NOTE_SetFlags,
            &BLACK_BOARD_NOTE_ClearFlags,
            &BLACK_BOARD_NOTE_HasFlags,
            &BLACK_BOARD_NOTE_FindChildOfType};
}

constexpr std::size_t kPoolNotes = 16;
constexpr std::size_t kNoteStride = 0x20;
constexpr std::size_t kPoolOffset = 0x800;
constexpr std::size_t kDataOffset = kPoolOffset + kPoolNotes * kNoteStride;
constexpr std::size_t kDataBytes = 0x400;
constexpr std::size_t kBufferBytes = kDataOffset + kDataBytes;
static_assert(sizeof(ComputerPlayer) <= kPoolOffset && sizeof(BlackBoardNote) <= kNoteStride);
static_assert(offsetof(AiPlayerData, best) + kBuildingTypeCount * sizeof(AiCandidate) <= kDataBytes);

BlackBoardNote* PoolNote(std::uint8_t* buffer, std::size_t id) {
    return reinterpret_cast<BlackBoardNote*>(buffer + kPoolOffset + id * kNoteStride);
}

// Pointers of one side: notes 0..15 into the pool, the data node.
void Link(std::uint8_t* buffer) {
    auto* player = reinterpret_cast<ComputerPlayer*>(buffer);
    for (std::size_t id = 0; id < kMaxNotes; ++id) {
        player->notes[id] = id < kPoolNotes ? PoolNote(buffer, id) : nullptr;
    }
    player->data = reinterpret_cast<std::uint32_t>(buffer + kDataOffset);
}

// The same pointers as buffer offsets (for the comparison).
void Unlink(std::uint8_t* buffer) {
    auto* player = reinterpret_cast<ComputerPlayer*>(buffer);
    const auto base = reinterpret_cast<std::uint32_t>(buffer);
    for (BlackBoardNote*& note : player->notes) {
        if (note != nullptr) {
            note = reinterpret_cast<BlackBoardNote*>(reinterpret_cast<std::uint32_t>(note) - base);
        }
    }
    player->data -= base;
}

std::uint16_t SmallId(test::Rng& rng) {
    return rng.Chance(1, 4) ? kNoNote : static_cast<std::uint16_t>(rng.Below(kPoolNotes));
}

void FillCase(test::Rng& rng, std::vector<std::uint8_t>& buffer) {
    rng.Fill(buffer.data(), buffer.size());
    auto* player = reinterpret_cast<ComputerPlayer*>(buffer.data());
    // Id lists: small ids, often a free tail, sometimes full.
    auto fillList = [&](std::uint16_t* list, std::size_t slots) {
        const std::size_t used = rng.Chance(1, 6) ? slots : rng.Below(static_cast<std::uint32_t>(slots));
        for (std::size_t k = 0; k < slots; ++k) {
            list[k] = k < used ? static_cast<std::uint16_t>(rng.Below(24)) : 0xffff;
        }
    };
    fillList(player->root_notes, kRootNoteSlots);
    fillList(player->pending_notes, kPendingNoteSlots);
    fillList(player->active_notes, kActiveNoteSlots);
    fillList(player->list200, kList200Slots);
    player->setup_index = rng.Below(64);
    // Agents: flags with bits 1 and 4 often set; children chains that only
    // go to higher ids (so they end).
    for (std::size_t id = 0; id < kPoolNotes; ++id) {
        BlackBoardNote* note = PoolNote(buffer.data(), id);
        note->flags = static_cast<std::uint16_t>(rng.Chance(1, 2) ? rng.Below(8) : rng.Next32());
        note->type = static_cast<std::uint16_t>(rng.Chance(1, 8) ? rng.Next32() : rng.Below(4));
        note->id = static_cast<std::uint16_t>(rng.Chance(1, 2) ? id : rng.Below(kPoolNotes));
        note->first_child = SmallId(rng);
        const std::uint32_t later = static_cast<std::uint32_t>(kPoolNotes - id - 1);
        note->next_sibling = later == 0 || rng.Chance(1, 4)
                                  ? kNoNote
                                  : static_cast<std::uint16_t>(id + 1 + rng.Below(later));
    }
    // Build priorities: zero, small, or near the cap.
    auto* data = reinterpret_cast<AiPlayerData*>(buffer.data() + kDataOffset);
    for (std::uint32_t type = 0; type < kBuildingTypeCount; ++type) {
        const std::uint32_t kind = rng.Below(4);
        const std::int32_t edge = static_cast<std::int32_t>(g_aiBuildPriorityCaps[type]) -
                                  static_cast<std::int32_t>(g_aiBuildPrioritySteps[type]);
        data->best[type].score = static_cast<std::uint16_t>(kind == 0   ? 0
                                                            : kind == 1 ? rng.Below(2000)
                                                            : kind == 2 ? edge + rng.Range(-2, 2)
                                                                        : rng.Next32());
    }
}

struct AiGlobals {
    ComputerPlayer* player;
    std::uint8_t number;
    AiPlayerSetup* setup;
    AiPlayerData* data;
};

AiGlobals SaveGlobals() { return {g_aiPlayer, g_aiPlayerNumber, g_aiPlayerSetup, g_aiPlayerData}; }

void RestoreGlobals(const AiGlobals& saved) {
    g_aiPlayer = saved.player;
    g_aiPlayerNumber = saved.number;
    g_aiPlayerSetup = saved.setup;
    g_aiPlayerData = saved.data;
}

void CompareComputerPlayer(test::Context& ctx, const ComputerPlayerFunctions& candidate, int cases) {
    const ComputerPlayerFunctions original = OriginalComputerPlayer();
    const AiGlobals saved = SaveGlobals();
    std::vector<std::uint8_t> a(kBufferBytes), b(kBufferBytes);
    constexpr int kOps = 19;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(kOps));
        FillCase(ctx.rng(), a);
        b = a;
        Link(a.data());
        Link(b.data());
        // Ids: noise in the upper half sometimes; (de)activation needs an
        // note behind the id (the original dereferences notes[id]).
        const bool needsNote = op == 7 || op == 9;
        const std::uint32_t id = (ctx.rng().Chance(1, 3) ? ctx.rng().Next32() & 0xffff0000u : 0) |
                                 (needsNote || ctx.rng().Chance(1, 2) ? ctx.rng().Below(kPoolNotes)
                                                                       : ctx.rng().Below(24));
        const std::uint32_t noteIndex = ctx.rng().Below(kPoolNotes);
        const std::uint32_t bits = ctx.rng().Chance(1, 2) ? 1u << ctx.rng().Below(4) : ctx.rng().Next32();
        auto* edx = reinterpret_cast<void*>(ctx.rng().Next32());
        RestoreGlobals(saved);
        std::uint32_t ra = 0, rb = 0;
        std::uint32_t eaxMask = 0xffffffffu;
        AiGlobals ga{}, gb{};
        // Runs the same call on both sides (note methods on note
        // `noteIndex`, player methods on the player).
        auto run = [&](auto&& call) {
            g_aiPlayer = reinterpret_cast<ComputerPlayer*>(a.data());
            ra = call(original, a.data());
            ga = SaveGlobals();
            g_aiPlayer = reinterpret_cast<ComputerPlayer*>(b.data());
            rb = call(candidate, b.data());
            gb = SaveGlobals();
        };
        auto noteOf = [&](std::uint8_t* buffer) { return PoolNote(buffer, noteIndex); };
        auto noteArg = [&](std::uint8_t* buffer) { return reinterpret_cast<std::uint32_t>(noteOf(buffer)); };
        if (op < 7) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) { return f.list[op](p, nullptr, id); });
        } else if (op == 7) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) { return f.activate(p, nullptr, id); });
        } else if (op == 8) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) { return f.activateOf(p, edx, noteArg(p)); });
        } else if (op == 9) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) { return f.deactivate(p, nullptr, id); });
        } else if (op == 10) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) {
                return f.deactivateOf(p, edx, noteArg(p));
            });
        } else if (op == 11) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) {
                f.raise(p, nullptr);
                return 0u;
            });
        } else if (op == 12) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) { return f.resetLinks(noteOf(p), nullptr); });
        } else if (op == 13) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) { return f.init(noteOf(p), nullptr, bits); });
        } else if (op == 14) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) {
                return f.markDispatched(noteOf(p), nullptr);
            });
            eaxMask = 0xffff;  // the upper half is the caller's EAX
        } else if (op == 15) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) {
                return f.setFlags(noteOf(p), nullptr, bits);
            });
            eaxMask = 0xffff;
        } else if (op == 16) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) {
                return f.clearFlags(noteOf(p), nullptr, bits);
            });
        } else if (op == 17) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) { return f.hasFlags(noteOf(p), nullptr, bits); });
        } else {
            const std::uint32_t type = ctx.rng().Chance(1, 4) ? ctx.rng().Next32() : ctx.rng().Below(4);
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) {
                return f.findChild(noteOf(p), nullptr, type);
            });
            // A found note: compare its offset in the buffer.
            if (ra != 0) {
                ra -= reinterpret_cast<std::uint32_t>(a.data());
            }
            if (rb != 0) {
                rb -= reinterpret_cast<std::uint32_t>(b.data());
            }
        }
        RestoreGlobals(saved);
        Unlink(a.data());
        Unlink(b.data());
        bool same = S3RE_EXPECT_EQ(ra & eaxMask, rb & eaxMask);
        same &= S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size());
        // AI_SetCurrentPlayer's globals (ActivateNote): each side's player,
        // the same number, setup and data pointer.
        same &= S3RE_EXPECT_EQ(reinterpret_cast<std::uint32_t>(ga.player) - reinterpret_cast<std::uint32_t>(a.data()),
                               reinterpret_cast<std::uint32_t>(gb.player) - reinterpret_cast<std::uint32_t>(b.data()));
        same &= S3RE_EXPECT_EQ(ga.number, gb.number);
        same &= S3RE_EXPECT_EQ(reinterpret_cast<std::uint32_t>(ga.setup), reinterpret_cast<std::uint32_t>(gb.setup));
        const auto dataA = reinterpret_cast<std::uint32_t>(ga.data), dataB = reinterpret_cast<std::uint32_t>(gb.data);
        if (op == 7 || op == 8) {
            same &= S3RE_EXPECT_EQ(dataA - reinterpret_cast<std::uint32_t>(a.data()),
                                   dataB - reinterpret_cast<std::uint32_t>(b.data()));
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
        }
    }
    RestoreGlobals(saved);
}

std::uint32_t __fastcall WrongDeactivateNote(void* self, void* /*edx*/, std::uint32_t id) {
    // Wrong: removes the note from the list even when it is not active.
    CComputerPlayer_RemoveActiveNote(self, nullptr, id);
    return CComputerPlayer_DeactivateNote(self, nullptr, id);
}

void __fastcall WrongRaiseBuildPriorities(void* self, void* /*edx*/) {
    // Wrong: an unsigned comparison (differs where cap < step: never in the
    // table) and <= instead of <.
    auto* data = reinterpret_cast<AiPlayerData*>(AsPlayer(self)->data);
    for (std::uint32_t type = 0; type < kBuildingTypeCount; ++type) {
        const std::uint16_t priority = data->best[type].score;
        if (priority != 0 && priority <= g_aiBuildPriorityCaps[type] - g_aiBuildPrioritySteps[type]) {
            data->best[type].score = static_cast<std::uint16_t>(priority + g_aiBuildPrioritySteps[type]);
        }
    }
}

std::uint32_t __fastcall WrongFindChildOfType(void* self, void* /*edx*/, std::uint32_t type) {
    // Wrong: stops after the first child.
    const std::uint16_t child = AsNote(self)->first_child;
    if (child == kNoNote) {
        return 0;
    }
    BlackBoardNote* candidate = g_aiPlayer.get()->notes[child];
    return candidate->type == static_cast<std::uint16_t>(type) ? reinterpret_cast<std::uint32_t>(candidate) : 0;
}

}  // namespace

S3RE_TEST(AiComputerPlayer_MatchOriginal) {
    CompareComputerPlayer(ctx, OurComputerPlayer(), 40000);
}

S3RE_TEST(AiComputerPlayer_ComparisonDetectsWrongImplementation) {
    const auto probe = [&](auto change) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        ComputerPlayerFunctions wrong = OurComputerPlayer();
        change(wrong);
        CompareComputerPlayer(quiet, wrong, 20000);
        S3RE_EXPECT(quiet.failures() > 0);
    };
    probe([](ComputerPlayerFunctions& f) { f.deactivate = &WrongDeactivateNote; });
    probe([](ComputerPlayerFunctions& f) { f.raise = &WrongRaiseBuildPriorities; });
    probe([](ComputerPlayerFunctions& f) { f.findChild = &WrongFindChildOfType; });
    probe([](ComputerPlayerFunctions& f) { f.list[3] = f.list[2]; });
}

}  // namespace s3re::game
