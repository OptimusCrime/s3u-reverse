// CGameData::UpdateSettlers (0x004e9670), job handlers 0x4eb43b..0x4ee0c1 of
// the original (settler_jobs.h):
//
// - Diggers: 5 (pick a tile of the site's levelling grid whose height
//   differs, or give up and release the site), 7 (level the tile one step
//   towards the wanted height), 8/9 (after the animation back to 7).
// - Builders: 0xe (go to a free tile next to the door, or to the next
//   unused tile of the building's layout), 0x12 (build: take boards and
//   stones, count up the buildup, finish the site with BuildingDone).
// - Carriers: 0xb, 0xa9, 0xaa (pick up a good from a pile and carry it to
//   the building's input), 0xd, 0x50, 0x51 (deliver), 0x4d, 0x4f (to and
//   from trade piles), 0x69 (take a tool and become a worker), 0x8a (take a
//   weapon at the barracks and become a soldier).
// - Soldiers entering a military building: 0x10, 0x6a, 0x8b, 0x8c.
// - The woodcutter (0x13, 0x18, 0x1e, 0x20, 0x21, 0x24..0x26, 0x66), the
//   stonecutter (0x14, 0x19, 0x1f, 0x22, 0x23, 0x27..0x29, 0x67), the
//   sawmill start (0x15, 0x1a) and the forester (0x16, 0x1b, 0x2c, 0x2e,
//   0x2f, 0x65); the smelter starts 0x38 and 0x42 share the sawmill's code.
//
// A few handlers continue in code of other ranges (shared tails); those
// parts are duplicated here with the address in a comment: 0x4f8b7c (wait
// 10 ticks), 0x4f8f44, 0x4f73bb, 0x4f827f/0x4f82a0 and 0x4f4328.
//
// Frame slots (settler_jobs.h): the tile searches of jobs 5, 0xe, 0x18,
// 0x19 and 0x1b leave their last candidate in slots 0x20 (x) and 0x14 (y),
// and several carrier jobs leave a pile number in slot 0x30; they are
// mirrored into ctx.frame. No handler of this range reads a slot another
// settler's handler left (the reads the leak analysis lists here are all
// preceded by a write on every path that reaches them).

#include "game/settler_jobs.h"
#include "game/settler_jobs_test.h"
#include "test/callee_stub.h"

#include <windows.h>

#include <cstdint>

