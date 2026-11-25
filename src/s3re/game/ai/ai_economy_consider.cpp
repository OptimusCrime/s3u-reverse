// The T_ECONOMY_AGENT's round of building conditions (Rainer\
// CompDoBuildHouseReq.cpp; re/computer_players.md "The economy agent"; the
// conditions themselves are in ai_economy_conditions.cpp).
//
//   0x0056a1a0 T_ECONOMY_AGENT::ConsiderBuildings() thiscall: for each
//              entry of a fixed list (kCommonConditions, then the list of
//              the agent's race, +8; a race above 3, compared unsigned,
//              has none) calls the entry's condition (thiscall on the
//              agent, no arguments) unless one of its building types has a
//              running countdown (g_aiPlayerData->candidate_values[type]
//              != 0) or is blocked (type_blocked[type] != 0); the
//              residences' entry needs all three types (26, 27, 28) free.
//              g_aiPlayerData is re-read after every call. Some entries
//              call NoOp 0x59f200 (types without a condition: the donkey
//              farm, marketplace, shipyard, harbour, the war workshops,
//              the Asian / Amazon / Egyptian temples' second slots, ...).
//              EAX: left over (Run 0x567890 doesn't use it).

#include "game/ai/ai_layout.h"
#include "game/enums/buildings.h"
#include "game/enums/races.h"
#include "game/globals.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace s3re::game {

using namespace ai;

namespace {

using B = BuildingType;

// The conditions (thiscall on the agent), by their S3.EXE addresses.
constexpr std::uintptr_t kWoodcutter = 0x00567e00;
constexpr std::uintptr_t kStonecutter = 0x00568420;
constexpr std::uintptr_t kSawmill = 0x00568250;
constexpr std::uintptr_t kForester = 0x005682b0;
constexpr std::uintptr_t kResidences = 0x00568890;
constexpr std::uintptr_t kCoalMine = 0x00568bc0;
constexpr std::uintptr_t kIronMine = 0x00568d80;
constexpr std::uintptr_t kIronSmelter = 0x00568f40;
constexpr std::uintptr_t kToolsmith = 0x00569160;
constexpr std::uintptr_t kWeaponsmith = 0x005691b0;
constexpr std::uintptr_t kFishery = 0x005693c0;
constexpr std::uintptr_t kFarm = 0x00569430;
constexpr std::uintptr_t kMill = 0x00569490;
constexpr std::uintptr_t kBakery = 0x005694f0;
constexpr std::uintptr_t kWaterworks = 0x005695e0;
constexpr std::uintptr_t kPigFarm = 0x00569520;
constexpr std::uintptr_t kSlaughterhouse = 0x00569590;
constexpr std::uintptr_t kBarracks = 0x00569640;
constexpr std::uintptr_t kGoldMine = 0x005696d0;
constexpr std::uintptr_t kGoldSmelter = 0x00569860;
constexpr std::uintptr_t kStorehouse = 0x00569900;
constexpr std::uintptr_t kHospital = 0x005699d0;
constexpr std::uintptr_t kCharcoalBurner = 0x00569b00;
constexpr std::uintptr_t kDrinkBuilding = 0x00569d20;
constexpr std::uintptr_t kSmallTemple = 0x00569b30;
constexpr std::uintptr_t kLargeTemple = 0x00569c00;
constexpr std::uintptr_t kGemMine = 0x00569a00;
constexpr std::uintptr_t kSulfurMine = 0x00569a30;
constexpr std::uintptr_t kPowderMaker = 0x00569aa0;
constexpr std::uintptr_t kRiceFarm = 0x00569330;
constexpr std::uintptr_t kApiary = 0x00569c90;
constexpr std::uintptr_t kNoOp = 0x0059f200;  // types without a condition

struct Condition {
    B type;
    std::uintptr_t function;
};

// 0x56a1a8..0x56a510, in the original's order (the residences' entry
// 0x56a228 is checked separately: it needs all three types free).
constexpr Condition kCommonBefore[] = {
    {B::Woodcutter, kWoodcutter}, {B::Stonecutter, kStonecutter}, {B::Sawmill, kSawmill},
    {B::Forester, kForester},
};
constexpr B kResidenceTypes[] = {B::SmallResidence, B::MediumResidence, B::LargeResidence};
constexpr Condition kCommonAfter[] = {
    {B::CoalMine, kCoalMine},       {B::IronMine, kIronMine},     {B::IronSmelter, kIronSmelter},
    {B::Toolsmith, kToolsmith},     {B::Weaponsmith, kWeaponsmith}, {B::Fishery, kFishery},
    {B::Farm, kFarm},               {B::Mill, kMill},             {B::Bakery, kBakery},
    {B::Waterworks, kWaterworks},   {B::PigFarm, kPigFarm},       {B::Slaughterhouse, kSlaughterhouse},
    {B::DonkeyFarm, kNoOp},         {B::Barracks, kBarracks},     {B::GoldMine, kGoldMine},
    {B::GoldSmelter, kGoldSmelter}, {B::Storehouse, kStorehouse}, {B::Hospital, kHospital},
    {B::Harbour, kNoOp},            {B::Shipyard, kNoOp},         {B::Marketplace, kNoOp},
};
// The races' own buildings (jump table 0x56a830).
constexpr Condition kRoman[] = {
    {B::CharcoalBurner, kCharcoalBurner}, {B::Vineyard, kDrinkBuilding}, {B::SmallTemple, kSmallTemple},
    {B::LargeTemple, kLargeTemple},       {B::CatapultWorkshop, kNoOp},
};
constexpr Condition kEgyptian[] = {
    {B::GemMine, kGemMine},  {B::Brewery, kDrinkBuilding}, {B::Sphinx, kSmallTemple},
    {B::Pyramid, kLargeTemple}, {B::BallistaWorkshop, kNoOp},
};
constexpr Condition kAsian[] = {
    {B::SulfurMine, kSulfurMine},   {B::PowderMaker, kPowderMaker}, {B::RiceFarm, kRiceFarm},
    {B::Distillery, kDrinkBuilding}, {B::SmallPagoda, kSmallTemple}, {B::Pyramid, kLargeTemple},
    {B::CannonWorkshop, kNoOp},
};
constexpr Condition kAmazon[] = {
    {B::AmazonSulfurMine, kSulfurMine}, {B::Meadery, kDrinkBuilding},       {B::Laboratory, kNoOp},
    {B::Apiary, kApiary},               {B::AmazonSmallTemple, kSmallTemple}, {B::AmazonLargeTemple, kLargeTemple},
    {B::LargeGong, kNoOp},
};

bool TypeFree(B type) {
    const AiPlayerData* data = g_aiPlayerData;
    return data->candidate_values[Value(type)] == 0 && data->type_blocked[Value(type)] == 0;
}

void Consider(EconomyAgent* agent, const Condition& condition) {
    if (TypeFree(condition.type)) {
        reinterpret_cast<void(__fastcall*)(EconomyAgent*, void*)>(condition.function)(agent, nullptr);
    }
}

template <std::size_t N>
void ConsiderAll(EconomyAgent* agent, const Condition (&conditions)[N]) {
    for (const Condition& condition : conditions) {
        Consider(agent, condition);
    }
}

}  // namespace

