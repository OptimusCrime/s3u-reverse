// A construction site becomes a finished building.
//
// CGameData::BuildingDone (0x004dbc50): (building) thiscall, ret 4. Called
// by UpdateSettlers (the builder's job 0x12 when the last stage is done),
// the map loader 0x4fd540 (buildings placed by the map) and 0x506cf0. EAX
// not used by the callers. In order:
//   1. QueuePendingEvent(1, building, 0). buildup +0x21 = 0, word +0x8 = 0,
//      work tile +0x30/+0x32 = position +0x2/+0x4, bytes +0x19, +0x1a,
//      +0x1b, +0x1c, +0x1e, +0x1f, +0x20 = 0, +0x18 = 1, word +0x38 = 100,
//      +0x34 = 0, word +0x28 = 0. race = the owner's race (player +0x4).
//      RemovePileFromTile(+0x10), RemovePileFromTile(+0x12) (the site's
//      material piles).
//   2. Military buildings 0xf / 0x10 / 0x12: +0x38 = the int16 at S3.EXE
//      0x659664 / 0x659668 / 0x65966c (250 / 500 / 1000, hit points?),
//      +0x1a = 1, +0x1c = 0, +0x20 = 0; a soldier tile (x, y) by type and
//      race (0xf: (2,1) (2,1) (0,1) (2,1); 0x10: (-1,2) (-1,2) (0,1) (0,2);
//      0x12: (-1,3) (1,6) (0,3) (0,4) from the position); for a race above
//      3 x and y are the building number (the original reads its argument
//      slot, which the other paths overwrite). UnblockTile(x, y), settler =
//      CreateSettler(x, y, 0x25, owner) with job 0xad and building +0x22 =
//      the building, and building +0x22 = settler.
//   3. Per type (each tested in this order; "door" = +0xa/+0xc, "pile g" =
//      CreatePile(door, g, 0, 0xff) stored in +0xe, "work" = work tile =
//      (+0xe, +0x14) before that): 3 work, pile 2; 2 work, pile 3; 5 work;
//      4 pile 1; 7 pile 5; 8 pile 6; 9 pile 7; 0x25 pile 0x1c; 0x1d/0x38
//      pile 0x19; 0xa pile 0xa; 0xb pile [0x730ef0] (11); 0xc piles
//      (not stored) 0xd at the door, 0xc (-1,0), 0xe (+1,0), 0xf (-1,-1),
//      0x10 (+1,+1), 0x11 (0,-1), 0x20 (0,+1) from the door; 0xd piles 0x12
//      door, 0x13 (-1,0), 0x14 (+1,0); 0x19 work, pile 8; 0x17 pile 0x17;
//      0x15 pile 0x18; 0x18 work, pile 9; 0x11 pile 0x16; 0x14 pile 4;
//      0x1e work, pile 0x1a; 0xe work, pile 0x15; 0x16 pile 0x21; 0x24 work,
//      pile 0x1b; 0x26 pile 0x1d; 0x27 pile 5; 0x28 pile 0x1e; 0x1f, 0x2f,
//      0x30, 0x32: +0x1a, +0x1b, +0x1c, +0x1f, +0x20 = 0; 0x20 the same and
//      +0x22 = 0xffff; 0x22: +0x1b = 0 and six piles CreatePile(position +
//      S3.EXE table 0x6594a4[race][k], 0xff, 0, 0xfc), each with prev +0xa
//      = the building, chained by next +0xc (the last 0), the first in
//      +0xe, then +0x34 = AllocBuildingExtra(owner); 0x21: +0x22 = 0xffff,
//      +0x1b = +0x20 = 0, the same with table 0x659564; 1: DebugLog
//      "BuildingDone: Storage.ProductionPile Good = %u" (+0xe), then six
//      CreatePile(position + (-2,0) (2,0) (-2,-2) (2,2) (0,-2) (0,2), +0xe,
//      0, 0); 0x1a/0x1b/0x1c: +0x19 = byte [0x730f00] (10) * 1/3/10 (low
//      byte); 0x2e, 0x2c, 0x2a, 0x36: +0x1c = 0; 0x2d, 0x2b, 0x29, 0x37:
//      +0x1c = 1, +0x19 = 1, and unless bit 0 of player +0x8 is set: player
//      +0x8c (mana) += 8, +0x90 += 8 and the S3.EXE dword 0x7acfe0[owner *
//      17] += 8 (temples, probably); 0x33 work, pile 0x23; 0x34 pile 0x22;
//      0x35 piles (not stored) 0xb at door (-1,0), 0xa at (+1,0).
//   4. +0x10, +0x12, +0x14 = 0; for k = 0..2: good = S3.EXE 0x6a6824[type
//      * 3 + k]; if not 0: DebugLog "%u. Warenstapel an Gebaude gefunden!
//      Typ = %u" (k + 1, good), pile = CreatePile(position + table
//      0x6a6ad0[(race * 57 + type) * 3 + k], good, 0, 0xfd) (race read from
//      the player again) into +0x10 + 2k, DebugLog "AddBuilding: y = %u - OP1
//      = %u - OP2 = %u - OP3 = %u" (pile, +0x10, +0x12, +0x14).
// Coordinates are computed in 32 bits (word + offset) as in the original.

