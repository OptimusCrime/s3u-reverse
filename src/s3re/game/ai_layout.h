#pragma once

// Globals and object layouts shared by the computer-player helpers
// (Rainer\*.cpp; ai_helpers.cpp, ai_small_helpers.cpp, ai_evaland.cpp).
// Only the fields our replacements touch are named; the rest is padding
// (unkXX) whose meaning is unknown. Offsets are checked by static_asserts.

#include "game/enums.h"
#include "game/layout.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game::ai {

// The AI code reaches the game through globals set before each player's
// turn (AI_SetCurrentPlayer and 0x551b80): g_aiWorld, g_aiPlayer,
// g_aiPlayerData, ... in game/globals.h.
constexpr std::size_t kAgentVectorCount = 20;  // g_aiAgentVectors

#pragma pack(push, 1)

// ---------------------------------------------------------------- player setup
// An entry of the settings table g_aiPlayerSetups (contents not known).
struct AiPlayerSetup {
    std::uint8_t unk000[0x180];
};
static_assert(sizeof(AiPlayerSetup) == 0x180);

// ---------------------------------------------------------------- blackboard agents
// BLACK_BOARD_AGENT (RTTI; vtable 0x65a828, 13 slots) and the classes
// derived from it. A computer player keeps up to 300 of them by id in
// ComputerPlayer::agents; they form a tree (parent / first child / next
// sibling, all ids, 0xffff = none). Layout from 0x582400 (which resets it),
// 0x551330 (deletes an agent and its subtree) and 0x5824d0 (child search);
// re/computer_players.md. The derived classes' own fields follow at +0x1a.
constexpr std::size_t kMaxAgents = 300;
constexpr std::uint16_t kNoAgent = 0xffff;
constexpr std::uint16_t kAgentDispatched = 0x0001;  // set before the note goes to its handler (0x551510)
constexpr std::uint16_t kAgentActive = 0x0004;      // in ComputerPlayer::active_agents (0x5515e0 / 0x551630)

struct BlackBoardAgent {
    std::uint32_t vtable;         // 0x00
    std::uint16_t flags;          // 0x04 kAgent* bits
    std::uint16_t type;           // 0x06 indexes ComputerPlayer::handlers; FindChildOfType compares it
    std::uint16_t id;             // 0x08 own index in ComputerPlayer::agents (0xffff after the reset)
    std::uint16_t unk0a;          // 0x0a 0 after the reset
    std::uint32_t unk0c;          // 0x0c 0 after the reset
    std::uint16_t parent;         // 0x10 id or kNoAgent (then it is a root agent)
    std::uint16_t next_sibling;   // 0x12
    std::uint16_t first_child;    // 0x14
    std::uint16_t source16;       // 0x16 index into ComputerPlayer::sources (0x582480 calls its slot 9)
    std::uint16_t source18;       // 0x18 index into ComputerPlayer::sources (0x582510 calls its slot 10)
};
static_assert(offsetof(BlackBoardAgent, flags) == 0x04);
static_assert(offsetof(BlackBoardAgent, id) == 0x08);
static_assert(offsetof(BlackBoardAgent, parent) == 0x10);
static_assert(offsetof(BlackBoardAgent, first_child) == 0x14);
static_assert(offsetof(BlackBoardAgent, source18) == 0x18);
static_assert(sizeof(BlackBoardAgent) == 0x1a);

// ---------------------------------------------------------------- CComputerPlayer
// One per computer player (20 pointers at CLogicThread+0x350; constructor
// 0x550900, per-tick entry 0x550f80). Size at least 0x780. The four id
// lists are AI_WordListInsert/Remove lists (0xffff = free, packed at the
// front) with a count of the entries added.
constexpr std::size_t kRootAgentSlots = 100;
constexpr std::size_t kPendingAgentSlots = 50;
constexpr std::size_t kActiveAgentSlots = 100;
constexpr std::size_t kList200Slots = 50;
constexpr std::size_t kKnowledgeSources = 11;

