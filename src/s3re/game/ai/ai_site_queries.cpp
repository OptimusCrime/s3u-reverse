// Computer-player helpers around building sites, military buildings and
// the CompMapWorkerAgent globals (computer players A2, batch 3;
// re/computer_players.md).
//
//   0x00571d00 AI_HasSiteNoteInArea(type, area) fastcall (const u16*,
//              const u16*): scans the current computer player's 300 notes
//              (g_aiPlayer): 1 if a type-4 note (probably
//              T_DO_BUILD_HOUSE_NOTE) is for that building type, or a
//              type-2 note (probably HOUSE_GROUND_REQUEST_NOTE) for another
//              type, and its position (+0x1e / +0x20) lies in that area
//              (AI_AreaAlongRow); else 0.
//   0x00571d90 AI_HasBuildingNear(type, x, y, distance, player) fastcall
//              (CX, DX, three stack dwords, ret 0xc): 1 if one of the
//              player's buildings of that type (any buildup) is at most
//              `distance` steps (TileDistance) from (x, y); else 0.
//   0x00571e30 AI_CountBuildingsNear(type, x, y, distance, player): the
//              same, counting (type 0xffff: any type). AX = the count.
//   0x00571ee0 AI_NextGarrisonedMilitaryBuilding(start, player) fastcall
//              (CX, DL): round robin from start + 1 over the building ids
//              (AI_BuildingIfUsed_58e1c0) for the player's small tower,
//              large tower or castle with a garrison (+8 != 0). AX = its
//              id, or 0xffff after a full round; EAX's upper half is that
//              of the last lookup's result (reproduced).
//   0x005720c0 AI_CheckBasicEconomy(player, data) thiscall-like (ECX =
//              const u16* player, the node data on the stack, ret 4):
//              AiPlayerData::basic_economy = 1 if the node has a sawmill, a
//              woodcutter and a small tower and the race's basics (Roman
//              and Amazon: stonecutter and forester; Egyptian: stonecutter;
//              Asian: forester), else 0 (also for a race above 3). EAX = it.
//   0x005930f0 AI_ResetUnitGroup(player, group) stdcall: clears the
//              group's entries of g_aiArmyState (group_values, group_words,
//              group_bytes) and g_aiGroupOrders (target, flag); index
//              player * 10 + group, not range-checked.
//   0x00593f50 AI_ArmyState_Reset() thiscall (on g_aiArmyState): the
//              per-player arrays (25 * player, zeros, 2000000), group
//              values 1..9 = 10 + 2 * group, the scalars (300, 500,
//              2000000) and g_aiPerPlayer7e79d0 = 0. EAX = this.

#include "game/ai/ai_layout.h"
#include "game/enums.h"
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
using layout::kPlayers;

namespace {

// Callees, called through their S3.EXE addresses.
constexpr std::uintptr_t kAreaAlongRow = 0x005996c0;     // AI_AreaAlongRow
constexpr std::uintptr_t kGetBuilding = 0x0058e170;      // AI_GetBuilding
constexpr std::uintptr_t kBuildingIfUsed = 0x0058e1c0;   // AI_BuildingIfUsed_58e1c0
constexpr std::uintptr_t kTileDistance = 0x004ff320;     // TileDistance (ECX = world, unused)

constexpr std::uint16_t kNone = 0xffff;
constexpr std::uint16_t kAnyType = 0xffff;

// Note types of 0x571d00 (BlackBoardNote::type).
constexpr std::uint16_t kGroundRequestNote = 2;  // probably HOUSE_GROUND_REQUEST_NOTE
constexpr std::uint16_t kBuildHouseNote = 4;     // probably T_DO_BUILD_HOUSE_NOTE

// The fields of those notes 0x571d00 reads.
struct SiteNote {
    BlackBoardNote base;
    std::uint16_t unk1a;
    std::uint16_t building_type;  // 0x1c
    std::uint16_t x;              // 0x1e
    std::uint16_t y;              // 0x20
};
static_assert(offsetof(SiteNote, building_type) == 0x1c);
static_assert(offsetof(SiteNote, y) == 0x20);

// AI_ArmyState_Reset's values.
constexpr std::uint32_t kPlayerStep000 = 25;
constexpr std::uint32_t kFar = 2000000;
constexpr std::uint32_t kUnk504 = 300;
constexpr std::uint32_t kUnk50c = 500;

std::uint32_t GetBuilding(std::uint32_t id, std::uint32_t* error) {
    using Fn = std::uint32_t(__fastcall*)(std::uint32_t id, std::uint32_t* error);
    return reinterpret_cast<Fn>(kGetBuilding)(id, error);
}

std::uint32_t Distance(std::uint32_t x1, std::uint32_t y1, std::uint32_t x2, std::uint32_t y2) {
    using Fn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t, std::uint32_t, std::uint32_t,
                                          std::uint32_t);
    return reinterpret_cast<Fn>(kTileDistance)(g_aiWorld, nullptr, x1, y1, x2, y2);
}

