// The sound loader (Dirk\SoundData.cpp): CSoundData's constructor and
// destructors, Close, the folder search, Open (creates the DirectSound
// object and the primary buffer, maps both SND files), DetachFromMedium,
// ReleaseBuffers, Restore and the master volume. File format and call tree:
// re/sound_formats.md; layout: sound_layout.h. The graphics loader
// (graphics/gfx_loader.cpp) is the same code for the GFX files.
//
// CSoundData::CSoundData (0x005fdb60): CDirectSound() (0x5c1cc0, which
//   registers the object in g_directSound); enabled = 0; the folder string
//   empty (its allocator byte comes from an uninitialized local: a known
//   difference, ours is 0); the two dictionaries (WCSkipNonTempBase 0x60b9f0
//   with probability 3, 8 levels, no duplicates; vtables 0x65d41c, 0x65d400);
//   primary = 0; createdTime = timeGetTime(); unk148 = -1; vtable 0x65d3ec;
//   for both files fileSizes = 0, a new Win32::CFileMapping (0x2c; null:
//   ThrowOutOfMemory("lpMapping [i]", line 21)), views = 0. Tables stay
//   uninitialized. EAX = this.
// CSoundData::~CSoundData (0x005fdc90): vtable; Close if views[0] is set;
//   both mappings' slot 0 with 1; the dictionaries (vtable 0x65d454 /
//   0x65d438, a no-op 0x59f200 if they still count entries, clear 0x60bd80,
//   WCExcept's vtable), lastUse first; the folder's destructor (inlined erase
//   + _Tidy); ~CDirectSound. EAX: ~CDirectSound's.
// CSoundData::scalar_deleting_destructor (0x005fdc70): (flags) ret 4.
// CSoundData::Close (0x005ff1c0): enabled = 0; if views[0] is set, for both
//   files with a view: CFileMapping::UnmapView(view) and the mapping's slot
//   1 (Close), then view = table = 0 (both, also without a view);
//   Disconnect (0x5c1da0) if the DirectSound interface is set; the primary
//   buffer's slot 0 with 1 (delete) if any; primary = 0.
// CSoundData::ReleaseBuffers (0x005fde20, vtable slot 2, called by
//   Disconnect): deletes every effect of `effects` (~CSoundEffect 0x5c2b00,
//   operator delete; null values skipped) while walking the dictionary with
//   an inlined iterator, clears it (0x60bd80), then the primary buffer's
//   slot 1 (Release) if there is one.
// CSoundData::Restore (0x005fdef0, vtable slot 3): CSoundEffect::Restore
//   (0x5c2d20) on every effect (not checked for null) inside
//   try { } catch (DirectX::CDirectSoundException* e) { delete e;
//   DebugLog("DSOUND: Cannot restore DSound object"); enabled = 0; }.
// CSoundData::SetVolume (0x005ff9f0): (volume) ret 4: if enabled and there
//   is a primary buffer: IDirectSoundBuffer::SetVolume (COM 0x3c),
//   CDirectSoundBuffer::CheckResult("...DSSetVolume"). EAX not used.
// CSoundData::GetVolume (0x005ffa30): if enabled and there is a primary
//   buffer: DSGetVolume (0x5c2150) into a local that starts at 0; EAX = it
//   (0 otherwise).
// CSoundData::DetachFromMedium (0x005ff230): (otherwise) ret 4: only when
//   enabled; if IsPathOnRemovableDrive(folder, otherwise) (0x5fa750):
//   DebugLog("SND: Detaching DAT files from medium ..."), DetachMemory(view,
//   file size) for both files with a view (a negative result, which never
//   happens, would ThrowHResult("error check", line 377)). The callers don't
//   use EAX (the original returns the SEH chain head when disabled).
//
// CSoundData::FindDirectoryContainingFile (0x005fdfc0): the same search as
//   CGraphicsData's (gfx_loader.cpp) with every Win32 call VERIFY'd (lines
//   86, 92, 102, 106, 115): restoring the saved directory has no catch here,
//   and the final restore is checked too. Not found (0x444710: found ==
//   ""): throw new Win32::CFileNotFound(file, 1). Result = found + '\\'.
//   Its allocator byte comes from the original's stack temporary (a known
//   difference, as in the GFX search).
//
// CSoundData::Open (0x005fe760, with its continuations 0x5fe948, 0x5fe9d8,
//   0x5feb05): (HWND window, const string* basePath, detachMode, format)
//   ret 10h. Step by step:
//   - names "00" and "01" (the GFX group name literals), the folder vector
//     (WCValOrderedVector<string>(10, 5): 0x457ea0 and vtable 0x655440);
//     format = the 4th argument.
//   - try { Connect(null) } catch (CDirectSoundException* e): codes
//     DSERR_ALLOCATED (0x8878000a), DSERR_NODRIVER (0x88780078) and E_FAIL
//     are handled (delete e, DebugLog "DSOUND: Cannot create DSound object",
//     no sound), others rethrown.
//   - With sound: try { SetCooperativeLevel(window, DSSCL_PRIORITY) checked }
//     catch: DSERR_ALLOCATED handled (DebugLog "...Cannot optain priority
//     coop level", SetCooperativeLevel(window, DSSCL_NORMAL) checked, no
//     primary format), others rethrown. Then new CPrimaryBuffer (0xc; null:
//     ThrowOutOfMemory("lpPrimary", line 179)), Create; unless the format
//     step was skipped: try { SetFormat(g_primaryFormats[format 1, 2, 4];
//     another format: ThrowAssertmentFailed("Invalid soundformat", line 199))
//     checked by the buffer's CheckResult } catch: DSERR_BADFORMAT
//     (0x88780064) only logs "DSOUND: PrimaryBuffer is not capable of
//     requested format", others rethrown. Play(0, 0, DSBPLAY_LOOPING)
//     checked; enabled = 1.
//   - file = "siedler3_00.dat" (names[0]); folders ".", basePath + "Snd",
//     ".\Snd", basePath, basePath + ".."; folder = FindDirectoryContainingFile.
//   - detachMode 2: detach; 3: if on a removable or CD drive; detaching logs
//     "SND: Preparing DAT files for detach" and maps copy-on-write.
//   - For each file with a name: new Win32::CFile (0xc; null:
//     ThrowOutOfMemory("lpFile", line 250)), Open(folder +
//     "siedler3_NN.dat", GENERIC_READ, FILE_SHARE_READ, null, OPEN_EXISTING,
//     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, null), the size, the
//     mapping's Create and MapView; unk148 = the header's +0x14 if that is
//     not -1 and unk148 still is; table = view + header.table.
//   - the locals' destructors.
//   The catches are SEH filters here (__except): an unhandled code is not
//   caught at all, which is what the original's `throw;` amounts to. Strings
//   are built through the same chain of temporaries as the original (the
//   emulator compares the heap calls); their allocator bytes come from
//   uninitialized locals there, ours are 0 (none survives the call).
//   Known difference: when a call throws, the original's frame frees its
//   local strings and the folder vector, ours leak them (only on the way to a
//   fatal error: missing SND files, no memory).

