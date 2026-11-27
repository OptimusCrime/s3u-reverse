// Queueing the settler of a draw cell (Volker\DrawBuilding.cpp, object file
// 0x4c85a0..0x4d3050; called by the frame drawer 0x4b26c0).
//
// CDrawer::QueueSettler (0x004c85a0; was Render_sub_4c85a0): (DrawDataView*
//   data, column, row, x, y) thiscall, ret 0x14. One function (one stack
//   frame of 0xafc bytes; jump tables at 0x4d1ae0 (settler types 0..0x2f)
//   and 0x4d1ba0 (a carrier's good 0..0x23), 77 cases built from one
//   template). The cell is data->cells[column][row], read as a SettlerCell
//   below; sprites go to (x, y - lift), param = 4 * (direction & 0x3f),
//   queue row 2 * row (one less when walking in directions 1 and 2, one
//   more in 4 and 5). "Race" is Render::Entry204 0x4c7ab0 of the owner; an
//   animation is g_raceAnimations[slot][race]; a "triple" at slot s is the
//   shadow (s, CGraphicsData::ShadowAnimationPointer + ListImageChecked),
//   the colour layer (s + 1, ColorIndexedAnimationPointer + ListImage, the
//   owner's palette) and the sprite (s + 2, AnimationPointer + the checked
//   frame 0x4b19c0) of one frame.
//   0. Prologue (0x4c85a0..0x4c8773): a todo count over 50000 is logged;
//      race 3 lifts the markers by 5; flag 0x40 (in a tower) moves the
//      settler to its tower place (g_towerSlotOffsets, by the slot byte
//      +0x24).
//   1. Ships, types 0x2b / 0x2c (0x4d06aa..0x4d1ac1): hull, passengers
//      (ferry) or cargo piles (trade ship) by their drawing order, five hit
//      areas; under construction (+0x0d) only the hull's part.
//   2. The catapult, type 0x29 (0x4c8794..0x4c9212): walking (three
//      animations) or shooting (action 0x8000..0x8005); its group marker
//      shows the stones it has as a number (CTextSurface::DrawTextF).
//   3. Other types, action 6.. (0x4d007c..0x4d06a5): a sequence animation
//      (CGraphicsData::GetSequenceFrame): offset, work sound, shadow /
//      colour layer / body if valid, a hit area for soldiers and the
//      special settlers.
//   4. Other types walking or standing (0x4c9215..0x4cbe51): the frame of
//      the direction and step, the status markers of flags 1, 2, 4, 8,
//      0x80, 0x10, the group marker, then per type (and carried good) one
//      triple, soldiers and special settlers with a hit area first. A
//      thief of another team than the local player's (flag 0x10 clear) is
//      drawn as a standing carrier in the colour of the cell's owner.

#include "game/enums/settler_types.h"
#include "game/global_access.h"
#include "game/globals.h"
#include "game/graphics/draw_data_layout.h"
#include "game/graphics/drawer_layout.h"
#include "game/graphics/text_surface.h"
#include "game/settlers/settler_groups.h"
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
// A draw cell as the settler drawer reads it (the same 48 bytes as
// DrawCell; the settler fields are +0x1e..+0x2d).
struct SettlerCell {
    std::uint8_t lift;           // +0x00 drawn this many pixels higher
    std::uint8_t unk01[3];
    std::uint8_t owner;          // +0x04 the cell's player (0xff: none); a disguised thief's colour
    std::uint8_t direction;      // +0x05 low 6 bits: param / 4
    std::uint8_t unk06[7];
    std::uint8_t ship_build;     // +0x0d a ship under construction: its progress (0: finished)
    std::uint8_t cargo[8];       // +0x0e ferry: passenger count, then their types; trade ship: 4 x
                                 //       (good, units)
    std::uint8_t unk16[8];
    std::uint16_t id;            // +0x1e the settler's number (hit areas, settler groups)
    std::uint32_t action;        // +0x20 0..5: walking/standing direction; else a sequence id;
                                 //       catapult 0x8000..0x8005: shooting in a direction
    std::uint8_t good;           // +0x24 the carried good; the catapult's stones; the tower place (flag 0x40)
    std::uint8_t step;           // +0x25 walking: the step 1..12 (catapult 1..36); 0 standing
    std::uint16_t steps;         // +0x26 the todo count (the frame of a sequence)
    std::uint8_t step_total;     // +0x28 walking: steps of the whole move (divisor)
    std::uint8_t player;         // +0x29 the owner
    std::uint8_t type;           // +0x2a the settler type
    std::uint8_t health;         // +0x2b
    std::uint8_t flags;          // +0x2c status markers 1, 2, 4, 8, 0x10, 0x80; 0x40 in a tower
    std::uint8_t max_health;     // +0x2d
    std::uint8_t unk2e[2];
};
#pragma pack(pop)
static_assert(sizeof(SettlerCell) == 0x30);
static_assert(offsetof(SettlerCell, ship_build) == 0x0d);
static_assert(offsetof(SettlerCell, id) == 0x1e);
static_assert(offsetof(SettlerCell, action) == 0x20);
static_assert(offsetof(SettlerCell, player) == 0x29);
static_assert(offsetof(SettlerCell, max_health) == 0x2d);

constexpr std::uintptr_t kEntry204 = 0x004c7ab0;
constexpr std::uintptr_t kShadowAnimationPointer = 0x005fabb0;
constexpr std::uintptr_t kColorIndexedAnimationPointer = 0x005fab50;
constexpr std::uintptr_t kAnimationPointer = 0x005faaf0;
constexpr std::uintptr_t kPalettePointer = 0x005fac10;
constexpr std::uintptr_t kGetSequenceFrame = 0x005fad50;
constexpr std::uintptr_t kOffsetX = 0x005fd980;
constexpr std::uintptr_t kOffsetY = 0x005fd990;
constexpr std::uintptr_t kBodyImage = 0x005fd9a0;
constexpr std::uintptr_t kColorIndexedImage = 0x005fd9b0;
constexpr std::uintptr_t kShadowImage = 0x005fd9c0;
constexpr std::uintptr_t kSoundId = 0x005fd9d0;
constexpr std::uintptr_t kSoundHighByte = 0x005fd9f0;
constexpr std::uintptr_t kIsValid = 0x005f6460;
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
constexpr std::uintptr_t kIsNearView = 0x004c7b50;
constexpr std::uintptr_t kSoundIsEnabled = 0x005ff4d0;
constexpr std::uintptr_t kSoundGetEffect = 0x005ff4f0;
constexpr std::uintptr_t kEffectPlay = 0x005c2fc0;
constexpr std::uintptr_t kClearTextRect = 0x004d65e0;
constexpr std::uintptr_t kBlitTextRect = 0x004d6740;
constexpr std::uintptr_t kDrawTextF = 0x004a87d0;
constexpr std::uintptr_t kDebugLog = 0x00428720;

using Arg0Fn = std::uint32_t(__fastcall*)(const void* self, void* edx);
using Arg1Fn = std::uint32_t(__fastcall*)(const void* self, void* edx, std::uint32_t a);
using Arg2Fn = std::uint32_t(__fastcall*)(const void* self, void* edx, std::uint32_t a, std::uint32_t b);
using Arg3Fn = std::uint32_t(__fastcall*)(const void* self, void* edx, std::uint32_t a, std::uint32_t b,
                                          std::uint32_t c);
using Arg4Fn = std::uint32_t(__fastcall*)(const void* self, void* edx, std::uint32_t a, std::uint32_t b,
                                          std::uint32_t c, std::uint32_t d);
using Arg5Fn = std::uint32_t(__fastcall*)(const void* self, void* edx, std::uint32_t a, std::uint32_t b,
                                          std::uint32_t c, std::uint32_t d, std::uint32_t e);
