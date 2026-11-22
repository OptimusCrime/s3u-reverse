// Fog of war of the local player's team.
//
// Tile::sight (+0xd) says how well the local team sees a tile right now
// (0 = not at all). UpdateEnvironment runs a 56-step cycle over the map
// (counter world+0x110a580, only while 0x7dfd30, the fast-forward flag, is
// clear): steps 0..7 clear one eighth of the rows (ClearSightSlice
// 0x518ec0, map_marks.cpp), steps 8..23 mark the sight around every 16th
// building (MarkBuildingSight; step 8 also the reveal points), steps
// 24..39 around every 16th settler (MarkSettlerSight), steps 40..55 turn
// the sight of a sixteenth of the map into the drawn fog level
// (UpdateFogSlice). Sight around a centre: for the offsets of the world's
// ring table (world+0x110a588, MapOffset, sorted by distance) up to the
// first entry with distance == radius, tile (x + dx, y + dy) gets
// max(sight, radius - distance + 1) (reveal points: 0x24 - distance).
// Teams: Player::team (+0xc) of the owner must equal the local
// player's.
//
// GameWorld::MarkRevealPointsSight (0x00518f20): () for every active reveal
//   point 0..reveal_point_high_water (signed) whose own tile has sight <
//   0x23: radius 0x23 around (x, y) with value 0x24 - distance, coordinates
//   checked signed against the map size, value compared signed. ret.
// GameWorld::MarkBuildingSight (0x00519fa0): (first) buildings first,
//   first+16, ... <= building_high_water (unsigned) that are used, finished
//   (buildup == 0) and of the local team: radius 0xf; type 6 0x37, types
//   0xf / 0x10 / 0x12 with a garrison 0x25 / 0x28 / 0x2b. Only if the
//   building's own tile has sight < radius. Coordinates unsigned. ret 4.
// GameWorld::MarkSettlerSight (0x0051a150): (first) the same for settlers
//   (step 16 too): radius 0x14; type 0x17 0x1e for the local player's own,
//   skipped for others of the team; 0x27 10; 0x29 / 0x2b / 0x2c 0x1e; 0x25
//   and 0x2a never. Only if the own tile has sight < radius - 3. ret 4.
// GameWorld::UpdateFogSlice (0x00519010): (slice 0..15) for every x and y
//   in [slice * s, (slice + 1) * s), s = map size / 16 (signed division,
//   re-read every step): level = min(sight, 7), or 2 for sight 0/1 when
//   draw_flags & 0x36; with 0x7dfd84 (whole map revealed) always 7. For
//   sight <= 2 and a previous level (draw_flags bits 3..5) >= 3, seen_object
//   (+0xe) = SeenObjectAt(x, y) (what the team saw last). draw_flags:
//   bits 3..5 = level; if it changed, bits 0..2 = the old level and 0x80
//   (redraw), else bits 0..2 = level. ret 4. EAX not used.
// GameWorld::SeenObjectAt (0x0051d5f0): (x, y) -> AX: a building (flag 0x20,
//   object_number): (race + 0x20) << 6 + type, with bits 0x100/0x200/0x300
//   for construction progress ((buildup + 50 * b+0x20) * 249 / (50 * b+0x1f
//   + 4): < 60 -> 0, < 120 -> 0x100, 121..179 -> 0x200, 181..250 -> 0x300,
//   otherwise none); a pile: 0 if empty, else (amount + 0x40) << 6 + good;
//   no number: object + 0x1800 or 0; | 0x400 on terrain 0x1c. Only AX is
//   defined (both callers store AX). ret 8.
// All thiscall (ECX = world).

#include "game/globals.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

using layout::Building;
using layout::MapOffset;
using layout::Pile;
using layout::Settler;
using layout::Tile;

constexpr std::uintptr_t kSeenObjectAt = 0x0051d5f0;

