// Win32::CFile and Win32::CFileMapping (Win32\Mapping.cpp): the file and
// file-mapping wrappers the graphics and sound loaders map their data files
// with (re/win32_framework.md, re/graphics_formats.md). Layouts in
// framework_layout.h (CWin32Handle, CFileMapping, WCPtrOrderedVector).
//
// Error checks: every Windows call is wrapped in the framework's VERIFY
// macro, which calls Win32::ThrowWin32Error (0x610de0; fastcall: ECX = the
// expression text, EDX = the source file, stack: line, GetLastError(),
// const std::string* message) when the expression is false. It never
// returns (it throws a Win32::CHResult). The message is an empty string
// (built from "" through two temporaries, which never allocate); its
// allocator byte is whatever the original's stack slot held, which is
// reproducible here: the low byte of the failed handle (Open: 0xff;
// Create, MapView: 0), of `overlapped` (Read, Write), of `method` (Seek)
// or of the `high` pointer (GetSize). Our code after a ThrowWin32Error
// call is never reached, like the original's.
//
// Win32::CFile::Open (0x00619610): (name, access, share, security,
//   disposition, flags, template) ret 1Ch: CreateFileA with the arguments.
//   INVALID_HANDLE_VALUE: if GetLastError() is ERROR_FILE_NOT_FOUND (2) or a
//   second GetLastError() is ERROR_PATH_NOT_FOUND (3), throws a new
//   Win32::CFileNotFound(name, 1) (operator new 0x28, constructor 0x611db0;
//   null if new fails) with _CxxThrowException (ThrowInfo 0x67b828);
//   otherwise ThrowWin32Error("hFile != INVALID_HANDLE_VALUE", line 46).
//   Then CWin32Handle::Set(handle, 1). EAX = the handle.
// Win32::CFile::Read (0x006198d0) / Write (0x00619b10): (buffer, size,
//   done, overlapped) ret 10h: ReadFile / WriteFile on the handle;
//   ThrowWin32Error("bRetval != FALSE", line 60 / 68) when it fails. EAX =
//   the BOOL result.
// Win32::CFile::Seek (0x00619d50): (low, high*, method) ret 0Ch:
//   SetFilePointer. Without `high`: ThrowWin32Error("dwRetval !=
//   0xffffffff", line 77) when it returns -1. With `high`: the check is
//   VERIFY((r != -1) && (GetLastError() != NO_ERROR)) (line 79), so it
//   throws when r == -1 and also when a successful call leaves
//   GetLastError() at 0 (an original bug: the intended check is the
//   opposite; kept). EAX = r.
// Win32::CFile::GetSize (0x0061a080): (high*) ret 4: GetFileSize, the same
//   two checks (lines 90 / 92). EAX = the low size.
//
// Win32::CFileMapping::CFileMapping (0x0061a3a0): () CWin32Handle(0, 0),
//   file = 0, the view list: vtable 0x657440, unk04 = 0, capacity 10,
//   filled 0, data = operator new(40) (capacity 0 if null), length 0, grow
//   5, vtable 0x65d9d0; object vtable 0x65d9c4; unk04 = 0x7f. EAX = this.
// Win32::CFileMapping::~CFileMapping (0x0061a460): vtable 0x65d9c4,
//   Close (0x61a7a0); the view list: delete data, filled = min(filled, 0),
//   data = capacity = length = 0, vtable 0x657440, (data is null now, so
//   the base destructor's own delete is skipped), vtable 0x655438; then
//   ~CWin32Handle (0x60beb0). EAX = what that returns.
// Win32::CFileMapping::scalar_deleting_destructor (0x0061a440): (flags)
//   ret 4: the destructor, operator delete(this) if flags & 1. EAX = this.
// Win32::CFileMapping::Close (0x0061a7a0, vtable slot 1): if a file is
//   attached: CWin32Handle::Close (0x60bf20) on the mapping handle, then
//   the file's slot 0 (scalar deleting destructor) with 1, file = 0. EAX =
//   the last call's result (0 when no file is attached).
// Win32::CFileMapping::Create (0x0061a520): (CFile* file, security,
//   protect, sizeHigh, sizeLow, name) ret 18h: file = the argument;
//   CreateFileMappingA(file ? file's handle : INVALID_HANDLE_VALUE, ...);
//   ThrowWin32Error("hMapping != NULL", line 135) on failure; Set(handle,
//   1). EAX = the handle.
// Win32::CFileMapping::MapView (0x0061a7d0): (access, offsetHigh,
//   offsetLow, size) ret 10h: MapViewOfFile on the mapping handle;
//   ThrowWin32Error("lpBaseAddress != NULL", line 168) on failure; then the
//   view is appended to the view list (WCValOrderedVector::insertAt
//   inlined, see AppendView). EAX = the view.
// Win32::CFileMapping::UnmapView (0x0061aa40): (view) ret 4:
//   UnmapViewOfFile (result ignored); if the list is not empty, its slot 3
//   (base_find_from 0x499da0: (&view, 1, &index), index starting at 0)
//   finds the view and the entries after it move down one place, length
//   and filled - 1. EAX = the new `filled`, or 0 (empty list / not found).

