// HOUSE_GROUND_REQUEST_NOTE's site search (computer players, session 54;
// the unnamed source file between Rainer\compGlobs.cpp and
// CompHouseReq.cpp, re/computer_players.md). HOUSE_GROUND_REQUEST_AGENT::
// WorkOnNote 0x576530 walks the note's spiral around the requested place
// and, on each of the player's tiles, asks CheckAndImproveSite. All three
// are thiscall on the note (HouseGroundRequestNote); the computer player's
// globals g_aiPlayer, g_aiPlayerNumber and g_aiPlayerSetup are set.
//
//   0x005760d0 CheckSite(const u16* x, const u16* y), ret 8: AL = 0 unless
//              the tile is the player's (AI::TileOwner 0x599570, 16-bit
//              compare with g_aiPlayerNumber) and, by the note's building
//              type (+0x1c; jump table 0x57635c):
//              - mines (coal, gold, iron, sulfur, gems): AI::ResourceAmountNear
//                0x571fa0 of the mine's resource (0x10, 0x30, 0x20, 0x50,
//                0x40) is at least g_aiPlayerSetup->min_mine_resources;
//              - fields: TileDistance 0x4ff320 from the request's place
//                (+0x1e, +0x20) is at least a work radius: the distance of
//                map offset 150 (vineyard), 300 + 5 (farm) or 400 + 3 (rice
//                farm);
//              then AL = CGameData::CheckBuildingSite 0x4d99e0(x, y, type,
//              player, 0) on g_aiPlayer->world (0 = impossible); 0 if the
//              player has a building of the type within g_aiSameTypeDistance
//              [type] (if not 0; AI::HasBuildingNear 0x571d90); for all but
//              mines (AI::SiteKindOf 0x58ed70 = 4), 0 if one of the
//              player's finished field buildings (SiteKindOf 5, AI::
//              GetBuilding 0x58e170 from id 1 to its end flag) has its work
//              point closer than its work radius + 4 (offset 150 / 300 /
//              400). Else AL = the site value. Only AL is meaningful (the
//              caller tests AL; the upper bytes are the callees' leftovers).
//   0x00575df0 CheckAndImproveSite(): v = CheckSite at the spiral position
//              (+0x2c, +0x2e); 0 -> EAX = 0. Mines: among the first 100
//              map offsets around it (on the map: unsigned < map size), the
//              place where CheckBuildingSite(.., mode 0) is not 0 and the
//              mine's resource summed over the first 64 offsets around it
//              (Tile::resources kind == the mine's, amounts) is the largest
//              (the first of equals); none above 0 -> EAX = 0 and nothing
//              stored. A small tower, or v <= 2: the spiral position
//              itself. Others: among the first 150 offsets, the first
//              place whose CheckSite is 1 or 2, else the one with the
//              lowest CheckSite value below v (the first of equals), else
//              the spiral position. The place goes to +0x2c / +0x2e, EAX =
//              1.
//   0x005763a0 NextSpiralPosition(): moves the spiral (+0x2c, WrappedPos::
//              Step 0x597110) on by one tile: the first call steps in
//              direction 4 to ring 1; then along side +0x3e (0..5) of ring
//              +0x3c, step +0x40; after 6 sides the next ring (again a
//              step in direction 4); EAX = 0 (and no step) once the ring
//              reaches +0x42 (the maximum radius), else 1.

#include "game/ai/ai_layout.h"
#include "game/enums/buildings.h"
#include "game/enums/resources.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/tables.h"
#include "game/typed_fields.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using namespace ai;
using layout::kMapRowTiles;

