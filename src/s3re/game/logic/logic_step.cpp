// CLogicThread::Step (0x0053db30): one iteration of the logic thread; vtable
// slot 9, called repeatedly by DirectX::CGameThread::Main. thiscall, no
// stack arguments, plain `ret` (EAX not used by the caller). Reimplemented
// as CLogicThread_Step_Impl (wip/logic-r8); the hook that runs it is the
// tooling wrapper in logic_thread.cpp (S3RE_HOOK_ALWAYS: original code in
// original mode, ours otherwise).
//
// What it does:
//   1. g_logicStartState 250 (the first step after a game was built): wait
//      for the graphics-data semaphore (vtable slot 0 of *0x7a800c,
//      (App::WaitTime(7), 1)), BuildMinimap, redraw 0, camera_drawn 10^7 /
//      10^7, minimap_ready 1, refresh_ui 1, ClampCamera, g_logicStartState 0.
//   2. start_state (+0x3ac): 1 FinishInitialising, then 0 and started[] 1;
//      2 the no-op 0x59f200, then 3. Nothing else while it is not 0.
//   3. step_time_previous = step_time, step_time = timeGetTime().
//   4. The wait: without a fast-forward countdown (g_stepCountdown 0)
//      g_fastForwarding = 0 and the wait set (+0x3dc) WaitFor(App::WaitTime
//      (4), 1), which returns the signalled object (with --fast a poll,
//      tooling/fast.h). During the game's own fast-forward (F12;
//      ProcessEvents adds 882 to the countdown) the countdown drops by one:
//      not yet 0, g_fastForwarding 1; at 0, g_fastForwarding 0,
//      camera_drawn 10^7 / 10^7 and the timer (+0x3bc) Wait(WaitTime(4),
//      1). In that branch the original leaves its "signalled" local
//      unwritten, so it keeps the value of the last wait of an earlier step
//      (the same stack slot); ours keeps it in g_lastSignalled.
//   5. ProcessEvents 0x524fa0, UpdateLobby 0x541170.
//   6. g_worldUpdateDisabled: pause_state 1 and the UI mode (11 with the
//      game objectives on and the counters off; 12 in campaign missions
//      68/69 when won; else 10) with refresh_ui 1.
//   7. A tick only when g_logicStartState is 0, the timer was signalled or
//      g_fastForwarding is set, and pause_state is not 1 (2: this tick, then
//      1). command_attempts += 4; ExecuteCommands; unless it returns 1 the
//      tick stops here (and g_gameTick is still updated). Then: won
//      (g_gameOutcome 1): message 0x21 0xf1 (2 dwords) to the network; at
//      every 256th tick (low byte 0) the RNG's first word goes into
//      g_syncRandomHistory[(tick >> 8) & 31] and, unless the game is lost,
//      the sync message 0x21 0xfb (3 dwords: the slot and that word
//      big-endian, the local player).
//   8. tick + 1; the pile claim at g_pileClaimsCursor cleared, the cursor
//      moves on (0..127).
//   9. Unless option2a of the game settings (a ladder "Liga" game,
//      re/logic_thread.md) or a lost game: every 28125
//      ticks (30 min) after the last multiplayer save the reminder text
//      0x80153 with ++g_global7deb50 * 30 (minutes), in the local player's
//      colour.
//  10. With the game objectives on: counters off -> g_worldUpdateDisabled
//      1, every tile's draw flags 0x3f, minimap_ready 1, the 64 minimap
//      slices and the border flags; otherwise at the tick
//      (objective_timeout - end_warning_minutes) * 60000 >> 6 (32-bit) the
//      text "Spiel endet in %u min".
//  11. The local player's map reveal (spell 31) ends 15 s after it began:
//      map_revealed 0, g_revealMap 0, DebugLog.
//  12. PrepareDrawData(0) (when no countdown runs) before and after each of
//      UpdateEnvironment, UpdateEconomy and UpdateSettlers, which only run
//      while g_simulationPaused is 0; then (paused re-checked) every
//      computer player's Step.
//  13. Tutorial (g_campaignMission 99): UpdateTutorial. While the outcome is
//      open (-1) and the world updates run: CheckGameEnd (its AL sign-
//      extended into g_gameOutcome); lost: message 0x21 0xfc, won: 0x21
//      0xfd (2 dwords, the local player in byte 4).
//  14. g_gameTick = tick (only when a tick was due), AgeMessages, and
//      without countdown PrepareDrawData(signalled == the draw request
//      event +0x3d8).
// The network messages' unwritten bytes are stack leftovers in the
// original (byte 3 is 0 in every recording, bytes 9..11 vary and are
// ignored by the recorder); ours are 0. The callees are called through
// their S3.EXE addresses (ProcessEvents, UpdateLobby, UpdateTutorial and
// PrepareDrawData are not ours yet). The texts of 9 and 10 go through a
// std::string built like the original's (two temporaries, then erased and
// freed); their allocator bytes are uninitialized stack bytes in the
// original and never copied, ours are 0.

#include "game/debug_log.h"
#include "game/framework/exception_strings.h"
#include "game/globals.h"
#include "game/layout.h"
#include "game/logic/logic_step.h"
#include "game/logic/logic_thread_layout.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/ui/game_settings_layout.h"
#include "msvc6/string.h"
#include "msvc6/wstring.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"
#include "tooling/fast.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

using msvc6::String;
using msvc6::WString;

