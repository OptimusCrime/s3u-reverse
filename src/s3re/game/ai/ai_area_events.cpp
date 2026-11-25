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

// ---- the AI event ring

namespace {

constexpr std::uintptr_t kBuildingIfUsed = 0x0058e1c0;        // AI::BuildingIfUsed_58e1c0 (fastcall)
constexpr std::uintptr_t kGetBuilding = 0x0058e170;           // AI::GetBuilding (fastcall)
constexpr std::uintptr_t kBuildingArea = 0x00599610;          // AI::BuildingArea (fastcall)
constexpr std::uintptr_t kGridCellOfTile = 0x00554770;        // CEvaLand::GridCellOfTile (thiscall)
constexpr std::uintptr_t kReevaluateAroundTile = 0x005548b0;  // CEvaLand::ReevaluateAroundTile (thiscall)
constexpr std::uintptr_t kAreaAlongRow = 0x005996c0;          // AI::AreaAlongRow (fastcall)
constexpr std::uintptr_t kAreaPopulation = 0x0059dcc0;        // AI::AreaPopulation (fastcall)
constexpr std::uintptr_t kRemoveArea = 0x00558e80;            // CEvaLand::RemoveArea
constexpr std::uintptr_t kAddArea = 0x00555260;               // CEvaLand::AddArea
constexpr std::uintptr_t kRefillAreaSiteList = 0x005579d0;    // CEvaLand::RefillAreaSiteList
constexpr std::uintptr_t kOnBuildingFinished = 0x00573890;    // AI::OnBuildingFinished (fastcall)
constexpr std::uintptr_t kBitsetXran = 0x00559550;            // std::bitset::_Xran (thiscall, throws)
constexpr std::uintptr_t kSetWorkArea = 0x0058e220;           // AI::SendSetWorkArea (fastcall)
constexpr std::uintptr_t kDestroyBuilding = 0x0058e5c0;       // AI::SendDestroyBuilding (fastcall)
constexpr std::uintptr_t kChangeGarrison = 0x0058e630;        // AI::SendChangeGarrison (fastcall)
constexpr std::uintptr_t kClearToolGoodsSettings = 0x0058e930;  // AI::ClearToolGoodsSettings (fastcall)
constexpr std::uintptr_t kEnsureGoodsSupply = 0x00573760;     // AI::EnsureGoodsSupply (fastcall)
// The work-area finders (fastcall: building, u16* x; stack u16* y).
constexpr std::uintptr_t kFindWoodcutterWorkArea = 0x0059a3a0;
constexpr std::uintptr_t kFindFarmWorkArea = 0x0059ac50;
constexpr std::uintptr_t kFindRiceFarmWorkArea = 0x0059b0a0;
constexpr std::uintptr_t kFindVineyardWorkArea = 0x0059b430;
constexpr std::uintptr_t kFindFisheryWorkArea = 0x0059b780;
constexpr std::uintptr_t kFindWaterworksWorkArea = 0x0059ba90;

constexpr std::int32_t kComputerControlled = 2;  // Player::active of a computer player
constexpr std::uint32_t kPendingEvents = 1000;   // the ring's size
constexpr std::uint32_t kNoPlayer = 0xff;
constexpr std::int32_t kCellTiles = 8;

// The ring's event types (who queues them: re/computer_players.md).
enum class AiEvent : std::int32_t {
    BuildingFinished = 1,  // BuildingDone: a = building
    SitePlaced = 2,        // CheckBuildingSite: a = building (the new site)
    BuildingGoneAt = 3,    // CrushBuilding: a = x, b = y (the next event's place)
    AreaChanged = 4,       // the area functions: a = area
    OwnerChanged = 5,      // UpdateSoldier (a building conquered): a = building, b = its old owner
    BuildingGone = 6,      // CrushBuilding: a = building type, b = player
    WorkerIdle = 7,        // UpdateSettlers: a = building (G: its worker finds nothing to do)
    Reevaluate = 8,        // UpdateEnvironment: a = x, b = y
};

const layout::Building* BuildingIfUsed(std::uint32_t id) {
    using Fn = const layout::Building*(__fastcall*)(std::uint32_t id);
    return reinterpret_cast<Fn>(kBuildingIfUsed)(id);
}

bool IsComputerPlayer(std::uint32_t player) {
    // (not range-checked, as in the original)
    return g_aiWorld.get()->players[player].active == kComputerControlled;
}

AiLandEvaluation* CurrentLand() {
    return reinterpret_cast<AiLandEvaluation*>(g_aiLandEvaluation.get());
}

AiGridCell* GridCellOfTile(AiLandEvaluation* land, std::uint32_t x, std::uint32_t y) {
    using Fn = AiGridCell*(__fastcall*)(AiLandEvaluation* self, void* edx, std::uint32_t x, std::uint32_t y);
    return reinterpret_cast<Fn>(kGridCellOfTile)(land, nullptr, x, y);
}

// The cell's std::bitset<57> of the player's building types; a type above
// 56 throws std::out_of_range (_Xran 0x559550, called with the bitset).
std::uint32_t& BuildingTypeWord(AiGridCell* cell, std::uint32_t player, std::uint32_t type) {
    std::uint32_t* bits = &cell->building_types[0][0] + 2 * player;  // (player not range-checked)
    if (type >= kBuildingTypeCount) {
        reinterpret_cast<void(__fastcall*)(void*, void*)>(kBitsetXran)(bits, nullptr);
    }
    return bits[type / 32];
}

std::uint32_t BuildingAreaOf(const std::uint32_t* building) {
    using Fn = std::uint32_t(__fastcall*)(const void* building);
    return reinterpret_cast<Fn>(kBuildingArea)(building);
}

AiAreaNode* FindAreaNode(AiList& list, std::uint16_t area) {
    auto* const head = reinterpret_cast<AiAreaNode*>(list.head);
    for (AiAreaNode* node = head->next; node != head; node = node->next) {
        if (node->value.area == area) {
            return node;
        }
    }
    return nullptr;
}

void Reevaluate(std::uint32_t x, std::uint32_t y) {
    using Fn = std::uint32_t(__fastcall*)(AiLandEvaluation* self, void* edx, std::uint32_t x, std::uint32_t y);
    reinterpret_cast<Fn>(kReevaluateAroundTile)(CurrentLand(), nullptr, x, y);
}

// A finder's work area sent as the building's (the finder's result is not
// looked at: without one the building's own position goes out).
void SetWorkAreaWith(std::uintptr_t finder, const layout::Building* building, std::uint32_t id, std::uint8_t owner,
                     std::uint32_t x, std::uint32_t y) {
    using FinderFn = std::uint32_t(__fastcall*)(const layout::Building* building, std::uint32_t* x, std::uint32_t* y);
    reinterpret_cast<FinderFn>(finder)(building, &x, &y);
    const std::uint32_t number = id & 0xffff;
    using SendFn = std::uint32_t(__fastcall*)(const void* building, const void* player, const void* x, const void* y);
    reinterpret_cast<SendFn>(kSetWorkArea)(&number, &owner, &x, &y);
}

void DestroyBuilding(std::uint32_t id, std::uint8_t owner) {
    using Fn = std::uint32_t(__fastcall*)(const void* building, const void* player);
    reinterpret_cast<Fn>(kDestroyBuilding)(&id, &owner);
}

void EnsureGoodsSupply(Good goods, std::uint8_t owner, const std::uint32_t* area) {
    const auto kind = static_cast<std::uint8_t>(Value(goods));
    using Fn = std::uint32_t(__fastcall*)(const void* goods, const void* player, const void* area);
    reinterpret_cast<Fn>(kEnsureGoodsSupply)(&kind, &owner, area);
}

std::uint32_t AreaPopulation(const std::uint32_t* area) {
    using Fn = std::uint32_t(__fastcall*)(const void* area);
    return reinterpret_cast<Fn>(kAreaPopulation)(area);
}

void RemoveArea(AiLandEvaluation* land, std::uint32_t area, std::uint32_t player) {
    using Fn = std::uint32_t(__fastcall*)(AiLandEvaluation* self, void* edx, std::uint32_t area, std::uint32_t player);
    reinterpret_cast<Fn>(kRemoveArea)(land, nullptr, area, player);
}

void AddArea(AiLandEvaluation* land, std::uint32_t area, std::uint32_t player) {
    reinterpret_cast<AddAreaFn>(kAddArea)(land, nullptr, area, player);
}

void Refill(AiLandEvaluation* land, std::uint32_t player, std::uint32_t area, B type) {
    using Fn = void(__fastcall*)(AiLandEvaluation* self, void* edx, std::uint32_t player, std::uint32_t area,
                                 std::uint32_t type);
    reinterpret_cast<Fn>(kRefillAreaSiteList)(land, nullptr, player, area, Value(type));
}

// 0x555002..0x5551b6 (event 2, the owner a computer player): the new site's
// type goes into its cell's bitset and its area record's counts; the
// candidates of the types that depend on it are rated again.
void OnSitePlaced(AiLandEvaluation* self, std::uint32_t id, const layout::Building& site) {
    // The player as passed on: the owner in the low word, the event
    // argument's high word left in the register.
    const std::uint32_t owner = site.owner;
    const std::uint32_t player = (id & 0xffff0000u) | owner;
    const std::uint32_t type = site.art;
    AiGridCell* cell = GridCellOfTile(CurrentLand(), site.x, site.y);
    BuildingTypeWord(cell, owner, type) |= 1u << (type % 32);
    const std::uint32_t area = BuildingAreaOf(&id);
    AiList& list = CurrentLand()->lists[owner];
    AiAreaNode* node = FindAreaNode(list, static_cast<std::uint16_t>(area));
    if (node != nullptr) {
        ++(&node->value.building_counts[0])[type];
        ++node->value.sites;
    }
    switch (type) {
    case Value(B::Woodcutter):
        Refill(self, player, area, B::Forester);
        Refill(self, player, area, B::Sawmill);
        break;
    case Value(B::CoalMine):
        Refill(self, player, area, B::Toolsmith);
        Refill(self, player, area, B::Weaponsmith);
        Refill(self, player, area, B::IronSmelter);
        Refill(self, player, area, B::GoldSmelter);
        break;
    case Value(B::IronMine): Refill(self, player, area, B::IronSmelter); break;
    case Value(B::IronSmelter):
        Refill(self, player, area, B::Toolsmith);
        Refill(self, player, area, B::Weaponsmith);
        break;
    case Value(B::SmallTower):
        // Without a record of the area this writes the list head's bytes
        // (a full node block), as the original does.
        (node != nullptr ? node : reinterpret_cast<AiAreaNode*>(list.head))->value.unk018 = 0;
        break;
    case Value(B::Mill): case Value(B::Waterworks): Refill(self, player, area, B::Bakery); break;
    case Value(B::Bakery): Refill(self, player, area, B::Mill); break;
    case Value(B::Farm):
        Refill(self, player, area, B::Mill);
        Refill(self, player, area, B::Bakery);
        break;
    default:
        break;
    }
}

// 0x554a62..0x554c34 (event 4): an area's owner may have changed; the
// records follow the area's current owner.
void OnAreaChanged(AiLandEvaluation* self, std::uint32_t area) {
    std::uint32_t found = kNoPlayer;  // the player with a record of the area
    AiAreaNode* node = nullptr;
    for (std::uint32_t p = 0; p < std::size(self->lists); ++p) {
        node = FindAreaNode(self->lists[p], static_cast<std::uint16_t>(area));
        if (node != nullptr) {
            found = p;
            break;
        }
    }
    const std::uint32_t owner = g_aiWorld.get()->areas[area & 0xffff].player;
    if (owner == kNoPlayer) {
        if (found != kNoPlayer && IsComputerPlayer(found)) {
            RemoveArea(self, area, found);
        }
        return;
    }
    if (found == kNoPlayer) {
        if (IsComputerPlayer(owner) && AreaPopulation(&area) != 0) {
            AddArea(self, area, owner);
        }
        return;
    }
    if (owner == found) {
        if (!IsComputerPlayer(owner)) {
            return;
        }
        if (AreaPopulation(&area) != 0) {
            using FillFn = void(__fastcall*)(AiLandEvaluation*, void*, AiArea*, std::uint32_t);
            reinterpret_cast<FillFn>(kFillAreaSiteLists)(self, nullptr, &node->value, owner);
            using CountFn = std::uint32_t(__fastcall*)(const void* player, const void* area);
            reinterpret_cast<CountFn>(kCountNodeBuildings)(&owner, &area);
        } else {
            RemoveArea(self, area, owner);
        }
        return;
    }
    if (IsComputerPlayer(found)) {
        RemoveArea(self, area, found);
    }
    if (IsComputerPlayer(owner) && AreaPopulation(&area) != 0) {
        AddArea(self, area, owner);
    }
}

// 0x554c39..0x554d49 (event 5): a conquered building moves from its old
// owner's bitset to its new owner's.
void OnOwnerChanged(std::uint32_t id, std::uint32_t oldOwner) {
    if (BuildingIfUsed(id) == nullptr) {
        GameDebugLog(texts::kAiOwnerChangeBuildingGone);
        return;
    }
    const layout::Building& building = g_aiWorld.get()->buildings[id & 0xffff];
    const std::uint32_t type = building.art;
    const std::uint32_t owner = building.owner;
    AiGridCell* cell = GridCellOfTile(CurrentLand(), building.x, building.y);
    if (IsComputerPlayer(oldOwner)) {
        BuildingTypeWord(cell, oldOwner, type) &= ~(1u << (type % 32));
    }
    if (IsComputerPlayer(owner)) {
        BuildingTypeWord(cell, owner, type) |= 1u << (type % 32);
    }
}

// 0x554d4e..0x554ec5 (event 6 after event 3's place): a building of `type`
// is gone. Its bit is cleared unless another building of the type stands in
// the 8 x 8 block of the place; its area record's count goes down.
void OnBuildingGone(AiLandEvaluation* self, std::uint32_t type, std::uint32_t player, std::uint32_t x,
                    std::uint32_t y) {
    AiGridCell* cell = GridCellOfTile(self, x, y);
    CGameData* world = g_aiWorld;
    const std::uint32_t left = (x & 0xffff) & ~static_cast<std::uint32_t>(kCellTiles - 1);
    const std::uint32_t top = (y & 0xffff) & ~static_cast<std::uint32_t>(kCellTiles - 1);
    bool another = false;
    std::uint32_t tx = 0;
    std::uint32_t ty = 0;
    for (std::int32_t i = 0; i < kCellTiles; ++i) {
        ty = top + i;
        for (std::int32_t j = 0; j < kCellTiles; ++j) {
            tx = left + j;
            const layout::Tile& tile = TileAtOffset(world, TileOffset(tx & 0xffff, ty & 0xffff));
            if (tile.object_number != 0 && (tile.flags & layout::kTileHasBuilding) != 0 &&
                world->buildings[tile.object_number].art == type) {
                GameDebugLog(texts::kAiSameTypeStillThere);
                another = true;
            }
        }
    }
    if (!another) {
        BuildingTypeWord(cell, player, type) &= ~(1u << (type % 32));
    }
    // The area at the block's last tile (the loops' last position).
    using AreaFn = std::uint32_t(__fastcall*)(std::uint32_t x, std::uint32_t y);
    const auto area = static_cast<std::uint16_t>(reinterpret_cast<AreaFn>(kAreaAlongRow)(tx, ty));
    AiAreaNode* node = FindAreaNode(self->lists[player], area);
    if (node == nullptr) {
        return;
    }
    std::uint16_t& count = (&node->value.building_counts[0])[type];  // (not range-checked)
    --count;
    if (another && count == 0) {
        GameDebugLog(texts::kAiDemolitionMixup);
    }
}

// 0x554eca..0x554fcb (event 7): a woodcutter or stonecutter gets a new work
// area, a mine is torn down (G: nothing left to cut or mine).
void OnWorkerIdle(std::uint32_t id) {
    const layout::Building* building = BuildingIfUsed(id);
    if (building == nullptr || !IsComputerPlayer(building->owner)) {
        return;
    }
    const auto owner = static_cast<std::uint8_t>(building->owner);
    const std::uint32_t x = building->x;
    const std::uint32_t y = building->y;
    Reevaluate(x, y);
    switch (building->art) {
    case Value(B::Woodcutter): SetWorkAreaWith(kFindWoodcutterWorkArea, building, id, owner, x, y); break;
    // The farm finder for a stonecutter (quirk kept; FindStonecutterWorkArea
    // is 0x59a8d0).
    case Value(B::Stonecutter): SetWorkAreaWith(kFindFarmWorkArea, building, id, owner, x, y); break;
    case Value(B::CoalMine): case Value(B::GoldMine): case Value(B::IronMine): case Value(B::SulfurMine):
    case Value(B::GemMine):
        DestroyBuilding(id & 0xffff, owner);
        break;
    default:
        break;
    }
}

}  // namespace

