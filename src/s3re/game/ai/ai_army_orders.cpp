// The military AI's orders (Rainer\CompMapWorkerAgent.cpp;
// re/computer_players.md "Army"): picking military buildings to gather at
// or attack, checking running attacks, and sending thieves and geologists.
// None of these range-check the player or group.
//
//   0x0058aa70 AI::NearestFreeInteriorMilitaryBuilding(player, x, y)
//              thiscall on g_aiEnemySearch, ret 0xc: among the player's
//              finished small / large towers, castles and barracks (owner
//              byte against the whole dword) that no unit group 1..9 of the
//              player names in its target words (AiGroupTarget x or y)
//              and that are not at the border, the nearest to (x, y)
//              (TileDistance, signed, first on a tie) whose door area is
//              the area of (x, y), or any if that is 0; its id, else 0.
//              "At the border": AI::FreeTileArea 0x58b480 (-1 off the map)
//              gives an area other than the door's and not 0 at (bx - 18,
//              by), (bx + 18, by), (x - 9, by - 18) or (x + 9, by + 18)
//              (original quirk, kept: the last two use the searched x
//              instead of the building's).
//   0x0058a7e0 AI::WeakestFrontierMilitaryBuilding(unused, player, region)
//              thiscall on g_aiEnemySearch, ret 0xc: among the player's
//              finished small / large towers and castles whose door tile is
//              in the walk region `region` (a byte against the dword; any
//              region when region <= -1), those at the frontier, the one
//              with the fewest soldiers in its garrison chain (Building
//              +8, Settler::next; first on a tie, fewer than 5000); its
//              id, else 0. "Frontier": with r = 5 + the distance of map
//              offset 3500 / 4000 / 5000 (the territory radius of the
//              type), a tile on the map (AI::IsOnMap 0x572090 per tested
//              coordinate) at (bx - r, by), (bx + r, by), (bx - r/2, by -
//              r) or (bx + r/2, by + r) lies in another area than the door.
//   0x00594d40 AI::Mission56FinalAssault() (only from StartAttack 0x594080
//              in campaign mission 56): player 1's groups 1..9 all attack
//              the weakest frontier building of player 0 in the walk
//              region of player 1's start position (0x55b6e0): per group
//              the target words and steal flag are cleared, the census
//              target building set, CGameData::Random logged ("RANDOM_55
//              wert = %u" through sprintf and DebugLog), the attack delay
//              raised by 2200 - random % 550 and the group sent
//              (SendMoveGroup 0x590c80). EAX = 0 without a target, else
//              the last SendMoveGroup's (StartAttack returns it).
//   0x00594a10 AI::Army::CheckGroupTargets(player ECX, float ratio), ret 4:
//              per group 1..9 with a target building (census +4): a
//              defending group (census +0x194) is reset (AI::ResetUnitGroup
//              0x5930f0) once no enemy soldiers are within 135 of the
//              building (AI::CountEnemySoldiersNear 0x58a2c0); an attack
//              is reset when the building became the player's own; else,
//              after CountSpareSoldiers 0x592100 (its result unused), with
//              the enemies within 60 of the building: a stealing group
//              (+0x320) resets every group with that target when a tenth
//              of its members (unsigned) is below those enemies or below
//              the enemies within 60 of its first member; other groups
//              reset every group with that target unless the members of
//              all groups with a target times `ratio` reach the enemies
//              (x87: fild, fmul float, fcompp; done in double; an
//              unordered compare counts as below). Each case is logged
//              with sprintf and OutputDebugStringA ("Verteidigung ...",
//              "Attack building ... gehoert mir", "Tower steal ...",
//              "Reset global Attack ..."). EAX is not used by the caller.
//   0x00594f70 AI::RandomPointNearSettler(settler ECX, int* x EDX, int* y),
//   0x005952d0 AI::RandomPointNearSettlerFar, ret 4: x, then y, gets the
//              settler's position plus a random offset: a CGameData::Random
//              draw picks the sign (bit 0 set: negative), a second one the
//              size (its low word % 64, or % 128 for the far one); then
//              AI::ClampToMap 0x572050 (EAX = its result).
//   0x00594e70 AI::SendThieves(player ECX): continuing at the player's thief
//              cursor (g_aiGroupOrders +0x8e8 per player, used while it
//              names a thief of the player) or the head of the player's
//              thief list (Player::settler_lists[23]), up to six thieves go
//              to a random point within 63 tiles (0x594f70) without enemy
//              soldiers within 18 (0x58a2c0; up to five new points, but a
//              point found by the fifth is dropped, original quirk kept)
//              through SendMoveSettler 0x590a30. The cursor is stored only
//              after six thieves. EAX is not used by the caller.
//   0x00595050 AI::SendGeologists(player ECX): every 7th call (counter in
//              g_aiArmyState +0x4b4 per player) walks the player's
//              geologists (Player::settler_lists[22]). One whose bit in
//              g_aiGroupOrders' bitset is set stays busy unless a Random
//              draw's low word is below 1000 (then the bit is cleared).
//              Otherwise the search index g_aiEnemySearch becomes 5000
//              (500 if a Random low word is above 0x1000) and
//              AI::NextSparseMapOffset 0x58a250 tries points around the
//              geologist (clamped, 0x572050) until one is a mountain tile
//              (terrain 32); when the offsets run out (dx = dy = 0) the
//              last point stays. A mountain point: SendPatrolSettler
//              0x590af0 and the bit is set; otherwise a random point
//              within 127 tiles (0x5952d0) without enemies within 18, as
//              for the thieves, through SendMoveSettler. The point's
//              variables are not initialized by the original (only read
//              after a search step wrote them; ours start at 0).

#include "game/ai/ai_layout.h"
#include "game/enums/buildings.h"
#include "game/enums/settler_types.h"
#include "game/enums/terrain.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/texts.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace s3re::game {

using namespace ai;
using layout::kMapRowTiles;
using layout::kPlayers;

