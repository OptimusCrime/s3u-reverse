// Queueing the landscape objects of the draw data's cells: trees, stones,
// fields, markers, the water animation, the selection and cell markers
// (the unnamed object file 0x4d3050..0x4d65e0 between Volker\DrawBuilding.cpp
// and GameDataSave.cpp; called three times by the frame drawer 0x4b26c0).
//
// CDrawer::QueueLandscapeObject (0x004d3050; was Logic_sub_4d3050):
//   (DrawData* data, column, row, x, y, object) thiscall, ret 0x18. The
//   cell is data->cells[column][row] (draw_data_layout.h); `object` is the
//   cell's object byte (0xfe for an object 0 with direction bit 0x40 and
//   bits 0xf0 of +0x2), the low byte of an object seen under the fog, or 0.
//   Every sprite is queued at (x, y - cell.lift) with the parameter
//   param = 4 * (cell.direction & 0x3f); the screen row of the queues is
//   2 * row unless said otherwise. "Shadow s, sprite a, frame f" below
//   means: CGraphicsData::ShadowAnimationPointer(s) 0x5fabb0, its image f
//   by GfxDword::ListImageChecked 0x5fcce0, CDrawer::QueueShadow 0x4d7c90;
//   then CGraphicsData::AnimationPointer(a) 0x5faaf0 and its frame f
//   (inlined: frame 0 when f is out of range), CDrawer::QueueSprite
//   0x4d7e10. The phase p(k) = (data->clock >> k) + origin_row +
//   origin_column + column + row (32-bit).
//   - 0xff: the race (Render::Entry204 0x4c7ab0 of the owner byte, called
//     before each of the three lookups) picks g_raceAnimations slots 12..14:
//     shadow frame p(1) % 12 at row `row` (not doubled), a colour layer
//     (CGraphicsData::ColorIndexedAnimationPointer 0x5fab50, image by
//     GfxDword::ListImage 0x5fc5a0, palette CGraphicsData::PalettePointer
//     0x5fac10(g_playerColourIndex[owner] + 0x10000, param),
//     CDrawer::QueueColourLayer 0x4d8170) and the sprite with that layer
//     (CDrawer::QueueSpriteWithColour 0x4d8050), both at row 2 * row - 1.
//   - 0xfe: the same with slots 15..17, frame 0, all at row 2 * row.
//   - trees 0x44..0x51: g_treeAnimations, frame p(3) & 31 mirrored above 16.
//   - 0x54, 0x70, 0x71, 0xe8..0xf2: fixed ids, frame 0 or 1 (table below);
//     0x55..0x5d, 0x5e..0x66, 0x67..0x6f: fields, ids 0x10017 / 0x10019 /
//     0x10018 with frames from tables; 0x72: only a sprite (0x10070) at row 2.
//   - 0x80..0xe7: g_growingTreeAnimations[object & 7], frame
//     (object - 0x80) / 8; objects 0xb8..0xcf queue the sprite at row
//     2 * row - 15.
//   - 0xf3..0xf6: sprite 0xd003b frame p(2) % 24, no shadow; frame 0 plays
//     sound 0x5c when Render::IsNearView 0x4c7b50 and CSoundData::IsEnabled.
//   - 0xf7..0xfd: shadow 0x6000b frame 0, sprite g_objectF7Animations
//     frame p(3) & 31; from 0xf8, when the drawer counted ambient sources of
//     kind 16 and rand() > 30500, sound 0x75 at
//     CDrawer::AmbientSoundVolume(16, 1, 100, -800) /
//     AmbientSoundPan(16).
//   - stones 0x73..0x7f, 0x23..0x43, 1..0x14, 0x15..0x1c, 0x1d..0x22:
//     one shadow/sprite id pair each, frame object - first (1..0x14: 36 -
//     object for 13..16 when cell.unk02 <= 7).
//   - 0: the water animations: with cell.unk02 == 0x50 one of six time
//     windows of a hash of the map position, lift and p-clock
//     (WaterWindow below, ids 0x1006a..0x1006c, sound 0x48); then with
//     unk02 2..7 and no animation word, a window of a second hash per level
//     (unk0c) and sprite 0x60007 at row 2 * row - 10 (sound 0x4f).
//   Then the tail: in UI mode 4 (data->unk060) a marker (0x1005b, frame
//   marker - 1) at row 3; in mode 1 with a group/item (+0x64/+0x68) and a
//   marker 1..0x20: the selection sprites (on the selected cell or for
//   markers above 0x10; g_raceAnimations slot from the group/item table)
//   and the cell marker (0x40006 frame marker - 1 (mod 16), 0x10086 /
//   0x10087 for 9 / 10) at row 0xf9, and CDrawer::AddHitArea 0x4d8240(0xff,
//   x, y, map column, map row).
//   Quirks kept: marker values 11..16 (and 27..32) queue the cell marker
//   with whatever sprite was queued last in this call (0 when none: the
//   original's local keeps it); the hit area's column/row are words.

