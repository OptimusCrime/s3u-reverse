// Area records: creating an area, moving the contents of an area to a new
// one, and splitting an area when one of its tiles is taken out (for example
// when the tile is blocked).
//
// Areas (0x2e0-byte records at world+0xe145e8, numbers 1..0xffe; see also
// map_areas.cpp and area_piles.cpp): +0x0 player (0xff = free), uint32
// +0x4 tile count, uint16 +0x8 head of a pile list, uint16 +0xa + goods*2
// heads of the pile lists per goods type (36), +0x52 / +0x9a / +0xe2 three
// more uint16 lists of 36 (goods requests and two building lists), uint16
// +0x12a 48 settler list heads, byte arrays of 36 at +0x18a, +0x1ae, +0x1d2,
// +0x1f6 and +0x21a, bytes +0x23e..+0x244, 51 bytes at +0x245 and 51 uint16
// at +0x278. The byte arrays are probably the economy settings of the area
// (goods distribution and priorities; their defaults depend on the race).
// Piles (14 bytes at world+0x123885c) link with +0xa previous / +0xc next and
// lie on tile (+0x6, +0x8); settlers (0x40 bytes at world+0x12a5e60) link
// with +0x10 next / +0x12 previous and stand on (+0x4, +0x6). The tile area
// word is the uint16 at (x * 0x300 + y + 0x44d9) * 24 (32-bit arithmetic).
//
// GameWorld::InitArea (0x004e6510): (area, player): QueuePendingEvent(4,
//   area, 0) and QueueEvent(6, area, 0, 0), then fills the record: player
//   byte, count 0, list heads and the per-goods arrays 0, then fixed
//   defaults, and per race (int32 at player record +0x4: world+0x11943e0 +
//   player*0x148; read again for each of the four race checks, 0..3) more
//   defaults. The stores are listed in the tables below in the original's
//   order. Only caller: AllocArea, which overwrites EAX. ret 8.
// GameWorld::AllocArea (0x004e64c0): (player) -> the first area 1..0xffe
//   whose player byte is 0xff (0 if none; 0xfff is never used), raises the
//   high-water mark (uint32 world+0xe115dc, unsigned compare) and calls
//   InitArea(area, player). ret 4.
// GameWorld::MoveAreaContents (0x004e54f0): (old, new): queues events (4,
//   old), (4, new) (QueuePendingEvent) and (6, old), (6, new) (QueueEvent),
//   then moves every pile of old's list +0x8 and of its 36 goods lists whose
//   tile lies in area `new` (32-bit compare with the zero-extended word) to
//   the front of the same list of `new`; for each goods type it then clears
//   the words +0x52/+0x9a/+0xe2 of both areas and copies the five byte
//   arrays from old to new. Then the same move for the 48 settler lists,
//   and copies +0x23e..+0x240, the 51 bytes at +0x245 (clearing the words at
//   +0x278 of both) and +0x241. Called after part of an area was flooded
//   with a new area number. EAX only flows into SplitAreaAtTile's (unused).
//   ret 8.
// GameWorld::MergeAreaContents (0x004e5a20): (into, from): queues events
//   (4, into), (4, from) and (6, into), (6, from), then puts every pile of
//   from's list +0x8 and of its 36 goods lists at the front of the same list
//   of `into` (one by one, so in reverse order; from's heads are left as
//   they were), clears into's words +0x52/+0x9a/+0xe2 per goods type, does
//   the same for the 48 settler lists, adds from's bytes +0x242..+0x244 to
//   into's, and if into has fewer tiles than from (uint32 +0x4, unsigned)
//   copies from's settings (the five byte arrays, +0x23e..+0x240, the 51
//   bytes at +0x245, +0x241) to into. Finally clears into's 51 words at
//   +0x278. EAX (0) is ignored by the only caller, JoinAreasAtTile. ret 8.
// GameWorld::JoinAreasAtTile (0x004e6150): (x, y): the tile joins the areas
//   of its six neighbours (offset table entries 1..6, no map check) that are
//   not blocked (bit 0 of +0xa) and have the tile's owner (byte +0x8): the
//   first such neighbour's area `a` gets the tile (AssignTileToArea, even if
//   that area word is 0 or marked); every later one in another area `b`:
//   MergeAreaContents(a, b), FloodMoveArea from that neighbour to `a`, `b`
//   freed (player byte 0xff) and events (4, b), (6, b) queued. With no such
//   neighbour the tile gets a new area (AllocArea(owner)). Probably the
//   counterpart of SplitAreaAtTile when a tile becomes free again. Callers
//   ignore EAX. ret 8.
// GameWorld::SplitAreaAtTile (0x004e5d50): (x, y): the tile's area `a` is
//   read, the tile leaves it (GameWorld::RemoveTileFromArea). Counting the
//   six neighbours (offset table entries 1..6 at world+0x110a598, no map
//   check) in `a`: none -> the area is freed (player byte 0xff) and events
//   (4, a) and (6, a) are queued; 2..4 -> the neighbours on the map
//   (NeighbourX/Y, GameWorld::IsOnMap) get flag 0x10 of tile byte +0xa set
//   if they are in `a` (else cleared). From the last such neighbour a walk
//   follows the border of `a` (always turning, six directions; wrapping
//   0..5), clearing flag 0x10 on every tile it enters, until either no
//   neighbour of (x, y) has flag 0x10 any more (still connected: done) or it
//   comes back to its first step (or found nothing in six turns before its
//   first step). Then each neighbour still in `a` with flag 0x10 is in a
//   separate part: a new area (AllocArea with the tile's owner byte +0x8)
//   gets that part (GameWorld::FloodMoveArea), `a`'s player byte and
//   MoveAreaContents(a, new). With 1, 5 or 6 neighbours nothing more
//   happens. Flag 0x10 stays set on unvisited tiles, as in the original.
//   EAX unused (callers tail-return it, their callers ignore it). ret 8.
// GameWorld::BlockTile (0x0051b6f0): (x, y): if bit 0 of tile byte +0xa
//   (blocked) is clear: sets it; GameWorld::SplitTileByte10Region if byte
//   +0x10 is not 0; SplitAreaAtTile if the area word is 1..0x7fff; then
//   GameWorld::SplitWord16RegionsAround. EAX = its EAX (0), or, if the tile
//   was already blocked, x * 0x300 + the flag byte (the original's AL load).
//   ret 8.
// GameWorld::UnblockTile (0x0051b770): (x, y): the counterpart of
//   BlockTile: if bit 0 of tile byte +0xa is set: clears it;
//   GameWorld::MergeTileByte10Regions if byte +0x10 is 0; JoinAreasAtTile
//   if the area word is 0 and the owner byte +0x8 is not 0xff; then
//   GameWorld::AssignWord16RegionsAround. EAX = its EAX, or, if the tile was
//   not blocked, x * 0x300 + the flag byte. ret 8.
// GameWorld::SetOwnerOfFreeTile (0x0051b5d0): (x, y, owner): only for a
//   tile without owner (byte +0x8 == 0xff): owner byte = owner (low byte);
//   then if the tile has no area, is not blocked and owner (32-bit) is not
//   0xff: JoinAreasAtTile and GameWorld::AddTilePileToArea. EAX = its EAX,
//   else x * 0x300 + y (the tile had an owner) or (x * 0x300 + y) * 3 +
//   0xce8b (the original's address arithmetic). ret 0xc.
// All thiscall (ECX = world).

#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uint32_t kTiles = 0x67444;
constexpr std::uint32_t kTileOwner = 0x8;
constexpr std::uint32_t kTileFlags = 0xa;
constexpr std::uint32_t kTileByte10 = 0x10;
constexpr std::uint8_t kTileBlocked = 0x01;
constexpr std::uint8_t kTileSplitMark = 0x10;
constexpr std::uint32_t kTileAreaIndexBias = 0x44d9;

constexpr std::uint32_t kAreas = 0xe145e8;
constexpr std::uint32_t kAreaBytes = 0x2e0;
constexpr std::uint32_t kAreaPlayer = 0x0;
constexpr std::uint32_t kAreaTileCount = 0x4;
constexpr std::uint32_t kAreaPileList = 0x8;
constexpr std::uint32_t kAreaGoodsPiles = 0xa;
constexpr std::uint32_t kAreaRequests = 0x52;
constexpr std::uint32_t kAreaSlotsA = 0x9a;
constexpr std::uint32_t kAreaSlotsB = 0xe2;
constexpr std::uint32_t kAreaSettlerLists = 0x12a;
constexpr std::uint32_t kAreaSettlerListCount = 0x30;
constexpr std::uint32_t kGoodsTypes = 0x24;
constexpr std::uint32_t kAreaWords51 = 0x278;
constexpr std::uint32_t kAreaBytes51 = 0x245;
constexpr std::uint32_t kAreaCount51 = 0x33;
constexpr std::uint8_t kFreeArea = 0xff;
constexpr std::uint32_t kLastArea = 0xfff;  // never handed out
constexpr std::uint32_t kAreaHighWater = 0xe115dc;
// Byte arrays per goods type, in the order InitArea clears them.
constexpr std::uint32_t kGoodsArraysInitOrder[] = {0x18a, 0x1f6, 0x1ae, 0x21a, 0x1d2};
// ... and in the order MoveAreaContents copies them.
constexpr std::uint32_t kGoodsArraysCopyOrder[] = {0x18a, 0x1ae, 0x1d2, 0x1f6, 0x21a};

