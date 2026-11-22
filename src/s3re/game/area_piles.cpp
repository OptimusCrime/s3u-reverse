// Goods piles in the per-area, per-goods-type lists.
//
// A tile's uint16 +0x2 is a building number when flag 0x20 of byte +0xa is
// set, otherwise the number of the goods pile lying there (0 = none). Area
// records (0x2e0 bytes at world+0xe145e8; +0x0 player) keep a doubly linked
// list of piles per goods type: uint16 heads at +0xa + type*2. Piles
// (14-byte records at world+0x123885c; +0x0 goods type, +0x1 amount, +0x4
// kind, PileKind in enums.h) link with +0xa (previous) and +0xc (next).
// Output piles (0xff) and dropped piles (0xfe) leave their goods list while
// every unit on them is spoken for (amount == leaving) and come back when
// one is free again (PileIncAmountLeaving / PileDecAmountLeaving,
// AddGoodsToPile).
//
// CGameData::AddTilePileToArea (0x005051b0): (x, y): if the tile holds a
//   pile (word +0x2 != 0 and no building flag): DebugLog("Pile %u Taken ,
//   eco %u", pile, area), adds the pile's amount to the goods counter of
//   the tile owner (CGameData::AddPlayerCounter(owner, type, amount)), and
//   puts the pile at the head of its area's list for its goods type (area =
//   the tile's area word +0x14, used as is). EAX (the only caller
//   tail-returns it) reproduced: world + tile index * 24 when there is no
//   pile, else area * 0x170. ret 8.
// CGameData::AddGoodsToPile (0x00505650): (pile): one more unit on the
//   pile: byte +0x3 (units on the way) -= 1, amount (+0x1) += 1. Before
//   that, an output pile (kind 0xff) whose amount equals its reserved count
//   (+0x2) is put at the head of its area's list for its goods type (as in
//   AddTilePileToArea; area = the area word of the pile's tile +0x6/+0x8).
//   DebugLog("...", pile, kind, goods, x, y) (format at 0x6a6150) for kind 0
//   with byte +0x3 == 0, for kind 0xfe, and when the amount reaches 9 or
//   more. EAX (callers ignore it; one tail-returns it) reproduced from the
//   original's register use. ret 4.
// CGameData::PileIncAmountLeaving (0x005058c0) (name from its DebugLog
//   text): (pile): one more unit is to leave the pile: byte +0x2 (units
//   leaving) += 1. If it was 8 or more before: DebugLog("PileIncAmountLeaving:
//   art ...", kind, pile, amount, goods, leaving, x, y) and, for the first
//   building 1..count (count at world+0x1499e60; 0x3a-byte records at
//   world+0x1499e64) with one of its words +0x10/+0x12/+0x14/+0xe equal to
//   the pile, DebugLog("... Building[i].art ...", type +0x6, buildup +0x21).
//   (The loop index is compared with the count in 16 bits, as in the
//   original.) Then, if all units are leaving (amount +0x1 == leaving) and
//   the kind (+0x4) is 0xff or 0xfe, the pile is unlinked from its area's
//   list for its goods type (area = the area word of the pile's tile); its
//   own links stay. EAX (callers ignore it or tail-return it; some only
//   overwrite AL) reproduced. ret 4.
// CGameData::PileIncAmountComing (0x00505bc0) / PileDecAmountComing
//   (0x00505bf0): (pile): byte +0x3 (units on the way) += 1 / -= 1. EAX =
//   the byte's address. ret 4.
// CGameData::RemovePileFromTile (0x00504650): (pile): the pile word +0x2 of
//   the tile at (+0x6, +0x8) = 0 and the goods byte +0x0 = 0. EAX = pile +
//   0x14d2e2 (the original's index arithmetic). ret 4.
// CGameData::PileDecAmountLeaving (0x00505ac0): (pile): the counterpart of
//   PileIncAmountLeaving: byte +0x2 (units leaving) -= 1. Before that, if
//   all units were leaving (amount +0x1 == leaving) and the kind (+0x4) is
//   0xff or 0xfe, the pile goes back to the head of its area's list for its
//   goods type (area = the area word of the pile's tile). EAX (callers only
//   overwrite AX) reproduced: the new leaving byte in AL, the upper bits of
//   area * 0x170 after a relink, else of pile * 7. ret 4.
// CGameData::MakePileEmpty (0x00506080): (pile): the pile leaves its area's
//   list for its goods type (unlinked as in PileIncAmountLeaving), its goods
//   byte becomes 0xff (Good::Empty, an allocated pile without goods) and it
//   goes to the head of the area's list +0x8 (empty piles). EAX = pile +
//   0x14d2e2. ret 4.
// CGameData::CreatePile (0x00504420): (x, y, goods, amount, kind): takes the
//   first free pile record 1..0x7cfe (goods byte 0; record 0x7cff counts as
//   "none free": EAX = 0 then, nothing changes), raises the high-water mark
//   (dword world+0x1238858) if needed, fills goods, amount, kind, x, y with
//   leaving/coming 0, and puts the pile number into the tile's word +0x2.
//   Then it links the pile: with amount 0 and kind <= 0x32 at the head of
//   its area's list for its goods type (the empty-pile list +0x8 for goods
//   0xff); with amount != 0 and kind <= 0x32, 0xfe or 0xff at the head of
//   the goods list, unless the tile's area word is 0 or >= 0x8000 (then no
//   links); other kinds get no links (previous and next 0). Goods, amount
//   and kind are compared as full dwords, the goods index too. EAX = the
//   pile (callers keep AX). ret 0x14.
// thiscall (ECX = world).


