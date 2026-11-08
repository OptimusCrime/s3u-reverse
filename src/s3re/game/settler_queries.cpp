// Settler targets and searches for settlers near a tile.
//
// Settler records: 0x40 bytes each at world+0x12a5e60 (+0x0 player, +0x1
// type, +0x4/+0x6 position, +0x10 next settler in a list, +0x19 a flag
// byte, +0x24/+0x26 target position, +0x2c a byte set to 1 with a new
// target, +0x30..+0x3b six uint16 cleared to 0xffff with it (probably a
// cached path)). Tile (x, y) at world+0x67444 + (x*0x300 + y)*24, its
// uint16 +0x0 is the settler on it.
//
// GameWorld::SetSettlerTarget (0x00503610): (settler, x, y): the target
//   becomes (x, y) (low words) if GameWorld::IsOnMap(x, y), else the
//   settler's own position; then the six words +0x30..+0x3a = 0xffff, byte
//   +0x2c = 1, and byte +0x19 = 1 if it was 0. 45 call sites. EAX (some
//   callers only overwrite AL/AX) = world + settler*0x40, reproduced.
//   ret 0xc.
// GameWorld::CountListSettlersNear (0x0050e8f0): (first, maxDistance, x,
//   y, value) -> how many settlers of the list starting at `first`
//   (linked by +0x10, ends at 0) stand on a tile whose byte +0x10 equals
//   `value` (zero-extended, 32-bit compare) and have TileDistance(x, y,
//   settler position) < maxDistance (unsigned). 0 if first is 0. Called by
//   PrepareDrawData. ret 0x14.
// GameWorld::FindNearbySettlerType29 (0x0050cdb0): (settler, x, y): looks at
//   the tiles (x+dx, y+dy) for the entries 1..36 of the offset table at
//   world+0x110a588 (int32 dx, dy; 16-byte stride; the rings around a
//   tile), skipping those off the map (GameWorld::IsOnMap), and stops at
//   the first one whose settler is non-zero, of type 0x29 and of the same
//   player as `settler`. Returns, as the original does, the settler word of
//   the last tile on the map it looked at: the match if there was one,
//   otherwise whatever settler (or 0) stood on the last tile looked at, or
//   x itself if none of the tiles was on the map. Caller 0x50e9f0.
//   ret 0xc.
// GameWorld::PlaceSettlerNear (0x0050ce80): (settler, x, y): the first of
//   the offset table entries 0..999 whose tile (x+dx, y+dy) is on the map
//   (unsigned compares), has no settler and a non-zero byte +0x10 gets the
//   settler: tile word +0x0 = settler, settler +0xc (uint16) = 0, +0x8
//   (int32) = GameWorld::RandomBelow6(), position +0x4/+0x6 = the tile.
//   EAX = 1 if placed, else 0. ret 0xc.
// Two lists of settlers per player (heads at player record +0x6e and +0x76;
// settlers link with +0x10 next and +0x12 previous) hold settlers waiting
// for work (probably carriers and builders). The two functions below take
// the settler of a list nearest to a target (TileDistance; the first of
// equal ones), unlink it and send it there (GameWorld::SetSettlerTarget):
// GameWorld::SendNearestSettlerToPile (0x004ddbd0): (pile, player, building):
//   list +0x6e, target = the pile's tile (+0x6/+0x8). The pile's byte +0x2
//   (reserved amount?) goes up by one; the settler gets job word +0xe =
//   0x133, +0xc = 0, byte +0x1a = the pile's goods type, byte +0x17 = 0, and
//   if (byte +0x2 & 0xf) != the pile's amount (+0x1) afterwards, byte +0x2
//   goes up once more and byte +0x17 = 1; word +0x22 = building. Nothing
//   happens with an empty list. EAX (ignored by the caller) reproduced.
//   ret 0xc.
// GameWorld::SendNearestSettlerToBuilding (0x004ddd70): (building): list
//   +0x76 of the building's player (byte +0x0); target = two steps from the
//   building's tile (+0x30/+0x32) in direction (word +0x22 + 3) % 6
//   (NeighbourX/NeighbourY twice); only if the nearest settler is less than
//   40 tiles away. The settler gets job word +0xe = 0x147, byte +0x3c = 0,
//   +0xc = 0 and word +0x22 = building. EAX (one caller tail-returns it)
//   reproduced. ret 4.
// GameWorld::RemoveSettlerFromLists (0x0050dff0): (settler): queues event
//   (5, settler, type, player) (GameWorld::QueueEvent; type and player are
//   the settler's bytes +0x1/+0x0), then removes the settler from ten global
//   word lists in S3.EXE's data (uint16 counts at 0x7ad620 + i*2, 100 words
//   each at 0x7ad634 + i*200) and from its player's ten lists in the world
//   (int32 counts at +0x11940bc + player*40 + i*4, 100 words each at
//   +0x118a47c + player*2000 + i*200). A removal moves the rest of the list
//   down one place and the scan goes on at the next index, so of two
//   adjacent copies the second stays (as in the original). Words are
//   compared zero-extended with the full 32-bit settler. EAX (callers only
//   overwrite AL or tail-return it) = 0. ret 4.
// All thiscall (ECX = world).

#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

constexpr std::uint32_t kTiles = 0x67444;
constexpr std::uint32_t kTileSettler = 0x0;
constexpr std::uint32_t kTileByte10 = 0x10;

