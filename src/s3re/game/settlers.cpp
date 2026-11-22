// Settler helpers.
//
// Settler records: 64 bytes each at world+0x12a5e60 (index 0 unused?);
// byte +1 (Settler::art) is the settler type. The table at 0x00658f64 (int32
// per type, in S3.EXE's .rdata) gives a class per type: 0 for most
// (civilians), 1..3 for some (soldier levels?), and other values (10,
// 20..22, 30, 40, 41, 50, 51, 99) for a few special types. Names here are
// provisional.
//
// CGameData::CanSettlerEnterTile (0x00503760): (x, y, player, settlerType)
// -> 1 if the tile is not blocked (Tile::flags bit 0 clear) and has no
// occupant (Tile::settler == 0), and, for class-0 types only, the tile's
// owner byte equals `player`; else 0. thiscall, ret 0x10; EAX is 0/1.
// (Older notes used a tile base of 0x67440, which put these fields at
// +0xe, +0x4 and +0xc; the absolute addresses are the same.)
//
// CGameData::SettlerGroup (0x005038e0): (settler) -> 3 for type 0x29; 1 for
// classes 50..59; for class 0: 2 if the settler's job (word +0xe) is 0xab
// or 0xac, else 1; 2 for all other classes. thiscall, ret 4; EAX is the
// result.

#include "game/tables.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game {

namespace {

using layout::Settler;
using layout::Tile;

// int32 per settler type, in S3.EXE's read-only data.

Tile& TileRecord(CGameData* world, std::uint32_t x, std::uint32_t y) {
    // The original's arithmetic: ((x * 0x300 + y) * 24), 32-bit (wraps).
    return TileAtOffset(world, TileOffset(x, y));
}

}  // namespace

using CanEnterFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x,
                                              std::uint32_t y, std::uint32_t player,
                                              std::uint32_t settlerType);
using SettlerGroupFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx,
                                                  std::uint32_t settler);

// Purpose: Whether a settler may step onto a tile: it must not be blocked or occupied, and ordinary
// settlers must stay on their own player's land (soldiers and some others may leave it).
std::uint32_t __fastcall CGameData_CanSettlerEnterTile(CGameData* world, void* /*edx*/,
                                                       std::uint32_t x, std::uint32_t y,
                                                       std::uint32_t player,
                                                       std::uint32_t settlerType) {
    const Tile& tile = TileRecord(world, x, y);
    if ((tile.flags & layout::kTileBlocked) != 0 || tile.settler != 0) {
        return 0;
    }
    if (g_settlerTypeClass[settlerType] != 0) {
        return 1;
    }
    return tile.owner == player ? 1 : 0;
}

// Purpose: Sorts a settler into a broad group (for example civilians, soldiers, special units). Probably
// used to decide which rules apply to it, e.g. how it moves or what it may enter.
std::uint32_t __fastcall CGameData_SettlerGroup(CGameData* world, void* /*edx*/,
                                                std::uint32_t settler) {
    const Settler& record = world->settlers[settler];
    const std::uint8_t type = record.art;
    if (type == 0x29) {
        return 3;
    }
    const std::int32_t typeClass = g_settlerTypeClass[type];
    if (typeClass >= 50 && typeClass <= 59) {
        return 1;
    }
    if (typeClass != 0) {
        return 2;
    }
    const std::uint16_t job = record.job;
    return (job == 0xab || job == 0xac) ? 2 : 1;
}

S3RE_HOOK(0x00503760, CGameData_CanSettlerEnterTile, CanEnterFn,
          Original_CGameData_CanSettlerEnterTile, CGameData_CanSettlerEnterTile);
S3RE_HOOK(0x005038e0, CGameData_SettlerGroup, SettlerGroupFn, Original_CGameData_SettlerGroup,
          CGameData_SettlerGroup);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestMapSize = 384;
constexpr std::size_t kMapWorld = offsetof(CGameData, tiles) + kTestMapSize * kTileRowBytes;
constexpr std::uint32_t kTestSettlers = 4096;
constexpr std::size_t kSettlerWorld =
    offsetof(CGameData, settlers) + kTestSettlers * sizeof(Settler) + 0x100;
constexpr std::uint32_t kTestTypes = 48;  // the class table's real entries

void CompareCanEnter(test::Context& ctx, CanEnterFn candidate, int cases) {
    test::WorldPair worlds(ctx, kMapWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t x = ctx.rng().Below(kTestMapSize);
        const std::uint32_t y = ctx.rng().Below(kTestMapSize);
        const std::uint32_t player = ctx.rng().Below(20);
        const std::uint32_t type = ctx.rng().Below(kTestTypes);
        // Make every branch likely: free tiles, own territory.
        if (ctx.rng().Chance(2, 3)) {
            worlds.Both([&](CGameData* w) { w->tiles[x][y].settler = 0; });
        }
        if (ctx.rng().Chance(2, 3)) {
            worlds.Both([&](CGameData* w) { w->tiles[x][y].flags &= 0xfe; });
        }
        if (ctx.rng().Chance(1, 2)) {
            worlds.Both([&](CGameData* w) { w->tiles[x][y].owner = static_cast<std::uint8_t>(player); });
        }
        const std::uint32_t expected =
            Original_CGameData_CanSettlerEnterTile(worlds.original(), nullptr, x, y, player, type);
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
std::uint32_t __fastcall WrongCanEnter(CGameData* world, void* /*edx*/, std::uint32_t x,
                                       std::uint32_t y, std::uint32_t /*player*/,
                                       std::uint32_t /*settlerType*/) {
    const Tile& tile = TileRecord(world, x, y);
    return ((tile.flags & layout::kTileBlocked) != 0 || tile.settler != 0) ? 0 : 1;
}

void CompareSettlerGroup(test::Context& ctx, SettlerGroupFn candidate, int cases) {
    test::WorldPair worlds(ctx, kSettlerWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t settler = ctx.rng().Below(kTestSettlers);
        std::uint8_t type = static_cast<std::uint8_t>(ctx.rng().Below(kTestTypes));
        if (ctx.rng().Chance(1, 10)) {
            type = 0x29;
        }
        worlds.Both([&](CGameData* w) { w->settlers[settler].art = type; });
        if (ctx.rng().Chance(1, 3)) {
            const auto job = static_cast<std::uint16_t>(0xaa + ctx.rng().Below(4));
            worlds.Both([&](CGameData* w) { w->settlers[settler].job = job; });
        }
        const std::uint32_t expected =
            Original_CGameData_SettlerGroup(worlds.original(), nullptr, settler);
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
std::uint32_t __fastcall WrongSettlerGroup(CGameData* world, void* edx, std::uint32_t settler) {
    const Settler& record = world->settlers[settler];
    if (record.art != 0x29 && g_settlerTypeClass[record.art] == 0 && record.job == 0xac) {
        return 1;
    }
    return CGameData_SettlerGroup(world, edx, settler);
}

}  // namespace

S3RE_TEST(CGameData_CanSettlerEnterTile_MatchesOriginal) {
    CompareCanEnter(ctx, &CGameData_CanSettlerEnterTile, 200000);
}

S3RE_TEST(CGameData_CanSettlerEnterTile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareCanEnter(probe, &WrongCanEnter, 20000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CGameData_SettlerGroup_MatchesOriginal) {
    CompareSettlerGroup(ctx, &CGameData_SettlerGroup, 200000);
}

S3RE_TEST(CGameData_SettlerGroup_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSettlerGroup(probe, &WrongSettlerGroup, 20000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