#include "game/debug_log.h"
#include "game/enums.h"
#include "game/replacements.h"
#include "game/texts.h"
#include "game/typed_fields.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <vector>

namespace s3re::game {

using layout::Area;
using layout::Building;
using layout::Pile;
using layout::Tile;
using layout::kTileHasBuilding;

namespace {

// The original computes the area's list head as (area * 0x170 + goods) * 2
// bytes into the area table, and returns area * 0x170 in EAX.
constexpr std::uint32_t kAreaWords = sizeof(Area) / 2;

// RemovePileFromTile and MakePileEmpty return pile + 0x14d2e2: the pile
// table's offset in records (the original's index arithmetic).
static_assert(offsetof(CGameData, piles) % sizeof(Pile) == 0);
constexpr std::uint32_t kPileIndexBias = offsetof(CGameData, piles) / sizeof(Pile);

constexpr std::uint8_t kPileCapacity = 8;  // more than this on one pile is logged as an error

std::uint32_t Address(const void* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
}

// The tile a pile lies on (its x/y words, so always a real tile index).
Tile& TileOf(CGameData* world, const Pile& pile) {
    return world->tiles[pile.x][pile.y];
}

// Output and dropped piles are the ones that leave their goods list while
// every unit on them is spoken for.
bool LeavesListWhenReserved(const Pile& pile) {
    return KindOf(pile) == PileKind::Output || KindOf(pile) == PileKind::Dropped;
}

// Puts `pile` at the head of the list whose head word is `head`, in the
// original's order of writes (next, the old head's previous, previous, head).
void PushFront(CGameData* world, std::uint32_t pile, std::uint16_t& head) {
    Pile& p = world->piles[pile];
    const std::uint32_t next = head;
    p.next = static_cast<std::uint16_t>(next);
    if (next != 0) {
        world->piles[next].prev = static_cast<std::uint16_t>(pile);
    }
    p.prev = 0;
    head = static_cast<std::uint16_t>(pile);
}

}  // namespace

using TileFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);

// Purpose: A goods pile lying on a tile becomes part of the economy: its goods count towards the tile
// owner's stock and the pile is listed in its area, so carriers can find it.
std::uint32_t __fastcall CGameData_AddTilePileToArea(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t index = x * static_cast<std::uint32_t>(layout::kMapRowTiles) + y;
    // x, y are full dwords: the tile offset is computed in 32 bits as in the original.
    Tile& tile = TileAtOffset(world, TileOffset(x, y));
    const std::uint32_t pile = tile.object_number;
    if (pile == 0 || (tile.flags & kTileHasBuilding) != 0) {
        return Address(world) + index * kTileBytes;
    }
    const std::uint32_t area = tile.area;
    GameDebugLog(texts::kPileTaken, pile, area);
    const Pile& p = world->piles[pile];
    CGameData_AddPlayerCounter(world, nullptr, tile.owner, p.good, p.amount);
    // The original reads the goods byte again for the head's second access;
    // nothing in between can change it, so one reference serves both.
    PushFront(world, pile, world->areas[area].piles_by_goods[p.good]);
    return area * kAreaWords;
}

S3RE_HOOK(0x005051b0, CGameData_AddTilePileToArea, TileFn, Original_CGameData_AddTilePileToArea,
          CGameData_AddTilePileToArea);

using PileFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t pile);

// Purpose: One more unit of goods arrives on a pile. If the pile has become available for collection, it is
// listed in its area so carriers can find it.
std::uint32_t __fastcall CGameData_AddGoodsToPile(CGameData* world, void*, std::uint32_t pile) {
    Pile& p = world->piles[pile];
    auto log = [&](std::uint32_t kind) {
        GameDebugLog(texts::kPileDetails, pile, kind, static_cast<std::uint32_t>(p.good), static_cast<std::uint32_t>(p.x),
                     static_cast<std::uint32_t>(p.y));
    };
    // The original's EAX, step by step (see the doc comment).
    std::uint32_t eax = ((pile * 7u) & 0xffffff00u) | p.art;
    if (KindOf(p) == PileKind::Storehouse && p.coming == 0) {
        eax = pile + kPileIndexBias;
        log(Value(PileKind::Storehouse));
    }
    if (KindOf(p) == PileKind::Dropped) {
        eax = p.good;
        log(Value(PileKind::Dropped));
    }
    if (KindOf(p) == PileKind::Output) {
        eax = (eax & 0xffffff00u) | p.leaving;
        if (p.amount == p.leaving) {
            // Everything was spoken for, so the pile was out of its list:
            // with the new unit it is listed again.
            const std::uint32_t area = TileOf(world, p).area;
            PushFront(world, pile, world->areas[area].piles_by_goods[p.good]);
            eax = area * kAreaWords;
        }
    }
    p.coming = static_cast<std::uint8_t>(p.coming - 1);
    p.amount = static_cast<std::uint8_t>(p.amount + 1);
    eax = (eax & 0xffffff00u) | p.amount;
    if (p.amount > kPileCapacity) {
        eax = p.art;
        log(p.art);
    }
    return eax;
}

