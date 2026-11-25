// The computer players' "force production" and "emergency powers" in an
// economy area, and the tools for their workers (computer players, session
// 55; Rainer\compGlobs.cpp, CompMapWorkerAgent.cpp and CompHouseReq.cpp;
// re/computer_players.md "Forced production"). Their original names come
// from the DebugLog / OutputDebugStringA texts ("COMP_ForceProduction -
// EcoSector gehoert nicht dem Spieler !?!": the economy sector doesn't
// belong to the player).
//
// The per-area node (AiPlayerData, found in g_aiLandEvaluation's list of
// the player by its area word, inlined like AI::FindNode 0x573be0) holds
// the flags; the changes reach the game as commands (ai_commands.cpp).
//
//   0x00572fc0 COMP_ForceProduction(const u16* player, const u16* area,
//              stack const u8* on) fastcall, ret 4: sprintf + OutputDebugStringA
//              "FORCEPRODUCTION AUS (sic: for on) / AN (for off) player: %u,
//              ecosector %u"; the area's node, or OutputDebugStringA "...
//              EcoSector gehoert nicht dem Spieler" and return;
//              node->force_production = *on. Then SetAreaCondition
//              commands (AI::SendSetAreaCondition 0x58ea30; kind, value):
//              on: with less than 5 coal (AI_AreaGoodsAmount) (3, 16), with
//              more than 3 iron (1, 4), Asians (5, 4) (6, 4), then (2, 4)
//              (4, 8); then, with less than 3 iron, (7, 16) (9, 4) (8, 8)
//              and per race (10 / 11 / 12 / 47, 4). Off: (4, 16) (8, 16)
//              (1, 16) (2, 12) (3, 4) (7, 4) (9, 4) and per race Roman
//              (10, 8), Egyptian (11, 8), Asian (12, 4) (5, 12) (6, 4),
//              Amazon (47, 4). The race is the low word of
//              Player::race (players not range-checked); other races send
//              no race-specific command. EAX is not used by the callers.
//   0x00573670 COMP_EmergencyPowersActWood(const u16* player, const u16*
//              area, stack const u8* on) fastcall, ret 4: the area's node
//              (else OutputDebugStringA "COMP_EmergencyPowersActWood -
//              EcoSector ..." and return), node->short_of_wood = *on; then
//              every construction site of the player (byte owner widened
//              to a word) in the area (AI_BuildingArea) that still misses
//              boards (BuildingSiteBytes::boards_missing), except
//              stonecutters, woodcutters, sawmills, foresters and small
//              towers, gets SetBuildingWorking (0x58eac0) with working =
//              !*on (the wood goes to the wood industry). EAX not used.
//   0x00573760 AI::EnsureGoodsSupply(const u8* goods, const u8* player,
//              stack const u16* area) fastcall, ret 4: 1 if the area has the
//              goods (AX of AI_AreaGoodsAmount); 0 if the area has no
//              finished toolsmith (AI_CountBuildings, its area quirk
//              included); 1 if Area::settings1[goods] is set already; else
//              COMP_ForceProduction(on) and AI::SendRaiseGoodsPriority, 1.
//   0x0059bf70 AI::OrderMissingTools(const u16* player, const u16* area)
//              fastcall: over the buildings (AI_GetBuilding until the error
//              flag): the player's (byte widened) finished buildings in the
//              area (AI::AreaAlongRow of position x / y +2 / +4) without a
//              worker (+8) whose worker needs a tool
//              (g_aiBuildingToolAndWorker), with no idle worker of that
//              settler type in the area (AI_CountAreaSettlersOfType), the
//              tool not in stock and a finished toolsmith there: if
//              settings1[tool] is 0, SendRaiseGoodsPriority for the tool
//              and, unless forced already (AI::IsForceProduction),
//              COMP_ForceProduction(on); either way it counts as needed.
//              Nothing needed and forced: COMP_ForceProduction(off). EAX
//              not used (its caller 0x550f80 overwrites AX and passes
//              the dword to a word reader).
//   0x0057abc0 LOCAL_WARE_REQUEST_AGENT::CheckWorkerOrTool(note) thiscall
//              (`this` unused), ret 4: the note's building type (+0x1c):
//              1 if it needs no tool, or a worker of its settler type is in
//              the area of the note's position (AI::AreaAlongRow; the area
//              dword is passed by address), or AI::EnsureGoodsSupply(tool,
//              g_aiPlayerNumber, area) says 1; else OutputDebugStringA
//              "KEIN PASSENDES WERKZEUG VORHANDEN" (no suitable tool) and 0.
//   0x0057ab70 LOCAL_WARE_REQUEST_AGENT::WorkOnNote(note) thiscall, slot
//              4, ret 4: CComputerPlayer::DeactivateNoteOf(g_aiPlayer,
//              note), BLACK_BOARD_NOTE::SetFlags(note, 8), then
//              ReportToRequester(note, 100 if CheckWorkerOrTool else 0, 0).
//              AX = 100 (the upper half is ReportToRequester's).

#include "game/ai/ai_layout.h"
#include "game/enums/buildings.h"
#include "game/enums/goods.h"
#include "game/enums/races.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/texts.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using namespace ai;
using B = BuildingType;