#include "game/sound/sound_layout.h"

#include "game/debug_log.h"
#include "game/framework/directx_sound.h"
#include "game/framework/framework_layout.h"
#include "game/globals.h"
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

namespace {

using fileformats::SkipListDict;
using msvc6::String;
using sound::CSoundData;
using sound::SndFileHeader;

constexpr std::uintptr_t kMappingConstruct = 0x0061a3a0;
constexpr std::uintptr_t kMappingUnmapView = 0x0061aa40;
constexpr std::uintptr_t kThrowOutOfMemory = 0x00610960;
constexpr std::uintptr_t kThrowAssertmentFailed = 0x006119b0;
constexpr std::uintptr_t kThrowWin32Error = 0x00610de0;
constexpr std::uintptr_t kThrowHResult = 0x00611050;
constexpr std::uintptr_t kCFileNotFoundConstruct = 0x00611db0;
constexpr std::uintptr_t kCxxThrowException = 0x0061f027;
constexpr std::uintptr_t kDictConstruct = 0x0060b9f0;
constexpr std::uintptr_t kDictClear = 0x0060bd80;
constexpr std::uintptr_t kNoOp = 0x0059f200;               // a bare `ret` (the iterators' error hooks)
constexpr std::uintptr_t kSoundEffectDestroy = 0x005c2b00;  // DirectX::CSoundEffect::~CSoundEffect
constexpr std::uintptr_t kSoundEffectRestore = 0x005c2d20;  // DirectX::CSoundEffect::Restore
constexpr std::uintptr_t kGetMember18 = 0x00613900;         // Win32::CHResult's code (+0x18)
constexpr std::uintptr_t kStringVectorIndex = 0x00428c80;   // WCValOrderedVector<string>::IndexCheck
constexpr std::uintptr_t kStringVectorConstruct = 0x00457ea0;  // WCBareVectorBase<string>(capacity, grow)
constexpr std::uintptr_t kStringVectorInsertAt = 0x004148a0;
constexpr std::uintptr_t kStringVectorDestroy = 0x0040a280;
constexpr std::uintptr_t kStringEqualsText = 0x00444710;    // fastcall (string*, text): *s == text
constexpr std::uintptr_t kStringCompareText = 0x0040a0e0;   // compare(const char*)
constexpr std::uintptr_t kFindDirectory = 0x005fdfc0;
constexpr std::uintptr_t kIsPathOnRemovableDrive = 0x005fa750;
constexpr std::uintptr_t kDetachMemory = 0x0061ab60;
constexpr std::uintptr_t kCFileConstruct = 0x006195c0;
constexpr std::uintptr_t kCFileOpen = 0x00619610;
constexpr std::uintptr_t kCFileGetSize = 0x0061a080;
constexpr std::uintptr_t kMappingCreate = 0x0061a520;
constexpr std::uintptr_t kMappingMapView = 0x0061a7d0;
constexpr std::uintptr_t kSoundClose = 0x005ff1c0;
constexpr std::uintptr_t kConnect = 0x005c1d20;
constexpr std::uintptr_t kDisconnect = 0x005c1da0;
constexpr std::uintptr_t kSoundConstruct = 0x005c1cc0;   // DirectX::CDirectSound()
constexpr std::uintptr_t kSoundDestroy = 0x005c1d00;     // ~CDirectSound
constexpr std::uintptr_t kCheckDSResult = 0x005c1c40;
constexpr std::uintptr_t kBufferCheckResult = 0x005c2000;
constexpr std::uintptr_t kBufferGetVolume = 0x005c2150;
constexpr std::uintptr_t kPrimaryConstruct = 0x005c2180;
constexpr std::uintptr_t kPrimaryCreate = 0x005c21c0;

constexpr std::uint32_t kMappingSize = 0x2c;       // sizeof(Win32::CFileMapping)
constexpr std::uint32_t kCFileSize = 0xc;          // sizeof(Win32::CFile)
constexpr std::uint32_t kPrimarySize = 0xc;        // sizeof(DirectX::CPrimaryBuffer)
constexpr std::uint32_t kFileNotFoundSize = 0x28;  // sizeof(Win32::CFileNotFound)
constexpr std::uint32_t kPathBuffer = 0x400;
constexpr std::uint32_t kFolderVectorCapacity = 10;
constexpr std::uint32_t kFolderVectorGrowth = 5;
constexpr std::uint32_t kDictProbability = 3;
constexpr std::uint32_t kDictMaxLevels = 8;
constexpr std::int32_t kNoValue = -1;

// Source lines (Dirk\SoundData.cpp).
constexpr std::uint32_t kLineNewMapping = 21;
constexpr std::uint32_t kLineSaveDirectory = 86;
constexpr std::uint32_t kLineRestoreDirectory = 92;
constexpr std::uint32_t kLineFoundDirectory = 102;
constexpr std::uint32_t kLineFindClose = 106;
constexpr std::uint32_t kLineFinalRestore = 115;
constexpr std::uint32_t kLineNewPrimary = 179;
constexpr std::uint32_t kLineInvalidFormat = 199;
constexpr std::uint32_t kLineNewFile = 250;
constexpr std::uint32_t kLineDetach = 377;

constexpr std::uint32_t kDetachAlways = 2;
constexpr std::uint32_t kDetachIfRemovable = 3;
constexpr std::uint32_t kFileFlags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN;

// DirectSound values.
constexpr std::int32_t kDSErrAllocated = static_cast<std::int32_t>(0x8878000a);
constexpr std::int32_t kDSErrNoDriver = static_cast<std::int32_t>(0x88780078);
constexpr std::int32_t kEFail = static_cast<std::int32_t>(0x80004005);
constexpr std::int32_t kDSErrBadFormat = static_cast<std::int32_t>(0x88780064);
constexpr std::uint32_t kCoopNormal = 1;    // DSSCL_NORMAL
constexpr std::uint32_t kCoopPriority = 2;  // DSSCL_PRIORITY
constexpr std::uint32_t kPlayLooping = 1;   // DSBPLAY_LOOPING
// COM methods (byte offsets).
constexpr std::uint32_t kSoundSetCooperativeLevel = 0x18;
constexpr std::uint32_t kBufferPlay = 0x30;
constexpr std::uint32_t kBufferSetFormat = 0x38;
constexpr std::uint32_t kBufferSetVolume = 0x3c;
// Game object slots (byte offsets).
constexpr std::uint32_t kSlotDelete = 0x00;
constexpr std::uint32_t kSlotRelease = 0x04;  // CDirectSoundBuffer::Release, CFileMapping::Close

// The group names "00" and "01" (g_gfxGroupNames runs from "61" down to "00").
constexpr std::size_t kName00 = kGfxGroupNameCount - 1;
constexpr std::size_t kName01 = kGfxGroupNameCount - 2;

// WCValOrderedVector<string> (0x1c bytes; only the length is read).
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
using Member1Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a);
using ThrowAtFn = void(__fastcall*)(const char* text, const char* file, std::uint32_t line);
using ThrowCodeFn = void(__fastcall*)(const char* expression, const char* file, std::uint32_t line,
                                      std::uint32_t code, const String* message);
