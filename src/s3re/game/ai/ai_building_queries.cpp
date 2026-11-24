// Computer-player queries over the building records (computer players A2,
// session 52; Rainer\CompMapWorkerAgent.cpp and compGlobs.cpp;
// re/computer_players.md).
//
//   0x0059db30 AI::CountSitesOfType(player, type, area) fastcall (const u8*
//              player, const u8* type, stack const u16* area; ret 4): the
//              player's construction sites (Building::buildup != 0) of that
//              type, walking the ids through AI_GetBuilding: for *area 0
//              those whose door area (AI_BuildingArea) is 0, for any other
//              *area all of them (original quirk, kept: `area == door area
//              || area != 0`, probably meant as "0 = any area"; the door
//              area is still looked up, *area is re-read per site). AX = the
//              count; EAX's upper
//              half is that of GetBuilding's end flag (reproduced; the
//              caller 0x568890 tests AX).
//   0x0058b410 AI::FirstBuildingInArea(player, area) stdcall: the first
//              finished building (buildup 0) of the player (owner byte
//              against the whole dword) whose door tile's area word equals
//              the whole dword `area`, over ids 1..building_high_water of
//              g_aiMapWorld (unsigned, so a high-water mark of -1 checks
//              none); its id, else -1. Caller AI::Army::AssignSoldier
//              0x592820.
//   0x0058b380 AI::FirstMilitaryBuildingInArea(player, area) stdcall: the
//              same for a small tower, large tower or castle.
//   0x00573810 AI::MineSuccessPercent(const u16* building) fastcall: -1 if
//              the building (g_aiWorld) has no worker or no digs counted
//              yet (BuildingProductionBytes::digs); else the finds among
//              the last digs (the bits of the 32-bit dig history
//              work_y:work_x, MinerJobDig) * 100 / digs, unsigned.
//              Caller CCentrumCheckAgent::vf08 0x560e00 (compares it with
//              a threshold to give up a mine).

#include "game/ai/ai_layout.h"
#include "game/enums/buildings.h"
#include "game/globals.h"
#include "game/layout.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace s3re::game {

using namespace ai;
using layout::Building;
using layout::kMapRowTiles;
using layout::Tile;

namespace {

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kGetBuilding = 0x0058e170;   // AI_GetBuilding
constexpr std::uintptr_t kBuildingArea = 0x00599610;  // AI_BuildingArea

constexpr std::uint32_t kNotFound = 0xffffffffu;
constexpr std::uint16_t kAreaZero = 0;
constexpr std::uint32_t kHistoryBits = 16;  // per word of the dig history
constexpr std::uint32_t kPercent = 100;

const Building* GetBuilding(std::uint32_t id, std::uint32_t* error) {
    using Fn = const Building*(__fastcall*)(std::uint32_t id, std::uint32_t* error);
    return reinterpret_cast<Fn>(kGetBuilding)(id, error);
}

std::uint16_t BuildingArea(const std::uint32_t* id) {
    using Fn = std::uint32_t(__fastcall*)(const std::uint32_t* id);
    return static_cast<std::uint16_t>(reinterpret_cast<Fn>(kBuildingArea)(id));
}

// The area word of a building's door tile, addressed like the original
// (x * 0x300 + y, no bounds check).
std::uint16_t DoorArea(const CGameData* world, const Building& building) {
    const Tile* tiles = &world->tiles[0][0];
    return tiles[building.door_x * kMapRowTiles + building.door_y].area;
}

bool IsMilitary(std::uint8_t type) {
    return type == Value(BuildingType::SmallTower) || type == Value(BuildingType::LargeTower) ||
           type == Value(BuildingType::Castle);
}

// 0x58b410 / 0x58b380: the first finished building of the player in the
// area (optionally only military ones).
std::uint32_t FirstBuildingInArea(std::uint32_t player, std::uint32_t area, bool militaryOnly) {
    const CGameData* world = g_aiMapWorld;
    const std::uint32_t end = world->building_high_water + 1;  // unsigned, may wrap to 0
    for (std::uint32_t id = 1; id < end; ++id) {
        const Building& building = world->buildings[id];  // (not range-checked, like the original)
        if (building.owner != player || building.buildup != 0 || DoorArea(world, building) != area) {
            continue;
        }
        if (!militaryOnly || IsMilitary(building.art)) {
            return id;
        }
    }
    return kNotFound;
}

std::uint32_t CountBits(std::uint16_t bits) {
    std::uint32_t count = 0;
    for (std::uint32_t bit = 0; bit < kHistoryBits; ++bit) {
        if ((bits & (1u << bit)) != 0) {
            ++count;
        }
    }
    return count;
}

}  // namespace