using ProcessPendingEventsFn = std::uint32_t(__fastcall*)(AiLandEvaluation* self, void* edx);
using OnBuildingFinishedFn = void(__fastcall*)(std::uint32_t id);

std::uint32_t __fastcall AI_CEvaLand_ProcessPendingEvents(AiLandEvaluation* self, void* edx);
void __fastcall AI_OnBuildingFinished(std::uint32_t id);

S3RE_HOOK(0x00554980, AI_CEvaLand_ProcessPendingEvents, ProcessPendingEventsFn,
          Original_AI_CEvaLand_ProcessPendingEvents, AI_CEvaLand_ProcessPendingEvents);
S3RE_HOOK(0x00573890, AI_OnBuildingFinished, OnBuildingFinishedFn, Original_AI_OnBuildingFinished,
          AI_OnBuildingFinished);

// Purpose: Brings the computer players' land evaluation up to date with what
// happened in the world since the last step (buildings finished, placed,
// conquered or gone, areas changing owner); stops after a new site of a
// computer player (returns 1) so that only one is handled per step.
std::uint32_t __fastcall AI_CEvaLand_ProcessPendingEvents(AiLandEvaluation* self, void* /*edx*/) {
    layout::Ring1000Indices& ring = g_aiWorld.get()->pending_event_index;
    // Event 3's place for the following event 6 (uninitialized stack slots
    // in the original until a 3 comes; CrushBuilding always queues 3 then 6).
    std::uint32_t goneX = 0;
    std::uint32_t goneY = 0;
    for (std::uint32_t index = ring.read; g_aiWorld.get()->pending_event_index.write != index; index = ring.read) {
        const layout::PendingEvent& entry = g_aiWorld.get()->pending_events[index];
        const std::int32_t type = entry.type;
        const auto a = static_cast<std::uint32_t>(entry.a);
        const auto b = static_cast<std::uint32_t>(entry.b);
        ring.read = index + 1;
        if (index + 1 >= kPendingEvents) {
            ring.read = 0;
        }
        switch (static_cast<AiEvent>(type)) {
        case AiEvent::BuildingFinished:
            reinterpret_cast<OnBuildingFinishedFn>(kOnBuildingFinished)(a);
            break;
        case AiEvent::SitePlaced: {
            const auto* site = BuildingIfUsed(a);
            if (site == nullptr) {
                GameDebugLog(texts::kAiSiteBuildingGone);
            } else if (IsComputerPlayer(site->owner)) {
                OnSitePlaced(self, a, *site);
                return 1;
            }
            break;
        }
        case AiEvent::BuildingGoneAt:
            goneX = a;
            goneY = b;
            break;
        case AiEvent::AreaChanged: OnAreaChanged(self, a); break;
        case AiEvent::OwnerChanged: OnOwnerChanged(a, b); break;
        case AiEvent::BuildingGone: OnBuildingGone(self, a, b, goneX, goneY); break;
        case AiEvent::WorkerIdle: OnWorkerIdle(a); break;
        case AiEvent::Reevaluate: Reevaluate(a, b); break;
        default: break;
        }
    }
    return 0;
}

