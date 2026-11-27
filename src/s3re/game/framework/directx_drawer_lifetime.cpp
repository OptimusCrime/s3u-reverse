// DirectX::CGameDrawer's constructor and shutdown, and
// CDirectDrawDevice::IsEqual (DirectX\Gamebase.cpp). Layout:
// framework::CGameDrawer (framework_layout.h).
//
// CDirectDrawDevice::IsEqual (0x005b69c0): (other) ret 4: the 16-byte
//   GUIDs at +0 are equal (repe cmpsd; EAX 1 / 0).
// CGameDrawer::CGameDrawer (0x005b6c80): (directDraw) ret 4:
//   CDirectDraw's constructor (0x5bead0) with the argument; +0xc = 0; the
//   surface list +0x10 and the palette list +0x28 (WCValSList constructors
//   0x5b7d40 / 0x5b7e00 (0, 0), then the WCPtrSList vtables 0x65cae8 /
//   0x65cad8); the device vector +0x40 and the display-mode vector +0x5c
//   (WCVectorBase constructors 0x5b7ec0 / 0x5b7f40 (10, 5), then the
//   WCValOrderedVector vtables 0x65cac4 / 0x65cab0); +0x78 = 0, byte +0x7c
//   = 0, vtable 0x65ca7c; the three containers' +4 (their Watcom exception
//   flags) = 0x7f; g_drawer = this. EAX = this. (The original's unwind
//   actions destroy the members built so far; none of the member
//   constructors can throw: Watcom's vectors take a null block.)
// CGameDrawer::Shutdown (0x005b6f00): vtable slot 8: slot 2 (Destroy); then
//   every palette: the first link taken off (WCIsvSListBase::get(0)
//   0x60b500), destroyed (the list's slot 1) and, if the link held a
//   palette, the palette deleted (slot 0 with 1), while the list is not
//   empty; clearAndDestroy (0x60b560); the device vector resized to 0
//   (WCValOrderedVector::resize 0x5b8080) and its entries = 0. EAX:
//   resize's.

#include "game/framework/framework_layout.h"
#include "game/globals.h"
#include "game/vtables.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

using framework::CGameDrawer;
using framework::PtrSList;

constexpr std::uintptr_t kDirectDrawConstruct = 0x005bead0;   // DirectX::CDirectDraw::CDirectDraw(other) ret 4
constexpr std::uintptr_t kSurfaceListConstruct = 0x005b7d40;  // WCValSList<CSurface*>(alloc, dealloc) ret 8
constexpr std::uintptr_t kPaletteListConstruct = 0x005b7e00;  // WCValSList<CPalette*>(alloc, dealloc) ret 8
constexpr std::uintptr_t kDeviceVectorConstruct = 0x005b7ec0; // WCVectorBase<CDirectDrawDevice>(capacity, grow) ret 8
constexpr std::uintptr_t kModeVectorConstruct = 0x005b7f40;   // WCVectorBase<CDisplayMode>(capacity, grow) ret 8
constexpr std::uintptr_t kListGet = 0x0060b500;               // WCIsvSListBase::get(index) ret 4
constexpr std::uintptr_t kListClearAndDestroy = 0x0060b560;   // WCIsvSListBase::clearAndDestroy() ret
constexpr std::uintptr_t kDeviceVectorResize = 0x005b8080;    // WCValOrderedVector<CDirectDrawDevice>::resize(n) ret 4

constexpr std::uint32_t kVectorCapacity = 10;
constexpr std::uint32_t kVectorGrow = 5;
constexpr std::uint32_t kAllExceptions = 0x7f;   // the Watcom containers' exception flags (+4)
constexpr std::uint32_t kSlotDelete = 0;
constexpr std::uint32_t kListSlotDestroyLink = 1;
constexpr std::uint32_t kSlotDestroy = 2;        // CGameDrawer::Destroy (disconnect, free the modes)

using MemberFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using Member1Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a);
using Member2Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b);

std::uint32_t CallSlot(void* object, std::uint32_t slot) {
    return reinterpret_cast<MemberFn>((*static_cast<void* const* const*>(object))[slot])(object, nullptr);
}
std::uint32_t CallSlot(void* object, std::uint32_t slot, std::uint32_t a) {
    return reinterpret_cast<Member1Fn>((*static_cast<void* const* const*>(object))[slot])(object, nullptr, a);
}

// A WCPtrSList link: next, then the element.
struct Link {
    void* next;
    void* element;  // +0x04
};

}  // namespace

std::uint32_t __fastcall DirectX_CDirectDrawDevice_IsEqual(const void* self, void* edx, const void* other);
void* __fastcall DirectX_CGameDrawer_Construct(void* self, void* edx, void* directDraw);
std::uint32_t __fastcall DirectX_CGameDrawer_Shutdown(void* self, void* edx);

