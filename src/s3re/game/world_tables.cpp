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
//   last one is cleared). Called once per UpdatePhase2. Plain ret; EAX
//   ignored.
// GameWorld::AddToRecentPositions (0x00510770): (x, y) 128 uint16 pairs
//   {y, x} at world+0x1195dd4. If (x, y) is not in the list, stores it in
//   the first empty slot (both words 0) searching downwards from the slot
//   before the cursor g_recentPositionsCursor (0x7ace60, wrapping from 0 to
//   127; loops forever if no slot is empty). The logic thread clears the
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

namespace {

constexpr std::uint32_t kBuildingExtras = 0x10f45e8;
constexpr std::uint32_t kBuildingExtraBytes = 0x5a;
constexpr std::uint32_t kBuildingExtraLimit = 999;
constexpr std::uint32_t kBuildingExtraHighWater = 0x110a578;

constexpr std::uint32_t kTable36700 = 0x36700;
constexpr std::uint32_t kTable36700Bytes = 20;
constexpr std::uint32_t kTable36700Limit = 9999;
constexpr std::uint32_t kTable36700HighWater = 0x67440;

constexpr std::uint32_t kEventRing = 0x32878;
constexpr std::uint32_t kEventBytes = 16;
constexpr std::uint32_t kEventWrite = 0x366fc;
constexpr std::uint32_t kPendingRing = 0x2f990;
constexpr std::uint32_t kPendingBytes = 12;
constexpr std::uint32_t kPendingRead = 0x32870;
constexpr std::uint32_t kPendingWrite = 0x32874;
constexpr std::uint32_t kRingEntries = 1000;
constexpr std::uint32_t kDedupedType = 6;

constexpr std::uint32_t kTiles = 0x67444;
constexpr std::uint32_t kTileOwner = 0x8;
constexpr std::uint32_t kTileTerrain = 0x6;
constexpr std::uint32_t kTileFlags = 0xa;
constexpr std::uint32_t kTileByte10 = 0x10;
constexpr std::uint32_t kTileWord16 = 0x16;
constexpr std::uint32_t kTileRing = 0xdfdc48;
constexpr std::uint32_t kTileRingIndex = 0xe0788c;
constexpr std::uint32_t kTileRingEntries = 10000;

constexpr std::uint32_t kTimers = 0x2e5e0;
constexpr std::uint32_t kTimerLists = 42;
constexpr std::uint32_t kTimersPerList = 10;
constexpr std::uint32_t kTimerBytes = 12;

constexpr std::uint32_t kRecentPositions = 0x1195dd4;
constexpr std::uint32_t kRecentPositionCount = 0x80;
constexpr std::uintptr_t kRecentPositionsCursor = 0x7ace60;  // uint16, S3.EXE global

std::uint8_t* Bytes(GameWorld* world) {
    return reinterpret_cast<std::uint8_t*>(world);
}
template <typename T>
T& At(GameWorld* world, std::uint32_t offset) {
    return *reinterpret_cast<T*>(Bytes(world) + offset);
}
std::uint32_t Address(GameWorld* world) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));
}

std::uint16_t RecentPositionsCursor() {
    return *reinterpret_cast<const volatile std::uint16_t*>(kRecentPositionsCursor);
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
using QueueTileFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x,
                                               std::uint32_t y, std::uint32_t player);
using NoArgFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx);
using PositionFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x,
                                              std::uint32_t y);
using ReplaceTileFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x,
                                                 std::uint32_t y, std::uint32_t expected,
                                                 std::uint32_t value);

std::uint32_t __fastcall GameWorld_AllocBuildingExtra(GameWorld* world, void*,
                                                      std::uint32_t value) {
    std::uint32_t record = 1;
    while (At<std::uint8_t>(world, kBuildingExtras + record * kBuildingExtraBytes) != 0xff) {
        if (record == kBuildingExtraLimit) {
            return 0;
        }
        ++record;
    }
    if (record == kBuildingExtraLimit) {
        return 0;
    }
    if (record > At<std::uint32_t>(world, kBuildingExtraHighWater)) {
        At<std::uint32_t>(world, kBuildingExtraHighWater) = record;
    }
    const std::uint32_t r = kBuildingExtras + record * kBuildingExtraBytes;
    At<std::uint8_t>(world, r + 0x0) = static_cast<std::uint8_t>(value);
    At<std::uint8_t>(world, r + 0x1) = 1;
    for (std::uint32_t field = 0x2; field <= 0x10; field += 2) {
        At<std::uint16_t>(world, r + field) = 0xffff;
    }
    std::memset(Bytes(world) + r + 0x12, 0, 0x48);
    return record;
}

