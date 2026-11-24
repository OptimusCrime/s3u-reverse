// Tests of the start towns (player_towns.cpp): the original against ours on
// two identical worlds built like a new game's (the original CGameData
// constructor, then a random map from CGameData::GenerateRandomMap), with
// players of every race and start-goods level. Compared: the whole world,
// the DebugLog calls and every writable byte of S3.EXE.

#include "game/debug_log.h"
#include "game/globals.h"
#include "game/map/map_test_support.h"
#include "game/ui/game_settings_layout.h"
#include "game/world.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>
#include <iterator>
#include <vector>

namespace s3re::game {

std::uint32_t __fastcall CGameData_PlaceTown(CGameData* world, void* edx, std::uint32_t player, std::uint32_t x,
                                             std::uint32_t y);
using PlaceTownFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t player, std::uint32_t x,
                                               std::uint32_t y);
extern PlaceTownFn Original_CGameData_PlaceTown;
std::uint32_t __fastcall CGameData_AddPlayerTowns(CGameData* world, void* edx);
using AddPlayerTownsFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx);
extern AddPlayerTownsFn Original_CGameData_AddPlayerTowns;

namespace {

using map_test::Globals;

constexpr std::uint32_t kPlayers = 20;
constexpr std::uint32_t kAnyStartPosition = 100000;  // IsGoodStartPosition: only the site must fit

struct MapCase {
    std::uint32_t seed;
    std::int32_t size;
    std::uint32_t symmetry;
};

// A world with a random map and `active` players (types 1 human, 2
// computer), races 0..3, no start positions yet.
void PrepareGame(test::Context& ctx, test::WorldPair& worlds, const MapCase& map, std::uint32_t active) {
    std::uint32_t races[kPlayers];
    for (std::uint32_t& race : races) {
        race = ctx.rng().Below(4);
    }
    worlds.Both([&](CGameData* w) {
        map_test::ConstructWorld(w);
        map_test::GenerateRandomMap(w, map.seed, map.size, map.symmetry);
        for (std::uint32_t p = 0; p < kPlayers; ++p) {
            layout::Player& player = w->players[p];
            player.active = p < active ? 1 + static_cast<std::int32_t>(p % 2) : 0;
            player.race = static_cast<std::int32_t>(races[p]);
            player.unk018 = 0;
            player.start_x = -1;
            player.start_y = -1;
        }
    });
}

// Runs `original` and `candidate` (each on its world) and compares the
// world, the DebugLog calls and the globals.
template <typename Run>
bool CompareRun(test::Context& ctx, test::WorldPair& worlds, Run run) {
    DebugLogRecorder log;
    Globals globals;
    run(true);
    const std::vector<DebugLogCall> expectedLog = log.Take();
    const std::vector<std::uint8_t> expectedGlobals = globals.TakeOriginal();
    run(false);
    const std::vector<DebugLogCall> actualLog = log.Take();
    const std::vector<std::uint8_t> actualGlobals = Globals::Now();
    bool same = worlds.Checkpoint(ctx);
    same = S3RE_EXPECT_EQ(expectedLog.size(), actualLog.size()) && same;
    for (std::size_t k = 0; same && k < expectedLog.size(); ++k) {
        same = S3RE_EXPECT(expectedLog[k] == actualLog[k]);
    }
    const std::ptrdiff_t difference = Globals::FirstDifference(expectedGlobals, actualGlobals);
    if (difference >= 0) {
        ctx.Fail(__FILE__, __LINE__, "  globals differ at 0x%08x",
                 static_cast<unsigned>(Globals::AddressOf(difference)));
        same = false;
    }
    return same;
}

constexpr MapCase kTownMaps[] = {{1234, 256, 0}, {77, 384, 1}, {4242, 192, 2}};

void ComparePlaceTown(test::Context& ctx, PlaceTownFn candidate, int townsPerMap, int maps) {
    using StartPositionFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t player,
                                                       std::uint32_t x, std::uint32_t y, std::uint32_t attempts);
    const auto isGoodStartPosition = reinterpret_cast<StartPositionFn>(0x005081a0);
    GameSettings& settings = g_gameSettings;
    const std::uint32_t savedGoods = settings.start_goods;
    test::WorldPair worlds(ctx, sizeof(CGameData));
    int placed = 0;
    for (int m = 0; m < maps; ++m) {
        const MapCase& map = kTownMaps[m];
        PrepareGame(ctx, worlds, map, 8);
        for (int t = 0; t < townsPerMap; ++t) {
            const std::uint32_t player = ctx.rng().Below(8);
            const auto size = static_cast<std::uint32_t>(map.size);
            std::uint32_t x = 0, y = 0;
            bool found = false;
            for (int attempt = 0; attempt < 4000 && !found; ++attempt) {
                x = 20 + ctx.rng().Below(size - 40);
                y = 20 + ctx.rng().Below(size - 40);
                found = isGoodStartPosition(worlds.original(), nullptr, player, x, y, kAnyStartPosition) != 0;
            }
            if (!found) {
                continue;
            }
            // The same levelling plan in our world.
            isGoodStartPosition(worlds.ours(), nullptr, player, x, y, kAnyStartPosition);
            settings.start_goods = ctx.rng().Below(5);
            const bool same = CompareRun(ctx, worlds, [&](bool original) {
                if (original) {
                    Original_CGameData_PlaceTown(worlds.original(), nullptr, player, x, y);
                } else {
                    candidate(worlds.ours(), nullptr, player, x, y);
                }
            });
            if (!same) {
                ctx.Fail(__FILE__, __LINE__, "  map %d town %d: player %u at %u,%u, goods %u", m, t, player, x, y,
                         settings.start_goods);
                settings.start_goods = savedGoods;
                return;
            }
            ++placed;
        }
    }
    settings.start_goods = savedGoods;
    // Most towns find a place (the first ones always).
    S3RE_EXPECT(placed >= townsPerMap * maps / 2);
}

