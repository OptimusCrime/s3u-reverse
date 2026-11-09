// Building the region and area numbers of a fresh map.
//
// GameWorld::BuildRegionsAndAreas (0x0051a580): (), thiscall, ret 0; EAX =
// mapSize (the last value the loops leave in EAX; GameWorld::Create returns
// it, nobody reads it). Called at the end of GameWorld::Create (random maps)
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
//      stride) that are on the map (GameWorld::IsOnMap). In this pass the
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

// Tile records (re/structs.md "Tile").
constexpr std::uint32_t kTiles = 0x67444;
constexpr std::uint32_t kTileTerrain = 0x6;
constexpr std::uint32_t kTileObject = 0x7;
constexpr std::uint32_t kTileOwner = 0x8;
constexpr std::uint32_t kTileFlags = 0xa;
constexpr std::uint8_t kFlagBlocked = 0x01;
constexpr std::uint8_t kFlagMark = 0x10;
constexpr std::uint8_t kFlagNearHarbourWater = 0x80;
constexpr std::uint8_t kNoOwner = 0xff;
// World fields: the id tables' in-use bytes (id_allocators.cpp) and the
// offset table (re/structs.md "MapOffset").
constexpr std::uint32_t kWalkRegionInUse = 0xe114dc;
constexpr std::uint32_t kWaterBodyInUse = 0xe115e4;
constexpr std::uint32_t kWideRegionInUse = 0xe125e8;
constexpr std::uint32_t kOffsetTable = 0x110a588;
constexpr std::uint32_t kOffsetTableBytes = 0x10;
constexpr std::uint32_t kWaterMarkEntries = 61;

constexpr std::int32_t kRow = 0x300 * 24;  // x + 1
constexpr std::int32_t kColumn = 24;       // y + 1

template <typename T>
T& At(GameWorld* world, std::uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<std::uint8_t*>(world) + offset);
}

std::uint32_t Tile(std::uint32_t x, std::uint32_t y) {
    return kTiles + (x * 0x300 + y) * 24;
}

std::uint32_t MapSize(GameWorld* world) {
    return At<std::uint32_t>(world, world_offset::kMapSize);
}

std::uint8_t& Flags(GameWorld* world, std::uint32_t x, std::uint32_t y) {
    return At<std::uint8_t>(world, Tile(x, y) + kTileFlags);
}

// Calls visit(x, y) for every map tile, y outer and x inner, with mapSize
// re-read after each tile (the flood fills don't change it).
template <typename Visit>
void ForEachTile(GameWorld* world, Visit visit) {
    for (std::uint32_t y = 0; y < MapSize(world); ++y) {
        for (std::uint32_t x = 0; x < MapSize(world); ++x) {
            visit(x, y);
        }
    }
}

void MarkAllTiles(GameWorld* world) {
    ForEachTile(world, [&](std::uint32_t x, std::uint32_t y) { Flags(world, x, y) |= kFlagMark; });
}

bool IsMarkedAndFree(std::uint8_t flags) {
    return (flags & kFlagMark) != 0 && (flags & kFlagBlocked) == 0;
}