std::uint32_t __fastcall GameWorld_AllocTable36700Record(GameWorld* world, void*, std::uint32_t a0,
                                                         std::uint32_t a1, std::uint32_t a2,
                                                         std::uint32_t a3, std::uint32_t a4,
                                                         std::uint32_t a5, std::uint32_t a6,
                                                         std::uint32_t a7, std::uint32_t a8,
                                                         std::uint32_t a9) {
    std::uint32_t record = 1;
    while (At<std::uint16_t>(world, kTable36700 + record * kTable36700Bytes) != 0 &&
           record != kTable36700Limit) {
        ++record;
    }
    if (record == kTable36700Limit) {
        return Address(world) + kTable36700 + kTable36700Limit * kTable36700Bytes;
    }
    if (record > At<std::uint32_t>(world, kTable36700HighWater)) {
        At<std::uint32_t>(world, kTable36700HighWater) = record;
    }
    const std::uint32_t values[] = {a0, a1, a2, a3, a4, a5, a6, a7, a8, a9};
    for (std::uint32_t i = 0; i < 10; ++i) {
        At<std::uint16_t>(world, kTable36700 + record * kTable36700Bytes + i * 2) =
            static_cast<std::uint16_t>(values[i]);
    }
    return Address(world) + record * kTable36700Bytes;
}

std::uint32_t __fastcall GameWorld_QueueEvent(GameWorld* world, void*, std::uint32_t type,
                                              std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    if (type == kDedupedType) {
        std::uint32_t read = At<std::uint32_t>(world, kPendingRead);
        const std::uint32_t write = At<std::uint32_t>(world, kPendingWrite);
        if (read != write) {
            bool found = false;
            do {
                const std::uint32_t entry = kPendingRing + read * kPendingBytes;
                found = At<std::uint32_t>(world, entry) == kDedupedType &&
                        At<std::uint32_t>(world, entry + 4) == a;
                if (++read == kRingEntries) {
                    read = 0;
                }
            } while (!found && read != write);
            if (found) {
                return read;  // the index after the duplicate
            }
        }
    }
    const std::uint32_t entry = kEventRing + At<std::uint32_t>(world, kEventWrite) * kEventBytes;
    At<std::uint32_t>(world, entry + 0x0) = type;
    At<std::uint32_t>(world, kEventRing + At<std::uint32_t>(world, kEventWrite) * kEventBytes + 0x4) = a;
    At<std::uint32_t>(world, kEventRing + At<std::uint32_t>(world, kEventWrite) * kEventBytes + 0x8) = b;
    At<std::uint32_t>(world, kEventRing + At<std::uint32_t>(world, kEventWrite) * kEventBytes + 0xc) = c;
    const std::uint32_t next = At<std::uint32_t>(world, kEventWrite) + 1;
    At<std::uint32_t>(world, kEventWrite) = next == kRingEntries ? 0 : next;
    return next;
}

std::uint32_t __fastcall GameWorld_QueueMarkedTile(GameWorld* world, void*, std::uint32_t x,
                                                   std::uint32_t y, std::uint32_t player) {
    const std::uint32_t tile = kTiles + (x * 0x300 + y) * 24;
    const std::uint8_t flags = At<std::uint8_t>(world, tile + kTileFlags);
    if ((flags & 0x10) == 0 || (flags & 0x01) != 0 ||
        At<std::uint8_t>(world, tile + kTileOwner) != player) {
        return 0;
    }
    At<std::uint16_t>(world, kTileRing + At<std::uint32_t>(world, kTileRingIndex) * 4) =
        static_cast<std::uint16_t>(x);
    At<std::uint16_t>(world, kTileRing + At<std::uint32_t>(world, kTileRingIndex) * 4 + 2) =
        static_cast<std::uint16_t>(y);
    At<std::uint8_t>(world, tile + kTileFlags) &= 0xef;
    const std::uint32_t next = At<std::uint32_t>(world, kTileRingIndex) + 1;
    At<std::uint32_t>(world, kTileRingIndex) = next == kTileRingEntries ? 0 : next;
    return 0;  // callers ignore EAX
}

