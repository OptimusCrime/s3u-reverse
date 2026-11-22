// The per-step world maintenance GameWorld::UpdateEnvironment (0x4e7810) and
// its small helpers.
//
// GameWorld::DecayTraffic (0x00503980): (x, y): foot traffic (Tile +0x12)
//   decays: t -= (t + 7) >> 3 (byte arithmetic). On a building footprint
//   (flags & 8) a result of 7 becomes 8; elsewhere a result of 7 on a worn
//   path (terrain 0x1c) turns the tile back into grass (0x10) and marks it
//   for redrawing (draw_flags | 0x80). UpdateEnvironment calls it for one
//   tile per step (cursor world+0x2d2f4 / +0x2d2f8). ret 8, EAX not used.
// GameWorld::UpdateEnvironment (0x004e7810): () the first world update of
//   every logic step (CLogicThread::Step). Nothing while
//   g_worldUpdateDisabled (0x7deb55). In order:
//   1. every 64th pile from g_gameTick & 63: a building pile (art 0xfe)
//      whose area's settings5 for its good is 1 and whose amount exceeds
//      its leaving count gets DispatchPileCarrier;
//   2. the timed events (Effect table world+0x36700, 1..high-water): the
//      countdown runs down; at 0 by type: 1 SpawnSpellEffects(p0..p4), 2
//      the spell itself (0x5480b0, p0..p4) and QueuePendingEvent(8, p1,
//      p2), 3 settler flags &= p1, 4 building request_flags = (p2 & flags)
//      | p1, 5 a building hit (hit points +0x38 -= p1, or
//      NotifyLocalPlayer 9 + CrushBuilding), 6 a catapult hit (type 0x29:
//      health -= p1, or the unit and its seven tiles are freed, map effects
//      0x544f80), 7 a ship hit (0x2b / 0x2c: health or it sinks: list
//      unlink, passengers freed (0x2c), cargo subtracted from the owner's
//      tallies (0x2b), notifications 10 / 11, map effects 0x545070); then
//      the type is cleared;
//   3. Player::soldier_strength from the goods 10 (and 28 for race 1) on
//      the piles of the player's areas, g_strengthDivisor and damping;
//   4. the reveal points' countdowns;
//   5. the fog of war cycle and the minimap cycle (fog_of_war.cpp; both
//      skipped while g_fastForwarding, the fog counter then reset);
//   6. size * size / 2000 tiles of the landscape (cursor +0x2d2f4/+0x2d2f8
//      advanced by +0x2d2fc): worked patches back to grass, the object
//      life cycles (tree growth, seeding, burning and fire spreading with
//      Random), DecayTraffic, fish moving to a random water neighbour;
//   7. every 220 ticks ShiftStatistics, then MapEffects::ExpireSome.
//   The comments in the code give the details. ret, EAX not used.
//   Known difference: the original sums the goods into two int32[20] on
//   its stack indexed by the area's player byte unchecked (a byte >= 20
//   overwrites its stack); ours uses 256 entries.
// thiscall (ECX = world).

#include "game/debug_log.h"
#include "game/world.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace s3re::game {

using layout::Tile;

using TileFn = void(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);

// Purpose: Lets the foot traffic on a tile fade, so that paths nobody walks any more grow back into grass.
void __fastcall GameWorld_DecayTraffic(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    Tile& tile = TileAtOffset(world, TileOffset(x, y));
    const std::uint8_t traffic = tile.traffic;
    if (traffic == 0) {
        return;
    }
    const auto decayed = static_cast<std::uint8_t>(traffic - ((traffic + 7) >> 3));
    tile.traffic = decayed;
    if (decayed != 7) {
        return;
    }
    if ((tile.flags & layout::kTileBuildingFootprint) != 0) {
        tile.traffic = 8;
    } else if (tile.terrain == 0x1c) {
        tile.terrain = 0x10;
        tile.draw_flags |= layout::kTileDrawRedraw;
    }
}

S3RE_HOOK(0x00503980, GameWorld_DecayTraffic, TileFn, Original_GameWorld_DecayTraffic, GameWorld_DecayTraffic);

// ---------------------------------------------------------------------------
// GameWorld::UpdateEnvironment
// ---------------------------------------------------------------------------

