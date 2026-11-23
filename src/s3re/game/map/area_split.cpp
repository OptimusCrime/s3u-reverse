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
// CGameData::InitArea (0x004e6510): (area, player): QueuePendingEvent(4,
//   area, 0) and QueueEvent(6, area, 0, 0), then fills the record: player
//   byte, count 0, list heads and the per-goods arrays 0, then fixed
//   defaults, and per race (int32 at player record +0x4: world+0x11943e0 +
//   player*0x148; read again for each of the four race checks, 0..3) more
//   defaults. The stores are listed in the tables below in the original's
//   order. Only caller: AllocArea, which overwrites EAX. ret 8.
// CGameData::AllocArea (0x004e64c0): (player) -> the first area 1..0xffe
//   whose player byte is 0xff (0 if none; 0xfff is never used), raises the
//   high-water mark (uint32 world+0xe115dc, unsigned compare) and calls
//   InitArea(area, player). ret 4.
// CGameData::MoveAreaContents (0x004e54f0): (old, new): queues events (4,
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
// CGameData::MergeAreaContents (0x004e5a20): (into, from): queues events
//   (4, into), (4, from) and (6, into), (6, from), then puts every pile of
//   from's list +0x8 and of its 36 goods lists at the front of the same list
//   of `into` (one by one, so in reverse order; from's heads are left as
//   they were), clears into's words +0x52/+0x9a/+0xe2 per goods type, does
//   the same for the 48 settler lists, adds from's bytes +0x242..+0x244 to
//   into's, and if into has fewer tiles than from (uint32 +0x4, unsigned)
//   copies from's settings (the five byte arrays, +0x23e..+0x240, the 51
//   bytes at +0x245, +0x241) to into. Finally clears into's 51 words at
//   +0x278. EAX (0) is ignored by the only caller, JoinAreasAtTile. ret 8.
// CGameData::JoinAreasAtTile (0x004e6150): (x, y): the tile joins the areas
//   of its six neighbours (offset table entries 1..6, no map check) that are
//   not blocked (bit 0 of +0xa) and have the tile's owner (byte +0x8): the
//   first such neighbour's area `a` gets the tile (AssignTileToArea, even if
//   that area word is 0 or marked); every later one in another area `b`:
//   MergeAreaContents(a, b), FloodMoveArea from that neighbour to `a`, `b`
//   freed (player byte 0xff) and events (4, b), (6, b) queued. With no such
//   neighbour the tile gets a new area (AllocArea(owner)). Probably the
//   counterpart of SplitAreaAtTile when a tile becomes free again. Callers
//   ignore EAX. ret 8.
// CGameData::SplitAreaAtTile (0x004e5d50): (x, y): the tile's area `a` is
//   read, the tile leaves it (CGameData::RemoveTileFromArea). Counting the
//   six neighbours (offset table entries 1..6 at world+0x110a598, no map
//   check) in `a`: none -> the area is freed (player byte 0xff) and events
//   (4, a) and (6, a) are queued; 2..4 -> the neighbours on the map
//   (NeighbourX/Y, CGameData::IsOnMap) get flag 0x10 of tile byte +0xa set
//   if they are in `a` (else cleared). From the last such neighbour a walk
//   follows the border of `a` (always turning, six directions; wrapping
//   0..5), clearing flag 0x10 on every tile it enters, until either no
//   neighbour of (x, y) has flag 0x10 any more (still connected: done) or it
//   comes back to its first step (or found nothing in six turns before its
//   first step). Then each neighbour still in `a` with flag 0x10 is in a
//   separate part: a new area (AllocArea with the tile's owner byte +0x8)
//   gets that part (CGameData::FloodMoveArea), `a`'s player byte and
//   MoveAreaContents(a, new). With 1, 5 or 6 neighbours nothing more
//   happens. Flag 0x10 stays set on unvisited tiles, as in the original.
//   EAX unused (callers tail-return it, their callers ignore it). ret 8.
// CGameData::BlockTile (0x0051b6f0): (x, y): if bit 0 of tile byte +0xa
//   (blocked) is clear: sets it; CGameData::SplitTileByte10Region if byte
//   +0x10 is not 0; SplitAreaAtTile if the area word is 1..0x7fff; then
//   CGameData::SplitWord16RegionsAround. EAX = its EAX (0), or, if the tile
//   was already blocked, x * 0x300 + the flag byte (the original's AL load).
//   ret 8.
// CGameData::UnblockTile (0x0051b770): (x, y): the counterpart of
//   BlockTile: if bit 0 of tile byte +0xa is set: clears it;
//   CGameData::MergeTileByte10Regions if byte +0x10 is 0; JoinAreasAtTile
//   if the area word is 0 and the owner byte +0x8 is not 0xff; then
//   CGameData::AssignWord16RegionsAround. EAX = its EAX, or, if the tile was
//   not blocked, x * 0x300 + the flag byte. ret 8.
// CGameData::SetOwnerOfFreeTile (0x0051b5d0): (x, y, owner): only for a
//   tile without owner (byte +0x8 == 0xff): owner byte = owner (low byte);
//   then if the tile has no area, is not blocked and owner (32-bit) is not
//   0xff: JoinAreasAtTile and CGameData::AddTilePileToArea. EAX = its EAX,
//   else x * 0x300 + y (the tile had an owner) or (x * 0x300 + y) * 3 +
//   0xce8b (the original's address arithmetic). ret 0xc.
// CGameData::ClearAreaGoodsSettings1D2 (0x004ff3c0): (area): the 36 bytes
//   at area record +0x1d2 = 0. EAX = 0. ret 4.
//
// Building extra tiles (building records: 0x3a bytes at world+0x1499e64,
// count at world+0x1499e60; +0x0 player, uint16 +0x2/+0x4 position, +0x6
// type, +0x1f state (high nibble), +0x20 0xfe once the extra tiles are
// taken, uint16 +0x22 direction, uint16 +0x30/+0x32 a shipyard's ship
// position, +0x36 a byte):
// CGameData::TakeBuildingExtraTiles (0x004dd6f0): (building): nothing if
//   +0x20 is 0xfe. The war machine workshops (catapult 31, ballista 47,
//   cannon 48) and the large gong (50) (0x1f, 0x2f, 0x30, 0x32; the seven
//   tiles are where the seven-tile machine they make stands): state 0x20
//   only marks the building done if +0x36 <= 24 (0x18);
//   otherwise the centre is the position plus the type's offset (int32 dx,
//   dy at S3.EXE 0x659624 + 8 * k: (-2, 4), (-2, 6), (-2, 4), (-1, 7)),
//   DebugLog("centerx,centery = %u,%u") for the gong, and if the seven
//   tiles (cx-1, cy-1), (cx, cy-1), (cx-1, cy), (cx, cy), (cx+1, cy),
//   (cx, cy+1), (cx+1, cy+1) are all CGameData::IsFreeOfCatapultsNearby
//   they are blocked (BlockTile, same order) and the building is done; EAX
//   0 if one is not free, else the last BlockTile's EAX. Shipyard (0x20)
//   with a ship position (+0x30 != 0xffff): the ship tile is two steps
//   (NeighbourX/Y) from it in direction d = (+0x22 + 3) % 6; if it has no
//   occupant, state 0x30 / 0x40 creates a ship (CGameData::CreateSettler of
//   a trade ship (43, 0x2b) / a ferry (44, 0x2c) for the building's player;
//   any other state uses the number +0x32 instead, a stale register in the
//   original), then DebugLog("Schiffsproduktion %u owner %u", type,
//   player), ship job +0xe = Job325 (0x145), +0x15 = 0, +0x1c = 0,
//   direction +0x8 = (d + 2) % 6, building
//   done; EAX (d + 2) / 6, 0 if no ship, the ship tile's y if occupied.
//   Other types: EAX = building. ret 4.
// CGameData::ReleaseBuildingExtraTiles (0x004dda10): (building): the
//   counterpart: the four types unblock the seven tiles (UnblockTile, same
//   order; EAX its last EAX; the original's check of the state nibble
//   against 0x14 never matches). A shipyard with state byte != 0 frees the
//   occupant of its ship tile (settler byte +0x0 = 0xff, tile word 0; EAX =
//   the tile's address); it does not check for an unset ship position.
//   Otherwise EAX = world + building * 0x3a. ret 4.
// CGameData::UnblockCastleTiles (0x004dfa60): (): for buildings
//   1..count (count re-read each time) of type 18 (0x12, the castle):
//   owner's race (int32 at player record +0x4) Roman: UnblockTile(x-2, y+3)
//   if that tile is blocked; Egyptian: the same for (x, y+7), then (x, y+6).
//   EAX = the count. ret 0.
// All thiscall (ECX = world).
//
// Field names (layout.h): area +0x8 Area::pile_list, +0xa piles_by_goods,
// +0x52 requests_by_goods, +0x9a/+0xe2 buildings_a/b, +0x12a
// settlers_by_type, +0x18a..+0x21a settings1..5, +0x23e settings_extra,
// +0x241 setting_241, +0x242 counters_242, +0x245 condition_enabled, +0x278
// counters_278; tile byte +0x10 Tile::walk_region ("byte 10"), word +0x16
// Tile::wide_region ("word 16"); building +0x20 Building::workshop.extra_tiles,
// +0x22 dock_direction, +0x30/+0x32 work_x/work_y, +0x36 unk36.

#include "game/debug_log.h"
#include "game/enums/races.h"
#include "game/enums/settler_types.h"
#include "game/enums/buildings.h"
#include "game/enums/job_ids.h"
#include "game/enums/directions.h"
#include "game/replacements.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/typed_fields.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <vector>

