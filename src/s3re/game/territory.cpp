// Land changing hands: a tile losing its owner, and what stands on it.
//
// GameWorld::ResetCivilianOnTile (0x0050e980): (x, y) thiscall, ret 8. If a
//   settler stands on the tile (word +0x0), belongs to the tile's owner
//   (+0x8) and its type has class 0 (civilians, S3.EXE 0x658f64):
//   ResetSettlerToIdle(settler). EAX not used by callers.
// GameWorld::LosePileOnTile (0x00504f70): (x, y) thiscall, ret 8. Only for a
//   tile in a land area (word +0x14 in 1..0x7fff) with a pile (word +0x2 not
//   0, flag 0x20 "building" clear). A dropped pile (kind +0x4 == 0xfe): if
//   units are leaving it (+0x2), every settler (1..high water, re-read) with
//   an owner and one of the fetching jobs 0xa, 0xb, 0xa9, 0x4c, 0x4d, 0x68,
//   0x69 whose target (+0x24/+0x26) is the pile's position gets
//   ResetSettlerToIdle; DebugLog "Pile %u Lost, eco %u"; the pile is
//   unlinked from its area's list for its goods type (prev +0xa / next +0xc,
//   head areas[area].piles_by_goods[good]). Any other kind is only logged
//   ("illegal pile in changing land: ...", "(x,y) = ..."). In both cases
//   SubtractPlayerCounter(tile owner, good, amount). The pile record and the
//   tile's pile word stay as they are. EAX not used by callers.
// GameWorld::ClearTileOwner (0x0051b640): (x, y) thiscall, ret 8. If the
//   tile has an owner (+0x8 != 0xff): ResetCivilianOnTile, LosePileOnTile,
//   owner = 0xff, and if the tile is in a land area (1..0x7fff),
//   SplitAreaAtTile(x, y). EAX not used by callers (the "flow" hit of
//   asm_tools continues into SetOwnerOfFreeTile).
// All tile positions use the original's 32-bit tile arithmetic.

#include "game/debug_log.h"
#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace s3re::game {

namespace {

using layout::Pile;
using layout::Settler;
using layout::Tile;

const std::int32_t* const kSettlerTypeClass = reinterpret_cast<const std::int32_t*>(0x00658f64);

constexpr std::uint8_t kFree = 0xff;
constexpr std::uint8_t kPileDropped = 0xfe;

constexpr std::uintptr_t kPileLostMessage = 0x006a6120;     // "Pile %u Lost, eco %u"
constexpr std::uintptr_t kIllegalPileMessage = 0x006a60d8;  // "illegal pile in changing land: nr.: %u ..."
constexpr std::uintptr_t kPositionMessage = 0x006a60c8;     // "(x,y) = (%u,%u)"

// Callees reached through their S3.EXE addresses (so that the tests can
// replace them by recording stubs): ResetSettlerToIdle belongs to another
// work package; SplitAreaAtTile (verified, area_split.cpp) floods regions.
using SettlerFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t settler);
using TileFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);
constexpr std::uintptr_t kResetSettlerToIdle = 0x0050cf50;
constexpr std::uintptr_t kSplitAreaAtTile = 0x004e5d50;

void ResetSettlerToIdle(GameWorld* world, std::uint32_t settler) {
    reinterpret_cast<SettlerFn>(kResetSettlerToIdle)(world, nullptr, settler);
}

Tile& TileAt(GameWorld* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

bool IsLandArea(std::uint16_t area) {
    return area != 0 && area < layout::kTileAreaWater;
}

// The jobs of settlers on their way to take goods from a pile (carriers and
// workers; the pile is their target position).
bool IsFetchingJob(std::uint16_t job) {
    return job == 0x0a || job == 0x0b || job == 0xa9 || job == 0x4c || job == 0x4d || job == 0x68 || job == 0x69;
}

}  // namespace

std::uint32_t __fastcall GameWorld_SubtractPlayerCounter(GameWorld* world, void* edx, std::uint32_t player,
                                                         std::uint32_t index, std::uint32_t amount);

