// Settler targets and searches for settlers near a tile.
//
// Settler records: 0x40 bytes each at world+0x12a5e60 (+0x0 player, +0x1
// type, +0x4/+0x6 position, +0x10 next settler in a list, +0x19 a flag
// byte, +0x24/+0x26 target position, +0x2c a byte set to 1 with a new
// target, +0x30..+0x3b six uint16 cleared to 0xffff with it (probably a
// cached path)). Tile (x, y) at world+0x67444 + (x*0x300 + y)*24, its
// uint16 +0x0 is the settler on it.
//
// GameWorld::SetSettlerTarget (0x00503610): (settler, x, y): the target
//   becomes (x, y) (low words) if GameWorld::IsOnMap(x, y), else the
//   settler's own position; then the six words +0x30..+0x3a = 0xffff, byte
//   +0x2c = 1, and byte +0x19 = 1 if it was 0. 45 call sites. EAX (some
//   callers only overwrite AL/AX) = world + settler*0x40, reproduced.
//   ret 0xc.
// GameWorld::CountListSettlersNear (0x0050e8f0): (first, maxDistance, x,
//   y, value) -> how many settlers of the list starting at `first`
//   (linked by +0x10, ends at 0) stand on a tile whose byte +0x10 equals
//   `value` (zero-extended, 32-bit compare) and have TileDistance(x, y,
//   settler position) < maxDistance (unsigned). 0 if first is 0. Called by
//   PrepareDrawData. ret 0x14.
// GameWorld::FindNearbySettlerType29 (0x0050cdb0): (settler, x, y): looks at
//   the tiles (x+dx, y+dy) for the entries 1..36 of the offset table at
//   world+0x110a588 (int32 dx, dy; 16-byte stride; the rings around a
//   tile), skipping those off the map (GameWorld::IsOnMap), and stops at
//   the first one whose settler is non-zero, of type 0x29 and of the same
//   player as `settler`. Returns, as the original does, the settler word of
//   the last tile on the map it looked at: the match if there was one,
//   otherwise whatever settler (or 0) stood on the last tile looked at, or
//   x itself if none of the tiles was on the map. Caller 0x50e9f0.
//   ret 0xc.
// All thiscall (ECX = world).

#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>

namespace s3re::game {

namespace {

constexpr std::uint32_t kTiles = 0x67444;
constexpr std::uint32_t kTileSettler = 0x0;
constexpr std::uint32_t kTileByte10 = 0x10;

constexpr std::uint32_t kSettlers = 0x12a5e60;
constexpr std::uint32_t kSettlerBytes = 0x40;
constexpr std::uint32_t kSettlerPlayer = 0x0;
constexpr std::uint32_t kSettlerType = 0x1;
constexpr std::uint32_t kSettlerX = 0x4;
constexpr std::uint32_t kSettlerY = 0x6;
constexpr std::uint32_t kSettlerNext = 0x10;
constexpr std::uint32_t kSettlerByte19 = 0x19;
constexpr std::uint32_t kSettlerTargetX = 0x24;
constexpr std::uint32_t kSettlerTargetY = 0x26;
constexpr std::uint32_t kSettlerNewTarget = 0x2c;
constexpr std::uint32_t kSettlerPath = 0x30;  // 6 x uint16
constexpr std::uint32_t kSettlerPathWords = 6;
constexpr std::uint8_t kSettlerType29 = 0x29;

constexpr std::uint32_t kOffsetTable = 0x110a588;  // int32 dx, int32 dy, 16-byte stride
constexpr std::uint32_t kOffsetTableBytes = 0x10;
constexpr std::uint32_t kRingEntriesFirst = 1;
constexpr std::uint32_t kRingEntriesLast = 36;

template <typename T>
T& At(GameWorld* world, std::uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<std::uint8_t*>(world) + offset);
}

std::uint32_t Tile(std::uint32_t x, std::uint32_t y) {
    return kTiles + (x * 0x300 + y) * 24;
}

std::uint32_t Settler(std::uint32_t settler) {
    return kSettlers + settler * kSettlerBytes;
}

}  // namespace

using SetTargetFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t settler,
                                               std::uint32_t x, std::uint32_t y);
using CountNearFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t first,
                                               std::uint32_t maxDistance, std::uint32_t x,
                                               std::uint32_t y, std::uint32_t value);
using FindNearbyFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t settler,
                                                std::uint32_t x, std::uint32_t y);

std::uint32_t __fastcall GameWorld_SetSettlerTarget(GameWorld* world, void*, std::uint32_t settler,
                                                    std::uint32_t x, std::uint32_t y) {
    const std::uint32_t record = Settler(settler);
    if (GameWorld_IsOnMap(world, nullptr, x, y)) {
        At<std::uint16_t>(world, record + kSettlerTargetX) = static_cast<std::uint16_t>(x);
        At<std::uint16_t>(world, record + kSettlerTargetY) = static_cast<std::uint16_t>(y);
    } else {
        At<std::uint16_t>(world, record + kSettlerTargetX) = At<std::uint16_t>(world, record + kSettlerX);
        At<std::uint16_t>(world, record + kSettlerTargetY) = At<std::uint16_t>(world, record + kSettlerY);
    }
    for (std::uint32_t i = 0; i < kSettlerPathWords; ++i) {
        At<std::uint16_t>(world, record + kSettlerPath + i * 2) = 0xffff;
    }
    At<std::uint8_t>(world, record + kSettlerNewTarget) = 1;
    if (At<std::uint8_t>(world, record + kSettlerByte19) == 0) {
        At<std::uint8_t>(world, record + kSettlerByte19) = 1;
    }
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world)) + (settler << 6);
}

std::uint32_t __fastcall GameWorld_CountListSettlersNear(GameWorld* world, void*, std::uint32_t first,
                                                         std::uint32_t maxDistance, std::uint32_t x,
                                                         std::uint32_t y, std::uint32_t value) {
    std::uint32_t count = 0;
    for (std::uint32_t settler = first; settler != 0;
         settler = At<std::uint16_t>(world, Settler(settler) + kSettlerNext)) {
        const std::uint32_t sx = At<std::uint16_t>(world, Settler(settler) + kSettlerX);
        const std::uint32_t sy = At<std::uint16_t>(world, Settler(settler) + kSettlerY);
        if (At<std::uint8_t>(world, Tile(sx, sy) + kTileByte10) == value &&
            TileDistance(x, y, sx, sy) < maxDistance) {
            ++count;
        }
    }
    return count;
}

std::uint32_t __fastcall GameWorld_FindNearbySettlerType29(GameWorld* world, void*, std::uint32_t settler,
                                                           std::uint32_t x, std::uint32_t y) {
    std::uint32_t last = x;  // the original's register, see above
    for (std::uint32_t entry = kRingEntriesFirst; entry <= kRingEntriesLast; ++entry) {
        const std::uint32_t at = kOffsetTable + entry * kOffsetTableBytes;
        const std::uint32_t tx = x + At<std::uint32_t>(world, at);
        const std::uint32_t ty = y + At<std::uint32_t>(world, at + 4);
        if (!GameWorld_IsOnMap(world, nullptr, tx, ty)) {
            continue;
        }
        last = At<std::uint16_t>(world, Tile(tx, ty) + kTileSettler);
        if (last != 0 && At<std::uint8_t>(world, Settler(last) + kSettlerType) == kSettlerType29 &&
            At<std::uint8_t>(world, Settler(last) + kSettlerPlayer) ==
                At<std::uint8_t>(world, Settler(settler) + kSettlerPlayer)) {
            break;
        }
    }
    return last;
}

S3RE_HOOK(0x00503610, GameWorld_SetSettlerTarget, SetTargetFn, Original_GameWorld_SetSettlerTarget,
          GameWorld_SetSettlerTarget);
S3RE_HOOK(0x0050e8f0, GameWorld_CountListSettlersNear, CountNearFn, Original_GameWorld_CountListSettlersNear,
          GameWorld_CountListSettlersNear);
S3RE_HOOK(0x0050cdb0, GameWorld_FindNearbySettlerType29, FindNearbyFn,
          Original_GameWorld_FindNearbySettlerType29, GameWorld_FindNearbySettlerType29);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestSettlers = 0x400;
