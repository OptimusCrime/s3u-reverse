// Queries about players.
//
// CGameData::FindPlayerByField0C (0x00507f10): (value) -> the first player
//   0..19 whose record (0x148 bytes at world+0x11943dc) has int32 +0x0 != 0
//   (taking part), int32 +0x18 == 0 and int32 +0xc == value; -1 if none.
//   thiscall, ret 4. Meaning of +0xc/+0x18 unknown; callers in 0x5074b0.
// BuildingTypeAllowedForRace (0x004ffd60): (race, type) -> AL. Callers pass
//   byte +0x4 of the local player's record (world+0x2d2d4 is the local
//   player; probably the race) and a type from static UI tables (0x658d1c,
//   0x65a117; probably building types of the build menu, but not in the
//   numbering of enums.h' BuildingType). Rules: type 21 (0x15) only for
//   Romans; 25 (0x19) for Asians and Amazons; 27, 30 and 33 (0x1b, 0x1e,
//   0x21) for Asians; 28 (0x1c) for Egyptians and Amazons; 29 (0x1d) for
//   Egyptians; 34 and 35 (0x22, 0x23) for Amazons; all other types for
//   everybody. Race is the low byte of the first argument,
//   the type a full int32. ECX is not used (callers load the world into it).
//   The original only sets AL (the rest of EAX is the caller's); every
//   caller only tests AL. ret 8.
// CGameData::LogPlayerLandResources (0x00507f60): (player): only a
//   DebugLog("Player %u   Land %u  Trees %u  Stone %u ..."). It looks at the
//   19,995 tiles given by the offset table (int32 dx, dy in 16-byte entries
//   at world+0x110a588) around the player's position (int32 +0x10/+0x14 of
//   the player record), skipping tiles off the map (unsigned compare with
//   the map size), and sums with a weight of 4 for entries below 3,500, 3
//   below 8,000, 2 below 13,500, else 1: land (high nibble of terrain byte
//   +0x6 is 1), swamp (terrain 21, 0x15), stream (high nibble 6), trees (object
//   byte +0x7 in 0x44..0x53), stone (object 0x73..0x7e, weight * (0x7f -
//   object)), and the resource byte +0x11 (high nibble: 0 fish, 1 coal, 2
//   iron, 3 gold, 4 gems, 5 sulfur; low nibble = amount, times the weight).
//   Logs each sum halved. EAX (the only caller ignores it) = land / 2.
//   thiscall, ret 4.

#include "game/debug_log.h"
#include "game/enums.h"
#include "game/texts.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <vector>

namespace s3re::game {

namespace {

using layout::Player;

constexpr std::uint32_t kPlayers = layout::kPlayers;
constexpr std::uint32_t kNoPlayer = 0xffffffffu;  // FindPlayerByField0C: -1

}  // namespace

using FindPlayerFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t value);
using RaceTypeFn = std::uint32_t(__stdcall*)(std::uint32_t race, std::uint32_t type);

// Purpose: Finds the active player whose index/team field matches a value. Probably used to map between a
// player's slot and their in-game number.
std::uint32_t __fastcall CGameData_FindPlayerByField0C(CGameData* world, void*,
                                                       std::uint32_t value) {
    // Field +0xc is Player::team, +0x18 Player::unk018.
    for (std::uint32_t player = 0; player < kPlayers; ++player) {
        const Player& record = world->players[player];
        if (record.active != 0 && record.unk018 == 0 &&
            static_cast<std::uint32_t>(record.team) == value) {
            return player;
        }
    }
    return kNoPlayer;
}

