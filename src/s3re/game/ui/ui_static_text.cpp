// CStaticText (RTTI name; vtable 0x65615c), the text label of the menus and
// the tooltip: a CStaticControl (screen_element.cpp) with a text.
//
// Layout (beyond the CStaticControl part): +0x1f byte "visible?" (set to 1),
// +0x20 byte 0, +0x28 the text (std::string), +0x38 byte 0, +0x3c the font
// (0x24 when 0 is passed), +0x40 the alignment or style (5 by default; the
// tooltip sets 0xc).
//
// CStaticText::CStaticText (0x0041c3e0): (const RECT* rect, const string&
//   text, font) ret 0Ch: CStaticControl(rect); the text member is an inlined
//   copy of `text` (string(text, 0, npos)) whose allocator byte is the low
//   byte of `rect` (the compiler let the empty allocator share rect's stack
//   slot); the fields above. EAX = this.
// CStaticText_SetStyle (0x0041ca60, our name): (style) ret 4: +0x40 = style;
//   EAX = style.

#include "game/ui/screen_element_layout.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/string.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace s3re::game {

// screen_element.cpp
void* __fastcall CStaticControl_Construct(void* self, void* edx, const RECT* rect);

namespace {

using msvc6::String;
using ui::CStaticText;


CStaticText* TextOf(void* self) {
    return static_cast<CStaticText*>(self);
}

}  // namespace

using StaticTextCtorFn = void*(__fastcall*)(void* self, void* edx, const RECT* rect, const String* text,
                                            std::uint32_t font);
using SetStyleFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t style);

void* __fastcall CStaticText_Construct(void* self, void* edx, const RECT* rect, const String* text,
                                       std::uint32_t font);
std::uint32_t __fastcall CStaticText_SetStyle(void* self, void* edx, std::uint32_t style);

S3RE_HOOK(0x0041c3e0, CStaticText_Construct, StaticTextCtorFn, Original_CStaticText_Construct,
          CStaticText_Construct);

// Purpose: Constructor of a text label of the menus (also the tooltip's).
void* __fastcall CStaticText_Construct(void* self, void* /*edx*/, const RECT* rect, const String* text,
                                       std::uint32_t font) {
    CStaticControl_Construct(self, nullptr, rect);
    CStaticText* label = TextOf(self);
    label->unk20 = 0;
    const auto allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(rect));
    msvc6::String_FromSubstr(&label->text, nullptr, text, 0, msvc6::detail::Npos(), &allocator);
    label->unk38 = 0;
    label->font = font != 0 ? font : 0x24;
    label->style = 5;
    label->element.vtable = vtables::kCStaticText;
    label->element.flag1f = 1;
    return self;
}

S3RE_HOOK(0x0041ca60, CStaticText_SetStyle, SetStyleFn, Original_CStaticText_SetStyle, CStaticText_SetStyle);

