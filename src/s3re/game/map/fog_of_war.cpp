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
// CGameData::MarkRevealPointsSight (0x00518f20): () for every active reveal
//   point 0..reveal_point_high_water (signed) whose own tile has sight <
//   35: radius 35 (0x23) around (x, y) with value 36 - distance, coordinates
//   checked signed against the map size, value compared signed. ret.
// CGameData::MarkBuildingSight (0x00519fa0): (first) buildings first,
//   first+16, ... <= building_high_water (unsigned) that are used, finished
//   (buildup == 0) and of the local team: radius 15; the lookout tower
//   (type 6) 55, the small tower / large tower / castle (15, 16, 18) with a
//   garrison 37 / 40 / 43. Only if the building's own tile has sight <
//   radius. Coordinates unsigned. ret 4.
// CGameData::MarkSettlerSight (0x0051a150): (first) the same for settlers
//   (step 16 too): radius 20; a thief (type 23) 30 for the local player's
//   own, skipped for others of the team (thieves stay unseen by allies);
//   donkeys (39) 10; catapults, trade ships and ferries (41, 43, 44) 30;
//   types 37 and 42 (a building's guard, the parts of a seven-tile unit)
//   never. Only if the own tile has sight < radius - 3. ret 4.
// CGameData::UpdateFogSlice (0x00519010): (slice 0..15) for every x and y
//   in [slice * s, (slice + 1) * s), s = map size / 16 (signed division,
//   re-read every step): level = min(sight, 7), or 2 for sight 0/1 when
//   draw_flags & 0x36; with 0x7dfd84 (whole map revealed) always 7. For
//   sight <= 2 and a previous level (draw_flags bits 3..5) >= 3, seen_object
//   (+0xe) = SeenObjectAt(x, y) (what the team saw last). draw_flags:
//   bits 3..5 = level; if it changed, bits 0..2 = the old level and 0x80
//   (redraw), else bits 0..2 = level. ret 4. EAX not used.
// CGameData::SeenObjectAt (0x0051d5f0): (x, y) -> AX: a building (flag 0x20,
//   object_number): (race + 0x20) << 6 + type, with bits 0x100/0x200/0x300
//   for construction progress ((buildup + 50 * b+0x20) * 249 / (50 * b+0x1f
//   + 4): < 60 -> 0, < 120 -> 0x100, 121..179 -> 0x200, 181..250 -> 0x300,
//   otherwise none); a pile: 0 if empty, else (amount + 0x40) << 6 + good;
//   no number: object + 0x1800 or 0; | 0x400 on terrain 0x1c (a worn
//   path). Only AX is
//   defined (both callers store AX). ret 8.
// All thiscall (ECX = world).

#include "game/enums/settler_types.h"
#include "game/enums/buildings.h"
#include "game/enums/terrain.h"
#include "game/globals.h"
#include "game/typed_fields.h"
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

using B = BuildingType;
using S = SettlerType;

constexpr std::uint8_t kNobody = 0xff;  // Building::owner / Settler::owner of a free record

// Sight radii (in rings of the offset table).
constexpr std::int32_t kRevealPointRadius = 35;  // 0x23; value 36 - distance
constexpr std::uint32_t kBuildingSight = 15;
constexpr std::uint32_t kLookoutTowerSight = 55;
constexpr std::uint32_t kSmallTowerSight = 37;
constexpr std::uint32_t kLargeTowerSight = 40;
constexpr std::uint32_t kCastleSight = 43;
constexpr std::uint32_t kSettlerSight = 20;
constexpr std::uint32_t kFarSight = 30;     // own thieves, catapults, ships
constexpr std::uint32_t kDonkeySight = 10;
constexpr std::uint32_t kSettlerSightSlack = 3;  // marked again only below radius - 3

// MarkBuildingSight / MarkSettlerSight handle every 16th record per call.
constexpr std::uint32_t kRecordStride = 16;

// UpdateFogSlice: the map is done in 16 slices; fog levels 0..7.
constexpr std::int32_t kFogSlices = 16;
constexpr std::uint32_t kMaxFogLevel = 7;
constexpr std::uint32_t kDimFogLevel = 2;  // at most this: the remembered picture is drawn

