// Counting functions over the building and settler records.
//
// Buildings (provisional name): records of 0x3a bytes at world+0x1499e64,
// numbered from 1; the number of records is the uint32 at world+0x1499e60.
// Byte +0x0 is the player, byte +0x6 the type, word +0x8 (Building::unk08)
// and byte +0x21 (Building::buildup) are tested below.
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
//
// Settlers standing in an area (area records: 0x2e0 bytes at
// world+0xe145e8, byte +0x0 the player, 0xff = free). Both walk settlers
// 1..count (count re-read or not; nothing is written) and count those of
// the area's player that stand on their own tile (tile word +0x0 == the
// settler) whose area word (+0x14) equals the area (zero-extended, 32-bit
// compare). 0 when the area is free. Tiles at world+0x67444 + (x*0x300 +
// y)*24, settler position +0x4/+0x6.
// GameWorld::CountAreaSettlersOfClass0 (0x0050e750): (area): settlers whose
//   type's class (S3.EXE table 0x658f64) is 0 (the civilians, see
//   settlers.cpp). ret 4.
// GameWorld::CountAreaSettlersOfType (0x0050e820): (area, type): settlers of
//   one type (byte +0x1, zero-extended compare). ret 8.

#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game {

namespace {

using layout::Building;
using layout::Settler;
using layout::Tile;

constexpr std::uint8_t kFreeArea = 0xff;

// int32 per settler type, in S3.EXE's read-only data.
const std::int32_t* const kSettlerTypeClass = reinterpret_cast<const std::int32_t*>(0x00658f64);

// Counts buildings 1..count for which `match(record)` holds.
template <typename Match>
std::uint32_t CountBuildingsWhere(GameWorld* world, Match match) {
    const std::uint32_t count = world->building_high_water;
    std::uint32_t found = 0;
    for (std::uint32_t i = 1; i <= count && i != 0; ++i) {
        found += match(world->buildings[i]) ? 1 : 0;
    }
    return found;
}

template <typename Match>
std::uint32_t CountSettlersWhere(GameWorld* world, Match match) {
    const std::uint32_t count = world->settler_high_water;
    std::uint32_t found = 0;
    for (std::uint32_t i = 1; i <= count && i != 0; ++i) {
        found += match(world->settlers[i]) ? 1 : 0;
    }
    return found;
}

// The tile a settler stands on, with the original's 32-bit arithmetic.
const Tile& SettlerTile(GameWorld* world, const Settler& s) {
    return TileAtOffset(world, TileOffset(s.x, s.y));
}

}  // namespace

using CountFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t player,
                                           std::uint32_t value);

// Purpose: Counts a player's buildings of one type. Used by mission goals and the AI.
std::uint32_t __fastcall GameWorld_CountBuildings(GameWorld* world, void* /*edx*/,
                                                  std::uint32_t player, std::uint32_t type) {
    return CountBuildingsWhere(world, [&](const Building& b) { return b.owner == player && b.art == type; });
}

// Purpose: Counts a player's finished (not under construction) buildings of one type.
std::uint32_t __fastcall GameWorld_CountBuildingsWithFlag21Clear(GameWorld* world, void* /*edx*/,
                                                                 std::uint32_t player,
                                                                 std::uint32_t type) {
    return CountBuildingsWhere(world, [&](const Building& b) {
        return b.owner == player && b.art == type && b.buildup == 0;
    });
}

// Purpose: Counts a player's finished buildings of one type that have a certain field set (meaning unknown;
// probably 'occupied/working').
std::uint32_t __fastcall GameWorld_CountBuildingsWithWord8(GameWorld* world, void* /*edx*/,
                                                           std::uint32_t player,
                                                           std::uint32_t type) {
    return CountBuildingsWhere(world, [&](const Building& b) {
        return b.owner == player && b.art == type && b.buildup == 0 && b.unk08 != 0;
    });
}

// Purpose: Counts a player's settlers of one type. Used by mission goals and the AI.
std::uint32_t __fastcall GameWorld_CountSettlers(GameWorld* world, void* /*edx*/,
                                                 std::uint32_t player, std::uint32_t type) {
    return CountSettlersWhere(world, [&](const Settler& s) { return s.owner == player && s.art == type; });
}

