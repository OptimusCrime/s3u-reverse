// Goods piles (provisional name): 14-byte records at world+0x123885c,
// numbered from 1; the number of records is the uint32 at world+0x1238858.
//   uint8  +0x0: goods type (0xff = none); index into the per-player goods
//                counters (players.cpp)
//   uint8  +0x1: amount
//   uint8  +0x4: kind; RecountPlayerGoods counts piles with kind <= 8 or 0xfe
//   uint16 +0x6/+0x8: tile (x, y)
// Tiles (from 0x67444, see building_tiles.cpp): uint8 +0x8 is the owning
// player (0xff = nobody), uint16 +0x2 a building number, flag 0x20 of byte
// +0xa set when there is one.
// Buildings (0x3a bytes at world+0x1499e64): uint16 +0x10/+0x12/+0x14 are
// pile numbers (three slots), byte +0xe unknown.
//
// GameWorld::BuildingPileHasGoods (0x004df9b0): (building, slot) -> AL = 1
//   if slot (low byte, 1..3) of the building holds a pile number != 0 whose
//   amount is > 0; else 0. The original only sets AL; callers only test AL.
//   ret 8.
// GameWorld::RecountPlayerGoods (0x004ffa50): () clears the 36 goods
//   counters of all 20 players, then adds the amount of every pile 1..count
//   with a goods type, kind <= 8 or 0xfe, standing on a tile with an owner,
//   to that owner's counter of the goods type. Only CLogicThread::OnStart
//   calls it. EAX = the pile count (ignored). Plain ret.
// GameWorld::PileNeighbourBuildingByteE (0x00505fd0): (pile) looks at the 19
//   tiles (pile.x + dx, pile.y + dy) for the int32 offset pairs (16-byte
//   stride) at world+0x110a588 and returns byte +0xe of the first building
//   found there, or 0xff. Callers only compare AL with 0xff; EAX is
//   reproduced anyway. ret 4.
// All thiscall (ECX = world).

#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>

namespace s3re::game {

namespace {

constexpr std::uint32_t kTiles = 0x67444;
constexpr std::uint32_t kTileBuilding = 0x2;
constexpr std::uint32_t kTileOwner = 0x8;
constexpr std::uint32_t kTileFlags = 0xa;
constexpr std::uint8_t kTileHasBuilding = 0x20;

constexpr std::uint32_t kPileCount = 0x1238858;
constexpr std::uint32_t kPiles = 0x123885c;
constexpr std::uint32_t kPileBytes = 14;
constexpr std::uint32_t kPileGoods = 0x0;
constexpr std::uint32_t kPileAmount = 0x1;
constexpr std::uint32_t kPileKind = 0x4;
constexpr std::uint32_t kPileX = 0x6;
constexpr std::uint32_t kPileY = 0x8;

constexpr std::uint32_t kBuildings = 0x1499e64;
constexpr std::uint32_t kBuildingBytes = 0x3a;
constexpr std::uint32_t kBuildingByteE = 0xe;
constexpr std::uint32_t kBuildingPiles = 0x10;  // uint16[3]

constexpr std::uint32_t kGoodsCounters = 0x11944b8;  // player 0; 0x148 bytes per player
constexpr std::uint32_t kPlayers = 20;
constexpr std::uint32_t kGoodsCounterDwords = 18;  // 36 uint16

constexpr std::uint32_t kNeighbourOffsets = 0x110a588;  // int32 dx, dy; 16-byte stride
constexpr std::uint32_t kNeighbourCount = 19;

constexpr std::uint8_t kNone = 0xff;

std::uint8_t* Bytes(GameWorld* world) {
    return reinterpret_cast<std::uint8_t*>(world);
}
template <typename T>
T& At(GameWorld* world, std::uint32_t offset) {
    return *reinterpret_cast<T*>(Bytes(world) + offset);
}

std::uint32_t Tile(std::uint32_t x, std::uint32_t y) {
    return kTiles + (x * 0x300 + y) * 24;
}
std::uint32_t Pile(std::uint32_t pile) {
    return kPiles + pile * kPileBytes;
}
std::uint32_t Building(std::uint32_t building) {
    return kBuildings + building * kBuildingBytes;
}

}  // namespace

using PileSlotFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t building,
                                              std::uint32_t slot);
using RecountFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx);
using PileFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t pile);