namespace s3re::game {

namespace {

using layout::Area;
using layout::Pile;
using layout::Settler;
using layout::Tile;
using layout::kTileBlocked;

// Tile flag 0x10 (layout::kTileFloodMark): SplitAreaAtTile marks the
// neighbours that the border walk still has to reach with it.
constexpr std::uint8_t kTileSplitMark = layout::kTileFloodMark;
// The original computes the address of a tile's area word as
// (x * 0x300 + y + 0x44d9) * 24, SetOwnerOfFreeTile as
// ((x * 0x300 + y) * 3 + 0xce8b) * 8 (its EAX keeps the part before * 8):
// both are TileOffset(x, y) + 0x14 (Tile::area), in 32 bits.
constexpr std::uint32_t kTileAreaWordBytes =
    static_cast<std::uint32_t>(offsetof(CGameData, tiles) + offsetof(Tile, area));
static_assert(kTileAreaWordBytes % 8 == 0 && kTileAreaWordBytes / 8 == 0xce8b);

constexpr std::uint8_t kFreeArea = 0xff;
constexpr std::uint8_t kNoOwner = 0xff;  // Tile::owner
constexpr std::uint32_t kLastArea = 0xfff;  // never handed out
constexpr std::uint32_t kGoodsTypes = layout::kGoodsTypes;
constexpr std::uint32_t kAreaSettlerListCount = layout::kSettlerTypes;
constexpr std::uint32_t kAreaCount51 = 51;
static_assert(sizeof(Area::condition_enabled) == kAreaCount51);
static_assert(sizeof(Area::counters_278) == kAreaCount51 * 2);

constexpr std::uint32_t kDirections = kDirectionCount;

constexpr std::uint32_t kPendingAreaEvent = 4;
constexpr std::uint32_t kAreaEvent = 6;

// The byte arrays of an area record: Area::settings1..5 (36 bytes each, at
// +0x18a, +0x1ae, +0x1d2, +0x1f6, +0x21a) and Area::condition_enabled (51
// bytes at +0x245).
enum AreaByteArray : std::uint8_t { kSettings1, kSettings2, kSettings3, kSettings4, kSettings5, kConditions };

std::uint8_t* AreaBytes(Area& area, AreaByteArray array) {
    switch (array) {
        case kSettings1: return area.settings1;
        case kSettings2: return area.settings2;
        case kSettings3: return area.settings3;
        case kSettings4: return area.settings4;
        case kSettings5: return area.settings5;
        default: return area.condition_enabled;
    }
}

// Byte arrays per goods type, in the order InitArea clears them.
constexpr AreaByteArray kGoodsArraysInitOrder[] = {kSettings1, kSettings4, kSettings2, kSettings5, kSettings3};
// ... and in the order MoveAreaContents copies them.
constexpr AreaByteArray kGoodsArraysCopyOrder[] = {kSettings1, kSettings2, kSettings3, kSettings4, kSettings5};

// InitArea's fixed defaults (Area::settings_extra, +0x23e..+0x240).
constexpr std::uint8_t kSettingsExtraDefaults[3] = {20, 25, 25};

// One byte store of InitArea: AreaBytes(array)[index] = value (values in
// decimal; their meaning is not known yet).
struct ByteStore {
    AreaByteArray array;
    std::uint8_t index;
    std::uint8_t value;
};

// InitArea's defaults for every race (after the clearing loops).
constexpr ByteStore kAreaDefaults[] = {
    {kConditions, 1, 16}, {kConditions, 2, 16}, {kConditions, 3, 12}, {kConditions, 4, 6},
    {kConditions, 7, 16}, {kConditions, 8, 16}, {kConditions, 9, 8}, {kConditions, 13, 16},
    {kConditions, 14, 8}, {kConditions, 16, 16}, {kConditions, 18, 16}, {kConditions, 22, 16},
    {kConditions, 23, 16}, {kConditions, 24, 8}, {kConditions, 26, 16}, {kConditions, 27, 16},
    {kConditions, 28, 8}, {kConditions, 30, 16}, {kConditions, 31, 0}, {kConditions, 32, 0},
    {kConditions, 35, 16}, {kConditions, 36, 0}, {kConditions, 37, 0}, {kConditions, 39, 16},
    {kConditions, 40, 0}, {kConditions, 41, 0}, {kConditions, 46, 0}, {kConditions, 47, 0},
    {kConditions, 48, 0}, {kConditions, 49, 0}, {kConditions, 50, 0}, {kSettings2, 18, 16},
    {kSettings2, 19, 12}, {kSettings2, 20, 8}, {kSettings5, 13, 1}, {kSettings5, 12, 1},
    {kSettings5, 14, 1}, {kSettings5, 15, 1}, {kSettings5, 16, 1}, {kSettings5, 32, 1},
    {kSettings5, 17, 1}, {kSettings5, 18, 1}, {kSettings5, 19, 1}, {kSettings5, 20, 1},
    {kSettings5, 10, 1},
};

constexpr ByteStore kRace0Defaults[] = {
    {kConditions, 5, 0}, {kConditions, 6, 0}, {kConditions, 10, 8}, {kConditions, 11, 0},
    {kConditions, 12, 0}, {kConditions, 43, 8}, {kConditions, 44, 0}, {kConditions, 45, 0},
    {kConditions, 15, 4}, {kConditions, 17, 8}, {kConditions, 19, 0}, {kConditions, 20, 0},
    {kConditions, 21, 0}, {kConditions, 25, 0}, {kConditions, 29, 0}, {kConditions, 33, 0},
    {kConditions, 34, 0}, {kConditions, 38, 0}, {kConditions, 42, 0}, {kSettings4, 1, 1},
    {kSettings4, 2, 2}, {kSettings4, 3, 3}, {kSettings4, 4, 7}, {kSettings4, 5, 6}, {kSettings4, 6, 24},
    {kSettings4, 7, 5}, {kSettings4, 8, 8}, {kSettings4, 9, 11}, {kSettings4, 10, 25}, {kSettings4, 11, 4},
    {kSettings4, 12, 17}, {kSettings4, 13, 18}, {kSettings4, 14, 19}, {kSettings4, 15, 20},
    {kSettings4, 16, 21}, {kSettings4, 17, 22}, {kSettings4, 18, 14}, {kSettings4, 19, 15},
    {kSettings4, 20, 16}, {kSettings4, 22, 10}, {kSettings4, 23, 12}, {kSettings4, 24, 9},
    {kSettings4, 26, 13}, {kSettings4, 32, 23}, {kSettings4, 21, 26},
};

constexpr ByteStore kRace1Defaults[] = {
    {kConditions, 5, 0}, {kConditions, 6, 0}, {kConditions, 10, 0}, {kConditions, 11, 8},
    {kConditions, 12, 0}, {kConditions, 43, 0}, {kConditions, 44, 8}, {kConditions, 45, 0},
    {kConditions, 15, 0}, {kConditions, 17, 0}, {kConditions, 19, 4}, {kConditions, 20, 0},
    {kConditions, 21, 0}, {kConditions, 25, 8}, {kConditions, 29, 8}, {kConditions, 33, 0},
    {kConditions, 34, 0}, {kConditions, 38, 0}, {kConditions, 42, 16}, {kSettings4, 1, 1},
    {kSettings4, 2, 2}, {kSettings4, 3, 3}, {kSettings4, 4, 7}, {kSettings4, 5, 6}, {kSettings4, 6, 24},
    {kSettings4, 7, 5}, {kSettings4, 8, 8}, {kSettings4, 9, 11}, {kSettings4, 10, 25}, {kSettings4, 11, 4},
    {kSettings4, 12, 17}, {kSettings4, 13, 18}, {kSettings4, 14, 19}, {kSettings4, 15, 20},
    {kSettings4, 16, 21}, {kSettings4, 17, 22}, {kSettings4, 18, 14}, {kSettings4, 19, 15},
    {kSettings4, 20, 16}, {kSettings4, 22, 10}, {kSettings4, 23, 12}, {kSettings4, 24, 9},
    {kSettings4, 26, 13}, {kSettings4, 32, 23}, {kSettings4, 29, 26}, {kSettings4, 28, 27},
    {kSettings5, 28, 1},
};

constexpr ByteStore kRace2Defaults[] = {
    {kConditions, 5, 8}, {kConditions, 6, 4}, {kConditions, 10, 0}, {kConditions, 11, 0},
    {kConditions, 12, 8}, {kConditions, 43, 0}, {kConditions, 44, 0}, {kConditions, 45, 8},
    {kConditions, 15, 0}, {kConditions, 17, 0}, {kConditions, 19, 0}, {kConditions, 20, 16},
    {kConditions, 21, 16}, {kConditions, 25, 0}, {kConditions, 29, 0}, {kConditions, 33, 0},
    {kConditions, 34, 0}, {kConditions, 38, 0}, {kConditions, 42, 0}, {kSettings4, 1, 1},
    {kSettings4, 2, 2}, {kSettings4, 3, 3}, {kSettings4, 4, 7}, {kSettings4, 5, 6}, {kSettings4, 6, 24},
    {kSettings4, 7, 5}, {kSettings4, 8, 8}, {kSettings4, 9, 11}, {kSettings4, 10, 25}, {kSettings4, 11, 4},
    {kSettings4, 12, 17}, {kSettings4, 13, 18}, {kSettings4, 14, 19}, {kSettings4, 15, 20},
    {kSettings4, 16, 21}, {kSettings4, 17, 22}, {kSettings4, 18, 14}, {kSettings4, 19, 15},
    {kSettings4, 20, 16}, {kSettings4, 22, 10}, {kSettings4, 23, 12}, {kSettings4, 24, 9},
    {kSettings4, 26, 13}, {kSettings4, 32, 23}, {kSettings4, 33, 26}, {kSettings4, 30, 28},
    {kSettings4, 27, 27}, {kSettings4, 25, 29},
};

constexpr ByteStore kRace3Defaults[] = {
    {kConditions, 5, 0}, {kConditions, 6, 0}, {kConditions, 10, 0}, {kConditions, 11, 0},
    {kConditions, 12, 0}, {kConditions, 43, 0}, {kConditions, 44, 0}, {kConditions, 45, 0},
    {kConditions, 15, 0}, {kConditions, 17, 0}, {kConditions, 19, 0}, {kConditions, 20, 0},
    {kConditions, 21, 0}, {kConditions, 25, 0}, {kConditions, 29, 0}, {kConditions, 33, 0},
    {kConditions, 34, 0}, {kConditions, 38, 0}, {kConditions, 42, 8}, {kConditions, 46, 8},
    {kConditions, 47, 8}, {kConditions, 48, 8}, {kConditions, 49, 8}, {kConditions, 50, 8},
    {kSettings4, 1, 1}, {kSettings4, 2, 2}, {kSettings4, 3, 3}, {kSettings4, 4, 7}, {kSettings4, 5, 6},
    {kSettings4, 6, 24}, {kSettings4, 7, 5}, {kSettings4, 8, 8}, {kSettings4, 9, 11}, {kSettings4, 10, 25},
    {kSettings4, 11, 4}, {kSettings4, 12, 17}, {kSettings4, 13, 18}, {kSettings4, 14, 19},
    {kSettings4, 15, 20}, {kSettings4, 16, 21}, {kSettings4, 17, 22}, {kSettings4, 18, 14},
    {kSettings4, 19, 15}, {kSettings4, 20, 16}, {kSettings4, 22, 10}, {kSettings4, 23, 12},
    {kSettings4, 24, 9}, {kSettings4, 26, 13}, {kSettings4, 32, 23}, {kSettings4, 34, 26},
    {kSettings4, 35, 27}, {kSettings4, 28, 28}, {kSettings4, 25, 29},
};

// Tile (x, y), with the original's 32-bit address arithmetic (x and y may
// be off the map: neighbours are looked up without a map check).
Tile& TileAt(CGameData* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

std::uint32_t TileAreaWord(CGameData* world, std::uint32_t x, std::uint32_t y) {
    return TileAt(world, x, y).area;
}

std::uint8_t& TileFlags(CGameData* world, std::uint32_t x, std::uint32_t y) {
    return TileAt(world, x, y).flags;
}

template <std::size_t N>
void StoreAll(Area& area, const ByteStore (&stores)[N]) {
    for (const ByteStore& store : stores) {
        AreaBytes(area, store.array)[store.index] = store.value;
    }
}

// The fields that link records of a doubly linked list (piles or settlers)
// and give their tile.
template <typename Record>
struct ListLinks {
    std::uint16_t Record::*x;
    std::uint16_t Record::*y;
    std::uint16_t Record::*previous;
    std::uint16_t Record::*next;
};
constexpr ListLinks<Pile> kPileLinks{&Pile::x, &Pile::y, &Pile::prev, &Pile::next};
constexpr ListLinks<Settler> kSettlerLinks{&Settler::x, &Settler::y, &Settler::prev, &Settler::next};

// Moves the members of a doubly linked list (head `from`) whose tile is in
// area `area` to the front of the list with head `to`.
template <typename Record>
void MoveListMembers(CGameData* world, std::uint16_t& from, std::uint16_t& to, std::uint32_t area, Record* table,
                     const ListLinks<Record>& links) {
    std::uint32_t member = from;
    while (member != 0) {
        Record& r = table[member];
        const std::uint16_t next = r.*links.next;
        if (TileAreaWord(world, r.*links.x, r.*links.y) == area) {
            const std::uint16_t previous = r.*links.previous;
            if (previous != 0) {
                table[previous].*links.next = next;
            } else {
                from = next;
            }
            const std::uint16_t after = r.*links.next;
            if (after != 0) {
                table[after].*links.previous = r.*links.previous;
            }
            const std::uint16_t head = to;
            r.*links.next = head;
            if (head != 0) {
                table[head].*links.previous = static_cast<std::uint16_t>(member);
            }
            r.*links.previous = 0;
            to = static_cast<std::uint16_t>(member);
        }
        member = next;
    }
}

void MovePiles(CGameData* world, std::uint16_t& from, std::uint16_t& to, std::uint32_t area) {
    MoveListMembers(world, from, to, area, world->piles, kPileLinks);
}

void MoveSettlers(CGameData* world, std::uint16_t& from, std::uint16_t& to, std::uint32_t area) {
    MoveListMembers(world, from, to, area, world->settlers, kSettlerLinks);
}

// Puts every member of the list with head `source` at the front of the list
// with head `target` (one by one, so in reverse order; `source` keeps its head).
template <typename Record>
void PrependAll(std::uint16_t& source, std::uint16_t& target, Record* table, const ListLinks<Record>& links) {
    std::uint32_t member = source;
    while (member != 0) {
        Record& r = table[member];
        const std::uint16_t next = r.*links.next;
        const std::uint16_t head = target;
        r.*links.next = head;
        if (head != 0) {
            table[head].*links.previous = static_cast<std::uint16_t>(member);
        }
        r.*links.previous = 0;
        target = static_cast<std::uint16_t>(member);
        member = next;
    }
}

}  // namespace

using InitAreaFn = void(__fastcall*)(CGameData* world, void* edx, std::uint32_t area, std::uint32_t player);
using AllocAreaFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t player);
using MoveAreaContentsFn = void(__fastcall*)(CGameData* world, void* edx, std::uint32_t oldArea,
                                             std::uint32_t newArea);
using MergeAreaContentsFn = void(__fastcall*)(CGameData* world, void* edx, std::uint32_t into, std::uint32_t from);
using TileFn = void(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);
using SetOwnerFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y,
                                              std::uint32_t owner);
using BlockTileFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);

// Purpose: Sets up a new area for a player: empty lists, and the default economy settings (probably goods
// distribution and priorities), which depend on the player's race.
void __fastcall CGameData_InitArea(CGameData* world, void*, std::uint32_t area, std::uint32_t player) {
    CGameData_QueuePendingEvent(world, nullptr, kPendingAreaEvent, area, 0);
    CGameData_QueueEvent(world, nullptr, kAreaEvent, area, 0, 0);
    Area& r = world->areas[area];
    r.player = static_cast<std::uint8_t>(player);
    r.tile_count = 0;
    r.pile_list = 0;
    r.settings_extra[0] = kSettingsExtraDefaults[0];
    r.settings_extra[1] = kSettingsExtraDefaults[1];
    r.settings_extra[2] = kSettingsExtraDefaults[2];
    r.setting_241 = 1;
    r.counters_242[0] = 0;
    r.counters_242[1] = 0;
    r.counters_242[2] = 0;
    for (std::uint32_t i = 0; i < kGoodsTypes; ++i) {
        r.piles_by_goods[i] = 0;
        r.requests_by_goods[i] = 0;
        r.buildings_a[i] = 0;
        r.buildings_b[i] = 0;
        for (const AreaByteArray array : kGoodsArraysInitOrder) {
            AreaBytes(r, array)[i] = 0;
        }
    }
    for (std::uint32_t i = 0; i < kAreaSettlerListCount; ++i) {
        r.settlers_by_type[i] = 0;
    }
    for (std::uint32_t i = 0; i < kAreaCount51; ++i) {
        r.condition_enabled[i] = 0;
        r.counters_278[i] = 0;
    }
    StoreAll(r, kAreaDefaults);
    // The race (player record +0x4) is read again for each check.
    const layout::Player& owner = world->players[player];
    if (RaceOf(owner) == Race::Roman) {
        StoreAll(r, kRace0Defaults);
    }
    if (RaceOf(owner) == Race::Egyptian) {
        StoreAll(r, kRace1Defaults);
    }
    if (RaceOf(owner) == Race::Asian) {
        StoreAll(r, kRace2Defaults);
    }
    if (RaceOf(owner) == Race::Amazon) {
        StoreAll(r, kRace3Defaults);
    }
}

// Purpose: Finds an unused area number, sets it up for the player (InitArea) and returns it; 0 if all
// are in use.
std::uint32_t __fastcall CGameData_AllocArea(CGameData* world, void*, std::uint32_t player) {
    std::uint32_t area = 1;
    while (world->areas[area].player != kFreeArea) {
        if (area == kLastArea) {
            return 0;
        }
        ++area;
    }
    if (area == kLastArea) {
        return 0;
    }
    if (area > world->area_high_water) {
        world->area_high_water = area;
    }
    CGameData_InitArea(world, nullptr, area, player);
    return area;
}

// Purpose: After part of an area became a new area, moves the goods piles and settlers lying in that part
// to the new area's lists, and gives the new area the old one's economy settings.
void __fastcall CGameData_MoveAreaContents(CGameData* world, void*, std::uint32_t oldArea, std::uint32_t newArea) {
    CGameData_QueuePendingEvent(world, nullptr, kPendingAreaEvent, oldArea, 0);
    CGameData_QueuePendingEvent(world, nullptr, kPendingAreaEvent, newArea, 0);
    CGameData_QueueEvent(world, nullptr, kAreaEvent, oldArea, 0, 0);
    CGameData_QueueEvent(world, nullptr, kAreaEvent, newArea, 0, 0);
    Area& from = world->areas[oldArea];
    Area& to = world->areas[newArea];
    MovePiles(world, from.pile_list, to.pile_list, newArea);
    for (std::uint32_t i = 0; i < kGoodsTypes; ++i) {
        MovePiles(world, from.piles_by_goods[i], to.piles_by_goods[i], newArea);
        for (Area* list : {&from, &to}) {
            list->requests_by_goods[i] = 0;
            list->buildings_a[i] = 0;
            list->buildings_b[i] = 0;
        }
        for (const AreaByteArray array : kGoodsArraysCopyOrder) {
            AreaBytes(to, array)[i] = AreaBytes(from, array)[i];
        }
    }
    for (std::uint32_t i = 0; i < kAreaSettlerListCount; ++i) {
        MoveSettlers(world, from.settlers_by_type[i], to.settlers_by_type[i], newArea);
    }
    for (std::uint32_t k = 0; k < 3; ++k) {
        to.settings_extra[k] = from.settings_extra[k];
    }
    for (std::uint32_t i = 0; i < kAreaCount51; ++i) {
        to.condition_enabled[i] = from.condition_enabled[i];
        to.counters_278[i] = 0;
        from.counters_278[i] = 0;
    }
    to.setting_241 = from.setting_241;
}

// Purpose: When two areas grow together, moves all goods piles and settlers of one area into the other's
// lists; the bigger area's economy settings are kept.
void __fastcall CGameData_MergeAreaContents(CGameData* world, void*, std::uint32_t into, std::uint32_t from) {
    CGameData_QueuePendingEvent(world, nullptr, kPendingAreaEvent, into, 0);
    CGameData_QueuePendingEvent(world, nullptr, kPendingAreaEvent, from, 0);
    CGameData_QueueEvent(world, nullptr, kAreaEvent, into, 0, 0);
    CGameData_QueueEvent(world, nullptr, kAreaEvent, from, 0, 0);
    Area& a = world->areas[into];
    Area& b = world->areas[from];
    PrependAll(b.pile_list, a.pile_list, world->piles, kPileLinks);
    for (std::uint32_t i = 0; i < kGoodsTypes; ++i) {
        PrependAll(b.piles_by_goods[i], a.piles_by_goods[i], world->piles, kPileLinks);
        a.requests_by_goods[i] = 0;
        a.buildings_a[i] = 0;
        a.buildings_b[i] = 0;
    }
    for (std::uint32_t i = 0; i < kAreaSettlerListCount; ++i) {
        PrependAll(b.settlers_by_type[i], a.settlers_by_type[i], world->settlers, kSettlerLinks);
    }
    for (std::uint32_t k = 0; k < 3; ++k) {
        a.counters_242[k] = static_cast<std::uint8_t>(a.counters_242[k] + b.counters_242[k]);
    }
    if (a.tile_count < b.tile_count) {
        for (std::uint32_t i = 0; i < kGoodsTypes; ++i) {
            for (const AreaByteArray array : kGoodsArraysCopyOrder) {
                AreaBytes(a, array)[i] = AreaBytes(b, array)[i];
            }
        }
        for (std::uint32_t k = 0; k < 3; ++k) {
            a.settings_extra[k] = b.settings_extra[k];
        }
        for (std::uint32_t i = 0; i < kAreaCount51; ++i) {
            a.condition_enabled[i] = b.condition_enabled[i];
        }
        a.setting_241 = b.setting_241;
    }
    for (std::uint32_t i = 0; i < kAreaCount51; ++i) {
        a.counters_278[i] = 0;
    }
}

