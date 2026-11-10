// Mine and terrain helpers of the settler jobs (UpdateSettlers callees,
// package B).
//
// GameWorld::TileSlope (0x004faa70): (x, y) -> the steepness at a tile:
//   the largest absolute height difference (tile byte +0x4) between
//   opposite neighbours: (x+1, y) / (x-1, y), (x, y+1) / (x, y-1),
//   (x+1, y+1) / (x-1, y-1). 0 unless 1 <= x, y < map size - 1 (unsigned;
//   map size 0 makes the limit 0xffffffff). Callers compare it with 7
//   (UpdateSettlers' work jobs, the AI 0x5540f0, 0x599a30). ret 8.
// GameWorld::MinerJobEnter (0x004f9a00): (settler): a miner goes into his
//   mine: job (+0xe) by the mine's building type: 7 -> 0x55, 8 -> 0x56,
//   9 -> 0x57, 0x25 -> 0xd8, 0x1d and 0x38 -> 0xd9, others unchanged
//   (jump table); the building's byte +0x19 += 1 (miners inside?), byte
//   +0x1a = 0, words +0x30/+0x32 = 0; the settler leaves its tile (tile
//   settler word = 0), counter +0xc = 0, byte +0x17 = 10. EAX = world +
//   settler * 0x40 (the original's register; UpdateSettlers overwrites
//   it). ret 4. Caller: UpdateSettlers (job of settler type 0xa).
// thiscall (ECX = world).

#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game {

using TileSlopeFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);

// Purpose: How steep the land is at a tile (largest height step across it), e.g. to decide whether a
// tile can be worked or built on (callers accept up to 7).
std::uint32_t __fastcall GameWorld_TileSlope(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t limit = world->map_size - 1;
    if (x < 1 || x >= limit || y < 1 || y >= limit) {
        return 0;
    }
    const std::uint32_t at = TileOffset(x, y);
    auto height = [&](std::uint32_t step) -> std::int32_t { return TileAtOffset(world, at + step).height; };
    const std::int32_t a = height(kTileRowBytes) - height(0u - kTileRowBytes);
    const std::int32_t b = height(kTileBytes) - height(0u - kTileBytes);
    const std::int32_t c = height(kTileRowBytes + kTileBytes) - height(0u - kTileRowBytes - kTileBytes);
    std::int32_t slope = a < 0 ? -a : a;
    const std::int32_t absB = b < 0 ? -b : b;
    const std::int32_t absC = c < 0 ? -c : c;
    if (slope < absB) {
        slope = absB;
    }
    if (slope < absC) {
        slope = absC;
    }
    return static_cast<std::uint32_t>(slope);
}

using SettlerFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t settler);

// Purpose: A miner enters his mine and starts the work cycle that fits the mine type (coal, iron,
// gold, stone, sulphur...); he disappears from the map while inside.
std::uint32_t __fastcall GameWorld_MinerJobEnter(GameWorld* world, void*, std::uint32_t settler) {
    layout::Settler& s = world->settlers[settler];
    switch (world->buildings[s.building].art) {
    case 0x07: s.job = 0x55; break;
    case 0x08: s.job = 0x56; break;
    case 0x09: s.job = 0x57; break;
    case 0x25: s.job = 0xd8; break;
    case 0x1d:
    case 0x38: s.job = 0xd9; break;
    default: break;
    }
    layout::Building& b = world->buildings[s.building];
    b.unk19 = static_cast<std::uint8_t>(b.unk19 + 1);
    world->buildings[s.building].unk1a = 0;
    world->buildings[s.building].work_x = 0;
    world->buildings[s.building].work_y = 0;
    TileAtOffset(world, TileOffset(s.x, s.y)).settler = 0;
    s.counter = 0;
    s.unk17 = 10;
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world)) + settler * sizeof(layout::Settler);
}

S3RE_HOOK(0x004f9a00, GameWorld_MinerJobEnter, SettlerFn, Original_GameWorld_MinerJobEnter, GameWorld_MinerJobEnter);
S3RE_HOOK(0x004faa70, GameWorld_TileSlope, TileSlopeFn, Original_GameWorld_TileSlope, GameWorld_TileSlope);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kTestWorld = offsetof(GameWorld, tiles) + 400 * sizeof(GameWorld::tiles[0]);

