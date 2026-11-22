// Simulation helpers of batch 25 (Volker\*.cpp, unnamed files), all
// thiscall on the world unless noted.
//
// GameWorld_WearPath (0x0051b7f0): (x, y), ret 8. Called when a tile's
//   traffic counter (+0x12) reaches 12 (0x503940, from the walking code).
//   If the terrain byte (+6) of tile (x, y) is grass (0x10) or one of 0x12,
//   0x13, 0x18 (probably grass variants), walks its six neighbours in the
//   order (x+1,y), (x-1,y), (x,y+1), (x,y-1), (x+1,y+1), (x-1,y-1): a
//   neighbour with terrain 0x10 or 0x1c (worn path) passes; one with
//   0x12/0x13/0x18 is turned into plain grass 0x10 (redraw bit 0x80 of +9
//   set) and passes; any other stops the walk (changes already made stay).
//   If all six pass, the tile becomes a worn path (0x1c, redraw bit) and
//   its traffic +0x12 grows by 9. EAX = the address of the tile.
// GameWorld_QueueMarkedTileWithRoom (0x0051d000): (x, y), ret 8. A
//   flood-fill step like QueueMarkedTileNoOwner (world_tables.cpp): the tile
//   is queued on the tile ring (and its mark 0x10 of byte +0xa cleared) only
//   if it is marked, not blocked (bit 0 of +0xa), none of the 18 tiles
//   within two steps is blocked, and neither it nor any of them has terrain
//   of class 0x2_ (byte +6 & 0xf0 == 0x20; class not identified). EAX: the
//   new ring index (10000 when it wrapped), else the tile's address.
// GameWorld_SetSettlerAnimation (0x0050e2c0): (settler, kind), ret 8:
//   id = table 0x6a8060[kind * 4 + race] (race = settler byte +2); settler
//   dword +8 (Settler::direction) = id, word +0xc = GfxData_SpriteWord(id) - 1 on the graphics
//   object [0x7a8010] (probably the number of frames - 1). EAX = that
//   SpriteWord result - 1 (the whole register).
// CLogicThread_RemoveSettlerFromGroups (0x005428e0): (settler, group), ret 8;
//   ECX not used. Unless `group` is 0, removes `settler` from the ten
//   settler groups in S3.EXE's data (counts 0x7ad620, lists of 100 words at
//   0x7ad634) other than group number `group`. After a removal the entry
//   that moved into the slot is not checked (the index still advances), as
//   in the original. EAX not used by callers.

#include "game/raw.h"
#include "game/settler_groups.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/fake_graphics.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

using layout::Settler;
using layout::Tile;

constexpr std::uint32_t kTileRingEntries = 10000;
constexpr std::uint32_t kGroups = SettlerGroups::kGroups;
// int32 animation ids per kind and race, in S3.EXE's data.
const std::uint32_t* const kAnimationTable = reinterpret_cast<const std::uint32_t*>(0x006a8060);
// The graphics object pointer, an S3.EXE global.
constexpr std::uintptr_t kGfxData = 0x007a8010;

// Byte distance (x * 0x300 + y) * 24 of tile (x, y) from the first tile,
// 32-bit like the original. The functions return world + this (not the
// tile's own address).
std::uint32_t TileIndexBytes(std::uint32_t x, std::uint32_t y) {
    return (x * 0x300 + y) * kTileBytes;
}

// Tile index deltas (x * 0x300 + y) of the neighbours, in the original's
// order.
constexpr std::int32_t kRing6[6] = {0x300, -0x300, 1, -1, 0x301, -0x301};
constexpr std::int32_t kRadius2[18] = {0x300,  -0x300, 1,      -1,     0x301,  -0x301, 0x600,  0x601,  0x602,
                                       0x302,  2,      -0x2ff, -0x600, -0x601, -0x602, -0x302, -2,     0x2ff};

// The tile `delta` tile indices from the tile at byte offset `centre` (32-bit
// wrap-around, may lie outside the map like in the original).
Tile& TileNear(GameWorld* world, std::uint32_t centre, std::int32_t delta) {
    return TileAtOffset(world, centre + static_cast<std::uint32_t>(delta * static_cast<std::int32_t>(kTileBytes)));
}

}  // namespace

std::uint32_t __fastcall GfxData_SpriteWord(void* self, void* edx, std::uint32_t id);

using PositionFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);
using SettlerAnimationFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t settler,
                                                      std::uint32_t kind);
using RemoveFromGroupsFn = void(__fastcall*)(void* self, void* edx, std::uint32_t settler, std::uint32_t group);

