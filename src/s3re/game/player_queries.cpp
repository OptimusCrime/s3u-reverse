// Queries about players.
//
// GameWorld::FindPlayerByField0C (0x00507f10): (value) -> the first player
//   0..19 whose record (0x148 bytes at world+0x11943dc) has int32 +0x0 != 0
//   (taking part), int32 +0x18 == 0 and int32 +0xc == value; -1 if none.
//   thiscall, ret 4. Meaning of +0xc/+0x18 unknown; callers in 0x5074b0.
// BuildingTypeAllowedForRace (0x004ffd60): (race, type) -> AL. Callers pass
//   byte +0x4 of the local player's record (world+0x2d2d4 is the local
//   player; probably the race) and a type from static UI tables (0x658d1c,
//   0x65a117; probably building types of the build menu). Rules: type 0x15
//   only for race 0; 0x19 for races 2 and 3; 0x1b, 0x1e and 0x21 for race 2;
//   0x1c for races 1 and 3; 0x1d for race 1; 0x22 and 0x23 for race 3; all
//   other types for everybody. Race is the low byte of the first argument,
//   the type a full int32. ECX is not used (callers load the world into it).
//   The original only sets AL (the rest of EAX is the caller's); every
//   caller only tests AL. ret 8.

#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>

namespace s3re::game {

namespace {

constexpr std::uint32_t kPlayers = 20;
constexpr std::uint32_t kPlayerActive = 0x0;
constexpr std::uint32_t kPlayerField0C = 0xc;
constexpr std::uint32_t kPlayerField18 = 0x18;

std::uint32_t& PlayerField(GameWorld* world, std::uint32_t player, std::uint32_t field) {
    return WorldField<std::uint32_t>(
        world, world_offset::kPlayerRecords + player * world_offset::kPlayerRecordBytes + field);
}

}  // namespace

using FindPlayerFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t value);
using RaceTypeFn = std::uint32_t(__stdcall*)(std::uint32_t race, std::uint32_t type);

// Purpose: Finds the active player whose index/team field matches a value. Probably used to map between a
// player's slot and their in-game number.
std::uint32_t __fastcall GameWorld_FindPlayerByField0C(GameWorld* world, void*,
                                                       std::uint32_t value) {
    for (std::uint32_t player = 0; player < kPlayers; ++player) {
        if (PlayerField(world, player, kPlayerActive) != 0 &&
            PlayerField(world, player, kPlayerField18) == 0 &&
            PlayerField(world, player, kPlayerField0C) == value) {
            return player;
        }
    }
    return 0xffffffffu;
}

// Purpose: Some buildings exist only for certain races (Romans, Egyptians, Asians, Amazons). This tells the
// build menu whether a building type is available to a race.
std::uint32_t __stdcall BuildingTypeAllowedForRace(std::uint32_t race, std::uint32_t type) {
    const auto r = static_cast<std::uint8_t>(race);
    switch (type) {
    case 0x15: return r == 0;
    case 0x19: return r == 2 || r == 3;
    case 0x1b: return r == 2;
    case 0x1c: return r == 1 || r == 3;
    case 0x1d: return r == 1;
    case 0x1e: return r == 2;
    case 0x21: return r == 2;
    case 0x22:
    case 0x23: return r == 3;
    default: return 1;
    }
}

S3RE_HOOK(0x00507f10, GameWorld_FindPlayerByField0C, FindPlayerFn,
          Original_GameWorld_FindPlayerByField0C, GameWorld_FindPlayerByField0C);
S3RE_HOOK(0x004ffd60, BuildingTypeAllowedForRace, RaceTypeFn, Original_BuildingTypeAllowedForRace,
          BuildingTypeAllowedForRace);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kTestWorld =
    world_offset::kPlayerRecords + kPlayers * world_offset::kPlayerRecordBytes;

void CompareFind(test::Context& ctx, FindPlayerFn original, FindPlayerFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        // Small field values so that matches, near-matches and duplicates occur.
        for (std::uint32_t player = 0; player < kPlayers; ++player) {
            const std::uint32_t record =
                world_offset::kPlayerRecords + player * world_offset::kPlayerRecordBytes;
            worlds.Set<std::uint32_t>(record + kPlayerActive,
                                      ctx.rng().Chance(1, 4) ? 0 : ctx.rng().Next32() | 1);
            worlds.Set<std::uint32_t>(record + kPlayerField18,
                                      ctx.rng().Chance(1, 3) ? ctx.rng().Next32() | 1 : 0);
            worlds.Set<std::uint32_t>(record + kPlayerField0C, ctx.rng().Below(6));
        }
        const std::uint32_t value = ctx.rng().Chance(1, 16) ? ctx.rng().Next32() : ctx.rng().Below(6);
        const std::uint32_t expected = original(worlds.original(), nullptr, value);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, value);
        if (!S3RE_EXPECT_EQ(expected, actual)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: value %u", i, value);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

// Wrong: ignores field +0x18.
std::uint32_t __fastcall WrongFindPlayerByField0C(GameWorld* world, void*, std::uint32_t value) {
    for (std::uint32_t player = 0; player < kPlayers; ++player) {
        if (PlayerField(world, player, kPlayerActive) != 0 &&
            PlayerField(world, player, kPlayerField0C) == value) {
            return player;
        }
    }
    return 0xffffffffu;
}

}  // namespace

S3RE_TEST(GameWorld_FindPlayerByField0C_MatchesOriginal) {
    CompareFind(ctx, Original_GameWorld_FindPlayerByField0C, &GameWorld_FindPlayerByField0C, 20000);
}
S3RE_TEST(GameWorld_FindPlayerByField0C_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareFind(probe, Original_GameWorld_FindPlayerByField0C, &WrongFindPlayerByField0C, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

// Exhaustive over all race bytes and the interesting types, random elsewhere.
S3RE_TEST(BuildingTypeAllowedForRace_MatchesOriginal) {
    for (std::uint32_t race = 0; race < 0x100; ++race) {
        for (std::uint32_t type = 0; type < 0x40; ++type) {
            const std::uint32_t r = race | (ctx.rng().Next32() & 0xffffff00u);
            const std::uint32_t expected = Original_BuildingTypeAllowedForRace(r, type) & 0xff;
            const std::uint32_t actual = BuildingTypeAllowedForRace(r, type) & 0xff;
            if (!S3RE_EXPECT_EQ(expected, actual)) {
                ctx.Fail(__FILE__, __LINE__, "  race %#x, type %#x", r, type);
                return;
            }
        }
    }
    for (int i = 0; i < 20000; ++i) {
        const std::uint32_t race = ctx.rng().Next32();
        const std::uint32_t type = ctx.rng().Chance(1, 2) ? ctx.rng().Next32() : 0x15 + ctx.rng().Below(16);
        if (!S3RE_EXPECT_EQ(Original_BuildingTypeAllowedForRace(race, type) & 0xff,
                            BuildingTypeAllowedForRace(race, type) & 0xff)) {
            ctx.Fail(__FILE__, __LINE__, "  race %#x, type %#x", race, type);
            return;
        }
    }
}

}  // namespace s3re::game
