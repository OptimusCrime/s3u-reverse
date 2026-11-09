#pragma once

// std::basic_string<char> as compiled into S3.EXE by MSVC 6.0 (Dinkumware STL).
//
// Game objects embed this layout directly, so code that touches game data must
// use it as-is rather than a modern std::string. The buffer is reference
// counted: the byte before _Ptr holds the count (0 = unshared, 0xFF = frozen,
// never shared; copies of a string share its buffer while the count is below
// 0xFE). The buffer is allocated with the game's operator new as _Res + 2
// bytes: count byte, characters, terminating zero.
//
// The functions below are the out-of-line members S3.EXE contains (MSVC 6.0
// inlined most of the others into their callers). Member functions are
// __thiscall in the original; they are written as __fastcall with a dummy EDX
// parameter (see hooks.h). Arguments the original reads as bool are passed as
// dwords; only the low byte counts.
//
// Exceptions: where the original calls std::_Xlen (0x61e4fa, "string too
// long") or std::_Xran (0x61e646, "invalid position"), ours calls the same
// game functions, which throw the game's C++ exception. The original's
// exception-handling frames free the temporaries of string_from_cstr,
// string_concat_cstr and operator+ during unwinding; ours have none, so on
// such an error the temporaries leak. That only differs on paths where the
// game is about to report a fatal error.

#include <cstdint>

namespace s3re::msvc6 {

struct String {
    std::uint8_t allocator;  // empty allocator object, padded to 4 bytes
    char* ptr;               // _Ptr: characters, or null when empty
    std::uint32_t len;       // _Len: length, excluding the terminating zero
    std::uint32_t res;       // _Res: capacity, excluding the terminating zero
};
static_assert(sizeof(String) == 16, "must match the MSVC 6.0 layout");

// _Eos(n): set the length to n and write the terminating zero (0x00401f60).
using StringEosFn = void(__fastcall*)(String* self, void* edx, std::uint32_t n);
void __fastcall String_Eos(String* self, void* edx, std::uint32_t n);
extern StringEosFn Original_String_Eos;

// _Tidy(built) (0x00402170): release the buffer if `built`, then empty.
using StringTidyFn = void(__fastcall*)(String* self, void* edx, std::uint32_t built);
void __fastcall String_Tidy(String* self, void* edx, std::uint32_t built);
extern StringTidyFn Original_String_Tidy;

// _Tidy(true) without the argument (0x00401d20).
using StringTidyFreeFn = void(__fastcall*)(String* self, void* edx);
void __fastcall String_TidyFree(String* self, void* edx);
extern StringTidyFreeFn Original_String_TidyFree;

// max_size() (0x00402260): 0xFFFFFFFD.
using StringMaxSizeFn = std::uint32_t(__fastcall*)(const String* self, void* edx);
std::uint32_t __fastcall String_MaxSize(const String* self, void* edx);
extern StringMaxSizeFn Original_String_MaxSize;

// _Copy(n) (0x00402270): move the text into a new, unshared buffer of
// capacity n | 31.
using StringCopyFn = void(__fastcall*)(String* self, void* edx, std::uint32_t n);
void __fastcall String_Copy(String* self, void* edx, std::uint32_t n);
extern StringCopyFn Original_String_Copy;

// _Grow(n, trim) (0x00401f80): make room for n characters in an unshared
// buffer; false if the string is empty afterwards (n == 0).
using StringGrowFn = bool(__fastcall*)(String* self, void* edx, std::uint32_t n, std::uint32_t trim);
bool __fastcall String_Grow(String* self, void* edx, std::uint32_t n, std::uint32_t trim);
extern StringGrowFn Original_String_Grow;

// _Split() (0x004020a0): give a shared string its own copy of the text.
using StringSplitFn = void(__fastcall*)(String* self, void* edx);
void __fastcall String_Split(String* self, void* edx);
extern StringSplitFn Original_String_Split;

// assign(const char* s, n) (0x004021c0).
using StringAssignCharsFn = String*(__fastcall*)(String* self, void* edx, const char* s, std::uint32_t n);
String* __fastcall String_AssignChars(String* self, void* edx, const char* s, std::uint32_t n);
extern StringAssignCharsFn Original_String_AssignChars;

// assign(const string& x, pos, n) (0x00401d60).
using StringAssignSubstrFn = String*(__fastcall*)(String* self, void* edx, const String* x,
                                                  std::uint32_t pos, std::uint32_t n);
String* __fastcall String_AssignSubstr(String* self, void* edx, const String* x, std::uint32_t pos,
                                       std::uint32_t n);
extern StringAssignSubstrFn Original_String_AssignSubstr;

// The destructor (0x00401b60): erase(), then _Tidy(true).
using StringDestroyFn = void(__fastcall*)(String* self, void* edx);
void __fastcall String_Destroy(String* self, void* edx);
extern StringDestroyFn Original_String_Destroy;

// Constructor from (const char* s, n) (0x00401a10).
using StringFromCharsFn = String*(__fastcall*)(String* self, void* edx, const char* s, std::uint32_t n);
String* __fastcall String_FromChars(String* self, void* edx, const char* s, std::uint32_t n);
extern StringFromCharsFn Original_String_FromChars;

// Default constructor with an allocator (0x00407880).
using StringDefaultFn = String*(__fastcall*)(String* self, void* edx, const std::uint8_t* allocator);
String* __fastcall String_Default(String* self, void* edx, const std::uint8_t* allocator);
extern StringDefaultFn Original_String_Default;

// operator+(const string& left, const char* right) (0x00402380), __cdecl
// with the result object as hidden first argument.
using StringPlusCharsFn = String*(__cdecl*)(String* result, const String* left, const char* right);
String* __cdecl String_PlusChars(String* result, const String* left, const char* right);
extern StringPlusCharsFn Original_String_PlusChars;

// Constructs `self` as left + right, or as a copy of left when right is null
// (0x00401c70). A compiler-generated __fastcall helper: ECX = self,
// EDX = left, right on the stack.
using StringConcatCharsFn = String*(__fastcall*)(String* self, const String* left, const char* right);
String* __fastcall String_ConcatChars(String* self, const String* left, const char* right);
extern StringConcatCharsFn Original_String_ConcatChars;

}  // namespace s3re::msvc6