// Purpose: Counts a player's settlers that currently do one job.
std::uint32_t __fastcall GameWorld_CountSettlersWithJob(GameWorld* world, void* /*edx*/,
                                                        std::uint32_t player, std::uint32_t job) {
    return CountSettlersWhere(world, [&](const Settler& s) { return s.owner == player && s.job == job; });
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

namespace {

// Counts the settlers of the area's player that stand on their own tile in
// the area and for which `match(record)` holds.
template <typename Match>
std::uint32_t CountAreaSettlersWhere(GameWorld* world, std::uint32_t area, Match match) {
    const std::uint32_t player = world->areas[area].player;
    if (player == kFreeArea) {
        return 0;
    }
    return CountSettlersWhere(world, [&](const Settler& s) {
        if (s.owner != player || !match(s)) {
            return false;
        }
        const Tile& tile = SettlerTile(world, s);
        const auto self = static_cast<std::uint32_t>(&s - world->settlers);
        return tile.settler == self && tile.area == area;
    });
}

}  // namespace

using CountAreaFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t area);

// Purpose: How many civilian settlers (not soldiers or special units) of an area's player stand inside
// the area; probably used by the economy to see how many workers/carriers an area has.
std::uint32_t __fastcall GameWorld_CountAreaSettlersOfClass0(GameWorld* world, void* /*edx*/, std::uint32_t area) {
    return CountAreaSettlersWhere(world, area,
                                  [](const Settler& s) { return kSettlerTypeClass[s.art] == 0; });
}

// Purpose: How many settlers of one type (e.g. carriers) of an area's player stand inside the area.
std::uint32_t __fastcall GameWorld_CountAreaSettlersOfType(GameWorld* world, void* /*edx*/, std::uint32_t area,
                                                           std::uint32_t type) {
    return CountAreaSettlersWhere(world, area, [&](const Settler& s) { return s.art == type; });
}

S3RE_HOOK(0x0050e750, GameWorld_CountAreaSettlersOfClass0, CountAreaFn,
          Original_GameWorld_CountAreaSettlersOfClass0, GameWorld_CountAreaSettlersOfClass0);
S3RE_HOOK(0x0050e820, GameWorld_CountAreaSettlersOfType, CountFn, Original_GameWorld_CountAreaSettlersOfType,
          GameWorld_CountAreaSettlersOfType);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kMaxRecords = 3000;
constexpr std::size_t kTestWorld = offsetof(GameWorld, buildings) + (kMaxRecords + 2) * sizeof(Building);
static_assert(offsetof(GameWorld, settlers) + (kMaxRecords + 2) * sizeof(Settler) < kTestWorld);

// Fills the records with few distinct values so that matches are common.
void FillRecords(test::Context& ctx, test::WorldPair& worlds) {
    for (std::uint32_t i = 0; i <= kMaxRecords + 1; ++i) {
        const auto bPlayer = static_cast<std::uint8_t>(ctx.rng().Below(4));
        const auto bType = static_cast<std::uint8_t>(ctx.rng().Below(4));
        const auto buildup = ctx.rng().Chance(1, 2) ? 0 : static_cast<std::uint8_t>(1 + ctx.rng().Below(255));
        const auto word8 = ctx.rng().Chance(1, 2) ? 0 : static_cast<std::uint16_t>(ctx.rng().Next32());
        const auto sPlayer = static_cast<std::uint8_t>(ctx.rng().Below(4));
        const auto sType = static_cast<std::uint8_t>(ctx.rng().Below(4));
        const auto job = static_cast<std::uint16_t>(ctx.rng().Below(4));
        worlds.Both([&](GameWorld* w) {
            Building& b = w->buildings[i];
            b.owner = bPlayer;
            b.art = bType;
            b.buildup = static_cast<std::uint8_t>(buildup);
            b.unk08 = static_cast<std::uint16_t>(word8);
            Settler& s = w->settlers[i];
            s.owner = sPlayer;
            s.art = sType;
            s.job = job;
        });
    }
}

