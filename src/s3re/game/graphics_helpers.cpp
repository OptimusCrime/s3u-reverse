// Small helpers of the graphics side (Dirk\graphicsdata.cpp and its
// neighbour, Volker\DrawBuilding.cpp) and a record constructor of
// Volker\LogicInit.cpp.
//
// Render_PackColour (0x004c7b00): (c1, c2, c3) stdcall, ret 0xc. Converts a
// colour into the screen's pixel format with per-channel scale factors and
// shifts set up for the display mode (globals 0x7dfba0..0x7dfbb4):
//   ((c1 * [0x7dfbb4]) >> 8 << [0x7dfba8]) + ((c2 * [0x7dfbb0]) >> 8 <<
//   [0x7dfba4]) + ((c3 * [0x7dfbac]) >> 8 << [0x7dfba0])
// (32-bit products, logical shifts, shift counts mod 32).
//
// GfxData_SpriteWord (0x005fad20): (id) thiscall, ret 4. id = group << 16 |
// index; reads the pointer at [[this+0xb24+group*4] + 8 + index*4] and
// returns the word at that pointer + [this+0x264+group*4] (a per-group
// field offset). Only AX is the result; the upper half of EAX is the upper
// half of the offset dword, reproduced.
//
// GfxData_SetDword (0x005fc540): (value) thiscall: +0 = value; EAX = this.
// ret 4. Point16_Construct (0x005fdab0): (x, y) thiscall: word +0 = x, word
// +2 = y; EAX = this. ret 8 (19 callers; a 16-bit point or size).
//
// LogicRecord24_Construct (0x005437b0): thiscall: nine dwords, all 0 except
// +0x10 = -1; EAX = this. ret. 100 of them are built at +0xc690c of the
// object constructed by 0x523480.

#include "game/framework_layout.h"
#include "game/raw.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace s3re::game {

namespace {

using framework::GfxData;

template <typename T>
T& At(std::uintptr_t address) {
    return RawAtAddress<T>(address);
}

template <typename T>
T* As(void* self) {
    return static_cast<T*>(self);
}

#pragma pack(push, 1)
// A one-dword graphics record (0x5fc540).
struct GfxDword {
    std::uint32_t value;  // +0x00
};

// A pair of 16-bit values (0x5fdab0; probably a point or size).
struct Point16 {
    std::uint16_t x;  // +0x00
    std::uint16_t y;  // +0x02
};
static_assert(sizeof(Point16) == 4);

// The 0x24-byte record of the logic set-up (0x5437b0).
struct LogicRecord24 {
    std::uint32_t unk00;  // +0x00
    std::uint32_t unk04;  // +0x04
    std::uint32_t unk08;  // +0x08
    std::uint32_t unk0c;  // +0x0c
    std::uint32_t unk10;  // +0x10 -1 ("none") after construction
    std::uint32_t unk14;  // +0x14
    std::uint32_t unk18;  // +0x18
    std::uint32_t unk1c;  // +0x1c
    std::uint32_t unk20;  // +0x20
};
static_assert(sizeof(LogicRecord24) == 0x24);

// The drawing object of Render_PutPixel16 / Render_Entry204 (class not
// identified).
struct DrawTarget {
    std::uint8_t unk000[0x90];
    std::uint32_t pitch;          // +0x90 in bytes
    std::uint8_t unk094[0xa4 - 0x94];
    std::uint32_t pixels;         // +0xa4 the 16-bit pixel buffer
    std::uint8_t unk0a8[0x204 - 0xa8];
    std::uint32_t* table204;      // +0x204 meaning unknown
};
static_assert(offsetof(DrawTarget, pitch) == 0x90);
static_assert(offsetof(DrawTarget, pixels) == 0xa4);
static_assert(offsetof(DrawTarget, table204) == 0x204);
#pragma pack(pop)

constexpr std::uintptr_t kColourShift3 = 0x007dfba0;
constexpr std::uintptr_t kColourShift2 = 0x007dfba4;
constexpr std::uintptr_t kColourShift1 = 0x007dfba8;
constexpr std::uintptr_t kColourScale3 = 0x007dfbac;
constexpr std::uintptr_t kColourScale2 = 0x007dfbb0;
constexpr std::uintptr_t kColourScale1 = 0x007dfbb4;

}  // namespace

