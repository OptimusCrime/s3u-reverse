// Tests of directx_drawer.cpp (DirectX::CGameDrawer members, CDisplayMode).
//
// The drawer sits at kDrawer in an arena (game heap, the same random bytes
// for both runs) with the fake game vtable (its slots 2, 5, 6, 10 and 11
// logged), the fake IDirectDraw2 at +4. Its surface list holds a random
// circle of links (from a per-side link pool, handed out by the lists'
// allocator) to fake game objects and sometimes to the primary surface's
// CSurface part (a CPrimarySurface in the arena with the real vbtable, its
// CSurface with the fake vtable); the palette list links CPalettes in the
// arena. The lists' vtable is a test one (slot 1 destroy link, slot 2
// compare: logged). Each case runs one function under two FakeDDraw scripts
// with the same seed; compared: EAX, the arena and the link pool (pointers
// into either made relative), g_primarySurfaceRect and the logs.

#include "game/framework/directx_drawer.h"
#include "game/framework/framework_layout.h"
#include "game/globals.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/difftest.h"
#include "test/fake_ddraw.h"
#include "test/fake_win32.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace s3re::game {

namespace {

using test::CaughtException;
using test::FakeDDraw;

using F0 = std::uint32_t(__fastcall*)(void* self, void* edx);
using F1 = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a);

constexpr std::size_t kArenaBytes = 0x800;
constexpr std::size_t kDrawer = 0x100;
constexpr std::size_t kModeA = 0x200;     // a DDSURFACEDESC
constexpr std::size_t kModeB = 0x280;
constexpr std::size_t kIterator = 0x2f0;  // WCListIterBase's object
constexpr std::size_t kPrimary = 0x300;   // a CPrimarySurface (vbptr, CSurface at +4)
constexpr std::size_t kPalettes = 0x380;  // CPalettes, 8 bytes apart
constexpr std::size_t kPaletteCount = 6;
constexpr std::size_t kPoolLinks = 32;

struct Link {
    Link* next;
    void* element;
};

Link g_pools[2][kPoolLinks];
std::size_t g_poolNext = 0;
int g_side = 0;
std::uint8_t* g_arenas[2] = {};
std::vector<std::uint32_t>* g_log = nullptr;

std::uint32_t Relative(std::uint32_t value, int side) {
    const auto arena = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_arenas[side]));
    if (value >= arena && value < arena + kArenaBytes) {
        return 0x10000 + (value - arena);
    }
    const auto pool = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_pools[side]));
    if (value >= pool && value < pool + sizeof(g_pools[0])) {
        return 0x20000 + (value - pool);
    }
    return test::DDrawLogPointer(reinterpret_cast<void*>(static_cast<std::uintptr_t>(value)));
}

std::uint32_t Log32(const void* p) {
    return Relative(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)), g_side);
}

void* __fastcall PoolAllocate(std::uint32_t size) {
    g_log->insert(g_log->end(), {0xa110cu, size});
    return g_poolNext < kPoolLinks ? &g_pools[g_side][g_poolNext++] : nullptr;
}

void __fastcall ListDestroyLink(void* list, void*, Link* link) {
    g_log->insert(g_log->end(), {0xde57u, Log32(list), Log32(link)});
}

std::uint32_t __fastcall ListEquivalent(void* list, void*, void* const* a, void* const* b) {
    g_log->insert(g_log->end(), {0xe9u, Log32(list), Log32(*a), Log32(*b)});
    return *a == *b ? 1 : 0;
}

void* g_listVtable[3] = {nullptr, reinterpret_cast<void*>(&ListDestroyLink),
                         reinterpret_cast<void*>(&ListEquivalent)};

std::uint32_t Address(const void* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
}

struct Op {
    const char* name;
    const void* original;
    const void* ours;
    std::uint32_t arity;
    bool ignoreEax;
};

template <typename T>
const void* Fn(T f) {
    return reinterpret_cast<const void*>(f);
}