using PositionFn = void(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);

// Purpose: When a tile changes hands, a civilian of the old owner standing on
// it stops what he is doing.
void __fastcall GameWorld_ResetCivilianOnTile(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    const Tile& tile = TileAt(world, x, y);
    const std::uint32_t settler = tile.settler;
    if (settler == 0) {
        return;
    }
    const Settler& record = world->settlers[settler];
    if (record.owner != tile.owner || kSettlerTypeClass[record.art] != 0) {
        return;
    }
    ResetSettlerToIdle(world, settler);
}

// Purpose: When a tile changes hands, the goods pile lying on it is lost to
// its old owner: settlers on their way to fetch from it give up, it leaves
// the area's goods lists and the owner's goods counter.
void __fastcall GameWorld_LosePileOnTile(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    Tile& tile = TileAt(world, x, y);
    const std::uint32_t area = tile.area;
    if (!IsLandArea(static_cast<std::uint16_t>(area))) {
        return;
    }
    if (tile.object_number == 0 || (tile.flags & layout::kTileHasBuilding) != 0) {
        return;
    }
    const std::uint32_t number = tile.object_number;
    Pile& pile = world->piles[number];
    if (pile.art == kPileDropped) {
        if (pile.leaving != 0) {
            for (std::uint32_t n = 1; n <= world->settler_high_water; ++n) {
                const Settler& settler = world->settlers[n];
                if (settler.owner != kFree && IsFetchingJob(settler.job) && settler.target_x == pile.x &&
                    settler.target_y == pile.y) {
                    ResetSettlerToIdle(world, n);
                }
            }
        }
        GameDebugLog(kPileLostMessage, number, area);
        if (pile.prev != 0) {
            world->piles[pile.prev].next = pile.next;
        } else {
            // The good byte indexes the list heads without a bound check.
            *(world->areas[area].piles_by_goods + pile.good) = pile.next;
        }
        if (pile.next != 0) {
            world->piles[pile.next].prev = pile.prev;
        }
    } else {
        GameDebugLog(kIllegalPileMessage, number, static_cast<std::uint32_t>(pile.amount),
                     static_cast<std::uint32_t>(pile.art), static_cast<std::uint32_t>(pile.good));
        GameDebugLog(kPositionMessage, static_cast<std::uint32_t>(pile.x), static_cast<std::uint32_t>(pile.y));
    }
    GameWorld_SubtractPlayerCounter(world, nullptr, tile.owner, pile.good, pile.amount);
}

// Purpose: Takes a tile away from its owner (e.g. when territory is lost):
// civilians on it stop, a pile on it is lost, and the economy area is split
// if the tile connected parts of it.
void __fastcall GameWorld_ClearTileOwner(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    std::uint8_t& owner = TileAt(world, x, y).owner;
    if (owner == kFree) {
        return;
    }
    GameWorld_ResetCivilianOnTile(world, nullptr, x, y);
    GameWorld_LosePileOnTile(world, nullptr, x, y);
    owner = kFree;
    if (IsLandArea(TileAt(world, x, y).area)) {
        reinterpret_cast<TileFn>(kSplitAreaAtTile)(world, nullptr, x, y);
    }
}

S3RE_HOOK(0x0050e980, GameWorld_ResetCivilianOnTile, PositionFn, Original_GameWorld_ResetCivilianOnTile,
          GameWorld_ResetCivilianOnTile);
S3RE_HOOK(0x00504f70, GameWorld_LosePileOnTile, PositionFn, Original_GameWorld_LosePileOnTile,
          GameWorld_LosePileOnTile);
S3RE_HOOK(0x0051b640, GameWorld_ClearTileOwner, PositionFn, Original_GameWorld_ClearTileOwner,
          GameWorld_ClearTileOwner);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kTestWorld = offsetof(GameWorld, settlers) + 200 * sizeof(Settler);

