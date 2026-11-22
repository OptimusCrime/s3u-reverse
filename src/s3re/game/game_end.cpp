// Counting helpers of the game-end check CLogicThread::CheckGameEnd
// (0x511c50; also called by 0x5145c0 and ProcessEvents). They take the
// logic thread in ECX and reach the world through this+0x3a0.
//
// CLogicThread::CountBuildingsOfTypes15To18 (0x00514a00): (int32 out[20]):
//   zeroes out[0..19], then for each building 1..count (count at
//   world+0x1499e60, re-read every round; 0x3a-byte records at
//   world+0x1499e64) whose player byte (+0x0) is not 0xff, byte +0x21 is 0
//   and type byte (+0x6) is 0xf, 0x10 or 0x12, and whose player record
//   (world+0x11943dc + player*0x148) has a non-zero first dword:
//   out[player] += 1. (A player above 19 would write past the 20 entries,
//   as in the original.) EAX (some callers only overwrite AL) = the player
//   byte of the last building looked at, or 0 if there are none.
//   ret 4.
// CLogicThread::CountTilesOwnedBy (0x00514a90): (player, points, count) ->
//   how many of the `count` (signed; none if <= 0) tiles in `points`
//   (int32 x, int32 y pairs) have owner byte (tile +0x8) == player (32-bit
//   compare). ret 0xc.
// CLogicThread::CountTilesOwnedByEither (0x00514ae0): (playerA, playerB,
//   points, count): the same for owner == playerA or owner == playerB.
//   ret 0x10.
// CLogicThread::SumAreaGoods10PerPlayer (0x00514b40): (int32 out[20]): zeroes
//   out[0..19], then for each area 1..count (count at world+0xe115dc;
//   0x2e0-byte records at world+0xe145e8) whose player byte (+0x0) is not
//   0xff: walks its list of goods-type-10 piles (head at +0x1e; the area
//   keeps one list per goods type, heads at +0xa + type*2, see
//   area_piles.cpp; next at pile +0xc; 14-byte pile records at
//   world+0x123885c) and adds each pile's amount (byte +0x1) to
//   out[player]. EAX (callers ignore it) = count + 1.
//   ret 4.

#include "game/logic_thread_layout.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {


namespace {

using layout::Area;
using layout::Building;

// Goods type whose piles SumAreaGoods10PerPlayer sums (list head at area +0x1e).
constexpr std::uint32_t kGoods10 = 10;
static_assert(offsetof(Area, piles_by_goods) + kGoods10 * 2 == 0x1e);

constexpr std::uint32_t kNoPlayer = 0xff;
constexpr std::uint32_t kPlayers = layout::kPlayers;

CGameData* WorldOf(CLogicThread* self) {
    return self->world;
}

// The owner of tile (x, y) of a point; the coordinates are not checked
// (TileOffset keeps the original's 32-bit address arithmetic).
std::uint32_t TileOwner(CGameData* world, const std::int32_t* point) {
    const std::uint32_t x = static_cast<std::uint32_t>(point[0]);
    const std::uint32_t y = static_cast<std::uint32_t>(point[1]);
    return TileAtOffset(world, TileOffset(x, y)).owner;
}

}  // namespace

using CountPerPlayerFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx, std::int32_t* out);
using CountOwnedFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx, std::uint32_t player,
                                                const std::int32_t* points, std::int32_t count);
using CountOwnedEitherFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx, std::uint32_t playerA,
                                                      std::uint32_t playerB, const std::int32_t* points,
                                                      std::int32_t count);

// Purpose: For the game-end check: counts, per player, the finished buildings of three particular types
// (probably castles/military buildings that decide whether a player is still in the game).
std::uint32_t __fastcall CLogicThread_CountBuildingsOfTypes15To18(CLogicThread* self, void*, std::int32_t* out) {
    std::memset(out, 0, kPlayers * sizeof(std::int32_t));
    std::uint32_t last = 0;
    for (std::uint32_t building = 1; building <= WorldOf(self)->building_high_water; ++building) {
        CGameData* world = WorldOf(self);
        const Building& record = world->buildings[building];
        const std::uint32_t player = record.owner;
        last = player;
        if (player == kNoPlayer || record.buildup != 0) {
            continue;
        }
        const std::uint8_t type = record.art;
        if (type != 0xf && type != 0x10 && type != 0x12) {
            continue;
        }
        if (world->players[player].active != 0) {  // players above 19 read past the records
            out[player] += 1;
        }
    }
    return last;
}