std::uint32_t __fastcall GameWorld_WearPath(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall GameWorld_QueueMarkedTileWithRoom(GameWorld* world, void* edx, std::uint32_t x,
                                                                  std::uint32_t y);
std::uint32_t __fastcall GameWorld_SetSettlerAnimation(GameWorld* world, void* edx, std::uint32_t settler,
                                                       std::uint32_t kind);
void __fastcall CLogicThread_RemoveSettlerFromGroups(void* self, void* edx, std::uint32_t settler,
                                                     std::uint32_t group);

S3RE_HOOK(0x0051b7f0, GameWorld_WearPath, PositionFn, Original_GameWorld_WearPath,
          GameWorld_WearPath);

// Purpose: Turns a much-walked grass tile into a worn path (terrain 0x1c,
// which decays back to grass in UpdateEnvironment) if its neighbours are
// grass or path, smoothing grass variants around it.
std::uint32_t __fastcall GameWorld_WearPath(GameWorld* world, void* /*edx*/, std::uint32_t x,
                                                     std::uint32_t y) {
    const std::uint32_t centre = TileOffset(x, y);
    Tile& tile = TileAtOffset(world, centre);
    const auto isSmall = [](std::uint8_t terrain) { return terrain == 0x12 || terrain == 0x13 || terrain == 0x18; };
    const std::uint8_t terrain = tile.terrain;
    const std::uint32_t result = reinterpret_cast<std::uint32_t>(world) + TileIndexBytes(x, y);
    if (terrain != 0x10 && !isSmall(terrain)) {
        return result;
    }
    for (const std::int32_t delta : kRing6) {
        Tile& neighbour = TileNear(world, centre, delta);
        const std::uint8_t value = neighbour.terrain;
        if (isSmall(value)) {
            neighbour.terrain = 0x10;
            neighbour.draw_flags |= layout::kTileDrawRedraw;
        } else if (value != 0x10 && value != 0x1c) {
            return result;
        }
    }
    tile.terrain = 0x1c;
    tile.draw_flags |= layout::kTileDrawRedraw;
    tile.traffic = static_cast<std::uint8_t>(tile.traffic + 9);
    return result;
}

S3RE_HOOK(0x0051d000, GameWorld_QueueMarkedTileWithRoom, PositionFn,
          Original_GameWorld_QueueMarkedTileWithRoom, GameWorld_QueueMarkedTileWithRoom);

// Purpose: One step of a flood fill over marked tiles that only spreads to
// places with room: no blocked tile and no terrain of class 0x2_ within two
// steps (probably to find space for something large).
std::uint32_t __fastcall GameWorld_QueueMarkedTileWithRoom(GameWorld* world, void* /*edx*/, std::uint32_t x,
                                                                  std::uint32_t y) {
    const std::uint32_t centre = TileOffset(x, y);
    Tile& tile = TileAtOffset(world, centre);
    const std::uint32_t result = reinterpret_cast<std::uint32_t>(world) + TileIndexBytes(x, y);
    const std::uint8_t flags = tile.flags;
    if ((flags & layout::kTileFloodMark) == 0 || (flags & layout::kTileBlocked) != 0) {
        return result;
    }
    for (const std::int32_t delta : kRadius2) {
        if ((TileNear(world, centre, delta).flags & layout::kTileBlocked) != 0) {
            return result;
        }
    }
    if ((tile.terrain & 0xf0) == 0x20) {
        return result;
    }
    for (const std::int32_t delta : kRadius2) {
        if ((TileNear(world, centre, delta).terrain & 0xf0) == 0x20) {
            return result;
        }
    }
    std::uint32_t& index = world->tile_ring_index.write;
    world->tile_ring[index].x = static_cast<std::uint16_t>(x);
    world->tile_ring[index].y = static_cast<std::uint16_t>(y);
    tile.flags &= 0xef;
    const std::uint32_t next = index + 1;
    index = next == kTileRingEntries ? 0 : next;
    return next;
}

S3RE_HOOK(0x0050e2c0, GameWorld_SetSettlerAnimation, SettlerAnimationFn, Original_GameWorld_SetSettlerAnimation,
          GameWorld_SetSettlerAnimation);

// Purpose: Gives a settler the animation of a kind of activity for its race
// (graphics id and frame count, probably walking, carrying, working...).
std::uint32_t __fastcall GameWorld_SetSettlerAnimation(GameWorld* world, void* /*edx*/, std::uint32_t settler,
                                                       std::uint32_t kind) {
    Settler& record = world->settlers[settler];
    const std::uint32_t entry = record.race + kind * 4;
    // The animation id goes into the dword +0x8 (Settler::direction).
    record.animation = kAnimationTable[entry];
    const std::uint32_t frames = GfxData_SpriteWord(RawAtAddress<void*>(kGfxData), nullptr, kAnimationTable[entry]) - 1;
    record.counter = static_cast<std::uint16_t>(frames);
    return frames;
}

S3RE_HOOK(0x005428e0, CLogicThread_RemoveSettlerFromGroups, RemoveFromGroupsFn,
          Original_CLogicThread_RemoveSettlerFromGroups, CLogicThread_RemoveSettlerFromGroups);

// Purpose: Takes a settler out of the player's numbered settler groups
// (the selection groups), except the given one (nothing at all for group
// 0).
void __fastcall CLogicThread_RemoveSettlerFromGroups(void* /*self*/, void* /*edx*/, std::uint32_t settler,
                                                     std::uint32_t group) {
    if (group == 0) {
        return;
    }
    SettlerGroups& groups = GlobalSettlerGroups();
    for (std::uint32_t g = 0; g < kGroups; ++g) {
        if (g == group) {
            continue;
        }
        std::uint16_t& count = groups.counts[g];
        std::uint16_t* list = groups.lists[g];
        for (std::uint32_t k = 0; k < count; ++k) {
            if (list[k] != settler) {
                continue;
            }
            for (std::uint32_t m = k; m + 1 < count; ++m) {
                list[m] = list[m + 1];
            }
            --count;
        }
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kTestWorld = sizeof(GameWorld);

// Random terrain bytes / flags around (x, y) from small sets, so all paths
// are reached.
void SetUpTiles(test::Context& ctx, test::WorldPair& worlds, std::uint32_t x, std::uint32_t y, bool objects) {
    static const std::uint8_t kObjects[] = {0x10, 0x12, 0x13, 0x18, 0x1c, 0x10, 0x12, 0x00, 0x21, 0x2f, 0x31};
    const std::uint32_t centre = TileOffset(x, y);
    for (int k = -1; k < 18; ++k) {
        const std::int32_t delta = k < 0 ? 0 : kRadius2[k];
        const bool rare = ctx.rng().Chance(1, 25);
        if (objects) {
            const std::uint8_t terrain =
                ctx.rng().Chance(1, 3) && k >= 0 ? (k < 6 ? 0x1c : 0) : kObjects[ctx.rng().Below(rare ? 11 : 7)];
            worlds.Both([&](GameWorld* w) { TileNear(w, centre, delta).terrain = terrain; });
        } else {
            const auto terrain = static_cast<std::uint8_t>(rare ? 0x20 + ctx.rng().Below(16) : ctx.rng().Below(0x20));
            const auto flags = static_cast<std::uint8_t>(ctx.rng().Next32());
            const auto tileFlags = static_cast<std::uint8_t>(
                k < 0 ? (ctx.rng().Chance(1, 4) ? flags : (flags | 0x10) & ~1) : (rare ? flags | 1 : flags & ~1));
            worlds.Both([&](GameWorld* w) {
                TileNear(w, centre, delta).terrain = terrain;
                TileNear(w, centre, delta).flags = tileFlags;
            });
        }
    }
}

template <typename Call>
void Compare(test::Context& ctx, int cases, Call call) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        std::uint32_t expected = 0, actual = 0;
        call(worlds, expected, actual);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 16 == 15 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 15 before it)", i);
            return;
        }
    }
}

