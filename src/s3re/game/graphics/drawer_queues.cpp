// The draw queues of the in-game drawer (CDrawer; graphics/drawer_layout.h
// explains the pools), its hit areas and two small pixel routines. All
// thiscall on the drawer unless noted; found in the unnamed object file
// 0x4d65e0..0x4d9250 between Volker\DrawBuilding.cpp and GameDataSave.cpp.
// Was the Logic_sub_4d7xxx cluster: these are drawing code, not simulation.
//
// CDrawer::QueueShadow (0x004d7c90, was Logic_sub_4d7c90): (row, x, y,
//   sprite), ret 0x10: unless the row is outside 0..249 (signed) or the
//   shadow pool is full (used == 4000 exactly), fills the row's empty last
//   record with x, y, sprite and fraction 0x10000 (all of it), links it to
//   the fresh empty record `used`, and counts it. EAX = row.
// CDrawer::QueueShadowPart (0x004d7d50, was Logic_sub_4d7d50): (row, x, y,
//   sprite, fraction), ret 0x14: the same with the given fraction.
// CDrawer::QueueSprite (0x004d7e10, was Logic_sub_4d7e10): (row, x, y,
//   sprite, param), ret 0x14: the same in the sprite pool (8000 records),
//   fraction 0x10000, no colour layer.
// CDrawer::QueueSpritePart (0x004d7f30, was Logic_sub_4d7f30): (row, x, y,
//   sprite, param, fraction), ret 0x18.
// CDrawer::QueueSpriteWithColour (0x004d8050, was Logic_sub_4d8050): (row,
//   x, y, sprite, param, colourLayer), ret 0x18: fraction 0x10000 and the
//   colour layer record returned by QueueColourLayer.
// CDrawer::QueueColourLayer (0x004d8170, was Logic_sub_4d8170): (row, x, y,
//   sprite, param), ret 0x14: the same in the colour-layer pool (4000
//   records). EAX = the record it filled (the row's empty last record, read
//   before the checks, so also when nothing is queued).
// CDrawer::AddHitArea (0x004d8240, was Logic_sub_4d8240): (kind, x, y,
//   value1, value2), ret 0x14, ECX not used: appends a hit area to the half
//   of g_hitAreas being drawn (entry 2 * count + 1 - g_hitAreaReadHalf),
//   unless 10000 are there (exactly). EAX not used.
// CDrawer::DrawStatisticsBar (0x004d7bd0, was Logic_sub_4d7bd0): (column,
//   row, length), ret 0xc: fills `length` cells of g_uiCellSize x 2 *
//   g_uiCellSize pixels, left to right from cell (column, row) of the
//   statistics screen (offset by g_drawOriginX/Y), on the surface +0xa4 in
//   cyan (PackColour(0, 200, 200)). The half pitch is an unsigned shift.
//   EAX = the colour.
// CDrawer::DrawGlyphRow (0x004d6950, was Logic_sub_4d6950): (column, row,
//   bits), ret 0xc: one 8-pixel row of a bitmap font glyph (bit 0x80 = the
//   leftmost pixel) at pixel (column * 8, row) of the surface +0xec in light
//   grey (PackColour(230, 230, 230)), with a black shadow pixel one down and
//   one right, except on the last screen row (g_displayHeight - 1). The half
//   pitch is a signed division. Called by the debug text routine 0x4d6740.

#include "game/globals.h"
#include "game/graphics/drawer_layout.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

constexpr std::uint32_t kFullFraction = 0x10000;  // 16.16: draw all of the image
constexpr std::int32_t kHitAreasPerHalf = 10000;
constexpr std::uint32_t kGlyphWidth = 8;

constexpr std::uintptr_t kPackColour = 0x004c7b00;  // Render::PackColour, stdcall (c1, c2, c3)

std::uint32_t PackColour(std::uint32_t c1, std::uint32_t c2, std::uint32_t c3) {
    using Fn = std::uint32_t(__stdcall*)(std::uint32_t, std::uint32_t, std::uint32_t);
    return reinterpret_cast<Fn>(kPackColour)(c1, c2, c3);
}