S3RE_HOOK(0x00505650, CGameData_AddGoodsToPile, PileFn, Original_CGameData_AddGoodsToPile, CGameData_AddGoodsToPile);

namespace {

// 0x5058c0..: PileIncAmountLeaving's error report when a pile already has
// a full pile's worth of units leaving: the pile, and the first building
// that refers to it. Returns the original's EAX after the report.
std::uint32_t LogOverfullLeaving(CGameData* world, std::uint32_t pile) {
    const Pile& p = world->piles[pile];
    GameDebugLog(texts::kPileIncAmountLeaving, static_cast<std::uint32_t>(p.art), pile,
                 static_cast<std::uint32_t>(p.amount), static_cast<std::uint32_t>(p.good),
                 static_cast<std::uint32_t>(p.leaving), static_cast<std::uint32_t>(p.x),
                 static_cast<std::uint32_t>(p.y));
    std::uint32_t eax = p.amount;
    const std::uint32_t count = world->building_high_water;
    if (count < 1) {
        return eax;
    }
    // The building index is kept in 16 bits (compared with the count after
    // `& 0xffff`), as in the original.
    std::uint32_t i = 1;
    for (;;) {
        const Building& building = world->buildings[i];
        if (building.piles[0] == pile || building.piles[1] == pile || building.piles[2] == pile ||
            building.pile_ring == pile) {
            const Building& found = world->buildings[i & 0xffffu];
            GameDebugLog(texts::kPileIncAmountLeavingBuilding, static_cast<std::uint32_t>(found.art),
                         static_cast<std::uint32_t>(found.buildup));
            return Address(world) + (i & 0xffffu) * static_cast<std::uint32_t>(sizeof(Building));
        }
        ++i;
        eax = i & 0xffffu;
        if (eax > count) {
            return eax;
        }
    }
}

}  // namespace

// Purpose: A carrier is coming to take one unit from a pile. Once every unit on the pile is spoken for, the
// pile is taken out of its area's list, so no other carrier is sent for it.
std::uint32_t __fastcall CGameData_PileIncAmountLeaving(CGameData* world, void*, std::uint32_t pile) {
    Pile& p = world->piles[pile];
    // The original's EAX, step by step.
    std::uint32_t eax = ((pile * 7u) & 0xffffff00u) | p.leaving;
    if (p.leaving >= kPileCapacity) {
        eax = LogOverfullLeaving(world, pile);
    }
    p.leaving = static_cast<std::uint8_t>(p.leaving + 1);
    eax = (eax & 0xffffff00u) | p.leaving;
    if (p.amount != p.leaving) {
        return eax;
    }
    eax = (eax & 0xffffff00u) | p.art;
    if (!LeavesListWhenReserved(p)) {
        return eax;
    }
    // Every unit is spoken for: unlink the pile from its goods list (its
    // own links stay).
    const std::uint32_t area = TileOf(world, p).area;
    const std::uint32_t previous = p.prev;
    const std::uint32_t next = p.next;
    if (previous != 0) {
        world->piles[previous].next = static_cast<std::uint16_t>(next);
        eax = ((previous * 7u) & 0xffff0000u) | next;
    } else {
        world->areas[area].piles_by_goods[p.good] = static_cast<std::uint16_t>(next);
        eax = next;
    }
    if (next != 0) {
        world->piles[next].prev = static_cast<std::uint16_t>(previous);
        eax = next;
    }
    return eax;
}

S3RE_HOOK(0x005058c0, CGameData_PileIncAmountLeaving, PileFn, Original_CGameData_PileIncAmountLeaving,
          CGameData_PileIncAmountLeaving);

// Purpose: One more unit of goods is on its way to a pile (counted in byte +0x3).
std::uint32_t __fastcall CGameData_PileIncAmountComing(CGameData* world, void*, std::uint32_t pile) {
    std::uint8_t& coming = world->piles[pile].coming;
    coming = static_cast<std::uint8_t>(coming + 1);
    return Address(&coming);
}

// Purpose: One unit less is on its way to a pile (probably when a delivery is cancelled).
std::uint32_t __fastcall CGameData_PileDecAmountComing(CGameData* world, void*, std::uint32_t pile) {
    std::uint8_t& coming = world->piles[pile].coming;
    coming = static_cast<std::uint8_t>(coming - 1);
    return Address(&coming);
}

// Purpose: Removes a goods pile from the map: its tile no longer holds it and the record's goods byte is
// cleared (probably freeing the record).
std::uint32_t __fastcall CGameData_RemovePileFromTile(CGameData* world, void*, std::uint32_t pile) {
    TileOf(world, world->piles[pile]).object_number = 0;
    world->piles[pile].good = Value(Good::Nothing);  // a free pile record
    return pile + kPileIndexBias;  // the original's index arithmetic
}

