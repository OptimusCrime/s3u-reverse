// A computer player's economy areas in the CEvaLand (Rainer\Cevaland.cpp,
// Rainer\compGlobs.cpp; re/computer_players.md "The area site lists"):
// adding an area record and the mine food conditions sent with it.
//
//   0x00555260 CEvaLand::AddArea(area, player) thiscall, ret 8: builds a
//              record on the stack (39 site trees constructed with pointers
//              to two uninitialized stack bytes as comparator / allocator;
//              area = the argument's low word; basic_economy,
//              short_of_wood, short_of_stone, unk018, sites 0;
//              force_production 1; unk014 0xff; every type's best[] and
//              taken best[57 + type], countdown, count and blocked flag 0;
//              the countdowns of the residences (26..28) from
//              g_aiCandidateDefaults; the padding and site_tree_of_type
//              left uninitialized), fills its site lists
//              (FillAreaSiteLists 0x556630) and appends a copy to
//              lists[player & 0xffff] (AreaList::insert 0x5594f0 before the
//              head). Then, on the new node (the list's last):
//              CountNodeBuildingsByType 0x572420 (&player, &area);
//              CheckBasicEconomy 0x5720c0 and without the basic economy the
//              countdowns of coal mine 400, iron mine and iron smelter 500;
//              sites = CountSitesInArea 0x59dbb0 (player's low byte, area);
//              COMP_FoodForMinesOptimum 0x572a30 (&player, &area); and
//              site_tree_of_type[type] = &site_trees[kSiteTreeOfType[type]]
//              for every type but 0 and the military ones (15, 16, 18),
//              whose pointers keep the stack record's uninitialized values.
//              So the copy constructor's verbatim pointers (they point into
//              the stack record) are replaced here at once; std::list never
//              moves its nodes, so they stay valid. Last the stack record is
//              destroyed (its trees from the last to the first, as ~Area
//              0x556080 does). EAX: left over.
//   0x00572a30 COMP_FoodForMinesOptimum(const u16* player, const u16* area)
//              fastcall: logs "COMP_FoodForMinesOptimum player: %u,
//              ecosector %u" (sprintf 0x61f27e, OutputDebugStringA); finds
//              the area's record in g_aiLandEvaluation's lists[*player] (none:
//              logs "... EcoSector gehört nicht dem Spieler" and returns);
//              reads the area's meat, fish, bread and rice
//              (AI_AreaGoodsAmount 0x59dc10) and, per mine type the record
//              counts (coal: bread, iron: meat, gold: fish, sulfur: rice,
//              gems: fish), sends SetAreaCondition commands (0x58ea30;
//              value 0 = off, 16 = on) for the food conditions: with the
//              food in stock the mine's own condition goes off, without it
//              on (and a log line "Brot / Fleisch / Fisch vorhanden" or
//              "KEIN ..."). Details at FoodForMines. EAX: left over.

#include "game/ai/ai_layout.h"
#include "game/debug_log.h"
#include "game/enums/buildings.h"
#include "game/enums/goods.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/tables.h"
#include "game/texts.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"
#include "game/ai/ai_area_test_world.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace s3re::game {

using namespace ai;