constexpr std::uint32_t kPlayerRace = 0x11943e0;  // int32 at player record +0x4
constexpr std::uint32_t kPlayerRecordBytes = 0x148;

constexpr std::uint32_t kPiles = 0x123885c;
constexpr std::uint32_t kPileBytes = 14;
constexpr std::uint32_t kPileX = 0x6;
constexpr std::uint32_t kPileY = 0x8;
constexpr std::uint32_t kPilePrevious = 0xa;
constexpr std::uint32_t kPileNext = 0xc;

constexpr std::uint32_t kSettlers = 0x12a5e60;
constexpr std::uint32_t kSettlerBytes = 0x40;
constexpr std::uint32_t kSettlerX = 0x4;
constexpr std::uint32_t kSettlerY = 0x6;
constexpr std::uint32_t kSettlerNext = 0x10;
constexpr std::uint32_t kSettlerPrevious = 0x12;

constexpr std::uint32_t kNeighbourOffsets = 0x110a598;  // offset table entries 1..6
constexpr std::uint32_t kOffsetBytes = 16;
constexpr std::uint32_t kDirections = 6;

constexpr std::uint32_t kPendingAreaEvent = 4;
constexpr std::uint32_t kAreaEvent = 6;

struct ByteStore {
    std::uint16_t offset;
    std::uint8_t value;
};

// InitArea's defaults for every race (after the clearing loops).
constexpr ByteStore kAreaDefaults[] = {
    {0x246, 0x10}, {0x247, 0x10}, {0x248, 0xc}, {0x249, 0x6}, {0x24c, 0x10}, {0x24d, 0x10}, {0x24e, 0x8},
    {0x252, 0x10}, {0x253, 0x8}, {0x255, 0x10}, {0x257, 0x10}, {0x25b, 0x10}, {0x25c, 0x10}, {0x25d, 0x8},
    {0x25f, 0x10}, {0x260, 0x10}, {0x261, 0x8}, {0x263, 0x10}, {0x264, 0x0}, {0x265, 0x0}, {0x268, 0x10},
    {0x269, 0x0}, {0x26a, 0x0}, {0x26c, 0x10}, {0x26d, 0x0}, {0x26e, 0x0}, {0x273, 0x0}, {0x274, 0x0},
    {0x275, 0x0}, {0x276, 0x0}, {0x277, 0x0}, {0x1c0, 0x10}, {0x1c1, 0xc}, {0x1c2, 0x8}, {0x227, 0x1},
    {0x226, 0x1}, {0x228, 0x1}, {0x229, 0x1}, {0x22a, 0x1}, {0x23a, 0x1}, {0x22b, 0x1}, {0x22c, 0x1},
    {0x22d, 0x1}, {0x22e, 0x1}, {0x224, 0x1},
};

constexpr ByteStore kRace0Defaults[] = {
    {0x24a, 0x0}, {0x24b, 0x0}, {0x24f, 0x8}, {0x250, 0x0}, {0x251, 0x0}, {0x270, 0x8}, {0x271, 0x0},
    {0x272, 0x0}, {0x254, 0x4}, {0x256, 0x8}, {0x258, 0x0}, {0x259, 0x0}, {0x25a, 0x0}, {0x25e, 0x0},
    {0x262, 0x0}, {0x266, 0x0}, {0x267, 0x0}, {0x26b, 0x0}, {0x26f, 0x0}, {0x1f7, 0x1}, {0x1f8, 0x2},
    {0x1f9, 0x3}, {0x1fa, 0x7}, {0x1fb, 0x6}, {0x1fc, 0x18}, {0x1fd, 0x5}, {0x1fe, 0x8}, {0x1ff, 0xb},
    {0x200, 0x19}, {0x201, 0x4}, {0x202, 0x11}, {0x203, 0x12}, {0x204, 0x13}, {0x205, 0x14}, {0x206, 0x15},
    {0x207, 0x16}, {0x208, 0xe}, {0x209, 0xf}, {0x20a, 0x10}, {0x20c, 0xa}, {0x20d, 0xc}, {0x20e, 0x9},
    {0x210, 0xd}, {0x216, 0x17}, {0x20b, 0x1a},
};

constexpr ByteStore kRace1Defaults[] = {
    {0x24a, 0x0}, {0x24b, 0x0}, {0x24f, 0x0}, {0x250, 0x8}, {0x251, 0x0}, {0x270, 0x0}, {0x271, 0x8},
    {0x272, 0x0}, {0x254, 0x0}, {0x256, 0x0}, {0x258, 0x4}, {0x259, 0x0}, {0x25a, 0x0}, {0x25e, 0x8},
    {0x262, 0x8}, {0x266, 0x0}, {0x267, 0x0}, {0x26b, 0x0}, {0x26f, 0x10}, {0x1f7, 0x1}, {0x1f8, 0x2},
    {0x1f9, 0x3}, {0x1fa, 0x7}, {0x1fb, 0x6}, {0x1fc, 0x18}, {0x1fd, 0x5}, {0x1fe, 0x8}, {0x1ff, 0xb},
    {0x200, 0x19}, {0x201, 0x4}, {0x202, 0x11}, {0x203, 0x12}, {0x204, 0x13}, {0x205, 0x14}, {0x206, 0x15},
    {0x207, 0x16}, {0x208, 0xe}, {0x209, 0xf}, {0x20a, 0x10}, {0x20c, 0xa}, {0x20d, 0xc}, {0x20e, 0x9},
    {0x210, 0xd}, {0x216, 0x17}, {0x213, 0x1a}, {0x212, 0x1b}, {0x236, 0x1},
};

constexpr ByteStore kRace2Defaults[] = {
    {0x24a, 0x8}, {0x24b, 0x4}, {0x24f, 0x0}, {0x250, 0x0}, {0x251, 0x8}, {0x270, 0x0}, {0x271, 0x0},
    {0x272, 0x8}, {0x254, 0x0}, {0x256, 0x0}, {0x258, 0x0}, {0x259, 0x10}, {0x25a, 0x10}, {0x25e, 0x0},
    {0x262, 0x0}, {0x266, 0x0}, {0x267, 0x0}, {0x26b, 0x0}, {0x26f, 0x0}, {0x1f7, 0x1}, {0x1f8, 0x2},
    {0x1f9, 0x3}, {0x1fa, 0x7}, {0x1fb, 0x6}, {0x1fc, 0x18}, {0x1fd, 0x5}, {0x1fe, 0x8}, {0x1ff, 0xb},
    {0x200, 0x19}, {0x201, 0x4}, {0x202, 0x11}, {0x203, 0x12}, {0x204, 0x13}, {0x205, 0x14}, {0x206, 0x15},
    {0x207, 0x16}, {0x208, 0xe}, {0x209, 0xf}, {0x20a, 0x10}, {0x20c, 0xa}, {0x20d, 0xc}, {0x20e, 0x9},
    {0x210, 0xd}, {0x216, 0x17}, {0x217, 0x1a}, {0x214, 0x1c}, {0x211, 0x1b}, {0x20f, 0x1d},
};

constexpr ByteStore kRace3Defaults[] = {
    {0x24a, 0x0}, {0x24b, 0x0}, {0x24f, 0x0}, {0x250, 0x0}, {0x251, 0x0}, {0x270, 0x0}, {0x271, 0x0},
    {0x272, 0x0}, {0x254, 0x0}, {0x256, 0x0}, {0x258, 0x0}, {0x259, 0x0}, {0x25a, 0x0}, {0x25e, 0x0},
    {0x262, 0x0}, {0x266, 0x0}, {0x267, 0x0}, {0x26b, 0x0}, {0x26f, 0x8}, {0x273, 0x8}, {0x274, 0x8},
    {0x275, 0x8}, {0x276, 0x8}, {0x277, 0x8}, {0x1f7, 0x1}, {0x1f8, 0x2}, {0x1f9, 0x3}, {0x1fa, 0x7},
    {0x1fb, 0x6}, {0x1fc, 0x18}, {0x1fd, 0x5}, {0x1fe, 0x8}, {0x1ff, 0xb}, {0x200, 0x19}, {0x201, 0x4},
    {0x202, 0x11}, {0x203, 0x12}, {0x204, 0x13}, {0x205, 0x14}, {0x206, 0x15}, {0x207, 0x16}, {0x208, 0xe},
    {0x209, 0xf}, {0x20a, 0x10}, {0x20c, 0xa}, {0x20d, 0xc}, {0x20e, 0x9}, {0x210, 0xd}, {0x216, 0x17},
    {0x218, 0x1a}, {0x219, 0x1b}, {0x212, 0x1c}, {0x20f, 0x1d},
};

template <typename T>
T& At(GameWorld* world, std::uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<std::uint8_t*>(world) + offset);
}

std::uint32_t AreaRecord(std::uint32_t area) {
    return kAreas + area * kAreaBytes;
}

std::uint32_t TileAreaWord(GameWorld* world, std::uint32_t x, std::uint32_t y) {
    return At<std::uint16_t>(world, (x * 0x300 + y + kTileAreaIndexBias) * 24);
}

std::uint8_t& TileFlags(GameWorld* world, std::uint32_t x, std::uint32_t y) {
    return At<std::uint8_t>(world, kTiles + (x * 0x300 + y) * 24 + kTileFlags);
}

template <std::size_t N>
void StoreAll(GameWorld* world, std::uint32_t record, const ByteStore (&stores)[N]) {
    for (const ByteStore& store : stores) {
        At<std::uint8_t>(world, record + store.offset) = store.value;
    }
}

