# CLogicThread::ProcessEvents (0x524fa0): the UI input of the logic thread

Analysis of round 8 (wip/events-r8, 2026-11-28). The function is one
65 KB body (0x524fa0..0x534bfe, 15,600 instructions, a 0xb10-byte frame;
jump table 0x534c00 with the byte index 0x534cbc follows the last `ret`).
Our version is split into handler files under
`src/s3re/game/logic/events/` (list in `process_events.h`).

`CLogicThread::Step` (0x53db30) calls it on every step (not only on
ticks), as the first thing. It turns the player's input, which the draw
thread queues as events, into UI state (the open panel menu and its page,
the selection, the camera, tooltips, the chat input) and into **player
commands**: 0x21 messages pushed on the logic -> network queue
(CLogicThread+0x3c4, MessageQueue::Push 0x4309c0) that come back through
ExecuteCommands in step order (re/execute_commands.md). In a replay the
recorded commands are fed in instead (re/commands_lockstep.md); this
function's own effects there are UI only.

## Frame

1. **Hover state.** `idle_steps` (+0x74) counts the calls since the mouse
   moved (ClearPendingUiRefresh 0x53f530, called by every mouse event,
   clears it). Above 10: `hover_state` (+0x78) = 1; exactly 10: 3 (and
   `hover_x/y` +0x7c/+0x80 = the mouse position); otherwise 0.
2. **Loop.** While the input queue (+0x3c0, `ui_events`, Setup argument 1)
   has dwords, pop one event (5 dwords via 5 Pop calls) into the frame's
   event buffer [F+0x12c]. When it is empty: if the hover is pending (3),
   handle one synthesized event of type 0 with `hover_state` = 2 (only byte
   0 of the buffer is written; the rest keeps the last event's bytes),
   else leave the loop. In hover state 2 every panel button sets its
   tooltip (+0x54, a game text id 0x8xxxx) instead of acting, and the end
   of the click handling asks for a UI refresh (+0x84) when a tooltip was
   set.
3. Per event: type 0x20 with byte 2 == 1 -> PlayNextMusicTrack 0x53f5f0;
   type 3 while the chat is open -> ChatInputChar 0x51e2e0 (the dword at
   event +6). Then, with `g_worldUpdateDisabled` 0x7deb55 set, the
   restricted handling (below); otherwise the normal one:
   - type 1 (key): unless the open chat takes it (ChatInputKey 0x51e160
     returns non-zero), the key handler;
   - types 0x10/0x11 (left button) or the hover event, not paused: the
     click handler (panel, minimap, map);
   - 0x12 (left button up): the drag selection;
   - 0x13 (right button down): remembers `command_attempts` in
     g_rightDownTime 0x7dfd70 and **ends the call** (the later events stay
     queued);
   - 0x15 (right button up): the move/attack command;
   - 0x16 (mouse move): position (+0x44/+0x48), PickHitArea, g_lastMouseX/Y;
   - 0x17 (right drag, scroll mode 0): scrolls the camera.
   While `paused` (+0x220, key P) the click, 0x12, 0x13, 0x15 and 0x17
   are ignored.
4. After the loop: edge scrolling (scroll mode 1): with the mouse on a
   screen edge the camera moves by (step_time - previous_step_time) *
   g_scrollSpeed / 20 (+0x650/+0x64c are Step's timeGetTime values).

### Restricted mode (g_worldUpdateDisabled)

Only: Escape (unless objectives are on or campaign mission 68/69 runs)
posts 0x801 to the app (leave the game); mouse moves (also clearing
+0x4c) and right drags; a click or hover then checks the finish button
(cells 3..37 x 87..91 of the panel, with objectives and counters, or
after winning missions 68/69) -> FinishGame 0x53e720, and the minimap
(centre the camera there).

### -record and -play (the open question from LOGBOOK)

With the game's `-record` option (g_optionRecord 0x7a800a) and world
`ui_2d268[0]` == 0, a right drag is not applied directly but sent as a
ScrollCamera command {0x21, 2, 12, ?, dx BE32, dy BE32} (dx = event dx *
g_scrollSpeed >> 3), so that the recording contains the scrolls. With
`-play` (0x7a800b) right drags, edge scrolling and all clicks on the
panel/map are ignored (the recorded commands drive the game). The
minimap click is also skipped while recording. That is all of it: the
mouse positions are not otherwise recorded.

## Events and the event buffer

| byte | meaning |
|---|---|
| +0 | type (UiEventType) |
| +1 | modifiers: 1 Shift (add to selection), 2 Ctrl (store group / attack), 4 Alt |
| +2 | Music: 1 = track ended |
| +4 | KeyDown: virtual key (signed byte) |
| +6 | Char: the dword passed to ChatInputChar |
| +8, +10 | MouseMove / RightUp: x, y (u16); RightDrag: dx, dy (s16) |

## Locals that live across events

The loop never leaves the frame, so a few locals keep their values from
one event to the next, and in the first event of a call they hold
whatever the caller left on the stack (not reproducible; ours start at 0,
which is also what the emulator's zeroed stack gives the original):
- [F+0x24..] the command being built: each command writes only its bytes
  (byte 3 and the unused tail stay; the "uninitialized bytes" of command
  messages noted in LOGBOOK come from here).
- [F+0x158]: an int of the players menu (menu 6) that key F5 reads: F5
  sets every player as chat recipient only when it is non-zero (a local
  of another case reused; kept).
- [F+0x16c]/[F+0x168]: the work-area search's last candidate tile
  (MapClick, building menu page 2), reused when the MapOffset table's
  first distance is above 5.
- [F+0x320] the chat message, [F+0x720] the sprintf buffer (always
  written before use).

## Keys (events_keys.cpp)

Enter (multiplayer: chat), Tab (drop the settlers at their start health
from the group), 1..9 (select group, Ctrl: store), Space (oldest
notification), Home (start position), arrows (scroll 2 * speed), Q W E R
/ A S D F / Y(Z) X C V (pages of menus 1 / 2 / 3), G (objectives menu
11), Alt+B (build costs), T (g_victoryProtectActive), P (pause, menu 9),
numpad +/- (volume +0x664, -10000..0), F1..F3 (display mode through the
draw thread's semaphore), F5/F6 (chat to all / team), F12 (fast-forward
882 steps, single player). The cheat: O I U J K sets g_cheatMode 9; then
F7 ends it, Shift+F8 wins the running campaign mission (g_gameOutcome =
1), F8 picks the campaign mission from the map name ("Single" + letter at
7 + digits at 8, 9) and announces it ("Activating campaign ...").

## Clicks (events_panel.cpp, events_map.cpp)

By position: above the minimap's bottom edge (g_layout730f34) and left
of its diagonal: the minimap buttons (players menu, minimap colours,
settlers, buildings) or a jump to the clicked minimap point. Elsewhere in
the sidebar the panel in cells of g_uiCellSize (40 x 106): the menu tabs
(build 1, statistics 2, settlers 3), the buttons below (save/load 7, the
four notification lists -> menu 8 + GoToNotification), then the open
menu's buttons (menus 1, 2, 3, 8, 7, 6, 5, 4, 11, one handler each).
Outside the sidebar: the tutorial's continue area (mission 99), else the
map: building placement, work area / trade route / dock of the selected
building, a priest's spell, or the start of a drag selection. Left
button up selects the settlers in the box (hit areas of the draw
thread, g_hitAreas); right button up sends the group (MoveUnits 9, 13
Shift, 14 Ctrl = attack, 15 Alt).

## Menus

(Filled from the part agents' reports.)
