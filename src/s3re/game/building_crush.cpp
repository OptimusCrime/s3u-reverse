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
constexpr std::uintptr_t kUnblockTile = 0x0051b770;  // GameWorld::UnblockTile (area_split.cpp)

}  // namespace

using BuildingFn = void(__fastcall*)(GameWorld* world, void* edx, std::uint32_t building);

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

S3RE_HOOK(0x0051aae0, GameWorld_ClearBuildingFootprint, BuildingFn, Original_GameWorld_ClearBuildingFootprint,
          GameWorld_ClearBuildingFootprint);

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

}  // namespace s3re::game
