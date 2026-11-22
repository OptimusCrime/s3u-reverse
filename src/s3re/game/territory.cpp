// Land changing hands: a tile losing its owner, and what stands on it.
//
// CGameData::ResetCivilianOnTile (0x0050e980): (x, y) thiscall, ret 8. If a
//   settler stands on the tile (word +0x0), belongs to the tile's owner
//   (+0x8) and its type has class 0 (civilians, S3.EXE 0x658f64):
//   ResetSettlerToIdle(settler). EAX not used by callers.
// CGameData::LosePileOnTile (0x00504f70): (x, y) thiscall, ret 8. Only for a
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
// CGameData::ClearTileOwner (0x0051b640): (x, y) thiscall, ret 8. If the
//   tile has an owner (+0x8 != 0xff): ResetCivilianOnTile, LosePileOnTile,
//   owner = 0xff, and if the tile is in a land area (1..0x7fff),
//   SplitAreaAtTile(x, y). EAX not used by callers (the "flow" hit of
//   asm_tools continues into SetOwnerOfFreeTile).
// All tile positions use the original's 32-bit tile arithmetic.

#include "game/debug_log.h"
#include "game/replacements.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/world.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

using layout::Pile;
using layout::Settler;
using layout::Tile;


constexpr std::uint8_t kFree = 0xff;
constexpr std::uint8_t kPileDropped = 0xfe;


// Callees reached through their S3.EXE addresses (so that the tests can
// replace them by recording stubs): ResetSettlerToIdle belongs to another
// work package; SplitAreaAtTile (verified, area_split.cpp) floods regions.
using SettlerFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t settler);
using TileFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);
constexpr std::uintptr_t kResetSettlerToIdle = 0x0050cf50;
constexpr std::uintptr_t kSplitAreaAtTile = 0x004e5d50;

void ResetSettlerToIdle(CGameData* world, std::uint32_t settler) {
    reinterpret_cast<SettlerFn>(kResetSettlerToIdle)(world, nullptr, settler);
}

Tile& TileAt(CGameData* world, std::uint32_t x, std::uint32_t y) {
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

std::uint32_t __fastcall CGameData_SubtractPlayerCounter(CGameData* world, void* edx, std::uint32_t player,
                                                         std::uint32_t index, std::uint32_t amount);

using PositionFn = void(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);

// Purpose: When a tile changes hands, a civilian of the old owner standing on
// it stops what he is doing.
void __fastcall CGameData_ResetCivilianOnTile(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    const Tile& tile = TileAt(world, x, y);
    const std::uint32_t settler = tile.settler;
    if (settler == 0) {
        return;
    }
    const Settler& record = world->settlers[settler];
    if (record.owner != tile.owner || g_settlerTypeClass[record.art] != 0) {
        return;
    }
    ResetSettlerToIdle(world, settler);
}

// Purpose: When a tile changes hands, the goods pile lying on it is lost to
// its old owner: settlers on their way to fetch from it give up, it leaves
// the area's goods lists and the owner's goods counter.
void __fastcall CGameData_LosePileOnTile(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
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
        GameDebugLog(texts::kPileLost, number, area);
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
        GameDebugLog(texts::kIllegalPileInChangingLand, number, static_cast<std::uint32_t>(pile.amount),
                     static_cast<std::uint32_t>(pile.art), static_cast<std::uint32_t>(pile.good));
        GameDebugLog(texts::kPositionXY, static_cast<std::uint32_t>(pile.x), static_cast<std::uint32_t>(pile.y));
    }
    CGameData_SubtractPlayerCounter(world, nullptr, tile.owner, pile.good, pile.amount);
}

