// Map areas: every tile has a 16-bit area number, and every area a record
// that counts its tiles and lists buildings.
//
// Tile area word: uint16 at world + (x * 0x300 + y + 0x44d9) * 24, i.e.
//   tile(x, y) + 0x18 with tile records at 0x67440 (world.h). 130 places in
//   the code index it this way; it is the strongest hint that the 24-byte
//   tile records really start at 0x67444 (then this word is field +0x14,
//   and the neighbour accesses at 0x6742c.. are fields of tile y-1). Not
//   changed in world.h yet; the formula below is the original's, including
//   its 32-bit wrap. Bit 0x8000 is set by 0x5194b0 (meaning unknown).
// Area records: 0x2e0 bytes at world+0xe145ec, indexed by the area number;
//   uint32 +0x0 = number of tiles in the area; uint16 slots at +0x96 and
//   +0xde hold building numbers (two lists of 36, meaning unknown).
// Buildings: 0x3a-byte records at world+0x1499e64 (see counting.cpp);
//   uint16 +0xa and +0xc are the building's tile (x, y).
//
// GameWorld::AssignTileToArea (0x004e5450): (x, y, area): count of `area`
//   += 1, then tile area word = area. EAX = area. ret 0xc.
// GameWorld::RemoveTileFromArea (0x004e54a0): (x, y): count of the tile's
//   area -= 1, then tile area word = 0. EAX = address of that count. ret 8.
// GameWorld::SetTileAreaMarked (0x005194b0): (x, y, value): tile area word =
//   value + 0x8000. EAX = (x * 0x300 + y + 0x44d9) * 3. ret 0xc.
// GameWorld::SetAreaBuildingSlotA (0x004ddfe0) / SetAreaBuildingSlotB
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

constexpr std::uint32_t kTileAreaIndexBias = 0x44d9;  // * 24 = 0x67458
constexpr std::uint32_t kTileAreaMarked = 0x8000;

constexpr std::size_t kAreaRecords = 0xe145ec;
constexpr std::uint32_t kAreaRecordBytes = 0x2e0;
constexpr std::size_t kAreaTileCount = 0x0;
constexpr std::uint32_t kAreaSlotsA = 0x96;
constexpr std::uint32_t kAreaSlotsB = 0xde;

constexpr std::size_t kBuildings = 0x1499e64;
constexpr std::uint32_t kBuildingBytes = 0x3a;
constexpr std::size_t kBuildingX = 0xa;
constexpr std::size_t kBuildingY = 0xc;

std::uint8_t* Bytes(GameWorld* world) {
    return reinterpret_cast<std::uint8_t*>(world);
}

// Index of the tile area word in 24-byte units (32-bit wrap, as the original).
std::uint32_t TileAreaIndex(std::uint32_t x, std::uint32_t y) {
    return x * world_offset::kMapRowTiles + y + kTileAreaIndexBias;
}

std::uint16_t& TileArea(GameWorld* world, std::uint32_t x, std::uint32_t y) {
    return *reinterpret_cast<std::uint16_t*>(Bytes(world) + TileAreaIndex(x, y) * 24u);
}

std::uint32_t& AreaTileCount(GameWorld* world, std::uint32_t area) {
    return *reinterpret_cast<std::uint32_t*>(Bytes(world) + kAreaRecords +
                                             area * kAreaRecordBytes + kAreaTileCount);
}

std::uint16_t BuildingWord(GameWorld* world, std::uint32_t building, std::size_t field) {
    return *reinterpret_cast<const std::uint16_t*>(Bytes(world) + kBuildings +
                                                   building * kBuildingBytes + field);
}

std::uint32_t SetAreaBuildingSlot(GameWorld* world, std::uint32_t building, std::uint32_t slot,
                                  std::uint32_t list) {
    const std::uint32_t area =
        TileArea(world, BuildingWord(world, building, kBuildingX), BuildingWord(world, building, kBuildingY));
    // Same address arithmetic as the original: ((area * 0x170 + slot) * 2).
    const std::uint32_t offset = (area * (kAreaRecordBytes / 2) + slot) * 2u;
    *reinterpret_cast<std::uint16_t*>(Bytes(world) + kAreaRecords + list + offset) =
        static_cast<std::uint16_t>(building);
    return slot;
}

}  // namespace

using AssignTileFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x,
                                                std::uint32_t y, std::uint32_t area);
using RemoveTileFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x,
                                                std::uint32_t y);
using BuildingSlotFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx,
                                                  std::uint32_t building, std::uint32_t slot);

// Purpose: Makes a tile part of an area. The map is divided into areas (probably the regions of connected
// land that make up a player's economy) and each area counts its tiles.
std::uint32_t __fastcall GameWorld_AssignTileToArea(GameWorld* world, void*, std::uint32_t x,
                                                    std::uint32_t y, std::uint32_t area) {
    std::uint32_t& count = AreaTileCount(world, area);
    count = count + 1;
    TileArea(world, x, y) = static_cast<std::uint16_t>(area);
    return area;
}

// Purpose: Takes a tile out of its area (the opposite of AssignTileToArea).
std::uint32_t __fastcall GameWorld_RemoveTileFromArea(GameWorld* world, void*, std::uint32_t x,
                                                      std::uint32_t y) {
    std::uint16_t& tileArea = TileArea(world, x, y);
    std::uint32_t& count = AreaTileCount(world, tileArea);
    count = count - 1;
    tileArea = 0;
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&count));
}

// Purpose: Writes a temporary marked area number into a tile, used while the game recomputes areas.
std::uint32_t __fastcall GameWorld_SetTileAreaMarked(GameWorld* world, void*, std::uint32_t x,
                                                     std::uint32_t y, std::uint32_t value) {
    TileArea(world, x, y) = static_cast<std::uint16_t>(value + kTileAreaMarked);
    return TileAreaIndex(x, y) * 3u;
}