#include "game/debug_log.h"
#include "game/enums/races.h"
#include "game/enums/settler_types.h"
#include "game/enums/buildings.h"
#include "game/enums/goods.h"
#include "game/enums/piles.h"
#include "game/enums/job_ids.h"
#include "game/globals.h"
#include "game/replacements.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/typed_fields.h"
#include "game/world.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

std::uint32_t __fastcall CGameData_AllocBuildingExtra(CGameData* world, void* edx, std::uint32_t value);

namespace {

using layout::Building;
using layout::Player;

using B = BuildingType;
using G = Good;

// The trade piles of a harbour or marketplace.
constexpr std::uint32_t kStorePiles = 6;
constexpr std::uint16_t kNoDirection = 0xffff;  // Building::dock_direction not chosen yet

// The settler a finished military building gets (type 37 = 0x25, job 173 = 0xad, both placeholders;
// Building::guard keeps it).
constexpr SettlerType kGuardType = SettlerType::Type37;
constexpr SettlerJob kGuardJob = SettlerJob::Job173;

constexpr std::uint16_t kStartHitPoints = 100;
constexpr std::int32_t kTempleMana = 8;
constexpr std::int32_t kPlayerFlagNoTempleMana = 1;  // Player::unk008 bit 0

// The pending event BuildingDone queues first (QueuePendingEvent(1, building, 0)).
constexpr std::uint32_t kEventBuildingDone = 1;

// UnblockTile (verified, area_split.cpp) through its S3.EXE address, so that
// tests can replace its region floods by a recording stub.
using TileFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);
constexpr std::uintptr_t kUnblockTile = 0x0051b770;

std::uint32_t PileAt(CGameData* world, std::uint32_t x, std::uint32_t y, std::uint32_t good, PileKind kind) {
    return CGameData_CreatePile(world, nullptr, x, y, good, 0, Value(kind));
}

std::uint32_t PileAt(CGameData* world, std::uint32_t x, std::uint32_t y, Good good, PileKind kind) {
    return PileAt(world, x, y, static_cast<std::uint32_t>(Value(good)), kind);
}

// The output pile at the door (stored in +0xe by the callers).
std::uint16_t DoorPile(CGameData* world, const Building& building, std::uint32_t good) {
    return static_cast<std::uint16_t>(PileAt(world, building.door_x, building.door_y, good, PileKind::Output));
}

std::uint16_t DoorPile(CGameData* world, const Building& building, Good good) {
    return DoorPile(world, building, static_cast<std::uint32_t>(Value(good)));
}

// An output pile next to the door, not stored in the building.
void PileNextToDoor(CGameData* world, const Building& building, std::int32_t dx, std::int32_t dy, Good good) {
    PileAt(world, building.door_x + static_cast<std::uint32_t>(dx), building.door_y + static_cast<std::uint32_t>(dy),
           good, PileKind::Output);
}

// The work tile becomes the position kept in +0xe/+0x14 during construction.
void WorkFromSite(Building& building) {
    building.work_x = building.pile_ring;
    building.work_y = building.piles[2];
}

