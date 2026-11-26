#pragma once

// Fields of the logic thread object (CLogicThread; see "Runtime structure"
// and "Commands and lockstep" in CLAUDE.md) that our code uses. Only these
// fields are named; the object is much larger (its size is not known here).

#include <cstddef>
#include <cstdint>

namespace s3re::game {

struct CGameData;  // layout.h

#pragma pack(push, 1)

// The ring buffer of dwords between the logic thread and the network thread
// (Push 0x4309c0, Pop 0x430980). Only the fields those two functions and
// ExecuteCommands use; the object is a Win32 waitable (vtable slot 3 waits
// for a message).
struct MessageQueue {
    void* const* vtable;       // +0x00 slot 3 (+0x0c): Wait(timeout ms, flag), throws Win32::CTimeout* when nothing came
    std::uint8_t unk04[0x10];
    std::uint32_t* buffer;     // +0x14 the ring of dwords
    std::uint32_t capacity;    // +0x18 a power of two (indexes wrap with capacity - 1)
    std::uint32_t read;        // +0x1c index of the next dword Pop returns
    std::uint32_t write;       // +0x20 index of the next dword Push writes
    std::int32_t count;        // +0x24 dwords in the ring
    void* event;               // +0x28 an object whose +8 is the event HANDLE Push signals (SetEvent)
    void* lock;                // +0x2c the CRITICAL_SECTION of both ends
    std::int32_t peak;         // +0x30 largest count so far (Push)
};

// One message line of the logic thread's message ring (0x1c bytes; re/logic_thread.md).
struct MessageLine {
    std::uint8_t text[0x10];   // +0x00 an MSVC 6 std::wstring
    std::int32_t lifetime;     // +0x10 ticks left (AgeMessages counts it down to 0)
    std::int32_t kind;         // +0x14
    std::uint8_t dirty;        // +0x18 1 = redraw the line
    std::uint8_t pad19[3];
};
static_assert(sizeof(MessageLine) == 0x1c);
static_assert(offsetof(MessageLine, lifetime) == 0x10);
static_assert(offsetof(MessageLine, dirty) == 0x18);

struct CLogicThread {
    std::uint8_t unk000[0x4c];
    std::uint32_t group_selection_changed;  // +0x04c set to 1 by ExecuteCommands 0x10 when the local player's carriers became units (new group 0)
    std::uint8_t unk050[0x20];
    std::uint32_t tick;                // +0x070 the game tick (copied to g_gameTick at the end of each Step); SaveGame passes it to GameSettings::SaveSetup
    std::uint32_t unk074;              // +0x074 cleared by ClearPendingUiRefresh (ui_state.cpp)
    std::uint8_t unk078[0xc];
    std::uint32_t pending_ui_refresh;  // +0x084 turned into the world's refresh_ui (ui_state.cpp)
    std::uint8_t unk088[0x224 - 0x88];
    std::uint32_t save_request_tick;    // +0x224 ExecuteCommands 0xf7/0xfa: game tick of the last multiplayer save request (0 = none)
    std::uint32_t save_failures;        // +0x228 ExecuteCommands 0xf7/0xf9 (multiplayer save)
    std::uint32_t save_acks;            // +0x22c ExecuteCommands 0xf7/0xf8: players that saved
    std::uint8_t unk230[4];
    std::uint8_t game_finished;         // +0x234 set by FinishGame (it runs once)
    std::uint8_t player_flags[20];      // +0x235 per player, cleared by ExecuteCommands 0xf2/0xfe/0xff
    std::uint8_t unk249[3];
    std::uint32_t last_game_end_check;  // +0x24c game tick of the last CheckGameEnd evaluation (every 50 ticks)
    std::uint8_t unk250[0x350 - 0x250];
    std::uint32_t computer_players[20];  // +0x350 CComputerPlayer* per player (0 = none); ExecuteCommands 0xfe/0xff create one
    CGameData* world;          // +0x3a0 the world (CGameData)
    std::uint8_t unk3a4[4];
    std::uint32_t setup_3a8;   // +0x3a8 Setup argument 8 (meaning unknown)
    std::uint8_t unk3ac[4];
    std::uint32_t step;        // +0x3b0 the lockstep step the logic runs next (0 before the first)
    std::uint32_t granted_step;  // +0x3b4 the last step the network allowed (StepGrant message)
    std::uint8_t setup_3b8;    // +0x3b8 Setup argument 4 (a byte; meaning unknown)
    std::uint8_t unk3b9[7];
    std::uint32_t setup_3c0;   // +0x3c0 Setup argument 1 (meaning unknown)
    MessageQueue* toNet;       // +0x3c4 logic -> network (0x20 step and 0x21 command messages)
    MessageQueue* fromNet;     // +0x3c8 network -> logic (the replies)
    void* draw_slot;           // +0x3cc the shared draw-data slot (PublishDrawData exchanges into it)
    void* draw_data;           // +0x3d0 the draw-data buffer being filled (DrawData, 0xc7720 bytes)
    void* draw_event;          // +0x3d4 event set when new draw data was published
    void* draw_request_event;  // +0x3d8 event the draw thread sets to ask for new draw data
    std::uint8_t unk3dc[0x414 - 0x3dc];
    MessageLine messages[20];  // +0x414 the message lines shown on the screen (a ring)
    std::int32_t message_count;  // +0x644 (ShowMessage 0x51e440)
    std::int32_t message_index;  // +0x648 the current line; ClearMessages sets it to message_count - 1
    std::uint8_t unk64c[0x654 - 0x64c];
    std::uint8_t music_stopped;   // +0x654 1 after StopMusic (PlayNextMusicTrack then does nothing)
    std::uint8_t unk655[3];
    std::int32_t music_track;     // +0x658 index in the playlist of the track playing (-1 before the first)
    std::int32_t music_fixed_playlist;  // +0x65c StartMusic's playlist; -1 = cycle through the four
    std::int32_t music_playlist;  // +0x660 the playlist playing (one per race)
    std::uint8_t unk664[0x66c - 0x664];
    void* lobby;               // +0x66c the lobby object (Setup argument 9)
    void* lobby_client;        // +0x670 the lobby client (Setup argument 10)
    std::uint8_t unk674;
    std::uint8_t desync_detected;  // +0x675 set by ExecuteCommands 0xfb when another player's RNG history word (0x7dfb58) differs ("Desync!")
    std::uint8_t unk676[2];
};

#pragma pack(pop)

static_assert(sizeof(void*) == 4, "32-bit build only");
static_assert(offsetof(CLogicThread, group_selection_changed) == 0x4c);
static_assert(offsetof(CLogicThread, tick) == 0x70);
static_assert(offsetof(CLogicThread, unk074) == 0x74);
static_assert(offsetof(CLogicThread, pending_ui_refresh) == 0x84);
static_assert(offsetof(CLogicThread, save_request_tick) == 0x224);
static_assert(offsetof(CLogicThread, player_flags) == 0x235);
static_assert(offsetof(CLogicThread, game_finished) == 0x234);
static_assert(offsetof(CLogicThread, music_stopped) == 0x654);
static_assert(offsetof(CLogicThread, music_playlist) == 0x660);
static_assert(offsetof(CLogicThread, last_game_end_check) == 0x24c);
static_assert(offsetof(CLogicThread, computer_players) == 0x350);
static_assert(offsetof(CLogicThread, step) == 0x3b0);
static_assert(offsetof(CLogicThread, granted_step) == 0x3b4);
static_assert(offsetof(CLogicThread, desync_detected) == 0x675);
static_assert(offsetof(MessageQueue, buffer) == 0x14);
static_assert(offsetof(MessageQueue, lock) == 0x2c);
static_assert(sizeof(MessageQueue) == 0x34);
static_assert(offsetof(CLogicThread, world) == 0x3a0);
static_assert(offsetof(CLogicThread, toNet) == 0x3c4);
static_assert(offsetof(CLogicThread, setup_3a8) == 0x3a8);
static_assert(offsetof(CLogicThread, setup_3b8) == 0x3b8);
static_assert(offsetof(CLogicThread, setup_3c0) == 0x3c0);
static_assert(offsetof(CLogicThread, draw_slot) == 0x3cc);
static_assert(offsetof(CLogicThread, draw_request_event) == 0x3d8);
static_assert(offsetof(CLogicThread, messages) == 0x414);
static_assert(offsetof(CLogicThread, message_count) == 0x644);
static_assert(offsetof(CLogicThread, message_index) == 0x648);
static_assert(offsetof(CLogicThread, lobby) == 0x66c);
static_assert(offsetof(CLogicThread, lobby_client) == 0x670);
static_assert(offsetof(CLogicThread, fromNet) == 0x3c8);

// The logic thread fields of a CLogicThread pointer or of a test buffer.
template <typename T>
CLogicThread* LogicThreadOf(T* thread) {
    return reinterpret_cast<CLogicThread*>(thread);
}

}  // namespace s3re::game