// Moves the members of a doubly linked list (uint16 head at `from`) whose
// tile is in area `area` to the front of the list at `to`. Records of
// `bytes` bytes at `table` with position words +x/+y and links +previous/+next.
void MoveListMembers(GameWorld* world, std::uint32_t from, std::uint32_t to, std::uint32_t area, std::uint32_t table,
                     std::uint32_t bytes, std::uint32_t xField, std::uint32_t yField, std::uint32_t previousField,
                     std::uint32_t nextField) {
    auto record = [&](std::uint32_t member) { return table + member * bytes; };
    std::uint32_t member = At<std::uint16_t>(world, from);
    while (member != 0) {
        const std::uint32_t r = record(member);
        const std::uint16_t next = At<std::uint16_t>(world, r + nextField);
        if (TileAreaWord(world, At<std::uint16_t>(world, r + xField), At<std::uint16_t>(world, r + yField)) == area) {
            const std::uint16_t previous = At<std::uint16_t>(world, r + previousField);
            if (previous != 0) {
                At<std::uint16_t>(world, record(previous) + nextField) = next;
            } else {
                At<std::uint16_t>(world, from) = next;
            }
            const std::uint16_t after = At<std::uint16_t>(world, r + nextField);
            if (after != 0) {
                At<std::uint16_t>(world, record(after) + previousField) = At<std::uint16_t>(world, r + previousField);
            }
            const std::uint16_t head = At<std::uint16_t>(world, to);
            At<std::uint16_t>(world, r + nextField) = head;
            if (head != 0) {
                At<std::uint16_t>(world, record(head) + previousField) = static_cast<std::uint16_t>(member);
            }
            At<std::uint16_t>(world, r + previousField) = 0;
            At<std::uint16_t>(world, to) = static_cast<std::uint16_t>(member);
        }
        member = next;
    }
}

void MovePiles(GameWorld* world, std::uint32_t from, std::uint32_t to, std::uint32_t area) {
    MoveListMembers(world, from, to, area, kPiles, kPileBytes, kPileX, kPileY, kPilePrevious, kPileNext);
}

void MoveSettlers(GameWorld* world, std::uint32_t from, std::uint32_t to, std::uint32_t area) {
    MoveListMembers(world, from, to, area, kSettlers, kSettlerBytes, kSettlerX, kSettlerY, kSettlerPrevious,
                    kSettlerNext);
}

}  // namespace

using InitAreaFn = void(__fastcall*)(GameWorld* world, void* edx, std::uint32_t area, std::uint32_t player);
using AllocAreaFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t player);
using MoveAreaContentsFn = void(__fastcall*)(GameWorld* world, void* edx, std::uint32_t oldArea,
                                             std::uint32_t newArea);
using MergeAreaContentsFn = void(__fastcall*)(GameWorld* world, void* edx, std::uint32_t into, std::uint32_t from);
using TileFn = void(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);
using SetOwnerFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y,
                                              std::uint32_t owner);
using BlockTileFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);

// Purpose: Sets up a new area for a player: empty lists, and the default economy settings (probably goods
// distribution and priorities), which depend on the player's race.
void __fastcall GameWorld_InitArea(GameWorld* world, void*, std::uint32_t area, std::uint32_t player) {
    GameWorld_QueuePendingEvent(world, nullptr, kPendingAreaEvent, area, 0);
    GameWorld_QueueEvent(world, nullptr, kAreaEvent, area, 0, 0);
    const std::uint32_t r = AreaRecord(area);
    At<std::uint8_t>(world, r + kAreaPlayer) = static_cast<std::uint8_t>(player);
    At<std::uint32_t>(world, r + kAreaTileCount) = 0;
    At<std::uint16_t>(world, r + kAreaPileList) = 0;
    At<std::uint8_t>(world, r + 0x23e) = 0x14;
    At<std::uint8_t>(world, r + 0x23f) = 0x19;
    At<std::uint8_t>(world, r + 0x240) = 0x19;
    At<std::uint8_t>(world, r + 0x241) = 1;
    At<std::uint8_t>(world, r + 0x242) = 0;
    At<std::uint8_t>(world, r + 0x243) = 0;
    At<std::uint8_t>(world, r + 0x244) = 0;
    for (std::uint32_t i = 0; i < kGoodsTypes; ++i) {
        At<std::uint16_t>(world, r + kAreaGoodsPiles + i * 2) = 0;
        At<std::uint16_t>(world, r + kAreaRequests + i * 2) = 0;
        At<std::uint16_t>(world, r + kAreaSlotsA + i * 2) = 0;
        At<std::uint16_t>(world, r + kAreaSlotsB + i * 2) = 0;
        for (const std::uint32_t array : kGoodsArraysInitOrder) {
            At<std::uint8_t>(world, r + array + i) = 0;
        }
    }
    for (std::uint32_t i = 0; i < kAreaSettlerListCount; ++i) {
        At<std::uint16_t>(world, r + kAreaSettlerLists + i * 2) = 0;
    }
    for (std::uint32_t i = 0; i < kAreaCount51; ++i) {
        At<std::uint8_t>(world, r + kAreaBytes51 + i) = 0;
        At<std::uint16_t>(world, r + kAreaWords51 + i * 2) = 0;
    }
    StoreAll(world, r, kAreaDefaults);
    const std::uint32_t race = kPlayerRace + player * kPlayerRecordBytes;
    if (At<std::int32_t>(world, race) == 0) {
        StoreAll(world, r, kRace0Defaults);
    }
    if (At<std::int32_t>(world, race) == 1) {
        StoreAll(world, r, kRace1Defaults);
    }
    if (At<std::int32_t>(world, race) == 2) {
        StoreAll(world, r, kRace2Defaults);
    }
    if (At<std::int32_t>(world, race) == 3) {
        StoreAll(world, r, kRace3Defaults);
    }
}

// Purpose: Finds an unused area number, sets it up for the player (InitArea) and returns it; 0 if all
// are in use.
std::uint32_t __fastcall GameWorld_AllocArea(GameWorld* world, void*, std::uint32_t player) {
    std::uint32_t area = 1;
    while (At<std::uint8_t>(world, AreaRecord(area) + kAreaPlayer) != kFreeArea) {
        if (area == kLastArea) {
            return 0;
        }
        ++area;
    }
    if (area == kLastArea) {
        return 0;
    }
    if (area > At<std::uint32_t>(world, kAreaHighWater)) {
        At<std::uint32_t>(world, kAreaHighWater) = area;
    }
    GameWorld_InitArea(world, nullptr, area, player);
    return area;
}

// Purpose: After part of an area became a new area, moves the goods piles and settlers lying in that part
// to the new area's lists, and gives the new area the old one's economy settings.
void __fastcall GameWorld_MoveAreaContents(GameWorld* world, void*, std::uint32_t oldArea, std::uint32_t newArea) {
    GameWorld_QueuePendingEvent(world, nullptr, kPendingAreaEvent, oldArea, 0);
    GameWorld_QueuePendingEvent(world, nullptr, kPendingAreaEvent, newArea, 0);
    GameWorld_QueueEvent(world, nullptr, kAreaEvent, oldArea, 0, 0);
    GameWorld_QueueEvent(world, nullptr, kAreaEvent, newArea, 0, 0);
    const std::uint32_t from = AreaRecord(oldArea);
    const std::uint32_t to = AreaRecord(newArea);
    MovePiles(world, from + kAreaPileList, to + kAreaPileList, newArea);
    for (std::uint32_t i = 0; i < kGoodsTypes; ++i) {
        MovePiles(world, from + kAreaGoodsPiles + i * 2, to + kAreaGoodsPiles + i * 2, newArea);
        for (const std::uint32_t list : {from, to}) {
            At<std::uint16_t>(world, list + kAreaRequests + i * 2) = 0;
            At<std::uint16_t>(world, list + kAreaSlotsA + i * 2) = 0;
            At<std::uint16_t>(world, list + kAreaSlotsB + i * 2) = 0;
        }
        for (const std::uint32_t array : kGoodsArraysCopyOrder) {
            At<std::uint8_t>(world, to + array + i) = At<std::uint8_t>(world, from + array + i);
        }
    }
    for (std::uint32_t i = 0; i < kAreaSettlerListCount; ++i) {
        MoveSettlers(world, from + kAreaSettlerLists + i * 2, to + kAreaSettlerLists + i * 2, newArea);
    }
    for (const std::uint32_t field : {0x23e, 0x23f, 0x240}) {
        At<std::uint8_t>(world, to + field) = At<std::uint8_t>(world, from + field);
    }
    for (std::uint32_t i = 0; i < kAreaCount51; ++i) {
        At<std::uint8_t>(world, to + kAreaBytes51 + i) = At<std::uint8_t>(world, from + kAreaBytes51 + i);
        At<std::uint16_t>(world, to + kAreaWords51 + i * 2) = 0;
        At<std::uint16_t>(world, from + kAreaWords51 + i * 2) = 0;
    }
    At<std::uint8_t>(world, to + 0x241) = At<std::uint8_t>(world, from + 0x241);
}

