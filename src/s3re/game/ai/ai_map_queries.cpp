// The computer players' map and world queries (computer players A2;
// Rainer\CompMapWorkerAgent.cpp and CompHouseReq.cpp; re/computer_players.md).
// They read the world through g_aiWorld (0x7e3408) or g_aiMapWorld
// (0x7e7b60) and check positions against the AI's map size words
// g_aiMapSizeX / g_aiMapSizeY (16-bit compares). Coordinates are passed
// as pointers to words unless noted.
//
// Several of them return only AX: off the map the original sets AX on top
// of whatever its caller left in EAX's upper half, which our C++ can't
// reproduce (the callers compare AX); on the map the upper half is
// address arithmetic, which we reproduce (some callers store all of EAX).
//
//   0x00599510 AI_BuildingAt(x, y) fastcall: the building on tile (x, y)
//              (Tile::object_number with kTileHasBuilding), else 0xffff;
//              AX = 0xffff off the map.
//   0x005995c0 AI_AreaAt(x, y) fastcall: Tile::area of (x, y); AX =
//              0xffff off the map. EAX's upper half: the tile's word index
//              of the area field (x * 0x300 + y + 0x44d9).
//   0x00599670 AI_BuildingRecordArea(building) fastcall: the same for the
//              door tile of a building record (+0xa / +0xc).
//   0x00599870 AI_WalkRegionAlongRow(x, y) fastcall, values (CX, DX): the
//              walk region of (x, y), or while it is 0 of x+1, x+2, ... (at
//              most 16 steps, while x+k < map size x - 1); AX = 0xffff off
//              the map. EAX's upper half: the last x * 0x300 + y.
//   0x00599910 AI_StoneAmountAt(x, y) fastcall, no bounds check: 0x7f -
//              object for stone objects 0x73..0x7e (1..12 stones), else 0;
//              the original subtracts a register whose upper half is the
//              address arithmetic (reproduced).
//   0x00599950 AI_IsTreeAt(x, y) fastcall, no bounds check: 1 for tree
//              objects 0x44..0x53, else 0.
//   0x00599990 AI_ResourceAmountAt(x, y) fastcall: Tile::resources & 0x0f;
//              AX = 0 off the map.
//   0x005999e0 AI_ResourceKindAt(x, y) fastcall: Tile::resources & 0xf0;
//              AX = 0xffff off the map.
//   0x0058ed30 AI_TerritoryRadius(type) fastcall (ECX = const u16*): for a
//              small tower, large tower or castle the distance of the
//              3500th / 4000th / 5000th nearest map offset (MapOffset::
//              distance, sorted by distance: the radius of that many
//              tiles), else of offset 0. AX only; EAX's upper half is the
//              world pointer's (reproduced; the caller pushes EAX).
//   0x0058ed70 AI_SiteKindOf(type) fastcall (CX): AL = the kind of site a
//              building type needs (table 0x58edb0 + jump table 0x58ed9c):
//              2 storehouse, 3 military, 4 mine, 5 field (vineyard, farm,
//              rice farm), else 1.
//   0x0058ecb0 AI_TakeBestCandidate(type) fastcall (CX): moves
//              g_aiPlayerData->best[type] to best[57 + type] (the candidate
//              taken for building) and clears it. EAX = g_aiPlayerData.
//   0x0059dbb0 AI_CountSitesInArea(player, area) fastcall (const u8*,
//              const u16*): construction sites (Building::buildup != 0) of
//              the player whose area (AI_BuildingArea) is `area`, walking
//              ids through AI_GetBuilding. AX = the count, the upper half
//              the error flag's (0).
//   0x0059dc70 AI_CountAreaSettlersOfType(type, area) fastcall (const u8*,
//              const u16*): the length of the area's settler list of that
//              type (Area::settlers_by_type, Settler::next).
//   0x00593140 AI_TeamHasActivePlayer(player) stdcall: 1 if some player
//              with Player::active == 1 is in the player's team (itself
//              included), else 0 (g_aiMapWorld).

#include "game/ai/ai_layout.h"
#include "game/enums.h"
#include "game/globals.h"
#include "game/layout.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using namespace ai;
using layout::kMapRowTiles;
using layout::kPlayers;
using layout::Tile;