#include "game/framework/framework_layout.h"
#include "game/globals.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"

#include <cstdint>

namespace s3re::game {

// win32_classes.cpp
void* __fastcall Win32_CWin32Handle_Construct(void* self, void* edx, std::uint32_t handle, std::uint32_t b);

namespace {

using framework::CFileMapping;
using framework::CWin32Handle;
using framework::WCPtrOrderedVector;

constexpr std::uintptr_t kThrowWin32Error = 0x00610de0;
constexpr std::uintptr_t kCFileNotFoundConstruct = 0x00611db0;
constexpr std::uintptr_t kCxxThrowException = 0x0061f027;
constexpr std::uintptr_t kCWin32HandleDestroy = 0x0060beb0;
constexpr std::uintptr_t kCWin32HandleClose = 0x0060bf20;
constexpr std::uintptr_t kMappingClose = 0x0061a7a0;
constexpr std::uintptr_t kMappingDestroy = 0x0061a460;
constexpr std::uintptr_t kVectorResize = 0x00429a00;  // watcom::PtrVector::resize

constexpr std::uint32_t kInvalidHandle = 0xffffffff;  // INVALID_HANDLE_VALUE
constexpr std::uint32_t kInvalidSize = 0xffffffff;    // INVALID_FILE_SIZE / INVALID_SET_FILE_POINTER
constexpr std::uint32_t kErrorFileNotFound = 2;
constexpr std::uint32_t kErrorPathNotFound = 3;
constexpr std::uint32_t kFileNotFoundSize = 0x28;     // sizeof(Win32::CFileNotFound)
constexpr std::uint32_t kInitialViewCapacity = 10;
constexpr std::uint32_t kViewGrowth = 5;
constexpr std::uint32_t kViewVectorFlags = 0x7f;
constexpr std::uint32_t kFindViewSlot = 3;            // WCOrderedVectorBase::base_find_from

// Source lines of the VERIFY checks (Win32\Mapping.cpp).
constexpr std::uint32_t kLineOpen = 46;
constexpr std::uint32_t kLineRead = 60;
constexpr std::uint32_t kLineWrite = 68;
constexpr std::uint32_t kLineSeek = 77;
constexpr std::uint32_t kLineSeekHigh = 79;
constexpr std::uint32_t kLineGetSize = 90;
constexpr std::uint32_t kLineGetSizeHigh = 92;
constexpr std::uint32_t kLineCreateMapping = 135;
constexpr std::uint32_t kLineMapView = 168;

using ThrowWin32ErrorFn = void(__fastcall*)(const char* expression, const char* file, std::uint32_t line,
                                            std::uint32_t error, const msvc6::String* message);
using CFileNotFoundConstructFn = void*(__fastcall*)(void* self, void* edx, const char* name,
                                                    std::uint32_t mostDerived);
using CxxThrowExceptionFn = void(__stdcall*)(void* object, std::uintptr_t throwInfo);
using MemberFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using DeletingFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t flags);
using ResizeFn = std::uint32_t(__fastcall*)(void* vector, void* edx, std::uint32_t n);
using FindFn = std::uint32_t(__fastcall*)(void* vector, void* edx, const std::uint32_t* value,
                                          std::uint32_t occurrence, std::int32_t* index);

CWin32Handle* Handle(void* self) {
    return static_cast<CWin32Handle*>(self);
}

void* HandleOf(void* self) {
    return reinterpret_cast<void*>(Handle(self)->handle);
}

// VERIFY failed: Win32::ThrowWin32Error with the current GetLastError()
// and an empty message whose allocator byte is `allocator`.
void ThrowWin32Error(const char* expression, std::uint32_t line, std::uint32_t allocator) {
    msvc6::String message{};
    message.allocator = static_cast<std::uint8_t>(allocator);
    const std::uint32_t error = g_importGetLastError.get()();
    reinterpret_cast<ThrowWin32ErrorFn>(kThrowWin32Error)(expression, texts::kMappingFile, line, error, &message);
}

// throw new Win32::CFileNotFound(name).
void ThrowFileNotFound(const char* name) {
    void* const object = msvc6::GameOperatorNew(kFileNotFoundSize);
    void* thrown = nullptr;
    if (object != nullptr) {
        thrown = reinterpret_cast<CFileNotFoundConstructFn>(kCFileNotFoundConstruct)(object, nullptr, name, 1);
    }
    reinterpret_cast<CxxThrowExceptionFn>(kCxxThrowException)(&thrown, throw_info::kCFileNotFoundPointer);
}

// The checks of Seek and GetSize: without a high dword only -1 fails; with
// one, VERIFY((r != -1) && (GetLastError() != NO_ERROR)) (see the header).
void CheckSizeResult(std::uint32_t result, bool withHigh, std::uint32_t line, std::uint32_t highLine,
                     std::uint32_t allocator) {
    if (!withHigh) {
        if (result == kInvalidSize) {
            ThrowWin32Error(texts::kRetvalValid, line, allocator);
        }
        return;
    }
    if (result == kInvalidSize || g_importGetLastError.get()() == 0) {
        ThrowWin32Error(texts::kRetvalValidHigh, highLine, allocator);
    }
}

// WCValOrderedVector<void*>::insertAt(length, view) as MapView inlines it:
// grows by `grow` when full (gives up if grow is 0 or the resize fails),
// then stores the view at the end. The index is the length read before
// growing, clamped to 0 when negative (the error hook 0x59f200 is an empty
// function); with a negative length the entries are shifted up from the
// end, as the generic insert does. Addresses wrap like the original's.
void AppendView(WCPtrOrderedVector* views, std::uint32_t view) {
    std::int32_t index = views->length;
    if (index < 0) {
        index = 0;
    }
    if (static_cast<std::uint32_t>(views->length) == views->capacity) {
        if (views->grow == 0) {
            return;
        }
        const std::uint32_t capacity = views->capacity + views->grow;
        if (reinterpret_cast<ResizeFn>(kVectorResize)(views, nullptr, capacity) == 0) {
            return;
        }
        if (static_cast<std::uint32_t>(views->length) > capacity) {
            views->length = static_cast<std::int32_t>(capacity);
        }
    }
    auto entry = [views](std::int32_t i) {
        return reinterpret_cast<std::uint32_t*>(views->data + static_cast<std::uint32_t>(i) * 4);
    };
    if (index == views->length) {
        std::uint32_t* const slot = entry(index);
        if (slot != nullptr) {
            *slot = view;
        }
    } else {
        std::uint32_t* const last = entry(views->length);
        if (last != nullptr) {
            *last = *entry(views->length - 1);
        }
        for (std::int32_t i = views->length - 2; i >= index; --i) {
            *entry(i + 1) = *entry(i);
        }
        *entry(index) = view;
    }
    ++views->length;
    ++views->filled;
}

}  // namespace