using CountSitesOfTypeFn = std::uint32_t(__fastcall*)(const std::uint8_t* player, const std::uint8_t* type,
                                                      const std::uint16_t* area);
using FirstBuildingFn = std::uint32_t(__stdcall*)(std::uint32_t player, std::uint32_t area);
using MineSuccessFn = std::uint32_t(__fastcall*)(const std::uint16_t* building);

std::uint32_t __fastcall AI_CountSitesOfType(const std::uint8_t* player, const std::uint8_t* type,
                                             const std::uint16_t* area);
std::uint32_t __stdcall AI_FirstBuildingInArea(std::uint32_t player, std::uint32_t area);
std::uint32_t __stdcall AI_FirstMilitaryBuildingInArea(std::uint32_t player, std::uint32_t area);
std::uint32_t __fastcall AI_MineSuccessPercent(const std::uint16_t* building);

S3RE_HOOK(0x0059db30, AI_CountSitesOfType, CountSitesOfTypeFn, Original_AI_CountSitesOfType, AI_CountSitesOfType);
S3RE_HOOK(0x0058b410, AI_FirstBuildingInArea, FirstBuildingFn, Original_AI_FirstBuildingInArea,
          AI_FirstBuildingInArea);
S3RE_HOOK(0x0058b380, AI_FirstMilitaryBuildingInArea, FirstBuildingFn, Original_AI_FirstMilitaryBuildingInArea,
          AI_FirstMilitaryBuildingInArea);
S3RE_HOOK(0x00573810, AI_MineSuccessPercent, MineSuccessFn, Original_AI_MineSuccessPercent, AI_MineSuccessPercent);

// Purpose: How many construction sites of a building type a player has
// (in economy area 0 when asked for area 0, else anywhere: an original
// quirk), for the economy agent's residence condition.
std::uint32_t __fastcall AI_CountSitesOfType(const std::uint8_t* player, const std::uint8_t* type,
                                             const std::uint16_t* area) {
    std::uint32_t error = 0;
    std::uint32_t count = 0;
    std::uint32_t id = 0;
    do {
        ++id;
        const Building* building = GetBuilding(id, &error);
        if (building == nullptr || building->owner != *player || building->buildup == 0 ||
            building->art != *type) {
            continue;
        }
        const std::uint16_t wanted = *area;  // re-read per site, as the original
        const std::uint32_t number = id;
        // Original quirk: a non-zero area counts every site (the test looks
        // inverted; "area 0 = anywhere" was probably meant).
        if (wanted == BuildingArea(&number) || wanted != kAreaZero) {
            ++count;
        }
    } while (error == 0);
    return (error & 0xffff0000u) | (count & 0xffff);
}

// Purpose: A finished building of a computer player in an economy area
// (the first by id), where its soldiers are assigned to.
std::uint32_t __stdcall AI_FirstBuildingInArea(std::uint32_t player, std::uint32_t area) {
    return FirstBuildingInArea(player, area, false);
}

// Purpose: A finished military building (tower or castle) of a computer
// player in an economy area (the first by id), where its soldiers gather.
std::uint32_t __stdcall AI_FirstMilitaryBuildingInArea(std::uint32_t player, std::uint32_t area) {
    return FirstBuildingInArea(player, area, true);
}

