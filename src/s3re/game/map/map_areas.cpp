// Map areas: every tile has a 16-bit area number, and every area a record
// that counts its tiles and lists buildings.
//
// Tile area word: uint16 at world + (x * 0x300 + y + 0x44d9) * 24, i.e.
//   tile(x, y) + 0x18 with tile records at 0x67440 (world.h). 130 places in
//   the code index it this way; it is the strongest hint that the 24-byte
//   tile records really start at 0x67444 (then this word is field +0x14,
//   and the neighbour accesses at 0x6742c.. are fields of tile y-1). That
//   is now settled (layout.h: Tile::area). The original's 32-bit wrap is
//   kept through TileOffset. Bit 0x8000 is set by 0x5194b0 (kTileAreaWater).
// Area records: 0x2e0 bytes (layout::Area at world+0xe145e8); the original
//   addresses them from world+0xe145ec = Area::tile_count (+0x4, number of
//   tiles in the area); uint16 slots at +0x96 and +0xde from there are
//   Area::buildings_a / buildings_b (+0x9a / +0xe2; two lists of 36).
// Buildings: 0x3a-byte records at world+0x1499e64 (see counting.cpp);
//   uint16 +0xa and +0xc are the building's tile (Building::door_x/door_y).
//
// CGameData::AssignTileToArea (0x004e5450): (x, y, area): count of `area`
//   += 1, then tile area word = area. EAX = area. ret 0xc.
// CGameData::RemoveTileFromArea (0x004e54a0): (x, y): count of the tile's
//   area -= 1, then tile area word = 0. EAX = address of that count. ret 8.
// CGameData::SetTileAreaMarked (0x005194b0): (x, y, value): tile area word =
//   value + 0x8000. EAX = (x * 0x300 + y + 0x44d9) * 3. ret 0xc.
// CGameData::SetAreaBuildingSlotA (0x004ddfe0) / SetAreaBuildingSlotB
//   (0x004de040): (building, slot): in the area record of the building's
//   tile, slot `slot` of list A (+0x96) / B (+0xde) = building. EAX = slot.
//   ret 8.
// All thiscall (ECX = world). EAX is reproduced exactly although only one
// caller (of SlotB) keeps part of it (it then overwrites AL).

#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>

namespace s3re::game {

namespace {

using layout::Area;

// The tile area word is Tile::area, at world + TileOffset(x, y) + 0x14. The
// original computes its address as (x * 0x300 + y + 0x44d9) * 24 in 32 bits;
// the index in 24-byte units is also SetTileAreaMarked's return value.
constexpr std::uint32_t kTileAreaIndexBias =
    (offsetof(CGameData, tiles) + offsetof(layout::Tile, area)) / kTileBytes;
static_assert((offsetof(CGameData, tiles) + offsetof(layout::Tile, area)) % kTileBytes == 0);
static_assert(kTileAreaIndexBias == 0x44d9);

// Index of the tile area word in 24-byte units (32-bit wrap, as the original).
std::uint32_t TileAreaIndex(std::uint32_t x, std::uint32_t y) {
    return x * layout::kMapRowTiles + y + kTileAreaIndexBias;
}

std::uint16_t& TileArea(CGameData* world, std::uint32_t x, std::uint32_t y) {
    // TileOffset wraps at 2^32 like the original's index arithmetic.
    return TileAtOffset(world, TileOffset(x, y)).area;
}

std::uint32_t& AreaTileCount(CGameData* world, std::uint32_t area) {
    return world->areas[area].tile_count;
}

// Area::buildings_a or Area::buildings_b.
using AreaBuildingList = std::uint16_t (Area::*)[layout::kGoodsTypes];

std::uint32_t SetAreaBuildingSlot(CGameData* world, std::uint32_t building, std::uint32_t slot,
                                  AreaBuildingList list) {
    const layout::Building& record = world->buildings[building];
    const std::uint32_t area = TileArea(world, record.door_x, record.door_y);
    // The original computes ((area * 0x170 + slot) * 2) from the list's start;
    // slots past 36 run into the following fields, like this 32-bit indexing.
    (world->areas[area].*list)[slot] = static_cast<std::uint16_t>(building);
    return slot;
}

}  // namespace

using AssignTileFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x,
                                                std::uint32_t y, std::uint32_t area);
using RemoveTileFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x,
                                                std::uint32_t y);
using BuildingSlotFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx,
                                                  std::uint32_t building, std::uint32_t slot);

