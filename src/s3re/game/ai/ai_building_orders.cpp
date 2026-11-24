// Small computer-player helpers that give orders for buildings (computer
// players, session 54; Rainer\CompMapWorkerAgent.cpp, re/computer_players.md).
// The orders go out through the command senders of ai_commands.cpp, which
// are called through their S3.EXE addresses with pointers to the values.
//
//   0x0058e200 AI::OrderPlaceBuilding(const u16* x, const u16* y, stack
//              const u16* type, const u8* player) fastcall, ret 8: passes
//              ECX, EDX and both stack arguments unchanged to
//              AI::SendPlaceBuilding 0x58e6a0; EAX = 1. Caller
//              HOUSE_GROUND_REQUEST_NOTE::PlaceBuilding 0x576430.
//   0x0058ede0 AI::NoSmallTowerNear(x, y, player) fastcall (values; stack
//              player), ret 4: 1 unless AI::HasBuildingNear 0x571d90 finds
//              one of the player's small towers (type 15) within 16 of (x,
//              y); else 0. Caller 0x576430.
//   0x0058e930 AI::ClearToolGoodsSettings(const u16* building) fastcall:
//              for the building's owner (Building::owner of record *building)
//              and area (AI::BuildingArea 0x599610, the whole EAX stored as
//              the area dword) sends AI::SendClearGoodsSetting 0x58e8b0 for
//              the tools shovel, hammer, axe, pickaxe, saw, fishing rod and
//              scythe (goods 12..17, 32); EAX = 1. Caller 0x573890.
//   0x0059c0f0 AI::SetForestersWorking(const u16* area, const u16* player)
//              fastcall, every 71 steps from CComputerPlayer::Step: the
//              player's node of the area (AI::FindNode 0x573be0); none ->
//              EAX = 0. Else working = the area's logs (AI::AreaGoodsAmount
//              0x59dc10, asked first) / 5 < the node's woodcutter count
//              (AiPlayerData::building_counts[2]); every finished forester
//              (type 5) whose area (BuildingArea) is the area gets
//              AI::SendSetBuildingWorking 0x58eac0(player, id, working),
//              except, for an Amazon player (race word 3), a forester with
//              one of the player's apiaries (type 51) within 32. EAX =
//              GetBuilding's end flag (1).

#include "game/ai/ai_layout.h"
#include "game/enums/buildings.h"
#include "game/enums/commands.h"
#include "game/enums/goods.h"
#include "game/enums/races.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/typed_fields.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using namespace ai;

namespace {

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kSendPlaceBuilding = 0x0058e6a0;       // AI::SendPlaceBuilding
constexpr std::uintptr_t kSendClearGoodsSetting = 0x0058e8b0;   // AI::SendClearGoodsSetting
constexpr std::uintptr_t kSendSetBuildingWorking = 0x0058eac0;  // AI::SendSetBuildingWorking
constexpr std::uintptr_t kHasBuildingNear = 0x00571d90;         // AI::HasBuildingNear
constexpr std::uintptr_t kBuildingArea = 0x00599610;            // AI::BuildingArea (fastcall, const u16*)
constexpr std::uintptr_t kAreaGoodsAmount = 0x0059dc10;         // AI::AreaGoodsAmount (fastcall, pointers)
constexpr std::uintptr_t kFindNode = 0x00573be0;                // AI::FindNode (fastcall)
constexpr std::uintptr_t kGetBuilding = 0x0058e170;             // AI::GetBuilding (fastcall)

using SendFn = std::uint32_t(__fastcall*)(const void* a, const void* b, const void* c, const void* d);
using Send3Fn = std::uint32_t(__fastcall*)(const void* a, const void* b, const void* c);
using HasBuildingNearFn = std::uint32_t(__fastcall*)(std::uint32_t type, std::uint32_t x, std::uint32_t y,
                                                     std::uint32_t distance, std::uint32_t player);
using BuildingAreaFn = std::uint32_t(__fastcall*)(const void* building);
using AreaGoodsAmountFn = std::uint32_t(__fastcall*)(const std::uint8_t* goods, const std::uint16_t* area);
using FindNodeFn = AiPlayerData**(__fastcall*)(AiPlayerData** out, std::uint32_t player, std::uint32_t area);
using GetBuildingFn = layout::Building*(__fastcall*)(std::uint32_t id, std::uint32_t* error);

constexpr std::uint32_t kSmallTowerDistance = 16;  // NoSmallTowerNear
constexpr std::uint32_t kApiaryDistance = 32;      // SetForestersWorking (Amazons)
constexpr std::uint32_t kLogsPerWoodcutter = 5;

}  // namespace