namespace {

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kAreaGoodsAmount = 0x0059dc10;      // AI_AreaGoodsAmount (fastcall)
constexpr std::uintptr_t kCountBuildings = 0x0059dac0;       // AI_CountBuildings (fastcall)
constexpr std::uintptr_t kGetBuilding = 0x0058e170;          // AI_GetBuilding (fastcall)
constexpr std::uintptr_t kBuildingArea = 0x00599610;         // AI_BuildingArea (fastcall)
constexpr std::uintptr_t kAreaAlongRow = 0x005996c0;         // AI::AreaAlongRow (fastcall, values)
constexpr std::uintptr_t kCountAreaSettlers = 0x0059dc70;    // AI_CountAreaSettlersOfType (fastcall)
constexpr std::uintptr_t kIsForceProduction = 0x00572fa0;    // AI::IsForceProduction (fastcall)
constexpr std::uintptr_t kSetAreaCondition = 0x0058ea30;     // AI::SendSetAreaCondition (fastcall)
constexpr std::uintptr_t kSetBuildingWorking = 0x0058eac0;   // AI::SendSetBuildingWorking (fastcall)
constexpr std::uintptr_t kRaiseGoodsPriority = 0x0058e7d0;   // AI::SendRaiseGoodsPriority (fastcall)
constexpr std::uintptr_t kForceProduction = 0x00572fc0;      // COMP_ForceProduction (fastcall)
constexpr std::uintptr_t kEnsureGoodsSupply = 0x00573760;    // AI::EnsureGoodsSupply (fastcall)
constexpr std::uintptr_t kDeactivateNoteOf = 0x00551610;     // CComputerPlayer::DeactivateNoteOf (thiscall)
constexpr std::uintptr_t kSetFlags = 0x00582440;             // BLACK_BOARD_NOTE::SetFlags (thiscall)
constexpr std::uintptr_t kReportToRequester = 0x00582480;    // BLACK_BOARD_NOTE::ReportToRequester (thiscall)
constexpr std::uintptr_t kCheckWorkerOrTool = 0x0057abc0;    // LOCAL_WARE_REQUEST_AGENT::CheckWorkerOrTool

constexpr std::size_t kTextBytes = 256;  // the original's stack buffer (0x11c bytes of locals)
constexpr std::uint32_t kEnoughCoal = 5;
constexpr std::uint32_t kEnoughIron = 3;
constexpr std::uint16_t kNoteFlagWorkedOn = 8;  // BLACK_BOARD_NOTE::SetFlags(8) before the report
constexpr std::uint32_t kToolFound = 100;
constexpr std::uint16_t kWorkOnNoteResult = 100;

using ForceProductionFn = void(__fastcall*)(const std::uint16_t* player, const std::uint16_t* area,
                                            const std::uint8_t* on);

// The player's node for an area (AI::FindNode 0x573be0, inlined in both
// COMP functions), or null.
AiPlayerData* AreaNode(std::uint16_t player, std::uint16_t area) {
    const auto* land = reinterpret_cast<const AiLandEvaluation*>(g_aiLandEvaluation.get());
    const AiListNode* head = land->lists[player].head;  // (not range-checked)
    for (auto* node = reinterpret_cast<AiPlayerData*>(head->next); node != reinterpret_cast<const void*>(head);
         node = reinterpret_cast<AiPlayerData*>(node->next)) {
        if (node->area == area) {
            return node;
        }
    }
    return nullptr;
}

std::uint32_t GoodsInArea(std::uint8_t goods, const std::uint16_t* area) {
    using Fn = std::uint32_t(__fastcall*)(const std::uint8_t* goods, const std::uint16_t* area);
    return reinterpret_cast<Fn>(kAreaGoodsAmount)(&goods, area);
}

bool HasToolsmith(std::uint8_t player, std::uint16_t area) {
    using Fn = std::uint32_t(__fastcall*)(std::uint32_t player, std::uint32_t type, std::uint32_t area);
    return static_cast<std::uint16_t>(reinterpret_cast<Fn>(kCountBuildings)(player, Value(B::Toolsmith), area)) != 0;
}

const layout::Building* BuildingOf(std::uint32_t id, std::uint32_t* error) {
    using Fn = const layout::Building*(__fastcall*)(std::uint32_t id, std::uint32_t* error);
    return reinterpret_cast<Fn>(kGetBuilding)(id, error);
}

std::uint16_t AreaAlongRow(std::uint16_t x, std::uint16_t y) {
    using Fn = std::uint32_t(__fastcall*)(std::uint32_t x, std::uint32_t y);
    return static_cast<std::uint16_t>(reinterpret_cast<Fn>(kAreaAlongRow)(x, y));
}

std::uint16_t IdleWorkersInArea(std::uint8_t type, const void* area) {
    using Fn = std::uint32_t(__fastcall*)(const std::uint8_t* type, const void* area);
    return static_cast<std::uint16_t>(reinterpret_cast<Fn>(kCountAreaSettlers)(&type, area));
}

void SetAreaCondition(std::uint8_t player, const std::uint16_t* area, std::uint8_t kind, std::uint8_t value) {
    using Fn = std::uint32_t(__fastcall*)(const void* player, const void* area, const void* kind, const void* value);
    reinterpret_cast<Fn>(kSetAreaCondition)(&player, area, &kind, &value);
}

void RaiseGoodsPriority(const std::uint8_t* goods, const std::uint8_t* player, const std::uint16_t* area) {
    using Fn = std::uint32_t(__fastcall*)(const void* goods, const void* player, const void* area);
    reinterpret_cast<Fn>(kRaiseGoodsPriority)(goods, player, area);
}

bool IsForceProduction(std::uint16_t player, std::uint16_t area) {
    using Fn = std::uint32_t(__fastcall*)(std::uint32_t player, std::uint32_t area);
    return reinterpret_cast<Fn>(kIsForceProduction)(player, area) != 0;
}

void ForceProduction(const std::uint16_t* player, const std::uint16_t* area, bool on) {
    const std::uint8_t flag = on ? 1 : 0;
    reinterpret_cast<ForceProductionFn>(kForceProduction)(player, area, &flag);
}

void LogDebugString(const char* text) { g_importOutputDebugStringA.get()(text); }

// (kind, value) of one SetAreaCondition command.
struct AreaCondition {
    std::uint8_t kind;
    std::uint8_t value;
};

}  // namespace

using EmergencyFn = void(__fastcall*)(const std::uint16_t* player, const std::uint16_t* area, const std::uint8_t* on);
using EnsureGoodsFn = std::uint32_t(__fastcall*)(const std::uint8_t* goods, const std::uint8_t* player,
                                                 const std::uint16_t* area);
