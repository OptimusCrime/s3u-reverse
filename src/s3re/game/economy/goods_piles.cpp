// Goods piles: world->piles, 14-byte records at world+0x123885c, numbered
// from 1 up to world->pile_high_water (world+0x1238858).
//   uint8  +0x0: goods type (Good; 0xff = Good::Empty); index into the
//                per-player goods counters (players.cpp)
//   uint8  +0x1: amount
//   uint8  +0x4: kind (PileKind); RecountPlayerGoods counts piles of kinds
//                0..8 and PileKind::Dropped (0xfe)
//   uint16 +0x6/+0x8: tile (x, y)
// Tiles (from 0x67444, see building_tiles.cpp): uint8 +0x8 is the owning
// player (0xff = nobody), uint16 +0x2 a building number, flag 0x20 of byte
// +0xa set when there is one.
// Buildings (0x3a bytes at world+0x1499e64): uint16 +0x10/+0x12/+0x14 are
// pile numbers (three slots), word +0xe (pile_ring) a pile or, for a
// storehouse, its goods type in the low byte.
//
// CGameData::BuildingPileHasGoods (0x004df9b0): (building, slot) -> AL = 1
//   if slot (low byte, 1..3) of the building holds a pile number != 0 whose
//   amount is > 0; else 0. The original only sets AL; callers only test AL.
//   ret 8.
// CGameData::RecountPlayerGoods (0x004ffa50): () clears the 36 goods
//   counters of all 20 players, then adds the amount of every pile 1..count
//   with a goods type, kind <= 8 or 0xfe, standing on a tile with an owner,
//   to that owner's counter of the goods type. Only CLogicThread::OnStart
//   calls it. EAX = the pile count (ignored). Plain ret.
// CGameData::PileNeighbourBuildingByteE (0x00505fd0): (pile) looks at the 19
//   tiles (pile.x + dx, pile.y + dy) for the int32 offset pairs (16-byte
//   stride) at world+0x110a588 and returns byte +0xe of the first building
//   found there, or 0xff. Callers only compare AL with 0xff; EAX is
//   reproduced anyway. TakeGoodFromPile uses the byte as the goods type an
//   emptied storehouse pile gets back. ret 4.
// All thiscall (ECX = world).


#include "game/enums/goods.h"
#include "game/enums/piles.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>

namespace s3re::game {

using layout::Building;
using layout::Pile;
using layout::Tile;
using layout::kTileHasBuilding;

namespace {

constexpr std::uint32_t kNeighbourCount = 19;  // the first 19 entries of world->offsets (radius 0..2)
constexpr std::uint32_t kPileSlots = 3;        // Building::piles, numbered 1..3 by the callers

constexpr std::uint8_t kNoOwner = 0xff;     // Tile::owner of land nobody owns
constexpr std::uint8_t kNoBuilding = 0xff;  // PileNeighbourBuildingByteE: no building next to the pile

// Piles of kinds 0..8 (storehouse piles are kind 0) and dropped piles count
// towards a player's goods at game start; output, order and trade piles
// don't.
constexpr std::uint8_t kLastCountedPileKind = 8;

}  // namespace

using PileSlotFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t building,
                                              std::uint32_t slot);
using RecountFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx);
using PileFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t pile);

// Purpose: Whether one of a building's goods piles (its input or output stock) has anything on it. Checked
// constantly by settlers deciding what to do next.
std::uint32_t __fastcall CGameData_BuildingPileHasGoods(CGameData* world, void*,
                                                        std::uint32_t building,
                                                        std::uint32_t slot) {
    slot &= 0xff;  // the original compares only the low byte
    if (slot < 1 || slot > kPileSlots) {
        return 0;
    }
    const std::uint16_t pile = world->buildings[building].piles[slot - 1];
    return pile != 0 && world->piles[pile].amount > 0 ? 1 : 0;
}

namespace {

std::uint32_t RecountPlayerGoods(CGameData* world, std::uint8_t lastCountedKind) {
    for (layout::Player& player : world->players) {
        // The original clears the 36 goods words as 18 dwords (same bytes).
        for (std::uint16_t& goods : player.goods) {
            goods = 0;
        }
    }
    std::uint32_t count = world->pile_high_water;
    for (std::uint32_t number = 1; number <= count; ++number) {
        const Pile& pile = world->piles[number];
        const std::uint8_t kind = pile.art;
        const std::uint8_t good = pile.good;
        const bool counted = kind <= lastCountedKind || kind == Value(PileKind::Dropped);
        if (counted && good != Value(Good::Empty)) {
            const std::uint8_t owner = world->tiles[pile.x][pile.y].owner;
            if (owner != kNoOwner) {
                // good <= 0xfe and owner <= 0xfe are not range-checked, as in the original.
                std::uint16_t& counter = world->players[owner].goods[good];
                counter = static_cast<std::uint16_t>(counter + pile.amount);
            }
        }
        count = world->pile_high_water;  // re-read every step, as the original
    }
    return count;
}

}  // namespace

// Purpose: Recounts from scratch how much of each kind of goods every player has, by adding up all goods
// piles on their land. Done once at game start.
std::uint32_t __fastcall CGameData_RecountPlayerGoods(CGameData* world, void*) {
    return RecountPlayerGoods(world, kLastCountedPileKind);
}