// Wrong: leaves out the diagonal pair.
std::uint32_t __fastcall WrongTileSlope(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y) {
    if (x >= 1 && y >= 1 && x + 1 < world->map_size && y + 1 < world->map_size) {
        layout::Tile& t = world->tiles[x + 1][y + 1];
        const std::uint8_t saved = t.height;
        t.height = world->tiles[x - 1][y - 1].height;
        const std::uint32_t result = GameWorld_TileSlope(world, edx, x, y);
        t.height = saved;
        return result;
    }
    return GameWorld_TileSlope(world, edx, x, y);
}

void CompareTileSlope(test::Context& ctx, TileSlopeFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = ctx.rng().Chance(1, 10) ? ctx.rng().Below(5) : 384;
        // Map size 0 lets any x, y pass: keep them on the buffer then.
        const bool wild = size != 0 && ctx.rng().Chance(1, 10);
        const std::uint32_t x = wild ? ctx.rng().Next32() : ctx.rng().Below(386);
        const std::uint32_t y = wild && ctx.rng().Chance(1, 2) ? ctx.rng().Next32() : ctx.rng().Below(386);
        worlds.Both([&](GameWorld* w) { w->map_size = size; });
        if (x >= 1 && x < 384 && y >= 1 && y < 384) {
            const std::uint32_t base = ctx.rng().Below(256);
            for (std::int32_t dx = -1; dx <= 1; ++dx) {
                for (std::int32_t dy = -1; dy <= 1; ++dy) {
                    const auto h = static_cast<std::uint8_t>(base + ctx.rng().Below(20));
                    worlds.Both([&](GameWorld* w) { w->tiles[x + dx][y + dy].height = h; });
                }
            }
        }
        const std::uint32_t expected = Original_GameWorld_TileSlope(worlds.original(), nullptr, x, y);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, x, y);
        if (!S3RE_EXPECT_EQ(expected, actual)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%u, %u) map %u", i, x, y, size);
            return;
        }
    }
    S3RE_EXPECT(worlds.Checkpoint(ctx));
}

// Wrong: type 0x38 gets job 0xd8.
std::uint32_t __fastcall WrongMinerJobEnter(GameWorld* world, void* edx, std::uint32_t settler) {
    const std::uint32_t result = GameWorld_MinerJobEnter(world, edx, settler);
    layout::Settler& s = world->settlers[settler];
    if (world->buildings[s.building].art == 0x38) {
        s.job = 0xd8;
    }
    return result;
}

void CompareMinerJobEnter(test::Context& ctx, SettlerFn candidate, int cases) {
    test::WorldPair worlds(ctx, offsetof(GameWorld, buildings) + 0x101 * sizeof(layout::Building));
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t settler = ctx.rng().Below(0x7d00);
        const std::uint32_t building = ctx.rng().Below(0x100);
        static constexpr std::uint8_t kArts[] = {7, 8, 9, 0x1d, 0x25, 0x38, 6, 0x0a, 0x39, 0x24};
        const std::uint8_t art = ctx.rng().Chance(1, 10) ? static_cast<std::uint8_t>(ctx.rng().Next32())
                                                          : kArts[ctx.rng().Below(10)];
        const auto x = static_cast<std::uint16_t>(ctx.rng().Below(384));
        const auto y = static_cast<std::uint16_t>(ctx.rng().Below(384));
        worlds.Both([&](GameWorld* w) {
            w->settlers[settler].building = static_cast<std::uint16_t>(building);
            w->settlers[settler].x = x;
            w->settlers[settler].y = y;
            w->buildings[building].art = art;
        });
        const std::uint32_t expected = Original_GameWorld_MinerJobEnter(worlds.original(), nullptr, settler) -
                                       static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(worlds.original()));
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, settler) -
                                     static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(worlds.ours()));
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 256 == 255 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 255 before it): settler %u art 0x%x", i, settler,
                     art);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(GameWorld_MinerJobEnter_MatchesOriginal) {
    CompareMinerJobEnter(ctx, &GameWorld_MinerJobEnter, 20000);
}
S3RE_TEST(GameWorld_MinerJobEnter_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMinerJobEnter(probe, &WrongMinerJobEnter, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_TileSlope_MatchesOriginal) {
    CompareTileSlope(ctx, &GameWorld_TileSlope, 20000);
}
S3RE_TEST(GameWorld_TileSlope_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareTileSlope(probe, &WrongTileSlope, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