using Arg6Fn = std::uint32_t(__fastcall*)(const void* self, void* edx, std::uint32_t a, std::uint32_t b,
                                          std::uint32_t c, std::uint32_t d, std::uint32_t e, std::uint32_t f);
using DebugLogFn = void(__cdecl*)(const char* format, ...);
using DrawTextFFn = TextRect*(__cdecl*)(void* self, TextRect* result, std::int32_t font, const TextRect* rect,
                                        std::uint32_t flags, const char* format, ...);

using T = SettlerType;

constexpr std::uint32_t kParamMask = 0x3f;
constexpr std::uint32_t kMarkerParam = 0xff;
constexpr std::uint32_t kRowGroupBar = 0xf9;     // the group marker's health bar
constexpr std::uint32_t kRowGroupMarker = 0xf8;
constexpr std::uint32_t kGroupBarAnimation = 0x40006;     // raw-ok: an animation id (group 4)
constexpr std::uint32_t kGroupMarkerAnimation = 0x40007;  // raw-ok: an animation id (group 4)
constexpr std::uint32_t kGroupBarFrames = 7;
constexpr std::int8_t kRace3Raise = -5;          // race 3's markers are 5 pixels higher
constexpr std::uint32_t kRace3 = 3;
constexpr std::uint32_t kInTower = 0x40;         // flag: drawn at its tower place
constexpr std::uint8_t kFlagClassMarker = 0x80;  // flag: a marker only for types with a class
constexpr std::uint8_t kFlagUncovered = 0x10;    // flag: a marker (not for the thief); a thief with it is
                                                 // drawn as itself to every team
constexpr std::uint32_t kMaxSteps = 50000;       // a larger todo count is logged ("extrem verdächtig")
constexpr std::uint32_t kStandingFrame = 72;     // frames 72 + direction: standing (some types)
constexpr std::uint32_t kLastWalkFrame = 71;
constexpr std::uint32_t kLastCatapultFrame = 215;
constexpr std::uint32_t kDirections = 6;
constexpr std::uint32_t kShotFirst = 0x8000;     // the catapult's shooting actions 0x8000..0x8005
constexpr std::uint32_t kShotLast = 0x8005;
constexpr std::int32_t kLabelFont = 99;
constexpr std::uint32_t kLabelFlags = 9;
constexpr std::uint32_t kHitCatapult = 40;
constexpr std::uint32_t kHitTradeShip = 50;
constexpr std::uint32_t kHitFerry = 51;
constexpr std::uint32_t kCarrierPassengerFrame = 4;  // passengers: frame 12 * direction + 4

// g_raceAnimations slots of the catapult and the ships.
constexpr std::uint32_t kCatapultShadowSlot = 353;
constexpr std::uint32_t kCatapultSlot = 354;
constexpr std::uint32_t kCatapultTopSlot = 355;  // not for race 3
constexpr std::uint32_t kCatapultShotOffset = 0x7e7e;        // slot = action - this (386..391)
constexpr std::uint32_t kCatapultShotShadowOffset = 0x7e75;  // slot = action - this (395..400)
constexpr std::uint32_t kTradeShipSlot = 361;      // +0 / +1 behind, +2 / +3 hull and colour, +4 front,
constexpr std::uint32_t kFerrySlot = 369;          //  +5 under construction
constexpr std::uint32_t kTradeShipShadowSlot = 444;
constexpr std::uint32_t kFerryShadowSlot = 445;
constexpr std::uint32_t kPassengerOrdersBehind = 1;      // drawing orders 1..10 (cargo 1..4) behind
constexpr std::uint32_t kPassengerOrdersFront = 0x81;    // 0x81..0x8a (cargo 0x81..0x84) in front
constexpr std::uint32_t kFerrySeatsPerDirection = 10;
constexpr std::uint32_t kCargoPlacesPerDirection = 4;
// Construction fraction of a ship: (build + 10) * 0xffff / (time * 10 + 11),
// halved below 40000, then compressed into 20000..
constexpr std::uint32_t kFractionKnee = 40000;

// The status markers of the flags (sprite, dx, dy): walking (frame 0 of the
// sprite) and in a sequence animation (frames of 0x40006).
struct StatusMarker {
    std::uint8_t flag;
    std::uint32_t animation;
    std::int32_t dx;
    std::int32_t dy;
    std::uint32_t sequenceFrame;
    std::int32_t sequenceDy;
};
constexpr StatusMarker kStatusMarkers[6] = {
    {1, 0x10082, -7, -30, 3, -25},  {2, 0x10081, -3, -33, 7, -28},    {4, 0x10080, 3, -33, 5, -28},
    {8, 0x10083, 12, -27, 0, -22},  {0x80, 0x10085, 7, -30, 0, -25},  {0x10, 0x10084, -12, -27, 0, -22},
};

// The triple slot of a carrier per carried good 0..0x23 (jump table
// 0x4d1ba0; 0: good 0x1f is not drawn).
constexpr std::uint16_t kCarrierSlots[36] = {
    18,  21,  24,  159, 261, 165, 168, 171, 270, 255, 174, 177, 180, 183, 392, 189, 192, 195,
    201, 204, 207, 287, 258, 264, 267, 305, 273, 296, 293, 290, 302, 0,   198, 299, 410, 413,
};

// Per type (jump table 0x4d1ae0): the hit area kind (0: none) and the triple
// slot: `slot` unless the good is listed in `goods` (then that entry's slot;
// a listed slot 0 = not drawn); `others` (0: not drawn) for any other good
// when `goods` is used as the complete list.
struct GoodSlot {
    std::uint8_t good;
    std::uint16_t slot;
};
struct TypeDrawing {
    std::uint8_t hitKind;
    std::uint16_t slot;            // good 0 (or any good if no `goods`)
    std::uint16_t otherGoods;      // any good not 0 and not listed (0: not drawn)
    GoodSlot goods[3];             // further goods with their own slot (good 0 = unused entry)
    std::uint8_t goodCount;
};
// clang-format off
constexpr TypeDrawing kTypes[0x30] = {
    /* 0x00 carrier: kCarrierSlots */ {0, 0, 0, {}, 0},
    /* 0x01 digger */       {0, 27, 27, {}, 0},
    /* 0x02 builder */      {0, 30, 30, {}, 0},
    /* 0x03 woodcutter */   {0, 39, 42, {}, 0},
    /* 0x04 stonecutter */  {0, 33, 36, {}, 0},
    /* 0x05 swordsman 1 */  {1, 45, 45, {}, 0},
    /* 0x06 sawmiller */    {0, 48, 48, {}, 0},
    /* 0x07 forester */     {0, 51, 68, {}, 0},
    /* 0x08 bowman 1 */     {2, 83, 83, {}, 0},
    /* 0x09 smelter */      {0, 92, 92, {}, 0},
    /* 0x0a miner */        {0, 162, 162, {}, 0},
    /* 0x0b smith */        {0, 210, 210, {}, 0},
    /* 0x0c miller */       {0, 234, 234, {}, 0},
    /* 0x0d baker */        {0, 237, 237, {}, 0},
    /* 0x0e butcher */      {0, 225, 225, {}, 0},
    /* 0x0f pikeman 1 */    {3, 246, 246, {}, 0},
    /* 0x10 farmer */       {0, 213, 219, {{1, 222}}, 1},
    /* 0x11 fisher */       {0, 228, 231, {}, 0},
    /* 0x12 waterworker */  {0, 240, 243, {}, 0},
    /* 0x13 shipyard */     {0, 358, 0, {{1, 377}, {11, 380}}, 2},
    /* 0x14 */              {0, 0, 0, {}, 0},
    /* 0x15 healer */       {0, 383, 383, {}, 0},
    /* 0x16 geologist */    {20, 278, 278, {}, 0},
    /* 0x17 thief */        {10, 281, 431, {}, 0},
    /* 0x18 charcoal */     {0, 326, 326, {}, 0},
    /* 0x19 distiller */    {0, 335, 335, {}, 0},
    /* 0x1a brewer */       {0, 329, 329, {}, 0},
    /* 0x1b powder maker */ {0, 332, 332, {}, 0},
    /* 0x1c pig farmer */   {0, 284, 284, {}, 0},
    /* 0x1d winegrower */   {0, 308, 311, {{2, 308}, {1, 314}}, 2},
    /* 0x1e bowman 2 */     {2, 89, 89, {}, 0},
    /* 0x1f pikeman 2 */    {3, 249, 249, {}, 0},
    /* 0x20 swordsman 2 */  {1, 80, 80, {}, 0},
    /* 0x21 bowman 3 */     {2, 86, 86, {}, 0},
    /* 0x22 pikeman 3 */    {3, 252, 252, {}, 0},
    /* 0x23 swordsman 3 */  {1, 77, 77, {}, 0},
    /* 0x24 mage */         {11, 347, 347, {}, 0},
    /* 0x25 */              {0, 0, 0, {}, 0},
    /* 0x26 rice farmer */  {0, 317, 320, {{1, 323}}, 1},
    /* 0x27 donkey */       {0, 341, 344, {}, 0},
    /* 0x28 pioneer */      {21, 338, 338, {}, 0},
    /* 0x29 catapult (drawn before the switch), 0x2a, 0x2b / 0x2c ships (before) */
    {0, 0, 0, {}, 0}, {0, 0, 0, {}, 0}, {0, 0, 0, {}, 0}, {0, 0, 0, {}, 0},
    /* 0x2d beekeeper */    {0, 419, 0, {{1, 437}, {2, 416}, {0x23, 434}}, 3},
    /* 0x2e mead maker */   {0, 440, 440, {}, 0},
    /* 0x2f alchemist */    {0, 422, 422, {}, 0},
};
// clang-format on

