// The computer players' command senders (computer players A4, session 52;
// Rainer\CompMapWorkerAgent.cpp; re/computer_players.md "How decisions
// reach the simulation"). The AI changes the game like a human player: it
// pushes 0x21 command messages (NetMessage::CommandRequest, byte 1 the
// CommandId, byte 2 the size, the payload from byte 4) on the logic->net
// queue with MessageQueue::Push (0x4309c0; thiscall (dwords, count,
// signal 1), count = size / 4), but only if GameSettings::IsHost (0x4318a0,
// g_gameSettings +0x7c) says this machine set up the game. EAX of the
// senders without a fixed result is IsHost's (not the host) or Push's.
//
// Economy senders, fastcall with pointer arguments, push on the current
// computer player's queue (g_aiPlayer->message_queue, read after IsHost).
// They build the message in an uninitialized stack buffer: the bytes they
// don't write (always header byte 3; payload bytes listed per function)
// are stack leftovers in the original and 0 in ours, a known difference:
// ExecuteCommands doesn't read them for these commands (its handlers read
// only the payload bytes listed below), so it changes nothing.
//
//   0x0058e220 AI::SendSetWorkArea(const u16* building, const u8* player,
//              stack const u16* x, const u16* y), ret 8, SetWorkArea (5),
//              12 bytes: x, y, building (words), player; left out: 11. If
//              the tile's area (AI_AreaAt) isn't the building's
//              (AI_BuildingArea), the tile first moves to the nearest one in
//              the building's area: map offsets 0..9899 of g_aiWorld added to
//              (x, y), on the map (unsigned, against CGameData::map_size),
//              area word equal; none found (or only at offset 9900): nothing
//              is sent and EAX = 9901 (the loop counter).
//   0x0058e5c0 AI::SendDestroyBuilding(const u16* building, const u8*
//              player), DestroyBuilding (6), 8 bytes: building, player; left
//              out: 7.
//   0x0058e630 AI::SendChangeGarrison(op, building, stack class) values,
//              ret 4, ChangeGarrison (12), 8 bytes: building (word, bits
//              8..15 and 0..7 of the dword), op, the stack byte. (Used by
//              0x573890 with the player as op? see the inventory.)
//   0x0058e6a0 AI::SendPlaceBuilding(const u16* x, const u16* y, stack
//              const u16* type, const u8* player), ret 8, PlaceBuilding (1),
//              12 bytes: x, y, type, player; left out: 11. The type's high
//              byte is always 0 (the original shifts the low byte right by 8).
//              EAX = 1.
//   0x0058e740 AI::SendChangeToolSettings(const u8* player, const u16* area,
//              stack const u8* kind, const u8* value), ret 8,
//              ChangeToolSettings (11), 12 bytes: kind, value, player, area;
//              left out: 9..11.
//   0x0058e7d0 AI::SendRaiseGoodsPriority(const u8* good, const u8* player,
//              stack const u16* area), ret 4, ChangeGoodsPriority (7): good,
//              1 (up), player, area; left out 9..11. EAX = 1.
//   0x0058e850 AI::SendPromoteSoldiers(const u8* player, const u8* kind),
//              PromoteSoldiers (24), 8 bytes: player, kind; left out 6, 7.
//   0x0058e8b0 AI::SendClearGoodsSetting(const u8* good, const u8* player,
//              stack const u16* area), ret 4, SetGoodsSetting (8): good, 0,
//              player, area; left out 9..11.
//   0x0058ea30 AI::SendSetAreaCondition(const u8* player, const u16* area,
//              stack const u8* kind, const u8* value), ret 8,
//              SetAreaCondition (17): kind, value, player, area; left out
//              9..11. EAX = 1.
//   0x0058eac0 AI::SendSetBuildingWorking(const u8* player, const u16*
//              building, stack const u8* working), ret 4, SetBuildingWorking
//              (4), 8 bytes: building, working, player. EAX = 1.
//   0x0058eb40 AI::SendToolSetting10(const u8* player, const u16* area,
//              stack const u8* value), ret 4, ChangeToolSettings (11) with
//              kind 10: 10, value, player, area; left out 9..11. EAX = 1.
//   0x0058ebc0 AI::SendChangeTransportOrder(const u8* player, const u16*
//              area, stack const u8* good, const u8* op), ret 8,
//              ChangeTransportOrder (10): good, op, player, area; left out
//              9..11. EAX = 1.
//   Not replaced: 0x0058e510 AI::SendPrioritizeSite. Its 8-byte message
//              leaves payload byte 2 (message byte 6) unwritten, and the
//              PrioritizeSite handler reads exactly that byte as the player
//              and does nothing unless it is the site's owner: whether the
//              command works depends on the original's stack leftovers,
//              which our code can't reproduce (open question, see
//              re/computer_players.md).
//
// Military senders, thiscall on g_aiCommandSender (0x7e5de8): the message
// is built in its buffer +0xc and pushed from there on g_aiMilitaryQueue
// (0x7e37c8). Bytes they don't write keep the previous command's values
// (reproduced: ours leaves them alone too). Words are written high byte
// first, from the low word of the dword arguments.
//
//   0x00590990 AI::SendCastSpell(player, priest, slot, x, y), ret 0x14,
//              CastSpell (21), 12 bytes: player, the spell (battle_spells
//              byte race * 3 + slot, race = the priest's Settler::race in
//              g_aiMapWorld; neither range-checked), x, y, priest.
//   0x00590a30 AI::SendMoveSettler(player, settler, x, y), ret 0x10: first
//              SetSettlerGroup (3): player, group 0, count word 1, the
//              settler; then MoveUnits (9): player, 0, x, y; bytes 10 and 11
//              stay. Each is sent if IsHost.
//   0x00590af0 AI::SendPatrolSettler: the same with MoveUnitsMode2 (14).
//   0x00590bb0 AI::QueueChangeGarrison(player, building, op), ret 0xc,
//              ChangeGarrison (12), 8 bytes: building, player, op.
//   0x00590c10 AI::SendConvertCarriers(player, kind, count, x, y), ret
//              0x14, ConvertCarriers (16), 12 bytes: kind, player, x, y,
//              count.
//   0x00590c80 AI::SendMoveGroup(player, group, x, y), ret 0x10: sends
//              nothing for an empty unit group (player_list_lengths; EAX =
//              the index player * 10 + group). Attack-move (MoveUnitsMode3,
//              15) instead of MoveUnits (9) when the group has no target
//              settler (AiGroupTarget::prey == 0) and either has the steal
//              flag, no target building (census) or defends. A defending
//              group asks AI::CountEnemyBowmenOnOwnLandNear 0x58a610 (on
//              0x7e5de4: x, y of its first member, distance 30, player)
//              and stays put while that is above 0 (EAX = the count). 12
//              bytes: player, group, x, y.

