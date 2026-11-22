// Small tables, rings and timers in the world object (all names provisional:
// what the entries mean is not known yet).
//
// GameWorld::AllocBuildingExtra (0x004df0a0): (value) takes the first free
//   record 1..998 of the 0x5a-byte records at world+0x10f45e8 (free: byte
//   +0x0 == 0xff), raises the high-water mark (uint32 world+0x110a578) and
//   initializes it: +0x0 = value (low byte), +0x1 = 1, uint16 +0x2..+0x10 =
//   0xffff, bytes +0x12..+0x59 = 0. Returns its number, 0 if none is free
//   (record 999 is never used). Only 0x4dbc50 calls it and stores the
//   number at building +0x34. ret 4.
// GameWorld::AllocTable36700Record (0x004fefd0): (ten uint16 values) takes
//   the first free record 1..9998 of the 20-byte records at world+0x36700
//   (free: uint16 +0x0 == 0), raises the high-water mark (uint32 at
//   world+0x67440, right before the tile records) and stores the ten values.
//   EAX = world + 20 * record, or world + 0x36700 + 20 * 9999 when full.
//   ret 0x28.
// GameWorld::QueueEvent (0x004e8ad0): (type, a, b, c) appends {type, a, b,
//   c} (int32 each) to the 1000-entry ring at world+0x32878 (write index
//   world+0x366fc). For type 6 it first scans the 12-byte ring at
//   world+0x2f990 (read/write indices world+0x32870/+0x32874) and appends
//   nothing if it holds an entry {6, a, ..}. EAX = the new write index
//   before wrapping (1..1000), or, when skipped, the index after the
//   duplicate (wrapped; one caller passes EAX on). ret 0x10.
// GameWorld::QueuePendingEvent (0x004e8ba0): (type, a, b) appends {type,
//   a, b} (int32 each) to the 12-byte ring at world+0x2f990 (the ring that
//   QueueEvent checks; read/write indices world+0x32870/+0x32874; no
//   full check). For type 4 it first scans the ring from read to write and
//   appends nothing if it holds an entry {4, a, ..}. EAX as in QueueEvent:
//   the new write index before wrapping (1..1000), or, when skipped, the
//   index after the duplicate (wrapped). ret 0xc; callers ignore EAX.
// GameWorld::QueueMarkedTile (0x004e6c40): (x, y, player) if the tile has
//   flag 0x10 and not flag 0x1 in byte +0xa and is owned by `player` (byte
//   +0x8, compared as int32): clears flag 0x10 and appends (x, y) as uint16
//   pairs to the 10000-entry ring at world+0xdfdc48 (index world+0xe0788c).
//   ret 0xc; callers ignore EAX.
// GameWorld::QueueMarkedTileTerrain5To7 (0x0051ce50): (x, y) the same
//   without the owner check, but only if byte +0x6 (the terrain byte at
//   world+0x6744a) is 5, 6 or 7 and flag 0x80 of byte +0xa is clear (flag
//   0x1 is not tested). ret 8; EAX ignored (callers: 0x51cee0).
// GameWorld::QueueMarkedTileNoOwner (0x0051d3d0): (x, y) like
//   QueueMarkedTile without the owner check. ret 8; EAX ignored (callers:
//   0x51d450).
// GameWorld::CountDownTimers (0x004fee00): 42 lists of 10 entries of 12
//   bytes at world+0x2e5e0; an entry is active while its int32 +0x0 is not
//   0, and the active entries come first. Decrements every active entry;
//   an entry that reaches 0 is removed (the entries after it move up, the
//   last one is cleared). Called once per UpdateEconomy. Plain ret; EAX
//   ignored.
// GameWorld::AddPileClaim (0x00510770; was AddToRecentPositions): (settler,
//   pile) the 128 pile claims {pile, settler} at world+0x1195dd4
//   (layout::PileClaim). If the claim is not in the list, stores it in
//   the first empty slot (both words 0) searching downwards from the slot
//   before the cursor g_pileClaimsCursor (0x7ace60, wrapping from 0 to
//   127; loops forever if no slot is empty). Only caller: UpdateSettlers
//   job 0x152; CountOtherPileClaims 0x510800 reads the list. The logic thread clears the
//   cursor's slot and advances it every step (0x53de6f). ret 8; EAX ignored.
// GameWorld::ReplaceTileWord16AndQueue (0x0051c560): (x, y, expected,
//   value) if the uint16 at tile +0x16 (zero-extended) equals `expected`
//   (int32): sets it to `value` (low word) and appends (x, y) to the tile
//   ring (no flag change, no ring-full check). One step of the flood fill
//   0x51c6c0, which uses the tile ring as its queue. ret 0x10; EAX ignored.
// GameWorld::ReplaceTileByte10AndQueue (0x0051c820): the same for the byte
//   at tile +0x10 (`value` low byte). Step of the flood fill 0x51c8a0.
//   ret 0x10; EAX ignored.
// All thiscall (ECX = world).