using OrderToolsFn = void(__fastcall*)(const std::uint16_t* player, const std::uint16_t* area);
using NoteCheckFn = std::uint32_t(__fastcall*)(void* self, void* edx, RequestNote* note);

void __fastcall COMP_ForceProduction(const std::uint16_t* player, const std::uint16_t* area, const std::uint8_t* on);
void __fastcall COMP_EmergencyPowersActWood(const std::uint16_t* player, const std::uint16_t* area,
                                            const std::uint8_t* on);
std::uint32_t __fastcall AI_EnsureGoodsSupply(const std::uint8_t* goods, const std::uint8_t* player,
                                              const std::uint16_t* area);
void __fastcall AI_OrderMissingTools(const std::uint16_t* player, const std::uint16_t* area);
std::uint32_t __fastcall LOCAL_WARE_REQUEST_AGENT_CheckWorkerOrTool(void* self, void* edx, RequestNote* note);
std::uint32_t __fastcall LOCAL_WARE_REQUEST_AGENT_WorkOnNote(void* self, void* edx, RequestNote* note);

S3RE_HOOK(0x00572fc0, COMP_ForceProduction, ForceProductionFn, Original_COMP_ForceProduction, COMP_ForceProduction);
S3RE_HOOK(0x00573670, COMP_EmergencyPowersActWood, EmergencyFn, Original_COMP_EmergencyPowersActWood,
          COMP_EmergencyPowersActWood);
S3RE_HOOK(0x00573760, AI_EnsureGoodsSupply, EnsureGoodsFn, Original_AI_EnsureGoodsSupply, AI_EnsureGoodsSupply);
S3RE_HOOK(0x0059bf70, AI_OrderMissingTools, OrderToolsFn, Original_AI_OrderMissingTools, AI_OrderMissingTools);
S3RE_HOOK(0x0057abc0, LOCAL_WARE_REQUEST_AGENT_CheckWorkerOrTool, NoteCheckFn,
          Original_LOCAL_WARE_REQUEST_AGENT_CheckWorkerOrTool, LOCAL_WARE_REQUEST_AGENT_CheckWorkerOrTool);
S3RE_HOOK(0x0057ab70, LOCAL_WARE_REQUEST_AGENT_WorkOnNote, NoteCheckFn, Original_LOCAL_WARE_REQUEST_AGENT_WorkOnNote,
          LOCAL_WARE_REQUEST_AGENT_WorkOnNote);

namespace {

// 0x573074..0x573245: the commands when production is forced on.
void ForceOn(const std::uint16_t* player, const std::uint16_t* area) {
    const std::uint32_t coal = GoodsInArea(Value(Good::Coal), area);
    const std::uint32_t iron = GoodsInArea(Value(Good::Iron), area);
    const CGameData* world = g_aiWorld;
    const auto race = static_cast<std::uint16_t>((&world->players[0])[*player].race);  // (read again)
    const auto playerByte = static_cast<std::uint8_t>(*player);
    const auto set = [&](AreaCondition condition) {
        SetAreaCondition(playerByte, area, condition.kind, condition.value);
    };
    if (coal < kEnoughCoal) {
        set({3, 16});
        if (iron > kEnoughIron) {
            set({1, 4});
        }
        if (race == Value(Race::Asian)) {
            set({5, 4});
            set({6, 4});
        }
        set({2, 4});
        set({4, 8});
    }
    if (iron >= kEnoughIron) {
        return;
    }
    set({7, 16});
    set({9, 4});
    set({8, 8});
    switch (race) {  // jump table 0x573534
    case Value(Race::Roman): set({10, 4}); break;
    case Value(Race::Egyptian): set({11, 4}); break;
    case Value(Race::Asian): set({12, 4}); break;
    case Value(Race::Amazon): set({47, 4}); break;
    default: break;
    }
}

// 0x57330b..0x573530: the commands when it is switched off.
void ForceOff(std::uint8_t player, const std::uint16_t* area, std::uint16_t race) {
    const auto set = [&](AreaCondition condition) {
        SetAreaCondition(player, area, condition.kind, condition.value);
    };
    for (AreaCondition condition : {AreaCondition{4, 16}, AreaCondition{8, 16}, AreaCondition{1, 16},
                                    AreaCondition{2, 12}, AreaCondition{3, 4}, AreaCondition{7, 4},
                                    AreaCondition{9, 4}}) {
        set(condition);
    }
    switch (race) {  // jump table 0x573544
    case Value(Race::Roman): set({10, 8}); break;
    case Value(Race::Egyptian): set({11, 8}); break;
    case Value(Race::Asian):
        set({12, 4});
        set({5, 12});
        set({6, 4});
        break;
    case Value(Race::Amazon): set({47, 4}); break;
    default: break;
    }
}

}  // namespace

// Purpose: Switches "force production" of a computer player's economy area
// on or off: records it in the area's node and sets the area's production
// conditions (which ones depend on the coal and iron in stock and on the
// race).
void __fastcall COMP_ForceProduction(const std::uint16_t* player, const std::uint16_t* area,
                                     const std::uint8_t* on) {
    char text[kTextBytes];
    using SprintfFn = int(__cdecl*)(char* buffer, const char* format, ...);
    // (the original logs "AUS" (off) when switching on and "AN" when off)
    const GameText& format = *on != 0 ? texts::kForceProductionOff : texts::kForceProductionOn;
    reinterpret_cast<SprintfFn>(0x0061f27e)(text, format.c_str(), static_cast<std::uint32_t>(*player),
                                            static_cast<std::uint32_t>(*area));
    LogDebugString(text);
    AiPlayerData* node = AreaNode(*player, *area);
    if (node == nullptr) {
        LogDebugString(texts::kForceProductionNotOwner.c_str());
        return;
    }
    node->force_production = *on;
    const CGameData* world = g_aiWorld;
    const auto race = static_cast<std::uint16_t>((&world->players[0])[*player].race);  // (not range-checked)
    if (*on != 0) {
        ForceOn(player, area);
    } else {
        ForceOff(static_cast<std::uint8_t>(*player), area, race);
    }
}

