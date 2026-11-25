// The computer players' military AI, its entry (Rainer\CompMapWorkerAgent.cpp;
// re/computer_players.md "Army"). T_ATTACK_AGENT::Run calls RunMilitary
// once per turn of a computer player; each call does one task, chosen by
// step counters (AiArmyState, g_aiCatapultHuntSteps).
//
//   0x0055ec10 T_ATTACK_AGENT::Run() thiscall (agent slot 8, `this`
//              unused): RunMilitary(g_aiWorld, g_aiPlayerNumber,
//              g_aiPlayer->message_queue, the reset byte 0x7e3410), then
//              the reset byte is cleared. The original wraps the call in
//              catch (...) { DebugLog("Marcus: FOE ATTACK EXCEPTION!");
//              throw; } (a C++ handler; VC6's catch (...) also catches
//              structured exceptions): ours logs in an SEH filter and lets
//              the exception go on (the original unwinds RunMilitary's
//              frame before it logs; RunMilitary has no destructors). EAX =
//              RunMilitary's (not used by CComputerPlayer).
//   0x0058f990 AI::RunMilitary(world ECX, player EDX, queue, reset byte)
//              fastcall, ret 8. With the reset byte: unless
//              g_aiSkipMilitaryReset is set (then it is cleared), the
//              army, the army state and the census are reset
//              (0x591e00, 0x593f50, 0x55b770), the battle-spell slots set
//              (0x590870), all hunter and catapult vectors emptied and
//              0x7e7ccc / 0x7e7cd0 cleared. Otherwise g_aiMapWorld = world,
//              g_aiMilitaryQueue = queue, ProcessWorldEvents 0x590da0, then
//              the first counter past its limit does its task and the call
//              ends (each counter counts every call it is reached, and
//              restarts at 0 when it fires): the player's catapult hunt
//              (> 50, AttackEnemyCatapults), thief hunt (> 420), mage hunt
//              (> 550); then the player's military steps (> 150) choose:
//              the attack part (below) or the upkeep: PruneHunters, then
//              the shared disband counter (> 1100, DisbandWeakGroups), the
//              player's spell counter (> 75, CastBattleSpells), the shared
//              rebuild counter (> 2000, or g_aiRebuildSettlersPending:
//              RebuildAssignedSettlers and the flag cleared), the recruit
//              counter (> 2800 and the census settlers counted:
//              RecruitGeologistsAndThieves), the settler census counter
//              (> 1900: CountSettlers; while the pass isn't done the
//              counter becomes 1895 so that the next slice follows), the
//              building census counter (> 700: CountBuildings), else
//              ComputeTeamStrength once both counts and census byte +2 are
//              set (then +2 = 0).
//              The attack part: ratio = (float)(Player::soldier_strength /
//              256) (x87 fild qword, fmul float 1/256, fstp float);
//              TickAttackDelays; the gather countdown: at 0 the gather
//              building is dropped, else the groups with members and a
//              target are sent there again ("Gathering ..."). Only once the
//              census team strengths are valid: CheckGroupTargets(ratio);
//              without a gather building, groups with a target that steal
//              or of which at most 22 % fight (GroupFightingShareAbove) are
//              sent to it again ("Attack or Steal ..."); StartAttack(ratio);
//              the garrison check of the player's finished military
//              buildings (g_aiGarrisonedBuildings): one with a garrison, or
//              soldiers on the way (bytes +0x19 / +0x1b / +0x1f), is
//              marked; for one without, every group of at least two
//              members whose first swordsman or bowman (a member of a
//              group, in list order) is within 64 tiles asks for its
//              garrison (QueueChangeGarrison op 5 for a swordsman, 3 for a
//              bowman) and marks it; one still unmarked logs "Eine Turm ist
//              nicht besetzt" and makes the patrols below prefer it.
//              AssignIdleUnits; groups 1..9 with members and no target:
//              patrol point 0 without point 1 is renewed (sent there when
//              no enemy bowmen are within 30 of the first member,
//              CountEnemyBowmenOnTerritoryNear 0x58a610), otherwise without
//              a prey the group returns home (ReturnGroupHome);
//              SendThieves; SendGeologists. Then the patrols of groups
//              1..highest group (re-read) with members, no target and no
//              delay, by the group's step counter (AiGroupOrders +0x1c48):
//              while it runs, the group goes (bowmen check) to patrol point
//              0 or 1 by its two low bits (logged "Next Step ...";
//              original quirk, kept: the point-1 text names point 0) and the
//              counter goes down, clearing both points when it reaches 0;
//              at 0 the group's first member's tile area 0 sends it home
//              (delay + 35 - (r & 7)) and ends the call; a missing point 0
//              is set (the free interior building near an unmanned tower,
//              else for groups above 1 on a Random draw below 5000
//              FreeInteriorMilitaryBuildingNear_AlwaysNone, else
//              NearestFreeInteriorMilitaryBuilding) and, when set, the delay
//              grows (500 - r % 125 for a tower, else v - r2 % (v / 4) with
//              v = 10 + r1 % 16) and the group goes there unless enemy
//              bowmen are near ("New Patrouilie point 0 ..."), ending the
//              call; a missing point 1 is set to the nearest free interior
//              building and, when found, the counter becomes 5 + Random % 4
//              (the only unlogged draw). EAX is not used by the caller.

#include "game/ai/ai_layout.h"
#include "game/enums/buildings.h"
#include "game/enums/settler_types.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/texts.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace s3re::game {

using namespace ai;
using layout::kPlayers;