namespace {

using B = BuildingType;

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kSiteTreeConstruct = 0x005590a0;   // AI::SiteTree::SiteTree(pred, alloc) thiscall
constexpr std::uintptr_t kAreaDestruct = 0x00556080;        // CEvaLand::Area::~Area
constexpr std::uintptr_t kFillAreaSiteLists = 0x00556630;   // CEvaLand::FillAreaSiteLists
constexpr std::uintptr_t kAreaListInsert = 0x005594f0;      // AI::AreaList::insert
constexpr std::uintptr_t kCountNodeBuildings = 0x00572420;  // AI::CountNodeBuildingsByType (fastcall)
constexpr std::uintptr_t kCheckBasicEconomy = 0x005720c0;   // AI::CheckBasicEconomy (fastcall)
constexpr std::uintptr_t kCountSitesInArea = 0x0059dbb0;    // AI::CountSitesInArea (fastcall)
constexpr std::uintptr_t kFoodForMines = 0x00572a30;        // COMP_FoodForMinesOptimum (fastcall)
constexpr std::uintptr_t kAreaGoodsAmount = 0x0059dc10;     // AI::AreaGoodsAmount (fastcall)
constexpr std::uintptr_t kSetAreaCondition = 0x0058ea30;    // AI::SendSetAreaCondition (fastcall)
constexpr std::uintptr_t kSprintf = 0x0061f27e;             // the CRT's sprintf

constexpr std::uint8_t kResidenceTypes[] = {Value(B::SmallResidence), Value(B::MediumResidence),
                                            Value(B::LargeResidence)};
// Without the basic economy the mines and the iron smelter wait.
constexpr std::uint16_t kCoalMineCountdown = 400;
constexpr std::uint16_t kIronCountdown = 500;
constexpr std::size_t kTextBytes = 0x100;  // the original's sprintf buffer (0x120 bytes of locals)

// SetAreaCondition values and the food conditions' kinds (the area's
// condition_enabled index; meanings from the mine types below).
constexpr std::uint8_t kConditionOff = 0;
constexpr std::uint8_t kConditionOn = 16;

template <typename... Args>
void LogDebugString(const GameText& format, Args... args) {
    char text[kTextBytes];
    using Fn = int(__cdecl*)(char* buffer, const char* format, ...);
    reinterpret_cast<Fn>(kSprintf)(text, format.c_str(), args...);
    g_importOutputDebugStringA.get()(text);
}

void LogDebugString(const GameText& text) {
    g_importOutputDebugStringA.get()(text.c_str());
}

void SetAreaCondition(std::uint8_t player, const std::uint16_t* area, std::uint8_t kind, std::uint8_t value) {
    using Fn = std::uint32_t(__fastcall*)(const void* player, const void* area, const void* kind, const void* value);
    reinterpret_cast<Fn>(kSetAreaCondition)(&player, area, &kind, &value);
}

std::uint32_t GoodsInArea(Good goods, const std::uint16_t* area) {
    const auto kind = static_cast<std::uint8_t>(Value(goods));
    using Fn = std::uint32_t(__fastcall*)(const std::uint8_t* goods, const std::uint16_t* area);
    return reinterpret_cast<Fn>(kAreaGoodsAmount)(&kind, area);
}

AiAreaNode* FindAreaNode(const AiLandEvaluation* land, std::uint16_t player, std::uint16_t area) {
    auto* const head = reinterpret_cast<AiAreaNode*>(land->lists[player].head);
    for (AiAreaNode* node = head->next; node != head; node = node->next) {
        if (node->value.area == area) {
            return node;
        }
    }
    return nullptr;
}

// One mine's food: three conditions (kinds a, b, c) with their values when
// the food is in stock and when it isn't; the log line in between or before.
struct MineFood {
    std::uint8_t kind[3];
    std::uint8_t with[3];     // values with the food in stock
    std::uint8_t without[3];  // without it
};

}  // namespace

using FoodForMinesFn = void(__fastcall*)(const std::uint16_t* player, const std::uint16_t* area);
using AddAreaFn = void(__fastcall*)(AiLandEvaluation* self, void* edx, std::uint32_t area, std::uint32_t player);

void __fastcall COMP_FoodForMinesOptimum(const std::uint16_t* player, const std::uint16_t* area);
void __fastcall AI_CEvaLand_AddArea(AiLandEvaluation* self, void* edx, std::uint32_t area, std::uint32_t player);

S3RE_HOOK(0x00572a30, COMP_FoodForMinesOptimum, FoodForMinesFn, Original_COMP_FoodForMinesOptimum,
          COMP_FoodForMinesOptimum);
S3RE_HOOK(0x00555260, AI_CEvaLand_AddArea, AddAreaFn, Original_AI_CEvaLand_AddArea, AI_CEvaLand_AddArea);

