// The military AI's unit-group bookkeeping on the army object
// g_aiGroupOrders (Rainer\CompMapWorkerAgent.cpp; AiGroupOrders in
// ai_layout.h, re/computer_players.md "Army"). A computer player's soldiers
// are kept in the unit groups 1..9 of the world's settler lists
// (CGameData::player_lists / player_list_lengths, index player * 10 +
// group); the army object holds per group its home area, target, gather
// position and mage count, and the settler bitset of everyone with orders.
// None of these range-check the player or group (flat indexes, as the
// original).
//
//   0x00591c10 AI::Army::Army() thiscall: the constructor of g_aiGroupOrders
//              (reached from a static-initializer stub): zeroes the settler
//              bitset (+0xca8, 1000 dwords; std::bitset's constructor) and
//              calls AI::Army::Reset 0x591e00. EAX = this.
//   0x00593f40 AI::ArmyState::ArmyState() thiscall: the constructor of
//              g_aiArmyState: calls AI::ArmyState_Reset 0x593f50. EAX = this.
//   0x00591fb0 AI::Army::CountSpareGroups(player) thiscall, ret 4, and
//   0x00592100 AI::Army::CountSpareSoldiers(player) thiscall, ret 4: both
//              first pair the player's non-empty groups 1..9 by home area
//              (+0): repeatedly the first unpicked group that isn't spare
//              is picked, and of the later groups with the same home area
//              every second one (the first, third, ...) becomes spare; this
//              repeats until a pass picks nothing (FindSpareGroups below,
//              0x591fca..0x5920a5 / 0x59211a..0x5921f5). Spare groups
//              without a target building (census +4) then count:
//              CountSpareGroups counts those with at least half the group
//              size (+0x1d60, signed / 2 toward zero) members,
//              CountSpareSoldiers sums their member counts.
//   0x00592240 AI::Army::MoveLastGroupInto(player, group) thiscall, ret 8:
//              shrinks the player's group size by 5 when the result stays
//              at least 7; then, unless `group` is the highest group in
//              use (+0x1d10; EAX = it), moves the highest group into slot
//              `group`: its target building (census +4) and defending flag
//              (census +0x194), its target (+0x3e8), its steal flag
//              (+0x320) and its world settler list and length; the attack
//              delays (g_aiArmyState +0x194) of both slots become 0, the
//              moved-from slot's fields 0. Home area, gather position, mage
//              count and +0x1c48 stay. The highest group goes down by one
//              (EAX = the new value).
//   0x005926e0 AI::Army::AssignMage(player, mage) thiscall, ret 8: unless
//              the mage's bit is set (AL = 0), appends it to the group
//              1..9 with 1..97 members (unsigned) that has the fewest mages
//              (+0x988, signed, below 100; the first such on a tie), counts
//              it there and sets its bit (AL = 1); no such group: AL = 0.
//              The bitset index is checked (std::bitset::test/set: >= 32000
//              calls _Xran 0x596570, which throws). EAX's upper bytes are
//              left over (reproduced; the caller 0x593cd0 doesn't use EAX).
//   0x00592820 AI::Army::AssignSoldier(player, soldier) thiscall, ret 8:
//              unless the soldier's bit is set (AL = 0), appends it to the
//              first group 1..9 whose length is below the group size
//              (unsigned); when all nine are full, the size grows by 5 and
//              the search starts again at group 1, unless the size passes
//              100 (then it becomes 100, AL = 0). A group's first member
//              sets its gather position (+0x1db0): the gather offset of the
//              group (+0x20d0) added to the nearest (TileDistance 0x4ff320,
//              signed, first on a tie) of the player's buildings found per
//              owned economy area 1..area_high_water (the first military
//              building of the area, 0x58b380, else its first building,
//              0x58b410), whose area becomes the group's home area (+0); with
//              none, to the soldier's own position (home area unchanged).
//              Then the length goes up, the highest group (+0x1d10) follows
//              and the bit is set (AL = 1). Dead code kept out: after a
//              first member of group 1 the original tests the address of
//              +0x1d10 against 0 (always non-zero), so its block 0x592aeb
//              (NearestFreeInteriorMilitaryBuilding 0x58aa70, home area and
//              the target of +0x190) never runs.
//   0x00592bf0 AI::Army::DisbandWeakGroups() thiscall: for every computer
//              player (Player::active == 2) and group 1..9: a non-empty
//              group without a target building that has at most a quarter
//              of the group size members (the length against the signed
//              quarter, unsigned) is disbanded; then any non-empty group
//              (also the one just moved into the slot) without a swordsman
//              or pikeman (types 5, 15, 31, 32, 34, 35) is disbanded too.
//              Disbanding clears the members' bits in g_aiGroupOrders'
//              bitset, sets the length to 0 and calls MoveLastGroupInto
//              0x592240 on g_aiGroupOrders; the group size is `this`'s.
//              EAX = this + 0x1db0 (the loop's pointer past the group sizes;
//              the caller 0x58f990 returns it).
//   0x00592620 AI::Army::ReturnGroupHome(player, group) thiscall, ret 8: a
//              group with more than one member (unsigned) whose first
//              member has no enemy bowmen within 30 (0x58a610 on
//              g_aiEnemySearch) is logged ("Center - player: %d, group: %d
//              \n", sprintf 0x61f27e and OutputDebugStringA) and sent to
//              its gather position (SendMoveGroup 0x590c80, x and y sign-
//              extended). EAX = player * 5 (a leftover) for a small group,
//              the bowmen count, or SendMoveGroup's.
//   0x005923e0 AI::Army::FindSpareGroup(player, building, anyDelay)
//              thiscall, ret 0xc: pairs the groups like CountSpareGroups,
//              then among the spare groups 1..9 without a target building
//              (census +4) that have at least half the group size members
//              (signed): with building 0, the first whose attack delay
//              (g_aiArmyState +0x194) is 0 is returned at once; otherwise
//              the one whose first member is nearest (signed TileDistance,
//              first on a tie) to the building: original quirk, kept,
//              TileDistance gets the building's x as both x and y. The
//              nearest one (or -1) becomes -1 when its attack delay is not
//              0 and the byte `anyDelay` is 0 (for -1 that reads the dword
//              before the player's group 0 delay; reproduced).
//   0x00591c40 AI::Army::RebuildAssignedSettlers() thiscall: clears the
//              settler bitset (+0xca8, 1000 dwords from the top down), then
//              for every computer player (Player::active == 2) sets the bits
//              of the members of groups 1..9 and counts the mages (type 36)
//              per group (+0x988), and sets the bits of the hunters in the
//              player's hunter vector (g_aiHunterVectors). Bitset indexes >=
//              32000 call _Xran 0x596570. EAX is not used by the caller.
//   0x00593cd0 AI::Army::AssignIdleUnits(player) thiscall, ret 4: walks the
//              player's settler lists (Player::settler_lists, chains by
//              Settler::next) of the nine soldier types in the order
//              Swordsman1, Swordsman2, Swordsman3, Bowman1, Bowman2,
//              Bowman3, Pikeman1, Pikeman2, Pikeman3 and of the mages in
//              parallel, one settler of each per round: soldiers go to
//              AssignSoldier 0x592820, mages to AssignMage 0x5926e0, until
//              all ten chains ended. EAX is not used by the caller.

#include "game/ai/ai_layout.h"
#include "game/enums/settler_types.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/texts.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace s3re::game {

using namespace ai;
using layout::kPlayers;