// The types whose standing frame is 72 + direction for race 3 (and the
// carrier without a good), 0x4c9405..0x4c9460.
bool StandsInOwnFrame(std::uint32_t type, std::uint32_t good) {
    switch (type) {
    case Value(T::Carrier): return good == 0;
    case Value(T::Digger): case Value(T::Builder): case Value(T::Geologist): case Value(T::Thief):
    case Value(T::Pioneer): case Value(T::Bowman1): case Value(T::Bowman2): case Value(T::Bowman3):
    case Value(T::Swordsman1): case Value(T::Swordsman2): case Value(T::Swordsman3):
    case Value(T::Pikeman1): case Value(T::Pikeman2): case Value(T::Pikeman3): case Value(T::Mage):
        return true;
    default: return false;
    }
}

// The types that get a hit area in a sequence animation (0x4d0524..0x4d0560).
bool HasSequenceHitArea(std::uint8_t type) {
    switch (type) {
    case Value(T::Swordsman1): case Value(T::Swordsman2): case Value(T::Swordsman3):
    case Value(T::Bowman1): case Value(T::Bowman2): case Value(T::Bowman3):
    case Value(T::Pikeman1): case Value(T::Pikeman2): case Value(T::Pikeman3):
    case Value(T::Geologist): case Value(T::Pioneer): case Value(T::Thief): case Value(T::Mage):
        return true;
    default: return false;
    }
}

// The frame of an image list, as GfxDword::ListImageChecked and the
// original's inlined copies: frame 0 out of range.
std::uint32_t InlineFrame(std::uint32_t list, std::int32_t frame) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(list));
    const auto* offsets = reinterpret_cast<const std::uint32_t*>(bytes + 8);
    const std::int32_t count = bytes[7];
    return list + (frame >= 0 && frame < count ? offsets[frame] : offsets[0]);
}

struct Drawing {
    CDrawer* self;
    const DrawDataView* data;
    const SettlerCell* cell;
    std::uint32_t row;      // the queue row (2 * row, +-1 when walking)
    std::int32_t x, y;
    std::uint32_t param;
    std::int8_t raise;      // the markers' extra height (race 3: -5)
    std::uint32_t owner;    // whose race and colour (the cell's player; a disguised thief: the cell owner's)
    std::int32_t frame;

    std::uint32_t Race() const { return reinterpret_cast<Arg1Fn>(kEntry204)(self, nullptr, owner); }
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
    static std::uint32_t FrameOf(std::uint32_t animation, std::uint32_t frame) {
        return ImageOf(kListImageChecked2, animation, frame);
    }
    std::uint32_t Palette(std::uint32_t player) const {
        return reinterpret_cast<Arg2Fn>(kPalettePointer)(self->graphics, nullptr,
                                                         g_playerColourIndex[player] + 0x10000u, param);
    }

    void QueueShadow(std::uint32_t queueRow, std::int32_t sx, std::int32_t sy, std::uint32_t image) const {
        reinterpret_cast<Arg4Fn>(kQueueShadow)(self, nullptr, queueRow, sx, sy, image);
    }
    void QueueSprite(std::uint32_t queueRow, std::int32_t sx, std::int32_t sy, std::uint32_t image,
                     std::uint32_t spriteParam) const {
        reinterpret_cast<Arg5Fn>(kQueueSprite)(self, nullptr, queueRow, sx, sy, image, spriteParam);
    }
    std::uint32_t QueueColourLayer(std::uint32_t queueRow, std::int32_t sx, std::int32_t sy, std::uint32_t image,
                                   std::uint32_t palette) const {
        return reinterpret_cast<Arg5Fn>(kQueueColourLayer)(self, nullptr, queueRow, sx, sy, image, palette);
    }
    void QueueSpriteWithColour(std::uint32_t queueRow, std::int32_t sx, std::int32_t sy, std::uint32_t image,
                               std::uint32_t layer) const {
        reinterpret_cast<Arg6Fn>(kQueueSpriteWithColour)(self, nullptr, queueRow, sx, sy, image, param, layer);
    }
    void AddHitArea(std::uint32_t kind, std::int32_t hx) const {
        reinterpret_cast<Arg5Fn>(kAddHitArea)(self, nullptr, kind, hx, y, cell->id, 0);
    }
    // The hit areas are only for the local player's settlers, and not in
    // UI mode 1 with a group and item chosen.
    bool GetsHitAreas() const {
        if (data->unk060 == 1 && data->unk064 != 0 && data->unk068 != 0) {
            return false;
        }
        return static_cast<std::uint32_t>(data->local_player) == owner;
    }
};

// 0x4c889c..0x4c89d0 (four copies): a walking settler's position between
// its tile and the next one in its direction (the neighbour's lift).
void ApplyWalkOffset(Drawing& d) {
    const SettlerCell* const cell = d.cell;
    constexpr std::int32_t kColumn = static_cast<std::int32_t>(kDrawCellRows);
    std::int32_t dx = 0;
    std::int32_t dl = 0;
    auto lift = [cell](std::int32_t delta) { return static_cast<std::int32_t>(cell[delta].lift); };
    const std::int32_t own = cell->lift;
    if (cell->action == 0) { dx = -16; dl = own - lift(-kColumn); }
    if (cell->action == 1) { dx = -8; dl = own - lift(-kColumn - 1) - 9; }
    if (cell->action == 2) { dx = 8; dl = own - lift(-1) - 9; }
    if (cell->action == 3) { dx = 16; dl = own - lift(kColumn); }
    if (cell->action == 4) { dx = 8; dl = own - lift(kColumn + 1) + 9; }
    if (cell->action == 5) { dx = -8; dl = own - lift(1) + 9; }
    const std::int32_t steps = cell->steps;
    const std::int32_t total = cell->step_total;
    d.x += steps * dx / total;
    d.y += steps * dl / total;
}