#define S3RE_OP(name, arity, ignoreEax) Op{#name, Fn(Original_##name), Fn(&name), arity, ignoreEax}

std::vector<Op> Ops() {
    return {
        S3RE_OP(DirectX_CDisplayMode_Construct, 1, false),
        S3RE_OP(DirectX_CDisplayMode_IsEqual, 1, false),
        S3RE_OP(ReturnThis, 0, false),
        S3RE_OP(DirectX_CGameDrawer_Initialize, 1, false),
        S3RE_OP(DirectX_CGameDrawer_CreateOnDevice, 1, false),
        S3RE_OP(DirectX_CGameDrawer_Create, 1, false),
        S3RE_OP(DirectX_CGameDrawer_Destroy, 0, true),
        S3RE_OP(DirectX_CGameDrawer_AddSurface, 1, false),
        S3RE_OP(DirectX_CGameDrawer_SetPrimarySurface, 1, false),
        S3RE_OP(DirectX_CGameDrawer_RemoveSurface, 1, false),
        S3RE_OP(SetMemberByte7CToOne, 1, true),
        S3RE_OP(DirectX_CGameDrawer_ReleaseSurfaces, 1, false),
        S3RE_OP(WCListIterBase_Construct, 1, false),
        S3RE_OP(DirectX_CGameDrawer_RestoreSurfaces, 0, false),
        S3RE_OP(DirectX_CGameDrawer_ReleasePalettes, 1, false),
        S3RE_OP(DirectX_CGameDrawThread_RequestFullRedraw, 0, true),
    };
}

#undef S3RE_OP

struct Outcome {
    std::uint32_t eax = 0;
    CaughtException caught;
};

Outcome Run(const void* fn, std::uint32_t arity, void* self, std::uint32_t arg) {
    Outcome out;
    __try {
        out.eax = arity == 0 ? reinterpret_cast<F0>(fn)(self, nullptr) : reinterpret_cast<F1>(fn)(self, nullptr, arg);
    } __except (test::CatchGameException(GetExceptionInformation(), &out.caught)) {
    }
    return out;
}

// The list at `at` in `arena`: the test vtable, the pool allocator, a circle
// of the given elements (links from the pool; g_poolNext advanced).
void BuildList(std::uint8_t* arena, std::size_t at, const std::vector<std::uint32_t>& elements, int side) {
    auto* const list = reinterpret_cast<framework::PtrSList*>(arena + at);
    list->vtable = Address(g_listVtable);
    list->allocator = reinterpret_cast<void*>(&PoolAllocate);
    list->count = static_cast<std::int32_t>(elements.size());
    list->last = nullptr;
    const std::size_t first = g_poolNext;
    for (std::size_t k = 0; k < elements.size(); ++k) {
        Link& link = g_pools[side][first + k];
        link.element = reinterpret_cast<void*>(static_cast<std::uintptr_t>(elements[k]));
        link.next = &g_pools[side][first + (k + 1) % elements.size()];
    }
    if (!elements.empty()) {
        list->last = &g_pools[side][first + elements.size() - 1];
    }
    g_poolNext = first + elements.size();
}

// An element value: a fake game object, the primary's CSurface part or a
// palette, by code (so both sides get their own arena's addresses).
std::uint32_t Element(std::uint32_t code, std::uint8_t* arena) {
    if (code < 0x100) {
        return Address(FakeDDraw::Game(1 + code));
    }
    if (code == 0x100) {
        return Address(arena + kPrimary + 4);
    }
    return Address(arena + kPalettes + 8 * (code - 0x200));
}

