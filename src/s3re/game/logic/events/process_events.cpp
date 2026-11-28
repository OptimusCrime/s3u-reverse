// CLogicThread::ProcessEvents (0x00524fa0): the user interface's input.
//
// Purpose: turns the player's input (keys, clicks, mouse moves, the chat
// input), which the draw thread queues as events, into UI state changes
// (the open panel menu, the selection, the camera, tooltips) and into the
// commands sent to the network (MessageQueue::Push on +0x3c4), which come
// back through ExecuteCommands. CLogicThread::Step calls it on every step,
// not only on ticks.
//
// Shape (thiscall on the CLogicThread, no arguments, EAX not used by Step):
//   idle_steps (+0x74) counts the calls since the mouse last moved
//   (ClearPendingUiRefresh 0x53f530 clears it): above 10 hover_state (+0x78)
//   is 1, at exactly 10 it is 3 (and hover_x/y = the mouse position),
//   otherwise 0. Then, for each event on the queue +0x3c0 (5 dwords;
//   MessageQueue::Pop 0x430980 per dword): when the queue is empty and the
//   hover is pending, one synthesized event of type 0 is handled with
//   hover_state 2 (the panel handlers then only set the tooltip +0x54);
//   otherwise the loop ends. Each event goes through: the music event
//   (0x20), the chat character (3), then either the restricted handling
//   (g_worldUpdateDisabled) or the normal one: keys, left click / hover,
//   left button up, right button down (ends the call), right button up,
//   mouse moves and right drags. After the loop: edge scrolling.
// Analysis and the list of events: re/process_events.md.

#include "game/logic/events/process_events.h"

#include "game/globals.h"
#include "hooks.h"

#include <cstring>

namespace s3re::game {

namespace events {

namespace {

constexpr std::uint32_t kHoverDelay = 10;     // calls without a mouse move until the tooltip
constexpr std::uint32_t kEventDwords = 5;
constexpr std::uint8_t kMusicTrackEnded = 1;  // UiEvent::arg2 of a Music event

// Pops the next event into frame->event (0x524ffe..0x525054). False when the
// queue is empty and no hover event is due: the loop ends.
bool NextEvent(Run& run) {
    CLogicThread* thread = run.thread;
    if (thread->ui_events->count == 0) {
        if (thread->hover_state != kHoverPending) {
            return false;
        }
        // 0x525040: the hover event; only its type byte is written.
        run.frame->event.type = UiEventType::None;
        thread->hover_state = kHoverEvent;
        return true;
    }
    auto* dwords = reinterpret_cast<std::uint32_t*>(&run.frame->event);
    for (std::uint32_t i = 0; i < kEventDwords; ++i) {
        dwords[i] = *reinterpret_cast<const std::uint32_t*>(CallOn<callee::kPopMessage>(thread->ui_events));
    }
    return true;
}

// 0x525054..0x525092: the events that are handled in every mode.
void MusicAndChatCharacter(Run& run) {
    const UiEvent& event = run.event();
    if (run.type() == UiEventType::Music) {
        if (event.arg2 == kMusicTrackEnded) {
            CallThread<callee::kPlayNextMusicTrack>(run.thread);
        }
        return;
    }
    if (run.type() == UiEventType::Char && run.thread->chat_open != 0) {
        std::uint32_t character;
        std::memcpy(&character, reinterpret_cast<const std::uint8_t*>(&event) + 6, 4);
        CallThread<callee::kChatInputChar>(run.thread, character);
    }
}

// What the loop does after an event.
enum class Next { Continue, Stop };

// 0x525452..0x534aa0: one event in the normal mode.
Next NormalEvent(Run& run) {
    CLogicThread* thread = run.thread;
    const UiEventType type = run.type();
    if (type == UiEventType::KeyDown) {
        // 0x52545a: an open chat input takes the key first; Enter (AL 0) is
        // passed on (it sends the line).
        const bool consumed = thread->chat_open != 0 &&
                              static_cast<std::uint8_t>(CallThread<callee::kChatInputKey>(
                                  thread, static_cast<std::int32_t>(run.event().key))) != 0;
        if (!consumed) {
            KeyDown(run);
        }
    }
    // 0x525866: a left click, or the hover event.
    if ((type == UiEventType::LeftDown || type == UiEventType::LeftDouble || thread->hover_state == kHoverEvent) &&
        thread->paused == 0) {
        LeftClick(run);
    }
    if (type == UiEventType::LeftUp) {  // 0x5339a1
        if (thread->paused != 0) {
            return Next::Continue;
        }
        LeftUp(run);
    }
    if (type == UiEventType::RightDown) {  // 0x534308
        if (thread->paused != 0) {
            return Next::Continue;
        }
        // 0x534aa5: remember when; the call ends here (the other events stay queued).
        CallThread<callee::kClearPendingUiRefresh>(thread);
        g_rightDownTime = run.world()->command_attempts;
        return Next::Stop;
    }
    if (type == UiEventType::RightUp) {  // 0x53431f
        if (thread->paused != 0) {
            return Next::Continue;
        }
        RightUp(run);
    }
    if (type == UiEventType::MouseMove) {  // 0x5348fa
        MouseMove(run);
    }
    if (type == UiEventType::RightDrag && thread->paused == 0) {  // 0x53494c
        RightDrag(run, false);
    }
    return Next::Continue;
}

}  // namespace

void Feedback(Run& run, std::uint32_t value) {
    CallThread<callee::kPushUiFeedback>(run.thread, value);
}

void PushCommand(Run& run, std::uint32_t dwords, std::uint32_t signal) {
    CallOn<callee::kPushMessage>(run.thread->toNet, U(run.frame->command), dwords, signal);
}

void RefreshUi(Run& run) {
    run.world()->refresh_ui = 1;
}

bool IsMultiplayer() {
    return static_cast<std::uint8_t>(CallOn<callee::kIsMultiplayer>(&g_gameSettings)) != 0;
}

void Run_(CLogicThread* thread, FrameLocals& frame) {
    // 0x524fc3: the hover state of this call.
    const std::uint32_t idle = ++thread->idle_steps;
    if (idle > kHoverDelay) {
        thread->hover_state = kHoverIdle;
    } else if (idle == kHoverDelay) {
        thread->hover_state = kHoverPending;
        thread->hover_x = thread->mouse_x;
        thread->hover_y = thread->mouse_y;
    } else {
        thread->hover_state = kHoverNone;
    }
    Run run{thread, &frame};
    while (NextEvent(run)) {
        MusicAndChatCharacter(run);
        if (g_worldUpdateDisabled != 0) {
            RestrictedEvent(run);
            continue;
        }
        if (NormalEvent(run) == Next::Stop) {
            break;
        }
    }
    EdgeScroll(thread);
}

}  // namespace events

using ProcessEventsFn = void(__fastcall*)(CLogicThread* self, void* edx);

// Purpose: handles the player's input for this step: keys, clicks on the
// panel and the map, mouse moves and the chat input; changes the UI state
// and sends the resulting commands to the network.
void __fastcall CLogicThread_ProcessEvents(CLogicThread* self, void*) {
    events::FrameLocals frame;
    std::memset(&frame, 0, sizeof(frame));
    events::Run_(self, frame);
}

S3RE_HOOK_UNVERIFIED(0x00524fa0, CLogicThread_ProcessEvents, ProcessEventsFn, Original_CLogicThread_ProcessEvents,
                     CLogicThread_ProcessEvents);

}  // namespace s3re::game
