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
// Dirk::CTextSurface::DrawWideText (0x004a8a20): thiscall (TextRect*
//   result, font, rect, flags, const std::wstring* text) ret 14h: the text
//   through WideToNarrowString (0x60b6a0) into a temporary, copied into a
//   string (allocator byte: the low byte of `result`), the temporary's
//   inlined destructor (_Split, erase, free), DrawText (0x4a8090), the
//   copy's inlined destructor. *result = DrawText's rect; EAX = result.
// Dirk::CTextSurface::Lock (0x004a8bd0): thiscall (): GdiFlush (VERIFY,
//   line 0xd4), sets `busy`, EAX = the bits.
// Dirk::CTextSurface::Unlock (0x004a8e00): thiscall (): clears `busy`.
// Dirk::CTextSurface::Create (0x004a78f0): thiscall (width, height) ret 8:
//   GetDC(0) (VERIFY "hDC != NULL", line 0x4c), a top-down 1-bit DIB
//   section (BITMAPINFOHEADER: width, -height, 1 plane, 1 bit; colours
//   black and white) by CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits
//   (+0x88), 0, 0) into +0x84 (VERIFY "hBitmap != NULL", line 0x4f),
//   ReleaseDC(0, dc); pitch (+0x8c) = 4 * (width / 32, plus 1 when width %
//   32 != 0; C's signed division). Both messages' allocator bytes are
//   uninitialized stack bytes. EAX = pitch / 4.
// Dirk::CTextSurface::AddFont (0x004a7df0): thiscall (font id, const
//   std::string* face, height, bold, italic) ret 14h: the default font
//   GetStockObject(DEFAULT_GUI_FONT) (VERIFY "hDefaultFont != NULL", line
//   0x73; the message's allocator byte is uninitialized), a zeroed LOGFONTA
//   with the face copied in (strcpy, unbounded as in the original), height,
//   weight 700 when the low byte of `bold` is set, italic = the low byte of
//   `italic`, charset SHIFTJIS (0x80) when GameConfig::Dword28 (0x4120a0,
//   probably the language) is 7, else DEFAULT_CHARSET; CreateFontIndirectA,
//   the default font if that fails; stored in the font dictionary under the
//   id (find, else insert; the HFONT is the dword before the returned
//   pointer; a failed insert writes to address 0, a crash in the original).
//   EAX = the dictionary's pointer.

#include "game/graphics/text_surface.h"

#include "game/globals.h"
#include "game/tables.h"
#include "game/texts.h"
#include "hooks.h"
#include "msvc6/string.h"
#include "msvc6/wstring.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

using msvc6::String;

constexpr std::uintptr_t kThrowWin32Error = 0x00610de0;
constexpr std::uintptr_t kFontsFind = 0x0060bab0;    // WCSkipNonTempBase::base_find(&key) -> entry + 4 or 0
constexpr std::uintptr_t kFontsInsert = 0x0060bbe0;  // WCSkipNonTempBase::base_insert(&pair) -> entry + 4 or 0
constexpr std::uintptr_t kDrawText = 0x004a8090;
constexpr std::uintptr_t kVsprintf = 0x00621408;     // the CRT's vsprintf
constexpr std::uintptr_t kWideToNarrow = 0x0060b6a0;  // WideToNarrowString (ECX = result, EDX = wide)

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

constexpr std::uint32_t kLineDefaultFont = 0x73;
constexpr std::uint32_t kLineGetDc = 0x4c;
constexpr std::uint32_t kLineCreateBitmap = 0x4f;
constexpr std::uint32_t kDibRgbColours = 0;          // DIB_RGB_COLORS
constexpr std::int32_t kBitsPerDword = 32;

struct OneBitInfo {              // BITMAPINFO with a two-colour table
    std::uint32_t size;          // +0x00 sizeof(BITMAPINFOHEADER) = 40
    std::int32_t width;
    std::int32_t height;         // negative: top-down
    std::uint16_t planes;
    std::uint16_t bit_count;
    std::uint32_t compression;
    std::uint32_t size_image;
    std::int32_t x_pels_per_meter;
    std::int32_t y_pels_per_meter;
    std::uint32_t colours_used;
    std::uint32_t colours_important;
    std::uint8_t colours[2][4];  // +0x28 RGBQUAD black, white
};
static_assert(sizeof(OneBitInfo) == 48);
constexpr std::int32_t kDefaultGuiFont = 17;         // GetStockObject's DEFAULT_GUI_FONT
constexpr std::int32_t kBoldWeight = 700;            // FW_BOLD
constexpr std::uint8_t kShiftJisCharset = 0x80;      // SHIFTJIS_CHARSET
constexpr std::uint8_t kDefaultCharset = 1;          // DEFAULT_CHARSET
constexpr std::uint32_t kJapanese = 7;               // GameConfig::Dword28 for the Japanese version (probably)
constexpr std::uintptr_t kConfigDword28 = 0x004120a0;

struct LogFont {                 // LOGFONTA
    std::int32_t height;         // +0x00
    std::int32_t width;
    std::int32_t escapement;
    std::int32_t orientation;
    std::int32_t weight;         // +0x10
    std::uint8_t italic;         // +0x14
    std::uint8_t underline;
    std::uint8_t strike_out;
    std::uint8_t charset;        // +0x17
    std::uint8_t out_precision;
    std::uint8_t clip_precision;
    std::uint8_t quality;
    std::uint8_t pitch_and_family;
    char face[32];               // +0x1c
};
static_assert(sizeof(LogFont) == 60);

using ThrowWin32ErrorFn = void(__fastcall*)(const char* expression, const char* file, std::uint32_t line,
                                            std::uint32_t error, const String* message);
