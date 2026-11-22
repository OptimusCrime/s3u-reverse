// Goods piles changed by settler jobs (UpdateSettlers callees, package B).
//
// GameWorld::SetPileGood (0x005061b0): (pile, good): the pile (an empty
//   pile record, kept in its area's list of empty piles +0x8) leaves that
//   list, gets the goods type `good` (byte +0x0) and goes to the head of
//   its area's list for that goods type (area = the area word of the
//   pile's tile). The unlink does not look at the old goods type: a pile
//   without a previous link is assumed to be the empty list's head. EAX =
//   area * 0x170 + the new goods byte (the original's index arithmetic;
//   one caller tail-returns it). ret 8. Callers: MakePileEmpty's
//   counterpart in 0x4dfb40, 0x5052c0, TakeGoodFromPile 0x505820,
//   ResetSettlerToIdle 0x50cf50.
// GameWorld::DropGoodNear (0x005046b0): (good, amount, x, y): puts
//   `amount` units of a goods type on the ground near (x, y). Walks the
//   offset table (entries 0..9999, nearest first) while units are left:
//   (1) a tile on the map holding a pile (no building flag) of the same
//   goods type, kind 0xfe (dropped goods) and fewer than 8 units takes as
//   many as fit up to 8; a pile whose units were all leaving (amount ==
//   leaving, so it was out of its area's goods list) goes back to the head
//   of that list first. (2) From entry 37 on (the tiles of radius 3 and
//   more), if units are left, the tile of entry i - 37 gets a new pile
//   (CreatePile kind 0xfe) with all remaining units if it is on the map,
//   without flags 0x0f, settler, pile/building number, with a walk region,
//   and none of its six neighbours holds a pile or building. Each change
//   adds the units to the goods counter of the owner of the pile's tile
//   (AddPlayerCounter). Units that find no place within 10,000 entries are
//   lost. EAX: callers don't use it. ret 0x10.
// GameWorld::TakeGoodFromPile (0x00505820): (pile): one reserved unit is
//   taken: leaving (+0x2) and amount (+0x1) -= 1. A building pile (kind 0)
//   that is now empty with nothing coming becomes an empty pile
//   (MakePileEmpty) and gets back the goods type of its building
//   (PileNeighbourBuildingByteE, SetPileGood; not if that is 0xff); an
//   empty dropped pile (kind 0xfe) is removed from its tile
//   (RemovePileFromTile). EAX = the last callee's, else world + pile * 14
//   (callers use AL at most). ret 4.
// All thiscall (ECX = world).

#include "game/debug_log.h"
#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace s3re::game {

namespace {

using layout::Area;
using layout::MapOffset;
using layout::Pile;
using layout::Tile;

// area * 0x170: the original's word index of an area record.
constexpr std::uint32_t kAreaWords = sizeof(Area) / 2;

constexpr std::uint8_t kDroppedPile = 0xfe;  // Pile::art of goods lying on the ground
constexpr std::uint8_t kPileFull = 8;
constexpr std::uint32_t kOffsetEntries = 10000;
constexpr std::uint32_t kNewPileLag = 37;  // entries of radius 0..3

// The tile a pile lies on (32-bit arithmetic of the original).
Tile& PileTile(GameWorld* world, const Pile& pile) {
    return TileAtOffset(world, TileOffset(pile.x, pile.y));
}

}  // namespace

using SetPileGoodFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t pile, std::uint32_t good);
using DropGoodNearFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t good,
                                                  std::uint32_t amount, std::uint32_t x, std::uint32_t y);

// Purpose: An empty pile slot (e.g. a building's output place) gets a goods type again: it moves from
// its area's list of empty piles to the list of piles of that goods type, where carriers look.
std::uint32_t __fastcall GameWorld_SetPileGood(GameWorld* world, void*, std::uint32_t pile, std::uint32_t good) {
    Pile& p = world->piles[pile];
    const std::uint32_t area = PileTile(world, p).area;
    Area& record = world->areas[area];
    if (p.prev != 0) {
        world->piles[p.prev].next = p.next;
    } else {
        record.pile_list = p.next;
    }
    if (p.next != 0) {
        world->piles[p.next].prev = p.prev;
    }
    p.good = static_cast<std::uint8_t>(good);
    const std::uint16_t head = record.piles_by_goods[good & 0xff];
    p.next = head;
    if (head != 0) {
        world->piles[head].prev = static_cast<std::uint16_t>(pile);
    }
    p.prev = 0;
    record.piles_by_goods[p.good] = static_cast<std::uint16_t>(pile);
    return area * kAreaWords + p.good;
}

