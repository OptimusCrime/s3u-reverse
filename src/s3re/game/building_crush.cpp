// Removing buildings and construction sites (callees of CrushBuilding
// 0x4d9250 and its helpers).
//
// GameWorld::ClearBuildingFootprint (0x0051aae0): (building) thiscall,
//   ret 4. With (x, y) the building's position, type +0x6 and race +0x7:
//   tile (x, y).object = 0; then in the building's layout (S3.EXE 0x6aa170,
//   see building_sites.cpp), after the 0x80000000 marker: for each (dx, dy)
//   up to 0x80000001 (the footprint), tile (x+dx, y+dy): flag 0x08 cleared,
//   object 0x71 (a stake) -> 0, then UnblockTile(x+dx, y+dy); after it, up
//   to 0x80000003, skipping a 0x80000002 marker (the rest of the footprint
//   and the border): flag 0x08 cleared and stake 0x71 -> 0 only. Then, if
//   the building is still in buildup stage 1 (+0x21 == 1, a construction
//   site being levelled), every non-zero cell (i, j) of its levelling grid
//   (site cache of its owner, slot +0x22: grid[j * 31 + i]) clears flag
//   0x04 of tile (x - 15 + j, y - 15 + i), and the slot is freed (valid =
//   0). Positions are not range-checked (32-bit tile arithmetic as in the
//   original). EAX not used by the only caller, CrushBuilding.
// GameWorld::BlockTileAndMoveSettler (0x004dbb10): (x, y) thiscall, ret 8.
//   If a settler stands on the tile: ResetSettlerToIdle(settler), tile
//   settler word = 0, BlockTile(x, y), object = 0, PlaceSettlerNear(settler,
//   x, y); else BlockTile(x, y), object = 0. EAX not used by its caller.
// GameWorld::BlockBuildingLine (0x004dbb90): (building) thiscall, ret 4.
//   Only for a finished building (+0x21 == 0) whose word +0x22 is a
//   direction (not 0xffff): from the work tile (+0x30/+0x32) along that
//   direction (NeighbourX/Y), BlockTileAndMoveSettler on every tile until
//   one of terrain 0x30 (probably the shore or shallow water); then +0x22 =
//   0xffff. Probably the line a shipyard keeps free for its ship (low
//   confidence). Callers CrushBuilding and ExecuteCommands; EAX unused.
// The three "Drop...Pile" functions (pile) thiscall, ret 4, called by
// RemoveBuildingStorage 0x4de430 for a building's input, storage and output
// piles: settlers bound to the pile get ResetSettlerToIdle, the pile is
// taken off its tile (RemovePileFromTile) and, if it held goods, they are
// put back on the ground there as a new dropped pile (CreatePile(x, y, good,
// amount, 0xfe)). Settler scans run over 1..high water (re-read), skip
// records without owner, and test the job +0xe and either the building
// +0x22 (== pile) or the target +0x24/+0x26 (== the pile's position).
// GameWorld::DropInputPile (0x00504a00): if units are coming (high nibble of
//   +0x2): carriers with jobs 0x4c..0x4f bringing to it are reset. DebugLog
//   "TROUBLE: cannot find all people that wanted to bring goods" (always).
//   If units are leaving (+0x2 != 0): ships' jobs 0x133/0x134 targeting it are
//   reset. Kind +0x4 = 0xfe, amount = 0 (after reading good and amount),
//   RemovePileFromTile, CreatePile if the amount was not 0.
// GameWorld::DropStoragePile (0x00504b70): area = the pile tile's area. If
//   units are coming (+0x3): carriers with jobs 0x4c..0x4f bringing to it are
//   reset, the count goes down per reset, and if some are left DebugLog
//   "TROUBLE: ...". If units are leaving: fetchers (jobs 0xa, 0xb, 0xa9,
//   0x4c, 0x4d, 0x68, 0x69) targeting it are reset. Then amount times
//   PileIncAmountLeaving + TakeGoodFromPile (the amount read before); if the
//   good byte is not 0xff: DebugLog "RemoveStorage: Pushing pile in
//   EmptyList!" and MakePileEmpty. If kind, amount and coming are then all 0,
//   the pile is unlinked from its area's empty list (+0x8). Kind = 0xfe,
//   RemovePileFromTile, CreatePile with the amount read before.
// GameWorld::DropOutputPile (0x00504e20): an empty pile (amount 0) is only
//   taken off its tile. Else: if units are leaving, fetchers targeting it are
//   reset; then amount (re-read) times PileIncAmountLeaving +
//   TakeGoodFromPile, RemovePileFromTile and CreatePile(x, y, good, amount,
//   0xfe).
// EAX of all three is not used by RemoveBuildingStorage.

