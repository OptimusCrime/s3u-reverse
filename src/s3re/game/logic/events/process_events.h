#pragma once

// Internal header of CLogicThread::ProcessEvents (0x524fa0), our version.
// The original is one 65 KB function (15,600 instructions); ours is split
// into the frame (the event loop) and one handler per group of events:
//   process_events.cpp        the frame: pops the events, idle/hover
//                             tracking, dispatch, edge scrolling at the end
//   events_keys.cpp           key presses (type 1): the key switch, the chat
//                             key, the cheat input
//   events_panel.cpp          a left click (or hover) on the side panel: the
//                             minimap and its buttons, the menu tabs and the
//                             buttons below, then the open menu's handler:
//   events_build_menu.cpp     menu 1 (build menu)
//   events_statistics.cpp     menu 2 (statistics, areas)
//   events_settler_menus.cpp  menu 3 (settler menus)
//   events_message_menus.cpp  menus 8 and 7 (messages, save/load)
//   events_players.cpp        menu 6 (players)
//   events_soldiers.cpp       menu 5 (settler groups and soldiers)
//   events_building_info.cpp  menu 4 (building info)
//   events_map.cpp            a left click on the map, the drag selection
//                             (left button up), the right click (command)
//   events_mouse.cpp          mouse moves and right-button drags (scrolling),
//                             edge scrolling, and the events while
//                             g_worldUpdateDisabled is set
// The original is hooked as one unit (process_events.cpp). Analysis and the
// list of events: re/process_events.md.

#include "game/layout.h"
#include "game/logic/logic_thread_layout.h"
#include "game/world.h"
#include "msvc6/wstring.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace s3re::game::events {

// ---------------------------------------------------------------- events
// Byte 0 of an event (the draw thread's input, 5 dwords per event on the
// queue CLogicThread+0x3c0).
enum class UiEventType : std::uint8_t {
    None = 0x00,         // the synthesized hover event (idle mouse), or nothing
    KeyDown = 0x01,      // key: virtual key code at +4 (signed byte), modifiers at +1
    Char = 0x03,         // a typed character (dword at +6) for the chat input
    LeftDown = 0x10,     // left button pressed at the mouse position (+0x44/+0x48)
    LeftDouble = 0x11,   // probably the double click; handled like 0x10
    LeftUp = 0x12,       // left button released: the drag selection (0x7dfd38.. to 0x7dfb9c..)
    RightDown = 0x13,    // right button pressed (remembers the time, 0x7dfd70)
    RightUp = 0x15,      // right button released: a short click sends the selection somewhere
    MouseMove = 0x16,    // new mouse position (x, y at +8/+10)
    RightDrag = 0x17,    // mouse moved with the right button: scrolls by (dx, dy) at +8/+10
    Music = 0x20,        // the music thread: byte 2 == 1 = track ended
};

// Modifier bits of event byte 1 (from their use with the number keys and
// the selection: 2 = Ctrl stores a group, 1 = Shift adds to the selection,
// 4 = probably Alt).
constexpr std::uint8_t kModShift = 1;
constexpr std::uint8_t kModCtrl = 2;
constexpr std::uint8_t kModAlt = 4;

#pragma pack(push, 1)
// One event as popped (5 dwords, [esp+0x12c] in the original's frame).
struct UiEvent {
    UiEventType type;        // +0x00
    std::uint8_t modifiers;  // +0x01 kMod* bits (keys and clicks)
    std::uint8_t arg2;       // +0x02 (Music: 1 = track ended)
    std::uint8_t arg3;       // +0x03
    std::int8_t key;         // +0x04 KeyDown: the virtual key code
    std::uint8_t arg5;       // +0x05
    std::uint8_t char_lo[2]; // +0x06 Char: the dword at +6 (overlaps x) is ChatInputChar's argument
    std::uint16_t x;         // +0x08 MouseMove: position; RightDrag: signed delta
    std::uint16_t y;         // +0x0a
    std::uint8_t rest[8];    // +0x0c
};
#pragma pack(pop)
static_assert(sizeof(UiEvent) == 20);
static_assert(offsetof(UiEvent, x) == 8);