namespace {

constexpr std::uint32_t kGroupSlots = 100;
constexpr std::uint32_t kOrderBits = 32000;
constexpr std::uint32_t kFirstGroup = 1;
constexpr std::uint32_t kLastGroup = 9;
constexpr std::int32_t kFarthest = 0x7fffffff;
constexpr std::int32_t kMostSoldiers = 5000;          // WeakestFrontierMilitaryBuilding starts below this
constexpr std::int32_t kBorderStep = 18;              // NearestFreeInteriorMilitaryBuilding's probes
constexpr std::int32_t kBorderHalfStep = 9;
constexpr std::int32_t kFrontierMargin = 5;           // added to the territory radius
constexpr std::size_t kSmallTowerRadiusOffset = 3500;  // map offset whose distance is the territory radius
constexpr std::size_t kLargeTowerRadiusOffset = 4000;
constexpr std::size_t kCastleRadiusOffset = 5000;
constexpr std::uint32_t kAttacker = 1;                // mission 56: player 1 attacks player 0
constexpr std::uint32_t kDefender = 0;
constexpr std::uint32_t kAssaultDelay = 2200;         // + up to 549 less
constexpr std::uint32_t kAssaultDelaySpread = 550;
constexpr std::uint32_t kDefenceDistance = 135;       // CheckGroupTargets: a defence ends without enemies this close
constexpr std::uint32_t kAttackDistance = 60;
constexpr std::uint32_t kSafeDistance = 18;           // thieves and geologists go where no enemy is this close
constexpr int kPointTries = 5;
constexpr int kThievesPerCall = 6;
constexpr std::int32_t kGeologistCallInterval = 6;    // SendGeologists works every 7th call
constexpr std::uint16_t kStayBusyDraw = 1000;         // a busy geologist is freed when a draw is below this
constexpr std::uint16_t kShortSearchDraw = 0x1000;
constexpr std::uint32_t kLongSearch = 5000;           // the first map offset searched
constexpr std::uint32_t kShortSearch = 500;
constexpr std::uint32_t kNearMask = 63;               // RandomPointNearSettler: offsets 0..63
constexpr std::uint32_t kFarMask = 127;
constexpr std::size_t kTextBytes = 256;               // the original's stack buffers

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kRandom = 0x004feb70;                // CGameData::Random (thiscall)
constexpr std::uintptr_t kTileDistance = 0x004ff320;          // TileDistance (stdcall)
constexpr std::uintptr_t kIsOnMap = 0x00572090;               // AI::IsOnMap (fastcall)
constexpr std::uintptr_t kClampToMap = 0x00572050;            // AI::ClampToMap (fastcall)
constexpr std::uintptr_t kFreeTileArea = 0x0058b480;          // AI::FreeTileArea (stdcall)
constexpr std::uintptr_t kNextSparseMapOffset = 0x0058a250;   // AI::NextSparseMapOffset (fastcall)
constexpr std::uintptr_t kCountEnemySoldiers = 0x0058a2c0;    // AI::CountEnemySoldiersNear (thiscall)
constexpr std::uintptr_t kStartPositionWalkRegion = 0x0055b6e0;  // AI::StartPositionWalkRegion (stdcall)
constexpr std::uintptr_t kWeakestFrontier = 0x0058a7e0;       // AI::WeakestFrontierMilitaryBuilding
constexpr std::uintptr_t kResetUnitGroup = 0x005930f0;        // AI::ResetUnitGroup (stdcall)
constexpr std::uintptr_t kCountSpareSoldiers = 0x00592100;    // AI::Army::CountSpareSoldiers
constexpr std::uintptr_t kRandomPointNear = 0x00594f70;       // AI::RandomPointNearSettler
constexpr std::uintptr_t kRandomPointFar = 0x005952d0;        // AI::RandomPointNearSettlerFar
constexpr std::uintptr_t kSendMoveSettler = 0x00590a30;       // AI::SendMoveSettler
constexpr std::uintptr_t kSendPatrolSettler = 0x00590af0;     // AI::SendPatrolSettler
constexpr std::uintptr_t kSendMoveGroup = 0x00590c80;         // AI::SendMoveGroup
constexpr std::uintptr_t kSprintf = 0x0061f27e;               // the CRT's sprintf
constexpr std::uintptr_t kDebugLog = 0x00428720;              // DebugLog (cdecl)

std::uint32_t Random() {
    using Fn = std::uint32_t(__fastcall*)(CGameData* world, void* edx);
    return reinterpret_cast<Fn>(kRandom)(g_aiMapWorld, nullptr);
}

std::int32_t TileDistance(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1) {
    using Fn = std::int32_t(__stdcall*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
    return reinterpret_cast<Fn>(kTileDistance)(x0, y0, x1, y1);
}

bool IsOnMap(std::int32_t v) {
    using Fn = bool(__fastcall*)(std::int32_t v);
    return reinterpret_cast<Fn>(kIsOnMap)(v);
}

std::uint32_t ClampToMap(std::int32_t* x, std::int32_t* y) {
    using Fn = std::uint32_t(__fastcall*)(std::int32_t* x, std::int32_t* y);
    return reinterpret_cast<Fn>(kClampToMap)(x, y);
}

std::int32_t CountEnemySoldiersNear(std::uint32_t x, std::uint32_t y, std::uint32_t distance, std::uint32_t player) {
    using Fn = std::int32_t(__fastcall*)(void* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t,
                                         std::uint32_t);
    return reinterpret_cast<Fn>(kCountEnemySoldiers)(&g_aiEnemySearch.get(), nullptr, x, y, distance, player);
}

// AI::ResetUnitGroup is stdcall; the original sets ECX to g_aiGroupOrders.
void ResetUnitGroup(std::uint32_t player, std::uint32_t group) {
    using Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t player, std::uint32_t group);
    reinterpret_cast<Fn>(kResetUnitGroup)(&g_aiGroupOrders.get(), nullptr, player, group);
}

using Sender4Fn = std::uint32_t(__fastcall*)(AiCommandSender* self, void* edx, std::uint32_t, std::uint32_t,
                                             std::uint32_t, std::uint32_t);

std::uint32_t Send(std::uintptr_t sender, std::uint32_t player, std::uint32_t who, std::uint32_t x,
                   std::uint32_t y) {
    return reinterpret_cast<Sender4Fn>(sender)(&g_aiCommandSender.get(), nullptr, player, who, x, y);
}

template <typename... Args>
void Format(char* text, const GameText& format, Args... args) {
    using Fn = int(__cdecl*)(char* buffer, const char* format, ...);
    reinterpret_cast<Fn>(kSprintf)(text, format.c_str(), args...);
}

// sprintf into a stack buffer and OutputDebugStringA, as the original logs.
template <typename... Args>
void LogDebugString(const GameText& format, Args... args) {
    char text[kTextBytes];
    Format(text, format, args...);
    g_importOutputDebugStringA.get()(text);
}

// A tile by its flat index x * 0x300 + y (wrapping like the original's
// address arithmetic).
const layout::Tile& TileAt(const CGameData* world, std::uint32_t x, std::uint32_t y) {
    return (&world->tiles[0][0])[x * kMapRowTiles + y];
}

const layout::Building& BuildingAt(const CGameData* world, std::uint32_t id) { return (&world->buildings[0])[id]; }
const layout::Settler& SettlerAt(const CGameData* world, std::uint32_t id) { return (&world->settlers[0])[id]; }

std::uint16_t& TargetBuilding(std::uint32_t index) {
    return (&g_aiCensus.get().group_target_buildings[0][0])[index];
}

// The bitset dword of a settler, checked like std::bitset (>= 32000: _Xran).
std::uint32_t& OrderWord(std::uint32_t settler) {
    std::uint8_t* bits = g_aiGroupOrders.get().settlers_with_orders;
    if (settler >= kOrderBits) {
        using XranFn = void(__fastcall*)(void* self, void* edx);
        reinterpret_cast<XranFn>(0x00596570)(bits, nullptr);  // std::bitset::_Xran (throws)
    }
    return reinterpret_cast<std::uint32_t*>(bits)[settler >> 5];
}

// A group of the player names the building in its target words.
bool UsedByGroup(std::uint32_t player, std::uint32_t building) {
    const AiGroupTarget* targets = &g_aiGroupOrders.get().group_targets[0][0] + player * kUnitGroups;
    for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
        if (targets[group].x == building || targets[group].y == building) {
            return true;
        }
    }
    return false;
}