#include "game/debug_log.h"
#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace s3re::game {

namespace {

using layout::Building;
using layout::PlayerSiteCache;
using layout::Tile;

constexpr std::uintptr_t kLayouts = 0x006aa170;
constexpr std::uint32_t kLayoutBytes = 2400;
constexpr std::uint32_t kLayoutTypes = 57;
constexpr std::uint32_t kMarkFootprint = 0x80000000;
constexpr std::uint32_t kMarkFootprintEnd = 0x80000001;
constexpr std::uint32_t kMarkBorder = 0x80000002;
constexpr std::uint32_t kMarkEnd = 0x80000003;

constexpr std::uint8_t kObjectStake = 0x71;
constexpr std::uint8_t kTileFootprint = 0x08;  // Tile::flags: part of a building
constexpr std::uint8_t kTileLevelling = 0x04;  // Tile::flags: being levelled
constexpr std::uint32_t kGrid = 31;
constexpr std::uint32_t kGridCentre = 15;
constexpr std::uint32_t kSlotsPerPlayer = 30;

Tile& TileAt(GameWorld* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

const std::uint32_t* Layout(std::uint32_t race, std::uint32_t type) {
    return reinterpret_cast<const std::uint32_t*>(kLayouts + (type + race * kLayoutTypes) * kLayoutBytes);
}

// Clears the footprint mark and a stake on one tile.
void ClearFootprintTile(GameWorld* world, std::uint32_t x, std::uint32_t y) {
    TileAt(world, x, y).flags &= static_cast<std::uint8_t>(~kTileFootprint);
    Tile& tile = TileAt(world, x, y);
    if (tile.object == kObjectStake) {
        tile.object = 0;
    }
}

// Callees reached through their S3.EXE addresses: verified ones (Detours
// sends the call to our replacement, behind its counting thunk) whose effects
// the tests replace by recording stubs, and those of other work packages.
using TileFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);
using SettlerFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t settler);
using PileFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t pile);
constexpr std::uintptr_t kUnblockTile = 0x0051b770;         // GameWorld::UnblockTile (area_split.cpp)
constexpr std::uintptr_t kBlockTile = 0x0051b6f0;           // GameWorld::BlockTile (area_split.cpp)
constexpr std::uintptr_t kResetSettlerToIdle = 0x0050cf50;  // package B
constexpr std::uintptr_t kTakeGoodFromPile = 0x00505820;    // package B

void ResetSettlerToIdle(GameWorld* world, std::uint32_t settler) {
    reinterpret_cast<SettlerFn>(kResetSettlerToIdle)(world, nullptr, settler);
}

void TakeGoodFromPile(GameWorld* world, std::uint32_t pile) {
    reinterpret_cast<PileFn>(kTakeGoodFromPile)(world, nullptr, pile);
}

constexpr std::uint8_t kFree = 0xff;
constexpr std::uint8_t kPileDropped = 0xfe;
constexpr std::uint8_t kGoodNone = 0xff;
constexpr std::uint8_t kLineEndTerrain = 0x30;  // Tile::terrain that ends the line (shore?)
constexpr std::uint16_t kNoDirection = 0xffff;

constexpr std::uintptr_t kTroubleMessage = 0x006a6060;    // "TROUBLE: cannot find all people that wanted..."
constexpr std::uintptr_t kEmptyListMessage = 0x006a609c;  // "RemoveStorage: Pushing pile in EmptyList!"

// Carriers bringing goods to a building pile.
bool IsDeliveringJob(std::uint16_t job) {
    return job == 0x4c || job == 0x4d || job == 0x4e || job == 0x4f;
}

// Settlers on their way to take goods from a pile (its position is their target).
bool IsFetchingJob(std::uint16_t job) {
    return job == 0x0a || job == 0x0b || job == 0xa9 || job == 0x4c || job == 0x4d || job == 0x68 || job == 0x69;
}

// Resets every settler with an owner for which `match` is true.
template <typename Match>
void ResetSettlersWhere(GameWorld* world, Match match) {
    for (std::uint32_t n = 1; n <= world->settler_high_water; ++n) {
        const layout::Settler& settler = world->settlers[n];
        if (settler.owner != kFree && match(settler)) {
            ResetSettlerToIdle(world, n);
        }
    }
}

// Resets the settlers about to fetch goods from the pile.
void ResetFetchers(GameWorld* world, const layout::Pile& pile) {
    ResetSettlersWhere(world, [&](const layout::Settler& s) {
        return IsFetchingJob(s.job) && s.target_x == pile.x && s.target_y == pile.y;
    });
}

}  // namespace

