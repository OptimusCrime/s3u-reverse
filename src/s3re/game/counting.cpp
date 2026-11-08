// Counting functions over the building and settler records.
//
// Buildings (provisional name): records of 0x3a bytes at world+0x1499e64,
// numbered from 1; the number of records is the uint32 at world+0x1499e60.
// Byte +0x0 is the player, byte +0x6 the type, word +0x8 and byte +0x21
// are tested below (meaning unknown).
// Settlers: records of 0x40 bytes at world+0x12a5e60, numbered from 1; the
// number of records is the uint32 at world+0x12a5e5c. Byte +0x0 is the
// player, byte +0x1 the type, word +0xe probably the job.
//
// Each function walks records 1..count and returns how many match; player
// and type arguments are compared as full 32-bit values with the
// zero-extended fields. All: thiscall (ECX = world), two stack arguments,
// ret 8, EAX = the count.
//
// GameWorld::CountBuildings (0x00515850): (player, type).
// GameWorld::CountBuildingsWithFlag21Clear (0x005158a0): (player, type) and
//   byte +0x21 == 0.
// GameWorld::CountBuildingsWithWord8 (0x005159d0): (player, type), byte
//   +0x21 == 0 and word +0x8 != 0.
// GameWorld::CountSettlers (0x00515a30): (player, type).
// GameWorld::CountSettlersWithJob (0x00515a80): (player, job word +0xe).

#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

constexpr std::size_t kBuildingCount = 0x1499e60;
constexpr std::size_t kBuildings = 0x1499e64;
constexpr std::size_t kBuildingBytes = 0x3a;
constexpr std::size_t kBuildingPlayer = 0x0;
constexpr std::size_t kBuildingType = 0x6;
constexpr std::size_t kBuildingWord8 = 0x8;
constexpr std::size_t kBuildingFlag21 = 0x21;

constexpr std::size_t kSettlerCount = 0x12a5e5c;
constexpr std::size_t kSettlers = 0x12a5e60;
constexpr std::size_t kSettlerBytes = 0x40;
constexpr std::size_t kSettlerPlayer = 0x0;
constexpr std::size_t kSettlerType = 0x1;
constexpr std::size_t kSettlerJob = 0xe;

const std::uint8_t* Building(GameWorld* world, std::uint32_t index) {
    return reinterpret_cast<const std::uint8_t*>(world) + kBuildings + index * kBuildingBytes;
}

const std::uint8_t* Settler(GameWorld* world, std::uint32_t index) {
    return reinterpret_cast<const std::uint8_t*>(world) + kSettlers + index * kSettlerBytes;
}

std::uint16_t Word(const std::uint8_t* record, std::size_t field) {
    std::uint16_t value;
    std::memcpy(&value, record + field, sizeof(value));
    return value;
}

// Counts buildings 1..count for which `match(record)` holds.
template <typename Match>
std::uint32_t CountBuildingsWhere(GameWorld* world, Match match) {
    const auto count = WorldField<std::uint32_t>(world, kBuildingCount);
    std::uint32_t found = 0;
    for (std::uint32_t i = 1; i <= count && i != 0; ++i) {
        found += match(Building(world, i)) ? 1 : 0;
    }
    return found;
}

template <typename Match>
std::uint32_t CountSettlersWhere(GameWorld* world, Match match) {
    const auto count = WorldField<std::uint32_t>(world, kSettlerCount);
    std::uint32_t found = 0;
    for (std::uint32_t i = 1; i <= count && i != 0; ++i) {
        found += match(Settler(world, i)) ? 1 : 0;
    }
    return found;
}

}  // namespace

using CountFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t player,
                                           std::uint32_t value);

// Purpose: Counts a player's buildings of one type. Used by mission goals and the AI.
std::uint32_t __fastcall GameWorld_CountBuildings(GameWorld* world, void* /*edx*/,
                                                  std::uint32_t player, std::uint32_t type) {
    return CountBuildingsWhere(world, [&](const std::uint8_t* b) {
        return b[kBuildingPlayer] == player && b[kBuildingType] == type;
    });
}

