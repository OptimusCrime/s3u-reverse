#pragma once

// Globals and object layouts shared by the computer-player helpers
// (Rainer\*.cpp; ai_helpers.cpp, ai_small_helpers.cpp, ai_evaland.cpp).
// Only the fields our replacements touch are named; the rest is padding
// (unkXX) whose meaning is unknown. Offsets are checked by static_asserts.

#include "game/enums/buildings.h"
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
constexpr std::uint16_t kNoteTaken = 0x0002;       // set by the agent that takes it, with BlackBoardNote::handler (slot 3: 0x5654d0, 0x5787c0)
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
    std::uint16_t requester;      // 0x16 kind of the agent that made the note (0x551270), or kNoAgent; 0x582480 reports to its slot 9
    std::uint16_t handler;        // 0x18 kind of the agent that took the note (its slot 3); 0x582510 calls its slot 10
};
static_assert(offsetof(BlackBoardNote, flags) == 0x04);
static_assert(offsetof(BlackBoardNote, id) == 0x08);
static_assert(offsetof(BlackBoardNote, parent) == 0x10);
static_assert(offsetof(BlackBoardNote, first_child) == 0x14);
static_assert(offsetof(BlackBoardNote, requester) == 0x16);
static_assert(offsetof(BlackBoardNote, handler) == 0x18);
static_assert(sizeof(BlackBoardNote) == 0x1a);

// ---------------------------------------------------------------- BLACK_BOARD_AGENT
// An agent as the CComputerPlayer sees it: a vtable and its kind (+4, set
// by AI_SetWord4; the index in ComputerPlayer::agents). The virtual slots
// the CComputerPlayer calls (package A3, ai_computer_player.cpp; all
// thiscall, callee pops) and what they are for (R from the callers, G where
// marked):
namespace agent_slot {
constexpr std::size_t kDeletingDestructor = 0;  // (flags) also slot 0 of the notes
constexpr std::size_t kNoteTypes = 2;     // (u16* types) writes the note types it handles; AX = their count (0x551400; 0x576500 writes 1 type)
constexpr std::size_t kTakeNote = 3;      // (note) a new note of a type it handles (0x551510)
constexpr std::size_t kWorkOnNote = 4;    // (note) an active note of its type, when not busy (0x5516e0)
constexpr std::size_t kBusy = 5;          // () non-zero: kWorkOnNote is skipped this time
constexpr std::size_t kWantsToRun = 7;    // () non-zero: kRun is called (0x551590)
constexpr std::size_t kRun = 8;           // () the agent's own work
constexpr std::size_t kNoteReported = 9;  // (parent note, note, result, value) a note made by it reports back (0x582480)
constexpr std::size_t kExecute = 10;      // (note) carry out a note it took (0x582510; was kCancelNote)
// kExecute: T_DO_BUILD_HOUSE_AGENT::WorkOnNote 0x5655c0 calls its own slot 10
// once the house request reported success; the chain through the request
// agents' slot 10 ends in the PlaceBuilding command sender 0x576430.
}  // namespace agent_slot
// The agent kinds (ComputerPlayer::agents index, word +4), from the
// factory AI::CreateAgent 0x56f6d0 (jump table 0x56fa68; each constructor
// passes its kind to AI_SetWord4).
enum class AgentKind : std::uint16_t {
    LocalWareRequest = 0,     // LOCAL_WARE_REQUEST_AGENT, 0x57ab50
    HouseGroundRequest = 1,   // HOUSE_GROUND_REQUEST_AGENT, 0x5764f0
    HouseRequest = 2,         // HOUSE_REQUEST_AGENT, 0x5787a0
    GlobalExpandEmpire = 3,   // GLOBAL_EXPAND_EMPIRE_AGENT, 0x56cd00
    DoBuildHouse = 4,         // T_DO_BUILD_HOUSE_AGENT, 0x5654b0
    ComputerPlayer = 5,       // COMPUTER_PLAYER_AGENT, 0x584680
    MapWorker = 6,            // T_MAP_WORKER_AGENT, 0x57cda0
    Attack = 7,               // T_ATTACK_AGENT, 0x55ebe0
    Economy = 8,              // T_ECONOMY_AGENT, 0x567850
    Safety = 9,               // T_SAFETY_AGENT, 0x586830
    CentrumCheck = 10,        // CCentrumCheckAgent, 0x560dd0
};

