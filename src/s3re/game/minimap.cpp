// The minimap (world+0x1223656, g_minimapWidth x g_minimapWidth 16-bit
// pixels), recomputed one interleaved 1/64 per step by UpdateEnvironment.
//
// GameWorld::UpdateMinimapSlice (0x004fb520): (slice 0..63) marks the
//   slice in g_minimapSliceDirty (0x7ad4f8, int32[64]) and recomputes the
//   pixels (column c, row r) with r = row0 + 8k, c = col0 + 8j below the
//   width W (0x730f28, re-read after every pixel), where row0/col0 are the
//   high/low three bits of a bit permutation of the slice (so neighbouring
//   slices spread over the map). With scale = (map size << 8) / W
//   (signed), pixel = MinimapPixel(c * scale >> 8, r * scale >> 8,
//   (c * scale + scale - 256) >> 8, (r * scale + scale - 256) >> 8): the
//   tile rectangle the pixel covers. minimap[W * r + c] = AX. ret 4, EAX
//   not used.
// GameWorld::MinimapPixel (0x004fb720): (x0, y0, x1, y1) -> the pixel
//   colour packed in the screen's format (g_pixelChannelShifts 0x7dfba0,
//   scales 0x7dfbac..: ((scale * c) >> 8) << shift per channel). The tile
//   (x0, y0) gives the brightness from its fog level (draw_flags & 7):
//   mode 0x730f58 == 0: level * 256 / 7 (signed); otherwise ((shade + 100)
//   * level * 256) / 2485. Brightness 0 -> black. Then, for fog level >= 3
//   and 0x730f60 set, the rectangle (x0..x1, y0..y1, signed bounds) is
//   scanned for settlers: mode 0: any settler (0x730f60 == 2) or one whose
//   type has a non-zero entry in the table 0x658f64 makes the pixel black;
//   mode != 0: the first such settler colours it by its owner (the local
//   player white, the team 0xdc cyan, others red; type 0x17 without flag
//   0x10 of another team counts as the tile's owner, or the local player
//   for a free tile; with 0x730f60 != 2 such settlers of another team are
//   not seen at all). Mode != 0 then scans for border tiles (draw_flags
//   0x40, fog >= 3): the first colours the pixel by the tile's owner (0xa0
//   white, team 0x8c, others red 0xa0). Finally: with 0x730f5c and a
//   footprint tile (flags 8): mode 0 the owner's colour at half intensity
//   (colour index table 0x7ace64, RGB dwords 0x7ace90), mode != 0 black;
//   otherwise mode 0 the owner's colour (grey 110/256 for nobody), mode
//   != 0 the terrain's colour (water, mode 1: everything else green).
//   ret 0x10. The full EAX is reproduced (the caller stores AX).
// thiscall (ECX = world).

#include "game/globals.h"
#include "game/tables.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

using layout::Settler;
using layout::Tile;
using U = std::uint32_t;

constexpr std::uintptr_t kMinimapPixel = 0x004fb720;

Tile& TileAt(GameWorld* world, U offset) {
    return TileAtOffset(world, offset);
}

std::int32_t Team(GameWorld* world, U player) {
    return (world->players + player)->team;
}

struct Rgb {
    U a, b, c;  // the channels in the order of the scales at 0x7dfbac.. reversed (see Pack)
};

const U* PlayerColour(U owner) {
    const U index = g_playerColourIndex[owner];
    return &g_playerColours[index].c0;
}

// esi, edi, ebx of the original -> the packed pixel.
U Pack(U esi, U edi, U ebx) {
    const auto* shifts = g_pixelFormat->shift;
    const auto* scales = g_pixelFormat->scale;
    return (((scales[0] * ebx) >> 8) << (shifts[0] & 31)) + (((scales[1] * edi) >> 8) << (shifts[1] & 31)) +
           (((scales[2] * esi) >> 8) << (shifts[2] & 31));
}

}  // namespace

using PixelFn = U(__fastcall*)(GameWorld* world, void* edx, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                               std::int32_t y1);
using SliceFn = void(__fastcall*)(GameWorld* world, void* edx, U slice);

