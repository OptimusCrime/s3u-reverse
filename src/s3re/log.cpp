#include "log.h"

#include <share.h>

#include <cstdarg>
#include <cstdio>

namespace s3re::log {
namespace {

FILE* g_file = nullptr;
CRITICAL_SECTION g_lock;

}  // namespace

void Open(HMODULE module) {
    InitializeCriticalSection(&g_lock);

    wchar_t path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return;
    }
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash == nullptr || (slash - path) + 10 >= MAX_PATH) {
        return;
    }
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"s3re.log");

    // Shared read so the log can be inspected while the game is running.
    g_file = _wfsopen(path, L"w", _SH_DENYWR);
}

void Close() {
    if (g_file != nullptr) {
        EnterCriticalSection(&g_lock);
        fclose(g_file);
        g_file = nullptr;
        LeaveCriticalSection(&g_lock);
    }
}

void Write(const char* format, ...) {
    if (g_file == nullptr) {
        return;
    }
    SYSTEMTIME now;
    GetLocalTime(&now);

    EnterCriticalSection(&g_lock);
    fprintf(g_file, "%02u:%02u:%02u.%03u [%5lu] ", now.wHour, now.wMinute, now.wSecond,
            now.wMilliseconds, GetCurrentThreadId());
    va_list args;
    va_start(args, format);
    vfprintf(g_file, format, args);
    va_end(args);
    fputc('\n', g_file);
    fflush(g_file);
    LeaveCriticalSection(&g_lock);
}

}  // namespace s3re::log