// 0x58ab44..0x58abfe: FreeTileArea at a probe is another area (and not 0).
bool OtherAreaAt(std::uint32_t x, std::uint32_t y, std::uint32_t area) {
    using Fn = std::int32_t(__stdcall*)(std::uint32_t x, std::uint32_t y);
    const auto found = static_cast<std::uint32_t>(reinterpret_cast<Fn>(kFreeTileArea)(x, y));
    return found != area && found != 0;
}

// The territory radius of the military building types the frontier search
// knows (0x58a878..0x58a8a8), or false for other types.
bool TerritoryRadius(const CGameData* world, std::uint8_t type, std::int32_t& radius) {
    switch (static_cast<BuildingType>(type)) {
    case BuildingType::SmallTower: radius = world->offsets[kSmallTowerRadiusOffset].distance; return true;
    case BuildingType::LargeTower: radius = world->offsets[kLargeTowerRadiusOffset].distance; return true;
    case BuildingType::Castle: radius = world->offsets[kCastleRadiusOffset].distance; return true;
    default: return false;
    }
}

// A tile in another area than `area` at (x, y) (only x is checked against
// the map; 0x58a8f6..0x58a9ea).
bool OtherAreaOnMap(std::int32_t x, std::int32_t y, std::uint16_t area) {
    return TileAt(g_aiMapWorld, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)).area != area;
}

// 0x594f7b..0x59500d: one random offset of the given size, sign first.
std::int32_t RandomOffset(std::uint32_t mask) {
    if (Random() & 1) {
        return -static_cast<std::int32_t>(Random() & 0xffff & mask);
    }
    return static_cast<std::int32_t>(Random() & 0xffff & mask);
}

std::uint32_t RandomPointNear(std::uint32_t settler, std::int32_t* x, std::int32_t* y, std::uint32_t mask) {
    const std::int32_t dx = RandomOffset(mask);
    const std::int32_t dy = RandomOffset(mask);
    const layout::Settler& record = SettlerAt(g_aiMapWorld, settler);
    *x = record.x + dx;
    *y = record.y + dy;
    return ClampToMap(x, y);
}

using PointFn = std::uint32_t(__fastcall*)(std::uint32_t settler, std::int32_t* x, std::int32_t* y);

// 0x594ec0..0x594f37 / 0x595226..0x59529d: a random point without enemy
// soldiers near it, and the settler sent there. After the first point up
// to five more are drawn; one found by the fifth is not used.
void SendToSafePoint(std::uintptr_t randomPoint, std::uint32_t player, std::uint32_t settler, std::int32_t& x,
                     std::int32_t& y) {
    const auto point = reinterpret_cast<PointFn>(randomPoint);
    point(settler, &x, &y);
    int tries = kPointTries;
    if (CountEnemySoldiersNear(x, y, kSafeDistance, player) != 0) {
        for (;;) {
            const int left = tries--;
            if (left <= 0) {
                return;
            }
            point(settler, &x, &y);
            if (CountEnemySoldiersNear(x, y, kSafeDistance, player) == 0) {
                break;
            }
        }
        if (tries <= 0) {
            return;
        }
    }
    Send(kSendMoveSettler, player, settler, x, y);
}

}  // namespace

using NearestFreeFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t player, std::uint32_t x,
                                                 std::uint32_t y);
using WeakestFrontierFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t unused,
                                                     std::uint32_t player, std::int32_t region);
using NoArgFn = std::uint32_t(__fastcall*)(void* ecx, void* edx);
using CheckTargetsFn = void(__fastcall*)(std::uint32_t player, void* edx, float ratio);
using PlayerFn = void(__fastcall*)(std::uint32_t player, void* edx);