constexpr std::uint32_t kStartStateReady = 250;      // g_logicStartState: the first step of a built game
constexpr std::int32_t kCameraNotDrawn = 10000000;  // camera_drawn_x/y: forces the draw thread to redraw
constexpr std::uint32_t kStartFinishInitialising = 1;  // start_state
constexpr std::uint32_t kStartNoOp = 2;
constexpr std::uint32_t kStartAfterNoOp = 3;
constexpr std::uint32_t kPausePaused = 1;  // pause_state
constexpr std::uint32_t kPauseOneTick = 2;
constexpr std::uint32_t kWaitGraphicsLock = 7;  // App::WaitTime kinds
constexpr std::uint32_t kWaitLogicTimer = 4;
constexpr std::uint32_t kThrowOnTimeout = 1;
constexpr std::int32_t kUiModeObjectivesTimeout = 11;  // ui_selection_mode while the world updates are off
constexpr std::int32_t kUiModeMissionWon = 12;
constexpr std::int32_t kUiModeGameOver = 10;
constexpr std::int32_t kMissionAmazonsA = 68;  // g_campaignMission (0x44 / 0x45)
constexpr std::int32_t kMissionAmazonsB = 69;
constexpr std::int32_t kMissionTutorial = 99;
constexpr std::int32_t kOutcomeLost = 0;  // g_gameOutcome
constexpr std::int32_t kOutcomeWon = 1;
constexpr std::int32_t kOutcomeOpen = -1;
constexpr std::int32_t kCommandAttemptStep = 4;
constexpr std::uint32_t kCommandsDone = 1;  // ExecuteCommands: the step's commands were all there
constexpr std::uint32_t kSyncTickMask = 0xff;
constexpr std::uint32_t kSyncSlots = 32;
constexpr std::uint16_t kPileClaims = 128;
constexpr std::uint32_t kSaveReminderTicks = 28125;  // 30 min of 64 ms ticks
constexpr std::int32_t kSaveReminderMinutes = 30;
constexpr std::uint32_t kTextSaveReminder = 0x80153;  // raw-ok: text id of the graphics data
constexpr std::uint32_t kCurrentLanguage = 0xffffffff;
constexpr std::uint8_t kTileDrawAllFog = 0x3f;
constexpr std::uint32_t kMinimapSlices = 64;
constexpr std::uint32_t kMsPerMinute = 60000;
constexpr std::uint32_t kTickShift = 6;  // 64 ms per tick
constexpr std::uint32_t kMsPerTick = 64;
constexpr std::uint32_t kRevealSeconds = 15;
constexpr std::size_t kTextBytes = 0x100;  // the original's sprintf buffers

// Network message bytes (0x21 = a command of the logic thread).
constexpr std::uint8_t kCommandMessage = 0x21;
constexpr std::uint8_t kCommandGameWon = 0xf1;
constexpr std::uint8_t kCommandSync = 0xfb;
constexpr std::uint8_t kCommandLost = 0xfc;
constexpr std::uint8_t kCommandWonFinal = 0xfd;
constexpr std::uint8_t kTwoDwordsBytes = 8;
constexpr std::uint8_t kThreeDwordsBytes = 12;
constexpr std::uint32_t kSignal = 1;

// Callees, through their S3.EXE addresses (whatever is installed runs).
constexpr std::uintptr_t kWaitTime = 0x0040d0e0;            // App::WaitTime (fastcall kind)
constexpr std::uintptr_t kBuildMinimap = 0x004fb650;        // CGameData::BuildMinimap
constexpr std::uintptr_t kClampCamera = 0x0051b4d0;         // CGameData::ClampCamera
constexpr std::uintptr_t kFinishInitialising = 0x004fce90;  // CGameData::FinishInitialising
constexpr std::uintptr_t kNoOp = 0x0059f200;                // ret
constexpr std::uintptr_t kProcessEvents = 0x00524fa0;       // CLogicThread::ProcessEvents
constexpr std::uintptr_t kUpdateLobby = 0x00541170;         // CLogicThread::UpdateLobby
constexpr std::uintptr_t kExecuteCommands = 0x00535990;     // CLogicThread::ExecuteCommands
constexpr std::uintptr_t kPush = 0x004309c0;                // MessageQueue::Push
constexpr std::uintptr_t kTextAddress = 0x005faaa0;         // CGraphicsData::TextAddress
constexpr std::uintptr_t kSprintf = 0x0061f27e;
constexpr std::uintptr_t kWiden = 0x0060b840;               // NarrowToWideString (fastcall wide out, narrow)
constexpr std::uintptr_t kShowMessage = 0x0051e440;         // CLogicThread::ShowMessage
constexpr std::uintptr_t kUpdateMinimapSlice = 0x004fb520;  // CGameData::UpdateMinimapSlice
constexpr std::uintptr_t kUpdateAllBorderFlags = 0x0051a420;
constexpr std::uintptr_t kPrepareDrawData = 0x0051f110;     // CLogicThread::PrepareDrawData(bool)
constexpr std::uintptr_t kUpdateEnvironment = 0x004e7810;
constexpr std::uintptr_t kUpdateEconomy = 0x004e05e0;
constexpr std::uintptr_t kUpdateSettlers = 0x004e9670;
constexpr std::uintptr_t kComputerPlayerStep = 0x00550f80;  // CComputerPlayer::Step
constexpr std::uintptr_t kUpdateTutorial = 0x0053fbb0;      // CLogicThread::UpdateTutorial
constexpr std::uintptr_t kCheckGameEnd = 0x00511c50;        // CLogicThread::CheckGameEnd
constexpr std::uintptr_t kAgeMessages = 0x0051e680;         // CLogicThread::AgeMessages

using WorldFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx);
using WorldArgFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t value);
using ThreadFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using ThreadArgFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx, std::uint32_t value);
using WaitFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t timeout, std::uint32_t throwOnTimeout);
using PushFn = void(__fastcall*)(MessageQueue* self, void* edx, const void* dwords, std::int32_t count,
                                 std::uint32_t signal);
using SprintfFn = int(__cdecl*)(char* buffer, const char* format, ...);
using StepFn = void(__fastcall*)(CLogicThread* self, void* edx);

// The "signalled" local of the original step, which its fast-forward
// branch doesn't write (see 4. above). Only the logic thread uses it.
void* g_lastSignalled = nullptr;