namespace {

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kTileOwner = 0x00599570;           // AI::TileOwner (fastcall, pointers)
constexpr std::uintptr_t kResourceAmountNear = 0x00571fa0;  // AI::ResourceAmountNear (fastcall x, y, kind)
constexpr std::uintptr_t kTileDistance = 0x004ff320;        // TileDistance (stdcall, ECX = world)
constexpr std::uintptr_t kCheckBuildingSite = 0x004d99e0;   // CGameData::CheckBuildingSite (thiscall)
constexpr std::uintptr_t kHasBuildingNear = 0x00571d90;     // AI::HasBuildingNear (fastcall)
constexpr std::uintptr_t kSiteKindOf = 0x0058ed70;          // AI::SiteKindOf (fastcall CX)
constexpr std::uintptr_t kGetBuilding = 0x0058e170;         // AI::GetBuilding (fastcall)
constexpr std::uintptr_t kCheckSite = 0x005760d0;           // HOUSE_GROUND_REQUEST_NOTE::CheckSite
constexpr std::uintptr_t kWrappedPosStep = 0x00597110;      // AI::WrappedPos::Step (thiscall)

using TileOwnerFn = std::uint32_t(__fastcall*)(const std::uint16_t* x, const std::uint16_t* y);
using AmountNearFn = std::uint32_t(__fastcall*)(std::uint32_t x, std::uint32_t y, std::uint32_t kind);
using DistanceFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x0, std::uint32_t y0,
                                              std::uint32_t x1, std::uint32_t y1);
using CheckBuildingSiteFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                       std::uint32_t type, std::uint32_t player, std::uint32_t mode);
using HasBuildingNearFn = std::uint32_t(__fastcall*)(std::uint32_t type, std::uint32_t x, std::uint32_t y,
                                                     std::uint32_t distance, std::uint32_t player);
using SiteKindOfFn = std::uint32_t(__fastcall*)(std::uint32_t type);
using GetBuildingFn = layout::Building*(__fastcall*)(std::uint32_t id, std::uint32_t* error);
using CheckSiteFn = std::uint32_t(__fastcall*)(void* self, void* edx, const void* x, const void* y);
using StepFn = std::uint32_t(__fastcall*)(WrappedPos* self, void* edx, std::uint32_t direction);

// AI::SiteKindOf's kinds used here.
constexpr std::uint8_t kSiteKindMine = 4;
constexpr std::uint8_t kSiteKindField = 5;

// Field work radii: the distance of these map offsets (sorted by distance).
constexpr std::size_t kVineyardRadiusOffset = 150;
constexpr std::size_t kFarmRadiusOffset = 300;
constexpr std::size_t kRiceFarmRadiusOffset = 400;
constexpr std::uint32_t kFarmCentreMargin = 5;      // CheckSite: from the request's place
constexpr std::uint32_t kRiceFarmCentreMargin = 3;
constexpr std::uint32_t kFieldMargin = 4;           // from another field building's work point

constexpr std::uint32_t kMineCandidates = 100;   // CheckAndImproveSite: offsets tried for a mine
constexpr std::uint32_t kMineArea = 64;          // offsets summed around each
constexpr std::uint32_t kSiteCandidates = 150;   // offsets tried for other buildings
constexpr std::uint8_t kGoodEnoughSite = 3;      // CheckSite values below this are taken at once

constexpr std::uint16_t kRingDirection = 4;      // WrappedPos::Step: to the next ring (x-1, y-1)
constexpr std::uint16_t kSpiralSides = 6;

std::uint32_t OffsetDistance(std::size_t offset) {
    const CGameData* world = g_aiWorld;
    return static_cast<std::uint32_t>(world->offsets[offset].distance);
}

// The resource a mine type digs, 0 for other types (jump tables 0x57635c /
// 0x576094).
std::uint8_t MineResource(std::uint16_t type) {
    using B = BuildingType;
    switch (type) {
    case Value(B::CoalMine): return kResourceCoal;
    case Value(B::GoldMine): return kResourceGold;
    case Value(B::IronMine): return kResourceIron;
    case Value(B::SulfurMine): return kResourceSulfur;
    case Value(B::GemMine): return kResourceGems;
    default: return 0;
    }
}

std::uint32_t Distance(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1) {
    return reinterpret_cast<DistanceFn>(kTileDistance)(g_aiWorld, nullptr, x0, y0, x1, y1);
}