using PackColourFn = std::uint32_t(__stdcall*)(std::uint32_t c1, std::uint32_t c2, std::uint32_t c3);
using SpriteWordFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t id);
using SetDwordFn = void*(__fastcall*)(void* self, void* edx, std::uint32_t value);
using Point16Fn = void*(__fastcall*)(void* self, void* edx, std::uint32_t x, std::uint32_t y);
using Record24Fn = void*(__fastcall*)(void* self, void* edx);

std::uint32_t __stdcall Render_PackColour(std::uint32_t c1, std::uint32_t c2, std::uint32_t c3);
std::uint32_t __fastcall GfxData_SpriteWord(void* self, void* edx, std::uint32_t id);
void* __fastcall GfxData_SetDword(void* self, void* edx, std::uint32_t value);
void* __fastcall Point16_Construct(void* self, void* edx, std::uint32_t x, std::uint32_t y);
void* __fastcall LogicRecord24_Construct(void* self, void* edx);

S3RE_HOOK(0x004c7b00, Render_PackColour, PackColourFn, Original_Render_PackColour, Render_PackColour);

// Purpose: Probably converts a colour (three channels, 0..255) into a pixel
// value for the current screen mode, e.g. for the minimap or overlays.
std::uint32_t __stdcall Render_PackColour(std::uint32_t c1, std::uint32_t c2, std::uint32_t c3) {
    const std::uint32_t part3 = ((c3 * At<std::uint32_t>(kColourScale3)) >> 8) << (At<std::uint32_t>(kColourShift3) & 31);
    const std::uint32_t part2 = ((c2 * At<std::uint32_t>(kColourScale2)) >> 8) << (At<std::uint32_t>(kColourShift2) & 31);
    const std::uint32_t part1 = ((c1 * At<std::uint32_t>(kColourScale1)) >> 8) << (At<std::uint32_t>(kColourShift1) & 31);
    return part3 + part2 + part1;
}

S3RE_HOOK(0x005fad20, GfxData_SpriteWord, SpriteWordFn, Original_GfxData_SpriteWord, GfxData_SpriteWord);

// Purpose: Reads a 16-bit value (probably a size or offset) from the header
// of a graphic, addressed by group and index, in the loaded graphics data.
std::uint32_t __fastcall GfxData_SpriteWord(void* self, void* /*edx*/, std::uint32_t id) {
    const std::uint32_t group = id >> 16;
    const std::uint32_t index = id & 0xffff;
    GfxData* const gfx = As<GfxData>(self);
    const std::uint32_t table = gfx->spriteTables[group];
    const std::uint32_t offset = gfx->groupBase[group];
    const std::uint32_t entry = At<std::uint32_t>(table + 8 + index * 4);
    return (offset & 0xffff0000u) | At<std::uint16_t>(entry + offset);
}

S3RE_HOOK(0x005fc540, GfxData_SetDword, SetDwordFn, Original_GfxData_SetDword, GfxData_SetDword);

// Purpose: Constructor of a one-field graphics record (probably a handle or
// pointer wrapper).
void* __fastcall GfxData_SetDword(void* self, void* /*edx*/, std::uint32_t value) {
    As<GfxDword>(self)->value = value;
    return self;
}

S3RE_HOOK(0x005fdab0, Point16_Construct, Point16Fn, Original_Point16_Construct, Point16_Construct);

// Purpose: Constructor of a pair of 16-bit values (probably a point or size
// on the screen).
void* __fastcall Point16_Construct(void* self, void* /*edx*/, std::uint32_t x, std::uint32_t y) {
    As<Point16>(self)->x = static_cast<std::uint16_t>(x);
    As<Point16>(self)->y = static_cast<std::uint16_t>(y);
    return self;
}

S3RE_HOOK(0x005437b0, LogicRecord24_Construct, Record24Fn, Original_LogicRecord24_Construct,
          LogicRecord24_Construct);

// Purpose: Constructor of a 0x24-byte record of the logic set-up (meaning
// unknown; its field +0x10 starts as "none", -1).
void* __fastcall LogicRecord24_Construct(void* self, void* /*edx*/) {
    LogicRecord24* const record = As<LogicRecord24>(self);
    record->unk00 = 0;
    record->unk04 = 0;
    record->unk08 = 0;
    record->unk0c = 0;
    record->unk10 = 0xffffffffu;
    record->unk14 = 0;
    record->unk18 = 0;
    record->unk1c = 0;
    record->unk20 = 0;
    return self;
}

