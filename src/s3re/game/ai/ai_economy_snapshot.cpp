// The T_ECONOMY_AGENT's snapshot of its economy area (computer players A2;
// re/computer_players.md "The economy agent"; EconomyAgent in ai_layout.h).
//
//   0x00567910 T_ECONOMY_AGENT::TakeAreaSnapshot() thiscall on the agent,
//              first step of its run 0x567890: carriers (settlers of type 0
//              in the area, AI_CountAreaSettlersOfType); the population
//              (once the census has counted the settlers, the census's
//              per-player figure, else AI::AreaPopulation); logs, boards,
//              stones, coal, iron and sulfur on the area's piles
//              (AI_AreaGoodsAmount); and the node's buildings per type
//              (g_aiPlayerData->building_counts): the common ones, then by
//              race the war machine workshop, the temples, the drink
//              building and the race-only buildings (0 for the other
//              races); a race above 3 leaves the race-specific fields as
//              they are. EAX is not used by the caller.
//   0x0059dcc0 AI::AreaPopulation(const u16* area) fastcall:
//              CGameData::CountAreaSettlersOfClass0(g_aiWorld, *area)
//              (0x50e750); EAX = its result.

#include "game/ai/ai_layout.h"
#include "game/enums/buildings.h"
#include "game/enums/goods.h"
#include "game/enums/races.h"
#include "game/globals.h"
#include "game/layout.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using namespace ai;

namespace {

using B = BuildingType;
using Agent = EconomyAgent;

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kCountAreaSettlersOfClass0 = 0x0050e750;  // CGameData::CountAreaSettlersOfClass0
constexpr std::uintptr_t kCountAreaSettlersOfType = 0x0059dc70;    // AI_CountAreaSettlersOfType
constexpr std::uintptr_t kAreaGoodsAmount = 0x0059dc10;            // AI_AreaGoodsAmount
constexpr std::uintptr_t kAreaPopulation = 0x0059dcc0;             // AI::AreaPopulation

constexpr std::uint8_t kCarrierType = 0;  // SettlerType 0: carriers

std::uint16_t GoodsInArea(const Agent& agent, Good good) {
    using Fn = std::uint32_t(__fastcall*)(const std::uint8_t* goods, const std::uint16_t* area);
    const std::uint8_t goods = Value(good);
    return static_cast<std::uint16_t>(reinterpret_cast<Fn>(kAreaGoodsAmount)(&goods, &agent.area));
}

// The node's buildings of a type (g_aiPlayerData is re-read each time, as
// in the original).
std::uint16_t NodeCount(B type) {
    return g_aiPlayerData.get()->building_counts[Value(type)];
}

}  // namespace

using SnapshotFn = std::uint32_t(__fastcall*)(Agent* agent, void* edx);
using AreaPopulationFn = std::uint32_t(__fastcall*)(const std::uint16_t* area);

std::uint32_t __fastcall T_ECONOMY_AGENT_TakeAreaSnapshot(Agent* agent, void* edx);
std::uint32_t __fastcall AI_AreaPopulation(const std::uint16_t* area);

S3RE_HOOK(0x00567910, T_ECONOMY_AGENT_TakeAreaSnapshot, SnapshotFn, Original_T_ECONOMY_AGENT_TakeAreaSnapshot,
          T_ECONOMY_AGENT_TakeAreaSnapshot);
S3RE_HOOK(0x0059dcc0, AI_AreaPopulation, AreaPopulationFn, Original_AI_AreaPopulation, AI_AreaPopulation);

// Purpose: How many civilians the player has in an economy area (the
// economy agent's population, and the land evaluation's).
std::uint32_t __fastcall AI_AreaPopulation(const std::uint16_t* area) {
    using Fn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t area);
    return reinterpret_cast<Fn>(kCountAreaSettlersOfClass0)(g_aiWorld, nullptr, *area);
}

