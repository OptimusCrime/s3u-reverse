// Walking settlers (UpdateSettlers' walking jobs) and the job lists the
// pathfinder uses.
//
// GameWorld::AddTileTraffic (0x00503940): (x, y): unless the tile's traffic
//   byte (+0x12) is 0xff it is increased by one; when it reaches 12 or more,
//   GameWorld::WearPath (0x51b7f0) is called for the tile (which adds 9 when
//   the tile becomes a path). thiscall, ret 8. EAX = the address of the
//   traffic byte, or WearPath's EAX when it was called (the only caller,
//   WalkSettler 0x500860, ignores it).
// GameWorld::IsWalkingJob (0x004fa6d0): (job) -> AL = 1 for the 47 jobs that
//   UpdateSettlers hands to WalkSettler (handler 0x4ead27 and the two jobs
//   0x35 and 0xb0 that call it too), else 0. The upper 24 bits of EAX are
//   those of the argument. ret 4, ECX unused. Called by 0x4a33d0.
// GameWorld::IsStationaryJob (0x004fa880, was Logic_sub_4fa880, proposed
//   "IsWalkingOrIdleJob"): (job) -> AL = 0 for the same 47 walking jobs and
//   the idle jobs 1 and 0x37, AL = 1 for every other job (the settler is
//   working at a place). Upper EAX bits as above. ret 4, ECX unused. Called
//   by Pathing::TileCost (0x4a36a0): a tile whose settler is busy counts as
//   blocked.

#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game {

namespace {

using layout::Tile;

constexpr std::uint16_t kWalkingJobs[] = {
    0x002, 0x003, 0x006, 0x00a, 0x00c, 0x00f, 0x011, 0x01c, 0x01d, 0x02a, 0x02b, 0x02d,
    0x030, 0x035, 0x04c, 0x04e, 0x068, 0x090, 0x093, 0x098, 0x09c, 0x0ab, 0x0b0, 0x0b2,
    0x0b5, 0x0bb, 0x0bc, 0x0e5, 0x0e8, 0x0ed, 0x0f1, 0x0f6, 0x0f9, 0x0fe, 0x102, 0x133,
    0x135, 0x140, 0x141, 0x147, 0x148, 0x149, 0x14a, 0x165, 0x168, 0x16a, 0x16d};
static_assert(sizeof(kWalkingJobs) / sizeof(kWalkingJobs[0]) == 47);

bool InWalkingList(std::uint32_t job) {
    for (std::uint16_t walking : kWalkingJobs) {
        if (job == walking) {
            return true;
        }
    }
    return false;
}

}  // namespace

using TileFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);
using JobFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t job);

// Purpose: Counts a settler's step onto a tile; tiles walked on often enough become paths.
std::uint32_t __fastcall GameWorld_AddTileTraffic(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    Tile& tile = TileAtOffset(world, TileOffset(x, y));
    const std::uint32_t address = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&tile.traffic));
    if (tile.traffic == 0xff) {
        return address;
    }
    ++tile.traffic;
    if (tile.traffic < 12) {
        return address;
    }
    return GameWorld_WearPath(world, nullptr, x, y);
}

// Purpose: Whether a job is one of the walking jobs (the settler is on its way somewhere).
std::uint32_t __fastcall GameWorld_IsWalkingJob(GameWorld*, void*, std::uint32_t job) {
    return (job & 0xffffff00u) | (InWalkingList(job) ? 1u : 0u);
}

// Purpose: Whether a settler with this job stays where it is (it is neither walking nor idle), so
// the pathfinder treats its tile as blocked.
std::uint32_t __fastcall GameWorld_IsStationaryJob(GameWorld*, void*, std::uint32_t job) {
    const bool moving = InWalkingList(job) || job == 1 || job == 0x37;
    return (job & 0xffffff00u) | (moving ? 0u : 1u);
}

S3RE_HOOK(0x00503940, GameWorld_AddTileTraffic, TileFn, Original_GameWorld_AddTileTraffic,
          GameWorld_AddTileTraffic);
S3RE_HOOK(0x004fa6d0, GameWorld_IsWalkingJob, JobFn, Original_GameWorld_IsWalkingJob, GameWorld_IsWalkingJob);
S3RE_HOOK(0x004fa880, GameWorld_IsStationaryJob, JobFn, Original_GameWorld_IsStationaryJob,
          GameWorld_IsStationaryJob);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestMapSize = 64;
