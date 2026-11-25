// Carrying out a computer player's building request (computer players
// session 55; re/computer_players.md "Requests"): the Execute chain of
// the request agents (vtable slot 10, reached through
// BLACK_BOARD_NOTE::ExecuteAtHandler 0x582510) from T_DO_BUILD_HOUSE over
// HOUSE_REQUEST to HOUSE_GROUND_REQUEST, which places the building, and the
// rule which buildings may be placed before the basic economy stands
// (Rainer\compGlobs.cpp, Rainer\CompDoBuildHouseReq.cpp, CompHouseReq.cpp
// and the unnamed file between them).
//
//   0x005721a0 AI::IsAllowedBeforeBasicEconomy(type) fastcall (CX; EDX is
//              passed on unchanged to CheckBasicEconomy, which ignores it):
//              1 when g_aiPlayerData->basic_economy is set or
//              AI::CheckBasicEconomy 0x5720c0 (player g_aiPlayerNumber as a
//              word, the data) says so; else only the first woodcutter or
//              stonecutter (building_counts[type] == 0; for races 0..3,
//              the race word of Player::race; another race: 0), a first
//              sawmill / forester, or a small tower while there are fewer
//              than 4. Other types: 0 (jump tables 0x572320 / 0x572334,
//              0x572344, 0x572354; every race takes the same branch).
//   0x00576430 HOUSE_GROUND_REQUEST_NOTE::PlaceBuilding() thiscall on the
//              note: the tile at the spiral's position (+0x2c / +0x2e) must
//              still be g_aiPlayerNumber's (AI::TileOwner, AX compared);
//              for a military site (AI::SiteKindOf == 3) no small tower of
//              the player within 16 (AI::NoSmallTowerNear); and
//              IsAllowedBeforeBasicEconomy(type, AreaAlongRow(x, y)); then
//              AI::OrderPlaceBuilding(&x, &y, &type byte, &player), AX of
//              that (1). AX = 0 otherwise. The callers use AX only (the
//              upper half: whatever the failing callee left; not
//              reproduced).
//   0x00576610 HOUSE_GROUND_REQUEST_AGENT::Execute(note), slot 10, ret 4:
//              PlaceBuilding(note).
//   0x005789b0 HOUSE_REQUEST_AGENT::Execute(note), slot 10, ret 4: the
//              note's child of type 1 (BLACK_BOARD_NOTE::FindChildOfType
//              0x5824d0); none: AX = 0 (EAX 0). Else EAX = 100 if its
//              handler's Execute (ExecuteAtHandler 0x582510) gives a
//              non-zero AX, else 0.
//   0x005656e0 T_DO_BUILD_HOUSE_AGENT::Execute(note), slot 10, ret 4: the
//              child of type 2; none: OutputDebugStringA "Kein ChildNote
//              gefunden!" (no child note found) and AX = 0 (the upper half
//              is the API's EAX; the caller 0x5655c0 tests AX). Else
//              ExecuteAtHandler(child)'s EAX.

#include "game/ai/ai_layout.h"
#include "game/enums/buildings.h"
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
constexpr std::uintptr_t kCheckBasicEconomy = 0x005720c0;  // AI::CheckBasicEconomy (ECX ptr, stack data)
constexpr std::uintptr_t kTileOwner = 0x00599570;          // AI::TileOwner (fastcall, pointers)
constexpr std::uintptr_t kSiteKindOf = 0x0058ed70;         // AI::SiteKindOf (fastcall, CX)
constexpr std::uintptr_t kNoSmallTowerNear = 0x0058ede0;   // AI::NoSmallTowerNear (fastcall)
constexpr std::uintptr_t kAreaAlongRow = 0x005996c0;       // AI::AreaAlongRow (fastcall)
constexpr std::uintptr_t kIsAllowed = 0x005721a0;          // AI::IsAllowedBeforeBasicEconomy
constexpr std::uintptr_t kOrderPlaceBuilding = 0x0058e200; // AI::OrderPlaceBuilding (fastcall)
constexpr std::uintptr_t kPlaceBuilding = 0x00576430;      // HOUSE_GROUND_REQUEST_NOTE::PlaceBuilding
constexpr std::uintptr_t kFindChildOfType = 0x005824d0;    // BLACK_BOARD_NOTE::FindChildOfType (thiscall)
constexpr std::uintptr_t kExecuteAtHandler = 0x00582510;   // BLACK_BOARD_NOTE::ExecuteAtHandler (thiscall)