// Purpose: Before the economy agent decides what to build, it notes the
// area's population, carriers, goods in stock and buildings per type.
std::uint32_t __fastcall T_ECONOMY_AGENT_TakeAreaSnapshot(Agent* agent, void* /*edx*/) {
    using CountFn = std::uint32_t(__fastcall*)(const std::uint8_t* type, const std::uint16_t* area);
    using PopulationFn = std::uint32_t(__fastcall*)(const std::uint16_t* area);
    const std::uint8_t carrier = kCarrierType;
    agent->carriers = static_cast<std::uint16_t>(reinterpret_cast<CountFn>(kCountAreaSettlersOfType)(&carrier, &agent->area));
    const AiAgentData& census = g_aiCensus;
    if (census.settlers_counted != 0) {
        // The census's settlers of the player (the low word).
        agent->population = static_cast<std::uint16_t>(census.all_settlers[g_aiPlayerNumber]);
    } else {
        agent->population = static_cast<std::uint16_t>(reinterpret_cast<PopulationFn>(kAreaPopulation)(&agent->area));
    }
    agent->logs = GoodsInArea(*agent, Good::Log);
    agent->boards = GoodsInArea(*agent, Good::Board);
    agent->stones = GoodsInArea(*agent, Good::Stone);
    agent->coal = GoodsInArea(*agent, Good::Coal);
    agent->iron = GoodsInArea(*agent, Good::Iron);
    agent->sulfur = GoodsInArea(*agent, Good::Sulfur);

    // 0x5679cf..0x567b98: the buildings every race has.
    agent->woodcutters = NodeCount(B::Woodcutter);
    agent->stonecutters = NodeCount(B::Stonecutter);
    agent->sawmills = NodeCount(B::Sawmill);
    agent->foresters = NodeCount(B::Forester);
    agent->iron_smelters = NodeCount(B::IronSmelter);
    agent->toolsmiths = NodeCount(B::Toolsmith);
    agent->weaponsmiths = NodeCount(B::Weaponsmith);
    agent->gold_smelters = NodeCount(B::GoldSmelter);
    agent->coal_mines = NodeCount(B::CoalMine);
    agent->iron_mines = NodeCount(B::IronMine);
    agent->gold_mines = NodeCount(B::GoldMine);
    agent->fisheries = NodeCount(B::Fishery);
    agent->farms = NodeCount(B::Farm);
    agent->waterworks = NodeCount(B::Waterworks);
    agent->mills = NodeCount(B::Mill);
    agent->bakeries = NodeCount(B::Bakery);
    agent->pig_farms = NodeCount(B::PigFarm);
    agent->slaughterhouses = NodeCount(B::Slaughterhouse);
    agent->donkey_farms = NodeCount(B::DonkeyFarm);
    agent->barracks = NodeCount(B::Barracks);
    agent->harbours = NodeCount(B::Harbour);
    agent->shipyards = NodeCount(B::Shipyard);
    agent->castles = NodeCount(B::Castle);
    agent->large_towers = NodeCount(B::LargeTower);
    agent->marketplaces = NodeCount(B::Marketplace);
    agent->hospitals = NodeCount(B::Hospital);

    // 0x567bac..0x567dd5 (jump table 0x567de4): the race's own buildings;
    // the fields of the other races' buildings become 0.
    switch (static_cast<Race>(agent->race)) {
    case Race::Roman:
        agent->war_workshops = NodeCount(B::CatapultWorkshop);
        agent->small_temples = NodeCount(B::SmallTemple);
        agent->large_temples = NodeCount(B::LargeTemple);
        agent->breweries = 0;
        agent->vineyards = NodeCount(B::Vineyard);
        agent->distilleries = 0;
        agent->meaderies = 0;
        agent->gem_mines = 0;
        agent->powder_makers = 0;
        agent->sulfur_mines = 0;
        agent->rice_farms = 0;
        break;
    case Race::Egyptian:
        agent->war_workshops = NodeCount(B::BallistaWorkshop);
        agent->small_temples = NodeCount(B::Sphinx);
        agent->vineyards = 0;
        agent->large_temples = NodeCount(B::Pyramid);
        agent->distilleries = 0;
        agent->breweries = NodeCount(B::Brewery);
        agent->meaderies = 0;
        agent->gem_mines = NodeCount(B::GemMine);
        agent->powder_makers = 0;
        agent->sulfur_mines = 0;
        agent->rice_farms = 0;
        break;
    case Race::Asian:
        agent->war_workshops = NodeCount(B::CannonWorkshop);
        agent->small_temples = NodeCount(B::SmallPagoda);
        agent->vineyards = 0;
        agent->large_temples = NodeCount(B::LargePagoda);
        agent->breweries = 0;
        agent->meaderies = 0;
        agent->distilleries = NodeCount(B::Distillery);
        agent->gem_mines = 0;
        agent->powder_makers = NodeCount(B::PowderMaker);
        agent->sulfur_mines = NodeCount(B::SulfurMine);
        agent->rice_farms = NodeCount(B::RiceFarm);
        break;
    case Race::Amazon:
        agent->war_workshops = NodeCount(B::LargeGong);
        agent->small_temples = NodeCount(B::AmazonSmallTemple);
        agent->vineyards = 0;
        agent->large_temples = NodeCount(B::AmazonLargeTemple);
        agent->breweries = 0;
        agent->distilleries = 0;
        agent->meaderies = NodeCount(B::Meadery);
        agent->powder_makers = 0;
        agent->gem_mines = NodeCount(B::GemMine);
        agent->rice_farms = 0;
        agent->sulfur_mines = NodeCount(B::AmazonSulfurMine);
        agent->apiaries = NodeCount(B::Apiary);
        agent->laboratories = NodeCount(B::Laboratory);
        break;
    default: break;
    }
    return 0;  // EAX: not used by the caller (0x567890)
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Both sides get the same random agent and AiPlayerData and a world pair
// with a few settlers (CountAreaSettlersOfClass0 scans them; the area's
// carrier list links some) and short pile lists of the six goods in each
// of four areas.

namespace {

constexpr std::size_t kAgentBytes = sizeof(Agent) + 16;
constexpr std::size_t kDataBytes = sizeof(AiPlayerData) + 16;
constexpr std::uint16_t kAreas = 4;
constexpr std::uint32_t kSettlers = 40;

struct Saved {
    CGameData* world = g_aiWorld;
    AiPlayerData* data = g_aiPlayerData;
    std::uint8_t number = g_aiPlayerNumber;
    std::uint8_t counted = g_aiCensus.get().settlers_counted;
    std::uint32_t settlers[layout::kPlayers];
    Saved() { std::memcpy(settlers, g_aiCensus.get().all_settlers, sizeof(settlers)); }
    ~Saved() {
        g_aiWorld = world;
        g_aiPlayerData = data;
        g_aiPlayerNumber = number;
        g_aiCensus.get().settlers_counted = counted;
        std::memcpy(g_aiCensus.get().all_settlers, settlers, sizeof(settlers));
    }
};

void SetUpWorld(test::Context& ctx, test::WorldPair& worlds) {
    // Settlers 1..kSettlers on a small part of the map; some stand on their
    // tile (tile.settler), whose area is one of the four.
    worlds.Both([&](CGameData* w) { w->settler_high_water = kSettlers; });
    for (std::uint32_t id = 1; id <= kSettlers + 1; ++id) {
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Chance(1, 6) ? 0xff : ctx.rng().Below(3));
        const auto art = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Below(60));
        const auto x = static_cast<std::uint16_t>(ctx.rng().Below(60));
        const auto y = static_cast<std::uint16_t>(ctx.rng().Below(60));
        const bool onTile = ctx.rng().Chance(2, 3);
        const auto area = static_cast<std::uint16_t>(ctx.rng().Below(kAreas));
        worlds.Both([&](CGameData* w) {
            layout::Settler& s = w->settlers[id];
            s.owner = owner;
            s.art = art;
            s.x = x;
            s.y = y;
            if (onTile) {
                w->tiles[x][y].settler = static_cast<std::uint16_t>(id);
                w->tiles[x][y].area = area;
            }
        });
    }
    std::uint32_t base = 1 + ctx.rng().Below(1000);
    constexpr std::uint8_t kGoods[] = {Value(Good::Log),  Value(Good::Board), Value(Good::Stone),
                                       Value(Good::Coal), Value(Good::Iron),  Value(Good::Sulfur)};
    for (std::uint16_t area = 0; area < kAreas; ++area) {
        const auto player = static_cast<std::uint8_t>(ctx.rng().Chance(1, 8) ? 0xff : ctx.rng().Below(3));
        // The carrier list: a chain of settlers (each in one list).
        std::uint16_t carriers = 0;
        const std::uint32_t length = ctx.rng().Below(5);
        for (std::uint32_t k = 0; k < length; ++k) {
            const auto settler = static_cast<std::uint16_t>(1000 + area * 10 + k);
            worlds.Both([&](CGameData* w) { w->settlers[settler].next = carriers; });
            carriers = settler;
        }
        worlds.Both([&](CGameData* w) {
            w->areas[area].player = player;
            w->areas[area].settlers_by_type[kCarrierType] = carriers;
        });
        for (std::uint8_t goods : kGoods) {
            std::uint16_t head = 0;
            const std::uint32_t piles = ctx.rng().Below(6);
            for (std::uint32_t k = 0; k < piles; ++k) {
                const auto pile = static_cast<std::uint16_t>(base + k);
                const auto amount = static_cast<std::uint8_t>(ctx.rng().Below(9));
                worlds.Both([&](CGameData* w) {
                    w->piles[pile].next = head;
                    w->piles[pile].amount = amount;
                });
                head = pile;
            }
            base += 10;
            worlds.Both([&](CGameData* w) { w->areas[area].piles_by_goods[goods] = head; });
        }
    }
}

