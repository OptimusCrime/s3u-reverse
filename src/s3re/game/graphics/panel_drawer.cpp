// CDrawer::DrawPanel (0x004b4d00, Volker\DrawBuilding.cpp's range; 71 KB,
// the biggest drawing function): thiscall (DrawDataView* data) ret 4, called
// once per frame by the frame drawer 0x4b26c0 after it reset the sprite
// queues. It draws the side panel of the game screen: the minimap buttons,
// the menu tabs, the open menu (data->unk060, 1..12) and the four buttons
// below the panel. Was Render_sub_4b4d00.
//
// The panel is only drawn when data->redraw_panel is 1, and once more in
// the frame after (the drawer's +0xf8 remembers it); drawing it sets the
// buttons' last-drawn state +0x8c to -1, so the buttons are drawn too.
// The buttons are drawn whenever their state +0x88 differs from +0x8c, and
// once more after that (+0xfc).
//
// Almost everything is GUI images (GuiImagePointerAt 0x5faca0 of image id
// and display mode - 1, Point16, GfxDword::DrawGuiImage 0x5f64c0 with the
// screen as clip at brightness 255), animation sprites (AnimationPointer
// 0x5faaf0, an image of the list, GfxDword::DrawSprite 0x5fadb0) and the
// drawer's panel texts (text_panels.cpp). The menus are split into files:
//   menu 1  the build menu (panel_build_menu.cpp)
//   menu 2  statistics pages (panel_statistics.cpp)
//   menu 3  settler pages (panel_settlers.cpp)
//   menu 4  the selected building (panel_building_info.cpp)
//   menu 5  soldiers (panel_soldiers.cpp)
//   menus 6..12 here
// Names of menus and values are guesses from what is drawn; the draw data
// fields are in draw_data_layout.h, the panel values by offset in
// re/structs.md ("DrawDataView").
//
// The original builds a std::string (the player name of menu 6) at the
// start and destroys it at the end, under an exception frame; both are kept
// (String_Default 0x407880, ~string 0x401b60). Its two GfxDword::Construct
// calls only zero stack temporaries and are left out.

#include "game/graphics/panel_drawer.h"

#include "game/globals.h"
#include "game/tables.h"
#include "game/texts.h"
#include "hooks.h"
#include "msvc6/string.h"

#include <cstdint>

