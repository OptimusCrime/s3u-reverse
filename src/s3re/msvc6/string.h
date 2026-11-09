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

// --- Batch 22: the other out-of-line members ------------------------------

// string(const string& x, pos, n, const allocator&) (0x004078a0).
using StringFromSubstrFn = String*(__fastcall*)(String* self, void* edx, const String* x,
                                                std::uint32_t pos, std::uint32_t n,
                                                const std::uint8_t* allocator);
String* __fastcall String_FromSubstr(String* self, void* edx, const String* x, std::uint32_t pos,
                                     std::uint32_t n, const std::uint8_t* allocator);
extern StringFromSubstrFn Original_String_FromSubstr;

// string(const char* s, const allocator&) (0x00407a50).
using StringFromTextFn = String*(__fastcall*)(String* self, void* edx, const char* s,
                                              const std::uint8_t* allocator);
String* __fastcall String_FromText(String* self, void* edx, const char* s,
                                   const std::uint8_t* allocator);
extern StringFromTextFn Original_String_FromText;

// string(n, c, const allocator&) (0x005f3bf0).
using StringFromRepeatFn = String*(__fastcall*)(String* self, void* edx, std::uint32_t n,
                                                std::uint32_t c, const std::uint8_t* allocator);
String* __fastcall String_FromRepeat(String* self, void* edx, std::uint32_t n, std::uint32_t c,
                                     const std::uint8_t* allocator);
extern StringFromRepeatFn Original_String_FromRepeat;

// erase(pos, n) (0x00407b10).
using StringEraseFn = String*(__fastcall*)(String* self, void* edx, std::uint32_t pos, std::uint32_t n);
String* __fastcall String_Erase(String* self, void* edx, std::uint32_t pos, std::uint32_t n);
extern StringEraseFn Original_String_Erase;

// operator=(const char* s) (0x00409a90).
using StringAssignTextFn = String*(__fastcall*)(String* self, void* edx, const char* s);
String* __fastcall String_AssignText(String* self, void* edx, const char* s);
extern StringAssignTextFn Original_String_AssignText;

// assign(n, c) (0x0041f430).
using StringAssignRepeatFn = String*(__fastcall*)(String* self, void* edx, std::uint32_t n,
                                                  std::uint32_t c);
String* __fastcall String_AssignRepeat(String* self, void* edx, std::uint32_t n, std::uint32_t c);
extern StringAssignRepeatFn Original_String_AssignRepeat;

// Assigns s, or the empty text at 0x7a7868 when s is null (0x00413d70).
using StringAssignTextOrEmptyFn = String*(__fastcall*)(String* self, void* edx, const char* s);
String* __fastcall String_AssignTextOrEmpty(String* self, void* edx, const char* s);
extern StringAssignTextOrEmptyFn Original_String_AssignTextOrEmpty;

// operator[](pos), the non-const one that freezes the buffer (0x004142d0).
using StringAtFn = char*(__fastcall*)(String* self, void* edx, std::uint32_t pos);
char* __fastcall String_At(String* self, void* edx, std::uint32_t pos);
extern StringAtFn Original_String_At;

// _Freeze() (0x00414490).
using StringFreezeFn = void(__fastcall*)(String* self, void* edx);
void __fastcall String_Freeze(String* self, void* edx);
extern StringFreezeFn Original_String_Freeze;

// operator+(const string&, const string&) (0x00409e60), __cdecl with the
// result object as hidden first argument.
using StringPlusStringFn = String*(__cdecl*)(String* result, const String* left, const String* right);
String* __cdecl String_PlusString(String* result, const String* left, const String* right);
extern StringPlusStringFn Original_String_PlusString;

// operator+(const char*, const string&) (0x00409f90).
using StringCharsPlusFn = String*(__cdecl*)(String* result, const char* left, const String* right);
String* __cdecl String_CharsPlus(String* result, const char* left, const String* right);
extern StringCharsPlusFn Original_String_CharsPlus;

// operator+(const string&, char) (0x00421aa0); the char is passed as a dword.
using StringPlusCharFn = String*(__cdecl*)(String* result, const String* left, std::uint32_t c);
String* __cdecl String_PlusChar(String* result, const String* left, std::uint32_t c);
extern StringPlusCharFn Original_String_PlusChar;

// Compiler-generated __fastcall helpers like String_ConcatChars (ECX = self,
// EDX = left, right on the stack) that construct self from operator+:
// left + right with both strings (0x004077f0), C text + string, or a copy of
// right when the text is null (0x00408e50 and its twin 0x0043d8e0), and
// string + char (0x00420cb0).
using StringConcatStringFn = String*(__fastcall*)(String* self, const String* left, const String* right);
String* __fastcall String_ConcatString(String* self, const String* left, const String* right);
extern StringConcatStringFn Original_String_ConcatString;

using StringConcatCharsLeftFn = String*(__fastcall*)(String* self, const char* left, const String* right);
String* __fastcall String_ConcatCharsLeft(String* self, const char* left, const String* right);
extern StringConcatCharsLeftFn Original_String_ConcatCharsLeft;
String* __fastcall String_ConcatCharsLeft2(String* self, const char* left, const String* right);
extern StringConcatCharsLeftFn Original_String_ConcatCharsLeft2;

using StringConcatCharFn = String*(__fastcall*)(String* self, const String* left, std::uint32_t c);
String* __fastcall String_ConcatChar(String* self, const String* left, std::uint32_t c);
extern StringConcatCharFn Original_String_ConcatChar;

// Helpers for other files that work on embedded strings (not hooks).
namespace detail {
// npos as the original reads it (its static member at 0x655390).
std::uint32_t Npos();
// c_str(): the buffer, or the static empty string at 0x6553a0.
char* CStr(const String* s);
// Drop one reference to a buffer: free it when unshared or frozen.
void Release(char* ptr);
// erase(p0, m) as the compiler inlines it.
void Erase(String* self, std::uint32_t p0, std::uint32_t m);
// Empty the fields (no release).
void Clear(String* s);
// strlen.
std::uint32_t Length(const char* s);
}  // namespace detail

}  // namespace s3re::msvc6