#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

using layout::Effect;
using layout::Event;
using layout::NotificationTimer;
using layout::PendingEvent;
using layout::Tile;
using layout::Trade;

namespace {

constexpr std::uint32_t kBuildingExtraLimit = layout::kMaxTrade + 1;  // 999: never used
constexpr std::uint32_t kTable36700Limit = layout::kMaxEffect + 1;    // 9999: never used

constexpr std::uint32_t kRingEntries = 1000;
constexpr std::uint32_t kDedupedType = 6;
constexpr std::uint32_t kDedupedPendingType = 4;

constexpr std::uint32_t kTileRingEntries = 10000;

constexpr std::uint32_t kTimerLists = 42;
constexpr std::uint32_t kTimersPerList = 10;

constexpr std::uint32_t kPileClaimCount = 0x80;
constexpr std::uintptr_t kPileClaimsCursor = 0x7ace60;  // uint16, S3.EXE global

std::uint32_t Address(const void* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
}

std::uint16_t PileClaimsCursor() {
    return *reinterpret_cast<const volatile std::uint16_t*>(kPileClaimsCursor);
}

// The tile (x, y) for 32-bit coordinates: the offset is computed in 32 bits,
// as in the original (no range check).
Tile& TileAt(GameWorld* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

}  // namespace

using ValueFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t value);
using Alloc36700Fn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t a0,
                                                std::uint32_t a1, std::uint32_t a2,
                                                std::uint32_t a3, std::uint32_t a4,
                                                std::uint32_t a5, std::uint32_t a6,
                                                std::uint32_t a7, std::uint32_t a8,
                                                std::uint32_t a9);
using QueueEventFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t type,
                                                std::uint32_t a, std::uint32_t b, std::uint32_t c);
using QueuePendingEventFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t type,
                                                       std::uint32_t a, std::uint32_t b);
using QueueTileFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x,
                                               std::uint32_t y, std::uint32_t player);
using NoArgFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx);
using PositionFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x,
                                              std::uint32_t y);
using ReplaceTileFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x,
                                                 std::uint32_t y, std::uint32_t expected,
                                                 std::uint32_t value);

// Purpose: Finds a free record in a table of extra data that some buildings need (probably for production
// or storage details) and initialises it.
std::uint32_t __fastcall GameWorld_AllocBuildingExtra(GameWorld* world, void*,
                                                      std::uint32_t value) {
    std::uint32_t record = 1;
    while (world->trades[record].kind != 0xff) {
        if (record == kBuildingExtraLimit) {
            return 0;
        }
        ++record;
    }
    if (record == kBuildingExtraLimit) {
        return 0;
    }
    if (record > world->trade_high_water) {
        world->trade_high_water = record;
    }
    Trade& trade = world->trades[record];
    trade.kind = static_cast<std::uint8_t>(value);
    trade.mode = 1;
    // The words +0x2..+0x10 in address order: route_x[0..3], route_y[0..3].
    for (std::uint16_t& word : trade.route_x) {
        word = 0xffff;
    }
    for (std::uint16_t& word : trade.route_y) {
        word = 0xffff;
    }
    // Bytes +0x12..+0x59 (ordered and coming) in one go, as the original.
    std::memset(trade.ordered, 0, sizeof(Trade) - offsetof(Trade, ordered));
    return record;
}

// Purpose: Finds a free record in a table of small 20-byte records (purpose unknown) and stores ten values
// in it.
std::uint32_t __fastcall GameWorld_AllocTable36700Record(GameWorld* world, void*, std::uint32_t a0,
                                                         std::uint32_t a1, std::uint32_t a2,
                                                         std::uint32_t a3, std::uint32_t a4,
                                                         std::uint32_t a5, std::uint32_t a6,
                                                         std::uint32_t a7, std::uint32_t a8,
                                                         std::uint32_t a9) {
    std::uint32_t record = 1;
    while (world->effects[record].type != 0 && record != kTable36700Limit) {
        ++record;
    }
    if (record == kTable36700Limit) {
        return Address(&world->effects[kTable36700Limit]);
    }
    if (record > world->effects_high_water) {
        world->effects_high_water = record;
    }
    Effect& effect = world->effects[record];
    effect.type = static_cast<std::uint16_t>(a0);
    effect.countdown = static_cast<std::uint16_t>(a1);
    const std::uint32_t params[] = {a2, a3, a4, a5, a6, a7, a8, a9};
    for (std::uint32_t i = 0; i < 8; ++i) {
        effect.params[i] = static_cast<std::uint16_t>(params[i]);
    }
    // world + 20 * record (not the record's address: the original's index arithmetic).
    return Address(world) + record * static_cast<std::uint32_t>(sizeof(Effect));
}

