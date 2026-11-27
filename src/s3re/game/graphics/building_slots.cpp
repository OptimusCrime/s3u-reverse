// CDrawer::BuildingAnimationSlot (0x004b1a00, Volker\DrawBuilding.cpp's
// range): thiscall (type) ret 4, `this` unused. A switch (jump table
// 0x4b1bc4) from a building type 1..56 to its g_raceAnimations slot: the
// shadow of the building's construction picture (the sprite is the next
// slot; the panel drawer 0x4b4d00 reads g_raceAnimations[slot + 1][race]).
// Every other value (0, 57 and above) is returned unchanged. The values
// equal the construction slots of CDrawer::QueueBuilding 0x4a9960
// (drawer_buildings.cpp), with types 54..56 using the slots of 46, 45 and
// 29.

#include "hooks.h"
#include "test/difftest.h"

#include <cstdint>

namespace s3re::game {

struct CDrawer;

namespace {

constexpr std::uint32_t kFirstType = 1;
constexpr std::uint32_t kLastType = 56;

// Indexed by type - 1 (0x4b1a15..0x4b1bb7).
constexpr std::uint16_t kSlots[kLastType] = {
    0,   2,   4,   6,   8,   10,  54,  56,  58,  60,  62,  64,  66,  135, 71,  73,  97,  75,  137,
    107, 95,  115, 101, 103, 105, 109, 111, 113, 117, 99,  119, 155, 157, 125, 127, 147, 143, 145,
    129, 149, 139, 141, 133, 131, 153, 151, 121, 123, 356, 402, 404, 406, 408, 151, 153, 117,
};

}  // namespace

using BuildingAnimationSlotFn = std::uint32_t(__fastcall*)(CDrawer* self, void* edx, std::uint32_t type);
std::uint32_t __fastcall CDrawer_BuildingAnimationSlot(CDrawer* self, void* edx, std::uint32_t type);

S3RE_HOOK(0x004b1a00, CDrawer_BuildingAnimationSlot, BuildingAnimationSlotFn, Original_CDrawer_BuildingAnimationSlot,
          CDrawer_BuildingAnimationSlot);

// Purpose: the animation slot of a building type's picture (its shadow; the
// sprite is the next slot), used by the panel drawer for the building menus.
std::uint32_t __fastcall CDrawer_BuildingAnimationSlot(CDrawer* /*self*/, void* /*edx*/, std::uint32_t type) {
    // dec eax; cmp eax, 37h; ja default: an unsigned range check.
    if (type - kFirstType > kLastType - kFirstType) {
        return type;
    }
    return kSlots[type - kFirstType];
}

namespace {

std::uint32_t __fastcall WrongBuildingAnimationSlot(CDrawer* self, void* edx, std::uint32_t type) {
    const std::uint32_t slot = CDrawer_BuildingAnimationSlot(self, edx, type);
    return type == 34 ? 0 : slot;
}

int CompareSlots(test::Context& ctx, BuildingAnimationSlotFn candidate, bool quiet) {
    int failures = 0;
    auto check = [&](std::uint32_t type) {
        const std::uint32_t expected = Original_CDrawer_BuildingAnimationSlot(nullptr, nullptr, type);
        const std::uint32_t actual = candidate(nullptr, nullptr, type);
        if (expected != actual) {
            ++failures;
            if (!quiet) {
                S3RE_EXPECT_EQ(expected, actual);
            }
        }
    };
    for (std::uint32_t type = 0; type < 300; ++type) {
        check(type);
    }
    for (int i = 0; i < 2000; ++i) {
        check(ctx.rng().Next32());
    }
    check(0xffffffffu);
    return failures;
}

}  // namespace

S3RE_TEST(CDrawer_BuildingAnimationSlot_MatchesOriginal) {
    CompareSlots(ctx, CDrawer_BuildingAnimationSlot, false);
}

S3RE_TEST(CDrawer_BuildingAnimationSlot_ComparisonDetectsWrongImplementation) {
    S3RE_EXPECT(CompareSlots(ctx, WrongBuildingAnimationSlot, true) > 0);
}

}  // namespace s3re::game