S3RE_HOOK(0x00505bc0, CGameData_PileIncAmountComing, PileFn, Original_CGameData_PileIncAmountComing,
          CGameData_PileIncAmountComing);
S3RE_HOOK(0x00505bf0, CGameData_PileDecAmountComing, PileFn, Original_CGameData_PileDecAmountComing,
          CGameData_PileDecAmountComing);
S3RE_HOOK(0x00504650, CGameData_RemovePileFromTile, PileFn, Original_CGameData_RemovePileFromTile,
          CGameData_RemovePileFromTile);

// Purpose: A carrier that was to take a unit from a pile won't (or has taken it): one unit less is leaving.
// If the pile had been taken out of its area's list because everything on it was spoken for, it is listed
// again, so carriers can be sent for it.
std::uint32_t __fastcall CGameData_PileDecAmountLeaving(CGameData* world, void*, std::uint32_t pile) {
    Pile& p = world->piles[pile];
    std::uint32_t upper = pile * 7u;  // the original's register before the byte loads
    if (p.amount == p.leaving && LeavesListWhenReserved(p)) {
        const std::uint32_t area = TileOf(world, p).area;
        upper = area * kAreaWords;
        PushFront(world, pile, world->areas[area].piles_by_goods[p.good]);
    }
    p.leaving = static_cast<std::uint8_t>(p.leaving - 1);
    return (upper & 0xffffff00u) | p.leaving;
}

// Purpose: A pile has no goods left (probably a building's pile slot that was emptied): it leaves its
// area's list for its goods type and is kept, marked empty, in the area's list of empty piles.
std::uint32_t __fastcall CGameData_MakePileEmpty(CGameData* world, void*, std::uint32_t pile) {
    Pile& p = world->piles[pile];
    const std::uint32_t area = TileOf(world, p).area;
    // Unlink from the goods list; p.next is read again after the first
    // write, as in the original.
    const std::uint32_t previous = p.prev;
    if (previous != 0) {
        world->piles[previous].next = p.next;
    } else {
        world->areas[area].piles_by_goods[p.good] = p.next;
    }
    const std::uint32_t next = p.next;
    if (next != 0) {
        world->piles[next].prev = p.prev;
    }
    // Push onto the empty list; the original writes the goods byte between
    // the previous link and the head.
    std::uint16_t& head = world->areas[area].pile_list;
    p.next = head;
    if (head != 0) {
        world->piles[head].prev = static_cast<std::uint16_t>(pile);
    }
    p.prev = 0;
    p.good = Value(Good::Empty);
    head = static_cast<std::uint16_t>(pile);
    return pile + kPileIndexBias;  // the original's index arithmetic
}

S3RE_HOOK(0x00505ac0, CGameData_PileDecAmountLeaving, PileFn, Original_CGameData_PileDecAmountLeaving,
          CGameData_PileDecAmountLeaving);
S3RE_HOOK(0x00506080, CGameData_MakePileEmpty, PileFn, Original_CGameData_MakePileEmpty, CGameData_MakePileEmpty);

using CreatePileFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                std::uint32_t goods, std::uint32_t amount, std::uint32_t kind);
std::uint32_t __fastcall CGameData_CreatePile(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y,
                                              std::uint32_t goods, std::uint32_t amount, std::uint32_t kind);

S3RE_HOOK(0x00504420, CGameData_CreatePile, CreatePileFn, Original_CGameData_CreatePile, CGameData_CreatePile);

namespace {

// Pile records 1..31998 can be used; reaching 31999 (0x7cff) means none is free.
constexpr std::uint32_t kPileLimit = 31999;

// Puts `pile` at the head of the list whose head word is `head` (CreatePile's
// order of writes: the head before the previous link).
void LinkAtHead(CGameData* world, std::uint32_t pile, std::uint16_t& head) {
    const std::uint16_t first = head;
    world->piles[pile].next = first;
    if (first != 0) {
        world->piles[first].prev = static_cast<std::uint16_t>(pile);
    }
    head = static_cast<std::uint16_t>(pile);
    world->piles[pile].prev = 0;
}

// CreatePile's kinds (a full dword, compared as such).
bool IsStorageKind(std::uint32_t kind) {
    return kind <= kLastStoragePileKind;
}

}  // namespace