namespace {

constexpr std::uint32_t kGroupSlots = 100;          // settlers per unit group list
constexpr std::uint32_t kOrderBits = 32000;         // the settler bitset's size
constexpr std::uint32_t kFirstGroup = 1;            // group 0 is the senders' scratch group
constexpr std::uint32_t kLastGroup = 9;
constexpr std::int32_t kGroupSizeStep = 5;          // AssignSoldier grows, MoveLastGroupInto shrinks by this
constexpr std::int32_t kSmallestGroupSize = 7;      // MoveLastGroupInto doesn't shrink below this
constexpr std::int32_t kLargestGroupSize = 100;
constexpr std::int32_t kMageLimit = 100;            // AssignMage: groups with fewer mages than this
constexpr std::uint32_t kMageGroupLength = 98;      // AssignMage: groups below this many members
constexpr std::int32_t kComputerControlled = 2;     // Player::active of a computer player
constexpr std::uint32_t kBowmenDistance = 30;       // ReturnGroupHome: enemy bowmen this close keep the group
constexpr std::int32_t kNoArea = -1;
constexpr std::int32_t kFarthest = 0x7fffffff;
constexpr std::size_t kCenterTextBytes = 256;       // the original's stack buffer

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kArmyReset = 0x00591e00;            // AI::Army::Reset
constexpr std::uintptr_t kArmyStateReset = 0x00593f50;       // AI::ArmyState_Reset
constexpr std::uintptr_t kMoveLastGroupInto = 0x00592240;    // AI::Army::MoveLastGroupInto
constexpr std::uintptr_t kBitsetXran = 0x00596570;           // std::bitset::_Xran (throws)
constexpr std::uintptr_t kCountEnemyBowmen = 0x0058a610;     // AI::CountEnemyBowmenOnTerritoryNear
constexpr std::uintptr_t kSendMoveGroup = 0x00590c80;        // AI::SendMoveGroup
constexpr std::uintptr_t kSprintf = 0x0061f27e;              // the CRT's sprintf
constexpr std::uintptr_t kFirstMilitaryBuilding = 0x0058b380;  // AI::FirstMilitaryBuildingInArea (stdcall)
constexpr std::uintptr_t kFirstBuilding = 0x0058b410;        // AI::FirstBuildingInArea (stdcall)
constexpr std::uintptr_t kTileDistance = 0x004ff320;         // TileDistance (stdcall)
constexpr std::uintptr_t kAssignSoldier = 0x00592820;        // AI::Army::AssignSoldier
constexpr std::uintptr_t kAssignMage = 0x005926e0;           // AI::Army::AssignMage

// A unit group's list length and members in the world (flat index player *
// 10 + group, not range-checked).
std::int32_t& ListLength(CGameData* world, std::uint32_t index) {
    return (&world->player_list_lengths[0][0])[index];
}

std::uint16_t* ListMembers(CGameData* world, std::uint32_t index) {
    return &world->player_lists[0][0][0] + index * kGroupSlots;
}

// The bitset's dword holding `settler`'s bit, checked like std::bitset's
// test/set/reset: an index >= 32000 calls _Xran first (it throws; in the
// emulator it is probed and returns).
std::uint32_t& OrderWord(std::uint8_t* bits, std::uint32_t settler) {
    if (settler >= kOrderBits) {
        using XranFn = void(__fastcall*)(void* self, void* edx);
        reinterpret_cast<XranFn>(kBitsetXran)(bits, nullptr);
    }
    return reinterpret_cast<std::uint32_t*>(bits)[settler >> 5];
}

std::uint32_t OrderMask(std::uint32_t settler) { return 1u << (settler & 31); }

// 0x591fca..0x5920a5 (and 0x59211a..0x5921f5): the player's spare groups.
// `unpicked` starts as "has members"; a pass picks the first unpicked group
// that isn't spare (its home area becomes the pass's area) and marks every
// second later unpicked group of that area spare; passes repeat while one
// picks or matches anything.
void FindSpareGroups(const AiGroupOrders* self, CGameData* world, std::uint32_t player, bool (&spare)[kUnitGroups]) {
    bool unpicked[kUnitGroups] = {};
    for (bool& flag : spare) {
        flag = false;
    }
    const std::uint32_t base = player * kUnitGroups;
    for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
        if (static_cast<std::uint32_t>(ListLength(world, base + group)) > 0) {
            unpicked[group] = true;
        }
    }
    const std::uint16_t* homeAreas = &self->home_areas[0][0] + base;
    bool changed = false;
    do {
        std::int32_t area = kNoArea;
        bool markNext = true;
        changed = false;
        for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
            if (!unpicked[group]) {
                continue;
            }
            if (area == kNoArea && !spare[group]) {
                area = homeAreas[group];
                unpicked[group] = false;
                changed = true;
            } else if (homeAreas[group] == area) {
                if (markNext) {
                    spare[group] = true;
                }
                markNext = !markNext;
                changed = true;
            }
        }
    } while (changed);
}

std::uint16_t& TargetBuilding(std::uint32_t index) {
    return (&g_aiCensus.get().group_target_buildings[0][0])[index];
}

// 0x592c7c..0x592ce3 (twice): clears the members' bits in g_aiGroupOrders,
// empties the list and moves the highest group into the slot.
void DisbandGroup(std::uint32_t player, std::uint32_t group) {
    AiGroupOrders& orders = g_aiGroupOrders.get();
    const std::uint32_t index = player * kUnitGroups + group;
    CGameData* world = g_aiMapWorld;
    for (std::uint32_t k = 0; k < static_cast<std::uint32_t>(ListLength(world, index)); ++k) {
        const std::uint32_t member = ListMembers(world, index)[k];
        OrderWord(orders.settlers_with_orders, member) &= ~OrderMask(member);
        world = g_aiMapWorld;
    }
    ListLength(world, index) = 0;
    using MoveFn = std::uint32_t(__fastcall*)(AiGroupOrders* self, void* edx, std::uint32_t player,
                                              std::uint32_t group);
    reinterpret_cast<MoveFn>(kMoveLastGroupInto)(&orders, nullptr, player, group);
}

// A swordsman or pikeman of any level: a group without one is disbanded
// (jump table 0x592e34 / 0x592e3c over the types 5..35).
bool IsMeleeSoldier(std::uint8_t type) {
    switch (static_cast<SettlerType>(type)) {
    case SettlerType::Swordsman1:
    case SettlerType::Swordsman2:
    case SettlerType::Swordsman3:
    case SettlerType::Pikeman1:
    case SettlerType::Pikeman2:
    case SettlerType::Pikeman3:
        return true;
    default:
        return false;
    }
}

}  // namespace

using ArmyCtorFn = AiGroupOrders*(__fastcall*)(AiGroupOrders* self, void* edx);
using ArmyStateCtorFn = AiArmyState*(__fastcall*)(AiArmyState* self, void* edx);
using ArmyPlayerFn = std::uint32_t(__fastcall*)(AiGroupOrders* self, void* edx, std::uint32_t player);
using ArmyPlayerGroupFn = std::uint32_t(__fastcall*)(AiGroupOrders* self, void* edx, std::uint32_t player,
                                                     std::uint32_t second);
using ArmyNoArgFn = std::uint32_t(__fastcall*)(AiGroupOrders* self, void* edx);

AiGroupOrders* __fastcall AI_Army_Army(AiGroupOrders* self, void* edx);
AiArmyState* __fastcall AI_ArmyState_ArmyState(AiArmyState* self, void* edx);
std::uint32_t __fastcall AI_Army_CountSpareGroups(AiGroupOrders* self, void* edx, std::uint32_t player);
std::uint32_t __fastcall AI_Army_CountSpareSoldiers(AiGroupOrders* self, void* edx, std::uint32_t player);
std::uint32_t __fastcall AI_Army_MoveLastGroupInto(AiGroupOrders* self, void* edx, std::uint32_t player,
                                                   std::uint32_t group);
std::uint32_t __fastcall AI_Army_AssignMage(AiGroupOrders* self, void* edx, std::uint32_t player, std::uint32_t mage);
std::uint32_t __fastcall AI_Army_AssignSoldier(AiGroupOrders* self, void* edx, std::uint32_t player,
                                               std::uint32_t soldier);
std::uint32_t __fastcall AI_Army_DisbandWeakGroups(AiGroupOrders* self, void* edx);
std::uint32_t __fastcall AI_Army_ReturnGroupHome(AiGroupOrders* self, void* edx, std::uint32_t player,
                                                 std::uint32_t group);
