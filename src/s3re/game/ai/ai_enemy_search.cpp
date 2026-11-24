// The military AI's enemy counts near a tile (computer players A2, session
// 53; Rainer\CompMapWorkerAgent.cpp, re/computer_players.md). Both are
// thiscall on g_aiEnemySearch (0x7e5de4) but never read `this`; they walk
// the idle-settler lists of g_aiMapWorld's players (Player::settler_lists,
// linked through Settler::next, a list ends at an id of 0).
//
//   0x0058a2c0 AI::CountEnemySoldiersNear(x, y, distance, player), ret 0x10:
//              over the players 0..19 other than `player` and not of its
//              team (Player::team; players[player] is not range-checked),
//              the settlers of the nine soldier lists (types 5, 8, 15,
//              30..35, in that order) with TileDistance(x, y, settler x, y)
//              < distance (unsigned). EAX = the count. Callers
//              AI::CastBattleSpells 0x5953b0 (distance 20) and 0x5935b0.
//   0x0058a610 AI::CountEnemyBowmenOnTerritoryNear(x, y, distance, player),
//              ret 0x10: the same over the three bowman lists (types 8, 30,
//              33), counting only bowmen standing on a tile owned by
//              `player` (Tile::owner) within the distance. EAX = the count.
//              Callers AI::SendMoveGroup 0x590c80 (distance 30), 0x592620
//              and 0x58f990.
//
// TileDistance (0x4ff320, stdcall) is called through its S3.EXE address, in
// the original's order.

#include "game/ai/ai_layout.h"
#include "game/enums/settler_types.h"
#include "game/globals.h"
#include "game/layout.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace s3re::game {

using namespace ai;
using layout::kMapRowTiles;
using layout::kPlayers;

namespace {

constexpr std::uintptr_t kTileDistance = 0x004ff320;  // TileDistance (stdcall)

using TileDistanceFn = std::uint32_t(__stdcall*)(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1,
                                                 std::uint32_t y1);

// The lists 0x58a2c0 walks, in its order.
constexpr SettlerType kSoldierTypes[] = {
    SettlerType::Swordsman1, SettlerType::Bowman1,  SettlerType::Pikeman1,  SettlerType::Bowman2,
    SettlerType::Pikeman2,   SettlerType::Swordsman2, SettlerType::Bowman3, SettlerType::Pikeman3,
    SettlerType::Swordsman3,
};
// The lists 0x58a610 walks.
constexpr SettlerType kBowmanTypes[] = {SettlerType::Bowman1, SettlerType::Bowman2, SettlerType::Bowman3};

const layout::Player& PlayerAt(const CGameData* world, std::uint32_t player) {
    return (&world->players[0])[player];  // (not range-checked)
}

bool IsEnemy(const CGameData* world, std::uint32_t other, std::uint32_t player) {
    return other != player && world->players[other].team != PlayerAt(world, player).team;
}

}  // namespace

using EnemyCountFn = std::int32_t(__fastcall*)(void* self, void* edx, std::uint32_t x, std::uint32_t y,
                                               std::uint32_t distance, std::uint32_t player);

std::int32_t __fastcall AI_CountEnemySoldiersNear(void* self, void* edx, std::uint32_t x, std::uint32_t y,
                                                  std::uint32_t distance, std::uint32_t player);
std::int32_t __fastcall AI_CountEnemyBowmenOnTerritoryNear(void* self, void* edx, std::uint32_t x, std::uint32_t y,
                                                           std::uint32_t distance, std::uint32_t player);

S3RE_HOOK(0x0058a2c0, AI_CountEnemySoldiersNear, EnemyCountFn, Original_AI_CountEnemySoldiersNear,
          AI_CountEnemySoldiersNear);
S3RE_HOOK(0x0058a610, AI_CountEnemyBowmenOnTerritoryNear, EnemyCountFn,
          Original_AI_CountEnemyBowmenOnTerritoryNear, AI_CountEnemyBowmenOnTerritoryNear);

// Purpose: How many enemy soldiers are near a tile; the military AI uses it
// to decide whether a mage should cast a battle spell.
std::int32_t __fastcall AI_CountEnemySoldiersNear(void* /*self*/, void* /*edx*/, std::uint32_t x, std::uint32_t y,
                                                  std::uint32_t distance, std::uint32_t player) {
    const auto tileDistance = reinterpret_cast<TileDistanceFn>(kTileDistance);
    std::int32_t count = 0;
    for (std::uint32_t other = 0; other < kPlayers; ++other) {
        const CGameData* world = g_aiMapWorld;
        if (!IsEnemy(world, other, player)) {
            continue;
        }
        for (const SettlerType type : kSoldierTypes) {
            world = g_aiMapWorld;
            for (std::uint32_t id = world->players[other].settler_lists[Value(type)]; id != 0;) {
                const layout::Settler& settler = world->settlers[id];
                if (tileDistance(x, y, settler.x, settler.y) < distance) {
                    ++count;
                }
                world = g_aiMapWorld;
                id = world->settlers[id].next;
            }
        }
    }
    return count;
}