// Purpose: The colour of one minimap pixel: territory or terrain colour of the land it covers, darkened by
// the fog of war, with settlers, borders and buildings marked depending on the minimap options.
U __fastcall GameWorld_MinimapPixel(GameWorld* world, void*, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                                    std::int32_t y1) {
    U esi = 0;
    U edi = 0;
    U ebx = 0;
    const U origin = TileOffset(static_cast<U>(x0), static_cast<U>(y0));
    const Tile& t = TileAt(world, origin);
    const U level = t.draw_flags & 7u;
    const std::int32_t mode = g_minimapMode;
    const U local = static_cast<U>(world->local_player);
    // Visits the rectangle's tiles (rows y0..y1 outside, x0..x1 inside).
    auto scan = [&](auto visit) {
        if (y0 > y1) {
            return;
        }
        U row = origin;
        for (std::int32_t j = y1 - y0 + 1; j != 0; --j) {
            if (x0 <= x1) {
                U tile = row;
                for (std::int32_t i = x1 - x0 + 1; i != 0; --i) {
                    visit(TileAt(world, tile));
                    tile += kTileRowBytes;
                }
            }
            row += kTileBytes;
        }
    };
    auto teamColour = [&](U player, U own, U friendly) {
        if (player == local) {
            esi = edi = ebx = own;
        } else if (Team(world, player) == Team(world, local)) {
            edi = friendly;
            esi = 0;
            ebx = friendly;
        } else {
            edi = 0;
            esi = own;
            ebx = 0;
        }
    };

    if (mode == 0) {
        const U bright = static_cast<U>(static_cast<std::int32_t>(level << 8) / 7);
        if (bright == 0) {
            return Pack(esi, edi, ebx);
        }
        bool none = true;
        const std::int32_t settlers = g_minimapShowSettlers;
        if (settlers != 0 && level >= 3 && y0 <= y1) {
            scan([&](const Tile& tile) {
                if (none && tile.settler != 0) {
                    if (settlers == 2 ||
                        g_settlerTypeClass[world->settlers[tile.settler].art] != 0) {
                        esi = edi = ebx = 0;
                        none = false;
                    }
                }
            });
            if (!none) {
                return Pack(esi, edi, ebx);
            }
        }
        if (level >= 3 && g_minimapShowBuildings != 0 && (t.flags & 8) != 0) {
            const U* c = PlayerColour(t.owner);
            esi = ((c[0] >> 1) * bright) >> 8;
            edi = ((c[1] >> 1) * bright) >> 8;
            ebx = ((c[2] >> 1) * bright) >> 8;
        } else if (t.owner != 0xff) {
            const U* c = PlayerColour(t.owner);
            esi = (c[0] * bright) >> 8;
            edi = (c[1] * bright) >> 8;
            ebx = (c[2] * bright) >> 8;
        } else {
            esi = edi = ebx = (bright * 0x6e) >> 8;
        }
        return Pack(esi, edi, ebx);
    }

    const U bright = static_cast<U>(static_cast<std::int32_t>(((t.shade + 100u) * level) << 8) / 0x9b5);
    if (bright == 0) {
        return Pack(esi, edi, ebx);
    }
    bool none = true;
    const std::int32_t settlers = g_minimapShowSettlers;
    if (settlers != 0 && level >= 3) {
        if (y0 <= y1) {
            scan([&](const Tile& tile) {
                if (!none || tile.settler == 0) {
                    return;
                }
                const Settler& s = world->settlers[tile.settler];
                const bool hidden = s.art == 0x17 && (s.flags & 0x10) == 0;
                if (settlers != 2) {
                    if (g_settlerTypeClass[s.art] == 0) {
                        return;
                    }
                    if (hidden && Team(world, s.owner) != Team(world, local)) {
                        return;
                    }
                }
                U owner = s.owner;
                if (hidden && Team(world, local) != Team(world, owner)) {
                    owner = tile.owner == 0xff ? local : tile.owner;
                }
                teamColour(owner, 0xff, 0xdc);
                none = false;
            });
            if (!none) {
                return Pack(esi, edi, ebx);
            }
        }
    }
    if (y0 <= y1) {
        scan([&](const Tile& tile) {
            const std::uint8_t flags = tile.draw_flags;
            if ((flags & 7) >= 3 && none && (flags & 0x40) != 0) {
                teamColour(tile.owner, 0xa0, 0x8c);
                none = false;
            }
        });
        if (!none) {
            return Pack(esi, edi, ebx);
        }
    }
    if (g_minimapShowBuildings != 0 && (t.flags & 8) != 0) {
        return Pack(0, 0, 0);
    }
    U r = 0x32;
    U g = 0xc8;
    U b = 0x32;
    const std::uint8_t terrain = t.terrain;
    if (terrain < 0x10) {
        if ((t.flags & 0x80) != 0) {
            r = g = b = 0x82;
        } else {
            r = 0;
            g = 0;
            b = 0xff;
        }
    } else if (mode != 1) {
        if (terrain >= 0x30 && terrain <= 0x4f) {
            r = 0xff;
            g = 0xff;
            b = 0;
        } else if (terrain >= 0x90 && terrain <= 0x9f) {
            r = 0xa0;
            g = 0x50;
            b = 0;
        } else if (terrain >= 0x20 && terrain <= 0x2f) {
            r = g = b = 0x8c;
        } else if (terrain >= 0x80 && terrain <= 0x8f) {
            r = g = b = 0xc8;
        } else if (terrain >= 0x50 && terrain <= 0x5f) {
            r = 0x28;
            g = 0x78;
            b = 0xfa;
        }
    }
    esi = (bright * r) >> 8;
    edi = (bright * g) >> 8;
    ebx = (bright * b) >> 8;
    return Pack(esi, edi, ebx);
}