namespace {

// Appends (x, y) to the tile ring and clears flag 0x10 of the tile.
void PushMarkedTile(GameWorld* world, std::uint32_t tile, std::uint32_t x, std::uint32_t y) {
    At<std::uint16_t>(world, kTileRing + At<std::uint32_t>(world, kTileRingIndex) * 4) =
        static_cast<std::uint16_t>(x);
    At<std::uint16_t>(world, kTileRing + At<std::uint32_t>(world, kTileRingIndex) * 4 + 2) =
        static_cast<std::uint16_t>(y);
    At<std::uint8_t>(world, tile + kTileFlags) &= 0xef;
    const std::uint32_t next = At<std::uint32_t>(world, kTileRingIndex) + 1;
    At<std::uint32_t>(world, kTileRingIndex) = next == kTileRingEntries ? 0 : next;
}

}  // namespace

std::uint32_t __fastcall GameWorld_QueueMarkedTileTerrain5To7(GameWorld* world, void*,
                                                              std::uint32_t x, std::uint32_t y) {
    const std::uint32_t tile = kTiles + (x * 0x300 + y) * 24;
    const std::uint8_t flags = At<std::uint8_t>(world, tile + kTileFlags);
    const std::uint8_t terrain = At<std::uint8_t>(world, tile + kTileTerrain);
    if ((flags & 0x10) == 0 || terrain < 5 || terrain > 7 || (flags & 0x80) != 0) {
        return 0;
    }
    PushMarkedTile(world, tile, x, y);
    return 0;  // callers ignore EAX
}

std::uint32_t __fastcall GameWorld_QueueMarkedTileNoOwner(GameWorld* world, void*, std::uint32_t x,
                                                          std::uint32_t y) {
    const std::uint32_t tile = kTiles + (x * 0x300 + y) * 24;
    const std::uint8_t flags = At<std::uint8_t>(world, tile + kTileFlags);
    if ((flags & 0x10) == 0 || (flags & 0x01) != 0) {
        return 0;
    }
    PushMarkedTile(world, tile, x, y);
    return 0;  // callers ignore EAX
}

std::uint32_t __fastcall GameWorld_CountDownTimers(GameWorld* world, void*) {
    for (std::uint32_t list = 0; list < kTimerLists; ++list) {
        const std::uint32_t first = kTimers + list * kTimersPerList * kTimerBytes;
        for (std::uint32_t i = 0; i < kTimersPerList; ++i) {
            std::uint32_t& timer = At<std::uint32_t>(world, first + i * kTimerBytes);
            if (timer == 0) {
                break;
            }
            if (--timer != 0) {
                continue;
            }
            // Remove entry i: move the later ones up, clear the last one, and
            // look at slot i again.
            for (std::uint32_t k = i; k < kTimersPerList - 1; ++k) {
                for (std::uint32_t word = 0; word < 3; ++word) {
                    At<std::uint32_t>(world, first + k * kTimerBytes + word * 4) =
                        At<std::uint32_t>(world, first + (k + 1) * kTimerBytes + word * 4);
                }
            }
            At<std::uint32_t>(world, first + (kTimersPerList - 1) * kTimerBytes) = 0;
            --i;
        }
    }
    return 0;  // the only caller ignores EAX
}

std::uint32_t __fastcall GameWorld_AddToRecentPositions(GameWorld* world, void*, std::uint32_t x,
                                                        std::uint32_t y) {
    const auto x16 = static_cast<std::uint16_t>(x);
    const auto y16 = static_cast<std::uint16_t>(y);
    for (std::uint32_t slot = 0; slot < kRecentPositionCount; ++slot) {
        if (At<std::uint16_t>(world, kRecentPositions + slot * 4) == y16 &&
            At<std::uint16_t>(world, kRecentPositions + slot * 4 + 2) == x16) {
            return 0;
        }
    }
    const std::uint16_t cursor = RecentPositionsCursor();
    std::uint32_t slot = cursor == 0 ? kRecentPositionCount - 1 : cursor - 1u;
    while (At<std::uint16_t>(world, kRecentPositions + slot * 4) != 0 ||
           At<std::uint16_t>(world, kRecentPositions + slot * 4 + 2) != 0) {
        slot = slot == 0 ? kRecentPositionCount - 1 : slot - 1;
    }
    At<std::uint16_t>(world, kRecentPositions + slot * 4) = y16;
    At<std::uint16_t>(world, kRecentPositions + slot * 4 + 2) = x16;
    return 0;  // the only caller ignores EAX
}

