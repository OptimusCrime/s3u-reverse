// Loading a map file into the world (the map path of CLogicThread::OnStart).
//
// CGameData::PrepareLoadedTerrain (0x00519540): (): every terrain byte
//   (tile +0x6) of the map whose class (high nibble) is 0, i.e. every water
//   variant 0..15, becomes 0; then the water gets its depth from the shore:
//   ReplaceTerrainNextTo(0, 0x30, 0xff) marks the water next to the shore
//   (0x30) with the marker 0xff, (0, 0xff, 1) turns the water next to the
//   marks into depth 1, then (0, 1, 2) .. (0, 5, 6) the further bands;
//   ReplaceTerrainType(0, 7) makes the remaining water the deepest (7) and
//   ReplaceTerrainType(0xff, 0) turns the marks back into 0. So water is 0
//   along the shore, then 1..6 band by band, 7 further out. The first loop
//   is unsigned, the map size re-read per step. EAX = the last ReplaceTerrainType's (LoadMap ignores it). Called
//   by LoadMap after "Setting all sectors for loaded Map".
// CGameData::LoadMap (0x004fd540): (CMapFile* file, const uint32_t
//   slotToPlayer[]) ret 8, EAX unused (void). CLogicThread::OnStart calls it
//   for a new game on a map file, with a 20-entry table mapping the map's
//   owner slots to players (0xff = slot not taken). Chunks (re/file_formats.md
//   "Map chunks"), each released after use:
//   - 6 landscape (missing: none of this): "Loading map...", map size,
//     tiles (an untaken slot's tiles get no owner and search_mark 1); then,
//     with chunk 8, "Deleting Building-Bits" for the buildings of untaken
//     slots (object and footprint flags cleared by the layout of player
//     number slot's race); "Setting all sectors", PrepareLoadedTerrain,
//     BuildRegionsAndAreas.
//   - 8 buildings (finished buildings of taken slots, towers manned), then
//     7 settlers; without chunk 8: AddPlayerTowns and no chunk 7.
//   - 9 piles (not on tiles with search_mark), 10 victory conditions,
//     0x40 -> g_worldUpdateDisabled (cleared without it).
//   - the colours g_playerColourIndex[0..19] = GameSettings::PlayerSetup(i)
//     ->Colour() (PlayerSetup stays the original: UI-side Watcom vector code
//     behind a mutex), search marks cleared, UnblockCastleTiles, then the
//     campaign missions' fix-ups (49 resources halved, 50 ferries with
//     passengers, 53/59/61 player 0's mana, 59 trees set on fire, 70 towers
//     become Amazon).
//   Quirks kept: slot bytes index the slot table and the player table
//   without a bounds check; catapults and ships get the map slot as their
//   player (not the mapped player); mission 49 halves only y 0..249; a pile
//   record without a kind retypes and relinks the tile's pile (no game map
//   has one).
// All thiscall (ECX = world).

#include "game/debug_log.h"
#include "game/enums/buildings.h"
#include "game/enums/job_ids.h"
#include "game/enums/objects.h"
#include "game/enums/races.h"
#include "game/enums/settler_types.h"
#include "game/enums/terrain.h"
#include "game/fileformats/map_file.h"
#include "game/globals.h"
#include "game/replacements.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/ui/game_settings_layout.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <cstring>
#include <iterator>

namespace s3re::game {

namespace {

// Marker for the water next to the shore while the depth bands are made
// (a marker byte, so hex).
constexpr std::uint32_t kShoreWaterMark = 0xff;
constexpr std::uint32_t kShallowWater = 0;  // water along the shore, and every water before the bands
constexpr std::uint32_t kDeepestBand = 6;   // the last band ReplaceTerrainNextTo makes
constexpr std::uint32_t kDeepWater = 7;     // everything further out

}  // namespace

using PrepareTerrainFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx);

// Purpose: After a map's landscape is loaded, turns all water into plain water and then gives it depth
// levels by its distance from the shore (shallow along the coast, deep further out).
std::uint32_t __fastcall CGameData_PrepareLoadedTerrain(CGameData* world, void*) {
    for (std::uint32_t x = 0; x < world->map_size; ++x) {
        for (std::uint32_t y = 0; y < world->map_size; ++y) {
            // The original walks y in the outer loop; the writes are independent.
            std::uint8_t& terrain = world->tiles[x][y].terrain;
            if ((terrain & kTerrainClassMask) == kTerrainClassWater) {
                terrain = 0;
            }
        }
    }
    CGameData_ReplaceTerrainNextTo(world, nullptr, kShallowWater, kTerrainShore, kShoreWaterMark);
    CGameData_ReplaceTerrainNextTo(world, nullptr, kShallowWater, kShoreWaterMark, 1);
    for (std::uint32_t band = 1; band < kDeepestBand; ++band) {
        CGameData_ReplaceTerrainNextTo(world, nullptr, kShallowWater, band, band + 1);
    }
    CGameData_ReplaceTerrainType(world, nullptr, kShallowWater, kDeepWater);
    return CGameData_ReplaceTerrainType(world, nullptr, kShoreWaterMark, kShallowWater);
}

S3RE_HOOK(0x00519540, CGameData_PrepareLoadedTerrain, PrepareTerrainFn, Original_CGameData_PrepareLoadedTerrain,
          CGameData_PrepareLoadedTerrain);

