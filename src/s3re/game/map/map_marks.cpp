// Bulk writes of single tile bytes along lines of the map.
//
// Tile offsets as in the old world.h (records at 0x67440, tile (x, y) at
// (x * 0x300 + y) * 24); the bytes are tile fields 0xb and 0x11, i.e.
// Tile::object (+0x7) and Tile::sight (+0xd) at the real tile base
// 0x67444 (layout.h).
//
// CGameData::SetDiagonalTileByteB (0x00518d10) / ClearDiagonalTileByteB
//   (0x00518dc0): (flags) write 0xff / 0 into byte 0xb of the tiles
//   - if flags & 1: on the diagonal (i, i), i < size;
//   - if flags & 2: on the anti-diagonal (i, size-1-i), i < size, and on
//     both lines next to it, (i, size-2-i) and (i+1, size-1-i), i < size-1.
//   Only CGameData::GenerateRandomMap calls them. The addresses are computed with the
//   original's 32-bit arithmetic ((size + i * 0x2ff) * 24 + displacement)
//   and the map size is re-read for every access, as in the original.
//   ret 4; EAX not reproduced (the only caller overwrites it).
// CGameData::ClearSightSlice (0x00518ec0): (first) clears byte 0x11 of
//   every tile in rows y = first, first+8, ... (< size). UpdateEnvironment
//   clears one such slice per call (counter at world+0x110a580, 0..7);
//   0x518e70 clears all eight. EAX = first if first >= size, otherwise the
//   map size (the last inner loop count), reproduced exactly. ret 4.
// All thiscall (ECX = world).

#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

using layout::Tile;

constexpr std::uint32_t kSliceRows = 8;

std::uint32_t MapSize(CGameData* world) {
    return world->map_size;
}

// Tile (x + dx, size - 1 - x - dy) next to the anti-diagonal, with the map
// size read once per access like the original. The original addresses it
// as (size + x * 0x2ff) * 24 + displacement, with displacement 0x67433
// (dx 0, dy 0), 0x6741b (dx 0, dy 1) or 0x6bc33 (dx 1, dy 0) for
// Tile::object; TileOffset gives the same 32-bit offsets.
Tile& AntiDiagonal(CGameData* world, std::uint32_t x, std::uint32_t dx, std::uint32_t dy) {
    return TileAtOffset(world, TileOffset(x + dx, MapSize(world) - 1 - x - dy));
}

void WriteDiagonals(CGameData* world, std::uint32_t flags, std::uint8_t value) {
    if ((flags & 1) != 0) {
        for (std::uint32_t i = 0; i < MapSize(world); ++i) {
            // Original: 0x6744b + i * 0x4818.
            TileAtOffset(world, TileOffset(i, i)).object = value;
        }
    }
    if ((flags & 2) != 0) {
        for (std::uint32_t i = 0; i < MapSize(world); ++i) {
            AntiDiagonal(world, i, 0, 0).object = value;
        }
        for (std::uint32_t i = 0; i < MapSize(world) - 1; ++i) {
            AntiDiagonal(world, i, 0, 1).object = value;
            AntiDiagonal(world, i, 1, 0).object = value;
        }
    }
}

}  // namespace

using FlagsFn = void(__fastcall*)(CGameData* world, void* edx, std::uint32_t flags);
using SliceFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t first);

// The original reads only the low byte of the argument.
// Purpose: Map setup: marks the tiles along the map's diagonals (the map is drawn as a diamond, so these
// are probably its visible edges).
void __fastcall CGameData_SetDiagonalTileByteB(CGameData* world, void*, std::uint32_t flags) {
    WriteDiagonals(world, flags & 0xff, 0xff);
}

// Purpose: Map setup: clears the diagonal marks set by SetDiagonalTileByteB.
void __fastcall CGameData_ClearDiagonalTileByteB(CGameData* world, void*, std::uint32_t flags) {
    WriteDiagonals(world, flags & 0xff, 0);
}

// Purpose: Clears one field (probably a resource or effect value) in every eighth row of the map. The
// landscape update does one slice per step so the work is spread over time.
std::uint32_t __fastcall CGameData_ClearSightSlice(CGameData* world, void*,
                                                        std::uint32_t first) {
    if (first >= MapSize(world)) {
        return first;
    }
    std::uint32_t x = 0;
    std::uint32_t row = TileOffset(0, first);  // 32-bit tile offsets, stepped like the original
    for (std::uint32_t y = first; y < MapSize(world); y += kSliceRows) {
        std::uint32_t tile = row;
        for (x = 0; x < MapSize(world); ++x) {
            TileAtOffset(world, tile).sight = 0;
            tile += kTileRowBytes;
        }
        row += kSliceRows * kTileBytes;
    }
    return x;
}