constexpr std::uint8_t kMilitarySite = 3;      // AI::SiteKindOf
constexpr std::uint16_t kMaxEarlySmallTowers = 4;
constexpr std::uint32_t kPlaced = 100;          // HOUSE_REQUEST_AGENT::Execute's result

}  // namespace

using IsAllowedFn = std::uint32_t(__fastcall*)(std::uint32_t type, std::uint32_t edx);
using PlaceFn = std::uint32_t(__fastcall*)(HouseGroundRequestNote* self, void* edx);
using ExecuteFn = std::uint32_t(__fastcall*)(void* self, void* edx, BlackBoardNote* note);

std::uint32_t __fastcall AI_IsAllowedBeforeBasicEconomy(std::uint32_t type, std::uint32_t edx);
std::uint32_t __fastcall HOUSE_GROUND_REQUEST_NOTE_PlaceBuilding(HouseGroundRequestNote* self, void* edx);
std::uint32_t __fastcall HOUSE_GROUND_REQUEST_AGENT_Execute(void* self, void* edx, BlackBoardNote* note);
std::uint32_t __fastcall HOUSE_REQUEST_AGENT_Execute(void* self, void* edx, BlackBoardNote* note);
std::uint32_t __fastcall T_DO_BUILD_HOUSE_AGENT_Execute(void* self, void* edx, BlackBoardNote* note);

S3RE_HOOK(0x005721a0, AI_IsAllowedBeforeBasicEconomy, IsAllowedFn, Original_AI_IsAllowedBeforeBasicEconomy,
          AI_IsAllowedBeforeBasicEconomy);
S3RE_HOOK(0x00576430, HOUSE_GROUND_REQUEST_NOTE_PlaceBuilding, PlaceFn, Original_HOUSE_GROUND_REQUEST_NOTE_PlaceBuilding,
          HOUSE_GROUND_REQUEST_NOTE_PlaceBuilding);
S3RE_HOOK(0x00576610, HOUSE_GROUND_REQUEST_AGENT_Execute, ExecuteFn, Original_HOUSE_GROUND_REQUEST_AGENT_Execute,
          HOUSE_GROUND_REQUEST_AGENT_Execute);
S3RE_HOOK(0x005789b0, HOUSE_REQUEST_AGENT_Execute, ExecuteFn, Original_HOUSE_REQUEST_AGENT_Execute,
          HOUSE_REQUEST_AGENT_Execute);
S3RE_HOOK(0x005656e0, T_DO_BUILD_HOUSE_AGENT_Execute, ExecuteFn, Original_T_DO_BUILD_HOUSE_AGENT_Execute,
          T_DO_BUILD_HOUSE_AGENT_Execute);

// Purpose: Whether a computer player may already place a building of this
// type: anything once its basic economy stands; before that only its first
// woodcutter, stonecutter, sawmill and forester and up to four small
// towers.
std::uint32_t __fastcall AI_IsAllowedBeforeBasicEconomy(std::uint32_t type, std::uint32_t edx) {
    AiPlayerData* data = g_aiPlayerData;
    if (data->basic_economy != 0) {
        return 1;
    }
    const std::uint32_t player = g_aiPlayerNumber;  // a word (dword) copy, passed by address
    using BasicFn = std::uint32_t(__fastcall*)(const std::uint32_t* player, std::uint32_t edx, AiPlayerData* data);
    if (reinterpret_cast<BasicFn>(kCheckBasicEconomy)(&player, edx, data) != 0) {
        return 1;
    }
    const CGameData* world = g_aiWorld;
    const auto race = static_cast<std::uint16_t>(world->players[g_aiPlayerNumber].race);
    const auto& counts = data->building_counts;
    switch (static_cast<std::uint16_t>(type)) {
    case Value(B::Woodcutter):
    case Value(B::Stonecutter):
        // (a race above 3 falls out of the per-race jump table; all four races take the same test)
        if (race > Value(Race::Amazon)) {
            return 0;
        }
        return counts[static_cast<std::uint16_t>(type)] < 1 ? 1 : 0;
    case Value(B::Sawmill):
    case Value(B::Forester): return counts[static_cast<std::uint16_t>(type)] < 1 ? 1 : 0;
    case Value(B::SmallTower): return counts[Value(B::SmallTower)] < kMaxEarlySmallTowers ? 1 : 0;
    default: return 0;
    }
}

