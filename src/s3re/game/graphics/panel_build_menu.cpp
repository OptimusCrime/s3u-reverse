// CDrawer::DrawPanel (0x004b4d00, panel_drawer.cpp), menu 1: the build
// menu (0x4badf4..0x4c013e). Four pages (data->unk064 1..4, the selection
// groups of QueueLandscapeObject) of building pictures in two columns, one
// list per race (data->unk094, the race's GUI file 14 / 24 / 34 / 44), a
// marker on the chosen item (data->unk068) and, when g_showBuildCosts is
// set, the costs of the twelve buildings (+0xa4 + 4 i, with "+%u" of
// +0xd4 + 4 i next to them).
//
// The original has one block per race and page (0x4bae99..0x4bfef9); here
// they are the table kBuildPages, drawn in the same order (left, right,
// row by row from the top).

#include "game/graphics/panel_drawer.h"

#include "game/globals.h"
#include "game/texts.h"

#include <cstdint>

namespace s3re::game::panel {

namespace {

constexpr std::int32_t kMenuBuild = 1;
constexpr std::int32_t kPages = 4;
constexpr std::int32_t kRaces = 4;
constexpr std::uint32_t kRowCells = 12;     // the rows of pictures are 12 cells apart, from row 22
constexpr std::uint32_t kRightColumn = 20;  // the right column's x (cells)
constexpr std::int32_t kDimmingMission = 55;  // g_campaignMission that greys out one picture

struct BuildPage {
    std::uint32_t count;
    std::uint16_t images[12];
};

// [race][page - 1]: the image indices in the race's GUI file.
constexpr BuildPage kBuildPages[kRaces][kPages] = {
    {  // race 0
        {11, {0, 6, 42, 3, 45, 24, 48, 21, 51, 30, 81}},
        {9, {18, 69, 66, 15, 12, 39, 36, 87, 90}},
        {9, {54, 60, 63, 9, 27, 96, 57, 108, 93}},
        {8, {72, 75, 78, 33, 99, 84, 102, 105}},
    },
    {  // race 1
        {11, {0, 6, 42, 3, 45, 24, 48, 21, 51, 30, 96}},
        {9, {18, 69, 66, 15, 12, 39, 36, 81, 90}},
        {9, {54, 60, 63, 9, 27, 99, 57, 84, 87}},
        {8, {72, 75, 78, 33, 93, 108, 105, 102}},
    },
    {  // race 2
        {12, {0, 6, 45, 3, 48, 27, 108, 24, 54, 33, 51, 105}},
        {10, {21, 72, 69, 18, 12, 42, 39, 90, 96, 102}},
        {9, {57, 63, 66, 9, 30, 111, 60, 114, 93}},
        {8, {75, 78, 81, 36, 99, 15, 84, 87}},
    },
    {  // race 3
        {12, {0, 6, 48, 3, 51, 27, 54, 24, 57, 33, 45, 15}},
        {10, {21, 75, 72, 18, 12, 42, 39, 93, 117, 96}},
        {9, {60, 66, 69, 9, 30, 102, 63, 114, 99}},
        {9, {78, 81, 84, 36, 105, 90, 108, 111, 87}},
    },
};
// Race 3's page 1 picture 3 is greyed out in that campaign mission
// (0x4bfa4c..0x4bfab0).
constexpr std::int32_t kDimmedRace = 3;
constexpr std::int32_t kDimmedPage = 1;
constexpr std::uint32_t kDimmedItem = 3;

constexpr std::int32_t kCostRows = 12;
constexpr std::uint32_t kCostFirstRow = 19;

}  // namespace

void PanelDrawer::DrawBuildMenu() {
    if (data_->unk060 != kMenuBuild) {
        return;
    }
    // The page tabs (0x4bae05..0x4bb02a).
    const std::int32_t page = data_->unk064;
    Gui(page == 1 ? Img(kPanelGui, 81) : Img(kPanelGui, 69), X(0), Y(12));
    Gui(page == 2 ? Img(kPanelGui, 84) : Img(kPanelGui, 72), X(10), Y(12));
    Gui(page == 3 ? Img(kPanelGui, 87) : Img(kPanelGui, 75), X(20), Y(12));
    Gui(page == 4 ? Img(kPanelGui, 90) : Img(kPanelGui, 78), X(30), Y(12));

    // The marker on the chosen item (0x4bb030..0x4bb0e3): items 1, 2 in
    // the first row, 3, 4 in the second, ...
    if (data_->unk064 != 0 && data_->unk068 != 0) {
        const auto item = static_cast<std::uint32_t>(data_->unk068) - 1;
        Gui(Img(kPanelGui, 123), X(20 * (item & 1)), Y((item >> 1) * 12 + 22));
    }

    // The pictures (0x4bb0e9..0x4bfef9).
    for (std::int32_t r = 0; r < kRaces; ++r) {
        for (std::int32_t p = 1; p <= kPages; ++p) {
            if (data_->unk064 != p || Race() != r) {
                continue;
            }
            const BuildPage& list = kBuildPages[r][p - 1];
            for (std::uint32_t i = 0; i < list.count; ++i) {
                std::uint32_t brightness = kFullBrightness;
                if (r == kDimmedRace && p == kDimmedPage && i == kDimmedItem && g_campaignMission == kDimmingMission) {
                    brightness = kDimmed;
                }
                Gui(Img(RaceGui(r), list.images[i]), X(i % 2 == 0 ? 0 : kRightColumn), Y(22 + (i / 2) * kRowCells),
                    brightness);
            }
        }
    }

    // The costs (0x4bfeff..0x4c0138): the even entries left (number at
    // column 16, row + 12; the extra at cells 12..20, row + 6), the odd
    // ones right (column 36, row + 6; cells 32..40, row).
    const std::int32_t costPage = data_->unk064;
    if (g_showBuildCosts.get() == 0 || costPage < 1 || costPage > 4) {
        return;
    }
    std::uint32_t row = kCostFirstRow;
    for (std::int32_t i = 0; i < kCostRows; ++i, row += 6) {
        const auto index = static_cast<std::uint32_t>(i);
        const auto cost = static_cast<std::uint32_t>(Value(0xa4 + 4 * index));
        const bool left = (i & 1) == 0;
        if (cost != 0 || Value(0xd4 + 4 * index) != 0) {
            PanelNumber(left ? 16 : 36, left ? row + 12 : row + 6, cost);
        }
        const auto extra = static_cast<std::uint32_t>(Value(0xd4 + 4 * index));
        if (extra == 0) {
            continue;
        }
        Rect rect{};
        rect.top = static_cast<std::int32_t>(Y(left ? row + 6 : row));
        rect.bottom = static_cast<std::int32_t>(static_cast<std::uint32_t>(rect.top) + 4 * Cell());
        rect.left = static_cast<std::int32_t>(X(left ? 12 : 32));
        rect.right = static_cast<std::int32_t>(X(left ? 20 : 40) - 1);
        ClearTextRect(static_cast<std::uint32_t>(rect.left), static_cast<std::uint32_t>(rect.top),
                      static_cast<std::uint32_t>(rect.right - rect.left), static_cast<std::uint32_t>(rect.bottom - rect.top));
        TextInRect(rect, texts::kFormatPlusUnsigned, extra);
    }
}

}  // namespace s3re::game::panel
