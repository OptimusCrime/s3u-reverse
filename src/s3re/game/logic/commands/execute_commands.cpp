// CLogicThread::ExecuteCommands (0x00535990): the lockstep executor.
//
// Purpose: before each simulation step, executes the commands the network
// thread sent back for this step (player and computer commands, in the
// same order on every machine) and asks for the next step. Single player
// runs it too (CNetBounceThread answers at once). Plan, message types and
// the command list: re/execute_commands.md; "Commands and lockstep" in
// CLAUDE.md.
//
// Shape (thiscall on the CLogicThread, no arguments, EAX used by
// CLogicThread::Step):
//   unless step (+0x3b0) is 0, repeat: wait for a message on the
//   network -> logic queue (+0x3c8, vtable slot 3, timeout from 0x40d0e0(5)),
//   pop it (byte 2 = its size in bytes) and execute it by type (byte 0):
//   1 Command (byte 1 = CommandId, payload from byte 4), 2 StepGrant (dword 1
//   = the last step allowed; ends the loop), 3/4/5/6/8/0x22 network
//   messages, other types are logged as invalid. After a grant above the
//   current step, or at step 0, the step counts up and EAX = 1. Then the
//   0x20 step message {0x20, 0, 8, 0, step} goes to the network (+0x3c4).
//   A Win32::CTimeout* thrown while waiting ends the function (catch at
//   0x53b823 deletes it; EAX = 0, no step message).
//
// The original is one 24 KB function; ours is split into one handler per
// command (execute_commands.h lists them and their files). This file has
// the frame and the simple world commands, the spell, and the ship
// commands.

#include "game/logic/commands/execute_commands.h"

#include "game/debug_log.h"
#include "game/globals.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/typed_fields.h"
#include "game/vtables.h"
#include "hooks.h"

#include <windows.h>

#include <cstring>

