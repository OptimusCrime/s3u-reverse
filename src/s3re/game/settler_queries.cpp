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
// GameWorld::SendNearestWorkerToBuilding (0x004db500): (building, list,
//   maxDistance): from the player's settler list `list` (uint16 heads at
//   player record +0x20 + list*2; the player is the building's byte +0x0;
//   linked +0x10/+0x12), the settler nearest to the building's word pair
//   +0x2/+0x4 (TileDistance, unsigned, less than maxDistance) among those
//   standing in the region (tile byte +0x10) of the building's tile
//   (+0xa/+0xc); the first one wins a tie. It is unlinked,
//   GameWorld::RemoveSettlerFromLists, sent to the building's tile
//   (GameWorld::SetSettlerTarget) and gets job word +0xe = 3, +0xc = 0 and
//   word +0x22 = building. EAX = that settler, or 0 if none. (The original
//   also walks the list to its end twice without using the result; that is
//   left out.) ret 0xc.
// GameWorld::FetchGoodsToTradeBuilding (0x004de0a0): (area, building,
//   goods): picks the pile of `goods` in the area's list for that goods type
//   (area record at world+0xe145e8 + area*0x2e0, uint16 heads at +0xa +
//   goods*2; piles linked +0xc) that does not have all its units leaving
//   (amount +0x1 != leaving +0x2) and has the smallest score: d =
//   TileDistance(building +0x2/+0x4, pile +0x6/+0x8), and for piles whose
//   kind (+0x4) is not 0xff the 32-bit value (amount*5 + d + (leaving + 1) *
//   0x7ffffffb) * 2, i.e. 2*(d + 5*(amount - leaving - 1)) mod 2^32
//   (unsigned compare; the first of equal ones). Then the carrier nearest to
//   that pile from the area's settler list (uint16 head at area +0x12a,
//   linked +0x10/+0x12). With both: GameWorld::PileIncAmountLeaving(pile),
//   the carrier is unlinked, the byte +0x36 + goods of the building's extra
//   record (uint16 +0x34; 0x5a-byte records at world+0x10f45e8) goes up by
//   one, and among the six piles of the building (a ring starting at uint16
//   +0xe, linked +0xc) the last one holding `goods` with (byte +0x2 >> 4) +
//   amount < 8 is the destination; if there is none, the last free one
//   (goods byte 0xff), which gets `goods`; if there is none either, nothing
//   more happens. Otherwise the destination's byte +0x2 += 0x10 (high
//   nibble: units on the way), the carrier is sent to the source pile
//   (GameWorld::SetSettlerTarget) and gets job word +0xe = 0x4c, +0xc = 0,
//   word +0x22 = destination and byte +0x1a = goods. Probably a market or
//   port ordering goods to trade. Callers (UpdateEconomy) ignore EAX.
//   ret 0xc.
// All thiscall (ECX = world).

#include "game/raw.h"
#include "game/replacements.h"
#include "game/settler_groups.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace s3re::game {

namespace {

using layout::Area;
using layout::Building;
using layout::Pile;
using layout::Player;
using layout::Settler;
using layout::Tile;

constexpr std::uint8_t kSettlerType29 = 0x29;
constexpr std::uint32_t kRingEntriesFirst = 1;
constexpr std::uint32_t kRingEntriesLast = 36;
constexpr std::uint32_t kPlaceEntries = 1000;

// The two waiting lists of a player: settler_lists[39] (+0x6e) and [43]
// (+0x76) of the player record.
constexpr std::size_t kPlayerWaitingA = (0x6e - 0x20) / 2;
constexpr std::size_t kPlayerWaitingB = (0x76 - 0x20) / 2;

// Tile (x, y) with the original's 32-bit arithmetic.
Tile& TileAt(GameWorld* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

std::uint32_t Address(const void* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
}

}  // namespace

using SetTargetFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t settler,
                                               std::uint32_t x, std::uint32_t y);
using CountNearFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t first,
                                               std::uint32_t maxDistance, std::uint32_t x,
                                               std::uint32_t y, std::uint32_t value);
using FindNearbyFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t settler,
                                                std::uint32_t x, std::uint32_t y);

// Purpose: Gives a settler a new place to walk to. Clears its remembered path so the path finder starts
// fresh. Used by nearly every job (45 places in the code).
std::uint32_t __fastcall GameWorld_SetSettlerTarget(GameWorld* world, void*, std::uint32_t settler,
                                                    std::uint32_t x, std::uint32_t y) {
    Settler& record = world->settlers[settler];
    if (GameWorld_IsOnMap(world, nullptr, x, y)) {
        record.target_x = static_cast<std::uint16_t>(x);
        record.target_y = static_cast<std::uint16_t>(y);
    } else {
        record.target_x = record.x;
        record.target_y = record.y;
    }
    for (std::uint16_t& word : record.path_cache) {
        word = 0xffff;
    }
    record.path_mode = 1;
    if (record.step_time == 0) {
        record.step_time = 1;
    }
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world)) + (settler << 6);
}

