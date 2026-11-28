// CLogicThread::ProcessEvents (0x00524fa0), part: a left click or the
// hover event (0x525866..0x5339a1).
//
// The click position is the thread's mouse position (+0x44/+0x48, set by
// the MouseMove events). Above the minimap's bottom (g_layout730f34) and
// left of its diagonal edge: the minimap buttons (left of the minimap) or a
// jump to the clicked minimap point; elsewhere in the sidebar
// (x < g_sidebarWidth): the panel, in cells of g_uiCellSize pixels from the
// draw origin (40 columns, 106 rows): the three menu tabs at the top, the
// five buttons below the menus, then the open menu's buttons (one handler
// per menu, events_*.cpp). Outside the sidebar: the tutorial's "continue"
// area or a click on the map (events_map.cpp). Every button acts on a
// real click (hover_state 0) and sets its tooltip on the hover event.
// At the end, a hover event that found a tooltip asks for a UI refresh.

#include "game/logic/events/process_events.h"

#include "game/globals.h"

namespace s3re::game::events {

namespace {

constexpr std::int32_t kPanelColumns = 40;   // 0..39
constexpr std::int32_t kPanelRows = 106;     // 0..105
constexpr std::int32_t kMinimapButtonColumns = 20;
constexpr std::int32_t kMinimapButtonRows = 40;
constexpr std::uint32_t kNoTooltip = 0xffffffff;
constexpr std::int32_t kTutorialMission = 99;
// The tutorial's "continue" area: the bottom left corner of the view.
constexpr std::int32_t kTutorialAreaWidth = 50;
constexpr std::int32_t kTutorialAreaHeight = 210;

// The menus (world ui_selection_mode), as the tabs and buttons open them.
constexpr std::int32_t kMenuBuild = 1;
constexpr std::int32_t kMenuStatistics = 2;
constexpr std::int32_t kMenuSettlers = 3;
constexpr std::int32_t kMenuPlayers = 6;
constexpr std::int32_t kMenuSaveLoad = 7;
constexpr std::int32_t kMenuMessages = 8;
constexpr std::int32_t kMinimapModes = 3;   // g_minimapMode, g_minimapShowSettlers cycle 0..2

bool In(std::int32_t value, std::int32_t low, std::int32_t high) {
    return value >= low && value <= high;
}

// The button acts (feedback first) on a real click; on the hover event its
// tooltip is set instead. Returns true when the caller should act.
bool Press(Run& run, std::uint32_t tooltip) {
    if (!run.Acting()) {
        run.thread->tooltip = tooltip;
        return false;
    }
    Feedback(run);
    return true;
}

}  // namespace

// The minimap point under the mouse as a map tile, clamped to the map
// (0x5253c9 and 0x526a1e).
void CenterCameraOnMinimapPoint(Run& run) {
    CLogicThread* thread = run.thread;
    CGameData* world = run.world();
    const std::int32_t size = static_cast<std::int32_t>(world->map_size);
    const std::int32_t y = thread->mouse_y - g_layout730f30;
    const std::int32_t width = g_minimapWidth;
    std::int32_t tileX = ((y / 2 - width / 2 - g_layout730f2c + thread->mouse_x) * size) / width;
    std::int32_t tileY = (size * y) / width;
    const std::int32_t last = size - 1;
    if (tileX < 0) {
        tileX = 0;
    }
    if (tileX > last) {
        tileX = last;
    }
    if (tileY < 0) {
        tileY = 0;
    }
    if (tileY > last) {
        tileY = last;
    }
    Call<callee::kCenterCameraOnTile>(world, tileX, tileY);
    world->redraw = 0;
}

namespace {

// 0x5258e5..0x5269f8: the buttons left of the minimap.
void MinimapButtons(Run& run) {
    CLogicThread* thread = run.thread;
    const std::int32_t cell = g_uiCellSize;
    const std::int32_t column = (thread->mouse_x - g_layout730f2c) / cell;
    const std::int32_t row = (thread->mouse_y - g_layout730f30) / cell;
    if (column < 0 || column >= kMinimapButtonColumns || row < 0 || row >= kMinimapButtonRows) {
        return;
    }
    if (column <= 7 && row <= 7) {  // 0x525925: the players menu
        if (Press(run, Tooltip(0x90))) {
            run.world()->ui_selection_mode = kMenuPlayers;
            RefreshUi(run);
            CallThread<callee::kClearSelection>(thread);
        }
    }
    if (In(column, 10, 15) && In(row, 1, 5) && run.Acting()) {  // 0x5268e1: minimap colours
        Feedback(run);
        if (++g_minimapMode == kMinimapModes) {
            g_minimapMode = 0;
        }
        Call<callee::kBuildMinimap>(run.world());
        RefreshUi(run);
    }
    if (In(column, 7, 12) && In(row, 7, 11) && run.Acting()) {  // 0x52693a: settlers on the minimap
        Feedback(run);
        if (++g_minimapShowSettlers == kMinimapModes) {
            g_minimapShowSettlers = 0;
        }
        Call<callee::kBuildMinimap>(run.world());
        RefreshUi(run);
    }
    if (In(column, 4, 9) && In(row, 13, 18) && run.Acting()) {  // 0x526993: buildings on the minimap
        Feedback(run);
        g_minimapShowBuildings = 1 - g_minimapShowBuildings;
        Call<callee::kBuildMinimap>(run.world());
        RefreshUi(run);
    }
}

// 0x526b05..0x526e0e: the menu tabs and the buttons below the menus.
void TabsAndButtons(Run& run) {
    CLogicThread* thread = run.thread;
    const std::int32_t column = run.column;
    const std::int32_t row = run.row;
    if (In(column, 2, 13) && In(row, 1, 8) && Press(run, Tooltip(0x4a))) {  // the build menu tab
        CGameData* world = run.world();
        world->ui_selection_mode = kMenuBuild;
        world->ui_selection_param2 = 0;
        world->refresh_ui = 1;
        CallThread<callee::kClearSelection>(thread);
    }
    if (In(column, 15, 26) && In(row, 1, 8) && Press(run, Tooltip(0x4b))) {  // statistics
        run.world()->ui_selection_mode = kMenuStatistics;
        RefreshUi(run);
    }
    if (column >= 28 && In(row, 1, 8) && Press(run, Tooltip(0x4c))) {  // settler menus
        run.world()->ui_selection_mode = kMenuSettlers;
        RefreshUi(run);
    }
    if (column <= 7 && In(row, 97, 103) && Press(run, Tooltip(0x8f))) {  // save / load
        CGameData* world = run.world();
        world->ui_selection_mode = kMenuSaveLoad;
        world->ui_special_menu = 0;
        world->refresh_ui = 1;
        CallThread<callee::kClearSelection>(thread);
    }
    // 0x526c60..0x526e07: the four notification lists (a button acts only
    // when its list has an entry).
    for (std::int32_t list = 0; list < 4; ++list) {
        const std::int32_t left = 8 + 8 * list;
        const bool inColumns = list < 3 ? In(column, left, left + 7) : column >= left;
        if (!inColumns || !In(row, 97, 103)) {
            continue;
        }
        if (!run.Acting()) {
            thread->tooltip = Tooltip(0xad + list);
            continue;
        }
        if (run.world()->notifications[list][0].type == 0) {
            continue;
        }
        Feedback(run);
        run.world()->ui_selection_mode = kMenuMessages;
        Call<callee::kGoToNotification>(run.world(), list + 1);
        RefreshUi(run);
        CallThread<callee::kClearSelection>(thread);
    }
}

// 0x526aaa..0x532f9d: a click in the panel.
void PanelClick(Run& run) {
    CLogicThread* thread = run.thread;
    const std::int32_t cell = g_uiCellSize;
    run.column = (thread->mouse_x - static_cast<std::int32_t>(g_drawOriginX.get())) / cell;
    run.row = (thread->mouse_y - static_cast<std::int32_t>(g_drawOriginY.get())) / cell;
    if (run.column < 0 || run.column >= kPanelColumns || run.row < 0 || run.row >= kPanelRows) {
        return;
    }
    TabsAndButtons(run);
    BuildMenuClick(run);
    StatisticsClick(run);
    SettlerMenusClick(run);
    MessageMenusClick(run);
    PlayersClick(run);
    SoldiersClick(run);
    BuildingInfoClick(run);
    // 0x532f4f: menu 11 (the end of a game with objectives): the finish button.
    CGameData* world = run.world();
    if (world->ui_selection_mode == 11 && world->counters_disabled != 0 && In(run.column, 3, 37) &&
        In(run.row, 87, 91)) {
        CallThread<callee::kFinishGame>(thread);
    }
}

// 0x5258aa..0x532fdf: the click by where it is.
void ClickByPosition(Run& run) {
    CLogicThread* thread = run.thread;
    const std::int32_t x = thread->mouse_x;
    const std::int32_t y = thread->mouse_y;
    if (y < g_layout730f34) {
        const std::int32_t half = (g_layout730f34 - y) / 2;
        if (x < half + g_sidebarWidth) {
            if (x < half + g_layout730f2c) {
                MinimapButtons(run);
            } else if (g_optionRecord == 0 && run.Acting()) {  // 0x5269fd: the minimap itself
                Feedback(run);
                CenterCameraOnMinimapPoint(run);
            }
            return;
        }
    }
    if (x < g_sidebarWidth) {
        PanelClick(run);
        return;
    }
    // 0x532fa2: outside the sidebar.
    if (g_campaignMission == kTutorialMission) {
        if (!run.Acting()) {
            return;
        }
        if (x - g_sidebarWidth < kTutorialAreaWidth && g_displayHeight - y < kTutorialAreaHeight) {
            run.world()->unk2d304 = 1;  // ends the tutorial's wait (TutorialWaitDone 0x515b00)
            return;
        }
    }
    if (run.Acting()) {
        MapClick(run);
    }
}

}  // namespace

void LeftClick(Run& run) {
    CLogicThread* thread = run.thread;
    const UiEventType type = run.type();
    if (type == UiEventType::LeftDown || type == UiEventType::LeftDouble) {
        thread->hover_state = kHoverNone;
        CallThread<callee::kClearPendingUiRefresh>(thread);
    }
    if (g_optionPlay == 0) {
        ClickByPosition(run);
    }
    // 0x53398b: a tooltip found by the hover event is shown.
    if (thread->hover_state == kHoverEvent && thread->tooltip != kNoTooltip) {
        thread->pending_ui_refresh = 1;
    }
}

}  // namespace s3re::game::events
