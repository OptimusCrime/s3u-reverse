// Settler helpers.
//
// Settler records: 64 bytes each at world+0x12a5e60 (index 0 unused?);
// byte +1 is the settler type. The table at 0x00658f64 (int32 per type, in
// S3.EXE's .rdata) gives a class per type: 0 for most (civilians), 1..3 for
// some (soldier levels?), and other values (10, 20..22, 30, 40, 41, 50, 51,
// 99) for a few special types. Names here are provisional.
//
// GameWorld::CanSettlerEnterTile (0x00503760): (x, y, player, settlerType)
// -> 1 if the tile is not blocked (tile byte +0xe bit 0 clear) and has no
// occupant (tile word +0x4 == 0), and, for class-0 types only, the tile's
// owner byte (+0xc) equals `player`; else 0. thiscall, ret 0x10; EAX is 0/1.
//
// GameWorld::SettlerGroup (0x005038e0): (settler) -> 3 for type 0x29; 1 for
// classes 50..59; for class 0: 2 if the settler's word +0xe is 0xab or 0xac,
// else 1; 2 for all other classes. thiscall, ret 4; EAX is the result.

#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

constexpr std::size_t kSettlers = 0x12a5e60;
constexpr std::size_t kSettlerBytes = 0x40;
constexpr std::size_t kSettlerType = 0x1;
constexpr std::size_t kSettlerWordE = 0xe;

// int32 per settler type, in S3.EXE's read-only data.
const std::int32_t* const kSettlerTypeClass = reinterpret_cast<const std::int32_t*>(0x00658f64);

constexpr std::size_t kTileOccupant = 0x4;  // uint16, offset in the tile record
constexpr std::size_t kTileOwner = 0xc;     // uint8
constexpr std::size_t kTileFlagsE = 0xe;    // uint8, bit 0 = blocked

std::uint8_t* TileRecord(GameWorld* world, std::uint32_t x, std::uint32_t y) {
    // The original's arithmetic: ((x * 0x300 + y) * 24), 32-bit.
    const std::uint32_t offset = (x * world_offset::kMapRowTiles + y) * world_offset::kTileBytes;
    return reinterpret_cast<std::uint8_t*>(world) + world_offset::kMapTiles + offset;
}

std::uint8_t* Settler(GameWorld* world, std::uint32_t index) {
    return reinterpret_cast<std::uint8_t*>(world) + kSettlers + index * kSettlerBytes;
}

}  // namespace

using CanEnterFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x,
                                              std::uint32_t y, std::uint32_t player,
                                              std::uint32_t settlerType);
using SettlerGroupFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx,
                                                  std::uint32_t settler);

// Purpose: Whether a settler may step onto a tile: it must not be blocked or occupied, and ordinary
// settlers must stay on their own player's land (soldiers and some others may leave it).
std::uint32_t __fastcall GameWorld_CanSettlerEnterTile(GameWorld* world, void* /*edx*/,
                                                       std::uint32_t x, std::uint32_t y,
                                                       std::uint32_t player,
                                                       std::uint32_t settlerType) {
    const std::uint8_t* tile = TileRecord(world, x, y);
    std::uint16_t occupant;
    std::memcpy(&occupant, tile + kTileOccupant, sizeof(occupant));
    if ((tile[kTileFlagsE] & 1) != 0 || occupant != 0) {
        return 0;
    }
    if (kSettlerTypeClass[settlerType] != 0) {
        return 1;
    }
    return tile[kTileOwner] == player ? 1 : 0;
}

// Purpose: Sorts a settler into a broad group (for example civilians, soldiers, special units). Probably
// used to decide which rules apply to it, e.g. how it moves or what it may enter.
std::uint32_t __fastcall GameWorld_SettlerGroup(GameWorld* world, void* /*edx*/,
                                                std::uint32_t settler) {
    const std::uint8_t* record = Settler(world, settler);
    const std::uint8_t type = record[kSettlerType];
    if (type == 0x29) {
        return 3;
    }
    const std::int32_t typeClass = kSettlerTypeClass[type];
    if (typeClass >= 50 && typeClass <= 59) {
        return 1;
    }
    if (typeClass != 0) {
        return 2;
    }
    std::uint16_t word;
    std::memcpy(&word, record + kSettlerWordE, sizeof(word));
    return (word == 0xab || word == 0xac) ? 2 : 1;
}

S3RE_HOOK(0x00503760, GameWorld_CanSettlerEnterTile, CanEnterFn,
          Original_GameWorld_CanSettlerEnterTile, GameWorld_CanSettlerEnterTile);
S3RE_HOOK(0x005038e0, GameWorld_SettlerGroup, SettlerGroupFn, Original_GameWorld_SettlerGroup,
          GameWorld_SettlerGroup);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestMapSize = 384;