using BuildingFn = void(__fastcall*)(GameWorld* world, void* edx, std::uint32_t building);
using PositionFn = void(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);
using DropPileFn = void(__fastcall*)(GameWorld* world, void* edx, std::uint32_t pile);

std::uint32_t __fastcall GameWorld_PlaceSettlerNear(GameWorld* world, void* edx, std::uint32_t settler, std::uint32_t x,
                                                    std::uint32_t y);
std::uint32_t __fastcall GameWorld_RemovePileFromTile(GameWorld* world, void* edx, std::uint32_t pile);
std::uint32_t __fastcall GameWorld_MakePileEmpty(GameWorld* world, void* edx, std::uint32_t pile);

// Purpose: Takes a building (or construction site) off the map: clears its
// footprint marks and stakes, unblocks the tiles it stood on and, for a site
// still being levelled, the levelling marks and its slot in the site cache.
void __fastcall GameWorld_ClearBuildingFootprint(GameWorld* world, void*, std::uint32_t number) {
    Building& building = world->buildings[number];
    const std::uint32_t x = building.x;
    const std::uint32_t y = building.y;
    TileAt(world, x, y).object = 0;
    const std::uint32_t* p = Layout(building.race, building.art);
    while (*p++ != kMarkFootprint) {
    }
    for (; *p != kMarkFootprintEnd; p += 2) {
        const std::uint32_t tx = x + p[0];
        const std::uint32_t ty = y + p[1];
        ClearFootprintTile(world, tx, ty);
        reinterpret_cast<TileFn>(kUnblockTile)(world, nullptr, tx, ty);
    }
    ++p;
    while (*p != kMarkEnd) {
        if (*p == kMarkBorder) {
            ++p;
        }
        ClearFootprintTile(world, x + p[0], y + p[1]);
        p += 2;
    }
    if (building.buildup != 1) {
        return;
    }
    for (std::uint32_t i = 0; i < kGrid; ++i) {
        for (std::uint32_t j = 0; j < kGrid; ++j) {
            const PlayerSiteCache& cache = world->site_cache[0][building.owner * kSlotsPerPlayer + building.unk22];
            if (cache.grid[j * kGrid + i] != 0) {
                TileAt(world, building.x + j - kGridCentre, building.y + i - kGridCentre).flags &=
                    static_cast<std::uint8_t>(~kTileLevelling);
            }
        }
    }
    world->site_cache[0][building.owner * kSlotsPerPlayer + building.unk22].valid = 0;
}

// Purpose: Blocks a tile for a building, removing its landscape object and
// sending a settler standing there to the nearest free tile.
void __fastcall GameWorld_BlockTileAndMoveSettler(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    Tile& tile = TileAt(world, x, y);
    const std::uint32_t settler = tile.settler;
    if (settler != 0) {
        ResetSettlerToIdle(world, settler);
        tile.settler = 0;
        reinterpret_cast<TileFn>(kBlockTile)(world, nullptr, x, y);
        tile.object = 0;
        GameWorld_PlaceSettlerNear(world, nullptr, settler, x, y);
    } else {
        reinterpret_cast<TileFn>(kBlockTile)(world, nullptr, x, y);
        tile.object = 0;
    }
}

// Purpose: Blocks the straight line of tiles a finished building keeps from
// its work tile in one direction (probably a shipyard's slipway) and forgets
// the direction.
void __fastcall GameWorld_BlockBuildingLine(GameWorld* world, void*, std::uint32_t number) {
    Building& building = world->buildings[number];
    if (building.buildup != 0 || building.unk22 == kNoDirection) {
        return;
    }
    const std::uint32_t direction = building.unk22;
    std::uint32_t x = building.work_x;
    std::uint32_t y = building.work_y;
    while (TileAt(world, x, y).terrain != kLineEndTerrain) {
        GameWorld_BlockTileAndMoveSettler(world, nullptr, x, y);
        x = NeighbourX(x, direction);
        y = NeighbourY(y, direction);
    }
    building.unk22 = kNoDirection;
}

// Purpose: Gives up a building's input pile: carriers bringing goods to it
// and ships loading from it stop, and its goods are left on the ground.
void __fastcall GameWorld_DropInputPile(GameWorld* world, void*, std::uint32_t number) {
    layout::Pile& pile = world->piles[number];
    if ((pile.leaving >> 4) != 0) {
        ResetSettlersWhere(world, [&](const layout::Settler& s) {
            return s.building == number && IsDeliveringJob(s.job);
        });
    }
    GameDebugLog(kTroubleMessage);
    if (pile.leaving != 0) {
        ResetSettlersWhere(world, [&](const layout::Settler& s) {
            return (s.job == 0x133 || s.job == 0x134) && s.target_x == pile.x && s.target_y == pile.y;
        });
    }
    const std::uint32_t x = pile.x;
    const std::uint32_t y = pile.y;
    const std::uint32_t good = pile.good;
    const std::uint32_t amount = pile.amount;
    pile.art = kPileDropped;
    pile.amount = 0;
    GameWorld_RemovePileFromTile(world, nullptr, number);
    if (amount != 0) {
        GameWorld_CreatePile(world, nullptr, x, y, good, amount, kPileDropped);
    }
}

