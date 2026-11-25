// The military AI's reaction to the world's events (Rainer\CompMapWorkerAgent.cpp;
// re/computer_players.md "Army"): AI::RunMilitary 0x58f990 calls
// ProcessWorldEvents first every time. The events are the world's ring
// CGameData::events (1000 entries, read / write index event_index; the
// game adds them, only this function reads them). Changes the simulation
// state (the ring's read index), so it is exact.
//
//   0x00590da0 AI::ProcessWorldEvents(): until the read index reaches the
//              write index (wrapping at 1000), per event {type, a, b, c}:
//    1, 5      a settler (a) of type b of player c is gone: its bit in
//              the army's settler bitset is cleared (std::bitset<32000>,
//              _Xran 0x596570 above), its entry as a hunter in player c's
//              hunter vector is erased, the first hunter after it in every
//              computer player's vector stops (target 0). A catapult (b =
//              41) is erased from c's catapult vector (logged "Delete
//              Catapult nr. ..." with the iterator, the vector's start and
//              end and its size through OutputDebugStringA; every computer
//              player's group 1..9 hunting it is reset, ResetUnitGroup
//              0x5930f0; original quirk, kept: after an erase the walk
//              still advances, so the moved-up entry is skipped). Any
//              other settler: each empty group 1..highest group of c is
//              reset and the highest group moved into it ("player: %d,
//              Army %d ist killed", ResetUnitGroup, MoveLastGroupInto
//              0x592240).
//    2         a building (a) is gone: its bit in g_aiGarrisonedBuildings
//              is cleared (_Xran 0x58b520 from 4000), and for every
//              computer player's group 1..9: a group attacking it is reset;
//              its home building and patrol buildings naming it become 0.
//    3         a building (a) changed hands from b to c: b's groups 1..9
//              drop it as home or patrol building. If c is a computer
//              player: its garrison is asked for (QueueChangeGarrison
//              0x590bb0, op 1); each of c's groups 0..9 that had it as
//              target loses the target, steal flag and delay; the first of
//              them, unless a group of c already patrols it, guards it
//              (gather position = the building, patrol building 0 = it,
//              home area = its door's area, home building = it), but only
//              when no group 1..9 has that home area yet (else its patrol
//              buildings become 0). Then every computer player's group
//              1..9 that attacked it loses target, steal flag and delay.
//    4         a building (a) of player b is attacked by c: if b is a
//              computer player, with the enemy soldiers within 30 of it
//              (CountEnemySoldiersNear 0x58a2c0) and CountSpareSoldiers
//              0x592100 (unused): when b's towers and castles (census)
//              exceed twice its highest group and c's soldiers three times
//              b's, or those enemies exceed twice b's soldiers, every
//              group 1..9 with members retreats into b's castle nearest
//              to its start (MilitaryBuildingNearestStart 0x58b1a0; none:
//              nothing; QueueChangeGarrison op 1; "Einigeln ...", defending,
//              delay + 2800 - r % 700); otherwise the nearest groups
//              (first members, TileDistance) among 1..highest group are
//              picked until their members reach the enemies (at most 21
//              rounds) and defend the building ("Defend Tower ...",
//              defending, delay + 100 - r % 25). Both clear the gather
//              building and countdown and the groups' patrols and steal
//              flags and send them (SendMoveGroup 0x590c80).
//    6         the areas changed: after the loop, every computer player's
//              group 0..9 gets home area 0 without a home building, else
//              the home building's door area.
//    other     logged "WRONG EVENT ..." through DebugLog.
//              The Random draws are logged "RANDOM_55 wert = %u" (DebugLog).
//              EAX is not used by the caller.
//   0x0058b1a0 AI::MilitaryBuildingNearestStart(player) thiscall (`this`
//              unused), ret 4: the player's finished castle nearest
//              (signed TileDistance from its start position, first on a
//              tie) if any; else the nearest large tower, else the nearest
//              small tower; 0 if none.

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
constexpr std::int32_t kFarthest = 0x7fffffff;
constexpr std::int32_t kComputerControlled = 2;
constexpr std::uint32_t kOrderBits = 32000;
constexpr std::uint32_t kGarrisonBits = 4000;
constexpr std::uint32_t kEventRing = 1000;
constexpr std::uint32_t kThreatDistance = 30;     // event 4: enemies counted around the building
constexpr std::uint32_t kDigInDelay = 2800;       // - up to 699
constexpr std::uint32_t kDigInDelaySpread = 700;
constexpr std::uint32_t kDefendDelay = 100;       // - up to 24
constexpr std::uint32_t kDefendDelaySpread = 25;
constexpr std::int32_t kDefendRounds = 20;        // the group picking runs at most 21 times
constexpr std::size_t kTextBytes = 256;