namespace {

// The sites that keep their boards in a wood emergency: the wood industry
// itself and the small tower.
bool KeepsWorking(std::uint8_t art) {
    return art == Value(B::Stonecutter) || art == Value(B::Woodcutter) || art == Value(B::Sawmill) ||
           art == Value(B::Forester) || art == Value(B::SmallTower);
}

}  // namespace

// Purpose: In a wood emergency of a computer player's economy area, stops
// (or, when it is over, restarts) the construction sites still waiting for
// boards, except those of the wood industry and small towers.
void __fastcall COMP_EmergencyPowersActWood(const std::uint16_t* player, const std::uint16_t* area,
                                            const std::uint8_t* on) {
    AiPlayerData* node = AreaNode(*player, *area);
    if (node == nullptr) {
        LogDebugString(texts::kEmergencyWoodNotOwner.c_str());
        return;
    }
    node->short_of_wood = *on;
    std::uint32_t error = 0;
    std::uint32_t id = 0;
    do {
        ++id;
        const layout::Building* building = BuildingOf(id, &error);
        if (building == nullptr || *player != building->owner || building->buildup == 0) {
            continue;
        }
        const std::uint32_t number = id;
        using BuildingAreaFn = std::uint32_t(__fastcall*)(const std::uint32_t* building);
        if (*area != static_cast<std::uint16_t>(reinterpret_cast<BuildingAreaFn>(kBuildingArea)(&number))) {
            continue;
        }
        if (KeepsWorking(building->art) || building->site.boards_missing == 0) {
            continue;
        }
        const std::uint8_t working = *on == 0 ? 1 : 0;
        const auto playerByte = static_cast<std::uint8_t>(*player);
        using Fn = std::uint32_t(__fastcall*)(const void* player, const void* building, const void* working);
        reinterpret_cast<Fn>(kSetBuildingWorking)(&playerByte, &number, &working);
    } while (error == 0);
}

// Purpose: Makes sure an economy area of a computer player gets a goods
// type (a tool): fine if it is in stock; impossible without a toolsmith;
// otherwise forces production and raises the goods' priority.
std::uint32_t __fastcall AI_EnsureGoodsSupply(const std::uint8_t* goods, const std::uint8_t* player,
                                              const std::uint16_t* area) {
    if (static_cast<std::uint16_t>(GoodsInArea(*goods, area)) != 0) {
        return 1;
    }
    if (!HasToolsmith(*player, *area)) {
        return 0;
    }
    const CGameData* world = g_aiWorld;
    if (world->areas[*area].settings1[*goods] != 0) {  // (neither index range-checked)
        return 1;
    }
    const std::uint16_t playerWord = *player;
    ForceProduction(&playerWord, area, true);
    RaiseGoodsPriority(goods, player, area);
    return 1;
}

// Purpose: Gets tools made for a computer player's buildings that stand
// without a worker for lack of a tool (raising the tool's priority and
// forcing production), and stops the forced production when no tool is
// missing any more.
void __fastcall AI_OrderMissingTools(const std::uint16_t* player, const std::uint16_t* area) {
    const CGameData* world = g_aiWorld;
    std::uint32_t error = 0;
    bool needed = false;
    std::uint32_t id = 0;
    do {
        ++id;
        const layout::Building* building = BuildingOf(id, &error);
        if (building == nullptr || *player != building->owner) {
            continue;
        }
        if (*area != AreaAlongRow(building->x, building->y) || building->buildup != 0 || building->worker != 0) {
            continue;
        }
        const AiToolAndWorker& entry = g_aiBuildingToolAndWorker[building->art];  // (art up to 255: past the table)
        const std::uint16_t tool = entry.tool;
        if (tool == 0 || IdleWorkersInArea(entry.worker, area) != 0 ||
            static_cast<std::uint16_t>(GoodsInArea(static_cast<std::uint8_t>(tool), area)) != 0 ||
            !HasToolsmith(static_cast<std::uint8_t>(*player), *area)) {
            continue;
        }
        if (world->areas[*area].settings1[tool] == 0) {
            const auto goods = static_cast<std::uint8_t>(tool);
            const auto playerByte = static_cast<std::uint8_t>(*player);
            RaiseGoodsPriority(&goods, &playerByte, area);
            if (!IsForceProduction(*player, *area)) {
                ForceProduction(player, area, true);
            }
        }
        needed = true;
    } while (error == 0);
    if (!needed && IsForceProduction(*player, *area)) {
        ForceProduction(player, area, false);
    }
}

// Purpose: Checks whether the worker of a building a computer player wants
// can start: no tool needed, a worker already waiting in the area, or the
// tool can be had (EnsureGoodsSupply); logs "no suitable tool" otherwise.
std::uint32_t __fastcall LOCAL_WARE_REQUEST_AGENT_CheckWorkerOrTool(void* /*self*/, void* /*edx*/,
                                                                     RequestNote* note) {
    const AiToolAndWorker& entry = g_aiBuildingToolAndWorker[note->building_type];  // (not range-checked)
    const std::uint16_t tool = entry.tool;
    if (tool == 0) {
        return 1;
    }
    using AreaFn = std::uint32_t(__fastcall*)(std::uint32_t x, std::uint32_t y);
    const std::uint32_t area = reinterpret_cast<AreaFn>(kAreaAlongRow)(note->x, note->y);  // passed by address
    if (IdleWorkersInArea(entry.worker, &area) != 0) {
        return 1;
    }
    const auto goods = static_cast<std::uint8_t>(tool);
    using EnsureFn = std::uint32_t(__fastcall*)(const std::uint8_t* goods, const std::uint8_t* player,
                                                const void* area);
    if (reinterpret_cast<EnsureFn>(kEnsureGoodsSupply)(&goods, &g_aiPlayerNumber.get(), &area) != 0) {
        return 1;
    }
    LogDebugString(texts::kAiNoSuitableTool.c_str());
    return 0;
}