// Purpose: Takes a tile away from its owner (e.g. when territory is lost):
// civilians on it stop, a pile on it is lost, and the economy area is split
// if the tile connected parts of it.
void __fastcall CGameData_ClearTileOwner(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    std::uint8_t& owner = TileAt(world, x, y).owner;
    if (owner == kFree) {
        return;
    }
    CGameData_ResetCivilianOnTile(world, nullptr, x, y);
    CGameData_LosePileOnTile(world, nullptr, x, y);
    owner = kFree;
    if (IsLandArea(TileAt(world, x, y).area)) {
        reinterpret_cast<TileFn>(kSplitAreaAtTile)(world, nullptr, x, y);
    }
}

S3RE_HOOK(0x0050e980, CGameData_ResetCivilianOnTile, PositionFn, Original_CGameData_ResetCivilianOnTile,
          CGameData_ResetCivilianOnTile);
S3RE_HOOK(0x00504f70, CGameData_LosePileOnTile, PositionFn, Original_CGameData_LosePileOnTile,
          CGameData_LosePileOnTile);
S3RE_HOOK(0x0051b640, CGameData_ClearTileOwner, PositionFn, Original_CGameData_ClearTileOwner,
          CGameData_ClearTileOwner);

// --- RecalculateTerritory -----------------------------------------------------

namespace {

// Callees through their S3.EXE addresses (tests replace them by recording
// stubs): DestroyBuildingAt (the recursion of the destruction cycle) and
// SetOwnerOfFreeTile (verified; joins economy areas) and ClearTileOwner.
constexpr std::uintptr_t kDestroyBuildingAt = 0x004d97b0;
constexpr std::uintptr_t kSetOwnerOfFreeTile = 0x0051b5d0;
constexpr std::uintptr_t kClearTileOwner = 0x0051b640;  // above
using SetOwnerFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y,
                                              std::uint32_t owner);

constexpr std::int32_t kScratchRadius = 50;  // the scratch covers (x - 50 .. x + 50, y - 50 .. y + 50)
constexpr std::int32_t kScratchSide = 101;
constexpr std::uint32_t kNoPlayer = 0xff;

// Influence of a garrisoned military building: number of offset-table
// entries (rings around it) it claims.
std::uint32_t InfluenceEntries(const layout::Building& b) {
    std::uint32_t entries = 0;
    if (b.art == 0xf && b.garrison != 0 && b.buildup == 0) {
        entries = 0xdac;
    }
    if (b.art == 0x10 && b.garrison != 0 && b.buildup == 0) {
        entries = 0xfa0;
    }
    if (b.art == 0x12 && b.garrison != 0 && b.buildup == 0) {
        entries = 0x1388;
    }
    return entries;
}

// The byte of the scratch for player byte `player` in cell (i, j) of the
// 101 x 101 window (players are not bound-checked, as in the original).
std::uint8_t& ScratchCell(CGameData* world, std::int32_t i, std::int32_t j, std::uint32_t player) {
    std::uint8_t* base = world->territory_scratch[0][0].player;
    return base[static_cast<std::uint32_t>((i * kScratchSide + j) * static_cast<std::int32_t>(layout::kPlayers)) + player];
}

// The player with the strongest claim on a cell (first of equals), or 0xff
// if nobody has one.
std::uint32_t StrongestClaim(CGameData* world, std::int32_t i, std::int32_t j) {
    std::uint32_t best = 0;
    std::uint32_t player = kNoPlayer;
    for (std::uint32_t p = 0; p < layout::kPlayers; ++p) {
        const std::uint32_t claim = ScratchCell(world, i, j, p);
        if (claim > best) {
            player = p;
            best = claim;
        }
    }
    return player;
}

}  // namespace

std::uint32_t __fastcall CGameData_RecalculateTerritory(CGameData* world, void*, std::uint32_t x, std::uint32_t y);