enum EventType : std::int32_t {
    kSettlerGone = 1,
    kBuildingGone = 2,
    kBuildingTaken = 3,
    kBuildingAttacked = 4,
    kSettlerRemoved = 5,  // handled like 1
    kAreasChanged = 6,
};

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kRandom = 0x004feb70;
constexpr std::uintptr_t kTileDistance = 0x004ff320;
constexpr std::uintptr_t kResetUnitGroup = 0x005930f0;
constexpr std::uintptr_t kMoveLastGroupInto = 0x00592240;
constexpr std::uintptr_t kQueueChangeGarrison = 0x00590bb0;
constexpr std::uintptr_t kSendMoveGroup = 0x00590c80;
constexpr std::uintptr_t kCountEnemySoldiers = 0x0058a2c0;
constexpr std::uintptr_t kCountSpareSoldiers = 0x00592100;
constexpr std::uintptr_t kNearestStartCastle = 0x0058b1a0;
constexpr std::uintptr_t kOrderXran = 0x00596570;
constexpr std::uintptr_t kGarrisonXran = 0x0058b520;
constexpr std::uintptr_t kSprintf = 0x0061f27e;
constexpr std::uintptr_t kDebugLog = 0x00428720;

std::uint32_t Random() {
    using Fn = std::uint32_t(__fastcall*)(CGameData* world, void* edx);
    return reinterpret_cast<Fn>(kRandom)(g_aiMapWorld, nullptr);
}

std::int32_t TileDistance(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1) {
    using Fn = std::int32_t(__stdcall*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
    return reinterpret_cast<Fn>(kTileDistance)(x0, y0, x1, y1);
}

using ArmyFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t player, std::uint32_t group);

void ResetUnitGroup(std::uint32_t player, std::uint32_t group) {
    reinterpret_cast<ArmyFn>(kResetUnitGroup)(&g_aiGroupOrders.get(), nullptr, player, group);
}

void QueueChangeGarrison(std::uint32_t player, std::uint32_t building) {
    using Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t player, std::uint32_t building,
                                          std::uint32_t op);
    reinterpret_cast<Fn>(kQueueChangeGarrison)(&g_aiCommandSender.get(), nullptr, player, building, 1);
}

void SendToBuilding(std::uint32_t player, std::uint32_t group, std::uint32_t building) {
    const layout::Building& record = (&g_aiMapWorld.get()->buildings[0])[building];
    using Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t player, std::uint32_t group,
                                          std::uint32_t x, std::uint32_t y);
    reinterpret_cast<Fn>(kSendMoveGroup)(&g_aiCommandSender.get(), nullptr, player, group, record.x, record.y);
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

template <typename... Args>
void LogDebugLog(const GameText& format, Args... args) {
    char text[kTextBytes];
    Format(text, format, args...);
    using DebugLogFn = void(__cdecl*)(const char* text);
    reinterpret_cast<DebugLogFn>(kDebugLog)(text);
}

std::uint32_t LoggedRandom() {
    const std::uint32_t draw = Random() & 0xffff;
    LogDebugLog(texts::kRandom55, draw);
    return draw;
}

const layout::Building& BuildingAt(std::uint32_t id) { return (&g_aiMapWorld.get()->buildings[0])[id]; }
const layout::Settler& SettlerAt(std::uint32_t id) { return (&g_aiMapWorld.get()->settlers[0])[id]; }

std::uint16_t DoorArea(std::uint32_t building) {
    const layout::Building& record = BuildingAt(building);
    return (&g_aiMapWorld.get()->tiles[0][0])[record.door_x * layout::kMapRowTiles + record.door_y].area;
}

bool IsComputer(std::uint32_t player) { return g_aiMapWorld.get()->players[player].active == kComputerControlled; }

std::int32_t ListLength(std::uint32_t index) { return (&g_aiMapWorld.get()->player_list_lengths[0][0])[index]; }

const layout::Settler& FirstMember(std::uint32_t index) {
    return SettlerAt((&g_aiMapWorld.get()->player_lists[0][0][0])[index * kGroupSlots]);
}

// The census soldiers of a player, 0 if not playing (wrapping sums).
std::int32_t CensusSoldiers(std::uint32_t player) {
    if (g_aiMapWorld.get()->players[player].active == 0) {
        return 0;
    }
    const std::uint32_t* s = g_aiCensus.get().soldiers[player];
    return static_cast<std::int32_t>((s[6] + s[8] + s[7]) + (s[5] + s[4] + s[3]) + (s[2] + s[1] + s[0]));
}

std::uint16_t& CensusTarget(std::uint32_t index) { return (&g_aiCensus.get().group_target_buildings[0][0])[index]; }
std::uint8_t& Defending(std::uint32_t index) { return (&g_aiCensus.get().group_defending[0][0])[index]; }
std::uint32_t& AttackDelay(std::uint32_t index) { return (&g_aiArmyState.get().attack_delays[0][0])[index]; }
std::uint8_t& StealFlag(std::uint32_t index) { return (&g_aiGroupOrders.get().group_flags[0][0])[index]; }
AiGroupTarget& GroupTarget(std::uint32_t index) { return (&g_aiGroupOrders.get().group_targets[0][0])[index]; }

// An entry of a hunter vector (g_aiHunterVectors).
struct HunterEntry {
    std::uint32_t hunter;
    std::uint32_t target;
};

