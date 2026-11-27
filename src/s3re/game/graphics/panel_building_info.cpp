// CDrawer::DrawPanel (0x004b4d00, panel_drawer.cpp), menu 4: the selected
// building (0x4c25df..0x4c5b90), drawn when +0xa4 (its type) is not 0.
// The page data->unk074:
//   0, 3    the building (its construction picture, name), for the types
//           15, 16, 18 of the local player (+0x98 == +0xa8, +0xb0 == 0) a
//           panel of eight pictures, three counts and two rows of goods;
//           buttons; its input goods (+0xb4..+0xc0) or, when +0xb0 is set,
//           a text and two goods; then more buttons
//   1, 2    the building with texts
//   4, 5    the trade grid: 36 goods with their counts (+0xb0 + 4 trade
//           good), the chosen one (+0x84) marked
//   6..13   a text
//   14, 15  a building with race pictures by the low byte of +0xc0 and
//           its input goods
//   18      the goods grid with ticks (+0x140..), the chosen good +0xac
// Fields: +0xa8 the owner, +0xac the race of its pictures, +0x98 the local
// player.

#include "game/graphics/panel_drawer.h"

#include "game/globals.h"
#include "game/tables.h"
#include "game/texts.h"

#include <cstdint>
#include <initializer_list>

namespace s3re::game::panel {

namespace {

constexpr std::int32_t kMenuBuilding = 4;
constexpr std::uint32_t kTicks = 0x40006;  // raw-ok: animation (group 4), image 0 on, 7 off
constexpr std::uint32_t kEmptySlot = 0xff;   // an input slot's value: an empty place
constexpr std::uint32_t kLockedSlot = 0xfe;  // probably a locked place
constexpr std::int32_t kGridSize = 6;
constexpr std::uintptr_t kStringFromChars = 0x00401a10;  // string(const char*, n)
constexpr std::uintptr_t kStringDestroy = 0x00401b60;
constexpr std::uintptr_t kDrawText = 0x004a8090;         // Dirk::CTextSurface::DrawText(result, font, rect, flags, text)
constexpr std::uint32_t kAllChars = 0xffffffff;
constexpr std::uint32_t kCentered = 9;
constexpr std::int32_t kTradeArrow = 100;  // a trade count of 100 is shown as "-->"

// The race goods of the input places (g_raceGoodImages columns), by good.
std::uint32_t RaceGoodColumn(std::int32_t good, bool secondGrid) {
    if (!secondGrid) {
        switch (good) {
        case 8: return 1;
        case 30: return 4;
        case 33: return 7;
        default: return 9;
        }
    }
    switch (good) {
    case 5: return 0;
    case 15: return 2;
    case 32: return 3;
    case 31: return 5;
    case 35: return 6;
    case 34: return 8;
    default: return 9;
    }
}

bool IsOneOf(std::int32_t value, std::initializer_list<std::int32_t> values) {
    for (std::int32_t v : values) {
        if (v == value) {
            return true;
        }
    }
    return false;
}

// Types 15, 16 and 18 (probably the residences / storehouses) get their own
// panel.
bool HasStockPanel(std::int32_t type) {
    return type == 15 || type == 16 || type == 18;
}

}  // namespace

void PanelDrawer::DrawBuildingInfo() {
    if (data_->unk060 != kMenuBuilding || data_->unk0a4 == 0) {
        return;
    }
    const std::int32_t type = data_->unk0a4;
    const std::uint32_t page = data_->unk074;
    if (page == 0 || page == 3) {
        DrawBuildingPage(type);
    } else if (page == 1) {
        DrawBuildingPicture(type, X(20), Y(65));
        BuildingName();
        PanelText(72, 3, Txt(230));
        PanelTextLeftHalf(90, 1, Txt(231));
        PanelTextRightHalf(90, 1, Txt(232));
    } else if (page == 2) {
        DrawBuildingPicture(type, X(20), Y(65));
        BuildingName();
        PanelText(82, 3, Txt(234));
    } else if (page == 4 || page == 5) {
        DrawTradePage();
    } else if (page == 6 || page == 7) {
        PanelText(25, 1, Txt(286));
    } else if (page == 8 || page == 9) {
        PanelText(25, 1, Txt(287));
    } else if (page == 10 || page == 11) {
        PanelText(25, 1, Txt(288));
    } else if (page == 12 || page == 13) {
        PanelText(25, 1, Txt(289));
    } else if (page == 14 || page == 15) {
        DrawShipPage(type, page == 14);
    } else if (page == 18) {
        DrawGoodsChoicePage();
    }
}

// The building's construction picture (its race's, +0xac) at (x, y),
// clipped to the panel area.
void PanelDrawer::DrawBuildingPicture(std::int32_t type, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t slot = BuildingAnimationSlot(static_cast<std::uint32_t>(type));
    const std::uint32_t* animations = &g_raceAnimations.get()[0][0];
    SelectSprite(animations[4 + slot * 4 + static_cast<std::uint32_t>(Value(0xac))], 0);
    DrawSprite(x, y, PanelArea(), kFullBrightness);
}

// The name over the picture, unless the stock panel replaces it.
void PanelDrawer::BuildingName() {
    const std::int32_t type = data_->unk0a4;
    if (!HasStockPanel(type) || Value(0xb0) != 0 || Value(0x98) != Value(0xa8)) {
        PanelText(39, 2, g_buildingNameTexts[type]);
    }
}

// Pages 0 and 3 (0x4c469c..0x4c5b8b).
void PanelDrawer::DrawBuildingPage(std::int32_t type) {
    const std::uint32_t slot = BuildingAnimationSlot(static_cast<std::uint32_t>(type));
    const auto owner = static_cast<std::uint32_t>(Value(0xa8));
    const std::uint32_t* animations = &g_raceAnimations.get()[0][0];
    SelectSprite(animations[4 + slot * 4 + static_cast<std::uint32_t>(Value(0xac))], 0);
    DrawSprite(X(20), Y(65), PanelArea(), kFullBrightness);
    BuildingName();
    if (HasStockPanel(data_->unk0a4) && Value(0xb0) == 0 && Value(0x98) == Value(0xa8)) {
        DrawStockPanel(owner);
    }

    // The building's picture button (0x4c4f10..0x4c50a7).
    const std::int32_t kind = data_->unk0a4;
    if (IsOneOf(kind, {2, 30, 14, 25, 36, 5, 24, 3, 51})) {
        if (Value(0x98) == Value(0xa8)) {
            Gui(Img(kPanelGui, 201), X(17), Y(23));
        }
    } else if (kind == 1 && Value(0xb0) != 0 && Value(0x98) == Value(0xa8)) {
        Gui(Img(kPanelGui, 213), X(17), Y(23));
    }

    // Two more buttons (0x4c50ac..0x4c5337): the first for buildings that
    // work or when +0xb0 is set (pressed by bit 0 of +0xc4).
    if (Value(0x98) == Value(0xa8)) {
        if (Value(0xb0) != 0 ||
            IsOneOf(data_->unk0a4, {2,  3,  4,  5,  7,  8,  9,  10, 11, 12, 13, 14, 17, 20, 21, 22, 23, 24,
                                    25, 29, 30, 31, 36, 37, 38, 39, 40, 47, 48, 49, 19, 50, 51, 52, 53, 56})) {
            Gui((Value(0xc4) & 1) != 0 ? Img(kPanelGui, 393) : Img(kPanelGui, 192), X(1), Y(23));
        }
        Gui(Img(kPanelGui, 198), X(33), Y(23));
    }

    if (Value(0x98) == Value(0xa8)) {
        DrawInputGoods();
    }

    // The last button (0x4c5805..0x4c5b8b).
    if (Value(0x98) != Value(0xa8)) {
        return;
    }
    if (Value(0xb0) != 0) {
        PanelText(79, 3, Txt(233));
        const auto flags = static_cast<std::uint32_t>(Value(0xc4));
        if ((flags & 1) == 0) {
            return;
        }
        Gui((flags & 0x1e) != 0 ? Img(kPanelGui, 378) : Img(kPanelGui, 195), X(8), Y(23));
        return;
    }
    switch (data_->unk0a4) {
    case 19: Gui(Img(RaceGui(0), 111), X(17), Y(23)); break;
    case 12: Gui(Img(kPanelGui, 126), X(17), Y(23)); break;
    case 13: Gui(Img(RaceGui(0), 111), X(17), Y(23)); break;
    case 26:
    case 27:
    case 28: Gui(Img(kPanelGui, 393), X(17), Y(23)); break;
    case 31:
    case 47:
    case 48:
    case 50: data_->unk074 = 14; break;
    default: break;
    }
}

// The stock panel of types 15, 16, 18 (0x4c47b4..0x4c4f0a): eight
// pictures, three counts (+0xcc..+0xd4), four places (+0xf0..) and a 3 x 2
// grid of places (+0xd8 + 4 column + 12 row). A place shows an empty or
// locked mark, nothing (0) or the owner race's picture of its good; for
// any other value the previous place's picture again. The first place
// starts from the previous Point16 (0x4c4bff: the original's register
// still holds that point; any non-zero value is drawn as an image id).
void PanelDrawer::DrawStockPanel(std::uint32_t owner) {
    PanelText(25, 1, g_buildingNameTexts[data_->unk0a4]);
    static constexpr std::uint32_t kPictures[2][4] = {{39, 27, 21, 24}, {42, 36, 30, 33}};
    for (std::uint32_t row = 0; row < 2; ++row) {
        for (std::uint32_t i = 0; i < 4; ++i) {
            Gui(Img(kPanelGui, kPictures[row][i]), X(2 + 10 * i), Y(74 + 12 * row));
        }
    }
    // The register the original reuses: the last Point16 (X(32), Y(86)).
    std::uint32_t image = (X(32) & 0xffff) | (Y(86) << 16);
    PanelNumber(15, 83, static_cast<std::uint32_t>(Value(0xcc)));
    PanelNumber(25, 83, static_cast<std::uint32_t>(Value(0xd0)));
    PanelNumber(35, 83, static_cast<std::uint32_t>(Value(0xd4)));

    const auto place = [&](std::int32_t value, bool secondGrid) {
        if (value == static_cast<std::int32_t>(kEmptySlot)) {
            image = Img(kPanelGui, 48);
            return true;
        }
        if (value == static_cast<std::int32_t>(kLockedSlot)) {
            image = Img(kPanelGui, 45);
            return true;
        }
        if (value == 0) {
            image = 0;
            return false;
        }
        const std::uint32_t column = RaceGoodColumn(value, secondGrid);
        if (column != 9) {
            const std::uint32_t race = RaceOfPlayer(owner);
            const std::uint32_t* images = &g_raceGoodImages.get()[0][0];
            image = images[race * 9 + column];
        }
        return image != 0;
    };
    for (std::uint32_t i = 0; i < 4; ++i) {
        if (place(Value(0xf0 + 4 * i), false)) {
            Gui(image, X(6 * i + 13), Y(28));
        }
    }
    for (std::uint32_t i = 0; i < 3; ++i) {
        for (std::uint32_t j = 0; j < 2; ++j) {
            if (place(Value(0xd8 + 4 * i + 12 * j), true)) {
                Gui(image, X(6 * i + 18 * j + 1), Y(44));
            }
        }
    }
}

// The input goods (0x4c533c..0x4c5800): up to four goods of the type
// (g_buildingPanelGoods) with their counts +0xb4..+0xc0, spread over the
// row by how many there are; with +0xb0 set a text and the goods 1 and 2.
// Each shown good and its column go to g_panelInputGoods / Columns.
void PanelDrawer::DrawInputGoods() {
    std::int32_t goods[4] = {0, 0, 0, 0};
    std::uint32_t columns[4] = {6, 14, 22, 34};
    if (Value(0xb0) != 0) {
        PanelText(34, 1, Txt(229));
        if (Value(0xb0) == 1) {
            PanelTextArg(49, 1, Txt(340), static_cast<std::uint32_t>(Value(0xc8)));
        }
        goods[0] = 1;
        goods[1] = 2;
        columns[0] = 15;
        columns[1] = 25;
    } else {
        if (IsOneOf(data_->unk0a4, {7, 9, 8, 29, 37, 56})) {
            PanelTextArg(80, 2, Txt(261), static_cast<std::uint32_t>(Value(0xcc)));
        }
        const std::int32_t* inputs = g_buildingPanelGoods[data_->unk0a4];
        for (int i = 0; i < 4; ++i) {
            goods[i] = inputs[i];
        }
        SpreadInputColumns(goods, columns);
    }
    for (std::int32_t& slot : g_panelInputGoods.get()) {
        slot = 0;
    }
    for (std::uint32_t i = 0; i < 4; ++i) {
        if (goods[i] == 0) {
            continue;
        }
        g_panelInputGoods[i] = goods[i];
        const std::uint32_t image = g_goodImages[goods[i]];
        g_panelInputColumns[i] = static_cast<std::int32_t>(columns[i]);
        Gui(image, X(columns[i] - 3), Y(82));
        PanelNumber(columns[i], 90, static_cast<std::uint32_t>(Value(0xb4 + 4 * i)));
    }
}

// 0x4c53f2..0x4c54ed: the columns of the four places by which are used.
void PanelDrawer::SpreadInputColumns(const std::int32_t* goods, std::uint32_t* columns) {
    const bool g0 = goods[0] != 0, g1 = goods[1] != 0, g2 = goods[2] != 0, g3 = goods[3] != 0;
    columns[0] = 6;
    columns[1] = 14;
    columns[2] = 22;
    columns[3] = 34;
    if (!g0) {
        if (g3) {
            columns[3] = 23;
        }
    } else if (!g1) {
        if (g3) {
            columns[0] = 14;
            columns[3] = 26;
        } else {
            columns[0] = 20;
        }
    } else if (g2 && g3) {
        // all four: the defaults
    } else if (g2) {
        columns[0] = 12;
        columns[1] = 20;
        columns[2] = 28;
    } else if (g3) {
        columns[0] = 10;
        columns[1] = 18;
        columns[3] = 30;
    } else {
        columns[0] = 16;
        columns[1] = 24;
    }
}

// Pages 4 and 5 (0x4c2a2d..0x4c2f3a): the trade grid.
void PanelDrawer::DrawTradePage() {
    ClearPanelText();
    if ((data_->unk074 & 1) == 0) {
        PanelText(25, 1, Txt(263));
        Gui(Img(kPanelGui, 372), X(4), Y(29));
    } else {
        PanelText(25, 1, Txt(264));
        Gui(Img(kPanelGui, 375), X(1), Y(29));
    }
    switch (Value(0xac)) {
    case 0: Gui(Img(kPanelGui, 192), X(0), Y(22)); break;
    case 1: Gui(Img(kPanelGui, 195), X(0), Y(22)); break;
    case 2: Gui(Img(kPanelGui, 378), X(0), Y(22)); break;
    default: break;
    }
    Gui(Img(kPanelGui, 198), X(34), Y(22));
    for (std::uint32_t row = 0; row < kGridSize; ++row) {
        for (std::uint32_t column = 0; column < kGridSize; ++column) {
            const std::uint32_t slot = row * kGridSize + column;
            const std::uint32_t image = g_goodsGridImages[slot];
            if (image == kNoImage) {
                continue;
            }
            Gui(image, X(6 * column + 2), Y(9 * row + 34));
            const std::uint32_t centreX = X(6 * column + 5);
            const std::uint32_t centreY = Y(9 * row + 41);
            Rect rect{};
            rect.top = static_cast<std::int32_t>(centreY - 2 * Cell());
            rect.bottom = static_cast<std::int32_t>(centreY + 2 * Cell());
            rect.left = static_cast<std::int32_t>(centreX - 6 * Cell());
            rect.right = static_cast<std::int32_t>(centreX + 6 * Cell());
            const std::int32_t count = Value(0xb0 + 4 * static_cast<std::uint32_t>(g_tradeMenuGoods[slot]));
            Rect result{};
            if (count == kTradeArrow) {
                msvc6::String arrow{};
                ThisCall(kStringFromChars, U(&arrow), U(texts::kArrow.c_str()), kAllChars);
                ThisCall(kDrawText, U(self_->text_surface), U(&result), 1, U(&rect), kCentered, U(&arrow));
                ThisCall(kStringDestroy, U(&arrow));
            } else {
                DrawTextFormat(rect, texts::kFormatUnsigned, static_cast<std::uint32_t>(count));
            }
            const auto left = static_cast<std::uint32_t>(rect.left);
            const auto top = static_cast<std::uint32_t>(rect.top);
            BlitTextRect(left, top, static_cast<std::uint32_t>(rect.right) - left,
                         static_cast<std::uint32_t>(rect.bottom) - top);
            if (static_cast<std::uint32_t>(data_->unk084) == slot) {
                Gui(Img(kPanelGui, 339), X(6 * column + 2), Y(9 * row + 34));
            }
        }
    }
    Gui(Img(kPanelGui, 384), X(1), Y(88));
    Gui(Img(kPanelGui, 387), X(7), Y(88));
    Gui(Img(kPanelGui, 219), X(15), Y(88));
    Gui(Img(kPanelGui, 225), X(21), Y(88));
    Gui(Img(kPanelGui, 228), X(27), Y(88));
    Gui(Img(kPanelGui, 222), X(33), Y(88));
}

// Pages 14 and 15 (0x4c3110..0x4c4134): a building of the race +0xac with
// two pictures chosen by the high nibble of +0xc0's low byte, and two or
// three of its input goods.
void PanelDrawer::DrawShipPage(std::int32_t type, bool page14) {
    const auto race = static_cast<std::uint32_t>(Value(0xac));
    const std::uint32_t slot = BuildingAnimationSlot(static_cast<std::uint32_t>(type));
    const std::uint32_t* animations = &g_raceAnimations.get()[0][0];
    SelectSprite(animations[4 + slot * 4 + race], 0);
    DrawSprite(X(28), page14 ? Y(60) : Y(65), PanelArea(), kFullBrightness);
    PanelText(32, 2, g_buildingNameTexts[data_->unk0a4]);
    Gui(Img(kPanelGui, 198), X(33), Y(23));
    const std::uint32_t state = static_cast<std::uint32_t>(Value(0xc0)) & 0xf0;
    if (page14) {
        // {state 0x10 ? a : b} at row 42, {state 0x20 ? c : d} at row 56.
        static constexpr std::uint16_t kPictures[4][4] = {
            {237, 234, 243, 240}, {231, 228, 237, 234}, {246, 243, 252, 249}, {240, 237, 0, 0}};
        if (race < 4) {
            const std::uint16_t* p = kPictures[race];
            Gui(Img(RaceGui(static_cast<std::int32_t>(race)), state == 0x10 ? p[0] : p[1]), X(1), Y(42));
            if (race != 3) {
                Gui(Img(RaceGui(static_cast<std::int32_t>(race)), state == 0x20 ? p[2] : p[3]), X(1), Y(56));
            }
        }
    } else {
        Gui(Img(kPanelGui, 390), X(33), Y(35));
        static constexpr std::uint16_t kPictures[4][4] = {
            {279, 276, 273, 270}, {267, 264, 273, 270}, {288, 285, 282, 279}, {282, 279, 276, 273}};
        if (race < 4) {
            const std::uint16_t* p = kPictures[race];
            Gui(Img(RaceGui(static_cast<std::int32_t>(race)), state == 0x30 ? p[0] : p[1]), X(1), Y(35));
            Gui(Img(RaceGui(static_cast<std::int32_t>(race)), state == 0x40 ? p[2] : p[3]), X(1), Y(45));
        }
    }
    Gui(Img(kPanelGui, 366), X(1), Y(23));
    const std::int32_t* inputs = g_buildingPanelGoods[data_->unk0a4];
    const std::int32_t second = inputs[1];
    const std::int32_t third = inputs[2];
    Gui(g_goodImages[inputs[0]], X(9), Y(84));
    PanelNumber(12, 92, static_cast<std::uint32_t>(Value(0xb4)));
    Gui(g_goodImages[second], X(17), Y(84));
    if (page14) {
        PanelNumber(20, 92, static_cast<std::uint32_t>(Value(0xb8)));
        Gui(g_goodImages[third], X(25), Y(84));
        PanelNumber(28, 92, static_cast<std::uint32_t>(Value(0xbc)));
    } else {
        PanelNumber(20, 92, static_cast<std::uint32_t>(Value(0xb8)));
    }
    PanelText(81, 3, Txt(233));
}

// Page 18 (0x4c3e4a..0x4c4693): the goods grid; with +0xac = 0xff every
// available good gets its tick (on when +0x140 + 4 slot is 0... image 7,
// else image 0); otherwise the chosen good +0xac is marked.
void PanelDrawer::DrawGoodsChoicePage() {
    const std::int32_t chosen = Value(0xac);
    const std::int32_t picture = Value(0xa8);
    PanelText(25, 1, Txt(1));
    const std::uint32_t* animations = &g_raceAnimations.get()[0][0];
    SelectSprite(animations[4 + static_cast<std::uint32_t>(picture)], 0);
    DrawSprite(X(20), Y(60), PanelArea(), kFullBrightness);
    Gui(Img(kPanelGui, 213), X(1), Y(23));
    Gui(Img(kPanelGui, 198), X(33), Y(23));
    for (std::uint32_t row = 0; row < kGridSize; ++row) {
        for (std::uint32_t column = 0; column < kGridSize; ++column) {
            const std::uint32_t slot = row * kGridSize + column;
            const std::uint32_t image = g_goodsGridImages[slot];
            if (image == kNoImage) {
                continue;
            }
            const std::uint32_t brightness = Value(0xb0 + 4 * slot) != 0 ? kFullBrightness : kDimmed;
            Gui(image, X(6 * column + 2), Y(6 * row + 58), brightness);
            if (chosen == static_cast<std::int32_t>(kEmptySlot) && brightness == kFullBrightness) {
                SelectSprite(kTicks, Value(0x140 + 4 * slot) == 0 ? 7 : 0);
                DrawSprite(X(6 * column + 4), Y(6 * row + 59), &screen_, kFullBrightness);
            }
        }
    }
    if (chosen != static_cast<std::int32_t>(kEmptySlot) && GoodsGridImage(chosen) != kNoImage) {
        const auto good = static_cast<std::uint32_t>(chosen);
        std::uint32_t column = good % 6;
        std::uint32_t row = good / 6 + 1;
        if (column == 0) {
            column = 6;
            row = good / 6;
        }
        Gui(Img(kPanelGui, 339), X(6 * column - 4), Y(6 * row + 52));
    }
}

}  // namespace s3re::game::panel
