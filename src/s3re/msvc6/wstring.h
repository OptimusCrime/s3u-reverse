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

// erase(pos, n) (0x00428730), EAX = this.
using WStringEraseFn = WString*(__fastcall*)(WString* self, void* edx, std::uint32_t pos, std::uint32_t n);
WString* __fastcall WString_Erase(WString* self, void* edx, std::uint32_t pos, std::uint32_t n);
extern WStringEraseFn Original_WString_Erase;

}  // namespace s3re::msvc6