Tile& TileAt(GameWorld* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

std::int32_t LocalTeam(GameWorld* world) {
    return (world->players + world->local_player)->team;
}

std::int32_t TeamOf(GameWorld* world, std::uint8_t owner) {
    return (world->players + owner)->team;
}

// Building/settler variant: unsigned coordinates and values.
void MarkSightAround(GameWorld* world, std::uint32_t x, std::uint32_t y, std::uint32_t radius) {
    for (const MapOffset* o = world->offsets; static_cast<std::uint32_t>(o->distance) != radius; ++o) {
        const std::uint32_t value = radius - static_cast<std::uint32_t>(o->distance) + 1;
        const std::uint32_t tx = static_cast<std::uint32_t>(o->dx) + x;
        const std::uint32_t ty = static_cast<std::uint32_t>(o->dy) + y;
        if (tx < world->map_size && ty < world->map_size) {
            Tile& tile = TileAt(world, tx, ty);
            if (tile.sight < value) {
                tile.sight = static_cast<std::uint8_t>(value);
            }
        }
    }
}

}  // namespace

using VoidFn = void(__fastcall*)(GameWorld* world, void* edx);
using FirstFn = void(__fastcall*)(GameWorld* world, void* edx, std::uint32_t first);
using SeenFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);

// Purpose: Lets the local team see around the revealed spots (probably from a spell) during the fog of war
// cycle.
void __fastcall GameWorld_MarkRevealPointsSight(GameWorld* world, void*) {
    constexpr std::int32_t kRadius = 0x23;
    for (std::int32_t i = 0; i <= world->reveal_point_high_water; ++i) {
        const layout::RevealPoint& point = world->reveal_points[i];
        if (point.active == 0) {
            continue;
        }
        const std::int32_t x = point.x;
        const std::int32_t y = point.y;
        if (TileAt(world, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)).sight >= kRadius) {
            continue;
        }
        for (const MapOffset* o = world->offsets; o->distance != kRadius; ++o) {
            const std::int32_t value = 0x24 - o->distance;
            const std::int32_t tx = o->dx + x;
            const std::int32_t ty = o->dy + y;
            const auto size = static_cast<std::int32_t>(world->map_size);
            if (tx >= 0 && ty >= 0 && tx < size && ty < size) {
                Tile& tile = TileAt(world, static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty));
                if (static_cast<std::int32_t>(tile.sight) < value) {
                    tile.sight = static_cast<std::uint8_t>(value);
                }
            }
        }
    }
}

// Purpose: Fog of war: the local team sees the land around its finished buildings (towers and a few others
// see farther); one sixteenth of the buildings per call.
void __fastcall GameWorld_MarkBuildingSight(GameWorld* world, void*, std::uint32_t first) {
    const std::int32_t team = LocalTeam(world);
    for (std::uint32_t i = first; i <= world->building_high_water; i += 16) {
        const Building& b = world->buildings[i];
        if (b.owner == 0xff || b.buildup != 0 || TeamOf(world, b.owner) != team) {
            continue;
        }
        std::uint32_t radius = 0xf;
        switch (b.art) {
        case 0x06: radius = 0x37; break;
        case 0x0f: if (b.garrison != 0) radius = 0x25; break;
        case 0x10: if (b.garrison != 0) radius = 0x28; break;
        case 0x12: if (b.garrison != 0) radius = 0x2b; break;
        default: break;
        }
        if (TileAt(world, b.x, b.y).sight < radius) {
            MarkSightAround(world, b.x, b.y, radius);
        }
    }
}

// Purpose: Fog of war: the local team sees the land around its settlers (some unit types see farther, some
// not at all); one sixteenth of the settlers per call.
void __fastcall GameWorld_MarkSettlerSight(GameWorld* world, void*, std::uint32_t first) {
    const std::int32_t team = LocalTeam(world);
    for (std::uint32_t i = first; i <= world->settler_high_water; i += 16) {
        const Settler& s = world->settlers[i];
        if (s.owner == 0xff || TeamOf(world, s.owner) != team) {
            continue;
        }
        std::uint32_t radius = 0x14;
        switch (s.art) {
        case 0x17:
            if (s.owner != static_cast<std::uint32_t>(world->local_player)) {
                continue;
            }
            radius = 0x1e;
            break;
        case 0x25:
        case 0x2a:
            continue;
        case 0x27: radius = 10; break;
        case 0x29:
        case 0x2b:
        case 0x2c: radius = 0x1e; break;
        default: break;
        }
        if (TileAt(world, s.x, s.y).sight < radius - 3) {
            MarkSightAround(world, s.x, s.y, radius);
        }
    }
}