// Purpose: Makes a tile part of an area. The map is divided into areas (probably the regions of connected
// land that make up a player's economy) and each area counts its tiles.
std::uint32_t __fastcall CGameData_AssignTileToArea(CGameData* world, void*, std::uint32_t x,
                                                    std::uint32_t y, std::uint32_t area) {
    std::uint32_t& count = AreaTileCount(world, area);
    count = count + 1;
    TileArea(world, x, y) = static_cast<std::uint16_t>(area);
    return area;
}

// Purpose: Takes a tile out of its area (the opposite of AssignTileToArea).
std::uint32_t __fastcall CGameData_RemoveTileFromArea(CGameData* world, void*, std::uint32_t x,
                                                      std::uint32_t y) {
    std::uint16_t& tileArea = TileArea(world, x, y);
    std::uint32_t& count = AreaTileCount(world, tileArea);
    count = count - 1;
    tileArea = 0;
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&count));
}

// Purpose: Writes a temporary marked area number into a tile, used while the game recomputes areas.
std::uint32_t __fastcall CGameData_SetTileAreaMarked(CGameData* world, void*, std::uint32_t x,
                                                     std::uint32_t y, std::uint32_t value) {
    TileArea(world, x, y) = static_cast<std::uint16_t>(value + layout::kTileAreaWater);
    return TileAreaIndex(x, y) * 3u;
}

// Purpose: Registers a building in one of its area's building lists (list A). Areas keep track of their
// buildings, probably so carriers and builders can find work nearby.
std::uint32_t __fastcall CGameData_SetAreaBuildingSlotA(CGameData* world, void*,
                                                        std::uint32_t building,
                                                        std::uint32_t slot) {
    return SetAreaBuildingSlot(world, building, slot, &Area::buildings_a);
}

// Purpose: Like SetAreaBuildingSlotA, for the area's second building list.
std::uint32_t __fastcall CGameData_SetAreaBuildingSlotB(CGameData* world, void*,
                                                        std::uint32_t building,
                                                        std::uint32_t slot) {
    return SetAreaBuildingSlot(world, building, slot, &Area::buildings_b);
}

S3RE_HOOK(0x004e5450, CGameData_AssignTileToArea, AssignTileFn,
          Original_CGameData_AssignTileToArea, CGameData_AssignTileToArea);
S3RE_HOOK(0x004e54a0, CGameData_RemoveTileFromArea, RemoveTileFn,
          Original_CGameData_RemoveTileFromArea, CGameData_RemoveTileFromArea);
S3RE_HOOK(0x005194b0, CGameData_SetTileAreaMarked, AssignTileFn,
          Original_CGameData_SetTileAreaMarked, CGameData_SetTileAreaMarked);
S3RE_HOOK(0x004ddfe0, CGameData_SetAreaBuildingSlotA, BuildingSlotFn,
          Original_CGameData_SetAreaBuildingSlotA, CGameData_SetAreaBuildingSlotA);
S3RE_HOOK(0x004de040, CGameData_SetAreaBuildingSlotB, BuildingSlotFn,
          Original_CGameData_SetAreaBuildingSlotB, CGameData_SetAreaBuildingSlotB);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestMapSize = 384;
constexpr std::uint32_t kTestAreas = 0x800;  // area numbers stay inside the test world
constexpr std::uint32_t kTestBuildings = 2000;
// Up to the tile count of area kTestAreas (the old kAreaRecords bound).
constexpr std::size_t kMapWorld =
    offsetof(CGameData, areas) + kTestAreas * sizeof(Area) + offsetof(Area, tile_count);
constexpr std::size_t kBuildingWorld =
    offsetof(CGameData, buildings) + (kTestBuildings + 1) * sizeof(layout::Building);

// Ours and the original return a world address in RemoveTileFromArea; make
// it relative to the world so the two buffers compare equal.
std::uint32_t Relative(std::uint32_t result, CGameData* world, bool isAddress) {
    return isAddress ? result - static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world))
                     : result;
}

enum class TileOp { Assign, Remove, Mark };

