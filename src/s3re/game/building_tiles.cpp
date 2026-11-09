// Tile references of buildings and settlers.
//
// Tile records (24 bytes, layout::Tile at world+0x67444):
//   uint16 +0x0 settler: settler number on the tile
//   uint16 +0x2 object_number: building number on the tile
//   uint8  +0x7 object: landscape object (cleared at a building's second tile)
//   uint8  +0xa flags: bit 0x20 is set where object_number holds a building
// Buildings: 0x3a-byte records at world+0x1499e64 (counting.cpp). uint8 +0x0
//   is the player (0xff = unused record); uint16 +0x2/+0x4 is one tile of the
//   building ("tile A", Building::x/y) and +0xa/+0xc a second one ("tile B",
//   Building::door_x/door_y; map_areas.cpp looks up the building's area
//   there). Which is the door is not settled.
// Settlers: 0x40-byte records at world+0x12a5e60 (counting.cpp): uint16
//   +0x4/+0x6 tile (x, y), uint32 +0x8 state (Settler::direction), uint8
//   +0x14 flags, uint8 +0x15 (low 3 bits: an x offset), uint16 +0x22 the
//   settler's building.
//
// GameWorld::RemoveBuildingFromTiles (0x004d9940): (building) clears the
//   building number at tile A, byte +0x7 at tile B and flag 0x20 at tile A,
//   and marks the record unused (player = 0xff). ret 4; callers ignore EAX.
// GameWorld::LeaveBuildingSlot (0x0050cc10): (settler) if settler flag 0x40
//   is set: clears the settler number at (A.x + (byte 0x11 & 7) + 1, A.y) of
//   its building, clears the flag, moves the settler to tile B of the
//   building and sets its state to 2. EAX = world + settler * 0x40 (one
//   caller keeps the upper half). ret 4.
// GameWorld::MoveSettlerToBuildingTileA (0x0050de90) / ...TileB (0x0050df40):
//   (settler) moves the settler number from its tile to tile A / B of its
//   building and updates its position. EAX = new tile index * 3 (callers
//   ignore it). ret 4.
// All thiscall (ECX = world). Coordinates are 16-bit fields combined with
// the original's 32-bit arithmetic.

#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace s3re::game {

namespace {

using layout::Building;
using layout::Settler;
using layout::Tile;

constexpr std::uint8_t kUnused = 0xff;
constexpr std::uint8_t kSettlerInSlot = 0x40;

std::uint32_t TileIndex(std::uint32_t x, std::uint32_t y) {
    return x * 0x300 + y;
}
// Tile (x, y) with the original's 32-bit arithmetic.
Tile& TileAt(GameWorld* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

// One of a building's two tiles: A = (x, y), B = (door_x, door_y).
struct BuildingTile {
    std::uint16_t Building::*x;
    std::uint16_t Building::*y;
};
constexpr BuildingTile kTileA = {&Building::x, &Building::y};
constexpr BuildingTile kTileB = {&Building::door_x, &Building::door_y};

std::uint32_t MoveSettlerToBuildingTile(GameWorld* world, std::uint32_t settler, BuildingTile tile) {
    Settler& s = world->settlers[settler];
    TileAt(world, s.x, s.y).settler = 0;
    s.x = world->buildings[s.building].*tile.x;
    const std::uint16_t y = world->buildings[s.building].*tile.y;  // re-reads the building number
    s.y = y;
    const std::uint32_t index = TileIndex(s.x, y);
    TileAtOffset(world, static_cast<std::uint32_t>(offsetof(GameWorld, tiles)) + index * kTileBytes).settler =
        static_cast<std::uint16_t>(settler);
    return index * 3;
}

}  // namespace

using IndexFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t index);

// Purpose: Removes a building from the map: its tiles no longer point to it and it no longer belongs to a
// player. Used when a building is destroyed or torn down.
std::uint32_t __fastcall GameWorld_RemoveBuildingFromTiles(GameWorld* world, void*,
                                                           std::uint32_t building) {
    Building& b = world->buildings[building];
    TileAt(world, b.x, b.y).object_number = 0;
    TileAt(world, b.door_x, b.door_y).object = 0;
    TileAt(world, b.x, b.y).flags &= static_cast<std::uint8_t>(~layout::kTileHasBuilding);
    b.owner = kUnused;
    return 0;  // callers ignore EAX
}

// Purpose: A settler that was inside a building (in one of its slots) leaves it and appears on the
// building's exit tile.
std::uint32_t __fastcall GameWorld_LeaveBuildingSlot(GameWorld* world, void*,
                                                     std::uint32_t settler) {
    const std::uint32_t result = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world)) +
                                 settler * static_cast<std::uint32_t>(sizeof(Settler));
    Settler& s = world->settlers[settler];
    if ((s.flags & kSettlerInSlot) == 0) {
        return result;
    }
    const Building& b = world->buildings[s.building];
    const std::uint32_t x = b.x + (s.slot & 7u) + 1;
    TileAt(world, x, b.y).settler = 0;
    s.flags &= static_cast<std::uint8_t>(~kSettlerInSlot);
    s.x = b.door_x;
    s.y = b.door_y;
    s.direction = 2;  // the "state" above
    return result;
}

// Purpose: Moves a settler onto the first of the two special tiles of its building (probably the building's
// main tile, where work happens).
std::uint32_t __fastcall GameWorld_MoveSettlerToBuildingTileA(GameWorld* world, void*,
                                                              std::uint32_t settler) {
    return MoveSettlerToBuildingTile(world, settler, kTileA);
}