// Batch 25.
//
// GfxData_FrameOffset (0x005faaa0): (id, variant) thiscall, ret 8. id =
//   group << 16 | frame; variant -1 means the object's current one
//   (+0xc40). table = [this+0x37c+group*4]; returns the dword at table +
//   0xc + (word table+8 * variant + frame) * 4 plus [this+0x264+group*4]
//   (the group's base offset). 78 callers.
// Render_PutPixel16 (0x004c7ac0): (x, y, colour) thiscall, ret 0xc: the
//   16-bit pixel at ((pitch +0x90) / 2) * (y + [0x730f40]) + [0x730f3c] + x
//   of the pixel buffer +0xa4 = colour (16-bit screen modes; the two
//   globals are the drawing origin). EAX = the pixel index.
// Render_Entry204 (0x004c7ab0): (i) thiscall, ret 4: dword i of the table
//   at [this+0x204].

using FrameOffsetFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t id, std::uint32_t variant);
using PutPixelFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t x, std::uint32_t y,
                                              std::uint32_t colour);
using Entry204Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t i);

std::uint32_t __fastcall GfxData_FrameOffset(void* self, void* edx, std::uint32_t id, std::uint32_t variant);
std::uint32_t __fastcall Render_PutPixel16(void* self, void* edx, std::uint32_t x, std::uint32_t y,
                                           std::uint32_t colour);
std::uint32_t __fastcall Render_Entry204(void* self, void* edx, std::uint32_t i);

namespace {
constexpr std::uintptr_t kDrawOriginX = 0x00730f3c;
constexpr std::uintptr_t kDrawOriginY = 0x00730f40;
}  // namespace

S3RE_HOOK(0x005faaa0, GfxData_FrameOffset, FrameOffsetFn, Original_GfxData_FrameOffset, GfxData_FrameOffset);

// Purpose: Where a frame of a graphics group starts in the loaded graphics
// data (probably the offset of a sprite's image in the GFX file), for one
// variant (e.g. a player colour or direction) or the current one.
std::uint32_t __fastcall GfxData_FrameOffset(void* self, void* /*edx*/, std::uint32_t id, std::uint32_t variant) {
    const std::uint32_t group = id >> 16;
    GfxData* const gfx = As<GfxData>(self);
    if (variant == 0xffffffffu) {
        variant = gfx->currentVariant;
    }
    // The frame table (layout not mapped): word +8 frames per variant, dword
    // offsets from +0xc.
    auto* table = reinterpret_cast<std::uint8_t*>(gfx->frameTables[group]);
    const std::uint32_t index = RawAt<std::uint16_t>(table, 8) * variant + (id & 0xffff);
    return RawAt<std::uint32_t>(table, 0xc + index * 4) + gfx->groupBase[group];
}

S3RE_HOOK(0x004c7ac0, Render_PutPixel16, PutPixelFn, Original_Render_PutPixel16, Render_PutPixel16);

// Purpose: Sets one pixel of a 16-bit drawing surface, relative to the
// current drawing origin (probably for the minimap or small markers).
std::uint32_t __fastcall Render_PutPixel16(void* self, void* /*edx*/, std::uint32_t x, std::uint32_t y,
                                           std::uint32_t colour) {
    DrawTarget* const target = As<DrawTarget>(self);
    const std::uint32_t index =
        (target->pitch >> 1) * (y + At<std::uint32_t>(kDrawOriginY)) + At<std::uint32_t>(kDrawOriginX) + x;
    *reinterpret_cast<std::uint16_t*>(target->pixels + index * 2) = static_cast<std::uint16_t>(colour);
    return index;
}

S3RE_HOOK(0x004c7ab0, Render_Entry204, Entry204Fn, Original_Render_Entry204, Render_Entry204);