namespace s3re::game {

namespace panel {

namespace {

constexpr std::uintptr_t kGuiImagePointerAt = 0x005faca0;  // CGraphicsData::GuiImagePointerAt(out, id, index)
constexpr std::uintptr_t kPoint16 = 0x005fdab0;            // Point16::Point16(x, y)
constexpr std::uintptr_t kDrawGuiImage = 0x005f64c0;       // GfxDword::DrawGuiImage(desc, pos, clip, brightness)
constexpr std::uintptr_t kAnimationPointer = 0x005faaf0;   // CGraphicsData::AnimationPointer(out, id)
constexpr std::uintptr_t kListImageChecked = 0x004b19c0;   // GfxDword::ListImageChecked(out, index)
constexpr std::uintptr_t kDrawSprite = 0x005fadb0;         // GfxDword::DrawSprite(desc, pos*, clip*, brightness, fraction)
constexpr std::uintptr_t kClearTextRect = 0x004d65e0;
constexpr std::uintptr_t kBlitTextRect = 0x004d6740;
constexpr std::uintptr_t kPanelText = 0x004d6a30;
constexpr std::uintptr_t kPanelTextFont1 = 0x004d6b10;
constexpr std::uintptr_t kPanelString = 0x004d6bf0;
constexpr std::uintptr_t kPanelTextArg = 0x004d6dd0;
constexpr std::uintptr_t kPanelTextIndentedArg = 0x004d6eb0;
constexpr std::uintptr_t kPanelTextLeftHalf = 0x004d6f90;
constexpr std::uintptr_t kPanelTextRightHalf = 0x004d7070;
constexpr std::uintptr_t kPanelNumber = 0x004d7150;
constexpr std::uintptr_t kPanelHundredths = 0x004d7230;
constexpr std::uintptr_t kPanelPercent = 0x004d7390;
constexpr std::uintptr_t kPanelTextAt = 0x004d7470;
constexpr std::uintptr_t kPanelTextAtFont1 = 0x004d7550;
constexpr std::uintptr_t kPanelCaption = 0x004d7630;
constexpr std::uintptr_t kPanelTextAtArg = 0x004d78d0;
constexpr std::uintptr_t kDrawValueBar = 0x004d79c0;       // Render::DrawValueBar (on the drawer)
constexpr std::uintptr_t kDrawStatisticsBar = 0x004d7bd0;
constexpr std::uintptr_t kPutPixel = 0x004c7ac0;           // Render::PutPixel16 (on the drawer)
constexpr std::uintptr_t kRaceOfPlayer = 0x004c7ab0;       // Render::Entry204
constexpr std::uintptr_t kBuildingAnimationSlot = 0x004b1a00;
constexpr std::uintptr_t kDrawTextF = 0x004a87d0;          // Dirk::CTextSurface::DrawTextF (cdecl, variadic)

constexpr std::uint32_t kCentered = 9;  // DrawText flags: centred both ways
constexpr std::int32_t kFont1 = 1;

using DrawTextFFn = Rect*(__cdecl*)(void* self, Rect* result, std::int32_t font, const Rect* rect,
                                    std::uint32_t flags, const char* format, ...);
using PanelStringFn = void(__fastcall*)(CDrawer* self, void* edx, std::int32_t row, std::int32_t lines,
                                        msvc6::String text);

}  // namespace

PanelDrawer::PanelDrawer(CDrawer* self, DrawDataView* data) : self_(self), data_(data) {
    screen_ = {0, 0, g_displayWidth, g_displayHeight};
    textRect_ = {self->panel_left, self->panel_top, self->panel_right, self->panel_bottom};
}

std::int32_t PanelDrawer::Value(std::uint32_t offset) const {
    return data_->panel_values[(offset - offsetof(DrawDataView, panel_values)) / 4];
}

void PanelDrawer::Gui(std::uint32_t id, std::uint32_t x, std::uint32_t y, std::uint32_t brightness) {
    std::uint32_t image = 0;
    ThisCall(kGuiImagePointerAt, U(self_->graphics), U(&image), id, g_displayMode - 1);
    std::uint32_t position = 0;
    ThisCall(kPoint16, U(&position), x, y);
    ThisCall(kDrawGuiImage, U(&image), U(&self_->surface_descriptor), position, screen_.left, screen_.top,
             screen_.right, screen_.bottom, brightness);
}

void PanelDrawer::SelectSpriteInline(std::uint32_t animation, std::int32_t index) {
    std::uint32_t out = 0;
    list_ = *reinterpret_cast<const std::uint32_t*>(
        static_cast<std::uintptr_t>(ThisCall(kAnimationPointer, U(self_->graphics), U(&out), animation)));
    const auto* header = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(list_));
    const auto* offsets = reinterpret_cast<const std::uint32_t*>(header + 8);
    const std::int32_t count = header[7];
    sprite_ = list_ + (index < 0 || index >= count ? offsets[0] : offsets[index]);
}

void PanelDrawer::SelectSprite(std::uint32_t animation, std::int32_t index) {
    std::uint32_t out = 0;
    list_ = *reinterpret_cast<const std::uint32_t*>(
        static_cast<std::uintptr_t>(ThisCall(kAnimationPointer, U(self_->graphics), U(&out), animation)));
    std::uint32_t image = 0;
    sprite_ = *reinterpret_cast<const std::uint32_t*>(
        static_cast<std::uintptr_t>(ThisCall(kListImageChecked, U(&list_), U(&image), index)));
}

void PanelDrawer::DrawSprite(std::uint32_t x, std::uint32_t y, const Rect* clip, std::uint32_t brightness) {
    std::uint32_t position = 0;
    ThisCall(kDrawSprite, U(&sprite_), U(&self_->surface_descriptor), ThisCall(kPoint16, U(&position), x, y), U(clip),
             brightness, kWholeSprite);
}

void PanelDrawer::SpriteForMode(std::uint32_t animation, std::uint32_t x, std::uint32_t y, const Rect* clip) {
    SelectSpriteInline(animation, g_displayMode - 1);
    DrawSprite(x, y, clip, kFullBrightness);
}

void PanelDrawer::ClearTextRect(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) {
    ThisCall(kClearTextRect, U(self_), x, y, width, height);
}

void PanelDrawer::BlitTextRect(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) {
    ThisCall(kBlitTextRect, U(self_), x, y, width, height);
}

void PanelDrawer::ClearPanelText() {
    const auto left = static_cast<std::uint32_t>(self_->panel_left);
    const auto top = static_cast<std::uint32_t>(self_->panel_top);
    ClearTextRect(left, top, static_cast<std::uint32_t>(self_->panel_right) - left,
                  static_cast<std::uint32_t>(self_->panel_bottom) - top);
}

void PanelDrawer::DrawTextFormat(const Rect& rect, const char* format, std::uint32_t value) {
    Rect result{};
    reinterpret_cast<DrawTextFFn>(kDrawTextF)(self_->text_surface, &result, kFont1, &rect, kCentered, format, value);
}

void PanelDrawer::TextInRect(const Rect& rect, const char* format, std::uint32_t value) {
    DrawTextFormat(rect, format, value);
    const auto left = static_cast<std::uint32_t>(rect.left);
    const auto top = static_cast<std::uint32_t>(rect.top);
    BlitTextRect(left, top, static_cast<std::uint32_t>(rect.right) - left, static_cast<std::uint32_t>(rect.bottom) - top);
}

void PanelDrawer::NumberInRect(const Rect& rect, std::uint32_t value) {
    TextInRect(rect, texts::kFormatUnsigned, value);
}

void PanelDrawer::PanelText(std::int32_t row, std::int32_t lines, std::uint32_t id) {
    ThisCall(kPanelText, U(self_), row, lines, id);
}
void PanelDrawer::PanelTextFont1(std::int32_t row, std::int32_t lines, std::uint32_t id) {
    ThisCall(kPanelTextFont1, U(self_), row, lines, id);
}
void PanelDrawer::PanelTextArg(std::int32_t row, std::int32_t lines, std::uint32_t id, std::uint32_t arg) {
    ThisCall(kPanelTextArg, U(self_), row, lines, id, arg);
}
void PanelDrawer::PanelTextIndentedArg(std::int32_t row, std::int32_t lines, std::uint32_t id, std::uint32_t arg) {
    ThisCall(kPanelTextIndentedArg, U(self_), row, lines, id, arg);
}
void PanelDrawer::PanelTextLeftHalf(std::int32_t row, std::int32_t lines, std::uint32_t id) {
    ThisCall(kPanelTextLeftHalf, U(self_), row, lines, id);
}
void PanelDrawer::PanelTextRightHalf(std::int32_t row, std::int32_t lines, std::uint32_t id) {
    ThisCall(kPanelTextRightHalf, U(self_), row, lines, id);
}
void PanelDrawer::PanelTextAt(std::int32_t row, std::int32_t column, std::int32_t lines, std::uint32_t id) {
    ThisCall(kPanelTextAt, U(self_), row, column, lines, id);
}
void PanelDrawer::PanelTextAtFont1(std::int32_t row, std::int32_t column, std::int32_t lines, std::uint32_t id) {
    ThisCall(kPanelTextAtFont1, U(self_), row, column, lines, id);
}
void PanelDrawer::PanelTextAtArg(std::int32_t row, std::int32_t column, std::int32_t lines, std::uint32_t id,
                                 std::uint32_t arg) {
    ThisCall(kPanelTextAtArg, U(self_), row, column, lines, id, arg);
}
void PanelDrawer::PanelNumber(std::uint32_t column, std::uint32_t row, std::uint32_t value) {
    ThisCall(kPanelNumber, U(self_), column, row, value);
}
void PanelDrawer::PanelPercent(std::uint32_t column, std::uint32_t row, std::uint32_t value) {
    ThisCall(kPanelPercent, U(self_), column, row, value);
}
std::uint32_t PanelDrawer::PanelHundredths(std::uint32_t font, std::uint32_t x, std::uint32_t y, std::uint32_t value) {
    return ThisCall(kPanelHundredths, U(self_), font, x, y, value);
}
void PanelDrawer::PanelCaption(std::int32_t row, const char* text) {
    ThisCall(kPanelCaption, U(self_), row, U(text));
}
void PanelDrawer::PanelString(std::int32_t row, std::int32_t lines, msvc6::String text) {
    reinterpret_cast<PanelStringFn>(kPanelString)(self_, nullptr, row, lines, text);
}
void PanelDrawer::DrawStatisticsBar(std::uint32_t column, std::uint32_t row, std::uint32_t value) {
    ThisCall(kDrawStatisticsBar, U(self_), column, row, value);
}
void PanelDrawer::DrawValueBar(std::uint32_t left, std::uint32_t top, std::uint32_t right, std::uint32_t bottom,
                               std::uint32_t value, std::uint32_t limit) {
    ThisCall(kDrawValueBar, U(self_), left, top, right, bottom, value, limit);
}
void PanelDrawer::PutPixel(std::uint32_t x, std::uint32_t y, std::uint32_t colour) {
    ThisCall(kPutPixel, U(self_), x, y, colour);
}
std::uint32_t PanelDrawer::RaceOfPlayer(std::uint32_t player) {
    return ThisCall(kRaceOfPlayer, U(self_), player);
}
std::uint32_t PanelDrawer::BuildingAnimationSlot(std::uint32_t type) {
    return ThisCall(kBuildingAnimationSlot, U(self_), type);
}

// ---------------------------------------------------------------------------
// 0x4b4de3..0x4b5449: the minimap buttons, the panel background sprites and
// the menu tabs (each tab drawn pressed when its menu is open).
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kPanelBackground = 0x40000;   // raw-ok: animation ids (group 4)
constexpr std::uint32_t kMinimapBackground = 0x40002;  // raw-ok

// Menu numbers (data->unk060).
constexpr std::int32_t kMenuBuild = 1;
constexpr std::int32_t kMenuStatistics = 2;
constexpr std::int32_t kMenuSettlers = 3;
constexpr std::int32_t kMenuBuilding = 4;
constexpr std::int32_t kMenuSoldiers = 5;
constexpr std::int32_t kMenuPlayers = 6;
constexpr std::int32_t kMenu7 = 7;
constexpr std::int32_t kMenu8 = 8;
constexpr std::int32_t kMenu9 = 9;
constexpr std::int32_t kMenu10 = 10;
constexpr std::int32_t kMenu11 = 11;
constexpr std::int32_t kMenu12 = 12;

}  // namespace

