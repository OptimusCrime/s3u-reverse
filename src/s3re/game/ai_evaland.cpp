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

#include "hooks.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kRefCount = 0x007e0568;
constexpr std::uintptr_t kInstance = 0x007e056c;
constexpr std::uintptr_t kGlobal7e3134 = 0x007e3134;
constexpr std::uintptr_t kEhVectorDestructor = 0x0061f18a;
constexpr std::uintptr_t kListDestructor = 0x005591e0;

template <typename T>
T& Global(std::uintptr_t address) {
    return *reinterpret_cast<T*>(address);
}

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
    Global<std::uint32_t>(kGlobal7e3134) = 0;
    using EhVectorDestructorFn = void(__stdcall*)(void* array, std::uint32_t size, std::uint32_t count,
                                                  std::uintptr_t destructor);
    reinterpret_cast<EhVectorDestructorFn>(kEhVectorDestructor)(self, 0xc, 0x14, kListDestructor);
    return 0;
}

S3RE_HOOK(0x00553dc0, AI_EvaLand_Release, GlobalFn, Original_AI_EvaLand_Release, AI_EvaLand_Release);

// Purpose: Drops one reference to the shared land evaluation object and
// deletes it with the last one (when the last computer player goes away).
std::uint32_t __cdecl AI_EvaLand_Release() {
    const std::uint32_t count = --Global<std::uint32_t>(kRefCount);
    if (count != 0) {
        return count;
    }
    std::uint32_t result = 0;
    void* instance = Global<void*>(kInstance);
    if (instance != nullptr) {
        AI_EvaLand_Destroy(instance, nullptr);
        result = GameDelete(instance);
    }
    Global<std::uint32_t>(kInstance) = 0;
    Global<std::uint32_t>(kRefCount) = 0;
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

std::uint8_t* MakeEvaLand(test::Context& ctx) {
    auto* object = static_cast<std::uint8_t*>(GameNew(0x15c));
    ctx.rng().Fill(object, 0x15c);
    for (int i = 0; i < 20; ++i) {
        auto* head = static_cast<std::uint32_t*>(GameNew(8));
        head[0] = head[1] = reinterpret_cast<std::uintptr_t>(head);
        std::memcpy(object + 12 * i + 4, &head, 4);
        const std::uint32_t zero = 0;
        std::memcpy(object + 12 * i + 8, &zero, 4);
    }
    void* grid = ctx.rng().Chance(1, 4) ? nullptr : GameNew(16);
    std::memcpy(object + 0x158, &grid, 4);
    return object;
}

void CompareEvaLand(test::Context& ctx, ThisFn destroy, GlobalFn release, int cases) {
    const std::uint32_t savedCount = Global<std::uint32_t>(kRefCount);
    const std::uint32_t savedInstance = Global<std::uint32_t>(kInstance);
    const std::uint32_t savedGlobal = Global<std::uint32_t>(kGlobal7e3134);
    for (int i = 0; i < cases; ++i) {
        bool same = true;
        if (ctx.rng().Chance(1, 2)) {
            std::uint8_t* a = MakeEvaLand(ctx);
            std::uint8_t* b = MakeEvaLand(ctx);
            std::memcpy(b, a, 4);  // same first dword (garbage there is overwritten anyway)
            Global<std::uint32_t>(kGlobal7e3134) = 0x1234;
            Original_AI_EvaLand_Destroy(a, nullptr);
            same &= S3RE_EXPECT_EQ(Global<std::uint32_t>(kGlobal7e3134), 0u);
            Global<std::uint32_t>(kGlobal7e3134) = 0x1234;
            destroy(b, nullptr);
            same &= S3RE_EXPECT_EQ(Global<std::uint32_t>(kGlobal7e3134), 0u);
            for (int k = 0; k < 20; ++k) {
                same &= S3RE_EXPECT_MEM_EQ(a + 12 * k + 4, b + 12 * k + 4, 8);
            }
            same &= S3RE_EXPECT_MEM_EQ(a + 0x158, b + 0x158, 4);
        } else {
            const std::uint32_t count = ctx.rng().Chance(1, 2) ? 1 : ctx.rng().Below(4) + 2;
            const bool none = ctx.rng().Chance(1, 4);
            Global<std::uint32_t>(kRefCount) = count;
            Global<std::uint8_t*>(kInstance) = none ? nullptr : MakeEvaLand(ctx);
            const std::uint32_t ra = Original_AI_EvaLand_Release();
            const std::uint32_t ca = Global<std::uint32_t>(kRefCount);
            const std::uint32_t ia = Global<std::uint32_t>(kInstance);
            Global<std::uint32_t>(kRefCount) = count;
            Global<std::uint8_t*>(kInstance) = none ? nullptr : MakeEvaLand(ctx);
            const std::uint32_t keep = Global<std::uint32_t>(kInstance);
            const std::uint32_t rb = release();
            same &= S3RE_EXPECT_EQ(ca, Global<std::uint32_t>(kRefCount));
            same &= S3RE_EXPECT_EQ(ia == 0, Global<std::uint32_t>(kInstance) == 0);
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
    Global<std::uint32_t>(kRefCount) = savedCount;
    Global<std::uint32_t>(kInstance) = savedInstance;
    Global<std::uint32_t>(kGlobal7e3134) = savedGlobal;
}

std::uint32_t __cdecl WrongRelease() {
    const std::uint32_t count = --Global<std::uint32_t>(kRefCount);
    if (count > 1) {  // wrong: releases one reference too early
        return count;
    }
    Global<std::uint32_t>(kInstance) = 0;
    Global<std::uint32_t>(kRefCount) = 0;
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
