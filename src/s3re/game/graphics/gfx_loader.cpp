// The graphics loader (Dirk\graphicsdata.cpp): CGraphicsData's
// constructor and destructor, the folder search, Open (maps every GFX
// file and finds its tables) and DetachFromMedium. File format and call
// tree: re/graphics_formats.md; layout: gfx_layout.h.
//
// CGraphicsData::CGraphicsData (0x005f67c0): () CObject (0x610320); the
//   name string empty (its allocator byte comes from an uninitialized
//   local of the original: a known difference, ours is 0; the allocator
//   of MSVC 6.0's string is never read, docs/reimplementation_notes.md);
//   languageCount = language = 0; vtable 0x65d3e4; for each of the 70
//   groups a new Win32::CFileMapping (operator new 0x2c, constructor
//   0x61a3a0; null: ThrowOutOfMemory("lpMapping [i]", line 278)) and view
//   = 0. File sizes and tables stay uninitialized. EAX = this.
// CGraphicsData::~CGraphicsData (0x005f68b0): vtable 0x65d3e4; Close
//   (0x5fa690) if group 0 has a view; every mapping's slot 0 (scalar
//   deleting destructor) with 1; the name's destructor (inlined erase +
//   _Tidy, the same as 0x401b60); ~CObject (0x610330).
// CGraphicsData::scalar_deleting_destructor (0x005f6890): (flags) ret 4.
//
// CGraphicsData::FindDirectoryContainingFile (0x005f6a10): thiscall (this
//   unused; result string*, WCValOrderedVector<string>* folders, const
//   string* file) ret 0Ch. Saves the current directory
//   (GetCurrentDirectoryA, VERIFY line 309); then for each folder, while
//   nothing is found: restores the saved directory (VERIFY "bResult" line
//   317, inside `try { } catch (Win32::CHResult* e) { delete e; }`, so a
//   failure is swallowed), takes the folder (the vector's index check
//   0x428c80, which can grow an empty vector), and if SetCurrentDirectoryA
//   succeeds and FindFirstFileA finds the file, the found folder is the
//   then current directory (GetCurrentDirectoryA, line 334) and the find
//   handle is closed (FindClose, line 338). Afterwards the saved directory
//   is restored (unchecked). Nothing found: throws new
//   Win32::CFileNotFound(file, 1). Result = found + '\\'. EAX = result.
//   The result's allocator byte is the low byte of the original's own
//   stack temporary's address (0x408cc0): not reproducible, a known
//   difference (Open assigns the result into the name, which keeps its
//   own allocator).
// CGraphicsData::DetachFromMedium (0x005fa7c0): (otherwise) ret 4: if
//   IsPathOnRemovableDrive(name, otherwise): DebugLog("GFX: Detaching DAT
//   files from medium ..."), then DetachMemory(view, file size) for every
//   group with a view (a negative result, which DetachMemory never
//   returns, would throw ThrowHResult("error check", line 691)). EAX: the
//   check's result, or the address after the views array when it detached.
//
// CGraphicsData::Open (0x005f6fd0): (const DDPIXELFORMAT* format, const
//   string* basePath, detachMode, groupSet) ret 10h. Step by step:
//   - 70 group names: "00".."17", "20".."27", "30".."37", "40".."48",
//     "60", "61" at their group numbers, the others empty. groupSet 0, 1
//     or 3 empties group 60; groupSet other than 1, 3 and 4 empties 40..49;
//     group 61 is emptied unless g_config's byte +0x159 is set (getter
//     0x413be0).
//   - The pixel format (32 bytes) is copied to +4; its R, G and B masks as
//     _itoa(mask, 16) make the format name ("f8007e01f" = RGB565).
//   - The file searched for is "siedler3_" + group 0's name + "." + format
//     + ".dat" (a "siedler3_??." pattern is built first and overwritten),
//     in the folders ".", basePath + "Gfx", ".\Gfx", basePath and
//     basePath + ".." (FindDirectoryContainingFile); the folder found
//     (with a trailing '\') becomes the name (+0x24).
//   - detachMode 2: detach; 3: detach if the folder is on a removable or
//     CD drive; else not. Detaching logs "GFX: Preparing DAT files for
//     detach" and maps copy-on-write (PAGE_WRITECOPY / FILE_MAP_COPY)
//     instead of read-only (PAGE_READONLY / FILE_MAP_READ).
//   - Every group with a name: new CFile (0xc; null:
//     ThrowOutOfMemory("lpFile", line 441)), Open(name + "siedler3_NN." +
//     format + ".dat", GENERIC_READ, FILE_SHARE_READ, no security,
//     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN),
//     file size, the group's mapping Create(file, ...) and MapView. A view
//     whose first dword is 0x1404 logs "Falsche Gfx-Datei %i" and asserts
//     (line 476). The eight tables are view + the header's offsets
//     (re/graphics_formats.md); the first text table with a language
//     count sets languageCount.
//   - The shading masks (g_shadeMasks) for RGB565 or RGB555 (any other
//     format asserts, line 585), then SetShadowSpan(masks, method 0).
//   Strings: the original builds every name and path through a chain of
//   std::string temporaries; ours makes the same string calls in the same
//   order, so the game's heap sees the same allocations (the emulator
//   compares them). Their allocator bytes come from uninitialized locals,
//   ours are 0; none of these strings survives the call.
//   Known difference: when a call throws (a missing GFX file:
//   CFileNotFound; a full heap), the original's exception frame frees the
//   local strings, ours leak them (only on the way to a fatal error).