// Purpose: The LOCAL_WARE_REQUEST_AGENT's turn on its note: takes the note
// off the active list and reports to whoever asked whether the building's
// worker can start (100) or not (0).
std::uint32_t __fastcall LOCAL_WARE_REQUEST_AGENT_WorkOnNote(void* self, void* /*edx*/, RequestNote* note) {
    using NoteFn = std::uint32_t(__fastcall*)(void* player, void* edx, RequestNote* note);
    reinterpret_cast<NoteFn>(kDeactivateNoteOf)(g_aiPlayer, nullptr, note);
    using FlagsFn = std::uint32_t(__fastcall*)(RequestNote* note, void* edx, std::uint32_t bits);
    reinterpret_cast<FlagsFn>(kSetFlags)(note, nullptr, kNoteFlagWorkedOn);
    const std::uint32_t found = reinterpret_cast<NoteCheckFn>(kCheckWorkerOrTool)(self, nullptr, note);
    using ReportFn = std::uint32_t(__fastcall*)(RequestNote* note, void* edx, std::uint32_t result,
                                                std::uint32_t value);
    const std::uint32_t report =
        reinterpret_cast<ReportFn>(kReportToRequester)(note, nullptr, found != 0 ? kToolFound : 0, 0);
    return (report & 0xffff0000u) | kWorkOnNoteResult;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// A 64 x 64 map (AI map size 64): players 0..2 with races 0..3 or any word;
// up to 40 buildings (tool users, toolsmiths, residences; owners 0..2 or
// free; finished or sites with boards missing; with or without a worker),
// tile areas 0..3; per area pile lists of coal, iron and the tools, settler
// lists of the worker types and random settings1 bytes; per player a node
// list over some of the areas. The command senders run with IsHost and
// MessageQueue::Push stubbed (Push logs the message bytes); compared: the
// commands, the nodes, the return values (and the pointed-to arguments).

namespace {

constexpr std::uintptr_t kIsHost = 0x004318a0;
constexpr std::uintptr_t kPush = 0x004309c0;
constexpr std::uint32_t kQueue = 0x5152a000;  // a fake queue (never dereferenced)
constexpr std::uint32_t kTestPlayers = 3;
constexpr std::uint16_t kTestAreas = 4;
constexpr std::uint32_t kTestMap = 64;
constexpr std::uint32_t kMaxBuildings = 40;

using Message = std::array<std::uint8_t, 12>;

struct Functions {
    ForceProductionFn force;
    EmergencyFn wood;
    EnsureGoodsFn ensure;
    OrderToolsFn tools;
    NoteCheckFn check;
};

Functions OriginalFunctions() {
    return {Original_COMP_ForceProduction, Original_COMP_EmergencyPowersActWood, Original_AI_EnsureGoodsSupply,
            Original_AI_OrderMissingTools, Original_LOCAL_WARE_REQUEST_AGENT_CheckWorkerOrTool};
}

Functions OurFunctions() {
    return {&COMP_ForceProduction, &COMP_EmergencyPowersActWood, &AI_EnsureGoodsSupply, &AI_OrderMissingTools,
            &LOCAL_WARE_REQUEST_AGENT_CheckWorkerOrTool};
}

struct SavedGlobals {
    CGameData* world = g_aiWorld;
    ComputerPlayer* player = g_aiPlayer;
    std::uint32_t land = g_aiLandEvaluation;
    std::uint8_t number = g_aiPlayerNumber;
    std::uint16_t sizeX = g_aiMapSizeX, sizeY = g_aiMapSizeY;
    ~SavedGlobals() {
        g_aiWorld = world;
        g_aiPlayer = player;
        g_aiLandEvaluation = land;
        g_aiPlayerNumber = number;
        g_aiMapSizeX = sizeX;
        g_aiMapSizeY = sizeY;
    }
};

// The building types of the test buildings: tool users, toolsmiths and
// some without a tool.
constexpr std::uint8_t kTestTypes[] = {Value(B::Woodcutter), Value(B::Stonecutter), Value(B::Sawmill),
                                       Value(B::Forester),   Value(B::Toolsmith),   Value(B::Toolsmith),
                                       Value(B::Farm),       Value(B::SmallTower),  Value(B::CoalMine),
                                       Value(B::SmallResidence)};

struct TestWorld {
    CGameData* world;
    AiLandEvaluation land{};
    AiListNode heads[kTestPlayers] = {};
    std::vector<AiPlayerData> nodes = std::vector<AiPlayerData>(kTestPlayers * kTestAreas);
    bool hasNode[kTestPlayers][kTestAreas] = {};
};

std::uint16_t PileChain(test::Rng& rng, CGameData* world, std::uint32_t& base, std::uint32_t length) {
    std::uint16_t head = 0;
    for (std::uint32_t k = 0; k < length; ++k) {
        const auto pile = static_cast<std::uint16_t>(base++);
        world->piles[pile].next = head;
        world->piles[pile].amount = static_cast<std::uint8_t>(1 + rng.Below(4));
        head = pile;
    }
    return head;
}

void SetUpWorld(test::Rng& rng, TestWorld& test) {
    CGameData* world = test.world;
    for (std::uint32_t p = 0; p < kTestPlayers; ++p) {
        world->players[p].race = static_cast<std::int32_t>(rng.Chance(1, 10) ? rng.Next32() : rng.Below(4));
    }
    const std::uint32_t count = rng.Below(kMaxBuildings);
    world->building_high_water = count;
    for (std::uint32_t x = 0; x < kTestMap; ++x) {
        for (std::uint32_t y = 0; y < kTestMap; ++y) {
            world->tiles[x][y].area = static_cast<std::uint16_t>(rng.Below(kTestAreas));
        }
    }
    for (std::uint32_t id = 1; id <= count; ++id) {
        layout::Building& building = world->buildings[id];
        building.owner = static_cast<std::uint8_t>(rng.Chance(1, 8) ? 0xff : rng.Below(kTestPlayers));
        building.art = kTestTypes[rng.Below(sizeof(kTestTypes))];
        building.buildup = static_cast<std::uint8_t>(rng.Chance(1, 3) ? 1 + rng.Below(200) : 0);
        building.site.boards_missing = static_cast<std::uint8_t>(rng.Chance(1, 3) ? 0 : 1 + rng.Below(8));
        building.worker = static_cast<std::uint16_t>(rng.Chance(1, 3) ? 1 + rng.Below(500) : 0);
        building.x = static_cast<std::uint16_t>(rng.Below(kTestMap));
        building.y = static_cast<std::uint16_t>(rng.Below(kTestMap));
        building.door_x = static_cast<std::uint16_t>(rng.Below(kTestMap));
        building.door_y = static_cast<std::uint16_t>(rng.Below(kTestMap));
    }
    // Areas: coal, iron and tool piles; idle worker lists; settings1.
    std::uint32_t pileBase = 1 + rng.Below(100);
    std::uint16_t settler = static_cast<std::uint16_t>(1 + rng.Below(100));
    for (std::uint16_t area = 0; area < kTestAreas; ++area) {
        layout::Area& record = world->areas[area];
        std::memset(record.piles_by_goods, 0, sizeof(record.piles_by_goods));
        std::memset(record.settlers_by_type, 0, sizeof(record.settlers_by_type));
        for (std::size_t goods = 0; goods < sizeof(record.settings1); ++goods) {
            record.settings1[goods] = static_cast<std::uint8_t>(rng.Chance(1, 4) ? 1 + rng.Below(3) : 0);
        }
        record.piles_by_goods[Value(Good::Coal)] = PileChain(rng, world, pileBase, rng.Below(4));
        record.piles_by_goods[Value(Good::Iron)] = PileChain(rng, world, pileBase, rng.Below(3));
        for (const AiToolAndWorker& entry : g_aiBuildingToolAndWorker.get()) {
            if (entry.tool != 0 && entry.tool < sizeof(record.piles_by_goods) / 2 && rng.Chance(1, 4)) {
                record.piles_by_goods[entry.tool] = PileChain(rng, world, pileBase, 1);
            }
            if (rng.Chance(1, 5)) {
                record.settlers_by_type[entry.worker] = settler;
                world->settlers[settler].next = 0;
                ++settler;
            }
        }
    }
    // Node lists: each player has nodes for some of the areas.
    for (std::uint32_t p = 0; p < kTestPlayers; ++p) {
        test.land.lists[p].head = &test.heads[p];
        auto previous = reinterpret_cast<std::uint32_t>(&test.heads[p]);
        for (std::uint16_t area = 0; area < kTestAreas; ++area) {
            test.hasNode[p][area] = !rng.Chance(1, 4);
            if (!test.hasNode[p][area]) {
                continue;
            }
            AiPlayerData& node = test.nodes[p * kTestAreas + area];
            node.area = area;
            node.force_production = rng.Chance(1, 2) ? 0 : 1;
            node.short_of_wood = rng.Next32();
            node.next = reinterpret_cast<std::uint32_t>(&test.heads[p]);
            *reinterpret_cast<std::uint32_t*>(previous) = reinterpret_cast<std::uint32_t>(&node);
            previous = reinterpret_cast<std::uint32_t>(&node);
        }
        if (previous == reinterpret_cast<std::uint32_t>(&test.heads[p])) {
            test.heads[p].next = previous;
        }
    }
}

void CompareFunctions(test::Context& ctx, const Functions& candidate, int cases) {
    const Functions original = OriginalFunctions();
    SavedGlobals saved;
    test::WorldBuffer buffer(sizeof(CGameData));
    TestWorld test{buffer.world()};
    std::vector<std::uint8_t> computerBytes(sizeof(ComputerPlayer));
    auto* computer = reinterpret_cast<ComputerPlayer*>(computerBytes.data());
    computer->message_queue = kQueue;
    g_aiWorld = test.world;
    g_aiPlayer = computer;
    g_aiLandEvaluation = reinterpret_cast<std::uint32_t>(&test.land);
    g_aiMapSizeX = static_cast<std::uint16_t>(kTestMap);
    g_aiMapSizeY = static_cast<std::uint16_t>(kTestMap);

    std::vector<Message>* log = nullptr;
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    stubs.Install<0, kIsHost>([](CGameData*, std::uint32_t, const std::uint32_t*) { return 1u; });
    stubs.Install<3, kPush>([&](CGameData*, std::uint32_t self, const std::uint32_t* args) {
        Message message{};
        std::memcpy(message.data(), reinterpret_cast<const void*>(args[0]), 4 * (args[1] < 3 ? args[1] : 3));
        message[3] = 0;  // (an unwritten header byte)
        message[11] ^= static_cast<std::uint8_t>(self == kQueue ? 0 : 0xff);
        log->push_back(message);
        return 0u;
    });
    int sent = 0;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        if (i % 10 == 0) {
            SetUpWorld(rng, test);
        }
        g_aiPlayerNumber = static_cast<std::uint8_t>(rng.Below(kTestPlayers));
        for (AiPlayerData& node : test.nodes) {
            node.force_production = rng.Chance(1, 2) ? 0 : 1;
        }
        const int op = static_cast<int>(rng.Below(5));
        const std::uint16_t player = static_cast<std::uint16_t>(rng.Below(kTestPlayers));
        std::uint16_t area = static_cast<std::uint16_t>(rng.Below(kTestAreas));
        if (op == 3) {  // OrderMissingTools: AI::IsForceProduction faults without a node (as in the original)
            for (std::uint16_t k = 0; k < kTestAreas && !test.hasNode[player][area]; ++k) {
                area = static_cast<std::uint16_t>((area + 1) % kTestAreas);
            }
            if (!test.hasNode[player][area]) {
                continue;
            }
        }
        const std::uint8_t flag = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : 1 + rng.Below(255));
        const auto goods = static_cast<std::uint8_t>(rng.Chance(1, 3) ? Value(Good::Coal) : 12 + rng.Below(8));
        const auto playerByte = static_cast<std::uint8_t>(player);
        RequestNote note{};
        note.building_type = kTestTypes[rng.Below(sizeof(kTestTypes))];
        note.x = static_cast<std::uint16_t>(rng.Below(kTestMap));  // (off the map: area 0xffff, past the world)
        note.y = static_cast<std::uint16_t>(rng.Below(kTestMap));

        const std::vector<AiPlayerData> startNodes = test.nodes;
        std::vector<Message> logA, logB;
        std::uint32_t ra = 0, rb = 0;
        const auto run = [&](const Functions& f) -> std::uint32_t {
            switch (op) {
            case 0: f.force(&player, &area, &flag); return 0;
            case 1: f.wood(&player, &area, &flag); return 0;
            case 2: return f.ensure(&goods, &playerByte, &area);
            case 3: f.tools(&player, &area); return 0;
            default: return f.check(nullptr, nullptr, &note);
            }
        };
        log = &logA;
        ra = run(original);
        const std::vector<AiPlayerData> nodesA = test.nodes;
        test.nodes = startNodes;
        log = &logB;
        rb = run(candidate);
        sent += logA.empty() ? 0 : 1;
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT(logA == logB);
        same &= S3RE_EXPECT_MEM_EQ(nodesA.data(), test.nodes.data(), nodesA.size() * sizeof(AiPlayerData));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d, player %u, area %u, flag %u, %u vs %u commands", i, op,
                     player, area, flag, static_cast<unsigned>(logA.size()), static_cast<unsigned>(logB.size()));
        }
        stubs.Take();
    }
    if (cases >= 1000 && !S3RE_EXPECT(sent >= cases / 5)) {
        ctx.Fail(__FILE__, __LINE__, "  only %d cases sent commands", sent);
    }
}

