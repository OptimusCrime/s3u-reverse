// Queueing a building of a draw cell (Volker\DrawBuilding.cpp, object file
// 0x4a9960..0x4b26c0; called twice by the frame drawer 0x4b26c0).
//
// CDrawer::QueueBuilding (0x004a9960; was Render_sub_4a9960): (DrawData*
//   data, column, row, x, y) thiscall, ret 0x14. One function (one stack
//   frame of 0x7c8 bytes; the 52 `ret 14h` are the epilogues of its switch
//   cases; jump tables at 0x4b1800 (construction slots, types 2..0x38) and
//   0x4b18dc (types 1..0x38)). The cell is data->cells[column][row], read
//   as a BuildingCell below; sprites go to (x, y - lift) with param =
//   4 * (direction & 0x3f), queue row 2 * row unless said otherwise.
//   Animation ids come from g_raceAnimations[slot][race] (race = the cell's
//   +0xc); "shadow" = CGraphicsData::ShadowAnimationPointer 0x5fabb0 and
//   its image by GfxDword::ListImageChecked 0x5fcce0, "sprite" =
//   CGraphicsData::AnimationPointer 0x5faaf0 and a frame (inlined, or
//   GfxDword::ListImageChecked_4b19c0: both give frame 0 out of range).
//   1. The menu building (cell word +0xa == g_selectedBuilding): sprite
//      0x4000b frame 0 at row 0xf8, param 0xff. With param > 72, eight
//      CDrawer::AddHitArea 0x4d8240 (100, x + dx, y + dy, building, 0).
//   2. Under construction (progress +0x10 >= 2): type 1 (6 parts) and 0x22
//      (2 parts) by their own layout; others: the construction slot of the
//      type (kConstructionSlots), the shadow/sprite of frame 1 cut to
//      progress * 0x20c (CDrawer::QueueShadowPart 0x4d7d50 /
//      QueueSpritePart 0x4d7f30) below progress 125, else frame 1 whole and
//      frame 0 cut to (progress - 125) * 0x20c. A type outside 2..0x38 logs
//      and asserts (Win32::ThrowAssertmentFailed 0x6119b0). Progress 1
//      draws nothing.
//   3. Finished (progress 0): per type (kBuildingSlots) the shadow and the
//      sprite frame 0, then the type's extras: the worker animation with
//      the owner's colour when flag +0x12 is set (QueueWorker), stock piles
//      (+0x13), smoke (+0x1c), sequence animations (+0x11), an extra
//      sprite (frame +0xe).

#include "game/global_access.h"
#include "game/globals.h"
#include "game/graphics/draw_data_layout.h"
#include "game/graphics/drawer_layout.h"
#include "game/tables.h"
#include "game/texts.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

#pragma pack(push, 1)
// A draw cell as the building drawer reads it (the same 48 bytes as
// DrawCell).
struct BuildingCell {
    std::uint8_t lift;           // +0x00
    std::uint8_t unk01[3];
    std::uint8_t owner;          // +0x04 player (the colour layers' palette, Render::Entry204)
    std::uint8_t direction;      // +0x05 low 6 bits: param / 4
    std::uint8_t unk06[4];
    std::uint16_t building;      // +0x0a the building's number (compared with g_selectedBuilding)
    std::uint8_t race;           // +0x0c 0..3, the column of g_raceAnimations
    std::uint8_t type;           // +0x0d the building type 1..0x38
    std::uint8_t extraFrame;     // +0x0e the frame of the extra sprite (types 0x2a, 0x2c, 0x2e, 0x36)
    std::uint8_t unk0f;
    std::uint8_t progress;       // +0x10 construction: 0 finished, 1 nothing drawn, 2.. the visible part
    std::uint8_t sequences;      // +0x11 number of sequence animations (types 0x17, 0x31)
    std::uint8_t working[6];     // +0x12 worker animations on (types 0x10 / 0x12 read 3 / 6 flags);
                                 //       +0x13 is also the stock of types 0x1f, 0x2f, 0x30
    std::uint8_t unk18[4];
    std::uint8_t smoke;          // +0x1c 255 - smoke amount
    std::uint8_t flags;          // +0x1d bit 2: type 0x11 hides its sprite
    std::uint8_t unk1e[0x30 - 0x1e];
};
#pragma pack(pop)
static_assert(sizeof(BuildingCell) == 0x30);
static_assert(offsetof(BuildingCell, building) == 0x0a);
static_assert(offsetof(BuildingCell, progress) == 0x10);
static_assert(offsetof(BuildingCell, smoke) == 0x1c);