std::uint32_t __fastcall AI_NearestFreeInteriorMilitaryBuilding(void* self, void* edx, std::uint32_t player,
                                                                std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall AI_WeakestFrontierMilitaryBuilding(void* self, void* edx, std::uint32_t unused,
                                                            std::uint32_t player, std::int32_t region);
std::uint32_t __fastcall AI_Mission56FinalAssault(void* ecx, void* edx);
void __fastcall AI_Army_CheckGroupTargets(std::uint32_t player, void* edx, float ratio);
std::uint32_t __fastcall AI_RandomPointNearSettler(std::uint32_t settler, std::int32_t* x, std::int32_t* y);
std::uint32_t __fastcall AI_RandomPointNearSettlerFar(std::uint32_t settler, std::int32_t* x, std::int32_t* y);
void __fastcall AI_SendThieves(std::uint32_t player, void* edx);
void __fastcall AI_SendGeologists(std::uint32_t player, void* edx);

S3RE_HOOK(0x0058aa70, AI_NearestFreeInteriorMilitaryBuilding, NearestFreeFn,
          Original_AI_NearestFreeInteriorMilitaryBuilding, AI_NearestFreeInteriorMilitaryBuilding);
S3RE_HOOK(0x0058a7e0, AI_WeakestFrontierMilitaryBuilding, WeakestFrontierFn,
          Original_AI_WeakestFrontierMilitaryBuilding, AI_WeakestFrontierMilitaryBuilding);
S3RE_HOOK(0x00594d40, AI_Mission56FinalAssault, NoArgFn, Original_AI_Mission56FinalAssault, AI_Mission56FinalAssault);
S3RE_HOOK(0x00594a10, AI_Army_CheckGroupTargets, CheckTargetsFn, Original_AI_Army_CheckGroupTargets,
          AI_Army_CheckGroupTargets);
S3RE_HOOK(0x00594f70, AI_RandomPointNearSettler, PointFn, Original_AI_RandomPointNearSettler,
          AI_RandomPointNearSettler);
S3RE_HOOK(0x005952d0, AI_RandomPointNearSettlerFar, PointFn, Original_AI_RandomPointNearSettlerFar,
          AI_RandomPointNearSettlerFar);
S3RE_HOOK(0x00594e70, AI_SendThieves, PlayerFn, Original_AI_SendThieves, AI_SendThieves);
S3RE_HOOK(0x00595050, AI_SendGeologists, PlayerFn, Original_AI_SendGeologists, AI_SendGeologists);

// Purpose: Finds the computer player's nearest military building inside
// its land (not at the border, not used by a unit group), probably as a
// gathering place for a unit group.
std::uint32_t __fastcall AI_NearestFreeInteriorMilitaryBuilding(void* /*self*/, void* /*edx*/, std::uint32_t player,
                                                                std::uint32_t x, std::uint32_t y) {
    const std::uint16_t pointArea = TileAt(g_aiMapWorld, x, y).area;
    std::int32_t nearest = kFarthest;
    std::uint32_t result = 0;
    for (std::uint32_t id = 1; id < g_aiMapWorld.get()->building_high_water + 1; ++id) {
        const layout::Building& building = BuildingAt(g_aiMapWorld, id);
        if (building.owner != player || building.buildup != 0) {
            continue;
        }
        switch (static_cast<BuildingType>(building.art)) {
        case BuildingType::SmallTower:
        case BuildingType::LargeTower:
        case BuildingType::Castle:
        case BuildingType::Barracks:
            break;
        default:
            continue;
        }
        if (UsedByGroup(player, id)) {
            continue;
        }
        const std::uint32_t bx = building.x;
        const std::uint32_t by = building.y;
        const std::uint16_t area = TileAt(g_aiMapWorld, building.door_x, building.door_y).area;
        bool border = OtherAreaAt(bx - kBorderStep, by, area);
        border |= OtherAreaAt(bx + kBorderStep, by, area);
        border |= OtherAreaAt(x - kBorderHalfStep, by - kBorderStep, area);  // the searched x (original quirk)
        if (OtherAreaAt(x + kBorderHalfStep, by + kBorderStep, area) || border) {
            continue;
        }
        const std::int32_t distance = TileDistance(x, y, building.x, building.y);
        if (distance < nearest) {
            const layout::Building& again = BuildingAt(g_aiMapWorld, id);
            const std::uint16_t doorArea = TileAt(g_aiMapWorld, again.door_x, again.door_y).area;
            if (doorArea == pointArea || pointArea == 0) {
                nearest = distance;
                result = id;
            }
        }
    }
    return result;
}

// Purpose: Finds the computer player's weakest guarded military building
// at the frontier (in a walk region), as the target of an attack by
// another player (mission 56) or the place to strengthen (StartAttack).
std::uint32_t __fastcall AI_WeakestFrontierMilitaryBuilding(void* /*self*/, void* /*edx*/, std::uint32_t /*unused*/,
                                                            std::uint32_t player, std::int32_t region) {
    std::int32_t fewest = kMostSoldiers;
    std::uint32_t result = 0;
    for (std::uint32_t id = 1; id < g_aiMapWorld.get()->building_high_water + 1; ++id) {
        const CGameData* world = g_aiMapWorld;
        const layout::Building& building = BuildingAt(world, id);
        if (building.owner != player || building.buildup != 0) {
            continue;
        }
        if (region > -1 && TileAt(world, building.door_x, building.door_y).walk_region !=
                               static_cast<std::uint32_t>(region)) {
            continue;
        }
        std::int32_t radius = 0;
        if (!TerritoryRadius(world, building.art, radius)) {
            continue;
        }
        const std::int32_t reach = radius + kFrontierMargin;
        const std::int32_t bx = building.x;
        const std::int32_t by = building.y;
        const std::uint16_t area = TileAt(world, building.door_x, building.door_y).area;
        // 0x58a8f6..0x58a9f9: probes left, right, upper left, lower right.
        bool frontier = false;
        if (IsOnMap(bx - reach) && OtherAreaOnMap(bx - reach, by, area)) {
            frontier = true;
        }
        if (IsOnMap(bx + reach) && OtherAreaOnMap(bx + reach, by, area)) {
            frontier = true;
        }
        if (IsOnMap(by - reach) && IsOnMap(bx - reach / 2) && OtherAreaOnMap(bx - reach / 2, by - reach, area)) {
            frontier = true;
        }
        if (IsOnMap(by + reach) && IsOnMap(bx + reach / 2) && OtherAreaOnMap(bx + reach / 2, by + reach, area)) {
            frontier = true;
        }
        if (!frontier) {
            continue;
        }
        std::int32_t soldiers = 0;
        for (std::uint32_t soldier = BuildingAt(g_aiMapWorld, id).garrison; soldier != 0;
             soldier = SettlerAt(g_aiMapWorld, soldier).next) {
            ++soldiers;
        }
        if (soldiers < fewest) {
            fewest = soldiers;
            result = id;
        }
    }
    return result;
}

// Purpose: The scripted final assault of campaign mission 56: all of
// player 1's unit groups attack player 0's weakest frontier building.
std::uint32_t __fastcall AI_Mission56FinalAssault(void* /*ecx*/, void* /*edx*/) {
    using RegionFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t player);
    const std::uint32_t region =
        reinterpret_cast<RegionFn>(kStartPositionWalkRegion)(&g_aiCensus.get(), nullptr, kAttacker);
    const std::uint32_t target = reinterpret_cast<WeakestFrontierFn>(kWeakestFrontier)(
        &g_aiEnemySearch.get(), nullptr, kAttacker, kDefender, static_cast<std::int32_t>(region));
    if (target == 0) {
        return 0;
    }
    AiGroupOrders& orders = g_aiGroupOrders.get();
    std::uint32_t sent = 0;
    for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
        AiGroupTarget& words = orders.group_targets[kAttacker][group];
        words.x = 0;
        words.y = 0;
        words.unk4 = 0;
        g_aiCensus.get().group_target_buildings[kAttacker][group] = static_cast<std::uint16_t>(target);
        orders.group_flags[kAttacker][group] = 0;
        const std::uint32_t draw = Random() & 0xffff;
        char text[kTextBytes];
        Format(text, texts::kRandom55, draw);
        using DebugLogFn = void(__cdecl*)(const char* text);
        reinterpret_cast<DebugLogFn>(kDebugLog)(text);
        std::uint32_t& delay = g_aiArmyState.get().attack_delays[kAttacker][group];
        delay += kAssaultDelay - draw % kAssaultDelaySpread;
        const layout::Building& building = BuildingAt(g_aiMapWorld, target);
        sent = Send(kSendMoveGroup, kAttacker, group, building.x, building.y);
    }
    return sent;
}