#include "game/global_access.h"
#include "game/globals.h"
#include "game/graphics/draw_data_layout.h"
#include "game/graphics/drawer_layout.h"
#include "game/tables.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kGfxDwordConstruct = 0x005fc540;
constexpr std::uintptr_t kEntry204 = 0x004c7ab0;
constexpr std::uintptr_t kShadowAnimationPointer = 0x005fabb0;
constexpr std::uintptr_t kColorIndexedAnimationPointer = 0x005fab50;
constexpr std::uintptr_t kAnimationPointer = 0x005faaf0;
constexpr std::uintptr_t kPalettePointer = 0x005fac10;
constexpr std::uintptr_t kListImageChecked = 0x005fcce0;
constexpr std::uintptr_t kListImage = 0x005fc5a0;
constexpr std::uintptr_t kQueueShadow = 0x004d7c90;
constexpr std::uintptr_t kQueueSprite = 0x004d7e10;
constexpr std::uintptr_t kQueueColourLayer = 0x004d8170;
constexpr std::uintptr_t kQueueSpriteWithColour = 0x004d8050;
constexpr std::uintptr_t kAddHitArea = 0x004d8240;
constexpr std::uintptr_t kIsNearView = 0x004c7b50;
constexpr std::uintptr_t kAmbientSoundVolume = 0x004c7a50;
constexpr std::uintptr_t kAmbientSoundPan = 0x004c79f0;
constexpr std::uintptr_t kSoundIsEnabled = 0x005ff4d0;
constexpr std::uintptr_t kSoundGetEffect = 0x005ff4f0;
constexpr std::uintptr_t kSoundEffectPlay = 0x005c2fc0;
constexpr std::uintptr_t kRand = 0x0061f802;  // the CRT's rand

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
using SoundEnabledFn = bool(__fastcall*)(void* self, void* edx);
using RandFn = int(__cdecl*)();

constexpr std::uint32_t kNoParamMask = 0x3f;

// Ids of the sound effects (CSoundData::GetEffect).
constexpr std::uint32_t kSoundWaterA = 0x48;
constexpr std::uint32_t kSoundWaterB = 0x4f;
constexpr std::uint32_t kSoundObjectF3 = 0x5c;
constexpr std::uint32_t kSoundObjectF8 = 0x75;
constexpr std::uint32_t kAnyVariant = 0xffffffffu;
constexpr std::int32_t kAmbientKind = 16;  // the ambient sound kind of objects 0xf8..0xfd
constexpr int kAmbientChance = 30500;      // rand() above this plays it (of 32767)

// g_raceAnimations slots.
constexpr std::uint32_t kMarkerSlot = 12;   // 12..14: object 0xff
constexpr std::uint32_t kMarker2Slot = 15;  // 15..17: object 0xfe

// Draw queue rows used by the markers.
constexpr std::uint32_t kRowObjectMarker = 3;
constexpr std::uint32_t kRowSelected = 0xf8;
constexpr std::uint32_t kRowCellMarker = 0xf9;
constexpr std::uint32_t kParamSelected = 0xff;
constexpr std::uint32_t kParamSelection = 0x80;

// One call's state: the drawer, the cell and its screen position, and the
// last sprite queued (read by the cell marker for unknown marker values).
struct Drawing {
    CDrawer* self;
    const DrawDataView* data;
    const DrawCell* cell;
    std::uint32_t column, row, x, y;
    std::uint32_t param;
    std::uint32_t lastSprite;

    std::uint32_t ScreenY() const { return y - cell->lift; }
    std::uint32_t Row2() const { return 2 * row; }

    std::uint32_t Phase(int shift) const {
        return (data->clock >> shift) + data->origin_row + data->origin_column + column + row;
    }

    // --- callees (through their S3.EXE addresses) ---
    std::uint32_t Race(std::uint32_t owner) const {
        return reinterpret_cast<Arg1Fn>(kEntry204)(self, nullptr, owner);
    }
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
    // The frame's image of an animation, inlined in the original: frame 0
    // when out of range.
    static std::uint32_t FrameOf(std::uint32_t animation, std::int32_t frame) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(animation));
        std::uint32_t offset;
        if (frame >= 0 && frame < static_cast<std::int32_t>(bytes[7])) {
            std::memcpy(&offset, bytes + 8 + 4 * frame, sizeof(offset));
        } else {
            std::memcpy(&offset, bytes + 8, sizeof(offset));
        }
        return offset + animation;
    }

    void QueueShadow(std::uint32_t queueRow, std::uint32_t image) const {
        reinterpret_cast<Arg4Fn>(kQueueShadow)(self, nullptr, queueRow, x, ScreenY(), image);
    }
    void QueueSprite(std::uint32_t queueRow, std::uint32_t image, std::uint32_t parameter) {
        lastSprite = image;
        reinterpret_cast<Arg5Fn>(kQueueSprite)(self, nullptr, queueRow, x, ScreenY(), image, parameter);
    }
    bool SoundEnabled() const { return reinterpret_cast<SoundEnabledFn>(kSoundIsEnabled)(self->sound_data, nullptr); }
    bool NearView() const { return reinterpret_cast<Arg2Fn>(kIsNearView)(self, nullptr, x, ScreenY()) != 0; }
    void PlaySound(std::uint32_t id, std::uint32_t volume, std::uint32_t pan) const {
        void* const effect = reinterpret_cast<void*>(static_cast<std::uintptr_t>(
            reinterpret_cast<Arg2Fn>(kSoundGetEffect)(self->sound_data, nullptr, id, kAnyVariant)));
        reinterpret_cast<Arg3Fn>(kSoundEffectPlay)(effect, nullptr, volume, pan, 0);
    }

    // The common shape: shadow s (image `shadowFrame`), then sprite a
    // (frame `frame`), both at row 2 * row.
    void ShadowAndSprite(std::uint32_t shadowId, std::int32_t shadowFrame, std::uint32_t spriteId,
                         std::int32_t frame) {
        const std::uint32_t shadow =
            ImageOf(kListImageChecked, ShadowAnimation(shadowId), static_cast<std::uint32_t>(shadowFrame));
        QueueShadow(Row2(), shadow);
        QueueSprite(Row2(), FrameOf(Animation(spriteId), frame), param);
    }
};

