// The T_SAFETY_AGENT (kind 9; vtable 0x65a5f8, constructor 0x586830): it
// walks the land evaluation grid cell by cell and asks for a large tower
// or a castle where the computer player has valuable buildings without
// military protection (re/computer_players.md "Agents").
//
//   0x00586850 T_SAFETY_AGENT::Run() thiscall (agent slot 8): the run
//              counter (word +8) goes up; once it reaches
//              AiPlayerSetup::safety_interval (unsigned) it becomes 0 and
//              CheckNextGridCell runs safety_cells_per_check times (both
//              words of g_aiPlayerSetup, re-read per step). EAX is not
//              used (CComputerPlayer ignores slot 8's result).
//   0x005868a0 T_SAFETY_AGENT::CheckNextGridCell() thiscall: moves the
//              cell cursor (words +0xa x, +0xc y) one cell on, row by row
//              over the grid of g_aiLandEvaluation (width +0x140, height
//              +0x144, compared as zero-extended words), wrapping at the
//              end. If the cell's centre tile belongs to g_aiPlayerNumber
//              (AI::CellCentreOwner 0x554850): over the 5 x 5 cells around
//              it (cell coordinates as signed 16-bit values, kept inside
//              the grid) the player's building-type bitset of each cell
//              (AiGridCell +0x24) adds a 16-bit danger sum: -250 for a
//              large tower, -350 for a castle, the setup's weights for
//              storehouses, mines, smelters, smiths and harbours and, by
//              the player's race (Player::race, word, 0..3), for its war
//              workshop, temples and (Asians) rice farms. A sum of at
//              least 100 (signed 16 bits) draws CGameData::Random: when
//              its low word % 100 is below safety_skip_percent nothing
//              happens; otherwise a sum above 200 draws again and asks for
//              a castle (distance 64) when that low word % 100 is below
//              castle_percent, else a large tower (distance 48). If fewer
//              than 3 buildings of that type are within the distance of
//              the cell centre (AI::TileCentre_554880; CountBuildingsNear
//              0x571e30) and no site note of the type is there
//              (HasSiteNoteNear 0x571c40), RecordBestCandidate 0x58ec50
//              records it with priority 30000. The centre words are passed
//              as dwords whose upper halves are uninitialized stack in the
//              original (ours 0); the callees use only the low words.

#include "game/ai/ai_layout.h"
#include "game/enums/buildings.h"
#include "game/enums/races.h"
#include "game/globals.h"
#include "game/layout.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using namespace ai;
using layout::kPlayers;

