// Bulk writes of single tile bytes along lines of the map.
//
// Tile offsets as in world.h (records at 0x67440, tile (x, y) at
// (x * 0x300 + y) * 24); the bytes are tile fields 0xb and 0x11, whose
// meaning is unknown (0xb is one of the most used tile fields).
//
// GameWorld::SetDiagonalTileByteB (0x00518d10) / ClearDiagonalTileByteB
//   (0x00518dc0): (flags) write 0xff / 0 into byte 0xb of the tiles
//   - if flags & 1: on the diagonal (i, i), i < size;
//   - if flags & 2: on the anti-diagonal (i, size-1-i), i < size, and on
//     both lines next to it, (i, size-2-i) and (i+1, size-1-i), i < size-1.
//   Only GameWorld::Create calls them. The addresses are computed with the
//   original's 32-bit arithmetic ((size + i * 0x2ff) * 24 + displacement)
//   and the map size is re-read for every access, as in the original.
//   ret 4; EAX not reproduced (the only caller overwrites it).
// GameWorld::ClearTileByte11Slice (0x00518ec0): (first) clears byte 0x11 of
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

constexpr std::uint32_t kTileByteB = 0x6744b;            // byte 0xb of tile (0, 0)
constexpr std::uint32_t kTileByteBUp = 0x67433;          // the same, one tile lower in y
constexpr std::uint32_t kTileByteBUp2 = 0x6741b;         // two tiles lower in y
constexpr std::uint32_t kTileByteBRightUp = 0x6bc33;     // x + 1, y - 1
constexpr std::uint32_t kTileByte11 = 0x67451;           // byte 0x11 of tile (0, 0)
constexpr std::uint32_t kDiagonalStep = 0x4818;          // (x + 1, y + 1)
constexpr std::uint32_t kAntiDiagonalStep = 0x2ff;       // (x + 1, y - 1), in tiles
constexpr std::uint32_t kSliceRows = 8;

std::uint32_t MapSize(GameWorld* world) {
    return WorldField<std::uint32_t>(world, world_offset::kMapSize);
}

void SetByte(GameWorld* world, std::uint32_t offset, std::uint8_t value) {
    reinterpret_cast<std::uint8_t*>(world)[offset] = value;
}

// Offset of the anti-diagonal tile i plus `displacement` (32-bit wrap).
std::uint32_t AntiDiagonal(GameWorld* world, std::uint32_t i, std::uint32_t displacement) {
    return (MapSize(world) + i * kAntiDiagonalStep) * world_offset::kTileBytes + displacement;
}

void WriteDiagonals(GameWorld* world, std::uint32_t flags, std::uint8_t value) {
    if ((flags & 1) != 0) {
        for (std::uint32_t i = 0; i < MapSize(world); ++i) {
            SetByte(world, kTileByteB + i * kDiagonalStep, value);
        }
    }
    if ((flags & 2) != 0) {
        for (std::uint32_t i = 0; i < MapSize(world); ++i) {
            SetByte(world, AntiDiagonal(world, i, kTileByteBUp), value);
        }
        for (std::uint32_t i = 0; i < MapSize(world) - 1; ++i) {
            SetByte(world, AntiDiagonal(world, i, kTileByteBUp2), value);
            SetByte(world, AntiDiagonal(world, i, kTileByteBRightUp), value);
        }
    }
}

}  // namespace

using FlagsFn = void(__fastcall*)(GameWorld* world, void* edx, std::uint32_t flags);
using SliceFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t first);

// The original reads only the low byte of the argument.
void __fastcall GameWorld_SetDiagonalTileByteB(GameWorld* world, void*, std::uint32_t flags) {
    WriteDiagonals(world, flags & 0xff, 0xff);
}

void __fastcall GameWorld_ClearDiagonalTileByteB(GameWorld* world, void*, std::uint32_t flags) {
    WriteDiagonals(world, flags & 0xff, 0);
}

