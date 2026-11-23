// CGameData::UpdateSettlers (0x004e9670): the settler update of every logic
// step (CLogicThread::Step, after UpdateEnvironment and UpdateEconomy).
// () thiscall, ret; EAX = the settler high water at the end (the caller
// ignores it; reproduced anyway).
//
// For settlers 1..high water (+0x12a5e5c, re-read after every settler):
// free records (owner 0xff) are skipped; a settler whose counter (+0xc) is
// not 0 only counts it down; a soldier-like settler (class 1..9) with flag
// 0x80 (+0x14) gets counter 0 and waits; otherwise its job (+0xe, 0..0x194;
// larger values are skipped) selects a handler through the jump table
// 0x4f9270. A handler can ask for the same settler to run again in the same
// step (after a job change; WalkSettler's EAX for the walking jobs).
//
// The ~300 handlers (16,600 instructions) are in the settler_jobs_*.cpp
// files, one per range of the original's code (settler_jobs.h); this file
// has the loop, the dispatch and the differential test harness
// (settler_jobs_test.h). Analysis: re/update_settlers.md,
// re/update_settlers_jobs.csv.

#include "game/settler_jobs.h"
#include "game/settler_jobs_test.h"
#include "hooks.h"
#include "test/callee_stub.h"

#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

using namespace settler_jobs;

constexpr std::uint8_t kFree = 0xff;             // Settler::owner of a free record
constexpr std::uint8_t kSettlerFlagWaits = 0x80;  // Settler::flags: a soldier that waits (counter held at 0)
constexpr std::uint32_t kLastJob = 0x194;

// The family of each job's handler (by the handler's address in the
// original, settler_jobs.h): u units, b building, m metal, f food, d drinks,
// s special, '-' straight to the loop end (0x4f9202: jobs 0x39, 0x43, 0x89,
// 0x106, 0x10d, 0x114, 0x11b, 0x154, 0x162, 0x172..0x17b, 0x17d).
constexpr char kJobFamily[] =
    "uuuuububbbububbububbbbbsbbbbuubb"  // 0..31
    "bbbbbbbbbbuububbummmmumub-mmmmmm"  // 32..63
    "mmb-mmmmmmmmububbbmmmmmmmmmmmmmm"  // 64..95
    "mmmmmbbbubbmffffffffmffffffffmmm"  // 96..127
    "mmmffffff-bbbsufuffuffffufffufff"  // 128..159
    "fffffffffbbuuuumufuffufffffuufff"  // 160..191
    "ffffffffffffffffffffddmmmmmmmmmm"  // 192..223
    "mmddduddudddduddduddddudduddddud"  // 224..255
    "dduddd-dddddd-dddddd-dddddd-dddd"  // 256..287
    "ddddddddmmmmmmddsffususmmmssssss"  // 288..319
    "uusssssuuuusssssssss-sssssssssss"  // 320..351
    "ss-ssussusussussus----------s-ss"  // 352..383
    "ssssssmmmmmmmmmmsssss";            // 384..404
static_assert(sizeof(kJobFamily) == kLastJob + 2);

JobResult RunJob(SettlerContext& ctx) {
    switch (kJobFamily[ctx.job]) {
    case 'u': return RunUnitJobs(ctx);
    case 'b': return RunBuildingJobs(ctx);
    case 'm': return RunMetalJobs(ctx);
    case 'f': return RunFoodJobs(ctx);
    case 'd': return RunDrinkJobs(ctx);
    case 's': return RunSpecialJobs(ctx);
    default: return JobResult::Next;
    }
}



}  // namespace