// Purpose: A computer player's building is finished: the area record's site
// count and the type's block go, and the building is set up (work area of
// fields and fisheries, the garrison of towers and castles, tool goods of a
// toolsmith, goods for residences; a gem mine tears down the player's gold
// mines and gold smelters).
void __fastcall AI_OnBuildingFinished(std::uint32_t id) {
    const layout::Building* building = BuildingIfUsed(id);
    if (building == nullptr) {
        GameDebugLog(texts::kAiFinishedBuildingGone);
        return;
    }
    const std::uint32_t owner = building->owner;
    if (!IsComputerPlayer(owner)) {
        return;
    }
    const std::uint32_t x = building->x;
    const std::uint32_t y = building->y;
    const std::uint32_t type = building->art;
    const std::uint32_t area = BuildingAreaOf(&id);
    // 0x573905..0x57394c: every record of the area (the list re-read from
    // g_aiLandEvaluation each time).
    auto* const head = reinterpret_cast<AiAreaNode*>(CurrentLand()->lists[owner].head);
    for (AiAreaNode* node = head->next; node != reinterpret_cast<AiAreaNode*>(CurrentLand()->lists[owner].head);
         node = node->next) {
        if (node->value.area == static_cast<std::uint16_t>(area)) {
            (&node->value.type_blocked[0])[type] = 0;  // (type not range-checked)
            if (node->value.sites != 0) {
                --node->value.sites;
            }
        }
    }
    const auto ownerByte = static_cast<std::uint8_t>(owner);
    switch (type) {
    case Value(B::Toolsmith):
        reinterpret_cast<std::uint32_t(__fastcall*)(const void*)>(kClearToolGoodsSettings)(&id);
        break;
    case Value(B::Vineyard): SetWorkAreaWith(kFindVineyardWorkArea, building, id, ownerByte, x, y); break;
    case Value(B::LargeTower): case Value(B::Castle):
        reinterpret_cast<std::uint32_t(__fastcall*)(std::uint32_t, std::uint32_t, std::uint32_t)>(kChangeGarrison)(
            owner, id & 0xffff, 1);
        break;
    case Value(B::Farm): SetWorkAreaWith(kFindFarmWorkArea, building, id, ownerByte, x, y); break;
    case Value(B::Fishery): SetWorkAreaWith(kFindFisheryWorkArea, building, id, ownerByte, x, y); break;
    case Value(B::MediumResidence): EnsureGoodsSupply(Good::Hammer, ownerByte, &area); break;
    case Value(B::LargeResidence):
        EnsureGoodsSupply(Good::Hammer, ownerByte, &area);
        EnsureGoodsSupply(Good::Shovel, ownerByte, &area);
        break;
    case Value(B::Waterworks): SetWorkAreaWith(kFindWaterworksWorkArea, building, id, ownerByte, x, y); break;
    case Value(B::RiceFarm): SetWorkAreaWith(kFindRiceFarmWorkArea, building, id, ownerByte, x, y); break;
    case Value(B::GemMine): {
        // 0x573b38: every building of the owner's that is a gold mine or a
        // gold smelter is torn down.
        std::uint32_t error = 0;
        std::uint32_t other = 0;
        do {
            ++other;
            using GetFn = const layout::Building*(__fastcall*)(std::uint32_t id, std::uint32_t* error);
            const layout::Building* candidate = reinterpret_cast<GetFn>(kGetBuilding)(other, &error);
            if (candidate != nullptr && candidate->owner == ownerByte &&
                (candidate->art == Value(B::GoldMine) || candidate->art == Value(B::GoldSmelter))) {
                DestroyBuilding(other, ownerByte);
            }
        } while (error == 0);
        break;
    }
    default:
        break;
    }
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


// ProcessPendingEvents and OnBuildingFinished: the test map and buildings
// (above) with tiles marking the buildings, players 0..2 (computer players
// mostly), areas 0..4 owned by players 0..2 or nobody; two CEvaLand
// objects with their own copies of the grid and the same lists for
// players 0..2; a ring of 1..10 events (types 0..9; a 3 always followed by
// a 6; sometimes across the ring's end). Compared: the result, the ring's
// read index, every list's records, the grids, the commands, DebugLog calls
// and log lines.

namespace {

bool PointsIntoRecord(const AiArea& record) {
    const auto* tree = record.site_tree_of_type[Value(B::Woodcutter)];
    return tree >= &record.site_trees[0] && tree < &record.site_trees[0] + std::size(record.site_trees);
}

std::vector<std::uint8_t> AllLists(const AiLandEvaluation& land) {
    std::vector<std::uint8_t> out;
    for (const AiList& list : land.lists) {
        out.push_back(static_cast<std::uint8_t>(list.count));
        const auto* head = reinterpret_cast<const AiAreaNode*>(list.head);
        for (const AiAreaNode* node = head->next; node != head; node = node->next) {
            const std::vector<std::uint8_t> record = RecordContents(node->value, PointsIntoRecord(node->value));
            out.insert(out.end(), record.begin(), record.end());
        }
    }
    return out;
}

void MarkBuildingTiles(test::Rng& rng, CGameData* world) {
    for (std::uint32_t id = 1; id <= world->building_high_water; ++id) {
        const layout::Building& b = world->buildings[id];
        layout::Tile& tile = world->tiles[b.x][b.y];
        tile.object_number = static_cast<std::uint16_t>(rng.Chance(1, 5) ? 0 : id);
        tile.flags = static_cast<std::uint8_t>(rng.Chance(1, 5) ? tile.flags & ~layout::kTileHasBuilding
                                                                : tile.flags | layout::kTileHasBuilding);
    }
}

std::uint32_t RandomBuildingId(test::Rng& rng, const CGameData* world) {
    return rng.Chance(1, 15) ? rng.Below(4000) : 1 + rng.Below(world->building_high_water + 2);
}

void QueueEvents(test::Rng& rng, CGameData* world) {
    layout::Ring1000Indices& ring = world->pending_event_index;
    ring.read = rng.Chance(1, 4) ? 995 + rng.Below(5) : rng.Below(1000);
    std::uint32_t write = ring.read;
    const std::uint32_t count = rng.Below(11);
    for (std::uint32_t k = 0; k < count; ++k) {
        layout::PendingEvent event{};
        event.type = static_cast<std::int32_t>(rng.Below(10));
        switch (event.type) {
        case 1: case 2: case 7:
            event.a = static_cast<std::int32_t>(RandomBuildingId(rng, world));
            if (rng.Chance(1, 8)) {
                event.a |= static_cast<std::int32_t>(rng.Next32() & 0xffff0000u);
            }
            break;
        case 3: case 6: {
            // 3 (the place) then 6 (type, player)
            event.type = 3;
            const layout::Building& b = world->buildings[1 + rng.Below(world->building_high_water + 1)];
            event.a = rng.Chance(1, 2) ? b.x : static_cast<std::int32_t>(rng.Below(kTestMap));
            event.b = rng.Chance(1, 2) ? b.y : static_cast<std::int32_t>(rng.Below(kTestMap));
            world->pending_events[write] = event;
            write = (write + 1) % 1000;
            event.type = 6;
            event.a = rng.Chance(1, 2) ? b.art : static_cast<std::int32_t>(rng.Below(kBuildingTypeCount));
            event.b = static_cast<std::int32_t>(rng.Below(3));
            break;
        }
        case 4:
            event.a = static_cast<std::int32_t>(rng.Below(5) | (rng.Chance(1, 8) ? rng.Next32() & 0xffff0000u : 0));
            break;
        case 5:
            event.a = static_cast<std::int32_t>(RandomBuildingId(rng, world));
            event.b = static_cast<std::int32_t>(rng.Below(4));
            break;
        default:
            event.a = static_cast<std::int32_t>(rng.Below(kTestMap + 8));
            event.b = static_cast<std::int32_t>(rng.Below(kTestMap + 8));
            break;
        }
        world->pending_events[write] = event;
        write = (write + 1) % 1000;
    }
    ring.write = write;
}

struct EventFns {
    ProcessPendingEventsFn process;
    OnBuildingFinishedFn finished;
};

void CompareEvents(test::Context& ctx, const EventFns& ours, int cases) {
    const EventFns original{Original_AI_CEvaLand_ProcessPendingEvents, Original_AI_OnBuildingFinished};
    Harness harness;
    DebugLogRecorder logs;
    CGameData* world = harness.world();
    const std::uint32_t refs = g_aiSiteTreeNilRefs;
    int handled = 0;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        if (!S3RE_EXPECT_EQ(g_aiSiteTreeNilRefs.get(), refs)) {
            ctx.Fail(__FILE__, __LINE__, "  before case %d", i);
        }
        if (i % 10 == 0) {
            SetUpMap(rng, world, harness.cells());
        }
        SetUpEconomy(rng, world);
        MarkBuildingTiles(rng, world);
        for (std::uint32_t p = 0; p < 4; ++p) {
            world->players[p].active = static_cast<std::int32_t>(rng.Chance(1, 4) ? rng.Below(3) : 2);
            world->players[p].race = static_cast<std::int32_t>(rng.Below(4));
        }
        for (std::uint32_t a = 0; a < 5; ++a) {
            world->areas[a].player = static_cast<std::uint8_t>(rng.Chance(1, 5) ? 0xff : rng.Below(3));
        }
        std::vector<AiGridCell> cellsA = harness.cells();
        std::vector<AiGridCell> cellsB = harness.cells();
        TestLand a(cellsA), b(cellsB);
        a.land.width = b.land.width = kGridSide;
        a.land.height = b.land.height = kGridSide;
        a.land.cell_count = b.land.cell_count = kGridSide * kGridSide;
        a.land.tiles_per_cell = b.land.tiles_per_cell = 64;  // (EvaluateCell divides by it)
        auto* record = static_cast<AiArea*>(msvc6::GameOperatorNew(sizeof(AiArea)));
        for (std::uint32_t p = 0; p < 3; ++p) {
            for (std::uint32_t k = rng.Below(3); k > 0; --k) {
                MakeRecord(rng, record, static_cast<std::uint16_t>(rng.Below(4)));
                a.Append(p, record);
                b.Append(p, record);
                DestroyRecord(record);
            }
        }
        msvc6::GameOperatorDelete(record);
        const bool direct = rng.Chance(1, 4);
        const std::uint32_t id = RandomBuildingId(rng, world);
        QueueEvents(rng, world);
        const layout::Ring1000Indices ring = world->pending_event_index;
        std::uint32_t ra = 0, rb = 0;
        const Captured ca = harness.Capture([&] {
            g_aiLandEvaluation = reinterpret_cast<std::uint32_t>(&a.land);
            if (direct) {
                original.finished(id);
            } else {
                ra = original.process(&a.land, nullptr);
            }
        });
        const std::vector<DebugLogCall> logsA = logs.Take();
        const layout::Ring1000Indices ringA = world->pending_event_index;
        world->pending_event_index = ring;
        const Captured cb = harness.Capture([&] {
            g_aiLandEvaluation = reinterpret_cast<std::uint32_t>(&b.land);
            if (direct) {
                ours.finished(id);
            } else {
                rb = ours.process(&b.land, nullptr);
            }
        });
        const std::vector<DebugLogCall> logsB = logs.Take();
        handled += ca.messages.empty() && ra == 0 ? 0 : 1;
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT_EQ(ringA.read, world->pending_event_index.read);
        same &= S3RE_EXPECT(ca == cb);
        same &= S3RE_EXPECT(logsA == logsB);
        same &= S3RE_EXPECT_MEM_EQ(cellsA.data(), cellsB.data(), cellsA.size() * sizeof(AiGridCell));
        same &= S3RE_EXPECT(AllLists(a.land) == AllLists(b.land));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, %s, ring %u..%u", i, direct ? "finished" : "events", ring.read,
                     ring.write);
        }
    }
    if (cases >= 500 && !S3RE_EXPECT(handled >= cases / 5)) {
        ctx.Fail(__FILE__, __LINE__, "  only %d cases sent commands or stopped", handled);
    }
}