bool CompareOnce(test::Context& ctx, const std::vector<Op>& ops, const std::vector<Op>& candidates,
                 std::size_t index) {
    test::Rng& rng = ctx.rng();
    const Op& op = ops[index];
    for (std::uint32_t slot : {1u, 2u, 3u, 4u, 10u}) {
        FakeDDraw::SetGameSlotArgs(slot, 0);
    }
    for (std::uint32_t slot : {5u, 6u, 11u}) {
        FakeDDraw::SetGameSlotArgs(slot, 1);
    }
    std::vector<std::uint8_t> bytes(kArenaBytes);
    rng.Fill(bytes.data(), bytes.size());
    // The list contents (codes), the primary, the argument.
    std::vector<std::uint32_t> surfaceCodes, paletteCodes;
    const std::uint32_t surfaces = rng.Chance(1, 6) ? 0 : 1 + rng.Below(6);
    const bool primaryListed = rng.Chance(1, 2);
    for (std::uint32_t k = 0; k < surfaces; ++k) {
        surfaceCodes.push_back(k);
    }
    if (primaryListed) {
        surfaceCodes.insert(surfaceCodes.begin() + rng.Below(surfaces + 1), 0x100);
    }
    const std::uint32_t palettes = rng.Below(kPaletteCount + 1);
    for (std::uint32_t k = 0; k < palettes; ++k) {
        paletteCodes.push_back(0x200 + k);
    }
    const bool hasPrimary = rng.Chance(2, 3);
    const std::uint32_t argKind = rng.Below(6);
    const std::uint32_t argGame = rng.Below(7);
    const std::uint32_t argValue = rng.Next32();
    const bool modesData = rng.Chance(2, 3);
    const std::uint8_t owns = rng.Chance(1, 2) ? 1 : 0;
    std::uint32_t paletteFakes[kPaletteCount];
    for (std::uint32_t& p : paletteFakes) {
        p = rng.Chance(1, 4) ? 0 : static_cast<std::uint32_t>(rng.Below(4));
    }

    std::uint8_t* arenas[2];
    std::uint32_t args[2];
    void* modeBlocks[2] = {};
    for (int side = 0; side < 2; ++side) {
        arenas[side] = static_cast<std::uint8_t*>(msvc6::GameOperatorNew(kArenaBytes));
        std::uint8_t* const a = arenas[side];
        std::memcpy(a, bytes.data(), kArenaBytes);
        g_arenas[side] = a;
        auto* const drawer = reinterpret_cast<framework::CGameDrawer*>(a + kDrawer);
        drawer->draw.vtable = Address(FakeDDraw::GameVtable());
        drawer->draw.directDraw = FakeDDraw::DirectDraw();
        drawer->draw.owns = owns;
        std::memset(g_pools[side], 0xcd, sizeof(g_pools[side]));
        g_poolNext = 0;
        std::vector<std::uint32_t> elements;
        for (std::uint32_t code : surfaceCodes) {
            elements.push_back(Element(code, a));
        }
        BuildList(a, kDrawer + offsetof(framework::CGameDrawer, surfaces), elements, side);
        elements.clear();
        for (std::uint32_t code : paletteCodes) {
            elements.push_back(Element(code, a));
        }
        BuildList(a, kDrawer + offsetof(framework::CGameDrawer, palettes), elements, side);
        for (std::size_t k = 0; k < kPaletteCount; ++k) {
            auto* const palette = reinterpret_cast<framework::CPalette*>(a + kPalettes + 8 * k);
            palette->palette = paletteFakes[k] == 0 ? nullptr : FakeDDraw::Palette(paletteFakes[k] - 1);
        }
        // The primary surface: real vbtable, its CSurface with the fake vtable.
        std::uint32_t vbtable = vtables::kCPrimarySurface_vbtable;
        std::memcpy(a + kPrimary, &vbtable, 4);
        auto* const base = reinterpret_cast<framework::CSurface*>(a + kPrimary + 4);
        base->simple.vtable = Address(FakeDDraw::GameVtable());
        drawer->primary = hasPrimary ? a + kPrimary : nullptr;
        if (modesData) {
            modeBlocks[side] = msvc6::GameOperatorNew(16);
        }
        drawer->modes.data = modeBlocks[side];
        switch (argKind) {
            case 0: args[side] = Element(argGame < 6 ? argGame : 0x100, a); break;
            case 1: args[side] = Address(a + kPrimary); break;  // the primary object (SetPrimarySurface)
            case 2: args[side] = Address(a + kModeB); break;     // a desc / another mode
            case 3: args[side] = 0; break;
            case 4: args[side] = Address(a + kDrawer + offsetof(framework::CGameDrawer, surfaces)); break;
            default: args[side] = argValue; break;
        }
    }
    // Arguments that are dereferenced must be valid.
    const std::string_view name = op.name;
    if (name == "DirectX_CDisplayMode_IsEqual" || name == "DirectX_CDisplayMode_Construct") {
        args[0] = argKind == 3 && name != "DirectX_CDisplayMode_IsEqual" ? 0 : Address(arenas[0] + kModeB);
        args[1] = args[0] == 0 ? 0 : Address(arenas[1] + kModeB);
    } else if (name == "DirectX_CGameDrawer_SetPrimarySurface") {
        args[0] = Address(arenas[0] + kPrimary);
        args[1] = Address(arenas[1] + kPrimary);
    } else if (name == "DirectX_CGameDrawer_CreateOnDevice" || name == "DirectX_CGameDrawer_Create") {
        if (argKind == 5) {
            args[0] = args[1] = 0;
        }
    } else if (name == "WCListIterBase_Construct" && argKind == 5) {
        args[0] = args[1] = 0;
    }
    // Where `self` is: the mode or iterator objects, else the drawer.
    std::size_t selfAt = kDrawer;
    if (name.rfind("DirectX_CDisplayMode", 0) == 0) {
        selfAt = kModeA;
    } else if (name == "WCListIterBase_Construct") {
        selfAt = kIterator;
    }
    if (name == "DirectX_CDisplayMode_IsEqual" && rng.Chance(1, 2)) {
        const std::uint32_t at = rng.Below(0x6c);
        const auto flip = static_cast<std::uint8_t>(rng.Below(2));
        for (int side = 0; side < 2; ++side) {
            std::memcpy(arenas[side] + kModeA, arenas[side] + kModeB, 0x6c);
            arenas[side][kModeA + at] ^= flip;
        }
    }

    const std::uint64_t seed = rng.Next64();
    test::FakeDDrawConfig config;
    config.failChance = rng.Below(4);
    FakeDDraw fakes[2] = {FakeDDraw(seed, config), FakeDDraw(seed, config)};
    std::int32_t savedRect[4];
    std::memcpy(savedRect, g_primarySurfaceRect.get(), sizeof(savedRect));
    std::int32_t rects[2][4];
    std::vector<std::uint32_t> logs[2];
    Outcome outcomes[2];
    for (int side = 0; side < 2; ++side) {
        FakeDDraw::ResetObjects();
        for (std::int32_t& value : g_primarySurfaceRect.get()) {
            value = 0x5a5a5a5a;
        }
        g_side = side;
        g_log = &fakes[side].log_;
        fakes[side].SetArena(arenas[side], kArenaBytes);
        fakes[side].Install();
        outcomes[side] = Run(side == 0 ? op.original : candidates[index].ours, op.arity, arenas[side] + selfAt,
                             args[side]);
        fakes[side].Uninstall();
        g_log = nullptr;
        std::memcpy(rects[side], g_primarySurfaceRect.get(), sizeof(rects[side]));
        // The second side's pool starts where the first one's did.
        if (side == 0) {
            g_poolNext = surfaceCodes.size() + paletteCodes.size();
        }
    }
    std::memcpy(g_primarySurfaceRect.get(), savedRect, sizeof(savedRect));

    bool same = S3RE_EXPECT(outcomes[0].caught == outcomes[1].caught);
    if (!outcomes[0].caught.threw && !op.ignoreEax) {
        same &= S3RE_EXPECT_EQ(Relative(outcomes[0].eax, 0), Relative(outcomes[1].eax, 1));
    }
    same &= S3RE_EXPECT(fakes[0].log_ == fakes[1].log_);
    same &= S3RE_EXPECT_MEM_EQ(rects[0], rects[1], sizeof(rects[0]));
    // Arena and pool, word by word with pointers made relative (the freed
    // mode array's pointer is cleared by Destroy).
    for (std::size_t k = 0; k + 4 <= kArenaBytes; k += 4) {
        std::uint32_t w0, w1;
        std::memcpy(&w0, arenas[0] + k, 4);
        std::memcpy(&w1, arenas[1] + k, 4);
        if (w0 == w1 || (w0 == Address(modeBlocks[0]) && w1 == Address(modeBlocks[1]))) {
            continue;
        }
        if (!S3RE_EXPECT_EQ(Relative(w0, 0), Relative(w1, 1))) {
            same = false;
            ctx.Fail(__FILE__, __LINE__, "  arena +%#x", static_cast<unsigned>(k));
        }
    }
    for (std::size_t k = 0; k < kPoolLinks; ++k) {
        same &= S3RE_EXPECT_EQ(Relative(Address(g_pools[0][k].next), 0), Relative(Address(g_pools[1][k].next), 1));
        same &= S3RE_EXPECT_EQ(Relative(Address(g_pools[0][k].element), 0),
                               Relative(Address(g_pools[1][k].element), 1));
    }
    if (!same) {
        ctx.Fail(__FILE__, __LINE__, "  op %s, arg kind %u", op.name, argKind);
        for (std::size_t i = 0; (i < fakes[0].log_.size() || i < fakes[1].log_.size()) && i < 60; ++i) {
            ctx.Fail(__FILE__, __LINE__, "  log[%u]: %#x %#x", static_cast<unsigned>(i),
                     i < fakes[0].log_.size() ? fakes[0].log_[i] : 0xdead,
                     i < fakes[1].log_.size() ? fakes[1].log_[i] : 0xdead);
        }
    }
    const bool destroyed = name == "DirectX_CGameDrawer_Destroy";
    for (int side = 0; side < 2; ++side) {
        if (!destroyed && modeBlocks[side] != nullptr) {
            msvc6::GameOperatorDelete(modeBlocks[side]);
        }
        msvc6::GameOperatorDelete(arenas[side]);
        g_arenas[side] = nullptr;
    }
    return same;
}