// Purpose: Puts goods on the ground near a place (e.g. what a settler carried when it lost its job, or
// what is left of a destroyed building): first onto nearby dropped piles of the same goods, then as new
// piles on free tiles further out.
std::uint32_t __fastcall GameWorld_DropGoodNear(GameWorld* world, void*, std::uint32_t good, std::uint32_t amount,
                                                std::uint32_t x, std::uint32_t y) {
    std::uint32_t remaining = amount;
    for (std::uint32_t i = 0; remaining != 0 && i < kOffsetEntries; ++i) {
        const MapOffset& offset = world->offsets[i];
        const std::uint32_t tx = static_cast<std::uint32_t>(offset.dx) + x;
        const std::uint32_t ty = static_cast<std::uint32_t>(offset.dy) + y;
        if (GameWorld_IsOnMap(world, nullptr, tx, ty) != 0) {
            const Tile& tile = TileAtOffset(world, TileOffset(tx, ty));
            const std::uint32_t pile = tile.object_number;
            if (pile != 0 && (tile.flags & layout::kTileHasBuilding) == 0) {
                Pile& p = world->piles[pile];
                if (p.good == good && p.art == kDroppedPile && p.amount < kPileFull) {
                    if (p.amount == p.leaving) {
                        // All units were spoken for, so the pile was out of
                        // its goods list: list it again.
                        Area& record = world->areas[PileTile(world, p).area];
                        const std::uint16_t head = record.piles_by_goods[p.good];
                        p.next = head;
                        if (head != 0) {
                            world->piles[head].prev = static_cast<std::uint16_t>(pile);
                        }
                        p.prev = 0;
                        record.piles_by_goods[p.good] = static_cast<std::uint16_t>(pile);
                    }
                    const std::uint32_t had = p.amount;
                    if (had + remaining <= kPileFull) {
                        p.amount = static_cast<std::uint8_t>(p.amount + remaining);
                        GameWorld_AddPlayerCounter(world, nullptr, PileTile(world, p).owner, good, remaining);
                        remaining = 0;
                    } else {
                        GameWorld_AddPlayerCounter(world, nullptr, PileTile(world, p).owner, good, kPileFull - had);
                        const std::uint32_t now = p.amount;
                        p.amount = kPileFull;
                        remaining = remaining + now - kPileFull;
                    }
                }
            }
        }
        if (remaining == 0 || i < kNewPileLag) {
            continue;
        }
        const MapOffset& back = world->offsets[i - kNewPileLag];
        const std::uint32_t nx = static_cast<std::uint32_t>(back.dx) + x;
        const std::uint32_t ny = static_cast<std::uint32_t>(back.dy) + y;
        if (GameWorld_IsOnMap(world, nullptr, nx, ny) == 0) {
            continue;
        }
        const std::uint32_t at = TileOffset(nx, ny);
        const Tile& tile = TileAtOffset(world, at);
        if ((tile.flags & 0x0f) != 0 || tile.settler != 0 || tile.walk_region == 0 || tile.object_number != 0) {
            continue;
        }
        static constexpr std::uint32_t kNeighbours[] = {kTileRowBytes, 0u - kTileRowBytes, kTileBytes, 0u - kTileBytes,
                                                        kTileRowBytes + kTileBytes, 0u - kTileRowBytes - kTileBytes};
        bool free = true;
        for (const std::uint32_t step : kNeighbours) {
            if (TileAtOffset(world, at + step).object_number != 0) {
                free = false;
                break;
            }
        }
        if (!free) {
            continue;
        }
        const std::uint32_t created = GameWorld_CreatePile(world, nullptr, nx, ny, good, remaining, kDroppedPile);
        GameWorld_AddPlayerCounter(world, nullptr, PileTile(world, world->piles[created]).owner, good, remaining);
        remaining = 0;
    }
    return 0;  // callers don't use EAX
}

