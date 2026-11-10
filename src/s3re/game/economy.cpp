// Callees of GameWorld::UpdatePhase2 (0x4e05e0): population counts and
// helpers of the economy step.
//
// GameWorld::RecountPlayerPopulation (0x005104c0): () thiscall, ret. For
//   each of the 20 players: population (+0xc4) = 0, population_limit
//   (+0xc8) = population_limit_base (+0xcc), carriers (+0xd0) = 0,
//   surplus_carriers (+0xd4) = 0. Then for settlers 1..high water (re-read
//   every step) with an owner (!= 0xff) whose type class (S3.EXE 0x658f64)
//   is 0 or 10..29 (signed): players[owner].population += 1, and for type 0
//   also carriers += 1. Then for buildings 1..high water with an owner and
//   buildup 0 (finished): type 0x1a/0x1b/0x1c adds 10/30/100 to the
//   owner's population_limit (residences, probably). Finally per player:
//   if population != 0 and population > limit (unsigned), surplus_carriers
//   = carriers - (((limit << 16) / population) * carriers >> 16) (all 32-bit
//   unsigned, truncating), else 0. The owner indexes the player records
//   without a bound check, as in the original. EAX: not used by the only
//   caller (UpdatePhase2).
// GameWorld::SetTowerSoldierAnimation (0x0050e200): (settler) thiscall,
//   ret 4. Animation kind 0x176, or 0x177..0x17e for settler types 0x20,
//   0x23, 8, 0x1e, 0x21, 0xf, 0x1f, 0x22 (the soldier types); + 9 if the
//   settler's building (+0x22) has type 0x10, + 0x12 for type 0x12; + 0x1b
//   / + 0x36 for the building's race 1 / 2. Then SetSettlerAnimation(settler,
//   kind); EAX = its result (the one caller overwrites AL at once). Called by
//   UpdatePhase2 when a soldier enters a military building.

#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace s3re::game {

namespace {

using layout::Building;
using layout::Player;
using layout::Settler;

const std::int32_t* const kSettlerTypeClass = reinterpret_cast<const std::int32_t*>(0x00658f64);

constexpr std::uint8_t kFree = 0xff;

// The player record of an owner byte (no bound check, like the original).
Player& PlayerOf(GameWorld* world, std::uint8_t owner) {
    return *(world->players + owner);
}

}  // namespace

std::uint32_t __fastcall GameWorld_SetSettlerAnimation(GameWorld* world, void* edx, std::uint32_t settler,
                                                       std::uint32_t kind);

using RecountFn = void(__fastcall*)(GameWorld* world, void* edx);
using SettlerFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t settler);

// Purpose: Recounts each player's population (civilians and carriers) and the
// housing limit from the finished residences, and how many carriers are
// beyond that limit.
void __fastcall GameWorld_RecountPlayerPopulation(GameWorld* world, void*) {
    for (Player& player : world->players) {
        player.population = 0;
        player.population_limit = player.population_limit_base;
        player.carriers = 0;
        player.surplus_carriers = 0;
    }
    for (std::uint32_t n = 1; n <= world->settler_high_water; ++n) {
        const Settler& settler = world->settlers[n];
        if (settler.owner == kFree) {
            continue;
        }
        const std::int32_t settlerClass = kSettlerTypeClass[settler.art];
        if (settlerClass != 0 && (settlerClass < 10 || settlerClass > 29)) {
            continue;
        }
        ++PlayerOf(world, settler.owner).population;
        if (settler.art == 0) {
            ++PlayerOf(world, settler.owner).carriers;
        }
    }
    for (std::uint32_t n = 1; n <= world->building_high_water; ++n) {
        const Building& building = world->buildings[n];
        if (building.owner == kFree || building.buildup != 0) {
            continue;
        }
        if (building.art == 0x1a) {
            PlayerOf(world, building.owner).population_limit += 10;
        }
        if (building.art == 0x1b) {
            PlayerOf(world, building.owner).population_limit += 30;
        }
        if (building.art == 0x1c) {
            PlayerOf(world, building.owner).population_limit += 100;
        }
    }
    for (Player& player : world->players) {
        const std::uint32_t population = player.population;
        if (population != 0 && population > player.population_limit) {
            const std::uint32_t share = (player.population_limit << 16) / population;
            player.surplus_carriers = player.carriers - ((share * player.carriers) >> 16);
        } else {
            player.surplus_carriers = 0;
        }
    }
}

