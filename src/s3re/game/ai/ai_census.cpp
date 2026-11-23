// The computer players' census of the world and small helpers of
// Rainer\Cevaland.cpp (computer players, batch 1; re/computer_players.md).
// The census object (AiAgentData in ai_layout.h, 0x1ad8 bytes) is filled
// by 0x58f990 through the two counting functions below. They read the
// world through g_aiMapWorld (0x7e7b60).
//
// AI_Census_Construct (0x0055b6b0): () the constructor: dwords18e4 = 0,
//   then AI_ResetAgentData (0x55b770). EAX = this.
// AI_Census_CountBuildings (0x0055be00): () buildings[][] = 0,
//   buildings_counted = 0; then for each building id 1..building_high_water
//   (unsigned, the count re-read every time): owner < 20 and buildup == 0
//   (finished) -> ++buildings[owner][type], flat index owner * 57 + type
//   (types past 56 run into the next row, like the original). Then unk002 =
//   1, buildings_counted = 1. EAX = building_high_water + 1.
// AI_Census_ComputeTeamStrength (0x0055bea0): () for each player i:
//   team_strength[i] = 0; if player i takes part (Player::active != 0):
//   for each player j that is i or in i's team (Player::team equal):
//   += 8 * (soldiers[j][0..8] with [2], [5], [8] counted twice) and
//   += 16 * (small towers + large towers + 2 * castles of j). Then
//   team_strength_valid = 1. EAX = this.
// AI_StartPositionWalkRegion (0x0055b6e0): (player) stdcall, ret 4: the
//   player's start position (Player::start_x/start_y); 0 if either is off
//   the map (AI_IsOnMap); else from it to the right while x < map size - 1
//   (signed) and the tile is blocked (layout::kTileBlocked), and EAX =
//   Tile::walk_region there.
// AI_PtrVector_Size (0x0055bfc0): () thiscall on an MSVC 6 std::vector of
//   4-byte elements (+4 first, +8 last): 0 if first is null, else
//   (last - first) >> 2 (arithmetic shift).
// AI_UninitializedCopyPtrs (0x0055bff0): (first, last, dest) stdcall,
//   ret 0xc: copies dwords from [first, last) to dest, skipping the store
//   where dest is null (std::uninitialized_copy with construct); EAX = the
//   dest end.
// AI_EvaLand_SumAllCells (0x005546c0): () AI_GridSumAround(x, y) for every
//   cell of the land evaluation grid, rows (y) outside, the sizes re-read
//   after each call. EAX = the grid height.

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
#include <vector>

