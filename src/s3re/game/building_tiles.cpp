// Tile references of buildings and settlers.
//
// Tile records (24 bytes) are addressed here from 0x67444, the start the
// evidence favours (see LOGBOOK; world.h still says 0x67440):
//   uint16 +0x0: settler number on the tile
//   uint16 +0x2: building number on the tile
//   uint8  +0x7: unknown (cleared at a building's second tile)
//   uint8  +0xa: flags; bit 0x20 is set where tile word +0x2 holds a building
// Buildings: 0x3a-byte records at world+0x1499e64 (counting.cpp). uint8 +0x0
//   is the player (0xff = unused record); uint16 +0x2/+0x4 is one tile of the
//   building ("tile A") and +0xa/+0xc a second one ("tile B"; map_areas.cpp
//   looks up the building's area there). Which is the door is not settled.
// Settlers: 0x40-byte records at world+0x12a5e60 (counting.cpp): uint16
//   +0x4/+0x6 tile (x, y), uint32 +0x8 state, uint8 +0x14 flags, uint8 +0x15
//   (low 3 bits: an x offset), uint16 +0x22 the settler's building.
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

#include <cstdint>
#include <initializer_list>

namespace s3re::game {

namespace {

constexpr std::uint32_t kTiles = 0x67444;
constexpr std::uint32_t kTileSettler = 0x0;
constexpr std::uint32_t kTileBuilding = 0x2;
constexpr std::uint32_t kTileByte7 = 0x7;
constexpr std::uint32_t kTileFlags = 0xa;
constexpr std::uint8_t kTileHasBuilding = 0x20;

constexpr std::uint32_t kBuildings = 0x1499e64;
constexpr std::uint32_t kBuildingBytes = 0x3a;
constexpr std::uint32_t kBuildingPlayer = 0x0;
constexpr std::uint32_t kBuildingTileA = 0x2;  // x; y at +2
constexpr std::uint32_t kBuildingTileB = 0xa;
constexpr std::uint8_t kUnused = 0xff;

constexpr std::uint32_t kSettlers = 0x12a5e60;
constexpr std::uint32_t kSettlerBytes = 0x40;
constexpr std::uint32_t kSettlerX = 0x4;
constexpr std::uint32_t kSettlerY = 0x6;
constexpr std::uint32_t kSettlerState = 0x8;
constexpr std::uint32_t kSettlerFlags = 0x14;
constexpr std::uint32_t kSettlerSlot = 0x15;
constexpr std::uint32_t kSettlerBuilding = 0x22;
constexpr std::uint8_t kSettlerInSlot = 0x40;

std::uint8_t* Bytes(GameWorld* world) {
    return reinterpret_cast<std::uint8_t*>(world);
}
template <typename T>
T& At(GameWorld* world, std::uint32_t offset) {
    return *reinterpret_cast<T*>(Bytes(world) + offset);
}

std::uint32_t TileIndex(std::uint32_t x, std::uint32_t y) {
    return x * 0x300 + y;
}
std::uint32_t Tile(std::uint32_t x, std::uint32_t y) {
    return kTiles + TileIndex(x, y) * 24;
}

std::uint32_t Building(std::uint32_t building) {
    return kBuildings + building * kBuildingBytes;
}
std::uint32_t Settler(std::uint32_t settler) {
    return kSettlers + settler * kSettlerBytes;
}

std::uint32_t MoveSettlerToBuildingTile(GameWorld* world, std::uint32_t settler,
                                        std::uint32_t tileField) {
    const std::uint32_t s = Settler(settler);
    At<std::uint16_t>(world, Tile(At<std::uint16_t>(world, s + kSettlerX),
                                  At<std::uint16_t>(world, s + kSettlerY)) +
                                 kTileSettler) = 0;
    const std::uint32_t b = Building(At<std::uint16_t>(world, s + kSettlerBuilding));
    At<std::uint16_t>(world, s + kSettlerX) = At<std::uint16_t>(world, b + tileField);
    const std::uint16_t y = At<std::uint16_t>(world, Building(At<std::uint16_t>(
                                                         world, s + kSettlerBuilding)) +
                                                     tileField + 2);
    At<std::uint16_t>(world, s + kSettlerY) = y;
    const std::uint32_t index = TileIndex(At<std::uint16_t>(world, s + kSettlerX), y);
    At<std::uint16_t>(world, kTiles + index * 24 + kTileSettler) =
        static_cast<std::uint16_t>(settler);
    return index * 3;
}

}  // namespace

using IndexFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t index);