namespace {

using layout::Area;
using layout::Building;
using layout::Effect;
using layout::Pile;
using layout::Player;
using layout::Settler;
using U = std::uint32_t;

// Callees, called through their S3.EXE addresses (Detours sends a call to
// our replacement where there is one; tests can stub them).
constexpr std::uintptr_t kDispatchPileCarrier = 0x005052c0;
constexpr std::uintptr_t kSpawnSpellEffects = 0x005474e0;
constexpr std::uintptr_t kExecuteSpell = 0x005480b0;
constexpr std::uintptr_t kQueuePendingEvent = 0x004e8ba0;
constexpr std::uintptr_t kNotifyLocalPlayer = 0x004fec10;
constexpr std::uintptr_t kCrushBuilding = 0x004d9250;
constexpr std::uintptr_t kTileScreenX = 0x00519610;
constexpr std::uintptr_t kTileScreenY = 0x00519640;
constexpr std::uintptr_t kSpawnCatapultHit = 0x00544f80;  // MapEffects::Spawn_544f80
constexpr std::uintptr_t kSpawnShipHit = 0x00545070;      // MapEffects::Spawn_545070
constexpr std::uintptr_t kSubtractPlayerCounter = 0x004ffb40;
constexpr std::uintptr_t kClearSightSlice = 0x00518ec0;
constexpr std::uintptr_t kMarkBuildingSight = 0x00519fa0;
constexpr std::uintptr_t kMarkRevealPointsSight = 0x00518f20;
constexpr std::uintptr_t kMarkSettlerSight = 0x0051a150;
constexpr std::uintptr_t kUpdateFogSlice = 0x00519010;
constexpr std::uintptr_t kUpdateMinimapSlice = 0x004fb520;
constexpr std::uintptr_t kIsOnMap = 0x005194e0;
constexpr std::uintptr_t kUnblockTile = 0x0051b770;
constexpr std::uintptr_t kRandom = 0x004feb70;
constexpr std::uintptr_t kSpellOnArea = 0x00548380;
constexpr std::uintptr_t kDecayTraffic = 0x00503980;
constexpr std::uintptr_t kRandomBelow6 = 0x00503880;
constexpr std::uintptr_t kNeighbourX = 0x00503700;
constexpr std::uintptr_t kNeighbourY = 0x00503730;
constexpr std::uintptr_t kShiftStatistics = 0x004ff470;
constexpr std::uintptr_t kExpireSome = 0x00545d40;
constexpr std::uintptr_t kDebugLogFn = 0x00428720;

// Globals.
constexpr std::uintptr_t kWorldUpdateDisabled = 0x007deb55;  // uint8 g_worldUpdateDisabled
constexpr std::uintptr_t kGameTick = 0x007dfd48;             // g_gameTick
constexpr std::uintptr_t kFastForwarding = 0x007dfd30;       // int32, set while CLogicThread::Step catches up
constexpr std::uintptr_t kStrengthBase = 0x007acfa4;         // uint32, see SoldierStrength
constexpr std::uintptr_t kGrowthStart = 0x00730efc;          // uint32, first object that grows (0xd8 in the image)

// DebugLog formats.
constexpr std::uintptr_t kBuildingHitText = 0x006a5aec;  // "Gebaeude getroffen %u"
constexpr std::uintptr_t kCatapultHitText = 0x006a5ad4;  // "Katapult getroffen %u"
constexpr std::uintptr_t kShipSinksText = 0x006a5ac0;    // "Schiff sinkt !!!"
constexpr std::uintptr_t kShipHitText = 0x006a5aac;      // "Schiff getroffen %u"

using Fn0 = U(__fastcall*)(GameWorld* world, void* edx);
using Fn1 = U(__fastcall*)(GameWorld* world, void* edx, U);
using Fn2 = U(__fastcall*)(GameWorld* world, void* edx, U, U);
using Fn3 = U(__fastcall*)(GameWorld* world, void* edx, U, U, U);
using Fn4 = U(__fastcall*)(GameWorld* world, void* edx, U, U, U, U);
using Fn5 = U(__fastcall*)(GameWorld* world, void* edx, U, U, U, U, U);
using TableFn1 = U(__fastcall*)(layout::MapEffectTable* table, void* edx, U);
using TableFn5 = U(__fastcall*)(layout::MapEffectTable* table, void* edx, U, U, U, U, U);
using NeighbourFn = U(__stdcall*)(U coordinate, U direction);

template <typename F>
F Callee(std::uintptr_t address) {
    return reinterpret_cast<F>(address);
}

U Tick() {
    return *reinterpret_cast<const volatile U*>(kGameTick);
}

Tile& TileAt(GameWorld* world, U x, U y) {
    return TileAtOffset(world, TileOffset(x, y));
}

void DebugLog(std::uintptr_t format) {
    using LogFn = void(__cdecl*)(const char*, ...);
    reinterpret_cast<LogFn>(kDebugLogFn)(reinterpret_cast<const char*>(format));
}

void DebugLog(std::uintptr_t format, U value) {
    using LogFn = void(__cdecl*)(const char*, ...);
    reinterpret_cast<LogFn>(kDebugLogFn)(reinterpret_cast<const char*>(format), value);
}

// Removes a settler from its owner's list of its type (prev/next links;
// the head is players[owner].settler_lists[type]).
void UnlinkSettler(GameWorld* world, Settler& s, U type) {
    if (s.prev != 0) {
        world->settlers[s.prev].next = s.next;
    } else {
        world->players[s.owner].settler_lists[type] = s.next;
    }
    if (s.next != 0) {
        world->settlers[s.next].prev = s.prev;
    }
}

U ScreenX(GameWorld* world, U x, U y) {
    return Callee<Fn2>(kTileScreenX)(world, nullptr, x, y);
}

U ScreenY(GameWorld* world, U x, U y) {
    return Callee<Fn2>(kTileScreenY)(world, nullptr, x, y);
}

// Event type 5: a building hit (by a catapult stone, probably).
void BuildingHit(GameWorld* world, const Effect& e) {
    const U number = e.params[0];
    Building& b = world->buildings[number];
    const U damage = e.params[1];
    if (b.hit_points >= damage) {
        b.hit_points = static_cast<std::uint16_t>(b.hit_points - damage);
    } else {
        Callee<Fn4>(kNotifyLocalPlayer)(world, nullptr, b.owner, 9, b.x, b.y);
        Callee<Fn1>(kCrushBuilding)(world, nullptr, number);
    }
    DebugLog(kBuildingHitText, e.params[1]);
}

// Event type 6: a catapult (the seven-tile unit, type 0x29) hit.
void CatapultHit(GameWorld* world, const Effect& e) {
    Settler& s = world->settlers[e.params[0]];
    if (s.art == 0x29) {
        const U damage = e.params[1];
        if (s.health >= damage) {
            s.health = static_cast<std::uint8_t>(s.health - damage);
        } else {
            UnlinkSettler(world, s, 0x29);
            // The seven tiles of the unit: centre, (+1, 0), (0, +1), (+1, +1),
            // (-1, 0), (0, -1), (-1, -1). Their settlers are freed, then the
            // tiles' settler words cleared.
            static constexpr std::int32_t kTiles[7][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}, {-1, 0}, {0, -1}, {-1, -1}};
            for (const auto& d : kTiles) {
                const Tile& t = TileAt(world, s.x + static_cast<U>(d[0]), s.y + static_cast<U>(d[1]));
                world->settlers[t.settler].owner = 0xff;
            }
            for (const auto& d : kTiles) {
                TileAt(world, s.x + static_cast<U>(d[0]), s.y + static_cast<U>(d[1])).settler = 0;
            }
            const U x = s.x;
            const U y = s.y;
            const U sy = ScreenY(world, x, y);
            const U sx = ScreenX(world, x, y);
            Callee<TableFn5>(kSpawnCatapultHit)(&world->map_effects, nullptr, Tick(), sx, sy, x, y);
        }
    }
    DebugLog(kCatapultHitText, e.params[1]);
}