// Purpose: Counts how many settlers of a list stand within some distance of a tile. Used when preparing
// what to draw (probably for on-screen indicators).
std::uint32_t __fastcall GameWorld_CountListSettlersNear(GameWorld* world, void*, std::uint32_t first,
                                                         std::uint32_t maxDistance, std::uint32_t x,
                                                         std::uint32_t y, std::uint32_t value) {
    std::uint32_t count = 0;
    for (std::uint32_t settler = first; settler != 0; settler = world->settlers[settler].next) {
        const std::uint32_t sx = world->settlers[settler].x;
        const std::uint32_t sy = world->settlers[settler].y;
        if (TileAt(world, sx, sy).walk_region == value && TileDistance(x, y, sx, sy) < maxDistance) {
            ++count;
        }
    }
    return count;
}

// Purpose: Looks around a tile for a type-0x29 settler of the same player. Its return value has an odd
// quirk in the original (see the notes above), which we keep.
std::uint32_t __fastcall GameWorld_FindNearbySettlerType29(GameWorld* world, void*, std::uint32_t settler,
                                                           std::uint32_t x, std::uint32_t y) {
    std::uint32_t last = x;  // the original's register, see above
    for (std::uint32_t entry = kRingEntriesFirst; entry <= kRingEntriesLast; ++entry) {
        const std::uint32_t tx = x + static_cast<std::uint32_t>(world->offsets[entry].dx);
        const std::uint32_t ty = y + static_cast<std::uint32_t>(world->offsets[entry].dy);
        if (!GameWorld_IsOnMap(world, nullptr, tx, ty)) {
            continue;
        }
        last = TileAt(world, tx, ty).settler;
        if (last != 0 && world->settlers[last].art == kSettlerType29 &&
            world->settlers[last].owner == world->settlers[settler].owner) {
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

// Purpose: Puts a (new) settler on the nearest free, walkable tile around a position. Used when settlers
// are created, e.g. by player commands.
std::uint32_t __fastcall GameWorld_PlaceSettlerNear(GameWorld* world, void*, std::uint32_t settler, std::uint32_t x,
                                                    std::uint32_t y) {
    for (std::uint32_t entry = 0; entry < kPlaceEntries; ++entry) {
        const std::uint32_t tx = x + static_cast<std::uint32_t>(world->offsets[entry].dx);
        const std::uint32_t ty = y + static_cast<std::uint32_t>(world->offsets[entry].dy);
        const std::uint32_t size = world->map_size;
        if (tx >= size || ty >= size) {
            continue;
        }
        Tile& tile = TileAt(world, tx, ty);
        if (tile.settler != 0 || tile.walk_region == 0) {
            continue;
        }
        tile.settler = static_cast<std::uint16_t>(settler);
        Settler& record = world->settlers[settler];
        record.counter = 0;
        record.direction = GameWorld_RandomBelow6(world, nullptr);
        record.x = static_cast<std::uint16_t>(tx);
        record.y = static_cast<std::uint16_t>(ty);
        return 1;
    }
    return 0;
}

S3RE_HOOK(0x0050ce80, GameWorld_PlaceSettlerNear, PlaceNearFn, Original_GameWorld_PlaceSettlerNear,
          GameWorld_PlaceSettlerNear);

namespace {

// The nearest settler of the list at `head` to (x, y); 0 if the list is
// empty. `distance` gets its distance, `last` the last distance computed.
std::uint32_t NearestInList(GameWorld* world, const std::uint16_t& head, std::uint32_t x, std::uint32_t y,
                            std::uint32_t& distance, std::uint32_t& last) {
    std::uint32_t best = 0;
    distance = 0xffffffffu;
    for (std::uint32_t settler = head; settler != 0; settler = world->settlers[settler].next) {
        last = TileDistance(x, y, world->settlers[settler].x, world->settlers[settler].y);
        if (last < distance) {
            distance = last;
            best = settler;
        }
    }
    return best;
}

void Unlink(GameWorld* world, std::uint16_t& head, std::uint32_t settler) {
    Settler& record = world->settlers[settler];
    const std::uint32_t previous = record.prev;
    if (previous != 0) {
        world->settlers[previous].next = record.next;
    } else {
        head = record.next;
    }
    const std::uint32_t next = record.next;
    if (next != 0) {
        world->settlers[next].prev = record.prev;
    }
}

}  // namespace

using SendToPileFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t pile, std::uint32_t player,
                                                std::uint32_t building);
using SendToBuildingFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t building);

// Purpose: Assigns work: finds the nearest waiting settler (probably a carrier) of a player and sends it to
// a goods pile to pick goods up for a building.
std::uint32_t __fastcall GameWorld_SendNearestSettlerToPile(GameWorld* world, void*, std::uint32_t pile,
                                                            std::uint32_t player, std::uint32_t building) {
    std::uint16_t& head = world->players[player].settler_lists[kPlayerWaitingA];
    if (head == 0) {
        return Address(&head);
    }
    Pile& record = world->piles[pile];
    const std::uint32_t px = record.x;
    const std::uint32_t py = record.y;
    std::uint32_t distance = 0;
    std::uint32_t last = 0;
    const std::uint32_t best = NearestInList(world, head, px, py, distance, last);
    if (best == 0) {
        return last;
    }
    std::uint8_t& reserved = record.leaving;
    reserved = static_cast<std::uint8_t>(reserved + 1);
    Unlink(world, head, best);
    GameWorld_SetSettlerTarget(world, nullptr, best, record.x, record.y);
    Settler& settler = world->settlers[best];
    settler.job = 0x133;
    settler.counter = 0;
    settler.good = record.good;
    settler.second_unit = 0;
    if (record.amount != (reserved & 0x0f)) {
        reserved = static_cast<std::uint8_t>(reserved + 1);
        settler.second_unit = 1;
    }
    settler.building = static_cast<std::uint16_t>(building);
    return ((pile + 0x14d2e2u) & 0xffffff00u) | reserved;
}

// Purpose: Assigns work: finds the nearest waiting settler (probably a builder) and sends it to a building
// site, if one is close enough.
std::uint32_t __fastcall GameWorld_SendNearestSettlerToBuilding(GameWorld* world, void*, std::uint32_t building) {
    const Building& record = world->buildings[building];
    const std::uint32_t direction = (record.dock_direction + 3u) % 6u;
    const std::uint32_t x1 = NeighbourX(record.work_x, direction);
    const std::uint32_t y1 = NeighbourY(record.work_y, direction);
    const std::uint32_t x2 = NeighbourX(x1, direction);
    const std::uint32_t y2 = NeighbourY(y1, direction);
    const std::uint32_t player = record.owner;
    std::uint16_t& head = world->players[player].settler_lists[kPlayerWaitingB];
    if (head == 0) {
        return Address(&head);
    }
    std::uint32_t distance = 0;
    std::uint32_t last = 0;
    const std::uint32_t best = NearestInList(world, head, x2, y2, distance, last);
    if (best == 0 || distance >= 40) {
        return last;
    }
    Unlink(world, head, best);
    const std::uint32_t result = GameWorld_SetSettlerTarget(world, nullptr, best, x2, y2);
    Settler& settler = world->settlers[best];
    settler.job = 0x147;
    settler.ship_walk = 0;
    settler.counter = 0;
    settler.building = static_cast<std::uint16_t>(building);
    return (result & 0xffff0000u) | (building & 0xffffu);
}

S3RE_HOOK(0x004ddbd0, GameWorld_SendNearestSettlerToPile, SendToPileFn, Original_GameWorld_SendNearestSettlerToPile,
          GameWorld_SendNearestSettlerToPile);
S3RE_HOOK(0x004ddd70, GameWorld_SendNearestSettlerToBuilding, SendToBuildingFn,
          Original_GameWorld_SendNearestSettlerToBuilding, GameWorld_SendNearestSettlerToBuilding);

namespace {

constexpr std::uint32_t kListCount = SettlerGroups::kGroups;
constexpr std::uint32_t kListWords = SettlerGroups::kWords;

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

// The int32 length of a player's list, read and written as uint32 like the
// original (no signed overflow on the decrement).
std::uint32_t& PlayerListLength(GameWorld* world, std::uint32_t player, std::uint32_t list) {
    return reinterpret_cast<std::uint32_t&>(world->player_list_lengths[player][list]);
}

}  // namespace

using RemoveSettlerFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t settler);