// Purpose: Ends a computer player's defences and attacks that are over or
// hopeless: a defence without enemies left, an attack on a building that
// is now its own, or one against more enemies than its soldiers (times
// `ratio`) can take.
void __fastcall AI_Army_CheckGroupTargets(std::uint32_t player, void* /*edx*/, float ratio) {
    const std::uint32_t base = player * kUnitGroups;
    for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
        const std::uint32_t index = base + group;
        const std::uint32_t target = TargetBuilding(index);
        if (target == 0) {
            continue;
        }
        if ((&g_aiCensus.get().group_defending[0][0])[index] != 0) {
            const layout::Building& building = BuildingAt(g_aiMapWorld, target);
            const std::int32_t enemies = CountEnemySoldiersNear(building.x, building.y, kDefenceDistance, player);
            LogDebugString(texts::kAiDefence, target, group, enemies);
            if (enemies != 0) {
                continue;
            }
            LogDebugString(texts::kAiDefenceOver, target, player, group);
            ResetUnitGroup(player, group);
            continue;
        }
        if (BuildingAt(g_aiMapWorld, target).owner == player) {
            LogDebugString(texts::kAiAttackOwnBuilding, target, player, group);
            ResetUnitGroup(player, group);
            continue;
        }
        using SpareFn = std::uint32_t(__fastcall*)(AiGroupOrders* self, void* edx, std::uint32_t player);
        reinterpret_cast<SpareFn>(kCountSpareSoldiers)(&g_aiGroupOrders.get(), nullptr, player);  // (unused)
        const layout::Building& building = BuildingAt(g_aiMapWorld, target);
        const std::int32_t enemies = CountEnemySoldiersNear(building.x, building.y, kAttackDistance, player);
        if ((&g_aiGroupOrders.get().group_flags[0][0])[index] != 0) {
            // 0x594ba8..0x594c57: a steal attack.
            CGameData* world = g_aiMapWorld;
            const layout::Settler& first = SettlerAt(world, (&world->player_lists[0][0][0])[index * kGroupSlots]);
            const std::int32_t defenders = CountEnemySoldiersNear(first.x, first.y, kAttackDistance, player);
            const std::uint32_t tenth =
                static_cast<std::uint32_t>((&g_aiMapWorld.get()->player_list_lengths[0][0])[index]) / 10;
            if (tenth < static_cast<std::uint32_t>(enemies) || tenth < static_cast<std::uint32_t>(defenders)) {
                LogDebugString(texts::kAiStealTargetDeleted, target);
                for (std::uint32_t other = kFirstGroup; other <= kLastGroup; ++other) {
                    if (TargetBuilding(base + other) == target) {
                        ResetUnitGroup(player, other);
                    }
                }
            }
            continue;
        }
        std::int32_t attackers = 0;
        for (std::uint32_t other = kFirstGroup; other <= kLastGroup; ++other) {
            if (TargetBuilding(base + other) != 0) {
                attackers += (&g_aiMapWorld.get()->player_list_lengths[0][0])[base + other];
            }
        }
        // 0x594c9a: x87 (fild, fmul float, fcompp), in double; NaN resets.
        const double strength = static_cast<double>(attackers) * static_cast<double>(ratio);
        if (strength >= static_cast<double>(enemies)) {
            continue;
        }
        for (std::uint32_t other = kFirstGroup; other <= kLastGroup; ++other) {
            const std::uint32_t otherTarget = TargetBuilding(base + other);
            if (otherTarget == target) {
                LogDebugString(texts::kAiGlobalAttackReset, player, other, otherTarget);
                ResetUnitGroup(player, other);
            }
        }
    }
}

// Purpose: A random point within 63 tiles of a settler (on the map), where
// the military AI sends a thief.
std::uint32_t __fastcall AI_RandomPointNearSettler(std::uint32_t settler, std::int32_t* x, std::int32_t* y) {
    return RandomPointNear(settler, x, y, kNearMask);
}

// Purpose: A random point within 127 tiles of a settler (on the map), where
// the military AI sends a geologist that found no mountain.
std::uint32_t __fastcall AI_RandomPointNearSettlerFar(std::uint32_t settler, std::int32_t* x, std::int32_t* y) {
    return RandomPointNear(settler, x, y, kFarMask);
}

// Purpose: Sends a computer player's thieves out to random places near them
// (away from enemy soldiers), six per call.
void __fastcall AI_SendThieves(std::uint32_t player, void* /*edx*/) {
    std::uint32_t& cursor = g_aiGroupOrders.get().thief_cursors[player];
    std::uint32_t thief = cursor;
    if (thief != 0) {
        const layout::Settler& record = SettlerAt(g_aiMapWorld, thief);
        if (record.art != Value(SettlerType::Thief) || record.owner != player) {
            thief = 0;
        }
    }
    if (thief == 0) {
        thief = g_aiMapWorld.get()->players[player].settler_lists[Value(SettlerType::Thief)];
    }
    if (static_cast<std::int32_t>(thief) <= 0) {
        return;
    }
    std::int32_t x = 0;
    std::int32_t y = 0;
    for (int sent = 1;; ++sent) {
        SendToSafePoint(kRandomPointNear, player, thief, x, y);
        thief = SettlerAt(g_aiMapWorld, thief).next;
        if (sent >= kThievesPerCall) {
            cursor = thief;
            return;
        }
        if (thief == 0) {
            return;
        }
    }
}

