// The military AI's periodic attack work (Rainer\CompMapWorkerAgent.cpp;
// re/computer_players.md "Army"): attack delays, catapult hunts, hunters
// for enemy thieves and mages, and recruiting geologists and thieves. All
// are called by AI::RunMilitary 0x58f990, each every so many steps. None
// range-check the player or group.
//
//   0x00594000 AI::ArmyState::TickAttackDelays() thiscall (on
//              g_aiArmyState): every positive attack delay (+0x194, players
//              0..19, groups 1..9; signed) goes down by one; one that
//              reaches 0 is logged ("RESET wegen abgelaufenem Attackdelay
//              ...", sprintf 0x61f27e and OutputDebugStringA) and its group
//              reset (AI::ResetUnitGroup 0x5930f0 on g_aiGroupOrders). EAX
//              is not used by the caller.
//   0x00593180 AI::Army::AttackEnemyCatapults(player) thiscall (on
//              g_aiGroupOrders), ret 4: for every other playing player
//              (Player::active != 0) of another team (Player::team) that is
//              a human (active 1) or whose team has an active player
//              (AI::TeamHasActivePlayer 0x593140, ECX = this), every
//              catapult in its vector (g_aiCatapultVectors): one standing in
//              an economy area of `player` is attacked by the group 1..9
//              with at least two members (signed) that doesn't defend
//              (census +0x194) whose first member is nearest (signed
//              TileDistance, first on a tie): a CGameData::Random draw
//              (its low word, logged "RANDOM_55 wert = %u" through sprintf
//              and DebugLog) adds 70 - draw % 17 to the group's attack
//              delay, the group's patrol buildings become 0, its prey the
//              catapult (a word), its steal flag 0; logged ("Catapult fight
//              ...", OutputDebugStringA) and sent (SendMoveGroup 0x590c80).
//              A catapult elsewhere resets every group 1..9 of `player`
//              whose prey it is (ResetUnitGroup). EAX is not used by the
//              caller.
//   0x00592e60 AI::RecruitGeologistsAndThieves() (no `this`): for every
//              computer player (active 2), once the census has counted the
//              buildings (census byte +1) and when the player has a
//              residence (census buildings of types 26, 27 or 28 above 0,
//              signed): wanted = other settlers (census +0x3a4) / 64 + 1
//              (signed division); if the census geologists (+0x2b4) are
//              below that, every economy area 1..area_high_water of the
//              player whose carrier list (Area::settlers_by_type[0]) isn't
//              empty gets a ConvertCarriers order for `wanted` geologists
//              at its first carrier (AI::SendConvertCarriers 0x590c10, kind
//              1) and the count goes up by `wanted` (no re-check per area);
//              then the same for thieves (+0x304, kind 2). EAX is not used.
//   0x005935b0 AI::Army::SendHunter(player, prey) thiscall (`this` unused),
//              ret 8: sends a hunter after an enemy settler in the player's
//              land. The player's hunter vector (g_aiHunterVectors,
//              {hunter, target} pairs) is walked: a hunter already after the
//              prey is sent to it again (SendMoveSettler 0x590a30) and the
//              function ends; one after another target within 4 tiles of
//              the prey (signed TileDistance) ends it too; of the free
//              hunters (target 0) the nearest is kept (first on a tie).
//              Then the groups 1..9 with at least two members (signed): a
//              group whose first member is nearer than the best so far
//              replaces it. A free hunter: its target becomes the prey and
//              it is sent. A group: original bug, kept: its search loop
//              (0x593840) tests the type of the group's LAST member eleven
//              times instead of walking the members, so the hunter is the
//              last member if it is a swordsman, pikeman or bowman (types 5,
//              32, 35, 15, 31, 34, 8, 30, 33), else nobody; it leaves the
//              group (length - 1), is sent and {hunter, prey} is appended
//              to the vector (std::vector::insert at end: in place when the
//              capacity, compared as a signed difference shifted then
//              unsigned, allows; else a new block of size + max(size, 1)
//              entries (1 if begin is null or size <= 1) from operator new
//              0x61efb9, the entries copied, the old block freed with
//              operator delete 0x61e4ef even when null). EAX is not used
//              by the callers.
//   0x00593ad0 AI::Army::HuntEnemyThieves(player) and
//   0x00593bd0 AI::Army::HuntEnemyMages(player) thiscall, ret 4: for every
//              other playing player of another team that is human or whose
//              team has an active player (as AttackEnemyCatapults), each of
//              its thieves (Player::settler_lists[23]) / mages ([36]) that
//              stands in an economy area of `player` gets SendHunter (with
//              this `this`). EAX is not used by the caller.
//   0x00594080 AI::StartAttack(player ECX, float ratio), ret 4: in mission
//              56 from tick 81000 on, Mission56FinalAssault 0x594d40 instead.
//              Otherwise the first 31 calls only count (g_aiArmyState
//              +0x190), and nothing happens while a gather building is set
//              (+0x898) or fewer than 15 soldiers are counted (census
//              soldiers, 9 kinds, 0 for an inactive player). The enemy is
//              the other team's playing player with a tower, castle or
//              barracks (census buildings) whose start position is in the
//              same walk region (StartPositionWalkRegion 0x55b6e0) with the
//              weakest team strength (census +0x25c, signed, first on a
//              tie); a computer enemy whose team has no active player
//              (TeamHasActivePlayer 0x593140) counts only on a Random draw
//              below 200 (no draw in mission 70 for player 2, mission 72
//              for player 1, and for humans). A group already attacking
//              (target building, not defending) ends it. With an enemy
//              with soldiers it attacks (x87, done in double: the soldiers
//              times `ratio` (s, also kept as a float) times 0.75 above the
//              enemy's soldiers, or the enemy's team strength times 2.2
//              below its own times `ratio`, unordered counting as below;
//              or more than 4 times the enemy's soldiers; or the mission 70
//              / 72 exceptions): the target is the enemy's weakest frontier
//              building in its walk region (0x58a7e0), dropped (0) when more
//              than s (float) times 1.5 enemy soldiers are within 60 of it
//              (CountEnemySoldiersNear 0x58a2c0); when the enemy has no
//              towers or castles but barracks and the player at least as
//              many spare groups (0x591fb0) as the enemy has barracks, the
//              enemy's first finished barracks is the target. Then
//              CountSpareGroups and CountSpareSoldiers run (results
//              unused), the gather building is the nearest interior one to
//              the target (0x58acd0; the gather countdown +0xa0 becomes
//              15) and up to 50 spare groups (FindSpareGroup 0x5923e0,
//              any delay) get the target (census +4), lose their patrol
//              buildings and steal flag, add 2200 - r % 550 to their
//              attack delay (Random, logged "RANDOM_55 wert = %u" through
//              DebugLog) and go (logged "Start Attack ..." or "Start Gather
//              ...", SendMoveGroup) to the target or the gather building.
//              Otherwise (a steal), when its soldiers minus its towers and
//              castles are above 10: the first finished military building
//              (SiteKindOf 0x58ed70 == 3) of another team's playing player
//              (a computer one only on a Random draw below 200) for which
//              a spare group without delay exists whose first member is in
//              the door's walk region, at most 120 tiles away, and whose
//              tenth (unsigned) reaches the enemy soldiers within 60 of
//              the building: that group gets it as target with the steal
//              flag, 900 - r % 225 delay ("Start Steal ...") and goes there.
//              EAX is not used by the caller.

#include "game/ai/ai_layout.h"
#include "game/enums/buildings.h"
#include "game/enums/settler_types.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/texts.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace s3re::game {

using namespace ai;
using layout::kPlayers;