namespace {

constexpr std::uint32_t kGroupSlots = 100;
constexpr std::uint32_t kFirstGroup = 1;
constexpr std::uint32_t kLastGroup = 9;
constexpr std::uint32_t kGarrisonBits = 4000;
constexpr std::size_t kTextBytes = 256;

// The counters' limits (the task runs when the old value is above them).
constexpr std::int32_t kCatapultHuntSteps = 50;
constexpr std::int32_t kThiefHuntSteps = 420;
constexpr std::int32_t kMageHuntSteps = 550;
constexpr std::int32_t kMilitarySteps = 150;
constexpr std::int32_t kDisbandSteps = 1100;
constexpr std::int32_t kSpellSteps = 75;
constexpr std::int32_t kRebuildSteps = 2000;
constexpr std::int32_t kRecruitSteps = 2800;
constexpr std::int32_t kSettlerCensusSteps = 1900;
constexpr std::uint32_t kSettlerCensusNextSlice = 1895;  // the counter while a census pass is not done
constexpr std::int32_t kBuildingCensusSteps = 700;

constexpr float kStrengthScale = 0.00390625f;   // 0x65a8dc: 1 / 256
constexpr std::int32_t kFightingPercent = 22;   // GroupFightingShareAbove: a group fights when more do
constexpr std::uint32_t kGarrisonReach = 65;    // a soldier below this distance asks for the garrison
constexpr std::uint32_t kSwordsmanGarrison = 5;  // QueueChangeGarrison op for a swordsman
constexpr std::uint32_t kBowmanGarrison = 3;
constexpr std::uint8_t kMilitarySite = 3;       // AI::SiteKindOf
constexpr std::uint32_t kBowmenDistance = 30;
constexpr std::uint16_t kPatrolSkipDraw = 5000;
constexpr std::uint32_t kStrayDelay = 35;       // - (r & 7)
constexpr std::uint32_t kTowerDelay = 500;      // - r % 125
constexpr std::uint32_t kTowerDelaySpread = 125;
constexpr std::int32_t kPatrolDelay = 10;       // + r1 % 16
constexpr std::uint8_t kPatrolSteps = 5;        // + r % 4

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kArmyReset = 0x00591e00;
constexpr std::uintptr_t kArmyStateReset = 0x00593f50;
constexpr std::uintptr_t kResetAgentData = 0x0055b770;
constexpr std::uintptr_t kInitBattleSpellSlots = 0x00590870;
constexpr std::uintptr_t kProcessWorldEvents = 0x00590da0;
constexpr std::uintptr_t kAttackEnemyCatapults = 0x00593180;
constexpr std::uintptr_t kHuntEnemyThieves = 0x00593ad0;
constexpr std::uintptr_t kHuntEnemyMages = 0x00593bd0;
constexpr std::uintptr_t kTickAttackDelays = 0x00594000;
constexpr std::uintptr_t kCheckGroupTargets = 0x00594a10;
constexpr std::uintptr_t kGroupFightingShareAbove = 0x00593040;
constexpr std::uintptr_t kStartAttack = 0x00594080;
constexpr std::uintptr_t kSiteKindOf = 0x0058ed70;
constexpr std::uintptr_t kGarrisonXran = 0x0058b520;
constexpr std::uintptr_t kQueueChangeGarrison = 0x00590bb0;
constexpr std::uintptr_t kSendMoveGroup = 0x00590c80;
constexpr std::uintptr_t kAssignIdleUnits = 0x00593cd0;
constexpr std::uintptr_t kCountEnemyBowmen = 0x0058a610;
constexpr std::uintptr_t kReturnGroupHome = 0x00592620;
constexpr std::uintptr_t kSendThieves = 0x00594e70;
constexpr std::uintptr_t kSendGeologists = 0x00595050;
constexpr std::uintptr_t kFirstUnassigned = 0x0058ae90;
constexpr std::uintptr_t kNearestFree = 0x0058aa70;
constexpr std::uintptr_t kAlwaysNone = 0x0058af60;
constexpr std::uintptr_t kPruneHunters = 0x005934a0;
constexpr std::uintptr_t kDisbandWeakGroups = 0x00592bf0;
constexpr std::uintptr_t kCastBattleSpells = 0x005953b0;
constexpr std::uintptr_t kRebuildAssignedSettlers = 0x00591c40;
constexpr std::uintptr_t kRecruit = 0x00592e60;
constexpr std::uintptr_t kCountSettlers = 0x0055b880;
constexpr std::uintptr_t kCountBuildings = 0x0055be00;
constexpr std::uintptr_t kComputeTeamStrength = 0x0055bea0;
constexpr std::uintptr_t kRandom = 0x004feb70;
constexpr std::uintptr_t kTileDistance = 0x004ff320;
constexpr std::uintptr_t kSprintf = 0x0061f27e;
constexpr std::uintptr_t kDebugLog = 0x00428720;
constexpr std::uintptr_t kRunMilitary = 0x0058f990;

using ThisFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using ThisPlayerFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t player);
using PlayerFn = std::uint32_t(__fastcall*)(std::uint32_t player, void* edx);

void* Army() { return &g_aiGroupOrders.get(); }
void* Census() { return &g_aiCensus.get(); }
void* EnemySearch() { return &g_aiEnemySearch.get(); }
void* Sender() { return &g_aiCommandSender.get(); }

std::uint32_t Call(std::uintptr_t function, void* self) { return reinterpret_cast<ThisFn>(function)(self, nullptr); }

std::uint32_t Call(std::uintptr_t function, void* self, std::uint32_t player) {
    return reinterpret_cast<ThisPlayerFn>(function)(self, nullptr, player);
}

std::uint32_t Random() {
    using Fn = std::uint32_t(__fastcall*)(CGameData* world, void* edx);
    return reinterpret_cast<Fn>(kRandom)(g_aiMapWorld, nullptr);
}

template <typename... Args>
void Format(char* text, const GameText& format, Args... args) {
    using Fn = int(__cdecl*)(char* buffer, const char* format, ...);
    reinterpret_cast<Fn>(kSprintf)(text, format.c_str(), args...);
}

template <typename... Args>
void LogDebugString(const GameText& format, Args... args) {
    char text[kTextBytes];
    Format(text, format, args...);
    g_importOutputDebugStringA.get()(text);
}

std::uint32_t LoggedRandom() {
    const std::uint32_t draw = Random() & 0xffff;
    char text[kTextBytes];
    Format(text, texts::kRandom55, draw);
    using DebugLogFn = void(__cdecl*)(const char* text);
    reinterpret_cast<DebugLogFn>(kDebugLog)(text);
    return draw;
}

const layout::Building& BuildingAt(std::uint32_t id) { return (&g_aiMapWorld.get()->buildings[0])[id]; }
const layout::Settler& SettlerAt(std::uint32_t id) { return (&g_aiMapWorld.get()->settlers[0])[id]; }
std::int32_t ListLength(std::uint32_t index) { return (&g_aiMapWorld.get()->player_list_lengths[0][0])[index]; }
std::uint16_t Member(std::uint32_t index, std::uint32_t k) {
    return (&g_aiMapWorld.get()->player_lists[0][0][0])[index * kGroupSlots + k];
}
std::uint16_t& CensusTarget(std::uint32_t index) { return (&g_aiCensus.get().group_target_buildings[0][0])[index]; }
std::uint32_t& AttackDelay(std::uint32_t index) { return (&g_aiArmyState.get().attack_delays[0][0])[index]; }
AiGroupTarget& GroupTarget(std::uint32_t index) { return (&g_aiGroupOrders.get().group_targets[0][0])[index]; }

void SendToBuilding(std::uint32_t player, std::uint32_t group, std::uint32_t building) {
    const layout::Building& record = BuildingAt(building);
    using Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t player, std::uint32_t group,
                                          std::uint32_t x, std::uint32_t y);
    reinterpret_cast<Fn>(kSendMoveGroup)(Sender(), nullptr, player, group, record.x, record.y);
}

std::int32_t EnemyBowmenNear(const layout::Settler& settler, std::uint32_t player) {
    using Fn = std::int32_t(__fastcall*)(void* self, void* edx, std::uint32_t x, std::uint32_t y,
                                         std::uint32_t distance, std::uint32_t player);
    return reinterpret_cast<Fn>(kCountEnemyBowmen)(EnemySearch(), nullptr, settler.x, settler.y, kBowmenDistance,
                                                   player);
}

using BuildingSearchFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t player, std::uint32_t x,
                                                    std::uint32_t y);
std::uint32_t Search(std::uintptr_t function, std::uint32_t player, std::uint32_t x, std::uint32_t y) {
    return reinterpret_cast<BuildingSearchFn>(function)(EnemySearch(), nullptr, player, x, y);
}