// Step 1 (after the pending event): the construction state is reset.
void ResetSite(Building& building) {
    building.buildup = 0;
    building.worker = 0;
    building.work_x = building.x;
    building.work_y = building.y;
    building.site.boards_missing = 0;
    building.site.stones_missing = 0;
    building.site.diggers = 0;
    building.site.builders = 0;
    building.site.unk1e = 0;  // +0x1d (workers_at_work) stays
    building.site.materials_total = 0;
    building.site.materials_used = 0;
    building.request_flags = 1;
    building.hit_points = kStartHitPoints;
    building.trade = 0;
    building.input_amount[2] = 0;
}

// Step 2: towers and castle get their hit points and their guard settler on a tile that depends on type
// and race. For a race above 3 the original reads its argument slot (overwritten by the building number by
// then) as both coordinates; ours uses the building number on purpose.
void FinishMilitaryBuilding(CGameData* world, std::uint32_t buildingId, Building& building, std::uint32_t kind) {
    building.hit_points = static_cast<std::uint16_t>(g_militaryHitPoints[kind]);
    building.military.class1_wanted = 1;
    building.military.class2_wanted = 0;
    building.military.class3_wanted = 0;
    // Guard tile offsets per kind (small tower, large tower, castle) and race (Roman, Egyptian, Asian, Amazon).
    static constexpr std::int32_t kGuardTile[3][kRaces][2] = {
        {{2, 1}, {2, 1}, {0, 1}, {2, 1}},
        {{-1, 2}, {-1, 2}, {0, 1}, {0, 2}},
        {{-1, 3}, {1, 6}, {0, 3}, {0, 4}},
    };
    const std::int32_t race = world->players[building.owner].race;
    std::uint32_t x = buildingId;
    std::uint32_t y = buildingId;
    if (race >= 0 && race < kRaces) {
        x = building.x + static_cast<std::uint32_t>(kGuardTile[kind][race][0]);
        y = building.y + static_cast<std::uint32_t>(kGuardTile[kind][race][1]);
    }
    reinterpret_cast<TileFn>(kUnblockTile)(world, nullptr, x, y);
    const std::uint32_t guard = CGameData_CreateSettler(world, nullptr, x, y, Value(kGuardType), building.owner);
    world->settlers[guard].job = Value(kGuardJob);
    world->settlers[guard].building = static_cast<std::uint16_t>(buildingId);
    building.guard = static_cast<std::uint16_t>(guard);
}

// Six store piles around the building (harbour and marketplace), linked to it.
void CreateStorePiles(CGameData* world, std::uint32_t buildingId, const std::int32_t* offsets) {
    Building& building = world->buildings[buildingId];
    std::uint32_t previous = 0;
    for (std::uint32_t k = 0; k < kStorePiles; ++k) {
        const std::uint32_t pile =
            PileAt(world, building.x + static_cast<std::uint32_t>(offsets[2 * k]),
                   building.y + static_cast<std::uint32_t>(offsets[2 * k + 1]), Good::Empty, PileKind::TradeRing);
        world->piles[pile].prev = static_cast<std::uint16_t>(buildingId);
        if (k == 0) {
            building.pile_ring = static_cast<std::uint16_t>(pile);
        } else {
            world->piles[previous].next = static_cast<std::uint16_t>(pile);
        }
        if (k == kStorePiles - 1) {
            world->piles[pile].next = 0;
        }
        previous = pile;
    }
    building.trade = static_cast<std::uint16_t>(CGameData_AllocBuildingExtra(world, nullptr, building.owner));
}

// A large temple adds mana unless the player's flag bit 0 is set.
void FinishLargeTemple(CGameData* world, Building& building) {
    building.temple.countdown = 1;
    building.temple.points = 1;
    Player& player = world->players[building.owner];
    if ((player.unk008 & kPlayerFlagNoTempleMana) != 0) {
        return;
    }
    player.mana += kTempleMana;
    world->players[building.owner].mana_total += kTempleMana;
    g_playerTallies[building.owner].unk0c += kTempleMana;
}