bool IsDrawRow(std::int32_t row) {
    return row >= 0 && row < static_cast<std::int32_t>(kDrawRows);
}

// The row's empty last record gets filled by `fill` (in the original's field
// order) and links to a fresh empty record. The caller checked the row and
// the pool.
template <typename Queue, typename Fill>
void Append(Queue& queue, std::int32_t row, Fill fill) {
    fill(queue.records[queue.tails[row]]);
    queue.records[queue.tails[row]].next = queue.used;
    queue.tails[row] = queue.used;
    queue.used = queue.used + 1;
    queue.counts[row] = queue.counts[row] + 1;
}

template <typename Queue>
bool HasRoom(const Queue& queue, std::uint32_t capacity) {
    return queue.used != capacity;  // (only exactly full counts, as in the original)
}

}  // namespace

using Queue4Fn = std::uint32_t(__fastcall*)(CDrawer* self, void* edx, std::int32_t row, std::uint32_t x,
                                            std::uint32_t y, std::uint32_t sprite);
using Queue5Fn = std::uint32_t(__fastcall*)(CDrawer* self, void* edx, std::int32_t row, std::uint32_t x,
                                            std::uint32_t y, std::uint32_t sprite, std::uint32_t a5);
using Queue6Fn = std::uint32_t(__fastcall*)(CDrawer* self, void* edx, std::int32_t row, std::uint32_t x,
                                            std::uint32_t y, std::uint32_t sprite, std::uint32_t a5,
                                            std::uint32_t a6);
using HitAreaFn = std::uint32_t(__stdcall*)(std::uint32_t kind, std::uint32_t x, std::uint32_t y,
                                            std::uint32_t value1, std::uint32_t value2);
using DrawerPixelsFn = std::uint32_t(__fastcall*)(CDrawer* self, void* edx, std::uint32_t a1, std::uint32_t a2,
                                                  std::uint32_t a3);

// Purpose: Queues the shadow of an object for drawing in its screen row
// (drawn before the objects themselves).
std::uint32_t __fastcall CDrawer_QueueShadow(CDrawer* self, void*, std::int32_t row, std::uint32_t x,
                                             std::uint32_t y, std::uint32_t sprite) {
    auto& queue = DrawerOf(self)->shadows;
    if (IsDrawRow(row) && HasRoom(queue, 4000)) {
        Append(queue, row, [&](ShadowRecord& r) {
            r.x = static_cast<std::int32_t>(x);
            r.y = static_cast<std::int32_t>(y);
            r.sprite = sprite;
            r.fraction = kFullFraction;
        });
    }
    return static_cast<std::uint32_t>(row);
}

// Purpose: Queues a partly visible shadow (e.g. of a building under
// construction).
std::uint32_t __fastcall CDrawer_QueueShadowPart(CDrawer* self, void*, std::int32_t row, std::uint32_t x,
                                                 std::uint32_t y, std::uint32_t sprite, std::uint32_t fraction) {
    auto& queue = DrawerOf(self)->shadows;
    if (IsDrawRow(row) && HasRoom(queue, 4000)) {
        Append(queue, row, [&](ShadowRecord& r) {
            r.x = static_cast<std::int32_t>(x);
            r.y = static_cast<std::int32_t>(y);
            r.sprite = sprite;
            r.fraction = fraction;
        });
    }
    return static_cast<std::uint32_t>(row);
}

namespace {

std::uint32_t QueueSpriteRecord(CDrawer* self, std::int32_t row, std::uint32_t x, std::uint32_t y,
                                std::uint32_t sprite, std::uint32_t param, std::uint32_t fraction,
                                std::uint32_t colourLayer) {
    auto& queue = DrawerOf(self)->sprites;
    if (IsDrawRow(row) && HasRoom(queue, 8000)) {
        Append(queue, row, [&](SpriteRecord& r) {
            r.x = static_cast<std::int32_t>(x);
            r.y = static_cast<std::int32_t>(y);
            r.param = param;
            r.sprite = sprite;
            r.fraction = fraction;
            r.colour_layer = colourLayer;
        });
    }
    return static_cast<std::uint32_t>(row);
}

}  // namespace