// Purpose: When a tile becomes part of the land again (probably when it is no longer blocked), puts it into
// the area of its neighbours of the same owner, merging their areas if they were different, or into a new
// area if it has no such neighbour.
void __fastcall CGameData_JoinAreasAtTile(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    Tile& tile = TileAt(world, x, y);
    const std::uint32_t owner = tile.owner;
    std::uint32_t count = 0;
    std::uint32_t base = 0;
    for (std::uint32_t k = 0; k < kDirections; ++k) {
        const layout::MapOffset& entry = world->offsets[k + 1];  // entries 1..6: the six neighbours
        const std::uint32_t nx = x + static_cast<std::uint32_t>(entry.dx);
        const std::uint32_t ny = y + static_cast<std::uint32_t>(entry.dy);
        const Tile& neighbour = TileAt(world, nx, ny);
        if ((neighbour.flags & kTileBlocked) != 0 || neighbour.owner != owner) {
            continue;
        }
        ++count;
        if (count == 1) {
            base = TileAreaWord(world, nx, ny);
            CGameData_AssignTileToArea(world, nullptr, x, y, base);
            continue;
        }
        const std::uint32_t other = TileAreaWord(world, nx, ny);
        if (other == base) {
            continue;
        }
        CGameData_MergeAreaContents(world, nullptr, base, other);
        CGameData_FloodMoveArea(world, nullptr, x + static_cast<std::uint32_t>(entry.dx),
                                y + static_cast<std::uint32_t>(entry.dy), base);
        world->areas[other].player = kFreeArea;
        CGameData_QueuePendingEvent(world, nullptr, kPendingAreaEvent, other, 0);
        CGameData_QueueEvent(world, nullptr, kAreaEvent, other, 0, 0);
    }
    if (count == 0) {
        const std::uint32_t area = CGameData_AllocArea(world, nullptr, tile.owner);
        CGameData_AssignTileToArea(world, nullptr, x, y, area);
    }
}

// Purpose: When a tile leaves its area (e.g. it gets blocked), checks whether the rest of the area is still
// connected around it; parts that got cut off become new areas with their own piles and settlers, and an
// area with no tiles left next to it is freed.
void __fastcall CGameData_SplitAreaAtTile(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t area = TileAreaWord(world, x, y);
    CGameData_RemoveTileFromArea(world, nullptr, x, y);
    std::uint32_t count = 0;
    for (std::uint32_t k = 0; k < kDirections; ++k) {
        const layout::MapOffset& entry = world->offsets[k + 1];  // entries 1..6: the six neighbours
        if (TileAreaWord(world, static_cast<std::uint32_t>(entry.dx) + x, static_cast<std::uint32_t>(entry.dy) + y) ==
            area) {
            ++count;
        }
    }
    if (count == 0) {
        world->areas[area].player = kFreeArea;
        CGameData_QueuePendingEvent(world, nullptr, kPendingAreaEvent, area, 0);
        CGameData_QueueEvent(world, nullptr, kAreaEvent, area, 0, 0);
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
        if (CGameData_IsOnMap(world, nullptr, nx, ny) == 0) {
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
            if (CGameData_IsOnMap(world, nullptr, tx, ty) != 0 && TileAreaWord(world, tx, ty) == area) {
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
        const std::uint32_t newArea = CGameData_AllocArea(world, nullptr, TileAt(world, nx, ny).owner);
        CGameData_FloodMoveArea(world, nullptr, nx, ny, newArea);
        world->areas[newArea].player = world->areas[area].player;
        CGameData_MoveAreaContents(world, nullptr, area, newArea);
    }
}

// Purpose: Makes a tile impassable (e.g. when something is built or placed on it) and updates the
// regions and areas that the tile connected.
std::uint32_t __fastcall CGameData_BlockTile(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    Tile& tile = TileAt(world, x, y);
    const std::uint8_t flags = tile.flags;
    if ((flags & kTileBlocked) != 0) {
        return x * layout::kMapRowTiles + flags;
    }
    tile.flags = static_cast<std::uint8_t>(flags | kTileBlocked);
    if (tile.walk_region != 0) {
        CGameData_SplitTileByte10Region(world, nullptr, x, y);
    }
    const std::uint16_t area = tile.area;
    if (area != 0 && area < layout::kTileAreaWater) {
        CGameData_SplitAreaAtTile(world, nullptr, x, y);
    }
    return CGameData_SplitWord16RegionsAround(world, nullptr, x, y);
}

// Purpose: Makes a blocked tile passable again (the opposite of BlockTile) and joins it to the regions and
// areas around it.
std::uint32_t __fastcall CGameData_UnblockTile(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    Tile& tile = TileAt(world, x, y);
    const std::uint8_t flags = tile.flags;
    if ((flags & kTileBlocked) == 0) {
        return x * layout::kMapRowTiles + flags;
    }
    tile.flags = static_cast<std::uint8_t>(flags & ~kTileBlocked);
    if (tile.walk_region == 0) {
        CGameData_MergeTileByte10Regions(world, nullptr, x, y);
    }
    if (tile.area == 0 && tile.owner != kNoOwner) {
        CGameData_JoinAreasAtTile(world, nullptr, x, y);
    }
    return CGameData_AssignWord16RegionsAround(world, nullptr, x, y);
}

// Purpose: Gives a tile that has no owner yet to a player (e.g. when territory grows), and puts it and the
// goods pile lying on it into that player's area.
std::uint32_t __fastcall CGameData_SetOwnerOfFreeTile(CGameData* world, void*, std::uint32_t x, std::uint32_t y,
                                                      std::uint32_t owner) {
    const std::uint32_t index = x * layout::kMapRowTiles + y;
    Tile& tile = TileAt(world, x, y);
    if (tile.owner != kNoOwner) {
        return index;
    }
    tile.owner = static_cast<std::uint8_t>(owner);
    const std::uint32_t areaIndex = index * 3 + kTileAreaWordBytes / 8;  // * 8 = the tile's area word
    // The original compares all 32 bits of the argument here.
    if (tile.area != 0 || (tile.flags & kTileBlocked) != 0 || owner == kNoOwner) {
        return areaIndex;
    }
    CGameData_JoinAreasAtTile(world, nullptr, x, y);
    return CGameData_AddTilePileToArea(world, nullptr, x, y);
}

// Purpose: Resets one of an area's per-goods settings arrays (+0x1d2, 36 bytes) to 0.
std::uint32_t __fastcall CGameData_ClearAreaGoodsSettings1D2(CGameData* world, void*, std::uint32_t area) {
    for (std::uint32_t i = 0; i < kGoodsTypes; ++i) {
        world->areas[area].settings3[i] = 0;
    }
    return 0;
}

namespace {

using layout::Building;

using B = BuildingType;

// Building::workshop.extra_tiles (+0x20) once the extra tiles are taken.
constexpr std::uint8_t kExtraDone = 0xfe;
constexpr std::uint8_t kShipyard = Value(B::Shipyard);
constexpr std::uint8_t kCastle = Value(B::Castle);

// Building::workshop.state: the high nibble is the state.
constexpr std::uint8_t kStateMask = 0xf0;
constexpr std::uint8_t kStateMachineWaiting = 0x20;  // done only once unk36 <= 24
constexpr std::uint8_t kStateTradeShip = 0x30;       // the shipyard launches a trade ship
constexpr std::uint8_t kStateFerry = 0x40;           // ... or a ferry
constexpr std::uint8_t kMaxUnk36ForDone = 24;        // 0x18
constexpr std::uint16_t kNoShipPosition = 0xffff;    // Building::work_x unset
constexpr std::uint8_t kGone = 0xff;                 // Settler::owner of a removed settler

// The offset (int32 dx, dy in S3.EXE's data) from a building's position to
// the centre of its seven extra tiles, for the four building types that
// have them (the war machine workshops and the large gong: the seven tiles
// where the machine they make stands); nullptr for other types.
const std::int32_t* ExtraTilesOffset(std::uint8_t type) {
    switch (type) {
        case Value(B::CatapultWorkshop): return g_extraTilesCentre[0];
        case Value(B::BallistaWorkshop): return g_extraTilesCentre[1];
        case Value(B::CannonWorkshop): return g_extraTilesCentre[2];
        case Value(B::LargeGong): return g_extraTilesCentre[3];
        default: return nullptr;
    }
}

// The seven extra tiles around the centre (cx, cy), in the original's order.
constexpr std::int32_t kExtraTiles[7][2] = {{-1, -1}, {0, -1}, {-1, 0}, {0, 0}, {1, 0}, {0, 1}, {1, 1}};

// The shipyard's ship tile: two steps from the ship position (Building::work_x/work_y, +0x30/+0x32) in
// direction (Building::dock_direction + 3) % 6.
void ShipTile(const Building& record, std::uint32_t& direction, std::uint32_t& x, std::uint32_t& y) {
    direction = (record.dock_direction + kDirectionCount / 2) % kDirectionCount;  // the opposite direction
    x = NeighbourX(NeighbourX(record.work_x, direction), direction);
    y = NeighbourY(NeighbourY(record.work_y, direction), direction);
}

}  // namespace

// Purpose: Takes the extra tiles a building needs beyond its site: blocks the seven tiles next to the war
// machine workshops and the large gong (where the machine they make stands), or, for a shipyard, launches
// the ship it has built onto its ship tile (a new ship settler). Marks the building as done.
std::uint32_t __fastcall CGameData_TakeBuildingExtraTiles(CGameData* world, void*, std::uint32_t building) {
    Building& record = world->buildings[building];
    if (record.workshop.extra_tiles == kExtraDone) {
        return building;
    }
    const std::uint8_t type = record.art;
    if (const std::int32_t* offset = ExtraTilesOffset(type)) {
        if ((record.workshop.state & kStateMask) == kStateMachineWaiting) {
            if (record.unk36 <= kMaxUnk36ForDone) {
                record.workshop.extra_tiles = kExtraDone;
            }
            return building;
        }
        const std::uint32_t cx = record.x + static_cast<std::uint32_t>(offset[0]);
        const std::uint32_t cy = record.y + static_cast<std::uint32_t>(offset[1]);
        if (type == Value(B::LargeGong)) {
            GameDebugLog(texts::kCentreXY, cx, cy);
        }
        for (const auto& step : kExtraTiles) {
            if (CGameData_IsFreeOfCatapultsNearby(world, nullptr, cx + step[0], cy + step[1]) == 0) {
                return 0;
            }
        }
        std::uint32_t result = 0;
        for (const auto& step : kExtraTiles) {
            result = CGameData_BlockTile(world, nullptr, cx + step[0], cy + step[1]);
        }
        record.workshop.extra_tiles = kExtraDone;
        return result;
    }
    if (type != kShipyard || record.work_x == kNoShipPosition) {
        return building;
    }
    std::uint32_t direction = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    ShipTile(record, direction, x, y);
    if (TileAt(world, x, y).settler != 0) {
        return y;
    }
    const std::uint8_t state = record.workshop.state & kStateMask;
    std::uint32_t ship = 0;
    if (state == kStateTradeShip) {
        ship = CGameData_CreateSettler(world, nullptr, x, y, Value(SettlerType::TradeShip), record.owner);
    } else if (state == kStateFerry) {
        ship = CGameData_CreateSettler(world, nullptr, x, y, Value(SettlerType::Ferry), record.owner);
    } else {
        ship = record.work_y;  // as the original (a stale register)
    }
    if (ship == 0) {
        return 0;
    }
    // The original addresses the settler as settlers + (ship << 6), in 32 bits like this indexing.
    Settler& settler = world->settlers[ship];
    GameDebugLog(texts::kShipProduction, static_cast<std::uint32_t>(settler.art),
                 static_cast<std::uint32_t>(settler.owner));
    SetJob(settler, SettlerJob::Job325);
    settler.slot = 0;
    settler.saved_animation = 0;
    settler.direction = (direction + 2) % kDirectionCount;
    record.workshop.extra_tiles = kExtraDone;
    return (direction + 2) / kDirectionCount;  // the original's EAX after the division
}

// Purpose: The opposite of TakeBuildingExtraTiles, probably when such a building is removed: frees the
// seven extra tiles again, or for a shipyard removes the ship standing on its ship tile.
std::uint32_t __fastcall CGameData_ReleaseBuildingExtraTiles(CGameData* world, void*, std::uint32_t building) {
    const Building& record = world->buildings[building];
    // The original's EAX: world + building * 0x3a (without the table's start).
    const std::uint32_t recordAddress = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world)) +
                                        building * static_cast<std::uint32_t>(sizeof(Building));
    const std::uint8_t type = record.art;
    if (const std::int32_t* offset = ExtraTilesOffset(type)) {
        // (The original also compares the state's high nibble with 0x14, which never matches.)
        const std::uint32_t cx = record.x + static_cast<std::uint32_t>(offset[0]);
        const std::uint32_t cy = record.y + static_cast<std::uint32_t>(offset[1]);
        std::uint32_t result = 0;
        for (const auto& step : kExtraTiles) {
            result = CGameData_UnblockTile(world, nullptr, cx + step[0], cy + step[1]);
        }
        return result;
    }
    if (type != kShipyard || record.workshop.state == 0) {
        return recordAddress;
    }
    std::uint32_t direction = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    ShipTile(record, direction, x, y);
    Tile& tile = TileAt(world, x, y);
    world->settlers[tile.settler].owner = kGone;
    tile.settler = 0;
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&tile));
}