// Purpose: How successful a mine's recent digs were (finds per 100 digs),
// for the computer players' decision to give a mine up; -1 while there is
// nothing to judge.
std::uint32_t __fastcall AI_MineSuccessPercent(const std::uint16_t* id) {
    const Building& mine = g_aiWorld.get()->buildings[*id];
    if (mine.worker == 0) {
        return kNotFound;
    }
    const std::uint8_t digs = mine.production.digs;
    if (digs == 0) {
        return kNotFound;
    }
    const std::uint32_t finds = CountBits(mine.work_x) + CountBits(mine.work_y);
    return finds * kPercent / digs;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// One world for both sides (the functions only read it), set up per case:
// a few dozen building records with owners, types, buildup and door tiles
// whose area words often match the asked area.

namespace {

struct BuildingQueries {
    CountSitesOfTypeFn countSites;
    FirstBuildingFn first, firstMilitary;
    MineSuccessFn mineSuccess;
};

BuildingQueries OriginalQueries() {
    return {Original_AI_CountSitesOfType, Original_AI_FirstBuildingInArea, Original_AI_FirstMilitaryBuildingInArea,
            Original_AI_MineSuccessPercent};
}

BuildingQueries OurQueries() {
    return {&AI_CountSitesOfType, &AI_FirstBuildingInArea, &AI_FirstMilitaryBuildingInArea,
            &AI_MineSuccessPercent};
}

struct SavedGlobals {
    CGameData* world = g_aiWorld;
    CGameData* mapWorld = g_aiMapWorld;
    std::uint16_t sizeX = g_aiMapSizeX, sizeY = g_aiMapSizeY;
    ~SavedGlobals() {
        g_aiWorld = world;
        g_aiMapWorld = mapWorld;
        g_aiMapSizeX = sizeX;
        g_aiMapSizeY = sizeY;
    }
};

constexpr std::uint32_t kMapSize = 400;
constexpr std::uint8_t kTypes[] = {1, 7, 15, 16, 17, 18, 19, 14};

void SetUpBuildings(test::Rng& rng, CGameData* world, std::uint32_t count) {
    world->building_high_water = count;
    for (std::uint32_t id = 1; id <= count + 2; ++id) {
        Building& building = world->buildings[id];
        building.owner = static_cast<std::uint8_t>(rng.Chance(1, 6) ? 0xff : rng.Below(3));
        building.art = rng.Chance(3, 4) ? kTypes[rng.Below(sizeof(kTypes))] : static_cast<std::uint8_t>(rng.Next32());
        building.buildup = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : rng.Next32());
        building.door_x = static_cast<std::uint16_t>(rng.Chance(1, 10) ? 500 : rng.Below(kMapSize));
        building.door_y = static_cast<std::uint16_t>(rng.Below(kMapSize));
        building.worker = static_cast<std::uint16_t>(rng.Chance(1, 4) ? 0 : rng.Next32());
        building.production.digs = static_cast<std::uint8_t>(rng.Chance(1, 4) ? 0 : rng.Below(40));
        building.work_x = static_cast<std::uint16_t>(rng.Next32());
        building.work_y = static_cast<std::uint16_t>(rng.Chance(1, 3) ? 0 : rng.Next32());
        world->tiles[building.door_x % kMapRowTiles][building.door_y].area =
            static_cast<std::uint16_t>(rng.Chance(3, 4) ? rng.Below(4) : rng.Next32());
    }
}

void CompareBuildingQueries(test::Context& ctx, const BuildingQueries& candidate, int cases) {
    const BuildingQueries original = OriginalQueries();
    SavedGlobals saved;
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    g_aiWorld = world;
    g_aiMapWorld = world;
    g_aiMapSizeX = kMapSize;
    g_aiMapSizeY = kMapSize;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        const std::uint32_t count = rng.Chance(1, 20) ? 0 : rng.Below(40);
        SetUpBuildings(rng, world, count);
        const int op = static_cast<int>(rng.Below(4));
        std::uint32_t ra = 0, rb = 0, mask = 0xffffffffu;
        switch (op) {
        case 0: {
            const auto player = static_cast<std::uint8_t>(rng.Below(3));
            const std::uint8_t type = kTypes[rng.Below(sizeof(kTypes))];
            const auto area = static_cast<std::uint16_t>(rng.Below(4));
            ra = original.countSites(&player, &type, &area);
            rb = candidate.countSites(&player, &type, &area);
            break;
        }
        case 1:
        case 2: {
            if (rng.Chance(1, 30)) {
                world->building_high_water = 0xffffffffu;  // no building at all (the end wraps to 0)
            }
            const std::uint32_t player = rng.Chance(9, 10) ? rng.Below(3) : rng.Next32();
            const std::uint32_t area = rng.Chance(9, 10) ? rng.Below(4) : rng.Next32();
            const FirstBuildingFn fa = op == 1 ? original.first : original.firstMilitary;
            const FirstBuildingFn fb = op == 1 ? candidate.first : candidate.firstMilitary;
            ra = fa(player, area);
            rb = fb(player, area);
            break;
        }
        default: {
            const auto id = static_cast<std::uint16_t>(1 + rng.Below(count + 2));
            ra = original.mineSuccess(&id);
            rb = candidate.mineSuccess(&id);
            break;
        }
        }
        if (!S3RE_EXPECT_EQ(ra & mask, rb & mask)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d, %u buildings", i, op, count);
        }
    }
}