// Counts a step counter up; true (and the counter restarted) when its old
// value was above the limit.
bool Fires(std::uint32_t& counter, std::int32_t limit) {
    const auto old = static_cast<std::int32_t>(counter);
    counter = static_cast<std::uint32_t>(old + 1);
    if (old <= limit) {
        return false;
    }
    counter = 0;
    return true;
}

// 0x58f9aa..0x58fa78: the reset request.
void ResetMilitary() {
    if (g_aiSkipMilitaryReset != 0) {
        g_aiSkipMilitaryReset = 0;
        return;
    }
    Call(kArmyReset, Army());
    Call(kArmyStateReset, &g_aiArmyState.get());
    Call(kResetAgentData, Census());
    Call(kInitBattleSpellSlots, Sender());
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        AiVector& hunters = g_aiHunterVectors.get()[p];
        hunters.end = hunters.begin;  // erase(begin, end): nothing moves
        AiVector& catapults = g_aiCatapultVectors.get()[p];
        catapults.end = catapults.begin;
    }
    g_aiUnk7e7ccc = 0;
    g_aiUnk7e7cd0 = 0;
}

// 0x5906ba..0x590866: the upkeep.
void Upkeep(std::uint32_t player) {
    Call(kPruneHunters, Army(), player);
    AiArmyState& state = g_aiArmyState.get();
    if (Fires(state.disband_steps, kDisbandSteps)) {
        Call(kDisbandWeakGroups, Army());
        return;
    }
    if (Fires(state.spell_steps[player], kSpellSteps)) {
        Call(kCastBattleSpells, Army(), player);
        return;
    }
    if (Fires(state.rebuild_steps, kRebuildSteps) || g_aiRebuildSettlersPending != 0) {
        Call(kRebuildAssignedSettlers, Army());
        g_aiRebuildSettlersPending = 0;
        return;
    }
    const auto recruit = static_cast<std::int32_t>(state.recruit_steps);
    state.recruit_steps = static_cast<std::uint32_t>(recruit + 1);
    const std::uint8_t settlersCounted = g_aiCensus.get().settlers_counted;
    if (recruit > kRecruitSteps) {
        state.recruit_steps = 0;
        if (settlersCounted != 0) {
            Call(kRecruit, Army());
            return;
        }
    }
    if (Fires(state.settler_census_steps, kSettlerCensusSteps)) {
        Call(kCountSettlers, Census());
        if (g_aiCensus.get().settlers_counted == 0) {
            state.settler_census_steps = kSettlerCensusNextSlice;
        }
        return;
    }
    if (Fires(state.building_census_steps, kBuildingCensusSteps)) {
        Call(kCountBuildings, Census());
        return;
    }
    AiAgentData& census = g_aiCensus.get();
    if (census.buildings_counted != 0 && settlersCounted != 0 && census.unk002 != 0) {
        Call(kComputeTeamStrength, Census());
        census.unk002 = 0;
    }
}

std::uint32_t& GarrisonWord(std::uint32_t building) {
    return g_aiGarrisonedBuildings.get()[building >> 5];
}

void CheckGarrisonIndex(std::uint32_t building) {
    if (building >= kGarrisonBits) {
        using XranFn = void(__fastcall*)(void* self, void* edx);
        reinterpret_cast<XranFn>(kGarrisonXran)(&g_aiGarrisonedBuildings.get(), nullptr);
    }
}

// 0x58fe2c..0x58ff70: groups with a swordsman or bowman near the building
// ask for its garrison.
void CallGarrison(std::uint32_t player, std::uint32_t building) {
    for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
        const std::uint32_t index = player * kUnitGroups + group;
        const std::int32_t length = ListLength(index);
        if (length < 2) {
            continue;
        }
        // the group's first swordsman or bowman
        std::int32_t found = -1;
        bool bowman = false;
        for (std::uint32_t k = 0; k < static_cast<std::uint32_t>(length); ++k) {
            const std::uint16_t member = Member(index, k);
            const std::uint8_t type = SettlerAt(member).art;
            switch (type) {
            case Value(SettlerType::Swordsman1):
            case Value(SettlerType::Swordsman2):
            case Value(SettlerType::Swordsman3):
                found = member;
                break;
            case Value(SettlerType::Bowman1):
            case Value(SettlerType::Bowman2):
            case Value(SettlerType::Bowman3):
                found = member;
                bowman = true;
                break;
            default:
                continue;
            }
            break;
        }
        if (found < 0) {
            continue;
        }
        const layout::Settler& soldier = SettlerAt(static_cast<std::uint32_t>(found));
        const layout::Building& record = BuildingAt(building);
        using DistanceFn = std::uint32_t(__stdcall*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
        if (reinterpret_cast<DistanceFn>(kTileDistance)(record.x, record.y, soldier.x, soldier.y) >= kGarrisonReach) {
            continue;
        }
        using QueueFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t player, std::uint32_t building,
                                                   std::uint32_t op);
        reinterpret_cast<QueueFn>(kQueueChangeGarrison)(Sender(), nullptr, player, building,
                                                        bowman ? kBowmanGarrison : kSwordsmanGarrison);
        CheckGarrisonIndex(building);
        GarrisonWord(building) |= 1u << (building & 31);
    }
}

// 0x58fd33..0x58ffeb: the garrison check; true if a tower is unmanned.
bool CheckGarrisons(std::uint32_t player) {
    bool unmanned = false;
    for (std::uint32_t id = 1; id < g_aiMapWorld.get()->building_high_water + 1; ++id) {
        const layout::Building& building = BuildingAt(id);
        if (building.owner != player || building.buildup != 0) {
            continue;
        }
        using KindFn = std::uint32_t(__fastcall*)(std::uint32_t type);
        if ((reinterpret_cast<KindFn>(kSiteKindOf)(building.art) & 0xff) != kMilitarySite) {
            continue;
        }
        CheckGarrisonIndex(id);
        const std::uint32_t bit = 1u << (id & 31);
        if ((GarrisonWord(id) & bit) != 0) {
            continue;
        }
        const layout::Building& record = BuildingAt(id);
        if (record.garrison != 0) {
            CheckGarrisonIndex(id);
            GarrisonWord(id) |= bit;
            continue;
        }
        if (record.military.class1_coming != 0 || record.military.class2_coming != 0 ||
            record.military.class3_coming != 0) {
            CheckGarrisonIndex(id);
            GarrisonWord(id) |= bit;
        } else {
            CallGarrison(player, id);
        }
        CheckGarrisonIndex(id);
        if ((GarrisonWord(id) & bit) == 0) {
            unmanned = true;
            g_importOutputDebugStringA.get()(texts::kAiTowerUnoccupied.c_str());
        }
    }
    return unmanned;
}

// 0x58fffc..0x590150: groups without a target renew their patrol point 0
// or go home.
void RenewPatrols(std::uint32_t player) {
    for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
        const std::uint32_t index = player * kUnitGroups + group;
        if (ListLength(index) == 0 || CensusTarget(index) != 0) {
            continue;
        }
        const AiGroupTarget& target = GroupTarget(index);
        if (target.patrol0 != 0 && target.patrol1 == 0) {
            if (EnemyBowmenNear(SettlerAt(Member(index, 0)), player) != 0) {
                continue;
            }
            LogDebugString(texts::kAiRenewPatrolPoint0, player, group, target.patrol0);
            SendToBuilding(player, group, GroupTarget(index).patrol0);
        } else if (target.prey == 0) {
            using HomeFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t player, std::uint32_t group);
            reinterpret_cast<HomeFn>(kReturnGroupHome)(Army(), nullptr, player, group);
        }
    }
}