// Purpose: For every castle (type 18, 0x12), unblocks one or two tiles at a fixed place next to it that
// depends on the owner's race (Romans: (x-2, y+3); Egyptians: (x, y+7) and (x, y+6)). Probably repairs
// tiles after a savegame is loaded.
std::uint32_t __fastcall CGameData_UnblockCastleTiles(CGameData* world, void*) {
    std::uint32_t count = world->building_high_water;
    for (std::uint32_t building = 1; building <= count; ++building) {
        const Building& record = world->buildings[building];
        if (record.art == kCastle) {
            const std::uint32_t player = record.owner;
            const Race race = RaceOf(world->players[player]);
            const std::uint32_t x = record.x;
            const std::uint32_t y = record.y;
            if (race == Race::Roman) {
                if ((TileFlags(world, x - 2, y + 3) & kTileBlocked) != 0) {
                    CGameData_UnblockTile(world, nullptr, x - 2, y + 3);
                }
            } else if (race == Race::Egyptian) {
                if ((TileFlags(world, x, y + 7) & kTileBlocked) != 0) {
                    CGameData_UnblockTile(world, nullptr, x, y + 7);
                }
                const std::uint32_t x2 = record.x;  // the original reads the position again
                const std::uint32_t y2 = record.y;
                if ((TileFlags(world, x2, y2 + 6) & kTileBlocked) != 0) {
                    CGameData_UnblockTile(world, nullptr, x2, y2 + 6);
                }
            }
        }
        count = world->building_high_water;
    }
    return count;
}

using AreaFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t area);
using BuildingFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t building);
using WorldFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx);

S3RE_HOOK(0x004ff3c0, CGameData_ClearAreaGoodsSettings1D2, AreaFn, Original_CGameData_ClearAreaGoodsSettings1D2,
          CGameData_ClearAreaGoodsSettings1D2);
S3RE_HOOK(0x004dd6f0, CGameData_TakeBuildingExtraTiles, BuildingFn, Original_CGameData_TakeBuildingExtraTiles,
          CGameData_TakeBuildingExtraTiles);
S3RE_HOOK(0x004dda10, CGameData_ReleaseBuildingExtraTiles, BuildingFn, Original_CGameData_ReleaseBuildingExtraTiles,
          CGameData_ReleaseBuildingExtraTiles);
S3RE_HOOK(0x004dfa60, CGameData_UnblockCastleTiles, WorldFn, Original_CGameData_UnblockCastleTiles,
          CGameData_UnblockCastleTiles);
S3RE_HOOK(0x004e6510, CGameData_InitArea, InitAreaFn, Original_CGameData_InitArea, CGameData_InitArea);
S3RE_HOOK(0x004e64c0, CGameData_AllocArea, AllocAreaFn, Original_CGameData_AllocArea, CGameData_AllocArea);
S3RE_HOOK(0x004e54f0, CGameData_MoveAreaContents, MoveAreaContentsFn, Original_CGameData_MoveAreaContents,
          CGameData_MoveAreaContents);
S3RE_HOOK(0x004e5a20, CGameData_MergeAreaContents, MergeAreaContentsFn, Original_CGameData_MergeAreaContents,
          CGameData_MergeAreaContents);
S3RE_HOOK(0x004e6150, CGameData_JoinAreasAtTile, TileFn, Original_CGameData_JoinAreasAtTile,
          CGameData_JoinAreasAtTile);
S3RE_HOOK(0x004e5d50, CGameData_SplitAreaAtTile, TileFn, Original_CGameData_SplitAreaAtTile,
          CGameData_SplitAreaAtTile);
S3RE_HOOK(0x0051b6f0, CGameData_BlockTile, BlockTileFn, Original_CGameData_BlockTile, CGameData_BlockTile);
S3RE_HOOK(0x0051b770, CGameData_UnblockTile, BlockTileFn, Original_CGameData_UnblockTile, CGameData_UnblockTile);
S3RE_HOOK(0x0051b5d0, CGameData_SetOwnerOfFreeTile, SetOwnerFn, Original_CGameData_SetOwnerOfFreeTile,
          CGameData_SetOwnerOfFreeTile);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// Up to settler 0x10000 (pile and settler numbers handed out stay below it).
constexpr std::size_t kTestWorld = offsetof(CGameData, settlers) + 0x10000 * sizeof(Settler);
constexpr std::uint32_t kUsedAreas = 3;     // areas 1..3 hold the map's tiles
constexpr std::uint32_t kTestAreas = 16;    // 4..16 used or free
constexpr std::int32_t kSteps[19][2] = {{0, 0},  {1, 0},  {1, 1},   {0, 1},   {-1, 0}, {-1, -1}, {0, -1},
                                        {2, 0},  {2, 1},  {2, 2},   {1, 2},   {0, 2},  {-1, 1},  {-2, 0},
                                        {-2, -1}, {-2, -2}, {-1, -2}, {0, -2}, {1, -1}};

// Event ring indices in range (InitArea and MoveAreaContents queue events).
void SetUpEvents(test::Context& ctx, test::WorldPair& worlds) {
    const std::uint32_t pendingRead = ctx.rng().Below(1000);
    const std::uint32_t pendingWrite = ctx.rng().Below(1000);
    const std::uint32_t eventWrite = ctx.rng().Below(1000);
    worlds.Both([&](CGameData* w) {
        w->pending_event_index.read = pendingRead;
        w->pending_event_index.write = pendingWrite;
        w->event_index.write = eventWrite;
    });
}

// The real offset table entries 0..18 (the tile, its six neighbours, the ring around them).
void SetUpOffsets(test::WorldPair& worlds) {
    worlds.Both([&](CGameData* w) {
        for (std::uint32_t k = 0; k < 19; ++k) {
            w->offsets[k].dx = kSteps[k][0];
            w->offsets[k].dy = kSteps[k][1];
        }
    });
}

// Valid, terminated lists for the area: the pile list +0x8, the 36 goods
// pile lists and the 48 settler lists, each empty or a few fresh members on
// tiles of the size x size map. `nextPile`/`nextSettler` hand out numbers.
void SetUpAreaLists(test::Context& ctx, test::WorldPair& worlds, std::uint32_t area, std::uint32_t size,
                    std::uint32_t& nextPile, std::uint32_t& nextSettler) {
    // `head(w)` is the list head in world w, `table(w)` the record table.
    auto fill = [&](auto head, auto table, const auto& links, std::uint32_t& next) {
        const std::uint32_t length = ctx.rng().Chance(1, 5) ? 1 + ctx.rng().Below(4) : 0;
        std::uint32_t previous = 0;
        const auto first = static_cast<std::uint16_t>(length ? next : 0);
        worlds.Both([&](CGameData* w) { head(w) = first; });
        for (std::uint32_t k = 0; k < length; ++k) {
            const std::uint32_t member = next++;
            const auto after = static_cast<std::uint16_t>(k + 1 < length ? next : 0);
            const auto x = static_cast<std::uint16_t>(ctx.rng().Below(size));
            const auto y = static_cast<std::uint16_t>(ctx.rng().Below(size));
            worlds.Both([&](CGameData* w) {
                auto& m = table(w)[member];
                m.*links.previous = static_cast<std::uint16_t>(previous);
                m.*links.next = after;
                m.*links.x = x;
                m.*links.y = y;
            });
            previous = member;
        }
    };
    auto piles = [](CGameData* w) { return w->piles; };
    auto settlers = [](CGameData* w) { return w->settlers; };
    fill([&](CGameData* w) -> auto& { return w->areas[area].pile_list; }, piles, kPileLinks, nextPile);
    for (std::uint32_t i = 0; i < kGoodsTypes; ++i) {
        fill([&](CGameData* w) -> auto& { return w->areas[area].piles_by_goods[i]; }, piles, kPileLinks, nextPile);
    }
    for (std::uint32_t i = 0; i < kAreaSettlerListCount; ++i) {
        fill([&](CGameData* w) -> auto& { return w->areas[area].settlers_by_type[i]; }, settlers, kSettlerLinks,
             nextSettler);
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
            const auto flags = static_cast<std::uint8_t>(ctx.rng().Next32());
            const auto owner = static_cast<std::uint8_t>(ctx.rng().Below(20));
            worlds.Both([&](CGameData* w) {
                Tile& tile = TileAt(w, tx, ty);
                tile.area = static_cast<std::uint16_t>(area);
                tile.flags = flags;
                tile.owner = owner;
            });
        }
    }
    std::uint32_t nextPile = 1 + ctx.rng().Below(0x1000);
    std::uint32_t nextSettler = 1 + ctx.rng().Below(0x1000);
    for (std::uint32_t area = 1; area <= kTestAreas; ++area) {
        const bool used = area <= kUsedAreas || (area < kTestAreas && ctx.rng().Chance(1, 2)) || full;
        const auto player = static_cast<std::uint8_t>(used ? ctx.rng().Below(20) : kFreeArea);
        worlds.Both([&](CGameData* w) { w->areas[area].player = player; });
        if (area <= kUsedAreas) {
            SetUpAreaLists(ctx, worlds, area, size, nextPile, nextSettler);
        }
    }
    for (std::uint32_t player = 0; player < 20; ++player) {
        const std::int32_t race = ctx.rng().Chance(1, 10) ? static_cast<std::int32_t>(ctx.rng().Next32())
                                                          : static_cast<std::int32_t>(ctx.rng().Below(4));
        worlds.Both([&](CGameData* w) { w->players[player].race = race; });
    }
    const std::uint32_t highWater = ctx.rng().Below(kTestAreas + 2);
    worlds.Both([&](CGameData* w) { w->area_high_water = highWater; });
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
            const auto wide = static_cast<std::uint16_t>(onMap ? label[tx * size + ty] : 0);
            // Byte-10 regions: one region over the whole map (one split per call is safe).
            const auto walk = static_cast<std::uint8_t>(onMap && ctx.rng().Chance(7, 8) ? 1 : 0);
            worlds.Both([&](CGameData* w) {
                Tile& tile = TileAt(w, tx, ty);
                tile.wide_region = wide;
                tile.walk_region = walk;
            });
        }
    }
    const std::uint32_t wideHighWater = ctx.rng().Below(next + 2);
    const std::uint32_t walkHighWater = ctx.rng().Below(4);
    worlds.Both([&](CGameData* w) {
        for (std::uint32_t id = 0; id < next + 2; ++id) {
            w->wide_region_used[id] = static_cast<std::uint8_t>(id != 0 && id < next ? 1 : 0);
        }
        w->wide_region_high_water = wideHighWater;
        for (std::uint32_t id = 0; id < 8; ++id) {
            w->walk_region_used[id] = static_cast<std::uint8_t>(id == 1 ? 1 : 0);
        }
        w->walk_region_high_water = walkHighWater;
    });
}

