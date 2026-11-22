// Soldiers fighting (UpdateSettlers callee, package B).
//
// GameWorld::UpdateSoldier (0x0050e9f0): (settler) -> EAX 1 if it attacked
//   this step, else 0. Only called for settler classes 1..3 (0x658f64:
//   1 melee, 2 bowmen, 3 probably spearmen/other melee); range/minimum
//   distance 1/1, 2/1, bowmen 13/6 (in a tower, job 0x8c: 22/10). ret 4.
//
//   Phase 1 (attack), unless settler flag 0x20 is set: if
//   IsOtherPlayerNear(x, y, range, owner), the offset table is walked from
//   entry 1 while entry.distance <= range; the first entry with distance >=
//   the minimum, on the map, holding a settler of another team (player
//   +0xc) that is a valid target (class 1..9 or 0x14..0x1d; 0xa..0x13 at
//   distance <= 7 or with its flag 0x10; class 0x63 or 0x29; class 0x1e in
//   job 0x135 for melee classes 1/3), for a tower bowman of a computer
//   player only on the tower owner's land, in an attackable job (idle
//   jobs, 3, tower jobs 0xad/0xae for class 1 (or 3 at distance 1),
//   0x130, 0xd4/0xd5/0x12e/0x12f, 0x135 for classes 1/3), is attacked:
//   - job 0x8e (unless 0xae/0x8c), attack animation by type and direction
//     (offset entry's sector + 3), base damage by type (melee types 5/0x20
//     /0x23: 10/14/20, with flag 0x4 15/21/30 and the counter * 5 / 8;
//     bowmen 8/0x1e/0x21: 5/6/7 and an arrow (SpawnArrow; from a tower
//     with an offset from the table 0x659738 by the slot byte); 0xf/0x1f/
//     0x22: 4/5/6);
//   - outside the own team's land: flag 0x2 doubles, 0x1 halves, then
//     damage = (player +0x80 * damage + 0x80) / [0x730eec] (256);
//     targets of class 3 or 0x29 take half, attacker flag 0x8 3/4; at
//     least 1;
//   - a tower soldier (0xad) queues event 4; a target in job 0x130 hands
//     the hit to a type-0x29 settler nearby (FindNearbySettlerType29);
//     notifications type 3; job 0x135 targets take no damage (-> 0x136);
//     in campaigns computer players' soldiers take half again;
//   - health (+3) >= damage: health -= damage. Otherwise the target dies:
//     towers (0xad/0xae) are captured when empty (garrison list +0x8
//     empty: the building's soldiers are cleared, its land re-owned from
//     the layout table 0x6aa170 via ClearTileOwner 0x51b640 /
//     SetOwnerOfFreeTile, RecalculateTerritory 0x519a90, the attacker
//     moves in; events, notifications 7/8, DebugLogs), else the target
//     gets job 0xaf; a type-0x29 unit (seven tiles) is freed with its
//     parts and a MapEffects_Spawn_544f80; others die (event 1,
//     RemoveSettlerFromLists, job 0x36, animation 0x46).
//   Phase 2 (approach), when nothing was attacked and the settler isn't
//   in a tower job (0xae/0x8c) or flag 0x20: within distance 18
//   (IsOtherPlayerNear(x, y, 0x12, owner)), the first enemy (human players:
//   another team; computer players: another team, unless both stand on
//   their own team's land) that is a valid target in an attackable job
//   (as above, 0xad/0xae for class 3 at any distance) gets the settler
//   sent to a free neighbour tile of the same walk region next to it
//   (bowmen closer than 8 to the target: next to the tile beside
//   themselves away from it): SetSettlerTarget, job 0x35, +0x1a = 0.
//
//   Not reproduced: other settler classes leave the minimum distance
//   uninitialized; unknown attacker types leave the damage uninitialized;
//   after a tower capture the original reads the offset entry indexed by
//   the soldier count it left in its loop variable (no effect).
// thiscall (ECX = world).

#include "game/debug_log.h"
#include "game/globals.h"
#include "game/replacements.h"
#include "game/tables.h"
#include "game/texts.h"
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
std::uint32_t __fastcall GameWorld_SetSettlerAnimation(GameWorld* world, void* edx, std::uint32_t settler,
                                                       std::uint32_t kind);