// Purpose: Moves every settler on by one step of its current job: walking,
// working in its building, carrying, building, fighting, sailing. This is
// where almost everything the settlers do happens.
std::uint32_t __fastcall CGameData_UpdateSettlers(CGameData* world, void*) {
    FrameSlots frame;
    for (std::uint32_t n = 1; n <= world->settler_high_water; ++n) {
        Settler& settler = SettlerAt(world, n);
        if (settler.owner == kFree) {
            continue;
        }
        if (settler.counter != 0) {
            --settler.counter;
            continue;
        }
        const std::uint32_t type = settler.art;
        // The class table has 48 entries; larger types read past it, as in the original.
        const std::int32_t typeClass = (&g_settlerTypeClass[0])[type];
        if (typeClass >= 1 && typeClass <= 9 && (settler.flags & kSettlerFlagWaits) != 0) {
            settler.counter = 0;  // 0x4f91fb
            continue;
        }
        const std::uint32_t job = settler.job;
        if (job > kLastJob) {
            continue;
        }
        SettlerContext ctx{world, n, settler, job, type, typeClass, frame};
        const JobResult result = RunJob(ctx);
        if (result == JobResult::Again) {
            --n;  // 0x4f9214
        }
    }
    return world->settler_high_water;
}

S3RE_HOOK(0x004e9670, CGameData_UpdateSettlers, UpdateSettlersFn, Original_CGameData_UpdateSettlers,
          CGameData_UpdateSettlers);

// ---------------------------------------------------------------------------
// Test harness (settler_jobs_test.h)
// ---------------------------------------------------------------------------