constexpr std::size_t kMapWorld = offsetof(GameWorld, tiles) + kTestMapSize * kTileRowBytes;

std::uint32_t Relative(std::uint32_t value, GameWorld* world) {
    return value - static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));
}

void CompareAddTileTraffic(test::Context& ctx, TileFn candidate, int cases) {
    test::WorldPair worlds(ctx, kMapWorld);
    static constexpr std::uint8_t kTerrains[] = {0x10, 0x12, 0x13, 0x18, 0x1c, 0x16, 0x05};
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t x = 2 + ctx.rng().Below(kTestMapSize - 4);
        const std::uint32_t y = 2 + ctx.rng().Below(kTestMapSize - 4);
        const std::uint32_t pick = ctx.rng().Below(8);
        const std::uint8_t traffic = pick < 3   ? static_cast<std::uint8_t>(11)
                                     : pick < 4 ? static_cast<std::uint8_t>(0xff)
                                     : pick < 5 ? static_cast<std::uint8_t>(0xfe)
                                                : static_cast<std::uint8_t>(ctx.rng().Below(256));
        worlds.Both([&](GameWorld* w) { w->tiles[x][y].traffic = traffic; });
        for (std::int32_t dx = -1; dx <= 1; ++dx) {
            for (std::int32_t dy = -1; dy <= 1; ++dy) {
                const std::uint8_t terrain = kTerrains[ctx.rng().Below(7)];
                worlds.Both([&](GameWorld* w) { w->tiles[x + dx][y + dy].terrain = terrain; });
            }
        }
        const std::uint32_t expected =
            Relative(Original_GameWorld_AddTileTraffic(worlds.original(), nullptr, x, y), worlds.original());
        const std::uint32_t actual = Relative(candidate(worlds.ours(), nullptr, x, y), worlds.ours());
        if (!S3RE_EXPECT_EQ(expected, actual) || (i % 512 == 511 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 511 before it): (%u, %u)", i, x, y);
            return;
        }
    }
    S3RE_EXPECT(worlds.Checkpoint(ctx));
}

void CompareJobFn(test::Context& ctx, JobFn original, JobFn candidate) {
    for (std::uint32_t i = 0; i < 0x20000; ++i) {
        const std::uint32_t job = i < 0x400 ? i : (i < 0x800 ? (ctx.rng().Next32() & 0xffff0000u) | (i & 0x1ff)
                                                             : ctx.rng().Next32());
        const std::uint32_t expected = original(nullptr, nullptr, job);
        const std::uint32_t actual = candidate(nullptr, nullptr, job);
        if (!S3RE_EXPECT_EQ(expected, actual)) {
            ctx.Fail(__FILE__, __LINE__, "  job 0x%x", job);
            return;
        }
    }
}

// Wrong: the traffic limit is checked before the increment.
std::uint32_t __fastcall WrongAddTileTraffic(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    Tile& tile = world->tiles[x][y];
    if (tile.traffic == 11) {
        ++tile.traffic;
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&tile.traffic));
    }
    return GameWorld_AddTileTraffic(world, nullptr, x, y);
}

// Wrong: forgets the idle job 0x37.
std::uint32_t __fastcall WrongIsStationaryJob(GameWorld* world, void*, std::uint32_t job) {
    return job == 0x37 ? 1 : GameWorld_IsStationaryJob(world, nullptr, job);
}

}  // namespace

S3RE_TEST(GameWorld_AddTileTraffic_MatchesOriginal) {
    CompareAddTileTraffic(ctx, &GameWorld_AddTileTraffic, 20000);
}
S3RE_TEST(GameWorld_AddTileTraffic_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareAddTileTraffic(probe, &WrongAddTileTraffic, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_IsWalkingJob_MatchesOriginal) {
    CompareJobFn(ctx, Original_GameWorld_IsWalkingJob, &GameWorld_IsWalkingJob);
}
S3RE_TEST(GameWorld_IsStationaryJob_MatchesOriginal) {
    CompareJobFn(ctx, Original_GameWorld_IsStationaryJob, &GameWorld_IsStationaryJob);
}
S3RE_TEST(GameWorld_IsStationaryJob_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareJobFn(probe, Original_GameWorld_IsStationaryJob, &WrongIsStationaryJob);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