namespace {

// Appends (x, y) to the tile ring (without touching the tile flags).
void PushTile(GameWorld* world, std::uint32_t x, std::uint32_t y) {
    std::uint32_t& index = At<std::uint32_t>(world, kTileRingIndex);
    At<std::uint16_t>(world, kTileRing + index * 4) = static_cast<std::uint16_t>(x);
    At<std::uint16_t>(world, kTileRing + index * 4 + 2) = static_cast<std::uint16_t>(y);
    index = index + 1 == kTileRingEntries ? 0 : index + 1;
}

}  // namespace

std::uint32_t __fastcall GameWorld_ReplaceTileWord16AndQueue(GameWorld* world, void*, std::uint32_t x,
                                                             std::uint32_t y, std::uint32_t expected,
                                                             std::uint32_t value) {
    std::uint16_t& field = At<std::uint16_t>(world, kTiles + (x * 0x300 + y) * 24 + kTileWord16);
    if (field == expected) {
        field = static_cast<std::uint16_t>(value);
        PushTile(world, x, y);
    }
    return 0;  // callers ignore EAX
}

std::uint32_t __fastcall GameWorld_ReplaceTileByte10AndQueue(GameWorld* world, void*, std::uint32_t x,
                                                             std::uint32_t y, std::uint32_t expected,
                                                             std::uint32_t value) {
    std::uint8_t& field = At<std::uint8_t>(world, kTiles + (x * 0x300 + y) * 24 + kTileByte10);
    if (field == expected) {
        field = static_cast<std::uint8_t>(value);
        PushTile(world, x, y);
    }
    return 0;  // callers ignore EAX
}

S3RE_HOOK(0x004df0a0, GameWorld_AllocBuildingExtra, ValueFn, Original_GameWorld_AllocBuildingExtra,
          GameWorld_AllocBuildingExtra);
S3RE_HOOK(0x004fefd0, GameWorld_AllocTable36700Record, Alloc36700Fn,
          Original_GameWorld_AllocTable36700Record, GameWorld_AllocTable36700Record);
S3RE_HOOK(0x004e8ad0, GameWorld_QueueEvent, QueueEventFn, Original_GameWorld_QueueEvent,
          GameWorld_QueueEvent);
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
S3RE_HOOK(0x00510770, GameWorld_AddToRecentPositions, PositionFn,
          Original_GameWorld_AddToRecentPositions, GameWorld_AddToRecentPositions);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kTestWorld = kRecentPositions + kRecentPositionCount * 4;

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

// Marks all records of a table used except a few (or none).
void FillTable(test::Context& ctx, test::WorldPair& worlds, std::uint32_t table,
               std::uint32_t bytes, std::uint32_t limit, bool byteField, std::uint32_t usedValue,
               std::uint32_t freeValue) {
    const bool full = ctx.rng().Chance(1, 8);
    const std::uint32_t freeA = full ? limit + 1 : 1 + ctx.rng().Below(limit);
    const std::uint32_t freeB = ctx.rng().Chance(1, 4) ? limit - 1 + ctx.rng().Below(2) : freeA;
    for (std::uint32_t record = 1; record <= limit; ++record) {
        const bool isFree = record == freeA || record == freeB;
        const std::uint32_t value = isFree ? freeValue : (usedValue == 0 ? 1 + ctx.rng().Below(0xfe) : usedValue);
        if (byteField) {
            worlds.Set<std::uint8_t>(table + record * bytes, static_cast<std::uint8_t>(value));
        } else {
            worlds.Set<std::uint16_t>(table + record * bytes, static_cast<std::uint16_t>(value));
        }
    }
    if (ctx.rng().Chance(1, 2)) {
        worlds.Set<std::uint32_t>(table == kBuildingExtras ? kBuildingExtraHighWater : kTable36700HighWater,
                                  ctx.rng().Below(limit + 2));
    }
}

void SetUpRings(test::Context& ctx, test::WorldPair& worlds) {
    worlds.Set<std::uint32_t>(kPendingRead, ctx.rng().Below(kRingEntries));
    worlds.Set<std::uint32_t>(kPendingWrite, ctx.rng().Chance(1, 8)
                                                 ? At<std::uint32_t>(worlds.original(), kPendingRead)
                                                 : ctx.rng().Below(kRingEntries));
    worlds.Set<std::uint32_t>(kEventWrite, ctx.rng().Chance(1, 8) ? kRingEntries - 1
                                                                   : ctx.rng().Below(kRingEntries));
    // Some pending entries of type 6 with small values, so that matches occur.
    for (int k = 0; k < 4; ++k) {
        const std::uint32_t entry = kPendingRing + ctx.rng().Below(kRingEntries) * kPendingBytes;
        worlds.Set<std::uint32_t>(entry, kDedupedType);
        worlds.Set<std::uint32_t>(entry + 4, ctx.rng().Below(4));
    }
}