// --- 0xff / 0xfe (0x4d30d8..0x4d3408): a race's marker with its player colour.
void QueueRaceMarker(Drawing& d, std::uint32_t slot, std::uint32_t frame, std::uint32_t shadowRow,
                     std::uint32_t row) {
    const std::uint32_t owner = d.cell->owner;
    const std::uint32_t shadowAnimation = d.ShadowAnimation(g_raceAnimations[slot][d.Race(owner)]);
    d.QueueShadow(shadowRow, Drawing::ImageOf(kListImageChecked, shadowAnimation, frame));
    const std::uint32_t palette = reinterpret_cast<Arg2Fn>(kPalettePointer)(
        d.self->graphics, nullptr, g_playerColourIndex[owner] + 0x10000u, d.param);
    const std::uint32_t colourAnimation = d.ColourAnimation(g_raceAnimations[slot + 1][d.Race(owner)]);
    const std::uint32_t colourImage = Drawing::ImageOf(kListImage, colourAnimation, frame);
    const std::uint32_t layer =
        reinterpret_cast<Arg5Fn>(kQueueColourLayer)(d.self, nullptr, row, d.x, d.ScreenY(), colourImage, palette);
    const std::uint32_t animation = d.Animation(g_raceAnimations[slot + 2][d.Race(owner)]);
    const std::uint32_t sprite = Drawing::FrameOf(animation, static_cast<std::int32_t>(frame));
    d.lastSprite = sprite;
    reinterpret_cast<Arg6Fn>(kQueueSpriteWithColour)(d.self, nullptr, row, d.x, d.ScreenY(), sprite, d.param, layer);
}

// --- object 0 (0x4d4829..0x4d52cc): the water animations.
// Six time windows of the 0x50 animation: window start, which of the two
// lengths (12 or 14 frames) it has.
constexpr std::int32_t kWaterWindowLengths[2] = {12, 14};
constexpr std::int32_t kWaterWindowStarts[6] = {0, 2000, 3100, 8000, 11500, 13000};
constexpr std::int32_t kWaterWindowKinds[6] = {0, 1, 1, 0, 1, 1};
constexpr std::uint8_t kWaterTerrainA = 0x50;
constexpr std::int32_t kWaterParamMin = 0x48;  // only drawn when param > 72

void QueueWaterWindow(Drawing& d) {
    const DrawDataView& data = *d.data;
    const std::uint32_t mapColumn = static_cast<std::uint32_t>(data.origin_column) + d.column;
    const std::uint32_t mapRow = static_cast<std::uint32_t>(data.origin_row) + d.row;
    const std::int32_t hash = static_cast<std::int32_t>(
        (((mapColumn * 0x521u) ^ (0u - mapRow * 0x23bu) ^ (d.cell->lift * 0x1677u)) + (data.clock >> 2)) & 0x3fff);
    for (int i = 0; i < 6; ++i) {
        const std::int32_t start = kWaterWindowStarts[i];
        if (hash < start || hash >= kWaterWindowLengths[kWaterWindowKinds[i]] + start) {
            continue;
        }
        const std::int32_t offset = hash - start;
        if (offset < 6) {
            d.QueueSprite(d.Row2(), Drawing::FrameOf(d.Animation(0x1006a), offset), d.param);
        } else if (kWaterWindowKinds[i] == 0) {
            d.QueueSprite(d.Row2(), Drawing::FrameOf(d.Animation(0x1006b), offset - 6), d.param);
        } else {
            d.QueueSprite(d.Row2(), Drawing::FrameOf(d.Animation(0x1006c), offset - 6), d.param);
            if (offset - 6 == 0 && d.SoundEnabled() && d.NearView() && d.SoundEnabled()) {
                d.PlaySound(kSoundWaterA, 0, 0);
            }
        }
        return;
    }
}

// The second water animation: per level (0..7, from the cell's +0xc) up to
// 20 windows of 15 frames; each window has a frame block (0..3).
constexpr std::int32_t kWaveWindowCounts[8] = {20, 18, 15, 10, 6, 3, 0, 0};
constexpr std::int32_t kWaveWindowBlocks[20] = {0, 1, 3, 2, 1, 0, 0, 1, 3, 2, 3, 2, 1, 0, 3, 2, 1, 3, 2, 0};
constexpr std::int32_t kWaveWindowStarts[8][20] = {
    {0, 5000, 13000, 18000, 26000, 35000, 41000, 50000, 60000, 61000, 64000, 80000, 84000, 90000, 95000, 100000,
     105000, 113000, 125000, 131000},
    {0, 6000, 15000, 18000, 30000, 37000, 45000, 55000, 70000, 74000, 81000, 85000, 90000, 95000, 105000, 106000,
     112000, 115000, 0, 0},
    {0, 7000, 16000, 18000, 33000, 40000, 46000, 55000, 65000, 75000, 85000, 90000, 100000, 115000, 125000},
    {0, 14000, 26000, 33000, 41000, 60000, 72000, 84000, 87000, 96000, 105000, 125000},
    {0, 25000, 50000, 75000, 100000, 125000},
    {0, 50000, 100000},
    {},
    {},
};
constexpr std::int32_t kWaveWindowFrames = 15;
constexpr std::uint8_t kWaveTerrainMin = 2;
constexpr std::uint8_t kWaveTerrainMax = 7;

