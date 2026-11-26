// The drawer's panel texts (no named source file; between
// Volker\DrawBuilding.cpp and Volker\GameDataSave.cpp): CDrawer helpers
// that render one text into the 1-bit text surface (Dirk::CTextSurface,
// text_surface.cpp) and copy it onto the screen as glyph rows. Probably
// the texts of the in-game menu panels (Render_sub_4b4d00, the panel
// drawer, calls them; positions are in "cells" of g_uiCellSize pixels
// from the drawing origin, the panel is 40 cells wide).
//
// Every text helper does the same four steps: set the rect at +0x5f684
// (left, top, right, bottom; the original writes top, bottom, left,
// right), ClearTextRect(left, top, width, height), CTextSurface::DrawTextF
// (font, &rect, flags, format[, argument]) into a local result, and
// BlitTextRect with the rect re-read from the drawer. The text is a game
// text (CGraphicsData::TextAddress(id, -1) 0x5faaa0, fetched after the
// clear) used as the format, or a fixed format. C = g_uiCellSize, (X, Y) =
// g_drawOriginX / Y; the rects:
//
//   0x4d6a30 PanelText(row, lines, id)              ret 0Ch  font 0, flags 9
//   0x4d6b10 PanelTextFont1(row, lines, id)         ret 0Ch  font 1, flags 9
//   0x4d6dd0 PanelTextArg(row, lines, id, arg)      ret 10h  font 0, flags 9
//     left X, top Y + (row - 3) C, right X + 40 C, bottom top + 5 C lines
//   0x4d6eb0 PanelTextIndentedArg(row, lines, id, arg)  ret 10h  font 1, flags 8
//     the same with left X + 2 C
//   0x4d6f90 PanelTextLeftHalf(row, lines, id)      ret 0Ch  font 0, flags 9: right X + 20 C
//   0x4d7070 PanelTextRightHalf(row, lines, id)     ret 0Ch  font 0, flags 9: left X + 20 C
//   0x4d7470 PanelTextAt(row, column, lines, id)    ret 10h  font 0, flags 8
//   0x4d7550 PanelTextAtFont1(row, column, lines, id)   ret 10h  font 1, flags 8
//   0x4d78d0 PanelTextAtArg(row, column, lines, id, arg)  ret 14h  font 0, flags 8
//     left X + column C, the rest as PanelText
//   0x4d7150 PanelNumber(column, row, value)        ret 0Ch  font 1, flags 9, "%u"
//   0x4d7390 PanelPercent(column, row, value)       ret 0Ch  font 1, flags 9, "%u%%"
//     left X + (column - 4) C, top Y + (row - 2) C, right X + (column + 4) C
//     (PanelNumber: minus 1), bottom top + 4 C
//   0x4d7230 PanelHundredths(font, x, y, value)     ret 10h  flags 9: value / 100 printed
//     by sprintf (0x61f27e) into a 12-byte buffer: "%u" of value / 100 when it is
//     a multiple of 100, else "%2.1f" (multiple of 10) or "%2.2f" of value * 0.01f
//     (x87: the exact product rounded to double once); the buffer is the format.
//     Pixel rect: left X + x - 20, top Y + y - 2, right X + x + 19, bottom top + 4 C.
//     EAX = sprintf's count.
//   0x4d6bf0 PanelString(row, lines, std::string text)  ret 18h: DrawText (not F),
//     font 1, flags 0; left X + 15 C, top Y + (row - 2) C, right X + 40 C, bottom
//     top + 4 C lines; then the by-value string's inlined destructor.
//   0x4d7630 PanelCaption(row, const char* text)    ret 8: a std::string of the
//     text (none for null) through temporaries, DrawText font 1, flags 0x4a;
//     left X + 2 C, top Y + (row - 2) C, right X + 38 C, bottom top + 4 C; the
//     string's inlined destructor before the blit.
//
// CDrawer::InitPanelFonts (0x004c6f60, Volker\DrawBuilding.cpp's range):
//   thiscall (): the text surface's fonts: unless the sound set
//   (CSoundData::GetSetNumber 0x5ff4e0 of +0x5f6bc) is 4, font 0 "Times New
//   Roman" 5 C high, font 1 "Arial" 4 C, font 99 "Small Fonts" 8 (pixels);
//   with set 4 (probably an Asian version) "MS Sans Serif" for fonts 0 and
//   1. Each face is a std::string built through temporaries
//   (string(const char*, alloc) 0x407a50 and string(string, 0, npos,
//   alloc) 0x4078a0, assigned into it; for set 4 the string starts from
//   string(alloc) 0x407880, and "Small Fonts" is string(text, npos)
//   0x401a10), passed to CTextSurface::AddFont (bold and italic 0), then
//   erased (0x407b10) and freed (_Tidy(true); set 4's last: ~string
//   0x401b60). Then the panel area +0x5f654 (X, Y + 24 C, X + 40 C, Y + 90
//   C), +0x5f664 = g_sidebarWidth, +0x5f668 = 0, +0x5f66c / +0x5f670 = the
//   display size, +0x5f694..+0x5f6b0 = 0.
// CDrawer::RenderMessageTexts (0x004d82b0): thiscall (const DrawDataView*)
//   ret 4: the message texts into the text surface (no blit; the frame
//   drawer 0x4b26c0 calls it): if the header is shown, ClearTextRect of its
//   rect (g_messageHeaderCells * C, tops from g_displayHeight) and
//   CTextSurface::DrawWideText(font 0, the rect, flags 0x40 = one line,
//   the text). Then for each of the 20 lines that is shown: ClearTextRect of
//   its rect (6 C high, line k 6 k cells above the first) and, when its
//   +0x10 is not 0, DrawWideText. C (g_uiCellSize) is re-read after each
//   draw, as the original does.
// CDrawer::BlitMessageTexts (0x004d8400): thiscall (const DrawDataView*)
//   ret 4: the message texts onto the screen: the header (when shown, or
//   when its text differs from the empty wide text g_setupWideText; the
//   compare of basic_string::compare(const E*) inlined, after wcslen
//   0x61fe1b): BlitTextRect of its rect, then its icon (animation 0x40009
//   when its +0x10 is set, else 0x4000a; image 0 of the list, inlined: the
//   list + its dword +8) by GfxDword::DrawSprite at ((hdr left - 2) C,
//   (hdr top + 3) C + H), brightness 0xff, fraction 0x10000. Each line that
//   is shown or has its +0x10 set: when +0x10 is set, its icon by the kind
//   +0x14: below 20 (signed) the colour-indexed animation 0x40000's image 0
//   (ListImage) with the palette 0x10000 + kind (PalettePointer(.., 0xff))
//   by DrawColorIndexed; 0xfd / 0xfe / 0xfc the sprites 0x10088 / 0x10089
//   / 0x1008b (0xfc 8 pixels further right) by DrawSprite; at ((line left -
//   2) C, (line top - 6 k + 3) C + H); then BlitTextRect of the line's rect.
//   Clip rect (0, 0, g_displayWidth, H); the surface descriptor is +0x80.
//   H = g_displayHeight, re-read where the original re-reads it.
// CDrawer::ClearTextRect (0x004d65e0): (x, y, width, height) ret 10h, pixels
//   of the text surface: Lock, for each row y .. y + height - 1 clear the bits
//   x .. x + width - 1 (bit 0x80 = the leftmost pixel of a byte; masks keep
//   the pixels outside, whole bytes in between are zeroed), Unlock.
// CDrawer::BlitTextRect (0x004d6740): (x, y, width, height) ret 10h: Lock,
//   for each row and each byte of the span with set bits (masked to the
//   span at both ends) CDrawer::DrawGlyphRow(byte column, row, bits), Unlock.
// Both loop while row < y + height (unsigned, so a wrapped end draws
// nothing) and compute the byte columns with unsigned shifts, as the
// original.