// The note types (BlackBoardNote::type): each is handled by the request
// agent of the same number (handlers[type] = kind; 0x576500, the
// HOUSE_GROUND_REQUEST_AGENT's slot 2, lists type 1).
enum class NoteType : std::uint16_t {
    LocalWareRequest = 0,    // LOCAL_WARE_REQUEST_NOTE (init 0x57ab20)
    HouseGroundRequest = 1,  // HOUSE_GROUND_REQUEST_NOTE (G: its init is not found yet)
    HouseRequest = 2,        // HOUSE_REQUEST_NOTE (init 0x578760)
    DoBuildHouse = 4,        // T_DO_BUILD_HOUSE_NOTE (init 0x565470)
};

constexpr std::uint16_t kAgentDone = 100;  // AX of kTakeNote / kWorkOnNote that ends the handler loop (one handler per type anyway)
constexpr std::uint16_t kNoAgent = 0xffff;

struct BlackBoardAgent {
    const void* const* vtable;  // 0x00
    std::uint16_t kind;         // 0x04
};
static_assert(offsetof(BlackBoardAgent, kind) == 4);

// The request notes' own fields, from their init functions (0x57ab20,
// 0x578760, 0x565470); meanings G: a T_DO_BUILD_HOUSE_NOTE is made with
// (building type, x, y, value) (0x56d510: type 15, small tower, value 32),
// its HOUSE_REQUEST_NOTE gets the same four (0x56551c), a
// LOCAL_WARE_REQUEST_NOTE the first three (0x5788aa).
struct RequestNote {
    BlackBoardNote base;          // 0x00
    std::uint16_t unk1a;
    std::uint16_t building_type;  // 0x1c (G)
    std::uint16_t x;              // 0x1e (G)
    std::uint16_t y;              // 0x20 (G)
    std::uint16_t value22;        // 0x22 not in LOCAL_WARE_REQUEST_NOTE's init
    std::uint16_t unk24;          // 0x24 0 after the init (not LOCAL_WARE_REQUEST_NOTE's)
    std::uint16_t unk26;          // 0x26 the same
};
static_assert(offsetof(RequestNote, building_type) == 0x1c);
static_assert(offsetof(RequestNote, value22) == 0x22);
static_assert(offsetof(RequestNote, unk26) == 0x26);

// ---------------------------------------------------------------- CComputerPlayer
// One per computer player (20 pointers at CLogicThread+0x350; constructor
// 0x550900, per-tick entry 0x550f80). Size at least 0x780. The four id
// lists are AI_WordListInsert/Remove lists (0xffff = free, packed at the
// front) with a count of the entries added.
constexpr std::size_t kRootNoteSlots = 100;
constexpr std::size_t kPendingNoteSlots = 50;
constexpr std::size_t kActiveNoteSlots = 100;
constexpr std::size_t kDoomedNoteSlots = 50;
constexpr std::size_t kAgentKinds = 11;