// Purpose: Takes a settler out of all the job/assignment lists it may be on, and announces the change as an
// event. Probably used when a settler dies or changes job.
std::uint32_t __fastcall GameWorld_RemoveSettlerFromLists(GameWorld* world, void*, std::uint32_t settler) {
    const Settler& record = world->settlers[settler];
    GameWorld_QueueEvent(world, nullptr, 5, settler, record.art, record.owner);
    const std::uint32_t player = record.owner;
    SettlerGroups& groups = GlobalSettlerGroups();
    for (std::uint32_t list = 0; list < kListCount; ++list) {
        RemoveFromList(groups.lists[list], groups.counts[list], settler);
    }
    for (std::uint32_t list = 0; list < kListCount; ++list) {
        RemoveFromList(world->player_lists[player][list], PlayerListLength(world, player, list), settler);
    }
    return 0;
}

S3RE_HOOK(0x0050dff0, GameWorld_RemoveSettlerFromLists, RemoveSettlerFn, Original_GameWorld_RemoveSettlerFromLists,
          GameWorld_RemoveSettlerFromLists);

namespace {

// Head of the player's settler list `list`: Player::settler_lists[list]
// (player record +0x20 + list*2; any list number, as in the original).
std::uint16_t& WorkerListHead(GameWorld* world, std::uint32_t player, std::uint32_t list) {
    return world->players[player].settler_lists[list];
}

}  // namespace

using SendWorkerFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t building,
                                                std::uint32_t list, std::uint32_t maxDistance);

// Purpose: Finds a worker for a building: the nearest free settler of the needed kind that can walk there
// (same region of the map) and is close enough, takes it off the free list and sends it to the building.
std::uint32_t __fastcall GameWorld_SendNearestWorkerToBuilding(GameWorld* world, void*, std::uint32_t building,
                                                               std::uint32_t list, std::uint32_t maxDistance) {
    const Building& record = world->buildings[building];
    const std::uint32_t bx = record.door_x;
    const std::uint32_t by = record.door_y;
    const std::uint32_t region = TileAt(world, bx, by).walk_region;
    const std::uint32_t player = record.owner;
    std::uint16_t& head = WorkerListHead(world, player, list);
    std::uint32_t best = 0xffffffffu;
    std::uint32_t chosen = 0;
    for (std::uint32_t settler = head; settler != 0; settler = world->settlers[settler].next) {
        const std::uint32_t sx = world->settlers[settler].x;
        const std::uint32_t sy = world->settlers[settler].y;
        if (TileAt(world, sx, sy).walk_region != region) {
            continue;
        }
        const std::uint32_t distance = TileDistance(record.x, record.y, sx, sy);
        if (distance < maxDistance && distance < best) {
            best = distance;
            chosen = settler;
        }
    }
    if (chosen == 0) {
        return 0;
    }
    Unlink(world, head, chosen);
    GameWorld_RemoveSettlerFromLists(world, nullptr, chosen);
    GameWorld_SetSettlerTarget(world, nullptr, chosen, record.door_x, record.door_y);
    Settler& settler = world->settlers[chosen];
    settler.job = 3;
    settler.counter = 0;
    settler.building = static_cast<std::uint16_t>(building);
    return chosen;
}