// Purpose: Places the building a computer player has found ground for, if
// the place is still its own, a military site has no small tower of its own
// near, and the type is allowed yet; 1 if the order went out.
std::uint32_t __fastcall HOUSE_GROUND_REQUEST_NOTE_PlaceBuilding(HouseGroundRequestNote* self, void* /*edx*/) {
    const std::uint32_t x = static_cast<std::uint16_t>(self->spiral.x);
    const std::uint32_t y = static_cast<std::uint16_t>(self->spiral.y);
    using OwnerFn = std::uint32_t(__fastcall*)(const std::uint32_t* x, const std::uint32_t* y);
    if (static_cast<std::uint16_t>(reinterpret_cast<OwnerFn>(kTileOwner)(&x, &y)) != g_aiPlayerNumber) {
        return 0;
    }
    const std::uint16_t type = self->request.building_type;
    using KindFn = std::uint32_t(__fastcall*)(std::uint32_t type);
    if (static_cast<std::uint8_t>(reinterpret_cast<KindFn>(kSiteKindOf)(type)) == kMilitarySite) {
        using NoTowerFn = std::uint32_t(__fastcall*)(std::uint32_t x, std::uint32_t y, std::uint32_t player);
        if (reinterpret_cast<NoTowerFn>(kNoSmallTowerNear)(x, y, g_aiPlayerNumber) == 0) {
            return 0;
        }
    }
    using AreaFn = std::uint32_t(__fastcall*)(std::uint32_t x, std::uint32_t y);
    const std::uint32_t area = reinterpret_cast<AreaFn>(kAreaAlongRow)(x, y);
    if (reinterpret_cast<IsAllowedFn>(kIsAllowed)(type, area) == 0) {
        return 0;
    }
    const auto typeByte = static_cast<std::uint8_t>(type);
    using OrderFn = std::uint32_t(__fastcall*)(const std::uint32_t* x, const std::uint32_t* y, const void* type,
                                               const void* player);
    return reinterpret_cast<OrderFn>(kOrderPlaceBuilding)(&x, &y, &typeByte, &g_aiPlayerNumber.get());
}

// Purpose: The ground request agent's part of carrying out a request:
// place the building.
std::uint32_t __fastcall HOUSE_GROUND_REQUEST_AGENT_Execute(void* /*self*/, void* /*edx*/, BlackBoardNote* note) {
    return reinterpret_cast<PlaceFn>(kPlaceBuilding)(reinterpret_cast<HouseGroundRequestNote*>(note), nullptr);
}

namespace {

BlackBoardNote* ChildOfType(BlackBoardNote* note, std::uint32_t type) {
    using Fn = BlackBoardNote*(__fastcall*)(BlackBoardNote* note, void* edx, std::uint32_t type);
    return reinterpret_cast<Fn>(kFindChildOfType)(note, nullptr, type);
}

std::uint32_t ExecuteAtHandler(BlackBoardNote* note) {
    using Fn = std::uint32_t(__fastcall*)(BlackBoardNote* note, void* edx);
    return reinterpret_cast<Fn>(kExecuteAtHandler)(note, nullptr);
}

constexpr std::uint32_t kGroundRequestNote = 1;  // the child types the agents pass the request on to
constexpr std::uint32_t kHouseRequestNote = 2;

}  // namespace

// Purpose: The house request agent's part: passes the request on to its
// ground request; 100 if the building was placed.
std::uint32_t __fastcall HOUSE_REQUEST_AGENT_Execute(void* /*self*/, void* /*edx*/, BlackBoardNote* note) {
    BlackBoardNote* child = ChildOfType(note, kGroundRequestNote);
    if (child == nullptr) {
        return 0;
    }
    return static_cast<std::uint16_t>(ExecuteAtHandler(child)) != 0 ? kPlaced : 0;
}

