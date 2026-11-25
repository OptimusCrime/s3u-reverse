// The sound sources of the DirectSound effects and the DirectSound
// exception (DirectX\Dsound.cpp; RTTI names): DirectX::CSoundFile,
// DirectX::CMemorySoundFile (the samples of the mapped SND files),
// DirectX::CDirectSoundException and DirectX::CAppNotActiveException's
// constructor and destructor. Layouts: directx_sound_layout.h.
//
// DirectX::CSoundFile::CSoundFile (0x005c2270): (const string& name) ret 4:
//   +4 = a copy of name (MSVC 6.0's copy constructor inlined: the allocator
//   byte is the low byte of the argument, as its empty allocator shares that
//   stack slot; the buffer is shared when possible), vtable 0x65cf08.
//   EAX = this.
// ~CSoundFile (0x005c23d0): vtable 0x65cf08, the name destroyed (split,
//   erase, free; inlined). Callers ignore EAX.
// The scalar deleting destructors 0x005c23b0 (CSoundFile) and 0x005c2780
//   (CMemorySoundFile, through the thunk 0x5c27a0 to ~CSoundFile) are in
//   msvc6/deleting_destructors.cpp.
// DirectX::CMemorySoundFile::CMemorySoundFile (0x005c24e0): (const
//   WAVEFORMATEX* format, const void* data, bytes, mode) ret 10h: the name
//   string("memory mapped file") is built through two temporaries (all
//   three get the low byte of `format` as allocator byte), CSoundFile(name),
//   the name destroyed; then the format (0x12 bytes) is copied, +0x28 data,
//   +0x2c bytes, +0x30 position 0, +0x34 mode, vtable 0x65cf20. Mode 4
//   (16 -> 8 bit and half the rate) applies only to 16-bit samples of at
//   least 22,050 Hz: bits 8, block align / 2, rate / 2, bytes per second / 4,
//   bytes / 4; otherwise it becomes mode 2. Mode 2 (16 -> 8 bit) only to
//   16-bit samples: bits 8, block align / 2, bytes per second / 2, bytes /
//   2; otherwise mode 1. EAX = this.
// Open / Close (0x005c27b0, vtable slots 1 and 2): position = 0 (EAX left
//   alone).
// GetFormat (0x005c27c0, slot 3): (WAVEFORMATEX* format, DWORD* bytes)
//   ret 8: copies the format and the byte count (EAX: the high word of the
//   format's address and the format's last word; unused).
// Read (0x005c2800, slot 4): (count, void* dest) ret 8: mode 1 copies count
//   bytes from data + position, position += count; modes 2 and 4 write
//   count bytes, each the high byte of a 16-bit sample + 0x80 (signed 16 to
//   unsigned 8 bit), position += 2 or 4 (mode 4 skips every other sample);
//   other modes do nothing. EAX = this.
//
// DirectX::CDirectSoundException::CDirectSoundException (0x005c15d0): (hr,
//   where, mostDerived) ret 0Ch: if mostDerived, +4 = 0x65ceac; the message
//   string(string("")) (temporaries with the low byte of mostDerived as
//   allocator byte); CRunTimeError(message, 0); the message destroyed; +0x18
//   = hr, +0x1c = where, vtable 0x65cea4; then Describe() into a temporary
//   that is destroyed again (a removed log call). EAX = this.
// CDirectSoundException::Describe (0x005c1930, vtable slot 1): (string*
//   result) ret 4: result = "DirectSound Error " + DSErrorText(hr) + "
//   ocurred at " + where, built like CAssertmentFailed::Describe. EAX =
//   result.
// ~CDirectSoundException (0x005c1830), ~CAppNotActiveException
//   (0x005b88e0) and Win32::CMCIException's (0x00614150, the CD music's
//   exception): the message at +8 destroyed (split, erase, free; inlined),
//   then ~CObject (0x610330). Callers ignore EAX.
// DirectX::CAppNotActiveException::CAppNotActiveException (0x005b8890):
//   (const string& msg, mostDerived) ret 8: if mostDerived, +4 = 0x65cbb8;
//   CRunTimeError(msg, 0); vtable 0x65cbb0. EAX = this.
//
// Known differences (allocator bytes, see docs/reimplementation_notes.md):
// CMemorySoundFile's name (+4) gets the low byte of the address of the
// constructor's own temporary (CSoundFile's copy takes its argument's
// slot), and Describe's result the byte of an uninitialized local of the
// original; both are never read. The other temporaries' allocator bytes
// (uninitialized in the original, freed before returning) are 0 in ours.

