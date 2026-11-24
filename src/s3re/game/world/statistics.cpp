// The statistics kept at the start of the world object (see world.h):
// per player and category, a history of 256 byte samples (0..100), newest
// first, which the statistics window charts (CGameData::BuildStatistics-
// Chart 0x4ff550 smooths it with the 13-entry kernel).
//
// CGameData::InitStatisticsKernel (0x004ff4c0): stores the kernel
// 3, 6, 8, 9, 9, 10, 10, 10, 9, 9, 8, 6, 3 (sums to 100) at world+0x4.
// thiscall, no arguments, plain ret. Called once, from world setup.
//
// CGameData::ShiftStatistics (0x004ff470): for every player whose record
// flag (int32 at world+0x11943dc + player*0x148) is non-zero, moves all 36
// histories one sample back (the oldest drops out) and sets the newest
// sample to 0. thiscall, no arguments, plain ret.
//
// CGameData::AddStatistic (0x004ff510): (player, category, amount) adds
// `amount` to the newest sample of that history, capped at 100. The cap test
// is done in unsigned 32-bit arithmetic on sample + amount, the store in
// 8 bits. thiscall, three stack arguments, ret 0xc; callers don't use EAX.
//
// CGameData::BuildStatisticsChart (0x004ff550): (player, category) thiscall,
// ret 8. Builds the curve the statistics window draws (world+0x2d038, 256
// uint16) from one history: every non-zero sample s at position i adds s *
// kernel[k] (16-bit products and sums of the low words) to the entries i +
// k - 6 that lie in 0..255. The first six entries, whose kernel is cut off
// at the newest end, are scaled up: entry j = entry * 100 / (100 - the
// kernel weights that fell off) (unsigned 32-bit division; the right end is
// not corrected). chart_max (+0x2d238) = the largest entry; the first axis
// scale of g_chartAxisScales whose top (value * 100 >> 16, signed) + 1 is at
// least that maximum is chosen (past the 14th the bytes after the table, as
// the original), and its five values * 100 >> 16 go to chart_axis
// (+0x2d23a); if it is the first scale, chart_max becomes its top. Neither
// argument is range-checked. EAX not used.

#include "game/tables.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

using layout::kGoodsTypes;
using layout::kPlayers;

// statistics[kPlayers][kGoodsTypes][kStatisticsSamples]: 36 categories per
// player (the same count as goods types).
constexpr std::uint32_t kStatisticsSamples = 256;
static_assert(sizeof(CGameData::statistics[0][0]) == kStatisticsSamples);

std::uint8_t* History(CGameData* world, std::uint32_t player, std::uint32_t category) {
    // The original computes the offset with 32-bit wrap-around.
    const std::uint32_t row = (player * static_cast<std::uint32_t>(kGoodsTypes) + category) * kStatisticsSamples;
    return &world->statistics[0][0][0] + row;
}

}  // namespace

using WorldMethodFn = void(__fastcall*)(CGameData* world, void* edx);
using AddStatisticFn = void(__fastcall*)(CGameData* world, void* edx, std::uint32_t player,
                                         std::uint32_t category, std::uint32_t amount);

// Purpose: Fills in the small table of weights used to smooth the curves in the statistics screen. Done
// once when the world is set up.
void __fastcall CGameData_InitStatisticsKernel(CGameData* world, void* /*edx*/) {
    static constexpr std::int32_t kKernel[13] = {3, 6, 8, 9, 9, 10, 10, 10, 9, 9, 8, 6, 3};
    static_assert(sizeof(kKernel) == sizeof(world->statistics_kernel));
    std::memcpy(world->statistics_kernel, kKernel, sizeof(kKernel));
}

S3RE_HOOK(0x004ff4c0, CGameData_InitStatisticsKernel, WorldMethodFn,
          Original_CGameData_InitStatisticsKernel, CGameData_InitStatisticsKernel);