// Purpose: Adds an event to the world's event queue (e.g. 'settler changed', 'building finished'), from
// which other parts of the game (probably the UI, sound and messages) learn what happened. Some
// duplicate events are skipped.
std::uint32_t __fastcall GameWorld_QueueEvent(GameWorld* world, void*, std::uint32_t type,
                                              std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    if (type == kDedupedType) {
        std::uint32_t read = world->pending_event_index.read;
        const std::uint32_t write = world->pending_event_index.write;
        if (read != write) {
            bool found = false;
            do {
                const PendingEvent& entry = world->pending_events[read];
                found = static_cast<std::uint32_t>(entry.type) == kDedupedType &&
                        static_cast<std::uint32_t>(entry.a) == a;
                if (++read == kRingEntries) {
                    read = 0;
                }
            } while (!found && read != write);
            if (found) {
                return read;  // the index after the duplicate
            }
        }
    }
    // The write index is read again for every field, as in the original.
    world->events[world->event_index.write].type = static_cast<std::int32_t>(type);
    world->events[world->event_index.write].a = static_cast<std::int32_t>(a);
    world->events[world->event_index.write].b = static_cast<std::int32_t>(b);
    world->events[world->event_index.write].c = static_cast<std::int32_t>(c);
    const std::uint32_t next = world->event_index.write + 1;
    world->event_index.write = next == kRingEntries ? 0 : next;
    return next;
}

namespace {

// Appends (x, y) to the tile ring and clears flag 0x10 of the tile.
void PushMarkedTile(GameWorld* world, Tile& tile, std::uint32_t x, std::uint32_t y) {
    world->tile_ring[world->tile_ring_index.write].x = static_cast<std::uint16_t>(x);
    world->tile_ring[world->tile_ring_index.write].y = static_cast<std::uint16_t>(y);
    tile.flags &= 0xef;
    const std::uint32_t next = world->tile_ring_index.write + 1;
    world->tile_ring_index.write = next == kTileRingEntries ? 0 : next;
}

}  // namespace

// Purpose: One step of a flood fill over a player's marked tiles: queues a neighbour if it is marked and
// belongs to that player, and unmarks it so it is visited only once.
std::uint32_t __fastcall GameWorld_QueueMarkedTile(GameWorld* world, void*, std::uint32_t x,
                                                   std::uint32_t y, std::uint32_t player) {
    Tile& tile = TileAt(world, x, y);
    const std::uint8_t flags = tile.flags;
    if ((flags & 0x10) == 0 || (flags & 0x01) != 0 || tile.owner != player) {
        return 0;
    }
    PushMarkedTile(world, tile, x, y);
    return 0;  // callers ignore EAX
}

// Purpose: Like QueueMarkedTile, for marked tiles of three particular terrain types (probably water or
// other non-land terrain).
std::uint32_t __fastcall GameWorld_QueueMarkedTileTerrain5To7(GameWorld* world, void*,
                                                              std::uint32_t x, std::uint32_t y) {
    Tile& tile = TileAt(world, x, y);
    const std::uint8_t flags = tile.flags;
    const std::uint8_t terrain = tile.terrain;
    if ((flags & 0x10) == 0 || terrain < 5 || terrain > 7 || (flags & 0x80) != 0) {
        return 0;
    }
    PushMarkedTile(world, tile, x, y);
    return 0;  // callers ignore EAX
}

// Purpose: Like QueueMarkedTile, without the owner check.
std::uint32_t __fastcall GameWorld_QueueMarkedTileNoOwner(GameWorld* world, void*, std::uint32_t x,
                                                          std::uint32_t y) {
    Tile& tile = TileAt(world, x, y);
    const std::uint8_t flags = tile.flags;
    if ((flags & 0x10) == 0 || (flags & 0x01) != 0) {
        return 0;
    }
    PushMarkedTile(world, tile, x, y);
    return 0;  // callers ignore EAX
}

// Purpose: Counts down the world's timers by one; timers that run out are removed. Called once per
// simulation update; probably drives delayed actions.
std::uint32_t __fastcall GameWorld_CountDownTimers(GameWorld* world, void*) {
    for (std::uint32_t list = 0; list < kTimerLists; ++list) {
        NotificationTimer* const timers = world->notification_timers[list];
        for (std::uint32_t i = 0; i < kTimersPerList; ++i) {
            std::int32_t& timer = timers[i].countdown;
            if (timer == 0) {
                break;
            }
            timer = static_cast<std::int32_t>(static_cast<std::uint32_t>(timer) - 1);  // 32-bit wrap
            if (timer != 0) {
                continue;
            }
            // Remove entry i: move the later ones up (dword by dword), clear
            // the last one's countdown, and look at slot i again.
            for (std::uint32_t k = i; k < kTimersPerList - 1; ++k) {
                timers[k].countdown = timers[k + 1].countdown;
                timers[k].x = timers[k + 1].x;
                timers[k].y = timers[k + 1].y;
            }
            timers[kTimersPerList - 1].countdown = 0;
            --i;
        }
    }
    return 0;  // the only caller ignores EAX
}

