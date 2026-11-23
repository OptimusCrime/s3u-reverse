#pragma once

// Internal header of CLogicThread::ExecuteCommands (0x535990), our version:
// the frame (wait, pop, dispatch, step grant) is in execute_commands.cpp,
// the command handlers are split over
//   execute_commands.cpp        simple world commands, spells, ships
//   execute_commands_units.cpp  MoveUnits (0x09, 0x0d..0x0f)
//   execute_commands_trade.cpp  SetTradeRoute (0x12), SetWorkshopProduction (0x13)
//   execute_commands_areas.cpp  0x0a, 0x0b, 0x0c, 0x10 (area settings, garrison, carriers)
//   execute_commands_net.cpp    DestroyBuilding (0x06, with its message), the
//                               multiplayer/UI commands 0xf2, 0xf4, 0xf7..0xff and
//                               the network messages 3, 4, 5, 6, 8, 0x22
// The original is one function; it is hooked as one unit (S3RE_HOOK in
// execute_commands.cpp). Analysis and the list of commands:
// re/execute_commands.md.

#include "game/enums.h"
#include "game/logic_thread_layout.h"
#include "game/world.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game::commands {

// Largest message: byte 2 (the size in bytes) is at most 255, so 63 dwords.
constexpr std::size_t kMessageDwords = 64;

// Locals of the original's frame that keep their value from one message to
// the next (the loop never leaves the frame). A handler that reads a local
// it didn't write in the same message reads it from here; each such field
// says which handler sets it.
struct FrameLocals {
    // The message being executed ([ebp-0x19c]): a message shorter than the
    // previous one leaves that message's later bytes in place.
    union {
        std::uint32_t dwords[kMessageDwords];
        std::uint8_t bytes[kMessageDwords * 4];
    } message;
    // The 0x20 step message pushed at the end ([ebp-0x98]): {0x20, 0, 8, 0, step}.
    std::uint8_t step_message[8];
    // The type 5 message the PlayerLost network message (3) pushes ([ebp-0x60]):
    // {5, player, 8, 0, 0}, byte 1 set per use.
    std::uint8_t kill_message[8];
};

// What a handler works with.
struct Run {
    CLogicThread* thread;
    FrameLocals* frame;

    CGameData* world() const { return thread->world; }
    // Byte `i` of the command's payload (message byte 4 + i).
    std::uint8_t Byte(std::size_t i) const { return frame->message.bytes[4 + i]; }
    // The payload bytes i, i + 1 as a big-endian 16-bit number (x, y,
    // building and settler numbers are sent this way).
    std::uint32_t Word(std::size_t i) const { return Byte(i) * 256u + Byte(i + 1); }
    // Message byte 1: the command id (Command messages) or a player.
    std::uint8_t Id() const { return frame->message.bytes[1]; }
};

