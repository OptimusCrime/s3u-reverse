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

// ---------------------------------------------------------------- blackboard
// The computer players work like a blackboard system (RTTI names):
// - BLACK_BOARD_AGENT (vtable 0x65a828, 13 slots) and its 11 subclasses are
//   the agents ("knowledge sources"), one of each kind 0..10 per computer
//   player in ComputerPlayer::agents, made by the factory 0x56f6d0
//   (fastcall kind). Kind 3 GLOBAL_EXPAND_EMPIRE_AGENT, 5
//   COMPUTER_PLAYER_AGENT, 6 T_MAP_WORKER_AGENT, 7 T_ATTACK_AGENT, 8
//   T_ECONOMY_AGENT, 9 T_SAFETY_AGENT, 10 CCentrumCheckAgent; 0
//   LOCAL_WARE_REQUEST_AGENT; 1, 2, 4 the house request / ground request /
//   do-build-house agents (to confirm). Their word +4 is the kind
//   (AiTaggedRecord below; AI_SetWord4).
// - BLACK_BOARD_NOTE (vtable 0x65a56c) and its subclasses HOUSE_REQUEST_NOTE,
//   HOUSE_GROUND_REQUEST_NOTE, LOCAL_WARE_REQUEST_NOTE, T_DO_BUILD_HOUSE_NOTE
//   are the notes (requests) on the blackboard: up to 300 per computer
//   player by id in ComputerPlayer::notes, in a tree (parent / first child /
//   next sibling, all ids, 0xffff = none). Layout from 0x582400 (which
//   resets it), 0x551330 (deletes a note and its subtree) and 0x5824d0
//   (child search); re/computer_players.md. The subclasses' own fields
//   follow at +0x1a.
constexpr std::size_t kMaxNotes = 300;
constexpr std::uint16_t kNoNote = 0xffff;
constexpr std::uint16_t kNoteDispatched = 0x0001;  // set before the note goes to its handler (0x551510)
constexpr std::uint16_t kNoteActive = 0x0004;      // in ComputerPlayer::active_notes (0x5515e0 / 0x551630)

struct BlackBoardNote {
    std::uint32_t vtable;         // 0x00
    std::uint16_t flags;          // 0x04 kNote* bits
    std::uint16_t type;           // 0x06 indexes ComputerPlayer::handlers; FindChildOfType compares it
    std::uint16_t id;             // 0x08 own index in ComputerPlayer::notes (0xffff after the reset)
    std::uint16_t unk0a;          // 0x0a 0 after the reset
    std::uint32_t unk0c;          // 0x0c 0 after the reset
    std::uint16_t parent;         // 0x10 id or kNoNote (then it is a root note)
    std::uint16_t next_sibling;   // 0x12
    std::uint16_t first_child;    // 0x14
    std::uint16_t agent16;        // 0x16 agent kind (ComputerPlayer::agents) 0x582480 calls slot 9 of
    std::uint16_t agent18;        // 0x18 agent kind 0x582510 calls slot 10 of
};
static_assert(offsetof(BlackBoardNote, flags) == 0x04);
static_assert(offsetof(BlackBoardNote, id) == 0x08);
static_assert(offsetof(BlackBoardNote, parent) == 0x10);
static_assert(offsetof(BlackBoardNote, first_child) == 0x14);
static_assert(offsetof(BlackBoardNote, agent18) == 0x18);
static_assert(sizeof(BlackBoardNote) == 0x1a);

// ---------------------------------------------------------------- CComputerPlayer
// One per computer player (20 pointers at CLogicThread+0x350; constructor
// 0x550900, per-tick entry 0x550f80). Size at least 0x780. The four id
// lists are AI_WordListInsert/Remove lists (0xffff = free, packed at the
// front) with a count of the entries added.
constexpr std::size_t kRootNoteSlots = 100;
constexpr std::size_t kPendingNoteSlots = 50;
constexpr std::size_t kActiveNoteSlots = 100;
constexpr std::size_t kList200Slots = 50;
constexpr std::size_t kAgentKinds = 11;