void LogIllegalAnimation(const Drawing& d) {
    reinterpret_cast<DebugLogFn>(kDebugLog)(texts::kIllegalAnimationLog, d.frame);
    reinterpret_cast<DebugLogFn>(kDebugLog)(texts::kSettlerTypeTodoLog, d.cell->type, d.cell->steps);
}

// The group marker (0x4c96e8..0x4c983b and copies): if the settler is in
// the shown settler group, its health bar (frame 7 - health * 8 / (max + 1))
// and the marker above it. Returns whether it is in the group.
bool QueueGroupMarker(const Drawing& d, std::int32_t raise) {
    const std::int32_t group = g_currentSettlerGroup;
    if (group == -1) {
        return false;
    }
    const SettlerGroups& groups = GlobalSettlerGroups();
    const std::uint32_t count = groups.counts[group];
    if (count == 0) {
        return false;
    }
    bool found = false;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (groups.lists[group][i] == d.cell->id) {
            found = true;
        }
    }
    if (!found) {
        return false;
    }
    std::int32_t level = static_cast<std::int32_t>(d.cell->health * 8u) / (d.cell->max_health + 1);
    if (level > static_cast<std::int32_t>(kGroupBarFrames)) {
        level = kGroupBarFrames;
    }
    const std::int32_t top = d.y + raise;
    d.QueueSprite(kRowGroupBar, d.x, top - 30,
                  InlineFrame(d.Animation(kGroupBarAnimation), static_cast<std::int32_t>(kGroupBarFrames) - level),
                  kMarkerParam);
    d.QueueSprite(kRowGroupMarker, d.x, top - 15, InlineFrame(d.Animation(kGroupMarkerAnimation), 0), kMarkerParam);
    return true;
}

// 0x4c8b16..0x4c8c08: the catapult's stones as a number above its marker,
// if the label fits into the view (the race-3 check only when walking).
void QueueNumberLabel(const Drawing& d, bool checkRace) {
    const std::int32_t labelY = d.y - 40;
    if (d.x < static_cast<std::int32_t>(g_drawOriginX + 10) || d.x >= g_displayWidth - 10 || labelY < 10 ||
        labelY >= g_displayHeight - 10) {
        return;
    }
    if (checkRace && d.Race() == kRace3) {
        return;
    }
    CDrawer& drawer = *d.self;
    drawer.number_label_top = labelY - 5;
    drawer.number_label_bottom = labelY + 5;
    drawer.number_label_left = d.x - 5;
    drawer.number_label_right = d.x + 5;
    const std::int32_t left = drawer.number_label_left;
    reinterpret_cast<Arg4Fn>(kClearTextRect)(d.self, nullptr, left, drawer.number_label_top, d.x + 5 - left,
                                             drawer.number_label_bottom - drawer.number_label_top);
    TextRect result;
    reinterpret_cast<DrawTextFFn>(kDrawTextF)(drawer.text_surface, &result, kLabelFont,
                                              reinterpret_cast<const TextRect*>(&drawer.number_label_left),
                                              kLabelFlags, texts::kFormatUnsigned, d.cell->good);
    // (the rect is read back after the drawing)
    reinterpret_cast<Arg4Fn>(kBlitTextRect)(d.self, nullptr, drawer.number_label_left, drawer.number_label_top,
                                            drawer.number_label_right - drawer.number_label_left,
                                            drawer.number_label_bottom - drawer.number_label_top);
}

// The status markers of flags 1, 2, 4, 8, 0x80 (types with a class), 0x10
// (not the thief): walking (0x4c9483..0x4c96e4) or in a sequence animation
// (0x4d0179..0x4d03cc).
void QueueStatusMarkers(const Drawing& d, bool sequence) {
    const std::uint32_t flags = d.cell->flags;
    if (flags == 0) {
        return;
    }
    const std::uint32_t type = d.cell->type;
    for (const StatusMarker& marker : kStatusMarkers) {
        if ((flags & marker.flag) == 0) {
            continue;
        }
        if (marker.flag == kFlagClassMarker && g_settlerTypeClass[type] == 0) {
            continue;
        }
        if (marker.flag == kFlagUncovered && type == Value(T::Thief)) {
            continue;
        }
        const std::uint32_t image = sequence ? Drawing::FrameOf(d.Animation(kGroupBarAnimation), marker.sequenceFrame)
                                             : Drawing::FrameOf(d.Animation(marker.animation), 0);
        const std::int32_t dy = sequence ? marker.sequenceDy : marker.dy;
        d.QueueSprite(d.row, d.x + marker.dx, d.raise + d.y + dy, image, kMarkerParam);
    }
}

// The triple at `slot` (0x4cbe54.. and the 76 other cases ending at
// 0x4cbe1e): shadow, colour layer in the owner's colour, sprite.
void QueueTriple(const Drawing& d, std::uint32_t slot) {
    const std::uint32_t frame = static_cast<std::uint32_t>(d.frame);
    d.QueueShadow(d.row, d.x, d.y, Drawing::ImageOf(kListImageChecked, d.ShadowAnimation(d.RaceAnimation(slot)), frame));
    const std::uint32_t palette = d.Palette(d.owner);
    const std::uint32_t colour = Drawing::ImageOf(kListImage, d.ColourAnimation(d.RaceAnimation(slot + 1)), frame);
    const std::uint32_t layer = d.QueueColourLayer(d.row, d.x, d.y, colour, palette);
    d.QueueSpriteWithColour(d.row, d.x, d.y, Drawing::FrameOf(d.Animation(d.RaceAnimation(slot + 2)), frame), layer);
}

// --- 1. ships (0x4d06aa..0x4d1ac1).

// A passenger or cargo place's index and whether one has the drawing order.
struct Place {
    bool found;
    std::uint32_t index;
};
Place FindPlace(const ShipPlace* places, std::uint32_t first, std::uint32_t count, std::uint32_t order,
                bool fromEnd) {
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t at = fromEnd ? first + count - i - 1 : first + i;
        if (places[at].order == order) {
            return {true, i};
        }
    }
    return {false, count};
}

// Ferry passengers with drawing orders first..first + 9 at queue row `row`.
void QueuePassengers(const Drawing& d, std::uint32_t firstOrder, std::uint32_t queueRow) {
    const std::uint32_t passengers = d.cell->cargo[0];
    const std::uint32_t frame = static_cast<std::uint32_t>(d.frame);
    for (std::uint32_t order = firstOrder; order < firstOrder + kFerrySeatsPerDirection; ++order) {
        const std::uint32_t seats = g_raceShipCapacity[d.Race()];
        const std::uint32_t base = (frame + d.Race() * kDirections) * kFerrySeatsPerDirection;
        const Place place = FindPlace(g_ferrySeats, base, seats, order, true);
        if (!place.found || passengers <= place.index) {
            continue;
        }
        const std::uint32_t passengerType = d.cell->cargo[1 + place.index];
        const std::uint32_t slot = g_ferryPassengerSlots[passengerType];
        if (slot == 0) {
            continue;
        }
        const ShipPlace& seat = g_ferrySeats[base + seats - place.index - 1];
        const std::int32_t sx = seat.dx + d.x;
        const std::int32_t sy = seat.dy + d.y;
        const std::uint32_t palette = d.Palette(d.owner);
        const std::uint32_t image = frame * 12 + kCarrierPassengerFrame;
        const std::uint32_t colour = Drawing::ImageOf(kListImage, d.ColourAnimation(d.RaceAnimation(slot + 1)), image);
        const std::uint32_t layer = d.QueueColourLayer(queueRow, sx, sy, colour, palette);
        d.QueueSpriteWithColour(queueRow, sx, sy,
                                InlineFrame(d.Animation(d.RaceAnimation(slot + 2)), static_cast<std::int32_t>(image)),
                                layer);
    }
}