// 0x5760f9..0x576214: the type's own conditions before CheckBuildingSite.
bool MeetsTypeConditions(const HouseGroundRequestNote& note, std::uint16_t x, std::uint16_t y) {
    using B = BuildingType;
    const std::uint16_t type = note.request.building_type;
    if (const std::uint8_t resource = MineResource(type); resource != 0) {
        const auto amount = static_cast<std::uint16_t>(reinterpret_cast<AmountNearFn>(kResourceAmountNear)(x, y, resource));
        return amount >= g_aiPlayerSetup.get()->min_mine_resources;
    }
    std::uint32_t radius = 0;
    switch (type) {
    case Value(B::Vineyard): radius = OffsetDistance(kVineyardRadiusOffset); break;
    case Value(B::Farm): radius = OffsetDistance(kFarmRadiusOffset) + kFarmCentreMargin; break;
    case Value(B::RiceFarm): radius = OffsetDistance(kRiceFarmRadiusOffset) + kRiceFarmCentreMargin; break;
    default: return true;
    }
    return Distance(note.request.x, note.request.y, x, y) >= radius;
}

// 0x5762ab..0x57634b: whether one of the player's finished fields works
// within its radius + 4 of the place.
bool NearOwnFieldWork(std::uint16_t x, std::uint16_t y) {
    using B = BuildingType;
    std::uint32_t end = 0;
    for (std::uint32_t id = 1; end == 0; ++id) {
        const layout::Building* building = reinterpret_cast<GetBuildingFn>(kGetBuilding)(id, &end);
        if (building == nullptr || building->owner != g_aiPlayerNumber || building->buildup != 0 ||
            static_cast<std::uint8_t>(reinterpret_cast<SiteKindOfFn>(kSiteKindOf)(building->art)) != kSiteKindField) {
            continue;
        }
        std::uint32_t radius = 0;
        switch (TypeOf(*building)) {
        case B::Vineyard: radius = OffsetDistance(kVineyardRadiusOffset) + kFieldMargin; break;
        case B::Farm: radius = OffsetDistance(kFarmRadiusOffset) + kFieldMargin; break;
        case B::RiceFarm: radius = OffsetDistance(kRiceFarmRadiusOffset) + kFieldMargin; break;
        default: break;  // not reached: SiteKindOf gives 5 for these three only
        }
        if (Distance(building->work_x, building->work_y, x, y) < radius) {
            return true;
        }
    }
    return false;
}

}  // namespace

using CheckAndImproveFn = std::uint32_t(__fastcall*)(void* self, void* edx);

std::uint32_t __fastcall HOUSE_GROUND_REQUEST_NOTE_CheckSite(void* self, void* edx, const void* x, const void* y);
std::uint32_t __fastcall HOUSE_GROUND_REQUEST_NOTE_CheckAndImproveSite(void* self, void* edx);
std::uint32_t __fastcall HOUSE_GROUND_REQUEST_NOTE_NextSpiralPosition(void* self, void* edx);

S3RE_HOOK(0x005760d0, HOUSE_GROUND_REQUEST_NOTE_CheckSite, CheckSiteFn, Original_HOUSE_GROUND_REQUEST_NOTE_CheckSite,
          HOUSE_GROUND_REQUEST_NOTE_CheckSite);
S3RE_HOOK(0x00575df0, HOUSE_GROUND_REQUEST_NOTE_CheckAndImproveSite, CheckAndImproveFn,
          Original_HOUSE_GROUND_REQUEST_NOTE_CheckAndImproveSite, HOUSE_GROUND_REQUEST_NOTE_CheckAndImproveSite);
S3RE_HOOK(0x005763a0, HOUSE_GROUND_REQUEST_NOTE_NextSpiralPosition, CheckAndImproveFn,
          Original_HOUSE_GROUND_REQUEST_NOTE_NextSpiralPosition, HOUSE_GROUND_REQUEST_NOTE_NextSpiralPosition);