using CFileNotFoundConstructFn = void*(__fastcall*)(void* self, void* edx, const char* name,
                                                    std::uint32_t mostDerived);
using CxxThrowExceptionFn = void(__stdcall*)(void* object, std::uintptr_t throwInfo);
using DictConstructFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t probability,
                                           std::uint32_t maxLevels, std::uint32_t duplicates);
using VectorConstructFn = void*(__fastcall*)(void* vector, void* edx, std::uint32_t capacity, std::uint32_t grow);
using InsertAtFn = std::uint32_t(__fastcall*)(void* vector, void* edx, std::uint32_t index, const String* value);
using EqualsTextFn = bool(__fastcall*)(const String* self, const char* text);
using CompareTextFn = std::int32_t(__fastcall*)(const String* self, void* edx, const char* text);
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
using ConnectFn = std::uint32_t(__fastcall*)(void* self, void* edx, const void* device);
using CheckFn = std::int32_t(__fastcall*)(void* self, void* edx, std::int32_t hr, const char* where);
using GetVolumeFn = std::int32_t(__fastcall*)(void* self, void* edx, std::int32_t* volume);

template <typename Fn>
Fn CallAt(std::uintptr_t address) {
    return reinterpret_cast<Fn>(address);
}

template <typename Fn>
Fn ComMethod(void* object, std::uint32_t offset) {
    return *reinterpret_cast<Fn*>(*static_cast<std::uint8_t**>(object) + offset);
}

std::uint32_t CallSlot(void* object, std::uint32_t slot) {
    return ComMethod<MemberFn>(object, slot)(object, nullptr);
}

std::uint32_t CallSlot1(void* object, std::uint32_t slot, std::uint32_t argument) {
    return ComMethod<Member1Fn>(object, slot)(object, nullptr, argument);
}

const char* CStr(const String* s) {
    return msvc6::detail::CStr(s);
}

std::uint32_t Npos() {
    return msvc6::detail::Npos();
}

CSoundData* AsSound(void* self) {
    return static_cast<CSoundData*>(self);
}

// The primary buffer's IDirectSoundBuffer.
void* PrimaryInterface(const CSoundData* sound) {
    return static_cast<framework::CDirectSoundBuffer*>(sound->primary)->buffer;
}

// ---------------------------------------------------------------------------
// The skip-list dictionaries
// ---------------------------------------------------------------------------

void ConstructDict(SkipListDict* dict, std::uint32_t vtable) {
    CallAt<DictConstructFn>(kDictConstruct)(dict, nullptr, kDictProbability, kDictMaxLevels, 0);
    dict->vtable = vtable;
}