std::uint32_t __fastcall GameWorld_PileNeighbourBuildingByteE(GameWorld* world, void* edx, std::uint32_t pile);
std::uint32_t __fastcall GameWorld_MakePileEmpty(GameWorld* world, void* edx, std::uint32_t pile);
std::uint32_t __fastcall GameWorld_RemovePileFromTile(GameWorld* world, void* edx, std::uint32_t pile);

using PileFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t pile);

// Purpose: A settler takes one unit from a pile it had reserved: the pile shrinks; an empty building
// pile slot is kept (and gets its building's goods type back), an empty pile on the ground goes away.
std::uint32_t __fastcall GameWorld_TakeGoodFromPile(GameWorld* world, void*, std::uint32_t pile) {
    Pile& p = world->piles[pile];
    p.leaving = static_cast<std::uint8_t>(p.leaving - 1);
    p.amount = static_cast<std::uint8_t>(p.amount - 1);
    const std::uint32_t record = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world)) +
                                 pile * static_cast<std::uint32_t>(sizeof(Pile));
    if (p.art == 0) {
        if (p.amount != 0 || p.coming != 0) {
            return record;
        }
        const std::uint32_t goods = GameWorld_PileNeighbourBuildingByteE(world, nullptr, pile);
        // The original keeps the goods byte in the low byte of its argument slot.
        const std::uint32_t argument = (pile & 0xffffff00u) | (goods & 0xff);
        if ((goods & 0xff) == 0xff) {
            return GameWorld_MakePileEmpty(world, nullptr, pile);
        }
        GameWorld_MakePileEmpty(world, nullptr, pile);
        return GameWorld_SetPileGood(world, nullptr, pile, argument);
    }
    if (p.amount != 0 || p.art != kDroppedPile) {
        return record;
    }
    return GameWorld_RemovePileFromTile(world, nullptr, pile);
}

std::uint32_t __fastcall GameWorld_ResetSettlerToIdle(GameWorld* world, void* edx, std::uint32_t settler);
std::uint32_t __fastcall GameWorld_PileDecAmountLeaving(GameWorld* world, void* edx, std::uint32_t pile);

namespace {

constexpr std::uintptr_t kNoFreeOrderPileText = 0x006a6224;  // "StealGoodFromPile: OrderPile nix mehr frei!"

bool IsFetchJob(std::uint16_t job) {
    return job == 0xa || job == 0xb || job == 0xa9 || job == 0x4c || job == 0x4d || job == 0x68 || job == 0x69;
}

// Cancels the first settler (1..high-water mark) on its way to fetch from
// the pile's tile (a job of IsFetchJob, target = the pile's position).
void CancelFirstFetcher(GameWorld* world, const Pile& p) {
    for (std::uint32_t s = 1; s <= world->settler_high_water; ++s) {
        const layout::Settler& settler = world->settlers[s];
        if (settler.owner != 0xff && IsFetchJob(settler.job) && settler.target_x == p.x && settler.target_y == p.y) {
            GameWorld_ResetSettlerToIdle(world, nullptr, s);
            return;
        }
    }
}

}  // namespace

