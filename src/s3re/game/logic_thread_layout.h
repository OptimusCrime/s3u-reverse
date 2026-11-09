#pragma once

// Fields of the logic thread object (CLogicThread; see "Runtime structure"
// and "Commands and lockstep" in CLAUDE.md) that our code uses. Only these
// fields are named; the object is much larger (its size is not known here).

#include <cstddef>
#include <cstdint>

namespace s3re::game {

struct GameWorld;  // layout.h

// The ring buffer of dwords between the logic thread and the network thread
// (Push 0x4309c0, Pop 0x430980); its layout is not mapped (opaque).
struct MessageQueue;

#pragma pack(push, 1)

struct CLogicThread {
    std::uint8_t unk000[0x3a0];
    GameWorld* world;          // +0x3a0 the world (CGameData)
    std::uint8_t unk3a4[0x3c4 - 0x3a4];
    MessageQueue* toNet;       // +0x3c4 logic -> network (0x20 step and 0x21 command messages)
    MessageQueue* fromNet;     // +0x3c8 network -> logic (the replies)
};

#pragma pack(pop)

static_assert(sizeof(void*) == 4, "32-bit build only");
static_assert(offsetof(CLogicThread, world) == 0x3a0);
static_assert(offsetof(CLogicThread, toNet) == 0x3c4);
static_assert(offsetof(CLogicThread, fromNet) == 0x3c8);

}  // namespace s3re::game