using OrderPlaceFn = std::uint32_t(__fastcall*)(const void* x, const void* y, const void* type, const void* player);
using NoTowerFn = std::uint32_t(__fastcall*)(std::uint32_t x, std::uint32_t y, std::uint32_t player);
using ClearToolsFn = std::uint32_t(__fastcall*)(const std::uint16_t* building);
using ForestersFn = std::uint32_t(__fastcall*)(const std::uint16_t* area, const std::uint16_t* player);

std::uint32_t __fastcall AI_OrderPlaceBuilding(const void* x, const void* y, const void* type, const void* player);
std::uint32_t __fastcall AI_NoSmallTowerNear(std::uint32_t x, std::uint32_t y, std::uint32_t player);
std::uint32_t __fastcall AI_ClearToolGoodsSettings(const std::uint16_t* building);
std::uint32_t __fastcall AI_SetForestersWorking(const std::uint16_t* area, const std::uint16_t* player);

S3RE_HOOK(0x0058e200, AI_OrderPlaceBuilding, OrderPlaceFn, Original_AI_OrderPlaceBuilding, AI_OrderPlaceBuilding);
S3RE_HOOK(0x0058ede0, AI_NoSmallTowerNear, NoTowerFn, Original_AI_NoSmallTowerNear, AI_NoSmallTowerNear);
S3RE_HOOK(0x0058e930, AI_ClearToolGoodsSettings, ClearToolsFn, Original_AI_ClearToolGoodsSettings,
          AI_ClearToolGoodsSettings);
S3RE_HOOK(0x0059c0f0, AI_SetForestersWorking, ForestersFn, Original_AI_SetForestersWorking, AI_SetForestersWorking);

// Purpose: Sends the command that places a computer player's building site
// (the last step of a ground request).
std::uint32_t __fastcall AI_OrderPlaceBuilding(const void* x, const void* y, const void* type, const void* player) {
    reinterpret_cast<SendFn>(kSendPlaceBuilding)(x, y, type, player);
    return 1;
}

// Purpose: Whether a place is at least 16 away from all of the computer
// player's small towers (probably so that new towers spread out).
std::uint32_t __fastcall AI_NoSmallTowerNear(std::uint32_t x, std::uint32_t y, std::uint32_t player) {
    const std::uint32_t found = reinterpret_cast<HasBuildingNearFn>(kHasBuildingNear)(
        Value(BuildingType::SmallTower), x, y, kSmallTowerDistance, player);
    return found == 0 ? 1 : 0;
}

// Purpose: Clears the computer player's goods settings for the tools in a
// building's area (probably so the toolsmith makes them on demand again).
std::uint32_t __fastcall AI_ClearToolGoodsSettings(const std::uint16_t* building) {
    const CGameData* world = g_aiWorld;
    const std::uint8_t owner = world->buildings[*building].owner;
    const std::uint32_t area = reinterpret_cast<BuildingAreaFn>(kBuildingArea)(building);
    constexpr Good kTools[] = {Good::Shovel, Good::Hammer,     Good::Axe,   Good::Pickaxe,
                               Good::Saw,    Good::FishingRod, Good::Scythe};
    for (const Good tool : kTools) {
        const std::uint8_t goods = Value(tool);
        reinterpret_cast<Send3Fn>(kSendClearGoodsSetting)(&goods, &owner, &area);
    }
    return 1;
}