// Purpose: Whether one of a building's goods piles (its input or output stock) has anything on it. Checked
// constantly by settlers deciding what to do next.
std::uint32_t __fastcall GameWorld_BuildingPileHasGoods(GameWorld* world, void*,
                                                        std::uint32_t building,
                                                        std::uint32_t slot) {
    slot &= 0xff;
    if (slot < 1 || slot > 3) {
        return 0;
    }
    const std::uint16_t pile =
        At<std::uint16_t>(world, Building(building) + kBuildingPiles + (slot - 1) * 2);
    return pile != 0 && At<std::uint8_t>(world, Pile(pile) + kPileAmount) > 0 ? 1 : 0;
}

namespace {

std::uint32_t RecountPlayerGoods(GameWorld* world, std::uint8_t maxKind) {
    for (std::uint32_t player = 0; player < kPlayers; ++player) {
        for (std::uint32_t i = 0; i < kGoodsCounterDwords; ++i) {
            At<std::uint32_t>(world, kGoodsCounters + player * world_offset::kPlayerRecordBytes +
                                         i * 4) = 0;
        }
    }
    std::uint32_t count = At<std::uint32_t>(world, kPileCount);
    for (std::uint32_t pile = 1; pile <= count; ++pile) {
        const std::uint32_t p = Pile(pile);
        const std::uint8_t kind = At<std::uint8_t>(world, p + kPileKind);
        const std::uint8_t goods = At<std::uint8_t>(world, p + kPileGoods);
        if ((kind <= maxKind || kind == 0xfe) && goods != kNone) {
            const std::uint8_t owner = At<std::uint8_t>(
                world, Tile(At<std::uint16_t>(world, p + kPileX), At<std::uint16_t>(world, p + kPileY)) +
                           kTileOwner);
            if (owner != kNone) {
                std::uint16_t& counter = At<std::uint16_t>(
                    world, kGoodsCounters + (goods + owner * 164u) * 2u);
                counter = static_cast<std::uint16_t>(counter + At<std::uint8_t>(world, p + kPileAmount));
            }
        }
        count = At<std::uint32_t>(world, kPileCount);  // re-read every step, as the original
    }
    return count;
}

}  // namespace

// Purpose: Recounts from scratch how much of each kind of goods every player has, by adding up all goods
// piles on their land. Done once at game start.
std::uint32_t __fastcall GameWorld_RecountPlayerGoods(GameWorld* world, void*) {
    return RecountPlayerGoods(world, 8);
}

// Purpose: Finds the building next to a goods pile and returns one of its fields (meaning unknown).
// Probably tells which building a pile belongs to.
std::uint32_t __fastcall GameWorld_PileNeighbourBuildingByteE(GameWorld* world, void*,
                                                              std::uint32_t pile) {
    const std::uint32_t p = Pile(pile);
    std::uint32_t building = 0;
    for (std::uint32_t i = 0; i < kNeighbourCount && building == 0; ++i) {
        const std::uint32_t entry = kNeighbourOffsets + i * 16;
        const std::uint32_t tile =
            Tile(At<std::uint16_t>(world, p + kPileX) + At<std::uint32_t>(world, entry),
                 At<std::uint16_t>(world, p + kPileY) + At<std::uint32_t>(world, entry + 4));
        const std::uint16_t found = At<std::uint16_t>(world, tile + kTileBuilding);
        if (found != 0 && (At<std::uint8_t>(world, tile + kTileFlags) & kTileHasBuilding) != 0) {
            building = found;
        }
    }
    if (building == 0) {
        return kNone;  // the original: loop counter 0x13 | 0xff
    }
    return ((building * 7) & 0xffffff00u) | At<std::uint8_t>(world, Building(building) + kBuildingByteE);
}

S3RE_HOOK(0x004df9b0, GameWorld_BuildingPileHasGoods, PileSlotFn,
          Original_GameWorld_BuildingPileHasGoods, GameWorld_BuildingPileHasGoods);
S3RE_HOOK(0x004ffa50, GameWorld_RecountPlayerGoods, RecountFn, Original_GameWorld_RecountPlayerGoods,
          GameWorld_RecountPlayerGoods);
S3RE_HOOK(0x00505fd0, GameWorld_PileNeighbourBuildingByteE, PileFn,
          Original_GameWorld_PileNeighbourBuildingByteE, GameWorld_PileNeighbourBuildingByteE);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestBuildings = 2000;
constexpr std::uint32_t kTestPiles = 3000;
// Building numbers come from random tile words (up to 0xffff).
constexpr std::size_t kTestWorld = kBuildings + 0x10000 * kBuildingBytes;

std::uint16_t Coordinate(test::Context& ctx) {
    return static_cast<std::uint16_t>(ctx.rng().Below(384));
}