// Purpose: Queues an object's image (a settler, building, tree, ...) for
// drawing in its screen row.
std::uint32_t __fastcall CDrawer_QueueSprite(CDrawer* self, void*, std::int32_t row, std::uint32_t x,
                                             std::uint32_t y, std::uint32_t sprite, std::uint32_t param) {
    return QueueSpriteRecord(self, row, x, y, sprite, param, kFullFraction, 0);
}

// Purpose: Queues a partly visible image (probably a building growing out of
// its construction site).
std::uint32_t __fastcall CDrawer_QueueSpritePart(CDrawer* self, void*, std::int32_t row, std::uint32_t x,
                                                 std::uint32_t y, std::uint32_t sprite, std::uint32_t param,
                                                 std::uint32_t fraction) {
    return QueueSpriteRecord(self, row, x, y, sprite, param, fraction, 0);
}

// Purpose: Queues an image together with its player-colour layer (queued
// before with QueueColourLayer), drawn right after it.
std::uint32_t __fastcall CDrawer_QueueSpriteWithColour(CDrawer* self, void*, std::int32_t row, std::uint32_t x,
                                                       std::uint32_t y, std::uint32_t sprite, std::uint32_t param,
                                                       std::uint32_t colourLayer) {
    return QueueSpriteRecord(self, row, x, y, sprite, param, kFullFraction, colourLayer);
}

// Purpose: Queues the player-colour layer of an image and returns its record,
// which the image's queue entry then refers to.
std::uint32_t __fastcall CDrawer_QueueColourLayer(CDrawer* self, void*, std::int32_t row, std::uint32_t x,
                                                  std::uint32_t y, std::uint32_t sprite, std::uint32_t param) {
    auto& queue = DrawerOf(self)->colour_layers;
    // (Read before the row check, as in the original.)
    const std::uint32_t record = queue.tails[row];
    if (IsDrawRow(row) && HasRoom(queue, 4000)) {
        Append(queue, row, [&](ColourRecord& r) {
            r.x = static_cast<std::int32_t>(x);
            r.y = static_cast<std::int32_t>(y);
            r.param = param;
            r.sprite = sprite;
        });
    }
    return record;
}

// Purpose: Remembers where an object was drawn so that a click on the screen
// can be matched to it (CGameData::PickHitArea).
std::uint32_t __stdcall CDrawer_AddHitArea(std::uint32_t kind, std::uint32_t x, std::uint32_t y,
                                           std::uint32_t value1, std::uint32_t value2) {
    const std::int32_t count = g_hitAreaDrawCount;
    if (count == kHitAreasPerHalf) {
        return 0;
    }
    // Entry 2 * count + (1 - half): the half the UI doesn't read (32-bit
    // index arithmetic, the half isn't checked).
    const std::uint32_t entry = static_cast<std::uint32_t>(count) * 2u - static_cast<std::uint32_t>(g_hitAreaReadHalf.get()) + 1u;
    HitArea& area = g_hitAreas.get()[entry];
    area.kind = static_cast<std::uint16_t>(kind);
    area.x = static_cast<std::int16_t>(x);
    area.y = static_cast<std::int16_t>(y);
    area.value1 = static_cast<std::uint16_t>(value1);
    area.value2 = static_cast<std::uint16_t>(value2);
    g_hitAreaDrawCount = count + 1;
    return 0;
}