// Purpose: Gives up a storage pile of a building: deliveries to it and
// fetches from it are cancelled and its goods are left on the ground.
void __fastcall GameWorld_DropStoragePile(GameWorld* world, void*, std::uint32_t number) {
    layout::Pile& pile = world->piles[number];
    const std::uint32_t area = TileAt(world, pile.x, pile.y).area;
    std::uint32_t coming = pile.coming;
    if (coming != 0) {
        for (std::uint32_t n = 1; n <= world->settler_high_water; ++n) {
            const layout::Settler& settler = world->settlers[n];
            if (settler.owner != kFree && settler.building == number && IsDeliveringJob(settler.job)) {
                ResetSettlerToIdle(world, n);
                --coming;
            }
        }
        if (coming != 0) {
            GameDebugLog(kTroubleMessage);
        }
    }
    if (pile.leaving != 0) {
        ResetFetchers(world, pile);
    }
    const std::uint32_t amount = pile.amount;
    const std::uint32_t good = pile.good;
    const std::uint32_t x = pile.x;
    const std::uint32_t y = pile.y;
    for (std::uint32_t k = amount; k != 0; --k) {
        GameWorld_PileIncAmountLeaving(world, nullptr, number);
        TakeGoodFromPile(world, number);
    }
    if (pile.good != kGoodNone) {
        GameDebugLog(kEmptyListMessage);
        GameWorld_MakePileEmpty(world, nullptr, number);
    }
    if (pile.art == 0 && pile.amount == 0 && pile.coming == 0) {
        if (pile.prev != 0) {
            world->piles[pile.prev].next = pile.next;
        } else {
            world->areas[area].pile_list = pile.next;
        }
        if (pile.next != 0) {
            world->piles[pile.next].prev = pile.prev;
        }
    }
    pile.art = kPileDropped;
    GameWorld_RemovePileFromTile(world, nullptr, number);
    if (amount != 0) {
        GameWorld_CreatePile(world, nullptr, x, y, good, amount, kPileDropped);
    }
}

// Purpose: Gives up a building's output pile: settlers about to fetch from it
// stop, and its goods are left on the ground.
void __fastcall GameWorld_DropOutputPile(GameWorld* world, void*, std::uint32_t number) {
    layout::Pile& pile = world->piles[number];
    if (pile.amount == 0) {
        GameWorld_RemovePileFromTile(world, nullptr, number);
        return;
    }
    if (pile.leaving != 0) {
        ResetFetchers(world, pile);
    }
    const std::uint32_t x = pile.x;
    const std::uint32_t y = pile.y;
    const std::uint32_t good = pile.good;
    const std::uint32_t amount = pile.amount;
    for (std::uint32_t k = amount; k != 0; --k) {
        GameWorld_PileIncAmountLeaving(world, nullptr, number);
        TakeGoodFromPile(world, number);
    }
    GameWorld_RemovePileFromTile(world, nullptr, number);
    GameWorld_CreatePile(world, nullptr, x, y, good, amount, kPileDropped);
}

S3RE_HOOK(0x0051aae0, GameWorld_ClearBuildingFootprint, BuildingFn, Original_GameWorld_ClearBuildingFootprint,
          GameWorld_ClearBuildingFootprint);
S3RE_HOOK(0x004dbb10, GameWorld_BlockTileAndMoveSettler, PositionFn, Original_GameWorld_BlockTileAndMoveSettler,
          GameWorld_BlockTileAndMoveSettler);
S3RE_HOOK(0x004dbb90, GameWorld_BlockBuildingLine, BuildingFn, Original_GameWorld_BlockBuildingLine,
          GameWorld_BlockBuildingLine);
S3RE_HOOK(0x00504a00, GameWorld_DropInputPile, DropPileFn, Original_GameWorld_DropInputPile, GameWorld_DropInputPile);
S3RE_HOOK(0x00504b70, GameWorld_DropStoragePile, DropPileFn, Original_GameWorld_DropStoragePile,
          GameWorld_DropStoragePile);
S3RE_HOOK(0x00504e20, GameWorld_DropOutputPile, DropPileFn, Original_GameWorld_DropOutputPile,
          GameWorld_DropOutputPile);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kTestWorld = offsetof(GameWorld, buildings) + 64 * sizeof(Building);