bool IsMilitary(std::uint8_t type) {
    return type == Value(BuildingType::SmallTower) || type == Value(BuildingType::LargeTower) ||
           type == Value(BuildingType::Castle);
}

}  // namespace

using HasNoteFn = std::uint32_t(__fastcall*)(const std::uint16_t* type, const std::uint16_t* area);
using NearFn = std::uint32_t(__fastcall*)(std::uint32_t type, std::uint32_t x, std::uint32_t y,
                                          std::uint32_t distance, std::uint32_t player);
using NextMilitaryFn = std::uint32_t(__fastcall*)(std::uint32_t start, std::uint32_t player);
using BasicEconomyFn = std::uint32_t(__fastcall*)(const std::uint16_t* player, void* edx, AiPlayerData* data);
using ResetGroupFn = std::uint32_t(__stdcall*)(std::uint32_t player, std::uint32_t group);
using ArmyResetFn = std::uint32_t(__fastcall*)(AiArmyState* self, void* edx);

std::uint32_t __fastcall AI_HasSiteNoteInArea(const std::uint16_t* type, const std::uint16_t* area);
std::uint32_t __fastcall AI_HasBuildingNear(std::uint32_t type, std::uint32_t x, std::uint32_t y,
                                            std::uint32_t distance, std::uint32_t player);
std::uint32_t __fastcall AI_CountBuildingsNear(std::uint32_t type, std::uint32_t x, std::uint32_t y,
                                               std::uint32_t distance, std::uint32_t player);
std::uint32_t __fastcall AI_NextGarrisonedMilitaryBuilding(std::uint32_t start, std::uint32_t player);
std::uint32_t __fastcall AI_CheckBasicEconomy(const std::uint16_t* player, void* edx, AiPlayerData* data);
std::uint32_t __stdcall AI_ResetUnitGroup(std::uint32_t player, std::uint32_t group);
std::uint32_t __fastcall AI_ArmyState_Reset(AiArmyState* self, void* edx);

S3RE_HOOK(0x00571d00, AI_HasSiteNoteInArea, HasNoteFn, Original_AI_HasSiteNoteInArea, AI_HasSiteNoteInArea);
S3RE_HOOK(0x00571d90, AI_HasBuildingNear, NearFn, Original_AI_HasBuildingNear, AI_HasBuildingNear);
S3RE_HOOK(0x00571e30, AI_CountBuildingsNear, NearFn, Original_AI_CountBuildingsNear, AI_CountBuildingsNear);
S3RE_HOOK(0x00571ee0, AI_NextGarrisonedMilitaryBuilding, NextMilitaryFn, Original_AI_NextGarrisonedMilitaryBuilding,
          AI_NextGarrisonedMilitaryBuilding);
S3RE_HOOK(0x005720c0, AI_CheckBasicEconomy, BasicEconomyFn, Original_AI_CheckBasicEconomy, AI_CheckBasicEconomy);
S3RE_HOOK(0x005930f0, AI_ResetUnitGroup, ResetGroupFn, Original_AI_ResetUnitGroup, AI_ResetUnitGroup);
S3RE_HOOK(0x00593f50, AI_ArmyState_Reset, ArmyResetFn, Original_AI_ArmyState_Reset, AI_ArmyState_Reset);