// The inlined destructor: the base vtable, the (empty) "not empty" hook,
// clear, WCExcept's vtable.
void DestroyDict(SkipListDict* dict, std::uint32_t baseVtable) {
    dict->vtable = baseVtable;
    if (dict->count != 0) {
        CallAt<MemberFn>(kNoOp)(dict, nullptr);
    }
    CallAt<MemberFn>(kDictClear)(dict, nullptr);
    dict->vtable = vtables::kWCExcept;
}

// The value of a skip-list node (nodes point at their forward array; the
// key and value sit before it).
void* NodeValue(std::uint32_t node) {
    return *reinterpret_cast<void**>(node - 4);
}

// The next node on level 0 (0 at the end); `node` 0 starts at the header.
std::uint32_t NextNode(const SkipListDict* dict, std::uint32_t node) {
    const std::uint32_t from = node != 0 ? node : dict->header_ptr;
    return *reinterpret_cast<const std::uint32_t*>(from);
}

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

// VERIFY failed in SoundData.cpp: ThrowWin32Error with an empty message.
void ThrowWin32Error(const char* expression, std::uint32_t line) {
    String message{};
    const std::uint32_t error = g_importGetLastError.get()();
    CallAt<ThrowCodeFn>(kThrowWin32Error)(expression, texts::kSoundDataFile, line, error, &message);
}

// The original's `catch (DirectX::CDirectSoundException* e)` blocks that
// handle some codes and rethrow the others: the filter takes only the
// handled ones (a rethrow is the same as not catching). A null pointer is
// taken too: the handler then reads its code like the original does (and
// faults like it).
struct DSCatch {
    const std::int32_t* codes;
    std::size_t count;
    void* caught;
};

std::int32_t ExceptionCode(void* exception) {
    return static_cast<std::int32_t>(CallAt<MemberFn>(kGetMember18)(exception, nullptr));
}

int CatchDSException(const EXCEPTION_POINTERS* info, DSCatch* clause) {
    const msvc6::CatchMatch match = msvc6::MatchPointerCatch(
        info->ExceptionRecord, type_descriptor::kCDirectSoundExceptionPointer, ".PAVCDirectSoundException@DirectX@@");
    if (!match.matched) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    bool handled = match.object == nullptr || clause->codes == nullptr;
    if (!handled) {
        const std::int32_t code = ExceptionCode(match.object);
        for (std::size_t i = 0; i < clause->count; ++i) {
            handled |= code == clause->codes[i];
        }
    }
    if (!handled) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    clause->caught = match.object;
    return EXCEPTION_EXECUTE_HANDLER;
}

// The handler's start: the code is read (a null pointer faults here, as in
// the original), then the exception deleted.
void DeleteCaught(void* exception) {
    if (exception == nullptr) {
        ExceptionCode(exception);
    }
    msvc6::DeleteCaughtException(exception);
}

// ---------------------------------------------------------------------------
// Open's DirectSound part (three try blocks; no C++ objects in these frames)
// ---------------------------------------------------------------------------

// try { Connect(null) } catch: the sound card is missing or busy.
bool TryConnect(CSoundData* sound) {
    static const std::int32_t kHandled[] = {kDSErrAllocated, kDSErrNoDriver, kEFail};
    DSCatch clause{kHandled, 3, nullptr};
    __try {
        CallAt<ConnectFn>(kConnect)(sound, nullptr, nullptr);
        return true;
    } __except (CatchDSException(GetExceptionInformation(), &clause)) {
    }
    DeleteCaught(clause.caught);
    GameDebugLog(texts::kDSCannotCreate);
    return false;
}

void SetCooperativeLevel(CSoundData* sound, std::uint32_t window, std::uint32_t level) {
    void* const ds = sound->base.sound;
    using SetLevelFn = std::int32_t(__stdcall*)(void* ds, std::uint32_t window, std::uint32_t level);
    const std::int32_t hr = ComMethod<SetLevelFn>(ds, kSoundSetCooperativeLevel)(ds, window, level);
    CallAt<CheckFn>(kCheckDSResult)(&sound->base.sound, nullptr, hr, texts::kDSSetCooperativeLevel);
}

// try { priority level } catch DSERR_ALLOCATED: the normal level; returns
// whether the primary buffer's format may be set.
bool TryPriorityLevel(CSoundData* sound, std::uint32_t window) {
    static const std::int32_t kHandled[] = {kDSErrAllocated};
    DSCatch clause{kHandled, 1, nullptr};
    __try {
        SetCooperativeLevel(sound, window, kCoopPriority);
        return true;
    } __except (CatchDSException(GetExceptionInformation(), &clause)) {
    }
    DeleteCaught(clause.caught);
    GameDebugLog(texts::kDSNoPriorityLevel);
    SetCooperativeLevel(sound, window, kCoopNormal);
    return false;
}

void SetPrimaryFormat(CSoundData* sound) {
    const void* format = nullptr;
    switch (sound->format) {
        case 1: format = g_primaryFormats[0]; break;
        case 2: format = g_primaryFormats[1]; break;
        case 4: format = g_primaryFormats[2]; break;
        default:
            CallAt<ThrowAtFn>(kThrowAssertmentFailed)(texts::kInvalidSoundFormat, texts::kSoundDataFile,
                                                      kLineInvalidFormat);
            return;
    }
    void* const buffer = PrimaryInterface(sound);
    using SetFormatFn = std::int32_t(__stdcall*)(void* buffer, const void* format);
    const std::int32_t hr = ComMethod<SetFormatFn>(buffer, kBufferSetFormat)(buffer, format);
    CallAt<CheckFn>(kBufferCheckResult)(sound->primary, nullptr, hr, texts::kDSSetFormat);
}

