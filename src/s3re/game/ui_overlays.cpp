// Map overlays drawn by the logic thread: marks on tiles that the drawing
// code shows (for example the working area of a selected building). They
// take the logic thread in ECX and reach the world through this+0x3a0. A
// mark is the current search generation in tile byte +0xb (so a new
// generation clears all marks at once, see GameWorld::NextSearchGeneration)
// plus a value in tile byte +0xc.
//
// CLogicThread::DrawOverlayLine (0x00523560): (x0, y0, x1, y1, value):
//   marks both end tiles (no map check), then n = TileDistance(x0, y0, x1,
//   y1); for k = 1..n (only if n is 1 or more as a signed int32) the tile
//   (x0 + k*(x1-x0)/n, y0 + k*(y1-y0)/n) (signed division, rounding to
//   zero; 32-bit sums) if it is on the map (signed compares with the map
//   size). Value is stored as a byte. EAX (one caller tail-returns it) =
//   n if n <= 0 as an int32, else 0. ret 0x14.
// CLogicThread::DrawWorkAreaOverlay (0x00523840): (building): starts a new
//   search generation, then marks four rings around the building: the
//   building type (byte +0x6) gives an offset table entry N (type 2 and 3:
//   3000, 5 and 0x33: 1500, 0x18: 300, 0x1e: 800, 0xe: 150, 0x24: 400,
//   0x19: 2000; other types draw nothing); r = the table's dword +0x8 of
//   entry N, minus 1 (the offset table at world+0x110a588 has 16-byte
//   entries: int32 dx, int32 dy, dword +0x8 nondecreasing, probably a
//   distance). The centre is the word pair +0x30/+0x32 when byte +0x21
//   (buildup) is 0, else +0xe/+0x14. Going through the entries while their
//   +0x8 value is at most r: an entry whose value equals the current
//   threshold (r/4, then 2r/4, 3r/4, 4r/4: each time an entry's value is
//   above it, the threshold moves on, the ring number k = 1..4 goes up and
//   that entry is skipped) marks centre + (dx, dy) with value k, if that
//   tile is on the map (unsigned compares) and in the same area (tile word
//   +0x14) as the building's tile (+0xa/+0xc). EAX (one caller
//   tail-returns it) = the world pointer. ret 4.
// CLogicThread::DrawBuildingRouteOverlay (0x005236b0): (building): starts a
//   new search generation and draws up to four DrawOverlayLine segments
//   (values 1..4) through the points of the building's extra record (uint16
//   +0x34 = record number; 0x5a-byte records at world+0x10f45e8, see
//   GameWorld::AllocBuildingExtra): points 1..3 x at +0x2/+0x4/+0x6, y at
//   +0xa/+0xc/+0xe, 0xffff = not set (skipped); the end point x +0x8, y
//   +0x10 (nothing is drawn if its x is 0xffff). The line starts at the
//   building's word pair +0x2/+0x4 for type (+0x6) 0x22, +0x30/+0x32 for
//   type 0x21, and otherwise (an original quirk, probably never reached) at
//   (building, building). Probably a trade route (types 0x21/0x22 would be
//   the trading buildings). Callers (ProcessEvents, ExecuteCommands)
//   tail-return EAX, but theirs is ignored or overwritten: not reproduced.
//   ret 4.

#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>