// Purpose: Puts a new goods pile on a tile (probably when goods are dropped
// or produced) and adds it to its area's pile list for its goods type.
std::uint32_t __fastcall CGameData_CreatePile(CGameData* world, void* /*edx*/, std::uint32_t x, std::uint32_t y,
                                              std::uint32_t goods, std::uint32_t amount, std::uint32_t kind) {
    std::uint32_t pile = 1;
    while (world->piles[pile].good != Value(Good::Nothing) && pile != kPileLimit) {
        ++pile;
    }
    if (pile == kPileLimit) {
        return 0;
    }
    if (pile > world->pile_high_water) {
        world->pile_high_water = pile;
    }
    Pile& record = world->piles[pile];
    record.good = static_cast<std::uint8_t>(goods);
    record.amount = static_cast<std::uint8_t>(amount);
    record.leaving = 0;
    record.coming = 0;
    record.art = static_cast<std::uint8_t>(kind);
    record.x = static_cast<std::uint16_t>(x);
    record.y = static_cast<std::uint16_t>(y);
    // x, y are full dwords: the tile offset is computed in 32 bits as in the original.
    Tile& tile = TileAtOffset(world, TileOffset(x, y));
    tile.object_number = static_cast<std::uint16_t>(pile);
    const std::uint32_t area = tile.area;
    // The goods index is the full dword argument (the 32-bit address
    // arithmetic of the indexing matches the original's).
    std::uint16_t& goodsHead = world->areas[area].piles_by_goods[goods];
    if (amount == 0) {
        if (IsStorageKind(kind)) {
            if (goods != Value(Good::Empty)) {
                LinkAtHead(world, pile, goodsHead);
            } else {
                // Same links; the original clears the previous link before
                // writing the head here (different words, same result).
                LinkAtHead(world, pile, world->areas[area].pile_list);
            }
            return pile;
        }
    } else if (IsStorageKind(kind) || kind == Value(PileKind::Dropped) || kind == Value(PileKind::Output)) {
        // Piles in water or in area 0 get no links.
        if (area != 0 && area < layout::kTileAreaWater) {
            LinkAtHead(world, pile, goodsHead);
        } else {
            record.next = 0;
            record.prev = 0;
        }
        return pile;
    }
    record.next = 0;
    record.prev = 0;
    return pile;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestPiles = 0x1000;
constexpr std::uint32_t kTestAreas = 0x400;
constexpr std::size_t kTestWorld = offsetof(CGameData, piles) + (kTestPiles + 1) * sizeof(Pile);

std::uint32_t Relative(std::uint32_t value, CGameData* world, std::uint32_t areaWords) {
    // Either a pointer into the world or area * 0x170.
    return value == areaWords ? value : value - Address(world);
}

}  // namespace

S3RE_TEST(CGameData_AddTilePileToArea_MatchesOriginal) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < 20000; ++i) {
        const std::uint32_t x = ctx.rng().Below(384);
        const std::uint32_t y = ctx.rng().Below(384);
        const std::uint32_t pile = ctx.rng().Chance(1, 8) ? 0 : 1 + ctx.rng().Below(kTestPiles);
        const auto flags = static_cast<std::uint8_t>(ctx.rng().Next32() & (ctx.rng().Chance(1, 6) ? 0xff : ~0x20));
        const std::uint32_t area = ctx.rng().Below(kTestAreas);
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Chance(1, 8) ? 0xff : ctx.rng().Below(20));
        worlds.Both([&](CGameData* w) {
            Tile& tile = w->tiles[x][y];
            tile.object_number = static_cast<std::uint16_t>(pile);
            tile.flags = flags;
            tile.area = static_cast<std::uint16_t>(area);
            tile.owner = owner;
        });
        if (pile != 0) {
            const auto goods = static_cast<std::uint8_t>(ctx.rng().Below(36));
            // The current head: often empty, else another pile.
            const auto head = static_cast<std::uint16_t>(ctx.rng().Chance(1, 3) ? 0 : 1 + ctx.rng().Below(kTestPiles));
            worlds.Both([&](CGameData* w) {
                w->piles[pile].good = goods;
                w->areas[area].piles_by_goods[goods] = head;
            });
        }
        const std::uint32_t areaWords = area * kAreaWords;
        const std::uint32_t expected =
            Relative(Original_CGameData_AddTilePileToArea(worlds.original(), nullptr, x, y), worlds.original(), areaWords);
        const std::uint32_t actual = Relative(CGameData_AddTilePileToArea(worlds.ours(), nullptr, x, y), worlds.ours(), areaWords);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 256 == 255) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 255 before it): (%u, %u), pile %u", i, x, y, pile);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

S3RE_TEST(CGameData_AddGoodsToPile_MatchesOriginal) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < 20000; ++i) {
        const std::uint32_t pile = 1 + ctx.rng().Below(kTestPiles);
        static constexpr std::uint8_t kKinds[] = {0, 0xfe, 0xff, 0xff, 1, 7};
        const std::uint8_t kind = kKinds[ctx.rng().Below(sizeof(kKinds))];
        const std::uint8_t amount = static_cast<std::uint8_t>(ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(10));
        const auto leaving = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? amount : ctx.rng().Below(10));
        const auto coming = static_cast<std::uint8_t>(ctx.rng().Below(3));
        const auto goods = static_cast<std::uint8_t>(ctx.rng().Below(36));
        const std::uint32_t x = ctx.rng().Below(384);
        const std::uint32_t y = ctx.rng().Below(384);
        const std::uint32_t area = ctx.rng().Below(kTestAreas);
        const auto head = static_cast<std::uint16_t>(ctx.rng().Chance(1, 3) ? 0 : 1 + ctx.rng().Below(kTestPiles));
        worlds.Both([&](CGameData* w) {
            Pile& p = w->piles[pile];
            p.art = kind;
            p.amount = amount;
            p.leaving = leaving;
            p.coming = coming;
            p.good = goods;
            p.x = static_cast<std::uint16_t>(x);
            p.y = static_cast<std::uint16_t>(y);
            w->tiles[x][y].area = static_cast<std::uint16_t>(area);
            w->areas[area].piles_by_goods[goods] = head;
        });
        const std::uint32_t expected = Original_CGameData_AddGoodsToPile(worlds.original(), nullptr, pile);
        const std::uint32_t actual = CGameData_AddGoodsToPile(worlds.ours(), nullptr, pile);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 256 == 255) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 255 before it): pile %u", i, pile);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

