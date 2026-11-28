#pragma once

// Our CLogicThread::Step (0x53db30), logic_step.cpp. The hook that runs it
// is the tooling wrapper in logic_thread.cpp.

#include "game/logic/logic_thread_layout.h"

namespace s3re::game {

void __fastcall CLogicThread_Step_Impl(CLogicThread* self, void* edx);

}  // namespace s3re::game