namespace s3re::game {

// ai_helpers.cpp
bool __fastcall AI_IsOnMap(std::int32_t v);
std::uint32_t __fastcall AI_GridSumAround(void* self, void* edx, std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall AI_ResetAgentData(void* self, void* edx);

using namespace ai;
using layout::kPlayers;

namespace {

// Soldier census weights (0x55bea0): every third kind (probably the
// highest level of each weapon) counts twice; soldiers weigh 8, towers 16.
constexpr std::uint32_t kSoldierWeight = 8;
constexpr std::uint32_t kTowerWeight = 16;

// An MSVC 6 std::vector (allocator byte at +0).
struct PtrVector {
    std::uint32_t allocator;
    std::int32_t first;  // +4
    std::int32_t last;   // +8
    std::int32_t end;    // +0xc
};
static_assert(offsetof(PtrVector, first) == 4);

AiAgentData* AsCensus(void* self) { return static_cast<AiAgentData*>(self); }

}  // namespace

using ThisFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using PlayerFn = std::uint32_t(__stdcall*)(std::uint32_t player);
using CopyFn = std::uint32_t(__stdcall*)(const std::uint32_t* first, const std::uint32_t* last, std::uint32_t* dest);

std::uint32_t __fastcall AI_Census_Construct(void* self, void* edx);
std::uint32_t __fastcall AI_Census_CountBuildings(void* self, void* edx);
std::uint32_t __fastcall AI_Census_ComputeTeamStrength(void* self, void* edx);
std::uint32_t __stdcall AI_StartPositionWalkRegion(std::uint32_t player);
std::uint32_t __fastcall AI_PtrVector_Size(void* self, void* edx);
std::uint32_t __stdcall AI_UninitializedCopyPtrs(const std::uint32_t* first, const std::uint32_t* last,
                                                 std::uint32_t* dest);
std::uint32_t __fastcall AI_EvaLand_SumAllCells(void* self, void* edx);

S3RE_HOOK(0x0055b6b0, AI_Census_Construct, ThisFn, Original_AI_Census_Construct, AI_Census_Construct);

// Purpose: Constructor of the computer players' census of the world.
std::uint32_t __fastcall AI_Census_Construct(void* self, void* /*edx*/) {
    for (std::uint32_t& value : AsCensus(self)->dwords18e4) {
        value = 0;
    }
    AI_ResetAgentData(self, nullptr);
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x0055be00, AI_Census_CountBuildings, ThisFn, Original_AI_Census_CountBuildings, AI_Census_CountBuildings);

// Purpose: Counts every player's finished buildings per type, for the
// computer players' planning.
std::uint32_t __fastcall AI_Census_CountBuildings(void* self, void* /*edx*/) {
    AiAgentData* census = AsCensus(self);
    std::memset(census->buildings, 0, sizeof(census->buildings));
    census->buildings_counted = 0;
    // Flat like the original (owner * 57 + type; types are not range-checked).
    std::uint32_t* counts = &census->buildings[0][0];
    const CGameData* world = g_aiMapWorld;
    for (std::uint32_t id = 1; id < world->building_high_water + 1; ++id) {
        const layout::Building& building = world->buildings[id];
        if (building.owner < kPlayers && building.buildup == 0) {
            ++counts[building.owner * kBuildingTypeCount + building.art];
            world = g_aiMapWorld;
        }
    }
    census->unk002 = 1;
    census->buildings_counted = 1;
    return world->building_high_water + 1;
}

S3RE_HOOK(0x0055bea0, AI_Census_ComputeTeamStrength, ThisFn, Original_AI_Census_ComputeTeamStrength,
          AI_Census_ComputeTeamStrength);

// Purpose: Rates the military strength of each player's team (soldiers and
// towers of all players in it), probably for the computer players' attack
// and defence decisions.
std::uint32_t __fastcall AI_Census_ComputeTeamStrength(void* self, void* /*edx*/) {
    AiAgentData* census = AsCensus(self);
    for (std::size_t i = 0; i < kPlayers; ++i) {
        census->team_strength[i] = 0;
        const CGameData* world = g_aiMapWorld;
        if (world->players[i].active == 0) {
            continue;
        }
        for (std::size_t j = 0; j < kPlayers; ++j) {
            if (world->players[i].active == 0) {
                continue;
            }
            if (i != j && world->players[j].team != world->players[i].team) {
                continue;
            }
            const std::uint32_t* s = census->soldiers[j];
            const std::uint32_t soldiers = s[0] + 2 * (s[2] + s[8] + s[5]) + s[1] + s[3] + s[7] + s[4] + s[6];
            census->team_strength[i] += soldiers * kSoldierWeight;
            const std::uint32_t* b = census->buildings[j];
            const auto type = [&b](BuildingType t) { return b[static_cast<std::size_t>(t)]; };
            const std::uint32_t towers =
                type(BuildingType::SmallTower) + 2 * type(BuildingType::Castle) + type(BuildingType::LargeTower);
            census->team_strength[i] += towers * kTowerWeight;
            world = g_aiMapWorld;
        }
    }
    census->team_strength_valid = 1;
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x0055b6e0, AI_StartPositionWalkRegion, PlayerFn, Original_AI_StartPositionWalkRegion,
          AI_StartPositionWalkRegion);

// Purpose: The walking region (connected land) of a player's start
// position, taking the first free tile to its right if it is blocked;
// probably to tell which players can reach each other on foot.
std::uint32_t __stdcall AI_StartPositionWalkRegion(std::uint32_t player) {
    const layout::Player& record = g_aiMapWorld.get()->players[player];
    std::int32_t x = record.start_x;
    const std::int32_t y = record.start_y;
    if (!AI_IsOnMap(x) || !AI_IsOnMap(y)) {
        return 0;
    }
    const CGameData* world = g_aiMapWorld;
    const std::int32_t last = static_cast<std::int32_t>(world->map_size) - 1;
    while (x < last && (world->tiles[x][y].flags & layout::kTileBlocked) != 0) {
        ++x;
    }
    return world->tiles[x][y].walk_region;
}

S3RE_HOOK(0x0055bfc0, AI_PtrVector_Size, ThisFn, Original_AI_PtrVector_Size, AI_PtrVector_Size);

// Purpose: std::vector::size for a vector of pointers (inlined library code).
std::uint32_t __fastcall AI_PtrVector_Size(void* self, void* /*edx*/) {
    const auto* vector = static_cast<const PtrVector*>(self);
    if (vector->first == 0) {
        return 0;
    }
    return static_cast<std::uint32_t>((vector->last - vector->first) >> 2);
}

S3RE_HOOK(0x0055bff0, AI_UninitializedCopyPtrs, CopyFn, Original_AI_UninitializedCopyPtrs, AI_UninitializedCopyPtrs);

// Purpose: std::uninitialized_copy for pointers (library code used when a
// vector of pointers grows).
std::uint32_t __stdcall AI_UninitializedCopyPtrs(const std::uint32_t* first, const std::uint32_t* last,
                                                 std::uint32_t* dest) {
    for (; first != last; ++first, ++dest) {
        if (dest != nullptr) {
            *dest = *first;
        }
    }
    return reinterpret_cast<std::uint32_t>(dest);
}

S3RE_HOOK(0x005546c0, AI_EvaLand_SumAllCells, ThisFn, Original_AI_EvaLand_SumAllCells, AI_EvaLand_SumAllCells);

// Purpose: Fills in the neighbourhood sums of every cell of the land
// evaluation grid.
std::uint32_t __fastcall AI_EvaLand_SumAllCells(void* self, void* /*edx*/) {
    const auto* evaluation = static_cast<const AiLandEvaluation*>(self);
    for (std::uint32_t y = 0; y < evaluation->height; ++y) {
        for (std::uint32_t x = 0; x < evaluation->width; ++x) {
            AI_GridSumAround(self, nullptr, x, y);
        }
    }
    return evaluation->height;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// The census functions run on census buffers (with slack past the object
// for out-of-row type indexes) and a world pair, with g_aiMapWorld and
// g_aiWorld pointed at each side's world. The constructor also writes the
// 20 shared vectors (AI_ResetAgentData); those S3.EXE bytes are captured
// per side and restored.

namespace {

struct CensusFunctions {
    ThisFn construct;
    ThisFn countBuildings;
    ThisFn teamStrength;
    PlayerFn walkRegion;
    ThisFn vectorSize;
    CopyFn copy;
    ThisFn sumAllCells;
};

CensusFunctions OriginalCensus() {
    return {Original_AI_Census_Construct, Original_AI_Census_CountBuildings,
            Original_AI_Census_ComputeTeamStrength, Original_AI_StartPositionWalkRegion,
            Original_AI_PtrVector_Size, Original_AI_UninitializedCopyPtrs,
            Original_AI_EvaLand_SumAllCells};
}

CensusFunctions OurCensus() {
    return {&AI_Census_Construct, &AI_Census_CountBuildings, &AI_Census_ComputeTeamStrength,
            &AI_StartPositionWalkRegion, &AI_PtrVector_Size, &AI_UninitializedCopyPtrs,
            &AI_EvaLand_SumAllCells};
}

constexpr std::size_t kCensusBytes = 0x2000;  // 0x1ad8 + slack (index up to 19 * 57 + 255)
static_assert(offsetof(AiAgentData, buildings) + (19 * kBuildingTypeCount + 256) * 4 <= kCensusBytes);
constexpr std::size_t kMaxBuildings = 600;
constexpr std::size_t kWorldBytes = offsetof(CGameData, buildings) + (kMaxBuildings + 1) * sizeof(layout::Building);
// The 20 vectors up to g_aiMapWorld (set per side), which follows them.
constexpr std::size_t kVectorsBytes = g_aiMapWorld.address() - g_aiAgentVectors.address();
static_assert(kVectorsBytes == kAgentVectorCount * sizeof(AiVector) - 4);
void* VectorsLow() { return &g_aiAgentVectors; }

void FillWorld(test::Rng& rng, test::WorldPair& worlds, std::uint32_t mapSize) {
    const std::uint32_t count = rng.Chance(1, 20) ? rng.Below(2) : rng.Below(kMaxBuildings);
    worlds.Both([&](CGameData* w) {
        w->map_size = mapSize;
        w->building_high_water = count;
    });
    for (std::uint32_t id = 0; id <= kMaxBuildings; ++id) {
        const auto owner = static_cast<std::uint8_t>(rng.Chance(1, 8) ? rng.Next32() : rng.Below(kPlayers));
        const auto art = static_cast<std::uint8_t>(rng.Chance(1, 16) ? rng.Next32() : rng.Below(kBuildingTypeCount));
        const auto buildup = static_cast<std::uint8_t>(rng.Chance(1, 4) ? rng.Below(256) : 0);
        worlds.Both([&](CGameData* w) {
            w->buildings[id].owner = owner;
            w->buildings[id].art = art;
            w->buildings[id].buildup = buildup;
        });
    }
    for (std::size_t p = 0; p < kPlayers; ++p) {
        const std::int32_t active = rng.Chance(1, 3) ? 0 : static_cast<std::int32_t>(rng.Below(3));
        const std::int32_t team = static_cast<std::int32_t>(rng.Below(4));
        const auto coordinate = [&]() {
            return rng.Chance(1, 10) ? static_cast<std::int32_t>(rng.Next32() % 200) - 100
                                     : static_cast<std::int32_t>(rng.Below(mapSize + 1));
        };
        const std::int32_t x = coordinate(), y = coordinate();
        worlds.Both([&](CGameData* w) {
            w->players[p].active = active;
            w->players[p].team = team;
            w->players[p].start_x = x;
            w->players[p].start_y = y;
        });
        // Runs of blocked tiles to the right of the start (often to the edge).
        if (x >= 0 && y >= 0 && static_cast<std::uint32_t>(x) < mapSize && static_cast<std::uint32_t>(y) < mapSize) {
            const std::uint32_t run = rng.Below(mapSize - x + 2);
            for (std::uint32_t k = 0; k <= run && x + k < mapSize; ++k) {
                const auto flags = static_cast<std::uint8_t>(rng.Next32());
                const auto blocked = static_cast<std::uint8_t>(k < run ? flags | layout::kTileBlocked : flags & ~layout::kTileBlocked);
                const auto region = static_cast<std::uint8_t>(rng.Next32());
                worlds.Both([&](CGameData* w) {
                    w->tiles[x + k][y].flags = blocked;
                    w->tiles[x + k][y].walk_region = region;
                });
            }
        }
    }
}

void CompareCensus(test::Context& ctx, const CensusFunctions& candidate, int cases) {
    const CensusFunctions original = OriginalCensus();
    test::WorldPair worlds(ctx, kWorldBytes);
    CGameData* const savedWorld = g_aiWorld;
    CGameData* const savedMapWorld = g_aiMapWorld;
    std::vector<std::uint8_t> savedVectors(kVectorsBytes), vectorsA(kVectorsBytes), vectorsB(kVectorsBytes);
    std::memcpy(savedVectors.data(), VectorsLow(), kVectorsBytes);
    std::vector<std::uint8_t> a(kCensusBytes), b(kCensusBytes);
    constexpr int kOps = 7;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(kOps));
        if (i % 200 == 0) {
            FillWorld(ctx.rng(), worlds, 8 + ctx.rng().Below(56));
        }
        ctx.rng().Fill(a.data(), a.size());
        // Census counts: small values (so sums don't always wrap).
        auto* census = reinterpret_cast<AiAgentData*>(a.data());
        if (ctx.rng().Chance(3, 4)) {
            for (auto& row : census->soldiers) {
                for (std::uint32_t& n : row) {
                    n = ctx.rng().Below(50);
                }
            }
            for (auto& row : census->buildings) {
                for (std::uint32_t& n : row) {
                    n = ctx.rng().Below(10);
                }
            }
        }
        std::uint32_t ra = 0, rb = 0;
        if (op == 6) {
            // Land evaluation: a small grid of cells inside the buffer.
            auto* evaluation = reinterpret_cast<AiLandEvaluation*>(a.data());
            evaluation->width = ctx.rng().Below(7);
            evaluation->height = ctx.rng().Below(7);
        }
        b = a;
        const auto rebase = [](std::vector<std::uint8_t>& buffer) {
            reinterpret_cast<AiLandEvaluation*>(buffer.data())->cells =
                reinterpret_cast<AiGridCell*>(buffer.data() + sizeof(AiLandEvaluation));
        };
        if (op == 6) {
            rebase(a);
            rebase(b);
        }
        const std::uint32_t player = ctx.rng().Below(kPlayers);
        // Vector helpers: first/last/dest inside the buffer.
        const std::uint32_t firstOffset = 0x100 + 4 * ctx.rng().Below(16);
        const std::uint32_t lastOffset = firstOffset + 4 * ctx.rng().Below(20);
        const bool nullDest = ctx.rng().Chance(1, 8);
        const std::uint32_t destOffset = 0x400 + 4 * ctx.rng().Below(16);
        const bool nullFirst = ctx.rng().Chance(1, 6);
        const auto call = [&](const CensusFunctions& f, std::vector<std::uint8_t>& buffer, CGameData* world) {
            g_aiWorld = world;
            g_aiMapWorld = world;
            std::uint8_t* p = buffer.data();
            const auto base = reinterpret_cast<std::uint32_t>(p);
            switch (op) {
                case 0:
                    return f.construct(p, nullptr) - base;
                case 1:
                    return f.countBuildings(p, nullptr);
                case 2:
                    return f.teamStrength(p, nullptr) - base;
                case 3:
                    return f.walkRegion(player);
                case 4: {
                    auto* vector = reinterpret_cast<PtrVector*>(p);
                    vector->first = nullFirst ? 0 : static_cast<std::int32_t>(base + firstOffset);
                    vector->last = static_cast<std::int32_t>(base + lastOffset);
                    const std::uint32_t r = f.vectorSize(p, nullptr);
                    vector->first = vector->last = 0;
                    return r;
                }
                case 5: {
                    std::uint32_t* dest = nullDest ? nullptr : reinterpret_cast<std::uint32_t*>(p + destOffset);
                    const std::uint32_t r =
                        f.copy(reinterpret_cast<std::uint32_t*>(p + firstOffset),
                               reinterpret_cast<std::uint32_t*>(p + lastOffset), dest);
                    return nullDest ? r : r - base;
                }
                default:
                    return f.sumAllCells(p, nullptr);
            }
        };
        if (op == 5 && nullDest && lastOffset != firstOffset) {
            continue;  // a null destination is only safe with an empty range
        }
        std::memcpy(VectorsLow(), savedVectors.data(), kVectorsBytes);
        ra = call(original, a, worlds.original());
        std::memcpy(vectorsA.data(), VectorsLow(), kVectorsBytes);
        std::memcpy(VectorsLow(), savedVectors.data(), kVectorsBytes);
        rb = call(candidate, b, worlds.ours());
        std::memcpy(vectorsB.data(), VectorsLow(), kVectorsBytes);
        std::memcpy(VectorsLow(), savedVectors.data(), kVectorsBytes);
        if (op == 6) {
            // The cell pointers differ per side.
            reinterpret_cast<AiLandEvaluation*>(a.data())->cells = nullptr;
            reinterpret_cast<AiLandEvaluation*>(b.data())->cells = nullptr;
        }
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size());
        same &= S3RE_EXPECT_MEM_EQ(vectorsA.data(), vectorsB.data(), kVectorsBytes);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
        }
    }
    g_aiWorld = savedWorld;
    g_aiMapWorld = savedMapWorld;
    std::memcpy(VectorsLow(), savedVectors.data(), kVectorsBytes);
    worlds.Checkpoint(ctx);  // nothing writes the world
}

