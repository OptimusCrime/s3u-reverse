// CNetThread: the lockstep command layer.
//
// Player commands (and those of computer players) are "announce" packets
// scheduled for an ExecuteStep. Every announced packet passes through
// CNetThread::OnNetAnnounce (0x0046cbd0), which queues it for execution and,
// with the game's `-record` option, appends it to Siedler3.evt.
//
// Announce packet layout (from OnNetAnnounce and recorded files):
//   +0  uint8  packet type (0x12 = announce)
//   +1  uint8  command id
//   +2  uint8  packet size in bytes (20 for every command seen so far)
//   +3  uint8  unknown (4 for command 0x01, else 0)
//   +4  uint8  net player id of the sender
//   +5  uint8  sequence number (per sender)
//   +6  uint32 ExecuteStep at which the command runs
//   +10 uint16 unknown (0)
//   +12 ...    command payload (size - 12 bytes)
//
// Status: pass-through hook that logs every announced packet when
// S3RE_LOG_COMMANDS is set (`s3re_launcher --log-commands`). Not
// reimplemented.

#include "game/globals.h"
#include "hooks.h"
#include "log.h"

#include <windows.h>

#include <intrin.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace s3re::game {

struct CNetThread;  // opaque

namespace {

using OnNetAnnounceFn = void(__fastcall*)(CNetThread* self, void* edx, const std::uint8_t* packet);

void __fastcall Hooked_CNetThread_OnNetAnnounce(CNetThread* self, void* edx,
                                                const std::uint8_t* packet);

S3RE_PASSTHROUGH(0x0046cbd0, CNetThread_OnNetAnnounce, OnNetAnnounceFn,
                 Original_CNetThread_OnNetAnnounce, Hooked_CNetThread_OnNetAnnounce);


// The announce packet's header (layout in the comment at the top); the
// command payload follows it.
#pragma pack(push, 1)
struct AnnounceHeader {
    std::uint8_t type;          // +0
    std::uint8_t command;       // +1
    std::uint8_t size;          // +2 whole packet, in bytes
    std::uint8_t unk03;         // +3
    std::uint8_t player;        // +4
    std::uint8_t sequence;      // +5
    std::uint32_t executeStep;  // +6
    std::uint16_t unk0a;        // +10
};
#pragma pack(pop)
static_assert(sizeof(AnnounceHeader) == 12);
static_assert(offsetof(AnnounceHeader, executeStep) == 6);

bool LogCommands() {
    static const bool value = GetEnvironmentVariableA("S3RE_LOG_COMMANDS", nullptr, 0) > 0;
    return value;
}

void __fastcall Hooked_CNetThread_OnNetAnnounce(CNetThread* self, void* edx,
                                                const std::uint8_t* packet) {
    if (LogCommands()) {
        const auto* header = reinterpret_cast<const AnnounceHeader*>(packet);
        const unsigned size = header->size;
        char payload[3 * 256 + 1] = {};
        for (unsigned i = sizeof(AnnounceHeader); i < size; ++i) {
            std::snprintf(payload + (i - sizeof(AnnounceHeader)) * 2, 3, "%02x", packet[i]);
        }
        const std::uint32_t step = header->executeStep;
        S3RE_LOG("announce tick=%u from=%p cmd=%02x size=%u b3=%u player=%u seq=%u step=%u payload=%s",
                 static_cast<std::uint32_t>(game::g_gameTick),
                 _ReturnAddress(), header->command, size, header->unk03, header->player,
                 header->sequence, step, payload);
    }
    Original_CNetThread_OnNetAnnounce(self, edx, packet);
}

}  // namespace

}  // namespace s3re::game
