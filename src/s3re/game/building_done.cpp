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
#include "game/globals.h"
#include "game/raw.h"
#include "game/replacements.h"
#include "game/tables.h"
#include "game/texts.h"
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



constexpr std::uint8_t kPileOutput = 0xff;
constexpr std::uint8_t kPileStore = 0xfc;
constexpr std::uint8_t kPileInput = 0xfd;
constexpr std::uint16_t kSoldierJob = 0xad;
constexpr std::uint32_t kSoldierType = 0x25;

// UnblockTile (verified, area_split.cpp) through its S3.EXE address, so that
// tests can replace its region floods by a recording stub.
using TileFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);
constexpr std::uintptr_t kUnblockTile = 0x0051b770;

std::uint32_t PileAt(CGameData* world, std::uint32_t x, std::uint32_t y, std::uint32_t good, std::uint32_t kind) {
    return CGameData_CreatePile(world, nullptr, x, y, good, 0, kind);
}

// The output pile at the door (stored in +0xe by the callers).
std::uint16_t DoorPile(CGameData* world, const Building& b, std::uint32_t good) {
    return static_cast<std::uint16_t>(PileAt(world, b.door_x, b.door_y, good, kPileOutput));
}

// The work tile becomes the position kept in +0xe/+0x14 during construction.
void WorkFromSite(Building& b) {
    b.work_x = b.pile_ring;
    b.work_y = b.piles[2];
}

// Six store piles around the building (types 0x21/0x22), linked to it.
void CreateStorePiles(CGameData* world, std::uint32_t number, const std::int32_t* offsets) {
    Building& b = world->buildings[number];
    std::uint32_t previous = 0;
    for (std::uint32_t k = 0; k < 6; ++k) {
        const std::uint32_t pile = PileAt(world, b.x + static_cast<std::uint32_t>(offsets[2 * k]),
                                          b.y + static_cast<std::uint32_t>(offsets[2 * k + 1]), 0xff, kPileStore);
        world->piles[pile].prev = static_cast<std::uint16_t>(number);
        if (k == 0) {
            b.pile_ring = static_cast<std::uint16_t>(pile);
        } else {
            world->piles[previous].next = static_cast<std::uint16_t>(pile);
        }
        if (k == 5) {
            world->piles[pile].next = 0;
        }
        previous = pile;
    }
    b.trade = static_cast<std::uint16_t>(CGameData_AllocBuildingExtra(world, nullptr, b.owner));
}

// Temples (probably): one more priest/mana source unless the player's flag
// bit 0 is set.
void FinishTemple(CGameData* world, Building& b) {
    b.temple.countdown = 1;
    b.temple.points = 1;
    Player& player = world->players[b.owner];
    if ((player.unk008 & 1) != 0) {
        return;
    }
    player.mana += 8;
    world->players[b.owner].mana_total += 8;
    g_playerTallies[b.owner].unk0c += 8;
}

void ClearFlags1aTo20(Building& b) {
    b.water.wanted[0] = 0;
    b.water.wanted[1] = 0;
    b.water.wanted[2] = 0;
    b.water.state = 0;
    b.water.extra_tiles = 0;
}

}  // namespace

std::uint32_t __fastcall CGameData_RemovePileFromTile(CGameData* world, void* edx, std::uint32_t pile);

using BuildingDoneFn = void(__fastcall*)(CGameData* world, void* edx, std::uint32_t building);