// Purpose: Rates a tile as the place for the building a computer player
// asked ground for: 0 if it can't go there (not the player's land, too few
// resources for a mine, too close to its fields or to a building of the
// same type), else how much earth would have to be moved (lower is
// better).
std::uint32_t __fastcall HOUSE_GROUND_REQUEST_NOTE_CheckSite(void* self, void* /*edx*/, const void* xArg,
                                                             const void* yArg) {
    const auto& note = *static_cast<const HouseGroundRequestNote*>(self);
    const auto* xPointer = static_cast<const std::uint16_t*>(xArg);
    const auto* yPointer = static_cast<const std::uint16_t*>(yArg);
    const auto owner = static_cast<std::uint16_t>(reinterpret_cast<TileOwnerFn>(kTileOwner)(xPointer, yPointer));
    if (owner != g_aiPlayerNumber) {
        return 0;
    }
    const std::uint16_t x = *xPointer, y = *yPointer;
    if (!MeetsTypeConditions(note, x, y)) {
        return 0;
    }
    const std::uint16_t type = note.request.building_type;
    const auto value = static_cast<std::uint8_t>(reinterpret_cast<CheckBuildingSiteFn>(kCheckBuildingSite)(
        reinterpret_cast<CGameData*>(g_aiPlayer.get()->world), nullptr, x, y, type, g_aiPlayerNumber, 0));
    if (value == 0) {
        return 0;
    }
    const std::uint16_t distance = g_aiSameTypeDistance[note.request.building_type];  // the type read again
    if (distance != 0 &&
        reinterpret_cast<HasBuildingNearFn>(kHasBuildingNear)(type, x, y, distance, g_aiPlayerNumber) != 0) {
        return 0;
    }
    if (static_cast<std::uint8_t>(reinterpret_cast<SiteKindOfFn>(kSiteKindOf)(type)) == kSiteKindMine) {
        return value;
    }
    return NearOwnFieldWork(x, y) ? 0 : value;
}

namespace {

// 0x575e91..0x575fa2: the best mine place among the first 100 offsets
// around (x, y); false if none has any of the resource.
bool FindBestMinePlace(const HouseGroundRequestNote& note, std::uint16_t x, std::uint16_t y, std::uint8_t resource,
                       std::uint32_t& bestX, std::uint32_t& bestY) {
    std::uint32_t bestSum = 0;
    for (std::size_t k = 0; k < kMineCandidates; ++k) {
        CGameData* world = g_aiWorld;
        const std::uint32_t placeX = static_cast<std::uint32_t>(world->offsets[k].dx) + x;
        const std::uint32_t placeY = static_cast<std::uint32_t>(world->offsets[k].dy) + y;
        if (placeX >= world->map_size || placeY >= world->map_size) {
            continue;
        }
        if (reinterpret_cast<CheckBuildingSiteFn>(kCheckBuildingSite)(
                reinterpret_cast<CGameData*>(g_aiPlayer.get()->world), nullptr, placeX, placeY,
                note.request.building_type, g_aiPlayerNumber, 0) == 0) {
            continue;
        }
        world = g_aiWorld;  // read again after the call
        std::uint32_t sum = 0;
        for (std::size_t j = 0; j < kMineArea; ++j) {
            const std::uint32_t tileX = static_cast<std::uint32_t>(world->offsets[j].dx) + placeX;
            const std::uint32_t tileY = static_cast<std::uint32_t>(world->offsets[j].dy) + placeY;
            if (tileX >= world->map_size || tileY >= world->map_size) {
                continue;
            }
            const std::uint8_t resources = (&world->tiles[0][0])[tileX * kMapRowTiles + tileY].resources;
            if ((resources & kResourceKindMask) == resource) {
                sum += resources & kResourceAmountMask;
            }
        }
        if (sum > bestSum) {
            bestSum = sum;
            bestX = placeX;
            bestY = placeY;
        }
    }
    return bestSum != 0;
}

// 0x575fd0..0x576068: a better place among the first 150 offsets around
// (x, y): the first rated 1 or 2, else the lowest rating below `value`.
void FindBetterPlace(void* self, std::uint16_t x, std::uint16_t y, std::uint8_t value, std::uint32_t& bestX,
                     std::uint32_t& bestY) {
    std::uint8_t best = value;
    for (std::size_t k = 0; k < kSiteCandidates; ++k) {
        const CGameData* world = g_aiWorld;
        const std::uint32_t placeX = static_cast<std::uint32_t>(world->offsets[k].dx) + x;
        const std::uint32_t placeY = static_cast<std::uint32_t>(world->offsets[k].dy) + y;
        if (placeX >= world->map_size || placeY >= world->map_size) {
            continue;
        }
        // CheckSite reads the words of these dwords
        const std::uint32_t copyX = placeX, copyY = placeY;
        const auto rating =
            static_cast<std::uint8_t>(reinterpret_cast<CheckSiteFn>(kCheckSite)(self, nullptr, &copyX, &copyY));
        if (rating == 0) {
            continue;
        }
        if (rating < kGoodEnoughSite) {
            bestX = placeX;
            bestY = placeY;
            return;
        }
        if (rating < best) {
            best = rating;
            bestX = placeX;
            bestY = placeY;
        }
    }
}

}  // namespace

