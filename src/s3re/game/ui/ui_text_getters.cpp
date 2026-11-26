// The text getters of CTextView and CEditControl (Dirk\dialog.cpp): copies
// of a string member into the caller's result object (the copy constructor
// inlined: an empty result, then assign(member, 0, npos), which shares the
// member's buffer unless it is empty or its count is 0xfe or more).
//
// CTextView::GetText (0x00420760): (std::string* result) ret 4: the text at
//   +0x410; n = min(length, 0xFFFFFFFF). The result's allocator byte is the
//   low byte of the result's address (the empty allocator argument shares
//   that stack slot). The self-assignment branch is dead (the result is a
//   new object). EAX = result.
// CEditControl::GetTextW (0x00418bc0): (std::wstring* result) ret 4: the
//   16-bit text at +0x4c (this = the edit control's start); the result's
//   allocator byte is the member's; n = min(length, wstring npos 0x655dcc).
//   EAX = result.
// CEditControl::GetText (0x00418a10): (std::string* result) ret 4: the 16-bit
//   text converted (WideToNarrowString 0x60b6a0 into a local), the result
//   = a copy of it (allocator byte: the result address's low byte; assign
//   (local, 0, 0xFFFFFFFF)), then the local's ~string inlined. EAX = result.

#include "game/tables.h"
#include "hooks.h"
#include "msvc6/string.h"
#include "msvc6/wstring.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

constexpr std::ptrdiff_t kTextViewText = 0x410;
constexpr std::ptrdiff_t kEditText = 0x4c;
constexpr std::uint32_t kAll = 0xFFFFFFFFu;  // GetText's literal length limit

msvc6::String* TextViewText(std::uintptr_t self) {
    return reinterpret_cast<msvc6::String*>(self + kTextViewText);
}

msvc6::WString* EditText(std::uintptr_t self) {
    return reinterpret_cast<msvc6::WString*>(self + kEditText);
}

}  // namespace

using GetTextFn = msvc6::String*(__fastcall*)(std::uintptr_t self, void* edx, msvc6::String* result);
using GetTextWFn = msvc6::WString*(__fastcall*)(std::uintptr_t self, void* edx, msvc6::WString* result);

msvc6::String* __fastcall CTextView_GetText(std::uintptr_t self, void* edx, msvc6::String* result);
msvc6::WString* __fastcall CEditControl_GetTextW(std::uintptr_t self, void* edx, msvc6::WString* result);
msvc6::String* __fastcall CEditControl_GetText(std::uintptr_t self, void* edx, msvc6::String* result);

S3RE_HOOK(0x00420760, CTextView_GetText, GetTextFn, Original_CTextView_GetText, CTextView_GetText);
S3RE_HOOK(0x00418bc0, CEditControl_GetTextW, GetTextWFn, Original_CEditControl_GetTextW, CEditControl_GetTextW);
S3RE_HOOK(0x00418a10, CEditControl_GetText, GetTextFn, Original_CEditControl_GetText, CEditControl_GetText);

// Purpose: Returns a copy of a multi-line text view's text.
msvc6::String* __fastcall CTextView_GetText(std::uintptr_t self, void* /*edx*/, msvc6::String* result) {
    result->allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(result));
    msvc6::detail::Clear(result);
    return msvc6::String_AssignSubstr(result, nullptr, TextViewText(self), 0, kAll);
}

// Purpose: Returns a copy of an edit field's (16-bit) text.
msvc6::WString* __fastcall CEditControl_GetTextW(std::uintptr_t self, void* /*edx*/, msvc6::WString* result) {
    const msvc6::WString* text = EditText(self);
    result->allocator = text->allocator;
    result->ptr = nullptr;
    result->len = 0;
    result->res = 0;
    return msvc6::WString_AssignSubstr(result, nullptr, text, 0, g_wstringNpos);
}

// Purpose: Returns an edit field's text as an 8-bit string (player names,
// chat lines, savegame names).
msvc6::String* __fastcall CEditControl_GetText(std::uintptr_t self, void* /*edx*/, msvc6::String* result) {
    using WideToNarrowFn = msvc6::String*(__fastcall*)(msvc6::String* result, const msvc6::WString* source);
    msvc6::String text;
    reinterpret_cast<WideToNarrowFn>(0x0060b6a0)(&text, EditText(self));  // WideToNarrowString (ours)
    result->allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(result));
    msvc6::detail::Clear(result);
    msvc6::String_AssignSubstr(result, nullptr, &text, 0, kAll);
    msvc6::String_Destroy(&text, nullptr);
    return result;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// A random object (with the member at its offset) whose member is none,