// Purpose: Whether the computer player already has a building order for
// this type (or a ground request for another type) in an economy area,
// probably so that it doesn't plan the same site twice.
std::uint32_t __fastcall AI_HasSiteNoteInArea(const std::uint16_t* type, const std::uint16_t* area) {
    using AreaFn = std::uint32_t(__fastcall*)(std::uint32_t x, std::uint32_t y);
    for (std::size_t id = 0; id < kMaxNotes; ++id) {
        const auto* note = reinterpret_cast<const SiteNote*>(g_aiPlayer.get()->notes[id]);
        if (note == nullptr) {
            continue;
        }
        const bool sameType = note->building_type == *type;
        const bool candidate = (note->base.type == kBuildHouseNote && sameType) ||
                               (note->base.type == kGroundRequestNote && !sameType);
        if (candidate &&
            static_cast<std::uint16_t>(reinterpret_cast<AreaFn>(kAreaAlongRow)(note->x, note->y)) == *area) {
            return 1;
        }
    }
    return 0;
}

// Purpose: Whether the player has a building of a type within a distance
// of a position (e.g. a tower near a planned site).
std::uint32_t __fastcall AI_HasBuildingNear(std::uint32_t type, std::uint32_t x, std::uint32_t y,
                                            std::uint32_t distance, std::uint32_t player) {
    std::uint32_t error = 0;
    std::uint32_t id = 0;
    for (;;) {
        ++id;
        const auto* building = reinterpret_cast<const layout::Building*>(GetBuilding(id, &error));
        if (building != nullptr && building->owner == static_cast<std::uint8_t>(player) &&
            static_cast<std::uint16_t>(type) == building->art &&
            Distance(x & 0xffff, y & 0xffff, building->x, building->y) <= (distance & 0xffff)) {
            return 1;
        }
        if (error != 0) {
            return 0;
        }
    }
}

// Purpose: How many of the player's buildings of a type (0xffff: of any
// type) lie within a distance of a position.
std::uint32_t __fastcall AI_CountBuildingsNear(std::uint32_t type, std::uint32_t x, std::uint32_t y,
                                               std::uint32_t distance, std::uint32_t player) {
    std::uint32_t error = 0;
    std::uint32_t count = 0;
    std::uint32_t id = 0;
    do {
        ++id;
        const auto* building = reinterpret_cast<const layout::Building*>(GetBuilding(id, &error));
        if (building != nullptr && building->owner == static_cast<std::uint8_t>(player)) {
            const auto wanted = static_cast<std::uint16_t>(type);
            if ((wanted == building->art || wanted == kAnyType) &&
                Distance(x & 0xffff, y & 0xffff, building->x, building->y) <= (distance & 0xffff)) {
                ++count;
            }
        }
    } while (error == 0);
    return (error & 0xffff0000u) | (count & 0xffff);
}

// Purpose: The player's next military building with soldiers inside,
// going round the building ids (probably to pick the next one to take
// soldiers from or to check).
std::uint32_t __fastcall AI_NextGarrisonedMilitaryBuilding(std::uint32_t startArg, std::uint32_t playerArg) {
    using LookupFn = std::uint32_t(__fastcall*)(std::uint32_t id);
    const auto player = static_cast<std::uint8_t>(playerArg);
    const auto last = static_cast<std::uint16_t>(g_aiWorld.get()->building_high_water);
    // 0x571ef4..0x571f06: the id after `start`, or 0.
    auto start = static_cast<std::uint16_t>(startArg);
    if (start > last || start == kNone) {
        start = 0;
    } else {
        ++start;
        if (start >= last) {
            start = 0;
        }
    }
    std::uint16_t id = start;
    std::uint32_t result = 0;
    for (;;) {
        result = reinterpret_cast<LookupFn>(kBuildingIfUsed)(id);
        const auto* building = reinterpret_cast<const layout::Building*>(result);
        if (building != nullptr && building->owner == player && building->garrison != 0 &&
            IsMilitary(building->art)) {
            return (result & 0xffff0000u) | id;  // the upper half is the record pointer's
        }
        ++id;
        if (static_cast<std::int32_t>(id) >= static_cast<std::int32_t>(last) + 1) {
            id = 0;
        }
        if (id == start) {
            return (result & 0xffff0000u) | kNone;
        }
    }
}

