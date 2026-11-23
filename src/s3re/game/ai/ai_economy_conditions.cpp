// The T_ECONOMY_AGENT's building conditions (computer players A2;
// re/computer_players.md "The economy agent").
//
// Each run of the economy agent (0x567890) takes a snapshot of the current
// node's area into the agent (0x567910: population, goods in stock,
// buildings of the node per type; EconomyAgent in ai_layout.h) and then
// calls one condition per building type from 0x56a1a0 (skipping a type
// while its countdown AiPlayerData::candidate_values[type] runs or
// type_blocked[type] is set). A condition that finds the type missing
// asks for it with AI::RecordBestCandidate (0x58ec50; type, priority, and
// the position -1, -1: any place), which keeps the highest priority per
// type and restarts the type's countdown. All are thiscall on the agent,
// without arguments; EAX is not used by 0x56a1a0.
//
//   0x00568250 Sawmill: none -> 65000; else (woodcutters + 2) / 3 >
//              sawmills -> g_aiBuildPriorityCaps[Sawmill].
//   0x005682b0 Forester: per race a surplus of woodcutters over foresters
//              (Roman wc * 100 - f * 250, Egyptian (wc - 3 f) * 100,
//              Asian and Amazon (wc - 2 f) * 100) minus boards and logs in
//              stock, graded > 80 / 60 / 40 / 20 / 0 into the cap of
//              Forester / 2 / 3 / 4 / 6 / 0 (also stored in
//              forester_priority). For a race above 3 the original grades
//              the agent's own address (a stack leftover; reproduced).
//   0x00569160 Toolsmith: no toolsmith order taken (best[57 + Toolsmith])
//              and no toolsmith, with coal (mines or stock) and iron
//              (smelters or stock) -> 35000.
//   0x005691b0 Weaponsmith: with coal and iron (mines/smelters or stock)
//              and a finished toolsmith in the area (AI_CountBuildings):
//              none -> 30000; else with iron > 4 and coal > 10 in stock,
//              graded by weaponsmiths * 100 / wanted (WantedForPopulation).
//   0x00569330 RiceFarm, 0x00569c90 Apiary: by population (> 500, > 250,
//              > 100) fewer than 6 / 4 / 2 -> 35000.
//   0x00569d20 per race the drink building (vineyard, brewery, distillery,
//              meadery): by population fewer than 6 / 4 / 2 (Roman) or
//              4 / 3 / 2 with enough farms / rice farms / apiaries ->
//              15000 / 25000 / 35000.
//   0x005693c0 Fishery (needs AiPlayerData::unk550): none -> 30000, and
//              (mines + 1) / 2 > fisheries -> 30000 (both calls can happen).
//   0x00569430 Farm: (all mines + 1) / 3 > farms -> 30000.
//   0x00569490 Mill: finished farms in the area > their consumers (donkey
//              farms, breweries, pig farms, mills) -> 25000.
//   0x005694f0 Bakery: 2 * mills > bakeries -> 20000.
//   0x00569520 PigFarm: with iron mines, finished farms > 2 * pig farms +
//              the other consumers -> 40000.
//   0x00569590 Slaughterhouse: (finished pig farms + 1) / 2 >
//              slaughterhouses -> 30000.
//   0x005695e0 Waterworks: (water consumers + 1) / 2 > waterworks -> 40000.
//   0x00569640 Barracks: (weaponsmiths + 2) / 3 > barracks, or none and
//              more than 5 swords, bows or spears in stock -> 40000.
//   0x005696d0 GoldMine: with barracks, coal and iron mines (and no gem
//              mines unless Amazon), graded by gold mines * 100 / wanted.
//   0x00569860 GoldSmelter: with iron smelters, coal, gold (mines or ore,
//              ore stored in gold_ore) and no gem mines: none -> 35000,
//              gold mines / 3 > smelters -> 20000.
//   0x00569900 Storehouse: unless the node has more storehouses than are
//              finished in the area: none yet with gold smelters, gem
//              mines or weaponsmiths -> 45000; else with a finished one and
//              at most 3 storehouse piles in the area -> 40000.
//   0x005699d0 Hospital: none, with barracks -> 20000.
//   0x00569a00 GemMine: with barracks, coal and iron mines -> 20000.
//   0x00569a30 sulfur mines: Asian 2 * cannon workshops, Amazon 2 *
//              laboratories > sulfur mines -> 20000.
//   0x00569aa0 PowderMaker (Asian): with a workshop, sulfur and coal,
//              (workshops + 1) / 2 > powder makers -> 20000.
//   0x00569b00 CharcoalBurner: more than 50 logs and less than 20 coal ->
//              20000.
//   0x00569b30 the race's small temple: more drink buildings than small
//              temples -> 35000 (Egyptian sphinx 45000).
//   0x00569c00 the race's large temple: a small one and no large one ->
//              35000.

#include "game/ai/ai_layout.h"
#include "game/enums/races.h"
#include "game/enums/buildings.h"
#include "game/enums/goods.h"
#include "game/enums/piles.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/tables.h"
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

// Callees, called through their S3.EXE addresses (verified replacements;
// the tests and emulator specs run the same callees for both sides).
constexpr std::uintptr_t kRecordBestCandidate = 0x0058ec50;  // AI::RecordBestCandidate
constexpr std::uintptr_t kCountBuildings = 0x0059dac0;       // AI_CountBuildings
constexpr std::uintptr_t kAreaGoodsAmount = 0x0059dc10;      // AI_AreaGoodsAmount

constexpr std::uint32_t kAnyPlace = 0xffffffff;  // the position arguments (-1, -1)
constexpr std::size_t kStages = 4;               // population stages 0..3