// Purpose: Turns a finished construction site into a working building: resets
// its construction state, puts the garrison's first soldier into a military
// building, and creates the building's goods piles (output, input and
// storage piles, by type and race).
void __fastcall CGameData_BuildingDone(CGameData* world, void*, std::uint32_t number) {
    CGameData_QueuePendingEvent(world, nullptr, 1, number, 0);
    Building& b = world->buildings[number];
    b.buildup = 0;
    b.worker = 0;
    b.work_x = b.x;
    b.work_y = b.y;
    b.site.boards_missing = 0;
    b.site.stones_missing = 0;
    b.site.diggers = 0;
    b.site.builders = 0;
    b.site.unk1e = 0;  // +0x1d (workers_at_work) stays
    b.site.materials_total = 0;
    b.site.materials_used = 0;
    b.request_flags = 1;
    b.hit_points = 100;
    b.trade = 0;
    b.input_amount[2] = 0;
    const std::int32_t race = world->players[b.owner].race;
    CGameData_RemovePileFromTile(world, nullptr, b.piles[0]);
    CGameData_RemovePileFromTile(world, nullptr, b.piles[1]);

    const std::uint8_t type = b.art;
    if (type == 0xf || type == 0x10 || type == 0x12) {
        const std::uint32_t index = type == 0xf ? 0 : type == 0x10 ? 1 : 2;
        b.hit_points = static_cast<std::uint16_t>(g_militaryHitPoints[index]);
        b.military.class1_wanted = 1;
        b.military.class2_wanted = 0;
        b.military.class3_wanted = 0;
        // Soldier tile offsets per type and race (races 0..3).
        static constexpr std::int32_t kSoldierTile[3][4][2] = {
            {{2, 1}, {2, 1}, {0, 1}, {2, 1}},
            {{-1, 2}, {-1, 2}, {0, 1}, {0, 2}},
            {{-1, 3}, {1, 6}, {0, 3}, {0, 4}},
        };
        const std::int32_t playerRace = world->players[b.owner].race;
        std::uint32_t x = number;
        std::uint32_t y = number;
        if (playerRace >= 0 && playerRace <= 3) {
            x = b.x + static_cast<std::uint32_t>(kSoldierTile[index][playerRace][0]);
            y = b.y + static_cast<std::uint32_t>(kSoldierTile[index][playerRace][1]);
        }
        reinterpret_cast<TileFn>(kUnblockTile)(world, nullptr, x, y);
        const std::uint32_t soldier = CGameData_CreateSettler(world, nullptr, x, y, kSoldierType, b.owner);
        world->settlers[soldier].job = kSoldierJob;
        world->settlers[soldier].building = static_cast<std::uint16_t>(number);
        b.guard = static_cast<std::uint16_t>(soldier);
    }

    // Output piles at the door, in the original's order of tests.
    struct DoorOutput {
        std::uint8_t type;
        std::uint8_t good;
        bool work;  // the work tile comes from the site's stored position first
    };
    static constexpr DoorOutput kFirst[] = {{3, 2, true}, {2, 3, true}};
    for (const DoorOutput& o : kFirst) {
        if (b.art == o.type) {
            WorkFromSite(b);
            b.pile_ring = DoorPile(world, b, o.good);
        }
    }
    if (b.art == 5) {
        WorkFromSite(b);
    }
    static constexpr DoorOutput kSecond[] = {{4, 1, false}, {7, 5, false}, {8, 6, false}, {9, 7, false},
                                             {0x25, 0x1c, false}};
    for (const DoorOutput& o : kSecond) {
        if (b.art == o.type) {
            b.pile_ring = DoorPile(world, b, o.good);
        }
    }
    if (b.art == 0x1d || b.art == 0x38) {
        b.pile_ring = DoorPile(world, b, 0x19);
    }
    if (b.art == 0xa) {
        b.pile_ring = DoorPile(world, b, 0xa);
    }
    if (b.art == 0xb) {
        b.pile_ring = DoorPile(world, b, static_cast<std::uint32_t>(g_goodType0B));
    }
    if (b.art == 0xc) {
        PileAt(world, b.door_x, b.door_y, 0xd, kPileOutput);
        PileAt(world, b.door_x - 1, b.door_y, 0xc, kPileOutput);
        PileAt(world, b.door_x + 1, b.door_y, 0xe, kPileOutput);
        PileAt(world, b.door_x - 1, b.door_y - 1, 0xf, kPileOutput);
        PileAt(world, b.door_x + 1, b.door_y + 1, 0x10, kPileOutput);
        PileAt(world, b.door_x, b.door_y - 1, 0x11, kPileOutput);
        PileAt(world, b.door_x, b.door_y + 1, 0x20, kPileOutput);
    }
    if (b.art == 0xd) {
        PileAt(world, b.door_x, b.door_y, 0x12, kPileOutput);
        PileAt(world, b.door_x - 1, b.door_y, 0x13, kPileOutput);
        PileAt(world, b.door_x + 1, b.door_y, 0x14, kPileOutput);
    }
    static constexpr DoorOutput kThird[] = {
        {0x19, 8, true},     {0x17, 0x17, false}, {0x15, 0x18, false}, {0x18, 9, true},   {0x11, 0x16, false},
        {0x14, 4, false},    {0x1e, 0x1a, true},  {0xe, 0x15, true},   {0x16, 0x21, false}, {0x24, 0x1b, true},
        {0x26, 0x1d, false}, {0x27, 5, false},    {0x28, 0x1e, false},
    };
    for (const DoorOutput& o : kThird) {
        if (b.art == o.type) {
            if (o.work) {
                WorkFromSite(b);
            }
            b.pile_ring = DoorPile(world, b, o.good);
        }
    }
    if (b.art == 0x1f || b.art == 0x2f || b.art == 0x30 || b.art == 0x32) {
        ClearFlags1aTo20(b);
    }
    if (b.art == 0x20) {
        ClearFlags1aTo20(b);
        b.dock_direction = 0xffff;
    }
    if (b.art == 0x22) {
        b.trading.goods_cursor = 0;
        CreateStorePiles(world, number, &g_storePileOffsets22[0][0][0] + static_cast<std::uint32_t>(race) * 12);
    }
    if (b.art == 0x21) {
        b.dock_direction = 0xffff;
        b.trading.goods_cursor = 0;
        b.trading.call_countdown = 0;
        CreateStorePiles(world, number, &g_storePileOffsets21[0][0][0] + static_cast<std::uint32_t>(race) * 12);
    }
    if (b.art == 1) {
        GameDebugLog(texts::kBuildingDoneProductionPile, static_cast<std::uint32_t>(b.pile_ring));
        static constexpr std::int32_t kAround[6][2] = {{-2, 0}, {2, 0}, {-2, -2}, {2, 2}, {0, -2}, {0, 2}};
        for (const auto& d : kAround) {
            PileAt(world, b.x + static_cast<std::uint32_t>(d[0]), b.y + static_cast<std::uint32_t>(d[1]), b.pile_ring,
                   0);
        }
    }
    const std::int8_t unit = g_residenceUnit;
    if (b.art == 0x1a) {
        b.residence.births = static_cast<std::uint8_t>(unit);
    }
    if (b.art == 0x1b) {
        b.residence.births = static_cast<std::uint8_t>(unit * 3);
    }
    if (b.art == 0x1c) {
        b.residence.births = static_cast<std::uint8_t>(unit * 10);
    }
    static constexpr std::uint8_t kTemples[4][2] = {{0x2e, 0x2d}, {0x2c, 0x2b}, {0x2a, 0x29}, {0x36, 0x37}};
    for (const auto& t : kTemples) {
        if (b.art == t[0]) {
            b.temple.countdown = 0;
        }
        if (b.art == t[1]) {
            FinishTemple(world, b);
        }
    }
    if (b.art == 0x33) {
        WorkFromSite(b);
        b.pile_ring = DoorPile(world, b, 0x23);
    }
    if (b.art == 0x34) {
        b.pile_ring = DoorPile(world, b, 0x22);
    }
    if (b.art == 0x35) {
        PileAt(world, b.door_x - 1, b.door_y, 0xb, kPileOutput);
        PileAt(world, b.door_x + 1, b.door_y, 0xa, kPileOutput);
    }

    // Input piles from the type's table.
    b.piles[0] = 0;
    b.piles[1] = 0;
    b.piles[2] = 0;
    for (std::uint32_t k = 0; k <= 2; ++k) {
        const std::int32_t good = g_buildingInputGoods[b.art][k];
        if (good == 0) {
            continue;
        }
        GameDebugLog(texts::kPileFoundAtBuilding, k + 1, static_cast<std::uint32_t>(good));
        const std::uint32_t entry =
            (static_cast<std::uint32_t>(world->players[b.owner].race) * 57 + b.art) * 3 + k;
        const std::int32_t* offset = g_buildingInputOffsets[entry];
        const std::uint32_t pile = PileAt(world, b.x + static_cast<std::uint32_t>(offset[0]),
                                          b.y + static_cast<std::uint32_t>(offset[1]),
                                          static_cast<std::uint32_t>(good), kPileInput);
        b.piles[k] = static_cast<std::uint16_t>(pile);
        GameDebugLog(texts::kAddBuilding, pile, static_cast<std::uint32_t>(b.piles[0]),
                     static_cast<std::uint32_t>(b.piles[1]), static_cast<std::uint32_t>(b.piles[2]));
    }
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

// Wrong: a temple always adds its mana.
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