// Purpose: Lets a computer player's foresters in an area work only while
// the area has few logs for its woodcutters (fewer than 5 per woodcutter);
// an Amazon forester next to an apiary is left alone.
std::uint32_t __fastcall AI_SetForestersWorking(const std::uint16_t* area, const std::uint16_t* player) {
    const CGameData* world = g_aiWorld;
    const auto race = static_cast<std::uint16_t>(world->players[*player].race);
    const std::uint8_t logs = Value(Good::Log);
    const std::uint32_t amount = reinterpret_cast<AreaGoodsAmountFn>(kAreaGoodsAmount)(&logs, area);
    AiPlayerData* node = nullptr;
    const AiPlayerData* found = *reinterpret_cast<FindNodeFn>(kFindNode)(&node, *player, *area);
    if (found == nullptr) {
        return 0;
    }
    const std::uint8_t working =
        amount / kLogsPerWoodcutter < found->building_counts[Value(BuildingType::Woodcutter)] ? 1 : 0;
    std::uint32_t end = 0;
    for (std::uint32_t id = 1; end == 0;) {
        const layout::Building* building = reinterpret_cast<GetBuildingFn>(kGetBuilding)(id, &end);
        const std::uint32_t buildingId = id++;
        if (building == nullptr || building->buildup != 0 || TypeOf(*building) != BuildingType::Forester) {
            continue;
        }
        if (static_cast<std::uint16_t>(reinterpret_cast<BuildingAreaFn>(kBuildingArea)(&buildingId)) != *area) {
            continue;
        }
        const auto owner = static_cast<std::uint8_t>(*player);
        if (race == Value(Race::Amazon) &&
            reinterpret_cast<HasBuildingNearFn>(kHasBuildingNear)(Value(BuildingType::Apiary), building->x, building->y,
                                                                  kApiaryDistance, owner) != 0) {
            continue;
        }
        reinterpret_cast<Send3Fn>(kSendSetBuildingWorking)(&owner, &buildingId, &working);
    }
    return end;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// A world with up to 40 building records (foresters, small towers, apiaries
// and others of players 0..2, finished or not) and door tiles in areas 0..3;
// player records with random races; the node lists of g_aiLandEvaluation
// with a node for some (player, area) pairs. MessageQueue::Push and
// GameSettings::IsHost are recording stubs (as in ai_commands.cpp): the
// pushed messages are compared, without the bytes the senders leave
// unwritten (byte 3; 11 of PlaceBuilding; 9..11 of SetGoodsSetting).

namespace {

constexpr std::uintptr_t kIsHost = 0x004318a0;  // GameSettings::IsHost
constexpr std::uintptr_t kPush = 0x004309c0;    // MessageQueue::Push
constexpr std::uint32_t kQueue = 0x5151a000;    // a fake queue pointer (Push is a stub)
constexpr std::uint32_t kTestMap = 64;
constexpr std::size_t kMessageBytes = 12;

struct Pushed {
    std::uint32_t self, count, signal;
    std::uint8_t bytes[kMessageBytes];
    bool operator==(const Pushed& other) const { return std::memcmp(this, &other, sizeof(*this)) == 0; }
};

std::vector<Pushed>* g_pushed = nullptr;

std::uint32_t RecordPushed(std::uint32_t self, const std::uint32_t* args) {
    Pushed record{self, args[1], args[2], {}};
    const auto* message = reinterpret_cast<const std::uint8_t*>(args[0]);
    std::memcpy(record.bytes, message, 4 * (args[1] < 3 ? args[1] : 3));
    record.bytes[3] = 0;
    if (record.bytes[1] == Value(CommandId::PlaceBuilding)) {
        record.bytes[11] = 0;
    } else if (record.bytes[1] == Value(CommandId::SetGoodsSetting)) {
        record.bytes[9] = record.bytes[10] = record.bytes[11] = 0;
    }
    g_pushed->push_back(record);
    return 0x7000 + static_cast<std::uint32_t>(g_pushed->size());
}

struct OrderFunctions {
    OrderPlaceFn place;
    NoTowerFn noTower;
    ClearToolsFn clearTools;
    ForestersFn foresters;
};

struct SavedOrderGlobals {
    CGameData* world = g_aiWorld;
    ComputerPlayer* player = g_aiPlayer;
    std::uint32_t land = g_aiLandEvaluation;
    std::uint16_t sizeX = g_aiMapSizeX, sizeY = g_aiMapSizeY;
    ~SavedOrderGlobals() {
        g_aiWorld = world;
        g_aiPlayer = player;
        g_aiLandEvaluation = land;
        g_aiMapSizeX = sizeX;
        g_aiMapSizeY = sizeY;
    }
};

// The node lists the AI's land evaluation keeps per player.
struct NodeLists {
    AiLandEvaluation land{};
    AiListNode heads[20]{};
    std::vector<AiPlayerData> nodes = std::vector<AiPlayerData>(3 * 4);
};

void SetUpOrderWorld(test::Rng& rng, CGameData* world, NodeLists& lists) {
    world->map_size = kTestMap;
    for (std::uint32_t x = 0; x < kTestMap; ++x) {
        for (std::uint32_t y = 0; y < kTestMap; ++y) {
            world->tiles[x][y].area = static_cast<std::uint16_t>(rng.Below(4));
        }
    }
    for (layout::Player& player : world->players) {
        player.race = static_cast<std::int32_t>(rng.Chance(1, 2) ? Value(Race::Amazon) : rng.Below(4));
    }
    using B = BuildingType;
    constexpr B kTypes[] = {B::Forester, B::Forester, B::SmallTower, B::Apiary, B::Woodcutter};
    world->building_high_water = rng.Below(40);
    for (std::uint32_t id = 0; id <= world->building_high_water + 1; ++id) {
        layout::Building& building = world->buildings[id];
        std::memset(&building, 0, sizeof(building));
        building.owner = static_cast<std::uint8_t>(rng.Chance(1, 6) ? 0xff : rng.Below(3));
        building.buildup = static_cast<std::uint8_t>(rng.Chance(1, 6) ? 1 + rng.Below(255) : 0);
        building.art = Value(kTypes[rng.Below(sizeof(kTypes) / sizeof(kTypes[0]))]);
        building.x = static_cast<std::uint16_t>(rng.Below(kTestMap));
        building.y = static_cast<std::uint16_t>(rng.Below(kTestMap));
        building.door_x = static_cast<std::uint16_t>(rng.Below(kTestMap));
        building.door_y = static_cast<std::uint16_t>(rng.Below(kTestMap));
    }
    // Node lists: per player 0..2 a node for some of the areas 0..3.
    for (std::uint32_t p = 0; p < 20; ++p) {
        AiListNode& head = lists.heads[p];
        head.next = head.prev = reinterpret_cast<std::uintptr_t>(&head);
        lists.land.lists[p].head = &head;
        if (p >= 3) {
            continue;
        }
        std::uintptr_t last = reinterpret_cast<std::uintptr_t>(&head);
        for (std::uint16_t area = 0; area < 4; ++area) {
            if (rng.Chance(1, 4)) {
                continue;
            }
            AiPlayerData& node = lists.nodes[p * 4 + area];
            rng.Fill(&node, sizeof(node));
            node.area = area;
            node.building_counts[Value(B::Woodcutter)] = static_cast<std::uint16_t>(rng.Below(6));
            reinterpret_cast<AiListNode*>(last)->next = reinterpret_cast<std::uintptr_t>(&node);
            last = reinterpret_cast<std::uintptr_t>(&node);
        }
        reinterpret_cast<AiListNode*>(last)->next = reinterpret_cast<std::uintptr_t>(&head);
    }
}

void CompareOrders(test::Context& ctx, const OrderFunctions& candidate, int cases) {
    const OrderFunctions original = {Original_AI_OrderPlaceBuilding, Original_AI_NoSmallTowerNear,
                                     Original_AI_ClearToolGoodsSettings, Original_AI_SetForestersWorking};
    SavedOrderGlobals saved;
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    std::vector<std::uint8_t> computer(sizeof(ComputerPlayer));
    auto* player = reinterpret_cast<ComputerPlayer*>(computer.data());
    player->message_queue = kQueue;
    NodeLists lists;
    g_aiWorld = world;
    g_aiPlayer = player;
    g_aiLandEvaluation = reinterpret_cast<std::uint32_t>(&lists.land);
    g_aiMapSizeX = kTestMap;
    g_aiMapSizeY = kTestMap;
    std::vector<Pushed> pushed;
    g_pushed = &pushed;
    std::uint32_t host = 1;
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    stubs.Install<0, kIsHost>([&](CGameData*, std::uint32_t, const std::uint32_t*) { return host; });
    stubs.Install<3, kPush>([&](CGameData*, std::uint32_t self, const std::uint32_t* args) {
        return RecordPushed(self, args);
    });
    int sent = 0;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        if (i % 10 == 0) {
            SetUpOrderWorld(rng, world, lists);
        }
        host = rng.Chance(1, 8) ? rng.Next32() & 0xffffff00u : rng.Next32() | 1;
        const int op = static_cast<int>(rng.Below(4));
        const std::uint32_t upper = rng.Chance(1, 4) ? rng.Next32() & 0xffff0000u : 0;
        std::uint16_t words[4];
        for (std::uint16_t& word : words) {
            word = static_cast<std::uint16_t>(rng.Below(kTestMap));
        }
        const auto owner = static_cast<std::uint8_t>(rng.Below(3));
        const auto building = static_cast<std::uint16_t>(words[3] % 41);
        const auto area = static_cast<std::uint16_t>(words[2] % 5);
        const std::uint16_t who = owner;
        const auto run = [&](const OrderFunctions& f) -> std::uint32_t {
            switch (op) {
            case 0: return f.place(&words[0], &words[1], &words[2], &owner);
            case 1: return f.noTower(words[0] | upper, words[1] | upper, owner | upper);
            case 2: return f.clearTools(&building);
            default: return f.foresters(&area, &who);
            }
        };
        const std::uint32_t ra = run(original);
        std::vector<Pushed> pushedA;
        pushedA.swap(pushed);
        const std::uint32_t rb = run(candidate);
        sent += pushedA.empty() ? 0 : 1;
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT(pushedA == pushed);
        pushed.clear();
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
        }
    }
    g_pushed = nullptr;
    if (cases >= 1000 && !S3RE_EXPECT(sent >= cases / 4)) {
        ctx.Fail(__FILE__, __LINE__, "  only %d cases sent commands", sent);
    }
}