// Purpose: Recomputes who owns the land within 50 tiles of (x, y) after a
// military building there changed (built, garrisoned, lost or destroyed):
// every garrisoned castle/tower within 100 tiles claims the tiles around it,
// strongest near it; free tiles go to the strongest claim, owned tiles
// without a claim of their owner go to the strongest other claim (destroying
// buildings standing there), and the border marks are redrawn.
std::uint32_t __fastcall CGameData_RecalculateTerritory(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    world->draw_call_counter = 0;
    std::memset(world->territory_scratch, 0, sizeof(world->territory_scratch));
    const std::int32_t cx = static_cast<std::int32_t>(x);
    const std::int32_t cy = static_cast<std::int32_t>(y);
    const std::int32_t mapSize = static_cast<std::int32_t>(world->map_size);

    // 1. Claims of the military buildings within 100 tiles.
    for (std::int32_t i = -100; i <= 100; ++i) {
        for (std::int32_t j = -100; j <= 100; ++j) {
            const std::int32_t tx = cx + i;
            const std::int32_t ty = cy + j;
            if (tx < 0 || tx >= mapSize || ty < 0 || ty >= mapSize) {
                continue;
            }
            const layout::Tile& tile = TileAt(world, static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty));
            if (tile.object_number == 0 || (tile.flags & layout::kTileHasBuilding) == 0) {
                continue;
            }
            const layout::Building& b = world->buildings[tile.object_number];
            const std::uint32_t entries = InfluenceEntries(b);
            const std::uint32_t owner = b.owner;
            for (std::uint32_t k = 0; k < entries; ++k) {
                const layout::MapOffset& offset = world->offsets[k];
                const std::int32_t ci = offset.dx + i + kScratchRadius;
                const std::int32_t cj = offset.dy + j + kScratchRadius;
                if (ci < 0 || ci > 100 || cj < 0 || cj > 100) {
                    continue;
                }
                std::uint8_t& cell = ScratchCell(world, ci, cj, owner);
                if (offset.distance <= 1) {
                    cell = 0xff;
                } else {
                    std::int32_t value = static_cast<std::int32_t>(cell) - offset.distance + 50;
                    if (value > 0xfe) {
                        value = 0xfe;
                    }
                    cell = static_cast<std::uint8_t>(value);
                }
            }
        }
    }

    // 2. Ownership of the tiles within 50 (not on the map's outer ring).
    for (std::int32_t i = -kScratchRadius; i <= kScratchRadius; ++i) {
        for (std::int32_t j = -kScratchRadius; j <= kScratchRadius; ++j) {
            const std::int32_t tx = cx + i;
            const std::int32_t ty = cy + j;
            if (tx < 1 || tx >= mapSize - 1 || ty < 1 || ty >= mapSize - 1) {
                continue;
            }
            const auto ux = static_cast<std::uint32_t>(tx);
            const auto uy = static_cast<std::uint32_t>(ty);
            layout::Tile& tile = TileAt(world, ux, uy);
            const std::int32_t ci = i + kScratchRadius;
            const std::int32_t cj = j + kScratchRadius;
            const std::uint8_t owner = tile.owner;
            if (owner == kFree) {
                const std::uint32_t player = StrongestClaim(world, ci, cj);
                if (player == kNoPlayer) {
                    continue;
                }
                reinterpret_cast<SetOwnerFn>(kSetOwnerOfFreeTile)(world, nullptr, ux, uy, player);
                tile.flags |= layout::kTileOwned;
                continue;
            }
            if (ScratchCell(world, ci, cj, owner) != 0) {
                tile.flags |= layout::kTileOwned;
                continue;
            }
            tile.flags &= static_cast<std::uint8_t>(~layout::kTileOwned);
            const std::uint32_t player = StrongestClaim(world, ci, cj);
            if (player == kNoPlayer) {
                continue;
            }
            if ((tile.flags & layout::kTileBuildingFootprint) != 0) {
                reinterpret_cast<PositionFn>(kDestroyBuildingAt)(world, nullptr, ux, uy);
            }
            reinterpret_cast<PositionFn>(kClearTileOwner)(world, nullptr, ux, uy);
            reinterpret_cast<SetOwnerFn>(kSetOwnerOfFreeTile)(world, nullptr, ux, uy, player);
            tile.flags |= layout::kTileOwned;
        }
    }

    // 3. Border marks.
    std::uint32_t result = 0;
    for (std::int32_t i = -51; i <= 51; ++i) {
        for (std::int32_t j = -51; j <= 51; ++j) {
            result = CGameData_UpdateBorderFlagsAround(world, nullptr, x + static_cast<std::uint32_t>(i),
                                                       y + static_cast<std::uint32_t>(j));
        }
    }
    return result;
}