void QueueWave(Drawing& d) {
    const DrawDataView& data = *d.data;
    const std::uint32_t mapColumn = static_cast<std::uint32_t>(data.origin_column) + d.column;
    const std::uint32_t mapRow = static_cast<std::uint32_t>(data.origin_row) + d.row;
    const std::int32_t hash = static_cast<std::int32_t>(
        (((mapColumn * 0x4521u) ^ (0u - mapRow * 0x23bu)) + (data.clock >> 2)) & 0x1ffff);
    const std::int32_t level = (15 - (d.cell->unk0c & 15)) >> 1;
    const std::int32_t count = kWaveWindowCounts[level];
    std::int32_t window = 0;
    while (window < count) {
        const std::int32_t start = kWaveWindowStarts[level][window];
        if (hash >= start && hash < start + kWaveWindowFrames) {
            break;
        }
        ++window;
    }
    if (window >= count) {
        return;
    }
    const std::int32_t offset = hash - kWaveWindowStarts[level][window];
    const std::uint32_t animation = d.Animation(0x60007);
    d.QueueSprite(d.Row2() - 10, Drawing::FrameOf(animation, kWaveWindowBlocks[window] * kWaveWindowFrames + offset),
                  d.param);
    if (offset == 10 && d.SoundEnabled() && d.NearView() && d.SoundEnabled()) {
        d.PlaySound(kSoundWaterB, 0, 0);
    }
}

void QueueWater(Drawing& d) {
    const std::int32_t param = static_cast<std::int32_t>(d.param);
    if (d.cell->unk02 == kWaterTerrainA && param > kWaterParamMin) {
        QueueWaterWindow(d);
    }
    if (d.cell->unk02 >= kWaveTerrainMin && d.cell->unk02 <= kWaveTerrainMax && d.cell->animation == 0 &&
        param > kWaterParamMin) {
        QueueWave(d);
    }
}

// --- objects with one shadow and one sprite of fixed ids (0x54, 0x70,
// 0x71, 0xe8..0xf2): shadow id, its image, sprite id, its frame.
struct FixedObject {
    std::uint32_t object;
    std::uint32_t shadow;
    std::uint32_t shadowFrame;
    std::uint32_t sprite;
    std::int32_t frame;
};
constexpr FixedObject kFixedObjects[] = {
    {0x54, 0x10016, 0, 0x10016, 0}, {0x70, 0x1005b, 0, 0x1005d, 0}, {0x71, 0x1005a, 0, 0x1005c, 0},
    {0xe8, 0x10061, 0, 0x10063, 0}, {0xe9, 0x1005c, 0, 0x1005e, 0}, {0xea, 0x1005c, 1, 0x1005e, 1},
    {0xeb, 0x1005f, 0, 0x10061, 0}, {0xec, 0x1005f, 1, 0x10061, 1}, {0xed, 0x1005e, 0, 0x10060, 0},
    {0xee, 0x1005e, 1, 0x10060, 1}, {0xef, 0x1005d, 0, 0x1005f, 0}, {0xf0, 0x1005d, 1, 0x1005f, 1},
    {0xf1, 0x10060, 0, 0x10062, 0}, {0xf2, 0x10060, 1, 0x10062, 1},
};

// Objects whose frame is object - first (stones, ...): first, last, shadow
// id, sprite id.
struct ObjectRange {
    std::uint32_t first, last;
    std::uint32_t shadow, sprite;
};
constexpr ObjectRange kStoneObjects = {0x73, 0x7f, 0x1001e, 0x1001f};
constexpr ObjectRange kRangeObjects[] = {
    {0x23, 0x43, 0x1001a, 0x1001b},
    {0x01, 0x14, 0x1001c, 0x1001d},  // (frame mirrored for 13..16 on unk02 <= 7)
    {0x15, 0x1c, 0x1001d, 0x1001e},
    {0x1d, 0x22, 0x1001b, 0x1001c},
};

// Fields: the frame per object.
constexpr std::int32_t kGrainFrames[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};        // 0x55..0x5d, id 0x10017
constexpr std::int32_t kVineFrames[9] = {0, 0, 1, 1, 2, 2, 3, 3, 0};         // 0x5e..0x66, id 0x10019
constexpr std::int32_t kRiceFrames[9] = {0, 1, 1, 2, 3, 3, 4, 4, 5};         // 0x67..0x6f, id 0x10018

bool In(std::uint32_t object, std::uint32_t first, std::uint32_t last) {
    return static_cast<std::int32_t>(object) >= static_cast<std::int32_t>(first) &&
           static_cast<std::int32_t>(object) <= static_cast<std::int32_t>(last);
}