void PanelDrawer::DrawFrame() {
    SpriteForMode(kPanelBackground, 0, 0, &screen_);
    SpriteForMode(kMinimapBackground, 0, static_cast<std::uint32_t>(g_layout730f34.get()), &screen_);
    // The original sets the buttons' last-drawn state here (0x4b4ed9).
    data_->panel_buttons_drawn = 0xffffffff;

    // The minimap buttons (0x4b4ee3..0x4b50d7): mode, settlers, buildings.
    const std::uint32_t c = Cell();
    const std::uint32_t left = static_cast<std::uint32_t>(g_layout730f2c.get());
    const std::uint32_t top = static_cast<std::uint32_t>(g_layout730f30.get());
    const std::int32_t mode = g_minimapMode;
    Gui(mode == 1 ? Img(kPanelGui, 342) : mode == 0 ? Img(kPanelGui, 345) : Img(kPanelGui, 348), left + 10 * c, top + c);
    const std::int32_t settlers = g_minimapShowSettlers;
    Gui(settlers == 0 ? Img(kPanelGui, 351) : settlers == 2 ? Img(kPanelGui, 354) : Img(kPanelGui, 357), left + 7 * c,
        top + 7 * c);
    Gui(g_minimapShowBuildings == 0 ? Img(kPanelGui, 363) : Img(kPanelGui, 360), left + 4 * c, top + 13 * c);
    SpriteForMode(kMinimapBackground, 0, static_cast<std::uint32_t>(g_layout730f34.get()), &screen_);

    // The menu tabs (0x4b515c..0x4b5444).
    const std::int32_t menu = data_->unk060;
    Gui(menu == kMenuBuild ? Img(kPanelGui, 60) : Img(kPanelGui, 51), X(0), Y(1));
    Gui(menu == kMenuStatistics ? Img(kPanelGui, 63) : Img(kPanelGui, 54), X(14), Y(1));
    Gui(menu == kMenuSettlers ? Img(kPanelGui, 66) : Img(kPanelGui, 57), X(28), Y(1));
    Gui(menu == kMenuPlayers ? Img(kPanelGui, 321) : Img(kPanelGui, 318), left, top);
    Gui(menu == kMenu7 ? Img(kPanelGui, 96) : Img(kPanelGui, 93), X(1), Y(97));
}