// Purpose: Sends a computer player's free geologists to the nearest
// mountain (or, without one, to a random place), every 7th call.
void __fastcall AI_SendGeologists(std::uint32_t player, void* /*edx*/) {
    std::uint32_t& calls = g_aiArmyState.get().geologist_timers[player];
    const auto count = static_cast<std::int32_t>(calls);
    calls = static_cast<std::uint32_t>(count + 1);
    if (count <= kGeologistCallInterval) {
        return;
    }
    calls = 0;
    std::uint32_t geologist = g_aiMapWorld.get()->players[player].settler_lists[Value(SettlerType::Geologist)];
    if (geologist == 0) {
        return;
    }
    std::int32_t x = 0;  // (uninitialized in the original, see the file header)
    std::int32_t y = 0;
    do {
        const std::uint32_t bit = 1u << (geologist & 31);
        if (OrderWord(geologist) & bit) {
            if ((Random() & 0xffff) >= kStayBusyDraw) {
                geologist = SettlerAt(g_aiMapWorld, geologist).next;
                continue;
            }
            OrderWord(geologist) &= ~bit;
        }
        const layout::Settler& record = SettlerAt(g_aiMapWorld, geologist);
        const std::int32_t homeX = record.x;
        const std::int32_t homeY = record.y;
        g_aiEnemySearch = (Random() & 0xffff) > kShortSearchDraw ? kShortSearch : kLongSearch;
        // 0x59516a..0x5951cc: the nearest mountain tile by the sparse offsets.
        for (;;) {
            std::int32_t dx = 0;
            std::int32_t dy = 0;
            using NextFn = std::uint32_t(__fastcall*)(std::uint32_t* index, void* edx, std::int32_t* dx,
                                                      std::int32_t* dy);
            reinterpret_cast<NextFn>(kNextSparseMapOffset)(&g_aiEnemySearch.get(), &dx, &dx, &dy);
            if (dx == 0 && dy == 0) {
                break;  // offsets used up: the last point stays
            }
            x = dx + homeX;
            y = dy + homeY;
            ClampToMap(&x, &y);
            if (TileAt(g_aiMapWorld, x, y).terrain == kTerrainClassMountain) {
                break;
            }
        }
        if (TileAt(g_aiMapWorld, x, y).terrain == kTerrainClassMountain) {
            Send(kSendPatrolSettler, player, geologist, x, y);
            OrderWord(geologist) |= bit;
        } else {
            SendToSafePoint(kRandomPointFar, player, geologist, x, y);
        }
        geologist = SettlerAt(g_aiMapWorld, geologist).next;
    } while (geologist != 0);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// One random army and world per case (g_aiMapWorld = g_aiWorld = the test
// world); the original runs, its results are kept, the start is restored
// and ours runs. CountEnemySoldiersNear is a stub with random results (both
// sides get the same sequence), the command senders are recording stubs,
// OutputDebugStringA's import a recorder; the other callees run (verified).

namespace {

constexpr std::uint32_t kTestSettlers = 400;
constexpr std::uint32_t kTestBuildings = 30;
constexpr std::uint32_t kTestMap = 120;

std::vector<std::string>* g_strings = nullptr;

void __stdcall RecordString(const char* text) {
    if (g_strings != nullptr) {
        g_strings->emplace_back(text);
    }
}

std::vector<std::uint8_t> Bytes(const void* start, std::size_t size) {
    const auto* p = static_cast<const std::uint8_t*>(start);
    return std::vector<std::uint8_t>(p, p + size);
}

constexpr std::size_t kListsOffset = offsetof(CGameData, player_lists);
constexpr std::size_t kListsBytes =
    offsetof(CGameData, player_list_lengths) + sizeof(CGameData::player_list_lengths) - kListsOffset;

struct OrderState {
    std::vector<std::uint8_t> lists, orders, armyState, census, sender;
    std::uint16_t random[3];
    std::uint32_t search;

    static OrderState Capture(CGameData* world) {
        OrderState s{Bytes(reinterpret_cast<std::uint8_t*>(world) + kListsOffset, kListsBytes),  // raw-ok: test snapshot
                     Bytes(&g_aiGroupOrders.get(), sizeof(AiGroupOrders)),
                     Bytes(&g_aiArmyState.get(), sizeof(AiArmyState)),
                     Bytes(&g_aiCensus.get(), sizeof(AiAgentData)),
                     Bytes(&g_aiCommandSender.get(), sizeof(AiCommandSender)),
                     {},
                     g_aiEnemySearch};
        std::memcpy(s.random, world->random_state, sizeof(s.random));
        return s;
    }
    void Restore(CGameData* world) const {
        std::memcpy(reinterpret_cast<std::uint8_t*>(world) + kListsOffset, lists.data(), kListsBytes);  // raw-ok
        std::memcpy(&g_aiGroupOrders.get(), orders.data(), orders.size());
        std::memcpy(&g_aiArmyState.get(), armyState.data(), armyState.size());
        std::memcpy(&g_aiCensus.get(), census.data(), census.size());
        std::memcpy(&g_aiCommandSender.get(), sender.data(), sender.size());
        std::memcpy(world->random_state, random, sizeof(random));
        g_aiEnemySearch = search;
    }
};

struct OrderFunctions {
    NearestFreeFn nearestFree;
    WeakestFrontierFn weakestFrontier;
    NoArgFn mission56;
    CheckTargetsFn checkTargets;
    PointFn pointNear, pointFar;
    PlayerFn sendThieves, sendGeologists;
};

OrderFunctions OriginalOrderFunctions() {
    return {Original_AI_NearestFreeInteriorMilitaryBuilding, Original_AI_WeakestFrontierMilitaryBuilding,
            Original_AI_Mission56FinalAssault,              Original_AI_Army_CheckGroupTargets,
            Original_AI_RandomPointNearSettler,             Original_AI_RandomPointNearSettlerFar,
            Original_AI_SendThieves,                        Original_AI_SendGeologists};
}

OrderFunctions OurOrderFunctions() {
    return {&AI_NearestFreeInteriorMilitaryBuilding, &AI_WeakestFrontierMilitaryBuilding,
            &AI_Mission56FinalAssault,              &AI_Army_CheckGroupTargets,
            &AI_RandomPointNearSettler,             &AI_RandomPointNearSettlerFar,
            &AI_SendThieves,                        &AI_SendGeologists};
}

std::uint16_t SmallCoordinate(test::Rng& rng) { return static_cast<std::uint16_t>(rng.Below(kTestMap)); }

void SetUpOrders(test::Rng& rng, CGameData* world) {
    world->map_size = 100 + rng.Below(40);
    rng.Fill(world->random_state, sizeof(world->random_state));
    for (std::uint32_t x = 0; x < kTestMap + 20; ++x) {
        for (std::uint32_t y = 0; y < kTestMap + 20; ++y) {
            layout::Tile& tile = world->tiles[x][y];
            tile.area = static_cast<std::uint16_t>(rng.Chance(7, 8) ? (x / 70) * 2 + y / 70 : rng.Below(4));
            tile.walk_region = static_cast<std::uint8_t>(rng.Below(3));
            tile.terrain = static_cast<std::uint8_t>(rng.Chance(1, 30) ? kTerrainClassMountain : rng.Below(20));
        }
    }
    for (std::size_t i = 1; i < 20000; ++i) {  // small offsets, never (0, 0) past the first
        layout::MapOffset& offset = world->offsets[i];
        offset.dx = static_cast<std::int32_t>(rng.Below(41)) - 20;
        offset.dy = static_cast<std::int32_t>(rng.Below(41)) - 20;
        if (offset.dx == 0 && offset.dy == 0) {
            offset.dx = 1;
        }
    }
    for (std::size_t i : {kSmallTowerRadiusOffset, kLargeTowerRadiusOffset, kCastleRadiusOffset}) {
        world->offsets[i].distance = static_cast<std::int32_t>(rng.Below(30));
    }
    world->building_high_water = rng.Chance(1, 20) ? 0xffffffffu : rng.Below(kTestBuildings);
    constexpr std::uint8_t kTypes[] = {15, 16, 18, 19, 17, 1};
    for (std::uint32_t id = 1; id < kTestBuildings; ++id) {
        layout::Building& building = world->buildings[id];
        building.owner = static_cast<std::uint8_t>(rng.Chance(1, 6) ? 0xff : rng.Below(3));
        building.art = kTypes[rng.Below(sizeof(kTypes))];
        building.buildup = static_cast<std::uint8_t>(rng.Chance(1, 5) ? 1 + rng.Below(100) : 0);
        building.x = static_cast<std::uint16_t>(20 + rng.Below(80));
        building.y = static_cast<std::uint16_t>(20 + rng.Below(80));
        building.door_x = static_cast<std::uint16_t>(building.x + 1);
        building.door_y = static_cast<std::uint16_t>(building.y + 1);
        building.garrison = static_cast<std::uint16_t>(rng.Chance(1, 4) ? 0 : 1 + rng.Below(kTestSettlers - 1));
    }
    for (std::uint32_t id = 1; id < kTestSettlers; ++id) {
        layout::Settler& settler = world->settlers[id];
        settler.owner = static_cast<std::uint8_t>(rng.Below(3));
        settler.art = static_cast<std::uint8_t>(rng.Chance(1, 2) ? Value(SettlerType::Thief)
                                                                 : rng.Chance(1, 2) ? Value(SettlerType::Geologist)
                                                                                    : rng.Below(48));
        settler.x = SmallCoordinate(rng);
        settler.y = SmallCoordinate(rng);
        // chains: each settler's next is a later one or the end
        settler.next = static_cast<std::uint16_t>(rng.Chance(1, 6) || id + 1 >= kTestSettlers
                                                      ? 0
                                                      : id + 1 + rng.Below(std::min<std::uint32_t>(5, kTestSettlers - id - 1)));
    }
    AiGroupOrders& orders = g_aiGroupOrders.get();
    rng.Fill(&orders, sizeof(orders));
    rng.Fill(&g_aiArmyState.get(), sizeof(AiArmyState));
    rng.Fill(&g_aiCensus.get(), sizeof(AiAgentData));
    AiAgentData& census = g_aiCensus.get();
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        world->players[p].active = static_cast<std::int32_t>(rng.Below(3));
        world->players[p].start_x = SmallCoordinate(rng);
        world->players[p].start_y = SmallCoordinate(rng);
        world->players[p].settler_lists[Value(SettlerType::Thief)] =
            static_cast<std::uint16_t>(rng.Chance(1, 5) ? 0 : 1 + rng.Below(kTestSettlers - 1));
        world->players[p].settler_lists[Value(SettlerType::Geologist)] =
            static_cast<std::uint16_t>(rng.Chance(1, 5) ? 0 : 1 + rng.Below(kTestSettlers - 1));
        orders.thief_cursors[p] = rng.Chance(1, 3) ? 0 : rng.Chance(1, 2) ? 1 + rng.Below(kTestSettlers - 1)
                                                                          : rng.Next32();
        if (orders.thief_cursors[p] >= kTestSettlers) {
            orders.thief_cursors[p] = 0;
        }
        g_aiArmyState.get().geologist_timers[p] = rng.Chance(1, 2) ? 7 + rng.Below(3) : rng.Below(7);
        orders.group_size[p] = static_cast<std::int32_t>(rng.Below(30));
        for (std::uint32_t g = 0; g < kUnitGroups; ++g) {
            const std::int32_t length = rng.Chance(1, 4) ? 0 : static_cast<std::int32_t>(rng.Below(40));
            world->player_list_lengths[p][g] = length;
            for (std::uint32_t k = 0; k < kGroupSlots; ++k) {
                world->player_lists[p][g][k] = static_cast<std::uint16_t>(1 + rng.Below(kTestSettlers - 1));
            }
            census.group_target_buildings[p][g] =
                static_cast<std::uint16_t>(rng.Chance(1, 3) ? 0 : 1 + rng.Below(kTestBuildings - 1));
            census.group_defending[p][g] = static_cast<std::uint8_t>(rng.Chance(2, 3) ? 0 : 1);
            orders.group_flags[p][g] = static_cast<std::uint8_t>(rng.Chance(2, 3) ? 0 : 1);
            orders.group_targets[p][g].x = static_cast<std::uint16_t>(rng.Chance(1, 2) ? 0 : rng.Below(kTestBuildings));
            orders.group_targets[p][g].y = static_cast<std::uint16_t>(rng.Chance(1, 2) ? 0 : rng.Below(kTestBuildings));
            orders.home_areas[p][g] = static_cast<std::uint16_t>(rng.Below(3));
        }
    }
    for (std::uint32_t word = 0; word < kTestSettlers / 32 + 1; ++word) {
        reinterpret_cast<std::uint32_t*>(orders.settlers_with_orders)[word] = rng.Chance(1, 2) ? 0 : rng.Next32();
    }
}

float RandomRatio(test::Rng& rng) {
    switch (rng.Below(6)) {
    case 0: return 1.0f;
    case 1: return 0.5f;
    case 2: return static_cast<float>(rng.Below(300)) / 100.0f;
    case 3: return 1.0f / static_cast<float>(1 + rng.Below(9));
    case 4: {
        const std::uint32_t bits = rng.Next32();
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    default: return static_cast<float>(rng.Below(5)) + 0.1f * static_cast<float>(rng.Below(10));
    }
}

void CompareOrderFunctions(test::Context& ctx, const OrderFunctions& candidate, int cases) {
    const OrderFunctions original = OriginalOrderFunctions();
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    const OrderState saved = OrderState::Capture(world);
    CGameData* savedMapWorld = g_aiMapWorld;
    CGameData* savedWorld = g_aiWorld;
    g_aiMapWorld = world;
    g_aiWorld = world;

    std::vector<std::int32_t> enemyCounts;
    std::size_t nextCount = 0;
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    stubs.Install<4, kCountEnemySoldiers>([&](CGameData*, std::uint32_t, const std::uint32_t*) {
        return static_cast<std::uint32_t>(enemyCounts[nextCount++ % enemyCounts.size()]);
    });
    stubs.Install<4, kSendMoveSettler>([](CGameData*, std::uint32_t, const std::uint32_t*) { return 0x5150u; });
    stubs.Install<4, kSendPatrolSettler>([](CGameData*, std::uint32_t, const std::uint32_t*) { return 0x5151u; });
    stubs.Install<4, kSendMoveGroup>([](CGameData*, std::uint32_t, const std::uint32_t* args) {
        return args[1] * 3 + args[2];
    });
    const auto recorder = reinterpret_cast<std::uint32_t>(&RecordString);
    test::PatchBytes(reinterpret_cast<std::uintptr_t>(&g_importOutputDebugStringA.get()), &recorder, sizeof(recorder));

    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        if (i % 4 == 0) {
            SetUpOrders(rng, world);
        }
        const int op = static_cast<int>(rng.Below(8));
        const std::uint32_t player = rng.Below(3);
        const std::uint32_t x = rng.Below(kTestMap);
        const std::uint32_t y = rng.Below(kTestMap);
        const std::int32_t region = rng.Chance(1, 3) ? -1 - static_cast<std::int32_t>(rng.Below(3))
                                                     : static_cast<std::int32_t>(rng.Below(4));
        const float ratio = RandomRatio(rng);
        const std::uint32_t settler = 1 + rng.Below(kTestSettlers - 1);
        enemyCounts.clear();
        for (int k = 0; k < 64; ++k) {
            enemyCounts.push_back(rng.Chance(1, 2) ? 0 : static_cast<std::int32_t>(rng.Below(rng.Chance(1, 4) ? 50 : 5)));
        }
        const OrderState start = OrderState::Capture(world);
        struct Outcome {
            std::uint32_t result;
            std::int32_t px, py;
            OrderState state;
            std::vector<test::StubCall> calls;
            std::vector<std::string> strings;
        };
        auto run = [&](const OrderFunctions& f) {
            start.Restore(world);
            nextCount = 0;
            Outcome out{};
            g_strings = &out.strings;
            switch (op) {
            case 0: out.result = f.nearestFree(&g_aiEnemySearch.get(), nullptr, player, x, y); break;
            case 1: out.result = f.weakestFrontier(&g_aiEnemySearch.get(), nullptr, 7, player, region); break;
            case 2: out.result = f.mission56(nullptr, nullptr); break;
            case 3: f.checkTargets(player, nullptr, ratio); break;
            case 4: out.result = f.pointNear(settler, &out.px, &out.py); break;
            case 5: out.result = f.pointFar(settler, &out.px, &out.py); break;
            case 6: f.sendThieves(player, nullptr); break;
            default: f.sendGeologists(player, nullptr); break;
            }
            g_strings = nullptr;
            out.state = OrderState::Capture(world);
            out.calls = stubs.Take();
            return out;
        };
        const Outcome a = run(original);
        const Outcome b = run(candidate);
        bool same = S3RE_EXPECT_EQ(a.result, b.result);
        same &= S3RE_EXPECT_EQ(a.px, b.px);
        same &= S3RE_EXPECT_EQ(a.py, b.py);
        same &= S3RE_EXPECT(a.state.lists == b.state.lists);
        same &= S3RE_EXPECT(a.state.orders == b.state.orders);
        same &= S3RE_EXPECT(a.state.armyState == b.state.armyState);
        same &= S3RE_EXPECT(a.state.census == b.state.census);
        same &= S3RE_EXPECT(a.state.sender == b.state.sender);
        same &= S3RE_EXPECT_MEM_EQ(a.state.random, b.state.random, sizeof(a.state.random));
        same &= S3RE_EXPECT_EQ(a.state.search, b.state.search);
        same &= S3RE_EXPECT(a.calls == b.calls);
        same &= S3RE_EXPECT(a.strings == b.strings);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d, player %u", i, op, player);
        }
    }
    test::UnpatchBytes(reinterpret_cast<std::uintptr_t>(&g_importOutputDebugStringA.get()));
    saved.Restore(world);
    g_aiMapWorld = savedMapWorld;
    g_aiWorld = savedWorld;
}