#include "game/globals.h"
#include "game/graphics/draw_data_layout.h"
#include "game/graphics/drawer_layout.h"
#include "game/graphics/text_surface.h"
#include "game/tables.h"
#include "game/texts.h"
#include "hooks.h"
#include "msvc6/string.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

using msvc6::String;

constexpr std::uintptr_t kGetPitch = 0x00412350;         // GetMemberDword8C: the surface's +0x8c
constexpr std::uintptr_t kLock = 0x004a8bd0;             // Dirk::CTextSurface::Lock
constexpr std::uintptr_t kUnlock = 0x004a8e00;           // Dirk::CTextSurface::Unlock
constexpr std::uintptr_t kDrawText = 0x004a8090;         // Dirk::CTextSurface::DrawText
constexpr std::uintptr_t kDrawTextF = 0x004a87d0;        // Dirk::CTextSurface::DrawTextF
constexpr std::uintptr_t kDrawGlyphRow = 0x004d6950;     // CDrawer::DrawGlyphRow
constexpr std::uintptr_t kClearTextRect = 0x004d65e0;
constexpr std::uintptr_t kBlitTextRect = 0x004d6740;
constexpr std::uintptr_t kTextAddress = 0x005faaa0;      // CGraphicsData::TextAddress(id, -1)
constexpr std::uintptr_t kSprintf = 0x0061f27e;          // the CRT's sprintf
constexpr std::uintptr_t kAddFont = 0x004a7df0;          // Dirk::CTextSurface::AddFont
constexpr std::uintptr_t kDrawWideText = 0x004a8a20;     // Dirk::CTextSurface::DrawWideText
constexpr std::uintptr_t kWcslen = 0x0061fe1b;           // the CRT's wcslen
constexpr std::uintptr_t kAnimationPointer = 0x005faaf0; // CGraphicsData::AnimationPointer(out, id)
constexpr std::uintptr_t kColourAnimationPointer = 0x005fab50;  // ColorIndexedAnimationPointer(out, id)
constexpr std::uintptr_t kPalettePointer = 0x005fac10;   // CGraphicsData::PalettePointer(id, 0xff)
constexpr std::uintptr_t kGfxDwordConstruct = 0x005fc540;
constexpr std::uintptr_t kListImage = 0x005fc5a0;        // GfxDword::ListImage(out, index)
constexpr std::uintptr_t kDrawSprite = 0x005fadb0;       // GfxDword::DrawSprite(desc, pos, clip, brightness, fraction)
constexpr std::uintptr_t kDrawColorIndexed = 0x005fc5f0; // GfxDword::DrawColorIndexed(desc, pos, clip, palette)
constexpr std::uintptr_t kPoint16Construct = 0x005fdab0;
constexpr std::uint32_t kHeaderIconSet = 0x40009;        // raw-ok: animation ids (group 4)
constexpr std::uint32_t kHeaderIconUnset = 0x4000a;      // raw-ok
constexpr std::uint32_t kLineColourIcons = 0x40000;      // raw-ok
constexpr std::uint32_t kIconPalettes = 0x10000;         // raw-ok: palette id 0x10000 + kind
constexpr std::uint32_t kIconSpriteFd = 0x10088;         // raw-ok: animation ids (group 1)
constexpr std::uint32_t kIconSpriteFe = 0x10089;         // raw-ok
constexpr std::uint32_t kIconSpriteFc = 0x1008b;         // raw-ok
constexpr std::int32_t kColourIconKinds = 20;
constexpr std::uint32_t kKindFd = 0xfd;
constexpr std::uint32_t kKindFe = 0xfe;
constexpr std::uint32_t kKindFc = 0xfc;
constexpr std::uint32_t kFullBrightness = 0xff;
constexpr std::uint32_t kAnyPalette = 0xff;
constexpr std::uint32_t kWholeSprite = 0x10000;          // 16.16 fraction: all of it
constexpr std::uint32_t kFirstImageOffset = 8;           // an image list's dword +8: image 0's offset
constexpr std::uint32_t kDescriptorOffset = 0x80;        // the drawer's surface descriptor
constexpr std::uint32_t kOneLine = 0x40;                 // DrawText flags: single line
constexpr std::int32_t kMessageLineCells = 6;            // a message line is 6 cells high
constexpr std::uintptr_t kSoundSetNumber = 0x005ff4e0;   // CSoundData::GetSetNumber

constexpr std::uint32_t kSoundSetSansSerif = 4;          // the sound set that gets MS Sans Serif (probably Asian)
constexpr std::int32_t kSmallFont = 99;                  // the font id of "Small Fonts"
constexpr std::int32_t kSmallFontHeight = 8;             // pixels

