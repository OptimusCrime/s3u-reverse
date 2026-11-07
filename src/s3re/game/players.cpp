// Per-player data in the world object.
//
// Player records: 20 records of 0x148 bytes at world+0x11943dc (world.h).
// At record+0xdc (world+0x11944b8) is an array of 16-bit counters, indexed
// by a small number (goods or settler types; not settled yet).
//
// GameWorld::AddPlayerCounter (0x004ffb00): (player, index,
// amount) adds the low 16 bits of `amount` to counter[index] of `player`.
// Does nothing for player 0xFF ("nobody") or while the byte flag at
// world+0x2d250 is set.
// GameWorld::SubtractPlayerCounter (0x004ffb40): same arguments and
// conditions; subtracts `amount`, but stores 0 if `amount` (compared as an
// unsigned 32-bit value) is larger than the counter.
// Both: thiscall, three stack arguments, ret 0xc. Some callers only overwrite
// AL after the call, so EAX is reproduced exactly (see the functions).
//
// GameWorld::ClearPlayerListLengths (0x0050c9c0): zeroes 200 int32 at
// world+0x11940bc: the lengths of 20 players x 10 lists whose entries
// (uint16, up to 100 each) are at world+0x118a47c; ExecuteCommands sets them
// (command 3). thiscall, no arguments, plain ret; EAX ends up 0.

#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

constexpr std::size_t kPlayerCounters = 0x11944b8;  // world offset of player 0's counters
constexpr std::size_t kWorldUpdateDisabledFlag = 0x2d250;
constexpr std::uint32_t kNoPlayer = 0xff;
constexpr std::size_t kPlayerListLengths = 0x11940bc;
constexpr std::size_t kPlayerListLengthCount = 200;

// The original's index arithmetic: index + player * 164 (164 words = 0x148
// bytes per record), 32-bit.
std::uint32_t CounterIndex(std::uint32_t player, std::uint32_t index) {
    return index + player * (world_offset::kPlayerRecordBytes / 2);
}

std::uint16_t* Counter(GameWorld* world, std::uint32_t slot) {
    return reinterpret_cast<std::uint16_t*>(reinterpret_cast<std::uint8_t*>(world) +
                                            kPlayerCounters + slot * 2u);
}

bool CountersFrozen(GameWorld* world, std::uint32_t player) {
    return player == kNoPlayer || WorldField<std::uint8_t>(world, kWorldUpdateDisabledFlag) != 0;
}

}  // namespace

using PlayerCounterFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx,
                                                   std::uint32_t player, std::uint32_t index,
                                                   std::uint32_t amount);
using ClearFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx);

// EAX: `player` when nothing is done, else the counter's address.
std::uint32_t __fastcall GameWorld_AddPlayerCounter(GameWorld* world, void* /*edx*/,
                                                    std::uint32_t player, std::uint32_t index,
                                                    std::uint32_t amount) {
    if (CountersFrozen(world, player)) {
        return player;
    }
    std::uint16_t* counter = Counter(world, CounterIndex(player, index));
    *counter = static_cast<std::uint16_t>(*counter + amount);
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(counter));
}

// EAX: `player` when nothing is done; otherwise the original has the slot
// number in EAX with its low 16 bits replaced by the old counter value, minus
// `amount` if that was subtracted.
std::uint32_t __fastcall GameWorld_SubtractPlayerCounter(GameWorld* world, void* /*edx*/,
                                                         std::uint32_t player,
                                                         std::uint32_t index,
                                                         std::uint32_t amount) {
    if (CountersFrozen(world, player)) {
        return player;
    }
    const std::uint32_t slot = CounterIndex(player, index);
    std::uint16_t* counter = Counter(world, slot);
    const std::uint32_t eax = (slot & 0xffff0000u) | *counter;
    if (amount > *counter) {
        *counter = 0;
        return eax;
    }
    *counter = static_cast<std::uint16_t>(*counter - amount);
    return eax - amount;
}

std::uint32_t __fastcall GameWorld_ClearPlayerListLengths(GameWorld* world, void* /*edx*/) {
    std::memset(&WorldField<std::int32_t>(world, kPlayerListLengths), 0,
                kPlayerListLengthCount * sizeof(std::int32_t));
    return 0;
}

S3RE_HOOK(0x004ffb00, GameWorld_AddPlayerCounter, PlayerCounterFn,
          Original_GameWorld_AddPlayerCounter, GameWorld_AddPlayerCounter);
S3RE_HOOK(0x004ffb40, GameWorld_SubtractPlayerCounter, PlayerCounterFn,
          Original_GameWorld_SubtractPlayerCounter, GameWorld_SubtractPlayerCounter);
S3RE_HOOK(0x0050c9c0, GameWorld_ClearPlayerListLengths, ClearFn,
          Original_GameWorld_ClearPlayerListLengths, GameWorld_ClearPlayerListLengths);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestCounters = 64;  // indices tried per player
constexpr std::size_t kCountersWorld =
    kPlayerCounters + (19 * 164 + kTestCounters) * 2 + 0x100;