// Trade ship cargo piles with drawing orders first..first + 3.
void QueueCargo(const Drawing& d, std::uint32_t firstOrder, std::uint32_t queueRow) {
    const std::uint32_t frame = static_cast<std::uint32_t>(d.frame);
    for (std::uint32_t order = firstOrder; order < firstOrder + kCargoPlacesPerDirection; ++order) {
        const std::uint32_t places = static_cast<std::uint32_t>(g_table659654[d.Race()]);
        const std::uint32_t base = (frame + d.Race() * kDirections) * kCargoPlacesPerDirection;
        const Place place = FindPlace(g_cargoPlaces, base, places, order, false);
        if (!place.found) {
            continue;
        }
        const std::uint32_t good = d.cell->cargo[2 * place.index];
        if (good == 0) {
            continue;
        }
        const std::uint32_t units = d.cell->cargo[2 * place.index + 1];
        // (the position is taken from the other end of the places)
        const ShipPlace& pile = g_cargoPlaces[base + places - place.index - 1];
        d.QueueSprite(queueRow, pile.dx + d.x, pile.dy + d.y,
                      Drawing::FrameOf(d.Animation(g_cargoAnimations[good][d.Race()]), units - 1), d.param);
    }
}

void QueueShip(Drawing& d) {
    const bool trade = d.cell->type == Value(T::TradeShip);
    const std::uint32_t slot = trade ? kTradeShipSlot : kFerrySlot;
    const std::uint32_t shadowSlot = trade ? kTradeShipShadowSlot : kFerryShadowSlot;
    const SettlerCell& cell = *d.cell;
    if (cell.action > 5) {
        reinterpret_cast<DebugLogFn>(kDebugLog)(texts::kStandsOnLog, cell.action);
        return;
    }
    d.frame = static_cast<std::int32_t>((cell.action + 4) % kDirections);
    const std::uint32_t frame = static_cast<std::uint32_t>(d.frame);
    if (cell.step >= 1 && cell.step <= 12) {
        ApplyWalkOffset(d);
    }
    QueueGroupMarker(d, d.raise);
    if (cell.ship_build != 0) {  // under construction (0x4d09a6..0x4d0b1e)
        const std::uint32_t scaled = (cell.ship_build + 10u) * 0xffffu;
        // (the original divides by an uninitialized stack value for any
        // other type; only ships get here)
        const std::uint32_t time = trade ? static_cast<std::uint32_t>(g_shipyardInputs[d.Race()][0].unkc)
                                         : static_cast<std::uint32_t>(g_shipyardInputs[d.Race()][1].unkc);
        const std::uint32_t quotient = scaled / (time * 10 + 11);
        const std::uint32_t fraction =
            quotient < kFractionKnee ? quotient >> 1 : (quotient * 0xb1dfu + 0x936fa440u) / 25535u + 20000u;
        const std::uint32_t shadow = Drawing::ImageOf(kListImageChecked, d.ShadowAnimation(d.RaceAnimation(shadowSlot)), frame);
        reinterpret_cast<Arg5Fn>(kQueueShadowPart)(d.self, nullptr, d.row, d.x, d.y, shadow, fraction);
        const std::uint32_t sprite = InlineFrame(d.Animation(d.RaceAnimation(slot + 5)), d.frame);
        reinterpret_cast<Arg6Fn>(kQueueSpritePart)(d.self, nullptr, d.row, d.x, d.y, sprite, d.param, fraction);
        return;
    }
    d.QueueShadow(d.row, d.x, d.y, Drawing::ImageOf(kListImageChecked, d.ShadowAnimation(d.RaceAnimation(shadowSlot)), frame));
    // The parts behind the hull, the passengers or cargo behind, the hull
    // with its colour, the ones in front, the front part.
    if (trade) {
        d.QueueSprite(d.row - 2, d.x, d.y, Drawing::FrameOf(d.Animation(d.RaceAnimation(slot)), frame), d.param);
        d.QueueSprite(d.row - 2, d.x, d.y, Drawing::FrameOf(d.Animation(d.RaceAnimation(slot + 1)), frame), d.param);
        QueueCargo(d, kPassengerOrdersBehind, d.row - 1);
    } else {
        d.QueueSprite(d.row - 2, d.x, d.y, InlineFrame(d.Animation(d.RaceAnimation(slot)), d.frame), d.param);
        d.QueueSprite(d.row - 2, d.x, d.y, InlineFrame(d.Animation(d.RaceAnimation(slot + 1)), d.frame), d.param);
        if (cell.cargo[0] != 0) {
            QueuePassengers(d, kPassengerOrdersBehind, d.row - 1);
        }
    }
    const std::uint32_t palette = d.Palette(d.owner);
    const std::uint32_t colour = Drawing::ImageOf(kListImage, d.ColourAnimation(d.RaceAnimation(slot + 3)), frame);
    const std::uint32_t layer = d.QueueColourLayer(d.row, d.x, d.y, colour, palette);
    const std::uint32_t hull = trade ? Drawing::FrameOf(d.Animation(d.RaceAnimation(slot + 2)), frame)
                                     : InlineFrame(d.Animation(d.RaceAnimation(slot + 2)), d.frame);
    d.QueueSpriteWithColour(d.row, d.x, d.y, hull, layer);
    if (trade) {
        QueueCargo(d, kPassengerOrdersFront, d.row + 1);
    } else if (cell.cargo[0] != 0) {
        QueuePassengers(d, kPassengerOrdersFront, d.row + 1);
    }
    d.QueueSprite(d.row + 2, d.x, d.y, Drawing::FrameOf(d.Animation(d.RaceAnimation(slot + 4)), frame), d.param);
    if (!d.GetsHitAreas()) {
        return;
    }
    const std::uint32_t kind = trade ? kHitTradeShip : kHitFerry;
    constexpr std::int32_t kShipHitAreas[5] = {0, -15, 15, -30, 30};
    for (const std::int32_t dx : kShipHitAreas) {
        d.AddHitArea(kind, d.x + dx);
    }
}

// --- 2. the catapult (0x4c8794..0x4c9212).

void QueueCatapultHitAreas(const Drawing& d, bool walking) {
    if (!d.GetsHitAreas()) {
        return;
    }
    d.AddHitArea(kHitCatapult, d.x);
    if (walking) {
        d.AddHitArea(kHitCatapult, d.x - 15);
        d.AddHitArea(kHitCatapult, d.x + 15);
    }
}

