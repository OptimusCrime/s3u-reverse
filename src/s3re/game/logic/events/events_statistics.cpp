// CLogicThread::ProcessEvents (0x00524fa0), part: menu 2, the statistics
// and area menu (0x528a38..0x52c237).
//
// Purpose: the buttons of the area menu's four pages (world ui_2d290[0];
// drawn by CDrawer::DrawPanel, panel_statistics.cpp). The four tabs at the
// top choose the page. Pages 2..4 act on the area PrepareDrawData found
// near the camera (CLogicThread ui_088 = 1 when there is one, its number in
// ui_08c (high byte) and ui_090 (low byte), 0x521252); their commands carry
// the area number and the local player and are pushed only while
// g_gameOutcome != 0:
//   page 1  the goods counts: no buttons
//   page 2  tools and weapons: per good one priority up/down pair
//           (ChangeGoodsPriority 7) and a slider of 17 steps
//           (SetGoodsSetting 8, value = the column - 20); a button at the top
//           right sends command 9 {1, player, area} (meaning unknown)
//   page 3  conditions: nine category buttons fill the list of up to six
//           condition kinds (world ui_2d2a4[3..8], by the local player's
//           race; the category in ui_2d2a4[2]); each kind's slider sends
//           SetAreaCondition 0x11 (value = the column - 20)
//   page 4  the transport order: 30 goods on a map choose the good
//           (world ui_2d2a4[0] = GoodsPriorityPosition(area, good), its
//           place in the order); six buttons below send ChangeTransportOrder
//           0x0a for it (on, off, top, up, down, last); a button at the top
//           right sends command 9 {2, player, area}
// Every button acts on a real click and sets its tooltip on the hover event
// (the page 4 goods only act).

#include "game/logic/events/process_events.h"

#include "game/enums/commands.h"
#include "game/enums/goods.h"
#include "game/enums/races.h"
#include "game/globals.h"
#include "game/texts.h"