// Wrong: the small towers looked for within 15.
std::uint32_t __fastcall WrongNoTower(std::uint32_t x, std::uint32_t y, std::uint32_t player) {
    return reinterpret_cast<HasBuildingNearFn>(kHasBuildingNear)(Value(BuildingType::SmallTower), x, y,
                                                                 kSmallTowerDistance - 1, player) == 0
               ? 1
               : 0;
}

// Wrong: the foresters of every area.
std::uint32_t __fastcall WrongForesters(const std::uint16_t* area, const std::uint16_t* player) {
    CGameData* world = g_aiWorld;
    std::vector<std::uint16_t> doors;
    for (std::uint32_t id = 0; id <= world->building_high_water + 1; ++id) {
        layout::Building& building = world->buildings[id];
        doors.push_back(world->tiles[building.door_x][building.door_y].area);
        world->tiles[building.door_x][building.door_y].area = *area;
    }
    const std::uint32_t result = AI_SetForestersWorking(area, player);
    for (std::size_t id = doors.size(); id-- > 0;) {
        layout::Building& building = world->buildings[id];
        world->tiles[building.door_x][building.door_y].area = doors[id];
    }
    return result;
}

}  // namespace

S3RE_TEST(AiBuildingOrders_MatchOriginal) {
    CompareOrders(ctx,
                  {&AI_OrderPlaceBuilding, &AI_NoSmallTowerNear, &AI_ClearToolGoodsSettings, &AI_SetForestersWorking},
                  2000);
}

S3RE_TEST(AiBuildingOrders_ComparisonDetectsWrongImplementation) {
    const OrderFunctions wrongs[] = {
        {&AI_OrderPlaceBuilding, &WrongNoTower, &AI_ClearToolGoodsSettings, &AI_SetForestersWorking},
        {&AI_OrderPlaceBuilding, &AI_NoSmallTowerNear, &AI_ClearToolGoodsSettings, &WrongForesters},
    };
    for (const OrderFunctions& wrong : wrongs) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        CompareOrders(quiet, wrong, 2000);
        S3RE_EXPECT(quiet.failures() > 0);
    }
}

}  // namespace s3re::game