constexpr std::uintptr_t kGfxDwordConstruct = 0x005fc540;
constexpr std::uintptr_t kEntry204 = 0x004c7ab0;
constexpr std::uintptr_t kShadowAnimationPointer = 0x005fabb0;
constexpr std::uintptr_t kColorIndexedAnimationPointer = 0x005fab50;
constexpr std::uintptr_t kAnimationPointer = 0x005faaf0;
constexpr std::uintptr_t kPalettePointer = 0x005fac10;
constexpr std::uintptr_t kSequenceFrameCount = 0x005fad20;
constexpr std::uintptr_t kGetSequenceFrame = 0x005fad50;
constexpr std::uintptr_t kBodyImage = 0x005fd9a0;
constexpr std::uintptr_t kColorIndexedImage = 0x005fd9b0;
constexpr std::uintptr_t kListImageChecked = 0x005fcce0;
constexpr std::uintptr_t kListImageChecked2 = 0x004b19c0;
constexpr std::uintptr_t kListImage = 0x005fc5a0;
constexpr std::uintptr_t kQueueShadow = 0x004d7c90;
constexpr std::uintptr_t kQueueShadowPart = 0x004d7d50;
constexpr std::uintptr_t kQueueSprite = 0x004d7e10;
constexpr std::uintptr_t kQueueSpritePart = 0x004d7f30;
constexpr std::uintptr_t kQueueColourLayer = 0x004d8170;
constexpr std::uintptr_t kQueueSpriteWithColour = 0x004d8050;
constexpr std::uintptr_t kAddHitArea = 0x004d8240;
constexpr std::uintptr_t kDebugLog = 0x00428720;
constexpr std::uintptr_t kThrowAssertmentFailed = 0x006119b0;

using ConstructFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t value);
using Arg1Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a);
using Arg2Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b);
using Arg3Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b, std::uint32_t c);
using Arg4Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                          std::uint32_t d);
using Arg5Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                          std::uint32_t d, std::uint32_t e);
using Arg6Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                          std::uint32_t d, std::uint32_t e, std::uint32_t f);
using DebugLogFn = void(__cdecl*)(const char* format, ...);
using AssertFn = void(__fastcall*)(const char* expression, const char* file, std::int32_t line);

constexpr std::uint32_t kParamMask = 0x3f;
constexpr std::int32_t kDetailParam = 0x48;      // hit areas, smoke and sequences only above this
constexpr std::uint32_t kMenuMarker = 0x4000b;      // raw-ok: an animation id (group 4)
constexpr std::uint32_t kRowMenuMarker = 0xf8;
constexpr std::uint32_t kParamMenuMarker = 0xff;
constexpr std::uint32_t kHitAreaKind = 100;
constexpr std::uint32_t kConstructionHalf = 125;  // progress: the frame-1 part is complete here
constexpr std::uint32_t kPartScale = 0x20c;       // fraction per progress step (16.16)
constexpr std::uint32_t kSmallPartScale = 0x106;  // types 1 and 0x22
constexpr std::uint32_t kSmoke = 0xd002b;  // raw-ok: an animation id (group 13)
constexpr std::uint32_t kSmokeFrames = 40;
constexpr std::uint32_t kPhaseStep = 0xdbd0bb;  // raw-ok: not an offset; between the phases of a building's smoke puffs / sequences
constexpr std::int32_t kAssertLine = 250;

// g_raceAnimations slots.
constexpr std::uint32_t kWorkerColourSlot = 351;   // the worker's colour layer
constexpr std::uint32_t kWorkerSpriteSlot = 352;
constexpr std::uint32_t kExtraWorkerColourSlot = 276;  // the extra workers of types 0xf, 0x10, 0x12
constexpr std::uint32_t kExtraWorkerSpriteSlot = 277;
constexpr std::uint32_t kExtraSpriteSlot = 443;    // the extra sprite of types 0x2a, 0x2c, 0x2e, 0x36
constexpr std::uint32_t kType22Slot = 125;

// The shadow slot of a finished building per type 1..0x38 (the sprite is
// the next slot); 0 for type 0x22 (no shadow).
constexpr std::uint16_t kBuildingSlots[0x39] = {
    0,   0,   2,   4,   6,   8,   10,  54,  56,  58,  60,  62,  64,  66,  135, 71,  73,  97,  75,
    137, 107, 95,  115, 101, 103, 105, 109, 111, 113, 117, 99,  119, 155, 157, 0,   127, 147, 143,
    145, 129, 149, 139, 141, 133, 131, 153, 151, 121, 123, 356, 402, 404, 406, 408, 425, 427, 117,
};
// The construction slot per type 2..0x38 (types 0x36 / 0x37 use the slots of 0x2e / 0x2d there).
constexpr std::uint16_t kConstructionSlots[0x39] = {
    0,   0,   2,   4,   6,   8,   10,  54,  56,  58,  60,  62,  64,  66,  135, 71,  73,  97,  75,
    137, 107, 95,  115, 101, 103, 105, 109, 111, 113, 117, 99,  119, 155, 157, 0,   127, 147, 143,
    145, 129, 149, 139, 141, 133, 131, 153, 151, 121, 123, 356, 402, 404, 406, 408, 151, 153, 117,
};

// The eight hit areas around a building (dx, dy).
constexpr std::int32_t kHitAreas[8][2] = {{-40, 0}, {-10, 0}, {-30, 0}, {0, 15}, {0, -15}, {30, 0}, {10, 0}, {40, 0}};