constexpr std::uint32_t kSettlers = 0x12a5e60;
constexpr std::uint32_t kSettlerBytes = 0x40;
constexpr std::uint32_t kSettlerPlayer = 0x0;
constexpr std::uint32_t kSettlerType = 0x1;
constexpr std::uint32_t kSettlerX = 0x4;
constexpr std::uint32_t kSettlerState = 0x8;  // int32
constexpr std::uint32_t kSettlerWordC = 0xc;
constexpr std::uint32_t kSettlerY = 0x6;
constexpr std::uint32_t kSettlerNext = 0x10;
constexpr std::uint32_t kSettlerPrevious = 0x12;
constexpr std::uint32_t kSettlerJob = 0xe;
constexpr std::uint32_t kSettlerByte17 = 0x17;
constexpr std::uint32_t kSettlerGoods = 0x1a;
constexpr std::uint32_t kSettlerWorkplace = 0x22;
constexpr std::uint32_t kSettlerByte3C = 0x3c;
constexpr std::uint32_t kSettlerByte19 = 0x19;
constexpr std::uint32_t kSettlerTargetX = 0x24;
constexpr std::uint32_t kSettlerTargetY = 0x26;
constexpr std::uint32_t kSettlerNewTarget = 0x2c;
constexpr std::uint32_t kSettlerPath = 0x30;  // 6 x uint16
constexpr std::uint32_t kSettlerPathWords = 6;
constexpr std::uint8_t kSettlerType29 = 0x29;

constexpr std::uint32_t kPlayerRecords = 0x11943dc;
constexpr std::uint32_t kPlayerRecordBytes = 0x148;
constexpr std::uint32_t kPlayerWaitingA = 0x6e;  // uint16 head
constexpr std::uint32_t kPlayerWaitingB = 0x76;  // uint16 head

constexpr std::uint32_t kPiles = 0x123885c;
constexpr std::uint32_t kPileBytes = 14;
constexpr std::uint32_t kPileGoods = 0x0;
constexpr std::uint32_t kPileAmount = 0x1;
constexpr std::uint32_t kPileReserved = 0x2;
constexpr std::uint32_t kPileX = 0x6;
constexpr std::uint32_t kPileY = 0x8;

constexpr std::uint32_t kBuildings = 0x1499e64;
constexpr std::uint32_t kBuildingBytes = 0x3a;
constexpr std::uint32_t kBuildingPlayer = 0x0;
constexpr std::uint32_t kBuildingDirection = 0x22;
constexpr std::uint32_t kBuildingDoorX = 0x30;
constexpr std::uint32_t kBuildingDoorY = 0x32;

constexpr std::uint32_t kOffsetTable = 0x110a588;  // int32 dx, int32 dy, 16-byte stride
constexpr std::uint32_t kOffsetTableBytes = 0x10;
constexpr std::uint32_t kRingEntriesFirst = 1;
constexpr std::uint32_t kRingEntriesLast = 36;
constexpr std::uint32_t kPlaceEntries = 1000;

template <typename T>
T& At(GameWorld* world, std::uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<std::uint8_t*>(world) + offset);
}

std::uint32_t Tile(std::uint32_t x, std::uint32_t y) {
    return kTiles + (x * 0x300 + y) * 24;
}

std::uint32_t Settler(std::uint32_t settler) {
    return kSettlers + settler * kSettlerBytes;
}

}  // namespace

using SetTargetFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t settler,
                                               std::uint32_t x, std::uint32_t y);
using CountNearFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t first,
                                               std::uint32_t maxDistance, std::uint32_t x,
                                               std::uint32_t y, std::uint32_t value);
using FindNearbyFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t settler,
                                                std::uint32_t x, std::uint32_t y);

std::uint32_t __fastcall GameWorld_SetSettlerTarget(GameWorld* world, void*, std::uint32_t settler,
                                                    std::uint32_t x, std::uint32_t y) {
    const std::uint32_t record = Settler(settler);
    if (GameWorld_IsOnMap(world, nullptr, x, y)) {
        At<std::uint16_t>(world, record + kSettlerTargetX) = static_cast<std::uint16_t>(x);
        At<std::uint16_t>(world, record + kSettlerTargetY) = static_cast<std::uint16_t>(y);
    } else {
        At<std::uint16_t>(world, record + kSettlerTargetX) = At<std::uint16_t>(world, record + kSettlerX);
        At<std::uint16_t>(world, record + kSettlerTargetY) = At<std::uint16_t>(world, record + kSettlerY);
    }
    for (std::uint32_t i = 0; i < kSettlerPathWords; ++i) {
        At<std::uint16_t>(world, record + kSettlerPath + i * 2) = 0xffff;
    }
    At<std::uint8_t>(world, record + kSettlerNewTarget) = 1;
    if (At<std::uint8_t>(world, record + kSettlerByte19) == 0) {
        At<std::uint8_t>(world, record + kSettlerByte19) = 1;
    }
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world)) + (settler << 6);
}

std::uint32_t __fastcall GameWorld_CountListSettlersNear(GameWorld* world, void*, std::uint32_t first,
                                                         std::uint32_t maxDistance, std::uint32_t x,
                                                         std::uint32_t y, std::uint32_t value) {
    std::uint32_t count = 0;
    for (std::uint32_t settler = first; settler != 0;
         settler = At<std::uint16_t>(world, Settler(settler) + kSettlerNext)) {
        const std::uint32_t sx = At<std::uint16_t>(world, Settler(settler) + kSettlerX);
        const std::uint32_t sy = At<std::uint16_t>(world, Settler(settler) + kSettlerY);
        if (At<std::uint8_t>(world, Tile(sx, sy) + kTileByte10) == value &&
            TileDistance(x, y, sx, sy) < maxDistance) {
            ++count;
        }
    }
    return count;
}