namespace {

constexpr std::uint32_t kGroupSlots = 100;
constexpr std::uint32_t kFirstGroup = 1;
constexpr std::uint32_t kLastGroup = 9;
constexpr std::int32_t kFarthest = 0x7fffffff;
constexpr std::int32_t kHumanPlayer = 1;            // Player::active
constexpr std::int32_t kComputerControlled = 2;
constexpr std::int32_t kCatapultHunters = 2;        // a group needs this many members to hunt a catapult
constexpr std::uint32_t kCatapultDelay = 70;        // - up to 16
constexpr std::uint32_t kCatapultDelaySpread = 17;
constexpr std::int32_t kSettlersPerRecruit = 64;    // one geologist / thief per 64 other settlers (+1)
constexpr std::uint32_t kRecruitGeologists = 1;     // SendConvertCarriers kinds
constexpr std::uint32_t kRecruitThieves = 2;
constexpr std::size_t kTextBytes = 256;             // the original's stack buffers

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kRandom = 0x004feb70;            // CGameData::Random (thiscall)
constexpr std::uintptr_t kTileDistance = 0x004ff320;      // TileDistance (stdcall)
constexpr std::uintptr_t kResetUnitGroup = 0x005930f0;    // AI::ResetUnitGroup (stdcall; ECX = g_aiGroupOrders)
constexpr std::uintptr_t kTeamHasActivePlayer = 0x00593140;  // AI::TeamHasActivePlayer (stdcall)
constexpr std::uintptr_t kSendMoveGroup = 0x00590c80;     // AI::SendMoveGroup
constexpr std::uintptr_t kSendConvertCarriers = 0x00590c10;  // AI::SendConvertCarriers
constexpr std::uintptr_t kSprintf = 0x0061f27e;           // the CRT's sprintf
constexpr std::uintptr_t kDebugLog = 0x00428720;          // DebugLog (cdecl)
constexpr std::uintptr_t kSendMoveSettler = 0x00590a30;   // AI::SendMoveSettler
constexpr std::uintptr_t kSendHunter = 0x005935b0;        // AI::Army::SendHunter
constexpr std::uintptr_t kOperatorNew = 0x0061efb9;       // the game's operator new (cdecl)
constexpr std::uintptr_t kOperatorDelete = 0x0061e4ef;    // the game's operator delete (cdecl)
constexpr std::int32_t kHuntersNear = 4;                  // a hunter after a target this close to the prey: nothing to do
constexpr std::int32_t kGroupHunters = 2;                 // a group needs this many members to give a hunter
constexpr std::uintptr_t kMission56FinalAssault = 0x00594d40;  // AI::Mission56FinalAssault
constexpr std::uintptr_t kStartPositionWalkRegion = 0x0055b6e0;  // AI::StartPositionWalkRegion (thiscall on the census)
constexpr std::uintptr_t kWeakestFrontier = 0x0058a7e0;    // AI::WeakestFrontierMilitaryBuilding
constexpr std::uintptr_t kCountEnemySoldiers = 0x0058a2c0;  // AI::CountEnemySoldiersNear
constexpr std::uintptr_t kCountSpareGroups = 0x00591fb0;   // AI::Army::CountSpareGroups
constexpr std::uintptr_t kCountSpareSoldiers = 0x00592100;  // AI::Army::CountSpareSoldiers
constexpr std::uintptr_t kFindSpareGroup = 0x005923e0;     // AI::Army::FindSpareGroup
constexpr std::uintptr_t kNearestInterior = 0x0058acd0;    // AI::NearestInteriorMilitaryBuilding
constexpr std::uintptr_t kSiteKindOf = 0x0058ed70;         // AI::SiteKindOf (fastcall, CX)
constexpr std::int32_t kFinalAssaultMission = 56;
constexpr std::uint32_t kFinalAssaultTick = 81000;
constexpr std::int32_t kMission70 = 70;                    // player 2 attacks without the strength checks
constexpr std::uint32_t kMission70Player = 2;
constexpr std::int32_t kMission72 = 72;
constexpr std::uint32_t kMission72Player = 1;
constexpr std::uint32_t kWarmUpCalls = 30;                 // StartAttack does nothing in its first 31 calls
constexpr std::int32_t kMinSoldiers = 15;
constexpr std::uint16_t kLonelyComputerDraw = 200;         // a computer enemy without an active teammate counts below this
constexpr float kAttackSoldierShare = 0.75f;               // 0x65a8f0
constexpr float kTeamStrengthMargin = 2.2f;                // 0x65a8ec
constexpr float kDefenderShare = 1.5f;                     // 0x65a8e8
constexpr std::int32_t kOutnumbering = 4;
constexpr std::uint32_t kAttackDistance = 60;              // enemies counted around the target
constexpr std::int32_t kGatherCountdown = 15;
constexpr std::int32_t kMaxAttackingGroups = 50;
constexpr std::uint32_t kAttackDelay = 2200;               // - up to 549
constexpr std::uint32_t kAttackDelaySpread = 550;
constexpr std::int32_t kStealSurplus = 10;                 // soldiers beyond the towers and castles needed to steal
constexpr std::uint8_t kMilitarySite = 3;                  // AI::SiteKindOf of a military building
constexpr std::int32_t kStealReach = 120;
constexpr std::uint32_t kStealDelay = 900;                 // - up to 224
constexpr std::uint32_t kStealDelaySpread = 225;
constexpr std::uint32_t kStealShare = 10;                  // a tenth of the group must reach the defenders

std::uint32_t Random() {
    using Fn = std::uint32_t(__fastcall*)(CGameData* world, void* edx);
    return reinterpret_cast<Fn>(kRandom)(g_aiMapWorld, nullptr);
}

std::int32_t TileDistance(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1) {
    using Fn = std::int32_t(__stdcall*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
    return reinterpret_cast<Fn>(kTileDistance)(x0, y0, x1, y1);
}

void ResetUnitGroup(std::uint32_t player, std::uint32_t group) {
    using Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t player, std::uint32_t group);
    reinterpret_cast<Fn>(kResetUnitGroup)(&g_aiGroupOrders.get(), nullptr, player, group);
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

// A Random draw's low word, logged "RANDOM_55 wert = %u" through DebugLog.
std::uint32_t LoggedRandom() {
    const std::uint32_t draw = Random() & 0xffff;
    char text[kTextBytes];
    Format(text, texts::kRandom55, draw);
    using DebugLogFn = void(__cdecl*)(const char* text);
    reinterpret_cast<DebugLogFn>(kDebugLog)(text);
    return draw;
}

const layout::Settler& SettlerAt(const CGameData* world, std::uint32_t id) { return (&world->settlers[0])[id]; }

// The economy area of the tile a settler stands on.
const layout::Area& AreaOfSettler(const CGameData* world, std::uint32_t settler) {
    const layout::Settler& record = SettlerAt(world, settler);
    const std::uint16_t area = (&world->tiles[0][0])[record.x * layout::kMapRowTiles + record.y].area;
    return world->areas[area];
}

std::int32_t ListLength(const CGameData* world, std::uint32_t index) {
    return (&world->player_list_lengths[0][0])[index];
}

const layout::Settler& FirstMember(const CGameData* world, std::uint32_t index) {
    return SettlerAt(world, (&world->player_lists[0][0][0])[index * kGroupSlots]);
}

// 0x593214..0x593416: one catapult of an enemy: hunted by the nearest group
// if it stands in the player's land, else the hunts for it end.
void HandleEnemyCatapult(std::uint32_t player, std::uint32_t catapult) {
    const CGameData* world = g_aiMapWorld;
    AiGroupOrders& orders = g_aiGroupOrders.get();
    const std::uint32_t base = player * kUnitGroups;
    if (AreaOfSettler(world, catapult).player != player) {
        for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
            if ((&orders.group_targets[0][0])[base + group].prey == catapult) {
                ResetUnitGroup(player, group);
            }
        }
        return;
    }
    std::int32_t nearest = kFarthest;
    std::int32_t chosen = -1;
    for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
        const std::uint32_t index = base + group;
        world = g_aiMapWorld;
        if (ListLength(world, index) < kCatapultHunters || (&g_aiCensus.get().group_defending[0][0])[index] != 0) {
            continue;
        }
        const layout::Settler& first = FirstMember(world, index);
        const layout::Settler& target = SettlerAt(world, catapult);
        const std::int32_t distance = TileDistance(target.x, target.y, first.x, first.y);
        if (distance < nearest) {
            nearest = distance;
            chosen = static_cast<std::int32_t>(group);
        }
    }
    if (chosen <= 0) {
        return;
    }
    const auto group = static_cast<std::uint32_t>(chosen);
    const std::uint32_t index = base + group;
    const std::uint32_t draw = LoggedRandom();
    (&g_aiArmyState.get().attack_delays[0][0])[index] += kCatapultDelay - draw % kCatapultDelaySpread;
    AiGroupTarget& groupTarget = (&orders.group_targets[0][0])[index];
    groupTarget.patrol0 = 0;
    groupTarget.patrol1 = 0;
    groupTarget.prey = static_cast<std::uint16_t>(catapult);
    (&orders.group_flags[0][0])[index] = 0;
    LogDebugString(texts::kAiCatapultFight, player, group, catapult);
    const layout::Settler& target = SettlerAt(g_aiMapWorld, catapult);
    using SendFn = std::uint32_t(__fastcall*)(AiCommandSender* self, void* edx, std::uint32_t player,
                                              std::uint32_t group, std::uint32_t x, std::uint32_t y);
    reinterpret_cast<SendFn>(kSendMoveGroup)(&g_aiCommandSender.get(), nullptr, player, group, target.x, target.y);
}

// 0x592ec0..0x592f61 / 0x592f65..0x593009: ConvertCarriers orders in every
// area of the player while `count` is below the wanted number.
void Recruit(std::uint32_t player, std::int32_t& count, std::uint32_t kind) {
    const std::int32_t wanted = g_aiCensus.get().other_settlers[player] / kSettlersPerRecruit + 1;
    if (count >= wanted) {
        return;
    }
    for (std::uint32_t area = 1; area < g_aiMapWorld.get()->area_high_water + 1; ++area) {
        const layout::Area& record = g_aiMapWorld.get()->areas[area];
        if (record.player != player || record.settlers_by_type[Value(SettlerType::Carrier)] == 0) {
            continue;
        }
        const layout::Settler& carrier = SettlerAt(g_aiMapWorld, record.settlers_by_type[Value(SettlerType::Carrier)]);
        using SendFn = std::uint32_t(__fastcall*)(AiCommandSender* self, void* edx, std::uint32_t player,
                                                  std::uint32_t kind, std::uint32_t count, std::uint32_t x,
                                                  std::uint32_t y);
        reinterpret_cast<SendFn>(kSendConvertCarriers)(&g_aiCommandSender.get(), nullptr, player, kind,
                                                       static_cast<std::uint32_t>(wanted), carrier.x, carrier.y);
        count += wanted;
    }
}