// 0x590de7..0x59109c: events 1 and 5.
void SettlerGone(std::uint32_t settler, std::int32_t type, std::uint32_t player) {
    AiGroupOrders& orders = g_aiGroupOrders.get();
    if (settler >= kOrderBits) {
        using XranFn = void(__fastcall*)(void* self, void* edx);
        reinterpret_cast<XranFn>(kOrderXran)(orders.settlers_with_orders, nullptr);
    }
    reinterpret_cast<std::uint32_t*>(orders.settlers_with_orders)[settler >> 5] &= ~(1u << (settler & 31));
    // 0x590e42..0x590e90: its own hunter entry erased
    AiVector& own = g_aiHunterVectors.get()[player];
    const std::uint32_t end = own.end;
    for (std::uint32_t at = own.begin; at < end; at += sizeof(HunterEntry)) {
        if (reinterpret_cast<const HunterEntry*>(at)->hunter != settler) {
            continue;
        }
        const std::uint32_t last = own.end;
        for (std::uint32_t next = at + sizeof(HunterEntry); next != last; next += sizeof(HunterEntry)) {
            *reinterpret_cast<HunterEntry*>(next - sizeof(HunterEntry)) = *reinterpret_cast<const HunterEntry*>(next);
        }
        own.end -= sizeof(HunterEntry);
        break;
    }
    // 0x590e96..0x590ef1: hunters after it stop
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        if (!IsComputer(p)) {
            continue;
        }
        const AiVector& hunters = g_aiHunterVectors.get()[p];
        for (std::uint32_t at = hunters.begin; at < hunters.end; at += sizeof(HunterEntry)) {
            auto* entry = reinterpret_cast<HunterEntry*>(at);
            if (entry->target == settler) {
                entry->target = 0;
                break;
            }
        }
    }
    if (type == Value(SettlerType::Catapult)) {
        // 0x590efe..0x591031: erased from the catapult vector
        AiVector& catapults = g_aiCatapultVectors.get()[player];
        for (std::uint32_t at = catapults.begin; at < catapults.end; at += 4) {
            const std::uint32_t value = *reinterpret_cast<const std::uint32_t*>(at);
            if (value != settler) {
                continue;
            }
            const std::uint32_t size =
                catapults.begin == 0
                    ? 0
                    : static_cast<std::uint32_t>(static_cast<std::int32_t>(catapults.end - catapults.begin) >> 2);
            LogDebugString(texts::kAiDeleteCatapult, value, at, catapults.begin, catapults.end, size);
            for (std::uint32_t p = 0; p < kPlayers; ++p) {
                if (!IsComputer(p)) {
                    continue;
                }
                for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
                    if (orders.group_targets[p][group].prey == *reinterpret_cast<const std::uint32_t*>(at)) {
                        ResetUnitGroup(p, group);
                    }
                }
            }
            const std::uint32_t last = catapults.end;
            for (std::uint32_t next = at + 4; next != last; next += 4) {
                *reinterpret_cast<std::uint32_t*>(next - 4) = *reinterpret_cast<const std::uint32_t*>(next);
            }
            catapults.end -= 4;  // (the walk goes on at at + 4: the moved-up entry is skipped)
        }
        return;
    }
    // 0x591036..0x59109c: empty groups dissolved
    for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
        if (ListLength(player * kUnitGroups + group) != 0 ||
            static_cast<std::int32_t>(group) > orders.highest_group[player]) {
            continue;
        }
        LogDebugString(texts::kAiArmyKilled, player, group);
        ResetUnitGroup(player, group);
        reinterpret_cast<ArmyFn>(kMoveLastGroupInto)(&orders, nullptr, player, group);
    }
}

// 0x5910a1..0x5911a3: event 2.
void BuildingGone(std::uint32_t building) {
    if (building >= kGarrisonBits) {
        using XranFn = void(__fastcall*)(void* self, void* edx);
        reinterpret_cast<XranFn>(kGarrisonXran)(&g_aiGarrisonedBuildings.get(), nullptr);
    }
    g_aiGarrisonedBuildings.get()[building >> 5] &= ~(1u << (building & 31));
    AiGroupOrders& orders = g_aiGroupOrders.get();
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        if (!IsComputer(p)) {
            continue;
        }
        for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
            const std::uint32_t index = p * kUnitGroups + group;
            if (CensusTarget(index) == building) {
                ResetUnitGroup(p, group);
            }
            if (orders.home_buildings[p][group] == building) {
                orders.home_buildings[p][group] = 0;
            }
            AiGroupTarget& target = GroupTarget(index);
            if (target.patrol0 == building) {
                target.patrol0 = 0;
            }
            if (target.patrol1 == building) {
                target.patrol1 = 0;
            }
        }
    }
}