// Purpose: What the local team remembers seeing on a tile (a building with its construction stage, a pile, or
// a landscape object), stored for drawing the tile under the fog of war.
std::uint32_t __fastcall GameWorld_SeenObjectAt(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    const Tile& tile = TileAt(world, x, y);
    std::uint16_t result = 0;
    if (tile.object_number != 0 && (tile.flags & layout::kTileHasBuilding) != 0) {
        const Building& b = world->buildings[tile.object_number];
        result = static_cast<std::uint16_t>(((b.race + 0x20) << 6) + b.art);
        if (b.buildup != 0) {
            const std::int32_t progress = (b.buildup + 50 * b.site.materials_used) * 249;
            const std::int32_t total = b.site.materials_total * 50 + 4;
            const auto q = static_cast<std::uint16_t>(progress / total);
            if (q < 0x3c) {
                result = 0;
            } else if (q < 0x78) {
                result |= 0x100;
            } else if (q >= 0x79 && q < 0xb4) {
                result |= 0x200;
            } else if (q >= 0xb5 && q <= 0xfa) {
                result |= 0x300;
            }
        }
    } else if (tile.object_number != 0) {
        const Pile& pile = world->piles[tile.object_number];
        if (pile.amount != 0) {
            result = static_cast<std::uint16_t>(pile.good + ((pile.amount + 0x40) << 6));
        }
    } else if (tile.object != 0) {
        result = static_cast<std::uint16_t>(tile.object + 0x1800);
    }
    if (tile.terrain == 0x1c) {
        result |= 0x400;
    }
    return result;
}

// Purpose: Fog of war: turns the local team's current sight of a sixteenth of the map into the fog level that
// is drawn, remembering what was last seen on tiles that fall back into the fog.
void __fastcall GameWorld_UpdateFogSlice(GameWorld* world, void*, std::uint32_t slice) {
    const auto seenObjectAt = reinterpret_cast<SeenFn>(kSeenObjectAt);
    const bool revealed = g_revealMap != 0;
    auto rows = [world]() { return static_cast<std::uint32_t>(static_cast<std::int32_t>(world->map_size) / 16); };
    for (std::uint32_t x = 0; x < world->map_size; ++x) {
        for (std::uint32_t y = rows() * slice; y < rows() * (slice + 1); ++y) {
            Tile& tile = TileAt(world, x, y);
            std::uint32_t level = tile.sight;
            if (revealed) {
                if (level <= 2 && (tile.draw_flags & 0x38) >= 0x18) {
                    tile.seen_object = static_cast<std::uint16_t>(seenObjectAt(world, nullptr, x, y));
                }
                level = 7;
            } else if (level > 7) {
                level = 7;
            } else if (level <= 2) {
                if ((tile.draw_flags & 0x38) >= 0x18) {
                    tile.seen_object = static_cast<std::uint16_t>(seenObjectAt(world, nullptr, x, y));
                }
                if (level < 2 && (tile.draw_flags & 0x36) != 0) {
                    level = 2;
                }
                // The original re-reads 0x7dfd84 here (7 if set): unreachable
                // while no other thread changes it.
            }
            const std::uint8_t flags = tile.draw_flags;
            const std::uint32_t current = (flags >> 3) & 7;
            if (current == level) {
                tile.draw_flags = static_cast<std::uint8_t>((flags & 0xf8) + level);
            } else {
                tile.draw_flags = static_cast<std::uint8_t>(((flags & 0xc0) | 0x80) + (level << 3) + current);
            }
        }
    }
}