// An entry of a hunter vector (g_aiHunterVectors).
struct HunterEntry {
    std::uint32_t hunter;  // +0 the settler sent hunting
    std::uint32_t target;  // +4 the settler it hunts, 0 = none
};
static_assert(sizeof(HunterEntry) == 8);

std::uint32_t SendMoveSettler(std::uint32_t player, std::uint32_t settler, std::uint32_t x, std::uint32_t y) {
    using SendFn = std::uint32_t(__fastcall*)(AiCommandSender* self, void* edx, std::uint32_t player,
                                              std::uint32_t settler, std::uint32_t x, std::uint32_t y);
    return reinterpret_cast<SendFn>(kSendMoveSettler)(&g_aiCommandSender.get(), nullptr, player, settler, x, y);
}

// The soldier types SendHunter takes from a group (0x593840..0x593862).
bool IsHunterType(std::uint8_t type) {
    switch (static_cast<SettlerType>(type)) {
    case SettlerType::Swordsman1:
    case SettlerType::Swordsman2:
    case SettlerType::Swordsman3:
    case SettlerType::Pikeman1:
    case SettlerType::Pikeman2:
    case SettlerType::Pikeman3:
    case SettlerType::Bowman1:
    case SettlerType::Bowman2:
    case SettlerType::Bowman3:
        return true;
    default:
        return false;
    }
}

// 0x5938cc..0x593abd: the vector's insert(end(), 1, entry) (MSVC 6), with
// the original's capacity arithmetic and null checks.
void AppendHunter(AiVector& hunters, const HunterEntry& entry) {
    const std::uint32_t end = hunters.end;
    const auto room = static_cast<std::uint32_t>(static_cast<std::int32_t>(hunters.unk8 - end) >> 3);
    if (room >= 1) {
        if (end != 0) {
            *reinterpret_cast<HunterEntry*>(end) = entry;  // std::_Construct 0x596650
        }
        hunters.end += sizeof(HunterEntry);
        return;
    }
    const std::uint32_t begin = hunters.begin;
    const auto size = static_cast<std::uint32_t>(static_cast<std::int32_t>(end - begin) >> 3);
    std::uint32_t grow = 1;
    if (begin != 0 && size > 1) {
        grow = size;
    }
    std::int32_t capacity = static_cast<std::int32_t>(grow + (begin == 0 ? 0 : size));
    const std::int32_t allocated = capacity < 0 ? 0 : capacity;
    using NewFn = void*(__cdecl*)(std::uint32_t bytes);
    auto* block = static_cast<std::uint8_t*>(
        reinterpret_cast<NewFn>(kOperatorNew)(static_cast<std::uint32_t>(allocated) * sizeof(HunterEntry)));
    std::uint8_t* out = block;
    for (std::uint32_t in = begin; in != end; in += sizeof(HunterEntry), out += sizeof(HunterEntry)) {
        if (out != nullptr) {
            *reinterpret_cast<HunterEntry*>(out) = *reinterpret_cast<const HunterEntry*>(in);
        }
    }
    if (out != nullptr) {
        *reinterpret_cast<HunterEntry*>(out) = entry;  // std::_Construct 0x596650
    }
    using DeleteFn = void(__cdecl*)(void* block);
    reinterpret_cast<DeleteFn>(kOperatorDelete)(reinterpret_cast<void*>(begin));
    hunters.unk8 = reinterpret_cast<std::uint32_t>(block) + static_cast<std::uint32_t>(capacity) * sizeof(HunterEntry);
    const std::uint32_t oldSize =
        hunters.begin == 0 ? 0 : static_cast<std::uint32_t>(static_cast<std::int32_t>(hunters.end - hunters.begin) >> 3);
    hunters.begin = reinterpret_cast<std::uint32_t>(block);
    hunters.end = reinterpret_cast<std::uint32_t>(block) + oldSize * sizeof(HunterEntry) + sizeof(HunterEntry);
}

// 0x593ad0 / 0x593bd0: the enemies' settlers of one list in the player's land
// get hunters.
void HuntEnemies(AiGroupOrders* self, std::uint32_t player, SettlerType type) {
    for (std::uint32_t other = 0; other < kPlayers; ++other) {
        const layout::Player& record = g_aiMapWorld.get()->players[other];
        if (record.active == 0 || other == player || record.team == g_aiMapWorld.get()->players[player].team) {
            continue;
        }
        if (record.active != kHumanPlayer) {
            using TeamFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t player);
            if ((reinterpret_cast<TeamFn>(kTeamHasActivePlayer)(self, nullptr, other) & 0xff) == 0) {
                continue;
            }
        }
        for (std::uint32_t prey = g_aiMapWorld.get()->players[other].settler_lists[Value(type)]; prey != 0;
             prey = SettlerAt(g_aiMapWorld, prey).next) {
            if (AreaOfSettler(g_aiMapWorld, prey).player == player) {
                using SendHunterFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t player,
                                                                std::uint32_t prey);
                reinterpret_cast<SendHunterFn>(kSendHunter)(self, nullptr, player, prey);
            }
        }
    }
}

// 0x5940e2..0x594172: the census soldiers of a player (0 if not playing),
// summed as the original does (three kinds at a time, wrapping).
std::int32_t CensusSoldiers(std::uint32_t player) {
    if (g_aiMapWorld.get()->players[player].active == 0) {
        return 0;
    }
    const std::uint32_t* soldiers = g_aiCensus.get().soldiers[player];
    const std::uint32_t first = soldiers[2] + soldiers[1] + soldiers[0];
    const std::uint32_t second = soldiers[5] + soldiers[4] + soldiers[3];
    const std::uint32_t third = soldiers[6] + soldiers[8] + soldiers[7];
    return static_cast<std::int32_t>(third + first + second);
}

std::uint32_t CensusBuildings(std::uint32_t player, BuildingType type) {
    return g_aiCensus.get().buildings[player][Value(type)];
}

// Towers and castles of a player in the census (wrapping like the original).
std::uint32_t CensusTowers(std::uint32_t player) {
    return CensusBuildings(player, BuildingType::Castle) + CensusBuildings(player, BuildingType::LargeTower) +
           CensusBuildings(player, BuildingType::SmallTower);
}

std::uint32_t StartRegion(std::uint32_t player) {
    using Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t player);
    return reinterpret_cast<Fn>(kStartPositionWalkRegion)(&g_aiCensus.get(), nullptr, player);
}

bool TeamHasActivePlayer(std::uint32_t player) {
    using TeamFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t player);
    return (reinterpret_cast<TeamFn>(kTeamHasActivePlayer)(&g_aiGroupOrders.get(), nullptr, player) & 0xff) != 0;
}

std::int32_t FindSpareGroup(std::uint32_t player, std::uint32_t building, std::uint32_t anyDelay) {
    using Fn = std::int32_t(__fastcall*)(void* self, void* edx, std::uint32_t player, std::uint32_t building,
                                         std::uint32_t anyDelay);
    return reinterpret_cast<Fn>(kFindSpareGroup)(&g_aiGroupOrders.get(), nullptr, player, building, anyDelay);
}

std::uint32_t CountEnemySoldiersNear(std::uint32_t x, std::uint32_t y, std::uint32_t distance, std::uint32_t player) {
    using Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t, std::uint32_t, std::uint32_t,
                                          std::uint32_t);
    return reinterpret_cast<Fn>(kCountEnemySoldiers)(&g_aiEnemySearch.get(), nullptr, x, y, distance, player);
}

const layout::Building& BuildingAt(const CGameData* world, std::uint32_t id) { return (&world->buildings[0])[id]; }

void SendGroup(std::uint32_t player, std::uint32_t group, std::uint32_t building) {
    const layout::Building& record = BuildingAt(g_aiMapWorld, building);
    using SendFn = std::uint32_t(__fastcall*)(AiCommandSender* self, void* edx, std::uint32_t player,
                                              std::uint32_t group, std::uint32_t x, std::uint32_t y);
    reinterpret_cast<SendFn>(kSendMoveGroup)(&g_aiCommandSender.get(), nullptr, player, group, record.x, record.y);
}