std::uint32_t __fastcall WrongCountBuildings(void* self, void* /*edx*/) {
    // Wrong: also counts buildings under construction.
    AiAgentData* census = AsCensus(self);
    std::memset(census->buildings, 0, sizeof(census->buildings));
    std::uint32_t* counts = &census->buildings[0][0];
    const CGameData* world = g_aiMapWorld;
    for (std::uint32_t id = 1; id <= world->building_high_water; ++id) {
        const layout::Building& building = world->buildings[id];
        if (building.owner < kPlayers) {
            ++counts[building.owner * kBuildingTypeCount + building.art];
        }
    }
    census->unk002 = 1;
    census->buildings_counted = 1;
    return world->building_high_water + 1;
}

std::uint32_t __fastcall WrongTeamStrength(void* self, void* edx) {
    // Wrong: castles count once.
    const std::uint32_t result = AI_Census_ComputeTeamStrength(self, edx);
    AiAgentData* census = AsCensus(self);
    for (std::size_t i = 0; i < kPlayers; ++i) {
        for (std::size_t j = 0; j < kPlayers; ++j) {
            const CGameData* world = g_aiMapWorld;
            if (world->players[i].active != 0 && (i == j || world->players[j].team == world->players[i].team)) {
                census->team_strength[i] -=
                    kTowerWeight * census->buildings[j][static_cast<std::size_t>(BuildingType::Castle)];
            }
        }
    }
    return result;
}