// Purpose: Counts a player's finished (not under construction) buildings of one type.
std::uint32_t __fastcall GameWorld_CountBuildingsWithFlag21Clear(GameWorld* world, void* /*edx*/,
                                                                 std::uint32_t player,
                                                                 std::uint32_t type) {
    return CountBuildingsWhere(world, [&](const std::uint8_t* b) {
        return b[kBuildingPlayer] == player && b[kBuildingType] == type &&
               b[kBuildingFlag21] == 0;
    });
}

// Purpose: Counts a player's finished buildings of one type that have a certain field set (meaning unknown;
// probably 'occupied/working').
std::uint32_t __fastcall GameWorld_CountBuildingsWithWord8(GameWorld* world, void* /*edx*/,
                                                           std::uint32_t player,
                                                           std::uint32_t type) {
    return CountBuildingsWhere(world, [&](const std::uint8_t* b) {
        return b[kBuildingPlayer] == player && b[kBuildingType] == type &&
               b[kBuildingFlag21] == 0 && Word(b, kBuildingWord8) != 0;
    });
}

// Purpose: Counts a player's settlers of one type. Used by mission goals and the AI.
std::uint32_t __fastcall GameWorld_CountSettlers(GameWorld* world, void* /*edx*/,
                                                 std::uint32_t player, std::uint32_t type) {
    return CountSettlersWhere(world, [&](const std::uint8_t* s) {
        return s[kSettlerPlayer] == player && s[kSettlerType] == type;
    });
}

// Purpose: Counts a player's settlers that currently do one job.
std::uint32_t __fastcall GameWorld_CountSettlersWithJob(GameWorld* world, void* /*edx*/,
                                                        std::uint32_t player, std::uint32_t job) {
    return CountSettlersWhere(world, [&](const std::uint8_t* s) {
        return s[kSettlerPlayer] == player && Word(s, kSettlerJob) == job;
    });
}

S3RE_HOOK(0x00515850, GameWorld_CountBuildings, CountFn, Original_GameWorld_CountBuildings,
          GameWorld_CountBuildings);
S3RE_HOOK(0x005158a0, GameWorld_CountBuildingsWithFlag21Clear, CountFn,
          Original_GameWorld_CountBuildingsWithFlag21Clear,
          GameWorld_CountBuildingsWithFlag21Clear);
S3RE_HOOK(0x005159d0, GameWorld_CountBuildingsWithWord8, CountFn,
          Original_GameWorld_CountBuildingsWithWord8, GameWorld_CountBuildingsWithWord8);
S3RE_HOOK(0x00515a30, GameWorld_CountSettlers, CountFn, Original_GameWorld_CountSettlers,
          GameWorld_CountSettlers);
S3RE_HOOK(0x00515a80, GameWorld_CountSettlersWithJob, CountFn,
          Original_GameWorld_CountSettlersWithJob, GameWorld_CountSettlersWithJob);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kMaxRecords = 3000;
constexpr std::size_t kTestWorld = kBuildings + (kMaxRecords + 2) * kBuildingBytes;
static_assert(kSettlers + (kMaxRecords + 2) * kSettlerBytes < kTestWorld);

// Fills the records with few distinct values so that matches are common.
void FillRecords(test::Context& ctx, test::WorldPair& worlds) {
    for (std::uint32_t i = 0; i <= kMaxRecords + 1; ++i) {
        const std::size_t b = kBuildings + i * kBuildingBytes;
        worlds.Set<std::uint8_t>(b + kBuildingPlayer, static_cast<std::uint8_t>(ctx.rng().Below(4)));
        worlds.Set<std::uint8_t>(b + kBuildingType, static_cast<std::uint8_t>(ctx.rng().Below(4)));
        worlds.Set<std::uint8_t>(b + kBuildingFlag21,
                                 ctx.rng().Chance(1, 2) ? 0 : static_cast<std::uint8_t>(1 + ctx.rng().Below(255)));
        worlds.Set<std::uint16_t>(b + kBuildingWord8,
                                  ctx.rng().Chance(1, 2) ? 0 : static_cast<std::uint16_t>(ctx.rng().Next32()));
        const std::size_t s = kSettlers + i * kSettlerBytes;
        worlds.Set<std::uint8_t>(s + kSettlerPlayer, static_cast<std::uint8_t>(ctx.rng().Below(4)));
        worlds.Set<std::uint8_t>(s + kSettlerType, static_cast<std::uint8_t>(ctx.rng().Below(4)));
        worlds.Set<std::uint16_t>(s + kSettlerJob, static_cast<std::uint16_t>(ctx.rng().Below(4)));
    }
}