std::uint32_t __fastcall WrongNearestFree(void* self, void* edx, std::uint32_t player, std::uint32_t x,
                                          std::uint32_t y) {
    // Wrong: probes with the building's x (fixes the original quirk).
    const std::uint16_t pointArea = TileAt(g_aiMapWorld, x, y).area;
    std::int32_t nearest = kFarthest;
    std::uint32_t result = 0;
    for (std::uint32_t id = 1; id < g_aiMapWorld.get()->building_high_water + 1; ++id) {
        const layout::Building& building = BuildingAt(g_aiMapWorld, id);
        const auto type = static_cast<BuildingType>(building.art);
        if (building.owner != player || building.buildup != 0 || type == BuildingType::Mill ||
            building.art < 15 || building.art > 19 || UsedByGroup(player, id)) {
            continue;
        }
        const std::uint16_t area = TileAt(g_aiMapWorld, building.door_x, building.door_y).area;
        if (OtherAreaAt(building.x - 18u, building.y, area) | OtherAreaAt(building.x + 18u, building.y, area) |
            OtherAreaAt(building.x - 9u, building.y - 18u, area) | OtherAreaAt(building.x + 9u, building.y + 18u, area)) {
            continue;
        }
        const std::int32_t distance = TileDistance(x, y, building.x, building.y);
        if (distance < nearest && (area == pointArea || pointArea == 0)) {
            nearest = distance;
            result = id;
        }
    }
    (void)self;
    (void)edx;
    return result;
}