#include "game/ai/ai_layout.h"
#include "game/enums/commands.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/ui/game_settings_layout.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using namespace ai;
using layout::kMapRowTiles;

namespace {

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kIsHost = 0x004318a0;          // GameSettings::IsHost
constexpr std::uintptr_t kPush = 0x004309c0;            // MessageQueue::Push
constexpr std::uintptr_t kBuildingArea = 0x00599610;    // AI_BuildingArea
constexpr std::uintptr_t kAreaAt = 0x005995c0;          // AI_AreaAt
constexpr std::uintptr_t kCountEnemyBowmen = 0x0058a610;  // AI::CountEnemyBowmenOnOwnLandNear

constexpr std::size_t kSizeByte = 2;
constexpr std::uint8_t kShortCommand = 8;
constexpr std::uint8_t kLongCommand = 12;

constexpr std::uint32_t kWorkAreaSearchOffsets = 9900;  // offsets 0..9900 are tried, a find at 9900 is dropped
constexpr std::uint32_t kGuardDistance = 30;            // 0x590c80: enemy bowmen this close keep a defender put
constexpr std::uint8_t kToolSetting10 = 10;

using Message = std::uint8_t[kLongCommand];

std::uint8_t High(std::uint32_t value) { return static_cast<std::uint8_t>(value >> 8); }
std::uint8_t Low(std::uint32_t value) { return static_cast<std::uint8_t>(value); }

void Header(std::uint8_t* message, CommandId id, std::uint8_t size) {
    message[0] = Value(NetMessage::CommandRequest);
    message[1] = Value(id);
    message[kSizeByte] = size;
}

// A word, high byte first (as the handlers read it, ExecuteCommands' Word).
void PutWord(std::uint8_t* at, std::uint32_t value) {
    at[0] = High(value);
    at[1] = Low(value);
}

std::uint32_t IsHost() {
    using Fn = std::uint32_t(__fastcall*)(GameSettings* self, void* edx);
    return reinterpret_cast<Fn>(kIsHost)(&g_gameSettings.get(), nullptr);
}

std::uint32_t Push(std::uint32_t queue, const std::uint8_t* message) {
    using Fn = std::uint32_t(__fastcall*)(std::uint32_t self, void* edx, const std::uint8_t* dwords,
                                          std::uint32_t count, std::uint32_t signal);
    return reinterpret_cast<Fn>(kPush)(queue, nullptr, message, message[kSizeByte] >> 2, 1);
}

// Sends a finished message on the current computer player's queue (if this
// machine is the host). EAX: IsHost's or Push's.
std::uint32_t SendFromPlayer(const std::uint8_t* message) {
    const std::uint32_t host = IsHost();
    if (Low(host) == 0) {
        return host;
    }
    return Push(g_aiPlayer.get()->message_queue, message);
}

// The same for the military senders' buffer and queue.
std::uint32_t SendFromSender(AiCommandSender* sender) {
    const std::uint32_t host = IsHost();
    if (Low(host) == 0) {
        return host;
    }
    return Push(g_aiMilitaryQueue, sender->message);
}

// The economy senders' area commands: two payload bytes, player, area.
std::uint32_t SendAreaCommand(CommandId id, std::uint8_t first, std::uint8_t second, std::uint8_t player,
                              std::uint16_t area) {
    Message message{};
    Header(message, id, kLongCommand);
    message[4] = first;
    message[5] = second;
    message[6] = player;
    PutWord(&message[7], area);
    return SendFromPlayer(message);
}

}  // namespace

// The hooks' types.
using PtrPtrFn = std::uint32_t(__fastcall*)(const void* a, const void* b);
using PtrPtrStackFn = std::uint32_t(__fastcall*)(const void* a, const void* b, const void* c);
using PtrPtrStack2Fn = std::uint32_t(__fastcall*)(const void* a, const void* b, const void* c, const void* d);
using GarrisonFn = std::uint32_t(__fastcall*)(std::uint32_t op, std::uint32_t building, std::uint32_t third);
using Sender4Fn = std::uint32_t(__fastcall*)(AiCommandSender* self, void* edx, std::uint32_t a, std::uint32_t b,
                                             std::uint32_t c, std::uint32_t d);
using Sender3Fn = std::uint32_t(__fastcall*)(AiCommandSender* self, void* edx, std::uint32_t a, std::uint32_t b,
                                             std::uint32_t c);
using Sender5Fn = std::uint32_t(__fastcall*)(AiCommandSender* self, void* edx, std::uint32_t a, std::uint32_t b,
                                             std::uint32_t c, std::uint32_t d, std::uint32_t e);

std::uint32_t __fastcall AI_SendSetWorkArea(const void* building, const void* player, const void* x, const void* y);
std::uint32_t __fastcall AI_SendDestroyBuilding(const void* building, const void* player);
std::uint32_t __fastcall AI_SendChangeGarrison(std::uint32_t op, std::uint32_t building, std::uint32_t third);
std::uint32_t __fastcall AI_SendPlaceBuilding(const void* x, const void* y, const void* type, const void* player);
std::uint32_t __fastcall AI_SendChangeToolSettings(const void* player, const void* area, const void* kind,
                                                   const void* value);