std::uint32_t __fastcall GameWorld_FindNearbySettlerType29(GameWorld* world, void*, std::uint32_t settler,
                                                           std::uint32_t x, std::uint32_t y) {
    std::uint32_t last = x;  // the original's register, see above
    for (std::uint32_t entry = kRingEntriesFirst; entry <= kRingEntriesLast; ++entry) {
        const std::uint32_t at = kOffsetTable + entry * kOffsetTableBytes;
        const std::uint32_t tx = x + At<std::uint32_t>(world, at);
        const std::uint32_t ty = y + At<std::uint32_t>(world, at + 4);
        if (!GameWorld_IsOnMap(world, nullptr, tx, ty)) {
            continue;
        }
        last = At<std::uint16_t>(world, Tile(tx, ty) + kTileSettler);
        if (last != 0 && At<std::uint8_t>(world, Settler(last) + kSettlerType) == kSettlerType29 &&
            At<std::uint8_t>(world, Settler(last) + kSettlerPlayer) ==
                At<std::uint8_t>(world, Settler(settler) + kSettlerPlayer)) {
            break;
        }
    }
    return last;
}

S3RE_HOOK(0x00503610, GameWorld_SetSettlerTarget, SetTargetFn, Original_GameWorld_SetSettlerTarget,
          GameWorld_SetSettlerTarget);
S3RE_HOOK(0x0050e8f0, GameWorld_CountListSettlersNear, CountNearFn, Original_GameWorld_CountListSettlersNear,
          GameWorld_CountListSettlersNear);
S3RE_HOOK(0x0050cdb0, GameWorld_FindNearbySettlerType29, FindNearbyFn,
          Original_GameWorld_FindNearbySettlerType29, GameWorld_FindNearbySettlerType29);

using PlaceNearFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t settler,
                                               std::uint32_t x, std::uint32_t y);

std::uint32_t __fastcall GameWorld_PlaceSettlerNear(GameWorld* world, void*, std::uint32_t settler, std::uint32_t x,
                                                    std::uint32_t y) {
    for (std::uint32_t entry = 0; entry < kPlaceEntries; ++entry) {
        const std::uint32_t at = kOffsetTable + entry * kOffsetTableBytes;
        const std::uint32_t tx = x + At<std::uint32_t>(world, at);
        const std::uint32_t ty = y + At<std::uint32_t>(world, at + 4);
        const std::uint32_t size = At<std::uint32_t>(world, world_offset::kMapSize);
        if (tx >= size || ty >= size) {
            continue;
        }
        const std::uint32_t tile = Tile(tx, ty);
        if (At<std::uint16_t>(world, tile + kTileSettler) != 0 || At<std::uint8_t>(world, tile + kTileByte10) == 0) {
            continue;
        }
        At<std::uint16_t>(world, tile + kTileSettler) = static_cast<std::uint16_t>(settler);
        const std::uint32_t record = Settler(settler);
        At<std::uint16_t>(world, record + kSettlerWordC) = 0;
        At<std::uint32_t>(world, record + kSettlerState) = GameWorld_RandomBelow6(world, nullptr);
        At<std::uint16_t>(world, record + kSettlerX) = static_cast<std::uint16_t>(tx);
        At<std::uint16_t>(world, record + kSettlerY) = static_cast<std::uint16_t>(ty);
        return 1;
    }
    return 0;
}

S3RE_HOOK(0x0050ce80, GameWorld_PlaceSettlerNear, PlaceNearFn, Original_GameWorld_PlaceSettlerNear,
          GameWorld_PlaceSettlerNear);

namespace {

// The nearest settler of the list at `head` to (x, y); 0 if the list is
// empty. `distance` gets its distance, `last` the last distance computed.
std::uint32_t NearestInList(GameWorld* world, std::uint32_t head, std::uint32_t x, std::uint32_t y,
                            std::uint32_t& distance, std::uint32_t& last) {
    std::uint32_t best = 0;
    distance = 0xffffffffu;
    for (std::uint32_t settler = At<std::uint16_t>(world, head); settler != 0;
         settler = At<std::uint16_t>(world, Settler(settler) + kSettlerNext)) {
        last = TileDistance(x, y, At<std::uint16_t>(world, Settler(settler) + kSettlerX),
                            At<std::uint16_t>(world, Settler(settler) + kSettlerY));
        if (last < distance) {
            distance = last;
            best = settler;
        }
    }
    return best;
}

void Unlink(GameWorld* world, std::uint32_t head, std::uint32_t settler) {
    const std::uint32_t record = Settler(settler);
    const std::uint32_t previous = At<std::uint16_t>(world, record + kSettlerPrevious);
    if (previous != 0) {
        At<std::uint16_t>(world, Settler(previous) + kSettlerNext) = At<std::uint16_t>(world, record + kSettlerNext);
    } else {
        At<std::uint16_t>(world, head) = At<std::uint16_t>(world, record + kSettlerNext);
    }
    const std::uint32_t next = At<std::uint16_t>(world, record + kSettlerNext);
    if (next != 0) {
        At<std::uint16_t>(world, Settler(next) + kSettlerPrevious) = At<std::uint16_t>(world, record + kSettlerPrevious);
    }
}

}  // namespace

using SendToPileFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t pile, std::uint32_t player,
                                                std::uint32_t building);
using SendToBuildingFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t building);

std::uint32_t __fastcall GameWorld_SendNearestSettlerToPile(GameWorld* world, void*, std::uint32_t pile,
                                                            std::uint32_t player, std::uint32_t building) {
    const std::uint32_t head = kPlayerRecords + player * kPlayerRecordBytes + kPlayerWaitingA;
    if (At<std::uint16_t>(world, head) == 0) {
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world)) + head;
    }
    const std::uint32_t record = kPiles + pile * kPileBytes;
    const std::uint32_t px = At<std::uint16_t>(world, record + kPileX);
    const std::uint32_t py = At<std::uint16_t>(world, record + kPileY);
    std::uint32_t distance = 0;
    std::uint32_t last = 0;
    const std::uint32_t best = NearestInList(world, head, px, py, distance, last);
    if (best == 0) {
        return last;
    }
    std::uint8_t& reserved = At<std::uint8_t>(world, record + kPileReserved);
    reserved = static_cast<std::uint8_t>(reserved + 1);
    Unlink(world, head, best);
    GameWorld_SetSettlerTarget(world, nullptr, best, At<std::uint16_t>(world, record + kPileX),
                               At<std::uint16_t>(world, record + kPileY));
    const std::uint32_t settler = Settler(best);
    At<std::uint16_t>(world, settler + kSettlerJob) = 0x133;
    At<std::uint16_t>(world, settler + kSettlerWordC) = 0;
    At<std::uint8_t>(world, settler + kSettlerGoods) = At<std::uint8_t>(world, record + kPileGoods);
    At<std::uint8_t>(world, settler + kSettlerByte17) = 0;
    if (At<std::uint8_t>(world, record + kPileAmount) != (reserved & 0x0f)) {
        reserved = static_cast<std::uint8_t>(reserved + 1);
        At<std::uint8_t>(world, settler + kSettlerByte17) = 1;
    }
    At<std::uint16_t>(world, settler + kSettlerWorkplace) = static_cast<std::uint16_t>(building);
    return ((pile + 0x14d2e2u) & 0xffffff00u) | reserved;
}

std::uint32_t __fastcall GameWorld_SendNearestSettlerToBuilding(GameWorld* world, void*, std::uint32_t building) {
    const std::uint32_t record = kBuildings + building * kBuildingBytes;
    const std::uint32_t direction = (At<std::uint16_t>(world, record + kBuildingDirection) + 3u) % 6u;
    const std::uint32_t x1 = NeighbourX(At<std::uint16_t>(world, record + kBuildingDoorX), direction);
    const std::uint32_t y1 = NeighbourY(At<std::uint16_t>(world, record + kBuildingDoorY), direction);
    const std::uint32_t x2 = NeighbourX(x1, direction);
    const std::uint32_t y2 = NeighbourY(y1, direction);
    const std::uint32_t player = At<std::uint8_t>(world, record + kBuildingPlayer);
    const std::uint32_t head = kPlayerRecords + player * kPlayerRecordBytes + kPlayerWaitingB;
    if (At<std::uint16_t>(world, head) == 0) {
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world)) + head;
    }
    std::uint32_t distance = 0;
    std::uint32_t last = 0;
    const std::uint32_t best = NearestInList(world, head, x2, y2, distance, last);
    if (best == 0 || distance >= 40) {
        return last;
    }
    Unlink(world, head, best);
    const std::uint32_t result = GameWorld_SetSettlerTarget(world, nullptr, best, x2, y2);
    const std::uint32_t settler = Settler(best);
    At<std::uint16_t>(world, settler + kSettlerJob) = 0x147;
    At<std::uint8_t>(world, settler + kSettlerByte3C) = 0;
    At<std::uint16_t>(world, settler + kSettlerWordC) = 0;
    At<std::uint16_t>(world, settler + kSettlerWorkplace) = static_cast<std::uint16_t>(building);
    return (result & 0xffff0000u) | (building & 0xffffu);
}

S3RE_HOOK(0x004ddbd0, GameWorld_SendNearestSettlerToPile, SendToPileFn, Original_GameWorld_SendNearestSettlerToPile,
          GameWorld_SendNearestSettlerToPile);
S3RE_HOOK(0x004ddd70, GameWorld_SendNearestSettlerToBuilding, SendToBuildingFn,
          Original_GameWorld_SendNearestSettlerToBuilding, GameWorld_SendNearestSettlerToBuilding);

namespace {

constexpr std::uintptr_t kGlobalListCounts = 0x007ad620;  // uint16[10], S3.EXE data
constexpr std::uintptr_t kGlobalLists = 0x007ad634;       // uint16[10][100]
constexpr std::uint32_t kListCount = 10;
constexpr std::uint32_t kListWords = 100;
constexpr std::uint32_t kPlayerListCounts = 0x11940bc;  // int32[20][10]
constexpr std::uint32_t kPlayerLists = 0x118a47c;       // uint16[20][10][100]

// Removes `value` from words[0..count) as the original does; `count` is
// re-read through the reference.
template <typename Count>
void RemoveFromList(std::uint16_t* words, Count& count, std::uint32_t value) {
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(count); ++i) {
        if (words[i] != value) {
            continue;
        }
        const std::uint32_t last = static_cast<std::uint32_t>(count) - 1;
        for (std::uint32_t j = i; j < last; ++j) {
            words[j] = words[j + 1];
        }
        count = static_cast<Count>(count - 1);
    }
}

}  // namespace

using RemoveSettlerFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t settler);

std::uint32_t __fastcall GameWorld_RemoveSettlerFromLists(GameWorld* world, void*, std::uint32_t settler) {
    const std::uint32_t record = Settler(settler);
    GameWorld_QueueEvent(world, nullptr, 5, settler, At<std::uint8_t>(world, record + kSettlerType),
                         At<std::uint8_t>(world, record + kSettlerPlayer));
    const std::uint32_t player = At<std::uint8_t>(world, record + kSettlerPlayer);
    for (std::uint32_t list = 0; list < kListCount; ++list) {
        auto& count = *reinterpret_cast<std::uint16_t*>(kGlobalListCounts + list * 2);
        RemoveFromList(reinterpret_cast<std::uint16_t*>(kGlobalLists + list * kListWords * 2), count, settler);
    }
    for (std::uint32_t list = 0; list < kListCount; ++list) {
        auto& count = At<std::uint32_t>(world, kPlayerListCounts + player * (kListCount * 4) + list * 4);
        RemoveFromList(&At<std::uint16_t>(world, kPlayerLists + player * (kListCount * kListWords * 2) + list * kListWords * 2),
                       count, settler);
    }
    return 0;
}

S3RE_HOOK(0x0050dff0, GameWorld_RemoveSettlerFromLists, RemoveSettlerFn, Original_GameWorld_RemoveSettlerFromLists,
          GameWorld_RemoveSettlerFromLists);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestSettlers = 0x400;
constexpr std::size_t kTestWorld = kSettlers + 0x10000 * kSettlerBytes;

// Runs `cases` cases; `call` prepares both worlds and returns both EAX values.
template <typename Call>
void Compare(test::Context& ctx, int cases, Call call) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        std::uint32_t expected = 0;
        std::uint32_t actual = 0;
        call(worlds, expected, actual);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 64 == 63 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 63 before it)", i);
            return;
        }
    }
}

std::uint32_t Relative(std::uint32_t value, GameWorld* world) {
    return value - static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));
}

// The real offset table: the tile, then rings of growing distance (only the
// first 37 entries are set; the order within a ring doesn't matter here).
void SetUpRings(test::Context& ctx, test::WorldPair& worlds) {
    std::uint32_t entry = 0;
    for (std::int32_t radius = 0; entry <= kRingEntriesLast; ++radius) {
        for (std::int32_t dx = -radius; dx <= radius && entry <= kRingEntriesLast; ++dx) {
            for (std::int32_t dy = -radius; dy <= radius && entry <= kRingEntriesLast; ++dy) {
                if ((dx < 0 ? -dx : dx) != radius && (dy < 0 ? -dy : dy) != radius) {
                    continue;
                }
                const bool random = ctx.rng().Chance(1, 32);
                worlds.Set<std::int32_t>(kOffsetTable + entry * kOffsetTableBytes, random ? ctx.rng().Range(-9, 9) : dx);
                worlds.Set<std::int32_t>(kOffsetTable + entry * kOffsetTableBytes + 4,
                                         random ? ctx.rng().Range(-9, 9) : dy);
                ++entry;
            }
        }
    }
}

// Wrong: returns the match or 0 (what the code probably meant).
std::uint32_t __fastcall WrongFindNearbySettlerType29(GameWorld* world, void* edx, std::uint32_t settler,
                                                      std::uint32_t x, std::uint32_t y) {
    const std::uint32_t last = GameWorld_FindNearbySettlerType29(world, edx, settler, x, y);
    if (last == 0 || last == x) {
        return last;
    }
    const bool match = At<std::uint8_t>(world, Settler(last) + kSettlerType) == kSettlerType29 &&
                       At<std::uint8_t>(world, Settler(last) + kSettlerPlayer) ==
                           At<std::uint8_t>(world, Settler(settler) + kSettlerPlayer);
    return match ? last : 0;
}

void CompareFindNearby(test::Context& ctx, FindNearbyFn candidate, int cases) {
    Compare(ctx, cases, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        const std::uint32_t size = ctx.rng().Chance(1, 2) ? 384 : 4 + ctx.rng().Below(30);
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
        SetUpRings(ctx, worlds);
        std::uint32_t x = ctx.rng().Below(size);
        std::uint32_t y = ctx.rng().Below(size);
        if (ctx.rng().Chance(1, 16)) {
            x += 400;  // nothing on the map
        }
        const std::uint32_t settler = 1 + ctx.rng().Below(kTestSettlers - 1);
        worlds.Set<std::uint8_t>(Settler(settler) + kSettlerPlayer, static_cast<std::uint8_t>(ctx.rng().Below(3)));
        const std::uint32_t sparse = ctx.rng().Below(4);
        for (std::int32_t dx = -5; dx <= 5; ++dx) {
            for (std::int32_t dy = -5; dy <= 5; ++dy) {
                const std::uint32_t tx = x + dx;
                const std::uint32_t ty = y + dy;
                if (tx >= size || ty >= size) {
                    continue;
                }
                const std::uint32_t other = ctx.rng().Below(4) < sparse ? 0 : 1 + ctx.rng().Below(kTestSettlers - 1);
                worlds.Set<std::uint16_t>(Tile(tx, ty) + kTileSettler, static_cast<std::uint16_t>(other));
                if (other != 0) {
                    worlds.Set<std::uint8_t>(Settler(other) + kSettlerType,
                                             ctx.rng().Chance(1, 6) ? kSettlerType29 : static_cast<std::uint8_t>(ctx.rng().Below(0x40)));
                    worlds.Set<std::uint8_t>(Settler(other) + kSettlerPlayer, static_cast<std::uint8_t>(ctx.rng().Below(3)));
                }
            }
        }
        expected = Original_GameWorld_FindNearbySettlerType29(worlds.original(), nullptr, settler, x, y);
        actual = candidate(worlds.ours(), nullptr, settler, x, y);
    });
}

}  // namespace

