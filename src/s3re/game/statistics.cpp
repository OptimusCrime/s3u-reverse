// The statistics kept at the start of the world object (see world.h):
// per player and category, a history of 256 byte samples (0..100), newest
// first, which the statistics window charts (GameWorld::BuildStatistics-
// Chart 0x4ff550 smooths it with the 13-entry kernel).
//
// GameWorld::InitStatisticsKernel (0x004ff4c0): stores the kernel
// 3, 6, 8, 9, 9, 10, 10, 10, 9, 9, 8, 6, 3 (sums to 100) at world+0x4.
// thiscall, no arguments, plain ret. Called once, from world setup.
//
// GameWorld::ShiftStatistics (0x004ff470): for every player whose record
// flag (int32 at world+0x11943dc + player*0x148) is non-zero, moves all 36
// histories one sample back (the oldest drops out) and sets the newest
// sample to 0. thiscall, no arguments, plain ret.
//
// GameWorld::AddStatistic (0x004ff510): (player, category, amount) adds
// `amount` to the newest sample of that history, capped at 100. The cap test
// is done in unsigned 32-bit arithmetic on sample + amount, the store in
// 8 bits. thiscall, three stack arguments, ret 0xc; callers don't use EAX.

#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

std::uint8_t* History(GameWorld* world, std::uint32_t player, std::uint32_t category) {
    using namespace world_offset;
    // The original computes the offset with 32-bit wrap-around.
    const std::uint32_t row = (player * kStatisticsCategories + category) * kStatisticsSamples;
    return &WorldField<std::uint8_t>(world, kStatisticsHistory) + row;
}

}  // namespace

using WorldMethodFn = void(__fastcall*)(GameWorld* world, void* edx);
using AddStatisticFn = void(__fastcall*)(GameWorld* world, void* edx, std::uint32_t player,
                                         std::uint32_t category, std::uint32_t amount);

// Purpose: Fills in the small table of weights used to smooth the curves in the statistics screen. Done
// once when the world is set up.
void __fastcall GameWorld_InitStatisticsKernel(GameWorld* world, void* /*edx*/) {
    static constexpr std::int32_t kKernel[world_offset::kStatisticsKernelSize] = {
        3, 6, 8, 9, 9, 10, 10, 10, 9, 9, 8, 6, 3};
    std::memcpy(&WorldField<std::int32_t>(world, world_offset::kStatisticsKernel), kKernel,
                sizeof(kKernel));
}

S3RE_HOOK(0x004ff4c0, GameWorld_InitStatisticsKernel, WorldMethodFn,
          Original_GameWorld_InitStatisticsKernel, GameWorld_InitStatisticsKernel);

// Purpose: Moves every player's statistics history one sample back to make room for a new sample. This is
// what makes the statistics graphs scroll over time.
void __fastcall GameWorld_ShiftStatistics(GameWorld* world, void* /*edx*/) {
    using namespace world_offset;
    for (std::uint32_t player = 0; player < kStatisticsPlayers; ++player) {
        if (WorldField<std::int32_t>(world, kPlayerRecords + player * kPlayerRecordBytes) == 0) {
            continue;
        }
        for (std::uint32_t category = 0; category < kStatisticsCategories; ++category) {
            std::uint8_t* samples = History(world, player, category);
            std::memmove(samples + 1, samples, kStatisticsSamples - 1);
            samples[0] = 0;
        }
    }
}

S3RE_HOOK(0x004ff470, GameWorld_ShiftStatistics, WorldMethodFn,
          Original_GameWorld_ShiftStatistics, GameWorld_ShiftStatistics);

// Purpose: Adds to the current statistics sample of a player (e.g. goods produced), capped at 100. Feeds
// the statistics screen.
void __fastcall GameWorld_AddStatistic(GameWorld* world, void* /*edx*/, std::uint32_t player,
                                       std::uint32_t category, std::uint32_t amount) {
    std::uint8_t& sample = *History(world, player, category);
    if (sample + amount > 100u) {
        sample = 100;
    } else {
        sample = static_cast<std::uint8_t>(sample + amount);
    }
}

S3RE_HOOK(0x004ff510, GameWorld_AddStatistic, AddStatisticFn, Original_GameWorld_AddStatistic,
          GameWorld_AddStatistic);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kHistoryEnd =
    world_offset::kStatisticsHistory +
    world_offset::kStatisticsPlayers * world_offset::kStatisticsPlayerBytes;
constexpr std::size_t kSmallWorld = kHistoryEnd + 0x100;  // kernel + histories
constexpr std::size_t kLargeWorld =                        // up to the player flags
    world_offset::kPlayerRecords +
    world_offset::kStatisticsPlayers * world_offset::kPlayerRecordBytes + 0x100;

using test::WorldBuffer;

void CompareInitKernel(test::Context& ctx, WorldMethodFn candidate, int cases) {
    std::vector<std::uint8_t> a(0x80);
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(a.data(), a.size());
        std::vector<std::uint8_t> b = a;
        Original_GameWorld_InitStatisticsKernel(reinterpret_cast<GameWorld*>(a.data()), nullptr);
        candidate(reinterpret_cast<GameWorld*>(b.data()), nullptr);
        if (!S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size())) {
            return;
        }
    }
}

