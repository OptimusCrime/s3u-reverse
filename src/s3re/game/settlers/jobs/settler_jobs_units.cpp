// CGameData::UpdateSettlers (0x004e9670), job handlers 0x4e971c..0x4eb43b of
// the original (settler_jobs.h):
//
// - job 0 New (0x4e971c): a new settler gets a direction and goes into an
//   idle list: units (class != 0) into its player's list per type (job
//   UnitIdle), civilians into its area's list per type (job Nothing);
// - job 0x8e (0x4e9884): a unit that just left a fight: UpdateSoldier, else
//   back to UnitIdle;
// - job 0x37 UnitIdle (0x4e98be): idle units: thieves at home, ships,
//   geologists and pioneers look for a spot to work, soldiers check for
//   enemies (UpdateSoldier), everyone else steps aside from crowded tiles;
//   ferries and trade ships then pick a direction on the water;
// - job 0xae GuardBuilding (0x4ea314): the guard of a tower watches the
//   eight nearest rings; with no enemy soldier there it becomes a new
//   settler (type 37) and goes inside;
// - job 0xad (0x4ea531): heals by one point every 20 ticks;
// - job 0xac SeekOwnArea (0x4ea55d): a civilian on foreign ground looks for
//   a free tile of its own player's area;
// - job 1 Nothing (0x4ea855): idle civilians; surplus carriers are marked
//   (flag 0x80) and play an idle animation; crowded tiles are left;
// - job 0xb0 (0x4eacd2), job 0x35 UnitWalk (0x4eacfc) and the 45 walking
//   jobs (0x4ead27): WalkSettler (the walking jobs repeat the settler when
//   it returns non-zero);
// - job 4 ArriveAtWork (0x4ead38): a worker reaches its building and starts
//   the first job of its chain, by settler type (table 0x4f98c4) and for
//   some types by building type (mines: tables 0x4f9984 / 0x4f999c);
// - job 0x170 (0x4eb3d2): the same start as type 45 (beekeeper).
//
// Frame slots (settler_jobs.h FrameSlots): the searches write slot 14 (y)
// and slot 20 (x) of every tile they look at, the ship steering the low
// byte of slot 44; all are mirrored into ctx.frame. No handler of this range
// reads a slot left over from another settler (the reads of slot 44's upper
// bytes at 0x4ea29d / 0x4ea2c5 are masked to the low byte).

#include "game/settlers/jobs/settler_jobs.h"
#include "game/settlers/jobs/settler_jobs_test.h"
#include "test/callee_stub.h"
#include "game/enums/settler_types.h"
#include "game/enums/buildings.h"
#include "game/enums/terrain.h"

#include <cstdint>