S3RE_HOOK(0x00518f20, GameWorld_MarkRevealPointsSight, VoidFn, Original_GameWorld_MarkRevealPointsSight,
          GameWorld_MarkRevealPointsSight);
S3RE_HOOK(0x00519fa0, GameWorld_MarkBuildingSight, FirstFn, Original_GameWorld_MarkBuildingSight,
          GameWorld_MarkBuildingSight);
S3RE_HOOK(0x0051a150, GameWorld_MarkSettlerSight, FirstFn, Original_GameWorld_MarkSettlerSight,
          GameWorld_MarkSettlerSight);
S3RE_HOOK(0x0051d5f0, GameWorld_SeenObjectAt, SeenFn, Original_GameWorld_SeenObjectAt, GameWorld_SeenObjectAt);
S3RE_HOOK(0x00519010, GameWorld_UpdateFogSlice, FirstFn, Original_GameWorld_UpdateFogSlice,
          GameWorld_UpdateFogSlice);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kWorld = sizeof(GameWorld);

// A ring table like the game's: every distance 0..64 appears (the loops stop
// at the first entry with distance == radius), offsets within the ring.
void SetUpOffsets(test::Context& ctx, test::WorldPair& worlds) {
    std::uint32_t n = 0;
    std::vector<MapOffset> table(20000);
    for (std::int32_t d = 0; d <= 64; ++d) {
        const std::int32_t count = d == 0 ? 1 : 1 + static_cast<std::int32_t>(ctx.rng().Below(6 * d));
        for (std::int32_t i = 0; i < count && n < 20000; ++i, ++n) {
            table[n].dx = d == 0 ? 0 : ctx.rng().Range(-d, d);
            table[n].dy = d == 0 ? 0 : ctx.rng().Range(-d, d);
            table[n].distance = d;
            table[n].sector = static_cast<std::int32_t>(ctx.rng().Below(6));
        }
    }
    worlds.Both([&](GameWorld* w) { std::memcpy(w->offsets, table.data(), n * sizeof(MapOffset)); });
}

std::uint32_t TestMapSize(test::Context& ctx) {
    return ctx.rng().Chance(1, 4) ? 16 + ctx.rng().Below(64) : 64 + ctx.rng().Below(321);
}

std::uint16_t Coordinate(test::Context& ctx, std::uint32_t size) {
    // Mostly on the map, sometimes just past it (the centre is used unchecked).
    return static_cast<std::uint16_t>(ctx.rng().Chance(1, 10) ? size + ctx.rng().Below(4) : ctx.rng().Below(size));
}

std::uint8_t Owner(test::Context& ctx) {
    return static_cast<std::uint8_t>(ctx.rng().Chance(1, 5) ? 0xff : ctx.rng().Below(20));
}

void SetUpTeams(test::Context& ctx, test::WorldPair& worlds) {
    const auto local = static_cast<std::int32_t>(ctx.rng().Below(20));
    std::int32_t teams[20];
    for (auto& t : teams) {
        t = static_cast<std::int32_t>(ctx.rng().Below(3));
    }
    worlds.Both([&](GameWorld* w) {
        w->local_player = local;
        for (int p = 0; p < 20; ++p) {
            w->players[p].team = teams[p];
        }
    });
}

// Sight values spread over the interesting range.
void RandomSight(test::Context& ctx, test::WorldPair& worlds, std::uint32_t size) {
    for (std::uint32_t x = 0; x < size + 4; ++x) {
        for (std::uint32_t y = 0; y < size + 4; ++y) {
            const auto value = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Below(0x40));
            worlds.Both([&](GameWorld* w) { w->tiles[x][y].sight = value; });
        }
    }
}