std::uint32_t __fastcall AI_SendRaiseGoodsPriority(const void* good, const void* player, const void* area);
std::uint32_t __fastcall AI_SendPromoteSoldiers(const void* player, const void* kind);
std::uint32_t __fastcall AI_SendClearGoodsSetting(const void* good, const void* player, const void* area);
std::uint32_t __fastcall AI_SendSetAreaCondition(const void* player, const void* area, const void* kind,
                                                 const void* value);
std::uint32_t __fastcall AI_SendSetBuildingWorking(const void* player, const void* building, const void* working);
std::uint32_t __fastcall AI_SendToolSetting10(const void* player, const void* area, const void* value);
std::uint32_t __fastcall AI_SendChangeTransportOrder(const void* player, const void* area, const void* good,
                                                     const void* op);
std::uint32_t __fastcall AI_SendCastSpell(AiCommandSender* self, void* edx, std::uint32_t player,
                                          std::uint32_t priest, std::uint32_t slot, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall AI_SendMoveSettler(AiCommandSender* self, void* edx, std::uint32_t player,
                                            std::uint32_t settler, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall AI_SendPatrolSettler(AiCommandSender* self, void* edx, std::uint32_t player,
                                              std::uint32_t settler, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall AI_QueueChangeGarrison(AiCommandSender* self, void* edx, std::uint32_t player,
                                                std::uint32_t building, std::uint32_t op);
std::uint32_t __fastcall AI_SendConvertCarriers(AiCommandSender* self, void* edx, std::uint32_t player,
                                                std::uint32_t kind, std::uint32_t count, std::uint32_t x,
                                                std::uint32_t y);
std::uint32_t __fastcall AI_SendMoveGroup(AiCommandSender* self, void* edx, std::uint32_t player, std::uint32_t group,
                                          std::uint32_t x, std::uint32_t y);

S3RE_HOOK(0x0058e220, AI_SendSetWorkArea, PtrPtrStack2Fn, Original_AI_SendSetWorkArea, AI_SendSetWorkArea);
S3RE_HOOK(0x0058e5c0, AI_SendDestroyBuilding, PtrPtrFn, Original_AI_SendDestroyBuilding, AI_SendDestroyBuilding);
S3RE_HOOK(0x0058e630, AI_SendChangeGarrison, GarrisonFn, Original_AI_SendChangeGarrison, AI_SendChangeGarrison);
S3RE_HOOK(0x0058e6a0, AI_SendPlaceBuilding, PtrPtrStack2Fn, Original_AI_SendPlaceBuilding, AI_SendPlaceBuilding);
S3RE_HOOK(0x0058e740, AI_SendChangeToolSettings, PtrPtrStack2Fn, Original_AI_SendChangeToolSettings,
          AI_SendChangeToolSettings);
S3RE_HOOK(0x0058e7d0, AI_SendRaiseGoodsPriority, PtrPtrStackFn, Original_AI_SendRaiseGoodsPriority,
          AI_SendRaiseGoodsPriority);
S3RE_HOOK(0x0058e850, AI_SendPromoteSoldiers, PtrPtrFn, Original_AI_SendPromoteSoldiers, AI_SendPromoteSoldiers);
S3RE_HOOK(0x0058e8b0, AI_SendClearGoodsSetting, PtrPtrStackFn, Original_AI_SendClearGoodsSetting,
          AI_SendClearGoodsSetting);
S3RE_HOOK(0x0058ea30, AI_SendSetAreaCondition, PtrPtrStack2Fn, Original_AI_SendSetAreaCondition,
          AI_SendSetAreaCondition);
S3RE_HOOK(0x0058eac0, AI_SendSetBuildingWorking, PtrPtrStackFn, Original_AI_SendSetBuildingWorking,
          AI_SendSetBuildingWorking);
S3RE_HOOK(0x0058eb40, AI_SendToolSetting10, PtrPtrStackFn, Original_AI_SendToolSetting10, AI_SendToolSetting10);
S3RE_HOOK(0x0058ebc0, AI_SendChangeTransportOrder, PtrPtrStack2Fn, Original_AI_SendChangeTransportOrder,
          AI_SendChangeTransportOrder);
S3RE_HOOK(0x00590990, AI_SendCastSpell, Sender5Fn, Original_AI_SendCastSpell, AI_SendCastSpell);
S3RE_HOOK(0x00590a30, AI_SendMoveSettler, Sender4Fn, Original_AI_SendMoveSettler, AI_SendMoveSettler);
S3RE_HOOK(0x00590af0, AI_SendPatrolSettler, Sender4Fn, Original_AI_SendPatrolSettler, AI_SendPatrolSettler);
S3RE_HOOK(0x00590bb0, AI_QueueChangeGarrison, Sender3Fn, Original_AI_QueueChangeGarrison, AI_QueueChangeGarrison);
S3RE_HOOK(0x00590c10, AI_SendConvertCarriers, Sender5Fn, Original_AI_SendConvertCarriers, AI_SendConvertCarriers);
S3RE_HOOK(0x00590c80, AI_SendMoveGroup, Sender4Fn, Original_AI_SendMoveGroup, AI_SendMoveGroup);

namespace {

const std::uint8_t& Byte(const void* p) { return *static_cast<const std::uint8_t*>(p); }
const std::uint16_t& Word(const void* p) { return *static_cast<const std::uint16_t*>(p); }

}  // namespace

// Purpose: Tells a production building where to work (a forester's or
// woodcutter's work area), moved into the building's economy area first;
// sent by the computer players.
std::uint32_t __fastcall AI_SendSetWorkArea(const void* building, const void* player, const void* xPtr,
                                            const void* yPtr) {
    std::uint32_t x = Word(xPtr);
    std::uint32_t y = Word(yPtr);
    const std::uint32_t number = Word(building);
    using AreaFn = std::uint32_t(__fastcall*)(const void* a, const void* b);
    const auto buildingArea = static_cast<std::uint16_t>(reinterpret_cast<AreaFn>(kBuildingArea)(&number, nullptr));
    const auto tileArea = static_cast<std::uint16_t>(reinterpret_cast<AreaFn>(kAreaAt)(xPtr, yPtr));
    if (tileArea != buildingArea) {
        // 0x58e26f..0x58e2f7: the nearest tile of the building's area.
        const CGameData* world = g_aiWorld;
        const std::uint32_t size = world->map_size;
        bool found = false;
        for (std::uint32_t i = 0;; ++i) {
            const std::uint32_t tx = world->offsets[i].dx + (x & 0xffff);
            const std::uint32_t ty = world->offsets[i].dy + (y & 0xffff);
            if (tx < size && ty < size && (&world->tiles[0][0])[tx * kMapRowTiles + ty].area == buildingArea) {
                x = tx;
                y = ty;
                found = true;
            }
            if (i + 1 > kWorkAreaSearchOffsets) {
                return i + 1;  // nothing sent; EAX = the loop counter
            }
            if (found) {
                break;
            }
        }
    }
    Message message{};
    Header(message, CommandId::SetWorkArea, kLongCommand);
    PutWord(&message[4], x);
    PutWord(&message[6], y);
    PutWord(&message[8], Word(building));
    message[10] = Byte(player);
    return SendFromPlayer(message);
}

// Purpose: Tears down a building of a computer player.
std::uint32_t __fastcall AI_SendDestroyBuilding(const void* building, const void* player) {
    Message message{};
    Header(message, CommandId::DestroyBuilding, kShortCommand);
    PutWord(&message[4], Word(building));
    message[6] = Byte(player);
    return SendFromPlayer(message);
}

// Purpose: Changes the soldiers a computer player wants in a tower or
// castle (probably; the argument order of its caller 0x573890 is not
// read yet).
std::uint32_t __fastcall AI_SendChangeGarrison(std::uint32_t op, std::uint32_t building, std::uint32_t third) {
    Message message{};
    Header(message, CommandId::ChangeGarrison, kShortCommand);
    PutWord(&message[4], building);
    message[6] = Low(op);
    message[7] = Low(third);
    return SendFromPlayer(message);
}

// Purpose: Places a new building site for a computer player.
std::uint32_t __fastcall AI_SendPlaceBuilding(const void* x, const void* y, const void* type, const void* player) {
    Message message{};
    Header(message, CommandId::PlaceBuilding, kLongCommand);
    PutWord(&message[4], Word(x));
    PutWord(&message[6], Word(y));
    message[8] = 0;  // the original shifts the type's low byte right by 8
    message[9] = Byte(type);
    message[10] = Byte(player);
    SendFromPlayer(message);
    return 1;
}

// Purpose: Changes one of the tool-making settings of a computer player's
// economy area.
std::uint32_t __fastcall AI_SendChangeToolSettings(const void* player, const void* area, const void* kind,
                                                   const void* value) {
    return SendAreaCommand(CommandId::ChangeToolSettings, Byte(kind), Byte(value), Byte(player), Word(area));
}

// Purpose: Raises the delivery priority of a good in a computer player's
// economy area by one step.
std::uint32_t __fastcall AI_SendRaiseGoodsPriority(const void* good, const void* player, const void* area) {
    SendAreaCommand(CommandId::ChangeGoodsPriority, Byte(good), 1, Byte(player), Word(area));
    return 1;
}

// Purpose: Promotes a computer player's soldiers of a kind (paid with
// mana).
std::uint32_t __fastcall AI_SendPromoteSoldiers(const void* player, const void* kind) {
    Message message{};
    Header(message, CommandId::PromoteSoldiers, kShortCommand);
    message[4] = Byte(player);
    message[5] = Byte(kind);
    return SendFromPlayer(message);
}

// Purpose: Sets a goods setting of a computer player's economy area to 0
// (probably: stops a good from being stored or produced there).
std::uint32_t __fastcall AI_SendClearGoodsSetting(const void* good, const void* player, const void* area) {
    return SendAreaCommand(CommandId::SetGoodsSetting, Byte(good), 0, Byte(player), Word(area));
}

// Purpose: Switches an area condition of a computer player's economy area
// (e.g. its "force production" switch, 0x572fc0).
std::uint32_t __fastcall AI_SendSetAreaCondition(const void* player, const void* area, const void* kind,
                                                 const void* value) {
    SendAreaCommand(CommandId::SetAreaCondition, Byte(kind), Byte(value), Byte(player), Word(area));
    return 1;
}

// Purpose: Lets a computer player's building work or stop working.
std::uint32_t __fastcall AI_SendSetBuildingWorking(const void* player, const void* building, const void* working) {
    Message message{};
    Header(message, CommandId::SetBuildingWorking, kShortCommand);
    PutWord(&message[4], Word(building));
    message[6] = Byte(working);
    message[7] = Byte(player);
    SendFromPlayer(message);
    return 1;
}

// Purpose: Sets the tool setting of kind 10 of a computer player's economy
// area.
std::uint32_t __fastcall AI_SendToolSetting10(const void* player, const void* area, const void* value) {
    SendAreaCommand(CommandId::ChangeToolSettings, kToolSetting10, Byte(value), Byte(player), Word(area));
    return 1;
}

// Purpose: Moves a good in the transport order of a computer player's
// economy area.
std::uint32_t __fastcall AI_SendChangeTransportOrder(const void* player, const void* area, const void* good,
                                                     const void* op) {
    SendAreaCommand(CommandId::ChangeTransportOrder, Byte(good), Byte(op), Byte(player), Word(area));
    return 1;
}

// Purpose: Has a computer player's priest cast one of its race's battle
// spells at a place.
std::uint32_t __fastcall AI_SendCastSpell(AiCommandSender* self, void* /*edx*/, std::uint32_t player,
                                          std::uint32_t priest, std::uint32_t slot, std::uint32_t x,
                                          std::uint32_t y) {
    const std::uint32_t race = g_aiMapWorld.get()->settlers[priest].race;  // (not range-checked)
    std::uint8_t* message = self->message;
    Header(message, CommandId::CastSpell, kLongCommand);
    message[4] = Low(player);
    message[5] = (&self->battle_spells[0][0])[race * 3 + slot];  // (not range-checked)
    PutWord(&message[6], x);
    PutWord(&message[8], y);
    PutWord(&message[10], priest);
    return SendFromSender(self);
}

namespace {

// 0x590a30 / 0x590af0: puts one settler into the player's scratch unit
// group 0 (SetSettlerGroup), then moves the group (`move`: MoveUnits or a
// variant). Bytes 10 and 11 keep their earlier values.
std::uint32_t MoveOneSettler(AiCommandSender* self, CommandId move, std::uint32_t player, std::uint32_t settler,
                             std::uint32_t x, std::uint32_t y) {
    constexpr std::uint8_t kScratchGroup = 0;
    std::uint8_t* message = self->message;
    Header(message, CommandId::SetSettlerGroup, kLongCommand);
    message[4] = Low(player);
    message[5] = kScratchGroup;
    PutWord(&message[6], 1);  // one settler
    PutWord(&message[8], settler);
    SendFromSender(self);
    Header(message, move, kLongCommand);
    message[4] = Low(player);
    message[5] = kScratchGroup;
    PutWord(&message[6], x);
    PutWord(&message[8], y);
    return SendFromSender(self);
}

}  // namespace

// Purpose: Sends a single settler of a computer player (e.g. a hunter
// after an enemy thief) to a place.
std::uint32_t __fastcall AI_SendMoveSettler(AiCommandSender* self, void* /*edx*/, std::uint32_t player,
                                            std::uint32_t settler, std::uint32_t x, std::uint32_t y) {
    return MoveOneSettler(self, CommandId::MoveUnits, player, settler, x, y);
}

// Purpose: The same with the second movement mode (probably patrolling or
// a forced march).
std::uint32_t __fastcall AI_SendPatrolSettler(AiCommandSender* self, void* /*edx*/, std::uint32_t player,
                                              std::uint32_t settler, std::uint32_t x, std::uint32_t y) {
    return MoveOneSettler(self, CommandId::MoveUnitsMode2, player, settler, x, y);
}

// Purpose: Changes the soldiers a computer player wants in a tower or
// castle (from the military AI).
std::uint32_t __fastcall AI_QueueChangeGarrison(AiCommandSender* self, void* /*edx*/, std::uint32_t player,
                                                std::uint32_t building, std::uint32_t op) {
    std::uint8_t* message = self->message;
    Header(message, CommandId::ChangeGarrison, kShortCommand);
    PutWord(&message[4], building);
    message[6] = Low(player);
    message[7] = Low(op);
    return SendFromSender(self);
}

// Purpose: Turns some of a computer player's carriers into specialists
// (geologists, thieves, pioneers) near a place.
std::uint32_t __fastcall AI_SendConvertCarriers(AiCommandSender* self, void* /*edx*/, std::uint32_t player,
                                                std::uint32_t kind, std::uint32_t count, std::uint32_t x,
                                                std::uint32_t y) {
    std::uint8_t* message = self->message;
    Header(message, CommandId::ConvertCarriers, kLongCommand);
    message[4] = Low(kind);
    message[5] = Low(player);
    PutWord(&message[6], x);
    PutWord(&message[8], y);
    message[10] = Low(count);
    return SendFromSender(self);
}

// Purpose: Sends a computer player's unit group to a place, attacking on
// the way when it goes for a building (or defends); a defending group stays
// while enemy bowmen are near.
std::uint32_t __fastcall AI_SendMoveGroup(AiCommandSender* self, void* /*edx*/, std::uint32_t player,
                                          std::uint32_t group, std::uint32_t x, std::uint32_t y) {
    const CGameData* world = g_aiMapWorld;
    const std::uint32_t index = player * kUnitGroups + group;  // (not range-checked)
    if ((&world->player_list_lengths[0][0])[index] == 0) {
        return index;
    }
    const AiGroupOrders& orders = g_aiGroupOrders.get();
    const AiAgentData& census = g_aiCensus.get();
    const std::uint8_t defending = (&census.group_defending[0][0])[index];
    bool attack = false;
    if ((&orders.group_targets[0][0])[index].prey == 0) {
        attack = (&orders.group_flags[0][0])[index] != 0 || (&census.group_target_buildings[0][0])[index] == 0 ||
                 defending != 0;
    }
    if ((&census.group_defending[0][0])[index] != 0) {
        const layout::Settler& first = world->settlers[(&world->player_lists[0][0][0])[index * 100]];
        using CountFn = std::int32_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t x, std::uint32_t y,
                                                  std::uint32_t distance, std::uint32_t player);
        const auto search = reinterpret_cast<std::uintptr_t>(&g_aiEnemySearch.get());
        const std::int32_t bowmen = reinterpret_cast<CountFn>(kCountEnemyBowmen)(search, nullptr, first.x, first.y,
                                                                                  kGuardDistance, player);
        if (bowmen > 0) {
            return static_cast<std::uint32_t>(bowmen);
        }
    }
    std::uint8_t* message = self->message;
    Header(message, attack ? CommandId::MoveUnitsMode3 : CommandId::MoveUnits, kLongCommand);
    message[4] = Low(player);
    message[5] = Low(group);
    PutWord(&message[6], x);
    PutWord(&message[8], y);
    return SendFromSender(self);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// IsHost, Push and CountEnemyBowmenOnOwnLandNear are recording stubs (both
// versions call them through their S3.EXE addresses): IsHost returns a
// random dword whose low byte is 0 a third of the time, Push logs the queue,
// the count, the signal flag and the message bytes (except the economy
// senders' unwritten stack bytes, see the file header). The military
// senders' buffer g_aiCommandSender and the globals they read are set to
// the same random bytes for both versions and compared afterwards.

namespace {

struct PushRecord {
    std::uint32_t callee;
    std::uint32_t self;
    std::uint32_t count;
    std::uint32_t signal;
    std::uint8_t bytes[kLongCommand];
    bool operator==(const PushRecord& other) const { return std::memcmp(this, &other, sizeof(*this)) == 0; }
};

struct CommandLog {
    std::vector<PushRecord> records;
    std::uint16_t ignored = 0;  // message bytes left out of the comparison (bit i: byte i)
};

CommandLog* g_log = nullptr;

constexpr std::uint32_t kQueue = 0x5151a000;  // the fake queue pointers (never dereferenced: Push is a stub)
constexpr std::uint32_t kMilitaryQueue = 0x5151b000;

std::uint32_t RecordPush(std::uint32_t self, const std::uint32_t* args) {
    PushRecord record{};
    record.callee = kPush;
    record.self = self;
    record.count = args[1];
    record.signal = args[2];
    std::memcpy(record.bytes, reinterpret_cast<const void*>(args[0]), sizeof(record.bytes));
    for (std::size_t i = 0; i < kLongCommand; ++i) {
        if ((g_log->ignored >> i) & 1 || i >= 4 * record.count) {  // (bytes past the message: not pushed)
            record.bytes[i] = 0;
        }
    }
    g_log->records.push_back(record);
    return 0x7000 + static_cast<std::uint32_t>(g_log->records.size());
}

struct Inputs {
    std::uint32_t host[2];   // IsHost's results (first and second call)
    std::int32_t bowmen;     // CountEnemyBowmenOnOwnLandNear's result
    std::uint32_t values[6];
    std::uint8_t bytes[4];
    std::uint16_t words[4];
};

struct Saved {
    AiCommandSender sender = g_aiCommandSender.get();
    std::uint32_t militaryQueue = g_aiMilitaryQueue;
    ComputerPlayer* player = g_aiPlayer;
    CGameData* world = g_aiWorld;
    CGameData* mapWorld = g_aiMapWorld;
    std::vector<std::uint8_t> orders, census;
    Saved()
        : orders(reinterpret_cast<const std::uint8_t*>(&g_aiGroupOrders.get()),
                 reinterpret_cast<const std::uint8_t*>(&g_aiGroupOrders.get()) + sizeof(AiGroupOrders)),
          census(reinterpret_cast<const std::uint8_t*>(&g_aiCensus.get()),
                 reinterpret_cast<const std::uint8_t*>(&g_aiCensus.get()) + sizeof(AiAgentData)) {}
    ~Saved() {
        g_aiCommandSender.get() = sender;
        g_aiMilitaryQueue = militaryQueue;
        g_aiPlayer = player;
        g_aiWorld = world;
        g_aiMapWorld = mapWorld;
        std::memcpy(&g_aiGroupOrders.get(), orders.data(), orders.size());
        std::memcpy(&g_aiCensus.get(), census.data(), census.size());
    }
};

// The senders under test, as one table of calls (original and candidate).
struct Senders {
    PtrPtrStack2Fn setWorkArea, placeBuilding, toolSettings, areaCondition, transportOrder;
    PtrPtrFn destroy, promote;
    GarrisonFn garrison;
    PtrPtrStackFn raisePriority, clearSetting, buildingWorking, toolSetting10;
    Sender5Fn castSpell, convertCarriers;
    Sender4Fn moveSettler, patrolSettler, moveGroup;
    Sender3Fn queueGarrison;
};

Senders OriginalSenders() {
    return {Original_AI_SendSetWorkArea,     Original_AI_SendPlaceBuilding,      Original_AI_SendChangeToolSettings,
            Original_AI_SendSetAreaCondition, Original_AI_SendChangeTransportOrder, Original_AI_SendDestroyBuilding,
            Original_AI_SendPromoteSoldiers, Original_AI_SendChangeGarrison,     Original_AI_SendRaiseGoodsPriority,
            Original_AI_SendClearGoodsSetting, Original_AI_SendSetBuildingWorking, Original_AI_SendToolSetting10,
            Original_AI_SendCastSpell,       Original_AI_SendConvertCarriers,    Original_AI_SendMoveSettler,
            Original_AI_SendPatrolSettler,   Original_AI_SendMoveGroup,          Original_AI_QueueChangeGarrison};
}

Senders OurSenders() {
    return {&AI_SendSetWorkArea,      &AI_SendPlaceBuilding,     &AI_SendChangeToolSettings,
            &AI_SendSetAreaCondition, &AI_SendChangeTransportOrder, &AI_SendDestroyBuilding,
            &AI_SendPromoteSoldiers,  &AI_SendChangeGarrison,    &AI_SendRaiseGoodsPriority,
            &AI_SendClearGoodsSetting, &AI_SendSetBuildingWorking, &AI_SendToolSetting10,
            &AI_SendCastSpell,        &AI_SendConvertCarriers,   &AI_SendMoveSettler,
            &AI_SendPatrolSettler,    &AI_SendMoveGroup,         &AI_QueueChangeGarrison};
}

constexpr int kOps = 18;

// Bytes the economy senders leave unwritten (bit i: message byte i).
constexpr std::uint16_t kHeaderGap = 1u << 3;
constexpr std::uint16_t kLongTail = (1u << 9) | (1u << 10) | (1u << 11);

std::uint32_t CallSender(const Senders& f, int op, const Inputs& in, CommandLog& log) {
    AiCommandSender* sender = &g_aiCommandSender.get();
    const std::uint32_t* v = in.values;
    const std::uint8_t* b = in.bytes;
    const std::uint16_t* w = in.words;
    switch (op) {
    case 0: log.ignored = kHeaderGap | 1u << 11; return f.setWorkArea(&w[0], &b[0], &w[1], &w[2]);
    case 1: log.ignored = kHeaderGap | 1u << 11; return f.placeBuilding(&w[0], &w[1], &w[2], &b[0]);
    case 2: log.ignored = kHeaderGap | kLongTail; return f.toolSettings(&b[0], &w[0], &b[1], &b[2]);
    case 3: log.ignored = kHeaderGap | kLongTail; return f.areaCondition(&b[0], &w[0], &b[1], &b[2]);
    case 4: log.ignored = kHeaderGap | kLongTail; return f.transportOrder(&b[0], &w[0], &b[1], &b[2]);
    case 5: log.ignored = kHeaderGap | 1u << 7; return f.destroy(&w[0], &b[0]);
    case 6: log.ignored = kHeaderGap | 1u << 6 | 1u << 7; return f.promote(&b[0], &b[1]);
    case 7: log.ignored = kHeaderGap; return f.garrison(v[0], v[1], v[2]);
    case 8: log.ignored = kHeaderGap | kLongTail; return f.raisePriority(&b[0], &b[1], &w[0]);
    case 9: log.ignored = kHeaderGap | kLongTail; return f.clearSetting(&b[0], &b[1], &w[0]);
    case 10: log.ignored = kHeaderGap; return f.buildingWorking(&b[0], &w[0], &b[1]);
    case 11: log.ignored = kHeaderGap | kLongTail; return f.toolSetting10(&b[0], &w[0], &b[1]);
    case 12: log.ignored = 0; return f.castSpell(sender, nullptr, v[0], v[1] % 32000, v[2] % 12, v[3], v[4]);
    case 13: log.ignored = 0; return f.convertCarriers(sender, nullptr, v[0], v[1], v[2], v[3], v[4]);
    case 14: log.ignored = 0; return f.moveSettler(sender, nullptr, v[0], v[1], v[2], v[3]);
    case 15: log.ignored = 0; return f.patrolSettler(sender, nullptr, v[0], v[1], v[2], v[3]);
    case 16: log.ignored = 0; return f.moveGroup(sender, nullptr, v[0] % 20, v[1] % 10, v[2], v[3]);
    default: log.ignored = 0; return f.queueGarrison(sender, nullptr, v[0], v[1], v[2]);
    }
}

// The world parts the senders read: the work-area search (map size, tiles'
// areas, offsets, a building's door), priests' races and the unit groups.
void SetUpWorld(test::Rng& rng, CGameData* world, const Inputs& in) {
    world->map_size = 40 + rng.Below(30);
    for (std::uint32_t x = 0; x < 80; ++x) {
        for (std::uint32_t y = 0; y < 80; ++y) {
            world->tiles[x][y].area = static_cast<std::uint16_t>(rng.Below(6));
        }
    }
    const std::uint16_t building = in.words[0];
    world->buildings[building].door_x = static_cast<std::uint16_t>(rng.Below(80));
    world->buildings[building].door_y = static_cast<std::uint16_t>(rng.Below(80));
    world->settlers[in.values[1] % 32000].race = static_cast<std::uint8_t>(rng.Below(4));
    for (std::uint32_t group = 0; group < kUnitGroups; ++group) {
        const std::uint32_t player = in.values[0] % 20;
        world->player_list_lengths[player][group] = rng.Chance(1, 4) ? 0 : static_cast<std::int32_t>(rng.Below(5));
        const auto first = static_cast<std::uint16_t>(1 + rng.Below(31999));
        world->player_lists[player][group][0] = first;
    }
}

void CompareSenders(test::Context& ctx, const Senders& candidate, int cases) {
    const Senders original = OriginalSenders();
    Saved saved;
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    ctx.rng().Fill(&world->offsets[0], 10000 * sizeof(layout::MapOffset));
    for (std::size_t i = 0; i < 10000; ++i) {  // small offsets, as the real table's first ones
        world->offsets[i].dx = static_cast<std::int32_t>(ctx.rng().Below(41)) - 20;
        world->offsets[i].dy = static_cast<std::int32_t>(ctx.rng().Below(41)) - 20;
    }
    g_aiWorld = world;
    g_aiMapWorld = world;
    std::vector<std::uint8_t> playerBytes(sizeof(ComputerPlayer));
    auto* player = reinterpret_cast<ComputerPlayer*>(playerBytes.data());
    player->message_queue = kQueue;
    g_aiPlayer = player;
    g_aiMilitaryQueue = kMilitaryQueue;

    Inputs in{};
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    int hostCall = 0;
    stubs.Install<0, kIsHost>([&](CGameData*, std::uint32_t, const std::uint32_t*) {
        return in.host[hostCall++ & 1];
    });
    stubs.Install<3, kPush>([&](CGameData*, std::uint32_t self, const std::uint32_t* args) {
        return RecordPush(self, args);
    });
    stubs.Install<4, kCountEnemyBowmen>([&](CGameData*, std::uint32_t, const std::uint32_t*) {
        return static_cast<std::uint32_t>(in.bowmen);
    });

    const std::uint16_t sizeX = g_aiMapSizeX, sizeY = g_aiMapSizeY;
    g_aiMapSizeX = 80;
    g_aiMapSizeY = 80;
    std::vector<std::uint8_t> ordersStart(sizeof(AiGroupOrders)), censusStart(sizeof(AiAgentData));
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        const int op = static_cast<int>(rng.Below(kOps));
        for (std::uint32_t& host : in.host) {
            host = rng.Next32() & (rng.Chance(1, 3) ? 0xffffff00u : 0xffffffffu);
            host |= (host & 0xff) == 0 && rng.Chance(1, 2) ? 1 : 0;
        }
        in.bowmen = static_cast<std::int32_t>(rng.Below(3)) - 1;
        for (std::uint32_t& value : in.values) {
            value = rng.Chance(1, 2) ? rng.Below(100) : rng.Next32();
        }
        rng.Fill(in.bytes, sizeof(in.bytes));
        for (std::uint16_t& word : in.words) {
            word = static_cast<std::uint16_t>(rng.Chance(1, 2) ? rng.Below(80) : rng.Next32());
        }
        in.words[0] = static_cast<std::uint16_t>(rng.Below(4000));
        SetUpWorld(rng, world, in);
        rng.Fill(ordersStart.data(), ordersStart.size());
        rng.Fill(censusStart.data(), censusStart.size());
        AiCommandSender senderStart{};
        rng.Fill(&senderStart, sizeof(senderStart));
        {  // the unit group 0x590c80 looks at: each condition often met
            auto* orders = reinterpret_cast<AiGroupOrders*>(ordersStart.data());
            auto* census = reinterpret_cast<AiAgentData*>(censusStart.data());
            const std::uint32_t p = in.values[0] % 20, g = in.values[1] % 10;
            orders->group_targets[p][g].prey = static_cast<std::uint16_t>(rng.Chance(1, 2) ? 0 : rng.Next32());
            orders->group_flags[p][g] = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : rng.Next32());
            census->group_target_buildings[p][g] =
                static_cast<std::uint16_t>(rng.Chance(1, 2) ? 0 : rng.Next32());
            census->group_defending[p][g] = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : rng.Next32());
        }

        CommandLog logA, logB;
        auto run = [&](const Senders& f, CommandLog& log, AiCommandSender& senderAfter) {
            std::memcpy(&g_aiGroupOrders.get(), ordersStart.data(), ordersStart.size());
            std::memcpy(&g_aiCensus.get(), censusStart.data(), censusStart.size());
            g_aiCommandSender.get() = senderStart;
            g_log = &log;
            hostCall = 0;
            const std::uint32_t result = CallSender(f, op, in, log);
            senderAfter = g_aiCommandSender.get();
            g_log = nullptr;
            return result;
        };
        AiCommandSender afterA{}, afterB{};
        const std::uint32_t ra = run(original, logA, afterA);
        const std::uint32_t rb = run(candidate, logB, afterB);
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT_EQ(logA.records.size(), logB.records.size());
        for (std::size_t k = 0; same && k < logA.records.size(); ++k) {
            same &= S3RE_EXPECT_MEM_EQ(&logA.records[k], &logB.records[k], sizeof(PushRecord));
        }
        same &= S3RE_EXPECT_MEM_EQ(&afterA, &afterB, sizeof(afterA));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
        }
    }
    g_aiMapSizeX = sizeX;
    g_aiMapSizeY = sizeY;
}

