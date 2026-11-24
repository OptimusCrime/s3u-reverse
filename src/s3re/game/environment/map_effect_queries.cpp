// The map effect table's constructor and two queries on a record (see
// map_effects.cpp; Volker\LogicInit.cpp).
//
// MapEffects::MapEffects (0x005442c0): () thiscall on the table
//   (world+0x14d2bc8), called by CGameData::CGameData: the 5000 records
//   get MapEffect::MapEffect (0x543830, through its address), then
//   slots_used .. height (+0x668a0..+0x668b8) and the 10000 cell heads are
//   0. EAX = this. ret.
// MapEffect::TileAt (0x005441c0): (tick, int* x, int* y) thiscall on a
//   record, ret 0xc: the tile the effect is over at `tick`, interpolated
//   from (start_tile_x, start_tile_y) at start_tick to (end_tile_x,
//   end_tile_y) after `duration` (signed word): *x = ((tick - start) *
//   end_x + (start + duration - tick) * start_x) / duration, the same for
//   y (32-bit products, signed division; start and duration are read again
//   after the store to *x, as in the original). EAX = the y quotient
//   (callers don't use it). Called by MakeDrawRecord 0x544100 and
//   CollectVisible 0x544390.
// MapEffect::IsVisibleIn (0x00544030): (tick, left, top, right, bottom)
//   thiscall, ret 0x14 -> AL: false before start_tick or (unless duration
//   is -1, "forever") from start_tick + duration on (signed); otherwise its
//   position at `tick` (MapEffect::PositionAt 0x544210) must lie within the
//   rectangle grown by 30 pixels: x + 30 >= left, y + 30 >= top, x - 30 <
//   right, y - 30 < bottom (signed). Only AL is the result (the original
//   leaves other bytes of EAX; its caller CollectVisible tests AL).

#include "game/layout.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <initializer_list>

