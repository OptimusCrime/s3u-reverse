// The computer players' shared land evaluation object (Rainer\Cevaland.cpp,
// "CEvaLand", our name from the file; 0x15c bytes): one instance for all
// computer players, reference counted through two globals:
//   0x7e0568 the reference count, 0x7e056c the instance.
// Layout used here: +0 an array of 20 lists of 12 bytes (element destructor
// 0x5591e0, which frees the nodes), +0x158 the grid (CEvaLand_FreeGrid).
//
// AI_EvaLand_Destroy (0x00553fe0): () the destructor: AI_GridFree (0x554040),
//   global 0x7e3134 = 0, then the CRT's `eh vector destructor iterator`
//   (0x61f18a, stdcall) runs 0x5591e0 on the 20 lists (12 bytes each,
//   from the last). EAX: not used.
// AI_EvaLand_Release (0x00553dc0): () if --count is 0: the instance (if
//   any) is destroyed and freed, both globals = 0. EAX = the decremented
//   count, or what operator delete returned when the instance was freed (the
//   caller 0x550f30 tail-jumps here).

#include "game/ai_layout.h"
#include "game/globals.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

using namespace ai;

namespace {

constexpr std::uintptr_t kEhVectorDestructor = 0x0061f18a;
constexpr std::uintptr_t kListDestructor = 0x005591e0;

std::uint32_t GameDelete(void* block) {
    return reinterpret_cast<std::uint32_t(__cdecl*)(void*)>(0x0061e4ef)(block);
}

}  // namespace

// ai_helpers.cpp (0x554040, AI::CEvaLand_FreeGrid)
void __fastcall AI_GridFree(void* self, void* edx);

using ThisFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using GlobalFn = std::uint32_t(__cdecl*)();

std::uint32_t __fastcall AI_EvaLand_Destroy(void* self, void* edx);
std::uint32_t __cdecl AI_EvaLand_Release();

S3RE_HOOK(0x00553fe0, AI_EvaLand_Destroy, ThisFn, Original_AI_EvaLand_Destroy, AI_EvaLand_Destroy);

// Purpose: Destructor of the computer players' shared land evaluation
// object (frees its grid and its 20 lists).
std::uint32_t __fastcall AI_EvaLand_Destroy(void* self, void* /*edx*/) {
    AI_GridFree(self, nullptr);
    g_aiUnk7e3134 = 0;
    using EhVectorDestructorFn = void(__stdcall*)(void* array, std::uint32_t size, std::uint32_t count,
                                                  std::uintptr_t destructor);
    auto* evaluation = static_cast<AiLandEvaluation*>(self);
    reinterpret_cast<EhVectorDestructorFn>(kEhVectorDestructor)(evaluation->lists, sizeof(AiList),
                                                                 sizeof(evaluation->lists) / sizeof(AiList),
                                                                 kListDestructor);
    return 0;
}

S3RE_HOOK(0x00553dc0, AI_EvaLand_Release, GlobalFn, Original_AI_EvaLand_Release, AI_EvaLand_Release);

