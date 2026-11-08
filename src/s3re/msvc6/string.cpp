// std::string (MSVC 6.0) member functions. See string.h for the layout.

#include "msvc6/string.h"

#include "hooks.h"
#include "test/difftest.h"

#include <vector>

namespace s3re::msvc6 {

// ---------------------------------------------------------------------------
// _Eos (0x00401f60)
//
//   mov eax, [esp+4]      ; n
//   mov [ecx+8], eax      ; _Len = n
//   mov ecx, [ecx+4]
//   mov byte [ecx+eax], 0 ; _Ptr[n] = 0
//   ret 4
// ---------------------------------------------------------------------------

S3RE_HOOK(0x00401f60, String_Eos, StringEosFn, Original_String_Eos, String_Eos);

// Purpose: Part of the C++ standard string the game was built with: sets a string's length and writes the
// terminating zero. Game data structures contain such strings, so we need the exact same
// behaviour.
void __fastcall String_Eos(String* self, void* /*edx*/, std::uint32_t n) {
    self->len = n;
    self->ptr[n] = '\0';
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// Runs `candidate` and the original _Eos on identical random strings and
// compares the resulting objects and buffers byte for byte.
void CompareEos(test::Context& ctx, StringEosFn candidate, int iterations) {
    constexpr std::uint32_t kGuard = 8;  // catch writes past the buffer
    for (int i = 0; i < iterations; ++i) {
        const std::uint32_t capacity = ctx.rng().Below(64);
        const std::uint32_t n = ctx.rng().Below(capacity + 1);
        const std::uint32_t size = capacity + 2 + kGuard;  // count + chars + zero + guard

        std::vector<unsigned char> bufferA(size);
        ctx.rng().Fill(bufferA.data(), size);
        std::vector<unsigned char> bufferB = bufferA;

        String a{};
        a.allocator = static_cast<std::uint8_t>(ctx.rng().Next32());
        a.ptr = reinterpret_cast<char*>(bufferA.data() + 1);
        a.len = ctx.rng().Below(capacity + 1);
        a.res = capacity;
        String b = a;
        b.ptr = reinterpret_cast<char*>(bufferB.data() + 1);

        Original_String_Eos(&a, nullptr, n);
        candidate(&b, nullptr, n);

        bool same = true;
        same &= S3RE_EXPECT_EQ(a.allocator, b.allocator);
        same &= S3RE_EXPECT_EQ(a.len, b.len);
        same &= S3RE_EXPECT_EQ(a.res, b.res);
        same &= S3RE_EXPECT(a.ptr == reinterpret_cast<char*>(bufferA.data() + 1));
        same &= S3RE_EXPECT(b.ptr == reinterpret_cast<char*>(bufferB.data() + 1));
        same &= S3RE_EXPECT_MEM_EQ(bufferA.data(), bufferB.data(), size);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  inputs: capacity=%u n=%u (iteration %d)",
                     capacity, n, i);
            return;
        }
    }
}

// A deliberately wrong _Eos (forgets the terminating zero when n is 0).
void __fastcall WrongEos(String* self, void* /*edx*/, std::uint32_t n) {
    self->len = n;
    if (n != 0) {
        self->ptr[n] = '\0';
    }
}

}  // namespace

S3RE_TEST(String_Eos_MatchesOriginal) {
    CompareEos(ctx, &String_Eos, 100000);
}

// Guards against a comparison that can't fail: the same check must catch a
// subtly wrong implementation.
S3RE_TEST(String_Eos_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareEos(probe, &WrongEos, 100000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::msvc6