// Purpose: For the game-end check: how many of a list of tiles belong to a player (e.g. mission goals like
// 'hold these places').
std::uint32_t __fastcall CLogicThread_CountTilesOwnedBy(CLogicThread* self, void*, std::uint32_t player,
                                                        const std::int32_t* points, std::int32_t count) {
    std::uint32_t result = 0;
    for (std::int32_t i = 0; i < count; ++i) {
        if (TileOwner(WorldOf(self), points + i * 2) == player) {
            ++result;
        }
    }
    return result;
}

// Purpose: Like CountTilesOwnedBy, for tiles owned by either of two players (probably allies).
std::uint32_t __fastcall CLogicThread_CountTilesOwnedByEither(CLogicThread* self, void*, std::uint32_t playerA,
                                                              std::uint32_t playerB, const std::int32_t* points,
                                                              std::int32_t count) {
    std::uint32_t result = 0;
    for (std::int32_t i = 0; i < count; ++i) {
        const std::uint32_t owner = TileOwner(WorldOf(self), points + i * 2);
        if (owner == playerA || owner == playerB) {
            ++result;
        }
    }
    return result;
}

// Purpose: For the game-end check: how much of goods type 10 each player has lying in their areas (probably
// gold, for 'collect N gold' goals).
std::uint32_t __fastcall CLogicThread_SumAreaGoods10PerPlayer(CLogicThread* self, void*, std::int32_t* out) {
    std::memset(out, 0, kPlayers * sizeof(std::int32_t));
    std::uint32_t area = 1;
    for (; area <= WorldOf(self)->area_high_water; ++area) {
        CGameData* world = WorldOf(self);
        const Area& record = world->areas[area];
        const std::uint32_t player = record.player;
        if (player == kNoPlayer) {
            continue;
        }
        for (std::uint32_t pile = record.piles_by_goods[kGoods10]; pile != 0;
             pile = WorldOf(self)->piles[pile].next) {
            out[player] += world->piles[pile].amount;
        }
    }
    return area;
}

S3RE_HOOK(0x00514a00, CLogicThread_CountBuildingsOfTypes15To18, CountPerPlayerFn,
          Original_CLogicThread_CountBuildingsOfTypes15To18, CLogicThread_CountBuildingsOfTypes15To18);
S3RE_HOOK(0x00514a90, CLogicThread_CountTilesOwnedBy, CountOwnedFn, Original_CLogicThread_CountTilesOwnedBy,
          CLogicThread_CountTilesOwnedBy);
S3RE_HOOK(0x00514ae0, CLogicThread_CountTilesOwnedByEither, CountOwnedEitherFn,
          Original_CLogicThread_CountTilesOwnedByEither, CLogicThread_CountTilesOwnedByEither);
S3RE_HOOK(0x00514b40, CLogicThread_SumAreaGoods10PerPlayer, CountPerPlayerFn,
          Original_CLogicThread_SumAreaGoods10PerPlayer, CLogicThread_SumAreaGoods10PerPlayer);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestBuildings = 600;
constexpr std::size_t kTestWorld = offsetof(CGameData, buildings) + (kTestBuildings + 2) * sizeof(Building);
constexpr std::uint32_t kTestAreas = 200;
constexpr std::uint32_t kTestPiles = 0x800;
// out[] gets 256 entries plus guard space: players above 19 write past 20.
constexpr std::size_t kOutEntries = 256 + 16;

// Two fake logic threads pointing at the two worlds, and two output buffers.
struct Fixture {
    explicit Fixture(test::Context& ctx) : worlds(ctx, kTestWorld) {
        threadA.world = worlds.original();
        threadB.world = worlds.ours();
    }
    CLogicThread* original() { return &threadA; }
    CLogicThread* ours() { return &threadB; }
    void FillOutputs(test::Context& ctx) {
        ctx.rng().Fill(outA, sizeof(outA));
        std::memcpy(outB, outA, sizeof(outA));
    }
    bool SameOutputs(test::Context& ctx) {
        return ctx.ExpectMemEq(outA, outB, sizeof(outA), "original out", "our out", __FILE__, __LINE__);
    }