// SeenObjectAt: construction stage of a building, from its progress 0..249.
constexpr std::uint16_t kStageFirst = 60;
constexpr std::uint16_t kStageSecond = 120;
constexpr std::uint16_t kStageThird = 180;
constexpr std::uint16_t kStageLast = 250;
constexpr std::uint8_t kTerrainWornPath = 0x1c;

Tile& TileAt(CGameData* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

std::int32_t LocalTeam(CGameData* world) {
    return (world->players + world->local_player)->team;
}

std::int32_t TeamOf(CGameData* world, std::uint8_t owner) {
    return (world->players + owner)->team;
}

// Building/settler variant: unsigned coordinates and values.
void MarkSightAround(CGameData* world, std::uint32_t x, std::uint32_t y, std::uint32_t radius) {
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

using VoidFn = void(__fastcall*)(CGameData* world, void* edx);
using FirstFn = void(__fastcall*)(CGameData* world, void* edx, std::uint32_t first);
using SeenFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);

// Purpose: Lets the local team see around the revealed spots (probably from a spell) during the fog of war
// cycle.
void __fastcall CGameData_MarkRevealPointsSight(CGameData* world, void*) {
    constexpr std::int32_t kRadius = kRevealPointRadius;
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
            const std::int32_t value = kRadius + 1 - o->distance;
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
void __fastcall CGameData_MarkBuildingSight(CGameData* world, void*, std::uint32_t first) {
    const std::int32_t team = LocalTeam(world);
    for (std::uint32_t i = first; i <= world->building_high_water; i += kRecordStride) {
        const Building& building = world->buildings[i];
        if (building.owner == kNobody || building.buildup != 0 || TeamOf(world, building.owner) != team) {
            continue;
        }
        const bool garrisoned = building.garrison != 0;
        std::uint32_t radius = kBuildingSight;
        switch (TypeOf(building)) {
        case B::LookoutTower: radius = kLookoutTowerSight; break;
        case B::SmallTower: if (garrisoned) radius = kSmallTowerSight; break;
        case B::LargeTower: if (garrisoned) radius = kLargeTowerSight; break;
        case B::Castle: if (garrisoned) radius = kCastleSight; break;
        default: break;
        }
        if (TileAt(world, building.x, building.y).sight < radius) {
            MarkSightAround(world, building.x, building.y, radius);
        }
    }
}

// Purpose: Fog of war: the local team sees the land around its settlers (some unit types see farther, some
// not at all); one sixteenth of the settlers per call.
void __fastcall CGameData_MarkSettlerSight(CGameData* world, void*, std::uint32_t first) {
    const std::int32_t team = LocalTeam(world);
    for (std::uint32_t i = first; i <= world->settler_high_water; i += kRecordStride) {
        const Settler& settler = world->settlers[i];
        if (settler.owner == kNobody || TeamOf(world, settler.owner) != team) {
            continue;
        }
        std::uint32_t radius = kSettlerSight;
        switch (TypeOf(settler)) {
        case S::Thief:
            // Allies don't see through another player's thief.
            if (settler.owner != static_cast<std::uint32_t>(world->local_player)) {
                continue;
            }
            radius = kFarSight;
            break;
        case S::Type37:
        case S::Type42:
            continue;
        case S::Donkey: radius = kDonkeySight; break;
        case S::Catapult:
        case S::TradeShip:
        case S::Ferry: radius = kFarSight; break;
        default: break;
        }
        if (TileAt(world, settler.x, settler.y).sight < radius - kSettlerSightSlack) {
            MarkSightAround(world, settler.x, settler.y, radius);
        }
    }
}

// Purpose: What the local team remembers seeing on a tile (a building with its construction stage, a pile, or
// a landscape object), stored for drawing the tile under the fog of war.
std::uint32_t __fastcall CGameData_SeenObjectAt(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    const Tile& tile = TileAt(world, x, y);
    std::uint16_t result = 0;
    if (tile.object_number != 0 && (tile.flags & layout::kTileHasBuilding) != 0) {
        const Building& b = world->buildings[tile.object_number];
        result = static_cast<std::uint16_t>(((b.race + 0x20) << 6) + b.art);
        if (b.buildup != 0) {
            const std::int32_t progress = (b.buildup + 50 * b.site.materials_used) * 249;
            const std::int32_t total = b.site.materials_total * 50 + 4;
            const auto stage = static_cast<std::uint16_t>(progress / total);
            // The gaps at exactly 120 and 180 (no stage bits) are the original's.
            if (stage < kStageFirst) {
                result = 0;
            } else if (stage < kStageSecond) {
                result |= 0x100;
            } else if (stage > kStageSecond && stage < kStageThird) {
                result |= 0x200;
            } else if (stage > kStageThird && stage <= kStageLast) {
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
    if (tile.terrain == kTerrainWornPath) {
        result |= 0x400;
    }
    return result;
}

// Purpose: Fog of war: turns the local team's current sight of a sixteenth of the map into the fog level that
// is drawn, remembering what was last seen on tiles that fall back into the fog.
void __fastcall CGameData_UpdateFogSlice(CGameData* world, void*, std::uint32_t slice) {
    const auto seenObjectAt = reinterpret_cast<SeenFn>(kSeenObjectAt);
    const bool revealed = g_revealMap != 0;
    auto rows = [world]() { return static_cast<std::uint32_t>(static_cast<std::int32_t>(world->map_size) / kFogSlices); };
    for (std::uint32_t x = 0; x < world->map_size; ++x) {
        for (std::uint32_t y = rows() * slice; y < rows() * (slice + 1); ++y) {
            Tile& tile = TileAt(world, x, y);
            std::uint32_t level = tile.sight;
            // draw_flags bits 3..5 (0x38) hold the previous level; 0x18 = level 3.
            if (revealed) {
                if (level <= kDimFogLevel && (tile.draw_flags & 0x38) >= 0x18) {
                    tile.seen_object = static_cast<std::uint16_t>(seenObjectAt(world, nullptr, x, y));
                }
                level = kMaxFogLevel;
            } else if (level > kMaxFogLevel) {
                level = kMaxFogLevel;
            } else if (level <= kDimFogLevel) {
                if ((tile.draw_flags & 0x38) >= 0x18) {
                    tile.seen_object = static_cast<std::uint16_t>(seenObjectAt(world, nullptr, x, y));
                }
                if (level < kDimFogLevel && (tile.draw_flags & 0x36) != 0) {
                    level = kDimFogLevel;
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

S3RE_HOOK(0x00518f20, CGameData_MarkRevealPointsSight, VoidFn, Original_CGameData_MarkRevealPointsSight,
          CGameData_MarkRevealPointsSight);
S3RE_HOOK(0x00519fa0, CGameData_MarkBuildingSight, FirstFn, Original_CGameData_MarkBuildingSight,
          CGameData_MarkBuildingSight);
S3RE_HOOK(0x0051a150, CGameData_MarkSettlerSight, FirstFn, Original_CGameData_MarkSettlerSight,
          CGameData_MarkSettlerSight);
S3RE_HOOK(0x0051d5f0, CGameData_SeenObjectAt, SeenFn, Original_CGameData_SeenObjectAt, CGameData_SeenObjectAt);
S3RE_HOOK(0x00519010, CGameData_UpdateFogSlice, FirstFn, Original_CGameData_UpdateFogSlice,
          CGameData_UpdateFogSlice);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kWorld = sizeof(CGameData);

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
    worlds.Both([&](CGameData* w) { std::memcpy(w->offsets, table.data(), n * sizeof(MapOffset)); });
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
    worlds.Both([&](CGameData* w) {
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
            worlds.Both([&](CGameData* w) { w->tiles[x][y].sight = value; });
        }
    }
}

template <typename Setup, typename Call>
void Compare(test::Context& ctx, int cases, Setup setup, Call call) {
    test::WorldPair worlds(ctx, kWorld);
    SetUpOffsets(ctx, worlds);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = TestMapSize(ctx);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
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
    worlds.Both([&](CGameData* w) { w->reveal_point_high_water = high; });
    for (std::int32_t i = 0; i < 50; ++i) {
        const auto active = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3) ? 0 : 1 + ctx.rng().Below(255));
        const std::int32_t x = ctx.rng().Range(-2, static_cast<std::int32_t>(size) + 2);
        const std::int32_t y = ctx.rng().Range(-2, static_cast<std::int32_t>(size) + 2);
        worlds.Both([&](CGameData* w) {
            w->reveal_points[i].active = active;
            w->reveal_points[i].x = x;
            w->reveal_points[i].y = y;
        });
    }
    return 0;
}

std::uint32_t SetUpBuildings(test::Context& ctx, test::WorldPair& worlds, std::uint32_t size) {
    const std::uint32_t high = ctx.rng().Below(600);
    worlds.Both([&](CGameData* w) { w->building_high_water = high; });
    static constexpr std::uint8_t kTypes[] = {Value(B::LookoutTower), Value(B::SmallTower), Value(B::LargeTower),
                                              Value(B::Castle),       Value(B::Storehouse), Value(B::Shipyard)};
    for (std::uint32_t i = 0; i <= high; ++i) {
        const std::uint8_t owner = Owner(ctx);
        const std::uint16_t x = Coordinate(ctx, size);
        const std::uint16_t y = Coordinate(ctx, size);
        const std::uint8_t art = kTypes[ctx.rng().Below(6)];
        const auto buildup = static_cast<std::uint8_t>(ctx.rng().Chance(1, 4) ? ctx.rng().Below(256) : 0);
        const auto garrison = static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Below(5));
        worlds.Both([&](CGameData* w) {
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
    worlds.Both([&](CGameData* w) { w->settler_high_water = high; });
    static constexpr std::uint8_t kTypes[] = {Value(S::Thief),     Value(S::Type37),    Value(S::Donkey),
                                              Value(S::Catapult),  Value(S::Type42),    Value(S::TradeShip),
                                              Value(S::Ferry),     Value(S::Digger),    Value(S::Farmer),
                                              48 /* past the last type */};
    for (std::uint32_t i = 0; i <= high; ++i) {
        const std::uint8_t owner = Owner(ctx);
        const std::uint16_t x = Coordinate(ctx, size);
        const std::uint16_t y = Coordinate(ctx, size);
        const std::uint8_t art = kTypes[ctx.rng().Below(10)];
        worlds.Both([&](CGameData* w) {
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
            const auto terrain =
                static_cast<std::uint8_t>(ctx.rng().Chance(1, 4) ? kTerrainWornPath : ctx.rng().Below(256));
            const auto object = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Below(256));
            worlds.Both([&](CGameData* w) {
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
        worlds.Both([&](CGameData* w) {
            w->buildings[i].buildup = buildup;
            w->buildings[i].site.materials_total = total;
            w->buildings[i].site.materials_used = done;
        });
    }
}

void __fastcall WrongMarkBuildingSight(CGameData* world, void*, std::uint32_t first) {
    // Wrong: large towers see as far as small towers.
    for (std::uint32_t i = first; i <= world->building_high_water; i += 16) {
        if (world->buildings[i].art == Value(B::LargeTower)) {
            world->buildings[i].art = Value(B::SmallTower);
            CGameData_MarkBuildingSight(world, nullptr, i);
            world->buildings[i].art = Value(B::LargeTower);
        }
    }
    CGameData_MarkBuildingSight(world, nullptr, first);
}

std::uint32_t __fastcall WrongSeenObjectAt(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    // Wrong: no worn-path bit.
    return CGameData_SeenObjectAt(world, nullptr, x, y) & ~0x400u;
}

void FogCompare(test::Context& ctx, FirstFn original, FirstFn candidate, int cases) {
    Compare(
        ctx, cases,
        [&](test::WorldPair& worlds, std::uint32_t size) {
            SetUpSeenTiles(ctx, worlds, size);
            for (std::uint32_t x = 0; x < size; ++x) {
                for (std::uint32_t y = 0; y < size; ++y) {
                    const auto flags = static_cast<std::uint8_t>(ctx.rng().Next32());
                    worlds.Both([&](CGameData* w) { w->tiles[x][y].draw_flags = flags; });
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

void __fastcall WrongUpdateFogSlice(CGameData* world, void*, std::uint32_t slice) {
    // Wrong: the first tile of the slice loses its redraw bit.
    CGameData_UpdateFogSlice(world, nullptr, slice);
    const std::uint32_t y = static_cast<std::uint32_t>(static_cast<std::int32_t>(world->map_size) / 16) * slice;
    world->tiles[0][y].draw_flags &= 0x7f;
}

}  // namespace

S3RE_TEST(CGameData_MarkRevealPointsSight_MatchesOriginal) {
    Compare(
        ctx, 60, [&](test::WorldPair& worlds, std::uint32_t size) { return SetUpRevealPoints(ctx, worlds, size); },
        [&](test::WorldPair& worlds, std::uint32_t) {
            Original_CGameData_MarkRevealPointsSight(worlds.original(), nullptr);
            CGameData_MarkRevealPointsSight(worlds.ours(), nullptr);
            return true;
        });
}

S3RE_TEST(CGameData_MarkBuildingSight_MatchesOriginal) {
    Compare(
        ctx, 60, [&](test::WorldPair& worlds, std::uint32_t size) { return SetUpBuildings(ctx, worlds, size); },
        [&](test::WorldPair& worlds, std::uint32_t first) {
            Original_CGameData_MarkBuildingSight(worlds.original(), nullptr, first);
            CGameData_MarkBuildingSight(worlds.ours(), nullptr, first);
            return true;
        });
}

S3RE_TEST(CGameData_MarkBuildingSight_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Compare(
        probe, 30, [&](test::WorldPair& worlds, std::uint32_t size) { return SetUpBuildings(probe, worlds, size); },
        [&](test::WorldPair& worlds, std::uint32_t first) {
            Original_CGameData_MarkBuildingSight(worlds.original(), nullptr, first);
            WrongMarkBuildingSight(worlds.ours(), nullptr, first);
            return true;
        });
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CGameData_MarkSettlerSight_MatchesOriginal) {
    Compare(
        ctx, 60, [&](test::WorldPair& worlds, std::uint32_t size) { return SetUpSettlers(ctx, worlds, size); },
        [&](test::WorldPair& worlds, std::uint32_t first) {
            Original_CGameData_MarkSettlerSight(worlds.original(), nullptr, first);
            CGameData_MarkSettlerSight(worlds.ours(), nullptr, first);
            return true;
        });
}

S3RE_TEST(CGameData_SeenObjectAt_MatchesOriginal) {
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
                    const std::uint32_t expected = Original_CGameData_SeenObjectAt(worlds.original(), nullptr, x, y);
                    const std::uint32_t actual = CGameData_SeenObjectAt(worlds.ours(), nullptr, x, y);
                    if (!S3RE_EXPECT_EQ(expected & 0xffff, actual & 0xffff)) {
                        ctx.Fail(__FILE__, __LINE__, "  tile (%u, %u)", x, y);
                        return false;
                    }
                }
            }
            return true;
        });
}

S3RE_TEST(CGameData_SeenObjectAt_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    test::WorldPair worlds(probe, kWorld);
    worlds.Both([&](CGameData* w) { w->map_size = 64; });
    SetUpSeenTiles(probe, worlds, 64);
    SetUpProgress(probe, worlds);
    bool differs = false;
    for (std::uint32_t x = 0; x < 64 && !differs; ++x) {
        for (std::uint32_t y = 0; y < 64 && !differs; ++y) {
            differs = (Original_CGameData_SeenObjectAt(worlds.original(), nullptr, x, y) & 0xffff) !=
                      (WrongSeenObjectAt(worlds.ours(), nullptr, x, y) & 0xffff);
        }
    }
    S3RE_EXPECT(differs);
}

S3RE_TEST(CGameData_UpdateFogSlice_MatchesOriginal) {
    FogCompare(ctx, Original_CGameData_UpdateFogSlice, &CGameData_UpdateFogSlice, 40);
}

S3RE_TEST(CGameData_UpdateFogSlice_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    FogCompare(probe, Original_CGameData_UpdateFogSlice, &WrongUpdateFogSlice, 60);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