// The object itself (0x4d30c9..0x4d52cc).
void QueueObject(Drawing& d, std::uint32_t object) {
    if (object == 0xff) {
        QueueRaceMarker(d, kMarkerSlot, d.Phase(1) % 12, d.row, d.Row2() - 1);
        return;
    }
    if (object == 0xfe) {
        QueueRaceMarker(d, kMarker2Slot, 0, d.Row2(), d.Row2());
        return;
    }
    for (const FixedObject& fixed : kFixedObjects) {
        if (object == fixed.object) {
            d.ShadowAndSprite(fixed.shadow, static_cast<std::int32_t>(fixed.shadowFrame), fixed.sprite, fixed.frame);
            return;
        }
    }
    if (object == 0x72) {
        d.QueueSprite(2, Drawing::FrameOf(d.Animation(0x10070), 0), d.param);
        return;
    }
    if (In(object, 0x44, 0x51)) {  // trees (0x4d340d)
        std::int32_t frame = static_cast<std::int32_t>(d.Phase(3) & 31);
        if (frame >= 17) {
            frame = 32 - frame;
        }
        const auto& ids = g_treeAnimations[object - 0x44];
        d.ShadowAndSprite(ids[1], frame, ids[0], frame);
    } else if (In(object, 0x55, 0x5d)) {
        d.ShadowAndSprite(0x10017, kGrainFrames[object - 0x55], 0x10017, kGrainFrames[object - 0x55]);
    } else if (In(object, 0x5e, 0x66)) {
        d.ShadowAndSprite(0x10019, kVineFrames[object - 0x5e], 0x10019, kVineFrames[object - 0x5e]);
    } else if (In(object, 0x67, 0x6f)) {
        d.ShadowAndSprite(0x10018, kRiceFrames[object - 0x67], 0x10018, kRiceFrames[object - 0x67]);
    } else if (In(object, 0x80, 0xe7)) {  // 0x4d3a87
        const std::int32_t frame = static_cast<std::int32_t>(object - 0x80) / 8;
        const auto& ids = g_growingTreeAnimations[object & 7];
        const std::uint32_t shadow =
            Drawing::ImageOf(kListImageChecked, d.ShadowAnimation(ids[1]), static_cast<std::uint32_t>(frame));
        d.QueueShadow(d.Row2(), shadow);
        const std::uint32_t sprite = Drawing::FrameOf(d.Animation(ids[0]), frame);
        d.QueueSprite(In(object, 0xb8, 0xcf) ? d.Row2() - 15 : d.Row2(), sprite, d.param);
    } else if (In(object, 0xf3, 0xf6)) {  // 0x4d4119
        const std::uint32_t frame = d.Phase(2) % 24;
        d.QueueSprite(d.Row2(), Drawing::FrameOf(d.Animation(0xd003b), static_cast<std::int32_t>(frame)), d.param);
        if (frame == 0 && d.NearView() && d.SoundEnabled()) {
            d.PlaySound(kSoundObjectF3, 0, 0);
        }
    } else if (In(object, 0xf7, 0xfd)) {  // 0x4d421e
        const std::int32_t frame = static_cast<std::int32_t>(d.Phase(3) & 31);
        d.QueueShadow(d.Row2(), Drawing::ImageOf(kListImageChecked, d.ShadowAnimation(0x6000b), 0));
        d.QueueSprite(d.Row2(), Drawing::FrameOf(d.Animation(g_objectF7Animations[object - 0xf7]), frame), d.param);
        if (object >= 0xf8 && d.self->ambient_count[kAmbientKind] > 0 &&
            reinterpret_cast<RandFn>(kRand)() > kAmbientChance) {
            const std::uint32_t volume =
                reinterpret_cast<Arg4Fn>(kAmbientSoundVolume)(d.self, nullptr, kAmbientKind, 1, 100, 0xfffffce0u);
            const std::uint32_t pan = reinterpret_cast<Arg1Fn>(kAmbientSoundPan)(d.self, nullptr, kAmbientKind);
            if (d.SoundEnabled()) {
                d.PlaySound(kSoundObjectF8, volume, pan);
            }
        }
    } else if (In(object, kStoneObjects.first, kStoneObjects.last)) {  // 0x4d439a
        const std::int32_t frame = static_cast<std::int32_t>(object - kStoneObjects.first);
        d.ShadowAndSprite(kStoneObjects.shadow, frame, kStoneObjects.sprite, frame);
    } else {
        for (const ObjectRange& range : kRangeObjects) {
            if (!In(object, range.first, range.last)) {
                continue;
            }
            std::int32_t frame = static_cast<std::int32_t>(object - range.first);
            if (range.first == 0x01 && In(object, 13, 16) && d.cell->unk02 <= 7) {
                frame = 36 - static_cast<std::int32_t>(object);
            }
            d.ShadowAndSprite(range.shadow, frame, range.sprite, frame);
            return;
        }
        if (object == 0) {
            QueueWater(d);
        }
    }
}

// --- the tail (0x4d52cc..0x4d5a42).
constexpr std::int32_t kModePlacing = 4;
constexpr std::int32_t kModeSelection = 1;
constexpr std::int32_t kMarkerTypes[] = {0x22, 0x21, 2, 3, 0x1e, 5, 0xe, 0x19, 0x24, 0x18, 0x33};
constexpr std::uint8_t kMarkerMax = 0x20;
constexpr std::uint8_t kMarkerAlways = 0x11;  // from here on drawn on every cell, not only the selected one

// The g_raceAnimations slot and frame count of the selection sprites.
struct SelectionSprite {
    std::uint32_t slot;
    std::uint32_t frames;
};