std::int32_t __fastcall AI_Army_FindSpareGroup(AiGroupOrders* self, void* edx, std::uint32_t player,
                                               std::uint32_t building, std::uint8_t anyDelay);
void __fastcall AI_Army_RebuildAssignedSettlers(AiGroupOrders* self, void* edx);
void __fastcall AI_Army_AssignIdleUnits(AiGroupOrders* self, void* edx, std::uint32_t player);

using FindSpareGroupFn = std::int32_t(__fastcall*)(AiGroupOrders* self, void* edx, std::uint32_t player,
                                                   std::uint32_t building, std::uint8_t anyDelay);
using RebuildFn = void(__fastcall*)(AiGroupOrders* self, void* edx);
using AssignIdleFn = void(__fastcall*)(AiGroupOrders* self, void* edx, std::uint32_t player);

S3RE_HOOK(0x00591c10, AI_Army_Army, ArmyCtorFn, Original_AI_Army_Army, AI_Army_Army);
S3RE_HOOK(0x00593f40, AI_ArmyState_ArmyState, ArmyStateCtorFn, Original_AI_ArmyState_ArmyState,
          AI_ArmyState_ArmyState);
S3RE_HOOK(0x00591fb0, AI_Army_CountSpareGroups, ArmyPlayerFn, Original_AI_Army_CountSpareGroups,
          AI_Army_CountSpareGroups);
S3RE_HOOK(0x00592100, AI_Army_CountSpareSoldiers, ArmyPlayerFn, Original_AI_Army_CountSpareSoldiers,
          AI_Army_CountSpareSoldiers);
S3RE_HOOK(0x00592240, AI_Army_MoveLastGroupInto, ArmyPlayerGroupFn, Original_AI_Army_MoveLastGroupInto,
          AI_Army_MoveLastGroupInto);
S3RE_HOOK(0x005926e0, AI_Army_AssignMage, ArmyPlayerGroupFn, Original_AI_Army_AssignMage, AI_Army_AssignMage);
S3RE_HOOK(0x00592820, AI_Army_AssignSoldier, ArmyPlayerGroupFn, Original_AI_Army_AssignSoldier,
          AI_Army_AssignSoldier);
S3RE_HOOK(0x00592bf0, AI_Army_DisbandWeakGroups, ArmyNoArgFn, Original_AI_Army_DisbandWeakGroups,
          AI_Army_DisbandWeakGroups);
S3RE_HOOK(0x005923e0, AI_Army_FindSpareGroup, FindSpareGroupFn, Original_AI_Army_FindSpareGroup,
          AI_Army_FindSpareGroup);
S3RE_HOOK(0x00591c40, AI_Army_RebuildAssignedSettlers, RebuildFn, Original_AI_Army_RebuildAssignedSettlers,
          AI_Army_RebuildAssignedSettlers);
S3RE_HOOK(0x00593cd0, AI_Army_AssignIdleUnits, AssignIdleFn, Original_AI_Army_AssignIdleUnits,
          AI_Army_AssignIdleUnits);
S3RE_HOOK(0x00592620, AI_Army_ReturnGroupHome, ArmyPlayerGroupFn, Original_AI_Army_ReturnGroupHome,
          AI_Army_ReturnGroupHome);

// Purpose: Builds the computer players' army object at program start.
AiGroupOrders* __fastcall AI_Army_Army(AiGroupOrders* self, void* /*edx*/) {
    for (std::uint8_t& bits : self->settlers_with_orders) {  // the bitset's constructor
        bits = 0;
    }
    using ResetFn = AiGroupOrders*(__fastcall*)(AiGroupOrders* self, void* edx);
    reinterpret_cast<ResetFn>(kArmyReset)(self, nullptr);
    return self;
}

// Purpose: Builds the computer players' attack timers object at program
// start.
AiArmyState* __fastcall AI_ArmyState_ArmyState(AiArmyState* self, void* /*edx*/) {
    using ResetFn = AiArmyState*(__fastcall*)(AiArmyState* self, void* edx);
    reinterpret_cast<ResetFn>(kArmyStateReset)(self, nullptr);
    return self;
}

// Purpose: How many of a computer player's unit groups could join an attack
// (groups that share their home area with another group and are at least
// half full), for the decision to start one.
std::uint32_t __fastcall AI_Army_CountSpareGroups(AiGroupOrders* self, void* /*edx*/, std::uint32_t player) {
    CGameData* world = g_aiMapWorld;
    bool spare[kUnitGroups];
    FindSpareGroups(self, world, player, spare);
    const std::int32_t half = self->group_size[player] / 2;
    std::uint32_t count = 0;
    for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
        const std::uint32_t index = player * kUnitGroups + group;
        if (spare[group] && TargetBuilding(index) == 0 && ListLength(world, index) >= half) {
            ++count;
        }
    }
    return count;
}

// Purpose: How many soldiers a computer player has in spare unit groups
// (groups sharing their home area with another one, without a target), to
// weigh an attack.
std::uint32_t __fastcall AI_Army_CountSpareSoldiers(AiGroupOrders* self, void* /*edx*/, std::uint32_t player) {
    CGameData* world = g_aiMapWorld;
    bool spare[kUnitGroups];
    FindSpareGroups(self, world, player, spare);
    std::uint32_t soldiers = 0;
    for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
        const std::uint32_t index = player * kUnitGroups + group;
        if (spare[group] && TargetBuilding(index) == 0) {
            soldiers += static_cast<std::uint32_t>(ListLength(world, index));
        }
    }
    return soldiers;
}

// Purpose: Fills the slot of a dissolved unit group with the computer
// player's highest group, so that the groups in use stay numbered 1..n,
// and lets new groups be a little smaller.
std::uint32_t __fastcall AI_Army_MoveLastGroupInto(AiGroupOrders* self, void* /*edx*/, std::uint32_t player,
                                                   std::uint32_t group) {
    const std::int32_t smaller = static_cast<std::int32_t>(static_cast<std::uint32_t>(self->group_size[player]) -
                                                           kGroupSizeStep);
    if (smaller >= kSmallestGroupSize) {
        self->group_size[player] = smaller;
    }
    const auto last = static_cast<std::uint32_t>(self->highest_group[player]);
    if (group == last) {
        return last;
    }
    const std::uint32_t from = player * kUnitGroups + last;
    const std::uint32_t to = player * kUnitGroups + group;
    AiAgentData& census = g_aiCensus.get();
    std::uint32_t* attackDelays = &g_aiArmyState.get().attack_delays[0][0];
    std::uint16_t* targetBuildings = &census.group_target_buildings[0][0];
    targetBuildings[to] = targetBuildings[from];
    attackDelays[from] = 0;
    targetBuildings[from] = 0;
    attackDelays[to] = 0;
    AiGroupTarget* targets = &self->group_targets[0][0];
    targets[to].patrol0 = targets[from].patrol0;
    targets[to].patrol1 = targets[from].patrol1;
    targets[to].prey = targets[from].prey;
    targets[from].patrol0 = 0;
    targets[from].patrol1 = 0;
    targets[from].prey = 0;
    std::uint8_t* defending = &census.group_defending[0][0];
    defending[to] = defending[from];
    defending[from] = 0;
    std::uint8_t* stealFlags = &self->group_flags[0][0];
    stealFlags[to] = stealFlags[from];
    stealFlags[from] = 0;
    // 0x59236a..0x592394: the whole list of 100 slots, then the length.
    for (std::uint32_t k = 0; k < kGroupSlots; ++k) {
        ListMembers(g_aiMapWorld, to)[k] = ListMembers(g_aiMapWorld, from)[k];
    }
    ListLength(g_aiMapWorld, to) = ListLength(g_aiMapWorld, from);
    ListLength(g_aiMapWorld, from) = 0;
    self->highest_group[player] -= 1;
    return static_cast<std::uint32_t>(self->highest_group[player]);
}

