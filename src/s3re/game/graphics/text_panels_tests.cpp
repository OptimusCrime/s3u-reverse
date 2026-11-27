// Tests of text_panels.cpp (the drawer's panel texts).
//
// ClearTextRect / BlitTextRect run on a random 1-bit surface (pitch 1..24
// bytes, 48 rows) with random rects inside it (also empty ones and ones
// whose end wraps); Lock / Unlock are ours (verified) with FakeGdi's
// GdiFlush, DrawGlyphRow is a recording stub. Compared: the bits, the
// surface object and the calls.
//
// The panel helpers run with everything they call stubbed (ClearTextRect,
// BlitTextRect, CGraphicsData::TextAddress, CTextSurface::DrawText and
// DrawTextF): compared are the calls in order (the rect pointer relative to
// the drawer, texts by contents, the variable argument where the helper
// passes one), the drawer's rect fields and EAX (PanelHundredths). The
// cell size and the drawing origin are random per case; PanelHundredths
// formats with the real sprintf; PanelString's by-value string is freed
// by the helper (sometimes shared, so the split runs).

#include "game/globals.h"
#include "game/graphics/draw_data_layout.h"
#include "game/graphics/drawer_layout.h"
#include "game/graphics/text_surface.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/fake_gdi.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using msvc6::String;

void __fastcall CDrawer_ClearTextRect(CDrawer* self, void* edx, std::uint32_t x, std::uint32_t y, std::uint32_t width,
                                      std::uint32_t height);
void __fastcall CDrawer_BlitTextRect(CDrawer* self, void* edx, std::uint32_t x, std::uint32_t y, std::uint32_t width,
                                     std::uint32_t height);
void __fastcall CDrawer_PanelText(CDrawer* self, void* edx, std::int32_t row, std::int32_t lines, std::uint32_t id);
void __fastcall CDrawer_PanelTextFont1(CDrawer* self, void* edx, std::int32_t row, std::int32_t lines,
                                       std::uint32_t id);
void __fastcall CDrawer_PanelTextLeftHalf(CDrawer* self, void* edx, std::int32_t row, std::int32_t lines,
                                          std::uint32_t id);
void __fastcall CDrawer_PanelTextRightHalf(CDrawer* self, void* edx, std::int32_t row, std::int32_t lines,
                                           std::uint32_t id);
void __fastcall CDrawer_PanelTextArg(CDrawer* self, void* edx, std::int32_t row, std::int32_t lines,
                                     std::uint32_t id, std::uint32_t argument);
void __fastcall CDrawer_PanelTextIndentedArg(CDrawer* self, void* edx, std::int32_t row, std::int32_t lines,
                                             std::uint32_t id, std::uint32_t argument);
void __fastcall CDrawer_PanelTextAt(CDrawer* self, void* edx, std::int32_t row, std::int32_t column,
                                    std::int32_t lines, std::uint32_t id);
void __fastcall CDrawer_PanelTextAtFont1(CDrawer* self, void* edx, std::int32_t row, std::int32_t column,
                                         std::int32_t lines, std::uint32_t id);
void __fastcall CDrawer_PanelTextAtArg(CDrawer* self, void* edx, std::int32_t row, std::int32_t column,
                                       std::int32_t lines, std::uint32_t id, std::uint32_t argument);
void __fastcall CDrawer_PanelNumber(CDrawer* self, void* edx, std::int32_t column, std::int32_t row,
                                    std::uint32_t value);
void __fastcall CDrawer_PanelPercent(CDrawer* self, void* edx, std::int32_t column, std::int32_t row,
                                     std::uint32_t value);
std::uint32_t __fastcall CDrawer_PanelHundredths(CDrawer* self, void* edx, std::uint32_t font, std::int32_t x,
                                                 std::int32_t y, std::uint32_t value);
void __fastcall CDrawer_PanelString(CDrawer* self, void* edx, std::int32_t row, std::int32_t lines, String text);
void __fastcall CDrawer_PanelCaption(CDrawer* self, void* edx, std::int32_t row, const char* text);
void __fastcall CDrawer_InitPanelFonts(CDrawer* self, void* edx);
void __fastcall CDrawer_RenderMessageTexts(CDrawer* self, void* edx, const DrawDataView* data);
void __fastcall CDrawer_BlitMessageTexts(CDrawer* self, void* edx, const DrawDataView* data);
using BlitMessageTextsFn = decltype(&CDrawer_BlitMessageTexts);
extern BlitMessageTextsFn Original_CDrawer_BlitMessageTexts;
using RenderMessageTextsFn = decltype(&CDrawer_RenderMessageTexts);
extern RenderMessageTextsFn Original_CDrawer_RenderMessageTexts;
using InitPanelFontsFn = decltype(&CDrawer_InitPanelFonts);
extern InitPanelFontsFn Original_CDrawer_InitPanelFonts;

