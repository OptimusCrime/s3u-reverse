// Dirk::CTextSurface (Dirk\Text.cpp): a 1-bit-per-pixel GDI DIB section the
// in-game panels render their texts into with DrawTextA; the drawer then
// copies the set bits to the screen surface as glyph rows (CDrawer's text
// helpers 0x4d65e0 / 0x4d6740, text_panels.cpp). Layout: CTextSurface below.
//
// Error checks: every Windows call is wrapped in the framework's VERIFY
// macro, which calls Win32::ThrowWin32Error (0x610de0; fastcall: ECX = the
// expression text, EDX = the source file, stack: line, GetLastError(),
// const std::string* message) when the expression is false. It never
// returns (it throws a Win32::CHResult); our code after such a call is
// never reached, like the original's. The message is an empty string built
// from "" (0x7a7868) through temporaries that never allocate; its
// allocator byte is the low byte of the rect pointer argument (DrawText,
// lines 0xa7..0xae), or an uninitialized stack byte (DrawText's DeleteDC
// check, line 0xb1, and Lock): those we leave 0 (the emulator spec doesn't
// compare it there).
//
// Dirk::CTextSurface::DrawText (0x004a8090): thiscall (TextRect* result,
//   font, const TextRect* rect, flags, const std::string* text) ret 14h.
//   Sets `busy`, converts the flags to a DrawTextA format (bit 0 ->
//   DT_CENTER, 2 -> DT_RIGHT, 3 -> DT_VCENTER, 5 -> DT_BOTTOM, 6 ->
//   DT_SINGLELINE, otherwise DT_WORDBREAK), makes a memory DC
//   (CreateCompatibleDC(0)), selects the bitmap, opaque white-on-black,
//   looks the font up in the font dictionary (find, else insert, through
//   the key/value slot at +0x7c), selects it, draws the text into a copy of
//   the rect, deletes the DC and clears `busy`. *result = the rect with
//   bottom = top + the height DrawTextA returned. EAX = result.
// Dirk::CTextSurface::DrawTextF (0x004a87d0): cdecl (this, TextRect* result,
//   font, rect, flags, format, ...): vsprintf into a 4096-byte buffer, a
//   std::string of it (through two temporaries), DrawText, the string's
//   inlined destructor. EAX = result.
// Dirk::CTextSurface::Lock (0x004a8bd0): thiscall (): GdiFlush (VERIFY,
//   line 0xd4), sets `busy`, EAX = the bits.
// Dirk::CTextSurface::Unlock (0x004a8e00): thiscall (): clears `busy`.

#include "game/graphics/text_surface.h"

#include "game/globals.h"
#include "game/tables.h"
#include "game/texts.h"
#include "hooks.h"
#include "msvc6/string.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>

