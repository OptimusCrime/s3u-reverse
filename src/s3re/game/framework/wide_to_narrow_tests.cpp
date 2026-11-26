// Tests of wide_to_narrow.cpp (WideToNarrowString 0x60b6a0).
//
// Random 16-bit strings of printable ASCII (see below), sometimes empty or
// without a buffer. The original and ours convert the same
// string; compared: EAX (relative to the result), the result's text,
// length, capacity and count byte (not its allocator byte, an
// uninitialized stack byte in the original), and that the source is
// unchanged.

#include "hooks.h"
#include "msvc6/string.h"
#include "msvc6/wstring.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

msvc6::String* __fastcall WideToNarrowString(msvc6::String* result, const msvc6::WString* wide);
using WideToNarrowFn = decltype(&WideToNarrowString);
extern WideToNarrowFn Original_WideToNarrowString;

namespace {

using msvc6::String;
using msvc6::WString;

constexpr std::uint32_t kMaxChars = 40;

struct Converted {
    std::uint32_t eax = 0;
    std::uint32_t len = 0;
    std::uint32_t res = 0;
    std::uint32_t count = 0xffff;
    char text[2 * kMaxChars + 4] = {};
};

Converted Convert(WideToNarrowFn fn, const WString* wide) {
    String result;
    std::memset(&result, 0x5a, sizeof(result));
    Converted out;
    out.eax = reinterpret_cast<std::uint32_t>(fn(&result, wide)) - reinterpret_cast<std::uint32_t>(&result);
    out.len = result.len;
    out.res = result.res;
    if (result.ptr != nullptr) {
        out.count = static_cast<std::uint8_t>(result.ptr[-1]);
        std::memcpy(out.text, result.ptr, result.len < sizeof(out.text) ? result.len : sizeof(out.text));
    }
    msvc6::String_Tidy(&result, nullptr, 1);
    return out;
}

void CompareWideToNarrow(test::Context& ctx, WideToNarrowFn candidate, int cases) {
    std::uint16_t characters[kMaxChars + 2];
    for (int i = 0; i < cases; ++i) {
        test::Rng& rng = ctx.rng();
        const std::uint32_t length = rng.Chance(1, 10) ? 0 : 1 + rng.Below(kMaxChars);
        for (std::uint32_t k = 0; k < kMaxChars + 2; ++k) {
            // Printable ASCII only: no embedded zeros (wcstombs then returns
            // 0 and both loop forever), and no characters wcstombs rejects
            // (it then leaves the fresh buffer unterminated and the text is
            // whatever the heap held: the emulator spec covers that path).
            characters[k] = static_cast<std::uint16_t>(32 + rng.Below(95));
        }
        characters[length] = 0;
        WString wide{};
        wide.ptr = rng.Chance(1, 10) ? nullptr : characters;
        wide.len = wide.ptr != nullptr ? length : 0;  // (no buffer with a length would loop forever too)
        wide.res = length;
        const WString before = wide;
        const Converted a = Convert(Original_WideToNarrowString, &wide);
        const Converted b = Convert(candidate, &wide);
        bool same = S3RE_EXPECT_EQ(a.eax, b.eax);
        same = S3RE_EXPECT_EQ(a.len, b.len) && same;
        same = S3RE_EXPECT_EQ(a.res, b.res) && same;
        same = S3RE_EXPECT_EQ(a.count, b.count) && same;
        same = S3RE_EXPECT_MEM_EQ(a.text, b.text, sizeof(a.text)) && same;
        same = S3RE_EXPECT_MEM_EQ(&before, &wide, sizeof(wide)) && same;
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: length %u", i, length);
            break;
        }
    }
}

// Wrong: the first character changed.
String* __fastcall WrongWideToNarrow(String* result, const WString* wide) {
    WideToNarrowString(result, wide);
    if (result->len > 0) {
        result->ptr[0] ^= 1;
    }
    return result;
}

}  // namespace

S3RE_TEST(WideToNarrow_MatchesOriginal) {
    CompareWideToNarrow(ctx, &WideToNarrowString, 20000);
}

S3RE_TEST(WideToNarrow_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareWideToNarrow(probe, &WrongWideToNarrow, 500);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