namespace {

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kGetBuilding = 0x0058e170;   // AI_GetBuilding
constexpr std::uintptr_t kBuildingArea = 0x00599610;  // AI_BuildingArea

constexpr std::uint16_t kNone = 0xffff;
constexpr std::uint32_t kWalkSearchSteps = 16;

// Landscape objects (Tile::object, re/structs.md).
constexpr std::uint8_t kFirstTree = 0x44;
constexpr std::uint8_t kLastTree = 0x53;
constexpr std::uint8_t kFirstStone = 0x73;   // the largest stone (12 units)
constexpr std::uint8_t kLastStone = 0x7e;    // the smallest (1 unit)
constexpr std::uint32_t kStoneUnitsBase = 0x7f;  // units = 0x7f - object

constexpr std::uint8_t kResourceAmountMask = 0x0f;
constexpr std::uint8_t kResourceKindMask = 0xf0;

// The map offsets whose distance is the territory radius per military
// building (0x58ed30).
constexpr std::size_t kSmallTowerOffsets = 3500;
constexpr std::size_t kLargeTowerOffsets = 4000;
constexpr std::size_t kCastleOffsets = 5000;

// The kinds of site (0x58ed70).
enum class SiteKind : std::uint8_t {
    Ordinary = 1,
    Storehouse = 2,
    Military = 3,
    Mine = 4,
    Field = 5,
};

std::uint32_t TileIndex(std::uint16_t x, std::uint16_t y) { return x * kMapRowTiles + y; }

bool OnAiMap(std::uint16_t x, std::uint16_t y) { return x < g_aiMapSizeX && y < g_aiMapSizeY; }

const Tile& AiTile(std::uint16_t x, std::uint16_t y) { return g_aiWorld.get()->tiles[x][y]; }

// The upper half the original leaves in EAX after addressing a tile's
// area word through world + (index + 0x44d9) * 24.
std::uint32_t AreaAddressHigh(std::uint16_t x, std::uint16_t y) {
    constexpr std::uint32_t kAreaWordIndex =
        (offsetof(CGameData, tiles) + offsetof(Tile, area)) / sizeof(Tile);  // 0x44d9
    static_assert((offsetof(CGameData, tiles) + offsetof(Tile, area)) % sizeof(Tile) == 0);
    return (TileIndex(x, y) + kAreaWordIndex) & 0xffff0000u;
}

std::uint32_t AreaOfTile(std::uint16_t x, std::uint16_t y) {
    if (!OnAiMap(x, y)) {
        return kNone;  // AX only (the upper half is the caller's)
    }
    return AreaAddressHigh(x, y) | AiTile(x, y).area;
}

}  // namespace

using CoordFn = std::uint32_t(__fastcall*)(const std::uint16_t* x, const std::uint16_t* y);
using ValueCoordFn = std::uint32_t(__fastcall*)(std::uint32_t x, std::uint32_t y);
using BuildingFn = std::uint32_t(__fastcall*)(const layout::Building* building);
using TypePtrFn = std::uint32_t(__fastcall*)(const std::uint16_t* type);
using TypeFn = std::uint32_t(__fastcall*)(std::uint32_t type);
using CountFn = std::uint32_t(__fastcall*)(const std::uint8_t* key, const std::uint16_t* area);
using PlayerFn = std::uint32_t(__stdcall*)(std::uint32_t player);