using IsEqualFn = decltype(&DirectX_CDirectDrawDevice_IsEqual);
using ConstructFn = decltype(&DirectX_CGameDrawer_Construct);
using ShutdownFn = decltype(&DirectX_CGameDrawer_Shutdown);

S3RE_HOOK(0x005b69c0, DirectX_CDirectDrawDevice_IsEqual, IsEqualFn, Original_DirectX_CDirectDrawDevice_IsEqual,
          DirectX_CDirectDrawDevice_IsEqual);
S3RE_HOOK(0x005b6c80, DirectX_CGameDrawer_Construct, ConstructFn, Original_DirectX_CGameDrawer_Construct,
          DirectX_CGameDrawer_Construct);
S3RE_HOOK(0x005b6f00, DirectX_CGameDrawer_Shutdown, ShutdownFn, Original_DirectX_CGameDrawer_Shutdown,
          DirectX_CGameDrawer_Shutdown);

// Purpose: whether two DirectDraw devices are the same (by their GUID).
std::uint32_t __fastcall DirectX_CDirectDrawDevice_IsEqual(const void* self, void* /*edx*/, const void* other) {
    return std::memcmp(self, other, 16) == 0 ? 1 : 0;
}

// Purpose: constructor of the framework drawer: its surface and palette
// lists, the device and display-mode lists, and registration as g_drawer.
void* __fastcall DirectX_CGameDrawer_Construct(void* self, void* /*edx*/, void* directDraw) {
    auto* const drawer = static_cast<CGameDrawer*>(self);
    reinterpret_cast<Member1Fn>(kDirectDrawConstruct)(self, nullptr, reinterpret_cast<std::uint32_t>(directDraw));
    drawer->window = nullptr;
    reinterpret_cast<Member2Fn>(kSurfaceListConstruct)(&drawer->surfaces, nullptr, 0, 0);
    drawer->surfaces.vtable = vtables::kWCPtrSList_CSurface;
    reinterpret_cast<Member2Fn>(kPaletteListConstruct)(&drawer->palettes, nullptr, 0, 0);
    drawer->palettes.vtable = vtables::kWCPtrSList_CPalette;
    reinterpret_cast<Member2Fn>(kDeviceVectorConstruct)(&drawer->devices, nullptr, kVectorCapacity, kVectorGrow);
    drawer->devices.vtable = vtables::kWCValOrderedVector_CDirectDrawDevice;
    reinterpret_cast<Member2Fn>(kModeVectorConstruct)(&drawer->modes, nullptr, kVectorCapacity, kVectorGrow);
    drawer->modes.vtable = vtables::kWCValOrderedVector_CDisplayMode;
    drawer->primary = nullptr;
    drawer->surfacesMade = 0;
    drawer->draw.vtable = vtables::kCGameDrawer;
    drawer->surfaces.unk04 = kAllExceptions;
    drawer->palettes.unk04 = kAllExceptions;
    drawer->devices.unk04 = kAllExceptions;
    g_drawer = self;
    return self;
}

// Purpose: shuts the framework drawer down: disconnects from DirectDraw
// and deletes its palettes and the device list.
std::uint32_t __fastcall DirectX_CGameDrawer_Shutdown(void* self, void* /*edx*/) {
    auto* const drawer = static_cast<CGameDrawer*>(self);
    CallSlot(self, kSlotDestroy);
    PtrSList* const list = &drawer->palettes;
    while (list->last != nullptr) {
        auto* const link = reinterpret_cast<Link*>(static_cast<std::uintptr_t>(
            reinterpret_cast<Member1Fn>(kListGet)(list, nullptr, 0)));
        if (link == nullptr) {
            continue;
        }
        void* const palette = link->element;
        CallSlot(list, kListSlotDestroyLink, reinterpret_cast<std::uint32_t>(link));
        if (palette != nullptr) {
            CallSlot(palette, kSlotDelete, 1);
        }
    }
    reinterpret_cast<MemberFn>(kListClearAndDestroy)(list, nullptr);
    const std::uint32_t result = reinterpret_cast<Member1Fn>(kDeviceVectorResize)(&drawer->devices, nullptr, 0);
    drawer->devices.entries = 0;
    return result;
}

// ---------------------------------------------------------------------------
// Tests: IsEqual on random GUID pairs (equal, one byte apart, random). The
// constructor on a random image with the member constructors as recording
// stubs (g_drawer compared). Shutdown on a drawer whose vtable slot 2,
// palette list slot 1 and palettes' slot 0 are recording fakes; get(0) is a
// stub that hands out the fake links in order (sometimes none) and empties
// the list after the last; clearAndDestroy and resize stubbed.
// ---------------------------------------------------------------------------