std::uint32_t WaitTime(std::uint32_t kind) {
    using Fn = std::uint32_t(__fastcall*)(std::uint32_t kind);
    return reinterpret_cast<Fn>(kWaitTime)(kind);
}

// Slot 0 of a Win32 waitable: Wait(timeout, throw on timeout).
void WaitOn(void* waitable, std::uint32_t kind) {
    const auto wait = reinterpret_cast<WaitFn>((*static_cast<void* const* const*>(waitable))[0]);
    const std::uint32_t timeout = WaitTime(kind);
    wait(waitable, nullptr, timeout, kThrowOnTimeout);
}

void CallWorld(CGameData* world, std::uintptr_t address) {
    reinterpret_cast<WorldFn>(address)(world, nullptr);
}

std::uint32_t CallThread(void* self, std::uintptr_t address) {
    return reinterpret_cast<ThreadFn>(address)(self, nullptr);
}

void PrepareDrawData(CLogicThread* self, bool requested) {
    reinterpret_cast<ThreadArgFn>(kPrepareDrawData)(self, nullptr, requested ? 1 : 0);
}

// PrepareDrawData between the update phases, unless the game's own
// fast-forward runs.
void PrepareDrawDataUnlessFastForward(CLogicThread* self) {
    if (g_stepCountdown == 0) {
        PrepareDrawData(self, false);
    }
}

// A message of `dwords` dwords to the network thread (signalled).
void PushToNet(CLogicThread* self, const std::uint8_t* bytes, std::int32_t dwords) {
    reinterpret_cast<PushFn>(kPush)(self->toNet, nullptr, bytes, dwords, kSignal);
}

// The message 0x21 `command` of two dwords with the local player in byte 4.
void PushPlayerMessage(CLogicThread* self, std::uint8_t command) {
    std::uint8_t message[8] = {kCommandMessage, command, kTwoDwordsBytes};
    message[4] = static_cast<std::uint8_t>(self->world->local_player);
    PushToNet(self, message, 2);
}

// Shows a narrow text as a message in the local player's colour: a
// std::string from the text through two temporaries, widened (0x60b840)
// into ShowMessage's by-value argument (which ShowMessage destroys), then
// the string erased and freed.
void ShowText(CLogicThread* self, const char* text) {
    String narrow{};
    framework::AssignThroughAssigns(&narrow, text, 0);
    const std::uint32_t colour = g_playerColourIndex[self->world->local_player];
    WString wide;
    using WidenFn = WString*(__fastcall*)(WString* out, const String* text);
    reinterpret_cast<WidenFn>(kWiden)(&wide, &narrow);
    using ShowFn = void(__fastcall*)(CLogicThread*, void*, WString, std::uint32_t);
    reinterpret_cast<ShowFn>(kShowMessage)(self, nullptr, wide, colour);
    framework::EraseAndFree(&narrow);
}

// 1. The first step of a built game: the minimap and the camera.
void FirstStepOfGame(CLogicThread* self) {
    WaitOn(g_graphicsDataLock.get(), kWaitGraphicsLock);
    CallWorld(self->world, kBuildMinimap);
    self->world->redraw = 0;
    self->world->camera_drawn_x = kCameraNotDrawn;
    self->world->camera_drawn_y = kCameraNotDrawn;
    self->world->minimap_ready = 1;
    self->world->refresh_ui = 1;
    CallWorld(self->world, kClampCamera);
    g_logicStartState = 0;
}

// 4. The wait for the next tick (or the game's own fast-forward).
void WaitForTick(CLogicThread* self) {
    if (g_stepCountdown == 0) {
        g_fastForwarding = 0;
        const std::uint32_t timeout = WaitTime(kWaitLogicTimer);
        g_lastSignalled = tooling::WaitForLogicTick(reinterpret_cast<tooling::CWin32WaitableBag*>(self->wait_set),
                                                    timeout, kThrowOnTimeout);
        return;
    }
    const std::uint32_t left = g_stepCountdown - 1;
    g_stepCountdown = left;
    if (left != 0) {
        g_fastForwarding = 1;
        return;
    }
    g_fastForwarding = 0;
    self->world->camera_drawn_x = kCameraNotDrawn;
    self->world->camera_drawn_y = kCameraNotDrawn;
    WaitOn(self->timer, kWaitLogicTimer);
}

// 6. While the world updates are off: paused, and the UI shows why.
void ShowWorldStopped(CLogicThread* self) {
    self->pause_state = kPausePaused;
    CGameData* world = self->world;
    std::int32_t mode = kUiModeGameOver;
    if (world->objectives_enabled != 0 && world->counters_disabled != 0) {
        mode = kUiModeObjectivesTimeout;
    } else if ((g_campaignMission == kMissionAmazonsA || g_campaignMission == kMissionAmazonsB) &&
               g_gameOutcome == kOutcomeWon) {
        mode = kUiModeMissionWon;
    }
    world->ui_selection_mode = mode;
    self->world->refresh_ui = 1;
}

// 7. The sync message every 256 ticks.
void SendSyncMessage(CLogicThread* self) {
    const std::uint32_t slot = (self->tick >> 8) & (kSyncSlots - 1);
    const std::uint16_t random = self->world->random_state[0];
    std::uint8_t message[12] = {kCommandMessage, kCommandSync, kThreeDwordsBytes};
    message[4] = static_cast<std::uint8_t>(slot >> 8);
    message[5] = static_cast<std::uint8_t>(slot);
    message[6] = static_cast<std::uint8_t>(random >> 8);
    message[7] = static_cast<std::uint8_t>(random);
    message[8] = static_cast<std::uint8_t>(self->world->local_player);
    g_syncRandomHistory[slot] = random;
    if (g_gameOutcome != kOutcomeLost) {
        PushToNet(self, message, 3);
    }
}