// Purpose: Notes that a settler has claimed a goods pile (it expires after 128 steps), so other settlers
// (UpdateSettlers job 0x152, CountOtherPileClaims) know how many units of the pile are already spoken for.
std::uint32_t __fastcall GameWorld_AddPileClaim(GameWorld* world, void*, std::uint32_t settler, std::uint32_t pile) {
    const auto settler16 = static_cast<std::uint16_t>(settler);
    const auto pile16 = static_cast<std::uint16_t>(pile);
    for (std::uint32_t slot = 0; slot < kPileClaimCount; ++slot) {
        if (world->pile_claims[slot].pile == pile16 && world->pile_claims[slot].settler == settler16) {
            return 0;
        }
    }
    const std::uint16_t cursor = PileClaimsCursor();
    std::uint32_t slot = cursor == 0 ? kPileClaimCount - 1 : cursor - 1u;
    while (world->pile_claims[slot].pile != 0 || world->pile_claims[slot].settler != 0) {
        slot = slot == 0 ? kPileClaimCount - 1 : slot - 1;
    }
    world->pile_claims[slot].pile = pile16;
    world->pile_claims[slot].settler = settler16;
    return 0;  // the only caller ignores EAX
}

namespace {

// Appends (x, y) to the tile ring (without touching the tile flags).
void PushTile(GameWorld* world, std::uint32_t x, std::uint32_t y) {
    std::uint32_t& index = world->tile_ring_index.write;
    world->tile_ring[index].x = static_cast<std::uint16_t>(x);
    world->tile_ring[index].y = static_cast<std::uint16_t>(y);
    index = index + 1 == kTileRingEntries ? 0 : index + 1;
}

}  // namespace

// Purpose: One step of a flood fill: if a tile still has the old region number, give it the new one and
// remember it so its neighbours get checked next.
std::uint32_t __fastcall GameWorld_ReplaceTileWord16AndQueue(GameWorld* world, void*, std::uint32_t x,
                                                             std::uint32_t y, std::uint32_t expected,
                                                             std::uint32_t value) {
    std::uint16_t& field = TileAt(world, x, y).wide_region;
    if (field == expected) {
        field = static_cast<std::uint16_t>(value);
        PushTile(world, x, y);
    }
    return 0;  // callers ignore EAX
}

// Purpose: Like ReplaceTileWord16AndQueue, for the other kind of region number stored in each tile.
std::uint32_t __fastcall GameWorld_ReplaceTileByte10AndQueue(GameWorld* world, void*, std::uint32_t x,
                                                             std::uint32_t y, std::uint32_t expected,
                                                             std::uint32_t value) {
    std::uint8_t& field = TileAt(world, x, y).walk_region;
    if (field == expected) {
        field = static_cast<std::uint8_t>(value);
        PushTile(world, x, y);
    }
    return 0;  // callers ignore EAX
}

// Purpose: Adds an entry to the world's second, smaller event queue (the one QueueEvent checks for
// duplicates; probably events still to be handled, e.g. for areas); an entry of type 4 for the same
// thing that is already queued is not added again.
std::uint32_t __fastcall GameWorld_QueuePendingEvent(GameWorld* world, void*, std::uint32_t type,
                                                     std::uint32_t a, std::uint32_t b) {
    if (type == kDedupedPendingType) {
        std::uint32_t read = world->pending_event_index.read;
        const std::uint32_t write = world->pending_event_index.write;
        if (read != write) {
            bool found = false;
            do {
                const PendingEvent& entry = world->pending_events[read];
                found = static_cast<std::uint32_t>(entry.type) == kDedupedPendingType &&
                        static_cast<std::uint32_t>(entry.a) == a;
                if (++read == kRingEntries) {
                    read = 0;
                }
            } while (!found && read != write);
            if (found) {
                return read;  // the index after the duplicate
            }
        }
    }
    // The write index is read again for every field, as in the original.
    world->pending_events[world->pending_event_index.write].type = static_cast<std::int32_t>(type);
    world->pending_events[world->pending_event_index.write].a = static_cast<std::int32_t>(a);
    world->pending_events[world->pending_event_index.write].b = static_cast<std::int32_t>(b);
    const std::uint32_t next = world->pending_event_index.write + 1;
    world->pending_event_index.write = next;
    if (next == kRingEntries) {
        world->pending_event_index.write = 0;
    }
    return next;
}

S3RE_HOOK(0x004df0a0, GameWorld_AllocBuildingExtra, ValueFn, Original_GameWorld_AllocBuildingExtra,
          GameWorld_AllocBuildingExtra);