// Purpose: Draws one bar of the statistics screen (probably a player's
// value), `length` cells long.
std::uint32_t __fastcall CDrawer_DrawStatisticsBar(CDrawer* self, void*, std::uint32_t column, std::uint32_t row,
                                                   std::uint32_t length) {
    CDrawer* drawer = DrawerOf(self);
    const std::uint32_t cell = static_cast<std::uint32_t>(g_uiCellSize.get());
    const std::uint32_t halfPitch = static_cast<std::uint32_t>(drawer->pitch) >> 1;
    const std::uint32_t first = cell * column + g_drawOriginX + (cell * row + g_drawOriginY) * halfPitch;
    std::uint16_t* pixel = drawer->pixels_a4 + first;
    const std::uint32_t colour = PackColour(0, 200, 200);
    // `length` times a block of g_uiCellSize columns (re-read like the
    // original; the columns of all blocks follow each other).
    for (std::uint32_t block = 0; block < length; ++block) {
        for (std::uint32_t c = 0; c < static_cast<std::uint32_t>(g_uiCellSize.get()); ++c) {
            std::uint16_t* p = pixel;
            for (std::uint32_t r = 0; r < 2 * static_cast<std::uint32_t>(g_uiCellSize.get()); ++r) {
                *p = static_cast<std::uint16_t>(colour);
                p = reinterpret_cast<std::uint16_t*>(reinterpret_cast<std::uint8_t*>(p) + halfPitch * 2);
            }
            ++pixel;
        }
    }
    return colour;
}

// Purpose: Draws one pixel row of a character of the debug text font, with a
// shadow.
std::uint32_t __fastcall CDrawer_DrawGlyphRow(CDrawer* self, void*, std::uint32_t column, std::uint32_t row,
                                              std::uint32_t bits) {
    CDrawer* drawer = DrawerOf(self);
    const auto colour = static_cast<std::uint16_t>(PackColour(230, 230, 230));
    const bool lastRow = row == static_cast<std::uint32_t>(g_displayHeight.get()) - 1u;
    std::uint32_t mask = 0x80;
    for (std::uint32_t bit = 0; bit < kGlyphWidth; ++bit, mask >>= 1) {
        if ((bits & mask) == 0) {
            continue;
        }
        // (The pitch is re-read for every pixel, as in the original.)
        const auto halfPitch = static_cast<std::uint32_t>(drawer->pitch / 2);
        drawer->pixels_ec[halfPitch * row + column * kGlyphWidth + bit] = colour;
        if (!lastRow) {
            const auto halfPitchBelow = static_cast<std::uint32_t>(drawer->pitch / 2);
            drawer->pixels_ec[halfPitchBelow * (row + 1) + column * kGlyphWidth + bit + 1] = 0;
        }
    }
    return 0;
}

S3RE_HOOK(0x004d7c90, CDrawer_QueueShadow, Queue4Fn, Original_CDrawer_QueueShadow, CDrawer_QueueShadow);
S3RE_HOOK(0x004d7d50, CDrawer_QueueShadowPart, Queue5Fn, Original_CDrawer_QueueShadowPart, CDrawer_QueueShadowPart);
S3RE_HOOK(0x004d7e10, CDrawer_QueueSprite, Queue5Fn, Original_CDrawer_QueueSprite, CDrawer_QueueSprite);
S3RE_HOOK(0x004d7f30, CDrawer_QueueSpritePart, Queue6Fn, Original_CDrawer_QueueSpritePart, CDrawer_QueueSpritePart);
S3RE_HOOK(0x004d8050, CDrawer_QueueSpriteWithColour, Queue6Fn, Original_CDrawer_QueueSpriteWithColour,
          CDrawer_QueueSpriteWithColour);
S3RE_HOOK(0x004d8170, CDrawer_QueueColourLayer, Queue5Fn, Original_CDrawer_QueueColourLayer,
          CDrawer_QueueColourLayer);
S3RE_HOOK(0x004d8240, CDrawer_AddHitArea, HitAreaFn, Original_CDrawer_AddHitArea, CDrawer_AddHitArea);
S3RE_HOOK(0x004d7bd0, CDrawer_DrawStatisticsBar, DrawerPixelsFn, Original_CDrawer_DrawStatisticsBar,
          CDrawer_DrawStatisticsBar);
