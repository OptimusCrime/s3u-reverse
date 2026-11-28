// Small members of the console thread (CConsoleThread, the application
// object on WinMain's stack; vtable 0x6553fc; Dirk\Console.cpp) and two
// small helpers compiled into that file.
//
// CConsoleThread::vf06 (0x00405b00): (bool* out) ret 4 -> the dword +0x54,
//   *out = 0 (vtable slot 6).
// CConsoleThread::vf07 (0x00405b10): () -> the dword +0x94 when the state
//   +0x44 is 0, +0x8c when it is 1, else the Console.cpp assert "Invalid
//   GameState" (line 0x251; Win32::ThrowAssertmentFailed 0x6119b0 throws)
//   (vtable slot 7).
// App_DecrementIndex (0x00409e50): (n) ret 4 -> n - 1, ECX unused. Callers:
//   CConsoleThread::vf04 and CGameSetupDialog's constructor. Was
//   App_sub_409e50.
// WCValOrderedVector_String::Equals (0x00409bd0): (const std::string* a,
//   const std::string* b) ret 8 -> 1 when both have the same length and
//   characters (a's text pointer used as it is, b's null text read as the
//   static empty string 0x6553a0), else 0 (slot 2 of the vtables 0x655440
//   and 0x656d08: the vector's equality callback). Was App_sub_409bd0.
// ostream_ApplyManipulator (0x004063f0): (manipulator) ret 4 -> this: calls
//   manipulator(this) (cdecl), the inlined ostream::operator<<(ostream&
//   (*)(ostream&)) of MSVCIRT. Callers: App_sub_405e50. Was App_sub_4063f0.

#include "game/tables.h"
#include "game/texts.h"
#include "hooks.h"
#include "msvc6/string.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace s3re::game {

#pragma pack(push, 1)
// The fields of CConsoleThread these members read (the rest unknown).
struct ConsoleThreadFields {
    std::uint8_t unk000[0x44];
    std::uint32_t game_state;  // +0x44 0 or 1 (vf07)
    std::uint8_t unk048[0x54 - 0x48];
    std::uint32_t value54;     // +0x54 (vf06)
    std::uint8_t unk058[0x8c - 0x58];
    std::uint32_t value8c;     // +0x8c (vf07, state 1)
    std::uint8_t unk090[0x94 - 0x90];
    std::uint32_t value94;     // +0x94 (vf07, state 0)
};
#pragma pack(pop)
static_assert(offsetof(ConsoleThreadFields, game_state) == 0x44);
static_assert(offsetof(ConsoleThreadFields, value54) == 0x54);
static_assert(offsetof(ConsoleThreadFields, value8c) == 0x8c);
static_assert(offsetof(ConsoleThreadFields, value94) == 0x94);

namespace {
constexpr std::uintptr_t kThrowAssertmentFailed = 0x006119b0;  // Win32::ThrowAssertmentFailed (ours)
constexpr std::int32_t kInvalidGameStateLine = 0x251;
constexpr std::uint32_t kStateZero = 0;
constexpr std::uint32_t kStateOne = 1;
}  // namespace

// Purpose: A console-thread virtual returning a value of the thread and a
// "false" flag (meaning unknown).
std::uint32_t __fastcall CConsoleThread_vf06(const ConsoleThreadFields* self, void*, std::uint8_t* out) {
    *out = 0;
    return self->value54;
}

// Purpose: A console-thread virtual returning one of two values by the
// thread's state (meaning unknown).
std::uint32_t __fastcall CConsoleThread_vf07(const ConsoleThreadFields* self, void*) {
    switch (self->game_state) {
    case kStateZero:
        return self->value94;
    case kStateOne:
        return self->value8c;
    default: {
        using ThrowFn = void(__fastcall*)(const char* expression, const char* file, std::int32_t line);
        reinterpret_cast<ThrowFn>(kThrowAssertmentFailed)(texts::kInvalidGameState, texts::kConsoleFile,
                                                          kInvalidGameStateLine);
        return 0;  // (not reached: the assert throws)
    }
    }
}

// Purpose: Turns a one-based number into an index (n - 1).
std::int32_t __fastcall App_DecrementIndex(const void*, void*, std::int32_t n) {
    return n - 1;
}

// Purpose: The string vector's equality test (same length, same characters).
std::uint32_t __stdcall WCValOrderedVector_String_Equals(const msvc6::String* a, const msvc6::String* b) {
    const char* textB = b->ptr != nullptr ? b->ptr : g_stringNullBuffer.get();
    const std::uint32_t shorter = a->len < b->len ? a->len : b->len;
    if (std::memcmp(a->ptr, textB, shorter) != 0) {
        return 0;
    }
    return a->len == b->len ? 1 : 0;
}

// Purpose: Applies an output-stream manipulator (std::endl and the like) to
// a stream.
void* __fastcall ostream_ApplyManipulator(void* self, void*, void(__cdecl* manipulator)(void* stream)) {
    manipulator(self);
    return self;
}