// Purpose: Tells a computer player's economy area which mine food to
// produce: the food a mine eats that isn't in stock gets its production
// conditions switched on (probably "make bread / meat / fish for the
// mines"), food in stock lets them go off.
void __fastcall COMP_FoodForMinesOptimum(const std::uint16_t* player, const std::uint16_t* area) {
    LogDebugString(texts::kFoodForMinesHeader, static_cast<std::uint32_t>(*player), static_cast<std::uint32_t>(*area));
    const auto* land = reinterpret_cast<const AiLandEvaluation*>(g_aiLandEvaluation.get());
    const AiAreaNode* node = FindAreaNode(land, *player, *area);
    if (node == nullptr) {
        LogDebugString(texts::kFoodForMinesNotOwned);
        return;
    }
    const std::uint32_t meat = GoodsInArea(Good::Meat, area);
    const std::uint32_t fish = GoodsInArea(Good::Fish, area);
    const std::uint32_t bread = GoodsInArea(Good::Bread, area);
    const std::uint32_t rice = GoodsInArea(Good::Rice, area);
    const std::uint16_t* counts = node->value.building_counts;
    const auto owner = static_cast<std::uint8_t>(*player);
    const auto set = [&](std::uint8_t kind, std::uint8_t value) { SetAreaCondition(owner, area, kind, value); };

    // 0x572b1f: coal mines eat bread.
    if (counts[Value(B::CoalMine)] != 0) {
        if (bread != 0) {
            set(36, kConditionOff);
            set(30, kConditionOn);
            LogDebugString(texts::kFoodForMinesBread);
            set(40, kConditionOff);
        } else {
            LogDebugString(texts::kFoodForMinesNoBread);
            set(36, kConditionOn);
            set(30, kConditionOn);
            set(40, kConditionOn);
        }
    }
    // 0x572c3e: iron mines eat meat.
    if (counts[Value(B::IronMine)] != 0) {
        LogDebugString(meat != 0 ? texts::kFoodForMinesMeat : texts::kFoodForMinesNoMeat);
        set(35, kConditionOn);
        set(31, meat != 0 ? kConditionOff : kConditionOn);
        set(41, meat != 0 ? kConditionOff : kConditionOn);
    }
    // 0x572d37: gold mines eat fish.
    if (counts[Value(B::GoldMine)] != 0) {
        LogDebugString(fish != 0 ? texts::kFoodForMinesFish : texts::kFoodForMinesNoFish);
        set(37, fish != 0 ? kConditionOff : kConditionOn);
        set(32, fish != 0 ? kConditionOff : kConditionOn);
        set(39, kConditionOn);
    }
    // 0x572e34: sulfur mines eat rice.
    if (counts[Value(B::SulfurMine)] != 0) {
        set(34, rice != 0 ? kConditionOff : kConditionOn);
    }
    // 0x572e8b: gem mines eat fish.
    if (counts[Value(B::GemMine)] != 0) {
        set(38, fish != 0 ? kConditionOff : kConditionOn);
        set(33, fish != 0 ? kConditionOff : kConditionOn);
        set(42, kConditionOn);
    }
}

namespace {

// 0x55570a..0x5557c4: the stack record's fields before FillAreaSiteLists.
void InitRecordFields(AiArea& record, std::uint32_t area) {
    record.area = static_cast<std::uint16_t>(area);
    record.basic_economy = 0;
    record.short_of_wood = 0;
    record.short_of_stone = 0;
    record.force_production = 1;
    record.unk014 = 0xff;
    record.unk018 = 0;
    record.sites = 0;
    for (std::uint32_t type = 0; type < kBuildingTypeCount; ++type) {
        record.best[type] = AiCandidate{0, 0, 0};
        record.best[kOrderedCandidates + type] = AiCandidate{0, 0, 0};
        record.candidate_values[type] = 0;
        record.type_blocked[type] = 0;
        record.building_counts[type] = 0;
    }
    for (std::uint8_t type : kResidenceTypes) {
        record.candidate_values[type] = g_aiCandidateDefaults[type];
    }
}

// 0x55584d..0x555aa9: the new node's sites, mine food and tree pointers.
void SetUpNewNode(AiArea& value, std::uint32_t* area, std::uint32_t* player) {
    const auto playerByte = static_cast<std::uint8_t>(*player);
    using CountSitesFn = std::uint32_t(__fastcall*)(const std::uint8_t* player, const void* area);
    value.sites = static_cast<std::uint16_t>(reinterpret_cast<CountSitesFn>(kCountSitesInArea)(&playerByte, area));
    using FoodFn = void(__fastcall*)(const void* player, const void* area);
    reinterpret_cast<FoodFn>(kFoodForMines)(player, area);
    for (std::uint32_t type = 0; type < kBuildingTypeCount; ++type) {
        if (kSiteTreeOfType[type] >= 0) {
            value.site_tree_of_type[type] = &value.site_trees[kSiteTreeOfType[type]];
        }
    }
}

}  // namespace