#include "game/debug_log.h"
#include "game/framework/framework_layout.h"
#include "game/globals.h"
#include "game/graphics/gfx_layout.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/catch_match.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace s3re::game {

// win32_classes.cpp
void* __fastcall Win32_CObject_Construct(void* self, void* edx);
void __fastcall Win32_CObject_Destroy(void* self, void* edx);

namespace {

using gfx::CGraphicsData;
using msvc6::String;

constexpr std::uintptr_t kMappingConstruct = 0x0061a3a0;
constexpr std::uintptr_t kThrowOutOfMemory = 0x00610960;
constexpr std::uintptr_t kThrowAssertmentFailed = 0x006119b0;
constexpr std::uintptr_t kThrowWin32Error = 0x00610de0;
constexpr std::uintptr_t kThrowHResult = 0x00611050;
constexpr std::uintptr_t kCFileNotFoundConstruct = 0x00611db0;
constexpr std::uintptr_t kCxxThrowException = 0x0061f027;
constexpr std::uintptr_t kGfxClose = 0x005fa690;
constexpr std::uintptr_t kStringVectorIndex = 0x00428c80;      // WCValOrderedVector<string>::IndexCheck
constexpr std::uintptr_t kStringVectorConstruct = 0x005fda00;  // WCValOrderedVector<string>(capacity, grow)
constexpr std::uintptr_t kStringVectorInsertAt = 0x004148a0;   // insertAt(index, const string&)
constexpr std::uintptr_t kStringVectorDestroy = 0x00409d50;
constexpr std::uintptr_t kStringDefault = 0x00523540;          // string() (allocator from the stack)
constexpr std::uintptr_t kStringAppendSubstr = 0x0042f920;     // append(const string&, pos, n)
constexpr std::uintptr_t kStringNotEqualText = 0x00413ad0;     // fastcall (string*, text): *s != text
constexpr std::uintptr_t kItoa = 0x006212f7;
constexpr std::uintptr_t kStringCompareText = 0x0040a0e0;          // compare(const char*)                   // CRT _itoa
constexpr std::uintptr_t kFindDirectory = 0x005f6a10;
constexpr std::uintptr_t kIsPathOnRemovableDrive = 0x005fa750;
constexpr std::uintptr_t kDetachMemory = 0x0061ab60;
constexpr std::uintptr_t kCFileConstruct = 0x006195c0;
constexpr std::uintptr_t kCFileOpen = 0x00619610;
constexpr std::uintptr_t kCFileGetSize = 0x0061a080;
constexpr std::uintptr_t kMappingCreate = 0x0061a520;
constexpr std::uintptr_t kMappingMapView = 0x0061a7d0;
constexpr std::uintptr_t kSetShadowSpan = 0x005fcd30;

constexpr std::uint32_t kMappingSize = 0x2c;       // sizeof(Win32::CFileMapping)
constexpr std::uint32_t kCFileSize = 0xc;          // sizeof(Win32::CFile)
constexpr std::uint32_t kFileNotFoundSize = 0x28;  // sizeof(Win32::CFileNotFound)
constexpr std::uint32_t kPathBuffer = 0x400;       // the folder buffers (MAX_PATH would do)
constexpr std::uint32_t kBadHeaderMagic = 0x1404;  // "Falsche Gfx-Datei" (wrong graphics file)
constexpr std::uint32_t kFolderVectorCapacity = 10;
constexpr std::uint32_t kFolderVectorGrowth = 5;

// Source lines (Dirk\graphicsdata.cpp).
constexpr std::uint32_t kLineNewMapping = 278;
constexpr std::uint32_t kLineSaveDirectory = 309;
constexpr std::uint32_t kLineRestoreDirectory = 317;
constexpr std::uint32_t kLineFoundDirectory = 334;
constexpr std::uint32_t kLineFindClose = 338;
constexpr std::uint32_t kLineNewFile = 441;
constexpr std::uint32_t kLineBadHeader = 476;
constexpr std::uint32_t kLinePixelFormat = 585;
constexpr std::uint32_t kLineDetach = 691;

// Open's detach modes (third argument).
constexpr std::uint32_t kDetachAlways = 2;
constexpr std::uint32_t kDetachIfRemovable = 3;

// Win32::CFile::Open's arguments in Open.
constexpr std::uint32_t kFileFlags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN;

// The header fields of a GFX file (re/graphics_formats.md).
struct GfxFileHeader {
    std::uint32_t magic;         // +0x00
    std::uint8_t unk04[0x30];
    std::uint32_t textTable;     // +0x34
    std::uint32_t landscapeTable;  // +0x38
    std::uint32_t guiTable;      // +0x3c
    std::uint32_t animationTable;  // +0x40
    std::uint32_t colorIndexedTable;  // +0x44
    std::uint32_t shadowTable;   // +0x48
    std::uint32_t sequenceTable;  // +0x4c
    std::uint32_t paletteTable;  // +0x50
};
static_assert(offsetof(GfxFileHeader, textTable) == 0x34);
static_assert(offsetof(GfxFileHeader, paletteTable) == 0x50);

// WCValOrderedVector<string> as Open builds it (0x1c bytes; only the
// length is read here).
struct StringVector {
    std::uint32_t vtable;
    std::uint32_t unk04;
    String* data;
    std::uint32_t capacity;
    std::int32_t filled;
    std::uint32_t length;  // +0x14
    std::uint32_t grow;
};
static_assert(sizeof(StringVector) == 0x1c);

using MemberFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using DeletingFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t flags);
using ThrowAtFn = void(__fastcall*)(const char* text, const char* file, std::uint32_t line);
using ThrowWin32ErrorFn = void(__fastcall*)(const char* expression, const char* file, std::uint32_t line,
                                            std::uint32_t error, const String* message);