namespace s3re::game {

struct CLogicThread;  // opaque

namespace {

constexpr std::uint32_t kLogicThreadWorld = 0x3a0;
constexpr std::uint32_t kTiles = 0x67444;
constexpr std::uint32_t kTileMark = 0xb;
constexpr std::uint32_t kTileMarkValue = 0xc;
constexpr std::uint32_t kTileArea = 0x14;
constexpr std::uint32_t kOffsetTable = 0x110a588;
constexpr std::uint32_t kOffsetTableBytes = 16;
constexpr std::uint32_t kOffsetTableValue = 0x8;
constexpr std::uint32_t kBuildings = 0x1499e64;
constexpr std::uint32_t kBuildingBytes = 0x3a;
constexpr std::uint32_t kBuildingType = 0x6;
constexpr std::uint32_t kBuildingX = 0xa;
constexpr std::uint32_t kBuildingY = 0xc;
constexpr std::uint32_t kBuildingBuildup = 0x21;
constexpr std::uint32_t kBuildingExtraNumber = 0x34;
constexpr std::uint32_t kBuildingExtras = 0x10f45e8;
constexpr std::uint32_t kBuildingExtraBytes = 0x5a;
constexpr std::uint16_t kNoPoint = 0xffff;

template <typename T>
T& At(GameWorld* world, std::uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<std::uint8_t*>(world) + offset);
}

GameWorld* WorldOf(CLogicThread* self) {
    return *reinterpret_cast<GameWorld**>(reinterpret_cast<std::uint8_t*>(self) + kLogicThreadWorld);
}

std::uint32_t OverlayTile(std::uint32_t x, std::uint32_t y) {
    return kTiles + (x * 0x300 + y) * 24;
}

void MarkOverlayTile(GameWorld* world, std::uint32_t tile, std::uint32_t value) {
    At<std::uint8_t>(world, tile + kTileMark) = At<std::uint8_t>(world, world_offset::kSearchGeneration);
    At<std::uint8_t>(world, tile + kTileMarkValue) = static_cast<std::uint8_t>(value);
}

// The offset table entry whose distance gives the working area of a building type; 0 = none.
std::uint32_t WorkAreaEntry(std::uint32_t type) {
    switch (type) {
    case 0x02: return 3000;
    case 0x03: return 3000;
    case 0x05: return 1500;
    case 0x0e: return 150;
    case 0x18: return 300;
    case 0x19: return 2000;
    case 0x1e: return 800;
    case 0x24: return 400;
    case 0x33: return 1500;
    default: return 0;
    }
}

}  // namespace

using LineFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx, std::uint32_t x0, std::uint32_t y0,
                                          std::uint32_t x1, std::uint32_t y1, std::uint32_t value);
using WorkAreaFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx, std::uint32_t building);
using RouteFn = void(__fastcall*)(CLogicThread* self, void* edx, std::uint32_t building);

// Purpose: Display only: marks the tiles along a straight line on the map (for an overlay the drawing code
// shows, e.g. probably a connection between two places).
std::uint32_t __fastcall CLogicThread_DrawOverlayLine(CLogicThread* self, void*, std::uint32_t x0, std::uint32_t y0,
                                                      std::uint32_t x1, std::uint32_t y1, std::uint32_t value) {
    GameWorld* world = WorldOf(self);
    MarkOverlayTile(world, OverlayTile(x0, y0), value);
    MarkOverlayTile(world, OverlayTile(x1, y1), value);
    const std::uint32_t steps = TileDistance(x0, y0, x1, y1);
    if (static_cast<std::int32_t>(steps) < 1) {
        return steps;
    }
    const std::uint32_t dx = x1 - x0;
    const std::uint32_t dy = y1 - y0;
    std::uint32_t sumX = dx;
    std::uint32_t sumY = dy;
    const auto n = static_cast<std::int32_t>(steps);
    for (std::uint32_t k = steps; k != 0; --k) {
        const auto x = static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(sumX) / n) + x0);
        const auto y = static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(sumY) / n) + y0);
        const auto size = At<std::int32_t>(world, world_offset::kMapSize);
        if (x >= 0 && x < size && y >= 0 && y < size) {
            MarkOverlayTile(world, OverlayTile(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)), value);
        }
        sumX += dx;
        sumY += dy;
    }
    return 0;
}

