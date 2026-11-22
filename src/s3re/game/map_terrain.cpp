// Terrain changes and territory border flags on the map tiles.
//
// Tile fields used here (tile (x, y) at world+0x67444 + (x*0x300 + y)*24):
// +0x6 terrain type (the high nibble is non-zero for land a player can own),
// +0x8 owner (player, 0xff = none), +0x9 draw_flags (0x40 = territory
// border, kTileDrawBorder).
//
// CGameData::ReplaceTerrainF3 (0x0051a400): (newType): calls
//   CGameData::ReplaceTerrainType(0xf3, newType) and returns its EAX. Used
//   four times by CGameData::GenerateRandomMap. ret 4.
// CGameData::SpreadTerrainRandomly (0x00519330): (from, neighbour, to, percent):
//   for x in [1, mapSize-1), y in [1, mapSize-1) (signed bounds; nothing if
//   mapSize - 1 <= 1), a tile whose terrain is `from` and that has a
//   neighbour of terrain `neighbour` becomes `to` (low byte) with probability
//   percent/100: r = Random() & 0xffff, (r * 100) >> 16 < percent (signed).
//   The six neighbours are checked in the order (x+1, y), (x-1, y),
//   (x, y-1), (x, y+1), (x+1, y+1), (x-1, y-1) and Random is drawn only when
//   one matches. Changed tiles are seen by the tiles scanned after them.
//   Terrain bytes are compared zero-extended with the full 32-bit `from`
//   and `neighbour`. EAX = mapSize - 1 (callers ignore it). ret 0x10.
// CGameData::UpdateBorderFlagsAround (0x0051a460): (x, y): for the first 7
//   entries of the offset table at world+0x110a588 (int32 dx, dy; 16-byte
//   stride: the tile itself and its six neighbours), each tile (x+dx, y+dy)
//   inside the map border (CGameData::IsInsideMapBorder) gets border flag
//   0x40 set if it is owned land (terrain & 0xf0, owner != 0xff) and one of
//   its six neighbours is owned land of another owner (terrain & 0xf0 and
//   owner differs), and cleared otherwise. Neighbour order: (x+1, y),
//   (x+1, y+1), (x, y+1), (x-1, y), (x-1, y-1), (x, y-1). EAX = 0. ret 8.
// CGameData::GrowTerrain (0x00519470): (from, into, percent):
//   SpreadTerrainRandomly(from, into, 0xf0, percent), then
//   ReplaceTerrainType(0xf0, into): tiles next to `into` become `into` with
//   the probability, through the marker 0xf0 so that tiles converted in this
//   pass don't spread further in it. EAX = ReplaceTerrainType's. ret 0xc.
// CGameData::UpdateAllBorderFlags (0x0051a420): UpdateBorderFlagsAround(x,
//   y) for every tile, x outer, y inner (unsigned, map size re-read). EAX =
//   mapSize (two callers tail-return it). Plain ret.
// All thiscall (ECX = world).

#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>

namespace s3re::game {

namespace {

using layout::Tile;

constexpr std::uint8_t kBorderFlag = layout::kTileDrawBorder;
constexpr std::uint32_t kNoOwner = 0xff;
constexpr std::uint32_t kAroundEntries = 7;  // world->offsets[0..6]

// Tiles are carried as 32-bit byte offsets from the world (TileOffset) and
// stepped to their neighbours like in the original.
constexpr std::int32_t kRow = static_cast<std::int32_t>(kTileRowBytes);  // x + 1
constexpr std::int32_t kColumn = static_cast<std::int32_t>(kTileBytes);   // y + 1

std::uint32_t Terrain(CGameData* world, std::uint32_t tile) {
    return TileAtOffset(world, tile).terrain;
}

Tile& TileAt(CGameData* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

}  // namespace

using ReplaceF3Fn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t newType);
using SpreadTerrainFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t from,
                                                   std::uint32_t neighbour, std::uint32_t to,
                                                   std::uint32_t percent);
using AroundFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x,
                                            std::uint32_t y);

// Purpose: Map generation: turns the placeholder terrain 0xf3 into a real terrain type.
std::uint32_t __fastcall CGameData_ReplaceTerrainF3(CGameData* world, void*, std::uint32_t newType) {
    return CGameData_ReplaceTerrainType(world, nullptr, 0xf3, newType);
}

// Purpose: Map generation: tiles of one terrain next to another terrain randomly change, so terrain edges
// look natural instead of straight.
std::uint32_t __fastcall CGameData_SpreadTerrainRandomly(CGameData* world, void*, std::uint32_t from,
                                                         std::uint32_t neighbour, std::uint32_t to,
                                                         std::uint32_t percent) {
    auto last = [&] { return static_cast<std::int32_t>(world->map_size - 1); };
    for (std::int32_t x = 1; x < last(); ++x) {
        for (std::int32_t y = 1; y < last(); ++y) {
            const std::uint32_t tile = TileOffset(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
            if (Terrain(world, tile) != from) {
                continue;
            }
            const bool nearby = Terrain(world, tile + kRow) == neighbour || Terrain(world, tile - kRow) == neighbour ||
                                Terrain(world, tile - kColumn) == neighbour || Terrain(world, tile + kColumn) == neighbour ||
                                Terrain(world, tile + kRow + kColumn) == neighbour ||
                                Terrain(world, tile - kRow - kColumn) == neighbour;
            if (!nearby) {
                continue;
            }
            const std::int32_t roll = static_cast<std::int32_t>((CGameData_Random(world, nullptr) & 0xffffu) * 100u) >> 16;
            if (roll < static_cast<std::int32_t>(percent)) {
                TileAtOffset(world, tile).terrain = static_cast<std::uint8_t>(to);
            }
        }
    }
    return world->map_size - 1;
}

// Purpose: Updates the 'territory border' mark of a tile and its neighbours: a tile is on the border if it
// is owned land next to land of another owner. Probably what the game uses to draw the border
// stones.
std::uint32_t __fastcall CGameData_UpdateBorderFlagsAround(CGameData* world, void*, std::uint32_t x,
                                                           std::uint32_t y) {
    static constexpr std::int32_t kNeighbours[6] = {kRow, kRow + kColumn, kColumn, -kRow, -kRow - kColumn, -kColumn};
    for (std::uint32_t entry = 0; entry < kAroundEntries; ++entry) {
        const std::uint32_t tx = x + static_cast<std::uint32_t>(world->offsets[entry].dx);
        const std::uint32_t ty = y + static_cast<std::uint32_t>(world->offsets[entry].dy);
        if (!CGameData_IsInsideMapBorder(world, nullptr, tx, ty)) {
            continue;
        }
        const std::uint32_t tile = TileOffset(tx, ty);
        const std::uint32_t owner = TileAtOffset(world, tile).owner;
        bool border = false;
        if ((Terrain(world, tile) & 0xf0) != 0 && owner != kNoOwner) {
            for (const std::int32_t step : kNeighbours) {
                const std::uint32_t neighbour = tile + step;
                if (TileAtOffset(world, neighbour).owner != owner &&
                    (Terrain(world, neighbour) & 0xf0) != 0) {
                    border = true;
                    break;
                }
            }
        }
        std::uint8_t& flags = TileAtOffset(world, tile).draw_flags;
        flags = border ? static_cast<std::uint8_t>(flags | kBorderFlag) : static_cast<std::uint8_t>(flags & ~kBorderFlag);
    }
    return 0;
}

S3RE_HOOK(0x0051a400, CGameData_ReplaceTerrainF3, ReplaceF3Fn, Original_CGameData_ReplaceTerrainF3,
          CGameData_ReplaceTerrainF3);
S3RE_HOOK(0x00519330, CGameData_SpreadTerrainRandomly, SpreadTerrainFn,
          Original_CGameData_SpreadTerrainRandomly, CGameData_SpreadTerrainRandomly);
S3RE_HOOK(0x0051a460, CGameData_UpdateBorderFlagsAround, AroundFn, Original_CGameData_UpdateBorderFlagsAround,
          CGameData_UpdateBorderFlagsAround);

using GrowTerrainFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t from,
                                                 std::uint32_t into, std::uint32_t percent);
using NoArgsFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx);

// Purpose: Map generation: lets one terrain grow randomly into its neighbour terrain by one step (via a
// temporary marker, so new tiles don't keep growing in the same pass).
std::uint32_t __fastcall CGameData_GrowTerrain(CGameData* world, void*, std::uint32_t from, std::uint32_t into,
                                               std::uint32_t percent) {
    CGameData_SpreadTerrainRandomly(world, nullptr, from, into, 0xf0, percent);
    return CGameData_ReplaceTerrainType(world, nullptr, 0xf0, into);
}

// Purpose: Recomputes the territory border marks of the whole map (e.g. at game start or after big
// territory changes).
std::uint32_t __fastcall CGameData_UpdateAllBorderFlags(CGameData* world, void*) {
    auto size = [&] { return world->map_size; };
    for (std::uint32_t x = 0; x < size(); ++x) {
        for (std::uint32_t y = 0; y < size(); ++y) {
            CGameData_UpdateBorderFlagsAround(world, nullptr, x, y);
        }
    }
    return size();
}

S3RE_HOOK(0x00519470, CGameData_GrowTerrain, GrowTerrainFn, Original_CGameData_GrowTerrain, CGameData_GrowTerrain);
S3RE_HOOK(0x0051a420, CGameData_UpdateAllBorderFlags, NoArgsFn, Original_CGameData_UpdateAllBorderFlags,
          CGameData_UpdateAllBorderFlags);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kTestWorld = offsetof(CGameData, offsets) + 64 * sizeof(layout::MapOffset);

// The real offset table starts with the tile itself and its six
// neighbours; tests use that, sometimes with a random entry.
void SetUpOffsets(test::Context& ctx, test::WorldPair& worlds) {
    static constexpr std::int32_t kOffsets[7][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {-1, 0}, {-1, -1}, {0, -1}};
    for (std::uint32_t i = 0; i < kAroundEntries; ++i) {
        std::int32_t dx = kOffsets[i][0];
        std::int32_t dy = kOffsets[i][1];
        if (ctx.rng().Chance(1, 16)) {
            dx = ctx.rng().Range(-3, 3);
            dy = ctx.rng().Range(-3, 3);
        }
        worlds.Both([&](CGameData* w) {
            w->offsets[i].dx = dx;
            w->offsets[i].dy = dy;
        });
    }
}

// Owners and terrains of the tiles within 4 of (x, y) (inside the map): few
// owners, mostly land.
void SetUpTerritory(test::Context& ctx, test::WorldPair& worlds, std::uint32_t size, std::uint32_t x,
                    std::uint32_t y) {
    for (std::int32_t dx = -4; dx <= 4; ++dx) {
        for (std::int32_t dy = -4; dy <= 4; ++dy) {
            const std::uint32_t tx = x + dx;
            const std::uint32_t ty = y + dy;
            if (tx >= size || ty >= size) {
                continue;
            }
            const std::uint32_t owner = ctx.rng().Chance(1, 6) ? kNoOwner : ctx.rng().Below(3);
            worlds.Both([&](CGameData* w) { TileAt(w, tx, ty).owner = static_cast<std::uint8_t>(owner); });
            const std::uint8_t terrain = static_cast<std::uint8_t>(ctx.rng().Next32());
            const std::uint8_t stored = ctx.rng().Chance(1, 5) ? static_cast<std::uint8_t>(terrain & 0x0f) : terrain;
            worlds.Both([&](CGameData* w) { TileAt(w, tx, ty).terrain = stored; });
        }
    }
}