constexpr std::uint32_t kAnyLanguage = 0xffffffff;       // TextAddress's second argument
constexpr std::int32_t kPanelCells = 40;                 // panel width in cells
constexpr std::int32_t kHalfPanelCells = 20;
constexpr std::int32_t kLineCells = 5;                   // one text line
constexpr std::uint32_t kFont0 = 0;
constexpr std::uint32_t kFont1 = 1;
constexpr std::uint32_t kCentered = 9;                   // DrawText flags: centred both ways
constexpr std::uint32_t kLeftVCentered = 8;              // vertically centred, left aligned
constexpr std::uint32_t kPlain = 0;                      // left, top, word break
constexpr std::uint32_t kCaptionFlags = 0x4a;            // single line, vertically centred (and bit 1, unused)
constexpr std::size_t kHundredthsBuffer = 12;
constexpr float kHundredth = 0.01f;                      // 0x658ad8

using GetPitchFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using LockFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using UnlockFn = void(__fastcall*)(void* self, void* edx);
using GlyphRowFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t column, std::uint32_t row,
                                              std::uint32_t bits);
using RectFn = void(__fastcall*)(void* self, void* edx, std::uint32_t x, std::uint32_t y, std::uint32_t width,
                                 std::uint32_t height);
using TextAddressFn = const char*(__fastcall*)(void* self, void* edx, std::uint32_t id, std::uint32_t language);
using DrawTextFn = TextRect*(__fastcall*)(void* self, void* edx, TextRect* result, std::int32_t font,
                                          const TextRect* rect, std::uint32_t flags, const String* text);
using DrawTextFFn = TextRect*(__cdecl*)(void* self, TextRect* result, std::int32_t font, const TextRect* rect,
                                        std::uint32_t flags, const char* format, ...);
using SprintfFn = int(__cdecl*)(char* buffer, const char* format, ...);
using AddFontFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::int32_t font, const String* face,
                                             std::int32_t height, std::uint32_t bold, std::uint32_t italic);
using SetNumberFn = std::uint32_t(__fastcall*)(const void* self, void* edx);
using DrawWideTextFn = TextRect*(__fastcall*)(void* self, void* edx, TextRect* result, std::int32_t font,
                                              const TextRect* rect, std::uint32_t flags, const msvc6::WString* text);

std::uint8_t* At(std::uint32_t address) {
    return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(address));
}

// The surface's pitch and locked bits (the surface pointer is read for
// each call, as the original does).
struct LockedSurface {
    std::uint32_t pitch;
    std::uint32_t bits;
};

LockedSurface LockTextSurface(CDrawer* drawer) {
    LockedSurface locked;
    locked.pitch = reinterpret_cast<GetPitchFn>(kGetPitch)(drawer->text_surface, nullptr);
    locked.bits = reinterpret_cast<LockFn>(kLock)(drawer->text_surface, nullptr);
    return locked;
}

void UnlockTextSurface(CDrawer* drawer) {
    reinterpret_cast<UnlockFn>(kUnlock)(drawer->text_surface, nullptr);
}

void GlyphRow(CDrawer* drawer, std::uint32_t column, std::uint32_t row, std::uint32_t bits) {
    reinterpret_cast<GlyphRowFn>(kDrawGlyphRow)(drawer, nullptr, column, row, bits);
}

// The panel rect at +0x5f684.
TextRect* PanelRect(CDrawer* drawer) {
    return reinterpret_cast<TextRect*>(&drawer->panel_text_left);
}

// (The rect arithmetic is done in 32-bit unsigned, wrapping like the
// original's.)
void SetPanelRect(CDrawer* drawer, std::uint32_t left, std::uint32_t top, std::uint32_t right,
                  std::uint32_t bottom) {
    drawer->panel_text_top = static_cast<std::int32_t>(top);
    drawer->panel_text_bottom = static_cast<std::int32_t>(bottom);
    drawer->panel_text_left = static_cast<std::int32_t>(left);
    drawer->panel_text_right = static_cast<std::int32_t>(right);
}

void ClearPanelRect(CDrawer* drawer) {
    const auto left = static_cast<std::uint32_t>(drawer->panel_text_left);
    const auto top = static_cast<std::uint32_t>(drawer->panel_text_top);
    reinterpret_cast<RectFn>(kClearTextRect)(drawer, nullptr, left, top,
                                             static_cast<std::uint32_t>(drawer->panel_text_right) - left,
                                             static_cast<std::uint32_t>(drawer->panel_text_bottom) - top);
}

void BlitPanelRect(CDrawer* drawer) {
    const auto top = static_cast<std::uint32_t>(drawer->panel_text_top);
    const auto left = static_cast<std::uint32_t>(drawer->panel_text_left);
    reinterpret_cast<RectFn>(kBlitTextRect)(drawer, nullptr, left, top,
                                            static_cast<std::uint32_t>(drawer->panel_text_right) - left,
                                            static_cast<std::uint32_t>(drawer->panel_text_bottom) - top);
}

const char* GameText(CDrawer* drawer, std::uint32_t id) {
    return reinterpret_cast<TextAddressFn>(kTextAddress)(drawer->graphics, nullptr, id, kAnyLanguage);
}

DrawTextFFn DrawTextF() {
    return reinterpret_cast<DrawTextFFn>(kDrawTextF);
}

std::uint32_t CellSize() {
    return static_cast<std::uint32_t>(g_uiCellSize.get());
}

std::uint32_t Cells(std::int32_t n) {
    return static_cast<std::uint32_t>(n) * CellSize();
}

std::uint32_t OriginX() {
    return g_drawOriginX.get();
}

std::uint32_t OriginY() {
    return g_drawOriginY.get();
}

// The rect of a text of `lines` lines at `row`, from cell `fromCell` to
// cell `toCell` of the panel.
void SetLinesRect(CDrawer* drawer, std::int32_t row, std::int32_t lines, std::int32_t fromCell,
                  std::int32_t toCell) {
    const std::uint32_t top = OriginY() + Cells(row - 3);
    SetPanelRect(drawer, OriginX() + Cells(fromCell), top, OriginX() + Cells(toCell),
                 top + kLineCells * Cells(lines));
}

// Steps 2..4 for a game text used as the format, optionally with one
// argument.
void DrawGameText(CDrawer* drawer, std::uint32_t id, std::uint32_t font, std::uint32_t flags) {
    ClearPanelRect(drawer);
    const char* const format = GameText(drawer, id);
    TextRect drawn;
    DrawTextF()(drawer->text_surface, &drawn, font, PanelRect(drawer), flags, format);
    BlitPanelRect(drawer);
}