// Event type 7: a ship (type 0x2b, or 0x2c which carries settlers) hit.
void ShipHit(GameWorld* world, const Effect& e) {
    const U number = e.params[0];
    Settler& s = world->settlers[number];
    if ((s.art == 0x2b || s.art == 0x2c) && s.owner != 0xff) {
        const U damage = e.params[1];
        if (s.health >= damage) {
            s.health = static_cast<std::uint8_t>(s.health - damage);
            Callee<Fn4>(kNotifyLocalPlayer)(world, nullptr, s.owner, 10, s.x, s.y);
        } else {
            DebugLog(kShipSinksText);
            if (s.art != 0x2c) {
                UnlinkSettler(world, s, s.art);
            }
            if (s.art == 0x2c) {
                // The passengers' chain (next links) goes down with the ship.
                U current = s.next;
                while (current != 0) {
                    Settler& p = world->settlers[current];
                    p.owner = 0xff;
                    p.prev = 0;
                    const U next = p.next;
                    world->settlers[world->settlers[next].prev].next = 0;
                    current = next;
                }
                s.passengers = 0;
            }
            if (s.art == 0x2b) {
                // Cargo: up to four (good, count) pairs, counts in nibbles
                // of +0x17 / +0x18, goods in the low bytes of +0x1c / +0x20.
                const auto subtract = Callee<Fn3>(kSubtractPlayerCounter);
                if ((s.cargo_amounts01 & 0x0f) != 0) {
                    subtract(world, nullptr, s.owner, s.saved_animation & 0xff, s.cargo_amounts01 & 0x0fu);
                }
                if ((s.cargo_amounts01 & 0xf0) != 0) {
                    subtract(world, nullptr, s.owner, (s.saved_animation >> 8) & 0xff, static_cast<U>(s.cargo_amounts01) >> 4);
                }
                if ((s.cargo_amounts23 & 0x0f) != 0) {
                    subtract(world, nullptr, s.owner, s.saved_counter & 0xffu, s.cargo_amounts23 & 0x0fu);
                }
                if ((s.cargo_amounts23 & 0xf0) != 0) {
                    subtract(world, nullptr, s.owner, static_cast<U>(s.saved_counter) >> 8, static_cast<U>(s.cargo_amounts23) >> 4);
                }
            }
            Callee<Fn4>(kNotifyLocalPlayer)(world, nullptr, s.owner, 0xb, s.x, s.y);
            const U x = s.x;
            const U y = s.y;
            const U sy = ScreenY(world, x, y);
            const U sx = ScreenX(world, x, y);
            Callee<TableFn5>(kSpawnShipHit)(&world->map_effects, nullptr, Tick(), sx, sy, x, y);
            TileAt(world, s.x, s.y).settler = 0;
            s.owner = 0xff;
        }
    }
    DebugLog(kShipHitText, e.params[1]);
}

