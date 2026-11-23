// CComputerPlayer and its blackboard notes (computer players, batch 1 of
// the AI; re/computer_players.md). The CComputerPlayer methods are in an
// unnamed source file before Rainer\Cevaland.cpp (object unit 57,
// 0x550900..0x553d30); the BLACK_BOARD_NOTE helpers sit between
// Rainer\CompHouseReq.cpp and Rainer\CompMapWorkerAgent.cpp.
//
// A computer player keeps its notes (BLACK_BOARD_NOTE and the derived
// request notes, RTTI) by id in ComputerPlayer::notes and four id lists (ai_layout.h):
// root notes, pending notes, active notes and the notes to delete
// (doomed_notes).
// The list functions are thiscall with one argument (ret 4):
//   0x5517f0 AddRootNote(id)       AI_WordListInsert(id, root_notes, 100), on success ++root_note_count
//   0x551820 RemoveRootNote(id)    AI_WordListRemove(...), on success --root_note_count
//   0x551850 AddPendingNote(id)    pending_notes (50), pending_note_count
//   0x551880 RemovePendingNote(id)
//   0x5518b0 AddActiveNote(id)     active_notes (100), active_note_count
//   0x5518e0 RemoveActiveNote(id)
//   0x551940 RemoveDoomedNote(id)  doomed_notes (50), doomed_note_count (the
//                                   add is AI_List32Add 0x551910, ai_helpers.cpp)
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
// Package A3, the note tree and the agents (thiscall unless noted; EAX
// unused by the callers unless given):
// 0x551270 AddNote(note, parent id, requester agent) ret 0xc: first free id
//   (0..299; none: EAX = 299, nothing else happens); note->id, notes[id];
//   parent 0xffff: AddRootNote, else in front of the parent's children;
//   parent, requester = the agent's kind (or 0xffff), flags = 0; EAX =
//   AddPendingNote's result.
// 0x551230 AddNoteUnder(note, parent note or 0, agent) ret 0xc: AddNote with
//   the parent's id.
// 0x551330 DeleteNote(id) ret 4: unlinks the note (RemoveRootNote or out of
//   the parent's children), deletes the children, RemovePendingNote,
//   DeactivateNote, notes[id] = 0, slot 0 (deleting destructor, 1). EAX: the
//   destructor's, or id & 0xffff for a free id.
// 0x551400 AddAgent(agent) ret 4: agents[kind] = agent; slot 2 writes the
//   note types it handles (AX = count); each type without a handler gets it.
// 0x551490 DeleteAgent(agent) ret 4: agents[kind] = 0, slot 0 (1).
// 0x551510 DispatchNote(note) ret 4: no handler for its type: DeleteNote;
//   else MarkDispatched and the handler's slot 3 (note).
// 0x5514c0 DispatchPendingNotes(): DispatchNote for each pending note.
// 0x551590 RunAgents(): each agent's slot 8 if its slot 7 says so.
// 0x5516e0 WorkOnNote(note) ret 4: handler's slot 4 unless its slot 5 says
//   busy, then DeleteDoomedNotes (not for a type without handler).
// 0x551670 WorkOnNextActiveNote(): the next active note (active_cursor).
// 0x551790 DeleteDoomedNotes(): DeleteNote for each doomed note.
// 0x5517d0 UpdateBlackboard(): RunAgents, DispatchPendingNotes,
//   WorkOnNextActiveNote.
// 0x550f30 Shutdown(): deletes the root notes and the agents, then
//   CEvaLand::Release (tail call).
// 0x5509d0 ~CComputerPlayer, 0x5509b0 its scalar deleting destructor.
// 0x551a70 StartBestBuilding(): see there (0x550f80 step 7).
// 0x582480 BLACK_BOARD_NOTE::ReportToRequester(result, value) ret 8 and
//   0x582510 CancelAtHandler(): the note callbacks (agent slots 9 and 10).
// Agent slots: ai_layout.h agent_slot. Reimplementations call ours
// directly; the tests run the original side entirely as original code
// (ScopedJump to the trampolines).
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

#include "game/ai/ai_layout.h"
#include "game/enums/buildings.h"
#include "game/globals.h"
#include "game/tables.h"
#include "game/vtables.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
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
using NoteVoidArgFn = void(__fastcall*)(void* self, void* edx, std::uint32_t arg);
using AddNoteFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t note, std::uint32_t parent,
                                             std::uint32_t requester);
using NoteReportFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t result, std::uint32_t value);
using DeletingDestroyFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t flags);