void CompareTileOp(test::Context& ctx, TileOp op, AssignTileFn originalAssign,
                   AssignTileFn candidateAssign, RemoveTileFn originalRemove,
                   RemoveTileFn candidateRemove, int cases) {
    test::WorldPair worlds(ctx, kMapWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t x = ctx.rng().Below(kTestMapSize);
        const std::uint32_t y = ctx.rng().Below(kTestMapSize);
        const std::uint32_t area = ctx.rng().Below(kTestAreas);
        // Assign indexes the area records with the full 32-bit argument, so
        // it must be a valid area number.
        const std::uint32_t value = op == TileOp::Mark ? ctx.rng().Next32() : area;
        worlds.Both([&](CGameData* w) { w->tiles[x][y].area = static_cast<std::uint16_t>(area); });
        std::uint32_t expected;
        std::uint32_t actual;
        if (op == TileOp::Remove) {
            expected = Relative(originalRemove(worlds.original(), nullptr, x, y),
                                worlds.original(), true);
            actual = Relative(candidateRemove(worlds.ours(), nullptr, x, y), worlds.ours(), true);
        } else {
            expected = originalAssign(worlds.original(), nullptr, x, y, value);
            actual = candidateAssign(worlds.ours(), nullptr, x, y, value);
        }
        if (!S3RE_EXPECT_EQ(expected, actual) ||
            !worlds.SameField(ctx, [&](CGameData* w) -> auto& { return w->tiles[x][y].area; }) ||
            !worlds.SameField(ctx, [&](CGameData* w) -> auto& { return w->areas[area].tile_count; }) ||
            (i % 128 == 127 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%u, %u), area %u, value %#x", i, x, y, area,
                     value);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

void CompareBuildingSlot(test::Context& ctx, BuildingSlotFn original, BuildingSlotFn candidate,
                         int cases) {
    test::WorldPair worlds(ctx, kBuildingWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t building = ctx.rng().Below(kTestBuildings + 1);
        const std::uint32_t slot = ctx.rng().Below(64);
        const auto x = static_cast<std::uint16_t>(ctx.rng().Below(kTestMapSize));
        const auto y = static_cast<std::uint16_t>(ctx.rng().Below(kTestMapSize));
        const auto area = static_cast<std::uint16_t>(ctx.rng().Below(kTestAreas));
        worlds.Both([&](CGameData* w) {
            w->buildings[building].door_x = x;
            w->buildings[building].door_y = y;
            w->tiles[x][y].area = area;
        });
        const std::uint32_t expected = original(worlds.original(), nullptr, building, slot);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, building, slot);
        // From the area's tile count, the whole record plus the 64 slots'
        // reach (slot < 64 runs past the record).
        const std::size_t window =
            worlds.OffsetOf([&](CGameData* w) -> auto& { return w->areas[area].tile_count; });
        if (!S3RE_EXPECT_EQ(expected, actual) ||
            !worlds.Same(ctx, window, sizeof(Area) + 0x40) ||
            (i % 128 == 127 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: building %u, slot %u, area %u", i, building,
                     slot, area);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

// Wrong: stores the slot into list A instead of list B.
std::uint32_t __fastcall WrongSetAreaBuildingSlotB(CGameData* world, void*, std::uint32_t building,
                                                   std::uint32_t slot) {
    return SetAreaBuildingSlot(world, building, slot, &Area::buildings_a);
}

}  // namespace

S3RE_TEST(CGameData_AssignTileToArea_MatchesOriginal) {
    CompareTileOp(ctx, TileOp::Assign, Original_CGameData_AssignTileToArea,
                  &CGameData_AssignTileToArea, nullptr, nullptr, 20000);
}
S3RE_TEST(CGameData_RemoveTileFromArea_MatchesOriginal) {
    CompareTileOp(ctx, TileOp::Remove, nullptr, nullptr, Original_CGameData_RemoveTileFromArea,
                  &CGameData_RemoveTileFromArea, 20000);
}
S3RE_TEST(CGameData_SetTileAreaMarked_MatchesOriginal) {
    CompareTileOp(ctx, TileOp::Mark, Original_CGameData_SetTileAreaMarked,
                  &CGameData_SetTileAreaMarked, nullptr, nullptr, 20000);
}
S3RE_TEST(CGameData_SetAreaBuildingSlotA_MatchesOriginal) {
    CompareBuildingSlot(ctx, Original_CGameData_SetAreaBuildingSlotA,
                        &CGameData_SetAreaBuildingSlotA, 20000);
}
S3RE_TEST(CGameData_SetAreaBuildingSlotB_MatchesOriginal) {
    CompareBuildingSlot(ctx, Original_CGameData_SetAreaBuildingSlotB,
                        &CGameData_SetAreaBuildingSlotB, 20000);
}
S3RE_TEST(CGameData_SetAreaBuildingSlotB_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareBuildingSlot(probe, Original_CGameData_SetAreaBuildingSlotB, &WrongSetAreaBuildingSlotB,
                        2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