S3RE_HOOK(0x004d6950, CDrawer_DrawGlyphRow, DrawerPixelsFn, Original_CDrawer_DrawGlyphRow, CDrawer_DrawGlyphRow);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// Picks a row (mostly valid), points its tail at a valid record and puts the
// pool near its limit sometimes.
template <typename Queue>
std::int32_t PrepareQueue(test::Context& ctx, test::WorldPair& drawers, Queue CDrawer::*queue,
                          std::uint32_t capacity) {
    const std::int32_t row = ctx.rng().Chance(1, 8) ? ctx.rng().Range(-5, 260) : static_cast<std::int32_t>(ctx.rng().Below(250));
    const std::uint32_t used = ctx.rng().Chance(1, 4) ? capacity - ctx.rng().Below(3) : 250 + ctx.rng().Below(capacity - 250);
    const std::uint32_t tail = ctx.rng().Below(capacity);
    drawers.Both([&](CGameData* w) {
        Queue& q = DrawerOf(w)->*queue;
        q.used = used;
        if (row >= -5 && row < 260) {
            // (rows -5..259 index inside the drawer: tails sits between the
            // records and the counts)
            q.tails[row] = tail;
        }
    });
    return row;
}

using QueueCall = std::uint32_t (*)(CDrawer* drawer, std::int32_t row, const std::uint32_t* a, bool original);

template <typename Queue>
void CompareQueue(test::Context& ctx, Queue CDrawer::*queue, std::uint32_t capacity, QueueCall call, int cases) {
    test::WorldPair drawers(ctx, sizeof(CDrawer));
    for (int i = 0; i < cases; ++i) {
        const std::int32_t row = PrepareQueue(ctx, drawers, queue, capacity);
        std::uint32_t a[5];
        for (std::uint32_t& value : a) {
            value = ctx.rng().Next32();
        }
        const std::uint32_t expected = call(DrawerOf(drawers.original()), row, a, true);
        const std::uint32_t actual = call(DrawerOf(drawers.ours()), row, a, false);
        if (!S3RE_EXPECT_EQ(expected, actual) || (i % 64 == 63 && !drawers.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: row %d", i, row);
            return;
        }
    }
    drawers.Checkpoint(ctx);
}

#define S3RE_QUEUE_CALL(name, ...)                                                                \
    [](CDrawer* d, std::int32_t row, const std::uint32_t* a, bool original) -> std::uint32_t {    \
        return original ? Original_##name(d, nullptr, row, __VA_ARGS__) : name(d, nullptr, row, __VA_ARGS__); \
    }

// Wrong: the record links to itself instead of the fresh empty record.
std::uint32_t WrongQueueSprite(CDrawer* d, std::int32_t row, const std::uint32_t* a, bool original) {
    if (original) {
        return Original_CDrawer_QueueSprite(d, nullptr, row, a[0], a[1], a[2], a[3]);
    }
    const std::uint32_t tail = IsDrawRow(row) ? d->sprites.tails[row] : 0;
    const std::uint32_t used = d->sprites.used;
    const std::uint32_t result = CDrawer_QueueSprite(d, nullptr, row, a[0], a[1], a[2], a[3]);
    if (IsDrawRow(row) && used != 8000) {
        d->sprites.records[tail].next = tail;
    }
    return result;
}

}  // namespace

S3RE_TEST(CDrawer_QueueShadow_MatchesOriginal) {
    CompareQueue(ctx, &CDrawer::shadows, 4000, S3RE_QUEUE_CALL(CDrawer_QueueShadow, a[0], a[1], a[2]), 3000);
}
S3RE_TEST(CDrawer_QueueShadowPart_MatchesOriginal) {
    CompareQueue(ctx, &CDrawer::shadows, 4000, S3RE_QUEUE_CALL(CDrawer_QueueShadowPart, a[0], a[1], a[2], a[3]), 3000);
}
S3RE_TEST(CDrawer_QueueSprite_MatchesOriginal) {
    CompareQueue(ctx, &CDrawer::sprites, 8000, S3RE_QUEUE_CALL(CDrawer_QueueSprite, a[0], a[1], a[2], a[3]), 3000);
}
S3RE_TEST(CDrawer_QueueSprite_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareQueue(probe, &CDrawer::sprites, 8000, &WrongQueueSprite, 500);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CDrawer_QueueSpritePart_MatchesOriginal) {
    CompareQueue(ctx, &CDrawer::sprites, 8000,
                 S3RE_QUEUE_CALL(CDrawer_QueueSpritePart, a[0], a[1], a[2], a[3], a[4]), 3000);
}
S3RE_TEST(CDrawer_QueueSpriteWithColour_MatchesOriginal) {
    CompareQueue(ctx, &CDrawer::sprites, 8000,
                 S3RE_QUEUE_CALL(CDrawer_QueueSpriteWithColour, a[0], a[1], a[2], a[3], a[4]), 3000);
}
S3RE_TEST(CDrawer_QueueColourLayer_MatchesOriginal) {
    CompareQueue(ctx, &CDrawer::colour_layers, 4000,
                 S3RE_QUEUE_CALL(CDrawer_QueueColourLayer, a[0], a[1], a[2], a[3]), 3000);
}

