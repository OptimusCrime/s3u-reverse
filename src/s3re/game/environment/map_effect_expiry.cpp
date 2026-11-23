// Expiry of map effects (the table at world+0x14d2bc8, see map_effects.cpp
// and map_effect_spawns.cpp).
//
// MapEffect::IsExpired (0x005440c0): thiscall on a record, (tick) -> AL:
//   duration (int16 +0x4) != -1: tick >= start_tick + duration (signed);
//   duration == -1 (an open-ended effect): tick >= start_tick + 15. Only AL
//   is defined (the upper bytes hold the duration or start + 15; the one
//   caller tests AL). ret 4.
// MapEffects::ExpireSome (0x00545d40): thiscall on the table, (tick): for
//   block_count_16 cells (signed loop), cursor expire_cursor += 1, back to 1 when
//   it reaches blocks_x * blocks_y + 1 (signed); every effect of that cell
//   list that IsExpired is unlinked (Unlink 0x545e40) and reset (Reset
//   0x543fd0). So the whole map is checked every 16 calls. UpdateEnvironment
//   calls it at the end of every step with g_gameTick. ret 4, EAX not used.

#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

using layout::MapEffect;
using layout::MapEffectTable;

constexpr std::uintptr_t kIsExpired = 0x005440c0;
constexpr std::uintptr_t kUnlink = 0x00545e40;
constexpr std::uintptr_t kReset = 0x00543fd0;

}  // namespace

using IsExpiredFn = std::uint32_t(__fastcall*)(MapEffect* self, void* edx, std::uint32_t tick);
using ExpireFn = void(__fastcall*)(MapEffectTable* self, void* edx, std::uint32_t tick);
using UnlinkFn = std::uint32_t(__fastcall*)(MapEffectTable* self, void* edx, std::uint32_t slot);
using ResetFn = std::uint32_t(__fastcall*)(MapEffect* self, void* edx);

// Purpose: Whether a map effect (e.g. an arrow or smoke) has run its time and should be removed.
std::uint32_t __fastcall MapEffect_IsExpired(MapEffect* self, void*, std::uint32_t tick) {
    // 32-bit wrapping sums, compared signed.
    const auto now = static_cast<std::int32_t>(tick);
    if (self->duration != -1) {
        const auto end = static_cast<std::int32_t>(self->start_tick + static_cast<std::uint32_t>(std::int32_t{self->duration}));
        return now >= end ? 1 : 0;
    }
    return now >= static_cast<std::int32_t>(self->start_tick + 15) ? 1 : 0;
}

// Purpose: Removes finished map effects, checking a sixteenth of the map's blocks per step.
void __fastcall MapEffects_ExpireSome(MapEffectTable* self, void*, std::uint32_t tick) {
    const auto isExpired = reinterpret_cast<IsExpiredFn>(kIsExpired);
    const auto unlink = reinterpret_cast<UnlinkFn>(kUnlink);
    const auto reset = reinterpret_cast<ResetFn>(kReset);
    for (std::int32_t n = 0; n < self->block_count_16; ++n) {
        const std::int32_t cursor = static_cast<std::int32_t>(self->expire_cursor) + 1;
        self->expire_cursor = static_cast<std::uint32_t>(cursor);
        if (cursor >= self->blocks_x * self->blocks_y + 1) {
            self->expire_cursor = 1;
        }
        std::uint32_t slot = self->cell_heads[self->expire_cursor];
        while (slot != 0) {
            MapEffect& record = self->records[slot];
            if ((isExpired(&record, nullptr, tick) & 0xff) != 0) {
                const std::uint32_t next = record.next;
                unlink(self, nullptr, slot);
                reset(&record, nullptr);
                slot = next;
            } else {
                slot = record.next;
            }
        }
    }
}