using ThrowHResultFn = void(__fastcall*)(const char* expression, const char* file, std::uint32_t line,
                                         std::uint32_t result, const String* message);
using CFileNotFoundConstructFn = void*(__fastcall*)(void* self, void* edx, const char* name,
                                                    std::uint32_t mostDerived);
using CxxThrowExceptionFn = void(__stdcall*)(void* object, std::uintptr_t throwInfo);
using IndexFn = std::uint32_t(__fastcall*)(void* vector, void* edx, std::uint32_t index);
using VectorConstructFn = void*(__fastcall*)(void* vector, void* edx, std::uint32_t capacity, std::uint32_t grow);
using InsertAtFn = std::uint32_t(__fastcall*)(void* vector, void* edx, std::uint32_t index, const String* value);
using AppendFn = String*(__fastcall*)(String* self, void* edx, const String* x, std::uint32_t pos, std::uint32_t n);
using NotEqualTextFn = bool(__fastcall*)(const String* self, const char* text);
using CompareTextFn = std::int32_t(__fastcall*)(const String* self, void* edx, const char* text);
using ItoaFn = char*(__cdecl*)(std::uint32_t value, char* buffer, std::int32_t radix);
using FindDirectoryFn = String*(__fastcall*)(void* self, void* edx, String* result, void* folders,
                                             const String* file);
using RemovableFn = std::uint32_t(__fastcall*)(void* self, void* edx, const String* path, std::uint32_t otherwise);
using DetachMemoryFn = std::int32_t(__fastcall*)(std::uint32_t start, std::uint32_t size);
using FileOpenFn = std::uint32_t(__fastcall*)(void* self, void* edx, const char* name, std::uint32_t access,
                                              std::uint32_t share, void* security, std::uint32_t disposition,
                                              std::uint32_t flags, void* templateFile);
using FileGetSizeFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t* high);
using MappingCreateFn = std::uint32_t(__fastcall*)(void* self, void* edx, void* file, void* security,
                                                   std::uint32_t protect, std::uint32_t sizeHigh,
                                                   std::uint32_t sizeLow, const char* name);
using MappingMapViewFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t access,
                                                    std::uint32_t offsetHigh, std::uint32_t offsetLow,
                                                    std::uint32_t size);
using SetShadowSpanFn = void(__fastcall*)(std::uint32_t halfMask16, std::uint32_t halfMask32, std::uint32_t mask18,
                                          std::uint32_t mask1c, std::uint32_t method);

template <typename Fn>
Fn CallAt(std::uintptr_t address) {
    return reinterpret_cast<Fn>(address);
}

const char* CStr(const String* s) {
    return msvc6::detail::CStr(s);
}

std::uint32_t Npos() {
    return msvc6::detail::Npos();
}

// VERIFY failed in graphicsdata.cpp: ThrowWin32Error with an empty
// message (allocator byte `allocator`).
void ThrowWin32Error(const char* expression, std::uint32_t line, std::uint32_t allocator) {
    String message{};
    message.allocator = static_cast<std::uint8_t>(allocator);
    const std::uint32_t error = g_importGetLastError.get()();
    CallAt<ThrowWin32ErrorFn>(kThrowWin32Error)(expression, texts::kGraphicsDataFile, line, error, &message);
}

// ---------------------------------------------------------------------------
// FindDirectoryContainingFile's try block
// ---------------------------------------------------------------------------

// The try block: restore the saved directory, VERIFY'd.
void RestoreSavedDirectory(const char* saved) {
    if (g_importSetCurrentDirectoryA.get()(saved) == 0) {
        ThrowWin32Error(texts::kBResult, kLineRestoreDirectory, 0);
    }
}

int CatchHResult(const EXCEPTION_POINTERS* info, void** caught) {
    const msvc6::CatchMatch match = msvc6::MatchPointerCatch(info->ExceptionRecord, type_descriptor::kCHResultPointer,
                                                            ".PAVCHResult@Win32@@");
    if (!match.matched) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    *caught = match.object;
    return EXCEPTION_EXECUTE_HANDLER;
}

