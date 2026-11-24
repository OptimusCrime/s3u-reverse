// Computer-player queries about the land around a tile (computer players
// A2, session 52; Rainer\compGlobs.cpp and CompMapWorkerAgent.cpp;
// re/computer_players.md).
//
//   0x00571f60 AI::ResourceAmountOfKindAt(x, y, kind) fastcall (CX, DX,
//              stack word; ret 4): the resource amount of the tile
//              (AI_ResourceAmountAt) if its resource kind
//              (AI_ResourceKindAt) is `kind`, else 0. Both callees get
//              pointers to local copies of x and y. EAX: the amount
//              query's, or the kind query's with AX cleared (reproduced;
//              off the map those callees leave their caller's upper half).
//   0x00571fa0 AI::ResourceAmountNear(x, y, kind) fastcall (CX, DX, stack
//              word; ret 4): ResourceAmountOfKindAt summed over the tile and
//              the two hexagonal rings around it, walked with an
//              AI::WrappedPos on the AI's map size (one step in direction 4
//              to the next ring, then `ring` steps in each of the
//              directions 0..5). AX = the sum; EAX's upper half is that of
//              the last ResourceAmountOfKindAt (reproduced). Used by
//              HOUSE_GROUND_REQUEST_NOTE::CheckSite 0x5760d0 for mines.
//   0x0059a720 AI::IsFarmLand(player, x, y) fastcall (const u8* player,
//              const u16* x, stack const u16* y; ret 4): the land
//              evaluation cell of the tile (CEvaLand::GridCellOfTile on
//              g_aiLandEvaluation): 1 if its grain_land_around is above 40 (G:
//              grain land), or for a player of race word 0 (Roman) its
//              wine_land_around is at least 10 (G: wine land), or for race word 2
//              (Asian) its rice_land_percent is at least 10 (G: rice land; the raw
//              value, not rice_land_around); else 0. Caller CCentrumCheckAgent's
//              run 0x560e00 (before it tears down a woodcutter).
//   0x0058a250 AI::NextSparseMapOffset(int* index, int* dx, int* dy)
//              fastcall (ECX, stack; ret 8): for *index >= 20000 (signed)
//              dx = dy = 0 and EAX = the dx pointer; else the map offset
//              table entry (g_aiMapWorld->offsets) of *index, and *index
//              grows by *index / 32 (signed, rounded toward zero), a search
//              pattern that thins out with the distance. *index is re-read
//              after each write (the pointers may alias). EAX = the new
//              index. Caller AI::RandomPointNearSettler... 0x595050.

#include "game/ai/ai_layout.h"
#include "game/enums/races.h"
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

namespace {

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kResourceAmountAt = 0x00599990;       // AI_ResourceAmountAt
constexpr std::uintptr_t kResourceKindAt = 0x005999e0;         // AI_ResourceKindAt
constexpr std::uintptr_t kResourceAmountOfKindAt = 0x00571f60;  // AI::ResourceAmountOfKindAt
constexpr std::uintptr_t kWrappedPosConstruct = 0x00597090;    // AI_WrappedPos_ConstructXywh
constexpr std::uintptr_t kWrappedPosStep = 0x00597110;         // AI_WrappedPos_Step
constexpr std::uintptr_t kGridCellOfTile = 0x00554770;         // AI_CEvaLand_GridCellOfTile

constexpr std::uint16_t kRings = 2;
constexpr std::uint16_t kSides = 6;
constexpr std::uint32_t kToNextRing = 4;  // the step direction (x - 1, y - 1) onto the next ring

constexpr std::uint8_t kGrainLandAbove = 40;
constexpr std::uint8_t kWineLandFrom = 10;
constexpr std::uint8_t kRiceLandFrom = 10;

constexpr std::int32_t kSparseOffsets = 20000;
constexpr std::int32_t kSparseGrowth = 32;

using CoordFn = std::uint32_t(__fastcall*)(const std::uint16_t* x, const std::uint16_t* y);

std::uint32_t AmountOfKindAt(std::uint32_t x, std::uint32_t y, std::uint32_t kind) {
    using Fn = std::uint32_t(__fastcall*)(std::uint32_t x, std::uint32_t y, std::uint32_t kind);
    return reinterpret_cast<Fn>(kResourceAmountOfKindAt)(x, y, kind);
}

void Step(WrappedPos* position, std::uint32_t direction) {
    using Fn = void(__fastcall*)(WrappedPos* self, void* edx, std::uint32_t direction);
    reinterpret_cast<Fn>(kWrappedPosStep)(position, nullptr, direction);
}

}  // namespace

