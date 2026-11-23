// CComputerPlayer and its blackboard agents (computer players, batch 1 of
// the AI; re/computer_players.md). The CComputerPlayer methods are in an
// unnamed source file before Rainer\Cevaland.cpp (object unit 57,
// 0x550900..0x553d30); the BLACK_BOARD_AGENT helpers sit between
// Rainer\CompHouseReq.cpp and Rainer\CompMapWorkerAgent.cpp.
//
// A computer player keeps its agents (BLACK_BOARD_AGENT and derived
// classes) by id in ComputerPlayer::agents and four id lists (ai_layout.h):
// root agents, pending notes, active agents and a fourth one (list200).
// The list functions are thiscall with one argument (ret 4):
//   0x5517f0 AddRootAgent(id)       AI_WordListInsert(id, root_agents, 100), on success ++root_agent_count
//   0x551820 RemoveRootAgent(id)    AI_WordListRemove(...), on success --root_agent_count
//   0x551850 AddPendingAgent(id)    pending_agents (50), pending_agent_count
//   0x551880 RemovePendingAgent(id)
//   0x5518b0 AddActiveAgent(id)     active_agents (100), active_agent_count
//   0x5518e0 RemoveActiveAgent(id)
//   0x551940 RemoveFromList200(id)  list200 (50), list200_count (the add is
//                                   AI_List32Add 0x551910, ai_helpers.cpp)
//   EAX = the insert/remove result (1/0).
// 0x5515e0 ActivateAgent(id): AI_SetCurrentPlayer(this), AddActiveAgent(id),
//   then BLACK_BOARD_AGENT::SetFlags(agents[id], 4). EAX = 4 (SetFlags puts
//   4 into AX; the upper half is AddActiveAgent's 0/1 result's, 0).
// 0x5515c0 ActivateAgentOf(agent): ActivateAgent(agent->id), the id loaded
//   with `mov dx`, so the upper half of the pushed dword is the caller's
//   EDX (ours gets it as the EDX parameter; only the low word is used).
// 0x551630 DeactivateAgent(id): if agents[id] has flag 4: RemoveActiveAgent(id)
//   and ClearFlags(4), EAX = ~4; else EAX = 0. (Ghidra put a false function
//   start at 0x551637, inside it.)
// 0x551610 DeactivateAgentOf(agent): the same with agent->id (`mov dx`).
// 0x551b10 RaiseBuildPriorities(): for each building type 0..56 of the
//   current AiPlayerData node (+0x778): a non-zero priority (best[type].score)
//   below cap - step (signed) grows by step (g_aiBuildPrioritySteps /
//   g_aiBuildPriorityCaps). EAX: not used (it flows into 0x550f80's return,
//   which CLogicThread::Step ignores).
//
// BLACK_BOARD_AGENT helpers (thiscall):
//   0x582400 ResetLinks(): flags, type, +0x0a, +0x0c = 0; id, parent, next
//     sibling, first child, +0x16, +0x18 = 0xffff. EAX = 0xffff.
//   0x5823e0 Init(type): ResetLinks, type = the low word. EAX = type & 0xffff.
//   0x582430 MarkDispatched(): SetFlags(1).
//   0x582440 SetFlags(bits): flags |= bits (low word). EAX: AX = bits, the
//     upper half is the caller's EAX (callers use AX; ours: 0).
//   0x582450 ClearFlags(bits): flags &= ~bits. EAX = ~bits.
//   0x582460 HasFlags(bits): (flags & bits) == bits on the low words; EAX 1/0.
//   0x5824d0 FindChildOfType(type): walks the children (first_child, then
//     next_sibling, ids into g_aiPlayer->agents) for one whose type equals
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

using AgentIdFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t id);
using AgentFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using AgentVoidFn = void(__fastcall*)(void* self, void* edx);