// try { RestoreSavedDirectory } catch (Win32::CHResult* e) { delete e; }
// (no C++ objects in this frame, so __try is allowed).
void RestoreSavedDirectoryGuarded(const char* saved) {
    void* caught = nullptr;
    __try {
        RestoreSavedDirectory(saved);
        return;
    } __except (CatchHResult(GetExceptionInformation(), &caught)) {
    }
    msvc6::DeleteCaughtException(caught);
}

// throw new Win32::CFileNotFound(name).
void ThrowFileNotFound(const char* name) {
    void* const object = msvc6::GameOperatorNew(kFileNotFoundSize);
    void* thrown = nullptr;
    if (object != nullptr) {
        thrown = CallAt<CFileNotFoundConstructFn>(kCFileNotFoundConstruct)(object, nullptr, name, 1);
    }
    CallAt<CxxThrowExceptionFn>(kCxxThrowException)(&thrown, throw_info::kCFileNotFoundPointer);
}

// ---------------------------------------------------------------------------
// Open's strings
// ---------------------------------------------------------------------------

// A group name as the original's array initializer builds it:
// string(), then assigned from string(string(text)).
void InitGroupName(String* name, const char* text) {
    std::uint8_t allocator = 0;
    msvc6::String_Default(name, nullptr, &allocator);
    String text1;
    String text2;
    msvc6::String_FromText(&text1, nullptr, text, &allocator);
    msvc6::String_FromSubstr(&text2, nullptr, &text1, 0, Npos(), &allocator);
    msvc6::String_AssignSubstr(name, nullptr, &text2, 0, Npos());
    msvc6::String_Tidy(&text2, nullptr, 1);
    msvc6::String_Tidy(&text1, nullptr, 1);
}

// The group names, by group (null: no group; "" then).
const char* GroupNameText(std::size_t group) {
    // The literals sit 4 bytes apart in S3.EXE, from "61" up to "00".
    static const std::uint8_t kGroups[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                                           16, 17, 20, 21, 22, 23, 24, 25, 26, 27, 30, 31, 32, 33, 34, 35,
                                           36, 37, 40, 41, 42, 43, 44, 45, 46, 47, 48, 60, 61};
    constexpr std::size_t kCount = sizeof(kGroups);
    static_assert(kCount == kGfxGroupNameCount);
    for (std::size_t i = 0; i < kCount; ++i) {
        if (kGroups[i] == group) {
            return g_gfxGroupNames[kCount - 1 - i];
        }
    }
    return nullptr;
}

// x = itoa(value, 16) as a temporary string (string(const char*, npos)).
void HexString(String* out, std::uint32_t value, char* buffer) {
    CallAt<ItoaFn>(kItoa)(value, buffer, 16);
    msvc6::String_FromChars(out, nullptr, buffer, 0xffffffff);
}

// file = "siedler3_" + group + "." + format + ".dat", built like the original.
void GroupFileName(String* file, const String* group, const String* format) {
    String prefix;
    String withGroup;
    String withDot;
    String withFormat;
    String full;
    msvc6::String_FromChars(&prefix, nullptr, texts::kSiedler3Prefix, 0xffffffff);
    msvc6::String_ConcatString(&withGroup, &prefix, group);
    msvc6::String_ConcatChars(&withDot, &withGroup, texts::kFullStop);
    msvc6::String_ConcatString(&withFormat, &withDot, format);
    msvc6::String_ConcatChars(&full, &withFormat, texts::kDatExtension);
    msvc6::String_AssignSubstr(file, nullptr, &full, 0, Npos());
    msvc6::String_Destroy(&full, nullptr);
    msvc6::String_Destroy(&withFormat, nullptr);
    msvc6::String_Destroy(&withDot, nullptr);
    msvc6::String_Destroy(&withGroup, nullptr);
    msvc6::String_Destroy(&prefix, nullptr);
}

void AppendFolder(StringVector* folders, const String* folder) {
    CallAt<InsertAtFn>(kStringVectorInsertAt)(folders, nullptr, folders->length, folder);
}

}  // namespace

void* __fastcall CGraphicsData_Construct(void* self, void* edx);
void* __fastcall CGraphicsData_DeletingDestroy(void* self, void* edx, std::uint32_t flags);
void __fastcall CGraphicsData_Destroy(void* self, void* edx);
String* __fastcall CGraphicsData_FindDirectoryContainingFile(void* self, void* edx, String* result, void* folders,
                                                            const String* file);
std::uint32_t __fastcall CGraphicsData_DetachFromMedium(void* self, void* edx, std::uint32_t otherwise);
void __fastcall CGraphicsData_Open(void* self, void* edx, const void* format, const String* basePath,
                                   std::uint32_t detachMode, std::uint32_t groupSet);