// Wrong: skips the duplicate check.
std::uint32_t __fastcall WrongQueueEvent(GameWorld* world, void*, std::uint32_t type,
                                         std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    if (type == kDedupedType) {
        type = 7;
        const std::uint32_t result = GameWorld_QueueEvent(world, nullptr, type, a, b, c);
        At<std::uint32_t>(world, kEventRing +
                                     ((At<std::uint32_t>(world, kEventWrite) + kRingEntries - 1) % kRingEntries) *
                                         kEventBytes) = kDedupedType;
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
        FillTable(ctx, worlds, kBuildingExtras, kBuildingExtraBytes, kBuildingExtraLimit, true, 0, 0xff);
        const std::uint32_t value = ctx.rng().Next32();
        expected = Original_GameWorld_AllocBuildingExtra(worlds.original(), nullptr, value);
        actual = GameWorld_AllocBuildingExtra(worlds.ours(), nullptr, value);
    });
}

S3RE_TEST(GameWorld_AllocTable36700Record_MatchesOriginal) {
    Compare(ctx, 100, true, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        FillTable(ctx, worlds, kTable36700, kTable36700Bytes, kTable36700Limit, false, 0, 0);
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

S3RE_TEST(GameWorld_QueueMarkedTile_MatchesOriginal) {
    Compare(ctx, 2000, false, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        const std::uint32_t x = ctx.rng().Below(384);
        const std::uint32_t y = ctx.rng().Below(384);
        const std::uint32_t tile = kTiles + (x * 0x300 + y) * 24;
        const std::uint32_t player = ctx.rng().Chance(1, 16) ? ctx.rng().Next32() : ctx.rng().Below(4);
        worlds.Set<std::uint8_t>(tile + kTileOwner, static_cast<std::uint8_t>(ctx.rng().Below(4)));
        worlds.Set<std::uint32_t>(kTileRingIndex, ctx.rng().Chance(1, 8) ? kTileRingEntries - 1
                                                                         : ctx.rng().Below(kTileRingEntries));
        expected = Original_GameWorld_QueueMarkedTile(worlds.original(), nullptr, x, y, player);
        actual = GameWorld_QueueMarkedTile(worlds.ours(), nullptr, x, y, player);
    });
}

namespace {

// Wrong: accepts terrain 4 too.
std::uint32_t __fastcall WrongQueueMarkedTileTerrain5To7(GameWorld* world, void* edx,
                                                         std::uint32_t x, std::uint32_t y) {
    const std::uint32_t tile = kTiles + (x * 0x300 + y) * 24;
    if (At<std::uint8_t>(world, tile + kTileTerrain) == 4) {
        At<std::uint8_t>(world, tile + kTileTerrain) = 5;
        GameWorld_QueueMarkedTileTerrain5To7(world, edx, x, y);
        At<std::uint8_t>(world, tile + kTileTerrain) = 4;
        return 0;
    }
    return GameWorld_QueueMarkedTileTerrain5To7(world, edx, x, y);
}

template <typename Fn>
void CompareQueueTile(test::Context& ctx, Fn original, Fn candidate, int cases) {
    Compare(ctx, cases, false, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        const std::uint32_t x = ctx.rng().Below(384);
        const std::uint32_t y = ctx.rng().Below(384);
        const std::uint32_t tile = kTiles + (x * 0x300 + y) * 24;
        worlds.Set<std::uint8_t>(tile + kTileTerrain, static_cast<std::uint8_t>(
                                     ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : 3 + ctx.rng().Below(6)));
        worlds.Set<std::uint32_t>(kTileRingIndex, ctx.rng().Chance(1, 8) ? kTileRingEntries - 1
                                                                         : ctx.rng().Below(kTileRingEntries));
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
    const std::uint16_t field = At<std::uint16_t>(world, kTiles + (x * 0x300 + y) * 24 + kTileWord16);
    return GameWorld_ReplaceTileWord16AndQueue(world, edx, x, y, field <= 0xff ? expected & 0xff : expected,
                                               value);
}

// `field` is kTileWord16 (2 bytes) or kTileByte10 (1 byte).
void CompareReplaceTile(test::Context& ctx, ReplaceTileFn original, ReplaceTileFn candidate,
                        std::uint32_t field, std::uint32_t size, int cases) {
    Compare(ctx, cases, false, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        const std::uint32_t x = ctx.rng().Below(384);
        const std::uint32_t y = ctx.rng().Below(384);
        const std::uint32_t at = kTiles + (x * 0x300 + y) * 24 + field;
        const std::uint32_t current = ctx.rng().Below(4);
        if (size == 2) {
            worlds.Set<std::uint16_t>(at, static_cast<std::uint16_t>(current));
        } else {
            worlds.Set<std::uint8_t>(at, static_cast<std::uint8_t>(current));
        }
        // Mostly small values (often equal); sometimes high bits set.
        std::uint32_t want = ctx.rng().Below(4);
        if (ctx.rng().Chance(1, 8)) {
            want |= ctx.rng().Next32() & ~0xffu;
        }
        const std::uint32_t value = ctx.rng().Next32();
        worlds.Set<std::uint32_t>(kTileRingIndex, ctx.rng().Chance(1, 8) ? kTileRingEntries - 1
                                                                         : ctx.rng().Below(kTileRingEntries));
        expected = original(worlds.original(), nullptr, x, y, want, value);
        actual = candidate(worlds.ours(), nullptr, x, y, want, value);
    });
}

}  // namespace

S3RE_TEST(GameWorld_ReplaceTileWord16AndQueue_MatchesOriginal) {
    CompareReplaceTile(ctx, Original_GameWorld_ReplaceTileWord16AndQueue,
                       &GameWorld_ReplaceTileWord16AndQueue, kTileWord16, 2, 2000);
}
S3RE_TEST(GameWorld_ReplaceTileWord16AndQueue_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareReplaceTile(probe, Original_GameWorld_ReplaceTileWord16AndQueue, &WrongReplaceTileWord16AndQueue,
                       kTileWord16, 2, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_ReplaceTileByte10AndQueue_MatchesOriginal) {
    CompareReplaceTile(ctx, Original_GameWorld_ReplaceTileByte10AndQueue,
                       &GameWorld_ReplaceTileByte10AndQueue, kTileByte10, 1, 2000);
}

S3RE_TEST(GameWorld_CountDownTimers_MatchesOriginal) {
    Compare(ctx, 300, false, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        // Small timer values (many expire) with a zero somewhere in most lists.
        for (std::uint32_t entry = 0; entry < kTimerLists * kTimersPerList; ++entry) {
            if (ctx.rng().Chance(1, 2)) {
                worlds.Set<std::uint32_t>(kTimers + entry * kTimerBytes, ctx.rng().Below(4));
            }
        }
        expected = Original_GameWorld_CountDownTimers(worlds.original(), nullptr);
        actual = GameWorld_CountDownTimers(worlds.ours(), nullptr);
    });
}

S3RE_TEST(GameWorld_AddToRecentPositions_MatchesOriginal) {
    // The cursor is an S3.EXE global; the test sets it and restores it.
    volatile std::uint16_t& cursor = *reinterpret_cast<volatile std::uint16_t*>(kRecentPositionsCursor);
    const std::uint16_t saved = cursor;
    Compare(ctx, 3000, false, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        cursor = static_cast<std::uint16_t>(ctx.rng().Below(kRecentPositionCount));
        // Small coordinates so that the list contains them now and then;
        // keep at least one empty slot (the original would loop forever).
        for (std::uint32_t slot = 0; slot < kRecentPositionCount; ++slot) {
            const bool empty = ctx.rng().Chance(1, 16);
            worlds.Set<std::uint16_t>(kRecentPositions + slot * 4,
                                      empty ? 0 : static_cast<std::uint16_t>(ctx.rng().Below(8)));
            worlds.Set<std::uint16_t>(kRecentPositions + slot * 4 + 2,
                                      empty ? 0 : static_cast<std::uint16_t>(ctx.rng().Below(8)));
        }
        worlds.Set<std::uint32_t>(kRecentPositions + ctx.rng().Below(kRecentPositionCount) * 4, 0);
        const std::uint32_t x = ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(8);
        const std::uint32_t y = ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(8);
        expected = Original_GameWorld_AddToRecentPositions(worlds.original(), nullptr, x, y);
        actual = GameWorld_AddToRecentPositions(worlds.ours(), nullptr, x, y);
    });
    cursor = saved;
}

}  // namespace s3re::game