std::uint32_t __fastcall WrongPlaceBuilding(const void* x, const void* y, const void* type, const void* player) {
    // Wrong: the type's high byte is sent.
    Message message{};
    Header(message, CommandId::PlaceBuilding, kLongCommand);
    PutWord(&message[4], Word(x));
    PutWord(&message[6], Word(y));
    PutWord(&message[8], Word(type));
    message[10] = Byte(player);
    SendFromPlayer(message);
    return 1;
}

std::uint32_t __fastcall WrongMoveSettler(AiCommandSender* self, void* /*edx*/, std::uint32_t player,
                                          std::uint32_t settler, std::uint32_t x, std::uint32_t y) {
    // Wrong: clears bytes 10 and 11 of the buffer.
    self->message[10] = 0;
    self->message[11] = 0;
    return MoveOneSettler(self, CommandId::MoveUnits, player, settler, x, y);
}

std::uint32_t __fastcall WrongMoveGroup(AiCommandSender* self, void* edx, std::uint32_t player, std::uint32_t group,
                                        std::uint32_t x, std::uint32_t y) {
    // Wrong: the steal flag doesn't make the group attack-move (the buffer
    // shows it after the call).
    const std::uint32_t result = AI_SendMoveGroup(self, edx, player, group, x, y);
    const std::uint32_t index = player * kUnitGroups + group;
    const AiAgentData& census = g_aiCensus.get();
    if ((&g_aiGroupOrders.get().group_flags[0][0])[index] != 0 &&
        (&census.group_target_buildings[0][0])[index] != 0 && (&census.group_defending[0][0])[index] == 0) {
        self->message[1] = Value(CommandId::MoveUnits);
    }
    return result;
}

