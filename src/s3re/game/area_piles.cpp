// Goods piles in the per-area, per-goods-type lists.
//
// A tile's uint16 +0x2 is a building number when flag 0x20 of byte +0xa is
// set, otherwise the number of the goods pile lying there (0 = none). Area
// records (0x2e0 bytes at world+0xe145e8; +0x0 player) keep a doubly linked
// list of piles per goods type: uint16 heads at +0xa + type*2. Piles
// (14-byte records at world+0x123885c; +0x0 goods type, +0x1 amount) link
// with +0xa (previous) and +0xc (next).
//
// GameWorld::AddTilePileToArea (0x005051b0): (x, y): if the tile holds a
//   pile (word +0x2 != 0 and no building flag): DebugLog("Pile %u Taken ,
//   eco %u", pile, area), adds the pile's amount to the goods counter of
//   the tile owner (GameWorld::AddPlayerCounter(owner, type, amount)), and
//   puts the pile at the head of its area's list for its goods type (area =
//   the tile's area word +0x14, used as is). EAX (the only caller
//   tail-returns it) reproduced: world + tile index * 24 when there is no
//   pile, else area * 0x170. ret 8.
// GameWorld::AddGoodsToPile (0x00505650): (pile): one more unit on the
//   pile: byte +0x3 (units on the way?) -= 1, amount (+0x1) += 1. Before
//   that, a pile of kind (+0x4) 0xff whose amount equals its reserved count
//   (+0x2) is put at the head of its area's list for its goods type (as in
//   AddTilePileToArea; area = the area word of the pile's tile +0x6/+0x8).
//   DebugLog("...", pile, kind, goods, x, y) (format at 0x6a6150) for kind 0
//   with byte +0x3 == 0, for kind 0xfe, and when the amount reaches 9 or
//   more. EAX (callers ignore it; one tail-returns it) reproduced from the
//   original's register use. ret 4.
// thiscall (ECX = world).

#include "game/debug_log.h"
#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>

namespace s3re::game {

namespace {

constexpr std::uint32_t kTiles = 0x67444;
constexpr std::uint32_t kTilePile = 0x2;
constexpr std::uint32_t kTileOwner = 0x8;
constexpr std::uint32_t kTileFlags = 0xa;
constexpr std::uint32_t kTileArea = 0x14;
constexpr std::uint8_t kTileHasBuilding = 0x20;

constexpr std::uint32_t kAreas = 0xe145e8;
constexpr std::uint32_t kAreaPileHeads = 0xa;  // uint16 per goods type

constexpr std::uint32_t kPiles = 0x123885c;
constexpr std::uint32_t kPileBytes = 14;
constexpr std::uint32_t kPileGoods = 0x0;
constexpr std::uint32_t kPileAmount = 0x1;
constexpr std::uint32_t kPileReserved = 0x2;
constexpr std::uint32_t kPileIncoming = 0x3;
constexpr std::uint32_t kPileKind = 0x4;
constexpr std::uint32_t kPileX = 0x6;
constexpr std::uint32_t kPileY = 0x8;
constexpr std::uint32_t kPilePrevious = 0xa;
constexpr std::uint32_t kPileNext = 0xc;

constexpr std::uintptr_t kPileTakenMessage = 0x006a6138;  // "Pile %u Taken , eco %u"
constexpr std::uintptr_t kPileMessage = 0x006a6150;

template <typename T>
T& At(GameWorld* world, std::uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<std::uint8_t*>(world) + offset);
}

std::uint32_t Pile(std::uint32_t pile) {
    return kPiles + pile * kPileBytes;
}

}  // namespace

using TileFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);

// Purpose: A goods pile lying on a tile becomes part of the economy: its goods count towards the tile
// owner's stock and the pile is listed in its area, so carriers can find it.
std::uint32_t __fastcall GameWorld_AddTilePileToArea(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t index = x * 0x300 + y;
    const std::uint32_t tile = kTiles + index * 24;
    const std::uint32_t pile = At<std::uint16_t>(world, tile + kTilePile);
    if (pile == 0 || (At<std::uint8_t>(world, tile + kTileFlags) & kTileHasBuilding) != 0) {
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world)) + index * 24;
    }
    const std::uint32_t area = At<std::uint16_t>(world, tile + kTileArea);
    GameDebugLog(kPileTakenMessage, pile, area);
    GameWorld_AddPlayerCounter(world, nullptr, At<std::uint8_t>(world, tile + kTileOwner),
                               At<std::uint8_t>(world, Pile(pile) + kPileGoods),
                               At<std::uint8_t>(world, Pile(pile) + kPileAmount));
    // The head's address as the original computes it: (area * 0x170 + type) * 2.
    const std::uint32_t areaWords = area * (0x2e0 / 2);
    std::uint16_t& head =
        At<std::uint16_t>(world, kAreas + kAreaPileHeads + (areaWords + At<std::uint8_t>(world, Pile(pile) + kPileGoods)) * 2);
    const std::uint32_t next = head;
    At<std::uint16_t>(world, Pile(pile) + kPileNext) = static_cast<std::uint16_t>(next);
    if (next != 0) {
        At<std::uint16_t>(world, Pile(next) + kPilePrevious) = static_cast<std::uint16_t>(pile);
    }
    At<std::uint16_t>(world, Pile(pile) + kPilePrevious) = 0;
    std::uint16_t& head2 =
        At<std::uint16_t>(world, kAreas + kAreaPileHeads + (areaWords + At<std::uint8_t>(world, Pile(pile) + kPileGoods)) * 2);
    head2 = static_cast<std::uint16_t>(pile);
    return areaWords;
}