template <typename Fn>
void ComparePosition(test::Context& ctx, Fn original, Fn candidate, bool objects, int cases) {
    Compare(ctx, cases, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        const std::uint32_t x = 2 + ctx.rng().Below(380);
        const std::uint32_t y = 2 + ctx.rng().Below(380);
        SetUpTiles(ctx, worlds, x, y, objects);
        const std::uint32_t index = ctx.rng().Chance(1, 8) ? kTileRingEntries - 1 : ctx.rng().Below(kTileRingEntries);
        worlds.Both([&](GameWorld* w) { w->tile_ring_index.write = index; });
        expected = original(worlds.original(), nullptr, x, y) - (objects ? reinterpret_cast<std::uint32_t>(worlds.original()) : 0);
        actual = candidate(worlds.ours(), nullptr, x, y) - (objects ? reinterpret_cast<std::uint32_t>(worlds.ours()) : 0);
        if (!objects && expected > kTileRingEntries) {  // a tile address: relative to the world
            expected -= reinterpret_cast<std::uint32_t>(worlds.original());
            actual -= reinterpret_cast<std::uint32_t>(worlds.ours());
        }
    });
}

// Wrong: the conversion forgets the redraw bit.
std::uint32_t __fastcall WrongWearPath(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y) {
    Tile& tile = TileAtOffset(world, TileOffset(x, y));
    const std::uint8_t saved = tile.draw_flags;
    const std::uint32_t result = GameWorld_WearPath(world, edx, x, y);
    if (tile.terrain == 0x1c) {
        tile.draw_flags = saved;
    }
    return result;
}

