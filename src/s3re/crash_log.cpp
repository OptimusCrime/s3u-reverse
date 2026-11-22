// Crash logging: a vectored exception handler that writes the details of
// access violations in game code to s3re.log, before the game's own handler
// shows its "Structured Exception ... occurred" box (which tells only the
// address). It only observes; the exception continues to the game's
// handlers.
//
// Logged: thread, EIP, registers, the faulting access, what the address
// belongs to (VirtualQuery), the bytes around ESI/EDI/EBX/EBP, the EBP frame
// chain and every dword on the stack that looks like a return address into
// S3.EXE (the callers).

#include "crash_log.h"

#include "log.h"

#include <windows.h>

#include <atomic>
#include <cstdint>

namespace s3re::crash {

namespace {

constexpr std::uintptr_t kGameCodeBegin = 0x00401000;
constexpr std::uintptr_t kGameCodeEnd = 0x0063a000;  // end of .text (approx.); raw-ok: section bound
constexpr int kMaxLogged = 8;  // the game might use access violations on purpose
constexpr int kStackDwords = 256;

std::atomic<int> g_logged{0};

bool InGameCode(std::uintptr_t address) {
    return address >= kGameCodeBegin && address < kGameCodeEnd;
}

bool Readable(std::uintptr_t address, std::size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) == 0) {
        return false;
    }
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                           PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    const auto end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    return info.State == MEM_COMMIT && (info.Protect & readable) != 0 &&
           (info.Protect & PAGE_GUARD) == 0 && address + size <= end;
}

void LogRegion(const char* what, std::uintptr_t address) {
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) == 0) {
        S3RE_LOG("crash:   %s %08x: VirtualQuery failed", what, static_cast<unsigned>(address));
        return;
    }
    char module[MAX_PATH] = "-";
    if (info.Type == MEM_IMAGE) {
        GetModuleFileNameA(static_cast<HMODULE>(info.AllocationBase), module, MAX_PATH);
    }
    S3RE_LOG("crash:   %s %08x: allocation %p, region %p+%lx, state %lx, protect %lx, type %lx %s",
             what, static_cast<unsigned>(address), info.AllocationBase, info.BaseAddress,
             static_cast<unsigned long>(info.RegionSize), info.State, info.Protect, info.Type,
             module);
}

void LogBytes(const char* what, std::uintptr_t address) {
    const std::uintptr_t start = address - 0x10;
    if (!Readable(start, 0x40)) {
        S3RE_LOG("crash:   [%s=%08x] not readable", what, static_cast<unsigned>(address));
        return;
    }
    const auto* d = reinterpret_cast<const std::uint32_t*>(start);
    for (int row = 0; row < 4; ++row) {
        S3RE_LOG("crash:   [%s%+03x] %08x %08x %08x %08x", what, row * 16 - 0x10, d[row * 4],
                 d[row * 4 + 1], d[row * 4 + 2], d[row * 4 + 3]);
    }
}

LONG CALLBACK OnException(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    const CONTEXT* c = info->ContextRecord;
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || !InGameCode(c->Eip) ||
        g_logged.fetch_add(1) >= kMaxLogged) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const auto kind = record->NumberParameters >= 2 ? record->ExceptionInformation[0] : 99;
    const auto target = record->NumberParameters >= 2 ? record->ExceptionInformation[1] : 0;
    S3RE_LOG("crash: access violation (%s %08x) at eip %08lx, thread %lu",
             kind == 0 ? "read" : kind == 1 ? "write" : kind == 8 ? "execute" : "?",
             static_cast<unsigned>(target), c->Eip, GetCurrentThreadId());
    S3RE_LOG("crash:   eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx ebp %08lx "
             "esp %08lx",
             c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    LogRegion("target", target);
    LogBytes("esi", c->Esi);
    LogBytes("edi", c->Edi);
    LogBytes("ebx", c->Ebx);
    LogBytes("ebp", c->Ebp);

    // EBP chain (MSVC 6 code mostly keeps frame pointers in larger functions).
    std::uintptr_t frame = c->Ebp;
    for (int i = 0; i < 16 && Readable(frame, 8); ++i) {
        const auto* f = reinterpret_cast<const std::uint32_t*>(frame);
        S3RE_LOG("crash:   frame %08x: return %08x", static_cast<unsigned>(frame), f[1]);
        if (f[0] <= frame) {
            break;
        }
        frame = f[0];
    }
    // Stack scan: return-address candidates (the dword before them is a call).
    for (int i = 0; i < kStackDwords; ++i) {
        const std::uintptr_t slot = c->Esp + i * 4;
        if (!Readable(slot, 4)) {
            break;
        }
        const std::uint32_t value = *reinterpret_cast<const std::uint32_t*>(slot);
        if (InGameCode(value) && Readable(value - 5, 5) &&
            *reinterpret_cast<const std::uint8_t*>(value - 5) == 0xE8) {
            S3RE_LOG("crash:   stack esp+%03x: %08x", i * 4, value);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void Install() {
    AddVectoredExceptionHandler(1, OnException);
}

}  // namespace s3re::crash