std::uint32_t __fastcall WrongProcessEvents(AiLandEvaluation* self, void* edx) {
    // Wrong: the stonecutter's work area from its own finder.
    CGameData* world = g_aiWorld;
    layout::Ring1000Indices& ring = world->pending_event_index;
    for (std::uint32_t k = ring.read; k != ring.write; k = (k + 1) % 1000) {
        layout::PendingEvent& event = world->pending_events[k];
        if (event.type == 7) {
            const layout::Building* b = BuildingIfUsed(static_cast<std::uint32_t>(event.a));
            if (b != nullptr && b->art == Value(B::Stonecutter) && IsComputerPlayer(b->owner)) {
                const std::uint32_t result = AI_CEvaLand_ProcessPendingEvents(self, edx);
                SetWorkAreaWith(0x0059a8d0, b, static_cast<std::uint32_t>(event.a), b->owner, b->x, b->y);
                return result;
            }
        }
    }
    return AI_CEvaLand_ProcessPendingEvents(self, edx);
}

void __fastcall WrongFinished(std::uint32_t id) {
    // Wrong: the site count isn't lowered.
    const layout::Building* b = BuildingIfUsed(id);
    std::vector<std::uint16_t> sites;
    if (b != nullptr) {
        auto* head = reinterpret_cast<AiAreaNode*>(CurrentLand()->lists[b->owner].head);
        for (AiAreaNode* node = head->next; node != head; node = node->next) {
            sites.push_back(node->value.sites);
        }
    }
    AI_OnBuildingFinished(id);
    if (b != nullptr) {
        auto* head = reinterpret_cast<AiAreaNode*>(CurrentLand()->lists[b->owner].head);
        std::size_t k = 0;
        for (AiAreaNode* node = head->next; node != head && k < sites.size(); node = node->next) {
            node->value.sites = sites[k++];
        }
    }
}

}  // namespace

S3RE_TEST(AiAreaPendingEvents_MatchOriginal) {
    CompareEvents(ctx, {&AI_CEvaLand_ProcessPendingEvents, &AI_OnBuildingFinished}, 1500);
}

S3RE_TEST(AiAreaPendingEvents_ComparisonDetectsWrongImplementation) {
    {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareEvents(quiet, {&WrongProcessEvents, &AI_OnBuildingFinished}, 1500);
        S3RE_EXPECT(quiet.failures() > 0);
    }
    {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareEvents(quiet, {&AI_CEvaLand_ProcessPendingEvents, &WrongFinished}, 1500);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

}  // namespace s3re::game