void RunTimedEvents(GameWorld* world) {
    for (U i = 1; i <= world->effects_high_water; ++i) {
        Effect& e = world->effects[i];
        if (e.type == 0) {
            continue;
        }
        if (e.countdown != 0) {
            --e.countdown;
        }
        if (e.countdown != 0) {
            continue;
        }
        switch (e.type) {
        case 1:
            Callee<Fn5>(kSpawnSpellEffects)(world, nullptr, e.params[0], e.params[1], e.params[2], e.params[3],
                                        e.params[4]);
            break;
        case 2:
            Callee<Fn5>(kExecuteSpell)(world, nullptr, e.params[0], e.params[1], e.params[2], e.params[3], e.params[4]);
            Callee<Fn3>(kQueuePendingEvent)(world, nullptr, 8, e.params[1], e.params[2]);
            break;
        case 3:
            world->settlers[e.params[0]].flags &= static_cast<std::uint8_t>(e.params[1]);
            break;
        case 4: {
            std::uint8_t& flags = world->buildings[e.params[0]].request_flags;
            flags = static_cast<std::uint8_t>((e.params[2] & flags) | e.params[1]);
            break;
        }
        case 5:
            BuildingHit(world, e);
            break;
        case 6:
            CatapultHit(world, e);
            break;
        case 7:
            ShipHit(world, e);
            break;
        default:
            break;
        }
        e.type = 0;
    }
}

// Player::soldier_strength from the amounts of goods 10 (and 28 for race 1)
// on the piles of each player's areas.
void UpdateSoldierStrength(GameWorld* world) {
    // The original sums into two int32[20] on its stack, indexed by the
    // area's player byte unchecked; 256 entries here (the same for players
    // 0..19).
    U sums[256] = {};
    U extra[256] = {};
    const U areas = world->area_high_water;
    for (U n = 1; n <= areas; ++n) {
        const Area& a = world->areas[n];
        const std::uint8_t player = a.player;
        if (player == 0xff) {
            continue;
        }
        for (U pile = a.piles_by_goods[10]; pile != 0; pile = world->piles[pile].next) {
            sums[player] += world->piles[pile].amount;
        }
        for (U pile = a.piles_by_goods[28]; pile != 0; pile = world->piles[pile].next) {
            extra[player] += world->piles[pile].amount;
        }
    }
    for (U p = 0; p < layout::kPlayers; ++p) {
        Player& player = world->players[p];
        const U base = 0x600u / (*reinterpret_cast<const U*>(kStrengthBase) + 4);
        U value = player.race == 1 ? base + (sums[p] + extra[p] * 4) * 2 : base + sums[p] * 4;
        for (U limit : {0x100u, 0x140u, 0x180u, 0x1c0u}) {
            if (value > limit) {
                value = ((value - limit) >> 1) + limit;
            }
        }
        if (world->objectives_enabled == 0) {
            player.soldier_strength = value >> 1;
        } else {
            player.soldier_strength = ((value >> 1) * 50u) / 100u;
        }
    }
}

bool IsGrowingTree(std::uint8_t object) {
    return object >= 0x55 && object <= 0x6f;
}

