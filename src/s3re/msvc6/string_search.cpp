// More out-of-line members of the MSVC 6.0 std::string (string.h): c_str,
// find and the comparison with C text, compiled into Dirk\Console.cpp and
// Dirk\DGlobals.cpp's units.
//
// std::string::c_str (0x00411fc0): thiscall: the buffer, or the static
//   empty string 0x6553a0 when there is none.
// std::string::find(const char* s, pos, n) (0x00409b40): thiscall, ret 0xc:
//   n == 0: pos when pos <= length, else npos. Otherwise, when pos < length
//   and n <= length - pos: candidates for the first character are found
//   with the CRT's memchr 0x61f3b0 (in the length - pos - n + 1 positions
//   left), each compared with s over n bytes; EAX = the first match's
//   position, or npos (read from the static 0x655390).
// operator!=(const string&, const char*) (0x00413ad0): __fastcall (ECX =
//   the string, EDX = the text, null meaning ""): EAX = 1 when they differ
//   (compare(0, length, text) != 0: the common prefix byte by byte, then the
//   lengths), else 0.

#include "msvc6/string.h"

#include "game/texts.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::msvc6 {

namespace {

constexpr std::uintptr_t kMemchr = 0x0061f3b0;  // the CRT's memchr (cdecl)

using MemchrFn = const void*(__cdecl*)(const void* buffer, int c, std::uint32_t count);

}  // namespace

// Purpose: Standard string c_str(): the text, or "" for a string without a
// buffer.
const char* __fastcall String_CStr(const String* self, void*) {
    return detail::CStr(self);
}

// Purpose: Standard string find(text, pos, n): where the first n characters
// of `s` first occur at or after `pos` (npos if nowhere); the command-line
// parser looks for its switches with it.
std::uint32_t __fastcall String_Find(const String* self, void*, const char* s, std::uint32_t pos, std::uint32_t n) {
    if (n == 0 && pos <= self->len) {
        return pos;
    }
    const std::uint32_t length = self->len;
    if (pos < length && n <= length - pos) {
        std::uint32_t left = length - pos - n + 1;  // candidate positions
        const char* at = self->ptr + pos;
        for (;;) {
            const char* found = static_cast<const char*>(
                reinterpret_cast<MemchrFn>(kMemchr)(at, static_cast<signed char>(s[0]), left));
            if (found == nullptr) {
                break;
            }
            if (std::memcmp(found, s, n) == 0) {
                return static_cast<std::uint32_t>(found - self->ptr);
            }
            left = left - static_cast<std::uint32_t>(found - at) - 1;
            at = found + 1;
        }
    }
    return detail::Npos();
}

// Purpose: Standard string comparison with C text (null counts as ""):
// whether they differ.
std::uint32_t __fastcall String_NotEqualText(const String* self, const char* text) {
    if (text == nullptr) {
        text = reinterpret_cast<const char*>(game::texts::kEmpty.address());  // "" (0x7a7868)
    }
    const std::uint32_t textLength = detail::Length(text);
    const std::uint32_t common = self->len < textLength ? self->len : textLength;
    int order = std::memcmp(self->ptr, text, common);
    if (order == 0) {
        order = self->len < textLength ? -1 : (self->len != textLength ? 1 : 0);
    }
    return order != 0 ? 1 : 0;
}

using CStrFn = const char*(__fastcall*)(const String*, void*);
using FindFn = std::uint32_t(__fastcall*)(const String*, void*, const char*, std::uint32_t, std::uint32_t);
using NotEqualTextFn = std::uint32_t(__fastcall*)(const String*, const char*);

S3RE_HOOK(0x00411fc0, String_CStr, CStrFn, Original_String_CStr, String_CStr);
S3RE_HOOK(0x00409b40, String_Find, FindFn, Original_String_Find, String_Find);
S3RE_HOOK(0x00413ad0, String_NotEqualText, NotEqualTextFn, Original_String_NotEqualText, String_NotEqualText);

// ---------------------------------------------------------------------------
// Tests: random strings over a small alphabet (so that find hits often),
// with and without buffers; texts and patterns from the same alphabet.
// Compared: EAX (c_str as the same pointer).
// ---------------------------------------------------------------------------

namespace {

struct Functions {
    CStrFn cStr;
    FindFn find;
    NotEqualTextFn notEqual;
};

void RandomText(test::Rng& rng, std::vector<char>& out, std::uint32_t maxLength) {
    const std::uint32_t length = rng.Below(maxLength + 1);
    out.assign(length + 1, 0);
    for (std::uint32_t i = 0; i < length; ++i) {
        out[i] = "abc\x80\xff"[rng.Below(5)];
    }
}

void CompareStrings(test::Context& ctx, const Functions& ours, int cases) {
    std::vector<char> buffer, text;
    for (int i = 0; i < cases; ++i) {
        RandomText(ctx.rng(), buffer, 24);
        String string{};
        const bool none = buffer.size() == 1 && ctx.rng().Chance(1, 2);
        string.ptr = none ? nullptr : buffer.data();
        string.len = static_cast<std::uint32_t>(buffer.size() - 1);
        string.res = string.len;
        RandomText(ctx.rng(), text, 6);
        if (ctx.rng().Chance(1, 3) && string.len > 0) {  // a pattern from the string
            const std::uint32_t at = ctx.rng().Below(string.len);
            const std::uint32_t n = 1 + ctx.rng().Below(string.len - at);
            text.assign(buffer.begin() + at, buffer.begin() + at + n);
            text.push_back(0);
        }
        const std::uint32_t pos = ctx.rng().Below(string.len + 3);
        const std::uint32_t n = ctx.rng().Chance(1, 6) ? 0 : static_cast<std::uint32_t>(text.size() - 1);
        const char* compareWith = ctx.rng().Chance(1, 8) ? nullptr : text.data();
        if (!S3RE_EXPECT_EQ(reinterpret_cast<std::uintptr_t>(Original_String_CStr(&string, nullptr)),
                            reinterpret_cast<std::uintptr_t>(ours.cStr(&string, nullptr))) ||
            !S3RE_EXPECT_EQ(Original_String_Find(&string, nullptr, text.data(), pos, n),
                            ours.find(&string, nullptr, text.data(), pos, n)) ||
            !S3RE_EXPECT_EQ(Original_String_NotEqualText(&string, compareWith),
                            ours.notEqual(&string, compareWith))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: length %u, pos %u, n %u", i, string.len, pos, n);
            return;
        }
    }
}

std::uint32_t __fastcall WrongFind(const String* self, void* edx, const char* s, std::uint32_t pos, std::uint32_t n) {
    // Forgets that a match may end exactly at the string's end.
    if (n > 0 && pos + n == self->len && std::memcmp(self->ptr + pos, s, n) == 0) {
        return detail::Npos();
    }
    return String_Find(self, edx, s, pos, n);
}

}  // namespace

S3RE_TEST(StringSearch_MatchesOriginal) {
    CompareStrings(ctx, {&String_CStr, &String_Find, &String_NotEqualText}, 20000);
}

S3RE_TEST(StringSearch_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareStrings(probe, {&String_CStr, &WrongFind, &String_NotEqualText}, 20000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::msvc6