using GfxConstructFn = decltype(&CGraphicsData_Construct);
using GfxDeletingFn = decltype(&CGraphicsData_DeletingDestroy);
using GfxDestroyFn = decltype(&CGraphicsData_Destroy);
using GfxFindDirectoryFn = decltype(&CGraphicsData_FindDirectoryContainingFile);
using GfxDetachFn = decltype(&CGraphicsData_DetachFromMedium);
using GfxOpenFn = decltype(&CGraphicsData_Open);

S3RE_HOOK(0x005f67c0, CGraphicsData_Construct, GfxConstructFn, Original_CGraphicsData_Construct,
          CGraphicsData_Construct);

// Purpose: constructor of the graphics data (one mapping object per
// graphics file group, nothing loaded yet).
void* __fastcall CGraphicsData_Construct(void* self, void* /*edx*/) {
    CGraphicsData* const gfx = static_cast<CGraphicsData*>(self);
    Win32_CObject_Construct(self, nullptr);
    gfx->name.allocator = 0;  // the original's is an uninitialized local (known difference)
    msvc6::detail::Clear(&gfx->name);
    gfx->languageCount = 0;
    gfx->language = 0;
    gfx->vtable = vtables::kCGraphicsData;
    for (std::size_t group = 0; group < gfx::kGroups; ++group) {
        void* const block = msvc6::GameOperatorNew(kMappingSize);
        void* mapping = nullptr;
        if (block != nullptr) {
            mapping = CallAt<void*(__fastcall*)(void*, void*)>(kMappingConstruct)(block, nullptr);
        }
        gfx->mappings[group] = reinterpret_cast<std::uint32_t>(mapping);
        if (mapping == nullptr) {
            CallAt<ThrowAtFn>(kThrowOutOfMemory)(texts::kLpMapping, texts::kGraphicsDataFile, kLineNewMapping);
        }
        gfx->views[group] = 0;
    }
    return self;
}

S3RE_HOOK(0x005f68b0, CGraphicsData_Destroy, GfxDestroyFn, Original_CGraphicsData_Destroy, CGraphicsData_Destroy);

// Purpose: destructor of the graphics data: unmaps the files and deletes
// the mapping objects.
void __fastcall CGraphicsData_Destroy(void* self, void* /*edx*/) {
    CGraphicsData* const gfx = static_cast<CGraphicsData*>(self);
    gfx->vtable = vtables::kCGraphicsData;
    if (gfx->views[0] != 0) {
        CallAt<MemberFn>(kGfxClose)(self, nullptr);
    }
    for (std::size_t group = 0; group < gfx::kGroups; ++group) {
        void* const mapping = reinterpret_cast<void*>(gfx->mappings[group]);
        if (mapping != nullptr) {
            const auto* const vtable = *static_cast<const std::uint32_t* const*>(mapping);
            reinterpret_cast<DeletingFn>(vtable[0])(mapping, nullptr, 1);
        }
    }
    msvc6::String_Destroy(&gfx->name, nullptr);
    Win32_CObject_Destroy(self, nullptr);
}

S3RE_HOOK(0x005f6890, CGraphicsData_DeletingDestroy, GfxDeletingFn, Original_CGraphicsData_DeletingDestroy,
          CGraphicsData_DeletingDestroy);

// Purpose: the compiler-generated "delete this object" entry of the vtable.
void* __fastcall CGraphicsData_DeletingDestroy(void* self, void* /*edx*/, std::uint32_t flags) {
    CGraphicsData_Destroy(self, nullptr);
    if ((flags & 1) != 0) {
        msvc6::GameOperatorDelete(self);
    }
    return self;
}

S3RE_HOOK(0x005f6a10, CGraphicsData_FindDirectoryContainingFile, GfxFindDirectoryFn,
          Original_CGraphicsData_FindDirectoryContainingFile, CGraphicsData_FindDirectoryContainingFile);

