#pragma once

// std::basic_string<char> as compiled into S3.EXE by MSVC 6.0 (Dinkumware STL).
//
// Game objects embed this layout directly, so code that touches game data must
// use it as-is rather than a modern std::string. The buffer is reference
// counted: the byte before _Ptr holds the count (0 = unshared, 0xFF = frozen,
// never shared). The buffer is allocated as _Res + 2 bytes: count byte,
// characters, terminating zero.

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

}  // namespace s3re::msvc6