// Purpose: Finds the building next to a goods pile and returns the low byte of its word +0xe: for a
// storehouse, the goods type its piles hold (TakeGoodFromPile gives an emptied storehouse pile that type
// back).
std::uint32_t __fastcall CGameData_PileNeighbourBuildingByteE(CGameData* world, void*,
                                                              std::uint32_t pile) {
    const Pile& p = world->piles[pile];
    std::uint32_t building = 0;
    for (std::uint32_t i = 0; i < kNeighbourCount && building == 0; ++i) {
        const layout::MapOffset& offset = world->offsets[i];
        // 32-bit coordinates (the neighbour can lie off the map): tile offset as in the original.
        const Tile& tile = TileAtOffset(world, TileOffset(p.x + static_cast<std::uint32_t>(offset.dx),
                                                          p.y + static_cast<std::uint32_t>(offset.dy)));
        const std::uint16_t found = tile.object_number;
        if (found != 0 && (tile.flags & kTileHasBuilding) != 0) {
            building = found;
        }
    }
    if (building == 0) {
        return kNoBuilding;  // the original: loop counter 19 (0x13) | 0xff
    }
    // The original reads only the low byte of the word +0xe (pile_ring).
    const std::uint8_t byteE = *reinterpret_cast<const std::uint8_t*>(&world->buildings[building].pile_ring);
    return ((building * 7) & 0xffffff00u) | byteE;  // upper bytes: the original's index arithmetic
}

S3RE_HOOK(0x004df9b0, CGameData_BuildingPileHasGoods, PileSlotFn,
          Original_CGameData_BuildingPileHasGoods, CGameData_BuildingPileHasGoods);
S3RE_HOOK(0x004ffa50, CGameData_RecountPlayerGoods, RecountFn, Original_CGameData_RecountPlayerGoods,
          CGameData_RecountPlayerGoods);
S3RE_HOOK(0x00505fd0, CGameData_PileNeighbourBuildingByteE, PileFn,
          Original_CGameData_PileNeighbourBuildingByteE, CGameData_PileNeighbourBuildingByteE);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestBuildings = 2000;
constexpr std::uint32_t kTestPiles = 3000;
// Building numbers come from random tile words (up to 0xffff).
constexpr std::size_t kTestWorld = offsetof(CGameData, buildings) + 0x10000 * sizeof(Building);

std::uint16_t Coordinate(test::Context& ctx) {
    return static_cast<std::uint16_t>(ctx.rng().Below(384));
}

void PlacePile(test::Context& ctx, test::WorldPair& worlds, std::uint32_t pile) {
    const std::uint16_t x = Coordinate(ctx);
    const std::uint16_t y = Coordinate(ctx);
    worlds.Both([&](CGameData* w) {
        w->piles[pile].x = x;
        w->piles[pile].y = y;
    });
    if (ctx.rng().Chance(1, 4)) {
        static constexpr std::uint8_t kKinds[] = {Value(PileKind::Storehouse), 8, 9, Value(PileKind::Dropped),
                                                  Value(PileKind::Order), Value(PileKind::Output)};
        const std::uint8_t kind = kKinds[ctx.rng().Below(6)];
        worlds.Both([&](CGameData* w) { w->piles[pile].art = kind; });
    }
    if (ctx.rng().Chance(1, 2)) {  // a real goods type (else random, often > 36)
        const auto goods = static_cast<std::uint8_t>(ctx.rng().Below(kGoodCount));
        worlds.Both([&](CGameData* w) { w->piles[pile].good = goods; });
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
            const std::uint32_t pile = ctx.rng().Below(0x10000);
            const std::uint8_t amount = ctx.rng().Chance(1, 2) ? 0 : 1;
            worlds.Both([&](CGameData* w) { w->piles[pile].amount = amount; });
        }
    }
    worlds.Checkpoint(ctx);
}

void CompareRecount(test::Context& ctx, RecountFn original, RecountFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t count = ctx.rng().Below(kTestPiles);
        worlds.Both([&](CGameData* w) { w->pile_high_water = count; });
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
            const std::int32_t dx = ctx.rng().Range(-3, 3);
            const std::int32_t dy = ctx.rng().Range(-3, 3);
            worlds.Both([&](CGameData* w) {
                w->offsets[k].dx = dx;
                w->offsets[k].dy = dy;
            });
        }
        // Tiles mostly without a building, so that later offsets get used.
        for (std::uint32_t k = 0; k < kNeighbourCount; ++k) {
            CGameData* const w0 = worlds.original();
            const std::uint32_t tile = TileOffset(w0->piles[pile].x + static_cast<std::uint32_t>(w0->offsets[k].dx),
                                                  w0->piles[pile].y + static_cast<std::uint32_t>(w0->offsets[k].dy));
            if (ctx.rng().Chance(7, 8)) {
                const auto flags = static_cast<std::uint8_t>(ctx.rng().Next32() & ~kTileHasBuilding);
                worlds.Both([&](CGameData* w) { TileAtOffset(w, tile).flags = flags; });
            }
            if (ctx.rng().Chance(1, 4)) {
                worlds.Both([&](CGameData* w) { TileAtOffset(w, tile).object_number = 0; });
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
std::uint32_t __fastcall WrongRecountPlayerGoods(CGameData* world, void*) {
    return RecountPlayerGoods(world, 9);
}

}  // namespace

S3RE_TEST(CGameData_BuildingPileHasGoods_MatchesOriginal) {
    CompareSlot(ctx, Original_CGameData_BuildingPileHasGoods, &CGameData_BuildingPileHasGoods, 20000);
}
S3RE_TEST(CGameData_RecountPlayerGoods_MatchesOriginal) {
    CompareRecount(ctx, Original_CGameData_RecountPlayerGoods, &CGameData_RecountPlayerGoods, 60);
}
S3RE_TEST(CGameData_RecountPlayerGoods_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareRecount(probe, Original_CGameData_RecountPlayerGoods, &WrongRecountPlayerGoods, 20);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_PileNeighbourBuildingByteE_MatchesOriginal) {
    CompareNeighbour(ctx, Original_CGameData_PileNeighbourBuildingByteE,
                     &CGameData_PileNeighbourBuildingByteE, 5000);
}

}  // namespace s3re::game