namespace s3re::game::settler_jobs {


namespace {

using layout::MapOffset;
using layout::PlayerSiteCache;
using J = Job;
using T = SettlerType;

constexpr std::uint32_t kWaitTicks = 10;        // 0x4f8b7c: no work or the output pile is full
constexpr std::uint32_t kPileFull = 7;          // more than 7 units: the output pile is full
constexpr std::uint32_t kSiteGridSide = 31;     // PlayerSiteCache::grid is 31 x 31 tiles ...
constexpr std::uint32_t kSiteGridCentre = 15;   // ... centred on the building
constexpr std::uint32_t kSiteGridCells = kSiteGridSide * kSiteGridSide;
constexpr std::uint32_t kSearchSteps = 50;      // offset-table entries a worker looks at per try
constexpr std::uint32_t kLayoutStart = 0x80000005;  // building layout list: start of the builder spots

// Tile flags used here (layout::kTile*).
constexpr std::uint8_t kTileTargeted = layout::kTileFlag02;  // a digger / builder goes there

// Landscape objects (Tile::object) of trees and stones.
constexpr std::uint8_t kFirstTree = 0x44;
constexpr std::uint8_t kLastTree = 0x52;
constexpr std::uint8_t kFirstStone = 0x73;
constexpr std::uint8_t kLastStone = 0x7e;
constexpr std::uint8_t kStoneUsedUp = 0x7f;
constexpr std::uint8_t kSapling = 0x54;

// ------------------------------------------------------------------- helpers

std::uint32_t TileOff(std::uint32_t x, std::uint32_t y) { return TileOffset(x, y); }
// The tile dx, dy steps from a tile offset (the original's +-0x4800 / +-0x18 arithmetic).
Tile& TileNear(CGameData* world, std::uint32_t offset, std::int32_t dx, std::int32_t dy) {
    return TileAtOffset(world, offset + static_cast<std::uint32_t>(dx * static_cast<std::int32_t>(kTileRowBytes) +
                                                                   dy * static_cast<std::int32_t>(kTileBytes)));
}

// Clears the highest set bit; false if no bit was set (the original's chain
// of tests 0x80 .. 0x01).
bool ClearHighestBit(std::uint8_t& bits) {
    for (std::uint8_t bit = 0x80; bit != 0; bit >>= 1) {
        if ((bits & bit) != 0) {
            bits = static_cast<std::uint8_t>(bits - bit);
            return true;
        }
    }
    return false;
}

// The site cache entry of a construction site (its levelling grid), as the
// original indexes it: (slot + owner * 30) entries of 0x3c2 bytes.
std::uint8_t* SiteEntry(CGameData* world, std::uint32_t slot, std::uint32_t owner) {
    const std::uint32_t index = slot + owner * 30;
    return reinterpret_cast<std::uint8_t*>(reinterpret_cast<std::uintptr_t>(&world->site_cache[0][0]) +  // raw-ok: computed record index, as the original
                                           index * static_cast<std::uint32_t>(sizeof(PlayerSiteCache)));
}

// 0x7acfd0 + owner * 0x44: the dword four bytes before the owner's tally
// (the original indexes g_playerTallies - 4; for owner 0 it lies before the
// array).
std::int32_t& TallyBefore(std::uint32_t owner) {
    return *(reinterpret_cast<std::int32_t*>(&g_playerTallies[0]) + owner * 17 - 1);
}

// The current animation / step done; wait 10 ticks (0x4f8b7c).
JobResult WaitTen(Settler& settler) {
    settler.counter = kWaitTicks;
    return JobResult::Next;
}

// 0x4f4328: counter 0, walk to the building's door; next settler.
JobResult WalkToDoor(SettlerContext& ctx) {
    ctx.settler.counter = 0;
    const Building& building = BuildingAt(ctx.world, ctx.settler.building);
    Call<kSetSettlerTarget>(ctx.world, ctx.number, building.door_x, building.door_y);
    return JobResult::Next;
}

// 0x4f827f / 0x4f82a0: counter 0, walk to the building's door; run again.
JobResult WalkToDoorAgain(SettlerContext& ctx) {
    ctx.settler.counter = 0;
    const Building& building = BuildingAt(ctx.world, ctx.settler.building);
    Call<kSetSettlerTarget>(ctx.world, ctx.number, building.door_x, building.door_y);
    return JobResult::Again;
}

Tile& OwnTile(SettlerContext& ctx) { return TileAt(ctx.world, ctx.settler.x, ctx.settler.y); }

// ------------------------------------------------------------------- diggers

// The digger releases its slot at the site (one bit of digger_slots).
void ReleaseDiggerSlot(Building& building, bool alsoWanted) {
    if (ClearHighestBit(building.digger_slots)) {
        --building.site.workers_at_work;
        if (alsoWanted) {
            --building.site.diggers;
        }
    }
}

// Whether a candidate tile of height `height` may be levelled towards
// `wanted`: none of its neighbours (x-1, y-1), (x, y-1) may be 7 and none
// of (x, y+1), (x+1, y+1) 5 steps beyond it in the direction of the change
// (0x4eb5ae..0x4eb626; the same test in job 7).
bool LevelStepAllowed(CGameData* world, std::uint32_t offset, std::int32_t height, std::int32_t wanted) {
    const std::int32_t first = wanted < height ? height + 7 : height - 5;
    const std::int32_t second = wanted < height ? height + 5 : height - 7;
    return TileNear(world, offset, -1, -1).height != first && TileNear(world, offset, 0, -1).height != first &&
           TileNear(world, offset, 0, 1).height != second && TileNear(world, offset, 1, 1).height != second;
}

// Purpose: A digger at a construction site picks the next tile to level:
// a random start in the site's 31x31 levelling grid, then steps of 13
// cells; a cell holds the wanted height (0 = done). With no tile left the
// site is levelled (buildup 2) and the digger is released.
// 0x4eb43b..0x4eb8ad (job 5)
JobResult DiggerDig(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    const std::uint32_t b = settler.building;
    if (BuildingAt(world, b).buildup != 1) {
        ReleaseDiggerSlot(BuildingAt(world, b), false);  // 0x4eb839
        Call<kClearSettlerJob>(world, ctx.number);
        return JobResult::Next;
    }
    const std::uint32_t r1 = Call<kRandom>(world) & 0xffff;
    std::uint32_t row = (r1 * 31) >> 16;
    const std::uint32_t r2 = Call<kRandom>(world) & 0xffff;
    std::uint32_t column = (r2 * 31) >> 16;
    const std::uint8_t* entry = SiteEntry(world, BuildingAt(world, b).levelling_slot, settler.owner);
    std::uint32_t tries = 0;
    std::uint32_t tx = 0;
    std::uint32_t ty = 0;
    bool found = false;
    for (;;) {
        row += 13;
        ++tries;
        if (row >= kSiteGridSide) {
            row -= kSiteGridSide;
            if (++column >= kSiteGridSide) {
                column = 0;
            }
        }
        const std::uint8_t wanted = entry[1 + row * kSiteGridSide + column];
        if (wanted != 0) {
            const Building& building = BuildingAt(world, b);
            tx = building.x + row - kSiteGridCentre;
            ty = building.y + column - kSiteGridCentre;
            ctx.frame.slot20 = tx;
            ctx.frame.slot14 = ty;
            const std::uint32_t offset = TileOff(tx, ty);
            const Tile& tile = TileAtOffset(world, offset);
            if (wanted != tile.height && (tile.flags & kTileTargeted) == 0 &&
                LevelStepAllowed(world, offset, tile.height, wanted)) {
                found = true;
            }
        }
        if (found && tries <= kSiteGridCells) {
            break;
        }
        if (tries > kSiteGridCells) {
            found = false;
            break;
        }
        found = false;
    }
    if (found) {
        // 0x4eb79c: walk there.
        Tile& tile = TileAt(world, tx, ty);
        tile.flags |= kTileTargeted;
        if ((tile.flags & layout::kTileBlocked) != 0) {
            // 0x4f922f: "Digger trouble", then the original hangs on purpose.
            const std::uint32_t site = SettlerAt(world, ctx.number).building;
            GameDebugLog(texts::kDiggerTrouble, site, static_cast<std::uint32_t>(BuildingAt(world, site).levelling_slot));
            for (;;) {
                Sleep(1000);
            }
        }
        Call<kSetSettlerTarget>(world, ctx.number, tx, ty);
        SetJob(settler, J::DiggerWalkToSpot);
        settler.counter = 0;
        const std::uint8_t* now = SiteEntry(world, BuildingAt(world, settler.building).levelling_slot, settler.owner);
        settler.food = now[1 + row * kSiteGridSide + column];  // +0x17: the wanted height
        return JobResult::Next;
    }
    // 0x4eb64b: nothing found. An empty grid means the site is levelled.
    const std::uint8_t* grid = SiteEntry(world, BuildingAt(world, b).levelling_slot, settler.owner) + 1;
    bool any = false;
    for (std::uint32_t k = 0; k < kSiteGridCells; ++k) {
        any = any || grid[k] != 0;
    }
    if (!any && BuildingAt(world, b).buildup == 1) {
        BuildingAt(world, b).buildup = 2;
        Call<kClearBuildingFlag10>(world, settler.building);
        SiteEntry(world, BuildingAt(world, settler.building).levelling_slot, settler.owner)[0] = 0;
    }
    const std::uint32_t site = settler.building;
    settler.building = 0;
    ReleaseDiggerSlot(BuildingAt(world, site), true);
    Call<kClearSettlerJob>(world, ctx.number);
    return JobResult::Next;
}

// Purpose: A digger levels its tile one step towards the wanted height
// (+0x17); at the wanted height the tile is done in the site grid.
// 0x4eb8ae..0x4ebab7 (job 7)
JobResult DiggerLevel(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    const std::uint32_t x = settler.x;
    const std::uint32_t y = settler.y;
    const std::uint32_t offset = TileOff(x, y);
    Tile& tile = TileAtOffset(world, offset);
    const std::int32_t wanted = settler.food;
    const std::int32_t height = tile.height;
    if (height == wanted) {
        // 0x4eba36: mark the grid cell done.
        const Building& building = BuildingAt(world, settler.building);
        std::uint8_t* entry = SiteEntry(world, building.levelling_slot, settler.owner);
        const std::uint32_t cell =
            1 + kSiteGridCentre * kSiteGridSide + kSiteGridCentre + (x - building.x) * kSiteGridSide + (y - building.y);
        *reinterpret_cast<std::uint8_t*>(reinterpret_cast<std::uintptr_t>(entry) + cell) = 0;  // raw-ok: unchecked cell, as the original
        tile.flags &= 0xf9;
        SetJob(settler, J::DiggerDig);
        settler.counter = 0;
        return JobResult::Next;
    }
    if (!LevelStepAllowed(world, offset, height, wanted)) {
        tile.flags &= static_cast<std::uint8_t>(~kTileTargeted);  // 0x4eba11
        SetJob(settler, J::DiggerDig);
        settler.counter = 0;
        return JobResult::Next;
    }
    if (height > wanted) {
        SetJob(settler, J::Job8);
        Call<kSetSettlerAnimation>(world, ctx.number, 1);
        tile.height = static_cast<std::uint8_t>(tile.height - 1);
    } else {
        SetJob(settler, J::Job9);
        Call<kSetSettlerAnimation>(world, ctx.number, 1);
        tile.height = static_cast<std::uint8_t>(tile.height + 1);
    }
    --BuildingAt(world, settler.building).work_x;  // +0x30: tiles left to level
    tile.draw_flags |= layout::kTileDrawRedraw;
    world->redraw = 0;
    Call<kUpdateShadeAround>(world, x, y);
    Call<kWearPath>(world, x, y);
    return JobResult::Next;
}

// ------------------------------------------------------------------- recruits and tools

// Purpose: A carrier at the barracks takes its weapon (sword, bow or pike,
// +0x20 = input slot 1..3) and becomes a soldier of its player's current
// level; without the weapon it goes idle.
// 0x4ebac6..0x4ebe0e (job 0x8a)
JobResult RecruitAtBarracks(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    std::uint32_t missing = 0;
    const std::uint16_t slot = settler.saved_counter;
    if (slot == 1) {
        if (PileAt(world, BuildingAt(world, settler.building).piles[0]).amount == 0) {
            missing = 1;
        }
    } else if (slot == 2) {
        if (PileAt(world, BuildingAt(world, settler.building).piles[1]).amount == 0) {
            missing = 1;
        }
    }
    if (settler.saved_counter == 3 && PileAt(world, BuildingAt(world, settler.building).piles[2]).amount == 0) {
        ++missing;
    }
    if (static_cast<std::uint16_t>(missing) != 0) {
        Call<kResetSettlerToIdle>(world, ctx.number);  // 0x4eabc5
        return JobResult::Next;
    }
    const std::uint32_t owner = settler.owner;
    ++g_playerTallies[owner].unk10;
    const Player& player = PlayerAt(world, owner);
    static constexpr T kSoldiers[3][3] = {{T::Swordsman1, T::Swordsman2, T::Swordsman3},
                                          {T::Bowman1, T::Bowman2, T::Bowman3},
                                          {T::Pikeman1, T::Pikeman2, T::Pikeman3}};
    const std::uint32_t kind = settler.saved_counter;
    if (kind >= 1 && kind <= 3) {
        const std::int32_t level = player.soldier_levels[kind - 1];  // +0xb8 / +0xbc / +0xc0
        if (level >= 0 && level <= 2) {
            settler.art = Value(kSoldiers[kind - 1][level]);
        }
    }
    settler.health = static_cast<std::uint8_t>(Call<kSettlerStartHealth>(world, ctx.number));
    const std::uint32_t b = settler.building;
    Building& building = BuildingAt(world, b);
    // Take the weapon from the input pile of the soldier's class.
    for (std::int32_t typeClass = 1; typeClass <= 3; ++typeClass) {
        if ((&g_settlerTypeClass[0])[settler.art] != typeClass) {
            continue;
        }
        const std::uint32_t input = static_cast<std::uint32_t>(typeClass - 1);
        Call<kPileIncAmountLeaving>(world, building.piles[input]);
        Call<kTakeGoodFromPile>(world, building.piles[input]);
        --building.recruiting.recruits[input];
        Call<kSubtractPlayerCounter>(world, settler.owner, 18 + input, 1);  // goods 18..20: sword, bow, pike
    }
    Call<kClearSettlerJob>(world, ctx.number);
    return JobResult::Next;
}

// Purpose: A carrier on a tool pile picks up the tool and walks to its
// building as a new worker, if its area still wants workers of that kind
// (area shares +0x23e.. in percent of the carriers); otherwise it goes idle.
// 0x4ebe0f..0x4ec00e (job 0x69)
JobResult CarrierTakeTool(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    const std::uint32_t areaNumber = OwnTile(ctx).area;
    const std::uint32_t carriers = Call<kCountAreaSettlersOfClass0>(world, areaNumber);
    const Area& area = AreaAt(world, areaNumber);
    const std::uint32_t diggers = Call<kCountAreaSettlersOfType>(world, areaNumber, 0);
    if ((area.settings_extra[0] * carriers) / 100 >= diggers) {
        Call<kResetSettlerToIdle>(world, ctx.number);
        return JobResult::Next;
    }
    bool go = false;
    if (settler.tool_type == 1) {
        const std::uint32_t count = Call<kCountAreaSettlersOfType>(world, areaNumber, 1);
        go = (area.settings_extra[1] * carriers) / 100 > count;
    }
    if (!go && settler.tool_type == 2) {
        const std::uint32_t count = Call<kCountAreaSettlersOfType>(world, areaNumber, 2);
        go = (area.settings_extra[2] * carriers) / 100 > count;
    }
    if (!go && (settler.tool_type == 1 || settler.tool_type == 2)) {
        Call<kResetSettlerToIdle>(world, ctx.number);
        return JobResult::Next;
    }
    // 0x4ebf01
    const Tile& tile = OwnTile(ctx);
    const std::uint16_t pile = tile.object_number;
    if (pile == 0 || (tile.flags & layout::kTileHasBuilding) != 0 || PileAt(world, pile).amount == 0) {
        Call<kResetSettlerToIdle>(world, ctx.number);
        return JobResult::Next;
    }
    const Building& building = BuildingAt(world, settler.building);
    Call<kSetSettlerTarget>(world, ctx.number, building.door_x, building.door_y);
    SetJob(settler, J::WalkToWork);
    settler.art = settler.tool_type;
    settler.counter = 0;
    const std::uint32_t taken = OwnTile(ctx).object_number;
    ctx.frame.slot30 = taken;
    Call<kSubtractPlayerCounter>(world, settler.owner, PileAt(world, taken).good, 1);
    Call<kTakeGoodFromPile>(world, taken);
    return JobResult::Next;
}

// ------------------------------------------------------------------- carriers

// Purpose: A carrier at a pile bends down (animation 0x6f, then 0x70).
// 0x4ec00f (job 0xb) and 0x4ec27e (job 0xd)
JobResult CarrierBend(SettlerContext& ctx, J next) {
    Call<kSetSettlerAnimation>(ctx.world, ctx.number, 0x6f);
    Call<kSetSettlerSavedAnimation>(ctx.world, ctx.number, 0x70);
    SetJob(ctx.settler, next);
    return JobResult::Next;
}

// Purpose: The carrier takes the good from the pile it stands on.
// 0x4ec031..0x4ec085 (job 0xa9)
JobResult CarrierPickUp(SettlerContext& ctx) {
    Settler& settler = ctx.settler;
    Call<kRestoreSettlerAnimation>(ctx.world, ctx.number);
    settler.slot = settler.good;  // +0x15: the good carried
    const std::uint32_t pile = OwnTile(ctx).object_number;
    ctx.frame.slot30 = pile;
    Call<kTakeGoodFromPile>(ctx.world, pile);
    SetJob(settler, J::CarrierLeavePile);
    return JobResult::Next;
}

// Purpose: The carrier sets off to the building that wants its good: the
// door of a construction site (boards, stones) or the input pile of the
// good at a finished building.
// 0x4ec086..0x4ec1f5 (job 0xaa)
JobResult CarrierLeavePile(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    const Building& building = BuildingAt(world, settler.building);
    if (building.buildup != 0) {
        if (settler.good == Value(Good::Board) || settler.good == Value(Good::Stone)) {
            Call<kSetSettlerTarget>(world, ctx.number, building.door_x, building.door_y);
        }
    } else {
        const std::uint32_t art = building.art;
        const std::uint32_t race = building.race;
        const std::uint32_t good = settler.good;
        for (std::uint32_t input = 0; input < 3; ++input) {
            if (static_cast<std::uint32_t>(g_buildingInputGoods[art][input]) != good) {
                continue;
            }
            const std::uint32_t index = (race * 57 + art) * 3 + input;
            const std::uint32_t y = building.y + static_cast<std::uint32_t>(g_buildingInputOffsets[index][1]);
            const std::uint32_t x = building.x + static_cast<std::uint32_t>(g_buildingInputOffsets[index][0]);
            Call<kSetSettlerTarget>(world, ctx.number, x, y);
            break;
        }
    }
    SetJob(settler, J::CarrierWalkToBuilding);
    settler.counter = 0;  // 0x4f91fb with EDX = 0
    return JobResult::Next;
}

// Purpose: A carrier at a trade pile (its number in +0x22) picks up the
// good there and walks back to it.
// 0x4ec1f6..0x4ec27d (job 0x4d)
JobResult CarrierAtTradePile(SettlerContext& ctx) {
    Settler& settler = ctx.settler;
    const Pile& target = PileAt(ctx.world, settler.building);
    Call<kSetSettlerTarget>(ctx.world, ctx.number, target.x, target.y);
    SetJob(settler, J::Job78);
    settler.counter = 0;
    settler.slot = settler.good;
    const std::uint32_t pile = OwnTile(ctx).object_number;
    ctx.frame.slot30 = pile;
    Call<kTakeGoodFromPile>(ctx.world, pile);
    return JobResult::Next;
}

// Purpose: The carrier puts its good down at the building: a construction
// site's board or stone pile (and the site stops asking once enough are
// there), or the finished building's input pile of that good.
// 0x4ec2a0..0x4ec3fa (job 0x50)
JobResult CarrierDeliver(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    Call<kRestoreSettlerAnimation>(world, ctx.number);
    SetJob(settler, J::Job81);
    const std::uint32_t b = settler.building;
    Building& building = BuildingAt(world, b);
    if (building.buildup != 0) {
        bool cleared = false;
        if (settler.slot == 1) {
            const std::uint32_t pile = building.piles[0];
            ctx.frame.slot30 = pile;
            Call<kAddGoodsToPile>(world, pile);
            if (PileAt(world, pile).amount == building.site.boards_missing) {
                Call<kClearBuildingFlag02>(world, b);
                cleared = true;
            }
        } else if (settler.slot == 2) {
            const std::uint32_t pile = building.piles[1];
            ctx.frame.slot30 = pile;
            Call<kAddGoodsToPile>(world, pile);
            if (PileAt(world, pile).amount == building.site.stones_missing) {
                Call<kClearBuildingFlag04>(world, b);
                cleared = true;
            }
        }
        if (cleared && static_cast<std::uint32_t>(g_selectedBuilding) == b) {
            world->refresh_ui = 1;
        }
    } else {
        // 0x4ec381: the input pile whose good it carries.
        for (std::uint32_t input = 0; input < 3; ++input) {
            const std::uint32_t pile = building.piles[input];
            if (settler.slot == PileAt(world, pile).good) {
                Call<kAddGoodsToPile>(world, pile);
                break;
            }
        }
    }
    settler.slot = 0;
    settler.building = 0;
    return JobResult::Next;
}

// Purpose: A carrier delivers to a trade pile (art 0xfc; its trade building
// in Pile::prev): the trade record's ordered and coming counts go down; if
// nothing of that good is ordered any more the good is dropped nearby.
// Other piles just get the good.
// 0x4ec3fb..0x4ec58c (job 0x4f)
JobResult CarrierAtTradeBuilding(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    const std::uint32_t pileNumber = OwnTile(ctx).object_number;
    ctx.frame.slot30 = pileNumber;
    Pile& pile = PileAt(world, pileNumber);
    if (pile.art != 0xfc) {
        Call<kAddGoodsToPile>(world, pileNumber);  // 0x4ec571
        settler.slot = 0;
        Call<kClearSettlerJob>(world, ctx.number);
        return JobResult::Next;
    }
    layout::Trade& trade = *(world->trades + BuildingAt(world, pile.prev).trade);
    // The goods index is not range-checked, as in the original.
    std::uint8_t* const ordered = &trade.ordered[0];
    std::uint8_t* const coming = &trade.coming[0];
    if (ordered[settler.slot] != 0) {
        pile.leaving = static_cast<std::uint8_t>(pile.leaving + 0xf0);  // high nibble: units coming
        pile.amount = static_cast<std::uint8_t>(pile.amount + g_byte730ef8);
        if (ordered[settler.slot] != 100) {
            --ordered[settler.slot];
        }
        --coming[settler.slot];
        settler.slot = 0;
        Call<kClearSettlerJob>(world, ctx.number);
        return JobResult::Next;
    }
    pile.leaving = static_cast<std::uint8_t>(pile.leaving + 0xf0);  // 0x4ec4f1
    --coming[settler.slot];
    Call<kDropGoodNear>(world, settler.slot, 1, settler.x, settler.y);
    Call<kSubtractPlayerCounter>(world, settler.owner, settler.slot, 1);
    settler.slot = 0;
    Call<kClearSettlerJob>(world, ctx.number);
    return JobResult::Next;
}

// ------------------------------------------------------------------- builders

void ReleaseBuilderSlot(Building& building) {
    if (ClearHighestBit(building.builder_slots)) {
        --building.site.unk1e;  // +0x1e: builders holding a slot
    }
}

bool FreeForBuilder(SettlerContext& ctx, std::uint32_t x, std::uint32_t y) {
    CGameData* world = ctx.world;
    if (x >= world->map_size || y >= world->map_size) {
        return false;
    }
    return Call<kCanSettlerEnterTile>(world, x, y, ctx.settler.owner, ctx.settler.art) != 0 &&
           (TileAt(world, x, y).flags & 0x0f) == 0;
}

// Purpose: A builder arrives at its site. Before the building stage it
// waits next to the door (a free tile from the offset table, or a random
// neighbour); in the building stage it walks to the next unused spot of
// the building's layout. Without a site any more it leaves.
// 0x4ec58d..0x4ec95f (job 0xe; 0x4ec67f is job 0x51)
JobResult BuilderStart(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    const std::uint32_t b = settler.building;
    Building& building = BuildingAt(world, b);
    const std::uint8_t buildup = building.buildup;
    const bool goBuild = buildup >= 4 ||
                         (buildup == 3 && PileAt(world, building.piles[1]).amount +
                                                  PileAt(world, building.piles[0]).amount != 0);
    if (goBuild) {
        // 0x4ec891: the first spot after the layout's start marker that no
        // other builder has taken.
        const std::uint32_t* list = &g_buildingLayouts[0][0] + (building.race * 57 + building.art) * 600;
        std::uint32_t k = 0;
        while (list[k] != kLayoutStart) {
            ++k;
        }
        const std::uint32_t* spot = list + k + 1;
        const std::uint32_t bx = building.x;
        const std::uint32_t by = building.y;
        std::uint32_t x;
        std::uint32_t y;
        std::uint32_t value;
        for (;;) {
            x = bx + spot[0];
            y = by + spot[1];
            value = spot[2];
            spot += 3;
            if ((TileAt(world, x, y).flags & kTileTargeted) == 0) {
                break;
            }
        }
        TileAt(world, x, y).flags |= kTileTargeted;
        Call<kSetSettlerTarget>(world, ctx.number, x, y);
        SetJob(settler, J::Job17);
        settler.counter = 0;
        settler.food = static_cast<std::uint8_t>(value);  // +0x17: the builder's animation variant
        return JobResult::Next;
    }
    if (buildup == 0) {
        ReleaseBuilderSlot(building);
        Call<kClearSettlerJob>(world, ctx.number);  // 0x4ec67f (also job 0x51)
        return JobResult::Next;
    }
    // 0x4ec68c: wait next to the door.
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    if ((OwnTile(ctx).flags & 0x0f) != 0) {
        const MapOffset* offset = &world->offsets[0];
        for (bool found = false; !found; ++offset) {
            const Building& site = BuildingAt(world, settler.building);
            x = site.door_x + static_cast<std::uint32_t>(offset->dx);
            ctx.frame.slot20 = x;
            y = site.door_y + static_cast<std::uint32_t>(offset->dy);
            ctx.frame.slot14 = y;
            found = FreeForBuilder(ctx, x, y);
        }
    } else {
        // 0x4ec777: a random free neighbour.
        std::uint32_t direction = Call<kRandomBelow6>(world);
        bool found = false;
        for (std::uint32_t tries = 0; tries < 6 && !found; ++tries) {
            x = CallStd<kNeighbourX>(settler.x, direction);
            y = CallStd<kNeighbourY>(settler.y, direction);
            ctx.frame.slot14 = y;
            found = FreeForBuilder(ctx, x, y);
            direction = CallStd<kNextDirection>(direction);
        }
        ctx.frame.slot20 = x;
        if (!found) {
            settler.counter = 0;  // 0x4eba28
            return JobResult::Next;
        }
    }
    Call<kSetSettlerTarget>(world, ctx.number, x, y);
    SetJob(settler, J::Job15);
    settler.counter = 0;
    return JobResult::Next;
}

// Purpose: A builder works at its spot: in stage 3 it takes a board or a
// stone (stage 4); with none left and none missing the site is done
// (BuildingDone); stages 4..50 count up the work, after 50 one more
// material is built in (+0x20) and the stage goes back to 3.
// 0x4ec960..0x4ecc69 (job 0x12)
JobResult BuilderBuild(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    const std::uint32_t b = settler.building;
    Building& building = BuildingAt(world, b);
    if (building.buildup == 0) {
        OwnTile(ctx).flags &= static_cast<std::uint8_t>(~kTileTargeted);
        ReleaseBuilderSlot(building);
        Call<kClearSettlerJob>(world, ctx.number);
        return JobResult::Next;
    }
    if (building.buildup == 3) {
        const std::uint32_t owner = settler.owner;
        bool took = false;
        for (std::uint32_t material = 0; material < 2 && !took; ++material) {
            if (PileAt(world, building.piles[material]).amount == 0 || (building.request_flags & 1) == 0) {
                continue;
            }
            ++TallyBefore(owner);
            if (material == 0) {
                --building.site.boards_missing;
            } else {
                --building.site.stones_missing;
            }
            building.buildup = 4;
            Call<kSubtractPlayerCounter>(world, settler.owner, material + 1, 1);  // goods 1 boards, 2 stones
            const std::uint32_t pile = building.piles[material];
            ctx.frame.slot30 = pile;
            Call<kPileIncAmountLeaving>(world, pile);
            Call<kTakeGoodFromPile>(world, pile);
            took = true;
        }
        if (!took) {
            // 0x4ecb45: nothing to build in.
            if (building.site.stones_missing + building.site.boards_missing == 0) {
                Call<kClearBuildingFlag08>(world, b);
                Call<kBuildingDone>(world, b);
            }
            OwnTile(ctx).flags &= static_cast<std::uint8_t>(~kTileTargeted);
            ReleaseBuilderSlot(building);
            Call<kClearSettlerJob>(world, ctx.number);
        }
    }
    // 0x4ecbfd
    const std::uint8_t stage = building.buildup;
    if (stage < 4) {
        return JobResult::Next;
    }
    if (stage == 50) {
        ++building.site.materials_used;
        building.buildup = 3;
    } else {
        building.buildup = static_cast<std::uint8_t>(stage + 1);
    }
    Call<kSetSettlerAnimation>(world, ctx.number, settler.food == 0 ? 2 : 3);
    if ((Call<kRandom>(world) & 1) != 0) {
        --settler.counter;
    }
    return JobResult::Next;
}

// ------------------------------------------------------------------- soldiers

// Purpose: A soldier enters its military building: it leaves its tile,
// joins the garrison chain, and the territory is recomputed when it is the
// first one.
// 0x4ecc6a (job 0x10: class 1), 0x4ecd51 (job 0x8b: class 3), shared 0x4ecd76.
JobResult SoldierEnter(SettlerContext& ctx, J next, std::uint8_t& coming) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    SetJob(settler, next);
    --coming;
    OwnTile(ctx).settler = 0;
    settler.counter = 0;
    Building& building = BuildingAt(world, settler.building);
    settler.next = building.garrison;
    BuildingAt(world, settler.building).garrison = static_cast<std::uint16_t>(ctx.number);
    if (settler.next == 0) {
        Call<kRecalculateTerritory>(world, settler.x, settler.y);
    }
    return JobResult::Next;
}

// Purpose: A bowman enters its building and takes a slot at a window.
// 0x4ecc94..0x4ecd50 (job 0x6a)
JobResult BowmanEnter(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    SetJob(settler, J::Job140);
    --BuildingAt(world, settler.building).military.class2_coming;
    OwnTile(ctx).settler = 0;
    settler.next = BuildingAt(world, settler.building).garrison;
    BuildingAt(world, settler.building).garrison = static_cast<std::uint16_t>(ctx.number);
    if (settler.next == 0) {
        Call<kRecalculateTerritory>(world, settler.x, settler.y);
    }
    Call<kEnterBuildingSlot>(world, ctx.number);
    settler.counter = 0;
    return JobResult::Next;
}

// ------------------------------------------------------------------- woodcutter, stonecutter, forester

// The worker goes into its building: +0x19 counts it, it leaves its tile.
void EnterWorkplace(SettlerContext& ctx) {
    ++BuildingAt(ctx.world, ctx.settler.building).kind_bytes[0];  // +0x19
    OwnTile(ctx).settler = 0;
}

// Purpose: The search of the woodcutter (trees, 0x18) and the stonecutter
// (stones, 0x19): 50 entries of the offset table around the building's work
// point, from the settler's cursor (+0x10). After 3000 entries without
// success the player is told once (notification 0x12 / 0x13).
struct CutterSearch {
    std::int32_t step;           // the tree / stone is `step` tiles right and down of the found spot
    bool onMapByIsOnMap;         // woodcutter: IsOnMap; stonecutter: below map_size - 2
    std::uint8_t firstObject, lastObject;
    bool cursorStepsWhenNone;    // woodcutter: cursor + 1 when the first entry matched
    std::uint32_t notification;
    J next;
    std::uint32_t animation;
};

JobResult CutterSearchJob(SettlerContext& ctx, const CutterSearch& kind) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    const Building& start = BuildingAt(world, settler.building);
    if ((start.request_flags & 1) == 0) {
        settler.counter = kWaitTicks;
        return JobResult::Next;
    }
    if (PileAt(world, start.pile_ring).amount > kPileFull) {
        return WaitTen(settler);
    }
    const MapOffset* const offsets = world->offsets + settler.next;
    bool found = false;
    std::uint32_t tries = 0;
    std::uint32_t tx = 0;
    std::uint32_t ty = 0;
    while (!found) {
        const Building& building = BuildingAt(world, settler.building);
        const MapOffset& offset = offsets[tries];
        tx = building.work_x + static_cast<std::uint32_t>(offset.dx);
        ty = building.work_y + static_cast<std::uint32_t>(offset.dy);
        ctx.frame.slot14 = ty;
        const std::uint32_t limit = world->map_size - 2;
        const bool onMap = kind.onMapByIsOnMap ? Call<kIsOnMap>(world, tx, ty) != 0 : tx < limit && ty < limit;
        if (onMap) {
            const Building& now = BuildingAt(world, settler.building);
            const std::uint32_t offsetHere = TileOff(tx, ty);
            const Tile& spot = TileNear(world, offsetHere, kind.step, kind.step);
            const std::uint8_t object = TileAtOffset(world, offsetHere).object;
            if (spot.area == TileAt(world, now.door_x, now.door_y).area && object >= kind.firstObject &&
                object <= kind.lastObject &&
                Call<kCanSettlerEnterTile>(world, tx + kind.step, ty + kind.step, settler.owner, settler.art) != 0) {
                found = true;
            }
        }
        if (!found) {
            ++tries;
        }
        if (tries >= kSearchSteps) {
            break;
        }
    }
    ctx.frame.slot20 = tx;
    settler.next = static_cast<std::uint16_t>(settler.next + tries);
    if (kind.cursorStepsWhenNone && tries == 0) {
        settler.next = static_cast<std::uint16_t>(settler.next + 1);
    }
    if (settler.next >= 3000) {
        settler.next = 0;
        if (settler.tool_type == 0) {  // +0x18: the last search found something
            Call<kQueuePendingEvent>(world, 7, settler.building, 0);
            Call<kNotifyLocalPlayer>(world, settler.owner, kind.notification, settler.x, settler.y);
        }
        settler.tool_type = 0;
    }
    if (!found) {
        return WaitTen(settler);
    }
    OwnTile(ctx).settler = static_cast<std::uint16_t>(ctx.number);
    Call<kSetSettlerTarget>(world, ctx.number, tx + kind.step, ty + kind.step);
    SetJob(settler, kind.next);
    Call<kSetSettlerAnimation>(world, ctx.number, kind.animation);
    settler.tool_type = 1;
    return JobResult::Next;
}