S3RE_HOOK(0x004fefd0, GameWorld_AllocTable36700Record, Alloc36700Fn,
          Original_GameWorld_AllocTable36700Record, GameWorld_AllocTable36700Record);
S3RE_HOOK(0x004e8ad0, GameWorld_QueueEvent, QueueEventFn, Original_GameWorld_QueueEvent,
          GameWorld_QueueEvent);
S3RE_HOOK(0x004e8ba0, GameWorld_QueuePendingEvent, QueuePendingEventFn, Original_GameWorld_QueuePendingEvent,
          GameWorld_QueuePendingEvent);
S3RE_HOOK(0x004e6c40, GameWorld_QueueMarkedTile, QueueTileFn, Original_GameWorld_QueueMarkedTile,
          GameWorld_QueueMarkedTile);
S3RE_HOOK(0x0051ce50, GameWorld_QueueMarkedTileTerrain5To7, PositionFn,
          Original_GameWorld_QueueMarkedTileTerrain5To7, GameWorld_QueueMarkedTileTerrain5To7);
S3RE_HOOK(0x0051d3d0, GameWorld_QueueMarkedTileNoOwner, PositionFn,
          Original_GameWorld_QueueMarkedTileNoOwner, GameWorld_QueueMarkedTileNoOwner);
S3RE_HOOK(0x0051c560, GameWorld_ReplaceTileWord16AndQueue, ReplaceTileFn,
          Original_GameWorld_ReplaceTileWord16AndQueue, GameWorld_ReplaceTileWord16AndQueue);
S3RE_HOOK(0x0051c820, GameWorld_ReplaceTileByte10AndQueue, ReplaceTileFn,
          Original_GameWorld_ReplaceTileByte10AndQueue, GameWorld_ReplaceTileByte10AndQueue);
S3RE_HOOK(0x004fee00, GameWorld_CountDownTimers, NoArgFn, Original_GameWorld_CountDownTimers,
          GameWorld_CountDownTimers);
S3RE_HOOK(0x00510770, GameWorld_AddPileClaim, PositionFn,
          Original_GameWorld_AddPileClaim, GameWorld_AddPileClaim);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// Up to the end of the pile claims (the last field these functions use).
constexpr std::size_t kTestWorld = offsetof(GameWorld, pile_claims) + sizeof(GameWorld::pile_claims);

std::uint32_t Relative(std::uint32_t value, GameWorld* world) {
    return value - Address(world);
}

// Runs `cases` cases of `call` (which prepares both worlds and returns the
// two EAX values) on the evolving worlds, compares EAX after every case and
// the whole world every 16 cases (a full compare per case would be slow).
template <typename Call>
void Compare(test::Context& ctx, int cases, bool compareEax, Call call) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        std::uint32_t expected = 0;
        std::uint32_t actual = 0;
        call(worlds, expected, actual);
        if ((compareEax && !S3RE_EXPECT_EQ(expected, actual)) ||
            ((i % 16 == 15 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 15 before it)", i);
            return;
        }
    }
}

// Marks all records of a table used except a few (or none). `setFirst(w,
// record, value)` writes the record's first field (the free marker);
// `highWater(w)` is the table's high-water mark.
template <typename SetFirst, typename HighWater>
void FillTable(test::Context& ctx, test::WorldPair& worlds, std::uint32_t limit, std::uint32_t usedValue,
               std::uint32_t freeValue, SetFirst setFirst, HighWater highWater) {
    const bool full = ctx.rng().Chance(1, 8);
    const std::uint32_t freeA = full ? limit + 1 : 1 + ctx.rng().Below(limit);
    const std::uint32_t freeB = ctx.rng().Chance(1, 4) ? limit - 1 + ctx.rng().Below(2) : freeA;
    for (std::uint32_t record = 1; record <= limit; ++record) {
        const bool isFree = record == freeA || record == freeB;
        const std::uint32_t value = isFree ? freeValue : (usedValue == 0 ? 1 + ctx.rng().Below(0xfe) : usedValue);
        worlds.Both([&](GameWorld* w) { setFirst(w, record, value); });
    }
    if (ctx.rng().Chance(1, 2)) {
        const std::uint32_t mark = ctx.rng().Below(limit + 2);
        worlds.Both([&](GameWorld* w) { highWater(w) = mark; });
    }
}