// Purpose: When two areas grow together, moves all goods piles and settlers of one area into the other's
// lists; the bigger area's economy settings are kept.
void __fastcall GameWorld_MergeAreaContents(GameWorld* world, void*, std::uint32_t into, std::uint32_t from) {
    GameWorld_QueuePendingEvent(world, nullptr, kPendingAreaEvent, into, 0);
    GameWorld_QueuePendingEvent(world, nullptr, kPendingAreaEvent, from, 0);
    GameWorld_QueueEvent(world, nullptr, kAreaEvent, into, 0, 0);
    GameWorld_QueueEvent(world, nullptr, kAreaEvent, from, 0, 0);
    const std::uint32_t a = AreaRecord(into);
    const std::uint32_t b = AreaRecord(from);
    // Puts every member of the list at `source` at the front of the list at `target`.
    auto prependAll = [&](std::uint32_t source, std::uint32_t target, std::uint32_t table, std::uint32_t bytes,
                          std::uint32_t previousField, std::uint32_t nextField) {
        std::uint32_t member = At<std::uint16_t>(world, source);
        while (member != 0) {
            const std::uint32_t r = table + member * bytes;
            const std::uint16_t next = At<std::uint16_t>(world, r + nextField);
            const std::uint16_t head = At<std::uint16_t>(world, target);
            At<std::uint16_t>(world, r + nextField) = head;
            if (head != 0) {
                At<std::uint16_t>(world, table + head * bytes + previousField) = static_cast<std::uint16_t>(member);
            }
            At<std::uint16_t>(world, r + previousField) = 0;
            At<std::uint16_t>(world, target) = static_cast<std::uint16_t>(member);
            member = next;
        }
    };
    prependAll(b + kAreaPileList, a + kAreaPileList, kPiles, kPileBytes, kPilePrevious, kPileNext);
    for (std::uint32_t i = 0; i < kGoodsTypes; ++i) {
        prependAll(b + kAreaGoodsPiles + i * 2, a + kAreaGoodsPiles + i * 2, kPiles, kPileBytes, kPilePrevious,
                   kPileNext);
        At<std::uint16_t>(world, a + kAreaRequests + i * 2) = 0;
        At<std::uint16_t>(world, a + kAreaSlotsA + i * 2) = 0;
        At<std::uint16_t>(world, a + kAreaSlotsB + i * 2) = 0;
    }
    for (std::uint32_t i = 0; i < kAreaSettlerListCount; ++i) {
        prependAll(b + kAreaSettlerLists + i * 2, a + kAreaSettlerLists + i * 2, kSettlers, kSettlerBytes,
                   kSettlerPrevious, kSettlerNext);
    }
    for (const std::uint32_t field : {0x242, 0x243, 0x244}) {
        At<std::uint8_t>(world, a + field) =
            static_cast<std::uint8_t>(At<std::uint8_t>(world, a + field) + At<std::uint8_t>(world, b + field));
    }
    if (At<std::uint32_t>(world, a + kAreaTileCount) < At<std::uint32_t>(world, b + kAreaTileCount)) {
        for (std::uint32_t i = 0; i < kGoodsTypes; ++i) {
            for (const std::uint32_t array : kGoodsArraysCopyOrder) {
                At<std::uint8_t>(world, a + array + i) = At<std::uint8_t>(world, b + array + i);
            }
        }
        for (const std::uint32_t field : {0x23e, 0x23f, 0x240}) {
            At<std::uint8_t>(world, a + field) = At<std::uint8_t>(world, b + field);
        }
        for (std::uint32_t i = 0; i < kAreaCount51; ++i) {
            At<std::uint8_t>(world, a + kAreaBytes51 + i) = At<std::uint8_t>(world, b + kAreaBytes51 + i);
        }
        At<std::uint8_t>(world, a + 0x241) = At<std::uint8_t>(world, b + 0x241);
    }
    for (std::uint32_t i = 0; i < kAreaCount51; ++i) {
        At<std::uint16_t>(world, a + kAreaWords51 + i * 2) = 0;
    }
}

// Purpose: When a tile becomes part of the land again (probably when it is no longer blocked), puts it into
// the area of its neighbours of the same owner, merging their areas if they were different, or into a new
// area if it has no such neighbour.
void __fastcall GameWorld_JoinAreasAtTile(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t ownerField = kTiles + (x * 0x300 + y) * 24 + kTileOwner;
    const std::uint32_t owner = At<std::uint8_t>(world, ownerField);
    std::uint32_t count = 0;
    std::uint32_t base = 0;
    for (std::uint32_t k = 0; k < kDirections; ++k) {
        const std::uint32_t entry = kNeighbourOffsets + k * kOffsetBytes;
        const std::uint32_t nx = x + At<std::uint32_t>(world, entry);
        const std::uint32_t ny = y + At<std::uint32_t>(world, entry + 4);
        const std::uint32_t tile = kTiles + (nx * 0x300 + ny) * 24;
        if ((At<std::uint8_t>(world, tile + kTileFlags) & kTileBlocked) != 0 ||
            At<std::uint8_t>(world, tile + kTileOwner) != owner) {
            continue;
        }
        ++count;
        if (count == 1) {
            base = TileAreaWord(world, nx, ny);
            GameWorld_AssignTileToArea(world, nullptr, x, y, base);
            continue;
        }
        const std::uint32_t other = TileAreaWord(world, nx, ny);
        if (other == base) {
            continue;
        }
        GameWorld_MergeAreaContents(world, nullptr, base, other);
        GameWorld_FloodMoveArea(world, nullptr, x + At<std::uint32_t>(world, entry), y + At<std::uint32_t>(world, entry + 4),
                                base);
        At<std::uint8_t>(world, AreaRecord(other) + kAreaPlayer) = kFreeArea;
        GameWorld_QueuePendingEvent(world, nullptr, kPendingAreaEvent, other, 0);
        GameWorld_QueueEvent(world, nullptr, kAreaEvent, other, 0, 0);
    }
    if (count == 0) {
        const std::uint32_t area = GameWorld_AllocArea(world, nullptr, At<std::uint8_t>(world, ownerField));
        GameWorld_AssignTileToArea(world, nullptr, x, y, area);
    }
}

// Purpose: When a tile leaves its area (e.g. it gets blocked), checks whether the rest of the area is still
// connected around it; parts that got cut off become new areas with their own piles and settlers, and an
// area with no tiles left next to it is freed.
void __fastcall GameWorld_SplitAreaAtTile(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t area = TileAreaWord(world, x, y);
    GameWorld_RemoveTileFromArea(world, nullptr, x, y);
    std::uint32_t count = 0;
    for (std::uint32_t k = 0; k < kDirections; ++k) {
        const std::uint32_t entry = kNeighbourOffsets + k * kOffsetBytes;
        if (TileAreaWord(world, At<std::uint32_t>(world, entry) + x, At<std::uint32_t>(world, entry + 4) + y) ==
            area) {
            ++count;
        }
    }
    if (count == 0) {
        At<std::uint8_t>(world, AreaRecord(area) + kAreaPlayer) = kFreeArea;
        GameWorld_QueuePendingEvent(world, nullptr, kPendingAreaEvent, area, 0);
        GameWorld_QueueEvent(world, nullptr, kAreaEvent, area, 0, 0);
        return;
    }
    if (count < 2 || count > 4) {
        return;
    }
    // Mark the neighbours in the area; the walk starts from the last one.
    std::uint32_t lastDirection = 0;
    for (std::uint32_t d = 0; d < kDirections; ++d) {
        const std::uint32_t nx = NeighbourX(x, d);
        const std::uint32_t ny = NeighbourY(y, d);
        if (GameWorld_IsOnMap(world, nullptr, nx, ny) == 0) {
            continue;
        }
        if (TileAreaWord(world, nx, ny) == area) {
            TileFlags(world, nx, ny) |= kTileSplitMark;
            lastDirection = d;
        } else {
            TileFlags(world, nx, ny) &= static_cast<std::uint8_t>(~kTileSplitMark);
        }
    }
    const std::uint32_t startX = NeighbourX(x, lastDirection);
    const std::uint32_t startY = NeighbourY(y, lastDirection);
    std::uint32_t direction = lastDirection + 3;
    if (direction >= kDirections) {
        direction -= kDirections;
    }
    TileFlags(world, startX, startY) &= static_cast<std::uint8_t>(~kTileSplitMark);
    std::uint32_t cx = startX;
    std::uint32_t cy = startY;
    bool stepped = false;          // the walk has made its first step
    std::uint32_t firstDirection = 0;  // the direction of that step
    bool done = false;
    for (;;) {
        // Turn until a neighbour in the area is found (at most six tries).
        for (std::uint32_t tries = 0; tries < kDirections;) {
            const std::uint32_t tx = NeighbourX(cx, direction);
            const std::uint32_t ty = NeighbourY(cy, direction);
            if (GameWorld_IsOnMap(world, nullptr, tx, ty) != 0 && TileAreaWord(world, tx, ty) == area) {
                if (stepped && cx == startX && cy == startY && firstDirection == direction) {
                    done = true;  // back at the first step
                }
                cx = tx;
                cy = ty;
                if (!stepped) {
                    stepped = true;
                    firstDirection = direction;
                }
                direction = direction == 0 ? kDirections - 1 : direction - 1;
                TileFlags(world, tx, ty) &= static_cast<std::uint8_t>(~kTileSplitMark);
                break;
            }
            ++tries;
            if (tries == kDirections && !stepped) {
                done = true;  // nothing to walk to
            }
            direction = direction == kDirections - 1 ? 0 : direction + 1;
        }
        // Still a marked neighbour left?
        std::uint32_t d = 0;
        while ((TileFlags(world, NeighbourX(x, d), NeighbourY(y, d)) & kTileSplitMark) == 0) {
            if (++d == kDirections) {
                return;  // all reached: the area is still connected
            }
        }
        if (done) {
            break;
        }
    }
    for (std::uint32_t d = 0; d < kDirections; ++d) {
        const std::uint32_t nx = NeighbourX(x, d);
        const std::uint32_t ny = NeighbourY(y, d);
        if (TileAreaWord(world, nx, ny) != area || (TileFlags(world, nx, ny) & kTileSplitMark) == 0) {
            continue;
        }
        const std::uint32_t newArea =
            GameWorld_AllocArea(world, nullptr, At<std::uint8_t>(world, kTiles + (nx * 0x300 + ny) * 24 + kTileOwner));
        GameWorld_FloodMoveArea(world, nullptr, nx, ny, newArea);
        At<std::uint8_t>(world, AreaRecord(newArea) + kAreaPlayer) = At<std::uint8_t>(world, AreaRecord(area) + kAreaPlayer);
        GameWorld_MoveAreaContents(world, nullptr, area, newArea);
    }
}