namespace s3re::game {

namespace {

using msvc6::String;

constexpr std::uintptr_t kThrowWin32Error = 0x00610de0;
constexpr std::uintptr_t kFontsFind = 0x0060bab0;    // WCSkipNonTempBase::base_find(&key) -> entry + 4 or 0
constexpr std::uintptr_t kFontsInsert = 0x0060bbe0;  // WCSkipNonTempBase::base_insert(&pair) -> entry + 4 or 0
constexpr std::uintptr_t kDrawText = 0x004a8090;
constexpr std::uintptr_t kVsprintf = 0x00621408;     // the CRT's vsprintf

// Source lines of the VERIFY checks (Dirk\Text.cpp).
constexpr std::uint32_t kLineCreateDc = 0xa7;
constexpr std::uint32_t kLineSelectBitmap = 0xa9;
constexpr std::uint32_t kLineBkMode = 0xaa;
constexpr std::uint32_t kLineBkColor = 0xab;
constexpr std::uint32_t kLineTextColor = 0xac;
constexpr std::uint32_t kLineSelectFont = 0xae;
constexpr std::uint32_t kLineDeleteDc = 0xb1;
constexpr std::uint32_t kLineGdiFlush = 0xd4;

// Text flags of the callers and the DrawTextA format bits they become.
constexpr std::uint32_t kFlagCenter = 0x01;
constexpr std::uint32_t kFlagRight = 0x04;
constexpr std::uint32_t kFlagVCenter = 0x08;
constexpr std::uint32_t kFlagBottom = 0x20;
constexpr std::uint32_t kFlagSingleLine = 0x40;
constexpr std::uint32_t kDtCenter = 0x01;
constexpr std::uint32_t kDtRight = 0x02;
constexpr std::uint32_t kDtVCenter = 0x04;
constexpr std::uint32_t kDtBottom = 0x08;
constexpr std::uint32_t kDtWordBreak = 0x10;
constexpr std::uint32_t kDtSingleLine = 0x20;

constexpr std::int32_t kOpaque = 2;                 // SetBkMode's OPAQUE
constexpr std::uint32_t kBlack = 0x000000;          // RGB(0, 0, 0)
constexpr std::uint32_t kWhite = 0xffffff;          // RGB(255, 255, 255) (a colour, raw-ok)
constexpr std::uint32_t kClrInvalid = 0xffffffff;   // CLR_INVALID

constexpr std::size_t kFormatBuffer = 4096;

using ThrowWin32ErrorFn = void(__fastcall*)(const char* expression, const char* file, std::uint32_t line,
                                            std::uint32_t error, const String* message);
using LookupFn = std::uint32_t(__fastcall*)(void* dictionary, void* edx, void* item);
using DrawTextFn = decltype(&CTextSurface_DrawText);
using VsprintfFn = int(__cdecl*)(char* buffer, const char* format, va_list args);

CTextSurface* Surface(void* self) {
    return static_cast<CTextSurface*>(self);
}

// VERIFY failed: Win32::ThrowWin32Error with an empty message (see the
// header) and the current GetLastError(). Never returns.
void ThrowVerify(const char* expression, std::uint32_t line, std::uint8_t allocator) {
    String message{};
    message.allocator = allocator;
    const std::uint32_t error = g_importGetLastError.get()();
    reinterpret_cast<ThrowWin32ErrorFn>(kThrowWin32Error)(expression, texts::kTextFile, line, error, &message);
}

std::uint32_t DrawTextFormat(std::uint32_t flags) {
    std::uint32_t format = 0;
    if ((flags & kFlagCenter) != 0) {
        format = kDtCenter;
    }
    if ((flags & kFlagRight) != 0) {
        format |= kDtRight;
    }
    if ((flags & kFlagVCenter) != 0) {
        format |= kDtVCenter;
    }
    if ((flags & kFlagBottom) != 0) {
        format |= kDtBottom;
    }
    format |= (flags & kFlagSingleLine) != 0 ? kDtSingleLine : kDtWordBreak;
    return format;
}

// vFonts[font] (0x4a8601..0x4a8628): the dictionary's operator[] inlined:
// find the key, else insert the pair (key, its current value); the HFONT
// is the dword before the returned pointer. (An insert that fails returns
// 0 and the original then reads address 0: it would crash.)
std::uint32_t FontHandle(CTextSurface* surface, std::int32_t font) {
    void* const dictionary = surface->fonts;
    surface->font_key = font;
    std::uint32_t entry = reinterpret_cast<LookupFn>(kFontsFind)(dictionary, nullptr, &surface->font_key);
    if (entry == 0) {
        entry = reinterpret_cast<LookupFn>(kFontsInsert)(dictionary, nullptr, &surface->font_key);
    }
    if (entry != 0) {
        entry -= 4;
    }
    return *reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(entry));
}

// `target` = string(string(text)) as the original inlines it into an empty
// string: a temporary from the text, a copy of it, assign, free both
// (0x4a8835..0x4a88ec).
void AssignThroughTemporaries(String* target, const char* text) {
    String fromText{};
    msvc6::String_Tidy(&fromText, nullptr, 0);
    msvc6::String_AssignChars(&fromText, nullptr, text, msvc6::detail::Length(text));
    String copy{};
    msvc6::String_Tidy(&copy, nullptr, 0);
    msvc6::String_AssignSubstr(&copy, nullptr, &fromText, 0, 0xFFFFFFFFu);
    msvc6::String_AssignSubstr(target, nullptr, &copy, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(&copy, nullptr, 1);
    msvc6::String_Tidy(&fromText, nullptr, 1);
}

}  // namespace

using LockFn = decltype(&CTextSurface_Lock);
using UnlockFn = decltype(&CTextSurface_Unlock);
using DrawTextFFn = decltype(&CTextSurface_DrawTextF);

S3RE_HOOK(0x004a8bd0, CTextSurface_Lock, LockFn, Original_CTextSurface_Lock, CTextSurface_Lock);

// Purpose: gives the panel text code the surface's pixels after GDI has
// finished drawing into them (GdiFlush), and marks the surface busy.
std::uint32_t __fastcall CTextSurface_Lock(void* self, void* /*edx*/) {
    if (g_importGdiFlush.get()() == 0) {
        ThrowVerify(texts::kGdiFlush, kLineGdiFlush, 0);
    }
    CTextSurface* const surface = Surface(self);
    const auto bits = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(surface->bits));
    surface->busy = 1;
    return bits;
}