// Purpose: finds the folder (of a list of candidates) that holds a
// graphics file: the game can run from the install folder or the CD.
String* __fastcall CGraphicsData_FindDirectoryContainingFile(void* /*self*/, void* /*edx*/, String* result,
                                                            void* folders, const String* file) {
    StringVector* const list = static_cast<StringVector*>(folders);
    // The allocator byte of `found` and of the messages: the top byte of
    // the `folders` argument's stack slot.
    const std::uint32_t allocator = reinterpret_cast<std::uint32_t>(folders) >> 24;
    String found{};
    found.allocator = static_cast<std::uint8_t>(allocator);
    char saved[kPathBuffer];
    if (g_importGetCurrentDirectoryA.get()(kPathBuffer, saved) == 0) {
        ThrowWin32Error(texts::kDwResultNotZero, kLineSaveDirectory, allocator);
    }
    std::uint32_t index = 0;
    do {
        RestoreSavedDirectoryGuarded(saved);
        const std::uint32_t at = CallAt<IndexFn>(kStringVectorIndex)(list, nullptr, index);
        const char* const folder = CStr(&list->data[at]);
        if (g_importSetCurrentDirectoryA.get()(folder) != 0) {
            WIN32_FIND_DATAA data;
            void* const search = g_importFindFirstFileA.get()(CStr(file), &data);
            if (search != INVALID_HANDLE_VALUE) {
                char current[kPathBuffer];
                if (g_importGetCurrentDirectoryA.get()(kPathBuffer, current) == 0) {
                    ThrowWin32Error(texts::kDwResultNotZero, kLineFoundDirectory, allocator);
                }
                std::uint8_t textAllocator = 0;
                String text;
                msvc6::String_FromText(&text, nullptr, current, &textAllocator);
                msvc6::String_AssignSubstr(&found, nullptr, &text, 0, Npos());
                msvc6::String_Tidy(&text, nullptr, 1);
                if (g_importFindClose.get()(search) == 0) {
                    ThrowWin32Error(texts::kBResult, kLineFindClose, 0);
                }
            }
        }
        ++index;
    } while (index < list->length && CallAt<CompareTextFn>(kStringCompareText)(&found, nullptr, texts::kEmpty) == 0);
    g_importSetCurrentDirectoryA.get()(saved);
    if (CallAt<CompareTextFn>(kStringCompareText)(&found, nullptr, texts::kEmpty) == 0) {
        ThrowFileNotFound(CStr(file));
    }
    String withSlash;
    msvc6::String_ConcatChar(&withSlash, &found, '\\');
    msvc6::String_FromSubstrDefault(result, nullptr, &withSlash, 0, Npos());
    msvc6::String_Destroy(&withSlash, nullptr);
    msvc6::String_Destroy(&found, nullptr);
    return result;
}

S3RE_HOOK(0x005fa7c0, CGraphicsData_DetachFromMedium, GfxDetachFn, Original_CGraphicsData_DetachFromMedium,
          CGraphicsData_DetachFromMedium);

// Purpose: when the graphics come from a CD (or removable drive), copies
// all mapped files into memory so the CD can be taken out.
std::uint32_t __fastcall CGraphicsData_DetachFromMedium(void* self, void* /*edx*/, std::uint32_t otherwise) {
    CGraphicsData* const gfx = static_cast<CGraphicsData*>(self);
    const std::uint32_t removable = CallAt<RemovableFn>(kIsPathOnRemovableDrive)(self, nullptr, &gfx->name, otherwise);
    if ((removable & 0xff) == 0) {
        return removable;
    }
    GameDebugLog(texts::kGfxDetaching);
    for (std::size_t group = 0; group < gfx::kGroups; ++group) {
        if (gfx->views[group] == 0) {
            continue;
        }
        const std::int32_t result = CallAt<DetachMemoryFn>(kDetachMemory)(gfx->views[group], gfx->fileSizes[group]);
        if (result < 0) {
            String message{};
            message.allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uint32_t>(&gfx->views[group]));
            CallAt<ThrowHResultFn>(kThrowHResult)(texts::kErrorCheck, texts::kGraphicsDataFile, kLineDetach,
                                              static_cast<std::uint32_t>(result), &message);
        }
    }
    return reinterpret_cast<std::uint32_t>(&gfx->views[gfx::kGroups]);
}

S3RE_HOOK(0x005f6fd0, CGraphicsData_Open, GfxOpenFn, Original_CGraphicsData_Open, CGraphicsData_Open);