S3RE_TEST(GameWorld_SetSettlerTarget_MatchesOriginal) {
    Compare(ctx, 5000, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        const std::uint32_t size = ctx.rng().Chance(1, 2) ? 384 : ctx.rng().Below(64);
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
        const std::uint32_t settler = ctx.rng().Below(0x10000);
        std::uint32_t x = ctx.rng().Below(400);
        std::uint32_t y = ctx.rng().Below(400);
        if (ctx.rng().Chance(1, 8)) {
            x = ctx.rng().Next32();
        }
        worlds.Set<std::uint8_t>(Settler(settler) + kSettlerByte19, static_cast<std::uint8_t>(ctx.rng().Below(3)));
        expected = Relative(Original_GameWorld_SetSettlerTarget(worlds.original(), nullptr, settler, x, y),
                            worlds.original());
        actual = Relative(GameWorld_SetSettlerTarget(worlds.ours(), nullptr, settler, x, y), worlds.ours());
    });
}

S3RE_TEST(GameWorld_CountListSettlersNear_MatchesOriginal) {
    Compare(ctx, 3000, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        // A list of up to 20 settlers on tiles near (x, y), some with the value.
        const std::uint32_t x = ctx.rng().Below(384);
        const std::uint32_t y = ctx.rng().Below(384);
        const std::uint32_t value = ctx.rng().Below(3);
        const std::uint32_t length = ctx.rng().Below(20);
        std::uint32_t first = 0;
        for (std::uint32_t i = 0; i < length; ++i) {
            const std::uint32_t settler = 1 + ctx.rng().Below(kTestSettlers - 1);
            const std::uint32_t sx = static_cast<std::uint32_t>(ctx.rng().Range(0, 383));
            const std::uint32_t sy = ctx.rng().Chance(1, 2) ? static_cast<std::uint32_t>(ctx.rng().Range(0, 383))
                                                            : y + ctx.rng().Below(10) - (y > 9 ? 5 : 0);
            worlds.Set<std::uint16_t>(Settler(settler) + kSettlerX, static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? x : sx));
            worlds.Set<std::uint16_t>(Settler(settler) + kSettlerY, static_cast<std::uint16_t>(sy % 384));
            worlds.Set<std::uint16_t>(Settler(settler) + kSettlerNext, static_cast<std::uint16_t>(first));
            const std::uint32_t tile = Tile(At<std::uint16_t>(worlds.original(), Settler(settler) + kSettlerX),
                                            At<std::uint16_t>(worlds.original(), Settler(settler) + kSettlerY));
            worlds.Set<std::uint8_t>(tile + kTileByte10,
                                     static_cast<std::uint8_t>(ctx.rng().Chance(2, 3) ? value : ctx.rng().Below(256)));
            first = settler;
        }
        // A list may loop through an earlier settler; make it end.
        const std::uint32_t maxDistance = ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(400);
        // Walk the list as built (it can contain the same settler twice and loop);
        // only call when it ends within 64 steps.
        std::uint32_t steps = 0;
        for (std::uint32_t s = first; s != 0 && steps < 64; s = At<std::uint16_t>(worlds.original(), Settler(s) + kSettlerNext)) {
            ++steps;
        }
        if (steps == 64) {
            first = 0;
        }
        expected = Original_GameWorld_CountListSettlersNear(worlds.original(), nullptr, first, maxDistance, x, y, value);
        actual = GameWorld_CountListSettlersNear(worlds.ours(), nullptr, first, maxDistance, x, y, value);
    });
}

S3RE_TEST(GameWorld_FindNearbySettlerType29_MatchesOriginal) {
    CompareFindNearby(ctx, &GameWorld_FindNearbySettlerType29, 3000);
}
S3RE_TEST(GameWorld_FindNearbySettlerType29_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareFindNearby(probe, &WrongFindNearbySettlerType29, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_PlaceSettlerNear_MatchesOriginal) {
    Compare(ctx, 3000, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        const std::uint32_t size = ctx.rng().Chance(1, 2) ? 384 : 4 + ctx.rng().Below(40);
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
        std::uint32_t x = ctx.rng().Below(size);
        const std::uint32_t y = ctx.rng().Below(size);
        if (ctx.rng().Chance(1, 32)) {
            x += 1000;  // nothing on the map
        }
        // Offsets: entries 0..999 spiral out to about 18 tiles.
        const std::uint32_t entries = ctx.rng().Chance(1, 8) ? kPlaceEntries : 80;
        for (std::uint32_t entry = 0; entry < entries; ++entry) {
            const std::int32_t reach = 1 + static_cast<std::int32_t>(entry / 60);
            worlds.Set<std::int32_t>(kOffsetTable + entry * kOffsetTableBytes, ctx.rng().Range(-reach, reach));
            worlds.Set<std::int32_t>(kOffsetTable + entry * kOffsetTableBytes + 4, ctx.rng().Range(-reach, reach));
        }
        // Tiles near (x, y): mostly occupied or without byte +0x10.
        const std::uint32_t free = ctx.rng().Below(64);
        for (std::int32_t dx = -20; dx <= 20; ++dx) {
            for (std::int32_t dy = -20; dy <= 20; ++dy) {
                const std::uint32_t tx = x + dx;
                const std::uint32_t ty = y + dy;
                if (tx >= 384 || ty >= 384) {
                    continue;
                }
                const bool empty = ctx.rng().Below(1024) < free;
                worlds.Set<std::uint16_t>(Tile(tx, ty) + kTileSettler,
                                          static_cast<std::uint16_t>(empty || ctx.rng().Chance(1, 2) ? 0 : 1 + ctx.rng().Below(100)));
                worlds.Set<std::uint8_t>(Tile(tx, ty) + kTileByte10,
                                         static_cast<std::uint8_t>(empty || ctx.rng().Chance(1, 2) ? 1 + ctx.rng().Below(3) : 0));
            }
        }
        worlds.Set<std::uint32_t>(world_offset::kRandomState, ctx.rng().Next32());
        const std::uint32_t settler = ctx.rng().Below(kTestSettlers);
        expected = Original_GameWorld_PlaceSettlerNear(worlds.original(), nullptr, settler, x, y);
        actual = GameWorld_PlaceSettlerNear(worlds.ours(), nullptr, settler, x, y);
    });
}