constexpr std::size_t kMapWorld =
    world_offset::kMapTiles + kTestMapSize * world_offset::kMapRowBytes;
constexpr std::uint32_t kTestSettlers = 4096;
constexpr std::size_t kSettlerWorld = kSettlers + kTestSettlers * kSettlerBytes + 0x100;
constexpr std::uint32_t kTestTypes = 48;  // the class table's real entries

void CompareCanEnter(test::Context& ctx, CanEnterFn candidate, int cases) {
    test::WorldPair worlds(ctx, kMapWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t x = ctx.rng().Below(kTestMapSize);
        const std::uint32_t y = ctx.rng().Below(kTestMapSize);
        const std::uint32_t player = ctx.rng().Below(20);
        const std::uint32_t type = ctx.rng().Below(kTestTypes);
        // Make every branch likely: free tiles, own territory.
        const std::size_t tile = static_cast<std::size_t>(
            TileRecord(worlds.original(), x, y) - worlds.originalBytes());
        if (ctx.rng().Chance(2, 3)) {
            worlds.Set<std::uint16_t>(tile + kTileOccupant, 0);
        }
        if (ctx.rng().Chance(2, 3)) {
            worlds.Set<std::uint8_t>(tile + kTileFlagsE,
                                     worlds.originalBytes()[tile + kTileFlagsE] & 0xfe);
        }
        if (ctx.rng().Chance(1, 2)) {
            worlds.Set<std::uint8_t>(tile + kTileOwner, static_cast<std::uint8_t>(player));
        }
        const std::uint32_t expected =
            Original_GameWorld_CanSettlerEnterTile(worlds.original(), nullptr, x, y, player, type);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, x, y, player, type);
        if (!S3RE_EXPECT_EQ(expected, actual) ||
            (i % 4096 == 4095 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%u, %u), player %u, type %u", i, x, y,
                     player, type);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

// Wrong: forgets the territory check for civilians.
std::uint32_t __fastcall WrongCanEnter(GameWorld* world, void* /*edx*/, std::uint32_t x,
                                       std::uint32_t y, std::uint32_t /*player*/,
                                       std::uint32_t /*settlerType*/) {
    const std::uint8_t* tile = TileRecord(world, x, y);
    std::uint16_t occupant;
    std::memcpy(&occupant, tile + kTileOccupant, sizeof(occupant));
    return ((tile[kTileFlagsE] & 1) != 0 || occupant != 0) ? 0 : 1;
}

void CompareSettlerGroup(test::Context& ctx, SettlerGroupFn candidate, int cases) {
    test::WorldPair worlds(ctx, kSettlerWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t settler = ctx.rng().Below(kTestSettlers);
        const std::size_t record = kSettlers + settler * kSettlerBytes;
        std::uint8_t type = static_cast<std::uint8_t>(ctx.rng().Below(kTestTypes));
        if (ctx.rng().Chance(1, 10)) {
            type = 0x29;
        }
        worlds.Set<std::uint8_t>(record + kSettlerType, type);
        if (ctx.rng().Chance(1, 3)) {
            worlds.Set<std::uint16_t>(record + kSettlerWordE,
                                      static_cast<std::uint16_t>(0xaa + ctx.rng().Below(4)));
        }
        const std::uint32_t expected =
            Original_GameWorld_SettlerGroup(worlds.original(), nullptr, settler);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, settler);
        if (!S3RE_EXPECT_EQ(expected, actual) ||
            (i % 4096 == 4095 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: settler %u, type 0x%02x", i, settler, type);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

// Wrong: treats only 0xab as the special job.
std::uint32_t __fastcall WrongSettlerGroup(GameWorld* world, void* edx, std::uint32_t settler) {
    const std::uint8_t* record = Settler(world, settler);
    std::uint16_t word;
    std::memcpy(&word, record + kSettlerWordE, sizeof(word));
    if (record[kSettlerType] != 0x29 && kSettlerTypeClass[record[kSettlerType]] == 0 &&
        word == 0xac) {
        return 1;
    }
    return GameWorld_SettlerGroup(world, edx, settler);
}

}  // namespace

S3RE_TEST(GameWorld_CanSettlerEnterTile_MatchesOriginal) {
    CompareCanEnter(ctx, &GameWorld_CanSettlerEnterTile, 200000);
}

S3RE_TEST(GameWorld_CanSettlerEnterTile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareCanEnter(probe, &WrongCanEnter, 20000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_SettlerGroup_MatchesOriginal) {
    CompareSettlerGroup(ctx, &GameWorld_SettlerGroup, 200000);
}

S3RE_TEST(GameWorld_SettlerGroup_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSettlerGroup(probe, &WrongSettlerGroup, 20000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