// A building at (20..300, 20..300) of a random type/race with a layout, its
// footprint tiles marked (flags, stakes, blocked bits as UnblockTile needs
// them random anyway), sometimes a levelling site with a random grid.
void CompareFootprint(test::Context& ctx, BuildingFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    // UnblockTile (verified, own tests) floods regions over the random map;
    // here it only clears the blocked bit, and its calls are compared.
    test::CalleeStubs stubs;
    stubs.Install<2, kUnblockTile>([](GameWorld* w, std::uint32_t, const std::uint32_t* args) {
        TileAt(w, args[0], args[1]).flags &= 0xfe;
        return 0u;
    });
    worlds.Both([&](GameWorld* w) { w->map_size = 384; });
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t number = 1 + ctx.rng().Below(60);
        const std::uint32_t x = 20 + ctx.rng().Below(280);
        const std::uint32_t y = 20 + ctx.rng().Below(280);
        const auto type = static_cast<std::uint8_t>(1 + ctx.rng().Below(56));
        const auto race = static_cast<std::uint8_t>(ctx.rng().Below(4));
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Below(20));
        const auto slot = static_cast<std::uint16_t>(ctx.rng().Below(30));
        const auto buildup = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 1 : ctx.rng().Below(4));
        worlds.Both([&](GameWorld* w) {
            Building& b = w->buildings[number];
            b.x = static_cast<std::uint16_t>(x);
            b.y = static_cast<std::uint16_t>(y);
            b.art = type;
            b.race = race;
            b.owner = owner;
            b.unk22 = slot;
            b.buildup = buildup;
        });
        for (std::uint32_t dx = 0; dx < 31; ++dx) {
            for (std::uint32_t dy = 0; dy < 31; ++dy) {
                const auto flags = static_cast<std::uint8_t>(ctx.rng().Next32());
                const auto object = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? kObjectStake : ctx.rng().Below(256));
                const bool cell = ctx.rng().Chance(1, 2);
                worlds.Both([&](GameWorld* w) {
                    Tile& t = w->tiles[x - 15 + dx][y - 15 + dy];
                    t.flags = flags;
                    t.object = object;
                    w->site_cache[owner][slot].grid[dx * 31 + dy] = cell ? 1 : 0;
                });
            }
        }
        stubs.SetWorld(worlds.original());
        Original_GameWorld_ClearBuildingFootprint(worlds.original(), nullptr, number);
        const std::vector<test::StubCall> expected = stubs.Take();
        stubs.SetWorld(worlds.ours());
        candidate(worlds.ours(), nullptr, number);
        const std::vector<test::StubCall> actual = stubs.Take();
        if (!S3RE_EXPECT(expected == actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: building %u type %u race %u buildup %u", i, number, type, race,
                     buildup);
            return;
        }
    }
}

// Wrong: forgets the levelling grid and the site cache slot.
void __fastcall WrongClearBuildingFootprint(GameWorld* world, void* edx, std::uint32_t number) {
    Building& building = world->buildings[number];
    const std::uint8_t buildup = building.buildup;
    building.buildup = 0;  // skip the grid part: the site cache stays valid
    GameWorld_ClearBuildingFootprint(world, edx, number);
    building.buildup = buildup;
}

}  // namespace

S3RE_TEST(GameWorld_ClearBuildingFootprint_MatchesOriginal) {
    CompareFootprint(ctx, &GameWorld_ClearBuildingFootprint, 600);
}
S3RE_TEST(GameWorld_ClearBuildingFootprint_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareFootprint(probe, &WrongClearBuildingFootprint, 100);
    S3RE_EXPECT(probe.failures() > 0);
}


// --- BlockTileAndMoveSettler, BlockBuildingLine, Drop*Pile ------------------