void __fastcall WrongForceProduction(const std::uint16_t* player, const std::uint16_t* area,
                                     const std::uint8_t* on) {
    // Wrong: the Amazons' commands for the Asians when switching off.
    const CGameData* world = g_aiWorld;
    if (*on == 0 && static_cast<std::uint16_t>(world->players[*player].race) == Value(Race::Asian) &&
        AreaNode(*player, *area) != nullptr) {
        AreaNode(*player, *area)->force_production = 0;
        ForceOff(static_cast<std::uint8_t>(*player), area, static_cast<std::uint16_t>(Value(Race::Amazon)));
        return;
    }
    COMP_ForceProduction(player, area, on);
}

void __fastcall WrongWood(const std::uint16_t* player, const std::uint16_t* area, const std::uint8_t* on) {
    // Wrong: the small towers stop too.
    AiPlayerData* node = AreaNode(*player, *area);
    if (node == nullptr) {
        COMP_EmergencyPowersActWood(player, area, on);
        return;
    }
    node->short_of_wood = *on;
    std::uint32_t error = 0;
    std::uint32_t id = 0;
    do {
        ++id;
        const layout::Building* building = BuildingOf(id, &error);
        if (building == nullptr || *player != building->owner || building->buildup == 0) {
            continue;
        }
        const std::uint32_t number = id;
        using BuildingAreaFn = std::uint32_t(__fastcall*)(const std::uint32_t* building);
        if (*area != static_cast<std::uint16_t>(reinterpret_cast<BuildingAreaFn>(kBuildingArea)(&number)) ||
            (KeepsWorking(building->art) && building->art != Value(B::SmallTower)) ||
            building->site.boards_missing == 0) {
            continue;
        }
        const std::uint8_t working = *on == 0 ? 1 : 0;
        const auto playerByte = static_cast<std::uint8_t>(*player);
        using Fn = std::uint32_t(__fastcall*)(const void* player, const void* building, const void* working);
        reinterpret_cast<Fn>(kSetBuildingWorking)(&playerByte, &number, &working);
    } while (error == 0);
}