// 0x4ecf82..0x4ed212 (job 0x18)
constexpr CutterSearch kWoodcutterSearch{1, true, kFirstTree, kLastTree, true, 0x12, J::Job30, 5};
// 0x4ed213..0x4ed4a3 (job 0x19)
constexpr CutterSearch kStonecutterSearch{2, false, kFirstStone, kLastStone, false, 0x13, J::Job31, 0x10};

// Purpose: The worker steps out of the building and walks to its spot.
// 0x4ed4a4 (job 0x1e) and 0x4ed4c3 (job 0x1f)
JobResult LeaveBuilding(SettlerContext& ctx, J walk) {
    Call<kMoveSettlerToBuildingTileB>(ctx.world, ctx.number);
    ctx.settler.counter = 0;
    SetJob(ctx.settler, walk);
    return JobResult::Again;
}

// Purpose: The woodcutter at a tree (up-left of it) starts cutting, or
// walks home if the tree is gone.
// 0x4ed4e2..0x4ed53a (job 0x20)
JobResult WoodcutterAtTree(SettlerContext& ctx) {
    Settler& settler = ctx.settler;
    const std::uint8_t object = TileNear(ctx.world, TileOff(settler.x, settler.y), -1, -1).object;
    if (object >= kFirstTree && object <= kLastTree) {
        SetJob(settler, J::Job33);
        Call<kSetSettlerAnimation>(ctx.world, ctx.number, 6);
        settler.food = 0;  // +0x17: cutting state
        return JobResult::Next;
    }
    SetJob(settler, J::Job42);
    return WalkToDoorAgain(ctx);  // 0x4f827f
}