// try { SetFormat } catch DSERR_BADFORMAT: the card's own format stays.
void TrySetPrimaryFormat(CSoundData* sound) {
    static const std::int32_t kHandled[] = {kDSErrBadFormat};
    DSCatch clause{kHandled, 1, nullptr};
    __try {
        SetPrimaryFormat(sound);
        return;
    } __except (CatchDSException(GetExceptionInformation(), &clause)) {
    }
    DeleteCaught(clause.caught);
    GameDebugLog(texts::kDSFormatNotCapable);
}

void OpenDirectSound(CSoundData* sound, std::uint32_t window) {
    if (!TryConnect(sound)) {
        return;
    }
    const bool setFormat = TryPriorityLevel(sound, window);
    void* const block = msvc6::GameOperatorNew(kPrimarySize);
    void* primary = nullptr;
    if (block != nullptr) {
        primary = CallAt<void*(__fastcall*)(void*, void*)>(kPrimaryConstruct)(block, nullptr);
    }
    sound->primary = primary;
    if (primary == nullptr) {
        CallAt<ThrowAtFn>(kThrowOutOfMemory)(texts::kLpPrimary, texts::kSoundDataFile, kLineNewPrimary);
    }
    CallAt<MemberFn>(kPrimaryCreate)(sound->primary, nullptr);
    if (setFormat) {
        TrySetPrimaryFormat(sound);
    }
    void* const buffer = PrimaryInterface(sound);
    using PlayFn = std::int32_t(__stdcall*)(void* buffer, std::uint32_t reserved, std::uint32_t priority,
                                           std::uint32_t flags);
    const std::int32_t hr = ComMethod<PlayFn>(buffer, kBufferPlay)(buffer, 0, 0, kPlayLooping);
    CallAt<CheckFn>(kBufferCheckResult)(sound->primary, nullptr, hr, texts::kDSPlay);
    sound->enabled = 1;
}

// ---------------------------------------------------------------------------
// Open's strings
// ---------------------------------------------------------------------------

// A string built as the original's inlined `string s(text)`: default, then
// assigned from string(string(text)).
void InitText(String* s, const char* text) {
    std::uint8_t allocator = 0;
    msvc6::String_Default(s, nullptr, &allocator);
    String text1;
    String text2;
    msvc6::String_FromText(&text1, nullptr, text, &allocator);
    msvc6::String_FromSubstr(&text2, nullptr, &text1, 0, Npos(), &allocator);
    msvc6::String_AssignSubstr(s, nullptr, &text2, 0, Npos());
    msvc6::String_Tidy(&text2, nullptr, 1);
    msvc6::String_Tidy(&text1, nullptr, 1);
}

// file = "siedler3_" + name + ".dat", built like the original.
void SndFileName(String* file, const String* name) {
    String prefix;
    String withName;
    String full;
    InitText(&prefix, texts::kSiedler3Prefix);
    msvc6::String_ConcatString(&withName, &prefix, name);
    msvc6::String_ConcatChars(&full, &withName, texts::kDatExtension);
    msvc6::String_AssignSubstr(file, nullptr, &full, 0, Npos());
    msvc6::String_Destroy(&full, nullptr);
    msvc6::String_Destroy(&withName, nullptr);
    msvc6::String_Destroy(&prefix, nullptr);
}

void AppendFolder(StringVector* folders, const String* folder) {
    CallAt<InsertAtFn>(kStringVectorInsertAt)(folders, nullptr, folders->length, folder);
}

void AppendTextFolder(StringVector* folders, const char* text) {
    String folder;
    InitText(&folder, text);
    AppendFolder(folders, &folder);
    msvc6::String_Destroy(&folder, nullptr);
}

void AppendJoinedFolder(StringVector* folders, const String* basePath, const char* text) {
    String folder;
    msvc6::String_ConcatChars(&folder, basePath, text);
    AppendFolder(folders, &folder);
    msvc6::String_Destroy(&folder, nullptr);
}

}  // namespace

void* __fastcall CSoundData_Construct(void* self, void* edx);
void* __fastcall CSoundData_DeletingDestroy(void* self, void* edx, std::uint32_t flags);
std::uint32_t __fastcall CSoundData_Destroy(void* self, void* edx);
void __fastcall CSoundData_Close(void* self, void* edx);
void __fastcall CSoundData_ReleaseBuffers(void* self, void* edx);
void __fastcall CSoundData_Restore(void* self, void* edx);
void __fastcall CSoundData_SetVolume(void* self, void* edx, std::int32_t volume);
std::int32_t __fastcall CSoundData_GetVolume(void* self, void* edx);
void __fastcall CSoundData_DetachFromMedium(void* self, void* edx, std::uint32_t otherwise);
String* __fastcall CSoundData_FindDirectoryContainingFile(void* self, void* edx, String* result, void* folders,
                                                         const String* file);
void __fastcall CSoundData_Open(void* self, void* edx, std::uint32_t window, const String* basePath,
                                std::uint32_t detachMode, std::uint32_t format);

using SndConstructFn = decltype(&CSoundData_Construct);
using SndDeletingFn = decltype(&CSoundData_DeletingDestroy);
using SndDestroyFn = decltype(&CSoundData_Destroy);
using SndVoidFn = decltype(&CSoundData_Close);
using SndSetVolumeFn = decltype(&CSoundData_SetVolume);
using SndGetVolumeFn = decltype(&CSoundData_GetVolume);
using SndDetachFn = decltype(&CSoundData_DetachFromMedium);
using SndFindDirectoryFn = decltype(&CSoundData_FindDirectoryContainingFile);
using SndOpenFn = decltype(&CSoundData_Open);

S3RE_HOOK(0x005fdb60, CSoundData_Construct, SndConstructFn, Original_CSoundData_Construct, CSoundData_Construct);