S3RE_HOOK(0x00518d10, CGameData_SetDiagonalTileByteB, FlagsFn,
          Original_CGameData_SetDiagonalTileByteB, CGameData_SetDiagonalTileByteB);
S3RE_HOOK(0x00518dc0, CGameData_ClearDiagonalTileByteB, FlagsFn,
          Original_CGameData_ClearDiagonalTileByteB, CGameData_ClearDiagonalTileByteB);
S3RE_HOOK(0x00518ec0, CGameData_ClearSightSlice, SliceFn,
          Original_CGameData_ClearSightSlice, CGameData_ClearSightSlice);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestMapSize = 384;
constexpr std::size_t kMapTilesOffset = offsetof(CGameData, tiles);
constexpr std::size_t kMapWorld = kMapTilesOffset + (kTestMapSize + 1) * kTileRowBytes;

std::uint32_t TestMapSize(test::Context& ctx) {
    // Never 0: the anti-diagonal loop would then run 2^32 times (in both).
    return ctx.rng().Chance(1, 4) ? 1 + ctx.rng().Below(3) : 1 + ctx.rng().Below(kTestMapSize);
}

// The whole world is compared after every case (the writes spread over the
// map).
void CompareDiagonals(test::Context& ctx, FlagsFn original, FlagsFn candidate, int cases) {
    test::WorldPair worlds(ctx, kMapWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = TestMapSize(ctx);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        const std::uint32_t flags = ctx.rng().Next32();
        original(worlds.original(), nullptr, flags);
        candidate(worlds.ours(), nullptr, flags);
        if (!worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: size %u, flags %#x", i, size, flags);
            return;
        }
        // Undo the marks now and then so both values keep being visible.
        if (i % 8 == 7) {
            ctx.rng().Fill(worlds.originalBytes() + kMapTilesOffset, kMapWorld - kMapTilesOffset);
            std::memcpy(worlds.oursBytes(), worlds.originalBytes(), kMapWorld);
        }
    }
}

void CompareSlice(test::Context& ctx, SliceFn original, SliceFn candidate, int cases) {
    test::WorldPair worlds(ctx, kMapWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = TestMapSize(ctx);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        const std::uint32_t first =
            ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(size + 9);
        const std::uint32_t expected = original(worlds.original(), nullptr, first);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, first);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: size %u, first %u", i, size, first);
            return;
        }
        if (i % 8 == 7) {
            ctx.rng().Fill(worlds.originalBytes() + kMapTilesOffset, kMapWorld - kMapTilesOffset);
            std::memcpy(worlds.oursBytes(), worlds.originalBytes(), kMapWorld);
        }
    }
}

// Wrong: the lines next to the anti-diagonal stop one tile early.
void __fastcall WrongSetDiagonalTileByteB(CGameData* world, void*, std::uint32_t flags) {
    if ((flags & 2) != 0 && MapSize(world) > 1) {
        const std::uint32_t last = MapSize(world) - 2;
        const std::uint8_t keepA = AntiDiagonal(world, last, 0, 1).object;
        CGameData_SetDiagonalTileByteB(world, nullptr, flags);
        AntiDiagonal(world, last, 0, 1).object = keepA;
        return;
    }
    CGameData_SetDiagonalTileByteB(world, nullptr, flags);
}

}  // namespace

S3RE_TEST(CGameData_SetDiagonalTileByteB_MatchesOriginal) {
    CompareDiagonals(ctx, Original_CGameData_SetDiagonalTileByteB, &CGameData_SetDiagonalTileByteB,
                     200);
}
S3RE_TEST(CGameData_SetDiagonalTileByteB_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDiagonals(probe, Original_CGameData_SetDiagonalTileByteB, &WrongSetDiagonalTileByteB,
                     100);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_ClearDiagonalTileByteB_MatchesOriginal) {
    CompareDiagonals(ctx, Original_CGameData_ClearDiagonalTileByteB,
                     &CGameData_ClearDiagonalTileByteB, 200);
}
S3RE_TEST(CGameData_ClearSightSlice_MatchesOriginal) {
    CompareSlice(ctx, Original_CGameData_ClearSightSlice, &CGameData_ClearSightSlice,
                 200);
}

}  // namespace s3re::game
