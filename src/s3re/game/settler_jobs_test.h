#pragma once

// Differential test harness of CGameData::UpdateSettlers (0x004e9670),
// shared by the settler_jobs_*.cpp files (defined in update_settlers.cpp).
//
// A family's test calls CompareUpdateSettlers with its jobs (and settler
// types): every case gives settlers 1..high water (1..6) a job from the
// list, runs the original UpdateSettlers on one world and ours on a copy,
// and compares the calls of every non-trivial callee (recording stubs, see
// below), the DebugLog calls, the player tallies, the settler records and,
// every 32 cases, the whole world.
//
// The world is a full CGameData plus 8 MB of slack, filled with random
// bytes once and sanitized where numbers become far-reaching indexes
// (coordinates, tile areas); record numbers taken from random 16-bit words
// stay inside the buffer thanks to the slack.
//
// Callees: the queries without side effects that are cheap and verified run
// for real (Random, RandomBelow6, NeighbourX/Y, Next/PreviousDirection,
// TileDistance, IsOnMap, TileScreenX/Y, SettlerStartHealth, NoOp_ret4);
// every other callee is a recording stub (test/callee_stub.h) whose return
// value comes from a hash of the callee, its arguments and its position in
// the call sequence (StubReturn; a family can override it). The stubs have
// no side effects unless a family's override adds them.

#include "game/settler_jobs.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game {

using UpdateSettlersFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx);
std::uint32_t __fastcall CGameData_UpdateSettlers(CGameData* world, void* edx);
extern UpdateSettlersFn Original_CGameData_UpdateSettlers;

}  // namespace s3re::game

namespace s3re::game::settler_jobs::testing {

// Record numbers the sanitized world uses (settlers 1..kSettlers, ...).
constexpr std::uint32_t kSettlers = 200;
constexpr std::uint32_t kBuildings = 100;
constexpr std::uint32_t kPiles = 400;
constexpr std::uint32_t kAreas = 64;
constexpr std::uint32_t kMapSize = 384;
constexpr std::uint32_t kMaxCaseSettlers = 6;

// Writes the same values into both worlds of a case.
class CaseSetup {
public:
    CaseSetup(test::Context& ctx, test::WorldPair& worlds) : ctx_(ctx), worlds_(worlds) {}
    test::Rng& rng() { return ctx_.rng(); }
    // f(world) on both worlds, e.g. Both([&](CGameData* w) { SettlerAt(w, n).good = g; });
    template <typename F>
    void Both(F f) {
        worlds_.Both(f);
    }
    // A coordinate on the map (with a margin of 8 tiles).
    std::uint16_t Coordinate() { return static_cast<std::uint16_t>(8 + rng().Below(kMapSize - 16)); }
    // A record number, sometimes 0.
    std::uint16_t SettlerNumber() { return static_cast<std::uint16_t>(rng().Below(kSettlers + 1)); }
    std::uint16_t BuildingNumber() { return static_cast<std::uint16_t>(rng().Below(kBuildings + 1)); }
    std::uint16_t PileNumber() { return static_cast<std::uint16_t>(rng().Below(kPiles + 1)); }

private:
    test::Context& ctx_;
    test::WorldPair& worlds_;
};

struct FamilyTest {
    const Job* jobs;
    std::size_t jobCount;
    // Settler types to draw (70% of the settlers; the rest any of 0..47);
    // nullptr: always any type.
    const SettlerType* types = nullptr;
    std::size_t typeCount = 0;
    // Optional: more set-up for settler n of the case (after the generic
    // part: job, type, owner, counter, flags, coordinates and the record
    // numbers in the settler and its building are set).
    void (*customize)(CaseSetup& setup, std::uint32_t n) = nullptr;
    // Optional: the return value (and side effects on `world`) of a stubbed
    // callee; return false to use the default (StubReturn). `h` is the
    // call's hash.
    bool (*stubReturn)(std::uintptr_t callee, std::uint32_t h, const std::uint32_t* args, CGameData* world,
                       std::uint32_t& eax) = nullptr;
    // Fewest compared (not skipped) cases the run must reach.
    int minCompared = 1;
};

// The default stub return values: 0/1 for most callees, small counts for
// the counting ones, settler numbers for CreateSettler, ...
std::uint32_t StubReturn(std::uintptr_t callee, std::uint32_t h);

// Runs `cases` cases of `family`; fails the test on the first difference.
void CompareUpdateSettlers(test::Context& ctx, UpdateSettlersFn candidate, const FamilyTest& family, int cases);

}  // namespace s3re::game::settler_jobs::testing