// Purpose: A thief takes one unit from a pile. If every unit is already promised to a carrier, the
// first carrier on its way there is sent back idle; a building's order pile (kind 0xfd) that is fully
// promised can't be robbed. Returns whether the theft worked (AL).
std::uint32_t __fastcall GameWorld_StealGoodFromPile(GameWorld* world, void*, std::uint32_t pile) {
    Pile& p = world->piles[pile];
    if (p.amount == 0) {
        return (pile * 7u) & 0xffffff00u;  // the original's EAX: AL = 0
    }
    const std::int32_t free = static_cast<std::int32_t>(p.amount) - static_cast<std::int32_t>(p.leaving);
    if (p.art <= 0x32 || p.art == 0xff) {
        if (free <= 0) {
            if (p.leaving == 0) {
                return p.art == 0xff ? 0 : 1;
            }
            CancelFirstFetcher(world, p);
        }
    } else if (p.art == 0xfc) {
        const std::uint32_t leaving = p.leaving & 0x0f;  // the low nibble (the high one counts arrivals)
        if (static_cast<std::int32_t>(p.amount) - static_cast<std::int32_t>(leaving) <= 0) {
            if (leaving == 0) {
                return 0;
            }
            for (std::uint32_t s = 1; s <= world->settler_high_water; ++s) {
                layout::Settler& settler = world->settlers[s];
                if (settler.owner != 0xff && settler.target_x == p.x && settler.target_y == p.y &&
                    (settler.job == 0x133 || settler.job == 0x134)) {
                    if (settler.second_unit == 0) {
                        GameWorld_ResetSettlerToIdle(world, nullptr, s);
                    } else {
                        settler.second_unit = 0;
                        GameWorld_PileDecAmountLeaving(world, nullptr, pile);
                    }
                    break;
                }
            }
        }
    } else if (p.art == 0xfd) {
        if (free <= 0) {
            GameDebugLog(kNoFreeOrderPileText);
            return 0;
        }
    } else if (p.art == kDroppedPile) {
        if (free <= 0 && p.leaving != 0) {
            CancelFirstFetcher(world, p);
        }
    } else {
        return 1;  // kinds 0x33..0xfb: nothing happens
    }
    GameWorld_PileIncAmountLeaving(world, nullptr, pile);
    GameWorld_TakeGoodFromPile(world, nullptr, pile);
    return 1;
}

S3RE_HOOK(0x00505c20, GameWorld_StealGoodFromPile, PileFn, Original_GameWorld_StealGoodFromPile,
          GameWorld_StealGoodFromPile);
S3RE_HOOK(0x00505820, GameWorld_TakeGoodFromPile, PileFn, Original_GameWorld_TakeGoodFromPile,
          GameWorld_TakeGoodFromPile);
S3RE_HOOK(0x005061b0, GameWorld_SetPileGood, SetPileGoodFn, Original_GameWorld_SetPileGood, GameWorld_SetPileGood);
S3RE_HOOK(0x005046b0, GameWorld_DropGoodNear, DropGoodNearFn, Original_GameWorld_DropGoodNear, GameWorld_DropGoodNear);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestPiles = 0x800;
constexpr std::uint32_t kTestAreas = 0x200;
constexpr std::size_t kTestWorld = offsetof(GameWorld, piles) + (kTestPiles + 1) * sizeof(Pile);

std::uint16_t RandomLink(test::Context& ctx) {
    return static_cast<std::uint16_t>(ctx.rng().Chance(1, 3) ? 0 : 1 + ctx.rng().Below(kTestPiles));
}

// Wrong: unlinks from the list of the old goods type instead of the empty list.
std::uint32_t __fastcall WrongSetPileGood(GameWorld* world, void* edx, std::uint32_t pile, std::uint32_t good) {
    Pile& p = world->piles[pile];
    Area& record = world->areas[PileTile(world, p).area];
    const std::uint16_t empty = record.pile_list;
    const bool fromGoods = p.prev == 0 && p.good < layout::kGoodsTypes;
    const std::uint32_t result = GameWorld_SetPileGood(world, edx, pile, good);
    if (fromGoods && record.pile_list != empty) {
        record.pile_list = empty;
    }
    return result;
}