// 0x590373..0x59046d: a group on patrol takes its next step.
void PatrolStep(std::uint32_t player, std::uint32_t group, std::uint8_t& steps) {
    const std::uint32_t index = player * kUnitGroups + group;
    const AiGroupTarget& target = GroupTarget(index);
    const auto flags = static_cast<std::int32_t>(static_cast<std::int8_t>(steps));
    char text[kTextBytes];
    std::uint32_t destination = 0;
    if ((flags & 1) != 0 && (flags & 2) != 0) {
        destination = target.patrol0;
        Format(text, texts::kAiNextPatrolPoint0Both, player, group, destination);
    } else if ((flags & 1) == 0 && (flags & 2) != 0) {
        destination = target.patrol0;
        Format(text, texts::kAiNextPatrolPoint0, player, group, destination);
    } else {
        destination = target.patrol1;
        Format(text, texts::kAiNextPatrolPoint1, player, group, static_cast<std::uint32_t>(target.patrol0));  // (quirk)
    }
    if (static_cast<std::int32_t>(destination) > 0 && EnemyBowmenNear(SettlerAt(Member(index, 0)), player) == 0) {
        g_importOutputDebugStringA.get()(text);
        SendToBuilding(player, group, destination);
    }
    steps = static_cast<std::uint8_t>(flags - 1);
    if (flags - 1 == 0) {
        GroupTarget(index).patrol0 = 0;
        GroupTarget(index).patrol1 = 0;
    }
}

// 0x59023b..0x590473 (and 0x5904bf, 0x59052d..0x5906b7) for a group whose
// step counter is 0; true if the call ends here.
bool StartPatrol(std::uint32_t player, std::uint32_t group, std::uint32_t towerTarget, std::uint8_t& steps) {
    const std::uint32_t index = player * kUnitGroups + group;
    const layout::Settler& first = SettlerAt(Member(index, 0));
    const std::uint32_t x = first.x;
    const std::uint32_t y = first.y;
    if ((&g_aiMapWorld.get()->tiles[0][0])[x * layout::kMapRowTiles + y].area == 0) {
        // 0x5904bf: off its land, the group goes home
        using HomeFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t player, std::uint32_t group);
        reinterpret_cast<HomeFn>(kReturnGroupHome)(Army(), nullptr, player, group);
        const std::uint32_t draw = LoggedRandom();
        AttackDelay(index) += kStrayDelay - (draw & 7);
        return true;
    }
    AiGroupTarget& target = GroupTarget(index);
    if (target.patrol0 == 0) {
        if (target.patrol1 != 0) {
            return false;
        }
        if (static_cast<std::int32_t>(towerTarget) > 0) {
            target.patrol0 = static_cast<std::uint16_t>(towerTarget);
            g_importOutputDebugStringA.get()(texts::kAiSendTroopToTower.c_str());
        } else if (group > 1 && (Random() & 0xffff) < kPatrolSkipDraw) {
            target.patrol0 = static_cast<std::uint16_t>(Search(kAlwaysNone, player, x, y));
        } else {
            target.patrol0 = static_cast<std::uint16_t>(Search(kNearestFree, player, x, y));
        }
        if (target.patrol0 != 0) {
            // 0x59052d..0x5906b7: the new patrol point, then the call ends
            if (static_cast<std::int32_t>(towerTarget) > 0) {
                const std::uint32_t draw = LoggedRandom();
                AttackDelay(index) += kTowerDelay - draw % kTowerDelaySpread;
            } else {
                const std::int32_t span = static_cast<std::int32_t>((Random() & 0xffff) % 16) + kPatrolDelay;
                const std::uint32_t draw = LoggedRandom();
                AttackDelay(index) +=
                    static_cast<std::uint32_t>(span) - draw % static_cast<std::uint32_t>(span / 4);
            }
            if (EnemyBowmenNear(first, player) != 0) {
                return true;
            }
            LogDebugString(texts::kAiNewPatrolPoint0, player, group, static_cast<std::uint32_t>(GroupTarget(index).patrol0));
            SendToBuilding(player, group, GroupTarget(index).patrol0);
            return true;
        }
    }
    if (target.patrol1 != 0) {
        return false;
    }
    target.patrol1 = static_cast<std::uint16_t>(Search(kNearestFree, player, x, y));
    if (target.patrol1 != 0) {
        steps = static_cast<std::uint8_t>((Random() & 0xffff & 3) + kPatrolSteps);
    }
    return false;
}

// 0x590164..0x5904bc: the patrols of groups 1..highest group.
void Patrol(std::uint32_t player, bool unmanned) {
    AiGroupOrders& orders = g_aiGroupOrders.get();
    for (std::int32_t g = 1; g <= orders.highest_group[player]; ++g) {
        const auto group = static_cast<std::uint32_t>(g);
        const std::uint32_t index = player * kUnitGroups + group;
        if (ListLength(index) == 0 || CensusTarget(index) != 0 || AttackDelay(index) != 0) {
            continue;
        }
        std::uint32_t towerTarget = 0;
        if (unmanned) {
            const auto tower = static_cast<std::int32_t>(Call(kFirstUnassigned, EnemySearch(), player));
            if (tower > 0) {
                const layout::Building& record = BuildingAt(static_cast<std::uint32_t>(tower));
                towerTarget = Search(kNearestFree, player, record.x, record.y);
                g_importOutputDebugStringA.get()(texts::kAiUnoccupiedTowerFound.c_str());
            }
        }
        std::uint8_t& steps = (&orders.patrol_steps[0][0])[index];
        if (steps != 0) {
            PatrolStep(player, group, steps);
        } else if (StartPatrol(player, group, towerTarget, steps)) {
            return;
        }
    }
}