namespace s3re::game::settler_jobs {

namespace {

using S = SettlerType;
using B = BuildingType;

constexpr std::uint8_t kNoPlayer = 0xff;
constexpr std::uint8_t kSettlerFlagMarked = 0x80;  // Settler::flags: a surplus carrier (job Nothing)
constexpr std::uint16_t kNoSpot = 0xffff;          // saved_animation / saved_counter: no work spot
constexpr std::uint32_t kDirections = 6;
constexpr std::uint8_t kTileTargeted = 0x02;       // Tile::flags bit set on a chosen work spot
constexpr std::uint8_t kTileOccupiedMask = 0x0f;   // Tile::flags bits that keep a settler off a tile

// ------------------------------------------------------------------ helpers

std::uint32_t RandomWord(CGameData* world) {
    return static_cast<std::uint16_t>(Call<kRandom>(world));  // only AX is defined
}

bool IsOnMap(CGameData* world, std::uint32_t x, std::uint32_t y) {
    return Call<kIsOnMap>(world, x, y) != 0;
}

bool Inside(CGameData* world, std::uint32_t x, std::uint32_t y) {
    return x < world->map_size && y < world->map_size;
}

// The tile (dx, dy) away from the tile at `offset`, with the original's
// 32-bit offset arithmetic.
Tile& Near(CGameData* world, std::uint32_t offset, std::int32_t dx, std::int32_t dy) {
    return TileAtOffset(world, offset + static_cast<std::uint32_t>(dx) * kTileRowBytes +
                                   static_cast<std::uint32_t>(dy) * kTileBytes);
}

Tile& OwnTile(SettlerContext& ctx) {
    return TileAt(ctx.world, ctx.settler.x, ctx.settler.y);
}

void SetTarget(SettlerContext& ctx, std::uint32_t x, std::uint32_t y) {
    Call<kSetSettlerTarget>(ctx.world, ctx.number, x, y);
}

// Starts walking to the target just set (job UnitWalk).
void StartUnitWalk(Settler& settler) {
    SetJob(settler, Job::UnitWalk);
    settler.walk_state = 0;
    settler.counter = 0;
}

// The first neighbour (in the original's order) with a settler on it, as a
// direction; -1 if none.
std::int32_t CrowdedSide(CGameData* world, std::uint32_t offset) {
    if (Near(world, offset, -1, 0).settler != 0) return 0;
    if (Near(world, offset, -1, -1).settler != 0) return 1;
    if (Near(world, offset, 0, -1).settler != 0) return 2;
    if (Near(world, offset, 1, 0).settler != 0) return 3;
    if (Near(world, offset, 1, 1).settler != 0) return 4;
    if (Near(world, offset, 0, 1).settler != 0) return 5;
    return -1;
}

bool NeighboursFree(CGameData* world, std::uint32_t offset) {
    return Near(world, offset, 1, 0).settler == 0 && Near(world, offset, -1, 0).settler == 0 &&
           Near(world, offset, 0, 1).settler == 0 && Near(world, offset, 0, -1).settler == 0 &&
           Near(world, offset, -1, -1).settler == 0 && Near(world, offset, 1, 1).settler == 0;
}

// Turns the facing direction one step now and then (Random above `limit`).
JobResult MaybeTurn(CGameData* world, Settler& settler, std::uint32_t limit) {
    if (RandomWord(world) <= limit) {
        return JobResult::Next;
    }
    const std::uint32_t direction = settler.direction + 1;
    settler.direction = direction;
    if (direction > 5) {
        settler.direction = 0;
    }
    return JobResult::Next;
}

// Steps aside from a crowded tile (0x4e9e86 / 0x4eaa41): starting at the
// side `side` (turned one step left or right now and then), the first
// neighbour the settler may enter becomes its target. Returns whether one
// was found.
bool StepAside(SettlerContext& ctx, std::uint32_t side, Job walkJob) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    std::uint32_t direction = side;
    const std::uint32_t r = RandomWord(world);
    if (r < 10000) {
        direction = CallStd<kPreviousDirection>(direction);
    } else if (r < 20000) {
        direction = CallStd<kNextDirection>(direction);
    }
    for (std::uint32_t tries = 0; tries != kDirections;) {
        const std::uint32_t x = CallStd<kNeighbourX>(settler.x, direction);
        ctx.frame.slot20 = x;
        const std::uint32_t y = CallStd<kNeighbourY>(settler.y, direction);
        ctx.frame.slot14 = y;
        if (Inside(world, x, y) &&
            Call<kCanSettlerEnterTile>(world, x, y, settler.owner, settler.art) != 0) {
            SetTarget(ctx, x, y);
            SetJob(settler, walkJob);
            if (walkJob == Job::UnitWalk) {
                settler.walk_state = 0;
            }
            settler.counter = 0;
            return true;
        }
        ++tries;
        ++direction;
        if (direction == kDirections) {
            direction = 0;
        }
    }
    return false;
}

// ------------------------------------------------------------------ job 0

// 0x4e971c: a new settler.
JobResult NewSettler(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    const std::uint32_t n = ctx.number;
    settler.frame = 0;
    settler.counter = 0;
    settler.direction = Call<kRandomBelow6>(world);
    if ((&g_settlerTypeClass[0])[settler.art] != 0) {
        // Units: their player's idle list per type.
        SetJob(settler, Job::UnitIdle);
        if (TypeOf(settler) == S::Pioneer || TypeOf(settler) == S::Geologist) {
            settler.saved_animation = kNoSpot;
            settler.saved_counter = kNoSpot;
        }
        if (TypeOf(settler) == S::Ferry) {
            return JobResult::Again;
        }
        std::uint16_t& head = PlayerAt(world, settler.owner).settler_lists[settler.art];
        settler.next = head;
        if (head != 0) {
            SettlerAt(world, head).prev = static_cast<std::uint16_t>(n);
        }
        settler.prev = 0;
        PlayerAt(world, settler.owner).settler_lists[settler.art] = static_cast<std::uint16_t>(n);
        return JobResult::Again;
    }
    // Civilians: the idle list per type of the area they stand in.
    SetJob(settler, Job::Nothing);
    const std::uint32_t area = OwnTile(ctx).area;
    const std::uint16_t head = AreaAt(world, area).settlers_by_type[settler.art];
    settler.next = head;
    if (head != 0) {
        SettlerAt(world, head).prev = static_cast<std::uint16_t>(n);
    }
    settler.prev = 0;
    AreaAt(world, area).settlers_by_type[settler.art] = static_cast<std::uint16_t>(n);
    return JobResult::Again;
}

// ------------------------------------------------------------------ job 0x8e

// 0x4e9884: after a fight.
JobResult AfterFight(SettlerContext& ctx) {
    if (Call<kUpdateSoldier>(ctx.world, ctx.number) != 0) {
        return JobResult::Next;
    }
    SetJob(ctx.settler, Job::UnitIdle);
    ctx.settler.counter = 5;
    ctx.settler.frame = 0;  // AL of UpdateSoldier's 0
    ctx.settler.direction = Call<kRandomBelow6>(ctx.world);
    return JobResult::Next;
}

// ------------------------------------------------------------------ job 0x37

// 0x4e9947: a geologist looks for a mountain tile without objects around it
// near its work spot (saved_animation, saved_counter), the nearest of the
// first ones found. Returns whether it found one (and walks there).
bool GeologistSearch(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    bool found = false;
    std::uint32_t looked = 0;
    std::uint32_t best = 0xffffffff;
    std::uint32_t bestX = 0;
    std::uint32_t bestY = 0;
    for (std::uint32_t k = 0; k < 500; ++k) {
        const layout::MapOffset& o = world->offsets[k];
        const std::uint32_t x = static_cast<std::uint32_t>(o.dx) + settler.saved_animation;
        ctx.frame.slot20 = x;
        const std::uint32_t y = static_cast<std::uint32_t>(o.dy) + settler.saved_counter;
        ctx.frame.slot14 = y;
        if (IsOnMap(world, x, y)) {
            const std::uint32_t at = TileOffset(x, y);
            const Tile& t = TileAtOffset(world, at);
            const std::uint32_t sx = settler.x;
            const std::uint32_t sy = settler.y;
            bool usable = t.walk_region == TileAt(world, sx, sy).walk_region &&
                          (t.flags & kTileOccupiedMask) == 0 &&
                          (t.terrain & kTerrainClassMask) == kTerrainClassMountain && t.object == 0;
            // No objects on twelve tiles around it either.
            static constexpr std::int8_t kAround[12][2] = {{-1, 0}, {1, 0},  {0, -1}, {0, 1},  {-1, -1}, {1, 1},
                                                           {2, 1},  {1, 2},  {-1, 1}, {-2, -1}, {-1, -2}, {1, -1}};
            for (const auto& d : kAround) {
                usable = usable && Near(world, at, d[0], d[1]).object == 0;
            }
            if (usable) {
                const std::uint32_t distance = CallStd<kTileDistance>(x, y, sx, sy);
                if (distance < best) {
                    best = distance;
                    found = true;
                    bestX = x;
                    bestY = y;
                    ++looked;
                } else if (found) {
                    ++looked;
                }
            } else if (found) {
                ++looked;
            }
        }
        if (looked > 50) {
            break;
        }
    }
    if (found) {
        SetTarget(ctx, bestX, bestY);
        TileAt(world, bestX, bestY).flags |= kTileTargeted;
        StartUnitWalk(settler);
        return true;
    }
    settler.saved_animation = kNoSpot;
    settler.saved_counter = kNoSpot;
    return false;
}

// 0x4e9bba: a pioneer looks for a tile not owned by its player near its
// work spot, the nearest of the first ones found. Returns whether it found
// one (and walks there).
bool PioneerSearch(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    bool found = false;
    std::uint32_t looked = 0;
    std::uint32_t best = 0xffffffff;
    std::uint32_t bestX = 0;
    std::uint32_t bestY = 0;
    for (std::uint32_t k = 0; k < 1000; ++k) {
        const layout::MapOffset& o = world->offsets[k];
        const std::uint32_t x = static_cast<std::uint32_t>(o.dx) + settler.saved_animation;
        ctx.frame.slot20 = x;
        const std::uint32_t y = static_cast<std::uint32_t>(o.dy) + settler.saved_counter;
        ctx.frame.slot14 = y;
        if (IsOnMap(world, x, y)) {
            const Tile& t = TileAt(world, x, y);
            const std::uint32_t sx = settler.x;
            const std::uint32_t sy = settler.y;
            const bool usable = t.walk_region == TileAt(world, sx, sy).walk_region && (t.flags & 0x4f) == 0 &&
                                (t.owner == kNoPlayer || t.owner != settler.owner);
            if (usable) {
                const std::uint32_t distance = CallStd<kTileDistance>(x, y, sx, sy);
                if (distance < best) {
                    found = true;
                    best = distance;
                    bestX = x;
                    bestY = y;
                    ++looked;
                } else if (found) {
                    ++looked;
                }
            } else if (found) {
                ++looked;
            }
        }
        if (looked > 50) {
            break;
        }
    }
    if (found) {
        SetTarget(ctx, bestX, bestY);
        TileAt(world, bestX, bestY).flags |= kTileTargeted;
        StartUnitWalk(settler);
        return true;
    }
    // Nothing left: stay here (or give up if already here).
    if (settler.saved_animation == settler.x && settler.saved_counter == settler.y) {
        settler.saved_animation = kNoSpot;
        settler.saved_counter = kNoSpot;
    } else {
        settler.saved_animation = settler.x;
        settler.saved_counter = settler.y;
    }
    return false;
}

// 0x4ea0c9: ferries and trade ships (and units that just set a target
// above): the side of the open water, away from other ships; the ship then
// heads for the neighbour on that side.
JobResult SteerShip(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    const std::uint32_t sx = settler.x;
    const std::uint32_t sy = settler.y;
    std::uint8_t side = 0xff;  // the original's slot 44 (low byte)
    ctx.frame.slot44 = (ctx.frame.slot44 & 0xffffff00) | side;
    // Per direction (MapOffset::sector, 0..5 in the game's table): ships seen
    // and, for a ferry on water, land next to it. The original counts in a
    // six-byte stack array indexed by the table value unchecked.
    std::uint8_t counts[kDirections] = {};
    auto count = [&](std::int32_t sector) {
        if (static_cast<std::uint32_t>(sector) < kDirections) {
            ++counts[sector];
        }
    };
    if (OwnTile(ctx).terrain == kTerrainWater5 && TypeOf(settler) == S::Ferry) {
        for (std::uint32_t k = 0; k < 6; ++k) {
            const layout::MapOffset& o = world->offsets[k];
            const std::uint32_t x = sx + static_cast<std::uint32_t>(o.dx);
            const std::uint32_t y = sy + static_cast<std::uint32_t>(o.dy);
            ctx.frame.slot14 = y;
            if (IsOnMap(world, x, y) && TileAt(world, x, y).terrain != kTerrainWater5) {
                count(o.sector);
            }
        }
    }
    for (std::uint32_t k = 1; k <= 216; ++k) {
        const layout::MapOffset& o = world->offsets[k];
        const std::uint32_t x = sx + static_cast<std::uint32_t>(o.dx);
        const std::uint32_t y = sy + static_cast<std::uint32_t>(o.dy);
        ctx.frame.slot20 = x;
        ctx.frame.slot14 = y;
        if (!IsOnMap(world, x, y)) {
            continue;
        }
        const Tile& t = TileAt(world, x, y);
        if (t.area != OwnTile(ctx).area || t.terrain < 5 || t.terrain > 7 || t.settler == 0) {
            continue;
        }
        const S other = TypeOf(SettlerAt(world, t.settler));
        if (other != S::Ferry && other != S::TradeShip) {
            continue;
        }
        side = static_cast<std::uint8_t>((o.sector + 3) % 6);  // the opposite side (signed division)
        count(o.sector);
        ctx.frame.slot44 = (ctx.frame.slot44 & 0xffffff00) | side;
    }
    if (side == 0xff) {
        return JobResult::Next;
    }
    if (side < kDirections && counts[side] != 0) {
        // That side is taken too: the last side without anything.
        for (std::uint8_t i = 0; i < kDirections; ++i) {
            if (counts[i] == 0) {
                side = i;
                ctx.frame.slot44 = (ctx.frame.slot44 & 0xffffff00) | side;
            }
        }
    }
    SetJob(settler, Job::UnitWalk);
    settler.counter = 0;
    const std::uint32_t y = CallStd<kNeighbourY>(sy, side);
    const std::uint32_t x = CallStd<kNeighbourX>(sx, side);
    SetTarget(ctx, x, y);
    return JobResult::Next;
}

// 0x4e9eea: a unit on a blocked tile without settlers next to it walks to
// the first tile of the offset table it may enter (no limit: the search
// goes on until one is found).
JobResult LeaveBlockedTile(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    for (std::uint32_t k = 0;; ++k) {
        const layout::MapOffset& o = world->offsets[k];
        const std::uint32_t x = static_cast<std::uint32_t>(o.dx) + settler.x;
        ctx.frame.slot20 = x;
        const std::uint32_t y = settler.y + static_cast<std::uint32_t>(o.dy);
        ctx.frame.slot14 = y;
        if (Inside(world, x, y) && Call<kCanSettlerEnterTile>(world, x, y, settler.owner, settler.art) != 0 &&
            (TileAt(world, x, y).flags & kTileOccupiedMask) == 0) {
            SetTarget(ctx, x, y);
            StartUnitWalk(settler);
            return JobResult::Next;
        }
    }
}

// 0x4e98be: an idle unit.
JobResult UnitIdle(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    bool targetSet = false;  // the original's slot 4C
    // A thief back on its own ground with its loot.
    if (static_cast<std::uint8_t>(ctx.type) == Value(S::Thief) && settler.slot != 0 &&
        OwnTile(ctx).owner == settler.owner) {
        settler.counter = 0;
        SetJob(settler, Job::ThiefReturned);
    }
    if (TypeOf(settler) == S::TradeShip || TypeOf(settler) == S::Ferry) {
        settler.counter = 10;
        targetSet = true;
    }
    if (TypeOf(settler) == S::Geologist && settler.saved_animation != kNoSpot && GeologistSearch(ctx)) {
        targetSet = true;
    }
    if (TypeOf(settler) == S::Pioneer && settler.saved_animation != kNoSpot && PioneerSearch(ctx)) {
        return SteerShip(ctx);  // 0x4e9d47 jumps there directly
    }
    if (targetSet) {
        return SteerShip(ctx);
    }
    // Soldiers look for a fight now and then.
    if (RandomWord(world) > 50000) {
        const std::int32_t typeClass = (&g_settlerTypeClass[0])[settler.art];
        if (typeClass >= 1 && typeClass <= 9 && Call<kUpdateSoldier>(world, ctx.number) != 0) {
            return JobResult::Next;
        }
    }
    const std::uint32_t here = TileOffset(settler.x, settler.y);
    if ((TileAtOffset(world, here).flags & kTileOccupiedMask) == 0 && NeighboursFree(world, here)) {
        settler.counter = 5;
        settler.frame = 0;
        return MaybeTurn(world, settler, 65000);
    }
    const std::int32_t side = CrowdedSide(world, here);
    if (side < 0) {
        return LeaveBlockedTile(ctx);
    }
    if (!StepAside(ctx, static_cast<std::uint32_t>(side), Job::UnitWalk)) {
        settler.counter = 0;
    }
    return JobResult::Next;
}

// ------------------------------------------------------------------ job 0xae

// 0x4ea314: the guard of a tower.
JobResult GuardBuilding(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    bool enemyNear = false;
    // Enemy soldiers (class 1 or 3) of another team within the rings closer than 8.
    for (std::uint32_t k = 0; world->offsets[k].distance < 8; ++k) {
        const layout::MapOffset& o = world->offsets[k];
        const std::uint32_t x = settler.x + static_cast<std::uint32_t>(o.dx);
        ctx.frame.slot20 = x;
        const std::uint32_t y = settler.y + static_cast<std::uint32_t>(o.dy);
        ctx.frame.slot14 = y;
        if (!Inside(world, x, y)) {
            continue;
        }
        const std::uint32_t s = TileAt(world, x, y).settler;
        if (s == 0) {
            continue;
        }
        const Settler& other = SettlerAt(world, s);
        if (PlayerAt(world, other.owner).team == PlayerAt(world, settler.owner).team) {
            continue;
        }
        const std::int32_t otherClass = (&g_settlerTypeClass[0])[other.art];
        if (otherClass == 1 || otherClass == 3) {
            enemyNear = true;
            break;
        }
    }
    if (enemyNear) {
        if (Call<kUpdateSoldier>(world, ctx.number) == 0) {
            settler.counter = 5;
            settler.direction = 2;
        }
        return JobResult::Next;
    }
    // Nobody to fight: a new guard (type 37) takes over inside.
    TileAt(world, settler.x, settler.y).settler = 0;
    const std::uint32_t guard = Call<kCreateSettler>(world, settler.x, settler.y, 37, settler.owner);
    Settler& created = SettlerAt(world, guard);
    SetJob(created, Job::GuardHeal);
    created.building = settler.building;
    created.health = 1;
    created.counter = 0;
    BuildingAt(world, settler.building).guard = static_cast<std::uint16_t>(guard);
    const std::int32_t typeClass = (&g_settlerTypeClass[0])[settler.art];
    if (typeClass == 1) {
        SetJob(settler, Job::SwordsmanInTower);
    } else if (typeClass == 2) {
        Call<kEnterBuildingSlot>(world, ctx.number);
        SetJob(settler, Job::BowmanInTower);
    } else if (typeClass == 3) {
        SetJob(settler, Job::PikemanInTower);
    }
    settler.counter = 0;
    return JobResult::Next;
}

// 0x4ea531: heals one point every 20 ticks up to 100.
JobResult Heal(SettlerContext& ctx) {
    ctx.settler.counter = 20;
    const std::uint8_t health = ctx.settler.health;
    if (health < 100 && health != 0) {
        ctx.settler.health = static_cast<std::uint8_t>(health + 1);
    }
    return JobResult::Next;
}

// ------------------------------------------------------------------ job 0xac

// 0x4ea55d: a civilian on foreign ground looks for a free tile of its
// player's area (one with no settlers around it, in its own area if
// possible) and walks there (job WalkToOwnArea).
JobResult SeekOwnArea(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    if (ctx.typeClass != 0) {
        Call<kClearSettlerJob>(world, ctx.number);
        return JobResult::Next;
    }
    std::uint32_t found = 0;  // 1: in its own area (target set), 2: elsewhere (fallback)
    std::uint32_t fallbackX = 0;
    std::uint32_t fallbackY = 0;
    for (std::uint32_t k = 1; k < 9900 && found != 1; ++k) {
        const layout::MapOffset& o = world->offsets[k];
        const std::uint32_t y = static_cast<std::uint32_t>(o.dy) + settler.y;
        ctx.frame.slot14 = y;
        const std::uint32_t x = static_cast<std::uint32_t>(o.dx) + settler.x;
        ctx.frame.slot20 = x;
        if (!Inside(world, x, y)) {
            continue;
        }
        const Tile& t = TileAt(world, x, y);
        if (t.walk_region != OwnTile(ctx).walk_region) {
            continue;
        }
        const std::uint16_t area = t.area;
        if (area == 0 || area >= layout::kTileAreaWater || AreaAt(world, area).player != settler.owner || t.settler != 0) {
            continue;
        }
        std::uint32_t neighbours = 0;
        for (std::uint32_t j = 1; j <= 6; ++j) {
            const std::uint32_t nx = x + static_cast<std::uint32_t>(world->offsets[j].dx);
            const std::uint32_t ny = y + static_cast<std::uint32_t>(world->offsets[j].dy);
            if (IsOnMap(world, nx, ny) && TileAt(world, nx, ny).settler != 0) {
                ++neighbours;
            }
        }
        if (neighbours != 0) {
            continue;
        }
        const std::uint16_t ownArea = OwnTile(ctx).area;
        if (ownArea != 0 && ownArea == area) {
            found = 1;
            SetTarget(ctx, x, y);
        } else if (found == 0) {
            fallbackX = x;
            fallbackY = y;
            found = 2;
        }
    }
    if (found == 2) {
        SetTarget(ctx, fallbackX, fallbackY);
    } else if (found == 0) {
        // Random tiles of the first 1024 offsets until one is free and reachable.
        for (;;) {
            const layout::MapOffset& o = world->offsets[RandomWord(world) & 0x3ff];
            const std::uint32_t x = static_cast<std::uint32_t>(o.dx) + settler.x;
            const std::uint32_t y = static_cast<std::uint32_t>(o.dy) + settler.y;
            ctx.frame.slot20 = x;
            ctx.frame.slot14 = y;
            if (!Inside(world, x, y)) {
                continue;
            }
            const Tile& t = TileAt(world, x, y);
            if (t.walk_region != 0 && t.walk_region == OwnTile(ctx).walk_region && t.settler == 0) {
                SetTarget(ctx, x, y);
                break;
            }
        }
    }
    settler.counter = 0;
    SetJob(settler, Job::WalkToOwnArea);
    return JobResult::Next;
}

// ------------------------------------------------------------------ job 1

// 0x4ea855: an idle civilian.
JobResult Nothing(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    const std::uint8_t groundOwner = OwnTile(ctx).owner;
    if (groundOwner != settler.owner) {
        GameDebugLog(texts::kSettlerOnForeignGround, ctx.number, ctx.type, static_cast<std::uint32_t>(settler.owner),
                     static_cast<std::uint32_t>(groundOwner));
    }
    const std::uint32_t here = TileOffset(settler.x, settler.y);
    if ((TileAtOffset(world, here).flags & kTileOccupiedMask) == 0 && NeighboursFree(world, here)) {
        // Carriers beyond the player's share are marked and wait.
        Player& player = PlayerAt(world, settler.owner);
        const std::uint32_t surplus = player.surplus_carriers;
        const std::uint32_t marked = player.surplus_carriers_marked;
        if (surplus != marked && TypeOf(settler) == S::Carrier) {
            if (surplus > marked) {
                if ((settler.flags & kSettlerFlagMarked) == 0) {
                    settler.flags |= kSettlerFlagMarked;
                    player.surplus_carriers_marked = marked + 1;
                }
            } else if ((settler.flags & kSettlerFlagMarked) != 0) {
                settler.direction = 0;
                settler.flags &= static_cast<std::uint8_t>(~kSettlerFlagMarked);
                player.surplus_carriers_marked = marked - 1;
            }
        }
        if ((settler.flags & kSettlerFlagMarked) != 0) {
            const std::uint32_t animation = (Call<kRandom>(world) & 1) != 0 ? 0x123 : 0x124;
            Call<kSetSettlerAnimation>(world, ctx.number, animation);
            return JobResult::Next;
        }
        settler.counter = 5;
        return MaybeTurn(world, settler, 63000);
    }
    const std::int32_t side = CrowdedSide(world, here);
    if (side >= 0) {
        if (!StepAside(ctx, static_cast<std::uint32_t>(side), Job::StepAside)) {
            Call<kResetSettlerToIdle>(world, ctx.number);
        }
        return JobResult::Next;
    }
    // 0x4eaab5: the first free tile of its area in the offset table.
    for (std::uint32_t k = 0; k < 9900; ++k) {
        const layout::MapOffset& o = world->offsets[k];
        const std::uint32_t x = static_cast<std::uint32_t>(o.dx) + settler.x;
        ctx.frame.slot20 = x;
        const std::uint32_t y = settler.y + static_cast<std::uint32_t>(o.dy);
        ctx.frame.slot14 = y;
        if (!Inside(world, x, y)) {
            continue;
        }
        const Tile& t = TileAt(world, x, y);
        if (t.area == OwnTile(ctx).area && (t.flags & kTileOccupiedMask) == 0 && t.settler == 0) {
            SetTarget(ctx, x, y);
            SetJob(settler, Job::StepAside);
            settler.counter = 0;
            return JobResult::Next;
        }
    }
    Call<kResetSettlerToIdle>(world, ctx.number);
    return JobResult::Next;
}

// ------------------------------------------------------------------ walking

// 0x4eacd2: job 0xb0 counts ship_walk down to 0, then continues as UnitWalk.
JobResult WalkCountingDown(SettlerContext& ctx) {
    std::uint8_t& count = ctx.settler.ship_walk;
    if (count != 0) {
        --count;
        if (count == 0) {
            SetJob(ctx.settler, Job::UnitWalk);
        }
    }
    Call<kWalkSettler>(ctx.world, ctx.number);  // EAX ignored
    return JobResult::Next;
}

// 0x4eacfc: a walking unit; soldiers may fight on the way.
JobResult UnitWalk(SettlerContext& ctx) {
    if (ctx.typeClass >= 1 && ctx.typeClass <= 9 && Call<kUpdateSoldier>(ctx.world, ctx.number) != 0) {
        return JobResult::Next;
    }
    Call<kWalkSettler>(ctx.world, ctx.number);  // EAX ignored
    return JobResult::Next;
}

// 0x4ead27: the walking jobs.
JobResult Walk(SettlerContext& ctx) {
    return Call<kWalkSettler>(ctx.world, ctx.number) != 0 ? JobResult::Again : JobResult::Next;
}

// ------------------------------------------------------------------ job 4

// The common start of a work chain: into the building, first job, its animation.
JobResult StartWork(SettlerContext& ctx, Job job, std::uint32_t animation) {
    Call<kMoveSettlerToBuildingTileA>(ctx.world, ctx.number);
    SetJob(ctx.settler, job);
    Call<kSetSettlerAnimation>(ctx.world, ctx.number, animation);
    return JobResult::Next;
}

// Soldiers: into the building, first job, the soldier animation of its slot.
JobResult StartGuard(SettlerContext& ctx, Job job) {
    Call<kMoveSettlerToBuildingTileA>(ctx.world, ctx.number);
    SetJob(ctx.settler, job);
    Call<kSetSoldierAnimationInBuilding>(ctx.world, ctx.number);
    return JobResult::Next;
}

// 0x4f91fb with EDX = 0.
JobResult Wait(SettlerContext& ctx) {
    ctx.settler.counter = 0;
    return JobResult::Next;
}

// 0x4ead4c: a carrier arriving at a barracks area: recruited only while the
// area keeps its share (+0x23e, percent of the civilians) of carriers.
JobResult CarrierArrives(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    const std::uint32_t area = OwnTile(ctx).area;
    const std::uint32_t civilians = Call<kCountAreaSettlersOfClass0>(world, area);
    const std::uint32_t keep = civilians * AreaAt(world, area).settings_extra[0] / 100;
    if (keep >= Call<kCountAreaSettlersOfType>(world, area, 0)) {
        Call<kResetSettlerToIdle>(world, ctx.number);
        return JobResult::Next;
    }
    SetJob(ctx.settler, Job::RecruitAtBarracks);
    ctx.settler.counter = 0;
    return JobResult::Next;
}

// 0x4eafac: a miner, by the type of its mine (tables 0x4f999c / 0x4f9984).
JobResult MinerArrives(SettlerContext& ctx, std::uint8_t mine) {
    switch (static_cast<B>(mine)) {
    case B::CoalMine: return StartWork(ctx, Job::MinerEnterCoalMine, 0x5b);
    case B::GoldMine: return StartWork(ctx, Job::MinerEnterGoldMine, 0x5c);
    case B::IronMine: return StartWork(ctx, Job::MinerEnterIronMine, 0x5d);
    case B::GemMine: return StartWork(ctx, Job::MinerEnterGemMine, 0xd6);
    case B::SulfurMine:
    case B::AmazonSulfurMine: return StartWork(ctx, Job::MinerEnterSulfurMine, 0xd7);
    default: return JobResult::Next;
    }
}

// 0x4eb08b: a smith, by its building (tool / weapon smith, war machines, gong).
JobResult SmithArrives(SettlerContext& ctx, std::uint8_t building) {
    switch (static_cast<B>(building)) {
    case B::Toolsmith: return StartWork(ctx, Job::ToolsmithGoIn, 0x74);
    case B::Weaponsmith: return StartWork(ctx, Job::WeaponSmithGoIn, 0x75);
    case B::CatapultWorkshop: return StartWork(ctx, Job::CatapultWorkshopGoIn, 0x112);
    case B::BallistaWorkshop: return StartWork(ctx, Job::BallistaWorkshopGoIn, 0x112);
    case B::CannonWorkshop: return StartWork(ctx, Job::CannonWorkshopGoIn, 0x112);
    case B::LargeGong: return StartWork(ctx, Job::GongGoIn, 0x112);
    default: return JobResult::Next;
    }
}

// 0x4ead38: a worker arrives at its building (table 0x4f98c4 by type).
JobResult ArriveAtWork(SettlerContext& ctx) {
    if (ctx.type > 47) {
        return Wait(ctx);
    }
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    const auto buildingType = [&] { return BuildingAt(world, settler.building).art; };
    switch (static_cast<S>(ctx.type)) {
    case S::Carrier: return CarrierArrives(ctx);
    case S::Digger:  // 0x4eadd0: one more worker at the site
        ++BuildingAt(world, settler.building).site.workers_at_work;
        SetJob(settler, Job::DiggerDig);
        return Wait(ctx);
    case S::Builder:  // 0x4eae05
        ++BuildingAt(world, settler.building).site.unk1e;
        SetJob(settler, Job::BuilderStart);
        return Wait(ctx);
    case S::Woodcutter: return StartWork(ctx, Job::WoodcutterGoIn, 4);
    case S::Stonecutter: return StartWork(ctx, Job::StonecutterGoIn, 15);
    case S::Swordsman1:
    case S::Swordsman2:
    case S::Swordsman3: return StartGuard(ctx, Job::SwordsmanEnterTower);
    case S::Bowman1:
    case S::Bowman2:
    case S::Bowman3: return StartGuard(ctx, Job::BowmanEnterTower);
    case S::Pikeman1:
    case S::Pikeman2:
    case S::Pikeman3: return StartGuard(ctx, Job::PikemanEnterTower);
    case S::Sawmiller: return StartWork(ctx, Job::SawmillerGoIn, 0x15);
    case S::Forester: return StartWork(ctx, Job::ForesterGoIn, 0x1b);
    case S::Smelter:
        if (buildingType() == Value(B::GoldSmelter)) {
            return StartWork(ctx, Job::GoldSmelterGoIn, 0x47);
        }
        return StartWork(ctx, Job::IronSmelterGoIn, 0x50);
    case S::Miner: {
        const std::uint32_t mine = buildingType();
        if (mine - 7 > 49) {
            return JobResult::Next;
        }
        return MinerArrives(ctx, static_cast<std::uint8_t>(mine));
    }
    case S::Smith: return SmithArrives(ctx, buildingType());
    case S::Miller: return StartWork(ctx, Job::MillerGoIn, 0x7a);
    case S::Baker: return StartWork(ctx, Job::BakerGoIn, 0x7b);
    case S::Butcher: return StartWork(ctx, Job::ButcherGoIn, 0x78);
    case S::Farmer: return StartWork(ctx, Job::FarmerGoIn, 0x79);
    case S::Fisher: return StartWork(ctx, Job::FisherGoIn, 0x76);
    case S::Waterworker: return StartWork(ctx, Job::WaterworkerGoIn, 0x7c);
    case S::ShipyardWorker: return StartWork(ctx, Job::ShipyardGoIn, 0x114);
    case S::Healer: return StartWork(ctx, Job::HealerGoIn, 0x120);
    case S::CharcoalBurner: return StartWork(ctx, Job::CharcoalBurnerGoIn, 0x105);
    case S::Distiller: return StartWork(ctx, Job::DistillerGoIn, 0xf0);
    case S::Brewer: return StartWork(ctx, Job::BrewerGoIn, 0xf7);
    case S::PowderMaker: return StartWork(ctx, Job::PowderMakerGoIn, 0xfe);
    case S::PigFarmer: {  // 0x4eb1f5
        const std::uint8_t farm = buildingType();
        if (farm == Value(B::PigFarm)) {
            return StartWork(ctx, Job::PigFarmGoIn, 0x77);
        }
        if (farm == Value(B::DonkeyFarm)) {
            return StartWork(ctx, Job::DonkeyFarmGoIn, 0x111);
        }
        return JobResult::Next;
    }
    case S::Winegrower: return StartWork(ctx, Job::WinegrowerGoIn, 0xe7);
    case S::RiceFarmer: return StartWork(ctx, Job::RiceFarmerGoIn, 0xde);
    case S::Beekeeper: return StartWork(ctx, Job::BeekeeperGoIn, 0x1fd);
    case S::MeadMaker: return StartWork(ctx, Job::MeadMakerGoIn, 0x206);
    case S::Alchemist: return StartWork(ctx, Job::AlchemistGoIn, 0x1f0);
    default: return Wait(ctx);  // types 20, 22, 23, 36, 37, 39..44: 0x4f91fb
    }
}

}  // namespace