namespace s3re::game {

namespace commands {

namespace {

using layout::Area;
using layout::Building;
using layout::Player;
using layout::Settler;
using layout::Tile;

constexpr std::uint8_t kSettlerNoFight = layout::kSettlerNoFight;

// The menus of world ui_selection_mode that the commands refresh.
constexpr std::int32_t kMenuSelection = 1;  // placing a building (with selection parameters)
constexpr std::int32_t kMenuAreas = 2;      // area settings; ui_2d290[0] is its page
constexpr std::int32_t kMenuPlayer = 3;     // player settings; ui_2d2a0 is its page
constexpr std::int32_t kMenuBuilding = 4;   // the selected building (g_selectedBuilding)
constexpr std::int32_t kPageGoodsSettings = 2;
constexpr std::int32_t kPageConditions = 3;
constexpr std::int32_t kPageSoldiers = 3;

// The campaign mission in which buildings may be placed without a military building.
constexpr std::int32_t kMissionWithoutGarrison = 66;
// SetWorkArea: the work tile must lie closer than this to the building.
constexpr std::uint32_t kWorkAreaRange = 60;
// Ships: settlers aboard at most (LoadShip), steps UnloadShip looks for a shore.
constexpr std::uint8_t kShipPlaces = 6;
constexpr std::uint32_t kShoreSearchSteps = 8;
// LoadShip: offsets (MapOffset table) up to this distance around the ship.
constexpr std::int32_t kLoadShipRange = 15;
// CastSpell: the priest's distance counts minus this.
constexpr std::uint32_t kSpellDistanceAllowance = 2;
// PromoteSoldiers: kinds above this first set the mana to the cost.
constexpr std::uint32_t kPromoteFree = 0x80;
// Godly event records (AllocTable36700Record): the spell, then its follow-up.
constexpr std::uint32_t kEffectSpellCast = 1;
constexpr std::uint32_t kEffectSpellFollowUp = 2;

// ------------------------------------------------------------ helpers

std::uint16_t* GroupSettlers(CGameData* world, std::uint32_t player, std::uint32_t group) {
    // player_lists[player][group]: the original indexes with player * 10 + group (any bytes).
    return &world->player_lists[0][0][0] + (player * 10 + group) * 100;
}
std::int32_t& GroupLength(CGameData* world, std::uint32_t player, std::uint32_t group) {
    return *(&world->player_list_lengths[0][0] + (player * 10 + group));
}
Building& BuildingAt(CGameData* world, std::uint32_t n) {
    return *(world->buildings + n);
}
Settler& SettlerAt(CGameData* world, std::uint32_t n) {
    return *(world->settlers + n);
}
Area& AreaAt(CGameData* world, std::uint32_t n) {
    return *(world->areas + n);
}
Player& PlayerAt(CGameData* world, std::uint32_t n) {
    return *(world->players + n);
}
// The tile at (x, y) with the original's 32-bit offset arithmetic.
Tile& TileAt(CGameData* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

bool IsMilitaryBuilding(BuildingType type) {
    return type == BuildingType::SmallTower || type == BuildingType::LargeTower || type == BuildingType::Castle;
}

}  // namespace

void PushToNet(CLogicThread* thread, const void* message, std::uint32_t dwordCount) {
    using PushFn = std::uint32_t(__fastcall*)(MessageQueue*, void*, const void*, std::uint32_t, std::uint32_t);
    reinterpret_cast<PushFn>(callee::kPushMessage)(thread->toNet, nullptr, message, dwordCount, 1);
}

void RefreshIfBuildingShown(Run& run, std::uint32_t building) {
    // 0x537084
    if (static_cast<std::uint32_t>(g_selectedBuilding) == building && run.world()->ui_selection_mode == kMenuBuilding) {
        run.world()->refresh_ui = 1;
    }
}

void RefreshIfMenuShows(Run& run, std::uint32_t player, std::int32_t menu, std::int32_t page, std::int32_t value) {
    CGameData* world = run.world();
    if (static_cast<std::uint32_t>(world->local_player) == player && world->ui_selection_mode == menu &&
        page == value) {
        world->refresh_ui = 1;  // 0x5370a5
    }
}

// ------------------------------------------------------------ 0x01 PlaceBuilding (0x535aa4)
// Payload: x, y, type (words), player. Outside campaign mission 66 the
// player needs a finished tower or castle with soldiers in it.
void PlaceBuilding(Run& run) {
    const std::uint32_t x = run.Word(0);
    const std::uint32_t y = run.Word(2);
    const std::uint32_t type = run.Word(4);
    const std::uint32_t player = run.Byte(6);
    CGameData* world = run.world();
    if (g_campaignMission != kMissionWithoutGarrison) {
        std::int32_t garrisoned = 0;
        for (std::uint32_t n = 1; n <= world->building_high_water; ++n) {
            const Building& building = BuildingAt(world, n);
            if (building.owner == player && building.buildup == 0 && IsMilitaryBuilding(TypeOf(building)) &&
                building.garrison != 0) {
                ++garrisoned;
            }
        }
        if (garrisoned <= 0) {
            return;
        }
    }
    constexpr std::uint32_t kPlaceSite = 1;  // CheckBuildingSite mode: place the site
    Call<callee::kCheckBuildingSite>(world, x, y, type, player, kPlaceSite);
    if (static_cast<std::uint32_t>(world->local_player) == player && world->ui_selection_mode == kMenuSelection &&
        world->ui_selection_param1 != 0 && world->ui_selection_param2 != 0) {
        Call<callee::kNextSearchGeneration>(world);
    }
}

// ------------------------------------------------------------ 0x02 ScrollCamera (0x535bea)
// Payload: two big-endian 32-bit steps for the camera (UI state in the world).
void ScrollCamera(Run& run) {
    CGameData* world = run.world();
    const std::uint32_t dx = run.Word(0) << 16 | run.Word(2);
    const std::uint32_t dy = run.Word(4) << 16 | run.Word(6);
    world->camera_x = static_cast<std::int32_t>(static_cast<std::uint32_t>(world->camera_x) + dx);
    world->camera_y = static_cast<std::int32_t>(static_cast<std::uint32_t>(world->camera_y) + dy);
    Call<callee::kClampCamera>(world);
    world->redraw = 0;
}

// ------------------------------------------------------------ 0x03 SetSettlerGroup (0x535cb0)
// Payload: player, group, count (word), then the settler numbers (words).
void SetSettlerGroup(Run& run) {
    CGameData* world = run.world();
    const std::uint32_t player = run.Byte(0);
    const std::uint32_t group = run.Byte(1);
    const std::uint32_t count = run.Word(2);
    std::uint16_t* settlers = GroupSettlers(world, player, group);
    for (std::uint32_t i = 0; i < count; ++i) {
        // Settler i is message bytes 8 + 2i, 9 + 2i. A count beyond the
        // message (more than 124 settlers, which the game's UI never sends)
        // makes the original read its own stack past the message buffer; we
        // read zeros there (FrameLocals has no such bytes).
        const std::size_t at = 4 + 2 * static_cast<std::size_t>(i);
        settlers[i] = static_cast<std::uint16_t>(at + 1 < 4 * kMessageDwords - 4 ? run.Word(at) : 0);
    }
    GroupLength(world, player, group) = static_cast<std::int32_t>(count);
}

// ------------------------------------------------------------ 0x04 SetBuildingWorking (0x536fbf)
// Payload: building (word), working (0 = stop), player. Stopping a site
// clears its request bits and sends its carriers away; stopping a finished
// building recalls its carriers.
void SetBuildingWorking(Run& run) {
    CGameData* world = run.world();
    const std::uint32_t number = run.Word(0);
    const std::uint8_t working = run.Byte(2);
    const std::uint32_t player = run.Byte(3);
    Building& building = BuildingAt(world, number);
    if (building.owner == player) {
        // The original adds the byte to the cleared bit 0 (a value above 1 carries into bit 1).
        building.request_flags = static_cast<std::uint8_t>((building.request_flags & 0xfe) + working);
        if (working == 0) {
            if (BuildingAt(world, number).buildup != 0) {
                Call<callee::kClearBuildingFlag02>(world, number);
                Call<callee::kClearBuildingFlag04>(run.world(), number);
                Call<callee::kClearBuildingFlag10>(run.world(), number);
                Call<callee::kClearBuildingFlag08>(run.world(), number);
                Call<callee::kRecallSiteCarriers>(run.world(), number);
            } else {
                Call<callee::kRecallBuildingCarriers>(world, number);
            }
        }
    }
    RefreshIfBuildingShown(run, number);
}

// ------------------------------------------------------------ 0x05 SetWorkArea (0x5370b4)
// Payload: x, y, building (words), player. The building types with a work
// area take a tile closer than 60 in the area of their door.
bool HasWorkArea(BuildingType type) {
    switch (type) {
    case BuildingType::Woodcutter:
    case BuildingType::Waterworks:
    case BuildingType::Vineyard:
    case BuildingType::Fishery:
    case BuildingType::RiceFarm:
    case BuildingType::Farm:
    case BuildingType::Forester:
    case BuildingType::Stonecutter:
    case BuildingType::Apiary:
        return true;
    default:
        return false;
    }
}

void SetWorkArea(Run& run) {
    const std::uint32_t x = run.Word(0);
    const std::uint32_t y = run.Word(2);
    const std::uint32_t number = run.Word(4);
    const std::uint32_t player = run.Byte(6);
    CGameData* world = run.world();
    if (BuildingAt(world, number).owner != player) {
        return;
    }
    if (Call<callee::kTileDistance>(world, x, y, BuildingAt(world, number).x, BuildingAt(world, number).y) >=
        kWorkAreaRange) {
        return;
    }
    world = run.world();
    Building& building = BuildingAt(world, number);
    if (TileAt(world, building.door_x, building.door_y).area != TileAt(world, x, y).area ||
        !HasWorkArea(TypeOf(building))) {
        return;
    }
    if (building.buildup == 0) {
        building.work_x = static_cast<std::uint16_t>(x);
        building.work_y = static_cast<std::uint16_t>(y);
    } else {
        // A site keeps the chosen tile in its pile ring word and its third pile word.
        building.pile_ring = static_cast<std::uint16_t>(x);
        building.piles[2] = static_cast<std::uint16_t>(y);
    }
    if (const std::uint16_t worker = building.worker; worker != 0) {
        SettlerAt(world, worker).next = 0;  // 0x53724e
    }
    if (static_cast<std::uint32_t>(g_selectedBuilding) == number && world->ui_selection_mode == kMenuBuilding) {
        CallThread<callee::kDrawWorkAreaOverlay>(run.thread, number);
        run.world()->refresh_ui = 1;
    }
}

// ------------------------------------------------------------ 0x07, 0x08, 0x11: area settings
// Payload: index, value, player, area (word). The area must be the player's.

// 0x5375c7: a goods priority one up (value != 0) or down, within 0..20.
void ChangeGoodsPriority(Run& run) {
    constexpr std::uint8_t kHighest = 20;
    const std::uint32_t good = run.Byte(0);
    const bool up = run.Byte(1) != 0;
    const std::uint32_t player = run.Byte(2);
    Area& area = AreaAt(run.world(), run.Word(3));
    if (area.player != player) {
        return;
    }
    std::uint8_t& priority = *(area.settings1 + good);
    if (up) {
        if (priority == kHighest) {  // only exactly 20 stops it
            return;
        }
        ++priority;
    } else {
        if (priority == 0) {
            return;
        }
        --priority;
    }
    RefreshIfMenuShows(run, player, kMenuAreas, run.world()->ui_2d290[0], kPageGoodsSettings);
}

// 0x53767d
void SetGoodsSetting(Run& run) {
    const std::uint32_t good = run.Byte(0);
    const std::uint8_t value = run.Byte(1);
    const std::uint32_t player = run.Byte(2);
    const std::uint32_t number = run.Word(3);
    Area& area = AreaAt(run.world(), number);
    if (area.player != player) {
        return;
    }
    *(area.settings2 + good) = value;
    Call<callee::kClearAreaGoodsSettings1D2>(run.world(), number);
    RefreshIfMenuShows(run, player, kMenuAreas, run.world()->ui_2d290[0], kPageGoodsSettings);
}

// 0x537728
void SetAreaCondition(Run& run) {
    const std::uint32_t kind = run.Byte(0);
    const std::uint8_t value = run.Byte(1);
    const std::uint32_t player = run.Byte(2);
    const std::uint32_t number = run.Word(3);
    Area& area = AreaAt(run.world(), number);
    if (area.player != player) {
        return;
    }
    *(area.condition_enabled + kind) = value;
    Call<callee::kResetAreaConditionCounters>(run.world(), number, kind);
    RefreshIfMenuShows(run, player, kMenuAreas, run.world()->ui_2d290[0], kPageConditions);
}

// ------------------------------------------------------------ 0x14, 0x17: dock lines (0x538e45, 0x538f19)
// Payload: x, y, building (words), player. A finished, idle shipyard or
// harbour of the player gets a new dock line near (x, y).
void SetDock(Run& run, BuildingType type) {
    const std::uint32_t x = run.Word(0);
    const std::uint32_t y = run.Word(2);
    const std::uint32_t number = run.Word(4);
    const std::uint32_t player = run.Byte(6);
    const Building& building = BuildingAt(run.world(), number);
    if (TypeOf(building) != type || building.buildup != 0 || building.workshop.state != 0 ||
        building.owner != player) {
        return;
    }
    Call<callee::kBlockBuildingLine>(run.world(), number);
    Call<callee::kPlaceDockLine>(run.world(), number, x, y);
}

void SetShipyardDock(Run& run) {
    SetDock(run, BuildingType::Shipyard);
}

void SetHarbourDock(Run& run) {
    SetDock(run, BuildingType::Harbour);
}

// ------------------------------------------------------------ 0x15 CastSpell (0x53888e)
// Payload: player, spell slot (0..7 of the race), x, y, priest (words). The
// priest (a mage, type 36) of the player casts the race's spell at (x, y)
// if the player has the mana and the tile is in range. The mana is not
// taken here.
void CastSpell(Run& run) {
    const std::uint32_t player = run.Byte(0);
    const std::uint32_t slot = run.Byte(1);
    const std::uint32_t x = run.Word(2);
    const std::uint32_t y = run.Word(4);
    const std::uint32_t priest = run.Word(6);
    CGameData* world = run.world();
    const Settler& mage = SettlerAt(world, priest);
    if (mage.owner != player || TypeOf(mage) != SettlerType::Mage) {
        return;
    }
    const Player& owner = PlayerAt(world, player);
    if (static_cast<std::uint32_t>(*(owner.spell_mana + slot)) > static_cast<std::uint32_t>(owner.mana)) {
        return;
    }
    // g_raceSpells[race][slot], indexed as the original (race * 8 + slot, any values).
    const std::uint32_t spell = *(&g_raceSpells[0][0] + (static_cast<std::uint32_t>(owner.race) * 8 + slot));
    GameDebugLog(texts::kGodlyEventRange, spell, g_spellCastRange[spell]);
    world = run.world();
    const std::uint32_t distance =
        Call<callee::kTileDistance>(world, x, y, SettlerAt(world, priest).x, SettlerAt(world, priest).y);
    if (distance - kSpellDistanceAllowance >= g_spellCastRange[spell]) {
        return;
    }
    Call<callee::kAllocTable36700Record>(run.world(), kEffectSpellCast, 1, player, x, y, priest, spell, 0, 0, 0);
    Call<callee::kAllocTable36700Record>(run.world(), kEffectSpellFollowUp,
                                         static_cast<std::uint32_t>(g_table658ee4[spell]) + 1, player, x, y, priest,
                                         spell, 0, 0, 0);
    const Settler& after = SettlerAt(run.world(), priest);
    GameDebugLog(texts::kGodlyEventHappened, spell, x, y, static_cast<std::uint32_t>(after.x),
                 static_cast<std::uint32_t>(after.y), g_spellCastRange[spell]);
}

// ------------------------------------------------------------ 0x16 UnloadShip (0x538bd3)
// Payload: player, ship (word). A ferry (type 44) of the player lying in
// water (terrain 5 or 6) with passengers looks for the shore (terrain 48)
// in its six directions, up to 8 steps; its passengers are put down there.
void UnloadShip(Run& run) {
    const std::uint32_t player = run.Byte(0);
    const std::uint32_t number = run.Word(1);
    CGameData* world = run.world();
    Settler& ship = SettlerAt(world, number);
    if (ship.owner != player || TypeOf(ship) != SettlerType::Ferry) {
        return;
    }
    const std::uint32_t shipX = ship.x;
    const std::uint32_t shipY = ship.y;
    const std::uint8_t water = TileAt(world, shipX, shipY).terrain;
    if ((water != kTerrainWater5 && water != kTerrainWater6) || ship.passengers == 0) {
        return;
    }
    // 0x538c67..0x538d37: walk outwards in all six directions at once; in
    // the first step that reaches a shore tile the last direction that does wins.
    std::uint32_t walkX[kDirectionCount];
    std::uint32_t walkY[kDirectionCount];
    for (std::uint32_t d = 0; d < kDirectionCount; ++d) {
        walkX[d] = shipX;
        walkY[d] = shipY;
    }
    bool found = false;
    std::uint32_t shoreX = 0;
    std::uint32_t shoreY = 0;
    std::uint32_t shoreDirection = 0;
    for (std::uint32_t step = 0; step < kShoreSearchSteps && !found; ++step) {
        for (std::uint32_t d = 0; d < kDirectionCount; ++d) {
            const std::uint32_t nx = Call<callee::kNeighbourX>(run.world(), walkX[d], d);
            walkX[d] = nx;
            const std::uint32_t ny = Call<callee::kNeighbourY>(run.world(), walkY[d], d);
            walkY[d] = ny;
            world = run.world();
            if (nx < world->map_size && ny < world->map_size && TileAt(world, nx, ny).terrain == kTerrainShore) {
                found = true;
                shoreX = nx;
                shoreY = ny;
                shoreDirection = d;
            }
        }
    }
    if (!found) {
        return;
    }
    // 0x538d3f: the ship faces the shore; each passenger leaves the chain
    // and is placed near the shore tile (job 0), or goes back aboard.
    world = run.world();
    SettlerAt(world, number).direction = shoreDirection;
    std::uint32_t passenger = SettlerAt(world, number).next;
    SettlerAt(world, number).next = 0;
    const std::uint32_t count = SettlerAt(run.world(), number).passengers;
    SettlerAt(run.world(), number).passengers = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t following = SettlerAt(run.world(), passenger).next;
        if (Call<callee::kPlaceSettlerNear>(run.world(), passenger, shoreX, shoreY) == 0) {
            world = run.world();
            SettlerAt(world, passenger).next = SettlerAt(world, number).next;
            SettlerAt(run.world(), number).next = static_cast<std::uint16_t>(passenger);
            ++SettlerAt(run.world(), number).passengers;
        } else {
            SettlerAt(run.world(), passenger).job = 0;
        }
        passenger = following;
    }
}

// ------------------------------------------------------------ 0x18 PromoteSoldiers (0x538fed)
// Payload: player, kind (1 swords, 2 bows, 3 pikes; + 0x80: the mana is set
// to the cost first). A level is bought for the player's promotion cost
// (Player +0xb4, promotion_cost) from mana_total; level 1 -> 2 needs both other kinds at
// level 1 or more, higher levels can't be bought here.
void PromoteSoldiers(Run& run) {
    const std::uint32_t player = run.Byte(0);
    std::uint32_t kind = run.Byte(1);
    if (kind >= kPromoteFree) {
        kind -= kPromoteFree;
        Player& p = PlayerAt(run.world(), player);
        p.mana_total = p.promotion_cost;
    }
    if (kind >= 1 && kind <= 3) {
        Player& p = PlayerAt(run.world(), player);
        std::int32_t* levels = p.soldier_levels;
        std::int32_t& level = levels[kind - 1];
        const std::int32_t other1 = levels[kind == 1 ? 1 : 0];  // 0x53906b / 0x5390d0 / 0x53912a
        const std::int32_t other2 = levels[kind == 3 ? 1 : 2];
        const bool affordable = static_cast<std::uint32_t>(p.mana_total) >= static_cast<std::uint32_t>(p.promotion_cost);
        const bool allowed = level == 0 || (level == 1 && other1 != 0 && other2 != 0);
        if (affordable && allowed) {
            ++level;
            p.mana_total -= p.promotion_cost;
            Call<callee::kSetPromotionCost>(run.world(), player);
        }
    }
    RefreshIfMenuShows(run, player, kMenuPlayer, run.world()->ui_2d2a0, kPageSoldiers);
}

// ------------------------------------------------------------ 0x19 LoadShip (0x538a4f)
// Payload: player, ship (word). A ship of the player with free places takes
// the units waiting at the war machine workshops (+0x36) of its owner within
// distance 15.
bool IsWarMachineWorkshop(BuildingType type) {
    return type == BuildingType::CatapultWorkshop || type == BuildingType::BallistaWorkshop ||
           type == BuildingType::CannonWorkshop || type == BuildingType::LargeGong;
}

void LoadShip(Run& run) {
    const std::uint32_t player = run.Byte(0);
    const std::uint32_t number = run.Word(1);
    CGameData* world = run.world();
    if (SettlerAt(world, number).passengers == kShipPlaces || SettlerAt(world, number).owner != player) {
        return;
    }
    const std::uint32_t shipX = SettlerAt(world, number).x;
    const std::uint32_t shipY = SettlerAt(world, number).y;
    for (std::uint32_t i = 0;; ++i) {
        world = run.world();
        if ((world->offsets + i)->distance > kLoadShipRange || SettlerAt(world, number).passengers == kShipPlaces) {
            return;
        }
        const std::uint32_t x = shipX + static_cast<std::uint32_t>((world->offsets + i)->dx);
        const std::uint32_t y = shipY + static_cast<std::uint32_t>((world->offsets + i)->dy);
        if (Call<callee::kIsOnMap>(world, x, y) == 0) {
            continue;
        }
        world = run.world();
        const Tile& tile = TileAt(world, x, y);
        const std::uint32_t buildingNumber = tile.object_number;
        if ((tile.flags & layout::kTileHasBuilding) == 0 || buildingNumber == 0) {
            continue;
        }
        Building& building = BuildingAt(world, buildingNumber);
        if (building.owner != SettlerAt(world, number).owner || !IsWarMachineWorkshop(TypeOf(building))) {
            continue;
        }
        // 0x538b86: one unit at a time until the ship is full or the workshop empty.
        while (SettlerAt(world, number).passengers != kShipPlaces && building.unk36 != 0) {
            ++SettlerAt(world, number).passengers;
            --building.unk36;
        }
    }
}

// ------------------------------------------------------------ 0x1b HoldPosition (0x535d63)
// Payload: player, group. The soldiers (class 1..9) of the group that stand
// or walk get flag 0x20 (no fighting) and, when walking, their position as
// their target.
void HoldPosition(Run& run) {
    const std::uint32_t player = run.Byte(0);
    const std::uint32_t group = run.Byte(1);
    CGameData* world = run.world();
    if (GroupLength(world, player, group) == 0) {
        return;
    }
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(GroupLength(run.world(), player, group)); ++i) {
        world = run.world();
        Settler& settler = SettlerAt(world, GroupSettlers(world, player, group)[i]);
        const std::int32_t settlerClass = Value(ClassOf(settler));
        if (settlerClass < 1 || settlerClass > 9) {
            continue;
        }
        const SettlerJob job = JobOf(settler);
        if (job != SettlerJob::UnitIdle && job != SettlerJob::UnitWalk && job != SettlerJob::Job176) {
            continue;
        }
        settler.flags |= kSettlerNoFight;
        if (JobOf(settler) == SettlerJob::UnitWalk || JobOf(settler) == SettlerJob::Job176) {
            settler.target_x = settler.x;
            settler.target_y = settler.y;
            settler.waypoint_x = settler.x;
            settler.waypoint_y = settler.y;
        }
    }
}

// ------------------------------------------------------------ 0x1c SetStorehouseGoods (0x535e8e)
// Payload: building (word), player, good. A storehouse of the player keeps
// the good (+0x0e) and its six piles (two tiles away) change to it.
void SetStorehouseGoods(Run& run) {
    const std::uint32_t number = run.Word(0);
    const std::uint32_t player = run.Byte(2);
    const std::uint8_t good = run.Byte(3);
    Building& building = BuildingAt(run.world(), number);
    if (building.owner != player || TypeOf(building) != BuildingType::Storehouse) {
        return;
    }
    building.pile_ring = good;
    // The six pile tiles, as offsets (in tiles) from the storehouse: (x-2, y),
    // (x+2, y), (x-2, y-2), (x+2, y+2), (x, y-2), (x, y+2).
    constexpr std::int32_t kPileTiles[6][2] = {{-2, 0}, {2, 0}, {-2, -2}, {2, 2}, {0, -2}, {0, 2}};
    for (const auto& offset : kPileTiles) {
        CGameData* world = run.world();
        const Building& b = BuildingAt(world, number);
        const std::uint32_t at = TileOffset(b.x, b.y) + static_cast<std::uint32_t>(offset[0]) * kTileRowBytes +
                                 static_cast<std::uint32_t>(offset[1]) * kTileBytes;
        Call<callee::kChangePileGood>(world, TileAtOffset(world, at).object_number, good);
    }
}

// ------------------------------------------------------------ 0xf1 RevealMap (0x53b34e)
void RevealMap(Run&) {
    g_revealMap = 1;
}

// ------------------------------------------------------------ 0xf5, 0xf6: site priority (0x53a608, 0x53a690)
// Payload: building (word), player.
void PrioritizeSite(Run& run) {
    const std::uint32_t number = run.Word(0);
    const std::uint32_t player = run.Byte(2);
    if (BuildingAt(run.world(), number).owner != player) {
        return;
    }
    Call<callee::kSetBuildingFlag02>(run.world(), number);
    Call<callee::kSetBuildingFlag04>(run.world(), number);
    Call<callee::kSetBuildingFlag10>(run.world(), number);
    Call<callee::kSetBuildingFlag08>(run.world(), number);
    Call<callee::kRecallCarriersOfOtherSites>(run.world(), number);
    RefreshIfBuildingShown(run, number);
}

void UnprioritizeSite(Run& run) {
    const std::uint32_t number = run.Word(0);
    const std::uint32_t player = run.Byte(2);
    if (BuildingAt(run.world(), number).owner != player) {
        return;
    }
    Call<callee::kClearBuildingFlag02>(run.world(), number);
    Call<callee::kClearBuildingFlag04>(run.world(), number);
    Call<callee::kClearBuildingFlag10>(run.world(), number);
    Call<callee::kClearBuildingFlag08>(run.world(), number);
    CGameData* world = run.world();
    // The owner's open building requests (Player +0x24, settler_lists[2]).
    if (PlayerAt(world, BuildingAt(world, number).owner).settler_lists[2] != 0) {
        Call<callee::kRecallSiteCarriers>(world, number);
    }
    RefreshIfBuildingShown(run, number);
}

// ------------------------------------------------------------ the frame

namespace {

// 0x535a00..0x535a57: waits for the next message and pops it into the frame.
void ReceiveMessage(CLogicThread* thread, FrameLocals& frame) {
    using WaitTimeFn = std::uint32_t(__fastcall*)(std::uint32_t kind, void* edx);
    constexpr std::uint32_t kWaitKind = 5;
    const std::uint32_t timeout = reinterpret_cast<WaitTimeFn>(callee::kWaitTime)(kWaitKind, nullptr);
    MessageQueue* queue = thread->fromNet;
    using WaitFn = std::uint32_t(__fastcall*)(MessageQueue*, void*, std::uint32_t timeout, std::uint32_t flag);
    constexpr std::size_t kWaitSlot = 3;
    reinterpret_cast<WaitFn>(queue->vtable[kWaitSlot])(queue, nullptr, timeout, 1);

    using PopFn = const std::uint32_t*(__fastcall*)(MessageQueue*, void*);
    const auto pop = reinterpret_cast<PopFn>(callee::kPopMessage);
    frame.message.dwords[0] = *pop(thread->fromNet, nullptr);
    // Byte 2 of the first dword is the size in bytes (re-read per dword, it doesn't change).
    for (std::int32_t i = 1; i < static_cast<std::int32_t>(frame.message.bytes[2] >> 2); ++i) {
        frame.message.dwords[i] = *pop(thread->fromNet, nullptr);
    }
}

// 0x535a78: a Command message, by its id (byte 1; jump table 0x53b898,
// index 0x53b934). Ids without a handler (0, 0x1a, 0x1d..0xf0, 0xf3) do nothing.
void ExecuteCommand(Run& run) {
    switch (static_cast<CommandId>(run.Id())) {
    case CommandId::PlaceBuilding: PlaceBuilding(run); return;
    case CommandId::ScrollCamera: ScrollCamera(run); return;
    case CommandId::SetSettlerGroup: SetSettlerGroup(run); return;
    case CommandId::SetBuildingWorking: SetBuildingWorking(run); return;
    case CommandId::SetWorkArea: SetWorkArea(run); return;
    case CommandId::DestroyBuilding: DestroyBuilding(run); return;
    case CommandId::ChangeGoodsPriority: ChangeGoodsPriority(run); return;
    case CommandId::SetGoodsSetting: SetGoodsSetting(run); return;
    case CommandId::MoveUnits:
    case CommandId::MoveUnitsMode1:
    case CommandId::MoveUnitsMode2:
    case CommandId::MoveUnitsMode3: MoveUnits(run); return;
    case CommandId::ChangeTransportOrder: ChangeTransportOrder(run); return;
    case CommandId::ChangeToolSettings: ChangeToolSettings(run); return;
    case CommandId::ChangeGarrison: ChangeGarrison(run); return;
    case CommandId::ConvertCarriers: ConvertCarriers(run); return;
    case CommandId::SetAreaCondition: SetAreaCondition(run); return;
    case CommandId::SetTradeRoute: SetTradeRoute(run); return;
    case CommandId::SetWorkshopProduction: SetWorkshopProduction(run); return;
    case CommandId::SetShipyardDock: SetShipyardDock(run); return;
    case CommandId::CastSpell: CastSpell(run); return;
    case CommandId::UnloadShip: UnloadShip(run); return;
    case CommandId::SetHarbourDock: SetHarbourDock(run); return;
    case CommandId::PromoteSoldiers: PromoteSoldiers(run); return;
    case CommandId::LoadShip: LoadShip(run); return;
    case CommandId::HoldPosition: HoldPosition(run); return;
    case CommandId::SetStorehouseGoods: SetStorehouseGoods(run); return;
    case CommandId::RevealMap: RevealMap(run); return;
    case CommandId::ClearPlayerFlag: ClearPlayerFlag(run); return;
    case CommandId::SyncCheck: SyncCheck(run); return;
    case CommandId::PrioritizeSite: PrioritizeSite(run); return;
    case CommandId::UnprioritizeSite: UnprioritizeSite(run); return;
    case CommandId::SaveRequest: SaveRequest(run); return;
    case CommandId::SaveDone: SaveDone(run); return;
    case CommandId::SaveFailed: SaveFailed(run); return;
    case CommandId::SaveAllowed: SaveAllowed(run); return;
    case CommandId::SyncRandomCheck: SyncRandomCheck(run); return;
    case CommandId::PlayerLost: PlayerLost(run); return;
    case CommandId::PlayerWon: PlayerWon(run); return;
    case CommandId::DropPlayer: DropPlayer(run); return;
    case CommandId::PlayerLeft: PlayerLeft(run); return;
    }
}

// The body of ExecuteCommands up to the step message (inside the
// original's try block). `stepped` is the original's return value local
// ([ebp-0x90]).
void ExecuteMessages(CLogicThread* thread, FrameLocals& frame, std::uint32_t& stepped) {
    Run run{thread, &frame};
    bool advance = thread->step == 0;  // 0x5359fa: the first call only asks for step 1
    if (!advance) {
        for (;;) {
            ReceiveMessage(thread, frame);
            const std::uint8_t type = frame.message.bytes[0];
            if (type == Value(NetMessage::StepGrant)) {
                // 0x53b35d: run up to the granted step; count up unless already there.
                const std::uint32_t granted = frame.message.dwords[1];
                thread->granted_step = granted;
                advance = thread->step < granted;
                break;
            }
            switch (static_cast<NetMessage>(type)) {
            case NetMessage::Command: ExecuteCommand(run); break;
            case NetMessage::PlayerLost: NetPlayerLost(run); break;
            case NetMessage::FilmEnd: NetFilmEnd(run); break;
            case NetMessage::KillPlayer: NetKillPlayer(run); break;
            case NetMessage::InGameChat: NetInGameChat(run); break;
            case NetMessage::ChecksumFailure: NetChecksumFailure(run); break;
            case NetMessage::LagReport: NetLagReport(run); break;
            default: GameDebugLog(texts::kInvalidNetMessage, static_cast<std::uint32_t>(type)); break;
            }
        }
    }
    if (advance) {
        ++thread->step;  // 0x53b377
        stepped = 1;
    }
    // 0x53b388: ask for the next step.
    std::memcpy(frame.step_message + 4, &thread->step, 4);
    PushToNet(thread, frame.step_message, frame.step_message[2] >> 2);
}

// The catch of the original (0x53b823): Win32::CTimeout* (type descriptor
// ".PAVCTimeout@Win32@@" at 0x698e98), thrown by the queue's wait when no
// message comes in time. Matched like MSVC's handler does, by the thrown
// type's catchable types.
constexpr DWORD kCxxExceptionCode = 0xe06d7363;
constexpr ULONG_PTR kCxxMagic = 0x19930520;
constexpr std::uintptr_t kTimeoutPointerType = type_descriptor::kCTimeoutPointer;
constexpr char kTimeoutPointerName[] = ".PAVCTimeout@Win32@@";

#pragma pack(push, 4)
struct CatchableType {
    std::uint32_t properties;
    const std::uint8_t* type;  // TypeDescriptor: vtable, spare, then the decorated name
    std::int32_t mdisp, pdisp, vdisp;
    std::int32_t size;
    void* copy;
};
struct CatchableTypeArray {
    std::int32_t count;
    const CatchableType* types[1];
};
struct ThrowInfo {
    std::uint32_t attributes;
    void* unwind;
    void* forward_compat;
    const CatchableTypeArray* catchable;
};
#pragma pack(pop)

struct CaughtTimeout {
    void* object = nullptr;
};

bool IsTimeoutPointer(const CatchableType* type) {
    if (reinterpret_cast<std::uintptr_t>(type->type) == kTimeoutPointerType) {
        return true;
    }
    return std::strcmp(reinterpret_cast<const char*>(type->type + 8), kTimeoutPointerName) == 0;
}

int TimeoutFilter(const EXCEPTION_RECORD* record, CaughtTimeout* caught) {
    if (record->ExceptionCode != kCxxExceptionCode || record->NumberParameters < 3 ||
        record->ExceptionInformation[0] != kCxxMagic || record->ExceptionInformation[2] == 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const auto* info = reinterpret_cast<const ThrowInfo*>(record->ExceptionInformation[2]);
    const CatchableTypeArray* types = info->catchable;
    for (std::int32_t i = 0; i < types->count; ++i) {
        const CatchableType* type = types->types[i];
        if (!IsTimeoutPointer(type)) {
            continue;
        }
        // The thrown value is the pointer; adjust it to the caught type (__AdjustPointer).
        auto* thrown = *reinterpret_cast<std::uint8_t**>(record->ExceptionInformation[1]);
        if (thrown != nullptr) {
            std::uint8_t* adjusted = thrown + type->mdisp;
            if (type->pdisp >= 0) {
                const auto* vbtable = *reinterpret_cast<std::uint8_t* const*>(thrown + type->pdisp);
                adjusted += *reinterpret_cast<const std::int32_t*>(vbtable + type->vdisp) + type->pdisp;
            }
            thrown = adjusted;
        }
        caught->object = thrown;
        return EXCEPTION_EXECUTE_HANDLER;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// Runs the body inside the equivalent of the original's try block; true if
// a CTimeout ended it (no C++ objects in this frame, so __try is allowed).
bool ExecuteMessagesGuarded(CLogicThread* thread, FrameLocals* frame, std::uint32_t* stepped, CaughtTimeout* caught) {
    __try {
        ExecuteMessages(thread, *frame, *stepped);
        return false;
    } __except (TimeoutFilter(GetExceptionInformation()->ExceptionRecord, caught)) {
        return true;
    }
}

}  // namespace

}  // namespace commands

using ExecuteCommandsFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx);

// Purpose: runs the commands of the next lockstep step (all players' input
// and the computer players' decisions) and asks the network thread for the
// step after it; returns 1 when the logic may run a step.
std::uint32_t __fastcall CLogicThread_ExecuteCommands(CLogicThread* self, void*) {
    commands::FrameLocals frame;
    // 0x5359b3..0x5359e1: the outgoing messages the frame keeps.
    constexpr std::uint8_t kStepMessage[8] = {Value(NetMessage::StepRequest), 0, 8, 0, 0, 0, 0, 0};
    constexpr std::uint8_t kKillMessage[8] = {Value(NetMessage::KillPlayer), 0, 8, 0, 0, 0, 0, 0};
    std::memcpy(frame.step_message, kStepMessage, sizeof(kStepMessage));
    std::memcpy(frame.kill_message, kKillMessage, sizeof(kKillMessage));
    std::memset(frame.message.bytes, 0, sizeof(frame.message.bytes));
    std::memset(frame.reply_message, 0, sizeof(frame.reply_message));
    frame.garrison_soldiers = 0;
    frame.garrison_bowmen = 0;
    std::uint32_t stepped = 0;
    commands::CaughtTimeout caught;
    if (commands::ExecuteMessagesGuarded(self, &frame, &stepped, &caught) && caught.object != nullptr) {
        // 0x53b823: delete the exception (deleting destructor, vtable slot 0).
        using DeleteFn = void(__fastcall*)(void* object, void* edx, std::uint32_t flags);
        const auto* vtable = *static_cast<void* const* const*>(caught.object);
        reinterpret_cast<DeleteFn>(vtable[0])(caught.object, nullptr, 1);
    }
    return stepped;
}

S3RE_HOOK(0x00535990, CLogicThread_ExecuteCommands, ExecuteCommandsFn, Original_CLogicThread_ExecuteCommands,
          CLogicThread_ExecuteCommands);

}  // namespace s3re::game

// ---------------------------------------------------------------------------
// Tests: the frame and the commands of this file (execute_commands_test.h).
// ---------------------------------------------------------------------------

#include "game/logic/commands/execute_commands_test.h"

namespace s3re::game::commands {

namespace {

using bench::Bench;
using bench::MessageBuilder;

constexpr std::uint32_t kTestBuildings = 40;
constexpr std::uint32_t kTestSettlers = 120;
constexpr std::uint32_t kTestAreas = 16;
constexpr std::uint32_t kTestMapSize = 384;

// Saves and restores the S3.EXE globals the tests change.
struct SavedGlobals {
    std::int32_t mission = g_campaignMission;
    std::int32_t selected = g_selectedBuilding;
    std::int32_t reveal = g_revealMap;
    ~SavedGlobals() {
        g_campaignMission = mission;
        g_selectedBuilding = selected;
        g_revealMap = reveal;
    }
};

// Callees of this file's commands, recorded; the effects give the return
// values the handlers use.
void InstallWorldStubs(test::CalleeStubs& stubs) {
    stubs.Install<5, callee::kCheckBuildingSite>();
    stubs.Install<0, callee::kNextSearchGeneration>();
    stubs.Install<0, callee::kClampCamera>();
    stubs.Install<2, callee::kChangePileGood>();
    stubs.Install<1, callee::kClearBuildingFlag02>();
    stubs.Install<1, callee::kClearBuildingFlag04>();
    stubs.Install<1, callee::kClearBuildingFlag08>();
    stubs.Install<1, callee::kClearBuildingFlag10>();
    stubs.Install<1, callee::kSetBuildingFlag02>();
    stubs.Install<1, callee::kSetBuildingFlag04>();
    stubs.Install<1, callee::kSetBuildingFlag08>();
    stubs.Install<1, callee::kSetBuildingFlag10>();
    stubs.Install<1, callee::kRecallSiteCarriers>();
    stubs.Install<1, callee::kRecallBuildingCarriers>();
    stubs.Install<1, callee::kRecallCarriersOfOtherSites>();
    stubs.Install<4, callee::kTileDistance>([](CGameData*, std::uint32_t, const std::uint32_t* a) {
        return (a[0] * 7 + a[1] * 3 + a[2] + a[3]) % 90;
    });
    stubs.Install<1, callee::kDrawWorkAreaOverlay>();
    stubs.Install<1, callee::kClearAreaGoodsSettings1D2>();
    stubs.Install<2, callee::kResetAreaConditionCounters>();
    stubs.Install<1, callee::kBlockBuildingLine>();
    stubs.Install<3, callee::kPlaceDockLine>();
    stubs.Install<1, callee::kSetPromotionCost>([](CGameData* w, std::uint32_t, const std::uint32_t* a) {
        w->players[a[0] % 20].promotion_cost = static_cast<std::int32_t>(a[0] * 10 + 20);  // the next cost
        return 0u;
    });
    stubs.Install<10, callee::kAllocTable36700Record>();
    stubs.Install<2, callee::kIsOnMap>([](CGameData*, std::uint32_t, const std::uint32_t* a) {
        return static_cast<std::uint32_t>(a[0] < kTestMapSize && a[1] < kTestMapSize && (a[0] + a[1]) % 5 != 0);
    });
    stubs.Install<2, callee::kNeighbourX>([](CGameData*, std::uint32_t, const std::uint32_t* a) {
        return a[0] + (a[1] % 3) - 1;
    });
    stubs.Install<2, callee::kNeighbourY>([](CGameData*, std::uint32_t, const std::uint32_t* a) {
        return a[0] + (a[1] & 1) * 2 - 1;
    });
    stubs.Install<3, callee::kPlaceSettlerNear>([](CGameData*, std::uint32_t, const std::uint32_t* a) {
        return a[0] & 1;
    });
}

// A world whose records the commands use hold sane numbers (the rest stays random).
void SetUpWorld(test::Context& ctx, Bench& bench) {
    test::Rng& rng = ctx.rng();
    test::WorldPair& worlds = bench.worlds;
    worlds.Both([&](CGameData* w) {
        w->map_size = kTestMapSize;
        w->building_high_water = kTestBuildings;
        w->settler_high_water = kTestSettlers;
    });
    // Every tile: a small object number (buildings 1..40 or none) and some shore / water.
    for (std::uint32_t x = 0; x < layout::kMapRowTiles; ++x) {
        for (std::uint32_t y = 0; y < layout::kMapRowTiles; ++y) {
            const auto object = static_cast<std::uint16_t>(rng.Below(kTestBuildings + 8));
            const std::uint8_t terrains[] = {kTerrainShore, kTerrainWater5, kTerrainWater6, 16, 32, 48};
            const std::uint8_t terrain = terrains[rng.Below(6)];
            const auto area = static_cast<std::uint16_t>(rng.Below(3));
            worlds.Both([&](CGameData* w) {
                w->tiles[x][y].object_number = object;
                w->tiles[x][y].terrain = terrain;
                w->tiles[x][y].area = area;
            });
        }
    }
    const std::uint8_t types[] = {1, 2, 3, 5, 14, 15, 16, 18, 24, 25, 30, 31, 32, 33, 36, 47, 48, 50, 51, 4, 7};
    for (std::uint32_t n = 0; n <= kTestBuildings + 1; ++n) {
        Building b;
        rng.Fill(&b, sizeof(b));
        b.owner = static_cast<std::uint8_t>(rng.Chance(1, 6) ? 0xff : rng.Below(4));
        b.art = types[rng.Below(static_cast<std::uint32_t>(sizeof(types)))];
        b.x = static_cast<std::uint16_t>(rng.Range(2, kTestMapSize - 3));
        b.y = static_cast<std::uint16_t>(rng.Range(2, kTestMapSize - 3));
        b.door_x = static_cast<std::uint16_t>(rng.Below(kTestMapSize));
        b.door_y = static_cast<std::uint16_t>(rng.Below(kTestMapSize));
        b.buildup = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : rng.Below(256));
        b.garrison = static_cast<std::uint16_t>(rng.Chance(1, 2) ? 0 : rng.Below(kTestSettlers));
        b.workshop.state = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : rng.Below(256));
        b.unk36 = static_cast<std::uint8_t>(rng.Below(5));
        worlds.Both([&](CGameData* w) { w->buildings[n] = b; });
    }
    for (std::uint32_t n = 0; n <= kTestSettlers + 1; ++n) {
        Settler s;
        rng.Fill(&s, sizeof(s));
        s.owner = static_cast<std::uint8_t>(rng.Chance(1, 6) ? 0xff : rng.Below(4));
        const std::uint8_t arts[] = {0, 1, 5, 8, 15, 32, 13, 36, 44, 44, 43, 22, 40};
        s.art = arts[rng.Below(static_cast<std::uint32_t>(sizeof(arts)))];
        s.x = static_cast<std::uint16_t>(rng.Range(10, kTestMapSize - 11));
        s.y = static_cast<std::uint16_t>(rng.Range(10, kTestMapSize - 11));
        const std::uint16_t jobs[] = {0x37, 0x35, 0xb0, 0, 1, 0x36};
        s.job = jobs[rng.Below(6)];
        s.next = static_cast<std::uint16_t>(rng.Below(kTestSettlers));
        s.passengers = static_cast<std::uint8_t>(rng.Below(8));
        worlds.Both([&](CGameData* w) { w->settlers[n] = s; });
    }
    for (std::uint32_t p = 0; p < layout::kPlayers; ++p) {
        const auto race = static_cast<std::int32_t>(rng.Below(4));
        const auto mana = static_cast<std::int32_t>(rng.Below(200));
        std::int32_t spellMana[8];
        for (auto& m : spellMana) {
            m = static_cast<std::int32_t>(rng.Below(220));
        }
        worlds.Both([&](CGameData* w) {
            w->players[p].race = race;
            w->players[p].mana = mana;
            std::memcpy(w->players[p].spell_mana, spellMana, sizeof(spellMana));
        });
        for (std::uint32_t g = 0; g < 10; ++g) {
            const auto length = static_cast<std::int32_t>(rng.Below(12));
            std::uint16_t members[100];
            for (auto& m : members) {
                m = static_cast<std::uint16_t>(rng.Below(kTestSettlers));
            }
            worlds.Both([&](CGameData* w) {
                w->player_list_lengths[p][g] = length;
                std::memcpy(w->player_lists[p][g], members, sizeof(members));
            });
        }
    }
    for (std::uint32_t a = 0; a < kTestAreas; ++a) {
        const auto player = static_cast<std::uint8_t>(rng.Chance(1, 5) ? 0xff : rng.Below(4));
        worlds.Both([&](CGameData* w) { w->areas[a].player = player; });
    }
    // The offset table (LoadShip): growing distances, small steps.
    for (std::uint32_t i = 0; i < 80; ++i) {
        const layout::MapOffset offset{rng.Range(-5, 5), rng.Range(-5, 5), static_cast<std::int32_t>(i / 4), 0};
        worlds.Both([&](CGameData* w) { w->offsets[i] = offset; });
    }
}

// The per-case UI state and globals.
void RandomizeUi(test::Context& ctx, Bench& bench) {
    test::Rng& rng = ctx.rng();
    const auto local = static_cast<std::int32_t>(rng.Below(4));
    const auto menu = static_cast<std::int32_t>(rng.Below(6));
    const auto page = static_cast<std::int32_t>(rng.Below(5));
    const auto page2 = static_cast<std::int32_t>(rng.Below(5));
    const auto param1 = static_cast<std::int32_t>(rng.Below(2));
    const auto param2 = static_cast<std::int32_t>(rng.Below(2));
    bench.worlds.Both([&](CGameData* w) {
        w->local_player = local;
        w->ui_selection_mode = menu;
        w->ui_2d290[0] = page;
        w->ui_2d2a0 = page2;
        w->ui_selection_param1 = param1;
        w->ui_selection_param2 = param2;
        w->refresh_ui = 0;
    });
    g_selectedBuilding = static_cast<std::int32_t>(rng.Below(kTestBuildings + 2));
    g_campaignMission = rng.Chance(1, 4) ? kMissionWithoutGarrison : static_cast<std::int32_t>(rng.Below(3));
    g_revealMap = 0;
}

// Random players' state that changes between cases (mana, promotions, requests).
void RandomizePlayers(test::Context& ctx, Bench& bench) {
    test::Rng& rng = ctx.rng();
    for (std::uint32_t p = 0; p < 4; ++p) {
        const auto manaTotal = static_cast<std::int32_t>(rng.Below(120));
        const auto cost = static_cast<std::int32_t>(rng.Chance(1, 8) ? -1 : static_cast<std::int32_t>(rng.Below(80)));
        const auto l1 = static_cast<std::int32_t>(rng.Below(3));
        const auto l2 = static_cast<std::int32_t>(rng.Below(3));
        const auto l3 = static_cast<std::int32_t>(rng.Below(3));
        const auto requests = static_cast<std::uint16_t>(rng.Chance(1, 2) ? 0 : rng.Below(5));
        bench.worlds.Both([&](CGameData* w) {
            Player& player = w->players[p];
            player.mana_total = manaTotal;
            player.promotion_cost = cost;
            player.soldier_levels[0] = l1;
            player.soldier_levels[1] = l2;
            player.soldier_levels[2] = l3;
            player.settler_lists[2] = requests;
        });
    }
}

std::uint8_t Hi(std::uint32_t v) {
    return static_cast<std::uint8_t>(v >> 8);
}
std::uint8_t Lo(std::uint32_t v) {
    return static_cast<std::uint8_t>(v);
}

// One random command of this file's set (or a message nothing handles).
void AddCommand(test::Context& ctx, MessageBuilder& m) {
    test::Rng& rng = ctx.rng();
    const auto player = static_cast<std::uint8_t>(rng.Chance(1, 8) ? rng.Below(20) : rng.Below(4));
    const std::uint32_t building = rng.Below(kTestBuildings + 1);
    const std::uint32_t x = rng.Below(kTestMapSize);
    const std::uint32_t y = rng.Below(kTestMapSize);
    const std::uint32_t settler = rng.Below(kTestSettlers + 1);
    const std::uint32_t area = rng.Below(kTestAreas);
    switch (rng.Below(19)) {
    case 0:
        m.Command(CommandId::PlaceBuilding, {Hi(x), Lo(x), Hi(y), Lo(y), 0, Lo(rng.Below(60)), player});
        break;
    case 1: {
        const std::uint32_t dx = rng.Next32();
        const std::uint32_t dy = rng.Next32();
        m.Command(CommandId::ScrollCamera,
                  {Lo(dx >> 24), Lo(dx >> 16), Hi(dx), Lo(dx), Lo(dy >> 24), Lo(dy >> 16), Hi(dy), Lo(dy)});
        break;
    }
    case 2: {
        const std::uint32_t count = rng.Chance(1, 5) ? 0 : rng.Below(30);
        std::vector<std::uint8_t> bytes = {Value(NetMessage::Command), Value(CommandId::SetSettlerGroup), 0, 0,
                                           player, Lo(rng.Below(player < 4 && rng.Chance(1, 4) ? 30 : 10)), Hi(count),
                                           Lo(count)};
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t s = rng.Below(kTestSettlers);
            bytes.push_back(Hi(s));
            bytes.push_back(Lo(s));
        }
        while (bytes.size() % 4 != 0) {
            bytes.push_back(Lo(rng.Next32()));
        }
        bytes[2] = static_cast<std::uint8_t>(bytes.size());
        m.Bytes(bytes);
        break;
    }
    case 3:
        m.Command(CommandId::SetBuildingWorking,
                  {Hi(building), Lo(building), Lo(rng.Chance(1, 2) ? 0 : rng.Below(3)), player});
        break;
    case 4:
        m.Command(CommandId::SetWorkArea, {Hi(x), Lo(x), Hi(y), Lo(y), Hi(building), Lo(building), player});
        break;
    case 5:
        m.Command(CommandId::ChangeGoodsPriority,
                  {Lo(rng.Below(rng.Chance(1, 8) ? 256 : 36)), Lo(rng.Below(2)), player, Hi(area), Lo(area)});
        break;
    case 6:
        m.Command(CommandId::SetGoodsSetting,
                  {Lo(rng.Below(rng.Chance(1, 8) ? 256 : 36)), Lo(rng.Next32()), player, Hi(area), Lo(area)});
        break;
    case 7:
        m.Command(CommandId::SetAreaCondition,
                  {Lo(rng.Below(rng.Chance(1, 8) ? 256 : 51)), Lo(rng.Below(3)), player, Hi(area), Lo(area)});
        break;
    case 8:
        m.Command(rng.Chance(1, 2) ? CommandId::SetShipyardDock : CommandId::SetHarbourDock,
                  {Hi(x), Lo(x), Hi(y), Lo(y), Hi(building), Lo(building), player});
        break;
    case 9:
        m.Command(CommandId::CastSpell,
                  {player, Lo(rng.Below(8)), Hi(x), Lo(x), Hi(y), Lo(y), Hi(settler), Lo(settler)});
        break;
    case 10:
        m.Command(CommandId::UnloadShip, {player, Hi(settler), Lo(settler)});
        break;
    case 11: {
        const std::uint8_t kinds[] = {1, 2, 3, 0, 4, 0x81, 0x82, 0x83, 0x80, 0xff};
        m.Command(CommandId::PromoteSoldiers, {player, kinds[rng.Below(10)]});
        break;
    }
    case 12:
        m.Command(CommandId::LoadShip, {player, Hi(settler), Lo(settler)});
        break;
    case 13:
        m.Command(CommandId::HoldPosition, {player, Lo(rng.Below(10))});
        break;
    case 14:
        m.Command(CommandId::SetStorehouseGoods, {Hi(building), Lo(building), player, Lo(rng.Below(40))});
        break;
    case 15:
        m.Command(CommandId::RevealMap, {});
        break;
    case 16:
        m.Command(CommandId::PrioritizeSite, {Hi(building), Lo(building), player});
        break;
    case 17:
        m.Command(CommandId::UnprioritizeSite, {Hi(building), Lo(building), player});
        break;
    default: {
        // Ids without a handler, and network message types without one (logged as invalid).
        const std::uint8_t idle[] = {0, 0x1a, 0x1d, 0x20, 0x80, 0xf0, 0xf3};
        if (rng.Chance(1, 2)) {
            m.Raw(Value(NetMessage::Command), idle[rng.Below(7)], {Lo(rng.Next32()), Lo(rng.Next32())});
        } else {
            const std::uint8_t types[] = {0, 7, 9, 0x10, 0x20, 0x21, 0x23, 0x80, 0xff};
            m.Raw(types[rng.Below(9)], Lo(rng.Next32()), {Lo(rng.Next32())});
        }
        break;
    }
    }
}