SelectionSprite SelectionSpriteOf(const DrawDataView& data) {
    const std::int32_t group = data.unk064;
    const std::int32_t item = data.unk068;
    const std::int32_t variant = data.unk094;
    // Per variant 0..3; 0 keeps the slot chosen so far.
    auto byVariant = [variant](std::uint32_t current, std::uint32_t v0, std::uint32_t v1, std::uint32_t v2,
                               std::uint32_t v3) {
        const std::uint32_t values[4] = {v0, v1, v2, v3};
        if (variant >= 0 && variant <= 3 && values[variant] != 0) {
            return values[variant];
        }
        return current;
    };
    SelectionSprite s{3, 1};
    if (group == 1) {
        switch (item) {
        case 2: s.slot = 7; break;
        case 3: s.slot = 5; break;
        case 4: s.slot = 9; break;
        case 5: s.slot = 0x3b; break;
        case 6: s.slot = 0x3f; break;
        case 7: s.slot = 0x39; break;
        case 8: s.slot = 0x3d; break;
        case 9: s.slot = 0x37; break;
        case 10: s.slot = 0x41; break;
        case 11: s.slot = byVariant(s.slot, 0x82, 0x90, 0x76, 0x76); break;
        case 12: s.slot = byVariant(s.slot, 0, 0, 0x96, 0x90); break;
        default: break;
        }
    }
    if (group == 2) {
        switch (item) {
        case 1: s.slot = 0x6a; break;
        case 2: s.slot = 0x68; break;
        case 3: s.slot = 0x66; break;
        case 4: s.slot = 0x62; break;
        case 5: s.slot = 0x60; break;
        case 6: s.slot = 0x6c; break;
        case 7: s.slot = 100; break;
        case 8: s.slot = 0x165; break;
        case 9: s.slot = byVariant(s.slot, 0x88, 0x92, 0x74, 0x195); break;
        case 10: s.slot = byVariant(s.slot, 0, 0, 0x94, 0x197); break;
        default: break;
        }
    }
    if (group == 3) {
        switch (item) {
        case 1: s.slot = 0x48; break;
        case 2: s.slot = 0x4a; break;
        case 3: s.slot = 0x4c; break;
        case 4: s.slot = 0xb; break;
        case 5: s.slot = 0x43; break;
        case 6: s.slot = byVariant(s.slot, 0x78, 0x7a, 0x7c, 0x193); break;
        case 7: s.slot = 0x8a; break;
        case 8: s.slot = 0x9c; break;
        case 9: s.slot = 0x80; break;
        default: break;
        }
    }
    if (group == 4) {
        switch (item) {
        case 1: s.slot = 0x6e; break;
        case 2: s.slot = 0x70; break;
        case 3: s.slot = 0x72; break;
        case 4: s = {1, 6}; break;
        case 5: s = {0x7d, 2}; break;
        case 6: s.slot = 0x9e; break;
        case 7: s.slot = byVariant(s.slot, 0x84, 0x8e, 0x98, 0x1aa); break;
        case 8: s.slot = byVariant(s.slot, 0x86, 0x8c, 0x9a, 0x1ac); break;
        case 9: s.slot = byVariant(s.slot, 0, 0, 0, 0x1ae); break;
        default: break;
        }
    }
    return s;
}

void QueueSelection(Drawing& d) {
    const DrawDataView& data = *d.data;
    const SelectionSprite s = SelectionSpriteOf(data);
    const std::uint32_t animation = d.Animation(g_raceAnimations[s.slot][d.Race(d.cell->owner)]);
    // (the selected cell is checked again per frame, as the original)
    auto selected = [&] {
        return data.selected_column == static_cast<std::int32_t>(d.column) &&
               data.selected_row == static_cast<std::int32_t>(d.row);
    };
    if (s.frames == 1) {
        const std::uint32_t sprite = Drawing::FrameOf(animation, 0);
        if (selected()) {
            d.QueueSprite(kRowSelected, sprite, kParamSelected);
        } else {
            d.QueueSprite(d.Row2(), sprite, kParamSelection);
        }
        return;
    }
    for (std::uint32_t frame = 0; frame < s.frames; ++frame) {
        const std::uint32_t sprite = Drawing::FrameOf(animation, static_cast<std::int32_t>(frame));
        if (selected()) {
            d.QueueSprite(kRowSelected, sprite, kParamSelected);
        } else {
            d.QueueSprite(d.Row2(), sprite, kParamSelection);
        }
    }
}

void QueueMarkers(Drawing& d) {
    const DrawDataView& data = *d.data;
    const std::uint8_t marker = d.cell->marker;
    if (data.unk060 == kModePlacing && data.unk074 >= 2 && data.unk074 <= 13) {
        bool listed = false;
        for (const std::int32_t type : kMarkerTypes) {
            listed |= data.unk0a4 == type;
        }
        if (listed && marker >= 1 && marker <= 4) {
            d.QueueSprite(kRowObjectMarker, Drawing::FrameOf(d.Animation(0x1005b), marker - 1), d.param);
        }
    }
    if (data.unk060 != kModeSelection || data.unk064 == 0 || data.unk068 == 0 || marker < 1 || marker > kMarkerMax) {
        return;
    }
    if (marker >= kMarkerAlways || (data.selected_column == static_cast<std::int32_t>(d.column) &&
                                    data.selected_row == static_cast<std::int32_t>(d.row))) {
        QueueSelection(d);
    }
    // The cell marker.
    std::int32_t index = marker - 1;
    if (index > 15) {
        index -= 16;
    }
    std::uint32_t sprite = d.lastSprite;  // (indexes 10..15: the last sprite, deliberately)
    if (index <= 7) {
        sprite = Drawing::FrameOf(d.Animation(0x40006), index);
    } else if (index == 8) {
        sprite = Drawing::FrameOf(d.Animation(0x10086), 0);
    } else if (index == 9) {
        sprite = Drawing::FrameOf(d.Animation(0x10087), 0);
    }
    d.QueueSprite(kRowCellMarker, sprite, kParamSelected);
    const std::uint16_t mapColumn = static_cast<std::uint16_t>(data.origin_column + static_cast<std::int32_t>(d.column));
    const std::uint16_t mapRow = static_cast<std::uint16_t>(data.origin_row + static_cast<std::int32_t>(d.row));
    reinterpret_cast<Arg5Fn>(kAddHitArea)(d.self, nullptr, 0xff, d.x, d.ScreenY(), mapColumn, mapRow);
}

}  // namespace

using QueueLandscapeObjectFn = void(__fastcall*)(CDrawer* self, void* edx, const DrawDataView* data,
                                                 std::uint32_t column, std::uint32_t row, std::uint32_t x,
                                                 std::uint32_t y, std::uint32_t object);