// Purpose: Gives a soldier who has just entered a military building the
// animation for his type, the building's type and the building's race.
std::uint32_t __fastcall GameWorld_SetTowerSoldierAnimation(GameWorld* world, void*, std::uint32_t settler) {
    std::uint32_t kind = 0x176;
    switch (world->settlers[settler].art) {
        case 0x20: kind = 0x177; break;
        case 0x23: kind = 0x178; break;
        case 0x08: kind = 0x179; break;
        case 0x1e: kind = 0x17a; break;
        case 0x21: kind = 0x17b; break;
        case 0x0f: kind = 0x17c; break;
        case 0x1f: kind = 0x17d; break;
        case 0x22: kind = 0x17e; break;
        default: break;
    }
    const Building& building = world->buildings[world->settlers[settler].building];
    if (building.art == 0x10) {
        kind += 9;
    }
    if (building.art == 0x12) {
        kind += 0x12;
    }
    if (building.race == 1) {
        kind += 0x1b;
    }
    if (building.race == 2) {
        kind += 0x36;
    }
    return GameWorld_SetSettlerAnimation(world, nullptr, settler, kind);
}

S3RE_HOOK(0x005104c0, GameWorld_RecountPlayerPopulation, RecountFn, Original_GameWorld_RecountPlayerPopulation,
          GameWorld_RecountPlayerPopulation);
S3RE_HOOK(0x0050e200, GameWorld_SetTowerSoldierAnimation, SettlerFn, Original_GameWorld_SetTowerSoldierAnimation,
          GameWorld_SetTowerSoldierAnimation);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kPopulationWorld = offsetof(GameWorld, buildings) + 400 * sizeof(Building);