// Purpose: Registers a building in one of its area's building lists (list A). Areas keep track of their
// buildings, probably so carriers and builders can find work nearby.
std::uint32_t __fastcall GameWorld_SetAreaBuildingSlotA(GameWorld* world, void*,
                                                        std::uint32_t building,
                                                        std::uint32_t slot) {
    return SetAreaBuildingSlot(world, building, slot, kAreaSlotsA);
}

// Purpose: Like SetAreaBuildingSlotA, for the area's second building list.
std::uint32_t __fastcall GameWorld_SetAreaBuildingSlotB(GameWorld* world, void*,
                                                        std::uint32_t building,
                                                        std::uint32_t slot) {
    return SetAreaBuildingSlot(world, building, slot, kAreaSlotsB);
}

S3RE_HOOK(0x004e5450, GameWorld_AssignTileToArea, AssignTileFn,
          Original_GameWorld_AssignTileToArea, GameWorld_AssignTileToArea);
S3RE_HOOK(0x004e54a0, GameWorld_RemoveTileFromArea, RemoveTileFn,
          Original_GameWorld_RemoveTileFromArea, GameWorld_RemoveTileFromArea);
S3RE_HOOK(0x005194b0, GameWorld_SetTileAreaMarked, AssignTileFn,
          Original_GameWorld_SetTileAreaMarked, GameWorld_SetTileAreaMarked);
S3RE_HOOK(0x004ddfe0, GameWorld_SetAreaBuildingSlotA, BuildingSlotFn,
          Original_GameWorld_SetAreaBuildingSlotA, GameWorld_SetAreaBuildingSlotA);
S3RE_HOOK(0x004de040, GameWorld_SetAreaBuildingSlotB, BuildingSlotFn,
          Original_GameWorld_SetAreaBuildingSlotB, GameWorld_SetAreaBuildingSlotB);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestMapSize = 384;
constexpr std::uint32_t kTestAreas = 0x800;  // area numbers stay inside the test world
constexpr std::uint32_t kTestBuildings = 2000;
constexpr std::size_t kMapWorld = kAreaRecords + kTestAreas * kAreaRecordBytes;
constexpr std::size_t kBuildingWorld = kBuildings + (kTestBuildings + 1) * kBuildingBytes;

std::size_t TileAreaOffset(std::uint32_t x, std::uint32_t y) {
    return TileAreaIndex(x, y) * 24u;
}

std::size_t AreaRecordOffset(std::uint32_t area) {
    return kAreaRecords + area * kAreaRecordBytes;
}

// Ours and the original return a world address in RemoveTileFromArea; make
// it relative to the world so the two buffers compare equal.
std::uint32_t Relative(std::uint32_t result, GameWorld* world, bool isAddress) {
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
        worlds.Set<std::uint16_t>(TileAreaOffset(x, y), static_cast<std::uint16_t>(area));
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
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Same(ctx, TileAreaOffset(x, y), 2) ||
            !worlds.Same(ctx, AreaRecordOffset(area), 4) ||
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
        const std::size_t record = kBuildings + building * kBuildingBytes;
        worlds.Set<std::uint16_t>(record + kBuildingX, x);
        worlds.Set<std::uint16_t>(record + kBuildingY, y);
        worlds.Set<std::uint16_t>(TileAreaOffset(x, y), area);
        const std::uint32_t expected = original(worlds.original(), nullptr, building, slot);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, building, slot);
        if (!S3RE_EXPECT_EQ(expected, actual) ||
            !worlds.Same(ctx, AreaRecordOffset(area), kAreaRecordBytes + 0x40) ||
            (i % 128 == 127 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: building %u, slot %u, area %u", i, building,
                     slot, area);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

// Wrong: stores the slot into list A instead of list B.
std::uint32_t __fastcall WrongSetAreaBuildingSlotB(GameWorld* world, void*, std::uint32_t building,
                                                   std::uint32_t slot) {
    return SetAreaBuildingSlot(world, building, slot, kAreaSlotsA);
}

}  // namespace

S3RE_TEST(GameWorld_AssignTileToArea_MatchesOriginal) {
    CompareTileOp(ctx, TileOp::Assign, Original_GameWorld_AssignTileToArea,
                  &GameWorld_AssignTileToArea, nullptr, nullptr, 20000);
}
S3RE_TEST(GameWorld_RemoveTileFromArea_MatchesOriginal) {
    CompareTileOp(ctx, TileOp::Remove, nullptr, nullptr, Original_GameWorld_RemoveTileFromArea,
                  &GameWorld_RemoveTileFromArea, 20000);
}
S3RE_TEST(GameWorld_SetTileAreaMarked_MatchesOriginal) {
    CompareTileOp(ctx, TileOp::Mark, Original_GameWorld_SetTileAreaMarked,
                  &GameWorld_SetTileAreaMarked, nullptr, nullptr, 20000);
}
S3RE_TEST(GameWorld_SetAreaBuildingSlotA_MatchesOriginal) {
    CompareBuildingSlot(ctx, Original_GameWorld_SetAreaBuildingSlotA,
                        &GameWorld_SetAreaBuildingSlotA, 20000);
}
S3RE_TEST(GameWorld_SetAreaBuildingSlotB_MatchesOriginal) {
    CompareBuildingSlot(ctx, Original_GameWorld_SetAreaBuildingSlotB,
                        &GameWorld_SetAreaBuildingSlotB, 20000);
}
S3RE_TEST(GameWorld_SetAreaBuildingSlotB_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareBuildingSlot(probe, Original_GameWorld_SetAreaBuildingSlotB, &WrongSetAreaBuildingSlotB,
                        2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