// Purpose: Starts the land evaluation record of an economy area a computer
// player now owns (at game start for each of its areas, and when an area
// changes owner): its candidate places per building type and its building
// counts; sends the mine food settings.
void __fastcall AI_CEvaLand_AddArea(AiLandEvaluation* self, void* /*edx*/, std::uint32_t area, std::uint32_t player) {
    // The original constructs the trees with pointers to uninitialized stack
    // bytes (the comparator and allocator bytes, copied into the node's
    // trees, never read); ours 0.
    static constexpr std::uint8_t kEmptyObject = 0;
    alignas(4) std::uint8_t storage[sizeof(AiArea)];
    auto* record = reinterpret_cast<AiArea*>(storage);
    using ConstructFn = SiteTree*(__fastcall*)(SiteTree* self, void* edx, const std::uint8_t* pred,
                                               const std::uint8_t* alloc);
    for (SiteTree& tree : record->site_trees) {
        reinterpret_cast<ConstructFn>(kSiteTreeConstruct)(&tree, nullptr, &kEmptyObject, &kEmptyObject);
    }
    InitRecordFields(*record, area);
    using FillFn = void(__fastcall*)(AiLandEvaluation*, void*, AiArea*, std::uint32_t);
    reinterpret_cast<FillFn>(kFillAreaSiteLists)(self, nullptr, record, player);

    AiList& list = self->lists[player & 0xffff];
    AiAreaNode* inserted;
    using InsertFn = AiAreaNode**(__fastcall*)(AiList*, void*, AiAreaNode**, AiAreaNode*, const AiArea*);
    reinterpret_cast<InsertFn>(kAreaListInsert)(&list, nullptr, &inserted, reinterpret_cast<AiAreaNode*>(list.head),
                                                record);
    using CountFn = std::uint32_t(__fastcall*)(const void* player, const void* area);
    reinterpret_cast<CountFn>(kCountNodeBuildings)(&player, &area);

    // The new node is the list's last (re-read from the list).
    AiAreaNode* node = reinterpret_cast<AiAreaNode*>(self->lists[player & 0xffff].head)->prev;
    using BasicEconomyFn = std::uint32_t(__fastcall*)(const void* player, void* edx, AiAreaNode* node);
    if (reinterpret_cast<BasicEconomyFn>(kCheckBasicEconomy)(&player, nullptr, node) == 0) {
        node->value.candidate_values[Value(B::IronSmelter)] = kIronCountdown;
        node->value.candidate_values[Value(B::CoalMine)] = kCoalMineCountdown;
        node->value.candidate_values[Value(B::IronMine)] = kIronCountdown;
    }
    SetUpNewNode(node->value, &area, &player);
    reinterpret_cast<void(__fastcall*)(AiArea*, void*)>(kAreaDestruct)(record, nullptr);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// The site-list test map (ai_area_test_world.h) plus random buildings
// (owners 0..2, every type, some sites) and goods piles of meat, fish,
// bread and rice in areas 0..3; MessageQueue::Push and the host check are
// stubbed (the commands are recorded) and OutputDebugStringA's import
// points at a recorder. FoodForMines: a list of records with random
// building counts; compared: the commands and log lines. AddArea: two
// CEvaLand objects with the same lists; compared: every record of the
// player's list (fields without the copy's skipped padding, every tree's
// values, the type -> tree pointers as tree indexes, except the types the
// original leaves uninitialized), the commands, the log lines and the nil
// node's references.

namespace {

using namespace ai_test;

constexpr std::uintptr_t kIsHost = 0x004318a0;
constexpr std::uintptr_t kPush = 0x004309c0;
constexpr std::uint32_t kQueue = 0x5152a000;  // a fake queue (never dereferenced)

using Message = std::array<std::uint8_t, 12>;

std::vector<std::string>* g_strings = nullptr;

void __stdcall RecordString(const char* text) {
    if (g_strings != nullptr) {
        g_strings->emplace_back(text);
    }
}

constexpr std::uint8_t kGoodsOfInterest[] = {Value(Good::Meat), Value(Good::Fish), Value(Good::Bread),
                                             Value(Good::Rice)};

void SetUpEconomy(test::Rng& rng, CGameData* world) {
    const std::uint32_t count = rng.Below(60);
    world->building_high_water = count;
    for (std::uint32_t id = 1; id <= count + 1 && id < 4000; ++id) {
        layout::Building& b = world->buildings[id];
        std::memset(&b, 0, sizeof(b));
        b.owner = static_cast<std::uint8_t>(rng.Chance(1, 8) ? 0xff : rng.Below(3));
        constexpr B kCommon[] = {B::CoalMine, B::IronMine,  B::GoldMine, B::SulfurMine, B::GemMine,
                                 B::Woodcutter, B::Sawmill, B::Stonecutter, B::Forester, B::SmallTower};
        b.art = static_cast<std::uint8_t>(rng.Chance(1, 3) ? rng.Below(kBuildingTypeCount)
                                                           : Value(kCommon[rng.Below(std::size(kCommon))]));
        b.buildup = static_cast<std::uint8_t>(rng.Chance(1, 3) ? 1 + rng.Below(4) : 0);
        b.x = b.door_x = static_cast<std::uint16_t>(rng.Below(kTestMap));
        b.y = b.door_y = static_cast<std::uint16_t>(rng.Below(kTestMap));
    }
    std::uint16_t nextPile = 1;
    for (std::uint32_t area = 0; area < 5; ++area) {
        std::memset(world->areas[area].piles_by_goods, 0, sizeof(world->areas[area].piles_by_goods));
        for (std::uint8_t goods : kGoodsOfInterest) {
            if (rng.Chance(1, 2)) {
                layout::Pile& pile = world->piles[nextPile];
                pile.amount = static_cast<std::uint8_t>(rng.Chance(1, 4) ? 0 : 1 + rng.Below(8));
                pile.next = 0;
                world->areas[area].piles_by_goods[goods] = nextPile++;
            }
        }
    }
}

struct Captured {
    std::vector<Message> messages;
    std::vector<std::string> strings;
    bool operator==(const Captured& other) const { return messages == other.messages && strings == other.strings; }
};

class Harness {
public:
    Harness() : buffer_(sizeof(CGameData)), cells_(kGridSide * kGridSide) {
        world_ = buffer_.world();
        g_aiWorld = world_;
        g_aiMapSizeX = static_cast<std::uint16_t>(kTestMap);
        g_aiMapSizeY = static_cast<std::uint16_t>(kTestMap);
        computer_.resize(sizeof(ComputerPlayer));
        reinterpret_cast<ComputerPlayer*>(computer_.data())->message_queue = kQueue;
        g_aiPlayer = reinterpret_cast<ComputerPlayer*>(computer_.data());
        stubs_.SetWorld(nullptr);
        stubs_.Install<0, kIsHost>([](CGameData*, std::uint32_t, const std::uint32_t*) { return 1u; });
        stubs_.Install<3, kPush>([this](CGameData*, std::uint32_t self, const std::uint32_t* args) {
            Message message{};
            std::memcpy(message.data(), reinterpret_cast<const void*>(args[0]), 4 * (args[1] < 3 ? args[1] : 3));
            message[3] = 0;  // (an unwritten header byte)
            message[11] ^= static_cast<std::uint8_t>(self == kQueue ? 0 : 0xff);
            capture_->messages.push_back(message);
            return 0u;
        });
        const auto recorder = reinterpret_cast<std::uint32_t>(&RecordString);
        test::PatchBytes(reinterpret_cast<std::uintptr_t>(&g_importOutputDebugStringA.get()), &recorder,
                         sizeof(recorder));
    }
    ~Harness() {
        test::UnpatchBytes(reinterpret_cast<std::uintptr_t>(&g_importOutputDebugStringA.get()));
        g_strings = nullptr;
        g_aiPlayer = savedPlayer_;
    }
    CGameData* world() { return world_; }
    std::vector<AiGridCell>& cells() { return cells_; }
    // Runs f with its commands and log lines captured.
    template <typename F>
    Captured Capture(F f) {
        Captured captured;
        capture_ = &captured;
        g_strings = &captured.strings;
        f();
        g_strings = nullptr;
        stubs_.Take();
        return captured;
    }

private:
    SavedGlobals saved_;
    ComputerPlayer* savedPlayer_ = g_aiPlayer;
    test::WorldBuffer buffer_;
    CGameData* world_;
    std::vector<AiGridCell> cells_;
    std::vector<std::uint8_t> computer_;
    test::CalleeStubs stubs_;
    Captured* capture_ = nullptr;
};

void RandomCounts(test::Rng& rng, AiArea* record) {
    for (B type : {B::CoalMine, B::IronMine, B::GoldMine, B::SulfurMine, B::GemMine}) {
        record->building_counts[Value(type)] = static_cast<std::uint16_t>(rng.Chance(1, 2) ? 0 : 1 + rng.Below(3));
    }
}

void CompareFoodForMines(test::Context& ctx, FoodForMinesFn ours, int cases) {
    Harness harness;
    int sent = 0;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        if (i % 20 == 0) {
            SetUpMap(rng, harness.world(), harness.cells());
        }
        SetUpEconomy(rng, harness.world());
        TestLand land(harness.cells());
        const std::uint16_t player = static_cast<std::uint16_t>(rng.Below(3));
        auto* record = static_cast<AiArea*>(msvc6::GameOperatorNew(sizeof(AiArea)));
        for (std::uint32_t k = rng.Chance(1, 10) ? 0 : 1 + rng.Below(3); k > 0; --k) {
            MakeRecord(rng, record, static_cast<std::uint16_t>(rng.Below(4)));
            RandomCounts(rng, record);
            land.Append(player, record);
            DestroyRecord(record);
        }
        msvc6::GameOperatorDelete(record);
        g_aiLandEvaluation = reinterpret_cast<std::uint32_t>(&land.land);
        const std::uint16_t area = static_cast<std::uint16_t>(rng.Below(rng.Chance(1, 10) ? 5 : 4));
        const Captured a = harness.Capture([&] { Original_COMP_FoodForMinesOptimum(&player, &area); });
        const Captured b = harness.Capture([&] { ours(&player, &area); });
        sent += a.messages.empty() ? 0 : 1;
        if (!S3RE_EXPECT(a == b)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, player %u, area %u: %u/%u commands, %u/%u lines", i, player, area,
                     static_cast<unsigned>(a.messages.size()), static_cast<unsigned>(b.messages.size()),
                     static_cast<unsigned>(a.strings.size()), static_cast<unsigned>(b.strings.size()));
        }
    }
    if (cases >= 500 && !S3RE_EXPECT(sent >= cases / 4)) {
        ctx.Fail(__FILE__, __LINE__, "  only %d cases sent commands", sent);
    }
}