namespace s3re::game {

using layout::MapEffect;
using layout::MapEffectTable;

namespace {

constexpr std::uintptr_t kMapEffectDefaultConstruct = 0x00543830;  // MapEffect::MapEffect ()
constexpr std::uintptr_t kMapEffectPositionAt = 0x00544210;        // MapEffect::PositionAt (tick, x*, y*)

constexpr std::int16_t kForever = -1;           // duration of an effect that doesn't expire
constexpr std::int32_t kVisibleMargin = 30;     // pixels around the view an effect may stick out

// 32-bit wrapping sum, as the original's add/lea.
std::int32_t Plus(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}

}  // namespace

using TableFn = MapEffectTable*(__fastcall*)(MapEffectTable* self, void* edx);
using TileAtFn = std::int32_t(__fastcall*)(MapEffect* self, void* edx, std::uint32_t tick, std::int32_t* x,
                                           std::int32_t* y);
using VisibleFn = bool(__fastcall*)(MapEffect* self, void* edx, std::int32_t tick, std::int32_t left,
                                    std::int32_t top, std::int32_t right, std::int32_t bottom);

// Purpose: Creates the empty map effect table when a world is built.
MapEffectTable* __fastcall MapEffects_Construct(MapEffectTable* self, void*) {
    using DefaultFn = void*(__fastcall*)(MapEffect* record, void* edx);
    for (MapEffect& record : self->records) {
        reinterpret_cast<DefaultFn>(kMapEffectDefaultConstruct)(&record, nullptr);
    }
    self->slots_used = 0;
    self->blocks_x = 0;
    self->blocks_y = 0;
    self->expire_cursor = 0;
    self->block_count_16 = 0;
    self->width = 0;
    self->height = 0;
    std::memset(self->cell_heads, 0, sizeof(self->cell_heads));
    return self;
}

// Purpose: Which tile a moving map effect (probably a flying stone or a
// spell) is over at a given tick, e.g. to check whether it is in view or
// under fog.
std::int32_t __fastcall MapEffect_TileAt(MapEffect* self, void*, std::uint32_t tick, std::int32_t* x,
                                         std::int32_t* y) {
    // The ticks left (start + duration - tick) are computed once; start and
    // duration are read again after the store to *x (which may alias them),
    // as in the original. 32-bit products, signed divisions.
    const std::uint32_t start = self->start_tick;
    const std::int32_t duration = self->duration;
    const std::uint32_t left = start + static_cast<std::uint32_t>(duration) - tick;
    *x = static_cast<std::int32_t>((tick - start) * self->end_tile_x + self->start_tile_x * left) / duration;
    const std::uint32_t done = tick - self->start_tick;
    const std::uint32_t partY = self->start_tile_y * left;
    const auto tileY = static_cast<std::int32_t>(done * self->end_tile_y + partY) / self->duration;
    *y = tileY;
    return tileY;
}

// Purpose: Whether a map effect exists at a tick and is (nearly) inside the
// visible part of the map, so the draw data must include it.
bool __fastcall MapEffect_IsVisibleIn(MapEffect* self, void*, std::int32_t tick, std::int32_t left, std::int32_t top,
                                      std::int32_t right, std::int32_t bottom) {
    const auto start = static_cast<std::int32_t>(self->start_tick);
    if (tick < start) {
        return false;
    }
    if (self->duration != kForever && tick >= Plus(start, self->duration)) {
        return false;
    }
    std::int32_t x = 0, y = 0;
    using PositionAtFn = void(__fastcall*)(MapEffect*, void*, std::int32_t, std::int32_t*, std::int32_t*);
    reinterpret_cast<PositionAtFn>(kMapEffectPositionAt)(self, nullptr, tick, &x, &y);
    return Plus(x, kVisibleMargin) >= left && Plus(y, kVisibleMargin) >= top && Plus(x, -kVisibleMargin) < right &&
           Plus(y, -kVisibleMargin) < bottom;
}

S3RE_HOOK(0x005442c0, MapEffects_Construct, TableFn, Original_MapEffects_Construct, MapEffects_Construct);
S3RE_HOOK(0x005441c0, MapEffect_TileAt, TileAtFn, Original_MapEffect_TileAt, MapEffect_TileAt);
S3RE_HOOK(0x00544030, MapEffect_IsVisibleIn, VisibleFn, Original_MapEffect_IsVisibleIn, MapEffect_IsVisibleIn);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kRecordBytes = 0x60;  // a record and guard bytes

// A record with a start tick, a positive or negative duration (or -1) and
// tile or pixel positions; the tick around its lifetime.
std::int32_t RandomRecord(test::Context& ctx, std::uint8_t* bytes) {
    ctx.rng().Fill(bytes, kRecordBytes);
    auto* record = reinterpret_cast<MapEffect*>(bytes);
    const std::int32_t start = ctx.rng().Range(-5, 1000);
    record->start_tick = static_cast<std::uint32_t>(start);
    const std::uint32_t kind = ctx.rng().Below(10);
    record->duration = static_cast<std::int16_t>(kind == 0 ? kForever : kind == 1 ? -ctx.rng().Range(2, 50) : ctx.rng().Range(1, 200));
    for (std::uint32_t* field : {&record->start_tile_x, &record->start_tile_y, &record->end_tile_x, &record->end_tile_y}) {
        *field = ctx.rng().Chance(1, 10) ? ctx.rng().Next32() : ctx.rng().Below(400);
    }
    for (std::int32_t* field : {&record->start_x, &record->start_y, &record->end_x, &record->end_y}) {
        *field = ctx.rng().Chance(1, 10) ? static_cast<std::int32_t>(ctx.rng().Next32()) : ctx.rng().Range(-500, 12000);
    }
    record->arc = static_cast<std::int16_t>(ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Range(-100, 300));
    const std::int32_t duration = record->duration == kForever ? 300 : record->duration;
    return start + ctx.rng().Range(-10, duration + 10);
}

void CompareTileAt(test::Context& ctx, TileAtFn candidate, int cases) {
    std::uint8_t a[kRecordBytes], b[kRecordBytes];
    for (int i = 0; i < cases; ++i) {
        const std::int32_t tick = RandomRecord(ctx, a);
        if (reinterpret_cast<MapEffect*>(a)->duration == kForever && ctx.rng().Chance(1, 2)) {
            reinterpret_cast<MapEffect*>(a)->duration = 7;
        }
        std::memcpy(b, a, sizeof(a));
        // Outputs: two slots after the record, or record fields (aliasing).
        // (Not the duration: the y division would divide by what *x stored.)
        static const std::size_t kSlots[] = {0x4c, 0x50, offsetof(MapEffect, start_tick), offsetof(MapEffect, start_tile_x),
                                             offsetof(MapEffect, end_tile_y), offsetof(MapEffect, start_tile_y)};
        const std::size_t sx = kSlots[ctx.rng().Chance(3, 4) ? 0 : ctx.rng().Below(6)];
        const std::size_t sy = kSlots[ctx.rng().Chance(3, 4) ? 1 : ctx.rng().Below(6)];
        const std::int32_t ra = Original_MapEffect_TileAt(reinterpret_cast<MapEffect*>(a), nullptr, static_cast<std::uint32_t>(tick),
                                                          reinterpret_cast<std::int32_t*>(a + sx), reinterpret_cast<std::int32_t*>(a + sy));
        const std::int32_t rb = candidate(reinterpret_cast<MapEffect*>(b), nullptr, static_cast<std::uint32_t>(tick),
                                          reinterpret_cast<std::int32_t*>(b + sx), reinterpret_cast<std::int32_t*>(b + sy));
        if (!S3RE_EXPECT_EQ(ra, rb) || !S3RE_EXPECT_MEM_EQ(a, b, sizeof(a))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: tick %d, slots %u %u", i, tick, static_cast<unsigned>(sx),
                     static_cast<unsigned>(sy));
            return;
        }
    }
}

