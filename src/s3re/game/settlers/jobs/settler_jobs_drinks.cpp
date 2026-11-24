// CGameData::UpdateSettlers (0x004e9670), job handlers 0x4f335a..0x4f5f6b of
// the original (settler_jobs.h):
//
// - The geologist (job GeologistWork 0xd4 / 0xd5): marks the resource of
//   the tile it stands on with a sign object and tells the local player
//   about finds.
// - The pioneer (job PioneerWork 0x12e / 0x12f): takes the tile it stands
//   on and encloses foreign land that its new border closes off (a wall
//   walk around the blocked neighbour region, then a flood fill on
//   tile_ring2).
// - The rice farmer (0xe2..0xf2) and the winegrower (0xf3..0x104): look
//   for a field to harvest or a spot to plant around their building's work
//   point (the offset table), walk there, plant (MakeFieldTile) or
//   harvest and bring the goods home.
// - Three two-input workshops (0x105..0x122): the distiller (coal and rice
//   -> rice liquor), the powder maker (coal and sulfur -> gunpowder) and
//   the brewer (water and grain -> beer), and the charcoal burner
//   (0x11a..0x127: five boards -> coal).
//
// Settler fields as used here: +0x15 slot (0 / 1 / 2: what the worker
// carries out or in), +0x17 a step counter of the harvest animation, +0x10
// (next) the search cursor into the offset table while it looks for a
// field. Building +0x19 (kind_bytes[0]) counts the workers that started.
//
// Shared tails of other ranges, duplicated here: 0x4ece3b (StartWork),
// 0x4f8f44 and 0x4ecf12 (StartWorkB), 0x4f73bb (ReleaseTileAndRepeat),
// 0x4f8b7c (WaitForInputs).
//
// Frame slots: the handlers write the original's [esp+20h] / [esp+14h]
// (search positions) and the low byte of [esp+58h] (0x12e); they are
// mirrored into ctx.frame. No read here depends on another settler's
// leftovers: the reads leaks2 lists in this range (0x4f3c0d slot 58,
// 0x4f3f36.., 0x4f48b7.. slots 14/20) are loop artifacts or use only the
// byte written in the same handler.

#include "game/settlers/jobs/settler_jobs.h"
#include "game/settlers/jobs/settler_jobs_test.h"
#include "test/callee_stub.h"