std::uint32_t __fastcall GameWorld_ClearTileByte11Slice(GameWorld* world, void*,
                                                        std::uint32_t first) {
    if (first >= MapSize(world)) {
        return first;
    }
    std::uint32_t x = 0;
    std::uint32_t row = first * world_offset::kTileBytes + kTileByte11;
    for (std::uint32_t y = first; y < MapSize(world); y += kSliceRows) {
        std::uint32_t tile = row;
        for (x = 0; x < MapSize(world); ++x) {
            SetByte(world, tile, 0);
            tile += world_offset::kMapRowBytes;
        }
        row += kSliceRows * world_offset::kTileBytes;
    }
    return x;
}

S3RE_HOOK(0x00518d10, GameWorld_SetDiagonalTileByteB, FlagsFn,
          Original_GameWorld_SetDiagonalTileByteB, GameWorld_SetDiagonalTileByteB);
S3RE_HOOK(0x00518dc0, GameWorld_ClearDiagonalTileByteB, FlagsFn,
          Original_GameWorld_ClearDiagonalTileByteB, GameWorld_ClearDiagonalTileByteB);
S3RE_HOOK(0x00518ec0, GameWorld_ClearTileByte11Slice, SliceFn,
          Original_GameWorld_ClearTileByte11Slice, GameWorld_ClearTileByte11Slice);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestMapSize = 384;
constexpr std::size_t kMapWorld =
    world_offset::kMapTiles + (kTestMapSize + 1) * world_offset::kMapRowBytes;

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
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
        const std::uint32_t flags = ctx.rng().Next32();
        original(worlds.original(), nullptr, flags);
        candidate(worlds.ours(), nullptr, flags);
        if (!worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: size %u, flags %#x", i, size, flags);
            return;
        }
        // Undo the marks now and then so both values keep being visible.
        if (i % 8 == 7) {
            ctx.rng().Fill(worlds.originalBytes() + world_offset::kMapTiles,
                           kMapWorld - world_offset::kMapTiles);
            std::memcpy(worlds.oursBytes(), worlds.originalBytes(), kMapWorld);
        }
    }
}

void CompareSlice(test::Context& ctx, SliceFn original, SliceFn candidate, int cases) {
    test::WorldPair worlds(ctx, kMapWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = TestMapSize(ctx);
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
        const std::uint32_t first =
            ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(size + 9);
        const std::uint32_t expected = original(worlds.original(), nullptr, first);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, first);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: size %u, first %u", i, size, first);
            return;
        }
        if (i % 8 == 7) {
            ctx.rng().Fill(worlds.originalBytes() + world_offset::kMapTiles,
                           kMapWorld - world_offset::kMapTiles);
            std::memcpy(worlds.oursBytes(), worlds.originalBytes(), kMapWorld);
        }
    }
}

// Wrong: the lines next to the anti-diagonal stop one tile early.
void __fastcall WrongSetDiagonalTileByteB(GameWorld* world, void*, std::uint32_t flags) {
    if ((flags & 2) != 0 && MapSize(world) > 1) {
        const std::uint32_t last = MapSize(world) - 2;
        const std::uint8_t keepA = reinterpret_cast<std::uint8_t*>(world)[AntiDiagonal(world, last, kTileByteBUp2)];
        GameWorld_SetDiagonalTileByteB(world, nullptr, flags);
        SetByte(world, AntiDiagonal(world, last, kTileByteBUp2), keepA);
        return;
    }
    GameWorld_SetDiagonalTileByteB(world, nullptr, flags);
}

}  // namespace

S3RE_TEST(GameWorld_SetDiagonalTileByteB_MatchesOriginal) {
    CompareDiagonals(ctx, Original_GameWorld_SetDiagonalTileByteB, &GameWorld_SetDiagonalTileByteB,
                     200);
}
S3RE_TEST(GameWorld_SetDiagonalTileByteB_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDiagonals(probe, Original_GameWorld_SetDiagonalTileByteB, &WrongSetDiagonalTileByteB,
                     100);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_ClearDiagonalTileByteB_MatchesOriginal) {
    CompareDiagonals(ctx, Original_GameWorld_ClearDiagonalTileByteB,
                     &GameWorld_ClearDiagonalTileByteB, 200);
}
S3RE_TEST(GameWorld_ClearTileByte11Slice_MatchesOriginal) {
    CompareSlice(ctx, Original_GameWorld_ClearTileByte11Slice, &GameWorld_ClearTileByte11Slice,
                 200);
}

}  // namespace s3re::game