namespace {

constexpr std::uint32_t kBuildingPiles = 6;
constexpr std::uint8_t kFreePile = 0xff;
constexpr std::uint16_t kJobFetchForTrade = 0x4c;

}  // namespace

using FetchGoodsFn = void(__fastcall*)(GameWorld* world, void* edx, std::uint32_t area, std::uint32_t building,
                                       std::uint32_t goods);

// Purpose: Probably a market or port ordering goods: finds the best pile of the wanted goods in its area and
// the carrier nearest to that pile, and sends the carrier to bring a unit to one of the building's six
// stock piles.
void __fastcall GameWorld_FetchGoodsToTradeBuilding(GameWorld* world, void*, std::uint32_t area,
                                                    std::uint32_t building, std::uint32_t goods) {
    const Building& record = world->buildings[building];
    std::uint32_t source = 0;
    std::uint32_t bestScore = 0xffffffffu;
    for (std::uint32_t pile = world->areas[area].piles_by_goods[goods]; pile != 0; pile = world->piles[pile].next) {
        const Pile& p = world->piles[pile];
        const std::uint32_t amount = p.amount;
        const std::uint32_t leaving = p.leaving;
        if (amount == leaving) {
            continue;
        }
        std::uint32_t score = TileDistance(record.x, record.y, p.x, p.y);
        if (p.art != 0xff) {
            score = (amount * 5 + score + (leaving + 1) * 0x7ffffffbu) * 2;
        }
        if (score < bestScore) {
            bestScore = score;
            source = pile;
        }
    }
    if (source == 0) {
        return;
    }
    // The area's settler list at +0x12a (settlers_by_type[0]).
    std::uint16_t& head = world->areas[area].settlers_by_type[0];
    const Pile& from = world->piles[source];
    std::uint32_t distance = 0;
    std::uint32_t last = 0;
    const std::uint32_t carrier = NearestInList(world, head, from.x, from.y, distance, last);
    if (carrier == 0) {
        return;
    }
    GameWorld_PileIncAmountLeaving(world, nullptr, source);
    Unlink(world, head, carrier);
    ++world->trades[record.trade].coming[goods];
    const std::uint32_t first = record.pile_ring;
    std::uint32_t target = 0;
    std::uint32_t pile = first;
    for (std::uint32_t k = 0; k < kBuildingPiles; ++k) {
        const Pile& p = world->piles[pile];
        if (p.good == goods && static_cast<std::int32_t>((p.leaving >> 4) + p.amount) < 8) {
            target = pile;
        }
        pile = p.next;
    }
    if (target == 0) {
        pile = first;
        for (std::uint32_t k = 0; k < kBuildingPiles; ++k) {
            if (world->piles[pile].good == kFreePile) {
                target = pile;
            }
            pile = world->piles[pile].next;
        }
        if (target == 0) {
            return;
        }
        world->piles[target].good = static_cast<std::uint8_t>(goods);
    }
    world->piles[target].leaving += 0x10;
    GameWorld_SetSettlerTarget(world, nullptr, carrier, from.x, from.y);
    Settler& s = world->settlers[carrier];
    s.job = kJobFetchForTrade;
    s.counter = 0;
    s.building = static_cast<std::uint16_t>(target);
    s.good = static_cast<std::uint8_t>(goods);
}

S3RE_HOOK(0x004de0a0, GameWorld_FetchGoodsToTradeBuilding, FetchGoodsFn, Original_GameWorld_FetchGoodsToTradeBuilding,
          GameWorld_FetchGoodsToTradeBuilding);

S3RE_HOOK(0x004db500, GameWorld_SendNearestWorkerToBuilding, SendWorkerFn, Original_GameWorld_SendNearestWorkerToBuilding,
          GameWorld_SendNearestWorkerToBuilding);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestSettlers = 0x400;
constexpr std::size_t kTestWorld = offsetof(GameWorld, settlers) + 0x10000 * sizeof(Settler);

// Sets `field` (a field of the original world) to `value` in both worlds.
template <typename T, typename V>
void SetField(test::WorldPair& worlds, T& field, V value) {
    worlds.Set<T>(OffsetIn(worlds.original(), &field), static_cast<T>(value));
}