void CompareIsVisible(test::Context& ctx, VisibleFn candidate, int cases) {
    std::uint8_t a[kRecordBytes], b[kRecordBytes];
    int visible = 0;
    for (int i = 0; i < cases; ++i) {
        const std::int32_t tick = RandomRecord(ctx, a);
        std::memcpy(b, a, sizeof(a));
        // A view around the effect's path (often near its start).
        const MapEffect* record = reinterpret_cast<const MapEffect*>(a);
        const bool around = ctx.rng().Chance(2, 3);
        const std::int32_t left = around ? record->start_x + ctx.rng().Range(-700, 100) : ctx.rng().Range(-600, 12000);
        const std::int32_t top = around ? record->start_y + ctx.rng().Range(-500, 100) : ctx.rng().Range(-600, 12000);
        const std::int32_t right = left + ctx.rng().Range(-10, 1200), bottom = top + ctx.rng().Range(-10, 900);
        const bool ra = Original_MapEffect_IsVisibleIn(reinterpret_cast<MapEffect*>(a), nullptr, tick, left, top, right, bottom);
        const bool rb = candidate(reinterpret_cast<MapEffect*>(b), nullptr, tick, left, top, right, bottom);
        visible += ra ? 1 : 0;
        if (!S3RE_EXPECT_EQ(ra, rb) || !S3RE_EXPECT_MEM_EQ(a, b, sizeof(a))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: tick %d, view (%d, %d)..(%d, %d)", i, tick, left, top, right, bottom);
            return;
        }
    }
    S3RE_EXPECT(cases < 1000 || visible > cases / 100);  // the inputs reach the visible case
}

// Wrong: the end tile is used for x at the start too.
std::int32_t __fastcall WrongTileAt(MapEffect* self, void* edx, std::uint32_t tick, std::int32_t* x, std::int32_t* y) {
    const std::int32_t r = MapEffect_TileAt(self, edx, tick, x, y);
    if (tick == self->start_tick) {
        *x = static_cast<std::int32_t>(self->end_tile_x);
    }
    return r;
}

// Wrong: an effect is visible until one tick after it ends.
bool __fastcall WrongIsVisibleIn(MapEffect* self, void* edx, std::int32_t tick, std::int32_t left, std::int32_t top,
                                 std::int32_t right, std::int32_t bottom) {
    if (self->duration != kForever && tick == static_cast<std::int32_t>(self->start_tick) + self->duration) {
        return MapEffect_IsVisibleIn(self, edx, tick - 1, left, top, right, bottom);
    }
    return MapEffect_IsVisibleIn(self, edx, tick, left, top, right, bottom);
}