template <typename Setup, typename Call>
void Compare(test::Context& ctx, int cases, Setup setup, Call call) {
    test::WorldPair worlds(ctx, kWorld);
    SetUpOffsets(ctx, worlds);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = TestMapSize(ctx);
        worlds.Both([&](GameWorld* w) { w->map_size = size; });
        SetUpTeams(ctx, worlds);
        RandomSight(ctx, worlds, size);
        const std::uint32_t arg = setup(worlds, size);
        if (!call(worlds, arg) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: size %u, arg %u", i, size, arg);
            return;
        }
    }
}

std::uint32_t SetUpRevealPoints(test::Context& ctx, test::WorldPair& worlds, std::uint32_t size) {
    const std::int32_t high = static_cast<std::int32_t>(ctx.rng().Below(52)) - 2;
    worlds.Both([&](GameWorld* w) { w->reveal_point_high_water = high; });
    for (std::int32_t i = 0; i < 50; ++i) {
        const auto active = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3) ? 0 : 1 + ctx.rng().Below(255));
        const std::int32_t x = ctx.rng().Range(-2, static_cast<std::int32_t>(size) + 2);
        const std::int32_t y = ctx.rng().Range(-2, static_cast<std::int32_t>(size) + 2);
        worlds.Both([&](GameWorld* w) {
            w->reveal_points[i].active = active;
            w->reveal_points[i].x = x;
            w->reveal_points[i].y = y;
        });
    }
    return 0;
}

std::uint32_t SetUpBuildings(test::Context& ctx, test::WorldPair& worlds, std::uint32_t size) {
    const std::uint32_t high = ctx.rng().Below(600);
    worlds.Both([&](GameWorld* w) { w->building_high_water = high; });
    static constexpr std::uint8_t kTypes[] = {6, 0xf, 0x10, 0x12, 1, 0x20};
    for (std::uint32_t i = 0; i <= high; ++i) {
        const std::uint8_t owner = Owner(ctx);
        const std::uint16_t x = Coordinate(ctx, size);
        const std::uint16_t y = Coordinate(ctx, size);
        const std::uint8_t art = kTypes[ctx.rng().Below(6)];
        const auto buildup = static_cast<std::uint8_t>(ctx.rng().Chance(1, 4) ? ctx.rng().Below(256) : 0);
        const auto garrison = static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Below(5));
        worlds.Both([&](GameWorld* w) {
            Building& b = w->buildings[i];
            b.owner = owner;
            b.x = x;
            b.y = y;
            b.art = art;
            b.buildup = buildup;
            b.garrison = garrison;
        });
    }
    return ctx.rng().Chance(1, 10) ? ctx.rng().Below(high + 20) : ctx.rng().Below(16);
}

std::uint32_t SetUpSettlers(test::Context& ctx, test::WorldPair& worlds, std::uint32_t size) {
    const std::uint32_t high = ctx.rng().Below(2000);
    worlds.Both([&](GameWorld* w) { w->settler_high_water = high; });
    static constexpr std::uint8_t kTypes[] = {0x17, 0x25, 0x27, 0x29, 0x2a, 0x2b, 0x2c, 1, 0x10, 0x30};
    for (std::uint32_t i = 0; i <= high; ++i) {
        const std::uint8_t owner = Owner(ctx);
        const std::uint16_t x = Coordinate(ctx, size);
        const std::uint16_t y = Coordinate(ctx, size);
        const std::uint8_t art = kTypes[ctx.rng().Below(10)];
        worlds.Both([&](GameWorld* w) {
            Settler& s = w->settlers[i];
            s.owner = owner;
            s.x = x;
            s.y = y;
            s.art = art;
        });
    }
    return ctx.rng().Chance(1, 10) ? ctx.rng().Below(high + 20) : ctx.rng().Below(16);
}