// The new records of a list: fields (without the copy's padding), trees
// and type -> tree indexes.
std::vector<std::uint8_t> RecordContents(const AiArea& record, bool pointers) {
    std::vector<std::uint8_t> out(reinterpret_cast<const std::uint8_t*>(&record),
                                  reinterpret_cast<const std::uint8_t*>(&record) + offsetof(AiArea, site_tree_of_type));
    for (std::size_t pad : {offsetof(AiArea, unk002), offsetof(AiArea, unk002) + 1, offsetof(AiArea, unk015),
                            offsetof(AiArea, unk015) + 1, offsetof(AiArea, unk015) + 2, offsetof(AiArea, unk3e7)}) {
        out[pad] = 0;
    }
    for (std::uint32_t type = 0; type < kBuildingTypeCount && pointers; ++type) {
        if (kSiteTreeOfType[type] >= 0) {
            out.push_back(static_cast<std::uint8_t>(record.site_tree_of_type[type] - &record.site_trees[0]));
        }
    }
    for (const SiteTree& tree : record.site_trees) {
        const std::vector<std::uint8_t> values = TreeValues(tree);
        out.insert(out.end(), values.begin(), values.end());
    }
    return out;
}

std::vector<std::uint8_t> ListRecords(const AiList& list) {
    std::vector<std::uint8_t> out{static_cast<std::uint8_t>(list.count)};
    const auto* head = reinterpret_cast<const AiAreaNode*>(list.head);
    for (const AiAreaNode* node = head->next; node != head; node = node->next) {
        // (the older records' pointers are random test bytes)
        const std::vector<std::uint8_t> record = RecordContents(node->value, node->next == head);
        out.insert(out.end(), record.begin(), record.end());
    }
    return out;
}