// 0x5941aa..0x5942a4: the enemy to measure up to: -1 if none.
std::int32_t ChooseEnemy(std::uint32_t player) {
    const std::int32_t mission = g_campaignMission;
    std::int32_t weakest = kFarthest;
    std::int32_t enemy = -1;
    for (std::uint32_t other = 0; other < kPlayers; ++other) {
        const CGameData* world = g_aiMapWorld;
        if (world->players[other].active == 0 || world->players[other].team == world->players[player].team) {
            continue;
        }
        if (static_cast<std::int32_t>(CensusTowers(other)) <= 0 &&
            static_cast<std::int32_t>(CensusBuildings(other, BuildingType::Barracks)) <= 0) {
            continue;
        }
        const std::uint32_t region = StartRegion(other);
        if (region != StartRegion(player)) {
            continue;
        }
        const bool scripted = (mission == kMission70 && player == kMission70Player) ||
                              (mission == kMission72 && player == kMission72Player);
        if (!scripted && g_aiMapWorld.get()->players[other].active != kHumanPlayer && !TeamHasActivePlayer(other) &&
            (Random() & 0xffff) >= kLonelyComputerDraw) {
            continue;
        }
        const auto strength = static_cast<std::int32_t>(g_aiCensus.get().team_strength[other]);
        if (strength < weakest) {
            weakest = strength;
            enemy = static_cast<std::int32_t>(other);
        }
    }
    return enemy;
}

// 0x594428..0x5946eb: an attack on the enemy's weakest frontier building
// (or its barracks), or a gathering first.
void LaunchAttack(std::uint32_t player, std::uint32_t enemy, float strength, bool enemyWithoutTowers) {
    const std::uint32_t region = StartRegion(player);
    using WeakestFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t unused, std::uint32_t player,
                                                 std::uint32_t region);
    std::uint32_t target = reinterpret_cast<WeakestFn>(kWeakestFrontier)(&g_aiEnemySearch.get(), nullptr, player,
                                                                         enemy, region);
    const layout::Building& weakest = BuildingAt(g_aiMapWorld, target);
    const auto defenders = static_cast<std::int32_t>(CountEnemySoldiersNear(weakest.x, weakest.y, kAttackDistance, player));
    // 0x594479: x87 (fild, fld float, fmul float, fcompp); unordered counts as below
    if (!(static_cast<double>(strength) * static_cast<double>(kDefenderShare) >= static_cast<double>(defenders))) {
        target = 0;
    }
    using CountFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t player);
    if (enemyWithoutTowers) {
        const auto barracks = static_cast<std::int32_t>(CensusBuildings(enemy, BuildingType::Barracks));
        if (barracks > 0 && static_cast<std::int32_t>(reinterpret_cast<CountFn>(kCountSpareGroups)(
                                &g_aiGroupOrders.get(), nullptr, player)) >= barracks) {
            // 0x5944c1..0x5944f9: the enemy's first finished barracks
            const CGameData* world = g_aiMapWorld;
            const std::uint32_t end = world->building_high_water + 1;
            for (std::uint32_t id = 1; id < end; ++id) {
                const layout::Building& building = BuildingAt(world, id);
                if (building.owner == enemy && building.buildup == 0 && building.art == Value(BuildingType::Barracks)) {
                    target = id;
                    break;
                }
            }
        }
    }
    reinterpret_cast<CountFn>(kCountSpareGroups)(&g_aiGroupOrders.get(), nullptr, player);    // (unused)
    reinterpret_cast<CountFn>(kCountSpareSoldiers)(&g_aiGroupOrders.get(), nullptr, player);  // (unused)
    std::int32_t group = FindSpareGroup(player, target, 1);
    AiGroupOrders& orders = g_aiGroupOrders.get();
    if (static_cast<std::int32_t>(target) > 0) {
        const layout::Building& record = BuildingAt(g_aiMapWorld, target);
        using NearestFn = std::int32_t(__fastcall*)(void* self, void* edx, std::uint32_t player, std::uint32_t x,
                                                    std::uint32_t y);
        const std::int32_t gather =
            reinterpret_cast<NearestFn>(kNearestInterior)(&g_aiEnemySearch.get(), nullptr, player, record.x, record.y);
        if (gather > 0) {
            orders.gather_buildings[player] = static_cast<std::uint32_t>(gather);
            g_aiArmyState.get().gather_countdowns[player] = kGatherCountdown;
        }
    }
    if (group == -1) {
        return;
    }
    for (std::int32_t sent = 0; static_cast<std::int32_t>(target) > 0 && sent < kMaxAttackingGroups; ++sent) {
        const std::uint32_t index = player * kUnitGroups + static_cast<std::uint32_t>(group);
        (&g_aiCensus.get().group_target_buildings[0][0])[index] = static_cast<std::uint16_t>(target);
        AiGroupTarget& groupTarget = (&orders.group_targets[0][0])[index];
        groupTarget.patrol0 = 0;
        groupTarget.patrol1 = 0;
        (&orders.group_flags[0][0])[index] = 0;
        const std::uint32_t draw = LoggedRandom();
        (&g_aiArmyState.get().attack_delays[0][0])[index] += kAttackDelay - draw % kAttackDelaySpread;
        const std::uint32_t gather = orders.gather_buildings[player];
        if (gather == 0) {
            LogDebugString(texts::kAiStartAttack, player, group, target);
            SendGroup(player, static_cast<std::uint32_t>(group), target);
        } else {
            LogDebugString(texts::kAiStartGather, player, group, gather);
            SendGroup(player, static_cast<std::uint32_t>(group), orders.gather_buildings[player]);
        }
        group = FindSpareGroup(player, target, 1);
        if (group == -1) {
            return;
        }
    }
}

// 0x594702..0x594921: a steal: a spare group goes for a weakly guarded
// enemy military building near it.
void TrySteal(std::uint32_t player, std::int32_t mySoldiers) {
    const auto surplus = static_cast<std::int32_t>(static_cast<std::uint32_t>(mySoldiers) - CensusTowers(player));
    if (surplus <= kStealSurplus) {
        return;
    }
    for (std::uint32_t id = 1; id < g_aiMapWorld.get()->building_high_water + 1; ++id) {
        const CGameData* world = g_aiMapWorld;
        const layout::Building& building = BuildingAt(world, id);
        const std::uint32_t owner = building.owner;
        if (owner == 0xff || building.buildup != 0) {
            continue;
        }
        using KindFn = std::uint32_t(__fastcall*)(std::uint32_t type);
        if ((reinterpret_cast<KindFn>(kSiteKindOf)(building.art) & 0xff) != kMilitarySite) {
            continue;
        }
        world = g_aiMapWorld;
        const std::int32_t active = world->players[owner].active;
        if (active == 0 || owner == player || world->players[owner].team == world->players[player].team) {
            continue;
        }
        if (active != kHumanPlayer && (Random() & 0xffff) >= kLonelyComputerDraw) {
            continue;
        }
        const std::int32_t group = FindSpareGroup(player, id, 0);
        if (group == -1) {
            continue;
        }
        world = g_aiMapWorld;
        const std::uint32_t index = player * kUnitGroups + static_cast<std::uint32_t>(group);
        const layout::Settler& first = FirstMember(world, index);
        const layout::Building& target = BuildingAt(world, id);
        const layout::Tile* tiles = &world->tiles[0][0];
        if (tiles[target.door_x * layout::kMapRowTiles + target.door_y].walk_region !=
            tiles[first.x * layout::kMapRowTiles + first.y].walk_region) {
            continue;
        }
        if (TileDistance(first.x, first.y, target.x, target.y) > kStealReach) {
            continue;
        }
        const std::uint32_t defenders = CountEnemySoldiersNear(target.x, target.y, kAttackDistance, player);
        if (static_cast<std::uint32_t>(ListLength(g_aiMapWorld, index)) / kStealShare < defenders) {
            continue;
        }
        AiGroupOrders& orders = g_aiGroupOrders.get();
        (&g_aiCensus.get().group_target_buildings[0][0])[index] = static_cast<std::uint16_t>(id);
        AiGroupTarget& groupTarget = (&orders.group_targets[0][0])[index];
        groupTarget.patrol0 = 0;
        groupTarget.patrol1 = 0;
        (&orders.group_flags[0][0])[index] = 1;
        const std::uint32_t draw = LoggedRandom();
        (&g_aiArmyState.get().attack_delays[0][0])[index] += kStealDelay - draw % kStealDelaySpread;
        LogDebugString(texts::kAiStartSteal, player, group, id);
        SendGroup(player, static_cast<std::uint32_t>(group), id);
        return;
    }
}

}  // namespace

using TickDelaysFn = void(__fastcall*)(AiArmyState* self, void* edx);
using CatapultsFn = void(__fastcall*)(AiGroupOrders* self, void* edx, std::uint32_t player);
using RecruitFn = void(__fastcall*)(void* ecx, void* edx);

void __fastcall AI_ArmyState_TickAttackDelays(AiArmyState* self, void* edx);
void __fastcall AI_Army_AttackEnemyCatapults(AiGroupOrders* self, void* edx, std::uint32_t player);
void __fastcall AI_RecruitGeologistsAndThieves(void* ecx, void* edx);
void __fastcall AI_Army_SendHunter(AiGroupOrders* self, void* edx, std::uint32_t player, std::uint32_t prey);
void __fastcall AI_Army_HuntEnemyThieves(AiGroupOrders* self, void* edx, std::uint32_t player);
void __fastcall AI_Army_HuntEnemyMages(AiGroupOrders* self, void* edx, std::uint32_t player);