// The war machine workshops and the large gong (BuildingWorkshopBytes, bytes +0x1a..+0x20).
void ClearWorkshopBytes(Building& building) {
    building.workshop.wanted[0] = 0;
    building.workshop.wanted[1] = 0;
    building.workshop.wanted[2] = 0;
    building.workshop.state = 0;
    building.workshop.extra_tiles = 0;
}

// Step 3: the type's own piles and fields. The original tests the types one after another; each type
// matches exactly one test and nothing in between changes the type, so a switch does the same.
void FinishByType(CGameData* world, std::uint32_t buildingId, Building& building, std::int32_t race) {
    switch (TypeOf(building)) {
    // Producers whose work tile is the position kept during construction.
    case B::Stonecutter: WorkFromSite(building); building.pile_ring = DoorPile(world, building, G::Stone); break;
    case B::Woodcutter: WorkFromSite(building); building.pile_ring = DoorPile(world, building, G::Log); break;
    case B::Forester: WorkFromSite(building); break;
    case B::Fishery: WorkFromSite(building); building.pile_ring = DoorPile(world, building, G::Fish); break;
    case B::Farm: WorkFromSite(building); building.pile_ring = DoorPile(world, building, G::Grain); break;
    case B::Waterworks: WorkFromSite(building); building.pile_ring = DoorPile(world, building, G::Water); break;
    case B::Vineyard: WorkFromSite(building); building.pile_ring = DoorPile(world, building, G::Wine); break;
    case B::RiceFarm: WorkFromSite(building); building.pile_ring = DoorPile(world, building, G::Rice); break;
    case B::Apiary: WorkFromSite(building); building.pile_ring = DoorPile(world, building, G::Honey); break;

    // One output pile at the door.
    case B::Sawmill: building.pile_ring = DoorPile(world, building, G::Board); break;
    case B::CoalMine: building.pile_ring = DoorPile(world, building, G::Coal); break;
    case B::GoldMine: building.pile_ring = DoorPile(world, building, G::GoldOre); break;
    case B::IronMine: building.pile_ring = DoorPile(world, building, G::IronOre); break;
    case B::GemMine: building.pile_ring = DoorPile(world, building, G::Gems); break;
    case B::SulfurMine:
    case B::AmazonSulfurMine: building.pile_ring = DoorPile(world, building, G::Sulfur); break;
    case B::GoldSmelter: building.pile_ring = DoorPile(world, building, G::Gold); break;
    // The iron smelter's good comes from a global (S3.EXE 0x730ef0, 11 = iron).
    case B::IronSmelter:
        building.pile_ring = DoorPile(world, building, static_cast<std::uint32_t>(g_goodType0B));
        break;
    case B::PigFarm: building.pile_ring = DoorPile(world, building, G::Pig); break;
    case B::Slaughterhouse: building.pile_ring = DoorPile(world, building, G::Meat); break;
    case B::Mill: building.pile_ring = DoorPile(world, building, G::Flour); break;
    case B::Bakery: building.pile_ring = DoorPile(world, building, G::Bread); break;
    case B::Distillery: building.pile_ring = DoorPile(world, building, G::RiceLiquor); break;
    case B::Brewery: building.pile_ring = DoorPile(world, building, G::Beer); break;
    case B::CharcoalBurner: building.pile_ring = DoorPile(world, building, G::Coal); break;
    case B::PowderMaker: building.pile_ring = DoorPile(world, building, G::Gunpowder); break;
    case B::Meadery: building.pile_ring = DoorPile(world, building, G::Mead); break;

    // Several output piles around the door, not stored in the building.
    case B::Toolsmith:
        PileNextToDoor(world, building, 0, 0, G::Hammer);
        PileNextToDoor(world, building, -1, 0, G::Shovel);
        PileNextToDoor(world, building, 1, 0, G::Axe);
        PileNextToDoor(world, building, -1, -1, G::Pickaxe);
        PileNextToDoor(world, building, 1, 1, G::Saw);
        PileNextToDoor(world, building, 0, -1, G::FishingRod);
        PileNextToDoor(world, building, 0, 1, G::Scythe);
        break;
    case B::Weaponsmith:
        PileNextToDoor(world, building, 0, 0, G::Sword);
        PileNextToDoor(world, building, -1, 0, G::Bow);
        PileNextToDoor(world, building, 1, 0, G::Spear);
        break;
    case B::Laboratory:
        PileNextToDoor(world, building, -1, 0, G::Iron);
        PileNextToDoor(world, building, 1, 0, G::Gold);
        break;

    case B::CatapultWorkshop:
    case B::BallistaWorkshop:
    case B::CannonWorkshop:
    case B::LargeGong: ClearWorkshopBytes(building); break;
    case B::Shipyard:
        ClearWorkshopBytes(building);
        building.dock_direction = kNoDirection;
        break;

    // Trade buildings: a ring of six store piles from the race's table (S3.EXE 0x6594a4, 0x659564).
    case B::Marketplace:
        building.trading.goods_cursor = 0;
        CreateStorePiles(world, buildingId,
                         &g_storePileOffsets22[0][0][0] + static_cast<std::uint32_t>(race) * kStorePiles * 2);
        break;
    case B::Harbour:
        building.dock_direction = kNoDirection;
        building.trading.goods_cursor = 0;
        building.trading.call_countdown = 0;
        CreateStorePiles(world, buildingId,
                         &g_storePileOffsets21[0][0][0] + static_cast<std::uint32_t>(race) * kStorePiles * 2);
        break;

    // The storehouse: six piles of its production good around it.
    case B::Storehouse: {
        GameDebugLog(texts::kBuildingDoneProductionPile, static_cast<std::uint32_t>(building.pile_ring));
        static constexpr std::int32_t kAround[kStorePiles][2] = {{-2, 0}, {2, 0}, {-2, -2}, {2, 2}, {0, -2}, {0, 2}};
        for (const auto& d : kAround) {
            PileAt(world, building.x + static_cast<std::uint32_t>(d[0]), building.y + static_cast<std::uint32_t>(d[1]),
                   building.pile_ring, PileKind::Storehouse);
        }
        break;
    }

    // Residences: births = the global unit (S3.EXE 0x730f00, 10) times 1, 3 or 10, in a byte.
    case B::SmallResidence: building.residence.births = static_cast<std::uint8_t>(g_residenceUnit); break;
    case B::MediumResidence: building.residence.births = static_cast<std::uint8_t>(g_residenceUnit * 3); break;
    case B::LargeResidence: building.residence.births = static_cast<std::uint8_t>(g_residenceUnit * 10); break;

    case B::SmallPagoda:
    case B::SmallTemple:
    case B::Sphinx:
    case B::AmazonSmallTemple: building.temple.countdown = 0; break;
    case B::LargePagoda:
    case B::LargeTemple:
    case B::Pyramid:
    case B::AmazonLargeTemple: FinishLargeTemple(world, building); break;

    default: break;
    }
}

