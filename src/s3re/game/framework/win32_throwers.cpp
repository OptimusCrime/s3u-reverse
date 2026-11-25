// The framework's other exception classes and the functions that throw them
// (Win32\Classes.cpp, RTTI names; session 58, wip/framework-errors):
// Win32::CHResult (a Windows error or HRESULT), CTimeout, CMMException
// (winmm), CMCIException (the CD audio's MCI), and the destructors of every
// exception class whose only member is the message (one shape, compiled
// many times). Layouts: framework_layout.h (CException: +0 vtable, +4
// virtual-base pointer, +8 message; CHResult +0x18 code, +0x1c location;
// CCodeException +0x18 code). Constructors are thiscall with the hidden
// last argument `mostDerived` (see win32_exceptions.cpp).
//
// Win32::CHResult::CHResult (0x00613220): (msg, code, const string*
//   location, mostDerived) ret 10h: if mostDerived, +4 = 0x65d8e8;
//   CRunTimeError(msg, 0); +0x18 = code; +0x1c = an empty string whose
//   allocator byte is the low byte of mostDerived, then assign(location, 0,
//   -1); +0 = 0x65d8e0; Describe() into a temporary that is split, erased
//   and freed again (a removed log call, as in CAssertmentFailed). EAX = this.
// Win32::CHResult::Describe (0x006134d0, slot 1): (string* result) ret 4.
//   A Win32 HRESULT (facility bits (code & 0x1fff0000) == 0x70000):
//   FormatMessageA(ALLOCATE_BUFFER | FROM_SYSTEM, 0, code & 0xffff,
//   LANG_USER_DEFAULT, &text, 0, 0); on failure sprintf(buffer, "Windows
//   error #%x occurred at %s.", code, message); otherwise the system text
//   without its last two characters (the CR LF; string(text, al) then
//   string(that, 0, strlen - 2, al)) goes into sprintf(buffer, "Windows
//   error #%x (%s) occured at %s.", code, text, message), then LocalFree.
//   Any other code: FormatMessageA with the whole code, its result not
//   checked; the text is built with _Tidy + assign instead, then the same
//   sprintf and LocalFree. *result = string(buffer) (through two
//   temporaries); its allocator byte comes from an uninitialized local
//   (ours 0; never read). EAX = result.
// Win32::CHResult::~CHResult (0x00613340): location (+0x1c) and message
//   (+8) erased and freed, then ~CObject.
// Win32::ThrowWin32Error (0x00610de0) / ThrowHResult (0x00611050):
//   fastcall (ECX = expression, EDX = file, stack: line, code, const
//   string* location), never return: the message expression + ", file " +
//   file + ", line " + _itoa(line) (Win32_BuildPlaceMessage, the code of
//   ThrowOutOfMemory); new 0x2c bytes, CHResult(message, hr, location, 1)
//   (null if new fails), thrown with ThrowInfo 0x68ea98. ThrowWin32Error
//   makes the HRESULT from a GetLastError code: 0 stays 0, otherwise
//   (code & 0xffff) | 0x80070000; ThrowHResult passes the code as it is.
// Win32::CTimeout::CTimeout (0x00615120): (msg, mostDerived) ret 8: if
//   mostDerived, +4 = 0x65d938; CRunTimeError(msg, 0); +0 = 0x65d930.
// Win32::ThrowTimeout (0x00610ba0): (expression, file; line): message as
//   above, new 0x18 bytes, CTimeout(message, 1), ThrowInfo 0x674f38.
// Win32::CMMException::CMMException (0x00613a40) and
//   Win32::CMCIException::CMCIException (0x00613fc0): (msg, code,
//   mostDerived) ret 0Ch: virtual-base pointer 0x65d8fc / 0x65d910;
//   CRunTimeError(msg, 0); +0x18 = code; vtable 0x65d8f4 / 0x65d908; then
//   their Describe into a temporary that is split, erased and freed.
// Win32::CMMException::Describe (0x00613cd0): "Multimedia Error #" +
//   _ultoa(code) + " at " + message. EAX = result.
// Win32::CMCIException::Describe (0x00614250): mciGetErrorStringA(code,
//   text, 1024) (its result not checked; the buffer starts as the word at
//   0x7e9178, then zeros), then "MCI Error #" + _ultoa(code) + " (" + text
//   + ") at " + message. EAX = result.
// Win32::ThrowMMError (0x006112a0): (expression, file; line, code): message
//   as above, new 0x1c bytes, CMMException(message, code, 1), ThrowInfo
//   0x682f68.
// Win32::ThrowMCIError (0x006114f0): (expression, file; line, code): its
//   own message code: message = expression; then the strings _itoa(line),
//   ", line ", file and ", file " are constructed in this order and joined
//   as message + ", file " + file + ", line " + number with operator+
//   (0x4077f0); new 0x1c bytes, CMCIException(message, code, 1), ThrowInfo
//   0x68ec98.
// Destructors of the classes without members of their own (the inlined
//   ~string of the message, then ~CObject 0x610330; EAX not used):
//   CException 0x610830, CRunTimeError 0x5b93c0, COutOfMemory 0x612790,
//   CProgrammingError 0x612c80, CAssertmentFailed 0x612d80, CTimeout
//   0x615170, CMMException 0x613bd0, CStructuredException 0x60e490,
//   DirectX::CDirectInputException 0x5b94c0, CDirectDrawException 0x5be3e0,
//   CDirectPlayException 0x5c0010, Lobby::priv::CDataError 0x600400 (and
//   CDirectSoundException, CAppNotActiveException, CMCIException in
//   directx_sound_files.cpp).
//
// Allocator bytes: the temporaries' are uninitialized stack bytes in the
// original; all of them are freed before the functions return, so ours use
// 0. The results of the Describe functions keep theirs (a known,
// behaviour-irrelevant difference: MSVC 6.0's allocator is an empty class).
//
// Known differences: (1) like ThrowOutOfMemory, the throwers have no
// exception frame, so their message leaks when the exception unwinds (the
// original's frame frees it). (2) CHResult::Describe of a non-Win32 code
// reads the pointer FormatMessageA left behind even when it failed; the
// original's is an uninitialized local (it would take strlen of garbage),
// ours starts as null and then skips the text. Both only happen on the
// way to a fatal error report.

