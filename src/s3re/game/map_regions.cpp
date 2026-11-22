// Building the region and area numbers of a fresh map.
//
// CGameData::BuildRegionsAndAreas (0x0051a580): (), thiscall, ret 0; EAX =
// mapSize (the last value the loops leave in EAX; CGameData::Create returns
// it, nobody reads it). Called at the end of CGameData::Create (random maps)
// and by the map loader 0x4fd540. Five passes over the map, each over
// tile (x, y) at world+0x67444 + (x*0x300 + y)*24 with x and y in
// [0, mapSize) (unsigned compares, mapSize at +0x2d2d0 re-read per step);
// the first four passes run with y outer and x inner:
//   1. every tile gets the temporary mark 0x10 in its flags (+0xa).
//   2. walk regions: a marked tile that is not blocked (flag 0x01) starts a
//      new walk region: id = FindFreeId256, in-use byte world+0xe114dc+id
//      = 1, the start tile's mark cleared, FloodSetByte10MarkedTiles(x, y,
//      id) (byte +0x10 of the connected marked tiles).
//   3. every tile is marked again; a tile with terrain (+0x6) <= 7 (water)
//      and object (+0x7) 0x0d..0x10 sets flag 0x80 on the 61 tiles of the
//      offset table's entries 0..60 (world+0x110a588, int32 dx, dy; 16-byte
//      stride) that are on the map (CGameData::IsOnMap). In this pass the
//      outer loop is y too, the inner x.
//   4. a marked, unblocked tile with an owner (+0x8 != 0xff) starts a new
//      economy area: AllocArea(owner), the start's mark cleared,
//      FloodAssignMarkedTiles(x, y, area, owner). Then (same tile, flags
//      re-read) a still marked tile with terrain 5, 6 or 7 and no flag 0x80
//      starts a water body: id = FindFreeId4096, in-use byte
//      world+0xe115e4+id = 1, mark cleared, FloodMarkAreaTerrain5To7(x, y,
//      id).
//   5. every tile is marked again; then (y outer, x inner) a marked tile
//      whose own tile and 18 tiles of the two rings around it are all
//      unblocked and none of whose 19 tiles has terrain class 0x20 starts a
//      wide region: id = FindFreeId8192, in-use byte world+0xe125e8+id = 1,
//      mark cleared, FloodSetWord16RoomyTiles(x, y, id). The ring tiles are
//      read even for x or y near 0 (two tiles before the map: other world
//      fields), like the original.

#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

using layout::Tile;

// Tile flags (re/structs.md "Tile").
constexpr std::uint8_t kFlagBlocked = layout::kTileBlocked;
constexpr std::uint8_t kFlagMark = layout::kTileFloodMark;
constexpr std::uint8_t kFlagNearHarbourWater = layout::kTileFlag80;
constexpr std::uint8_t kNoOwner = 0xff;
// Entries 0..60 of the offset table world->offsets (re/structs.md "MapOffset").
constexpr std::uint32_t kWaterMarkEntries = 61;

constexpr std::int32_t kRow = static_cast<std::int32_t>(kTileRowBytes);  // x + 1
constexpr std::int32_t kColumn = static_cast<std::int32_t>(kTileBytes);   // y + 1

Tile& TileAt(CGameData* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

std::uint32_t MapSize(CGameData* world) {
    return world->map_size;
}

std::uint8_t& Flags(CGameData* world, std::uint32_t x, std::uint32_t y) {
    return TileAt(world, x, y).flags;
}

// Calls visit(x, y) for every map tile, y outer and x inner, with mapSize
// re-read after each tile (the flood fills don't change it).
template <typename Visit>
void ForEachTile(CGameData* world, Visit visit) {
    for (std::uint32_t y = 0; y < MapSize(world); ++y) {
        for (std::uint32_t x = 0; x < MapSize(world); ++x) {
            visit(x, y);
        }
    }
}

void MarkAllTiles(CGameData* world) {
    ForEachTile(world, [&](std::uint32_t x, std::uint32_t y) { Flags(world, x, y) |= kFlagMark; });
}

bool IsMarkedAndFree(std::uint8_t flags) {
    return (flags & kFlagMark) != 0 && (flags & kFlagBlocked) == 0;
}

// Pass 3's object test: water tiles carrying objects 0x0d..0x10 (probably
// harbour or ship-related objects) mark their surroundings.
void MarkSurroundingsOfWaterObjects(CGameData* world) {
    ForEachTile(world, [&](std::uint32_t x, std::uint32_t y) {
        Tile& tile = TileAt(world, x, y);
        tile.flags |= kFlagMark;
        const std::uint8_t object = tile.object;
        if (tile.terrain > 7 || object < 0x0d || object > 0x10) {
            return;
        }
        for (std::uint32_t entry = 0; entry < kWaterMarkEntries; ++entry) {
            const std::uint32_t nx = x + static_cast<std::uint32_t>(world->offsets[entry].dx);
            const std::uint32_t ny = y + static_cast<std::uint32_t>(world->offsets[entry].dy);
            if (CGameData_IsOnMap(world, nullptr, nx, ny) != 0) {
                Flags(world, nx, ny) |= kFlagNearHarbourWater;
            }
        }
    });
}

// The two rings around a tile (relative byte steps), as pass 5 tests them.
constexpr std::int32_t kTwoRings[] = {
    kRow,               -kRow,              kColumn,           -kColumn,
    kRow + kColumn,     -kRow - kColumn,    2 * kRow,          2 * kRow + kColumn,
    2 * kRow + 2 * kColumn, kRow + 2 * kColumn, 2 * kColumn,   -kRow + kColumn,
    -2 * kRow,          -2 * kRow - kColumn, -2 * kRow - 2 * kColumn, -kRow - 2 * kColumn,
    -2 * kColumn,       kRow - kColumn,
};

// `tile` is a tile's 32-bit byte offset from the world (TileOffset); the
// ring tiles of edge tiles lie before or after the map.
bool HasRoomAround(CGameData* world, std::uint32_t tile) {
    if ((TileAtOffset(world, tile).terrain & 0xf0) == 0x20) {
        return false;
    }
    for (const std::int32_t step : kTwoRings) {
        const Tile& other = TileAtOffset(world, tile + static_cast<std::uint32_t>(step));
        if ((other.flags & kFlagBlocked) != 0 || (other.terrain & 0xf0) == 0x20) {
            return false;
        }
    }
    return true;
}

}  // namespace

using BuildRegionsFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx);

// Purpose: After a map was loaded or generated, numbers its connected walkable regions, splits the
// players' land into economy areas, numbers the lakes and seas, and numbers the wide open regions used by
// path finding.
std::uint32_t __fastcall CGameData_BuildRegionsAndAreas(CGameData* world, void*) {
    MarkAllTiles(world);
    ForEachTile(world, [&](std::uint32_t x, std::uint32_t y) {
        std::uint8_t& flags = Flags(world, x, y);
        if (!IsMarkedAndFree(flags)) {
            return;
        }
        const std::uint32_t id = CGameData_FindFreeId256(world, nullptr);
        world->walk_region_used[id] = 1;
        flags &= static_cast<std::uint8_t>(~kFlagMark);
        CGameData_FloodSetByte10MarkedTiles(world, nullptr, x, y, id);
    });

    MarkSurroundingsOfWaterObjects(world);

    ForEachTile(world, [&](std::uint32_t x, std::uint32_t y) {
        Tile& tile = TileAt(world, x, y);
        std::uint8_t& flags = tile.flags;
        const std::uint8_t owner = tile.owner;
        if (IsMarkedAndFree(flags) && owner != kNoOwner) {
            const std::uint32_t area = CGameData_AllocArea(world, nullptr, owner);
            flags &= static_cast<std::uint8_t>(~kFlagMark);
            CGameData_FloodAssignMarkedTiles(world, nullptr, x, y, area, owner);
        }
        const std::uint8_t terrain = tile.terrain;
        if ((flags & kFlagMark) != 0 && terrain >= 5 && terrain <= 7 && (flags & kFlagNearHarbourWater) == 0) {
            const std::uint32_t id = CGameData_FindFreeId4096(world, nullptr);
            world->ids4096_used[id] = 1;
            flags &= static_cast<std::uint8_t>(~kFlagMark);
            CGameData_FloodMarkAreaTerrain5To7(world, nullptr, x, y, id);
        }
    });

    MarkAllTiles(world);
    ForEachTile(world, [&](std::uint32_t x, std::uint32_t y) {
        const std::uint32_t tile = TileOffset(x, y);
        std::uint8_t& flags = TileAtOffset(world, tile).flags;
        if (!IsMarkedAndFree(flags) || !HasRoomAround(world, tile)) {
            return;
        }
        const std::uint32_t id = CGameData_FindFreeId8192(world, nullptr);
        world->wide_region_used[id] = 1;
        flags &= static_cast<std::uint8_t>(~kFlagMark);
        CGameData_FloodSetWord16RoomyTiles(world, nullptr, x, y, id);
    });
    return MapSize(world);
}

S3RE_HOOK(0x0051a580, CGameData_BuildRegionsAndAreas, BuildRegionsFn, Original_CGameData_BuildRegionsAndAreas,
          CGameData_BuildRegionsAndAreas);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kWorldSize = sizeof(CGameData);
// The three id tables with their marks (walk_region_high_water up to the
// area records; includes area_high_water).
constexpr std::size_t kIdTables = offsetof(CGameData, walk_region_high_water);
constexpr std::size_t kIdTablesEnd = offsetof(CGameData, areas);
constexpr std::uint32_t kAreaCount = 4096;

