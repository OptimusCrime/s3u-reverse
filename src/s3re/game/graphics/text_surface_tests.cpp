// Tests of text_surface.cpp (Dirk::CTextSurface: Lock, Unlock, DrawText,
// DrawTextF).
//
// The GDI imports are test::FakeGdi with failures (every VERIFY check
// fails now and then), GetLastError is test::FakeWin32; the original runs
// on one surface and ours on a copy, with fakes from the same seed.
// Compared: EAX (relative to the result buffer), the surface bytes, the
// result rect, the call logs (texts by contents) and the thrown exception
// (Win32::ThrowWin32Error is real; CatchGameException records it). The
// font dictionary's find and insert (Watcom code, not ours) are recording
// stubs that return entries of a small fake table, find sometimes nothing.
// DrawTextF runs with DrawText stubbed: compared are the formatted text it
// passes (by contents), the other arguments and the copied result.

#include "game/graphics/text_surface.h"

#include "game/globals.h"
#include "hooks.h"
#include "msvc6/string.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/fake_gdi.h"
#include "test/fake_win32.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using LockFn = decltype(&CTextSurface_Lock);
using UnlockFn = decltype(&CTextSurface_Unlock);
using DrawTextFn = decltype(&CTextSurface_DrawText);
using DrawTextFFn = decltype(&CTextSurface_DrawTextF);
extern LockFn Original_CTextSurface_Lock;
extern UnlockFn Original_CTextSurface_Unlock;
extern DrawTextFn Original_CTextSurface_DrawText;
extern DrawTextFFn Original_CTextSurface_DrawTextF;