// ---------------------------------------------------------------------------
// CGameData::LoadMap (0x004fd540)
// ---------------------------------------------------------------------------

namespace {

using fileformats::CMapFile;
using layout::Building;
using layout::Pile;
using layout::Player;
using layout::Settler;
using layout::Tile;

// The callees, at their S3.EXE addresses (all replaced and verified except
// GameSettings::PlayerSetup, which stays the original: UI-side Watcom vector
// code behind a mutex).
constexpr std::uintptr_t kGetChunk = 0x004a60f0;             // CMapFile, (type, sub) -> buffer or 0
constexpr std::uintptr_t kReleaseChunk = 0x004a6310;         // CMapFile, (type, sub)
constexpr std::uintptr_t kPlayerSetup = 0x004351e0;          // GameSettings, (index) -> GamePlayerSetup*
constexpr std::uintptr_t kPlayerColour = 0x0049fec0;         // GamePlayerSetup, () -> colour index
constexpr std::uintptr_t kBuildingDone = 0x004dbc50;         // (building)
constexpr std::uintptr_t kAddBuildingSite = 0x004dd280;      // (x, y, player, type) -> building
constexpr std::uintptr_t kUnblockCastleTiles = 0x004dfa60;   // ()
constexpr std::uintptr_t kCreatePile = 0x00504420;           // (x, y, good, amount, kind)
constexpr std::uintptr_t kAddPlayerTowns = 0x005074b0;       // ()
constexpr std::uintptr_t kCreateSettler = 0x0050c9e0;        // (x, y, type, player) -> settler
constexpr std::uintptr_t kEnterBuildingSlot = 0x0050ccb0;    // (settler)
constexpr std::uintptr_t kSpawnSettlersOnWater = 0x0050dc10; // (count, type, player, x, y)
constexpr std::uintptr_t kCreateSevenTileUnit = 0x0050dd20;  // (type, player, x, y)
constexpr std::uintptr_t kPrepareLoadedTerrain = 0x00519540; // ()
constexpr std::uintptr_t kBuildRegionsAndAreas = 0x0051a580; // ()

// thiscall with dword arguments.
template <std::uintptr_t Address, typename Self, typename... Args>
std::uint32_t Call(Self* self, Args... args) {
    using Fn = std::uint32_t(__fastcall*)(Self*, void*, decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(Address)(self, nullptr, static_cast<std::uint32_t>(args)...);
}

// The map chunks LoadMap reads (re/file_formats.md "Map chunks").
enum class MapChunk : std::uint32_t {
    Landscape = 6,
    Settlers = 7,
    Buildings = 8,
    Piles = 9,
    VictoryConditions = 10,
    NoWorldUpdate = 0x40,  // presence only
};

// A chunk: 8 header bytes, then (for the record chunks) a little-endian
// record count and the records, which LoadMap reads byte by byte.
constexpr std::uint32_t kChunkDataOffset = 8;
constexpr std::uint32_t kChunkRecordsOffset = 12;
constexpr std::uint32_t kLandscapeRecordBytes = 6;
constexpr std::uint32_t kBuildingRecordBytes = 12;
constexpr std::uint32_t kSettlerRecordBytes = 6;
constexpr std::uint32_t kPileRecordBytes = 8;

const std::uint8_t* GetChunk(CMapFile* file, MapChunk type) {
    return reinterpret_cast<const std::uint8_t*>(Call<kGetChunk>(file, Value(type), 0));
}

void ReleaseChunk(CMapFile* file, MapChunk type) {
    Call<kReleaseChunk>(file, Value(type), 0);
}

std::uint32_t Word16(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) | static_cast<std::uint32_t>(bytes[1]) << 8;
}

std::uint32_t Word32(const std::uint8_t* bytes) {
    return Word16(bytes) | Word16(bytes + 2) << 16;
}

// The record count of a record chunk (+8).
std::uint32_t RecordCount(const std::uint8_t* chunk) {
    return Word32(chunk + kChunkDataOffset);
}

// The map's owner slots and the game's players: slotToPlayer[slot] is the
// player of map slot `slot`, or kNoPlayer. CLogicThread::OnStart passes a
// 20-entry array; a slot byte above 19 reads past it, as in the original
// (no map of the game has one, except one landscape tile with slot 191).
constexpr std::uint32_t kNoPlayer = 0xff;
constexpr std::uint8_t kNoOwner = 0xff;

Player& PlayerAt(CGameData* world, std::uint32_t n) { return *(world->players + n); }
Settler& SettlerAt(CGameData* world, std::uint32_t n) { return *(world->settlers + n); }
Building& BuildingAt(CGameData* world, std::uint32_t n) { return *(world->buildings + n); }
Pile& PileAt(CGameData* world, std::uint32_t n) { return *(world->piles + n); }
layout::Area& AreaAt(CGameData* world, std::uint32_t n) { return *(world->areas + n); }
Tile& TileAt(CGameData* world, std::uint32_t x, std::uint32_t y) { return TileAtOffset(world, TileOffset(x, y)); }

// Landscape objects of the map editor that stand in water (terrain 0..7)
// become 0x0d..0x10 (BuildRegionsAndAreas blocks the tiles around those).
constexpr std::uint8_t kLastWaterTerrain = 7;
constexpr std::uint8_t kEditorWaterObjectFirst = 0x6f;
constexpr std::uint8_t kEditorWaterObjectLast = 0x72;
constexpr std::uint8_t kEditorWaterObjectShift = 0x62;

// 0x4fd56e..0x4fd6c9: chunk 6, the landscape: the map size, then one
// 6-byte record per tile (height, terrain, object, owner slot, flags,
// resources), y in the outer loop (both bounds re-read every step).
void LoadLandscape(CGameData* world, const std::uint8_t* chunk, const std::uint32_t* slotToPlayer) {
    GameDebugLog(texts::kLoadingMap);
    world->map_size = Word32(chunk + kChunkDataOffset);
    GameDebugLog(texts::kWorldsize, world->map_size);
    const std::uint8_t* record = chunk + kChunkRecordsOffset;
    for (std::uint32_t y = 0; y < world->map_size; ++y) {
        for (std::uint32_t x = 0; x < world->map_size; ++x, record += kLandscapeRecordBytes) {
            Tile& tile = world->tiles[x][y];
            tile.height = record[0];
            tile.terrain = record[1];
            tile.object = record[2];
            tile.seen_object = record[2];
            const std::uint8_t slot = record[3];
            // A slot that no player took: no owner, and the tile is marked
            // (search_mark 1) so that the piles chunk leaves it alone.
            tile.search_mark = 0;
            if (slot == kNoOwner) {
                tile.owner = kNoOwner;
            } else if (slotToPlayer[slot] == kNoPlayer) {
                tile.owner = kNoOwner;
                tile.search_mark = 1;
            } else {
                tile.owner = static_cast<std::uint8_t>(slotToPlayer[slot]);
            }
            tile.flags = record[4];
            if (tile.owner != kNoOwner) {
                tile.flags |= layout::kTileOwned;
            }
            tile.resources = record[5];
            tile.draw_flags = 0;
            tile.settler = 0;
            tile.object_number = 0;
            tile.walk_region = 0;
            tile.area = 0;
            tile.traffic = 0;
            if (tile.terrain <= kLastWaterTerrain && tile.object >= kEditorWaterObjectFirst &&
                tile.object <= kEditorWaterObjectLast) {
                tile.object = static_cast<std::uint8_t>(tile.object - kEditorWaterObjectShift);
            }
        }
    }
}

// Building layout markers (building_sites.cpp, player_towns.cpp).
constexpr std::uint32_t kMarkFootprint = 0x80000000;
constexpr std::uint32_t kMarkNoStakes = 0x80000001;
constexpr std::uint32_t kMarkBorder = 0x80000002;
constexpr std::uint32_t kMarkEnd = 0x80000003;

// The tile (x + dx, y + dy) of a layout entry, in the original's 32-bit
// arithmetic.
Tile& LayoutTile(CGameData* world, std::uint32_t x, std::uint32_t y, const std::uint32_t* entry) {
    return TileAt(world, x + entry[0], y + entry[1]);
}

// 0x4fd6ee..0x4fd879: chunk 8, first pass ("Deleting Building-Bits"): the
// buildings of map slots that no player took leave no trace in the
// landscape: their tile's object is cleared, and the footprint flag (and on
// the footprint proper also the blocked flag) of their layout's tiles. The
// layout is the one of the race of *player number* `slot` (Player +4), not
// of a mapped player.
void DeleteUnusedBuildingBits(CGameData* world, const std::uint8_t* chunk, const std::uint32_t* slotToPlayer) {
    GameDebugLog(texts::kDeletingBuildingBits);
    const std::uint32_t count = RecordCount(chunk);
    const std::uint8_t* record = chunk + kChunkRecordsOffset;
    for (std::uint32_t i = 0; i < count; ++i, record += kBuildingRecordBytes) {
        const std::uint8_t slot = record[0];
        if (slotToPlayer[slot] != kNoPlayer) {
            continue;
        }
        const std::uint32_t type = record[1];
        const std::uint32_t x = Word16(record + 2);
        const std::uint32_t y = Word16(record + 4);
        const std::uint32_t race = static_cast<std::uint32_t>(PlayerAt(world, slot).race);
        TileAt(world, x, y).object = kObjectNone;
        // The footprint starts after the first marker 0x80000000.
        const std::uint32_t* entry = g_buildingLayouts[race * kBuildingTypeCount + type];
        while (*entry != kMarkFootprint) {
            ++entry;
        }
        ++entry;
        for (; *entry != kMarkNoStakes; entry += 2) {
            LayoutTile(world, x, y, entry).flags &= static_cast<std::uint8_t>(~layout::kTileBuildingFootprint);
            LayoutTile(world, x, y, entry).flags &= static_cast<std::uint8_t>(~layout::kTileBlocked);
        }
        // The border (up to 0x80000003; a 0x80000002 where a pair would start
        // is skipped, and the pair after it is read without checking for the
        // end, as in PlaceTown).
        ++entry;
        while (*entry != kMarkEnd) {
            if (*entry == kMarkBorder) {
                ++entry;
            }
            LayoutTile(world, x, y, entry).flags &= static_cast<std::uint8_t>(~layout::kTileBuildingFootprint);
            entry += 2;
        }
    }
}

// The soldiers a map can put into a tower (chunk 8, mode 1): per kind a
// count nibble of the record, the settler type, its job and the building's
// counter byte (kind_bytes +0x1a / +0x1c / +0x20). Swordsmen are chained
// into the garrison, bowmen go to a slot of the building
// (EnterBuildingSlot), pikemen are only counted.
enum class GarrisonKind { Swordsmen, Bowmen, Pikemen };

struct GarrisonGroup {
    std::uint32_t recordByte;
    bool highNibble;
    SettlerType type;
    GarrisonKind kind;
};

// 0x4fdaae..0x4fe117, in the original's order.
constexpr GarrisonGroup kGarrisonGroups[] = {
    {7, true, SettlerType::Swordsman1, GarrisonKind::Swordsmen},
    {7, false, SettlerType::Swordsman2, GarrisonKind::Swordsmen},
    {9, true, SettlerType::Swordsman3, GarrisonKind::Swordsmen},
    {9, false, SettlerType::Bowman1, GarrisonKind::Bowmen},
    {8, true, SettlerType::Bowman2, GarrisonKind::Bowmen},
    {8, false, SettlerType::Bowman3, GarrisonKind::Bowmen},
    {11, true, SettlerType::Pikeman1, GarrisonKind::Pikemen},
    {11, false, SettlerType::Pikeman2, GarrisonKind::Pikemen},
    {10, true, SettlerType::Pikeman3, GarrisonKind::Pikemen},
};

// Record byte 6: how the tower is manned.
constexpr std::uint8_t kGarrisonDefault = 0;  // one Swordsman1
constexpr std::uint8_t kGarrisonListed = 1;   // the counts of bytes 7..11

// Kind byte offsets (Building +0x19 + n): +0x1a swordsmen, +0x1c bowmen,
// +0x20 pikemen.
constexpr std::uint32_t kSwordsmenCount = 1;
constexpr std::uint32_t kBowmenCount = 3;
constexpr std::uint32_t kPikemenCount = 7;

// A new soldier of the tower: created at its door, the tile does not keep
// it (it is inside), and it belongs to the building.
std::uint32_t CreateTowerSoldier(CGameData* world, std::uint32_t buildingNumber, SettlerType type,
                                 std::uint32_t player, SettlerJob job, bool chain) {
    Building& building = BuildingAt(world, buildingNumber);
    const std::uint32_t soldierNumber =
        Call<kCreateSettler>(world, building.door_x, building.door_y, Value(type), player);
    Settler& soldier = SettlerAt(world, soldierNumber);
    if (chain) {
        soldier.next = building.garrison;
        building.garrison = static_cast<std::uint16_t>(soldierNumber);
    } else {
        // 0x4fda4f: the single default swordsman (the garrison word first).
        building.garrison = static_cast<std::uint16_t>(soldierNumber);
        soldier.next = 0;
    }
    soldier.building = static_cast<std::uint16_t>(buildingNumber);
    soldier.job = job;
    TileAt(world, soldier.x, soldier.y).settler = 0;
    soldier.counter = 0;
    return soldierNumber;
}

// 0x4fda08..0x4fe117: a tower's (small, large, castle) garrison.
void ManTower(CGameData* world, std::uint32_t buildingNumber, const std::uint8_t* record, std::uint32_t player) {
    Building& building = BuildingAt(world, buildingNumber);
    const std::uint8_t mode = record[6];
    if (mode == kGarrisonDefault) {
        CreateTowerSoldier(world, buildingNumber, SettlerType::Swordsman1, player, SettlerJob::SwordsmanInTower,
                           false);
        return;
    }
    if (mode != kGarrisonListed) {
        return;
    }
    building.kind_bytes[kSwordsmenCount] = 0;
    building.kind_bytes[kBowmenCount] = 0;
    building.kind_bytes[kPikemenCount] = 0;
    for (const GarrisonGroup& group : kGarrisonGroups) {
        const std::uint8_t byte = record[group.recordByte];
        const std::uint32_t count = group.highNibble ? byte >> 4 : byte & 0x0f;
        for (std::uint32_t n = 0; n < count; ++n) {
            switch (group.kind) {
            case GarrisonKind::Swordsmen:
                CreateTowerSoldier(world, buildingNumber, group.type, player, SettlerJob::SwordsmanInTower, true);
                ++building.kind_bytes[kSwordsmenCount];
                break;
            case GarrisonKind::Bowmen: {
                const std::uint32_t bowman =
                    CreateTowerSoldier(world, buildingNumber, group.type, player, SettlerJob::BowmanInTower, true);
                Call<kEnterBuildingSlot>(world, bowman);
                ++building.kind_bytes[kBowmenCount];
                break;
            }
            case GarrisonKind::Pikemen:
                CreateTowerSoldier(world, buildingNumber, group.type, player, SettlerJob::PikemanInTower, true);
                ++building.kind_bytes[kPikemenCount];
                break;
            }
        }
    }
}

bool IsTower(std::uint32_t type) {
    return type == Value(BuildingType::SmallTower) || type == Value(BuildingType::LargeTower) ||
           type == Value(BuildingType::Castle);
}

// 0x4fd8ba..0x4fe137: chunk 8, the buildings: each one of a taken slot is
// built as a finished building of the slot's player; towers get their
// garrison.
void LoadBuildings(CGameData* world, const std::uint8_t* chunk, const std::uint32_t* slotToPlayer) {
    GameDebugLog(texts::kLoadingBuildings);
    const std::uint32_t count = RecordCount(chunk);
    GameDebugLog(texts::kAmountOfBuildings, count);
    const std::uint8_t* record = chunk + kChunkRecordsOffset;
    for (std::uint32_t i = 0; i < count; ++i, record += kBuildingRecordBytes) {
        const std::uint32_t slot = record[0];
        const std::uint32_t type = record[1];
        const std::uint32_t x = Word16(record + 2);
        const std::uint32_t y = Word16(record + 4);
        GameDebugLog(texts::kBuildingTypeAt, type, x, y, slot, slotToPlayer[slot]);
        const std::uint32_t player = slotToPlayer[slot];
        if (player == kNoPlayer) {
            continue;
        }
        const std::uint32_t buildingNumber = Call<kAddBuildingSite>(world, x, y, player, type);
        BuildingAt(world, buildingNumber).buildup = 0;
        Call<kBuildingDone>(world, buildingNumber);
        if (IsTower(type)) {
            ManTower(world, buildingNumber, record, player);
        }
    }
}

// Settler classes whose settlers count towards the population base
// (g_settlerTypeClass: 0 civilians, 10..29 other non-military classes).
constexpr std::int32_t kCivilianClass = 0;
constexpr std::int32_t kFirstWorkerClass = 10;
constexpr std::int32_t kLastWorkerClass = 29;

// 0x4fe15e..0x4fe27b: chunk 7, the settlers of taken slots. Catapults
// (seven tiles) and ships (on water) are made by their own functions,
// which get the *map slot* as the player, not the mapped player (as in the
// original; the game's maps give their slots in player order).
void LoadSettlers(CGameData* world, const std::uint8_t* chunk, const std::uint32_t* slotToPlayer) {
    GameDebugLog(texts::kLoadingSettlers);
    const std::uint32_t count = RecordCount(chunk);
    GameDebugLog(texts::kAmountOfSettlers, count);
    const std::uint8_t* record = chunk + kChunkRecordsOffset;
    for (std::uint32_t i = 0; i < count; ++i, record += kSettlerRecordBytes) {
        const std::uint32_t slot = record[0];
        const std::uint32_t type = record[1];
        const std::uint32_t x = Word16(record + 2);
        const std::uint32_t y = Word16(record + 4);
        const std::uint32_t player = slotToPlayer[slot];
        if (player == kNoPlayer) {
            continue;
        }
        if (type == Value(SettlerType::Catapult)) {
            Call<kCreateSevenTileUnit>(world, type, slot, x, y);
        } else if (type == Value(SettlerType::TradeShip) || type == Value(SettlerType::Ferry)) {
            Call<kSpawnSettlersOnWater>(world, 1, type, slot, x, y);
        } else {
            const std::uint32_t settlerNumber = Call<kCreateSettler>(world, x, y, type, player);
            Settler& settler = SettlerAt(world, settlerNumber);
            settler.counter = 0;
            if (settlerNumber == 0) {
                continue;
            }
            // The class table has 0x30 entries; a bigger type reads past it, as in the original.
            const std::int32_t settlerClass = (&g_settlerTypeClass[0])[type];
            if (settlerClass == kCivilianClass ||
                (settlerClass >= kFirstWorkerClass && settlerClass <= kLastWorkerClass)) {
                ++PlayerAt(world, settler.owner).population_limit_base;
            }
        }
    }
}

// 0x4fe36e..0x4fe4a7, a pile record without a kind: the pile on the tile
// gets the record's goods and amount and moves from the area's pile list to
// the head of the area's list for its goods. No map of the game has such a
// record.
void RetypeTilePile(CGameData* world, std::uint32_t x, std::uint32_t y, std::uint8_t good, std::uint8_t amount) {
    const Tile& tile = TileAt(world, x, y);
    const std::uint32_t area = tile.area;
    const std::uint16_t pileNumber = tile.object_number;
    if (amount == 0) {
        return;
    }
    Pile& pile = PileAt(world, pileNumber);
    if (pile.prev != 0) {
        PileAt(world, pile.prev).next = pile.next;
    } else {
        AreaAt(world, area).pile_list = pile.next;
    }
    if (pile.next != 0) {
        PileAt(world, pile.next).prev = pile.prev;
    }
    pile.good = good;
    pile.amount = amount;
    pile.art = 0;
    // The goods index is not bounds-checked (a byte), as in the original.
    std::uint16_t& head = (&AreaAt(world, area).piles_by_goods[0])[pile.good];
    pile.next = head;
    if (pile.next != 0) {
        PileAt(world, pile.next).prev = pileNumber;
    }
    pile.prev = 0;
    head = pileNumber;
}

// 0x4fe2b3..0x4fe4bd: chunk 9, the piles: 8-byte records (x, y, goods,
// amount, kind); none on tiles of untaken slots (search_mark from the
// landscape). (The original also turns the goods of a record with amount
// and kind 0 into 0xff, which nothing reads: such a record is skipped.)
void LoadPiles(CGameData* world, const std::uint8_t* chunk) {
    GameDebugLog(texts::kLoadingPiles);
    const std::uint32_t count = RecordCount(chunk);
    GameDebugLog(texts::kAmountOfPiles, count);
    const std::uint8_t* record = chunk + kChunkRecordsOffset;
    for (std::uint32_t i = 0; i < count; ++i, record += kPileRecordBytes) {
        const std::uint32_t x = Word16(record);
        const std::uint32_t y = Word16(record + 2);
        const std::uint8_t good = record[4];
        const std::uint8_t amount = record[5];
        const std::uint8_t kind = record[6];
        if (TileAt(world, x, y).search_mark != 0) {
            continue;
        }
        if (kind != 0) {
            Call<kCreatePile>(world, x, y, good, amount, kind);
        } else {
            RetypeTilePile(world, x, y, good, amount);
        }
    }
}

// 0x4fe4d2..0x4fe51f: chunk 10, the victory conditions.
void LoadVictoryConditions(CGameData* world, CMapFile* file) {
    const std::uint8_t* chunk = GetChunk(file, MapChunk::VictoryConditions);
    if (chunk == nullptr) {
        return;
    }
    std::memcpy(&world->victory_conditions, chunk + kChunkDataOffset, sizeof(world->victory_conditions));
    if (world->victory_conditions.protect_players != 0) {
        g_victoryProtectActive = 1;
    }
    ReleaseChunk(file, MapChunk::VictoryConditions);
}

// 0x4fe550..0x4fe576: the players' colours from the game setup.
constexpr std::uint32_t kColourEntries = 20;

void TakePlayerColours() {
    GameSettings* settings = &g_gameSettings.get();
    for (std::uint32_t i = 0; i < kColourEntries; ++i) {
        auto* setup = reinterpret_cast<GamePlayerSetup*>(Call<kPlayerSetup>(settings, i));
        g_playerColourIndex[i] = static_cast<std::uint16_t>(Call<kPlayerColour>(setup));
    }
}

// 0x4fe578..0x4fe5b4: the landscape's marks of untaken slots are cleared.
void ClearSearchMarks(CGameData* world) {
    for (std::uint32_t x = 0; x < world->map_size; ++x) {
        for (std::uint32_t y = 0; y < world->map_size; ++y) {
            world->tiles[x][y].search_mark = 0;
        }
    }
}

// ----- Campaign missions (g_campaignMission, 0x4fe5bb: a switch over 49..70)

constexpr std::int32_t kMissionHalvedResources = 49;
constexpr std::int32_t kMissionFerries = 50;
constexpr std::int32_t kMissionMana30 = 53;
constexpr std::int32_t kMissionBurningTrees = 59;
constexpr std::int32_t kMissionMana60 = 61;
constexpr std::int32_t kMissionAmazonTowers = 70;

// 0x4fe5dc..0x4fe621, mission 49: every resource amount (tile +0x11, low
// nibble) is halved; only the first 250 tiles of each column.
constexpr std::uint32_t kHalvedRows = 250;

void HalveResources(CGameData* world) {
    for (std::uint32_t x = 0; x < world->map_size; ++x) {
        for (std::uint32_t y = 0; y < kHalvedRows; ++y) {
            std::uint8_t& resources = world->tiles[x][y].resources;
            const std::uint8_t kind = resources & 0xf0;
            if (kind != 0) {
                resources = static_cast<std::uint8_t>(kind + ((resources >> 1) & 7));
            }
        }
    }
}

// 0x4fe62e..0x4fe7c2, mission 50: four ferries of player 0, the first three
// with eight pioneers each, the last with eight Swordsman1, all on board.
struct TilePoint {
    std::uint32_t x, y;
};
constexpr TilePoint kFerries[] = {{60, 510}, {70, 520}, {80, 530}, {90, 540}};
constexpr TilePoint kPassengersCreatedAt = {60, 530};
constexpr std::uint32_t kPassengersPerFerry = 8;
constexpr std::uint32_t kPioneerFerries = 3;
constexpr std::uint32_t kMissionPlayer = 0;
constexpr std::uint32_t kFerryDirection = 3;

void LoadFerries(CGameData* world) {
    std::uint16_t ferries[4];
    for (std::uint32_t k = 0; k < 4; ++k) {
        ferries[k] = static_cast<std::uint16_t>(
            Call<kCreateSettler>(world, kFerries[k].x, kFerries[k].y, Value(SettlerType::Ferry), kMissionPlayer));
    }
    for (std::uint32_t k = 0; k < 4; ++k) {
        Settler& ferry = SettlerAt(world, ferries[k]);
        ferry.passengers = 0;
        ferry.saved_animation = 0;
        ferry.saved_counter = 0;
        ferry.cargo_amounts01 = 0;
        ferry.cargo_amounts23 = 0;
        ferry.job = SettlerJob::New;
        ferry.direction = kFerryDirection;
        for (std::uint32_t n = 0; n < kPassengersPerFerry; ++n) {
            std::uint32_t passengerNumber;
            if (k < kPioneerFerries) {
                passengerNumber = Call<kCreateSettler>(world, kPassengersCreatedAt.x, kPassengersCreatedAt.y,
                                                       Value(SettlerType::Pioneer), kMissionPlayer);
                ++PlayerAt(world, SettlerAt(world, passengerNumber).owner).population_limit_base;
            } else {
                passengerNumber = Call<kCreateSettler>(world, kPassengersCreatedAt.x, kPassengersCreatedAt.y,
                                                       Value(SettlerType::Swordsman1), kMissionPlayer);
            }
            ++ferry.passengers;
            Settler& passenger = SettlerAt(world, passengerNumber);
            passenger.next = ferry.next;
            ferry.next = static_cast<std::uint16_t>(passengerNumber);
            TileAt(world, passenger.x, passenger.y).settler = 0;
            passenger.walk_state = 0;
            passenger.job = SettlerJob::OnBoard;
            passenger.counter = 0;
        }
    }
}

// 0x4fe7ec..0x4fe899, mission 59: near each of four points the first tree
// among the 21 nearest tiles catches fire (16-bit coordinates).
constexpr TilePoint kFirePoints[] = {{313, 64}, {351, 113}, {392, 152}, {535, 277}};
constexpr std::uint32_t kFireSearchEntries = 21;
constexpr std::uint8_t kFireFirstTree = kObjectFirstTree;
constexpr std::uint8_t kFireLastTree = 0x51;

void SetTreesOnFire(CGameData* world) {
    for (const TilePoint& point : kFirePoints) {
        for (std::uint32_t i = 0; i < kFireSearchEntries; ++i) {
            const layout::MapOffset& offset = world->offsets[i];
            const auto x = static_cast<std::uint16_t>(point.x + static_cast<std::uint32_t>(offset.dx));
            const auto y = static_cast<std::uint16_t>(point.y + static_cast<std::uint32_t>(offset.dy));
            std::uint8_t& object = TileAt(world, x, y).object;
            if (object >= kFireFirstTree && object <= kFireLastTree) {
                object = kObjectFireStart;
                break;
            }
        }
    }
}

// 0x4fe8b2..0x4feaf3, mission 70: 17 towers given by position and type
// become Amazon buildings ("xxx race changed"); a position without a
// building is reported ("NOT changed"), one with another type is skipped.
struct MissionTower {
    std::uint32_t x, y;
    BuildingType type;
};
constexpr MissionTower kAmazonTowers[] = {
    {266, 533, BuildingType::LargeTower}, {276, 524, BuildingType::LargeTower},
    {293, 512, BuildingType::LargeTower}, {312, 511, BuildingType::LargeTower},
    {323, 510, BuildingType::LargeTower}, {342, 515, BuildingType::SmallTower},
    {360, 528, BuildingType::LargeTower}, {377, 532, BuildingType::LargeTower},
    {404, 555, BuildingType::LargeTower}, {372, 544, BuildingType::LargeTower},
    {365, 586, BuildingType::LargeTower}, {348, 594, BuildingType::LargeTower},
    {326, 596, BuildingType::LargeTower}, {305, 591, BuildingType::LargeTower},
    {297, 578, BuildingType::LargeTower}, {331, 570, BuildingType::SmallTower},
    {429, 542, BuildingType::LargeTower},
};

void MakeTowersAmazon(CGameData* world) {
    for (std::uint32_t k = 0; k < std::size(kAmazonTowers); ++k) {
        const MissionTower& tower = kAmazonTowers[k];
        const std::uint32_t type = Value(tower.type);
        const Tile& tile = TileAt(world, tower.x, tower.y);
        if (tile.object_number == 0 || (tile.flags & layout::kTileHasBuilding) == 0) {
            GameDebugLog(texts::kRaceNotChanged, k, type);
            continue;
        }
        Building& building = BuildingAt(world, tile.object_number);
        if (building.art == type) {
            building.race = static_cast<std::uint8_t>(Race::Amazon);
            GameDebugLog(texts::kRaceChanged, k, type);
        }
    }
}

// 0x4fe5bb..0x4feaf5.
void ApplyMissionFixUps(CGameData* world) {
    switch (g_campaignMission) {
    case kMissionHalvedResources:
        HalveResources(world);
        break;
    case kMissionFerries:
        LoadFerries(world);
        break;
    case kMissionMana30:
        PlayerAt(world, kMissionPlayer).mana = 30;
        break;
    case kMissionBurningTrees:
        SetTreesOnFire(world);
        PlayerAt(world, kMissionPlayer).mana = 60;
        break;
    case kMissionMana60:
        PlayerAt(world, kMissionPlayer).mana = 60;
        break;
    case kMissionAmazonTowers:
        MakeTowersAmazon(world);
        break;
    default:
        break;
    }
}

}  // namespace