// Tiles for SeenObjectAt: building numbers stay inside the record array.
void SetUpSeenTiles(test::Context& ctx, test::WorldPair& worlds, std::uint32_t size) {
    for (std::uint32_t x = 0; x < size; ++x) {
        for (std::uint32_t y = 0; y < size; ++y) {
            const std::uint32_t kind = ctx.rng().Below(4);
            std::uint16_t number = 0;
            std::uint8_t flags = static_cast<std::uint8_t>(ctx.rng().Next32());
            if (kind == 0) {
                number = static_cast<std::uint16_t>(1 + ctx.rng().Below(3999));
                flags |= layout::kTileHasBuilding;
            } else if (kind == 1) {
                number = static_cast<std::uint16_t>(1 + ctx.rng().Below(31999));
                flags &= static_cast<std::uint8_t>(~layout::kTileHasBuilding);
            }
            const auto terrain = static_cast<std::uint8_t>(ctx.rng().Chance(1, 4) ? 0x1c : ctx.rng().Below(256));
            const auto object = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Below(256));
            worlds.Both([&](GameWorld* w) {
                Tile& t = w->tiles[x][y];
                t.object_number = number;
                t.flags = flags;
                t.terrain = terrain;
                t.object = object;
            });
        }
    }
}

void SetUpProgress(test::Context& ctx, test::WorldPair& worlds) {
    for (std::uint32_t i = 0; i < 4000; ++i) {
        const auto buildup = static_cast<std::uint8_t>(ctx.rng().Chance(1, 4) ? 0 : ctx.rng().Below(256));
        const auto total = static_cast<std::uint8_t>(ctx.rng().Below(8));
        const auto done = static_cast<std::uint8_t>(ctx.rng().Below(total + 1u));
        worlds.Both([&](GameWorld* w) {
            w->buildings[i].buildup = buildup;
            w->buildings[i].site.materials_total = total;
            w->buildings[i].site.materials_used = done;
        });
    }
}

void __fastcall WrongMarkBuildingSight(GameWorld* world, void*, std::uint32_t first) {
    // Wrong: towers of type 0x10 see as far as type 0xf.
    for (std::uint32_t i = first; i <= world->building_high_water; i += 16) {
        if (world->buildings[i].art == 0x10) {
            world->buildings[i].art = 0xf;
            GameWorld_MarkBuildingSight(world, nullptr, i);
            world->buildings[i].art = 0x10;
        }
    }
    GameWorld_MarkBuildingSight(world, nullptr, first);
}

std::uint32_t __fastcall WrongSeenObjectAt(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    // Wrong: no worn-path bit.
    return GameWorld_SeenObjectAt(world, nullptr, x, y) & ~0x400u;
}

void FogCompare(test::Context& ctx, FirstFn original, FirstFn candidate, int cases) {
    Compare(
        ctx, cases,
        [&](test::WorldPair& worlds, std::uint32_t size) {
            SetUpSeenTiles(ctx, worlds, size);
            for (std::uint32_t x = 0; x < size; ++x) {
                for (std::uint32_t y = 0; y < size; ++y) {
                    const auto flags = static_cast<std::uint8_t>(ctx.rng().Next32());
                    worlds.Both([&](GameWorld* w) { w->tiles[x][y].draw_flags = flags; });
                }
            }
            const std::int32_t revealed = ctx.rng().Chance(1, 4) ? 1 : 0;
            g_revealMap = revealed;
            return ctx.rng().Below(16);
        },
        [&](test::WorldPair& worlds, std::uint32_t slice) {
            original(worlds.original(), nullptr, slice);
            candidate(worlds.ours(), nullptr, slice);
            return true;
        });
    g_revealMap = 0;
}

void __fastcall WrongUpdateFogSlice(GameWorld* world, void*, std::uint32_t slice) {
    // Wrong: the first tile of the slice loses its redraw bit.
    GameWorld_UpdateFogSlice(world, nullptr, slice);
    const std::uint32_t y = static_cast<std::uint32_t>(static_cast<std::int32_t>(world->map_size) / 16) * slice;
    world->tiles[0][y].draw_flags &= 0x7f;
}

}  // namespace