using TextRectFn = decltype(&CDrawer_ClearTextRect);
using PanelTextFn = decltype(&CDrawer_PanelText);
using PanelTextArgFn = decltype(&CDrawer_PanelTextArg);
using PanelTextAtFn = decltype(&CDrawer_PanelTextAt);
using PanelTextAtArgFn = decltype(&CDrawer_PanelTextAtArg);
using PanelNumberFn = decltype(&CDrawer_PanelNumber);
using PanelHundredthsFn = decltype(&CDrawer_PanelHundredths);
using PanelStringFn = decltype(&CDrawer_PanelString);
using PanelCaptionFn = decltype(&CDrawer_PanelCaption);
extern TextRectFn Original_CDrawer_ClearTextRect;
extern TextRectFn Original_CDrawer_BlitTextRect;
extern PanelTextFn Original_CDrawer_PanelText;
extern PanelTextFn Original_CDrawer_PanelTextFont1;
extern PanelTextFn Original_CDrawer_PanelTextLeftHalf;
extern PanelTextFn Original_CDrawer_PanelTextRightHalf;
extern PanelTextArgFn Original_CDrawer_PanelTextArg;
extern PanelTextArgFn Original_CDrawer_PanelTextIndentedArg;
extern PanelTextAtFn Original_CDrawer_PanelTextAt;
extern PanelTextAtFn Original_CDrawer_PanelTextAtFont1;
extern PanelTextAtArgFn Original_CDrawer_PanelTextAtArg;
extern PanelNumberFn Original_CDrawer_PanelNumber;
extern PanelNumberFn Original_CDrawer_PanelPercent;
extern PanelHundredthsFn Original_CDrawer_PanelHundredths;
extern PanelStringFn Original_CDrawer_PanelString;
extern PanelCaptionFn Original_CDrawer_PanelCaption;

namespace {

constexpr std::uintptr_t kDrawGlyphRow = 0x004d6950;
constexpr std::uintptr_t kClearTextRect = 0x004d65e0;
constexpr std::uintptr_t kBlitTextRect = 0x004d6740;
constexpr std::uintptr_t kTextAddress = 0x005faaa0;
constexpr std::uintptr_t kDrawText = 0x004a8090;
constexpr std::uintptr_t kDrawTextF = 0x004a87d0;

constexpr std::uint32_t kRows = 48;
constexpr std::uint32_t kMaxPitch = 24;

std::uint32_t U(const void* p) {
    return reinterpret_cast<std::uint32_t>(p);
}

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

// One drawer for all cases (only the fields the helpers use matter).
CDrawer* SharedDrawer() {
    static std::vector<std::uint8_t> bytes(sizeof(CDrawer));
    return reinterpret_cast<CDrawer*>(bytes.data());
}

// ---------------------------------------------------------------------------
// ClearTextRect / BlitTextRect
// ---------------------------------------------------------------------------

struct SurfaceSide {
    CTextSurface surface;
    std::uint8_t pad[4];
    std::uint8_t bits[kMaxPitch * kRows + 8];
    std::vector<std::uint32_t> log;
};

void RunRect(TextRectFn fn, CDrawer* drawer, SurfaceSide& side, std::uint32_t x, std::uint32_t y, std::uint32_t w,
             std::uint32_t h, std::uint64_t seed) {
    test::FakeGdi gdi(seed);
    test::CalleeStubs stubs;
    stubs.SetWorld(reinterpret_cast<CGameData*>(drawer));
    stubs.Install<3, kDrawGlyphRow>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        side.log.insert(side.log.end(), {0x9e, self, a[0], a[1], a[2]});
        return a[1];
    });
    side.surface.bits = side.bits;
    drawer->text_surface = &side.surface;
    gdi.Install();
    fn(drawer, nullptr, x, y, w, h);
    gdi.Uninstall();
    side.log.insert(side.log.end(), gdi.log().begin(), gdi.log().end());
}