// Purpose: Makes a tile impassable (e.g. when something is built or placed on it) and updates the
// regions and areas that the tile connected.
std::uint32_t __fastcall GameWorld_BlockTile(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t tile = kTiles + (x * 0x300 + y) * 24;
    const std::uint8_t flags = At<std::uint8_t>(world, tile + kTileFlags);
    if ((flags & kTileBlocked) != 0) {
        return x * 0x300 + flags;
    }
    At<std::uint8_t>(world, tile + kTileFlags) = static_cast<std::uint8_t>(flags | kTileBlocked);
    if (At<std::uint8_t>(world, tile + kTileByte10) != 0) {
        GameWorld_SplitTileByte10Region(world, nullptr, x, y);
    }
    const std::uint16_t area = At<std::uint16_t>(world, (x * 0x300 + y + kTileAreaIndexBias) * 24);
    if (area != 0 && area < 0x8000) {
        GameWorld_SplitAreaAtTile(world, nullptr, x, y);
    }
    return GameWorld_SplitWord16RegionsAround(world, nullptr, x, y);
}

// Purpose: Makes a blocked tile passable again (the opposite of BlockTile) and joins it to the regions and
// areas around it.
std::uint32_t __fastcall GameWorld_UnblockTile(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t tile = kTiles + (x * 0x300 + y) * 24;
    const std::uint8_t flags = At<std::uint8_t>(world, tile + kTileFlags);
    if ((flags & kTileBlocked) == 0) {
        return x * 0x300 + flags;
    }
    At<std::uint8_t>(world, tile + kTileFlags) = static_cast<std::uint8_t>(flags & ~kTileBlocked);
    if (At<std::uint8_t>(world, tile + kTileByte10) == 0) {
        GameWorld_MergeTileByte10Regions(world, nullptr, x, y);
    }
    if (At<std::uint16_t>(world, (x * 0x300 + y + kTileAreaIndexBias) * 24) == 0 &&
        At<std::uint8_t>(world, tile + kTileOwner) != 0xff) {
        GameWorld_JoinAreasAtTile(world, nullptr, x, y);
    }
    return GameWorld_AssignWord16RegionsAround(world, nullptr, x, y);
}

// Purpose: Gives a tile that has no owner yet to a player (e.g. when territory grows), and puts it and the
// goods pile lying on it into that player's area.
std::uint32_t __fastcall GameWorld_SetOwnerOfFreeTile(GameWorld* world, void*, std::uint32_t x, std::uint32_t y,
                                                      std::uint32_t owner) {
    const std::uint32_t index = x * 0x300 + y;
    const std::uint32_t tile = kTiles + index * 24;
    if (At<std::uint8_t>(world, tile + kTileOwner) != 0xff) {
        return index;
    }
    At<std::uint8_t>(world, tile + kTileOwner) = static_cast<std::uint8_t>(owner);
    const std::uint32_t areaIndex = index * 3 + 0xce8b;  // * 8 = the tile's area word
    if (At<std::uint16_t>(world, areaIndex * 8) != 0 || (At<std::uint8_t>(world, tile + kTileFlags) & kTileBlocked) != 0 ||
        owner == 0xff) {
        return areaIndex;
    }
    GameWorld_JoinAreasAtTile(world, nullptr, x, y);
    return GameWorld_AddTilePileToArea(world, nullptr, x, y);
}

S3RE_HOOK(0x004e6510, GameWorld_InitArea, InitAreaFn, Original_GameWorld_InitArea, GameWorld_InitArea);
S3RE_HOOK(0x004e64c0, GameWorld_AllocArea, AllocAreaFn, Original_GameWorld_AllocArea, GameWorld_AllocArea);
S3RE_HOOK(0x004e54f0, GameWorld_MoveAreaContents, MoveAreaContentsFn, Original_GameWorld_MoveAreaContents,
          GameWorld_MoveAreaContents);
S3RE_HOOK(0x004e5a20, GameWorld_MergeAreaContents, MergeAreaContentsFn, Original_GameWorld_MergeAreaContents,
          GameWorld_MergeAreaContents);
S3RE_HOOK(0x004e6150, GameWorld_JoinAreasAtTile, TileFn, Original_GameWorld_JoinAreasAtTile,
          GameWorld_JoinAreasAtTile);
S3RE_HOOK(0x004e5d50, GameWorld_SplitAreaAtTile, TileFn, Original_GameWorld_SplitAreaAtTile,
          GameWorld_SplitAreaAtTile);
S3RE_HOOK(0x0051b6f0, GameWorld_BlockTile, BlockTileFn, Original_GameWorld_BlockTile, GameWorld_BlockTile);
S3RE_HOOK(0x0051b770, GameWorld_UnblockTile, BlockTileFn, Original_GameWorld_UnblockTile, GameWorld_UnblockTile);
S3RE_HOOK(0x0051b5d0, GameWorld_SetOwnerOfFreeTile, SetOwnerFn, Original_GameWorld_SetOwnerOfFreeTile,
          GameWorld_SetOwnerOfFreeTile);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kTestWorld = kSettlers + 0x10000 * kSettlerBytes;
constexpr std::uint32_t kOffsetTable = 0x110a588;
constexpr std::uint32_t kTileWord16 = 0x16;
constexpr std::uint32_t kIds256 = 0xe114dc;
constexpr std::uint32_t kIds256Mark = 0xe114d8;
constexpr std::uint32_t kIds8192 = 0xe125e8;
constexpr std::uint32_t kIds8192Mark = 0xe125e4;
constexpr std::uint32_t kPendingRead = 0x32870;
constexpr std::uint32_t kPendingWrite = 0x32874;
constexpr std::uint32_t kEventWrite = 0x366fc;
constexpr std::uint32_t kUsedAreas = 3;     // areas 1..3 hold the map's tiles
constexpr std::uint32_t kTestAreas = 16;    // 4..16 used or free
constexpr std::int32_t kSteps[19][2] = {{0, 0},  {1, 0},  {1, 1},   {0, 1},   {-1, 0}, {-1, -1}, {0, -1},
                                        {2, 0},  {2, 1},  {2, 2},   {1, 2},   {0, 2},  {-1, 1},  {-2, 0},
                                        {-2, -1}, {-2, -2}, {-1, -2}, {0, -2}, {1, -1}};

std::uint32_t Tile(std::uint32_t x, std::uint32_t y) {
    return kTiles + (x * 0x300 + y) * 24;
}
std::uint32_t TileArea(std::uint32_t x, std::uint32_t y) {
    return (x * 0x300 + y + kTileAreaIndexBias) * 24;
}

// Event ring indices in range (InitArea and MoveAreaContents queue events).
void SetUpEvents(test::Context& ctx, test::WorldPair& worlds) {
    worlds.Set<std::uint32_t>(kPendingRead, ctx.rng().Below(1000));
    worlds.Set<std::uint32_t>(kPendingWrite, ctx.rng().Below(1000));
    worlds.Set<std::uint32_t>(kEventWrite, ctx.rng().Below(1000));
}

// The real offset table entries 0..18 (the tile, its six neighbours, the ring around them).
void SetUpOffsets(test::WorldPair& worlds) {
    for (std::uint32_t k = 0; k < 19; ++k) {
        worlds.Set<std::int32_t>(kOffsetTable + k * kOffsetBytes, kSteps[k][0]);
        worlds.Set<std::int32_t>(kOffsetTable + k * kOffsetBytes + 4, kSteps[k][1]);
    }
}

// Valid, terminated lists for the area: the pile list +0x8, the 36 goods
// pile lists and the 48 settler lists, each empty or a few fresh members on
// tiles of the size x size map. `nextPile`/`nextSettler` hand out numbers.
void SetUpAreaLists(test::Context& ctx, test::WorldPair& worlds, std::uint32_t area, std::uint32_t size,
                    std::uint32_t& nextPile, std::uint32_t& nextSettler) {
    const std::uint32_t r = AreaRecord(area);
    auto fill = [&](std::uint32_t head, std::uint32_t table, std::uint32_t bytes, std::uint32_t xField,
                    std::uint32_t yField, std::uint32_t previousField, std::uint32_t nextField, std::uint32_t& next) {
        const std::uint32_t length = ctx.rng().Chance(1, 5) ? 1 + ctx.rng().Below(4) : 0;
        std::uint32_t previous = 0;
        worlds.Set<std::uint16_t>(head, static_cast<std::uint16_t>(length ? next : 0));
        for (std::uint32_t k = 0; k < length; ++k) {
            const std::uint32_t member = next++;
            const std::uint32_t m = table + member * bytes;
            worlds.Set<std::uint16_t>(m + previousField, static_cast<std::uint16_t>(previous));
            worlds.Set<std::uint16_t>(m + nextField, static_cast<std::uint16_t>(k + 1 < length ? next : 0));
            worlds.Set<std::uint16_t>(m + xField, static_cast<std::uint16_t>(ctx.rng().Below(size)));
            worlds.Set<std::uint16_t>(m + yField, static_cast<std::uint16_t>(ctx.rng().Below(size)));
            previous = member;
        }
    };
    fill(r + kAreaPileList, kPiles, kPileBytes, kPileX, kPileY, kPilePrevious, kPileNext, nextPile);
    for (std::uint32_t i = 0; i < kGoodsTypes; ++i) {
        fill(r + kAreaGoodsPiles + i * 2, kPiles, kPileBytes, kPileX, kPileY, kPilePrevious, kPileNext, nextPile);
    }
    for (std::uint32_t i = 0; i < kAreaSettlerListCount; ++i) {
        fill(r + kAreaSettlerLists + i * 2, kSettlers, kSettlerBytes, kSettlerX, kSettlerY, kSettlerPrevious,
             kSettlerNext, nextSettler);
    }
}