using Vf06Fn = std::uint32_t(__fastcall*)(const ConsoleThreadFields* self, void* edx, std::uint8_t* out);
using Vf07Fn = std::uint32_t(__fastcall*)(const ConsoleThreadFields* self, void* edx);
using DecrementFn = std::int32_t(__fastcall*)(const void* self, void* edx, std::int32_t n);
using EqualsFn = std::uint32_t(__stdcall*)(const msvc6::String* a, const msvc6::String* b);
using ManipulatorFn = void(__cdecl*)(void* stream);
using ApplyFn = void*(__fastcall*)(void* self, void* edx, ManipulatorFn manipulator);
S3RE_HOOK(0x00405b00, CConsoleThread_vf06, Vf06Fn, Original_CConsoleThread_vf06, CConsoleThread_vf06);
S3RE_HOOK(0x00405b10, CConsoleThread_vf07, Vf07Fn, Original_CConsoleThread_vf07, CConsoleThread_vf07);
S3RE_HOOK(0x00409e50, App_DecrementIndex, DecrementFn, Original_App_DecrementIndex, App_DecrementIndex);
S3RE_HOOK(0x00409bd0, WCValOrderedVector_String_Equals, EqualsFn, Original_WCValOrderedVector_String_Equals,
          WCValOrderedVector_String_Equals);
S3RE_HOOK(0x004063f0, ostream_ApplyManipulator, ApplyFn, Original_ostream_ApplyManipulator,
          ostream_ApplyManipulator);

namespace {

void* g_manipulated = nullptr;
std::uint32_t g_manipulations = 0;

void __cdecl RecordManipulator(void* stream) {
    g_manipulated = stream;
    ++g_manipulations;
}

std::string RandomText(test::Rng& rng) {
    std::string text(rng.Below(6), 'a');
    for (char& c : text) {
        c = static_cast<char>('a' + rng.Below(3));
    }
    return text;
}

// Wrong: compares only the lengths.
std::uint32_t __stdcall WrongEquals(const msvc6::String* a, const msvc6::String* b) {
    return a->len == b->len ? 1 : 0;
}

int CountEqualsDifferences(test::Context& ctx, EqualsFn candidate, int cases) {
    int differences = 0;
    for (int i = 0; i < cases; ++i) {
        const std::string ta = RandomText(ctx.rng());
        const std::string tb = ctx.rng().Chance(1, 3) ? ta : RandomText(ctx.rng());
        msvc6::String a;
        msvc6::String b;
        msvc6::String_FromChars(&a, nullptr, ta.c_str(), static_cast<std::uint32_t>(ta.size()));
        if (tb.empty() && ctx.rng().Chance(1, 2)) {
            msvc6::detail::Clear(&b);
        } else {
            msvc6::String_FromChars(&b, nullptr, tb.c_str(), static_cast<std::uint32_t>(tb.size()));
        }
        if ((Original_WCValOrderedVector_String_Equals(&a, &b) & 0xff) != (candidate(&a, &b) & 0xff)) {
            ++differences;
        }
        msvc6::String_Destroy(&a, nullptr);
        msvc6::String_Destroy(&b, nullptr);
    }
    return differences;
}

}  // namespace

S3RE_TEST(ConsoleThreadSmall_MatchOriginal) {
    for (int i = 0; i < 500; ++i) {
        ConsoleThreadFields fields;
        ctx.rng().Fill(&fields, sizeof(fields));
        fields.game_state = ctx.rng().Below(2);
        std::uint8_t outA = 0x5a;
        std::uint8_t outB = 0x5a;
        const std::int32_t n = static_cast<std::int32_t>(ctx.rng().Next32());
        if (!S3RE_EXPECT_EQ(Original_CConsoleThread_vf06(&fields, nullptr, &outA),
                            CConsoleThread_vf06(&fields, nullptr, &outB)) ||
            !S3RE_EXPECT_EQ(outA, outB) ||
            !S3RE_EXPECT_EQ(Original_CConsoleThread_vf07(&fields, nullptr), CConsoleThread_vf07(&fields, nullptr)) ||
            !S3RE_EXPECT_EQ(Original_App_DecrementIndex(nullptr, nullptr, n), App_DecrementIndex(nullptr, nullptr, n))) {
            return;
        }
    }
    S3RE_EXPECT_EQ(0, CountEqualsDifferences(ctx, &WCValOrderedVector_String_Equals, 3000));
    std::uint8_t stream[16] = {};
    g_manipulations = 0;
    S3RE_EXPECT(Original_ostream_ApplyManipulator(stream, nullptr, &RecordManipulator) == stream);
    S3RE_EXPECT(ostream_ApplyManipulator(stream, nullptr, &RecordManipulator) == stream);
    S3RE_EXPECT(g_manipulations == 2 && g_manipulated == stream);
}

S3RE_TEST(ConsoleThreadSmall_ComparisonDetectsWrongImplementation) {
    S3RE_EXPECT(CountEqualsDifferences(ctx, &WrongEquals, 3000) > 0);
}

}  // namespace s3re::game