S3RE_HOOK(0x005051b0, GameWorld_AddTilePileToArea, TileFn, Original_GameWorld_AddTilePileToArea,
          GameWorld_AddTilePileToArea);

using PileFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t pile);

// Purpose: One more unit of goods arrives on a pile. If the pile has become available for collection, it is
// listed in its area so carriers can find it.
std::uint32_t __fastcall GameWorld_AddGoodsToPile(GameWorld* world, void*, std::uint32_t pile) {
    const std::uint32_t record = Pile(pile);
    auto byte = [&](std::uint32_t field) -> std::uint8_t& { return At<std::uint8_t>(world, record + field); };
    auto log = [&](std::uint32_t kind) {
        GameDebugLog(kPileMessage, pile, kind, static_cast<std::uint32_t>(byte(kPileGoods)),
                     static_cast<std::uint32_t>(At<std::uint16_t>(world, record + kPileX)),
                     static_cast<std::uint32_t>(At<std::uint16_t>(world, record + kPileY)));
    };
    // The original's EAX, step by step (see the doc comment).
    std::uint32_t eax = ((pile * 7u) & 0xffffff00u) | byte(kPileKind);
    if (byte(kPileKind) == 0 && byte(kPileIncoming) == 0) {
        eax = pile + 0x14d2e2u;
        log(0);
    }
    if (byte(kPileKind) == 0xfe) {
        eax = byte(kPileGoods);
        log(0xfe);
    }
    if (byte(kPileKind) == 0xff) {
        eax = (eax & 0xffffff00u) | byte(kPileReserved);
        if (byte(kPileAmount) == byte(kPileReserved)) {
            const std::uint32_t x = At<std::uint16_t>(world, record + kPileX);
            const std::uint32_t y = At<std::uint16_t>(world, record + kPileY);
            const std::uint32_t area = At<std::uint16_t>(world, kTiles + (x * 0x300 + y) * 24 + kTileArea);
            const std::uint32_t areaWords = area * (0x2e0 / 2);
            std::uint16_t& head = At<std::uint16_t>(world, kAreas + kAreaPileHeads + (areaWords + byte(kPileGoods)) * 2);
            const std::uint32_t next = head;
            At<std::uint16_t>(world, record + kPileNext) = static_cast<std::uint16_t>(next);
            if (next != 0) {
                At<std::uint16_t>(world, Pile(next) + kPilePrevious) = static_cast<std::uint16_t>(pile);
            }
            At<std::uint16_t>(world, record + kPilePrevious) = 0;
            At<std::uint16_t>(world, kAreas + kAreaPileHeads + (areaWords + byte(kPileGoods)) * 2) =
                static_cast<std::uint16_t>(pile);
            eax = areaWords;
        }
    }
    byte(kPileIncoming) = static_cast<std::uint8_t>(byte(kPileIncoming) - 1);
    byte(kPileAmount) = static_cast<std::uint8_t>(byte(kPileAmount) + 1);
    eax = (eax & 0xffffff00u) | byte(kPileAmount);
    if (byte(kPileAmount) >= 9) {
        eax = byte(kPileKind);
        log(byte(kPileKind));
    }
    return eax;
}

S3RE_HOOK(0x00505650, GameWorld_AddGoodsToPile, PileFn, Original_GameWorld_AddGoodsToPile, GameWorld_AddGoodsToPile);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestPiles = 0x1000;
constexpr std::uint32_t kTestAreas = 0x400;
constexpr std::size_t kTestWorld = kPiles + (kTestPiles + 1) * kPileBytes;

std::uint32_t Relative(std::uint32_t value, GameWorld* world, std::uint32_t areaWords) {
    // Either a pointer into the world or area * 0x170.
    const auto base = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));
    return value == areaWords ? value : value - base;
}

}  // namespace

