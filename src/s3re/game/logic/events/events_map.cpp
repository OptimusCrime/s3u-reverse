// CLogicThread::ProcessEvents (0x00524fa0), part: the map.
//
// MapClick (0x532fe4..0x533985), a left click outside the sidebar, by the
// open menu:
//   build menu with a building chosen: CheckBuildingSite(mode 0) on the
//     tile under the hit area and, if it can be built, the PlaceBuilding
//     command (g_placeOneBuilding then ends the placement);
//   building menu (4), by its page (ui_special_menu): 2 the work area
//     (SetWorkArea, within 60 tiles and in the door's area, for the
//     buildings that have one), 6..13 a trade route point (SetTradeRoute,
//     harbours/marketplaces), 16 / 17 the dock of a shipyard / harbour
//     (within 25 tiles);
//   settler groups (5) page 1 with a spell chosen: the priest casts it
//     (CastSpell) when he alone is selected, the mana suffices and the
//     tile is in range;
//   otherwise: the drag selection starts (g_dragSelecting, g_dragStartX/Y).
// LeftUp (0x5339bd..0x5342f7): the drag selection ends: the hit areas
//   (g_hitAreas, the read half) inside the box (a click: at least 20 x 37
//   pixels around the point) give the settlers of the new selection
//   (current group, or added to it with Shift): for a click every settler
//   kind, for a real box only the kind of the highest priority present
//   (soldiers, then 0x32/0x33, 0x0b, 0x15, 0x14, 0x0a); one settler with
//   Alt / Ctrl selects the settlers of its class (Alt: all soldiers)
//   around it; no settler but a building: the building menu (4) with its
//   page; nothing: the selection is cleared.
// RightUp (0x534337..0x5348f5): a right click (released within 15 command
//   steps, 50 with edge scrolling) closes the building placement or the
//   building menu and sends the selected group to the clicked tile (or
//   the minimap point): the group itself first if it changed
//   (SetSettlerGroup), then MoveUnits / Mode1 (Shift) / Mode2 (Ctrl: an
//   attack; with a unit of type 0x17 on an allied player's tile only a
//   message) / Mode3 (Alt).

#include "game/logic/events/process_events.h"

#include "game/enums/commands.h"
#include "game/globals.h"
#include "game/settlers/settler_groups.h"
#include "game/tables.h"
#include "game/texts.h"
#include "msvc6/string.h"
#include "msvc6/wstring.h"

#include <utility>