namespace {

constexpr int kAround = 2;                      // the 5 x 5 cells around the cell
constexpr std::int16_t kLargeTowerProtection = -250;
constexpr std::int16_t kCastleProtection = -350;
constexpr std::int16_t kDangerous = 100;        // a sum of at least this asks for protection
constexpr std::int16_t kVeryDangerous = 200;    // above this a castle may be asked for
constexpr std::uint32_t kPercent = 100;
constexpr std::uint16_t kLargeTowerDistance = 48;
constexpr std::uint16_t kCastleDistance = 64;
constexpr std::uint16_t kEnoughProtection = 3;  // this many towers / castles near: nothing asked
constexpr std::uint32_t kSafetyPriority = 30000;

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kRandom = 0x004feb70;             // CGameData::Random (thiscall)
constexpr std::uintptr_t kGridCell = 0x00554700;           // AI::CEvaLand::GridCell (thiscall)
constexpr std::uintptr_t kCellCentreOwner = 0x00554850;    // AI::CellCentreOwner (thiscall)
constexpr std::uintptr_t kTileCentre = 0x00554880;         // AI::TileCentre_554880 (thiscall)
constexpr std::uintptr_t kCountBuildingsNear = 0x00571e30;  // AI::CountBuildingsNear (fastcall)
constexpr std::uintptr_t kHasSiteNoteNear = 0x00571c40;    // AI::HasSiteNoteNear (fastcall)
constexpr std::uintptr_t kRecordBestCandidate = 0x0058ec50;  // AI::RecordBestCandidate (fastcall)
constexpr std::uintptr_t kCheckNextGridCell = 0x005868a0;  // T_SAFETY_AGENT::CheckNextGridCell

std::uint32_t Random() {
    using Fn = std::uint32_t(__fastcall*)(CGameData* world, void* edx);
    return reinterpret_cast<Fn>(kRandom)(g_aiWorld, nullptr);
}

void* LandEvaluation() { return reinterpret_cast<void*>(g_aiLandEvaluation.get()); }

bool Has(std::uint32_t bits, int bit) { return ((bits >> bit) & 1) != 0; }

// 0x5869d4..0x586bc3: the danger of one cell's buildings of the player
// (low / high dword of its building-type bitset), added to `sum`.
void AddCellDanger(std::uint32_t low, std::uint32_t high, std::uint16_t race, std::int16_t& sum) {
    const AiPlayerSetup& setup = *g_aiPlayerSetup.get();
    const auto add = [&](std::uint16_t weight) { sum = static_cast<std::int16_t>(sum + weight); };
    const auto type = [&](BuildingType t) {
        const int bit = Value(t);
        return bit < 32 ? Has(low, bit) : Has(high, bit - 32);
    };
    if (type(BuildingType::LargeTower)) {
        sum = static_cast<std::int16_t>(sum + kLargeTowerProtection);
    }
    if (type(BuildingType::Castle)) {
        sum = static_cast<std::int16_t>(sum + kCastleProtection);
    }
    if (type(BuildingType::CoalMine)) add(setup.coal_mine_weight);
    if (type(BuildingType::IronMine)) add(setup.iron_mine_weight);
    if (type(BuildingType::GoldMine)) add(setup.gold_mine_weight);
    if (type(BuildingType::IronSmelter)) add(setup.iron_smelter_weight);
    if (type(BuildingType::GoldSmelter)) add(setup.gold_smelter_weight);
    if (type(BuildingType::Toolsmith)) add(setup.toolsmith_weight);
    if (type(BuildingType::Weaponsmith)) add(setup.weaponsmith_weight);
    if (type(BuildingType::Storehouse)) add(setup.storehouse_weight);
    if (type(BuildingType::Harbour)) add(setup.harbour_weight);
    // jump table 0x586cc0: the race's own buildings
    switch (race) {
    case Value(Race::Roman):
        if (type(BuildingType::CatapultWorkshop)) add(setup.war_workshop_weight);
        if (type(BuildingType::SmallTemple)) add(setup.small_temple_weight);
        if (type(BuildingType::LargeTemple)) add(setup.large_temple_weight);
        break;
    case Value(Race::Egyptian):
        if (type(BuildingType::BallistaWorkshop)) add(setup.war_workshop_weight);
        if (type(BuildingType::Sphinx)) add(setup.small_temple_weight);
        if (type(BuildingType::Pyramid)) add(setup.large_temple_weight);
        break;
    case Value(Race::Asian):
        if (type(BuildingType::CannonWorkshop)) add(setup.war_workshop_weight);
        if (type(BuildingType::SmallPagoda)) add(setup.small_temple_weight);
        if (type(BuildingType::LargePagoda)) add(setup.large_temple_weight);
        if (type(BuildingType::RiceFarm)) add(setup.rice_farm_weight);
        break;
    case Value(Race::Amazon):
        if (type(BuildingType::LargeGong)) add(setup.war_workshop_weight);
        if (type(BuildingType::AmazonSmallTemple)) add(setup.small_temple_weight);
        if (type(BuildingType::AmazonLargeTemple)) add(setup.large_temple_weight);
        break;
    default:
        break;
    }
}

}  // namespace

using SafetyFn = std::uint32_t(__fastcall*)(SafetyAgent* self, void* edx);

std::uint32_t __fastcall T_SAFETY_AGENT_CheckNextGridCell(SafetyAgent* self, void* edx);
std::uint32_t __fastcall T_SAFETY_AGENT_Run(SafetyAgent* self, void* edx);

S3RE_HOOK(0x005868a0, T_SAFETY_AGENT_CheckNextGridCell, SafetyFn, Original_T_SAFETY_AGENT_CheckNextGridCell,
          T_SAFETY_AGENT_CheckNextGridCell);
S3RE_HOOK(0x00586850, T_SAFETY_AGENT_Run, SafetyFn, Original_T_SAFETY_AGENT_Run, T_SAFETY_AGENT_Run);