using SendHunterFn = void(__fastcall*)(AiGroupOrders* self, void* edx, std::uint32_t player, std::uint32_t prey);
using StartAttackFn = void(__fastcall*)(std::uint32_t player, void* edx, float ratio);
void __fastcall AI_StartAttack(std::uint32_t player, void* edx, float ratio);

S3RE_HOOK(0x00594000, AI_ArmyState_TickAttackDelays, TickDelaysFn, Original_AI_ArmyState_TickAttackDelays,
          AI_ArmyState_TickAttackDelays);
S3RE_HOOK(0x00593180, AI_Army_AttackEnemyCatapults, CatapultsFn, Original_AI_Army_AttackEnemyCatapults,
          AI_Army_AttackEnemyCatapults);
S3RE_HOOK(0x005935b0, AI_Army_SendHunter, SendHunterFn, Original_AI_Army_SendHunter, AI_Army_SendHunter);
S3RE_HOOK(0x00593ad0, AI_Army_HuntEnemyThieves, CatapultsFn, Original_AI_Army_HuntEnemyThieves,
          AI_Army_HuntEnemyThieves);
S3RE_HOOK(0x00593bd0, AI_Army_HuntEnemyMages, CatapultsFn, Original_AI_Army_HuntEnemyMages, AI_Army_HuntEnemyMages);
S3RE_HOOK(0x00594080, AI_StartAttack, StartAttackFn, Original_AI_StartAttack, AI_StartAttack);
S3RE_HOOK(0x00592e60, AI_RecruitGeologistsAndThieves, RecruitFn, Original_AI_RecruitGeologistsAndThieves,
          AI_RecruitGeologistsAndThieves);

// Purpose: Counts down the computer players' attack delays; a group whose
// delay runs out gives up its attack (or hunt) and is reset.
void __fastcall AI_ArmyState_TickAttackDelays(AiArmyState* self, void* /*edx*/) {
    for (std::uint32_t player = 0; player < kPlayers; ++player) {
        for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
            std::uint32_t& delay = self->attack_delays[player][group];
            if (static_cast<std::int32_t>(delay) <= 0) {
                continue;
            }
            if (--delay == 0) {
                LogDebugString(texts::kAiAttackDelayReset, player, group);
                ResetUnitGroup(player, group);
            }
        }
    }
}

// Purpose: Sends a computer player's nearest unit group after each enemy
// catapult that stands in its land (and calls off the hunt for catapults
// that left it).
void __fastcall AI_Army_AttackEnemyCatapults(AiGroupOrders* self, void* /*edx*/, std::uint32_t player) {
    for (std::uint32_t other = 0; other < kPlayers; ++other) {
        const layout::Player& record = g_aiMapWorld.get()->players[other];
        if (record.active == 0 || other == player || record.team == g_aiMapWorld.get()->players[player].team) {
            continue;
        }
        if (record.active != kHumanPlayer) {
            using TeamFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t player);
            if ((reinterpret_cast<TeamFn>(kTeamHasActivePlayer)(self, nullptr, other) & 0xff) == 0) {
                continue;
            }
        }
        const AiVector& catapults = g_aiCatapultVectors.get()[other];
        for (std::uint32_t entry = catapults.begin; entry != catapults.end; entry += 4) {
            HandleEnemyCatapult(player, *reinterpret_cast<const std::uint32_t*>(entry));
        }
    }
}

// Purpose: Has the computer players turn carriers into geologists and
// thieves, one of each per 64 settlers, once they have residences.
void __fastcall AI_RecruitGeologistsAndThieves(void* /*ecx*/, void* /*edx*/) {
    AiAgentData& census = g_aiCensus.get();
    for (std::uint32_t player = 0; player < kPlayers; ++player) {
        if (g_aiMapWorld.get()->players[player].active != kComputerControlled || census.buildings_counted == 0) {
            continue;
        }
        const std::uint32_t* buildings = census.buildings[player];
        if (static_cast<std::int32_t>(buildings[Value(BuildingType::SmallResidence)]) <= 0 &&
            static_cast<std::int32_t>(buildings[Value(BuildingType::MediumResidence)]) <= 0 &&
            static_cast<std::int32_t>(buildings[Value(BuildingType::LargeResidence)]) <= 0) {
            continue;
        }
        Recruit(player, census.geologists[player], kRecruitGeologists);
        Recruit(player, census.thieves[player], kRecruitThieves);
    }
}

// Purpose: Sends a hunter (a free one, or a soldier taken from a unit
// group) after an enemy thief or mage in a computer player's land.
void __fastcall AI_Army_SendHunter(AiGroupOrders* /*self*/, void* /*edx*/, std::uint32_t player, std::uint32_t prey) {
    AiVector& hunters = g_aiHunterVectors.get()[player];
    std::int32_t nearest = kFarthest;
    std::uint32_t freeHunter = 0;
    HunterEntry* freeEntry = nullptr;
    for (std::uint32_t at = hunters.begin; at < hunters.end; at += sizeof(HunterEntry)) {
        auto* entry = reinterpret_cast<HunterEntry*>(at);
        const layout::Settler& target = SettlerAt(g_aiMapWorld, prey);
        if (entry->target == prey) {
            SendMoveSettler(player, entry->hunter, target.x, target.y);
            return;
        }
        if (entry->target != 0) {
            const layout::Settler& other = SettlerAt(g_aiMapWorld, entry->target);
            if (TileDistance(target.x, target.y, other.x, other.y) < kHuntersNear) {
                return;
            }
            continue;
        }
        const layout::Settler& hunter = SettlerAt(g_aiMapWorld, entry->hunter);
        const std::int32_t distance = TileDistance(target.x, target.y, hunter.x, hunter.y);
        if (distance < nearest) {
            freeEntry = entry;
            freeHunter = entry->hunter;
            nearest = distance;
        }
    }
    std::int32_t nearestGroup = -1;
    for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
        const std::uint32_t index = player * kUnitGroups + group;
        const CGameData* world = g_aiMapWorld;
        if (ListLength(world, index) < kGroupHunters) {
            continue;
        }
        const layout::Settler& first = FirstMember(world, index);
        const layout::Settler& target = SettlerAt(world, prey);
        const std::int32_t distance = TileDistance(target.x, target.y, first.x, first.y);
        if (distance < nearest) {
            nearest = distance;
            nearestGroup = static_cast<std::int32_t>(group);
            freeHunter = 0;
        }
    }
    if (static_cast<std::int32_t>(freeHunter) > 0) {
        freeEntry->target = prey;
        const layout::Settler& target = SettlerAt(g_aiMapWorld, prey);
        SendMoveSettler(player, freeHunter, target.x, target.y);
        return;
    }
    if (nearestGroup <= 0) {
        return;
    }
    CGameData* world = g_aiMapWorld;
    const std::uint32_t index = player * kUnitGroups + static_cast<std::uint32_t>(nearestGroup);
    std::int32_t& length = (&world->player_list_lengths[0][0])[index];
    const std::int32_t last = length - 1;
    std::uint16_t* members = &world->player_lists[0][0][0] + index * kGroupSlots;
    // Original bug: only the last member's type is tested (see the header).
    if (!IsHunterType(SettlerAt(world, members[last]).art)) {
        return;
    }
    const std::uint32_t hunter = members[last];
    --length;
    const layout::Settler& target = SettlerAt(g_aiMapWorld, prey);
    SendMoveSettler(player, hunter, target.x, target.y);
    AppendHunter(hunters, HunterEntry{hunter, prey});
}

// Purpose: Sends hunters after enemy thieves in a computer player's land.
void __fastcall AI_Army_HuntEnemyThieves(AiGroupOrders* self, void* /*edx*/, std::uint32_t player) {
    HuntEnemies(self, player, SettlerType::Thief);
}

// Purpose: Sends hunters after enemy mages in a computer player's land.
void __fastcall AI_Army_HuntEnemyMages(AiGroupOrders* self, void* /*edx*/, std::uint32_t player) {
    HuntEnemies(self, player, SettlerType::Mage);
}