namespace s3re::game::events {

namespace {

using layout::Building;
using layout::Settler;
using msvc6::String;
using msvc6::WString;

constexpr std::int32_t kMenuBuild = 1;
constexpr std::int32_t kMenuBuildingInfo = 4;
constexpr std::int32_t kMenuGroups = 5;
// ui_special_menu pages of the building menu.
constexpr std::uint32_t kPageWorkArea = 2;
constexpr std::uint32_t kPageWorkAreaSet = 3;
constexpr std::uint32_t kPageTradeFirst = 6;
constexpr std::uint32_t kPageTradeLast = 13;
constexpr std::uint32_t kPageShipyardDock = 16;
constexpr std::uint32_t kPageShipyardDockSet = 15;
constexpr std::uint32_t kPageHarbourDock = 17;
constexpr std::uint32_t kPageHarbour = 5;
constexpr std::uint32_t kPageMarketplace = 4;
constexpr std::uint32_t kPageWorkshop = 14;
constexpr std::uint32_t kPageShipyard = 15;
constexpr std::uint32_t kPageWoodcutter = 18;
constexpr std::int32_t kPageGroupSpells = 1;  // ui_special_menu of menu 5

// PushUiFeedback values.
constexpr std::uint32_t kFeedbackBuildingPlaced = 0x2e;
constexpr std::uint32_t kFeedbackWorkArea = 0x5f;
constexpr std::uint32_t kFeedbackTooFar = 0x74;
constexpr std::uint32_t kFeedbackOrder = 0x34;
constexpr std::uint32_t kFeedbackFewUnits = 0x39;   // up to 5 settlers sent
constexpr std::uint32_t kFeedbackSomeUnits = 0x3a;  // up to 20
constexpr std::uint32_t kFeedbackManyUnits = 0x3b;

constexpr std::uint32_t kWorkAreaRange = 60;
constexpr std::uint32_t kDockRange = 25;
constexpr std::int32_t kWorkAreaSearch = 5;   // MapOffset distances searched for a tile with an area
constexpr std::uint32_t kSearchOffsets = 3000;  // MapOffsets around a settler (48000 bytes)
constexpr std::uint8_t kNoOwner = 0xff;
constexpr std::uint16_t kMaxGroupSettlers = 100;
constexpr std::uint16_t kHitBuilding = 100;   // HitArea::kind of a building
constexpr std::uint8_t kCommandSize = 12;
constexpr std::int32_t kRightClickSteps = 15;
constexpr std::int32_t kRightClickStepsEdgeScroll = 50;
constexpr std::int32_t kScrollAtEdges = 1;
constexpr std::uint8_t kSettlerType23 = 0x17;  // a unit that can't attack allies (the message 0x80174)
constexpr std::uint8_t kSettlerPriest = 0x24;  // casts the spells
constexpr std::uint32_t kTextAlliedTile = Tooltip(0x174);  // text 0x80174 (group 8)
constexpr std::uint32_t kCurrentLanguage = 0xffffffff;

// Callees by address.
constexpr std::uintptr_t kSprintf = 0x0061f27e;
constexpr std::uintptr_t kDebugLog = 0x00428720;
constexpr std::uintptr_t kStringFromCstr = 0x00401a10;
constexpr std::uintptr_t kStringDestroy = 0x00401b60;
constexpr std::uintptr_t kNarrowToWide = 0x0060b840;  // (ECX = out wstring, EDX = string)

Building& BuildingAt(CGameData* world, std::int32_t n) {
    return *(world->buildings + n);
}
Settler& SettlerAt(CGameData* world, std::uint32_t n) {
    return *(world->settlers + n);
}

// The command bytes: the message header, then the payload from byte 4
// (big-endian words, as the network sends them).
void Header(Run& run, CommandId id) {
    std::uint8_t* command = run.frame->command;
    command[0] = static_cast<std::uint8_t>(NetMessage::CommandRequest);
    command[1] = static_cast<std::uint8_t>(id);
    command[2] = kCommandSize;
}
void Word(Run& run, std::size_t offset, std::uint32_t value) {
    run.frame->command[offset] = static_cast<std::uint8_t>(value >> 8);
    run.frame->command[offset + 1] = static_cast<std::uint8_t>(value);
}
void Byte(Run& run, std::size_t offset, std::uint32_t value) {
    run.frame->command[offset] = static_cast<std::uint8_t>(value);
}
// The 3-dword command, unless the game is lost (g_gameOutcome 0).
void PushIfPlaying(Run& run) {
    if (g_gameOutcome != 0) {
        PushCommand(run, 3, 1);
    }
}

std::uint16_t TileArea(CGameData* world, std::uint32_t x, std::uint32_t y) {
    return world->tiles[x][y].area;
}

// 0x533008: the build menu's building on the clicked tile.
void PlaceBuilding(Run& run) {
    CGameData* world = run.world();
    Call<callee::kPickHitArea>(world, run.thread->mouse_x, run.thread->mouse_y);
    world = run.world();
    if (Call<callee::kCheckBuildingSite>(world, world->last_search_x, world->last_search_y, world->ui_cursor_mode,
                                         world->local_player, 0) == 0) {
        return;
    }
    Feedback(run, kFeedbackBuildingPlaced);
    Header(run, CommandId::PlaceBuilding);
    Word(run, 4, static_cast<std::uint32_t>(run.world()->last_search_x));
    Word(run, 6, static_cast<std::uint32_t>(run.world()->last_search_y));
    Word(run, 8, static_cast<std::uint32_t>(run.world()->ui_cursor_mode));
    Byte(run, 10, static_cast<std::uint32_t>(run.world()->local_player));
    PushIfPlaying(run);
    if (g_placeOneBuilding != 0) {
        run.world()->ui_selection_param2 = 0;
        RefreshUi(run);
    }
}

// The selected building of the building menu, if it is the local player's.
bool OwnSelectedBuilding(Run& run) {
    CGameData* world = run.world();
    return BuildingAt(world, g_selectedBuilding).owner == static_cast<std::uint32_t>(world->local_player);
}

std::uint32_t DistanceToSelectedBuilding(Run& run) {
    CGameData* world = run.world();
    const Building& building = BuildingAt(world, g_selectedBuilding);
    return CallStd<callee::kTileDistance>(world->last_search_x, world->last_search_y, building.x, building.y);
}

// 0x53314d: the work area of the selected building.
void SetWorkArea(Run& run) {
    CLogicThread* thread = run.thread;
    Call<callee::kTileAtScreenPoint>(run.world(), thread->mouse_x, thread->mouse_y);
    if (!OwnSelectedBuilding(run)) {
        return;
    }
    // 0x533183: the nearest tile with an area within distance 5 of the
    // clicked one (MapOffset order). If the table's first offset is
    // farther, the frame's last candidate is used again (from an earlier
    // event of the call; 0 at first: the original's stack leftover).
    CGameData* world = run.world();
    FrameLocals& frame = *run.frame;
    if (world->offsets[0].distance <= kWorkAreaSearch) {
        for (std::size_t k = 0;; ++k) {
            const layout::MapOffset& offset = world->offsets[k];
            frame.work_area_x = static_cast<std::uint32_t>(offset.dx) + static_cast<std::uint32_t>(world->last_search_x);
            frame.work_area_y = static_cast<std::uint32_t>(offset.dy) + static_cast<std::uint32_t>(world->last_search_y);
            if (frame.work_area_x < world->map_size && frame.work_area_y < world->map_size &&
                TileArea(world, frame.work_area_x, frame.work_area_y) != 0) {
                break;
            }
            if (world->offsets[k + 1].distance > kWorkAreaSearch) {
                break;
            }
        }
    }
    world->last_search_x = static_cast<std::int32_t>(frame.work_area_x);
    run.world()->last_search_y = static_cast<std::int32_t>(frame.work_area_y);
    if (DistanceToSelectedBuilding(run) >= kWorkAreaRange) {
        Feedback(run, kFeedbackTooFar);
        return;
    }
    world = run.world();
    const Building& building = BuildingAt(world, g_selectedBuilding);
    if (TileArea(world, building.door_x, building.door_y) !=
        TileArea(world, static_cast<std::uint32_t>(world->last_search_x), static_cast<std::uint32_t>(world->last_search_y))) {
        return;
    }
    // 0x5332b9: the buildings with a work area.
    switch (building.art) {
    case 2: case 0x1e: case 0x0e: case 0x19: case 0x24: case 0x18: case 5: case 3: case 0x33: case 0x34: break;
    default: return;
    }
    Feedback(run, kFeedbackWorkArea);
    run.world()->ui_special_menu = static_cast<std::int32_t>(kPageWorkAreaSet);
    Header(run, CommandId::SetWorkArea);
    Word(run, 4, static_cast<std::uint32_t>(run.world()->last_search_x));
    Word(run, 6, static_cast<std::uint32_t>(run.world()->last_search_y));
    Word(run, 8, static_cast<std::uint32_t>(g_selectedBuilding.get()));
    Byte(run, 10, static_cast<std::uint32_t>(run.world()->local_player));
    PushIfPlaying(run);
    RefreshUi(run);
}

// 0x5333e6: a route point of a harbour (0x22) or marketplace (0x21).
void SetTradeRoutePoint(Run& run) {
    CLogicThread* thread = run.thread;
    Call<callee::kTileAtScreenPoint>(run.world(), thread->mouse_x, thread->mouse_y);
    CGameData* world = run.world();
    const Building& building = BuildingAt(world, g_selectedBuilding);
    if (building.owner != static_cast<std::uint32_t>(world->local_player) || (building.art != 0x22 && building.art != 0x21)) {
        return;
    }
    Feedback(run, kFeedbackOrder);
    Header(run, CommandId::SetTradeRoute);
    Byte(run, 4, static_cast<std::uint32_t>(run.world()->local_player));
    // The route point 0..3 of the page pair (6/7 -> 4, ..., 12/13 -> 7).
    Byte(run, 5, (((static_cast<std::uint32_t>(run.world()->ui_special_menu) - kPageTradeFirst) >> 1) & 0x7f) + 4);
    Word(run, 6, static_cast<std::uint32_t>(g_selectedBuilding.get()));
    Word(run, 8, static_cast<std::uint32_t>(run.world()->last_search_x));
    Word(run, 10, static_cast<std::uint32_t>(run.world()->last_search_y));
    PushIfPlaying(run);
    world = run.world();
    world->ui_special_menu = (world->ui_special_menu & 1) + static_cast<std::int32_t>(kPageMarketplace);
    RefreshUi(run);
}

// 0x533523 / 0x533683: the dock of a shipyard (0x20) or harbour (0x21).
void SetDock(Run& run, std::uint8_t type, std::uint32_t page, CommandId id) {
    CLogicThread* thread = run.thread;
    Call<callee::kTileAtScreenPoint>(run.world(), thread->mouse_x, thread->mouse_y);
    if (!OwnSelectedBuilding(run) || DistanceToSelectedBuilding(run) >= kDockRange ||
        BuildingAt(run.world(), g_selectedBuilding).art != type) {
        return;
    }
    Feedback(run, kFeedbackOrder);
    run.world()->ui_special_menu = static_cast<std::int32_t>(page);
    Header(run, id);
    Word(run, 4, static_cast<std::uint32_t>(run.world()->last_search_x));
    Word(run, 6, static_cast<std::uint32_t>(run.world()->last_search_y));
    Word(run, 8, static_cast<std::uint32_t>(g_selectedBuilding.get()));
    Byte(run, 10, static_cast<std::uint32_t>(run.world()->local_player));
    PushIfPlaying(run);
    RefreshUi(run);
}

// 0x5337f4: the priest of a one-settler group casts the chosen spell.
void CastSpell(Run& run) {
    CLogicThread* thread = run.thread;
    Call<callee::kTileAtScreenPoint>(run.world(), thread->mouse_x, thread->mouse_y);
    if (GroupCount(g_currentSettlerGroup) != 1) {
        return;
    }
    const std::uint16_t priest = GroupEntry(g_currentSettlerGroup, 0);
    CGameData* world = run.world();
    const Settler& settler = SettlerAt(world, priest);
    if (settler.owner != static_cast<std::uint32_t>(world->local_player) || settler.art != kSettlerPriest) {
        return;
    }
    const std::uint32_t spell = static_cast<std::uint32_t>(world->ui_2d29c) - 1;
    const layout::Player& player = world->players[world->local_player];
    const std::uint32_t number = g_raceSpells[player.race][spell];
    if (static_cast<std::uint32_t>(player.mana) < static_cast<std::uint32_t>(player.spell_mana[spell])) {
        return;
    }
    if (CallStd<callee::kTileDistance>(world->last_search_x, world->last_search_y, settler.x, settler.y) >=
        g_spellCastRange[number]) {
        return;
    }
    using LogFn = void(__cdecl*)(const char*, ...);
    reinterpret_cast<LogFn>(kDebugLog)(texts::kSendingGodlyEvent, spell);
    Feedback(run, kFeedbackOrder);
    run.world()->ui_2d29c = 0;
    Header(run, CommandId::CastSpell);
    Byte(run, 4, static_cast<std::uint32_t>(run.world()->local_player));
    Byte(run, 5, spell);
    Word(run, 6, static_cast<std::uint32_t>(run.world()->last_search_x));
    Word(run, 8, static_cast<std::uint32_t>(run.world()->last_search_y));
    Word(run, 10, priest);
    PushIfPlaying(run);
    RefreshUi(run);
}

}  // namespace

void MapClick(Run& run) {
    CLogicThread* thread = run.thread;
    CGameData* world = run.world();
    const std::int32_t menu = world->ui_selection_mode;
    if (menu == kMenuBuild && world->ui_selection_param1 != 0 && world->ui_selection_param2 != 0) {
        PlaceBuilding(run);
        return;
    }
    const auto page = static_cast<std::uint32_t>(world->ui_special_menu);
    if (menu == kMenuBuildingInfo) {
        if (page == kPageWorkArea) {
            SetWorkArea(run);
            return;
        }
        if (page >= kPageTradeFirst && page <= kPageTradeLast) {
            SetTradeRoutePoint(run);
            return;
        }
        if (page == kPageShipyardDock) {
            SetDock(run, 0x20, kPageShipyardDockSet, CommandId::SetShipyardDock);
            return;
        }
        if (page == kPageHarbourDock) {
            SetDock(run, 0x21, kPageHarbour, CommandId::SetHarbourDock);
            return;
        }
    }
    if (menu == kMenuGroups && world->ui_special_menu == kPageGroupSpells && world->ui_2d29c != 0) {
        CastSpell(run);
        return;
    }
    // 0x533973: the drag selection starts here.
    g_dragSelecting = 1;
    g_dragStartX = thread->mouse_x;
    g_dragStartY = thread->mouse_y;
}

namespace {

// The selection box of LeftUp, in screen pixels.
struct Box {
    std::int32_t left, right, top, bottom;
    bool Contains(const HitArea& area) const {
        return area.x >= left && area.x <= right && area.y >= top && area.y <= bottom;
    }
};

// The kind priorities of a dragged box (0x533ce9): lower wins.
std::uint32_t KindPriority(std::uint16_t kind, std::uint32_t best) {
    std::uint32_t priority;
    if ((kind >= 1 && kind <= 9) || kind == 0x28) {
        priority = 1;  // soldiers
    } else if (kind >= 0x32 && kind <= 0x33) {
        priority = 2;
    } else if (kind == 0x0b) {
        priority = 4;
    } else if (kind == 0x15) {
        priority = 5;
    } else if (kind == 0x14) {
        priority = 6;
    } else if (kind == 0x0a) {
        priority = 7;
    } else {
        return best;
    }
    return priority < best ? priority : best;
}

bool KindMatches(std::uint16_t kind, std::uint32_t priority) {
    if (priority == 1 && kind >= 1 && kind <= 9) {
        return true;
    }
    if (kind == 0x28) {
        return true;
    }
    switch (priority) {
    case 2: return kind >= 0x32 && kind <= 0x33;
    case 4: return kind == 0x0b;
    case 5: return kind == 0x15;
    case 6: return kind == 0x14;
    case 7: return kind == 0x0a;
    default: return false;
    }
}

// The selection being built: the group number (re-read from
// g_currentSettlerGroup after every RemoveSettlerFromGroups, as the
// original does) and the count.
struct Selection {
    std::int32_t group;
    std::uint32_t count;