// Purpose: Checks the next cell of the land grid for valuable buildings of
// the computer player that lack military protection, and asks for a large
// tower or castle there.
std::uint32_t __fastcall T_SAFETY_AGENT_CheckNextGridCell(SafetyAgent* self, void* /*edx*/) {
    const AiLandEvaluation& land = *reinterpret_cast<const AiLandEvaluation*>(g_aiLandEvaluation.get());
    const std::uint32_t width = land.width;
    const std::uint32_t height = land.height;
    if (++self->cell_x == width) {
        ++self->cell_y;
        self->cell_x = 0;
        if (self->cell_y == height) {
            self->cell_y = 0;
        }
    }
    using OwnerFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t x, std::uint32_t y);
    const auto owner = static_cast<std::uint16_t>(
        reinterpret_cast<OwnerFn>(kCellCentreOwner)(LandEvaluation(), nullptr, self->cell_x, self->cell_y));
    const std::uint8_t player = g_aiPlayerNumber;
    if (owner != player) {
        return 0;
    }
    std::uint32_t centreX = 0;  // (the original's dwords get only their low words written)
    std::uint32_t centreY = 0;
    using CentreFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t x, std::uint32_t y,
                                                std::uint32_t* outX, std::uint32_t* outY);
    reinterpret_cast<CentreFn>(kTileCentre)(LandEvaluation(), nullptr, self->cell_x, self->cell_y, &centreX, &centreY);
    const std::uint16_t race = static_cast<std::uint16_t>(g_aiWorld.get()->players[g_aiPlayerNumber].race);
    const auto left = static_cast<std::int16_t>(self->cell_x - kAround);
    const auto top = static_cast<std::int16_t>(self->cell_y - kAround);
    std::int16_t sum = 0;
    for (int i = 0; i <= 2 * kAround; ++i) {
        const auto x = static_cast<std::int16_t>(left + i);
        for (int j = 0; j <= 2 * kAround; ++j) {
            const auto y = static_cast<std::int16_t>(top + j);
            if (x < 0 || x >= static_cast<std::int32_t>(width) || y < 0 || y >= static_cast<std::int32_t>(height)) {
                continue;
            }
            using CellFn = const AiGridCell*(__fastcall*)(void* self, void* edx, std::int32_t x, std::int32_t y);
            const AiGridCell* cell = reinterpret_cast<CellFn>(kGridCell)(LandEvaluation(), nullptr, x, y);
            const std::uint8_t p = g_aiPlayerNumber;
            AddCellDanger(cell->building_types[p][0], cell->building_types[p][1], race, sum);
        }
    }
    if (sum < kDangerous) {
        return 0;
    }
    if ((Random() & 0xffff) % kPercent < g_aiPlayerSetup.get()->safety_skip_percent) {
        return 0;
    }
    bool castle = false;
    if (sum > kVeryDangerous) {
        castle = (Random() & 0xffff) % kPercent < g_aiPlayerSetup.get()->castle_percent;
    }
    const std::uint32_t type = Value(castle ? BuildingType::Castle : BuildingType::LargeTower);
    const std::uint32_t distance = castle ? kCastleDistance : kLargeTowerDistance;
    using CountFn = std::uint32_t(__fastcall*)(std::uint32_t type, std::uint32_t x, std::uint32_t y,
                                               std::uint32_t distance, std::uint32_t player);
    const auto nearby = static_cast<std::uint16_t>(
        reinterpret_cast<CountFn>(kCountBuildingsNear)(type, centreX, centreY, distance, g_aiPlayerNumber));
    if (nearby >= kEnoughProtection) {
        return 0;
    }
    using NoteFn = std::uint32_t(__fastcall*)(std::uint32_t type, std::uint32_t x, std::uint32_t y,
                                              std::uint32_t distance);
    if (reinterpret_cast<NoteFn>(kHasSiteNoteNear)(type, centreX, centreY, distance) != 0) {
        return 0;
    }
    using RecordFn = std::uint32_t(__fastcall*)(std::uint32_t index, std::uint32_t score, std::uint32_t a,
                                                std::uint32_t b);
    return reinterpret_cast<RecordFn>(kRecordBestCandidate)(type, kSafetyPriority, centreX, centreY);
}