// Purpose: Moves a settler onto the second special tile of its building (probably the door/flag tile).
std::uint32_t __fastcall GameWorld_MoveSettlerToBuildingTileB(GameWorld* world, void*,
                                                              std::uint32_t settler) {
    return MoveSettlerToBuildingTile(world, settler, kTileB);
}

S3RE_HOOK(0x004d9940, GameWorld_RemoveBuildingFromTiles, IndexFn,
          Original_GameWorld_RemoveBuildingFromTiles, GameWorld_RemoveBuildingFromTiles);
S3RE_HOOK(0x0050cc10, GameWorld_LeaveBuildingSlot, IndexFn, Original_GameWorld_LeaveBuildingSlot,
          GameWorld_LeaveBuildingSlot);
S3RE_HOOK(0x0050de90, GameWorld_MoveSettlerToBuildingTileA, IndexFn,
          Original_GameWorld_MoveSettlerToBuildingTileA, GameWorld_MoveSettlerToBuildingTileA);
S3RE_HOOK(0x0050df40, GameWorld_MoveSettlerToBuildingTileB, IndexFn,
          Original_GameWorld_MoveSettlerToBuildingTileB, GameWorld_MoveSettlerToBuildingTileB);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestBuildings = 2000;
constexpr std::uint32_t kTestSettlers = 4000;
constexpr std::size_t kTestWorld = offsetof(GameWorld, buildings) + (kTestBuildings + 1) * sizeof(Building);

// Puts the settler's position, its building and that building's tiles on the
// map (x < 392 even with the slot offset), so every access stays inside the
// test world. The rest stays random.
void PrepareCase(test::Context& ctx, test::WorldPair& worlds, std::uint32_t settler,
                 std::uint32_t building) {
    auto coordinate = [&] { return static_cast<std::uint16_t>(ctx.rng().Below(384)); };
    const std::uint16_t sx = coordinate();
    const std::uint16_t sy = coordinate();
    worlds.Both([&](GameWorld* w) {
        w->settlers[settler].x = sx;
        w->settlers[settler].y = sy;
        w->settlers[settler].building = static_cast<std::uint16_t>(building);
    });
    for (const BuildingTile tile : {kTileA, kTileB}) {
        const std::uint16_t bx = coordinate();
        const std::uint16_t by = coordinate();
        worlds.Both([&](GameWorld* w) {
            w->buildings[building].*tile.x = bx;
            w->buildings[building].*tile.y = by;
        });
    }
    if (ctx.rng().Chance(1, 8)) {  // same tile for A and B
        worlds.Both([&](GameWorld* w) {
            w->buildings[building].door_x = w->buildings[building].x;
            w->buildings[building].door_y = w->buildings[building].y;
        });
    }
}

std::uint32_t Relative(std::uint32_t value, GameWorld* world, bool isAddress) {
    return isAddress ? value - static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world))
                     : value;
}

enum class Arg { Building, Settler };

void Compare(test::Context& ctx, IndexFn original, IndexFn candidate, Arg arg, bool eaxIsAddress,
             bool compareEax, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t building = ctx.rng().Below(kTestBuildings + 1);
        const std::uint32_t settler = ctx.rng().Below(kTestSettlers);
        PrepareCase(ctx, worlds, settler, building);
        const std::uint32_t value = arg == Arg::Building ? building : settler;
        const std::uint32_t expected =
            Relative(original(worlds.original(), nullptr, value), worlds.original(), eaxIsAddress);
        const std::uint32_t actual =
            Relative(candidate(worlds.ours(), nullptr, value), worlds.ours(), eaxIsAddress);
        if ((compareEax && !S3RE_EXPECT_EQ(expected, actual)) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: building %u, settler %u", i, building,
                     settler);
            return;
        }
    }
}

// Wrong: leaves flag 0x20 of the tile set.
std::uint32_t __fastcall WrongRemoveBuildingFromTiles(GameWorld* world, void*,
                                                      std::uint32_t building) {
    const Building& b = world->buildings[building];
    std::uint8_t& flags = TileAt(world, b.x, b.y).flags;
    const std::uint8_t keep = flags;
    GameWorld_RemoveBuildingFromTiles(world, nullptr, building);
    flags = keep;
    return 0;
}

}  // namespace

S3RE_TEST(GameWorld_RemoveBuildingFromTiles_MatchesOriginal) {
    Compare(ctx, Original_GameWorld_RemoveBuildingFromTiles, &GameWorld_RemoveBuildingFromTiles,
            Arg::Building, false, false, 120);
}
S3RE_TEST(GameWorld_RemoveBuildingFromTiles_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Compare(probe, Original_GameWorld_RemoveBuildingFromTiles, &WrongRemoveBuildingFromTiles,
            Arg::Building, false, false, 100);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_LeaveBuildingSlot_MatchesOriginal) {
    Compare(ctx, Original_GameWorld_LeaveBuildingSlot, &GameWorld_LeaveBuildingSlot, Arg::Settler,
            true, true, 120);
}
S3RE_TEST(GameWorld_MoveSettlerToBuildingTileA_MatchesOriginal) {
    Compare(ctx, Original_GameWorld_MoveSettlerToBuildingTileA,
            &GameWorld_MoveSettlerToBuildingTileA, Arg::Settler, false, true, 120);
}
S3RE_TEST(GameWorld_MoveSettlerToBuildingTileB_MatchesOriginal) {
    Compare(ctx, Original_GameWorld_MoveSettlerToBuildingTileB,
            &GameWorld_MoveSettlerToBuildingTileB, Arg::Settler, false, true, 120);
}

}  // namespace s3re::game