// Asks for a building type with a priority (the agent doesn't choose a place).
void Request(B type, std::uint32_t priority) {
    using Fn = std::uint32_t(__fastcall*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
    reinterpret_cast<Fn>(kRecordBestCandidate)(Value(type), priority, kAnyPlace, kAnyPlace);
}

// The computer player's finished buildings of a type in the agent's area
// (AX of AI_CountBuildings).
std::uint16_t FinishedInArea(const Agent& agent, B type) {
    using Fn = std::uint32_t(__fastcall*)(std::uint32_t player, std::uint32_t type, std::uint32_t area);
    return static_cast<std::uint16_t>(
        reinterpret_cast<Fn>(kCountBuildings)(g_aiPlayerNumber, Value(type), agent.area));
}

// Units of a goods type on the piles of the agent's area.
std::uint32_t GoodsInArea(const Agent& agent, Good good) {
    using Fn = std::uint32_t(__fastcall*)(const std::uint8_t* goods, const std::uint16_t* area);
    const std::uint8_t goods = Value(good);
    return reinterpret_cast<Fn>(kAreaGoodsAmount)(&goods, &agent.area);
}

std::uint16_t PopulationStage(std::uint16_t population) {
    for (std::uint16_t stage = 0; stage + 1 < kStages; ++stage) {
        if (population < g_aiPopulationStages[stage + 1]) {
            return stage;
        }
    }
    return kStages - 1;
}

// The CRT's _ftol (0x61fe98): truncation through a 64-bit integer, EAX is
// the low dword ("integer indefinite", i.e. 0, for NaN and out of range).
std::uint32_t Ftol(double value) {
    if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0)) {
        return 0;
    }
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(value));
}

// How many buildings of a kind the agent wants for its population (stored
// as agent.stage): wanted[stage] + (population - stage start) * slope, the
// slopes in reverse order (g_aiWanted*Slopes). The x87 code runs at the
// thread's default 53-bit precision; the product (a 17-bit integer times
// a float) is exact there and in double, so double arithmetic in the
// original order gives the same value. EAX of _ftol.
std::uint32_t WantedForPopulation(Agent& agent, const float* slopes, const std::uint8_t* wanted) {
    const std::uint16_t stage = PopulationStage(agent.population);
    agent.stage = stage;
    const std::int32_t above = agent.population - g_aiPopulationStages[stage];
    const double slope = slopes[kStages - 1 - stage];
    return Ftol(static_cast<double>(above) * slope + static_cast<double>(wanted[stage]));
}

// The priority for a ratio "have * 100 / wanted": the fewer, the higher.
std::uint32_t PriorityForRatio(std::int32_t ratio, const std::uint32_t (&priorities)[5]) {
    constexpr std::int32_t kBelow[5] = {10, 30, 50, 70, 100};
    for (std::size_t i = 0; i < 5; ++i) {
        if (ratio < kBelow[i]) {
            return priorities[i];
        }
    }
    return 0;
}

// The three population limits of 0x569330 / 0x569c90 / 0x569d20: above
// stages[4] / 4 (500), stages[3] / 2 (250) and stages[2] (100).
enum class Crowd { Small, Medium, Large, Huge };
Crowd CrowdOf(std::uint16_t population) {
    if (population > static_cast<std::uint16_t>(g_aiPopulationStages[4] >> 2)) {
        return Crowd::Huge;
    }
    if (population > static_cast<std::uint16_t>(g_aiPopulationStages[3] >> 1)) {
        return Crowd::Large;
    }
    if (population > g_aiPopulationStages[2]) {
        return Crowd::Medium;
    }
    return Crowd::Small;
}

}  // namespace

using ConditionFn = void(__fastcall*)(Agent* agent, void* edx);