    test::WorldPair worlds;
    CLogicThread threadA = {};
    CLogicThread threadB = {};
    std::int32_t outA[kOutEntries] = {};
    std::int32_t outB[kOutEntries] = {};
};

std::uint8_t RandomPlayer(test::Context& ctx) {
    if (ctx.rng().Chance(1, 6)) {
        return static_cast<std::uint8_t>(kNoPlayer);
    }
    return static_cast<std::uint8_t>(ctx.rng().Chance(1, 20) ? ctx.rng().Below(255) : ctx.rng().Below(kPlayers));
}

void SetUpPlayers(test::Context& ctx, test::WorldPair& worlds) {
    // Players up to 254 (building owners above 19 read past the 20 records).
    for (std::uint32_t player = 0; player < 255; ++player) {
        const auto active = static_cast<std::int32_t>(ctx.rng().Chance(1, 4) ? 0 : ctx.rng().Next32() | 1);
        worlds.Both([&](CGameData* w) { w->players[player].active = active; });
    }
}

// Wrong: counts type 0x11 too.
std::uint32_t __fastcall WrongCountBuildingsOfTypes15To18(CLogicThread* self, void* edx, std::int32_t* out) {
    CGameData* world = WorldOf(self);
    const std::uint32_t result = CLogicThread_CountBuildingsOfTypes15To18(self, edx, out);
    for (std::uint32_t building = 1; building <= world->building_high_water; ++building) {
        const Building& record = world->buildings[building];
        const std::uint32_t player = record.owner;
        if (player != kNoPlayer && record.buildup == 0 && record.art == 0x11 && world->players[player].active != 0) {
            out[player] += 1;
        }
    }
    return result;
}

void CompareBuildingCounts(test::Context& ctx, CountPerPlayerFn candidate, int cases) {
    Fixture f(ctx);
    for (int i = 0; i < cases; ++i) {
        SetUpPlayers(ctx, f.worlds);
        const std::uint32_t count = ctx.rng().Chance(1, 16) ? 0 : ctx.rng().Below(kTestBuildings);
        f.worlds.Both([&](CGameData* w) { w->building_high_water = count; });
        for (std::uint32_t building = 1; building <= count + 1; ++building) {
            const std::uint8_t owner = RandomPlayer(ctx);
            const auto buildup = static_cast<std::uint8_t>(ctx.rng().Chance(2, 3) ? 0 : ctx.rng().Below(256));
            const auto type = static_cast<std::uint8_t>(0xe + ctx.rng().Below(6));
            f.worlds.Both([&](CGameData* w) {
                Building& record = w->buildings[building];
                record.owner = owner;
                record.buildup = buildup;
                record.art = type;
            });
        }
        f.FillOutputs(ctx);
        const std::uint32_t expected = Original_CLogicThread_CountBuildingsOfTypes15To18(f.original(), nullptr, f.outA);
        const std::uint32_t actual = candidate(f.ours(), nullptr, f.outB);
        if (!S3RE_EXPECT_EQ(expected, actual) || !f.SameOutputs(ctx) ||
            ((i % 16 == 15 || i == cases - 1) && !f.worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: %u buildings", i, count);
            return;
        }
    }
}

// `count` points, mostly on tiles owned by one of a few players.
void SetUpPoints(test::Context& ctx, test::WorldPair& worlds, std::int32_t* points, std::int32_t count) {
    for (std::int32_t i = 0; i < count; ++i) {
        const std::uint32_t x = ctx.rng().Below(384);
        const std::uint32_t y = ctx.rng().Below(384);
        points[i * 2] = static_cast<std::int32_t>(x);
        points[i * 2 + 1] = static_cast<std::int32_t>(y);
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Chance(1, 8) ? kNoPlayer : ctx.rng().Below(4));
        worlds.Both([&](CGameData* w) { w->tiles[x][y].owner = owner; });
    }
}

std::uint32_t RandomOwnerArgument(test::Context& ctx) {
    return ctx.rng().Chance(1, 8) ? (ctx.rng().Chance(1, 2) ? kNoPlayer : ctx.rng().Next32()) : ctx.rng().Below(4);
}

}  // namespace