// Wrong: ignores the terrain of the neighbours.
std::uint32_t __fastcall WrongUpdateBorderFlagsAround(CGameData* world, void*, std::uint32_t x,
                                                      std::uint32_t y) {
    static constexpr std::int32_t kNeighbours[6] = {kRow, kRow + kColumn, kColumn, -kRow, -kRow - kColumn, -kColumn};
    for (std::uint32_t entry = 0; entry < kAroundEntries; ++entry) {
        const std::uint32_t tx = x + static_cast<std::uint32_t>(world->offsets[entry].dx);
        const std::uint32_t ty = y + static_cast<std::uint32_t>(world->offsets[entry].dy);
        if (!CGameData_IsInsideMapBorder(world, nullptr, tx, ty)) {
            continue;
        }
        const std::uint32_t tile = TileOffset(tx, ty);
        const std::uint32_t owner = TileAtOffset(world, tile).owner;
        bool border = false;
        if ((Terrain(world, tile) & 0xf0) != 0 && owner != kNoOwner) {
            for (const std::int32_t step : kNeighbours) {
                border = border || TileAtOffset(world, tile + step).owner != owner;
            }
        }
        std::uint8_t& flags = TileAtOffset(world, tile).draw_flags;
        flags = border ? static_cast<std::uint8_t>(flags | kBorderFlag) : static_cast<std::uint8_t>(flags & ~kBorderFlag);
    }
    return 0;
}

void CompareBorderFlags(test::Context& ctx, AroundFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = ctx.rng().Chance(1, 2) ? 384 : 8 + ctx.rng().Below(40);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        // Anywhere on the map; the edges often on the small maps.
        const std::uint32_t x = ctx.rng().Below(size);
        const std::uint32_t y = ctx.rng().Below(size);
        SetUpOffsets(ctx, worlds);
        SetUpTerritory(ctx, worlds, size, x, y);
        const std::uint32_t expected = Original_CGameData_UpdateBorderFlagsAround(worlds.original(), nullptr, x, y);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, x, y);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 32 == 31 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 31 before it)", i);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(CGameData_UpdateBorderFlagsAround_MatchesOriginal) {
    CompareBorderFlags(ctx, &CGameData_UpdateBorderFlagsAround, 3000);
}
S3RE_TEST(CGameData_UpdateBorderFlagsAround_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareBorderFlags(probe, &WrongUpdateBorderFlagsAround, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CGameData_SpreadTerrainRandomly_MatchesOriginal) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < 40; ++i) {
        const std::uint32_t size = ctx.rng().Chance(1, 4) ? 384 : ctx.rng().Below(60);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        // Few terrain types, so tiles match often.
        const std::uint32_t limit = size < 384 ? size + 1 : 384;
        for (std::uint32_t x = 0; x < limit; ++x) {
            for (std::uint32_t y = 0; y < limit; ++y) {
                const auto terrain = static_cast<std::uint8_t>(ctx.rng().Below(4));
                worlds.Both([&](CGameData* w) { TileAt(w, x, y).terrain = terrain; });
            }
        }
        const std::uint32_t from = ctx.rng().Below(4);
        const std::uint32_t neighbour = ctx.rng().Chance(1, 8) ? from : ctx.rng().Below(4);
        std::uint32_t to = ctx.rng().Below(4);
        if (ctx.rng().Chance(1, 4)) {
            to |= ctx.rng().Next32() & ~0xffu;
        }
        const std::uint32_t percent = ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(110);
        const auto a = static_cast<std::uint16_t>(ctx.rng().Next32());
        const std::uint32_t bc = ctx.rng().Next32();
        worlds.Both([&](CGameData* w) {
            w->random_state[0] = a;
            w->random_state[1] = static_cast<std::uint16_t>(bc);
            w->random_state[2] = static_cast<std::uint16_t>(bc >> 16);
        });
        const std::uint32_t expected =
            Original_CGameData_SpreadTerrainRandomly(worlds.original(), nullptr, from, neighbour, to, percent);
        const std::uint32_t actual = CGameData_SpreadTerrainRandomly(worlds.ours(), nullptr, from, neighbour, to, percent);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: map %u, %u neighbour %u -> 0x%x at %u%%", i, size, from, neighbour, to,
                     percent);
            return;
        }
    }
}