#include "game/framework/exception_strings.h"
#include "game/framework/framework_layout.h"
#include "game/globals.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"

#include <cstdint>

namespace s3re::game {

// win32_exceptions.cpp, win32_errors.cpp
void* __fastcall Win32_CRunTimeError_Construct(void* self, void* edx, const msvc6::String* msg,
                                               std::uint32_t mostDerived);
void Win32_BuildPlaceMessage(msvc6::String* message, const char* text, const char* file, std::int32_t line);

namespace {

using framework::CCodeException;
using framework::CException;
using framework::CHResult;
using msvc6::String;

constexpr std::uintptr_t kObjectDestroy = 0x00610330;  // Win32::CObject::~CObject
constexpr std::uintptr_t kCHResultDescribe = 0x006134d0;
constexpr std::uintptr_t kCMMExceptionDescribe = 0x00613cd0;
constexpr std::uintptr_t kCMCIExceptionDescribe = 0x00614250;
constexpr std::uintptr_t kItoa = 0x00621380;
constexpr std::uintptr_t kUltoa = 0x006213aa;
constexpr std::uintptr_t kSprintf = 0x0061f27e;
constexpr std::uintptr_t kCxxThrowException = 0x0061f027;
constexpr std::int32_t kDecimal = 10;  // _itoa's radix

// Object sizes the throwers allocate.
constexpr std::uint32_t kHResultBytes = 0x2c;
constexpr std::uint32_t kTimeoutBytes = 0x18;
constexpr std::uint32_t kCodeExceptionBytes = 0x1c;

// HRESULTs
constexpr std::uint32_t kFacilityMask = 0x1FFF0000;
constexpr std::uint32_t kFacilityWin32 = 7u << 16;          // FACILITY_WIN32 (7) in the facility bits
constexpr std::uint32_t kWin32ErrorBase = 0x80070000;      // HRESULT_FROM_WIN32's high word
constexpr std::uint32_t kWin32CodeMask = 0xFFFF;
// FormatMessageA
constexpr std::uint32_t kFormatFlags = 0x1100;  // FORMAT_MESSAGE_ALLOCATE_BUFFER | FROM_SYSTEM
constexpr std::uint32_t kUserDefaultLanguage = 0x400;  // LANG_USER_DEFAULT
constexpr std::uint32_t kLineEndLength = 2;            // the CR LF FormatMessageA ends its text with

constexpr std::uint32_t kDescribeBufferBytes = 0x1000;  // CHResult::Describe's sprintf buffer
constexpr std::uint32_t kMciTextBytes = 0x400;           // CMCIException::Describe's text buffer

using ThisFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using DescribeFn = String*(__fastcall*)(void* self, void* edx, String* result);
using SprintfFn = int(__cdecl*)(char* buffer, const char* format, ...);
using NumberFn = char*(__cdecl*)(std::uint32_t value, char* buffer, std::int32_t radix);

CException* AsException(void* self) {
    return static_cast<CException*>(self);
}

[[noreturn]] void Throw(void* object, std::uintptr_t info) {
    using CxxThrowFn = void(__stdcall*)(void* object, const void* info);
    reinterpret_cast<CxxThrowFn>(kCxxThrowException)(&object, reinterpret_cast<const void*>(info));
    __assume(0);
}

// The constructors' leftover of a removed log call: the description is
// made, split, erased and freed.
void DescribeAndDiscard(void* self, std::uintptr_t describe) {
    String description;
    reinterpret_cast<DescribeFn>(describe)(self, nullptr, &description);
    msvc6::String_Split(&description, nullptr);
    framework::EraseAndFree(&description);
}

// The message destructors: the inlined ~string of the member, then ~CObject.
std::uint32_t DestroyMessage(void* self) {
    framework::EraseAndFree(&AsException(self)->message);
    reinterpret_cast<ThisFn>(kObjectDestroy)(self, nullptr);
    return 0;  // callers ignore EAX
}

char* Number(std::uintptr_t function, std::uint32_t value, char* digits) {
    return reinterpret_cast<NumberFn>(function)(value, digits, kDecimal);
}

}  // namespace

using HResultCtorFn = void*(__fastcall*)(void* self, void* edx, const String* msg, std::uint32_t code,
                                         const String* location, std::uint32_t mostDerived);
using CodeCtorFn = void*(__fastcall*)(void* self, void* edx, const String* msg, std::uint32_t code,
                                      std::uint32_t mostDerived);
using MessageCtorFn = void*(__fastcall*)(void* self, void* edx, const String* msg, std::uint32_t mostDerived);
using ExceptionDescribeFn = String*(__fastcall*)(void* self, void* edx, String* result);
using ExceptionDestroyFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using ThrowLocationFn = void(__fastcall*)(const char* text, const char* file, std::int32_t line, std::uint32_t code,
                                          const String* location);
using ThrowCodeFn = void(__fastcall*)(const char* text, const char* file, std::int32_t line, std::uint32_t code);
using ThrowPlaceFn = void(__fastcall*)(const char* text, const char* file, std::int32_t line);

void* __fastcall Win32_CHResult_Construct(void* self, void* edx, const String* msg, std::uint32_t code,
                                          const String* location, std::uint32_t mostDerived);
String* __fastcall Win32_CHResult_Describe(void* self, void* edx, String* result);
std::uint32_t __fastcall Win32_CHResult_Destroy(void* self, void* edx);
void __fastcall Win32_ThrowWin32Error(const char* text, const char* file, std::int32_t line, std::uint32_t error,
                                      const String* location);
void __fastcall Win32_ThrowHResult(const char* text, const char* file, std::int32_t line, std::uint32_t hr,
                                   const String* location);
void* __fastcall Win32_CTimeout_Construct(void* self, void* edx, const String* msg, std::uint32_t mostDerived);
void __fastcall Win32_ThrowTimeout(const char* text, const char* file, std::int32_t line);
void* __fastcall Win32_CMMException_Construct(void* self, void* edx, const String* msg, std::uint32_t code,
                                              std::uint32_t mostDerived);
String* __fastcall Win32_CMMException_Describe(void* self, void* edx, String* result);
void __fastcall Win32_ThrowMMError(const char* text, const char* file, std::int32_t line, std::uint32_t code);
void* __fastcall Win32_CMCIException_Construct(void* self, void* edx, const String* msg, std::uint32_t code,
                                               std::uint32_t mostDerived);
String* __fastcall Win32_CMCIException_Describe(void* self, void* edx, String* result);
void __fastcall Win32_ThrowMCIError(const char* text, const char* file, std::int32_t line, std::uint32_t code);
std::uint32_t __fastcall Win32_CException_Destroy(void* self, void* edx);
std::uint32_t __fastcall Win32_CRunTimeError_Destroy(void* self, void* edx);
std::uint32_t __fastcall Win32_COutOfMemory_Destroy(void* self, void* edx);
std::uint32_t __fastcall Win32_CProgrammingError_Destroy(void* self, void* edx);
std::uint32_t __fastcall Win32_CAssertmentFailed_Destroy(void* self, void* edx);
std::uint32_t __fastcall Win32_CTimeout_Destroy(void* self, void* edx);
std::uint32_t __fastcall Win32_CMMException_Destroy(void* self, void* edx);
std::uint32_t __fastcall Win32_CStructuredException_Destroy(void* self, void* edx);
std::uint32_t __fastcall DirectX_CDirectInputException_Destroy(void* self, void* edx);
std::uint32_t __fastcall DirectX_CDirectDrawException_Destroy(void* self, void* edx);
std::uint32_t __fastcall DirectX_CDirectPlayException_Destroy(void* self, void* edx);
std::uint32_t __fastcall Lobby_CDataError_Destroy(void* self, void* edx);

// ---------------------------------------------------------------------------
// CHResult
// ---------------------------------------------------------------------------

S3RE_HOOK(0x00613220, Win32_CHResult_Construct, HResultCtorFn, Original_Win32_CHResult_Construct,
          Win32_CHResult_Construct);

// Purpose: constructor of the exception for a failed Windows call (the
// error code as an HRESULT, for the game's error report).
void* __fastcall Win32_CHResult_Construct(void* self, void* /*edx*/, const String* msg, std::uint32_t code,
                                          const String* location, std::uint32_t mostDerived) {
    CHResult* const error = static_cast<CHResult*>(self);
    if (mostDerived != 0) {
        error->base.vbase = vbtables::kCHResult;
    }
    Win32_CRunTimeError_Construct(self, nullptr, msg, 0);
    error->code = code;
    framework::MakeEmpty(&error->location, static_cast<std::uint8_t>(mostDerived));
    msvc6::String_AssignSubstr(&error->location, nullptr, location, 0, 0xFFFFFFFFu);
    error->base.vtable = vtables::kCHResult;
    DescribeAndDiscard(self, kCHResultDescribe);
    return self;
}

namespace {

// The system text without the CR LF at its end, as the facility-Win32 branch
// builds it (0x61357a..0x6135ec): string(text, al), string(that, 0, n - 2, al).
void TrimmedByConstructors(String* described, const char* text, std::uint32_t trimmed) {
    const std::uint8_t allocator = 0;
    String fromText;
    String copy;
    msvc6::String_FromText(&fromText, nullptr, text, &allocator);
    msvc6::String_FromSubstr(&copy, nullptr, &fromText, 0, trimmed, &allocator);
    msvc6::String_AssignSubstr(described, nullptr, &copy, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(&copy, nullptr, 1);
    msvc6::String_Tidy(&fromText, nullptr, 1);
}

// The same in the other branch (0x6136ce..0x613765): _Tidy + assign(text, n).
void TrimmedByAssign(String* described, const char* text, std::uint32_t trimmed) {
    const std::uint8_t allocator = 0;
    String chars;
    framework::MakeEmpty(&chars, allocator);
    msvc6::String_AssignChars(&chars, nullptr, text, msvc6::detail::Length(text));
    String copy;
    msvc6::String_FromSubstr(&copy, nullptr, &chars, 0, trimmed, &allocator);
    msvc6::String_AssignSubstr(described, nullptr, &copy, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(&copy, nullptr, 1);
    msvc6::String_Tidy(&chars, nullptr, 1);
}

std::uint32_t FormatSystemMessage(std::uint32_t id, char** text) {
    return g_importFormatMessageA.get()(kFormatFlags, nullptr, id, kUserDefaultLanguage, reinterpret_cast<char*>(text),
                                        0, nullptr);
}

}  // namespace

S3RE_HOOK(0x006134d0, Win32_CHResult_Describe, ExceptionDescribeFn, Original_Win32_CHResult_Describe,
          Win32_CHResult_Describe);

// Purpose: the text of a Windows error for the error report: "Windows error
// #<code> (<the system's message>) occured at <where>".
String* __fastcall Win32_CHResult_Describe(void* self, void* /*edx*/, String* result) {
    const CHResult* const error = static_cast<const CHResult*>(self);
    const std::uint32_t code = error->code;
    const auto sprintf = reinterpret_cast<SprintfFn>(kSprintf);
    char buffer[kDescribeBufferBytes];
    char* systemText = nullptr;  // uninitialized in the original (see the header)
    const std::uint8_t allocator = 0;
    if ((code & kFacilityMask) == kFacilityWin32) {
        if (FormatSystemMessage(code & kWin32CodeMask, &systemText) == 0) {
            sprintf(buffer, texts::kWindowsError.c_str(), code, msvc6::detail::CStr(&error->base.message));
        } else {
            const char* const at = msvc6::detail::CStr(&error->base.message);
            String described;
            framework::MakeEmpty(&described, allocator);
            if (systemText != nullptr) {
                TrimmedByConstructors(&described, systemText, msvc6::detail::Length(systemText) - kLineEndLength);
            }
            sprintf(buffer, texts::kWindowsErrorText.c_str(), code, msvc6::detail::CStr(&described), at);
            framework::CallEraseAndFree(&described);
            g_importLocalFree.get()(systemText);
        }
    } else {
        FormatSystemMessage(code, &systemText);  // result not checked
        const char* const at = msvc6::detail::CStr(&error->base.message);
        String described;
        framework::MakeEmpty(&described, allocator);
        if (systemText != nullptr) {
            TrimmedByAssign(&described, systemText, msvc6::detail::Length(systemText) - kLineEndLength);
        }
        sprintf(buffer, texts::kWindowsErrorText.c_str(), code, msvc6::detail::CStr(&described), at);
        msvc6::String_Split(&described, nullptr);
        framework::EraseAndFree(&described);
        g_importLocalFree.get()(systemText);
    }
    framework::MakeEmpty(result, allocator);  // the original's allocator byte is uninitialized
    framework::AssignThroughAssigns(result, buffer, allocator);
    return result;
}

S3RE_HOOK(0x00613340, Win32_CHResult_Destroy, ExceptionDestroyFn, Original_Win32_CHResult_Destroy,
          Win32_CHResult_Destroy);

// Purpose: destructor of the Windows-error exception (frees its strings).
std::uint32_t __fastcall Win32_CHResult_Destroy(void* self, void* /*edx*/) {
    framework::EraseAndFree(&static_cast<CHResult*>(self)->location);
    return DestroyMessage(self);
}

namespace {

void ThrowHResultObject(const char* text, const char* file, std::int32_t line, std::uint32_t hr,
                        const String* location) {
    String message;  // left alive: the original's unwinding frees it, ours has no frame
    Win32_BuildPlaceMessage(&message, text, file, line);
    void* const block = msvc6::GameOperatorNew(kHResultBytes);
    void* thrown = nullptr;
    if (block != nullptr) {
        thrown = Win32_CHResult_Construct(block, nullptr, &message, hr, location, 1);
    }
    Throw(thrown, throw_info::kCHResultPointer);
}

}  // namespace

S3RE_HOOK(0x00610de0, Win32_ThrowWin32Error, ThrowLocationFn,
          Original_Win32_ThrowWin32Error, Win32_ThrowWin32Error);

// Purpose: the framework's check of a Windows call: throws CHResult with the
// GetLastError code (as an HRESULT) and the place of the call.
void __fastcall Win32_ThrowWin32Error(const char* text, const char* file, std::int32_t line, std::uint32_t error,
                                      const String* location) {
    const std::uint32_t hr = error == 0 ? 0 : (error & kWin32CodeMask) | kWin32ErrorBase;
    ThrowHResultObject(text, file, line, hr, location);
}

S3RE_HOOK(0x00611050, Win32_ThrowHResult, ThrowLocationFn, Original_Win32_ThrowHResult, Win32_ThrowHResult);

// Purpose: the same for a call that returns an HRESULT itself.
void __fastcall Win32_ThrowHResult(const char* text, const char* file, std::int32_t line, std::uint32_t hr,
                                   const String* location) {
    ThrowHResultObject(text, file, line, hr, location);
}

// ---------------------------------------------------------------------------
// CTimeout
// ---------------------------------------------------------------------------

S3RE_HOOK(0x00615120, Win32_CTimeout_Construct, MessageCtorFn, Original_Win32_CTimeout_Construct,
          Win32_CTimeout_Construct);

// Purpose: constructor of the exception thrown when a wait for a thread or
// event takes too long.
void* __fastcall Win32_CTimeout_Construct(void* self, void* /*edx*/, const String* msg, std::uint32_t mostDerived) {
    if (mostDerived != 0) {
        AsException(self)->vbase = vbtables::kCTimeout;
    }
    Win32_CRunTimeError_Construct(self, nullptr, msg, 0);
    AsException(self)->vtable = vtables::kCTimeout;
    return self;
}

S3RE_HOOK(0x00610ba0, Win32_ThrowTimeout, ThrowPlaceFn, Original_Win32_ThrowTimeout, Win32_ThrowTimeout);

// Purpose: throws CTimeout with the place of the wait that timed out.
void __fastcall Win32_ThrowTimeout(const char* text, const char* file, std::int32_t line) {
    String message;  // left alive (see the header)
    Win32_BuildPlaceMessage(&message, text, file, line);
    void* const block = msvc6::GameOperatorNew(kTimeoutBytes);
    void* thrown = nullptr;
    if (block != nullptr) {
        thrown = Win32_CTimeout_Construct(block, nullptr, &message, 1);
    }
    Throw(thrown, throw_info::kCTimeoutPointer);
}

// ---------------------------------------------------------------------------
// CMMException, CMCIException
// ---------------------------------------------------------------------------

namespace {

void ConstructCodeException(void* self, const String* msg, std::uint32_t code, std::uint32_t mostDerived,
                            std::uint32_t vbase, std::uint32_t vtable, std::uintptr_t describe) {
    CCodeException* const error = static_cast<CCodeException*>(self);
    if (mostDerived != 0) {
        error->base.vbase = vbase;
    }
    Win32_CRunTimeError_Construct(self, nullptr, msg, 0);
    error->code = code;
    error->base.vtable = vtable;
    DescribeAndDiscard(self, describe);
}

}  // namespace

S3RE_HOOK(0x00613a40, Win32_CMMException_Construct, CodeCtorFn, Original_Win32_CMMException_Construct,
          Win32_CMMException_Construct);

// Purpose: constructor of the exception for a failed multimedia (winmm)
// timer call.
void* __fastcall Win32_CMMException_Construct(void* self, void* /*edx*/, const String* msg, std::uint32_t code,
                                              std::uint32_t mostDerived) {
    ConstructCodeException(self, msg, code, mostDerived, vbtables::kCMMException, vtables::kCMMException,
                           kCMMExceptionDescribe);
    return self;
}

S3RE_HOOK(0x00613cd0, Win32_CMMException_Describe, ExceptionDescribeFn, Original_Win32_CMMException_Describe,
          Win32_CMMException_Describe);

// Purpose: the text of a multimedia error: "Multimedia Error #<code> at
// <where>".
String* __fastcall Win32_CMMException_Describe(void* self, void* /*edx*/, String* result) {
    const CCodeException* const error = static_cast<const CCodeException*>(self);
    const std::uint8_t allocator = 0;
    String at;
    framework::MakeEmpty(&at, allocator);
    framework::AssignThroughConstructors(&at, texts::kSpaceAt.c_str(), &allocator);
    String head;
    framework::MakeEmpty(&head, allocator);
    framework::AssignThroughConstructors(&head, texts::kMultimediaError.c_str(), &allocator);
    char digits[16];
    String withNumber, withAt, whole;
    msvc6::String_ConcatChars(&withNumber, &head, Number(kUltoa, error->code, digits));
    msvc6::String_ConcatString(&withAt, &withNumber, &at);
    msvc6::String_ConcatString(&whole, &withAt, &error->base.message);
    msvc6::String_FromSubstr(result, nullptr, &whole, 0, 0xFFFFFFFFu, &allocator);
    String* const destroyed[] = {&whole, &withAt, &withNumber, &head, &at};
    for (String* s : destroyed) {
        framework::CallEraseAndFree(s);
    }
    return result;
}

S3RE_HOOK(0x006112a0, Win32_ThrowMMError, ThrowCodeFn, Original_Win32_ThrowMMError, Win32_ThrowMMError);

// Purpose: throws CMMException for a failed multimedia timer call.
void __fastcall Win32_ThrowMMError(const char* text, const char* file, std::int32_t line, std::uint32_t code) {
    String message;  // left alive (see the header)
    Win32_BuildPlaceMessage(&message, text, file, line);
    void* const block = msvc6::GameOperatorNew(kCodeExceptionBytes);
    void* thrown = nullptr;
    if (block != nullptr) {
        thrown = Win32_CMMException_Construct(block, nullptr, &message, code, 1);
    }
    Throw(thrown, throw_info::kCMMExceptionPointer);
}

S3RE_HOOK(0x00613fc0, Win32_CMCIException_Construct, CodeCtorFn, Original_Win32_CMCIException_Construct,
          Win32_CMCIException_Construct);

// Purpose: constructor of the exception for a failed MCI command (the CD
// music).
void* __fastcall Win32_CMCIException_Construct(void* self, void* /*edx*/, const String* msg, std::uint32_t code,
                                               std::uint32_t mostDerived) {
    ConstructCodeException(self, msg, code, mostDerived, vbtables::kCMCIException, vtables::kCMCIException,
                           kCMCIExceptionDescribe);
    return self;
}

S3RE_HOOK(0x00614250, Win32_CMCIException_Describe, ExceptionDescribeFn, Original_Win32_CMCIException_Describe,
          Win32_CMCIException_Describe);

// Purpose: the text of an MCI error: "MCI Error #<code> (<MCI's message>)
// at <where>".
String* __fastcall Win32_CMCIException_Describe(void* self, void* /*edx*/, String* result) {
    const CCodeException* const error = static_cast<const CCodeException*>(self);
    char mciText[kMciTextBytes] = {};
    const std::uint16_t start = g_emptyTextWord;
    mciText[0] = static_cast<char>(start & 0xFF);
    mciText[1] = static_cast<char>(start >> 8);
    g_importMciGetErrorStringA.get()(error->code, mciText, kMciTextBytes);  // result not checked
    const std::uint8_t allocator = 0;
    String closeAt;
    framework::MakeEmpty(&closeAt, allocator);
    framework::AssignThroughConstructors(&closeAt, texts::kCloseParenthesisAt.c_str(), &allocator);
    String open;
    framework::ConstructFromText(&open, texts::kOpenParenthesis.c_str(), &allocator);
    String head;
    framework::ConstructFromText(&head, texts::kMciError.c_str(), &allocator);
    char digits[16];
    String withNumber, withOpen, withText, withClose, whole;
    msvc6::String_ConcatChars(&withNumber, &head, Number(kUltoa, error->code, digits));
    msvc6::String_ConcatString(&withOpen, &withNumber, &open);
    msvc6::String_ConcatChars(&withText, &withOpen, mciText);
    msvc6::String_ConcatString(&withClose, &withText, &closeAt);
    msvc6::String_ConcatString(&whole, &withClose, &error->base.message);
    msvc6::String_FromSubstr(result, nullptr, &whole, 0, 0xFFFFFFFFu, &allocator);
    framework::CallEraseAndFree(&whole);
    String* const destroyed[] = {&withClose, &withText, &withOpen, &withNumber, &head, &open, &closeAt};
    for (String* s : destroyed) {
        msvc6::String_Destroy(s, nullptr);
    }
    return result;
}

S3RE_HOOK(0x006114f0, Win32_ThrowMCIError, ThrowCodeFn, Original_Win32_ThrowMCIError, Win32_ThrowMCIError);

// Purpose: throws CMCIException for a failed MCI command of the CD music.
void __fastcall Win32_ThrowMCIError(const char* text, const char* file, std::int32_t line, std::uint32_t code) {
    const std::uint8_t allocator = 0;  // the original's are uninitialized
    String message;                    // left alive (see the header)
    framework::MakeEmpty(&message, allocator);
    if (text != nullptr) {
        framework::AssignThroughConstructors(&message, text, &allocator);
    }
    char digits[16];
    const char* const number =
        reinterpret_cast<char*(__cdecl*)(std::int32_t, char*, std::int32_t)>(kItoa)(line, digits, kDecimal);
    String lineNumber, lineText, fileName, fileText;
    msvc6::String_Default(&lineNumber, nullptr, &allocator);
    if (number != nullptr) {
        framework::AssignThroughConstructors(&lineNumber, number, &allocator);
    }
    framework::ConstructFromText(&lineText, texts::kCommaLine.c_str(), &allocator);
    msvc6::String_Default(&fileName, nullptr, &allocator);
    if (file != nullptr) {
        framework::AssignThroughConstructors(&fileName, file, &allocator);
    }
    framework::ConstructFromText(&fileText, texts::kCommaFile.c_str(), &allocator);
    String withFileText, withFile, withLineText, whole;
    msvc6::String_ConcatString(&withFileText, &message, &fileText);
    msvc6::String_ConcatString(&withFile, &withFileText, &fileName);
    msvc6::String_ConcatString(&withLineText, &withFile, &lineText);
    msvc6::String_ConcatString(&whole, &withLineText, &lineNumber);
    msvc6::String_AssignSubstr(&message, nullptr, &whole, 0, msvc6::detail::Npos());
    String* const destroyed[] = {&whole,     &withLineText, &withFile,   &withFileText,
                                 &fileText, &fileName,     &lineText, &lineNumber};
    for (String* s : destroyed) {
        msvc6::String_Destroy(s, nullptr);
    }
    void* const block = msvc6::GameOperatorNew(kCodeExceptionBytes);
    void* thrown = nullptr;
    if (block != nullptr) {
        thrown = Win32_CMCIException_Construct(block, nullptr, &message, code, 1);
    }
    Throw(thrown, throw_info::kCMCIExceptionPointer);
}

// ---------------------------------------------------------------------------
// The message destructors
// ---------------------------------------------------------------------------

S3RE_HOOK(0x00610830, Win32_CException_Destroy, ExceptionDestroyFn,
          Original_Win32_CException_Destroy, Win32_CException_Destroy);

// Purpose: destructor of the framework's base exception (frees its message).
std::uint32_t __fastcall Win32_CException_Destroy(void* self, void* /*edx*/) {
    return DestroyMessage(self);
}

S3RE_HOOK(0x005b93c0, Win32_CRunTimeError_Destroy, ExceptionDestroyFn,
          Original_Win32_CRunTimeError_Destroy, Win32_CRunTimeError_Destroy);

// Purpose: destructor of the run-time error (frees its message).
std::uint32_t __fastcall Win32_CRunTimeError_Destroy(void* self, void* /*edx*/) {
    return DestroyMessage(self);
}

S3RE_HOOK(0x00612790, Win32_COutOfMemory_Destroy, ExceptionDestroyFn,
          Original_Win32_COutOfMemory_Destroy, Win32_COutOfMemory_Destroy);

// Purpose: destructor of the out-of-memory exception (frees its message).
std::uint32_t __fastcall Win32_COutOfMemory_Destroy(void* self, void* /*edx*/) {
    return DestroyMessage(self);
}

S3RE_HOOK(0x00612c80, Win32_CProgrammingError_Destroy, ExceptionDestroyFn,
          Original_Win32_CProgrammingError_Destroy, Win32_CProgrammingError_Destroy);

// Purpose: destructor of the programming-error exception (frees its message).
std::uint32_t __fastcall Win32_CProgrammingError_Destroy(void* self, void* /*edx*/) {
    return DestroyMessage(self);
}

S3RE_HOOK(0x00612d80, Win32_CAssertmentFailed_Destroy, ExceptionDestroyFn,
          Original_Win32_CAssertmentFailed_Destroy, Win32_CAssertmentFailed_Destroy);

// Purpose: destructor of the failed-assertion exception (frees its message).
std::uint32_t __fastcall Win32_CAssertmentFailed_Destroy(void* self, void* /*edx*/) {
    return DestroyMessage(self);
}

S3RE_HOOK(0x00615170, Win32_CTimeout_Destroy, ExceptionDestroyFn,
          Original_Win32_CTimeout_Destroy, Win32_CTimeout_Destroy);

// Purpose: destructor of the timeout exception (frees its message).
std::uint32_t __fastcall Win32_CTimeout_Destroy(void* self, void* /*edx*/) {
    return DestroyMessage(self);
}

S3RE_HOOK(0x00613bd0, Win32_CMMException_Destroy, ExceptionDestroyFn,
          Original_Win32_CMMException_Destroy, Win32_CMMException_Destroy);

// Purpose: destructor of the multimedia-error exception (frees its message).
std::uint32_t __fastcall Win32_CMMException_Destroy(void* self, void* /*edx*/) {
    return DestroyMessage(self);
}

S3RE_HOOK(0x0060e490, Win32_CStructuredException_Destroy, ExceptionDestroyFn,
          Original_Win32_CStructuredException_Destroy, Win32_CStructuredException_Destroy);

// Purpose: destructor of the structured (Windows) exception (frees its message).
std::uint32_t __fastcall Win32_CStructuredException_Destroy(void* self, void* /*edx*/) {
    return DestroyMessage(self);
}

S3RE_HOOK(0x005b94c0, DirectX_CDirectInputException_Destroy, ExceptionDestroyFn,
          Original_DirectX_CDirectInputException_Destroy, DirectX_CDirectInputException_Destroy);

// Purpose: destructor of the DirectInput exception (frees its message).
std::uint32_t __fastcall DirectX_CDirectInputException_Destroy(void* self, void* /*edx*/) {
    return DestroyMessage(self);
}

S3RE_HOOK(0x005be3e0, DirectX_CDirectDrawException_Destroy, ExceptionDestroyFn,
          Original_DirectX_CDirectDrawException_Destroy, DirectX_CDirectDrawException_Destroy);

// Purpose: destructor of the DirectDraw exception (frees its message).
std::uint32_t __fastcall DirectX_CDirectDrawException_Destroy(void* self, void* /*edx*/) {
    return DestroyMessage(self);
}

S3RE_HOOK(0x005c0010, DirectX_CDirectPlayException_Destroy, ExceptionDestroyFn,
          Original_DirectX_CDirectPlayException_Destroy, DirectX_CDirectPlayException_Destroy);

// Purpose: destructor of the DirectPlay exception (frees its message).
std::uint32_t __fastcall DirectX_CDirectPlayException_Destroy(void* self, void* /*edx*/) {
    return DestroyMessage(self);
}

S3RE_HOOK(0x00600400, Lobby_CDataError_Destroy, ExceptionDestroyFn,
          Original_Lobby_CDataError_Destroy, Lobby_CDataError_Destroy);

// Purpose: destructor of the data-error exception (malformed data files;
// frees its message).
std::uint32_t __fastcall Lobby_CDataError_Destroy(void* self, void* /*edx*/) {
    return DestroyMessage(self);
}

}  // namespace s3re::game