namespace {

struct Script {
    test::Rng rng{0};
    std::vector<std::uint32_t> log;
    std::uint32_t base = 0;
    std::uint32_t links = 0;  // links still in the list
    std::uint32_t handed = 0;
};
Script* g_script = nullptr;

std::uint32_t Rel(const void* p) { return reinterpret_cast<std::uint32_t>(p) - g_script->base; }

std::uint32_t __fastcall FakeDestroy(void* self, void*) {
    g_script->log.insert(g_script->log.end(), {0xd2u, Rel(self)});
    return g_script->rng.Next32();
}
std::uint32_t __fastcall FakeDestroyLink(void* self, void*, std::uint32_t link) {
    g_script->log.insert(g_script->log.end(), {0x11u, Rel(self), Rel(reinterpret_cast<void*>(link))});
    return g_script->rng.Next32();
}
std::uint32_t __fastcall FakeDeletePalette(void* self, void*, std::uint32_t flags) {
    g_script->log.insert(g_script->log.end(), {0xde1u, Rel(self), flags});
    return g_script->rng.Next32();
}

constexpr int kFakeLinks = 6;

struct Fixture {
    CGameDrawer drawer;
    std::uint8_t drawerRest[0x20];
    Link links[kFakeLinks];
    const void* palettes[kFakeLinks];  // fake palette objects (their vtable pointers)
};

enum class Target { Construct, Shutdown };

struct Outcome {
    std::uint32_t eax = 0;
    Fixture fixture;
    std::vector<std::uint32_t> log;
    std::vector<test::StubCall> calls;
    std::uint32_t drawerGlobal = 0;
};

Outcome Run(const Fixture& start, std::uint64_t seed, std::uint32_t links, std::uint32_t arg, Target target,
            ConstructFn construct, ShutdownFn shutdown) {
    Outcome out;
    Script script;
    script.rng = test::Rng(seed);
    script.links = links;
    g_script = &script;
    out.fixture = start;
    Fixture& fx = out.fixture;
    script.base = reinterpret_cast<std::uint32_t>(&fx);
    static void* drawerVtable[16] = {};
    drawerVtable[kSlotDestroy] = reinterpret_cast<void*>(&FakeDestroy);
    static void* listVtable[4] = {};
    listVtable[kListSlotDestroyLink] = reinterpret_cast<void*>(&FakeDestroyLink);
    static void* paletteVtable[1] = {reinterpret_cast<void*>(&FakeDeletePalette)};
    if (target == Target::Shutdown) {
        fx.drawer.draw.vtable = reinterpret_cast<std::uint32_t>(drawerVtable);
        fx.drawer.palettes.vtable = reinterpret_cast<std::uint32_t>(listVtable);
        fx.drawer.palettes.last = links != 0 ? &fx.links[0] : nullptr;
        for (int i = 0; i < kFakeLinks; ++i) {
            fx.palettes[i] = paletteVtable;
            fx.links[i].element = script.rng.Chance(1, 5) ? nullptr : &fx.palettes[i];
        }
    }
    void* const savedDrawer = g_drawer;
    g_drawer = reinterpret_cast<void*>(0x0d7a3d00);
    {
        test::CalleeStubs stubs;
        stubs.SetWorld(reinterpret_cast<CGameData*>(&fx));
        auto value = [&](CGameData*, std::uint32_t, const std::uint32_t*) { return script.rng.Next32(); };
        stubs.Install<1, kDirectDrawConstruct>(value);
        stubs.Install<2, kSurfaceListConstruct>(value);
        stubs.Install<2, kPaletteListConstruct>(value);
        stubs.Install<2, kDeviceVectorConstruct>(value);
        stubs.Install<2, kModeVectorConstruct>(value);
        stubs.Install<0, kListClearAndDestroy>(value);
        stubs.Install<1, kDeviceVectorResize>(value);
        stubs.Install<1, kListGet>([&](CGameData*, std::uint32_t, const std::uint32_t*) -> std::uint32_t {
            if (script.links == 0) {
                return 0;
            }
            --script.links;
            if (script.links == 0) {
                fx.drawer.palettes.last = nullptr;
            }
            if (script.rng.Chance(1, 8)) {
                return 0;  // (no link this time)
            }
            return reinterpret_cast<std::uint32_t>(&fx.links[script.handed++ % kFakeLinks]);
        });
        if (target == Target::Construct) {
            out.eax = Rel(construct(&fx.drawer, nullptr, reinterpret_cast<void*>(arg)));
        } else {
            out.eax = shutdown(&fx.drawer, nullptr);
        }
        out.calls = stubs.Take();
    }
    out.drawerGlobal = g_drawer.get() == &fx.drawer ? 1 : reinterpret_cast<std::uint32_t>(g_drawer.get());
    g_drawer = savedDrawer;
    if (target == Target::Shutdown) {
        fx.drawer.draw.vtable = 0;
        fx.drawer.palettes.vtable = 0;
        fx.drawer.palettes.last = fx.drawer.palettes.last == nullptr ? nullptr : reinterpret_cast<void*>(1);
        for (int i = 0; i < kFakeLinks; ++i) {
            fx.palettes[i] = nullptr;
            fx.links[i].element = fx.links[i].element == nullptr ? nullptr : reinterpret_cast<void*>(2);
        }
    }
    out.log = script.log;
    g_script = nullptr;
    return out;
}

void Compare(test::Context& ctx, Target target, ConstructFn construct, ShutdownFn shutdown, int cases) {
    for (int i = 0; i < cases; ++i) {
        Fixture start;
        ctx.rng().Fill(&start, sizeof(start));
        const std::uint64_t seed = ctx.rng().Next64();
        const std::uint32_t links = ctx.rng().Below(5);
        const std::uint32_t arg = ctx.rng().Next32();
        const Outcome expected = Run(start, seed, links, arg, target, Original_DirectX_CGameDrawer_Construct,
                                     Original_DirectX_CGameDrawer_Shutdown);
        const Outcome actual = Run(start, seed, links, arg, target, construct, shutdown);
        bool same = S3RE_EXPECT_EQ(expected.eax, actual.eax);
        same &= S3RE_EXPECT_MEM_EQ(&expected.fixture, &actual.fixture, sizeof(Fixture));
        same &= S3RE_EXPECT(expected.log == actual.log);
        same &= S3RE_EXPECT(expected.calls == actual.calls);
        same &= S3RE_EXPECT_EQ(expected.drawerGlobal, actual.drawerGlobal);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (target %d, links %u)", i, static_cast<int>(target), links);
            return;
        }
    }
}

