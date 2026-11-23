// Goods piles changed by settler jobs (UpdateSettlers callees, package B).
//
// Pile kinds (Pile::art, PileKind in enums.h): 0 storehouse, 0xfc trade
// ring, 0xfd order (a building's input), 0xfe dropped, 0xff output.
//
// CGameData::SetPileGood (0x005061b0): (pile, good): the pile (an empty
//   pile record, kept in its area's list of empty piles +0x8) leaves that
//   list, gets the goods type `good` (byte +0x0) and goes to the head of
//   its area's list for that goods type (area = the area word of the
//   pile's tile). The unlink does not look at the old goods type: a pile
//   without a previous link is assumed to be the empty list's head. EAX =
//   area * 0x170 + the new goods byte (the original's index arithmetic;
//   one caller tail-returns it). ret 8. Callers: MakePileEmpty's
//   counterpart in 0x4dfb40, 0x5052c0, TakeGoodFromPile 0x505820,
//   ResetSettlerToIdle 0x50cf50.
// CGameData::DropGoodNear (0x005046b0): (good, amount, x, y): puts
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
// CGameData::TakeGoodFromPile (0x00505820): (pile): one reserved unit is
//   taken: leaving (+0x2) and amount (+0x1) -= 1. A storehouse pile (kind 0)
//   that is now empty with nothing coming becomes an empty pile
//   (MakePileEmpty) and gets back the goods type of its storehouse
//   (PileNeighbourBuildingByteE, SetPileGood; not if that is 0xff); an
//   empty dropped pile (kind 0xfe) is removed from its tile
//   (RemovePileFromTile). EAX = the last callee's, else world + pile * 14
//   (callers use AL at most). ret 4.
// CGameData::StealGoodFromPile (0x00505c20): (pile): a thief takes one unit
//   (PileIncAmountLeaving, TakeGoodFromPile). Nothing on the pile: AL = 0.
//   Per kind, when no unit is free (amount - leaving <= 0): storehouse
//   kinds (0..0x32) and output piles cancel the first settler fetching
//   from the pile's tile (jobs CarrierWalkToPile, CarrierAtPile,
//   CarrierPickUp, 76, 77, CarrierWalkToTool, CarrierTakeTool; with nothing
//   leaving: AL = 1 for kinds 0..0x32, 0 for output piles, and nothing is
//   taken); trade ring piles (only the low nibble of leaving counts) cancel
//   the first carrier with job 307/308 there, or take back his second unit
//   (PileDecAmountLeaving); order piles refuse (DebugLog "OrderPile nix mehr
//   frei!", AL = 0); dropped piles cancel a fetcher if units are leaving.
//   Kinds 0x33..0xfb: AL = 1, nothing happens. ret 4.
// All thiscall (ECX = world).

#include "game/debug_log.h"
#include "game/enums/goods.h"
#include "game/enums/piles.h"
#include "game/enums/job_ids.h"
#include "game/replacements.h"
#include "game/texts.h"
#include "game/typed_fields.h"
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
using layout::Settler;
using layout::Tile;

using Job = SettlerJob;

// area * 0x170: the original's word index of an area record.
constexpr std::uint32_t kAreaWords = sizeof(Area) / 2;

constexpr std::uint8_t kPileFull = 8;            // units a dropped pile holds
constexpr std::uint32_t kOffsetEntries = 10000;  // DropGoodNear's search radius, in offset-table entries
constexpr std::uint32_t kNewPileLag = 37;        // entries of radius 0..3
constexpr std::uint8_t kFreeSettler = 0xff;      // Settler::owner of a free record

// Tile flags 0x01..0x08 (blocked, claimed, 0x04, building footprint): no
// new dropped pile goes there.
constexpr std::uint8_t kTileFlagsNoNewPile = 0x0f;

// A trade ring pile's `leaving` byte counts units leaving in the low nibble
// (the high one counts arrivals, see settler_idle.cpp).
constexpr std::uint8_t kTradeRingLeavingMask = 0x0f;

// The tile a pile lies on (32-bit arithmetic of the original).
Tile& PileTile(CGameData* world, const Pile& pile) {
    return TileAtOffset(world, TileOffset(pile.x, pile.y));
}