// Purpose: Redraws one sixty-fourth of the minimap (spread evenly over it) from the current map state.
void __fastcall GameWorld_UpdateMinimapSlice(GameWorld* world, void*, U slice) {
    const auto pixel = reinterpret_cast<PixelFn>(kMinimapPixel);
    auto width = [] { return static_cast<U>(g_minimapWidth); };
    const U scale = static_cast<U>(static_cast<std::int32_t>(world->map_size << 8) / static_cast<std::int32_t>(width()));
    g_minimapSliceDirty[slice] = 1;
    const U order = ((slice >> 1) & 8) + 2 * ((slice & 2) + 2 * ((slice & 4) + 8 * (slice & 1))) +
                    ((slice >> 5) & 1) + ((slice >> 2) & 2);
    U w = width();
    for (U r = (order >> 3) & 7, ry = r * scale; r < w; r += 8, ry += scale * 8) {
        for (U c = order & 7, cx = c * scale; c < w; c += 8, cx += scale * 8) {
            const U value = pixel(world, nullptr, static_cast<std::int32_t>(cx >> 8), static_cast<std::int32_t>(ry >> 8),
                                  static_cast<std::int32_t>((cx + scale - 0x100) >> 8),
                                  static_cast<std::int32_t>((ry + scale - 0x100) >> 8));
            world->minimap[width() * r + c] = static_cast<std::uint16_t>(value);
            w = width();
        }
    }
}

S3RE_HOOK(0x004fb720, GameWorld_MinimapPixel, PixelFn, Original_GameWorld_MinimapPixel, GameWorld_MinimapPixel);
S3RE_HOOK(0x004fb520, GameWorld_UpdateMinimapSlice, SliceFn, Original_GameWorld_UpdateMinimapSlice,
          GameWorld_UpdateMinimapSlice);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kWorld = sizeof(GameWorld);

struct SavedGlobals {
    std::int32_t width, mode, buildings, settlers;
    U shifts[3], scales[3];
    std::int32_t dirty[64];
    SavedGlobals() {
        width = g_minimapWidth;
        mode = g_minimapMode;
        buildings = g_minimapShowBuildings;
        settlers = g_minimapShowSettlers;
        std::memcpy(shifts, g_pixelFormat->shift, sizeof(shifts));
        std::memcpy(scales, g_pixelFormat->scale, sizeof(scales));
        std::memcpy(dirty, &g_minimapSliceDirty, sizeof(dirty));
    }
    ~SavedGlobals() {
        g_minimapWidth = width;
        g_minimapMode = mode;
        g_minimapShowBuildings = buildings;
        g_minimapShowSettlers = settlers;
        std::memcpy(g_pixelFormat->shift, shifts, sizeof(shifts));
        std::memcpy(g_pixelFormat->scale, scales, sizeof(scales));
        std::memcpy(&g_minimapSliceDirty, dirty, sizeof(dirty));
    }
};

void RandomGlobals(test::Context& ctx) {
    auto& rng = ctx.rng();
    g_minimapMode = static_cast<std::int32_t>(rng.Below(3));
    g_minimapShowBuildings = static_cast<std::int32_t>(rng.Below(2));
    g_minimapShowSettlers = static_cast<std::int32_t>(rng.Below(3));
    static constexpr U kShifts[][3] = {{11, 5, 0}, {10, 5, 0}, {16, 8, 0}};
    static constexpr U kScales[][3] = {{31, 63, 31}, {31, 31, 31}, {255, 255, 255}};
    const U format = rng.Below(3);
    std::memcpy(g_pixelFormat->shift, kShifts[format], sizeof(kShifts[0]));
    std::memcpy(g_pixelFormat->scale, kScales[format], sizeof(kScales[0]));
}