S3RE_TEST(CLogicThread_CountBuildingsOfTypes15To18_MatchesOriginal) {
    CompareBuildingCounts(ctx, &CLogicThread_CountBuildingsOfTypes15To18, 200);
}
S3RE_TEST(CLogicThread_CountBuildingsOfTypes15To18_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareBuildingCounts(probe, &WrongCountBuildingsOfTypes15To18, 50);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CLogicThread_CountTilesOwnedBy_MatchesOriginal) {
    Fixture f(ctx);
    std::int32_t points[2 * 64] = {};
    for (int i = 0; i < 3000; ++i) {
        const std::int32_t count = ctx.rng().Chance(1, 8) ? ctx.rng().Range(-3, 0) : ctx.rng().Range(1, 64);
        SetUpPoints(ctx, f.worlds, points, count > 0 ? count : 0);
        const std::uint32_t player = RandomOwnerArgument(ctx);
        const std::uint32_t expected = Original_CLogicThread_CountTilesOwnedBy(f.original(), nullptr, player, points, count);
        const std::uint32_t actual = CLogicThread_CountTilesOwnedBy(f.ours(), nullptr, player, points, count);
        if (!S3RE_EXPECT_EQ(expected, actual)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: %d points", i, count);
            return;
        }
    }
    f.worlds.Checkpoint(ctx);
}

S3RE_TEST(CLogicThread_CountTilesOwnedByEither_MatchesOriginal) {
    Fixture f(ctx);
    std::int32_t points[2 * 64] = {};
    for (int i = 0; i < 3000; ++i) {
        const std::int32_t count = ctx.rng().Chance(1, 8) ? ctx.rng().Range(-3, 0) : ctx.rng().Range(1, 64);
        SetUpPoints(ctx, f.worlds, points, count > 0 ? count : 0);
        const std::uint32_t playerA = RandomOwnerArgument(ctx);
        const std::uint32_t playerB = RandomOwnerArgument(ctx);
        const std::uint32_t expected =
            Original_CLogicThread_CountTilesOwnedByEither(f.original(), nullptr, playerA, playerB, points, count);
        const std::uint32_t actual = CLogicThread_CountTilesOwnedByEither(f.ours(), nullptr, playerA, playerB, points, count);
        if (!S3RE_EXPECT_EQ(expected, actual)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: %d points", i, count);
            return;
        }
    }
    f.worlds.Checkpoint(ctx);
}

S3RE_TEST(CLogicThread_SumAreaGoods10PerPlayer_MatchesOriginal) {
    Fixture f(ctx);
    for (int i = 0; i < 300; ++i) {
        const std::uint32_t count = ctx.rng().Chance(1, 16) ? 0 : ctx.rng().Below(kTestAreas);
        f.worlds.Both([&](CGameData* w) { w->area_high_water = count; });
        // Each area gets a short chain of distinct piles (chains end at 0).
        std::uint32_t nextPile = 1;
        for (std::uint32_t area = 1; area <= count; ++area) {
            const std::uint8_t player = RandomPlayer(ctx);
            f.worlds.Both([&](CGameData* w) { w->areas[area].player = player; });
            const std::uint32_t length = ctx.rng().Below(5);
            std::uint32_t first = 0;
            for (std::uint32_t k = 0; k < length && nextPile < kTestPiles; ++k, ++nextPile) {
                const auto amount = static_cast<std::uint8_t>(ctx.rng().Next32());
                f.worlds.Both([&](CGameData* w) {
                    w->piles[nextPile].next = static_cast<std::uint16_t>(first);
                    w->piles[nextPile].amount = amount;
                });
                first = nextPile;
            }
            f.worlds.Both([&](CGameData* w) { w->areas[area].piles_by_goods[kGoods10] = static_cast<std::uint16_t>(first); });
        }
        f.FillOutputs(ctx);
        const std::uint32_t expected = Original_CLogicThread_SumAreaGoods10PerPlayer(f.original(), nullptr, f.outA);
        const std::uint32_t actual = CLogicThread_SumAreaGoods10PerPlayer(f.ours(), nullptr, f.outB);
        if (!S3RE_EXPECT_EQ(expected, actual) || !f.SameOutputs(ctx) ||
            ((i % 16 == 15 || i == 299) && !f.worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: %u areas", i, count);
            return;
        }
    }
}

}  // namespace s3re::game