// Puts `pile` at the head of the list whose head word is `head` (next, the
// old head's previous, previous, head: the original's order).
void PushFront(CGameData* world, std::uint32_t pile, std::uint16_t& head) {
    Pile& p = world->piles[pile];
    const std::uint16_t first = head;
    p.next = first;
    if (first != 0) {
        world->piles[first].prev = static_cast<std::uint16_t>(pile);
    }
    p.prev = 0;
    head = static_cast<std::uint16_t>(pile);
}

}  // namespace

using SetPileGoodFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t pile, std::uint32_t good);
using DropGoodNearFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t good,
                                                  std::uint32_t amount, std::uint32_t x, std::uint32_t y);

// Purpose: An empty pile slot (e.g. a building's output place) gets a goods type again: it moves from
// its area's list of empty piles to the list of piles of that goods type, where carriers look.
std::uint32_t __fastcall CGameData_SetPileGood(CGameData* world, void*, std::uint32_t pile, std::uint32_t good) {
    Pile& p = world->piles[pile];
    const std::uint32_t area = PileTile(world, p).area;
    Area& record = world->areas[area];
    // Leave the empty list (a pile without a previous link is taken to be
    // its head, whatever its goods type).
    if (p.prev != 0) {
        world->piles[p.prev].next = p.next;
    } else {
        record.pile_list = p.next;
    }
    if (p.next != 0) {
        world->piles[p.next].prev = p.prev;
    }
    p.good = static_cast<std::uint8_t>(good);
    // The original reads the head through the argument's low byte and writes
    // it through the stored goods byte (the same index).
    const std::uint16_t head = record.piles_by_goods[good & 0xff];
    p.next = head;
    if (head != 0) {
        world->piles[head].prev = static_cast<std::uint16_t>(pile);
    }
    p.prev = 0;
    record.piles_by_goods[p.good] = static_cast<std::uint16_t>(pile);
    return area * kAreaWords + p.good;
}

namespace {

// 0x5046b0, part (1): adds units to the dropped pile on (tx, ty), if it is
// one of the goods type with room. Returns the units left.
std::uint32_t AddToDroppedPile(CGameData* world, std::uint32_t good, std::uint32_t remaining, std::uint32_t tx,
                               std::uint32_t ty) {
    const Tile& tile = TileAtOffset(world, TileOffset(tx, ty));
    const std::uint32_t pile = tile.object_number;
    if (pile == 0 || (tile.flags & layout::kTileHasBuilding) != 0) {
        return remaining;
    }
    Pile& p = world->piles[pile];
    if (p.good != good || KindOf(p) != PileKind::Dropped || p.amount >= kPileFull) {
        return remaining;
    }
    if (p.amount == p.leaving) {
        // All units were spoken for, so the pile was out of its goods list:
        // list it again.
        PushFront(world, pile, world->areas[PileTile(world, p).area].piles_by_goods[p.good]);
    }
    const std::uint32_t had = p.amount;
    if (had + remaining <= kPileFull) {
        p.amount = static_cast<std::uint8_t>(p.amount + remaining);
        CGameData_AddPlayerCounter(world, nullptr, PileTile(world, p).owner, good, remaining);
        return 0;
    }
    CGameData_AddPlayerCounter(world, nullptr, PileTile(world, p).owner, good, kPileFull - had);
    const std::uint32_t now = p.amount;  // read again after the call, as the original
    p.amount = kPileFull;
    return remaining + now - kPileFull;
}

// 0x5046b0, part (2): whether a new dropped pile may go on the tile at
// (nx, ny): on the map, free of flags 0x0f, settlers, piles and buildings,
// with a walk region, and no pile or building on its six neighbours.
bool CanPlaceNewPile(CGameData* world, std::uint32_t nx, std::uint32_t ny) {
    if (CGameData_IsOnMap(world, nullptr, nx, ny) == 0) {
        return false;
    }
    const std::uint32_t at = TileOffset(nx, ny);
    const Tile& tile = TileAtOffset(world, at);
    if ((tile.flags & kTileFlagsNoNewPile) != 0 || tile.settler != 0 || tile.walk_region == 0 ||
        tile.object_number != 0) {
        return false;
    }
    static constexpr std::uint32_t kNeighbours[] = {kTileRowBytes, 0u - kTileRowBytes, kTileBytes, 0u - kTileBytes,
                                                    kTileRowBytes + kTileBytes, 0u - kTileRowBytes - kTileBytes};
    for (const std::uint32_t step : kNeighbours) {
        if (TileAtOffset(world, at + step).object_number != 0) {
            return false;
        }
    }
    return true;
}

}  // namespace