// Purpose: Some buildings exist only for certain races (Romans, Egyptians, Asians, Amazons). This tells the
// build menu whether a building type is available to a race.
// The type numbers are not those of BuildingType (TODO.md), so they stay
// numbers here; only the low byte of `race` counts.
std::uint32_t __stdcall BuildingTypeAllowedForRace(std::uint32_t race, std::uint32_t type) {
    const auto r = static_cast<Race>(static_cast<std::uint8_t>(race));
    switch (type) {
    case 21: return r == Race::Roman;
    case 25: return r == Race::Asian || r == Race::Amazon;
    case 27: return r == Race::Asian;
    case 28: return r == Race::Egyptian || r == Race::Amazon;
    case 29: return r == Race::Egyptian;
    case 30: return r == Race::Asian;
    case 33: return r == Race::Asian;
    case 34:
    case 35: return r == Race::Amazon;
    default: return 1;
    }
}

namespace {

constexpr std::uint32_t kSurveyEntries = 19995;  // 0x4e1b entries of world->offsets

// The weight of a tile falls with its index in the offset table (which
// spirals outwards from the start position): 4 for the first 3,500, then
// 3, 2 and 1.
std::uint32_t SurveyWeight(std::uint32_t index) {
    return index < 3500 ? 4 : index < 8000 ? 3 : index < 13500 ? 2 : 1;
}

// Tile::terrain: the class is the high nibble.
constexpr std::uint8_t kTerrainClassGrass = 0x10;   // counted as land
constexpr std::uint8_t kTerrainSwamp = 21;          // 0x15
constexpr std::uint8_t kTerrainClassStream = 0x60;  // the waterworker draws water here

// Tile::object: trees 68..83 (0x44..0x53); stone 115..126 (0x73..0x7e)
// with 127 - object units left.
constexpr std::uint32_t kObjectFirstTree = 0x44;
constexpr std::uint32_t kObjectLastTree = 0x53;
constexpr std::uint32_t kObjectFirstStone = 0x73;
constexpr std::uint32_t kObjectLastStone = 0x7e;
constexpr std::uint32_t kStoneEnd = 0x7f;

// Tile::resources: the high nibble is the kind, the low nibble the amount.
constexpr std::uint32_t kResourceKindMask = 0xf0;
constexpr std::uint32_t kResourceAmountMask = 0x0f;
constexpr std::uint32_t kResourceFish = 0x00;
constexpr std::uint32_t kResourceCoal = 0x10;
constexpr std::uint32_t kResourceIron = 0x20;
constexpr std::uint32_t kResourceGold = 0x30;
constexpr std::uint32_t kResourceGems = 0x40;
constexpr std::uint32_t kResourceSulfur = 0x50;

}  // namespace

using LogLandFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t player);

// Purpose: Debug output only: how much land and how many resources (trees, stone, coal, ...) lie around a
// player's start position. Probably a leftover from tuning the maps or the computer players.
std::uint32_t __fastcall CGameData_LogPlayerLandResources(CGameData* world, void*, std::uint32_t player) {
    const Player& record = world->players[player];  // player is not checked
    const auto px = static_cast<std::uint32_t>(record.start_x);
    const auto py = static_cast<std::uint32_t>(record.start_y);
    std::uint32_t land = 0, trees = 0, stone = 0, coal = 0, iron = 0, gold = 0, sulfur = 0, gems = 0, fish = 0,
                  swamp = 0, stream = 0;
    for (std::uint32_t i = 0; i < kSurveyEntries; ++i) {
        const std::uint32_t weight = SurveyWeight(i);
        const std::uint32_t x = static_cast<std::uint32_t>(world->offsets[i].dx) + px;
        const std::uint32_t y = static_cast<std::uint32_t>(world->offsets[i].dy) + py;
        const std::uint32_t size = world->map_size;  // re-read every entry, like the original
        if (x >= size || y >= size) {
            continue;
        }
        const layout::Tile& tile = world->tiles[x][y];
        const std::uint32_t terrain = tile.terrain;
        if ((terrain & kTerrainClassMask) == kTerrainClassGrass) {
            land += weight;
        }
        if (terrain == kTerrainSwamp) {
            swamp += weight;
        }
        if ((terrain & kTerrainClassMask) == kTerrainClassStream) {
            stream += weight;
        }
        const std::uint32_t object = tile.object;
        if (object >= kObjectFirstTree && object <= kObjectLastTree) {
            trees += weight;
        }
        if (object >= kObjectFirstStone && object <= kObjectLastStone) {
            stone += (kStoneEnd - object) * weight;
        }
        const std::uint32_t resource = tile.resources;
        const std::uint32_t amount = (resource & kResourceAmountMask) * weight;
        switch (resource & kResourceKindMask) {
        case kResourceFish: fish += amount; break;
        case kResourceCoal: coal += amount; break;
        case kResourceIron: iron += amount; break;
        case kResourceGold: gold += amount; break;
        case kResourceGems: gems += amount; break;
        case kResourceSulfur: sulfur += amount; break;
        default: break;
        }
    }
    // Each sum is logged halved.
    GameDebugLog(texts::kPlayerLand, player, land >> 1, trees >> 1, stone >> 1, coal >> 1, iron >> 1, gold >> 1,
                 sulfur >> 1, gems >> 1, fish >> 1, swamp >> 1, stream >> 1);
    return land >> 1;
}