// Purpose: builds the world of a new game from a map file: the landscape,
// the map's buildings, settlers and piles for the players that take part
// (the start towns when the map has no buildings), its victory conditions,
// the players' colours and the fix-ups of some campaign missions.
void __fastcall CGameData_LoadMap(CGameData* world, void* /*edx*/, CMapFile* file, const std::uint32_t* slotToPlayer) {
    // 0x4fd558..0x4fd8a1: the landscape, the buildings of untaken slots
    // removed from it, then the regions and areas.
    if (const std::uint8_t* landscape = GetChunk(file, MapChunk::Landscape)) {
        LoadLandscape(world, landscape, slotToPlayer);
        ReleaseChunk(file, MapChunk::Landscape);
        if (const std::uint8_t* buildings = GetChunk(file, MapChunk::Buildings)) {
            DeleteUnusedBuildingBits(world, buildings, slotToPlayer);
            ReleaseChunk(file, MapChunk::Buildings);
        }
        GameDebugLog(texts::kSettingAllSectors);
        Call<kPrepareLoadedTerrain>(world);
        Call<kBuildRegionsAndAreas>(world);
    }

    // 0x4fd8a1..0x4fe294: buildings and settlers, or the start towns.
    if (const std::uint8_t* buildings = GetChunk(file, MapChunk::Buildings)) {
        LoadBuildings(world, buildings, slotToPlayer);
        ReleaseChunk(file, MapChunk::Buildings);
        if (const std::uint8_t* settlers = GetChunk(file, MapChunk::Settlers)) {
            LoadSettlers(world, settlers, slotToPlayer);
            ReleaseChunk(file, MapChunk::Settlers);
        }
    } else {
        Call<kAddPlayerTowns>(world);
    }

    if (const std::uint8_t* piles = GetChunk(file, MapChunk::Piles)) {
        LoadPiles(world, piles);
        ReleaseChunk(file, MapChunk::Piles);
    }
    LoadVictoryConditions(world, file);

    // 0x4fe51f..0x4fe550: chunk 0x40 only says "no world updates".
    if (GetChunk(file, MapChunk::NoWorldUpdate) != nullptr) {
        g_worldUpdateDisabled = 1;
        ReleaseChunk(file, MapChunk::NoWorldUpdate);
    } else {
        g_worldUpdateDisabled = 0;
    }

    TakePlayerColours();
    ClearSearchMarks(world);
    Call<kUnblockCastleTiles>(world);
    ApplyMissionFixUps(world);
}