// A size x size map whose tiles are in areas 1..3 (patches; sometimes no
// area), the frame off the map in none; areas 1..3 used with valid lists,
// 4..16 used or free (16 free unless `full`), players' races 0..3 (or other).
// With `stripes`, every tile of the map is in an area and each area is one
// connected stripe (along x, y or x - y): merging areas assumes that an
// area is connected (MergeAreaContents leaves the merged area's list heads
// as they were, so merging a second piece of it would link lists twice).
void SetUpAreaMap(test::Context& ctx, test::WorldPair& worlds, std::uint32_t size, bool full = false,
                  bool stripes = false) {
    const std::uint32_t patch = 1 + ctx.rng().Below(4);
    const std::uint32_t empty = ctx.rng().Below(3);  // of 8
    const std::uint32_t orientation = ctx.rng().Below(3);
    const std::int32_t first = ctx.rng().Range(-2, static_cast<std::int32_t>(size));
    const std::int32_t second = first + ctx.rng().Range(1, static_cast<std::int32_t>(size));
    for (std::uint32_t tx = 0xffffffffu; tx != size + 1; ++tx) {
        for (std::uint32_t ty = 0xffffffffu; ty != size + 1; ++ty) {
            const bool onMap = tx < size && ty < size;
            std::uint32_t area = 0;
            if (onMap && stripes) {
                const std::int32_t f = orientation == 0   ? static_cast<std::int32_t>(tx)
                                       : orientation == 1 ? static_cast<std::int32_t>(ty)
                                                          : static_cast<std::int32_t>(tx) - static_cast<std::int32_t>(ty);
                area = f < first ? 1 : f < second ? 2 : 3;
            } else if (onMap && ctx.rng().Below(8) >= empty) {
                area = 1 + ((tx / patch) * 5 + (ty / patch) * 3 + (ctx.rng().Chance(1, 6) ? 1 : 0)) % kUsedAreas;
            }
            worlds.Set<std::uint16_t>(TileArea(tx, ty), static_cast<std::uint16_t>(area));
            worlds.Set<std::uint8_t>(Tile(tx, ty) + kTileFlags, static_cast<std::uint8_t>(ctx.rng().Next32()));
            worlds.Set<std::uint8_t>(Tile(tx, ty) + kTileOwner, static_cast<std::uint8_t>(ctx.rng().Below(20)));
        }
    }
    std::uint32_t nextPile = 1 + ctx.rng().Below(0x1000);
    std::uint32_t nextSettler = 1 + ctx.rng().Below(0x1000);
    for (std::uint32_t area = 1; area <= kTestAreas; ++area) {
        const bool used = area <= kUsedAreas || (area < kTestAreas && ctx.rng().Chance(1, 2)) || full;
        worlds.Set<std::uint8_t>(AreaRecord(area) + kAreaPlayer,
                                 static_cast<std::uint8_t>(used ? ctx.rng().Below(20) : kFreeArea));
        if (area <= kUsedAreas) {
            SetUpAreaLists(ctx, worlds, area, size, nextPile, nextSettler);
        }
    }
    for (std::uint32_t player = 0; player < 20; ++player) {
        worlds.Set<std::int32_t>(kPlayerRace + player * kPlayerRecordBytes,
                                 ctx.rng().Chance(1, 10) ? static_cast<std::int32_t>(ctx.rng().Next32())
                                                         : static_cast<std::int32_t>(ctx.rng().Below(4)));
    }
    worlds.Set<std::uint32_t>(kAreaHighWater, ctx.rng().Below(kTestAreas + 2));
    SetUpEvents(ctx, worlds);
}

// Word-16 regions as in the game: each connected stretch of the map's tiles
// (randomly some without) gets its own id (a shared id could be freed and
// handed out again while in use: the flood fill would loop for ever).
void SetUpWord16Regions(test::Context& ctx, test::WorldPair& worlds, std::uint32_t size) {
    const std::uint32_t density = 4 + ctx.rng().Below(5);  // of 8
    std::vector<std::uint8_t> walkable(size * size);
    for (auto& w : walkable) {
        w = ctx.rng().Below(8) < density ? 1 : 0;
    }
    std::vector<std::uint32_t> label(size * size, 0);
    std::uint32_t next = 1;
    std::vector<std::uint32_t> queue;
    for (std::uint32_t start = 0; start < size * size; ++start) {
        if (!walkable[start] || label[start] != 0) {
            continue;
        }
        label[start] = next;
        queue.assign(1, start);
        while (!queue.empty()) {
            const std::uint32_t t = queue.back();
            queue.pop_back();
            for (std::uint32_t k = 1; k <= 6; ++k) {
                const std::int32_t nx = static_cast<std::int32_t>(t / size) + kSteps[k][0];
                const std::int32_t ny = static_cast<std::int32_t>(t % size) + kSteps[k][1];
                if (nx < 0 || ny < 0 || nx >= static_cast<std::int32_t>(size) || ny >= static_cast<std::int32_t>(size)) {
                    continue;
                }
                const std::uint32_t n = static_cast<std::uint32_t>(nx) * size + static_cast<std::uint32_t>(ny);
                if (walkable[n] && label[n] == 0) {
                    label[n] = next;
                    queue.push_back(n);
                }
            }
        }
        ++next;
    }
    for (std::uint32_t tx = 0xffffffffu; tx != size + 1; ++tx) {
        for (std::uint32_t ty = 0xffffffffu; ty != size + 1; ++ty) {
            const bool onMap = tx < size && ty < size;
            worlds.Set<std::uint16_t>(Tile(tx, ty) + kTileWord16,
                                      static_cast<std::uint16_t>(onMap ? label[tx * size + ty] : 0));
            // Byte-10 regions: one region over the whole map (one split per call is safe).
            worlds.Set<std::uint8_t>(Tile(tx, ty) + kTileByte10,
                                     static_cast<std::uint8_t>(onMap && ctx.rng().Chance(7, 8) ? 1 : 0));
        }
    }
    for (std::uint32_t id = 0; id < next + 2; ++id) {
        worlds.Set<std::uint8_t>(kIds8192 + id, static_cast<std::uint8_t>(id != 0 && id < next ? 1 : 0));
    }
    worlds.Set<std::uint32_t>(kIds8192Mark, ctx.rng().Below(next + 2));
    for (std::uint32_t id = 0; id < 8; ++id) {
        worlds.Set<std::uint8_t>(kIds256 + id, static_cast<std::uint8_t>(id == 1 ? 1 : 0));
    }
    worlds.Set<std::uint32_t>(kIds256Mark, ctx.rng().Below(4));
}

// Wrong: race 1 gets race 0's defaults.
void __fastcall WrongInitArea(GameWorld* world, void* edx, std::uint32_t area, std::uint32_t player) {
    GameWorld_InitArea(world, edx, area, player);
    if (At<std::int32_t>(world, kPlayerRace + player * kPlayerRecordBytes) == 1) {
        StoreAll(world, AreaRecord(area), kRace0Defaults);
    }
}

void CompareInitArea(test::Context& ctx, InitAreaFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        SetUpEvents(ctx, worlds);
        const std::uint32_t area = 1 + ctx.rng().Below(0xffe);
        const std::uint32_t player = ctx.rng().Chance(1, 10) ? ctx.rng().Below(256) : ctx.rng().Below(20);
        worlds.Set<std::int32_t>(kPlayerRace + player * kPlayerRecordBytes,
                                 ctx.rng().Chance(1, 8) ? static_cast<std::int32_t>(ctx.rng().Next32())
                                                        : static_cast<std::int32_t>(ctx.rng().Below(5)));
        Original_GameWorld_InitArea(worlds.original(), nullptr, area, player);
        candidate(worlds.ours(), nullptr, area, player);
        if ((i % 32 == 31 || i == cases - 1) && !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 31 before it): area %u player %u", i, area, player);
            return;
        }
    }
}

// Wrong: never hands out the last free area found by a full scan (off by one in the limit).
std::uint32_t __fastcall WrongAllocArea(GameWorld* world, void* edx, std::uint32_t player) {
    if (At<std::uint8_t>(world, AreaRecord(kLastArea - 1) + kAreaPlayer) == kFreeArea) {
        bool othersUsed = true;
        for (std::uint32_t a = 1; a < kLastArea - 1 && othersUsed; ++a) {
            othersUsed = At<std::uint8_t>(world, AreaRecord(a) + kAreaPlayer) != kFreeArea;
        }
        if (othersUsed) {
            return 0;
        }
    }
    return GameWorld_AllocArea(world, edx, player);
}