void CompareSetPileGood(test::Context& ctx, SetPileGoodFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t pile = 1 + ctx.rng().Below(kTestPiles);
        const std::uint32_t good = ctx.rng().Chance(1, 10) ? ctx.rng().Next32() : ctx.rng().Below(36);
        const auto x = static_cast<std::uint16_t>(ctx.rng().Below(384));
        const auto y = static_cast<std::uint16_t>(ctx.rng().Below(384));
        const auto area = static_cast<std::uint16_t>(ctx.rng().Below(kTestAreas));
        const auto oldGood = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0xff : ctx.rng().Below(36));
        const std::uint16_t previous = RandomLink(ctx), next = RandomLink(ctx), head = RandomLink(ctx),
                            empty = ctx.rng().Chance(1, 2) ? static_cast<std::uint16_t>(pile) : RandomLink(ctx);
        worlds.Both([&](GameWorld* w) {
            Pile& p = w->piles[pile];
            p.x = x;
            p.y = y;
            p.good = oldGood;
            p.prev = previous;
            p.next = next;
            w->tiles[x][y].area = area;
            w->areas[area].piles_by_goods[good & 0xff] = head;
            w->areas[area].pile_list = empty;
        });
        const std::uint32_t expected = Original_GameWorld_SetPileGood(worlds.original(), nullptr, pile, good);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, pile, good);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 256 == 255 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 255 before it): pile %u good %u", i, pile, good);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(GameWorld_SetPileGood_MatchesOriginal) {
    CompareSetPileGood(ctx, &GameWorld_SetPileGood, 20000);
}
S3RE_TEST(GameWorld_SetPileGood_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSetPileGood(probe, &WrongSetPileGood, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

constexpr std::size_t kDropTestWorld = sizeof(GameWorld);

// Wrong: never adds to an existing pile that was out of its goods list.
std::uint32_t __fastcall WrongDropGoodNear(GameWorld* world, void* edx, std::uint32_t good, std::uint32_t amount,
                                           std::uint32_t x, std::uint32_t y) {
    std::uint8_t saved[kTestPiles + 1];
    for (std::uint32_t p = 0; p <= kTestPiles; ++p) {
        saved[p] = world->piles[p].art;
        if (world->piles[p].amount == world->piles[p].leaving && saved[p] == kDroppedPile) {
            world->piles[p].art = 0;
        }
    }
    const std::uint32_t result = GameWorld_DropGoodNear(world, edx, good, amount, x, y);
    for (std::uint32_t p = 0; p <= kTestPiles; ++p) {
        if (world->piles[p].art == 0 && saved[p] == kDroppedPile) {
            world->piles[p].art = kDroppedPile;
        }
    }
    return result;
}

// The game's offset table: all tiles by TileDistance from (0, 0), nearest
// first (ring r holds 6r tiles; entries 0..36 are radius 0..3).
void FillOffsetSpiral(test::WorldPair& worlds) {
    std::uint32_t k = 0;
    for (std::int32_t r = 0; k < kOffsetEntries; ++r) {
        for (std::int32_t dx = -r; dx <= r && k < kOffsetEntries; ++dx) {
            for (std::int32_t dy = -r; dy <= r && k < kOffsetEntries; ++dy) {
                if (TileDistance(0, 0, static_cast<std::uint32_t>(dx), static_cast<std::uint32_t>(dy)) !=
                    static_cast<std::uint32_t>(r)) {
                    continue;
                }
                worlds.Both([&](GameWorld* w) {
                    w->offsets[k].dx = dx;
                    w->offsets[k].dy = dy;
                    w->offsets[k].distance = r;
                });
                ++k;
            }
        }
    }
}

// A neighbourhood of (x, y) (radius 7) with free tiles, dropped piles of
// the goods type (some full, some all leaving), other piles and buildings.
// Pile records come from a small range so piles get reused and links point
// at each other; CreatePile finds free records (goods 0) among them.
void SetUpDrop(test::Context& ctx, test::WorldPair& worlds, std::uint32_t x, std::uint32_t y, std::uint32_t good) {
    for (std::int32_t dx = -7; dx <= 7; ++dx) {
        for (std::int32_t dy = -7; dy <= 7; ++dy) {
            const std::uint32_t tx = x + dx, ty = y + dy;
            Tile tile{};
            tile.owner = static_cast<std::uint8_t>(ctx.rng().Below(20));
            tile.area = static_cast<std::uint16_t>(ctx.rng().Below(kTestAreas));
            tile.walk_region = static_cast<std::uint8_t>(ctx.rng().Chance(1, 10) ? 0 : 1 + ctx.rng().Below(5));
            tile.flags = static_cast<std::uint8_t>(ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : 0);
            tile.settler = static_cast<std::uint16_t>(ctx.rng().Chance(1, 10) ? 1 + ctx.rng().Below(100) : 0);
            const std::uint32_t what = ctx.rng().Below(12);
            std::uint32_t pile = 0;
            if (what < 3) {
                pile = 1 + ctx.rng().Below(kTestPiles);
                tile.object_number = static_cast<std::uint16_t>(pile);
            }
            if (what == 3) {
                tile.object_number = static_cast<std::uint16_t>(1 + ctx.rng().Below(100));
                tile.flags |= layout::kTileHasBuilding;
            }
            worlds.Both([&](GameWorld* w) {
                Tile& t = TileAtOffset(w, TileOffset(tx, ty));
                const std::uint8_t height = t.height;
                t = tile;
                t.height = height;
            });
            if (pile != 0) {
                const auto amount = static_cast<std::uint8_t>(ctx.rng().Chance(1, 10) ? ctx.rng().Next32() : ctx.rng().Below(9));
                const auto leaving = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3) ? amount : ctx.rng().Below(9));
                const auto pileGood = static_cast<std::uint8_t>(ctx.rng().Chance(3, 4) ? good : ctx.rng().Below(36));
                const auto art = static_cast<std::uint8_t>(ctx.rng().Chance(3, 4) ? kDroppedPile : ctx.rng().Next32());
                const std::uint16_t previous = RandomLink(ctx), next = RandomLink(ctx), head = RandomLink(ctx);
                worlds.Both([&](GameWorld* w) {
                    Pile& p = w->piles[pile];
                    p.good = pileGood;
                    p.amount = amount;
                    p.leaving = leaving;
                    p.art = art;
                    p.x = static_cast<std::uint16_t>(tx);
                    p.y = static_cast<std::uint16_t>(ty);
                    p.prev = previous;
                    p.next = next;
                    w->areas[tile.area].piles_by_goods[pileGood] = head;
                });
            }
        }
    }
    // Some free pile records for CreatePile.
    for (int k = 0; k < 4; ++k) {
        const std::uint32_t pile = 1 + ctx.rng().Below(kTestPiles);
        worlds.Both([&](GameWorld* w) { w->piles[pile].good = 0; });
    }
}