// Recording stubs for ResetSettlerToIdle (the settler's job becomes 1, so
// that a scan doesn't find him twice) and SplitAreaAtTile (no effect).
void InstallStubs(test::CalleeStubs& stubs) {
    stubs.Install<1, kResetSettlerToIdle>([](GameWorld* w, std::uint32_t, const std::uint32_t* args) {
        w->settlers[args[0] & 0xffff].job = 1;
        return 0x5a5a0001u;
    });
    stubs.Install<2, kSplitAreaAtTile>();
}

// One tile at (x, y) in 10..370 with a settler and a pile, and up to 150
// settlers of which many fetch from the pile.
struct TileCase {
    std::uint32_t x, y;
};

TileCase SetUpTile(test::Context& ctx, test::WorldPair& worlds) {
    const std::uint32_t x = 10 + ctx.rng().Below(360);
    const std::uint32_t y = 10 + ctx.rng().Below(360);
    const std::uint32_t settlers = ctx.rng().Below(150);
    const auto pileNumber = static_cast<std::uint16_t>(ctx.rng().Chance(1, 10) ? 0 : 1 + ctx.rng().Below(100));
    const auto settler = static_cast<std::uint16_t>(ctx.rng().Chance(1, 5) ? 0 : 1 + ctx.rng().Below(150));
    const auto owner = static_cast<std::uint8_t>(ctx.rng().Chance(1, 8) ? kFree : ctx.rng().Below(8));
    // Land areas 1..50, sometimes none or a water body (0x8000 + n); areas
    // past the 4096 records would make both versions write outside the world.
    const std::uint32_t areaKind = ctx.rng().Below(8);
    const auto area = static_cast<std::uint16_t>(areaKind == 0   ? 0
                                                 : areaKind == 1 ? 0x8000 + ctx.rng().Below(0x8000)
                                                                 : 1 + ctx.rng().Below(50));
    const auto flags = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? ctx.rng().Next32() & ~0x20u : ctx.rng().Next32());
    const auto px = static_cast<std::uint16_t>(ctx.rng().Chance(4, 5) ? x : ctx.rng().Below(384));
    const auto py = static_cast<std::uint16_t>(ctx.rng().Chance(4, 5) ? y : ctx.rng().Below(384));
    const auto art = static_cast<std::uint8_t>(ctx.rng().Chance(3, 4) ? kPileDropped : ctx.rng().Below(256));
    const auto leaving = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Below(9));
    const auto good = static_cast<std::uint8_t>(ctx.rng().Below(36));
    const auto amount = static_cast<std::uint8_t>(ctx.rng().Below(9));
    const auto prev = static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? 0 : 1 + ctx.rng().Below(200));
    const auto next = static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? 0 : 1 + ctx.rng().Below(200));
    worlds.Both([&](GameWorld* w) {
        w->settler_high_water = settlers;
        Tile& t = w->tiles[x][y];
        t.settler = settler;
        t.object_number = pileNumber;
        t.owner = owner;
        t.area = area;
        t.flags = flags;
        Pile& p = w->piles[pileNumber];
        p.x = px;
        p.y = py;
        p.art = art;
        p.leaving = leaving;
        p.good = good;
        p.amount = amount;
        p.prev = prev;
        p.next = next;
    });
    for (std::uint32_t n = 1; n <= 150; ++n) {
        static constexpr std::uint16_t kJobs[] = {0x0a, 0x0b, 0xa9, 0x4c, 0x4d, 0x68, 0x69, 0x4e, 0x1, 0x37};
        const auto job = ctx.rng().Chance(3, 4) ? kJobs[ctx.rng().Below(10)] : static_cast<std::uint16_t>(ctx.rng().Below(0x195));
        const auto sowner = static_cast<std::uint8_t>(ctx.rng().Chance(1, 6) ? kFree : ctx.rng().Chance(1, 2) ? owner : ctx.rng().Below(8));
        const auto tx = static_cast<std::uint16_t>(ctx.rng().Chance(2, 3) ? px : ctx.rng().Below(384));
        const auto ty = static_cast<std::uint16_t>(ctx.rng().Chance(2, 3) ? py : ctx.rng().Below(384));
        const auto sart = static_cast<std::uint8_t>(ctx.rng().Below(0x30));
        worlds.Both([&](GameWorld* w) {
            Settler& s = w->settlers[n];
            s.job = job;
            s.owner = sowner;
            s.target_x = tx;
            s.target_y = ty;
            s.art = sart;
        });
    }
    return {x, y};
}

