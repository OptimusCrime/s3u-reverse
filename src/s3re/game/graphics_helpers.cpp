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

#include "hooks.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

template <typename T>
T& At(std::uintptr_t address) {
    return *reinterpret_cast<T*>(address);
}

template <typename T>
T& At(void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset);
}

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
    const std::uint32_t table = At<std::uint32_t>(self, 0xb24 + group * 4);
    const std::uint32_t offset = At<std::uint32_t>(self, 0x264 + group * 4);
    const std::uint32_t entry = At<std::uint32_t>(table + 8 + index * 4);
    return (offset & 0xffff0000u) | At<std::uint16_t>(entry + offset);
}

S3RE_HOOK(0x005fc540, GfxData_SetDword, SetDwordFn, Original_GfxData_SetDword, GfxData_SetDword);

// Purpose: Constructor of a one-field graphics record (probably a handle or
// pointer wrapper).
void* __fastcall GfxData_SetDword(void* self, void* /*edx*/, std::uint32_t value) {
    At<std::uint32_t>(self, 0) = value;
    return self;
}

S3RE_HOOK(0x005fdab0, Point16_Construct, Point16Fn, Original_Point16_Construct, Point16_Construct);

// Purpose: Constructor of a pair of 16-bit values (probably a point or size
// on the screen).
void* __fastcall Point16_Construct(void* self, void* /*edx*/, std::uint32_t x, std::uint32_t y) {
    At<std::uint16_t>(self, 0) = static_cast<std::uint16_t>(x);
    At<std::uint16_t>(self, 2) = static_cast<std::uint16_t>(y);
    return self;
}

S3RE_HOOK(0x005437b0, LogicRecord24_Construct, Record24Fn, Original_LogicRecord24_Construct,
          LogicRecord24_Construct);

// Purpose: Constructor of a 0x24-byte record of the logic set-up (meaning
// unknown; its field +0x10 starts as "none", -1).
void* __fastcall LogicRecord24_Construct(void* self, void* /*edx*/) {
    for (std::size_t offset = 0; offset < 0x24; offset += 4) {
        At<std::uint32_t>(self, offset) = offset == 0x10 ? 0xffffffffu : 0;
    }
    return self;
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
            At<std::uint32_t>(object, 0xb24 + group * 4) = reinterpret_cast<std::uint32_t>(data);
            // Any offset (its upper half shows in EAX); the entry points so
            // that entry + offset lands in the buffer.
            const std::uint32_t offset = ctx.rng().Next32();
            At<std::uint32_t>(object, 0x264 + group * 4) = offset;
            At<std::uint32_t>(data, 8 + index * 4) =
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

}  // namespace s3re::game
