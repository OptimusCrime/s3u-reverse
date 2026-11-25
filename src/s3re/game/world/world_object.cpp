// The world object's (CGameData) destructors.
//
// CGameData::~CGameData (0x004fce80): () thiscall: vtable = 0x658c18 (the
//   CGameData vtable), then a jump to Win32::CObject::~CObject 0x610330.
//   Nothing else is released (the members own no memory). EAX unchanged
//   (void).
// CGameData::scalar_deleting_destructor (0x004fce60; vtable slot 0):
//   (flags) ret 4: ~CGameData, then operator delete(this) if flags bit 0;
//   EAX = this.

#include "game/vtables.h"
#include "game/world.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

using WorldDestructorFn = void(__fastcall*)(CGameData* self, void* edx);
using WorldDeletingDestructorFn = CGameData*(__fastcall*)(CGameData* self, void* edx, std::uint32_t flags);
void __fastcall CGameData_Destroy(CGameData* self, void* edx);
CGameData* __fastcall CGameData_DeletingDestroy(CGameData* self, void* edx, std::uint32_t flags);
S3RE_HOOK(0x004fce80, CGameData_Destroy, WorldDestructorFn, Original_CGameData_Destroy, CGameData_Destroy);
S3RE_HOOK(0x004fce60, CGameData_DeletingDestroy, WorldDeletingDestructorFn, Original_CGameData_DeletingDestroy,
          CGameData_DeletingDestroy);

// Purpose: Destructor of the world object (when a game ends).
void __fastcall CGameData_Destroy(CGameData* self, void*) {
    self->vtable = vtables::kCGameData;
    reinterpret_cast<WorldDestructorFn>(0x00610330)(self, nullptr);  // Win32::CObject::~CObject
}

// Purpose: The compiler-generated "delete this world" entry of its vtable.
CGameData* __fastcall CGameData_DeletingDestroy(CGameData* self, void*, std::uint32_t flags) {
    reinterpret_cast<WorldDestructorFn>(0x004fce80)(self, nullptr);
    if ((flags & 1) != 0) {
        msvc6::GameOperatorDelete(self);
    }
    return self;
}

// ---------------------------------------------------------------------------
// Tests: small random objects (only the vtable is touched); the deleting
// destructor with flags 0 on the stack and with bit 0 on blocks from the
// game's heap (the return value; the free itself is what the emulator spec
// compares).
// ---------------------------------------------------------------------------

namespace {

void CompareDestructors(test::Context& ctx, WorldDestructorFn destroy, WorldDeletingDestructorFn deleting,
                        int cases) {
    for (int i = 0; i < cases; ++i) {
        alignas(4) std::uint8_t a[32], b[32];
        ctx.rng().Fill(a, sizeof(a));
        std::memcpy(b, a, sizeof(a));
        const bool useDeleting = ctx.rng().Chance(1, 2);
        const std::uint32_t flags = ctx.rng().Next32() & ~1u;
        bool same = true;
        if (useDeleting) {
            CGameData* ra = Original_CGameData_DeletingDestroy(reinterpret_cast<CGameData*>(a), nullptr, flags);
            CGameData* rb = deleting(reinterpret_cast<CGameData*>(b), nullptr, flags);
            same = S3RE_EXPECT(ra == reinterpret_cast<CGameData*>(a) && rb == reinterpret_cast<CGameData*>(b));
        } else {
            Original_CGameData_Destroy(reinterpret_cast<CGameData*>(a), nullptr);
            destroy(reinterpret_cast<CGameData*>(b), nullptr);
        }
        same = same && S3RE_EXPECT_MEM_EQ(a, b, sizeof(a));
        // With bit 0: the object is freed.
        auto* block = static_cast<CGameData*>(msvc6::GameOperatorNew(16));
        same = same && S3RE_EXPECT(deleting(block, nullptr, flags | 1) == block);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

// Wrong: leaves the CGameData vtable (as if ~CObject weren't called).
void __fastcall WrongDestroy(CGameData* self, void* edx) {
    CGameData_Destroy(self, edx);
    self->vtable = vtables::kCGameData;
}

}  // namespace

S3RE_TEST(CGameData_Destructors_MatchOriginal) {
    CompareDestructors(ctx, &CGameData_Destroy, &CGameData_DeletingDestroy, 500);
}

S3RE_TEST(CGameData_Destructors_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDestructors(probe, &WrongDestroy, &CGameData_DeletingDestroy, 50);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