void CompareSetSettlerAnimation(test::Context& ctx, SettlerAnimationFn candidate, int cases) {
    test::FakeGraphics gfx;
    Compare(ctx, cases, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        const std::uint32_t settler = ctx.rng().Below(0x8000);
        const std::uint32_t race = ctx.rng().Below(4);
        const std::uint32_t kind = ctx.rng().Below(40);
        worlds.Both([&](GameWorld* w) { w->settlers[settler].race = static_cast<std::uint8_t>(race); });
        if (!gfx.PrepareSprite(ctx, kAnimationTable[kind * 4 + race])) {
            expected = actual = 0;
            return;  // not a graphics id (the table holds other data there)
        }
        expected = Original_GameWorld_SetSettlerAnimation(worlds.original(), nullptr, settler, kind);
        actual = candidate(worlds.ours(), nullptr, settler, kind);
    });
}

// Wrong: stores the frame count without the -1.
std::uint32_t __fastcall WrongSetSettlerAnimation(GameWorld* world, void* edx, std::uint32_t settler,
                                                  std::uint32_t kind) {
    const std::uint32_t result = GameWorld_SetSettlerAnimation(world, edx, settler, kind);
    ++world->settlers[settler].counter;
    return result;
}

void CompareRemoveFromGroups(test::Context& ctx, RemoveFromGroupsFn candidate, int cases) {
    constexpr std::size_t kBytes = sizeof(SettlerGroups);
    std::vector<std::uint8_t> saved(kBytes), before(kBytes), afterOriginal(kBytes);
    SettlerGroups& groups = GlobalSettlerGroups();
    auto* globals = reinterpret_cast<std::uint8_t*>(&groups);
    std::memcpy(saved.data(), globals, kBytes);
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(globals, kBytes);
        const std::uint32_t settler = ctx.rng().Below(6);
        for (std::uint32_t g = 0; g < kGroups; ++g) {
            const auto count = static_cast<std::uint16_t>(ctx.rng().Chance(1, 4) ? 0 : ctx.rng().Below(101));
            groups.counts[g] = count;
            for (std::uint32_t k = 0; k < 100; ++k) {
                groups.lists[g][k] = static_cast<std::uint16_t>(ctx.rng().Below(6));
            }
        }
        const std::uint32_t argument = ctx.rng().Chance(1, 8) ? (ctx.rng().Next32() & 0xffff0000u) | settler : settler;
        const std::uint32_t group = ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(11);
        std::memcpy(before.data(), globals, kBytes);
        Original_CLogicThread_RemoveSettlerFromGroups(nullptr, nullptr, argument, group);
        std::memcpy(afterOriginal.data(), globals, kBytes);
        std::memcpy(globals, before.data(), kBytes);
        candidate(nullptr, nullptr, argument, group);
        if (!S3RE_EXPECT_MEM_EQ(afterOriginal.data(), globals, kBytes)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: settler 0x%x group 0x%x", i, argument, group);
            break;
        }
    }
    std::memcpy(globals, saved.data(), kBytes);
}

// Wrong: checks the entry that moved into the slot after a removal.
void __fastcall WrongRemoveFromGroups(void* self, void* edx, std::uint32_t settler, std::uint32_t group) {
    for (int pass = 0; pass < 4; ++pass) {
        CLogicThread_RemoveSettlerFromGroups(self, edx, settler, group);
    }
}

}  // namespace

S3RE_TEST(GameWorld_WearPath_MatchesOriginal) {
    ComparePosition<PositionFn>(ctx, Original_GameWorld_WearPath, &GameWorld_WearPath, true, 5000);
}

S3RE_TEST(GameWorld_WearPath_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePosition<PositionFn>(probe, Original_GameWorld_WearPath, &WrongWearPath, true, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_QueueMarkedTileWithRoom_MatchesOriginal) {
    ComparePosition<PositionFn>(ctx, Original_GameWorld_QueueMarkedTileWithRoom,
                                &GameWorld_QueueMarkedTileWithRoom, false, 5000);
}

S3RE_TEST(GameWorld_QueueMarkedTileWithRoom_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    // Wrong pairing: the "no owner" queue step has no neighbourhood checks.
    ComparePosition<PositionFn>(probe, Original_GameWorld_QueueMarkedTileWithRoom,
                                reinterpret_cast<PositionFn>(0x0051d3d0), false, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_SetSettlerAnimation_MatchesOriginal) {
    CompareSetSettlerAnimation(ctx, &GameWorld_SetSettlerAnimation, 5000);
}

S3RE_TEST(GameWorld_SetSettlerAnimation_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSetSettlerAnimation(probe, &WrongSetSettlerAnimation, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CLogicThread_RemoveSettlerFromGroups_MatchesOriginal) {
    CompareRemoveFromGroups(ctx, &CLogicThread_RemoveSettlerFromGroups, 5000);
}

S3RE_TEST(CLogicThread_RemoveSettlerFromGroups_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareRemoveFromGroups(probe, &WrongRemoveFromGroups, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