namespace settler_jobs::testing {

namespace {

constexpr std::size_t kSlack = 8u << 20;
constexpr std::size_t kTestWorld = sizeof(CGameData) + kSlack;

std::uint32_t g_stubSequence = 0;
const FamilyTest* g_family = nullptr;

std::uint32_t Mix(std::uintptr_t callee, const std::uint32_t* args) {
    std::uint64_t h = 0x9e3779b97f4a7c15ull ^ (static_cast<std::uint64_t>(callee) << 32) ^ g_stubSequence++;
    for (int k = 0; k < test::kMaxStubArgs; ++k) {
        h = (h ^ args[k]) * 0xbf58476d1ce4e5b9ull;
        h ^= h >> 31;
    }
    return static_cast<std::uint32_t>(h >> 16);
}

template <int N, std::uintptr_t Address>
void Stub(test::CalleeStubs& stubs) {
    stubs.Install<N, Address>([](CGameData* world, std::uint32_t, const std::uint32_t* args) {
        const std::uint32_t h = Mix(Address, args);
        std::uint32_t eax = 0;
        if (g_family != nullptr && g_family->stubReturn != nullptr &&
            g_family->stubReturn(Address, h, args, world, eax)) {
            return eax;
        }
        return StubReturn(Address, h);
    });
}

void InstallStubs(test::CalleeStubs& stubs) {
    Stub<2, kDestroyBuildingAt>(stubs);
    Stub<1, kBuildingDone>(stubs);
    Stub<1, kReleaseBuildingExtraTiles>(stubs);
    Stub<1, kClearBuildingFlag02>(stubs);
    Stub<1, kClearBuildingFlag04>(stubs);
    Stub<1, kClearBuildingFlag08>(stubs);
    Stub<1, kClearBuildingFlag10>(stubs);
    Stub<2, kBuildingPileHasGoods>(stubs);
    Stub<3, kQueuePendingEvent>(stubs);
    Stub<1, kMinerJobEnter>(stubs);
    Stub<1, kMinerJobFood>(stubs);
    Stub<1, kMinerJobDig>(stubs);
    Stub<2, kTileSlope>(stubs);
    Stub<4, kNotifyLocalPlayer>(stubs);
    Stub<10, kAllocTable36700Record>(stubs);
    Stub<1, kClearAreaGoodsSettings1D2>(stubs);
    Stub<3, kAddStatistic>(stubs);
    Stub<3, kAddPlayerCounter>(stubs);
    Stub<3, kSubtractPlayerCounter>(stubs);
    Stub<1, kWalkSettler>(stubs);
    Stub<3, kSetSettlerTarget>(stubs);
    Stub<4, kCanSettlerEnterTile>(stubs);
    Stub<4, kDropGoodNear>(stubs);
    Stub<1, kAddGoodsToPile>(stubs);
    Stub<1, kTakeGoodFromPile>(stubs);
    Stub<1, kPileIncAmountLeaving>(stubs);
    Stub<1, kPileIncAmountComing>(stubs);
    Stub<1, kStealGoodFromPile>(stubs);
    Stub<4, kCreateSettler>(stubs);
    Stub<5, kSpawnSettlersAround>(stubs);
    Stub<1, kLeaveBuildingSlot>(stubs);
    Stub<1, kEnterBuildingSlot>(stubs);
    Stub<1, kResetSettlerToIdle>(stubs);
    Stub<4, kCreateSevenTileUnit>(stubs);
    Stub<1, kMoveSettlerToBuildingTileA>(stubs);
    Stub<1, kMoveSettlerToBuildingTileB>(stubs);
    Stub<1, kSetSoldierAnimationInBuilding>(stubs);
    Stub<2, kSetSettlerAnimation>(stubs);
    Stub<2, kSetSettlerSavedAnimation>(stubs);
    Stub<1, kRestoreSettlerAnimation>(stubs);
    Stub<1, kCountAreaSettlersOfClass0>(stubs);
    Stub<2, kCountAreaSettlersOfType>(stubs);
    Stub<1, kUpdateSoldier>(stubs);
    Stub<1, kClearSettlerJob>(stubs);
    Stub<2, kAddPileClaim>(stubs);
    Stub<2, kCountOtherPileClaims>(stubs);
    Stub<2, kIsFreeOfCatapultsNearby>(stubs);
    Stub<2, kUpdateShadeAround>(stubs);
    Stub<2, kRecalculateTerritory>(stubs);
    Stub<2, kUpdateBorderFlagsAround>(stubs);
    Stub<3, kSetOwnerOfFreeTile>(stubs);
    Stub<2, kClearTileOwner>(stubs);
    Stub<2, kBlockTile>(stubs);
    Stub<2, kUnblockTile>(stubs);
    Stub<2, kWearPath>(stubs);
    Stub<2, kMakeFieldTile>(stubs);
    Stub<8, kSpawn544630>(stubs);
    Stub<8, kSpawn5449f0>(stubs);
    Stub<7, kSpawn544dc0>(stubs);
    Stub<5, kSpawn545120>(stubs);
}

// One-time set-up: coordinates on the map and small tile areas, so that no
// random word indexes far outside the buffer (record numbers from random
// words stay within the slack).
void SanitizeWorld(test::Context& ctx, test::WorldPair& worlds) {
    auto& rng = ctx.rng();
    worlds.Both([&](CGameData* w) { w->map_size = kMapSize; });
    for (std::uint32_t x = 0; x < layout::kMapRowTiles; ++x) {
        for (std::uint32_t y = 0; y < layout::kMapRowTiles; ++y) {
            const auto area = static_cast<std::uint16_t>(rng.Below(kAreas));
            const auto settler = static_cast<std::uint16_t>(rng.Chance(2, 3) ? 0 : 1 + rng.Below(kSettlers));
            const auto object = static_cast<std::uint16_t>(rng.Below(kBuildings + 1));
            const auto owner = static_cast<std::uint8_t>(rng.Chance(1, 5) ? 0xff : rng.Below(4));
            worlds.Both([&](CGameData* w) {
                Tile& t = w->tiles[x][y];
                t.area = area;
                t.settler = settler;
                t.object_number = object;
                t.owner = owner;
            });
        }
    }
    for (std::uint32_t n = 0; n <= kSettlers; ++n) {
        const std::uint16_t c[6] = {static_cast<std::uint16_t>(8 + rng.Below(kMapSize - 16)),
                                    static_cast<std::uint16_t>(8 + rng.Below(kMapSize - 16)),
                                    static_cast<std::uint16_t>(8 + rng.Below(kMapSize - 16)),
                                    static_cast<std::uint16_t>(8 + rng.Below(kMapSize - 16)),
                                    static_cast<std::uint16_t>(8 + rng.Below(kMapSize - 16)),
                                    static_cast<std::uint16_t>(8 + rng.Below(kMapSize - 16))};
        worlds.Both([&](CGameData* w) {
            Settler& s = SettlerAt(w, n);
            s.x = c[0];
            s.y = c[1];
            s.target_x = c[2];
            s.target_y = c[3];
            s.waypoint_x = c[4];
            s.waypoint_y = c[5];
        });
    }
    for (std::uint32_t b = 0; b <= kBuildings; ++b) {
        const std::uint16_t c[6] = {static_cast<std::uint16_t>(8 + rng.Below(kMapSize - 16)),
                                    static_cast<std::uint16_t>(8 + rng.Below(kMapSize - 16)),
                                    static_cast<std::uint16_t>(8 + rng.Below(kMapSize - 16)),
                                    static_cast<std::uint16_t>(8 + rng.Below(kMapSize - 16)),
                                    static_cast<std::uint16_t>(8 + rng.Below(kMapSize - 16)),
                                    static_cast<std::uint16_t>(8 + rng.Below(kMapSize - 16))};
        const auto race = static_cast<std::uint8_t>(rng.Below(4));
        const auto art = static_cast<std::uint8_t>(1 + rng.Below(0x38));
        worlds.Both([&](CGameData* w) {
            Building& r = BuildingAt(w, b);
            r.x = c[0];
            r.y = c[1];
            r.door_x = c[2];
            r.door_y = c[3];
            r.work_x = c[4];
            r.work_y = c[5];
            r.race = race;
            r.art = art;
        });
    }
    for (std::uint32_t p = 0; p <= kPiles; ++p) {
        const auto x = static_cast<std::uint16_t>(8 + rng.Below(kMapSize - 16));
        const auto y = static_cast<std::uint16_t>(8 + rng.Below(kMapSize - 16));
        worlds.Both([&](CGameData* w) {
            PileAt(w, p).x = x;
            PileAt(w, p).y = y;
        });
    }
    for (std::uint32_t p = 0; p < layout::kPlayers; ++p) {
        const auto race = static_cast<std::int32_t>(rng.Below(4));
        worlds.Both([&](CGameData* w) { w->players[p].race = race; });
    }
}

// The generic part of a case's settler n.
void SetUpSettler(CaseSetup& setup, const FamilyTest& family, std::uint32_t n) {
    auto& rng = setup.rng();
    const Job job = family.jobs[rng.Below(static_cast<std::uint32_t>(family.jobCount))];
    const SettlerType type = family.types != nullptr && rng.Chance(7, 10)
                                 ? family.types[rng.Below(static_cast<std::uint32_t>(family.typeCount))]
                                 : static_cast<SettlerType>(rng.Below(kSettlerTypeCount));
    const auto owner = static_cast<std::uint8_t>(rng.Chance(1, 20) ? kFree : rng.Below(4));
    const auto counter = static_cast<std::uint16_t>(rng.Chance(9, 10) ? 0 : 1 + rng.Below(3));
    const auto flags = static_cast<std::uint8_t>(rng.Chance(1, 10) ? 0x80 | rng.Next32() : rng.Next32() & 0x7f);
    const std::uint16_t x = setup.Coordinate();
    const std::uint16_t y = setup.Coordinate();
    const std::uint16_t building = setup.BuildingNumber();
    const std::uint16_t next = setup.SettlerNumber();
    const std::uint16_t prev = setup.SettlerNumber();
    const std::uint16_t part = setup.SettlerNumber();
    const auto race = static_cast<std::uint8_t>(rng.Below(4));
    const std::uint16_t piles[4] = {setup.PileNumber(), setup.PileNumber(), setup.PileNumber(), setup.PileNumber()};
    const std::uint16_t worker = setup.SettlerNumber();
    const auto buildingOwner = static_cast<std::uint8_t>(rng.Chance(1, 20) ? kFree : rng.Below(4));
    setup.Both([&](CGameData* w) {
        Settler& s = SettlerAt(w, n);
        SetJob(s, job);
        s.art = Value(type);
        s.owner = owner;
        s.race = race;
        s.counter = counter;
        s.flags = flags;
        s.x = x;
        s.y = y;
        s.building = building;
        s.next = next;
        s.prev = prev;
        s.saved_counter = part;
        Building& b = BuildingAt(w, building);
        b.owner = buildingOwner;
        b.pile_ring = piles[0];
        b.piles[0] = piles[1];
        b.piles[1] = piles[2];
        b.piles[2] = piles[3];
        b.worker = worker;
    });
    if (family.customize != nullptr) {
        family.customize(setup, n);
    }
}

}  // namespace

std::uint32_t StubReturn(std::uintptr_t callee, std::uint32_t h) {
    switch (callee) {
    case kWalkSettler: return (h & 3) == 0 ? 1 : 0;  // run again now and then
    case kCreateSettler:
    case kCreateSevenTileUnit: return (h & 0xff) < 64 ? 0 : 1 + (h >> 8) % kSettlers;
    case kCountAreaSettlersOfClass0:
    case kCountAreaSettlersOfType: return h % 8;
    case kTileSlope: return h % 12;
    default: return h & 1;
    }
}

void CompareUpdateSettlers(test::Context& ctx, UpdateSettlersFn candidate, const FamilyTest& family, int cases) {
    PlayerTally* const tallies = &g_playerTallies[0];
    constexpr std::size_t kTallyBytes = sizeof(PlayerTally) * 20 + 0x10;  // and 16 bytes before (0x7acfc4..)
    std::uint8_t* const tallyBytes = reinterpret_cast<std::uint8_t*>(tallies) - 0x10;
    const std::vector<std::uint8_t> savedTallies(tallyBytes, tallyBytes + kTallyBytes);
    const std::uint32_t savedTick = g_gameTick;
    test::WorldPair worlds(ctx, kTestWorld);
    SanitizeWorld(ctx, worlds);
    test::CalleeStubs stubs;
    InstallStubs(stubs);
    g_family = &family;
    DebugLogRecorder logs;
    CaseSetup setup(ctx, worlds);
    int compared = 0;
    for (int i = 0; i < cases; ++i) {
        const auto high = static_cast<std::uint32_t>(1 + ctx.rng().Below(kMaxCaseSettlers));
        setup.Both([&](CGameData* w) { w->settler_high_water = high; });
        for (std::uint32_t n = 1; n <= high; ++n) {
            SetUpSettler(setup, family, n);
        }
        const std::uint32_t tick = ctx.rng().Next32();
        g_gameTick = tick;
        const std::vector<std::uint8_t> before(tallyBytes, tallyBytes + kTallyBytes);

        stubs.SetWorld(worlds.original());
        g_stubSequence = 0;
        const std::uint32_t expected = Original_CGameData_UpdateSettlers(worlds.original(), nullptr);
        const std::vector<test::StubCall> expectedCalls = stubs.Take();
        const std::vector<DebugLogCall> expectedLogs = logs.Take();
        const std::vector<std::uint8_t> expectedTallies(tallyBytes, tallyBytes + kTallyBytes);

        std::memcpy(tallyBytes, before.data(), kTallyBytes);
        g_gameTick = tick;
        stubs.SetWorld(worlds.ours());
        g_stubSequence = 0;
        const std::uint32_t actual = candidate(worlds.ours(), nullptr);
        const std::vector<test::StubCall> actualCalls = stubs.Take();
        const std::vector<DebugLogCall> actualLogs = logs.Take();
        const std::vector<std::uint8_t> actualTallies(tallyBytes, tallyBytes + kTallyBytes);

        ++compared;
        const bool checkpoint = compared % 32 == 0 || i == cases - 1;
        std::size_t firstCall = 0;
        while (firstCall < expectedCalls.size() && firstCall < actualCalls.size() &&
               expectedCalls[firstCall] == actualCalls[firstCall]) {
            ++firstCall;
        }
        if (!S3RE_EXPECT_EQ(expected, actual) || !S3RE_EXPECT_EQ(expectedCalls.size(), actualCalls.size()) ||
            !S3RE_EXPECT(expectedCalls == actualCalls) || !S3RE_EXPECT(expectedLogs == actualLogs) ||
            !S3RE_EXPECT(expectedTallies == actualTallies) ||
            !worlds.Same(ctx, offsetof(CGameData, settlers), sizeof(Settler) * (kSettlers + 1)) ||
            !worlds.SameField(ctx, [](CGameData* w) -> auto& { return w->players; }) ||
            !worlds.SameField(ctx, [](CGameData* w) -> auto& { return w->random_state; }) ||
            (checkpoint && !worlds.Checkpoint(ctx))) {
            const Settler& s = SettlerAt(worlds.original(), 1);
            ctx.Fail(__FILE__, __LINE__,
                     "  case %d (or one of the 31 before it): high water %u, settler 1 art %u job 0x%x; "
                     "%u/%u stub calls, first difference at call %u (callee 0x%x / 0x%x)",
                     i, high, s.art, s.job, static_cast<unsigned>(expectedCalls.size()),
                     static_cast<unsigned>(actualCalls.size()), static_cast<unsigned>(firstCall),
                     firstCall < expectedCalls.size() ? static_cast<unsigned>(expectedCalls[firstCall].callee) : 0u,
                     firstCall < actualCalls.size() ? static_cast<unsigned>(actualCalls[firstCall].callee) : 0u);
            break;
        }
    }
    g_family = nullptr;
    std::memcpy(tallyBytes, savedTallies.data(), kTallyBytes);
    g_gameTick = savedTick;
    if (!S3RE_EXPECT(compared >= family.minCompared)) {
        ctx.Fail(__FILE__, __LINE__, "  only %d cases compared", compared);
    }
}

}  // namespace settler_jobs::testing