namespace {

// Recording stubs: ResetSettlerToIdle (job becomes 1, so a scan doesn't find
// the settler twice), TakeGoodFromPile (amount and leaving - 1, as the
// original does for a pile that keeps goods), BlockTile (sets the blocked
// bit only; the real one floods regions over the random map).
void InstallSettlerStubs(test::CalleeStubs& stubs) {
    stubs.Install<1, kResetSettlerToIdle>([](GameWorld* w, std::uint32_t, const std::uint32_t* args) {
        w->settlers[args[0] & 0xffff].job = 1;
        return 0x5a5a0001u;
    });
    stubs.Install<1, kTakeGoodFromPile>([](GameWorld* w, std::uint32_t, const std::uint32_t* args) {
        layout::Pile& pile = w->piles[args[0] & 0xffff];
        --pile.amount;
        --pile.leaving;
        return 0u;
    });
    stubs.Install<2, kBlockTile>([](GameWorld* w, std::uint32_t, const std::uint32_t* args) {
        TileAt(w, args[0], args[1]).flags |= 1;
        return 0u;
    });
}

constexpr std::size_t kSettlerWorld = offsetof(GameWorld, buildings) + 64 * sizeof(Building);
constexpr std::uint32_t kSettlers = 150;

// The parts of the world every case needs sane: map size, small offset
// table (PlaceSettlerNear), small record counts (PileIncAmountLeaving scans
// the buildings), some free pile records (CreatePile).
void SetUpWorld(test::Context& ctx, test::WorldPair& worlds) {
    worlds.Both([&](GameWorld* w) {
        w->map_size = 384;
        w->building_high_water = 50;
    });
    for (std::uint32_t k = 0; k < 1000; ++k) {
        const std::int32_t dx = ctx.rng().Range(-4, 4);
        const std::int32_t dy = ctx.rng().Range(-4, 4);
        worlds.Both([&](GameWorld* w) {
            w->offsets[k].dx = dx;
            w->offsets[k].dy = dy;
        });
    }
}

// Settlers 1..150 with jobs and positions around the pile or tile.
void SetUpSettlers(test::Context& ctx, test::WorldPair& worlds, std::uint32_t pile, std::uint16_t px,
                   std::uint16_t py) {
    const std::uint32_t count = ctx.rng().Below(kSettlers);
    worlds.Both([&](GameWorld* w) { w->settler_high_water = count; });
    static constexpr std::uint16_t kJobs[] = {0x0a, 0x0b, 0xa9, 0x4c, 0x4d, 0x4e, 0x4f, 0x68, 0x69, 0x133, 0x134, 0x1};
    for (std::uint32_t n = 1; n <= kSettlers; ++n) {
        const auto job = ctx.rng().Chance(4, 5) ? kJobs[ctx.rng().Below(12)] : static_cast<std::uint16_t>(ctx.rng().Below(0x195));
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Chance(1, 6) ? kFree : ctx.rng().Below(8));
        const auto tx = static_cast<std::uint16_t>(ctx.rng().Chance(2, 3) ? px : ctx.rng().Below(384));
        const auto ty = static_cast<std::uint16_t>(ctx.rng().Chance(2, 3) ? py : ctx.rng().Below(384));
        const auto building = static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? pile : ctx.rng().Below(100));
        worlds.Both([&](GameWorld* w) {
            layout::Settler& s = w->settlers[n];
            s.job = job;
            s.owner = owner;
            s.target_x = tx;
            s.target_y = ty;
            s.building = building;
        });
    }
}

std::uint32_t SetUpPile(test::Context& ctx, test::WorldPair& worlds) {
    const std::uint32_t number = 1 + ctx.rng().Below(100);
    const auto x = static_cast<std::uint16_t>(10 + ctx.rng().Below(360));
    const auto y = static_cast<std::uint16_t>(10 + ctx.rng().Below(360));
    static constexpr std::uint8_t kKinds[] = {0, 0xfe, 0xfd, 0xff, 1};
    const std::uint8_t art = ctx.rng().Chance(4, 5) ? kKinds[ctx.rng().Below(5)] : static_cast<std::uint8_t>(ctx.rng().Below(256));
    const auto good = static_cast<std::uint8_t>(ctx.rng().Chance(1, 5) ? 0xff : 1 + ctx.rng().Below(35));
    const auto amount = static_cast<std::uint8_t>(ctx.rng().Chance(1, 4) ? 0 : ctx.rng().Below(9));
    const auto leaving = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Below(256));
    const auto coming = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Below(9));
    const auto prev = static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? 0 : 101 + ctx.rng().Below(50));
    const auto next = static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? 0 : 101 + ctx.rng().Below(50));
    const auto area = static_cast<std::uint16_t>(1 + ctx.rng().Below(50));
    worlds.Both([&](GameWorld* w) {
        layout::Pile& p = w->piles[number];
        p.x = x;
        p.y = y;
        p.art = art;
        p.good = good;
        p.amount = amount;
        p.leaving = leaving;
        p.coming = coming;
        p.prev = prev;
        p.next = next;
        w->tiles[x][y].object_number = static_cast<std::uint16_t>(number);
        w->tiles[x][y].area = area;
        w->pile_high_water = 160;
    });
    // A few free records for CreatePile.
    for (std::uint32_t k = 0; k < 3; ++k) {
        const std::uint32_t free = 1 + ctx.rng().Below(160);
        if (free != number) {
            worlds.Both([&](GameWorld* w) { w->piles[free].good = 0; });
        }
    }
    SetUpSettlers(ctx, worlds, number, x, y);
    return number;
}

