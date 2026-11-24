// CMapFileStream, the file stream of Dirk::CMapFile (CMapFile + 8; layout
// in map_file.h). A Win32::CFile (Win32\Mapping.cpp, called at its S3.EXE
// addresses) whose errors are caught: every member runs the CFile call in
// a try block with `catch (Win32::CHResult* e)` that stores e's code
// (GetCode, the folded getter 0x613900: +0x18) in `error`, then rethrows
// if the constructor's `rethrow` flag is set, else deletes e (vtable slot
// 0 with 1). Every caller in S3.EXE passes 0 for that flag.
//
// CMapFileStream::CMapFileStream (0x004a55e0): (rethrow) ret 4: +0 =
//   rethrow (byte), Win32::CFile::CFile (0x6195c0) at +4, error = 0.
//   EAX = this.
// CMapFileStream::Open (0x004a5600): (const string& path, mode) ret 8: if
//   no error is pending, CFile::Open (0x619610) with the path's text ("" if
//   empty), access GENERIC_READ (mode bit 1) | GENERIC_WRITE (bit 2), share
//   FILE_SHARE_READ for reading only, no security attributes, OPEN_EXISTING
//   (read) or CREATE_ALWAYS (write; the later bit wins), attributes
//   FILE_ATTRIBUTE_NORMAL, no template. A second catch clause, before the
//   CHResult one, takes Win32::CFileNotFound* and sets error 2 (no GetCode).
//   EAX = (error == 0).
// CMapFileStream::Close (0x004a5710): () if no error is pending, the
//   CFile's vtable slot 1 (Close); then error = 0. EAX: the error from
//   before clearing with its low byte replaced by (error == 0) (sete al).
// CMapFileStream::Read (0x004a57b0): (buffer, size) ret 8: if no error is
//   pending, CFile::Read (0x6198d0: ReadFile, bytes read into a local that
//   starts at 0). EAX = -1 if an error is pending afterwards, else the
//   bytes read.
// CMapFileStream::Write (0x004a5870): (buffer, size) ret 8: the same with
//   CFile::Write (0x619b10, WriteFile).
// CMapFileStream::Seek (0x004a5930): (offset, origin) ret 8: CFile::Seek
//   (0x619d50: SetFilePointer without a high dword), also with an error
//   pending. EAX = the new position.
// CMapFileStream::GetSize (0x004a59d0): () CFile::GetSize (0x61a080:
//   GetFileSize without a high dword), also with an error pending. EAX =
//   -1 if an error is pending afterwards, else the size.
//
// After a caught exception, Seek returns what EAX holds when MSVC 6.0's
// exception handler jumps back into the function: the address the catch
// block returned (_JumpToContinuation 0x61eb95 jumps through EAX),
// 0x4a59a8. No caller uses Seek's result.

#include "game/fileformats/map_file.h"

#include "game/tables.h"
#include "game/vtables.h"
#include "hooks.h"

#include <windows.h>

#include <cstring>