// Purpose: Checks the computer player's current candidate place for a
// requested building and looks around it for a better one (the richest
// spot for a mine, a flatter one for other buildings); stores the place and
// returns 1 if the building can go there.
std::uint32_t __fastcall HOUSE_GROUND_REQUEST_NOTE_CheckAndImproveSite(void* self, void* /*edx*/) {
    auto& note = *static_cast<HouseGroundRequestNote*>(self);
    // CheckSite reads the words of these dwords
    const std::uint32_t spiralX = static_cast<std::uint16_t>(note.spiral.x);
    const std::uint32_t spiralY = static_cast<std::uint16_t>(note.spiral.y);
    const auto value =
        static_cast<std::uint8_t>(reinterpret_cast<CheckSiteFn>(kCheckSite)(self, nullptr, &spiralX, &spiralY));
    if (value == 0) {
        return 0;
    }
    std::uint32_t bestX = spiralX, bestY = spiralY;
    const std::uint16_t type = note.request.building_type;
    const auto kind = static_cast<std::uint8_t>(reinterpret_cast<SiteKindOfFn>(kSiteKindOf)(type));
    if (kind == kSiteKindMine) {
        // a mine type without a resource (none exists) would compare with the value
        const std::uint8_t resource = MineResource(note.request.building_type);
        const std::uint8_t wanted = resource != 0 ? resource : value;
        if (!FindBestMinePlace(note, static_cast<std::uint16_t>(spiralX), static_cast<std::uint16_t>(spiralY), wanted,
                               bestX, bestY)) {
            return 0;
        }
    } else if (note.request.building_type != Value(BuildingType::SmallTower) && value >= kGoodEnoughSite) {
        FindBetterPlace(self, static_cast<std::uint16_t>(spiralX), static_cast<std::uint16_t>(spiralY), value, bestX,
                        bestY);
    }
    note.spiral.x = static_cast<std::uint16_t>(bestX);
    note.spiral.y = static_cast<std::uint16_t>(bestY);
    return 1;
}