// Tiles around the minimap's area with fog levels, borders, footprints,
// settlers (numbers inside the record array) and terrain classes.
void SetUpTiles(test::Context& ctx, test::WorldPair& worlds, U size) {
    auto& rng = ctx.rng();
    GameWorld* w = worlds.original();
    w->map_size = size;
    w->local_player = static_cast<std::int32_t>(rng.Below(20));
    for (U p = 0; p < 21; ++p) {
        w->players[p].team = static_cast<std::int32_t>(rng.Below(3));
    }
    const U span = size + 4 < 0x300 ? size + 4 : 0x300;
    for (U x = 0; x < span; ++x) {
        for (U y = 0; y < span; ++y) {
            Tile& t = w->tiles[x][y];
            t.draw_flags = static_cast<std::uint8_t>(rng.Next32());
            t.flags = static_cast<std::uint8_t>(rng.Next32());
            t.owner = static_cast<std::uint8_t>(rng.Chance(1, 3) ? 0xff : rng.Below(20));
            t.settler = static_cast<std::uint16_t>(rng.Chance(3, 4) ? 0 : 1 + rng.Below(31999));
            t.terrain = static_cast<std::uint8_t>(rng.Next32());
        }
    }
    for (Settler& s : w->settlers) {
        s.owner = static_cast<std::uint8_t>(rng.Below(20));
        s.art = static_cast<std::uint8_t>(rng.Chance(1, 3) ? 0x17 : rng.Below(48));
    }
    std::memcpy(worlds.oursBytes(), worlds.originalBytes(), kWorld);
}

void ComparePixels(test::Context& ctx, PixelFn candidate, int cases) {
    SavedGlobals saved;
    test::WorldPair worlds(ctx, kWorld);
    for (int i = 0; i < cases; ++i) {
        if (i % 2000 == 0) {
            SetUpTiles(ctx, worlds, 32 + ctx.rng().Below(353));
        }
        RandomGlobals(ctx);
        const U size = worlds.original()->map_size;
        const auto x0 = static_cast<std::int32_t>(ctx.rng().Below(size));
        const auto y0 = static_cast<std::int32_t>(ctx.rng().Below(size));
        const std::int32_t x1 = x0 + ctx.rng().Range(-1, 3);
        const std::int32_t y1 = y0 + ctx.rng().Range(-1, 3);
        const U expected = Original_GameWorld_MinimapPixel(worlds.original(), nullptr, x0, y0, x1, y1);
        const U actual = candidate(worlds.ours(), nullptr, x0, y0, x1, y1);
        if (!S3RE_EXPECT_EQ(expected, actual)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%d, %d)..(%d, %d) mode %d", i, x0, y0, x1, y1,
                     g_minimapMode.get());
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

void CompareSlices(test::Context& ctx, SliceFn candidate, int cases) {
    SavedGlobals saved;
    test::WorldPair worlds(ctx, kWorld);
    for (int i = 0; i < cases; ++i) {
        const U size = 64 + ctx.rng().Below(321);
        SetUpTiles(ctx, worlds, size);
        RandomGlobals(ctx);
        // The game's width (0x80) is below every map size; a wider minimap
        // would give pixels a rectangle with x1 < x0 - 1 (an unsigned wrap).
        const U widest = size < 208 ? size : 208;
        g_minimapWidth = static_cast<std::int32_t>(16 + ctx.rng().Below(widest - 15));
        const U slice = ctx.rng().Below(64);
        std::memset(&g_minimapSliceDirty, 0, 256);
        Original_GameWorld_UpdateMinimapSlice(worlds.original(), nullptr, slice);
        std::int32_t dirty[64];
        std::memcpy(dirty, &g_minimapSliceDirty, sizeof(dirty));
        std::memset(&g_minimapSliceDirty, 0, 256);
        candidate(worlds.ours(), nullptr, slice);
        if (!S3RE_EXPECT(std::memcmp(dirty, &g_minimapSliceDirty, sizeof(dirty)) == 0) ||
            !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: slice %u", i, slice);
            return;
        }
    }
}

// Wrong: border tiles of the team get the local player's colour.
U __fastcall WrongMinimapPixel(GameWorld* world, void*, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                               std::int32_t y1) {
    const U value = GameWorld_MinimapPixel(world, nullptr, x0, y0, x1, y1);
    return value == Pack(0, 0x8c, 0x8c) ? Pack(0xa0, 0xa0, 0xa0) : value;
}

// Wrong: the slice order skips the top bit.
void __fastcall WrongUpdateMinimapSlice(GameWorld* world, void*, U slice) {
    GameWorld_UpdateMinimapSlice(world, nullptr, slice & 31);
}

}  // namespace

S3RE_TEST(GameWorld_MinimapPixel_MatchesOriginal) {
    ComparePixels(ctx, &GameWorld_MinimapPixel, 40000);
}

S3RE_TEST(GameWorld_MinimapPixel_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePixels(probe, &WrongMinimapPixel, 40000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_UpdateMinimapSlice_MatchesOriginal) {
    CompareSlices(ctx, &GameWorld_UpdateMinimapSlice, 30);
}

S3RE_TEST(GameWorld_UpdateMinimapSlice_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSlices(probe, &WrongUpdateMinimapSlice, 30);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