void CompareRects(test::Context& ctx, TextRectFn original, TextRectFn candidate, int cases) {
    CDrawer* const drawer = SharedDrawer();
    for (int i = 0; i < cases; ++i) {
        test::Rng& rng = ctx.rng();
        SurfaceSide a{}, b{};
        rng.Fill(&a.surface, sizeof(a.surface));
        const std::uint32_t pitch = 1 + rng.Below(kMaxPitch);
        a.surface.pitch = pitch;
        rng.Fill(a.bits, sizeof(a.bits));
        if (rng.Chance(1, 3)) {
            for (std::uint8_t& v : a.bits) {
                v = rng.Chance(1, 2) ? 0 : v;  // more empty bytes (Blit skips them)
            }
        }
        b.surface = a.surface;
        std::memcpy(b.bits, a.bits, sizeof(a.bits));
        const std::uint32_t span = pitch * 8;
        std::uint32_t x = rng.Below(span);
        std::uint32_t w = 1 + rng.Below(span - x);
        std::uint32_t y = rng.Below(kRows);
        std::uint32_t h = rng.Below(kRows - y + 1);
        const std::uint32_t kind = rng.Below(10);
        if (kind == 0 && x >= 8) {
            w = 0;  // ends one pixel left of x: same byte, or the byte before
        } else if (kind == 1) {
            y = 4 + rng.Below(kRows - 4);
            h = 0xffffffffu - rng.Below(4);  // y + height wraps to below y: nothing is drawn
        } else if (kind == 2) {
            w = 8 - (x & 7);  // up to a byte boundary
        }
        const std::uint64_t seed = rng.Next64();
        RunRect(original, drawer, a, x, y, w, h, seed);
        RunRect(candidate, drawer, b, x, y, w, h, seed);
        bool same = S3RE_EXPECT_MEM_EQ(a.bits, b.bits, sizeof(a.bits));
        b.surface.bits = a.surface.bits;
        same = S3RE_EXPECT_MEM_EQ(&a.surface, &b.surface, sizeof(a.surface)) && same;
        same = S3RE_EXPECT(a.log == b.log) && same;
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: x %u, y %u, w %u, h %u, pitch %u", i, x, y, w, h, pitch);
            break;
        }
    }
}

// Wrong: the right mask one pixel short.
void __fastcall WrongClearTextRect(CDrawer* self, void* edx, std::uint32_t x, std::uint32_t y, std::uint32_t width,
                                   std::uint32_t height) {
    CDrawer_ClearTextRect(self, edx, x, y, width > 1 ? width - 1 : width, height);
}

// ---------------------------------------------------------------------------
// The panel helpers
// ---------------------------------------------------------------------------

enum class Helper {
    Text, TextFont1, LeftHalf, RightHalf, TextArg, IndentedArg, TextAt, TextAtFont1, TextAtArg,
    Number, Percent, Hundredths, String, Caption, Count
};

struct PanelFunctions {
    PanelTextFn text, textFont1, leftHalf, rightHalf;
    PanelTextArgFn textArg, indentedArg;
    PanelTextAtFn textAt, textAtFont1;
    PanelTextAtArgFn textAtArg;
    PanelNumberFn number, percent;
    PanelHundredthsFn hundredths;
    PanelStringFn string;
    PanelCaptionFn caption;
};

PanelFunctions OriginalPanels() {
    return {Original_CDrawer_PanelText,        Original_CDrawer_PanelTextFont1,
            Original_CDrawer_PanelTextLeftHalf, Original_CDrawer_PanelTextRightHalf,
            Original_CDrawer_PanelTextArg,     Original_CDrawer_PanelTextIndentedArg,
            Original_CDrawer_PanelTextAt,      Original_CDrawer_PanelTextAtFont1,
            Original_CDrawer_PanelTextAtArg,   Original_CDrawer_PanelNumber,
            Original_CDrawer_PanelPercent,     Original_CDrawer_PanelHundredths,
            Original_CDrawer_PanelString,      Original_CDrawer_PanelCaption};
}

const PanelFunctions kOurPanels{&CDrawer_PanelText,        &CDrawer_PanelTextFont1,  &CDrawer_PanelTextLeftHalf,
                                &CDrawer_PanelTextRightHalf, &CDrawer_PanelTextArg,    &CDrawer_PanelTextIndentedArg,
                                &CDrawer_PanelTextAt,      &CDrawer_PanelTextAtFont1, &CDrawer_PanelTextAtArg,
                                &CDrawer_PanelNumber,      &CDrawer_PanelPercent,    &CDrawer_PanelHundredths,
                                &CDrawer_PanelString,      &CDrawer_PanelCaption};

// The DrawTextF stub (cdecl, variadic: it reads one variable argument and
// logs it only when the helper passes one).
std::vector<std::uint32_t>* g_panelLog = nullptr;
CDrawer* g_panelDrawer = nullptr;
bool g_panelVararg = false;

TextRect* __cdecl StubDrawTextF(void* self, TextRect* result, std::int32_t font, const TextRect* rect,
                                std::uint32_t flags, const char* format, std::uint32_t argument) {
    const auto length = static_cast<std::uint32_t>(std::strlen(format));
    g_panelLog->insert(g_panelLog->end(), {0xdf, U(self), result != nullptr ? 1u : 0u, static_cast<std::uint32_t>(font),
                                           U(rect) - U(g_panelDrawer), flags, length, HashText(format, length),
                                           g_panelVararg ? argument : 0u});
    *result = {1, 2, 3, 4};
    return result;
}