void __fastcall CDrawer_QueueLandscapeObject(CDrawer* self, void* edx, const DrawDataView* data, std::uint32_t column,
                                             std::uint32_t row, std::uint32_t x, std::uint32_t y,
                                             std::uint32_t object);
S3RE_HOOK(0x004d3050, CDrawer_QueueLandscapeObject, QueueLandscapeObjectFn, Original_CDrawer_QueueLandscapeObject,
          CDrawer_QueueLandscapeObject);

// Purpose: Queues the landscape object of a visible map cell (trees,
// stones, fields, the water animation) and the markers drawn on the cell,
// with their sounds, for drawing in the frame.
void __fastcall CDrawer_QueueLandscapeObject(CDrawer* self, void*, const DrawDataView* data, std::uint32_t column,
                                             std::uint32_t row, std::uint32_t x, std::uint32_t y,
                                             std::uint32_t object) {
    std::uint32_t locals[4];
    for (std::uint32_t& local : locals) {
        reinterpret_cast<ConstructFn>(kGfxDwordConstruct)(&local, nullptr, 0);
    }
    // 32-bit cell address arithmetic, as the original.
    const auto* cell = reinterpret_cast<const DrawCell*>(reinterpret_cast<std::uintptr_t>(data) +
                                                         offsetof(DrawDataView, cells) +
                                                         (column * kDrawCellRows + row) * sizeof(DrawCell));
    Drawing d{self, data, cell, column, row, x, y, static_cast<std::uint32_t>(cell->direction & kNoParamMask) * 4, 0};
    QueueObject(d, object);
    QueueMarkers(d);
}

// ---------------------------------------------------------------------------
// Tests: random cells, draw-data fields and arguments; every callee replaced
// by a recording stub (the getters write values derived from their
// arguments, the animation pointer a random sequence record; the sound and
// queue functions record their arguments; rand returns random values).
// ---------------------------------------------------------------------------

namespace {

struct Log {
    std::vector<std::uint32_t> values;
    std::uint8_t* sequence = nullptr;
    std::uint32_t randValue = 0;
    std::uint32_t self = 0;  // the drawer (recorded relative to it)
};

std::uint32_t Mix(std::uint32_t v) {
    v ^= v >> 13;
    v *= 0x9e3779b1u;
    return v ^ (v >> 16);
}

constexpr std::uint32_t kFakeEffect = 0x1234;

void RunQueueLandscapeObject(QueueLandscapeObjectFn fn, CDrawer* drawer, const DrawDataView* data,
                             std::uint32_t column, std::uint32_t row, std::uint32_t x, std::uint32_t y,
                             std::uint32_t object, std::uint64_t seed, Log& log) {
    test::Rng rng(seed);
    test::CalleeStubs stubs;
    stubs.SetWorld(reinterpret_cast<CGameData*>(drawer));
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
        return out(a[0], Mix(a[1] + 1));
    });
    stubs.Install<2, kColorIndexedAnimationPointer>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {3, self, a[1]});
        return out(a[0], Mix(a[1] + 2));
    });
    stubs.Install<2, kAnimationPointer>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {4, self, a[1]});
        return out(a[0], static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(log.sequence)));
    });
    stubs.Install<2, kPalettePointer>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {5, self, a[0], a[1]});
        return Mix(a[0] ^ a[1]);
    });
    // `this` of the list getters is a GfxDword local: its value is recorded.
    stubs.Install<2, kListImageChecked>([&](CGameData* world, std::uint32_t self, const std::uint32_t* a) {
        const std::uint32_t value =
            *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uintptr_t>(world) + self);
        log.values.insert(log.values.end(), {6, value, a[1]});
        return out(a[0], Mix(a[1] + 7));
    });
    stubs.Install<2, kListImage>([&](CGameData* world, std::uint32_t self, const std::uint32_t* a) {
        const std::uint32_t value =
            *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uintptr_t>(world) + self);
        log.values.insert(log.values.end(), {7, value, a[1]});
        return out(a[0], Mix(a[1] + 9));
    });
    const std::uint32_t sequence = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(log.sequence));
    auto relative = [sequence](std::uint32_t image) { return image - sequence; };
    stubs.Install<4, kQueueShadow>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {8, self, a[0], a[1], a[2], a[3]});
        return a[0];
    });
    stubs.Install<5, kQueueSprite>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {9, self, a[0], a[1], a[2], relative(a[3]), a[4]});
        return a[0];
    });
    stubs.Install<5, kQueueColourLayer>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {10, self, a[0], a[1], a[2], a[3], a[4]});
        return Mix(a[0] + a[3]);
    });
    stubs.Install<6, kQueueSpriteWithColour>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {11, self, a[0], a[1], a[2], relative(a[3]), a[4], a[5]});
        return a[0];
    });
    // Only the words of the hit area's last three arguments are stored.
    stubs.Install<5, kAddHitArea>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(),
                          {12, self, a[0], a[1], a[2] & 0xffff, a[3] & 0xffff, a[4] & 0xffff});
        return 0;
    });
    stubs.Install<2, kIsNearView>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {13, self, a[0], a[1]});
        return Mix(a[0] + a[1]) & 1;
    });
    stubs.Install<4, kAmbientSoundVolume>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {14, self, a[0], a[1], a[2], a[3]});
        return 77;
    });
    stubs.Install<1, kAmbientSoundPan>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {15, self, a[0]});
        return 55;
    });
    stubs.Install<0, kSoundIsEnabled>([&](CGameData*, std::uint32_t self, const std::uint32_t*) {
        log.values.insert(log.values.end(), {16, self});
        return static_cast<std::uint32_t>(rng.Chance(3, 4) ? 1 : 0);
    });
    stubs.Install<2, kSoundGetEffect>([&](CGameData*, std::uint32_t self, const std::uint32_t* a) {
        log.values.insert(log.values.end(), {17, self, a[0], a[1]});
        return kFakeEffect;
    });
    stubs.Install<3, kSoundEffectPlay>([&](CGameData* world, std::uint32_t self, const std::uint32_t* a) {
        const std::uint32_t effect = self + static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));
        log.values.insert(log.values.end(), {18, effect, a[0], a[1], a[2]});
        return 0;
    });
    stubs.Install<0, kRand>([&](CGameData*, std::uint32_t, const std::uint32_t*) {
        log.values.push_back(19);
        return rng.Below(32768);
    });
    fn(drawer, nullptr, data, column, row, x, y, object);
}