using LoadMapFn = void(__fastcall*)(CGameData* world, void* edx, CMapFile* file, const std::uint32_t* slotToPlayer);
S3RE_HOOK(0x004fd540, CGameData_LoadMap, LoadMapFn, Original_CGameData_LoadMap, CGameData_LoadMap);


// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// Maps of 0..80 tiles (sometimes 384) of land, shore and water patches; the
// whole world is compared.
void ComparePrepareTerrain(test::Context& ctx, PrepareTerrainFn candidate, int cases) {
    test::WorldPair worlds(ctx, sizeof(CGameData));
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = ctx.rng().Chance(1, 10)   ? ctx.rng().Below(4)
                                   : ctx.rng().Chance(1, 10) ? 384
                                                             : 8 + ctx.rng().Below(72);
        const std::uint32_t patch = 2 + ctx.rng().Below(8);
        for (std::uint32_t x = 0; x < size + 2; ++x) {
            for (std::uint32_t y = 0; y < size + 2; ++y) {
                const std::uint32_t kind = ((x / patch) * 7 + (y / patch) * 3) % 5;
                std::uint8_t terrain;
                if (ctx.rng().Chance(1, 20)) {
                    terrain = static_cast<std::uint8_t>(ctx.rng().Next32());
                } else if (kind < 2) {
                    terrain = static_cast<std::uint8_t>(ctx.rng().Below(16));  // water variants
                } else if (kind == 2) {
                    terrain = kTerrainShore;
                } else {
                    terrain = static_cast<std::uint8_t>(0x10 + ctx.rng().Below(0x30));
                }
                worlds.Both([&](CGameData* w) { w->tiles[x][y].terrain = terrain; });
            }
        }
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        const std::uint32_t expected = Original_CGameData_PrepareLoadedTerrain(worlds.original(), nullptr);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr);
        if (!S3RE_EXPECT_EQ(expected, actual) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (size %u)", i, size);
            return;
        }
    }
}