void __fastcall WrongInitKernel(GameWorld* world, void* edx) {
    GameWorld_InitStatisticsKernel(world, edx);
    WorldField<std::int32_t>(world, world_offset::kStatisticsKernel + 4 * 12) = 4;
}

void CompareShiftStatistics(test::Context& ctx, WorldMethodFn candidate, int cases) {
    using namespace world_offset;
    WorldBuffer a(kLargeWorld);
    WorldBuffer b(kLargeWorld);
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(a.data(), kSmallWorld);
        for (std::uint32_t player = 0; player < kStatisticsPlayers; ++player) {
            // Inactive (0), small flags, and values whose low byte is 0.
            std::int32_t flag = 0;
            switch (ctx.rng().Below(4)) {
            case 0:
                flag = 0;
                break;
            case 1:
                flag = 1;
                break;
            case 2:
                flag = static_cast<std::int32_t>(ctx.rng().Below(8) << 8);
                break;
            default:
                flag = static_cast<std::int32_t>(ctx.rng().Next32());
                break;
            }
            WorldField<std::int32_t>(a.world(), kPlayerRecords + player * kPlayerRecordBytes) =
                flag;
        }
        std::memcpy(b.data(), a.data(), a.size());
        Original_GameWorld_ShiftStatistics(a.world(), nullptr);
        candidate(b.world(), nullptr);
        if (!S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size())) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

// Wrong: shifts the histories of inactive players too.
void __fastcall WrongShiftStatistics(GameWorld* world, void* /*edx*/) {
    using namespace world_offset;
    for (std::uint32_t player = 0; player < kStatisticsPlayers; ++player) {
        for (std::uint32_t category = 0; category < kStatisticsCategories; ++category) {
            std::uint8_t* samples = History(world, player, category);
            std::memmove(samples + 1, samples, kStatisticsSamples - 1);
            samples[0] = 0;
        }
    }
}

void CompareAddStatistic(test::Context& ctx, AddStatisticFn candidate, int cases) {
    using namespace world_offset;
    WorldBuffer a(kSmallWorld);
    WorldBuffer b(kSmallWorld);
    ctx.rng().Fill(a.data(), a.size());
    std::memcpy(b.data(), a.data(), a.size());
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t player = ctx.rng().Below(kStatisticsPlayers);
        const std::uint32_t category = ctx.rng().Below(kStatisticsCategories);
        // Samples around the cap, amounts small, large and "negative".
        std::uint8_t* sample = History(a.world(), player, category);
        if (ctx.rng().Chance(1, 2)) {
            *sample = static_cast<std::uint8_t>(ctx.rng().Range(90, 110));
            *History(b.world(), player, category) = *sample;
        }
        std::uint32_t amount;
        switch (ctx.rng().Below(4)) {
        case 0:
            amount = ctx.rng().Next32();
            break;
        case 1:
            amount = 0u - ctx.rng().Below(8);  // -7..0
            break;
        default:
            amount = ctx.rng().Below(16);
            break;
        }
        Original_GameWorld_AddStatistic(a.world(), nullptr, player, category, amount);
        candidate(b.world(), nullptr, player, category, amount);
        // Whole buffer every 64 cases; otherwise the touched row.
        const bool full = i % 64 == 63;
        const std::uint8_t* rowA = History(a.world(), player, category);
        const std::uint8_t* rowB = History(b.world(), player, category);
        const bool same = full ? S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size())
                               : S3RE_EXPECT_MEM_EQ(rowA, rowB, kStatisticsSamples);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: player %u, category %u, amount 0x%08x", i,
                     player, category, amount);
            return;
        }
    }
}

// Wrong: caps with a signed comparison (differs for "negative" amounts).
void __fastcall WrongAddStatistic(GameWorld* world, void* /*edx*/, std::uint32_t player,
                                  std::uint32_t category, std::uint32_t amount) {
    std::uint8_t& sample = *History(world, player, category);
    const std::int32_t sum = static_cast<std::int32_t>(sample + amount);
    sample = sum > 100 ? 100 : static_cast<std::uint8_t>(sum);
}

}  // namespace

S3RE_TEST(GameWorld_InitStatisticsKernel_MatchesOriginal) {
    CompareInitKernel(ctx, &GameWorld_InitStatisticsKernel, 100);
}

S3RE_TEST(GameWorld_InitStatisticsKernel_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareInitKernel(probe, &WrongInitKernel, 10);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_ShiftStatistics_MatchesOriginal) {
    CompareShiftStatistics(ctx, &GameWorld_ShiftStatistics, 20);
}

S3RE_TEST(GameWorld_ShiftStatistics_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareShiftStatistics(probe, &WrongShiftStatistics, 3);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_AddStatistic_MatchesOriginal) {
    CompareAddStatistic(ctx, &GameWorld_AddStatistic, 200000);
}

S3RE_TEST(GameWorld_AddStatistic_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareAddStatistic(probe, &WrongAddStatistic, 20000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