struct Pile {
    std::int32_t dx, dy;
};
// Stock piles of types 0x1f, 0x2f, 0x30: the animation and five pile places.
struct StockLayout {
    std::uint32_t animation;
    Pile piles[5];
};
constexpr StockLayout kStock1f = {0x10058, {{0x10, 0x30}, {0x30, 0x20}, {0x58, 8}, {0x20, 0x28}, {0x44, 0x14}}};
constexpr StockLayout kStock2f = {0x10057, {{0, 0x40}, {0x30, 0x30}, {0x58, 0x18}, {0x18, 0x38}, {0x44, 0x24}}};
constexpr StockLayout kStock30 = {0x10056, {{0, 0x30}, {0x30, 0x20}, {0x50, 0x10}, {0x18, 0x28}, {0x40, 0x18}}};
constexpr std::uint32_t kPileMax = 6;

struct Drawing {
    CDrawer* self;
    const DrawDataView* data;
    const BuildingCell* cell;
    std::uint32_t column, row, x, y;
    std::uint32_t param;

    std::uint32_t ScreenY() const { return y - cell->lift; }
    std::uint32_t Row2() const { return 2 * row; }
    std::uint32_t Race() const { return cell->race; }
    std::uint32_t RaceAnimation(std::uint32_t slot) const { return g_raceAnimations[slot][Race()]; }

    std::uint32_t Getter(std::uintptr_t fn, std::uint32_t id) const {
        std::uint32_t slot = 0;
        const auto* result = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(
            reinterpret_cast<Arg2Fn>(fn)(self->graphics, nullptr, reinterpret_cast<std::uint32_t>(&slot), id)));
        return *result;
    }
    std::uint32_t ShadowAnimation(std::uint32_t id) const { return Getter(kShadowAnimationPointer, id); }
    std::uint32_t ColourAnimation(std::uint32_t id) const { return Getter(kColorIndexedAnimationPointer, id); }
    std::uint32_t Animation(std::uint32_t id) const { return Getter(kAnimationPointer, id); }
    static std::uint32_t ImageOf(std::uintptr_t fn, std::uint32_t list, std::uint32_t frame) {
        std::uint32_t local = list;
        std::uint32_t slot = 0;
        const auto* result = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(
            reinterpret_cast<Arg2Fn>(fn)(&local, nullptr, reinterpret_cast<std::uint32_t>(&slot), frame)));
        return *result;
    }
    // The checked frame (inlined in the original for small frames).
    static std::uint32_t FrameOf(std::uint32_t animation, std::uint32_t frame) {
        return ImageOf(kListImageChecked2, animation, frame);
    }
    std::uint32_t Shadow(std::uint32_t slot, std::uint32_t frame) const {
        return ImageOf(kListImageChecked, ShadowAnimation(RaceAnimation(slot)), frame);
    }
    std::uint32_t Palette() const {
        return reinterpret_cast<Arg2Fn>(kPalettePointer)(self->graphics, nullptr,
                                                         g_playerColourIndex[cell->owner] + 0x10000u, param);
    }

    void QueueShadow(std::uint32_t queueRow, std::uint32_t image) const {
        reinterpret_cast<Arg4Fn>(kQueueShadow)(self, nullptr, queueRow, x, ScreenY(), image);
    }
    void QueueSprite(std::uint32_t queueRow, std::uint32_t sx, std::uint32_t sy, std::uint32_t image) const {
        reinterpret_cast<Arg5Fn>(kQueueSprite)(self, nullptr, queueRow, sx, sy, image, param);
    }
    void QueueSprite(std::uint32_t queueRow, std::uint32_t image) const { QueueSprite(queueRow, x, ScreenY(), image); }
    void QueueShadowPart(std::uint32_t queueRow, std::uint32_t image, std::uint32_t fraction) const {
        reinterpret_cast<Arg5Fn>(kQueueShadowPart)(self, nullptr, queueRow, x, ScreenY(), image, fraction);
    }
    void QueueSpritePart(std::uint32_t queueRow, std::uint32_t image, std::uint32_t fraction) const {
        reinterpret_cast<Arg6Fn>(kQueueSpritePart)(self, nullptr, queueRow, x, ScreenY(), image, param, fraction);
    }
    // A sprite with a colour layer at (sx, sy).
    void QueueColoured(std::uint32_t queueRow, std::uint32_t sx, std::uint32_t sy, std::uint32_t colourImage,
                       std::uint32_t palette, std::uint32_t sprite) const {
        const std::uint32_t layer =
            reinterpret_cast<Arg5Fn>(kQueueColourLayer)(self, nullptr, queueRow, sx, sy, colourImage, palette);
        reinterpret_cast<Arg6Fn>(kQueueSpriteWithColour)(self, nullptr, queueRow, sx, sy, sprite, param, layer);
    }
    // A phase that varies per building position: ((row + origin_row) * 0x1847d
    // + (column + origin_column) * 18 + (clock >> shift)) - offset.
    std::uint32_t PositionPhase(int shift, std::uint32_t offset) const {
        return (row + static_cast<std::uint32_t>(data->origin_row)) * 0x1847du +
               (column + static_cast<std::uint32_t>(data->origin_column)) * 18u + (data->clock >> shift) - offset;
    }
};