// 0x58fb57..0x5904bc: the attack part.
void AttackPart(std::uint32_t player) {
    AiArmyState& state = g_aiArmyState.get();
    state.military_steps[player] = 0;
    // x87: fild qword (the strength as unsigned), fmul float, fstp float
    const auto ratio = static_cast<float>(static_cast<double>(g_aiMapWorld.get()->players[player].soldier_strength) *
                                          static_cast<double>(kStrengthScale));
    Call(kTickAttackDelays, &state);
    AiGroupOrders& orders = g_aiGroupOrders.get();
    std::uint32_t& countdown = state.gather_countdowns[player];
    if (static_cast<std::int32_t>(countdown) > 0) {
        --countdown;
    }
    if (countdown == 0) {
        orders.gather_buildings[player] = 0;
    } else if (static_cast<std::int32_t>(orders.gather_buildings[player]) > 0) {
        for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
            const std::uint32_t index = player * kUnitGroups + group;
            if (ListLength(index) == 0 || CensusTarget(index) == 0) {
                continue;
            }
            LogDebugString(texts::kAiGathering, player, group, orders.gather_buildings[player]);
            SendToBuilding(player, group, orders.gather_buildings[player]);
        }
    }
    if (g_aiCensus.get().team_strength_valid == 0) {
        return;
    }
    using RatioFn = void(__fastcall*)(std::uint32_t player, void* edx, float ratio);
    reinterpret_cast<RatioFn>(kCheckGroupTargets)(player, nullptr, ratio);
    if (orders.gather_buildings[player] == 0) {
        for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
            const std::uint32_t index = player * kUnitGroups + group;
            const std::uint32_t target = CensusTarget(index);
            if (target == 0) {
                continue;
            }
            if ((&orders.group_flags[0][0])[index] == 0) {
                using ShareFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t player,
                                                           std::uint32_t group, std::int32_t percent);
                if ((reinterpret_cast<ShareFn>(kGroupFightingShareAbove)(Army(), nullptr, player, group,
                                                                        kFightingPercent) &
                     0xff) != 0) {
                    continue;
                }
            }
            LogDebugString(texts::kAiAttackOrSteal, player, group, target);
            SendToBuilding(player, group, target);
        }
    }
    reinterpret_cast<RatioFn>(kStartAttack)(player, nullptr, ratio);
    const bool unmanned = CheckGarrisons(player);
    Call(kAssignIdleUnits, Army(), player);
    RenewPatrols(player);
    reinterpret_cast<PlayerFn>(kSendThieves)(player, nullptr);
    reinterpret_cast<PlayerFn>(kSendGeologists)(player, nullptr);
    Patrol(player, unmanned);
}

}  // namespace

using RunMilitaryFn = std::uint32_t(__fastcall*)(CGameData* world, std::uint32_t player, std::uint32_t queue,
                                                 std::uint32_t reset);
using AgentRunFn = std::uint32_t(__fastcall*)(void* self, void* edx);

std::uint32_t __fastcall AI_RunMilitary(CGameData* world, std::uint32_t player, std::uint32_t queue,
                                        std::uint32_t reset);
std::uint32_t __fastcall T_ATTACK_AGENT_Run(void* self, void* edx);

S3RE_HOOK(0x0058f990, AI_RunMilitary, RunMilitaryFn, Original_AI_RunMilitary, AI_RunMilitary);
S3RE_HOOK(0x0055ec10, T_ATTACK_AGENT_Run, AgentRunFn, Original_T_ATTACK_AGENT_Run, T_ATTACK_AGENT_Run);

// Purpose: The computer players' military turn: reacts to the world's
// events, then does one military task (hunts, attacks, garrisons, patrols,
// or upkeep such as counting the settlers), chosen by step counters.
std::uint32_t __fastcall AI_RunMilitary(CGameData* world, std::uint32_t player, std::uint32_t queue,
                                        std::uint32_t reset) {
    if ((reset & 0xff) != 0) {
        ResetMilitary();
        return 0;
    }
    g_aiMapWorld = world;
    g_aiMilitaryQueue = queue;
    Call(kProcessWorldEvents, Sender());
    AiArmyState& state = g_aiArmyState.get();
    if (Fires(g_aiCatapultHuntSteps.get()[player], kCatapultHuntSteps)) {
        Call(kAttackEnemyCatapults, Army(), player);
        return 0;
    }
    if (Fires(state.thief_hunt_steps[player], kThiefHuntSteps)) {
        Call(kHuntEnemyThieves, Army(), player);
        return 0;
    }
    if (Fires(state.mage_hunt_steps[player], kMageHuntSteps)) {
        Call(kHuntEnemyMages, Army(), player);
        return 0;
    }
    const auto steps = static_cast<std::int32_t>(state.military_steps[player]);
    state.military_steps[player] = static_cast<std::uint32_t>(steps + 1);
    if (steps > kMilitarySteps) {
        AttackPart(player);
    } else {
        Upkeep(player);
    }
    return 0;
}

namespace {

// T_ATTACK_AGENT::Run's catch (...): log, then let the exception go on.
int LogFoeAttackException() {
    using DebugLogFn = void(__cdecl*)(const char* text);
    reinterpret_cast<DebugLogFn>(kDebugLog)(texts::kAiFoeAttackException.c_str());
    return EXCEPTION_CONTINUE_SEARCH;
}

std::uint32_t RunMilitaryGuarded(std::uint32_t queue, std::uint32_t reset) {
    __try {
        return reinterpret_cast<RunMilitaryFn>(kRunMilitary)(g_aiWorld, g_aiPlayerNumber, queue, reset);
    } __except (LogFoeAttackException()) {
        return 0;  // (not reached: the filter passes the exception on)
    }
}

}  // namespace