void ComparePopulation(test::Context& ctx, RecountFn candidate, int cases) {
    test::WorldPair worlds(ctx, kPopulationWorld);
    static constexpr std::uint8_t kResidences[] = {0x1a, 0x1b, 0x1c};
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t settlers = ctx.rng().Below(300);
        const std::uint32_t buildings = ctx.rng().Below(390);
        const bool wildOwners = ctx.rng().Chance(1, 10);
        auto owner = [&]() -> std::uint8_t {
            if (ctx.rng().Chance(1, 6)) {
                return kFree;
            }
            return static_cast<std::uint8_t>(wildOwners && ctx.rng().Chance(1, 20) ? ctx.rng().Below(255)
                                                                                    : ctx.rng().Below(20));
        };
        worlds.Both([&](GameWorld* w) { w->settler_high_water = settlers; });
        worlds.Both([&](GameWorld* w) { w->building_high_water = buildings; });
        for (std::uint32_t n = 1; n <= settlers; ++n) {
            const std::uint8_t o = owner();
            const auto art = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Below(0x30));
            worlds.Both([&](GameWorld* w) {
                w->settlers[n].owner = o;
                w->settlers[n].art = art;
            });
        }
        for (std::uint32_t n = 1; n <= buildings; ++n) {
            const std::uint8_t o = owner();
            const auto art = ctx.rng().Chance(1, 2) ? kResidences[ctx.rng().Below(3)]
                                                    : static_cast<std::uint8_t>(ctx.rng().Below(0x39));
            const auto buildup = static_cast<std::uint8_t>(ctx.rng().Chance(3, 4) ? 0 : ctx.rng().Below(256));
            worlds.Both([&](GameWorld* w) {
                w->buildings[n].owner = o;
                w->buildings[n].art = art;
                w->buildings[n].buildup = buildup;
            });
        }
        for (std::uint32_t p = 0; p < 20; ++p) {
            const std::uint32_t base = ctx.rng().Chance(1, 4) ? ctx.rng().Next32() : ctx.rng().Below(200);
            worlds.Both([&](GameWorld* w) { w->players[p].population_limit_base = base; });
        }
        Original_GameWorld_RecountPlayerPopulation(worlds.original(), nullptr);
        candidate(worlds.ours(), nullptr);
        if (!worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

// Wrong: rounds the share up instead of truncating.
void __fastcall WrongRecountPlayerPopulation(GameWorld* world, void* edx) {
    GameWorld_RecountPlayerPopulation(world, edx);
    for (Player& player : world->players) {
        if (player.surplus_carriers != 0) {
            --player.surplus_carriers;
        }
    }
}

// The graphics object fields GfxData_SpriteWord reads (see logic_helpers.cpp):
// [[this+0xb24+group*4] + 8 + index*4] + [this+0x264+group*4].
#pragma pack(push, 1)
struct GfxFields {
    std::uint8_t pad000[0x264];
    std::uint32_t group_bias[0xb0];    // 0x264
    std::uint8_t pad524[0x600];
    std::uint32_t group_tables[0xb0];  // 0xb24
};
#pragma pack(pop)
static_assert(offsetof(GfxFields, group_tables) == 0xb24);

constexpr std::uintptr_t kGfxData = 0x007a8010;

void CompareTowerAnimation(test::Context& ctx, SettlerFn candidate, int cases) {
    // Every group's sprite table points at the same random sprite pointers.
    std::vector<std::uint8_t> object(0x1000);
    std::vector<std::uint32_t> sprites(2 + 0x4000);
    std::vector<std::uint8_t> frames(0x2000);
    ctx.rng().Fill(frames.data(), frames.size());
    auto& gfx = *reinterpret_cast<GfxFields*>(object.data());
    for (std::uint32_t g = 0; g < 0xb0; ++g) {
        gfx.group_tables[g] = reinterpret_cast<std::uint32_t>(sprites.data());
        gfx.group_bias[g] = 0;
    }
    for (std::uint32_t k = 2; k < sprites.size(); ++k) {
        sprites[k] = reinterpret_cast<std::uint32_t>(frames.data() + ctx.rng().Below(0x1f00));
    }
    std::uint32_t& global = *reinterpret_cast<std::uint32_t*>(kGfxData);
    const std::uint32_t saved = global;
    global = reinterpret_cast<std::uint32_t>(object.data());

    test::WorldPair worlds(ctx, offsetof(GameWorld, buildings) + 0x200 * sizeof(Building));
    static constexpr std::uint8_t kSoldiers[] = {0x20, 0x23, 8, 0x1e, 0x21, 0xf, 0x1f, 0x22};
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t settler = 1 + ctx.rng().Below(0x7cfe);
        const auto art = ctx.rng().Chance(3, 4) ? kSoldiers[ctx.rng().Below(8)]
                                                : static_cast<std::uint8_t>(ctx.rng().Below(256));
        const auto race = static_cast<std::uint8_t>(ctx.rng().Below(4));
        const auto building = static_cast<std::uint16_t>(ctx.rng().Below(0x200));
        static constexpr std::uint8_t kTypes[] = {0x10, 0x12, 0xf, 0x11};
        const std::uint8_t type = ctx.rng().Chance(4, 5) ? kTypes[ctx.rng().Below(4)]
                                                        : static_cast<std::uint8_t>(ctx.rng().Below(256));
        const auto buildingRace = static_cast<std::uint8_t>(ctx.rng().Chance(9, 10) ? ctx.rng().Below(4)
                                                                                   : ctx.rng().Below(256));
        worlds.Both([&](GameWorld* w) {
            w->settlers[settler].art = art;
            w->settlers[settler].race = race;
            w->settlers[settler].building = building;
            w->buildings[building].art = type;
            w->buildings[building].race = buildingRace;
        });
        const std::uint32_t expected = Original_GameWorld_SetTowerSoldierAnimation(worlds.original(), nullptr, settler);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, settler);
        if (!S3RE_EXPECT_EQ(expected, actual) ||
            !worlds.SameField(ctx, [&](GameWorld* w) -> auto& { return w->settlers[settler]; }) ||
            (i % 256 == 255 && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: settler %u art %u building type %u race %u", i, settler, art,
                     type, buildingRace);
            break;
        }
    }
    S3RE_EXPECT(worlds.Checkpoint(ctx));
    global = saved;
}

// Wrong: ignores the building's race 2.
std::uint32_t __fastcall WrongTowerSoldierAnimation(GameWorld* world, void* edx, std::uint32_t settler) {
    Building& building = world->buildings[world->settlers[settler].building];
    const std::uint8_t race = building.race;
    if (race == 2) {
        building.race = 0;
    }
    const std::uint32_t result = GameWorld_SetTowerSoldierAnimation(world, edx, settler);
    building.race = race;
    return result;
}

}  // namespace

S3RE_TEST(GameWorld_RecountPlayerPopulation_MatchesOriginal) {
    ComparePopulation(ctx, &GameWorld_RecountPlayerPopulation, 300);
}
S3RE_TEST(GameWorld_RecountPlayerPopulation_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePopulation(probe, &WrongRecountPlayerPopulation, 50);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_SetTowerSoldierAnimation_MatchesOriginal) {
    CompareTowerAnimation(ctx, &GameWorld_SetTowerSoldierAnimation, 20000);
}
S3RE_TEST(GameWorld_SetTowerSoldierAnimation_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareTowerAnimation(probe, &WrongTowerSoldierAnimation, 5000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