// Runs one case of both versions; compares stub calls, DebugLogs and (every
// `every` cases) the whole world.
template <typename Fn, typename... Args>
bool RunCase(test::Context& ctx, test::WorldPair& worlds, test::CalleeStubs& stubs, DebugLogRecorder& logs,
             Fn original, Fn candidate, bool checkpoint, Args... args) {
    stubs.SetWorld(worlds.original());
    original(worlds.original(), nullptr, args...);
    const std::vector<test::StubCall> expectedCalls = stubs.Take();
    const std::vector<DebugLogCall> expectedLogs = logs.Take();
    stubs.SetWorld(worlds.ours());
    candidate(worlds.ours(), nullptr, args...);
    const std::vector<test::StubCall> actualCalls = stubs.Take();
    const std::vector<DebugLogCall> actualLogs = logs.Take();
    return S3RE_EXPECT(expectedCalls == actualCalls) && S3RE_EXPECT(expectedLogs == actualLogs) &&
           (!checkpoint || worlds.Checkpoint(ctx));
}

void CompareDropPile(test::Context& ctx, DropPileFn original, DropPileFn candidate, int cases) {
    test::WorldPair worlds(ctx, kSettlerWorld);
    test::CalleeStubs stubs;
    InstallSettlerStubs(stubs);
    DebugLogRecorder logs;
    SetUpWorld(ctx, worlds);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t pile = SetUpPile(ctx, worlds);
        if (!RunCase(ctx, worlds, stubs, logs, original, candidate, i % 32 == 31 || i == cases - 1, pile)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 31 before it): pile %u", i, pile);
            return;
        }
    }
}

void CompareBlockTile(test::Context& ctx, PositionFn candidate, int cases) {
    test::WorldPair worlds(ctx, kSettlerWorld);
    test::CalleeStubs stubs;
    InstallSettlerStubs(stubs);
    DebugLogRecorder logs;
    SetUpWorld(ctx, worlds);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t x = 20 + ctx.rng().Below(340);
        const std::uint32_t y = 20 + ctx.rng().Below(340);
        const auto settler = static_cast<std::uint16_t>(ctx.rng().Chance(1, 3) ? 0 : 1 + ctx.rng().Below(kSettlers));
        worlds.Both([&](GameWorld* w) { w->tiles[x][y].settler = settler; });
        // Neighbourhood for PlaceSettlerNear: some free, walkable tiles.
        for (std::int32_t dx = -4; dx <= 4; ++dx) {
            for (std::int32_t dy = -4; dy <= 4; ++dy) {
                const auto other = static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? 0 : 1 + ctx.rng().Below(kSettlers));
                const auto walk = static_cast<std::uint8_t>(ctx.rng().Chance(1, 4) ? 0 : 1 + ctx.rng().Below(200));
                if (dx == 0 && dy == 0) {
                    continue;
                }
                worlds.Both([&](GameWorld* w) {
                    w->tiles[x + dx][y + dy].settler = other;
                    w->tiles[x + dx][y + dy].walk_region = walk;
                });
            }
        }
        SetUpSettlers(ctx, worlds, 0, static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y));
        if (!RunCase(ctx, worlds, stubs, logs, Original_GameWorld_BlockTileAndMoveSettler, candidate,
                     i % 32 == 31 || i == cases - 1, x, y)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 31 before it): (%u, %u)", i, x, y);
            return;
        }
    }
}

void CompareBlockLine(test::Context& ctx, BuildingFn candidate, int cases) {
    test::WorldPair worlds(ctx, kSettlerWorld);
    test::CalleeStubs stubs;
    InstallSettlerStubs(stubs);
    DebugLogRecorder logs;
    SetUpWorld(ctx, worlds);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t number = 1 + ctx.rng().Below(50);
        const auto buildup = static_cast<std::uint8_t>(ctx.rng().Chance(4, 5) ? 0 : ctx.rng().Below(256));
        const auto direction = static_cast<std::uint16_t>(ctx.rng().Chance(1, 8) ? kNoDirection : ctx.rng().Below(6));
        const auto wx = static_cast<std::uint16_t>(30 + ctx.rng().Below(320));
        const auto wy = static_cast<std::uint16_t>(30 + ctx.rng().Below(320));
        worlds.Both([&](GameWorld* w) {
            Building& b = w->buildings[number];
            b.buildup = buildup;
            b.unk22 = direction;
            b.work_x = wx;
            b.work_y = wy;
        });
        if (direction != kNoDirection) {
            const std::uint32_t length = ctx.rng().Below(10);
            std::uint32_t x = wx;
            std::uint32_t y = wy;
            for (std::uint32_t k = 0; k <= length; ++k) {
                const std::uint8_t terrain = k == length ? kLineEndTerrain
                                                          : static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0x10 : ctx.rng().Below(0x30));
                const auto settler = static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? 0 : 1 + ctx.rng().Below(kSettlers));
                worlds.Both([&](GameWorld* w) {
                    TileAt(w, x, y).terrain = terrain;
                    TileAt(w, x, y).settler = settler;
                });
                x = NeighbourX(x, direction);
                y = NeighbourY(y, direction);
            }
        }
        SetUpSettlers(ctx, worlds, 0, wx, wy);
        if (!RunCase(ctx, worlds, stubs, logs, Original_GameWorld_BlockBuildingLine, candidate,
                     i % 32 == 31 || i == cases - 1, number)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 31 before it): building %u", i, number);
            return;
        }
    }
}