void CompareConstruct(test::Context& ctx, TableFn candidate, int cases) {
    // Whole tables plus guard bytes.
    static std::uint8_t a[sizeof(MapEffectTable) + 16], b[sizeof(MapEffectTable) + 16];
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(a, sizeof(a));
        std::memcpy(b, a, sizeof(a));
        MapEffectTable* ra = Original_MapEffects_Construct(reinterpret_cast<MapEffectTable*>(a), nullptr);
        MapEffectTable* rb = candidate(reinterpret_cast<MapEffectTable*>(b), nullptr);
        if (!S3RE_EXPECT(ra == reinterpret_cast<MapEffectTable*>(a) && rb == reinterpret_cast<MapEffectTable*>(b)) ||
            !S3RE_EXPECT_MEM_EQ(a, b, sizeof(a))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

// Wrong: the last record is left alone.
MapEffectTable* __fastcall WrongConstruct(MapEffectTable* self, void* edx) {
    MapEffect last;
    std::memcpy(&last, &self->records[4999], sizeof(last));
    MapEffectTable* r = MapEffects_Construct(self, edx);
    std::memcpy(&self->records[4999], &last, sizeof(last));
    return r;
}

}  // namespace

S3RE_TEST(MapEffect_TileAt_MatchesOriginal) {
    CompareTileAt(ctx, &MapEffect_TileAt, 50000);
}
S3RE_TEST(MapEffect_TileAt_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareTileAt(probe, &WrongTileAt, 20000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(MapEffect_IsVisibleIn_MatchesOriginal) {
    CompareIsVisible(ctx, &MapEffect_IsVisibleIn, 50000);
}
S3RE_TEST(MapEffect_IsVisibleIn_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareIsVisible(probe, &WrongIsVisibleIn, 50000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(MapEffects_Construct_MatchesOriginal) {
    CompareConstruct(ctx, &MapEffects_Construct, 5);
}
S3RE_TEST(MapEffects_Construct_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareConstruct(probe, &WrongConstruct, 2);
    S3RE_EXPECT(probe.failures() > 0);
}


// MapEffect::MakeDrawRecord (0x00544100): (LogicRecord24* out, tick, brightness) thiscall on a record,
//   ret 0xc; called by MapEffects::CollectVisible for every visible effect. The pixel position at `tick`
//   (PositionAt), the tile (TileAt), the frame (with a graphic: first_frame + (tick - start) %
//   anim_period * frame_count / anim_period, signed; without one (-1) 0) and the brightness (the record's
//   own if non-zero, else the argument) go into a LogicRecord24 (pixel x, pixel y, tile x, tile y, graphic,
//   frame, byte +0x38, brightness, dword +0x34), built on the stack by its constructor 0x5437e0 and copied
//   to *out. A duration of -1 is set to 0 on the way (the original writes the record). EAX = out.

namespace {

constexpr std::uintptr_t kMapEffectTileAt = 0x005441c0;
constexpr std::uintptr_t kDrawRecordConstruct = 0x005437e0;  // LogicRecord24::LogicRecord24 (a1..a9)
constexpr std::uint32_t kNoGraphic = 0xffffffffu;

struct DrawRecord {
    std::uint32_t values[9];
};
static_assert(sizeof(DrawRecord) == 0x24);

}  // namespace

using MakeDrawRecordFn = DrawRecord*(__fastcall*)(MapEffect* self, void* edx, DrawRecord* out, std::int32_t tick,
                                                  std::uint32_t brightness);

// Purpose: Describes a visible map effect for the draw thread (where to draw
// which frame of its graphic, how bright).
DrawRecord* __fastcall MapEffect_MakeDrawRecord(MapEffect* self, void*, DrawRecord* out, std::int32_t tick,
                                                std::uint32_t brightness) {
    std::int32_t pixelX = 0, pixelY = 0, tileX = 0, tileY = 0;
    using PositionAtFn = void(__fastcall*)(MapEffect*, void*, std::int32_t, std::int32_t*, std::int32_t*);
    reinterpret_cast<PositionAtFn>(kMapEffectPositionAt)(self, nullptr, tick, &pixelX, &pixelY);
    using TileAtFn = std::int32_t(__fastcall*)(MapEffect*, void*, std::uint32_t, std::int32_t*, std::int32_t*);
    reinterpret_cast<TileAtFn>(kMapEffectTileAt)(self, nullptr, static_cast<std::uint32_t>(tick), &tileX, &tileY);
    const std::uint32_t graphic = self->graphic;
    std::int32_t frame = 0;
    if (graphic != kNoGraphic) {
        const auto period = static_cast<std::int32_t>(static_cast<std::int16_t>(self->anim_period));
        const auto elapsed = static_cast<std::int32_t>(static_cast<std::uint32_t>(tick) - self->start_tick);
        const auto scaled = static_cast<std::int32_t>(static_cast<std::uint32_t>(elapsed % period) *
                                                      static_cast<std::uint32_t>(static_cast<std::int16_t>(self->frame_count)));
        frame = static_cast<std::int32_t>(static_cast<std::uint32_t>(scaled / period) +
                                          static_cast<std::uint32_t>(static_cast<std::int16_t>(self->first_frame)));
    }
    if (self->duration == kForever) {
        self->duration = 0;
    }
    const std::uint32_t shade = self->brightness != 0 ? self->brightness : brightness;
    DrawRecord record;
    using ConstructFn = DrawRecord*(__fastcall*)(DrawRecord*, void*, std::uint32_t, std::uint32_t, std::uint32_t,
                                                 std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                                 std::uint32_t, std::uint32_t);
    const DrawRecord* built = reinterpret_cast<ConstructFn>(kDrawRecordConstruct)(
        &record, nullptr, static_cast<std::uint32_t>(pixelX), static_cast<std::uint32_t>(pixelY),
        static_cast<std::uint32_t>(tileX), static_cast<std::uint32_t>(tileY), graphic, static_cast<std::uint32_t>(frame),
        self->unk38, shade, self->unk34);
    *out = *built;
    return out;
}

S3RE_HOOK(0x00544100, MapEffect_MakeDrawRecord, MakeDrawRecordFn, Original_MapEffect_MakeDrawRecord,
          MapEffect_MakeDrawRecord);

namespace {

void CompareMakeDrawRecord(test::Context& ctx, MakeDrawRecordFn candidate, int cases) {
    std::uint8_t a[kRecordBytes], b[kRecordBytes];
    DrawRecord outA[3], outB[3];
    for (int i = 0; i < cases; ++i) {
        const std::int32_t tick = RandomRecord(ctx, a);
        auto* record = reinterpret_cast<MapEffect*>(a);
        if (record->duration == 0) {
            record->duration = 1;
        }
        record->graphic = ctx.rng().Chance(1, 4) ? kNoGraphic : ctx.rng().Next32();
        record->anim_period = static_cast<std::uint16_t>(ctx.rng().Chance(1, 10) ? 0x8000 + ctx.rng().Below(0x7fff) : 1 + ctx.rng().Below(64));
        record->brightness = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Next32());
        std::memcpy(b, a, sizeof(a));
        ctx.rng().Fill(outA, sizeof(outA));
        std::memcpy(outB, outA, sizeof(outA));
        const std::uint32_t brightness = ctx.rng().Next32();
        DrawRecord* ra = Original_MapEffect_MakeDrawRecord(reinterpret_cast<MapEffect*>(a), nullptr, &outA[1], tick, brightness);
        DrawRecord* rb = candidate(reinterpret_cast<MapEffect*>(b), nullptr, &outB[1], tick, brightness);
        if (!S3RE_EXPECT(ra == &outA[1] && rb == &outB[1]) || !S3RE_EXPECT_MEM_EQ(a, b, sizeof(a)) ||
            !S3RE_EXPECT_MEM_EQ(outA, outB, sizeof(outA))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: tick %d", i, tick);
            return;
        }
    }
}

// Wrong: the record's own brightness is ignored.
DrawRecord* __fastcall WrongMakeDrawRecord(MapEffect* self, void* edx, DrawRecord* out, std::int32_t tick,
                                           std::uint32_t brightness) {
    const std::uint8_t own = self->brightness;
    self->brightness = 0;
    DrawRecord* r = MapEffect_MakeDrawRecord(self, edx, out, tick, brightness);
    self->brightness = own;
    return r;
}

}  // namespace

S3RE_TEST(MapEffect_MakeDrawRecord_MatchesOriginal) {
    CompareMakeDrawRecord(ctx, &MapEffect_MakeDrawRecord, 30000);
}
S3RE_TEST(MapEffect_MakeDrawRecord_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMakeDrawRecord(probe, &WrongMakeDrawRecord, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