// CLogicThread+0x78: what the current event is.
constexpr std::int32_t kHoverNone = 0;     // a real event
constexpr std::int32_t kHoverIdle = 1;     // the mouse has been still for more than 10 calls
constexpr std::int32_t kHoverEvent = 2;    // the synthesized hover event: handlers set the tooltip (+0x54)
constexpr std::int32_t kHoverPending = 3;  // exactly 10 calls without a move: synthesize the hover event

// CLogicThread::PushUiFeedback values (CGameData+0x1539484, drained by
// PrepareDrawData; probably sounds).
constexpr std::uint32_t kFeedbackClick = 0x3d;  // a panel button was pressed

// A tooltip: game text id (group 8) shown for the hovered button
// (CLogicThread+0x54).
constexpr std::uint32_t Tooltip(std::uint32_t index) {
    return 8u << 16 | index;
}

// ---------------------------------------------------------------- frame locals
// Locals of the original's frame that keep their value from one event to
// the next (the loop never leaves the frame). A handler that reads a local
// it didn't write for the same event reads it from here. The original's
// first event of a call sees whatever its caller left on the stack there;
// ours start at 0 (not reproducible).
struct FrameLocals {
    UiEvent event;  // [F+0x12c]: a hover event writes only byte 0, the rest stays
    // The command message being built ([F+0x24], 12 bytes and more: byte 0
    // type 0x21, byte 1 the command, byte 2 the size; the payload from byte
    // 4). Each push writes only the bytes it needs; the rest stay from the
    // previous command of the same call (uninitialized in the original's
    // first one).
    std::uint8_t command[0x100];
    // The chat line sent by Enter ([F+0x320]: {6, 0, size, 0, slot, length,
    // ?, ?, recipients, wide text}; bytes 6 and 7 are never written).
    std::uint8_t chat_message[0x400];
    // The sprintf buffer of the texts ([F+0x720]).
    char text[0x400];
    // The work-area search of MapClick ([F+0x16c], [F+0x168]): the last
    // candidate tile; reused when the search doesn't run (events_map.cpp).
    std::uint32_t work_area_x;
    std::uint32_t work_area_y;
    // The owner of the attacked tile (RightUp, [F+0x164], a byte).
    std::uint8_t target_owner;
    // [F+0x158]: written by the players menu (menu 6, events_players.cpp)
    // and read by key 0x74 (F5) of a later event (events_keys.cpp). Also the
    // data pointer of a string of the cheat input ([F+0x154], 0 after it).
    std::uint32_t slot158;
};

// ---------------------------------------------------------------- run
// What a handler works with.
struct Run {
    CLogicThread* thread;
    FrameLocals* frame;
    // The panel cell of the click/hover (events_panel.cpp, [F+0x10] and
    // [F+0x14]): (mouse - draw origin) / g_uiCellSize, column and row.
    std::int32_t column = 0;
    std::int32_t row = 0;

    CGameData* world() const { return thread->world; }
    const UiEvent& event() const { return frame->event; }
    UiEventType type() const { return frame->event.type; }
    // True when the event is a real one (not the hover event): the
    // handlers act; otherwise they only set the tooltip.
    bool Acting() const { return thread->hover_state == kHoverNone; }
};