using AmountOfKindFn = std::uint32_t(__fastcall*)(std::uint32_t x, std::uint32_t y, std::uint32_t kind);
using IsFarmLandFn = std::uint32_t(__fastcall*)(const std::uint8_t* player, const std::uint16_t* x,
                                                const std::uint16_t* y);
using SparseOffsetFn = std::uint32_t(__fastcall*)(std::int32_t* index, void* edx, std::int32_t* dx,
                                                  std::int32_t* dy);

std::uint32_t __fastcall AI_ResourceAmountOfKindAt(std::uint32_t x, std::uint32_t y, std::uint32_t kind);
std::uint32_t __fastcall AI_ResourceAmountNear(std::uint32_t x, std::uint32_t y, std::uint32_t kind);
std::uint32_t __fastcall AI_IsFarmLand(const std::uint8_t* player, const std::uint16_t* x, const std::uint16_t* y);
std::uint32_t __fastcall AI_NextSparseMapOffset(std::int32_t* index, void* edx, std::int32_t* dx, std::int32_t* dy);

S3RE_HOOK(0x00571f60, AI_ResourceAmountOfKindAt, AmountOfKindFn, Original_AI_ResourceAmountOfKindAt,
          AI_ResourceAmountOfKindAt);
S3RE_HOOK(0x00571fa0, AI_ResourceAmountNear, AmountOfKindFn, Original_AI_ResourceAmountNear, AI_ResourceAmountNear);
S3RE_HOOK(0x0059a720, AI_IsFarmLand, IsFarmLandFn, Original_AI_IsFarmLand, AI_IsFarmLand);
S3RE_HOOK(0x0058a250, AI_NextSparseMapOffset, SparseOffsetFn, Original_AI_NextSparseMapOffset,
          AI_NextSparseMapOffset);

// Purpose: How much of a given resource (coal, iron, gold, ...) lies in
// the ground of a tile, for the computer players' mine planning (0 for
// another resource).
std::uint32_t __fastcall AI_ResourceAmountOfKindAt(std::uint32_t xArg, std::uint32_t yArg, std::uint32_t kind) {
    const auto x = static_cast<std::uint16_t>(xArg);
    const auto y = static_cast<std::uint16_t>(yArg);
    const std::uint32_t found = reinterpret_cast<CoordFn>(kResourceKindAt)(&x, &y);
    if (static_cast<std::uint16_t>(found) != static_cast<std::uint16_t>(kind)) {
        return found & 0xffff0000u;  // AX cleared, the rest is the query's
    }
    return reinterpret_cast<CoordFn>(kResourceAmountAt)(&x, &y);
}

// Purpose: How much of a resource lies in the ground on and around a tile
// (two rings), for the computer players' choice of a mine site.
std::uint32_t __fastcall AI_ResourceAmountNear(std::uint32_t x, std::uint32_t y, std::uint32_t kind) {
    using ConstructFn = WrappedPos*(__fastcall*)(WrappedPos* self, void* edx, std::uint32_t x, std::uint32_t y,
                                                 std::uint32_t width, std::uint32_t height);
    WrappedPos position;
    reinterpret_cast<ConstructFn>(kWrappedPosConstruct)(&position, nullptr, x, y, g_aiMapSizeX, g_aiMapSizeY);
    std::uint32_t last = AmountOfKindAt(x, y, kind);
    std::uint32_t total = last;
    for (std::uint16_t ring = 1; ring <= kRings; ++ring) {
        Step(&position, kToNextRing);
        for (std::uint16_t side = 0; side < kSides; ++side) {
            for (std::uint16_t k = 0; k < ring; ++k) {
                Step(&position, side);
                last = AmountOfKindAt(static_cast<std::uint16_t>(position.x), static_cast<std::uint16_t>(position.y),
                                      kind);
                total += last;
            }
        }
    }
    return (last & 0xffff0000u) | (total & 0xffff);  // AX: the sum
}

