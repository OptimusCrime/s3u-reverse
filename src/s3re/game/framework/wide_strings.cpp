// WideToNarrowString (0x0060b6a0, our name): the framework's conversion of
// a 16-bit std::wstring to a std::string (used by CEditControl::GetText
// 0x418a10). A compiler-made __fastcall helper: ECX = the result object
// (constructed here), EDX = the source; no stack arguments. EAX = result.
//
// A local string; a buffer of length * 2 + 2 bytes from operator new; when
// there is one and the source isn't empty, a loop from position 0: the
// CRT's wcstombs 0x622308(buffer, characters + position, length * 2 + 2)
// (the static empty wide string for a source without buffer), position +=
// its result, and the local = string(buffer) (through a temporary, which
// assign(temporary, 0, npos) shares), while position < length (re-read).
// So the local ends as the conversion of the last piece; wcstombs's -1 for
// an unconvertible character ends the loop when it happens at position 0
// and steps one character back otherwise, and a 0 (a zero character first)
// would loop for ever (kept as in the original). Then operator delete
// (buffer) (also when null), the result = assign(local, 0, 0xFFFFFFFF),
// and the local's ~string.
//
// Allocator bytes: the local's, the temporaries' and the result's come from
// uninitialized stack bytes of the original; ours use 0. Only the result's
// leaves the function (an empty allocator's byte, never read).

#include "game/tables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "msvc6/wstring.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <cwchar>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kWcstombs = 0x00622308;  // the CRT's wcstombs
constexpr std::uint32_t kAll = 0xFFFFFFFFu;

using WcstombsFn = std::uint32_t(__cdecl*)(char* to, const std::uint16_t* from, std::uint32_t count);

}  // namespace

using WideToNarrowFn = msvc6::String*(__fastcall*)(msvc6::String* result, const msvc6::WString* source);
msvc6::String* __fastcall WideToNarrowString(msvc6::String* result, const msvc6::WString* source);
S3RE_HOOK(0x0060b6a0, WideToNarrowString, WideToNarrowFn, Original_WideToNarrowString, WideToNarrowString);

// Purpose: Converts a 16-bit text (an edit field's) to an 8-bit string.
msvc6::String* __fastcall WideToNarrowString(msvc6::String* result, const msvc6::WString* source) {
    msvc6::String text{};
    msvc6::String_Tidy(&text, nullptr, 0);
    auto* buffer = static_cast<char*>(msvc6::GameOperatorNew(source->len * 2 + 2));
    if (buffer != nullptr && source->len != 0) {
        std::uint32_t position = 0;
        do {
            const std::uint16_t* characters = source->ptr != nullptr ? source->ptr : &g_wstringNullBuffer[0];
            position += reinterpret_cast<WcstombsFn>(kWcstombs)(buffer, characters + position, source->len * 2 + 2);
            msvc6::String piece{};
            msvc6::String_Tidy(&piece, nullptr, 0);
            msvc6::String_AssignChars(&piece, nullptr, buffer, msvc6::detail::Length(buffer));
            msvc6::String_AssignSubstr(&text, nullptr, &piece, 0, msvc6::detail::Npos());
            msvc6::String_Tidy(&piece, nullptr, 1);
        } while (position < source->len);
    }
    msvc6::GameOperatorDelete(buffer);
    result->allocator = 0;
    msvc6::detail::Clear(result);
    msvc6::String_AssignSubstr(result, nullptr, &text, 0, kAll);
    msvc6::String_Destroy(&text, nullptr);
    return result;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Random wide texts of ASCII and Latin-1 characters (no zero characters:
// one would end a piece and then loop for ever in both; no characters
// wcstombs can't convert in the C locale: it then leaves the buffer
// unterminated, and the string(buffer) reads the uninitialized heap block,
// which differs between the two runs here; the emulator spec, whose
// operator new hands both the same bytes, covers them), sometimes without buffer. Compared: EAX
// (as result), the result's text, length, capacity and count byte (its
// allocator byte is the original's stack garbage: not compared).

msvc6::String* __fastcall WrongWideToNarrowString(msvc6::String* result, const msvc6::WString* source);

namespace {

void CompareConversion(test::Context& ctx, WideToNarrowFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        std::uint16_t words[48];
        const std::uint32_t length = ctx.rng().Chance(1, 8) ? 0 : 1 + ctx.rng().Below(40);
        for (std::uint32_t k = 0; k < length; ++k) {
            const std::uint32_t kind = ctx.rng().Below(10);
            words[k] = static_cast<std::uint16_t>(kind < 7 ? 0x20 + ctx.rng().Below(0x5f) : 0x80 + ctx.rng().Below(0x80));
        }
        words[length] = 0;
        const bool noBuffer = length == 0 && ctx.rng().Chance(1, 2);
        msvc6::String results[2];
        msvc6::String* eax[2] = {};
        for (int side = 0; side < 2; ++side) {
            // The source as a wide string with its own buffer (count byte 0).
            static std::uint16_t block[2][52];
            block[side][0] = 0;
            std::memcpy(block[side] + 1, words, (length + 1) * 2);
            msvc6::WString source{};
            source.ptr = noBuffer ? nullptr : block[side] + 1;
            source.len = length;
            source.res = length;
            std::memset(&results[side], 0xcd, sizeof(results[side]));
            eax[side] = side == 0 ? Original_WideToNarrowString(&results[side], &source)
                                  : candidate(&results[side], &source);
        }
        const msvc6::String& a = results[0];
        const msvc6::String& b = results[1];
        bool same = S3RE_EXPECT(eax[0] == &results[0] && eax[1] == &results[1]) && S3RE_EXPECT_EQ(a.len, b.len) &&
                    S3RE_EXPECT_EQ(a.res, b.res) && S3RE_EXPECT_EQ(a.ptr == nullptr, b.ptr == nullptr);
        if (same && a.ptr != nullptr) {
            same = S3RE_EXPECT_EQ(static_cast<std::uint8_t>(a.ptr[-1]), static_cast<std::uint8_t>(b.ptr[-1])) &&
                   S3RE_EXPECT_MEM_EQ(a.ptr, b.ptr, a.len + 1);
        }
        msvc6::String_Destroy(&results[0], nullptr);
        msvc6::String_Destroy(&results[1], nullptr);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: length %u", i, length);
            return;
        }
    }
}

}  // namespace

// Wrong (also the emulator self-test's pairing): the last character dropped.
msvc6::String* __fastcall WrongWideToNarrowString(msvc6::String* result, const msvc6::WString* source) {
    WideToNarrowString(result, source);
    if (result->len > 1) {
        msvc6::String_Erase(result, nullptr, result->len - 1, 1);
    }
    return result;
}

S3RE_TEST(WideToNarrowString_MatchesOriginal) {
    CompareConversion(ctx, &WideToNarrowString, 3000);
}

S3RE_TEST(WideToNarrowString_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareConversion(probe, &WrongWideToNarrowString, 300);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