// Purpose: The stonecutter at a stone (two tiles up-left) starts cutting,
// or walks home if the stone is gone.
// 0x4ed53b..0x4ed58d (job 0x22)
JobResult StonecutterAtStone(SettlerContext& ctx) {
    Settler& settler = ctx.settler;
    const std::uint8_t object = TileNear(ctx.world, TileOff(settler.x, settler.y), -2, -2).object;
    if (object >= kFirstStone && object <= kLastStone) {
        SetJob(settler, J::Job35);
        Call<kSetSettlerAnimation>(ctx.world, ctx.number, 0x11);
        settler.food = 0;
        return JobResult::Next;
    }
    SetJob(settler, J::Job43);  // 0x4ed7eb
    return WalkToDoorAgain(ctx);
}

// Purpose: The woodcutter cuts the tree in nine steps (table 0x4f99d0 on
// +0x17): the tree falls (state 1), its object counts on (2..8), and at the
// end the tile is free again and the log is carried home (+0x15 = 3).
// 0x4ed58e..0x4ed7c6 (job 0x21)
JobResult WoodcutterCut(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    const std::uint8_t state = ++settler.food;
    if (state < 1 || state > 9) {
        return JobResult::Next;
    }
    Tile& tree = TileNear(world, TileOff(settler.x, settler.y), -1, -1);
    if (state == 9) {
        Call<kUnblockTile>(world, settler.x - 1u, settler.y - 1u);  // 0x4ed774
        settler.slot = 3;  // carries a log
        SetJob(settler, J::Job42);
        return WalkToDoorAgain(ctx);  // 0x4f82a0
    }
    static constexpr std::uint32_t kAnimations[8] = {7, 7, 7, 8, 9, 0xa, 0xb, 0xc};
    if (state == 1) {
        // The felled tree: (object - 0x44) / 2 + 0x80 (signed division).
        const std::int32_t felled = (static_cast<std::int32_t>(tree.object) - 0x44) / 2;
        tree.object = static_cast<std::uint8_t>(felled + 0x80);
    } else {
        tree.object = static_cast<std::uint8_t>(tree.object + 8);
    }
    Call<kSetSettlerAnimation>(world, ctx.number, kAnimations[state - 1]);
    return JobResult::Next;
}