// Wrong: Egyptians get the Romans' defaults.
void __fastcall WrongInitArea(CGameData* world, void* edx, std::uint32_t area, std::uint32_t player) {
    CGameData_InitArea(world, edx, area, player);
    if (RaceOf(world->players[player]) == Race::Egyptian) {
        StoreAll(world->areas[area], kRace0Defaults);
    }
}

void CompareInitArea(test::Context& ctx, InitAreaFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        SetUpEvents(ctx, worlds);
        const std::uint32_t area = 1 + ctx.rng().Below(0xffe);
        const std::uint32_t player = ctx.rng().Chance(1, 10) ? ctx.rng().Below(256) : ctx.rng().Below(20);
        const std::int32_t race = ctx.rng().Chance(1, 8) ? static_cast<std::int32_t>(ctx.rng().Next32())
                                                         : static_cast<std::int32_t>(ctx.rng().Below(5));
        worlds.Both([&](CGameData* w) { w->players[player].race = race; });
        Original_CGameData_InitArea(worlds.original(), nullptr, area, player);
        candidate(worlds.ours(), nullptr, area, player);
        if ((i % 32 == 31 || i == cases - 1) && !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 31 before it): area %u player %u", i, area, player);
            return;
        }
    }
}

// Wrong: never hands out the last free area found by a full scan (off by one in the limit).
std::uint32_t __fastcall WrongAllocArea(CGameData* world, void* edx, std::uint32_t player) {
    if (world->areas[kLastArea - 1].player == kFreeArea) {
        bool othersUsed = true;
        for (std::uint32_t a = 1; a < kLastArea - 1 && othersUsed; ++a) {
            othersUsed = world->areas[a].player != kFreeArea;
        }
        if (othersUsed) {
            return 0;
        }
    }
    return CGameData_AllocArea(world, edx, player);
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
            const auto player = static_cast<std::uint8_t>(a == first ? kFreeArea : ctx.rng().Below(20));
            worlds.Both([&](CGameData* w) { w->areas[a].player = player; });
        }
        const std::uint32_t highWater = ctx.rng().Chance(1, 2) ? first + ctx.rng().Below(3) - 1 : ctx.rng().Next32();
        worlds.Both([&](CGameData* w) { w->area_high_water = highWater; });
        const std::uint32_t player = ctx.rng().Below(20);
        const auto race = static_cast<std::int32_t>(ctx.rng().Below(4));
        worlds.Both([&](CGameData* w) { w->players[player].race = race; });
        const std::uint32_t expected = Original_CGameData_AllocArea(worlds.original(), nullptr, player);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, player);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 16 == 15 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 15 before it): first free %u", i, first);
            return;
        }
    }
}

// Wrong: does not move settlers.
void __fastcall WrongMoveAreaContents(CGameData* world, void* edx, std::uint32_t oldArea, std::uint32_t newArea) {
    std::uint16_t saved[kAreaSettlerListCount];
    for (std::uint32_t i = 0; i < kAreaSettlerListCount; ++i) {
        saved[i] = world->areas[oldArea].settlers_by_type[i];
        world->areas[oldArea].settlers_by_type[i] = 0;
    }
    CGameData_MoveAreaContents(world, edx, oldArea, newArea);
    for (std::uint32_t i = 0; i < kAreaSettlerListCount; ++i) {
        world->areas[oldArea].settlers_by_type[i] = saved[i];
    }
}

void CompareMoveAreaContents(test::Context& ctx, MoveAreaContentsFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = 4 + ctx.rng().Below(20);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        SetUpAreaMap(ctx, worlds, size);
        const std::uint32_t oldArea = 1 + ctx.rng().Below(kUsedAreas);
        const std::uint32_t newArea = 1 + (oldArea + ctx.rng().Below(kUsedAreas - 1)) % kUsedAreas;
        Original_CGameData_MoveAreaContents(worlds.original(), nullptr, oldArea, newArea);
        candidate(worlds.ours(), nullptr, oldArea, newArea);
        if ((i % 16 == 15 || i == cases - 1) && !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 15 before it): %u -> %u", i, oldArea, newArea);
            return;
        }
    }
}

// Wrong: keeps into's settings even when `from` is bigger.
void __fastcall WrongMergeAreaContents(CGameData* world, void* edx, std::uint32_t into, std::uint32_t from) {
    const std::uint32_t saved = world->areas[into].tile_count;
    world->areas[into].tile_count = 0xffffffffu;
    CGameData_MergeAreaContents(world, edx, into, from);
    world->areas[into].tile_count = saved;
}

void CompareMergeAreaContents(test::Context& ctx, MergeAreaContentsFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = 4 + ctx.rng().Below(20);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        SetUpAreaMap(ctx, worlds, size);
        const std::uint32_t into = 1 + ctx.rng().Below(kUsedAreas);
        const std::uint32_t from = 1 + (into + ctx.rng().Below(kUsedAreas - 1)) % kUsedAreas;
        for (const std::uint32_t area : {into, from}) {
            const std::uint32_t tiles = ctx.rng().Below(4);
            worlds.Both([&](CGameData* w) { w->areas[area].tile_count = tiles; });
        }
        Original_CGameData_MergeAreaContents(worlds.original(), nullptr, into, from);
        candidate(worlds.ours(), nullptr, into, from);
        if ((i % 16 == 15 || i == cases - 1) && !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 15 before it): %u <- %u", i, into, from);
            return;
        }
    }
}

// Never joins with a neighbour of another owner (it sets the neighbour's owner for the call).
void __fastcall WrongJoinAreasAtTile(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t nx = x + 1;
    std::uint8_t& owner = TileAt(world, nx, y).owner;
    const std::uint8_t saved = owner;
    if (TileAreaWord(world, nx, y) != 0) {
        owner = TileAt(world, x, y).owner;
    }
    CGameData_JoinAreasAtTile(world, edx, x, y);
    owner = saved;
}

// A map for joining areas: connected stripe areas, owners of 1..3 players
// (returned) on the map's tiles; tiles without an area (the frame) have
// none (0xfe), so no area 0 is ever flooded; most tiles not blocked.
std::uint32_t SetUpJoinMap(test::Context& ctx, test::WorldPair& worlds, std::uint32_t size) {
    SetUpAreaMap(ctx, worlds, size, ctx.rng().Chance(1, 40), /*stripes=*/true);
    const std::uint32_t owners = 1 + ctx.rng().Below(3);
    for (std::uint32_t tx = 0xffffffffu; tx != size + 1; ++tx) {
        for (std::uint32_t ty = 0xffffffffu; ty != size + 1; ++ty) {
            const Tile& before = TileAt(worlds.original(), tx, ty);
            const bool none = before.area == 0;
            const auto owner = static_cast<std::uint8_t>(none ? 0xfe : ctx.rng().Below(owners));
            const std::uint8_t flags = before.flags;
            const auto newFlags = static_cast<std::uint8_t>(ctx.rng().Chance(1, 4) ? flags : flags & ~kTileBlocked);
            worlds.Both([&](CGameData* w) {
                Tile& tile = TileAt(w, tx, ty);
                tile.owner = owner;
                tile.flags = newFlags;
            });
        }
    }
    return owners;
}

std::uint32_t RelativeIfPointer(std::uint32_t value, CGameData* world) {
    const auto base = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));
    return value - base < kTestWorld ? value - base : value;
}

// Wrong: never merges byte-10 regions.
std::uint32_t __fastcall WrongUnblockTile(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y) {
    std::uint8_t& walk = TileAt(world, x, y).walk_region;
    const std::uint8_t saved = walk;
    walk = 1;
    const std::uint32_t result = CGameData_UnblockTile(world, edx, x, y);
    if (saved == 0 && walk == 1) {
        walk = 0;
    }
    return result;
}

void CompareUnblockTile(test::Context& ctx, BlockTileFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    SetUpOffsets(worlds);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = 8 + ctx.rng().Below(12);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        const std::uint32_t owners = SetUpJoinMap(ctx, worlds, size);
        SetUpWord16Regions(ctx, worlds, size);
        // Three tiles inside (region merges look one tile beyond their tiles).
        const std::uint32_t x = 3 + ctx.rng().Below(size - 6);
        const std::uint32_t y = 3 + ctx.rng().Below(size - 6);
        const std::uint8_t flags = static_cast<std::uint8_t>(ctx.rng().Next32());
        const auto newFlags =
            static_cast<std::uint8_t>(ctx.rng().Chance(5, 6) ? flags | kTileBlocked : flags & ~kTileBlocked);
        const auto walk = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0 : 1);
        const auto area = static_cast<std::uint16_t>(ctx.rng().Chance(3, 4) ? 0 : 1 + ctx.rng().Below(3));
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Chance(1, 8) ? 0xff : ctx.rng().Below(owners));
        worlds.Both([&](CGameData* w) {
            Tile& tile = w->tiles[x][y];
            tile.flags = newFlags;
            tile.walk_region = walk;
            tile.area = area;
            tile.owner = owner;
        });
        const std::uint32_t expected =
            RelativeIfPointer(Original_CGameData_UnblockTile(worlds.original(), nullptr, x, y), worlds.original());
        const std::uint32_t actual = RelativeIfPointer(candidate(worlds.ours(), nullptr, x, y), worlds.ours());
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 8 == 7 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 7 before it): (%u, %u) map %u", i, x, y, size);
            return;
        }
    }
}