std::uint32_t __fastcall Win32_CFile_Open(void* self, void* edx, const char* name, std::uint32_t access,
                                          std::uint32_t share, void* security, std::uint32_t disposition,
                                          std::uint32_t flags, void* templateFile);
std::uint32_t __fastcall Win32_CFile_Read(void* self, void* edx, void* buffer, std::uint32_t size,
                                          std::uint32_t* done, void* overlapped);
std::uint32_t __fastcall Win32_CFile_Write(void* self, void* edx, const void* buffer, std::uint32_t size,
                                           std::uint32_t* done, void* overlapped);
std::uint32_t __fastcall Win32_CFile_Seek(void* self, void* edx, std::int32_t low, std::int32_t* high,
                                          std::uint32_t method);
std::uint32_t __fastcall Win32_CFile_GetSize(void* self, void* edx, std::uint32_t* high);
void* __fastcall Win32_CFileMapping_Construct(void* self, void* edx);
std::uint32_t __fastcall Win32_CFileMapping_Destroy(void* self, void* edx);
void* __fastcall Win32_CFileMapping_DeletingDestroy(void* self, void* edx, std::uint32_t flags);
std::uint32_t __fastcall Win32_CFileMapping_Close(void* self, void* edx);
std::uint32_t __fastcall Win32_CFileMapping_Create(void* self, void* edx, void* file, void* security,
                                                   std::uint32_t protect, std::uint32_t sizeHigh,
                                                   std::uint32_t sizeLow, const char* name);