// Purpose: Whether a computer player's node has its basic economy (a
// sawmill, woodcutters, a small tower and the race's stone and forestry
// buildings), stored in the node.
std::uint32_t __fastcall AI_CheckBasicEconomy(const std::uint16_t* player, void* /*edx*/, AiPlayerData* data) {
    const auto& counts = data->building_counts;
    // The race is compared as a word (0x5720d0).
    const auto race = static_cast<std::uint16_t>(g_aiWorld.get()->players[*player].race);
    const bool sawmill = counts[Value(BuildingType::Sawmill)] != 0;
    const bool stone = counts[Value(BuildingType::Stonecutter)] != 0;
    const bool wood = counts[Value(BuildingType::Woodcutter)] != 0;
    const bool forester = counts[Value(BuildingType::Forester)] != 0;
    const bool tower = counts[Value(BuildingType::SmallTower)] != 0;
    bool ready = false;
    if (sawmill) {
        switch (race) {
        case Value(Race::Roman):
        case Value(Race::Amazon): ready = stone && wood && forester && tower; break;
        case Value(Race::Egyptian): ready = stone && wood && tower; break;
        case Value(Race::Asian): ready = wood && forester && tower; break;
        default: break;
        }
    }
    data->basic_economy = ready ? 1 : 0;
    return data->basic_economy;
}

// Purpose: Forgets what the computer players planned for one of a player's
// unit groups (probably when the group is disbanded or re-formed).
std::uint32_t __stdcall AI_ResetUnitGroup(std::uint32_t player, std::uint32_t group) {
    // Flat index, not range-checked (like the original's address arithmetic).
    const std::uint32_t index = player * kUnitGroups + group;
    AiArmyState& army = g_aiArmyState;
    AiGroupOrders& orders = g_aiGroupOrders;
    (&army.group_bytes[0][0])[index] = 0;
    (&army.group_words[0][0])[index] = 0;
    (&army.group_values[0][0])[index] = 0;
    AiGroupTarget& target = (&orders.group_targets[0][0])[index];
    target.x = 0;
    target.y = 0;
    target.unk4 = 0;
    (&orders.group_flags[0][0])[index] = 0;
    return index;
}