// ---------------------------------------------------------------------------
// Menu 6 (0x4c1fa3..0x4c23c1): the players: for each of the 20 players with
// a non-zero entry (data +0xa4 + 12 i: shown, the sprite's image / 2, a flag
// for the second sprite) the player's colour, a sprite, the name (a player
// with entry 1 by name, others in parentheses) and a second sprite. The
// rows are 5, 4 or 3 cells apart by g_strengthDivisor (at most 12, 15,
// more).
// ---------------------------------------------------------------------------

namespace {

constexpr std::uintptr_t kPalettePointer = 0x005fac10;      // CGraphicsData::PalettePointer(id, 0xff)
constexpr std::uintptr_t kColourAnimationPointer = 0x005fab50;
constexpr std::uintptr_t kListImage = 0x005fc5a0;
constexpr std::uintptr_t kDrawColorIndexed = 0x005fc5f0;
constexpr std::uint32_t kPlayerColours = 0x10000;   // raw-ok: palette ids 0x10000 + colour
constexpr std::uint32_t kAnyPalette = 0xff;
constexpr std::uint32_t kPlayerFigure = 0x40000;    // raw-ok: the colour-indexed figure (group 4)
constexpr std::uint32_t kPlayerIcons = 0x2000d;     // raw-ok: animation (group 2)
constexpr std::uint32_t kTicks = 0x40006;           // raw-ok: images 0 and 7 (off / on)
constexpr std::int32_t kPlayers = 20;
constexpr std::uint32_t kTickOff = 7;
constexpr std::uint32_t kWholeString = 0xffffffff;  // the n of a substring, pushed as -1 (not npos)

constexpr std::uintptr_t kStringDestroy = 0x00401b60;       // ~string
constexpr std::uintptr_t kStringAssignSubstr = 0x00401d60;   // assign(x, pos, n)
constexpr std::uintptr_t kStringCompareText = 0x0040a0e0;    // compare(const char*)
constexpr std::uintptr_t kStringFromText = 0x00407a50;       // string(const char*, alloc)
constexpr std::uintptr_t kStringTidy = 0x00402170;           // _Tidy(built)
constexpr std::uintptr_t kStringFromSubstr = 0x004078a0;     // string(x, pos, n, alloc)
constexpr std::uintptr_t kStringFromSubstrDefault = 0x00408cc0;  // string(x, pos, n)
constexpr std::uintptr_t kStringConcatCharsLeft = 0x00408e50;    // self = text + x (ECX, EDX, stack)
constexpr std::uintptr_t kStringConcatChars = 0x00401c70;        // self = x + text
using ConcatCharsLeftFn = msvc6::String*(__fastcall*)(msvc6::String* self, const char* left, const msvc6::String* right);
using ConcatCharsFn = msvc6::String*(__fastcall*)(msvc6::String* self, const msvc6::String* left, const char* right);

struct PlayerRow {  // data +0xa4 + 12 i
    std::int32_t shown;   // 0: no row; 1: the name without parentheses
    std::int32_t icon;    // the icon's image / 2
    std::int32_t ticked;  // 1: image 0 of kTicks, 0: image 7
};
static_assert(sizeof(PlayerRow) == 12);

}  // namespace