namespace {

using test::CaughtException;

constexpr std::size_t kSurfaceBytes = 0x94;
constexpr std::uintptr_t kFontsFind = 0x0060bab0;
constexpr std::uintptr_t kFontsInsert = 0x0060bbe0;
constexpr std::uintptr_t kDrawText = 0x004a8090;
constexpr std::uint32_t kFontEntries = 8;

struct Functions {
    LockFn lock;
    UnlockFn unlock;
    DrawTextFn drawText;
    DrawTextFFn drawTextF;
};

Functions OriginalFunctions() {
    return {Original_CTextSurface_Lock, Original_CTextSurface_Unlock, Original_CTextSurface_DrawText,
            Original_CTextSurface_DrawTextF};
}

const Functions kOurs{&CTextSurface_Lock, &CTextSurface_Unlock, &CTextSurface_DrawText, &CTextSurface_DrawTextF};

enum class Op { Lock, Unlock, DrawText, DrawTextF };

// One call: plain data only (the __try frame must not hold C++ objects).
struct Call {
    Op op;
    void* surface;
    TextRect* result;
    std::int32_t font;
    const TextRect* rect;
    std::uint32_t flags;
    const msvc6::String* text;
    const char* format;
    std::uint32_t a, b;
    const char* s;
};

std::uint32_t Dispatch(const Functions* f, const Call* c) {
    switch (c->op) {
    case Op::Lock: return f->lock(c->surface, nullptr);
    case Op::Unlock: f->unlock(c->surface, nullptr); return 0;
    case Op::DrawText:
        return reinterpret_cast<std::uint32_t>(f->drawText(c->surface, nullptr, c->result, c->font, c->rect, c->flags,
                                                           c->text));
    case Op::DrawTextF:
        return reinterpret_cast<std::uint32_t>(
            f->drawTextF(c->surface, c->result, c->font, c->rect, c->flags, c->format, c->a, c->s, c->b));
    }
    return 0;
}

struct Outcome {
    std::uint32_t eax = 0;
    CaughtException caught;
};

Outcome Run(const Functions* f, const Call* call) {
    Outcome out;
    __try {
        out.eax = Dispatch(f, call);
    } __except (test::CatchGameException(GetExceptionInformation(), &out.caught)) {
    }
    return out;
}

// FNV-1a of a text (null: a marker).
std::uint32_t HashText(const char* text, std::uint32_t length) {
    if (text == nullptr) {
        return 0xffffffffu;
    }
    std::uint32_t hash = 2166136261u;
    for (std::uint32_t i = 0; i < length; ++i) {
        hash = (hash ^ static_cast<std::uint8_t>(text[i])) * 16777619u;
    }
    return hash;
}

struct Side {
    std::uint8_t surface[kSurfaceBytes];
    std::uint32_t fonts[2 * kFontEntries];  // entries: HFONT, then the dword the pointer points at
    TextRect result;
    std::vector<std::uint32_t> log;
    Outcome outcome;
};

// Runs one operation on `side` with fakes from `seed`.
void RunSide(const Functions& f, Side& side, Call call, std::uint64_t seed, std::uint32_t findMisses,
             bool stubDrawText) {
    test::FakeGdi gdi(seed, /*failures=*/true);
    test::FakeWin32 win32(seed ^ 0x5eed, test::kFakeGetLastError);
    test::CalleeStubs stubs;
    stubs.SetWorld(reinterpret_cast<CGameData*>(side.surface));
    const std::uint32_t base = reinterpret_cast<std::uint32_t>(side.surface);
    std::uint32_t lookups = 0;
    auto entry = [&side](std::uint32_t key) {
        return reinterpret_cast<std::uint32_t>(&side.fonts[2 * (key % kFontEntries) + 1]);
    };
    stubs.Install<1, kFontsFind>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) -> std::uint32_t {
        const std::uint32_t key = *reinterpret_cast<const std::uint32_t*>(a[0]);
        side.log.insert(side.log.end(), {0xf1d, self, a[0] - base, key});
        return ((findMisses >> (lookups++ & 31)) & 1) != 0 ? 0 : entry(key);
    });
    stubs.Install<1, kFontsInsert>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) -> std::uint32_t {
        const std::uint32_t key = *reinterpret_cast<const std::uint32_t*>(a[0]);
        side.log.insert(side.log.end(), {0x1e5, self, a[0] - base, key});
        return entry(key + 1);
    });
    if (stubDrawText) {
        stubs.Install<5, kDrawText>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) -> std::uint32_t {
            const auto* text = reinterpret_cast<const msvc6::String*>(a[4]);
            side.log.insert(side.log.end(), {0xd7, self, a[0] == reinterpret_cast<std::uint32_t>(&side.result),
                                             a[1], a[2], a[3], text->len, HashText(text->ptr, text->len),
                                             text->ptr != nullptr ? static_cast<std::uint32_t>(text->ptr[text->len])
                                                                  : 0xffu});
            // Writes a rect into its own result buffer (a stack local of the caller).
            auto* out = reinterpret_cast<TextRect*>(a[0]);
            *out = {static_cast<std::int32_t>(a[1]), static_cast<std::int32_t>(a[3]), 7, -3};
            return a[0];
        });
    }
    gdi.Install();
    win32.Install();
    call.surface = side.surface;
    call.result = &side.result;
    side.outcome = Run(&f, &call);
    win32.Uninstall();
    gdi.Uninstall();
    if ((call.op == Op::DrawText || call.op == Op::DrawTextF) && !side.outcome.caught.threw) {
        // EAX is the result buffer: compare it relative to the side.
        side.outcome.eax -= reinterpret_cast<std::uint32_t>(&side.result);
    }
    side.log.insert(side.log.end(), gdi.log().begin(), gdi.log().end());
    side.log.insert(side.log.end(), win32.log().begin(), win32.log().end());
}

const char* const kFormats[] = {"", "Holz", "%d", "%u / %u", "%s: %d", "100%%", "%s", "Steine %d%%",
                                "%x %s %u", "lang\nzweizeilig"};
const char* const kArguments[] = {"", "Baumeister", "a", "Kohle und Eisen"};