const char* const kGameTexts[] = {"Holz", "%u Steine", "", "Bauplatz\nfrei", "%d%%", "Kohle: %u"};

struct PanelSide {
    std::vector<std::uint32_t> log;
    std::int32_t rect[4];
    std::uint32_t eax = 0;
    std::uint8_t sharedCount = 0;  // PanelString: the count byte of the caller's buffer afterwards
};

struct PanelCall {
    Helper helper;
    std::uint32_t a0, a1, a2, a3, a4;
    const char* caption;
    const char* stringText;
    std::uint8_t stringCount;
};

void RunPanel(const PanelFunctions& f, const PanelCall& c, PanelSide& side, const std::uint8_t* drawerFields) {
    CDrawer* const drawer = SharedDrawer();
    std::memcpy(&drawer->panel_text_left, drawerFields, 16);
    drawer->graphics = reinterpret_cast<void*>(0x6a000);
    drawer->text_surface = reinterpret_cast<void*>(0x75000);
    test::CalleeStubs stubs;
    stubs.SetWorld(reinterpret_cast<CGameData*>(0));
    stubs.Install<4, kClearTextRect>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        side.log.insert(side.log.end(), {0xc1, self - U(drawer), a[0], a[1], a[2], a[3]});
        return 0u;
    });
    stubs.Install<4, kBlitTextRect>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        side.log.insert(side.log.end(), {0xb1, self - U(drawer), a[0], a[1], a[2], a[3]});
        return 0u;
    });
    stubs.Install<2, kTextAddress>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        side.log.insert(side.log.end(), {0x7a, self, a[0], a[1]});
        return U(kGameTexts[a[0] % 6]);
    });
    stubs.Install<5, kDrawText>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        const auto* text = reinterpret_cast<const String*>(a[4]);
        side.log.insert(side.log.end(), {0xd7, self, a[0] != 0 ? 1u : 0u, a[1], a[2] - U(drawer), a[3], text->len,
                                         HashText(text->ptr, text->len)});
        return a[0];
    });
    g_panelLog = &side.log;
    g_panelDrawer = drawer;
    g_panelVararg = c.helper == Helper::TextArg || c.helper == Helper::IndentedArg || c.helper == Helper::TextAtArg ||
                    c.helper == Helper::Number || c.helper == Helper::Percent;
    test::ScopedJump drawTextF(kDrawTextF, reinterpret_cast<const void*>(&StubDrawTextF));
    const auto i = [](std::uint32_t v) { return static_cast<std::int32_t>(v); };
    switch (c.helper) {
    case Helper::Text: f.text(drawer, nullptr, i(c.a0), i(c.a1), c.a2); break;
    case Helper::TextFont1: f.textFont1(drawer, nullptr, i(c.a0), i(c.a1), c.a2); break;
    case Helper::LeftHalf: f.leftHalf(drawer, nullptr, i(c.a0), i(c.a1), c.a2); break;
    case Helper::RightHalf: f.rightHalf(drawer, nullptr, i(c.a0), i(c.a1), c.a2); break;
    case Helper::TextArg: f.textArg(drawer, nullptr, i(c.a0), i(c.a1), c.a2, c.a3); break;
    case Helper::IndentedArg: f.indentedArg(drawer, nullptr, i(c.a0), i(c.a1), c.a2, c.a3); break;
    case Helper::TextAt: f.textAt(drawer, nullptr, i(c.a0), i(c.a1), i(c.a2), c.a3); break;
    case Helper::TextAtFont1: f.textAtFont1(drawer, nullptr, i(c.a0), i(c.a1), i(c.a2), c.a3); break;
    case Helper::TextAtArg: f.textAtArg(drawer, nullptr, i(c.a0), i(c.a1), i(c.a2), c.a3, c.a4); break;
    case Helper::Number: f.number(drawer, nullptr, i(c.a0), i(c.a1), c.a2); break;
    case Helper::Percent: f.percent(drawer, nullptr, i(c.a0), i(c.a1), c.a2); break;
    case Helper::Hundredths: side.eax = f.hundredths(drawer, nullptr, c.a0, i(c.a1), i(c.a2), c.a3); break;
    case Helper::String: {
        String text{};
        msvc6::String_Tidy(&text, nullptr, 0);
        msvc6::String_AssignChars(&text, nullptr, c.stringText, static_cast<std::uint32_t>(std::strlen(c.stringText)));
        char* const buffer = text.ptr;
        if (buffer != nullptr) {
            buffer[-1] = static_cast<char>(c.stringCount);
        }
        f.string(drawer, nullptr, i(c.a0), i(c.a1), text);
        if (buffer != nullptr && c.stringCount != 0) {
            // Shared: the helper split off its own copy and released one reference.
            side.sharedCount = static_cast<std::uint8_t>(buffer[-1]);
            msvc6::GameOperatorDelete(buffer - 1);
        }
        break;
    }
    case Helper::Caption: f.caption(drawer, nullptr, i(c.a0), c.caption); break;
    case Helper::Count: break;
    }
    std::memcpy(side.rect, &drawer->panel_text_left, 16);
    g_panelLog = nullptr;
}