// Wrong: medium start goods placed as low.
std::uint32_t __fastcall WrongPlaceTown(CGameData* world, void* edx, std::uint32_t player, std::uint32_t x,
                                        std::uint32_t y) {
    GameSettings& settings = g_gameSettings;
    const std::uint32_t saved = settings.start_goods;
    if (saved == 2) {
        settings.start_goods = 1;
    }
    CGameData_PlaceTown(world, edx, player, x, y);
    settings.start_goods = saved;
    return 0;
}

// AddPlayerTowns cases: a random map, the players taking part, their mirror
// teams (Player +0xc) and whether they have start positions.
struct TownsCase {
    MapCase map;
    std::uint32_t players;
    std::uint32_t teams[4];
    bool startPositions;
    std::uint32_t symmetryOverride;  // world+0x2d274 after the map is made (0xffffffff: keep)
};

constexpr std::uint32_t kKeep = 0xffffffff;

constexpr TownsCase kTownsCases[] = {
    {{1234, 256, 0}, 3, {0, 0, 0, 0}, false, kKeep},
    {{77, 320, 1}, 2, {0, 1, 0, 0}, false, kKeep},
    {{78, 320, 1}, 3, {0, 0, 0, 0}, false, kKeep},
    {{5150, 384, 2}, 2, {0, 1, 0, 0}, false, kKeep},
    {{4242, 384, 3}, 4, {0, 1, 2, 3}, false, kKeep},
    {{4243, 384, 3}, 3, {0, 1, 2, 0}, false, kKeep},
    {{4244, 320, 3}, 2, {0, 1, 0, 0}, false, kKeep},
    {{999, 256, 0}, 3, {0, 0, 0, 0}, true, kKeep},
    {{998, 320, 1}, 2, {0, 1, 0, 0}, true, kKeep},
    {{997, 320, 2}, 2, {0, 1, 0, 0}, true, kKeep},
    {{996, 384, 3}, 4, {0, 1, 2, 3}, true, kKeep},
    // Not tested: symmetry 3 with start positions, team 1 present and team 2 or 3 missing. Both would place
    // player -1 (PlaceTown on the record before the player table; SpawnSettlersAround then writes before the
    // world). Random maps have no start positions, so the game never gets there.
    {{995, 384, 3}, 4, {1, 0, 3, 2}, true, kKeep},
    {{994, 256, 0}, 2, {0, 0, 0, 0}, false, 7},  // symmetry above 3: no towns
};

void CompareAddPlayerTowns(test::Context& ctx, AddPlayerTownsFn candidate, const TownsCase* cases, int count) {
    GameSettings& settings = g_gameSettings;
    const std::uint32_t savedGoods = settings.start_goods;
    test::WorldPair worlds(ctx, sizeof(CGameData));
    for (int i = 0; i < count; ++i) {
        const TownsCase& c = cases[i];
        PrepareGame(ctx, worlds, c.map, c.players);
        const auto size = static_cast<std::uint32_t>(c.map.size);
        std::int32_t starts[4][2];
        for (auto& start : starts) {
            // Given start positions: near the map's middle or anywhere (a site may not be found).
            start[0] = static_cast<std::int32_t>(ctx.rng().Chance(1, 2) ? size / 2 - 40 + ctx.rng().Below(80)
                                                                        : ctx.rng().Below(size));
            start[1] = static_cast<std::int32_t>(ctx.rng().Below(size));
        }
        worlds.Both([&](CGameData* w) {
            for (std::uint32_t p = 0; p < 4; ++p) {
                w->players[p].team = static_cast<std::int32_t>(c.teams[p]);
                if (c.startPositions) {
                    w->players[p].start_x = starts[p][0];
                    w->players[p].start_y = starts[p][1];
                }
            }
            if (c.symmetryOverride != kKeep) {
                w->random_map_symmetry = static_cast<std::int32_t>(c.symmetryOverride);
            }
        });
        settings.start_goods = ctx.rng().Below(4);
        const bool same = CompareRun(ctx, worlds, [&](bool original) {
            if (original) {
                Original_CGameData_AddPlayerTowns(worlds.original(), nullptr);
            } else {
                candidate(worlds.ours(), nullptr);
            }
        });
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: map seed %u size %d symmetry %u", i, c.map.seed, c.map.size,
                     c.map.symmetry);
            break;
        }
    }
    settings.start_goods = savedGoods;
}

// Wrong: symmetry 2 mirrors like symmetry 1 ((y, x) instead of (size - y - 1, size - x - 1)).
std::uint32_t __fastcall WrongAddPlayerTowns(CGameData* world, void* edx) {
    const std::int32_t saved = world->random_map_symmetry;
    if (saved == 2) {
        world->random_map_symmetry = 1;
    }
    const std::uint32_t result = CGameData_AddPlayerTowns(world, edx);
    world->random_map_symmetry = saved;
    return result;
}

}  // namespace

S3RE_TEST(CGameData_AddPlayerTowns_MatchesOriginal) {
    CompareAddPlayerTowns(ctx, &CGameData_AddPlayerTowns, kTownsCases, static_cast<int>(std::size(kTownsCases)));
}
S3RE_TEST(CGameData_AddPlayerTowns_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareAddPlayerTowns(probe, &WrongAddPlayerTowns, kTownsCases + 3, 1);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_PlaceTown_MatchesOriginal) {
    ComparePlaceTown(ctx, &CGameData_PlaceTown, 12, 3);
}
S3RE_TEST(CGameData_PlaceTown_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePlaceTown(probe, &WrongPlaceTown, 12, 1);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