void ComparePlayerCounter(test::Context& ctx, PlayerCounterFn original,
                          PlayerCounterFn candidate, int cases) {
    test::WorldPair worlds(ctx, kCountersWorld);
    for (int i = 0; i < cases; ++i) {
        std::uint32_t player = ctx.rng().Below(20);
        if (ctx.rng().Chance(1, 10)) {
            player = kNoPlayer;
        }
        const std::uint32_t index = ctx.rng().Below(kTestCounters);
        std::uint32_t amount;
        switch (ctx.rng().Below(4)) {
        case 0:
            amount = ctx.rng().Next32();
            break;
        case 1:
            amount = 0x10000u + ctx.rng().Below(4);  // only the low 16 bits are added
            break;
        default:
            amount = ctx.rng().Below(200);
            break;
        }
        worlds.Set<std::uint8_t>(kWorldUpdateDisabledFlag, ctx.rng().Chance(1, 8) ? 1 : 0);
        // Small counters half the time, so subtraction hits the floor.
        if (player != kNoPlayer && ctx.rng().Chance(1, 2)) {
            worlds.Set<std::uint16_t>(kPlayerCounters + CounterIndex(player, index) * 2,
                                      static_cast<std::uint16_t>(ctx.rng().Below(300)));
        }
        // Return values that are addresses differ between the two buffers;
        // compare them relative to the buffer.
        auto relative = [](std::uint32_t value, GameWorld* world) {
            const auto base = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));
            return value - base;
        };
        const std::uint32_t expected = original(worlds.original(), nullptr, player, index, amount);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, player, index, amount);
        const bool isAddress = expected > 0x10000u && actual > 0x10000u &&
                               relative(expected, worlds.original()) < kCountersWorld;
        bool same = isAddress ? S3RE_EXPECT_EQ(relative(expected, worlds.original()),
                                               relative(actual, worlds.ours()))
                              : S3RE_EXPECT_EQ(expected, actual);
        same &= (i % 128 == 127) ? worlds.Checkpoint(ctx)
                                 : worlds.Same(ctx, kPlayerCounters, kCountersWorld -
                                                                         kPlayerCounters);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: player %u, index %u, amount 0x%08x", i,
                     player, index, amount);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

// Wrong: ignores the "world update disabled" flag.
std::uint32_t __fastcall WrongAddPlayerCounter(GameWorld* world, void* edx, std::uint32_t player,
                                               std::uint32_t index, std::uint32_t amount) {
    const std::uint8_t flag = WorldField<std::uint8_t>(world, kWorldUpdateDisabledFlag);
    WorldField<std::uint8_t>(world, kWorldUpdateDisabledFlag) = 0;
    const std::uint32_t result = GameWorld_AddPlayerCounter(world, edx, player, index, amount);
    WorldField<std::uint8_t>(world, kWorldUpdateDisabledFlag) = flag;
    return result;
}

// Wrong: lets the counter wrap instead of stopping at 0.
std::uint32_t __fastcall WrongSubtractPlayerCounter(GameWorld* world, void* /*edx*/,
                                                    std::uint32_t player, std::uint32_t index,
                                                    std::uint32_t amount) {
    if (CountersFrozen(world, player)) {
        return player;
    }
    const std::uint32_t slot = CounterIndex(player, index);
    std::uint16_t* counter = Counter(world, slot);
    const std::uint32_t eax = (slot & 0xffff0000u) | *counter;
    *counter = static_cast<std::uint16_t>(*counter - amount);
    return eax - amount;
}

void CompareClearLists(test::Context& ctx, ClearFn candidate, int cases) {
    const std::size_t size = kPlayerListLengths + kPlayerListLengthCount * 4 + 0x100;
    test::WorldPair worlds(ctx, size);
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(worlds.originalBytes() + kPlayerListLengths - 0x40, 0x400);
        std::memcpy(worlds.oursBytes() + kPlayerListLengths - 0x40,
                    worlds.originalBytes() + kPlayerListLengths - 0x40, 0x400);
        bool same = S3RE_EXPECT_EQ(Original_GameWorld_ClearPlayerListLengths(worlds.original(),
                                                                             nullptr),
                                   candidate(worlds.ours(), nullptr));
        same &= worlds.Checkpoint(ctx);
        if (!same) {
            return;
        }
    }
}

// Wrong: clears one entry too few.
std::uint32_t __fastcall WrongClearPlayerListLengths(GameWorld* world, void* /*edx*/) {
    std::memset(&WorldField<std::int32_t>(world, kPlayerListLengths), 0,
                (kPlayerListLengthCount - 1) * sizeof(std::int32_t));
    return 0;
}

}  // namespace

S3RE_TEST(GameWorld_AddPlayerCounter_MatchesOriginal) {
    ComparePlayerCounter(ctx, Original_GameWorld_AddPlayerCounter, &GameWorld_AddPlayerCounter,
                         20000);
}

S3RE_TEST(GameWorld_AddPlayerCounter_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePlayerCounter(probe, Original_GameWorld_AddPlayerCounter, &WrongAddPlayerCounter,
                         2000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_SubtractPlayerCounter_MatchesOriginal) {
    ComparePlayerCounter(ctx, Original_GameWorld_SubtractPlayerCounter,
                         &GameWorld_SubtractPlayerCounter, 20000);
}

S3RE_TEST(GameWorld_SubtractPlayerCounter_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePlayerCounter(probe, Original_GameWorld_SubtractPlayerCounter,
                         &WrongSubtractPlayerCounter, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_ClearPlayerListLengths_MatchesOriginal) {
    CompareClearLists(ctx, &GameWorld_ClearPlayerListLengths, 20);
}

S3RE_TEST(GameWorld_ClearPlayerListLengths_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareClearLists(probe, &WrongClearPlayerListLengths, 3);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