// Purpose: Moves every player's statistics history one sample back to make room for a new sample. This is
// what makes the statistics graphs scroll over time.
void __fastcall CGameData_ShiftStatistics(CGameData* world, void* /*edx*/) {
    for (std::uint32_t player = 0; player < kPlayers; ++player) {
        if (world->players[player].active == 0) {
            continue;
        }
        for (std::uint32_t category = 0; category < kGoodsTypes; ++category) {
            std::uint8_t* samples = History(world, player, category);
            std::memmove(samples + 1, samples, kStatisticsSamples - 1);
            samples[0] = 0;
        }
    }
}

S3RE_HOOK(0x004ff470, CGameData_ShiftStatistics, WorldMethodFn,
          Original_CGameData_ShiftStatistics, CGameData_ShiftStatistics);

// Purpose: Adds to the current statistics sample of a player (e.g. goods produced), capped at 100. Feeds
// the statistics screen.
void __fastcall CGameData_AddStatistic(CGameData* world, void* /*edx*/, std::uint32_t player,
                                       std::uint32_t category, std::uint32_t amount) {
    std::uint8_t& sample = *History(world, player, category);
    if (sample + amount > 100u) {
        sample = 100;
    } else {
        sample = static_cast<std::uint8_t>(sample + amount);
    }
}

S3RE_HOOK(0x004ff510, CGameData_AddStatistic, AddStatisticFn, Original_CGameData_AddStatistic,
          CGameData_AddStatistic);

namespace {

constexpr std::int32_t kKernelTaps = 13;
constexpr std::int32_t kKernelCentre = 6;    // the kernel covers samples i - 6 .. i + 6
constexpr std::uint32_t kKernelTotal = 100;  // the weights sum to 100
constexpr std::size_t kAxisScales = 14;
constexpr std::size_t kAxisValues = 5;

// A 16.16 axis value times 100, in whole units (signed shift).
std::int16_t AxisUnits(std::int32_t value) {
    return static_cast<std::int16_t>(static_cast<std::int32_t>(static_cast<std::uint32_t>(value) * 100u) >> 16);
}

// The largest maximum a scale's top fits: (value * 100 - 100) >> 16, + 1.
std::int32_t AxisLimit(std::int32_t top) {
    return (static_cast<std::int32_t>(static_cast<std::uint32_t>(top) * 100u - 100u) >> 16) + 1;
}

}  // namespace

using ChartFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t player, std::uint32_t category);

// Purpose: Prepares the curve and the axis of the statistics window for one
// player and category (smoothed history of the last 256 samples).
std::uint32_t __fastcall CGameData_BuildStatisticsChart(CGameData* world, void*, std::uint32_t player,
                                                        std::uint32_t category) {
    std::memset(world->chart, 0, sizeof(world->chart));
    const std::uint8_t* samples = History(world, player, category);
    for (std::uint32_t i = 0; i < kStatisticsSamples; ++i) {
        if (samples[i] == 0) {
            continue;
        }
        for (std::int32_t k = 0; k < kKernelTaps; ++k) {
            const std::uint32_t target = i + static_cast<std::uint32_t>(k - kKernelCentre);
            if (target < kStatisticsSamples) {
                // 16-bit multiply of the sample by the weight's low word.
                const auto weight = static_cast<std::uint16_t>(world->statistics_kernel[k]);
                world->chart[target] = static_cast<std::uint16_t>(world->chart[target] + samples[i] * weight);
            }
        }
    }
    // 0x4ff5ce: the newest entries lost the weights of samples before 0.
    for (std::int32_t j = 0; j < kKernelCentre; ++j) {
        std::uint32_t weights = kKernelTotal;
        for (std::int32_t k = 0; k < kKernelCentre - j; ++k) {
            weights -= static_cast<std::uint32_t>(world->statistics_kernel[k]);
        }
        world->chart[j] = static_cast<std::uint16_t>(world->chart[j] * 100u / weights);
    }
    world->chart_max = 0;
    for (const std::uint16_t value : world->chart) {
        if (value > world->chart_max) {
            world->chart_max = value;
        }
    }
    // 0x4ff63d: the first scale whose top fits.
    const std::int32_t* scales = &g_chartAxisScales[0][0];
    std::size_t scale = 0;
    while (scale < kAxisScales && world->chart_max > AxisLimit(scales[scale * kAxisValues])) {
        ++scale;
    }
    if (scale == 0) {
        world->chart_max = static_cast<std::uint16_t>(AxisUnits(scales[0]));
    }
    // (scale 14 reads past the table, as the original.)
    for (std::size_t v = 0; v < kAxisValues; ++v) {
        world->chart_axis[v] = AxisUnits(scales[scale * kAxisValues + v]);
    }
    return 0;
}