struct ComputerPlayer {
    std::uint32_t vtable;                           // 0x000 0x65a530
    std::uint16_t root_agents[kRootAgentSlots];     // 0x004 agents without a parent
    std::uint16_t root_agent_count;                 // 0x0cc
    std::uint16_t pending_agents[kPendingAgentSlots];  // 0x0ce new notes, handed to their handler by 0x5514c0
    std::uint16_t pending_agent_count;              // 0x132
    std::uint16_t active_agents[kActiveAgentSlots];  // 0x134 agents with kAgentActive
    std::uint16_t active_agent_count;               // 0x1fc
    std::uint16_t unk1fe;
    std::uint16_t list200[kList200Slots];           // 0x200 (AI_List32Add; meaning unknown)
    std::uint16_t list200_count;                    // 0x264
    std::uint16_t handlers[6];                      // 0x266 per agent type: index into sources, 0xffff = none (0x551510)
    std::uint8_t player_number;                     // 0x272
    std::uint8_t unk273;
    std::uint16_t timer274;                         // 0x274 the three step counters of 0x550f80
    std::uint16_t unk276;
    std::uint16_t timer278;                         // 0x278
    std::uint16_t unk27a;
    std::uint16_t timer27c;                         // 0x27c
    std::uint16_t unk27e;
    BlackBoardAgent* agents[kMaxAgents];            // 0x280 by id
    BlackBoardAgent* sources[kKnowledgeSources];    // 0x730 the knowledge-source agents (T_ECONOMY_AGENT, ...); [7] runs every step
    std::uint32_t world;                            // 0x75c CGameData*
    std::uint32_t message_queue;                    // 0x760 the logic->net MessageQueue (commands are pushed there)
    std::uint32_t steps;                            // 0x764 +1 per 0x550f80
    std::uint8_t unk768;
    std::uint8_t pad769[3];
    std::uint32_t player;                           // 0x76c the player index (constructor argument)
    std::uint32_t setup_index;                      // 0x770 index into g_aiPlayerSetups
    std::uint32_t settlement_lists;                 // 0x774 12-byte entries per player: [+4] list of AiPlayerData nodes
    std::uint32_t data;                             // 0x778 AiPlayerData* (the current node of that list)
    std::uint32_t data_key;                         // 0x77c word +8 of the current node
};
static_assert(offsetof(ComputerPlayer, root_agents) == 0x004);
static_assert(offsetof(ComputerPlayer, root_agent_count) == 0x0cc);
static_assert(offsetof(ComputerPlayer, pending_agents) == 0x0ce);
static_assert(offsetof(ComputerPlayer, pending_agent_count) == 0x132);
static_assert(offsetof(ComputerPlayer, active_agents) == 0x134);
static_assert(offsetof(ComputerPlayer, active_agent_count) == 0x1fc);
static_assert(offsetof(ComputerPlayer, list200) == 0x200);
static_assert(offsetof(ComputerPlayer, list200_count) == 0x264);
static_assert(offsetof(ComputerPlayer, handlers) == 0x266);
static_assert(offsetof(ComputerPlayer, player_number) == 0x272);
static_assert(offsetof(ComputerPlayer, timer27c) == 0x27c);
static_assert(offsetof(ComputerPlayer, agents) == 0x280);
static_assert(offsetof(ComputerPlayer, sources) == 0x730);
static_assert(offsetof(ComputerPlayer, world) == 0x75c);
static_assert(offsetof(ComputerPlayer, player) == 0x76c);
static_assert(offsetof(ComputerPlayer, setup_index) == 0x770);
static_assert(offsetof(ComputerPlayer, data) == 0x778);
static_assert(sizeof(ComputerPlayer) == 0x780);