// Purpose: The stonecutter cuts a piece of the stone (state 1); a stone used
// up (object 0x7f) frees its seven tiles. State 2 carries the stone home.
// 0x4ed7c7..0x4ed92e (job 0x23)
JobResult StonecutterCut(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    const std::uint8_t state = ++settler.food;
    if (state == 2) {
        settler.slot = 2;  // carries a stone
        SetJob(settler, J::Job43);
        return WalkToDoorAgain(ctx);
    }
    if (state != 1) {
        return JobResult::Next;
    }
    Tile& stone = TileNear(world, TileOff(settler.x, settler.y), -2, -2);
    ++stone.object;
    if (TileNear(world, TileOff(settler.x, settler.y), -2, -2).object == kStoneUsedUp) {
        const std::uint32_t x = settler.x;
        const std::uint32_t y = settler.y;
        Call<kUnblockTile>(world, x - 3, y - 3);
        Call<kUnblockTile>(world, x - 2, y - 3);
        Call<kUnblockTile>(world, x - 3, y - 2);
        Call<kUnblockTile>(world, x - 2, y - 2);
        Call<kUnblockTile>(world, x - 1, y - 2);
        Call<kUnblockTile>(world, x - 2, y - 1);
        Call<kUnblockTile>(world, x - 1, y - 1);
    }
    Call<kSetSettlerAnimation>(world, ctx.number, 0x12);
    return JobResult::Next;
}

JobResult GoInside(SettlerContext& ctx, J inside, std::uint32_t animation);

// Purpose: Back home with something (+0x15) the worker puts it down;
// otherwise it goes inside (job 0x66 / 0x67).
// 0x4ed92f (job 0x24; 0x4ed951 is job 0x26) and 0x4eda06 (job 0x27; 0x4eda28 is job 0x29)
JobResult HomeWithGoods(SettlerContext& ctx, J put, std::uint32_t putAnimation, J inside,
                        std::uint32_t insideAnimation) {
    if (ctx.settler.slot != 0) {
        SetJob(ctx.settler, put);
        Call<kSetSettlerAnimation>(ctx.world, ctx.number, putAnimation);
        return JobResult::Next;
    }
    return GoInside(ctx, inside, insideAnimation);
}

// Purpose: The worker puts its log / stone on the output pile.
// 0x4ed971 (job 0x25) and 0x4eda48 (job 0x28)
JobResult PutOnPile(SettlerContext& ctx, Good good, J next, std::uint32_t animation) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    Call<kPileIncAmountComing>(world, BuildingAt(world, settler.building).pile_ring);
    Call<kAddGoodsToPile>(world, BuildingAt(world, settler.building).pile_ring);
    Call<kAddStatistic>(world, settler.owner, Value(good), 1);
    Call<kAddPlayerCounter>(world, settler.owner, Value(good), 1);
    SetJob(settler, next);
    Call<kSetSettlerAnimation>(world, ctx.number, animation);
    settler.slot = 0;
    return JobResult::Next;
}

