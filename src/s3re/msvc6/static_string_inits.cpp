// The static initializers `static std::string name = base + "\\Text";` of
// the object files in 0x4c0000..0x560000 (compiler code; each object file
// has its own copy of the registry path strings: a base path string plus
// "\General", "\Index Server", "\Patches", ...). All of them have the same
// 94 instructions (found by comparing their code with the constants
// masked); only the three addresses differ, listed below with the text.
// Each runs once at program start from the CRT's _initterm; its destructor
// is registered by the StaticDtorReg_ function after it.
//
// StaticInit_<dest> (cdecl, no arguments): temp = left + text
//   (std::string::operator+(const string&, const char*) 0x401c70); the
//   destination string is default-constructed (allocator byte, then ptr,
//   len, res = 0; the old contents are not freed) and assigned temp
//   (assign(temp, 0, npos) 0x401d60: it shares temp's buffer); then temp's
//   destructor (inlined: erase(0, npos) with its _Split, then _Tidy(true);
//   ours calls ~string 0x401b60, the same steps).
// Known difference: the destination's allocator byte is an uninitialized
// stack byte of the original's frame; ours is 0 (the allocator is empty;
// docs/reimplementation_notes.md, case b). The emulator specs ignore it.

#include "game/tables.h"
#include "hooks.h"
#include "msvc6/string.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::msvc6 {

namespace {

constexpr std::uintptr_t kPlusChars = 0x00401c70;     // operator+(const string&, const char*), fastcall
constexpr std::uintptr_t kAssignSubstr = 0x00401d60;  // assign(const string&, pos, n)
constexpr std::uintptr_t kDestroy = 0x00401b60;       // ~string

// The body all of them share.
void ConcatInit(std::uintptr_t destination, std::uintptr_t left, std::uintptr_t text) {
    String temp;
    using PlusFn = String*(__fastcall*)(String* result, const String* left, const char* text);
    reinterpret_cast<PlusFn>(kPlusChars)(&temp, reinterpret_cast<const String*>(left),
                                         reinterpret_cast<const char*>(text));
    auto* const dest = reinterpret_cast<String*>(destination);
    dest->allocator = 0;  // (an uninitialized stack byte in the original)
    dest->ptr = nullptr;
    dest->len = 0;
    dest->res = 0;
    using AssignFn = String*(__fastcall*)(String*, void*, const String*, std::uint32_t, std::uint32_t);
    reinterpret_cast<AssignFn>(kAssignSubstr)(dest, nullptr, &temp, 0, game::g_stringNpos);
    reinterpret_cast<void(__fastcall*)(String*, void*)>(kDestroy)(&temp, nullptr);
}

}  // namespace

using StaticInitFn = void(__cdecl*)();