const char* const kCaptions[] = {"Lager", "", "Ein langer Titel mit Wörtern", "%s"};

void ComparePanels(test::Context& ctx, const PanelFunctions& candidate, int cases) {
    const PanelFunctions original = OriginalPanels();
    const std::int32_t savedCell = g_uiCellSize;
    const std::uint32_t savedX = g_drawOriginX;
    const std::uint32_t savedY = g_drawOriginY;
    for (int n = 0; n < cases; ++n) {
        test::Rng& rng = ctx.rng();
        g_uiCellSize = rng.Chance(1, 8) ? static_cast<std::int32_t>(rng.Next32()) : rng.Range(3, 5);
        g_drawOriginX = rng.Chance(1, 8) ? rng.Next32() : rng.Below(800);
        g_drawOriginY = rng.Chance(1, 8) ? rng.Next32() : rng.Below(600);
        PanelCall c{};
        c.helper = static_cast<Helper>(rng.Below(static_cast<std::uint32_t>(Helper::Count)));
        auto small = [&rng]() { return rng.Chance(1, 8) ? rng.Next32() : rng.Below(60); };
        c.a0 = small();
        c.a1 = small();
        c.a2 = small();
        c.a3 = rng.Chance(1, 2) ? small() : rng.Next32();
        c.a4 = rng.Next32();
        if (c.helper == Helper::Hundredths) {
            const std::uint32_t r = rng.Below(4);
            c.a3 = r == 0 ? 100 * rng.Below(1000) : r == 1 ? 10 * rng.Below(10000) : r == 2 ? rng.Next32()
                                                                                              : rng.Below(100000);
        }
        c.caption = rng.Chance(1, 6) ? nullptr : kCaptions[rng.Below(4)];
        c.stringText = kCaptions[rng.Below(4)];
        c.stringCount = static_cast<std::uint8_t>(rng.Chance(1, 3) ? 1 + rng.Below(3) : 0);
        std::uint8_t fields[16];
        rng.Fill(fields, sizeof(fields));
        PanelSide a, b;
        RunPanel(original, c, a, fields);
        RunPanel(candidate, c, b, fields);
        bool same = S3RE_EXPECT(a.log == b.log);
        same = S3RE_EXPECT_MEM_EQ(a.rect, b.rect, sizeof(a.rect)) && same;
        same = S3RE_EXPECT_EQ(a.eax, b.eax) && same;
        same = S3RE_EXPECT_EQ(a.sharedCount, b.sharedCount) && same;
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: helper %d, args %u %u %u %u", n, static_cast<int>(c.helper), c.a0,
                     c.a1, c.a2, c.a3);
            break;
        }
    }
    g_uiCellSize = savedCell;
    g_drawOriginX = savedX;
    g_drawOriginY = savedY;
}

// ---------------------------------------------------------------------------
// InitPanelFonts: GetSetNumber and AddFont stubbed (faces by contents);
// compared: the calls and the drawer's fields +0x5f654..+0x5f6b4.
// ---------------------------------------------------------------------------

constexpr std::uintptr_t kSoundSetNumber = 0x005ff4e0;
constexpr std::uintptr_t kAddFont = 0x004a7df0;
constexpr std::size_t kPanelFieldsBegin = offsetof(CDrawer, panel_left);
constexpr std::size_t kPanelFieldsEnd = offsetof(CDrawer, unk05f6b4);

struct FontsSide {
    std::vector<std::uint32_t> log;
    std::uint8_t fields[kPanelFieldsEnd - kPanelFieldsBegin];
};