// Purpose: Display only: when a building is selected, marks four rings around it showing how far its
// workers go (e.g. woodcutter, fisher, hunter); only tiles in the building's own area are marked.
std::uint32_t __fastcall CLogicThread_DrawWorkAreaOverlay(CLogicThread* self, void*, std::uint32_t building) {
    GameWorld* world = WorldOf(self);
    GameWorld_NextSearchGeneration(world, nullptr);
    const auto result = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));
    const std::uint32_t record = kBuildings + building * kBuildingBytes;
    const std::uint32_t entry = WorkAreaEntry(At<std::uint8_t>(world, record + kBuildingType));
    if (entry == 0) {
        return result;
    }
    auto value = [&](std::uint32_t i) {
        return At<std::uint32_t>(world, kOffsetTable + i * kOffsetTableBytes + kOffsetTableValue);
    };
    const std::uint32_t radius = value(entry) - 1;
    std::uint32_t cx = 0;
    std::uint32_t cy = 0;
    if (At<std::uint8_t>(world, record + kBuildingBuildup) == 0) {
        cx = At<std::uint16_t>(world, record + 0x30);
        cy = At<std::uint16_t>(world, record + 0x32);
    } else {
        cx = At<std::uint16_t>(world, record + 0xe);
        cy = At<std::uint16_t>(world, record + 0x14);
    }
    std::uint32_t sum = radius;
    std::uint32_t threshold = radius >> 2;
    std::uint32_t ring = 1;
    for (std::uint32_t i = 0; value(i) <= radius; ++i) {
        const std::uint32_t v = value(i);
        if (v < threshold) {
            continue;
        }
        if (v > threshold) {
            sum += radius;
            ++ring;
            threshold = sum >> 2;
            continue;
        }
        const std::uint32_t x = cx + At<std::uint32_t>(world, kOffsetTable + i * kOffsetTableBytes);
        const std::uint32_t y = cy + At<std::uint32_t>(world, kOffsetTable + i * kOffsetTableBytes + 4);
        const std::uint32_t size = At<std::uint32_t>(world, world_offset::kMapSize);
        if (x >= size || y >= size) {
            continue;
        }
        const std::uint32_t home =
            OverlayTile(At<std::uint16_t>(world, record + kBuildingX), At<std::uint16_t>(world, record + kBuildingY));
        if (At<std::uint16_t>(world, OverlayTile(x, y) + kTileArea) == At<std::uint16_t>(world, home + kTileArea)) {
            MarkOverlayTile(world, OverlayTile(x, y), ring);
        }
    }
    return result;
}

// Purpose: Display only: when a trading building (probably market or port) is selected, draws its route
// as lines on the map: from the building through up to three waypoints to the destination.
void __fastcall CLogicThread_DrawBuildingRouteOverlay(CLogicThread* self, void*, std::uint32_t building) {
    GameWorld* world = WorldOf(self);
    const std::uint32_t record = kBuildings + building * kBuildingBytes;
    const std::uint8_t type = At<std::uint8_t>(world, record + kBuildingType);
    std::uint32_t x = building;
    std::uint32_t y = building;
    if (type == 0x22) {
        x = At<std::uint16_t>(world, record + 0x2);
        y = At<std::uint16_t>(world, record + 0x4);
    } else if (type == 0x21) {
        x = At<std::uint16_t>(world, record + 0x30);
        y = At<std::uint16_t>(world, record + 0x32);
    }
    const std::uint32_t extra =
        kBuildingExtras + static_cast<std::uint32_t>(At<std::uint16_t>(world, record + kBuildingExtraNumber)) *
                              kBuildingExtraBytes;
    GameWorld_NextSearchGeneration(world, nullptr);
    if (At<std::uint16_t>(world, extra + 0x8) == kNoPoint) {
        return;
    }
    for (std::uint32_t point = 0; point < 3; ++point) {
        const std::uint16_t px = At<std::uint16_t>(world, extra + 0x2 + point * 2);
        if (px == kNoPoint) {
            continue;
        }
        const std::uint32_t py = At<std::uint16_t>(world, extra + 0xa + point * 2);
        CLogicThread_DrawOverlayLine(self, nullptr, x, y, px, py, point + 1);
        x = px;
        y = py;
    }
    CLogicThread_DrawOverlayLine(self, nullptr, x, y, At<std::uint16_t>(world, extra + 0x8),
                                 At<std::uint16_t>(world, extra + 0x10), 4);
}