// Purpose: The safety agent's turn: every few turns it checks a number of
// land grid cells for unprotected valuable buildings.
std::uint32_t __fastcall T_SAFETY_AGENT_Run(SafetyAgent* self, void* /*edx*/) {
    if (++self->run_count < g_aiPlayerSetup.get()->safety_interval) {
        return 0;
    }
    self->run_count = 0;
    for (std::uint16_t k = 0; k < g_aiPlayerSetup.get()->safety_cells_per_check; ++k) {
        reinterpret_cast<SafetyFn>(kCheckNextGridCell)(self, nullptr);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// A test world (g_aiWorld = g_aiMapWorld), a small land grid (g_aiLandEvaluation),
// a player setup, an empty computer player (no site notes) and an
// AiPlayerData node; the callees run (verified). Each case runs the
// original, keeps the agent, the node and the RNG, restores, runs ours.

namespace {

constexpr std::uint32_t kTestGrid = 12;
constexpr std::uint32_t kTestBuildings = 30;

struct SafetyBuffers {
    AiLandEvaluation land{};
    std::vector<AiGridCell> cells = std::vector<AiGridCell>(kTestGrid * kTestGrid);
    AiPlayerSetup setup{};
    std::vector<std::uint8_t> player = std::vector<std::uint8_t>(sizeof(ComputerPlayer));
    std::vector<std::uint8_t> data = std::vector<std::uint8_t>(0x800);
};

void SetUpSafety(test::Rng& rng, CGameData* world, SafetyBuffers& b) {
    b.land.width = 1 + rng.Below(kTestGrid);
    b.land.height = 1 + rng.Below(kTestGrid);
    b.land.cells = b.cells.data();
    for (AiGridCell& cell : b.cells) {
        for (auto& bits : cell.building_types) {
            bits[0] = rng.Chance(1, 3) ? 0 : rng.Next32() & rng.Next32();
            bits[1] = rng.Chance(1, 3) ? 0 : rng.Next32() & rng.Next32();
        }
    }
    rng.Fill(&b.setup, sizeof(b.setup));
    const auto weight = [&]() { return static_cast<std::uint16_t>(rng.Chance(1, 8) ? rng.Next32() : rng.Below(120)); };
    for (std::uint16_t* w : {&b.setup.gold_smelter_weight, &b.setup.gold_mine_weight, &b.setup.storehouse_weight,
                             &b.setup.weaponsmith_weight, &b.setup.toolsmith_weight, &b.setup.coal_mine_weight,
                             &b.setup.iron_mine_weight, &b.setup.iron_smelter_weight, &b.setup.harbour_weight,
                             &b.setup.war_workshop_weight, &b.setup.small_temple_weight,
                             &b.setup.large_temple_weight, &b.setup.rice_farm_weight}) {
        *w = weight();
    }
    b.setup.safety_skip_percent = static_cast<std::uint16_t>(rng.Below(120));
    b.setup.castle_percent = static_cast<std::uint16_t>(rng.Below(120));
    b.setup.safety_interval = static_cast<std::uint16_t>(rng.Below(5));
    b.setup.safety_cells_per_check = static_cast<std::uint16_t>(rng.Below(6));
    std::fill(b.player.begin(), b.player.end(), 0);  // no notes
    rng.Fill(b.data.data(), b.data.size());
    g_aiMapSizeX = static_cast<std::uint16_t>(kTestGrid * 8);
    g_aiMapSizeY = static_cast<std::uint16_t>(kTestGrid * 8);
    world->map_size = kTestGrid * 8;
    for (std::uint32_t x = 0; x < kTestGrid * 8; ++x) {
        for (std::uint32_t y = 0; y < kTestGrid * 8; ++y) {
            world->tiles[x][y].owner = static_cast<std::uint8_t>(rng.Chance(2, 3) ? 1 : rng.Below(4));
        }
    }
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        world->players[p].race = static_cast<std::int32_t>(rng.Chance(1, 10) ? rng.Below(8) : rng.Below(4));
    }
    world->building_high_water = rng.Below(kTestBuildings);
    for (std::uint32_t id = 1; id < kTestBuildings; ++id) {
        layout::Building& building = world->buildings[id];
        building.owner = static_cast<std::uint8_t>(rng.Below(3));
        building.art = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 16 + 2 * rng.Below(2) : rng.Below(30));
        building.x = static_cast<std::uint16_t>(rng.Below(kTestGrid * 8));
        building.y = static_cast<std::uint16_t>(rng.Below(kTestGrid * 8));
    }
}

struct SafetyOutcome {
    std::uint32_t result;
    SafetyAgent agent;
    std::vector<std::uint8_t> data;
    std::uint16_t random[3];
};

void CompareSafety(test::Context& ctx, SafetyFn run, SafetyFn check, int cases) {
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    SafetyBuffers b;
    CGameData* savedWorld = g_aiWorld;
    CGameData* savedMapWorld = g_aiMapWorld;
    const std::uint32_t savedLand = g_aiLandEvaluation;
    AiPlayerSetup* savedSetup = g_aiPlayerSetup;
    ComputerPlayer* savedPlayer = g_aiPlayer;
    AiPlayerData* savedData = g_aiPlayerData;
    const std::uint8_t savedNumber = g_aiPlayerNumber;
    const std::uint16_t savedSizeX = g_aiMapSizeX;
    const std::uint16_t savedSizeY = g_aiMapSizeY;
    g_aiWorld = world;
    g_aiMapWorld = world;
    g_aiLandEvaluation = reinterpret_cast<std::uint32_t>(&b.land);
    g_aiPlayerSetup = &b.setup;
    g_aiPlayer = reinterpret_cast<ComputerPlayer*>(b.player.data());
    g_aiPlayerData = reinterpret_cast<AiPlayerData*>(b.data.data());

    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        if (i % 4 == 0) {
            SetUpSafety(rng, world, b);
        }
        g_aiPlayerNumber = static_cast<std::uint8_t>(1 + (rng.Chance(1, 5) ? rng.Below(3) : 0));
        SafetyAgent start{};
        rng.Fill(&start, sizeof(start));
        start.cell_x = static_cast<std::uint16_t>(rng.Chance(1, 4) ? b.land.width - 1 : rng.Below(b.land.width));
        start.cell_y = static_cast<std::uint16_t>(rng.Chance(1, 4) ? b.land.height - 1 : rng.Below(b.land.height));
        start.run_count = static_cast<std::uint16_t>(rng.Below(6));
        rng.Fill(world->random_state, sizeof(world->random_state));
        const bool useRun = rng.Chance(1, 3);
        const std::vector<std::uint8_t> data = b.data;
        std::uint16_t random[3];
        std::memcpy(random, world->random_state, sizeof(random));
        auto go = [&](SafetyFn f) {
            b.data = data;
            std::memcpy(world->random_state, random, sizeof(random));
            SafetyOutcome out{};
            out.agent = start;
            out.result = f(&out.agent, nullptr);
            out.data = b.data;
            std::memcpy(out.random, world->random_state, sizeof(out.random));
            return out;
        };
        const SafetyOutcome a = go(useRun ? Original_T_SAFETY_AGENT_Run : Original_T_SAFETY_AGENT_CheckNextGridCell);
        const SafetyOutcome c = go(useRun ? run : check);
        bool same = S3RE_EXPECT_MEM_EQ(&a.agent, &c.agent, sizeof(SafetyAgent));
        same &= S3RE_EXPECT(a.data == c.data);
        same &= S3RE_EXPECT_MEM_EQ(a.random, c.random, sizeof(a.random));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, run %d", i, useRun ? 1 : 0);
        }
    }
    g_aiWorld = savedWorld;
    g_aiMapWorld = savedMapWorld;
    g_aiLandEvaluation = savedLand;
    g_aiPlayerSetup = savedSetup;
    g_aiPlayer = savedPlayer;
    g_aiPlayerData = savedData;
    g_aiPlayerNumber = savedNumber;
    g_aiMapSizeX = savedSizeX;
    g_aiMapSizeY = savedSizeY;
}