// Purpose: Drops one reference to the shared land evaluation object and
// deletes it with the last one (when the last computer player goes away).
std::uint32_t __cdecl AI_EvaLand_Release() {
    const std::uint32_t count = --g_aiEvaLandRefCount;
    if (count != 0) {
        return count;
    }
    std::uint32_t result = 0;
    void* instance = g_aiEvaLand.get();
    if (instance != nullptr) {
        AI_EvaLand_Destroy(instance, nullptr);
        result = GameDelete(instance);
    }
    g_aiEvaLand = nullptr;
    g_aiEvaLandRefCount = 0;
    return result;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Objects on the game's heap with a grid block and 20 empty lists (each list
// head is a heap node that points at itself, as an empty list does), built
// the same way for both sides. Compared: EAX (Release), the globals, and the
// object's bytes after the destructor (list fields cleared, grid pointer
// cleared). Freed blocks can't be compared directly; the destructor runs on
// objects that stay allocated, Release frees its own instance.

namespace {

void* GameNew(std::uint32_t size) {
    return reinterpret_cast<void*(__cdecl*)(std::uint32_t)>(0x0061efb9)(size);
}

AiLandEvaluation* MakeEvaLand(test::Context& ctx) {
    auto* object = static_cast<AiLandEvaluation*>(GameNew(sizeof(AiLandEvaluation)));
    ctx.rng().Fill(object, sizeof(AiLandEvaluation));
    for (AiList& list : object->lists) {
        auto* head = static_cast<AiListNode*>(GameNew(sizeof(AiListNode)));
        head->next = head->prev = reinterpret_cast<std::uintptr_t>(head);
        list.head = head;
        list.count = 0;
    }
    object->cells = ctx.rng().Chance(1, 4) ? nullptr : static_cast<AiGridCell*>(GameNew(16));
    return object;
}

// A pointer global's value as a dword (for comparing and printing).
std::uint32_t PointerBits(const void* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
}

void CompareEvaLand(test::Context& ctx, ThisFn destroy, GlobalFn release, int cases) {
    const std::uint32_t savedCount = g_aiEvaLandRefCount;
    AiLandEvaluation* const savedInstance = g_aiEvaLand;
    const std::uint32_t savedGlobal = g_aiUnk7e3134;
    for (int i = 0; i < cases; ++i) {
        bool same = true;
        if (ctx.rng().Chance(1, 2)) {
            AiLandEvaluation* a = MakeEvaLand(ctx);
            AiLandEvaluation* b = MakeEvaLand(ctx);
            b->lists[0].unk0 = a->lists[0].unk0;  // same first dword (garbage there is overwritten anyway)
            g_aiUnk7e3134 = 0x1234;
            Original_AI_EvaLand_Destroy(a, nullptr);
            same &= S3RE_EXPECT_EQ(g_aiUnk7e3134.get(), 0u);
            g_aiUnk7e3134 = 0x1234;
            destroy(b, nullptr);
            same &= S3RE_EXPECT_EQ(g_aiUnk7e3134.get(), 0u);
            for (int k = 0; k < 20; ++k) {
                same &= S3RE_EXPECT_MEM_EQ(&a->lists[k].head, &b->lists[k].head, 8);  // head and count
            }
            same &= S3RE_EXPECT_MEM_EQ(&a->cells, &b->cells, 4);
        } else {
            const std::uint32_t count = ctx.rng().Chance(1, 2) ? 1 : ctx.rng().Below(4) + 2;
            const bool none = ctx.rng().Chance(1, 4);
            g_aiEvaLandRefCount = count;
            g_aiEvaLand = none ? nullptr : MakeEvaLand(ctx);
            const std::uint32_t ra = Original_AI_EvaLand_Release();
            const std::uint32_t ca = g_aiEvaLandRefCount;
            const std::uint32_t ia = PointerBits(g_aiEvaLand.get());
            g_aiEvaLandRefCount = count;
            g_aiEvaLand = none ? nullptr : MakeEvaLand(ctx);
            const std::uint32_t keep = PointerBits(g_aiEvaLand.get());
            const std::uint32_t rb = release();
            same &= S3RE_EXPECT_EQ(ca, g_aiEvaLandRefCount.get());
            same &= S3RE_EXPECT_EQ(ia == 0, PointerBits(g_aiEvaLand.get()) == 0);
            if (count == 1) {
                // EAX: the count (0) or what delete returned (free's value).
                same &= S3RE_EXPECT_EQ(ra == 0, rb == 0);
            } else {
                same &= S3RE_EXPECT_EQ(ra, rb);
                (void)keep;
            }
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            break;
        }
    }
    g_aiEvaLandRefCount = savedCount;
    g_aiEvaLand = savedInstance;
    g_aiUnk7e3134 = savedGlobal;
}

std::uint32_t __cdecl WrongRelease() {
    const std::uint32_t count = --g_aiEvaLandRefCount;
    if (count > 1) {  // wrong: releases one reference too early
        return count;
    }
    g_aiEvaLand = nullptr;
    g_aiEvaLandRefCount = 0;
    return 0;
}

}  // namespace

S3RE_TEST(AiEvaLand_MatchOriginal) {
    CompareEvaLand(ctx, &AI_EvaLand_Destroy, &AI_EvaLand_Release, 2000);
}

S3RE_TEST(AiEvaLand_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareEvaLand(probe, &AI_EvaLand_Destroy, &WrongRelease, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
