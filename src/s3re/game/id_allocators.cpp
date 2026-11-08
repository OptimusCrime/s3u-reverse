// Free-id search in three "in use" byte tables of the world.
//
// Each table is an array of bytes, one per id, non-zero when the id is in
// use, preceded by a uint32 high-water mark (the largest id handed out).
// The functions return the lowest free id in 1..size-2 and raise the mark if
// needed; they don't mark the id as used (the callers do). If there is
// none they return 0; id size-1 is never handed out, even when free.
//
// GameWorld::FindFreeId256 (0x0051d5b0): table at +0x0e114dc, mark at
//   +0x0e114d8. Used by the flood fill 0x51a580 for areas (tile byte 0xe
//   bit 0x10), so probably area/region ids.
// GameWorld::FindFreeId4096 (0x0051d570): table at +0x0e115e4, mark at
//   +0x0e115e0.
// GameWorld::FindFreeId8192 (0x0051c040): table at +0x0e125e8, mark at
//   +0x0e125e4.
// All: thiscall (ECX = world), no arguments, ret 0, EAX = the id or 0.

#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>

namespace s3re::game {

namespace {

struct IdTable {
    std::size_t highWaterMark;
    std::size_t inUse;
    std::uint32_t size;
};

constexpr IdTable kIds256{0xe114d8, 0xe114dc, 0x100};
constexpr IdTable kIds4096{0xe115e0, 0xe115e4, 0x1000};
constexpr IdTable kIds8192{0xe125e4, 0xe125e8, 0x2000};

std::uint32_t FindFreeId(GameWorld* world, const IdTable& table) {
    const std::uint8_t* inUse = &WorldField<std::uint8_t>(world, table.inUse);
    for (std::uint32_t id = 1; id < table.size - 1; ++id) {
        if (inUse[id] == 0) {
            auto& mark = WorldField<std::uint32_t>(world, table.highWaterMark);
            if (id > mark) {
                mark = id;
            }
            return id;
        }
    }
    return 0;
}

}  // namespace

using FindFreeIdFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx);

// Purpose: Finds an unused number (1..254) for a new area/region and marks it as the highest in use. Used
// when regions are created.
std::uint32_t __fastcall GameWorld_FindFreeId256(GameWorld* world, void*) {
    return FindFreeId(world, kIds256);
}

// Purpose: Finds an unused number in a table of 4,096 ids (what the ids are for is not known yet).
std::uint32_t __fastcall GameWorld_FindFreeId4096(GameWorld* world, void*) {
    return FindFreeId(world, kIds4096);
}

// Purpose: Finds an unused number in a table of 8,192 ids, used for the word-16 regions (probably connected
// walkable land).
std::uint32_t __fastcall GameWorld_FindFreeId8192(GameWorld* world, void*) {
    return FindFreeId(world, kIds8192);
}

S3RE_HOOK(0x0051d5b0, GameWorld_FindFreeId256, FindFreeIdFn, Original_GameWorld_FindFreeId256,
          GameWorld_FindFreeId256);
S3RE_HOOK(0x0051d570, GameWorld_FindFreeId4096, FindFreeIdFn, Original_GameWorld_FindFreeId4096,
          GameWorld_FindFreeId4096);
S3RE_HOOK(0x0051c040, GameWorld_FindFreeId8192, FindFreeIdFn, Original_GameWorld_FindFreeId8192,
          GameWorld_FindFreeId8192);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// Fills the table mostly with used ids, leaving the first free one at a
// random place (often near the end, and sometimes only id size-1 or none),
// with the high-water mark below or above it.
void CompareFindFreeId(test::Context& ctx, const IdTable& table, FindFreeIdFn original,
                       FindFreeIdFn candidate, int cases) {
    test::WorldPair worlds(ctx, table.inUse + table.size + 0x100);
    for (int i = 0; i < cases; ++i) {
        std::uint32_t firstFree;
        switch (ctx.rng().Below(4)) {
        case 0:
            firstFree = table.size - 1 - ctx.rng().Below(3);
            break;
        case 1:
            firstFree = table.size + ctx.rng().Below(4);  // none free in range
            break;
        default:
            firstFree = 1 + ctx.rng().Below(table.size);
            break;
        }
        for (std::uint32_t id = 0; id < table.size + 4; ++id) {
            const auto used = static_cast<std::uint8_t>(1 + ctx.rng().Below(255));
            const bool free = id == firstFree || (id > firstFree && ctx.rng().Chance(1, 2));
            const std::uint32_t value = free ? 0 : (id == 0 ? ctx.rng().Below(2) : used);
            worlds.Set<std::uint8_t>(table.inUse + id, static_cast<std::uint8_t>(value));
        }
        const std::uint32_t mark = ctx.rng().Chance(1, 2)
                                       ? firstFree + static_cast<std::uint32_t>(ctx.rng().Range(-2, 2))
                                       : ctx.rng().Next32();
        worlds.Set<std::uint32_t>(table.highWaterMark, mark);
        const std::uint32_t expected = original(worlds.original(), nullptr);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Same(ctx, table.highWaterMark, 4) ||
            (i % 64 == 63 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: first free %u, mark %u", i, firstFree, mark);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

// Wrong: also hands out the last id.
std::uint32_t __fastcall WrongFindFreeId256(GameWorld* world, void*) {
    return FindFreeId(world, IdTable{kIds256.highWaterMark, kIds256.inUse, kIds256.size + 1});
}

}  // namespace

S3RE_TEST(GameWorld_FindFreeId256_MatchesOriginal) {
    CompareFindFreeId(ctx, kIds256, Original_GameWorld_FindFreeId256, &GameWorld_FindFreeId256,
                      5000);
}
S3RE_TEST(GameWorld_FindFreeId256_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareFindFreeId(probe, kIds256, Original_GameWorld_FindFreeId256, &WrongFindFreeId256,
                      2000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_FindFreeId4096_MatchesOriginal) {
    CompareFindFreeId(ctx, kIds4096, Original_GameWorld_FindFreeId4096,
                      &GameWorld_FindFreeId4096, 2000);
}
S3RE_TEST(GameWorld_FindFreeId8192_MatchesOriginal) {
    CompareFindFreeId(ctx, kIds8192, Original_GameWorld_FindFreeId8192,
                      &GameWorld_FindFreeId8192, 1000);
}

}  // namespace s3re::game