// Event ring indices as QueueEvent expects (write index in range).
void SetUpEventRings(test::Context& ctx, test::WorldPair& worlds) {
    GameWorld* o = worlds.original();
    SetField(worlds, o->event_index.write, ctx.rng().Below(1000));
    SetField(worlds, o->pending_event_index.read, 0u);
    SetField(worlds, o->pending_event_index.write, 0u);
}

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
    GameWorld* o = worlds.original();
    std::uint32_t entry = 0;
    for (std::int32_t radius = 0; entry <= kRingEntriesLast; ++radius) {
        for (std::int32_t dx = -radius; dx <= radius && entry <= kRingEntriesLast; ++dx) {
            for (std::int32_t dy = -radius; dy <= radius && entry <= kRingEntriesLast; ++dy) {
                if ((dx < 0 ? -dx : dx) != radius && (dy < 0 ? -dy : dy) != radius) {
                    continue;
                }
                const bool random = ctx.rng().Chance(1, 32);
                SetField(worlds, o->offsets[entry].dx, random ? ctx.rng().Range(-9, 9) : dx);
                SetField(worlds, o->offsets[entry].dy, random ? ctx.rng().Range(-9, 9) : dy);
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
    const bool match = world->settlers[last].art == kSettlerType29 &&
                       world->settlers[last].owner == world->settlers[settler].owner;
    return match ? last : 0;
}

void CompareFindNearby(test::Context& ctx, FindNearbyFn candidate, int cases) {
    Compare(ctx, cases, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        GameWorld* o = worlds.original();
        const std::uint32_t size = ctx.rng().Chance(1, 2) ? 384 : 4 + ctx.rng().Below(30);
        SetField(worlds, o->map_size, size);
        SetUpRings(ctx, worlds);
        std::uint32_t x = ctx.rng().Below(size);
        std::uint32_t y = ctx.rng().Below(size);
        if (ctx.rng().Chance(1, 16)) {
            x += 400;  // nothing on the map
        }
        const std::uint32_t settler = 1 + ctx.rng().Below(kTestSettlers - 1);
        SetField(worlds, o->settlers[settler].owner, ctx.rng().Below(3));
        const std::uint32_t sparse = ctx.rng().Below(4);
        for (std::int32_t dx = -5; dx <= 5; ++dx) {
            for (std::int32_t dy = -5; dy <= 5; ++dy) {
                const std::uint32_t tx = x + dx;
                const std::uint32_t ty = y + dy;
                if (tx >= size || ty >= size) {
                    continue;
                }
                const std::uint32_t other = ctx.rng().Below(4) < sparse ? 0 : 1 + ctx.rng().Below(kTestSettlers - 1);
                SetField(worlds, o->tiles[tx][ty].settler, other);
                if (other != 0) {
                    SetField(worlds, o->settlers[other].art,
                             ctx.rng().Chance(1, 6) ? kSettlerType29 : static_cast<std::uint8_t>(ctx.rng().Below(0x40)));
                    SetField(worlds, o->settlers[other].owner, ctx.rng().Below(3));
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
        GameWorld* o = worlds.original();
        const std::uint32_t size = ctx.rng().Chance(1, 2) ? 384 : ctx.rng().Below(64);
        SetField(worlds, o->map_size, size);
        const std::uint32_t settler = ctx.rng().Below(0x10000);
        std::uint32_t x = ctx.rng().Below(400);
        std::uint32_t y = ctx.rng().Below(400);
        if (ctx.rng().Chance(1, 8)) {
            x = ctx.rng().Next32();
        }
        SetField(worlds, o->settlers[settler].step_time, ctx.rng().Below(3));
        expected = Relative(Original_GameWorld_SetSettlerTarget(worlds.original(), nullptr, settler, x, y),
                            worlds.original());
        actual = Relative(GameWorld_SetSettlerTarget(worlds.ours(), nullptr, settler, x, y), worlds.ours());
    });
}

S3RE_TEST(GameWorld_CountListSettlersNear_MatchesOriginal) {
    Compare(ctx, 3000, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        // A list of up to 20 settlers on tiles near (x, y), some with the value.
        GameWorld* o = worlds.original();
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
            Settler& s = o->settlers[settler];
            SetField(worlds, s.x, ctx.rng().Chance(1, 2) ? x : sx);
            SetField(worlds, s.y, sy % 384);
            SetField(worlds, s.next, first);
            SetField(worlds, o->tiles[s.x][s.y].walk_region, ctx.rng().Chance(2, 3) ? value : ctx.rng().Below(256));
            first = settler;
        }
        // A list may loop through an earlier settler; make it end.
        const std::uint32_t maxDistance = ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(400);
        // Walk the list as built (it can contain the same settler twice and loop);
        // only call when it ends within 64 steps.
        std::uint32_t steps = 0;
        for (std::uint32_t s = first; s != 0 && steps < 64; s = o->settlers[s].next) {
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
        GameWorld* o = worlds.original();
        const std::uint32_t size = ctx.rng().Chance(1, 2) ? 384 : 4 + ctx.rng().Below(40);
        SetField(worlds, o->map_size, size);
        std::uint32_t x = ctx.rng().Below(size);
        const std::uint32_t y = ctx.rng().Below(size);
        if (ctx.rng().Chance(1, 32)) {
            x += 1000;  // nothing on the map
        }
        // Offsets: entries 0..999 spiral out to about 18 tiles.
        const std::uint32_t entries = ctx.rng().Chance(1, 8) ? kPlaceEntries : 80;
        for (std::uint32_t entry = 0; entry < entries; ++entry) {
            const std::int32_t reach = 1 + static_cast<std::int32_t>(entry / 60);
            SetField(worlds, o->offsets[entry].dx, ctx.rng().Range(-reach, reach));
            SetField(worlds, o->offsets[entry].dy, ctx.rng().Range(-reach, reach));
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
                SetField(worlds, o->tiles[tx][ty].settler, empty || ctx.rng().Chance(1, 2) ? 0 : 1 + ctx.rng().Below(100));
                SetField(worlds, o->tiles[tx][ty].walk_region,
                         empty || ctx.rng().Chance(1, 2) ? 1 + ctx.rng().Below(3) : 0);
            }
        }
        // The first two words of the random state.
        const std::uint32_t state = ctx.rng().Next32();
        SetField(worlds, o->random_state[0], state & 0xffff);
        SetField(worlds, o->random_state[1], state >> 16);
        const std::uint32_t settler = ctx.rng().Below(kTestSettlers);
        expected = Original_GameWorld_PlaceSettlerNear(worlds.original(), nullptr, settler, x, y);
        actual = GameWorld_PlaceSettlerNear(worlds.ours(), nullptr, settler, x, y);
    });
}

namespace {

// A doubly linked list of up to 12 distinct settlers at `head`, near (x, y).
void SetUpWaitingList(test::Context& ctx, test::WorldPair& worlds, std::uint16_t& head, std::uint32_t x,
                      std::uint32_t y) {
    GameWorld* o = worlds.original();
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
    SetField(worlds, head, length ? used[0] : 0);
    for (std::uint32_t i = 0; i < length; ++i) {
        Settler& record = o->settlers[used[i]];
        SetField(worlds, record.prev, i ? used[i - 1] : 0);
        SetField(worlds, record.next, i + 1 < length ? used[i + 1] : 0);
        const std::int32_t reach = ctx.rng().Chance(1, 2) ? 8 : 60;
        SetField(worlds, record.x, (x + ctx.rng().Range(-reach, reach)) & 0xffff);
        SetField(worlds, record.y, (y + ctx.rng().Range(-reach, reach)) & 0xffff);
    }
}

std::uint32_t RelativeIfPointer(std::uint32_t value, GameWorld* world) {
    const auto base = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));
    return value - base < kTestWorld ? value - base : value;
}

}  // namespace