void PlacePile(test::Context& ctx, test::WorldPair& worlds, std::uint32_t pile) {
    worlds.Set<std::uint16_t>(Pile(pile) + kPileX, Coordinate(ctx));
    worlds.Set<std::uint16_t>(Pile(pile) + kPileY, Coordinate(ctx));
    if (ctx.rng().Chance(1, 4)) {
        static constexpr std::uint8_t kKinds[] = {0, 8, 9, 0xfe, 0xfd, 0xff};
        worlds.Set<std::uint8_t>(Pile(pile) + kPileKind, kKinds[ctx.rng().Below(6)]);
    }
    if (ctx.rng().Chance(1, 2)) {  // a real goods type (else random, often > 36)
        worlds.Set<std::uint8_t>(Pile(pile) + kPileGoods, static_cast<std::uint8_t>(ctx.rng().Below(36)));
    }
}

void CompareSlot(test::Context& ctx, PileSlotFn original, PileSlotFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t building = ctx.rng().Below(kTestBuildings);
        const std::uint32_t slot = ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(5);
        const std::uint32_t expected = original(worlds.original(), nullptr, building, slot) & 0xff;
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, building, slot) & 0xff;
        if (!S3RE_EXPECT_EQ(expected, actual)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: building %u, slot %#x", i, building, slot);
            return;
        }
        if (i % 16 == 15) {  // new random pile amounts and numbers
            worlds.Set<std::uint8_t>(Pile(ctx.rng().Below(0x10000)) + kPileAmount,
                                     ctx.rng().Chance(1, 2) ? 0 : 1);
        }
    }
    worlds.Checkpoint(ctx);
}

void CompareRecount(test::Context& ctx, RecountFn original, RecountFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t count = ctx.rng().Below(kTestPiles);
        worlds.Set<std::uint32_t>(kPileCount, count);
        for (std::uint32_t pile = 1; pile <= count; ++pile) {
            PlacePile(ctx, worlds, pile);
        }
        const std::uint32_t expected = original(worlds.original(), nullptr);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: %u piles", i, count);
            return;
        }
    }
}

void CompareNeighbour(test::Context& ctx, PileFn original, PileFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t pile = 1 + ctx.rng().Below(kTestPiles);
        PlacePile(ctx, worlds, pile);
        for (std::uint32_t k = 0; k < kNeighbourCount; ++k) {
            worlds.Set<std::int32_t>(kNeighbourOffsets + k * 16, ctx.rng().Range(-3, 3));
            worlds.Set<std::int32_t>(kNeighbourOffsets + k * 16 + 4, ctx.rng().Range(-3, 3));
        }
        // Tiles mostly without a building, so that later offsets get used.
        for (std::uint32_t k = 0; k < kNeighbourCount; ++k) {
            const std::uint32_t tile =
                Tile(At<std::uint16_t>(worlds.original(), Pile(pile) + kPileX) +
                         At<std::uint32_t>(worlds.original(), kNeighbourOffsets + k * 16),
                     At<std::uint16_t>(worlds.original(), Pile(pile) + kPileY) +
                         At<std::uint32_t>(worlds.original(), kNeighbourOffsets + k * 16 + 4));
            if (ctx.rng().Chance(7, 8)) {
                worlds.Set<std::uint8_t>(tile + kTileFlags,
                                         static_cast<std::uint8_t>(ctx.rng().Next32() & ~kTileHasBuilding));
            }
            if (ctx.rng().Chance(1, 4)) {
                worlds.Set<std::uint16_t>(tile + kTileBuilding, 0);
            }
        }
        const std::uint32_t expected = original(worlds.original(), nullptr, pile);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, pile);
        if (!S3RE_EXPECT_EQ(expected, actual)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: pile %u", i, pile);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

// Wrong: also counts piles of kind 9.
std::uint32_t __fastcall WrongRecountPlayerGoods(GameWorld* world, void*) {
    return RecountPlayerGoods(world, 9);
}

}  // namespace

S3RE_TEST(GameWorld_BuildingPileHasGoods_MatchesOriginal) {
    CompareSlot(ctx, Original_GameWorld_BuildingPileHasGoods, &GameWorld_BuildingPileHasGoods, 20000);
}
S3RE_TEST(GameWorld_RecountPlayerGoods_MatchesOriginal) {
    CompareRecount(ctx, Original_GameWorld_RecountPlayerGoods, &GameWorld_RecountPlayerGoods, 60);
}
S3RE_TEST(GameWorld_RecountPlayerGoods_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareRecount(probe, Original_GameWorld_RecountPlayerGoods, &WrongRecountPlayerGoods, 20);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_PileNeighbourBuildingByteE_MatchesOriginal) {
    CompareNeighbour(ctx, Original_GameWorld_PileNeighbourBuildingByteE,
                     &GameWorld_PileNeighbourBuildingByteE, 5000);
}

}  // namespace s3re::game