// Purpose: The computer player's decision to attack: picks the weakest
// enemy within reach and, when strong enough, sends its spare unit groups
// against the enemy's weakest frontier building (gathering first), or else
// lets a group steal a weakly guarded enemy building.
void __fastcall AI_StartAttack(std::uint32_t player, void* /*edx*/, float ratio) {
    if (g_campaignMission == kFinalAssaultMission && g_gameTick >= kFinalAssaultTick) {
        using NoArgFn = std::uint32_t(__fastcall*)(void* ecx, void* edx);
        reinterpret_cast<NoArgFn>(kMission56FinalAssault)(nullptr, nullptr);
        return;
    }
    std::uint32_t& warmUp = g_aiArmyState.get().start_attack_calls;
    if (static_cast<std::int32_t>(warmUp) <= static_cast<std::int32_t>(kWarmUpCalls)) {
        ++warmUp;
        return;
    }
    if (static_cast<std::int32_t>(g_aiGroupOrders.get().gather_buildings[player]) > 0) {
        return;
    }
    const std::int32_t mySoldiers = CensusSoldiers(player);
    if (mySoldiers < kMinSoldiers) {
        return;
    }
    const std::int32_t enemy = ChooseEnemy(player);
    std::int32_t enemySoldiers = 0;
    bool enemyWithoutTowers = false;
    if (enemy > -1) {
        const auto e = static_cast<std::uint32_t>(enemy);
        enemySoldiers = CensusSoldiers(e);
        enemyWithoutTowers = CensusTowers(e) == 0;
    }
    // 0x594365..0x594392: a group already attacking: nothing new
    for (std::uint32_t group = kFirstGroup; group <= kLastGroup; ++group) {
        const std::uint32_t index = player * kUnitGroups + group;
        if ((&g_aiCensus.get().group_target_buildings[0][0])[index] != 0 &&
            (&g_aiCensus.get().group_defending[0][0])[index] == 0) {
            return;
        }
    }
    if (enemySoldiers <= 0 || enemy <= -1) {
        TrySteal(player, mySoldiers);
        return;
    }
    const auto e = static_cast<std::uint32_t>(enemy);
    // 0x5943a5..0x5943f8: x87 (fild, fmul float, fst float, fmul float, fcompp), in double
    const double strength = static_cast<double>(mySoldiers) * static_cast<double>(ratio);
    const auto strengthFloat = static_cast<float>(strength);
    const auto ownTeam = static_cast<std::int32_t>(g_aiCensus.get().team_strength[player]);
    const auto enemyTeam = static_cast<std::int32_t>(g_aiCensus.get().team_strength[e]);
    const std::int32_t mission = g_campaignMission;
    bool attack = !(static_cast<double>(enemySoldiers) >= strength * static_cast<double>(kAttackSoldierShare));
    attack = attack || !(static_cast<double>(enemyTeam) * static_cast<double>(kTeamStrengthMargin) >=
                         static_cast<double>(ownTeam) * static_cast<double>(ratio));
    attack = attack || mySoldiers > static_cast<std::int32_t>(static_cast<std::uint32_t>(enemySoldiers) * kOutnumbering);
    if (!attack) {
        if (mission == kMission70) {
            attack = player == kMission70Player && mySoldiers >= enemySoldiers;
        } else if (mission == kMission72) {
            attack = player == kMission72Player;
        }
    }
    if (attack) {
        LaunchAttack(player, e, strengthFloat, enemyWithoutTowers);
    } else {
        TrySteal(player, mySoldiers);
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// One random army and world per case (g_aiMapWorld = g_aiWorld = the test
// world); the original runs, its results are kept, the start is restored
// and ours runs. The command senders are recording stubs and
// OutputDebugStringA's import a recorder; ResetUnitGroup, TeamHasActivePlayer,
// TileDistance and CGameData::Random run (verified).

namespace {

constexpr std::uint32_t kTestSettlers = 400;
constexpr std::uint32_t kTestMap = 120;
constexpr std::uint32_t kTestAreas = 8;
constexpr std::uint32_t kCatapultsPerPlayer = 5;

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

std::uint32_t g_testCatapults[kPlayers][kCatapultsPerPlayer];

struct AttackState {
    std::vector<std::uint8_t> lists, orders, armyState, census, sender, catapults;
    std::uint16_t random[3];

    static AttackState Capture(CGameData* world) {
        AttackState s{Bytes(reinterpret_cast<std::uint8_t*>(world) + kListsOffset, kListsBytes),  // raw-ok: test snapshot
                      Bytes(&g_aiGroupOrders.get(), sizeof(AiGroupOrders)),
                      Bytes(&g_aiArmyState.get(), sizeof(AiArmyState)),
                      Bytes(&g_aiCensus.get(), sizeof(AiAgentData)),
                      Bytes(&g_aiCommandSender.get(), sizeof(AiCommandSender)),
                      Bytes(&g_aiCatapultVectors.get(), sizeof(AiVector) * kPlayers),
                      {}};
        std::memcpy(s.random, world->random_state, sizeof(s.random));
        return s;
    }
    void Restore(CGameData* world) const {
        std::memcpy(reinterpret_cast<std::uint8_t*>(world) + kListsOffset, lists.data(), kListsBytes);  // raw-ok
        std::memcpy(&g_aiGroupOrders.get(), orders.data(), orders.size());
        std::memcpy(&g_aiArmyState.get(), armyState.data(), armyState.size());
        std::memcpy(&g_aiCensus.get(), census.data(), census.size());
        std::memcpy(&g_aiCommandSender.get(), sender.data(), sender.size());
        std::memcpy(&g_aiCatapultVectors.get(), catapults.data(), catapults.size());
        std::memcpy(world->random_state, random, sizeof(random));
    }
    bool operator==(const AttackState& o) const {
        return lists == o.lists && orders == o.orders && armyState == o.armyState && census == o.census &&
               sender == o.sender && catapults == o.catapults && std::memcmp(random, o.random, sizeof(random)) == 0;
    }
};

struct AttackFunctions {
    TickDelaysFn tickDelays;
    CatapultsFn catapults;
    RecruitFn recruit;
    SendHunterFn sendHunter;
    CatapultsFn huntThieves, huntMages;
    StartAttackFn startAttack;
};

AttackFunctions OriginalAttackFunctions() {
    return {Original_AI_ArmyState_TickAttackDelays, Original_AI_Army_AttackEnemyCatapults,
            Original_AI_RecruitGeologistsAndThieves, Original_AI_Army_SendHunter,
            Original_AI_Army_HuntEnemyThieves,      Original_AI_Army_HuntEnemyMages,
            Original_AI_StartAttack};
}

AttackFunctions OurAttackFunctions() {
    return {&AI_ArmyState_TickAttackDelays, &AI_Army_AttackEnemyCatapults, &AI_RecruitGeologistsAndThieves,
            &AI_Army_SendHunter,            &AI_Army_HuntEnemyThieves,     &AI_Army_HuntEnemyMages,
            &AI_StartAttack};
}

void SetUpAttack(test::Rng& rng, CGameData* world) {
    rng.Fill(world->random_state, sizeof(world->random_state));
    for (std::uint32_t x = 0; x < kTestMap; ++x) {
        for (std::uint32_t y = 0; y < kTestMap; ++y) {
            world->tiles[x][y].area = static_cast<std::uint16_t>(rng.Below(kTestAreas));
        }
    }
    world->area_high_water = rng.Chance(1, 20) ? 0xffffffffu : rng.Below(kTestAreas);
    for (std::uint32_t area = 0; area < kTestAreas; ++area) {
        layout::Area& record = world->areas[area];
        record.player = static_cast<std::uint8_t>(rng.Chance(1, 6) ? 0xff : rng.Below(3));
        record.settlers_by_type[0] = static_cast<std::uint16_t>(rng.Chance(1, 4) ? 0 : 1 + rng.Below(kTestSettlers - 1));
    }
    for (std::uint32_t id = 1; id < kTestSettlers; ++id) {
        layout::Settler& settler = world->settlers[id];
        settler.x = static_cast<std::uint16_t>(rng.Below(kTestMap));
        settler.y = static_cast<std::uint16_t>(rng.Below(kTestMap));
        settler.art = static_cast<std::uint8_t>(rng.Chance(2, 3) ? 5 + rng.Below(31) : rng.Below(48));
        settler.next = static_cast<std::uint16_t>(rng.Chance(1, 4) || id + 10 >= kTestSettlers ? 0
                                                                                          : id + 1 + rng.Below(10));
    }
    AiGroupOrders& orders = g_aiGroupOrders.get();
    rng.Fill(&orders, sizeof(orders));
    rng.Fill(&g_aiArmyState.get(), sizeof(AiArmyState));
    rng.Fill(&g_aiCensus.get(), sizeof(AiAgentData));
    AiAgentData& census = g_aiCensus.get();
    census.buildings_counted = static_cast<std::uint8_t>(rng.Chance(1, 5) ? 0 : 1 + rng.Below(255));
    AiVector* catapults = g_aiCatapultVectors;
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        world->players[p].active = static_cast<std::int32_t>(rng.Below(3));
        world->players[p].team = static_cast<std::int32_t>(rng.Below(3));
        for (SettlerType type : {SettlerType::Thief, SettlerType::Mage}) {
            world->players[p].settler_lists[Value(type)] =
                static_cast<std::uint16_t>(rng.Chance(1, 4) ? 0 : 1 + rng.Below(kTestSettlers - 1));
        }
        for (std::uint32_t k = 0; k < kCatapultsPerPlayer; ++k) {
            g_testCatapults[p][k] = 1 + rng.Below(kTestSettlers - 1);
        }
        catapults[p].begin = reinterpret_cast<std::uint32_t>(&g_testCatapults[p][0]);
        catapults[p].end = catapults[p].begin + 4 * (rng.Chance(1, 3) ? 0 : rng.Below(kCatapultsPerPlayer + 1));
        for (BuildingType type : {BuildingType::SmallResidence, BuildingType::MediumResidence, BuildingType::LargeResidence}) {
            census.buildings[p][Value(type)] = rng.Chance(2, 3) ? 0 : rng.Chance(1, 4) ? rng.Next32() : rng.Below(3);
        }
        census.other_settlers[p] = rng.Chance(1, 8) ? static_cast<std::int32_t>(rng.Next32())
                                                    : static_cast<std::int32_t>(rng.Below(400)) - 40;
        census.geologists[p] = static_cast<std::int32_t>(rng.Below(10)) - 2;
        census.thieves[p] = static_cast<std::int32_t>(rng.Below(10)) - 2;
        for (std::uint32_t g = 0; g < kUnitGroups; ++g) {
            world->player_list_lengths[p][g] = rng.Chance(1, 4) ? static_cast<std::int32_t>(rng.Below(3)) - 1
                                                                : static_cast<std::int32_t>(rng.Below(40));
            for (std::uint32_t k = 0; k < kGroupSlots; ++k) {
                world->player_lists[p][g][k] = static_cast<std::uint16_t>(1 + rng.Below(kTestSettlers - 1));
            }
            census.group_defending[p][g] = static_cast<std::uint8_t>(rng.Chance(2, 3) ? 0 : 1);
            orders.group_targets[p][g].prey = static_cast<std::uint16_t>(
                rng.Chance(1, 2) ? g_testCatapults[rng.Below(3)][rng.Below(kCatapultsPerPlayer)] : rng.Below(kTestSettlers));
            g_aiArmyState.get().attack_delays[p][g] =
                rng.Chance(1, 3) ? rng.Below(3) : rng.Chance(1, 2) ? rng.Next32() : rng.Below(100);
        }
    }
}

// The tested player's hunter vector is built fresh before each run in a
// block of the game's operator new (SendHunter may free it and allocate a
// new one); after the run its contents are kept and the block freed.
struct HunterVectorStart {
    std::vector<HunterEntry> entries;
    std::uint32_t capacity = 0;  // entries
    bool none = false;           // begin = end = capacity = null
};

void* GameNew(std::uint32_t bytes) {
    using NewFn = void*(__cdecl*)(std::uint32_t bytes);
    return reinterpret_cast<NewFn>(kOperatorNew)(bytes);
}

void GameDelete(void* block) {
    using DeleteFn = void(__cdecl*)(void* block);
    reinterpret_cast<DeleteFn>(kOperatorDelete)(block);
}

HunterVectorStart RandomHunters(test::Rng& rng) {
    HunterVectorStart start;
    if (rng.Chance(1, 6)) {
        start.none = true;
        return start;
    }
    const std::uint32_t count = rng.Below(6);
    for (std::uint32_t k = 0; k < count; ++k) {
        start.entries.push_back(HunterEntry{1 + rng.Below(kTestSettlers - 1),
                                            rng.Chance(1, 2) ? 0 : 1 + rng.Below(kTestSettlers - 1)});
    }
    start.capacity = count + (rng.Chance(1, 2) ? 0 : rng.Below(3));
    return start;
}

void BuildHunters(std::uint32_t player, const HunterVectorStart& start) {
    AiVector& vector = g_aiHunterVectors.get()[player];
    if (start.none) {
        vector.begin = vector.end = vector.unk8 = 0;
        return;
    }
    auto* block = static_cast<HunterEntry*>(GameNew((start.capacity + 1) * sizeof(HunterEntry)));
    std::memcpy(block, start.entries.data(), start.entries.size() * sizeof(HunterEntry));
    vector.begin = reinterpret_cast<std::uint32_t>(block);
    vector.end = vector.begin + static_cast<std::uint32_t>(start.entries.size() * sizeof(HunterEntry));
    vector.unk8 = vector.begin + start.capacity * sizeof(HunterEntry);
}

struct HunterVectorEnd {
    std::vector<std::uint8_t> entries;
    std::uint32_t capacity = 0;  // bytes from begin
    bool operator==(const HunterVectorEnd& o) const { return entries == o.entries && capacity == o.capacity; }
};

HunterVectorEnd TakeHunters(std::uint32_t player) {
    AiVector& vector = g_aiHunterVectors.get()[player];
    HunterVectorEnd end;
    if (vector.begin != 0) {
        end.entries = Bytes(reinterpret_cast<const void*>(vector.begin), vector.end - vector.begin);
        end.capacity = vector.unk8 - vector.begin;
        GameDelete(reinterpret_cast<void*>(vector.begin));
    }
    vector.begin = vector.end = vector.unk8 = 0;
    return end;
}

constexpr std::uint32_t kTestBuildings = 30;

// StartAttack's state: census soldiers, buildings and team strengths,
// start positions (walk regions), world buildings, group members' tiles.
void SetUpStartAttack(test::Rng& rng, CGameData* world) {
    AiAgentData& census = g_aiCensus.get();
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        for (std::uint32_t& soldiers : census.soldiers[p]) {
            soldiers = rng.Chance(1, 3) ? 0 : rng.Chance(1, 10) ? rng.Next32() : rng.Below(6);
        }
        for (BuildingType type : {BuildingType::SmallTower, BuildingType::LargeTower, BuildingType::Castle,
                                  BuildingType::Barracks}) {
            census.buildings[p][Value(type)] = rng.Chance(1, 2) ? 0 : rng.Chance(1, 10) ? rng.Next32() : rng.Below(3);
        }
        census.team_strength[p] = rng.Chance(1, 10) ? rng.Next32() : rng.Below(60);
        world->players[p].start_x = static_cast<std::int32_t>(rng.Below(kTestMap));
        world->players[p].start_y = static_cast<std::int32_t>(rng.Below(kTestMap));
        g_aiGroupOrders.get().gather_buildings[p] = rng.Chance(2, 3) ? 0 : rng.Chance(1, 2) ? rng.Next32() : rng.Below(5);
        g_aiGroupOrders.get().group_size[p] = static_cast<std::int32_t>(rng.Below(20));
        for (std::uint32_t g = 0; g < kUnitGroups; ++g) {
            census.group_target_buildings[p][g] = static_cast<std::uint16_t>(rng.Chance(4, 5) ? 0 : 1 + rng.Below(5));
            g_aiGroupOrders.get().home_areas[p][g] = static_cast<std::uint16_t>(rng.Below(3));
            if (rng.Chance(1, 2)) {
                g_aiArmyState.get().attack_delays[p][g] = 0;
            }
        }
    }
    for (std::uint32_t x = 0; x < kTestMap; ++x) {
        for (std::uint32_t y = 0; y < kTestMap; ++y) {
            world->tiles[x][y].walk_region = static_cast<std::uint8_t>(rng.Chance(7, 8) ? 1 : rng.Below(3));
        }
    }
    world->building_high_water = rng.Chance(1, 20) ? 0xffffffffu : rng.Below(kTestBuildings);
    constexpr std::uint8_t kTypes[] = {15, 16, 18, 19, 19, 2, 17};
    for (std::uint32_t id = 1; id < kTestBuildings; ++id) {
        layout::Building& building = world->buildings[id];
        building.owner = static_cast<std::uint8_t>(rng.Chance(1, 8) ? 0xff : rng.Below(4));
        building.art = kTypes[rng.Below(sizeof(kTypes))];
        building.buildup = static_cast<std::uint8_t>(rng.Chance(1, 5) ? 1 + rng.Below(50) : 0);
        building.x = static_cast<std::uint16_t>(rng.Below(kTestMap));
        building.y = static_cast<std::uint16_t>(rng.Below(kTestMap));
        building.door_x = static_cast<std::uint16_t>(rng.Below(kTestMap));
        building.door_y = static_cast<std::uint16_t>(rng.Below(kTestMap));
        building.garrison = 0;
    }
}

float RandomRatio(test::Rng& rng) {
    switch (rng.Below(5)) {
    case 0: return 1.0f;
    case 1: return static_cast<float>(rng.Below(300)) / 100.0f;
    case 2: return static_cast<float>(rng.Below(400)) * 0.00390625f;
    case 3: {
        const std::uint32_t bits = rng.Next32();
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    default: return 1.0f / static_cast<float>(1 + rng.Below(9));
    }
}

void CompareAttackFunctions(test::Context& ctx, const AttackFunctions& candidate, int cases) {
    const AttackFunctions original = OriginalAttackFunctions();
    test::WorldBuffer buffer(sizeof(CGameData));
    CGameData* world = buffer.world();
    const AttackState saved = AttackState::Capture(world);
    const std::vector<std::uint8_t> savedHunters = Bytes(&g_aiHunterVectors.get(), sizeof(AiVector) * kPlayers);
    CGameData* savedMapWorld = g_aiMapWorld;
    CGameData* savedWorld = g_aiWorld;
    const std::int32_t savedMission = g_campaignMission;
    const std::uint32_t savedTick = g_gameTick;
    g_aiMapWorld = world;
    g_aiWorld = world;

    std::vector<std::uint32_t> enemyCounts;
    std::size_t nextCount = 0;
    test::CalleeStubs stubs;
    stubs.SetWorld(nullptr);
    stubs.Install<4, kSendMoveGroup>([](CGameData*, std::uint32_t, const std::uint32_t* args) {
        return args[1] * 3 + args[2];
    });
    stubs.Install<5, kSendConvertCarriers>([](CGameData*, std::uint32_t, const std::uint32_t*) { return 0x5160u; });
    stubs.Install<4, kSendMoveSettler>([](CGameData*, std::uint32_t, const std::uint32_t*) { return 0x5170u; });
    stubs.Install<4, kCountEnemySoldiers>([&](CGameData*, std::uint32_t, const std::uint32_t*) {
        return enemyCounts[nextCount++ % enemyCounts.size()];
    });
    const auto recorder = reinterpret_cast<std::uint32_t>(&RecordString);
    test::PatchBytes(reinterpret_cast<std::uintptr_t>(&g_importOutputDebugStringA.get()), &recorder, sizeof(recorder));

    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        test::Rng& rng = ctx.rng();
        if (i % 3 == 0) {
            SetUpAttack(rng, world);
        }
        const int op = static_cast<int>(rng.Below(7));
        if (op == 6) {
            SetUpStartAttack(rng, world);
            g_campaignMission = rng.Chance(1, 2) ? 0 : static_cast<std::int32_t>(rng.Chance(1, 2) ? 56 : 70 + 2 * rng.Below(2));
            g_gameTick = rng.Chance(1, 2) ? 80999 + rng.Below(3) : rng.Below(100000);
            g_aiArmyState.get().start_attack_calls = rng.Chance(1, 10) ? 29 + rng.Below(4) : 31 + rng.Below(3);
        }
        const float ratio = RandomRatio(rng);
        enemyCounts.clear();
        for (int k = 0; k < 16; ++k) {
            enemyCounts.push_back(rng.Chance(1, 2) ? 0 : rng.Below(rng.Chance(1, 4) ? 60 : 6));
        }
        const std::uint32_t player = rng.Below(3);
        const std::uint32_t prey = 1 + rng.Below(kTestSettlers - 1);
        const HunterVectorStart hunters = RandomHunters(rng);
        if (op == 3 && rng.Chance(1, 2)) {  // a hunter already after the prey, or near it
            if (!hunters.none && !hunters.entries.empty()) {
                const_cast<HunterVectorStart&>(hunters).entries[rng.Below(
                    static_cast<std::uint32_t>(hunters.entries.size()))].target = prey;
            }
        }
        const AttackState start = AttackState::Capture(world);
        struct Outcome {
            AttackState state;
            HunterVectorEnd hunters;
            std::vector<test::StubCall> calls;
            std::vector<std::string> strings;
        };
        auto run = [&](const AttackFunctions& f) {
            start.Restore(world);
            nextCount = 0;
            BuildHunters(player, hunters);
            Outcome out{};
            g_strings = &out.strings;
            switch (op) {
            case 0: f.tickDelays(&g_aiArmyState.get(), nullptr); break;
            case 1: f.catapults(&g_aiGroupOrders.get(), nullptr, player); break;
            case 2: f.recruit(nullptr, nullptr); break;
            case 3: f.sendHunter(&g_aiGroupOrders.get(), nullptr, player, prey); break;
            case 4: f.huntThieves(&g_aiGroupOrders.get(), nullptr, player); break;
            case 5: f.huntMages(&g_aiGroupOrders.get(), nullptr, player); break;
            default: f.startAttack(player, nullptr, ratio); break;
            }
            g_strings = nullptr;
            out.state = AttackState::Capture(world);
            out.hunters = TakeHunters(player);
            out.calls = stubs.Take();
            return out;
        };
        const Outcome a = run(original);
        const Outcome b = run(candidate);
        bool same = S3RE_EXPECT(a.state.lists == b.state.lists);
        same &= S3RE_EXPECT(a.state.orders == b.state.orders);
        same &= S3RE_EXPECT(a.state.armyState == b.state.armyState);
        same &= S3RE_EXPECT(a.state.census == b.state.census);
        same &= S3RE_EXPECT(a.state == b.state);
        same &= S3RE_EXPECT(a.hunters == b.hunters);
        same &= S3RE_EXPECT(a.calls == b.calls);
        same &= S3RE_EXPECT(a.strings == b.strings);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d, player %u, prey %u", i, op, player, prey);
        }
    }
    test::UnpatchBytes(reinterpret_cast<std::uintptr_t>(&g_importOutputDebugStringA.get()));
    saved.Restore(world);
    std::memcpy(&g_aiHunterVectors.get(), savedHunters.data(), savedHunters.size());
    g_campaignMission = savedMission;
    g_gameTick = savedTick;
    g_aiMapWorld = savedMapWorld;
    g_aiWorld = savedWorld;
}