void RunInitFonts(InitPanelFontsFn fn, const std::uint8_t* fields, std::uint32_t soundSet, FontsSide& side) {
    CDrawer* const drawer = SharedDrawer();
    auto* const bytes = reinterpret_cast<std::uint8_t*>(drawer);
    std::memcpy(bytes + kPanelFieldsBegin, fields, sizeof(side.fields));
    drawer->sound = reinterpret_cast<void*>(0x50000);
    drawer->text_surface = reinterpret_cast<void*>(0x75000);
    test::CalleeStubs stubs;
    stubs.SetWorld(reinterpret_cast<CGameData*>(0));
    stubs.Install<0, kSoundSetNumber>([&](CGameData*, std::uint32_t self, const std::uint32_t*) {
        side.log.insert(side.log.end(), {0x55, self});
        return soundSet;
    });
    stubs.Install<5, kAddFont>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        const auto* face = reinterpret_cast<const String*>(a[1]);
        side.log.insert(side.log.end(), {0xaf, self, a[0], face->len, HashText(face->ptr, face->len),
                                         face->ptr != nullptr ? static_cast<std::uint32_t>(face->ptr[-1]) : 0xffffu,
                                         a[2], a[3], a[4]});
        return 0u;
    });
    fn(drawer, nullptr);
    std::memcpy(side.fields, bytes + kPanelFieldsBegin, sizeof(side.fields));
}

void CompareInitFonts(test::Context& ctx, InitPanelFontsFn candidate, int cases) {
    const std::int32_t savedCell = g_uiCellSize;
    const std::uint32_t savedX = g_drawOriginX;
    const std::uint32_t savedY = g_drawOriginY;
    for (int n = 0; n < cases; ++n) {
        test::Rng& rng = ctx.rng();
        g_uiCellSize = rng.Chance(1, 8) ? static_cast<std::int32_t>(rng.Next32()) : rng.Range(3, 5);
        g_drawOriginX = rng.Chance(1, 8) ? rng.Next32() : rng.Below(800);
        g_drawOriginY = rng.Chance(1, 8) ? rng.Next32() : rng.Below(600);
        const std::uint32_t soundSet = rng.Chance(1, 2) ? 4 : rng.Below(8);
        std::uint8_t fields[kPanelFieldsEnd - kPanelFieldsBegin];
        rng.Fill(fields, sizeof(fields));
        FontsSide a, b;
        RunInitFonts(Original_CDrawer_InitPanelFonts, fields, soundSet, a);
        RunInitFonts(candidate, fields, soundSet, b);
        bool same = S3RE_EXPECT(a.log == b.log);
        same = S3RE_EXPECT_MEM_EQ(a.fields, b.fields, sizeof(a.fields)) && same;
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: sound set %u", n, soundSet);
            break;
        }
    }
    g_uiCellSize = savedCell;
    g_drawOriginX = savedX;
    g_drawOriginY = savedY;
}

// Wrong: the panel one cell narrower.
void __fastcall WrongInitPanelFonts(CDrawer* self, void* edx) {
    CDrawer_InitPanelFonts(self, edx);
    self->panel_right -= g_uiCellSize;
}

// ---------------------------------------------------------------------------
// RenderMessageTexts: ClearTextRect and DrawWideText stubbed (the rect by
// contents, the text relative to the draw data); random shown flags, cell
// size and display height.
// ---------------------------------------------------------------------------

constexpr std::uintptr_t kDrawWideText = 0x004a8a20;

DrawDataView* SharedDrawData() {
    static std::vector<std::uint8_t> bytes(sizeof(DrawDataView));
    return reinterpret_cast<DrawDataView*>(bytes.data());
}

std::vector<std::uint32_t> RunMessages(RenderMessageTextsFn fn) {
    CDrawer* const drawer = SharedDrawer();
    const DrawDataView* const data = SharedDrawData();
    drawer->text_surface = reinterpret_cast<void*>(0x75000);
    std::vector<std::uint32_t> log;
    test::CalleeStubs stubs;
    stubs.SetWorld(reinterpret_cast<CGameData*>(0));
    stubs.Install<4, kClearTextRect>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.insert(log.end(), {0xc1, self - U(drawer), a[0], a[1], a[2], a[3]});
        return 0u;
    });
    stubs.Install<5, kDrawWideText>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        const auto* rect = reinterpret_cast<const std::uint32_t*>(a[2]);
        log.insert(log.end(), {0xd3, self, a[0] != 0 ? 1u : 0u, a[1], rect[0], rect[1], rect[2], rect[3], a[3],
                               a[4] - U(data)});
        return a[0];
    });
    fn(drawer, nullptr, data);
    return log;
}