void CompareDropGoodNear(test::Context& ctx, DropGoodNearFn candidate, int cases) {
    test::WorldPair worlds(ctx, kDropTestWorld);
    const std::uint32_t mapSize = 384;
    FillOffsetSpiral(worlds);
    worlds.Both([&](GameWorld* w) {
        w->map_size = mapSize;
        w->pile_high_water = kTestPiles;
        for (std::uint32_t p = kTestPiles + 1; p < 0x7d00; ++p) {
            w->piles[p].good = 1;  // CreatePile only finds records below kTestPiles + 1
        }
        w->piles[0].x = 0;  // CreatePile's "none free" result is used as a pile too
        w->piles[0].y = 0;
        // Tiles outside the prepared neighbourhoods: a building and a
        // settler, so they take no goods (their random pile numbers would
        // lead to piles off the map).
        for (auto& row : w->tiles) {
            for (Tile& tile : row) {
                tile.flags |= layout::kTileHasBuilding;
                tile.settler |= 1;
            }
        }
    });
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t x = ctx.rng().Chance(1, 10) ? ctx.rng().Below(mapSize) : 8 + ctx.rng().Below(mapSize - 16);
        const std::uint32_t y = ctx.rng().Chance(1, 10) ? ctx.rng().Below(mapSize) : 8 + ctx.rng().Below(mapSize - 16);
        const std::uint32_t good = ctx.rng().Below(36);
        const std::uint32_t amount = ctx.rng().Chance(1, 10) ? ctx.rng().Below(40) : 1 + ctx.rng().Below(8);
        SetUpDrop(ctx, worlds, x, y, good);
        Original_GameWorld_DropGoodNear(worlds.original(), nullptr, good, amount, x, y);
        candidate(worlds.ours(), nullptr, good, amount, x, y);
        if ((i % 16 == 15 || i == cases - 1) && !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 15 before it): good %u amount %u at (%u, %u)", i,
                     good, amount, x, y);
            return;
        }
    }
}

}  // namespace