void DrawGameTextWith(CDrawer* drawer, std::uint32_t id, std::uint32_t argument, std::uint32_t font,
                      std::uint32_t flags) {
    ClearPanelRect(drawer);
    const char* const format = GameText(drawer, id);
    TextRect drawn;
    DrawTextF()(drawer->text_surface, &drawn, font, PanelRect(drawer), flags, format, argument);
    BlitPanelRect(drawer);
}

// The small number boxes of PanelNumber / PanelPercent (8 cells wide).
void SetNumberRect(CDrawer* drawer, std::int32_t column, std::int32_t row, std::uint32_t rightAdjust) {
    const std::uint32_t top = OriginY() + Cells(row - 2);
    SetPanelRect(drawer, Cells(column - 4) + OriginX(), top, Cells(column + 4) + OriginX() + rightAdjust,
                 top + 4 * CellSize());
}

void DrawFormatted(CDrawer* drawer, const char* format, std::uint32_t value) {
    ClearPanelRect(drawer);
    TextRect drawn;
    DrawTextF()(drawer->text_surface, &drawn, kFont1, PanelRect(drawer), kCentered, format, value);
    BlitPanelRect(drawer);
}

// `target` = string(string(text)) as the original inlines it (see
// text_surface.cpp).
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

// `target` = string(string(text, alloc), 0, npos, alloc) through the
// out-of-line constructors (InitPanelFonts).
void AssignThroughConstructors(String* target, const char* text) {
    const std::uint8_t allocator = 0;  // the original's is an uninitialized stack byte
    String fromText;
    String copy;
    msvc6::String_FromText(&fromText, nullptr, text, &allocator);
    msvc6::String_FromSubstr(&copy, nullptr, &fromText, 0, 0xFFFFFFFFu, &allocator);
    msvc6::String_AssignSubstr(target, nullptr, &copy, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(&copy, nullptr, 1);
    msvc6::String_Tidy(&fromText, nullptr, 1);
}

// AddFont(font, face, height, 0, 0) with a face string as InitPanelFonts
// builds and drops it: `face` starts empty (_Tidy(false), or string(alloc)
// for the sound set 4), gets the text, and is erased and freed afterwards.
void AddPanelFont(CDrawer* drawer, std::int32_t font, const char* text, std::int32_t height, bool fromDefault) {
    String face{};
    if (fromDefault) {
        const std::uint8_t allocator = 0;
        msvc6::String_Default(&face, nullptr, &allocator);
    } else {
        msvc6::String_Tidy(&face, nullptr, 0);
    }
    AssignThroughConstructors(&face, text);
    reinterpret_cast<AddFontFn>(kAddFont)(drawer->text_surface, nullptr, font, &face, height, 0, 0);
    msvc6::String_Erase(&face, nullptr, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(&face, nullptr, 1);
}

// A string's inlined destructor: erase(0, npos), then free.
void DestroyString(String* s) {
    msvc6::detail::Erase(s, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(s, nullptr, 1);
}

}  // namespace

// ---------------------------------------------------------------------------
// The panel fonts
// ---------------------------------------------------------------------------

void __fastcall CDrawer_InitPanelFonts(CDrawer* self, void* edx);
using InitPanelFontsFn = decltype(&CDrawer_InitPanelFonts);

S3RE_HOOK(0x004c6f60, CDrawer_InitPanelFonts, InitPanelFontsFn, Original_CDrawer_InitPanelFonts,
          CDrawer_InitPanelFonts);

// Purpose: creates the fonts of the panel texts for the current cell size
// (so they scale with the display mode) and sets the panel area.
void __fastcall CDrawer_InitPanelFonts(CDrawer* self, void* /*edx*/) {
    const std::uint32_t soundSet = reinterpret_cast<SetNumberFn>(kSoundSetNumber)(self->sound, nullptr);
    if (soundSet != kSoundSetSansSerif) {
        AddPanelFont(self, static_cast<std::int32_t>(kFont0), texts::kTimesNewRoman,
                     static_cast<std::int32_t>(5 * CellSize()), false);
        AddPanelFont(self, static_cast<std::int32_t>(kFont1), texts::kArial, static_cast<std::int32_t>(4 * CellSize()),
                     false);
        AddPanelFont(self, kSmallFont, texts::kSmallFonts, kSmallFontHeight, false);
    } else {
        AddPanelFont(self, static_cast<std::int32_t>(kFont0), texts::kMsSansSerif,
                     static_cast<std::int32_t>(5 * CellSize()), true);
        AddPanelFont(self, static_cast<std::int32_t>(kFont1), texts::kMsSansSerif,
                     static_cast<std::int32_t>(4 * CellSize()), true);
        String face{};
        msvc6::String_FromChars(&face, nullptr, texts::kSmallFonts, 0xFFFFFFFFu);
        reinterpret_cast<AddFontFn>(kAddFont)(self->text_surface, nullptr, kSmallFont, &face, kSmallFontHeight, 0, 0);
        msvc6::String_Destroy(&face, nullptr);
    }
    self->panel_top = static_cast<std::int32_t>(OriginY() + 24 * CellSize());
    self->panel_bottom = static_cast<std::int32_t>(OriginY() + 90 * CellSize());
    self->panel_left = static_cast<std::int32_t>(OriginX());
    self->unk05f668 = 0;
    self->panel_right = static_cast<std::int32_t>(OriginX() + Cells(kPanelCells));
    self->unk05f670 = g_displayHeight;
    self->unk05f664 = g_sidebarWidth;
    self->unk05f66c = g_displayWidth;
    for (std::int32_t& value : self->unk05f694) {
        value = 0;
    }
}

// ---------------------------------------------------------------------------
// The message texts
// ---------------------------------------------------------------------------

void __fastcall CDrawer_RenderMessageTexts(CDrawer* self, void* edx, const DrawDataView* data);
using RenderMessageTextsFn = decltype(&CDrawer_RenderMessageTexts);

S3RE_HOOK(0x004d82b0, CDrawer_RenderMessageTexts, RenderMessageTextsFn, Original_CDrawer_RenderMessageTexts,
          CDrawer_RenderMessageTexts);

// Purpose: renders the message texts at the bottom of the screen (probably
// the chat and game messages) into the text surface.
void __fastcall CDrawer_RenderMessageTexts(CDrawer* self, void* /*edx*/, const DrawDataView* data) {
    const auto bottomEdge = static_cast<std::uint32_t>(g_displayHeight.get());
    std::uint32_t cell = CellSize();
    const auto header = [](std::size_t i) { return static_cast<std::uint32_t>(g_messageHeaderCells[i]); };
    const auto line = [](std::size_t i) { return static_cast<std::uint32_t>(g_messageLineCells[i]); };
    const TextRect headerRect{static_cast<std::int32_t>(header(0) * cell),
                              static_cast<std::int32_t>(header(1) * cell + bottomEdge),
                              static_cast<std::int32_t>(header(2) * cell),
                              static_cast<std::int32_t>(header(3) * cell + bottomEdge)};
    if (data->message_header.shown != 0) {
        reinterpret_cast<RectFn>(kClearTextRect)(
            self, nullptr, static_cast<std::uint32_t>(headerRect.left), static_cast<std::uint32_t>(headerRect.top),
            static_cast<std::uint32_t>(headerRect.right - headerRect.left),
            static_cast<std::uint32_t>(headerRect.bottom - headerRect.top));
        TextRect drawn;
        reinterpret_cast<DrawWideTextFn>(kDrawWideText)(self->text_surface, nullptr, &drawn, 0, &headerRect, kOneLine,
                                                        &data->message_header.text);
        cell = CellSize();
    }
    for (std::size_t k = 0; k < kDrawMessageLines; ++k) {
        const DrawMessageLine& message = data->messages[k];
        if (message.shown == 0) {
            continue;
        }
        const auto up = static_cast<std::uint32_t>(kMessageLineCells * static_cast<std::int32_t>(k));
        const TextRect rect{static_cast<std::int32_t>(line(0) * cell),
                            static_cast<std::int32_t>(static_cast<std::uint32_t>(g_displayHeight.get()) + (line(1) - up) * cell),
                            static_cast<std::int32_t>(line(2) * cell),
                            static_cast<std::int32_t>((line(3) - up) * cell + static_cast<std::uint32_t>(g_displayHeight.get()))};
        reinterpret_cast<RectFn>(kClearTextRect)(self, nullptr, static_cast<std::uint32_t>(rect.left),
                                                 static_cast<std::uint32_t>(rect.top), (line(2) - line(0)) * cell,
                                                 kMessageLineCells * cell);
        if (message.unk10 != 0) {
            TextRect drawn;
            reinterpret_cast<DrawWideTextFn>(kDrawWideText)(self->text_surface, nullptr, &drawn, 0, &rect, kOneLine,
                                                            &message.text);
        }
        cell = CellSize();
    }
}

// ---------------------------------------------------------------------------
// The message texts onto the screen
// ---------------------------------------------------------------------------

namespace {

using PointerFn = std::uint32_t*(__fastcall*)(void* self, void* edx, std::uint32_t* out, std::uint32_t id);
using PaletteFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t id, std::uint32_t any);
using ConstructDwordFn = void*(__fastcall*)(std::uint32_t* self, void* edx, std::uint32_t value);
using ListImageFn = std::uint32_t*(__fastcall*)(std::uint32_t* self, void* edx, std::uint32_t* out,
                                                std::uint32_t index);
using Point16Fn = std::uint32_t*(__fastcall*)(std::uint32_t* self, void* edx, std::uint32_t x, std::uint32_t y);
using DrawSpriteFn = std::uint32_t(__fastcall*)(std::uint32_t* self, void* edx, void* desc, const std::uint32_t* pos,
                                                const TextRect* clip, std::uint32_t brightness,
                                                std::uint32_t fraction);
using DrawColourFn = std::uint32_t(__fastcall*)(std::uint32_t* self, void* edx, void* desc, const std::uint32_t* pos,
                                                const TextRect* clip, std::uint32_t palette);
using WcslenFn = std::uint32_t(__cdecl*)(const std::uint16_t* text);

void* Descriptor(CDrawer* drawer) {
    return reinterpret_cast<std::uint8_t*>(drawer) + kDescriptorOffset;
}

// basic_string<unsigned short>::compare(const E*) != 0, as inlined.
bool DiffersFrom(const msvc6::WString& text, const std::uint16_t* other) {
    const std::uint32_t otherLength = reinterpret_cast<WcslenFn>(kWcslen)(other);
    std::uint32_t common = text.len < otherLength ? text.len : otherLength;
    const std::uint16_t* a = text.ptr;
    const std::uint16_t* b = other;
    for (; common != 0; --common, ++a, ++b) {
        if (*a != *b) {
            return true;
        }
    }
    return text.len < otherLength || text.len != otherLength;
}

// A sprite icon: image 0 of the animation (inlined: list + its dword +8)
// by DrawSprite.
void DrawIconSprite(CDrawer* drawer, std::uint32_t animation, std::uint32_t x, std::uint32_t y, const TextRect* clip) {
    std::uint32_t out = 0;
    const std::uint32_t list =
        *reinterpret_cast<PointerFn>(kAnimationPointer)(drawer->graphics, nullptr, &out, animation);
    std::uint32_t image = list + *reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(list) +
                                                                          kFirstImageOffset);
    std::uint32_t position = 0;
    reinterpret_cast<Point16Fn>(kPoint16Construct)(&position, nullptr, x, y);
    reinterpret_cast<DrawSpriteFn>(kDrawSprite)(&image, nullptr, Descriptor(drawer), &position, clip,
                                                kFullBrightness, kWholeSprite);
}

}  // namespace