void CompareTextSurface(test::Context& ctx, const Functions& candidate, int cases) {
    const Functions original = OriginalFunctions();
    char text[64];
    for (int i = 0; i < cases; ++i) {
        test::Rng& rng = ctx.rng();
        Side a{}, b{};
        rng.Fill(a.surface, sizeof(a.surface));
        rng.Fill(a.fonts, sizeof(a.fonts));
        rng.Fill(&a.result, sizeof(a.result));
        if (rng.Chance(1, 2)) {
            a.surface[0x90] = static_cast<std::uint8_t>(rng.Below(2));
        }
        std::memcpy(&b, &a, offsetof(Side, log));
        TextRect rect;
        rng.Fill(&rect, sizeof(rect));
        // Text: random characters, length up to 40; sometimes no buffer.
        const std::uint32_t length = rng.Below(41);
        for (std::uint32_t k = 0; k < sizeof(text); ++k) {
            text[k] = static_cast<char>(32 + rng.Below(95));
        }
        text[length] = 0;
        msvc6::String string{};
        string.ptr = rng.Chance(1, 8) ? nullptr : text;
        string.len = string.ptr != nullptr ? length : 0;
        string.res = string.len;

        Call call{};
        call.op = static_cast<Op>(rng.Below(4));
        call.font = rng.Chance(1, 4) ? static_cast<std::int32_t>(rng.Next32()) : rng.Range(0, 5);
        call.rect = &rect;
        call.flags = rng.Chance(1, 4) ? rng.Next32() : rng.Below(128);
        call.text = &string;
        call.format = kFormats[rng.Below(10)];
        call.a = rng.Chance(1, 2) ? rng.Below(1000) : rng.Next32();
        call.s = kArguments[rng.Below(4)];
        call.b = rng.Next32();
        if (call.format == kFormats[4] || call.format == kFormats[6]) {
            call.a = reinterpret_cast<std::uint32_t>(call.s);  // the first conversion is the %s
        }
        const std::uint32_t findMisses = rng.Next32();
        const std::uint64_t seed = rng.Next64();
        const bool stubDrawText = call.op == Op::DrawTextF;
        RunSide(original, a, call, seed, findMisses, stubDrawText);
        RunSide(candidate, b, call, seed, findMisses, stubDrawText);
        bool same = S3RE_EXPECT_EQ(a.outcome.eax, b.outcome.eax);
        same = S3RE_EXPECT(a.outcome.caught == b.outcome.caught) && same;
        same = S3RE_EXPECT_MEM_EQ(a.surface, b.surface, sizeof(a.surface)) && same;
        same = S3RE_EXPECT_MEM_EQ(a.fonts, b.fonts, sizeof(a.fonts)) && same;
        same = S3RE_EXPECT_MEM_EQ(&a.result, &b.result, sizeof(a.result)) && same;
        same = S3RE_EXPECT(a.log == b.log) && same;
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d, flags 0x%x, threw %d", i, static_cast<int>(call.op),
                     call.flags, a.outcome.caught.threw ? 1 : 0);
            break;
        }
    }
}

// Wrong: DT_VCENTER for bit 4 instead of bit 3.
TextRect* __fastcall WrongDrawText(void* self, void* edx, TextRect* result, std::int32_t font, const TextRect* rect,
                                   std::uint32_t flags, const msvc6::String* text) {
    const std::uint32_t moved = (flags & ~0x18u) | ((flags & 0x08u) << 1) | ((flags & 0x10u) >> 1);
    return CTextSurface_DrawText(self, edx, result, font, rect, moved, text);
}

}  // namespace

S3RE_TEST(TextSurface_MatchesOriginal) {
    CompareTextSurface(ctx, kOurs, 6000);
}

S3RE_TEST(TextSurface_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Functions wrong = kOurs;
    wrong.drawText = &WrongDrawText;
    CompareTextSurface(probe, wrong, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