S3RE_TEST(CGameData_ReplaceTerrainF3_MatchesOriginal) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < 20; ++i) {
        const std::uint32_t size = ctx.rng().Chance(1, 4) ? 384 : ctx.rng().Below(60);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        for (std::uint32_t x = 0; x < 64; ++x) {
            for (std::uint32_t y = 0; y < 64; ++y) {
                if (ctx.rng().Chance(1, 3)) {
                    worlds.Both([&](CGameData* w) { TileAt(w, x, y).terrain = 0xf3; });
                }
            }
        }
        const std::uint32_t newType = ctx.rng().Chance(1, 2) ? ctx.rng().Below(256) : ctx.rng().Next32();
        const std::uint32_t expected = Original_CGameData_ReplaceTerrainF3(worlds.original(), nullptr, newType);
        const std::uint32_t actual = CGameData_ReplaceTerrainF3(worlds.ours(), nullptr, newType);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

S3RE_TEST(CGameData_GrowTerrain_MatchesOriginal) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < 30; ++i) {
        const std::uint32_t size = ctx.rng().Chance(1, 4) ? 384 : ctx.rng().Below(60);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        const std::uint32_t limit = size < 384 ? size + 1 : 384;
        for (std::uint32_t x = 0; x < limit; ++x) {
            for (std::uint32_t y = 0; y < limit; ++y) {
                const auto terrain = static_cast<std::uint8_t>(ctx.rng().Below(4));
                worlds.Both([&](CGameData* w) { TileAt(w, x, y).terrain = terrain; });
            }
        }
        const std::uint32_t from = ctx.rng().Below(4);
        const std::uint32_t into = ctx.rng().Below(4);
        const std::uint32_t percent = ctx.rng().Below(110);
        const std::uint32_t ab = ctx.rng().Next32();
        worlds.Both([&](CGameData* w) {
            w->random_state[0] = static_cast<std::uint16_t>(ab);
            w->random_state[1] = static_cast<std::uint16_t>(ab >> 16);
        });
        const std::uint32_t expected = Original_CGameData_GrowTerrain(worlds.original(), nullptr, from, into, percent);
        const std::uint32_t actual = CGameData_GrowTerrain(worlds.ours(), nullptr, from, into, percent);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

S3RE_TEST(CGameData_UpdateAllBorderFlags_MatchesOriginal) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < 12; ++i) {
        const std::uint32_t size = ctx.rng().Chance(1, 3) ? 384 : ctx.rng().Below(80);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        SetUpOffsets(ctx, worlds);
        const std::uint32_t limit = size < 384 ? size : 384;
        for (std::uint32_t x = 0; x < limit; x += 7) {
            for (std::uint32_t y = 0; y < limit; y += 7) {
                SetUpTerritory(ctx, worlds, size, x, y);
            }
        }
        const std::uint32_t expected = Original_CGameData_UpdateAllBorderFlags(worlds.original(), nullptr);
        const std::uint32_t actual = CGameData_UpdateAllBorderFlags(worlds.ours(), nullptr);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: map %u", i, size);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// CGameData::TileShade (0x00519720): (x, y) -> a brightness 0..127 for the
// tile from the heights (byte +0x4) of its six neighbours relative to its
// own: each difference d becomes d+4 above 4, d-4 below -4, else 2d; then
// s = 2 * (f(x, y-1) - f(x, y+1)) - f(x+1, y+1) - f(x-1, y) + f(x+1, y) +
// f(x-1, y-1), shade = s * 45000 / 65536 (signed, rounded toward zero) +
// 64, clamped to 0..127. Probably the landscape lighting (slopes facing
// one way lighter). thiscall, ret 8; EAX = the shade.
// ---------------------------------------------------------------------------

using TileShadeFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);

