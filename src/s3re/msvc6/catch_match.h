#pragma once

// Matching a game C++ exception (thrown by S3.EXE's MSVC 6.0 runtime,
// _CxxThrowException 0x61f027) against a catch clause of pointer type, as
// MSVC's frame handler does it, so that a replacement can implement the
// original's `catch (T* e)` with SEH (__try / __except). The thrown
// object's catchable types are compared with the clause's type descriptor
// (by address, or by decorated name for descriptors of other modules, as
// MSVC does), and the pointer is adjusted to the clause's type
// (__AdjustPointer).

#include <windows.h>

#include <cstdint>

namespace s3re::msvc6 {

struct CatchMatch {
    bool matched = false;
    void* object = nullptr;  // the caught pointer, adjusted to the clause's type
};

// `descriptor`: the TypeDescriptor's address in S3.EXE; `decoratedName`:
// its name (".PAVCHResult@Win32@@").
CatchMatch MatchPointerCatch(const EXCEPTION_RECORD* record, std::uintptr_t descriptor, const char* decoratedName);

// Deletes a caught framework exception the way the original catch blocks
// do: its vtable slot 0 (scalar deleting destructor) with 1.
void DeleteCaughtException(void* object);

}  // namespace s3re::msvc6