// Purpose: Removes a building from the map: its tiles no longer point to it and it no longer belongs to a
// player. Used when a building is destroyed or torn down.
std::uint32_t __fastcall GameWorld_RemoveBuildingFromTiles(GameWorld* world, void*,
                                                           std::uint32_t building) {
    const std::uint32_t b = Building(building);
    At<std::uint16_t>(world, Tile(At<std::uint16_t>(world, b + kBuildingTileA),
                                  At<std::uint16_t>(world, b + kBuildingTileA + 2)) +
                                 kTileBuilding) = 0;
    At<std::uint8_t>(world, Tile(At<std::uint16_t>(world, b + kBuildingTileB),
                                 At<std::uint16_t>(world, b + kBuildingTileB + 2)) +
                                kTileByte7) = 0;
    At<std::uint8_t>(world, Tile(At<std::uint16_t>(world, b + kBuildingTileA),
                                 At<std::uint16_t>(world, b + kBuildingTileA + 2)) +
                                kTileFlags) &= static_cast<std::uint8_t>(~kTileHasBuilding);
    At<std::uint8_t>(world, b + kBuildingPlayer) = kUnused;
    return 0;  // callers ignore EAX
}

// Purpose: A settler that was inside a building (in one of its slots) leaves it and appears on the
// building's exit tile.
std::uint32_t __fastcall GameWorld_LeaveBuildingSlot(GameWorld* world, void*,
                                                     std::uint32_t settler) {
    const std::uint32_t s = settler * kSettlerBytes;
    const std::uint32_t result = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world)) + s;
    if ((At<std::uint8_t>(world, kSettlers + s + kSettlerFlags) & kSettlerInSlot) == 0) {
        return result;
    }
    const std::uint32_t b = Building(At<std::uint16_t>(world, kSettlers + s + kSettlerBuilding));
    const std::uint32_t x = At<std::uint16_t>(world, b + kBuildingTileA) +
                            (At<std::uint8_t>(world, kSettlers + s + kSettlerSlot) & 7u) + 1;
    At<std::uint16_t>(world, Tile(x, At<std::uint16_t>(world, b + kBuildingTileA + 2)) +
                                 kTileSettler) = 0;
    At<std::uint8_t>(world, kSettlers + s + kSettlerFlags) &= static_cast<std::uint8_t>(~kSettlerInSlot);
    At<std::uint16_t>(world, kSettlers + s + kSettlerX) = At<std::uint16_t>(world, b + kBuildingTileB);
    At<std::uint16_t>(world, kSettlers + s + kSettlerY) =
        At<std::uint16_t>(world, b + kBuildingTileB + 2);
    At<std::uint32_t>(world, kSettlers + s + kSettlerState) = 2;
    return result;
}

// Purpose: Moves a settler onto the first of the two special tiles of its building (probably the building's
// main tile, where work happens).
std::uint32_t __fastcall GameWorld_MoveSettlerToBuildingTileA(GameWorld* world, void*,
                                                              std::uint32_t settler) {
    return MoveSettlerToBuildingTile(world, settler, kBuildingTileA);
}

// Purpose: Moves a settler onto the second special tile of its building (probably the door/flag tile).
std::uint32_t __fastcall GameWorld_MoveSettlerToBuildingTileB(GameWorld* world, void*,
                                                              std::uint32_t settler) {
    return MoveSettlerToBuildingTile(world, settler, kBuildingTileB);
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
constexpr std::size_t kTestWorld = kBuildings + (kTestBuildings + 1) * kBuildingBytes;

// Puts the settler's position, its building and that building's tiles on the
// map (x < 392 even with the slot offset), so every access stays inside the
// test world. The rest stays random.
void PrepareCase(test::Context& ctx, test::WorldPair& worlds, std::uint32_t settler,
                 std::uint32_t building) {
    auto coordinate = [&] { return static_cast<std::uint16_t>(ctx.rng().Below(384)); };
    worlds.Set<std::uint16_t>(Settler(settler) + kSettlerX, coordinate());
    worlds.Set<std::uint16_t>(Settler(settler) + kSettlerY, coordinate());
    worlds.Set<std::uint16_t>(Settler(settler) + kSettlerBuilding,
                              static_cast<std::uint16_t>(building));
    for (std::uint32_t field : {kBuildingTileA, kBuildingTileB}) {
        worlds.Set<std::uint16_t>(Building(building) + field, coordinate());
        worlds.Set<std::uint16_t>(Building(building) + field + 2, coordinate());
    }
    if (ctx.rng().Chance(1, 8)) {  // same tile for A and B
        worlds.Set<std::uint32_t>(Building(building) + kBuildingTileB,
                                  At<std::uint32_t>(worlds.original(), Building(building) + kBuildingTileA));
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
    const std::uint32_t b = Building(building);
    const std::uint32_t flags = Tile(At<std::uint16_t>(world, b + kBuildingTileA),
                                     At<std::uint16_t>(world, b + kBuildingTileA + 2)) +
                                kTileFlags;
    const std::uint8_t keep = At<std::uint8_t>(world, flags);
    GameWorld_RemoveBuildingFromTiles(world, nullptr, building);
    At<std::uint8_t>(world, flags) = keep;
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