S3RE_HOOK(0x004ff550, CGameData_BuildStatisticsChart, ChartFn, Original_CGameData_BuildStatisticsChart,
          CGameData_BuildStatisticsChart);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kHistoryEnd = offsetof(CGameData, statistics) + sizeof(CGameData::statistics);
constexpr std::size_t kSmallWorld = kHistoryEnd + 0x100;  // kernel + histories
constexpr std::size_t kLargeWorld =                        // up to the player flags
    offsetof(CGameData, players) + sizeof(CGameData::players) + 0x100;

using test::WorldBuffer;

void CompareInitKernel(test::Context& ctx, WorldMethodFn candidate, int cases) {
    std::vector<std::uint8_t> a(0x80);
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(a.data(), a.size());
        std::vector<std::uint8_t> b = a;
        Original_CGameData_InitStatisticsKernel(reinterpret_cast<CGameData*>(a.data()), nullptr);
        candidate(reinterpret_cast<CGameData*>(b.data()), nullptr);
        if (!S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size())) {
            return;
        }
    }
}

void __fastcall WrongInitKernel(CGameData* world, void* edx) {
    CGameData_InitStatisticsKernel(world, edx);
    world->statistics_kernel[12] = 4;
}

void CompareShiftStatistics(test::Context& ctx, WorldMethodFn candidate, int cases) {
    WorldBuffer a(kLargeWorld);
    WorldBuffer b(kLargeWorld);
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(a.data(), kSmallWorld);
        for (std::uint32_t player = 0; player < kPlayers; ++player) {
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
            a.world()->players[player].active = flag;
        }
        std::memcpy(b.data(), a.data(), a.size());
        Original_CGameData_ShiftStatistics(a.world(), nullptr);
        candidate(b.world(), nullptr);
        if (!S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size())) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

// Wrong: shifts the histories of inactive players too.
void __fastcall WrongShiftStatistics(CGameData* world, void* /*edx*/) {
    for (std::uint32_t player = 0; player < kPlayers; ++player) {
        for (std::uint32_t category = 0; category < kGoodsTypes; ++category) {
            std::uint8_t* samples = History(world, player, category);
            std::memmove(samples + 1, samples, kStatisticsSamples - 1);
            samples[0] = 0;
        }
    }
}

