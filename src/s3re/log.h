#pragma once

#include <windows.h>

namespace s3re::log {

// Opens s3re.log in the same directory as the given module (our DLL).
void Open(HMODULE module);
void Close();

// printf-style, timestamped, thread-safe; each line is flushed immediately so
// the log survives a crash of the game.
void Write(const char* format, ...);

}  // namespace s3re::log

#define S3RE_LOG(...) ::s3re::log::Write(__VA_ARGS__)