using ConsiderBuildingsFn = void(__fastcall*)(EconomyAgent* self, void* edx);

void __fastcall T_ECONOMY_AGENT_ConsiderBuildings(EconomyAgent* self, void* edx);

S3RE_HOOK(0x0056a1a0, T_ECONOMY_AGENT_ConsiderBuildings, ConsiderBuildingsFn,
          Original_T_ECONOMY_AGENT_ConsiderBuildings, T_ECONOMY_AGENT_ConsiderBuildings);

// Purpose: The economy agent's look at every building type its race can
// use: each type that isn't waiting (countdown) or blocked gets its
// condition, which may ask for that building.
void __fastcall T_ECONOMY_AGENT_ConsiderBuildings(EconomyAgent* self, void* /*edx*/) {
    ConsiderAll(self, kCommonBefore);
    bool residencesFree = true;
    for (B type : kResidenceTypes) {
        residencesFree = residencesFree && TypeFree(type);
    }
    if (residencesFree) {
        reinterpret_cast<void(__fastcall*)(EconomyAgent*, void*)>(kResidences)(self, nullptr);
    }
    ConsiderAll(self, kCommonAfter);
    switch (static_cast<std::uint32_t>(self->race)) {
    case Value(Race::Roman): ConsiderAll(self, kRoman); break;
    case Value(Race::Egyptian): ConsiderAll(self, kEgyptian); break;
    case Value(Race::Asian): ConsiderAll(self, kAsian); break;
    case Value(Race::Amazon): ConsiderAll(self, kAmazon); break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Every condition is replaced by a recorder (ScopedJump; NoOp runs: a bare
// `ret`, too short to patch); g_aiPlayerData is a buffer with random
// countdowns (mostly 0) and blocked flags; the race 0..3, sometimes
// another value. A condition sometimes changes a later type's countdown
// (as a real one restarts its own), and sometimes g_aiPlayerData itself,
// so the re-reads matter. Compared: the calls in order, the data.

namespace {

std::vector<std::uintptr_t>* g_considered = nullptr;
AiPlayerData* g_otherData = nullptr;
std::uint32_t g_effect = 0;

template <std::uintptr_t Address>
void __fastcall RecordCondition(EconomyAgent*, void*) {
    g_considered->push_back(Address);
    g_effect = g_effect * 1103515245u + 12345u;
    if ((g_effect >> 16) % 9 == 0) {
        g_aiPlayerData.get()->candidate_values[(g_effect >> 8) % kBuildingTypeCount] = 1;
    } else if ((g_effect >> 16) % 23 == 0) {
        g_aiPlayerData = g_otherData;
    }
}

struct Stubs {
    template <std::uintptr_t... A>
    struct List {
        test::ScopedJump jumps[sizeof...(A)] = {test::ScopedJump(A, reinterpret_cast<void*>(&RecordCondition<A>))...};
    };
    List<kWoodcutter, kStonecutter, kSawmill, kForester, kResidences, kCoalMine, kIronMine, kIronSmelter, kToolsmith,
         kWeaponsmith, kFishery, kFarm, kMill, kBakery, kWaterworks, kPigFarm, kSlaughterhouse, kBarracks, kGoldMine,
         kGoldSmelter, kStorehouse, kHospital, kCharcoalBurner, kDrinkBuilding, kSmallTemple, kLargeTemple, kGemMine,
         kSulfurMine, kPowderMaker, kRiceFarm, kApiary>
        list;
};

void CompareConsider(test::Context& ctx, ConsiderBuildingsFn ours, int cases) {
    AiPlayerData* const savedData = g_aiPlayerData;
    Stubs stubs;
    std::vector<std::uint8_t> start(sizeof(AiPlayerData)), other(sizeof(AiPlayerData));
    std::vector<std::uint8_t> data[2], others[2];
    EconomyAgent agent{};
    int calls = 0;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        rng.Fill(start.data(), start.size());
        rng.Fill(other.data(), other.size());
        for (std::vector<std::uint8_t>* bytes : {&start, &other}) {
            auto* d = reinterpret_cast<AiPlayerData*>(bytes->data());
            const std::uint32_t busy = rng.Below(4);
            for (std::uint32_t t = 0; t < kBuildingTypeCount; ++t) {
                d->candidate_values[t] = static_cast<std::uint16_t>(rng.Below(8) < busy ? rng.Next32() : 0);
                d->type_blocked[t] = static_cast<std::uint8_t>(rng.Below(8) < busy ? rng.Next32() : 0);
            }
        }
        agent.race = static_cast<std::int32_t>(rng.Chance(1, 10) ? rng.Next32() : rng.Below(4));
        const std::uint32_t effect = rng.Next32();
        std::vector<std::uintptr_t> considered[2];
        const ConsiderBuildingsFn fns[2] = {Original_T_ECONOMY_AGENT_ConsiderBuildings, ours};
        for (int side = 0; side < 2; ++side) {
            data[side] = start;
            others[side] = other;
            g_aiPlayerData = reinterpret_cast<AiPlayerData*>(data[side].data());
            g_otherData = reinterpret_cast<AiPlayerData*>(others[side].data());
            g_considered = &considered[side];
            g_effect = effect;
            fns[side](&agent, nullptr);
        }
        calls += static_cast<int>(considered[0].size());
        bool same = S3RE_EXPECT(considered[0] == considered[1]);
        same &= S3RE_EXPECT(data[0] == data[1]);
        same &= S3RE_EXPECT(others[0] == others[1]);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, race %d: %u / %u calls", i, agent.race,
                     static_cast<unsigned>(considered[0].size()), static_cast<unsigned>(considered[1].size()));
        }
    }
    g_aiPlayerData = savedData;
    S3RE_EXPECT(calls > cases * 5);
}

void __fastcall WrongConsider(EconomyAgent* self, void* edx) {
    // Wrong: the Asians' second temple slot is the small pagoda's.
    if (self->race == Value(Race::Asian) && TypeFree(B::SmallPagoda) && !TypeFree(B::Pyramid)) {
        reinterpret_cast<void(__fastcall*)(EconomyAgent*, void*)>(kLargeTemple)(self, nullptr);
    }
    T_ECONOMY_AGENT_ConsiderBuildings(self, edx);
}

}  // namespace

S3RE_TEST(AiConsiderBuildings_MatchOriginal) {
    CompareConsider(ctx, &T_ECONOMY_AGENT_ConsiderBuildings, 3000);
}

S3RE_TEST(AiConsiderBuildings_ComparisonDetectsWrongImplementation) {
    test::Context quiet(ctx.name(), 1, /*quiet=*/true);
    CompareConsider(quiet, &WrongConsider, 1000);
    S3RE_EXPECT(quiet.failures() > 0);
}

}  // namespace s3re::game