S3RE_HOOK(0x005440c0, MapEffect_IsExpired, IsExpiredFn, Original_MapEffect_IsExpired, MapEffect_IsExpired);
S3RE_HOOK(0x00545d40, MapEffects_ExpireSome, ExpireFn, Original_MapEffects_ExpireSome, MapEffects_ExpireSome);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// Up to 40 blocks with consistent lists of 0..4 effects each; durations
// around the tick (and -1), the cursor anywhere near the wrap.
std::uint32_t SetUpTable(test::Context& ctx, test::WorldPair& tables) {
    std::memset(tables.originalBytes(), 0, sizeof(MapEffectTable));
    MapEffectTable& t = *reinterpret_cast<MapEffectTable*>(tables.originalBytes());
    t.blocks_x = 1 + static_cast<std::int32_t>(ctx.rng().Below(8));
    t.blocks_y = 1 + static_cast<std::int32_t>(ctx.rng().Below(5));
    const std::int32_t cells = t.blocks_x * t.blocks_y;
    t.block_count_16 = ctx.rng().Chance(1, 8) ? static_cast<std::int32_t>(ctx.rng().Below(3)) - 1
                                              : 1 + static_cast<std::int32_t>(ctx.rng().Below(cells + 3));
    t.expire_cursor = ctx.rng().Below(static_cast<std::uint32_t>(cells) + 2);
    const std::uint32_t tick = 1000 + ctx.rng().Below(100);
    std::uint32_t slot = 1;
    for (std::int32_t cell = 0; cell <= cells && slot < 4900; ++cell) {
        const std::uint32_t count = ctx.rng().Below(5);
        std::uint32_t previous = 0;
        for (std::uint32_t i = 0; i < count; ++i, ++slot) {
            MapEffect& r = t.records[slot];
            ctx.rng().Fill(&r, sizeof(r));
            r.start_tick = tick - ctx.rng().Below(60);
            r.duration = ctx.rng().Chance(1, 4) ? static_cast<std::int16_t>(-1)
                                                : static_cast<std::int16_t>(ctx.rng().Range(-5, 60));
            r.list = static_cast<std::uint32_t>(cell);
            r.previous = previous;
            r.next = 0;
            if (previous != 0) {
                t.records[previous].next = slot;
            } else {
                t.cell_heads[cell] = slot;
            }
            previous = slot;
        }
    }
    std::memcpy(tables.oursBytes(), tables.originalBytes(), sizeof(MapEffectTable));
    return tick;
}

void CompareExpire(test::Context& ctx, ExpireFn candidate, int cases) {
    test::WorldPair tables(ctx, sizeof(MapEffectTable));
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t tick = SetUpTable(ctx, tables);
        Original_MapEffects_ExpireSome(reinterpret_cast<MapEffectTable*>(tables.originalBytes()), nullptr, tick);
        candidate(reinterpret_cast<MapEffectTable*>(tables.oursBytes()), nullptr, tick);
        if (!tables.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: tick %u", i, tick);
            return;
        }
    }
}

// Wrong: the cursor wraps one cell early.
void __fastcall WrongExpireSome(MapEffectTable* self, void*, std::uint32_t tick) {
    if (static_cast<std::int32_t>(self->expire_cursor) + 1 == self->blocks_x * self->blocks_y) {
        self->expire_cursor = 0;
    }
    MapEffects_ExpireSome(self, nullptr, tick);
}

}  // namespace

S3RE_TEST(MapEffect_IsExpired_MatchesOriginal) {
    MapEffect record{};
    for (int i = 0; i < 100000; ++i) {
        record.start_tick = ctx.rng().Chance(1, 2) ? ctx.rng().Next32() : 1000 + ctx.rng().Below(50);
        record.duration = ctx.rng().Chance(1, 4) ? static_cast<std::int16_t>(-1)
                                                 : static_cast<std::int16_t>(ctx.rng().Next32());
        const std::uint32_t tick = ctx.rng().Chance(1, 2) ? ctx.rng().Next32() : 1000 + ctx.rng().Below(100);
        const std::uint32_t expected = Original_MapEffect_IsExpired(&record, nullptr, tick) & 0xff;
        const std::uint32_t actual = MapEffect_IsExpired(&record, nullptr, tick) & 0xff;
        if (!S3RE_EXPECT_EQ(expected, actual)) {
            ctx.Fail(__FILE__, __LINE__, "  start %u, duration %d, tick %u", record.start_tick, record.duration, tick);
            return;
        }
    }
}

S3RE_TEST(MapEffects_ExpireSome_MatchesOriginal) {
    CompareExpire(ctx, &MapEffects_ExpireSome, 300);
}

S3RE_TEST(MapEffects_ExpireSome_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareExpire(probe, &WrongExpireSome, 300);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