namespace s3re::game {

using fileformats::CMapFileStream;

namespace fileformats {
namespace {

// ---------------------------------------------------------------------------
// The catch clauses, as MSVC 6.0's handler matches them: by the thrown
// object's catchable types (pointer types match by type descriptor; the
// name compare covers descriptors from other modules, as MSVC does).
// ---------------------------------------------------------------------------

constexpr DWORD kCxxExceptionCode = 0xe06d7363;
constexpr ULONG_PTR kCxxMagic = 0x19930520;
constexpr char kCHResultPointerName[] = ".PAVCHResult@Win32@@";
constexpr char kCFileNotFoundPointerName[] = ".PAVCFileNotFound@Win32@@";
constexpr std::uint32_t kFileNotFoundError = 2;  // ERROR_FILE_NOT_FOUND

#pragma pack(push, 4)
struct CatchableType {
    std::uint32_t properties;
    const std::uint8_t* type;  // TypeDescriptor: vtable, spare, then the decorated name
    std::int32_t mdisp, pdisp, vdisp;
    std::int32_t size;
    void* copy;
};
struct CatchableTypeArray {
    std::int32_t count;
    const CatchableType* types[1];
};
struct ThrowInfo {
    std::uint32_t attributes;
    void* unwind;
    void* forward_compat;
    const CatchableTypeArray* catchable;
};
#pragma pack(pop)

enum class CatchClause { None, FileNotFound, HResult };

struct Caught {
    CatchClause clause = CatchClause::None;
    void* object = nullptr;  // the caught pointer (adjusted to the clause's type)
};

bool IsType(const CatchableType* type, std::uintptr_t descriptor, const char* name) {
    if (reinterpret_cast<std::uintptr_t>(type->type) == descriptor) {
        return true;
    }
    return std::strcmp(reinterpret_cast<const char*>(type->type + 8), name) == 0;
}

// The thrown pointer adjusted to a base type (__AdjustPointer).
void* AdjustedPointer(const EXCEPTION_RECORD* record, const CatchableType* type) {
    auto* thrown = *reinterpret_cast<std::uint8_t**>(record->ExceptionInformation[1]);
    if (thrown == nullptr) {
        return nullptr;
    }
    std::uint8_t* adjusted = thrown + type->mdisp;
    if (type->pdisp >= 0) {
        const auto* vbtable = *reinterpret_cast<std::uint8_t* const*>(thrown + type->pdisp);
        adjusted += *reinterpret_cast<const std::int32_t*>(vbtable + type->vdisp) + type->pdisp;
    }
    return adjusted;
}

// The first clause (in source order) whose type the thrown object has.
Caught MatchClauses(const EXCEPTION_RECORD* record, bool withFileNotFound) {
    if (record->ExceptionCode != kCxxExceptionCode || record->NumberParameters < 3 ||
        record->ExceptionInformation[0] != kCxxMagic || record->ExceptionInformation[2] == 0) {
        return {};
    }
    const auto* info = reinterpret_cast<const ThrowInfo*>(record->ExceptionInformation[2]);
    const CatchableTypeArray* types = info->catchable;
    if (withFileNotFound) {
        for (std::int32_t i = 0; i < types->count; ++i) {
            if (IsType(types->types[i], type_descriptor::kCFileNotFoundPointer, kCFileNotFoundPointerName)) {
                return {CatchClause::FileNotFound, AdjustedPointer(record, types->types[i])};
            }
        }
    }
    for (std::int32_t i = 0; i < types->count; ++i) {
        if (IsType(types->types[i], type_descriptor::kCHResultPointer, kCHResultPointerName)) {
            return {CatchClause::HResult, AdjustedPointer(record, types->types[i])};
        }
    }
    return {};
}

// Win32::CHResult's code (the folded getter 0x613900: +0x18).
std::uint32_t HResultCode(void* exception) {
    using GetCodeFn = std::uint32_t(__fastcall*)(void* self, void* edx);
    return reinterpret_cast<GetCodeFn>(0x00613900)(exception, nullptr);
}

// The part of the catch block before `throw;` / delete.
void StoreError(CMapFileStream* stream, const Caught& caught) {
    stream->error = caught.clause == CatchClause::FileNotFound ? kFileNotFoundError : HResultCode(caught.object);
}

struct FilterState {
    CMapFileStream* stream;
    bool withFileNotFound;
    Caught caught;
};

// With `rethrow` set, the catch block stores the error and rethrows the
// same exception: here the error is stored and the search goes on.
int StreamFilter(const EXCEPTION_POINTERS* pointers, FilterState* state) {
    const Caught caught = MatchClauses(pointers->ExceptionRecord, state->withFileNotFound);
    if (caught.clause == CatchClause::None) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (state->stream->rethrow != 0) {
        StoreError(state->stream, caught);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    state->caught = caught;
    return EXCEPTION_EXECUTE_HANDLER;
}

// The rest of the catch block: store the error and delete the exception.
void FinishCatch(CMapFileStream* stream, const Caught& caught) {
    StoreError(stream, caught);
    if (caught.object != nullptr) {
        using DeleteFn = void*(__fastcall*)(void* object, void* edx, std::uint32_t flags);
        const auto* vtable = *static_cast<void* const* const*>(caught.object);
        reinterpret_cast<DeleteFn>(vtable[0])(caught.object, nullptr, 1);
    }
}

// ---------------------------------------------------------------------------
// The CFile calls (Win32\Mapping.cpp, at their S3.EXE addresses).
// ---------------------------------------------------------------------------

using FileOpenFn = void(__fastcall*)(framework::CWin32Handle* self, void* edx, const char* name, std::uint32_t access,
                                     std::uint32_t share, void* security, std::uint32_t creation,
                                     std::uint32_t attributes, void* templateFile);
using FileTransferFn = void(__fastcall*)(framework::CWin32Handle* self, void* edx, void* buffer, std::uint32_t size,
                                         std::uint32_t* transferred, void* overlapped);
using FileSeekFn = std::uint32_t(__fastcall*)(framework::CWin32Handle* self, void* edx, std::int32_t offset,
                                              std::int32_t* high, std::uint32_t origin);
using FileGetSizeFn = std::uint32_t(__fastcall*)(framework::CWin32Handle* self, void* edx, std::uint32_t* high);
using FileCloseFn = void(__fastcall*)(framework::CWin32Handle* self, void* edx);
constexpr std::uintptr_t kFileOpen = 0x00619610;
constexpr std::uintptr_t kFileRead = 0x006198d0;
constexpr std::uintptr_t kFileWrite = 0x00619b10;
constexpr std::uintptr_t kFileSeek = 0x00619d50;
constexpr std::uintptr_t kFileGetSize = 0x0061a080;
constexpr std::uint32_t kFileCloseSlot = 1;

// The continuation addresses EAX holds after a caught exception (see the
// file header).
constexpr std::uint32_t kSeekCatchContinuation = 0x004a59a8;

// The try blocks: true if the body ran without a caught exception. No C++
// objects in these frames, so __try is allowed.
bool GuardedOpen(FilterState* state, const char* name, std::uint32_t access, std::uint32_t share,
                 std::uint32_t creation) {
    __try {
        reinterpret_cast<FileOpenFn>(kFileOpen)(&state->stream->file, nullptr, name, access, share, nullptr,
                                                creation, FILE_ATTRIBUTE_NORMAL, nullptr);
        return true;
    } __except (StreamFilter(GetExceptionInformation(), state)) {
        return false;
    }
}

bool GuardedTransfer(FilterState* state, std::uintptr_t function, void* buffer, std::uint32_t size,
                     std::uint32_t* transferred) {
    __try {
        reinterpret_cast<FileTransferFn>(function)(&state->stream->file, nullptr, buffer, size, transferred, nullptr);
        return true;
    } __except (StreamFilter(GetExceptionInformation(), state)) {
        return false;
    }
}

bool GuardedSeek(FilterState* state, std::int32_t offset, std::uint32_t origin, std::uint32_t* position) {
    __try {
        *position = reinterpret_cast<FileSeekFn>(kFileSeek)(&state->stream->file, nullptr, offset, nullptr, origin);
        return true;
    } __except (StreamFilter(GetExceptionInformation(), state)) {
        return false;
    }
}

bool GuardedGetSize(FilterState* state, std::uint32_t* size) {
    __try {
        *size = reinterpret_cast<FileGetSizeFn>(kFileGetSize)(&state->stream->file, nullptr, nullptr);
        return true;
    } __except (StreamFilter(GetExceptionInformation(), state)) {
        return false;
    }
}

bool GuardedClose(FilterState* state) {
    __try {
        const auto* vtable = reinterpret_cast<void* const*>(static_cast<std::uintptr_t>(state->stream->file.vtable));
        reinterpret_cast<FileCloseFn>(vtable[kFileCloseSlot])(&state->stream->file, nullptr);
        return true;
    } __except (StreamFilter(GetExceptionInformation(), state)) {
        return false;
    }
}

// Read and Write: the same frame around CFile::Read / CFile::Write.
std::int32_t Transfer(CMapFileStream* stream, std::uintptr_t function, void* buffer, std::uint32_t size) {
    std::uint32_t transferred = 0;
    if (stream->error == 0) {
        FilterState state{stream, false, {}};
        if (!GuardedTransfer(&state, function, buffer, size, &transferred)) {
            FinishCatch(stream, state.caught);
        }
    }
    if (stream->error != 0) {
        return -1;
    }
    return static_cast<std::int32_t>(transferred);
}

}  // namespace
}  // namespace fileformats

using namespace fileformats;

// Purpose: sets up the file stream of a map or savegame file object.
CMapFileStream* __fastcall CMapFileStream_Construct(CMapFileStream* self, void* /*edx*/, std::uint32_t rethrow) {
    using FileConstructFn = void*(__fastcall*)(framework::CWin32Handle* self, void* edx);
    self->rethrow = static_cast<std::uint8_t>(rethrow);
    reinterpret_cast<FileConstructFn>(0x006195c0)(&self->file, nullptr);
    self->error = 0;
    return self;
}

// Purpose: opens a map or savegame file for reading (loading, the map and
// savegame lists) or writing (saving); a missing file becomes error 2.
std::uint32_t __fastcall CMapFileStream_Open(CMapFileStream* self, void* /*edx*/, const msvc6::String* path,
                                             std::uint32_t mode) {
    if (self->error == 0) {
        std::uint32_t access = 0;
        std::uint32_t creation = 0;
        std::uint32_t share = 0;
        if ((mode & kOpenRead) != 0) {
            access = GENERIC_READ;
            creation = OPEN_EXISTING;
            share = FILE_SHARE_READ;
        }
        if ((mode & kOpenWrite) != 0) {
            access |= GENERIC_WRITE;
            creation = CREATE_ALWAYS;
            share = 0;
        }
        const char* name = path->ptr != nullptr ? path->ptr : static_cast<const char*>(g_stringNullBuffer);
        FilterState state{self, true, {}};
        if (!GuardedOpen(&state, name, access, share, creation)) {
            FinishCatch(self, state.caught);
        }
    }
    return self->error == 0 ? 1 : 0;
}

// Purpose: closes the file of a map or savegame and forgets a pending
// error.
std::uint32_t __fastcall CMapFileStream_Close(CMapFileStream* self, void* /*edx*/) {
    if (self->error == 0) {
        FilterState state{self, false, {}};
        if (!GuardedClose(&state)) {
            FinishCatch(self, state.caught);
        }
    }
    const std::uint32_t error = self->error;
    self->error = 0;
    return (error & 0xffffff00u) | (error == 0 ? 1 : 0);  // sete al over the error in EAX
}

// Purpose: reads bytes of a map or savegame (chunk headers and chunks,
// the checksum pass); -1 once an error is pending.
std::int32_t __fastcall CMapFileStream_Read(CMapFileStream* self, void* /*edx*/, void* buffer, std::uint32_t size) {
    return Transfer(self, kFileRead, buffer, size);
}

// Purpose: writes bytes of a savegame (header and chunks); -1 once an
// error is pending.
std::int32_t __fastcall CMapFileStream_Write(CMapFileStream* self, void* /*edx*/, const void* buffer,
                                             std::uint32_t size) {
    return Transfer(self, kFileWrite, const_cast<void*>(buffer), size);
}

// Purpose: moves within a map or savegame file (over chunk headers and
// chunks, back to the start for the checksum).
std::uint32_t __fastcall CMapFileStream_Seek(CMapFileStream* self, void* /*edx*/, std::int32_t offset,
                                             std::uint32_t origin) {
    std::uint32_t position = 0;
    FilterState state{self, false, {}};
    if (!GuardedSeek(&state, offset, origin, &position)) {
        FinishCatch(self, state.caught);
        return kSeekCatchContinuation;
    }
    return position;
}

// Purpose: the size of a map or savegame file, which bounds the checksum
// pass.
std::uint32_t __fastcall CMapFileStream_GetSize(CMapFileStream* self, void* /*edx*/) {
    std::uint32_t size = 0;
    FilterState state{self, false, {}};
    if (!GuardedGetSize(&state, &size)) {
        FinishCatch(self, state.caught);
        // 0x4a5a34: EAX = the slot of the caught pointer, then -1 below,
        // since the catch left an error (a CHResult's code is never 0).
        size = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(state.caught.object));
    }
    if (self->error != 0) {
        return 0xffffffffu;  // -1: also for an error pending before the call
    }
    return size;
}

using StreamConstructFn = CMapFileStream*(__fastcall*)(CMapFileStream* self, void* edx, std::uint32_t rethrow);
using StreamOpenFn = std::uint32_t(__fastcall*)(CMapFileStream* self, void* edx, const msvc6::String* path,
                                                std::uint32_t mode);
using StreamCloseFn = std::uint32_t(__fastcall*)(CMapFileStream* self, void* edx);
using StreamReadHookFn = std::int32_t(__fastcall*)(CMapFileStream* self, void* edx, void* buffer, std::uint32_t size);
using StreamWriteFn = std::int32_t(__fastcall*)(CMapFileStream* self, void* edx, const void* buffer,
                                                std::uint32_t size);
using StreamSeekHookFn = std::uint32_t(__fastcall*)(CMapFileStream* self, void* edx, std::int32_t offset,
                                                    std::uint32_t origin);
using StreamGetSizeFn = std::uint32_t(__fastcall*)(CMapFileStream* self, void* edx);

S3RE_HOOK(0x004a55e0, CMapFileStream_Construct, StreamConstructFn, Original_CMapFileStream_Construct,
          CMapFileStream_Construct);
S3RE_HOOK(0x004a5600, CMapFileStream_Open, StreamOpenFn, Original_CMapFileStream_Open, CMapFileStream_Open);
S3RE_HOOK(0x004a5710, CMapFileStream_Close, StreamCloseFn, Original_CMapFileStream_Close, CMapFileStream_Close);
S3RE_HOOK(0x004a57b0, CMapFileStream_Read, StreamReadHookFn, Original_CMapFileStream_Read, CMapFileStream_Read);
S3RE_HOOK(0x004a5870, CMapFileStream_Write, StreamWriteFn, Original_CMapFileStream_Write, CMapFileStream_Write);
S3RE_HOOK(0x004a5930, CMapFileStream_Seek, StreamSeekHookFn, Original_CMapFileStream_Seek, CMapFileStream_Seek);
S3RE_HOOK(0x004a59d0, CMapFileStream_GetSize, StreamGetSizeFn, Original_CMapFileStream_GetSize,
          CMapFileStream_GetSize);

}  // namespace s3re::game