void __fastcall T_ECONOMY_AGENT_ConsiderSawmill(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderForester(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderToolsmith(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderWeaponsmith(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderRiceFarm(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderFishery(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderFarm(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderMill(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderBakery(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderPigFarm(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderSlaughterhouse(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderWaterworks(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderBarracks(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderGoldMine(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderGoldSmelter(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderStorehouse(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderHospital(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderGemMine(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderSulfurMine(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderPowderMaker(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderCharcoalBurner(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderSmallTemple(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderLargeTemple(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderApiary(Agent* agent, void* edx);
void __fastcall T_ECONOMY_AGENT_ConsiderDrinkBuilding(Agent* agent, void* edx);

S3RE_HOOK(0x00568250, T_ECONOMY_AGENT_ConsiderSawmill, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderSawmill,
          T_ECONOMY_AGENT_ConsiderSawmill);
S3RE_HOOK(0x005682b0, T_ECONOMY_AGENT_ConsiderForester, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderForester,
          T_ECONOMY_AGENT_ConsiderForester);
S3RE_HOOK(0x00569160, T_ECONOMY_AGENT_ConsiderToolsmith, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderToolsmith,
          T_ECONOMY_AGENT_ConsiderToolsmith);
S3RE_HOOK(0x005691b0, T_ECONOMY_AGENT_ConsiderWeaponsmith, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderWeaponsmith,
          T_ECONOMY_AGENT_ConsiderWeaponsmith);
S3RE_HOOK(0x00569330, T_ECONOMY_AGENT_ConsiderRiceFarm, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderRiceFarm,
          T_ECONOMY_AGENT_ConsiderRiceFarm);
S3RE_HOOK(0x005693c0, T_ECONOMY_AGENT_ConsiderFishery, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderFishery,
          T_ECONOMY_AGENT_ConsiderFishery);
S3RE_HOOK(0x00569430, T_ECONOMY_AGENT_ConsiderFarm, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderFarm,
          T_ECONOMY_AGENT_ConsiderFarm);
S3RE_HOOK(0x00569490, T_ECONOMY_AGENT_ConsiderMill, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderMill,
          T_ECONOMY_AGENT_ConsiderMill);
S3RE_HOOK(0x005694f0, T_ECONOMY_AGENT_ConsiderBakery, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderBakery,
          T_ECONOMY_AGENT_ConsiderBakery);
S3RE_HOOK(0x00569520, T_ECONOMY_AGENT_ConsiderPigFarm, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderPigFarm,
          T_ECONOMY_AGENT_ConsiderPigFarm);
S3RE_HOOK(0x00569590, T_ECONOMY_AGENT_ConsiderSlaughterhouse, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderSlaughterhouse,
          T_ECONOMY_AGENT_ConsiderSlaughterhouse);
S3RE_HOOK(0x005695e0, T_ECONOMY_AGENT_ConsiderWaterworks, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderWaterworks,
          T_ECONOMY_AGENT_ConsiderWaterworks);
S3RE_HOOK(0x00569640, T_ECONOMY_AGENT_ConsiderBarracks, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderBarracks,
          T_ECONOMY_AGENT_ConsiderBarracks);
S3RE_HOOK(0x005696d0, T_ECONOMY_AGENT_ConsiderGoldMine, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderGoldMine,
          T_ECONOMY_AGENT_ConsiderGoldMine);
S3RE_HOOK(0x00569860, T_ECONOMY_AGENT_ConsiderGoldSmelter, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderGoldSmelter,
          T_ECONOMY_AGENT_ConsiderGoldSmelter);
S3RE_HOOK(0x00569900, T_ECONOMY_AGENT_ConsiderStorehouse, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderStorehouse,
          T_ECONOMY_AGENT_ConsiderStorehouse);
S3RE_HOOK(0x005699d0, T_ECONOMY_AGENT_ConsiderHospital, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderHospital,
          T_ECONOMY_AGENT_ConsiderHospital);
S3RE_HOOK(0x00569a00, T_ECONOMY_AGENT_ConsiderGemMine, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderGemMine,
          T_ECONOMY_AGENT_ConsiderGemMine);
S3RE_HOOK(0x00569a30, T_ECONOMY_AGENT_ConsiderSulfurMine, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderSulfurMine,
          T_ECONOMY_AGENT_ConsiderSulfurMine);
S3RE_HOOK(0x00569aa0, T_ECONOMY_AGENT_ConsiderPowderMaker, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderPowderMaker,
          T_ECONOMY_AGENT_ConsiderPowderMaker);
S3RE_HOOK(0x00569b00, T_ECONOMY_AGENT_ConsiderCharcoalBurner, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderCharcoalBurner,
          T_ECONOMY_AGENT_ConsiderCharcoalBurner);
S3RE_HOOK(0x00569b30, T_ECONOMY_AGENT_ConsiderSmallTemple, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderSmallTemple,
          T_ECONOMY_AGENT_ConsiderSmallTemple);
S3RE_HOOK(0x00569c00, T_ECONOMY_AGENT_ConsiderLargeTemple, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderLargeTemple,
          T_ECONOMY_AGENT_ConsiderLargeTemple);
S3RE_HOOK(0x00569c90, T_ECONOMY_AGENT_ConsiderApiary, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderApiary,
          T_ECONOMY_AGENT_ConsiderApiary);
S3RE_HOOK(0x00569d20, T_ECONOMY_AGENT_ConsiderDrinkBuilding, ConditionFn, Original_T_ECONOMY_AGENT_ConsiderDrinkBuilding,
          T_ECONOMY_AGENT_ConsiderDrinkBuilding);

// Purpose: Asks for a sawmill when the area has none or too few for its
// woodcutters (one per three).
void __fastcall T_ECONOMY_AGENT_ConsiderSawmill(Agent* agent, void* /*edx*/) {
    if (agent->sawmills == 0) {
        Request(B::Sawmill, 65000);
        return;
    }
    if ((agent->woodcutters + 2) / 3 - agent->sawmills > 0) {
        Request(B::Sawmill, g_aiBuildPriorityCaps[Value(B::Sawmill)]);
    }
}

// Purpose: Asks for a forester, the more urgently the more the woodcutters
// outnumber the foresters (and the less wood is in stock).
void __fastcall T_ECONOMY_AGENT_ConsiderForester(Agent* agent, void* /*edx*/) {
    const std::int32_t woodcutters = agent->woodcutters;
    const std::int32_t foresters = agent->foresters;
    std::int32_t surplus;
    switch (static_cast<Race>(agent->race)) {
    case Race::Roman: surplus = woodcutters * 100 - foresters * 250; break;
    case Race::Egyptian: surplus = (woodcutters - 3 * foresters) * 100; break;
    case Race::Asian:
    case Race::Amazon: surplus = (woodcutters - 2 * foresters) * 100; break;
    default:
        // Quirk: past the jump table the original reads its stack slot that
        // holds the pushed ECX, i.e. it grades the agent's address.
        surplus = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(agent));
        break;
    }
    if (static_cast<std::uint32_t>(agent->race) <= 3) {
        surplus -= agent->boards;
        surplus -= agent->logs;
    }
    const std::uint32_t cap = g_aiBuildPriorityCaps[Value(B::Forester)];
    std::uint32_t priority = 0;
    if (surplus > 80) {
        priority = cap;
    } else if (surplus > 60) {
        priority = cap / 2;
    } else if (surplus > 40) {
        priority = cap / 3;
    } else if (surplus > 20) {
        priority = cap / 4;
    } else if (surplus > 0) {
        priority = cap / 6;
    }
    agent->forester_priority = priority;
    Request(B::Forester, priority);
}

// Purpose: Asks for the first toolsmith once coal and iron are available
// (and none is already being built).
void __fastcall T_ECONOMY_AGENT_ConsiderToolsmith(Agent* agent, void* /*edx*/) {
    if (g_aiPlayerData.get()->best[kOrderedCandidates + Value(B::Toolsmith)].score != 0 || agent->toolsmiths != 0) {
        return;
    }
    if (agent->coal_mines == 0 && agent->coal == 0) {
        return;
    }
    if (agent->iron_smelters == 0 && agent->iron == 0) {
        return;
    }
    Request(B::Toolsmith, 35000);
}

// Purpose: Asks for weaponsmiths: the first once coal, iron and a
// toolsmith are there, more as the population grows.
void __fastcall T_ECONOMY_AGENT_ConsiderWeaponsmith(Agent* agent, void* /*edx*/) {
    if (agent->coal_mines == 0 && agent->coal == 0) {
        return;
    }
    if (agent->iron_smelters == 0 && agent->iron == 0) {
        return;
    }
    if (FinishedInArea(*agent, B::Toolsmith) == 0) {
        return;
    }
    const std::uint16_t weaponsmiths = agent->weaponsmiths;
    if (weaponsmiths == 0) {
        Request(B::Weaponsmith, 30000);
        return;
    }
    if (agent->iron <= 4 || agent->coal <= 10) {
        return;
    }
    const auto wanted = static_cast<std::int16_t>(
        WantedForPopulation(*agent, g_aiWantedWeaponsmithSlopes.get(), g_aiWantedWeaponsmiths.get()));
    std::uint32_t priority = 0;
    if (wanted - 1 > 0) {
        const std::int32_t ratio = weaponsmiths * 100 / wanted;
        agent->stock_ratio = ratio;
        priority = PriorityForRatio(ratio, {40000, 20000, 10000, 5000, 2000});
    }
    Request(B::Weaponsmith, priority);
}

namespace {

// Up to `limits` buildings by the population (0x569330 / 0x569c90 /
// 0x569d20): fewer than limits[0] above 500 settlers, [1] above 250, [2]
// above 100; with `enough` the supply the building needs.
struct CrowdRule {
    std::uint16_t limits[3];
    std::uint32_t priorities[3];
};

bool WantsMore(Crowd crowd, std::uint16_t have, const std::uint16_t (&limits)[3], std::size_t* level) {
    switch (crowd) {
    case Crowd::Huge: *level = 0; break;
    case Crowd::Large: *level = 1; break;
    case Crowd::Medium: *level = 2; break;
    default: return false;
    }
    return have < limits[*level];
}

}  // namespace

// Purpose: Asks for rice farms (Asian) as the population grows: up to 6.
void __fastcall T_ECONOMY_AGENT_ConsiderRiceFarm(Agent* agent, void* /*edx*/) {
    std::size_t level = 0;
    if (WantsMore(CrowdOf(agent->population), agent->rice_farms, {6, 4, 2}, &level)) {
        Request(B::RiceFarm, 35000);
    }
}

// Purpose: Asks for apiaries (Amazon) as the population grows: up to 6.
void __fastcall T_ECONOMY_AGENT_ConsiderApiary(Agent* agent, void* /*edx*/) {
    std::size_t level = 0;
    if (WantsMore(CrowdOf(agent->population), agent->apiaries, {6, 4, 2}, &level)) {
        Request(B::Apiary, 35000);
    }
}

// Purpose: Asks for the race's drink building (wine, beer, rice liquor,
// mead; the offerings for the temples) as the population grows, when the
// farms that feed it are there.
void __fastcall T_ECONOMY_AGENT_ConsiderDrinkBuilding(Agent* agent, void* /*edx*/) {
    constexpr std::uint32_t kPriorities[3] = {15000, 25000, 35000};
    const Crowd crowd = CrowdOf(agent->population);
    std::size_t level = 0;
    switch (static_cast<Race>(agent->race)) {
    case Race::Roman:
        if (WantsMore(crowd, agent->vineyards, {6, 4, 2}, &level)) {
            Request(B::Vineyard, kPriorities[level]);
        }
        return;
    case Race::Egyptian: {
        // Farms needed: more than 6 / 4 / 2.
        constexpr std::uint16_t kFarms[3] = {6, 4, 2};
        if (WantsMore(crowd, agent->breweries, {4, 3, 2}, &level) && agent->farms > kFarms[level]) {
            Request(B::Brewery, kPriorities[level]);
        }
        return;
    }
    case Race::Asian: {
        constexpr std::uint16_t kRiceFarms[3] = {4, 2, 0};
        if (WantsMore(crowd, agent->distilleries, {4, 3, 2}, &level) && agent->rice_farms > kRiceFarms[level]) {
            Request(B::Distillery, kPriorities[level]);
        }
        return;
    }
    case Race::Amazon: {
        constexpr std::uint16_t kApiaries[3] = {4, 2, 0};
        if (WantsMore(crowd, agent->meaderies, {4, 3, 2}, &level) && agent->apiaries > kApiaries[level]) {
            Request(B::Meadery, kPriorities[level]);
        }
        return;
    }
    }
}

// Purpose: Asks for fisheries (food for the mines): one at least, then one
// per two mines.
void __fastcall T_ECONOMY_AGENT_ConsiderFishery(Agent* agent, void* /*edx*/) {
    if (g_aiPlayerData.get()->unk550 == 0) {
        return;
    }
    if (agent->fisheries == 0) {
        Request(B::Fishery, 30000);
    }
    const std::int32_t mines = agent->gold_mines + agent->iron_mines + agent->coal_mines;
    if ((mines + 1) / 2 - agent->fisheries > 0) {
        Request(B::Fishery, 30000);
    }
}

// Purpose: Asks for farms (grain for the mines' food): one per three mines.
void __fastcall T_ECONOMY_AGENT_ConsiderFarm(Agent* agent, void* /*edx*/) {
    const std::int32_t mines =
        agent->gem_mines + agent->sulfur_mines + agent->gold_mines + agent->iron_mines + agent->coal_mines;
    if ((mines + 1) / 3 - agent->farms > 0) {
        Request(B::Farm, 30000);
    }
}

// Purpose: Asks for a mill when the area's farms have grain left over
// (more finished farms than buildings that use grain).
void __fastcall T_ECONOMY_AGENT_ConsiderMill(Agent* agent, void* /*edx*/) {
    const std::int32_t farms = FinishedInArea(*agent, B::Farm);
    if (farms - agent->donkey_farms - agent->breweries - agent->pig_farms - agent->mills > 0) {
        Request(B::Mill, 25000);
    }
}

// Purpose: Asks for bakeries: two per mill.
void __fastcall T_ECONOMY_AGENT_ConsiderBakery(Agent* agent, void* /*edx*/) {
    if (2 * agent->mills - agent->bakeries > 0) {
        Request(B::Bakery, 20000);
    }
}

// Purpose: Asks for a pig farm when there are iron mines to feed and the
// area's farms have grain left over (a pig farm counts twice).
void __fastcall T_ECONOMY_AGENT_ConsiderPigFarm(Agent* agent, void* /*edx*/) {
    if (agent->iron_mines == 0) {
        return;
    }
    const std::int32_t farms = FinishedInArea(*agent, B::Farm);
    if (farms - 2 * agent->pig_farms - agent->donkey_farms - agent->breweries - agent->mills > 0) {
        Request(B::PigFarm, 40000);
    }
}

// Purpose: Asks for slaughterhouses: one per two finished pig farms.
void __fastcall T_ECONOMY_AGENT_ConsiderSlaughterhouse(Agent* agent, void* /*edx*/) {
    const std::int32_t pigFarms = FinishedInArea(*agent, B::PigFarm);
    if ((pigFarms + 1) / 2 - agent->slaughterhouses > 0) {
        Request(B::Slaughterhouse, 30000);
    }
}

// Purpose: Asks for waterworks: one per two buildings that use water.
void __fastcall T_ECONOMY_AGENT_ConsiderWaterworks(Agent* agent, void* /*edx*/) {
    const std::int32_t users =
        agent->meaderies + agent->donkey_farms + agent->breweries + agent->pig_farms + agent->bakeries;
    if ((users + 1) / 2 - agent->waterworks > 0) {
        Request(B::Waterworks, 40000);
    }
}

// Purpose: Asks for barracks: one per three weaponsmiths, or the first one
// once more than 5 weapons of a kind lie in stock.
void __fastcall T_ECONOMY_AGENT_ConsiderBarracks(Agent* agent, void* /*edx*/) {
    constexpr std::uint32_t kWeaponsInStock = 5;
    if ((agent->weaponsmiths + 2) / 3 - agent->barracks <= 0) {
        if (agent->barracks != 0) {
            return;
        }
        if (GoodsInArea(*agent, Good::Sword) <= kWeaponsInStock && GoodsInArea(*agent, Good::Bow) <= kWeaponsInStock &&
            GoodsInArea(*agent, Good::Spear) <= kWeaponsInStock) {
            return;
        }
    }
    Request(B::Barracks, 40000);
}

// Purpose: Asks for gold mines once the military economy runs (barracks,
// coal and iron mines), more as the population grows.
void __fastcall T_ECONOMY_AGENT_ConsiderGoldMine(Agent* agent, void* /*edx*/) {
    if (agent->barracks == 0 || agent->coal_mines == 0 || agent->iron_mines == 0) {
        return;
    }
    if (static_cast<Race>(agent->race) != Race::Amazon && agent->gem_mines != 0) {
        return;
    }
    const auto wanted = static_cast<std::uint16_t>(
        WantedForPopulation(*agent, g_aiWantedGoldMineSlopes.get(), g_aiWantedGoldMines.get()));
    std::uint32_t priority = 0;
    if (wanted != 0) {
        const std::int32_t ratio = agent->gold_mines * 100 / wanted;
        agent->stock_ratio = ratio;
        priority = PriorityForRatio(ratio, {10000, 5000, 2000, 1000, 500});
    }
    Request(B::GoldMine, priority);
}

// Purpose: Asks for gold smelters when iron smelting runs and coal and gold
// are there: the first, then one per three gold mines.
void __fastcall T_ECONOMY_AGENT_ConsiderGoldSmelter(Agent* agent, void* /*edx*/) {
    if (agent->iron_smelters == 0) {
        return;
    }
    const auto ore = static_cast<std::uint16_t>(GoodsInArea(*agent, Good::GoldOre));
    agent->gold_ore = ore;
    if (agent->coal_mines == 0 && agent->coal == 0) {
        return;
    }
    if (agent->gold_mines == 0 && ore == 0) {
        return;
    }
    if (agent->gem_mines != 0) {
        return;
    }
    if (agent->gold_smelters == 0) {
        Request(B::GoldSmelter, 35000);
    } else if (agent->gold_mines / 3 - agent->gold_smelters > 0) {
        Request(B::GoldSmelter, 20000);
    }
}

// Purpose: Asks for a storehouse: the first one once the economy has
// valuable goods (gold, gems, weapons), another when the area's
// storehouses have few piles.
void __fastcall T_ECONOMY_AGENT_ConsiderStorehouse(Agent* agent, void* /*edx*/) {
    constexpr std::uint16_t kFewPiles = 3;
    const std::uint16_t storehouses = g_aiPlayerData.get()->building_counts[Value(B::Storehouse)];
    const std::uint16_t finished = FinishedInArea(*agent, B::Storehouse);
    if (storehouses > finished) {
        return;  // one is still being built
    }
    if ((agent->gold_smelters != 0 || agent->gem_mines != 0 || agent->weaponsmiths != 0) && storehouses == 0) {
        Request(B::Storehouse, 45000);
        return;
    }
    if (finished == 0) {
        return;
    }
    // The storehouse piles among the area's piles (0x569960..0x5699b0).
    const CGameData* world = g_aiWorld;
    std::uint16_t piles = 0;
    for (std::uint16_t pile = world->areas[agent->area].pile_list; pile != 0; pile = world->piles[pile].next) {
        if (world->piles[pile].art == Value(PileKind::Storehouse)) {
            ++piles;
        }
    }
    if (piles <= kFewPiles) {
        Request(B::Storehouse, 40000);
    }
}

// Purpose: Asks for a hospital once there are barracks.
void __fastcall T_ECONOMY_AGENT_ConsiderHospital(Agent* agent, void* /*edx*/) {
    if (agent->hospitals == 0 && agent->barracks != 0) {
        Request(B::Hospital, 20000);
    }
}

// Purpose: Asks for a gem mine once barracks, coal and iron mines are
// there.
void __fastcall T_ECONOMY_AGENT_ConsiderGemMine(Agent* agent, void* /*edx*/) {
    if (agent->barracks != 0 && agent->coal_mines != 0 && agent->iron_mines != 0) {
        Request(B::GemMine, 20000);
    }
}

// Purpose: Asks for sulfur mines (Asian: two per cannon workshop; Amazon:
// two per laboratory).
void __fastcall T_ECONOMY_AGENT_ConsiderSulfurMine(Agent* agent, void* /*edx*/) {
    switch (static_cast<Race>(agent->race)) {
    case Race::Asian:
        if (2 * agent->war_workshops - agent->sulfur_mines > 0) {
            Request(B::SulfurMine, 20000);
        }
        return;
    case Race::Amazon:
        if (2 * agent->laboratories - agent->sulfur_mines > 0) {
            Request(B::AmazonSulfurMine, 20000);
        }
        return;
    default: return;
    }
}

// Purpose: Asks for powder makers (Asian; gunpowder for the cannons): one
// per two cannon workshops, when sulfur and coal are there.
void __fastcall T_ECONOMY_AGENT_ConsiderPowderMaker(Agent* agent, void* /*edx*/) {
    if (static_cast<Race>(agent->race) != Race::Asian) {
        return;
    }
    const std::uint16_t workshops = agent->war_workshops;
    if (workshops == 0) {
        return;
    }
    if (agent->sulfur_mines == 0 && agent->sulfur == 0) {
        return;
    }
    if (agent->coal_mines == 0 && agent->coal == 0) {
        return;
    }
    if ((workshops + 1) / 2 - agent->powder_makers > 0) {
        Request(B::PowderMaker, 20000);
    }
}

// Purpose: Asks for a charcoal burner when logs pile up and coal is short.
void __fastcall T_ECONOMY_AGENT_ConsiderCharcoalBurner(Agent* agent, void* /*edx*/) {
    constexpr std::uint16_t kManyLogs = 50;
    constexpr std::uint16_t kLittleCoal = 20;
    if (agent->logs > kManyLogs && agent->coal < kLittleCoal) {
        Request(B::CharcoalBurner, 20000);
    }
}

// Purpose: Asks for the race's small temple: one per drink building (whose
// drink is its offering).
void __fastcall T_ECONOMY_AGENT_ConsiderSmallTemple(Agent* agent, void* /*edx*/) {
    const std::int32_t temples = agent->small_temples;
    switch (static_cast<Race>(agent->race)) {
    case Race::Roman:
        if (agent->vineyards - temples > 0) {
            Request(B::SmallTemple, 35000);
        }
        return;
    case Race::Egyptian:
        if (agent->breweries - temples > 0) {
            Request(B::Sphinx, 45000);
        }
        return;
    case Race::Asian:
        if (agent->distilleries - temples > 0) {
            Request(B::SmallPagoda, 35000);
        }
        return;
    case Race::Amazon:
        if (agent->meaderies - temples > 0) {
            Request(B::AmazonSmallTemple, 35000);
        }
        return;
    }
}

// Purpose: Asks for the race's large temple once a small one stands.
void __fastcall T_ECONOMY_AGENT_ConsiderLargeTemple(Agent* agent, void* /*edx*/) {
    if (agent->small_temples == 0 || agent->large_temples != 0) {
        return;
    }
    switch (static_cast<Race>(agent->race)) {
    case Race::Roman: Request(B::LargeTemple, 35000); return;
    case Race::Egyptian: Request(B::Pyramid, 35000); return;
    case Race::Asian: Request(B::LargePagoda, 35000); return;
    case Race::Amazon: Request(B::AmazonLargeTemple, 35000); return;
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Both sides get the same random agent (0xa0 bytes plus guard bytes), the
// same random AiPlayerData (g_aiPlayerData) and a world pair (g_aiWorld)
// with a few buildings for AI_CountBuildings and short pile lists in the
// agent's area for AI_AreaGoodsAmount and the storehouse count. The
// callees are the hooked S3.EXE addresses for both sides.

namespace {

constexpr std::size_t kConditions = 25;
constexpr std::size_t kAgentBytes = sizeof(Agent) + 16;
constexpr std::size_t kDataBytes = 0x600;
constexpr std::uint32_t kMapSize = 400;

struct Conditions {
    ConditionFn fn[kConditions];
};

Conditions OriginalConditions() {
    return {{Original_T_ECONOMY_AGENT_ConsiderSawmill,       Original_T_ECONOMY_AGENT_ConsiderForester,
             Original_T_ECONOMY_AGENT_ConsiderToolsmith,     Original_T_ECONOMY_AGENT_ConsiderWeaponsmith,
             Original_T_ECONOMY_AGENT_ConsiderRiceFarm,      Original_T_ECONOMY_AGENT_ConsiderFishery,
             Original_T_ECONOMY_AGENT_ConsiderFarm,          Original_T_ECONOMY_AGENT_ConsiderMill,
             Original_T_ECONOMY_AGENT_ConsiderBakery,        Original_T_ECONOMY_AGENT_ConsiderPigFarm,
             Original_T_ECONOMY_AGENT_ConsiderSlaughterhouse, Original_T_ECONOMY_AGENT_ConsiderWaterworks,
             Original_T_ECONOMY_AGENT_ConsiderBarracks,      Original_T_ECONOMY_AGENT_ConsiderGoldMine,
             Original_T_ECONOMY_AGENT_ConsiderGoldSmelter,   Original_T_ECONOMY_AGENT_ConsiderStorehouse,
             Original_T_ECONOMY_AGENT_ConsiderHospital,      Original_T_ECONOMY_AGENT_ConsiderGemMine,
             Original_T_ECONOMY_AGENT_ConsiderSulfurMine,    Original_T_ECONOMY_AGENT_ConsiderPowderMaker,
             Original_T_ECONOMY_AGENT_ConsiderCharcoalBurner, Original_T_ECONOMY_AGENT_ConsiderSmallTemple,
             Original_T_ECONOMY_AGENT_ConsiderLargeTemple,   Original_T_ECONOMY_AGENT_ConsiderApiary,
             Original_T_ECONOMY_AGENT_ConsiderDrinkBuilding}};
}

Conditions OurConditions() {
    return {{&T_ECONOMY_AGENT_ConsiderSawmill,       &T_ECONOMY_AGENT_ConsiderForester,
             &T_ECONOMY_AGENT_ConsiderToolsmith,     &T_ECONOMY_AGENT_ConsiderWeaponsmith,
             &T_ECONOMY_AGENT_ConsiderRiceFarm,      &T_ECONOMY_AGENT_ConsiderFishery,
             &T_ECONOMY_AGENT_ConsiderFarm,          &T_ECONOMY_AGENT_ConsiderMill,
             &T_ECONOMY_AGENT_ConsiderBakery,        &T_ECONOMY_AGENT_ConsiderPigFarm,
             &T_ECONOMY_AGENT_ConsiderSlaughterhouse, &T_ECONOMY_AGENT_ConsiderWaterworks,
             &T_ECONOMY_AGENT_ConsiderBarracks,      &T_ECONOMY_AGENT_ConsiderGoldMine,
             &T_ECONOMY_AGENT_ConsiderGoldSmelter,   &T_ECONOMY_AGENT_ConsiderStorehouse,
             &T_ECONOMY_AGENT_ConsiderHospital,      &T_ECONOMY_AGENT_ConsiderGemMine,
             &T_ECONOMY_AGENT_ConsiderSulfurMine,    &T_ECONOMY_AGENT_ConsiderPowderMaker,
             &T_ECONOMY_AGENT_ConsiderCharcoalBurner, &T_ECONOMY_AGENT_ConsiderSmallTemple,
             &T_ECONOMY_AGENT_ConsiderLargeTemple,   &T_ECONOMY_AGENT_ConsiderApiary,
             &T_ECONOMY_AGENT_ConsiderDrinkBuilding}};
}

struct SavedState {
    CGameData* world = g_aiWorld;
    AiPlayerData* data = g_aiPlayerData;
    std::uint8_t number = g_aiPlayerNumber;
    std::uint16_t sizeX = g_aiMapSizeX, sizeY = g_aiMapSizeY;
    float weaponsmithSlopes[kStages];
    float goldMineSlopes[kStages];
    SavedState() {
        std::memcpy(weaponsmithSlopes, g_aiWantedWeaponsmithSlopes.get(), sizeof(weaponsmithSlopes));
        std::memcpy(goldMineSlopes, g_aiWantedGoldMineSlopes.get(), sizeof(goldMineSlopes));
    }
    ~SavedState() {
        g_aiWorld = world;
        g_aiPlayerData = data;
        g_aiPlayerNumber = number;
        g_aiMapSizeX = sizeX;
        g_aiMapSizeY = sizeY;
        std::memcpy(g_aiWantedWeaponsmithSlopes.get(), weaponsmithSlopes, sizeof(weaponsmithSlopes));
        std::memcpy(g_aiWantedGoldMineSlopes.get(), goldMineSlopes, sizeof(goldMineSlopes));
    }
};

std::uint16_t SmallCount(test::Rng& rng) {
    if (rng.Chance(1, 3)) {
        return 0;
    }
    return static_cast<std::uint16_t>(rng.Chance(1, 20) ? rng.Next32() : rng.Below(9));
}

// A chain of `length` distinct piles starting at pile ids from `base` (each
// list its own range, as the lists share Pile::next); returns the head.
std::uint16_t BuildChain(test::Context& ctx, test::WorldPair& worlds, std::uint32_t base, int length,
                         bool storehouseKinds) {
    std::uint16_t next = 0;
    const std::uint32_t stride = 1 + ctx.rng().Below(3);
    for (int k = 0; k < length; ++k) {
        const auto pile = static_cast<std::uint16_t>(base + static_cast<std::uint32_t>(k) * stride);
        const auto amount = static_cast<std::uint8_t>(ctx.rng().Below(5));
        const auto art = static_cast<std::uint8_t>(storehouseKinds && ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Below(256));
        worlds.Both([&](CGameData* w) {
            w->piles[pile].next = next;
            w->piles[pile].amount = amount;
            w->piles[pile].art = art;
        });
        next = pile;
    }
    return next;
}

constexpr std::uint16_t kAreas = 4;  // the agent's area is one of 0..3

void SetUpWorld(test::Context& ctx, test::WorldPair& worlds) {
    // Buildings for AI_CountBuildings: players 0..2, the types the
    // conditions count, on the AI's 400 x 400 map.
    constexpr std::uint8_t kTypes[] = {Value(B::Toolsmith), Value(B::Farm), Value(B::PigFarm), Value(B::Storehouse)};
    const std::uint32_t count = ctx.rng().Below(40);
    worlds.Both([&](CGameData* w) { w->building_high_water = count; });
    for (std::uint32_t id = 1; id <= count; ++id) {
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Chance(1, 6) ? 0xff : ctx.rng().Below(3));
        const std::uint8_t art = kTypes[ctx.rng().Below(4)];
        const auto buildup = static_cast<std::uint8_t>(ctx.rng().Chance(1, 4));
        const auto x = static_cast<std::uint16_t>(ctx.rng().Below(kMapSize));
        const auto y = static_cast<std::uint16_t>(ctx.rng().Below(kMapSize));
        const auto tileArea = static_cast<std::uint16_t>(ctx.rng().Below(kAreas));
        worlds.Both([&](CGameData* w) {
            layout::Building& record = w->buildings[id];
            record.owner = owner;
            record.art = art;
            record.buildup = buildup;
            record.door_x = x;
            record.door_y = y;
            w->tiles[x][y].area = tileArea;
        });
    }
    // Pile lists of each area: goods 6 and 18..20, and the area's own list.
    constexpr Good kGoods[] = {Good::GoldOre, Good::Sword, Good::Bow, Good::Spear};
    std::uint32_t base = 1 + ctx.rng().Below(1000);
    for (std::uint16_t area = 0; area < kAreas; ++area) {
        for (Good good : kGoods) {
            const std::uint16_t head = BuildChain(ctx, worlds, base, static_cast<int>(ctx.rng().Below(9)), false);
            worlds.Both([&](CGameData* w) { w->areas[area].piles_by_goods[Value(good)] = head; });
            base += 40;
        }
        const std::uint16_t head = BuildChain(ctx, worlds, base, static_cast<int>(ctx.rng().Below(8)), true);
        worlds.Both([&](CGameData* w) { w->areas[area].pile_list = head; });
        base += 40;
    }
}

void RandomAgent(test::Context& ctx, std::uint8_t* bytes, std::uint16_t area) {
    ctx.rng().Fill(bytes, kAgentBytes);
    auto* agent = reinterpret_cast<Agent*>(bytes);
    agent->race = ctx.rng().Chance(1, 12) ? static_cast<std::int32_t>(ctx.rng().Next32())
                                          : static_cast<std::int32_t>(ctx.rng().Below(4));
    agent->area = area;
    // Counts and stocks: mostly small, often 0.
    for (std::size_t offset = offsetof(Agent, logs); offset < sizeof(Agent); offset += 2) {
        const std::uint16_t value = SmallCount(ctx.rng());
        std::memcpy(bytes + offset, &value, 2);
    }
    if (ctx.rng().Chance(1, 4)) {
        agent->logs = static_cast<std::uint16_t>(ctx.rng().Below(100));
        agent->coal = static_cast<std::uint16_t>(ctx.rng().Below(40));
        agent->iron = static_cast<std::uint16_t>(ctx.rng().Below(12));
    }
    agent->population = static_cast<std::uint16_t>(ctx.rng().Chance(1, 10) ? ctx.rng().Next32()
                                                                            : ctx.rng().Below(2500));
}

void RandomSlopes(test::Context& ctx) {
    float* slopes[2] = {g_aiWantedWeaponsmithSlopes.get(), g_aiWantedGoldMineSlopes.get()};
    for (float* table : slopes) {
        for (std::size_t s = 0; s < kStages; ++s) {
            if (ctx.rng().Chance(1, 10)) {
                const std::uint32_t bits = ctx.rng().Next32();
                std::memcpy(&table[s], &bits, 4);
            } else {
                table[s] = static_cast<float>(ctx.rng().Below(1000)) / 997.0f;
            }
        }
    }
}

void CompareConditions(test::Context& ctx, const Conditions& candidate, int cases) {
    const Conditions original = OriginalConditions();
    SavedState saved;
    g_aiMapSizeX = static_cast<std::uint16_t>(kMapSize);
    g_aiMapSizeY = static_cast<std::uint16_t>(kMapSize);
    test::WorldPair worlds(ctx, sizeof(CGameData));
    std::vector<std::uint8_t> agentA(kAgentBytes), agentB(kAgentBytes), dataA(kDataBytes), dataB(kDataBytes);
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        const auto op = static_cast<std::size_t>(ctx.rng().Below(kConditions));
        const auto area = static_cast<std::uint16_t>(ctx.rng().Below(kAreas));
        if (i % 16 == 0) {
            SetUpWorld(ctx, worlds);
        }
        RandomAgent(ctx, agentA.data(), area);
        agentB = agentA;
        ctx.rng().Fill(dataA.data(), dataA.size());
        auto* data = reinterpret_cast<AiPlayerData*>(dataA.data());
        if (ctx.rng().Chance(1, 2)) {
            data->best[kOrderedCandidates + Value(B::Toolsmith)].score = 0;
        }
        if (ctx.rng().Chance(1, 4)) {
            data->unk550 = 0;
        }
        data->building_counts[Value(B::Storehouse)] = static_cast<std::uint16_t>(ctx.rng().Below(3));
        dataB = dataA;
        g_aiPlayerNumber = static_cast<std::uint8_t>(ctx.rng().Below(3));
        RandomSlopes(ctx);

        g_aiWorld = worlds.original();
        g_aiPlayerData = reinterpret_cast<AiPlayerData*>(dataA.data());
        original.fn[op](reinterpret_cast<Agent*>(agentA.data()), nullptr);
        g_aiWorld = worlds.ours();
        g_aiPlayerData = reinterpret_cast<AiPlayerData*>(dataB.data());
        candidate.fn[op](reinterpret_cast<Agent*>(agentB.data()), nullptr);

        bool same = S3RE_EXPECT_MEM_EQ(agentA.data(), agentB.data(), agentA.size());
        same &= S3RE_EXPECT_MEM_EQ(dataA.data(), dataB.data(), dataA.size());
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, condition %u", i, static_cast<unsigned>(op));
        }
    }
}

void __fastcall WrongForester(Agent* agent, void* /*edx*/) {
    // Wrong: ignores the wood in stock.
    const std::uint16_t boards = agent->boards, logs = agent->logs;
    agent->boards = 0;
    agent->logs = 0;
    T_ECONOMY_AGENT_ConsiderForester(agent, nullptr);
    agent->boards = boards;
    agent->logs = logs;
}

void __fastcall WrongWeaponsmith(Agent* agent, void* edx) {
    // Wrong: the stages in forward order.
    float* slopes = g_aiWantedWeaponsmithSlopes.get();
    std::swap(slopes[0], slopes[3]);
    std::swap(slopes[1], slopes[2]);
    T_ECONOMY_AGENT_ConsiderWeaponsmith(agent, edx);
    std::swap(slopes[0], slopes[3]);
    std::swap(slopes[1], slopes[2]);
}

void __fastcall WrongStorehouse(Agent* agent, void* /*edx*/) {
    // Wrong: the second storehouse already at 4 piles.
    const std::uint16_t storehouses = g_aiPlayerData.get()->building_counts[Value(B::Storehouse)];
    const std::uint16_t finished = FinishedInArea(*agent, B::Storehouse);
    if (storehouses > finished || finished == 0 ||
        ((agent->gold_smelters != 0 || agent->gem_mines != 0 || agent->weaponsmiths != 0) && storehouses == 0)) {
        T_ECONOMY_AGENT_ConsiderStorehouse(agent, nullptr);
        return;
    }
    const CGameData* world = g_aiWorld;
    std::uint16_t piles = 0;
    for (std::uint16_t pile = world->areas[agent->area].pile_list; pile != 0; pile = world->piles[pile].next) {
        piles += world->piles[pile].art == 0 ? 1 : 0;
    }
    if (piles <= 4) {
        Request(B::Storehouse, 40000);
    }
}

void __fastcall WrongDrink(Agent* agent, void* /*edx*/) {
    // Wrong: the Egyptian breweries need only 6 farms at the top.
    if (static_cast<Race>(agent->race) == Race::Egyptian && CrowdOf(agent->population) == Crowd::Huge &&
        agent->breweries < 4 && agent->farms == 6) {
        Request(B::Brewery, 15000);
        return;
    }
    T_ECONOMY_AGENT_ConsiderDrinkBuilding(agent, nullptr);
}

}  // namespace

S3RE_TEST(AiEconomyConditions_MatchOriginal) {
    CompareConditions(ctx, OurConditions(), 40000);
}

S3RE_TEST(AiEconomyConditions_ComparisonDetectsWrongImplementation) {
    const auto probe = [&](std::size_t index, ConditionFn wrong) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        Conditions conditions = OurConditions();
        conditions.fn[index] = wrong;
        CompareConditions(quiet, conditions, 20000);
        S3RE_EXPECT(quiet.failures() > 0);
    };
    probe(1, &WrongForester);
    probe(3, &WrongWeaponsmith);
    probe(15, &WrongStorehouse);
    probe(24, &WrongDrink);
    probe(21, &T_ECONOMY_AGENT_ConsiderLargeTemple);  // swapped small and large temple
}

}  // namespace s3re::game