// own, shared with an outside string or frozen (or for the wide one: a
// count of 0xfe); a random result object at one of a few addresses (its
// allocator byte depends on it). Compared: EAX (as result), the result's
// bytes (its buffer as "shares the member's"), the member's count byte, the
// texts.

namespace {

template <typename Str, typename Char>
struct Kit;

template <>
struct Kit<msvc6::String, char> {
    static void Assign(msvc6::String* s, const char* text, std::uint32_t n) {
        msvc6::String_AssignChars(s, nullptr, text, n);
    }
    static void Share(msvc6::String* to, const msvc6::String* from) {
        msvc6::String_AssignSubstr(to, nullptr, from, 0, kAll);
    }
    static void Destroy(msvc6::String* s) { msvc6::String_Destroy(s, nullptr); }
    static std::uint8_t& Count(msvc6::String* s) { return reinterpret_cast<std::uint8_t*>(s->ptr)[-1]; }
};

using NarrowKit = Kit<msvc6::String, char>;

struct WideKit {
    // A fresh buffer: assigned from a stack temporary whose count 0xfe
    // forbids sharing, so the text is copied.
    static void Assign(msvc6::WString* s, const std::uint16_t* text, std::uint32_t n) {
        msvc6::WString temp{};
        temp.len = n;
        temp.res = n;
        std::uint16_t block[48];
        block[0] = 0xfe00;  // byte 1 = count 0xfe
        std::memcpy(block + 1, text, n * 2);
        block[1 + n] = 0;
        temp.ptr = block + 1;
        msvc6::WString_AssignSubstr(s, nullptr, &temp, 0, 0xFFFFFFFFu);
    }
    static std::uint8_t& Count(msvc6::WString* s) { return reinterpret_cast<std::uint8_t*>(s->ptr)[-1]; }
};

void CompareGetText(test::Context& ctx, GetTextFn candidate, int cases) {
    alignas(16) static std::uint8_t objects[2][0x420 + 16];
    alignas(16) static std::uint8_t results[2][64];
    for (int i = 0; i < cases; ++i) {
        const std::uint64_t seed = ctx.rng().Next64();
        const std::size_t resultAt = 4 * ctx.rng().Below(8);
        msvc6::String* got[2] = {};
        msvc6::String* eax[2] = {};
        msvc6::String outside[2] = {};
        for (int side = 0; side < 2; ++side) {
            test::Rng rng(seed);
            rng.Fill(objects[side], sizeof(objects[side]));
            rng.Fill(results[side], sizeof(results[side]));
            const auto self = reinterpret_cast<std::uintptr_t>(objects[side]);
            msvc6::String* member = TextViewText(self);
            msvc6::detail::Clear(member);
            char text[40];
            const std::uint32_t length = rng.Chance(1, 6) ? 0 : rng.Below(sizeof(text));
            for (std::uint32_t k = 0; k < length; ++k) {
                text[k] = static_cast<char>(1 + rng.Below(255));
            }
            const int kind = static_cast<int>(rng.Below(5));  // 0 none, 1 own, 2 shared, 3 frozen, 4 count 0xfe
            if (kind != 0) {
                NarrowKit::Assign(member, text, length);
            }
            if (kind == 2 && member->ptr != nullptr) {
                NarrowKit::Share(&outside[side], member);
            }
            if (kind == 3) {
                msvc6::String_Freeze(member, nullptr);
            }
            if (kind == 4 && member->ptr != nullptr) {
                NarrowKit::Count(member) = 0xfe;  // too many copies to share
            }
            got[side] = reinterpret_cast<msvc6::String*>(results[side] + resultAt);
            eax[side] = side == 0 ? Original_CTextView_GetText(self, nullptr, got[side])
                                  : candidate(self, nullptr, got[side]);
        }
        msvc6::String* ma = TextViewText(reinterpret_cast<std::uintptr_t>(objects[0]));
        msvc6::String* mb = TextViewText(reinterpret_cast<std::uintptr_t>(objects[1]));
        bool same = S3RE_EXPECT(eax[0] == got[0] && eax[1] == got[1]) &&
                    // The difference wraps as a byte (int promotion would make 0x14 and -0xec differ).
                    S3RE_EXPECT_EQ(static_cast<std::uint8_t>(got[0]->allocator - static_cast<std::uint8_t>(
                                       reinterpret_cast<std::uintptr_t>(results[0]))),
                                   static_cast<std::uint8_t>(got[1]->allocator - static_cast<std::uint8_t>(
                                       reinterpret_cast<std::uintptr_t>(results[1])))) &&
                    S3RE_EXPECT_EQ(got[0]->len, got[1]->len) && S3RE_EXPECT_EQ(got[0]->res, got[1]->res) &&
                    S3RE_EXPECT_EQ(got[0]->ptr == ma->ptr, got[1]->ptr == mb->ptr) &&
                    S3RE_EXPECT_EQ(got[0]->ptr == nullptr, got[1]->ptr == nullptr);
        if (same && got[0]->ptr != nullptr) {
            same = S3RE_EXPECT_EQ(NarrowKit::Count(got[0]), NarrowKit::Count(got[1])) &&
                   S3RE_EXPECT_MEM_EQ(got[0]->ptr, got[1]->ptr, got[0]->len + 1);
        }
        if (same && ma->ptr != nullptr) {
            same = S3RE_EXPECT_EQ(NarrowKit::Count(ma), NarrowKit::Count(mb));
        }
        for (int side = 0; side < 2; ++side) {
            msvc6::String* member = TextViewText(reinterpret_cast<std::uintptr_t>(objects[side]));
            if (member->ptr != nullptr && NarrowKit::Count(member) == 0xfe) {
                NarrowKit::Count(member) = 0;  // (made up above; the result has its own buffer)
            }
            NarrowKit::Destroy(got[side]);
            NarrowKit::Destroy(member);
            NarrowKit::Destroy(&outside[side]);
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

msvc6::String* __fastcall WrongGetText(std::uintptr_t self, void* edx, msvc6::String* result) {
    CTextView_GetText(self, edx, result);
    result->allocator = 0;  // wrong: not the address's low byte
    return result;
}

// GetTextW: the member is none, an own buffer (count 0, 1 or 0xfe) or
// frozen; its text random words.
void CompareGetTextW(test::Context& ctx, GetTextWFn candidate, int cases) {
    alignas(16) static std::uint8_t objects[2][0x60];
    alignas(16) static std::uint8_t results[2][32];
    for (int i = 0; i < cases; ++i) {
        const std::uint64_t seed = ctx.rng().Next64();
        msvc6::WString* got[2] = {};
        msvc6::WString* eax[2] = {};
        for (int side = 0; side < 2; ++side) {
            test::Rng rng(seed);
            rng.Fill(objects[side], sizeof(objects[side]));
            rng.Fill(results[side], sizeof(results[side]));
            const auto self = reinterpret_cast<std::uintptr_t>(objects[side]);
            msvc6::WString* member = EditText(self);
            member->ptr = nullptr;
            member->len = 0;
            member->res = 0;
            std::uint16_t text[40];
            const std::uint32_t length = rng.Chance(1, 6) ? 0 : rng.Below(40);
            for (std::uint32_t k = 0; k < length; ++k) {
                text[k] = static_cast<std::uint16_t>(1 + rng.Below(0xffff));
            }
            const int kind = static_cast<int>(rng.Below(5));  // 0 none, 1 own, 2 count 1, 3 frozen, 4 count 0xfe
            if (kind != 0) {
                WideKit::Assign(member, text, length);
            }
            if (member->ptr != nullptr) {
                static constexpr std::uint8_t kCounts[] = {0, 0, 1, 0xff, 0xfe};
                WideKit::Count(member) = kCounts[kind];
            }
            got[side] = reinterpret_cast<msvc6::WString*>(results[side] + 8);
            eax[side] = side == 0 ? Original_CEditControl_GetTextW(self, nullptr, got[side])
                                  : candidate(self, nullptr, got[side]);
        }
        msvc6::WString* ma = EditText(reinterpret_cast<std::uintptr_t>(objects[0]));
        msvc6::WString* mb = EditText(reinterpret_cast<std::uintptr_t>(objects[1]));
        bool same = S3RE_EXPECT(eax[0] == got[0] && eax[1] == got[1]) &&
                    S3RE_EXPECT_MEM_EQ(results[0], results[1], 8) && S3RE_EXPECT_MEM_EQ(results[0] + 24, results[1] + 24, 8) &&
                    S3RE_EXPECT_MEM_EQ(got[0]->pad - 1, got[1]->pad - 1, 4) && S3RE_EXPECT_EQ(got[0]->len, got[1]->len) &&
                    S3RE_EXPECT_EQ(got[0]->res, got[1]->res) && S3RE_EXPECT_EQ(got[0]->ptr == ma->ptr, got[1]->ptr == mb->ptr) &&
                    S3RE_EXPECT_EQ(got[0]->ptr == nullptr, got[1]->ptr == nullptr);
        if (same && got[0]->ptr != nullptr) {
            same = S3RE_EXPECT_EQ(WideKit::Count(got[0]), WideKit::Count(got[1])) &&
                   S3RE_EXPECT_MEM_EQ(got[0]->ptr, got[1]->ptr, (got[0]->len + 1) * 2);
        }
        if (same && ma->ptr != nullptr) {
            same = S3RE_EXPECT_EQ(WideKit::Count(ma), WideKit::Count(mb));
        }
        // The buffers leak (the test's made-up counts make their owners
        // unclear; a few hundred small blocks).
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

msvc6::WString* __fastcall WrongGetTextW(std::uintptr_t self, void* edx, msvc6::WString* result) {
    CEditControl_GetTextW(self, edx, result);
    result->allocator ^= 1;  // wrong: not the member's allocator byte
    return result;
}

}  // namespace

namespace {

// CEditControl::GetText: a member of random printable words (or none);
// compared: EAX, the result's allocator byte (relative to its address),
// length, capacity, count byte and text.
void CompareEditGetText(test::Context& ctx, GetTextFn candidate, int cases) {
    alignas(16) static std::uint8_t objects[2][0x60];
    alignas(16) static std::uint8_t results[2][64];
    static std::uint16_t blocks[2][48];
    for (int i = 0; i < cases; ++i) {
        const std::uint64_t seed = ctx.rng().Next64();
        const std::size_t resultAt = 4 * ctx.rng().Below(8);
        msvc6::String* got[2] = {};
        msvc6::String* eax[2] = {};
        for (int side = 0; side < 2; ++side) {
            test::Rng rng(seed);
            rng.Fill(objects[side], sizeof(objects[side]));
            rng.Fill(results[side], sizeof(results[side]));
            const auto self = reinterpret_cast<std::uintptr_t>(objects[side]);
            msvc6::WString* member = EditText(self);
            const std::uint32_t length = rng.Chance(1, 6) ? 0 : rng.Below(40);
            blocks[side][0] = 0;
            for (std::uint32_t k = 0; k < length; ++k) {
                blocks[side][1 + k] = static_cast<std::uint16_t>(0x20 + rng.Below(0xe0));
            }
            blocks[side][1 + length] = 0;
            member->ptr = length == 0 && rng.Chance(1, 2) ? nullptr : blocks[side] + 1;
            member->len = length;
            member->res = length;
            got[side] = reinterpret_cast<msvc6::String*>(results[side] + resultAt);
            eax[side] = side == 0 ? Original_CEditControl_GetText(self, nullptr, got[side])
                                  : candidate(self, nullptr, got[side]);
        }
        bool same = S3RE_EXPECT(eax[0] == got[0] && eax[1] == got[1]) &&
                    // The difference wraps as a byte (int promotion would make 0x14 and -0xec differ).
                    S3RE_EXPECT_EQ(static_cast<std::uint8_t>(got[0]->allocator - static_cast<std::uint8_t>(
                                       reinterpret_cast<std::uintptr_t>(results[0]))),
                                   static_cast<std::uint8_t>(got[1]->allocator - static_cast<std::uint8_t>(
                                       reinterpret_cast<std::uintptr_t>(results[1])))) &&
                    S3RE_EXPECT_EQ(got[0]->len, got[1]->len) && S3RE_EXPECT_EQ(got[0]->res, got[1]->res) &&
                    S3RE_EXPECT_EQ(got[0]->ptr == nullptr, got[1]->ptr == nullptr);
        if (same && got[0]->ptr != nullptr) {
            same = S3RE_EXPECT_EQ(NarrowKit::Count(got[0]), NarrowKit::Count(got[1])) &&
                   S3RE_EXPECT_MEM_EQ(got[0]->ptr, got[1]->ptr, got[0]->len + 1);
        }
        NarrowKit::Destroy(got[0]);
        NarrowKit::Destroy(got[1]);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

msvc6::String* __fastcall WrongEditGetText(std::uintptr_t self, void* edx, msvc6::String* result) {
    CEditControl_GetText(self, edx, result);
    result->allocator = 0;  // wrong: not the address's low byte
    return result;
}

}  // namespace

S3RE_TEST(UiTextGetters_MatchOriginal) {
    CompareGetText(ctx, &CTextView_GetText, 3000);
    CompareGetTextW(ctx, &CEditControl_GetTextW, 3000);
    CompareEditGetText(ctx, &CEditControl_GetText, 3000);
}

S3RE_TEST(UiTextGetters_EditGetTextDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 3, /*quiet=*/true);
    CompareEditGetText(probe, &WrongEditGetText, 300);
    S3RE_EXPECT(probe.failures() > 0);
}


S3RE_TEST(UiTextGetters_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareGetText(probe, &WrongGetText, 300);
    S3RE_EXPECT(probe.failures() > 0);
    test::Context probe2(ctx.name(), 2, /*quiet=*/true);
    CompareGetTextW(probe2, &WrongGetTextW, 300);
    S3RE_EXPECT(probe2.failures() > 0);
}

}  // namespace s3re::game