void CompareAllocArea(test::Context& ctx, AllocAreaFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        SetUpEvents(ctx, worlds);
        // Areas 1..first-1 used, `first` free (often near or past the end).
        const std::uint32_t r = ctx.rng().Below(8);
        const std::uint32_t first = r == 0 ? kLastArea - 1 : r == 1 ? kLastArea + ctx.rng().Below(2)
                                                       : 1 + ctx.rng().Below(64);
        for (std::uint32_t a = 1; a <= kLastArea; ++a) {
            worlds.Set<std::uint8_t>(AreaRecord(a) + kAreaPlayer,
                                     static_cast<std::uint8_t>(a == first ? kFreeArea : ctx.rng().Below(20)));
        }
        worlds.Set<std::uint32_t>(kAreaHighWater, ctx.rng().Chance(1, 2) ? first + ctx.rng().Below(3) - 1
                                                                          : ctx.rng().Next32());
        const std::uint32_t player = ctx.rng().Below(20);
        worlds.Set<std::int32_t>(kPlayerRace + player * kPlayerRecordBytes, static_cast<std::int32_t>(ctx.rng().Below(4)));
        const std::uint32_t expected = Original_GameWorld_AllocArea(worlds.original(), nullptr, player);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, player);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 16 == 15 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 15 before it): first free %u", i, first);
            return;
        }
    }
}

// Wrong: does not move settlers.
void __fastcall WrongMoveAreaContents(GameWorld* world, void* edx, std::uint32_t oldArea, std::uint32_t newArea) {
    std::uint16_t saved[kAreaSettlerListCount];
    for (std::uint32_t i = 0; i < kAreaSettlerListCount; ++i) {
        saved[i] = At<std::uint16_t>(world, AreaRecord(oldArea) + kAreaSettlerLists + i * 2);
        At<std::uint16_t>(world, AreaRecord(oldArea) + kAreaSettlerLists + i * 2) = 0;
    }
    GameWorld_MoveAreaContents(world, edx, oldArea, newArea);
    for (std::uint32_t i = 0; i < kAreaSettlerListCount; ++i) {
        At<std::uint16_t>(world, AreaRecord(oldArea) + kAreaSettlerLists + i * 2) = saved[i];
    }
}

void CompareMoveAreaContents(test::Context& ctx, MoveAreaContentsFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = 4 + ctx.rng().Below(20);
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
        SetUpAreaMap(ctx, worlds, size);
        const std::uint32_t oldArea = 1 + ctx.rng().Below(kUsedAreas);
        const std::uint32_t newArea = 1 + (oldArea + ctx.rng().Below(kUsedAreas - 1)) % kUsedAreas;
        Original_GameWorld_MoveAreaContents(worlds.original(), nullptr, oldArea, newArea);
        candidate(worlds.ours(), nullptr, oldArea, newArea);
        if ((i % 16 == 15 || i == cases - 1) && !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 15 before it): %u -> %u", i, oldArea, newArea);
            return;
        }
    }
}

// Wrong: keeps into's settings even when `from` is bigger.
void __fastcall WrongMergeAreaContents(GameWorld* world, void* edx, std::uint32_t into, std::uint32_t from) {
    const std::uint32_t saved = At<std::uint32_t>(world, AreaRecord(into) + kAreaTileCount);
    At<std::uint32_t>(world, AreaRecord(into) + kAreaTileCount) = 0xffffffffu;
    GameWorld_MergeAreaContents(world, edx, into, from);
    At<std::uint32_t>(world, AreaRecord(into) + kAreaTileCount) = saved;
}

void CompareMergeAreaContents(test::Context& ctx, MergeAreaContentsFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = 4 + ctx.rng().Below(20);
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
        SetUpAreaMap(ctx, worlds, size);
        const std::uint32_t into = 1 + ctx.rng().Below(kUsedAreas);
        const std::uint32_t from = 1 + (into + ctx.rng().Below(kUsedAreas - 1)) % kUsedAreas;
        for (const std::uint32_t area : {into, from}) {
            worlds.Set<std::uint32_t>(AreaRecord(area) + kAreaTileCount, ctx.rng().Below(4));
        }
        Original_GameWorld_MergeAreaContents(worlds.original(), nullptr, into, from);
        candidate(worlds.ours(), nullptr, into, from);
        if ((i % 16 == 15 || i == cases - 1) && !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 15 before it): %u <- %u", i, into, from);
            return;
        }
    }
}

// Never joins with a neighbour of another owner (it sets the neighbour's owner for the call).
void __fastcall WrongJoinAreasAtTile(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t nx = x + 1;
    const std::uint32_t tile = kTiles + (nx * 0x300 + y) * 24 + kTileOwner;
    const std::uint8_t saved = At<std::uint8_t>(world, tile);
    if (TileAreaWord(world, nx, y) != 0) {
        At<std::uint8_t>(world, tile) = At<std::uint8_t>(world, kTiles + (x * 0x300 + y) * 24 + kTileOwner);
    }
    GameWorld_JoinAreasAtTile(world, edx, x, y);
    At<std::uint8_t>(world, tile) = saved;
}

// A map for joining areas: connected stripe areas, owners of 1..3 players
// (returned) on the map's tiles; tiles without an area (the frame) have
// none (0xfe), so no area 0 is ever flooded; most tiles not blocked.
std::uint32_t SetUpJoinMap(test::Context& ctx, test::WorldPair& worlds, std::uint32_t size) {
    SetUpAreaMap(ctx, worlds, size, ctx.rng().Chance(1, 40), /*stripes=*/true);
    const std::uint32_t owners = 1 + ctx.rng().Below(3);
    for (std::uint32_t tx = 0xffffffffu; tx != size + 1; ++tx) {
        for (std::uint32_t ty = 0xffffffffu; ty != size + 1; ++ty) {
            const bool none = At<std::uint16_t>(worlds.original(), TileArea(tx, ty)) == 0;
            worlds.Set<std::uint8_t>(Tile(tx, ty) + kTileOwner,
                                     static_cast<std::uint8_t>(none ? 0xfe : ctx.rng().Below(owners)));
            const std::uint8_t flags = At<std::uint8_t>(worlds.original(), Tile(tx, ty) + kTileFlags);
            worlds.Set<std::uint8_t>(Tile(tx, ty) + kTileFlags,
                                     static_cast<std::uint8_t>(ctx.rng().Chance(1, 4) ? flags : flags & ~kTileBlocked));
        }
    }
    return owners;
}

std::uint32_t RelativeIfPointer(std::uint32_t value, GameWorld* world) {
    const auto base = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));
    return value - base < kTestWorld ? value - base : value;
}

// Wrong: never merges byte-10 regions.
std::uint32_t __fastcall WrongUnblockTile(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y) {
    const std::uint8_t saved = At<std::uint8_t>(world, Tile(x, y) + kTileByte10);
    At<std::uint8_t>(world, Tile(x, y) + kTileByte10) = 1;
    const std::uint32_t result = GameWorld_UnblockTile(world, edx, x, y);
    if (saved == 0 && At<std::uint8_t>(world, Tile(x, y) + kTileByte10) == 1) {
        At<std::uint8_t>(world, Tile(x, y) + kTileByte10) = 0;
    }
    return result;
}

void CompareUnblockTile(test::Context& ctx, BlockTileFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    SetUpOffsets(worlds);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = 8 + ctx.rng().Below(12);
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
        const std::uint32_t owners = SetUpJoinMap(ctx, worlds, size);
        SetUpWord16Regions(ctx, worlds, size);
        // Three tiles inside (region merges look one tile beyond their tiles).
        const std::uint32_t x = 3 + ctx.rng().Below(size - 6);
        const std::uint32_t y = 3 + ctx.rng().Below(size - 6);
        const std::uint8_t flags = static_cast<std::uint8_t>(ctx.rng().Next32());
        worlds.Set<std::uint8_t>(Tile(x, y) + kTileFlags,
                                 static_cast<std::uint8_t>(ctx.rng().Chance(5, 6) ? flags | kTileBlocked : flags & ~kTileBlocked));
        worlds.Set<std::uint8_t>(Tile(x, y) + kTileByte10, static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0 : 1));
        worlds.Set<std::uint16_t>(TileArea(x, y), static_cast<std::uint16_t>(ctx.rng().Chance(3, 4) ? 0 : 1 + ctx.rng().Below(3)));
        worlds.Set<std::uint8_t>(Tile(x, y) + kTileOwner,
                                 static_cast<std::uint8_t>(ctx.rng().Chance(1, 8) ? 0xff : ctx.rng().Below(owners)));
        const std::uint32_t expected =
            RelativeIfPointer(Original_GameWorld_UnblockTile(worlds.original(), nullptr, x, y), worlds.original());
        const std::uint32_t actual = RelativeIfPointer(candidate(worlds.ours(), nullptr, x, y), worlds.ours());
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 8 == 7 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 7 before it): (%u, %u) map %u", i, x, y, size);
            return;
        }
    }
}

// Wrong: also takes tiles that have an owner.
std::uint32_t __fastcall WrongSetOwnerOfFreeTile(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                 std::uint32_t owner) {
    if (At<std::uint8_t>(world, Tile(x, y) + kTileOwner) != 0xff) {
        At<std::uint8_t>(world, Tile(x, y) + kTileOwner) = 0xff;
    }
    return GameWorld_SetOwnerOfFreeTile(world, edx, x, y, owner);
}