// --- 2. a building under construction (0x4a9bd4..0x4aa34e).
void QueueConstruction(const Drawing& d, std::uint32_t progress) {
    const std::uint32_t type = d.cell->type;
    if (type == 1) {
        const std::uint32_t fraction = progress * kSmallPartScale;
        d.QueueShadowPart(d.Row2(), d.Shadow(0, 0), fraction);
        const std::uint32_t animation = d.Animation(d.RaceAnimation(1));
        constexpr std::int32_t kRows[6] = {-8, -4, 4, 8, 4, -4};
        for (std::uint32_t frame = 0; frame < 6; ++frame) {
            d.QueueSpritePart(d.Row2() + kRows[frame], Drawing::FrameOf(animation, frame), fraction);
        }
        return;
    }
    if (type == 0x22) {
        const std::uint32_t fraction = progress * kSmallPartScale;
        const std::uint32_t animation = d.Animation(d.RaceAnimation(kType22Slot));
        d.QueueSpritePart(d.Row2() - 20, Drawing::FrameOf(animation, 0), fraction);
        d.QueueSpritePart(d.Row2(), Drawing::FrameOf(animation, 1), fraction);
        return;
    }
    if (type < 2 || type > 0x38) {
        reinterpret_cast<DebugLogFn>(kDebugLog)(texts::kBuildingPhaseUndefinedLog, type);
        reinterpret_cast<AssertFn>(kThrowAssertmentFailed)(texts::kBuildingPhaseUndefined,
                                                           texts::kDrawBuildingFile, kAssertLine);
        // (the original continues with an uninitialized slot; the assertion throws)
        return;
    }
    const std::uint32_t slot = kConstructionSlots[type];
    if (progress < kConstructionHalf) {
        const std::uint32_t fraction = progress * kPartScale;
        d.QueueShadowPart(d.Row2(), d.Shadow(slot, 1), fraction);
        d.QueueSpritePart(d.Row2(), Drawing::FrameOf(d.Animation(d.RaceAnimation(slot + 1)), 1), fraction);
        return;
    }
    d.QueueShadow(d.Row2(), d.Shadow(slot, 1));
    d.QueueSprite(d.Row2(), Drawing::FrameOf(d.Animation(d.RaceAnimation(slot + 1)), 1));
    const std::uint32_t fraction = (progress - kConstructionHalf) * kPartScale;
    d.QueueShadowPart(d.Row2(), d.Shadow(slot, 0), fraction);
    d.QueueSpritePart(d.Row2(), Drawing::FrameOf(d.Animation(d.RaceAnimation(slot + 1)), 0), fraction);
}

// --- 3. finished buildings.

// The worker animation with the owner's colour (flag +0x12): frame
// p(1) % 12 at the type's work offset.
void QueueWorker(const Drawing& d) {
    if (d.cell->working[0] == 0) {
        return;
    }
    const DrawDataView& data = *d.data;
    const std::uint32_t frame =
        ((data.clock >> 1) + data.origin_row + data.origin_column + d.column + d.row) % 12;
    const std::uint32_t palette = d.Palette();
    const std::uint32_t colourImage = Drawing::ImageOf(kListImage, d.ColourAnimation(d.RaceAnimation(kWorkerColourSlot)), frame);
    const auto& offset = g_buildingWorkOffsets[d.Race() + d.cell->type * 4u];
    const std::uint32_t sx = d.x + static_cast<std::uint32_t>(offset[0]);
    const std::uint32_t sy = (static_cast<std::uint32_t>(offset[1]) - d.cell->lift) + d.y;
    // (the colour layer is queued before the sprite's image is looked up)
    const std::uint32_t layer =
        reinterpret_cast<Arg5Fn>(kQueueColourLayer)(d.self, nullptr, d.Row2(), sx, sy, colourImage, palette);
    const std::uint32_t sprite = Drawing::FrameOf(d.Animation(d.RaceAnimation(kWorkerSpriteSlot)), frame);
    reinterpret_cast<Arg6Fn>(kQueueSpriteWithColour)(d.self, nullptr, d.Row2(), sx, sy, sprite, d.param, layer);
}