// Purpose: Puts a computer player's new mage into the unit group with the
// fewest mages, so that every group gets magic support.
std::uint32_t __fastcall AI_Army_AssignMage(AiGroupOrders* self, void* /*edx*/, std::uint32_t player,
                                            std::uint32_t mage) {
    std::uint8_t* bits = self->settlers_with_orders;
    const std::uint32_t mask = OrderMask(mage);
    if (OrderWord(bits, mage) & mask) {
        return mask & 0xffffff00u;  // AL = 0 (EAX: the mask's upper bytes)
    }
    CGameData* world = g_aiMapWorld;
    const std::uint32_t base = player * kUnitGroups;
    const std::int32_t* mages = &self->group_mages[0][0] + base;
    std::int32_t fewest = kMageLimit;
    std::uint32_t chosen = 0;
    for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
        const auto length = static_cast<std::uint32_t>(ListLength(world, base + group));
        if (length != 0 && length < kMageGroupLength && mages[group] < fewest) {
            fewest = mages[group];
            chosen = group;
        }
    }
    if (chosen == 0) {
        return player & 0xffffff00u;  // AL = 0 (EAX: the player's upper bytes)
    }
    const std::uint32_t index = base + chosen;
    ListMembers(g_aiMapWorld, index)[ListLength(g_aiMapWorld, index)] = static_cast<std::uint16_t>(mage);
    ListLength(g_aiMapWorld, index) += 1;
    (&self->group_mages[0][0])[index] += 1;
    std::uint32_t& word = OrderWord(bits, mage);
    word |= mask;
    return (word & 0xffffff00u) | 1;  // AL = 1 on the bitset dword's upper bytes
}

// Purpose: Puts a computer player's new soldier into the first unit group
// with room (growing the groups when all are full); a new group's gather
// point is set next to the player's nearest building.
std::uint32_t __fastcall AI_Army_AssignSoldier(AiGroupOrders* self, void* /*edx*/, std::uint32_t player,
                                               std::uint32_t soldier) {
    std::uint8_t* bits = self->settlers_with_orders;
    const std::uint32_t mask = OrderMask(soldier);
    const std::uint32_t wordOffset = (soldier >> 5) * 4;
    if (OrderWord(bits, soldier) & mask) {
        return wordOffset & 0xffffff00u;  // AL = 0 on the dword offset's upper bytes
    }
    CGameData* world = g_aiMapWorld;
    std::int32_t& size = self->group_size[player];
    const std::uint32_t base = player * kUnitGroups;
    // 0x59287d..0x5928d4: the first group below the size; when all are full
    // the size grows and the search starts again at group 1.
    std::uint32_t group = kFirstGroup;
    while (static_cast<std::uint32_t>(ListLength(world, base + group)) >= static_cast<std::uint32_t>(size)) {
        if (group < kLastGroup) {
            ++group;
            continue;
        }
        const std::int32_t grown = static_cast<std::int32_t>(static_cast<std::uint32_t>(size) + kGroupSizeStep);
        group = kFirstGroup;
        size = grown;
        if (grown > kLargestGroupSize) {
            size = kLargestGroupSize;
            return static_cast<std::uint32_t>(grown) & 0xffffff00u;  // AL = 0
        }
    }
    const std::uint32_t index = base + group;
    ListMembers(world, index)[ListLength(world, index)] = static_cast<std::uint16_t>(soldier);
    world = g_aiMapWorld;
    if (ListLength(world, index) == 0) {
        // 0x59290f..0x592ad0: the group's first member: gather near the
        // nearest building of the player.
        std::int32_t nearestArea = kNoArea;
        std::int32_t nearest = kFarthest;
        std::uint16_t buildingX = 0;
        std::uint16_t buildingY = 0;
        for (std::uint32_t area = 1; area < world->area_high_water + 1; ++area) {
            if ((&world->areas[0])[area].player != player) {
                continue;
            }
            using FirstFn = std::int32_t(__stdcall*)(std::uint32_t player, std::uint32_t area);
            std::int32_t building = reinterpret_cast<FirstFn>(kFirstMilitaryBuilding)(player, area);
            if (building < 1) {
                building = reinterpret_cast<FirstFn>(kFirstBuilding)(player, area);
            }
            world = g_aiMapWorld;
            if (building < 1) {
                continue;
            }
            const layout::Building& record = world->buildings[building];
            const layout::Settler& settler = (&world->settlers[0])[soldier];
            using DistanceFn = std::int32_t(__stdcall*)(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1,
                                                        std::uint32_t y1);
            const std::int32_t distance =
                reinterpret_cast<DistanceFn>(kTileDistance)(settler.x, settler.y, record.x, record.y);
            world = g_aiMapWorld;
            if (distance < nearest) {
                nearest = distance;
                nearestArea = static_cast<std::int32_t>(area);
                buildingX = world->buildings[building].x;
                buildingY = world->buildings[building].y;
            }
        }
        AiGroupPosition& gather = (&self->gather_positions[0][0])[index];
        const AiGroupPosition& offset = self->gather_offsets[group];
        if (nearestArea >= 1) {
            gather.x = static_cast<std::int16_t>(offset.x + buildingX);
            gather.y = static_cast<std::int16_t>(offset.y + buildingY);
            (&self->home_areas[0][0])[index] = static_cast<std::uint16_t>(nearestArea);
        } else {
            const layout::Settler& settler = (&world->settlers[0])[soldier];
            gather.x = static_cast<std::int16_t>(offset.x + settler.x);
            gather.y = static_cast<std::int16_t>(offset.y + settler.y);
        }
        // (0x592ad4..0x592b8c: dead, see the file header.)
    }
    world = g_aiMapWorld;
    ListLength(world, index) += 1;
    if (static_cast<std::int32_t>(group) > self->highest_group[player]) {
        self->highest_group[player] = static_cast<std::int32_t>(group);
    }
    OrderWord(bits, soldier) |= mask;
    return (wordOffset & 0xffffff00u) | 1;  // AL = 1 on the dword offset's upper bytes
}

// Purpose: Dissolves the computer players' unit groups that are too small
// or have no melee soldiers left, so their members can be regrouped.
std::uint32_t __fastcall AI_Army_DisbandWeakGroups(AiGroupOrders* self, void* /*edx*/) {
    for (std::uint32_t player = 0; player < kPlayers; ++player) {
        if (g_aiMapWorld.get()->players[player].active != kComputerControlled) {
            continue;
        }
        for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
            const std::uint32_t index = player * kUnitGroups + group;
            const auto length = static_cast<std::uint32_t>(ListLength(g_aiMapWorld, index));
            const std::int32_t quarter = self->group_size[player] / 4;
            if (length <= static_cast<std::uint32_t>(quarter) && length != 0 && TargetBuilding(index) == 0) {
                DisbandGroup(player, group);
            }
            CGameData* world = g_aiMapWorld;
            const auto members = static_cast<std::uint32_t>(ListLength(world, index));
            if (members == 0) {
                continue;
            }
            bool melee = false;
            for (std::uint32_t k = 0; k < members && !melee; ++k) {
                melee = IsMeleeSoldier(world->settlers[ListMembers(world, index)[k]].art);
            }
            if (!melee) {
                DisbandGroup(player, group);
            }
        }
    }
    return reinterpret_cast<std::uint32_t>(&self->gather_positions);  // the loop's pointer past group_size
}