void CompareSnapshot(test::Context& ctx, SnapshotFn candidate, AreaPopulationFn population, int cases) {
    Saved saved;
    test::WorldPair worlds(ctx, sizeof(CGameData));
    std::vector<std::uint8_t> agentA(kAgentBytes), agentB(kAgentBytes), dataA(kDataBytes), dataB(kDataBytes);
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        if (i % 16 == 0) {
            SetUpWorld(ctx, worlds);
        }
        ctx.rng().Fill(agentA.data(), agentA.size());
        auto* agent = reinterpret_cast<Agent*>(agentA.data());
        agent->race = ctx.rng().Chance(1, 10) ? static_cast<std::int32_t>(ctx.rng().Next32())
                                              : static_cast<std::int32_t>(ctx.rng().Below(4));
        agent->area = static_cast<std::uint16_t>(ctx.rng().Below(kAreas));
        agentB = agentA;
        ctx.rng().Fill(dataA.data(), dataA.size());
        dataB = dataA;
        g_aiPlayerNumber = static_cast<std::uint8_t>(ctx.rng().Below(layout::kPlayers));
        g_aiCensus.get().settlers_counted = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0 : 1 + ctx.rng().Below(255));
        ctx.rng().Fill(g_aiCensus.get().all_settlers, sizeof(g_aiCensus.get().all_settlers));

        std::uint32_t ra = 0, rb = 0;
        if (ctx.rng().Chance(1, 4)) {
            const auto area = static_cast<std::uint16_t>(ctx.rng().Below(kAreas));
            g_aiWorld = worlds.original();
            ra = Original_AI_AreaPopulation(&area);
            g_aiWorld = worlds.ours();
            rb = population(&area);
        } else {
            g_aiWorld = worlds.original();
            g_aiPlayerData = reinterpret_cast<AiPlayerData*>(dataA.data());
            Original_T_ECONOMY_AGENT_TakeAreaSnapshot(reinterpret_cast<Agent*>(agentA.data()), nullptr);
            g_aiWorld = worlds.ours();
            g_aiPlayerData = reinterpret_cast<AiPlayerData*>(dataB.data());
            candidate(reinterpret_cast<Agent*>(agentB.data()), nullptr);  // EAX: not used by the caller
        }
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT_MEM_EQ(agentA.data(), agentB.data(), agentA.size());
        same &= S3RE_EXPECT_MEM_EQ(dataA.data(), dataB.data(), dataA.size());
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
        }
    }
    S3RE_EXPECT(worlds.Checkpoint(ctx));
}