constexpr std::size_t kTestWorld = kSettlers + 0x10000 * kSettlerBytes;

// Runs `cases` cases; `call` prepares both worlds and returns both EAX values.
template <typename Call>
void Compare(test::Context& ctx, int cases, Call call) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        std::uint32_t expected = 0;
        std::uint32_t actual = 0;
        call(worlds, expected, actual);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 64 == 63 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 63 before it)", i);
            return;
        }
    }
}

std::uint32_t Relative(std::uint32_t value, GameWorld* world) {
    return value - static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));
}

// The real offset table: the tile, then rings of growing distance (only the
// first 37 entries are set; the order within a ring doesn't matter here).
void SetUpRings(test::Context& ctx, test::WorldPair& worlds) {
    std::uint32_t entry = 0;
    for (std::int32_t radius = 0; entry <= kRingEntriesLast; ++radius) {
        for (std::int32_t dx = -radius; dx <= radius && entry <= kRingEntriesLast; ++dx) {
            for (std::int32_t dy = -radius; dy <= radius && entry <= kRingEntriesLast; ++dy) {
                if ((dx < 0 ? -dx : dx) != radius && (dy < 0 ? -dy : dy) != radius) {
                    continue;
                }
                const bool random = ctx.rng().Chance(1, 32);
                worlds.Set<std::int32_t>(kOffsetTable + entry * kOffsetTableBytes, random ? ctx.rng().Range(-9, 9) : dx);
                worlds.Set<std::int32_t>(kOffsetTable + entry * kOffsetTableBytes + 4,
                                         random ? ctx.rng().Range(-9, 9) : dy);
                ++entry;
            }
        }
    }
}

// Wrong: returns the match or 0 (what the code probably meant).
std::uint32_t __fastcall WrongFindNearbySettlerType29(GameWorld* world, void* edx, std::uint32_t settler,
                                                      std::uint32_t x, std::uint32_t y) {
    const std::uint32_t last = GameWorld_FindNearbySettlerType29(world, edx, settler, x, y);
    if (last == 0 || last == x) {
        return last;
    }
    const bool match = At<std::uint8_t>(world, Settler(last) + kSettlerType) == kSettlerType29 &&
                       At<std::uint8_t>(world, Settler(last) + kSettlerPlayer) ==
                           At<std::uint8_t>(world, Settler(settler) + kSettlerPlayer);
    return match ? last : 0;
}

void CompareFindNearby(test::Context& ctx, FindNearbyFn candidate, int cases) {
    Compare(ctx, cases, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        const std::uint32_t size = ctx.rng().Chance(1, 2) ? 384 : 4 + ctx.rng().Below(30);
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
        SetUpRings(ctx, worlds);
        std::uint32_t x = ctx.rng().Below(size);
        std::uint32_t y = ctx.rng().Below(size);
        if (ctx.rng().Chance(1, 16)) {
            x += 400;  // nothing on the map
        }
        const std::uint32_t settler = 1 + ctx.rng().Below(kTestSettlers - 1);
        worlds.Set<std::uint8_t>(Settler(settler) + kSettlerPlayer, static_cast<std::uint8_t>(ctx.rng().Below(3)));
        const std::uint32_t sparse = ctx.rng().Below(4);
        for (std::int32_t dx = -5; dx <= 5; ++dx) {
            for (std::int32_t dy = -5; dy <= 5; ++dy) {
                const std::uint32_t tx = x + dx;
                const std::uint32_t ty = y + dy;
                if (tx >= size || ty >= size) {
                    continue;
                }
                const std::uint32_t other = ctx.rng().Below(4) < sparse ? 0 : 1 + ctx.rng().Below(kTestSettlers - 1);
                worlds.Set<std::uint16_t>(Tile(tx, ty) + kTileSettler, static_cast<std::uint16_t>(other));
                if (other != 0) {
                    worlds.Set<std::uint8_t>(Settler(other) + kSettlerType,
                                             ctx.rng().Chance(1, 6) ? kSettlerType29 : static_cast<std::uint8_t>(ctx.rng().Below(0x40)));
                    worlds.Set<std::uint8_t>(Settler(other) + kSettlerPlayer, static_cast<std::uint8_t>(ctx.rng().Below(3)));
                }
            }
        }
        expected = Original_GameWorld_FindNearbySettlerType29(worlds.original(), nullptr, settler, x, y);
        actual = candidate(worlds.ours(), nullptr, settler, x, y);
    });
}

}  // namespace