// Purpose: Sends a computer player's unit group back to its gather point
// (unless enemy bowmen are close to it).
std::uint32_t __fastcall AI_Army_ReturnGroupHome(AiGroupOrders* self, void* /*edx*/, std::uint32_t player,
                                                 std::uint32_t group) {
    CGameData* world = g_aiMapWorld;
    const std::uint32_t index = player * kUnitGroups + group;
    if (static_cast<std::uint32_t>(ListLength(world, index)) <= 1) {
        return player * 5;  // a leftover of the index arithmetic
    }
    const layout::Settler& first = world->settlers[ListMembers(world, index)[0]];
    using CountFn = std::int32_t(__fastcall*)(void* self, void* edx, std::uint32_t x, std::uint32_t y,
                                              std::uint32_t distance, std::uint32_t player);
    const std::int32_t bowmen =
        reinterpret_cast<CountFn>(kCountEnemyBowmen)(&g_aiEnemySearch.get(), nullptr, first.x, first.y,
                                                     kBowmenDistance, player);
    if (bowmen != 0) {
        return static_cast<std::uint32_t>(bowmen);
    }
    char text[kCenterTextBytes];
    using SprintfFn = int(__cdecl*)(char* buffer, const char* format, ...);
    reinterpret_cast<SprintfFn>(kSprintf)(text, texts::kAiCenterGroup.c_str(), player, group);
    g_importOutputDebugStringA.get()(text);
    const AiGroupPosition& gather = (&self->gather_positions[0][0])[index];
    using SendFn = std::uint32_t(__fastcall*)(AiCommandSender* self, void* edx, std::uint32_t player,
                                              std::uint32_t group, std::int32_t x, std::int32_t y);
    return reinterpret_cast<SendFn>(kSendMoveGroup)(&g_aiCommandSender.get(), nullptr, player, group, gather.x,
                                                    gather.y);
}

// Purpose: Picks a computer player's spare unit group for an attack: the
// one nearest to the target building, or (without a building) the first
// one ready to go.
std::int32_t __fastcall AI_Army_FindSpareGroup(AiGroupOrders* self, void* /*edx*/, std::uint32_t player,
                                               std::uint32_t building, std::uint8_t anyDelay) {
    bool spare[kUnitGroups];
    FindSpareGroups(self, g_aiMapWorld, player, spare);
    const std::uint32_t* delays = &g_aiArmyState.get().attack_delays[0][0];
    const std::int32_t base = static_cast<std::int32_t>(player * kUnitGroups);
    std::int32_t nearest = kFarthest;
    std::int32_t result = -1;
    for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
        const std::uint32_t index = player * kUnitGroups + group;
        CGameData* world = g_aiMapWorld;
        if (!spare[group] || TargetBuilding(index) != 0 || ListLength(world, index) < self->group_size[player] / 2) {
            continue;
        }
        if (building == 0 && delays[index] == 0) {
            return static_cast<std::int32_t>(group);
        }
        const layout::Settler& first = world->settlers[ListMembers(world, index)[0]];
        const std::uint16_t bx = (&world->buildings[0])[building].x;
        using DistanceFn = std::int32_t(__stdcall*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
        // original quirk: the building's x twice
        const std::int32_t distance = reinterpret_cast<DistanceFn>(kTileDistance)(bx, bx, first.x, first.y);
        if (distance < nearest) {
            nearest = distance;
            result = static_cast<std::int32_t>(group);
        }
    }
    if (delays[base + result] != 0 && anyDelay == 0) {  // (index base - 1 for -1, reproduced)
        result = -1;
    }
    return result;
}

// Purpose: Rebuilds the bitset of settlers that have military orders from
// the computer players' unit groups and hunters (and recounts the mages per
// group), every so often.
void __fastcall AI_Army_RebuildAssignedSettlers(AiGroupOrders* self, void* /*edx*/) {
    for (std::size_t word = sizeof(self->settlers_with_orders) / 4; word-- > 0;) {
        reinterpret_cast<std::uint32_t*>(self->settlers_with_orders)[word] = 0;
    }
    for (std::uint32_t player = 0; player < kPlayers; ++player) {
        if (g_aiMapWorld.get()->players[player].active != kComputerControlled) {
            continue;
        }
        for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
            const std::uint32_t index = player * kUnitGroups + group;
            std::int32_t& mages = (&self->group_mages[0][0])[index];
            mages = 0;
            for (std::uint32_t k = 0; k < static_cast<std::uint32_t>(ListLength(g_aiMapWorld, index)); ++k) {
                const std::uint32_t member = ListMembers(g_aiMapWorld, index)[k];
                OrderWord(self->settlers_with_orders, member) |= OrderMask(member);
                if (g_aiMapWorld.get()->settlers[ListMembers(g_aiMapWorld, index)[k]].art ==
                    Value(SettlerType::Mage)) {
                    ++mages;
                }
            }
        }
        const AiVector& hunters = g_aiHunterVectors.get()[player];
        for (std::uint32_t entry = hunters.begin; entry != hunters.end; entry += 8) {
            const std::uint32_t hunter = *reinterpret_cast<const std::uint32_t*>(entry);
            OrderWord(self->settlers_with_orders, hunter) |= OrderMask(hunter);
        }
    }
}

