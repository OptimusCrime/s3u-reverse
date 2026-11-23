// Free-id search in three "in use" byte tables of the world.
//
// Each table is an array of bytes, one per id, non-zero when the id is in
// use, preceded by a uint32 high-water mark (the largest id handed out).
// The functions return the lowest free id in 1..size-2 and raise the mark if
// needed; they don't mark the id as used (the callers do). If there is
// none they return 0; id size-1 is never handed out, even when free.
//
// CGameData::FindFreeId256 (0x0051d5b0): table at +0x0e114dc, mark at
//   +0x0e114d8. Used by the flood fill 0x51a580 for areas (tile byte 0xe
//   bit 0x10), so probably area/region ids.
// CGameData::FindFreeId4096 (0x0051d570): table at +0x0e115e4, mark at
//   +0x0e115e0.
// CGameData::FindFreeId8192 (0x0051c040): table at +0x0e125e8, mark at
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
    std::uint32_t& (*highWaterMark)(CGameData* world);
    std::uint8_t* (*inUse)(CGameData* world);
    std::uint32_t size;
    std::size_t inUseOffset;  // for sizing test buffers
};

constexpr IdTable kIds256{[](CGameData* w) -> std::uint32_t& { return w->walk_region_high_water; },
                          [](CGameData* w) { return w->walk_region_used; }, 0x100,
                          offsetof(CGameData, walk_region_used)};
constexpr IdTable kIds4096{[](CGameData* w) -> std::uint32_t& { return w->ids4096_high_water; },
                           [](CGameData* w) { return w->ids4096_used; }, 0x1000,
                           offsetof(CGameData, ids4096_used)};
constexpr IdTable kIds8192{[](CGameData* w) -> std::uint32_t& { return w->wide_region_high_water; },
                           [](CGameData* w) { return w->wide_region_used; }, 0x2000,
                           offsetof(CGameData, wide_region_used)};
static_assert(sizeof(CGameData::walk_region_used) == 0x100);
static_assert(sizeof(CGameData::ids4096_used) == 0x1000);
static_assert(sizeof(CGameData::wide_region_used) == 0x2000);

std::uint32_t FindFreeId(CGameData* world, const IdTable& table) {
    const std::uint8_t* inUse = table.inUse(world);
    for (std::uint32_t id = 1; id < table.size - 1; ++id) {
        if (inUse[id] == 0) {
            auto& mark = table.highWaterMark(world);
            if (id > mark) {
                mark = id;
            }
            return id;
        }
    }
    return 0;
}

}  // namespace

using FindFreeIdFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx);

// Purpose: Finds an unused number (1..254) for a new area/region and marks it as the highest in use. Used
// when regions are created.
std::uint32_t __fastcall CGameData_FindFreeId256(CGameData* world, void*) {
    return FindFreeId(world, kIds256);
}

// Purpose: Finds an unused number in a table of 4,096 ids (what the ids are for is not known yet).
std::uint32_t __fastcall CGameData_FindFreeId4096(CGameData* world, void*) {
    return FindFreeId(world, kIds4096);
}

// Purpose: Finds an unused number in a table of 8,192 ids, used for the word-16 regions (probably connected
// walkable land).
std::uint32_t __fastcall CGameData_FindFreeId8192(CGameData* world, void*) {
    return FindFreeId(world, kIds8192);
}

S3RE_HOOK(0x0051d5b0, CGameData_FindFreeId256, FindFreeIdFn, Original_CGameData_FindFreeId256,
          CGameData_FindFreeId256);
S3RE_HOOK(0x0051d570, CGameData_FindFreeId4096, FindFreeIdFn, Original_CGameData_FindFreeId4096,
          CGameData_FindFreeId4096);
S3RE_HOOK(0x0051c040, CGameData_FindFreeId8192, FindFreeIdFn, Original_CGameData_FindFreeId8192,
          CGameData_FindFreeId8192);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// Fills the table mostly with used ids, leaving the first free one at a
// random place (often near the end, and sometimes only id size-1 or none),
// with the high-water mark below or above it.
void CompareFindFreeId(test::Context& ctx, const IdTable& table, FindFreeIdFn original,
                       FindFreeIdFn candidate, int cases) {
    test::WorldPair worlds(ctx, table.inUseOffset + table.size + 0x100);
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
            // Ids up to size+3 lie past the table (in the next field), as before.
            worlds.Both([&](CGameData* w) { table.inUse(w)[id] = static_cast<std::uint8_t>(value); });
        }
        const std::uint32_t mark = ctx.rng().Chance(1, 2)
                                       ? firstFree + static_cast<std::uint32_t>(ctx.rng().Range(-2, 2))
                                       : ctx.rng().Next32();
        worlds.Both([&](CGameData* w) { table.highWaterMark(w) = mark; });
        const std::uint32_t expected = original(worlds.original(), nullptr);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.SameField(ctx, [&](CGameData* w) -> auto& { return table.highWaterMark(w); }) ||
            (i % 64 == 63 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: first free %u, mark %u", i, firstFree, mark);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

// Wrong: also hands out the last id.
std::uint32_t __fastcall WrongFindFreeId256(CGameData* world, void*) {
    return FindFreeId(world, IdTable{kIds256.highWaterMark, kIds256.inUse, kIds256.size + 1, kIds256.inUseOffset});
}

}  // namespace

S3RE_TEST(CGameData_FindFreeId256_MatchesOriginal) {
    CompareFindFreeId(ctx, kIds256, Original_CGameData_FindFreeId256, &CGameData_FindFreeId256,
                      5000);
}
S3RE_TEST(CGameData_FindFreeId256_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareFindFreeId(probe, kIds256, Original_CGameData_FindFreeId256, &WrongFindFreeId256,
                      2000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_FindFreeId4096_MatchesOriginal) {
    CompareFindFreeId(ctx, kIds4096, Original_CGameData_FindFreeId4096,
                      &CGameData_FindFreeId4096, 2000);
}
S3RE_TEST(CGameData_FindFreeId8192_MatchesOriginal) {
    CompareFindFreeId(ctx, kIds8192, Original_CGameData_FindFreeId8192,
                      &CGameData_FindFreeId8192, 1000);
}

}  // namespace s3re::game