#undef S3RE_QUEUE_CALL

namespace {

constexpr std::size_t kHitAreaBytes = sizeof(HitArea) * 20000;

void CompareAddHitArea(test::Context& ctx, HitAreaFn candidate, int cases) {
    static std::uint8_t saved[kHitAreaBytes], originalAreas[kHitAreaBytes];
    std::memcpy(saved, &g_hitAreas.get(), kHitAreaBytes);
    const std::int32_t savedCount = g_hitAreaDrawCount, savedHalf = g_hitAreaReadHalf;
    for (int i = 0; i < cases; ++i) {
        const std::int32_t count = ctx.rng().Chance(1, 4) ? kHitAreasPerHalf - static_cast<std::int32_t>(ctx.rng().Below(3))
                                                          : static_cast<std::int32_t>(ctx.rng().Below(10000));
        const std::int32_t half = static_cast<std::int32_t>(ctx.rng().Below(2));
        std::uint32_t a[5];
        for (std::uint32_t& value : a) {
            value = ctx.rng().Next32();
        }
        g_hitAreaDrawCount = count;
        g_hitAreaReadHalf = half;
        Original_CDrawer_AddHitArea(a[0], a[1], a[2], a[3], a[4]);
        std::memcpy(originalAreas, &g_hitAreas.get(), kHitAreaBytes);
        const std::int32_t originalCount = g_hitAreaDrawCount;
        std::memcpy(&g_hitAreas.get(), saved, kHitAreaBytes);
        g_hitAreaDrawCount = count;
        candidate(a[0], a[1], a[2], a[3], a[4]);
        bool same = S3RE_EXPECT_EQ(originalCount, g_hitAreaDrawCount.get());
        same &= S3RE_EXPECT_MEM_EQ(originalAreas, &g_hitAreas.get(), kHitAreaBytes);
        std::memcpy(&g_hitAreas.get(), saved, kHitAreaBytes);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: count %d, half %d", i, count, half);
            break;
        }
    }
    g_hitAreaDrawCount = savedCount;
    g_hitAreaReadHalf = savedHalf;
}

// Wrong: writes into the half the UI reads.
std::uint32_t __stdcall WrongAddHitArea(std::uint32_t kind, std::uint32_t x, std::uint32_t y, std::uint32_t v1,
                                        std::uint32_t v2) {
    g_hitAreaReadHalf = 1 - g_hitAreaReadHalf;
    const std::uint32_t r = CDrawer_AddHitArea(kind, x, y, v1, v2);
    g_hitAreaReadHalf = 1 - g_hitAreaReadHalf;
    return r;
}

}  // namespace