struct ComputerPlayer {
    std::uint32_t vtable;                              // 0x000 0x65a530
    std::uint16_t root_notes[kRootNoteSlots];          // 0x004 notes without a parent
    std::uint16_t root_note_count;                     // 0x0cc
    std::uint16_t pending_notes[kPendingNoteSlots];    // 0x0ce new notes, handed to their handler by 0x5514c0
    std::uint16_t pending_note_count;                  // 0x132
    std::uint16_t active_notes[kActiveNoteSlots];      // 0x134 notes with kNoteActive
    std::uint16_t active_note_count;                   // 0x1fc
    std::uint16_t active_cursor;                       // 0x1fe next index into active_notes (0x551670, round robin)
    std::uint16_t doomed_notes[kDoomedNoteSlots];      // 0x200 notes to delete after the next active note's turn (CComputerPlayer::DoomNote 0x551910; 0x551790)
    std::uint16_t doomed_note_count;                   // 0x264
    std::uint16_t handlers[6];                         // 0x266 per note type: the kind of the agent that handles it, kNoAgent = none (0x551400, 0x551510)
    std::uint8_t player_number;                        // 0x272
    std::uint8_t unk273;
    std::uint16_t timer274;                            // 0x274 the three step counters of 0x550f80
    std::uint16_t unk276;
    std::uint16_t timer278;                            // 0x278
    std::uint16_t unk27a;
    std::uint16_t timer27c;                            // 0x27c
    std::uint16_t unk27e;
    BlackBoardNote* notes[kMaxNotes];                  // 0x280 by id
    BlackBoardAgent* agents[kAgentKinds];              // 0x730 by kind (0x551400 adds, 0x551490 deletes); 0x550f80 runs slot 8 of [7] (probably T_ATTACK_AGENT) when the player has no nodes
    std::uint32_t world;                               // 0x75c CGameData*
    std::uint32_t message_queue;                       // 0x760 the logic->net MessageQueue (commands are pushed there)
    std::uint32_t steps;                               // 0x764 +1 per 0x550f80
    std::uint8_t unk768;
    std::uint8_t pad769[3];
    std::uint32_t player;                              // 0x76c the player index (constructor argument)
    std::uint32_t setup_index;                         // 0x770 index into g_aiPlayerSetups
    std::uint32_t land_evaluation;                     // 0x774 AiLandEvaluation* (CEvaLand::Acquire 0x553d30, stored by CComputerPlayer::Init 0x550a30; ECX of 0x5547a0 / 0x554980)
    std::uint32_t data;                                // 0x778 AiPlayerData* (the current node of that list)
    std::uint32_t area;                                // 0x77c the current node's economy area (node word +8; a tile area id, 0x551a70)
};
static_assert(offsetof(ComputerPlayer, root_notes) == 0x004);
static_assert(offsetof(ComputerPlayer, root_note_count) == 0x0cc);
static_assert(offsetof(ComputerPlayer, pending_notes) == 0x0ce);
static_assert(offsetof(ComputerPlayer, pending_note_count) == 0x132);
static_assert(offsetof(ComputerPlayer, active_notes) == 0x134);
static_assert(offsetof(ComputerPlayer, active_note_count) == 0x1fc);
static_assert(offsetof(ComputerPlayer, active_cursor) == 0x1fe);
static_assert(offsetof(ComputerPlayer, doomed_notes) == 0x200);
static_assert(offsetof(ComputerPlayer, doomed_note_count) == 0x264);
static_assert(offsetof(ComputerPlayer, handlers) == 0x266);
static_assert(offsetof(ComputerPlayer, player_number) == 0x272);
static_assert(offsetof(ComputerPlayer, timer27c) == 0x27c);
static_assert(offsetof(ComputerPlayer, notes) == 0x280);
static_assert(offsetof(ComputerPlayer, agents) == 0x730);
static_assert(offsetof(ComputerPlayer, world) == 0x75c);
static_assert(offsetof(ComputerPlayer, player) == 0x76c);
static_assert(offsetof(ComputerPlayer, setup_index) == 0x770);
static_assert(offsetof(ComputerPlayer, land_evaluation) == 0x774);
static_assert(offsetof(ComputerPlayer, data) == 0x778);
static_assert(sizeof(ComputerPlayer) == 0x780);

// ---------------------------------------------------------------- AI player data
// g_aiPlayerData: the best place found per candidate index (probably per
// building type, AI_RecordBestCandidate) and a per-index value reset to
// its default from g_aiCandidateDefaults. Indexes are not range-checked by the
// original (callers pass up to 0xffff), so the arrays are indexed past their
// declared ends like the original's address arithmetic.
// An AiPlayerData is a node of the player's std::list in CEvaLand::lists
// (one per economy area of the player, created by CEvaLand::AddArea
// 0x555260): +0 next, +4 previous, the value (the area record, starting
// with the area id word at +8) after it. Its offsets below are node offsets.
struct AiCandidate {
    std::uint16_t score;  // +0
    std::uint16_t a;      // +2 (probably x)
    std::uint16_t b;      // +4 (probably y)
};
static_assert(sizeof(AiCandidate) == 6);