    bool Has(std::uint16_t settler) const {
        bool found = false;
        for (std::uint32_t i = 0; i < count; ++i) {
            if (GroupEntry(group, static_cast<std::int32_t>(i)) == settler) {
                found = true;
            }
        }
        return found;
    }
    void Add(Run& run, std::uint16_t settler) {
        GroupEntry(group, static_cast<std::int32_t>(count)) = settler;
        CallThread<callee::kRemoveSettlerFromGroups>(run.thread, settler, group);
        group = g_currentSettlerGroup;
        ++count;
    }
};

const HitArea& HitAreaAt(std::int32_t k) {
    return g_hitAreas[g_hitAreaReadHalf + 2 * k];
}

// 0x533c3d: the first building in the box becomes the selected building.
void NoteBuilding(std::int32_t k) {
    if (g_selectedBuilding == 0 && HitAreaAt(k).kind == kHitBuilding) {
        g_selectedBuilding = HitAreaAt(k).value1;
    }
}

// 0x533b3a: a click: every settler kind (only the first of kinds 1..29,
// then any 0x28 / 0x32 / 0x33).
void SelectClicked(Run& run, Selection& selection, const Box& box) {
    bool added = false;
    for (std::int32_t k = 0; k < g_hitAreaReadCount; ++k) {
        const HitArea& area = HitAreaAt(k);
        if (!box.Contains(area)) {
            continue;
        }
        const std::uint16_t kind = area.kind;
        if (selection.count != kMaxGroupSettlers &&
            ((!added && kind >= 1 && kind <= 0x1d) || kind == 0x28 || kind == 0x32 || kind == 0x33)) {
            if (selection.count == 0 || !selection.Has(area.value1)) {
                selection.Add(run, area.value1);
                added = true;
            }
        }
        NoteBuilding(k);
    }
}

// 0x533c8c: a dragged box: the kind of the highest priority.
void SelectBoxed(Run& run, Selection& selection, const Box& box) {
    std::uint32_t priority = 0xffff;
    for (std::int32_t k = 0; k < g_hitAreaReadCount; ++k) {
        if (box.Contains(HitAreaAt(k))) {
            priority = KindPriority(HitAreaAt(k).kind, priority);
        }
    }
    for (std::int32_t k = 0; k < g_hitAreaReadCount; ++k) {
        const HitArea& area = HitAreaAt(k);
        if (!box.Contains(area)) {
            continue;
        }
        if (selection.count != kMaxGroupSettlers && KindMatches(area.kind, priority)) {
            if (selection.count == 0 || !selection.Has(area.value1)) {
                selection.Add(run, area.value1);
            }
        }
        NoteBuilding(k);
    }
}

// 0x533f42: Alt / Ctrl with one settler: the settlers of its class
// (Alt with a soldier: every soldier class 1..9) around it.
void SelectSameClass(Run& run, Selection& selection, bool alt) {
    CGameData* world = run.world();
    const Settler& first = SettlerAt(world, GroupEntry(selection.group, 0));
    const std::int32_t firstClass = g_settlerTypeClass[first.art];
    std::uint32_t low;
    std::uint32_t high;
    if (alt && firstClass <= 9) {
        low = 1;
        high = 9;
    } else {
        low = static_cast<std::uint32_t>(firstClass);
        high = low;
    }
    const std::uint32_t originX = first.x;
    const std::uint32_t originY = first.y;
    for (std::uint32_t k = 0; k < kSearchOffsets; ++k) {
        world = run.world();
        const std::uint32_t x = static_cast<std::uint32_t>(world->offsets[k].dx) + originX;
        const std::uint32_t y = static_cast<std::uint32_t>(world->offsets[k].dy) + originY;
        const std::uint32_t last = world->map_size - 1;
        if (x > last || y > last) {
            continue;
        }
        const std::uint16_t number = world->tiles[x][y].settler;
        if (number == 0) {
            continue;
        }
        const Settler& settler = SettlerAt(world, number);
        if (settler.owner != static_cast<std::uint32_t>(world->local_player)) {
            continue;
        }
        const auto cls = static_cast<std::uint32_t>(g_settlerTypeClass[settler.art]);
        if (cls < low || cls > high) {
            continue;
        }
        const bool found = selection.count != 0 && selection.Has(number);
        if (selection.count != kMaxGroupSettlers && !found) {
            selection.Add(run, number);
        }
    }
}

// 0x534169: no settler selected: the building menu of a building in the
// box, or nothing.
void SelectBuildingOrNothing(Run& run) {
    CLogicThread* thread = run.thread;
    if (g_selectedBuilding == 0) {
        CallThread<callee::kClearSelection>(thread);
        thread->group_selection_changed = 0;
        run.world()->ui_selection_mode = 0;
        RefreshUi(run);
        return;
    }
    g_currentSettlerGroup = -1;
    thread->group_selection_changed = 0;
    run.world()->ui_selection_mode = kMenuBuildingInfo;
    CGameData* world = run.world();
    const Building& building = BuildingAt(world, g_selectedBuilding);
    const bool own = building.buildup == 0 &&
                     building.owner == static_cast<std::uint32_t>(world->local_player);
    const std::uint8_t type = building.art;
    if (type == 0x22 && own) {
        world->ui_special_menu = static_cast<std::int32_t>(kPageMarketplace);
        CallThread<callee::kDrawBuildingRouteOverlay>(thread, g_selectedBuilding.get());
    } else if (type == 0x21 && own) {
        world->ui_special_menu = static_cast<std::int32_t>(kPageHarbour);
        CallThread<callee::kDrawBuildingRouteOverlay>(thread, g_selectedBuilding.get());
    } else if ((type == 0x1f || type == 0x2f || type == 0x30 || type == 0x32) && own) {
        world->ui_special_menu = static_cast<std::int32_t>(kPageWorkshop);
    } else if (type == 0x20 && own) {
        world->ui_special_menu = static_cast<std::int32_t>(kPageShipyard);
    } else if (type == 1 && own) {
        world->ui_special_menu = static_cast<std::int32_t>(kPageWoodcutter);
    } else {
        world->ui_special_menu = 0;
    }
    RefreshUi(run);
}

}  // namespace

void LeftUp(Run& run) {
    CLogicThread* thread = run.thread;
    CallThread<callee::kClearPendingUiRefresh>(thread);
    if (g_dragSelecting == 0) {
        return;
    }
    g_dragSelecting = 0;
    // 0x5339d1: the box between the drag start and the last mouse position.
    Box box{g_dragStartX, g_lastMouseX, g_dragStartY, g_lastMouseY};
    if (box.right < box.left) {
        std::swap(box.left, box.right);
    }
    if (box.bottom < box.top) {
        std::swap(box.top, box.bottom);
    }
    const bool click = box.right - box.left < 5 && box.bottom - box.top < 5;
    if (box.right - box.left < 20) {
        const std::int32_t middle = (box.right + box.left) / 2;
        box.left = middle >= g_sidebarWidth + 10 ? middle - 10 : g_sidebarWidth.get();
        box.right = middle < g_displayWidth + 10 ? middle + 10 : g_displayWidth - 1;
    }
    if (box.bottom - box.top < 37) {
        const std::int32_t middle = (box.bottom + box.top) / 2;
        box.top = middle >= 5 ? middle - 5 : 0;
        box.bottom = middle < g_displayHeight + 32 ? middle + 32 : g_displayHeight - 1;
    }
    // 0x533ad9: Shift adds to the current group.
    const std::uint8_t modifiers = run.event().modifiers;
    g_selectedBuilding = 0;
    Selection selection{};
    if ((modifiers & kModShift) != 0 && g_currentSettlerGroup != -1) {
        selection.group = g_currentSettlerGroup;
        selection.count = GroupCount(selection.group);
    } else {
        selection.group = 0;
        selection.count = 0;
        g_currentSettlerGroup = 0;
    }
    if (g_hitAreaReadCount != 0) {
        if (click) {
            SelectClicked(run, selection, box);
        } else {
            SelectBoxed(run, selection, box);
        }
    }
    if (selection.count == 1 && (modifiers & (kModAlt | kModCtrl)) != 0) {
        SelectSameClass(run, selection, (modifiers & kModAlt) != 0);
    }
    if (selection.count == 0) {
        SelectBuildingOrNothing(run);
        return;
    }
    // 0x53410a: the selection becomes the group.
    GroupCount(selection.group) = static_cast<std::uint16_t>(selection.count);
    g_settlerGroupChanged[selection.group] = 1;
    thread->group_selection_changed = 1;
    g_selectedBuilding = 0;
    run.world()->ui_selection_mode = kMenuGroups;
    run.world()->ui_special_menu = 0;
    RefreshUi(run);
    CallThread<callee::kUpdateGroupMenu>(thread, g_currentSettlerGroup.get());
}

namespace {

// 0x53442a: the clicked minimap point as the target tile (last_search_x/y).
void MinimapTarget(Run& run) {
    CLogicThread* thread = run.thread;
    CGameData* world = run.world();
    const std::int32_t size = static_cast<std::int32_t>(world->map_size);
    const std::int32_t y = thread->mouse_y - g_layout730f30;
    const std::int32_t width = g_minimapWidth;
    std::int32_t tileX = ((y / 2 - width / 2 - g_layout730f2c + thread->mouse_x) * size) / width;
    std::int32_t tileY = (y * size) / width;
    const std::int32_t last = size - 1;
    tileX = tileX < 0 ? 0 : tileX;
    tileX = tileX > last ? last : tileX;
    tileY = tileY < 0 ? 0 : tileY;
    tileY = tileY > last ? last : tileY;
    world->last_search_x = tileX;
    run.world()->last_search_y = tileY;
}

// 0x534539: the changed group goes to the network first (SetSettlerGroup:
// player, group, count, the settler numbers).
void SendGroup(Run& run, std::int32_t group) {
    const std::uint16_t count = GroupCount(group);
    std::uint8_t* command = run.frame->command;
    command[0] = static_cast<std::uint8_t>(NetMessage::CommandRequest);
    command[1] = static_cast<std::uint8_t>(CommandId::SetSettlerGroup);
    command[2] = static_cast<std::uint8_t>((((count + 1) / 2) + 2) << 2);
    command[4] = static_cast<std::uint8_t>(run.world()->local_player);
    command[5] = static_cast<std::uint8_t>(group);
    command[6] = static_cast<std::uint8_t>(count >> 8);
    command[7] = static_cast<std::uint8_t>(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint16_t settler = GroupEntry(group, static_cast<std::int32_t>(i));
        command[8 + 2 * i] = static_cast<std::uint8_t>(settler >> 8);
        command[9 + 2 * i] = static_cast<std::uint8_t>(settler);
    }
    if (g_gameOutcome != 0) {
        PushCommand(run, command[2] >> 2, 1);
        group = g_currentSettlerGroup;
    }
    g_settlerGroupChanged[group] = 0;
}

// 0x534819: MoveUnits and its modes: player, group, x, y.
void SendMove(Run& run, CommandId id, std::int32_t group, std::int32_t outcome) {
    Header(run, id);
    Byte(run, 4, static_cast<std::uint32_t>(run.world()->local_player));
    Byte(run, 5, static_cast<std::uint32_t>(group));
    Word(run, 6, static_cast<std::uint32_t>(run.world()->last_search_x));
    Word(run, 8, static_cast<std::uint32_t>(run.world()->last_search_y));
    if (outcome != 0) {
        PushCommand(run, 3, 1);
    }
}

// 0x5346f4: a unit of type 0x17 can't attack an allied player: the message.
void ShowAlliedMessage(Run& run) {
    using TextFn = const char*(__fastcall*)(void*, void*, std::uint32_t, std::uint32_t);
    const char* text = reinterpret_cast<TextFn>(callee::kTextAddress)(g_gfxData.get(), nullptr, kTextAlliedTile,
                                                                      kCurrentLanguage);
    char* buffer = run.frame->text;
    using SprintfFn = int(__cdecl*)(char*, const char*, ...);
    reinterpret_cast<SprintfFn>(kSprintf)(buffer, text);
    String message;
    CallOn<kStringFromCstr>(&message, U(buffer), 0xffffffff);
    WString wide;
    using WidenFn = WString*(__fastcall*)(WString* out, const String* text);
    reinterpret_cast<WidenFn>(kNarrowToWide)(&wide, &message);
    ShowWideMessage(run, wide);
    CallOn<kStringDestroy>(&message);
}

// 0x534613: Ctrl: the attack, unless the target tile belongs to an ally
// and the group has a unit of type 0x17.
void SendAttack(Run& run, std::int32_t group) {
    CGameData* world = run.world();
    const std::uint8_t owner =
        world->tiles[static_cast<std::uint32_t>(world->last_search_x)][static_cast<std::uint32_t>(world->last_search_y)].owner;
    run.frame->target_owner = owner;
    if (owner != kNoOwner && owner != static_cast<std::uint32_t>(world->local_player) && GroupCount(group) != 0) {
        bool restricted = false;
        const std::int32_t count = GroupCount(group);
        for (std::int32_t i = 0; i < count && !restricted; ++i) {
            if (SettlerAt(world, GroupEntry(group, i)).art == kSettlerType23) {
                restricted = true;
            }
        }
        if (restricted && world->players[owner].team == world->players[world->local_player].team) {
            ShowAlliedMessage(run);
            return;
        }
    }
    SendMove(run, CommandId::MoveUnitsMode2, group, g_gameOutcome);
}

}  // namespace

void RightUp(Run& run) {
    CLogicThread* thread = run.thread;
    CallThread<callee::kClearPendingUiRefresh>(thread);
    thread->group_selection_changed =
        g_currentSettlerGroup != -1 && GroupCount(g_currentSettlerGroup) != 0 ? 1 : 0;
    // 0x534369: only a short click counts.
    CGameData* world = run.world();
    const std::int32_t held = world->command_attempts - g_rightDownTime;
    if (held >= kRightClickSteps && !(g_scrollMode == kScrollAtEdges && held < kRightClickStepsEdgeScroll)) {
        return;
    }
    if (world->ui_selection_mode == kMenuBuild && world->ui_selection_param1 != 0 && world->ui_selection_param2 != 0) {
        world->ui_selection_param2 = 0;  // ends the building placement
        RefreshUi(run);
    }
    if (run.world()->ui_selection_mode == kMenuBuildingInfo) {
        g_selectedBuilding = 0;
        run.world()->ui_selection_mode = 0;
        RefreshUi(run);
    }
    if (g_currentSettlerGroup == -1) {
        return;
    }
    const std::uint16_t count = GroupCount(g_currentSettlerGroup);
    if (count == 0) {
        return;
    }
    Feedback(run, count <= 5 ? kFeedbackFewUnits : count <= 20 ? kFeedbackSomeUnits : kFeedbackManyUnits);
    // 0x53442a: the target: the minimap point or the tile under the mouse.
    if (thread->mouse_y < g_layout730f34 &&
        thread->mouse_x < (g_layout730f34 - thread->mouse_y) / 2 + g_sidebarWidth) {
        MinimapTarget(run);
    } else {
        thread->mouse_x = run.event().x;
        thread->mouse_y = run.event().y;
        Call<callee::kTileAtScreenPoint>(run.world(), thread->mouse_x, thread->mouse_y);
    }
    world = run.world();
    if (world->last_search_x == 0 || world->last_search_y == 0) {
        return;
    }
    std::int32_t group = g_currentSettlerGroup;
    if (g_settlerGroupChanged[group] != 0) {
        SendGroup(run, group);
        group = g_currentSettlerGroup;
    }
    // 0x5345eb: the move by the modifiers (g_gameOutcome read once here,
    // except for the attack, which reads it again).
    const std::uint8_t modifiers = run.event().modifiers;
    const std::int32_t outcome = g_gameOutcome;
    if ((modifiers & kModShift) != 0) {
        SendMove(run, CommandId::MoveUnitsMode1, group, outcome);
    } else if ((modifiers & kModCtrl) != 0) {
        SendAttack(run, group);
    } else if ((modifiers & kModAlt) != 0) {
        SendMove(run, CommandId::MoveUnitsMode3, group, outcome);
    } else {
        SendMove(run, CommandId::MoveUnits, group, outcome);
    }
}

}  // namespace s3re::game::events
