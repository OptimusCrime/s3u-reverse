// The game's thread wait times (Dirk\ConsoleInit.cpp).
//
// App::WaitTime (0x0040d0e0): __fastcall (ECX = kind 0..8), no stack
//   arguments: the wait time in ms of a kind of thread wait, from a table
//   the function builds on its stack: normally the first column, with the
//   "-debug" command-line switch (g_debugSwitch 0x7e916c, set by the
//   command-line parser 0x405e50) the second, where -1 (INFINITE) means no
//   time-out. Callers: the draw, net, bounce and logic threads (CLogicThread::
//   Step, OnStart, ExecuteCommands with kind 5). Kinds above 8 read past the
//   table in the original (other stack slots); all callers pass constants
//   0..8.

#include "game/globals.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstdint>
#include <initializer_list>

namespace s3re::game {

namespace {

constexpr std::uint32_t kInfinite = 0xffffffffu;

struct WaitTimes {
    std::uint32_t normal;
    std::uint32_t debug;  // with -debug
};

constexpr WaitTimes kWaitTimes[] = {
    {60000, kInfinite}, {1000, 1000}, {5000, kInfinite}, {500, 500},   {5000, 5000},
    {5000, kInfinite},  {500, 500},   {300000, kInfinite}, {80000, kInfinite},
};
constexpr std::uint32_t kWaitKinds = sizeof(kWaitTimes) / sizeof(kWaitTimes[0]);

}  // namespace

// Purpose: How long a game thread waits for an event of the given kind
// before it gives up (ms; INFINITE in some kinds with -debug).
std::uint32_t __fastcall App_WaitTime(std::uint32_t kind) {
    const WaitTimes& times = kWaitTimes[kind];  // (callers pass 0..8)
    return g_debugSwitch != 0 ? times.debug : times.normal;
}

using WaitTimeFn = std::uint32_t(__fastcall*)(std::uint32_t kind);
S3RE_HOOK(0x0040d0e0, App_WaitTime, WaitTimeFn, Original_App_WaitTime, App_WaitTime);

namespace {

std::uint32_t __fastcall WrongWaitTime(std::uint32_t kind) {
    return kind == 7 ? 30000 : App_WaitTime(kind);
}

void CompareWaitTimes(test::Context& ctx, WaitTimeFn candidate) {
    const std::uint32_t saved = g_debugSwitch;
    for (const std::uint32_t debug : {0u, 1u, 0x100u}) {
        g_debugSwitch = debug;
        for (std::uint32_t kind = 0; kind < kWaitKinds; ++kind) {
            if (!S3RE_EXPECT_EQ(Original_App_WaitTime(kind), candidate(kind))) {
                ctx.Fail(__FILE__, __LINE__, "  kind %u, debug %u", kind, debug);
            }
        }
    }
    g_debugSwitch = saved;
}

}  // namespace

S3RE_TEST(App_WaitTime_MatchesOriginal) {
    CompareWaitTimes(ctx, &App_WaitTime);
}

S3RE_TEST(App_WaitTime_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareWaitTimes(probe, &WrongWaitTime);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