std::uint32_t __fastcall Win32_CFileMapping_MapView(void* self, void* edx, std::uint32_t access,
                                                    std::uint32_t offsetHigh, std::uint32_t offsetLow,
                                                    std::uint32_t size);
std::uint32_t __fastcall Win32_CFileMapping_UnmapView(void* self, void* edx, std::uint32_t view);

using FileOpenFn = decltype(&Win32_CFile_Open);
using FileReadFn = decltype(&Win32_CFile_Read);
using FileWriteFn = decltype(&Win32_CFile_Write);
using FileSeekFn = decltype(&Win32_CFile_Seek);
using FileGetSizeFn = decltype(&Win32_CFile_GetSize);
using MappingConstructFn = decltype(&Win32_CFileMapping_Construct);
using MappingDestroyFn = decltype(&Win32_CFileMapping_Destroy);
using MappingDeletingFn = decltype(&Win32_CFileMapping_DeletingDestroy);
using MappingCloseFn = decltype(&Win32_CFileMapping_Close);
using MappingCreateFn = decltype(&Win32_CFileMapping_Create);
using MappingMapViewFn = decltype(&Win32_CFileMapping_MapView);
using MappingUnmapViewFn = decltype(&Win32_CFileMapping_UnmapView);

S3RE_HOOK(0x00619610, Win32_CFile_Open, FileOpenFn, Original_Win32_CFile_Open, Win32_CFile_Open);

// Purpose: opens a file (CreateFileA); a missing file or folder throws
// CFileNotFound, other errors a Win32 error.
std::uint32_t __fastcall Win32_CFile_Open(void* self, void* /*edx*/, const char* name, std::uint32_t access,
                                          std::uint32_t share, void* security, std::uint32_t disposition,
                                          std::uint32_t flags, void* templateFile) {
    void* const file = g_importCreateFileA.get()(name, access, share, security, disposition, flags, templateFile);
    const std::uint32_t handle = reinterpret_cast<std::uint32_t>(file);
    if (handle == kInvalidHandle) {
        if (g_importGetLastError.get()() == kErrorFileNotFound ||
            g_importGetLastError.get()() == kErrorPathNotFound) {
            ThrowFileNotFound(name);
        }
        ThrowWin32Error(texts::kFileHandleValid, kLineOpen, handle);
    }
    Handle(self)->handle = handle;  // CWin32Handle::Set(handle, 1) 0x60bf00
    Handle(self)->owned = 1;
    return handle;
}

S3RE_HOOK(0x006198d0, Win32_CFile_Read, FileReadFn, Original_Win32_CFile_Read, Win32_CFile_Read);

// Purpose: reads from the file (ReadFile); a failure throws a Win32 error.
std::uint32_t __fastcall Win32_CFile_Read(void* self, void* /*edx*/, void* buffer, std::uint32_t size,
                                          std::uint32_t* done, void* overlapped) {
    const int result = g_importReadFile.get()(HandleOf(self), buffer, size, done, overlapped);
    if (result == 0) {
        ThrowWin32Error(texts::kRetvalNotFalse, kLineRead, reinterpret_cast<std::uint32_t>(overlapped));
    }
    return static_cast<std::uint32_t>(result);
}

S3RE_HOOK(0x00619b10, Win32_CFile_Write, FileWriteFn, Original_Win32_CFile_Write, Win32_CFile_Write);

// Purpose: writes to the file (WriteFile); a failure throws a Win32 error.
std::uint32_t __fastcall Win32_CFile_Write(void* self, void* /*edx*/, const void* buffer, std::uint32_t size,
                                           std::uint32_t* done, void* overlapped) {
    const int result = g_importWriteFile.get()(HandleOf(self), buffer, size, done, overlapped);
    if (result == 0) {
        ThrowWin32Error(texts::kRetvalNotFalse, kLineWrite, reinterpret_cast<std::uint32_t>(overlapped));
    }
    return static_cast<std::uint32_t>(result);
}

S3RE_HOOK(0x00619d50, Win32_CFile_Seek, FileSeekFn, Original_Win32_CFile_Seek, Win32_CFile_Seek);