void QueueCatapult(Drawing& d) {
    const SettlerCell& cell = *d.cell;
    if (cell.action <= 5) {
        // 36 frames per direction (race 3: 12).
        const std::uint32_t perDirection = d.Race() == kRace3 ? 3 : 9;
        d.frame = static_cast<std::int32_t>(((cell.action + 4) % kDirections) * perDirection * 4);
        if (cell.step >= 1 && cell.step <= 36) {
            const bool race3 = d.Race() == kRace3;
            const std::int32_t phase = static_cast<std::int32_t>(cell.step) - cell.steps + (race3 ? 39 : 119);
            d.frame += phase % (race3 ? 12 : 36);
            if (d.frame > static_cast<std::int32_t>(kLastCatapultFrame) || d.frame < 0) {
                LogIllegalAnimation(d);
            }
            ApplyWalkOffset(d);
        }
        if (QueueGroupMarker(d, 0)) {
            QueueNumberLabel(d, true);
        }
        const std::uint32_t row = d.row + 3;
        const bool race3 = d.Race() == kRace3;
        const std::uint32_t frame = race3 ? static_cast<std::uint32_t>(d.frame)
                                          : static_cast<std::uint32_t>(d.frame / 36);
        d.QueueShadow(row, d.x, d.y,
                      Drawing::ImageOf(kListImageChecked, d.ShadowAnimation(d.RaceAnimation(kCatapultShadowSlot)), frame));
        d.QueueSprite(row, d.x, d.y, Drawing::FrameOf(d.Animation(d.RaceAnimation(kCatapultSlot)), frame), d.param);
        if (d.Race() != kRace3) {
            d.QueueSprite(row, d.x, d.y,
                          Drawing::FrameOf(d.Animation(d.RaceAnimation(kCatapultTopSlot)),
                                           static_cast<std::uint32_t>(d.frame)),
                          d.param);
        }
        QueueCatapultHitAreas(d, true);
        return;
    }
    if (cell.action < kShotFirst || cell.action > kShotLast) {
        return;
    }
    // Shooting (0x4c8e91..0x4c9212): the shot animation counts down.
    if (QueueGroupMarker(d, 0)) {
        QueueNumberLabel(d, false);
    }
    const std::uint32_t row = d.row + 3;
    const std::uint32_t animation = d.Animation(d.RaceAnimation(cell.action - kCatapultShotOffset));
    const std::uint32_t frame = static_cast<std::uint32_t>(g_raceShotTime[d.Race()]) - cell.steps - 1;
    d.QueueSprite(row, d.x, d.y, Drawing::FrameOf(animation, frame), d.param);
    const std::uint32_t shadow = d.ShadowAnimation(d.RaceAnimation(cell.action - kCatapultShotShadowOffset));
    d.QueueShadow(row, d.x, d.y, Drawing::ImageOf(kListImageChecked, shadow, frame));
    QueueCatapultHitAreas(d, false);
}

// --- 3. a sequence animation (0x4d007c..0x4d06a5).

void QueueSequence(Drawing& d) {
    const SettlerCell& cell = *d.cell;
    std::uint32_t record[5];
    const auto* source = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(
        reinterpret_cast<Arg3Fn>(kGetSequenceFrame)(d.self->graphics, nullptr, reinterpret_cast<std::uint32_t>(record),
                                                    cell.action, cell.steps)));
    std::uint32_t frame[5];
    std::memcpy(frame, source, sizeof(frame));
    auto accessor = [&frame](std::uintptr_t fn) { return reinterpret_cast<Arg0Fn>(fn)(frame, nullptr); };
    d.x += static_cast<std::int16_t>(accessor(kOffsetX));
    d.y += static_cast<std::int16_t>(accessor(kOffsetY));
    if (d.data->sound_level != -1) {
        const std::uint32_t sound = accessor(kSoundId);
        if (accessor(kSoundHighByte) <= static_cast<std::uint32_t>(d.data->sound_level) &&
            static_cast<std::uint8_t>(reinterpret_cast<Arg0Fn>(kSoundIsEnabled)(d.self->sound, nullptr)) != 0 &&
            reinterpret_cast<Arg2Fn>(kIsNearView)(d.self, nullptr, d.x, d.y) != 0 &&
            static_cast<std::uint8_t>(reinterpret_cast<Arg0Fn>(kSoundIsEnabled)(d.self->sound, nullptr)) != 0) {
            const std::uint32_t effect = reinterpret_cast<Arg2Fn>(kSoundGetEffect)(d.self->sound, nullptr, sound,
                                                                                   0xffffffffu);
            reinterpret_cast<Arg3Fn>(kEffectPlay)(reinterpret_cast<void*>(static_cast<std::uintptr_t>(effect)),
                                                  nullptr, 0, 0, 0);
        }
    }
    const std::uint32_t row = d.row + static_cast<std::uint32_t>(static_cast<std::int16_t>(accessor(kOffsetY)) / 4);
    QueueStatusMarkers(d, true);
    QueueGroupMarker(d, d.raise);
    if (HasSequenceHitArea(cell.type) && d.GetsHitAreas()) {
        d.AddHitArea(static_cast<std::uint16_t>(g_settlerTypeClass[cell.type]), d.x);
    }
    auto image = [&frame](std::uintptr_t fn) {
        std::uint32_t out = 0;
        std::uint32_t value = *reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(
            reinterpret_cast<Arg1Fn>(fn)(frame, nullptr, reinterpret_cast<std::uint32_t>(&out))));
        return value;
    };
    auto valid = [](std::uint32_t& value) { return (reinterpret_cast<Arg0Fn>(kIsValid)(&value, nullptr) & 0xff) != 0; };
    std::uint32_t shadow = image(kShadowImage);
    if (valid(shadow)) {
        d.QueueShadow(row, d.x, d.y, shadow);
    }
    const std::uint32_t palette = d.Palette(cell.player);
    std::uint32_t colour = image(kColorIndexedImage);
    std::uint32_t layer = 0;
    if (valid(colour)) {
        layer = d.QueueColourLayer(row, d.x, d.y, colour, palette);
    }
    std::uint32_t body = image(kBodyImage);
    if (valid(body)) {
        d.QueueSpriteWithColour(row, d.x, d.y, body, layer);
    }
}

// --- 4. walking or standing (0x4c9215..0x4cbe51).

// The triple slot of a type and good (0: not drawn).
std::uint32_t TripleSlot(std::uint32_t type, std::uint32_t good) {
    if (type == Value(T::Carrier)) {
        return good < 36 ? kCarrierSlots[good] : 0;
    }
    const TypeDrawing& drawing = kTypes[type];
    if (good == 0) {
        return drawing.slot;
    }
    for (std::uint8_t i = 0; i < drawing.goodCount; ++i) {
        if (drawing.goods[i].good == good) {
            return drawing.goods[i].slot;
        }
    }
    return drawing.otherGoods;
}

void QueueWalking(Drawing& d) {
    const SettlerCell& cell = *d.cell;
    const std::uint32_t type = cell.type;
    const std::uint32_t direction = (cell.action + 4) % kDirections;
    d.frame = static_cast<std::int32_t>(direction * 12);
    if (cell.step >= 1 && cell.step <= 12) {
        d.frame += (static_cast<std::int32_t>(cell.step) - cell.steps + 119) % 12;
        if (d.frame > static_cast<std::int32_t>(kLastWalkFrame) || d.frame < 0) {
            LogIllegalAnimation(d);
        }
        ApplyWalkOffset(d);
    } else {
        if (type == Value(T::Donkey)) {
            d.frame = static_cast<std::int32_t>(direction + kStandingFrame);
        }
        if (d.Race() == kRace3 && StandsInOwnFrame(type, cell.good)) {
            d.frame = static_cast<std::int32_t>(direction + kStandingFrame);
        }
    }
    QueueStatusMarkers(d, false);
    QueueGroupMarker(d, d.raise);

    std::uint32_t drawnType = type;
    if (type == Value(T::Thief)) {
        // 0x4c9848: a thief of another team is drawn as a standing carrier
        // of the cell owner (the local player on unowned land).
        if (d.data->player_team[d.owner] != d.data->player_team[d.data->local_player] && (cell.flags & kFlagUncovered) == 0) {
            drawnType = Value(T::Carrier);
            if (d.frame >= static_cast<std::int32_t>(kStandingFrame)) {
                d.frame = (d.frame * 3 - 216) * 4;
            }
            d.owner = cell.owner != 0xff ? cell.owner : static_cast<std::uint32_t>(d.data->local_player);
        }
    } else if (type >= kSettlerTypeCount) {
        return;
    }
    const std::uint32_t slot = TripleSlot(drawnType, cell.good);
    if (slot == 0) {
        return;
    }
    const std::uint32_t hitKind = kTypes[drawnType].hitKind;
    if (hitKind != 0 && d.GetsHitAreas()) {
        d.AddHitArea(hitKind, d.x);
    }
    QueueTriple(d, slot);
}

}  // namespace