std::uint32_t __fastcall WrongSetWorkArea(const void* building, const void* player, const void* x, const void* y) {
    // Wrong: never moves the tile into the building's area.
    Message message{};
    Header(message, CommandId::SetWorkArea, kLongCommand);
    PutWord(&message[4], Word(x));
    PutWord(&message[6], Word(y));
    PutWord(&message[8], Word(building));
    message[10] = Byte(player);
    return SendFromPlayer(message);
}

}  // namespace

S3RE_TEST(AiCommandSenders_MatchOriginal) {
    CompareSenders(ctx, OurSenders(), 6000);
}

S3RE_TEST(AiCommandSenders_ComparisonDetectsWrongImplementation) {
    const auto probe = [&](auto change) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        Senders wrong = OurSenders();
        change(wrong);
        CompareSenders(quiet, wrong, 3000);
        S3RE_EXPECT(quiet.failures() > 0);
    };
    probe([](Senders& f) { f.placeBuilding = &WrongPlaceBuilding; });
    probe([](Senders& f) { f.moveSettler = &WrongMoveSettler; });
    probe([](Senders& f) { f.moveGroup = &WrongMoveGroup; });
    probe([](Senders& f) { f.setWorkArea = &WrongSetWorkArea; });
}

}  // namespace s3re::game
