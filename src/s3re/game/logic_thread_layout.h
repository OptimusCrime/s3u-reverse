#pragma once

// Fields of the logic thread object (CLogicThread; see "Runtime structure"
// and "Commands and lockstep" in CLAUDE.md) that our code uses. Only these
// fields are named; the object is much larger (its size is not known here).

#include <cstddef>
#include <cstdint>

namespace s3re::game {

struct CGameData;  // layout.h

// The ring buffer of dwords between the logic thread and the network thread
// (Push 0x4309c0, Pop 0x430980); its layout is not mapped (opaque).
struct MessageQueue;

#pragma pack(push, 1)

struct CLogicThread {
    std::uint8_t unk000[0x74];
    std::uint32_t unk074;              // +0x074 cleared by ClearPendingUiRefresh (ui_state.cpp)
    std::uint8_t unk078[0xc];
    std::uint32_t pending_ui_refresh;  // +0x084 turned into the world's refresh_ui (ui_state.cpp)
    std::uint8_t unk088[0x24c - 0x88];
    std::uint32_t last_game_end_check;  // +0x24c game tick of the last CheckGameEnd evaluation (every 50 ticks)
    std::uint8_t unk250[0x3a0 - 0x250];
    CGameData* world;          // +0x3a0 the world (CGameData)
    std::uint8_t unk3a4[0x3c4 - 0x3a4];
    MessageQueue* toNet;       // +0x3c4 logic -> network (0x20 step and 0x21 command messages)
    MessageQueue* fromNet;     // +0x3c8 network -> logic (the replies)
};

#pragma pack(pop)

static_assert(sizeof(void*) == 4, "32-bit build only");
static_assert(offsetof(CLogicThread, unk074) == 0x74);
static_assert(offsetof(CLogicThread, pending_ui_refresh) == 0x84);
static_assert(offsetof(CLogicThread, last_game_end_check) == 0x24c);
static_assert(offsetof(CLogicThread, world) == 0x3a0);
static_assert(offsetof(CLogicThread, toNet) == 0x3c4);
static_assert(offsetof(CLogicThread, fromNet) == 0x3c8);

// The logic thread fields of a CLogicThread pointer or of a test buffer.
template <typename T>
CLogicThread* LogicThreadOf(T* thread) {
    return reinterpret_cast<CLogicThread*>(thread);
}

}  // namespace s3re::game