std::uint32_t __fastcall WrongSnapshot(Agent* agent, void* edx) {
    // Wrong: the Asian powder makers are left as they were.
    const std::uint16_t kept = agent->powder_makers;
    const std::uint32_t result = T_ECONOMY_AGENT_TakeAreaSnapshot(agent, edx);
    if (static_cast<Race>(agent->race) == Race::Asian) {
        agent->powder_makers = kept;
    }
    return result;
}

std::uint32_t __fastcall WrongPopulation(const std::uint16_t* area) {
    // Wrong: one too many.
    return AI_AreaPopulation(area) + 1;
}

}  // namespace

S3RE_TEST(AiEconomySnapshot_MatchesOriginal) {
    CompareSnapshot(ctx, &T_ECONOMY_AGENT_TakeAreaSnapshot, &AI_AreaPopulation, 3000);
}

S3RE_TEST(AiEconomySnapshot_ComparisonDetectsWrongImplementation) {
    {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareSnapshot(quiet, &WrongSnapshot, &AI_AreaPopulation, 600);
        S3RE_EXPECT(quiet.failures() > 0);
    }
    {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareSnapshot(quiet, &T_ECONOMY_AGENT_TakeAreaSnapshot, &WrongPopulation, 600);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

}  // namespace s3re::game