S3RE_HOOK(0x00507f60, CGameData_LogPlayerLandResources, LogLandFn, Original_CGameData_LogPlayerLandResources,
          CGameData_LogPlayerLandResources);
S3RE_HOOK(0x00507f10, CGameData_FindPlayerByField0C, FindPlayerFn,
          Original_CGameData_FindPlayerByField0C, CGameData_FindPlayerByField0C);
S3RE_HOOK(0x004ffd60, BuildingTypeAllowedForRace, RaceTypeFn, Original_BuildingTypeAllowedForRace,
          BuildingTypeAllowedForRace);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// Up to the end of the player records.
constexpr std::size_t kTestWorld = offsetof(CGameData, players) + sizeof(CGameData::players);

void CompareFind(test::Context& ctx, FindPlayerFn original, FindPlayerFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        // Small field values so that matches, near-matches and duplicates occur.
        for (std::uint32_t player = 0; player < kPlayers; ++player) {
            const auto active = static_cast<std::int32_t>(ctx.rng().Chance(1, 4) ? 0 : ctx.rng().Next32() | 1);
            const auto field18 = static_cast<std::int32_t>(ctx.rng().Chance(1, 3) ? ctx.rng().Next32() | 1 : 0);
            const auto field0c = static_cast<std::int32_t>(ctx.rng().Below(6));
            worlds.Both([&](CGameData* w) {
                w->players[player].active = active;
                w->players[player].unk018 = field18;
                w->players[player].team = field0c;
            });
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
std::uint32_t __fastcall WrongFindPlayerByField0C(CGameData* world, void*, std::uint32_t value) {
    for (std::uint32_t player = 0; player < kPlayers; ++player) {
        const Player& record = world->players[player];
        if (record.active != 0 && static_cast<std::uint32_t>(record.team) == value) {
            return player;
        }
    }
    return kNoPlayer;
}

}  // namespace

S3RE_TEST(CGameData_FindPlayerByField0C_MatchesOriginal) {
    CompareFind(ctx, Original_CGameData_FindPlayerByField0C, &CGameData_FindPlayerByField0C, 20000);
}
S3RE_TEST(CGameData_FindPlayerByField0C_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareFind(probe, Original_CGameData_FindPlayerByField0C, &WrongFindPlayerByField0C, 2000);
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

namespace {

// Wrong: counts gems as sulfur (rewrites resource nibble 4 to 5 around the call).
std::uint32_t __fastcall WrongLogPlayerLandResources(CGameData* world, void* edx, std::uint32_t player) {
    std::vector<std::uint8_t*> changed;
    const std::uint32_t size = world->map_size;
    for (std::uint32_t x = 0; x < size; ++x) {
        for (std::uint32_t y = 0; y < size; ++y) {
            std::uint8_t& r = world->tiles[x][y].resources;
            if ((r & 0xf0) == 0x40) {
                r = static_cast<std::uint8_t>(r + 0x10);
                changed.push_back(&r);
            }
        }
    }
    const std::uint32_t result = CGameData_LogPlayerLandResources(world, edx, player);
    for (std::uint8_t* r : changed) {
        *r = static_cast<std::uint8_t>(*r - 0x10);
    }
    return result;
}

// The player records lie beyond the offset table.
constexpr std::size_t kSurveyTestWorld = offsetof(CGameData, players) + sizeof(CGameData::players);

void CompareLandSurvey(test::Context& ctx, LogLandFn candidate, int cases) {
    test::WorldPair worlds(ctx, kSurveyTestWorld);
    // The real table is a spiral of offsets out to about 80 tiles; random
    // offsets of -90..90 (and some wild ones) reach the same code.
    for (std::uint32_t i = 0; i < kSurveyEntries; ++i) {
        const bool wild = ctx.rng().Chance(1, 50);
        const std::int32_t dx = wild ? static_cast<std::int32_t>(ctx.rng().Next32())
                                     : static_cast<std::int32_t>(ctx.rng().Below(181)) - 90;
        const std::int32_t dy = static_cast<std::int32_t>(ctx.rng().Below(181)) - 90;
        worlds.Both([&](CGameData* w) {
            w->offsets[i].dx = dx;
            w->offsets[i].dy = dy;
        });
    }
    DebugLogRecorder logs;
    static constexpr std::uint8_t kTerrain[] = {0x10, 0x15, 0x60, 0x1f, 0x65, 0x20, 0x00, 0x30};
    static constexpr std::uint8_t kObject[] = {0, 0x43, 0x44, 0x50, 0x53, 0x54, 0x72, 0x73, 0x7e, 0x7f};
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = ctx.rng().Chance(1, 2) ? 384 : 64 + ctx.rng().Below(320);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        for (std::uint32_t x = 0; x < size; ++x) {
            for (std::uint32_t y = 0; y < size; ++y) {
                const std::uint8_t terrain = kTerrain[ctx.rng().Below(8)];
                const std::uint8_t object = kObject[ctx.rng().Below(10)];
                const auto resources = static_cast<std::uint8_t>(ctx.rng().Next32() & 0x7f);
                worlds.Both([&](CGameData* w) {
                    layout::Tile& tile = w->tiles[x][y];
                    tile.terrain = terrain;
                    tile.object = object;
                    tile.resources = resources;
                });
            }
        }
        const std::uint32_t player = ctx.rng().Below(20);
        const auto startX = static_cast<std::int32_t>(ctx.rng().Chance(1, 10) ? ctx.rng().Next32() : ctx.rng().Below(size));
        const auto startY = static_cast<std::int32_t>(ctx.rng().Below(size));
        worlds.Both([&](CGameData* w) {
            w->players[player].start_x = startX;
            w->players[player].start_y = startY;
        });
        const std::uint32_t expected = Original_CGameData_LogPlayerLandResources(worlds.original(), nullptr, player);
        const std::vector<DebugLogCall> expectedLogs = logs.Take();
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, player);
        const std::vector<DebugLogCall> actualLogs = logs.Take();
        if (!S3RE_EXPECT_EQ(expected, actual) || !S3RE_EXPECT(expectedLogs.size() == 1) ||
            !S3RE_EXPECT(expectedLogs == actualLogs) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: player %u, map %u", i, player, size);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(CGameData_LogPlayerLandResources_MatchesOriginal) {
    CompareLandSurvey(ctx, &CGameData_LogPlayerLandResources, 40);
}
S3RE_TEST(CGameData_LogPlayerLandResources_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareLandSurvey(probe, &WrongLogPlayerLandResources, 5);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