using QueueSettlerFn = void(__fastcall*)(CDrawer* self, void* edx, const DrawDataView* data, std::uint32_t column,
                                         std::uint32_t row, std::int32_t x, std::int32_t y);
void __fastcall CDrawer_QueueSettler(CDrawer* self, void* edx, const DrawDataView* data, std::uint32_t column,
                                     std::uint32_t row, std::int32_t x, std::int32_t y);
S3RE_HOOK(0x004c85a0, CDrawer_QueueSettler, QueueSettlerFn, Original_CDrawer_QueueSettler, CDrawer_QueueSettler);

// Purpose: Queues the settler standing on a visible map cell for drawing
// in the frame: its body in the owner's colour (per type, carried good,
// direction and step, or a work animation with its sound), ships with
// their passengers or cargo, status and group markers, and the hit areas
// that let the player click it.
void __fastcall CDrawer_QueueSettler(CDrawer* self, void*, const DrawDataView* data, std::uint32_t column,
                                     std::uint32_t row, std::int32_t x, std::int32_t y) {
    const auto* cell = reinterpret_cast<const SettlerCell*>(reinterpret_cast<std::uintptr_t>(data) +
                                                            offsetof(DrawDataView, cells) +
                                                            (column * kDrawCellRows + row) * sizeof(SettlerCell));
    if (cell->steps > kMaxSteps) {
        reinterpret_cast<DebugLogFn>(kDebugLog)(texts::kSuspiciousTodoCountLog);
        reinterpret_cast<DebugLogFn>(kDebugLog)(texts::kSettlerTodoLog, cell->type, cell->player, cell->action,
                                                cell->steps);
    }
    Drawing d{self, data, cell, 2 * row, x, y - cell->lift, (cell->direction & kParamMask) * 4u, 0, cell->player, 0};
    if (d.Race() == kRace3) {
        d.raise = kRace3Raise;
    }
    if (cell->flags & kInTower) {  // 0x4c86f1: at its place in a tower
        const std::uint32_t place = cell->good;
        const std::uint32_t low = place & 7;
        const TowerSlotOffset& offset = g_towerSlotOffsets[low + (((place >> 3) & 3) + (place >> 5) * 3) * 12];
        d.x = offset.x - static_cast<std::int32_t>((low + 1) * 16) + x;
        d.y += offset.y;
    }
    if (cell->action == 1 || cell->action == 2) {
        --d.row;
    }
    if (cell->action == 4 || cell->action == 5) {
        ++d.row;
    }
    switch (cell->type) {
    case Value(T::TradeShip): case Value(T::Ferry): QueueShip(d); return;
    case Value(T::Catapult): QueueCatapult(d); return;
    default: break;
    }
    if (cell->action > 5) {
        QueueSequence(d);
        return;
    }
    QueueWalking(d);
}

// ---------------------------------------------------------------------------
// Tests: random cells (all types, actions, steps, goods, flags, ships'
// cargo), draw-data fields, settler groups and arguments; the animation
// getters stubbed to return a random image list (the frame lookups then run
// on it), GetSequenceFrame a random frame record (its accessors run on it),
// the queue functions, palette, hit areas, sounds and the label's text
// calls recorded. Render::Entry204 is stubbed without recording (a pure
// table read; the original calls it many more times than ours).
// ---------------------------------------------------------------------------

namespace {

struct Log {
    std::vector<std::uint32_t> values;
    std::uint8_t* list = nullptr;
    std::uint8_t* record = nullptr;
    std::uint32_t enabled = 1;
};
Log* g_textLog = nullptr;

std::uint32_t Mix(std::uint32_t v) {
    v ^= v >> 13;
    v *= 0x9e3779b1u;
    return v ^ (v >> 16);
}

// CTextSurface::DrawTextF's stand-in (cdecl, so not a CalleeStubs stub).
TextRect* __cdecl RecordDrawTextF(void* self, TextRect* result, std::int32_t font, const TextRect* rect,
                                  std::uint32_t flags, const char* format, std::uint32_t value) {
    g_textLog->values.insert(g_textLog->values.end(),
                             {20, reinterpret_cast<std::uint32_t>(self), static_cast<std::uint32_t>(font),
                              static_cast<std::uint32_t>(rect->left), static_cast<std::uint32_t>(rect->top),
                              static_cast<std::uint32_t>(rect->right), static_cast<std::uint32_t>(rect->bottom), flags,
                              reinterpret_cast<std::uint32_t>(format), value});
    return result;
}

void RunQueueSettler(QueueSettlerFn fn, CDrawer* drawer, const DrawDataView* data, std::uint32_t column,
                     std::uint32_t row, std::int32_t x, std::int32_t y, Log& log) {
    test::CalleeStubs stubs;
    stubs.SetWorld(reinterpret_cast<CGameData*>(drawer));
    g_textLog = &log;
    test::ScopedJump text(kDrawTextF, reinterpret_cast<const void*>(&RecordDrawTextF));
    const std::uint32_t list = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(log.list));
    auto relative = [list](std::uint32_t image) { return image - list; };
    auto out = [](std::uint32_t address, std::uint32_t value) {
        *reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(address)) = value;
        return address;
    };
    stubs.Install<1, kEntry204>([&](CGameData*, std::uint32_t, const std::uint32_t* a) { return Mix(a[0]) & 3; });
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
    stubs.Install<3, kGetSequenceFrame>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {7, self, a[1], a[2]});
        std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(a[0])), log.record, 20);
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
    stubs.Install<0, kSoundIsEnabled>([&](CGameData*, std::uint32_t self, const std::uint32_t*) {
        log.values.insert(log.values.end(), {15, self});
        return log.enabled;
    });
    stubs.Install<2, kIsNearView>([&](CGameData*, std::uint32_t, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {16, a[0], a[1]});
        return Mix(a[0] * 7 + a[1]) & 1;
    });
    stubs.Install<2, kSoundGetEffect>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {17, self, a[0], a[1]});
        return 0x5000u + (a[0] & 0xff);
    });
    stubs.Install<3, kEffectPlay>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {18, self, a[0], a[1], a[2]});
        return 0;
    });
    stubs.Install<4, kClearTextRect>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {19, self, a[0], a[1], a[2], a[3]});
        return 0;
    });
    stubs.Install<4, kBlitTextRect>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {21, self, a[0], a[1], a[2], a[3]});
        return 0;
    });
    fn(drawer, nullptr, data, column, row, x, y);
    log.values.insert(log.values.end(),
                      {99, static_cast<std::uint32_t>(drawer->number_label_left),
                       static_cast<std::uint32_t>(drawer->number_label_top),
                       static_cast<std::uint32_t>(drawer->number_label_right),
                       static_cast<std::uint32_t>(drawer->number_label_bottom)});
    g_textLog = nullptr;
}

// A cell's type: every type, the special ones more often.
std::uint8_t RandomType(test::Rng& rng) {
    const std::uint32_t r = rng.Below(10);
    if (r < 5) {
        return static_cast<std::uint8_t>(rng.Below(0x30));
    }
    constexpr std::uint8_t kSpecial[] = {0, 0, 0x17, 0x17, 0x29, 0x29, 0x2b, 0x2c, 0x13, 0x2d, 0x10, 0x1d, 0x26};
    if (r < 9) {
        return kSpecial[rng.Below(sizeof(kSpecial))];
    }
    return static_cast<std::uint8_t>(rng.Next32());
}