S3RE_HOOK(0x00519a90, CGameData_RecalculateTerritory, TileFn, Original_CGameData_RecalculateTerritory,
          CGameData_RecalculateTerritory);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kTestWorld = offsetof(CGameData, settlers) + 200 * sizeof(Settler);

// Recording stubs for ResetSettlerToIdle (the settler's job becomes 1, so
// that a scan doesn't find him twice) and SplitAreaAtTile (no effect).
void InstallStubs(test::CalleeStubs& stubs) {
    stubs.Install<1, kResetSettlerToIdle>([](CGameData* w, std::uint32_t, const std::uint32_t* args) {
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
    worlds.Both([&](CGameData* w) {
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
        worlds.Both([&](CGameData* w) {
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
void __fastcall WrongResetCivilianOnTile(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y) {
    Tile& tile = TileAt(world, x, y);
    const std::uint8_t saved = tile.owner;
    if (tile.settler != 0) {
        tile.owner = world->settlers[tile.settler].owner;
    }
    CGameData_ResetCivilianOnTile(world, edx, x, y);
    tile.owner = saved;
}

// Wrong: ignores job 0xa9.
void __fastcall WrongLosePileOnTile(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y) {
    std::vector<std::uint32_t> changed;
    for (std::uint32_t n = 1; n <= world->settler_high_water && n < 200; ++n) {
        if (world->settlers[n].job == 0xa9) {
            world->settlers[n].job = 0x3;
            changed.push_back(n);
        }
    }
    CGameData_LosePileOnTile(world, edx, x, y);
    for (const std::uint32_t n : changed) {
        if (world->settlers[n].job == 0x3) {
            world->settlers[n].job = 0xa9;
        }
    }
}

// Wrong: splits the area even for water tiles.
void __fastcall WrongClearTileOwner(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y) {
    const bool owned = TileAt(world, x, y).owner != kFree;
    CGameData_ClearTileOwner(world, edx, x, y);
    if (owned && TileAt(world, x, y).area >= layout::kTileAreaWater) {
        reinterpret_cast<TileFn>(kSplitAreaAtTile)(world, nullptr, x, y);
    }
}

}  // namespace

S3RE_TEST(CGameData_ResetCivilianOnTile_MatchesOriginal) {
    ComparePositionFn(ctx, Original_CGameData_ResetCivilianOnTile, &CGameData_ResetCivilianOnTile, 3000);
}
S3RE_TEST(CGameData_ResetCivilianOnTile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePositionFn(probe, Original_CGameData_ResetCivilianOnTile, &WrongResetCivilianOnTile, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_LosePileOnTile_MatchesOriginal) {
    ComparePositionFn(ctx, Original_CGameData_LosePileOnTile, &CGameData_LosePileOnTile, 3000);
}
S3RE_TEST(CGameData_LosePileOnTile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePositionFn(probe, Original_CGameData_LosePileOnTile, &WrongLosePileOnTile, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_ClearTileOwner_MatchesOriginal) {
    ComparePositionFn(ctx, Original_CGameData_ClearTileOwner, &CGameData_ClearTileOwner, 3000);
}
S3RE_TEST(CGameData_ClearTileOwner_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePositionFn(probe, Original_CGameData_ClearTileOwner, &WrongClearTileOwner, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}


// --- RecalculateTerritory and the destruction cycle ---------------------------

// The other cycle functions (building_crush.cpp) and their trampolines.
using CycleCrushFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t building);
using CycleBuildingFn = void(__fastcall*)(CGameData* world, void* edx, std::uint32_t building);
std::uint32_t __fastcall CGameData_CrushBuilding(CGameData* world, void* edx, std::uint32_t building);
extern CycleCrushFn Original_CGameData_CrushBuilding;
extern CycleBuildingFn Original_CGameData_ReleaseSiteWorkers;
extern TileFn Original_CGameData_DestroyBuildingAt;

namespace {

constexpr std::size_t kTerritoryWorld = offsetof(CGameData, buildings) + 64 * sizeof(layout::Building);
constexpr std::uint32_t kMarkFootprint = 0x80000000;
constexpr std::uint32_t kMarkFootprintEnd = 0x80000001;

// The offset table as the game builds it: rings of growing hex distance.
void SetRingOffsets(test::WorldPair& worlds, std::uint32_t entries) {
    std::vector<layout::MapOffset> table;
    for (std::int32_t ring = 0; table.size() < entries; ++ring) {
        for (std::int32_t dx = -ring; dx <= ring; ++dx) {
            for (std::int32_t dy = -ring; dy <= ring; ++dy) {
                const bool same = (dx >= 0) == (dy >= 0);
                const std::int32_t ax = dx < 0 ? -dx : dx;
                const std::int32_t ay = dy < 0 ? -dy : dy;
                const std::int32_t d = same ? (ax > ay ? ax : ay) : ax + ay;
                if (d == ring) {
                    table.push_back({dx, dy, ring, 0});
                }
            }
        }
    }
    worlds.Both([&](CGameData* w) {
        for (std::uint32_t k = 0; k < entries; ++k) {
            w->offsets[k] = table[k];
        }
    });
}

// Building `number` at (x, y): its footprint tiles (layout list up to
// 0x80000001) get the building and footprint flags (0x20 | 0x08) and its
// number.
void PlaceBuilding(test::WorldPair& worlds, std::uint32_t number, std::uint8_t type, std::uint8_t race,
                   std::uint8_t owner, std::uint16_t x, std::uint16_t y, std::uint8_t buildup, std::uint16_t garrison) {
    worlds.Both([&](CGameData* w) {
        layout::Building& b = w->buildings[number];
        b.art = type;
        b.race = race;
        b.owner = owner;
        b.x = x;
        b.y = y;
        b.door_x = static_cast<std::uint16_t>(x + 1);
        b.door_y = static_cast<std::uint16_t>(y + 1);
        b.buildup = buildup;
        b.garrison = garrison;
        b.site.boards_missing = 0;  // no materials missing (RemoveBuildingGoods returns half)
        b.site.stones_missing = 0;
        const std::uint32_t* p = g_buildingLayouts[type + race * 57u];
        while (*p++ != kMarkFootprint) {
        }
        TileAt(w, x, y).flags |= 0x28;
        TileAt(w, x, y).object_number = static_cast<std::uint16_t>(number);
        for (; *p != kMarkFootprintEnd; p += 2) {
            Tile& t = TileAt(w, x + p[0], y + p[1]);
            t.flags |= 0x28;
            t.object_number = static_cast<std::uint16_t>(number);
        }
    });
}

// Clears the window of +-radius around (x, y): no buildings, settlers or
// piles; owners by `owner(tx, ty)`.
template <typename Owner>
void ClearWindow(test::Context& ctx, test::WorldPair& worlds, std::int32_t x, std::int32_t y, std::int32_t radius,
                 Owner owner) {
    for (std::int32_t tx = x - radius; tx <= x + radius; ++tx) {
        for (std::int32_t ty = y - radius; ty <= y + radius; ++ty) {
            if (tx < 0 || ty < 0 || tx >= 0x300 || ty >= 0x300) {
                continue;
            }
            const auto flags = static_cast<std::uint8_t>(ctx.rng().Below(256) & 0x56);
            const std::uint8_t o = owner(tx, ty);
            worlds.Both([&](CGameData* w) {
                Tile& t = w->tiles[tx][ty];
                t.flags = flags;
                t.owner = o;
                t.settler = 0;
                t.object_number = 0;
                t.area = static_cast<std::uint16_t>(1 + (tx / 40) * 10 + ty / 40);
            });
        }
    }
}

void InstallTerritoryStubs(test::CalleeStubs& stubs, bool cycle) {
    InstallStubs(stubs);  // ResetSettlerToIdle, SplitAreaAtTile
    stubs.Install<3, kSetOwnerOfFreeTile>([](CGameData* w, std::uint32_t, const std::uint32_t* args) {
        Tile& t = TileAt(w, args[0], args[1]);
        if (t.owner == kFree) {
            t.owner = static_cast<std::uint8_t>(args[2]);
        }
        return 0u;
    });
    if (!cycle) {
        stubs.Install<2, kDestroyBuildingAt>();
        stubs.Install<2, kClearTileOwner>([](CGameData* w, std::uint32_t, const std::uint32_t* args) {
            TileAt(w, args[0], args[1]).owner = kFree;
            return 0u;
        });
        return;
    }
    // The rest of the destruction cycle runs; its other callees are leaves.
    stubs.Install<2, 0x0051b770>();                         // UnblockTile (region floods)
    stubs.Install<4, 0x005046b0>();                         // DropGoodNear (package B)
    stubs.Install<5, 0x00544f20>();                         // MapEffects::Spawn_544f20
    stubs.Install<4, 0x00545c80>();                         // MapEffects::RemoveMatching
}

void CompareRecalculate(test::Context& ctx, TileFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTerritoryWorld);
    test::CalleeStubs stubs;
    InstallTerritoryStubs(stubs, false);
    DebugLogRecorder logs;
    SetRingOffsets(worlds, 5200);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t mapSize = ctx.rng().Chance(1, 4) ? 120 + ctx.rng().Below(200) : 384;
        const auto cx = static_cast<std::int32_t>(ctx.rng().Below(mapSize));
        const auto cy = static_cast<std::int32_t>(ctx.rng().Below(mapSize));
        worlds.Both([&](CGameData* w) { w->map_size = mapSize; });
        const std::int32_t split = cx + ctx.rng().Range(-30, 30);
        ClearWindow(ctx, worlds, cx, cy, 102, [&](std::int32_t tx, std::int32_t ty) -> std::uint8_t {
            if ((tx + ty) % 7 == 0) {
                return kFree;
            }
            return static_cast<std::uint8_t>(tx < split ? 0 : 1 + (ty & 1));
        });
        const std::uint32_t count = ctx.rng().Below(7);
        for (std::uint32_t k = 0; k < count; ++k) {
            static constexpr std::uint8_t kTypes[] = {0xf, 0x10, 0x12, 0x12, 3};
            const std::uint8_t type = kTypes[ctx.rng().Below(5)];
            const auto bx = static_cast<std::int32_t>(cx + ctx.rng().Range(-95, 95));
            const auto by = static_cast<std::int32_t>(cy + ctx.rng().Range(-95, 95));
            if (bx < 10 || by < 10 || bx > 0x2f0 || by > 0x2f0) {
                continue;
            }
            PlaceBuilding(worlds, 1 + k, type, static_cast<std::uint8_t>(ctx.rng().Below(4)),
                          static_cast<std::uint8_t>(ctx.rng().Below(4)), static_cast<std::uint16_t>(bx),
                          static_cast<std::uint16_t>(by), static_cast<std::uint8_t>(ctx.rng().Chance(4, 5) ? 0 : 1),
                          static_cast<std::uint16_t>(ctx.rng().Chance(5, 6) ? 1 + ctx.rng().Below(100) : 0));
        }
        stubs.SetWorld(worlds.original());
        const std::uint32_t expected = Original_CGameData_RecalculateTerritory(worlds.original(), nullptr, cx, cy);
        const std::vector<test::StubCall> expectedCalls = stubs.Take();
        const std::vector<DebugLogCall> expectedLogs = logs.Take();
        stubs.SetWorld(worlds.ours());
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, cx, cy);
        const std::vector<test::StubCall> actualCalls = stubs.Take();
        const std::vector<DebugLogCall> actualLogs = logs.Take();
        if (!S3RE_EXPECT_EQ(expected, actual) ||
            !S3RE_EXPECT(expectedCalls == actualCalls) || !S3RE_EXPECT(expectedLogs == actualLogs) ||
            ((i % 8 == 7 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or before): centre (%d, %d) map %u", i, cx, cy, mapSize);
            return;
        }
    }
}

// Wrong: the claim of a building's neighbour ring (distance 1) is not full.
std::uint32_t __fastcall WrongRecalculateTerritory(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y) {
    const std::int32_t saved = world->offsets[1].distance;
    world->offsets[1].distance = 2;
    const std::uint32_t result = CGameData_RecalculateTerritory(world, edx, x, y);
    world->offsets[1].distance = saved;
    return result;
}

// The destruction cycle: player 1's garrisoned tower T1 is crushed; its
// garrison leaves (ReleaseSiteWorkers) and the territory is recalculated
// without it; land of player 1 within the claim of player 0's tower T0 goes
// to player 0, and player 1's construction site S standing there is
// destroyed (DestroyBuildingAt -> CrushBuilding -> ReleaseSiteWorkers, which
// sends its diggers away). The original runs the whole chain as original
// code (the four cycle functions jump to their trampolines); ours runs our
// four replacements.
void CompareCycle(test::Context& ctx, CycleCrushFn candidate, int cases, int& recursions) {
    test::WorldPair worlds(ctx, kTerritoryWorld);
    test::CalleeStubs stubs;
    InstallTerritoryStubs(stubs, true);
    DebugLogRecorder logs;
    SetRingOffsets(worlds, 5200);
    worlds.Both([&](CGameData* w) {
        w->map_size = 384;
        w->settler_high_water = 60;
        w->building_high_water = 10;
    });
    for (int i = 0; i < cases; ++i) {
        const auto x0 = static_cast<std::int32_t>(130 + ctx.rng().Below(40));
        const auto y0 = static_cast<std::int32_t>(130 + ctx.rng().Below(100));
        const std::int32_t gap = 18 + static_cast<std::int32_t>(ctx.rng().Below(14));
        const std::int32_t x1 = x0 + gap;
        const std::int32_t y1 = y0 + ctx.rng().Range(-4, 4);
        const std::int32_t xs = x0 + gap / 2 + ctx.rng().Range(-2, 3);
        const std::int32_t ys = y0 + ctx.rng().Range(-6, 6);
        ClearWindow(ctx, worlds, x1, y1, 110, [&](std::int32_t tx, std::int32_t) -> std::uint8_t {
            return static_cast<std::uint8_t>(tx < x0 + gap / 3 ? 0 : 1);
        });
        worlds.Both([&](CGameData* w) {
            for (std::uint32_t n = 1; n <= 10; ++n) {
                w->buildings[n].owner = kFree;
            }
            for (int p = 0; p < 4; ++p) {
                w->players[p].team = p;
                w->players[p].race = p & 3;
            }
            w->pending_event_index = {0, 0};
            w->event_index = {0, 0};
        });
        static constexpr std::uint8_t kSiteTypes[] = {3, 4, 0x1a, 0x13, 7, 0x22};
        const std::uint8_t siteType = kSiteTypes[ctx.rng().Below(6)];
        // Garrisons: soldiers 1..3 (T0), 4..6 (T1) chained by next.
        PlaceBuilding(worlds, 1, 0x12, 0, 0, static_cast<std::uint16_t>(x0), static_cast<std::uint16_t>(y0), 0, 1);
        PlaceBuilding(worlds, 2, static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0x10 : 0xf), 1, 1,
                      static_cast<std::uint16_t>(x1), static_cast<std::uint16_t>(y1), 0, 4);
        PlaceBuilding(worlds, 3, siteType, 1, 1, static_cast<std::uint16_t>(xs), static_cast<std::uint16_t>(ys),
                      static_cast<std::uint8_t>(1 + ctx.rng().Below(3)), 0);
        for (std::uint32_t n = 1; n <= 60; ++n) {
            const auto job = static_cast<std::uint16_t>(n <= 6 ? 0x8c : ctx.rng().Chance(1, 2) ? 5 : 1);
            const auto building = static_cast<std::uint16_t>(n <= 3 ? 1 : n <= 6 ? 2 : ctx.rng().Chance(2, 3) ? 3 : 0);
            const auto next = static_cast<std::uint16_t>(n == 3 || n == 6 ? 0 : n < 6 ? n + 1 : 0);
            worlds.Both([&](CGameData* w) {
                layout::Settler& s = w->settlers[n];
                s.owner = static_cast<std::uint8_t>(n <= 3 ? 0 : 1);
                s.art = static_cast<std::uint8_t>(n <= 6 ? 0x20 : 1);
                s.job = job;
                s.building = building;
                s.next = next;
                s.flags = 0;
                s.x = static_cast<std::uint16_t>(xs);
                s.y = static_cast<std::uint16_t>(ys);
            });
        }
        worlds.Both([&](CGameData* w) {
            for (std::uint32_t b = 1; b <= 3; ++b) {
                layout::Building& r = w->buildings[b];
                r.piles[0] = r.piles[1] = r.piles[2] = 0;
                r.pile_ring = 0;
                r.levelling_slot = 0xffff;
                r.digger_slots = r.builder_slots = 0;
                for (std::uint16_t& head : w->areas[TileAt(w, r.door_x, r.door_y).area].requests_by_goods) {
                    head = 0;
                }
            }
            w->buildings[1].levelling_slot = 1;  // the towers' first soldiers
            w->buildings[2].levelling_slot = 4;
            w->buildings[3].levelling_slot = 7;  // the site's levelling slot
        });
        // Original chain.
        stubs.SetWorld(worlds.original());
        std::uint32_t expected = 0;
        {
            test::ScopedJump crush(0x004d9250, reinterpret_cast<const void*>(Original_CGameData_CrushBuilding));
            test::ScopedJump release(0x0050e3b0, reinterpret_cast<const void*>(Original_CGameData_ReleaseSiteWorkers));
            test::ScopedJump destroy(0x004d97b0, reinterpret_cast<const void*>(Original_CGameData_DestroyBuildingAt));
            test::ScopedJump recalc(0x00519a90, reinterpret_cast<const void*>(Original_CGameData_RecalculateTerritory));
            expected = Original_CGameData_CrushBuilding(worlds.original(), nullptr, 2);
        }
        const std::vector<test::StubCall> expectedCalls = stubs.Take();
        const std::vector<DebugLogCall> expectedLogs = logs.Take();
        stubs.SetWorld(worlds.ours());
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, 2);
        const std::vector<test::StubCall> actualCalls = stubs.Take();
        const std::vector<DebugLogCall> actualLogs = logs.Take();
        if (worlds.ours()->buildings[3].owner == kFree) {
            ++recursions;  // the site was destroyed inside the recalculation
        }
        if (!S3RE_EXPECT_EQ(expected, actual) || !S3RE_EXPECT(expectedCalls == actualCalls) ||
            !S3RE_EXPECT(expectedLogs == actualLogs) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: towers (%d, %d) (%d, %d), site 0x%x at (%d, %d)", i, x0, y0, x1,
                     y1, siteType, xs, ys);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(CGameData_RecalculateTerritory_MatchesOriginal) {
    CompareRecalculate(ctx, &CGameData_RecalculateTerritory, 150);
}
S3RE_TEST(CGameData_RecalculateTerritory_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareRecalculate(probe, &WrongRecalculateTerritory, 40);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_DestructionCycle_MatchesOriginal) {
    int recursions = 0;
    CompareCycle(ctx, &CGameData_CrushBuilding, 40, recursions);
    // Most cases must really go through the recursion.
    S3RE_EXPECT(recursions >= 20);
}

}  // namespace s3re::game