std::uint32_t __fastcall AI_BuildingAt(const std::uint16_t* x, const std::uint16_t* y);
std::uint32_t __fastcall AI_AreaAt(const std::uint16_t* x, const std::uint16_t* y);
std::uint32_t __fastcall AI_BuildingRecordArea(const layout::Building* building);
std::uint32_t __fastcall AI_WalkRegionAlongRow(std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall AI_StoneAmountAt(const std::uint16_t* x, const std::uint16_t* y);
std::uint32_t __fastcall AI_IsTreeAt(const std::uint16_t* x, const std::uint16_t* y);
std::uint32_t __fastcall AI_ResourceAmountAt(const std::uint16_t* x, const std::uint16_t* y);
std::uint32_t __fastcall AI_ResourceKindAt(const std::uint16_t* x, const std::uint16_t* y);
std::uint32_t __fastcall AI_TerritoryRadius(const std::uint16_t* type);
std::uint32_t __fastcall AI_SiteKindOf(std::uint32_t type);
std::uint32_t __fastcall AI_TakeBestCandidate(std::uint32_t type);
std::uint32_t __fastcall AI_CountSitesInArea(const std::uint8_t* player, const std::uint16_t* area);
std::uint32_t __fastcall AI_CountAreaSettlersOfType(const std::uint8_t* type, const std::uint16_t* area);
std::uint32_t __stdcall AI_TeamHasActivePlayer(std::uint32_t player);

S3RE_HOOK(0x00599510, AI_BuildingAt, CoordFn, Original_AI_BuildingAt, AI_BuildingAt);
S3RE_HOOK(0x005995c0, AI_AreaAt, CoordFn, Original_AI_AreaAt, AI_AreaAt);
S3RE_HOOK(0x00599670, AI_BuildingRecordArea, BuildingFn, Original_AI_BuildingRecordArea, AI_BuildingRecordArea);
S3RE_HOOK(0x00599870, AI_WalkRegionAlongRow, ValueCoordFn, Original_AI_WalkRegionAlongRow, AI_WalkRegionAlongRow);
S3RE_HOOK(0x00599910, AI_StoneAmountAt, CoordFn, Original_AI_StoneAmountAt, AI_StoneAmountAt);
S3RE_HOOK(0x00599950, AI_IsTreeAt, CoordFn, Original_AI_IsTreeAt, AI_IsTreeAt);
S3RE_HOOK(0x00599990, AI_ResourceAmountAt, CoordFn, Original_AI_ResourceAmountAt, AI_ResourceAmountAt);
S3RE_HOOK(0x005999e0, AI_ResourceKindAt, CoordFn, Original_AI_ResourceKindAt, AI_ResourceKindAt);
S3RE_HOOK(0x0058ed30, AI_TerritoryRadius, TypePtrFn, Original_AI_TerritoryRadius, AI_TerritoryRadius);
S3RE_HOOK(0x0058ed70, AI_SiteKindOf, TypeFn, Original_AI_SiteKindOf, AI_SiteKindOf);
S3RE_HOOK(0x0058ecb0, AI_TakeBestCandidate, TypeFn, Original_AI_TakeBestCandidate, AI_TakeBestCandidate);
S3RE_HOOK(0x0059dbb0, AI_CountSitesInArea, CountFn, Original_AI_CountSitesInArea, AI_CountSitesInArea);
S3RE_HOOK(0x0059dc70, AI_CountAreaSettlersOfType, CountFn, Original_AI_CountAreaSettlersOfType,
          AI_CountAreaSettlersOfType);
S3RE_HOOK(0x00593140, AI_TeamHasActivePlayer, PlayerFn, Original_AI_TeamHasActivePlayer, AI_TeamHasActivePlayer);

// Purpose: The building standing on a map position, for the computer
// players (0xffff if none or off the map).
std::uint32_t __fastcall AI_BuildingAt(const std::uint16_t* x, const std::uint16_t* y) {
    if (!OnAiMap(*x, *y)) {
        return kNone;  // AX only
    }
    const Tile& tile = AiTile(*x, *y);
    if (tile.object_number != 0 && (tile.flags & layout::kTileHasBuilding) != 0) {
        return tile.object_number;
    }
    return kNone;
}

// Purpose: The economy area of a map position, for the computer players.
std::uint32_t __fastcall AI_AreaAt(const std::uint16_t* x, const std::uint16_t* y) {
    return AreaOfTile(*x, *y);
}

// Purpose: The economy area of a building (of its door tile), for the
// computer players.
std::uint32_t __fastcall AI_BuildingRecordArea(const layout::Building* building) {
    return AreaOfTile(building->door_x, building->door_y);
}

// Purpose: The walk region at a map position for the computer players; on
// a tile without one it looks a few tiles further in +x (0xffff off the
// map).
std::uint32_t __fastcall AI_WalkRegionAlongRow(std::uint32_t xArg, std::uint32_t yArg) {
    auto x = static_cast<std::uint16_t>(xArg);
    const auto y = static_cast<std::uint16_t>(yArg);
    const std::uint16_t sizeX = g_aiMapSizeX;
    if (x >= sizeX || y >= g_aiMapSizeY) {
        return kNone;  // AX only
    }
    // 0x5998ad..0x5998e2: at most 16 steps in +x, while x < size - 1.
    for (std::uint32_t steps = 0; AiTile(x, y).walk_region == 0 && steps < kWalkSearchSteps; ++steps) {
        const auto next = static_cast<std::uint16_t>(x + 1);
        x = next;
        if (static_cast<std::int32_t>(next) >= static_cast<std::int32_t>(sizeX) - 1) {
            break;
        }
    }
    return (TileIndex(x, y) & 0xffff0000u) | AiTile(x, y).walk_region;
}

// Purpose: How many stones a stone object on a map position still holds
// (0 if there is none), for the computer players.
std::uint32_t __fastcall AI_StoneAmountAt(const std::uint16_t* x, const std::uint16_t* y) {
    const std::uint8_t object = AiTile(*x, *y).object;
    if (object < kFirstStone || object > kLastStone) {
        return 0;
    }
    // The original subtracts the whole register: its upper half holds the
    // tile index * 3 of the address arithmetic.
    const std::uint32_t reg = ((TileIndex(*x, *y) * 3) & 0xffff0000u) | object;
    return kStoneUnitsBase - reg;
}

// Purpose: Whether a tree stands on a map position, for the computer
// players.
std::uint32_t __fastcall AI_IsTreeAt(const std::uint16_t* x, const std::uint16_t* y) {
    const std::uint8_t object = AiTile(*x, *y).object;
    return object >= kFirstTree && object <= kLastTree ? 1 : 0;
}

// Purpose: The amount of the resource (ore, coal, ... in the ground) on a
// map position, for the computer players (0 off the map).
std::uint32_t __fastcall AI_ResourceAmountAt(const std::uint16_t* x, const std::uint16_t* y) {
    if (!OnAiMap(*x, *y)) {
        return 0;  // AX only
    }
    return AiTile(*x, *y).resources & kResourceAmountMask;
}

// Purpose: The kind of resource in the ground of a map position (high
// nibble), for the computer players (0xffff off the map).
std::uint32_t __fastcall AI_ResourceKindAt(const std::uint16_t* x, const std::uint16_t* y) {
    if (!OnAiMap(*x, *y)) {
        return kNone;  // AX only
    }
    return AiTile(*x, *y).resources & kResourceKindMask;
}

// Purpose: The radius of the land a military building claims (the
// distance within which its 3500 / 4000 / 5000 nearest tiles lie), for
// the computer players' planning.
std::uint32_t __fastcall AI_TerritoryRadius(const std::uint16_t* type) {
    std::size_t offset = 0;
    switch (*type) {  // the whole word (never narrowed into the uint8 enum)
    case Value(BuildingType::SmallTower): offset = kSmallTowerOffsets; break;
    case Value(BuildingType::LargeTower): offset = kLargeTowerOffsets; break;
    case Value(BuildingType::Castle): offset = kCastleOffsets; break;
    default: break;
    }
    const CGameData* world = g_aiWorld;
    // AX: the distance's low word; the upper half is the world pointer's.
    const auto distance = static_cast<std::uint16_t>(world->offsets[offset].distance);
    return (reinterpret_cast<std::uint32_t>(world) & 0xffff0000u) | distance;
}

// Purpose: What kind of building site a building type needs (a mine, a
// field, a military building, a storehouse or an ordinary one), for the
// computer players' site search.
std::uint32_t __fastcall AI_SiteKindOf(std::uint32_t typeArg) {
    using B = BuildingType;
    SiteKind kind = SiteKind::Ordinary;
    switch (static_cast<std::uint16_t>(typeArg)) {  // the whole word (never narrowed into the uint8 enum)
    case Value(B::Storehouse): kind = SiteKind::Storehouse; break;
    case Value(B::SmallTower):
    case Value(B::LargeTower):
    case Value(B::Castle): kind = SiteKind::Military; break;
    case Value(B::CoalMine):
    case Value(B::GoldMine):
    case Value(B::IronMine):
    case Value(B::SulfurMine):
    case Value(B::GemMine): kind = SiteKind::Mine; break;
    case Value(B::Vineyard):
    case Value(B::Farm):
    case Value(B::RiceFarm): kind = SiteKind::Field; break;
    default: break;  // (types above 37 too: the table covers 1..37 only)
    }
    return Value(kind);  // AL only
}

// Purpose: Takes the best place found for a building type for building
// it: moves it to the "taken" candidates and clears it.
std::uint32_t __fastcall AI_TakeBestCandidate(std::uint32_t typeArg) {
    const std::uint32_t type = typeArg & 0xffff;
    AiPlayerData* data = g_aiPlayerData;
    // (Not range-checked, like the original's address arithmetic.)
    AiCandidate& best = data->best[type];
    AiCandidate& taken = data->best[kOrderedCandidates + type];
    taken.score = best.score;
    taken.a = best.a;
    taken.b = best.b;
    best.score = 0;
    best.a = 0;
    best.b = 0;
    return reinterpret_cast<std::uint32_t>(data);
}

// Purpose: How many construction sites a player has in an economy area,
// for the computer players.
std::uint32_t __fastcall AI_CountSitesInArea(const std::uint8_t* player, const std::uint16_t* area) {
    using GetBuildingFn = const layout::Building*(__fastcall*)(std::uint32_t id, std::uint32_t* error);
    using BuildingAreaFn = std::uint32_t(__fastcall*)(const std::uint32_t* id);
    std::uint32_t error = 0;
    std::uint16_t count = 0;
    std::uint32_t id = 0;
    do {
        ++id;
        const layout::Building* building = reinterpret_cast<GetBuildingFn>(kGetBuilding)(id, &error);
        if (building != nullptr && building->owner == *player && building->buildup != 0) {
            const std::uint32_t number = id;
            if (*area == static_cast<std::uint16_t>(reinterpret_cast<BuildingAreaFn>(kBuildingArea)(&number))) {
                ++count;
            }
        }
    } while (error == 0);
    return (error & 0xffff0000u) | count;
}

// Purpose: How many settlers of a type an economy area has (the length of
// its settler list for the type), for the computer players.
std::uint32_t __fastcall AI_CountAreaSettlersOfType(const std::uint8_t* type, const std::uint16_t* area) {
    const CGameData* world = g_aiWorld;
    // Neither index is range-checked (indexes past the arrays read what lies
    // there, like the original).
    std::uint32_t count = 0;
    for (std::uint16_t settler = world->areas[*area].settlers_by_type[*type]; settler != 0;
         settler = world->settlers[settler].next) {
        ++count;
    }
    return count;
}

// Purpose: Whether a player's team still has a player taking part
// (Player::active == 1; the player itself counts), for the computer
// players.
std::uint32_t __stdcall AI_TeamHasActivePlayer(std::uint32_t player) {
    const CGameData* world = g_aiMapWorld;
    const std::int32_t team = world->players[player].team;
    for (std::size_t p = 0; p < kPlayers; ++p) {
        if (world->players[p].active == 1 && world->players[p].team == team) {
            return 1;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// A world pair (g_aiWorld / g_aiMapWorld pointed at each side), the AI map
// size and random positions near and past its edges. Off the map only AX
// is compared (the upper half is the caller's EAX).

namespace {

struct MapQueries {
    CoordFn buildingAt, areaAt;
    BuildingFn recordArea;
    ValueCoordFn walkRegion;
    CoordFn stones, tree, resourceAmount, resourceKind;
    TypePtrFn radius;
    TypeFn siteKind, take;
    CountFn sites, settlers;
    PlayerFn team;
};

MapQueries OriginalQueries() {
    return {Original_AI_BuildingAt,       Original_AI_AreaAt,          Original_AI_BuildingRecordArea,
            Original_AI_WalkRegionAlongRow, Original_AI_StoneAmountAt, Original_AI_IsTreeAt,
            Original_AI_ResourceAmountAt, Original_AI_ResourceKindAt,  Original_AI_TerritoryRadius,
            Original_AI_SiteKindOf,       Original_AI_TakeBestCandidate, Original_AI_CountSitesInArea,
            Original_AI_CountAreaSettlersOfType, Original_AI_TeamHasActivePlayer};
}

MapQueries OurQueries() {
    return {&AI_BuildingAt,       &AI_AreaAt,          &AI_BuildingRecordArea,  &AI_WalkRegionAlongRow,
            &AI_StoneAmountAt,    &AI_IsTreeAt,        &AI_ResourceAmountAt,    &AI_ResourceKindAt,
            &AI_TerritoryRadius,  &AI_SiteKindOf,      &AI_TakeBestCandidate,   &AI_CountSitesInArea,
            &AI_CountAreaSettlersOfType, &AI_TeamHasActivePlayer};
}

constexpr int kOps = 14;
constexpr std::size_t kDataBytes = 0x1000;

struct SavedState {
    CGameData* world = g_aiWorld;
    CGameData* mapWorld = g_aiMapWorld;
    AiPlayerData* data = g_aiPlayerData;
    std::uint16_t sizeX = g_aiMapSizeX, sizeY = g_aiMapSizeY;
    ~SavedState() {
        g_aiWorld = world;
        g_aiMapWorld = mapWorld;
        g_aiPlayerData = data;
        g_aiMapSizeX = sizeX;
        g_aiMapSizeY = sizeY;
    }
};

std::uint16_t Coordinate(test::Rng& rng, std::uint16_t size) {
    if (rng.Chance(1, 10)) {
        return static_cast<std::uint16_t>(size + rng.Below(4));  // just off the map
    }
    if (rng.Chance(1, 4)) {
        return static_cast<std::uint16_t>(size - 1 - rng.Below(size < 20 ? size : 20));  // near the edge
    }
    return static_cast<std::uint16_t>(rng.Below(size));
}

std::uint8_t RandomObject(test::Rng& rng) {
    constexpr std::uint8_t kEdges[] = {0x43, 0x44, 0x53, 0x54, 0x72, 0x73, 0x7e, 0x7f, 0};
    return rng.Chance(1, 2) ? kEdges[rng.Below(9)] : static_cast<std::uint8_t>(rng.Below(256));
}

void CompareQueries(test::Context& ctx, const MapQueries& candidate, int cases) {
    const MapQueries original = OriginalQueries();
    SavedState saved;
    test::WorldPair worlds(ctx, sizeof(CGameData));
    std::vector<std::uint8_t> dataA(kDataBytes), dataB(kDataBytes);
    auto use = [&](bool ours) {
        CGameData* world = ours ? worlds.ours() : worlds.original();
        g_aiWorld = world;
        g_aiMapWorld = world;
        g_aiPlayerData = reinterpret_cast<AiPlayerData*>(ours ? dataB.data() : dataA.data());
    };
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(kOps));
        const auto sizeX = static_cast<std::uint16_t>(1 + ctx.rng().Below(kMapRowTiles - 1));
        const auto sizeY = static_cast<std::uint16_t>(1 + ctx.rng().Below(kMapRowTiles - 1));
        g_aiMapSizeX = sizeX;
        g_aiMapSizeY = sizeY;
        std::uint16_t xy[2] = {Coordinate(ctx.rng(), sizeX), Coordinate(ctx.rng(), sizeY)};
        const bool onMap = xy[0] < sizeX && xy[1] < sizeY;
        // The tile and its row to the right.
        for (std::uint16_t k = 0; k < 20 && onMap && xy[0] + k < kMapRowTiles; ++k) {
            const auto x = static_cast<std::uint16_t>(xy[0] + k);
            const auto region = static_cast<std::uint8_t>(ctx.rng().Chance(3, 4) ? 0 : ctx.rng().Below(256));
            worlds.Both([&](CGameData* w) { w->tiles[x][xy[1]].walk_region = region; });
        }
        if (xy[0] < kMapRowTiles && xy[1] < kMapRowTiles) {
            const std::uint8_t object = RandomObject(ctx.rng());
            const auto number = static_cast<std::uint16_t>(ctx.rng().Chance(1, 4) ? 0 : ctx.rng().Next32());
            const auto flags = static_cast<std::uint8_t>(ctx.rng().Next32());
            worlds.Both([&](CGameData* w) {
                w->tiles[xy[0]][xy[1]].object = object;
                w->tiles[xy[0]][xy[1]].object_number = number;
                w->tiles[xy[0]][xy[1]].flags = flags;
            });
        }
        ctx.rng().Fill(dataA.data(), dataA.size());
        dataB = dataA;
        std::uint32_t ra = 0, rb = 0;
        std::uint32_t mask = 0xffffffffu;
        switch (op) {
        case 0:
        case 1:
        case 4:
        case 5:
        case 6:
        case 7: {
            const CoordFn fa[] = {original.buildingAt, original.areaAt, nullptr, nullptr, original.stones,
                                  original.tree, original.resourceAmount, original.resourceKind};
            const CoordFn fb[] = {candidate.buildingAt, candidate.areaAt, nullptr, nullptr, candidate.stones,
                                  candidate.tree, candidate.resourceAmount, candidate.resourceKind};
            const bool bounded = op != 4 && op != 5;
            if (!bounded && !(xy[0] < kMapRowTiles && xy[1] < kMapRowTiles)) {
                xy[0] = static_cast<std::uint16_t>(xy[0] % kMapRowTiles);
                xy[1] = static_cast<std::uint16_t>(xy[1] % kMapRowTiles);
                const std::uint8_t object = RandomObject(ctx.rng());
                worlds.Both([&](CGameData* w) { w->tiles[xy[0]][xy[1]].object = object; });
            }
            if (bounded && !onMap) {
                mask = 0xffff;
            }
            use(false);
            ra = fa[op](&xy[0], &xy[1]);
            use(true);
            rb = fb[op](&xy[0], &xy[1]);
            break;
        }
        case 2: {
            layout::Building record{};
            record.door_x = xy[0];
            record.door_y = xy[1];
            mask = onMap ? 0xffffffffu : 0xffff;
            use(false);
            ra = original.recordArea(&record);
            use(true);
            rb = candidate.recordArea(&record);
            break;
        }
        case 3: {
            const std::uint32_t x = xy[0] | (ctx.rng().Chance(1, 4) ? ctx.rng().Next32() & 0xffff0000u : 0);
            const std::uint32_t y = xy[1] | (ctx.rng().Chance(1, 4) ? ctx.rng().Next32() & 0xffff0000u : 0);
            mask = onMap ? 0xffffffffu : 0xffff;
            use(false);
            ra = original.walkRegion(x, y);
            use(true);
            rb = candidate.walkRegion(x, y);
            break;
        }
        case 8: {
            const std::uint16_t type = ctx.rng().Chance(1, 2) ? static_cast<std::uint16_t>(14 + ctx.rng().Below(6))
                                                              : static_cast<std::uint16_t>(ctx.rng().Next32());
            use(false);
            ra = original.radius(&type) - (reinterpret_cast<std::uint32_t>(worlds.original()) & 0xffff0000u);
            use(true);
            rb = candidate.radius(&type) - (reinterpret_cast<std::uint32_t>(worlds.ours()) & 0xffff0000u);
            break;
        }
        case 9: {
            const std::uint32_t type = ctx.rng().Chance(3, 4) ? ctx.rng().Below(60) : ctx.rng().Next32();
            mask = 0xff;
            ra = original.siteKind(type);
            rb = candidate.siteKind(type);
            break;
        }
        case 10: {
            const std::uint32_t type = ctx.rng().Below(100) | (ctx.rng().Chance(1, 4) ? ctx.rng().Next32() << 16 : 0);
            use(false);
            ra = original.take(type) - reinterpret_cast<std::uint32_t>(dataA.data());
            use(true);
            rb = candidate.take(type) - reinterpret_cast<std::uint32_t>(dataB.data());
            break;
        }
        case 11: {
            const std::uint32_t count = ctx.rng().Below(40);
            const auto player = static_cast<std::uint8_t>(ctx.rng().Below(3));
            const auto area = static_cast<std::uint16_t>(ctx.rng().Below(4));
            g_aiMapSizeX = 400;
            g_aiMapSizeY = 400;
            worlds.Both([&](CGameData* w) { w->building_high_water = count; });
            for (std::uint32_t id = 1; id <= count; ++id) {
                const auto owner = static_cast<std::uint8_t>(ctx.rng().Chance(1, 6) ? 0xff : ctx.rng().Below(3));
                const auto buildup = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Next32());
                const auto x = static_cast<std::uint16_t>(ctx.rng().Below(400));
                const auto y = static_cast<std::uint16_t>(ctx.rng().Below(400));
                const auto tileArea = static_cast<std::uint16_t>(ctx.rng().Below(4));
                worlds.Both([&](CGameData* w) {
                    w->buildings[id].owner = owner;
                    w->buildings[id].buildup = buildup;
                    w->buildings[id].door_x = x;
                    w->buildings[id].door_y = y;
                    w->tiles[x][y].area = tileArea;
                });
            }
            use(false);
            ra = original.sites(&player, &area);
            use(true);
            rb = candidate.sites(&player, &area);
            break;
        }
        case 12: {
            const auto type = static_cast<std::uint8_t>(ctx.rng().Below(48));
            const auto area = static_cast<std::uint16_t>(ctx.rng().Below(4096));
            const std::uint32_t length = ctx.rng().Below(10);
            const std::uint32_t start = 1 + ctx.rng().Below(20000);
            std::uint16_t head = 0;
            for (std::uint32_t k = 0; k < length; ++k) {
                const auto settler = static_cast<std::uint16_t>(start + k * 7);
                worlds.Both([&](CGameData* w) { w->settlers[settler].next = head; });
                head = settler;
            }
            worlds.Both([&](CGameData* w) { w->areas[area].settlers_by_type[type] = head; });
            use(false);
            ra = original.settlers(&type, &area);
            use(true);
            rb = candidate.settlers(&type, &area);
            break;
        }
        default: {
            for (std::size_t p = 0; p < kPlayers; ++p) {
                const std::int32_t active = ctx.rng().Chance(1, 2) ? 1 : static_cast<std::int32_t>(ctx.rng().Below(3));
                const auto team = static_cast<std::int32_t>(ctx.rng().Below(4));
                worlds.Both([&](CGameData* w) {
                    w->players[p].active = active;
                    w->players[p].team = team;
                });
            }
            const std::uint32_t player = ctx.rng().Below(kPlayers);
            use(false);
            ra = original.team(player);
            use(true);
            rb = candidate.team(player);
            break;
        }
        }
        bool same = S3RE_EXPECT_EQ(ra & mask, rb & mask);
        same &= S3RE_EXPECT_MEM_EQ(dataA.data(), dataB.data(), dataA.size());
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d, x %u, y %u", i, op, xy[0], xy[1]);
        }
    }
    S3RE_EXPECT(worlds.Checkpoint(ctx));
}

std::uint32_t __fastcall WrongWalkRegion(std::uint32_t x, std::uint32_t y) {
    // Wrong: searches up to the last column too.
    const auto row = static_cast<std::uint16_t>(y);
    auto column = static_cast<std::uint16_t>(x);
    if (column >= g_aiMapSizeX || row >= g_aiMapSizeY) {
        return kNone;
    }
    for (std::uint32_t steps = 0; AiTile(column, row).walk_region == 0 && steps < kWalkSearchSteps; ++steps) {
        if (column + 1 >= g_aiMapSizeX) {
            break;
        }
        ++column;
    }
    return (TileIndex(column, row) & 0xffff0000u) | AiTile(column, row).walk_region;
}

std::uint32_t __fastcall WrongStones(const std::uint16_t* x, const std::uint16_t* y) {
    // Wrong: without the register's upper half.
    const std::uint8_t object = AiTile(*x, *y).object;
    return object >= kFirstStone && object <= kLastStone ? kStoneUnitsBase - object : 0;
}

std::uint32_t __fastcall WrongSiteKind(std::uint32_t type) {
    // Wrong: the gem mine is no mine.
    return static_cast<std::uint16_t>(type) == Value(BuildingType::GemMine) ? Value(SiteKind::Ordinary)
                                                                          : AI_SiteKindOf(type);
}

std::uint32_t __stdcall WrongTeam(std::uint32_t player) {
    // Wrong: counts any player taking part (active != 0).
    const CGameData* world = g_aiMapWorld;
    for (std::size_t p = 0; p < kPlayers; ++p) {
        if (world->players[p].active != 0 && world->players[p].team == world->players[player].team) {
            return 1;
        }
    }
    return 0;
}

}  // namespace

S3RE_TEST(AiMapQueries_MatchOriginal) {
    CompareQueries(ctx, OurQueries(), 40000);
}

S3RE_TEST(AiMapQueries_ComparisonDetectsWrongImplementation) {
    const auto probe = [&](auto change) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        MapQueries wrong = OurQueries();
        change(wrong);
        CompareQueries(quiet, wrong, 20000);
        S3RE_EXPECT(quiet.failures() > 0);
    };
    probe([](MapQueries& f) { f.walkRegion = &WrongWalkRegion; });
    probe([](MapQueries& f) { f.stones = &WrongStones; });
    probe([](MapQueries& f) { f.siteKind = &WrongSiteKind; });
    probe([](MapQueries& f) { f.team = &WrongTeam; });
    probe([](MapQueries& f) { f.resourceAmount = f.resourceKind; });
}

}  // namespace s3re::game