// 8. The pile claim ring: one slot cleared per tick.
void AdvancePileClaims(CLogicThread* self) {
    self->world->pile_claims[g_pileClaimsCursor].settler = 0;
    self->world->pile_claims[g_pileClaimsCursor].pile = 0;
    const auto cursor = static_cast<std::uint16_t>(g_pileClaimsCursor + 1);
    g_pileClaimsCursor = cursor;
    if (cursor >= kPileClaims) {
        g_pileClaimsCursor = 0;
    }
}

// 9. The reminder to save, every 30 minutes after the last save.
void RemindToSave(CLogicThread* self) {
    if (g_gameSettings->option2a != 0 || g_gameOutcome == kOutcomeLost) {
        return;
    }
    const std::uint32_t tick = g_gameTick;
    if (tick == 0 || tick == g_lastSaveTick || (tick - g_lastSaveTick) % kSaveReminderTicks != 0) {
        return;
    }
    g_global7deb50 = g_global7deb50 + 1;
    const std::int32_t minutes = g_global7deb50 * kSaveReminderMinutes;
    using TextFn = const char*(__fastcall*)(void* gfx, void* edx, std::uint32_t id, std::uint32_t language);
    const char* format = reinterpret_cast<TextFn>(kTextAddress)(g_gfxData.get(), nullptr, kTextSaveReminder,
                                                                kCurrentLanguage);
    char text[kTextBytes];
    reinterpret_cast<SprintfFn>(kSprintf)(text, format, minutes);
    ShowText(self, text);
}

// 10. The game objectives: the end of the game time, or the warning before.
void CheckObjectiveTime(CLogicThread* self) {
    CGameData* world = self->world;
    if (world->objectives_enabled == 0) {
        return;
    }
    if (world->counters_disabled != 0) {
        g_worldUpdateDisabled = 1;
        for (std::int16_t x = 0; x < static_cast<std::int32_t>(self->world->map_size); ++x) {
            for (std::int16_t y = 0; y < static_cast<std::int32_t>(self->world->map_size); ++y) {
                self->world->tiles[x][y].draw_flags = kTileDrawAllFog;
            }
        }
        self->world->minimap_ready = 1;
        for (std::uint32_t slice = 0; slice < kMinimapSlices; ++slice) {
            reinterpret_cast<WorldArgFn>(kUpdateMinimapSlice)(self->world, nullptr, slice);
        }
        CallWorld(self->world, kUpdateAllBorderFlags);
        return;
    }
    const std::uint32_t warningTick = (static_cast<std::uint32_t>(world->objective_timeout) * kMsPerMinute -
                                       static_cast<std::uint32_t>(world->end_warning_minutes) * kMsPerMinute) >>
                                      kTickShift;
    if (g_gameTick != warningTick) {
        return;
    }
    char text[kTextBytes];
    reinterpret_cast<SprintfFn>(kSprintf)(text, texts::kGameEndsInMinutes.c_str(), world->end_warning_minutes);
    ShowText(self, text);
}

// 11. The map reveal spell of the local player ends after 15 seconds.
void EndMapReveal(CLogicThread* self) {
    auto& player = self->world->players[self->world->local_player];
    if (player.map_revealed == 0) {
        return;
    }
    const std::uint32_t elapsedMs = (g_gameTick - static_cast<std::uint32_t>(player.map_revealed_tick)) * kMsPerTick;
    if (elapsedMs / 1000 < kRevealSeconds) {
        return;
    }
    player.map_revealed = 0;
    const std::uint32_t tick = g_gameTick;
    g_revealMap = 0;
    GameDebugLog(texts::kRevealMapTimeout, tick);
}

// 12. The world update phases and the computer players.
void UpdateWorld(CLogicThread* self) {
    PrepareDrawDataUnlessFastForward(self);
    if (g_simulationPaused != 0) {
        return;
    }
    CallWorld(self->world, kUpdateEnvironment);
    PrepareDrawDataUnlessFastForward(self);
    CallWorld(self->world, kUpdateEconomy);
    PrepareDrawDataUnlessFastForward(self);
    CallWorld(self->world, kUpdateSettlers);
    PrepareDrawDataUnlessFastForward(self);
    if (g_simulationPaused != 0) {
        return;
    }
    for (const std::uint32_t player : self->computer_players) {
        if (player != 0) {
            CallThread(reinterpret_cast<void*>(player), kComputerPlayerStep);
        }
    }
}

// 13. The game end check (and its message to the other players).
void CheckGameEnd(CLogicThread* self) {
    if (g_gameOutcome != kOutcomeOpen || g_worldUpdateDisabled != 0) {
        return;
    }
    const std::int32_t outcome = static_cast<std::int8_t>(CallThread(self, kCheckGameEnd));
    g_gameOutcome = outcome;
    if (outcome == kOutcomeLost) {
        PushPlayerMessage(self, kCommandLost);
    } else if (outcome == kOutcomeWon) {
        PushPlayerMessage(self, kCommandWonFinal);
    }
}

// 7..13: one tick of the simulation; it stops after ExecuteCommands when
// the step's commands weren't all there yet.
void RunTick(CLogicThread* self) {
    self->world->command_attempts += kCommandAttemptStep;
    if (CallThread(self, kExecuteCommands) != kCommandsDone) {
        return;
    }
    if (g_gameOutcome == kOutcomeWon) {
        const std::uint8_t message[8] = {kCommandMessage, kCommandGameWon, kTwoDwordsBytes};
        PushToNet(self, message, 2);
    }
    if ((self->tick & kSyncTickMask) == 0) {
        SendSyncMessage(self);
    }
    ++self->tick;
    AdvancePileClaims(self);
    RemindToSave(self);
    CheckObjectiveTime(self);
    EndMapReveal(self);
    UpdateWorld(self);
    if (g_campaignMission == kMissionTutorial) {
        CallThread(self, kUpdateTutorial);
    }
    CheckGameEnd(self);
}

}  // namespace