void Compare(test::Context& ctx, const std::vector<Op>& candidates, int cases) {
    const std::vector<Op> ops = Ops();
    for (int i = 0; i < cases; ++i) {
        const std::size_t index = ctx.rng().Below(static_cast<std::uint32_t>(ops.size()));
        if (!CompareOnce(ctx, ops, candidates, index)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

// Wrong: a surface that isn't listed removes nothing.
std::uint32_t __fastcall WrongRemoveSurface(void* self, void* edx, void* surface) {
    auto* const drawer = static_cast<framework::CGameDrawer*>(self);
    bool listed = false;
    if (drawer->surfaces.last != nullptr) {
        const Link* const last = static_cast<const Link*>(drawer->surfaces.last);
        const Link* link = last;
        do {
            link = link->next;
            listed |= link->element == surface;
        } while (link != last);
    }
    if (!listed) {
        return 0;
    }
    return DirectX_CGameDrawer_RemoveSurface(self, edx, surface);
}

}  // namespace

S3RE_TEST(DirectXDrawer_MatchOriginal) {
    Compare(ctx, Ops(), 20000);
}

S3RE_TEST(DirectXDrawer_ComparisonDetectsWrongImplementation) {
    std::vector<Op> wrong = Ops();
    for (Op& op : wrong) {
        if (op.original == Fn(Original_DirectX_CGameDrawer_RemoveSurface)) {
            op.ours = Fn(&WrongRemoveSurface);
        }
    }
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Compare(probe, wrong, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