void PanelDrawer::DrawPlayers() {
    if (data_->unk060 != kMenuPlayers) {
        return;
    }
    PanelText(25, 1, Txt(255));
    const std::int32_t page = Value(0x194);
    PanelText(31, 1, page == 0 ? Txt(256) : page == 1 ? Txt(257) : Txt(258));
    const std::uint32_t divisor = g_strengthDivisor;
    const std::uint32_t step = divisor <= 12 ? 5 : divisor <= 15 ? 4 : 3;

    const auto* rows = reinterpret_cast<const PlayerRow*>(&data_->unk0a4);
    std::uint32_t row = 0;
    for (std::int32_t player = 0; player < kPlayers; ++player) {
        const PlayerRow& entry = rows[player];
        if (entry.shown == 0) {
            continue;
        }
        // The name: a copy of the player's, "no name" for an empty one
        // (0x4c203d..0x4c20e9).
        ThisCall(kStringAssignSubstr, U(&name_), U(&g_playerInfo[player].name), 0, g_stringNpos.get());
        if (ThisCall(kStringCompareText, U(&name_), U(texts::kEmpty.c_str())) == 0) {
            msvc6::String noName{};
            std::uint8_t allocator = 0;
            const std::uint32_t made = ThisCall(kStringFromText, U(&noName), U(texts::kNoName.c_str()), U(&allocator));
            ThisCall(kStringAssignSubstr, U(&name_), made, 0, g_stringNpos.get());
            ThisCall(kStringTidy, U(&noName), 1);
        }
        // The player's colour (0x4c20f0..0x4c2199).
        const std::uint32_t palette = ThisCall(kPalettePointer, U(self_->graphics),
                                               kPlayerColours + g_playerColourIndex[player], kAnyPalette);
        std::uint32_t out = 0;
        std::uint32_t figureList = *reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(
            ThisCall(kColourAnimationPointer, U(self_->graphics), U(&out), kPlayerFigure)));
        std::uint32_t figure = *reinterpret_cast<const std::uint32_t*>(
            static_cast<std::uintptr_t>(ThisCall(kListImage, U(&figureList), U(&out), 0)));
        std::uint32_t position = 0;
        ThisCall(kDrawColorIndexed, U(&figure), U(&self_->surface_descriptor),
                 ThisCall(kPoint16, U(&position), X(3), Y(row + 35)), U(&screen_), palette);
        SelectSprite(kPlayerIcons, entry.icon * 2);
        DrawSprite(X(5), Y(row + 33), &screen_, kFullBrightness);
        // The name (0x4c2227..0x4c22eb), passed by value: PanelString frees it.
        msvc6::String text{};
        if (entry.shown == 1) {
            std::uint8_t allocator = 0;
            ThisCall(kStringFromSubstr, U(&text), U(&name_), 0, kWholeString, U(&allocator));
            PanelString(static_cast<std::int32_t>(row + 35), 1, text);
        } else {
            msvc6::String opened{};
            reinterpret_cast<ConcatCharsLeftFn>(kStringConcatCharsLeft)(&opened, texts::kOpenParenthesisOnly, &name_);
            msvc6::String closed{};
            reinterpret_cast<ConcatCharsFn>(kStringConcatChars)(&closed, &opened, texts::kCloseParenthesis);
            ThisCall(kStringFromSubstrDefault, U(&text), U(&closed), 0, kWholeString);
            PanelString(static_cast<std::int32_t>(row + 35), 1, text);
            ThisCall(kStringDestroy, U(&closed));
            ThisCall(kStringDestroy, U(&opened));
        }
        SelectSprite(kTicks, static_cast<std::int32_t>(kTickOff - static_cast<std::uint32_t>(entry.ticked) * 7));
        DrawSprite(X(14), Y(row + 35), &screen_, kFullBrightness);
        row += step;
    }
}