// Purpose: The build agent's part: passes the request on to its house
// request (logs "no child note found" if there is none).
std::uint32_t __fastcall T_DO_BUILD_HOUSE_AGENT_Execute(void* /*self*/, void* /*edx*/, BlackBoardNote* note) {
    BlackBoardNote* child = ChildOfType(note, kHouseRequestNote);
    if (child == nullptr) {
        g_importOutputDebugStringA.get()(texts::kAiNoChildNote.c_str());
        return 0;  // AX (the upper half is the API's EAX; the caller tests AX)
    }
    return ExecuteAtHandler(child);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// IsAllowedBeforeBasicEconomy and PlaceBuilding: a 64 x 64 map in patches of
// owners 0..2, small towers and other buildings, players with races 0..3 or
// any word, random player data (basic_economy mostly 0, the counts of the
// basic buildings small). SendPlaceBuilding runs with IsHost / Push
// stubbed. Compared: EAX (AX for PlaceBuilding), the data, the commands.

namespace {

constexpr std::uintptr_t kIsHost = 0x004318a0;
constexpr std::uintptr_t kPush = 0x004309c0;
constexpr std::uint32_t kQueue = 0x5152a000;
constexpr std::uint32_t kTestMap = 64;
constexpr std::size_t kDataBytes = sizeof(AiPlayerData) + 16;

using Message = std::array<std::uint8_t, 12>;

struct SavedGlobals {
    CGameData* world = g_aiWorld;
    ComputerPlayer* player = g_aiPlayer;
    AiPlayerData* data = g_aiPlayerData;
    std::uint8_t number = g_aiPlayerNumber;
    std::uint16_t sizeX = g_aiMapSizeX, sizeY = g_aiMapSizeY;
    ~SavedGlobals() {
        g_aiWorld = world;
        g_aiPlayer = player;
        g_aiPlayerData = data;
        g_aiPlayerNumber = number;
        g_aiMapSizeX = sizeX;
        g_aiMapSizeY = sizeY;
    }
};

constexpr std::uint8_t kTypes[] = {Value(B::Woodcutter), Value(B::Stonecutter), Value(B::Sawmill),
                                   Value(B::Forester),   Value(B::SmallTower),  Value(B::LargeTower),
                                   Value(B::Castle),     Value(B::Farm),        Value(B::Storehouse),
                                   1,                    0,                     16};

void SetUpMap(test::Rng& rng, CGameData* world) {
    world->map_size = kTestMap;
    for (std::uint32_t x = 0; x < kTestMap; ++x) {
        for (std::uint32_t y = 0; y < kTestMap; ++y) {
            layout::Tile& tile = world->tiles[x][y];
            tile.owner = static_cast<std::uint8_t>(((x / 16) + (y / 16)) % 4 == 3 ? 0xff : ((x / 16) + (y / 16)) % 3);
            tile.area = static_cast<std::uint16_t>(rng.Below(4));
        }
    }
    for (std::uint32_t p = 0; p < 3; ++p) {
        world->players[p].race = static_cast<std::int32_t>(rng.Chance(1, 8) ? rng.Next32() : rng.Below(4));
    }
    const std::uint32_t count = rng.Below(30);
    world->building_high_water = count;
    for (std::uint32_t id = 1; id <= count; ++id) {
        layout::Building& building = world->buildings[id];
        building.owner = static_cast<std::uint8_t>(rng.Chance(1, 8) ? 0xff : rng.Below(3));
        building.art = static_cast<std::uint8_t>(rng.Chance(1, 2) ? Value(B::SmallTower) : kTypes[rng.Below(12)]);
        building.buildup = static_cast<std::uint8_t>(rng.Chance(1, 4) ? 1 : 0);
        building.x = building.door_x = static_cast<std::uint16_t>(rng.Below(kTestMap));
        building.y = building.door_y = static_cast<std::uint16_t>(rng.Below(kTestMap));
    }
}

void RandomData(test::Rng& rng, std::uint8_t* bytes) {
    rng.Fill(bytes, kDataBytes);
    auto* data = reinterpret_cast<AiPlayerData*>(bytes);
    data->basic_economy = rng.Chance(1, 6) ? rng.Next32() : 0;
    for (std::size_t type = 0; type < kBuildingTypeCount; ++type) {
        data->building_counts[type] = static_cast<std::uint16_t>(rng.Chance(1, 2) ? 0 : rng.Below(6));
    }
}

void CompareIsAllowed(test::Context& ctx, IsAllowedFn candidate, int cases) {
    SavedGlobals saved;
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    g_aiWorld = world;
    std::vector<std::uint8_t> dataA(kDataBytes), dataB(kDataBytes);
    int allowed = 0;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        if (i % 50 == 0) {
            SetUpMap(rng, world);
        }
        g_aiPlayerNumber = static_cast<std::uint8_t>(rng.Below(3));
        RandomData(rng, dataA.data());
        dataB = dataA;
        const std::uint32_t type = (rng.Chance(1, 10) ? rng.Below(60) : kTypes[rng.Below(12)]) |
                                   (rng.Chance(1, 4) ? rng.Next32() & 0xffff0000u : 0);
        const std::uint32_t edx = rng.Next32();
        g_aiPlayerData = reinterpret_cast<AiPlayerData*>(dataA.data());
        const std::uint32_t ra = Original_AI_IsAllowedBeforeBasicEconomy(type, edx);
        g_aiPlayerData = reinterpret_cast<AiPlayerData*>(dataB.data());
        const std::uint32_t rb = candidate(type, edx);
        allowed += ra != 0 ? 1 : 0;
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT_MEM_EQ(dataA.data(), dataB.data(), dataA.size());
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, type %08x", i, type);
        }
    }
    if (cases >= 1000 && !S3RE_EXPECT(allowed >= cases / 10 && allowed <= cases - cases / 10)) {
        ctx.Fail(__FILE__, __LINE__, "  %d of %d allowed", allowed, cases);
    }
}