// One tile of the landscape pass (the cursor was just advanced).
void UpdateLandscapeTile(GameWorld* world) {
    auto x = [world] { return world->landscape_x; };
    auto y = [world] { return world->landscape_y; };
    auto object = [&]() -> std::uint8_t& { return TileAt(world, x(), y()).object; };
    const auto random = Callee<Fn0>(kRandom);

    // A worked patch (terrain 0x16) without growing trees around turns back
    // into grass.
    if (TileAt(world, x(), y()).terrain == 0x16 && Callee<Fn2>(kIsOnMap)(world, nullptr, x(), y()) != 0) {
        const U cx = x();
        const U cy = y();
        static constexpr std::int32_t kAround[7][2] = {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, 1}};
        bool clear = true;
        for (const auto& d : kAround) {
            if (IsGrowingTree(TileAt(world, cx + static_cast<U>(d[0]), cy + static_cast<U>(d[1])).object)) {
                clear = false;
                break;
            }
        }
        if (clear) {
            Tile& t = TileAt(world, cx, cy);
            t.terrain = 0x10;
            TileAt(world, x(), y()).draw_flags |= layout::kTileDrawRedraw;
        }
    }
    if (object() >= 0xd0 && object() <= 0xd7) {
        object() = 0;
    }
    if (object() == 0x5d || object() == 0x66 || object() == 0x6f) {
        object() = 0;
    }
    if (object() >= 0xb8 && object() <= 0xcf) {
        object() = static_cast<std::uint8_t>(object() + 8);
    }
    if (object() >= *reinterpret_cast<const U*>(kGrowthStart) && object() <= 0xdf) {
        std::uint8_t& o = object();
        const U r = random(world, nullptr);
        o = static_cast<std::uint8_t>((r & 1) + ((o + 0x4a) << 1));
    }
    if (object() >= 0xe0 && object() <= 0xe7) {
        object() = static_cast<std::uint8_t>(object() - 8);
    }
    if (object() == 0x54) {
        U stage = (random(world, nullptr) & 0xffff) % 6;
        if (stage == 5) {
            stage = 6;
        }
        object() = static_cast<std::uint8_t>(stage - 0x20);
    }
    if (object() >= 0xf7 && object() <= 0xfd) {
        // Trees (0x44..0x52) among the first 201 ring offsets; one within
        // the first 19 is needed to go on.
        bool treeNear = false;
        U trees = 0;
        for (U i = 0; i <= 0xc8; ++i) {
            const U tx = static_cast<U>(world->offsets[i].dx) + x();
            const U ty = static_cast<U>(world->offsets[i].dy) + y();
            if (Callee<Fn2>(kIsOnMap)(world, nullptr, tx, ty) != 0) {
                const std::uint8_t o = TileAt(world, tx, ty).object;
                if (o >= 0x44 && o <= 0x52) {
                    if (!treeNear && i <= 0x12) {
                        treeNear = true;
                    }
                    ++trees;
                }
            }
        }
        if (!treeNear) {
            std::uint8_t& o = object();
            if (o >= 0xf8) {
                --o;
            } else {
                o = 0;
                Callee<Fn2>(kUnblockTile)(world, nullptr, x(), y());
            }
        } else if (object() <= 0xfc) {
            const U chance = trees < 10 ? trees * 10 : 100;
            const U roll = (random(world, nullptr) & 0xffff) % 0x63;
            if (chance >= roll) {
                ++object();
            }
        }
    }
    if (object() >= 0x55 && object() <= 0x5c) {
        ++object();
    }
    if (object() >= 0x5e && object() <= 0x65) {
        ++object();
    }
    if (object() >= 0x67 && object() <= 0x6e) {
        ++object();
    }
    if (object() == 0xf6) {
        object() = 0;
        Callee<Fn2>(kUnblockTile)(world, nullptr, x(), y());
        Callee<Fn3>(kQueuePendingEvent)(world, nullptr, 8, x(), y());
    }
    if (object() >= 0xf3 && object() <= 0xf5) {
        ++object();
        if (object() == 0xf4) {
            Callee<Fn3>(kSpellOnArea)(world, nullptr, 0x20, x(), y());
        }
        // Fire spreads to trees (0x44..0x53) at ring offsets 19..60.
        for (U i = 19; i < 19 + 0x2a; ++i) {
            if ((random(world, nullptr) & 0xffff) <= 40000) {
                continue;
            }
            const U tx = static_cast<U>(world->offsets[i].dx) + x();
            const U ty = static_cast<U>(world->offsets[i].dy) + y();
            if (tx >= world->map_size || ty >= world->map_size) {
                continue;
            }
            Tile& t = TileAt(world, tx, ty);
            if (t.object >= 0x44 && t.object <= 0x53 && TileAt(world, tx + 1, ty + 1).settler == 0) {
                t.object = 0xf3;
                Callee<Fn3>(kQueuePendingEvent)(world, nullptr, 8, tx, ty);
            }
        }
    }
    Callee<Fn2>(kDecayTraffic)(world, nullptr, x(), y());
    // Fish (resources of kind 0, amount 1..15) move to a random neighbour in
    // the water.
    const std::uint8_t fish = TileAt(world, x(), y()).resources;
    if (fish <= 0x0f && fish != 0) {
        const U direction = Callee<Fn0>(kRandomBelow6)(world, nullptr);
        const U nx = Callee<NeighbourFn>(kNeighbourX)(x(), direction);
        const U ny = Callee<NeighbourFn>(kNeighbourY)(y(), direction);
        if (nx < world->map_size && ny < world->map_size) {
            Tile& to = TileAt(world, nx, ny);
            if ((to.terrain & 0xf0) == 0 && to.resources != 0x0f) {
                ++to.resources;
                --TileAt(world, x(), y()).resources;
            }
        }
    }
    if ((random(world, nullptr) & 0xffff) > 50000) {
        if (object() >= 0xe8 && object() <= 0xf2) {
            object() = 0;
        }
    }
}

}  // namespace

using UpdateFn = void(__fastcall*)(GameWorld* world, void* edx);

// Purpose: The first part of every simulation step: due timed events (spell effects, spells, catapult
// and ship hits, ...), the soldiers' strength, the fog of war and minimap cycles, and the slow life of the
// landscape (trees growing, spreading and burning, fields returning to grass, fish moving).
void __fastcall GameWorld_UpdateEnvironment(GameWorld* world, void*) {
    if (*reinterpret_cast<const volatile std::uint8_t*>(kWorldUpdateDisabled) != 0) {
        return;
    }
    // Every 64th pile: building piles whose area lets the good be moved.
    for (U n = Tick() & 0x3f; n <= world->pile_high_water; n += 0x40) {
        const Pile& pile = world->piles[n];
        if (pile.art != 0xfe) {
            continue;
        }
        const Area& area = world->areas[TileAt(world, pile.x, pile.y).area];
        if ((&area.settings5[0])[pile.good] == 1 && pile.amount > pile.leaving) {
            Callee<Fn1>(kDispatchPileCarrier)(world, nullptr, n);
        }
    }
    RunTimedEvents(world);
    UpdateSoldierStrength(world);
    // Reveal points run out.
    U i = 0;
    do {
        layout::RevealPoint& point = world->reveal_points[i];
        if (point.countdown > 0 && --point.countdown == 0) {
            point.active = 0;
        }
        ++i;
    } while (i <= static_cast<U>(world->reveal_point_high_water));
    const auto fastForwarding = [] { return *reinterpret_cast<const volatile std::int32_t*>(kFastForwarding) != 0; };
    // Fog of war, 56 steps.
    bool resetFog = true;
    if (!fastForwarding()) {
        const U step = world->fog_cycle;
        if (step < 8) {
            Callee<Fn1>(kClearSightSlice)(world, nullptr, step);
        } else if (step - 8 < 16) {
            Callee<Fn1>(kMarkBuildingSight)(world, nullptr, step - 8);
            if (step - 8 == 0) {
                Callee<Fn0>(kMarkRevealPointsSight)(world, nullptr);
            }
        } else if (step - 0x18 < 16) {
            Callee<Fn1>(kMarkSettlerSight)(world, nullptr, step - 0x18);
        } else {
            Callee<Fn1>(kUpdateFogSlice)(world, nullptr, step - 0x28);
        }
        resetFog = ++world->fog_cycle == 0x38;
    }
    if (resetFog) {
        world->fog_cycle = 0;
    }
    // Minimap, 64 steps.
    if (!fastForwarding()) {
        if (++world->minimap_cycle >= 0x40) {
            world->minimap_cycle = 0;
        }
        Callee<Fn1>(kUpdateMinimapSlice)(world, nullptr, world->minimap_cycle);
    }
    // Landscape: size * size / 2000 tiles per step.
    auto tilesPerStep = [world] {
        const auto cells = static_cast<std::int32_t>(world->map_size * world->map_size);
        return static_cast<U>(cells / 2000);
    };
    if (tilesPerStep() != 0) {
        U done = 0;
        do {
            const U size = world->map_size;
            world->landscape_x += world->landscape_step;
            if (world->landscape_x >= size) {
                world->landscape_x -= size;
                if (++world->landscape_y >= size) {
                    world->landscape_y -= size;
                }
            }
            UpdateLandscapeTile(world);
            ++done;
        } while (done < tilesPerStep());
    }
    if (Tick() % 0xdc == 0) {
        Callee<Fn0>(kShiftStatistics)(world, nullptr);
    }
    Callee<TableFn1>(kExpireSome)(&world->map_effects, nullptr, Tick());
}