// 0x5911a8..0x5914bb: event 3.
void BuildingTaken(std::uint32_t building, std::uint32_t oldOwner, std::uint32_t newOwner) {
    AiGroupOrders& orders = g_aiGroupOrders.get();
    for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
        const std::uint32_t index = oldOwner * kUnitGroups + group;
        std::uint16_t& home = (&orders.home_buildings[0][0])[index];
        if (home == building) {
            home = 0;
        }
        AiGroupTarget& target = GroupTarget(index);
        if (target.patrol0 == building) {
            target.patrol0 = 0;
        }
        if (target.patrol1 == building) {
            target.patrol1 = 0;
        }
    }
    if (IsComputer(newOwner)) {
        QueueChangeGarrison(newOwner, building);
        const std::uint32_t doorArea = DoorArea(building);
        bool patrolled = false;
        for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
            const AiGroupTarget& target = GroupTarget(newOwner * kUnitGroups + group);
            if (target.patrol0 == building || target.patrol1 == building) {
                patrolled = true;
                break;
            }
        }
        bool assigned = false;
        for (std::uint32_t group = 0; group < kUnitGroups; ++group) {
            const std::uint32_t index = newOwner * kUnitGroups + group;
            if (CensusTarget(index) != building) {
                continue;
            }
            bool keepPatrol = true;
            if (!assigned && !patrolled) {
                assigned = true;
                bool homeTaken = false;
                for (std::uint32_t other = kFirstGroup; other <= kLastGroup; ++other) {
                    if (orders.home_areas[newOwner][other] == doorArea) {
                        homeTaken = true;
                        break;
                    }
                }
                if (homeTaken) {
                    keepPatrol = false;
                } else {
                    const layout::Building& record = BuildingAt(building);
                    orders.gather_positions[newOwner][group].x = static_cast<std::int16_t>(record.x);
                    orders.gather_positions[newOwner][group].y = static_cast<std::int16_t>(record.y);
                    AiGroupTarget& target = GroupTarget(index);
                    target.patrol0 = static_cast<std::uint16_t>(building);
                    target.patrol1 = 0;
                    orders.home_areas[newOwner][group] = DoorArea(building);
                    orders.home_buildings[newOwner][group] = static_cast<std::uint16_t>(building);
                }
            }
            CensusTarget(index) = 0;
            if (!keepPatrol) {
                GroupTarget(index).patrol0 = 0;
                GroupTarget(index).patrol1 = 0;
            }
            StealFlag(index) = 0;
            AttackDelay(index) = 0;
        }
    }
    // 0x591445..0x5914b1: every computer player's attack on it ends
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        if (!IsComputer(p)) {
            continue;
        }
        for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
            const std::uint32_t index = p * kUnitGroups + group;
            if (CensusTarget(index) == building) {
                CensusTarget(index) = 0;
                StealFlag(index) = 0;
                AttackDelay(index) = 0;
            }
        }
    }
}

// 0x59167d..0x591806: event 4, every group with members retreats into the
// castle (or tower) nearest to the start.
void DigIn(std::uint32_t player) {
    using NearestFn = std::int32_t(__fastcall*)(void* self, void* edx, std::uint32_t player);
    const std::int32_t castle =
        reinterpret_cast<NearestFn>(kNearestStartCastle)(&g_aiEnemySearch.get(), nullptr, player);
    if (castle <= 0) {
        return;
    }
    const auto refuge = static_cast<std::uint32_t>(castle);
    QueueChangeGarrison(player, refuge);
    AiGroupOrders& orders = g_aiGroupOrders.get();
    for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
        const std::uint32_t index = player * kUnitGroups + group;
        if (ListLength(index) <= 0) {
            continue;
        }
        CensusTarget(index) = static_cast<std::uint16_t>(refuge);
        Defending(index) = 1;
        const std::uint32_t draw = LoggedRandom();
        AttackDelay(index) += kDigInDelay - draw % kDigInDelaySpread;
        g_aiArmyState.get().gather_countdowns[player] = 0;
        GroupTarget(index).patrol0 = 0;
        GroupTarget(index).patrol1 = 0;
        StealFlag(index) = 0;
        orders.gather_buildings[player] = 0;
        LogDebugString(texts::kAiDigIn, player, group, refuge);
        SendToBuilding(player, group, refuge);
    }
}

// 0x59180b..0x591a65: event 4, the nearest groups defend the building.
void DefendBuilding(std::uint32_t player, std::uint32_t building, std::int32_t enemies) {
    // (indexed by group 1..highest group, which the army keeps at most 9;
    // larger, the original's array would overlap other locals)
    bool chosen[2 * kUnitGroups] = {};
    std::int32_t members = 0;
    for (std::int32_t round = 0;; ++round) {
        std::int32_t nearest = kFarthest;
        std::uint32_t best = 0;
        for (std::int32_t group = 1; group <= g_aiGroupOrders.get().highest_group[player]; ++group) {
            const std::uint32_t index = player * kUnitGroups + static_cast<std::uint32_t>(group);
            if (ListLength(index) == 0 || chosen[group]) {
                continue;
            }
            const layout::Building& record = BuildingAt(building);
            const layout::Settler& first = FirstMember(index);
            const std::int32_t distance = TileDistance(record.x, record.y, first.x, first.y);
            if (distance < nearest) {
                nearest = distance;
                best = static_cast<std::uint32_t>(group);
            }
        }
        if (static_cast<std::int32_t>(best) > 0) {
            chosen[best] = true;
            members += ListLength(player * kUnitGroups + best);
        }
        if (members >= enemies || best == 0 || round >= kDefendRounds) {
            break;
        }
    }
    AiGroupOrders& orders = g_aiGroupOrders.get();
    for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
        if (!chosen[group]) {
            continue;
        }
        const std::uint32_t index = player * kUnitGroups + group;
        CensusTarget(index) = static_cast<std::uint16_t>(building);
        Defending(index) = 1;
        GroupTarget(index).patrol0 = 0;
        GroupTarget(index).patrol1 = 0;
        StealFlag(index) = 0;
        const std::uint32_t draw = LoggedRandom();
        AttackDelay(index) += kDefendDelay - draw % kDefendDelaySpread;
        orders.gather_buildings[player] = 0;
        g_aiArmyState.get().gather_countdowns[player] = 0;
        LogDebugString(texts::kAiDefendTower, player, group, building);
        SendToBuilding(player, group, building);
    }
}

