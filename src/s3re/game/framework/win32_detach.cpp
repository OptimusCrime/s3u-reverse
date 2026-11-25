// Win32::DetachMemory (Win32\Detach.cpp): makes every page of a memory
// range a private copy, so that a file mapped copy-on-write (the graphics
// and sound files when the game is started from CD, "detach") no longer
// needs the medium.
//
// Win32::DetachMemory (0x0061ab60): fastcall (ECX = start, EDX = size).
//   The page size comes from g_detachPageSize (GetSystemInfo on the first
//   call). Walks the range region by region with VirtualQuery (28 bytes;
//   any other result: ThrowHResult("error check", E_UNEXPECTED, line 159)).
//   A region ends at min(base + RegionSize, start + size). Committed
//   regions that are neither guard pages nor PAGE_NOACCESS: unless they
//   are already PAGE_WRITECOPY / PAGE_EXECUTE_WRITECOPY, VirtualProtect to
//   PAGE_EXECUTE_READWRITE (failure: ThrowWin32Error("false", line 123));
//   if the old protection it reports differs from VirtualQuery's, the old
//   one is put back and ThrowHResult(line 119). Then one dword per page,
//   from the region's base, is read and written back (that write makes a
//   copy-on-write page private), and the protection is restored
//   (failure: ThrowWin32Error, line 149). EAX = 0.
//   Quirk kept: a region that isn't committed doesn't advance the walk, so
//   it would loop forever (mapped views are always committed).
//
// The throwers (0x610de0, 0x611050; Win32\Classes.cpp) never return; the
// original's code after them leaves the function, ours returns as well.
// Both get an empty std::string as their message; the original's has an
// allocator byte from an uninitialized local, ours 0 (the string is empty
// and its allocator is never read; the exception copies it on the way to a
// fatal error report).

#include "game/globals.h"
#include "game/texts.h"
#include "hooks.h"
#include "msvc6/string.h"

#include <windows.h>

#include <cstdint>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kThrowWin32Error = 0x00610de0;
constexpr std::uintptr_t kThrowHResult = 0x00611050;

constexpr std::uint32_t kUnexpected = 0x8000ffff;  // E_UNEXPECTED

// Source lines of the checks (Win32\Detach.cpp).
constexpr std::uint32_t kLineProtectChanged = 119;
constexpr std::uint32_t kLineProtect = 123;
constexpr std::uint32_t kLineRestore = 149;
constexpr std::uint32_t kLineQuery = 159;

using ThrowWin32ErrorFn = void(__fastcall*)(const char* expression, const char* file, std::uint32_t line,
                                            std::uint32_t error, const msvc6::String* message);
using ThrowHResultFn = void(__fastcall*)(const char* expression, const char* file, std::uint32_t line,
                                         std::uint32_t result, const msvc6::String* message);

void ThrowWin32Error(std::uint32_t line) {
    const msvc6::String message{};
    const std::uint32_t error = g_importGetLastError.get()();
    reinterpret_cast<ThrowWin32ErrorFn>(kThrowWin32Error)(texts::kFalse, texts::kDetachFile, line, error, &message);
}

void ThrowUnexpected(std::uint32_t line) {
    const msvc6::String message{};
    reinterpret_cast<ThrowHResultFn>(kThrowHResult)(texts::kErrorCheck, texts::kDetachFile, line, kUnexpected,
                                                    &message);
}

// One read and write per page: a copy-on-write page becomes private.
void TouchPages(std::uint32_t begin, std::uint32_t end) {
    for (std::uint32_t page = begin; page < end; page += g_detachPageSize.get()) {
        volatile std::uint32_t* const dword = reinterpret_cast<volatile std::uint32_t*>(page);
        *dword = *dword;
    }
}

}  // namespace

std::uint32_t __fastcall Win32_DetachMemory(std::uint32_t start, std::uint32_t size);
using DetachMemoryFn = decltype(&Win32_DetachMemory);

S3RE_HOOK(0x0061ab60, Win32_DetachMemory, DetachMemoryFn, Original_Win32_DetachMemory, Win32_DetachMemory);

// Purpose: copies a mapped file's pages into private memory, so the game
// keeps running when its CD is taken out ("detach" option of the graphics
// and sound loaders).
std::uint32_t __fastcall Win32_DetachMemory(std::uint32_t start, std::uint32_t size) {
    if (g_detachPageSize.get() == 0) {
        SYSTEM_INFO info;
        g_importGetSystemInfo.get()(&info);
        g_detachPageSize.get() = info.dwPageSize;
    }
    const std::uint32_t end = start + size;
    std::uint32_t address = start;
    while (address < end) {
        MEMORY_BASIC_INFORMATION region;
        if (g_importVirtualQuery.get()(reinterpret_cast<const void*>(address), &region, sizeof(region)) !=
            sizeof(region)) {
            ThrowUnexpected(kLineQuery);
            return 0;
        }
        const std::uint32_t base = reinterpret_cast<std::uint32_t>(region.BaseAddress);
        std::uint32_t regionEnd = base + region.RegionSize;
        if (regionEnd > end) {
            regionEnd = end;
        }
        if (region.State != MEM_COMMIT) {
            continue;  // (original bug: the walk doesn't advance)
        }
        const std::uint32_t protect = region.Protect;
        if ((protect & PAGE_GUARD) == 0 && protect != PAGE_NOACCESS) {
            const bool copyOnWrite = protect == PAGE_WRITECOPY || protect == PAGE_EXECUTE_WRITECOPY;
            if (!copyOnWrite) {
                std::uint32_t old = 0;
                if (g_importVirtualProtect.get()(region.BaseAddress, regionEnd - base, PAGE_EXECUTE_READWRITE,
                                                 &old) == 0) {
                    ThrowWin32Error(kLineProtect);
                    return 0;
                }
                if (old != region.Protect) {
                    g_importVirtualProtect.get()(region.BaseAddress, regionEnd - base, old, &old);
                    ThrowUnexpected(kLineProtectChanged);
                    return 0;
                }
            }
            TouchPages(base, regionEnd);
            if (!copyOnWrite) {
                std::uint32_t old = 0;
                if (g_importVirtualProtect.get()(region.BaseAddress, regionEnd - base, region.Protect, &old) ==
                    0) {
                    ThrowWin32Error(kLineRestore);
                    return 0;
                }
            }
        }
        address = regionEnd;
    }
    return 0;
}

}  // namespace s3re::game