void CompareQueueLandscapeObject(test::Context& ctx, QueueLandscapeObjectFn candidate, int cases) {
    std::vector<std::uint8_t> dataBytes(sizeof(DrawDataView));
    std::vector<std::uint8_t> drawerBytes(sizeof(CDrawer));
    std::uint8_t sequence[8 + 4 * 256];
    auto* const data = reinterpret_cast<DrawDataView*>(dataBytes.data());
    auto* const drawer = reinterpret_cast<CDrawer*>(drawerBytes.data());
    for (int i = 0; i < cases; ++i) {
        test::Rng& rng = ctx.rng();
        if (i % 200 == 0) {
            rng.Fill(dataBytes.data(), dataBytes.size());
            rng.Fill(drawerBytes.data(), drawerBytes.size());
        }
        const std::uint32_t column = rng.Below(kDrawCellRows);
        const std::uint32_t row = rng.Below(kDrawCellRows);
        DrawCell& cell = data->cells[column][row];
        rng.Fill(&cell, sizeof(cell));
        // Values the branches test.
        if (rng.Chance(1, 2)) {
            cell.unk02 = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0x50 : rng.Below(10));
        }
        if (rng.Chance(1, 3)) {
            cell.animation = 0;
        }
        if (rng.Chance(1, 2)) {
            cell.marker = static_cast<std::uint8_t>(rng.Below(0x24));
        }
        if (rng.Chance(1, 2)) {
            cell.owner = static_cast<std::uint8_t>(rng.Below(20));
        }
        data->clock = rng.Next32();
        data->origin_column = static_cast<std::int32_t>(rng.Chance(1, 4) ? rng.Next32() : rng.Below(1024));
        data->origin_row = static_cast<std::int32_t>(rng.Chance(1, 4) ? rng.Next32() : rng.Below(1024));
        data->unk060 = static_cast<std::int32_t>(rng.Below(6));
        data->unk064 = static_cast<std::int32_t>(rng.Below(6));
        data->unk068 = static_cast<std::int32_t>(rng.Below(15));
        data->unk074 = rng.Chance(1, 8) ? rng.Next32() : rng.Below(16);
        data->unk094 = static_cast<std::int32_t>(rng.Below(5));
        data->unk0a4 = static_cast<std::int32_t>(rng.Below(0x40));
        if (rng.Chance(1, 2)) {
            data->selected_column = static_cast<std::int32_t>(column);
            data->selected_row = static_cast<std::int32_t>(row);
        } else {
            data->selected_column = static_cast<std::int32_t>(rng.Below(kDrawCellRows));
            data->selected_row = static_cast<std::int32_t>(rng.Below(kDrawCellRows));
        }
        drawer->ambient_count[kAmbientKind] = static_cast<std::int32_t>(rng.Below(4)) - 1;
        rng.Fill(sequence, sizeof(sequence));
        sequence[7] = static_cast<std::uint8_t>(rng.Chance(1, 4) ? rng.Below(3) : rng.Below(256));
        const std::uint32_t x = rng.Next32();
        const std::uint32_t y = rng.Chance(1, 2) ? rng.Below(600) : rng.Next32();
        const std::uint32_t object = rng.Chance(1, 8) ? 0 : (rng.Chance(1, 16) ? rng.Next32() : rng.Below(256));
        const std::uint64_t seed = rng.Next64();
        Log a, b;
        a.sequence = b.sequence = sequence;
        RunQueueLandscapeObject(Original_CDrawer_QueueLandscapeObject, drawer, data, column, row, x, y, object, seed,
                                a);
        RunQueueLandscapeObject(candidate, drawer, data, column, row, x, y, object, seed, b);
        if (!S3RE_EXPECT(a.values == b.values)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, object 0x%x, sizes %zu / %zu", i, object, a.values.size(),
                     b.values.size());
            return;
        }
    }
}

// Wrong: the trees of object 0x50 one pixel to the right.
void __fastcall WrongQueueLandscapeObject(CDrawer* self, void* edx, const DrawDataView* data, std::uint32_t column,
                                          std::uint32_t row, std::uint32_t x, std::uint32_t y, std::uint32_t object) {
    CDrawer_QueueLandscapeObject(self, edx, data, column, row, x + (object == 0x50 ? 1 : 0), y, object);
}

}  // namespace

S3RE_TEST(CDrawer_QueueLandscapeObject_MatchesOriginal) {
    CompareQueueLandscapeObject(ctx, &CDrawer_QueueLandscapeObject, 60000);
}

S3RE_TEST(CDrawer_QueueLandscapeObject_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareQueueLandscapeObject(probe, &WrongQueueLandscapeObject, 20000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