// Purpose: constructor of the sound data (the DirectSound object, one
// mapping object per SND file, nothing loaded yet).
void* __fastcall CSoundData_Construct(void* self, void* /*edx*/) {
    CSoundData* const sound = AsSound(self);
    CallAt<MemberFn>(kSoundConstruct)(self, nullptr);
    sound->enabled = 0;
    sound->folder.allocator = 0;  // the original's is an uninitialized local (known difference)
    msvc6::detail::Clear(&sound->folder);
    ConstructDict(&sound->effects, vtables::kSoundEffectDict);
    ConstructDict(&sound->lastUse, vtables::kSoundUseDict);
    sound->primary = nullptr;
    sound->createdTime = g_importTimeGetTime.get()();
    sound->unk148 = kNoValue;
    sound->base.vtable = vtables::kCSoundData;
    for (std::size_t file = 0; file < sound::kFiles; ++file) {
        sound->fileSizes[file] = 0;
        void* const block = msvc6::GameOperatorNew(kMappingSize);
        void* mapping = nullptr;
        if (block != nullptr) {
            mapping = CallAt<void*(__fastcall*)(void*, void*)>(kMappingConstruct)(block, nullptr);
        }
        sound->mappings[file] = reinterpret_cast<std::uint32_t>(mapping);
        if (mapping == nullptr) {
            CallAt<ThrowAtFn>(kThrowOutOfMemory)(texts::kLpMapping, texts::kSoundDataFile, kLineNewMapping);
        }
        sound->views[file] = 0;
    }
    return self;
}

S3RE_HOOK(0x005fdc90, CSoundData_Destroy, SndDestroyFn, Original_CSoundData_Destroy, CSoundData_Destroy);

// Purpose: destructor of the sound data: closes everything and deletes the
// mapping objects.
std::uint32_t __fastcall CSoundData_Destroy(void* self, void* /*edx*/) {
    CSoundData* const sound = AsSound(self);
    sound->base.vtable = vtables::kCSoundData;
    if (sound->views[0] != 0) {
        CallAt<MemberFn>(kSoundClose)(self, nullptr);
    }
    for (std::size_t file = 0; file < sound::kFiles; ++file) {
        void* const mapping = reinterpret_cast<void*>(sound->mappings[file]);
        if (mapping != nullptr) {
            CallSlot1(mapping, kSlotDelete, 1);
        }
    }
    DestroyDict(&sound->lastUse, vtables::kSoundUseSkipListBase);
    DestroyDict(&sound->effects, vtables::kSoundEffectSkipListBase);
    msvc6::String_Destroy(&sound->folder, nullptr);
    return CallAt<MemberFn>(kSoundDestroy)(self, nullptr);
}

S3RE_HOOK(0x005fdc70, CSoundData_DeletingDestroy, SndDeletingFn, Original_CSoundData_DeletingDestroy,
          CSoundData_DeletingDestroy);

// Purpose: the compiler-generated "delete this object" entry of the vtable.
void* __fastcall CSoundData_DeletingDestroy(void* self, void* /*edx*/, std::uint32_t flags) {
    CSoundData_Destroy(self, nullptr);
    if ((flags & 1) != 0) {
        msvc6::GameOperatorDelete(self);
    }
    return self;
}

S3RE_HOOK(0x005ff1c0, CSoundData_Close, SndVoidFn, Original_CSoundData_Close, CSoundData_Close);

// Purpose: switches the sound off: unmaps the SND files and releases the
// DirectSound objects.
void __fastcall CSoundData_Close(void* self, void* /*edx*/) {
    CSoundData* const sound = AsSound(self);
    sound->enabled = 0;
    if (sound->views[0] != 0) {
        for (std::size_t file = 0; file < sound::kFiles; ++file) {
            if (sound->views[file] != 0) {
                void* const mapping = reinterpret_cast<void*>(sound->mappings[file]);
                CallAt<Member1Fn>(kMappingUnmapView)(mapping, nullptr, sound->views[file]);
                CallSlot(mapping, kSlotRelease);
            }
            sound->views[file] = 0;
            sound->tables[file] = 0;
        }
    }
    if (sound->base.sound != nullptr) {
        CallAt<MemberFn>(kDisconnect)(self, nullptr);
    }
    if (sound->primary != nullptr) {
        CallSlot1(sound->primary, kSlotDelete, 1);
    }
    sound->primary = nullptr;
}

S3RE_HOOK(0x005fde20, CSoundData_ReleaseBuffers, SndVoidFn, Original_CSoundData_ReleaseBuffers,
          CSoundData_ReleaseBuffers);

// Purpose: deletes all sound effects and releases the primary buffer
// (before the DirectSound object goes away).
void __fastcall CSoundData_ReleaseBuffers(void* self, void* /*edx*/) {
    CSoundData* const sound = AsSound(self);
    for (std::uint32_t node = NextNode(&sound->effects, 0); node != 0; node = NextNode(&sound->effects, node)) {
        void* const effect = NodeValue(node);
        if (effect != nullptr) {
            CallAt<MemberFn>(kSoundEffectDestroy)(effect, nullptr);
            msvc6::GameOperatorDelete(effect);
        }
    }
    CallAt<MemberFn>(kDictClear)(&sound->effects, nullptr);
    if (sound->primary != nullptr) {
        CallSlot(sound->primary, kSlotRelease);
    }
}

namespace {

void RestoreEffects(CSoundData* sound) {
    for (std::uint32_t node = NextNode(&sound->effects, 0); node != 0; node = NextNode(&sound->effects, node)) {
        CallAt<MemberFn>(kSoundEffectRestore)(NodeValue(node), nullptr);
    }
}

}  // namespace

S3RE_HOOK(0x005fdef0, CSoundData_Restore, SndVoidFn, Original_CSoundData_Restore, CSoundData_Restore);