// Step 4: the input piles from the type's table (S3.EXE 0x6a6824 goods, 0x6a6ad0 offsets per race and type).
void CreateInputPiles(CGameData* world, Building& building) {
    building.piles[0] = 0;
    building.piles[1] = 0;
    building.piles[2] = 0;
    for (std::uint32_t k = 0; k <= 2; ++k) {
        const std::int32_t good = g_buildingInputGoods[building.art][k];
        if (good == 0) {
            continue;
        }
        GameDebugLog(texts::kPileFoundAtBuilding, k + 1, static_cast<std::uint32_t>(good));
        // The race is read from the player again.
        const std::uint32_t race = static_cast<std::uint32_t>(world->players[building.owner].race);
        const std::uint32_t entry = (race * kBuildingTypeCount + building.art) * 3 + k;
        const std::int32_t* offset = g_buildingInputOffsets[entry];
        const std::uint32_t pile = PileAt(world, building.x + static_cast<std::uint32_t>(offset[0]),
                                          building.y + static_cast<std::uint32_t>(offset[1]),
                                          static_cast<std::uint32_t>(good), PileKind::Order);
        building.piles[k] = static_cast<std::uint16_t>(pile);
        GameDebugLog(texts::kAddBuilding, pile, static_cast<std::uint32_t>(building.piles[0]),
                     static_cast<std::uint32_t>(building.piles[1]), static_cast<std::uint32_t>(building.piles[2]));
    }
}

}  // namespace