#include "game/framework/directx_sound.h"
#include "game/framework/directx_sound_layout.h"

#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

// win32_exceptions.cpp
void* __fastcall Win32_CRunTimeError_Construct(void* self, void* edx, const msvc6::String* msg,
                                               std::uint32_t mostDerived);

namespace {

using framework::CDirectSoundException;
using framework::CException;
using framework::CMemorySoundFile;
using framework::CSoundFile;
using framework::WaveFormat;
using msvc6::String;

constexpr std::uintptr_t kObjectDestroy = 0x00610330;     // Win32::CObject::~CObject
constexpr std::uintptr_t kSoundFileConstruct = 0x005c2270;
constexpr std::uintptr_t kExceptionDescribe = 0x005c1930;  // CDirectSoundException::Describe
constexpr std::uintptr_t kDSErrorText = 0x005c1c10;

// CMemorySoundFile's sample conversions (+0x34).
constexpr std::uint32_t kModeCopy = 1;
constexpr std::uint32_t kModeTo8Bit = 2;
constexpr std::uint32_t kModeTo8BitHalfRate = 4;
constexpr std::uint32_t kHalfRateMinimum = 22050;  // Hz
constexpr std::uint16_t kBits16 = 16;
constexpr std::uint16_t kBits8 = 8;
constexpr std::uint8_t kSignFlip = 0x80;  // signed to unsigned 8-bit samples

using ThisFn = std::uint32_t(__fastcall*)(void* self, void* edx);

// `target` = string(string(text)) as the original inlines it into an empty
// string: a temporary from the text, a copy of it, assign, free both.
void AssignThroughTemporaries(String* target, const char* text, std::uint8_t allocator) {
    String fromText;
    fromText.allocator = allocator;
    msvc6::String_Tidy(&fromText, nullptr, 0);
    msvc6::String_AssignChars(&fromText, nullptr, text, msvc6::detail::Length(text));
    String copy;
    copy.allocator = allocator;
    msvc6::String_Tidy(&copy, nullptr, 0);
    msvc6::String_AssignSubstr(&copy, nullptr, &fromText, 0, 0xFFFFFFFFu);
    msvc6::String_AssignSubstr(target, nullptr, &copy, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(&copy, nullptr, 1);
    msvc6::String_Tidy(&fromText, nullptr, 1);
}

// The same with the out-of-line constructors (0x407a50, 0x4078a0).
void AssignThroughConstructors(String* target, const char* text) {
    const std::uint8_t allocator = 0;  // the original's are uninitialized (freed temporaries)
    String fromText;
    String copy;
    msvc6::String_FromText(&fromText, nullptr, text, &allocator);
    msvc6::String_FromSubstr(&copy, nullptr, &fromText, 0, 0xFFFFFFFFu, &allocator);
    msvc6::String_AssignSubstr(target, nullptr, &copy, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(&copy, nullptr, 1);
    msvc6::String_Tidy(&fromText, nullptr, 1);
}

// The inlined destruction of a string: split, erase(0, npos), free.
void EraseAndFree(String* s) {
    msvc6::detail::Erase(s, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(s, nullptr, 1);
}

// The same with the out-of-line erase() (0x407b10).
void CallEraseAndFree(String* s) {
    msvc6::String_Erase(s, nullptr, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(s, nullptr, 1);
}

CMemorySoundFile* AsMemoryFile(void* self) {
    return static_cast<CMemorySoundFile*>(self);
}

// The mode 4 and mode 2 conversions of the constructor.
void ApplyConversion(CMemorySoundFile* file) {
    WaveFormat& format = file->format;
    if (file->mode == kModeTo8BitHalfRate) {
        if (format.samplesPerSec >= kHalfRateMinimum && format.bitsPerSample == kBits16) {
            format.bitsPerSample = kBits8;
            format.blockAlign >>= 1;
            format.samplesPerSec >>= 1;
            format.avgBytesPerSec >>= 2;
            file->bytes >>= 2;
        } else {
            file->mode = kModeTo8Bit;
        }
    }
    if (file->mode == kModeTo8Bit) {
        if (format.bitsPerSample == kBits16) {
            format.bitsPerSample = kBits8;
            format.blockAlign >>= 1;
            format.avgBytesPerSec >>= 1;
            file->bytes >>= 1;
        } else {
            file->mode = kModeCopy;
        }
    }
}

}  // namespace

using SoundFileConstructFn = void*(__fastcall*)(void* self, void* edx, const String* name);
using MemorySoundFileConstructFn = void*(__fastcall*)(void* self, void* edx, const void* format, const void* data,
                                                      std::uint32_t bytes, std::uint32_t mode);
using SoundFileDestroyFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using SoundFileRewindFn = void(__fastcall*)(void* self, void* edx);
using SoundFileGetFormatFn = std::uint32_t(__fastcall*)(void* self, void* edx, void* format, std::uint32_t* bytes);
using SoundFileReadFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t count, void* dest);
using DSExceptionConstructFn = void*(__fastcall*)(void* self, void* edx, std::int32_t hr, const char* where,
                                                  std::uint32_t mostDerived);
using DSExceptionDescribeFn = String*(__fastcall*)(void* self, void* edx, String* result);
using ExceptionDestroyFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using AppNotActiveConstructFn = void*(__fastcall*)(void* self, void* edx, const String* msg,
                                                   std::uint32_t mostDerived);

void* __fastcall DirectX_CSoundFile_Construct(void* self, void* edx, const String* name);
std::uint32_t __fastcall DirectX_CSoundFile_Destroy(void* self, void* edx);
void* __fastcall DirectX_CMemorySoundFile_Construct(void* self, void* edx, const void* format, const void* data,
                                                    std::uint32_t bytes, std::uint32_t mode);
void __fastcall DirectX_CMemorySoundFile_Rewind(void* self, void* edx);
std::uint32_t __fastcall DirectX_CMemorySoundFile_GetFormat(void* self, void* edx, void* format,
                                                            std::uint32_t* bytes);
void* __fastcall DirectX_CMemorySoundFile_Read(void* self, void* edx, std::uint32_t count, void* dest);
void* __fastcall DirectX_CDirectSoundException_Construct(void* self, void* edx, std::int32_t hr, const char* where,
                                                         std::uint32_t mostDerived);
String* __fastcall DirectX_CDirectSoundException_Describe(void* self, void* edx, String* result);
std::uint32_t __fastcall DirectX_CDirectSoundException_Destroy(void* self, void* edx);
std::uint32_t __fastcall DirectX_CAppNotActiveException_Destroy(void* self, void* edx);
void* __fastcall DirectX_CAppNotActiveException_Construct(void* self, void* edx, const String* msg,
                                                          std::uint32_t mostDerived);

// ---------------------------------------------------------------------------
// CSoundFile, CMemorySoundFile
// ---------------------------------------------------------------------------

S3RE_HOOK(0x005c2270, DirectX_CSoundFile_Construct, SoundFileConstructFn, Original_DirectX_CSoundFile_Construct,
          DirectX_CSoundFile_Construct);

// Purpose: constructor of the abstract sound source of an effect (keeps
// its name).
void* __fastcall DirectX_CSoundFile_Construct(void* self, void* /*edx*/, const String* name) {
    // The copy constructor's allocator byte: the low byte of the argument
    // (deliberate, see the header).
    const auto allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(name));
    CSoundFile* const file = static_cast<CSoundFile*>(self);
    msvc6::String_FromSubstr(&file->name, nullptr, name, 0, 0xFFFFFFFFu, &allocator);
    file->vtable = vtables::kCSoundFile;
    return self;
}

S3RE_HOOK(0x005c23d0, DirectX_CSoundFile_Destroy, SoundFileDestroyFn, Original_DirectX_CSoundFile_Destroy,
          DirectX_CSoundFile_Destroy);

// Purpose: destructor of a sound source (frees its name).
std::uint32_t __fastcall DirectX_CSoundFile_Destroy(void* self, void* /*edx*/) {
    CSoundFile* const file = static_cast<CSoundFile*>(self);
    file->vtable = vtables::kCSoundFile;
    EraseAndFree(&file->name);
    return 0;  // callers ignore EAX
}

S3RE_HOOK(0x005c24e0, DirectX_CMemorySoundFile_Construct, MemorySoundFileConstructFn,
          Original_DirectX_CMemorySoundFile_Construct, DirectX_CMemorySoundFile_Construct);

// Purpose: constructor of the sound source that reads a sample from memory
// (a sample of the mapped SND files), optionally converted to 8 bits and
// half the rate for slower machines.
void* __fastcall DirectX_CMemorySoundFile_Construct(void* self, void* /*edx*/, const void* format, const void* data,
                                                    std::uint32_t bytes, std::uint32_t mode) {
    // All three temporaries take the low byte of `format` (its stack slot
    // holds their empty allocator arguments; deliberate).
    const auto allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(format));
    String name;
    name.allocator = allocator;
    msvc6::String_Tidy(&name, nullptr, 0);
    AssignThroughTemporaries(&name, texts::kMemoryMappedFile.c_str(), allocator);
    reinterpret_cast<SoundFileConstructFn>(kSoundFileConstruct)(self, nullptr, &name);
    EraseAndFree(&name);

    CMemorySoundFile* const file = AsMemoryFile(self);
    std::memcpy(&file->format, format, sizeof(WaveFormat));
    file->data = static_cast<const std::uint8_t*>(data);
    file->bytes = bytes;
    file->position = 0;
    file->mode = mode;
    file->base.vtable = vtables::kCMemorySoundFile;
    ApplyConversion(file);
    return self;
}

S3RE_HOOK(0x005c27b0, DirectX_CMemorySoundFile_Rewind, SoundFileRewindFn, Original_DirectX_CMemorySoundFile_Rewind,
          DirectX_CMemorySoundFile_Rewind);

// Purpose: Open and Close of a sample in memory: start reading from its
// beginning.
void __fastcall DirectX_CMemorySoundFile_Rewind(void* self, void* /*edx*/) {
    AsMemoryFile(self)->position = 0;
}

S3RE_HOOK(0x005c27c0, DirectX_CMemorySoundFile_GetFormat, SoundFileGetFormatFn,
          Original_DirectX_CMemorySoundFile_GetFormat, DirectX_CMemorySoundFile_GetFormat);

// Purpose: tells the effect the sample's format and size (to create its
// DirectSound buffer).
std::uint32_t __fastcall DirectX_CMemorySoundFile_GetFormat(void* self, void* /*edx*/, void* format,
                                                            std::uint32_t* bytes) {
    CMemorySoundFile* const file = AsMemoryFile(self);
    std::memcpy(format, &file->format, sizeof(WaveFormat));
    *bytes = file->bytes;
    // EAX as the original leaves it (unused): the format's address with its
    // last word in the low half.
    return (reinterpret_cast<std::uint32_t>(&file->format) & 0xFFFF0000u) | file->format.extraSize;
}

S3RE_HOOK(0x005c2800, DirectX_CMemorySoundFile_Read, SoundFileReadFn, Original_DirectX_CMemorySoundFile_Read,
          DirectX_CMemorySoundFile_Read);

// Purpose: delivers the next `count` bytes of the sample, converted to 8
// bits in modes 2 and 4.
void* __fastcall DirectX_CMemorySoundFile_Read(void* self, void* /*edx*/, std::uint32_t count, void* dest) {
    CMemorySoundFile* const file = AsMemoryFile(self);
    auto* out = static_cast<std::uint8_t*>(dest);
    if (file->mode == kModeCopy) {
        std::memcpy(out, file->data + file->position, count);
        file->position += count;
        return self;
    }
    if (file->mode != kModeTo8Bit && file->mode != kModeTo8BitHalfRate) {
        return self;
    }
    const std::uint32_t step = file->mode;  // bytes per input sample: 2, or 4 to skip every other one
    for (std::uint32_t i = 0; i < count; ++i) {
        // The high byte of the little-endian 16-bit sample.
        out[i] = static_cast<std::uint8_t>(file->data[file->position + 1] + kSignFlip);
        file->position += step;
    }
    return self;
}

// ---------------------------------------------------------------------------
// CDirectSoundException, CAppNotActiveException
// ---------------------------------------------------------------------------

S3RE_HOOK(0x005c15d0, DirectX_CDirectSoundException_Construct, DSExceptionConstructFn,
          Original_DirectX_CDirectSoundException_Construct, DirectX_CDirectSoundException_Construct);

// Purpose: constructor of the exception a failed DirectSound call throws
// (keeps the result code and the call's name for the error report).
void* __fastcall DirectX_CDirectSoundException_Construct(void* self, void* /*edx*/, std::int32_t hr,
                                                         const char* where, std::uint32_t mostDerived) {
    CDirectSoundException* const exception = static_cast<CDirectSoundException*>(self);
    if (mostDerived != 0) {
        exception->base.vbase = vbtables::kCDirectSoundException;
    }
    const auto allocator = static_cast<std::uint8_t>(mostDerived);
    String message;
    message.allocator = allocator;
    msvc6::String_Tidy(&message, nullptr, 0);
    AssignThroughTemporaries(&message, texts::kEmpty.c_str(), allocator);
    Win32_CRunTimeError_Construct(self, nullptr, &message, 0);
    msvc6::String_Split(&message, nullptr);
    EraseAndFree(&message);
    exception->result = hr;
    exception->where = where;
    exception->base.vtable = vtables::kCDirectSoundException;
    // The description is made and thrown away (a removed log call).
    String description;
    reinterpret_cast<DSExceptionDescribeFn>(kExceptionDescribe)(self, nullptr, &description);
    EraseAndFree(&description);
    return self;
}

S3RE_HOOK(0x005c1930, DirectX_CDirectSoundException_Describe, DSExceptionDescribeFn,
          Original_DirectX_CDirectSoundException_Describe, DirectX_CDirectSoundException_Describe);

// Purpose: the text of a DirectSound error for the error report:
// "DirectSound Error <name of the code> ocurred at <call>".
String* __fastcall DirectX_CDirectSoundException_Describe(void* self, void* /*edx*/, String* result) {
    const CDirectSoundException* const exception = static_cast<const CDirectSoundException*>(self);
    const std::uint8_t allocator = 0;  // the original's are uninitialized (see the header)
    String at;
    at.allocator = allocator;
    msvc6::String_Tidy(&at, nullptr, 0);
    AssignThroughConstructors(&at, texts::kOcurredAt.c_str());
    String head;
    head.allocator = allocator;
    msvc6::String_Tidy(&head, nullptr, 0);
    AssignThroughConstructors(&head, texts::kDirectSoundError.c_str());

    const char* const name = reinterpret_cast<DSErrorTextFn>(kDSErrorText)(exception->result);
    String withName, withAt, whole;
    msvc6::String_ConcatChars(&withName, &head, name);
    msvc6::String_ConcatString(&withAt, &withName, &at);
    msvc6::String_ConcatChars2(&whole, &withAt, exception->where);
    msvc6::String_FromSubstr(result, nullptr, &whole, 0, 0xFFFFFFFFu, &allocator);
    String* const destroyed[] = {&whole, &withAt, &withName, &head, &at};
    for (String* s : destroyed) {
        CallEraseAndFree(s);
    }
    return result;
}

namespace {

// The destructors of the exceptions without members of their own: the
// message at +8 destroyed, then ~CObject.
std::uint32_t DestroyMessageException(void* self) {
    EraseAndFree(&static_cast<CException*>(self)->message);
    reinterpret_cast<ThisFn>(kObjectDestroy)(self, nullptr);
    return 0;  // callers ignore EAX
}

}  // namespace

S3RE_HOOK(0x005c1830, DirectX_CDirectSoundException_Destroy, ExceptionDestroyFn,
          Original_DirectX_CDirectSoundException_Destroy, DirectX_CDirectSoundException_Destroy);

// Purpose: destructor of the DirectSound exception (frees its message).
std::uint32_t __fastcall DirectX_CDirectSoundException_Destroy(void* self, void* /*edx*/) {
    return DestroyMessageException(self);
}

S3RE_HOOK(0x005b88e0, DirectX_CAppNotActiveException_Destroy, ExceptionDestroyFn,
          Original_DirectX_CAppNotActiveException_Destroy, DirectX_CAppNotActiveException_Destroy);

// Purpose: destructor of the "application not active" exception (frees its
// message).
std::uint32_t __fastcall DirectX_CAppNotActiveException_Destroy(void* self, void* /*edx*/) {
    return DestroyMessageException(self);
}

std::uint32_t __fastcall Win32_CMCIException_Destroy(void* self, void* edx);

S3RE_HOOK(0x00614150, Win32_CMCIException_Destroy, ExceptionDestroyFn, Original_Win32_CMCIException_Destroy,
          Win32_CMCIException_Destroy);

// Purpose: destructor of the exception of a failed MCI call (the CD music;
// byte-identical to the two above).
std::uint32_t __fastcall Win32_CMCIException_Destroy(void* self, void* /*edx*/) {
    return DestroyMessageException(self);
}

S3RE_HOOK(0x005b8890, DirectX_CAppNotActiveException_Construct, AppNotActiveConstructFn,
          Original_DirectX_CAppNotActiveException_Construct, DirectX_CAppNotActiveException_Construct);

// Purpose: constructor of the exception thrown when lost sound or surface
// memory is to be restored while the game is not the active application.
void* __fastcall DirectX_CAppNotActiveException_Construct(void* self, void* /*edx*/, const String* msg,
                                                          std::uint32_t mostDerived) {
    CException* const exception = static_cast<CException*>(self);
    if (mostDerived != 0) {
        exception->vbase = vbtables::kCAppNotActiveException;
    }
    Win32_CRunTimeError_Construct(self, nullptr, msg, 0);
    exception->vtable = vtables::kCAppNotActiveException;
    return self;
}

}  // namespace s3re::game