struct ComputerPlayer {
    std::uint32_t vtable;                              // 0x000 0x65a530
    std::uint16_t root_notes[kRootNoteSlots];          // 0x004 notes without a parent
    std::uint16_t root_note_count;                     // 0x0cc
    std::uint16_t pending_notes[kPendingNoteSlots];    // 0x0ce new notes, handed to their handler by 0x5514c0
    std::uint16_t pending_note_count;                  // 0x132
    std::uint16_t active_notes[kActiveNoteSlots];      // 0x134 notes with kNoteActive
    std::uint16_t active_note_count;                   // 0x1fc
    std::uint16_t unk1fe;
    std::uint16_t list200[kList200Slots];              // 0x200 (AI_List32Add; meaning unknown)
    std::uint16_t list200_count;                       // 0x264
    std::uint16_t handlers[6];                         // 0x266 per note type: agent kind, 0xffff = none (0x551510)
    std::uint8_t player_number;                        // 0x272
    std::uint8_t unk273;
    std::uint16_t timer274;                            // 0x274 the three step counters of 0x550f80
    std::uint16_t unk276;
    std::uint16_t timer278;                            // 0x278
    std::uint16_t unk27a;
    std::uint16_t timer27c;                            // 0x27c
    std::uint16_t unk27e;
    BlackBoardNote* notes[kMaxNotes];                  // 0x280 by id
    std::uint32_t agents[kAgentKinds];                 // 0x730 BLACK_BOARD_AGENT* by kind; 0x550f80 runs slot 8 of [7] (probably T_ATTACK_AGENT) when the player has no nodes
    std::uint32_t world;                               // 0x75c CGameData*
    std::uint32_t message_queue;                       // 0x760 the logic->net MessageQueue (commands are pushed there)
    std::uint32_t steps;                               // 0x764 +1 per 0x550f80
    std::uint8_t unk768;
    std::uint8_t pad769[3];
    std::uint32_t player;                              // 0x76c the player index (constructor argument)
    std::uint32_t setup_index;                         // 0x770 index into g_aiPlayerSetups
    std::uint32_t settlement_lists;                    // 0x774 12-byte entries per player: [+4] list of AiPlayerData nodes, [+8] their count
    std::uint32_t data;                                // 0x778 AiPlayerData* (the current node of that list)
    std::uint32_t data_key;                            // 0x77c word +8 of the current node
};
static_assert(offsetof(ComputerPlayer, root_notes) == 0x004);
static_assert(offsetof(ComputerPlayer, root_note_count) == 0x0cc);
static_assert(offsetof(ComputerPlayer, pending_notes) == 0x0ce);
static_assert(offsetof(ComputerPlayer, pending_note_count) == 0x132);
static_assert(offsetof(ComputerPlayer, active_notes) == 0x134);
static_assert(offsetof(ComputerPlayer, active_note_count) == 0x1fc);
static_assert(offsetof(ComputerPlayer, list200) == 0x200);
static_assert(offsetof(ComputerPlayer, list200_count) == 0x264);
static_assert(offsetof(ComputerPlayer, handlers) == 0x266);
static_assert(offsetof(ComputerPlayer, player_number) == 0x272);
static_assert(offsetof(ComputerPlayer, timer27c) == 0x27c);
static_assert(offsetof(ComputerPlayer, notes) == 0x280);
static_assert(offsetof(ComputerPlayer, agents) == 0x730);
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
    std::uint8_t unk000[0xc];
    std::uint32_t basic_economy;          // 0x00c 1 once a sawmill, woodcutter, small tower and the race's basics (stonecutter, forester) stand (AI::CheckBasicEconomy 0x5720c0)
    std::uint8_t unk010[0x16];
    // Indexes 0..56 are building types: best[type].score is the build
    // priority that 0x551b10 raises by g_aiBuildPrioritySteps[type] up to
    // g_aiBuildPriorityCaps[type] (only while non-zero); candidate_values
    // are per-type countdowns (0x550f80 lowers 57 of them by one per step
    // in every node of the list). best[57 + type] is the candidate taken
    // for building (0x58ecb0 moves best[type] there and clears it; the
    // T_ECONOMY_AGENT's toolsmith condition 0x569160 waits while one is
    // there).
    AiCandidate best[114];                // 0x026
    std::uint16_t candidate_values[kBuildingTypeCount];  // 0x2d2 (0x56a1a0 skips a type while non-zero)
    // Computer players A2 (session 48), read by the T_ECONOMY_AGENT:
    std::uint16_t building_counts[kBuildingTypeCount];   // 0x344 per type; filled by 0x572420, copied by 0x567910
    std::uint8_t type_blocked[kBuildingTypeCount];       // 0x3b6 per type; non-zero: 0x56a1a0 skips the type (cleared by 0x565630, 0x573890)
    std::uint8_t unk3ef[0x550 - 0x3ef];
    std::uint32_t unk550;                                 // 0x550 the fishery condition 0x5693c0 needs it non-zero (probably "has fishing water")
};
static_assert(offsetof(AiPlayerData, basic_economy) == 0xc);
static_assert(offsetof(AiPlayerData, best) == 0x26);
static_assert(offsetof(AiPlayerData, candidate_values) == 0x2d2);
static_assert(offsetof(AiPlayerData, building_counts) == 0x344);
static_assert(offsetof(AiPlayerData, type_blocked) == 0x3b6);
static_assert(offsetof(AiPlayerData, unk550) == 0x550);
// best[kOrderedCandidates + type]: the candidate taken for building.
constexpr std::size_t kOrderedCandidates = kBuildingTypeCount;