// Purpose: Moves a ground request's search one tile further along its
// spiral around the requested place; 0 when the spiral has reached its
// maximum radius.
std::uint32_t __fastcall HOUSE_GROUND_REQUEST_NOTE_NextSpiralPosition(void* self, void* /*edx*/) {
    auto& note = *static_cast<HouseGroundRequestNote*>(self);
    const auto step = [&note](std::uint16_t direction) {
        reinterpret_cast<StepFn>(kWrappedPosStep)(&note.spiral, nullptr, direction);
    };
    if (note.spiral_ring == 0) {
        note.spiral_ring = 1;
        step(kRingDirection);
        note.spiral_side = 0;
        note.spiral_step = 0;
        return 1;
    }
    step(note.spiral_side);
    ++note.spiral_step;
    if (note.spiral_step != note.spiral_ring) {
        return 1;
    }
    ++note.spiral_side;
    note.spiral_step = 0;
    if (note.spiral_side != kSpiralSides) {
        return 1;
    }
    const auto ring = static_cast<std::uint16_t>(note.spiral_ring + 1);
    note.spiral_ring = ring;
    if (ring == note.max_radius) {
        return 0;
    }
    step(kRingDirection);
    note.spiral_side = 0;
    return 1;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// An 80 x 80 test map (AI map size 60..80) owned in large patches by
// players 0..2, with resources mostly of one kind per patch; the map offset
// table ordered by hexagonal ring (as the game builds it); up to 40
// building records (fields of the three field types among them); a note of
// a random interesting building type. CheckBuildingSite is replaced by a
// recording stub that gives a value 0..10 fixed by its arguments (it is
// verified on its own; on these test tiles it would almost always say 0),
// so both sides see the same site values and their calls are compared.
// Compared: AL (CheckSite) or EAX, the note, the stub's calls.

namespace {

constexpr std::uint32_t kTestMap = 80;
constexpr std::int32_t kOffsetRings = 13;  // 547 offsets (400 are read)

using GroundFn = std::uint32_t(__fastcall*)(void* self, void* edx);

struct GroundFunctions {
    CheckSiteFn checkSite;
    GroundFn improve;
    GroundFn nextSpiral;
};

GroundFunctions OriginalGroundFunctions() {
    return {Original_HOUSE_GROUND_REQUEST_NOTE_CheckSite, Original_HOUSE_GROUND_REQUEST_NOTE_CheckAndImproveSite,
            Original_HOUSE_GROUND_REQUEST_NOTE_NextSpiralPosition};
}

GroundFunctions OurGroundFunctions() {
    return {&HOUSE_GROUND_REQUEST_NOTE_CheckSite, &HOUSE_GROUND_REQUEST_NOTE_CheckAndImproveSite,
            &HOUSE_GROUND_REQUEST_NOTE_NextSpiralPosition};
}

struct SavedGroundGlobals {
    CGameData* world = g_aiWorld;
    ComputerPlayer* player = g_aiPlayer;
    std::uint8_t number = g_aiPlayerNumber;
    AiPlayerSetup* setup = g_aiPlayerSetup;
    std::uint16_t sizeX = g_aiMapSizeX, sizeY = g_aiMapSizeY;
    ~SavedGroundGlobals() {
        g_aiWorld = world;
        g_aiPlayer = player;
        g_aiPlayerNumber = number;
        g_aiPlayerSetup = setup;
        g_aiMapSizeX = sizeX;
        g_aiMapSizeY = sizeY;
    }
};

std::int32_t RingOf(std::int32_t dx, std::int32_t dy) {
    const std::int32_t ax = dx < 0 ? -dx : dx;
    const std::int32_t ay = dy < 0 ? -dy : dy;
    if ((dx >= 0) == (dy >= 0)) {
        return ax > ay ? ax : ay;
    }
    return ax + ay;
}

void SetUpGroundWorld(test::Rng& rng, CGameData* world) {
    world->map_size = kTestMap;
    std::size_t entry = 0;
    for (std::int32_t ring = 0; ring <= kOffsetRings; ++ring) {
        for (std::int32_t dx = -ring; dx <= ring; ++dx) {
            for (std::int32_t dy = -ring; dy <= ring; ++dy) {
                if (RingOf(dx, dy) == ring) {
                    world->offsets[entry++] = layout::MapOffset{dx, dy, ring, 0};
                }
            }
        }
    }
    // Owners and resources in 10 x 10 patches.
    for (std::uint32_t px = 0; px < kTestMap; px += 10) {
        for (std::uint32_t py = 0; py < kTestMap; py += 10) {
            const auto owner = static_cast<std::uint8_t>(rng.Chance(1, 5) ? 0xff : rng.Below(3));
            const auto kind = static_cast<std::uint8_t>(rng.Below(6) << 4);
            for (std::uint32_t x = px; x < px + 10; ++x) {
                for (std::uint32_t y = py; y < py + 10; ++y) {
                    layout::Tile& tile = world->tiles[x][y];
                    tile.owner = rng.Chance(1, 15) ? static_cast<std::uint8_t>(rng.Below(3)) : owner;
                    tile.resources = static_cast<std::uint8_t>((rng.Chance(4, 5) ? kind : rng.Below(6) << 4) |
                                                               (rng.Chance(1, 3) ? 0 : rng.Below(16)));
                    tile.terrain = static_cast<std::uint8_t>(rng.Below(256));
                }
            }
        }
    }
    using B = BuildingType;
    constexpr B kTypes[] = {B::Vineyard, B::Farm, B::RiceFarm, B::Woodcutter, B::CoalMine, B::Sawmill};
    world->building_high_water = rng.Below(40);
    for (std::uint32_t id = 0; id <= world->building_high_water + 1; ++id) {
        layout::Building& building = world->buildings[id];
        std::memset(&building, 0, sizeof(building));
        building.owner = static_cast<std::uint8_t>(rng.Chance(1, 6) ? 0xff : rng.Below(3));
        building.buildup = static_cast<std::uint8_t>(rng.Chance(1, 6) ? 1 + rng.Below(255) : 0);
        building.art = Value(kTypes[rng.Below(sizeof(kTypes) / sizeof(kTypes[0]))]);
        building.x = static_cast<std::uint16_t>(rng.Below(kTestMap));
        building.y = static_cast<std::uint16_t>(rng.Below(kTestMap));
        building.work_x = static_cast<std::uint16_t>(rng.Below(kTestMap));
        building.work_y = static_cast<std::uint16_t>(rng.Below(kTestMap));
    }
}

std::uint16_t RandomGroundType(test::Rng& rng) {
    constexpr std::uint16_t kTypes[] = {7, 8, 9, 29, 37, 14, 24, 36, 15, 2, 3, 5, 1, 25, 51, 33};
    return rng.Chance(1, 10) ? static_cast<std::uint16_t>(rng.Below(64))
                             : kTypes[rng.Below(sizeof(kTypes) / sizeof(kTypes[0]))];
}

// CheckBuildingSite's stub: 0..10 from its arguments (0 in about 30 %).
std::uint32_t StubSiteValue(CGameData*, std::uint32_t, const std::uint32_t* args) {
    std::uint32_t h = args[0] * 0x9e3779b1u ^ args[1] * 0x85ebca6bu ^ args[2] * 0xc2b2ae35u ^ args[3] * 0x27d4eb2fu;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 13;
    return h % 10 < 3 ? 0 : 1 + h % 10;
}

void CompareGround(test::Context& ctx, const GroundFunctions& candidate, int cases) {
    const GroundFunctions original = OriginalGroundFunctions();
    SavedGroundGlobals saved;
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    std::vector<std::uint8_t> computer(sizeof(ComputerPlayer));
    AiPlayerSetup setup{};
    g_aiWorld = world;
    g_aiPlayer = reinterpret_cast<ComputerPlayer*>(computer.data());
    g_aiPlayer.get()->world = reinterpret_cast<std::uint32_t>(world);
    g_aiPlayerSetup = &setup;
    test::CalleeStubs stubs;
    stubs.SetWorld(world);
    stubs.Install<5, kCheckBuildingSite>(&StubSiteValue);
    int moved = 0;
    int accepted = 0;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        if (i % 40 == 0) {
            SetUpGroundWorld(rng, world);
        }
        g_aiMapSizeX = static_cast<std::uint16_t>(60 + rng.Below(21));
        g_aiMapSizeY = static_cast<std::uint16_t>(60 + rng.Below(21));
        g_aiPlayerNumber = static_cast<std::uint8_t>(rng.Below(3));
        setup.min_mine_resources = static_cast<std::uint16_t>(rng.Below(60));
        HouseGroundRequestNote a{};
        rng.Fill(&a, sizeof(a));
        a.request.building_type = RandomGroundType(rng);
        a.request.x = static_cast<std::uint16_t>(rng.Below(kTestMap));
        a.request.y = static_cast<std::uint16_t>(rng.Below(kTestMap));
        a.spiral.x = static_cast<std::int16_t>(rng.Below(kTestMap));
        a.spiral.y = static_cast<std::int16_t>(rng.Below(kTestMap));
        a.spiral.width = g_aiMapSizeX;
        a.spiral.height = g_aiMapSizeY;
        a.max_radius = static_cast<std::uint16_t>(1 + rng.Below(8));
        a.spiral_ring = static_cast<std::uint16_t>(rng.Chance(1, 5) ? 0 : rng.Below(a.max_radius + 1));
        a.spiral_side = static_cast<std::uint16_t>(rng.Below(6));
        a.spiral_step = static_cast<std::uint16_t>(a.spiral_ring == 0 ? 0 : rng.Below(a.spiral_ring));
        HouseGroundRequestNote b = a;
        const WrappedPos start = a.spiral;
        const int op = static_cast<int>(rng.Below(3));
        std::uint32_t ra = 0, rb = 0;
        std::vector<test::StubCall> callsA, callsB;
        const std::uint32_t upper = rng.Chance(1, 4) ? rng.Next32() & 0xffff0000u : 0;
        const std::uint32_t x = rng.Below(kTestMap) | upper, y = rng.Below(kTestMap) | upper;
        switch (op) {
        case 0:
            ra = original.checkSite(&a, nullptr, &x, &y) & 0xff;  // only AL is meaningful
            callsA = stubs.Take();
            rb = candidate.checkSite(&b, nullptr, &x, &y) & 0xff;
            callsB = stubs.Take();
            accepted += ra != 0 ? 1 : 0;
            break;
        case 1:
            ra = original.improve(&a, nullptr);
            callsA = stubs.Take();
            rb = candidate.improve(&b, nullptr);
            callsB = stubs.Take();
            moved += ra != 0 && (a.spiral.x != start.x || a.spiral.y != start.y) ? 1 : 0;
            break;
        default:
            ra = original.nextSpiral(&a, nullptr);
            rb = candidate.nextSpiral(&b, nullptr);
            break;
        }
        bool same = S3RE_EXPECT(callsA == callsB);
        same &= S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT_MEM_EQ(&a, &b, sizeof(a));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d, type %u", i, op, a.request.building_type);
        }
    }
    if (cases >= 1000 && !S3RE_EXPECT(accepted >= 20 && moved >= 20)) {
        ctx.Fail(__FILE__, __LINE__, "  only %d accepted sites, %d found places", accepted, moved);
    }
}

