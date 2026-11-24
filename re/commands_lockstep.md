# Commands and lockstep

Moved out of `CLAUDE.md` on 2026-11-24.

How player commands reach the simulation (verified by recording and
replaying, see `src/s3re/tooling/replay/command_log.h`):

- The logic thread talks to a network thread through two `MessageQueue`s
  (ring buffers of dwords; `Push` `0x4309c0`, `Pop` `0x430980`): it pushes
  on `CLogicThread+0x3c4` and reads replies from `CLogicThread+0x3c8`.
  Messages start with a type byte; byte 2 is the size in bytes.
- Commands (player input, computer players, sync checks) are pushed as type
  `0x21` messages, byte 1 = command id. Each ExecuteStep,
  `CLogicThread::ExecuteCommands` (`0x535990`) pushes a `0x20` step message.
- In single player the network thread is `CNetBounceThread`
  (`0x46f6f0`): it sends the commands back in order (as type 1), batched
  with the reply to the next step message, plus a type 2 message that allows
  the logic to run up to step+5. `ExecuteCommands` executes the batch.
  Multiplayer uses `CNetThread` (commands become 0x12 "announce" packets
  with an ExecuteStep, `CNetThread::OnNetAnnounce` `0x46cbd0`).
- So the order of 0x21 and 0x20 messages on the logic->net queue fully
  determines which commands run at which step. Our recorder keys each
  command by the number of 0x20 messages before it.
- Bytes 9..11 of 12-byte command messages are often uninitialized stack
  memory (they differ between runs); the simulation doesn't depend on them
  as far as tested.
- The game's own `-record`/`-play` (`Siedler3.evt`, class `CEventFile`)
  record the same commands, but `-play` adds the file's commands to the live
  ones instead of replacing them, so computer-player commands run twice and
  the replay diverges. Don't use it for verification.