std::uint32_t __fastcall WrongWeakestFrontier(void* self, void* edx, std::uint32_t unused, std::uint32_t player,
                                              std::int32_t region) {
    // Wrong: the last of equally weak buildings wins.
    const std::uint32_t first = AI_WeakestFrontierMilitaryBuilding(self, edx, unused, player, region);
    if (first == 0) {
        return 0;
    }
    std::uint32_t chosen = first;
    auto count = [](std::uint32_t id) {
        std::int32_t n = 0;
        for (std::uint32_t s = BuildingAt(g_aiMapWorld, id).garrison; s != 0; s = SettlerAt(g_aiMapWorld, s).next) {
            ++n;
        }
        return n;
    };
    const std::int32_t fewest = count(first);
    for (std::uint32_t id = first + 1; id < g_aiMapWorld.get()->building_high_water + 1; ++id) {
        const layout::Building& b = BuildingAt(g_aiMapWorld, id);
        if (b.owner == player && b.buildup == 0 && b.art == b.art && count(id) == fewest &&
            (b.art == 15 || b.art == 16 || b.art == 18)) {
            chosen = id;  // (not checked for the frontier: also wrong)
        }
    }
    return chosen;
}

void __fastcall WrongCheckTargets(std::uint32_t player, void* edx, float ratio) {
    // Wrong: an unordered compare (NaN ratio) keeps the attack, and half
    // the ratio.
    AI_Army_CheckGroupTargets(player, edx, ratio == ratio ? ratio * 0.5f : 1.0f);
}

std::uint32_t __fastcall WrongPointNear(std::uint32_t settler, std::int32_t* x, std::int32_t* y) {
    // Wrong: y before x.
    const std::int32_t dy = RandomOffset(kNearMask);
    const std::int32_t dx = RandomOffset(kNearMask);
    const layout::Settler& record = SettlerAt(g_aiMapWorld, settler);
    *x = record.x + dx;
    *y = record.y + dy;
    return ClampToMap(x, y);
}

void __fastcall WrongSendThieves(std::uint32_t player, void* edx) {
    // Wrong: the cursor is always stored.
    AI_SendThieves(player, edx);
    std::uint32_t& cursor = g_aiGroupOrders.get().thief_cursors[player];
    if (cursor == 0) {
        cursor = 1;
    }
}

void __fastcall WrongSendGeologists(std::uint32_t player, void* edx) {
    // Wrong: works every 6th call.
    std::uint32_t& calls = g_aiArmyState.get().geologist_timers[player];
    if (calls == 6) {
        calls = 7;
    }
    AI_SendGeologists(player, edx);
}

}  // namespace

S3RE_TEST(AiArmyOrders_MatchOriginal) {
    CompareOrderFunctions(ctx, OurOrderFunctions(), 4000);
}

S3RE_TEST(AiArmyOrders_ComparisonDetectsWrongImplementation) {
    int index = 0;
    const auto probe = [&](auto change) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        OrderFunctions wrong = OurOrderFunctions();
        change(wrong);
        CompareOrderFunctions(quiet, wrong, 3000);
        if (quiet.failures() == 0) {
            ctx.Fail(__FILE__, __LINE__, "  wrong implementation %d not detected", index);
        }
        ++index;
    };
    probe([](OrderFunctions& f) { f.nearestFree = &WrongNearestFree; });
    probe([](OrderFunctions& f) { f.weakestFrontier = &WrongWeakestFrontier; });
    probe([](OrderFunctions& f) { f.checkTargets = &WrongCheckTargets; });
    probe([](OrderFunctions& f) { f.pointNear = &WrongPointNear; });
    probe([](OrderFunctions& f) { f.sendThieves = &WrongSendThieves; });
    probe([](OrderFunctions& f) { f.sendGeologists = &WrongSendGeologists; });
}

}  // namespace s3re::game