// ---------------------------------------------------------------- T_ECONOMY_AGENT
// The economy agent (kind 8; vtable 0x65a630, constructor 0x567850, run
// 0x567890). Each run, 0x567910 takes a snapshot of the current node's
// area: its population, goods in stock (AI_AreaGoodsAmount 0x59dc10) and
// the node's buildings per type (AiPlayerData::building_counts; the
// race-specific fields hold the race's own building, 0 for other races).
// Then 0x56a1a0 runs one condition per building type (0x568250..0x569d20,
// ai_economy_conditions.cpp) that asks for the type with a priority
// through AI::RecordBestCandidate (0x58ec50).
struct EconomyAgent {
    std::uint32_t vtable;               // 0x00
    std::uint16_t kind;                 // 0x04 8 (AI_SetWord4)
    std::uint16_t unk06;
    std::int32_t race;                  // 0x08 Race of g_aiPlayerNumber (Player::race)
    std::uint16_t area;                 // 0x0c the node's key (ComputerPlayer::data_key): an economy area
    std::uint16_t unk0e;
    std::uint16_t run_count;            // 0x10 +1 per run; past player + 14 reset, and 0x56a000.. run
    std::uint16_t unk12;
    std::uint32_t unk14[6];             // 0x14..0x2b (cleared with run_count by the constructor)
    std::int32_t stock_ratio;           // 0x2c buildings * 100 / wanted, of the last stage-based condition
    std::uint32_t forester_priority;    // 0x30 the forester condition's last priority (0x5682b0)
    std::uint16_t stage;                // 0x34 population stage 0..3 (g_aiPopulationStages)
    std::uint8_t unk36[8];
    std::uint16_t population;           // 0x3e settlers of class 0 in the area (CGameData::CountAreaSettlersOfClass0), or a per-player global
    std::uint16_t carriers;             // 0x40 settlers of type 0 in the area (0x59dc70)
    std::uint16_t logs;                 // 0x42 goods in the area
    std::uint16_t boards;               // 0x44
    std::uint16_t coal;                 // 0x46
    std::uint16_t iron;                 // 0x48
    std::uint16_t unk4a;
    std::uint16_t gold_ore;             // 0x4c (set by the gold smelter condition 0x569860)
    std::uint16_t sulfur;               // 0x4e
    std::uint16_t stones;               // 0x50
    std::uint16_t woodcutters;          // 0x52 buildings of the node per type
    std::uint16_t stonecutters;         // 0x54
    std::uint16_t sawmills;             // 0x56
    std::uint16_t foresters;            // 0x58
    std::uint16_t iron_smelters;        // 0x5a
    std::uint16_t toolsmiths;           // 0x5c
    std::uint16_t weaponsmiths;         // 0x5e
    std::uint16_t gold_smelters;        // 0x60
    std::uint16_t coal_mines;           // 0x62
    std::uint16_t iron_mines;           // 0x64
    std::uint16_t gold_mines;           // 0x66
    std::uint16_t sulfur_mines;         // 0x68 Asian SulfurMine, Amazon AmazonSulfurMine
    std::uint16_t gem_mines;            // 0x6a Egyptian, Amazon
    std::uint16_t fisheries;            // 0x6c
    std::uint16_t farms;                // 0x6e
    std::uint16_t waterworks;           // 0x70
    std::uint16_t mills;                // 0x72
    std::uint16_t bakeries;             // 0x74
    std::uint16_t pig_farms;            // 0x76
    std::uint16_t slaughterhouses;      // 0x78
    std::uint16_t vineyards;            // 0x7a Roman
    std::uint16_t breweries;            // 0x7c Egyptian
    std::uint16_t distilleries;         // 0x7e Asian
    std::uint16_t donkey_farms;         // 0x80
    std::uint16_t barracks;             // 0x82
    std::uint16_t rice_farms;           // 0x84 Asian
    std::uint16_t castles;              // 0x86
    std::uint16_t large_towers;         // 0x88
    std::uint16_t harbours;             // 0x8a
    std::uint16_t shipyards;            // 0x8c
    std::uint16_t marketplaces;         // 0x8e
    std::uint16_t hospitals;            // 0x90
    std::uint16_t war_workshops;        // 0x92 catapult / ballista / cannon workshop, Amazon large gong
    std::uint16_t powder_makers;        // 0x94 Asian
    std::uint16_t small_temples;        // 0x96 the race's small temple
    std::uint16_t large_temples;        // 0x98 the race's large temple
    std::uint16_t apiaries;             // 0x9a Amazon
    std::uint16_t meaderies;            // 0x9c Amazon
    std::uint16_t laboratories;         // 0x9e Amazon
};
static_assert(offsetof(EconomyAgent, race) == 0x08);
static_assert(offsetof(EconomyAgent, area) == 0x0c);
static_assert(offsetof(EconomyAgent, run_count) == 0x10);
static_assert(offsetof(EconomyAgent, stock_ratio) == 0x2c);
static_assert(offsetof(EconomyAgent, stage) == 0x34);
static_assert(offsetof(EconomyAgent, population) == 0x3e);
static_assert(offsetof(EconomyAgent, logs) == 0x42);
static_assert(offsetof(EconomyAgent, gold_ore) == 0x4c);
static_assert(offsetof(EconomyAgent, woodcutters) == 0x52);
static_assert(offsetof(EconomyAgent, coal_mines) == 0x62);
static_assert(offsetof(EconomyAgent, fisheries) == 0x6c);
static_assert(offsetof(EconomyAgent, donkey_farms) == 0x80);
static_assert(offsetof(EconomyAgent, hospitals) == 0x90);
static_assert(offsetof(EconomyAgent, laboratories) == 0x9e);
static_assert(sizeof(EconomyAgent) == 0xa0);