// Purpose: The forester looks for a spot to plant a tree: 50 entries of
// the offset table around the work point; the spot must be in the door's
// area, empty, unblocked with free neighbours (two rings) and on grass.
// 0x4edadd..0x4eddbd (job 0x1b)
JobResult ForesterSearch(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    if ((BuildingAt(world, settler.building).request_flags & 1) == 0) {
        settler.counter = kWaitTicks;
        return JobResult::Next;
    }
    // Neighbours whose flags must have neither bit 0 nor bit 3 (0x4edc1a..0x4edcd6).
    static constexpr std::int8_t kRing[][2] = {{1, 0},  {-1, 0}, {0, 1},  {0, -1}, {1, 1},  {-1, -1}, {2, 0},
                                               {2, 1},  {2, 2},  {1, 2},  {0, 2},  {-1, 1}, {-2, 0},  {-2, -1},
                                               {-2, -2}, {-1, -2}, {0, -2}, {1, -1}};
    bool found = false;
    std::uint32_t tries = 0;
    std::uint32_t tx = 0;
    std::uint32_t ty = 0;
    while (!found) {
        const Building& building = BuildingAt(world, settler.building);
        const MapOffset& offset = *(world->offsets + settler.next + tries);
        tx = building.work_x + static_cast<std::uint32_t>(offset.dx);
        ty = building.work_y + static_cast<std::uint32_t>(offset.dy);
        ctx.frame.slot14 = ty;
        const std::uint32_t limit = world->map_size - 2;
        if (tx >= 2 && tx < limit && ty >= 2 && ty < limit) {
            const std::uint32_t here = TileOff(tx, ty);
            const Tile& spot = TileAtOffset(world, here);
            bool ok = spot.area == TileAt(world, building.door_x, building.door_y).area && spot.object == 0 &&
                      spot.object_number == 0 && spot.settler == 0 && (spot.flags & 0x0d) == 0;
            for (const auto& d : kRing) {
                ok = ok && (TileNear(world, here, d[0], d[1]).flags & 0x09) == 0;
            }
            ok = ok && (spot.terrain & 0xf0) == 0x10 && spot.terrain != 0x1c;
            found = ok;
        }
        if (!found) {
            ++tries;
        }
        if (tries >= kSearchSteps) {
            break;
        }
    }
    settler.next = static_cast<std::uint16_t>(settler.next + tries);
    ctx.frame.slot20 = tx;
    if (tries == 0) {
        settler.next = static_cast<std::uint16_t>(settler.next + 1);
    }
    if (settler.next >= 1500) {
        settler.next = 0;
    }
    if (!found) {
        return WaitTen(settler);
    }
    if (Call<kCanSettlerEnterTile>(world, tx, ty - 1, settler.owner, settler.art) == 0) {
        return JobResult::Next;
    }
    OwnTile(ctx).settler = static_cast<std::uint16_t>(ctx.number);
    Call<kSetSettlerTarget>(world, ctx.number, tx, ty - 1);
    SetJob(settler, J::Job44);
    Call<kSetSettlerAnimation>(world, ctx.number, 0x1d);
    return JobResult::Next;
}

// Purpose: The forester leaves the building with a sapling.
// 0x4eddbe..0x4edde3 (job 0x2c)
JobResult ForesterLeave(SettlerContext& ctx) {
    Call<kMoveSettlerToBuildingTileB>(ctx.world, ctx.number);
    ctx.settler.slot = 1;
    ctx.settler.counter = 0;
    SetJob(ctx.settler, J::Job45);
    return JobResult::Again;
}

// Purpose: At the spot (one tile below the planting tile) the forester
// checks the tile again and starts planting, or walks home.
// 0x4edde4..0x4edf92 (job 0x2e)
JobResult ForesterAtSpot(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    const std::uint32_t x = settler.x;
    const std::uint32_t y = settler.y;
    const std::uint32_t here = TileOff(x, y);
    const Tile& plant = TileNear(world, here, 0, 1);
    static constexpr std::int8_t kRing[][2] = {{1, 1}, {-1, 1}, {0, 2},  {0, 0},   {1, 2},   {-1, 0}, {2, 1},
                                               {2, 2}, {2, 3},  {1, 3},  {0, 3},   {-1, 2},  {-2, 1}, {-2, 0},
                                               {-2, -1}, {-1, -1}, {0, -1}, {1, 0}};
    bool ok = plant.object == 0 && TileAtOffset(world, here).object_number == 0 && plant.settler == 0 &&
              (plant.flags & 0x0d) == 0;
    for (const auto& d : kRing) {
        ok = ok && (TileNear(world, here, d[0], d[1]).flags & 0x09) == 0;
    }
    if (ok && Call<kIsFreeOfCatapultsNearby>(world, x, y + 1) != 0) {
        const std::uint8_t terrain = TileNear(world, TileOff(settler.x, settler.y), 0, 1).terrain;
        if ((terrain & 0xf0) == 0x10 && terrain != 0x1c) {
            Call<kBlockTile>(world, settler.x, settler.y + 1u);
            SetJob(settler, J::Job47);
            Call<kSetSettlerAnimation>(world, ctx.number, 0x1e);
            settler.food = 0;
            return JobResult::Next;
        }
    }
    SetJob(settler, J::Job48);  // 0x4edfd7
    return WalkToDoor(ctx);     // 0x4f4328
}

// Purpose: Planting: state 1 animation, state 2 puts the sapling (object
// 0x54) and walks home.
// 0x4edf93..0x4edffa (job 0x2f)
JobResult ForesterPlant(SettlerContext& ctx) {
    Settler& settler = ctx.settler;
    const std::uint8_t state = ++settler.food;
    if (state == 1) {
        Call<kSetSettlerAnimation>(ctx.world, ctx.number, 0x1f);
        settler.slot = 0;
        return JobResult::Next;
    }
    if (state != 2) {
        return JobResult::Next;
    }
    TileNear(ctx.world, TileOff(settler.x, settler.y), 0, 1).object = kSapling;
    SetJob(settler, J::Job48);
    return WalkToDoor(ctx);
}

// Purpose: The sawmill worker takes a log from the input pile if there is
// one and the output pile has room.
// 0x4edffb..0x4ee0c0 (job 0x1a)
JobResult SawmillStart(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    if ((BuildingAt(world, settler.building).request_flags & 1) == 0) {
        settler.counter = kWaitTicks;
        return JobResult::Next;
    }
    if ((Call<kBuildingPileHasGoods>(world, settler.building, 1) & 0xff) == 0) {
        return WaitTen(settler);
    }
    const Building& building = BuildingAt(world, settler.building);
    if (PileAt(world, building.pile_ring).amount > kPileFull) {
        return WaitTen(settler);
    }
    Call<kPileIncAmountLeaving>(world, building.piles[0]);
    OwnTile(ctx).settler = static_cast<std::uint16_t>(ctx.number);
    SetJob(settler, J::Job49);
    Call<kSetSettlerAnimation>(world, ctx.number, 0x17);
    return JobResult::Next;
}

JobResult GoInside(SettlerContext& ctx, J inside, std::uint32_t animation) {
    Call<kMoveSettlerToBuildingTileA>(ctx.world, ctx.number);
    SetJob(ctx.settler, inside);
    Call<kSetSettlerAnimation>(ctx.world, ctx.number, animation);
    return JobResult::Next;
}

}  // namespace