// Purpose: Resets the computer players' shared army state (at game start
// and when the AI state is loaded).
std::uint32_t __fastcall AI_ArmyState_Reset(AiArmyState* self, void* /*edx*/) {
    for (std::size_t p = 0; p < kPlayers; ++p) {
        self->per_player000[p] = kPlayerStep000 * static_cast<std::uint32_t>(p);
        self->per_player050[p] = 0;
        self->per_player0a0[p] = 0;
        self->per_player0f0[p] = 0;
        self->per_player140[p] = 0;
        g_aiPerPlayer7e79d0[p] = 0;
        self->per_player4b4[p] = kFar;
        // Groups 1..9 (the original's row pointer starts at +0x198).
        for (std::uint32_t group = 1; group < kUnitGroups; ++group) {
            self->group_values[p][group] = 10 + 2 * group;
        }
    }
    self->unk190 = 0;
    self->unk504 = kUnk504;
    self->unk50c = kUnk50c;
    for (std::uint32_t& value : self->unk510) {
        value = kFar;
    }
    self->unk508 = 0;
    return reinterpret_cast<std::uint32_t>(self);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

struct SiteQueries {
    HasNoteFn hasNote;
    NearFn hasNear, countNear;
    NextMilitaryFn nextMilitary;
    BasicEconomyFn basic;
    ResetGroupFn resetGroup;
    ArmyResetFn armyReset;
};

SiteQueries OriginalSiteQueries() {
    return {Original_AI_HasSiteNoteInArea, Original_AI_HasBuildingNear, Original_AI_CountBuildingsNear,
            Original_AI_NextGarrisonedMilitaryBuilding, Original_AI_CheckBasicEconomy, Original_AI_ResetUnitGroup,
            Original_AI_ArmyState_Reset};
}

SiteQueries OurSiteQueries() {
    return {&AI_HasSiteNoteInArea, &AI_HasBuildingNear, &AI_CountBuildingsNear, &AI_NextGarrisonedMilitaryBuilding,
            &AI_CheckBasicEconomy, &AI_ResetUnitGroup,  &AI_ArmyState_Reset};
}

constexpr std::uint32_t kMap = 400;
constexpr std::size_t kNoteBytes = sizeof(SiteNote) + 8;

struct Saved {
    CGameData* world = g_aiWorld;
    ComputerPlayer* player = g_aiPlayer;
    std::uint16_t sizeX = g_aiMapSizeX, sizeY = g_aiMapSizeY;
    AiArmyState army = g_aiArmyState;
    AiGroupOrders orders = g_aiGroupOrders;
    std::uint32_t perPlayer[kPlayers];
    Saved() { std::memcpy(perPlayer, g_aiPerPlayer7e79d0.get(), sizeof(perPlayer)); }
    ~Saved() {
        g_aiWorld = world;
        g_aiPlayer = player;
        g_aiMapSizeX = sizeX;
        g_aiMapSizeY = sizeY;
        g_aiArmyState = army;
        g_aiGroupOrders = orders;
        std::memcpy(g_aiPerPlayer7e79d0.get(), perPlayer, sizeof(perPlayer));
    }
};

void SetUpBuildings(test::Context& ctx, test::WorldPair& worlds) {
    constexpr std::uint8_t kTypes[] = {2, 3, 15, 16, 18, 24};
    const std::uint32_t count = ctx.rng().Below(40);
    worlds.Both([&](CGameData* w) { w->building_high_water = count; });
    for (std::uint32_t id = 1; id <= count + 1; ++id) {
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Chance(1, 6) ? 0xff : ctx.rng().Below(3));
        const std::uint8_t art = kTypes[ctx.rng().Below(6)];
        const auto garrison = static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? 0 : 1 + ctx.rng().Below(100));
        const auto x = static_cast<std::uint16_t>(ctx.rng().Below(60));
        const auto y = static_cast<std::uint16_t>(ctx.rng().Below(60));
        worlds.Both([&](CGameData* w) {
            layout::Building& b = w->buildings[id];
            b.owner = owner;
            b.art = art;
            b.garrison = garrison;
            b.x = x;
            b.y = y;
        });
    }
}