namespace {

// A doubly linked list of up to 12 distinct settlers at `head`, near (x, y).
void SetUpWaitingList(test::Context& ctx, test::WorldPair& worlds, std::uint32_t head, std::uint32_t x,
                      std::uint32_t y) {
    std::uint32_t used[16] = {};
    const std::uint32_t length = ctx.rng().Chance(1, 8) ? 0 : 1 + ctx.rng().Below(12);
    for (std::uint32_t i = 0; i < length; ++i) {
        std::uint32_t settler = 0;
        bool fresh = false;
        while (!fresh) {
            settler = 1 + ctx.rng().Below(kTestSettlers - 1);
            fresh = true;
            for (std::uint32_t j = 0; j < i; ++j) {
                fresh = fresh && used[j] != settler;
            }
        }
        used[i] = settler;
    }
    worlds.Set<std::uint16_t>(head, static_cast<std::uint16_t>(length ? used[0] : 0));
    for (std::uint32_t i = 0; i < length; ++i) {
        const std::uint32_t record = Settler(used[i]);
        worlds.Set<std::uint16_t>(record + kSettlerPrevious, static_cast<std::uint16_t>(i ? used[i - 1] : 0));
        worlds.Set<std::uint16_t>(record + kSettlerNext, static_cast<std::uint16_t>(i + 1 < length ? used[i + 1] : 0));
        const std::int32_t reach = ctx.rng().Chance(1, 2) ? 8 : 60;
        worlds.Set<std::uint16_t>(record + kSettlerX,
                                  static_cast<std::uint16_t>((x + ctx.rng().Range(-reach, reach)) & 0xffff));
        worlds.Set<std::uint16_t>(record + kSettlerY,
                                  static_cast<std::uint16_t>((y + ctx.rng().Range(-reach, reach)) & 0xffff));
    }
}

std::uint32_t RelativeIfPointer(std::uint32_t value, GameWorld* world) {
    const auto base = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));
    return value - base < kTestWorld ? value - base : value;
}

}  // namespace

S3RE_TEST(GameWorld_SendNearestSettlerToPile_MatchesOriginal) {
    Compare(ctx, 3000, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        worlds.Set<std::uint32_t>(world_offset::kMapSize, 384);
        const std::uint32_t pile = 1 + ctx.rng().Below(0x800);
        const std::uint32_t player = ctx.rng().Below(20);
        const std::uint32_t x = ctx.rng().Below(384);
        const std::uint32_t y = ctx.rng().Below(384);
        const std::uint32_t record = kPiles + pile * kPileBytes;
        worlds.Set<std::uint16_t>(record + kPileX, static_cast<std::uint16_t>(x));
        worlds.Set<std::uint16_t>(record + kPileY, static_cast<std::uint16_t>(y));
        worlds.Set<std::uint8_t>(record + kPileAmount, static_cast<std::uint8_t>(ctx.rng().Below(10)));
        worlds.Set<std::uint8_t>(record + kPileReserved, static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? ctx.rng().Below(10) : ctx.rng().Next32()));
        SetUpWaitingList(ctx, worlds, kPlayerRecords + player * kPlayerRecordBytes + kPlayerWaitingA, x, y);
        const std::uint32_t building = ctx.rng().Next32();
        expected = RelativeIfPointer(
            Original_GameWorld_SendNearestSettlerToPile(worlds.original(), nullptr, pile, player, building), worlds.original());
        actual = RelativeIfPointer(GameWorld_SendNearestSettlerToPile(worlds.ours(), nullptr, pile, player, building),
                                   worlds.ours());
    });
}