// 0x5914c0..0x591a65: event 4.
void BuildingAttacked(std::uint32_t building, std::uint32_t defender, std::uint32_t attacker) {
    if (!IsComputer(defender)) {
        return;
    }
    const layout::Building& record = BuildingAt(building);
    using CountFn = std::int32_t(__fastcall*)(void* self, void* edx, std::uint32_t x, std::uint32_t y,
                                              std::uint32_t distance, std::uint32_t player);
    const std::int32_t enemies = reinterpret_cast<CountFn>(kCountEnemySoldiers)(&g_aiEnemySearch.get(), nullptr,
                                                                                record.x, record.y, kThreatDistance,
                                                                                defender);
    using SpareFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t player);
    reinterpret_cast<SpareFn>(kCountSpareSoldiers)(&g_aiGroupOrders.get(), nullptr, defender);  // (unused)
    const std::int32_t attackers = CensusSoldiers(attacker);
    const std::int32_t own = CensusSoldiers(defender);
    const std::int32_t highest = g_aiGroupOrders.get().highest_group[defender];
    const std::uint32_t* buildings = g_aiCensus.get().buildings[defender];
    const auto towers = static_cast<std::int32_t>(buildings[Value(BuildingType::Castle)] +
                                                  buildings[Value(BuildingType::LargeTower)] +
                                                  buildings[Value(BuildingType::SmallTower)]);
    const bool outnumbered = static_cast<std::int32_t>(static_cast<std::uint32_t>(highest) * 2) < towers &&
                             attackers > static_cast<std::int32_t>(static_cast<std::uint32_t>(own) * 3);
    if (outnumbered || enemies > static_cast<std::int32_t>(static_cast<std::uint32_t>(own) * 2)) {
        DigIn(defender);
    } else {
        DefendBuilding(defender, building, enemies);
    }
}

// 0x591b0e..0x591be7: event 6, the home areas follow the home buildings.
void RefreshHomeAreas() {
    AiGroupOrders& orders = g_aiGroupOrders.get();
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        if (!IsComputer(p)) {
            continue;
        }
        for (std::uint32_t group = 0; group < kUnitGroups; ++group) {
            const std::uint16_t home = orders.home_buildings[p][group];
            std::uint16_t& area = orders.home_areas[p][group];
            if (home == 0) {
                area = 0;
            } else if (area != DoorArea(home)) {
                area = DoorArea(home);
            }
        }
    }
}

}  // namespace

using NoArgFn = std::uint32_t(__fastcall*)(void* ecx, void* edx);
using NearestStartFn = std::int32_t(__fastcall*)(void* self, void* edx, std::uint32_t player);

std::uint32_t __fastcall AI_ProcessWorldEvents(void* ecx, void* edx);
std::int32_t __fastcall AI_MilitaryBuildingNearestStart(void* self, void* edx, std::uint32_t player);

S3RE_HOOK(0x00590da0, AI_ProcessWorldEvents, NoArgFn, Original_AI_ProcessWorldEvents, AI_ProcessWorldEvents);
S3RE_HOOK(0x0058b1a0, AI_MilitaryBuildingNearestStart, NearestStartFn, Original_AI_MilitaryBuildingNearestStart,
          AI_MilitaryBuildingNearestStart);

// Purpose: Lets the computer players react to what happened in the world
// since their last turn: dead settlers and catapults, lost, captured and
// attacked buildings, changed areas.
std::uint32_t __fastcall AI_ProcessWorldEvents(void* /*ecx*/, void* /*edx*/) {
    bool areasChanged = false;
    while (g_aiMapWorld.get()->event_index.write != g_aiMapWorld.get()->event_index.read) {
        CGameData* world = g_aiMapWorld;
        const layout::Event& event = world->events[world->event_index.read];
        switch (event.type) {
        case kSettlerGone:
        case kSettlerRemoved:
            SettlerGone(static_cast<std::uint32_t>(event.a), event.b, static_cast<std::uint32_t>(event.c));
            break;
        case kBuildingGone:
            BuildingGone(static_cast<std::uint32_t>(event.a));
            break;
        case kBuildingTaken:
            BuildingTaken(static_cast<std::uint32_t>(event.a), static_cast<std::uint32_t>(event.b),
                          static_cast<std::uint32_t>(event.c));
            break;
        case kBuildingAttacked:
            BuildingAttacked(static_cast<std::uint32_t>(event.a), static_cast<std::uint32_t>(event.b),
                             static_cast<std::uint32_t>(event.c));
            break;
        case kAreasChanged:
            areasChanged = true;
            break;
        default: {
            const std::uint32_t read = world->event_index.read;
            LogDebugLog(texts::kAiWrongEvent, read, world->events[read].type, world->events[read].a,
                        world->events[read].b);
            break;
        }
        }
        layout::Ring1000Indices& index = g_aiMapWorld.get()->event_index;
        ++index.read;
        if (g_aiMapWorld.get()->event_index.read >= kEventRing) {
            g_aiMapWorld.get()->event_index.read = 0;
        }
    }
    if (areasChanged) {
        RefreshHomeAreas();
    }
    return 0;
}