// Builds the offset table with the game's own builder (0x516530, run by the
// world constructor).
void BuildOffsetTable(CGameData* world) {
    using BuildFn = void(__fastcall*)(CGameData* world, void* edx);
    reinterpret_cast<BuildFn>(0x00516530)(world, nullptr);
}

void Zero(test::WorldPair& worlds, std::size_t offset, std::size_t size) {
    std::memset(worlds.originalBytes() + offset, 0, size);
    std::memset(worlds.oursBytes() + offset, 0, size);
}

// A map of grass, water patches with some harbour objects, mountains
// (class 0x20), blocked tiles and owned land of a few players; areas mostly
// free, races 0..3. Everything else keeps the random fill.
void SetUpRegionMap(test::Context& ctx, test::WorldPair& worlds) {
    const std::uint32_t size = ctx.rng().Chance(1, 2) ? 64 + 64 * ctx.rng().Below(6) : 8 + ctx.rng().Below(120);
    worlds.Both([&](CGameData* w) {
        w->map_size = size;
        w->tile_ring_index = {};      // tile ring read/write index
        w->pending_event_index = {};  // pending-event ring read/write index
        w->event_index.write = 0;     // event ring write index
    });
    Zero(worlds, kIdTables, kIdTablesEnd - kIdTables);
    for (std::uint32_t area = 0; area < kAreaCount; ++area) {
        const auto player = static_cast<std::uint8_t>(ctx.rng().Chance(1, 20) ? 0 : kNoOwner);
        worlds.Both([&](CGameData* w) { w->areas[area].player = player; });
    }
    for (std::uint32_t player = 0; player < 20; ++player) {
        const auto race = static_cast<std::int32_t>(ctx.rng().Below(5));
        worlds.Both([&](CGameData* w) { w->players[player].race = race; });
    }
    const std::uint32_t owners = ctx.rng().Below(5);
    const std::uint32_t limit = size < 0x300 ? size : 0x300;
    for (std::uint32_t x = 0; x < limit; ++x) {
        for (std::uint32_t y = 0; y < limit; ++y) {
            const std::uint32_t patch = ((x / 9) * 7 + (y / 11) * 3) % 8;
            std::uint8_t terrain = patch < 4 ? 0x10 : patch < 6 ? static_cast<std::uint8_t>(5 + patch % 3)
                                                     : patch == 6 ? 0x20 : 0x30;
            if (ctx.rng().Chance(1, 30)) {
                terrain = static_cast<std::uint8_t>(ctx.rng().Next32());
            }
            const bool water = terrain <= 7;
            const auto object = static_cast<std::uint8_t>(water && ctx.rng().Chance(1, 200)
                                                              ? 0x0d + ctx.rng().Below(4)
                                                              : ctx.rng().Chance(1, 10) ? ctx.rng().Next32() : 0);
            const std::uint32_t quarter = (x * 2 / size) * 2 + y * 2 / size;
            const auto owner = static_cast<std::uint8_t>(quarter < owners ? quarter : kNoOwner);
            std::uint8_t flags = static_cast<std::uint8_t>(ctx.rng().Next32() & 0x6e);
            if (water || ctx.rng().Chance(1, 15)) {
                flags |= kFlagBlocked;
            }
            worlds.Both([&](CGameData* w) {
                Tile& tile = w->tiles[x][y];
                tile.terrain = terrain;
                tile.object = object;
                tile.owner = owner;
                tile.flags = flags;
                // No area yet (as after loading or generating; a garbage area
                // number would index past the area records).
                tile.area = 0;
            });
        }
    }
}

void CompareBuildRegions(test::Context& ctx, BuildRegionsFn candidate, int cases) {
    test::WorldPair worlds(ctx, kWorldSize);
    BuildOffsetTable(worlds.original());
    BuildOffsetTable(worlds.ours());
    for (int i = 0; i < cases; ++i) {
        SetUpRegionMap(ctx, worlds);
        const std::uint32_t expected = Original_CGameData_BuildRegionsAndAreas(worlds.original(), nullptr);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: map %u", i, MapSize(worlds.original()));
            return;
        }
    }
}

// Wrong: water bodies ignore flag 0x80 (the surroundings of harbour objects).
std::uint32_t __fastcall WrongBuildRegions(CGameData* world, void*) {
    ForEachTile(world, [&](std::uint32_t x, std::uint32_t y) {
        Tile& tile = TileAt(world, x, y);
        if (tile.terrain <= 7 && tile.object >= 0x0d && tile.object <= 0x10) {
            tile.object = 0;
        }
    });
    return CGameData_BuildRegionsAndAreas(world, nullptr);
}

}  // namespace

S3RE_TEST(CGameData_BuildRegionsAndAreas_MatchesOriginal) {
    CompareBuildRegions(ctx, &CGameData_BuildRegionsAndAreas, 12);
}
S3RE_TEST(CGameData_BuildRegionsAndAreas_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareBuildRegions(probe, &WrongBuildRegions, 6);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