std::uint32_t __fastcall CGameData_RemovePileFromTile(CGameData* world, void* edx, std::uint32_t pile);

using BuildingDoneFn = void(__fastcall*)(CGameData* world, void* edx, std::uint32_t building);

// Purpose: Turns a finished construction site into a working building: resets
// its construction state, puts the garrison's first soldier into a military
// building, and creates the building's goods piles (output, input and
// storage piles, by type and race). CGameData::BuildingDone (0x4dbc50).
void __fastcall CGameData_BuildingDone(CGameData* world, void*, std::uint32_t buildingId) {
    CGameData_QueuePendingEvent(world, nullptr, kEventBuildingDone, buildingId, 0);
    Building& building = world->buildings[buildingId];
    ResetSite(building);
    const std::int32_t race = world->players[building.owner].race;
    CGameData_RemovePileFromTile(world, nullptr, building.piles[0]);  // the site's material piles
    CGameData_RemovePileFromTile(world, nullptr, building.piles[1]);

    switch (TypeOf(building)) {
    case B::SmallTower: FinishMilitaryBuilding(world, buildingId, building, 0); break;
    case B::LargeTower: FinishMilitaryBuilding(world, buildingId, building, 1); break;
    case B::Castle: FinishMilitaryBuilding(world, buildingId, building, 2); break;
    default: break;
    }
    FinishByType(world, buildingId, building, race);
    CreateInputPiles(world, building);
}

S3RE_HOOK(0x004dbc50, CGameData_BuildingDone, BuildingDoneFn, Original_CGameData_BuildingDone, CGameData_BuildingDone);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kTestWorld = offsetof(CGameData, buildings) + 64 * sizeof(Building);
constexpr std::size_t kCountersBytes = sizeof(g_playerTallies.get());

