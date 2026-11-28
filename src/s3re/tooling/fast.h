#pragma once

// --fast (S3RE_FAST) for our CLogicThread::Step (fast.cpp).

namespace s3re::tooling {

// Win32::CWin32WaitableBag<Win32::CWin32Waitable> (opaque): the wait set at
// CLogicThread+0x3dc.
struct CWin32WaitableBag;

// True when --fast is on (read once when the DLL loads).
extern const bool g_fastLogicTicks;

// The logic step's wait for its timer: Win32::CWin32WaitableBag::WaitFor
// (0x463100, thiscall (timeout ms, throw on timeout)), which returns the
// signalled object; with --fast a poll that reports the timer object
// (CLogicThread+0x3bc) when nothing is signalled.
void* WaitForLogicTick(CWin32WaitableBag* waitSet, unsigned timeoutMs, int throwOnTimeout);

}  // namespace s3re::tooling