S3RE_HOOK(0x004e7810, GameWorld_UpdateEnvironment, UpdateFn, Original_GameWorld_UpdateEnvironment,
          GameWorld_UpdateEnvironment);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kTestWorld = offsetof(GameWorld, tiles) + 0x40 * kTileRowBytes;

// Wrong: a footprint tile keeps 7.
void __fastcall WrongDecayTraffic(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    GameWorld_DecayTraffic(world, nullptr, x, y);
    Tile& tile = world->tiles[x][y];
    if (tile.traffic == 8 && (tile.flags & layout::kTileBuildingFootprint) != 0) {
        tile.traffic = 7;
    }
}

void CompareDecay(test::Context& ctx, TileFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t x = ctx.rng().Below(0x40);
        const std::uint32_t y = ctx.rng().Below(0x300);
        const auto traffic = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 7 + ctx.rng().Below(3) : ctx.rng().Below(256));
        const auto terrain = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0x1c : ctx.rng().Below(256));
        const auto flags = static_cast<std::uint8_t>(ctx.rng().Next32());
        worlds.Both([&](GameWorld* w) {
            w->tiles[x][y].traffic = traffic;
            w->tiles[x][y].terrain = terrain;
            w->tiles[x][y].flags = flags;
        });
        Original_GameWorld_DecayTraffic(worlds.original(), nullptr, x, y);
        candidate(worlds.ours(), nullptr, x, y);
        if (!worlds.Same(ctx, TileOffset(x, y), sizeof(Tile))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%u, %u) traffic %u", i, x, y, traffic);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

}  // namespace

S3RE_TEST(GameWorld_DecayTraffic_MatchesOriginal) {
    CompareDecay(ctx, &GameWorld_DecayTraffic, 50000);
}

S3RE_TEST(GameWorld_DecayTraffic_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDecay(probe, &WrongDecayTraffic, 5000);
    S3RE_EXPECT(probe.failures() > 0);
}

// ---------------------------------------------------------------------------
// UpdateEnvironment tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kFullWorld = sizeof(GameWorld);

// Callees whose own tests cover them and which would need a consistent
// world: recorded instead of run (both versions must make the same calls).
void InstallEnvironmentStubs(test::CalleeStubs& stubs) {
    stubs.Install<1, kDispatchPileCarrier>();
    stubs.Install<5, kSpawnSpellEffects>();
    stubs.Install<5, kExecuteSpell>();
    stubs.Install<3, kQueuePendingEvent>();
    stubs.Install<4, kNotifyLocalPlayer>();
    stubs.Install<1, kCrushBuilding>();
    stubs.Install<5, kSpawnCatapultHit>();
    stubs.Install<5, kSpawnShipHit>();
    stubs.Install<3, kSubtractPlayerCounter>();
    stubs.Install<1, kClearSightSlice>();
    stubs.Install<1, kMarkBuildingSight>();
    stubs.Install<1, kMarkSettlerSight>();
    stubs.Install<1, kUpdateFogSlice>();
    stubs.Install<1, kUpdateMinimapSlice>();
    stubs.Install<2, kUnblockTile>();
    stubs.Install<3, kSpellOnArea>();
    stubs.Install<1, kExpireSome>();
}