// Wrong: the palette is deleted before its link is destroyed.
std::uint32_t __fastcall WrongShutdown(void* self, void* /*edx*/) {
    auto* const drawer = static_cast<CGameDrawer*>(self);
    CallSlot(self, kSlotDestroy);
    PtrSList* const list = &drawer->palettes;
    while (list->last != nullptr) {
        auto* const link = reinterpret_cast<Link*>(static_cast<std::uintptr_t>(
            reinterpret_cast<Member1Fn>(kListGet)(list, nullptr, 0)));
        if (link == nullptr) {
            continue;
        }
        if (link->element != nullptr) {
            CallSlot(link->element, kSlotDelete, 1);
        }
        CallSlot(list, kListSlotDestroyLink, reinterpret_cast<std::uint32_t>(link));
    }
    reinterpret_cast<MemberFn>(kListClearAndDestroy)(list, nullptr);
    const std::uint32_t result = reinterpret_cast<Member1Fn>(kDeviceVectorResize)(&drawer->devices, nullptr, 0);
    drawer->devices.entries = 0;
    return result;
}

}  // namespace

S3RE_TEST(DirectX_CDirectDrawDevice_IsEqual_MatchesOriginal) {
    for (int i = 0; i < 3000; ++i) {
        std::uint8_t a[16];
        std::uint8_t b[16];
        ctx.rng().Fill(a, sizeof(a));
        std::memcpy(b, a, sizeof(b));
        const std::uint32_t mode = ctx.rng().Below(3);
        if (mode == 1) {
            b[ctx.rng().Below(16)] ^= static_cast<std::uint8_t>(1 + ctx.rng().Below(255));
        } else if (mode == 2) {
            ctx.rng().Fill(b, sizeof(b));
        }
        if (!S3RE_EXPECT_EQ(Original_DirectX_CDirectDrawDevice_IsEqual(a, nullptr, b),
                            DirectX_CDirectDrawDevice_IsEqual(a, nullptr, b))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            break;
        }
    }
}
S3RE_TEST(DirectX_CGameDrawer_Construct_MatchesOriginal) {
    Compare(ctx, Target::Construct, DirectX_CGameDrawer_Construct, nullptr, 300);
}
S3RE_TEST(DirectX_CGameDrawer_Shutdown_MatchesOriginal) {
    Compare(ctx, Target::Shutdown, nullptr, DirectX_CGameDrawer_Shutdown, 1000);
}
S3RE_TEST(DirectX_CGameDrawer_Shutdown_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Compare(probe, Target::Shutdown, nullptr, &WrongShutdown, 300);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