// Purpose: Puts goods on the ground near a place (e.g. what a settler carried when it lost its job, or
// what is left of a destroyed building): first onto nearby dropped piles of the same goods, then as new
// piles on free tiles further out.
std::uint32_t __fastcall CGameData_DropGoodNear(CGameData* world, void*, std::uint32_t good, std::uint32_t amount,
                                                std::uint32_t x, std::uint32_t y) {
    std::uint32_t remaining = amount;
    for (std::uint32_t i = 0; remaining != 0 && i < kOffsetEntries; ++i) {
        const MapOffset& offset = world->offsets[i];
        const std::uint32_t tx = static_cast<std::uint32_t>(offset.dx) + x;
        const std::uint32_t ty = static_cast<std::uint32_t>(offset.dy) + y;
        if (CGameData_IsOnMap(world, nullptr, tx, ty) != 0) {
            remaining = AddToDroppedPile(world, good, remaining, tx, ty);
        }
        // New piles trail the search by 37 entries, so nearby piles are
        // filled first.
        if (remaining == 0 || i < kNewPileLag) {
            continue;
        }
        const MapOffset& back = world->offsets[i - kNewPileLag];
        const std::uint32_t nx = static_cast<std::uint32_t>(back.dx) + x;
        const std::uint32_t ny = static_cast<std::uint32_t>(back.dy) + y;
        if (!CanPlaceNewPile(world, nx, ny)) {
            continue;
        }
        const std::uint32_t created =
            CGameData_CreatePile(world, nullptr, nx, ny, good, remaining, Value(PileKind::Dropped));
        CGameData_AddPlayerCounter(world, nullptr, PileTile(world, world->piles[created]).owner, good, remaining);
        remaining = 0;
    }
    return 0;  // callers don't use EAX
}

std::uint32_t __fastcall CGameData_PileNeighbourBuildingByteE(CGameData* world, void* edx, std::uint32_t pile);
std::uint32_t __fastcall CGameData_MakePileEmpty(CGameData* world, void* edx, std::uint32_t pile);
std::uint32_t __fastcall CGameData_RemovePileFromTile(CGameData* world, void* edx, std::uint32_t pile);

using PileFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t pile);

// Purpose: A settler takes one unit from a pile it had reserved: the pile shrinks; an empty storehouse
// pile is kept (and gets its storehouse's goods type back), an empty pile on the ground goes away.
std::uint32_t __fastcall CGameData_TakeGoodFromPile(CGameData* world, void*, std::uint32_t pile) {
    Pile& p = world->piles[pile];
    p.leaving = static_cast<std::uint8_t>(p.leaving - 1);
    p.amount = static_cast<std::uint8_t>(p.amount - 1);
    const std::uint32_t record = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world)) +
                                 pile * static_cast<std::uint32_t>(sizeof(Pile));
    if (KindOf(p) == PileKind::Storehouse) {
        if (p.amount != 0 || p.coming != 0) {
            return record;
        }
        const std::uint32_t goods = CGameData_PileNeighbourBuildingByteE(world, nullptr, pile);
        // The original keeps the goods byte in the low byte of its argument slot.
        const std::uint32_t argument = (pile & 0xffffff00u) | (goods & 0xff);
        if ((goods & 0xff) == Value(Good::Empty)) {
            return CGameData_MakePileEmpty(world, nullptr, pile);
        }
        CGameData_MakePileEmpty(world, nullptr, pile);
        return CGameData_SetPileGood(world, nullptr, pile, argument);
    }
    if (p.amount != 0 || KindOf(p) != PileKind::Dropped) {
        return record;
    }
    return CGameData_RemovePileFromTile(world, nullptr, pile);
}