namespace s3re::game::events {

namespace {

constexpr std::int32_t kMenuStatistics = 2;
constexpr std::int32_t kLastPanelColumn = 39;

// The pages (world ui_2d290[0]).
constexpr std::int32_t kPageGoods = 1;
constexpr std::int32_t kPageGoodsSettings = 2;
constexpr std::int32_t kPageConditions = 3;
constexpr std::int32_t kPageTransport = 4;

// World ui_2d2a4[]: the page 4 choice and page 3's lists.
constexpr std::int32_t kTransportChoice = 0;    // 0x2d2a4 the chosen good's place in the transport order (0 = none)
constexpr std::int32_t kConditionCategory = 2;  // 0x2d2ac the page 3 category 1..9
constexpr std::int32_t kConditionKinds = 3;     // 0x2d2b0..0x2d2c4 the category's condition kinds (0 = none)
constexpr std::int32_t kConditionSlots = 6;

// CLogicThread::PushUiFeedback values of this menu (probably sounds).
constexpr std::uint32_t kFeedbackUp = 0x2f;
constexpr std::uint32_t kFeedbackDown = 0x30;
constexpr std::uint32_t kFeedbackTop = 0x31;
constexpr std::uint32_t kFeedbackLast = 0x32;
constexpr std::uint32_t kFeedbackOn = 0x37;
constexpr std::uint32_t kFeedbackOff = 0x38;
constexpr std::uint32_t kFeedbackCommand9 = 0x3c;

// The command messages (bytes of FrameLocals::command).
constexpr std::uint8_t kAreaCommandSize = 12;  // {0x21, id, 12, ?, a, b, player, area high, area low}
constexpr std::uint8_t kCommand9Size = 8;      // {0x21, 9, 8, ?, which, player, area high, area low}
constexpr std::uint32_t kSignal = 1;
// The sliders: the value is the column - 20 (0..16).
constexpr std::int32_t kSliderFirstColumn = 20;
constexpr std::int32_t kSliderLastColumn = 36;

constexpr std::uintptr_t kDebugLog = 0x00428720;  // printf-style (cdecl), a bare ret in this build

bool In(std::int32_t value, std::int32_t low, std::int32_t high) {
    return value >= low && value <= high;
}

std::uint8_t Byte(std::int32_t value) {
    return static_cast<std::uint8_t>(value);
}

std::int32_t& Ui2d2a4(Run& run, std::int32_t index) {
    return run.world()->ui_2d2a4[index];
}

// The area number of the commands and of GoodsPriorityPosition.
std::uint32_t AreaNumber(const CLogicThread* thread) {
    return (static_cast<std::uint32_t>(thread->ui_08c) << 8) + static_cast<std::uint32_t>(thread->ui_090);
}

// {0x21, id, 12, ?, a, b, player, area high, area low}, pushed (3 dwords)
// while g_gameOutcome != 0, then ClearSelection.
void PushAreaCommand(Run& run, CommandId id, std::uint8_t a, std::uint8_t b) {
    std::uint8_t* command = run.frame->command;
    command[0] = static_cast<std::uint8_t>(NetMessage::CommandRequest);
    command[1] = static_cast<std::uint8_t>(id);
    command[2] = kAreaCommandSize;
    command[4] = a;
    command[5] = b;
    command[6] = Byte(run.world()->local_player);
    command[7] = Byte(run.thread->ui_08c);
    command[8] = Byte(run.thread->ui_090);
    if (g_gameOutcome != 0) {
        PushCommand(run, 3, kSignal);
    }
    CallThread<callee::kClearSelection>(run.thread);
}

// 0x528b9e (page 2) / 0x52c19f (page 4): command 9 {0x21, 9, 8, ?, which,
// player, area high, area low} (2 dwords). ExecuteCommands runs id 9 as
// MoveUnits, which does not fit this payload: its meaning here is unknown.
void Command9Button(Run& run, std::uint32_t feedback, std::uint8_t which) {
    if (!run.Acting()) {
        run.thread->tooltip = Tooltip(0x5e);
        return;
    }
    Feedback(run, feedback);
    std::uint8_t* command = run.frame->command;
    command[0] = static_cast<std::uint8_t>(NetMessage::CommandRequest);
    command[1] = static_cast<std::uint8_t>(CommandId::MoveUnits);
    command[2] = kCommand9Size;
    command[4] = which;
    command[5] = Byte(run.world()->local_player);
    command[6] = Byte(run.thread->ui_08c);
    command[7] = Byte(run.thread->ui_090);
    if (g_gameOutcome != 0) {
        PushCommand(run, 2, kSignal);
    }
    CallThread<callee::kClearSelection>(run.thread);
}

// 0x528a45..0x528b8b: the page tabs (rows 11..18, ten columns each).
void PageTabs(Run& run) {
    struct Tab {
        std::int32_t first, last, page;
        std::uint32_t tooltip;
    };
    static constexpr Tab kTabs[] = {{0, 9, kPageGoods, 0x51},
                                    {10, 19, kPageGoodsSettings, 0x52},
                                    {20, 29, kPageConditions, 0x81},
                                    {30, kLastPanelColumn, kPageTransport, 0x82}};
    if (!In(run.row, 11, 18)) {
        return;
    }
    for (const Tab& tab : kTabs) {
        if (!In(run.column, tab.first, tab.last)) {
            continue;
        }
        if (!run.Acting()) {
            run.thread->tooltip = Tooltip(tab.tooltip);
            continue;
        }
        Feedback(run);
        CGameData* world = run.world();
        world->ui_2d290[0] = tab.page;
        if (tab.page == kPageGoodsSettings) {
            world->ui_2d290[1] = 0;
        }
        world->refresh_ui = 1;
        CallThread<callee::kClearSelection>(run.thread);
    }
}

// ---------------------------------------------------------------- page 2
// 0x528b9e..0x529d20: the tools and weapons, one row of buttons each: the
// up / down pair (columns 14..17) and the slider (columns 20..36).
constexpr std::int32_t kSettingGoods = 10;
constexpr Good kSettingGood[kSettingGoods] = {Good::Hammer, Good::Shovel,     Good::Pickaxe, Good::Axe, Good::Saw,
                                              Good::Scythe, Good::FishingRod, Good::Sword,   Good::Bow, Good::Spear};

// The first row of a good's buttons: 28 + 6 i, the three weapons after a gap.
std::int32_t SettingFirstRow(std::int32_t i) {
    constexpr std::int32_t kTools = 7;
    return i < kTools ? 28 + 6 * i : 76 + 6 * (i - kTools);
}

void GoodsSettingsPage(Run& run) {
    const std::int32_t column = run.column;
    const std::int32_t row = run.row;
    if (column >= 34 && In(row, 22, 27)) {
        Command9Button(run, kFeedbackClick, 1);
    }
    if (In(column, 14, 17)) {  // 0x528c33: priority up (rows +0..+2) / down (+3..+5)
        for (std::int32_t i = 0; i < kSettingGoods; ++i) {
            const std::int32_t first = SettingFirstRow(i);
            for (std::int32_t down = 0; down < 2; ++down) {
                if (!In(row, first + 3 * down, first + 3 * down + 2)) {
                    continue;
                }
                if (!run.Acting()) {
                    run.thread->tooltip = Tooltip(0x5f);
                    continue;
                }
                Feedback(run, down == 0 ? kFeedbackUp : kFeedbackDown);
                PushAreaCommand(run, CommandId::ChangeGoodsPriority, static_cast<std::uint8_t>(kSettingGood[i]),
                                down == 0 ? 1 : 0);
            }
        }
    }
    if (In(column, kSliderFirstColumn, kSliderLastColumn)) {  // 0x529730: the slider (rows +1..+4)
        for (std::int32_t i = 0; i < kSettingGoods; ++i) {
            const std::int32_t first = SettingFirstRow(i);
            if (!In(row, first + 1, first + 4)) {
                continue;
            }
            if (!run.Acting()) {
                run.thread->tooltip = Tooltip(0x60);
                continue;
            }
            Feedback(run);
            PushAreaCommand(run, CommandId::SetGoodsSetting, static_cast<std::uint8_t>(kSettingGood[i]),
                            Byte(column - kSliderFirstColumn));
        }
    }
}

// ---------------------------------------------------------------- page 3
// The condition kinds of a category by race (0 = an empty slot); all six
// slots are written, also where the original's paths share code.
constexpr std::int32_t kCategories = 9;
constexpr std::int32_t kConditionTable[kCategories][kRaces][kConditionSlots] = {
    // Roman             Egyptian               Asian                   Amazon
    {{1, 2, 3, 4, 0, 0}, {1, 2, 3, 4, 0, 0}, {1, 2, 3, 4, 5, 6}, {1, 2, 3, 4, 0, 0}},                 // 0x529d6c
    {{7, 8, 9, 10, 0, 0}, {7, 8, 9, 11, 0, 0}, {7, 8, 9, 12, 0, 0}, {7, 8, 9, 47, 0, 0}},             // 0x529f3b
    {{13, 14, 15, 43, 0, 0}, {13, 14, 44, 0, 0, 0}, {13, 14, 45, 0, 0, 0}, {13, 14, 46, 0, 0, 0}},    // 0x52a10b
    {{16, 17, 0, 0, 0, 0}, {18, 19, 0, 0, 0, 0}, {20, 21, 0, 0, 0, 0}, {49, 48, 0, 0, 0, 0}},         // 0x52a2c8
    {{22, 23, 24, 0, 0, 0}, {22, 23, 24, 25, 0, 0}, {22, 23, 24, 0, 0, 0}, {22, 23, 24, 0, 0, 0}},    // 0x52a4a2
    {{26, 27, 28, 0, 0, 0}, {26, 27, 28, 29, 0, 0}, {26, 27, 28, 0, 0, 0}, {26, 27, 28, 50, 0, 0}},   // 0x52a628
    {{30, 31, 32, 0, 0, 0}, {30, 31, 32, 33, 0, 0}, {30, 31, 32, 34, 0, 0}, {30, 31, 32, 34, 33, 0}}, // 0x52a7e9
    {{35, 36, 37, 0, 0, 0}, {35, 36, 37, 38, 0, 0}, {35, 36, 37, 0, 0, 0}, {35, 36, 37, 38, 0, 0}},   // 0x52a9be
    {{39, 40, 41, 0, 0, 0}, {39, 40, 41, 42, 0, 0}, {39, 40, 41, 0, 0, 0}, {39, 40, 41, 42, 0, 0}},   // 0x52ab6f
};
// The tooltips of the categories; category 4's depends on the race.
constexpr std::int32_t kCategoryWithRaceTooltip = 4;
constexpr std::uint32_t kCategoryTooltips[kCategories] = {0x83, 0x84, 0x85, 0, 0x89, 0x8a, 0x8b, 0x8c, 0x8d};
constexpr std::uint32_t kCategory4Tooltips[kRaces] = {0x86, 0x87, 0x88, 0x16f};

std::int32_t LocalRace(Run& run) {
    CGameData* world = run.world();
    return world->players[world->local_player].race;
}

bool KnownRace(std::int32_t race) {
    return race >= 0 && race < kRaces;
}

// 0x529d3f..0x52ad0a: the category buttons (columns 2..7, rows 30 + 7 i ..
// 35 + 7 i).
void ConditionCategories(Run& run) {
    if (!In(run.column, 2, 7)) {
        return;
    }
    for (std::int32_t i = 0; i < kCategories; ++i) {
        const std::int32_t category = i + 1;
        if (!In(run.row, 30 + 7 * i, 35 + 7 * i)) {
            continue;
        }
        if (!run.Acting()) {
            if (category != kCategoryWithRaceTooltip) {
                run.thread->tooltip = Tooltip(kCategoryTooltips[i]);
            } else if (const std::int32_t race = LocalRace(run); KnownRace(race)) {  // 0x52a42a
                run.thread->tooltip = Tooltip(kCategory4Tooltips[race]);
            }
            continue;
        }
        Ui2d2a4(run, kConditionCategory) = category;
        Feedback(run);
        if (const std::int32_t race = LocalRace(run); KnownRace(race)) {
            for (std::int32_t slot = 0; slot < kConditionSlots; ++slot) {
                Ui2d2a4(run, kConditionKinds + slot) = kConditionTable[i][race][slot];
            }
        }
        RefreshUi(run);
        CallThread<callee::kClearSelection>(run.thread);
    }
}

// 0x52ad0a..0x52b17a: the sliders of the six kinds (columns 20..36, rows
// 35 + 11 i .. 38 + 11 i); an empty slot does not act.
void ConditionSliders(Run& run) {
    const std::int32_t column = run.column;
    if (!In(column, kSliderFirstColumn, kSliderLastColumn)) {
        return;
    }
    for (std::int32_t slot = 0; slot < kConditionSlots; ++slot) {
        if (!In(run.row, 35 + 11 * slot, 38 + 11 * slot)) {
            continue;
        }
        if (!run.Acting()) {
            run.thread->tooltip = Tooltip(0x8e);
            continue;
        }
        if (Ui2d2a4(run, kConditionKinds + slot) == 0) {
            continue;
        }
        Feedback(run);
        PushAreaCommand(run, CommandId::SetAreaCondition, Byte(Ui2d2a4(run, kConditionKinds + slot)),
                        Byte(column - kSliderFirstColumn));
    }
}

// ---------------------------------------------------------------- page 4
// The goods map (0x52b199..0x52bd20): goods 1..30, each a 6 x 6 cell button
// at (column, row), in rows of six that run left to right and back.
struct MapPlace {
    std::int32_t column, row;
};
constexpr std::int32_t kMapGoods = 30;
constexpr std::int32_t kMapButtonSize = 6;
constexpr MapPlace kGoodsMap[kMapGoods] = {
    {2, 28},  {8, 29},  {14, 30}, {20, 31}, {26, 32}, {32, 33},   // goods 1..6
    {32, 39}, {26, 40}, {20, 41}, {14, 42}, {8, 43},  {2, 44},    // 7..12
    {2, 50},  {8, 51},  {14, 52}, {20, 53}, {26, 54}, {32, 55},   // 13..18
    {32, 61}, {26, 62}, {20, 63}, {14, 64}, {8, 65},  {2, 66},    // 19..24
    {2, 72},  {8, 73},  {14, 74}, {20, 75}, {26, 76}, {32, 77}};  // 25..30

void GoodsMap(Run& run) {
    CLogicThread* thread = run.thread;
    for (std::int32_t i = 0; i < kMapGoods; ++i) {
        const MapPlace& place = kGoodsMap[i];
        if (!In(run.column, place.column, place.column + kMapButtonSize - 1) ||
            !In(run.row, place.row, place.row + kMapButtonSize - 1) || !run.Acting()) {
            continue;
        }
        Feedback(run);
        const auto good = static_cast<std::uint32_t>(i + 1);
        Ui2d2a4(run, kTransportChoice) =
            static_cast<std::int32_t>(CallThread<callee::kGoodsPriorityPosition>(thread, AreaNumber(thread), good));
        if (i == 0) {  // 0x52b1ee: only the first good logs
            using LogFn = void(__cdecl*)(const char*, ...);
            reinterpret_cast<LogFn>(kDebugLog)(texts::kGoodsPriorityLog.c_str(),
                                               static_cast<std::uint32_t>(Ui2d2a4(run, kTransportChoice)),
                                               static_cast<std::uint32_t>(thread->ui_08c),
                                               static_cast<std::uint32_t>(thread->ui_090));
        }
        RefreshUi(run);
        CallThread<callee::kClearSelection>(thread);
    }
}

// 0x52bd20..0x52c19f: the transport order buttons below the map (rows
// 86..91): ChangeTransportOrder operation 1..6 for the chosen good (only
// when there is one).
void TransportButtons(Run& run) {
    struct Button {
        std::int32_t first, last;
        std::uint32_t feedback;
        std::uint32_t tooltip;
    };
    static constexpr Button kButtons[] = {{1, 6, kFeedbackOn, 0x61},    {7, 12, kFeedbackOff, 0x62},
                                          {15, 20, kFeedbackTop, 0x63}, {21, 26, kFeedbackUp, 0x64},
                                          {27, 32, kFeedbackDown, 0x65}, {33, 38, kFeedbackLast, 0x66}};
    if (!In(run.row, 86, 91)) {
        return;
    }
    std::uint8_t operation = 1;
    for (const Button& button : kButtons) {
        const std::uint8_t thisOperation = operation++;
        if (!In(run.column, button.first, button.last)) {
            continue;
        }
        if (!run.Acting()) {
            run.thread->tooltip = Tooltip(button.tooltip);
            continue;
        }
        if (Ui2d2a4(run, kTransportChoice) == 0) {
            continue;
        }
        Feedback(run, button.feedback);
        PushAreaCommand(run, CommandId::ChangeTransportOrder, Byte(Ui2d2a4(run, kTransportChoice)), thisOperation);
    }
}

// The page is open and there is an area to act on.
bool PageWithArea(Run& run, std::int32_t page) {
    return run.world()->ui_2d290[0] == page && run.thread->ui_088 != 0;
}

}  // namespace

void StatisticsClick(Run& run) {
    if (run.world()->ui_selection_mode != kMenuStatistics) {
        return;
    }
    PageTabs(run);
    if (PageWithArea(run, kPageGoodsSettings)) {  // 0x528b8b
        GoodsSettingsPage(run);
    }
    if (PageWithArea(run, kPageConditions)) {  // 0x529d20
        ConditionCategories(run);
        ConditionSliders(run);
    }
    if (PageWithArea(run, kPageTransport)) {  // 0x52b17a
        GoodsMap(run);
        TransportButtons(run);
        if (In(run.column, 32, 37) && In(run.row, 22, 27)) {  // 0x52c19f
            Command9Button(run, kFeedbackCommand9, 2);
        }
    }
}

}  // namespace s3re::game::events