namespace {

constexpr std::uint32_t kTestBuildings = 0x200;
constexpr std::size_t kLeavingTestWorld = offsetof(CGameData, buildings) + (kTestBuildings + 1) * sizeof(Building);

using PileTestFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t pile);

// Wrong: unlinks piles of every kind.
std::uint32_t __fastcall WrongPileIncAmountLeaving(CGameData* world, void* edx, std::uint32_t pile) {
    const std::uint8_t kind = world->piles[pile].art;
    world->piles[pile].art = 0xff;
    const std::uint32_t result = CGameData_PileIncAmountLeaving(world, edx, pile);
    world->piles[pile].art = kind;
    return result;
}

void ComparePileLeaving(test::Context& ctx, PileTestFn candidate, int cases) {
    test::WorldPair worlds(ctx, kLeavingTestWorld);
    DebugLogRecorder logs;
    std::size_t logged = 0;
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t pile = 1 + ctx.rng().Below(kTestPiles);
        static constexpr std::uint8_t kKinds[] = {0xff, 0xfe, 0xff, 0, 1, 7};
        const std::uint8_t kind = kKinds[ctx.rng().Below(sizeof(kKinds))];
        const std::uint8_t leaving = static_cast<std::uint8_t>(ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(12));
        const auto amount = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? leaving + 1 : ctx.rng().Below(12));
        const auto goods = static_cast<std::uint8_t>(ctx.rng().Below(36));
        const std::uint32_t x = ctx.rng().Below(384);
        const std::uint32_t y = ctx.rng().Below(384);
        const std::uint32_t area = ctx.rng().Below(kTestAreas);
        const auto previous = static_cast<std::uint16_t>(ctx.rng().Chance(1, 3) ? 0 : 1 + ctx.rng().Below(kTestPiles));
        const auto next = static_cast<std::uint16_t>(ctx.rng().Chance(1, 3) ? 0 : 1 + ctx.rng().Below(kTestPiles));
        worlds.Both([&](CGameData* w) {
            Pile& p = w->piles[pile];
            p.art = kind;
            p.leaving = leaving;
            p.amount = amount;
            p.good = goods;
            p.x = static_cast<std::uint16_t>(x);
            p.y = static_cast<std::uint16_t>(y);
            w->tiles[x][y].area = static_cast<std::uint16_t>(area);
            p.prev = previous;
            p.next = next;
        });
        // Buildings: a small count; sometimes one of them refers to the pile
        // (in one of its words piles[0..2], pile_ring).
        const std::uint32_t count = ctx.rng().Chance(1, 8) ? 0 : ctx.rng().Below(kTestBuildings);
        worlds.Both([&](CGameData* w) { w->building_high_water = count; });
        if (count != 0 && ctx.rng().Chance(1, 2)) {
            const std::uint32_t b = 1 + ctx.rng().Below(count);
            const std::uint32_t which = ctx.rng().Below(4);
            worlds.Both([&](CGameData* w) {
                Building& building = w->buildings[b];
                std::uint16_t& word = which < 3 ? building.piles[which] : building.pile_ring;
                word = static_cast<std::uint16_t>(pile);
            });
        }
        const std::uint32_t expected = Original_CGameData_PileIncAmountLeaving(worlds.original(), nullptr, pile);
        const std::vector<DebugLogCall> expectedLogs = logs.Take();
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, pile);
        const std::vector<DebugLogCall> actualLogs = logs.Take();
        logged += expectedLogs.size();
        // EAX can be world + building * 0x3a: compare that relative to the world.
        const std::uint32_t e = expected - Address(worlds.original());
        const std::uint32_t a = actual - Address(worlds.ours());
        const bool pointer = e < (kTestBuildings + 1) * sizeof(Building);
        if (!(pointer ? S3RE_EXPECT_EQ(e, a) : S3RE_EXPECT_EQ(expected, actual)) ||
            !S3RE_EXPECT(expectedLogs == actualLogs) || ((i % 256 == 255) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 255 before it): pile %u", i, pile);
            return;
        }
    }
    worlds.Checkpoint(ctx);
    S3RE_EXPECT(logged > 0);  // the capture works
}

}  // namespace