// Purpose: Sets a text label's style (probably the alignment; the tooltip
// uses 0xc).
std::uint32_t __fastcall CStaticText_SetStyle(void* self, void* /*edx*/, std::uint32_t style) {
    TextOf(self)->style = style;
    return style;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Identical random object buffers (with guard bytes), a random text string
// per side (heap buffer with a random count byte, or none) and the same
// rectangle, font and style. Compared: EAX, every object byte (the text
// member as "shares the argument's buffer" plus its text) and the count
// bytes. The rectangle is the same object for both sides, so the allocator
// byte (its address's low byte) is compared too.

namespace {

void* GameNew(std::uint32_t size) {
    return reinterpret_cast<void*(__cdecl*)(std::uint32_t)>(0x0061efb9)(size);
}

String MakeText(std::uint32_t length, std::uint8_t count, const char* text, bool none) {
    String s{};
    s.allocator = static_cast<std::uint8_t>(length * 3);
    if (none) {
        return s;
    }
    auto* block = static_cast<char*>(GameNew(length + 2));
    block[0] = static_cast<char>(count);
    std::memcpy(block + 1, text, length);
    block[1 + length] = 0;
    s.ptr = block + 1;
    s.len = length;
    s.res = length;
    return s;
}

void CompareStaticText(test::Context& ctx, StaticTextCtorFn ctor, SetStyleFn setStyle, int cases) {
    constexpr std::size_t kGuard = 8, kObject = sizeof(CStaticText);
    std::uint8_t a[kGuard * 2 + kObject], b[sizeof(a)];
    char text[64];
    RECT rects[4];
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t length = ctx.rng().Chance(1, 8) ? 0 : ctx.rng().Below(60);
        for (std::uint32_t k = 0; k < length; ++k) {
            text[k] = static_cast<char>(1 + ctx.rng().Below(255));
        }
        static constexpr std::uint8_t kCounts[] = {0, 0, 1, 2, 0xfd, 0xfe, 0xff};
        const std::uint8_t count = kCounts[ctx.rng().Below(sizeof(kCounts))];
        const bool none = ctx.rng().Chance(1, 10);
        String textA = MakeText(length, count, text, none);
        String textB = MakeText(length, count, text, none);
        ctx.rng().Fill(rects, sizeof(rects));
        const RECT* rect = &rects[ctx.rng().Below(4)];
        const std::uint32_t font = ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Next32();
        const std::uint32_t style = ctx.rng().Next32();
        ctx.rng().Fill(a, sizeof(a));
        std::memcpy(b, a, sizeof(a));
        void* ra = Original_CStaticText_Construct(a + kGuard, nullptr, rect, &textA, font);
        void* rb = ctor(b + kGuard, nullptr, rect, &textB, font);
        bool same = S3RE_EXPECT(ra == a + kGuard && rb == b + kGuard);
        if (ctx.rng().Chance(1, 2)) {
            same &= S3RE_EXPECT_EQ(Original_CStaticText_SetStyle(a + kGuard, nullptr, style),
                                   setStyle(b + kGuard, nullptr, style));
        }
        String& sa = TextOf(a + kGuard)->text;
        String& sb = TextOf(b + kGuard)->text;
        same &= S3RE_EXPECT_EQ(sa.ptr == nullptr, sb.ptr == nullptr);
        same &= S3RE_EXPECT_EQ(sa.ptr != nullptr && sa.ptr == textA.ptr, sb.ptr != nullptr && sb.ptr == textB.ptr);
        if (same && sa.ptr != nullptr) {
            same &= S3RE_EXPECT_EQ(static_cast<std::uint8_t>(sa.ptr[-1]), static_cast<std::uint8_t>(sb.ptr[-1]));
            same &= S3RE_EXPECT_MEM_EQ(sa.ptr, sb.ptr, sa.len + 1);
        }
        if (same && textA.ptr != nullptr) {
            same &= S3RE_EXPECT_EQ(static_cast<std::uint8_t>(textA.ptr[-1]), static_cast<std::uint8_t>(textB.ptr[-1]));
        }
        sa.ptr = sb.ptr = nullptr;
        same &= S3RE_EXPECT_MEM_EQ(a, b, sizeof(a));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, length %u, count %u", i, length, count);
            return;
        }
    }
}

void* __fastcall WrongStaticText(void* self, void* edx, const RECT* rect, const String* text, std::uint32_t font) {
    CStaticText_Construct(self, edx, rect, text, font);
    TextOf(self)->text.allocator = 0;  // wrong: allocator byte not taken from the rectangle's address
    return self;
}

}  // namespace

S3RE_TEST(CStaticText_MatchOriginal) {
    CompareStaticText(ctx, &CStaticText_Construct, &CStaticText_SetStyle, 20000);
}

S3RE_TEST(CStaticText_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareStaticText(probe, &WrongStaticText, &CStaticText_SetStyle, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