// Purpose: One iteration of the logic thread: waits for the next 64 ms
// tick and runs it (the players' commands, the world updates, the computer
// players, the game end check), and keeps the draw data and the on-screen
// messages up to date.
void __fastcall CLogicThread_Step_Impl(CLogicThread* self, void* /*edx*/) {
    if (g_logicStartState == kStartStateReady) {
        FirstStepOfGame(self);
    }
    if (self->start_state == kStartFinishInitialising) {
        CallWorld(self->world, kFinishInitialising);
        self->start_state = 0;
        self->started[0] = 1;
        self->started[1] = 1;
    } else if (self->start_state == kStartNoOp) {
        CallWorld(self->world, kNoOp);
        self->start_state = kStartAfterNoOp;
    }
    if (self->start_state != 0) {
        return;
    }
    self->step_time_previous = self->step_time;
    self->step_time = g_importTimeGetTime.get()();
    WaitForTick(self);
    CallThread(self, kProcessEvents);
    CallThread(self, kUpdateLobby);
    if (g_worldUpdateDisabled != 0) {
        ShowWorldStopped(self);
    }
    if (g_logicStartState == 0 && (g_lastSignalled == self->timer || g_fastForwarding != 0) &&
        self->pause_state != kPausePaused) {
        if (self->pause_state == kPauseOneTick) {
            self->pause_state = kPausePaused;
        }
        RunTick(self);
        g_gameTick = self->tick;
    }
    CallThread(self, kAgeMessages);
    if (g_stepCountdown == 0) {
        PrepareDrawData(self, g_lastSignalled == self->draw_request_event);
    }
}

// ---------------------------------------------------------------------------
// Tests: one world-sized buffer per side (zeroed once; the fields Step
// reads are randomized per case, the thread lies behind the world), every
// subsystem callee a recording stub (CalleeStubs; ECX relative to the
// world), the message queue's Push, NarrowToWideString and ShowMessage
// recording fakes (the message bytes Step writes, the text), the timer's
// and the semaphore's Wait a fake vtable, timeGetTime a fake clock, the
// globals Step uses set per case and restored afterwards. Compared: the
// thread, the world fields Step writes, the globals, the call logs. The
// fast-forward branch that reaches 0 uses a timer and draw-request event
// that are never "signalled" (the original's local is a stack leftover
// there, see 4. above).
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kThreadOffset = (sizeof(CGameData) + 0xfff) & ~static_cast<std::size_t>(0xfff);
constexpr std::size_t kObjectsOffset = kThreadOffset + 0x1000;  // the timer, the lock, the computer players
constexpr std::size_t kStepBufferBytes = kObjectsOffset + 0x1000;
constexpr std::int32_t kTestMapSize = 16;

struct StepLog {
    std::vector<std::uint32_t> events;  // Push / ShowMessage / Wait records, in order
    std::uintptr_t base = 0;
    std::uint32_t clock = 0;
};
StepLog* g_stepLog = nullptr;

std::uint32_t __stdcall FakeStepClock() {
    return g_stepLog->clock++;
}

void __fastcall FakePush(MessageQueue* queue, void*, const std::uint8_t* bytes, std::int32_t count,
                         std::uint32_t signal) {
    auto& e = g_stepLog->events;
    e.insert(e.end(), {0x5005u, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(queue)),
                       static_cast<std::uint32_t>(count), signal});
    // The bytes Step writes: 0..2 (0x21, the command, the size), then the
    // local player (byte 4 of 0xfc / 0xfd) or the sync data (4..8 of 0xfb).
    static constexpr int kWritten[] = {0, 1, 2, 4, 5, 6, 7, 8};
    const int written = count == 3 ? 8 : (bytes[1] == kCommandGameWon ? 3 : 4);
    for (int k = 0; k < written; ++k) {
        e.push_back(bytes[kWritten[k]]);
    }
}

// NarrowToWideString (fastcall: ECX the wide result, EDX the narrow
// string): the text's bytes as characters in a static buffer.
std::uint16_t g_wideText[0x100];
WString* __fastcall FakeWiden(WString* out, const String* narrow) {
    std::uint32_t n = 0;
    for (; narrow->ptr != nullptr && n < narrow->len && n + 1 < 0x100; ++n) {
        g_wideText[n] = static_cast<std::uint8_t>(narrow->ptr[n]);
    }
    g_wideText[n] = 0;
    *out = WString{};
    out->ptr = g_wideText;
    out->len = n;
    out->res = n;
    return out;
}

void __fastcall FakeShowMessage(CLogicThread* self, void*, WString text, std::uint32_t colour) {
    auto& e = g_stepLog->events;
    e.insert(e.end(), {0x5400u, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(self) - g_stepLog->base),
                       text.len, colour});
    for (std::uint32_t i = 0; i < text.len; ++i) {
        e.push_back(text.ptr[i]);
    }
}

std::uint32_t __fastcall FakeWait(void* self, void*, std::uint32_t timeout, std::uint32_t flag) {
    g_stepLog->events.insert(g_stepLog->events.end(),
                             {0x5a17u, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(self) -
                                                                  g_stepLog->base),
                              timeout, flag});
    return 1;
}
void* const g_fakeWaitVtable[1] = {reinterpret_cast<void*>(&FakeWait)};

// The S3.EXE globals Step reads or writes.
struct StepGlobals {
    std::uint32_t startState, countdown;
    std::int32_t fastForwarding;
    std::uint8_t worldUpdateDisabled;
    std::int32_t mission, outcome;
    std::uint16_t paused;
    std::uint32_t tick, lastSave;
    std::int32_t reminders;
    std::uint8_t option2a;
    std::uint16_t cursor;
    void* lock;
    std::uint16_t history[32];
    std::int32_t revealMap;

