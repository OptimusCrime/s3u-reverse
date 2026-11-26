#pragma once

// Logs access violations in game code to s3re.log (see crash_log.cpp).

namespace s3re::crash {

void Install();

// Logs EIP and the return addresses on the stack of every other thread of
// the process (for a shutdown that hangs).
void LogAllThreadStacks(const char* why);

}  // namespace s3re::crash