void __fastcall CDrawer_BlitMessageTexts(CDrawer* self, void* edx, const DrawDataView* data);
using BlitMessageTextsFn = decltype(&CDrawer_BlitMessageTexts);

S3RE_HOOK(0x004d8400, CDrawer_BlitMessageTexts, BlitMessageTextsFn, Original_CDrawer_BlitMessageTexts,
          CDrawer_BlitMessageTexts);

// Purpose: copies the rendered message texts onto the screen, each with
// its icon.
void __fastcall CDrawer_BlitMessageTexts(CDrawer* self, void* /*edx*/, const DrawDataView* data) {
    const auto width = static_cast<std::uint32_t>(g_displayWidth.get());
    const std::uint32_t cell = CellSize();
    const auto header = [](std::size_t i) { return static_cast<std::uint32_t>(g_messageHeaderCells[i]); };
    const auto line = [](std::size_t i) { return static_cast<std::uint32_t>(g_messageLineCells[i]); };
    const auto bottomEdge = [] { return static_cast<std::uint32_t>(g_displayHeight.get()); };
    const std::uint32_t headerLeft = header(0) * cell;
    const std::uint32_t headerTop = header(1) * cell + bottomEdge();
    const std::uint32_t headerBottom = header(3) * cell + bottomEdge();
    const std::uint32_t headerRight = header(2) * cell;
    const TextRect clip{0, 0, static_cast<std::int32_t>(width), static_cast<std::int32_t>(bottomEdge())};
    std::uint32_t colourList = 0;
    std::uint32_t colourImage = 0;
    reinterpret_cast<ConstructDwordFn>(kGfxDwordConstruct)(&colourList, nullptr, 0);
    reinterpret_cast<ConstructDwordFn>(kGfxDwordConstruct)(&colourImage, nullptr, 0);

    const DrawMessageHeader& head = data->message_header;
    if (head.shown != 0 || DiffersFrom(head.text, g_setupWideText.get())) {
        reinterpret_cast<RectFn>(kBlitTextRect)(self, nullptr, headerLeft, headerTop, headerRight - headerLeft,
                                                headerBottom - headerTop);
        DrawIconSprite(self, head.unk10 != 0 ? kHeaderIconSet : kHeaderIconUnset, (header(0) - 2) * CellSize(),
                       (header(1) + 3) * CellSize() + bottomEdge(), &clip);
    }
    for (std::size_t k = 0; k < kDrawMessageLines; ++k) {
        const DrawMessageLine& message = data->messages[k];
        if (message.shown == 0 && message.unk10 == 0) {
            continue;
        }
        const auto up = static_cast<std::uint32_t>(kMessageLineCells * static_cast<std::int32_t>(k));
        if (message.unk10 != 0) {
            const auto kind = static_cast<std::int32_t>(message.unk14);
            const auto iconX = [&] { return (line(0) - 2) * CellSize(); };
            const auto iconY = [&] { return (line(1) - up + 3) * CellSize() + bottomEdge(); };
            if (kind < kColourIconKinds) {
                std::uint32_t out = 0;
                colourList = *reinterpret_cast<PointerFn>(kColourAnimationPointer)(self->graphics, nullptr, &out,
                                                                                    kLineColourIcons);
                std::uint32_t imageOut = 0;
                colourImage = *reinterpret_cast<ListImageFn>(kListImage)(&colourList, nullptr, &imageOut, 0);
                const std::uint32_t palette = reinterpret_cast<PaletteFn>(kPalettePointer)(
                    self->graphics, nullptr, message.unk14 + kIconPalettes, kAnyPalette);
                const std::uint32_t y = iconY();
                const std::uint32_t x = iconX();
                std::uint32_t position = 0;
                reinterpret_cast<Point16Fn>(kPoint16Construct)(&position, nullptr, x, y);
                reinterpret_cast<DrawColourFn>(kDrawColorIndexed)(&colourImage, nullptr, Descriptor(self), &position,
                                                                  &clip, palette);
            } else if (message.unk14 == kKindFd || message.unk14 == kKindFe || message.unk14 == kKindFc) {
                const std::uint32_t animation = message.unk14 == kKindFd   ? kIconSpriteFd
                                                : message.unk14 == kKindFe ? kIconSpriteFe
                                                                           : kIconSpriteFc;
                std::uint32_t out = 0;
                const std::uint32_t list =
                    *reinterpret_cast<PointerFn>(kAnimationPointer)(self->graphics, nullptr, &out, animation);
                std::uint32_t image = list + *reinterpret_cast<const std::uint32_t*>(
                                                 static_cast<std::uintptr_t>(list) + kFirstImageOffset);
                const std::uint32_t y = iconY();
                const std::uint32_t x = iconX() + (message.unk14 == kKindFc ? 8u : 0u);
                std::uint32_t position = 0;
                reinterpret_cast<Point16Fn>(kPoint16Construct)(&position, nullptr, x, y);
                reinterpret_cast<DrawSpriteFn>(kDrawSprite)(&image, nullptr, Descriptor(self), &position, &clip,
                                                            kFullBrightness, kWholeSprite);
            }
        }
        const std::uint32_t c = CellSize();
        reinterpret_cast<RectFn>(kBlitTextRect)(self, nullptr, line(0) * c, (line(1) - up) * c + bottomEdge(),
                                                (line(2) - line(0)) * c, kMessageLineCells * c);
    }
}