// ---------------------------------------------------------------- AI player data
// g_aiPlayerData: the best place found per candidate index (probably per
// building type, AI_RecordBestCandidate) and a per-index value reset to
// its default from g_aiCandidateDefaults. Indexes are not range-checked by the
// original (callers pass up to 0xffff), so the arrays are indexed past their
// declared ends like the original's address arithmetic.
struct AiCandidate {
    std::uint16_t score;  // +0
    std::uint16_t a;      // +2 (probably x)
    std::uint16_t b;      // +4 (probably y)
};
static_assert(sizeof(AiCandidate) == 6);

struct AiPlayerData {
    std::uint8_t unk000[0x26];
    // Indexes 0..56 are building types: best[type].score is the build
    // priority that 0x551b10 raises by g_aiBuildPrioritySteps[type] up to
    // g_aiBuildPriorityCaps[type] (only while non-zero); candidate_values
    // are per-type countdowns (0x550f80 lowers 57 of them by one per step
    // in every node of the list). The rest of best[] is not known.
    AiCandidate best[114];                // 0x026
    std::uint16_t candidate_values[1];    // 0x2d2 (count unknown, at least 57)
};
static_assert(offsetof(AiPlayerData, best) == 0x26);
static_assert(offsetof(AiPlayerData, candidate_values) == 0x2d2);

// ---------------------------------------------------------------- agent data
// The computer players' census of the world (AI_ResetAgentData clears it,
// constructor 0x55b6b0; size 0x1ad8): buildings per player and type
// (0x55be00), soldiers per player (filled elsewhere, not yet known) and
// the military strength of each player's team (0x55bea0). Other fields'
// meanings unknown.
constexpr std::size_t kCensusSoldierKinds = 9;  // probably 3 weapons x 3 levels (every third weighs double)

struct AiAgentData {
    std::uint8_t unk000;                 // 0x000
    std::uint8_t buildings_counted;      // 0x001 0 while 0x55be00 counts, then 1
    std::uint8_t unk002;                 // 0x002 set to 1 by 0x55be00
    std::uint8_t unk003;
    std::uint16_t words004[20][10];      // 0x004
    std::uint8_t bytes194[20][10];       // 0x194
    std::uint32_t team_strength[20];     // 0x25c per player: soldiers and towers of its team (0x55bea0)
    std::uint8_t team_strength_valid;    // 0x2ac set to 1 by 0x55bea0
    std::uint8_t unk2ad[7];
    std::uint32_t dwords2b4[20];         // 0x2b4
    std::uint32_t dwords304[20];         // 0x304
    std::uint32_t dwords354[20];         // 0x354
    std::uint32_t dwords3a4[20];         // 0x3a4
    std::uint32_t dwords3f4[20];         // 0x3f4
    std::uint32_t soldiers[20][kCensusSoldierKinds];      // 0x444 per player
    std::uint32_t buildings[20][kBuildingTypeCount];      // 0x714 finished buildings per player and type
    std::uint32_t dwords18e4[0x7d];      // 0x18e4..0x1ad4
};
static_assert(offsetof(AiAgentData, words004) == 0x4);
static_assert(offsetof(AiAgentData, bytes194) == 0x194);
static_assert(offsetof(AiAgentData, team_strength) == 0x25c);
static_assert(offsetof(AiAgentData, team_strength_valid) == 0x2ac);
static_assert(offsetof(AiAgentData, dwords2b4) == 0x2b4);
static_assert(offsetof(AiAgentData, dwords3f4) == 0x3f4);
static_assert(offsetof(AiAgentData, soldiers) == 0x444);
static_assert(offsetof(AiAgentData, buildings) == 0x714);
static_assert(offsetof(AiAgentData, dwords18e4) == 0x18e4);
static_assert(sizeof(AiAgentData) == 0x1ad8);

// ---------------------------------------------------------------- 50-entry list
// The object AI_List32Add works on (meaning unknown): a list of up to 50
// ids (0xffff = free slot) and a count of the entries added.
struct AiList50Owner {
    std::uint8_t unk000[0x200];
    std::uint16_t entries[50];  // 0x200
    std::uint16_t count;        // 0x264
};
static_assert(offsetof(AiList50Owner, entries) == 0x200);
static_assert(offsetof(AiList50Owner, count) == 0x264);

