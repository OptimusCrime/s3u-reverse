// s3re.dll: loaded into the original S3.EXE by s3re_launcher before any game
// code runs. Installs the hooks that redirect original functions to our
// reimplementations.

#include "crash_log.h"
#include "hooks.h"
#include "log.h"
#include "trace/determinism.h"

#include <windows.h>
#include <detours.h>

static_assert(sizeof(void*) == 4, "s3re.dll must be built as 32-bit x86");

namespace {

// Identity of the S3.EXE build the hook addresses belong to. The launcher
// already checks the full SHA-256; this is a cheap second check in case the
// DLL is loaded some other way.
constexpr DWORD kExpectedTimeDateStamp = 0x38E4B1B0;  // 2000-03-31 14:09:52 UTC
constexpr DWORD kExpectedSizeOfImage = 0x003ED000;

bool IsExpectedGameBuild() {
    const auto* base = reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }
    S3RE_LOG("host image: base=%p timestamp=0x%08lx sizeOfImage=0x%08lx", base,
             nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
    return reinterpret_cast<DWORD>(base) == 0x00400000 &&
           nt->FileHeader.TimeDateStamp == kExpectedTimeDateStamp &&
           nt->OptionalHeader.SizeOfImage == kExpectedSizeOfImage;
}

void Startup(HMODULE self) {
    s3re::log::Open(self);

    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    S3RE_LOG("s3re loaded into %s (pid %lu)", exePath, GetCurrentProcessId());
    s3re::crash::Install();

    if (!IsExpectedGameBuild()) {
        S3RE_LOG("unexpected host executable; no hooks installed, game runs unmodified");
        return;
    }
    if (!s3re::hooks::InstallAll()) {
        S3RE_LOG("hook installation failed; game runs unmodified");
    }
}

void Shutdown() {
    S3RE_LOG("process detaching");
    s3re::hooks::LogCallCounts();
    s3re::hooks::UninstallAll();
    s3re::trace::CloseDeterminismTrace();
    s3re::log::Close();
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID /*reserved*/) {
    if (DetourIsHelperProcess()) {
        return TRUE;
    }
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(instance);
            // Undo the in-memory import table change the launcher made to load us.
            DetourRestoreAfterWith();
            Startup(instance);
            break;
        case DLL_PROCESS_DETACH:
            Shutdown();
            break;
        default:
            break;
    }
    return TRUE;
}