    static StepGlobals Read() {
        StepGlobals g{};
        g.startState = g_logicStartState;
        g.countdown = g_stepCountdown;
        g.fastForwarding = g_fastForwarding;
        g.worldUpdateDisabled = g_worldUpdateDisabled;
        g.mission = g_campaignMission;
        g.outcome = g_gameOutcome;
        g.paused = g_simulationPaused;
        g.tick = g_gameTick;
        g.lastSave = g_lastSaveTick;
        g.reminders = g_global7deb50;
        g.option2a = g_gameSettings->option2a;
        g.cursor = g_pileClaimsCursor;
        g.lock = g_graphicsDataLock;
        std::memcpy(g.history, &g_syncRandomHistory[0], sizeof(g.history));
        g.revealMap = g_revealMap;
        return g;
    }
    void Write() const {
        g_logicStartState = startState;
        g_stepCountdown = countdown;
        g_fastForwarding = fastForwarding;
        g_worldUpdateDisabled = worldUpdateDisabled;
        g_campaignMission = mission;
        g_gameOutcome = outcome;
        g_simulationPaused = paused;
        g_gameTick = tick;
        g_lastSaveTick = lastSave;
        g_global7deb50 = reminders;
        g_gameSettings->option2a = option2a;
        g_pileClaimsCursor = cursor;
        g_graphicsDataLock = lock;
        std::memcpy(&g_syncRandomHistory[0], history, sizeof(history));
        g_revealMap = revealMap;
    }
    std::vector<std::uint8_t> Bytes() const {
        // (the lock pointer differs between the sides)
        StepGlobals copy = *this;
        copy.lock = nullptr;
        const auto* p = reinterpret_cast<const std::uint8_t*>(&copy);
        return std::vector<std::uint8_t>(p, p + sizeof(copy));
    }
};

// One case's inputs.
struct StepCase {
    std::vector<std::uint8_t> thread;  // the CLogicThread image
    StepGlobals globals{};
    std::int32_t player = 0;
    std::uint32_t mapSize = 0;
    std::uint8_t objectives = 0, countersOff = 0, revealed = 0;
    std::int32_t timeout = 0, warning = 0, revealTick = 0;
    std::uint16_t random = 0;
    std::uint32_t seed = 0;
    bool timerIsWaitSet = false, requestIsWaitSet = false;
    std::uint32_t computers = 0;  // bit p: computer player p exists
};

StepCase RandomStepCase(test::Rng& rng) {
    StepCase c;
    c.thread.resize(sizeof(CLogicThread));
    rng.Fill(c.thread.data(), c.thread.size());
    auto* t = reinterpret_cast<CLogicThread*>(c.thread.data());
    static const std::uint32_t kTicks[] = {0, 0x100, 0x3f00, 0x12345600};
    t->tick = rng.Chance(1, 2) ? kTicks[rng.Below(4)] : rng.Next32();
    t->pause_state = rng.Chance(3, 4) ? rng.Below(3) : rng.Next32();
    t->start_state = rng.Chance(3, 4) ? 0 : rng.Below(4);
    StepGlobals& g = c.globals;
    static const std::uint32_t kCountdowns[] = {0, 0, 0, 0, 1, 2, 881};
    g.countdown = kCountdowns[rng.Below(7)];
    g.startState = rng.Chance(1, 6) ? 250 : (rng.Chance(1, 10) ? 1 : 0);
    g.fastForwarding = static_cast<std::int32_t>(rng.Below(2));
    g.worldUpdateDisabled = static_cast<std::uint8_t>(rng.Chance(1, 4));
    static const std::int32_t kMissions[] = {0, 0, 68, 69, 99, 70};
    g.mission = kMissions[rng.Below(6)];
    static const std::int32_t kOutcomes[] = {-1, -1, -1, 0, 1, 2};
    g.outcome = kOutcomes[rng.Below(6)];
    g.paused = static_cast<std::uint16_t>(rng.Chance(1, 4));
    c.timeout = static_cast<std::int32_t>(rng.Below(200));
    c.warning = static_cast<std::int32_t>(rng.Below(20));
    static const std::uint32_t kGameTicks[] = {0, 28125, 56250};
    g.tick = rng.Chance(1, 2) ? kGameTicks[rng.Below(3)] : rng.Below(100000);
    if (rng.Chance(1, 3)) {
        g.tick = (static_cast<std::uint32_t>(c.timeout) * 60000 - static_cast<std::uint32_t>(c.warning) * 60000) >> 6;
    }
    static const std::uint32_t kSinceSave[] = {0, 28125, 56250, 1};
    g.lastSave = g.tick - (rng.Chance(3, 4) ? kSinceSave[rng.Below(4)] : rng.Below(100000));
    g.reminders = static_cast<std::int32_t>(rng.Below(5));
    g.option2a = static_cast<std::uint8_t>(rng.Chance(1, 3));
    g.cursor = static_cast<std::uint16_t>(rng.Chance(1, 3) ? 127 : rng.Below(128));
    for (auto& word : g.history) {
        word = static_cast<std::uint16_t>(rng.Next32());
    }
    g.revealMap = static_cast<std::int32_t>(rng.Next32());
    c.player = static_cast<std::int32_t>(rng.Below(20));
    c.mapSize = rng.Below(kTestMapSize + 1);
    c.objectives = static_cast<std::uint8_t>(rng.Chance(1, 2) ? rng.Below(2) : rng.Next32());
    c.countersOff = static_cast<std::uint8_t>(rng.Chance(1, 4));
    c.revealed = static_cast<std::uint8_t>(rng.Below(2));
    static const std::uint32_t kAges[] = {0, 1, 234, 235, 236};
    c.revealTick = static_cast<std::int32_t>(g.tick - (rng.Chance(3, 4) ? kAges[rng.Below(5)] : rng.Below(1000)));
    c.random = static_cast<std::uint16_t>(rng.Next32());
    c.seed = rng.Next32();
    const bool signalled = g.countdown != 1;
    c.timerIsWaitSet = signalled && rng.Chance(7, 10);
    c.requestIsWaitSet = signalled && rng.Chance(3, 10);
    c.computers = rng.Chance(1, 2) ? 0 : rng.Next32() & 0xfffff;
    return c;
}