// Purpose: Whether the land around a tile is good farm land for the
// player's race (grain; wine for the Romans, rice for the Asians), so that
// the computer player keeps it free of a woodcutter (probably).
std::uint32_t __fastcall AI_IsFarmLand(const std::uint8_t* player, const std::uint16_t* x, const std::uint16_t* y) {
    using CellFn = const AiGridCell*(__fastcall*)(std::uint32_t self, void* edx, std::uint32_t x, std::uint32_t y);
    const AiGridCell* cell = reinterpret_cast<CellFn>(kGridCellOfTile)(g_aiLandEvaluation, nullptr, *x, *y);
    if (cell->grain_land_around > kGrainLandAbove) {
        return 1;
    }
    // The race's low word, as the original compares it.
    const auto race = static_cast<std::uint16_t>(g_aiWorld.get()->players[*player].race);
    switch (race) {
    case Value(Race::Roman): return cell->wine_land_around >= kWineLandFrom ? 1 : 0;
    case Value(Race::Asian): return cell->rice_land_percent >= kRiceLandFrom ? 1 : 0;
    default: return 0;
    }
}

// Purpose: The next map offset of a search around a point that takes
// fewer offsets the farther it gets, for the computer players' choice of
// a random point near a settler.
std::uint32_t __fastcall AI_NextSparseMapOffset(std::int32_t* index, void* /*edx*/, std::int32_t* dx,
                                                std::int32_t* dy) {
    if (*index >= kSparseOffsets) {
        *dx = 0;
        *dy = 0;
        return reinterpret_cast<std::uint32_t>(dx);
    }
    *dx = g_aiMapWorld.get()->offsets[*index].dx;
    *dy = g_aiMapWorld.get()->offsets[*index].dy;  // index re-read: dx may alias it
    const std::int32_t current = *index;
    *index = current + current / kSparseGrowth;
    return static_cast<std::uint32_t>(*index);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

struct LandQueries {
    AmountOfKindFn amountOfKind, amountNear;
    IsFarmLandFn farmLand;
    SparseOffsetFn sparse;
};

LandQueries OriginalQueries() {
    return {Original_AI_ResourceAmountOfKindAt, Original_AI_ResourceAmountNear, Original_AI_IsFarmLand,
            Original_AI_NextSparseMapOffset};
}

LandQueries OurQueries() {
    return {&AI_ResourceAmountOfKindAt, &AI_ResourceAmountNear, &AI_IsFarmLand, &AI_NextSparseMapOffset};
}

struct SavedGlobals {
    CGameData* world = g_aiWorld;
    CGameData* mapWorld = g_aiMapWorld;
    std::uint32_t land = g_aiLandEvaluation;
    std::uint16_t sizeX = g_aiMapSizeX, sizeY = g_aiMapSizeY;
    ~SavedGlobals() {
        g_aiWorld = world;
        g_aiMapWorld = mapWorld;
        g_aiLandEvaluation = land;
        g_aiMapSizeX = sizeX;
        g_aiMapSizeY = sizeY;
    }
};

constexpr std::uint32_t kGridSide = 16;  // cells per side of the test grid

std::uint8_t RandomResources(test::Rng& rng) {
    // Mostly a few kinds (high nibble) so that the asked kind is often there.
    return static_cast<std::uint8_t>(rng.Chance(3, 4) ? (rng.Below(3) << 4) | rng.Below(16) : rng.Next32());
}

std::uint8_t RandomPercent(test::Rng& rng) {
    constexpr std::uint8_t kEdges[] = {9, 10, 11, 39, 40, 41, 0, 255};
    return rng.Chance(1, 2) ? kEdges[rng.Below(sizeof(kEdges))] : static_cast<std::uint8_t>(rng.Next32());
}

void CompareLandQueries(test::Context& ctx, const LandQueries& candidate, int cases) {
    const LandQueries original = OriginalQueries();
    SavedGlobals saved;
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    ctx.rng().Fill(&world->offsets[0], sizeof(world->offsets));
    std::vector<AiGridCell> cells(kGridSide * kGridSide);
    AiLandEvaluation land{};
    land.width = kGridSide;
    land.height = kGridSide;
    land.cells = cells.data();
    g_aiWorld = world;
    g_aiMapWorld = world;
    g_aiLandEvaluation = reinterpret_cast<std::uint32_t>(&land);
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        const auto sizeX = static_cast<std::uint16_t>(1 + rng.Below(60));
        const auto sizeY = static_cast<std::uint16_t>(1 + rng.Below(60));
        g_aiMapSizeX = sizeX;
        g_aiMapSizeY = sizeY;
        const int op = static_cast<int>(rng.Below(4));
        std::uint32_t ra = 0, rb = 0, mask = 0xffffffffu;
        std::int32_t outA[3] = {}, outB[3] = {};
        switch (op) {
        case 0:
        case 1: {
            for (std::uint32_t x = 0; x < 64; ++x) {
                for (std::uint32_t y = 0; y < 64; ++y) {
                    world->tiles[x][y].resources = RandomResources(rng);
                }
            }
            const std::uint32_t x = rng.Below(sizeX + 3) | (rng.Chance(1, 4) ? rng.Next32() & 0xffff0000u : 0);
            const std::uint32_t y = rng.Below(sizeY + 3) | (rng.Chance(1, 4) ? rng.Next32() & 0xffff0000u : 0);
            const std::uint32_t kind = (rng.Below(3) << 4) | (rng.Chance(1, 4) ? rng.Next32() & 0xffffff00u : 0);
            // Off the map the callees keep their caller's upper half of EAX.
            const bool onMap = static_cast<std::uint16_t>(x) < sizeX && static_cast<std::uint16_t>(y) < sizeY;
            mask = op == 1 || !onMap ? 0xffff : 0xffffffffu;
            const AmountOfKindFn fa = op == 0 ? original.amountOfKind : original.amountNear;
            const AmountOfKindFn fb = op == 0 ? candidate.amountOfKind : candidate.amountNear;
            ra = fa(x, y, kind);
            rb = fb(x, y, kind);
            break;
        }
        case 2: {
            for (AiGridCell& cell : cells) {
                cell.grain_land_around = RandomPercent(rng);
                cell.wine_land_around = RandomPercent(rng);
                cell.rice_land_percent = RandomPercent(rng);
            }
            const auto player = static_cast<std::uint8_t>(rng.Below(kPlayers));
            const std::int32_t race = rng.Chance(9, 10) ? static_cast<std::int32_t>(rng.Below(5))
                                                         : static_cast<std::int32_t>(rng.Next32());
            world->players[player].race = race;
            const auto x = static_cast<std::uint16_t>(rng.Below(kGridSide * 8 + 20));
            const auto y = static_cast<std::uint16_t>(rng.Below(kGridSide * 8 + 20));
            ra = original.farmLand(&player, &x, &y);
            rb = candidate.farmLand(&player, &x, &y);
            break;
        }
        default: {
            // Near the end, inside the table, or a little before it (a
            // negative index grows toward 0).
            const std::uint32_t roll = rng.Below(5);
            const std::int32_t start = roll == 0   ? static_cast<std::int32_t>(19990 + rng.Below(20))
                                       : roll == 1 ? -static_cast<std::int32_t>(rng.Below(2000))
                                                   : static_cast<std::int32_t>(rng.Below(20000));
            // 2: dy aliases the index (an aliasing dx would make the dy
            // lookup use a random table value as index).
            const int alias = static_cast<int>(rng.Below(4));
            outA[0] = start;
            outA[1] = static_cast<std::int32_t>(rng.Next32());
            outA[2] = static_cast<std::int32_t>(rng.Next32());
            std::memcpy(outB, outA, sizeof(outA));
            std::int32_t* dxA = &outA[1];
            std::int32_t* dyA = alias == 2 ? &outA[0] : &outA[2];
            std::int32_t* dxB = &outB[1];
            std::int32_t* dyB = alias == 2 ? &outB[0] : &outB[2];
            ra = original.sparse(&outA[0], nullptr, dxA, dyA);
            rb = candidate.sparse(&outB[0], nullptr, dxB, dyB);
            if (ra == reinterpret_cast<std::uint32_t>(dxA)) {  // the dx pointer
                ra = 1;
                rb = rb == reinterpret_cast<std::uint32_t>(dxB) ? 1 : 0;
            }
            break;
        }
        }
        bool same = S3RE_EXPECT_EQ(ra & mask, rb & mask);
        same &= S3RE_EXPECT_MEM_EQ(outA, outB, sizeof(outA));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
        }
    }
}

