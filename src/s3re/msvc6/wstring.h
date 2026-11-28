#pragma once

// std::basic_string<unsigned short> (16-bit characters) as compiled into
// S3.EXE by MSVC 6.0: the layout of std::string (string.h) with 16-bit
// characters. The buffer is allocated with the game's operator new as
// (_Res + 2) * 2 bytes: one unused byte, the count byte (0 = unshared,
// 0xFF = frozen), the characters and a terminating zero word; _Ptr points
// 2 bytes into the block, so the count byte is the byte before _Ptr.
// Members are __thiscall in the original, written as __fastcall with a
// dummy EDX (hooks.h). Implemented in wstring.cpp.

#include <cstdint>

namespace s3re::msvc6 {

#pragma pack(push, 1)
struct WString {
    std::uint8_t allocator;  // empty allocator object, padded to 4 bytes
    std::uint8_t pad[3];
    std::uint16_t* ptr;      // _Ptr: characters, or null when empty
    std::uint32_t len;       // _Len
    std::uint32_t res;       // _Res
};
#pragma pack(pop)
static_assert(sizeof(WString) == 16);

// _Copy(n) (0x004289e0, with its continuation 0x00428a62).
using WStringCopyFn = void(__fastcall*)(WString* self, void* edx, std::uint32_t n);
void __fastcall WString_Copy(WString* self, void* edx, std::uint32_t n);
extern WStringCopyFn Original_WString_Copy;

// _Grow(n, trim) (0x004288a0): false if the string is empty afterwards.
using WStringGrowFn = bool(__fastcall*)(WString* self, void* edx, std::uint32_t n, std::uint32_t trim);
bool __fastcall WString_Grow(WString* self, void* edx, std::uint32_t n, std::uint32_t trim);
extern WStringGrowFn Original_WString_Grow;

// _Split() (0x00428af0).
using WStringSplitFn = void(__fastcall*)(WString* self, void* edx);
void __fastcall WString_Split(WString* self, void* edx);
extern WStringSplitFn Original_WString_Split;

// assign(const wstring& x, pos, n) (0x00428400).
using WStringAssignSubstrFn = WString*(__fastcall*)(WString* self, void* edx, const WString* x, std::uint32_t pos,
                                                    std::uint32_t n);
WString* __fastcall WString_AssignSubstr(WString* self, void* edx, const WString* x, std::uint32_t pos,
                                         std::uint32_t n);
extern WStringAssignSubstrFn Original_WString_AssignSubstr;

// The copy constructor wstring(const wstring& x) (0x00534d30), EAX = this.
using WStringCopyConstructFn = WString*(__fastcall*)(WString* self, void* edx, const WString* x);
WString* __fastcall WString_CopyConstruct(WString* self, void* edx, const WString* x);
extern WStringCopyConstructFn Original_WString_CopyConstruct;

// operator+(const wchar_t* text, const wstring& right) (0x005418a0), cdecl,
// the result object first; EAX = result.
using WStringConcatWcsLeftFn = WString*(__cdecl*)(WString* result, const std::uint16_t* text, const WString* right);
WString* __cdecl WString_ConcatWcsLeft(WString* result, const std::uint16_t* text, const WString* right);
extern WStringConcatWcsLeftFn Original_WString_ConcatWcsLeft;

// wstring(const wstring& x, pos, n, const allocator& a) (0x00541410), ret 0x10,
// EAX = this.
using WStringSubstrConstructFn = WString*(__fastcall*)(WString* self, void* edx, const WString* x, std::uint32_t pos,
                                                       std::uint32_t n, const std::uint8_t* allocator);
WString* __fastcall WString_SubstrConstruct(WString* self, void* edx, const WString* x, std::uint32_t pos,
                                            std::uint32_t n, const std::uint8_t* allocator);
extern WStringSubstrConstructFn Original_WString_SubstrConstruct;

// erase(pos, n) (0x00428730), EAX = this.
using WStringEraseFn = WString*(__fastcall*)(WString* self, void* edx, std::uint32_t pos, std::uint32_t n);
WString* __fastcall WString_Erase(WString* self, void* edx, std::uint32_t pos, std::uint32_t n);
extern WStringEraseFn Original_WString_Erase;

// wstring(const wchar_t* s, const allocator& a) (0x0053ba60), ret 8,
// EAX = this.
using WStringFromWcsFn = WString*(__fastcall*)(WString* self, void* edx, const std::uint16_t* text,
                                               const std::uint8_t* allocator);
WString* __fastcall WString_FromWcs(WString* self, void* edx, const std::uint16_t* text, const std::uint8_t* allocator);
extern WStringFromWcsFn Original_WString_FromWcs;

// operator+(const wstring& left, const wchar_t* text) (0x00534e50), cdecl,
// the result object first; EAX = result.
using WStringConcatWcsFn = WString*(__cdecl*)(WString* result, const WString* left, const std::uint16_t* text);
WString* __cdecl WString_ConcatWcs(WString* result, const WString* left, const std::uint16_t* text);
extern WStringConcatWcsFn Original_WString_ConcatWcs;

// operator=(const wstring& x) (0x00523a40), ret 4, EAX = this.
using WStringAssignCopyFn = WString*(__fastcall*)(WString* self, void* edx, const WString* x);
WString* __fastcall WString_AssignCopy(WString* self, void* edx, const WString* x);
extern WStringAssignCopyFn Original_WString_AssignCopy;

}  // namespace s3re::msvc6