// Purpose: Puts all of a computer player's soldiers and mages that are not
// in a unit group yet into one, every so often.
void __fastcall AI_Army_AssignIdleUnits(AiGroupOrders* self, void* /*edx*/, std::uint32_t player) {
    // 0x593cf0..0x593d62: the chains in the original's order.
    constexpr SettlerType kSoldierTypes[] = {
        SettlerType::Swordsman1, SettlerType::Swordsman2, SettlerType::Swordsman3,
        SettlerType::Bowman1,    SettlerType::Bowman2,    SettlerType::Bowman3,
        SettlerType::Pikeman1,   SettlerType::Pikeman2,   SettlerType::Pikeman3,
    };
    const layout::Player& record = g_aiMapWorld.get()->players[player];
    std::uint32_t soldiers[std::size(kSoldierTypes)];
    for (std::size_t k = 0; k < std::size(kSoldierTypes); ++k) {
        soldiers[k] = record.settler_lists[Value(kSoldierTypes[k])];
    }
    std::uint32_t mage = record.settler_lists[Value(SettlerType::Mage)];
    using AssignFn = std::uint32_t(__fastcall*)(AiGroupOrders* self, void* edx, std::uint32_t player,
                                                std::uint32_t settler);
    bool any = false;
    do {
        any = false;
        for (std::uint32_t& soldier : soldiers) {
            if (soldier != 0) {
                reinterpret_cast<AssignFn>(kAssignSoldier)(self, nullptr, player, soldier);
                soldier = g_aiMapWorld.get()->settlers[soldier].next;
                any = true;
            }
        }
        if (mage != 0) {
            reinterpret_cast<AssignFn>(kAssignMage)(self, nullptr, player, mage);
            mage = g_aiMapWorld.get()->settlers[mage].next;
            any = true;
        }
    } while (any);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// The functions run on the real g_aiGroupOrders (`this` of every caller),
// g_aiArmyState and g_aiCensus and on a test world (g_aiMapWorld). Each case
// sets up random state, runs the original, keeps the state it left
// (world unit lists, the three objects), restores the start, runs ours and
// compares. ReturnGroupHome's callees (0x58a610, SendMoveGroup) are
// recording stubs and OutputDebugStringA's import is pointed at a recorder;
// AssignSoldier and DisbandWeakGroups call their verified callees.

namespace {

constexpr std::uint32_t kTestSettlers = 3000;  // members are drawn from 1..2999
constexpr std::uint32_t kTestAreas = 6;
constexpr std::uint32_t kTestBuildings = 40;

std::vector<std::string>* g_debugStrings = nullptr;

void __stdcall RecordDebugString(const char* text) {
    if (g_debugStrings != nullptr) {
        g_debugStrings->emplace_back(text);
    }
}

std::vector<std::uint8_t> Bytes(const void* start, std::size_t size) {
    const auto* p = static_cast<const std::uint8_t*>(start);
    return std::vector<std::uint8_t>(p, p + size);
}

constexpr std::size_t kListsOffset = offsetof(CGameData, player_lists);
constexpr std::size_t kListsBytes = offsetof(CGameData, player_list_lengths) + sizeof(CGameData::player_list_lengths) -
                                    kListsOffset;

// The state the functions change.
struct ArmyBytes {
    std::vector<std::uint8_t> lists, orders, armyState, census;

    static ArmyBytes Capture(CGameData* world) {
        return {Bytes(reinterpret_cast<std::uint8_t*>(world) + kListsOffset, kListsBytes),  // raw-ok: test snapshot
                Bytes(&g_aiGroupOrders.get(), sizeof(AiGroupOrders)),
                Bytes(&g_aiArmyState.get(), sizeof(AiArmyState)), Bytes(&g_aiCensus.get(), sizeof(AiAgentData))};
    }
    void Restore(CGameData* world) const {
        std::memcpy(reinterpret_cast<std::uint8_t*>(world) + kListsOffset, lists.data(), kListsBytes);  // raw-ok
        std::memcpy(&g_aiGroupOrders.get(), orders.data(), orders.size());
        std::memcpy(&g_aiArmyState.get(), armyState.data(), armyState.size());
        std::memcpy(&g_aiCensus.get(), census.data(), census.size());
    }
};

struct SavedArmy {
    ArmyBytes bytes;
    std::vector<std::uint8_t> vectors = Bytes(&g_aiHunterVectors.get(), sizeof(AiVector) * kPlayers);
    CGameData* mapWorld = g_aiMapWorld;
    SavedArmy(CGameData* world) : bytes(ArmyBytes::Capture(world)) {}
    ~SavedArmy() {
        std::memcpy(&g_aiGroupOrders.get(), bytes.orders.data(), bytes.orders.size());
        std::memcpy(&g_aiArmyState.get(), bytes.armyState.data(), bytes.armyState.size());
        std::memcpy(&g_aiCensus.get(), bytes.census.data(), bytes.census.size());
        std::memcpy(&g_aiHunterVectors.get(), vectors.data(), vectors.size());
        g_aiMapWorld = mapWorld;
    }
};

struct GroupFunctions {
    ArmyPlayerFn countSpareGroups, countSpareSoldiers;
    ArmyPlayerGroupFn moveLastGroupInto, assignMage, assignSoldier, returnGroupHome;
    ArmyNoArgFn disbandWeakGroups;
    FindSpareGroupFn findSpareGroup;
    RebuildFn rebuild;
    AssignIdleFn assignIdle;
};

GroupFunctions OriginalGroupFunctions() {
    return {Original_AI_Army_CountSpareGroups, Original_AI_Army_CountSpareSoldiers, Original_AI_Army_MoveLastGroupInto,
            Original_AI_Army_AssignMage,       Original_AI_Army_AssignSoldier,      Original_AI_Army_ReturnGroupHome,
            Original_AI_Army_DisbandWeakGroups, Original_AI_Army_FindSpareGroup,
            Original_AI_Army_RebuildAssignedSettlers, Original_AI_Army_AssignIdleUnits};
}

GroupFunctions OurGroupFunctions() {
    return {&AI_Army_CountSpareGroups, &AI_Army_CountSpareSoldiers, &AI_Army_MoveLastGroupInto,
            &AI_Army_AssignMage,       &AI_Army_AssignSoldier,      &AI_Army_ReturnGroupHome,
            &AI_Army_DisbandWeakGroups, &AI_Army_FindSpareGroup,
            &AI_Army_RebuildAssignedSettlers, &AI_Army_AssignIdleUnits};
}

std::int32_t RandomLength(test::Rng& rng, std::int32_t size) {
    switch (rng.Below(5)) {
    case 0: return 0;
    case 1: return static_cast<std::int32_t>(1 + rng.Below(3));
    case 2: {  // around the group size (lists hold at most 100: longer ones would read other lists)
        const std::int32_t length = size + static_cast<std::int32_t>(rng.Below(5)) - 2;
        return length < 0 ? 0 : length > 100 ? 100 : length;
    }
    case 3: return static_cast<std::int32_t>(rng.Below(101));
    default: return static_cast<std::int32_t>(rng.Below(12));
    }
}

// A random army: groups with lengths around the group size, members drawn
// from a pool of settlers with soldier types, few home areas (so groups
// pair up), small targets, flags and bitset words.
// The hunter vectors' storage during the tests ({hunter, target} pairs).
constexpr std::uint32_t kHuntersPerPlayer = 6;
std::uint32_t g_testHunters[kPlayers][kHuntersPerPlayer][2];

void SetUpArmy(test::Rng& rng, CGameData* world) {
    AiGroupOrders& orders = g_aiGroupOrders.get();
    AiVector* hunters = g_aiHunterVectors;
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        const std::uint32_t count = rng.Chance(1, 3) ? 0 : rng.Below(kHuntersPerPlayer + 1);
        for (std::uint32_t k = 0; k < kHuntersPerPlayer; ++k) {
            g_testHunters[p][k][0] = 1 + rng.Below(kTestSettlers - 1);
            g_testHunters[p][k][1] = rng.Below(kTestSettlers);
        }
        hunters[p].begin = reinterpret_cast<std::uint32_t>(&g_testHunters[p][0][0]);
        hunters[p].end = hunters[p].begin + count * 8;
    }
    rng.Fill(&orders, sizeof(orders));
    rng.Fill(&g_aiArmyState.get(), sizeof(AiArmyState));
    AiAgentData& census = g_aiCensus.get();
    rng.Fill(&census, sizeof(census));
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        world->players[p].active = static_cast<std::int32_t>(rng.Below(3));
        const std::int32_t size = rng.Chance(1, 10) ? static_cast<std::int32_t>(rng.Next32() % 200) - 50
                                                    : static_cast<std::int32_t>(rng.Below(106));
        orders.group_size[p] = size;
        orders.highest_group[p] = static_cast<std::int32_t>(rng.Below(10));
        for (std::uint32_t g = 0; g < kUnitGroups; ++g) {
            const std::int32_t length = RandomLength(rng, size);
            world->player_list_lengths[p][g] = length;
            for (std::uint32_t k = 0; k < kGroupSlots; ++k) {
                world->player_lists[p][g][k] = static_cast<std::uint16_t>(1 + rng.Below(kTestSettlers - 1));
            }
            orders.home_areas[p][g] = static_cast<std::uint16_t>(rng.Below(3));
            orders.group_mages[p][g] = static_cast<std::int32_t>(rng.Below(rng.Chance(1, 8) ? 200 : 4)) -
                                       (rng.Chance(1, 10) ? 50 : 0);
            census.group_target_buildings[p][g] = static_cast<std::uint16_t>(rng.Chance(1, 2) ? 0 : rng.Next32());
        }
    }
    for (std::uint32_t settler = 1; settler < kTestSettlers; ++settler) {
        layout::Settler& record = world->settlers[settler];
        record.art = static_cast<std::uint8_t>(rng.Chance(1, 2) ? rng.Below(48) : 5 + rng.Below(32));
        record.x = static_cast<std::uint16_t>(rng.Below(64));
        record.y = static_cast<std::uint16_t>(rng.Below(64));
        // chains of the settler lists: a later settler or the end
        record.next = static_cast<std::uint16_t>(rng.Chance(1, 4) || settler + 40 >= kTestSettlers
                                                     ? 0
                                                     : settler + 1 + rng.Below(40));
    }
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        for (std::uint16_t& head : world->players[p].settler_lists) {
            head = static_cast<std::uint16_t>(rng.Chance(1, 3) ? 0 : 1 + rng.Below(kTestSettlers - 1));
        }
    }
    if (rng.Chance(2, 3)) {  // sparse groups make "no melee soldier" likely
        for (std::uint32_t settler = 1; settler < kTestSettlers; ++settler) {
            if (rng.Chance(9, 10)) {
                world->settlers[settler].art = Value(SettlerType::Bowman1);
            }
        }
    }
    // Areas and buildings for AssignSoldier's search.
    world->area_high_water = rng.Chance(1, 10) ? 0xffffffffu : rng.Below(kTestAreas);
    for (std::uint32_t area = 0; area < kTestAreas; ++area) {
        world->areas[area].player = static_cast<std::uint8_t>(rng.Chance(1, 6) ? 0xff : rng.Below(3));
    }
    for (std::uint32_t x = 0; x < 64; ++x) {
        for (std::uint32_t y = 0; y < 64; ++y) {
            world->tiles[x][y].area = static_cast<std::uint16_t>(rng.Below(kTestAreas));
        }
    }
    world->building_high_water = rng.Below(kTestBuildings);
    constexpr std::uint8_t kTypes[] = {15, 16, 18, 1, 7, 20};
    for (std::uint32_t building = 1; building < kTestBuildings; ++building) {
        layout::Building& record = world->buildings[building];
        record.owner = static_cast<std::uint8_t>(rng.Chance(1, 8) ? 0xff : rng.Below(3));
        record.art = kTypes[rng.Below(sizeof(kTypes))];
        record.buildup = static_cast<std::uint8_t>(rng.Chance(1, 4) ? 1 + rng.Below(200) : 0);
        record.x = static_cast<std::uint16_t>(rng.Below(64));
        record.y = static_cast<std::uint16_t>(rng.Below(64));
        record.door_x = static_cast<std::uint16_t>(rng.Below(64));
        record.door_y = static_cast<std::uint16_t>(rng.Below(64));
    }
}