struct StepOutcome {
    std::vector<std::uint8_t> thread;
    std::vector<std::uint8_t> world;  // the world fields Step writes
    std::vector<std::uint8_t> globals;
    std::vector<test::StubCall> calls;
    std::vector<std::uint32_t> events;
};

constexpr std::uint32_t kStubExecuteCommands = 0x5ec0;

// Runs `step` on the case in `buffer` (a world-sized buffer, the thread at
// kThreadOffset).
StepOutcome RunStep(const StepCase& c, StepFn step, std::uint8_t* buffer) {
    CGameData* world = reinterpret_cast<CGameData*>(buffer);
    auto* thread = reinterpret_cast<CLogicThread*>(buffer + kThreadOffset);
    std::memcpy(thread, c.thread.data(), c.thread.size());
    thread->world = world;
    std::uint8_t* objects = buffer + kObjectsOffset;
    void** timer = reinterpret_cast<void**>(objects);
    void** lock = reinterpret_cast<void**>(objects + 0x10);
    *timer = const_cast<void**>(g_fakeWaitVtable);
    *lock = const_cast<void**>(g_fakeWaitVtable);
    *reinterpret_cast<void**>(thread->wait_set) = const_cast<void**>(g_fakeWaitVtable);
    thread->timer = c.timerIsWaitSet ? static_cast<void*>(thread->wait_set) : static_cast<void*>(timer);
    thread->draw_request_event = c.requestIsWaitSet ? static_cast<void*>(thread->wait_set) : static_cast<void*>(lock);
    thread->toNet = reinterpret_cast<MessageQueue*>(objects + 0x20);
    for (std::uint32_t p = 0; p < 20; ++p) {
        thread->computer_players[p] =
            (c.computers >> p) & 1 ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(objects + 0x100 + p))
                                   : 0;
    }
    world->objectives_enabled = c.objectives;
    world->counters_disabled = c.countersOff;
    world->map_size = c.mapSize;
    world->local_player = c.player;
    world->objective_timeout = c.timeout;
    world->end_warning_minutes = c.warning;
    world->random_state[0] = c.random;
    auto& player = world->players[c.player];
    player.map_revealed = c.revealed;
    player.map_revealed_tick = c.revealTick;
    // The fields Step writes start from the same random values.
    test::Rng rng(c.seed);
    world->redraw = static_cast<std::int32_t>(rng.Next32());
    world->camera_drawn_x = static_cast<std::int32_t>(rng.Next32());
    world->camera_drawn_y = static_cast<std::int32_t>(rng.Next32());
    world->minimap_ready = static_cast<std::int32_t>(rng.Next32());
    world->refresh_ui = static_cast<std::int32_t>(rng.Next32());
    world->ui_selection_mode = static_cast<std::int32_t>(rng.Next32());
    world->command_attempts = static_cast<std::int32_t>(rng.Next32());
    rng.Fill(world->pile_claims, sizeof(world->pile_claims));
    for (int x = 0; x < kTestMapSize; ++x) {
        for (int y = 0; y < kTestMapSize; ++y) {
            world->tiles[x][y].draw_flags = static_cast<std::uint8_t>(rng.Next32());
        }
    }
    StepGlobals globals = c.globals;
    globals.lock = lock;
    globals.Write();

    StepLog log;
    log.base = reinterpret_cast<std::uintptr_t>(buffer);
    log.clock = c.seed;
    g_stepLog = &log;
    test::CalleeStubs stubs;
    stubs.SetWorld(world);
    static const char kFormat[] = "%d min";
    stubs.Install<0, kBuildMinimap>();
    stubs.Install<0, kClampCamera>();
    stubs.Install<0, kFinishInitialising>();
    stubs.Install<2, 0x00463100>([](CGameData* w, std::uint32_t self, const std::uint32_t*) {
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(w)) + self;
    });
    stubs.Install<0, kProcessEvents>();
    stubs.Install<0, kUpdateLobby>();
    const std::uint32_t commands = c.seed % 4 != 0 ? 1 : c.seed;
    stubs.Install<0, kExecuteCommands>([commands](CGameData*, std::uint32_t, const std::uint32_t*) { return commands; });
    stubs.Install<2, kTextAddress>([](CGameData*, std::uint32_t, const std::uint32_t*) {
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(kFormat));
    });
    stubs.Install<1, kUpdateMinimapSlice>();
    stubs.Install<0, kUpdateAllBorderFlags>();
    stubs.Install<1, kPrepareDrawData>();
    stubs.Install<0, kUpdateEnvironment>();
    stubs.Install<0, kUpdateEconomy>();
    stubs.Install<0, kUpdateSettlers>();
    stubs.Install<0, kComputerPlayerStep>();
    stubs.Install<0, kUpdateTutorial>();
    const std::uint32_t outcome = (c.seed & 0xffffff00u) | static_cast<std::uint8_t>(c.seed % 3 == 0 ? 0xff : c.seed % 3);
    stubs.Install<0, kCheckGameEnd>([outcome](CGameData*, std::uint32_t, const std::uint32_t*) { return outcome; });
    stubs.Install<0, kAgeMessages>();
    test::ScopedJump push{kPush, reinterpret_cast<const void*>(&FakePush)};
    test::ScopedJump widen{kWiden, reinterpret_cast<const void*>(&FakeWiden)};
    test::ScopedJump show{kShowMessage, reinterpret_cast<const void*>(&FakeShowMessage)};
    const void* clock = reinterpret_cast<const void*>(&FakeStepClock);
    test::PatchBytes(reinterpret_cast<std::uintptr_t>(&g_importTimeGetTime), &clock, sizeof(clock));

    step(thread, nullptr);

    test::UnpatchBytes(reinterpret_cast<std::uintptr_t>(&g_importTimeGetTime));
    g_stepLog = nullptr;
    StepOutcome out;
    out.thread.assign(reinterpret_cast<std::uint8_t*>(thread), reinterpret_cast<std::uint8_t*>(thread) + c.thread.size());
    auto* image = reinterpret_cast<CLogicThread*>(out.thread.data());
    image->world = nullptr;  // (pointers into the side's buffer)
    image->timer = nullptr;
    image->draw_request_event = nullptr;
    image->toNet = nullptr;
    std::memset(image->computer_players, 0, sizeof(image->computer_players));
    auto add = [&out](const void* p, std::size_t n) {
        const auto* bytes = static_cast<const std::uint8_t*>(p);
        out.world.insert(out.world.end(), bytes, bytes + n);
    };
    add(&world->redraw, 4);
    add(&world->camera_drawn_x, 8);
    add(&world->minimap_ready, 4);
    add(&world->refresh_ui, 4);
    add(&world->ui_selection_mode, 4);
    add(&world->command_attempts, 4);
    add(world->pile_claims, sizeof(world->pile_claims));
    add(&player.map_revealed, 1);
    for (int x = 0; x < kTestMapSize; ++x) {
        for (int y = 0; y < kTestMapSize; ++y) {
            add(&world->tiles[x][y].draw_flags, 1);
        }
    }
    out.globals = StepGlobals::Read().Bytes();
    out.calls = stubs.Take();
    for (test::StubCall& call : out.calls) {
        if (call.callee == kTextAddress) {
            call.self = 0;  // (g_gfxData, not in the side's buffer)
        }
    }
    out.events = std::move(log.events);
    // The queue pointer of the Push records (into the side's buffer).
    for (std::size_t i = 0; i + 3 < out.events.size(); ++i) {
        if (out.events[i] == 0x5005u) {
            out.events[i + 1] -= static_cast<std::uint32_t>(log.base);
        }
    }
    return out;
}