std::uint32_t __fastcall WrongIsAllowed(std::uint32_t type, std::uint32_t edx) {
    // Wrong: up to 4 small towers inclusive.
    if (static_cast<std::uint16_t>(type) == Value(B::SmallTower) &&
        g_aiPlayerData.get()->building_counts[Value(B::SmallTower)] == kMaxEarlySmallTowers &&
        g_aiPlayerData.get()->basic_economy == 0) {
        const std::uint32_t result = AI_IsAllowedBeforeBasicEconomy(type, edx);
        return g_aiPlayerData.get()->basic_economy != 0 ? result : 1;
    }
    return AI_IsAllowedBeforeBasicEconomy(type, edx);
}

void ComparePlaceBuilding(test::Context& ctx, PlaceFn candidate, int cases) {
    SavedGlobals saved;
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    g_aiWorld = world;
    g_aiMapSizeX = static_cast<std::uint16_t>(kTestMap);
    g_aiMapSizeY = static_cast<std::uint16_t>(kTestMap);
    std::vector<std::uint8_t> computerBytes(sizeof(ComputerPlayer));
    reinterpret_cast<ComputerPlayer*>(computerBytes.data())->message_queue = kQueue;
    g_aiPlayer = reinterpret_cast<ComputerPlayer*>(computerBytes.data());
    std::vector<std::uint8_t> dataA(kDataBytes), dataB(kDataBytes);
    std::vector<Message>* log = nullptr;
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    stubs.Install<0, kIsHost>([](CGameData*, std::uint32_t, const std::uint32_t*) { return 1u; });
    stubs.Install<3, kPush>([&](CGameData*, std::uint32_t, const std::uint32_t* args) {
        Message message{};
        std::memcpy(message.data(), reinterpret_cast<const void*>(args[0]), 4 * (args[1] < 3 ? args[1] : 3));
        message[3] = 0;   // (unwritten bytes of the message)
        message[11] = 0;
        log->push_back(message);
        return 0u;
    });
    int placed = 0;
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        if (i % 50 == 0) {
            SetUpMap(rng, world);
        }
        g_aiPlayerNumber = static_cast<std::uint8_t>(rng.Below(3));
        RandomData(rng, dataA.data());
        dataB = dataA;
        HouseGroundRequestNote noteA{};
        rng.Fill(&noteA, sizeof(noteA));
        noteA.request.building_type = kTypes[rng.Below(12)];
        noteA.spiral.x = static_cast<std::int16_t>(rng.Below(kTestMap));
        noteA.spiral.y = static_cast<std::int16_t>(rng.Below(kTestMap));
        HouseGroundRequestNote noteB = noteA;
        std::vector<Message> logA, logB;
        log = &logA;
        g_aiPlayerData = reinterpret_cast<AiPlayerData*>(dataA.data());
        const std::uint32_t ra = Original_HOUSE_GROUND_REQUEST_NOTE_PlaceBuilding(&noteA, nullptr);
        log = &logB;
        g_aiPlayerData = reinterpret_cast<AiPlayerData*>(dataB.data());
        const std::uint32_t rb = candidate(&noteB, nullptr);
        placed += (ra & 0xffff) != 0 ? 1 : 0;
        bool same = S3RE_EXPECT_EQ(ra & 0xffff, rb & 0xffff);  // callers use AX
        same &= S3RE_EXPECT(logA == logB);
        same &= S3RE_EXPECT_MEM_EQ(&noteA, &noteB, sizeof(noteA));
        same &= S3RE_EXPECT_MEM_EQ(dataA.data(), dataB.data(), dataA.size());
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, type %u at (%d, %d)", i, noteA.request.building_type,
                     noteA.spiral.x, noteA.spiral.y);
        }
        stubs.Take();
    }
    if (cases >= 1000 && !S3RE_EXPECT(placed >= cases / 20)) {
        ctx.Fail(__FILE__, __LINE__, "  only %d placed", placed);
    }
}