namespace {

using settler_jobs::testing::FamilyTest;

// The loop alone: jobs whose handler is the loop end, and jobs above 0x194.
constexpr Job kLoopJobs[] = {Job::Job57,  Job::Job67,  Job::Job137, Job::Job262, Job::Job269, Job::Job276,
                             Job::Job283, Job::Job340, Job::Job354, Job::Job370, Job::Job381,
                             static_cast<Job>(0x195), static_cast<Job>(0x1ff), static_cast<Job>(0xffff)};

// Wrong: the last settler is left out.
std::uint32_t __fastcall WrongUpdateSettlersLast(CGameData* world, void* edx) {
    --world->settler_high_water;
    CGameData_UpdateSettlers(world, edx);
    return ++world->settler_high_water;
}

}  // namespace

S3RE_TEST(CGameData_UpdateSettlers_Loop_MatchesOriginal) {
    const FamilyTest family{kLoopJobs, sizeof(kLoopJobs) / sizeof(kLoopJobs[0])};
    settler_jobs::testing::CompareUpdateSettlers(ctx, &CGameData_UpdateSettlers, family, 2000);
}

S3RE_TEST(CGameData_UpdateSettlers_Loop_ComparisonDetectsWrongLoop) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    const FamilyTest family{kLoopJobs, sizeof(kLoopJobs) / sizeof(kLoopJobs[0])};
    settler_jobs::testing::CompareUpdateSettlers(probe, &WrongUpdateSettlersLast, family, 400);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