void CompareExecuteCommands(test::Context& ctx, ExecuteCommandsFn candidate, int cases) {
    SavedGlobals saved;
    Bench bench(ctx);
    SetUpWorld(ctx, bench);
    InstallWorldStubs(bench.stubs);
    test::Rng& rng = ctx.rng();
    for (int i = 0; i < cases; ++i) {
        RandomizeUi(ctx, bench);
        RandomizePlayers(ctx, bench);
        const std::uint32_t step = rng.Chance(1, 10) ? 0 : 1 + rng.Below(1000);
        bench.SetStep(step);
        MessageBuilder messages;
        const std::uint32_t count = rng.Below(5);
        for (std::uint32_t k = 0; k < count; ++k) {
            AddCommand(ctx, messages);
        }
        // Grants at, below and above the step.
        messages.Grant(step + static_cast<std::uint32_t>(rng.Range(-2, 6)));
        // Sometimes the queue's wait throws a CTimeout before the grant.
        const int throwAt = rng.Chance(1, 12) ? static_cast<int>(rng.Below(count + 1)) : -1;
        if (!bench.Run(ctx, i, messages, throwAt, candidate)) {
            return;
        }
    }
    bench.Finish(ctx);
}

// Wrong: places buildings without the garrison rule (as in campaign mission 66).
std::uint32_t __fastcall WrongExecuteCommands(CLogicThread* self, void* edx) {
    const std::int32_t mission = g_campaignMission;
    g_campaignMission = kMissionWithoutGarrison;
    const std::uint32_t result = CLogicThread_ExecuteCommands(self, edx);
    g_campaignMission = mission;
    return result;
}

}  // namespace

S3RE_TEST(CLogicThread_ExecuteCommands_MatchesOriginal) {
    CompareExecuteCommands(ctx, &CLogicThread_ExecuteCommands, 3000);
}
S3RE_TEST(CLogicThread_ExecuteCommands_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareExecuteCommands(probe, &WrongExecuteCommands, 600);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game::commands