// Purpose: loads the graphics: finds the GFX folder, maps every graphics
// file of the screen's pixel format into memory and records where its
// tables are.
void __fastcall CGraphicsData_Open(void* self, void* /*edx*/, const void* format, const String* basePath,
                                   std::uint32_t detachMode, std::uint32_t groupSet) {
    CGraphicsData* const gfx = static_cast<CGraphicsData*>(self);
    std::uint8_t allocator = 0;

    // The searched file name and the 70 group names.
    String file;
    msvc6::String_Default(&file, nullptr, &allocator);
    String names[gfx::kGroups];
    constexpr std::size_t kInitializedNames = 59;  // names[59..61] are built from C text, the rest default
    for (std::size_t group = 0; group < kInitializedNames; ++group) {
        const char* const text = GroupNameText(group);
        InitGroupName(&names[group], text != nullptr ? text : texts::kEmpty.c_str());
    }
    for (std::size_t group = kInitializedNames; group <= 61; ++group) {
        const char* const text = GroupNameText(group);
        msvc6::String_FromChars(&names[group], nullptr, text != nullptr ? text : texts::kEmpty.c_str(), 0xffffffff);
    }
    std::memset(&names[62], 0, sizeof(String) * (gfx::kGroups - 62));
    for (std::size_t group = 62; group < gfx::kGroups; ++group) {
        CallAt<MemberFn>(kStringDefault)(&names[group], nullptr);
    }
    String formatName;
    CallAt<MemberFn>(kStringDefault)(&formatName, nullptr);
    StringVector folders;
    CallAt<VectorConstructFn>(kStringVectorConstruct)(&folders, nullptr, kFolderVectorCapacity, kFolderVectorGrowth);

    // The installed content: groups left out by groupSet and g_config.
    constexpr std::size_t kGroupExtra = 60;
    constexpr std::size_t kGroupAddOn = 61;
    constexpr std::size_t kGroupsRace4 = 40;
    if (groupSet == 0 || groupSet == 1 || groupSet == 3) {
        msvc6::String_AssignTextOrEmpty(&names[kGroupExtra], nullptr, texts::kEmpty);
    }
    if (groupSet != 3 && groupSet != 4 && groupSet != 1) {
        for (std::size_t group = kGroupsRace4; group < kGroupsRace4 + 10; ++group) {
            msvc6::String_AssignTextOrEmpty(&names[group], nullptr, texts::kEmpty);
        }
    }
    if (g_config->gfx_group61 == 0) {
        msvc6::String_AssignTextOrEmpty(&names[kGroupAddOn], nullptr, texts::kEmpty);
    }

    // The pixel format and its name (the R, G and B masks in hex).
    std::memcpy(gfx->pixelFormat, format, sizeof(gfx->pixelFormat));
    const auto* const masks = reinterpret_cast<const std::uint32_t*>(gfx->pixelFormat + 0x10);  // dwRBitMask..
    char digits[3][36];
    String hex;
    HexString(&hex, masks[0], digits[0]);
    msvc6::String_AssignSubstr(&formatName, nullptr, &hex, 0, Npos());
    msvc6::String_Destroy(&hex, nullptr);
    HexString(&hex, masks[1], digits[1]);
    CallAt<AppendFn>(kStringAppendSubstr)(&formatName, nullptr, &hex, 0, Npos());
    msvc6::String_Destroy(&hex, nullptr);
    HexString(&hex, masks[2], digits[2]);
    CallAt<AppendFn>(kStringAppendSubstr)(&formatName, nullptr, &hex, 0, Npos());
    msvc6::String_Destroy(&hex, nullptr);

    // The file to search for: first a "??" pattern, then group 0's file
    // (which replaces it).
    {
        String pattern;
        String withFormat;
        String full;
        msvc6::String_FromChars(&pattern, nullptr, texts::kSiedler3Pattern, 0xffffffff);
        msvc6::String_ConcatString(&withFormat, &pattern, &formatName);
        msvc6::String_ConcatChars(&full, &withFormat, texts::kDatExtension);
        msvc6::String_AssignSubstr(&file, nullptr, &full, 0, Npos());
        msvc6::String_Destroy(&full, nullptr);
        msvc6::String_Destroy(&withFormat, nullptr);
        msvc6::String_Destroy(&pattern, nullptr);
    }
    GroupFileName(&file, &names[0], &formatName);

    // The folders to look in.
    {
        String folder;
        msvc6::String_FromChars(&folder, nullptr, texts::kFullStop, 0xffffffff);
        AppendFolder(&folders, &folder);
        msvc6::String_Destroy(&folder, nullptr);
        msvc6::String_ConcatChars(&folder, basePath, texts::kGfxFolder);
        AppendFolder(&folders, &folder);
        msvc6::String_Destroy(&folder, nullptr);
        msvc6::String_FromChars(&folder, nullptr, texts::kDotGfxFolder, 0xffffffff);
        AppendFolder(&folders, &folder);
        msvc6::String_Destroy(&folder, nullptr);
        AppendFolder(&folders, basePath);
        msvc6::String_ConcatChars(&folder, basePath, texts::kParentFolder);
        AppendFolder(&folders, &folder);
        msvc6::String_Destroy(&folder, nullptr);
    }
    {
        String found;
        CallAt<FindDirectoryFn>(kFindDirectory)(self, nullptr, &found, &folders, &file);
        msvc6::String_AssignSubstr(&gfx->name, nullptr, &found, 0, Npos());
        msvc6::String_Destroy(&found, nullptr);
    }

    bool detach = false;
    if (detachMode == kDetachAlways) {
        detach = true;
    } else if (detachMode == kDetachIfRemovable) {
        detach = (CallAt<RemovableFn>(kIsPathOnRemovableDrive)(self, nullptr, &gfx->name, 0) & 0xff) != 0;
    }
    if (detach) {
        GameDebugLog(texts::kGfxPreparingDetach);
    }

    // Map every group.
    for (std::int32_t group = 0; group < static_cast<std::int32_t>(gfx::kGroups); ++group) {
        if (!CallAt<NotEqualTextFn>(kStringNotEqualText)(&names[group], texts::kEmpty)) {
            continue;
        }
        void* const block = msvc6::GameOperatorNew(kCFileSize);
        void* fileObject = nullptr;
        if (block != nullptr) {
            fileObject = CallAt<void*(__fastcall*)(void*, void*)>(kCFileConstruct)(block, nullptr);
        }
        if (fileObject == nullptr) {
            CallAt<ThrowAtFn>(kThrowOutOfMemory)(texts::kLpFile, texts::kGraphicsDataFile, kLineNewFile);
        }
        GroupFileName(&file, &names[group], &formatName);
        String path;
        msvc6::String_ConcatString(&path, &gfx->name, &file);
        CallAt<FileOpenFn>(kCFileOpen)(fileObject, nullptr, CStr(&path), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, kFileFlags, nullptr);
        msvc6::String_Destroy(&path, nullptr);
        gfx->fileSizes[group] = CallAt<FileGetSizeFn>(kCFileGetSize)(fileObject, nullptr, nullptr);
        CallAt<MappingCreateFn>(kMappingCreate)(reinterpret_cast<void*>(gfx->mappings[group]), nullptr, fileObject,
                                            nullptr, detach ? PAGE_WRITECOPY : PAGE_READONLY, 0, 0, nullptr);
        const std::uint32_t view = CallAt<MappingMapViewFn>(kMappingMapView)(
            reinterpret_cast<void*>(gfx->mappings[group]), nullptr, detach ? FILE_MAP_COPY : FILE_MAP_READ, 0, 0, 0);
        gfx->views[group] = view;
        const auto* const header = reinterpret_cast<const GfxFileHeader*>(view);
        if (header->magic == kBadHeaderMagic) {
            GameDebugLog(texts::kWrongGfxFile, group);
            CallAt<ThrowAtFn>(kThrowAssertmentFailed)(texts::kGfxHeaderAssert, texts::kGraphicsDataFile, kLineBadHeader);
        }
        // The tables, in the original's order (each adds the view read again).
        gfx->textTables[group] = reinterpret_cast<const gfx::TextTable*>(header->textTable + gfx->views[group]);
        gfx->animationTables[group] =
            reinterpret_cast<const gfx::OffsetTable*>(header->animationTable + gfx->views[group]);
        gfx->colorIndexedTables[group] =
            reinterpret_cast<const gfx::OffsetTable*>(header->colorIndexedTable + gfx->views[group]);
        gfx->shadowTables[group] = reinterpret_cast<const gfx::OffsetTable*>(header->shadowTable + gfx->views[group]);
        gfx->paletteTables[group] =
            reinterpret_cast<const gfx::PaletteTable*>(header->paletteTable + gfx->views[group]);
        gfx->guiImageTables[group] = reinterpret_cast<const gfx::OffsetTable*>(header->guiTable + gfx->views[group]);
        gfx->landscapeTextureTables[group] =
            reinterpret_cast<const gfx::OffsetTable*>(header->landscapeTable + gfx->views[group]);
        gfx->sequenceTables[group] =
            reinterpret_cast<const gfx::SequenceTable*>(header->sequenceTable + gfx->views[group]);
        const std::uint16_t languages = gfx->textTables[group]->languages;
        if (languages != 0 && gfx->languageCount == 0) {
            gfx->languageCount = languages;
        }
    }

    // The shading masks of the pixel format.
    if (masks[0] == 0xf800 && masks[1] == 0x7e0 && masks[2] == 0x1f) {  // RGB565
        g_shadeMasks->pixel[0] = 0x7bef;
        g_shadeMasks->pixel[1] = 0x39e7;
        g_shadeMasks->pixel[2] = 0x18e3;
        g_shadeMasks->pixel[3] = 0x861;
        g_shadeMasks->pixel[4] = 0x20;
        g_shadeMasks->pair[0] = 0x7bef7bef;
        g_shadeMasks->pair[1] = 0x39e739e7;
        g_shadeMasks->pair[2] = 0x18e318e3;
        g_shadeMasks->pair[3] = 0x8610861;
        g_shadeMasks->pair[4] = 0x200020;
        g_shadeMasks->shadow[0] = 0x7bef7bef;
        g_shadeMasks->shadow[1] = 0x7bef7bef;
    } else if (masks[0] == 0x7c00 && masks[1] == 0x3e0 && masks[2] == 0x1f) {  // RGB555
        g_shadeMasks->pixel[0] = 0x3def;
        g_shadeMasks->pixel[1] = 0x1ce7;
        g_shadeMasks->pixel[2] = 0xc63;
        g_shadeMasks->pixel[3] = 0x421;
        g_shadeMasks->pixel[4] = 0;
        g_shadeMasks->pair[0] = 0x3def3def;
        g_shadeMasks->pair[1] = 0x1ce71ce7;
        g_shadeMasks->pair[2] = 0xc630c63;
        g_shadeMasks->pair[3] = 0x4210421;
        g_shadeMasks->pair[4] = 0;
        g_shadeMasks->shadow[0] = 0x3def3def;
        g_shadeMasks->shadow[1] = 0x3def3def;
    } else {
        CallAt<ThrowAtFn>(kThrowAssertmentFailed)(texts::kGfxPixelFormatAssert, texts::kGraphicsDataFile,
                                              kLinePixelFormat);
    }
    CallAt<SetShadowSpanFn>(kSetShadowSpan)(g_shadeMasks->pixel[0], g_shadeMasks->pair[0], g_shadeMasks->shadow[0],
                                        g_shadeMasks->shadow[1], 0);

    // The locals' destructors, in the original's order.
    CallAt<MemberFn>(kStringVectorDestroy)(&folders, nullptr);
    msvc6::String_Destroy(&formatName, nullptr);
    for (std::size_t group = gfx::kGroups; group-- > 0;) {
        msvc6::String_Destroy(&names[group], nullptr);
    }
    msvc6::String_Destroy(&file, nullptr);
}

}  // namespace s3re::game