// Extra workers of types 0xf, 0x10, 0x12 (one per flag +0x12 + i), the
// owner's race by Render::Entry204.
void QueueExtraWorkers(const Drawing& d, std::uint32_t first, std::uint32_t count) {
    const std::uint32_t owner = d.cell->owner;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (d.cell->working[i] == 0) {
            continue;
        }
        const auto& entry = g_buildingWorkerSprites[d.Race()][first + i];
        const std::uint32_t sx = static_cast<std::uint32_t>(entry[0]) + d.x;
        const std::uint32_t sy = (static_cast<std::uint32_t>(entry[1]) - d.cell->lift) + d.y;
        const std::uint32_t palette = d.Palette();
        const std::uint32_t k = reinterpret_cast<Arg1Fn>(kEntry204)(d.self, nullptr, owner);
        const std::uint32_t colour = d.ColourAnimation(g_raceAnimations[kExtraWorkerColourSlot][k]);
        const std::uint32_t colourImage = Drawing::ImageOf(kListImage, colour, static_cast<std::uint32_t>(entry[2]));
        const std::uint32_t layer =
            reinterpret_cast<Arg5Fn>(kQueueColourLayer)(d.self, nullptr, d.Row2(), sx, sy, colourImage, palette);
        const std::uint32_t k2 = reinterpret_cast<Arg1Fn>(kEntry204)(d.self, nullptr, owner);
        const std::uint32_t sprite =
            Drawing::FrameOf(d.Animation(g_raceAnimations[kExtraWorkerSpriteSlot][k2]), static_cast<std::uint32_t>(entry[2]));
        reinterpret_cast<Arg6Fn>(kQueueSpriteWithColour)(d.self, nullptr, d.Row2(), sx, sy, sprite, d.param, layer);
    }
}

// The smoke of types 0xf, 0x10, 0x12: up to (255 - smoke) * scale / 256
// puffs at the positions of the race.
void QueueSmoke(const Drawing& d, std::int32_t scale, const std::int32_t (*positions)[2]) {
    if (static_cast<std::int32_t>(d.param) <= kDetailParam) {
        return;
    }
    const std::uint32_t animation = d.Animation(kSmoke);
    auto puffs = [&] { return ((255 - static_cast<std::int32_t>(d.cell->smoke)) * scale) / 256; };
    std::uint32_t offset = 0;
    for (std::int32_t i = 0; i < puffs(); ++i) {
        const std::uint32_t frame = d.PositionPhase(3, offset) % kSmokeFrames;
        const std::uint32_t sprite = Drawing::FrameOf(animation, frame);
        d.QueueSprite(d.Row2() + 2, d.x + static_cast<std::uint32_t>(positions[i][0]),
                      (static_cast<std::uint32_t>(positions[i][1]) - d.cell->lift) + d.y, sprite);
        offset += kPhaseStep;
    }
}

// Types 0xf, 0x10, 0x12: a building whose shadow and sprite go to row
// 2 * row - 1, its workers, the sprite frame 2 at 2 * row + 1, smoke.
void QueueWorkshop(const Drawing& d, std::uint32_t slot, std::uint32_t firstWorker, std::uint32_t workers,
                   std::int32_t smokeScale, const std::int32_t (*smoke)[2]) {
    d.QueueShadow(d.Row2() - 1, d.Shadow(slot, 0));
    d.QueueSprite(d.Row2() - 1, Drawing::FrameOf(d.Animation(d.RaceAnimation(slot + 1)), 0));
    QueueExtraWorkers(d, firstWorker, workers);
    d.QueueSprite(d.Row2() + 1, Drawing::FrameOf(d.Animation(d.RaceAnimation(slot + 1)), 2));
    QueueSmoke(d, smokeScale, smoke);
}

// Stock piles (+0x13): up to five piles of up to six units.
void QueueStock(const Drawing& d, const StockLayout& layout) {
    std::uint32_t left = d.cell->working[1];
    if (left == 0) {
        return;
    }
    const std::uint32_t animation = d.Animation(layout.animation);
    for (const Pile& pile : layout.piles) {
        const std::uint32_t units = left > kPileMax ? kPileMax : left;
        d.QueueSprite(d.Row2() + 1, d.x + static_cast<std::uint32_t>(pile.dx),
                      (d.ScreenY()) + static_cast<std::uint32_t>(pile.dy), Drawing::FrameOf(animation, units - 1));
        left -= units;
        if (left == 0) {
            return;
        }
    }
}