void CompareSiteQueries(test::Context& ctx, const SiteQueries& candidate, int cases) {
    const SiteQueries original = OriginalSiteQueries();
    Saved saved;
    g_aiMapSizeX = static_cast<std::uint16_t>(kMap);
    g_aiMapSizeY = static_cast<std::uint16_t>(kMap);
    test::WorldPair worlds(ctx, sizeof(CGameData));
    std::vector<std::uint8_t> player(sizeof(ComputerPlayer));
    std::vector<std::uint8_t> notes(kMaxNotes * kNoteBytes);
    std::vector<std::uint8_t> a(sizeof(AiArmyState) + 16), b(a.size());
    std::vector<std::uint8_t> ordersA(sizeof(AiGroupOrders)), perA(4 * kPlayers);
    for (int i = 0; i < cases && ctx.failures() == 0; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(7));
        if (i % 16 == 0) {
            SetUpBuildings(ctx, worlds);
        }
        std::uint32_t ra = 0, rb = 0;
        bool same = true;
        switch (op) {
        case 0: {
            ctx.rng().Fill(player.data(), player.size());
            ctx.rng().Fill(notes.data(), notes.size());
            auto* computer = reinterpret_cast<ComputerPlayer*>(player.data());
            for (std::size_t n = 0; n < kMaxNotes; ++n) {
                auto* note = reinterpret_cast<SiteNote*>(notes.data() + n * kNoteBytes);
                computer->notes[n] = ctx.rng().Chance(1, 3) ? nullptr : &note->base;
                note->base.type = static_cast<std::uint16_t>(ctx.rng().Below(6));
                note->building_type = static_cast<std::uint16_t>(ctx.rng().Below(4));
                note->x = static_cast<std::uint16_t>(ctx.rng().Below(kMap + 10));
                note->y = static_cast<std::uint16_t>(ctx.rng().Below(kMap));
                if (note->x < kMap) {
                    const auto area = static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Below(300));
                    worlds.Both([&](CGameData* w) { w->tiles[note->x][note->y].area = area; });
                }
            }
            g_aiPlayer = computer;
            const std::uint16_t type = static_cast<std::uint16_t>(ctx.rng().Below(4));
            const std::uint16_t area = static_cast<std::uint16_t>(ctx.rng().Below(300));
            g_aiWorld = worlds.original();
            ra = original.hasNote(&type, &area);
            g_aiWorld = worlds.ours();
            rb = candidate.hasNote(&type, &area);
            break;
        }
        case 1:
        case 2: {
            const std::uint32_t type = ctx.rng().Chance(1, 4) ? 0xffff : (ctx.rng().Chance(1, 2) ? 15 : 2);
            const std::uint32_t x = ctx.rng().Below(60) | (ctx.rng().Chance(1, 4) ? ctx.rng().Next32() << 16 : 0);
            const std::uint32_t y = ctx.rng().Below(60);
            const std::uint32_t distance = ctx.rng().Below(40) | (ctx.rng().Chance(1, 4) ? ctx.rng().Next32() << 16 : 0);
            const std::uint32_t who = ctx.rng().Below(3);
            const NearFn fa = op == 1 ? original.hasNear : original.countNear;
            const NearFn fb = op == 1 ? candidate.hasNear : candidate.countNear;
            g_aiWorld = worlds.original();
            ra = fa(type, x, y, distance, who);
            g_aiWorld = worlds.ours();
            rb = fb(type, x, y, distance, who);
            break;
        }
        case 3: {
            const std::uint32_t start = ctx.rng().Chance(1, 8) ? 0xffff : ctx.rng().Below(45);
            const std::uint32_t who = ctx.rng().Below(3) | (ctx.rng().Next32() & 0xffffff00u);
            // The upper half is that of a record pointer (the worlds are
            // 64 KB aligned, so it is the world's plus the record offset's)
            // or 0 after a null lookup.
            const auto relative = [](std::uint32_t value, const CGameData* world) {
                return (value >> 16) == 0 ? value : value - (reinterpret_cast<std::uint32_t>(world) & 0xffff0000u);
            };
            g_aiWorld = worlds.original();
            ra = relative(original.nextMilitary(start, who), worlds.original());
            g_aiWorld = worlds.ours();
            rb = relative(candidate.nextMilitary(start, who), worlds.ours());
            break;
        }
        case 4: {
            ctx.rng().Fill(a.data(), a.size());
            auto* data = reinterpret_cast<AiPlayerData*>(a.data());
            for (std::uint16_t& c : data->building_counts) {
                c = static_cast<std::uint16_t>(ctx.rng().Chance(1, 5) ? 0 : 1);
            }
            b = a;
            const std::uint16_t p = static_cast<std::uint16_t>(ctx.rng().Below(kPlayers));
            const std::int32_t race = ctx.rng().Chance(1, 8) ? static_cast<std::int32_t>(ctx.rng().Next32())
                                                             : static_cast<std::int32_t>(ctx.rng().Below(5));
            worlds.Both([&](CGameData* w) { w->players[p].race = race; });
            g_aiWorld = worlds.original();
            ra = original.basic(&p, nullptr, data);
            g_aiWorld = worlds.ours();
            rb = candidate.basic(&p, nullptr, reinterpret_cast<AiPlayerData*>(b.data()));
            same &= S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size());
            break;
        }
        case 5: {
            ctx.rng().Fill(&g_aiArmyState.get(), sizeof(AiArmyState));
            ctx.rng().Fill(&g_aiGroupOrders.get(), sizeof(AiGroupOrders));
            std::memcpy(b.data(), &g_aiArmyState.get(), sizeof(AiArmyState));
            std::memcpy(ordersA.data(), &g_aiGroupOrders.get(), sizeof(AiGroupOrders));
            const std::uint32_t p = ctx.rng().Below(kPlayers), g = ctx.rng().Below(kUnitGroups);
            original.resetGroup(p, g);
            std::memcpy(a.data(), &g_aiArmyState.get(), sizeof(AiArmyState));
            std::vector<std::uint8_t> ordersOriginal(sizeof(AiGroupOrders));
            std::memcpy(ordersOriginal.data(), &g_aiGroupOrders.get(), sizeof(AiGroupOrders));
            std::memcpy(&g_aiArmyState.get(), b.data(), sizeof(AiArmyState));
            std::memcpy(&g_aiGroupOrders.get(), ordersA.data(), sizeof(AiGroupOrders));
            candidate.resetGroup(p, g);  // EAX is not used by the callers
            same &= S3RE_EXPECT_MEM_EQ(a.data(), &g_aiArmyState.get(), sizeof(AiArmyState));
            same &= S3RE_EXPECT_MEM_EQ(ordersOriginal.data(), &g_aiGroupOrders.get(), sizeof(AiGroupOrders));
            break;
        }
        default: {
            ctx.rng().Fill(a.data(), a.size());
            b = a;
            ctx.rng().Fill(g_aiPerPlayer7e79d0.get(), 4 * kPlayers);
            ra = original.armyReset(reinterpret_cast<AiArmyState*>(a.data()), nullptr) -
                 reinterpret_cast<std::uint32_t>(a.data());
            std::memcpy(perA.data(), g_aiPerPlayer7e79d0.get(), perA.size());
            ctx.rng().Fill(g_aiPerPlayer7e79d0.get(), 4 * kPlayers);
            rb = candidate.armyReset(reinterpret_cast<AiArmyState*>(b.data()), nullptr) -
                 reinterpret_cast<std::uint32_t>(b.data());
            same &= S3RE_EXPECT_MEM_EQ(a.data(), b.data(), a.size());
            same &= S3RE_EXPECT_MEM_EQ(perA.data(), g_aiPerPlayer7e79d0.get(), perA.size());
            break;
        }
        }
        same &= S3RE_EXPECT_EQ(ra, rb);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
        }
    }
    S3RE_EXPECT(worlds.Checkpoint(ctx));
}