// Purpose: moves the file position (SetFilePointer).
std::uint32_t __fastcall Win32_CFile_Seek(void* self, void* /*edx*/, std::int32_t low, std::int32_t* high,
                                          std::uint32_t method) {
    const std::uint32_t result = g_importSetFilePointer.get()(HandleOf(self), low, high, method);
    CheckSizeResult(result, high != nullptr, kLineSeek, kLineSeekHigh, method);
    return result;
}

S3RE_HOOK(0x0061a080, Win32_CFile_GetSize, FileGetSizeFn, Original_Win32_CFile_GetSize, Win32_CFile_GetSize);

// Purpose: the file's size (GetFileSize); the graphics loader records each
// data file's size with it.
std::uint32_t __fastcall Win32_CFile_GetSize(void* self, void* /*edx*/, std::uint32_t* high) {
    const std::uint32_t result = g_importGetFileSize.get()(HandleOf(self), high);
    CheckSizeResult(result, high != nullptr, kLineGetSize, kLineGetSizeHigh, reinterpret_cast<std::uint32_t>(high));
    return result;
}

S3RE_HOOK(0x0061a3a0, Win32_CFileMapping_Construct, MappingConstructFn, Original_Win32_CFileMapping_Construct,
          Win32_CFileMapping_Construct);

// Purpose: constructor of a file mapping (no mapping object yet, an empty
// list of views).
void* __fastcall Win32_CFileMapping_Construct(void* self, void* /*edx*/) {
    CFileMapping* const mapping = static_cast<CFileMapping*>(self);
    Win32_CWin32Handle_Construct(self, nullptr, 0, 0);
    mapping->file = 0;
    WCPtrOrderedVector* const views = &mapping->views;
    views->unk04 = 0;
    views->vtable = vtables::kMappingViewVectorBase;
    views->capacity = kInitialViewCapacity;
    views->filled = 0;
    views->data = reinterpret_cast<std::uint32_t>(msvc6::GameOperatorNew(kInitialViewCapacity * 4));
    if (views->data == 0) {
        views->capacity = 0;
    }
    views->length = 0;
    views->grow = kViewGrowth;
    views->vtable = vtables::kMappingViewVector;
    mapping->handle.vtable = vtables::kCFileMapping;
    views->unk04 = kViewVectorFlags;
    return self;
}

S3RE_HOOK(0x0061a460, Win32_CFileMapping_Destroy, MappingDestroyFn, Original_Win32_CFileMapping_Destroy,
          Win32_CFileMapping_Destroy);

// Purpose: destructor of a file mapping: closes the mapping and its file
// and frees the view list (the views themselves stay mapped).
std::uint32_t __fastcall Win32_CFileMapping_Destroy(void* self, void* /*edx*/) {
    CFileMapping* const mapping = static_cast<CFileMapping*>(self);
    mapping->handle.vtable = vtables::kCFileMapping;
    reinterpret_cast<MemberFn>(kMappingClose)(self, nullptr);
    WCPtrOrderedVector* const views = &mapping->views;
    const std::int32_t filled = views->filled >= 0 ? 0 : views->filled;
    msvc6::GameOperatorDelete(reinterpret_cast<void*>(views->data));
    views->filled = filled;
    views->data = 0;
    views->capacity = 0;
    views->length = 0;
    views->vtable = vtables::kMappingViewVectorBase;
    views->vtable = vtables::kMappingViewVectorRoot;
    return reinterpret_cast<MemberFn>(kCWin32HandleDestroy)(self, nullptr);
}

S3RE_HOOK(0x0061a440, Win32_CFileMapping_DeletingDestroy, MappingDeletingFn,
          Original_Win32_CFileMapping_DeletingDestroy, Win32_CFileMapping_DeletingDestroy);

// Purpose: the compiler-generated "delete this mapping" entry of its vtable.
void* __fastcall Win32_CFileMapping_DeletingDestroy(void* self, void* /*edx*/, std::uint32_t flags) {
    reinterpret_cast<MemberFn>(kMappingDestroy)(self, nullptr);
    if ((flags & 1) != 0) {
        msvc6::GameOperatorDelete(self);
    }
    return self;
}

S3RE_HOOK(0x0061a7a0, Win32_CFileMapping_Close, MappingCloseFn, Original_Win32_CFileMapping_Close,
          Win32_CFileMapping_Close);