S3RE_TEST(GameWorld_SetSettlerTarget_MatchesOriginal) {
    Compare(ctx, 5000, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        const std::uint32_t size = ctx.rng().Chance(1, 2) ? 384 : ctx.rng().Below(64);
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
        const std::uint32_t settler = ctx.rng().Below(0x10000);
        std::uint32_t x = ctx.rng().Below(400);
        std::uint32_t y = ctx.rng().Below(400);
        if (ctx.rng().Chance(1, 8)) {
            x = ctx.rng().Next32();
        }
        worlds.Set<std::uint8_t>(Settler(settler) + kSettlerByte19, static_cast<std::uint8_t>(ctx.rng().Below(3)));
        expected = Relative(Original_GameWorld_SetSettlerTarget(worlds.original(), nullptr, settler, x, y),
                            worlds.original());
        actual = Relative(GameWorld_SetSettlerTarget(worlds.ours(), nullptr, settler, x, y), worlds.ours());
    });
}

S3RE_TEST(GameWorld_CountListSettlersNear_MatchesOriginal) {
    Compare(ctx, 3000, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        // A list of up to 20 settlers on tiles near (x, y), some with the value.
        const std::uint32_t x = ctx.rng().Below(384);
        const std::uint32_t y = ctx.rng().Below(384);
        const std::uint32_t value = ctx.rng().Below(3);
        const std::uint32_t length = ctx.rng().Below(20);
        std::uint32_t first = 0;
        for (std::uint32_t i = 0; i < length; ++i) {
            const std::uint32_t settler = 1 + ctx.rng().Below(kTestSettlers - 1);
            const std::uint32_t sx = static_cast<std::uint32_t>(ctx.rng().Range(0, 383));
            const std::uint32_t sy = ctx.rng().Chance(1, 2) ? static_cast<std::uint32_t>(ctx.rng().Range(0, 383))
                                                            : y + ctx.rng().Below(10) - (y > 9 ? 5 : 0);
            worlds.Set<std::uint16_t>(Settler(settler) + kSettlerX, static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? x : sx));
            worlds.Set<std::uint16_t>(Settler(settler) + kSettlerY, static_cast<std::uint16_t>(sy % 384));
            worlds.Set<std::uint16_t>(Settler(settler) + kSettlerNext, static_cast<std::uint16_t>(first));
            const std::uint32_t tile = Tile(At<std::uint16_t>(worlds.original(), Settler(settler) + kSettlerX),
                                            At<std::uint16_t>(worlds.original(), Settler(settler) + kSettlerY));
            worlds.Set<std::uint8_t>(tile + kTileByte10,
                                     static_cast<std::uint8_t>(ctx.rng().Chance(2, 3) ? value : ctx.rng().Below(256)));
            first = settler;
        }
        // A list may loop through an earlier settler; make it end.
        const std::uint32_t maxDistance = ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(400);
        // Walk the list as built (it can contain the same settler twice and loop);
        // only call when it ends within 64 steps.
        std::uint32_t steps = 0;
        for (std::uint32_t s = first; s != 0 && steps < 64; s = At<std::uint16_t>(worlds.original(), Settler(s) + kSettlerNext)) {
            ++steps;
        }
        if (steps == 64) {
            first = 0;
        }
        expected = Original_GameWorld_CountListSettlersNear(worlds.original(), nullptr, first, maxDistance, x, y, value);
        actual = GameWorld_CountListSettlersNear(worlds.ours(), nullptr, first, maxDistance, x, y, value);
    });
}

S3RE_TEST(GameWorld_FindNearbySettlerType29_MatchesOriginal) {
    CompareFindNearby(ctx, &GameWorld_FindNearbySettlerType29, 3000);
}
S3RE_TEST(GameWorld_FindNearbySettlerType29_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareFindNearby(probe, &WrongFindNearbySettlerType29, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