std::uint32_t __fastcall WrongEnsure(const std::uint8_t* goods, const std::uint8_t* player,
                                     const std::uint16_t* area) {
    // Wrong: ignores settings1.
    if (static_cast<std::uint16_t>(GoodsInArea(*goods, area)) != 0) {
        return 1;
    }
    if (!HasToolsmith(*player, *area)) {
        return 0;
    }
    const std::uint16_t playerWord = *player;
    ForceProduction(&playerWord, area, true);
    RaiseGoodsPriority(goods, player, area);
    return 1;
}

void __fastcall WrongTools(const std::uint16_t* player, const std::uint16_t* area) {
    // Wrong: never switches forced production off.
    if (!IsForceProduction(*player, *area)) {
        AI_OrderMissingTools(player, area);
        return;
    }
    const std::uint32_t saved = AreaNode(*player, *area)->force_production;
    AreaNode(*player, *area)->force_production = 0;
    AI_OrderMissingTools(player, area);
    if (AreaNode(*player, *area)->force_production == 0) {
        AreaNode(*player, *area)->force_production = saved;
    }
}

std::uint32_t __fastcall WrongCheck(void* self, void* edx, RequestNote* note) {
    // Wrong: an idle worker doesn't count.
    const AiToolAndWorker& entry = g_aiBuildingToolAndWorker[note->building_type];
    if (entry.tool == 0) {
        return 1;
    }
    using AreaFn = std::uint32_t(__fastcall*)(std::uint32_t x, std::uint32_t y);
    const std::uint32_t area = reinterpret_cast<AreaFn>(kAreaAlongRow)(note->x, note->y);
    const auto goods = static_cast<std::uint8_t>(entry.tool);
    using EnsureFn = std::uint32_t(__fastcall*)(const std::uint8_t* goods, const std::uint8_t* player,
                                                const void* area);
    (void)self;
    (void)edx;
    return reinterpret_cast<EnsureFn>(kEnsureGoodsSupply)(&goods, &g_aiPlayerNumber.get(), &area) != 0 ? 1 : 0;
}

}  // namespace