// ---------------------------------------------------------------- CompMapWorkerAgent globals
// Two global objects of Rainer\CompMapWorkerAgent.cpp (computer players
// A2, session 48; meanings mostly unknown, G: the computer players' unit
// groups and armies). Many arrays are per player and per unit group (the
// world's 10 settler lists per player, CGameData::player_lists), indexed
// player * 10 + group.
constexpr std::size_t kUnitGroups = 10;

struct AiGroupTarget {
    std::uint16_t x;   // +0 (probably; 0x590649 / 0x593180)
    std::uint16_t y;   // +2
    std::uint16_t unk4;  // +4 (0x590c80 tests it)
};
static_assert(sizeof(AiGroupTarget) == 6);

// g_aiGroupOrders (0x7e37d0), set up by 0x591e00.
struct AiGroupOrders {
    std::uint8_t unk000[0x320];
    std::uint8_t group_flags[layout::kPlayers][kUnitGroups];      // 0x320 (byte; 0x590c80 tests it)
    AiGroupTarget group_targets[layout::kPlayers][kUnitGroups];   // 0x3e8
};
static_assert(offsetof(AiGroupOrders, group_flags) == 0x320);
static_assert(offsetof(AiGroupOrders, group_targets) == 0x3e8);
static_assert(sizeof(AiGroupOrders) == 0x898);