S3RE_TEST(GameWorld_AddTilePileToArea_MatchesOriginal) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < 20000; ++i) {
        const std::uint32_t x = ctx.rng().Below(384);
        const std::uint32_t y = ctx.rng().Below(384);
        const std::uint32_t tile = kTiles + (x * 0x300 + y) * 24;
        const std::uint32_t pile = ctx.rng().Chance(1, 8) ? 0 : 1 + ctx.rng().Below(kTestPiles);
        worlds.Set<std::uint16_t>(tile + kTilePile, static_cast<std::uint16_t>(pile));
        worlds.Set<std::uint8_t>(tile + kTileFlags,
                                 static_cast<std::uint8_t>(ctx.rng().Next32() & (ctx.rng().Chance(1, 6) ? 0xff : ~0x20)));
        const std::uint32_t area = ctx.rng().Below(kTestAreas);
        worlds.Set<std::uint16_t>(tile + kTileArea, static_cast<std::uint16_t>(area));
        worlds.Set<std::uint8_t>(tile + kTileOwner, static_cast<std::uint8_t>(ctx.rng().Chance(1, 8) ? 0xff : ctx.rng().Below(20)));
        if (pile != 0) {
            worlds.Set<std::uint8_t>(Pile(pile) + kPileGoods, static_cast<std::uint8_t>(ctx.rng().Below(36)));
            // The current head: often empty, else another pile.
            const std::uint32_t goods = At<std::uint8_t>(worlds.original(), Pile(pile) + kPileGoods);
            worlds.Set<std::uint16_t>(kAreas + kAreaPileHeads + (area * 0x170 + goods) * 2,
                                      static_cast<std::uint16_t>(ctx.rng().Chance(1, 3) ? 0 : 1 + ctx.rng().Below(kTestPiles)));
        }
        const std::uint32_t areaWords = area * 0x170;
        const std::uint32_t expected =
            Relative(Original_GameWorld_AddTilePileToArea(worlds.original(), nullptr, x, y), worlds.original(), areaWords);
        const std::uint32_t actual = Relative(GameWorld_AddTilePileToArea(worlds.ours(), nullptr, x, y), worlds.ours(), areaWords);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 256 == 255) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 255 before it): (%u, %u), pile %u", i, x, y, pile);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

S3RE_TEST(GameWorld_AddGoodsToPile_MatchesOriginal) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < 20000; ++i) {
        const std::uint32_t pile = 1 + ctx.rng().Below(kTestPiles);
        const std::uint32_t record = Pile(pile);
        static constexpr std::uint8_t kKinds[] = {0, 0xfe, 0xff, 0xff, 1, 7};
        worlds.Set<std::uint8_t>(record + kPileKind, kKinds[ctx.rng().Below(sizeof(kKinds))]);
        const std::uint8_t amount = static_cast<std::uint8_t>(ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(10));
        worlds.Set<std::uint8_t>(record + kPileAmount, amount);
        worlds.Set<std::uint8_t>(record + kPileReserved,
                                 static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? amount : ctx.rng().Below(10)));
        worlds.Set<std::uint8_t>(record + kPileIncoming, static_cast<std::uint8_t>(ctx.rng().Below(3)));
        worlds.Set<std::uint8_t>(record + kPileGoods, static_cast<std::uint8_t>(ctx.rng().Below(36)));
        const std::uint32_t x = ctx.rng().Below(384);
        const std::uint32_t y = ctx.rng().Below(384);
        worlds.Set<std::uint16_t>(record + kPileX, static_cast<std::uint16_t>(x));
        worlds.Set<std::uint16_t>(record + kPileY, static_cast<std::uint16_t>(y));
        const std::uint32_t area = ctx.rng().Below(kTestAreas);
        worlds.Set<std::uint16_t>(kTiles + (x * 0x300 + y) * 24 + kTileArea, static_cast<std::uint16_t>(area));
        const std::uint32_t goods = At<std::uint8_t>(worlds.original(), record + kPileGoods);
        worlds.Set<std::uint16_t>(kAreas + kAreaPileHeads + (area * 0x170 + goods) * 2,
                                  static_cast<std::uint16_t>(ctx.rng().Chance(1, 3) ? 0 : 1 + ctx.rng().Below(kTestPiles)));
        const std::uint32_t expected = Original_GameWorld_AddGoodsToPile(worlds.original(), nullptr, pile);
        const std::uint32_t actual = GameWorld_AddGoodsToPile(worlds.ours(), nullptr, pile);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 256 == 255) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 255 before it): pile %u", i, pile);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

}  // namespace s3re::game