S3RE_TEST(AiForceProduction_MatchOriginal) {
    CompareFunctions(ctx, OurFunctions(), 6000);
}

S3RE_TEST(AiForceProduction_ComparisonDetectsWrongImplementation) {
    const Functions ours = OurFunctions();
    const Functions wrongs[] = {
        {&WrongForceProduction, ours.wood, ours.ensure, ours.tools, ours.check},
        {ours.force, &WrongWood, ours.ensure, ours.tools, ours.check},
        {ours.force, ours.wood, &WrongEnsure, ours.tools, ours.check},
        {ours.force, ours.wood, ours.ensure, &WrongTools, ours.check},
        {ours.force, ours.wood, ours.ensure, ours.tools, &WrongCheck},
    };
    for (const Functions& wrong : wrongs) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareFunctions(quiet, wrong, 6000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

// WorkOnNote: DeactivateNoteOf, SetFlags, CheckWorkerOrTool and
// ReportToRequester stubbed (recorded with their arguments; the check
// answers at random, the report returns a random dword). Compared: the
// call logs and EAX.

namespace {

using WorkOnNoteFn = NoteCheckFn;

struct StubCall {
    std::uint32_t callee;
    std::uint32_t self;
    std::uint32_t args[3];
    bool operator==(const StubCall& other) const {
        return callee == other.callee && self == other.self && args[0] == other.args[0] &&
               args[1] == other.args[1] && args[2] == other.args[2];
    }
};

void CompareWorkOnNote(test::Context& ctx, WorkOnNoteFn candidate, int cases) {
    SavedGlobals saved;
    std::vector<StubCall>* log = nullptr;
    std::uint32_t checkResult = 0, reportResult = 0;
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    const auto record = [&](std::uint32_t callee, std::uint32_t self, const std::uint32_t* args, int count) {
        StubCall call{callee, self, {0, 0, 0}};
        for (int k = 0; k < count; ++k) {
            call.args[k] = args[k];
        }
        log->push_back(call);
    };
    stubs.Install<1, kDeactivateNoteOf>([&](CGameData*, std::uint32_t self, const std::uint32_t* args) {
        record(kDeactivateNoteOf, self, args, 1);
        return 0u;
    });
    stubs.Install<1, kSetFlags>([&](CGameData*, std::uint32_t self, const std::uint32_t* args) {
        record(kSetFlags, self, args, 1);
        return 0u;
    });
    stubs.Install<1, kCheckWorkerOrTool>([&](CGameData*, std::uint32_t self, const std::uint32_t* args) {
        record(kCheckWorkerOrTool, self, args, 1);
        return checkResult;
    });
    stubs.Install<2, kReportToRequester>([&](CGameData*, std::uint32_t self, const std::uint32_t* args) {
        record(kReportToRequester, self, args, 2);
        return reportResult;
    });
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        g_aiPlayer = reinterpret_cast<ComputerPlayer*>(rng.Next32());
        auto* self = reinterpret_cast<void*>(rng.Next32());
        auto* note = reinterpret_cast<RequestNote*>(rng.Next32());
        checkResult = rng.Chance(1, 2) ? 0 : rng.Chance(1, 2) ? 1 : rng.Next32();
        reportResult = rng.Next32();
        std::vector<StubCall> logA, logB;
        log = &logA;
        const std::uint32_t ra = Original_LOCAL_WARE_REQUEST_AGENT_WorkOnNote(self, nullptr, note);
        log = &logB;
        const std::uint32_t rb = candidate(self, nullptr, note);
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT(logA == logB);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
        }
        stubs.Take();
    }
}

std::uint32_t __fastcall WrongWorkOnNote(void* self, void* edx, RequestNote* note) {
    // Wrong: reports 1 instead of 100.
    using NoteFn = std::uint32_t(__fastcall*)(void* player, void* edx, RequestNote* note);
    reinterpret_cast<NoteFn>(kDeactivateNoteOf)(g_aiPlayer, nullptr, note);
    using FlagsFn = std::uint32_t(__fastcall*)(RequestNote* note, void* edx, std::uint32_t bits);
    reinterpret_cast<FlagsFn>(kSetFlags)(note, nullptr, kNoteFlagWorkedOn);
    const std::uint32_t found = reinterpret_cast<NoteCheckFn>(kCheckWorkerOrTool)(self, edx, note);
    using ReportFn = std::uint32_t(__fastcall*)(RequestNote* note, void* edx, std::uint32_t result,
                                                std::uint32_t value);
    const std::uint32_t report = reinterpret_cast<ReportFn>(kReportToRequester)(note, nullptr, found != 0 ? 1 : 0, 0);
    return (report & 0xffff0000u) | kWorkOnNoteResult;
}

}  // namespace

S3RE_TEST(AiLocalWareWorkOnNote_MatchOriginal) {
    CompareWorkOnNote(ctx, &LOCAL_WARE_REQUEST_AGENT_WorkOnNote, 500);
}

S3RE_TEST(AiLocalWareWorkOnNote_ComparisonDetectsWrongImplementation) {
    test::Context quiet(ctx.name(), 1, /*quiet=*/true);
    CompareWorkOnNote(quiet, &WrongWorkOnNote, 500);
    S3RE_EXPECT(quiet.failures() > 0);
}

}  // namespace s3re::game