// Wrong: the field check also made one tile to the right (x + 1), as if
// the work radius were one larger in that direction.
std::uint32_t __fastcall WrongCheckSite(void* self, void* edx, const void* x, const void* y) {
    const std::uint32_t result = HOUSE_GROUND_REQUEST_NOTE_CheckSite(self, edx, x, y);
    if (result == 0 || NearOwnFieldWork(static_cast<std::uint16_t>(*static_cast<const std::uint32_t*>(x) + 1),
                                        *static_cast<const std::uint16_t*>(y))) {
        return 0;
    }
    return result;
}

// Wrong: the small tower searches for a better place too.
std::uint32_t __fastcall WrongImprove(void* self, void* edx) {
    auto& note = *static_cast<HouseGroundRequestNote*>(self);
    if (note.request.building_type == Value(BuildingType::SmallTower)) {
        note.request.building_type = Value(BuildingType::Sawmill);
        const std::uint32_t result = HOUSE_GROUND_REQUEST_NOTE_CheckAndImproveSite(self, edx);
        note.request.building_type = Value(BuildingType::SmallTower);
        return result;
    }
    return HOUSE_GROUND_REQUEST_NOTE_CheckAndImproveSite(self, edx);
}

// Wrong: a ring ends one step early.
std::uint32_t __fastcall WrongNextSpiral(void* self, void* edx) {
    auto& note = *static_cast<HouseGroundRequestNote*>(self);
    const std::uint32_t result = HOUSE_GROUND_REQUEST_NOTE_NextSpiralPosition(self, edx);
    if (note.spiral_ring > 1 && note.spiral_step == note.spiral_ring - 1) {
        note.spiral_step = 0;
        ++note.spiral_side;
    }
    return result;
}

}  // namespace

S3RE_TEST(AiGroundRequest_MatchOriginal) {
    CompareGround(ctx, OurGroundFunctions(), 3000);
}

S3RE_TEST(AiGroundRequest_ComparisonDetectsWrongImplementation) {
    const GroundFunctions ours = OurGroundFunctions();
    const GroundFunctions wrongs[] = {
        {&WrongCheckSite, ours.improve, ours.nextSpiral},
        {ours.checkSite, &WrongImprove, ours.nextSpiral},
        {ours.checkSite, ours.improve, &WrongNextSpiral},
    };
    for (const GroundFunctions& wrong : wrongs) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareGround(quiet, wrong, 3000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}


}  // namespace s3re::game