// ---------------------------------------------------------------------------
// The text surface rect helpers
// ---------------------------------------------------------------------------

void __fastcall CDrawer_ClearTextRect(CDrawer* self, void* edx, std::uint32_t x, std::uint32_t y, std::uint32_t width,
                                      std::uint32_t height);
void __fastcall CDrawer_BlitTextRect(CDrawer* self, void* edx, std::uint32_t x, std::uint32_t y, std::uint32_t width,
                                     std::uint32_t height);
using TextRectFn = decltype(&CDrawer_ClearTextRect);

S3RE_HOOK(0x004d65e0, CDrawer_ClearTextRect, TextRectFn, Original_CDrawer_ClearTextRect, CDrawer_ClearTextRect);

// Purpose: blanks a rectangle of the text surface before a panel text is
// drawn into it.
void __fastcall CDrawer_ClearTextRect(CDrawer* self, void* /*edx*/, std::uint32_t x, std::uint32_t y,
                                      std::uint32_t width, std::uint32_t height) {
    const LockedSurface surface = LockTextSurface(self);
    std::uint32_t rowStart = surface.bits + surface.pitch * y;
    const std::uint32_t first = x >> 3;
    const std::uint32_t last = (x + width - 1) >> 3;
    // Masks of the pixels to keep: those left of x, those right of the end.
    const std::uint8_t keepLeft = static_cast<std::uint8_t>(0xffu << (8 - (x & 7)));
    const std::uint8_t keepRight = static_cast<std::uint8_t>(0xffu >> (((x + width - 1) & 7) + 1));
    const std::uint32_t end = y + height;
    if (y < end) {
        for (std::uint32_t rows = end - y; rows != 0; --rows, rowStart += surface.pitch) {
            if (first == last) {
                *At(rowStart + first) &= keepLeft & keepRight;
            } else if (first + 1 == last) {
                *At(rowStart + first) &= keepLeft;
                *At(rowStart + last) &= keepRight;
            } else {
                *At(rowStart + first) &= keepLeft;
                if (first + 1 < last) {
                    std::memset(At(rowStart + first + 1), 0, last - (first + 1));
                }
                *At(rowStart + last) &= keepRight;
            }
        }
    }
    UnlockTextSurface(self);
}