void SetUpRings(test::Context& ctx, test::WorldPair& worlds) {
    const std::uint32_t read = ctx.rng().Below(kRingEntries);
    const std::uint32_t write = ctx.rng().Chance(1, 8) ? read : ctx.rng().Below(kRingEntries);
    const std::uint32_t eventWrite = ctx.rng().Chance(1, 8) ? kRingEntries - 1 : ctx.rng().Below(kRingEntries);
    worlds.Both([&](GameWorld* w) {
        w->pending_event_index.read = read;
        w->pending_event_index.write = write;
        w->event_index.write = eventWrite;
    });
    // Some pending entries of type 6 with small values, so that matches occur.
    for (int k = 0; k < 4; ++k) {
        const std::uint32_t entry = ctx.rng().Below(kRingEntries);
        const auto a = static_cast<std::int32_t>(ctx.rng().Below(4));
        worlds.Both([&](GameWorld* w) {
            w->pending_events[entry].type = kDedupedType;
            w->pending_events[entry].a = a;
        });
    }
}

// Wrong: skips the duplicate check.
std::uint32_t __fastcall WrongQueueEvent(GameWorld* world, void*, std::uint32_t type,
                                         std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    if (type == kDedupedType) {
        type = 7;
        const std::uint32_t result = GameWorld_QueueEvent(world, nullptr, type, a, b, c);
        world->events[(world->event_index.write + kRingEntries - 1) % kRingEntries].type = kDedupedType;
        return result;
    }
    return GameWorld_QueueEvent(world, nullptr, type, a, b, c);
}

void CompareQueueEvent(test::Context& ctx, QueueEventFn original, QueueEventFn candidate, int cases) {
    Compare(ctx, cases, true, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        SetUpRings(ctx, worlds);
        const std::uint32_t type = ctx.rng().Chance(1, 2) ? kDedupedType : ctx.rng().Below(10);
        const std::uint32_t a = ctx.rng().Below(4);
        const std::uint32_t b = ctx.rng().Next32();
        const std::uint32_t c = ctx.rng().Next32();
        expected = original(worlds.original(), nullptr, type, a, b, c);
        actual = candidate(worlds.ours(), nullptr, type, a, b, c);
    });
}

}  // namespace

S3RE_TEST(GameWorld_AllocBuildingExtra_MatchesOriginal) {
    Compare(ctx, 200, true, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        FillTable(
            ctx, worlds, kBuildingExtraLimit, 0, 0xff,
            [](GameWorld* w, std::uint32_t record, std::uint32_t value) {
                w->trades[record].kind = static_cast<std::uint8_t>(value);
            },
            [](GameWorld* w) -> std::uint32_t& { return w->trade_high_water; });
        const std::uint32_t value = ctx.rng().Next32();
        expected = Original_GameWorld_AllocBuildingExtra(worlds.original(), nullptr, value);
        actual = GameWorld_AllocBuildingExtra(worlds.ours(), nullptr, value);
    });
}

S3RE_TEST(GameWorld_AllocTable36700Record_MatchesOriginal) {
    Compare(ctx, 100, true, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        FillTable(
            ctx, worlds, kTable36700Limit, 0, 0,
            [](GameWorld* w, std::uint32_t record, std::uint32_t value) {
                w->effects[record].type = static_cast<std::uint16_t>(value);
            },
            [](GameWorld* w) -> std::uint32_t& { return w->effects_high_water; });
        std::uint32_t v[10];
        for (auto& value : v) {
            value = ctx.rng().Next32();
        }
        expected = Relative(Original_GameWorld_AllocTable36700Record(worlds.original(), nullptr, v[0], v[1], v[2],
                                                                     v[3], v[4], v[5], v[6], v[7], v[8], v[9]),
                            worlds.original());
        actual = Relative(GameWorld_AllocTable36700Record(worlds.ours(), nullptr, v[0], v[1], v[2], v[3], v[4],
                                                          v[5], v[6], v[7], v[8], v[9]),
                          worlds.ours());
    });
}