std::uint32_t __fastcall CGameData_ResetSettlerToIdle(CGameData* world, void* edx, std::uint32_t settler);
std::uint32_t __fastcall CGameData_PileDecAmountLeaving(CGameData* world, void* edx, std::uint32_t pile);

namespace {

// The jobs of a settler on its way to fetch from a pile (or at it).
bool IsFetchJob(Job job) {
    return job == Job::CarrierWalkToPile || job == Job::CarrierAtPile || job == Job::CarrierPickUp ||
           job == Job::Job76 || job == Job::Job77 || job == Job::CarrierWalkToTool || job == Job::CarrierTakeTool;
}

bool TargetsPile(const Settler& settler, const Pile& pile) {
    return settler.target_x == pile.x && settler.target_y == pile.y;
}

// Cancels the first settler (1..high-water mark) on its way to fetch from
// the pile's tile (a job of IsFetchJob, target = the pile's position).
void CancelFirstFetcher(CGameData* world, const Pile& p) {
    for (std::uint32_t s = 1; s <= world->settler_high_water; ++s) {
        const Settler& settler = world->settlers[s];
        if (settler.owner != kFreeSettler && IsFetchJob(JobOf(settler)) && TargetsPile(settler, p)) {
            CGameData_ResetSettlerToIdle(world, nullptr, s);
            return;
        }
    }
}

// A trade ring pile: the first carrier fetching there (jobs 307/308) is
// sent back idle, or gives up the second unit he meant to take.
void CancelFirstTradeRingFetcher(CGameData* world, std::uint32_t pile) {
    const Pile& p = world->piles[pile];
    for (std::uint32_t s = 1; s <= world->settler_high_water; ++s) {
        Settler& settler = world->settlers[s];
        if (settler.owner != kFreeSettler && TargetsPile(settler, p) &&
            (JobOf(settler) == Job::Job307 || JobOf(settler) == Job::Job308)) {
            if (settler.second_unit == 0) {
                CGameData_ResetSettlerToIdle(world, nullptr, s);
            } else {
                settler.second_unit = 0;
                CGameData_PileDecAmountLeaving(world, nullptr, pile);
            }
            return;
        }
    }
}

}  // namespace

// Purpose: A thief takes one unit from a pile. If every unit is already promised to a carrier, the
// first carrier on its way there is sent back idle; a building's order pile that is fully promised
// can't be robbed. Returns whether the theft worked (AL).
std::uint32_t __fastcall CGameData_StealGoodFromPile(CGameData* world, void*, std::uint32_t pile) {
    Pile& p = world->piles[pile];
    if (p.amount == 0) {
        return (pile * 7u) & 0xffffff00u;  // the original's EAX: AL = 0
    }
    const std::int32_t free = static_cast<std::int32_t>(p.amount) - static_cast<std::int32_t>(p.leaving);
    const PileKind kind = KindOf(p);
    if (p.art <= kLastStoragePileKind || kind == PileKind::Output) {
        if (free <= 0) {
            if (p.leaving == 0) {
                return kind == PileKind::Output ? 0 : 1;
            }
            CancelFirstFetcher(world, p);
        }
    } else if (kind == PileKind::TradeRing) {
        const std::uint32_t leaving = p.leaving & kTradeRingLeavingMask;
        if (static_cast<std::int32_t>(p.amount) - static_cast<std::int32_t>(leaving) <= 0) {
            if (leaving == 0) {
                return 0;
            }
            CancelFirstTradeRingFetcher(world, pile);
        }
    } else if (kind == PileKind::Order) {
        if (free <= 0) {
            GameDebugLog(texts::kOrderPileFull);
            return 0;
        }
    } else if (kind == PileKind::Dropped) {
        if (free <= 0 && p.leaving != 0) {
            CancelFirstFetcher(world, p);
        }
    } else {
        return 1;  // kinds 51..251 (0x33..0xfb): nothing happens
    }
    CGameData_PileIncAmountLeaving(world, nullptr, pile);
    CGameData_TakeGoodFromPile(world, nullptr, pile);
    return 1;
}

S3RE_HOOK(0x00505c20, CGameData_StealGoodFromPile, PileFn, Original_CGameData_StealGoodFromPile,
          CGameData_StealGoodFromPile);
