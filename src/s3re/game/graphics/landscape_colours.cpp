// A colour table of the landscape drawing code (Volker\landscape.cpp).
//
// Landscape::FindOrAddColour (0x0054c940): ECX is the CLandscapeSurface
//   (its caller 0x54bec0 is called by CDrawer::InitSurfaces); its dword +0xa0
//   points at its table of uint16 colours (new(0x202) in the constructor
//   0x54bd30), slots 1..255 used (slot 0 never). (colour): 0 for colour 0
//   (only the low 16 bits of the argument count). Otherwise the first slot
//   1..255 holding the colour; if there is none, the colour is put into the
//   first empty (0) slot 1..255, or, when the table is full,
//   DebugLog("Error ! zu viele Farben" = too many colours) and it
//   overwrites slot 255. EAX = the slot. (The original's searches also read
//   slot 256, without effect.) thiscall, ret 4.

#include "game/debug_log.h"
#include "game/framework/framework_layout.h"
#include "game/texts.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using framework::CLandscapeSurface;

namespace {

constexpr std::uint32_t kSlots = 256;  // colours in the landscape colour table
constexpr std::uint32_t kLastSlot = kSlots - 1;

std::uint16_t* ColourTable(CLandscapeSurface* self) {
    return static_cast<std::uint16_t*>(self->colours);
}

}  // namespace

using FindOrAddColourFn = std::uint32_t(__fastcall*)(CLandscapeSurface* self, void* edx, std::uint32_t colour);

// Purpose: Builds a palette for drawing the landscape: returns the palette slot of a colour, adding the
// colour if it isn't in the palette yet (at most 255 colours).
std::uint32_t __fastcall Landscape_FindOrAddColour(CLandscapeSurface* self, void*, std::uint32_t colour) {
    const auto wanted = static_cast<std::uint16_t>(colour);
    if (wanted == 0) {
        return 0;
    }
    std::uint16_t* table = ColourTable(self);
    for (std::uint32_t slot = 1; slot < kSlots; ++slot) {
        if (table[slot] == wanted) {
            return slot;
        }
    }
    std::uint32_t slot = 1;
    while (slot < kSlots && table[slot] != 0) {
        ++slot;
    }
    if (slot == kSlots) {
        GameDebugLog(texts::kTooManyColours);
        slot = kLastSlot;  // the table is full: the last slot is overwritten
    }
    ColourTable(self)[slot] = wanted;
    return slot;
}

S3RE_HOOK(0x0054c940, Landscape_FindOrAddColour, FindOrAddColourFn, Original_Landscape_FindOrAddColour,
          Landscape_FindOrAddColour);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

// A landscape surface with only the table pointer set, and the table (257 slots: the
// original reads slot 256) with guard bytes after it.
struct ColourTest {
    CLandscapeSurface object = {};
    std::uint16_t table[kSlots + 1 + 16] = {};

    ColourTest() { object.colours = table; }
    CLandscapeSurface* self() { return &object; }
};

// Wrong: a full table overwrites slot 1 instead of 255.
std::uint32_t __fastcall WrongFindOrAddColour(CLandscapeSurface* self, void* edx, std::uint32_t colour) {
    std::uint16_t* table = ColourTable(self);
    const std::uint16_t last = table[0xff];
    const std::uint32_t slot = Landscape_FindOrAddColour(self, edx, colour);
    if (slot == 0xff && last != 0 && table[0xff] != last) {
        table[0xff] = last;
        table[1] = static_cast<std::uint16_t>(colour);
        return 1;
    }
    return slot;
}

void CompareColours(test::Context& ctx, FindOrAddColourFn candidate, int cases) {
    ColourTest a;
    ColourTest b;
    DebugLogRecorder logs;
    for (int i = 0; i < cases; ++i) {
        // Fill the slots 1..n with distinct-ish small colours, then zeros (or
        // a full table); random guard values in slot 0, 256 and after.
        const std::uint32_t filled = ctx.rng().Chance(1, 4) ? kSlots + 1 : ctx.rng().Below(kSlots + 1);
        for (std::uint32_t slot = 0; slot < kSlots + 1 + 16; ++slot) {
            std::uint16_t value = static_cast<std::uint16_t>(ctx.rng().Next32());
            if (slot >= 1 && slot < kSlots) {
                value = slot < filled ? static_cast<std::uint16_t>(1 + ctx.rng().Below(400)) : 0;
                if (slot >= filled && ctx.rng().Chance(1, 30)) {
                    value = static_cast<std::uint16_t>(1 + ctx.rng().Below(400));  // a gap filled again
                }
            }
            a.table[slot] = value;
        }
        std::memcpy(b.table, a.table, sizeof(a.table));
        std::uint32_t colour = ctx.rng().Chance(1, 2) ? 1 + ctx.rng().Below(500) : ctx.rng().Next32();
        if (ctx.rng().Chance(1, 20)) {
            colour = ctx.rng().Next32() & 0xffff0000u;  // low word 0
        }
        if (ctx.rng().Chance(1, 10)) {
            colour = a.table[kSlots] | (ctx.rng().Next32() & 0xffff0000u);  // only in slot 256
        }
        const std::uint32_t expected = Original_Landscape_FindOrAddColour(a.self(), nullptr, colour);
        const std::vector<DebugLogCall> expectedLogs = logs.Take();
        const std::uint32_t actual = candidate(b.self(), nullptr, colour);
        const std::vector<DebugLogCall> actualLogs = logs.Take();
        if (!S3RE_EXPECT_EQ(expected, actual) || !S3RE_EXPECT(expectedLogs == actualLogs) ||
            !ctx.ExpectMemEq(a.table, b.table, sizeof(a.table), "original table", "our table", __FILE__, __LINE__)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: colour %#x, %u slots filled", i, colour, filled);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(Landscape_FindOrAddColour_MatchesOriginal) {
    CompareColours(ctx, &Landscape_FindOrAddColour, 20000);
}
S3RE_TEST(Landscape_FindOrAddColour_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareColours(probe, &WrongFindOrAddColour, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