S3RE_TEST(GameWorld_QueueEvent_MatchesOriginal) {
    CompareQueueEvent(ctx, Original_GameWorld_QueueEvent, &GameWorld_QueueEvent, 2000);
}
S3RE_TEST(GameWorld_QueueEvent_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareQueueEvent(probe, Original_GameWorld_QueueEvent, &WrongQueueEvent, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

// Wrong: checks for duplicates of type 6 instead of 4.
std::uint32_t __fastcall WrongQueuePendingEvent(GameWorld* world, void* edx, std::uint32_t type, std::uint32_t a,
                                                std::uint32_t b) {
    if (type == kDedupedPendingType || type == kDedupedType) {
        const std::uint32_t write = world->pending_event_index.write;
        const std::uint32_t result = GameWorld_QueuePendingEvent(world, edx, type ^ 4 ^ 6, a, b);
        if (world->pending_event_index.write != write) {
            world->pending_events[write].type = static_cast<std::int32_t>(type);
        }
        return result;
    }
    return GameWorld_QueuePendingEvent(world, edx, type, a, b);
}

void CompareQueuePendingEvent(test::Context& ctx, QueuePendingEventFn candidate, int cases) {
    Compare(ctx, cases, true, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        SetUpRings(ctx, worlds);
        for (int k = 0; k < 4; ++k) {
            const std::uint32_t entry = ctx.rng().Below(kRingEntries);
            const auto a = static_cast<std::int32_t>(ctx.rng().Below(4));
            worlds.Both([&](GameWorld* w) {
                w->pending_events[entry].type = kDedupedPendingType;
                w->pending_events[entry].a = a;
            });
        }
        if (ctx.rng().Chance(1, 8)) {
            worlds.Both([&](GameWorld* w) { w->pending_event_index.write = kRingEntries - 1; });
        }
        const std::uint32_t type = ctx.rng().Chance(1, 2) ? kDedupedPendingType
                                   : ctx.rng().Chance(1, 3) ? kDedupedType
                                                            : ctx.rng().Below(10);
        const std::uint32_t a = ctx.rng().Below(4);
        const std::uint32_t b = ctx.rng().Next32();
        expected = Original_GameWorld_QueuePendingEvent(worlds.original(), nullptr, type, a, b);
        actual = candidate(worlds.ours(), nullptr, type, a, b);
    });
}

void SetTileRingWrite(test::Context& ctx, test::WorldPair& worlds) {
    const std::uint32_t write = ctx.rng().Chance(1, 8) ? kTileRingEntries - 1 : ctx.rng().Below(kTileRingEntries);
    worlds.Both([&](GameWorld* w) { w->tile_ring_index.write = write; });
}

}  // namespace

S3RE_TEST(GameWorld_QueuePendingEvent_MatchesOriginal) {
    CompareQueuePendingEvent(ctx, &GameWorld_QueuePendingEvent, 2000);
}
S3RE_TEST(GameWorld_QueuePendingEvent_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareQueuePendingEvent(probe, &WrongQueuePendingEvent, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_QueueMarkedTile_MatchesOriginal) {
    Compare(ctx, 2000, false, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        const std::uint32_t x = ctx.rng().Below(384);
        const std::uint32_t y = ctx.rng().Below(384);
        const std::uint32_t player = ctx.rng().Chance(1, 16) ? ctx.rng().Next32() : ctx.rng().Below(4);
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Below(4));
        worlds.Both([&](GameWorld* w) { w->tiles[x][y].owner = owner; });
        SetTileRingWrite(ctx, worlds);
        expected = Original_GameWorld_QueueMarkedTile(worlds.original(), nullptr, x, y, player);
        actual = GameWorld_QueueMarkedTile(worlds.ours(), nullptr, x, y, player);
    });
}

namespace {

// Wrong: accepts terrain 4 too.
std::uint32_t __fastcall WrongQueueMarkedTileTerrain5To7(GameWorld* world, void* edx,
                                                         std::uint32_t x, std::uint32_t y) {
    std::uint8_t& terrain = TileAt(world, x, y).terrain;
    if (terrain == 4) {
        terrain = 5;
        GameWorld_QueueMarkedTileTerrain5To7(world, edx, x, y);
        terrain = 4;
        return 0;
    }
    return GameWorld_QueueMarkedTileTerrain5To7(world, edx, x, y);
}

template <typename Fn>
void CompareQueueTile(test::Context& ctx, Fn original, Fn candidate, int cases) {
    Compare(ctx, cases, false, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        const std::uint32_t x = ctx.rng().Below(384);
        const std::uint32_t y = ctx.rng().Below(384);
        const auto terrain =
            static_cast<std::uint8_t>(ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : 3 + ctx.rng().Below(6));
        worlds.Both([&](GameWorld* w) { w->tiles[x][y].terrain = terrain; });
        SetTileRingWrite(ctx, worlds);
        expected = original(worlds.original(), nullptr, x, y);
        actual = candidate(worlds.ours(), nullptr, x, y);
    });
}

}  // namespace

