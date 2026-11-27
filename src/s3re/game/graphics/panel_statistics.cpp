// CDrawer::DrawPanel (0x004b4d00, panel_drawer.cpp), menu 2: the statistics
// pages (0x4b5449..0x4b7c27), page data->unk06c 1..4, each with its tab
// pressed:
//   page 1  the 6 x 6 goods grid with each good's count (dimmed when the
//           flag +0x134 + 4 slot is 0)
//   page 2  ten counts with frames (+0xb0.., +0xcc..) and ten statistics
//           bars (+0xd8..+0xfc), race pictures
//   page 3  six shares (+0xb4..+0xc8) as bars and percentages, the six
//           percentages rounded so that they add up to 100
//   page 4  30 goods on a map of their positions, each with a tick sprite
//           (+0x140 + 4 i) and a marker on the chosen one (+0x80)
// Pages 2..4 only when +0xa4 is not 0.

#include "game/graphics/panel_drawer.h"

#include "game/tables.h"

#include <cstdint>

namespace s3re::game::panel {

namespace {

constexpr std::int32_t kMenuStatistics = 2;
constexpr std::uint32_t kTicks = 0x40006;  // raw-ok: animation (group 4), image 0 on, 7 off
constexpr std::int32_t kGridSize = 6;
constexpr std::uint32_t kPercentScale = 10000;

}  // namespace

void PanelDrawer::DrawStatistics() {
    if (data_->unk060 != kMenuStatistics) {
        return;
    }
    // The page tabs (0x4b5453..0x4b56b2).
    const std::int32_t page = data_->unk06c;
    Gui(page == 1 ? Img(kPanelGui, 270) : Img(kPanelGui, 258), X(0), Y(12));
    Gui(page == 2 ? Img(kPanelGui, 273) : Img(kPanelGui, 261), X(10), Y(12));
    Gui(page == 3 ? Img(kPanelGui, 276) : Img(kPanelGui, 264), X(20), Y(12));
    Gui(page == 4 ? Img(kPanelGui, 279) : Img(kPanelGui, 267), X(30), Y(12));

    // Page 1 (0x4b56b8..0x4b58e1): the goods grid with the counts
    // (+0xa4 + 4 slot) centred below each picture.
    if (data_->unk06c == 1) {
        ClearPanelText();
        PanelText(25, 1, Txt(254));
        for (std::int32_t row = 0; row < kGridSize; ++row) {
            for (std::int32_t column = 0; column < kGridSize; ++column) {
                const std::int32_t slot = row * kGridSize + column;
                const std::uint32_t image = g_goodsGridImages[slot];
                if (image == kNoImage) {
                    continue;
                }
                const std::uint32_t brightness = Value(0x134 + 4 * slot) != 0 ? kFullBrightness : kDimmed;
                const auto r = static_cast<std::uint32_t>(row);
                const auto k = static_cast<std::uint32_t>(column);
                Gui(image, X(6 * k + 2), Y(9 * r + 32), brightness);
                if (brightness != kDimmed) {
                    const std::uint32_t middle = Y(9 * r + 39);
                    textRect_.top = static_cast<std::int32_t>(middle - 2 * Cell());
                    textRect_.bottom = static_cast<std::int32_t>(middle + 2 * Cell());
                    textRect_.left = static_cast<std::int32_t>(X(6 * k + 5) - 6 * Cell());
                    textRect_.right = static_cast<std::int32_t>(X(6 * k + 5) + 6 * Cell());
                    NumberInRect(textRect_, static_cast<std::uint32_t>(Value(0xa4 + 4 * slot)));
                }
            }
        }
    }

    // Page 2 (0x4b58e7..0x4b66de).
    if (data_->unk06c == 2 && data_->unk0a4 != 0) {
        static constexpr std::uint32_t kLabels[] = {126, 138, 129, 153, 177, 165, 141};
        for (std::uint32_t i = 0; i < 7; ++i) {
            Gui(Img(kPanelGui, kLabels[i]), X(2), Y(28 + 6 * i));
        }
        // Three race pictures (other races: none).
        static constexpr std::uint32_t kRacePictures[4][3] = {
            {111, 114, 117}, {111, 117, 114}, {117, 123, 120}, {120, 123, 126}};
        const std::int32_t race = Race();
        if (race >= 0 && race <= 3) {
            for (std::uint32_t i = 0; i < 3; ++i) {
                Gui(Img(RaceGui(race), kRacePictures[race][i]), X(2), Y(76 + 6 * i));
            }
        }
        Gui(Img(kPanelGui, 315), X(0), Y(22));
        Gui(Img(kPanelGui, 312), X(34), Y(22));
        for (std::uint32_t i = 0; i < 7; ++i) {
            Gui(Img(kPanelGui, 231), X(14), Y(6 * i + 28));
            Gui(Img(kPanelGui, 336), X(20), Y(6 * i + 29));
            PanelNumber(11, 31 + 6 * i, static_cast<std::uint32_t>(Value(0xb0 + 4 * i)));
        }
        for (std::uint32_t i = 0; i < 3; ++i) {
            Gui(Img(kPanelGui, 231), X(14), Y(6 * i + 76));
            Gui(Img(kPanelGui, 336), X(20), Y(6 * i + 77));
            PanelNumber(11, 79 + 6 * i, static_cast<std::uint32_t>(Value(0xcc + 4 * i)));
        }
        static constexpr std::uint32_t kBarRows[10] = {30, 36, 42, 48, 54, 60, 66, 78, 84, 90};
        for (std::uint32_t i = 0; i < 10; ++i) {
            DrawStatisticsBar(21, kBarRows[i], static_cast<std::uint32_t>(Value(0xd8 + 4 * i)));
        }
        PanelText(25, 1, Txt(235));
        PanelText(73, 1, Txt(236));
    }

    // Page 3 (0x4b66e4..0x4b75a3).
    if (data_->unk06c == 3 && data_->unk0a4 != 0) {
        PanelText(25, 1, Txt(249));
        Gui(g_goodImages[5], X(2), Y(30));
        Gui(g_goodImages[11], X(2), Y(37));
        Gui(g_goodImages[1], X(2), Y(44));
        // A race's own good (0x4b6886..0x4b6abc).
        static constexpr std::uint32_t kRaceGoods[4] = {2, 3, 27, 35};
        for (std::int32_t race = 0; race < 4; ++race) {
            if (Race() == race) {
                Gui(g_goodImages[kRaceGoods[race]], X(2), Y(51));
            }
        }
        Gui(g_goodImages[9], X(2), Y(58));
        Gui(g_goodImages[26], X(2), Y(65));
        Gui(g_goodImages[4], X(2), Y(72));
        Gui(g_goodImages[24], X(2), Y(79));
        Gui(g_goodImages[8], X(2), Y(86));
        if (Value(0xb0) != 0) {
            Gui(Img(kPanelGui, 339), X(2), Y(static_cast<std::uint32_t>(Value(0xb0)) * 7 + 23));
        }
        DrawShares();
    }

    // Page 4 (0x4b75a9..0x4b7c21).
    if (data_->unk06c == 4 && data_->unk0a4 != 0) {
        DrawGoodsMap();
    }
}

// Page 3's shares (0x4b6e04..0x4b75a3): the six values +0xb4..+0xc8 (-1 =
// none, counted as 0) as percentages in hundredths, cut to whole percent;
// what is missing to 100 goes to the first value whose remainder is larger
// than the remainders of all values after it (the last one when none is).
// The original adds the missing points one by one in a loop; the remainders
// do not change, so they all go to the same value. Then each value that is
// not -1: its label, a frame, its bar and its percentage.
void PanelDrawer::DrawShares() {
    constexpr int kShares = 6;
    std::uint32_t shares[kShares];
    for (int i = 0; i < kShares; ++i) {
        const std::int32_t value = Value(0xb4 + 4 * i);
        shares[i] = value == -1 ? 0 : static_cast<std::uint32_t>(value);
    }
    std::uint32_t total = 0;
    for (std::uint32_t share : shares) {
        total += share;
    }
    std::uint32_t percent[kShares];
    for (int i = 0; i < kShares; ++i) {
        percent[i] = shares[i];
    }
    if (total != 0) {
        std::uint32_t remainder[kShares];
        for (int i = 0; i < kShares; ++i) {
            const std::uint32_t hundredths = shares[i] * kPercentScale / total;
            percent[i] = hundredths / 100;
            remainder[i] = hundredths % 100;
        }
        std::uint32_t missing = 100;
        for (std::uint32_t p : percent) {
            missing -= p;
        }
        if (missing != 0) {
            int winner = kShares - 1;
            for (int i = 0; i < kShares - 1; ++i) {
                bool largest = true;
                for (int j = i + 1; j < kShares; ++j) {
                    largest = largest && remainder[i] > remainder[j];
                }
                if (largest) {
                    winner = i;
                    break;
                }
            }
            percent[winner] += missing;  // the loop's count of increments, wrapping alike
        }
    }
    for (std::uint32_t i = 0; i < kShares; ++i) {
        if (Value(0xb4 + 4 * i) == -1) {
            continue;
        }
        const std::uint32_t row = 33 + 11 * i;
        PanelTextAtFont1(static_cast<std::int32_t>(row), 10, 1, static_cast<std::uint32_t>(Value(0xcc + 4 * i)));
        Gui(Img(kPanelGui, 336), X(20), Y(row + 2));
        DrawStatisticsBar(21, row + 3, static_cast<std::uint32_t>(Value(0xb4 + 4 * i)));
        PanelPercent(14, row + 4, percent[i]);
    }
}

// Page 4 (0x4b75b3..0x4b7c21): goods 1..30 at their map positions: the
// picture of the slot (0..35) whose value +0xb0 + 4 slot is the good, its
// tick sprite (on when +0x140 + 4 slot is 1) and the marker on the slot
// +0x80. Then the frame and the six pictures of the bottom row.
void PanelDrawer::DrawGoodsMap() {
    constexpr std::int32_t kGoods = 30;
    constexpr std::int32_t kSlots = 36;
    for (std::int32_t good = 1; good <= kGoods; ++good) {
        std::int32_t slot = 0;
        while (slot < kSlots && Value(0xb0 + 4 * static_cast<std::uint32_t>(slot)) != good) {
            ++slot;
        }
        if (slot == kSlots) {
            continue;
        }
        const std::uint32_t image = g_goodImages[slot];
        if (image == kNoImage) {
            continue;
        }
        const auto& place = g_goodsMapPositions[good - 1];
        const auto column = static_cast<std::uint32_t>(place[0]);
        const auto row = static_cast<std::uint32_t>(place[1]);
        Gui(image, X(column), Y(row));
        SelectSpriteInline(kTicks, 7 - 7 * Value(0x140 + 4 * static_cast<std::uint32_t>(slot)));
        DrawSprite(X(column + 2), Y(row + 1), PanelArea(), kFullBrightness);
        if (data_->unk080 == slot) {
            Gui(Img(kPanelGui, 339), X(column), Y(row));
        }
    }
    Gui(Img(kPanelGui, 315), X(0), Y(22));
    Gui(Img(kPanelGui, 312), X(34), Y(22));
    Gui(Img(kPanelGui, 213), X(1), Y(86));
    Gui(Img(kPanelGui, 216), X(7), Y(86));
    Gui(Img(kPanelGui, 219), X(15), Y(86));
    Gui(Img(kPanelGui, 225), X(21), Y(86));
    Gui(Img(kPanelGui, 228), X(27), Y(86));
    Gui(Img(kPanelGui, 222), X(33), Y(86));
    PanelText(25, 1, Txt(237));
}

}  // namespace s3re::game::panel