namespace s3re::game::settler_jobs {

namespace {

using G = Good;

// Job values of this range (placeholders of enums.h with their meaning here).

// Tile objects (Tile::object) the jobs here place or look for.
constexpr std::uint8_t kNoSign = 0xe8;          // geologist: nothing found
constexpr std::uint8_t kRicePlantedObject = 0x67;
constexpr std::uint8_t kRiceRipeFirst = 0x6c;   // 0x6c..0x6e: ripe rice
constexpr std::uint8_t kRiceRipeLast = 0x6e;
constexpr std::uint8_t kRiceHarvested = 0x6f;
constexpr std::uint8_t kVinePlanted = 0x5e;
constexpr std::uint8_t kVineRipeFirst = 0x63;   // 0x63..0x65: ripe vines
constexpr std::uint8_t kVineRipeLast = 0x65;
constexpr std::uint8_t kVineHarvested = 0x66;

constexpr std::uint8_t kRiceTerrain = 0x15;     // Tile::terrain where rice grows
constexpr std::uint8_t kTileBlockedOrFootprint = 0x09;  // layout::kTileBlocked | kTileBuildingFootprint
constexpr std::uint8_t kTileNotFree = 0x4d;     // 0x12e: blocked, footprint, has building, owned
constexpr std::uint8_t kTileFlag02Clear = 0xfd;
constexpr std::uint8_t kRequestInputs = 0x01;   // Building::request_flags bit 0
constexpr std::uint16_t kWaitForInputs = 10;    // counter while inputs are missing
constexpr std::uint8_t kPileFull = 7;           // a pile with more than 7 units takes no more
constexpr std::uint8_t kFree = 0xff;
constexpr std::uint32_t kSearchTries = 50;      // offset-table entries tried per search step
constexpr std::uint32_t kRingSize = 10000;      // tile_ring2

// ------------------------------------------------------------------ helpers

std::uint32_t Anim(SettlerContext& ctx, std::uint32_t animation) {
    return Call<kSetSettlerAnimation>(ctx.world, ctx.number, animation);
}

std::uint32_t CurrentTick() {
    const std::uint32_t tick = g_gameTick;
    return tick;
}

Tile& SettlerTile(SettlerContext& ctx) { return TileAt(ctx.world, ctx.settler.x, ctx.settler.y); }
Building& WorkBuilding(SettlerContext& ctx) { return BuildingAt(ctx.world, ctx.settler.building); }
Tile& DoorTile(CGameData* world, const Building& b) { return TileAt(world, b.door_x, b.door_y); }
std::uint32_t OwnerOf(SettlerContext& ctx) { return ctx.settler.owner; }
std::int32_t TeamOf(CGameData* world, std::uint32_t player) { return PlayerAt(world, player).team; }

// The tile `dx`, `dy` from the one at `offset`, with the original's 32-bit
// offset arithmetic.
Tile& TileNear(CGameData* world, std::uint32_t offset, std::int32_t dx, std::int32_t dy) {
    return TileAtOffset(world, offset + static_cast<std::uint32_t>(dx * static_cast<std::int32_t>(kTileRowBytes) +
                                                                   dy * static_cast<std::int32_t>(kTileBytes)));
}

// 0x4ece3b: the worker starts in its building (and its field search starts over).
JobResult StartWork(SettlerContext& ctx) {
    ++WorkBuilding(ctx).kind_bytes[0];
    SettlerTile(ctx).settler = 0;
    ctx.settler.next = 0;
    ctx.settler.counter = 0;  // 0x4f91fb with DX = 0
    return JobResult::Next;
}

// 0x4f8f44 / 0x4ecf12: the same without the search cursor.
JobResult StartWorkB(SettlerContext& ctx) {
    ++WorkBuilding(ctx).kind_bytes[0];
    SettlerTile(ctx).settler = 0;
    ctx.settler.counter = 0;
    return JobResult::Next;
}

// 0x4f73bb: frees the tile and runs the new job at once.
JobResult ReleaseTileAndRepeat(SettlerContext& ctx) {
    SettlerTile(ctx).settler = 0;
    ctx.settler.counter = 0;
    return JobResult::Again;
}

// 0x4f8b7c: inputs missing, try again in 10 steps.
JobResult WaitForInputs(SettlerContext& ctx) {
    ctx.settler.counter = kWaitForInputs;
    return JobResult::Next;
}

// 0x4f4328 (also 0x4f323b): walk back to the building's door.
JobResult ReturnHome(SettlerContext& ctx) {
    ctx.settler.counter = 0;
    const Building& b = WorkBuilding(ctx);
    Call<kSetSettlerTarget>(ctx.world, ctx.number, b.door_x, b.door_y);
    return JobResult::Next;
}

// Leaves the building towards a field (0xe4, 0xea, 0xf5, 0xfb).
JobResult LeaveBuilding(SettlerContext& ctx, std::uint8_t slot, Job walk) {
    Call<kMoveSettlerToBuildingTileB>(ctx.world, ctx.number);
    ctx.settler.slot = slot;
    ctx.settler.counter = 0;
    SetJob(ctx.settler, walk);
    return JobResult::Again;
}

// Stores one unit of `good` in the output pile (0xef, 0x101, 0x109, ...).
void StoreProduct(SettlerContext& ctx, Good good) {
    Call<kPileIncAmountComing>(ctx.world, WorkBuilding(ctx).pile_ring);
    Call<kAddGoodsToPile>(ctx.world, WorkBuilding(ctx).pile_ring);
    Call<kAddStatistic>(ctx.world, OwnerOf(ctx), Value(good), 1);
    Call<kAddPlayerCounter>(ctx.world, OwnerOf(ctx), Value(good), 1);
}

// Takes one unit from input pile `index` (0x107, 0x108, 0x11d, ...).
void TakeInput(SettlerContext& ctx, std::uint32_t index, Good good) {
    Call<kTakeGoodFromPile>(ctx.world, WorkBuilding(ctx).piles[index]);
    Call<kSubtractPlayerCounter>(ctx.world, OwnerOf(ctx), Value(good), 1);
}

// ------------------------------------------------------------------ geologist

struct GeologistSign {
    std::uint8_t first, last;  // resource byte range (Tile::resources)
    std::uint8_t sign;         // object placed
    std::uint8_t found;        // the sign pair reported (sign or sign - 1); 0 = not reported
    int raceA, raceB;          // reported only for these races (-1 = any)
};

// 0x4f335a..0x4f34f5: resource -> sign. Coal 0x11..0x1f, iron 0x21..0x2f,
// gold 0x31..0x3f, then two more kinds reported only for races 1 / 3 and 2 / 3.
constexpr GeologistSign kSigns[] = {
    {0x11, 0x17, 0xe9, 0xe9, -1, -1}, {0x18, 0x1f, 0xea, 0xe9, -1, -1}, {0x21, 0x27, 0xeb, 0xeb, -1, -1},
    {0x28, 0x2f, 0xec, 0xeb, -1, -1}, {0x31, 0x37, 0xed, 0xed, -1, -1}, {0x38, 0x3f, 0xee, 0xed, -1, -1},
    {0x41, 0x47, 0xef, 0xef, 1, 3},   {0x48, 0x4f, 0xf0, 0xef, 1, 3},   {0x51, 0x57, 0xf1, 0xf1, 2, 3},
    {0x58, 0x5f, 0xf2, 0xf1, 2, 3},
};

// Notification kinds per reported sign: [0] a deposit (five or more signs
// near), [1] a first sign of its kind (none near).
std::uint32_t SignNotification(std::uint32_t found, bool deposit) {
    switch (found) {
    case 0xe9: return deposit ? 0x24 : 0x1f;
    case 0xeb: return deposit ? 0x25 : 0x20;
    case 0xed: return deposit ? 0x26 : 0x21;
    case 0xef: return deposit ? 0x27 : 0x22;
    case 0xf1: return deposit ? 0x28 : 0x23;
    default: return 0;
    }
}

// 0xd4 (0x4f335a): the geologist examines its tile.
JobResult GeologistWork(SettlerContext& ctx) {
    CGameData* const world = ctx.world;
    SetJob(ctx.settler, Job::GeologistDone);
    Anim(ctx, 0xd5);
    Tile& tile = SettlerTile(ctx);
    const std::uint32_t resources = tile.resources;
    std::uint32_t found = 0;  // [esp+28h]
    tile.object = kNoSign;
    for (const GeologistSign& s : kSigns) {
        if (resources < s.first || resources > s.last) {
            continue;
        }
        tile.object = s.sign;
        const std::int32_t race = PlayerAt(world, ctx.settler.owner).race;
        if (s.raceA < 0 || race == s.raceA || race == s.raceB) {
            found = s.found;
        }
        break;
    }
    if (resources == 0) {
        return JobResult::Next;
    }
    // Count the signs of the same kind within distance 4 (offset table from entry 1).
    std::uint32_t count = 0;  // [esp+18h]
    bool counted = false;
    if (world->offsets[1].distance <= 4) {
        counted = true;
        for (std::uint32_t k = 1;; ++k) {
            const std::uint32_t x = ctx.settler.x + static_cast<std::uint32_t>(world->offsets[k].dx);
            const std::uint32_t y = ctx.settler.y + static_cast<std::uint32_t>(world->offsets[k].dy);
            ctx.frame.slot20 = x;
            ctx.frame.slot14 = y;
            if (Call<kIsOnMap>(world, x, y) != 0) {
                const std::uint32_t object = TileAt(world, x, y).object;
                if (object == found || object == found + 1) {
                    ++count;
                }
            }
            if (world->offsets[k + 1].distance > 4) {
                break;
            }
        }
    }
    if (counted && count >= 5) {
        const std::uint32_t kind = SignNotification(found, true);
        if (kind != 0) {
            Call<kNotifyLocalPlayer>(world, ctx.settler.owner, kind, ctx.settler.x, ctx.settler.y);
        }
        return JobResult::Next;
    }
    if (counted && count != 0) {
        return JobResult::Next;
    }
    const std::uint32_t kind = SignNotification(found, false);
    if (kind != 0) {
        Call<kNotifyLocalPlayer>(world, ctx.settler.owner, kind, ctx.settler.x, ctx.settler.y);
    }
    const std::uint32_t x = ctx.settler.x;
    const std::uint32_t y = ctx.settler.y;
    const std::uint32_t screenY = Call<kTileScreenY>(world, ctx.settler.x, ctx.settler.y);
    const std::uint32_t screenX = Call<kTileScreenX>(world, ctx.settler.x, ctx.settler.y);
    Call<kSpawn545120>(&world->map_effects, CurrentTick(), screenX, screenY, x, y);
    return JobResult::Next;
}

// 0xd5 (0x4f376b) and 0x12f (0x4f3cf3): back to idle.
JobResult BackToIdle(SettlerContext& ctx) {
    SetJob(ctx.settler, Job::UnitIdle);
    ctx.settler.counter = 0;
    ctx.settler.direction = 0;
    ctx.settler.frame = 0;
    SettlerTile(ctx).flags &= kTileFlag02Clear;
    return JobResult::Next;
}

// ------------------------------------------------------------------ pioneer

// A tile the pioneer's land grab can take: not of its team, blocked or a
// footprint, and a terrain class with a high nibble.
bool SameTeam(SettlerContext& ctx, std::uint8_t owner) {
    return owner != kFree && TeamOf(ctx.world, owner) == TeamOf(ctx.world, ctx.settler.owner);
}

// Takes tile (x, y) for the pioneer's player (0x4f3b02.., 0x4f3c25..).
void TakeTile(SettlerContext& ctx, std::uint32_t x, std::uint32_t y, std::uint8_t owner, std::uint8_t flags) {
    if (owner == ctx.settler.owner) {
        return;
    }
    if (owner != kFree) {
        if ((flags & layout::kTileBuildingFootprint) != 0) {
            Call<kDestroyBuildingAt>(ctx.world, x, y);
        }
        Call<kClearTileOwner>(ctx.world, x, y);
    }
    Call<kSetOwnerOfFreeTile>(ctx.world, x, y, ctx.settler.owner);
}

// 0x4f3ad4..0x4f3cd6: flood fill from (x, y) over the takeable tiles.
void FillEnclosure(SettlerContext& ctx, std::uint32_t x, std::uint32_t y, Tile& start) {
    CGameData* const world = ctx.world;
    world->tile_ring2[0].x = static_cast<std::uint16_t>(x);
    world->tile_ring2[0].y = static_cast<std::uint16_t>(y);
    world->tile_ring2_index.read = 0;
    world->tile_ring2_index.write = 1;
    TakeTile(ctx, x, y, start.owner, start.flags);
    while (world->tile_ring2_index.read != world->tile_ring2_index.write) {
        const std::uint32_t read = world->tile_ring2_index.read;
        const std::uint32_t cx = world->tile_ring2[read].x;
        const std::uint32_t cy = world->tile_ring2[read].y;
        Call<kUpdateBorderFlagsAround>(world, cx, cy);
        for (std::uint32_t d = 0; d <= 5; ++d) {
            const std::uint32_t ex = CallStd<kNeighbourX>(cx, d);
            const std::uint32_t ey = CallStd<kNeighbourY>(cy, d);
            if (ex >= world->map_size || ey >= world->map_size) {
                continue;
            }
            Tile& t = TileAt(world, ex, ey);
            const std::uint8_t owner = t.owner;
            ctx.frame.slot58 = (ctx.frame.slot58 & 0xffffff00u) | owner;  // byte [esp+58h]
            if (SameTeam(ctx, owner)) {
                continue;
            }
            const std::uint8_t flags = t.flags;
            if ((flags & kTileBlockedOrFootprint) == 0 || (t.terrain & 0xf0) == 0) {
                continue;
            }
            TakeTile(ctx, ex, ey, owner, flags);
            const std::uint32_t write = world->tile_ring2_index.write;
            world->tile_ring2[write].x = static_cast<std::uint16_t>(ex);
            world->tile_ring2[world->tile_ring2_index.write].y = static_cast<std::uint16_t>(ey);
            if (++world->tile_ring2_index.write == kRingSize) {
                world->tile_ring2_index.write = 0;
            }
        }
        if (++world->tile_ring2_index.read == kRingSize) {
            world->tile_ring2_index.read = 0;
        }
    }
}

// 0x4f398a..0x4f3ace: walks along the edge of the takeable region that
// contains (nx, ny), starting towards the pioneer. True when the walk came
// back to the start three times or turned six times in a row (the region
// is enclosed); false when it met an edge it cannot pass.
bool RegionIsEnclosed(SettlerContext& ctx, std::uint32_t nx, std::uint32_t ny, std::uint32_t d) {
    CGameData* const world = ctx.world;
    std::uint32_t cx = nx;
    std::uint32_t cy = ny;
    std::uint32_t dir = (d + 3) % 6;
    std::uint32_t closed = 0;
    std::uint32_t turns = 0;
    while (closed != 3) {
        const std::uint32_t px = CallStd<kNeighbourX>(cx, dir);
        const std::uint32_t py = CallStd<kNeighbourY>(cy, dir);
        const Tile& t = TileAt(world, px, py);
        const bool sameTeam = SameTeam(ctx, t.owner);
        if (!sameTeam && (t.flags & kTileBlockedOrFootprint) != 0 && (t.terrain & 0xf0) != 0) {
            cx = px;
            cy = py;
            dir = CallStd<kNextDirection>(dir);
            turns = 0;
            if (px == ctx.frame.slot20 && py == ctx.frame.slot14) {
                ++closed;
            }
            continue;
        }
        if (!sameTeam && (t.terrain & 0xf0) != 0) {
            return false;  // 0x4f3ac9 (closed is never 3 here)
        }
        dir = CallStd<kPreviousDirection>(dir);
        if (++turns == 6) {
            closed = 3;
        }
    }
    return true;
}

// 0x12e (0x4f37bd): the pioneer takes its tile and encloses cut-off land.
JobResult PioneerWork(SettlerContext& ctx) {
    CGameData* const world = ctx.world;
    SetJob(ctx.settler, Job::PioneerDone);
    Anim(ctx, 0x110);
    const std::uint32_t x = ctx.settler.x;
    const std::uint32_t y = ctx.settler.y;
    Tile& here = TileAt(world, x, y);
    if ((here.flags & kTileNotFree) != 0) {
        return JobResult::Next;
    }
    const std::uint8_t owner = here.owner;
    if (owner != kFree) {
        if (owner == ctx.settler.owner) {
            return JobResult::Next;
        }
        const std::uint32_t kind = TeamOf(world, ctx.settler.owner) == TeamOf(world, owner) ? 6 : 5;
        Call<kNotifyLocalPlayer>(world, owner, kind, x, y);
        Call<kClearTileOwner>(world, ctx.settler.x, ctx.settler.y);
    }
    Call<kSetOwnerOfFreeTile>(world, ctx.settler.x, ctx.settler.y, ctx.settler.owner);
    Call<kUpdateBorderFlagsAround>(world, ctx.settler.x, ctx.settler.y);
    for (std::uint32_t d = 0; d <= 5; ++d) {
        const std::uint32_t nx = CallStd<kNeighbourX>(ctx.settler.x, d);
        ctx.frame.slot20 = nx;
        const std::uint32_t ny = CallStd<kNeighbourY>(ctx.settler.y, d);
        ctx.frame.slot14 = ny;
        Tile& next = TileAt(world, nx, ny);
        if (SameTeam(ctx, next.owner)) {
            continue;
        }
        if ((next.flags & kTileBlockedOrFootprint) == 0 || (next.terrain & 0xf0) == 0) {
            continue;
        }
        if (RegionIsEnclosed(ctx, nx, ny, d)) {
            FillEnclosure(ctx, ctx.frame.slot20, ctx.frame.slot14, next);
        }
    }
    return JobResult::Next;
}

// ------------------------------------------------------------------ fields

// The search of the rice farmer and the winegrower (0x4f3d53, 0x4f4471):
// up to 50 offset-table entries from the settler's cursor (+0x10) around
// the building's work point. 2 = a ripe field, 1 = a spot to plant.
struct FieldSearch {
    std::uint32_t margin;     // positions closer than this to the map edge are skipped
    std::uint8_t ripeFirst, ripeLast;
    std::uint16_t cursorEnd;  // the cursor wraps to 0 here
    std::uint16_t counter;    // counter when something was found
    Job harvest, plant;       // next jobs
    std::uint32_t harvestAnimation, plantAnimation;
    bool (*canPlant)(SettlerContext& ctx, std::uint32_t x, std::uint32_t y, std::uint32_t offset);
};

// A free, unblocked tile of the building's area.
bool IsFreeFieldTile(CGameData* world, const Building& b, std::uint32_t offset) {
    const Tile& t = TileAtOffset(world, offset);
    return t.area == DoorTile(world, b).area && t.object == 0 && t.object_number == 0 &&
           (t.flags & kTileBlockedOrFootprint) == 0;
}

bool CanPlantRice(SettlerContext& ctx, std::uint32_t, std::uint32_t, std::uint32_t offset) {
    return IsFreeFieldTile(ctx.world, WorkBuilding(ctx), offset) &&
           TileAtOffset(ctx.world, offset).terrain == kRiceTerrain;
}

bool IsVineTerrain(std::uint8_t terrain) { return terrain == 0x10 || terrain == 0x18 || terrain == 0x16; }

// 0x4f466b..0x4f487b / 0x4f4aee..0x4f4d0e: one of three rows of vine
// plots around the tile is free of objects and buildings / piles.
bool VineRowFree(CGameData* world, std::uint32_t offset) {
    auto empty = [&](std::int32_t dx, std::int32_t dy) {
        const Tile& t = TileNear(world, offset, dx, dy);
        return t.object == 0 && t.object_number == 0;
    };
    const bool rowA = empty(-1, 0) && empty(1, 0) && empty(-1, -1) && empty(1, 1) && empty(-1, 1) &&
                      empty(-1, -2) && empty(1, -1) && empty(1, 2);
    const bool rowB = empty(-1, 0) && empty(1, 0) && empty(0, -1) && empty(0, 1) && empty(-2, -1) &&
                      empty(-1, -2) && empty(1, 2) && empty(2, 1);
    const bool rowC = empty(0, -1) && empty(0, 1) && empty(-1, -1) && empty(1, 1) && empty(-2, -1) &&
                      empty(1, -1) && empty(-1, 1) && empty(2, 1);
    return rowA || rowB || rowC;
}

// The checks before a vine is planted (0x4f460f.., 0x4f4a92..); TileSlope is
// only called for a free tile of vine terrain.
bool CanPlantVineAt(SettlerContext& ctx, std::uint32_t x, std::uint32_t y, std::uint32_t offset) {
    if (!IsFreeFieldTile(ctx.world, WorkBuilding(ctx), offset) ||
        !IsVineTerrain(TileAtOffset(ctx.world, offset).terrain)) {
        return false;
    }
    if (Call<kTileSlope>(ctx.world, x, y) < 7) {
        return false;
    }
    return VineRowFree(ctx.world, offset);
}

JobResult SearchField(SettlerContext& ctx, const FieldSearch& s) {
    CGameData* const world = ctx.world;
    Building& b = WorkBuilding(ctx);
    if ((b.request_flags & kRequestInputs) == 0) {
        ctx.settler.counter = kWaitForInputs;
        return JobResult::Next;
    }
    std::uint32_t found = 0;  // [esp+1Ch]
    std::uint32_t tries = 0;  // [esp+18h]
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    while (found == 0 && tries < kSearchTries) {
        const layout::MapOffset& o = world->offsets[ctx.settler.next + tries];
        x = b.work_x + static_cast<std::uint32_t>(o.dx);
        y = b.work_y + static_cast<std::uint32_t>(o.dy);
        ctx.frame.slot14 = y;
        const std::uint32_t limit = world->map_size - s.margin;
        if (x < s.margin || x >= limit || y < s.margin || y >= limit) {
            ++tries;
            continue;
        }
        const std::uint32_t offset = TileOffset(x, y);
        if (PileAt(world, b.pile_ring).amount <= kPileFull) {
            const std::uint8_t object = TileAtOffset(world, offset).object;
            if (object >= s.ripeFirst && object <= s.ripeLast &&
                TileAtOffset(world, offset).area == DoorTile(world, b).area) {
                found = 2;
                break;
            }
        }
        if (s.canPlant(ctx, x, y, offset)) {
            found = 1;
        } else {
            ++tries;
        }
    }
    ctx.frame.slot20 = x;
    ctx.settler.next = static_cast<std::uint16_t>(ctx.settler.next + tries);
    if (tries == 0) {
        ctx.settler.next = static_cast<std::uint16_t>(ctx.settler.next + 1);
    }
    if (ctx.settler.next >= s.cursorEnd) {
        ctx.settler.next = 0;
    }
    if (found == 0) {
        return WaitForInputs(ctx);
    }
    ctx.settler.counter = s.counter;
    ctx.settler.next = 0;
    if (Call<kCanSettlerEnterTile>(world, x, y, ctx.settler.owner, ctx.settler.art) == 0) {
        return JobResult::Next;
    }
    SettlerTile(ctx).settler = static_cast<std::uint16_t>(ctx.number);
    Call<kSetSettlerTarget>(world, ctx.number, x, y);
    SetJob(ctx.settler, found == 2 ? s.harvest : s.plant);
    Anim(ctx, found == 2 ? s.harvestAnimation : s.plantAnimation);
    return JobResult::Next;
}

constexpr FieldSearch kRiceSearchParams{
    1,    kRiceRipeFirst, kRiceRipeLast, 400, 1, Job::RiceFarmerLeaveToHarvest, Job::RiceFarmerLeaveToPlant,
    0xe4, 0xdf,           &CanPlantRice};
constexpr FieldSearch kVineSearchParams{
    2,    kVineRipeFirst, kVineRipeLast,  150, 0, Job::WinegrowerLeaveToHarvest, Job::WinegrowerLeaveToPlant,
    0xed, 0xe8,           &CanPlantVineAt};

// 0xe6 (0x4f4097): plants rice on the settler's tile (a patch of seven tiles).
JobResult PlantRice(SettlerContext& ctx) {
    CGameData* const world = ctx.world;
    const std::uint32_t x = ctx.settler.x;
    const std::uint32_t y = ctx.settler.y;
    ctx.frame.slot14 = y;
    ctx.frame.slot20 = x;
    const std::uint32_t offset = TileOffset(x, y);
    if (!IsFreeFieldTile(world, WorkBuilding(ctx), offset) ||
        TileAtOffset(world, offset).terrain != kRiceTerrain) {
        SetJob(ctx.settler, Job::RiceFarmerWalkHome);
        return ReturnHome(ctx);
    }
    Call<kMakeFieldTile>(world, x, y);
    // The six neighbours, each with the settler's position read again.
    Call<kMakeFieldTile>(world, ctx.settler.x + 1u, ctx.settler.y);
    Call<kMakeFieldTile>(world, ctx.settler.x - 1u, ctx.settler.y);
    Call<kMakeFieldTile>(world, ctx.settler.x, ctx.settler.y + 1u);
    Call<kMakeFieldTile>(world, ctx.settler.x, ctx.settler.y - 1u);
    Call<kMakeFieldTile>(world, ctx.settler.x + 1u, ctx.settler.y + 1u);
    Call<kMakeFieldTile>(world, ctx.settler.x - 1u, ctx.settler.y - 1u);
    SetJob(ctx.settler, Job::RiceFarmerPlanted);
    Anim(ctx, 0xe0);
    return JobResult::Next;
}

// 0xf7 (0x4f4a19): plants a vine on the settler's tile.
JobResult PlantVine(SettlerContext& ctx) {
    CGameData* const world = ctx.world;
    const std::uint32_t x = ctx.settler.x;
    const std::uint32_t y = ctx.settler.y;
    ctx.frame.slot20 = x;
    ctx.frame.slot14 = y;
    if (!CanPlantVineAt(ctx, x, y, TileOffset(x, y))) {
        SetJob(ctx.settler, Job::WinegrowerWalkHome);
        return ReturnHome(ctx);  // 0x4f4d46 -> 0x4f323b
    }
    Call<kMakeFieldTile>(world, ctx.settler.x, ctx.settler.y);
    SetJob(ctx.settler, Job::WinegrowerPlanted);
    Anim(ctx, 0xe9);
    return JobResult::Next;
}

// 0xeb (0x4f428d) / 0xfc (0x4f4dbb): at a ripe field: start harvesting.
JobResult StartHarvest(SettlerContext& ctx, std::uint8_t first, std::uint8_t last, Job harvest,
                       std::uint32_t animation, Job walkHome) {
    const std::uint8_t object = SettlerTile(ctx).object;
    if (object < first || object > last) {
        SetJob(ctx.settler, walkHome);
        return ReturnHome(ctx);
    }
    SetJob(ctx.settler, harvest);
    Anim(ctx, animation);
    ctx.settler.second_unit = 0;  // +0x17: harvest step
    return JobResult::Next;
}

// 0xec (0x4f42db): two steps of rice harvest.
JobResult HarvestRice(SettlerContext& ctx) {
    const std::uint8_t step = ++ctx.settler.second_unit;
    if (step == 1) {
        Anim(ctx, 0xe3);
        ctx.settler.slot = 2;
        return JobResult::Next;
    }
    if (step == 2) {
        SettlerTile(ctx).object = kRiceHarvested;
        SetJob(ctx.settler, Job::RiceFarmerWalkHomeLoaded);
        return ReturnHome(ctx);
    }
    return JobResult::Next;
}

// 0xfd (0x4f4e09): one step of vine harvest.
JobResult HarvestVine(SettlerContext& ctx) {
    const std::uint8_t step = ++ctx.settler.second_unit;
    if (step != 1) {
        return JobResult::Next;
    }
    ctx.settler.slot = 2;
    SettlerTile(ctx).object = kVineHarvested;
    SetJob(ctx.settler, Job::WinegrowerWalkHomeLoaded);
    return ReturnHome(ctx);
}

// 0xee (0x4f4383) / 0xf0 (0x4f43a8): back at the rice farm.
JobResult RiceEnter(SettlerContext& ctx) {
    Call<kMoveSettlerToBuildingTileA>(ctx.world, ctx.number);
    SetJob(ctx.settler, Job::RiceFarmerRest);
    Anim(ctx, 0xde);
    return JobResult::Next;
}

JobResult RiceArrive(SettlerContext& ctx) {
    if (ctx.settler.slot != 0) {
        SetJob(ctx.settler, Job::RiceFarmerStore);
        Anim(ctx, 0xe5);
        return JobResult::Next;
    }
    return RiceEnter(ctx);
}

// 0x104 (0x4f4f4b): back in the winery.
JobResult WineEnter(SettlerContext& ctx) {
    Call<kMoveSettlerToBuildingTileA>(ctx.world, ctx.number);
    SetJob(ctx.settler, Job::WinegrowerRest);
    Anim(ctx, 0xe7);
    return JobResult::Next;
}

// 0xff (0x4f4e5f): at the winery's door.
JobResult WineArrive(SettlerContext& ctx) {
    if (ctx.settler.slot == 0) {
        return WineEnter(ctx);
    }
    Call<kMoveSettlerToBuildingTileA>(ctx.world, ctx.number);
    Anim(ctx, 0xe7);
    SetJob(ctx.settler, Job::WinegrowerEnterLoaded);
    return JobResult::Next;
}

// ------------------------------------------------------------------ workshops

// A workshop that turns two inputs into one product: the check (inputs
// there, output pile not full), take input A, take input B, produce, and
// either go on at once or wait. The take steps are separate cases below.
struct TwoInputWorkshop {
    Job first;          // the check's next job (take input A)
    std::uint32_t firstAnimation;
    Good product;
    Job again;          // inputs there after producing: take input A at once
    std::uint32_t againAnimation;
    Job wait;           // else: leave the work tile, check again
    std::uint32_t waitAnimation;
};

// 0x105.. (0x4f4f7c..0x4f52b8): coal and rice -> rice liquor.
constexpr TwoInputWorkshop kDistiller{
    Job::DistillerTakeCoal, 0xf2, G::RiceLiquor, Job::DistillerTakeCoalAgain, 0xf6,
    Job::DistillerEndRound, 0xf5};
// 0x10c.. (0x4f561a..0x4f5956): water and grain -> beer.
constexpr TwoInputWorkshop kBrewer{Job::BrewerTakeWater, 0xf9, G::Beer, Job::BrewerTakeWaterAgain, 0xfd,
                                   Job::BrewerEndRound,  0xfc};
// 0x113.. (0x4f52cb..0x4f5607): coal and sulfur -> gunpowder.
constexpr TwoInputWorkshop kPowderMaker{
    Job::PowderMakerTakeCoal, 0x100, G::Gunpowder, Job::PowderMakerTakeCoalAgain, 0x104,
    Job::PowderMakerEndRound, 0x103};

bool InputsReady(SettlerContext& ctx) {
    if (static_cast<std::uint8_t>(Call<kBuildingPileHasGoods>(ctx.world, ctx.settler.building, 1)) == 0) {
        return false;
    }
    if (static_cast<std::uint8_t>(Call<kBuildingPileHasGoods>(ctx.world, ctx.settler.building, 2)) == 0) {
        return false;
    }
    return PileAt(ctx.world, WorkBuilding(ctx).pile_ring).amount <= kPileFull;
}

void ReserveTwoInputs(SettlerContext& ctx) {
    Call<kPileIncAmountLeaving>(ctx.world, WorkBuilding(ctx).piles[0]);
    Call<kPileIncAmountLeaving>(ctx.world, WorkBuilding(ctx).piles[1]);
}

// 0x120 / 0x121 / 0x122: waits until both inputs are there.
JobResult WorkshopCheck(SettlerContext& ctx, const TwoInputWorkshop& w) {
    if ((WorkBuilding(ctx).request_flags & kRequestInputs) == 0 || !InputsReady(ctx)) {
        return WaitForInputs(ctx);
    }
    ReserveTwoInputs(ctx);
    SettlerTile(ctx).settler = static_cast<std::uint16_t>(ctx.number);
    SetJob(ctx.settler, w.first);
    Anim(ctx, w.firstAnimation);
    return JobResult::Next;
}

JobResult WorkshopTake(SettlerContext& ctx, std::uint32_t pile, Good good, Job next, std::uint32_t animation) {
    TakeInput(ctx, pile, good);
    SetJob(ctx.settler, next);
    Anim(ctx, animation);
    return JobResult::Next;
}

// 0x109 / 0x110 / 0x117: stores the product; goes on at once if the next
// inputs are there.
JobResult WorkshopProduce(SettlerContext& ctx, const TwoInputWorkshop& w) {
    StoreProduct(ctx, w.product);
    if (InputsReady(ctx) && (WorkBuilding(ctx).request_flags & kRequestInputs) != 0) {
        ReserveTwoInputs(ctx);
        SetJob(ctx.settler, w.again);
        Anim(ctx, w.againAnimation);
    } else {
        SetJob(ctx.settler, w.wait);
        Anim(ctx, w.waitAnimation);
    }
    return JobResult::Next;
}

// 0x10a / 0x111 / 0x118 / 0x11e: leaves its work tile and checks again.
JobResult WorkshopWait(SettlerContext& ctx, Job check) {
    SettlerTile(ctx).settler = 0;
    SetJob(ctx.settler, check);
    ctx.settler.counter = 0;
    return JobResult::Next;
}

// The charcoal burner needs five boards (input pile 0) per coal.
constexpr std::uint8_t kBoardsPerCoal = 5;

bool CharcoalInputsReady(CGameData* world, const Building& b) {
    return PileAt(world, b.piles[0]).amount >= kBoardsPerCoal && PileAt(world, b.pile_ring).amount <= kPileFull;
}

void ReserveFiveBoards(SettlerContext& ctx, std::uint32_t firstPile) {
    Call<kPileIncAmountLeaving>(ctx.world, firstPile);
    for (int k = 1; k < kBoardsPerCoal; ++k) {
        Call<kPileIncAmountLeaving>(ctx.world, WorkBuilding(ctx).piles[0]);
    }
}

// 0x123 (0x4f5969).
JobResult CharcoalCheck(SettlerContext& ctx) {
    const Building& b = WorkBuilding(ctx);
    if ((b.request_flags & kRequestInputs) == 0) {
        ctx.settler.counter = kWaitForInputs;
        return JobResult::Next;
    }
    if (!CharcoalInputsReady(ctx.world, b)) {
        return WaitForInputs(ctx);
    }
    ReserveFiveBoards(ctx, b.piles[0]);
    SettlerTile(ctx).settler = static_cast<std::uint16_t>(ctx.number);
    SetJob(ctx.settler, Job::CharcoalBurnerTakeBoard1);
    Anim(ctx, 0x107);
    return JobResult::Next;
}

// 0x11c (0x4f5abe) / 0x11f (0x4f5eb4): the first board goes in; smoke
// rises from the building (MapEffects).
JobResult CharcoalFirstBoard(SettlerContext& ctx) {
    CGameData* const world = ctx.world;
    const std::uint32_t number = ctx.settler.building;
    const Building& b = BuildingAt(world, number);
    const std::uint32_t bx = b.x;
    const std::uint32_t by = b.y;
    const std::uint32_t pile = b.piles[0];
    Call<kTakeGoodFromPile>(world, pile);
    Call<kSubtractPlayerCounter>(world, OwnerOf(ctx), Value(G::Board), 1);
    SetJob(ctx.settler, Job::CharcoalBurnerTakeBoard2);
    Anim(ctx, 0x108);
    const std::uint32_t screenY = Call<kTileScreenY>(world, bx, by);
    const std::uint32_t screenX = Call<kTileScreenX>(world, bx, by);
    Call<kSpawn5449f0>(&world->map_effects, CurrentTick(), BuildingAt(world, number).race, 3, number, screenX,
                       screenY, bx, by);
    return JobResult::Next;
}

// 0x127 (0x4f5ccb): the coal is done.
JobResult CharcoalProduce(SettlerContext& ctx) {
    StoreProduct(ctx, G::Coal);
    const Building& b = WorkBuilding(ctx);
    if (CharcoalInputsReady(ctx.world, b) && (b.request_flags & kRequestInputs) != 0) {
        ReserveFiveBoards(ctx, b.piles[0]);
        SetJob(ctx.settler, Job::CharcoalBurnerTakeBoard1Again);
        Anim(ctx, 0x10a);
    } else {
        SetJob(ctx.settler, Job::CharcoalBurnerEndRound);
        Anim(ctx, 0x109);
    }
    return JobResult::Next;
}

}  // namespace

// Purpose: The jobs of the geologist, the pioneer, the rice farmer, the
// winegrower and the drink, gunpowder and charcoal workshops.
JobResult RunDrinkJobs(SettlerContext& ctx) {
    switch (static_cast<Job>(ctx.job)) {
    case Job::GeologistWork: return GeologistWork(ctx);
    case Job::GeologistDone:
    case Job::PioneerDone: return BackToIdle(ctx);
    case Job::PioneerWork: return PioneerWork(ctx);

    // Rice farmer.
    case Job::RiceFarmerGoIn: SetJob(ctx.settler, Job::RiceFarmerSearch); return StartWork(ctx);
    case Job::RiceFarmerSearch: return SearchField(ctx, kRiceSearchParams);
    case Job::RiceFarmerLeaveToPlant: return LeaveBuilding(ctx, 1, Job::RiceFarmerWalkToPlant);
    case Job::RiceFarmerPlant: return PlantRice(ctx);
    case Job::RiceFarmerPlanted:
        SettlerTile(ctx).object = kRicePlantedObject;
        SetJob(ctx.settler, Job::RiceFarmerWalkHome);
        return ReturnHome(ctx);
    case Job::RiceFarmerBackIn:  // 0xe9
    case Job::RiceFarmerRest: SetJob(ctx.settler, Job::RiceFarmerSearch); return ReleaseTileAndRepeat(ctx);
    case Job::RiceFarmerLeaveToHarvest: return LeaveBuilding(ctx, 0, Job::RiceFarmerWalkToHarvest);
    case Job::RiceFarmerAtField:
        return StartHarvest(ctx, kRiceRipeFirst, kRiceRipeLast, Job::RiceFarmerHarvest, 0xe2,
                            Job::RiceFarmerWalkHome);
    case Job::RiceFarmerHarvest: return HarvestRice(ctx);
    case Job::RiceFarmerHome: return RiceArrive(ctx);  // 0xee
    case Job::RiceFarmerGoInAgain: return RiceEnter(ctx);
    case Job::RiceFarmerStore:
        StoreProduct(ctx, G::Rice);
        SetJob(ctx.settler, Job::RiceFarmerGoInAgain);
        Anim(ctx, 0xe6);
        ctx.settler.slot = 0;
        return JobResult::Next;

    // Winegrower.
    case Job::WinegrowerGoIn: SetJob(ctx.settler, Job::WinegrowerSearch); return StartWork(ctx);  // 0xf3
    case Job::WinegrowerSearch: return SearchField(ctx, kVineSearchParams);
    case Job::WinegrowerLeaveToPlant: return LeaveBuilding(ctx, 1, Job::WinegrowerWalkToPlant);
    case Job::WinegrowerPlant: return PlantVine(ctx);  // 0xf7
    case Job::WinegrowerPlanted:
        SettlerTile(ctx).object = kVinePlanted;
        SetJob(ctx.settler, Job::WinegrowerWalkHome);
        return ReturnHome(ctx);
    case Job::WinegrowerBackIn:  // 0xfa
    case Job::WinegrowerRest: SetJob(ctx.settler, Job::WinegrowerSearch); return ReleaseTileAndRepeat(ctx);
    case Job::WinegrowerLeaveToHarvest: return LeaveBuilding(ctx, 0, Job::WinegrowerWalkToHarvest);
    case Job::WinegrowerAtField:
        return StartHarvest(ctx, kVineRipeFirst, kVineRipeLast, Job::WinegrowerHarvest, 0xeb,
                            Job::WinegrowerWalkHome);
    case Job::WinegrowerHarvest: return HarvestVine(ctx);
    case Job::WinegrowerHome: return WineArrive(ctx);  // 0xff
    case Job::WinegrowerEnterLoaded:
        Call<kMoveSettlerToBuildingTileB>(ctx.world, ctx.number);
        SetJob(ctx.settler, Job::WinegrowerStore);
        Anim(ctx, 0xee);
        return JobResult::Next;
    case Job::WinegrowerStore:
        StoreProduct(ctx, G::Wine);
        SetJob(ctx.settler, Job::WinegrowerGoInAgain);
        Anim(ctx, 0xef);
        ctx.settler.slot = 0;
        return JobResult::Next;
    case Job::WinegrowerGoInAgain: return WineEnter(ctx);

    // Distiller, brewer, powder maker.
    case Job::DistillerGoIn: SetJob(ctx.settler, Job::DistillerCheck); return StartWorkB(ctx);      // 0x105
    case Job::BrewerGoIn: SetJob(ctx.settler, Job::BrewerCheck); return StartWorkB(ctx);            // 0x10c
    case Job::PowderMakerGoIn: SetJob(ctx.settler, Job::PowderMakerCheck); return StartWorkB(ctx);  // 0x113
    case Job::DistillerCheck: return WorkshopCheck(ctx, kDistiller);
    case Job::BrewerCheck: return WorkshopCheck(ctx, kBrewer);
    case Job::PowderMakerCheck: return WorkshopCheck(ctx, kPowderMaker);
    case Job::DistillerTakeCoal:  // 0x107
    case Job::DistillerTakeCoalAgain: return WorkshopTake(ctx, 1, G::Coal, Job::DistillerTakeRice, 0xf3);
    case Job::DistillerTakeRice: return WorkshopTake(ctx, 0, G::Rice, Job::DistillerProduce, 0xf4);
    case Job::DistillerProduce: return WorkshopProduce(ctx, kDistiller);
    case Job::DistillerEndRound: return WorkshopWait(ctx, Job::DistillerCheck);
    case Job::BrewerTakeWater:  // 0x10e
    case Job::BrewerTakeWaterAgain: return WorkshopTake(ctx, 1, G::Water, Job::BrewerTakeGrain, 0xfa);
    case Job::BrewerTakeGrain: return WorkshopTake(ctx, 0, G::Grain, Job::BrewerProduce, 0xfb);
    case Job::BrewerProduce: return WorkshopProduce(ctx, kBrewer);
    case Job::BrewerEndRound: return WorkshopWait(ctx, Job::BrewerCheck);
    case Job::PowderMakerTakeCoal:  // 0x115
    case Job::PowderMakerTakeCoalAgain:
        return WorkshopTake(ctx, 0, G::Coal, Job::PowderMakerTakeSulfur, 0x101);
    case Job::PowderMakerTakeSulfur: return WorkshopTake(ctx, 1, G::Sulfur, Job::PowderMakerProduce, 0x102);
    case Job::PowderMakerProduce: return WorkshopProduce(ctx, kPowderMaker);
    case Job::PowderMakerEndRound: return WorkshopWait(ctx, Job::PowderMakerCheck);

    // Charcoal burner.
    case Job::CharcoalBurnerGoIn:
        SetJob(ctx.settler, Job::CharcoalBurnerCheck);
        return StartWorkB(ctx);  // 0x11a
    case Job::CharcoalBurnerCheck: return CharcoalCheck(ctx);
    case Job::CharcoalBurnerTakeBoard1:  // 0x11c
    case Job::CharcoalBurnerTakeBoard1Again: return CharcoalFirstBoard(ctx);
    case Job::CharcoalBurnerTakeBoard2:
        return WorkshopTake(ctx, 0, G::Board, Job::CharcoalBurnerTakeBoard3, 0x10b);
    case Job::CharcoalBurnerTakeBoard3:
        return WorkshopTake(ctx, 0, G::Board, Job::CharcoalBurnerTakeBoard4, 0x10c);
    case Job::CharcoalBurnerTakeBoard4:
        return WorkshopTake(ctx, 0, G::Board, Job::CharcoalBurnerTakeBoard5, 0x10d);
    case Job::CharcoalBurnerTakeBoard5:
        return WorkshopTake(ctx, 0, G::Board, Job::CharcoalBurnerProduce, 0x10e);
    case Job::CharcoalBurnerProduce: return CharcoalProduce(ctx);
    case Job::CharcoalBurnerEndRound: return WorkshopWait(ctx, Job::CharcoalBurnerCheck);
    default: return JobResult::Next;  // not a job of this family (kJobFamily)
    }
}

// ---------------------------------------------------------------------------
// Tests (harness: settler_jobs_test.h)
// ---------------------------------------------------------------------------

namespace {

using testing::CaseSetup;
using testing::FamilyTest;

constexpr Job kDrinkJobs[] = {
    Job::GeologistWork, Job::GeologistDone, Job::RiceFarmerGoIn, Job::RiceFarmerSearch,
    Job::RiceFarmerLeaveToPlant, Job::RiceFarmerPlant, Job::RiceFarmerPlanted, Job::RiceFarmerBackIn,
    Job::RiceFarmerLeaveToHarvest, Job::RiceFarmerAtField, Job::RiceFarmerHarvest, Job::RiceFarmerHome,
    Job::RiceFarmerStore, Job::RiceFarmerGoInAgain, Job::RiceFarmerRest, Job::WinegrowerGoIn,
    Job::WinegrowerSearch, Job::WinegrowerLeaveToPlant, Job::WinegrowerPlant, Job::WinegrowerPlanted,
    Job::WinegrowerBackIn, Job::WinegrowerLeaveToHarvest, Job::WinegrowerAtField, Job::WinegrowerHarvest,
    Job::WinegrowerHome, Job::WinegrowerEnterLoaded, Job::WinegrowerStore, Job::WinegrowerRest,
    Job::WinegrowerGoInAgain, Job::DistillerGoIn, Job::DistillerTakeCoal, Job::DistillerTakeRice,
    Job::DistillerProduce, Job::DistillerEndRound, Job::DistillerTakeCoalAgain, Job::BrewerGoIn,
    Job::BrewerTakeWater, Job::BrewerTakeGrain, Job::BrewerProduce, Job::BrewerEndRound,
    Job::BrewerTakeWaterAgain, Job::PowderMakerGoIn, Job::PowderMakerTakeCoal, Job::PowderMakerTakeSulfur,
    Job::PowderMakerProduce, Job::PowderMakerEndRound, Job::PowderMakerTakeCoalAgain, Job::CharcoalBurnerGoIn,
    Job::CharcoalBurnerTakeBoard1, Job::CharcoalBurnerTakeBoard2, Job::CharcoalBurnerEndRound,
    Job::CharcoalBurnerTakeBoard1Again, Job::DistillerCheck, Job::BrewerCheck, Job::PowderMakerCheck,
    Job::CharcoalBurnerCheck, Job::CharcoalBurnerTakeBoard3, Job::CharcoalBurnerTakeBoard4,
    Job::CharcoalBurnerTakeBoard5, Job::CharcoalBurnerProduce, Job::PioneerWork, Job::PioneerDone,
    // the search, plant and land jobs more often
    Job::RiceFarmerSearch, Job::WinegrowerSearch, Job::RiceFarmerPlant, Job::WinegrowerPlant,
    Job::GeologistWork, Job::PioneerWork, Job::PioneerWork};
constexpr SettlerType kDrinkTypes[] = {SettlerType::Geologist,   SettlerType::Pioneer,   SettlerType::RiceFarmer,
                                       SettlerType::Winegrower,  SettlerType::Distiller, SettlerType::Brewer,
                                       SettlerType::PowderMaker, SettlerType::CharcoalBurner};

constexpr std::int32_t kMarker = 0x5a5a5a5a;  // MapOffset::sector of entry 0: the world is prepared

// Once per world: the offset table as rings of growing distance (as the
// game builds it) and small team numbers.
void PrepareWorld(CaseSetup& setup) {
    bool prepared = false;
    setup.Both([&](CGameData* w) { prepared = w->offsets[0].sector == kMarker; });
    if (prepared) {
        return;
    }
    std::uint32_t k = 0;
    for (std::int32_t r = 0; r <= 13; ++r) {
        for (std::int32_t dx = -r; dx <= r; ++dx) {
            for (std::int32_t dy = -r; dy <= r; ++dy) {
                if (CallStd<kTileDistance>(0, 0, dx, dy) != static_cast<std::uint32_t>(r)) {
                    continue;
                }
                setup.Both([&](CGameData* w) {
                    w->offsets[k].dx = dx;
                    w->offsets[k].dy = dy;
                    w->offsets[k].distance = r;
                });
                ++k;
            }
        }
    }
    setup.Both([&](CGameData* w) {
        w->offsets[k].distance = 1000;
        w->offsets[0].sector = kMarker;
    });
    for (std::uint32_t p = 0; p < layout::kPlayers; ++p) {
        const auto team = static_cast<std::int32_t>(setup.rng().Below(3));
        setup.Both([&](CGameData* w) { w->players[p].team = team; });
    }
}

// Tiles around (cx, cy) that the field jobs look at: ripe fields, free
// plots of the right terrain, the building's area.
void FieldTiles(CaseSetup& setup, std::uint32_t cx, std::uint32_t cy, std::int32_t radius, std::uint16_t area) {
    auto& rng = setup.rng();
    static constexpr std::uint8_t kObjects[] = {0, 0, 0, 0, 0, 0, 0x6c, 0x6d, 0x6e, 0x63, 0x64, 0x65, 0x67, 0x5e, 1};
    static constexpr std::uint8_t kTerrains[] = {0x15, 0x15, 0x10, 0x18, 0x16, 0x20};
    // Half the time almost every tile is empty (the vine rows need eight).
    const bool sparse = rng.Chance(1, 2);
    for (std::int32_t dx = -radius; dx <= radius; ++dx) {
        for (std::int32_t dy = -radius; dy <= radius; ++dy) {
            const std::uint8_t object = sparse && rng.Chance(19, 20) ? 0
                                        : rng.Chance(9, 10)          ? kObjects[rng.Below(sizeof(kObjects))]
                                                                     : static_cast<std::uint8_t>(rng.Next32());
            const auto number = static_cast<std::uint16_t>(rng.Chance(sparse ? 19 : 4, sparse ? 20 : 5)
                                                               ? 0
                                                               : 1 + rng.Below(testing::kPiles));
            const bool clear = rng.Chance(4, 5);
            const std::uint8_t terrain = kTerrains[rng.Below(sizeof(kTerrains))];
            const bool sameArea = rng.Chance(4, 5);
            const std::uint32_t tx = cx + static_cast<std::uint32_t>(dx);
            const std::uint32_t ty = cy + static_cast<std::uint32_t>(dy);
            setup.Both([&](CGameData* w) {
                Tile& t = w->tiles[tx][ty];
                t.object = object;
                t.object_number = number;
                if (clear) {
                    t.flags &= static_cast<std::uint8_t>(~kTileBlockedOrFootprint);
                }
                t.terrain = terrain;
                if (sameArea) {
                    t.area = area;
                }
            });
        }
    }
}

void CustomizeGeologist(CaseSetup& setup, std::uint32_t x, std::uint32_t y) {
    auto& rng = setup.rng();
    static constexpr std::uint8_t kResources[] = {0,    0x11, 0x17, 0x18, 0x1f, 0x20, 0x21, 0x28, 0x31, 0x38,
                                                  0x3f, 0x41, 0x47, 0x48, 0x4f, 0x51, 0x57, 0x58, 0x5f, 0x60};
    const std::uint8_t resources =
        rng.Chance(9, 10) ? kResources[rng.Below(sizeof(kResources))] : static_cast<std::uint8_t>(rng.Next32());
    static constexpr std::uint8_t kSignsNear[] = {0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, 0xf0, 0xf1, 0xf2, 0, 1};
    const std::uint32_t fill = rng.Below(4);  // 0: random signs, else mostly one
    const std::uint8_t sign = kSignsNear[rng.Below(sizeof(kSignsNear))];
    for (std::int32_t dx = -4; dx <= 4; ++dx) {
        for (std::int32_t dy = -4; dy <= 4; ++dy) {
            const std::uint8_t object =
                fill != 0 && rng.Chance(fill, 4) ? sign : kSignsNear[rng.Below(sizeof(kSignsNear))];
            const std::uint32_t tx = x + static_cast<std::uint32_t>(dx);
            const std::uint32_t ty = y + static_cast<std::uint32_t>(dy);
            setup.Both([&](CGameData* w) { w->tiles[tx][ty].object = object; });
        }
    }
    setup.Both([&](CGameData* w) { w->tiles[x][y].resources = resources; });
}

// A moat of unblocked tiles around the pioneer, with a blob of up to three
// takeable tiles next to it (the wall walk and the fill stay inside it).
// Most moat tiles are walls for the walk too (of the pioneer's player, or
// a terrain class without high nibble); the others end it.
void CustomizePioneer(CaseSetup& setup, std::uint32_t n, std::uint32_t x, std::uint32_t y) {
    auto& rng = setup.rng();
    const std::uint32_t walls = rng.Below(3);  // 0: random moat, else mostly walls
    for (std::uint32_t tx = x - 4; tx <= x + 4; ++tx) {
        for (std::uint32_t ty = y - 4; ty <= y + 4; ++ty) {
            const bool wall = walls != 0 && rng.Chance(walls == 1 ? 9 : 1, 10);
            const bool owned = rng.Chance(1, 2);
            setup.Both([&](CGameData* w) {
                Tile& t = w->tiles[tx][ty];
                t.flags &= static_cast<std::uint8_t>(~kTileBlockedOrFootprint);
                if (wall && owned) {
                    t.owner = w->settlers[n].owner;
                } else if (wall) {
                    t.terrain &= 0x0f;
                }
            });
        }
    }
    const bool freeHere = rng.Chance(5, 6);
    const auto hereOwner = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0xff : rng.Below(4));
    setup.Both([&](CGameData* w) {
        Tile& t = w->tiles[x][y];
        if (freeHere) {
            t.flags &= static_cast<std::uint8_t>(~kTileNotFree);
        }
        t.owner = hereOwner;
    });
    const std::uint32_t blob = rng.Below(4);
    std::uint32_t d = rng.Below(6);
    std::uint32_t bx = x;
    std::uint32_t by = y;
    for (std::uint32_t k = 0; k < blob; ++k) {
        bx = CallStd<kNeighbourX>(bx, d);
        by = CallStd<kNeighbourY>(by, d);
        const auto flags =
            static_cast<std::uint8_t>(rng.Chance(1, 4) ? layout::kTileBuildingFootprint : layout::kTileBlocked);
        const auto terrain = static_cast<std::uint8_t>(0x10 + rng.Below(0xf0));
        const auto owner = static_cast<std::uint8_t>(rng.Chance(1, 3) ? 0xff : rng.Below(4));
        setup.Both([&](CGameData* w) {
            Tile& t = w->tiles[bx][by];
            t.flags = static_cast<std::uint8_t>((t.flags & ~kTileBlockedOrFootprint) | flags);
            t.terrain = terrain;
            t.owner = owner;
        });
        if (rng.Chance(1, 2)) {
            d = (d + 1) % 6;
        }
    }
}

void CustomizeDrinks(CaseSetup& setup, std::uint32_t n) {
    PrepareWorld(setup);
    auto& rng = setup.rng();
    std::uint32_t job = 0, x = 0, y = 0, building = 0;
    setup.Both([&](CGameData* w) {
        const Settler& s = w->settlers[n];
        job = s.job;
        x = s.x;
        y = s.y;
        building = s.building;
    });
    // The building's piles: amounts around the limits the jobs test.
    std::uint8_t amounts[4];
    for (std::uint8_t& a : amounts) {
        a = static_cast<std::uint8_t>(rng.Below(10));
    }
    const std::uint16_t workX = setup.Coordinate();
    const std::uint16_t workY = setup.Coordinate();
    const auto area = static_cast<std::uint16_t>(1 + rng.Below(testing::kAreas - 1));
    setup.Both([&](CGameData* w) {
        Building& b = w->buildings[building];
        w->piles[b.pile_ring].amount = amounts[0];
        w->piles[b.piles[0]].amount = amounts[1];
        w->piles[b.piles[1]].amount = amounts[2];
        w->piles[b.piles[2]].amount = amounts[3];
        b.work_x = workX;
        b.work_y = workY;
        w->tiles[b.door_x][b.door_y].area = area;
    });
    const auto j = static_cast<Job>(job);
    if (j == Job::RiceFarmerSearch || j == Job::WinegrowerSearch) {
        FieldTiles(setup, workX, workY, 8, area);
        const auto cursor = static_cast<std::uint16_t>(rng.Chance(1, 2) ? rng.Below(20) : rng.Below(400));
        setup.Both([&](CGameData* w) { w->settlers[n].next = cursor; });
    } else if (j == Job::RiceFarmerPlant || j == Job::WinegrowerPlant || j == Job::RiceFarmerAtField ||
               j == Job::WinegrowerAtField) {
        FieldTiles(setup, x, y, 3, area);
    } else if (j == Job::GeologistWork) {
        CustomizeGeologist(setup, x, y);
    } else if (j == Job::PioneerWork) {
        CustomizePioneer(setup, n, x, y);
    }
}

// Side effects the pioneer's fill needs (without them the fill never ends):
// SetOwnerOfFreeTile and ClearTileOwner change the tile's owner.
bool DrinkStubs(std::uintptr_t callee, std::uint32_t, const std::uint32_t* args, CGameData* world,
                std::uint32_t& eax) {
    if ((callee != kSetOwnerOfFreeTile && callee != kClearTileOwner) || args[0] >= layout::kMapRowTiles ||
        args[1] >= layout::kMapRowTiles) {
        return false;
    }
    Tile& t = world->tiles[args[0]][args[1]];
    if (callee == kClearTileOwner) {
        t.owner = kFree;
    } else if (t.owner == kFree) {
        t.owner = static_cast<std::uint8_t>(args[2]);
    }
    eax = 0;
    return true;
}

FamilyTest DrinkFamily() {
    FamilyTest family{kDrinkJobs, sizeof(kDrinkJobs) / sizeof(kDrinkJobs[0]), kDrinkTypes,
                      sizeof(kDrinkTypes) / sizeof(kDrinkTypes[0])};
    family.customize = &CustomizeDrinks;
    family.stubReturn = &DrinkStubs;
    family.minCompared = 3000;
    return family;
}

// Wrong: the geologist's sign is one object off.
std::uint32_t __fastcall WrongGeologistSign(CGameData* world, void* edx) {
    const std::uint32_t result = CGameData_UpdateSettlers(world, edx);
    for (std::uint32_t n = 1; n <= world->settler_high_water; ++n) {
        const Settler& s = world->settlers[n];
        if (JobOf(s) == Job::GeologistDone && s.x < layout::kMapRowTiles && s.y < layout::kMapRowTiles) {
            world->tiles[s.x][s.y].object ^= 1;
        }
    }
    return result;
}

// Wrong: the field search cursor is not reset after a find.
std::uint32_t __fastcall WrongFieldCursor(CGameData* world, void* edx) {
    const std::uint32_t result = CGameData_UpdateSettlers(world, edx);
    for (std::uint32_t n = 1; n <= world->settler_high_water; ++n) {
        Settler& s = world->settlers[n];
        if ((JobOf(s) == Job::RiceFarmerLeaveToPlant || JobOf(s) == Job::WinegrowerLeaveToPlant) &&
            s.next == 0) {
            s.next = 1;
        }
    }
    return result;
}

}  // namespace

S3RE_TEST(CGameData_UpdateSettlers_Drinks_MatchesOriginal) {
    testing::CompareUpdateSettlers(ctx, &CGameData_UpdateSettlers, DrinkFamily(), 4000);
}

S3RE_TEST(CGameData_UpdateSettlers_Drinks_ComparisonDetectsWrongSign) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    testing::CompareUpdateSettlers(probe, &WrongGeologistSign, DrinkFamily(), 600);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CGameData_UpdateSettlers_Drinks_ComparisonDetectsWrongCursor) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    testing::CompareUpdateSettlers(probe, &WrongFieldCursor, DrinkFamily(), 1000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game::settler_jobs