// Purpose: How many enemy bowmen stand on a player's own land near a tile; a
// defending unit group of the military AI stays put while there are any.
std::int32_t __fastcall AI_CountEnemyBowmenOnTerritoryNear(void* /*self*/, void* /*edx*/, std::uint32_t x,
                                                           std::uint32_t y, std::uint32_t distance,
                                                           std::uint32_t player) {
    const auto tileDistance = reinterpret_cast<TileDistanceFn>(kTileDistance);
    std::int32_t count = 0;
    for (std::uint32_t other = 0; other < kPlayers; ++other) {
        const CGameData* world = g_aiMapWorld;
        if (!IsEnemy(world, other, player)) {
            continue;
        }
        for (const SettlerType type : kBowmanTypes) {
            world = g_aiMapWorld;
            for (std::uint32_t id = world->players[other].settler_lists[Value(type)]; id != 0;) {
                const layout::Settler& settler = world->settlers[id];
                const std::uint32_t settlerX = settler.x, settlerY = settler.y;
                // (the position is not range-checked)
                if ((&world->tiles[0][0])[settlerX * kMapRowTiles + settlerY].owner == player &&
                    tileDistance(x, y, settlerX, settlerY) < distance) {
                    ++count;
                }
                world = g_aiMapWorld;
                id = world->settlers[id].next;
            }
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// A test world with four teams over the 20 players, soldier lists of up to 8
// settlers (of all lists, so a wrong list shows), settlers on a 64 x 64 map
// whose tiles belong to players 0..5. TileDistance runs (it is verified).

namespace {

constexpr std::uint32_t kTestMap = 64;
constexpr std::uint32_t kTestSettlers = 2000;

struct EnemyCounts {
    EnemyCountFn soldiers, bowmen;
};

void SetUpWorld(test::Rng& rng, CGameData* world) {
    for (std::uint32_t x = 0; x < kTestMap; ++x) {
        for (std::uint32_t y = 0; y < kTestMap; ++y) {
            world->tiles[x][y].owner = static_cast<std::uint8_t>(rng.Chance(1, 8) ? 0xff : rng.Below(6));
        }
    }
    std::uint32_t next = 1;
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        layout::Player& player = world->players[p];
        player.team = static_cast<std::int32_t>(rng.Below(4));
        for (std::uint16_t& head : player.settler_lists) {
            head = 0;
        }
        for (const SettlerType type : kSoldierTypes) {
            const std::uint32_t length = rng.Chance(1, 3) ? 0 : 1 + rng.Below(8);
            std::uint16_t* link = &player.settler_lists[Value(type)];
            for (std::uint32_t k = 0; k < length && next < kTestSettlers; ++k, ++next) {
                *link = static_cast<std::uint16_t>(next);
                layout::Settler& settler = world->settlers[next];
                settler.x = static_cast<std::uint16_t>(rng.Below(kTestMap));
                settler.y = static_cast<std::uint16_t>(rng.Below(kTestMap));
                link = &settler.next;
            }
            *link = 0;
        }
    }
}

void CompareEnemyCounts(test::Context& ctx, const EnemyCounts& candidate, int cases) {
    const EnemyCounts original{Original_AI_CountEnemySoldiersNear, Original_AI_CountEnemyBowmenOnTerritoryNear};
    CGameData* savedMapWorld = g_aiMapWorld;
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    g_aiMapWorld = world;
    void* self = &g_aiEnemySearch.get();
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        if (i % 50 == 0) {
            SetUpWorld(rng, world);
        }
        const std::uint32_t x = rng.Chance(1, 10) ? rng.Next32() : rng.Below(kTestMap);
        const std::uint32_t y = rng.Chance(1, 10) ? rng.Next32() : rng.Below(kTestMap);
        const std::uint32_t distance = rng.Chance(1, 10) ? rng.Next32() : rng.Below(50);
        const std::uint32_t player = rng.Below(kPlayers);
        const bool bowmen = rng.Chance(1, 2);
        const EnemyCountFn f = bowmen ? original.bowmen : original.soldiers;
        const EnemyCountFn g = bowmen ? candidate.bowmen : candidate.soldiers;
        const std::int32_t ra = f(self, nullptr, x, y, distance, player);
        const std::int32_t rb = g(self, nullptr, x, y, distance, player);
        if (!S3RE_EXPECT_EQ(ra, rb)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, %s, (%u, %u), distance %u, player %u", i,
                     bowmen ? "bowmen" : "soldiers", x, y, distance, player);
        }
    }
    g_aiMapWorld = savedMapWorld;
}

std::int32_t __fastcall WrongCountSoldiers(void* /*self*/, void* /*edx*/, std::uint32_t x, std::uint32_t y,
                                           std::uint32_t distance, std::uint32_t player) {
    // Wrong: counts allies too (only skips the player itself).
    const CGameData* world = g_aiMapWorld;
    const auto tileDistance = reinterpret_cast<TileDistanceFn>(kTileDistance);
    std::int32_t count = 0;
    for (std::uint32_t other = 0; other < kPlayers; ++other) {
        if (other == player) {
            continue;
        }
        for (const SettlerType type : kSoldierTypes) {
            for (std::uint32_t id = world->players[other].settler_lists[Value(type)]; id != 0;
                 id = world->settlers[id].next) {
                count += tileDistance(x, y, world->settlers[id].x, world->settlers[id].y) < distance ? 1 : 0;
            }
        }
    }
    return count;
}

std::int32_t __fastcall WrongCountBowmen(void* self, void* edx, std::uint32_t x, std::uint32_t y,
                                         std::uint32_t distance, std::uint32_t player) {
    // Wrong: "at most" the distance.
    return AI_CountEnemyBowmenOnTerritoryNear(self, edx, x, y, distance + 1, player);
}

}  // namespace

S3RE_TEST(AiEnemyCounts_MatchOriginal) {
    CompareEnemyCounts(ctx, {&AI_CountEnemySoldiersNear, &AI_CountEnemyBowmenOnTerritoryNear}, 4000);
}

S3RE_TEST(AiEnemyCounts_ComparisonDetectsWrongImplementation) {
    for (const EnemyCounts& wrong : {EnemyCounts{&WrongCountSoldiers, &AI_CountEnemyBowmenOnTerritoryNear},
                                     EnemyCounts{&AI_CountEnemySoldiersNear, &WrongCountBowmen}}) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareEnemyCounts(quiet, wrong, 2000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

}  // namespace s3re::game