// Purpose: The attack agent's turn: runs the military AI for the current
// computer player.
std::uint32_t __fastcall T_ATTACK_AGENT_Run(void* /*self*/, void* /*edx*/) {
    const std::uint32_t result = RunMilitaryGuarded(g_aiPlayer.get()->message_queue, g_aiMilitaryResetRequest);
    g_aiMilitaryResetRequest = 0;
    return result;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// RunMilitary on a random test world and random military state (counters
// near their limits, so that every task comes up), players 0..3 playing.
// The command senders and the enemy counts are recording stubs,
// OutputDebugStringA's import and DebugLog recorders; everything else runs
// (the callees are verified). The hunter and catapult vectors live in
// fixed buffers with room to spare (nothing reallocates), so their
// addresses (logged by ProcessWorldEvents) are the same in both runs.
// T_ATTACK_AGENT::Run is compared the same way (its call goes to our hook).

namespace {

constexpr std::uint32_t kTestSettlers = 400;
constexpr std::uint32_t kTestBuildings = 30;
constexpr std::uint32_t kTestMap = 120;
constexpr std::uint32_t kTestAreas = 6;
constexpr std::uint32_t kVectorRoom = 400;

constexpr std::uintptr_t kSendCastSpell = 0x00590990;
constexpr std::uintptr_t kSendMoveSettler = 0x00590a30;
constexpr std::uintptr_t kSendPatrolSettler = 0x00590af0;
constexpr std::uintptr_t kSendConvertCarriers = 0x00590c10;
constexpr std::uintptr_t kCountEnemySoldiers = 0x0058a2c0;

std::vector<std::string>* g_strings = nullptr;

void __stdcall RecordString(const char* text) {
    if (g_strings != nullptr) {
        g_strings->emplace_back(text);
    }
}

void __cdecl RecordDebugLog(const char* text) {
    if (g_strings != nullptr) {
        g_strings->emplace_back(std::string("log: ") + text);
    }
}

std::vector<std::uint8_t> Bytes(const void* start, std::size_t size) {
    const auto* p = static_cast<const std::uint8_t*>(start);
    return std::vector<std::uint8_t>(p, p + size);
}

std::uint32_t g_hunters[kPlayers][kVectorRoom][2];
std::uint32_t g_catapults[kPlayers][kVectorRoom];

constexpr std::size_t kListsOffset = offsetof(CGameData, player_lists);
constexpr std::size_t kListsBytes =
    offsetof(CGameData, player_list_lengths) + sizeof(CGameData::player_list_lengths) - kListsOffset;

struct MilitaryState {
    std::vector<std::uint8_t> lists, orders, armyState, census, sender, hunters, catapults, hunterData, catapultData,
        garrisons, catapultSteps;
    layout::Ring1000Indices index;
    std::uint16_t random[3];
    std::uint32_t censusStep, censusCursor, unk7e7ccc, unk7e7cd0, queue, search;
    std::uint8_t rebuildPending, skipReset, resetRequest;
    CGameData* mapWorld;

    static MilitaryState Capture(CGameData* world) {
        MilitaryState s{Bytes(reinterpret_cast<std::uint8_t*>(world) + kListsOffset, kListsBytes),  // raw-ok: test snapshot
                        Bytes(&g_aiGroupOrders.get(), sizeof(AiGroupOrders)),
                        Bytes(&g_aiArmyState.get(), sizeof(AiArmyState)),
                        Bytes(&g_aiCensus.get(), sizeof(AiAgentData)),
                        Bytes(&g_aiCommandSender.get(), sizeof(AiCommandSender)),
                        Bytes(&g_aiHunterVectors.get(), sizeof(AiVector) * kPlayers),
                        Bytes(&g_aiCatapultVectors.get(), sizeof(AiVector) * kPlayers),
                        Bytes(g_hunters, sizeof(g_hunters)),
                        Bytes(g_catapults, sizeof(g_catapults)),
                        Bytes(&g_aiGarrisonedBuildings.get(), sizeof(g_aiGarrisonedBuildings.get())),
                        Bytes(&g_aiCatapultHuntSteps.get(), sizeof(g_aiCatapultHuntSteps.get())),
                        world->event_index,
                        {},
                        g_aiCensusStep,
                        g_aiCensusCursor,
                        g_aiUnk7e7ccc,
                        g_aiUnk7e7cd0,
                        g_aiMilitaryQueue,
                        g_aiEnemySearch,
                        g_aiRebuildSettlersPending,
                        g_aiSkipMilitaryReset,
                        g_aiMilitaryResetRequest,
                        g_aiMapWorld};
        std::memcpy(s.random, world->random_state, sizeof(s.random));
        return s;
    }
    void Restore(CGameData* world) const {
        std::memcpy(reinterpret_cast<std::uint8_t*>(world) + kListsOffset, lists.data(), kListsBytes);  // raw-ok
        std::memcpy(&g_aiGroupOrders.get(), orders.data(), orders.size());
        std::memcpy(&g_aiArmyState.get(), armyState.data(), armyState.size());
        std::memcpy(&g_aiCensus.get(), census.data(), census.size());
        std::memcpy(&g_aiCommandSender.get(), sender.data(), sender.size());
        std::memcpy(&g_aiHunterVectors.get(), hunters.data(), hunters.size());
        std::memcpy(&g_aiCatapultVectors.get(), catapults.data(), catapults.size());
        std::memcpy(g_hunters, hunterData.data(), hunterData.size());
        std::memcpy(g_catapults, catapultData.data(), catapultData.size());
        std::memcpy(&g_aiGarrisonedBuildings.get(), garrisons.data(), garrisons.size());
        std::memcpy(&g_aiCatapultHuntSteps.get(), catapultSteps.data(), catapultSteps.size());
        world->event_index = index;
        std::memcpy(world->random_state, random, sizeof(random));
        g_aiCensusStep = censusStep;
        g_aiCensusCursor = censusCursor;
        g_aiUnk7e7ccc = unk7e7ccc;
        g_aiUnk7e7cd0 = unk7e7cd0;
        g_aiMilitaryQueue = queue;
        g_aiEnemySearch = search;
        g_aiRebuildSettlersPending = rebuildPending;
        g_aiSkipMilitaryReset = skipReset;
        g_aiMilitaryResetRequest = resetRequest;
        g_aiMapWorld = mapWorld;
    }
    bool operator==(const MilitaryState& o) const {
        return lists == o.lists && orders == o.orders && armyState == o.armyState && census == o.census &&
               sender == o.sender && hunters == o.hunters && catapults == o.catapults && hunterData == o.hunterData &&
               catapultData == o.catapultData && garrisons == o.garrisons && catapultSteps == o.catapultSteps &&
               index.read == o.index.read && index.write == o.index.write &&
               std::memcmp(random, o.random, sizeof(random)) == 0 && censusStep == o.censusStep &&
               censusCursor == o.censusCursor && unk7e7ccc == o.unk7e7ccc && unk7e7cd0 == o.unk7e7cd0 &&
               queue == o.queue && search == o.search && rebuildPending == o.rebuildPending &&
               skipReset == o.skipReset && resetRequest == o.resetRequest && mapWorld == o.mapWorld;
    }
};

std::uint32_t SmallSettler(test::Rng& rng) { return 1 + rng.Below(kTestSettlers - 1); }
std::uint16_t SmallBuilding(test::Rng& rng) {
    return static_cast<std::uint16_t>(rng.Chance(1, 2) ? 0 : rng.Below(kTestBuildings));
}

// A counter around its limit (or anywhere).
std::uint32_t Near(test::Rng& rng, std::int32_t limit) {
    if (rng.Chance(1, 3)) {
        return rng.Below(static_cast<std::uint32_t>(limit) + 3);
    }
    return static_cast<std::uint32_t>(limit - 1 + static_cast<std::int32_t>(rng.Below(4)));
}

void SetUpMilitary(test::Rng& rng, CGameData* world) {
    rng.Fill(world->random_state, sizeof(world->random_state));
    world->map_size = kTestMap;
    for (std::uint32_t x = 0; x < kTestMap; ++x) {
        for (std::uint32_t y = 0; y < kTestMap; ++y) {
            layout::Tile& tile = world->tiles[x][y];
            tile.area = static_cast<std::uint16_t>(rng.Chance(1, 10) ? 0 : 1 + rng.Below(kTestAreas - 1));
            tile.walk_region = static_cast<std::uint8_t>(rng.Chance(7, 8) ? 1 : 2);
            tile.owner = static_cast<std::uint8_t>(rng.Below(4));
        }
    }
    world->area_high_water = rng.Below(kTestAreas);
    for (std::uint32_t area = 0; area < kTestAreas; ++area) {
        world->areas[area].player = static_cast<std::uint8_t>(rng.Below(4));
        world->areas[area].settlers_by_type[0] = static_cast<std::uint16_t>(rng.Chance(1, 3) ? 0 : SmallSettler(rng));
    }
    world->settler_high_water = rng.Chance(1, 2) ? rng.Below(49) : 60 + rng.Below(kTestSettlers - 70);
    for (std::uint32_t id = 1; id < kTestSettlers; ++id) {
        layout::Settler& settler = world->settlers[id];
        settler.owner = static_cast<std::uint8_t>(rng.Below(4));
        settler.art = static_cast<std::uint8_t>(rng.Chance(1, 20) ? Value(SettlerType::Catapult)
                                                : rng.Chance(2, 3)  ? 5 + rng.Below(32)
                                                                    : rng.Below(48));
        settler.x = static_cast<std::uint16_t>(rng.Below(kTestMap));
        settler.y = static_cast<std::uint16_t>(rng.Below(kTestMap));
        settler.next = static_cast<std::uint16_t>(rng.Chance(1, 3) || id + 10 >= kTestSettlers ? 0
                                                                                           : id + 1 + rng.Below(9));
    }
    world->building_high_water = rng.Below(kTestBuildings);
    constexpr std::uint8_t kTypes[] = {15, 16, 18, 19, 15, 2, 26, 27};
    for (std::uint32_t id = 1; id < kTestBuildings; ++id) {
        layout::Building& building = world->buildings[id];
        building.owner = static_cast<std::uint8_t>(rng.Below(4));
        building.art = kTypes[rng.Below(sizeof(kTypes))];
        building.buildup = static_cast<std::uint8_t>(rng.Chance(1, 5) ? 1 + rng.Below(50) : 0);
        building.x = static_cast<std::uint16_t>(20 + rng.Below(80));
        building.y = static_cast<std::uint16_t>(20 + rng.Below(80));
        building.door_x = static_cast<std::uint16_t>(building.x + 1);
        building.door_y = static_cast<std::uint16_t>(building.y + 1);
        building.garrison = static_cast<std::uint16_t>(rng.Chance(2, 3) ? 0 : SmallSettler(rng));
        for (std::uint8_t& b : building.kind_bytes) {
            b = static_cast<std::uint8_t>(rng.Chance(3, 4) ? 0 : rng.Below(3));
        }
    }
    AiGroupOrders& orders = g_aiGroupOrders.get();
    rng.Fill(&orders, sizeof(orders));
    rng.Fill(&g_aiArmyState.get(), sizeof(AiArmyState));
    rng.Fill(&g_aiCensus.get(), sizeof(AiAgentData));
    for (std::uint32_t& word : g_aiGarrisonedBuildings.get()) {
        word = rng.Chance(1, 2) ? 0 : rng.Next32();
    }
    for (std::uint32_t word = 0; word < 32; ++word) {
        reinterpret_cast<std::uint32_t*>(orders.settlers_with_orders)[word] = rng.Chance(1, 2) ? 0 : rng.Next32();
    }
    AiAgentData& census = g_aiCensus.get();
    AiArmyState& state = g_aiArmyState.get();
    census.settlers_counted = static_cast<std::uint8_t>(rng.Below(2));
    census.buildings_counted = static_cast<std::uint8_t>(rng.Below(2));
    census.unk002 = static_cast<std::uint8_t>(rng.Below(2));
    census.team_strength_valid = static_cast<std::uint8_t>(rng.Chance(3, 4) ? 1 : 0);
    state.start_attack_calls = rng.Chance(1, 2) ? 40 : rng.Below(35);
    state.disband_steps = Near(rng, 1100);
    state.rebuild_steps = Near(rng, 2000);
    state.recruit_steps = Near(rng, 2800);
    state.settler_census_steps = Near(rng, 1900);
    state.building_census_steps = Near(rng, 700);
    g_aiCensusStep = rng.Below(9);
    g_aiCensusCursor = rng.Below(100);
    g_aiRebuildSettlersPending = static_cast<std::uint8_t>(rng.Chance(1, 8) ? 1 : 0);
    g_aiSkipMilitaryReset = static_cast<std::uint8_t>(rng.Below(2));
    g_aiEnemySearch = rng.Chance(1, 2) ? 500 : 5000;
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        layout::Player& player = world->players[p];
        player.active = static_cast<std::int32_t>(p < 4 ? (rng.Chance(2, 3) ? 2 : 1) : 0);
        player.team = static_cast<std::int32_t>(rng.Below(3));
        player.race = static_cast<std::int32_t>(rng.Below(4));
        player.start_x = static_cast<std::int32_t>(20 + rng.Below(80));
        player.start_y = static_cast<std::int32_t>(20 + rng.Below(80));
        player.soldier_strength = rng.Chance(1, 10) ? rng.Next32() : rng.Below(1000);
        for (std::uint16_t& head : player.settler_lists) {
            head = static_cast<std::uint16_t>(rng.Chance(1, 2) ? 0 : SmallSettler(rng));
        }
        g_aiCatapultHuntSteps.get()[p] = Near(rng, 50);
        state.thief_hunt_steps[p] = Near(rng, 420);
        state.mage_hunt_steps[p] = Near(rng, 550);
        state.military_steps[p] = Near(rng, 150);
        state.spell_steps[p] = Near(rng, 75);
        state.gather_countdowns[p] = rng.Below(4);
        state.geologist_timers[p] = rng.Below(10);
        orders.gather_buildings[p] = rng.Chance(2, 3) ? 0 : SmallBuilding(rng);
        orders.thief_cursors[p] = 0;
        orders.highest_group[p] = static_cast<std::int32_t>(rng.Below(10));
        orders.group_size[p] = static_cast<std::int32_t>(rng.Below(20));
        for (std::uint32_t& s : census.soldiers[p]) {
            s = rng.Chance(1, 3) ? 0 : rng.Below(8);
        }
        for (std::uint32_t& b : census.buildings[p]) {
            b = rng.Chance(2, 3) ? 0 : rng.Below(3);
        }
        census.team_strength[p] = rng.Below(60);
        census.geologists[p] = static_cast<std::int32_t>(rng.Below(5));
        census.thieves[p] = static_cast<std::int32_t>(rng.Below(5));
        census.other_settlers[p] = static_cast<std::int32_t>(rng.Below(300));
        const std::uint32_t hunters = rng.Below(6);
        const std::uint32_t catapults = rng.Below(6);
        for (std::uint32_t k = 0; k < kVectorRoom; ++k) {
            g_hunters[p][k][0] = SmallSettler(rng);
            g_hunters[p][k][1] = rng.Chance(1, 2) ? 0 : SmallSettler(rng);
            g_catapults[p][k] = SmallSettler(rng);
        }
        AiVector& h = g_aiHunterVectors.get()[p];
        h.begin = reinterpret_cast<std::uint32_t>(&g_hunters[p][0][0]);
        h.end = h.begin + hunters * 8;
        h.unk8 = h.begin + kVectorRoom * 8;
        AiVector& c = g_aiCatapultVectors.get()[p];
        c.begin = reinterpret_cast<std::uint32_t>(&g_catapults[p][0]);
        c.end = c.begin + catapults * 4;
        c.unk8 = c.begin + kVectorRoom * 4;
        for (std::uint32_t g = 0; g < kUnitGroups; ++g) {
            world->player_list_lengths[p][g] = rng.Chance(1, 3) ? 0 : static_cast<std::int32_t>(rng.Below(30));
            for (std::uint32_t k = 0; k < kGroupSlots; ++k) {
                world->player_lists[p][g][k] = static_cast<std::uint16_t>(SmallSettler(rng));
            }
            census.group_target_buildings[p][g] = static_cast<std::uint16_t>(rng.Chance(2, 3) ? 0 : SmallBuilding(rng));
            census.group_defending[p][g] = static_cast<std::uint8_t>(rng.Below(2));
            orders.group_targets[p][g].patrol0 = SmallBuilding(rng);
            orders.group_targets[p][g].patrol1 = SmallBuilding(rng);
            orders.group_targets[p][g].prey = static_cast<std::uint16_t>(rng.Chance(1, 2) ? 0 : SmallSettler(rng));
            orders.home_buildings[p][g] = SmallBuilding(rng);
            orders.home_areas[p][g] = static_cast<std::uint16_t>(rng.Below(kTestAreas));
            orders.group_flags[p][g] = static_cast<std::uint8_t>(rng.Chance(3, 4) ? 0 : 1);
            orders.patrol_steps[p][g] = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : rng.Below(9));
            orders.group_mages[p][g] = static_cast<std::int32_t>(rng.Below(4));
            state.attack_delays[p][g] = rng.Chance(2, 3) ? 0 : rng.Below(5);
        }
    }
    const std::uint32_t count = rng.Below(4);
    const std::uint32_t read = rng.Below(1000);
    world->event_index.read = read;
    world->event_index.write = (read + count) % 1000;
    for (std::uint32_t k = 0; k < count; ++k) {
        layout::Event& event = world->events[(read + k) % 1000];
        event.type = static_cast<std::int32_t>(1 + rng.Below(6));
        event.a = static_cast<std::int32_t>(event.type == 1 || event.type == 5 ? SmallSettler(rng) : rng.Below(kTestBuildings));
        event.b = static_cast<std::int32_t>(event.type == 1 || event.type == 5 ? rng.Below(48) : rng.Below(4));
        event.c = static_cast<std::int32_t>(rng.Below(4));
    }
}