// Purpose: Reads entry i of a table of the drawing object (+0x204; meaning
// unknown, probably per-type graphics ids or offsets).
std::uint32_t __fastcall Render_Entry204(void* self, void* /*edx*/, std::uint32_t i) {
    return As<DrawTarget>(self)->table204[i];
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

struct GraphicsFunctions {
    PackColourFn packColour;
    SpriteWordFn spriteWord;
    SetDwordFn setDword;
    Point16Fn point16;
    Record24Fn record24;
};

void CompareGraphics(test::Context& ctx, const GraphicsFunctions& candidate, int cases) {
    const GraphicsFunctions original{Original_Render_PackColour, Original_GfxData_SpriteWord,
                                     Original_GfxData_SetDword, Original_Point16_Construct,
                                     Original_LogicRecord24_Construct};
    std::uint32_t saved[6];
    std::memcpy(saved, reinterpret_cast<void*>(kColourShift3), sizeof(saved));
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(5));
        bool same = true;
        if (op == 0) {
            std::uint32_t globals[6];
            for (int g = 0; g < 3; ++g) {
                globals[g] = ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(24);
                globals[g + 3] = ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(0x200);
            }
            std::memcpy(reinterpret_cast<void*>(kColourShift3), globals, sizeof(globals));
            const std::uint32_t c1 = ctx.rng().Chance(1, 4) ? ctx.rng().Next32() : ctx.rng().Below(256);
            const std::uint32_t c2 = ctx.rng().Chance(1, 4) ? ctx.rng().Next32() : ctx.rng().Below(256);
            const std::uint32_t c3 = ctx.rng().Chance(1, 4) ? ctx.rng().Next32() : ctx.rng().Below(256);
            same &= S3RE_EXPECT_EQ(original.packColour(c1, c2, c3), candidate.packColour(c1, c2, c3));
        } else if (op == 1) {
            // A graphics object with group tables pointing into one buffer.
            static std::uint8_t object[0x1000];
            static std::uint8_t data[0x4000];
            ctx.rng().Fill(object, sizeof(object));
            ctx.rng().Fill(data, sizeof(data));
            const std::uint32_t group = ctx.rng().Below(0xb0);
            const std::uint32_t index = ctx.rng().Below(0x100);
            static_assert(sizeof(object) >= sizeof(GfxData));
            As<GfxData>(object)->spriteTables[group] = reinterpret_cast<std::uint32_t>(data);
            // Any offset (its upper half shows in EAX); the entry points so
            // that entry + offset lands in the buffer.
            const std::uint32_t offset = ctx.rng().Next32();
            As<GfxData>(object)->groupBase[group] = offset;
            RawAt<std::uint32_t>(data, 8 + index * 4) =  // the sprite table's entries start at +8
                reinterpret_cast<std::uint32_t>(data) + 0x1000 + ctx.rng().Below(0x1000) - offset;
            same &= S3RE_EXPECT_EQ(original.spriteWord(object, nullptr, group << 16 | index),
                                   candidate.spriteWord(object, nullptr, group << 16 | index));
        } else {
            std::uint8_t a[0x40], b[0x40];
            ctx.rng().Fill(a, sizeof(a));
            std::memcpy(b, a, sizeof(a));
            const std::uint32_t x = ctx.rng().Next32(), y = ctx.rng().Next32();
            void* ra = nullptr;
            void* rb = nullptr;
            if (op == 2) {
                ra = original.setDword(a + 8, nullptr, x);
                rb = candidate.setDword(b + 8, nullptr, x);
            } else if (op == 3) {
                ra = original.point16(a + 8, nullptr, x, y);
                rb = candidate.point16(b + 8, nullptr, x, y);
            } else {
                ra = original.record24(a + 8, nullptr);
                rb = candidate.record24(b + 8, nullptr);
            }
            same &= S3RE_EXPECT(ra == a + 8 && rb == b + 8);
            same &= S3RE_EXPECT_MEM_EQ(a, b, sizeof(a));
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
            break;
        }
    }
    std::memcpy(reinterpret_cast<void*>(kColourShift3), saved, sizeof(saved));
}

// Wrong: arithmetic instead of logical shift after the scaling.
std::uint32_t __stdcall WrongPackColour(std::uint32_t c1, std::uint32_t c2, std::uint32_t c3) {
    const auto scaled = static_cast<std::uint32_t>(static_cast<std::int32_t>(c1 * At<std::uint32_t>(kColourScale1)) >> 8);
    return Render_PackColour(0, c2, c3) + (scaled << (At<std::uint32_t>(kColourShift1) & 31));
}

}  // namespace

S3RE_TEST(GraphicsHelpers_MatchOriginal) {
    CompareGraphics(ctx, {&Render_PackColour, &GfxData_SpriteWord, &GfxData_SetDword, &Point16_Construct,
                          &LogicRecord24_Construct},
                    50000);
}

S3RE_TEST(GraphicsHelpers_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareGraphics(probe, {&WrongPackColour, &GfxData_SpriteWord, &GfxData_SetDword, &Point16_Construct,
                            &LogicRecord24_Construct},
                    5000);
    S3RE_EXPECT(probe.failures() > 0);
}