// Wrong: also takes tiles that have an owner.
std::uint32_t __fastcall WrongSetOwnerOfFreeTile(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                 std::uint32_t owner) {
    std::uint8_t& tileOwner = TileAt(world, x, y).owner;
    if (tileOwner != 0xff) {
        tileOwner = 0xff;
    }
    return CGameData_SetOwnerOfFreeTile(world, edx, x, y, owner);
}

void CompareSetOwnerOfFreeTile(test::Context& ctx, SetOwnerFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    SetUpOffsets(worlds);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = 4 + ctx.rng().Below(16);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        const std::uint32_t owners = SetUpJoinMap(ctx, worlds, size);
        const std::uint32_t x = 1 + ctx.rng().Below(size - 2);
        const std::uint32_t y = 1 + ctx.rng().Below(size - 2);
        const auto tileOwner = static_cast<std::uint8_t>(ctx.rng().Chance(4, 5) ? 0xff : ctx.rng().Below(owners));
        const auto area = static_cast<std::uint16_t>(ctx.rng().Chance(4, 5) ? 0 : 1 + ctx.rng().Below(3));
        // A pile on the tile or not (AddTilePileToArea): pile words in the
        // used range, small goods types and amounts.
        const std::uint32_t pile = ctx.rng().Chance(1, 2) ? 0 : 0x2000 + ctx.rng().Below(0x1000);
        worlds.Both([&](CGameData* w) {
            Tile& tile = w->tiles[x][y];
            tile.owner = tileOwner;
            tile.area = area;
            tile.object_number = static_cast<std::uint16_t>(pile);
        });
        if (pile != 0) {
            const auto good = static_cast<std::uint8_t>(ctx.rng().Below(36));
            const auto amount = static_cast<std::uint8_t>(ctx.rng().Below(9));
            worlds.Both([&](CGameData* w) {
                w->piles[pile].good = good;
                w->piles[pile].amount = amount;
            });
        }
        const std::uint32_t owner = ctx.rng().Chance(1, 8) ? 0xff : ctx.rng().Chance(1, 20) ? 0x100 + ctx.rng().Below(owners)
                                                                                           : ctx.rng().Below(owners);
        const std::uint32_t expected =
            RelativeIfPointer(Original_CGameData_SetOwnerOfFreeTile(worlds.original(), nullptr, x, y, owner), worlds.original());
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
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        const std::uint32_t owners = SetUpJoinMap(ctx, worlds, size);
        const std::uint32_t x = 1 + ctx.rng().Below(size - 2);
        const std::uint32_t y = 1 + ctx.rng().Below(size - 2);
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Below(owners));
        worlds.Both([&](CGameData* w) {
            w->tiles[x][y].owner = owner;
            w->tiles[x][y].area = 0;
        });
        Original_CGameData_JoinAreasAtTile(worlds.original(), nullptr, x, y);
        candidate(worlds.ours(), nullptr, x, y);
        if ((i % 8 == 7 || i == cases - 1) && !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 7 before it): (%u, %u) map %u", i, x, y, size);
            return;
        }
    }
}

// Wrong: never splits (only frees an area left without neighbours).
void __fastcall WrongSplitAreaAtTile(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t area = TileAreaWord(world, x, y);
    std::uint32_t count = 0;
    for (std::uint32_t k = 0; k < kDirections; ++k) {
        if (TileAreaWord(world, x + kSteps[k + 1][0], y + kSteps[k + 1][1]) == area) {
            ++count;
        }
    }
    if (count == 0 || count > 4 || count < 2) {
        CGameData_SplitAreaAtTile(world, edx, x, y);
    } else {
        CGameData_RemoveTileFromArea(world, nullptr, x, y);
    }
}

template <typename Fn, typename Call>
void CompareOnAreaMap(test::Context& ctx, int cases, bool regions, Call call) {
    test::WorldPair worlds(ctx, kTestWorld);
    SetUpOffsets(worlds);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = 4 + ctx.rng().Below(16);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        SetUpAreaMap(ctx, worlds, size, ctx.rng().Chance(1, 40));
        if (regions) {
            SetUpWord16Regions(ctx, worlds, size);
        }
        const std::uint32_t x = 1 + ctx.rng().Below(size - 2);
        const std::uint32_t y = 1 + ctx.rng().Below(size - 2);
        // The tile in one of the used areas (its callers only call it then).
        if (!regions || ctx.rng().Chance(3, 4)) {
            const auto area = static_cast<std::uint16_t>(1 + ctx.rng().Below(kUsedAreas));
            worlds.Both([&](CGameData* w) { w->tiles[x][y].area = area; });
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
                                 Original_CGameData_SplitAreaAtTile(worlds.original(), nullptr, x, y);
                                 candidate(worlds.ours(), nullptr, x, y);
                             });
}

// Wrong: does not split byte-10 regions.
std::uint32_t __fastcall WrongBlockTile(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y) {
    std::uint8_t& walk = TileAt(world, x, y).walk_region;
    const std::uint8_t saved = walk;
    walk = 0;
    const std::uint32_t result = CGameData_BlockTile(world, edx, x, y);
    walk = saved;
    return result;
}

void CompareBlockTile(test::Context& ctx, BlockTileFn candidate, int cases) {
    CompareOnAreaMap<BlockTileFn>(
        ctx, cases, true,
        [&](test::WorldPair& worlds, std::uint32_t x, std::uint32_t y, std::uint32_t& expected, std::uint32_t& actual) {
            // Mostly not yet blocked; sometimes a marked area word (>= 0x8000).
            const std::uint8_t flags = worlds.original()->tiles[x][y].flags;
            const auto newFlags =
                static_cast<std::uint8_t>(ctx.rng().Chance(1, 6) ? flags | kTileBlocked : flags & ~kTileBlocked);
            worlds.Both([&](CGameData* w) { w->tiles[x][y].flags = newFlags; });
            if (ctx.rng().Chance(1, 10)) {
                const auto area = static_cast<std::uint16_t>(layout::kTileAreaWater + ctx.rng().Below(4));
                worlds.Both([&](CGameData* w) { w->tiles[x][y].area = area; });
            }
            expected = Original_CGameData_BlockTile(worlds.original(), nullptr, x, y);
            actual = candidate(worlds.ours(), nullptr, x, y);
        });
}

}  // namespace

S3RE_TEST(CGameData_InitArea_MatchesOriginal) {
    CompareInitArea(ctx, &CGameData_InitArea, 1000);
}
S3RE_TEST(CGameData_InitArea_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareInitArea(probe, &WrongInitArea, 300);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_AllocArea_MatchesOriginal) {
    CompareAllocArea(ctx, &CGameData_AllocArea, 400);
}
S3RE_TEST(CGameData_AllocArea_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareAllocArea(probe, &WrongAllocArea, 200);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_MoveAreaContents_MatchesOriginal) {
    CompareMoveAreaContents(ctx, &CGameData_MoveAreaContents, 600);
}
S3RE_TEST(CGameData_MoveAreaContents_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMoveAreaContents(probe, &WrongMoveAreaContents, 100);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_MergeAreaContents_MatchesOriginal) {
    CompareMergeAreaContents(ctx, &CGameData_MergeAreaContents, 600);
}
S3RE_TEST(CGameData_MergeAreaContents_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMergeAreaContents(probe, &WrongMergeAreaContents, 100);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_JoinAreasAtTile_MatchesOriginal) {
    CompareJoinAreas(ctx, &CGameData_JoinAreasAtTile, 2000);
}
S3RE_TEST(CGameData_JoinAreasAtTile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareJoinAreas(probe, &WrongJoinAreasAtTile, 400);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_UnblockTile_MatchesOriginal) {
    CompareUnblockTile(ctx, &CGameData_UnblockTile, 1500);
}
S3RE_TEST(CGameData_UnblockTile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareUnblockTile(probe, &WrongUnblockTile, 300);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_SetOwnerOfFreeTile_MatchesOriginal) {
    CompareSetOwnerOfFreeTile(ctx, &CGameData_SetOwnerOfFreeTile, 2000);
}
S3RE_TEST(CGameData_SetOwnerOfFreeTile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSetOwnerOfFreeTile(probe, &WrongSetOwnerOfFreeTile, 300);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_SplitAreaAtTile_MatchesOriginal) {
    CompareSplitArea(ctx, &CGameData_SplitAreaAtTile, 2000);
}
S3RE_TEST(CGameData_SplitAreaAtTile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSplitArea(probe, &WrongSplitAreaAtTile, 400);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_BlockTile_MatchesOriginal) {
    CompareBlockTile(ctx, &CGameData_BlockTile, 1500);
}
S3RE_TEST(CGameData_BlockTile_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareBlockTile(probe, &WrongBlockTile, 300);
    S3RE_EXPECT(probe.failures() > 0);
}

// ---------------------------------------------------------------------------
// Tests: area settings, building extra tiles
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestBuildings = 64;
constexpr std::int32_t kDirectionX[6] = {1, 1, 0, -1, -1, 0};
constexpr std::int32_t kDirectionY[6] = {0, 1, 1, 0, -1, -1};

// A map for taking and releasing extra tiles: a join map (connected stripe
// areas, owners), word-16 regions labelled per connected stretch, one
// byte-10 region over the whole map (connected, so a call may split it
// several times), and few occupants (tile word +0x0; their settlers often
// catapults). Returns the number of owners.
std::uint32_t SetUpExtraTilesMap(test::Context& ctx, test::WorldPair& worlds, std::uint32_t size) {
    const std::uint32_t owners = SetUpJoinMap(ctx, worlds, size);
    SetUpWord16Regions(ctx, worlds, size);
    for (std::uint32_t tx = 0xffffffffu; tx != size + 1; ++tx) {
        for (std::uint32_t ty = 0xffffffffu; ty != size + 1; ++ty) {
            const bool onMap = tx < size && ty < size;
            const std::uint32_t occupant = ctx.rng().Chance(1, 40) ? 1 + ctx.rng().Below(0x8000) : 0;
            worlds.Both([&](CGameData* w) {
                Tile& tile = TileAt(w, tx, ty);
                tile.walk_region = static_cast<std::uint8_t>(onMap ? 1 : 0);
                tile.settler = static_cast<std::uint16_t>(occupant);
            });
            if (occupant != 0) {
                const auto art = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? Value(SettlerType::Catapult)
                                                                                 : ctx.rng().Below(0x50));
                worlds.Both([&](CGameData* w) { w->settlers[occupant].art = art; });
            }
        }
    }
    return owners;
}

