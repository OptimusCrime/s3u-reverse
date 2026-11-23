// Soldiers fighting (UpdateSettlers callee, package B).
//
// CGameData::UpdateSoldier (0x0050e9f0): (settler) -> EAX 1 if it attacked
//   this step, else 0. Only called for the soldier classes 1..3 (table
//   0x658f64: Swordsman, Bowman, Pikeman); range/minimum distance:
//   swordsmen 1/1, pikemen 2/1, bowmen 13/6 (shooting from a tower, job
//   140 (0x8c): 22/10). ret 4.
//
//   Phase 1 (attack), unless settler flag 0x20 is set: if
//   IsOtherPlayerNear(x, y, range, owner), the offset table is walked from
//   entry 1 while entry.distance <= range; the first entry with distance >=
//   the minimum, on the map, holding a settler of another team (player
//   +0xc) that is a valid target (soldier classes 1..9 or 20..29
//   (0x14..0x1d: geologist, pioneer, mage); 10..19 (thief) at distance
//   <= 7 or with its flag 0x10; class 99 (0x63, the tower guard type 37)
//   or 41 (0x29, the parts of a seven-tile unit); a donkey (class 30) in
//   job 309 (0x135) for swordsmen and pikemen), for a tower bowman of a
//   computer player only on the tower owner's land, in an attackable job
//   (idle and walking jobs, WalkToWork, the tower jobs 173/174 (0xad/0xae)
//   for swordsmen (pikemen at distance 1), the seven-tile part's job 304
//   (0x130), the geologist and pioneer work jobs 212/213/302/303, job 309
//   for swordsmen and pikemen), is attacked:
//   - job 142 (0x8e) (unless 174/140), attack animation by type and
//     direction (offset entry's sector + 4), base damage by type
//     (swordsmen 1/2/3: 10/14/20, with flag 0x4 15/21/30 and the counter *
//     5 / 8; bowmen 1/2/3: 5/6/7 and an arrow (SpawnArrow; from a tower
//     with an offset from the table 0x659738 by the slot byte); pikemen
//     1/2/3: 4/5/6);
//   - outside the own team's land: flag 0x2 doubles, 0x1 halves, then
//     damage = (player +0x80 * damage + 0x80) / [0x730eec] (256);
//     pikemen and seven-tile parts (class 41) take half, attacker flag
//     0x8 3/4; at least 1;
//   - a tower guard (173) queues event 4; a seven-tile part (job 304)
//     hands the hit to the catapult nearby (FindNearbySettlerType29);
//     notifications type 3; job 309 targets take no damage (-> 310,
//     0x136); in campaigns computer players' soldiers take half again;
//   - health (+3) >= damage: health -= damage. Otherwise the target dies:
//     towers (guard 173 / garrison 174) are captured when empty (garrison
//     list +0x8 empty: the building's soldiers are cleared, its land
//     re-owned from the layout table 0x6aa170 via ClearTileOwner 0x51b640
//     / SetOwnerOfFreeTile, RecalculateTerritory 0x519a90, the attacker
//     moves in; events, notifications 7/8, DebugLogs), else the target
//     gets job 175 (0xaf); a catapult (type 41, seven tiles) is freed with
//     its parts and a MapEffects_Spawn_544f80; others die (event 1,
//     RemoveSettlerFromLists, job 54 (0x36), animation 70 (0x46)).
//   Phase 2 (approach), when nothing was attacked and the settler isn't
//   in a tower job (174/140) or flag 0x20: within distance 18
//   (IsOtherPlayerNear(x, y, 18, owner)), the first enemy (human players:
//   another team; computer players: another team, unless both stand on
//   their own team's land) that is a valid target in an attackable job
//   (as above, the tower jobs for pikemen at any distance) gets the
//   settler sent to a free neighbour tile of the same walk region next to
//   it (bowmen closer than 8 to the target: next to the tile beside
//   themselves away from it): SetSettlerTarget, job UnitWalk (53, 0x35),
//   +0x1a = 0.
//
//   Not reproduced: other settler classes leave the minimum distance
//   uninitialized; unknown attacker types leave the damage uninitialized;
//   after a tower capture the original reads the offset entry indexed by
//   the soldier count it left in its loop variable (no effect).
// thiscall (ECX = world).

#include "game/debug_log.h"
#include "game/enums.h"
#include "game/globals.h"
#include "game/replacements.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/typed_fields.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/fake_graphics.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

std::uint32_t __fastcall GetMemberByte189(void* self, void* edx);
std::uint32_t __fastcall CGameData_SetSettlerAnimation(CGameData* world, void* edx, std::uint32_t settler,
                                                       std::uint32_t kind);