JobResult RunUnitJobs(SettlerContext& ctx) {
    switch (static_cast<Job>(ctx.job)) {
    case Job::New: return NewSettler(ctx);
    case Job::UnitFighting: return AfterFight(ctx);
    case Job::UnitIdle: return UnitIdle(ctx);
    case Job::GuardBuilding: return GuardBuilding(ctx);
    case Job::GuardHeal: return Heal(ctx);
    case Job::SeekOwnArea: return SeekOwnArea(ctx);
    case Job::Nothing: return Nothing(ctx);
    case Job::UnitWalkCountdown: return WalkCountingDown(ctx);
    case Job::UnitWalk: return UnitWalk(ctx);
    case Job::ArriveAtWork: return ArriveAtWork(ctx);
    case Job::BeekeeperGoInAgain:
        return StartWork(ctx, Job::BeekeeperGoIn, 0x1fd);  // 0x4eb3d2, shared with type 45
    default: return Walk(ctx);  // the 45 walking jobs (0x4ead27)
    }
}

}  // namespace s3re::game::settler_jobs

// ---------------------------------------------------------------------------
// Tests (harness: settler_jobs_test.h)
// ---------------------------------------------------------------------------

namespace s3re::game {

namespace {

using settler_jobs::Job;
using settler_jobs::testing::CaseSetup;
using settler_jobs::testing::FamilyTest;
using S = SettlerType;

constexpr Job kUnitJobs[] = {
    Job::New, Job::UnitFighting, Job::UnitIdle, Job::UnitIdle, Job::UnitIdle, Job::GuardBuilding,
    Job::GuardHeal, Job::SeekOwnArea, Job::Nothing, Job::Nothing, Job::Nothing, Job::Nothing,
    Job::UnitWalkCountdown, Job::UnitWalk, Job::ArriveAtWork, Job::ArriveAtWork, Job::ArriveAtWork,
    Job::BeekeeperGoInAgain,
    // walking (0x4ead27)
    Job::StepAside, Job::WalkToWork, Job::DiggerWalkToSpot, Job::CarrierWalkToPile,
    Job::CarrierWalkToBuilding, Job::BuilderWalkToWait, Job::BuilderWalkToSpot, Job::WoodcutterWalkToTree,
    Job::StonecutterWalkToStone, Job::WoodcutterWalkHome, Job::StonecutterWalkHome, Job::ForesterWalkToSpot,
    Job::ForesterWalkHome, Job::CarrierWalkToSourcePile, Job::CarrierWalkToTargetPile, Job::CarrierWalkToTool,
    Job::FarmerWalkToSow, Job::FarmerWalkHome, Job::FarmerWalkHomeLoaded, Job::FarmerWalkToHarvest,
    Job::WalkToOwnArea, Job::FisherWalkToWater, Job::FisherWalkHome, Job::WaterworkerWalkToWater,
    Job::WaterworkerWalkHome, Job::RiceFarmerWalkToPlant, Job::RiceFarmerWalkHome,
    Job::RiceFarmerWalkHomeLoaded, Job::RiceFarmerWalkToHarvest, Job::WinegrowerWalkToPlant,
    Job::WinegrowerWalkHome, Job::WinegrowerWalkHomeLoaded, Job::WinegrowerWalkToHarvest,
    Job::DonkeyWalkToPile, Job::DonkeyWalkToMarket, Job::ShipyardWalkToSlipway, Job::ShipyardWalkHome,
    Job::ShipWalkToHarbour, Job::ShipSailWithCargo, Job::ShipSailHome, Job::DonkeyWalkHome,
    Job::BeekeeperWalkToHiveSpot, Job::BeekeeperWalkHome, Job::BeekeeperWalkToHive,
    Job::BeekeeperWalkHomeLoaded};

// Every type once, the ones with their own code paths more often.
constexpr S kUnitTypes[] = {
    S::Carrier, S::Digger, S::Builder, S::Woodcutter, S::Stonecutter, S::Swordsman1, S::Sawmiller, S::Forester,
    S::Bowman1, S::Smelter, S::Miner, S::Smith, S::Miller, S::Baker, S::Butcher, S::Pikeman1, S::Farmer,
    S::Fisher, S::Waterworker, S::ShipyardWorker, S::Type20, S::Healer, S::Geologist, S::Thief,
    S::CharcoalBurner, S::Distiller, S::Brewer, S::PowderMaker, S::PigFarmer, S::Winegrower, S::Bowman2,
    S::Pikeman2, S::Swordsman2, S::Bowman3, S::Pikeman3, S::Swordsman3, S::Mage, S::Type37, S::RiceFarmer,
    S::Donkey, S::Pioneer, S::Catapult, S::Type42, S::TradeShip, S::Ferry, S::Beekeeper, S::MeadMaker,
    S::Alchemist,
    S::Carrier, S::Carrier, S::Carrier, S::Carrier, S::Geologist, S::Geologist, S::Pioneer, S::Pioneer, S::Ferry, S::Ferry,
    S::TradeShip, S::Thief, S::Swordsman1, S::Bowman1, S::Pikeman1, S::Miner, S::Smith, S::Smelter,
    S::PigFarmer};

// Building types of the work places (mines, smelters, smiths, farms) and two others.
constexpr std::uint8_t kWorkBuildings[] = {7, 8, 9, 29, 37, 56, 10, 11, 12, 13, 31, 47, 48, 50, 23, 49, 6, 60};
constexpr std::int32_t kRingsMarker = 0x5a5a5a5a;

// The offset table as in the game: rings of growing distance (the searches
// walk it until they find a tile; random entries would point off the map).
// Filled once per test world (marked in the last entry).
void FillOffsetRings(CGameData* w) {
    if (w->offsets[19999].sector == kRingsMarker) {
        return;
    }
    std::uint32_t k = 0;
    for (std::int32_t r = 0; r <= 80 && k < 19999; ++r) {
        for (std::int32_t dx = -r; dx <= r && k < 19999; ++dx) {
            for (std::int32_t dy = -r; dy <= r && k < 19999; ++dy) {
                if (settler_jobs::CallStd<settler_jobs::kTileDistance>(0, 0, dx, dy) !=
                    static_cast<std::uint32_t>(r)) {
                    continue;
                }
                layout::MapOffset& o = w->offsets[k++];
                o.dx = dx;
                o.dy = dy;
                o.distance = r;
                o.sector = ((dx * 7 + dy * 3 + r) % 6 + 6) % 6;
            }
        }
    }
    for (; k < 19999; ++k) {
        w->offsets[k].distance = 1000;
    }
    w->offsets[19999].distance = 1000;
    w->offsets[19999].sector = kRingsMarker;
}

void CustomizeUnit(CaseSetup& setup, std::uint32_t n) {
    auto& rng = setup.rng();
    setup.Both([](CGameData* w) { FillOffsetRings(w); });
    std::uint8_t type = 0;
    std::uint16_t job = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t building = 0;
    std::uint8_t owner = 0;
    setup.Both([&](CGameData* w) {
        const layout::Settler& s = w->settlers[n];
        type = s.art;
        job = s.job;
        x = s.x;
        y = s.y;
        building = s.building;
        owner = s.owner;
    });
    // A work spot near the settler (geologists, pioneers), or none.
    const bool spot = rng.Chance(3, 4);
    const auto spotX = static_cast<std::uint16_t>(x + rng.Range(-6, 6));
    const auto spotY = static_cast<std::uint16_t>(y + rng.Range(-6, 6));
    const auto shipWalk = static_cast<std::uint8_t>(rng.Below(4));
    const auto slot = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : 1 + rng.Below(7));
    const auto health = static_cast<std::uint8_t>(rng.Chance(1, 4) ? rng.Next32() : rng.Below(101));
    const std::uint8_t art = kWorkBuildings[rng.Below(sizeof(kWorkBuildings))];
    const bool calm = rng.Chance(1, 2);       // own tile and its neighbours free
    const bool ownGround = rng.Chance(2, 3);  // standing on its player's ground
    const bool water = rng.Chance(1, 2);
    const auto ownRegion = static_cast<std::uint8_t>(1 + rng.Below(255));
    const auto ownArea = static_cast<std::uint16_t>(1 + rng.Below(settler_jobs::testing::kAreas - 1));
    const std::uint32_t surplus = rng.Below(3);  // marked - 1, marked or marked + 1
    const bool carrier = rng.Chance(1, 2);
    setup.Both([&](CGameData* w) {
        layout::Settler& s = w->settlers[n];
        s.saved_animation = spot ? spotX : 0xffff;
        s.saved_counter = spotY;
        s.ship_walk = shipWalk;
        s.slot = slot;
        s.health = health;
        w->buildings[building].art = art;
        layout::Tile& own = w->tiles[x][y];
        own.walk_region = ownRegion;
        own.area = ownArea;
        if (calm) {
            own.flags &= 0xf0;
            w->tiles[x + 1][y].settler = 0;
            w->tiles[x - 1][y].settler = 0;
            w->tiles[x][y + 1].settler = 0;
            w->tiles[x][y - 1].settler = 0;
            w->tiles[x - 1][y - 1].settler = 0;
            w->tiles[x + 1][y + 1].settler = 0;
        }
        if (ownGround) {
            own.owner = owner;
        }
        if (water && (type == Value(S::Ferry) || type == Value(S::TradeShip))) {
            own.terrain = kTerrainWater5;
        }
        // The surplus relative to the marked count, which is left alone (a
        // difference there must survive until the next full comparison).
        if (owner < layout::kPlayers) {
            w->players[owner].surplus_carriers = w->players[owner].surplus_carriers_marked + surplus - 1;
        }
        // Idle civilians: mostly carriers (their surplus handling).
        if (job == Value(Job::Nothing) && carrier) {
            s.art = Value(S::Carrier);
        }
    });
    // The surroundings the searches look at.
    const bool geologist = type == Value(S::Geologist);
    const bool pioneer = type == Value(S::Pioneer);
    const bool ship = type == Value(S::Ferry) || type == Value(S::TradeShip);
    const bool seek = job == Value(Job::SeekOwnArea) || job == Value(Job::Nothing);
    if (!geologist && !pioneer && !ship && !seek) {
        return;
    }
    const std::uint16_t cx = geologist || pioneer ? spotX : x;
    const std::uint16_t cy = geologist || pioneer ? spotY : y;
    for (std::int32_t dx = -7; dx <= 7; ++dx) {
        for (std::int32_t dy = -7; dy <= 7; ++dy) {
            const bool sameRegion = rng.Chance(3, 4);
            const auto terrain = static_cast<std::uint8_t>(geologist ? 0x20 + rng.Below(rng.Chance(4, 5) ? 16 : 64)
                                                           : ship    ? 5 + rng.Below(rng.Chance(4, 5) ? 3 : 10)
                                                                     : rng.Next32());
            const bool noObject = rng.Chance(9, 10);
            const bool free = rng.Chance(4, 5);
            const auto settler = static_cast<std::uint16_t>(
                rng.Chance(2, 3) ? 0 : 1 + rng.Below(settler_jobs::testing::kSettlers));
            const auto tileOwner = static_cast<std::uint8_t>(rng.Chance(1, 3) ? 0xff : rng.Below(4));
            const bool sameArea = rng.Chance(3, 4);
            const auto areaPlayer = static_cast<std::uint8_t>(rng.Chance(2, 3) ? owner : rng.Below(4));
            if (cx + dx < 0 || cy + dy < 0) {
                continue;
            }
            setup.Both([&](CGameData* w) {
                layout::Tile& t = w->tiles[cx + dx][cy + dy];
                if (sameRegion) {
                    t.walk_region = ownRegion;
                }
                t.terrain = terrain;
                if (noObject) {
                    t.object = 0;
                }
                if (free) {
                    t.flags &= 0xb0;
                }
                if (dx != 0 || dy != 0) {
                    t.settler = settler;
                }
                t.owner = tileOwner;
                if (sameArea) {
                    t.area = ownArea;
                }
                w->areas[t.area].player = areaPlayer;
            });
        }
    }
}

void CompareUnits(test::Context& ctx, UpdateSettlersFn candidate, int cases) {
    FamilyTest family{kUnitJobs, sizeof(kUnitJobs) / sizeof(kUnitJobs[0])};
    family.types = kUnitTypes;
    family.typeCount = sizeof(kUnitTypes) / sizeof(kUnitTypes[0]);
    family.customize = &CustomizeUnit;
    family.minCompared = cases / 4;
    settler_jobs::testing::CompareUpdateSettlers(ctx, candidate, family, cases);
}

// Wrong: bowmen arriving at work start like swordsmen (job 0x10 instead of 0x6a).
std::uint32_t __fastcall WrongUnitsBowmen(CGameData* world, void* edx) {
    constexpr std::uint32_t kMax = settler_jobs::testing::kMaxCaseSettlers;
    bool changed[kMax + 1] = {};
    for (std::uint32_t n = 1; n <= world->settler_high_water && n <= kMax; ++n) {
        if (world->settlers[n].art == Value(S::Bowman1) && world->settlers[n].job == Value(Job::ArriveAtWork)) {
            world->settlers[n].art = Value(S::Swordsman1);
            changed[n] = true;
        }
    }
    const std::uint32_t result = CGameData_UpdateSettlers(world, edx);
    for (std::uint32_t n = 1; n <= kMax; ++n) {
        if (changed[n]) {
            world->settlers[n].art = Value(S::Bowman1);
        }
    }
    return result;
}

}  // namespace

S3RE_TEST(CGameData_UpdateSettlers_Units_MatchesOriginal) {
    CompareUnits(ctx, &CGameData_UpdateSettlers, 4000);
}

S3RE_TEST(CGameData_UpdateSettlers_Units_ComparisonDetectsWrongBowmen) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareUnits(probe, &WrongUnitsBowmen, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
