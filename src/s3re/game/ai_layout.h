#pragma once

// Globals and object layouts shared by the computer-player helpers
// (Rainer\*.cpp; ai_helpers.cpp, ai_small_helpers.cpp, ai_evaland.cpp).
// Only the fields our replacements touch are named; the rest is padding
// (unkXX) whose meaning is unknown. Offsets are checked by static_asserts.

#include "game/layout.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game::ai {

// ---------------------------------------------------------------- globals
// The AI code reaches the game through globals set before each player's
// turn (AI_SetCurrentPlayer and 0x551b80).
constexpr std::uintptr_t kAiWorld = 0x007e3408;         // GameWorld*: the world, from computer player +0x75c
constexpr std::uintptr_t kAiPlayer = 0x007e3130;        // the CComputerPlayer whose turn it is
constexpr std::uintptr_t kAiPlayerNumber = 0x007e2eb8;  // uint8: its player number
constexpr std::uintptr_t kAiPlayerSetup = 0x007e2eb4;   // its entry of the settings table below
constexpr std::uintptr_t kAiPlayerData = 0x007e2eb0;    // AiPlayerData*: its working data
constexpr std::uintptr_t kPlayerSetupTable = 0x00731488;  // 0x180-byte entries
constexpr std::uint32_t kPlayerSetupBytes = 0x180;
constexpr std::uintptr_t kCandidateDefaults = 0x007329d0;  // uint16 per candidate index
constexpr std::uintptr_t kAiMapSizeY = 0x007e3412;  // uint16 (copied from the world by 0x551b80)
constexpr std::uintptr_t kAiMapSizeX = 0x007e3414;  // uint16
constexpr std::uintptr_t kAiMapWorld = 0x007e7b60;  // GameWorld*: a second copy of the AI's world pointer (0x58fa7b)
// The shared land evaluation object (AI_EvaLand_Release): reference count
// and instance (also cleared by AI_ClearGlobals7e0568).
constexpr std::uintptr_t kEvaLandRefCount = 0x007e0568;
constexpr std::uintptr_t kEvaLandInstance = 0x007e056c;
// 20 vectors (AiVector below), emptied by AI_ResetAgentData.
constexpr std::uintptr_t kAgentVectors = 0x007e7a24;
constexpr std::size_t kAgentVectorCount = 20;

template <typename T>
T& Global(std::uintptr_t address) {
    return *reinterpret_cast<T*>(address);
}

inline GameWorld* AiWorld() {
    return Global<GameWorld*>(kAiWorld);
}

#pragma pack(push, 1)

// ---------------------------------------------------------------- CComputerPlayer
// The fields AI_SetCurrentPlayer copies into the globals (size unknown).
struct ComputerPlayer {
    std::uint8_t unk000[0x272];
    std::uint8_t player_number;   // 0x272
    std::uint8_t unk273[0x75c - 0x273];
    std::uint32_t world;          // 0x75c GameWorld*
    std::uint8_t unk760[0x10];
    std::uint32_t setup_index;    // 0x770 index into kPlayerSetupTable
    std::uint32_t unk774;
    std::uint32_t data;           // 0x778 AiPlayerData*
};
static_assert(offsetof(ComputerPlayer, player_number) == 0x272);
static_assert(offsetof(ComputerPlayer, world) == 0x75c);
static_assert(offsetof(ComputerPlayer, setup_index) == 0x770);
static_assert(offsetof(ComputerPlayer, data) == 0x778);

// ---------------------------------------------------------------- AI player data
// g_aiPlayerData: the best place found per candidate index (probably per
// building type, AI_RecordBestCandidate) and a per-index value reset to
// its default from kCandidateDefaults. Indexes are not range-checked by the
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
    AiCandidate best[114];                // 0x026
    std::uint16_t candidate_values[1];    // 0x2d2 (count unknown)
};
static_assert(offsetof(AiPlayerData, best) == 0x26);
static_assert(offsetof(AiPlayerData, candidate_values) == 0x2d2);

// ---------------------------------------------------------------- agent data
// The working data AI_ResetAgentData clears (size at least 0x1ad8; probably
// a computer player's statistics and orders, meanings unknown).
struct AiAgentData {
    std::uint8_t unk000;                 // 0x000
    std::uint8_t unk001;                 // 0x001
    std::uint8_t unk002;                 // 0x002
    std::uint8_t unk003;
    std::uint16_t words004[20][10];      // 0x004
    std::uint8_t bytes194[20][10];       // 0x194
    std::uint32_t dwords25c[20];         // 0x25c
    std::uint8_t unk2ac;                 // 0x2ac
    std::uint8_t unk2ad[7];
    std::uint32_t dwords2b4[20];         // 0x2b4
    std::uint32_t dwords304[20];         // 0x304
    std::uint32_t dwords354[20];         // 0x354
    std::uint32_t dwords3a4[20];         // 0x3a4
    std::uint32_t dwords3f4[20];         // 0x3f4
    std::uint32_t dwords444[0xb4];       // 0x444
    std::uint32_t dwords714[0x474];      // 0x714
    std::uint32_t dwords18e4[0x7d];      // 0x18e4..0x1ad4
};
static_assert(offsetof(AiAgentData, words004) == 0x4);
static_assert(offsetof(AiAgentData, bytes194) == 0x194);
static_assert(offsetof(AiAgentData, dwords25c) == 0x25c);
static_assert(offsetof(AiAgentData, unk2ac) == 0x2ac);
static_assert(offsetof(AiAgentData, dwords2b4) == 0x2b4);
static_assert(offsetof(AiAgentData, dwords3f4) == 0x3f4);
static_assert(offsetof(AiAgentData, dwords444) == 0x444);
static_assert(offsetof(AiAgentData, dwords714) == 0x714);
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

// A vector of 16-byte entries (the 20 at kAgentVectors).
struct AiVector {
    std::uint32_t begin;  // +0
    std::uint32_t end;    // +4 (end = begin empties it)
    std::uint32_t unk8;
    std::uint32_t unkc;
};
static_assert(sizeof(AiVector) == 0x10);

inline AiVector* AgentVectors() {
    return reinterpret_cast<AiVector*>(kAgentVectors);
}

#pragma pack(pop)

}  // namespace s3re::game::ai