// g_aiArmyState (0x7e58c8), reset by 0x593f50.
struct AiArmyState {
    std::uint32_t per_player000[layout::kPlayers];               // 0x000 25 * player after the reset
    std::uint32_t per_player050[layout::kPlayers];               // 0x050
    std::uint32_t per_player0a0[layout::kPlayers];               // 0x0a0
    std::uint32_t per_player0f0[layout::kPlayers];               // 0x0f0
    std::uint32_t per_player140[layout::kPlayers];               // 0x140
    std::uint32_t unk190;                                // 0x190
    std::uint32_t group_values[layout::kPlayers][kUnitGroups];   // 0x194 groups 1..9: 10 + 2 * group after the reset
    std::uint32_t per_player4b4[layout::kPlayers];               // 0x4b4 2000000 after the reset
    std::uint32_t unk504;                                // 0x504 300 after the reset
    std::uint32_t unk508;                                // 0x508 0
    std::uint32_t unk50c;                                // 0x50c 500
    std::uint32_t unk510[3];                             // 0x510 2000000
    std::uint8_t unk51c[0x630 - 0x51c];
    std::uint8_t use_player_population;                  // 0x630 the economy agent's population source (0x567910)
    std::uint8_t unk631[3];
    std::uint16_t group_words[layout::kPlayers][kUnitGroups];    // 0x634
    std::uint8_t group_bytes[layout::kPlayers][kUnitGroups];     // 0x7c4
};
static_assert(offsetof(AiArmyState, unk190) == 0x190);
static_assert(offsetof(AiArmyState, group_values) == 0x194);
static_assert(offsetof(AiArmyState, per_player4b4) == 0x4b4);
static_assert(offsetof(AiArmyState, unk504) == 0x504);
static_assert(offsetof(AiArmyState, unk510) == 0x510);
static_assert(offsetof(AiArmyState, use_player_population) == 0x630);
static_assert(offsetof(AiArmyState, group_words) == 0x634);
static_assert(offsetof(AiArmyState, group_bytes) == 0x7c4);

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
// A BLACK_BOARD_AGENT's start: AI_SetWord4 (0x55ca60) stores its kind at +4
// (the base class adds no fields; its vtable is stored by the factory
// 0x56f6d0 before the subclass constructor runs, e.g. 0x55ebe0 for
// T_ATTACK_AGENT, kind 7).
struct AiTaggedRecord {
    std::uint32_t unk00;  // +0
    std::uint16_t tag;    // +4
};
static_assert(offsetof(AiTaggedRecord, tag) == 4);

#pragma pack(pop)

}  // namespace s3re::game::ai