S3RE_HOOK(0x004d6740, CDrawer_BlitTextRect, TextRectFn, Original_CDrawer_BlitTextRect, CDrawer_BlitTextRect);

// Purpose: copies the set pixels of a text surface rectangle onto the
// screen (as 8-pixel glyph rows), after a panel text was drawn into it.
void __fastcall CDrawer_BlitTextRect(CDrawer* self, void* /*edx*/, std::uint32_t x, std::uint32_t y,
                                     std::uint32_t width, std::uint32_t height) {
    const LockedSurface surface = LockTextSurface(self);
    std::uint32_t rowStart = surface.bits + surface.pitch * y;
    const std::uint32_t first = x >> 3;
    const std::uint32_t last = (x + width - 1) >> 3;
    // Masks of the pixels inside the span.
    const std::uint32_t inLeft = 0xffu >> (x & 7);
    const std::uint32_t inRight = 0xffu << (7 - ((x + width - 1) & 7));
    const std::uint32_t end = y + height;
    for (std::uint32_t row = y; row < end; ++row, rowStart += surface.pitch) {
        if (first == last) {
            const std::uint32_t bits = *At(rowStart + first) & inLeft & inRight;
            if (bits != 0) {
                GlyphRow(self, first, row, bits);
            }
            continue;
        }
        const std::uint32_t leftBits = *At(rowStart + first) & inLeft;
        if (leftBits != 0) {
            GlyphRow(self, first, row, leftBits);
        }
        if (first + 1 != last) {
            for (std::uint32_t column = first + 1; column < last; ++column) {
                const std::uint32_t bits = *At(rowStart + column);
                if (bits != 0) {
                    GlyphRow(self, column, row, bits);
                }
            }
        }
        const std::uint32_t rightBits = *At(rowStart + last) & inRight;
        if (rightBits != 0) {
            GlyphRow(self, last, row, rightBits);
        }
    }
    UnlockTextSurface(self);
}

// ---------------------------------------------------------------------------
// The panel text helpers
// ---------------------------------------------------------------------------

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

using PanelTextFn = decltype(&CDrawer_PanelText);
using PanelTextArgFn = decltype(&CDrawer_PanelTextArg);
using PanelTextAtFn = decltype(&CDrawer_PanelTextAt);
using PanelTextAtArgFn = decltype(&CDrawer_PanelTextAtArg);
using PanelNumberFn = decltype(&CDrawer_PanelNumber);
using PanelHundredthsFn = decltype(&CDrawer_PanelHundredths);
using PanelStringFn = decltype(&CDrawer_PanelString);
using PanelCaptionFn = decltype(&CDrawer_PanelCaption);

S3RE_HOOK(0x004d6a30, CDrawer_PanelText, PanelTextFn, Original_CDrawer_PanelText, CDrawer_PanelText);
S3RE_HOOK(0x004d6b10, CDrawer_PanelTextFont1, PanelTextFn, Original_CDrawer_PanelTextFont1, CDrawer_PanelTextFont1);
S3RE_HOOK(0x004d6f90, CDrawer_PanelTextLeftHalf, PanelTextFn, Original_CDrawer_PanelTextLeftHalf,
          CDrawer_PanelTextLeftHalf);
S3RE_HOOK(0x004d7070, CDrawer_PanelTextRightHalf, PanelTextFn, Original_CDrawer_PanelTextRightHalf,
          CDrawer_PanelTextRightHalf);
S3RE_HOOK(0x004d6dd0, CDrawer_PanelTextArg, PanelTextArgFn, Original_CDrawer_PanelTextArg, CDrawer_PanelTextArg);
S3RE_HOOK(0x004d6eb0, CDrawer_PanelTextIndentedArg, PanelTextArgFn, Original_CDrawer_PanelTextIndentedArg,
          CDrawer_PanelTextIndentedArg);
S3RE_HOOK(0x004d7470, CDrawer_PanelTextAt, PanelTextAtFn, Original_CDrawer_PanelTextAt, CDrawer_PanelTextAt);
S3RE_HOOK(0x004d7550, CDrawer_PanelTextAtFont1, PanelTextAtFn, Original_CDrawer_PanelTextAtFont1,
          CDrawer_PanelTextAtFont1);
S3RE_HOOK(0x004d78d0, CDrawer_PanelTextAtArg, PanelTextAtArgFn, Original_CDrawer_PanelTextAtArg,
          CDrawer_PanelTextAtArg);
S3RE_HOOK(0x004d7150, CDrawer_PanelNumber, PanelNumberFn, Original_CDrawer_PanelNumber, CDrawer_PanelNumber);
S3RE_HOOK(0x004d7390, CDrawer_PanelPercent, PanelNumberFn, Original_CDrawer_PanelPercent, CDrawer_PanelPercent);
S3RE_HOOK(0x004d7230, CDrawer_PanelHundredths, PanelHundredthsFn, Original_CDrawer_PanelHundredths,
          CDrawer_PanelHundredths);
S3RE_HOOK(0x004d6bf0, CDrawer_PanelString, PanelStringFn, Original_CDrawer_PanelString, CDrawer_PanelString);
S3RE_HOOK(0x004d7630, CDrawer_PanelCaption, PanelCaptionFn, Original_CDrawer_PanelCaption, CDrawer_PanelCaption);

// Purpose: draws a (multi-line) game text across the whole panel, centred.
void __fastcall CDrawer_PanelText(CDrawer* self, void* /*edx*/, std::int32_t row, std::int32_t lines,
                                  std::uint32_t id) {
    SetLinesRect(self, row, lines, 0, kPanelCells);
    DrawGameText(self, id, kFont0, kCentered);
}

// Purpose: the same in the other font.
void __fastcall CDrawer_PanelTextFont1(CDrawer* self, void* /*edx*/, std::int32_t row, std::int32_t lines,
                                       std::uint32_t id) {
    SetLinesRect(self, row, lines, 0, kPanelCells);
    DrawGameText(self, id, kFont1, kCentered);
}

// Purpose: a centred game text in the left half of the panel.
void __fastcall CDrawer_PanelTextLeftHalf(CDrawer* self, void* /*edx*/, std::int32_t row, std::int32_t lines,
                                          std::uint32_t id) {
    SetLinesRect(self, row, lines, 0, kHalfPanelCells);
    DrawGameText(self, id, kFont0, kCentered);
}