void CompareAddStatistic(test::Context& ctx, AddStatisticFn candidate, int cases) {
    WorldBuffer a(kSmallWorld);
    WorldBuffer b(kSmallWorld);
    ctx.rng().Fill(a.data(), a.size());
    std::memcpy(b.data(), a.data(), a.size());
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t player = ctx.rng().Below(kPlayers);
        const std::uint32_t category = ctx.rng().Below(kGoodsTypes);
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
        Original_CGameData_AddStatistic(a.world(), nullptr, player, category, amount);
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
void __fastcall WrongAddStatistic(CGameData* world, void* /*edx*/, std::uint32_t player,
                                  std::uint32_t category, std::uint32_t amount) {
    std::uint8_t& sample = *History(world, player, category);
    const std::int32_t sum = static_cast<std::int32_t>(sample + amount);
    sample = sum > 100 ? 100 : static_cast<std::uint8_t>(sum);
}

void CompareStatisticsChart(test::Context& ctx, ChartFn candidate, int cases) {
    test::WorldPair worlds(ctx, offsetof(CGameData, chart_selection));
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t player = ctx.rng().Below(kPlayers);
        const std::uint32_t category = ctx.rng().Below(kGoodsTypes);
        // The game's kernel, or random weights whose cut-off sums stay below 100.
        std::int32_t kernel[13] = {3, 6, 8, 9, 9, 10, 10, 10, 9, 9, 8, 6, 3};
        if (ctx.rng().Chance(1, 3)) {
            for (std::int32_t k = 0; k < 13; ++k) {
                // (high bits only where they don't reach the divisor)
                const bool high = k >= 6 && ctx.rng().Chance(1, 4);
                kernel[k] = static_cast<std::int32_t>(ctx.rng().Below(16) | (high ? ctx.rng().Next32() & 0xffff0000u : 0u));
            }
        }
        std::uint8_t samples[kStatisticsSamples];
        const std::uint32_t kind = ctx.rng().Below(5);  // 4: (almost) empty, fits the first axis scale
        for (std::uint8_t& s : samples) {
            s = kind == 4 ? static_cast<std::uint8_t>(ctx.rng().Chance(1, 100) ? 1 : 0)
              : kind == 0 ? static_cast<std::uint8_t>(ctx.rng().Next32())
              : kind == 1 ? (ctx.rng().Chance(1, 3) ? static_cast<std::uint8_t>(ctx.rng().Below(101)) : 0)
                          : static_cast<std::uint8_t>(ctx.rng().Below(kind == 2 ? 101 : 8));
        }
        worlds.Both([&](CGameData* w) {
            std::memcpy(w->statistics_kernel, kernel, sizeof(kernel));
            std::memcpy(History(w, player, category), samples, sizeof(samples));
        });
        Original_CGameData_BuildStatisticsChart(worlds.original(), nullptr, player, category);
        candidate(worlds.ours(), nullptr, player, category);
        if (!worlds.Same(ctx, offsetof(CGameData, chart), offsetof(CGameData, chart_selection) - offsetof(CGameData, chart)) ||
            (i % 64 == 63 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: player %u, category %u, kind %u", i, player, category, kind);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

// Wrong: the first six entries are not scaled up (recomputed as plain sums).
std::uint32_t __fastcall WrongStatisticsChart(CGameData* world, void* edx, std::uint32_t player,
                                              std::uint32_t category) {
    const std::uint32_t r = CGameData_BuildStatisticsChart(world, edx, player, category);
    const std::uint8_t* samples = History(world, player, category);
    for (std::uint32_t j = 0; j < 6; ++j) {
        std::uint16_t sum = 0;
        for (std::int32_t k = 0; k < 13; ++k) {
            const std::uint32_t source = j - static_cast<std::uint32_t>(k - 6);
            if (source < kStatisticsSamples) {
                sum = static_cast<std::uint16_t>(sum + samples[source] * static_cast<std::uint16_t>(world->statistics_kernel[k]));
            }
        }
        world->chart[j] = sum;
    }
    return r;
}

}  // namespace

S3RE_TEST(CGameData_BuildStatisticsChart_MatchesOriginal) {
    CompareStatisticsChart(ctx, &CGameData_BuildStatisticsChart, 3000);
}

S3RE_TEST(CGameData_BuildStatisticsChart_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareStatisticsChart(probe, &WrongStatisticsChart, 300);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CGameData_InitStatisticsKernel_MatchesOriginal) {
    CompareInitKernel(ctx, &CGameData_InitStatisticsKernel, 100);
}

S3RE_TEST(CGameData_InitStatisticsKernel_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareInitKernel(probe, &WrongInitKernel, 10);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CGameData_ShiftStatistics_MatchesOriginal) {
    CompareShiftStatistics(ctx, &CGameData_ShiftStatistics, 20);
}

S3RE_TEST(CGameData_ShiftStatistics_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareShiftStatistics(probe, &WrongShiftStatistics, 3);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CGameData_AddStatistic_MatchesOriginal) {
    CompareAddStatistic(ctx, &CGameData_AddStatistic, 200000);
}

S3RE_TEST(CGameData_AddStatistic_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareAddStatistic(probe, &WrongAddStatistic, 20000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