S3RE_HOOK(0x00505820, CGameData_TakeGoodFromPile, PileFn, Original_CGameData_TakeGoodFromPile,
          CGameData_TakeGoodFromPile);
S3RE_HOOK(0x005061b0, CGameData_SetPileGood, SetPileGoodFn, Original_CGameData_SetPileGood, CGameData_SetPileGood);
S3RE_HOOK(0x005046b0, CGameData_DropGoodNear, DropGoodNearFn, Original_CGameData_DropGoodNear, CGameData_DropGoodNear);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestPiles = 0x800;
constexpr std::uint32_t kTestAreas = 0x200;
constexpr std::size_t kTestWorld = offsetof(CGameData, piles) + (kTestPiles + 1) * sizeof(Pile);

std::uint16_t RandomLink(test::Context& ctx) {
    return static_cast<std::uint16_t>(ctx.rng().Chance(1, 3) ? 0 : 1 + ctx.rng().Below(kTestPiles));
}

// Wrong: unlinks from the list of the old goods type instead of the empty list.
std::uint32_t __fastcall WrongSetPileGood(CGameData* world, void* edx, std::uint32_t pile, std::uint32_t good) {
    Pile& p = world->piles[pile];
    Area& record = world->areas[PileTile(world, p).area];
    const std::uint16_t empty = record.pile_list;
    const bool fromGoods = p.prev == 0 && p.good < layout::kGoodsTypes;
    const std::uint32_t result = CGameData_SetPileGood(world, edx, pile, good);
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
        worlds.Both([&](CGameData* w) {
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
        const std::uint32_t expected = Original_CGameData_SetPileGood(worlds.original(), nullptr, pile, good);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, pile, good);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 256 == 255 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 255 before it): pile %u good %u", i, pile, good);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(CGameData_SetPileGood_MatchesOriginal) {
    CompareSetPileGood(ctx, &CGameData_SetPileGood, 20000);
}
S3RE_TEST(CGameData_SetPileGood_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSetPileGood(probe, &WrongSetPileGood, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

constexpr std::size_t kDropTestWorld = sizeof(CGameData);

// Wrong: never adds to an existing pile that was out of its goods list.
std::uint32_t __fastcall WrongDropGoodNear(CGameData* world, void* edx, std::uint32_t good, std::uint32_t amount,
                                           std::uint32_t x, std::uint32_t y) {
    std::uint8_t saved[kTestPiles + 1];
    for (std::uint32_t p = 0; p <= kTestPiles; ++p) {
        saved[p] = world->piles[p].art;
        if (world->piles[p].amount == world->piles[p].leaving && saved[p] == Value(PileKind::Dropped)) {
            world->piles[p].art = 0;
        }
    }
    const std::uint32_t result = CGameData_DropGoodNear(world, edx, good, amount, x, y);
    for (std::uint32_t p = 0; p <= kTestPiles; ++p) {
        if (world->piles[p].art == 0 && saved[p] == Value(PileKind::Dropped)) {
            world->piles[p].art = Value(PileKind::Dropped);
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
                worlds.Both([&](CGameData* w) {
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
            worlds.Both([&](CGameData* w) {
                Tile& t = TileAtOffset(w, TileOffset(tx, ty));
                const std::uint8_t height = t.height;
                t = tile;
                t.height = height;
            });
            if (pile != 0) {
                const auto amount = static_cast<std::uint8_t>(ctx.rng().Chance(1, 10) ? ctx.rng().Next32() : ctx.rng().Below(9));
                const auto leaving = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3) ? amount : ctx.rng().Below(9));
                const auto pileGood = static_cast<std::uint8_t>(ctx.rng().Chance(3, 4) ? good : ctx.rng().Below(36));
                const auto art = static_cast<std::uint8_t>(ctx.rng().Chance(3, 4) ? Value(PileKind::Dropped) : ctx.rng().Next32());
                const std::uint16_t previous = RandomLink(ctx), next = RandomLink(ctx), head = RandomLink(ctx);
                worlds.Both([&](CGameData* w) {
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
        worlds.Both([&](CGameData* w) { w->piles[pile].good = 0; });
    }
}

void CompareDropGoodNear(test::Context& ctx, DropGoodNearFn candidate, int cases) {
    test::WorldPair worlds(ctx, kDropTestWorld);
    const std::uint32_t mapSize = 384;
    FillOffsetSpiral(worlds);
    worlds.Both([&](CGameData* w) {
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
        Original_CGameData_DropGoodNear(worlds.original(), nullptr, good, amount, x, y);
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
std::uint32_t __fastcall WrongTakeGoodFromPile(CGameData* world, void* edx, std::uint32_t pile) {
    Pile& p = world->piles[pile];
    if (p.art == Value(PileKind::Dropped) && p.amount == 1) {
        p.leaving = static_cast<std::uint8_t>(p.leaving - 1);
        p.amount = 0;
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world)) + pile * 14;
    }
    return CGameData_TakeGoodFromPile(world, edx, pile);
}

void CompareTakeGood(test::Context& ctx, PileFn candidate, int cases) {
    test::WorldPair worlds(ctx, sizeof(CGameData));
    FillOffsetSpiral(worlds);
    worlds.Both([&](CGameData* w) {
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
        static constexpr std::uint8_t kKinds[] = {0, 0, 0, Value(PileKind::Dropped), Value(PileKind::Dropped), 0xff, 7};
        const std::uint8_t kind = kKinds[ctx.rng().Below(sizeof(kKinds))];
        const auto amount = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 1 : ctx.rng().Below(5));
        const auto coming = static_cast<std::uint8_t>(ctx.rng().Chance(2, 3) ? 0 : ctx.rng().Below(4));
        const std::uint16_t previous = RandomLink(ctx), next = RandomLink(ctx);
        worlds.Both([&](CGameData* w) {
            Pile& p = w->piles[pile];
            p.art = kind;
            p.amount = amount;
            p.coming = coming;
            p.prev = previous;
            p.next = next;
        });
        auto relative = [](std::uint32_t value, CGameData* w) {
            const std::uint32_t offset = value - static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(w));
            return offset < sizeof(CGameData) ? offset : value;
        };
        const std::uint32_t expected =
            relative(Original_CGameData_TakeGoodFromPile(worlds.original(), nullptr, pile), worlds.original());
        const std::uint32_t actual = relative(candidate(worlds.ours(), nullptr, pile), worlds.ours());
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 256 == 255 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 255 before it): pile %u", i, pile);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(CGameData_TakeGoodFromPile_MatchesOriginal) {
    CompareTakeGood(ctx, &CGameData_TakeGoodFromPile, 5000);
}
S3RE_TEST(CGameData_TakeGoodFromPile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareTakeGood(probe, &WrongTakeGoodFromPile, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

// Wrong: a fully promised dropped pile cancels no carrier.
std::uint32_t __fastcall WrongStealGoodFromPile(CGameData* world, void* edx, std::uint32_t pile) {
    Pile& p = world->piles[pile];
    if (p.art == Value(PileKind::Dropped) && p.amount <= p.leaving && p.leaving != 0 && p.amount != 0) {
        CGameData_PileIncAmountLeaving(world, nullptr, pile);
        CGameData_TakeGoodFromPile(world, nullptr, pile);
        return 1;
    }
    return CGameData_StealGoodFromPile(world, edx, pile);
}

void CompareSteal(test::Context& ctx, PileFn candidate, int cases) {
    test::WorldPair worlds(ctx, sizeof(CGameData));
    FillOffsetSpiral(worlds);
    worlds.Both([&](CGameData* w) {
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
        worlds.Both([&](CGameData* w) {
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
            worlds.Both([&](CGameData* w) {
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
        const std::uint32_t expected = Original_CGameData_StealGoodFromPile(worlds.original(), nullptr, pile) & 0xff;
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

S3RE_TEST(CGameData_StealGoodFromPile_MatchesOriginal) {
    CompareSteal(ctx, &CGameData_StealGoodFromPile, 5000);
}
S3RE_TEST(CGameData_StealGoodFromPile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSteal(probe, &WrongStealGoodFromPile, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CGameData_DropGoodNear_MatchesOriginal) {
    CompareDropGoodNear(ctx, &CGameData_DropGoodNear, 3000);
}
S3RE_TEST(CGameData_DropGoodNear_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDropGoodNear(probe, &WrongDropGoodNear, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