std::uint32_t __fastcall CGameData_NotifyLocalPlayer(CGameData* world, void* edx, std::uint32_t player,
                                                     std::uint32_t type, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall CGameData_IsOtherPlayerNear(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                     std::uint32_t radius, std::uint32_t player);
std::uint32_t __fastcall CGameData_TileScreenX(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall CGameData_TileScreenY(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall CGameData_QueuePendingEvent(CGameData* world, void* edx, std::uint32_t type,
                                                     std::uint32_t a, std::uint32_t b);
std::uint32_t __fastcall CGameData_FindNearbySettlerType29(CGameData* world, void* edx, std::uint32_t settler,
                                                           std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall CGameData_ClearSettlerJob(CGameData* world, void* edx, std::uint32_t settler);
std::uint32_t __fastcall CGameData_SetOwnerOfFreeTile(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                      std::uint32_t owner);
std::uint32_t __stdcall NeighbourX(std::uint32_t x, std::uint32_t direction);
std::uint32_t __stdcall NeighbourY(std::uint32_t y, std::uint32_t direction);
std::uint32_t __fastcall MapEffects_SpawnArrow(layout::MapEffectTable* self, void* edx, std::uint32_t, std::uint32_t,
                                               std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                               std::uint32_t, std::uint32_t, std::uint32_t);
std::uint32_t __fastcall MapEffects_Spawn_544f80(layout::MapEffectTable* self, void* edx, std::uint32_t a1,
                                                 std::uint32_t a2, std::uint32_t a3, std::uint32_t a4,
                                                 std::uint32_t a5);

namespace {

using layout::Building;
using layout::MapOffset;
using layout::Player;
using layout::Settler;
using layout::Tile;

using Job = SettlerJob;
using S = SettlerType;

// Package C's functions, called through their S3.EXE addresses (Detours
// sends the calls to C's replacements once they are hooked).
using TileFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);
const TileFn kClearTileOwner = reinterpret_cast<TileFn>(0x0051b640);
const TileFn kRecalculateTerritory = reinterpret_cast<TileFn>(0x00519a90);

// The building layout lists' end markers (table 0x6aa170).
constexpr std::uint32_t kLayoutEnd1 = 0x80000000u, kLayoutEnd2 = 0x80000001u, kLayoutSkip = 0x80000002u,
                        kLayoutEnd = 0x80000003u;

// The jobs UpdateSoldier deals with. Placeholders of enums.h whose role is
// clear from this function get a local name.
constexpr Job kJobFighting = Job::Job142;          // 0x8e: set on each attack (back to UnitIdle after)
constexpr Job kJobTowerGuard = Job::Job173;        // 0xad: the guard of a military building (Building::guard)
constexpr Job kJobInGarrison = Job::GuardBuilding;  // 0xae: in a building's garrison chain
constexpr Job kJobKilledInTower = Job::Job175;     // 0xaf: a garrison soldier killed while others remain
constexpr Job kJobDying = Job::Job54;              // 0x36: killed (animation kDyingAnimation)
constexpr Job kJobSwordsmanInTower = Job::Job23;   // 0x17: the jobs a tower's conqueror gets, by class
constexpr Job kJobBowmanInTower = Job::Job140;     // 0x8c: shoots from the tower (longer range)
constexpr Job kJobPikemanInTower = Job::Job141;    // 0x8d
constexpr Job kJobSevenTilePart = Job::Job304;     // 0x130: a part of a seven-tile unit (CreateSevenTileUnit)
constexpr Job kJobShielded = Job::Job309;          // 0x135: a donkey's job in which a hit does no damage
constexpr Job kJobShieldHit = Job::Job310;         // 0x136: ... and the job it gets when hit

// Settler::flags bits as UpdateSoldier uses them (layout.h).
constexpr std::uint8_t kFlagHalfDamage = layout::kSettlerHalfDamage;
constexpr std::uint8_t kFlagDoubleDamage = layout::kSettlerDoubleDamage;
constexpr std::uint8_t kFlagStrongHit = layout::kSettlerStrongHit;
constexpr std::uint8_t kFlagThreeQuarters = layout::kSettlerThreeQuarters;
constexpr std::uint8_t kFlagSpotted = layout::kSettlerSpotted;
constexpr std::uint8_t kFlagNoFight = layout::kSettlerNoFight;

// Tile flag 0x02: probably "a settler is on its way to work here" (as in settler_idle.cpp).
constexpr std::uint8_t kTileClaimed = layout::kTileFlag02;

constexpr std::uint8_t kNoOwner = 0xff;  // tile owner, and a free settler record's owner
constexpr std::int32_t kHumanPlayer = 1;     // Player::active
constexpr std::int32_t kComputerPlayer = 2;

// Reach in offset-table distance: attack range and minimum distance by class.
constexpr std::int32_t kSwordsmanRange = 1, kSwordsmanMinimum = 1;
constexpr std::int32_t kBowmanRange = 13, kBowmanMinimum = 6;
constexpr std::int32_t kTowerBowmanRange = 22, kTowerBowmanMinimum = 10;
constexpr std::int32_t kPikemanRange = 2, kPikemanMinimum = 1;
// Phase 2 looks for enemies within 18; bowmen closer than 8 step back.
constexpr std::int32_t kApproachRadius = 18;
constexpr std::uint32_t kBowmanKeepAway = 8;
// Thief-like targets are attacked within 7 (or once spotted).
constexpr std::int32_t kSpotDistance = 7;

// The animation kinds of the attacks (+ direction) and of dying.
constexpr std::uint32_t kDyingAnimation = 70;  // 0x46
// Arrows start and end 20 pixels above the tiles (0x14).
constexpr std::uint32_t kArrowHeight = 20;
// A tower's soldier places are 16 pixels apart.
constexpr std::uint32_t kTowerPlaceWidth = 16;

// Damage: the player's soldier strength scaled by 1/[0x730eec] (256), rounded.
constexpr std::uint32_t kStrengthRounding = 128;
constexpr std::uint32_t kSevenTileKillScore = 5;

// Notification types (NotifyLocalPlayer) and events (QueueEvent).
constexpr std::uint32_t kNoticeUnderAttack = 3;
constexpr std::uint32_t kNoticeTowerSoldierDied = 4;
constexpr std::uint32_t kNoticeTowerWon = 7;
constexpr std::uint32_t kNoticeTowerLost = 8;
constexpr std::uint32_t kEventSettlerDied = 1;
constexpr std::uint32_t kEventTowerCaptured = 3;
constexpr std::uint32_t kEventTowerAttacked = 4;
constexpr std::uint32_t kPendingEventTowerCaptured = 5;

Tile& TileAt(CGameData* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

std::int32_t Team(CGameData* world, std::uint32_t player) {
    return world->players[player].team;
}

// The class number of a settler's type (int32, as the original compares ranges of it).
std::int32_t ClassNumber(const Settler& s) {
    return Value(ClassOf(s));
}

bool In(std::int32_t v, std::int32_t low, std::int32_t high) {
    return v >= low && v <= high;
}

constexpr std::int32_t kSwordsman = Value(SettlerClass::Swordsman);
constexpr std::int32_t kBowman = Value(SettlerClass::Bowman);
constexpr std::int32_t kPikeman = Value(SettlerClass::Pikeman);

// The jobs a target may be in. `farTower`: the tower jobs for pikemen also
// beyond distance 1 (phase 2).
bool AttackableJob(Job job, std::int32_t attackerClass, std::int32_t distance, bool farTower) {
    switch (job) {
    case Job::UnitIdle: case kJobFighting: case Job::UnitWalk: case Job::Job176: case Job::WalkToWork:
    case Job::GeologistWork: case Job::Job213: case Job::PioneerWork: case Job::Job303:
        return true;
    case kJobTowerGuard: case kJobInGarrison:
        return attackerClass == kSwordsman || (attackerClass == kPikeman && (farTower || distance == 1));
    case kJobSevenTilePart: case kJobShielded:
        return attackerClass == kSwordsman || attackerClass == kPikeman;
    default:
        return false;
    }
}

// Removes a settler from its player's idle list for its type (links +0x10/+0x12).
void UnlinkIdle(CGameData* world, Settler& s) {
    if (s.prev != 0) {
        world->settlers[s.prev].next = s.next;
    } else {
        world->players[s.owner].settler_lists[s.art] = s.next;
    }
    if (s.next != 0) {
        world->settlers[s.next].prev = s.prev;
    }
}

}  // namespace

using SoldierFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t settler);

namespace {

void CaptureTower(CGameData* world, std::uint32_t settler, Settler& victim, std::uint32_t victimNumber);

// The attacker hits target `t` (offset entry `entry`); the caller returns 1.
// 0x50ec6f..0x50f9f6 of UpdateSoldier.
void Attack(CGameData* world, std::uint32_t settler, std::uint32_t t, const MapOffset& entry) {
    Settler& s = world->settlers[settler];
    // The direction towards the target: the entry's sector + 4 (mod 6).
    std::uint32_t direction = static_cast<std::uint32_t>(entry.sector) + 4;
    if (static_cast<std::int32_t>(direction) >= static_cast<std::int32_t>(kDirectionCount)) {
        direction -= kDirectionCount;
    }
    if (JobOf(s) != kJobInGarrison && JobOf(s) != kJobBowmanInTower) {
        SetJob(s, kJobFighting);
    }

    // 1. The hit by soldier type: attack animation and base damage; bowmen shoot an arrow.
    std::uint32_t damage = 0;  // the original's is uninitialized for other types
    const Settler& e = world->settlers[t];
    const std::uint32_t tick = g_gameTick;
    auto* const effects = &world->map_effects;
    auto melee = [&](std::uint32_t animation, std::uint32_t base, std::uint32_t strong) {
        damage = base;
        CGameData_SetSettlerAnimation(world, nullptr, settler, direction + animation);
        if ((s.flags & kFlagStrongHit) != 0) {
            damage = strong;
            s.counter = static_cast<std::uint16_t>((static_cast<std::uint32_t>(s.counter) * 5) / 8);
        }
    };
    // An arrow from the soldier's place in its tower (table 0x659738: 12
    // places per race and building, indexed by the slot byte of
    // EnterBuildingSlot). The screen positions are computed in the
    // original's order (target first).
    auto towerArrow = [&]() {
        const std::uint32_t slot = s.slot;
        const std::uint32_t place = slot & 7;
        const std::uint32_t index = place + 12 * (3 * (slot >> 5) + ((slot >> 3) & 3));
        const std::uint32_t offsetX = static_cast<std::uint32_t>(g_towerSlotOffsets[index].x) -
                                      kTowerPlaceWidth * (place + 1);
        const std::uint32_t offsetY = static_cast<std::uint32_t>(g_towerSlotOffsets[index].y);
        const std::uint32_t ex = e.x, ey = e.y, sx = s.x, sy = s.y;
        const std::uint32_t toY = CGameData_TileScreenY(world, nullptr, ex, ey) - kArrowHeight;
        const std::uint32_t toX = CGameData_TileScreenX(world, nullptr, ex, ey);
        const std::uint32_t fromY = CGameData_TileScreenY(world, nullptr, sx, sy) + offsetY - kArrowHeight;
        const std::uint32_t fromX = CGameData_TileScreenX(world, nullptr, sx, sy) + offsetX;
        MapEffects_SpawnArrow(effects, nullptr, tick, fromX, fromY, sx, sy, toX, toY, ex, ey);
    };
    auto arrow = [&](bool fromTarget) {
        const std::uint32_t ex = e.x, ey = e.y, sx = s.x, sy = s.y;
        const std::uint32_t toY = CGameData_TileScreenY(world, nullptr, ex, ey) - kArrowHeight;
        const std::uint32_t toX = CGameData_TileScreenX(world, nullptr, ex, ey);
        const std::uint32_t fromY = CGameData_TileScreenY(world, nullptr, sx, sy) - kArrowHeight;
        const std::uint32_t fromX = CGameData_TileScreenX(world, nullptr, sx, sy);
        // Bowmen 2 and 3 pass the target's tile as the arrow's start tile (the original's pushes).
        MapEffects_SpawnArrow(effects, nullptr, tick, fromX, fromY, fromTarget ? ex : sx, fromTarget ? ey : sy, toX,
                              toY, ex, ey);
    };
    switch (TypeOf(s)) {
    case S::Swordsman1: melee(34, 10, 15); break;
    case S::Swordsman2: melee(46, 14, 21); break;
    case S::Swordsman3: melee(58, 20, 30); break;
    case S::Bowman1:
        damage = 5;
        CGameData_SetSettlerAnimation(world, nullptr, settler, direction + 40);
        if (JobOf(s) == kJobBowmanInTower) {
            towerArrow();
        } else {
            arrow(false);
        }
        break;
    case S::Bowman2:
    case S::Bowman3: {
        const bool second = TypeOf(s) == S::Bowman2;
        damage = second ? 6 : 7;
        CGameData_SetSettlerAnimation(world, nullptr, settler, direction + (second ? 52 : 64));
        if (JobOf(s) == kJobBowmanInTower) {
            towerArrow();
        } else {
            arrow(true);
        }
        break;
    }
    case S::Pikeman1: damage = 4; CGameData_SetSettlerAnimation(world, nullptr, settler, direction + 137); break;
    case S::Pikeman2: damage = 5; CGameData_SetSettlerAnimation(world, nullptr, settler, direction + 143); break;
    case S::Pikeman3: damage = 6; CGameData_SetSettlerAnimation(world, nullptr, settler, direction + 149); break;
    default: break;
    }

    // 2. Damage modifiers (0x50f1d0..).
    const std::uint8_t landOwner = TileAt(world, s.x, s.y).owner;
    if (landOwner == kNoOwner || Team(world, landOwner) != Team(world, s.owner)) {
        if ((s.flags & kFlagDoubleDamage) != 0) {
            damage *= 2;
        }
        if ((s.flags & kFlagHalfDamage) != 0) {
            damage >>= 1;
        }
        damage = (static_cast<std::uint32_t>(world->players[s.owner].soldier_strength) * damage + kStrengthRounding) /
                 g_damageDivisor;
    }
    // Pikemen and the parts of seven-tile units take half.
    const SettlerClass targetClass = ClassOf(world->settlers[t]);
    if (targetClass == SettlerClass::Pikeman || targetClass == SettlerClass::Class41) {
        damage >>= 1;
    }
    if ((s.flags & kFlagThreeQuarters) != 0) {
        damage = (damage * 3) >> 2;
    }
    if (JobOf(world->settlers[t]) == kJobTowerGuard) {
        CGameData_QueueEvent(world, nullptr, kEventTowerAttacked, world->settlers[t].building, world->settlers[t].owner,
                             s.owner);
    }
    if (damage == 0) {
        damage = 1;
    }

    // 3. Who takes the hit: a seven-tile unit's part passes it on to the
    // catapult nearby (FindNearbySettlerType29).
    std::uint32_t victimNumber = t;
    if (JobOf(world->settlers[t]) == kJobSevenTilePart) {
        CGameData_NotifyLocalPlayer(world, nullptr, world->settlers[t].owner, kNoticeUnderAttack, s.x, s.y);
        victimNumber = CGameData_FindNearbySettlerType29(world, nullptr, t, s.x, s.y);
    } else {
        CGameData_NotifyLocalPlayer(world, nullptr, world->settlers[t].owner, kNoticeUnderAttack, world->settlers[t].x,
                                    world->settlers[t].y);
    }
    Settler& victim = world->settlers[victimNumber];
    if (JobOf(victim) == kJobShielded) {
        SetJob(victim, kJobShieldHit);
        damage = 0;
    }
    victim.flags &= static_cast<std::uint8_t>(~kFlagNoFight);
    // In campaigns computer players' soldiers take half again.
    if (g_campaignMission != 0 && (GetMemberByte189(&g_config, nullptr) & 0xff) != 0 &&
        world->players[victim.owner].active == kComputerPlayer) {
        damage += damage >> 1;
    }
    if (victim.health >= damage) {
        victim.health = static_cast<std::uint8_t>(victim.health - damage);
        return;
    }

    // 4. The victim dies (0x50f4a7..).
    const Job job = JobOf(victim);
    if (job == kJobTowerGuard || job == kJobInGarrison) {
        CaptureTower(world, settler, victim, victimNumber);
        return;
    }
    if (job == Job::WalkToWork || job == Job::ArriveAtWork) {
        // A soldier on its way to a building: one fewer of its class coming.
        Building& b = world->buildings[victim.building];
        switch (ClassOf(victim)) {
        case SettlerClass::Swordsman: --b.military.class1_coming; break;
        case SettlerClass::Bowman: --b.military.class2_coming; break;
        case SettlerClass::Pikeman: --b.military.class3_coming; break;
        default: break;
        }
    }
    switch (JobOf(victim)) {
    case Job::UnitIdle: case Job::UnitWalk: case Job::Job176: case kJobFighting: case Job::Shoot:
    case Job::PioneerWork: case Job::Job303: case Job::GeologistWork: case Job::Job213:
        UnlinkIdle(world, victim);
        break;
    default:
        break;
    }
    if (TypeOf(victim) == S::Pioneer || TypeOf(victim) == S::Geologist) {
        // Free the tile it walked to or worked on.
        if (JobOf(victim) == Job::UnitWalk || JobOf(victim) == Job::Job176) {
            TileAt(world, victim.target_x, victim.target_y).flags &= static_cast<std::uint8_t>(~kTileClaimed);
        }
        if (JobOf(victim) == Job::GeologistWork || JobOf(victim) == Job::Job213 || JobOf(victim) == Job::PioneerWork ||
            JobOf(victim) == Job::Job303) {
            TileAt(world, victim.x, victim.y).flags &= static_cast<std::uint8_t>(~kTileClaimed);
        }
    }
    if (TypeOf(victim) == S::Catapult) {
        // A seven-tile unit: all its parts go.
        CGameData_RemoveSettlerFromLists(world, nullptr, victimNumber);
        static constexpr std::int32_t kParts[7][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}, {-1, 0}, {0, -1}, {-1, -1}};
        for (const auto& p : kParts) {
            world->settlers[TileAt(world, victim.x + p[0], victim.y + p[1]).settler].owner = kNoOwner;
        }
        for (const auto& p : kParts) {
            TileAt(world, victim.x + p[0], victim.y + p[1]).settler = 0;
        }
        g_playerTallies[s.owner].unk14 += kSevenTileKillScore;
        const std::uint32_t screenY = CGameData_TileScreenY(world, nullptr, victim.x, victim.y);
        const std::uint32_t screenX = CGameData_TileScreenX(world, nullptr, victim.x, victim.y);
        MapEffects_Spawn_544f80(effects, nullptr, g_gameTick, screenX, screenY, victim.x, victim.y);
        return;
    }
    ++g_playerTallies[s.owner].unk14;
    CGameData_QueueEvent(world, nullptr, kEventSettlerDied, victimNumber, victim.art, victim.owner);
    CGameData_RemoveSettlerFromLists(world, nullptr, victimNumber);
    SetJob(victim, kJobDying);
    CGameData_SetSettlerAnimation(world, nullptr, victimNumber, kDyingAnimation);
}

// A tower's guard or garrison soldier dies (0x50f7a0..): it leaves the
// garrison; if the building is then empty, the attacker takes it over.
void CaptureTower(CGameData* world, std::uint32_t settler, Settler& victim, std::uint32_t victimNumber) {
    Settler& s = world->settlers[settler];
    CGameData_NotifyLocalPlayer(world, nullptr, victim.owner, kNoticeTowerSoldierDied, victim.x, victim.y);
    const std::uint32_t tower = victim.building;
    if (JobOf(victim) == kJobInGarrison) {
        std::uint16_t& head = world->buildings[tower].garrison;  // the garrison list
        if (head == victimNumber) {
            head = victim.next;
        } else {
            std::uint32_t p = head;
            while (world->settlers[p].next != victimNumber) {
                p = world->settlers[p].next;
            }
            world->settlers[p].next = victim.next;
        }
    }
    if (JobOf(victim) == kJobTowerGuard) {
        world->buildings[tower].guard = 0;
    }
    Building& b = world->buildings[tower];
    if (b.garrison != 0) {
        SetJob(victim, kJobKilledInTower);
        CGameData_SetSettlerAnimation(world, nullptr, victimNumber, kDyingAnimation);
        return;
    }

    // The tower is empty: its soldiers on the way are called off.
    std::uint32_t left =
        static_cast<std::uint32_t>(b.military.class1_coming) + b.military.class3_coming + b.military.class2_coming;
    for (std::uint32_t i = 1; i <= world->settler_high_water; ++i) {
        const Settler& r = world->settlers[i];
        if (r.owner == kNoOwner || r.building != tower || ClassOf(r) == SettlerClass::Civilian) {
            continue;
        }
        const Job job = JobOf(r);
        if (job == Job::WalkToWork || job == Job::ArriveAtWork || job == Job::Job16 || job == Job::Job106 ||
            job == Job::Job139) {
            CGameData_ClearSettlerJob(world, nullptr, i);
            --left;
        }
    }
    if (left != 0) {
        GameDebugLog(texts::kSoldiersLeft, left);
    }

    // The attacker takes the tower.
    UnlinkIdle(world, s);
    CGameData_RemoveSettlerFromLists(world, nullptr, settler);
    GameDebugLog(texts::kWinsTower, static_cast<std::uint32_t>(s.owner), static_cast<std::uint32_t>(victim.owner));
    CGameData_NotifyLocalPlayer(world, nullptr, s.owner, kNoticeTowerWon, s.x, s.y);
    CGameData_NotifyLocalPlayer(world, nullptr, victim.owner, kNoticeTowerLost, victim.x, victim.y);
    CGameData_QueuePendingEvent(world, nullptr, kPendingEventTowerCaptured, tower, b.owner);
    CGameData_QueueEvent(world, nullptr, kEventTowerCaptured, tower, b.owner, s.owner);
    b.owner = s.owner;
    // A new guard (type 37, class 99) at the dead one's place.
    const std::uint32_t guard = CGameData_CreateSettler(world, nullptr, victim.x, victim.y, Value(S::Type37), s.owner);
    SetJob(world->settlers[guard], kJobTowerGuard);
    world->settlers[guard].building = static_cast<std::uint16_t>(tower);
    world->settlers[guard].health = 1;
    b.guard = static_cast<std::uint16_t>(guard);

    // Re-own the building's land: the layout's second and third lists.
    const std::uint32_t* item = g_buildingLayouts[kBuildingTypeCount * static_cast<std::uint32_t>(b.race) + b.art] + 1;
    if (item[-1] != kLayoutEnd1) {
        while (*item++ != kLayoutEnd1) {
        }
    }
    auto reown = [&](const std::uint32_t* pair) {
        // The original reads the building's position again for each call.
        const std::uint32_t x = world->buildings[tower].x + pair[0], y = world->buildings[tower].y + pair[1];
        kClearTileOwner(world, nullptr, x, y);
        CGameData_SetOwnerOfFreeTile(world, nullptr, world->buildings[tower].x + pair[0],
                                     world->buildings[tower].y + pair[1], s.owner);
    };
    while (item[0] != kLayoutEnd2) {
        reown(item);
        item += 2;
    }
    ++item;
    if (item[0] != kLayoutEnd) {
        do {
            if (item[0] == kLayoutSkip) {
                ++item;
            }
            reown(item);
            item += 2;
        } while (item[0] != kLayoutEnd);
    }

    // The attacker is the new garrison.
    b.garrison = static_cast<std::uint16_t>(settler);
    b.military.class1_coming = 0;
    b.military.class1_wanted = 0;
    b.military.class2_coming = 0;
    b.military.class2_wanted = 0;
    b.military.class3_coming = 0;
    b.military.class3_wanted = 0;
    switch (ClassOf(s)) {
    case SettlerClass::Swordsman: SetJob(s, kJobSwordsmanInTower); b.military.class1_wanted = 1; break;
    case SettlerClass::Bowman: SetJob(s, kJobBowmanInTower); b.military.class2_wanted = 1; break;
    case SettlerClass::Pikeman: SetJob(s, kJobPikemanInTower); b.military.class3_wanted = 1; break;
    default: break;
    }
    TileAt(world, s.x, s.y).settler = 0;
    s.counter = 0;
    s.building = static_cast<std::uint16_t>(tower);
    s.next = 0;
    kRecalculateTerritory(world, nullptr, world->buildings[tower].x, world->buildings[tower].y);
    SetJob(victim, kJobDying);
    CGameData_SetSettlerAnimation(world, nullptr, victimNumber, kDyingAnimation);
}

// Whether `e` is a valid target (phase 1 and 2 differ for classes 20..29).
// `distance` is the offset entry's. Thief-like targets get kFlagSpotted.
bool ValidTarget(CGameData* world, const Settler& s, Settler& e, std::int32_t distance, bool phase2) {
    const std::int32_t c = ClassNumber(e);
    // Soldiers (classes 1..9).
    bool ok = In(c, 1, 9);
    // Geologists, pioneers, mages (20..29): in phase 2 only off their own land.
    if (!ok && In(c, 20, 29)) {
        ok = !phase2 || TileAt(world, e.x, e.y).owner != e.owner;
    }
    // Thieves (10..19): when near, or once spotted.
    if (!ok && In(c, 10, 19)) {
        ok = distance <= kSpotDistance || (e.flags & kFlagSpotted) != 0;
    }
    // Tower guards (class 99), seven-tile parts (41), and donkeys in the
    // shielded job for swordsmen and pikemen.
    if (!ok) {
        ok = c == Value(SettlerClass::Class99) || c == Value(SettlerClass::Class41) ||
             (c == Value(SettlerClass::Donkey) && JobOf(e) == kJobShielded &&
              (ClassNumber(s) == kSwordsman || ClassNumber(s) == kPikeman));
    }
    if (!ok) {
        return false;
    }
    if (In(c, 10, 19)) {
        e.flags |= kFlagSpotted;
    }
    return true;
}

// Whether the tile (tx, ty) lies on the map (the original's signed compares).
bool OnMapSigned(std::uint32_t tx, std::uint32_t ty, std::int32_t mapSize) {
    return static_cast<std::int32_t>(tx) >= 0 && static_cast<std::int32_t>(tx) < mapSize &&
           static_cast<std::int32_t>(ty) >= 0 && static_cast<std::int32_t>(ty) < mapSize;
}

// Phase 1 (0x50eaa0..): attack the first valid enemy in reach. True if it attacked.
bool AttackInReach(CGameData* world, std::uint32_t settler, std::int32_t range, std::int32_t minimum) {
    Settler& s = world->settlers[settler];
    const std::int32_t cls = ClassNumber(s);
    const std::uint32_t x = s.x, y = s.y;
    const auto mapSize = static_cast<std::int32_t>(world->map_size);
    if ((s.flags & kFlagNoFight) != 0 || CGameData_IsOtherPlayerNear(world, nullptr, x, y, range, s.owner) == 0 ||
        world->offsets[1].distance > range) {
        return false;
    }
    for (std::uint32_t k = 1; world->offsets[k].distance <= range; ++k) {
        const MapOffset& entry = world->offsets[k];
        if (entry.distance < minimum) {
            continue;
        }
        const std::uint32_t tx = entry.dx + x, ty = entry.dy + y;
        if (!OnMapSigned(tx, ty, mapSize)) {
            continue;
        }
        const std::uint32_t t = TileAt(world, tx, ty).settler;
        if (t == 0) {
            continue;
        }
        Settler& e = world->settlers[t];
        if (Team(world, s.owner) == Team(world, e.owner)) {
            continue;
        }
        if (!ValidTarget(world, s, e, entry.distance, false)) {
            continue;
        }
        // A tower bowman shoots at computer players' settlers only on its own land.
        if (JobOf(s) == kJobBowmanInTower && world->players[e.owner].active == kComputerPlayer &&
            TileAt(world, e.x, e.y).owner != s.owner) {
            continue;
        }
        if (!AttackableJob(JobOf(e), cls, entry.distance, false)) {
            continue;
        }
        Attack(world, settler, t, entry);
        return true;
    }
    return false;
}

// Whether `e` is an enemy for phase 2: human players fight every other
// team; computer players too, unless both stand on their own team's land.
bool IsEnemyToApproach(CGameData* world, const Settler& s, const Settler& e) {
    const Player& me = world->players[s.owner];
    if (me.active == kHumanPlayer) {
        return Team(world, e.owner) != me.team;
    }
    if (me.active != kComputerPlayer) {
        return false;
    }
    const std::int32_t theirs = Team(world, e.owner);
    if (me.team == theirs) {
        return false;
    }
    const std::uint8_t myLand = TileAt(world, s.x, s.y).owner;
    const std::uint8_t theirLand = TileAt(world, e.x, e.y).owner;
    const bool bothAtHome = myLand != kNoOwner && theirLand != kNoOwner && Team(world, myLand) == me.team &&
                            Team(world, theirLand) == theirs;
    return !bothAtHome;
}

// Phase 2 (0x50fa00..): walk towards the first valid enemy within 18.
void ApproachEnemy(CGameData* world, std::uint32_t settler) {
    Settler& s = world->settlers[settler];
    const std::int32_t cls = ClassNumber(s);
    const std::uint32_t x = s.x, y = s.y;
    const auto mapSize = static_cast<std::int32_t>(world->map_size);
    if (CGameData_IsOtherPlayerNear(world, nullptr, x, y, kApproachRadius, s.owner) == 0 ||
        world->offsets[1].distance > kApproachRadius) {
        return;
    }
    for (std::uint32_t k = 1; world->offsets[k].distance <= kApproachRadius; ++k) {
        const MapOffset& entry = world->offsets[k];
        const std::uint32_t tx = entry.dx + x, ty = entry.dy + y;
        if (!OnMapSigned(tx, ty, mapSize)) {
            continue;
        }
        const std::uint32_t t = TileAt(world, tx, ty).settler;
        if (t == 0) {
            continue;
        }
        Settler& e = world->settlers[t];
        if (!IsEnemyToApproach(world, s, e)) {
            continue;
        }
        if (!ValidTarget(world, s, e, entry.distance, true)) {
            continue;
        }
        if (!AttackableJob(JobOf(e), cls, entry.distance, true)) {
            continue;
        }
        // Next to the enemy; bowmen that are already close go next to the
        // tile beside themselves away from it.
        std::uint32_t nearX = tx, nearY = ty;
        if (cls == kBowman && TileDistance(s.x, s.y, e.x, e.y) < kBowmanKeepAway) {
            // Signed arithmetic on the int32 sector, as in the original.
            const std::uint32_t away = static_cast<std::uint32_t>((entry.sector + 3) % 6);
            nearX = NeighbourX(s.x, away);
            nearY = NeighbourY(s.y, away);
        }
        // The first neighbour (offset entries 1..6) in the soldier's walk
        // region that is free (any, next to a catapult).
        bool sent = false;
        for (std::uint32_t j = 1; j <= kDirectionCount && !sent; ++j) {
            const std::uint32_t nx = world->offsets[j].dx + nearX, ny = world->offsets[j].dy + nearY;
            if (nx >= world->map_size || ny >= world->map_size) {
                continue;
            }
            const Tile& spot = TileAt(world, nx, ny);
            if (spot.walk_region != TileAt(world, s.x, s.y).walk_region) {
                continue;
            }
            if (spot.settler != 0 && ClassOf(e) != SettlerClass::Catapult) {
                continue;
            }
            CGameData_SetSettlerTarget(world, nullptr, settler, nx, ny);
            sent = true;
        }
        if (!sent) {
            continue;
        }
        SetJob(s, Job::UnitWalk);
        s.walk_state = 0;
        return;
    }
}

}  // namespace

// Purpose: One step of a soldier: attack an enemy within reach (melee or arrows; killing it, or taking
// an enemy tower when its last defender falls), otherwise walk towards an enemy nearby. Returns 1 if
// it attacked.
std::uint32_t __fastcall CGameData_UpdateSoldier(CGameData* world, void*, std::uint32_t settler) {
    Settler& s = world->settlers[settler];
    std::int32_t range = 0, minimum = 0;  // minimum: uninitialized in the original for other classes
    switch (ClassOf(s)) {
    case SettlerClass::Swordsman:
        range = kSwordsmanRange;
        minimum = kSwordsmanMinimum;
        break;
    case SettlerClass::Bowman:
        range = kBowmanRange;
        minimum = kBowmanMinimum;
        if (JobOf(s) == kJobBowmanInTower) {
            range = kTowerBowmanRange;
            minimum = kTowerBowmanMinimum;
        }
        break;
    case SettlerClass::Pikeman:
        range = kPikemanRange;
        minimum = kPikemanMinimum;
        break;
    default: break;
    }
    if (AttackInReach(world, settler, range, minimum)) {
        return 1;
    }
    if (JobOf(s) == kJobInGarrison || JobOf(s) == kJobBowmanInTower || (s.flags & kFlagNoFight) != 0) {
        return 0;
    }
    ApproachEnemy(world, settler);
    return 0;
}

S3RE_HOOK(0x0050e9f0, CGameData_UpdateSoldier, SoldierFn, Original_CGameData_UpdateSoldier, CGameData_UpdateSoldier);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestSettlers = 0x400;
constexpr std::uint32_t kTestBuildings = 0x40;

// S3.EXE data the function writes (tallies) or reads (campaign mission,
// config byte): saved, set per case, compared and restored.
struct Globals {
    static constexpr std::size_t kTallyBytes = 20 * 0x44;
    std::vector<std::uint8_t> saved = std::vector<std::uint8_t>(kTallyBytes);
    std::int32_t mission = g_campaignMission;
    std::uint8_t config = g_config->unk189;
    Globals() { std::memcpy(saved.data(), Tallies(), kTallyBytes); }
    ~Globals() {
        std::memcpy(Tallies(), saved.data(), kTallyBytes);
        g_campaignMission = mission;
        g_config->unk189 = config;
    }
    static void* Tallies() { return &g_playerTallies; }
};

constexpr std::uint8_t kSoldierTypes[] = {Value(S::Swordsman1), Value(S::Swordsman2), Value(S::Swordsman3),
                                          Value(S::Bowman1),    Value(S::Bowman2),    Value(S::Bowman3),
                                          Value(S::Pikeman1),   Value(S::Pikeman2),   Value(S::Pikeman3)};
// Targets: soldiers, and types of classes 20 (geologist), 10 (thief), 99
// (type 37), 30 (donkey), 21 (pioneer), 40 (catapult), 41 (type 42), 0 (digger).
constexpr std::uint8_t kTargetTypes[] = {Value(S::Swordsman1), Value(S::Swordsman2), Value(S::Bowman1),
                                         Value(S::Bowman2),    Value(S::Pikeman1),   Value(S::Pikeman3),
                                         Value(S::Geologist),  Value(S::Thief),      Value(S::Type37),
                                         Value(S::Donkey),     Value(S::Pioneer),    Value(S::Catapult),
                                         Value(S::Type42),     Value(S::Digger)};
constexpr std::uint16_t kTargetJobs[] = {
    Value(Job::UnitIdle),  Value(kJobFighting),   Value(Job::UnitWalk),      Value(Job::Job176),
    Value(Job::WalkToWork), Value(Job::ArriveAtWork), Value(kJobTowerGuard), Value(kJobInGarrison),
    Value(kJobTowerGuard), Value(kJobInGarrison), Value(kJobSevenTilePart), Value(Job::GeologistWork),
    Value(Job::Job303),    Value(kJobShielded),   Value(Job::Job16),         Value(Job::MinerEatCoalMine),
    Value(Job::Shoot)};
constexpr std::uint16_t kSoldierJobs[] = {Value(kJobBowmanInTower), Value(kJobInGarrison),     Value(kJobFighting),
                                          Value(Job::UnitWalk),     Value(Job::UnitIdle),      Value(kJobSwordsmanInTower),
                                          Value(kJobPikemanInTower)};

void SetUpWorld(CGameData* w) {
    w->map_size = 384;
    w->settler_high_water = kTestSettlers;
    w->building_high_water = kTestBuildings;
    w->presence_max_player = 3;  // IsOtherPlayerNear's loop bound
    // The offset table: rings by TileDistance up to radius 25, a random
    // direction sector per entry (from the world's random bytes).
    std::uint32_t k = 0;
    for (std::int32_t r = 0; k < 2000; ++r) {
        for (std::int32_t dx = -r; dx <= r && k < 2000; ++dx) {
            for (std::int32_t dy = -r; dy <= r && k < 2000; ++dy) {
                if (TileDistance(0, 0, dx, dy) != static_cast<std::uint32_t>(r)) {
                    continue;
                }
                MapOffset& o = w->offsets[k];
                o.dx = dx;
                o.dy = dy;
                o.distance = r;
                o.sector = static_cast<std::int32_t>(static_cast<std::uint32_t>(o.sector) % 6);
                ++k;
            }
        }
    }
    for (; k < 20000; ++k) {
        w->offsets[k].distance = 100;
    }
    for (std::uint32_t s = 0; s < 0x7d00; ++s) {
        Settler& r = w->settlers[s];
        r.x = static_cast<std::uint16_t>(20 + r.x % 340);
        r.y = static_cast<std::uint16_t>(20 + r.y % 340);
        r.target_x = static_cast<std::uint16_t>(r.target_x % 384);
        r.target_y = static_cast<std::uint16_t>(r.target_y % 384);
        r.building = static_cast<std::uint16_t>(r.building % kTestBuildings);
        r.next = static_cast<std::uint16_t>(r.next % kTestSettlers);
        r.prev = static_cast<std::uint16_t>(r.prev % kTestSettlers);
        r.owner = static_cast<std::uint8_t>(s < kTestSettlers ? r.owner % 4 : 1);
        r.art = static_cast<std::uint8_t>(r.art % 0x40);
        r.job = Value(Job::MinerEatCoalMine);  // no attackable job: only the settlers a case places are targets
    }
    for (std::uint32_t b = 0; b <= kTestBuildings; ++b) {
        Building& r = w->buildings[b];
        r.x = static_cast<std::uint16_t>(30 + r.x % 320);
        r.y = static_cast<std::uint16_t>(30 + r.y % 320);
        // The large tower, mill (no military building) and castle (16..18).
        r.art = static_cast<std::uint8_t>(Value(BuildingType::LargeTower) + r.art % 3);
        r.race = static_cast<std::uint8_t>(r.race % 3);
        r.owner = static_cast<std::uint8_t>(r.owner % 4);
        r.garrison = static_cast<std::uint16_t>(r.garrison % kTestSettlers);
    }
    for (auto& lengths : w->player_list_lengths) {  // RemoveSettlerFromLists walks these
        for (std::int32_t& length : lengths) {
            length = static_cast<std::int32_t>(static_cast<std::uint32_t>(length) % 100);
        }
    }
    for (std::uint32_t p = 0; p < 20; ++p) {
        Player& r = w->players[p];
        r.team = static_cast<std::int32_t>(p % 3);
        r.active = static_cast<std::int32_t>(1 + (p & 1));
        r.race = static_cast<std::int32_t>(p % 3);
        r.soldier_strength = static_cast<std::uint32_t>(0xe0 + (p * 37) % 0x60);
    }
    for (auto& row : w->tiles) {
        for (Tile& t : row) {
            t.settler = static_cast<std::uint16_t>(t.settler % kTestSettlers);
            t.owner = static_cast<std::uint8_t>(t.owner % 5 == 4 ? 0xff : t.owner % 5);
            t.walk_region = static_cast<std::uint8_t>(1 + t.walk_region % 2);
            t.area = static_cast<std::uint16_t>(t.area % 0x100);
            t.object_number = static_cast<std::uint16_t>(t.object_number % kTestBuildings);
        }
    }
    // The effect table: a few records, empty cell lists.
    layout::MapEffectTable& fx = w->map_effects;
    fx.slots_used = 20;
    fx.blocks_x = 80;
    for (std::uint32_t c = 0; c < 10000; ++c) {
        fx.cell_heads[c] = 0;
    }
    for (std::uint32_t r = 1; r < 5000; ++r) {
        fx.records[r].start_tick = 0;
        fx.records[r].next = 0;
    }
    w->pending_event_index.read = w->pending_event_index.read % 1000;
    w->pending_event_index.write = w->pending_event_index.write % 1000;
    w->event_index.read = w->event_index.read % 1000;
    w->event_index.write = w->event_index.write % 1000;
}

void SetUpCase(test::Context& ctx, test::WorldPair& worlds, std::uint32_t soldier, std::vector<std::uint32_t>& placed) {
    auto& rng = ctx.rng();
    // The last case's settlers leave (their garrison lists may be gone).
    for (const std::uint32_t old : placed) {
        worlds.Both([&](CGameData* w) {
            Settler& r = w->settlers[old];
            if (w->tiles[r.x][r.y].settler == old) {
                w->tiles[r.x][r.y].settler = 0;
            }
            r.job = Value(Job::MinerEatCoalMine);
        });
    }
    placed.clear();
    placed.push_back(soldier);
    const std::uint8_t art = kSoldierTypes[rng.Below(sizeof(kSoldierTypes))];
    const std::uint16_t job = kSoldierJobs[rng.Below(sizeof(kSoldierJobs) / 2)];
    const auto flags = static_cast<std::uint8_t>(rng.Next32() & (rng.Chance(1, 4) ? 0xff : 0xdf));
    const std::uint32_t x = 40 + rng.Below(300), y = 40 + rng.Below(300);
    const auto owner = static_cast<std::uint8_t>(rng.Below(4));
    // A tower slot as EnterBuildingSlot makes it: race << 5, + 8 / 0x10 by
    // building type, + place 0..3 (indexes the tower arrow offsets).
    const auto slot = static_cast<std::uint8_t>((rng.Below(3) << 5) | (rng.Below(3) << 3) | rng.Below(4));
    worlds.Both([&](CGameData* w) {
        Settler& s = w->settlers[soldier];
        s.slot = slot;
        s.art = art;
        s.job = job;
        s.flags = flags;
        s.owner = owner;
        s.x = static_cast<std::uint16_t>(x);
        s.y = static_cast<std::uint16_t>(y);
        w->tiles[x][y].settler = static_cast<std::uint16_t>(soldier);
    });
    // Enemies (and friends) around: mostly within 3, some up to 20.
    const std::uint32_t count = rng.Below(6);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t reach = rng.Chance(2, 3) ? 3 : 20;
        const std::uint32_t ex = x + rng.Below(2 * reach + 1) - reach, ey = y + rng.Below(2 * reach + 1) - reach;
        if (ex == x && ey == y) {
            continue;
        }
        const std::uint32_t e = 1 + rng.Below(kTestSettlers - 1);
        if (e == soldier) {
            continue;
        }
        const std::uint8_t eart = kTargetTypes[rng.Below(sizeof(kTargetTypes))];
        const std::uint16_t ejob = kTargetJobs[rng.Below(sizeof(kTargetJobs) / 2)];
        const auto eowner = static_cast<std::uint8_t>(rng.Below(4));
        const auto health = static_cast<std::uint8_t>(rng.Chance(1, 2) ? rng.Below(8) : rng.Next32());
        const auto eflags = static_cast<std::uint8_t>(rng.Next32());
        const std::uint32_t building = 1 + rng.Below(kTestBuildings - 1);
        const bool empty = rng.Chance(1, 2);
        placed.push_back(e);
        placed.push_back(1 + (e + 1) % (kTestSettlers - 1));
        worlds.Both([&](CGameData* w) {
            Settler& r = w->settlers[e];
            w->tiles[r.x][r.y].settler = 0;
            r.art = eart;
            r.job = ejob;
            r.owner = eowner;
            r.health = health;
            r.flags = eflags;
            r.x = static_cast<std::uint16_t>(ex);
            r.y = static_cast<std::uint16_t>(ey);
            r.building = static_cast<std::uint16_t>(building);
            w->tiles[ex][ey].settler = static_cast<std::uint16_t>(e);
            Building& b = w->buildings[building];
            // Garrisons are never left empty here: a tower capture calls
            // package C's RecalculateTerritory 0x519a90, which needs a
            // consistent world (the emulator spec covers captures with a
            // probe instead).
            const std::uint32_t other = 1 + (e + 1) % (kTestSettlers - 1);
            if (ejob == Value(kJobInGarrison)) {
                if (empty) {  // e at the head, another soldier after it
                    r.next = static_cast<std::uint16_t>(other);
                    b.garrison = static_cast<std::uint16_t>(e);
                } else {
                    r.next = 0;
                    w->settlers[other].next = static_cast<std::uint16_t>(e);
                    b.garrison = static_cast<std::uint16_t>(other);
                }
            } else if (ejob == Value(kJobTowerGuard)) {
                b.garrison = static_cast<std::uint16_t>(other);
            }
        });
    }
}

void Compare(test::Context& ctx, SoldierFn candidate, int cases) {
    test::FakeGraphics gfx;  // every sprite valid, for SetSettlerAnimation
    gfx.MakeEverySpriteValid(7);
    Globals globals;
    test::WorldPair worlds(ctx, sizeof(CGameData));
    worlds.Both(&SetUpWorld);
    DebugLogRecorder logs;
    std::vector<std::uint8_t> before(Globals::kTallyBytes), after(Globals::kTallyBytes);
    std::uint32_t attacks = 0;
    std::vector<std::uint32_t> placed;
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t soldier = 1 + ctx.rng().Below(kTestSettlers - 1);
        SetUpCase(ctx, worlds, soldier, placed);
        g_campaignMission = ctx.rng().Chance(1, 2) ? 0 : 5;
        g_config->unk189 = static_cast<std::uint8_t>(ctx.rng().Below(2));
        std::memcpy(before.data(), Globals::Tallies(), before.size());
        const std::uint32_t expected = Original_CGameData_UpdateSoldier(worlds.original(), nullptr, soldier);
        const std::vector<DebugLogCall> expectedLogs = logs.Take();
        std::memcpy(after.data(), Globals::Tallies(), after.size());
        std::memcpy(Globals::Tallies(), before.data(), before.size());
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, soldier);
        const std::vector<DebugLogCall> actualLogs = logs.Take();
        attacks += expected;
        if (!S3RE_EXPECT_EQ(expected, actual) || !S3RE_EXPECT(expectedLogs == actualLogs) ||
            !S3RE_EXPECT_MEM_EQ(after.data(), Globals::Tallies(), after.size()) ||
            ((i % 32 == 31 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 31 before it): soldier %u", i, soldier);
            return;
        }
    }
    S3RE_EXPECT(attacks > 0);
}

// Wrong: bowmen in towers use the normal range.
std::uint32_t __fastcall WrongUpdateSoldier(CGameData* world, void* edx, std::uint32_t settler) {
    Settler& s = world->settlers[settler];
    if (JobOf(s) == kJobBowmanInTower) {
        SetJob(s, Job::Job139);
        const std::uint32_t result = CGameData_UpdateSoldier(world, edx, settler);
        if (JobOf(s) == Job::Job139) {
            SetJob(s, kJobBowmanInTower);
        }
        return result;
    }
    return CGameData_UpdateSoldier(world, edx, settler);
}

}  // namespace

S3RE_TEST(CGameData_UpdateSoldier_MatchesOriginal) {
    Compare(ctx, &CGameData_UpdateSoldier, 4000);
}
S3RE_TEST(CGameData_UpdateSoldier_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Compare(probe, &WrongUpdateSoldier, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