void RandomCell(test::Rng& rng, SettlerCell& cell, std::uint32_t player) {
    rng.Fill(&cell, sizeof(cell));
    cell.type = RandomType(rng);
    cell.player = static_cast<std::uint8_t>(player);
    const std::uint32_t r = rng.Below(10);
    if (r < 6) {
        cell.action = rng.Below(6);
    } else if (r < 8) {
        cell.action = cell.type == Value(T::Catapult) ? kShotFirst + rng.Below(7) : 6 + rng.Below(1000);
    } else {
        cell.action = r < 9 ? rng.Below(16) : rng.Next32();
    }
    const std::uint32_t s = rng.Below(4);
    if (s == 0) {
        cell.step = 0;
    } else if (s == 1) {
        cell.step = static_cast<std::uint8_t>(1 + rng.Below(12));
    } else if (s == 2) {
        cell.step = static_cast<std::uint8_t>(rng.Below(40));
    }
    cell.steps = static_cast<std::uint16_t>(rng.Chance(1, 20) ? 50000 + rng.Below(15536) : rng.Below(80));
    if (cell.step_total == 0) {
        cell.step_total = 1;
    }
    if (rng.Chance(3, 4)) {
        cell.good = static_cast<std::uint8_t>(rng.Chance(1, 2) ? rng.Below(3) : rng.Below(0x24));
    }
    if (rng.Chance(1, 3)) {
        cell.flags &= static_cast<std::uint8_t>(~kInTower);
    }
    if (rng.Chance(1, 4)) {
        cell.flags = 0;
    }
    if (rng.Chance(1, 3)) {
        cell.owner = 0xff;
    }
    if (rng.Chance(1, 2)) {
        cell.ship_build = 0;
    }
    if (rng.Chance(1, 2)) {
        cell.cargo[0] = static_cast<std::uint8_t>(rng.Below(8));
        for (std::uint32_t i = 1; i < 8; ++i) {
            cell.cargo[i] = static_cast<std::uint8_t>(rng.Below(48));
        }
    }
    if (cell.type == Value(T::TradeShip) && rng.Chance(1, 2)) {
        for (std::uint32_t i = 0; i < 4; ++i) {
            cell.cargo[2 * i] = static_cast<std::uint8_t>(rng.Chance(1, 3) ? 0 : rng.Below(35));
            cell.cargo[2 * i + 1] = static_cast<std::uint8_t>(rng.Below(9));
        }
    }
}

void CompareQueueSettler(test::Context& ctx, QueueSettlerFn candidate, int cases) {
    std::vector<std::uint8_t> dataBytes(sizeof(DrawDataView));
    std::vector<std::uint8_t> drawerBytes(sizeof(CDrawer));
    std::uint8_t list[8 + 4 * 256];
    std::uint8_t record[20];
    auto* const data = reinterpret_cast<DrawDataView*>(dataBytes.data());
    auto* const drawer = reinterpret_cast<CDrawer*>(drawerBytes.data());
    SettlerGroups& groups = GlobalSettlerGroups();
    const SettlerGroups savedGroups = groups;
    const std::int32_t savedGroup = g_currentSettlerGroup;
    const std::uint32_t savedOrigin = g_drawOriginX;
    const std::int32_t savedWidth = g_displayWidth;
    const std::int32_t savedHeight = g_displayHeight;
    g_drawOriginX = 0;
    g_displayWidth = 800;
    g_displayHeight = 600;
    for (int i = 0; i < cases; ++i) {
        test::Rng& rng = ctx.rng();
        if (i % 200 == 0) {
            rng.Fill(dataBytes.data(), dataBytes.size());
            rng.Fill(drawerBytes.data(), drawerBytes.size());
        }
        const std::uint32_t column = 1 + rng.Below(kDrawCellRows - 2);
        const std::uint32_t row = 1 + rng.Below(kDrawCellRows - 2);
        const std::uint32_t player = rng.Below(20);
        auto& cell = *reinterpret_cast<SettlerCell*>(&data->cells[column][row]);
        RandomCell(rng, cell, player);
        data->unk060 = rng.Chance(1, 2) ? 1 : static_cast<std::int32_t>(rng.Below(3));
        data->unk064 = static_cast<std::int32_t>(rng.Below(2));
        data->unk068 = static_cast<std::int32_t>(rng.Below(2));
        data->local_player =
            static_cast<std::int32_t>(rng.Chance(1, 2) ? player : rng.Below(20));
        for (std::int32_t& team : data->player_team) {
            team = static_cast<std::int32_t>(rng.Below(3));
        }
        data->sound_level = rng.Chance(1, 4) ? -1 : static_cast<std::int32_t>(rng.Below(300));
        g_currentSettlerGroup = rng.Chance(1, 4) ? -1 : static_cast<std::int32_t>(rng.Below(10));
        for (std::uint32_t g = 0; g < SettlerGroups::kGroups; ++g) {
            groups.counts[g] = static_cast<std::uint16_t>(rng.Chance(1, 4) ? 0 : rng.Below(SettlerGroups::kWords));
            for (std::uint16_t& id : groups.lists[g]) {
                id = static_cast<std::uint16_t>(rng.Chance(1, 30) ? cell.id : rng.Next32());
            }
        }
        rng.Fill(list, sizeof(list));
        list[7] = static_cast<std::uint8_t>(rng.Chance(1, 4) ? rng.Below(8) : rng.Below(256));
        for (std::uint32_t k = 0; k < 256; ++k) {
            const std::uint32_t offset = rng.Below(0x10000);
            std::memcpy(list + 8 + 4 * k, &offset, 4);
        }
        rng.Fill(record, sizeof(record));
        for (std::uint32_t k = 1; k < 4; ++k) {
            if (rng.Chance(1, 4)) {
                std::memset(record + 4 * k, 0, 4);
            }
        }
        if (rng.Chance(1, 2)) {
            record[19] = static_cast<std::uint8_t>(rng.Below(256));
        }
        const std::int32_t x = static_cast<std::int32_t>(rng.Chance(1, 8) ? rng.Next32() : rng.Below(900)) - 50;
        const std::int32_t y = static_cast<std::int32_t>(rng.Chance(1, 8) ? rng.Next32() : rng.Below(700)) - 20;
        Log a, b;
        a.list = b.list = list;
        a.record = b.record = record;
        a.enabled = b.enabled = rng.Chance(3, 4) ? 1 : 0;
        RunQueueSettler(Original_CDrawer_QueueSettler, drawer, data, column, row, x, y, a);
        RunQueueSettler(candidate, drawer, data, column, row, x, y, b);
        if (!S3RE_EXPECT(a.values == b.values)) {
            std::size_t k = 0;
            while (k < a.values.size() && k < b.values.size() && a.values[k] == b.values[k]) {
                ++k;
            }
            ctx.Fail(__FILE__, __LINE__,
                     "  case %d, type 0x%x, action 0x%x, step %u, good %u, flags 0x%x, sizes %zu / %zu, first "
                     "difference %zu (%u / %u)",
                     i, cell.type, cell.action, cell.step, cell.good, cell.flags, a.values.size(), b.values.size(), k,
                     k < a.values.size() ? a.values[k] : 0, k < b.values.size() ? b.values[k] : 0);
            break;
        }
    }
    groups = savedGroups;
    g_currentSettlerGroup = savedGroup;
    g_drawOriginX = savedOrigin;
    g_displayWidth = savedWidth;
    g_displayHeight = savedHeight;
}

// Wrong: one pixel lower.
void __fastcall WrongQueueSettler(CDrawer* self, void* edx, const DrawDataView* data, std::uint32_t column,
                                  std::uint32_t row, std::int32_t x, std::int32_t y) {
    CDrawer_QueueSettler(self, edx, data, column, row, x, y + 1);
}

}  // namespace

S3RE_TEST(CDrawer_QueueSettler_MatchesOriginal) {
    CompareQueueSettler(ctx, &CDrawer_QueueSettler, 60000);
}

S3RE_TEST(CDrawer_QueueSettler_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareQueueSettler(probe, &WrongQueueSettler, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