void CompareCount(test::Context& ctx, CountFn original, CountFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    FillRecords(ctx, worlds);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t count = i < 4 ? static_cast<std::uint32_t>(i) : ctx.rng().Below(kMaxRecords + 1);
        worlds.Set<std::uint32_t>(kBuildingCount, count);
        worlds.Set<std::uint32_t>(kSettlerCount, count);
        // Mostly matching values; sometimes ones that only match in the
        // low byte (must not count).
        std::uint32_t player = ctx.rng().Below(5);
        std::uint32_t value = ctx.rng().Below(5);
        if (ctx.rng().Chance(1, 8)) {
            player |= 0x100;
        }
        if (ctx.rng().Chance(1, 8)) {
            value |= 0x10000;
        }
        const std::uint32_t expected = original(worlds.original(), nullptr, player, value);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, player, value);
        if (!S3RE_EXPECT_EQ(expected, actual) || (i % 512 == 511 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: count %u, player 0x%x, value 0x%x", i, count,
                     player, value);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

// Wrong: counts records 0..count-1 instead of 1..count.
std::uint32_t __fastcall WrongCountSettlers(GameWorld* world, void* /*edx*/, std::uint32_t player,
                                            std::uint32_t type) {
    const auto count = WorldField<std::uint32_t>(world, kSettlerCount);
    std::uint32_t found = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* s = Settler(world, i);
        found += (s[kSettlerPlayer] == player && s[kSettlerType] == type) ? 1 : 0;
    }
    return found;
}

// Wrong: ignores the word +0x8 condition.
std::uint32_t __fastcall WrongCountBuildingsWithWord8(GameWorld* world, void* edx,
                                                      std::uint32_t player, std::uint32_t type) {
    return GameWorld_CountBuildingsWithFlag21Clear(world, edx, player, type);
}

}  // namespace

S3RE_TEST(GameWorld_CountBuildings_MatchesOriginal) {
    CompareCount(ctx, Original_GameWorld_CountBuildings, &GameWorld_CountBuildings, 3000);
}

S3RE_TEST(GameWorld_CountBuildingsWithFlag21Clear_MatchesOriginal) {
    CompareCount(ctx, Original_GameWorld_CountBuildingsWithFlag21Clear,
                 &GameWorld_CountBuildingsWithFlag21Clear, 3000);
}

S3RE_TEST(GameWorld_CountBuildingsWithWord8_MatchesOriginal) {
    CompareCount(ctx, Original_GameWorld_CountBuildingsWithWord8, &GameWorld_CountBuildingsWithWord8,
                 3000);
}

S3RE_TEST(GameWorld_CountBuildingsWithWord8_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareCount(probe, Original_GameWorld_CountBuildingsWithWord8, &WrongCountBuildingsWithWord8,
                 300);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_CountSettlers_MatchesOriginal) {
    CompareCount(ctx, Original_GameWorld_CountSettlers, &GameWorld_CountSettlers, 3000);
}

S3RE_TEST(GameWorld_CountSettlers_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareCount(probe, Original_GameWorld_CountSettlers, &WrongCountSettlers, 300);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_CountSettlersWithJob_MatchesOriginal) {
    CompareCount(ctx, Original_GameWorld_CountSettlersWithJob, &GameWorld_CountSettlersWithJob,
                 3000);
}

}  // namespace s3re::game