void ComparePositionFn(test::Context& ctx, PositionFn original, PositionFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    test::CalleeStubs stubs;
    InstallStubs(stubs);
    DebugLogRecorder logs;
    for (int i = 0; i < cases; ++i) {
        const TileCase c = SetUpTile(ctx, worlds);
        stubs.SetWorld(worlds.original());
        original(worlds.original(), nullptr, c.x, c.y);
        const std::vector<test::StubCall> expectedCalls = stubs.Take();
        const std::vector<DebugLogCall> expectedLogs = logs.Take();
        stubs.SetWorld(worlds.ours());
        candidate(worlds.ours(), nullptr, c.x, c.y);
        const std::vector<test::StubCall> actualCalls = stubs.Take();
        const std::vector<DebugLogCall> actualLogs = logs.Take();
        if (!S3RE_EXPECT(expectedCalls == actualCalls) || !S3RE_EXPECT(expectedLogs == actualLogs) ||
            ((i % 64 == 63 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or before): (%u, %u), %u calls / %u expected", i, c.x, c.y,
                     static_cast<unsigned>(actualCalls.size()), static_cast<unsigned>(expectedCalls.size()));
            return;
        }
    }
}

// Wrong: also resets civilians of other owners.
void __fastcall WrongResetCivilianOnTile(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y) {
    Tile& tile = TileAt(world, x, y);
    const std::uint8_t saved = tile.owner;
    if (tile.settler != 0) {
        tile.owner = world->settlers[tile.settler].owner;
    }
    GameWorld_ResetCivilianOnTile(world, edx, x, y);
    tile.owner = saved;
}

// Wrong: ignores job 0xa9.
void __fastcall WrongLosePileOnTile(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y) {
    std::vector<std::uint32_t> changed;
    for (std::uint32_t n = 1; n <= world->settler_high_water && n < 200; ++n) {
        if (world->settlers[n].job == 0xa9) {
            world->settlers[n].job = 0x3;
            changed.push_back(n);
        }
    }
    GameWorld_LosePileOnTile(world, edx, x, y);
    for (const std::uint32_t n : changed) {
        if (world->settlers[n].job == 0x3) {
            world->settlers[n].job = 0xa9;
        }
    }
}

// Wrong: splits the area even for water tiles.
void __fastcall WrongClearTileOwner(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y) {
    const bool owned = TileAt(world, x, y).owner != kFree;
    GameWorld_ClearTileOwner(world, edx, x, y);
    if (owned && TileAt(world, x, y).area >= layout::kTileAreaWater) {
        reinterpret_cast<TileFn>(kSplitAreaAtTile)(world, nullptr, x, y);
    }
}

}  // namespace

S3RE_TEST(GameWorld_ResetCivilianOnTile_MatchesOriginal) {
    ComparePositionFn(ctx, Original_GameWorld_ResetCivilianOnTile, &GameWorld_ResetCivilianOnTile, 3000);
}
S3RE_TEST(GameWorld_ResetCivilianOnTile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePositionFn(probe, Original_GameWorld_ResetCivilianOnTile, &WrongResetCivilianOnTile, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_LosePileOnTile_MatchesOriginal) {
    ComparePositionFn(ctx, Original_GameWorld_LosePileOnTile, &GameWorld_LosePileOnTile, 3000);
}
S3RE_TEST(GameWorld_LosePileOnTile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePositionFn(probe, Original_GameWorld_LosePileOnTile, &WrongLosePileOnTile, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_ClearTileOwner_MatchesOriginal) {
    ComparePositionFn(ctx, Original_GameWorld_ClearTileOwner, &GameWorld_ClearTileOwner, 3000);
}
S3RE_TEST(GameWorld_ClearTileOwner_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePositionFn(probe, Original_GameWorld_ClearTileOwner, &WrongClearTileOwner, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