std::uint32_t __fastcall CComputerPlayer_AddRootAgent(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_RemoveRootAgent(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_AddPendingAgent(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_RemovePendingAgent(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_AddActiveAgent(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_RemoveActiveAgent(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_RemoveFromList200(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_ActivateAgent(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_ActivateAgentOf(void* self, void* edx, std::uint32_t agent);
std::uint32_t __fastcall CComputerPlayer_DeactivateAgent(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CComputerPlayer_DeactivateAgentOf(void* self, void* edx, std::uint32_t agent);
void __fastcall CComputerPlayer_RaiseBuildPriorities(void* self, void* edx);
std::uint32_t __fastcall BLACK_BOARD_AGENT_ResetLinks(void* self, void* edx);
std::uint32_t __fastcall BLACK_BOARD_AGENT_Init(void* self, void* edx, std::uint32_t type);
std::uint32_t __fastcall BLACK_BOARD_AGENT_MarkDispatched(void* self, void* edx);
std::uint32_t __fastcall BLACK_BOARD_AGENT_SetFlags(void* self, void* edx, std::uint32_t bits);
std::uint32_t __fastcall BLACK_BOARD_AGENT_ClearFlags(void* self, void* edx, std::uint32_t bits);
std::uint32_t __fastcall BLACK_BOARD_AGENT_HasFlags(void* self, void* edx, std::uint32_t bits);
std::uint32_t __fastcall BLACK_BOARD_AGENT_FindChildOfType(void* self, void* edx, std::uint32_t type);

namespace {

ComputerPlayer* AsPlayer(void* self) { return static_cast<ComputerPlayer*>(self); }
BlackBoardAgent* AsAgent(void* self) { return static_cast<BlackBoardAgent*>(self); }

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

// The id of an agent as 0x5515c0 / 0x551610 push it: `mov dx, [agent+8]`
// keeps the caller's EDX in the upper half.
std::uint32_t AgentIdWithCallerEdx(std::uint32_t agent, void* edx) {
    const std::uint16_t id = reinterpret_cast<const BlackBoardAgent*>(agent)->id;
    return (reinterpret_cast<std::uint32_t>(edx) & 0xffff0000u) | id;
}

}  // namespace

// ---------------------------------------------------------------- id lists

S3RE_HOOK(0x005517f0, CComputerPlayer_AddRootAgent, AgentIdFn, Original_CComputerPlayer_AddRootAgent,
          CComputerPlayer_AddRootAgent);

// Purpose: Records an agent without a parent as one of the computer
// player's root agents (its top-level goals, probably).
std::uint32_t __fastcall CComputerPlayer_AddRootAgent(void* self, void* /*edx*/, std::uint32_t id) {
    ComputerPlayer* player = AsPlayer(self);
    return AddToList(id, player->root_agents, kRootAgentSlots, player->root_agent_count);
}

S3RE_HOOK(0x00551820, CComputerPlayer_RemoveRootAgent, AgentIdFn, Original_CComputerPlayer_RemoveRootAgent,
          CComputerPlayer_RemoveRootAgent);

// Purpose: Forgets a root agent (when it is deleted, 0x551330).
std::uint32_t __fastcall CComputerPlayer_RemoveRootAgent(void* self, void* /*edx*/, std::uint32_t id) {
    ComputerPlayer* player = AsPlayer(self);
    return RemoveFromList(id, player->root_agents, kRootAgentSlots, player->root_agent_count);
}

S3RE_HOOK(0x00551850, CComputerPlayer_AddPendingAgent, AgentIdFn, Original_CComputerPlayer_AddPendingAgent,
          CComputerPlayer_AddPendingAgent);

// Purpose: Queues a new note (agent) for its handler, which 0x5514c0 calls
// on the next step.
std::uint32_t __fastcall CComputerPlayer_AddPendingAgent(void* self, void* /*edx*/, std::uint32_t id) {
    ComputerPlayer* player = AsPlayer(self);
    return AddToList(id, player->pending_agents, kPendingAgentSlots, player->pending_agent_count);
}

S3RE_HOOK(0x00551880, CComputerPlayer_RemovePendingAgent, AgentIdFn, Original_CComputerPlayer_RemovePendingAgent,
          CComputerPlayer_RemovePendingAgent);

// Purpose: Takes a note off the queue of new notes (handled or deleted).
std::uint32_t __fastcall CComputerPlayer_RemovePendingAgent(void* self, void* /*edx*/, std::uint32_t id) {
    ComputerPlayer* player = AsPlayer(self);
    return RemoveFromList(id, player->pending_agents, kPendingAgentSlots, player->pending_agent_count);
}

S3RE_HOOK(0x005518b0, CComputerPlayer_AddActiveAgent, AgentIdFn, Original_CComputerPlayer_AddActiveAgent,
          CComputerPlayer_AddActiveAgent);

// Purpose: Adds an agent to the computer player's active agents.
std::uint32_t __fastcall CComputerPlayer_AddActiveAgent(void* self, void* /*edx*/, std::uint32_t id) {
    ComputerPlayer* player = AsPlayer(self);
    return AddToList(id, player->active_agents, kActiveAgentSlots, player->active_agent_count);
}

S3RE_HOOK(0x005518e0, CComputerPlayer_RemoveActiveAgent, AgentIdFn, Original_CComputerPlayer_RemoveActiveAgent,
          CComputerPlayer_RemoveActiveAgent);

// Purpose: Removes an agent from the active agents.
std::uint32_t __fastcall CComputerPlayer_RemoveActiveAgent(void* self, void* /*edx*/, std::uint32_t id) {
    ComputerPlayer* player = AsPlayer(self);
    return RemoveFromList(id, player->active_agents, kActiveAgentSlots, player->active_agent_count);
}

S3RE_HOOK(0x00551940, CComputerPlayer_RemoveFromList200, AgentIdFn, Original_CComputerPlayer_RemoveFromList200,
          CComputerPlayer_RemoveFromList200);

// Purpose: Removes an id from the computer player's fourth id list (the one
// AI_List32Add fills; its meaning is not known yet).
std::uint32_t __fastcall CComputerPlayer_RemoveFromList200(void* self, void* /*edx*/, std::uint32_t id) {
    ComputerPlayer* player = AsPlayer(self);
    return RemoveFromList(id, player->list200, kList200Slots, player->list200_count);
}

// ---------------------------------------------------------------- activation

S3RE_HOOK(0x005515e0, CComputerPlayer_ActivateAgent, AgentIdFn, Original_CComputerPlayer_ActivateAgent,
          CComputerPlayer_ActivateAgent);

// Purpose: Makes an agent active: listed among the active agents and
// marked with the active flag.
std::uint32_t __fastcall CComputerPlayer_ActivateAgent(void* self, void* /*edx*/, std::uint32_t id) {
    AI_SetCurrentPlayer(self);
    CComputerPlayer_AddActiveAgent(self, nullptr, id);
    BlackBoardAgent* agent = AsPlayer(self)->agents[id & 0xffff];
    return BLACK_BOARD_AGENT_SetFlags(agent, nullptr, kAgentActive);
}

S3RE_HOOK(0x005515c0, CComputerPlayer_ActivateAgentOf, AgentIdFn, Original_CComputerPlayer_ActivateAgentOf,
          CComputerPlayer_ActivateAgentOf);

// Purpose: ActivateAgent for an agent given by pointer.
std::uint32_t __fastcall CComputerPlayer_ActivateAgentOf(void* self, void* edx, std::uint32_t agent) {
    return CComputerPlayer_ActivateAgent(self, nullptr, AgentIdWithCallerEdx(agent, edx));
}

S3RE_HOOK(0x00551630, CComputerPlayer_DeactivateAgent, AgentIdFn, Original_CComputerPlayer_DeactivateAgent,
          CComputerPlayer_DeactivateAgent);

// Purpose: Makes an active agent inactive again (nothing happens to an
// inactive one).
std::uint32_t __fastcall CComputerPlayer_DeactivateAgent(void* self, void* /*edx*/, std::uint32_t id) {
    BlackBoardAgent* agent = AsPlayer(self)->agents[id & 0xffff];
    if (BLACK_BOARD_AGENT_HasFlags(agent, nullptr, kAgentActive) == 0) {
        return 0;
    }
    CComputerPlayer_RemoveActiveAgent(self, nullptr, id);
    return BLACK_BOARD_AGENT_ClearFlags(agent, nullptr, kAgentActive);
}

S3RE_HOOK(0x00551610, CComputerPlayer_DeactivateAgentOf, AgentIdFn, Original_CComputerPlayer_DeactivateAgentOf,
          CComputerPlayer_DeactivateAgentOf);

// Purpose: DeactivateAgent for an agent given by pointer.
std::uint32_t __fastcall CComputerPlayer_DeactivateAgentOf(void* self, void* edx, std::uint32_t agent) {
    return CComputerPlayer_DeactivateAgent(self, nullptr, AgentIdWithCallerEdx(agent, edx));
}

// ---------------------------------------------------------------- build priorities

S3RE_HOOK(0x00551b10, CComputerPlayer_RaiseBuildPriorities, AgentVoidFn,
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

// ---------------------------------------------------------------- BLACK_BOARD_AGENT

S3RE_HOOK(0x00582400, BLACK_BOARD_AGENT_ResetLinks, AgentFn, Original_BLACK_BOARD_AGENT_ResetLinks,
          BLACK_BOARD_AGENT_ResetLinks);

// Purpose: Clears an agent's flags and type and unlinks it (no id, parent,
// sibling or children yet); part of setting up a new agent.
std::uint32_t __fastcall BLACK_BOARD_AGENT_ResetLinks(void* self, void* /*edx*/) {
    BlackBoardAgent* agent = AsAgent(self);
    agent->flags = 0;
    agent->type = 0;
    agent->unk0a = 0;
    agent->unk0c = 0;
    agent->first_child = kNoAgent;
    agent->next_sibling = kNoAgent;
    agent->parent = kNoAgent;
    agent->id = kNoAgent;
    agent->source18 = kNoAgent;
    agent->source16 = kNoAgent;
    return kNoAgent;
}

S3RE_HOOK(0x005823e0, BLACK_BOARD_AGENT_Init, AgentIdFn, Original_BLACK_BOARD_AGENT_Init, BLACK_BOARD_AGENT_Init);

// Purpose: Sets up a new agent of a given type (called by the derived
// agents' constructors).
std::uint32_t __fastcall BLACK_BOARD_AGENT_Init(void* self, void* /*edx*/, std::uint32_t type) {
    BLACK_BOARD_AGENT_ResetLinks(self, nullptr);
    AsAgent(self)->type = static_cast<std::uint16_t>(type);
    return type & 0xffff;
}

S3RE_HOOK(0x00582430, BLACK_BOARD_AGENT_MarkDispatched, AgentFn, Original_BLACK_BOARD_AGENT_MarkDispatched,
          BLACK_BOARD_AGENT_MarkDispatched);

// Purpose: Marks a note as handed to its handler (0x551510).
std::uint32_t __fastcall BLACK_BOARD_AGENT_MarkDispatched(void* self, void* /*edx*/) {
    return BLACK_BOARD_AGENT_SetFlags(self, nullptr, kAgentDispatched);
}

S3RE_HOOK(0x00582440, BLACK_BOARD_AGENT_SetFlags, AgentIdFn, Original_BLACK_BOARD_AGENT_SetFlags,
          BLACK_BOARD_AGENT_SetFlags);

// Purpose: Sets flag bits of an agent.
std::uint32_t __fastcall BLACK_BOARD_AGENT_SetFlags(void* self, void* /*edx*/, std::uint32_t bits) {
    const auto low = static_cast<std::uint16_t>(bits);
    AsAgent(self)->flags |= low;
    return low;  // AX; the original keeps the caller's upper half of EAX
}

S3RE_HOOK(0x00582450, BLACK_BOARD_AGENT_ClearFlags, AgentIdFn, Original_BLACK_BOARD_AGENT_ClearFlags,
          BLACK_BOARD_AGENT_ClearFlags);

// Purpose: Clears flag bits of an agent.
std::uint32_t __fastcall BLACK_BOARD_AGENT_ClearFlags(void* self, void* /*edx*/, std::uint32_t bits) {
    AsAgent(self)->flags &= static_cast<std::uint16_t>(~bits);
    return ~bits;
}

S3RE_HOOK(0x00582460, BLACK_BOARD_AGENT_HasFlags, AgentIdFn, Original_BLACK_BOARD_AGENT_HasFlags,
          BLACK_BOARD_AGENT_HasFlags);

// Purpose: Whether all the given flag bits of an agent are set.
std::uint32_t __fastcall BLACK_BOARD_AGENT_HasFlags(void* self, void* /*edx*/, std::uint32_t bits) {
    const auto low = static_cast<std::uint16_t>(bits);
    return (AsAgent(self)->flags & low) == low ? 1 : 0;
}

S3RE_HOOK(0x005824d0, BLACK_BOARD_AGENT_FindChildOfType, AgentIdFn, Original_BLACK_BOARD_AGENT_FindChildOfType,
          BLACK_BOARD_AGENT_FindChildOfType);

// Purpose: Finds a child agent of a given type (e.g. whether a house
// request already has a building order; callers 0x5656e0, 0x5789b0).
std::uint32_t __fastcall BLACK_BOARD_AGENT_FindChildOfType(void* self, void* /*edx*/, std::uint32_t type) {
    std::uint16_t child = AsAgent(self)->first_child;
    if (child == kNoAgent) {
        return 0;
    }
    ComputerPlayer* player = g_aiPlayer;
    do {
        BlackBoardAgent* candidate = player->agents[child];
        if (candidate->type == static_cast<std::uint16_t>(type)) {
            return reinterpret_cast<std::uint32_t>(candidate);
        }
        child = candidate->next_sibling;
    } while (child != kNoAgent);
    return 0;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// One buffer per side holds a computer player, a pool of 16 agents (ids
// 0..15; ids 16..299 are null) and an AiPlayerData node. Pointers are set
// up per side and turned back into buffer offsets before the comparison.
// The globals AI_SetCurrentPlayer writes are saved and restored; after each
// call g_aiPlayer must point at that side's player and the other three
// globals must agree.

namespace {

struct ComputerPlayerFunctions {
    AgentIdFn list[7];
    AgentIdFn activate;
    AgentIdFn activateOf;
    AgentIdFn deactivate;
    AgentIdFn deactivateOf;
    AgentVoidFn raise;
    AgentFn resetLinks;
    AgentIdFn init;
    AgentFn markDispatched;
    AgentIdFn setFlags;
    AgentIdFn clearFlags;
    AgentIdFn hasFlags;
    AgentIdFn findChild;
};

ComputerPlayerFunctions OriginalComputerPlayer() {
    return {{Original_CComputerPlayer_AddRootAgent, Original_CComputerPlayer_RemoveRootAgent,
             Original_CComputerPlayer_AddPendingAgent, Original_CComputerPlayer_RemovePendingAgent,
             Original_CComputerPlayer_AddActiveAgent, Original_CComputerPlayer_RemoveActiveAgent,
             Original_CComputerPlayer_RemoveFromList200},
            Original_CComputerPlayer_ActivateAgent,
            Original_CComputerPlayer_ActivateAgentOf,
            Original_CComputerPlayer_DeactivateAgent,
            Original_CComputerPlayer_DeactivateAgentOf,
            Original_CComputerPlayer_RaiseBuildPriorities,
            Original_BLACK_BOARD_AGENT_ResetLinks,
            Original_BLACK_BOARD_AGENT_Init,
            Original_BLACK_BOARD_AGENT_MarkDispatched,
            Original_BLACK_BOARD_AGENT_SetFlags,
            Original_BLACK_BOARD_AGENT_ClearFlags,
            Original_BLACK_BOARD_AGENT_HasFlags,
            Original_BLACK_BOARD_AGENT_FindChildOfType};
}

ComputerPlayerFunctions OurComputerPlayer() {
    return {{&CComputerPlayer_AddRootAgent, &CComputerPlayer_RemoveRootAgent, &CComputerPlayer_AddPendingAgent,
             &CComputerPlayer_RemovePendingAgent, &CComputerPlayer_AddActiveAgent,
             &CComputerPlayer_RemoveActiveAgent, &CComputerPlayer_RemoveFromList200},
            &CComputerPlayer_ActivateAgent,
            &CComputerPlayer_ActivateAgentOf,
            &CComputerPlayer_DeactivateAgent,
            &CComputerPlayer_DeactivateAgentOf,
            &CComputerPlayer_RaiseBuildPriorities,
            &BLACK_BOARD_AGENT_ResetLinks,
            &BLACK_BOARD_AGENT_Init,
            &BLACK_BOARD_AGENT_MarkDispatched,
            &BLACK_BOARD_AGENT_SetFlags,
            &BLACK_BOARD_AGENT_ClearFlags,
            &BLACK_BOARD_AGENT_HasFlags,
            &BLACK_BOARD_AGENT_FindChildOfType};
}

constexpr std::size_t kPoolAgents = 16;
constexpr std::size_t kAgentStride = 0x20;
constexpr std::size_t kPoolOffset = 0x800;
constexpr std::size_t kDataOffset = kPoolOffset + kPoolAgents * kAgentStride;
constexpr std::size_t kDataBytes = 0x400;
constexpr std::size_t kBufferBytes = kDataOffset + kDataBytes;
static_assert(sizeof(ComputerPlayer) <= kPoolOffset && sizeof(BlackBoardAgent) <= kAgentStride);
static_assert(offsetof(AiPlayerData, best) + kBuildingTypeCount * sizeof(AiCandidate) <= kDataBytes);

BlackBoardAgent* PoolAgent(std::uint8_t* buffer, std::size_t id) {
    return reinterpret_cast<BlackBoardAgent*>(buffer + kPoolOffset + id * kAgentStride);
}

// Pointers of one side: agents 0..15 into the pool, the data node.
void Link(std::uint8_t* buffer) {
    auto* player = reinterpret_cast<ComputerPlayer*>(buffer);
    for (std::size_t id = 0; id < kMaxAgents; ++id) {
        player->agents[id] = id < kPoolAgents ? PoolAgent(buffer, id) : nullptr;
    }
    player->data = reinterpret_cast<std::uint32_t>(buffer + kDataOffset);
}

// The same pointers as buffer offsets (for the comparison).
void Unlink(std::uint8_t* buffer) {
    auto* player = reinterpret_cast<ComputerPlayer*>(buffer);
    const auto base = reinterpret_cast<std::uint32_t>(buffer);
    for (BlackBoardAgent*& agent : player->agents) {
        if (agent != nullptr) {
            agent = reinterpret_cast<BlackBoardAgent*>(reinterpret_cast<std::uint32_t>(agent) - base);
        }
    }
    player->data -= base;
}

std::uint16_t SmallId(test::Rng& rng) {
    return rng.Chance(1, 4) ? kNoAgent : static_cast<std::uint16_t>(rng.Below(kPoolAgents));
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
    fillList(player->root_agents, kRootAgentSlots);
    fillList(player->pending_agents, kPendingAgentSlots);
    fillList(player->active_agents, kActiveAgentSlots);
    fillList(player->list200, kList200Slots);
    player->setup_index = rng.Below(64);
    // Agents: flags with bits 1 and 4 often set; children chains that only
    // go to higher ids (so they end).
    for (std::size_t id = 0; id < kPoolAgents; ++id) {
        BlackBoardAgent* agent = PoolAgent(buffer.data(), id);
        agent->flags = static_cast<std::uint16_t>(rng.Chance(1, 2) ? rng.Below(8) : rng.Next32());
        agent->type = static_cast<std::uint16_t>(rng.Chance(1, 8) ? rng.Next32() : rng.Below(4));
        agent->id = static_cast<std::uint16_t>(rng.Chance(1, 2) ? id : rng.Below(kPoolAgents));
        agent->first_child = SmallId(rng);
        const std::uint32_t later = static_cast<std::uint32_t>(kPoolAgents - id - 1);
        agent->next_sibling = later == 0 || rng.Chance(1, 4)
                                  ? kNoAgent
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
        // agent behind the id (the original dereferences agents[id]).
        const bool needsAgent = op == 7 || op == 9;
        const std::uint32_t id = (ctx.rng().Chance(1, 3) ? ctx.rng().Next32() & 0xffff0000u : 0) |
                                 (needsAgent || ctx.rng().Chance(1, 2) ? ctx.rng().Below(kPoolAgents)
                                                                       : ctx.rng().Below(24));
        const std::uint32_t agentIndex = ctx.rng().Below(kPoolAgents);
        const std::uint32_t bits = ctx.rng().Chance(1, 2) ? 1u << ctx.rng().Below(4) : ctx.rng().Next32();
        auto* edx = reinterpret_cast<void*>(ctx.rng().Next32());
        RestoreGlobals(saved);
        std::uint32_t ra = 0, rb = 0;
        std::uint32_t eaxMask = 0xffffffffu;
        AiGlobals ga{}, gb{};
        // Runs the same call on both sides (agent methods on agent
        // `agentIndex`, player methods on the player).
        auto run = [&](auto&& call) {
            g_aiPlayer = reinterpret_cast<ComputerPlayer*>(a.data());
            ra = call(original, a.data());
            ga = SaveGlobals();
            g_aiPlayer = reinterpret_cast<ComputerPlayer*>(b.data());
            rb = call(candidate, b.data());
            gb = SaveGlobals();
        };
        auto agentOf = [&](std::uint8_t* buffer) { return PoolAgent(buffer, agentIndex); };
        auto agentArg = [&](std::uint8_t* buffer) { return reinterpret_cast<std::uint32_t>(agentOf(buffer)); };
        if (op < 7) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) { return f.list[op](p, nullptr, id); });
        } else if (op == 7) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) { return f.activate(p, nullptr, id); });
        } else if (op == 8) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) { return f.activateOf(p, edx, agentArg(p)); });
        } else if (op == 9) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) { return f.deactivate(p, nullptr, id); });
        } else if (op == 10) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) {
                return f.deactivateOf(p, edx, agentArg(p));
            });
        } else if (op == 11) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) {
                f.raise(p, nullptr);
                return 0u;
            });
        } else if (op == 12) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) { return f.resetLinks(agentOf(p), nullptr); });
        } else if (op == 13) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) { return f.init(agentOf(p), nullptr, bits); });
        } else if (op == 14) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) {
                return f.markDispatched(agentOf(p), nullptr);
            });
            eaxMask = 0xffff;  // the upper half is the caller's EAX
        } else if (op == 15) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) {
                return f.setFlags(agentOf(p), nullptr, bits);
            });
            eaxMask = 0xffff;
        } else if (op == 16) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) {
                return f.clearFlags(agentOf(p), nullptr, bits);
            });
        } else if (op == 17) {
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) { return f.hasFlags(agentOf(p), nullptr, bits); });
        } else {
            const std::uint32_t type = ctx.rng().Chance(1, 4) ? ctx.rng().Next32() : ctx.rng().Below(4);
            run([&](const ComputerPlayerFunctions& f, std::uint8_t* p) {
                return f.findChild(agentOf(p), nullptr, type);
            });
            // A found agent: compare its offset in the buffer.
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
        // AI_SetCurrentPlayer's globals (ActivateAgent): each side's player,
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

std::uint32_t __fastcall WrongDeactivateAgent(void* self, void* /*edx*/, std::uint32_t id) {
    // Wrong: removes the agent from the list even when it is not active.
    CComputerPlayer_RemoveActiveAgent(self, nullptr, id);
    return CComputerPlayer_DeactivateAgent(self, nullptr, id);
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
    const std::uint16_t child = AsAgent(self)->first_child;
    if (child == kNoAgent) {
        return 0;
    }
    BlackBoardAgent* candidate = g_aiPlayer.get()->agents[child];
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
    probe([](ComputerPlayerFunctions& f) { f.deactivate = &WrongDeactivateAgent; });
    probe([](ComputerPlayerFunctions& f) { f.raise = &WrongRaiseBuildPriorities; });
    probe([](ComputerPlayerFunctions& f) { f.findChild = &WrongFindChildOfType; });
    probe([](ComputerPlayerFunctions& f) { f.list[3] = f.list[2]; });
}

}  // namespace s3re::game