S3RE_TEST(CGameData_PileIncAmountLeaving_MatchesOriginal) {
    ComparePileLeaving(ctx, &CGameData_PileIncAmountLeaving, 20000);
}
S3RE_TEST(CGameData_PileIncAmountLeaving_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePileLeaving(probe, &WrongPileIncAmountLeaving, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

// Compares a pile function on random piles lying on tiles of the map: EAX
// (relative to the world when it points into it) and the whole world.
void ComparePileFn(test::Context& ctx, PileTestFn original, PileTestFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t pile = ctx.rng().Below(kTestPiles + 1);
        const auto x = static_cast<std::uint16_t>(ctx.rng().Below(384));
        const auto y = static_cast<std::uint16_t>(ctx.rng().Below(384));
        worlds.Both([&](CGameData* w) {
            w->piles[pile].x = x;
            w->piles[pile].y = y;
        });
        if (ctx.rng().Chance(1, 8)) {
            const auto coming = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0 : 0xff);
            worlds.Both([&](CGameData* w) { w->piles[pile].coming = coming; });
        }
        std::uint32_t expected = original(worlds.original(), nullptr, pile);
        std::uint32_t actual = candidate(worlds.ours(), nullptr, pile);
        if (expected - Address(worlds.original()) < kTestWorld) {
            expected -= Address(worlds.original());
            actual -= Address(worlds.ours());
        }
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 256 == 255 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 255 before it): pile %u", i, pile);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(CGameData_PileIncAmountComing_MatchesOriginal) {
    ComparePileFn(ctx, Original_CGameData_PileIncAmountComing, &CGameData_PileIncAmountComing, 20000);
}
S3RE_TEST(CGameData_PileIncAmountComing_ComparisonDetectsWrongImplementation) {
    // Swapping increment and decrement must be caught.
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePileFn(probe, Original_CGameData_PileIncAmountComing, &CGameData_PileDecAmountComing, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_PileDecAmountComing_MatchesOriginal) {
    ComparePileFn(ctx, Original_CGameData_PileDecAmountComing, &CGameData_PileDecAmountComing, 20000);
}
S3RE_TEST(CGameData_RemovePileFromTile_MatchesOriginal) {
    ComparePileFn(ctx, Original_CGameData_RemovePileFromTile, &CGameData_RemovePileFromTile, 20000);
}

namespace {

// Random linked piles for the relink functions: the pile on a tile of the
// map with an area word below kTestAreas, its goods type, amount, leaving
// count and kind often in the relinking state; links and list heads often
// 0, else other piles (they need not form consistent lists: the functions
// only follow one link each).
void SetUpLinkedPile(test::Context& ctx, test::WorldPair& worlds, std::uint32_t pile) {
    static constexpr std::uint8_t kKinds[] = {0xff, 0xfe, 0xff, 0xfe, 0, 1, 0xfd};
    const std::uint8_t kind = kKinds[ctx.rng().Below(sizeof(kKinds))];
    const std::uint8_t leaving = static_cast<std::uint8_t>(ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(9));
    const auto amount = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? leaving : ctx.rng().Below(9));
    const std::uint8_t goods = static_cast<std::uint8_t>(ctx.rng().Chance(1, 10) ? ctx.rng().Next32() : ctx.rng().Below(36));
    const std::uint32_t x = ctx.rng().Below(384);
    const std::uint32_t y = ctx.rng().Below(384);
    const std::uint32_t area = ctx.rng().Below(kTestAreas);
    auto link = [&]() { return static_cast<std::uint16_t>(ctx.rng().Chance(1, 3) ? 0 : 1 + ctx.rng().Below(kTestPiles)); };
    const std::uint16_t previous = link();
    const std::uint16_t next = link();
    const std::uint16_t goodsHead = link();
    const std::uint16_t emptyHead = link();
    worlds.Both([&](CGameData* w) {
        Pile& p = w->piles[pile];
        p.art = kind;
        p.leaving = leaving;
        p.amount = amount;
        p.good = goods;
        p.x = static_cast<std::uint16_t>(x);
        p.y = static_cast<std::uint16_t>(y);
        w->tiles[x][y].area = static_cast<std::uint16_t>(area);
        p.prev = previous;
        p.next = next;
        // goods may be >= 36: the head then lies in later fields of the
        // area record (or the next record), as in the original.
        w->areas[area].piles_by_goods[goods] = goodsHead;
        w->areas[area].pile_list = emptyHead;
    });
}

// Compares a relink function: EAX exactly (it is never a pointer) and the
// whole world every 256 cases.
void CompareLinkedPileFn(test::Context& ctx, PileTestFn original, PileTestFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t pile = 1 + ctx.rng().Below(kTestPiles);
        SetUpLinkedPile(ctx, worlds, pile);
        const std::uint32_t expected = original(worlds.original(), nullptr, pile);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, pile);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 256 == 255 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 255 before it): pile %u", i, pile);
            return;
        }
    }
}

// Wrong: relinks piles of every kind.
std::uint32_t __fastcall WrongPileDecAmountLeaving(CGameData* world, void* edx, std::uint32_t pile) {
    std::uint8_t& kind = world->piles[pile].art;
    const std::uint8_t saved = kind;
    kind = 0xff;
    const std::uint32_t result = CGameData_PileDecAmountLeaving(world, edx, pile);
    kind = saved;
    return result;
}

// Wrong: does not set the previous link of the old head of the empty list.
std::uint32_t __fastcall WrongMakePileEmpty(CGameData* world, void* edx, std::uint32_t pile) {
    const std::uint32_t area = TileOf(world, world->piles[pile]).area;
    const std::uint32_t head = world->areas[area].pile_list;
    const std::uint16_t saved = world->piles[head].prev;
    const std::uint32_t result = CGameData_MakePileEmpty(world, edx, pile);
    if (head != pile) {
        world->piles[head].prev = saved;
    }
    return result;
}

}  // namespace