using LookupFn = std::uint32_t(__fastcall*)(void* dictionary, void* edx, void* item);
using DrawTextFn = decltype(&CTextSurface_DrawText);
using VsprintfFn = int(__cdecl*)(char* buffer, const char* format, va_list args);
using ConfigGetFn = std::uint32_t(__fastcall*)(const void* self, void* edx);
using WideToNarrowFn = String*(__fastcall*)(String* result, const msvc6::WString* wide);

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

// The dictionary entry of a font id (find, else insert, through the key
// slot), as AddFont inlines it.
std::uint32_t FontEntry(CTextSurface* surface, std::int32_t font) {
    void* const dictionary = surface->fonts;
    surface->font_key = font;
    const std::uint32_t entry = reinterpret_cast<LookupFn>(kFontsFind)(dictionary, nullptr, &surface->font_key);
    if (entry != 0) {
        return entry;
    }
    return reinterpret_cast<LookupFn>(kFontsInsert)(dictionary, nullptr, &surface->font_key);
}

}  // namespace

using DrawWideTextFn = decltype(&CTextSurface_DrawWideText);

S3RE_HOOK(0x004a8a20, CTextSurface_DrawWideText, DrawWideTextFn, Original_CTextSurface_DrawWideText,
          CTextSurface_DrawWideText);

// Purpose: DrawText for a wide (16-bit) string, e.g. a chat message.
TextRect* __fastcall CTextSurface_DrawWideText(void* self, void* /*edx*/, TextRect* result, std::int32_t font,
                                               const TextRect* rect, std::uint32_t flags,
                                               const msvc6::WString* text) {
    String converted;
    reinterpret_cast<WideToNarrowFn>(kWideToNarrow)(&converted, text);
    String narrow{};
    narrow.allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(result));
    msvc6::String_AssignSubstr(&narrow, nullptr, &converted, 0, 0xFFFFFFFFu);
    // The temporary's inlined destructor: erase(0, npos) (with the split), free.
    msvc6::detail::Erase(&converted, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(&converted, nullptr, 1);
    TextRect drawn;
    const TextRect* const area =
        reinterpret_cast<DrawTextFn>(kDrawText)(self, nullptr, &drawn, font, rect, flags, &narrow);
    *result = *area;
    msvc6::detail::Erase(&narrow, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(&narrow, nullptr, 1);
    return result;
}

using CreateFn = decltype(&CTextSurface_Create);

S3RE_HOOK(0x004a78f0, CTextSurface_Create, CreateFn, Original_CTextSurface_Create, CTextSurface_Create);

// Purpose: creates the surface's bitmap for the screen size (called when
// the drawer creates its surfaces).
std::uint32_t __fastcall CTextSurface_Create(void* self, void* /*edx*/, std::int32_t width, std::int32_t height) {
    CTextSurface* const surface = Surface(self);
    OneBitInfo info{};
    info.size = 40;
    info.width = width;
    info.height = -height;
    info.planes = 1;
    info.bit_count = 1;
    info.colours[1][0] = 0xff;
    info.colours[1][1] = 0xff;
    info.colours[1][2] = 0xff;
    void* const dc = g_importGetDC.get()(nullptr);
    if (dc == nullptr) {
        ThrowVerify(texts::kDcNotNull, kLineGetDc, 0);
    }
    surface->bitmap = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(
        g_importCreateDIBSection.get()(dc, &info, kDibRgbColours, reinterpret_cast<void**>(&surface->bits), nullptr,
                                       0)));
    if (surface->bitmap == 0) {
        ThrowVerify(texts::kBitmapNotNull, kLineCreateBitmap, 0);
    }
    g_importReleaseDC.get()(nullptr, dc);
    std::int32_t dwords = width / kBitsPerDword;
    if (width % kBitsPerDword != 0) {
        ++dwords;
    }
    surface->pitch = static_cast<std::uint32_t>(dwords) * 4;
    return static_cast<std::uint32_t>(dwords);
}

using AddFontFn = decltype(&CTextSurface_AddFont);

S3RE_HOOK(0x004a7df0, CTextSurface_AddFont, AddFontFn, Original_CTextSurface_AddFont, CTextSurface_AddFont);

// Purpose: creates one of the surface's fonts (face, height, bold, italic)
// and files it under its id; the drawer sets up its panel fonts with it.
std::uint32_t __fastcall CTextSurface_AddFont(void* self, void* /*edx*/, std::int32_t font, const String* face,
                                              std::int32_t height, std::uint32_t bold, std::uint32_t italic) {
    void* const defaultFont = g_importGetStockObject.get()(kDefaultGuiFont);
    if (defaultFont == nullptr) {
        ThrowVerify(texts::kDefaultFontNotNull, kLineDefaultFont, 0);
    }
    LogFont logFont;
    std::memset(&logFont, 0, sizeof(logFont));
    std::strcpy(logFont.face, face->ptr != nullptr ? face->ptr : g_stringNullBuffer.get());  // unbounded, as the original
    const std::uint32_t language = reinterpret_cast<ConfigGetFn>(kConfigDword28)(&g_config.get(), nullptr);
    logFont.height = height;
    logFont.charset = language == kJapanese ? kShiftJisCharset : kDefaultCharset;
    if (static_cast<std::uint8_t>(bold) != 0) {
        logFont.weight = kBoldWeight;
    }
    logFont.italic = static_cast<std::uint8_t>(italic);
    void* created = g_importCreateFontIndirectA.get()(&logFont);
    if (created == nullptr) {
        created = defaultFont;
    }
    const std::uint32_t entry = FontEntry(Surface(self), font);
    // The HFONT is the dword before the entry pointer; a failed insert (0)
    // makes the original write to address 0.
    auto* const slot = reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(entry != 0 ? entry - 4 : 0));
    *slot = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(created));
    return entry;
}

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