std::uint32_t __fastcall WrongPlaceBuilding(HouseGroundRequestNote* self, void* edx) {
    // Wrong: skips the small-tower check of military sites.
    const std::uint16_t type = self->request.building_type;
    using KindFn = std::uint32_t(__fastcall*)(std::uint32_t type);
    if (static_cast<std::uint8_t>(reinterpret_cast<KindFn>(kSiteKindOf)(type)) != kMilitarySite) {
        return HOUSE_GROUND_REQUEST_NOTE_PlaceBuilding(self, edx);
    }
    const std::uint32_t x = static_cast<std::uint16_t>(self->spiral.x);
    const std::uint32_t y = static_cast<std::uint16_t>(self->spiral.y);
    using OwnerFn = std::uint32_t(__fastcall*)(const std::uint32_t* x, const std::uint32_t* y);
    if (static_cast<std::uint16_t>(reinterpret_cast<OwnerFn>(kTileOwner)(&x, &y)) != g_aiPlayerNumber) {
        return 0;
    }
    using AreaFn = std::uint32_t(__fastcall*)(std::uint32_t x, std::uint32_t y);
    const std::uint32_t area = reinterpret_cast<AreaFn>(kAreaAlongRow)(x, y);
    if (reinterpret_cast<IsAllowedFn>(kIsAllowed)(type, area) == 0) {
        return 0;
    }
    const auto typeByte = static_cast<std::uint8_t>(type);
    using OrderFn = std::uint32_t(__fastcall*)(const std::uint32_t* x, const std::uint32_t* y, const void* type,
                                               const void* player);
    return reinterpret_cast<OrderFn>(kOrderPlaceBuilding)(&x, &y, &typeByte, &g_aiPlayerNumber.get());
}

}  // namespace

S3RE_TEST(AiIsAllowedBeforeBasicEconomy_MatchOriginal) {
    CompareIsAllowed(ctx, &AI_IsAllowedBeforeBasicEconomy, 6000);
}

S3RE_TEST(AiIsAllowedBeforeBasicEconomy_ComparisonDetectsWrongImplementation) {
    test::Context quiet(ctx.name(), 1, /*quiet=*/true);
    CompareIsAllowed(quiet, &WrongIsAllowed, 6000);
    S3RE_EXPECT(quiet.failures() > 0);
}

S3RE_TEST(AiGroundRequestPlaceBuilding_MatchOriginal) {
    ComparePlaceBuilding(ctx, &HOUSE_GROUND_REQUEST_NOTE_PlaceBuilding, 4000);
}

S3RE_TEST(AiGroundRequestPlaceBuilding_ComparisonDetectsWrongImplementation) {
    test::Context quiet(ctx.name(), 1, /*quiet=*/true);
    ComparePlaceBuilding(quiet, &WrongPlaceBuilding, 4000);
    S3RE_EXPECT(quiet.failures() > 0);
}