S3RE_HOOK(0x004a8e00, CTextSurface_Unlock, UnlockFn, Original_CTextSurface_Unlock, CTextSurface_Unlock);

// Purpose: ends a Lock (the panel text code is done with the pixels).
void __fastcall CTextSurface_Unlock(void* self, void* /*edx*/) {
    Surface(self)->busy = 0;
}

S3RE_HOOK(0x004a8090, CTextSurface_DrawText, DrawTextFn, Original_CTextSurface_DrawText, CTextSurface_DrawText);

// Purpose: renders a text with one of the surface's fonts into a rectangle
// of the 1-bit surface (white on black) and tells the caller how high the
// text came out.
TextRect* __fastcall CTextSurface_DrawText(void* self, void* /*edx*/, TextRect* result, std::int32_t font,
                                           const TextRect* rect, std::uint32_t flags, const String* text) {
    CTextSurface* const surface = Surface(self);
    TextRect area = *rect;
    surface->busy = 1;
    const std::uint32_t format = DrawTextFormat(flags);
    const auto allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(rect));

    void* const dc = g_importCreateCompatibleDC.get()(nullptr);
    if (dc == nullptr) {
        ThrowVerify(texts::kDcNotNull, kLineCreateDc, allocator);
    }
    if (g_importSelectObject.get()(dc, reinterpret_cast<void*>(surface->bitmap)) == nullptr) {
        ThrowVerify(texts::kSelectBitmap, kLineSelectBitmap, allocator);
    }
    if (g_importSetBkMode.get()(dc, kOpaque) == 0) {
        ThrowVerify(texts::kSetBkModeOpaque, kLineBkMode, allocator);
    }
    if (g_importSetBkColor.get()(dc, kBlack) == kClrInvalid) {
        ThrowVerify(texts::kSetBkColorBlack, kLineBkColor, allocator);
    }
    if (g_importSetTextColor.get()(dc, kWhite) == kClrInvalid) {
        ThrowVerify(texts::kSetTextColorWhite, kLineTextColor, allocator);
    }
    const std::uint32_t fontHandle = FontHandle(surface, font);
    if (g_importSelectObject.get()(dc, reinterpret_cast<void*>(fontHandle)) == nullptr) {
        ThrowVerify(texts::kSelectFont, kLineSelectFont, allocator);
    }
    const char* const characters = text->ptr != nullptr ? text->ptr : g_stringNullBuffer.get();
    const std::int32_t height =
        g_importDrawTextA.get()(dc, characters, static_cast<std::int32_t>(text->len), &area, format);
    if (g_importDeleteDC.get()(dc) == 0) {
        ThrowVerify(texts::kDeleteDc, kLineDeleteDc, 0);
    }
    surface->busy = 0;
    result->left = area.left;
    result->top = area.top;
    result->right = area.right;
    result->bottom = area.top + height;
    return result;
}

S3RE_HOOK(0x004a87d0, CTextSurface_DrawTextF, DrawTextFFn, Original_CTextSurface_DrawTextF, CTextSurface_DrawTextF);

// Purpose: DrawText with a printf-style text (the panels pass their texts
// as the format, with or without arguments).
TextRect* __cdecl CTextSurface_DrawTextF(void* self, TextRect* result, std::int32_t font, const TextRect* rect,
                                         std::uint32_t flags, const char* format, ...) {
    char buffer[kFormatBuffer];
    va_list args;
    va_start(args, format);
    reinterpret_cast<VsprintfFn>(kVsprintf)(buffer, format, args);
    va_end(args);

    String text{};
    msvc6::String_Tidy(&text, nullptr, 0);
    AssignThroughTemporaries(&text, buffer);
    TextRect drawn;
    const TextRect* const area = reinterpret_cast<DrawTextFn>(kDrawText)(self, nullptr, &drawn, font, rect, flags,
                                                                         &text);
    *result = *area;
    // The string's inlined destructor: erase(0, npos), then free.
    msvc6::detail::Erase(&text, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(&text, nullptr, 1);
    return result;
}

}  // namespace s3re::game
