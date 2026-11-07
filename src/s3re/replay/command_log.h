#pragma once

// Recording and replaying player commands (system-level verification).
//
// How commands reach the simulation (single player; see "Commands and
// lockstep" in CLAUDE.md): the logic thread pushes its messages to the
// network side on the queue at CLogicThread+0x3c4. Command messages (type
// 0x21: player input, computer players, sync checks) and one step message
// (type 0x20) per ExecuteStep share that FIFO queue. CNetBounceThread sends
// every command back in order, batched with the reply to the next step
// message, and CLogicThread::ExecuteCommands executes them. So the order of
// 0x21 and 0x20 messages on that queue fully determines which commands run
// at which step.
//
// Recording (`s3re_launcher --record-commands FILE`, S3RE_RECORD_COMMANDS):
// every 0x21 message is written with its slot, the number of 0x20 messages
// pushed before it.
//
// Replaying (`--replay-commands FILE`, S3RE_REPLAY_COMMANDS): live 0x21
// messages are dropped, and the recorded ones are pushed just before the
// 0x20 message of their slot. Live messages are still compared with the
// recording; without human input they must match exactly (a mismatch means
// the simulation has diverged), and they are logged.
//
// File format: src/common/recording_format.h. A recording starts with the
// game's seed, random-race setting and map; a replay checks them against
// the running game and stops (exit code 3) if they differ. The launcher
// takes seed and map from the recording, so `--replay-commands FILE` alone
// starts the right game.

#include <cstdint>

namespace s3re::replay {

// Called by the CLogicThread::Step hook with the logic thread object.
void SetLogicThread(void* logicThread);

// Ends recording/replaying cleanly and logs a summary.
void CloseCommandLog();

}  // namespace s3re::replay