void CompareMessages(test::Context& ctx, RenderMessageTextsFn candidate, int cases) {
    DrawDataView* const data = SharedDrawData();
    const std::int32_t savedCell = g_uiCellSize;
    const std::int32_t savedHeight = g_displayHeight;
    for (int n = 0; n < cases; ++n) {
        test::Rng& rng = ctx.rng();
        g_uiCellSize = rng.Chance(1, 8) ? static_cast<std::int32_t>(rng.Next32()) : rng.Range(3, 5);
        g_displayHeight = rng.Chance(1, 8) ? static_cast<std::int32_t>(rng.Next32()) : rng.Range(480, 768);
        rng.Fill(&data->message_header, sizeof(data->message_header) + sizeof(data->messages));
        data->message_header.shown = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : rng.Next32());
        for (DrawMessageLine& line : data->messages) {
            line.shown = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : rng.Next32());
            if (rng.Chance(1, 3)) {
                line.unk10 = 0;
            }
        }
        const std::vector<std::uint32_t> a = RunMessages(Original_CDrawer_RenderMessageTexts);
        const std::vector<std::uint32_t> b = RunMessages(candidate);
        if (!S3RE_EXPECT(a == b)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: %zu / %zu values", n, a.size(), b.size());
            break;
        }
    }
    g_uiCellSize = savedCell;
    g_displayHeight = savedHeight;
}

// ---------------------------------------------------------------------------
// BlitMessageTexts: BlitTextRect, the animation / palette getters and the
// two draw methods stubbed (the drawn GfxDword by its value relative to the
// image list, the position and clip by contents); Point16, GfxDword's
// constructor, ListImage and wcslen run. Random shown flags, kinds, texts.
// ---------------------------------------------------------------------------

constexpr std::uintptr_t kAnimationPointer = 0x005faaf0;
constexpr std::uintptr_t kColourAnimationPointer = 0x005fab50;
constexpr std::uintptr_t kPalettePointer = 0x005fac10;
constexpr std::uintptr_t kDrawSprite = 0x005fadb0;
constexpr std::uintptr_t kDrawColorIndexed = 0x005fc5f0;

std::vector<std::uint32_t> RunBlitMessages(BlitMessageTextsFn fn, std::uint8_t* list) {
    CDrawer* const drawer = SharedDrawer();
    const DrawDataView* const data = SharedDrawData();
    drawer->graphics = reinterpret_cast<void*>(0x6a000);
    std::vector<std::uint32_t> log;
    const std::uint32_t base = U(list);
    test::CalleeStubs stubs;
    stubs.SetWorld(reinterpret_cast<CGameData*>(0));
    auto out = [base](std::uint32_t address) {
        *reinterpret_cast<std::uint32_t*>(address) = base;
        return address;
    };
    stubs.Install<4, kBlitTextRect>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.insert(log.end(), {0xb1, self - U(drawer), a[0], a[1], a[2], a[3]});
        return 0u;
    });
    stubs.Install<2, kAnimationPointer>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.insert(log.end(), {0xa1, self, a[1]});
        return out(a[0]);
    });
    stubs.Install<2, kColourAnimationPointer>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.insert(log.end(), {0xa2, self, a[1]});
        return out(a[0]);
    });
    stubs.Install<2, kPalettePointer>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.insert(log.end(), {0xa3, self, a[0], a[1]});
        return a[0] * 3u;
    });
    auto drawn = [&](std::uint32_t tag, std::uint32_t self, const std::uint32_t* a, std::uint32_t extra) {
        const auto* clip = reinterpret_cast<const std::uint32_t*>(a[2]);
        log.insert(log.end(), {tag, *reinterpret_cast<const std::uint32_t*>(self) - base, a[0] - U(drawer),
                               *reinterpret_cast<const std::uint32_t*>(a[1]), clip[0], clip[1], clip[2], clip[3],
                               a[3], extra});
        return 0u;
    };
    stubs.Install<5, kDrawSprite>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        return drawn(0xd5, self, a, a[4]);
    });
    stubs.Install<4, kDrawColorIndexed>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        return drawn(0xdc, self, a, 0);
    });
    fn(drawer, nullptr, data);
    return log;
}