void CompareSetOwnerOfFreeTile(test::Context& ctx, SetOwnerFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    SetUpOffsets(worlds);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = 4 + ctx.rng().Below(16);
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
        const std::uint32_t owners = SetUpJoinMap(ctx, worlds, size);
        const std::uint32_t x = 1 + ctx.rng().Below(size - 2);
        const std::uint32_t y = 1 + ctx.rng().Below(size - 2);
        const std::uint32_t t = Tile(x, y);
        worlds.Set<std::uint8_t>(t + kTileOwner, static_cast<std::uint8_t>(ctx.rng().Chance(4, 5) ? 0xff : ctx.rng().Below(owners)));
        worlds.Set<std::uint16_t>(TileArea(x, y), static_cast<std::uint16_t>(ctx.rng().Chance(4, 5) ? 0 : 1 + ctx.rng().Below(3)));
        // A pile on the tile or not (AddTilePileToArea): pile words in the
        // used range, small goods types and amounts.
        const std::uint32_t pile = ctx.rng().Chance(1, 2) ? 0 : 0x2000 + ctx.rng().Below(0x1000);
        worlds.Set<std::uint16_t>(t + 0x2, static_cast<std::uint16_t>(pile));
        if (pile != 0) {
            worlds.Set<std::uint8_t>(kPiles + pile * kPileBytes + 0x0, static_cast<std::uint8_t>(ctx.rng().Below(36)));
            worlds.Set<std::uint8_t>(kPiles + pile * kPileBytes + 0x1, static_cast<std::uint8_t>(ctx.rng().Below(9)));
        }
        const std::uint32_t owner = ctx.rng().Chance(1, 8) ? 0xff : ctx.rng().Chance(1, 20) ? 0x100 + ctx.rng().Below(owners)
                                                                                           : ctx.rng().Below(owners);
        const std::uint32_t expected =
            RelativeIfPointer(Original_GameWorld_SetOwnerOfFreeTile(worlds.original(), nullptr, x, y, owner), worlds.original());
        const std::uint32_t actual = RelativeIfPointer(candidate(worlds.ours(), nullptr, x, y, owner), worlds.ours());
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 8 == 7 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 7 before it): (%u, %u) owner %u map %u", i, x, y, owner,
                     size);
            return;
        }
    }
}

void CompareJoinAreas(test::Context& ctx, TileFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    SetUpOffsets(worlds);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = 4 + ctx.rng().Below(16);
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
        const std::uint32_t owners = SetUpJoinMap(ctx, worlds, size);
        const std::uint32_t x = 1 + ctx.rng().Below(size - 2);
        const std::uint32_t y = 1 + ctx.rng().Below(size - 2);
        worlds.Set<std::uint8_t>(Tile(x, y) + kTileOwner, static_cast<std::uint8_t>(ctx.rng().Below(owners)));
        worlds.Set<std::uint16_t>(TileArea(x, y), 0);
        Original_GameWorld_JoinAreasAtTile(worlds.original(), nullptr, x, y);
        candidate(worlds.ours(), nullptr, x, y);
        if ((i % 8 == 7 || i == cases - 1) && !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 7 before it): (%u, %u) map %u", i, x, y, size);
            return;
        }
    }
}

// Wrong: never splits (only frees an area left without neighbours).
void __fastcall WrongSplitAreaAtTile(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t area = TileAreaWord(world, x, y);
    std::uint32_t count = 0;
    for (std::uint32_t k = 0; k < kDirections; ++k) {
        if (TileAreaWord(world, x + kSteps[k + 1][0], y + kSteps[k + 1][1]) == area) {
            ++count;
        }
    }
    if (count == 0 || count > 4 || count < 2) {
        GameWorld_SplitAreaAtTile(world, edx, x, y);
    } else {
        GameWorld_RemoveTileFromArea(world, nullptr, x, y);
    }
}

template <typename Fn, typename Call>
void CompareOnAreaMap(test::Context& ctx, int cases, bool regions, Call call) {
    test::WorldPair worlds(ctx, kTestWorld);
    SetUpOffsets(worlds);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = 4 + ctx.rng().Below(16);
        worlds.Set<std::uint32_t>(world_offset::kMapSize, size);
        SetUpAreaMap(ctx, worlds, size, ctx.rng().Chance(1, 40));
        if (regions) {
            SetUpWord16Regions(ctx, worlds, size);
        }
        const std::uint32_t x = 1 + ctx.rng().Below(size - 2);
        const std::uint32_t y = 1 + ctx.rng().Below(size - 2);
        // The tile in one of the used areas (its callers only call it then).
        if (!regions || ctx.rng().Chance(3, 4)) {
            worlds.Set<std::uint16_t>(TileArea(x, y), static_cast<std::uint16_t>(1 + ctx.rng().Below(kUsedAreas)));
        }
        std::uint32_t expected = 0;
        std::uint32_t actual = 0;
        call(worlds, x, y, expected, actual);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 8 == 7 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 7 before it): (%u, %u) map %u", i, x, y, size);
            return;
        }
    }
}

void CompareSplitArea(test::Context& ctx, TileFn candidate, int cases) {
    CompareOnAreaMap<TileFn>(ctx, cases, false,
                             [&](test::WorldPair& worlds, std::uint32_t x, std::uint32_t y, std::uint32_t&, std::uint32_t&) {
                                 Original_GameWorld_SplitAreaAtTile(worlds.original(), nullptr, x, y);
                                 candidate(worlds.ours(), nullptr, x, y);
                             });
}

// Wrong: does not split byte-10 regions.
std::uint32_t __fastcall WrongBlockTile(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y) {
    const std::uint8_t saved = At<std::uint8_t>(world, Tile(x, y) + kTileByte10);
    At<std::uint8_t>(world, Tile(x, y) + kTileByte10) = 0;
    const std::uint32_t result = GameWorld_BlockTile(world, edx, x, y);
    At<std::uint8_t>(world, Tile(x, y) + kTileByte10) = saved;
    return result;
}

void CompareBlockTile(test::Context& ctx, BlockTileFn candidate, int cases) {
    CompareOnAreaMap<BlockTileFn>(
        ctx, cases, true,
        [&](test::WorldPair& worlds, std::uint32_t x, std::uint32_t y, std::uint32_t& expected, std::uint32_t& actual) {
            // Mostly not yet blocked; sometimes a marked area word (>= 0x8000).
            const std::uint8_t flags = At<std::uint8_t>(worlds.original(), Tile(x, y) + kTileFlags);
            worlds.Set<std::uint8_t>(Tile(x, y) + kTileFlags,
                                     static_cast<std::uint8_t>(ctx.rng().Chance(1, 6) ? flags | kTileBlocked
                                                                                      : flags & ~kTileBlocked));
            if (ctx.rng().Chance(1, 10)) {
                worlds.Set<std::uint16_t>(TileArea(x, y), static_cast<std::uint16_t>(0x8000 + ctx.rng().Below(4)));
            }
            expected = Original_GameWorld_BlockTile(worlds.original(), nullptr, x, y);
            actual = candidate(worlds.ours(), nullptr, x, y);
        });
}

}  // namespace

S3RE_TEST(GameWorld_InitArea_MatchesOriginal) {
    CompareInitArea(ctx, &GameWorld_InitArea, 1000);
}
S3RE_TEST(GameWorld_InitArea_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareInitArea(probe, &WrongInitArea, 300);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_AllocArea_MatchesOriginal) {
    CompareAllocArea(ctx, &GameWorld_AllocArea, 400);
}
S3RE_TEST(GameWorld_AllocArea_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareAllocArea(probe, &WrongAllocArea, 200);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_MoveAreaContents_MatchesOriginal) {
    CompareMoveAreaContents(ctx, &GameWorld_MoveAreaContents, 600);
}
S3RE_TEST(GameWorld_MoveAreaContents_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMoveAreaContents(probe, &WrongMoveAreaContents, 100);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_MergeAreaContents_MatchesOriginal) {
    CompareMergeAreaContents(ctx, &GameWorld_MergeAreaContents, 600);
}
S3RE_TEST(GameWorld_MergeAreaContents_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMergeAreaContents(probe, &WrongMergeAreaContents, 100);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_JoinAreasAtTile_MatchesOriginal) {
    CompareJoinAreas(ctx, &GameWorld_JoinAreasAtTile, 2000);
}
S3RE_TEST(GameWorld_JoinAreasAtTile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareJoinAreas(probe, &WrongJoinAreasAtTile, 400);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_UnblockTile_MatchesOriginal) {
    CompareUnblockTile(ctx, &GameWorld_UnblockTile, 1500);
}
S3RE_TEST(GameWorld_UnblockTile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareUnblockTile(probe, &WrongUnblockTile, 300);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_SetOwnerOfFreeTile_MatchesOriginal) {
    CompareSetOwnerOfFreeTile(ctx, &GameWorld_SetOwnerOfFreeTile, 2000);
}
S3RE_TEST(GameWorld_SetOwnerOfFreeTile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSetOwnerOfFreeTile(probe, &WrongSetOwnerOfFreeTile, 300);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_SplitAreaAtTile_MatchesOriginal) {
    CompareSplitArea(ctx, &GameWorld_SplitAreaAtTile, 2000);
}
S3RE_TEST(GameWorld_SplitAreaAtTile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSplitArea(probe, &WrongSplitAreaAtTile, 400);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_BlockTile_MatchesOriginal) {
    CompareBlockTile(ctx, &GameWorld_BlockTile, 1500);
}
S3RE_TEST(GameWorld_BlockTile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareBlockTile(probe, &WrongBlockTile, 300);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