// Purpose: The computer player's castle (else large, else small tower)
// nearest to its start position: where its unit groups retreat when it is
// attacked by a much stronger enemy.
std::int32_t __fastcall AI_MilitaryBuildingNearestStart(void* /*self*/, void* /*edx*/, std::uint32_t player) {
    const layout::Player& record = g_aiMapWorld.get()->players[player];
    const auto startX = static_cast<std::uint32_t>(record.start_x);
    const auto startY = static_cast<std::uint32_t>(record.start_y);
    std::int32_t nearest = kFarthest;
    std::int32_t result = 0;
    const auto search = [&](BuildingType type) {
        for (std::uint32_t id = 1; id < g_aiMapWorld.get()->building_high_water + 1; ++id) {
            const layout::Building& building = BuildingAt(id);
            if (building.owner != player || building.buildup != 0 || building.art != Value(type)) {
                continue;
            }
            const std::int32_t distance = TileDistance(startX, startY, building.x, building.y);
            if (distance < nearest) {
                nearest = distance;
                result = static_cast<std::int32_t>(id);
            }
        }
    };
    search(BuildingType::Castle);
    if (result > 0) {
        return result;
    }
    search(BuildingType::LargeTower);
    if (result < 1) {
        search(BuildingType::SmallTower);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// A test world with a few events in its ring (all types, near the wrap),
// random army, census and army state, hunter and catapult vectors in fixed
// buffers (the function only erases, so their addresses are the same for
// both runs). The command senders and CountEnemySoldiersNear are recording
// stubs, OutputDebugStringA's import and DebugLog recorders; ResetUnitGroup,
// MoveLastGroupInto, CountSpareSoldiers, MilitaryBuildingNearestStart,
// TileDistance and CGameData::Random run.

namespace {

constexpr std::uint32_t kTestSettlers = 400;
constexpr std::uint32_t kTestBuildings = 30;
constexpr std::uint32_t kTestMap = 120;
constexpr std::uint32_t kTestAreas = 6;
constexpr std::uint32_t kVectorEntries = 6;

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

std::uint32_t g_hunters[kPlayers][kVectorEntries][2];
std::uint32_t g_catapults[kPlayers][kVectorEntries];

constexpr std::size_t kListsOffset = offsetof(CGameData, player_lists);
constexpr std::size_t kListsBytes =
    offsetof(CGameData, player_list_lengths) + sizeof(CGameData::player_list_lengths) - kListsOffset;

struct EventState {
    std::vector<std::uint8_t> lists, orders, armyState, census, sender, hunters, catapults, hunterData, catapultData,
        garrisons;
    layout::Ring1000Indices index;
    std::uint16_t random[3];

    static EventState Capture(CGameData* world) {
        EventState s{Bytes(reinterpret_cast<std::uint8_t*>(world) + kListsOffset, kListsBytes),  // raw-ok: test snapshot
                     Bytes(&g_aiGroupOrders.get(), sizeof(AiGroupOrders)),
                     Bytes(&g_aiArmyState.get(), sizeof(AiArmyState)),
                     Bytes(&g_aiCensus.get(), sizeof(AiAgentData)),
                     Bytes(&g_aiCommandSender.get(), sizeof(AiCommandSender)),
                     Bytes(&g_aiHunterVectors.get(), sizeof(AiVector) * kPlayers),
                     Bytes(&g_aiCatapultVectors.get(), sizeof(AiVector) * kPlayers),
                     Bytes(g_hunters, sizeof(g_hunters)),
                     Bytes(g_catapults, sizeof(g_catapults)),
                     Bytes(&g_aiGarrisonedBuildings.get(), sizeof(g_aiGarrisonedBuildings.get())),
                     world->event_index,
                     {}};
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
        world->event_index = index;
        std::memcpy(world->random_state, random, sizeof(random));
    }
    bool operator==(const EventState& o) const {
        return lists == o.lists && orders == o.orders && armyState == o.armyState && census == o.census &&
               sender == o.sender && hunters == o.hunters && catapults == o.catapults && hunterData == o.hunterData &&
               catapultData == o.catapultData && garrisons == o.garrisons && index.read == o.index.read &&
               index.write == o.index.write && std::memcmp(random, o.random, sizeof(random)) == 0;
    }
};

std::uint32_t SmallSettler(test::Rng& rng) { return 1 + rng.Below(kTestSettlers - 1); }

void SetUpEvents(test::Rng& rng, CGameData* world) {
    rng.Fill(world->random_state, sizeof(world->random_state));
    for (std::uint32_t x = 0; x < kTestMap; ++x) {
        for (std::uint32_t y = 0; y < kTestMap; ++y) {
            world->tiles[x][y].area = static_cast<std::uint16_t>(rng.Below(kTestAreas));
        }
    }
    for (std::uint32_t id = 1; id < kTestSettlers; ++id) {
        layout::Settler& settler = world->settlers[id];
        settler.x = static_cast<std::uint16_t>(rng.Below(kTestMap));
        settler.y = static_cast<std::uint16_t>(rng.Below(kTestMap));
    }
    world->building_high_water = rng.Chance(1, 20) ? 0xffffffffu : rng.Below(kTestBuildings);
    constexpr std::uint8_t kTypes[] = {15, 16, 18, 18, 19, 2};
    for (std::uint32_t id = 1; id < kTestBuildings; ++id) {
        layout::Building& building = world->buildings[id];
        building.owner = static_cast<std::uint8_t>(rng.Below(4));
        building.art = kTypes[rng.Below(sizeof(kTypes))];
        building.buildup = static_cast<std::uint8_t>(rng.Chance(1, 5) ? 1 + rng.Below(50) : 0);
        building.x = static_cast<std::uint16_t>(rng.Below(kTestMap));
        building.y = static_cast<std::uint16_t>(rng.Below(kTestMap));
        building.door_x = static_cast<std::uint16_t>(rng.Below(kTestMap));
        building.door_y = static_cast<std::uint16_t>(rng.Below(kTestMap));
    }
    AiGroupOrders& orders = g_aiGroupOrders.get();
    rng.Fill(&orders, sizeof(orders));
    rng.Fill(&g_aiArmyState.get(), sizeof(AiArmyState));
    rng.Fill(&g_aiCensus.get(), sizeof(AiAgentData));
    for (std::uint32_t& word : g_aiGarrisonedBuildings.get()) {
        word = rng.Next32();
    }
    AiAgentData& census = g_aiCensus.get();
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        layout::Player& player = world->players[p];
        player.active = static_cast<std::int32_t>(p < 4 ? (rng.Chance(2, 3) ? 2 : rng.Below(2)) : 0);
        player.start_x = static_cast<std::int32_t>(rng.Below(kTestMap));
        player.start_y = static_cast<std::int32_t>(rng.Below(kTestMap));
        orders.highest_group[p] = static_cast<std::int32_t>(rng.Below(10));
        for (std::uint32_t& s : census.soldiers[p]) {
            s = rng.Chance(1, 3) ? 0 : rng.Below(rng.Chance(1, 5) ? 60 : 6);
        }
        for (BuildingType type : {BuildingType::SmallTower, BuildingType::LargeTower, BuildingType::Castle}) {
            census.buildings[p][Value(type)] = rng.Below(rng.Chance(1, 2) ? 3 : 30);
        }
        const std::uint32_t hunters = rng.Below(kVectorEntries + 1);
        const std::uint32_t catapults = rng.Below(kVectorEntries + 1);
        for (std::uint32_t k = 0; k < kVectorEntries; ++k) {
            g_hunters[p][k][0] = SmallSettler(rng);
            g_hunters[p][k][1] = rng.Chance(1, 2) ? 0 : SmallSettler(rng);
            g_catapults[p][k] = SmallSettler(rng);
        }
        AiVector& h = g_aiHunterVectors.get()[p];
        h.begin = reinterpret_cast<std::uint32_t>(&g_hunters[p][0][0]);
        h.end = h.begin + hunters * 8;
        AiVector& c = g_aiCatapultVectors.get()[p];
        c.begin = rng.Chance(1, 8) ? 0 : reinterpret_cast<std::uint32_t>(&g_catapults[p][0]);
        c.end = c.begin == 0 ? 0 : c.begin + catapults * 4;
        for (std::uint32_t g = 0; g < kUnitGroups; ++g) {
            world->player_list_lengths[p][g] = rng.Chance(1, 3) ? 0 : static_cast<std::int32_t>(rng.Below(30));
            for (std::uint32_t k = 0; k < kGroupSlots; ++k) {
                world->player_lists[p][g][k] = static_cast<std::uint16_t>(SmallSettler(rng));
            }
            const auto building = [&]() {
                return static_cast<std::uint16_t>(rng.Chance(1, 2) ? 0 : rng.Below(kTestBuildings));
            };
            census.group_target_buildings[p][g] = building();
            orders.group_targets[p][g].patrol0 = building();
            orders.group_targets[p][g].patrol1 = building();
            orders.group_targets[p][g].prey = static_cast<std::uint16_t>(
                rng.Chance(1, 2) ? g_catapults[rng.Below(4)][rng.Below(kVectorEntries)] : SmallSettler(rng));
            orders.home_buildings[p][g] = building();
            orders.home_areas[p][g] = static_cast<std::uint16_t>(rng.Below(kTestAreas));
        }
    }
    // the events: 0..8 of them ending near the ring's wrap
    const std::uint32_t count = rng.Below(9);
    const std::uint32_t read = rng.Chance(1, 2) ? 995 + rng.Below(5) : rng.Below(1000);
    world->event_index.read = read;
    world->event_index.write = (read + count) % 1000;
    for (std::uint32_t k = 0; k < count; ++k) {
        layout::Event& event = world->events[(read + k) % 1000];
        event.type = static_cast<std::int32_t>(rng.Chance(1, 15) ? rng.Below(9) : 1 + rng.Below(6));
        switch (event.type) {
        case 1:
        case 5: {
            const std::uint32_t player = rng.Below(4);
            const bool catapult = rng.Chance(1, 3);
            event.a = static_cast<std::int32_t>(
                rng.Chance(1, 2) ? (catapult ? g_catapults[player][rng.Below(kVectorEntries)]
                                             : g_hunters[player][rng.Below(kVectorEntries)][rng.Below(2)])
                                 : SmallSettler(rng));
            event.b = catapult ? Value(SettlerType::Catapult) : static_cast<std::int32_t>(rng.Below(48));
            event.c = static_cast<std::int32_t>(player);
            break;
        }
        default:
            event.a = static_cast<std::int32_t>(rng.Below(kTestBuildings));
            event.b = static_cast<std::int32_t>(rng.Below(4));
            event.c = static_cast<std::int32_t>(rng.Below(4));
            break;
        }
    }
}

struct EventFunctions {
    NoArgFn process;
    NearestStartFn nearestStart;
};

void CompareEvents(test::Context& ctx, const EventFunctions& candidate, int cases) {
    const EventFunctions original{Original_AI_ProcessWorldEvents, Original_AI_MilitaryBuildingNearestStart};
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    const EventState saved = EventState::Capture(world);
    CGameData* savedMapWorld = g_aiMapWorld;
    g_aiMapWorld = world;

    std::vector<std::uint32_t> enemyCounts;
    std::size_t nextCount = 0;
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    stubs.Install<4, kCountEnemySoldiers>([&](CGameData*, std::uint32_t, const std::uint32_t*) {
        return enemyCounts[nextCount++ % enemyCounts.size()];
    });
    stubs.Install<4, kSendMoveGroup>([](CGameData*, std::uint32_t, const std::uint32_t* args) {
        return args[1] * 3 + args[2];
    });
    stubs.Install<3, kQueueChangeGarrison>([](CGameData*, std::uint32_t, const std::uint32_t*) { return 0x5180u; });
    const auto recorder = reinterpret_cast<std::uint32_t>(&RecordString);
    test::PatchBytes(reinterpret_cast<std::uintptr_t>(&g_importOutputDebugStringA.get()), &recorder, sizeof(recorder));
    test::ScopedJump debugLog(kDebugLog, reinterpret_cast<const void*>(&RecordDebugLog));

    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        SetUpEvents(rng, world);
        const bool nearest = rng.Chance(1, 5);
        const std::uint32_t player = rng.Below(4);
        enemyCounts.clear();
        for (int k = 0; k < 16; ++k) {
            enemyCounts.push_back(rng.Chance(1, 3) ? 0 : rng.Below(rng.Chance(1, 3) ? 200 : 20));
        }
        const EventState start = EventState::Capture(world);
        struct Outcome {
            std::uint32_t result;
            EventState state;
            std::vector<test::StubCall> calls;
            std::vector<std::string> strings;
        };
        auto run = [&](const EventFunctions& f) {
            start.Restore(world);
            nextCount = 0;
            Outcome out{};
            g_strings = &out.strings;
            out.result = nearest ? static_cast<std::uint32_t>(f.nearestStart(nullptr, nullptr, player))
                                 : (f.process(nullptr, nullptr), 0u);
            g_strings = nullptr;
            out.state = EventState::Capture(world);
            out.calls = stubs.Take();
            return out;
        };
        const Outcome a = run(original);
        const Outcome b = run(candidate);
        bool same = S3RE_EXPECT_EQ(a.result, b.result);
        same &= S3RE_EXPECT(a.state.lists == b.state.lists);
        same &= S3RE_EXPECT(a.state.orders == b.state.orders);
        same &= S3RE_EXPECT(a.state.census == b.state.census);
        same &= S3RE_EXPECT(a.state.armyState == b.state.armyState);
        same &= S3RE_EXPECT(a.state == b.state);
        same &= S3RE_EXPECT(a.calls == b.calls);
        same &= S3RE_EXPECT(a.strings == b.strings);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, nearest %d", i, nearest ? 1 : 0);
        }
    }
    test::UnpatchBytes(reinterpret_cast<std::uintptr_t>(&g_importOutputDebugStringA.get()));
    saved.Restore(world);
    g_aiMapWorld = savedMapWorld;
}