namespace {

// Wrong: a dropped pile is not removed when it becomes empty.
std::uint32_t __fastcall WrongTakeGoodFromPile(GameWorld* world, void* edx, std::uint32_t pile) {
    Pile& p = world->piles[pile];
    if (p.art == kDroppedPile && p.amount == 1) {
        p.leaving = static_cast<std::uint8_t>(p.leaving - 1);
        p.amount = 0;
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world)) + pile * 14;
    }
    return GameWorld_TakeGoodFromPile(world, edx, pile);
}

void CompareTakeGood(test::Context& ctx, PileFn candidate, int cases) {
    test::WorldPair worlds(ctx, sizeof(GameWorld));
    FillOffsetSpiral(worlds);
    worlds.Both([&](GameWorld* w) {
        for (std::uint32_t p = 0; p < 0x7d00; ++p) {
            w->piles[p].x = static_cast<std::uint16_t>(w->piles[p].x % 384);
            w->piles[p].y = static_cast<std::uint16_t>(w->piles[p].y % 384);
        }
        for (auto& row : w->tiles) {
            for (Tile& tile : row) {
                tile.area = static_cast<std::uint16_t>(tile.area % kTestAreas);
                tile.object_number = static_cast<std::uint16_t>(tile.object_number % 0x100);
            }
        }
    });
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t pile = 1 + ctx.rng().Below(kTestPiles);
        static constexpr std::uint8_t kKinds[] = {0, 0, 0, kDroppedPile, kDroppedPile, 0xff, 7};
        const std::uint8_t kind = kKinds[ctx.rng().Below(sizeof(kKinds))];
        const auto amount = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 1 : ctx.rng().Below(5));
        const auto coming = static_cast<std::uint8_t>(ctx.rng().Chance(2, 3) ? 0 : ctx.rng().Below(4));
        const std::uint16_t previous = RandomLink(ctx), next = RandomLink(ctx);
        worlds.Both([&](GameWorld* w) {
            Pile& p = w->piles[pile];
            p.art = kind;
            p.amount = amount;
            p.coming = coming;
            p.prev = previous;
            p.next = next;
        });
        auto relative = [](std::uint32_t value, GameWorld* w) {
            const std::uint32_t offset = value - static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(w));
            return offset < sizeof(GameWorld) ? offset : value;
        };
        const std::uint32_t expected =
            relative(Original_GameWorld_TakeGoodFromPile(worlds.original(), nullptr, pile), worlds.original());
        const std::uint32_t actual = relative(candidate(worlds.ours(), nullptr, pile), worlds.ours());
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 256 == 255 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 255 before it): pile %u", i, pile);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(GameWorld_TakeGoodFromPile_MatchesOriginal) {
    CompareTakeGood(ctx, &GameWorld_TakeGoodFromPile, 5000);
}
S3RE_TEST(GameWorld_TakeGoodFromPile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareTakeGood(probe, &WrongTakeGoodFromPile, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

// Wrong: a fully promised dropped pile cancels no carrier.
std::uint32_t __fastcall WrongStealGoodFromPile(GameWorld* world, void* edx, std::uint32_t pile) {
    Pile& p = world->piles[pile];
    if (p.art == kDroppedPile && p.amount <= p.leaving && p.leaving != 0 && p.amount != 0) {
        GameWorld_PileIncAmountLeaving(world, nullptr, pile);
        GameWorld_TakeGoodFromPile(world, nullptr, pile);
        return 1;
    }
    return GameWorld_StealGoodFromPile(world, edx, pile);
}

void CompareSteal(test::Context& ctx, PileFn candidate, int cases) {
    test::WorldPair worlds(ctx, sizeof(GameWorld));
    FillOffsetSpiral(worlds);
    worlds.Both([&](GameWorld* w) {
        w->map_size = 384;
        w->building_high_water = 0x100;  // PileIncAmountLeaving's log loop
        for (std::uint32_t p = 0; p < 0x7d00; ++p) {
            Pile& pile = w->piles[p];
            pile.x = static_cast<std::uint16_t>(pile.x % 384);
            pile.y = static_cast<std::uint16_t>(pile.y % 384);
            pile.prev = static_cast<std::uint16_t>(pile.prev % kTestPiles);
            pile.next = static_cast<std::uint16_t>(pile.next % kTestPiles);
        }
        for (std::uint32_t s = 0; s < 0x7d00; ++s) {
            layout::Settler& r = w->settlers[s];
            r.x = static_cast<std::uint16_t>(r.x % 384);
            r.y = static_cast<std::uint16_t>(r.y % 384);
            r.target_x = static_cast<std::uint16_t>(r.target_x % 384);
            r.target_y = static_cast<std::uint16_t>(r.target_y % 384);
            r.building = static_cast<std::uint16_t>(r.building % 0x100);
        }
        for (auto& row : w->tiles) {
            for (Tile& tile : row) {
                tile.area = static_cast<std::uint16_t>(tile.area % kTestAreas);
                tile.object_number = static_cast<std::uint16_t>(tile.object_number % kTestPiles);
            }
        }
    });
    DebugLogRecorder logs;
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t pile = 1 + ctx.rng().Below(kTestPiles);
        static constexpr std::uint8_t kKinds[] = {0, 5, 0x32, 0x33, 0xfc, 0xfc, 0xfd, 0xfe, 0xfe, 0xff, 0xff, 0x80};
        const std::uint8_t kind = kKinds[ctx.rng().Below(sizeof(kKinds))];
        const auto amount = static_cast<std::uint8_t>(ctx.rng().Chance(1, 8) ? 0 : 1 + ctx.rng().Below(4));
        const auto leaving = static_cast<std::uint8_t>(
            ctx.rng().Chance(1, 2) ? amount + ctx.rng().Below(2) : ctx.rng().Below(5) | (ctx.rng().Below(3) << 4));
        const auto px = static_cast<std::uint16_t>(ctx.rng().Below(384));
        const auto py = static_cast<std::uint16_t>(ctx.rng().Below(384));
        const std::uint32_t count = ctx.rng().Below(40);
        worlds.Both([&](GameWorld* w) {
            Pile& p = w->piles[pile];
            p.art = kind;
            p.amount = amount;
            p.leaving = leaving;
            p.x = px;
            p.y = py;
            w->settler_high_water = count;
        });
        // Settlers fetching from the pile (some), others elsewhere.
        for (std::uint32_t s = 1; s <= count; ++s) {
            static constexpr std::uint16_t kJobs[] = {0xa, 0xb, 0xa9, 0x4c, 0x4d, 0x68, 0x69, 0x133, 0x134, 0x5};
            const std::uint16_t job = kJobs[ctx.rng().Below(10)];
            const bool here = ctx.rng().Chance(1, 4);
            const auto owner = static_cast<std::uint8_t>(ctx.rng().Chance(1, 6) ? 0xff : ctx.rng().Below(4));
            const auto secondUnit = static_cast<std::uint8_t>(ctx.rng().Below(2));
            worlds.Both([&](GameWorld* w) {
                layout::Settler& r = w->settlers[s];
                r.job = job;
                r.owner = owner;
                r.second_unit = secondUnit;
                if (here) {
                    r.target_x = px;
                    r.target_y = py;
                }
            });
        }
        const std::uint32_t expected = Original_GameWorld_StealGoodFromPile(worlds.original(), nullptr, pile) & 0xff;
        const std::vector<DebugLogCall> expectedLogs = logs.Take();
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, pile) & 0xff;
        const std::vector<DebugLogCall> actualLogs = logs.Take();
        if (!S3RE_EXPECT_EQ(expected, actual) || !S3RE_EXPECT(expectedLogs == actualLogs) ||
            ((i % 64 == 63 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 63 before it): pile %u kind 0x%x", i, pile, kind);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(GameWorld_StealGoodFromPile_MatchesOriginal) {
    CompareSteal(ctx, &GameWorld_StealGoodFromPile, 5000);
}
S3RE_TEST(GameWorld_StealGoodFromPile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSteal(probe, &WrongStealGoodFromPile, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_DropGoodNear_MatchesOriginal) {
    CompareDropGoodNear(ctx, &GameWorld_DropGoodNear, 3000);
}
S3RE_TEST(GameWorld_DropGoodNear_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDropGoodNear(probe, &WrongDropGoodNear, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