std::uint32_t __fastcall WrongCheck(SafetyAgent* self, void* edx) {
    // Wrong: castles don't protect.
    AiLandEvaluation& land = *reinterpret_cast<AiLandEvaluation*>(g_aiLandEvaluation.get());
    std::vector<std::uint32_t> saved;
    for (std::uint32_t k = 0; k < land.width * land.height; ++k) {
        for (auto& bits : land.cells[k].building_types) {
            saved.push_back(bits[0]);
            bits[0] &= ~(1u << Value(BuildingType::Castle));
        }
    }
    const std::uint32_t result = T_SAFETY_AGENT_CheckNextGridCell(self, edx);
    std::size_t n = 0;
    for (std::uint32_t k = 0; k < land.width * land.height; ++k) {
        for (auto& bits : land.cells[k].building_types) {
            bits[0] = saved[n++];
        }
    }
    return result;
}

std::uint32_t __fastcall WrongRun(SafetyAgent* self, void* edx) {
    // Wrong: one cell fewer per check.
    if (++self->run_count < g_aiPlayerSetup.get()->safety_interval) {
        return 0;
    }
    self->run_count = 0;
    for (std::uint16_t k = 1; k < g_aiPlayerSetup.get()->safety_cells_per_check; ++k) {
        T_SAFETY_AGENT_CheckNextGridCell(self, edx);
    }
    return 0;
}

}  // namespace

S3RE_TEST(AiSafetyAgent_MatchOriginal) {
    CompareSafety(ctx, &T_SAFETY_AGENT_Run, &T_SAFETY_AGENT_CheckNextGridCell, 4000);
}

S3RE_TEST(AiSafetyAgent_ComparisonDetectsWrongImplementation) {
    {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareSafety(quiet, &T_SAFETY_AGENT_Run, &WrongCheck, 4000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
    {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareSafety(quiet, &WrongRun, &T_SAFETY_AGENT_CheckNextGridCell, 4000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

}  // namespace s3re::game