std::uint32_t __fastcall WrongCountSites(const std::uint8_t* player, const std::uint8_t* type,
                                         const std::uint16_t* area) {
    // Wrong: the probably intended test (area 0 = any area, else only that
    // area).
    std::uint32_t error = 0;
    std::uint32_t count = 0;
    std::uint32_t id = 0;
    do {
        ++id;
        const Building* building = GetBuilding(id, &error);
        if (building != nullptr && building->owner == *player && building->buildup != 0 && building->art == *type) {
            const std::uint32_t number = id;
            count += *area == kAreaZero || *area == BuildingArea(&number);
        }
    } while (error == 0);
    return (error & 0xffff0000u) | (count & 0xffff);
}

std::uint32_t __stdcall WrongFirstMilitary(std::uint32_t player, std::uint32_t area) {
    // Wrong: the castle doesn't count.
    const std::uint32_t id = AI_FirstMilitaryBuildingInArea(player, area);
    if (id != kNotFound && g_aiMapWorld.get()->buildings[id].art == Value(BuildingType::Castle)) {
        return kNotFound;
    }
    return id;
}

std::uint32_t __fastcall WrongMineSuccess(const std::uint16_t* id) {
    // Wrong: only the newer half of the dig history.
    const Building& mine = g_aiWorld.get()->buildings[*id];
    if (mine.worker == 0 || mine.production.digs == 0) {
        return kNotFound;
    }
    return CountBits(mine.work_x) * kPercent / mine.production.digs;
}

std::uint32_t __stdcall WrongFirstBuilding(std::uint32_t player, std::uint32_t area) {
    // Wrong: construction sites count too.
    const CGameData* world = g_aiMapWorld;
    for (std::uint32_t id = 1; id < world->building_high_water + 1; ++id) {
        const Building& building = world->buildings[id];
        if (building.owner == player && DoorArea(world, building) == area) {
            return id;
        }
    }
    return kNotFound;
}

}  // namespace

S3RE_TEST(AiBuildingQueries_MatchOriginal) {
    CompareBuildingQueries(ctx, OurQueries(), 8000);
}

S3RE_TEST(AiBuildingQueries_ComparisonDetectsWrongImplementation) {
    const auto probe = [&](auto change) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        BuildingQueries wrong = OurQueries();
        change(wrong);
        CompareBuildingQueries(quiet, wrong, 4000);
        S3RE_EXPECT(quiet.failures() > 0);
    };
    probe([](BuildingQueries& f) { f.countSites = &WrongCountSites; });
    probe([](BuildingQueries& f) { f.firstMilitary = &WrongFirstMilitary; });
    probe([](BuildingQueries& f) { f.first = &WrongFirstBuilding; });
    probe([](BuildingQueries& f) { f.mineSuccess = &WrongMineSuccess; });
}

}  // namespace s3re::game