void CompareMilitary(test::Context& ctx, RunMilitaryFn candidate, AgentRunFn candidateRun, int cases) {
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    const MilitaryState saved = MilitaryState::Capture(world);
    CGameData* savedWorld = g_aiWorld;
    ComputerPlayer* savedPlayer = g_aiPlayer;
    const std::uint8_t savedNumber = g_aiPlayerNumber;
    const std::int32_t savedMission = g_campaignMission;
    std::vector<std::uint8_t> computerPlayer(sizeof(ComputerPlayer));
    g_aiWorld = world;
    g_aiPlayer = reinterpret_cast<ComputerPlayer*>(computerPlayer.data());
    g_campaignMission = 0;

    std::vector<std::uint32_t> counts;
    std::size_t nextCount = 0;
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    const auto count = [&](CGameData*, std::uint32_t, const std::uint32_t*) { return counts[nextCount++ % counts.size()]; };
    stubs.Install<4, kCountEnemySoldiers>(count);
    stubs.Install<4, kCountEnemyBowmen>(count);
    stubs.Install<5, kSendCastSpell>();
    stubs.Install<4, kSendMoveSettler>();
    stubs.Install<4, kSendPatrolSettler>();
    stubs.Install<3, kQueueChangeGarrison>();
    stubs.Install<5, kSendConvertCarriers>();
    stubs.Install<4, kSendMoveGroup>();
    const auto recorder = reinterpret_cast<std::uint32_t>(&RecordString);
    test::PatchBytes(reinterpret_cast<std::uintptr_t>(&g_importOutputDebugStringA.get()), &recorder, sizeof(recorder));
    test::ScopedJump debugLog(kDebugLog, reinterpret_cast<const void*>(&RecordDebugLog));

    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        SetUpMilitary(rng, world);
        g_aiMapWorld = rng.Chance(1, 2) ? world : nullptr;  // (RunMilitary sets it)
        const std::uint32_t player = rng.Below(4);
        const std::uint32_t reset = rng.Chance(1, 10) ? 1 + rng.Below(255) : (rng.Chance(1, 2) ? 0 : 0x100u);
        const std::uint32_t queue = rng.Next32();
        const bool viaAgent = rng.Chance(1, 4);
        g_aiPlayerNumber = static_cast<std::uint8_t>(player);
        reinterpret_cast<ComputerPlayer*>(computerPlayer.data())->message_queue = queue;
        g_aiMilitaryResetRequest = static_cast<std::uint8_t>(reset & 0xff);
        counts.clear();
        for (int k = 0; k < 16; ++k) {
            counts.push_back(rng.Chance(1, 2) ? 0 : rng.Below(rng.Chance(1, 3) ? 40 : 4));
        }
        const MilitaryState start = MilitaryState::Capture(world);
        struct Outcome {
            MilitaryState state;
            std::vector<test::StubCall> calls;
            std::vector<std::string> strings;
        };
        auto run = [&](RunMilitaryFn f, AgentRunFn agent) {
            start.Restore(world);
            nextCount = 0;
            Outcome out{start, {}, {}};
            g_strings = &out.strings;
            if (viaAgent) {
                agent(nullptr, nullptr);
            } else {
                f(world, player, queue, reset);
            }
            g_strings = nullptr;
            out.state = MilitaryState::Capture(world);
            out.calls = stubs.Take();
            return out;
        };
        const Outcome a = run(Original_AI_RunMilitary, Original_T_ATTACK_AGENT_Run);
        const Outcome b = run(candidate, candidateRun);
        bool same = S3RE_EXPECT(a.state.lists == b.state.lists);
        same &= S3RE_EXPECT(a.state.orders == b.state.orders);
        same &= S3RE_EXPECT(a.state.armyState == b.state.armyState);
        same &= S3RE_EXPECT(a.state.census == b.state.census);
        same &= S3RE_EXPECT(a.state == b.state);
        same &= S3RE_EXPECT(a.calls == b.calls);
        same &= S3RE_EXPECT(a.strings == b.strings);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, player %u, reset %u, agent %d", i, player, reset, viaAgent ? 1 : 0);
        }
    }
    test::UnpatchBytes(reinterpret_cast<std::uintptr_t>(&g_importOutputDebugStringA.get()));
    saved.Restore(world);
    g_aiWorld = savedWorld;
    g_aiPlayer = savedPlayer;
    g_aiPlayerNumber = savedNumber;
    g_campaignMission = savedMission;
}