S3RE_TEST(CGameData_PileDecAmountLeaving_MatchesOriginal) {
    CompareLinkedPileFn(ctx, Original_CGameData_PileDecAmountLeaving, &CGameData_PileDecAmountLeaving, 20000);
}
S3RE_TEST(CGameData_PileDecAmountLeaving_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareLinkedPileFn(probe, Original_CGameData_PileDecAmountLeaving, &WrongPileDecAmountLeaving, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_MakePileEmpty_MatchesOriginal) {
    CompareLinkedPileFn(ctx, Original_CGameData_MakePileEmpty, &CGameData_MakePileEmpty, 20000);
}
S3RE_TEST(CGameData_MakePileEmpty_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareLinkedPileFn(probe, Original_CGameData_MakePileEmpty, &WrongMakePileEmpty, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

// The pile records searched by CreatePile (1..0x7cff) must exist in the test
// world; all of them are kept in use (goods byte != 0) except the one a case
// frees, so the search ends there (or finds none).
constexpr std::size_t kCreateWorld = offsetof(CGameData, piles) + (kPileLimit + 1) * sizeof(Pile);

void CompareCreatePile(test::Context& ctx, CreatePileFn candidate, int cases) {
    test::WorldPair worlds(ctx, kCreateWorld);
    for (std::uint32_t pile = 1; pile <= kPileLimit; ++pile) {
        const auto goods = static_cast<std::uint8_t>(1 + ctx.rng().Below(255));
        worlds.Both([&](CGameData* w) { w->piles[pile].good = goods; });
    }
    static constexpr std::uint32_t kKinds[] = {0, 1, 0x32, 0x33, 0xfd, 0xfe, 0xff, 0x100, 7};
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t free = ctx.rng().Chance(1, 20) ? 0 : ctx.rng().Chance(1, 20) ? kPileLimit
                                                                                         : 1 + ctx.rng().Below(kPileLimit);
        if (free != 0) {
            worlds.Both([&](CGameData* w) { w->piles[free].good = 0; });
        }
        const std::uint32_t highWater = ctx.rng().Below(0x8000);
        worlds.Both([&](CGameData* w) { w->pile_high_water = highWater; });
        const std::uint32_t x = ctx.rng().Below(384);
        const std::uint32_t y = ctx.rng().Below(384);
        const std::uint32_t amount = ctx.rng().Chance(1, 2) ? 0 : (ctx.rng().Chance(1, 8) ? 0x100 : 1 + ctx.rng().Below(8));
        // Areas >= 0x8000 only with goods (otherwise the original would link
        // the pile into a list far outside the test world).
        const std::uint32_t area = ctx.rng().Chance(1, 10)
                                       ? (amount == 0 || ctx.rng().Chance(1, 2) ? 0 : 0x8000 + ctx.rng().Below(0x8000))
                                       : ctx.rng().Below(kTestAreas);
        worlds.Both([&](CGameData* w) { w->tiles[x][y].area = static_cast<std::uint16_t>(area); });
        const std::uint32_t goods = ctx.rng().Chance(1, 5) ? 0xff : ctx.rng().Below(36);
        const std::uint32_t kind = kKinds[ctx.rng().Below(sizeof(kKinds) / sizeof(kKinds[0]))];
        // The heads the pile may go to: empty or another pile.
        auto link = [&]() { return static_cast<std::uint16_t>(ctx.rng().Chance(1, 3) ? 0 : 1 + ctx.rng().Below(kPileLimit)); };
        if (area < kTestAreas) {
            const std::uint16_t goodsHead = link();
            const std::uint16_t emptyHead = link();
            worlds.Both([&](CGameData* w) {
                w->areas[area].piles_by_goods[goods] = goodsHead;  // goods 0xff: past the array, as in the original
                w->areas[area].pile_list = emptyHead;
            });
        }
        const std::uint32_t expected = Original_CGameData_CreatePile(worlds.original(), nullptr, x, y, goods, amount, kind);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, x, y, goods, amount, kind);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 64 == 63 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 63 before it): free %u, goods %u, amount %u, kind %u, area %u",
                     i, free, goods, amount, kind, area);
            return;
        }
        // Keep every record in use again.
        if (free != 0) {
            worlds.Both([&](CGameData* w) { w->piles[free].good = 1; });
        }
    }
}

// Wrong: the high-water mark is never raised.
std::uint32_t __fastcall WrongCreatePile(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y,
                                         std::uint32_t goods, std::uint32_t amount, std::uint32_t kind) {
    const std::uint32_t saved = world->pile_high_water;
    const std::uint32_t result = CGameData_CreatePile(world, edx, x, y, goods, amount, kind);
    world->pile_high_water = saved;
    return result;
}

}  // namespace

S3RE_TEST(CGameData_CreatePile_MatchesOriginal) {
    CompareCreatePile(ctx, &CGameData_CreatePile, 6000);
}
S3RE_TEST(CGameData_CreatePile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareCreatePile(probe, &WrongCreatePile, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