std::uint32_t __fastcall WrongProcess(void* ecx, void* edx) {
    // Wrong: a captured building is guarded even when a group has its home area.
    for (auto& row : g_aiGroupOrders.get().home_areas) {
        for (std::uint16_t& area : row) {
            area = static_cast<std::uint16_t>(area + 100);
        }
    }
    const std::uint32_t result = AI_ProcessWorldEvents(ecx, edx);
    for (auto& row : g_aiGroupOrders.get().home_areas) {
        for (std::uint16_t& area : row) {
            area = static_cast<std::uint16_t>(area - 100);
        }
    }
    return result;
}

std::int32_t __fastcall WrongNearestStart(void* self, void* edx, std::uint32_t player) {
    // Wrong: large towers before castles.
    const std::int32_t result = AI_MilitaryBuildingNearestStart(self, edx, player);
    if (result > 0 && BuildingAt(static_cast<std::uint32_t>(result)).art == Value(BuildingType::Castle)) {
        for (std::uint32_t id = 1; id < g_aiMapWorld.get()->building_high_water + 1; ++id) {
            const layout::Building& b = BuildingAt(id);
            if (b.owner == player && b.buildup == 0 && b.art == Value(BuildingType::LargeTower)) {
                return static_cast<std::int32_t>(id);
            }
        }
    }
    return result;
}

}  // namespace

S3RE_TEST(AiWorldEvents_MatchOriginal) {
    CompareEvents(ctx, EventFunctions{&AI_ProcessWorldEvents, &AI_MilitaryBuildingNearestStart}, 4000);
}

S3RE_TEST(AiWorldEvents_ComparisonDetectsWrongImplementation) {
    {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareEvents(quiet, EventFunctions{&WrongProcess, &AI_MilitaryBuildingNearestStart}, 3000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
    {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareEvents(quiet, EventFunctions{&AI_ProcessWorldEvents, &WrongNearestStart}, 3000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

}  // namespace s3re::game