// Sequence animations (+0x11) of types 0x17 (sprite only) and 0x31
// (with colour layers).
void QueueSequences(const Drawing& d, std::uint32_t sequence, int shift, const std::int32_t (*positions)[2],
                    bool coloured) {
    if (d.cell->sequences == 0 || static_cast<std::int32_t>(d.param) <= kDetailParam) {
        return;
    }
    std::uint32_t offset = 0;
    for (std::uint32_t i = 0; i < d.cell->sequences; ++i) {
        const std::uint32_t phase = d.PositionPhase(shift, offset);
        const std::uint32_t frames =
            reinterpret_cast<Arg1Fn>(kSequenceFrameCount)(d.self->graphics, nullptr, sequence) & 0xffff;
        std::uint32_t frame[5];
        const auto* record = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(
            reinterpret_cast<Arg3Fn>(kGetSequenceFrame)(d.self->graphics, nullptr,
                                                        reinterpret_cast<std::uint32_t>(frame), sequence,
                                                        phase % frames)));
        std::uint32_t copy[5];
        std::memcpy(copy, record, sizeof(copy));
        std::uint32_t out = 0;
        const std::uint32_t body = *reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(
            reinterpret_cast<Arg1Fn>(kBodyImage)(copy, nullptr, reinterpret_cast<std::uint32_t>(&out))));
        const std::uint32_t sx = static_cast<std::uint32_t>(positions[i][0]) + d.x;
        const std::uint32_t sy = (static_cast<std::uint32_t>(positions[i][1]) - d.cell->lift) + d.y;
        if (coloured) {
            std::uint32_t out2 = 0;
            const std::uint32_t colour = *reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(
                reinterpret_cast<Arg1Fn>(kColorIndexedImage)(copy, nullptr, reinterpret_cast<std::uint32_t>(&out2))));
            const std::uint32_t palette = d.Palette();
            d.QueueColoured(d.Row2() + 1, sx, sy, colour, palette, body);
        } else {
            d.QueueSprite(d.Row2() + 1, sx, sy, body);
        }
        offset += kPhaseStep;
    }
}

// The extra sprite (frame +0xe) of types 0x2a, 0x2c, 0x2e, 0x36 at the
// type's position (0x36: 8 right and 0x2f lower).
void QueueExtraSprite(const Drawing& d, std::int32_t dx, std::int32_t dy) {
    const std::uint32_t sprite = Drawing::FrameOf(d.Animation(d.RaceAnimation(kExtraSpriteSlot)), d.cell->extraFrame);
    const auto& position = g_buildingExtraSpritePositions[d.cell->type + d.Race() * 57u];
    d.QueueSprite(d.Row2() + 1, static_cast<std::uint32_t>(position[0]) + dx + d.x,
                  (static_cast<std::uint32_t>(position[1]) - d.cell->lift) + static_cast<std::uint32_t>(dy) + d.y,
                  sprite);
}

void QueueFinished(const Drawing& d) {
    const std::uint32_t type = d.cell->type;
    if (type < 1 || type > 0x38) {
        return;
    }
    const std::uint32_t slot = kBuildingSlots[type];
    switch (type) {
    case 1: {  // six parts
        d.QueueShadow(d.Row2(), d.Shadow(0, 0));
        const std::uint32_t animation = d.Animation(d.RaceAnimation(1));
        constexpr std::int32_t kRows[6] = {-8, -4, -4, 4, 4, 8};
        constexpr std::uint32_t kFrames[6] = {0, 1, 5, 2, 4, 3};
        for (int i = 0; i < 6; ++i) {
            d.QueueSprite(d.Row2() + kRows[i], Drawing::FrameOf(animation, kFrames[i]));
        }
        return;
    }
    case 0x0f: QueueWorkshop(d, slot, 0, 1, 4, g_smokePositionsSmall[d.Race()]); return;
    case 0x10: QueueWorkshop(d, slot, 12, 3, 7, g_smokePositionsMedium[d.Race()]); return;
    case 0x12: QueueWorkshop(d, slot, 24, 6, 11, g_smokePositionsLarge[d.Race()]); return;
    case 0x22: {
        const std::uint32_t animation = d.Animation(d.RaceAnimation(kType22Slot));
        d.QueueSprite(d.Row2() - 20, Drawing::FrameOf(animation, 0));
        d.QueueSprite(d.Row2(), Drawing::FrameOf(animation, 1));
        return;
    }
    default: break;
    }
    // The shadow and the sprite.
    d.QueueShadow(d.Row2(), d.Shadow(slot, 0));
    if (type != 0x11 || (d.cell->flags & 2) == 0) {
        d.QueueSprite(d.Row2(), Drawing::FrameOf(d.Animation(d.RaceAnimation(slot + 1)), 0));
    }
    switch (type) {
    // Without extras.
    case 6: case 0x13: case 0x1a: case 0x1b: case 0x1c: case 0x29: case 0x2b: case 0x2d: case 0x37: return;
    case 0x17: QueueSequences(d, 0xf0223, 3, g_buildingSequencePositions17[d.Race()], false); break;
    case 0x31: QueueSequences(d, 0xf0226, 2, g_buildingSequencePositions31[d.Race()], true); break;
    case 0x1f: QueueStock(d, kStock1f); break;
    case 0x2f: QueueStock(d, kStock2f); break;
    case 0x30: QueueStock(d, kStock30); break;
    case 0x2a: case 0x2c: case 0x2e: QueueExtraSprite(d, 0, 0); return;
    case 0x36: QueueExtraSprite(d, 8, 0x2f); return;
    default: break;
    }
    QueueWorker(d);
}

}  // namespace

using QueueBuildingFn = void(__fastcall*)(CDrawer* self, void* edx, const DrawDataView* data, std::uint32_t column,
                                          std::uint32_t row, std::uint32_t x, std::uint32_t y);