void __fastcall WrongTickDelays(AiArmyState* self, void* edx) {
    // Wrong: negative delays count down too.
    for (auto& row : self->attack_delays) {
        for (std::uint32_t& delay : row) {
            if (static_cast<std::int32_t>(delay) < 0) {
                --delay;
            }
        }
    }
    AI_ArmyState_TickAttackDelays(self, edx);
}

void __fastcall WrongCatapults(AiGroupOrders* self, void* edx, std::uint32_t player) {
    // Wrong: allies' catapults are hunted too.
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        g_aiMapWorld.get()->players[p].team += static_cast<std::int32_t>(p) * 7;
    }
    AI_Army_AttackEnemyCatapults(self, edx, player);
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        g_aiMapWorld.get()->players[p].team -= static_cast<std::int32_t>(p) * 7;
    }
}

void __fastcall WrongRecruit(void* ecx, void* edx) {
    // Wrong: thieves first.
    AiAgentData& census = g_aiCensus.get();
    std::int32_t geologists[kPlayers];
    std::memcpy(geologists, census.geologists, sizeof(geologists));
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        census.geologists[p] = 0x7fffffff;
    }
    AI_RecruitGeologistsAndThieves(ecx, edx);
    std::memcpy(census.geologists, geologists, sizeof(geologists));
    std::int32_t thieves[kPlayers];
    std::memcpy(thieves, census.thieves, sizeof(thieves));
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        census.thieves[p] = 0x7fffffff;
    }
    AI_RecruitGeologistsAndThieves(ecx, edx);
    std::memcpy(census.thieves, thieves, sizeof(thieves));
}