// Purpose: after DirectSound lost its buffers (another program took the
// sound card), reloads every sound effect; if that fails, the sound stays
// off.
void __fastcall CSoundData_Restore(void* self, void* /*edx*/) {
    CSoundData* const sound = AsSound(self);
    DSCatch clause{nullptr, 0, nullptr};
    __try {
        RestoreEffects(sound);
        return;
    } __except (CatchDSException(GetExceptionInformation(), &clause)) {
    }
    msvc6::DeleteCaughtException(clause.caught);
    GameDebugLog(texts::kDSCannotRestore);
    sound->enabled = 0;
}

S3RE_HOOK(0x005ff9f0, CSoundData_SetVolume, SndSetVolumeFn, Original_CSoundData_SetVolume, CSoundData_SetVolume);

// Purpose: sets the master volume of the sound effects (the primary
// buffer's volume).
void __fastcall CSoundData_SetVolume(void* self, void* /*edx*/, std::int32_t volume) {
    CSoundData* const sound = AsSound(self);
    if (sound->enabled == 0 || sound->primary == nullptr) {
        return;
    }
    void* const buffer = PrimaryInterface(sound);
    using SetVolumeFn = std::int32_t(__stdcall*)(void* buffer, std::int32_t volume);
    const std::int32_t hr = ComMethod<SetVolumeFn>(buffer, kBufferSetVolume)(buffer, volume);
    CallAt<CheckFn>(kBufferCheckResult)(sound->primary, nullptr, hr, texts::kDSSetVolume);
}

S3RE_HOOK(0x005ffa30, CSoundData_GetVolume, SndGetVolumeFn, Original_CSoundData_GetVolume, CSoundData_GetVolume);

// Purpose: the master volume of the sound effects (0 when the sound is off).
std::int32_t __fastcall CSoundData_GetVolume(void* self, void* /*edx*/) {
    CSoundData* const sound = AsSound(self);
    std::int32_t volume = 0;
    if (sound->enabled != 0 && sound->primary != nullptr) {
        CallAt<GetVolumeFn>(kBufferGetVolume)(sound->primary, nullptr, &volume);
    }
    return volume;
}

S3RE_HOOK(0x005ff230, CSoundData_DetachFromMedium, SndDetachFn, Original_CSoundData_DetachFromMedium,
          CSoundData_DetachFromMedium);

// Purpose: when the sounds come from a CD (or removable drive), copies the
// mapped SND files into memory so the CD can be taken out.
void __fastcall CSoundData_DetachFromMedium(void* self, void* /*edx*/, std::uint32_t otherwise) {
    CSoundData* const sound = AsSound(self);
    if (sound->enabled == 0) {
        return;
    }
    const std::uint32_t removable =
        CallAt<RemovableFn>(kIsPathOnRemovableDrive)(self, nullptr, &sound->folder, otherwise);
    if ((removable & 0xff) == 0) {
        return;
    }
    GameDebugLog(texts::kSndDetaching);
    for (std::size_t file = 0; file < sound::kFiles; ++file) {
        if (sound->views[file] == 0) {
            continue;
        }
        const std::int32_t result = CallAt<DetachMemoryFn>(kDetachMemory)(sound->views[file], sound->fileSizes[file]);
        if (result < 0) {
            String message{};
            message.allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uint32_t>(&sound->views[file]));
            CallAt<ThrowCodeFn>(kThrowHResult)(texts::kErrorCheck, texts::kSoundDataFile, kLineDetach,
                                               static_cast<std::uint32_t>(result), &message);
        }
    }
}

S3RE_HOOK(0x005fdfc0, CSoundData_FindDirectoryContainingFile, SndFindDirectoryFn,
          Original_CSoundData_FindDirectoryContainingFile, CSoundData_FindDirectoryContainingFile);

// Purpose: finds the folder (of a list of candidates) that holds the SND
// files: the game can run from the install folder or the CD.
String* __fastcall CSoundData_FindDirectoryContainingFile(void* /*self*/, void* /*edx*/, String* result,
                                                         void* folders, const String* file) {
    StringVector* const list = static_cast<StringVector*>(folders);
    String found{};
    char saved[kPathBuffer];
    if (g_importGetCurrentDirectoryA.get()(kPathBuffer, saved) == 0) {
        ThrowWin32Error(texts::kDwResultNotZero, kLineSaveDirectory);
    }
    std::uint32_t index = 0;
    do {
        if (g_importSetCurrentDirectoryA.get()(saved) == 0) {
            ThrowWin32Error(texts::kBResult, kLineRestoreDirectory);
        }
        const std::uint32_t at = CallAt<Member1Fn>(kStringVectorIndex)(list, nullptr, index);
        if (g_importSetCurrentDirectoryA.get()(CStr(&list->data[at])) != 0) {
            WIN32_FIND_DATAA data;
            void* const search = g_importFindFirstFileA.get()(CStr(file), &data);
            if (search != INVALID_HANDLE_VALUE) {
                char current[kPathBuffer];
                if (g_importGetCurrentDirectoryA.get()(kPathBuffer, current) == 0) {
                    ThrowWin32Error(texts::kDwResultNotZero, kLineFoundDirectory);
                }
                std::uint8_t textAllocator = 0;
                String text;
                msvc6::String_FromText(&text, nullptr, current, &textAllocator);
                msvc6::String_AssignSubstr(&found, nullptr, &text, 0, Npos());
                msvc6::String_Tidy(&text, nullptr, 1);
                if (g_importFindClose.get()(search) == 0) {
                    ThrowWin32Error(texts::kBResult, kLineFindClose);
                }
            }
        }
        ++index;
    } while (index < list->length && CallAt<CompareTextFn>(kStringCompareText)(&found, nullptr, texts::kEmpty) == 0);
    if (g_importSetCurrentDirectoryA.get()(saved) == 0) {
        ThrowWin32Error(texts::kBResult, kLineFinalRestore);
    }
    if (CallAt<EqualsTextFn>(kStringEqualsText)(&found, texts::kEmpty)) {
        void* const object = msvc6::GameOperatorNew(kFileNotFoundSize);
        void* thrown = nullptr;
        if (object != nullptr) {
            thrown = CallAt<CFileNotFoundConstructFn>(kCFileNotFoundConstruct)(object, nullptr, CStr(file), 1);
        }
        CallAt<CxxThrowExceptionFn>(kCxxThrowException)(&thrown, throw_info::kCFileNotFoundPointer);
    }
    String withSlash;
    msvc6::String_ConcatChar(&withSlash, &found, '\\');
    msvc6::String_FromSubstrDefault(result, nullptr, &withSlash, 0, Npos());
    msvc6::String_Destroy(&withSlash, nullptr);
    msvc6::String_Destroy(&found, nullptr);
    return result;
}