void __fastcall CDrawer_QueueBuilding(CDrawer* self, void* edx, const DrawDataView* data, std::uint32_t column,
                                      std::uint32_t row, std::uint32_t x, std::uint32_t y);
S3RE_HOOK(0x004a9960, CDrawer_QueueBuilding, QueueBuildingFn, Original_CDrawer_QueueBuilding, CDrawer_QueueBuilding);

// Purpose: Queues a building of a visible map cell (finished or under
// construction, with its workers, smoke and stock) and its hit areas for
// drawing in the frame.
void __fastcall CDrawer_QueueBuilding(CDrawer* self, void*, const DrawDataView* data, std::uint32_t column,
                                      std::uint32_t row, std::uint32_t x, std::uint32_t y) {
    std::uint32_t locals[4];
    for (std::uint32_t& local : locals) {
        reinterpret_cast<ConstructFn>(kGfxDwordConstruct)(&local, nullptr, 0);
    }
    const auto* cell = reinterpret_cast<const BuildingCell*>(reinterpret_cast<std::uintptr_t>(data) +
                                                             offsetof(DrawDataView, cells) +
                                                             (column * kDrawCellRows + row) * sizeof(BuildingCell));
    const Drawing d{self, data, cell, column, row, x, y, static_cast<std::uint32_t>(cell->direction & kParamMask) * 4};
    if (cell->building == static_cast<std::uint32_t>(g_selectedBuilding)) {
        reinterpret_cast<Arg5Fn>(kQueueSprite)(self, nullptr, kRowMenuMarker, x, d.ScreenY(),
                                               Drawing::FrameOf(d.Animation(kMenuMarker), 0), kParamMenuMarker);
    }
    if (static_cast<std::int32_t>(d.param) > kDetailParam) {
        for (const auto& area : kHitAreas) {
            reinterpret_cast<Arg5Fn>(kAddHitArea)(self, nullptr, kHitAreaKind, x + area[0], d.ScreenY() + area[1],
                                                  cell->building, 0);
        }
    }
    const std::uint32_t progress = cell->progress;
    if (progress >= 2) {
        QueueConstruction(d, progress);
    }
    if (progress == 0) {
        QueueFinished(d);
    }
}

// ---------------------------------------------------------------------------
// Tests: random cells (types 1..0x38, progress 0, 1 or a part), draw-data
// fields and arguments; the animation getters stubbed to return a random
// image list (the frame lookups then run ours / the original on it), the
// queue functions, the palette and the sequence getters recorded.
// ---------------------------------------------------------------------------

namespace {

struct Log {
    std::vector<std::uint32_t> values;
    std::uint8_t* list = nullptr;
    std::uint8_t* record = nullptr;
};

std::uint32_t Mix(std::uint32_t v) {
    v ^= v >> 13;
    v *= 0x9e3779b1u;
    return v ^ (v >> 16);
}

void RunQueueBuilding(QueueBuildingFn fn, CDrawer* drawer, const DrawDataView* data, std::uint32_t column,
                      std::uint32_t row, std::uint32_t x, std::uint32_t y, Log& log) {
    test::CalleeStubs stubs;
    stubs.SetWorld(reinterpret_cast<CGameData*>(drawer));
    const std::uint32_t list = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(log.list));
    auto relative = [list](std::uint32_t image) { return image - list; };
    auto out = [](std::uint32_t address, std::uint32_t value) {
        *reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(address)) = value;
        return address;
    };
    stubs.Install<1, kEntry204>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {1, self, a[0]});
        return Mix(a[0]) & 3;
    });
    stubs.Install<2, kShadowAnimationPointer>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {2, self, a[1]});
        return out(a[0], list);
    });
    stubs.Install<2, kColorIndexedAnimationPointer>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {3, self, a[1]});
        return out(a[0], list);
    });
    stubs.Install<2, kAnimationPointer>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {4, self, a[1]});
        return out(a[0], list);
    });
    stubs.Install<2, kPalettePointer>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {5, self, a[0], a[1]});
        return Mix(a[0] ^ a[1]);
    });
    stubs.Install<1, kSequenceFrameCount>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {6, self, a[0]});
        return 0xabcd0000u | (1 + Mix(a[0]) % 50);
    });
    stubs.Install<3, kGetSequenceFrame>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {7, self, a[1], a[2]});
        std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(a[0])), log.record, 20);
        reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(a[0]))[1] += a[2];
        return a[0];
    });
    stubs.Install<4, kQueueShadow>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {8, self, a[0], a[1], a[2], relative(a[3])});
        return a[0];
    });
    stubs.Install<5, kQueueShadowPart>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {9, self, a[0], a[1], a[2], relative(a[3]), a[4]});
        return a[0];
    });
    stubs.Install<5, kQueueSprite>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {10, self, a[0], a[1], a[2], relative(a[3]), a[4]});
        return a[0];
    });
    stubs.Install<6, kQueueSpritePart>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {11, self, a[0], a[1], a[2], relative(a[3]), a[4], a[5]});
        return a[0];
    });
    stubs.Install<5, kQueueColourLayer>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {12, self, a[0], a[1], a[2], a[3], a[4]});
        return Mix(a[0] + a[3]);
    });
    stubs.Install<6, kQueueSpriteWithColour>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {13, self, a[0], a[1], a[2], a[3], a[4], a[5]});
        return a[0];
    });
    // Only words are stored by the hit area function.
    stubs.Install<5, kAddHitArea>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {14, self, a[0] & 0xffff, a[1] & 0xffff, a[2] & 0xffff, a[3] & 0xffff,
                                             a[4] & 0xffff});
        return 0;
    });
    fn(drawer, nullptr, data, column, row, x, y);
}