StepFn OriginalStep() {
    std::uintptr_t address = 0;
    const void* original = nullptr;
    if (!hooks::FindHook("CLogicThread_Step", &address, &original)) {
        return nullptr;
    }
    return reinterpret_cast<StepFn>(const_cast<void*>(original));
}

void CompareStep(test::Context& ctx, StepFn candidate, int cases) {
    const StepFn original = OriginalStep();
    if (!S3RE_EXPECT(original != nullptr)) {
        return;
    }
    test::WorldBuffer a(kStepBufferBytes), b(kStepBufferBytes);
    const StepGlobals saved = StepGlobals::Read();
    for (int i = 0; i < cases; ++i) {
        const StepCase c = RandomStepCase(ctx.rng());
        const StepOutcome expected = RunStep(c, original, a.data());
        const StepOutcome actual = RunStep(c, candidate, b.data());
        bool same = S3RE_EXPECT(expected.thread == actual.thread);
        same &= S3RE_EXPECT(expected.world == actual.world);
        same &= S3RE_EXPECT(expected.globals == actual.globals);
        same &= S3RE_EXPECT(expected.calls == actual.calls);
        same &= S3RE_EXPECT(expected.events == actual.events);
        if (!same) {
            for (std::size_t k = 0; k < expected.calls.size() || k < actual.calls.size(); ++k) {
                const test::StubCall none{};
                const test::StubCall& e = k < expected.calls.size() ? expected.calls[k] : none;
                const test::StubCall& o = k < actual.calls.size() ? actual.calls[k] : none;
                if (!(e == o)) {
                    ctx.Fail(__FILE__, __LINE__, "  call %u: 0x%x(0x%x; 0x%x 0x%x) vs 0x%x(0x%x; 0x%x 0x%x)",
                             static_cast<unsigned>(k), static_cast<unsigned>(e.callee), e.self, e.args[0], e.args[1],
                             static_cast<unsigned>(o.callee), o.self, o.args[0], o.args[1]);
                    break;
                }
            }
            for (std::size_t k = 0; k < expected.events.size() || k < actual.events.size(); ++k) {
                const std::uint32_t e = k < expected.events.size() ? expected.events[k] : 0xdeadu;
                const std::uint32_t o = k < actual.events.size() ? actual.events[k] : 0xdeadu;
                if (e != o) {
                    ctx.Fail(__FILE__, __LINE__, "  event %u: 0x%x vs 0x%x (of %u / %u)", static_cast<unsigned>(k), e,
                             o, static_cast<unsigned>(expected.events.size()),
                             static_cast<unsigned>(actual.events.size()));
                    break;
                }
            }
            ctx.Fail(__FILE__, __LINE__, "  case %d: countdown %u, start %u/%u, pause %u, outcome %d", i,
                     c.globals.countdown, c.globals.startState, reinterpret_cast<const CLogicThread*>(c.thread.data())->start_state,
                     reinterpret_cast<const CLogicThread*>(c.thread.data())->pause_state, c.globals.outcome);
            break;
        }
    }
    saved.Write();
}

// Wrong: a won campaign mission 68/69 shows the plain game-over mode.
void __fastcall WrongStep(CLogicThread* self, void* edx) {
    CLogicThread_Step_Impl(self, edx);
    if (self->world->ui_selection_mode == kUiModeMissionWon) {
        self->world->ui_selection_mode = kUiModeGameOver;
    }
}

}  // namespace

S3RE_TEST(CLogicThread_Step_MatchesOriginal) {
    CompareStep(ctx, &CLogicThread_Step_Impl, 3000);
}

S3RE_TEST(CLogicThread_Step_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareStep(probe, &WrongStep, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