void CompareAddArea(test::Context& ctx, AddAreaFn ours, int cases) {
    Harness harness;
    const std::uint32_t refs = g_aiSiteTreeNilRefs;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        if (!S3RE_EXPECT_EQ(g_aiSiteTreeNilRefs.get(), refs)) {  // (the last case's trees all freed)
            ctx.Fail(__FILE__, __LINE__, "  before case %d", i);
        }
        if (i % 10 == 0) {
            SetUpMap(rng, harness.world(), harness.cells());
        }
        SetUpEconomy(rng, harness.world());
        for (layout::Player& p : harness.world()->players) {
            p.race = static_cast<std::int32_t>(rng.Below(4));
        }
        TestLand a(harness.cells()), b(harness.cells());
        a.land.width = b.land.width = kGridSide;
        a.land.height = b.land.height = kGridSide;
        const std::uint32_t index = rng.Below(3);
        const std::uint32_t player = index | (rng.Chance(1, 5) ? rng.Next32() & 0xffff0000u : 0);
        auto* record = static_cast<AiArea*>(msvc6::GameOperatorNew(sizeof(AiArea)));
        for (std::uint32_t k = rng.Below(3); k > 0; --k) {
            MakeRecord(rng, record, static_cast<std::uint16_t>(rng.Below(4)));
            a.Append(index, record);
            b.Append(index, record);
            DestroyRecord(record);
        }
        msvc6::GameOperatorDelete(record);
        const std::uint32_t area = rng.Below(5) | (rng.Chance(1, 5) ? rng.Next32() & 0xffff0000u : 0);
        const Captured ca = harness.Capture([&] {
            g_aiLandEvaluation = reinterpret_cast<std::uint32_t>(&a.land);
            Original_AI_CEvaLand_AddArea(&a.land, nullptr, area, player);
        });
        const Captured cb = harness.Capture([&] {
            g_aiLandEvaluation = reinterpret_cast<std::uint32_t>(&b.land);
            ours(&b.land, nullptr, area, player);
        });
        bool same = S3RE_EXPECT(ca == cb);
        const std::vector<std::uint8_t> ra = ListRecords(a.land.lists[index]);
        const std::vector<std::uint8_t> rb = ListRecords(b.land.lists[index]);
        if (!S3RE_EXPECT(ra == rb)) {
            same = false;
            std::size_t k = 0;
            while (k < ra.size() && k < rb.size() && ra[k] == rb[k]) {
                ++k;
            }
            ctx.Fail(__FILE__, __LINE__, "  sizes %u / %u, first difference at %u", static_cast<unsigned>(ra.size()),
                     static_cast<unsigned>(rb.size()), static_cast<unsigned>(k));
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, player %08x, area %08x", i, player, area);
        }
    }
}