struct AiPlayerData {
    std::uint32_t next;                   // 0x000 list node: the next node (the list head is the sentinel)
    std::uint32_t prev;                   // 0x004 the previous node
    std::uint16_t area;                   // 0x008 the node's key: the economy area (AI::FindNode 0x573be0, 0x572420)
    std::uint16_t unk00a;
    std::uint32_t basic_economy;          // 0x00c 1 once a sawmill, woodcutter, small tower and the race's basics (stonecutter, forester) stand (AI::CheckBasicEconomy 0x5720c0)
    // G: the economy lacks wood / stone; 0x551a70 then only starts a
    // woodcutter, sawmill, forester (wood) / a stonecutter (stone) or a
    // small tower.
    std::uint32_t short_of_wood;          // 0x010
    std::uint32_t short_of_stone;         // 0x014
    std::uint32_t force_production;       // 0x018 set by COMP_ForceProduction 0x572fc0, read by AI::IsForceProduction 0x572fa0
    std::uint8_t unk01c[0x0a];
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
static_assert(offsetof(AiPlayerData, area) == 0x8);
static_assert(offsetof(AiPlayerData, basic_economy) == 0xc);
static_assert(offsetof(AiPlayerData, force_production) == 0x18);
static_assert(offsetof(AiPlayerData, short_of_wood) == 0x10);
static_assert(offsetof(AiPlayerData, short_of_stone) == 0x14);
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
    std::uint32_t last_priority;        // 0x30 the last priority of the forester, woodcutter, stonecutter, coal/iron mine and iron smelter conditions (was forester_priority)
    std::uint16_t stage;                // 0x34 population stage 0..3 (g_aiPopulationStages)
    std::uint8_t unk36[8];
    std::uint16_t population;           // 0x3e settlers of class 0 in the area (CGameData::CountAreaSettlersOfClass0), or a per-player global
    std::uint16_t carriers;             // 0x40 settlers of type 0 in the area (0x59dc70)
    std::uint16_t logs;                 // 0x42 goods in the area
    std::uint16_t boards;               // 0x44
    std::uint16_t coal;                 // 0x46
    std::uint16_t iron;                 // 0x48
    std::uint16_t ore;                  // 0x4a iron (0x568d80) or iron ore (0x568f40) in the area, stored by those conditions
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

struct AiGroupPosition {
    std::int16_t x;  // +0
    std::int16_t y;  // +2
};
static_assert(sizeof(AiGroupPosition) == 4);

// g_aiGroupOrders (0x7e37d0), the military AI's "army" object (the thiscall
// `this` of 0x591c40..0x5953b0), set up by 0x591e00; 0x20f8 bytes (savegame
// chunk 0x15, so it ends at g_aiArmyState). Fields from
// re/computer_players_naming.md ("Army object 0x7e37d0").
struct AiGroupOrders {
    std::uint16_t home_areas[layout::kPlayers][kUnitGroups];     // 0x000 area of the own military building nearest to the group (0x592820)
    std::uint16_t unk190[layout::kPlayers][kUnitGroups];         // 0x190 cleared per group by the reset (meaning open)
    std::uint8_t group_flags[layout::kPlayers][kUnitGroups];     // 0x320 steal flag: the group goes for a weakly guarded enemy building (0x594080; 0x590c80 tests it)
    AiGroupTarget group_targets[layout::kPlayers][kUnitGroups];  // 0x3e8
    std::uint32_t gather_buildings[layout::kPlayers];            // 0x898 the gather building of a pending attack (0x594080)
    std::uint32_t per_player8e8[layout::kPlayers];               // 0x8e8
    std::uint8_t unk938[0x988 - 0x938];
    std::int32_t group_mages[layout::kPlayers][kUnitGroups];     // 0x988 mages per group (0x591c40, 0x5926e0)
    std::uint8_t settlers_with_orders[32000 / 8];                // 0xca8 std::bitset<32000>: group members, hunters, busy geologists
    std::uint8_t group_flags1c48[layout::kPlayers][kUnitGroups];  // 0x1c48 per group, cleared by the reset 0x591e00 (meaning open)
    std::int32_t highest_group[layout::kPlayers];                // 0x1d10 highest group in use
    std::int32_t group_size[layout::kPlayers];                   // 0x1d60 7 after the reset, up to 100
    AiGroupPosition gather_positions[layout::kPlayers][kUnitGroups];  // 0x1db0
    AiGroupPosition gather_offsets[kUnitGroups];                 // 0x20d0 added to the gather position
};
static_assert(offsetof(AiGroupOrders, group_flags) == 0x320);
static_assert(offsetof(AiGroupOrders, group_targets) == 0x3e8);
static_assert(offsetof(AiGroupOrders, gather_buildings) == 0x898);
static_assert(offsetof(AiGroupOrders, group_mages) == 0x988);
static_assert(offsetof(AiGroupOrders, settlers_with_orders) == 0xca8);
static_assert(offsetof(AiGroupOrders, highest_group) == 0x1d10);
static_assert(offsetof(AiGroupOrders, gather_positions) == 0x1db0);
static_assert(offsetof(AiGroupOrders, gather_offsets) == 0x20d0);
static_assert(sizeof(AiGroupOrders) == 0x20f8);

// g_aiArmyState (0x7e58c8), reset by 0x593f50; 0x51c bytes (savegame chunk
// 0x17). The per-group target building and defending flag once placed
// behind it (+0x634, +0x7c4) belong to the census g_aiCensus (AiAgentData).
struct AiArmyState {
    std::uint32_t per_player000[layout::kPlayers];               // 0x000 25 * player after the reset
    std::uint32_t per_player050[layout::kPlayers];               // 0x050
    std::uint32_t per_player0a0[layout::kPlayers];               // 0x0a0
    std::uint32_t per_player0f0[layout::kPlayers];               // 0x0f0
    std::uint32_t per_player140[layout::kPlayers];               // 0x140
    std::uint32_t unk190;                                // 0x190
    std::uint32_t attack_delays[layout::kPlayers][kUnitGroups];  // 0x194 countdowns; 0x594000 resets the group when one runs out (groups 1..9: 10 + 2 * group after the reset)
    std::uint32_t per_player4b4[layout::kPlayers];               // 0x4b4 2000000 after the reset
    std::uint32_t unk504;                                // 0x504 300 after the reset
    std::uint32_t unk508;                                // 0x508 0
    std::uint32_t unk50c;                                // 0x50c 500
    std::uint32_t unk510[3];                             // 0x510 2000000
};
static_assert(offsetof(AiArmyState, unk190) == 0x190);
static_assert(offsetof(AiArmyState, attack_delays) == 0x194);
static_assert(offsetof(AiArmyState, per_player4b4) == 0x4b4);
static_assert(offsetof(AiArmyState, unk504) == 0x504);
static_assert(offsetof(AiArmyState, unk510) == 0x510);
static_assert(sizeof(AiArmyState) == 0x51c);

// ---------------------------------------------------------------- agent data
// The computer players' census of the world (AI_ResetAgentData clears it,
// constructor 0x55b6b0; size 0x1ad8): buildings per player and type
// (0x55be00), soldiers per player (filled elsewhere, not yet known) and
// the military strength of each player's team (0x55bea0). Other fields'
// meanings unknown.
constexpr std::size_t kCensusSoldierKinds = 9;  // probably 3 weapons x 3 levels (every third weighs double)

struct AiAgentData {
    std::uint8_t settlers_counted;       // 0x000 0 while CountSettlers 0x55b880 counts, then 1 (the economy agent 0x567910 then takes the per-player figure)
    std::uint8_t buildings_counted;      // 0x001 0 while 0x55be00 counts, then 1
    std::uint8_t unk002;                 // 0x002 set to 1 by 0x55be00
    std::uint8_t unk003;
    std::uint16_t group_target_buildings[20][10];  // 0x004 per player and unit group the target building, 0 = none (0x594080, 0x594a10)
    std::uint8_t group_defending[20][10];          // 0x194 per player and unit group: the group defends (skipped as attacker, 0x594a10)
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
static_assert(offsetof(AiAgentData, group_target_buildings) == 0x4);
static_assert(offsetof(AiAgentData, group_defending) == 0x194);
static_assert(offsetof(AiAgentData, team_strength) == 0x25c);
static_assert(offsetof(AiAgentData, team_strength_valid) == 0x2ac);
static_assert(offsetof(AiAgentData, dwords2b4) == 0x2b4);
static_assert(offsetof(AiAgentData, dwords3f4) == 0x3f4);
static_assert(offsetof(AiAgentData, soldiers) == 0x444);
static_assert(offsetof(AiAgentData, buildings) == 0x714);
static_assert(offsetof(AiAgentData, dwords18e4) == 0x18e4);
static_assert(sizeof(AiAgentData) == 0x1ad8);

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
// +0x00..+0x0e are written by AI::CEvaLand::EvaluateCell 0x5540f0 from the
// cell's tiles; +0x0f..+0x20 by GridSumAround 0x554530 from the 3x3 cells
// around (re/computer_players_naming.md "Land evaluation grid cell").
// Amounts are halved and capped at 250 (EvaluateCell).
struct AiGridCell {
    std::uint16_t flags;                // 0x00 kGridCellHasLand / NoWater / HasWater / NoLand
    std::uint8_t stream_tiles;          // 0x02 tiles of terrain class 0x60 (stream: water for waterworks)
    std::uint8_t stones;                // 0x03 stone amount / 2, summed into stones_around
    std::uint8_t trees;                 // 0x04 tree tiles, summed into trees_around
    std::uint8_t resources[6];          // 0x05..0x0a iron, coal, gold, sulfur, gems, fish (amount / 2)
    std::uint8_t grain_land_percent;    // 0x0b % of grass tiles with TileSlope < 7 (incl. trees and stones)
    std::uint8_t wine_land_percent;     // 0x0c % of grass tiles with slope >= 7
    std::uint8_t rice_land_percent;     // 0x0d % of free swampy-grass (0x15) tiles with slope < 7
    std::uint8_t grass_percent;         // 0x0e % of grass-class (0x10) tiles
    std::uint8_t trees_around;          // 0x0f
    std::uint16_t stones_around;        // 0x10
    std::uint16_t resources_around[6];  // 0x12..0x1c
    std::uint8_t grain_land_around;     // 0x1e sum of grain_land_percent * 100 / 576
    std::uint8_t wine_land_around;      // 0x1f the same for wine_land_percent
    std::uint8_t rice_land_around;      // 0x20 the same for rice_land_percent
    std::uint8_t unk21[0xc4 - 0x21];    // +0x24: per player a bitset<57> of building types (naming notes)
};
static_assert(sizeof(AiGridCell) == 0xc4);
static_assert(offsetof(AiGridCell, stream_tiles) == 0x02);
static_assert(offsetof(AiGridCell, resources) == 0x05);
static_assert(offsetof(AiGridCell, grass_percent) == 0x0e);
static_assert(offsetof(AiGridCell, trees_around) == 0x0f);
static_assert(offsetof(AiGridCell, resources_around) == 0x12);
static_assert(offsetof(AiGridCell, grain_land_around) == 0x1e);

// AiGridCell::flags (EvaluateCell): starts as NoWater | NoLand; a land tile
// (terrain class not 0) sets HasLand and clears NoLand, a water tile
// (class 0) sets HasWater and clears NoWater.
constexpr std::uint16_t kGridCellHasLand = 0x1;
constexpr std::uint16_t kGridCellNoWater = 0x2;
constexpr std::uint16_t kGridCellHasWater = 0x4;
constexpr std::uint16_t kGridCellNoLand = 0x8;

// AiGridCell::resources indexes.
enum GridResource : std::uint8_t { kGridIron, kGridCoal, kGridGold, kGridSulfur, kGridGems, kGridFish };

// CEvaLand (our name, from Rainer\Cevaland.cpp; 0x15c bytes): the computer
// players' shared land evaluation object.
struct AiLandEvaluation {
    AiList lists[20];          // 0x000
    std::uint32_t areas_changed[20];  // 0x0f0 per player: set by RemoveArea 0x558e80, read by CComputerPlayer::Step
    std::uint32_t width;           // 0x140 grid width (cells)
    std::uint32_t height;          // 0x144 grid height
    std::uint32_t cell_count;      // 0x148 width * height (Init 0x553ea0)
    std::uint32_t tiles_per_cell;  // 0x14c 64 (Init); EvaluateCell's percentages divide by it
    std::uint32_t cursor_x;        // 0x150 UpdateNextCells' position on the grid
    std::uint32_t cursor_y;        // 0x154
    AiGridCell* cells;         // 0x158 the grid
};
static_assert(offsetof(AiLandEvaluation, areas_changed) == 0x0f0);
static_assert(offsetof(AiLandEvaluation, width) == 0x140);
static_assert(offsetof(AiLandEvaluation, tiles_per_cell) == 0x14c);
static_assert(offsetof(AiLandEvaluation, cursor_x) == 0x150);
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

// ---------------------------------------------------------------- command sender
// g_aiCommandSender (0x7e5de8; our name, no RTTI): the thiscall `this` of
// the military AI's command senders 0x590990..0x590c80 (ai_commands.cpp).
// They build each command in `message` and push it from there, so bytes a
// sender doesn't write keep the previous command's values (deterministic).
struct AiCommandSender {
    std::uint8_t battle_spells[4][3];  // 0x00 per race the spell of battle-spell slot k (0x590870 fills it)
    std::uint8_t message[12];          // 0x0c the command being sent (0x21, id, size, ...)
};
static_assert(offsetof(AiCommandSender, message) == 0x0c);
static_assert(sizeof(AiCommandSender) == 0x18);

// ---------------------------------------------------------------- wrapping position
// AI::WrappedPos (our name; 0x597070..0x597300, ai_wrapped_pos.cpp): a
// position on a width x height grid that wraps at the edges, the cursor of
// the computer players' searches around a tile.
struct WrappedPos {
    std::int16_t x;        // +0x0
    std::int16_t y;        // +0x2
    std::uint16_t width;   // +0x4
    std::uint16_t height;  // +0x6
    std::uint32_t unk08;   // +0x8 cleared by SetPosition
    std::uint32_t unk0c;   // +0xc cleared by SetPosition
};
static_assert(sizeof(WrappedPos) == 16);

// HOUSE_GROUND_REQUEST_NOTE (0x4c bytes, vtable 0x65a598): the request for
// ground for a building; its agent searches a spiral around (x, y)
// (NextSpiralPosition 0x5763a0, CheckSite 0x5760d0). Set up by Init
// 0x575d60.
struct HouseGroundRequestNote {
    RequestNote request;          // 0x00 (+0x22..+0x26 = 0xffff after Init)
    std::uint16_t unk28;          // 0x28 0 after Init
    std::uint16_t unk2a;
    WrappedPos spiral;            // 0x2c the search position: map-sized, starting at (x, y)
    std::uint16_t unk3c;          // 0x3c 0 after Init
    std::uint16_t unk3e[2];
    std::uint16_t max_radius;     // 0x42 Init's fourth argument
    std::uint32_t unk44;          // 0x44 0 after Init
    std::uint32_t military;       // 0x48 1 if the building needs a military site (AI_SiteKindOf == 3)
};
static_assert(offsetof(HouseGroundRequestNote, spiral) == 0x2c);
static_assert(offsetof(HouseGroundRequestNote, unk3c) == 0x3c);
static_assert(offsetof(HouseGroundRequestNote, max_radius) == 0x42);
static_assert(offsetof(HouseGroundRequestNote, military) == 0x48);
static_assert(sizeof(HouseGroundRequestNote) == 0x4c);

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
