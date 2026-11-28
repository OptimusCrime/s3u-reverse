#pragma once

// MessageQueue::Pop (0x00430980) and Push (0x004309c0), implemented in
// message_queue.cpp. Push is installed by the command recorder's hook
// (tooling/replay/command_log.cpp, S3RE_HOOK_ALWAYS), which calls ours
// outside original mode.

#include "game/logic/logic_thread_layout.h"

#include <cstdint>

namespace s3re::game {

std::uint32_t* __fastcall MessageQueue_Pop(MessageQueue* self, void* edx);
void __fastcall MessageQueue_Push(MessageQueue* self, void* edx, const std::uint32_t* dwords, std::int32_t count,
                                  std::uint32_t signal);

}  // namespace s3re::game
