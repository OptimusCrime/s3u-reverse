// CDrawer::DrawPanel (0x004b4d00, panel_drawer.cpp), menu 3: the settler
// pages (0x4b7c27..0x4badf4), page data->unk07c 1..4, each with its tab
// pressed:
//   page 1  the settlers by kind: twelve race pictures with their counts
//           (+0xb0..+0xec)
//   page 2  three texts with numbers (+0xb0..+0xb8) and four groups of
//           soldier pictures
//   page 3  three choices (+0xb0..+0xb8: 0, 10, 1, 11 select the picture),
//           two texts with numbers (+0xdc, +0xe0), seven counts
//           (+0xbc..+0xd4) and a text by +0xd8
//   page 4  the history chart of the chosen good (+0x2ec, its history
//           +0x2f0, scale +0x4f0 and four marks +0x4f6) above the goods
//           grid (dimmed where +0xa4 + 4 slot is 0)
// Pages 2 and 3 only when +0xa4 is not 0.

#include "game/graphics/panel_drawer.h"

#include "game/tables.h"

#include <cstdint>
#include <initializer_list>

namespace s3re::game::panel {

namespace {

constexpr std::int32_t kMenuSettlers = 3;
constexpr std::uintptr_t kPackColour = 0x004c7b00;  // Render::PackColour(r, g, b), stdcall
constexpr std::uint32_t kWhite = 0xffff;
constexpr std::int32_t kGridSize = 6;

using PackColourFn = std::uint32_t(__stdcall*)(std::uint32_t r, std::uint32_t g, std::uint32_t b);

// Kinds of g_raceSettlerImages.
enum SettlerPicture : std::uint32_t {
    kPicture0 = 0, kPicture1, kPicture2, kPicture3, kPicture4, kPicture5,
    kPicture6, kPicture7, kPicture8, kPicture9, kPicture10, kPicture11,
};

}  // namespace

std::uint32_t PanelDrawer::RaceSettlerImage(std::uint32_t kind) const {
    // [kind][race], with the race not range-checked as in the original.
    const std::uint32_t* images = &g_raceSettlerImages.get()[0][0];
    return images[kind * 4 + static_cast<std::uint32_t>(Race())];
}

void PanelDrawer::DrawSettlerMenus() {
    if (data_->unk060 != kMenuSettlers) {
        return;
    }
    const std::int32_t page = data_->unk07c;
    Gui(page == 1 ? Img(kPanelGui, 246) : Img(kPanelGui, 234), X(0), Y(12));
    Gui(page == 2 ? Img(kPanelGui, 249) : Img(kPanelGui, 237), X(10), Y(12));
    Gui(page == 3 ? Img(kPanelGui, 252) : Img(kPanelGui, 240), X(20), Y(12));
    Gui(page == 4 ? Img(kPanelGui, 255) : Img(kPanelGui, 243), X(30), Y(12));

    // Page 1 (0x4b7e96..0x4b882d).
    if (data_->unk07c == 1) {
        PanelText(25, 1, Txt(251));
        Gui(Img(kPanelGui, 393), X(4), Y(28));
        Gui(g_goodImages[13], X(12), Y(28));
        Gui(Img(kPanelGui, 3), X(20), Y(31));
        Gui(g_goodImages[18], X(30), Y(28));
        static constexpr std::uint32_t kRows[3] = {41, 59, 77};
        for (std::uint32_t i = 0; i < 11; ++i) {
            Gui(RaceSettlerImage(i), X(1 + 10 * (i % 4)), Y(kRows[i / 4]));
        }
        Gui(Img(kPanelGui, 0), X(31), Y(77));
        struct Count {
            std::uint32_t column, row, offset;
        };
        static constexpr Count kCounts[] = {
            {24, 30, 0xb0}, {15, 35, 0xb4}, {7, 35, 0xe8},  {33, 35, 0xb8}, {5, 55, 0xbc},  {15, 55, 0xc0},
            {25, 55, 0xc4}, {35, 55, 0xc8}, {5, 73, 0xcc},  {15, 73, 0xd0}, {25, 73, 0xd4}, {35, 73, 0xd8},
            {5, 91, 0xdc},  {15, 91, 0xe0}, {25, 91, 0xe4}, {35, 91, 0xec},
        };
        for (const Count& count : kCounts) {
            PanelNumber(count.column, count.row, static_cast<std::uint32_t>(Value(count.offset)));
        }
    }

    // Page 2 (0x4b8833..0x4b95d3).
    if (data_->unk07c == 2 && data_->unk0a4 != 0) {
        Gui(Img(kPanelGui, 315), X(0), Y(22));
        Gui(Img(kPanelGui, 312), X(34), Y(22));
        Gui(Img(kPanelGui, 231), X(2), Y(30));
        Gui(Img(kPanelGui, 231), X(2), Y(38));
        Gui(Img(kPanelGui, 231), X(2), Y(46));
        PanelText(25, 1, Txt(238));
        PanelTextAtArg(33, 8, 1, Txt(239), static_cast<std::uint32_t>(Value(0xb0)));
        PanelTextAtArg(41, 8, 1, Txt(240), static_cast<std::uint32_t>(Value(0xb4)));
        PanelTextAtArg(49, 8, 1, Txt(241), static_cast<std::uint32_t>(Value(0xb8)));
        PanelText(57, 1, Txt(248));
        // Groups: three frames (not for the last), the soldier and two marks.
        const auto group = [this](std::uint32_t column, std::uint32_t row, std::uint32_t kind, bool frames) {
            if (frames) {
                Gui(Img(kPanelGui, 432), X(column), Y(row));
                Gui(Img(kPanelGui, 435), X(column), Y(row + 4));
                Gui(Img(kPanelGui, 438), X(column), Y(row + 8));
            }
            Gui(RaceSettlerImage(kind), X(column + 4), Y(row));
            Gui(Img(kPanelGui, 219), X(column + 12), Y(row));
            Gui(Img(kPanelGui, 225), X(column + 12), Y(row + 6));
        };
        group(1, 62, kPicture8, true);
        group(1, 76, kPicture9, true);
        group(21, 62, kPicture10, true);
        group(21, 76, kPicture0, false);
    }

    // Page 3 (0x4b95d9..0x4ba67e).
    if (data_->unk07c == 3 && data_->unk0a4 != 0) {
        PanelText(25, 1, Txt(242));
        PanelTextArg(32, 1, Txt(271), static_cast<std::uint32_t>(Value(0xdc)));
        PanelTextIndentedArg(40, 1, Txt(272), static_cast<std::uint32_t>(Value(0xe0)));
        // Each of the three choices: the picture of its value 0, 10, 1 or 11.
        static constexpr std::int32_t kChoiceValues[4] = {0, 10, 1, 11};
        for (std::uint32_t choice = 0; choice < 3; ++choice) {
            for (std::uint32_t k = 0; k < 4; ++k) {
                if (Value(0xb0 + 4 * choice) == kChoiceValues[k]) {
                    Gui(Img(kPanelGui, 396 + 12 * choice + 3 * k), X(2 + 12 * choice), Y(43));
                }
            }
        }
        Gui(Img(kPanelGui, 315), X(0), Y(22));
        Gui(Img(kPanelGui, 312), X(34), Y(22));
        Gui(Img(RaceGui(0), 111), X(2), Y(62));
        Gui(Img(RaceGui(0), 114), X(2), Y(70));
        Gui(RaceSettlerImage(kPicture0), X(2), Y(82));
        Gui(Img(RaceGui(0), 117), X(2), Y(78));
        for (std::uint32_t row : {62u, 70u, 78u}) {
            Gui(Img(kPanelGui, 6), X(16), Y(row));
            Gui(Img(kPanelGui, 12), X(20), Y(row));
            Gui(Img(kPanelGui, 15), X(30), Y(row));
            Gui(Img(kPanelGui, 9), X(34), Y(row));
        }
        PanelText(57, 1, Txt(247));
        PanelNumber(11, 65, static_cast<std::uint32_t>(Value(0xbc)));
        PanelNumber(11, 73, static_cast<std::uint32_t>(Value(0xc0)));
        PanelNumber(11, 81, static_cast<std::uint32_t>(Value(0xc4)));
        PanelNumber(11, 89, static_cast<std::uint32_t>(Value(0xc8)));
        PanelNumber(27, 65, static_cast<std::uint32_t>(Value(0xcc)));
        PanelNumber(27, 73, static_cast<std::uint32_t>(Value(0xd0)));
        PanelNumber(27, 81, static_cast<std::uint32_t>(Value(0xd4)));
        PanelTextAt(89, 16, 1, Value(0xd8) == 0 ? Txt(244) : Txt(243));
    }

    // Page 4 (0x4ba684..0x4badee).
    if (data_->unk07c == 4) {
        ClearPanelText();
        PanelText(25, 1, Txt(337));
        const std::uint32_t good = data_->unk2ec;
        if (good != 0 && GoodsGridImage(static_cast<std::int32_t>(good)) != kNoImage) {
            DrawHistoryChart();
        }
        for (std::int32_t row = 0; row < kGridSize; ++row) {
            for (std::int32_t column = 0; column < kGridSize; ++column) {
                const std::int32_t slot = row * kGridSize + column;
                const std::uint32_t image = g_goodsGridImages[slot];
                if (image != kNoImage) {
                    const auto r = static_cast<std::uint32_t>(row);
                    const auto k = static_cast<std::uint32_t>(column);
                    Gui(image, X(6 * k + 2), Y(6 * r + 58), Value(0xa4 + 4 * slot) != 0 ? kFullBrightness : kDimmed);
                }
            }
        }
        // The marker on the chosen good's slot (0x4bad14..0x4badef).
        const std::uint32_t chosen = data_->unk2ec;
        if (chosen != 0 && GoodsGridImage(static_cast<std::int32_t>(chosen)) != kNoImage) {
            std::uint32_t column = chosen % 6;
            std::uint32_t row = chosen / 6 + 1;
            if (column == 0) {
                column = 6;
                row = chosen / 6;
            }
            Gui(Img(kPanelGui, 339), X(6 * column - 4), Y(6 * row + 52));
        }
    }
}

// Page 4's chart (0x4ba6e1..0x4bac05), in pixels from the screen's corner
// (not the drawing origin): the axes, a scale of 15 per 8 cells on the x
// axis, the history as red columns (height history * 24 C / scale, at most
// the scale) over 32 C pixels, four marks of the scale with their values
// and dotted lines, and the scale's top line with its value. The original
// draws each mark's values and dotted line four times.
void PanelDrawer::DrawHistoryChart() {
    const std::uint32_t c = Cell();
    const std::uint32_t width = c << 5;           // [esp+0x3c] 32 C
    const std::uint32_t middle = width >> 1;      // [esp+0x28] 16 C
    const std::uint32_t labelX = middle + 4 * c;  // [esp+0x50]
    const std::uint32_t base = 53 * c;            // [esp+0x38] the x axis
    const std::uint32_t right = 36 * c;           // [esp+0x68] the y axis
    const std::uint32_t top = 29 * c;             // [esp+0x40] the top line

    // The x axis (0x4ba6fa..0x4ba739).
    for (std::uint32_t x = 38 * c; x >= 2 * c; --x) {
        PutPixel(x, base, kWhite);
    }
    // Its ticks and numbers every 8 cells (0x4ba73b..0x4ba803).
    std::uint32_t label = 0;
    std::uint32_t x = right;
    for (std::uint32_t i = 36; i >= 4; --i, x -= c) {
        if ((i & 7) == 0) {
            PutPixel(x - 4 * c, base + 1, kWhite);
            PutPixel(x - 4 * c, base + 2, kWhite);
            PutPixel(x - 4 * c, base + 3, kWhite);
            label += 15;
            PanelNumber(i - 4, 56, label);
        }
    }
    // The right and left edges and the top line's left end (0x4ba809..0x4ba8d8).
    for (std::uint32_t y = 55 * c; y >= 27 * c; --y) {
        PutPixel(right, y, kWhite);
    }
    for (std::uint32_t y = base; y >= top; --y) {
        PutPixel(4 * c, y, kWhite);
    }
    for (std::uint32_t i = 1; i <= 2 * c; ++i) {
        PutPixel(4 * c - i, top, kWhite);
    }

    // The history (0x4ba8da..0x4ba9ad): column i from the right shows entry
    // position >> 16, the position advancing 0x1000000 / width per column.
    const std::uint32_t step = 0x1000000 / width;  // raw-ok: 16.16 fraction, not an offset
    std::uint32_t position = 0;
    for (std::uint32_t i = 1; i < width; ++i) {
        const std::uint32_t scale = data_->unk4f0;
        std::uint32_t height = 0;
        if (scale != 0) {
            height = data_->unk2f0[position >> 16] * c * 24 / scale;
        }
        if (height > scale) {
            height = scale;
        }
        for (std::uint32_t j = 1; j <= height; ++j) {
            const std::uint32_t red = reinterpret_cast<PackColourFn>(kPackColour)(256, 0, 0);
            PutPixel(right - i, base - j, red);
        }
        position += step;
    }

    // The four marks (0x4ba9b3..0x4bab32).
    const std::uint32_t limit = c * 2160 / 100;  // [esp+0x88]
    // [esp+0x24]: before the first mark's value it still holds the history
    // loop's count (only read when nothing is drawn with it).
    std::uint32_t labelHalfWidth = width > 1 ? width : 1;
    for (std::uint32_t mark = 0; mark < 4; ++mark) {
        const std::uint32_t scale = data_->unk4f0;
        std::uint32_t height = 0;
        if (scale != 0) {
            height = data_->unk4f6[mark] * c * 24 / scale;
        }
        for (std::uint32_t k = 1; k <= 4; ++k) {
            if (height <= limit) {
                const std::uint32_t y = base - height;
                labelHalfWidth = PanelHundredths(1, labelX, y - 7, data_->unk4f6[mark]) * 3;
                PutPixel(k + right, y, kWhite);
                PutPixel(4 * c - k, y, kWhite);
            }
            DottedLine(width, middle, labelHalfWidth, height <= limit, base - height);
        }
    }

    // The top line, its value and its dotted line (0x4bab38..0x4bac05).
    for (std::uint32_t i = 0; i < 2 * c; ++i) {
        PutPixel(right + 1 + i, top, kWhite);
    }
    labelHalfWidth = PanelHundredths(1, labelX, top - 7, data_->unk4f0) * 3;
    DottedLine(width, middle, labelHalfWidth, true, top);
}

// A dotted line from x = 4 C over the chart's width, every fourth pixel,
// leaving out the label in the middle (half width `gap` + 3).
void PanelDrawer::DottedLine(std::uint32_t width, std::uint32_t middle, std::uint32_t gap, bool shown, std::uint32_t y) {
    std::uint32_t x = 4 * Cell();
    for (std::uint32_t e = 0; e < width; ++e, ++x) {
        if ((e & 3) == 0 && (e < middle - gap - 3 || e > gap + middle + 3) && shown) {
            PutPixel(x, y, kWhite);
        }
    }
}

}  // namespace s3re::game::panel
