#pragma once

// Internal to the side panel drawer CDrawer::DrawPanel (0x004b4d00,
// panel_drawer*.cpp): the drawing state the original keeps in its 4 KB
// stack frame, and the primitives every menu uses. See panel_drawer.cpp
// for the overview.
//
// Positions are in "cells" of g_uiCellSize pixels from the drawing origin
// (g_drawOriginX / Y). Everything the original computes for a Point16 is
// cut to 16 bits by Point16::Point16 (0x5fdab0), so the order of the
// additions and multiplications does not matter; values passed to other
// functions are computed in 32-bit unsigned arithmetic as the original
// does.

#include "game/globals.h"
#include "game/graphics/draw_data_layout.h"
#include "game/graphics/drawer_layout.h"
#include "game/tables.h"
#include "msvc6/string.h"

#include <cstdint>

namespace s3re::game::panel {

// A GUI image id (CGraphicsData::GuiImagePointerAt 0x5faca0): GFX file
// (group) in the high word, image in the low word.
constexpr std::uint32_t Img(std::uint32_t file, std::uint32_t index) {
    return file << 16 | index;
}
// The GUI files of the four races (Romans 14, Egyptians 24, Asians 34,
// Amazons 44; probably).
constexpr std::uint32_t RaceGui(std::int32_t race) {
    return 14u + 10u * static_cast<std::uint32_t>(race);
}
constexpr std::uint32_t kPanelGui = 3;  // the panel's own GUI file

// A game text id (CGraphicsData::TextAddress 0x5faaa0, group 8).
constexpr std::uint32_t Txt(std::uint32_t index) {
    return 8u << 16 | index;
}

constexpr std::uint32_t kFullBrightness = 255;
constexpr std::uint32_t kDimmed = 100;      // greyed out (not available)
constexpr std::uint32_t kHalfDimmed = 128;  // 0x80: nothing of this kind
constexpr std::uint32_t kWholeSprite = 0x10000;  // DrawSprite's fraction: all of it (16.16)
constexpr std::uint32_t kNoImage = 0xffffffff;   // an empty slot of an image table

struct Rect {
    std::int32_t left, top, right, bottom;
};
static_assert(sizeof(Rect) == 16);

// A pointer as the dword the original passes.
template <typename T>
std::uint32_t U(T* pointer) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

// A __thiscall of an S3.EXE function with dword arguments (as __fastcall
// with the dummy EDX, see hooks.h).
template <typename... Args>
std::uint32_t ThisCall(std::uintptr_t address, std::uint32_t self, Args... args) {
    using Fn = std::uint32_t(__fastcall*)(std::uint32_t, std::uint32_t, decltype(static_cast<std::uint32_t>(args))...);
    return reinterpret_cast<Fn>(address)(self, 0, static_cast<std::uint32_t>(args)...);
}

// The image of a slot (1..36) of the goods grid; slot 0 reads the dword
// before the table, as the original does (0x6a5714).
inline std::uint32_t GoodsGridImage(std::int32_t slot) {
    const std::uint32_t* images = g_goodsGridImages.get();
    return images[slot - 1];
}
// The grid slot of a good number (the original reads 0x658a93 + good).
inline std::int32_t GoodsSlot(std::int32_t good) {
    const std::int8_t* slots = g_goodsGridSlot.get();
    return slots[good - 1];
}

class PanelDrawer {
public:
    PanelDrawer(CDrawer* self, DrawDataView* data);

    // --- the parts (one per menu; panel_drawer*.cpp) ---
    void DrawFrame();            // 0x4b4de3..0x4b5449 (panel_drawer.cpp)
    void DrawStatistics();       // menu 2 (panel_statistics.cpp)
    void DrawSettlerMenus();     // menu 3 (panel_settlers.cpp)
    void DrawBuildMenu();        // menu 1 (panel_build_menu.cpp)
    void DrawSoldiers();         // menu 5 (panel_soldiers.cpp)
    void DrawPlayers();          // menu 6 (panel_drawer.cpp)
    void DrawMenu7();            // menu 7 (panel_drawer.cpp)
    void DrawMessageMenus();     // menus 8, 9, 10, 12 (panel_drawer.cpp)
    void DrawBuildingInfo();     // menu 4 (panel_building_info.cpp)
    void DrawMenu11();           // menu 11 (panel_drawer.cpp)
    void DrawButtonRow();        // 0x4c5eca..0x4c62fe (panel_drawer.cpp)

    // --- parts of the menus ---
    void DrawShares();                                             // panel_statistics.cpp
    void DrawGoodsMap();
    void DrawHistoryChart();                                       // panel_settlers.cpp
    void DottedLine(std::uint32_t width, std::uint32_t middle, std::uint32_t gap, bool shown, std::uint32_t y);
    std::uint32_t RaceSettlerImage(std::uint32_t kind) const;
    void DrawSoldierCounts();                                      // panel_soldiers.cpp
    void DrawBuildingPicture(std::int32_t type, std::uint32_t x, std::uint32_t y);  // panel_building_info.cpp
    void BuildingName();
    void DrawBuildingPage(std::int32_t type);
    void DrawStockPanel(std::uint32_t owner);
    void DrawInputGoods();
    static void SpreadInputColumns(const std::int32_t* goods, std::uint32_t* columns);
    void DrawTradePage();
    void DrawShipPage(std::int32_t type, bool page14);
    void DrawGoodsChoicePage();