struct Globals {
    U tick, fast, base;
    std::uint8_t disabled;
    static Globals Save() {
        return {*reinterpret_cast<U*>(kGameTick), *reinterpret_cast<U*>(kFastForwarding),
                *reinterpret_cast<U*>(kStrengthBase), *reinterpret_cast<std::uint8_t*>(kWorldUpdateDisabled)};
    }
    void Restore() const {
        *reinterpret_cast<U*>(kGameTick) = tick;
        *reinterpret_cast<U*>(kFastForwarding) = fast;
        *reinterpret_cast<U*>(kStrengthBase) = base;
        *reinterpret_cast<std::uint8_t*>(kWorldUpdateDisabled) = disabled;
    }
};

// A small consistent world: ring table, piles, timed events, areas with
// short pile lists, settlers for the hit events, reveal points, landscape.
void SetUpEnvironment(test::Context& ctx, test::WorldPair& worlds) {
    auto& rng = ctx.rng();
    GameWorld* w = worlds.original();
    const U size = rng.Chance(1, 8) ? rng.Below(44) : 64 + rng.Below(321);
    w->map_size = size;
    // Ring table: every distance 0..64, offsets within the ring.
    U n = 0;
    for (std::int32_t d = 0; d <= 64; ++d) {
        const U count = d == 0 ? 1 : 1 + rng.Below(4 * static_cast<U>(d));
        for (U k = 0; k < count && n < 20000; ++k, ++n) {
            w->offsets[n].dx = d == 0 ? 0 : rng.Range(-d, d);
            w->offsets[n].dy = d == 0 ? 0 : rng.Range(-d, d);
            w->offsets[n].distance = d;
        }
    }
    // Landscape tiles: interesting objects, terrain and fish.
    const U span = size + 2 < 0x300 ? size + 2 : 0x300;
    for (U x = 0; x < span; ++x) {
        for (U y = 0; y < span; ++y) {
            Tile& t = w->tiles[x][y];
            static constexpr std::uint8_t kObjects[] = {0, 0, 0x44, 0x4a, 0x52, 0x53, 0x54, 0x55, 0x5c, 0x5d, 0x5e,
                                                       0x66, 0x6e, 0x6f, 0xb8, 0xcf, 0xd0, 0xd7, 0xd8, 0xdf, 0xe0,
                                                       0xe7, 0xe8, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xfc, 0xfd,
                                                       0xfe};
            t.object = rng.Chance(1, 4) ? static_cast<std::uint8_t>(rng.Next32())
                                        : kObjects[rng.Below(sizeof(kObjects))];
            t.terrain = rng.Chance(1, 3) ? 0x16 : static_cast<std::uint8_t>(rng.Chance(1, 2) ? rng.Below(16) : rng.Next32());
            t.resources = static_cast<std::uint8_t>(rng.Chance(1, 2) ? rng.Below(17) : rng.Next32());
            t.settler = static_cast<std::uint16_t>(rng.Chance(1, 2) ? 0 : 1 + rng.Below(31999));
            t.area = static_cast<std::uint16_t>(rng.Below(300));
        }
    }
    w->landscape_x = size != 0 ? rng.Below(size) : 0;
    w->landscape_y = size != 0 ? rng.Below(size) : 0;
    w->landscape_step = 1 + rng.Below(size + 1);  // (the game uses a step below the map size)
    // Piles (building piles, art 0xfe, on the map).
    w->pile_high_water = rng.Below(1500);
    for (U p = 0; p <= w->pile_high_water; ++p) {
        Pile& pile = w->piles[p];
        pile.art = rng.Chance(1, 2) ? 0xfe : static_cast<std::uint8_t>(rng.Next32());
        pile.good = static_cast<std::uint8_t>(rng.Below(36));
        pile.x = static_cast<std::uint16_t>(rng.Below(span));
        pile.y = static_cast<std::uint16_t>(rng.Below(span));
        pile.next = 0;
    }
    // Areas: settings5 values 0..2, short pile lists of goods 10 and 28.
    w->area_high_water = rng.Below(300);
    U nextListPile = 2000;  // distinct piles, so that no list loops
    for (U a = 0; a < 300; ++a) {
        Area& area = w->areas[a];
        area.player = static_cast<std::uint8_t>(rng.Chance(1, 4) ? 0xff : rng.Below(20));
        for (auto& s : area.settings5) {
            s = static_cast<std::uint8_t>(rng.Below(3));
        }
        for (U good : {10u, 28u}) {
            U head = 0;
            for (U k = rng.Below(4); k > 0; --k) {
                const U pile = nextListPile++;
                w->piles[pile].next = static_cast<std::uint16_t>(head);
                head = pile;
            }
            area.piles_by_goods[good] = static_cast<std::uint16_t>(head);
        }
    }
    for (U p = 0; p < layout::kPlayers; ++p) {
        w->players[p].race = static_cast<std::int32_t>(rng.Below(5));
    }
    w->objectives_enabled = static_cast<std::uint8_t>(rng.Chance(1, 3) ? 1 : 0);
    // Settler links stay inside the record array (the hit events follow them).
    for (Settler& s : w->settlers) {
        s.prev = static_cast<std::uint16_t>(rng.Below(32000));
        s.next = static_cast<std::uint16_t>(rng.Below(32000));
    }
    // Timed events.
    w->effects_high_water = rng.Below(300);
    U nextPassenger = 30000;  // passengers come from their own range, so that no chain loops
    for (U e = 1; e <= w->effects_high_water; ++e) {
        Effect& effect = w->effects[e];
        effect.type = static_cast<std::uint16_t>(rng.Below(9));
        effect.countdown = static_cast<std::uint16_t>(rng.Below(3));
        effect.params[0] = static_cast<std::uint16_t>(effect.type == 4 || effect.type == 5 ? rng.Below(4000)
                                                                                           : 1 + rng.Below(29999));
        effect.params[1] = static_cast<std::uint16_t>(rng.Chance(1, 2) ? rng.Below(300) : rng.Next32());
        effect.params[2] = static_cast<std::uint16_t>(rng.Next32());
        for (U k = 3; k < 8; ++k) {
            effect.params[k] = static_cast<std::uint16_t>(rng.Next32());
        }
        if (effect.type == 6 || effect.type == 7) {
            // A unit on the map; ships of type 0x2c carry a chain of passengers.
            Settler& s = w->settlers[effect.params[0]];
            static constexpr std::uint8_t kArts[] = {0x29, 0x2b, 0x2c, 0x01};
            s.art = kArts[rng.Below(4)];
            s.owner = static_cast<std::uint8_t>(rng.Chance(1, 6) ? 0xff : rng.Below(20));
            s.x = static_cast<std::uint16_t>(1 + rng.Below(span > 3 ? span - 2 : 1));
            s.y = static_cast<std::uint16_t>(1 + rng.Below(span > 3 ? span - 2 : 1));
            s.health = static_cast<std::uint8_t>(rng.Next32());
            U chain = 0;
            for (U k = rng.Below(4); k > 0; --k) {
                const U p = nextPassenger < 32000 ? nextPassenger++ : 0;
                if (p == 0) {
                    break;
                }
                w->settlers[p].next = static_cast<std::uint16_t>(chain);
                chain = p;
            }
            s.next = static_cast<std::uint16_t>(s.art == 0x2c ? chain : rng.Below(32000));
        }
    }
    // Reveal points.
    w->reveal_point_high_water = static_cast<std::int32_t>(rng.Below(50));
    for (auto& point : w->reveal_points) {
        point.countdown = rng.Range(-2, 3);
        point.x = static_cast<std::int32_t>(rng.Below(span));
        point.y = static_cast<std::int32_t>(rng.Below(span));
    }
    w->fog_cycle = rng.Below(0x38);
    w->minimap_cycle = rng.Chance(1, 8) ? rng.Next32() : rng.Below(0x40);
    std::memcpy(worlds.oursBytes(), worlds.originalBytes(), kFullWorld);
    *reinterpret_cast<U*>(kGameTick) = rng.Chance(1, 4) ? 220 * rng.Below(1000) : rng.Next32();
    *reinterpret_cast<U*>(kFastForwarding) = rng.Chance(1, 5) ? 1 : 0;
    *reinterpret_cast<U*>(kStrengthBase) = rng.Below(20);
    *reinterpret_cast<std::uint8_t*>(kWorldUpdateDisabled) = rng.Chance(1, 20) ? 1 : 0;
}