// Purpose: closes a file mapping and deletes the file it maps (when the
// graphics are released).
std::uint32_t __fastcall Win32_CFileMapping_Close(void* self, void* /*edx*/) {
    CFileMapping* const mapping = static_cast<CFileMapping*>(self);
    if (mapping->file == 0) {
        return 0;
    }
    std::uint32_t result = reinterpret_cast<MemberFn>(kCWin32HandleClose)(self, nullptr);
    void* const file = reinterpret_cast<void*>(mapping->file);
    if (file != nullptr) {
        const auto* const vtable = *static_cast<const std::uint32_t* const*>(file);
        result = reinterpret_cast<DeletingFn>(vtable[0])(file, nullptr, 1);
    }
    mapping->file = 0;
    return result;
}

S3RE_HOOK(0x0061a520, Win32_CFileMapping_Create, MappingCreateFn, Original_Win32_CFileMapping_Create,
          Win32_CFileMapping_Create);

// Purpose: creates the mapping object of a file (or of the paging file
// without one); the mapping owns the file from now on.
std::uint32_t __fastcall Win32_CFileMapping_Create(void* self, void* /*edx*/, void* file, void* security,
                                                   std::uint32_t protect, std::uint32_t sizeHigh,
                                                   std::uint32_t sizeLow, const char* name) {
    CFileMapping* const mapping = static_cast<CFileMapping*>(self);
    mapping->file = reinterpret_cast<std::uint32_t>(file);
    // The file's handle (Folded_GetMember4 0x618250), or the paging file.
    void* const fileHandle = file != nullptr ? HandleOf(file) : reinterpret_cast<void*>(kInvalidHandle);
    void* const object = g_importCreateFileMappingA.get()(fileHandle, security, protect, sizeHigh, sizeLow, name);
    if (object == nullptr) {
        ThrowWin32Error(texts::kMappingNotNull, kLineCreateMapping, 0);
    }
    const std::uint32_t handle = reinterpret_cast<std::uint32_t>(object);
    mapping->handle.handle = handle;  // CWin32Handle::Set(handle, 1) 0x60bf00
    mapping->handle.owned = 1;
    return handle;
}

S3RE_HOOK(0x0061a7d0, Win32_CFileMapping_MapView, MappingMapViewFn, Original_Win32_CFileMapping_MapView,
          Win32_CFileMapping_MapView);

// Purpose: maps a view of the file into memory and remembers it; the
// graphics loader draws straight from these views.
std::uint32_t __fastcall Win32_CFileMapping_MapView(void* self, void* /*edx*/, std::uint32_t access,
                                                    std::uint32_t offsetHigh, std::uint32_t offsetLow,
                                                    std::uint32_t size) {
    CFileMapping* const mapping = static_cast<CFileMapping*>(self);
    void* const address = g_importMapViewOfFile.get()(HandleOf(self), access, offsetHigh, offsetLow, size);
    if (address == nullptr) {
        ThrowWin32Error(texts::kBaseAddressNotNull, kLineMapView, 0);
    }
    const std::uint32_t view = reinterpret_cast<std::uint32_t>(address);
    AppendView(&mapping->views, view);
    return view;
}

S3RE_HOOK(0x0061aa40, Win32_CFileMapping_UnmapView, MappingUnmapViewFn, Original_Win32_CFileMapping_UnmapView,
          Win32_CFileMapping_UnmapView);

// Purpose: unmaps a view and forgets it.
std::uint32_t __fastcall Win32_CFileMapping_UnmapView(void* self, void* /*edx*/, std::uint32_t view) {
    CFileMapping* const mapping = static_cast<CFileMapping*>(self);
    g_importUnmapViewOfFile.get()(reinterpret_cast<const void*>(view));
    WCPtrOrderedVector* const views = &mapping->views;
    std::int32_t index = 0;
    if (views->length == 0) {
        return 0;
    }
    const auto* const vtable = reinterpret_cast<const std::uint32_t*>(views->vtable);
    const std::uint32_t found = reinterpret_cast<FindFn>(vtable[kFindViewSlot])(views, nullptr, &view, 1, &index);
    if (found == 0) {
        return 0;
    }
    auto entry = [views](std::int32_t i) {
        return reinterpret_cast<std::uint32_t*>(views->data + static_cast<std::uint32_t>(i) * 4);
    };
    for (std::int32_t i = index; i < views->length - 1; ++i) {
        *entry(i) = *entry(i + 1);
    }
    --views->length;
    --views->filled;
    return static_cast<std::uint32_t>(views->filled);
}

}  // namespace s3re::game