// Wrong: never makes band 6 (that water becomes 7).
std::uint32_t __fastcall WrongPrepareLoadedTerrain(CGameData* world, void* edx) {
    for (std::uint32_t x = 0; x < world->map_size; ++x) {
        for (std::uint32_t y = 0; y < world->map_size; ++y) {
            std::uint8_t& terrain = world->tiles[x][y].terrain;
            if ((terrain & kTerrainClassMask) == kTerrainClassWater) {
                terrain = 0;
            }
        }
    }
    CGameData_ReplaceTerrainNextTo(world, edx, 0, kTerrainShore, kShoreWaterMark);
    CGameData_ReplaceTerrainNextTo(world, edx, 0, kShoreWaterMark, 1);
    for (std::uint32_t band = 1; band < kDeepestBand - 1; ++band) {
        CGameData_ReplaceTerrainNextTo(world, edx, 0, band, band + 1);
    }
    CGameData_ReplaceTerrainType(world, edx, 0, kDeepWater);
    return CGameData_ReplaceTerrainType(world, edx, kShoreWaterMark, 0);
}

}  // namespace

S3RE_TEST(CGameData_PrepareLoadedTerrain_MatchesOriginal) {
    ComparePrepareTerrain(ctx, &CGameData_PrepareLoadedTerrain, 150);
}
S3RE_TEST(CGameData_PrepareLoadedTerrain_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePrepareTerrain(probe, &WrongPrepareLoadedTerrain, 30);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