// Pass 3's object test: water tiles carrying objects 0x0d..0x10 (probably
// harbour or ship-related objects) mark their surroundings.
void MarkSurroundingsOfWaterObjects(GameWorld* world) {
    ForEachTile(world, [&](std::uint32_t x, std::uint32_t y) {
        const std::uint32_t tile = Tile(x, y);
        At<std::uint8_t>(world, tile + kTileFlags) |= kFlagMark;
        const std::uint8_t object = At<std::uint8_t>(world, tile + kTileObject);
        if (At<std::uint8_t>(world, tile + kTileTerrain) > 7 || object < 0x0d || object > 0x10) {
            return;
        }
        for (std::uint32_t entry = 0; entry < kWaterMarkEntries; ++entry) {
            const std::uint32_t nx = x + At<std::uint32_t>(world, kOffsetTable + entry * kOffsetTableBytes);
            const std::uint32_t ny = y + At<std::uint32_t>(world, kOffsetTable + entry * kOffsetTableBytes + 4);
            if (GameWorld_IsOnMap(world, nullptr, nx, ny) != 0) {
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

bool HasRoomAround(GameWorld* world, std::uint32_t tile) {
    if ((At<std::uint8_t>(world, tile + kTileTerrain) & 0xf0) == 0x20) {
        return false;
    }
    for (const std::int32_t step : kTwoRings) {
        const std::uint32_t other = tile + static_cast<std::uint32_t>(step);
        if ((At<std::uint8_t>(world, other + kTileFlags) & kFlagBlocked) != 0 ||
            (At<std::uint8_t>(world, other + kTileTerrain) & 0xf0) == 0x20) {
            return false;
        }
    }
    return true;
}

}  // namespace

using BuildRegionsFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx);

// Purpose: After a map was loaded or generated, numbers its connected walkable regions, splits the
// players' land into economy areas, numbers the lakes and seas, and numbers the wide open regions used by
// path finding.
std::uint32_t __fastcall GameWorld_BuildRegionsAndAreas(GameWorld* world, void*) {
    MarkAllTiles(world);
    ForEachTile(world, [&](std::uint32_t x, std::uint32_t y) {
        std::uint8_t& flags = Flags(world, x, y);
        if (!IsMarkedAndFree(flags)) {
            return;
        }
        const std::uint32_t id = GameWorld_FindFreeId256(world, nullptr);
        At<std::uint8_t>(world, kWalkRegionInUse + id) = 1;
        flags &= static_cast<std::uint8_t>(~kFlagMark);
        GameWorld_FloodSetByte10MarkedTiles(world, nullptr, x, y, id);
    });

    MarkSurroundingsOfWaterObjects(world);

    ForEachTile(world, [&](std::uint32_t x, std::uint32_t y) {
        const std::uint32_t tile = Tile(x, y);
        std::uint8_t& flags = At<std::uint8_t>(world, tile + kTileFlags);
        const std::uint8_t owner = At<std::uint8_t>(world, tile + kTileOwner);
        if (IsMarkedAndFree(flags) && owner != kNoOwner) {
            const std::uint32_t area = GameWorld_AllocArea(world, nullptr, owner);
            flags &= static_cast<std::uint8_t>(~kFlagMark);
            GameWorld_FloodAssignMarkedTiles(world, nullptr, x, y, area, owner);
        }
        const std::uint8_t terrain = At<std::uint8_t>(world, tile + kTileTerrain);
        if ((flags & kFlagMark) != 0 && terrain >= 5 && terrain <= 7 && (flags & kFlagNearHarbourWater) == 0) {
            const std::uint32_t id = GameWorld_FindFreeId4096(world, nullptr);
            At<std::uint8_t>(world, kWaterBodyInUse + id) = 1;
            flags &= static_cast<std::uint8_t>(~kFlagMark);
            GameWorld_FloodMarkAreaTerrain5To7(world, nullptr, x, y, id);
        }
    });

    MarkAllTiles(world);
    ForEachTile(world, [&](std::uint32_t x, std::uint32_t y) {
        const std::uint32_t tile = Tile(x, y);
        std::uint8_t& flags = At<std::uint8_t>(world, tile + kTileFlags);
        if (!IsMarkedAndFree(flags) || !HasRoomAround(world, tile)) {
            return;
        }
        const std::uint32_t id = GameWorld_FindFreeId8192(world, nullptr);
        At<std::uint8_t>(world, kWideRegionInUse + id) = 1;
        flags &= static_cast<std::uint8_t>(~kFlagMark);
        GameWorld_FloodSetWord16RoomyTiles(world, nullptr, x, y, id);
    });
    return MapSize(world);
}

S3RE_HOOK(0x0051a580, GameWorld_BuildRegionsAndAreas, BuildRegionsFn, Original_GameWorld_BuildRegionsAndAreas,
          GameWorld_BuildRegionsAndAreas);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kWorldSize = 0x153a128;
constexpr std::uint32_t kTileRingRead = 0xe07888;
constexpr std::uint32_t kIdTables = 0xe114d8;      // the three id tables with their marks
constexpr std::uint32_t kIdTablesEnd = 0xe125e8 + 0x2000;
constexpr std::uint32_t kAreaRecords = 0xe145e8;
constexpr std::uint32_t kAreaRecordBytes = 0x2e0;
constexpr std::uint32_t kAreaCount = 4096;
constexpr std::uint32_t kPlayerRace = 0x11943e0;
constexpr std::uint32_t kPlayerRecordBytes = 0x148;

// Builds the offset table with the game's own builder (0x516530, run by the
// world constructor).
void BuildOffsetTable(GameWorld* world) {
    using BuildFn = void(__fastcall*)(GameWorld* world, void* edx);
    reinterpret_cast<BuildFn>(0x00516530)(world, nullptr);
}

void Zero(test::WorldPair& worlds, std::uint32_t offset, std::uint32_t size) {
    std::memset(worlds.originalBytes() + offset, 0, size);
    std::memset(worlds.oursBytes() + offset, 0, size);
}

// A map of grass, water patches with some harbour objects, mountains
// (class 0x20), blocked tiles and owned land of a few players; areas mostly
// free, races 0..3. Everything else keeps the random fill.
void SetUpRegionMap(test::Context& ctx, test::WorldPair& worlds) {
    const std::uint32_t size = ctx.rng().Chance(1, 2) ? 64 + 64 * ctx.rng().Below(6) : 8 + ctx.rng().Below(120);
    worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
    Zero(worlds, kTileRingRead, 8);
    Zero(worlds, 0x32870, 8);   // pending-event ring read/write index
    Zero(worlds, 0x366fc, 4);   // event ring write index
    Zero(worlds, kIdTables, kIdTablesEnd - kIdTables);
    for (std::uint32_t area = 0; area < kAreaCount; ++area) {
        worlds.Set<std::uint8_t>(kAreaRecords + area * kAreaRecordBytes,
                                 static_cast<std::uint8_t>(ctx.rng().Chance(1, 20) ? 0 : kNoOwner));
    }
    for (std::uint32_t player = 0; player < 20; ++player) {
        worlds.Set<std::int32_t>(kPlayerRace + player * kPlayerRecordBytes,
                                 static_cast<std::int32_t>(ctx.rng().Below(5)));
    }
    const std::uint32_t owners = ctx.rng().Below(5);
    const std::uint32_t limit = size < 0x300 ? size : 0x300;
    for (std::uint32_t x = 0; x < limit; ++x) {
        for (std::uint32_t y = 0; y < limit; ++y) {
            const std::uint32_t tile = Tile(x, y);
            const std::uint32_t patch = ((x / 9) * 7 + (y / 11) * 3) % 8;
            std::uint8_t terrain = patch < 4 ? 0x10 : patch < 6 ? static_cast<std::uint8_t>(5 + patch % 3)
                                                     : patch == 6 ? 0x20 : 0x30;
            if (ctx.rng().Chance(1, 30)) {
                terrain = static_cast<std::uint8_t>(ctx.rng().Next32());
            }
            worlds.Set<std::uint8_t>(tile + kTileTerrain, terrain);
            const bool water = terrain <= 7;
            worlds.Set<std::uint8_t>(tile + kTileObject,
                                     static_cast<std::uint8_t>(water && ctx.rng().Chance(1, 200)
                                                                   ? 0x0d + ctx.rng().Below(4)
                                                                   : ctx.rng().Chance(1, 10) ? ctx.rng().Next32() : 0));
            const std::uint32_t quarter = (x * 2 / size) * 2 + y * 2 / size;
            worlds.Set<std::uint8_t>(tile + kTileOwner,
                                     static_cast<std::uint8_t>(quarter < owners ? quarter : kNoOwner));
            std::uint8_t flags = static_cast<std::uint8_t>(ctx.rng().Next32() & 0x6e);
            if (water || ctx.rng().Chance(1, 15)) {
                flags |= kFlagBlocked;
            }
            worlds.Set<std::uint8_t>(tile + kTileFlags, flags);
            // No area yet (as after loading or generating; a garbage area
            // number would index past the area records).
            worlds.Set<std::uint16_t>(tile + 0x14, 0);
        }
    }
}

void CompareBuildRegions(test::Context& ctx, BuildRegionsFn candidate, int cases) {
    test::WorldPair worlds(ctx, kWorldSize);
    BuildOffsetTable(worlds.original());
    BuildOffsetTable(worlds.ours());
    for (int i = 0; i < cases; ++i) {
        SetUpRegionMap(ctx, worlds);
        const std::uint32_t expected = Original_GameWorld_BuildRegionsAndAreas(worlds.original(), nullptr);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: map %u", i, MapSize(worlds.original()));
            return;
        }
    }
}

// Wrong: water bodies ignore flag 0x80 (the surroundings of harbour objects).
std::uint32_t __fastcall WrongBuildRegions(GameWorld* world, void*) {
    ForEachTile(world, [&](std::uint32_t x, std::uint32_t y) {
        const std::uint32_t tile = Tile(x, y);
        if (At<std::uint8_t>(world, tile + kTileTerrain) <= 7 &&
            At<std::uint8_t>(world, tile + kTileObject) >= 0x0d && At<std::uint8_t>(world, tile + kTileObject) <= 0x10) {
            At<std::uint8_t>(world, tile + kTileObject) = 0;
        }
    });
    return GameWorld_BuildRegionsAndAreas(world, nullptr);
}

}  // namespace

S3RE_TEST(GameWorld_BuildRegionsAndAreas_MatchesOriginal) {
    CompareBuildRegions(ctx, &GameWorld_BuildRegionsAndAreas, 12);
}
S3RE_TEST(GameWorld_BuildRegionsAndAreas_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareBuildRegions(probe, &WrongBuildRegions, 6);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