// ---------------------------------------------------------------- callees
// Calls of S3.EXE functions through their addresses (thiscall: ECX = the
// world or the thread; our verified replacements run behind the hooks, and
// the tests can stub them).
template <std::uintptr_t Address, typename... Args>
std::uint32_t Call(CGameData* world, Args... args) {
    using Fn = std::uint32_t(__fastcall*)(CGameData*, void*, decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(Address)(world, nullptr, static_cast<std::uint32_t>(args)...);
}
template <std::uintptr_t Address, typename... Args>
std::uint32_t CallThread(CLogicThread* thread, Args... args) {
    using Fn = std::uint32_t(__fastcall*)(CLogicThread*, void*, decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(Address)(thread, nullptr, static_cast<std::uint32_t>(args)...);
}
// A __thiscall on any object (ECX = self).
template <std::uintptr_t Address, typename... Args>
std::uint32_t CallOn(const void* self, Args... args) {
    using Fn = std::uint32_t(__fastcall*)(const void*, void*, decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(Address)(self, nullptr, static_cast<std::uint32_t>(args)...);
}
template <std::uintptr_t Address, typename... Args>
std::uint32_t CallStd(Args... args) {
    using Fn = std::uint32_t(__stdcall*)(decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(Address)(static_cast<std::uint32_t>(args)...);
}
// A pointer as the dword the original passes.
template <typename T>
std::uint32_t U(T* pointer) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

namespace callee {
// CLogicThread members (ECX = the thread)
inline constexpr std::uintptr_t kPushUiFeedback = 0x0053f560;        // (value) ret 4
inline constexpr std::uintptr_t kClearSelection = 0x00542630;        // ()
inline constexpr std::uintptr_t kClearPendingUiRefresh = 0x0053f530; // ()
inline constexpr std::uintptr_t kGoodsPriorityPosition = 0x00542d40; // (area, good) ret 8
inline constexpr std::uintptr_t kSelectSettlersOfType = 0x00542b60;
inline constexpr std::uintptr_t kStoreSettlerGroup = 0x00542820;     // (slot) ret 4
inline constexpr std::uintptr_t kSelectSettlerGroup = 0x00542660;    // (slot) ret 4
inline constexpr std::uintptr_t kRemoveSettlerFromGroups = 0x005428e0;
inline constexpr std::uintptr_t kUpdateGroupMenu = 0x00542990;
inline constexpr std::uintptr_t kSaveGameToSlot = 0x0050ac40;
inline constexpr std::uintptr_t kReadSaveSlotInfo = 0x0050b670;      // (slot, ?, three out strings) ret 0x14
inline constexpr std::uintptr_t kShowMessage = 0x0051e440;
inline constexpr std::uintptr_t kFinishGame = 0x0053e720;            // ()
inline constexpr std::uintptr_t kPlayNextMusicTrack = 0x0053f5f0;    // ()
inline constexpr std::uintptr_t kChatInputChar = 0x0051e2e0;         // (dword) ret 4
inline constexpr std::uintptr_t kChatInputKey = 0x0051e160;          // (key) ret 4 -> AL
inline constexpr std::uintptr_t kCountMilitaryBuildings = 0x00514a00;
inline constexpr std::uintptr_t kDrawBuildingRouteOverlay = 0x005236b0;
inline constexpr std::uintptr_t kDrawWorkAreaOverlay = 0x00523840;
// CGameData members (ECX = the world)
inline constexpr std::uintptr_t kNextSearchGeneration = 0x004ff3f0;  // ()
inline constexpr std::uintptr_t kClampCamera = 0x0051b4d0;           // ()
inline constexpr std::uintptr_t kCenterCameraOnTile = 0x0051b560;    // (x, y) ret 8
inline constexpr std::uintptr_t kCenterCameraOnLocalPlayer = 0x0051b6b0;
inline constexpr std::uintptr_t kPickHitArea = 0x004ff0a0;           // (x, y) ret 8
inline constexpr std::uintptr_t kTileAtScreenPoint = 0x004ff170;     // (x, y) ret 8
inline constexpr std::uintptr_t kGoToNotification = 0x004fee90;      // (list 1..4) ret 4
inline constexpr std::uintptr_t kBuildMinimap = 0x004fb650;          // ()
inline constexpr std::uintptr_t kCheckBuildingSite = 0x004d99e0;     // (x, y, type, player, mode) ret 0x14
inline constexpr std::uintptr_t kSettlerStartHealth = 0x00510470;
// others
inline constexpr std::uintptr_t kPushMessage = 0x004309c0;   // MessageQueue::Push(dwords, count, signal), ECX = the queue
inline constexpr std::uintptr_t kPopMessage = 0x00430980;    // MessageQueue::Pop(), ECX = the queue -> dword*
inline constexpr std::uintptr_t kTileDistance = 0x004ff320;  // stdcall (x0, y0, x1, y1)
inline constexpr std::uintptr_t kBuildingTypeAllowedForRace = 0x004ffd60;
inline constexpr std::uintptr_t kIsMultiplayer = 0x004318d0;  // GameSettings::IsMultiplayer, ECX = g_gameSettings 0x7a7a18
inline constexpr std::uintptr_t kTextAddress = 0x005faaa0;    // CGraphicsData::TextAddress(id)
}  // namespace callee

// The settler groups by a signed group number (-1 reads before the table,
// as the original does with g_currentSettlerGroup == -1; events_keys.cpp).
std::uint16_t& GroupCount(std::int32_t group);
std::uint16_t& GroupEntry(std::int32_t group, std::int32_t index);
// CLogicThread::ShowMessage(text, colour of the local player); the callee
// frees text (events_keys.cpp).
void ShowWideMessage(Run& run, const msvc6::WString& text);

// Helpers of the frame (process_events.cpp).
// CLogicThread::PushUiFeedback(value).
void Feedback(Run& run, std::uint32_t value = kFeedbackClick);
// MessageQueue::Push(frame->command, dwords, signal) on the logic -> network
// queue (+0x3c4).
void PushCommand(Run& run, std::uint32_t dwords, std::uint32_t signal);
// The world's refresh_ui = 1 (the panel is redrawn).
void RefreshUi(Run& run);
// g_gameSettings.IsMultiplayer().
bool IsMultiplayer();

// ---------------------------------------------------------------- handlers
// events_mouse.cpp: the event while g_worldUpdateDisabled is set (only
// Escape, mouse moves/drags, the finish button and the minimap).
void RestrictedEvent(Run& run);
// events_keys.cpp: a KeyDown event (not consumed by the chat input).
void KeyDown(Run& run);
// events_panel.cpp: a left click (LeftDown / LeftDouble) or the hover event
// on the panel, the tutorial click and the map click (0x525866..0x5339a1).
void LeftClick(Run& run);
// The menus' parts of LeftClick, each checks its menu (world
// ui_selection_mode) itself; called in the original's order (1, 2, 3, 8,
// 7, 6, 5, 4, 11) with the panel cell in run.column / run.row.
void BuildMenuClick(Run& run);       // events_build_menu.cpp: menu 1, 0x526e14..0x528a38
void StatisticsClick(Run& run);      // events_statistics.cpp: menu 2, 0x528a38..0x52c237
void SettlerMenusClick(Run& run);    // events_settler_menus.cpp: menu 3, 0x52c237..0x52e1b0
void MessageMenusClick(Run& run);    // events_message_menus.cpp: menus 8 and 7, 0x52e1b0..0x52ea09
void PlayersClick(Run& run);         // events_players.cpp: menu 6, 0x52ea09..0x52ec5a
void SoldiersClick(Run& run);        // events_soldiers.cpp: menu 5, 0x52ec5a..0x52fe70
void BuildingInfoClick(Run& run);    // events_building_info.cpp: menu 4, 0x52fe70..0x532f4f
// events_map.cpp
void MapClick(Run& run);             // a left click on the map (0x532fe4..0x53398b)
void LeftUp(Run& run);               // LeftUp: the drag selection (0x5339a1..0x534301)
void RightUp(Run& run);              // RightUp: the command for the selection (0x534317..0x5348fa)
// events_mouse.cpp
void MouseMove(Run& run);            // MouseMove (0x5348fa..)
void RightDrag(Run& run, bool restricted);  // RightDrag (restricted: the copy of the restricted mode)
void EdgeScroll(CLogicThread* thread);  // after the loop: scrolling at the screen edges (0x534abe)

}  // namespace s3re::game::events