// Batch 25 functions.
namespace {

struct Graphics25Functions {
    FrameOffsetFn frameOffset;
    PutPixelFn putPixel;
    Entry204Fn entry204;
};

void CompareGraphics25(test::Context& ctx, const Graphics25Functions& candidate, int cases) {
    const Graphics25Functions original{Original_GfxData_FrameOffset, Original_Render_PutPixel16,
                                       Original_Render_Entry204};
    const std::uint32_t savedX = At<std::uint32_t>(kDrawOriginX), savedY = At<std::uint32_t>(kDrawOriginY);
    static std::uint8_t a[0x8000], b[0x8000];
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(3));
        ctx.rng().Fill(a, sizeof(a));
        std::uint32_t ra = 0, rb = 0;
        if (op == 0) {
            // Group tables at +0x1000 + group * 0x100 (inside the buffer).
            const std::uint32_t group = ctx.rng().Below(0xb0);
            const std::uint32_t frames = ctx.rng().Below(8);
            const std::uint32_t frame = ctx.rng().Below(8);
            std::uint32_t variant = ctx.rng().Chance(1, 3) ? 0xffffffffu : ctx.rng().Below(4);
            As<GfxData>(a)->currentVariant = ctx.rng().Below(4);
            std::memcpy(b, a, sizeof(a));
            for (std::uint8_t* side : std::initializer_list<std::uint8_t*>{a, b}) {
                std::uint8_t* table = side + 0x1000 + group * 0x40;
                As<GfxData>(side)->frameTables[group] = reinterpret_cast<std::uint32_t>(table);
                RawAt<std::uint16_t>(table, 8) = static_cast<std::uint16_t>(frames);  // frames per variant
            }
            ra = original.frameOffset(a, nullptr, group << 16 | frame, variant);
            rb = candidate.frameOffset(b, nullptr, group << 16 | frame, variant);
        } else if (op == 1) {
            At<std::uint32_t>(kDrawOriginX) = ctx.rng().Below(32);
            At<std::uint32_t>(kDrawOriginY) = ctx.rng().Below(32);
            As<DrawTarget>(a)->pitch = 2 * (32 + ctx.rng().Below(32));
            const std::uint32_t x = ctx.rng().Below(64), y = ctx.rng().Below(64);
            const std::uint32_t colour = ctx.rng().Next32();
            std::memcpy(b, a, sizeof(a));
            As<DrawTarget>(a)->pixels = reinterpret_cast<std::uint32_t>(a + 0x100);
            As<DrawTarget>(b)->pixels = reinterpret_cast<std::uint32_t>(b + 0x100);
            ra = original.putPixel(a, nullptr, x, y, colour);
            rb = candidate.putPixel(b, nullptr, x, y, colour);
        } else {
            std::memcpy(b, a, sizeof(a));
            As<DrawTarget>(a)->table204 = reinterpret_cast<std::uint32_t*>(a + 0x400);
            As<DrawTarget>(b)->table204 = reinterpret_cast<std::uint32_t*>(b + 0x400);
            const std::uint32_t index = ctx.rng().Below(0x100);
            ra = original.entry204(a, nullptr, index);
            rb = candidate.entry204(b, nullptr, index);
        }
        // The pointers set above point into each side's own buffer.
        As<DrawTarget>(b)->pixels = As<DrawTarget>(a)->pixels;
        As<DrawTarget>(b)->table204 = As<DrawTarget>(a)->table204;
        for (std::uint32_t group = 0; group < 0xb0; ++group) {  // past the 70 groups, like the cases above
            if (As<GfxData>(a)->frameTables[group] == reinterpret_cast<std::uint32_t>(a + 0x1000 + group * 0x40)) {
                As<GfxData>(b)->frameTables[group] = As<GfxData>(a)->frameTables[group];
            }
        }
        bool same = S3RE_EXPECT_EQ(ra, rb);
        same &= S3RE_EXPECT_MEM_EQ(a, b, sizeof(a));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
            break;
        }
    }
    At<std::uint32_t>(kDrawOriginX) = savedX;
    At<std::uint32_t>(kDrawOriginY) = savedY;
}

// Wrong: ignores the "current variant" default.
std::uint32_t __fastcall WrongFrameOffset(void* self, void* edx, std::uint32_t id, std::uint32_t variant) {
    return GfxData_FrameOffset(self, edx, id, variant == 0xffffffffu ? 0 : variant);
}

}  // namespace

S3RE_TEST(GraphicsHelpers25_MatchOriginal) {
    CompareGraphics25(ctx, {&GfxData_FrameOffset, &Render_PutPixel16, &Render_Entry204}, 30000);
}

S3RE_TEST(GraphicsHelpers25_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareGraphics25(probe, {&WrongFrameOffset, &Render_PutPixel16, &Render_Entry204}, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