std::uint32_t __stdcall WrongWalkRegion(std::uint32_t player) {
    // Wrong: does not step past blocked tiles.
    const layout::Player& record = g_aiMapWorld.get()->players[player];
    if (!AI_IsOnMap(record.start_x) || !AI_IsOnMap(record.start_y)) {
        return 0;
    }
    return g_aiMapWorld.get()->tiles[record.start_x][record.start_y].walk_region;
}

std::uint32_t __fastcall WrongVectorSize(void* self, void* /*edx*/) {
    // Wrong: no null check (a null first gives last >> 2).
    const auto* vector = static_cast<const PtrVector*>(self);
    return static_cast<std::uint32_t>((vector->last - vector->first) >> 2);
}

}  // namespace

S3RE_TEST(AiCensus_MatchOriginal) {
    CompareCensus(ctx, OurCensus(), 8000);
}

S3RE_TEST(AiCensus_ComparisonDetectsWrongImplementation) {
    const auto probe = [&](auto change) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CensusFunctions wrong = OurCensus();
        change(wrong);
        CompareCensus(quiet, wrong, 2000);
        S3RE_EXPECT(quiet.failures() > 0);
    };
    probe([](CensusFunctions& f) { f.countBuildings = &WrongCountBuildings; });
    probe([](CensusFunctions& f) { f.teamStrength = &WrongTeamStrength; });
    probe([](CensusFunctions& f) { f.walkRegion = &WrongWalkRegion; });
    probe([](CensusFunctions& f) { f.vectorSize = &WrongVectorSize; });
}

}  // namespace s3re::game