// The Execute chain: PlaceBuilding, FindChildOfType and ExecuteAtHandler
// stubbed (recorded; random results). Compared: the stub calls and EAX
// (AX for the ones whose callers test AX).

namespace {

struct ExecuteCall {
    std::uint32_t callee, self, arg;
    bool operator==(const ExecuteCall& other) const {
        return callee == other.callee && self == other.self && arg == other.arg;
    }
};

struct Executes {
    ExecuteFn ground, house, build;
};

void CompareExecutes(test::Context& ctx, const Executes& candidate, int cases) {
    const Executes original{Original_HOUSE_GROUND_REQUEST_AGENT_Execute, Original_HOUSE_REQUEST_AGENT_Execute,
                            Original_T_DO_BUILD_HOUSE_AGENT_Execute};
    std::vector<ExecuteCall>* log = nullptr;
    std::uint32_t placeResult = 0, childResult = 0, handlerResult = 0;
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    stubs.Install<0, kPlaceBuilding>([&](CGameData*, std::uint32_t self, const std::uint32_t*) {
        log->push_back({kPlaceBuilding, self, 0});
        return placeResult;
    });
    stubs.Install<1, kFindChildOfType>([&](CGameData*, std::uint32_t self, const std::uint32_t* args) {
        log->push_back({kFindChildOfType, self, args[0]});
        return childResult;
    });
    stubs.Install<0, kExecuteAtHandler>([&](CGameData*, std::uint32_t self, const std::uint32_t*) {
        log->push_back({kExecuteAtHandler, self, 0});
        return handlerResult;
    });
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        placeResult = rng.Next32();
        childResult = rng.Chance(1, 3) ? 0 : rng.Next32();
        handlerResult = rng.Chance(1, 3) ? rng.Next32() & 0xffff0000u : rng.Next32();
        auto* self = reinterpret_cast<void*>(rng.Next32());
        auto* note = reinterpret_cast<BlackBoardNote*>(rng.Next32());
        const int op = static_cast<int>(rng.Below(3));
        const ExecuteFn fa = op == 0 ? original.ground : op == 1 ? original.house : original.build;
        const ExecuteFn fb = op == 0 ? candidate.ground : op == 1 ? candidate.house : candidate.build;
        std::vector<ExecuteCall> logA, logB;
        log = &logA;
        const std::uint32_t ra = fa(self, nullptr, note);
        log = &logB;
        const std::uint32_t rb = fb(self, nullptr, note);
        // T_DO_BUILD_HOUSE without a child: AX only (the upper half is OutputDebugStringA's EAX).
        const std::uint32_t mask = op == 2 && childResult == 0 ? 0xffffu : 0xffffffffu;
        bool same = S3RE_EXPECT_EQ(ra & mask, rb & mask);
        same &= S3RE_EXPECT(logA == logB);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
        }
        stubs.Take();
    }
}

std::uint32_t __fastcall WrongHouseExecute(void* self, void* edx, BlackBoardNote* note) {
    // Wrong: tests the whole EAX of the handler.
    BlackBoardNote* child = ChildOfType(note, kGroundRequestNote);
    (void)self;
    (void)edx;
    if (child == nullptr) {
        return 0;
    }
    return ExecuteAtHandler(child) != 0 ? kPlaced : 0;
}

}  // namespace

S3RE_TEST(AiRequestExecute_MatchOriginal) {
    CompareExecutes(ctx, {&HOUSE_GROUND_REQUEST_AGENT_Execute, &HOUSE_REQUEST_AGENT_Execute,
                          &T_DO_BUILD_HOUSE_AGENT_Execute},
                    600);
}

S3RE_TEST(AiRequestExecute_ComparisonDetectsWrongImplementation) {
    test::Context quiet(ctx.name(), 1, /*quiet=*/true);
    CompareExecutes(quiet, {&HOUSE_GROUND_REQUEST_AGENT_Execute, &WrongHouseExecute, &T_DO_BUILD_HOUSE_AGENT_Execute},
                    600);
    S3RE_EXPECT(quiet.failures() > 0);
}

}  // namespace s3re::game