void __fastcall WrongFoodForMines(const std::uint16_t* player, const std::uint16_t* area) {
    // Wrong: gem mines also ask for rice (like sulfur mines).
    COMP_FoodForMinesOptimum(player, area);
    const auto* land = reinterpret_cast<const AiLandEvaluation*>(g_aiLandEvaluation.get());
    const AiAreaNode* node = FindAreaNode(land, *player, *area);
    if (node != nullptr && node->value.building_counts[Value(B::GemMine)] != 0) {
        SetAreaCondition(static_cast<std::uint8_t>(*player), area, 34, kConditionOn);
    }
}

void __fastcall WrongAddArea(AiLandEvaluation* self, void* edx, std::uint32_t area, std::uint32_t player) {
    // Wrong: without the basic economy only the coal mine waits.
    AI_CEvaLand_AddArea(self, edx, area, player);
    AiAreaNode* node = reinterpret_cast<AiAreaNode*>(self->lists[player & 0xffff].head)->prev;
    if (node->value.candidate_values[Value(B::IronMine)] == kIronCountdown) {
        node->value.candidate_values[Value(B::IronMine)] = 0;
    }
}

}  // namespace

S3RE_TEST(AiAreaEvents_MatchOriginal) {
    CompareFoodForMines(ctx, &COMP_FoodForMinesOptimum, 1000);
    CompareAddArea(ctx, &AI_CEvaLand_AddArea, 300);
}

S3RE_TEST(AiAreaEvents_ComparisonDetectsWrongImplementation) {
    {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareFoodForMines(quiet, &WrongFoodForMines, 1000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
    {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareAddArea(quiet, &WrongAddArea, 300);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

}  // namespace s3re::game