// Purpose: How light a tile is drawn: computed from the slope of the land around it (probably the
// landscape's lighting, so hills look three-dimensional).
std::uint32_t __fastcall CGameData_TileShade(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t tile = TileOffset(x, y);
    const std::int32_t own = TileAtOffset(world, tile).height;
    auto f = [&](std::int32_t step) {
        const std::int32_t d = TileAtOffset(world, tile + static_cast<std::uint32_t>(step)).height - own;
        return d > 4 ? d + 4 : d < -4 ? d - 4 : 2 * d;
    };
    const std::int32_t sum = 2 * (f(-kColumn) - f(kColumn)) - f(kRow + kColumn) - f(-kRow) + f(kRow) +
                             f(-kRow - kColumn);
    const std::int32_t shade = sum * 45000 / 65536 + 64;
    return static_cast<std::uint32_t>(shade < 0 ? 0 : shade > 127 ? 127 : shade);
}

S3RE_HOOK(0x00519720, CGameData_TileShade, TileShadeFn, Original_CGameData_TileShade, CGameData_TileShade);

namespace {

// Tiles (1..30, 1..30) with heights near each other (all branches of the
// difference mapping) or anywhere in 0..255 (clamping).
void CompareTileShade(test::Context& ctx, TileShadeFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t x = 1 + ctx.rng().Below(30);
        const std::uint32_t y = 1 + ctx.rng().Below(30);
        const bool wide = ctx.rng().Chance(1, 4);
        const std::uint32_t base = ctx.rng().Below(256);
        for (std::uint32_t dx = 0; dx < 3; ++dx) {
            for (std::uint32_t dy = 0; dy < 3; ++dy) {
                const std::uint32_t h = wide ? ctx.rng().Below(256) : (base + ctx.rng().Below(25)) & 0xff;
                worlds.Both([&](CGameData* w) { TileAt(w, x + dx - 1, y + dy - 1).height = static_cast<std::uint8_t>(h); });
            }
        }
        const std::uint32_t expected = Original_CGameData_TileShade(worlds.original(), nullptr, x, y);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, x, y);
        if (!S3RE_EXPECT_EQ(expected, actual) || (i % 4096 == 4095 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%u, %u)", i, x, y);
            return;
        }
    }
}

// Wrong: rounds toward minus infinity (arithmetic shift) instead of toward zero.
std::uint32_t __fastcall WrongTileShade(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t tile = TileOffset(x, y);
    const std::int32_t own = TileAtOffset(world, tile).height;
    auto f = [&](std::int32_t step) {
        const std::int32_t d = TileAtOffset(world, tile + static_cast<std::uint32_t>(step)).height - own;
        return d > 4 ? d + 4 : d < -4 ? d - 4 : 2 * d;
    };
    const std::int32_t sum = 2 * (f(-kColumn) - f(kColumn)) - f(kRow + kColumn) - f(-kRow) + f(kRow) +
                             f(-kRow - kColumn);
    const std::int32_t shade = ((sum * 45000) >> 16) + 64;
    return static_cast<std::uint32_t>(shade < 0 ? 0 : shade > 127 ? 127 : shade);
}

}  // namespace

S3RE_TEST(CGameData_TileShade_MatchesOriginal) {
    CompareTileShade(ctx, &CGameData_TileShade, 100000);
}
S3RE_TEST(CGameData_TileShade_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareTileShade(probe, &WrongTileShade, 20000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