// ---------------------------------------------------------------- callees
// Calls of S3.EXE functions through their addresses (thiscall: ECX = the
// world or the thread; our verified replacements run behind the hooks, and
// the tests can stub them).
template <std::uintptr_t Address, typename... Args>
std::uint32_t Call(CGameData* world, Args... args) {
    using Fn = std::uint32_t(__fastcall*)(CGameData*, void*, decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(Address)(world, nullptr, static_cast<std::uint32_t>(args)...);
}
template <std::uintptr_t Address, typename... Args>
std::uint32_t CallThread(CLogicThread* thread, Args... args) {
    using Fn = std::uint32_t(__fastcall*)(CLogicThread*, void*, decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(Address)(thread, nullptr, static_cast<std::uint32_t>(args)...);
}

namespace callee {
inline constexpr std::uintptr_t kCheckBuildingSite = 0x004d99e0;
inline constexpr std::uintptr_t kNextSearchGeneration = 0x004ff3f0;
inline constexpr std::uintptr_t kClampCamera = 0x0051b4d0;
inline constexpr std::uintptr_t kChangePileGood = 0x004dfb40;
inline constexpr std::uintptr_t kClearBuildingFlag02 = 0x004df490;
inline constexpr std::uintptr_t kClearBuildingFlag04 = 0x004df510;
inline constexpr std::uintptr_t kClearBuildingFlag08 = 0x004df590;
inline constexpr std::uintptr_t kClearBuildingFlag10 = 0x004df610;
inline constexpr std::uintptr_t kSetBuildingFlag02 = 0x004df690;
inline constexpr std::uintptr_t kSetBuildingFlag04 = 0x004df730;
inline constexpr std::uintptr_t kSetBuildingFlag08 = 0x004df7d0;
inline constexpr std::uintptr_t kSetBuildingFlag10 = 0x004df850;
inline constexpr std::uintptr_t kRecallSiteCarriers = 0x004dee20;
inline constexpr std::uintptr_t kRecallBuildingCarriers = 0x004deec0;
inline constexpr std::uintptr_t kRecallCarriersOfOtherSites = 0x004df8e0;
inline constexpr std::uintptr_t kTileDistance = 0x004ff320;
inline constexpr std::uintptr_t kDrawWorkAreaOverlay = 0x00523840;  // ECX = the thread
inline constexpr std::uintptr_t kClearAreaGoodsSettings1D2 = 0x004ff3c0;
inline constexpr std::uintptr_t kResetAreaConditionCounters = 0x0051bc20;
inline constexpr std::uintptr_t kBlockBuildingLine = 0x004dbb90;
inline constexpr std::uintptr_t kPlaceDockLine = 0x004db710;
inline constexpr std::uintptr_t kSetPromotionCost = 0x00508450;
inline constexpr std::uintptr_t kAllocTable36700Record = 0x004fefd0;
inline constexpr std::uintptr_t kIsOnMap = 0x005194e0;
inline constexpr std::uintptr_t kNeighbourX = 0x00503700;
inline constexpr std::uintptr_t kNeighbourY = 0x00503730;
inline constexpr std::uintptr_t kPlaceSettlerNear = 0x0050ce80;
inline constexpr std::uintptr_t kPopMessage = 0x00430980;   // MessageQueue::Pop, ECX = the queue
inline constexpr std::uintptr_t kPushMessage = 0x004309c0;  // MessageQueue::Push(dwords, count, signal)
inline constexpr std::uintptr_t kWaitTime = 0x0040d0e0;     // the wait time of a kind (ECX = 5 here), ms
}  // namespace callee

// Pushes `dwordCount` dwords of `message` on the logic -> network queue
// (MessageQueue::Push 0x4309c0 with signal 1).
void PushToNet(CLogicThread* thread, const void* message, std::uint32_t dwordCount);

// ---------------------------------------------------------------- UI refresh
// Most handlers end by asking the UI to redraw the open menu when it shows
// what the command changed (world refresh_ui = 1):
// 0x537084: the building is the selected one (0x7dfd6c) and the building menu (4) is open.
void RefreshIfBuildingShown(Run& run, std::uint32_t building);
// 0x53709f with its callers' checks: the command's player is the local
// player, the open menu is `menu` and its page (`page`, one of the menu's
// fields) equals `value`.
void RefreshIfMenuShows(Run& run, std::uint32_t player, std::int32_t menu, std::int32_t page, std::int32_t value);

// ---------------------------------------------------------------- handlers
// execute_commands.cpp
void PlaceBuilding(Run& run);          // 0x01
void ScrollCamera(Run& run);           // 0x02
void SetSettlerGroup(Run& run);        // 0x03
void SetBuildingWorking(Run& run);     // 0x04
void SetWorkArea(Run& run);            // 0x05
void ChangeGoodsPriority(Run& run);    // 0x07
void SetGoodsSetting(Run& run);        // 0x08
void SetAreaCondition(Run& run);       // 0x11
void SetShipyardDock(Run& run);        // 0x14
void CastSpell(Run& run);              // 0x15
void UnloadShip(Run& run);             // 0x16
void SetHarbourDock(Run& run);         // 0x17
void PromoteSoldiers(Run& run);        // 0x18
void LoadShip(Run& run);               // 0x19
void HoldPosition(Run& run);           // 0x1b
void SetStorehouseGoods(Run& run);     // 0x1c
void RevealMap(Run& run);              // 0xf1
void PrioritizeSite(Run& run);         // 0xf5
void UnprioritizeSite(Run& run);       // 0xf6

// execute_commands_units.cpp
void MoveUnits(Run& run);              // 0x09, 0x0d, 0x0e, 0x0f

// execute_commands_trade.cpp
void SetTradeRoute(Run& run);          // 0x12
void SetWorkshopProduction(Run& run);  // 0x13

// execute_commands_areas.cpp
void ChangeTransportOrder(Run& run);   // 0x0a
void ChangeToolSettings(Run& run);     // 0x0b
void ChangeGarrison(Run& run);         // 0x0c
void ConvertCarriers(Run& run);        // 0x10

// execute_commands_net.cpp
void DestroyBuilding(Run& run);        // 0x06
void ClearPlayerFlag(Run& run);        // 0xf2
void SyncCheck(Run& run);              // 0xf4
void SaveRequest(Run& run);            // 0xf7
void SaveDone(Run& run);               // 0xf8
void SaveFailed(Run& run);             // 0xf9
void SaveAllowed(Run& run);            // 0xfa
void CheckVersion(Run& run);           // 0xfb
void PlayerLost(Run& run);             // 0xfc
void PlayerWon(Run& run);              // 0xfd
void DropPlayer(Run& run);             // 0xfe
void PlayerLeft(Run& run);             // 0xff
// The network messages other than Command and StepGrant.
void NetPlayerLost(Run& run);          // 3
void NetFilmEnd(Run& run);             // 4
void NetKillPlayer(Run& run);          // 5
void NetInGameChat(Run& run);          // 6
void NetChecksumFailure(Run& run);     // 8
void NetLagReport(Run& run);           // 0x22

}  // namespace s3re::game::commands