JobResult RunBuildingJobs(SettlerContext& ctx) {
    CGameData* world = ctx.world;
    Settler& settler = ctx.settler;
    switch (static_cast<J>(ctx.job)) {
    case J::DiggerDig: return DiggerDig(ctx);
    case J::DiggerLevel: return DiggerLevel(ctx);
    case J::Job8:
    case J::Job9:
        SetJob(settler, J::DiggerLevel);  // 0x4ebab8
        settler.counter = 0;
        return JobResult::Next;
    case J::RecruitAtBarracks: return RecruitAtBarracks(ctx);
    case J::CarrierTakeTool: return CarrierTakeTool(ctx);
    case J::CarrierAtPile: return CarrierBend(ctx, J::CarrierPickUp);
    case J::CarrierPickUp: return CarrierPickUp(ctx);
    case J::CarrierLeavePile: return CarrierLeavePile(ctx);
    case J::Job77: return CarrierAtTradePile(ctx);
    case J::CarrierAtBuilding: return CarrierBend(ctx, J::CarrierDeliver);
    case J::CarrierDeliver: return CarrierDeliver(ctx);
    case J::Job79: return CarrierAtTradeBuilding(ctx);
    case J::BuilderStart: return BuilderStart(ctx);
    case J::Job81:
        Call<kClearSettlerJob>(world, ctx.number);  // 0x4ec67f
        return JobResult::Next;
    case J::BuilderBuild: return BuilderBuild(ctx);
    case J::Job16:
        return SoldierEnter(ctx, J::Job23, BuildingAt(world, settler.building).military.class1_coming);
    case J::Job106: return BowmanEnter(ctx);
    case J::Job139:
        return SoldierEnter(ctx, J::Job141, BuildingAt(world, settler.building).military.class3_coming);
    case J::Job140:
        // 0x4ece16: a bowman in its building; it waits while it doesn't shoot.
        if (Call<kUpdateSoldier>(world, ctx.number) == 0) {
            settler.counter = 0;
        }
        return JobResult::Next;
    case J::Job19:  // 0x4ece32: the woodcutter goes in
        SetJob(settler, J::Job24);
        EnterWorkplace(ctx);
        settler.next = 0;
        settler.counter = 0;
        return JobResult::Next;
    case J::Job22:  // 0x4ecf66: the forester goes in (0x4ece3b)
        SetJob(settler, J::Job27);
        EnterWorkplace(ctx);
        settler.next = 0;
        settler.counter = 0;
        return JobResult::Next;
    case J::Job20:  // 0x4ecea1: the stonecutter goes in
        SetJob(settler, J::Job25);
        EnterWorkplace(ctx);
        settler.next = 0;
        settler.counter = 0;
        return JobResult::Again;
    case J::Job102:  // 0x4ece93 -> 0x4f73bb: back inside, search again
        SetJob(settler, J::Job24);
        OwnTile(ctx).settler = 0;
        settler.counter = 0;
        return JobResult::Again;
    case J::Job101:  // 0x4ecf74 -> 0x4f73bb
        SetJob(settler, J::Job27);
        OwnTile(ctx).settler = 0;
        settler.counter = 0;
        return JobResult::Again;
    case J::Job103:  // 0x4ecf09
        SetJob(settler, J::Job25);
        OwnTile(ctx).settler = 0;
        settler.counter = 0;
        return JobResult::Next;
    case J::Job21:  // 0x4ecf3c -> 0x4f8f44: the sawmill worker goes in
    case J::Job56:  // 0x4ecf4a: smelter
    case J::Job66:  // 0x4ecf58: smelter
        SetJob(settler, static_cast<J>(ctx.job) == J::Job21   ? J::Job26
                        : static_cast<J>(ctx.job) == J::Job56 ? J::Job65
                                                              : J::Job75);
        ++BuildingAt(world, settler.building).kind_bytes[0];  // +0x19
        OwnTile(ctx).settler = 0;                             // 0x4ecf12
        settler.counter = 0;
        return JobResult::Next;
    case J::Job24: return CutterSearchJob(ctx, kWoodcutterSearch);
    case J::Job25: return CutterSearchJob(ctx, kStonecutterSearch);
    case J::Job30: return LeaveBuilding(ctx, J::Job28);
    case J::Job31: return LeaveBuilding(ctx, J::Job29);
    case J::Job32: return WoodcutterAtTree(ctx);
    case J::Job33: return WoodcutterCut(ctx);
    case J::Job34: return StonecutterAtStone(ctx);
    case J::Job35: return StonecutterCut(ctx);
    case J::Job36: return HomeWithGoods(ctx, J::Job37, 0xd, J::Job102, 4);
    case J::Job38: return GoInside(ctx, J::Job102, 4);
    case J::Job37: return PutOnPile(ctx, Good::Log, J::Job38, 0xe);
    case J::Job39: return HomeWithGoods(ctx, J::Job40, 0x13, J::Job103, 0xf);
    case J::Job41: return GoInside(ctx, J::Job103, 0xf);
    case J::Job40: return PutOnPile(ctx, Good::Stone, J::Job41, 0x14);
    case J::Job27: return ForesterSearch(ctx);
    case J::Job44: return ForesterLeave(ctx);
    case J::Job46: return ForesterAtSpot(ctx);
    case J::Job47: return ForesterPlant(ctx);
    case J::Job26: return SawmillStart(ctx);
    default: return JobResult::Next;  // not a job of this family (kJobFamily)
    }
}

}  // namespace s3re::game::settler_jobs

// ---------------------------------------------------------------------------
// Tests (harness: settler_jobs_test.h)
// ---------------------------------------------------------------------------