S3RE_TEST(GameWorld_SendNearestSettlerToPile_MatchesOriginal) {
    Compare(ctx, 3000, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        GameWorld* o = worlds.original();
        SetField(worlds, o->map_size, 384u);
        const std::uint32_t pile = 1 + ctx.rng().Below(0x800);
        const std::uint32_t player = ctx.rng().Below(20);
        const std::uint32_t x = ctx.rng().Below(384);
        const std::uint32_t y = ctx.rng().Below(384);
        Pile& record = o->piles[pile];
        SetField(worlds, record.x, x);
        SetField(worlds, record.y, y);
        SetField(worlds, record.amount, ctx.rng().Below(10));
        SetField(worlds, record.leaving, ctx.rng().Chance(1, 2) ? ctx.rng().Below(10) : ctx.rng().Next32());
        SetUpWaitingList(ctx, worlds, o->players[player].settler_lists[kPlayerWaitingA], x, y);
        const std::uint32_t building = ctx.rng().Next32();
        expected = RelativeIfPointer(
            Original_GameWorld_SendNearestSettlerToPile(worlds.original(), nullptr, pile, player, building), worlds.original());
        actual = RelativeIfPointer(GameWorld_SendNearestSettlerToPile(worlds.ours(), nullptr, pile, player, building),
                                   worlds.ours());
    });
}

S3RE_TEST(GameWorld_SendNearestSettlerToBuilding_MatchesOriginal) {
    Compare(ctx, 3000, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        GameWorld* o = worlds.original();
        SetField(worlds, o->map_size, 384u);
        const std::uint32_t building = 1 + ctx.rng().Below(0x800);
        Building& record = o->buildings[building];
        const std::uint32_t player = ctx.rng().Below(20);
        const std::uint32_t x = 2 + ctx.rng().Below(380);
        const std::uint32_t y = 2 + ctx.rng().Below(380);
        SetField(worlds, record.owner, player);
        SetField(worlds, record.work_x, x);
        SetField(worlds, record.work_y, y);
        SetField(worlds, record.dock_direction, ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(6));
        SetUpWaitingList(ctx, worlds, o->players[player].settler_lists[kPlayerWaitingB], x, y);
        expected = RelativeIfPointer(Original_GameWorld_SendNearestSettlerToBuilding(worlds.original(), nullptr, building),
                                     worlds.original());
        actual = RelativeIfPointer(GameWorld_SendNearestSettlerToBuilding(worlds.ours(), nullptr, building), worlds.ours());
    });
}