void CompareEnvironment(test::Context& ctx, UpdateFn candidate, int cases) {
    const Globals saved = Globals::Save();
    test::WorldPair worlds(ctx, kFullWorld);
    test::CalleeStubs stubs;
    InstallEnvironmentStubs(stubs);
    DebugLogRecorder logs;
    for (int i = 0; i < cases; ++i) {
        SetUpEnvironment(ctx, worlds);
        logs.Take();
        stubs.SetWorld(worlds.original());
        Original_GameWorld_UpdateEnvironment(worlds.original(), nullptr);
        const std::vector<test::StubCall> expectedCalls = stubs.Take();
        const std::vector<DebugLogCall> expectedLogs = logs.Take();
        stubs.SetWorld(worlds.ours());
        candidate(worlds.ours(), nullptr);
        const std::vector<test::StubCall> actualCalls = stubs.Take();
        const std::vector<DebugLogCall> actualLogs = logs.Take();
        if (!S3RE_EXPECT(expectedCalls == actualCalls) || !S3RE_EXPECT(expectedLogs == actualLogs) ||
            !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: size %u, calls %u/%u, logs %u/%u", i,
                     worlds.original()->map_size, static_cast<U>(expectedCalls.size()),
                     static_cast<U>(actualCalls.size()), static_cast<U>(expectedLogs.size()),
                     static_cast<U>(actualLogs.size()));
            break;
        }
    }
    saved.Restore();
}

// Wrong: race 1's soldiers ignore good 28.
void __fastcall WrongUpdateEnvironment(GameWorld* world, void*) {
    std::int32_t races[layout::kPlayers];
    for (U p = 0; p < layout::kPlayers; ++p) {
        races[p] = world->players[p].race;
        if (races[p] == 1) {
            world->players[p].race = 0;
        }
    }
    GameWorld_UpdateEnvironment(world, nullptr);
    for (U p = 0; p < layout::kPlayers; ++p) {
        world->players[p].race = races[p];
    }
}

}  // namespace

S3RE_TEST(GameWorld_UpdateEnvironment_MatchesOriginal) {
    CompareEnvironment(ctx, &GameWorld_UpdateEnvironment, 150);
}

S3RE_TEST(GameWorld_UpdateEnvironment_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareEnvironment(probe, &WrongUpdateEnvironment, 60);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