    // --- positions ---
    std::uint32_t Cell() const { return static_cast<std::uint32_t>(g_uiCellSize.get()); }
    std::uint32_t X(std::uint32_t cells) const { return g_drawOriginX + cells * Cell(); }
    std::uint32_t Y(std::uint32_t cells) const { return g_drawOriginY + cells * Cell(); }

    // --- primitives (calls of the original functions, in the original's order) ---
    // GuiImagePointerAt(id, display mode - 1), Point16(x, y), DrawGuiImage
    // with the screen as the clip.
    void Gui(std::uint32_t id, std::uint32_t x, std::uint32_t y, std::uint32_t brightness = kFullBrightness);
    // An animation's image for the display mode (the inlined
    // GfxDword::ListImageChecked: image mode - 1 if the list has it, else 0),
    // drawn by DrawSprite.
    void SpriteForMode(std::uint32_t animation, std::uint32_t x, std::uint32_t y, const Rect* clip);
    // AnimationPointer(animation) into list_; the out-of-line
    // GfxDword::ListImageChecked 0x4b19c0 (index) into sprite_.
    void SelectSprite(std::uint32_t animation, std::int32_t index);
    // The inlined form of the same check (index < 0 or past the list: image 0).
    void SelectSpriteInline(std::uint32_t animation, std::int32_t index);
    // DrawSprite of sprite_ at (x, y).
    void DrawSprite(std::uint32_t x, std::uint32_t y, const Rect* clip, std::uint32_t brightness);

    // CDrawer helpers (text_panels.cpp, value_bar.cpp, ...), by address.
    void ClearPanelText();       // ClearTextRect of the panel area +0x5f654
    void PanelText(std::int32_t row, std::int32_t lines, std::uint32_t id);
    void PanelTextFont1(std::int32_t row, std::int32_t lines, std::uint32_t id);
    void PanelTextArg(std::int32_t row, std::int32_t lines, std::uint32_t id, std::uint32_t arg);
    void PanelTextIndentedArg(std::int32_t row, std::int32_t lines, std::uint32_t id, std::uint32_t arg);
    void PanelTextLeftHalf(std::int32_t row, std::int32_t lines, std::uint32_t id);
    void PanelTextRightHalf(std::int32_t row, std::int32_t lines, std::uint32_t id);
    void PanelTextAt(std::int32_t row, std::int32_t column, std::int32_t lines, std::uint32_t id);
    void PanelTextAtFont1(std::int32_t row, std::int32_t column, std::int32_t lines, std::uint32_t id);
    void PanelTextAtArg(std::int32_t row, std::int32_t column, std::int32_t lines, std::uint32_t id,
                        std::uint32_t arg);
    void PanelNumber(std::uint32_t column, std::uint32_t row, std::uint32_t value);
    void PanelPercent(std::uint32_t column, std::uint32_t row, std::uint32_t value);
    std::uint32_t PanelHundredths(std::uint32_t font, std::uint32_t x, std::uint32_t y, std::uint32_t value);
    void PanelCaption(std::int32_t row, const char* text);
    void PanelString(std::int32_t row, std::int32_t lines, msvc6::String text);  // the callee frees text
    void DrawStatisticsBar(std::uint32_t column, std::uint32_t row, std::uint32_t value);
    void DrawValueBar(std::uint32_t left, std::uint32_t top, std::uint32_t right, std::uint32_t bottom,
                      std::uint32_t value, std::uint32_t limit);
    void PutPixel(std::uint32_t x, std::uint32_t y, std::uint32_t colour);
    void ClearTextRect(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height);
    void BlitTextRect(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height);
    // Dirk::CTextSurface::DrawTextF(font 1, rect, centred, format, value);
    // with the blit of the rect (BlitTextRect) after it, or only "%u".
    void DrawTextFormat(const Rect& rect, const char* format, std::uint32_t value);
    void TextInRect(const Rect& rect, const char* format, std::uint32_t value);
    void NumberInRect(const Rect& rect, std::uint32_t value);
    std::uint32_t RaceOfPlayer(std::uint32_t player);  // Render::Entry204 0x4c7ab0
    std::uint32_t BuildingAnimationSlot(std::uint32_t type);  // 0x4b1a00

    // Draw-data values of the panel (meaning per menu).
    std::int32_t Value(std::uint32_t offset) const;
    std::int32_t Race() const { return data_->unk094; }
    // The drawer's panel area +0x5f654 (the clip of some sprites).
    const Rect* PanelArea() const { return reinterpret_cast<const Rect*>(&self_->panel_left); }

    CDrawer* self_;
    DrawDataView* data_;
    Rect screen_;      // [esp+0x14] (0, 0, display width, display height): the clip of the GUI images
    Rect textRect_;    // [esp+0x78] the rect of the number texts, first the panel area +0x5f654
    std::uint32_t list_ = 0;    // [esp+0x58] the image list of the last animation
    std::uint32_t sprite_ = 0;  // [esp+0x4c] the image DrawSprite draws
    msvc6::String name_{};      // [esp+0xbc] the player name of menu 6
};

}  // namespace s3re::game::panel