// ---------------------------------------------------------------------------
// Menu 7 (0x4c23c7..0x4c24f5): pages of texts by data->unk074; page 2 lists
// eight pairs of caption lines from the draw data.
// ---------------------------------------------------------------------------

void PanelDrawer::DrawMenu7() {
    if (data_->unk060 != kMenu7) {
        return;
    }
    switch (data_->unk074) {
    case 0:
        PanelText(25, 1, Txt(259));
        if (data_->unk0a4 == 1) {
            PanelText(55, 1, Txt(368));
        }
        PanelText(90, 1, Txt(260));
        break;
    case 1:
        PanelText(50, 4, Txt(262));
        PanelTextLeftHalf(90, 1, Txt(231));
        PanelTextRightHalf(90, 1, Txt(232));
        break;
    case 2:
        PanelText(25, 1, Txt(259));
        for (std::int32_t k = 0; k < 8; ++k) {
            const std::int32_t row = 34 + 7 * k;
            PanelCaption(row - 3, msvc6::detail::CStr(&data_->panel_captions[k][0]));
            PanelCaption(row, msvc6::detail::CStr(&data_->panel_captions[k][1]));
        }
        break;
    case 3: PanelText(25, 1, Txt(285)); break;
    case 4: PanelText(30, 10, Txt(291)); break;
    case 5: PanelText(30, 4, Txt(299)); break;
    default: break;
    }
}

