// CDrawer::DrawPanel (0x004b4d00, panel_drawer.cpp), menu 5: the soldiers
// (0x4c013e..0x4c1f9d). Unless data->unk074 is 1: three groups of three
// ranks (+0xa4..+0xc4) with their totals, four more kinds (+0xc8..+0xd4)
// and a ninth (+0xd8) with up to six rank sprites (+0xdc..); a kind with
// nothing is drawn at half brightness. Then a text by the page
// data->unk074; page 1 (when +0xc8 is set and data->unk078 is 0) shows the
// race's eight pictures with eight value bars (+0xc4 down to +0xa8, the
// limit +0xa4).

#include "game/graphics/panel_drawer.h"

#include "game/globals.h"

#include <cstdint>

namespace s3re::game::panel {

namespace {

constexpr std::int32_t kMenuSoldiers = 5;
constexpr std::uint32_t kRankWithout = 64;  // 0x40: the brightness of a missing rank's sprite
constexpr std::int32_t kDimmingMission = 67;  // g_campaignMission that greys out the Roman pictures
constexpr std::uint32_t kMissionUnlocked = 0x1000;  // +0x9c bit: not greyed out in that mission

// One kind of soldier: its picture (g_raceSettlerImages kind) and number.
struct Kind {
    std::uint32_t picture;
    std::uint32_t column, row;   // the picture
    std::uint32_t numberColumn;  // the number (row: picture row + 14)
    std::uint32_t offset;        // its count
};

// Page 1's race pictures, top to bottom.
constexpr std::uint16_t kRacePictures[4][8] = {
    {267, 264, 261, 258, 255, 252, 249, 246},
    {261, 258, 255, 252, 249, 246, 243, 240},
    {276, 273, 270, 267, 264, 261, 258, 255},
    {270, 255, 264, 261, 258, 267, 252, 249},
};

}  // namespace

void PanelDrawer::DrawSoldiers() {
    if (data_->unk060 != kMenuSoldiers) {
        return;
    }
    if (data_->unk074 != 1) {
        DrawSoldierCounts();
    }
    switch (data_->unk074) {
    case 0: PanelTextArg(90, 1, Txt(283), static_cast<std::uint32_t>(data_->unk0a4)); break;
    case 2:
        PanelText(25, 3, Txt(269));
        PanelText(90, 1, Txt(270));
        break;
    case 3: break;
    case 4:
        if (Race() != 3) {
            PanelText(90, 1, Txt(280));
        }
        break;
    case 5: PanelText(90, 1, Txt(281)); break;
    case 6: PanelText(90, 1, Txt(282)); break;
    case 7: PanelText(90, 1, Txt(369)); break;
    case 1:
        if (Value(0xc8) == 0) {
            break;
        }
        if (data_->unk078 != 0) {
            PanelText(40, 3, Txt(266));
            PanelText(83, 3, Txt(268));
            break;
        }
        PanelText(25, 1, Txt(267));
        {
            const std::int32_t race = Race();
            if (race >= 0 && race <= 3) {
                std::uint32_t brightness = kFullBrightness;
                if (race == 0 && g_campaignMission == kDimmingMission &&
                    (static_cast<std::uint32_t>(Value(0x9c)) & kMissionUnlocked) == 0) {
                    brightness = kDimmed;
                }
                for (std::uint32_t i = 0; i < 8; ++i) {
                    Gui(Img(RaceGui(race), kRacePictures[race][i]), X(4), Y(28 + 8 * i), brightness);
                }
            }
        }
        // The bars (0x4c1f2b..0x4c1f8f).
        for (std::uint32_t i = 0; i < 8; ++i) {
            DrawValueBar(17 * Cell(), (30 + 8 * i) * Cell(), 38 * Cell(), (34 + 8 * i) * Cell(),
                         static_cast<std::uint32_t>(Value(0xc4 - 4 * i)), static_cast<std::uint32_t>(data_->unk0a4));
        }
        break;
    default: break;
    }
}

// 0x4c0148..0x4c1c9d.
void PanelDrawer::DrawSoldierCounts() {
    if (Value(0xb8) + Value(0xb0) + data_->unk0a4 + Value(0xbc) + Value(0xc0) + Value(0xac) + Value(0xa8) +
            Value(0xb4) + Value(0xc4) !=
        0) {
        PanelTextArg(25, 1, Txt(246), static_cast<std::uint32_t>(Value(0xf4)));
    }
    // Three groups of three ranks: the picture with the group's total, and
    // the three counts when the upper ranks have any.
    struct Group {
        std::uint32_t picture, column, offset;
    };
    static constexpr Group kGroups[3] = {{4, 1, 0xa4}, {5, 14, 0xb0}, {6, 27, 0xbc}};
    for (const Group& group : kGroups) {
        const std::int32_t low = Value(group.offset);
        const std::int32_t middle = Value(group.offset + 4);
        const std::int32_t high = Value(group.offset + 8);
        if (low + middle + high == 0) {
            Gui(RaceSettlerImage(group.picture), X(group.column), Y(30), kHalfDimmed);
            continue;
        }
        Gui(RaceSettlerImage(group.picture), X(group.column), Y(30));
        PanelNumber(group.column + 4, 44, static_cast<std::uint32_t>(low + high + middle));
        if (middle != 0 || high != 0) {
            PanelNumber(group.column + 9, 32, static_cast<std::uint32_t>(high));
            PanelNumber(group.column + 9, 36, static_cast<std::uint32_t>(middle));
            PanelNumber(group.column + 9, 40, static_cast<std::uint32_t>(low));
        }
    }
    // Four kinds with one count each.
    static constexpr Kind kKinds[4] = {
        {8, 1, 48, 5, 0xc8}, {10, 11, 48, 15, 0xcc}, {9, 21, 48, 25, 0xd0}, {7, 31, 48, 35, 0xd4}};
    for (const Kind& kind : kKinds) {
        const std::int32_t count = Value(kind.offset);
        if (count == 0) {
            Gui(RaceSettlerImage(kind.picture), X(kind.column), Y(kind.row), kHalfDimmed);
        } else {
            Gui(RaceSettlerImage(kind.picture), X(kind.column), Y(kind.row));
            PanelNumber(kind.numberColumn, kind.row + 14, static_cast<std::uint32_t>(count));
        }
    }
    // The ninth kind and its ranks (0x4c18cf..0x4c1c9d): two rows of three
    // sprites, one per unit (at most six); a unit's rank r > 0 shows image
    // r - 1 of the race's rank animation, rank 0 image 5 dimmed.
    const auto units = static_cast<std::uint32_t>(Value(0xd8));
    if (units == 0) {
        Gui(RaceSettlerImage(11), X(2), Y(66), kHalfDimmed);
        return;
    }
    Gui(RaceSettlerImage(11), X(2), Y(66));
    PanelNumber(8, 80, units);
    if (Race() == 3) {
        return;
    }
    for (std::uint32_t row = 0; row < 2; ++row) {
        for (std::uint32_t column = 0; column < 3; ++column) {
            const std::uint32_t unit = row * 3 + column;
            if (unit >= static_cast<std::uint32_t>(Value(0xd8)) || unit >= 6) {
                continue;
            }
            const std::int32_t rank = Value(0xdc + 4 * unit);
            const std::uint32_t* animations = g_raceRankAnimations.get();
            SelectSpriteInline(animations[Race()], rank == 0 ? 5 : rank - 1);
            DrawSprite(X(6 * column + 18), Y(8 * row + 70), &screen_, rank == 0 ? kRankWithout : kFullBrightness);
        }
    }
}

}  // namespace s3re::game::panel