void CompareBlitMessages(test::Context& ctx, BlitMessageTextsFn candidate, int cases) {
    DrawDataView* const data = SharedDrawData();
    const std::int32_t savedCell = g_uiCellSize;
    const std::int32_t savedHeight = g_displayHeight;
    const std::int32_t savedWidth = g_displayWidth;
    std::uint8_t list[64];
    std::uint16_t texts[kDrawMessageLines + 1][8];
    for (int n = 0; n < cases; ++n) {
        test::Rng& rng = ctx.rng();
        g_uiCellSize = rng.Chance(1, 8) ? static_cast<std::int32_t>(rng.Next32()) : rng.Range(3, 5);
        g_displayHeight = rng.Chance(1, 8) ? static_cast<std::int32_t>(rng.Next32()) : rng.Range(480, 768);
        g_displayWidth = rng.Range(640, 1024);
        rng.Fill(list, sizeof(list));
        const std::uint32_t first = rng.Below(32);
        std::memcpy(list + 8, &first, 4);
        rng.Fill(texts, sizeof(texts));
        rng.Fill(&data->message_header, sizeof(data->message_header) + sizeof(data->messages));
        DrawMessageHeader& head = data->message_header;
        head.shown = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : rng.Next32());
        head.text.ptr = texts[0];
        head.text.len = rng.Chance(1, 2) ? 0 : rng.Below(8);
        if (rng.Chance(1, 2)) {
            head.unk10 = 0;
        }
        for (std::size_t k = 0; k < kDrawMessageLines; ++k) {
            DrawMessageLine& line = data->messages[k];
            line.text.ptr = texts[k + 1];
            line.shown = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : rng.Next32());
            if (rng.Chance(1, 3)) {
                line.unk10 = 0;
            }
            const std::uint32_t r = rng.Below(5);
            line.unk14 = r == 0 ? rng.Below(20) : r == 1 ? 0xfc + rng.Below(3) : r == 2 ? rng.Next32()
                                                                                        : rng.Below(300);
        }
        const std::vector<std::uint32_t> a = RunBlitMessages(Original_CDrawer_BlitMessageTexts, list);
        const std::vector<std::uint32_t> b = RunBlitMessages(candidate, list);
        if (!S3RE_EXPECT(a == b)) {
            std::size_t k = 0;
            while (k < a.size() && k < b.size() && a[k] == b[k]) {
                ++k;
            }
            ctx.Fail(__FILE__, __LINE__, "  case %d: %zu / %zu values, first difference %zu", n, a.size(), b.size(),
                     k);
            break;
        }
    }
    g_uiCellSize = savedCell;
    g_displayHeight = savedHeight;
    g_displayWidth = savedWidth;
}

// Wrong: the 0xfc icon without its 8 pixels.
void __fastcall WrongBlitMessageTexts(CDrawer* self, void* edx, const DrawDataView* data) {
    DrawDataView* const mutableData = const_cast<DrawDataView*>(data);
    std::uint32_t saved[kDrawMessageLines];
    for (std::size_t k = 0; k < kDrawMessageLines; ++k) {
        saved[k] = data->messages[k].unk14;
        if (saved[k] == 0xfc) {
            mutableData->messages[k].unk14 = 0xfd;
        }
    }
    CDrawer_BlitMessageTexts(self, edx, data);
    for (std::size_t k = 0; k < kDrawMessageLines; ++k) {
        mutableData->messages[k].unk14 = saved[k];
    }
}

// Wrong: lines 5 cells apart instead of 6.
void __fastcall WrongRenderMessageTexts(CDrawer* self, void* edx, const DrawDataView* data) {
    const std::int32_t saved = g_uiCellSize;
    CDrawer_RenderMessageTexts(self, edx, data);
    g_uiCellSize = saved;
    if (data->messages[1].shown != 0) {
        reinterpret_cast<TextRectFn>(kClearTextRect)(self, nullptr, 0, 0, 0, 0);
    }
}

// Wrong: PanelPercent's box one pixel narrower (like PanelNumber's).
void __fastcall WrongPanelPercent(CDrawer* self, void* edx, std::int32_t column, std::int32_t row,
                                  std::uint32_t value) {
    CDrawer_PanelNumber(self, edx, column, row, value);
}

}  // namespace

S3RE_TEST(TextRects_MatchOriginal) {
    CompareRects(ctx, Original_CDrawer_ClearTextRect, &CDrawer_ClearTextRect, 6000);
    CompareRects(ctx, Original_CDrawer_BlitTextRect, &CDrawer_BlitTextRect, 6000);
}

S3RE_TEST(TextRects_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareRects(probe, Original_CDrawer_ClearTextRect, &WrongClearTextRect, 1000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(TextPanels_MatchOriginal) {
    ComparePanels(ctx, kOurPanels, 8000);
}

S3RE_TEST(PanelFonts_MatchOriginal) {
    CompareInitFonts(ctx, &CDrawer_InitPanelFonts, 2000);
}

S3RE_TEST(PanelFonts_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareInitFonts(probe, &WrongInitPanelFonts, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(MessageTexts_MatchOriginal) {
    CompareMessages(ctx, &CDrawer_RenderMessageTexts, 3000);
}

S3RE_TEST(MessageTexts_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMessages(probe, &WrongRenderMessageTexts, 300);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(BlitMessageTexts_MatchOriginal) {
    CompareBlitMessages(ctx, &CDrawer_BlitMessageTexts, 3000);
}

S3RE_TEST(BlitMessageTexts_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareBlitMessages(probe, &WrongBlitMessageTexts, 300);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(TextPanels_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    PanelFunctions wrong = kOurPanels;
    wrong.percent = &WrongPanelPercent;
    ComparePanels(probe, wrong, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