// A building 1..50 of a random type (mostly 0..0x38) of player 0..3, with
// sane material piles (RemovePileFromTile clears their tiles), positions
// in the map, a small pending-event ring index; the players' races mostly
// 0..3 and the temple flag bit random. UnblockTile is a recording stub; the
// tallies' counters (PlayerTally::unk0c on) are compared and restored per run.
void CompareBuildingDone(test::Context& ctx, BuildingDoneFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    test::CalleeStubs stubs;
    stubs.Install<2, kUnblockTile>([](CGameData* w, std::uint32_t, const std::uint32_t* args) {
        TileAtOffset(w, TileOffset(args[0], args[1])).flags &= 0xfe;
        return 0u;
    });
    DebugLogRecorder logs;
    std::vector<std::uint8_t> saved(kCountersBytes), afterOriginal(kCountersBytes);
    void* counters = &g_playerTallies[0].unk0c;  // (to 12 bytes past the tallies, as before)
    std::memcpy(saved.data(), counters, kCountersBytes);
    worlds.Both([&](CGameData* w) {
        w->map_size = 384;
        w->settler_high_water = 100;
        w->pile_high_water = 300;
        w->trade_high_water = 50;
    });
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t number = 1 + ctx.rng().Below(50);
        const auto type = static_cast<std::uint8_t>(ctx.rng().Chance(9, 10) ? ctx.rng().Below(0x39) : ctx.rng().Below(0x40));
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Below(4));
        const auto x = static_cast<std::uint16_t>(30 + ctx.rng().Below(320));
        const auto y = static_cast<std::uint16_t>(30 + ctx.rng().Below(320));
        const auto dx = static_cast<std::uint16_t>(x + ctx.rng().Below(5) - 2);
        const auto dy = static_cast<std::uint16_t>(y + ctx.rng().Below(5));
        const auto site0 = static_cast<std::uint16_t>(type == 1 ? ctx.rng().Below(40) : ctx.rng().Next32());
        const auto site2 = static_cast<std::uint16_t>(ctx.rng().Next32());
        const auto pile0 = static_cast<std::uint16_t>(1 + ctx.rng().Below(250));
        const auto pile1 = static_cast<std::uint16_t>(1 + ctx.rng().Below(250));
        std::int32_t races[4];
        std::int32_t flags[4];
        for (int p = 0; p < 4; ++p) {
            // Races above 3 only reach the soldier tile of military
            // buildings (types 0xf/0x10/0x12 have no table-driven piles);
            // elsewhere they would index past the race tables.
            const bool military = type == 0xf || type == 0x10 || type == 0x12;
            races[p] = military && ctx.rng().Chance(1, 5) ? static_cast<std::int32_t>(ctx.rng().Below(8))
                                                         : static_cast<std::int32_t>(ctx.rng().Below(4));
            flags[p] = static_cast<std::int32_t>(ctx.rng().Next32());
        }
        const auto read = ctx.rng().Below(1000);
        const auto write = ctx.rng().Below(1000);
        const auto px0 = static_cast<std::uint16_t>(20 + ctx.rng().Below(340));
        const auto py0 = static_cast<std::uint16_t>(20 + ctx.rng().Below(340));
        worlds.Both([&](CGameData* w) {
            Building& b = w->buildings[number];
            b.art = type;
            b.owner = owner;
            b.x = x;
            b.y = y;
            b.door_x = dx;
            b.door_y = dy;
            b.pile_ring = site0;
            b.piles[0] = pile0;
            b.piles[1] = pile1;
            b.piles[2] = site2;
            for (int p = 0; p < 4; ++p) {
                w->players[p].race = races[p];
                w->players[p].unk008 = flags[p];
            }
            w->piles[pile0].x = px0;
            w->piles[pile0].y = py0;
            w->piles[pile1].x = static_cast<std::uint16_t>(py0);
            w->piles[pile1].y = static_cast<std::uint16_t>(px0);
            w->pending_event_index.read = read;
            w->pending_event_index.write = write;
        });
        // Economy areas 1..50 on the tiles around (piles link into them).
        for (std::uint32_t tx = x - 12u; tx != x + 13u; ++tx) {
            for (std::uint32_t ty = y - 12u; ty != y + 13u; ++ty) {
                const auto area = static_cast<std::uint16_t>(1 + ctx.rng().Below(50));
                worlds.Both([&](CGameData* w) { w->tiles[tx][ty].area = area; });
            }
        }
        stubs.SetWorld(worlds.original());
        Original_CGameData_BuildingDone(worlds.original(), nullptr, number);
        const std::vector<test::StubCall> expectedCalls = stubs.Take();
        const std::vector<DebugLogCall> expectedLogs = logs.Take();
        std::memcpy(afterOriginal.data(), counters, kCountersBytes);
        std::memcpy(counters, saved.data(), kCountersBytes);
        stubs.SetWorld(worlds.ours());
        candidate(worlds.ours(), nullptr, number);
        const std::vector<test::StubCall> actualCalls = stubs.Take();
        const std::vector<DebugLogCall> actualLogs = logs.Take();
        const bool countersSame = std::memcmp(afterOriginal.data(), counters, kCountersBytes) == 0;
        std::memcpy(counters, saved.data(), kCountersBytes);
        if (!S3RE_EXPECT(expectedCalls == actualCalls) || !S3RE_EXPECT(expectedLogs == actualLogs) ||
            !S3RE_EXPECT(countersSame) || ((i % 32 == 31 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 31 before it): building %u type 0x%x race %d", i,
                     number, type, races[owner]);
            return;
        }
    }
}

// Wrong: a large temple always adds its mana.
void __fastcall WrongBuildingDone(CGameData* world, void* edx, std::uint32_t number) {
    Player& player = world->players[world->buildings[number].owner];
    const std::int32_t saved = player.unk008;
    player.unk008 &= ~1;
    CGameData_BuildingDone(world, edx, number);
    player.unk008 = saved;
}

}  // namespace

S3RE_TEST(CGameData_BuildingDone_MatchesOriginal) {
    CompareBuildingDone(ctx, &CGameData_BuildingDone, 6000);
}
S3RE_TEST(CGameData_BuildingDone_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareBuildingDone(probe, &WrongBuildingDone, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