std::uint32_t __fastcall CComputerPlayer_AddRootNote(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_RemoveRootNote(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_AddPendingNote(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_RemovePendingNote(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_AddActiveNote(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_RemoveActiveNote(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_RemoveDoomedNote(void* self, void* edx, std::uint32_t id);
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
// Package A3: the note tree, the agents, destruction.
std::uint32_t __fastcall CComputerPlayer_AddNote(void* self, void* edx, std::uint32_t note, std::uint32_t parent,
                                                 std::uint32_t requester);
std::uint32_t __fastcall CComputerPlayer_AddNoteUnder(void* self, void* edx, std::uint32_t note,
                                                      std::uint32_t parent, std::uint32_t requester);
std::uint32_t __fastcall CComputerPlayer_DeleteNote(void* self, void* edx, std::uint32_t id);
void __fastcall CComputerPlayer_AddAgent(void* self, void* edx, std::uint32_t agent);
void __fastcall CComputerPlayer_DeleteAgent(void* self, void* edx, std::uint32_t agent);
void __fastcall CComputerPlayer_DispatchNote(void* self, void* edx, std::uint32_t note);
void __fastcall CComputerPlayer_DispatchPendingNotes(void* self, void* edx);
void __fastcall CComputerPlayer_RunAgents(void* self, void* edx);
void __fastcall CComputerPlayer_WorkOnNote(void* self, void* edx, std::uint32_t note);
void __fastcall CComputerPlayer_WorkOnNextActiveNote(void* self, void* edx);
void __fastcall CComputerPlayer_DeleteDoomedNotes(void* self, void* edx);
void __fastcall CComputerPlayer_UpdateBlackboard(void* self, void* edx);
void __fastcall CComputerPlayer_Shutdown(void* self, void* edx);
void __fastcall CComputerPlayer_Destroy(void* self, void* edx);
void* __fastcall CComputerPlayer_DeletingDestroy(void* self, void* edx, std::uint32_t flags);
void __fastcall CComputerPlayer_StartBestBuilding(void* self, void* edx);
std::uint32_t __fastcall BLACK_BOARD_NOTE_ReportToRequester(void* self, void* edx, std::uint32_t result,
                                                            std::uint32_t value);
std::uint32_t __fastcall BLACK_BOARD_NOTE_CancelAtHandler(void* self, void* edx);

// ai_evaland.cpp, win32_classes.cpp
std::uint32_t __cdecl AI_EvaLand_Release();
void __fastcall Win32_CObject_Destroy(void* self, void* edx);

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

S3RE_HOOK(0x00551940, CComputerPlayer_RemoveDoomedNote, NoteIdFn, Original_CComputerPlayer_RemoveDoomedNote,
          CComputerPlayer_RemoveDoomedNote);

// Purpose: Removes an id from the computer player's fourth id list (the one
// AI_List32Add fills; its meaning is not known yet).
std::uint32_t __fastcall CComputerPlayer_RemoveDoomedNote(void* self, void* /*edx*/, std::uint32_t id) {
    ComputerPlayer* player = AsPlayer(self);
    return RemoveFromList(id, player->doomed_notes, kDoomedNoteSlots, player->doomed_note_count);
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
    note->handler = kNoAgent;
    note->requester = kNoAgent;
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

// ---------------------------------------------------------------- the note tree (package A3)

namespace {

// A virtual call of an agent or note (thiscall, callee pops); arguments are
// dwords, pointers passed as their addresses.
template <typename... A>
std::uint32_t CallSlot(void* object, std::size_t slot, A... args) {
    using Fn = std::uint32_t(__fastcall*)(void*, void*, A...);
    const void* const* vtable = *static_cast<const void* const* const*>(object);
    return reinterpret_cast<Fn>(vtable[slot])(object, nullptr, args...);
}

std::uint32_t Address(const void* pointer) { return reinterpret_cast<std::uint32_t>(pointer); }

// The scalar deleting destructor of a note or agent, with "free it".
std::uint32_t DeleteObject(void* object) {
    return CallSlot(object, agent_slot::kDeletingDestructor, 1u);
}

// 0x551351..0x5513b6 of DeleteNote: takes a note out of its parent's
// children (a singly linked list from first_child over next_sibling), or
// out of the root notes.
void UnlinkNote(ComputerPlayer* player, const BlackBoardNote* note, std::uint32_t id) {
    if (note->parent == kNoNote) {
        CComputerPlayer_RemoveRootNote(player, nullptr, id);
        return;
    }
    const auto ownId = static_cast<std::uint16_t>(id);
    BlackBoardNote* parent = player->notes[note->parent];
    if (parent->first_child == ownId) {
        parent->first_child = note->next_sibling;
        return;
    }
    for (std::uint16_t child = parent->first_child; child != kNoNote;) {
        BlackBoardNote* sibling = player->notes[child];
        if (sibling->next_sibling == ownId) {
            sibling->next_sibling = note->next_sibling;
            return;
        }
        child = sibling->next_sibling;
    }
}

// Building types StartBestBuilding may start while the economy lacks wood /
// stone (AiPlayerData::short_of_wood / short_of_stone; 0x551ad4..0x551afe).
bool HelpsAgainstStoneShortage(std::uint32_t type) {
    return type == Value(BuildingType::Stonecutter) || type == Value(BuildingType::SmallTower);
}

bool HelpsAgainstWoodShortage(std::uint32_t type) {
    return type == Value(BuildingType::Woodcutter) || type == Value(BuildingType::Sawmill) ||
           type == Value(BuildingType::Forester) || type == Value(BuildingType::SmallTower);
}

// Callees of StartBestBuilding that package A2 owns (not reimplemented
// yet), called through their S3.EXE addresses (tests replace them).
//   0x59dbb0 (fastcall: const u8* player, const u32* area): the number of
//     the player's construction sites (building byte +0x21 set) in the area
//     (word compare); AX.
//   0x58ecb0 (fastcall: type): moves g_aiPlayerData->best[type] to
//     best[57 + type] and clears it (the candidate is handed on).
using CountSitesFn = std::uint32_t(__fastcall*)(const std::uint8_t* player, const std::uint32_t* area);
using CommitCandidateFn = std::uint32_t(__fastcall*)(std::uint32_t type);
constexpr std::uintptr_t kCountSitesInArea = 0x0059dbb0;
constexpr std::uintptr_t kCommitCandidate = 0x0058ecb0;

constexpr std::uint16_t kMaxConstructionSites = 4;  // StartBestBuilding waits while the area has this many
constexpr std::uint16_t kTopPriority = 65000;       // the search for the best type stops at it (0x551abe)
constexpr std::size_t kMaxAgentNoteTypes = 50;      // AddAgent's 0x64-byte buffer for agent slot 2

}  // namespace

S3RE_HOOK(0x00551270, CComputerPlayer_AddNote, AddNoteFn, Original_CComputerPlayer_AddNote, CComputerPlayer_AddNote);

// Purpose: Puts a new note (a request) on the computer player's
// blackboard: gives it the first free id, hangs it below its parent note
// (as the first child) or makes it a root note, remembers which agent asked
// for it and queues it for its handler (DispatchPendingNotes, next step).
std::uint32_t __fastcall CComputerPlayer_AddNote(void* self, void* /*edx*/, std::uint32_t noteAddress,
                                                 std::uint32_t parent, std::uint32_t requesterAddress) {
    ComputerPlayer* player = AsPlayer(self);
    AI_SetCurrentPlayer(self);
    std::uint16_t id = 0;
    while (player->notes[id] != nullptr) {
        if (++id == kMaxNotes) {
            return kMaxNotes - 1;  // all 300 ids taken: nothing happens (EAX: the last id tried)
        }
    }
    auto* note = reinterpret_cast<BlackBoardNote*>(noteAddress);
    note->id = id;
    player->notes[id] = note;
    const auto parentId = static_cast<std::uint16_t>(parent);
    if (parentId == kNoNote) {
        CComputerPlayer_AddRootNote(self, nullptr, id);
    } else {
        // In front of the parent's other children (0x5512f2); the first
        // child keeps whatever next_sibling it had.
        BlackBoardNote* parentNote = player->notes[parentId];
        if (parentNote->first_child != kNoNote) {
            note->next_sibling = parentNote->first_child;
        }
        parentNote->first_child = id;
    }
    note->parent = parentId;
    const auto* requester = reinterpret_cast<const BlackBoardAgent*>(requesterAddress);
    note->requester = requester != nullptr ? requester->kind : kNoAgent;
    note->flags = 0;
    return CComputerPlayer_AddPendingNote(self, nullptr, id);
}

S3RE_HOOK(0x00551230, CComputerPlayer_AddNoteUnder, AddNoteFn, Original_CComputerPlayer_AddNoteUnder,
          CComputerPlayer_AddNoteUnder);

// Purpose: AddNote with the parent given as a note (or none): how the
// agents put their sub-requests on the blackboard (0x5654d0, 0x5787c0).
std::uint32_t __fastcall CComputerPlayer_AddNoteUnder(void* self, void* /*edx*/, std::uint32_t note,
                                                      std::uint32_t parentAddress, std::uint32_t requester) {
    const auto* parent = reinterpret_cast<const BlackBoardNote*>(parentAddress);
    const std::uint32_t parentId = parent != nullptr ? parent->id : kNoNote;
    return CComputerPlayer_AddNote(self, nullptr, note, parentId, requester);
}

S3RE_HOOK(0x00551330, CComputerPlayer_DeleteNote, NoteIdFn, Original_CComputerPlayer_DeleteNote,
          CComputerPlayer_DeleteNote);

// Purpose: Takes a note off the blackboard together with all notes below
// it (a request that is done or given up, with its sub-requests): unlinks
// it, deletes its children first, drops it from the pending and active
// notes and destroys the object.
std::uint32_t __fastcall CComputerPlayer_DeleteNote(void* self, void* /*edx*/, std::uint32_t id) {
    ComputerPlayer* player = AsPlayer(self);
    AI_SetCurrentPlayer(self);
    BlackBoardNote*& slot = player->notes[id & 0xffff];
    BlackBoardNote* note = slot;
    if (note == nullptr) {
        return id & 0xffff;
    }
    UnlinkNote(player, note, id);
    // Each child unlinks itself, so first_child moves on (0x5513ba). The
    // original pushes the child id with the upper half of the previous
    // call's EAX; every callee uses only the low word.
    while (note->first_child != kNoNote) {
        CComputerPlayer_DeleteNote(self, nullptr, note->first_child);
    }
    CComputerPlayer_RemovePendingNote(self, nullptr, id);
    CComputerPlayer_DeactivateNote(self, nullptr, id);
    slot = nullptr;
    return DeleteObject(note);
}

S3RE_HOOK(0x00551400, CComputerPlayer_AddAgent, NoteVoidArgFn, Original_CComputerPlayer_AddAgent,
          CComputerPlayer_AddAgent);

// Purpose: Gives the computer player one of its agents (from its
// constructor 0x550a30, one per kind): stores it by kind and makes it the
// handler of each note type it asks for that has none yet.
void __fastcall CComputerPlayer_AddAgent(void* self, void* /*edx*/, std::uint32_t agentAddress) {
    ComputerPlayer* player = AsPlayer(self);
    AI_SetCurrentPlayer(self);
    auto* agent = reinterpret_cast<BlackBoardAgent*>(agentAddress);
    player->agents[agent->kind] = agent;
    // Only the first `count` entries are written by the agent (the original
    // would read its own stack beyond 50; real agents return 1 or so).
    std::uint16_t types[kMaxAgentNoteTypes];
    const auto count = static_cast<std::uint16_t>(CallSlot(agent, agent_slot::kNoteTypes, Address(types)));
    for (std::uint16_t k = 0; k < count; ++k) {
        // One handler per type (the original loops over one slot).
        std::uint16_t& handler = player->handlers[types[k]];
        if (handler == kNoAgent) {
            handler = agent->kind;
        }
    }
}

S3RE_HOOK(0x00551490, CComputerPlayer_DeleteAgent, NoteVoidArgFn, Original_CComputerPlayer_DeleteAgent,
          CComputerPlayer_DeleteAgent);

// Purpose: Removes one of the computer player's agents and destroys it
// (Shutdown, for each agent).
void __fastcall CComputerPlayer_DeleteAgent(void* self, void* /*edx*/, std::uint32_t agentAddress) {
    ComputerPlayer* player = AsPlayer(self);
    AI_SetCurrentPlayer(self);
    auto* agent = reinterpret_cast<BlackBoardAgent*>(agentAddress);
    player->agents[agent->kind] = nullptr;  // read before the null check, as in the original (0x5514a0)
    if (agent != nullptr) {
        DeleteObject(agent);
    }
}

S3RE_HOOK(0x00551510, CComputerPlayer_DispatchNote, NoteVoidArgFn, Original_CComputerPlayer_DispatchNote,
          CComputerPlayer_DispatchNote);

// Purpose: Hands a new note to the agent that handles its type (agent
// slot 3); a note that no agent handles is deleted.
void __fastcall CComputerPlayer_DispatchNote(void* self, void* /*edx*/, std::uint32_t noteAddress) {
    ComputerPlayer* player = AsPlayer(self);
    auto* note = reinterpret_cast<BlackBoardNote*>(noteAddress);
    const std::uint16_t handler = player->handlers[note->type];
    if (handler == kNoAgent) {
        CComputerPlayer_DeleteNote(self, nullptr, note->id);
        return;
    }
    BLACK_BOARD_NOTE_MarkDispatched(note, nullptr);
    // The original loops over one handler slot per type and stops early on
    // kAgentDone (0x551576), so it is one call.
    CallSlot(player->agents[handler], agent_slot::kTakeNote, noteAddress);
}

S3RE_HOOK(0x005514c0, CComputerPlayer_DispatchPendingNotes, NoteVoidFn,
          Original_CComputerPlayer_DispatchPendingNotes, CComputerPlayer_DispatchPendingNotes);

// Purpose: Hands every note queued since the last step to its handler
// (UpdateBlackboard, once per computer-player step).
void __fastcall CComputerPlayer_DispatchPendingNotes(void* self, void* /*edx*/) {
    ComputerPlayer* player = AsPlayer(self);
    AI_SetCurrentPlayer(self);
    while (player->pending_notes[0] != kNoNote) {
        const std::uint16_t id = player->pending_notes[0];
        CComputerPlayer_DispatchNote(self, nullptr, Address(player->notes[id]));
        CComputerPlayer_RemovePendingNote(self, nullptr, id);
    }
}

S3RE_HOOK(0x00551590, CComputerPlayer_RunAgents, NoteVoidFn, Original_CComputerPlayer_RunAgents,
          CComputerPlayer_RunAgents);

// Purpose: Lets each agent do its own work if it wants to (agent slots 7
// and 8), once per computer-player step.
void __fastcall CComputerPlayer_RunAgents(void* self, void* /*edx*/) {
    for (BlackBoardAgent* agent : AsPlayer(self)->agents) {
        if (agent != nullptr && CallSlot(agent, agent_slot::kWantsToRun) != 0) {
            CallSlot(agent, agent_slot::kRun);
        }
    }
}

S3RE_HOOK(0x005516e0, CComputerPlayer_WorkOnNote, NoteVoidArgFn, Original_CComputerPlayer_WorkOnNote,
          CComputerPlayer_WorkOnNote);

// Purpose: Gives the handler of an active note a turn on it (agent slot 4,
// unless the agent says it is busy), then deletes the notes marked for
// deletion. A note whose type has no handler is left alone, and the
// deletions wait.
void __fastcall CComputerPlayer_WorkOnNote(void* self, void* /*edx*/, std::uint32_t noteAddress) {
    ComputerPlayer* player = AsPlayer(self);
    const auto* note = reinterpret_cast<const BlackBoardNote*>(noteAddress);
    if (note != nullptr) {
        const std::uint16_t handler = player->handlers[note->type];
        if (handler == kNoAgent) {
            return;
        }
        // One handler slot per type, as in DispatchNote.
        BlackBoardAgent* agent = player->agents[handler];
        if (CallSlot(agent, agent_slot::kBusy) == 0) {
            CallSlot(agent, agent_slot::kWorkOnNote, noteAddress);
        }
    }
    CComputerPlayer_DeleteDoomedNotes(self, nullptr);
}

S3RE_HOOK(0x00551670, CComputerPlayer_WorkOnNextActiveNote, NoteVoidFn,
          Original_CComputerPlayer_WorkOnNextActiveNote, CComputerPlayer_WorkOnNextActiveNote);

// Purpose: Works on the active notes round robin, one per computer-player
// step (UpdateBlackboard).
void __fastcall CComputerPlayer_WorkOnNextActiveNote(void* self, void* /*edx*/) {
    ComputerPlayer* player = AsPlayer(self);
    AI_SetCurrentPlayer(self);
    // The cursor is not range-checked: past the 100 slots the original reads
    // on into active_note_count, active_cursor and doomed_notes (only with
    // a full list).
    const std::uint16_t* active = player->active_notes;
    if (active[player->active_cursor] == kNoNote) {
        player->active_cursor = 0;  // past the last active note: start again
    }
    const std::uint16_t cursor = player->active_cursor;
    const std::uint16_t id = active[cursor];
    if (id == kNoNote) {
        return;  // no active notes
    }
    player->active_cursor = static_cast<std::uint16_t>(cursor + 1);
    CComputerPlayer_WorkOnNote(self, nullptr, Address(player->notes[id]));
}

S3RE_HOOK(0x00551790, CComputerPlayer_DeleteDoomedNotes, NoteVoidFn, Original_CComputerPlayer_DeleteDoomedNotes,
          CComputerPlayer_DeleteDoomedNotes);

// Purpose: Deletes the notes that were marked for deletion (AI_List32Add,
// e.g. by the agents' slot 3 through 0x551760) since the last time.
void __fastcall CComputerPlayer_DeleteDoomedNotes(void* self, void* /*edx*/) {
    ComputerPlayer* player = AsPlayer(self);
    while (player->doomed_notes[0] != kNoNote) {
        const std::uint16_t id = player->doomed_notes[0];
        CComputerPlayer_RemoveDoomedNote(self, nullptr, id);
        CComputerPlayer_DeleteNote(self, nullptr, id);
    }
}

S3RE_HOOK(0x005517d0, CComputerPlayer_UpdateBlackboard, NoteVoidFn, Original_CComputerPlayer_UpdateBlackboard,
          CComputerPlayer_UpdateBlackboard);

// Purpose: The blackboard's part of a computer-player step (0x550f80):
// the agents' own work, then the new notes to their handlers, then one
// active note.
void __fastcall CComputerPlayer_UpdateBlackboard(void* self, void* /*edx*/) {
    CComputerPlayer_RunAgents(self, nullptr);
    CComputerPlayer_DispatchPendingNotes(self, nullptr);
    CComputerPlayer_WorkOnNextActiveNote(self, nullptr);
}

// ---------------------------------------------------------------- destruction

S3RE_HOOK(0x00550f30, CComputerPlayer_Shutdown, NoteVoidFn, Original_CComputerPlayer_Shutdown,
          CComputerPlayer_Shutdown);

// Purpose: Clears a computer player away (from its destructor): deletes
// every note tree and every agent and lets go of the shared land
// evaluation.
void __fastcall CComputerPlayer_Shutdown(void* self, void* /*edx*/) {
    ComputerPlayer* player = AsPlayer(self);
    AI_SetCurrentPlayer(self);
    while (player->root_notes[0] != kNoNote) {
        CComputerPlayer_DeleteNote(self, nullptr, player->root_notes[0]);
    }
    for (BlackBoardAgent* agent : player->agents) {
        if (agent != nullptr) {
            CComputerPlayer_DeleteAgent(self, nullptr, Address(agent));
        }
    }
    AI_EvaLand_Release();  // a tail call in the original
}

S3RE_HOOK(0x005509d0, CComputerPlayer_Destroy, NoteVoidFn, Original_CComputerPlayer_Destroy, CComputerPlayer_Destroy);

// Purpose: CComputerPlayer's destructor (a computer player leaves the
// game, or the game ends). The original's exception frame only unwinds the
// CObject base, so it has no effect here.
void __fastcall CComputerPlayer_Destroy(void* self, void* /*edx*/) {
    ComputerPlayer* player = AsPlayer(self);
    player->vtable = vtables::kCComputerPlayer;
    player->world = 0;
    player->message_queue = 0;
    CComputerPlayer_Shutdown(self, nullptr);
    Win32_CObject_Destroy(self, nullptr);
}

S3RE_HOOK(0x005509b0, CComputerPlayer_DeletingDestroy, DeletingDestroyFn, Original_CComputerPlayer_DeletingDestroy,
          CComputerPlayer_DeletingDestroy);

// Purpose: The compiler-generated "delete this computer player" entry of
// its vtable: destroys it and frees it if asked to.
void* __fastcall CComputerPlayer_DeletingDestroy(void* self, void* /*edx*/, std::uint32_t flags) {
    CComputerPlayer_Destroy(self, nullptr);
    if ((flags & 1) != 0) {
        reinterpret_cast<void(__cdecl*)(void*)>(0x0061e4ef)(self);  // the game's operator delete
    }
    return self;
}

// ---------------------------------------------------------------- building

S3RE_HOOK(0x00551a70, CComputerPlayer_StartBestBuilding, NoteVoidFn, Original_CComputerPlayer_StartBestBuilding,
          CComputerPlayer_StartBestBuilding);

// Purpose: While the current area has fewer than four construction sites
// of this player, hands on the building type with the highest build
// priority (0x58ecb0) to be built; when the economy lacks wood or stone,
// only a type that helps against it (or a small tower).
void __fastcall CComputerPlayer_StartBestBuilding(void* self, void* /*edx*/) {
    const ComputerPlayer* player = AsPlayer(self);
    const auto playerNumber = static_cast<std::uint8_t>(player->player);
    // The original's upper half of this dword is the caller's EAX; 0x59dbb0
    // compares only the low word.
    const std::uint32_t area = player->area & 0xffff;
    const auto sites = reinterpret_cast<CountSitesFn>(kCountSitesInArea)(&playerNumber, &area);
    if (static_cast<std::uint16_t>(sites) >= kMaxConstructionSites) {
        return;
    }
    const auto* data = reinterpret_cast<const AiPlayerData*>(player->data);
    std::uint32_t bestType = kBuildingTypeCount;
    std::uint16_t bestPriority = 0;
    for (std::uint32_t type = 0; type < kBuildingTypeCount; ++type) {
        const std::uint16_t priority = data->best[type].score;
        if (priority > bestPriority) {  // the first of equal priorities wins
            bestPriority = priority;
            bestType = type;
        }
        if (bestPriority == kTopPriority) {
            break;
        }
    }
    if (bestType == kBuildingTypeCount) {
        return;  // no type wanted
    }
    if (data->short_of_stone != 0 && !HelpsAgainstStoneShortage(bestType)) {
        return;
    }
    if (data->short_of_wood != 0 && !HelpsAgainstWoodShortage(bestType)) {
        return;
    }
    reinterpret_cast<CommitCandidateFn>(kCommitCandidate)(bestType);
}

// ---------------------------------------------------------------- note callbacks

S3RE_HOOK(0x00582480, BLACK_BOARD_NOTE_ReportToRequester, NoteReportFn, Original_BLACK_BOARD_NOTE_ReportToRequester,
          BLACK_BOARD_NOTE_ReportToRequester);

// Purpose: A note reports back to the agent that asked for it (its slot 9,
// with the parent note, the note, a result (0 or 100 at the callers) and a
// value), e.g. when a house request's sub-request is done.
std::uint32_t __fastcall BLACK_BOARD_NOTE_ReportToRequester(void* self, void* /*edx*/, std::uint32_t result,
                                                            std::uint32_t value) {
    BlackBoardNote* note = AsNote(self);
    if (note->requester == kNoAgent) {
        return kNoAgent;  // AX; the original's upper half is the caller's (callers use AX)
    }
    ComputerPlayer* player = g_aiPlayer;
    BlackBoardAgent* requester = player->agents[note->requester];
    // A root note (parent kNoNote) reads past the notes array, as the original.
    BlackBoardNote* parent = player->notes[note->parent];
    return CallSlot(requester, agent_slot::kNoteReported, Address(parent), Address(note), result, value);
}

S3RE_HOOK(0x00582510, BLACK_BOARD_NOTE_CancelAtHandler, NoteFn, Original_BLACK_BOARD_NOTE_CancelAtHandler,
          BLACK_BOARD_NOTE_CancelAtHandler);

// Purpose: Tells the agent that took a note that it is withdrawn (its slot
// 10; G), e.g. when the request above it is cancelled (0x5656e0, 0x5789b0).
// There is no check for a note nobody took.
std::uint32_t __fastcall BLACK_BOARD_NOTE_CancelAtHandler(void* self, void* /*edx*/) {
    ComputerPlayer* player = g_aiPlayer;
    return CallSlot(player->agents[AsNote(self)->handler], agent_slot::kCancelNote, Address(self));
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
             Original_CComputerPlayer_RemoveDoomedNote},
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
             &CComputerPlayer_RemoveActiveNote, &CComputerPlayer_RemoveDoomedNote},
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
    fillList(player->doomed_notes, kDoomedNoteSlots);
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

// ---------------------------------------------------------------- note tree tests (package A3)
//
// One buffer per side: the computer player, 24 notes (some ids free), the
// 11 agent objects (some not in agents[]), the note AddNote adds, a filler
// note (for a player whose 300 ids are all taken) and an AiPlayerData
// node; the buffer reaches up to notes[0xffff] (ReportToRequester on a root
// note reads it). The tree is consistent (parents, children, root list),
// as DeleteNote needs to end. Notes and agents have a fake vtable whose
// slots record their calls (pointers as offsets in the side's buffer) and
// answer with values from the case seed, so both sides get the same
// answers as long as they make the same calls. The original side runs as
// pure original code: the A3 entry points jump to their trampolines while
// it runs. StartBestBuilding's two callees (package A2) are replaced by
// recording stubs for both sides.

namespace {

constexpr std::size_t kTreeNotes = 24;
constexpr std::size_t kTreeNoteOffset = 0x800;
constexpr std::size_t kTreeAgentOffset = kTreeNoteOffset + kTreeNotes * kNoteStride;
constexpr std::size_t kTreeNewNoteOffset = kTreeAgentOffset + kAgentKinds * kNoteStride;
constexpr std::size_t kTreeFillerOffset = kTreeNewNoteOffset + kNoteStride;
constexpr std::size_t kTreeDataOffset = kTreeFillerOffset + kNoteStride;
constexpr std::size_t kTreeUsedBytes = kTreeDataOffset + kDataBytes;
constexpr std::size_t kFarNoteOffset = offsetof(ComputerPlayer, notes) + 4 * std::size_t{kNoNote};
constexpr std::size_t kTreeBytes = kFarNoteOffset + 4;
constexpr std::uint32_t kFarNoteValue = 0x0badf00d;  // notes[0xffff]: passed on, never dereferenced
static_assert(sizeof(ComputerPlayer) <= kTreeNoteOffset && kTreeUsedBytes <= kFarNoteOffset);

constexpr std::uint32_t kStubCountSites = 0x100;  // call log ids of the two stubs
constexpr std::uint32_t kStubCommit = 0x101;

struct FakeCall {
    std::uint32_t slot;
    std::uint32_t self;
    std::uint32_t args[4];
};

struct FakeBench {
    std::uint32_t base = 0;  // the side's buffer
    std::uint64_t seed = 0;
    std::uint32_t answers = 0;
    std::uint32_t sites = 0;  // CountSitesInArea's answer
    std::vector<FakeCall> calls;
};

FakeBench& Bench() {
    static FakeBench bench;
    return bench;
}

std::uint32_t Relative(std::uint32_t value) {
    const std::uint32_t offset = value - Bench().base;
    return offset < kTreeBytes ? offset : value;
}

std::uint32_t NextAnswer() {
    FakeBench& bench = Bench();
    std::uint64_t h = bench.seed + 0x9e3779b97f4a7c15ull * ++bench.answers;
    h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ull;
    h = (h ^ (h >> 27)) * 0x94d049bb133111ebull;
    return static_cast<std::uint32_t>(h ^ (h >> 31));
}

// 0, 1, kAgentDone (with noise above) or anything.
std::uint32_t Answer() {
    const std::uint32_t h = NextAnswer();
    switch (h & 3) {
        case 0: return 0;
        case 1: return 1;
        case 2: return (h & 0xffff0000u) | kAgentDone;
        default: return h;
    }
}

// Pointer arguments are passed through Relative by the caller; other values
// stay as they are (a random value could fall into one side's buffer).
void Record(std::uint32_t slot, const void* self, std::uint32_t a = 0, std::uint32_t b = 0, std::uint32_t c = 0,
            std::uint32_t d = 0) {
    Bench().calls.push_back({slot, Relative(Address(self)), {a, b, c, d}});
}

std::uint32_t __fastcall FakeDeletingDestructor(void* self, void* /*edx*/, std::uint32_t flags) {
    Record(agent_slot::kDeletingDestructor, self, flags);
    return Address(self);
}

std::uint32_t __fastcall FakeNoteTypes(void* self, void* /*edx*/, std::uint32_t buffer) {
    Record(agent_slot::kNoteTypes, self);  // the buffer is on each side's own stack
    const std::uint32_t h = NextAnswer();
    const std::uint32_t count = (h & 7) == 0 ? (h >> 8) % (kMaxAgentNoteTypes + 1) : (h >> 8) % 4;
    auto* types = reinterpret_cast<std::uint16_t*>(buffer);
    for (std::uint32_t k = 0; k < count; ++k) {
        // 6..8 reach past handlers[] into player_number and timer274/unk276.
        types[k] = static_cast<std::uint16_t>(NextAnswer() % 9);
    }
    return (h & 0xffff0000u) | count;  // callers use AX
}

template <std::uint32_t Slot>
std::uint32_t __fastcall FakeSlot(void* self, void* /*edx*/) {
    Record(Slot, self);
    return Answer();
}

template <std::uint32_t Slot>
std::uint32_t __fastcall FakeSlotWithNote(void* self, void* /*edx*/, std::uint32_t note) {
    Record(Slot, self, Relative(note));
    return Answer();
}

std::uint32_t __fastcall FakeNoteReported(void* self, void* /*edx*/, std::uint32_t parent, std::uint32_t note,
                                          std::uint32_t result, std::uint32_t value) {
    Record(agent_slot::kNoteReported, self, Relative(parent), Relative(note), result, value);
    return Answer();
}

const void* const kFakeVtable[] = {
    reinterpret_cast<const void*>(&FakeDeletingDestructor),
    nullptr,
    reinterpret_cast<const void*>(&FakeNoteTypes),
    reinterpret_cast<const void*>(&FakeSlotWithNote<agent_slot::kTakeNote>),
    reinterpret_cast<const void*>(&FakeSlotWithNote<agent_slot::kWorkOnNote>),
    reinterpret_cast<const void*>(&FakeSlot<agent_slot::kBusy>),
    nullptr,
    reinterpret_cast<const void*>(&FakeSlot<agent_slot::kWantsToRun>),
    reinterpret_cast<const void*>(&FakeSlot<agent_slot::kRun>),
    reinterpret_cast<const void*>(&FakeNoteReported),
    reinterpret_cast<const void*>(&FakeSlotWithNote<agent_slot::kCancelNote>),
};

std::uint32_t __fastcall FakeCountSites(const std::uint8_t* player, const std::uint32_t* area) {
    Record(kStubCountSites, nullptr, *player, *area & 0xffff);
    return Bench().sites;
}

std::uint32_t __fastcall FakeCommitCandidate(std::uint32_t type) {
    Record(kStubCommit, nullptr, type);
    return 0;
}

ComputerPlayer* TreePlayer(std::uint8_t* buffer) { return reinterpret_cast<ComputerPlayer*>(buffer); }
BlackBoardNote* TreeNote(std::uint8_t* buffer, std::size_t id) {
    return reinterpret_cast<BlackBoardNote*>(buffer + kTreeNoteOffset + id * kNoteStride);
}
BlackBoardAgent* TreeAgent(std::uint8_t* buffer, std::size_t k) {
    return reinterpret_cast<BlackBoardAgent*>(buffer + kTreeAgentOffset + k * kNoteStride);
}

struct TreeShape {
    std::vector<std::uint16_t> notes;   // present note ids
    std::vector<std::uint16_t> agents;  // indexes with agents[k] set
    bool full = false;
};

TreeShape FillTree(test::Rng& rng, std::vector<std::uint8_t>& buffer) {
    std::memset(buffer.data(), 0, buffer.size());
    rng.Fill(buffer.data(), kTreeUsedBytes);
    std::uint8_t* base = buffer.data();
    ComputerPlayer* player = TreePlayer(base);
    TreeShape shape;
    auto shuffled = [&](std::vector<std::uint16_t> ids) {
        for (std::size_t k = ids.size(); k > 1; --k) {
            std::swap(ids[k - 1], ids[rng.Below(static_cast<std::uint32_t>(k))]);
        }
        return ids;
    };
    // Agents: every object valid (kind mostly its index), most in agents[].
    for (std::uint16_t k = 0; k < kAgentKinds; ++k) {
        BlackBoardAgent* agent = TreeAgent(base, k);
        agent->vtable = kFakeVtable;
        agent->kind = rng.Chance(1, 10) ? static_cast<std::uint16_t>(rng.Below(kAgentKinds)) : k;
        const bool listed = !rng.Chance(1, 8);
        player->agents[k] = listed ? agent : nullptr;
        if (listed) {
            shape.agents.push_back(k);
        }
    }
    auto someAgent = [&]() -> std::uint16_t {
        return shape.agents.empty() ? kNoAgent
                                    : shape.agents[rng.Below(static_cast<std::uint32_t>(shape.agents.size()))];
    };
    for (std::uint16_t& handler : player->handlers) {
        handler = rng.Chance(1, 4) ? kNoAgent : someAgent();
    }
    // Notes: a consistent forest; each new child goes in front (as AddNote).
    for (BlackBoardNote*& note : player->notes) {
        note = nullptr;
    }
    std::vector<std::uint16_t> roots;
    for (std::uint16_t id = 0; id < kTreeNotes; ++id) {
        if (rng.Chance(1, 6)) {
            continue;
        }
        BlackBoardNote* note = TreeNote(base, id);
        note->vtable = Address(kFakeVtable);
        note->flags = static_cast<std::uint16_t>(rng.Chance(1, 2) ? rng.Below(16) : rng.Next32());
        note->type = static_cast<std::uint16_t>(rng.Below(6));
        note->id = id;
        note->first_child = kNoNote;
        note->requester = rng.Chance(1, 4) ? kNoAgent : someAgent();
        note->handler = someAgent();
        if (!shape.notes.empty() && rng.Chance(3, 5)) {
            const std::uint16_t parent = shape.notes[rng.Below(static_cast<std::uint32_t>(shape.notes.size()))];
            note->parent = parent;
            note->next_sibling = player->notes[parent]->first_child;
            player->notes[parent]->first_child = id;
        } else {
            note->parent = kNoNote;
            note->next_sibling = static_cast<std::uint16_t>(rng.Next32());
            roots.push_back(id);
        }
        player->notes[id] = note;
        shape.notes.push_back(id);
    }
    auto subset = [&](std::uint32_t numerator, std::uint32_t denominator) {
        std::vector<std::uint16_t> ids;
        for (std::uint16_t id : shape.notes) {
            if (rng.Chance(numerator, denominator)) {
                ids.push_back(id);
            }
        }
        return shuffled(ids);
    };
    auto fillList = [&](std::uint16_t* list, std::size_t slots, std::uint16_t& count,
                        const std::vector<std::uint16_t>& ids) {
        for (std::size_t k = 0; k < slots; ++k) {
            list[k] = k < ids.size() ? ids[k] : kNoNote;
        }
        count = static_cast<std::uint16_t>(rng.Chance(1, 8) ? rng.Below(kTreeNotes + 8) : ids.size());
    };
    fillList(player->root_notes, kRootNoteSlots, player->root_note_count, shuffled(roots));
    fillList(player->pending_notes, kPendingNoteSlots, player->pending_note_count, subset(1, 3));
    const std::vector<std::uint16_t> active = subset(1, 2);
    fillList(player->active_notes, kActiveNoteSlots, player->active_note_count, active);
    for (std::uint16_t id : shape.notes) {
        BlackBoardNote* note = player->notes[id];
        const bool isActive = std::find(active.begin(), active.end(), id) != active.end();
        if (!rng.Chance(1, 10)) {
            note->flags = static_cast<std::uint16_t>(isActive ? note->flags | kNoteActive : note->flags & ~kNoteActive);
        }
    }
    fillList(player->doomed_notes, kDoomedNoteSlots, player->doomed_note_count, subset(1, 5));
    player->active_cursor = static_cast<std::uint16_t>(rng.Below(103));
    player->setup_index = rng.Below(64);
    // Sometimes every id is taken (AddNote finds none): a filler note of
    // type 0 in the free slots (in no list; WorkOnNextActiveNote may reach
    // it past the active list).
    BlackBoardNote* filler = reinterpret_cast<BlackBoardNote*>(base + kTreeFillerOffset);
    filler->vtable = Address(kFakeVtable);
    filler->type = 0;
    shape.full = rng.Chance(1, 16);
    if (shape.full) {
        for (BlackBoardNote*& note : player->notes) {
            if (note == nullptr) {
                note = filler;
            }
        }
    }
    reinterpret_cast<BlackBoardNote*>(base + kTreeNewNoteOffset)->vtable = Address(kFakeVtable);
    // Build priorities: zero, small, the top value, or anything.
    player->data = Address(base + kTreeDataOffset);
    auto* data = reinterpret_cast<AiPlayerData*>(base + kTreeDataOffset);
    for (std::uint32_t type = 0; type < kBuildingTypeCount; ++type) {
        const std::uint32_t kind = rng.Below(8);
        data->best[type].score = static_cast<std::uint16_t>(kind < 3 ? 0 : kind < 5 ? rng.Below(2000) : kind == 5 ? 65000 : rng.Next32());
    }
    if (rng.Chance(1, 4)) {
        for (std::uint32_t type = 0; type < kBuildingTypeCount; ++type) {
            data->best[type].score = static_cast<std::uint16_t>(rng.Chance(1, 4) ? rng.Below(3) : 0);
        }
    }
    data->short_of_wood = rng.Chance(1, 2) ? 0 : rng.Next32();
    data->short_of_stone = rng.Chance(1, 2) ? 0 : rng.Next32();
    *reinterpret_cast<std::uint32_t*>(base + kFarNoteOffset) = kFarNoteValue;
    return shape;
}

// Moves the pointers of a copied buffer from the source buffer into it.
void TreeRebase(std::uint8_t* copy, const std::uint8_t* source) {
    ComputerPlayer* player = TreePlayer(copy);
    const std::uint32_t from = Address(source);
    const std::uint32_t to = Address(copy);
    auto rebase = [&](std::uint32_t value) { return value - from < kTreeBytes ? value - from + to : value; };
    for (BlackBoardNote*& note : player->notes) {
        note = reinterpret_cast<BlackBoardNote*>(rebase(Address(note)));
    }
    for (BlackBoardAgent*& agent : player->agents) {
        agent = reinterpret_cast<BlackBoardAgent*>(rebase(Address(agent)));
    }
    player->data = rebase(player->data);
}

// Pointers of the buffer as offsets (for the comparison).
void TreeUnlink(std::uint8_t* buffer) {
    ComputerPlayer* player = TreePlayer(buffer);
    for (BlackBoardNote*& note : player->notes) {
        note = reinterpret_cast<BlackBoardNote*>(Relative(Address(note)));
    }
    for (BlackBoardAgent*& agent : player->agents) {
        agent = reinterpret_cast<BlackBoardAgent*>(Relative(Address(agent)));
    }
    player->data = Relative(player->data);
}

struct NoteTreeFunctions {
    AddNoteFn addNote;
    AddNoteFn addNoteUnder;
    NoteIdFn deleteNote;
    NoteVoidArgFn addAgent;
    NoteVoidArgFn deleteAgent;
    NoteVoidArgFn dispatchNote;
    NoteVoidFn dispatchPending;
    NoteVoidFn runAgents;
    NoteVoidArgFn workOnNote;
    NoteVoidFn workOnNext;
    NoteVoidFn deleteDoomed;
    NoteVoidFn update;
    NoteVoidFn shutdown;
    NoteVoidFn destroy;
    DeletingDestroyFn deletingDestroy;
    NoteVoidFn startBest;
    NoteReportFn report;
    NoteFn cancel;
};

NoteTreeFunctions OriginalNoteTree() {
    return {Original_CComputerPlayer_AddNote,
            Original_CComputerPlayer_AddNoteUnder,
            Original_CComputerPlayer_DeleteNote,
            Original_CComputerPlayer_AddAgent,
            Original_CComputerPlayer_DeleteAgent,
            Original_CComputerPlayer_DispatchNote,
            Original_CComputerPlayer_DispatchPendingNotes,
            Original_CComputerPlayer_RunAgents,
            Original_CComputerPlayer_WorkOnNote,
            Original_CComputerPlayer_WorkOnNextActiveNote,
            Original_CComputerPlayer_DeleteDoomedNotes,
            Original_CComputerPlayer_UpdateBlackboard,
            Original_CComputerPlayer_Shutdown,
            Original_CComputerPlayer_Destroy,
            Original_CComputerPlayer_DeletingDestroy,
            Original_CComputerPlayer_StartBestBuilding,
            Original_BLACK_BOARD_NOTE_ReportToRequester,
            Original_BLACK_BOARD_NOTE_CancelAtHandler};
}

NoteTreeFunctions OurNoteTree() {
    return {&CComputerPlayer_AddNote,
            &CComputerPlayer_AddNoteUnder,
            &CComputerPlayer_DeleteNote,
            &CComputerPlayer_AddAgent,
            &CComputerPlayer_DeleteAgent,
            &CComputerPlayer_DispatchNote,
            &CComputerPlayer_DispatchPendingNotes,
            &CComputerPlayer_RunAgents,
            &CComputerPlayer_WorkOnNote,
            &CComputerPlayer_WorkOnNextActiveNote,
            &CComputerPlayer_DeleteDoomedNotes,
            &CComputerPlayer_UpdateBlackboard,
            &CComputerPlayer_Shutdown,
            &CComputerPlayer_Destroy,
            &CComputerPlayer_DeletingDestroy,
            &CComputerPlayer_StartBestBuilding,
            &BLACK_BOARD_NOTE_ReportToRequester,
            &BLACK_BOARD_NOTE_CancelAtHandler};
}

// While it lives, the A3 entry points run their original code (the
// trampolines), so the original side's inner calls stay original.
class OriginalNoteTreeCode {
public:
    OriginalNoteTreeCode() {
        const NoteTreeFunctions f = OriginalNoteTree();
        Jump(0x00551270, f.addNote);
        Jump(0x00551230, f.addNoteUnder);
        Jump(0x00551330, f.deleteNote);
        Jump(0x00551400, f.addAgent);
        Jump(0x00551490, f.deleteAgent);
        Jump(0x00551510, f.dispatchNote);
        Jump(0x005514c0, f.dispatchPending);
        Jump(0x00551590, f.runAgents);
        Jump(0x005516e0, f.workOnNote);
        Jump(0x00551670, f.workOnNext);
        Jump(0x00551790, f.deleteDoomed);
        Jump(0x005517d0, f.update);
        Jump(0x00550f30, f.shutdown);
        Jump(0x005509d0, f.destroy);
        Jump(0x005509b0, f.deletingDestroy);
        Jump(0x00551a70, f.startBest);
        Jump(0x00582480, f.report);
        Jump(0x00582510, f.cancel);
    }

private:
    template <typename Fn>
    void Jump(std::uintptr_t address, Fn original) {
        if (reinterpret_cast<std::uintptr_t>(original) != address) {  // not hooked: already original
            jumps_.push_back(std::make_unique<test::ScopedJump>(address, reinterpret_cast<const void*>(original)));
        }
    }
    std::vector<std::unique_ptr<test::ScopedJump>> jumps_;
};

enum class TreeOp {
    AddNote, AddNoteUnder, DeleteNote, AddAgent, DeleteAgent, DispatchNote, DispatchPending, RunAgents,
    WorkOnNote, WorkOnNext, DeleteDoomed, Update, Shutdown, Destroy, DeletingDestroy, StartBest, Report, Cancel,
    Count
};

struct TreeCase {
    TreeOp op;
    std::uint32_t id;       // DeleteNote
    std::uint32_t note;     // a note index (kTreeNotes: none)
    std::uint32_t parent;   // AddNote: an id (with noise above); AddNoteUnder: a note index or kTreeNotes
    std::uint32_t agent;    // an agent object index (kAgentKinds: none)
    std::uint32_t result, value, flags;
};

std::uint32_t RunTreeOp(const NoteTreeFunctions& f, const TreeCase& c, std::uint8_t* buffer) {
    void* player = buffer;
    const std::uint32_t note = c.note < kTreeNotes ? Address(TreeNote(buffer, c.note)) : 0;
    const std::uint32_t agent = c.agent < kAgentKinds ? Address(TreeAgent(buffer, c.agent)) : 0;
    const std::uint32_t newNote = Address(buffer + kTreeNewNoteOffset);
    switch (c.op) {
        case TreeOp::AddNote: return f.addNote(player, nullptr, newNote, c.parent, agent);
        case TreeOp::AddNoteUnder: {
            const std::uint32_t parent = c.parent < kTreeNotes ? Address(TreeNote(buffer, c.parent)) : 0;
            return f.addNoteUnder(player, nullptr, newNote, parent, agent);
        }
        case TreeOp::DeleteNote: return f.deleteNote(player, nullptr, c.id);
        case TreeOp::AddAgent: f.addAgent(player, nullptr, agent); return 0;
        case TreeOp::DeleteAgent: f.deleteAgent(player, nullptr, agent); return 0;
        case TreeOp::DispatchNote: f.dispatchNote(player, nullptr, note); return 0;
        case TreeOp::DispatchPending: f.dispatchPending(player, nullptr); return 0;
        case TreeOp::RunAgents: f.runAgents(player, nullptr); return 0;
        case TreeOp::WorkOnNote: f.workOnNote(player, nullptr, note); return 0;
        case TreeOp::WorkOnNext: f.workOnNext(player, nullptr); return 0;
        case TreeOp::DeleteDoomed: f.deleteDoomed(player, nullptr); return 0;
        case TreeOp::Update: f.update(player, nullptr); return 0;
        case TreeOp::Shutdown: f.shutdown(player, nullptr); return 0;
        case TreeOp::Destroy: f.destroy(player, nullptr); return 0;
        // Without "free it": the buffer is ours (the emulator spec covers bit 0).
        case TreeOp::DeletingDestroy: return Address(f.deletingDestroy(player, nullptr, c.flags & ~1u));
        case TreeOp::StartBest: f.startBest(player, nullptr); return 0;
        case TreeOp::Report: return f.report(reinterpret_cast<void*>(note), nullptr, c.result, c.value);
        case TreeOp::Cancel: return f.cancel(reinterpret_cast<void*>(note), nullptr);
        default: return 0;
    }
}

TreeCase MakeTreeCase(test::Rng& rng, const TreeShape& shape) {
    TreeCase c{};
    c.op = static_cast<TreeOp>(rng.Below(static_cast<std::uint32_t>(TreeOp::Count)));
    auto presentNote = [&]() -> std::uint32_t {
        return shape.notes.empty() ? kTreeNotes
                                   : shape.notes[rng.Below(static_cast<std::uint32_t>(shape.notes.size()))];
    };
    const std::uint32_t noise = rng.Chance(1, 3) ? rng.Next32() & 0xffff0000u : 0;
    c.note = presentNote();
    if (c.op == TreeOp::WorkOnNote && rng.Chance(1, 4)) {
        c.note = kTreeNotes;  // a null note: only the doomed notes are deleted
    }
    // DeleteNote: a present id, or a free one (never the filler's ids).
    const bool freeId = shape.notes.empty() || (!shape.full && rng.Chance(1, 4));
    c.id = noise | (freeId ? rng.Below(kTreeNotes) : presentNote());
    if (c.op == TreeOp::AddNote) {
        c.parent = noise | (rng.Chance(1, 3) || shape.notes.empty() ? kNoNote : presentNote());
    } else {
        c.parent = rng.Chance(1, 3) ? kTreeNotes : presentNote();
    }
    const bool needsAgent = c.op == TreeOp::AddAgent || c.op == TreeOp::DeleteAgent;
    c.agent = !needsAgent && rng.Chance(1, 4) ? kAgentKinds : rng.Below(kAgentKinds);
    c.result = rng.Chance(1, 2) ? (rng.Chance(1, 2) ? kAgentDone : 0) : rng.Next32();
    c.value = rng.Next32();
    c.flags = rng.Next32();
    return c;
}

bool TreeOpRunnable(const TreeCase& c, const TreeShape& shape) {
    const bool needsNote = c.op == TreeOp::DispatchNote || c.op == TreeOp::Report || c.op == TreeOp::Cancel;
    if (needsNote && c.note >= kTreeNotes) {
        return false;
    }
    if (c.op == TreeOp::DeleteNote && shape.full && shape.notes.empty()) {
        return false;  // every id is the filler's
    }
    // CancelAtHandler and the handlers need agents (a note's handler is one).
    return !shape.agents.empty() || c.op == TreeOp::AddNote || c.op == TreeOp::AddNoteUnder;
}

struct EvaLandGlobals {
    std::uint32_t count;
    AiLandEvaluation* instance;
};

void CompareNoteTree(test::Context& ctx, const NoteTreeFunctions& candidate, int cases) {
    const NoteTreeFunctions original = OriginalNoteTree();
    const AiGlobals saved = SaveGlobals();
    const EvaLandGlobals savedEvaLand{g_aiEvaLandRefCount, g_aiEvaLand};
    test::ScopedJump countSites{kCountSitesInArea, reinterpret_cast<const void*>(&FakeCountSites)};
    test::ScopedJump commit{kCommitCandidate, reinterpret_cast<const void*>(&FakeCommitCandidate)};
    std::vector<std::uint8_t> a(kTreeBytes), b(kTreeBytes);
    FakeBench& bench = Bench();
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        const TreeShape shape = FillTree(ctx.rng(), a);
        const TreeCase c = MakeTreeCase(ctx.rng(), shape);
        if (!TreeOpRunnable(c, shape)) {
            continue;
        }
        b = a;
        TreeRebase(b.data(), a.data());
        bench.seed = ctx.rng().Next64();
        const std::uint32_t sites = ctx.rng().Below(7) | (ctx.rng().Chance(1, 4) ? ctx.rng().Next32() << 16 : 0);
        // CEvaLand::Release (Shutdown): mostly other users left; sometimes
        // the last one, with no instance to free.
        const EvaLandGlobals evaLand = ctx.rng().Chance(1, 4) ? EvaLandGlobals{1, nullptr}
                                                              : EvaLandGlobals{2 + ctx.rng().Below(5), nullptr};
        // A note without requester returns AX only (the original's upper
        // half is its caller's EAX).
        const bool axOnly = c.op == TreeOp::Report && TreeNote(a.data(), c.note)->requester == kNoAgent;
        auto runSide = [&](const NoteTreeFunctions& f, std::vector<std::uint8_t>& buffer, bool originalCode,
                           std::vector<FakeCall>& calls, AiGlobals& globals, EvaLandGlobals& evaLandAfter) {
            bench.base = Address(buffer.data());
            bench.answers = 0;
            bench.sites = sites;
            bench.calls.clear();
            RestoreGlobals(saved);
            g_aiPlayer = TreePlayer(buffer.data());
            g_aiEvaLandRefCount = evaLand.count;
            g_aiEvaLand = evaLand.instance;
            std::uint32_t result = 0;
            if (originalCode) {
                OriginalNoteTreeCode jumps;
                result = RunTreeOp(f, c, buffer.data());
            } else {
                result = RunTreeOp(f, c, buffer.data());
            }
            // Pointers: DeleteNote returns the destructor's EAX (the
            // note), DeletingDestroy the player.
            if (c.op == TreeOp::DeleteNote || c.op == TreeOp::DeletingDestroy) {
                result = Relative(result);
            }
            globals = SaveGlobals();
            evaLandAfter = {g_aiEvaLandRefCount, g_aiEvaLand};
            calls.swap(bench.calls);
            TreeUnlink(buffer.data());
            return result;
        };
        std::vector<FakeCall> callsA, callsB;
        AiGlobals ga{}, gb{};
        EvaLandGlobals ea{}, eb{};
        std::uint32_t ra = runSide(original, a, true, callsA, ga, ea);
        std::uint32_t rb = runSide(candidate, b, false, callsB, gb, eb);
        RestoreGlobals(saved);
        if (axOnly) {
            ra &= 0xffff;
            rb &= 0xffff;
        }
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size());
        same &= S3RE_EXPECT_EQ(callsA.size(), callsB.size());
        if (callsA.size() == callsB.size() && !callsA.empty()) {
            same &= S3RE_EXPECT_MEM_EQ(callsA.data(), callsB.data(), callsA.size() * sizeof(FakeCall));
        }
        // Pointers into the side's buffer as offsets (functions that don't
        // call AI_SetCurrentPlayer leave the saved values).
        auto offsetIn = [](const void* pointer, const std::vector<std::uint8_t>& buffer) {
            const std::uint32_t offset = Address(pointer) - Address(buffer.data());
            return offset < kTreeBytes ? offset : Address(pointer);
        };
        same &= S3RE_EXPECT_EQ(offsetIn(ga.player, a), offsetIn(gb.player, b));
        same &= S3RE_EXPECT_EQ(ga.number, gb.number);
        same &= S3RE_EXPECT_EQ(Address(ga.setup), Address(gb.setup));
        same &= S3RE_EXPECT_EQ(offsetIn(ga.data, a), offsetIn(gb.data, b));
        same &= S3RE_EXPECT_EQ(ea.count, eb.count);
        same &= S3RE_EXPECT_EQ(Address(ea.instance), Address(eb.instance));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, static_cast<int>(c.op));
        }
    }
    RestoreGlobals(saved);
    g_aiEvaLandRefCount = savedEvaLand.count;
    g_aiEvaLand = savedEvaLand.instance;
}

// Wrong: deletes the doomed notes also when the note's type has no handler.
void __fastcall WrongWorkOnNote(void* self, void* /*edx*/, std::uint32_t note) {
    CComputerPlayer_WorkOnNote(self, nullptr, note);
    CComputerPlayer_DeleteDoomedNotes(self, nullptr);
}

// Wrong: an active note stays in the active list.
std::uint32_t __fastcall WrongDeleteNote(void* self, void* /*edx*/, std::uint32_t id) {
    const BlackBoardNote* note = AsPlayer(self)->notes[id & 0xffff];
    const bool active = note != nullptr && (note->flags & kNoteActive) != 0;
    const std::uint32_t result = CComputerPlayer_DeleteNote(self, nullptr, id);
    if (active) {
        CComputerPlayer_AddActiveNote(self, nullptr, id);
    }
    return result;
}

// Wrong: the last of equal priorities wins.
void __fastcall WrongStartBestBuilding(void* self, void* /*edx*/) {
    const ComputerPlayer* player = AsPlayer(self);
    const auto playerNumber = static_cast<std::uint8_t>(player->player);
    const std::uint32_t area = player->area & 0xffff;
    if (static_cast<std::uint16_t>(reinterpret_cast<CountSitesFn>(kCountSitesInArea)(&playerNumber, &area)) >=
        kMaxConstructionSites) {
        return;
    }
    const auto* data = reinterpret_cast<const AiPlayerData*>(player->data);
    std::uint32_t bestType = kBuildingTypeCount;
    std::uint16_t bestPriority = 0;
    for (std::uint32_t type = 0; type < kBuildingTypeCount && bestPriority != kTopPriority; ++type) {
        const std::uint16_t priority = data->best[type].score;
        if (priority != 0 && priority >= bestPriority) {
            bestPriority = priority;
            bestType = type;
        }
    }
    if (bestType == kBuildingTypeCount || (data->short_of_stone != 0 && !HelpsAgainstStoneShortage(bestType)) ||
        (data->short_of_wood != 0 && !HelpsAgainstWoodShortage(bestType))) {
        return;
    }
    reinterpret_cast<CommitCandidateFn>(kCommitCandidate)(bestType);
}

// Wrong: a first child gets next_sibling = kNoNote.
std::uint32_t __fastcall WrongAddNote(void* self, void* /*edx*/, std::uint32_t note, std::uint32_t parent,
                                      std::uint32_t requester) {
    const auto parentId = static_cast<std::uint16_t>(parent);
    const bool firstChild = parentId != kNoNote && AsPlayer(self)->notes[parentId]->first_child == kNoNote;
    const std::uint32_t result = CComputerPlayer_AddNote(self, nullptr, note, parent, requester);
    if (firstChild) {
        reinterpret_cast<BlackBoardNote*>(note)->next_sibling = kNoNote;
    }
    return result;
}

}  // namespace

S3RE_TEST(AiNoteTree_MatchOriginal) {
    CompareNoteTree(ctx, OurNoteTree(), 30000);
}

S3RE_TEST(AiNoteTree_ComparisonDetectsWrongImplementation) {
    const auto probe = [&](auto change) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        NoteTreeFunctions wrong = OurNoteTree();
        change(wrong);
        CompareNoteTree(quiet, wrong, 20000);
        S3RE_EXPECT(quiet.failures() > 0);
    };
    probe([](NoteTreeFunctions& f) { f.workOnNote = &WrongWorkOnNote; });
    probe([](NoteTreeFunctions& f) { f.deleteNote = &WrongDeleteNote; });
    probe([](NoteTreeFunctions& f) { f.startBest = &WrongStartBestBuilding; });
    probe([](NoteTreeFunctions& f) { f.addNote = &WrongAddNote; });
}

}  // namespace s3re::game