void CompareCount(test::Context& ctx, CountFn original, CountFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    FillRecords(ctx, worlds);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t count = i < 4 ? static_cast<std::uint32_t>(i) : ctx.rng().Below(kMaxRecords + 1);
        worlds.Both([&](GameWorld* w) {
            w->building_high_water = count;
            w->settler_high_water = count;
        });
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
    const std::uint32_t count = world->settler_high_water;
    std::uint32_t found = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const Settler& s = world->settlers[i];
        found += (s.owner == player && s.art == type) ? 1 : 0;
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

namespace {

constexpr std::uint32_t kTestAreas = 4;
constexpr std::uint32_t kTestSpan = 12;  // settlers stand on tiles 0..11 x 0..11

// Settlers 0..kMaxRecords+1 of players 0..3 with real types (0..0x2f,
// sometimes others), standing on tiles near (0, 0), many of them on "their
// own" tile; tile areas and area players 0..3 (sometimes free).
void FillAreaSettlers(test::Context& ctx, test::WorldPair& worlds) {
    for (std::uint32_t x = 0; x < kTestSpan; ++x) {
        for (std::uint32_t y = 0; y < kTestSpan; ++y) {
            const auto area =
                static_cast<std::uint16_t>(ctx.rng().Chance(1, 16) ? ctx.rng().Next32() : ctx.rng().Below(kTestAreas));
            worlds.Both([&](GameWorld* w) { w->tiles[x][y].area = area; });
        }
    }
    for (std::uint32_t i = 0; i <= kMaxRecords + 1; ++i) {
        const auto player = static_cast<std::uint8_t>(ctx.rng().Below(4));
        const auto type =
            static_cast<std::uint8_t>(ctx.rng().Chance(1, 10) ? ctx.rng().Below(256) : ctx.rng().Below(0x30));
        const std::uint32_t x = ctx.rng().Below(kTestSpan);
        const std::uint32_t y = ctx.rng().Below(kTestSpan);
        const bool own = ctx.rng().Chance(2, 3);
        worlds.Both([&](GameWorld* w) {
            Settler& s = w->settlers[i];
            s.owner = player;
            s.art = type;
            s.x = static_cast<std::uint16_t>(x);
            s.y = static_cast<std::uint16_t>(y);
            if (own) {
                w->tiles[x][y].settler = static_cast<std::uint16_t>(i);
            }
        });
    }
}

template <typename Call>
void CompareAreaCount(test::Context& ctx, int cases, Call call) {
    test::WorldPair worlds(ctx, kTestWorld);
    FillAreaSettlers(ctx, worlds);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t count = i < 4 ? static_cast<std::uint32_t>(i) : ctx.rng().Below(kMaxRecords + 1);
        worlds.Both([&](GameWorld* w) { w->settler_high_water = count; });
        const std::uint32_t area = ctx.rng().Below(kTestAreas + 1);
        const auto areaPlayer = static_cast<std::uint8_t>(ctx.rng().Chance(1, 8) ? kFreeArea : ctx.rng().Below(4));
        worlds.Both([&](GameWorld* w) { w->areas[area].player = areaPlayer; });
        std::uint32_t type = ctx.rng().Below(0x30);
        if (ctx.rng().Chance(1, 8)) {
            type |= 0x100;  // matches only in the low byte: must not count
        }
        std::uint32_t expected = 0;
        std::uint32_t actual = 0;
        call(worlds, area, type, expected, actual);
        if (!S3RE_EXPECT_EQ(expected, actual) || (i % 512 == 511 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: count %u, area %u, type 0x%x", i, count, area, type);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

// Wrong: does not check that the settler stands on its own tile.
std::uint32_t __fastcall WrongCountAreaSettlersOfType(GameWorld* world, void*, std::uint32_t area, std::uint32_t type) {
    const std::uint32_t player = world->areas[area].player;
    if (player == kFreeArea) {
        return 0;
    }
    return CountSettlersWhere(world, [&](const Settler& s) {
        return s.owner == player && s.art == type && SettlerTile(world, s).area == area;
    });
}

}  // namespace

S3RE_TEST(GameWorld_CountAreaSettlersOfClass0_MatchesOriginal) {
    CompareAreaCount(ctx, 3000, [](test::WorldPair& w, std::uint32_t area, std::uint32_t, std::uint32_t& e, std::uint32_t& a) {
        e = Original_GameWorld_CountAreaSettlersOfClass0(w.original(), nullptr, area);
        a = GameWorld_CountAreaSettlersOfClass0(w.ours(), nullptr, area);
    });
}
S3RE_TEST(GameWorld_CountAreaSettlersOfType_MatchesOriginal) {
    CompareAreaCount(ctx, 3000, [](test::WorldPair& w, std::uint32_t area, std::uint32_t type, std::uint32_t& e,
                                   std::uint32_t& a) {
        e = Original_GameWorld_CountAreaSettlersOfType(w.original(), nullptr, area, type);
        a = GameWorld_CountAreaSettlersOfType(w.ours(), nullptr, area, type);
    });
}
S3RE_TEST(GameWorld_CountAreaSettlersOfType_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareAreaCount(probe, 300, [](test::WorldPair& w, std::uint32_t area, std::uint32_t type, std::uint32_t& e,
                                    std::uint32_t& a) {
        e = Original_GameWorld_CountAreaSettlersOfType(w.original(), nullptr, area, type);
        a = WrongCountAreaSettlersOfType(w.ours(), nullptr, area, type);
    });
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
