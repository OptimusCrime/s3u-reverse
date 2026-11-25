// Watcom WCBareVectorBase<unsigned short> constructor (0x00462df0): (n)
// thiscall, ret 4, EAX = this. vtable 0x657404, +4 = 0, +0xc (capacity) =
// n, +0x10 (filled) = 0, +8 (data) = operator new(n * 2) when n is not 0
// (the byte count wraps at 32 bits), else 0; when operator new fails the
// capacity becomes 0. The derived vectors set their own vtable and fields
// afterwards (e.g. GamePlayerSetup's vector at +0x38: length +0x14 = 0,
// +0x18 = 5, vtable 0x657410). Layout as in ptr_vector.h.

#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>

namespace s3re::watcom {

#pragma pack(push, 1)
struct UShortVectorBase {
    std::uint32_t vtable;
    std::uint32_t unk04;
    std::uint16_t* data;
    std::uint32_t capacity;
    std::int32_t filled;
};
#pragma pack(pop)
static_assert(sizeof(UShortVectorBase) == 0x14);

using UShortVectorConstructFn = UShortVectorBase*(__fastcall*)(UShortVectorBase* self, void* edx, std::uint32_t n);
UShortVectorBase* __fastcall WCBareVectorBase_UShort_Construct(UShortVectorBase* self, void* edx, std::uint32_t n);

S3RE_HOOK(0x00462df0, WCBareVectorBase_UShort_Construct, UShortVectorConstructFn,
          Original_WCBareVectorBase_UShort_Construct, WCBareVectorBase_UShort_Construct);

// Purpose: Sets up an empty Watcom vector of 16-bit values with room for n
// of them (a member of the player setups and of menu objects).
UShortVectorBase* __fastcall WCBareVectorBase_UShort_Construct(UShortVectorBase* self, void* /*edx*/,
                                                               std::uint32_t n) {
    self->unk04 = 0;
    self->vtable = game::vtables::kWCBareVectorBase_ushort;
    self->capacity = n;
    self->filled = 0;
    if (n == 0) {
        self->data = nullptr;
        return self;
    }
    self->data = static_cast<std::uint16_t*>(msvc6::GameOperatorNew(n * 2));
    if (self->data == nullptr) {
        self->capacity = 0;
    }
    return self;
}

// ---------------------------------------------------------------------------
// Tests: on random objects with n 0..64: the objects' bytes (the data
// pointers only as null or not) and EAX; the blocks are freed again.
// ---------------------------------------------------------------------------

namespace {

void CompareConstruct(test::Context& ctx, UShortVectorConstructFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        UShortVectorBase a, b;
        ctx.rng().Fill(&a, sizeof(a));
        std::memcpy(&b, &a, sizeof(a));
        const std::uint32_t n = ctx.rng().Chance(1, 4) ? 0 : ctx.rng().Below(65);
        UShortVectorBase* ra = Original_WCBareVectorBase_UShort_Construct(&a, nullptr, n);
        UShortVectorBase* rb = candidate(&b, nullptr, n);
        const bool nullA = a.data == nullptr, nullB = b.data == nullptr;
        msvc6::GameOperatorDelete(a.data);
        msvc6::GameOperatorDelete(b.data);
        a.data = b.data = nullptr;
        if (!S3RE_EXPECT(ra == &a && rb == &b && nullA == nullB) || !S3RE_EXPECT_MEM_EQ(&a, &b, sizeof(a))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: n %u", i, n);
            return;
        }
    }
}

// Wrong: leaves +4 alone.
UShortVectorBase* __fastcall WrongConstruct(UShortVectorBase* self, void* edx, std::uint32_t n) {
    const std::uint32_t keep = self->unk04;
    WCBareVectorBase_UShort_Construct(self, edx, n);
    self->unk04 = keep;
    return self;
}

}  // namespace

S3RE_TEST(WCBareVectorBase_UShort_Construct_MatchesOriginal) {
    CompareConstruct(ctx, &WCBareVectorBase_UShort_Construct, 2000);
}
S3RE_TEST(WCBareVectorBase_UShort_Construct_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareConstruct(probe, &WrongConstruct, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::watcom