S3RE_HOOK(0x005fe760, CSoundData_Open, SndOpenFn, Original_CSoundData_Open, CSoundData_Open);

// Purpose: starts the sound: creates the DirectSound object and the
// primary buffer (looping, so the card keeps running) and maps the SND
// files into memory.
void __fastcall CSoundData_Open(void* self, void* /*edx*/, std::uint32_t window, const String* basePath,
                                std::uint32_t detachMode, std::uint32_t format) {
    CSoundData* const sound = AsSound(self);
    std::uint8_t allocator = 0;
    String file;
    msvc6::String_Default(&file, nullptr, &allocator);
    String names[sound::kFiles];
    InitText(&names[0], g_gfxGroupNames[kName00]);
    InitText(&names[1], g_gfxGroupNames[kName01]);
    StringVector folders;
    CallAt<VectorConstructFn>(kStringVectorConstruct)(&folders, nullptr, kFolderVectorCapacity, kFolderVectorGrowth);
    folders.vtable = vtables::kStringVector;

    sound->format = format;
    OpenDirectSound(sound, window);

    // The file to search for and the folders to look in.
    SndFileName(&file, &names[0]);
    AppendTextFolder(&folders, texts::kFullStop);
    AppendJoinedFolder(&folders, basePath, texts::kSndFolder);
    AppendTextFolder(&folders, texts::kDotSndFolder);
    AppendFolder(&folders, basePath);
    AppendJoinedFolder(&folders, basePath, texts::kParentFolder);
    {
        String found;
        CallAt<FindDirectoryFn>(kFindDirectory)(self, nullptr, &found, &folders, &file);
        msvc6::String_AssignSubstr(&sound->folder, nullptr, &found, 0, Npos());
        msvc6::String_Destroy(&found, nullptr);
    }

    bool detach = false;
    if (detachMode == kDetachAlways) {
        detach = true;
    } else if (detachMode == kDetachIfRemovable) {
        detach = (CallAt<RemovableFn>(kIsPathOnRemovableDrive)(self, nullptr, &sound->folder, 0) & 0xff) != 0;
    }
    if (detach) {
        GameDebugLog(texts::kSndPreparingDetach);
    }

    // Map both files.
    for (std::size_t index = 0; index < sound::kFiles; ++index) {
        if (CallAt<CompareTextFn>(kStringCompareText)(&names[index], nullptr, texts::kEmpty) == 0) {
            continue;  // (never: both names are set)
        }
        void* const block = msvc6::GameOperatorNew(kCFileSize);
        void* fileObject = nullptr;
        if (block != nullptr) {
            fileObject = CallAt<void*(__fastcall*)(void*, void*)>(kCFileConstruct)(block, nullptr);
        }
        if (fileObject == nullptr) {
            CallAt<ThrowAtFn>(kThrowOutOfMemory)(texts::kLpFile, texts::kSoundDataFile, kLineNewFile);
        }
        SndFileName(&file, &names[index]);
        String path;
        msvc6::String_ConcatString(&path, &sound->folder, &file);
        CallAt<FileOpenFn>(kCFileOpen)(fileObject, nullptr, CStr(&path), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                       OPEN_EXISTING, kFileFlags, nullptr);
        msvc6::String_Destroy(&path, nullptr);
        sound->fileSizes[index] = CallAt<FileGetSizeFn>(kCFileGetSize)(fileObject, nullptr, nullptr);
        void* const mapping = reinterpret_cast<void*>(sound->mappings[index]);
        CallAt<MappingCreateFn>(kMappingCreate)(mapping, nullptr, fileObject, nullptr,
                                                detach ? PAGE_WRITECOPY : PAGE_READONLY, 0, 0, nullptr);
        const std::uint32_t view = CallAt<MappingMapViewFn>(kMappingMapView)(
            mapping, nullptr, detach ? FILE_MAP_COPY : FILE_MAP_READ, 0, 0, 0);
        sound->views[index] = view;
        const auto* const header = reinterpret_cast<const SndFileHeader*>(view);
        if (header->unk14 != kNoValue && sound->unk148 == kNoValue) {
            sound->unk148 = header->unk14;
        }
        sound->tables[index] = header->table + sound->views[index];
    }

    // The locals' destructors, in the original's order.
    folders.vtable = vtables::kStringVector;
    CallAt<MemberFn>(kStringVectorDestroy)(&folders, nullptr);
    msvc6::String_Destroy(&names[1], nullptr);
    msvc6::String_Destroy(&names[0], nullptr);
    msvc6::String_Destroy(&file, nullptr);
}

}  // namespace s3re::game