S3RE_HOOK(0x00523560, CLogicThread_DrawOverlayLine, LineFn, Original_CLogicThread_DrawOverlayLine,
          CLogicThread_DrawOverlayLine);
S3RE_HOOK(0x00523840, CLogicThread_DrawWorkAreaOverlay, WorkAreaFn, Original_CLogicThread_DrawWorkAreaOverlay,
          CLogicThread_DrawWorkAreaOverlay);
S3RE_HOOK(0x005236b0, CLogicThread_DrawBuildingRouteOverlay, RouteFn, Original_CLogicThread_DrawBuildingRouteOverlay,
          CLogicThread_DrawBuildingRouteOverlay);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestBuildings = 0x100;
constexpr std::size_t kTestWorld = kBuildings + (kTestBuildings + 1) * kBuildingBytes;
constexpr std::uint32_t kTableEntries = 3200;
constexpr std::uint32_t kTestExtras = 64;
static_assert(kBuildingExtras + (kTestExtras + 1) * kBuildingExtraBytes < kTestWorld, "extras inside the test world");

// Two fake logic threads, each pointing at one of the worlds.
class ThreadPair {
public:
    explicit ThreadPair(test::WorldPair& worlds) {
        *reinterpret_cast<GameWorld**>(a_ + kLogicThreadWorld) = worlds.original();
        *reinterpret_cast<GameWorld**>(b_ + kLogicThreadWorld) = worlds.ours();
    }
    CLogicThread* original() { return reinterpret_cast<CLogicThread*>(a_); }
    CLogicThread* ours() { return reinterpret_cast<CLogicThread*>(b_); }

private:
    std::uint8_t a_[kLogicThreadWorld + 4] = {};
    std::uint8_t b_[kLogicThreadWorld + 4] = {};
};

// Wrong: marks with the value + 1.
std::uint32_t __fastcall WrongDrawOverlayLine(CLogicThread* self, void* edx, std::uint32_t x0, std::uint32_t y0,
                                              std::uint32_t x1, std::uint32_t y1, std::uint32_t value) {
    return CLogicThread_DrawOverlayLine(self, edx, x0, y0, x1, y1, value + 1);
}

void CompareLine(test::Context& ctx, LineFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    ThreadPair threads(worlds);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = ctx.rng().Chance(1, 2) ? 384 : 1 + ctx.rng().Below(384);
        worlds.Set<std::uint32_t>(world_offset::kMapSize, ctx.rng().Chance(1, 50) ? ctx.rng().Next32() : size);
        // End points on the map (they are marked without a check), the line
        // sometimes leaving a smaller map; rarely a pair so far apart that
        // the distance is negative as an int32 (nothing but the end tiles).
        std::uint32_t x0 = ctx.rng().Below(384), y0 = ctx.rng().Below(384);
        std::uint32_t x1 = ctx.rng().Below(384), y1 = ctx.rng().Below(384);
        if (ctx.rng().Chance(1, 4)) {
            x1 = x0 + ctx.rng().Below(12) - ctx.rng().Below(12);
            y1 = y0 + ctx.rng().Below(12) - ctx.rng().Below(12);
            x1 = x1 > 383 ? x0 : x1;
            y1 = y1 > 383 ? y0 : y1;
        }
        if (ctx.rng().Chance(1, 40)) {
            // A far end point whose tile address wraps around to (x0, y0)
            // (0x50000000 * 0x4800 and 0x60000000 * 24 are 0 mod 2^32), at a
            // distance of 2^31 or more: only the end tiles are marked.
            x1 = x0 + 0x50000000u;
            y1 = y0 - 0x60000000u;
        }
        const std::uint32_t value = ctx.rng().Next32();
        const std::uint32_t expected =
            Original_CLogicThread_DrawOverlayLine(threads.original(), nullptr, x0, y0, x1, y1, value);
        const std::uint32_t actual = candidate(threads.ours(), nullptr, x0, y0, x1, y1, value);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 64 == 63 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 63 before it): (%u, %u) -> (%u, %u)", i, x0, y0, x1, y1);
            return;
        }
    }
}