S3RE_TEST(CDrawer_AddHitArea_MatchesOriginal) {
    CompareAddHitArea(ctx, &CDrawer_AddHitArea, 300);
}
S3RE_TEST(CDrawer_AddHitArea_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareAddHitArea(probe, &WrongAddHitArea, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

constexpr std::size_t kSurfaceBytes = 256 * 1024;

// A drawer with the two surface pointers aimed at its own pixel buffer;
// the pitch, the cell size, the origin and the screen height are random
// within ranges that keep the pixels inside the buffer.
void ComparePixels(test::Context& ctx, DrawerPixelsFn original, DrawerPixelsFn candidate, bool glyph, int cases) {
    test::WorldPair drawers(ctx, sizeof(CDrawer));
    static std::uint8_t surfaceA[kSurfaceBytes], surfaceB[kSurfaceBytes];
    const std::int32_t savedCell = g_uiCellSize, savedHeight = g_displayHeight;
    const std::uint32_t savedX = g_drawOriginX, savedY = g_drawOriginY;
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(surfaceA, sizeof(surfaceA));
        std::memcpy(surfaceB, surfaceA, sizeof(surfaceB));
        const std::int32_t pitch = static_cast<std::int32_t>(ctx.rng().Range(0, 300)) * 2 + (ctx.rng().Chance(1, 4) ? 1 : 0);
        g_uiCellSize = static_cast<std::int32_t>(ctx.rng().Below(6));
        g_drawOriginX = ctx.rng().Below(16);
        g_drawOriginY = ctx.rng().Below(16);
        std::uint32_t a1, a2, a3;
        if (glyph) {
            a2 = ctx.rng().Below(200);
            g_displayHeight = static_cast<std::int32_t>(ctx.rng().Chance(1, 3) ? a2 + 1 : ctx.rng().Below(400));
            a1 = ctx.rng().Below(30);
            a3 = ctx.rng().Chance(1, 4) ? ctx.rng().Next32() : ctx.rng().Below(256);
        } else {
            a1 = ctx.rng().Below(12);
            a2 = ctx.rng().Below(12);
            a3 = ctx.rng().Below(12);
        }
        drawers.Both([&](CGameData* w) { DrawerOf(w)->pitch = pitch; });
        DrawerOf(drawers.original())->pixels_a4 = reinterpret_cast<std::uint16_t*>(surfaceA);
        DrawerOf(drawers.original())->pixels_ec = reinterpret_cast<std::uint16_t*>(surfaceA);
        DrawerOf(drawers.ours())->pixels_a4 = reinterpret_cast<std::uint16_t*>(surfaceB);
        DrawerOf(drawers.ours())->pixels_ec = reinterpret_cast<std::uint16_t*>(surfaceB);
        const std::uint32_t expected = original(DrawerOf(drawers.original()), nullptr, a1, a2, a3);
        const std::uint32_t actual = candidate(DrawerOf(drawers.ours()), nullptr, a1, a2, a3);
        bool same = glyph || S3RE_EXPECT_EQ(expected, actual);
        same &= S3RE_EXPECT_MEM_EQ(surfaceA, surfaceB, sizeof(surfaceA));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%u, %u, %#x), pitch %d", i, a1, a2, a3, pitch);
            break;
        }
    }
    g_uiCellSize = savedCell;
    g_displayHeight = savedHeight;
    g_drawOriginX = savedX;
    g_drawOriginY = savedY;
}

// Wrong: the glyph's shadow is also drawn on the last row.
std::uint32_t __fastcall WrongGlyphRow(CDrawer* self, void* edx, std::uint32_t column, std::uint32_t row,
                                       std::uint32_t bits) {
    const std::int32_t height = g_displayHeight;
    g_displayHeight = 0;
    const std::uint32_t r = CDrawer_DrawGlyphRow(self, edx, column, row, bits);
    g_displayHeight = height;
    return r;
}

}  // namespace

S3RE_TEST(CDrawer_DrawStatisticsBar_MatchesOriginal) {
    ComparePixels(ctx, Original_CDrawer_DrawStatisticsBar, &CDrawer_DrawStatisticsBar, false, 500);
}
S3RE_TEST(CDrawer_DrawGlyphRow_MatchesOriginal) {
    ComparePixels(ctx, Original_CDrawer_DrawGlyphRow, &CDrawer_DrawGlyphRow, true, 1000);
}
S3RE_TEST(CDrawer_DrawGlyphRow_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePixels(probe, Original_CDrawer_DrawGlyphRow, &WrongGlyphRow, true, 300);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
