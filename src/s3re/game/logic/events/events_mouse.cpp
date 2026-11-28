// CLogicThread::ProcessEvents (0x00524fa0), part: mouse moves, scrolling
// by right drag and at the screen edges, and the events while
// g_worldUpdateDisabled is set (0x5250a6..0x52544d).
//
// A right drag (event 0x17, scroll mode 0 only) moves the camera by the
// event's (dx, dy) * g_scrollSpeed / 8; with the game's -record option (and
// world ui_2d268[0] == 0) it becomes a ScrollCamera command instead
// ({0x21, 2, 12, ?, dx BE, dy BE}, so it is recorded), with -play it is
// ignored. Edge scrolling (scroll mode 1) moves it by the time since the
// last step * g_scrollSpeed / 20 when the mouse is on a screen edge.

#include "game/logic/events/process_events.h"

#include "game/enums/commands.h"
#include "game/globals.h"

namespace s3re::game::events {

namespace {

constexpr std::int32_t kScrollByDrag = 0;   // g_scrollMode
constexpr std::int32_t kScrollAtEdges = 1;
constexpr std::uint32_t kSelectionScrolling = 2;  // CLogicThread::group_selection_changed while dragging
constexpr std::int32_t kEdgeScrollDivisor = 20;
constexpr std::uint8_t kScrollCommandSize = 12;   // bytes

void StoreBigEndian(std::uint8_t* out, std::int32_t value) {
    const auto v = static_cast<std::uint32_t>(value);
    out[0] = static_cast<std::uint8_t>(v >> 24);
    out[1] = static_cast<std::uint8_t>(v >> 16);
    out[2] = static_cast<std::uint8_t>(v >> 8);
    out[3] = static_cast<std::uint8_t>(v);
}

void MoveCamera(CGameData* world, std::int32_t dx, std::int32_t dy) {
    world->camera_x += dx;
    world->camera_y += dy;
    Call<callee::kClampCamera>(world);
    world->redraw = 0;
}

// The moved edges of EdgeScroll: one axis, then ClampCamera.
void MoveCameraX(CGameData* world, std::int32_t dx) {
    world->camera_x += dx;
    Call<callee::kClampCamera>(world);
    world->redraw = 0;
}
void MoveCameraY(CGameData* world, std::int32_t dy) {
    world->camera_y += dy;
    Call<callee::kClampCamera>(world);
    world->redraw = 0;
}

}  // namespace

// 0x534904 (and 0x525103 in the restricted mode, which also clears
// group_selection_changed): the mouse moved.
void MouseMove(Run& run) {
    CLogicThread* thread = run.thread;
    CallThread<callee::kClearPendingUiRefresh>(thread);
    thread->mouse_x = run.event().x;
    thread->mouse_y = run.event().y;
    Call<callee::kPickHitArea>(run.world(), thread->mouse_x, thread->mouse_y);
    g_lastMouseX = thread->mouse_x;
    g_lastMouseY = thread->mouse_y;
}

// 0x53496a (and 0x525161 in the restricted mode): the right drag; the
// caller checked `paused` (normal mode).
void RightDrag(Run& run, bool restricted) {
    CLogicThread* thread = run.thread;
    CallThread<callee::kClearPendingUiRefresh>(thread);
    if (g_scrollMode != kScrollByDrag) {
        return;
    }
    const std::int32_t speed = g_scrollSpeed;
    const std::int32_t dx = (static_cast<std::int16_t>(run.event().x) * speed) >> 3;
    const std::int32_t dy = (static_cast<std::int16_t>(run.event().y) * speed) >> 3;
    if (g_optionRecord != 0 && run.world()->ui_2d268[0] == 0) {
        // 0x525194: as a ScrollCamera command, so that -record writes it.
        std::uint8_t* command = run.frame->command;
        command[0] = static_cast<std::uint8_t>(NetMessage::CommandRequest);
        command[1] = static_cast<std::uint8_t>(CommandId::ScrollCamera);
        command[2] = kScrollCommandSize;
        StoreBigEndian(command + 4, dx);
        StoreBigEndian(command + 8, dy);
        PushCommand(run, 3, 1);
        return;
    }
    if (g_optionPlay != 0) {
        if (restricted) {
            thread->group_selection_changed = 0;  // 0x52528e (only in the restricted copy)
        }
        return;
    }
    thread->group_selection_changed = kSelectionScrolling;
    MoveCamera(run.world(), dx, dy);
}

// 0x534abe: after the loop; scrolling at the screen edges.
void EdgeScroll(CLogicThread* thread) {
    if (g_scrollMode != kScrollAtEdges || thread->paused != 0) {
        return;
    }
    const std::int32_t step = static_cast<std::int32_t>((thread->step_time - thread->previous_step_time) *
                                                        static_cast<std::uint32_t>(g_scrollSpeed.get())) /
                              kEdgeScrollDivisor;
    if (g_optionPlay != 0) {
        return;
    }
    CGameData* world = thread->world;
    if (thread->mouse_x == g_displayWidth - 1) {
        MoveCameraX(world, step);
    }
    if (thread->mouse_x == 0) {
        MoveCameraX(world, -step);
    }
    if (thread->mouse_y == 0) {
        MoveCameraY(world, -step);
    }
    if (thread->mouse_y == g_displayHeight - 1) {
        MoveCameraY(world, step);
    }
}

void CenterCameraOnMinimapPoint(Run& run);  // events_panel.cpp

// 0x5250a6..0x52544d: g_worldUpdateDisabled is set (the world stands still,
// probably the end of a game): only Escape (leaves the game), mouse moves,
// right drags, the finish buttons and the minimap.
void RestrictedEvent(Run& run) {
    CLogicThread* thread = run.thread;
    const UiEventType type = run.type();
    CGameData* world = run.world();
    if (type == UiEventType::KeyDown) {
        constexpr std::int8_t kKeyEscape = 0x1b;
        constexpr std::int32_t kMissionNoEscape1 = 0x44;  // campaign missions 68, 69 can't be left
        constexpr std::int32_t kMissionNoEscape2 = 0x45;
        constexpr std::uint32_t kMessageLeaveGame = 0x801;
        if (run.event().key == kKeyEscape && world->objectives_enabled == 0 &&
            g_campaignMission != kMissionNoEscape1 && g_campaignMission != kMissionNoEscape2) {
            CallOn<0x0060f480>(g_app.get() + 0xc, kMessageLeaveGame, 2, 0);  // Win32::CMessagedThread::PostThreadMessage
        }
    } else {
        if (type == UiEventType::MouseMove) {
            CallThread<callee::kClearPendingUiRefresh>(thread);
            thread->group_selection_changed = 0;
            thread->mouse_x = run.event().x;
            thread->mouse_y = run.event().y;
            Call<callee::kPickHitArea>(world, thread->mouse_x, thread->mouse_y);
            g_lastMouseX = thread->mouse_x;
            g_lastMouseY = thread->mouse_y;
        }
        if (type == UiEventType::RightDrag) {
            RightDrag(run, true);
        }
    }
    if (type != UiEventType::LeftDown && type != UiEventType::LeftDouble && thread->hover_state != kHoverEvent) {
        return;
    }
    // 0x5252aa: the finish button of a game with objectives (as menu 11's).
    const std::int32_t cell = g_uiCellSize;
    auto onFinishButton = [&]() {
        if (thread->mouse_x >= g_sidebarWidth) {
            return false;
        }
        const std::int32_t column = (thread->mouse_x - static_cast<std::int32_t>(g_drawOriginX.get())) / cell;
        const std::int32_t row = (thread->mouse_y - static_cast<std::int32_t>(g_drawOriginY.get())) / cell;
        return column >= 3 && column <= 37 && row >= 87 && row <= 91;
    };
    world = run.world();
    if (world->objectives_enabled != 0 && world->counters_disabled != 0 && onFinishButton()) {
        CallThread<callee::kFinishGame>(thread);
    }
    // 0x52530d: the same button after the won missions 68 / 69.
    constexpr std::int32_t kGameWon = 1;
    if ((g_campaignMission == 0x44 || g_campaignMission == 0x45) && g_gameOutcome == kGameWon && onFinishButton()) {
        CallThread<callee::kFinishGame>(thread);
    }
    // 0x52536e: a click on the minimap.
    if (g_optionPlay != 0 || thread->mouse_y >= g_layout730f34) {
        return;
    }
    const std::int32_t half = (g_layout730f34 - thread->mouse_y) / 2;
    if (thread->mouse_x >= half + g_sidebarWidth || thread->mouse_x <= half + g_layout730f2c ||
        thread->hover_state != kHoverNone) {
        return;
    }
    Feedback(run);
    CenterCameraOnMinimapPoint(run);
}

}  // namespace s3re::game::events