// Purpose: a centred game text in the right half of the panel.
void __fastcall CDrawer_PanelTextRightHalf(CDrawer* self, void* /*edx*/, std::int32_t row, std::int32_t lines,
                                           std::uint32_t id) {
    SetLinesRect(self, row, lines, kHalfPanelCells, kPanelCells);
    DrawGameText(self, id, kFont0, kCentered);
}

// Purpose: a centred game text with one value filled in (the text is the
// printf format).
void __fastcall CDrawer_PanelTextArg(CDrawer* self, void* /*edx*/, std::int32_t row, std::int32_t lines,
                                     std::uint32_t id, std::uint32_t argument) {
    SetLinesRect(self, row, lines, 0, kPanelCells);
    DrawGameTextWith(self, id, argument, kFont0, kCentered);
}

// Purpose: a left-aligned game text with one value filled in, indented by
// two cells.
void __fastcall CDrawer_PanelTextIndentedArg(CDrawer* self, void* /*edx*/, std::int32_t row, std::int32_t lines,
                                             std::uint32_t id, std::uint32_t argument) {
    SetLinesRect(self, row, lines, 2, kPanelCells);
    DrawGameTextWith(self, id, argument, kFont1, kLeftVCentered);
}

// Purpose: a left-aligned game text starting at a given cell column.
void __fastcall CDrawer_PanelTextAt(CDrawer* self, void* /*edx*/, std::int32_t row, std::int32_t column,
                                    std::int32_t lines, std::uint32_t id) {
    SetLinesRect(self, row, lines, column, kPanelCells);
    DrawGameText(self, id, kFont0, kLeftVCentered);
}

// Purpose: the same in the other font.
void __fastcall CDrawer_PanelTextAtFont1(CDrawer* self, void* /*edx*/, std::int32_t row, std::int32_t column,
                                         std::int32_t lines, std::uint32_t id) {
    SetLinesRect(self, row, lines, column, kPanelCells);
    DrawGameText(self, id, kFont1, kLeftVCentered);
}

// Purpose: a left-aligned game text from a cell column, with one value
// filled in.
void __fastcall CDrawer_PanelTextAtArg(CDrawer* self, void* /*edx*/, std::int32_t row, std::int32_t column,
                                       std::int32_t lines, std::uint32_t id, std::uint32_t argument) {
    SetLinesRect(self, row, lines, column, kPanelCells);
    DrawGameTextWith(self, id, argument, kFont0, kLeftVCentered);
}

// Purpose: a number centred in a small box around a cell column (probably
// the amounts in the panels).
void __fastcall CDrawer_PanelNumber(CDrawer* self, void* /*edx*/, std::int32_t column, std::int32_t row,
                                    std::uint32_t value) {
    SetNumberRect(self, column, row, 0xffffffffu);  // right minus 1
    DrawFormatted(self, texts::kFormatUnsigned, value);
}

// Purpose: a percentage centred in a small box around a cell column.
void __fastcall CDrawer_PanelPercent(CDrawer* self, void* /*edx*/, std::int32_t column, std::int32_t row,
                                     std::uint32_t value) {
    SetNumberRect(self, column, row, 0);
    DrawFormatted(self, texts::kFormatPercent, value);
}

// Purpose: a value given in hundredths (e.g. 250 -> "2.5"), centred around
// a pixel position.
std::uint32_t __fastcall CDrawer_PanelHundredths(CDrawer* self, void* /*edx*/, std::uint32_t font, std::int32_t x,
                                                 std::int32_t y, std::uint32_t value) {
    char buffer[kHundredthsBuffer];
    const auto print = reinterpret_cast<SprintfFn>(kSprintf);
    int count;
    if (value % 100 == 0) {
        count = print(buffer, texts::kFormatUnsigned, value / 100);
    } else {
        // fild qword (value, 0); fmul dword 0.01f; fstp qword: the exact
        // product rounded to double once.
        const double hundredths = static_cast<double>(value) * static_cast<double>(kHundredth);
        count = print(buffer, value % 10 != 0 ? texts::kFormatTwoDecimals : texts::kFormatOneDecimal, hundredths);
    }
    const std::uint32_t top = OriginY() + static_cast<std::uint32_t>(y) - 2;
    const auto across = static_cast<std::uint32_t>(x);
    SetPanelRect(self, OriginX() + across - 20, top, OriginX() + across + 19, top + 4 * CellSize());
    ClearPanelRect(self);
    TextRect drawn;
    DrawTextF()(self->text_surface, &drawn, font, PanelRect(self), kCentered, buffer);
    BlitPanelRect(self);
    return static_cast<std::uint32_t>(count);
}

// Purpose: draws a std::string (passed by value) at the right of the panel,
// left aligned, word-wrapped over `lines` lines.
void __fastcall CDrawer_PanelString(CDrawer* self, void* /*edx*/, std::int32_t row, std::int32_t lines,
                                    String text) {
    const std::uint32_t top = OriginY() + Cells(row - 2);
    SetPanelRect(self, OriginX() + Cells(3) + Cells(12), top, OriginX() + Cells(kPanelCells),
                 top + 4 * Cells(lines));
    ClearPanelRect(self);
    TextRect drawn;
    reinterpret_cast<DrawTextFn>(kDrawText)(self->text_surface, nullptr, &drawn, kFont1, PanelRect(self), kPlain,
                                            &text);
    BlitPanelRect(self);
    DestroyString(&text);
}

// Purpose: draws a one-line C text (a caption, probably) across the panel.
void __fastcall CDrawer_PanelCaption(CDrawer* self, void* /*edx*/, std::int32_t row, const char* text) {
    const std::uint32_t top = OriginY() + Cells(row - 2);
    SetPanelRect(self, OriginX() + Cells(2), top, OriginX() + Cells(38), top + 4 * CellSize());
    ClearPanelRect(self);
    String caption{};
    msvc6::String_Tidy(&caption, nullptr, 0);
    if (text != nullptr) {
        AssignThroughTemporaries(&caption, text);
    }
    TextRect drawn;
    reinterpret_cast<DrawTextFn>(kDrawText)(self->text_surface, nullptr, &drawn, kFont1, PanelRect(self),
                                            kCaptionFlags, &caption);
    DestroyString(&caption);
    BlitPanelRect(self);
}

}  // namespace s3re::game