std::uint32_t __fastcall WrongAmountNear(std::uint32_t x, std::uint32_t y, std::uint32_t kind) {
    // Wrong: only the first ring.
    WrappedPos position{};
    position.x = static_cast<std::int16_t>(x);
    position.y = static_cast<std::int16_t>(y);
    position.width = g_aiMapSizeX;
    position.height = g_aiMapSizeY;
    std::uint32_t total = AmountOfKindAt(x, y, kind);
    Step(&position, kToNextRing);
    for (std::uint32_t side = 0; side < kSides; ++side) {
        Step(&position, side);
        total += AmountOfKindAt(static_cast<std::uint16_t>(position.x), static_cast<std::uint16_t>(position.y), kind);
    }
    return total;
}

std::uint32_t __fastcall WrongFarmLand(const std::uint8_t* player, const std::uint16_t* x, const std::uint16_t* y) {
    // Wrong: the Asians' rice land by rice_land_around.
    using CellFn = const AiGridCell*(__fastcall*)(std::uint32_t self, void* edx, std::uint32_t x, std::uint32_t y);
    const AiGridCell* cell = reinterpret_cast<CellFn>(kGridCellOfTile)(g_aiLandEvaluation, nullptr, *x, *y);
    if (static_cast<std::uint16_t>(g_aiWorld.get()->players[*player].race) == Value(Race::Asian) &&
        cell->grain_land_around <= kGrainLandAbove) {
        return cell->rice_land_around >= kRiceLandFrom ? 1 : 0;
    }
    return AI_IsFarmLand(player, x, y);
}