namespace s3re::game {

namespace {

using settler_jobs::testing::CaseSetup;
using settler_jobs::testing::FamilyTest;
using J = SettlerJob;
using T = SettlerType;

constexpr J kBuildingJobs[] = {
    J::DiggerDig, J::DiggerLevel, J::Job8,     J::Job9,     J::RecruitAtBarracks, J::CarrierTakeTool,
    J::CarrierAtPile, J::CarrierPickUp, J::CarrierLeavePile, J::Job77, J::CarrierAtBuilding, J::CarrierDeliver,
    J::Job79,     J::BuilderStart, J::Job81,  J::BuilderBuild, J::Job16,  J::Job106,   J::Job139,  J::Job140,
    J::Job19,     J::Job102,   J::Job20,    J::Job103,   J::Job21,    J::Job56,    J::Job66,    J::Job22,
    J::Job101,    J::Job24,    J::Job25,    J::Job30,    J::Job31,    J::Job32,    J::Job34,    J::Job33,
    J::Job35,     J::Job36,    J::Job38,    J::Job37,    J::Job39,    J::Job41,    J::Job40,    J::Job27,
    J::Job44,     J::Job46,    J::Job47,    J::Job26};
static_assert(sizeof(kBuildingJobs) / sizeof(kBuildingJobs[0]) == 48);

constexpr T kBuildingTypes[] = {T::Carrier,    T::Digger,  T::Builder,  T::Woodcutter, T::Stonecutter,
                                T::Sawmiller,  T::Forester, T::Swordsman1, T::Bowman1, T::Pikeman1,
                                T::Swordsman2, T::Bowman3, T::Smelter};

// Makes the searches of the handlers succeed now and then: small offset
// table entries, tiles around the settler's building that pass the tests,
// a site grid and layout the diggers and builders can use, matching goods.
// The tiles around the building are never blocked: the original hangs on
// purpose after "Digger trouble".
void CustomizeBuilding(CaseSetup& setup, std::uint32_t n) {
    auto& rng = setup.rng();
    const auto bx = static_cast<std::uint16_t>(24 + rng.Below(320));
    const auto by = static_cast<std::uint16_t>(24 + rng.Below(320));
    const auto wx = static_cast<std::uint16_t>(bx + rng.Range(-3, 3));
    const auto wy = static_cast<std::uint16_t>(by + rng.Range(-3, 3));
    const auto dx = static_cast<std::uint16_t>(bx + rng.Range(-2, 2));
    const auto dy = static_cast<std::uint16_t>(by + rng.Range(-2, 2));
    const auto levelling = static_cast<std::uint16_t>(rng.Below(30));
    const std::uint32_t buildupPick = rng.Below(8);
    const auto buildup = static_cast<std::uint8_t>(buildupPick < 2   ? 0
                                                   : buildupPick < 4 ? 1
                                                   : buildupPick < 5 ? 2
                                                   : buildupPick < 7 ? 3
                                                   : rng.Chance(1, 4) ? 50
                                                                     : 4 + rng.Below(48));
    const auto requestFlags = static_cast<std::uint8_t>(rng.Next32());
    const bool clearGrid = rng.Chance(1, 8);
    const bool clearAround = rng.Chance(1, 2);
    const bool sameArea = rng.Chance(3, 4);
    const auto area = static_cast<std::uint16_t>(rng.Below(settler_jobs::testing::kAreas));
    const auto slot = static_cast<std::uint8_t>(rng.Chance(2, 3) ? 1 + rng.Below(3) : rng.Next32());
    const std::uint32_t goodPick = rng.Below(4);
    const bool treeNear = rng.Chance(1, 2);
    const auto tree = static_cast<std::uint8_t>(0x44 + rng.Below(15));
    const auto stone = static_cast<std::uint8_t>(rng.Chance(1, 4) ? 0x7e : 0x73 + rng.Below(12));
    const bool levelDone = rng.Chance(1, 3);
    const bool tradePile = rng.Chance(1, 2);
    const auto sc = static_cast<std::uint16_t>(rng.Chance(4, 5) ? 1 + rng.Below(3) : rng.Below(5));
    const auto toolType = static_cast<std::uint8_t>(rng.Below(4));
    const std::uint32_t foodPick = rng.Below(4);
    const auto food = static_cast<std::uint8_t>(foodPick == 0 ? 0 : foodPick == 1 ? rng.Next32() : rng.Below(12));
    const std::uint32_t nextPick = rng.Below(8);
    const auto cursor = static_cast<std::uint16_t>(nextPick == 0 ? 2960 + rng.Below(40) : nextPick == 1 ? 1460 + rng.Below(40) : 0);
    const bool emptyInputs[3] = {rng.Chance(1, 4), rng.Chance(1, 4), rng.Chance(1, 4)};
    const bool roomInRing = rng.Chance(2, 3);
    const auto ringAmount = static_cast<std::uint8_t>(rng.Below(8));
    const bool missingMatches = rng.Chance(1, 2);
    const bool nothingMissing = rng.Chance(1, 6);
    const bool slotIsGood = rng.Chance(1, 3);
    const bool notOrdered = rng.Chance(1, 2);
    const bool noGarrison = rng.Chance(1, 4);
    const bool shownSite = rng.Chance(1, 10);
    const bool nearBuilding = rng.Chance(1, 2);
    const auto nearX = static_cast<std::uint16_t>(bx + rng.Range(-12, 12));
    const auto nearY = static_cast<std::uint16_t>(by + rng.Range(-12, 12));
    std::int32_t offsets[300][2];
    for (auto& o : offsets) {
        o[0] = rng.Range(-4, 4);
        o[1] = rng.Range(-4, 4);
    }
    bool isDigger = false;
    std::uint32_t gridOwner = 0;
    setup.Both([&](CGameData* w) {
        using namespace settler_jobs;
        Settler& s = SettlerAt(w, n);
        if (shownSite) {
            s.building = 7;  // the site the UI shows (g_selectedBuilding in the test)
        }
        Building& b = BuildingAt(w, s.building);
        b.x = bx;
        b.y = by;
        b.work_x = wx;
        b.work_y = wy;
        b.door_x = dx;
        b.door_y = dy;
        b.levelling_slot = levelling;
        b.request_flags = requestFlags;
        b.buildup = buildup;
        s.slot = slot;
        s.saved_counter = sc;
        s.tool_type = toolType;
        s.food = food;
        if (nearBuilding) {
            s.x = nearX;
            s.y = nearY;
        }
        // Small offset table entries (the builder's door search has no limit).
        for (std::uint32_t k = 0; k < 300; ++k) {
            w->offsets[k].dx = offsets[k][0];
            w->offsets[k].dy = offsets[k][1];
        }
        for (std::int32_t ox = -17; ox <= 17; ++ox) {
            for (std::int32_t oy = -17; oy <= 17; ++oy) {
                Tile& t = w->tiles[bx + ox][by + oy];
                t.flags &= static_cast<std::uint8_t>(~layout::kTileBlocked);
                if (sameArea) {
                    t.area = area;
                }
                if (clearAround && ox >= -8 && ox <= 8 && oy >= -8 && oy <= 8) {
                    t.flags &= 0xf0;
                    if (((ox + oy) & 3) == 0) {
                        t.object = 0;
                        t.object_number = 0;
                        t.settler = 0;
                        t.terrain = 0x10;
                    } else if (((ox + oy) & 3) == 1) {
                        t.object = treeNear ? tree : stone;
                    }
                }
            }
        }
        if (cursor != 0) {
            s.next = cursor;
        }
        for (int k = 0; k < 3; ++k) {
            if (emptyInputs[k]) {
                PileAt(w, b.piles[k]).amount = 0;
            }
        }
        if (roomInRing) {
            PileAt(w, b.pile_ring).amount = ringAmount;
        }
        if (missingMatches) {
            b.site.boards_missing = PileAt(w, b.piles[0]).amount;
            b.site.stones_missing = PileAt(w, b.piles[1]).amount;
        }
        if (nothingMissing) {
            b.site.boards_missing = 0;
            b.site.stones_missing = 0;
        }
        if (slotIsGood && b.buildup == 0) {
            s.slot = PileAt(w, b.piles[goodPick % 3]).good;
        }
        if (noGarrison) {
            b.garrison = 0;
        }
        if (notOrdered) {
            layout::Trade& trade = *(w->trades + BuildingAt(w, PileAt(w, w->tiles[s.x][s.y].object_number).prev).trade);
            (&trade.ordered[0])[s.slot] = 0;
        }
        isDigger = JobOf(s) == J::DiggerDig;
        gridOwner = s.owner % 20;
        if (levelDone) {
            s.food = w->tiles[s.x][s.y].height;
        }
        // Trees and stones where the cutters look; the trade pile kind.
        if (treeNear) {
            w->tiles[s.x - 1][s.y - 1].object = tree;
        }
        w->tiles[s.x - 2][s.y - 2].object = stone;
        if (tradePile) {
            PileAt(w, w->tiles[s.x][s.y].object_number).art = 0xfc;
        }
        // A carried good the building wants.
        if (goodPick < 3) {
            s.good = static_cast<std::uint8_t>(g_buildingInputGoods[b.art][goodPick]);
        } else if (b.buildup != 0) {
            s.good = static_cast<std::uint8_t>(1 + (s.good & 1));
        }
        // Builders in the building stage need a layout with spots; the
        // first two spots are free.
        const std::uint32_t* list = &g_buildingLayouts[0][0] + (b.race * 57 + b.art) * 600;
        std::uint32_t marker = 0;
        while (marker < 597 && list[marker] != 0x80000005) {
            ++marker;
        }
        if (marker >= 597) {
            if (b.buildup >= 3) {
                b.buildup = 1;
            }
        } else {
            const std::uint32_t* spot = list + marker + 1;
            for (int k = 0; k < 2; ++k) {
                if (spot[3 * k] + 20 < 40 && spot[3 * k + 1] + 20 < 40) {
                    w->tiles[bx + spot[3 * k]][by + spot[3 * k + 1]].flags &= static_cast<std::uint8_t>(~layout::kTileFlag02);
                }
            }
        }
    });
    // A digger's levelling grid: emptied now and then (the site is done),
    // else a few cells with wanted heights (otherwise the other handlers'
    // writes would empty every grid over the cases).
    if (isDigger) {
        std::uint8_t grid[31 * 31] = {};
        if (!clearGrid) {
            const std::uint32_t cells = 1 + rng.Below(40);
            for (std::uint32_t k = 0; k < cells; ++k) {
                grid[rng.Below(31 * 31)] = static_cast<std::uint8_t>(1 + rng.Below(255));
            }
        }
        setup.Both([&](CGameData* w) {
            std::uint8_t* cell = &w->site_cache[gridOwner][levelling].grid[0];
            for (std::uint32_t k = 0; k < 31 * 31; ++k) {
                cell[k] = grid[k];
            }
        });
    }
}

// BuildingPileHasGoods: only its low byte counts (the original tests AL).
bool BuildingStubReturn(std::uintptr_t callee, std::uint32_t h, const std::uint32_t*, CGameData*, std::uint32_t& eax) {
    if (callee == settler_jobs::kBuildingPileHasGoods) {
        eax = (h & 0xffffff00u) | ((h >> 8) & 1u);
        return true;
    }
    return false;
}

FamilyTest BuildingFamily() {
    FamilyTest family{kBuildingJobs, sizeof(kBuildingJobs) / sizeof(kBuildingJobs[0])};
    family.types = kBuildingTypes;
    family.typeCount = sizeof(kBuildingTypes) / sizeof(kBuildingTypes[0]);
    family.customize = &CustomizeBuilding;
    family.stubReturn = &BuildingStubReturn;
    family.minCompared = 1000;
    return family;
}

// Wrong: a digger sent to a tile remembers a height one off.
std::uint32_t __fastcall WrongDiggerHeight(CGameData* world, void* edx) {
    const std::uint32_t result = CGameData_UpdateSettlers(world, edx);
    for (std::uint32_t n = 1; n <= world->settler_high_water; ++n) {
        if (JobOf(world->settlers[n]) == J::DiggerWalkToSpot) {
            world->settlers[n].food ^= 1;
        }
    }
    return result;
}

// Wrong: the woodcutter's search cursor advances one entry too far when
// it found nothing.
std::uint32_t __fastcall WrongSearchCursor(CGameData* world, void* edx) {
    const std::uint32_t result = CGameData_UpdateSettlers(world, edx);
    for (std::uint32_t n = 1; n <= world->settler_high_water; ++n) {
        layout::Settler& s = world->settlers[n];
        if (JobOf(s) == J::Job24 && s.counter == 10) {
            ++s.next;
        }
    }
    return result;
}

}  // namespace

S3RE_TEST(CGameData_UpdateSettlers_Building_MatchesOriginal) {
    // The site whose material counts the UI shows (job 0x50 refreshes it).
    const std::int32_t savedShown = g_selectedBuilding;
    g_selectedBuilding = 7;
    settler_jobs::testing::CompareUpdateSettlers(ctx, &CGameData_UpdateSettlers, BuildingFamily(), 4000);
    g_selectedBuilding = savedShown;
}

S3RE_TEST(CGameData_UpdateSettlers_Building_ComparisonDetectsWrongDigger) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    FamilyTest family = BuildingFamily();
    family.minCompared = 1;
    settler_jobs::testing::CompareUpdateSettlers(probe, &WrongDiggerHeight, family, 1000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CGameData_UpdateSettlers_Building_ComparisonDetectsWrongCursor) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    FamilyTest family = BuildingFamily();
    family.minCompared = 1;
    settler_jobs::testing::CompareUpdateSettlers(probe, &WrongSearchCursor, family, 1000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
