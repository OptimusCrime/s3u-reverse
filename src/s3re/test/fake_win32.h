#pragma once

// Scripted Windows functions for differential tests of code that calls
// S3.EXE's imports (CFile, CFileMapping, DetachMemory, the graphics
// loader). FakeWin32 patches the import address table slots with fakes
// that log every call (an id and the arguments that matter) and return
// values drawn from their own Rng. A test runs the original with one
// FakeWin32 and ours with another made from the same seed: as long as both
// make the same calls they see the same results, and the logs are
// compared. Only the imports listed in `imports` are patched; the rest
// stay real.
//
// The game's C++ exceptions (thrown by Win32::ThrowWin32Error and
// friends) are caught by CatchGameException, which records the thrown
// object (vtable, throw info, Win32::CHResult code) and deletes it.

#include "game/globals.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::test {

enum FakeImport : std::uint32_t {
    kFakeCreateFile = 1u << 0,
    kFakeGetLastError = 1u << 1,
    kFakeReadFile = 1u << 2,
    kFakeWriteFile = 1u << 3,
    kFakeSetFilePointer = 1u << 4,
    kFakeGetFileSize = 1u << 5,
    kFakeCreateFileMapping = 1u << 6,
    kFakeMapView = 1u << 7,
    kFakeUnmapView = 1u << 8,
    kFakeCloseHandle = 1u << 9,
    kFakeGetSystemInfo = 1u << 10,
    kFakeVirtualQuery = 1u << 11,
    kFakeVirtualProtect = 1u << 12,
    kFakeGetCurrentDirectory = 1u << 13,
    kFakeSetCurrentDirectory = 1u << 14,
    kFakeFindFirstFile = 1u << 15,
    kFakeFindClose = 1u << 16,
};

struct FakeWin32Config {
    // VirtualQuery: regions are reported inside [regionLow, regionHigh).
    std::uint32_t regionLow = 0;
    std::uint32_t regionHigh = 0;
    // GetCurrentDirectoryA / FindFirstFileA results.
    const char* currentDirectory = "C:\\Games\\S3";
    // Chance (out of 8) that a call fails.
    std::uint32_t failChance = 2;
    // MapViewOfFile: views are returned from this list in turn (0: random).
    const std::vector<std::uint32_t>* views = nullptr;
};

class FakeWin32 {
public:
    FakeWin32(std::uint64_t seed, std::uint32_t imports, const FakeWin32Config& config = {})
        : rng_(seed), config_(config), imports_(imports) {}

    // Patches the imports and makes this the active fake.
    void Install();
    void Uninstall();

    std::vector<std::uint32_t>& log() { return log_; }
    Rng& rng() { return rng_; }

    // State of the fakes (read by fake_win32.cpp).
    Rng rng_;
    FakeWin32Config config_;
    std::uint32_t imports_;
    std::vector<std::uint32_t> log_;
    std::uint32_t lastProtect_ = PAGE_READWRITE;
    std::size_t nextView_ = 0;
};

// The thrown game exception, if any.
struct CaughtException {
    bool threw = false;
    std::uint32_t vtable = 0;
    std::uint32_t throwInfo = 0;
    std::uint32_t code = 0;    // Win32::CHResult's code (+0x18) when the type is one
    bool isHResult = false;
    bool nullObject = false;
    bool operator==(const CaughtException& o) const {
        return threw == o.threw && vtable == o.vtable && throwInfo == o.throwInfo && code == o.code &&
               isHResult == o.isHResult && nullObject == o.nullObject;
    }
};

// SEH filter for a game C++ exception (code 0xE06D7363, a thrown
// pointer); fills `out`, deletes the object (slot 0 with 1).
int CatchGameException(const EXCEPTION_POINTERS* info, CaughtException* out);

}  // namespace s3re::test