void CompareGroupFunctions(test::Context& ctx, const GroupFunctions& candidate, int cases) {
    const GroupFunctions original = OriginalGroupFunctions();
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    SavedArmy saved(world);
    g_aiMapWorld = world;
    AiGroupOrders* self = &g_aiGroupOrders.get();

    std::int32_t bowmen = 0;
    std::uint32_t sendResult = 0;
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    stubs.Install<4, kCountEnemyBowmen>(
        [&](CGameData*, std::uint32_t, const std::uint32_t*) { return static_cast<std::uint32_t>(bowmen); });
    stubs.Install<4, kSendMoveGroup>([&](CGameData*, std::uint32_t, const std::uint32_t*) { return sendResult; });
    const auto recorder = reinterpret_cast<std::uint32_t>(&RecordDebugString);
    test::PatchBytes(reinterpret_cast<std::uintptr_t>(&g_importOutputDebugStringA.get()), &recorder, sizeof(recorder));

    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        SetUpArmy(rng, world);
        const int op = static_cast<int>(rng.Below(10));
        const std::uint32_t player = rng.Below(kPlayers);
        const std::uint32_t group = rng.Below(static_cast<std::uint32_t>(kUnitGroups));
        const std::uint32_t settler = 1 + rng.Below(kTestSettlers - 1);
        if (op == 2 && rng.Chance(1, 2)) {  // MoveLastGroupInto: often the highest group itself
            g_aiGroupOrders.get().highest_group[player] = static_cast<std::int32_t>(group);
        }
        if ((op == 3 || op == 4) && rng.Chance(1, 4)) {  // already assigned
            OrderWord(g_aiGroupOrders.get().settlers_with_orders, settler) |= OrderMask(settler);
        }
        bowmen = rng.Chance(1, 3) ? static_cast<std::int32_t>(rng.Below(3)) - 1 : 0;
        sendResult = rng.Next32();
        const std::uint32_t building = rng.Chance(1, 3) ? 0 : rng.Below(kTestBuildings);
        const auto anyDelay = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : 1 + rng.Below(255));
        if (op == 7 && rng.Chance(1, 2)) {  // FindSpareGroup: some groups ready (delay 0)
            for (std::uint32_t& delay : g_aiArmyState.get().attack_delays[player]) {
                delay = rng.Chance(1, 2) ? 0 : delay;
            }
            for (std::uint16_t& target : g_aiCensus.get().group_target_buildings[player]) {
                target = rng.Chance(2, 3) ? 0 : target;
            }
        }
        const ArmyBytes start = ArmyBytes::Capture(world);
        auto run = [&](const GroupFunctions& f, std::vector<std::string>& strings) {
            start.Restore(world);
            g_debugStrings = &strings;
            std::uint32_t result = 0;
            switch (op) {
            case 0: result = f.countSpareGroups(self, nullptr, player); break;
            case 1: result = f.countSpareSoldiers(self, nullptr, player); break;
            case 2: result = f.moveLastGroupInto(self, nullptr, player, group); break;
            case 3: result = f.assignMage(self, nullptr, player, settler); break;
            case 4: result = f.assignSoldier(self, nullptr, player, settler); break;
            case 5: result = f.returnGroupHome(self, nullptr, player, group); break;
            case 6: result = f.disbandWeakGroups(self, nullptr); break;
            case 7: result = static_cast<std::uint32_t>(f.findSpareGroup(self, nullptr, player, building, anyDelay)); break;
            case 8: f.rebuild(self, nullptr); break;
            default: f.assignIdle(self, nullptr, player); break;
            }
            g_debugStrings = nullptr;
            return result;
        };
        std::vector<std::string> stringsA, stringsB;
        const std::uint32_t ra = run(original, stringsA);
        const ArmyBytes afterA = ArmyBytes::Capture(world);
        const std::vector<test::StubCall> callsA = stubs.Take();
        const std::uint32_t rb = run(candidate, stringsB);
        const ArmyBytes afterB = ArmyBytes::Capture(world);
        const std::vector<test::StubCall> callsB = stubs.Take();
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT_MEM_EQ(afterA.lists.data(), afterB.lists.data(), afterA.lists.size());
        same &= S3RE_EXPECT_MEM_EQ(afterA.orders.data(), afterB.orders.data(), afterA.orders.size());
        same &= S3RE_EXPECT_MEM_EQ(afterA.armyState.data(), afterB.armyState.data(), afterA.armyState.size());
        same &= S3RE_EXPECT_MEM_EQ(afterA.census.data(), afterB.census.data(), afterA.census.size());
        same &= S3RE_EXPECT(callsA == callsB);
        same &= S3RE_EXPECT(stringsA == stringsB);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d, player %u, group %u, settler %u", i, op, player, group,
                     settler);
        }
    }
    test::UnpatchBytes(reinterpret_cast<std::uintptr_t>(&g_importOutputDebugStringA.get()));
}

std::uint32_t __fastcall WrongCountSpareGroups(AiGroupOrders* self, void* /*edx*/, std::uint32_t player) {
    // Wrong: every group sharing its home area counts, not every second one.
    CGameData* world = g_aiMapWorld;
    const std::int32_t half = self->group_size[player] / 2;
    std::uint32_t count = 0;
    for (std::uint32_t g = kFirstGroup; g <= kLastGroup; ++g) {
        const std::uint32_t index = player * kUnitGroups + g;
        bool shared = false;
        for (std::uint32_t other = kFirstGroup; other < g; ++other) {
            shared |= ListLength(world, player * kUnitGroups + other) != 0 &&
                      self->home_areas[player][other] == self->home_areas[player][g];
        }
        if (shared && ListLength(world, index) != 0 && TargetBuilding(index) == 0 && ListLength(world, index) >= half) {
            ++count;
        }
    }
    return count;
}

std::uint32_t __fastcall WrongMoveLastGroupInto(AiGroupOrders* self, void* edx, std::uint32_t player,
                                                std::uint32_t group) {
    // Wrong: also moves the home area.
    const std::int32_t last = self->highest_group[player];
    if (static_cast<std::uint32_t>(last) != group && last >= 0 && last < 10 && group < 10) {
        self->home_areas[player][group] = self->home_areas[player][last];
    }
    return AI_Army_MoveLastGroupInto(self, edx, player, group);
}

std::uint32_t __fastcall WrongAssignSoldier(AiGroupOrders* self, void* edx, std::uint32_t player,
                                            std::uint32_t soldier) {
    // Wrong: a new group's home area is not set.
    std::uint16_t homes[kUnitGroups];
    std::memcpy(homes, self->home_areas[player], sizeof(homes));
    const std::uint32_t result = AI_Army_AssignSoldier(self, edx, player, soldier);
    std::memcpy(self->home_areas[player], homes, sizeof(homes));
    return result;
}