S3RE_TEST(GameWorld_SendNearestSettlerToBuilding_MatchesOriginal) {
    Compare(ctx, 3000, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        worlds.Set<std::uint32_t>(world_offset::kMapSize, 384);
        const std::uint32_t building = 1 + ctx.rng().Below(0x800);
        const std::uint32_t record = kBuildings + building * kBuildingBytes;
        const std::uint32_t player = ctx.rng().Below(20);
        const std::uint32_t x = 2 + ctx.rng().Below(380);
        const std::uint32_t y = 2 + ctx.rng().Below(380);
        worlds.Set<std::uint8_t>(record + kBuildingPlayer, static_cast<std::uint8_t>(player));
        worlds.Set<std::uint16_t>(record + kBuildingDoorX, static_cast<std::uint16_t>(x));
        worlds.Set<std::uint16_t>(record + kBuildingDoorY, static_cast<std::uint16_t>(y));
        worlds.Set<std::uint16_t>(record + kBuildingDirection, static_cast<std::uint16_t>(ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(6)));
        SetUpWaitingList(ctx, worlds, kPlayerRecords + player * kPlayerRecordBytes + kPlayerWaitingB, x, y);
        expected = RelativeIfPointer(Original_GameWorld_SendNearestSettlerToBuilding(worlds.original(), nullptr, building),
                                     worlds.original());
        actual = RelativeIfPointer(GameWorld_SendNearestSettlerToBuilding(worlds.ours(), nullptr, building), worlds.ours());
    });
}

namespace {

constexpr std::size_t kGlobalListBytes = kListCount * 2 + kListCount * kListWords * 2;  // 0x7ad620..0x7ade04

// Saves the global lists, sets them per case and restores them at the end.
class GlobalLists {
public:
    GlobalLists() { std::memcpy(saved_, Base(), sizeof(saved_)); }
    ~GlobalLists() { std::memcpy(Base(), saved_, sizeof(saved_)); }
    static std::uint8_t* Base() { return reinterpret_cast<std::uint8_t*>(kGlobalListCounts); }

private:
    std::uint8_t saved_[kGlobalListBytes];
};

// A list of up to 100 words with some copies of `settler` (sometimes
// adjacent), the rest other small numbers.
template <typename Count>
void FillList(test::Context& ctx, std::uint16_t* words, Count& count, std::uint32_t settler) {
    count = static_cast<Count>(ctx.rng().Chance(1, 8) ? 0 : ctx.rng().Below(kListWords + 1));
    for (std::uint32_t i = 0; i < kListWords; ++i) {
        words[i] = static_cast<std::uint16_t>(ctx.rng().Chance(1, 6) ? settler : ctx.rng().Below(8));
    }
}

// Wrong: rescans the moved-down element (removes adjacent copies too).
std::uint32_t __fastcall WrongRemoveSettlerFromLists(GameWorld* world, void* edx, std::uint32_t settler) {
    const std::uint32_t player = At<std::uint8_t>(world, Settler(settler) + kSettlerPlayer);
    GameWorld_RemoveSettlerFromLists(world, edx, settler);
    for (std::uint32_t list = 0; list < kListCount; ++list) {
        auto& count = *reinterpret_cast<std::uint16_t*>(kGlobalListCounts + list * 2);
        RemoveFromList(reinterpret_cast<std::uint16_t*>(kGlobalLists + list * kListWords * 2), count, settler);
    }
    (void)player;  // the player lists are left as they are
    return 0;
}

void CompareRemoveSettler(test::Context& ctx, RemoveSettlerFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    GlobalLists restore;
    std::uint8_t globalsBefore[kGlobalListBytes];
    std::uint8_t globalsOriginal[kGlobalListBytes];
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t settler = 1 + ctx.rng().Below(8);
        const std::uint32_t record = Settler(settler);
        const std::uint32_t player = ctx.rng().Below(20);
        worlds.Set<std::uint8_t>(record + kSettlerPlayer, static_cast<std::uint8_t>(player));
        // Event rings as QueueEvent expects (write index in range).
        worlds.Set<std::uint32_t>(0x366fc, ctx.rng().Below(1000));
        worlds.Set<std::uint32_t>(0x32870, 0);
        worlds.Set<std::uint32_t>(0x32874, 0);
        for (std::uint32_t list = 0; list < kListCount; ++list) {
            FillList(ctx, reinterpret_cast<std::uint16_t*>(kGlobalLists + list * kListWords * 2),
                     *reinterpret_cast<std::uint16_t*>(kGlobalListCounts + list * 2), settler);
            std::uint32_t count = 0;
            std::uint16_t words[kListWords];
            FillList(ctx, words, count, settler);
            worlds.Set<std::uint32_t>(kPlayerListCounts + player * 40 + list * 4, count);
            for (std::uint32_t w = 0; w < kListWords; ++w) {
                worlds.Set<std::uint16_t>(kPlayerLists + player * 2000 + list * 200 + w * 2, words[w]);
            }
        }
        std::memcpy(globalsBefore, GlobalLists::Base(), kGlobalListBytes);
        const std::uint32_t expected = Original_GameWorld_RemoveSettlerFromLists(worlds.original(), nullptr, settler);
        std::memcpy(globalsOriginal, GlobalLists::Base(), kGlobalListBytes);
        std::memcpy(GlobalLists::Base(), globalsBefore, kGlobalListBytes);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, settler);
        if (!S3RE_EXPECT_EQ(expected, actual) ||
            !ctx.ExpectMemEq(globalsOriginal, GlobalLists::Base(), kGlobalListBytes, "original globals", "our globals",
                             __FILE__, __LINE__) ||
            ((i % 16 == 15 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 15 before it): settler %u", i, settler);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(GameWorld_RemoveSettlerFromLists_MatchesOriginal) {
    CompareRemoveSettler(ctx, &GameWorld_RemoveSettlerFromLists, 500);
}
S3RE_TEST(GameWorld_RemoveSettlerFromLists_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareRemoveSettler(probe, &WrongRemoveSettlerFromLists, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