// Wrong: the rings are numbered from 0.
std::uint32_t __fastcall WrongDrawWorkAreaOverlay(CLogicThread* self, void* edx, std::uint32_t building) {
    const std::uint32_t result = CLogicThread_DrawWorkAreaOverlay(self, edx, building);
    GameWorld* world = WorldOf(self);
    const std::uint32_t size = At<std::uint32_t>(world, world_offset::kMapSize);
    for (std::uint32_t x = 0; x < size; ++x) {
        for (std::uint32_t y = 0; y < size; ++y) {
            const std::uint32_t tile = OverlayTile(x, y);
            if (At<std::uint8_t>(world, tile + kTileMark) == At<std::uint8_t>(world, world_offset::kSearchGeneration) &&
                At<std::uint8_t>(world, tile + kTileMarkValue) >= 1 && At<std::uint8_t>(world, tile + kTileMarkValue) <= 4) {
                --At<std::uint8_t>(world, tile + kTileMarkValue);
            }
        }
    }
    return result;
}

void CompareWorkArea(test::Context& ctx, WorkAreaFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    ThreadPair threads(worlds);
    // An offset table like the real one: values nondecreasing (steps of 0..2),
    // offsets growing with them.
    std::uint32_t v = 0;
    for (std::uint32_t i = 0; i < kTableEntries; ++i) {
        v += ctx.rng().Below(8) < 5 ? 0 : 1 + ctx.rng().Below(2);
        const std::int32_t reach = 1 + static_cast<std::int32_t>(i / 60);
        worlds.Set<std::int32_t>(kOffsetTable + i * kOffsetTableBytes, ctx.rng().Range(-reach, reach));
        worlds.Set<std::int32_t>(kOffsetTable + i * kOffsetTableBytes + 4, ctx.rng().Range(-reach, reach));
        worlds.Set<std::uint32_t>(kOffsetTable + i * kOffsetTableBytes + kOffsetTableValue, v);
    }
    // Areas: blobs of 0..2 in 8x8 blocks.
    for (std::uint32_t x = 0; x < 384; ++x) {
        for (std::uint32_t y = 0; y < 384; ++y) {
            worlds.Set<std::uint16_t>(OverlayTile(x, y) + kTileArea, static_cast<std::uint16_t>(((x / 8) * 7 + (y / 8) * 3) % 3));
        }
    }
    static constexpr std::uint8_t kTypes[] = {2, 3, 5, 0xe, 0x18, 0x19, 0x1e, 0x24, 0x33, 0, 1, 0x30, 0xff};
    for (int i = 0; i < cases; ++i) {
        worlds.Set<std::uint32_t>(world_offset::kMapSize, ctx.rng().Chance(2, 3) ? 384 : 1 + ctx.rng().Below(384));
        worlds.Set<std::uint8_t>(world_offset::kSearchGeneration,
                                 static_cast<std::uint8_t>(ctx.rng().Chance(1, 20) ? 0xff : ctx.rng().Below(255)));
        const std::uint32_t building = 1 + ctx.rng().Below(kTestBuildings);
        const std::uint32_t record = kBuildings + building * kBuildingBytes;
        worlds.Set<std::uint8_t>(record + kBuildingType, kTypes[ctx.rng().Below(sizeof(kTypes))]);
        worlds.Set<std::uint8_t>(record + kBuildingBuildup, static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Next32()));
        static constexpr std::uint32_t kFields[] = {0x30, 0x32, 0xe, 0x14, kBuildingX, kBuildingY};
        for (const std::uint32_t field : kFields) {
            worlds.Set<std::uint16_t>(record + field, static_cast<std::uint16_t>(ctx.rng().Below(384)));
        }
        const std::uint32_t expected =
            Original_CLogicThread_DrawWorkAreaOverlay(threads.original(), nullptr, building) -
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(worlds.original()));
        const std::uint32_t actual = candidate(threads.ours(), nullptr, building) -
                                     static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(worlds.ours()));
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 16 == 15 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 15 before it): building %u", i, building);
            return;
        }
    }
}