// ---------------------------------------------------------------- linked list
// A 12-byte list object (element destructor 0x5591e0 frees its nodes); the
// head node's first dword points at the first node.
struct AiListNode {
    std::uint32_t next;  // +0 probably the next node
    std::uint32_t prev;  // +4
};

struct AiList {
    std::uint32_t unk0;
    AiListNode* head;     // +4
    std::uint32_t count;  // +8 probably the node count
};
static_assert(sizeof(AiList) == 12);
static_assert(offsetof(AiList, head) == 4);

// ---------------------------------------------------------------- land evaluation
// A cell of the land evaluation grid (0xc4 bytes; one per 8x8 tiles).
struct AiGridCell {
    std::uint8_t unk00[3];
    std::uint8_t value03;          // 0x03 summed into sum10
    std::uint8_t value04;          // 0x04 summed into sum0f
    std::uint8_t values05[6];      // 0x05..0x0a summed into sums12
    std::uint8_t value0b;          // 0x0b
    std::uint8_t value0c;          // 0x0c
    std::uint8_t value0d;          // 0x0d
    std::uint8_t unk0e;
    std::uint8_t sum0f;            // 0x0f
    std::uint16_t sum10;           // 0x10
    std::uint16_t sums12[6];       // 0x12..0x1c
    std::uint8_t percent1e;        // 0x1e sum of value0b * 100 / 576
    std::uint8_t percent1f;        // 0x1f the same for value0c
    std::uint8_t percent20;        // 0x20 the same for value0d
    std::uint8_t unk21[0xc4 - 0x21];
};
static_assert(sizeof(AiGridCell) == 0xc4);
static_assert(offsetof(AiGridCell, sum0f) == 0x0f);
static_assert(offsetof(AiGridCell, sums12) == 0x12);
static_assert(offsetof(AiGridCell, percent1e) == 0x1e);

// CEvaLand (our name, from Rainer\Cevaland.cpp; 0x15c bytes): the computer
// players' shared land evaluation object.
struct AiLandEvaluation {
    AiList lists[20];          // 0x000
    std::uint8_t unk0f0[0x50];
    std::uint32_t width;       // 0x140 grid width (cells)
    std::uint32_t height;      // 0x144 grid height
    std::uint32_t unk148;      // 0x148 (cleared with the sizes)
    std::uint32_t unk14c;      // 0x14c
    std::uint32_t unk150;
    std::uint32_t unk154;
    AiGridCell* cells;         // 0x158 the grid
};
static_assert(offsetof(AiLandEvaluation, width) == 0x140);
static_assert(offsetof(AiLandEvaluation, height) == 0x144);
static_assert(offsetof(AiLandEvaluation, cells) == 0x158);
static_assert(sizeof(AiLandEvaluation) == 0x15c);

// A vector of 16-byte entries (the 20 of g_aiAgentVectors).
struct AiVector {
    std::uint32_t begin;  // +0
    std::uint32_t end;    // +4 (end = begin empties it)
    std::uint32_t unk8;
    std::uint32_t unkc;
};
static_assert(sizeof(AiVector) == 0x10);

// ---------------------------------------------------------------- tagged record
// The objects AI_SetWord4 (0x55ca60) starts: their constructors (e.g.
// 0x55ebe0 in Rainer\Cevaland.cpp, 0x560dd0) pass a constant tag (4, 7,
// 0xa...) and then clear their own fields from +8. No vtable is stored, and
// the class is not identified; the tag is probably the record's kind.
struct AiTaggedRecord {
    std::uint32_t unk00;  // +0
    std::uint16_t tag;    // +4
};
static_assert(offsetof(AiTaggedRecord, tag) == 4);

#pragma pack(pop)

}  // namespace s3re::game::ai