void CompareQueueBuilding(test::Context& ctx, QueueBuildingFn candidate, int cases) {
    std::vector<std::uint8_t> dataBytes(sizeof(DrawDataView));
    std::vector<std::uint8_t> drawerBytes(sizeof(CDrawer));
    std::uint8_t list[8 + 4 * 256];
    std::uint8_t record[20];
    auto* const data = reinterpret_cast<DrawDataView*>(dataBytes.data());
    auto* const drawer = reinterpret_cast<CDrawer*>(drawerBytes.data());
    const std::int32_t savedSelected = g_selectedBuilding;
    for (int i = 0; i < cases; ++i) {
        test::Rng& rng = ctx.rng();
        if (i % 200 == 0) {
            rng.Fill(dataBytes.data(), dataBytes.size());
            rng.Fill(drawerBytes.data(), drawerBytes.size());
        }
        const std::uint32_t column = rng.Below(kDrawCellRows);
        const std::uint32_t row = rng.Below(kDrawCellRows);
        auto& cell = *reinterpret_cast<BuildingCell*>(&data->cells[column][row]);
        rng.Fill(&cell, sizeof(cell));
        cell.race = static_cast<std::uint8_t>(rng.Below(4));
        cell.type = static_cast<std::uint8_t>(1 + rng.Below(0x38));
        const std::uint32_t r = rng.Below(4);
        cell.progress = static_cast<std::uint8_t>(r == 0 ? 0 : r == 1 ? rng.Below(3) : rng.Below(256));
        if (rng.Chance(1, 2)) {
            cell.direction = static_cast<std::uint8_t>(19 + rng.Below(45));
        }
        if (rng.Chance(1, 2)) {
            cell.sequences = static_cast<std::uint8_t>(rng.Below(4));
        }
        if (rng.Chance(1, 3)) {
            cell.working[1] = static_cast<std::uint8_t>(rng.Below(40));
        }
        cell.owner = static_cast<std::uint8_t>(rng.Below(20));
        data->clock = rng.Next32();
        data->origin_column = static_cast<std::int32_t>(rng.Below(1024));
        data->origin_row = static_cast<std::int32_t>(rng.Below(1024));
        g_selectedBuilding = rng.Chance(1, 4) ? cell.building : static_cast<std::int32_t>(rng.Next32());
        rng.Fill(list, sizeof(list));
        list[7] = static_cast<std::uint8_t>(rng.Chance(1, 4) ? rng.Below(8) : rng.Below(256));
        for (std::uint32_t k = 0; k < 256; ++k) {
            const std::uint32_t offset = rng.Below(0x10000);
            std::memcpy(list + 8 + 4 * k, &offset, 4);
        }
        rng.Fill(record, sizeof(record));
        const std::uint32_t x = rng.Next32();
        const std::uint32_t y = rng.Chance(1, 2) ? rng.Below(600) : rng.Next32();
        Log a, b;
        a.list = b.list = list;
        a.record = b.record = record;
        RunQueueBuilding(Original_CDrawer_QueueBuilding, drawer, data, column, row, x, y, a);
        RunQueueBuilding(candidate, drawer, data, column, row, x, y, b);
        if (!S3RE_EXPECT(a.values == b.values)) {
            std::size_t k = 0;
            while (k < a.values.size() && k < b.values.size() && a.values[k] == b.values[k]) {
                ++k;
            }
            ctx.Fail(__FILE__, __LINE__, "  case %d, type 0x%x, progress %u, sizes %zu / %zu, first difference %zu",
                     i, cell.type, cell.progress, a.values.size(), b.values.size(), k);
            break;
        }
    }
    g_selectedBuilding = savedSelected;
}

// Wrong: one pixel lower.
void __fastcall WrongQueueBuilding(CDrawer* self, void* edx, const DrawDataView* data, std::uint32_t column,
                                   std::uint32_t row, std::uint32_t x, std::uint32_t y) {
    CDrawer_QueueBuilding(self, edx, data, column, row, x, y + 1);
}

}  // namespace

S3RE_TEST(CDrawer_QueueBuilding_MatchesOriginal) {
    CompareQueueBuilding(ctx, &CDrawer_QueueBuilding, 40000);
}

S3RE_TEST(CDrawer_QueueBuilding_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareQueueBuilding(probe, &WrongQueueBuilding, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
