// WinMain (0x0045a120)
//
// Called by the C runtime startup code at the entry point (0x00621bc6) as
// WinMain(GetModuleHandleA(NULL), NULL, <command line>, <show command>); the
// return value is passed to exit().
//
// Status: pass-through hook (logs and calls the original). Not reimplemented.
// In test mode (s3re_launcher --test) the game is not started; the test suite
// runs instead and the number of failed tests becomes the exit code.

#include "hooks.h"
#include "log.h"
#include "test/difftest.h"

#include <windows.h>

namespace {

using WinMainFn = int(WINAPI*)(HINSTANCE, HINSTANCE, LPSTR, int);

int WINAPI Hooked_WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR commandLine,
                          int showCommand);

S3RE_PASSTHROUGH(0x0045a120, WinMain, WinMainFn, Original_WinMain, Hooked_WinMain);

int WINAPI Hooked_WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR commandLine,
                          int showCommand) {
    S3RE_LOG("WinMain(instance=%p, previous=%p, commandLine=\"%s\", showCommand=%d)",
             instance, previous, commandLine != nullptr ? commandLine : "(null)",
             showCommand);

    if (s3re::test::IsTestMode()) {
        return s3re::test::RunAll();
    }

    const int result = Original_WinMain(instance, previous, commandLine, showCommand);
    S3RE_LOG("WinMain returned %d", result);
    return result;
}

}  // namespace