S3RE_TEST(GameWorld_QueueMarkedTileTerrain5To7_MatchesOriginal) {
    CompareQueueTile(ctx, Original_GameWorld_QueueMarkedTileTerrain5To7,
                     &GameWorld_QueueMarkedTileTerrain5To7, 2000);
}
S3RE_TEST(GameWorld_QueueMarkedTileTerrain5To7_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareQueueTile(probe, Original_GameWorld_QueueMarkedTileTerrain5To7,
                     &WrongQueueMarkedTileTerrain5To7, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_QueueMarkedTileNoOwner_MatchesOriginal) {
    CompareQueueTile(ctx, Original_GameWorld_QueueMarkedTileNoOwner,
                     &GameWorld_QueueMarkedTileNoOwner, 2000);
}

namespace {

// Wrong: compares only the low byte of `expected` with a small word.
std::uint32_t __fastcall WrongReplaceTileWord16AndQueue(GameWorld* world, void* edx, std::uint32_t x,
                                                        std::uint32_t y, std::uint32_t expected,
                                                        std::uint32_t value) {
    const std::uint16_t field = TileAt(world, x, y).wide_region;
    return GameWorld_ReplaceTileWord16AndQueue(world, edx, x, y, field <= 0xff ? expected & 0xff : expected,
                                               value);
}

// `wideRegion`: the word wide_region (+0x16), else the byte walk_region (+0x10).
void CompareReplaceTile(test::Context& ctx, ReplaceTileFn original, ReplaceTileFn candidate, bool wideRegion,
                        int cases) {
    Compare(ctx, cases, false, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        const std::uint32_t x = ctx.rng().Below(384);
        const std::uint32_t y = ctx.rng().Below(384);
        const std::uint32_t current = ctx.rng().Below(4);
        worlds.Both([&](GameWorld* w) {
            if (wideRegion) {
                w->tiles[x][y].wide_region = static_cast<std::uint16_t>(current);
            } else {
                w->tiles[x][y].walk_region = static_cast<std::uint8_t>(current);
            }
        });
        // Mostly small values (often equal); sometimes high bits set.
        std::uint32_t want = ctx.rng().Below(4);
        if (ctx.rng().Chance(1, 8)) {
            want |= ctx.rng().Next32() & ~0xffu;
        }
        const std::uint32_t value = ctx.rng().Next32();
        SetTileRingWrite(ctx, worlds);
        expected = original(worlds.original(), nullptr, x, y, want, value);
        actual = candidate(worlds.ours(), nullptr, x, y, want, value);
    });
}

}  // namespace

S3RE_TEST(GameWorld_ReplaceTileWord16AndQueue_MatchesOriginal) {
    CompareReplaceTile(ctx, Original_GameWorld_ReplaceTileWord16AndQueue,
                       &GameWorld_ReplaceTileWord16AndQueue, true, 2000);
}
S3RE_TEST(GameWorld_ReplaceTileWord16AndQueue_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareReplaceTile(probe, Original_GameWorld_ReplaceTileWord16AndQueue, &WrongReplaceTileWord16AndQueue,
                       true, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_ReplaceTileByte10AndQueue_MatchesOriginal) {
    CompareReplaceTile(ctx, Original_GameWorld_ReplaceTileByte10AndQueue,
                       &GameWorld_ReplaceTileByte10AndQueue, false, 2000);
}

S3RE_TEST(GameWorld_CountDownTimers_MatchesOriginal) {
    Compare(ctx, 300, false, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        // Small timer values (many expire) with a zero somewhere in most lists.
        for (std::uint32_t entry = 0; entry < kTimerLists * kTimersPerList; ++entry) {
            if (ctx.rng().Chance(1, 2)) {
                const auto countdown = static_cast<std::int32_t>(ctx.rng().Below(4));
                worlds.Both([&](GameWorld* w) {
                    w->notification_timers[entry / kTimersPerList][entry % kTimersPerList].countdown = countdown;
                });
            }
        }
        expected = Original_GameWorld_CountDownTimers(worlds.original(), nullptr);
        actual = GameWorld_CountDownTimers(worlds.ours(), nullptr);
    });
}

S3RE_TEST(GameWorld_AddPileClaim_MatchesOriginal) {
    // The cursor is an S3.EXE global; the test sets it and restores it.
    volatile std::uint16_t& cursor = *reinterpret_cast<volatile std::uint16_t*>(kPileClaimsCursor);
    const std::uint16_t saved = cursor;
    Compare(ctx, 3000, false, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        cursor = static_cast<std::uint16_t>(ctx.rng().Below(kPileClaimCount));
        // Small numbers so that the list contains the claim now and then;
        // keep at least one empty slot (the original would loop forever).
        for (std::uint32_t slot = 0; slot < kPileClaimCount; ++slot) {
            const bool empty = ctx.rng().Chance(1, 16);
            const auto first = empty ? std::uint16_t{0} : static_cast<std::uint16_t>(ctx.rng().Below(8));
            const auto second = empty ? std::uint16_t{0} : static_cast<std::uint16_t>(ctx.rng().Below(8));
            worlds.Both([&](GameWorld* w) {
                w->pile_claims[slot].pile = first;
                w->pile_claims[slot].settler = second;
            });
        }
        const std::uint32_t emptySlot = ctx.rng().Below(kPileClaimCount);
        worlds.Both([&](GameWorld* w) {
            w->pile_claims[emptySlot].pile = 0;
            w->pile_claims[emptySlot].settler = 0;
        });
        const std::uint32_t settler = ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(8);
        const std::uint32_t pile = ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(8);
        expected = Original_GameWorld_AddPileClaim(worlds.original(), nullptr, settler, pile);
        actual = GameWorld_AddPileClaim(worlds.ours(), nullptr, settler, pile);
    });
    cursor = saved;
}

}  // namespace s3re::game