std::uint32_t __fastcall WrongCountNear(std::uint32_t type, std::uint32_t x, std::uint32_t y, std::uint32_t distance,
                                        std::uint32_t player) {
    // Wrong: strictly closer than the distance.
    return AI_CountBuildingsNear(type, x, y, distance - 1, player);
}

std::uint32_t __fastcall WrongBasic(const std::uint16_t* player, void* edx, AiPlayerData* data) {
    // Wrong: Egyptians need a forester too.
    const std::uint32_t result = AI_CheckBasicEconomy(player, edx, data);
    if (g_aiWorld.get()->players[*player].race == Value(Race::Egyptian) &&
        data->building_counts[Value(BuildingType::Forester)] == 0) {
        data->basic_economy = 0;
        return 0;
    }
    return result;
}

std::uint32_t __fastcall WrongArmyReset(AiArmyState* self, void* edx) {
    // Wrong: also sets group 0.
    const std::uint32_t result = AI_ArmyState_Reset(self, edx);
    for (auto& row : self->group_values) {
        row[0] = 10;
    }
    return result;
}

}  // namespace

S3RE_TEST(AiSiteQueries_MatchOriginal) {
    CompareSiteQueries(ctx, OurSiteQueries(), 6000);
}

S3RE_TEST(AiSiteQueries_ComparisonDetectsWrongImplementation) {
    const auto probe = [&](auto change) {
        test::Context quiet(ctx.name(), 1, /*quiet=*/true);
        SiteQueries wrong = OurSiteQueries();
        change(wrong);
        CompareSiteQueries(quiet, wrong, 3000);
        S3RE_EXPECT(quiet.failures() > 0);
    };
    probe([](SiteQueries& f) { f.countNear = &WrongCountNear; });
    probe([](SiteQueries& f) { f.basic = &WrongBasic; });
    probe([](SiteQueries& f) { f.armyReset = &WrongArmyReset; });
    probe([](SiteQueries& f) { f.hasNear = f.countNear; });
}

}  // namespace s3re::game