// Menus 8, 9, 10 and 12 (0x4c24fc..0x4c25dc): one text each (menu 8's
// from the draw data, with an image below).
void PanelDrawer::DrawMessageMenus() {
    const std::int32_t menu = data_->unk060;
    if (menu == kMenu8) {
        PanelText(30, 10, static_cast<std::uint32_t>(data_->unk090));
        Gui(Img(kPanelGui, 225), X(17), Y(84));
    }
    if (data_->unk060 == kMenu9) {
        PanelText(30, 10, Txt(290));
    }
    if (data_->unk060 == kMenu10) {
        PanelText(30, 10, Txt(292));
    }
    if (data_->unk060 == kMenu12) {
        PanelText(90, 1, Txt(260));
    }
}

// ---------------------------------------------------------------------------
// Menu 11 (0x4c5b90..0x4c5ec4): a comparison of seven goods (data +0xfc:
// their numbers, shown through the goods slot table): one or two columns of
// counts (+0xa4, and +0xc0 when +0xdc is set), and with +0x4ff a third
// column (+0xe0) and markers where the shown count is above or below it.
// ---------------------------------------------------------------------------

void PanelDrawer::DrawMenu11() {
    if (data_->unk060 != kMenu11) {
        return;
    }
    const bool second = Value(0xdc) != 0;
    PanelText(25, 1, Txt(368));
    const bool third = data_->unk4ff != 0;
    PanelTextFont1(32, 1, third ? (second ? Txt(345) : Txt(344)) : (second ? Txt(346) : Txt(347)));
    constexpr std::uint32_t kRows = 7;
    for (std::uint32_t i = 0; i < kRows; ++i) {
        const std::uint32_t image = GoodsGridImage(GoodsSlot(Value(0xfc + 4 * i)));
        if (image != kNoImage) {
            Gui(image, X(2), Y(7 * i + 36));
        }
    }
    if (data_->unk4ff != 0) {
        const std::uint32_t markColumn = second ? 25 : 15;
        for (std::uint32_t i = 0; i < kRows; ++i) {
            const auto shown = static_cast<std::uint32_t>(second ? Value(0xc0 + 4 * i) : Value(0xa4 + 4 * i));
            const auto other = static_cast<std::uint32_t>(Value(0xe0 + 4 * i));
            if (shown > other) {
                Gui(Img(kPanelGui, 339), X(markColumn - 3), Y(7 * i + 36));
            } else if (shown < other) {
                Gui(Img(kPanelGui, 339), X(32), Y(7 * i + 36));
            }
        }
    }
    for (std::uint32_t i = 0; i < kRows; ++i) {
        const std::uint32_t row = 39 + 7 * i;
        PanelNumber(15, row, static_cast<std::uint32_t>(Value(0xa4 + 4 * i)));
        if (second) {
            PanelNumber(25, row, static_cast<std::uint32_t>(Value(0xc0 + 4 * i)));
        }
        if (data_->unk4ff != 0) {
            // The original draws the third column's number seven times
            // (0x4c5e79..0x4c5e98), and its row's text once more each time.
            for (int repeat = 0; repeat < 7; ++repeat) {
                PanelNumber(35, row, static_cast<std::uint32_t>(Value(0xe0 + 4 * i)));
            }
            if (data_->unk4ff != 0) {
                PanelText(90, 1, Txt(260));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 0x4c5eca..0x4c62fe: the four buttons below the panel (bits 0..3 of
// data->panel_buttons: pressed), drawn when they changed and once more.
// ---------------------------------------------------------------------------

void PanelDrawer::DrawButtonRow() {
    if (data_->panel_buttons == data_->panel_buttons_drawn) {
        if (self_->panel_buttons_redraw == 0) {
            return;
        }
        self_->panel_buttons_redraw = 0;
    } else {
        self_->panel_buttons_redraw = 1;
    }
    const std::uint32_t buttons = data_->panel_buttons;
    Gui(buttons & 1 ? Img(kPanelGui, 114) : Img(kPanelGui, 102), X(8), Y(97));
    Gui(buttons & 2 ? Img(kPanelGui, 111) : Img(kPanelGui, 99), X(16), Y(97));
    Gui(buttons & 4 ? Img(kPanelGui, 117) : Img(kPanelGui, 105), X(24), Y(97));
    Gui(buttons & 8 ? Img(kPanelGui, 120) : Img(kPanelGui, 108), X(32), Y(97));
}

}  // namespace panel

namespace {
constexpr std::uintptr_t kStringDefault = 0x00407880;  // string(alloc)
constexpr std::uintptr_t kStringDestroy = 0x00401b60;  // ~string
}  // namespace

using DrawPanelFn = void(__fastcall*)(CDrawer* self, void* edx, DrawDataView* data);
void __fastcall CDrawer_DrawPanel(CDrawer* self, void* edx, DrawDataView* data);

S3RE_HOOK(0x004b4d00, CDrawer_DrawPanel, DrawPanelFn, Original_CDrawer_DrawPanel, CDrawer_DrawPanel);

// Purpose: draws the game screen's side panel (menu tabs, the open menu
// with its pictures, numbers and texts, the buttons below it) each frame
// when the logic thread asked for it.
void __fastcall CDrawer_DrawPanel(CDrawer* self, void* /*edx*/, DrawDataView* data) {
    panel::PanelDrawer drawer(self, data);
    std::uint8_t allocator = 0;
    panel::ThisCall(kStringDefault, panel::U(&drawer.name_), panel::U(&allocator));

    // 0x4b4db7..0x4b4ddd: draw this frame and the next one after a request.
    bool draw = false;
    if (data->redraw_panel == 1) {
        self->panel_redraw = 1;
        draw = true;
    } else if (self->panel_redraw == 1) {
        self->panel_redraw = 0;
        draw = true;
    }
    if (draw) {
        drawer.DrawFrame();
        // Each menu checks data->unk060 itself, in this order (0x4b5449,
        // 0x4b7c27, 0x4badf4, ...).
        drawer.DrawStatistics();
        drawer.DrawSettlerMenus();
        drawer.DrawBuildMenu();
        drawer.DrawSoldiers();
        drawer.DrawPlayers();
        drawer.DrawMenu7();
        drawer.DrawMessageMenus();
        drawer.DrawBuildingInfo();
        drawer.DrawMenu11();
    }
    drawer.DrawButtonRow();
    panel::ThisCall(kStringDestroy, panel::U(&drawer.name_));
}

}  // namespace s3re::game