void __fastcall WrongSendHunter(AiGroupOrders* self, void* edx, std::uint32_t player, std::uint32_t prey) {
    // Wrong: a group's last member of any type becomes the hunter.
    CGameData* world = g_aiMapWorld;
    std::vector<std::pair<layout::Settler*, std::uint8_t>> changed;
    for (std::uint32_t g = kFirstGroup; g <= kLastGroup; ++g) {
        const std::uint32_t index = player * kUnitGroups + g;
        const std::int32_t length = ListLength(world, index);
        if (length >= 1 && length <= static_cast<std::int32_t>(kGroupSlots)) {
            const std::uint16_t member = (&world->player_lists[0][0][0])[index * kGroupSlots + length - 1];
            layout::Settler* last = &(&world->settlers[0])[member];
            changed.emplace_back(last, last->art);
            last->art = Value(SettlerType::Swordsman1);
        }
    }
    AI_Army_SendHunter(self, edx, player, prey);
    for (auto it = changed.rbegin(); it != changed.rend(); ++it) {
        it->first->art = it->second;
    }
}

void __fastcall WrongHuntThieves(AiGroupOrders* self, void* edx, std::uint32_t player) {
    // Wrong: hunts the thieves of its own team too.
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        g_aiMapWorld.get()->players[p].team += static_cast<std::int32_t>(p) * 7;
    }
    AI_Army_HuntEnemyThieves(self, edx, player);
    for (std::uint32_t p = 0; p < kPlayers; ++p) {
        g_aiMapWorld.get()->players[p].team -= static_cast<std::int32_t>(p) * 7;
    }
}

void __fastcall WrongStartAttack(std::uint32_t player, void* edx, float ratio) {
    // Wrong: the defenders check uses the ratio once more.
    AI_StartAttack(player, edx, ratio == ratio && ratio > 0.5f ? ratio * 1.25f : ratio);
}

}  // namespace

S3RE_TEST(AiArmyAttack_MatchOriginal) {
    CompareAttackFunctions(ctx, OurAttackFunctions(), 9000);
}

S3RE_TEST(AiArmyAttack_ComparisonDetectsWrongImplementation) {
    int index = 0;
    const auto probe = [&](auto change) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        AttackFunctions wrong = OurAttackFunctions();
        change(wrong);
        CompareAttackFunctions(quiet, wrong, 1500);
        if (quiet.failures() == 0) {
            ctx.Fail(__FILE__, __LINE__, "  wrong implementation %d not detected", index);
        }
        ++index;
    };
    probe([](AttackFunctions& f) { f.tickDelays = &WrongTickDelays; });
    probe([](AttackFunctions& f) { f.catapults = &WrongCatapults; });
    probe([](AttackFunctions& f) { f.recruit = &WrongRecruit; });
    probe([](AttackFunctions& f) { f.sendHunter = &WrongSendHunter; });
    probe([](AttackFunctions& f) { f.huntThieves = &WrongHuntThieves; });
    probe([](AttackFunctions& f) { f.startAttack = &WrongStartAttack; });
}

}  // namespace s3re::game