namespace {

constexpr std::size_t kGlobalListBytes = sizeof(SettlerGroups);  // 0x7ad620..0x7ade04

// Saves the global lists, sets them per case and restores them at the end.
class GlobalLists {
public:
    GlobalLists() { std::memcpy(saved_, Base(), sizeof(saved_)); }
    ~GlobalLists() { std::memcpy(Base(), saved_, sizeof(saved_)); }
    static std::uint8_t* Base() { return reinterpret_cast<std::uint8_t*>(&GlobalSettlerGroups()); }

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
    const std::uint32_t player = world->settlers[settler].owner;
    GameWorld_RemoveSettlerFromLists(world, edx, settler);
    SettlerGroups& groups = GlobalSettlerGroups();
    for (std::uint32_t list = 0; list < kListCount; ++list) {
        RemoveFromList(groups.lists[list], groups.counts[list], settler);
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
        GameWorld* o = worlds.original();
        const std::uint32_t player = ctx.rng().Below(20);
        SetField(worlds, o->settlers[settler].owner, player);
        SetUpEventRings(ctx, worlds);
        SettlerGroups& groups = GlobalSettlerGroups();
        for (std::uint32_t list = 0; list < kListCount; ++list) {
            FillList(ctx, groups.lists[list], groups.counts[list], settler);
            std::uint32_t count = 0;
            std::uint16_t words[kListWords];
            FillList(ctx, words, count, settler);
            SetField(worlds, o->player_list_lengths[player][list], count);
            for (std::uint32_t w = 0; w < kListWords; ++w) {
                SetField(worlds, o->player_lists[player][list][w], words[w]);
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

namespace {

// Wrong: ignores the region (any settler of the list may be sent).
std::uint32_t __fastcall WrongSendNearestWorkerToBuilding(GameWorld* world, void* edx, std::uint32_t building,
                                                          std::uint32_t list, std::uint32_t maxDistance) {
    const Building& record = world->buildings[building];
    const Tile& tile = TileAt(world, record.door_x, record.door_y);
    // Put every listed settler's tile into the building's region for the call.
    const std::uint16_t& head = WorkerListHead(world, record.owner, list);
    for (std::uint32_t settler = head; settler != 0; settler = world->settlers[settler].next) {
        const Settler& s = world->settlers[settler];
        TileAt(world, s.x, s.y).walk_region = tile.walk_region;
    }
    return GameWorld_SendNearestWorkerToBuilding(world, edx, building, list, maxDistance);
}

void CompareSendWorker(test::Context& ctx, SendWorkerFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    GlobalLists restore;
    std::uint8_t globalsBefore[kGlobalListBytes];
    std::uint8_t globalsOriginal[kGlobalListBytes];
    for (int i = 0; i < cases; ++i) {
        GameWorld* o = worlds.original();
        SetField(worlds, o->map_size, 384u);
        const std::uint32_t building = 1 + ctx.rng().Below(0x800);
        Building& record = o->buildings[building];
        const std::uint32_t player = ctx.rng().Below(20);
        const std::uint32_t list = ctx.rng().Below(40);
        const std::uint32_t x = 2 + ctx.rng().Below(380);
        const std::uint32_t y = 2 + ctx.rng().Below(380);
        SetField(worlds, record.owner, player);
        SetField(worlds, record.door_x, x);
        SetField(worlds, record.door_y, y);
        SetField(worlds, record.x, x + ctx.rng().Below(3));
        SetField(worlds, record.y, y + ctx.rng().Below(3));
        std::uint16_t& head = WorkerListHead(o, player, list);
        SetUpWaitingList(ctx, worlds, head, x, y);
        // Regions: the building's tile and the settlers' tiles in one of two.
        const std::uint8_t region = static_cast<std::uint8_t>(1 + ctx.rng().Below(2));
        SetField(worlds, o->tiles[x][y].walk_region, region);
        for (std::uint32_t s = head; s != 0; s = o->settlers[s].next) {
            // On the map (the region byte of their tile is read).
            auto onMap = [&](std::uint32_t v) {
                const std::int32_t c = static_cast<std::int32_t>(v) + ctx.rng().Range(-40, 40);
                return static_cast<std::uint16_t>(c < 0 ? 0 : c > 383 ? 383 : c);
            };
            Settler& settler = o->settlers[s];
            SetField(worlds, settler.x, onMap(x));
            SetField(worlds, settler.y, onMap(y));
            SetField(worlds, o->tiles[settler.x][settler.y].walk_region,
                     ctx.rng().Chance(2, 3) ? region : 3 - region);
            SetField(worlds, settler.owner, ctx.rng().Below(20));
        }
        // For RemoveSettlerFromLists: event ring indices in range, empty lists.
        SetUpEventRings(ctx, worlds);
        for (std::uint32_t l = 0; l < kListCount; ++l) {
            GlobalSettlerGroups().counts[l] = 0;
            for (std::uint32_t p = 0; p < 20; ++p) {
                SetField(worlds, o->player_list_lengths[p][l], 0);
            }
        }
        const std::uint32_t maxDistance = ctx.rng().Chance(1, 4) ? ctx.rng().Next32() : ctx.rng().Below(70);
        std::memcpy(globalsBefore, GlobalLists::Base(), kGlobalListBytes);
        const std::uint32_t expected =
            Original_GameWorld_SendNearestWorkerToBuilding(worlds.original(), nullptr, building, list, maxDistance);
        std::memcpy(globalsOriginal, GlobalLists::Base(), kGlobalListBytes);
        std::memcpy(GlobalLists::Base(), globalsBefore, kGlobalListBytes);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, building, list, maxDistance);
        if (!S3RE_EXPECT_EQ(expected, actual) ||
            !ctx.ExpectMemEq(globalsOriginal, GlobalLists::Base(), kGlobalListBytes, "original globals", "our globals",
                             __FILE__, __LINE__) ||
            ((i % 64 == 63 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 63 before it): building %u list %u", i, building, list);
            return;
        }
    }
}

}  // namespace

namespace {

// Wrong: ignores the kind (scores every pile by distance only).
void __fastcall WrongFetchGoodsToTradeBuilding(GameWorld* world, void* edx, std::uint32_t area, std::uint32_t building,
                                               std::uint32_t goods) {
    std::vector<std::pair<std::uint32_t, std::uint8_t>> kinds;
    for (std::uint32_t pile = world->areas[area].piles_by_goods[goods]; pile != 0 && kinds.size() < 64;
         pile = world->piles[pile].next) {
        kinds.emplace_back(pile, world->piles[pile].art);
        world->piles[pile].art = 0xff;
    }
    GameWorld_FetchGoodsToTradeBuilding(world, edx, area, building, goods);
    for (auto it = kinds.rbegin(); it != kinds.rend(); ++it) {
        world->piles[it->first].art = it->second;
    }
}

void CompareFetchGoods(test::Context& ctx, FetchGoodsFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        GameWorld* o = worlds.original();
        SetField(worlds, o->map_size, 384u);
        SetField(worlds, o->building_high_water, ctx.rng().Below(4));  // PileIncAmountLeaving's log
        const std::uint32_t area = 1 + ctx.rng().Below(0x40);
        const std::uint32_t goods = ctx.rng().Below(36);
        const std::uint32_t building = 1 + ctx.rng().Below(0x800);
        Building& record = o->buildings[building];
        const std::uint32_t x = 32 + ctx.rng().Below(320);
        const std::uint32_t y = 32 + ctx.rng().Below(320);
        SetField(worlds, record.x, x);
        SetField(worlds, record.y, y);
        SetField(worlds, record.trade, 1 + ctx.rng().Below(998));
        // The area's piles of this goods type (1..0x7ff, distinct).
        std::uint16_t& pileHead = o->areas[area].piles_by_goods[goods];
        const std::uint32_t length = ctx.rng().Chance(1, 10) ? 0 : 1 + ctx.rng().Below(8);
        std::uint32_t piles[8] = {};
        for (std::uint32_t k = 0; k < length; ++k) {
            bool fresh = false;
            while (!fresh) {
                piles[k] = 1 + ctx.rng().Below(0x7fe);
                fresh = true;
                for (std::uint32_t j = 0; j < k; ++j) {
                    fresh = fresh && piles[j] != piles[k];
                }
            }
        }
        SetField(worlds, pileHead, length ? piles[0] : 0);
        for (std::uint32_t k = 0; k < length; ++k) {
            Pile& p = o->piles[piles[k]];
            SetField(worlds, p.next, k + 1 < length ? piles[k + 1] : 0);
            SetField(worlds, p.x, x + ctx.rng().Range(-30, 30));
            SetField(worlds, p.y, y + ctx.rng().Range(-30, 30));
            const std::uint8_t amount = static_cast<std::uint8_t>(ctx.rng().Below(9));
            SetField(worlds, p.amount, amount);
            SetField(worlds, p.leaving, ctx.rng().Chance(1, 4) ? amount : static_cast<std::uint8_t>(ctx.rng().Below(8)));
            static constexpr std::uint8_t kKinds[] = {0xff, 0xff, 0, 0xfe, 1};
            SetField(worlds, p.art, kKinds[ctx.rng().Below(sizeof(kKinds))]);
            SetField(worlds, p.good, goods);
            // PileIncAmountLeaving unlinks the pile from the list of its tile's area.
            SetField(worlds, o->tiles[p.x][p.y].area, area);
        }
        // Carriers near the piles.
        SetUpWaitingList(ctx, worlds, o->areas[area].settlers_by_type[0], x, y);
        // The building's six piles: a ring in 0x800..0xfff.
        std::uint32_t ring[6];
        for (auto& r : ring) {
            r = 0x800 + ctx.rng().Below(0x7ff);
        }
        SetField(worlds, record.pile_ring, ring[0]);
        for (std::uint32_t k = 0; k < 6; ++k) {
            Pile& p = o->piles[ring[k]];
            SetField(worlds, p.next, ring[(k + 1) % 6]);
            const std::uint32_t r = ctx.rng().Below(4);
            SetField(worlds, p.good, r == 0 ? goods : r == 1 ? 0xff : ctx.rng().Below(36));
            SetField(worlds, p.amount, ctx.rng().Below(9));
            SetField(worlds, p.leaving, ctx.rng().Below(0x80));
        }
        Original_GameWorld_FetchGoodsToTradeBuilding(worlds.original(), nullptr, area, building, goods);
        candidate(worlds.ours(), nullptr, area, building, goods);
        if ((i % 32 == 31 || i == cases - 1) && !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 31 before it): area %u building %u goods %u", i, area,
                     building, goods);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(GameWorld_FetchGoodsToTradeBuilding_MatchesOriginal) {
    CompareFetchGoods(ctx, &GameWorld_FetchGoodsToTradeBuilding, 3000);
}
S3RE_TEST(GameWorld_FetchGoodsToTradeBuilding_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareFetchGoods(probe, &WrongFetchGoodsToTradeBuilding, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_SendNearestWorkerToBuilding_MatchesOriginal) {
    CompareSendWorker(ctx, &GameWorld_SendNearestWorkerToBuilding, 3000);
}
S3RE_TEST(GameWorld_SendNearestWorkerToBuilding_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSendWorker(probe, &WrongSendNearestWorkerToBuilding, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