std::uint32_t __fastcall WrongRunMilitary(CGameData* world, std::uint32_t player, std::uint32_t queue,
                                          std::uint32_t reset) {
    // Wrong: the thief hunt fires one step early.
    if ((reset & 0xff) == 0 && g_aiArmyState.get().thief_hunt_steps[player] == 420) {
        g_aiArmyState.get().thief_hunt_steps[player] = 421;
    }
    return AI_RunMilitary(world, player, queue, reset);
}

std::uint32_t __fastcall WrongPatrolRun(CGameData* world, std::uint32_t player, std::uint32_t queue,
                                        std::uint32_t reset) {
    // Wrong: the patrol step counter starts one lower.
    for (std::uint8_t& steps : g_aiGroupOrders.get().patrol_steps[player]) {
        if (steps > 1) {
            --steps;
        }
    }
    return AI_RunMilitary(world, player, queue, reset);
}

}  // namespace

S3RE_TEST(AiRunMilitary_MatchOriginal) {
    CompareMilitary(ctx, &AI_RunMilitary, &T_ATTACK_AGENT_Run, 4000);
}

S3RE_TEST(AiRunMilitary_ComparisonDetectsWrongImplementation) {
    for (RunMilitaryFn wrong : {&WrongRunMilitary, &WrongPatrolRun}) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareMilitary(quiet, wrong, &T_ATTACK_AGENT_Run, 3000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

}  // namespace s3re::game