std::uint32_t __fastcall GameWorld_NotifyLocalPlayer(GameWorld* world, void* edx, std::uint32_t player,
                                                     std::uint32_t type, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall GameWorld_IsOtherPlayerNear(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                     std::uint32_t radius, std::uint32_t player);
std::uint32_t __fastcall GameWorld_TileScreenX(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall GameWorld_TileScreenY(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall GameWorld_QueuePendingEvent(GameWorld* world, void* edx, std::uint32_t type,
                                                     std::uint32_t a, std::uint32_t b);
std::uint32_t __fastcall GameWorld_FindNearbySettlerType29(GameWorld* world, void* edx, std::uint32_t settler,
                                                           std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall GameWorld_ClearSettlerJob(GameWorld* world, void* edx, std::uint32_t settler);
std::uint32_t __fastcall GameWorld_SetOwnerOfFreeTile(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y,
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


// Package C's functions, called through their S3.EXE addresses (Detours
// sends the calls to C's replacements once they are hooked).
using TileFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);
const TileFn kClearTileOwner = reinterpret_cast<TileFn>(0x0051b640);
const TileFn kRecalculateTerritory = reinterpret_cast<TileFn>(0x00519a90);


constexpr std::uint32_t kLayoutEnd1 = 0x80000000u, kLayoutEnd2 = 0x80000001u, kLayoutSkip = 0x80000002u,
                        kLayoutEnd = 0x80000003u;


Tile& TileAt(GameWorld* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

std::int32_t Team(GameWorld* world, std::uint32_t player) {
    return world->players[player].team;
}

std::int32_t ClassOf(const Settler& s) {
    return g_settlerTypeClass[s.art];
}

bool In(std::int32_t v, std::int32_t low, std::int32_t high) {
    return v >= low && v <= high;
}

// The jobs a target may be in. `farTower`: jobs 0xad/0xae for class 3 also
// beyond distance 1 (phase 2).
bool AttackableJob(std::uint16_t job, std::int32_t attackerClass, std::int32_t distance, bool farTower) {
    switch (job) {
    case 0x37: case 0x8e: case 0x35: case 0xb0: case 3:
    case 0xd4: case 0xd5: case 0x12e: case 0x12f:
        return true;
    case 0xad: case 0xae:
        return attackerClass == 1 || (attackerClass == 3 && (farTower || distance == 1));
    case 0x130: case 0x135:
        return attackerClass == 1 || attackerClass == 3;
    default:
        return false;
    }
}

// Removes a settler from its player's idle list for its type (links +0x10/+0x12).
void UnlinkIdle(GameWorld* world, Settler& s) {
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

using SoldierFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t settler);

namespace {

// The attacker hits target `t` (offset entry `k`); returns nothing, the
// caller returns 1.
void Attack(GameWorld* world, std::uint32_t settler, std::uint32_t t, const MapOffset& entry) {
    Settler& s = world->settlers[settler];
    std::uint32_t direction = static_cast<std::uint32_t>(entry.sector) + 4;
    if (static_cast<std::int32_t>(direction) >= 6) {
        direction -= 6;
    }
    if (s.job != 0xae && s.job != 0x8c) {
        s.job = 0x8e;
    }
    std::uint32_t damage = 0;  // the original's is uninitialized for other types
    const Settler& e = world->settlers[t];
    const std::uint32_t tick = g_gameTick;
    auto* const effects = &world->map_effects;
    auto melee = [&](std::uint32_t animation, std::uint32_t base, std::uint32_t strong) {
        damage = base;
        GameWorld_SetSettlerAnimation(world, nullptr, settler, direction + animation);
        if ((s.flags & 4) != 0) {
            damage = strong;
            s.counter = static_cast<std::uint16_t>((static_cast<std::uint32_t>(s.counter) * 5) / 8);
        }
    };
    auto towerArrow = [&]() {
        const std::uint32_t slot = s.slot;
        const std::uint32_t index = (slot & 7) + 12 * (3 * (slot >> 5) + ((slot >> 3) & 3));
        const std::uint32_t offsetX = static_cast<std::uint32_t>(g_towerSlotOffsets[index].x) - 16 * ((slot & 7) + 1);
        const std::uint32_t offsetY = static_cast<std::uint32_t>(g_towerSlotOffsets[index].y);
        const std::uint32_t ex = e.x, ey = e.y, sx = s.x, sy = s.y;
        const std::uint32_t a7 = GameWorld_TileScreenY(world, nullptr, ex, ey) - 0x14;
        const std::uint32_t a6 = GameWorld_TileScreenX(world, nullptr, ex, ey);
        const std::uint32_t a3 = GameWorld_TileScreenY(world, nullptr, sx, sy) + offsetY - 0x14;
        const std::uint32_t a2 = GameWorld_TileScreenX(world, nullptr, sx, sy) + offsetX;
        MapEffects_SpawnArrow(effects, nullptr, tick, a2, a3, sx, sy, a6, a7, ex, ey);
    };
    auto arrow = [&](bool fromTarget) {
        const std::uint32_t ex = e.x, ey = e.y, sx = s.x, sy = s.y;
        const std::uint32_t a7 = GameWorld_TileScreenY(world, nullptr, ex, ey) - 0x14;
        const std::uint32_t a6 = GameWorld_TileScreenX(world, nullptr, ex, ey);
        const std::uint32_t a3 = GameWorld_TileScreenY(world, nullptr, sx, sy) - 0x14;
        const std::uint32_t a2 = GameWorld_TileScreenX(world, nullptr, sx, sy);
        // Types 0x1e and 0x21 pass the target's tile as a4/a5 (the original's pushes).
        MapEffects_SpawnArrow(effects, nullptr, tick, a2, a3, fromTarget ? ex : sx, fromTarget ? ey : sy, a6, a7, ex,
                              ey);
    };
    switch (s.art) {
    case 0x05: melee(0x22, 0xa, 0xf); break;
    case 0x20: melee(0x2e, 0xe, 0x15); break;
    case 0x23: melee(0x3a, 0x14, 0x1e); break;
    case 0x08:
        damage = 5;
        GameWorld_SetSettlerAnimation(world, nullptr, settler, direction + 0x28);
        if (s.job == 0x8c) {
            towerArrow();
        } else {
            arrow(false);
        }
        break;
    case 0x1e:
    case 0x21:
        damage = s.art == 0x1e ? 6 : 7;
        GameWorld_SetSettlerAnimation(world, nullptr, settler, direction + (s.art == 0x1e ? 0x34 : 0x40));
        if (s.job == 0x8c) {
            towerArrow();
        } else {
            arrow(true);
        }
        break;
    case 0x0f: damage = 4; GameWorld_SetSettlerAnimation(world, nullptr, settler, direction + 0x89); break;
    case 0x1f: damage = 5; GameWorld_SetSettlerAnimation(world, nullptr, settler, direction + 0x8f); break;
    case 0x22: damage = 6; GameWorld_SetSettlerAnimation(world, nullptr, settler, direction + 0x95); break;
    default: break;
    }

    // Damage modifiers.
    const std::uint8_t landOwner = TileAt(world, s.x, s.y).owner;
    if (landOwner == 0xff || Team(world, landOwner) != Team(world, s.owner)) {
        if ((s.flags & 2) != 0) {
            damage *= 2;
        }
        if ((s.flags & 1) != 0) {
            damage >>= 1;
        }
        damage = (static_cast<std::uint32_t>(world->players[s.owner].soldier_strength) * damage + 0x80) / g_damageDivisor;
    }
    const std::int32_t targetClass = ClassOf(world->settlers[t]);
    if (targetClass == 3 || targetClass == 0x29) {
        damage >>= 1;
    }
    if ((s.flags & 8) != 0) {
        damage = (damage * 3) >> 2;
    }
    if (world->settlers[t].job == 0xad) {
        GameWorld_QueueEvent(world, nullptr, 4, world->settlers[t].building, world->settlers[t].owner, s.owner);
    }
    if (damage == 0) {
        damage = 1;
    }
    std::uint32_t victim = t;
    if (world->settlers[t].job == 0x130) {
        GameWorld_NotifyLocalPlayer(world, nullptr, world->settlers[t].owner, 3, s.x, s.y);
        victim = GameWorld_FindNearbySettlerType29(world, nullptr, t, s.x, s.y);
    } else {
        GameWorld_NotifyLocalPlayer(world, nullptr, world->settlers[t].owner, 3, world->settlers[t].x,
                                    world->settlers[t].y);
    }
    Settler& v = world->settlers[victim];
    if (v.job == 0x135) {
        v.job = 0x136;
        damage = 0;
    }
    v.flags &= 0xdf;
    if (g_campaignMission != 0 && (GetMemberByte189(&g_config, nullptr) & 0xff) != 0 &&
        world->players[v.owner].active == 2) {
        damage += damage >> 1;
    }
    if (v.health >= damage) {
        v.health = static_cast<std::uint8_t>(v.health - damage);
        return;
    }

    // The victim dies.
    const std::uint16_t job = v.job;
    if (job != 0xad && job != 0xae) {
        if (job == 3 || job == 4) {
            Building& b = world->buildings[v.building];
            switch (ClassOf(v)) {
            case 1: --b.military.class1_coming; break;
            case 2: --b.military.class2_coming; break;
            case 3: --b.military.class3_coming; break;
            default: break;
            }
        }
        switch (v.job) {
        case 0x37: case 0x35: case 0xb0: case 0x8e: case 0x151: case 0x12e: case 0x12f: case 0xd4: case 0xd5:
            UnlinkIdle(world, v);
            break;
        default:
            break;
        }
        if (v.art == 0x28 || v.art == 0x16) {
            if (v.job == 0x35 || v.job == 0xb0) {
                TileAt(world, v.target_x, v.target_y).flags &= 0xfd;
            }
            if (v.job == 0xd4 || v.job == 0xd5 || v.job == 0x12e || v.job == 0x12f) {
                TileAt(world, v.x, v.y).flags &= 0xfd;
            }
        }
        if (v.art == 0x29) {
            // A seven-tile unit: all its parts go.
            GameWorld_RemoveSettlerFromLists(world, nullptr, victim);
            static constexpr std::int32_t kParts[7][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}, {-1, 0}, {0, -1}, {-1, -1}};
            for (const auto& p : kParts) {
                world->settlers[TileAt(world, v.x + p[0], v.y + p[1]).settler].owner = 0xff;
            }
            for (const auto& p : kParts) {
                TileAt(world, v.x + p[0], v.y + p[1]).settler = 0;
            }
            g_playerTallies[s.owner].unk14 += 5;
            const std::uint32_t screenY = GameWorld_TileScreenY(world, nullptr, v.x, v.y);
            const std::uint32_t screenX = GameWorld_TileScreenX(world, nullptr, v.x, v.y);
            MapEffects_Spawn_544f80(effects, nullptr, g_gameTick, screenX, screenY, v.x, v.y);
            return;
        }
        ++g_playerTallies[s.owner].unk14;
        GameWorld_QueueEvent(world, nullptr, 1, victim, v.art, v.owner);
        GameWorld_RemoveSettlerFromLists(world, nullptr, victim);
        v.job = 0x36;
        GameWorld_SetSettlerAnimation(world, nullptr, victim, 0x46);
        return;
    }

    // A tower soldier dies.
    GameWorld_NotifyLocalPlayer(world, nullptr, v.owner, 4, v.x, v.y);
    const std::uint32_t tower = v.building;
    if (v.job == 0xae) {
        std::uint16_t& head = world->buildings[tower].garrison;  // the garrison list
        if (head == victim) {
            head = v.next;
        } else {
            std::uint32_t p = head;
            while (world->settlers[p].next != victim) {
                p = world->settlers[p].next;
            }
            world->settlers[p].next = v.next;
        }
    }
    if (v.job == 0xad) {
        world->buildings[tower].guard = 0;
    }
    Building& b = world->buildings[tower];
    if (b.garrison != 0) {
        v.job = 0xaf;
        GameWorld_SetSettlerAnimation(world, nullptr, victim, 0x46);
        return;
    }
    // The tower is empty: the attacker takes it.
    std::uint32_t left = static_cast<std::uint32_t>(b.military.class1_coming) + b.military.class3_coming + b.military.class2_coming;
    for (std::uint32_t i = 1; i <= world->settler_high_water; ++i) {
        const Settler& r = world->settlers[i];
        if (r.owner == 0xff || r.building != tower || g_settlerTypeClass[r.art] == 0) {
            continue;
        }
        if (r.job == 3 || r.job == 4 || r.job == 0x10 || r.job == 0x6a || r.job == 0x8b) {
            GameWorld_ClearSettlerJob(world, nullptr, i);
            --left;
        }
    }
    if (left != 0) {
        GameDebugLog(texts::kSoldiersLeft, left);
    }
    UnlinkIdle(world, s);
    GameWorld_RemoveSettlerFromLists(world, nullptr, settler);
    GameDebugLog(texts::kWinsTower, static_cast<std::uint32_t>(s.owner), static_cast<std::uint32_t>(v.owner));
    GameWorld_NotifyLocalPlayer(world, nullptr, s.owner, 7, s.x, s.y);
    GameWorld_NotifyLocalPlayer(world, nullptr, v.owner, 8, v.x, v.y);
    GameWorld_QueuePendingEvent(world, nullptr, 5, tower, b.owner);
    GameWorld_QueueEvent(world, nullptr, 3, tower, b.owner, s.owner);
    b.owner = s.owner;
    const std::uint32_t guard = GameWorld_CreateSettler(world, nullptr, v.x, v.y, 0x25, s.owner);
    world->settlers[guard].job = 0xad;
    world->settlers[guard].building = static_cast<std::uint16_t>(tower);
    world->settlers[guard].health = 1;
    b.guard = static_cast<std::uint16_t>(guard);
    // Re-own the building's land: the layout's second and third lists.
    const std::uint32_t* item = g_buildingLayouts[57 * static_cast<std::uint32_t>(b.race) + b.art] + 1;
    if (item[-1] != kLayoutEnd1) {
        while (*item++ != kLayoutEnd1) {
        }
    }
    auto reown = [&](const std::uint32_t* pair) {
        const std::uint32_t x = world->buildings[tower].x + pair[0], y = world->buildings[tower].y + pair[1];
        kClearTileOwner(world, nullptr, x, y);
        GameWorld_SetOwnerOfFreeTile(world, nullptr, world->buildings[tower].x + pair[0],
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
    b.garrison = static_cast<std::uint16_t>(settler);
    b.military.class1_coming = 0;
    b.military.class1_wanted = 0;
    b.military.class2_coming = 0;
    b.military.class2_wanted = 0;
    b.military.class3_coming = 0;
    b.military.class3_wanted = 0;
    switch (ClassOf(s)) {
    case 1: s.job = 0x17; b.military.class1_wanted = 1; break;
    case 2: s.job = 0x8c; b.military.class2_wanted = 1; break;
    case 3: s.job = 0x8d; b.military.class3_wanted = 1; break;
    default: break;
    }
    TileAt(world, s.x, s.y).settler = 0;
    s.counter = 0;
    s.building = static_cast<std::uint16_t>(tower);
    s.next = 0;
    kRecalculateTerritory(world, nullptr, world->buildings[tower].x, world->buildings[tower].y);
    v.job = 0x36;
    GameWorld_SetSettlerAnimation(world, nullptr, victim, 0x46);
}

// Whether `e` is a valid target (phase 1 and 2 differ for classes
// 0x14..0x1d). `distance` is the offset entry's.
bool ValidTarget(GameWorld* world, const Settler& s, Settler& e, std::int32_t distance, bool phase2) {
    const std::int32_t c = ClassOf(e);
    bool ok = In(c, 1, 9);
    if (!ok && In(c, 0x14, 0x1d)) {
        ok = !phase2 || TileAt(world, e.x, e.y).owner != e.owner;
    }
    if (!ok && In(c, 0xa, 0x13)) {
        ok = distance <= 7 || (e.flags & 0x10) != 0;
    }
    if (!ok) {
        ok = c == 0x63 || c == 0x29 || (c == 0x1e && e.job == 0x135 && (ClassOf(s) == 1 || ClassOf(s) == 3));
    }
    if (!ok) {
        return false;
    }
    if (In(c, 0xa, 0x13)) {
        e.flags |= 0x10;
    }
    return true;
}

}  // namespace

// Purpose: One step of a soldier: attack an enemy within reach (melee or arrows; killing it, or taking
// an enemy tower when its last defender falls), otherwise walk towards an enemy nearby. Returns 1 if
// it attacked.
std::uint32_t __fastcall GameWorld_UpdateSoldier(GameWorld* world, void*, std::uint32_t settler) {
    Settler& s = world->settlers[settler];
    const std::int32_t cls = ClassOf(s);
    std::int32_t range = 0, minimum = 0;  // minimum: uninitialized in the original for other classes
    if (cls == 1) {
        range = 1;
        minimum = 1;
    }
    if (cls == 2) {
        range = 0xd;
        minimum = 6;
        if (s.job == 0x8c) {
            range = 0x16;
            minimum = 0xa;
        }
    }
    if (cls == 3) {
        range = 2;
        minimum = 1;
    }
    const std::uint32_t x = s.x, y = s.y;
    const auto mapSize = static_cast<std::int32_t>(world->map_size);
    if ((s.flags & 0x20) == 0 && GameWorld_IsOtherPlayerNear(world, nullptr, x, y, range, s.owner) != 0 &&
        world->offsets[1].distance <= range) {
        for (std::uint32_t k = 1; world->offsets[k].distance <= range; ++k) {
            const MapOffset& entry = world->offsets[k];
            if (entry.distance < minimum) {
                continue;
            }
            const auto tx = static_cast<std::int32_t>(entry.dx + x), ty = static_cast<std::int32_t>(entry.dy + y);
            if (tx < 0 || tx >= mapSize || ty < 0 || ty >= mapSize) {
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
            if (s.job == 0x8c && world->players[e.owner].active == 2 && TileAt(world, e.x, e.y).owner != s.owner) {
                continue;
            }
            if (!AttackableJob(e.job, cls, entry.distance, false)) {
                continue;
            }
            Attack(world, settler, t, entry);
            return 1;
        }
    }

    // Phase 2: walk towards an enemy.
    if (s.job == 0xae || s.job == 0x8c || (s.flags & 0x20) != 0) {
        return 0;
    }
    if (GameWorld_IsOtherPlayerNear(world, nullptr, x, y, 0x12, s.owner) == 0 || world->offsets[1].distance > 0x12) {
        return 0;
    }
    for (std::uint32_t k = 1; world->offsets[k].distance <= 0x12; ++k) {
        const MapOffset& entry = world->offsets[k];
        const std::uint32_t tx = entry.dx + x, ty = entry.dy + y;
        if (static_cast<std::int32_t>(tx) < 0 || static_cast<std::int32_t>(tx) >= mapSize ||
            static_cast<std::int32_t>(ty) < 0 || static_cast<std::int32_t>(ty) >= mapSize) {
            continue;
        }
        const std::uint32_t t = TileAt(world, tx, ty).settler;
        if (t == 0) {
            continue;
        }
        const Player& me = world->players[s.owner];
        Settler& e = world->settlers[t];
        if (me.active == 1) {
            if (Team(world, e.owner) == me.team) {
                continue;
            }
        } else {
            if (me.active != 2) {
                continue;
            }
            const std::int32_t theirs = Team(world, e.owner);
            if (me.team == theirs) {
                continue;
            }
            const std::uint8_t myLand = TileAt(world, s.x, s.y).owner;
            const std::uint8_t theirLand = TileAt(world, e.x, e.y).owner;
            if (myLand != 0xff && theirLand != 0xff && Team(world, myLand) == me.team &&
                Team(world, theirLand) == theirs) {
                continue;  // both on their own land
            }
        }
        if (!ValidTarget(world, s, e, entry.distance, true)) {
            continue;
        }
        if (!AttackableJob(e.job, cls, entry.distance, true)) {
            continue;
        }
        std::uint32_t nearX = tx, nearY = ty;
        if (cls == 2 && TileDistance(s.x, s.y, e.x, e.y) < 8) {
            const std::uint32_t direction = static_cast<std::uint32_t>((entry.sector + 3) % 6);
            nearX = NeighbourX(s.x, direction);
            nearY = NeighbourY(s.y, direction);
        }
        bool sent = false;
        for (std::uint32_t j = 1; j <= 6 && !sent; ++j) {
            const std::uint32_t nx = world->offsets[j].dx + nearX, ny = world->offsets[j].dy + nearY;
            if (nx >= world->map_size || ny >= world->map_size) {
                continue;
            }
            const Tile& spot = TileAt(world, nx, ny);
            if (spot.walk_region != TileAt(world, s.x, s.y).walk_region) {
                continue;
            }
            if (spot.settler != 0 && ClassOf(e) != 0x28) {
                continue;
            }
            GameWorld_SetSettlerTarget(world, nullptr, settler, nx, ny);
            sent = true;
        }
        if (!sent) {
            continue;
        }
        s.job = 0x35;
        s.good = 0;
        return 0;
    }
    return 0;
}

S3RE_HOOK(0x0050e9f0, GameWorld_UpdateSoldier, SoldierFn, Original_GameWorld_UpdateSoldier, GameWorld_UpdateSoldier);

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

constexpr std::uint8_t kSoldierTypes[] = {5, 0x20, 0x23, 8, 0x1e, 0x21, 0x0f, 0x1f, 0x22};
// Targets: soldiers, and types of classes 0x14 (0x16), 0xa (0x17), 0x63
// (0x25), 0x1e (0x27), 0x15 (0x28), 0x28 (0x29), 0x29 (0x2a), 0 (1).
constexpr std::uint8_t kTargetTypes[] = {5, 0x20, 8, 0x1e, 0x0f, 0x22, 0x16, 0x17, 0x25, 0x27, 0x28, 0x29, 0x2a, 1};
constexpr std::uint16_t kTargetJobs[] = {0x37, 0x8e, 0x35, 0xb0, 3, 4, 0xad, 0xae, 0xad, 0xae,
                                         0x130, 0xd4, 0x12f, 0x135, 0x10, 0x55, 0x151};
constexpr std::uint16_t kSoldierJobs[] = {0x8c, 0xae, 0x8e, 0x35, 0x37, 0x17, 0x8d};

void SetUpWorld(GameWorld* w) {
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
        r.job = 0x55;  // no attackable job: only the settlers a case places are targets
    }
    for (std::uint32_t b = 0; b <= kTestBuildings; ++b) {
        Building& r = w->buildings[b];
        r.x = static_cast<std::uint16_t>(30 + r.x % 320);
        r.y = static_cast<std::uint16_t>(30 + r.y % 320);
        r.art = static_cast<std::uint8_t>(0x10 + r.art % 3);  // the three military buildings
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
        worlds.Both([&](GameWorld* w) {
            Settler& r = w->settlers[old];
            if (w->tiles[r.x][r.y].settler == old) {
                w->tiles[r.x][r.y].settler = 0;
            }
            r.job = 0x55;
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
    worlds.Both([&](GameWorld* w) {
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
        worlds.Both([&](GameWorld* w) {
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
            if (ejob == 0xae) {
                if (empty) {  // e at the head, another soldier after it
                    r.next = static_cast<std::uint16_t>(other);
                    b.garrison = static_cast<std::uint16_t>(e);
                } else {
                    r.next = 0;
                    w->settlers[other].next = static_cast<std::uint16_t>(e);
                    b.garrison = static_cast<std::uint16_t>(other);
                }
            } else if (ejob == 0xad) {
                b.garrison = static_cast<std::uint16_t>(other);
            }
        });
    }
}

void Compare(test::Context& ctx, SoldierFn candidate, int cases) {
    test::FakeGraphics gfx;  // every sprite valid, for SetSettlerAnimation
    gfx.MakeEverySpriteValid(7);
    Globals globals;
    test::WorldPair worlds(ctx, sizeof(GameWorld));
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
        const std::uint32_t expected = Original_GameWorld_UpdateSoldier(worlds.original(), nullptr, soldier);
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
std::uint32_t __fastcall WrongUpdateSoldier(GameWorld* world, void* edx, std::uint32_t settler) {
    Settler& s = world->settlers[settler];
    if (s.job == 0x8c) {
        s.job = 0x8b;
        const std::uint32_t result = GameWorld_UpdateSoldier(world, edx, settler);
        if (s.job == 0x8b) {
            s.job = 0x8c;
        }
        return result;
    }
    return GameWorld_UpdateSoldier(world, edx, settler);
}

}  // namespace

S3RE_TEST(GameWorld_UpdateSoldier_MatchesOriginal) {
    Compare(ctx, &GameWorld_UpdateSoldier, 4000);
}
S3RE_TEST(GameWorld_UpdateSoldier_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Compare(probe, &WrongUpdateSoldier, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