std::uint32_t __fastcall WrongDisbandWeakGroups(AiGroupOrders* self, void* /*edx*/) {
    // Wrong: bowmen count as melee soldiers.
    for (std::uint32_t player = 0; player < kPlayers; ++player) {
        if (g_aiMapWorld.get()->players[player].active != kComputerControlled) {
            continue;
        }
        for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
            const std::uint32_t index = player * kUnitGroups + group;
            const auto length = static_cast<std::uint32_t>(ListLength(g_aiMapWorld, index));
            if (length <= static_cast<std::uint32_t>(self->group_size[player] / 4) && length != 0 &&
                TargetBuilding(index) == 0) {
                DisbandGroup(player, group);
            }
            CGameData* world = g_aiMapWorld;
            const auto members = static_cast<std::uint32_t>(ListLength(world, index));
            bool melee = false;
            for (std::uint32_t k = 0; k < members && !melee; ++k) {
                const std::uint8_t type = world->settlers[ListMembers(world, index)[k]].art;
                melee = IsMeleeSoldier(type) || type == Value(SettlerType::Bowman1);
            }
            if (members != 0 && !melee) {
                DisbandGroup(player, group);
            }
        }
    }
    return reinterpret_cast<std::uint32_t>(&self->gather_positions);
}

std::uint32_t __fastcall WrongReturnGroupHome(AiGroupOrders* self, void* edx, std::uint32_t player,
                                              std::uint32_t group) {
    // Wrong: sends a group of one member too.
    const std::uint32_t index = player * kUnitGroups + group;
    std::int32_t& length = ListLength(g_aiMapWorld, index);
    if (length == 1) {
        length = 2;
        const std::uint32_t result = AI_Army_ReturnGroupHome(self, edx, player, group);
        length = 1;
        return result;
    }
    return AI_Army_ReturnGroupHome(self, edx, player, group);
}

std::int32_t __fastcall WrongFindSpareGroup(AiGroupOrders* self, void* edx, std::uint32_t player,
                                            std::uint32_t building, std::uint8_t anyDelay) {
    // Wrong: the building's y instead of x twice ("fixes" the quirk the other way).
    const std::uint32_t saved = g_aiMapWorld.get()->buildings[building].x;
    g_aiMapWorld.get()->buildings[building].x = g_aiMapWorld.get()->buildings[building].y;
    const std::int32_t result = AI_Army_FindSpareGroup(self, edx, player, building, anyDelay);
    g_aiMapWorld.get()->buildings[building].x = static_cast<std::uint16_t>(saved);
    return result;
}

void __fastcall WrongRebuild(AiGroupOrders* self, void* edx) {
    // Wrong: forgets the hunters.
    AiVector* hunters = g_aiHunterVectors;
    std::uint32_t ends[kPlayers];
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        ends[p] = hunters[p].end;
        hunters[p].end = hunters[p].begin;
    }
    AI_Army_RebuildAssignedSettlers(self, edx);
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        hunters[p].end = ends[p];
    }
}

void __fastcall WrongAssignIdle(AiGroupOrders* self, void* edx, std::uint32_t player) {
    // Wrong: the mages first.
    layout::Player& record = g_aiMapWorld.get()->players[player];
    std::uint16_t saved[std::size(record.settler_lists)];
    std::memcpy(saved, record.settler_lists, sizeof(saved));
    std::memset(record.settler_lists, 0, sizeof(saved));
    record.settler_lists[Value(SettlerType::Mage)] = saved[Value(SettlerType::Mage)];
    AI_Army_AssignIdleUnits(self, edx, player);
    std::memcpy(record.settler_lists, saved, sizeof(saved));
    record.settler_lists[Value(SettlerType::Mage)] = 0;
    AI_Army_AssignIdleUnits(self, edx, player);
    record.settler_lists[Value(SettlerType::Mage)] = saved[Value(SettlerType::Mage)];
}

}  // namespace

S3RE_TEST(AiArmyGroups_MatchOriginal) {
    CompareGroupFunctions(ctx, OurGroupFunctions(), 4000);
}

S3RE_TEST(AiArmyGroups_ComparisonDetectsWrongImplementation) {
    const auto probe = [&](auto change) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        GroupFunctions wrong = OurGroupFunctions();
        change(wrong);
        CompareGroupFunctions(quiet, wrong, 3000);
        S3RE_EXPECT(quiet.failures() > 0);
    };
    probe([](GroupFunctions& f) { f.countSpareGroups = &WrongCountSpareGroups; });
    probe([](GroupFunctions& f) { f.moveLastGroupInto = &WrongMoveLastGroupInto; });
    probe([](GroupFunctions& f) { f.assignSoldier = &WrongAssignSoldier; });
    probe([](GroupFunctions& f) { f.disbandWeakGroups = &WrongDisbandWeakGroups; });
    probe([](GroupFunctions& f) { f.returnGroupHome = &WrongReturnGroupHome; });
    probe([](GroupFunctions& f) { f.findSpareGroup = &WrongFindSpareGroup; });
    probe([](GroupFunctions& f) { f.rebuild = &WrongRebuild; });
    probe([](GroupFunctions& f) { f.assignIdle = &WrongAssignIdle; });
}

// The constructors: on scratch copies (the army's on a buffer, with the
// hunter vectors its Reset clears saved; ArmyState's on a buffer).
S3RE_TEST(AiArmyConstructors_MatchOriginal) {
    std::vector<std::uint8_t> vectorsStart(sizeof(AiVector) * kPlayers);
    const std::vector<std::uint8_t> savedVectors = Bytes(&g_aiHunterVectors.get(), vectorsStart.size());
    const std::vector<std::uint8_t> savedMarker = Bytes(&g_aiPerPlayer7e79d0.get(), sizeof(std::uint32_t[20]));
    for (int i = 0; i < 40 && ctx.failures() == 0; ++i) {
        std::vector<std::uint8_t> a(sizeof(AiGroupOrders) + 32), b;
        ctx.rng().Fill(a.data(), a.size());
        b = a;
        ctx.rng().Fill(vectorsStart.data(), vectorsStart.size());
        std::memcpy(&g_aiHunterVectors.get(), vectorsStart.data(), vectorsStart.size());
        auto* ra = Original_AI_Army_Army(reinterpret_cast<AiGroupOrders*>(a.data()), nullptr);
        const std::vector<std::uint8_t> vectorsA = Bytes(&g_aiHunterVectors.get(), vectorsStart.size());
        std::memcpy(&g_aiHunterVectors.get(), vectorsStart.data(), vectorsStart.size());
        auto* rb = AI_Army_Army(reinterpret_cast<AiGroupOrders*>(b.data()), nullptr);
        bool same = S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size());
        same &= S3RE_EXPECT(vectorsA == Bytes(&g_aiHunterVectors.get(), vectorsStart.size()));
        same &= S3RE_EXPECT_EQ(reinterpret_cast<std::uint8_t*>(ra) - a.data(),
                               reinterpret_cast<std::uint8_t*>(rb) - b.data());

        std::vector<std::uint8_t> c(sizeof(AiArmyState) + 32), d;
        ctx.rng().Fill(c.data(), c.size());
        d = c;
        auto* rc = Original_AI_ArmyState_ArmyState(reinterpret_cast<AiArmyState*>(c.data()), nullptr);
        auto* rd = AI_ArmyState_ArmyState(reinterpret_cast<AiArmyState*>(d.data()), nullptr);
        same &= S3RE_EXPECT_MEM_EQ(c.data(), d.data(), c.size());
        same &= S3RE_EXPECT_EQ(reinterpret_cast<std::uint8_t*>(rc) - c.data(),
                               reinterpret_cast<std::uint8_t*>(rd) - d.data());
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
        }
    }
    std::memcpy(&g_aiHunterVectors.get(), savedVectors.data(), savedVectors.size());
    std::memcpy(&g_aiPerPlayer7e79d0.get(), savedMarker.data(), savedMarker.size());
}

}  // namespace s3re::game