// Purpose (each): Initializes one of the game's static path strings (a
// registry key path) at program start. (destination, base string, text;
// raw-ok: the initializers' own data addresses.)
void __cdecl StaticInit_7aa5c0() { ConcatInit(0x7aa5c0, 0x7aa5d0, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x004c6430, StaticInit_7aa5c0, StaticInitFn, Original_StaticInit_7aa5c0, StaticInit_7aa5c0);
void __cdecl StaticInit_7aa5b0() { ConcatInit(0x7aa5b0, 0x7aa5d0, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x004c6590, StaticInit_7aa5b0, StaticInitFn, Original_StaticInit_7aa5b0, StaticInit_7aa5b0);
void __cdecl StaticInit_7aa5a0() { ConcatInit(0x7aa5a0, 0x7aa5d0, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x004c66f0, StaticInit_7aa5a0, StaticInitFn, Original_StaticInit_7aa5a0, StaticInit_7aa5a0);
void __cdecl StaticInit_7aa720() { ConcatInit(0x7aa720, 0x7aa730, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x004c7c90, StaticInit_7aa720, StaticInitFn, Original_StaticInit_7aa720, StaticInit_7aa720);
void __cdecl StaticInit_7aa710() { ConcatInit(0x7aa710, 0x7aa730, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x004c7df0, StaticInit_7aa710, StaticInitFn, Original_StaticInit_7aa710, StaticInit_7aa710);
void __cdecl StaticInit_7aa700() { ConcatInit(0x7aa700, 0x7aa730, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x004c7f50, StaticInit_7aa700, StaticInitFn, Original_StaticInit_7aa700, StaticInit_7aa700);
void __cdecl StaticInit_7aa880() { ConcatInit(0x7aa880, 0x7aa890, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x004d1d30, StaticInit_7aa880, StaticInitFn, Original_StaticInit_7aa880, StaticInit_7aa880);
void __cdecl StaticInit_7aa870() { ConcatInit(0x7aa870, 0x7aa890, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x004d1e90, StaticInit_7aa870, StaticInitFn, Original_StaticInit_7aa870, StaticInit_7aa870);
void __cdecl StaticInit_7aa860() { ConcatInit(0x7aa860, 0x7aa890, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x004d1ff0, StaticInit_7aa860, StaticInitFn, Original_StaticInit_7aa860, StaticInit_7aa860);
void __cdecl StaticInit_7aa9e0() { ConcatInit(0x7aa9e0, 0x7aa9f0, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x004d2740, StaticInit_7aa9e0, StaticInitFn, Original_StaticInit_7aa9e0, StaticInit_7aa9e0);
void __cdecl StaticInit_7aa9d0() { ConcatInit(0x7aa9d0, 0x7aa9f0, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x004d28a0, StaticInit_7aa9d0, StaticInitFn, Original_StaticInit_7aa9d0, StaticInit_7aa9d0);
void __cdecl StaticInit_7aa9c0() { ConcatInit(0x7aa9c0, 0x7aa9f0, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x004d2a00, StaticInit_7aa9c0, StaticInitFn, Original_StaticInit_7aa9c0, StaticInit_7aa9c0);
void __cdecl StaticInit_7aab40() { ConcatInit(0x7aab40, 0x7aab50, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x004d5cd0, StaticInit_7aab40, StaticInitFn, Original_StaticInit_7aab40, StaticInit_7aab40);
void __cdecl StaticInit_7aab30() { ConcatInit(0x7aab30, 0x7aab50, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x004d5e30, StaticInit_7aab30, StaticInitFn, Original_StaticInit_7aab30, StaticInit_7aab30);
void __cdecl StaticInit_7aab20() { ConcatInit(0x7aab20, 0x7aab50, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x004d5f90, StaticInit_7aab20, StaticInitFn, Original_StaticInit_7aab20, StaticInit_7aab20);
void __cdecl StaticInit_7aaca0() { ConcatInit(0x7aaca0, 0x7aacb0, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x004d8940, StaticInit_7aaca0, StaticInitFn, Original_StaticInit_7aaca0, StaticInit_7aaca0);
void __cdecl StaticInit_7aac90() { ConcatInit(0x7aac90, 0x7aacb0, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x004d8aa0, StaticInit_7aac90, StaticInitFn, Original_StaticInit_7aac90, StaticInit_7aac90);
void __cdecl StaticInit_7aac80() { ConcatInit(0x7aac80, 0x7aacb0, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x004d8c00, StaticInit_7aac80, StaticInitFn, Original_StaticInit_7aac80, StaticInit_7aac80);
void __cdecl StaticInit_7aae00() { ConcatInit(0x7aae00, 0x7aae10, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x004dfcd0, StaticInit_7aae00, StaticInitFn, Original_StaticInit_7aae00, StaticInit_7aae00);
void __cdecl StaticInit_7aadf0() { ConcatInit(0x7aadf0, 0x7aae10, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x004dfe30, StaticInit_7aadf0, StaticInitFn, Original_StaticInit_7aadf0, StaticInit_7aadf0);
void __cdecl StaticInit_7aade0() { ConcatInit(0x7aade0, 0x7aae10, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x004dff90, StaticInit_7aade0, StaticInitFn, Original_StaticInit_7aade0, StaticInit_7aade0);
void __cdecl StaticInit_7aaf60() { ConcatInit(0x7aaf60, 0x7aaf70, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x004e4b40, StaticInit_7aaf60, StaticInitFn, Original_StaticInit_7aaf60, StaticInit_7aaf60);
void __cdecl StaticInit_7aaf50() { ConcatInit(0x7aaf50, 0x7aaf70, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x004e4ca0, StaticInit_7aaf50, StaticInitFn, Original_StaticInit_7aaf50, StaticInit_7aaf50);
void __cdecl StaticInit_7aaf40() { ConcatInit(0x7aaf40, 0x7aaf70, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x004e4e00, StaticInit_7aaf40, StaticInitFn, Original_StaticInit_7aaf40, StaticInit_7aaf40);
void __cdecl StaticInit_7ab0c0() { ConcatInit(0x7ab0c0, 0x7ab0d0, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x004e6f00, StaticInit_7ab0c0, StaticInitFn, Original_StaticInit_7ab0c0, StaticInit_7ab0c0);
void __cdecl StaticInit_7ab0b0() { ConcatInit(0x7ab0b0, 0x7ab0d0, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x004e7060, StaticInit_7ab0b0, StaticInitFn, Original_StaticInit_7ab0b0, StaticInit_7ab0b0);
void __cdecl StaticInit_7ab0a0() { ConcatInit(0x7ab0a0, 0x7ab0d0, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x004e71c0, StaticInit_7ab0a0, StaticInitFn, Original_StaticInit_7ab0a0, StaticInit_7ab0a0);
void __cdecl StaticInit_7ab220() { ConcatInit(0x7ab220, 0x7ab230, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x004e8d60, StaticInit_7ab220, StaticInitFn, Original_StaticInit_7ab220, StaticInit_7ab220);
void __cdecl StaticInit_7ab210() { ConcatInit(0x7ab210, 0x7ab230, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x004e8ec0, StaticInit_7ab210, StaticInitFn, Original_StaticInit_7ab210, StaticInit_7ab210);
void __cdecl StaticInit_7ab200() { ConcatInit(0x7ab200, 0x7ab230, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x004e9020, StaticInit_7ab200, StaticInitFn, Original_StaticInit_7ab200, StaticInit_7ab200);
void __cdecl StaticInit_7ab380() { ConcatInit(0x7ab380, 0x7ab390, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x004fac10, StaticInit_7ab380, StaticInitFn, Original_StaticInit_7ab380, StaticInit_7ab380);
void __cdecl StaticInit_7ab370() { ConcatInit(0x7ab370, 0x7ab390, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x004fad70, StaticInit_7ab370, StaticInitFn, Original_StaticInit_7ab370, StaticInit_7ab370);
void __cdecl StaticInit_7ab360() { ConcatInit(0x7ab360, 0x7ab390, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x004faed0, StaticInit_7ab360, StaticInitFn, Original_StaticInit_7ab360, StaticInit_7ab360);
void __cdecl StaticInit_7ab4e0() { ConcatInit(0x7ab4e0, 0x7ab4f0, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x004fc080, StaticInit_7ab4e0, StaticInitFn, Original_StaticInit_7ab4e0, StaticInit_7ab4e0);
void __cdecl StaticInit_7ab4d0() { ConcatInit(0x7ab4d0, 0x7ab4f0, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x004fc1e0, StaticInit_7ab4d0, StaticInitFn, Original_StaticInit_7ab4d0, StaticInit_7ab4d0);
void __cdecl StaticInit_7ab4c0() { ConcatInit(0x7ab4c0, 0x7ab4f0, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x004fc340, StaticInit_7ab4c0, StaticInitFn, Original_StaticInit_7ab4c0, StaticInit_7ab4c0);
void __cdecl StaticInit_7ab640() { ConcatInit(0x7ab640, 0x7ab650, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x004fff50, StaticInit_7ab640, StaticInitFn, Original_StaticInit_7ab640, StaticInit_7ab640);
void __cdecl StaticInit_7ab630() { ConcatInit(0x7ab630, 0x7ab650, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x005000b0, StaticInit_7ab630, StaticInitFn, Original_StaticInit_7ab630, StaticInit_7ab630);
void __cdecl StaticInit_7ab620() { ConcatInit(0x7ab620, 0x7ab650, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x00500210, StaticInit_7ab620, StaticInitFn, Original_StaticInit_7ab620, StaticInit_7ab620);
void __cdecl StaticInit_7ab7a0() { ConcatInit(0x7ab7a0, 0x7ab7b0, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x00503b10, StaticInit_7ab7a0, StaticInitFn, Original_StaticInit_7ab7a0, StaticInit_7ab7a0);
void __cdecl StaticInit_7ab790() { ConcatInit(0x7ab790, 0x7ab7b0, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x00503c70, StaticInit_7ab790, StaticInitFn, Original_StaticInit_7ab790, StaticInit_7ab790);
void __cdecl StaticInit_7ab780() { ConcatInit(0x7ab780, 0x7ab7b0, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x00503dd0, StaticInit_7ab780, StaticInitFn, Original_StaticInit_7ab780, StaticInit_7ab780);
void __cdecl StaticInit_7ab900() { ConcatInit(0x7ab900, 0x7ab910, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x005063e0, StaticInit_7ab900, StaticInitFn, Original_StaticInit_7ab900, StaticInit_7ab900);
void __cdecl StaticInit_7ab8f0() { ConcatInit(0x7ab8f0, 0x7ab910, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x00506540, StaticInit_7ab8f0, StaticInitFn, Original_StaticInit_7ab8f0, StaticInit_7ab8f0);
void __cdecl StaticInit_7ab8e0() { ConcatInit(0x7ab8e0, 0x7ab910, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x005066a0, StaticInit_7ab8e0, StaticInitFn, Original_StaticInit_7ab8e0, StaticInit_7ab8e0);
void __cdecl StaticInit_7aba80() { ConcatInit(0x7aba80, 0x7aba90, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x005085f0, StaticInit_7aba80, StaticInitFn, Original_StaticInit_7aba80, StaticInit_7aba80);
void __cdecl StaticInit_7aba70() { ConcatInit(0x7aba70, 0x7aba90, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x00508750, StaticInit_7aba70, StaticInitFn, Original_StaticInit_7aba70, StaticInit_7aba70);
void __cdecl StaticInit_7aba60() { ConcatInit(0x7aba60, 0x7aba90, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x005088b0, StaticInit_7aba60, StaticInitFn, Original_StaticInit_7aba60, StaticInit_7aba60);
void __cdecl StaticInit_7abbe0() { ConcatInit(0x7abbe0, 0x7abbf0, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x0050c0b0, StaticInit_7abbe0, StaticInitFn, Original_StaticInit_7abbe0, StaticInit_7abbe0);
void __cdecl StaticInit_7abbd0() { ConcatInit(0x7abbd0, 0x7abbf0, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x0050c210, StaticInit_7abbd0, StaticInitFn, Original_StaticInit_7abbd0, StaticInit_7abbd0);
void __cdecl StaticInit_7abbc0() { ConcatInit(0x7abbc0, 0x7abbf0, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x0050c370, StaticInit_7abbc0, StaticInitFn, Original_StaticInit_7abbc0, StaticInit_7abbc0);
void __cdecl StaticInit_7abd40() { ConcatInit(0x7abd40, 0x7abd50, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x00510930, StaticInit_7abd40, StaticInitFn, Original_StaticInit_7abd40, StaticInit_7abd40);
void __cdecl StaticInit_7abd30() { ConcatInit(0x7abd30, 0x7abd50, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x00510a90, StaticInit_7abd30, StaticInitFn, Original_StaticInit_7abd30, StaticInit_7abd30);
void __cdecl StaticInit_7abd20() { ConcatInit(0x7abd20, 0x7abd50, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x00510bf0, StaticInit_7abd20, StaticInitFn, Original_StaticInit_7abd20, StaticInit_7abd20);
void __cdecl StaticInit_7abea0() { ConcatInit(0x7abea0, 0x7abeb0, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x00511340, StaticInit_7abea0, StaticInitFn, Original_StaticInit_7abea0, StaticInit_7abea0);
void __cdecl StaticInit_7abe90() { ConcatInit(0x7abe90, 0x7abeb0, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x005114a0, StaticInit_7abe90, StaticInitFn, Original_StaticInit_7abe90, StaticInit_7abe90);
void __cdecl StaticInit_7abe80() { ConcatInit(0x7abe80, 0x7abeb0, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x00511600, StaticInit_7abe80, StaticInitFn, Original_StaticInit_7abe80, StaticInit_7abe80);
void __cdecl StaticInit_7ac000() { ConcatInit(0x7ac000, 0x7ac010, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x00514ea0, StaticInit_7ac000, StaticInitFn, Original_StaticInit_7ac000, StaticInit_7ac000);
void __cdecl StaticInit_7abff0() { ConcatInit(0x7abff0, 0x7ac010, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x00515000, StaticInit_7abff0, StaticInitFn, Original_StaticInit_7abff0, StaticInit_7abff0);
void __cdecl StaticInit_7abfe0() { ConcatInit(0x7abfe0, 0x7ac010, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x00515160, StaticInit_7abfe0, StaticInitFn, Original_StaticInit_7abfe0, StaticInit_7abfe0);
void __cdecl StaticInit_7ac160() { ConcatInit(0x7ac160, 0x7ac170, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x00515c20, StaticInit_7ac160, StaticInitFn, Original_StaticInit_7ac160, StaticInit_7ac160);
void __cdecl StaticInit_7ac150() { ConcatInit(0x7ac150, 0x7ac170, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x00515d80, StaticInit_7ac150, StaticInitFn, Original_StaticInit_7ac150, StaticInit_7ac150);
void __cdecl StaticInit_7ac140() { ConcatInit(0x7ac140, 0x7ac170, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x00515ee0, StaticInit_7ac140, StaticInitFn, Original_StaticInit_7ac140, StaticInit_7ac140);
void __cdecl StaticInit_7ac2c0() { ConcatInit(0x7ac2c0, 0x7ac2d0, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x0051d850, StaticInit_7ac2c0, StaticInitFn, Original_StaticInit_7ac2c0, StaticInit_7ac2c0);
void __cdecl StaticInit_7ac2b0() { ConcatInit(0x7ac2b0, 0x7ac2d0, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x0051d9b0, StaticInit_7ac2b0, StaticInitFn, Original_StaticInit_7ac2b0, StaticInit_7ac2b0);
void __cdecl StaticInit_7ac2a0() { ConcatInit(0x7ac2a0, 0x7ac2d0, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x0051db10, StaticInit_7ac2a0, StaticInitFn, Original_StaticInit_7ac2a0, StaticInit_7ac2a0);
void __cdecl StaticInit_7ac420() { ConcatInit(0x7ac420, 0x7ac430, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x0051e7a0, StaticInit_7ac420, StaticInitFn, Original_StaticInit_7ac420, StaticInit_7ac420);
void __cdecl StaticInit_7ac410() { ConcatInit(0x7ac410, 0x7ac430, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x0051e900, StaticInit_7ac410, StaticInitFn, Original_StaticInit_7ac410, StaticInit_7ac410);
void __cdecl StaticInit_7ac400() { ConcatInit(0x7ac400, 0x7ac430, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x0051ea60, StaticInit_7ac400, StaticInitFn, Original_StaticInit_7ac400, StaticInit_7ac400);
void __cdecl StaticInit_7ac580() { ConcatInit(0x7ac580, 0x7ac590, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x00523c80, StaticInit_7ac580, StaticInitFn, Original_StaticInit_7ac580, StaticInit_7ac580);
void __cdecl StaticInit_7ac570() { ConcatInit(0x7ac570, 0x7ac590, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x00523de0, StaticInit_7ac570, StaticInitFn, Original_StaticInit_7ac570, StaticInit_7ac570);
void __cdecl StaticInit_7ac560() { ConcatInit(0x7ac560, 0x7ac590, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x00523f40, StaticInit_7ac560, StaticInitFn, Original_StaticInit_7ac560, StaticInit_7ac560);
void __cdecl StaticInit_7ac6e0() { ConcatInit(0x7ac6e0, 0x7ac6f0, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x00524690, StaticInit_7ac6e0, StaticInitFn, Original_StaticInit_7ac6e0, StaticInit_7ac6e0);
void __cdecl StaticInit_7ac6d0() { ConcatInit(0x7ac6d0, 0x7ac6f0, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x005247f0, StaticInit_7ac6d0, StaticInitFn, Original_StaticInit_7ac6d0, StaticInit_7ac6d0);
void __cdecl StaticInit_7ac6c0() { ConcatInit(0x7ac6c0, 0x7ac6f0, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x00524950, StaticInit_7ac6c0, StaticInitFn, Original_StaticInit_7ac6c0, StaticInit_7ac6c0);
void __cdecl StaticInit_7ac840() { ConcatInit(0x7ac840, 0x7ac850, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x00535080, StaticInit_7ac840, StaticInitFn, Original_StaticInit_7ac840, StaticInit_7ac840);
void __cdecl StaticInit_7ac830() { ConcatInit(0x7ac830, 0x7ac850, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x005351e0, StaticInit_7ac830, StaticInitFn, Original_StaticInit_7ac830, StaticInit_7ac830);
void __cdecl StaticInit_7ac820() { ConcatInit(0x7ac820, 0x7ac850, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x00535340, StaticInit_7ac820, StaticInitFn, Original_StaticInit_7ac820, StaticInit_7ac820);
void __cdecl StaticInit_7ac9c0() { ConcatInit(0x7ac9c0, 0x7ac9d0, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x0053bc30, StaticInit_7ac9c0, StaticInitFn, Original_StaticInit_7ac9c0, StaticInit_7ac9c0);
void __cdecl StaticInit_7ac9b0() { ConcatInit(0x7ac9b0, 0x7ac9d0, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x0053bd90, StaticInit_7ac9b0, StaticInitFn, Original_StaticInit_7ac9b0, StaticInit_7ac9b0);
void __cdecl StaticInit_7ac9a0() { ConcatInit(0x7ac9a0, 0x7ac9d0, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x0053bef0, StaticInit_7ac9a0, StaticInitFn, Original_StaticInit_7ac9a0, StaticInit_7ac9a0);
void __cdecl StaticInit_7acb40() { ConcatInit(0x7acb40, 0x7acb50, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x0053eb80, StaticInit_7acb40, StaticInitFn, Original_StaticInit_7acb40, StaticInit_7acb40);
void __cdecl StaticInit_7acb30() { ConcatInit(0x7acb30, 0x7acb50, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x0053ece0, StaticInit_7acb30, StaticInitFn, Original_StaticInit_7acb30, StaticInit_7acb30);
void __cdecl StaticInit_7acb20() { ConcatInit(0x7acb20, 0x7acb50, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x0053ee40, StaticInit_7acb20, StaticInitFn, Original_StaticInit_7acb20, StaticInit_7acb20);
void __cdecl StaticInit_7accc0() { ConcatInit(0x7accc0, 0x7accd0, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x00541c80, StaticInit_7accc0, StaticInitFn, Original_StaticInit_7accc0, StaticInit_7accc0);
void __cdecl StaticInit_7accb0() { ConcatInit(0x7accb0, 0x7accd0, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x00541de0, StaticInit_7accb0, StaticInitFn, Original_StaticInit_7accb0, StaticInit_7accb0);
void __cdecl StaticInit_7acca0() { ConcatInit(0x7acca0, 0x7accd0, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x00541f40, StaticInit_7acca0, StaticInitFn, Original_StaticInit_7acca0, StaticInit_7acca0);
void __cdecl StaticInit_7ace20() { ConcatInit(0x7ace20, 0x7ace30, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x00542ea0, StaticInit_7ace20, StaticInitFn, Original_StaticInit_7ace20, StaticInit_7ace20);
void __cdecl StaticInit_7ace10() { ConcatInit(0x7ace10, 0x7ace30, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x00543000, StaticInit_7ace10, StaticInitFn, Original_StaticInit_7ace10, StaticInit_7ace10);
void __cdecl StaticInit_7ace00() { ConcatInit(0x7ace00, 0x7ace30, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x00543160, StaticInit_7ace00, StaticInitFn, Original_StaticInit_7ace00, StaticInit_7ace00);
void __cdecl StaticInit_7dfce8() { ConcatInit(0x7dfce8, 0x7dfcf8, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x00546030, StaticInit_7dfce8, StaticInitFn, Original_StaticInit_7dfce8, StaticInit_7dfce8);
void __cdecl StaticInit_7dfcd8() { ConcatInit(0x7dfcd8, 0x7dfcf8, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x00546190, StaticInit_7dfcd8, StaticInitFn, Original_StaticInit_7dfcd8, StaticInit_7dfcd8);
void __cdecl StaticInit_7dfcc8() { ConcatInit(0x7dfcc8, 0x7dfcf8, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x005462f0, StaticInit_7dfcc8, StaticInitFn, Original_StaticInit_7dfcc8, StaticInit_7dfcc8);
void __cdecl StaticInit_7dfed8() { ConcatInit(0x7dfed8, 0x7dfee8, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x00546bd0, StaticInit_7dfed8, StaticInitFn, Original_StaticInit_7dfed8, StaticInit_7dfed8);
void __cdecl StaticInit_7dfec8() { ConcatInit(0x7dfec8, 0x7dfee8, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x00546d30, StaticInit_7dfec8, StaticInitFn, Original_StaticInit_7dfec8, StaticInit_7dfec8);
void __cdecl StaticInit_7dfeb8() { ConcatInit(0x7dfeb8, 0x7dfee8, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x00546e90, StaticInit_7dfeb8, StaticInitFn, Original_StaticInit_7dfeb8, StaticInit_7dfeb8);
void __cdecl StaticInit_7e0038() { ConcatInit(0x7e0038, 0x7e0048, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x0054b420, StaticInit_7e0038, StaticInitFn, Original_StaticInit_7e0038, StaticInit_7e0038);
void __cdecl StaticInit_7e0028() { ConcatInit(0x7e0028, 0x7e0048, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x0054b580, StaticInit_7e0028, StaticInitFn, Original_StaticInit_7e0028, StaticInit_7e0028);
void __cdecl StaticInit_7e0018() { ConcatInit(0x7e0018, 0x7e0048, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x0054b6e0, StaticInit_7e0018, StaticInitFn, Original_StaticInit_7e0018, StaticInit_7e0018);
void __cdecl StaticInit_7e02b0() { ConcatInit(0x7e02b0, 0x7e02c0, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x0054e8c0, StaticInit_7e02b0, StaticInitFn, Original_StaticInit_7e02b0, StaticInit_7e02b0);
void __cdecl StaticInit_7e02a0() { ConcatInit(0x7e02a0, 0x7e02c0, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x0054ea20, StaticInit_7e02a0, StaticInitFn, Original_StaticInit_7e02a0, StaticInit_7e02a0);
void __cdecl StaticInit_7e0290() { ConcatInit(0x7e0290, 0x7e02c0, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x0054eb80, StaticInit_7e0290, StaticInitFn, Original_StaticInit_7e0290, StaticInit_7e0290);
void __cdecl StaticInit_7e0528() { ConcatInit(0x7e0528, 0x7e0538, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x00551cf0, StaticInit_7e0528, StaticInitFn, Original_StaticInit_7e0528, StaticInit_7e0528);
void __cdecl StaticInit_7e0518() { ConcatInit(0x7e0518, 0x7e0538, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x00551e50, StaticInit_7e0518, StaticInitFn, Original_StaticInit_7e0518, StaticInit_7e0518);
void __cdecl StaticInit_7e0508() { ConcatInit(0x7e0508, 0x7e0538, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x00551fb0, StaticInit_7e0508, StaticInitFn, Original_StaticInit_7e0508, StaticInit_7e0508);
void __cdecl StaticInit_7e06a0() { ConcatInit(0x7e06a0, 0x7e06b0, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x0055ada0, StaticInit_7e06a0, StaticInitFn, Original_StaticInit_7e06a0, StaticInit_7e06a0);
void __cdecl StaticInit_7e0690() { ConcatInit(0x7e0690, 0x7e06b0, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x0055af00, StaticInit_7e0690, StaticInitFn, Original_StaticInit_7e0690, StaticInit_7e0690);
void __cdecl StaticInit_7e0680() { ConcatInit(0x7e0680, 0x7e06b0, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x0055b060, StaticInit_7e0680, StaticInitFn, Original_StaticInit_7e0680, StaticInit_7e0680);
void __cdecl StaticInit_7e0808() { ConcatInit(0x7e0808, 0x7e0818, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x0055c150, StaticInit_7e0808, StaticInitFn, Original_StaticInit_7e0808, StaticInit_7e0808);
void __cdecl StaticInit_7e07f8() { ConcatInit(0x7e07f8, 0x7e0818, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x0055c2b0, StaticInit_7e07f8, StaticInitFn, Original_StaticInit_7e07f8, StaticInit_7e07f8);
void __cdecl StaticInit_7e07e8() { ConcatInit(0x7e07e8, 0x7e0818, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x0055c410, StaticInit_7e07e8, StaticInitFn, Original_StaticInit_7e07e8, StaticInit_7e07e8);
void __cdecl StaticInit_7e0a78() { ConcatInit(0x7e0a78, 0x7e0a88, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x0055cba0, StaticInit_7e0a78, StaticInitFn, Original_StaticInit_7e0a78, StaticInit_7e0a78);
void __cdecl StaticInit_7e0a68() { ConcatInit(0x7e0a68, 0x7e0a88, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x0055cd00, StaticInit_7e0a68, StaticInitFn, Original_StaticInit_7e0a68, StaticInit_7e0a68);
void __cdecl StaticInit_7e0a58() { ConcatInit(0x7e0a58, 0x7e0a88, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x0055ce60, StaticInit_7e0a58, StaticInitFn, Original_StaticInit_7e0a58, StaticInit_7e0a58);
void __cdecl StaticInit_7e0ce8() { ConcatInit(0x7e0ce8, 0x7e0cf8, 0x6959f4); }  // "\General" (raw-ok)
S3RE_HOOK(0x0055ed90, StaticInit_7e0ce8, StaticInitFn, Original_StaticInit_7e0ce8, StaticInit_7e0ce8);
void __cdecl StaticInit_7e0cd8() { ConcatInit(0x7e0cd8, 0x7e0cf8, 0x695a00); }  // "\Index Server" (raw-ok)
S3RE_HOOK(0x0055eef0, StaticInit_7e0cd8, StaticInitFn, Original_StaticInit_7e0cd8, StaticInit_7e0cd8);
void __cdecl StaticInit_7e0cc8() { ConcatInit(0x7e0cc8, 0x7e0cf8, 0x695a10); }  // "\Patches" (raw-ok)
S3RE_HOOK(0x0055f050, StaticInit_7e0cc8, StaticInitFn, Original_StaticInit_7e0cc8, StaticInit_7e0cc8);


// ---------------------------------------------------------------------------
// Tests: every initializer run as original and as ours, on the real strings
// (each leaves a new buffer behind and doesn't free the old one, so running
// them again is harmless); compared: the destination's length, capacity,
// text and count byte (not the allocator byte).
// ---------------------------------------------------------------------------

namespace {

struct InitEntry {
    const char* name;
    std::uintptr_t destination;
    StaticInitFn* original;
    StaticInitFn ours;
};

const InitEntry kInits[] = {
    {"StaticInit_7aa5c0", 0x7aa5c0, &Original_StaticInit_7aa5c0, &StaticInit_7aa5c0},  // raw-ok
    {"StaticInit_7aa5b0", 0x7aa5b0, &Original_StaticInit_7aa5b0, &StaticInit_7aa5b0},  // raw-ok
    {"StaticInit_7aa5a0", 0x7aa5a0, &Original_StaticInit_7aa5a0, &StaticInit_7aa5a0},  // raw-ok
    {"StaticInit_7aa720", 0x7aa720, &Original_StaticInit_7aa720, &StaticInit_7aa720},  // raw-ok
    {"StaticInit_7aa710", 0x7aa710, &Original_StaticInit_7aa710, &StaticInit_7aa710},  // raw-ok
    {"StaticInit_7aa700", 0x7aa700, &Original_StaticInit_7aa700, &StaticInit_7aa700},  // raw-ok
    {"StaticInit_7aa880", 0x7aa880, &Original_StaticInit_7aa880, &StaticInit_7aa880},  // raw-ok
    {"StaticInit_7aa870", 0x7aa870, &Original_StaticInit_7aa870, &StaticInit_7aa870},  // raw-ok
    {"StaticInit_7aa860", 0x7aa860, &Original_StaticInit_7aa860, &StaticInit_7aa860},  // raw-ok
    {"StaticInit_7aa9e0", 0x7aa9e0, &Original_StaticInit_7aa9e0, &StaticInit_7aa9e0},  // raw-ok
    {"StaticInit_7aa9d0", 0x7aa9d0, &Original_StaticInit_7aa9d0, &StaticInit_7aa9d0},  // raw-ok
    {"StaticInit_7aa9c0", 0x7aa9c0, &Original_StaticInit_7aa9c0, &StaticInit_7aa9c0},  // raw-ok
    {"StaticInit_7aab40", 0x7aab40, &Original_StaticInit_7aab40, &StaticInit_7aab40},  // raw-ok
    {"StaticInit_7aab30", 0x7aab30, &Original_StaticInit_7aab30, &StaticInit_7aab30},  // raw-ok
    {"StaticInit_7aab20", 0x7aab20, &Original_StaticInit_7aab20, &StaticInit_7aab20},  // raw-ok
    {"StaticInit_7aaca0", 0x7aaca0, &Original_StaticInit_7aaca0, &StaticInit_7aaca0},  // raw-ok
    {"StaticInit_7aac90", 0x7aac90, &Original_StaticInit_7aac90, &StaticInit_7aac90},  // raw-ok
    {"StaticInit_7aac80", 0x7aac80, &Original_StaticInit_7aac80, &StaticInit_7aac80},  // raw-ok
    {"StaticInit_7aae00", 0x7aae00, &Original_StaticInit_7aae00, &StaticInit_7aae00},  // raw-ok
    {"StaticInit_7aadf0", 0x7aadf0, &Original_StaticInit_7aadf0, &StaticInit_7aadf0},  // raw-ok
    {"StaticInit_7aade0", 0x7aade0, &Original_StaticInit_7aade0, &StaticInit_7aade0},  // raw-ok
    {"StaticInit_7aaf60", 0x7aaf60, &Original_StaticInit_7aaf60, &StaticInit_7aaf60},  // raw-ok
    {"StaticInit_7aaf50", 0x7aaf50, &Original_StaticInit_7aaf50, &StaticInit_7aaf50},  // raw-ok
    {"StaticInit_7aaf40", 0x7aaf40, &Original_StaticInit_7aaf40, &StaticInit_7aaf40},  // raw-ok
    {"StaticInit_7ab0c0", 0x7ab0c0, &Original_StaticInit_7ab0c0, &StaticInit_7ab0c0},  // raw-ok
    {"StaticInit_7ab0b0", 0x7ab0b0, &Original_StaticInit_7ab0b0, &StaticInit_7ab0b0},  // raw-ok
    {"StaticInit_7ab0a0", 0x7ab0a0, &Original_StaticInit_7ab0a0, &StaticInit_7ab0a0},  // raw-ok
    {"StaticInit_7ab220", 0x7ab220, &Original_StaticInit_7ab220, &StaticInit_7ab220},  // raw-ok
    {"StaticInit_7ab210", 0x7ab210, &Original_StaticInit_7ab210, &StaticInit_7ab210},  // raw-ok
    {"StaticInit_7ab200", 0x7ab200, &Original_StaticInit_7ab200, &StaticInit_7ab200},  // raw-ok
    {"StaticInit_7ab380", 0x7ab380, &Original_StaticInit_7ab380, &StaticInit_7ab380},  // raw-ok
    {"StaticInit_7ab370", 0x7ab370, &Original_StaticInit_7ab370, &StaticInit_7ab370},  // raw-ok
    {"StaticInit_7ab360", 0x7ab360, &Original_StaticInit_7ab360, &StaticInit_7ab360},  // raw-ok
    {"StaticInit_7ab4e0", 0x7ab4e0, &Original_StaticInit_7ab4e0, &StaticInit_7ab4e0},  // raw-ok
    {"StaticInit_7ab4d0", 0x7ab4d0, &Original_StaticInit_7ab4d0, &StaticInit_7ab4d0},  // raw-ok
    {"StaticInit_7ab4c0", 0x7ab4c0, &Original_StaticInit_7ab4c0, &StaticInit_7ab4c0},  // raw-ok
    {"StaticInit_7ab640", 0x7ab640, &Original_StaticInit_7ab640, &StaticInit_7ab640},  // raw-ok
    {"StaticInit_7ab630", 0x7ab630, &Original_StaticInit_7ab630, &StaticInit_7ab630},  // raw-ok
    {"StaticInit_7ab620", 0x7ab620, &Original_StaticInit_7ab620, &StaticInit_7ab620},  // raw-ok
    {"StaticInit_7ab7a0", 0x7ab7a0, &Original_StaticInit_7ab7a0, &StaticInit_7ab7a0},  // raw-ok
    {"StaticInit_7ab790", 0x7ab790, &Original_StaticInit_7ab790, &StaticInit_7ab790},  // raw-ok
    {"StaticInit_7ab780", 0x7ab780, &Original_StaticInit_7ab780, &StaticInit_7ab780},  // raw-ok
    {"StaticInit_7ab900", 0x7ab900, &Original_StaticInit_7ab900, &StaticInit_7ab900},  // raw-ok
    {"StaticInit_7ab8f0", 0x7ab8f0, &Original_StaticInit_7ab8f0, &StaticInit_7ab8f0},  // raw-ok
    {"StaticInit_7ab8e0", 0x7ab8e0, &Original_StaticInit_7ab8e0, &StaticInit_7ab8e0},  // raw-ok
    {"StaticInit_7aba80", 0x7aba80, &Original_StaticInit_7aba80, &StaticInit_7aba80},  // raw-ok
    {"StaticInit_7aba70", 0x7aba70, &Original_StaticInit_7aba70, &StaticInit_7aba70},  // raw-ok
    {"StaticInit_7aba60", 0x7aba60, &Original_StaticInit_7aba60, &StaticInit_7aba60},  // raw-ok
    {"StaticInit_7abbe0", 0x7abbe0, &Original_StaticInit_7abbe0, &StaticInit_7abbe0},  // raw-ok
    {"StaticInit_7abbd0", 0x7abbd0, &Original_StaticInit_7abbd0, &StaticInit_7abbd0},  // raw-ok
    {"StaticInit_7abbc0", 0x7abbc0, &Original_StaticInit_7abbc0, &StaticInit_7abbc0},  // raw-ok
    {"StaticInit_7abd40", 0x7abd40, &Original_StaticInit_7abd40, &StaticInit_7abd40},  // raw-ok
    {"StaticInit_7abd30", 0x7abd30, &Original_StaticInit_7abd30, &StaticInit_7abd30},  // raw-ok
    {"StaticInit_7abd20", 0x7abd20, &Original_StaticInit_7abd20, &StaticInit_7abd20},  // raw-ok
    {"StaticInit_7abea0", 0x7abea0, &Original_StaticInit_7abea0, &StaticInit_7abea0},  // raw-ok
    {"StaticInit_7abe90", 0x7abe90, &Original_StaticInit_7abe90, &StaticInit_7abe90},  // raw-ok
    {"StaticInit_7abe80", 0x7abe80, &Original_StaticInit_7abe80, &StaticInit_7abe80},  // raw-ok
    {"StaticInit_7ac000", 0x7ac000, &Original_StaticInit_7ac000, &StaticInit_7ac000},  // raw-ok
    {"StaticInit_7abff0", 0x7abff0, &Original_StaticInit_7abff0, &StaticInit_7abff0},  // raw-ok
    {"StaticInit_7abfe0", 0x7abfe0, &Original_StaticInit_7abfe0, &StaticInit_7abfe0},  // raw-ok
    {"StaticInit_7ac160", 0x7ac160, &Original_StaticInit_7ac160, &StaticInit_7ac160},  // raw-ok
    {"StaticInit_7ac150", 0x7ac150, &Original_StaticInit_7ac150, &StaticInit_7ac150},  // raw-ok
    {"StaticInit_7ac140", 0x7ac140, &Original_StaticInit_7ac140, &StaticInit_7ac140},  // raw-ok
    {"StaticInit_7ac2c0", 0x7ac2c0, &Original_StaticInit_7ac2c0, &StaticInit_7ac2c0},  // raw-ok
    {"StaticInit_7ac2b0", 0x7ac2b0, &Original_StaticInit_7ac2b0, &StaticInit_7ac2b0},  // raw-ok
    {"StaticInit_7ac2a0", 0x7ac2a0, &Original_StaticInit_7ac2a0, &StaticInit_7ac2a0},  // raw-ok
    {"StaticInit_7ac420", 0x7ac420, &Original_StaticInit_7ac420, &StaticInit_7ac420},  // raw-ok
    {"StaticInit_7ac410", 0x7ac410, &Original_StaticInit_7ac410, &StaticInit_7ac410},  // raw-ok
    {"StaticInit_7ac400", 0x7ac400, &Original_StaticInit_7ac400, &StaticInit_7ac400},  // raw-ok
    {"StaticInit_7ac580", 0x7ac580, &Original_StaticInit_7ac580, &StaticInit_7ac580},  // raw-ok
    {"StaticInit_7ac570", 0x7ac570, &Original_StaticInit_7ac570, &StaticInit_7ac570},  // raw-ok
    {"StaticInit_7ac560", 0x7ac560, &Original_StaticInit_7ac560, &StaticInit_7ac560},  // raw-ok
    {"StaticInit_7ac6e0", 0x7ac6e0, &Original_StaticInit_7ac6e0, &StaticInit_7ac6e0},  // raw-ok
    {"StaticInit_7ac6d0", 0x7ac6d0, &Original_StaticInit_7ac6d0, &StaticInit_7ac6d0},  // raw-ok
    {"StaticInit_7ac6c0", 0x7ac6c0, &Original_StaticInit_7ac6c0, &StaticInit_7ac6c0},  // raw-ok
    {"StaticInit_7ac840", 0x7ac840, &Original_StaticInit_7ac840, &StaticInit_7ac840},  // raw-ok
    {"StaticInit_7ac830", 0x7ac830, &Original_StaticInit_7ac830, &StaticInit_7ac830},  // raw-ok
    {"StaticInit_7ac820", 0x7ac820, &Original_StaticInit_7ac820, &StaticInit_7ac820},  // raw-ok
    {"StaticInit_7ac9c0", 0x7ac9c0, &Original_StaticInit_7ac9c0, &StaticInit_7ac9c0},  // raw-ok
    {"StaticInit_7ac9b0", 0x7ac9b0, &Original_StaticInit_7ac9b0, &StaticInit_7ac9b0},  // raw-ok
    {"StaticInit_7ac9a0", 0x7ac9a0, &Original_StaticInit_7ac9a0, &StaticInit_7ac9a0},  // raw-ok
    {"StaticInit_7acb40", 0x7acb40, &Original_StaticInit_7acb40, &StaticInit_7acb40},  // raw-ok
    {"StaticInit_7acb30", 0x7acb30, &Original_StaticInit_7acb30, &StaticInit_7acb30},  // raw-ok
    {"StaticInit_7acb20", 0x7acb20, &Original_StaticInit_7acb20, &StaticInit_7acb20},  // raw-ok
    {"StaticInit_7accc0", 0x7accc0, &Original_StaticInit_7accc0, &StaticInit_7accc0},  // raw-ok
    {"StaticInit_7accb0", 0x7accb0, &Original_StaticInit_7accb0, &StaticInit_7accb0},  // raw-ok
    {"StaticInit_7acca0", 0x7acca0, &Original_StaticInit_7acca0, &StaticInit_7acca0},  // raw-ok
    {"StaticInit_7ace20", 0x7ace20, &Original_StaticInit_7ace20, &StaticInit_7ace20},  // raw-ok
    {"StaticInit_7ace10", 0x7ace10, &Original_StaticInit_7ace10, &StaticInit_7ace10},  // raw-ok
    {"StaticInit_7ace00", 0x7ace00, &Original_StaticInit_7ace00, &StaticInit_7ace00},  // raw-ok
    {"StaticInit_7dfce8", 0x7dfce8, &Original_StaticInit_7dfce8, &StaticInit_7dfce8},  // raw-ok
    {"StaticInit_7dfcd8", 0x7dfcd8, &Original_StaticInit_7dfcd8, &StaticInit_7dfcd8},  // raw-ok
    {"StaticInit_7dfcc8", 0x7dfcc8, &Original_StaticInit_7dfcc8, &StaticInit_7dfcc8},  // raw-ok
    {"StaticInit_7dfed8", 0x7dfed8, &Original_StaticInit_7dfed8, &StaticInit_7dfed8},  // raw-ok
    {"StaticInit_7dfec8", 0x7dfec8, &Original_StaticInit_7dfec8, &StaticInit_7dfec8},  // raw-ok
    {"StaticInit_7dfeb8", 0x7dfeb8, &Original_StaticInit_7dfeb8, &StaticInit_7dfeb8},  // raw-ok
    {"StaticInit_7e0038", 0x7e0038, &Original_StaticInit_7e0038, &StaticInit_7e0038},  // raw-ok
    {"StaticInit_7e0028", 0x7e0028, &Original_StaticInit_7e0028, &StaticInit_7e0028},  // raw-ok
    {"StaticInit_7e0018", 0x7e0018, &Original_StaticInit_7e0018, &StaticInit_7e0018},  // raw-ok
    {"StaticInit_7e02b0", 0x7e02b0, &Original_StaticInit_7e02b0, &StaticInit_7e02b0},  // raw-ok
    {"StaticInit_7e02a0", 0x7e02a0, &Original_StaticInit_7e02a0, &StaticInit_7e02a0},  // raw-ok
    {"StaticInit_7e0290", 0x7e0290, &Original_StaticInit_7e0290, &StaticInit_7e0290},  // raw-ok
    {"StaticInit_7e0528", 0x7e0528, &Original_StaticInit_7e0528, &StaticInit_7e0528},  // raw-ok
    {"StaticInit_7e0518", 0x7e0518, &Original_StaticInit_7e0518, &StaticInit_7e0518},  // raw-ok
    {"StaticInit_7e0508", 0x7e0508, &Original_StaticInit_7e0508, &StaticInit_7e0508},  // raw-ok
    {"StaticInit_7e06a0", 0x7e06a0, &Original_StaticInit_7e06a0, &StaticInit_7e06a0},  // raw-ok
    {"StaticInit_7e0690", 0x7e0690, &Original_StaticInit_7e0690, &StaticInit_7e0690},  // raw-ok
    {"StaticInit_7e0680", 0x7e0680, &Original_StaticInit_7e0680, &StaticInit_7e0680},  // raw-ok
    {"StaticInit_7e0808", 0x7e0808, &Original_StaticInit_7e0808, &StaticInit_7e0808},  // raw-ok
    {"StaticInit_7e07f8", 0x7e07f8, &Original_StaticInit_7e07f8, &StaticInit_7e07f8},  // raw-ok
    {"StaticInit_7e07e8", 0x7e07e8, &Original_StaticInit_7e07e8, &StaticInit_7e07e8},  // raw-ok
    {"StaticInit_7e0a78", 0x7e0a78, &Original_StaticInit_7e0a78, &StaticInit_7e0a78},  // raw-ok
    {"StaticInit_7e0a68", 0x7e0a68, &Original_StaticInit_7e0a68, &StaticInit_7e0a68},  // raw-ok
    {"StaticInit_7e0a58", 0x7e0a58, &Original_StaticInit_7e0a58, &StaticInit_7e0a58},  // raw-ok
    {"StaticInit_7e0ce8", 0x7e0ce8, &Original_StaticInit_7e0ce8, &StaticInit_7e0ce8},  // raw-ok
    {"StaticInit_7e0cd8", 0x7e0cd8, &Original_StaticInit_7e0cd8, &StaticInit_7e0cd8},  // raw-ok
    {"StaticInit_7e0cc8", 0x7e0cc8, &Original_StaticInit_7e0cc8, &StaticInit_7e0cc8},  // raw-ok
};

std::vector<std::uint8_t> Contents(std::uintptr_t destination) {
    const auto* s = reinterpret_cast<const String*>(destination);
    std::vector<std::uint8_t> out(12);
    std::memcpy(out.data(), &s->len, 4);
    std::memcpy(out.data() + 4, &s->res, 4);
    out[8] = s->ptr == nullptr ? 1 : 0;
    if (s->ptr != nullptr) {
        out[9] = static_cast<std::uint8_t>(s->ptr[-1]);
        out.insert(out.end(), s->ptr, s->ptr + s->len + 1);
    }
    return out;
}

void CompareInits(test::Context& ctx, StaticInitFn wrong) {
    for (const InitEntry& entry : kInits) {
        (*entry.original)();
        const std::vector<std::uint8_t> expected = Contents(entry.destination);
        (wrong != nullptr ? wrong : entry.ours)();
        const std::vector<std::uint8_t> actual = Contents(entry.destination);
        if (!S3RE_EXPECT(expected == actual)) {
            ctx.Fail(__FILE__, __LINE__, "  %s", entry.name);
            return;
        }
    }
}

// Wrong: the destination loses its last character.
void __cdecl WrongInit() {
    const InitEntry& first = kInits[0];
    (*first.original)();
    auto* dest = reinterpret_cast<String*>(first.destination);
    if (dest->len > 0) {
        dest->len = dest->len - 1;
    }
}

}  // namespace

S3RE_TEST(StaticConcatInits_MatchOriginal) {
    CompareInits(ctx, nullptr);
}

S3RE_TEST(StaticConcatInits_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareInits(probe, &WrongInit);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::msvc6