S3RE_TEST(GameWorld_MarkRevealPointsSight_MatchesOriginal) {
    Compare(
        ctx, 60, [&](test::WorldPair& worlds, std::uint32_t size) { return SetUpRevealPoints(ctx, worlds, size); },
        [&](test::WorldPair& worlds, std::uint32_t) {
            Original_GameWorld_MarkRevealPointsSight(worlds.original(), nullptr);
            GameWorld_MarkRevealPointsSight(worlds.ours(), nullptr);
            return true;
        });
}

S3RE_TEST(GameWorld_MarkBuildingSight_MatchesOriginal) {
    Compare(
        ctx, 60, [&](test::WorldPair& worlds, std::uint32_t size) { return SetUpBuildings(ctx, worlds, size); },
        [&](test::WorldPair& worlds, std::uint32_t first) {
            Original_GameWorld_MarkBuildingSight(worlds.original(), nullptr, first);
            GameWorld_MarkBuildingSight(worlds.ours(), nullptr, first);
            return true;
        });
}

S3RE_TEST(GameWorld_MarkBuildingSight_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Compare(
        probe, 30, [&](test::WorldPair& worlds, std::uint32_t size) { return SetUpBuildings(probe, worlds, size); },
        [&](test::WorldPair& worlds, std::uint32_t first) {
            Original_GameWorld_MarkBuildingSight(worlds.original(), nullptr, first);
            WrongMarkBuildingSight(worlds.ours(), nullptr, first);
            return true;
        });
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_MarkSettlerSight_MatchesOriginal) {
    Compare(
        ctx, 60, [&](test::WorldPair& worlds, std::uint32_t size) { return SetUpSettlers(ctx, worlds, size); },
        [&](test::WorldPair& worlds, std::uint32_t first) {
            Original_GameWorld_MarkSettlerSight(worlds.original(), nullptr, first);
            GameWorld_MarkSettlerSight(worlds.ours(), nullptr, first);
            return true;
        });
}

S3RE_TEST(GameWorld_SeenObjectAt_MatchesOriginal) {
    Compare(
        ctx, 20,
        [&](test::WorldPair& worlds, std::uint32_t size) {
            SetUpSeenTiles(ctx, worlds, size);
            SetUpProgress(ctx, worlds);
            return 0u;
        },
        [&](test::WorldPair& worlds, std::uint32_t) {
            const std::uint32_t size = worlds.original()->map_size;
            for (std::uint32_t x = 0; x < size; ++x) {
                for (std::uint32_t y = 0; y < size; ++y) {
                    const std::uint32_t expected = Original_GameWorld_SeenObjectAt(worlds.original(), nullptr, x, y);
                    const std::uint32_t actual = GameWorld_SeenObjectAt(worlds.ours(), nullptr, x, y);
                    if (!S3RE_EXPECT_EQ(expected & 0xffff, actual & 0xffff)) {
                        ctx.Fail(__FILE__, __LINE__, "  tile (%u, %u)", x, y);
                        return false;
                    }
                }
            }
            return true;
        });
}

S3RE_TEST(GameWorld_SeenObjectAt_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    test::WorldPair worlds(probe, kWorld);
    worlds.Both([&](GameWorld* w) { w->map_size = 64; });
    SetUpSeenTiles(probe, worlds, 64);
    SetUpProgress(probe, worlds);
    bool differs = false;
    for (std::uint32_t x = 0; x < 64 && !differs; ++x) {
        for (std::uint32_t y = 0; y < 64 && !differs; ++y) {
            differs = (Original_GameWorld_SeenObjectAt(worlds.original(), nullptr, x, y) & 0xffff) !=
                      (WrongSeenObjectAt(worlds.ours(), nullptr, x, y) & 0xffff);
        }
    }
    S3RE_EXPECT(differs);
}

S3RE_TEST(GameWorld_UpdateFogSlice_MatchesOriginal) {
    FogCompare(ctx, Original_GameWorld_UpdateFogSlice, &GameWorld_UpdateFogSlice, 40);
}

S3RE_TEST(GameWorld_UpdateFogSlice_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    FogCompare(probe, Original_GameWorld_UpdateFogSlice, &WrongUpdateFogSlice, 60);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