// Wrong: leaves the settler word on the tile.
void __fastcall WrongBlockTileAndMoveSettler(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y) {
    const std::uint16_t settler = TileAt(world, x, y).settler;
    GameWorld_BlockTileAndMoveSettler(world, edx, x, y);
    if (TileAt(world, x, y).settler == 0) {
        TileAt(world, x, y).settler = settler;
    }
}

// Wrong: keeps the direction.
void __fastcall WrongBlockBuildingLine(GameWorld* world, void* edx, std::uint32_t number) {
    const std::uint16_t direction = world->buildings[number].unk22;
    GameWorld_BlockBuildingLine(world, edx, number);
    world->buildings[number].unk22 = direction;
}

// Wrong: no "TROUBLE" log when nobody was coming.
void __fastcall WrongDropInputPile(GameWorld* world, void* edx, std::uint32_t number) {
    if ((world->piles[number].leaving >> 4) == 0) {
        world->piles[number].leaving = static_cast<std::uint8_t>(world->piles[number].leaving | 0x10);
        GameWorld_DropInputPile(world, edx, number);
        return;
    }
    GameWorld_DropInputPile(world, edx, number);
}

// Wrong: the dropped pile gets the amount left after taking the goods.
void __fastcall WrongDropStoragePile(GameWorld* world, void* edx, std::uint32_t number) {
    layout::Pile& pile = world->piles[number];
    if (pile.amount > 1) {
        --pile.amount;
    }
    GameWorld_DropStoragePile(world, edx, number);
}

// Wrong: fetchers with job 0x68 keep going.
void __fastcall WrongDropOutputPile(GameWorld* world, void* edx, std::uint32_t number) {
    std::vector<std::uint32_t> changed;
    for (std::uint32_t n = 1; n <= world->settler_high_water && n <= kSettlers; ++n) {
        if (world->settlers[n].job == 0x68) {
            world->settlers[n].job = 0x3;
            changed.push_back(n);
        }
    }
    GameWorld_DropOutputPile(world, edx, number);
    for (const std::uint32_t n : changed) {
        if (world->settlers[n].job == 0x3) {
            world->settlers[n].job = 0x68;
        }
    }
}

}  // namespace

S3RE_TEST(GameWorld_BlockTileAndMoveSettler_MatchesOriginal) {
    CompareBlockTile(ctx, &GameWorld_BlockTileAndMoveSettler, 2000);
}
S3RE_TEST(GameWorld_BlockTileAndMoveSettler_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareBlockTile(probe, &WrongBlockTileAndMoveSettler, 500);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_BlockBuildingLine_MatchesOriginal) {
    CompareBlockLine(ctx, &GameWorld_BlockBuildingLine, 2000);
}
S3RE_TEST(GameWorld_BlockBuildingLine_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareBlockLine(probe, &WrongBlockBuildingLine, 500);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_DropInputPile_MatchesOriginal) {
    CompareDropPile(ctx, Original_GameWorld_DropInputPile, &GameWorld_DropInputPile, 2000);
}
S3RE_TEST(GameWorld_DropInputPile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDropPile(probe, Original_GameWorld_DropInputPile, &WrongDropInputPile, 500);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_DropStoragePile_MatchesOriginal) {
    CompareDropPile(ctx, Original_GameWorld_DropStoragePile, &GameWorld_DropStoragePile, 2000);
}
S3RE_TEST(GameWorld_DropStoragePile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDropPile(probe, Original_GameWorld_DropStoragePile, &WrongDropStoragePile, 500);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_DropOutputPile_MatchesOriginal) {
    CompareDropPile(ctx, Original_GameWorld_DropOutputPile, &GameWorld_DropOutputPile, 2000);
}
S3RE_TEST(GameWorld_DropOutputPile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDropPile(probe, Original_GameWorld_DropOutputPile, &WrongDropOutputPile, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