// Wrong: skips the second waypoint.
void __fastcall WrongDrawBuildingRouteOverlay(CLogicThread* self, void* edx, std::uint32_t building) {
    GameWorld* world = WorldOf(self);
    const std::uint32_t number = At<std::uint16_t>(world, kBuildings + building * kBuildingBytes + kBuildingExtraNumber);
    const std::uint32_t extra = kBuildingExtras + number * kBuildingExtraBytes;
    const std::uint16_t saved = At<std::uint16_t>(world, extra + 0x4);
    At<std::uint16_t>(world, extra + 0x4) = kNoPoint;
    CLogicThread_DrawBuildingRouteOverlay(self, edx, building);
    At<std::uint16_t>(world, extra + 0x4) = saved;
}

void CompareRoute(test::Context& ctx, RouteFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    ThreadPair threads(worlds);
    static constexpr std::uint8_t kTypes[] = {0x21, 0x22, 0x21, 0x22, 0x21, 0x22, 0, 0x20, 0xff};
    for (int i = 0; i < cases; ++i) {
        worlds.Set<std::uint32_t>(world_offset::kMapSize, ctx.rng().Chance(2, 3) ? 384 : 1 + ctx.rng().Below(384));
        worlds.Set<std::uint8_t>(world_offset::kSearchGeneration,
                                 static_cast<std::uint8_t>(ctx.rng().Chance(1, 20) ? 0xff : ctx.rng().Below(255)));
        const std::uint32_t building = 1 + ctx.rng().Below(kTestBuildings);
        const std::uint32_t record = kBuildings + building * kBuildingBytes;
        worlds.Set<std::uint8_t>(record + kBuildingType, kTypes[ctx.rng().Below(sizeof(kTypes))]);
        static constexpr std::uint32_t kFields[] = {0x2, 0x4, 0x30, 0x32};
        for (const std::uint32_t field : kFields) {
            worlds.Set<std::uint16_t>(record + field, static_cast<std::uint16_t>(ctx.rng().Below(384)));
        }
        const std::uint32_t number = 1 + ctx.rng().Below(kTestExtras);
        worlds.Set<std::uint16_t>(record + kBuildingExtraNumber, static_cast<std::uint16_t>(number));
        const std::uint32_t extra = kBuildingExtras + number * kBuildingExtraBytes;
        for (std::uint32_t point = 0; point < 4; ++point) {
            const bool unset = ctx.rng().Chance(1, point == 3 ? 10 : 3);
            worlds.Set<std::uint16_t>(extra + 0x2 + point * 2,
                                      unset ? kNoPoint : static_cast<std::uint16_t>(ctx.rng().Below(384)));
            worlds.Set<std::uint16_t>(extra + 0xa + point * 2, static_cast<std::uint16_t>(ctx.rng().Below(384)));
        }
        Original_CLogicThread_DrawBuildingRouteOverlay(threads.original(), nullptr, building);
        candidate(threads.ours(), nullptr, building);
        if ((i % 16 == 15 || i == cases - 1) && !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 15 before it): building %u", i, building);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(CLogicThread_DrawBuildingRouteOverlay_MatchesOriginal) {
    CompareRoute(ctx, &CLogicThread_DrawBuildingRouteOverlay, 800);
}
S3RE_TEST(CLogicThread_DrawBuildingRouteOverlay_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareRoute(probe, &WrongDrawBuildingRouteOverlay, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CLogicThread_DrawOverlayLine_MatchesOriginal) {
    CompareLine(ctx, &CLogicThread_DrawOverlayLine, 5000);
}
S3RE_TEST(CLogicThread_DrawOverlayLine_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareLine(probe, &WrongDrawOverlayLine, 200);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CLogicThread_DrawWorkAreaOverlay_MatchesOriginal) {
    CompareWorkArea(ctx, &CLogicThread_DrawWorkAreaOverlay, 500);
}
S3RE_TEST(CLogicThread_DrawWorkAreaOverlay_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareWorkArea(probe, &WrongDrawWorkAreaOverlay, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