// A building of a random type: one of the four with extra tiles (its seven
// tiles 4 inside the map), a shipyard (its ship tile 4 inside the map; with
// `unset`, the ship position sometimes unset: only TakeBuildingExtraTiles
// checks that, ReleaseBuildingExtraTiles would read far off the map) or
// another type.
std::uint32_t SetUpExtraTilesBuilding(test::Context& ctx, test::WorldPair& worlds, std::uint32_t size, bool unset) {
    const std::uint32_t building = 1 + ctx.rng().Below(kTestBuildings);
    auto record = [&](CGameData* w) -> Building& { return w->buildings[building]; };
    static constexpr std::uint8_t kTypes[] = {Value(B::CatapultWorkshop), Value(B::BallistaWorkshop),
                                              Value(B::CannonWorkshop),   Value(B::LargeGong),
                                              kShipyard,                  kShipyard,
                                              kShipyard,                  kCastle};
    const std::uint8_t type =
        ctx.rng().Chance(1, 10) ? static_cast<std::uint8_t>(ctx.rng().Below(256)) : kTypes[ctx.rng().Below(sizeof(kTypes))];
    const auto owner = static_cast<std::uint8_t>(ctx.rng().Below(20));
    const auto extraTiles = static_cast<std::uint8_t>(ctx.rng().Chance(1, 8) ? kExtraDone : ctx.rng().Below(0xfe));
    static constexpr std::uint8_t kStates[] = {0x20, 0x30, 0x40, 0x00, 0x30, 0x40, 0x50};
    const std::uint8_t state = static_cast<std::uint8_t>(kStates[ctx.rng().Below(sizeof(kStates))] |
                                                         (ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Below(16)));
    const std::uint8_t storedState = ctx.rng().Chance(1, 10) ? std::uint8_t{0} : state;
    const auto byte36 = static_cast<std::uint8_t>(0x10 + ctx.rng().Below(0x10));
    worlds.Both([&](CGameData* w) {
        record(w).art = type;
        record(w).owner = owner;
        record(w).workshop.extra_tiles = extraTiles;
        record(w).workshop.state = storedState;
        record(w).unk36 = byte36;
    });
    if (const std::int32_t* offset = ExtraTilesOffset(type)) {
        // The building's position is a word: y = cy - dy must not be negative.
        const std::uint32_t cx = 4 + ctx.rng().Below(size - 8);
        const std::uint32_t cy = 8 + ctx.rng().Below(size - 12);
        worlds.Both([&](CGameData* w) {
            record(w).x = static_cast<std::uint16_t>(cx - static_cast<std::uint32_t>(offset[0]));
            record(w).y = static_cast<std::uint16_t>(cy - static_cast<std::uint32_t>(offset[1]));
        });
    } else {
        const std::uint32_t d = ctx.rng().Below(6);
        const auto direction = static_cast<std::uint16_t>((d + 3) % 6 + 6 * ctx.rng().Below(100));
        const std::uint32_t tx = 4 + ctx.rng().Below(size - 8);
        const std::uint32_t ty = 4 + ctx.rng().Below(size - 8);
        const std::uint32_t sx = tx - 2 * static_cast<std::uint32_t>(kDirectionX[d]);
        const auto shipX = static_cast<std::uint16_t>(unset && ctx.rng().Chance(1, 10) ? 0xffff : sx);
        const auto shipY = static_cast<std::uint16_t>(ty - 2 * static_cast<std::uint32_t>(kDirectionY[d]));
        const auto occupant = static_cast<std::uint16_t>(ctx.rng().Chance(1, 4) ? 1 + ctx.rng().Below(0x8000) : 0);
        worlds.Both([&](CGameData* w) {
            record(w).dock_direction = direction;
            record(w).work_x = shipX;
            record(w).work_y = shipY;
            w->tiles[tx][ty].settler = occupant;
        });
    }
    return building;
}

// Blocks the building's seven extra tiles (each 7 in 8) the way the game does.
void BlockExtraTiles(test::Context& ctx, test::WorldPair& worlds, std::uint32_t building) {
    const Building& record = worlds.original()->buildings[building];
    const std::int32_t* offset = ExtraTilesOffset(record.art);
    if (offset == nullptr) {
        return;
    }
    const std::uint32_t cx = record.x + static_cast<std::uint32_t>(offset[0]);
    const std::uint32_t cy = record.y + static_cast<std::uint32_t>(offset[1]);
    for (const auto& step : kExtraTiles) {
        if (ctx.rng().Chance(7, 8)) {
            Original_CGameData_BlockTile(worlds.original(), nullptr, cx + step[0], cy + step[1]);
            Original_CGameData_BlockTile(worlds.ours(), nullptr, cx + step[0], cy + step[1]);
        }
    }
}

// Wrong: never marks the building as done.
std::uint32_t __fastcall WrongTakeBuildingExtraTiles(CGameData* world, void* edx, std::uint32_t building) {
    const std::uint8_t done = world->buildings[building].workshop.extra_tiles;
    const std::uint32_t result = CGameData_TakeBuildingExtraTiles(world, edx, building);
    world->buildings[building].workshop.extra_tiles = done;
    return result;
}

// Wrong: the ballista workshop has no extra tiles.
std::uint32_t __fastcall WrongReleaseBuildingExtraTiles(CGameData* world, void* edx, std::uint32_t building) {
    std::uint8_t& type = world->buildings[building].art;
    const std::uint8_t saved = type;
    if (type == Value(B::BallistaWorkshop)) {
        type = Value(B::SmallPagoda);
    }
    const std::uint32_t result = CGameData_ReleaseBuildingExtraTiles(world, edx, building);
    type = saved;
    return result;
}

template <typename Fn>
void CompareExtraTiles(test::Context& ctx, Fn original, Fn candidate, int cases, bool release) {
    test::WorldPair worlds(ctx, kTestWorld);
    SetUpOffsets(worlds);
    DebugLogRecorder logs;
    std::size_t logged = 0;
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = 14 + ctx.rng().Below(8);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        SetUpExtraTilesMap(ctx, worlds, size);
        const std::uint32_t building = SetUpExtraTilesBuilding(ctx, worlds, size, !release);
        if (release) {
            BlockExtraTiles(ctx, worlds, building);
        }
        logs.Take();
        const std::uint32_t expected = RelativeIfPointer(original(worlds.original(), nullptr, building), worlds.original());
        const std::vector<DebugLogCall> expectedLogs = logs.Take();
        const std::uint32_t actual = RelativeIfPointer(candidate(worlds.ours(), nullptr, building), worlds.ours());
        const std::vector<DebugLogCall> actualLogs = logs.Take();
        logged += expectedLogs.size();
        if (!S3RE_EXPECT_EQ(expected, actual) || !S3RE_EXPECT(expectedLogs == actualLogs) ||
            ((i % 4 == 3 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 3 before it): building %u type 0x%x map %u", i,
                     building, worlds.original()->buildings[building].art, size);
            return;
        }
    }
    S3RE_EXPECT(release || logged > 0);
}

// Wrong: Egyptians unblock the Romans' tile.
std::uint32_t __fastcall WrongUnblockCastleTiles(CGameData* world, void* edx) {
    for (std::uint32_t building = 1; building <= world->building_high_water; ++building) {
        const Building& record = world->buildings[building];
        if (record.art == kCastle) {
            const Race race = RaceOf(world->players[record.owner]);
            if (race == Race::Roman || race == Race::Egyptian) {
                CGameData_UnblockTile(world, edx, record.x - 2u, record.y + 3u);
            }
        }
    }
    return world->building_high_water;
}

void CompareUnblockCastleTiles(test::Context& ctx, WorldFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    SetUpOffsets(worlds);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = 16 + ctx.rng().Below(8);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        SetUpExtraTilesMap(ctx, worlds, size);
        const std::uint32_t count = ctx.rng().Chance(1, 10) ? 0 : 1 + ctx.rng().Below(5);
        worlds.Both([&](CGameData* w) { w->building_high_water = count; });
        for (std::uint32_t building = 1; building <= count; ++building) {
            const auto type = static_cast<std::uint8_t>(ctx.rng().Chance(2, 3) ? kCastle : ctx.rng().Below(0x40));
            const std::uint32_t player = ctx.rng().Below(20);
            const auto race =
                static_cast<std::int32_t>(ctx.rng().Chance(1, 6) ? ctx.rng().Below(4) : ctx.rng().Below(2));
            const std::uint32_t x = 5 + ctx.rng().Below(size - 8);
            const std::uint32_t y = 3 + ctx.rng().Below(size - 13);
            worlds.Both([&](CGameData* w) {
                Building& record = w->buildings[building];
                record.art = type;
                record.owner = static_cast<std::uint8_t>(player);
                w->players[player].race = race;
                record.x = static_cast<std::uint16_t>(x);
                record.y = static_cast<std::uint16_t>(y);
            });
            const std::uint32_t targets[3][2] = {{x - 2, y + 3}, {x, y + 7}, {x, y + 6}};
            for (const auto& target : targets) {
                if (ctx.rng().Chance(3, 4)) {
                    Original_CGameData_BlockTile(worlds.original(), nullptr, target[0], target[1]);
                    Original_CGameData_BlockTile(worlds.ours(), nullptr, target[0], target[1]);
                }
            }
        }
        const std::uint32_t expected = Original_CGameData_UnblockCastleTiles(worlds.original(), nullptr);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 4 == 3 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 3 before it): %u buildings, map %u", i, count, size);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(CGameData_ClearAreaGoodsSettings1D2_MatchesOriginal) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < 4000; ++i) {
        const std::uint32_t area = ctx.rng().Below(0x1000);
        const std::uint32_t expected = Original_CGameData_ClearAreaGoodsSettings1D2(worlds.original(), nullptr, area);
        const std::uint32_t actual = CGameData_ClearAreaGoodsSettings1D2(worlds.ours(), nullptr, area);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 512 == 511) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 511 before it): area %u", i, area);
            return;
        }
    }
}
S3RE_TEST(CGameData_TakeBuildingExtraTiles_MatchesOriginal) {
    CompareExtraTiles<BuildingFn>(ctx, Original_CGameData_TakeBuildingExtraTiles, &CGameData_TakeBuildingExtraTiles,
                                  800, false);
}
S3RE_TEST(CGameData_TakeBuildingExtraTiles_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareExtraTiles<BuildingFn>(probe, Original_CGameData_TakeBuildingExtraTiles, &WrongTakeBuildingExtraTiles, 200,
                                  false);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_ReleaseBuildingExtraTiles_MatchesOriginal) {
    CompareExtraTiles<BuildingFn>(ctx, Original_CGameData_ReleaseBuildingExtraTiles,
                                  &CGameData_ReleaseBuildingExtraTiles, 800, true);
}
S3RE_TEST(CGameData_ReleaseBuildingExtraTiles_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareExtraTiles<BuildingFn>(probe, Original_CGameData_ReleaseBuildingExtraTiles,
                                  &WrongReleaseBuildingExtraTiles, 200, true);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_UnblockCastleTiles_MatchesOriginal) {
    CompareUnblockCastleTiles(ctx, &CGameData_UnblockCastleTiles, 800);
}
S3RE_TEST(CGameData_UnblockCastleTiles_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareUnblockCastleTiles(probe, &WrongUnblockCastleTiles, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