std::uint32_t __fastcall WrongSparse(std::int32_t* index, void* edx, std::int32_t* dx, std::int32_t* dy) {
    // Wrong: rounds a negative growth down (an arithmetic shift).
    const std::int32_t start = *index;
    const std::uint32_t result = AI_NextSparseMapOffset(index, edx, dx, dy);
    if (start < kSparseOffsets && *index == start + start / kSparseGrowth) {
        *index = start + (start >> 5);
    }
    return result;
}

std::uint32_t __fastcall WrongAmountOfKind(std::uint32_t x, std::uint32_t y, std::uint32_t kind) {
    // Wrong: compares only the low byte of the kind.
    return AI_ResourceAmountOfKindAt(x, y, kind & 0xff);
}

}  // namespace

S3RE_TEST(AiLandQueries_MatchOriginal) {
    CompareLandQueries(ctx, OurQueries(), 4000);
}

S3RE_TEST(AiLandQueries_ComparisonDetectsWrongImplementation) {
    const auto probe = [&](auto change) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        LandQueries wrong = OurQueries();
        change(wrong);
        CompareLandQueries(quiet, wrong, 2000);
        S3RE_EXPECT(quiet.failures() > 0);
    };
    probe([](LandQueries& f) { f.amountNear = &WrongAmountNear; });
    probe([](LandQueries& f) { f.farmLand = &WrongFarmLand; });
    probe([](LandQueries& f) { f.amountOfKind = &WrongAmountOfKind; });
    probe([](LandQueries& f) { f.sparse = &WrongSparse; });
}

}  // namespace s3re::game
