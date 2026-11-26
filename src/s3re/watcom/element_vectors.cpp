// Destructors of Watcom vectors whose elements have a destructor (layout
// and shared code in vector_base.h). Each address is its own copy of the
// template code; only the vtable, the element size and the way an element
// is destroyed differ. thiscall, no arguments: vtable = WCBareVectorBase<T>;
// if there is a block: the empty error hook 0x59f200, each of the `filled`
// elements destroyed (filled and the block re-read per element), operator
// delete(data); vtable = WCExcept 0x655438. EAX = operator delete's, or 0
// without a block. (The derived classes' destructors inlined the bare
// base's: the first vtable stored is the bare base's.)
//
// Elements with a virtual destructor: the element's own scalar deleting
// destructor (vtable slot 0) with flags 0.
//   address    size   T                              class (from its scalar deleting destructor or the vtable)
//   0x0044c4e0  0x54  LobbyClient::CPlayer           WCValSortedVector<LobbyClient::CPlayer>
//   0x0044d290  0x54  LobbyClient::CPlayer           WCBareVectorBase<LobbyClient::CPlayer>
//   0x005de3b0  0x54  LobbyClient::CPlayer           WCBareVectorBase<LobbyClient::CPlayer>
//   0x005de450  0x54  LobbyClient::CPlayer           WCBareVectorBase<LobbyClient::CPlayer>
//   0x0044c580 0x11c  LobbyClient::CGroup            WCValSortedVector<LobbyClient::CGroup>
//   0x0044d690 0x11c  LobbyClient::CGroup            WCBareVectorBase<LobbyClient::CGroup>
//   0x00458770 0x11c  LobbyClient::CGroup            WCBareVectorBase<LobbyClient::CGroup>
//   0x00458980 0x11c  LobbyClient::CGroup            WCBareVectorBase<LobbyClient::CGroup>
//   0x0044cbe0  0x20  LobbyClient::CChatMessage      WCValOrderedVector<LobbyClient::CChatMessage>
//   0x0044dd10  0x20  LobbyClient::CChatMessage      WCBareVectorBase<LobbyClient::CChatMessage>
//   0x0044de40  0x20  LobbyClient::CChatMessage      WCBareVectorBase<LobbyClient::CChatMessage>
//   0x00458d80  0x20  LobbyClient::CChatMessage      WCBareVectorBase<LobbyClient::CChatMessage>
//   0x00609940  0x14  Lobby::CLMGeneric              WCValOrderedVector<Lobby::CLMGeneric>
//   0x0060a130  0x14  Lobby::CLMGeneric              WCBareVectorBase<Lobby::CLMGeneric>
//   0x0060a590  0x14  Lobby::CLMGeneric              WCBareVectorBase<Lobby::CLMGeneric>
//   0x0060a630  0x14  Lobby::CLMGeneric              WCBareVectorBase<Lobby::CLMGeneric>
//   0x0044cb20 0x17c  LobbyClient::CPrivateChat      WCValOrderedVector<LobbyClient::CPrivateChat>
//   0x0044db90 0x17c  LobbyClient::CPrivateChat      WCBareVectorBase<LobbyClient::CPrivateChat>
//   0x005dfd90 0x17c  LobbyClient::CPrivateChat      WCBareVectorBase<LobbyClient::CPrivateChat>
//   0x004627b0  0x40  DirectX::CConnectionShortcut   WCValOrderedVector<DirectX::CConnectionShortcut>
//   0x00468420  0x40  DirectX::CConnectionShortcut   WCBareVectorBase<DirectX::CConnectionShortcut>
//   0x00472b10  0x40  DirectX::CConnectionShortcut   WCBareVectorBase<DirectX::CConnectionShortcut>
// Elements destroyed by a direct call of their destructor:
//   0x00427fe0  0x20  CListView::CColumn (~CColumn 0x4275e0)  WCValOrderedVector<CListView::CColumn>
//   0x004290f0  0x20  CListView::CColumn                      WCBareVectorBase<CListView::CColumn>
//   0x00428070  0x14  CListView::CLine (~CLine 0x4277d0)      WCValOrderedVector<CListView::CLine>
//   0x00429480  0x14  CListView::CLine                        WCBareVectorBase<CListView::CLine>

#include "watcom/vector_base.h"

#include "game/vtables.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::watcom {

namespace vt = game::vtables;

using ElementVectorDestructorFn = std::uint32_t(__fastcall*)(VectorBase* self, void* edx);

namespace {

using ElementDeletingDestructorFn = std::uint32_t(__fastcall*)(std::uint32_t self, void* edx, std::uint32_t flags);

// Each element through its own vtable's slot 0 (the scalar deleting
// destructor), flags 0: destroy, don't free.
std::uint32_t DestroyVirtualElementVector(VectorBase* self, std::uint32_t vtable, std::uint32_t elementSize) {
    return DestroyVector(self, vtable, vt::kWCExcept, elementSize, [](std::uint32_t element) {
        const std::uint32_t elementVtable = *reinterpret_cast<const std::uint32_t*>(element);
        const auto destroy = *reinterpret_cast<const ElementDeletingDestructorFn*>(elementVtable);
        destroy(element, nullptr, 0);
    });
}

constexpr std::uintptr_t kDestroyColumn = 0x004275e0;  // CListView::CColumn::~CColumn
constexpr std::uintptr_t kDestroyLine = 0x004277d0;    // CListView::CLine::~CLine
constexpr std::uint32_t kColumnSize = 0x20;
constexpr std::uint32_t kLineSize = 0x14;

template <std::uintptr_t Destroy>
std::uint32_t DestroyDirectElementVector(VectorBase* self, std::uint32_t vtable, std::uint32_t elementSize) {
    return DestroyVector(self, vtable, vt::kWCExcept, elementSize, [](std::uint32_t element) {
        reinterpret_cast<void(__fastcall*)(std::uint32_t, void*)>(Destroy)(element, nullptr);
    });
}

constexpr std::uint32_t kLobbyPlayerSize = 0x54;
constexpr std::uint32_t kGroupSize = 0x11c;
constexpr std::uint32_t kChatMessageSize = 0x20;
constexpr std::uint32_t kGenericMessageSize = 0x14;
constexpr std::uint32_t kPrivateChatSize = 0x17c;
constexpr std::uint32_t kConnectionShortcutSize = 0x40;

}  // namespace

// Purpose: Destructors of the lobby's vectors of players, groups, chat
// messages, private chats and protocol messages, and of the network's
// connection shortcuts: destroy each element, then free the block.
std::uint32_t __fastcall ElementVectorDestructor_44c4e0(VectorBase* self, void*) {
    return DestroyVirtualElementVector(self, vt::kWCBareVectorBase_LobbyPlayer, kLobbyPlayerSize);
}
std::uint32_t __fastcall ElementVectorDestructor_44d290(VectorBase* self, void*) {
    return DestroyVirtualElementVector(self, vt::kWCBareVectorBase_LobbyPlayer, kLobbyPlayerSize);
}
std::uint32_t __fastcall ElementVectorDestructor_5de3b0(VectorBase* self, void*) {
    return DestroyVirtualElementVector(self, vt::kWCBareVectorBase_LobbyPlayer, kLobbyPlayerSize);
}
std::uint32_t __fastcall ElementVectorDestructor_5de450(VectorBase* self, void*) {
    return DestroyVirtualElementVector(self, vt::kWCBareVectorBase_LobbyPlayer, kLobbyPlayerSize);
}
std::uint32_t __fastcall ElementVectorDestructor_44c580(VectorBase* self, void*) {
    return DestroyVirtualElementVector(self, vt::kWCBareVectorBase_CGroup, kGroupSize);
}
std::uint32_t __fastcall ElementVectorDestructor_44d690(VectorBase* self, void*) {
    return DestroyVirtualElementVector(self, vt::kWCBareVectorBase_CGroup, kGroupSize);
}
std::uint32_t __fastcall ElementVectorDestructor_458770(VectorBase* self, void*) {
    return DestroyVirtualElementVector(self, vt::kWCBareVectorBase_CGroup, kGroupSize);
}
std::uint32_t __fastcall ElementVectorDestructor_458980(VectorBase* self, void*) {
    return DestroyVirtualElementVector(self, vt::kWCBareVectorBase_CGroup, kGroupSize);
}
std::uint32_t __fastcall ElementVectorDestructor_44cbe0(VectorBase* self, void*) {
    return DestroyVirtualElementVector(self, vt::kWCBareVectorBase_CChatMessage, kChatMessageSize);
}
std::uint32_t __fastcall ElementVectorDestructor_44dd10(VectorBase* self, void*) {
    return DestroyVirtualElementVector(self, vt::kWCBareVectorBase_CChatMessage, kChatMessageSize);
}
std::uint32_t __fastcall ElementVectorDestructor_44de40(VectorBase* self, void*) {
    return DestroyVirtualElementVector(self, vt::kWCBareVectorBase_CChatMessage, kChatMessageSize);
}
std::uint32_t __fastcall ElementVectorDestructor_458d80(VectorBase* self, void*) {
    return DestroyVirtualElementVector(self, vt::kWCBareVectorBase_CChatMessage, kChatMessageSize);
}
std::uint32_t __fastcall ElementVectorDestructor_609940(VectorBase* self, void*) {
    return DestroyVirtualElementVector(self, vt::kWCBareVectorBase_CLMGeneric, kGenericMessageSize);
}
std::uint32_t __fastcall ElementVectorDestructor_60a130(VectorBase* self, void*) {
    return DestroyVirtualElementVector(self, vt::kWCBareVectorBase_CLMGeneric, kGenericMessageSize);
}
std::uint32_t __fastcall ElementVectorDestructor_60a590(VectorBase* self, void*) {
    return DestroyVirtualElementVector(self, vt::kWCBareVectorBase_CLMGeneric, kGenericMessageSize);
}
std::uint32_t __fastcall ElementVectorDestructor_60a630(VectorBase* self, void*) {
    return DestroyVirtualElementVector(self, vt::kWCBareVectorBase_CLMGeneric, kGenericMessageSize);
}
std::uint32_t __fastcall ElementVectorDestructor_44cb20(VectorBase* self, void*) {
    return DestroyVirtualElementVector(self, vt::kWCBareVectorBase_CPrivateChat, kPrivateChatSize);
}
std::uint32_t __fastcall ElementVectorDestructor_44db90(VectorBase* self, void*) {
    return DestroyVirtualElementVector(self, vt::kWCBareVectorBase_CPrivateChat, kPrivateChatSize);
}
std::uint32_t __fastcall ElementVectorDestructor_5dfd90(VectorBase* self, void*) {
    return DestroyVirtualElementVector(self, vt::kWCBareVectorBase_CPrivateChat, kPrivateChatSize);
}
std::uint32_t __fastcall ElementVectorDestructor_4627b0(VectorBase* self, void*) {
    return DestroyVirtualElementVector(self, vt::kWCBareVectorBase_CConnectionShortcut, kConnectionShortcutSize);
}
std::uint32_t __fastcall ElementVectorDestructor_468420(VectorBase* self, void*) {
    return DestroyVirtualElementVector(self, vt::kWCBareVectorBase_CConnectionShortcut, kConnectionShortcutSize);
}
std::uint32_t __fastcall ElementVectorDestructor_472b10(VectorBase* self, void*) {
    return DestroyVirtualElementVector(self, vt::kWCBareVectorBase_CConnectionShortcut, kConnectionShortcutSize);
}

// Purpose: Destructors of the list views' vectors of columns and lines.
std::uint32_t __fastcall ElementVectorDestructor_427fe0(VectorBase* self, void*) {
    return DestroyDirectElementVector<kDestroyColumn>(self, vt::kWCBareVectorBase_CColumn, kColumnSize);
}
std::uint32_t __fastcall ElementVectorDestructor_4290f0(VectorBase* self, void*) {
    return DestroyDirectElementVector<kDestroyColumn>(self, vt::kWCBareVectorBase_CColumn, kColumnSize);
}
std::uint32_t __fastcall ElementVectorDestructor_428070(VectorBase* self, void*) {
    return DestroyDirectElementVector<kDestroyLine>(self, vt::kWCBareVectorBase_CLine, kLineSize);
}
std::uint32_t __fastcall ElementVectorDestructor_429480(VectorBase* self, void*) {
    return DestroyDirectElementVector<kDestroyLine>(self, vt::kWCBareVectorBase_CLine, kLineSize);
}

S3RE_HOOK(0x0044c4e0, ElementVectorDestructor_44c4e0, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_44c4e0, ElementVectorDestructor_44c4e0);
S3RE_HOOK(0x0044d290, ElementVectorDestructor_44d290, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_44d290, ElementVectorDestructor_44d290);
S3RE_HOOK(0x005de3b0, ElementVectorDestructor_5de3b0, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_5de3b0, ElementVectorDestructor_5de3b0);
S3RE_HOOK(0x005de450, ElementVectorDestructor_5de450, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_5de450, ElementVectorDestructor_5de450);
S3RE_HOOK(0x0044c580, ElementVectorDestructor_44c580, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_44c580, ElementVectorDestructor_44c580);
S3RE_HOOK(0x0044d690, ElementVectorDestructor_44d690, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_44d690, ElementVectorDestructor_44d690);
S3RE_HOOK(0x00458770, ElementVectorDestructor_458770, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_458770, ElementVectorDestructor_458770);
S3RE_HOOK(0x00458980, ElementVectorDestructor_458980, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_458980, ElementVectorDestructor_458980);
S3RE_HOOK(0x0044cbe0, ElementVectorDestructor_44cbe0, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_44cbe0, ElementVectorDestructor_44cbe0);
S3RE_HOOK(0x0044dd10, ElementVectorDestructor_44dd10, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_44dd10, ElementVectorDestructor_44dd10);
S3RE_HOOK(0x0044de40, ElementVectorDestructor_44de40, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_44de40, ElementVectorDestructor_44de40);
S3RE_HOOK(0x00458d80, ElementVectorDestructor_458d80, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_458d80, ElementVectorDestructor_458d80);
S3RE_HOOK(0x00609940, ElementVectorDestructor_609940, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_609940, ElementVectorDestructor_609940);
S3RE_HOOK(0x0060a130, ElementVectorDestructor_60a130, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_60a130, ElementVectorDestructor_60a130);
S3RE_HOOK(0x0060a590, ElementVectorDestructor_60a590, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_60a590, ElementVectorDestructor_60a590);
S3RE_HOOK(0x0060a630, ElementVectorDestructor_60a630, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_60a630, ElementVectorDestructor_60a630);
S3RE_HOOK(0x0044cb20, ElementVectorDestructor_44cb20, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_44cb20, ElementVectorDestructor_44cb20);
S3RE_HOOK(0x0044db90, ElementVectorDestructor_44db90, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_44db90, ElementVectorDestructor_44db90);
S3RE_HOOK(0x005dfd90, ElementVectorDestructor_5dfd90, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_5dfd90, ElementVectorDestructor_5dfd90);
S3RE_HOOK(0x004627b0, ElementVectorDestructor_4627b0, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_4627b0, ElementVectorDestructor_4627b0);
S3RE_HOOK(0x00468420, ElementVectorDestructor_468420, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_468420, ElementVectorDestructor_468420);
S3RE_HOOK(0x00472b10, ElementVectorDestructor_472b10, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_472b10, ElementVectorDestructor_472b10);
S3RE_HOOK(0x00427fe0, ElementVectorDestructor_427fe0, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_427fe0, ElementVectorDestructor_427fe0);
S3RE_HOOK(0x004290f0, ElementVectorDestructor_4290f0, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_4290f0, ElementVectorDestructor_4290f0);
S3RE_HOOK(0x00428070, ElementVectorDestructor_428070, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_428070, ElementVectorDestructor_428070);
S3RE_HOOK(0x00429480, ElementVectorDestructor_429480, ElementVectorDestructorFn,
          Original_ElementVectorDestructor_429480, ElementVectorDestructor_429480);

// ---------------------------------------------------------------------------
// Tests: random objects with a block from the game's operator new (or
// none), filled -2..6. The block's elements point at a fake vtable whose
// slot 0 records (element offset, flags); ~CColumn and ~CLine are replaced
// by recording stubs. Compared: the calls, the objects' bytes and, without a
// block, EAX (with one it is the heap's).
// ---------------------------------------------------------------------------

namespace {

struct ElementPair {
    ElementVectorDestructorFn original;
    ElementVectorDestructorFn ours;
    std::uint32_t address;
    std::uint32_t elementSize;
};

const ElementPair kElementPairs[] = {
    {Original_ElementVectorDestructor_44c4e0, &ElementVectorDestructor_44c4e0, 0x44c4e0, kLobbyPlayerSize},
    {Original_ElementVectorDestructor_44d290, &ElementVectorDestructor_44d290, 0x44d290, kLobbyPlayerSize},
    {Original_ElementVectorDestructor_5de3b0, &ElementVectorDestructor_5de3b0, 0x5de3b0, kLobbyPlayerSize},
    {Original_ElementVectorDestructor_5de450, &ElementVectorDestructor_5de450, 0x5de450, kLobbyPlayerSize},
    {Original_ElementVectorDestructor_44c580, &ElementVectorDestructor_44c580, 0x44c580, kGroupSize},
    {Original_ElementVectorDestructor_44d690, &ElementVectorDestructor_44d690, 0x44d690, kGroupSize},
    {Original_ElementVectorDestructor_458770, &ElementVectorDestructor_458770, 0x458770, kGroupSize},
    {Original_ElementVectorDestructor_458980, &ElementVectorDestructor_458980, 0x458980, kGroupSize},
    {Original_ElementVectorDestructor_44cbe0, &ElementVectorDestructor_44cbe0, 0x44cbe0, kChatMessageSize},
    {Original_ElementVectorDestructor_44dd10, &ElementVectorDestructor_44dd10, 0x44dd10, kChatMessageSize},
    {Original_ElementVectorDestructor_44de40, &ElementVectorDestructor_44de40, 0x44de40, kChatMessageSize},
    {Original_ElementVectorDestructor_458d80, &ElementVectorDestructor_458d80, 0x458d80, kChatMessageSize},
    {Original_ElementVectorDestructor_609940, &ElementVectorDestructor_609940, 0x609940, kGenericMessageSize},
    {Original_ElementVectorDestructor_60a130, &ElementVectorDestructor_60a130, 0x60a130, kGenericMessageSize},
    {Original_ElementVectorDestructor_60a590, &ElementVectorDestructor_60a590, 0x60a590, kGenericMessageSize},
    {Original_ElementVectorDestructor_60a630, &ElementVectorDestructor_60a630, 0x60a630, kGenericMessageSize},
    {Original_ElementVectorDestructor_44cb20, &ElementVectorDestructor_44cb20, 0x44cb20, kPrivateChatSize},
    {Original_ElementVectorDestructor_44db90, &ElementVectorDestructor_44db90, 0x44db90, kPrivateChatSize},
    {Original_ElementVectorDestructor_5dfd90, &ElementVectorDestructor_5dfd90, 0x5dfd90, kPrivateChatSize},
    {Original_ElementVectorDestructor_4627b0, &ElementVectorDestructor_4627b0, 0x4627b0, kConnectionShortcutSize},
    {Original_ElementVectorDestructor_468420, &ElementVectorDestructor_468420, 0x468420, kConnectionShortcutSize},
    {Original_ElementVectorDestructor_472b10, &ElementVectorDestructor_472b10, 0x472b10, kConnectionShortcutSize},
    {Original_ElementVectorDestructor_427fe0, &ElementVectorDestructor_427fe0, 0x427fe0, kColumnSize},
    {Original_ElementVectorDestructor_4290f0, &ElementVectorDestructor_4290f0, 0x4290f0, kColumnSize},
    {Original_ElementVectorDestructor_428070, &ElementVectorDestructor_428070, 0x428070, kLineSize},
    {Original_ElementVectorDestructor_429480, &ElementVectorDestructor_429480, 0x429480, kLineSize},
};

constexpr int kMaxElements = 6;

// The fake element vtable's slot 0: records (offset into the block, flags).
std::uint32_t g_block = 0;
std::vector<std::uint32_t> g_elementCalls;

std::uint32_t __fastcall RecordElementDestroy(std::uint32_t self, void*, std::uint32_t flags) {
    g_elementCalls.push_back(self - g_block);
    g_elementCalls.push_back(flags);
    return self;
}
const std::uint32_t kFakeElementVtable[1] = {reinterpret_cast<std::uint32_t>(&RecordElementDestroy)};

struct Run {
    std::uint32_t eax = 0;
    std::vector<std::uint32_t> elementCalls;
    std::vector<test::StubCall> stubCalls;
};

Run RunDestructor(ElementVectorDestructorFn fn, VectorBase& vector, std::uint32_t elementSize) {
    Run run;
    g_block = vector.data;
    g_elementCalls.clear();
    if (vector.data != 0) {
        for (int k = 0; k < kMaxElements; ++k) {
            *reinterpret_cast<std::uint32_t*>(vector.data + k * elementSize) =
                reinterpret_cast<std::uint32_t>(kFakeElementVtable);
        }
    }
    test::CalleeStubs stubs;
    stubs.Install<0, kDestroyColumn>();
    stubs.Install<0, kDestroyLine>();
    stubs.SetWorld(reinterpret_cast<game::CGameData*>(vector.data));
    run.eax = fn(&vector, nullptr);
    run.stubCalls = stubs.Take();
    run.elementCalls = g_elementCalls;
    return run;
}

bool CompareElementDestructor(test::Context& ctx, const ElementPair& pair, int cases) {
    for (int i = 0; i < cases; ++i) {
        VectorBase a, b;
        ctx.rng().Fill(&a, sizeof(a));
        a.filled = ctx.rng().Range(-2, kMaxElements);
        std::memcpy(&b, &a, sizeof(a));
        const bool block = ctx.rng().Chance(3, 4);
        const std::uint32_t bytes = kMaxElements * pair.elementSize;
        a.data = block ? reinterpret_cast<std::uintptr_t>(msvc6::GameOperatorNew(bytes)) : 0;
        b.data = block ? reinterpret_cast<std::uintptr_t>(msvc6::GameOperatorNew(bytes)) : 0;
        const Run ra = RunDestructor(pair.original, a, pair.elementSize);
        const Run rb = RunDestructor(pair.ours, b, pair.elementSize);
        a.data = b.data = 0;  // freed (or none)
        bool same = S3RE_EXPECT(ra.elementCalls == rb.elementCalls) && S3RE_EXPECT(ra.stubCalls == rb.stubCalls) &&
                    S3RE_EXPECT_MEM_EQ(&a, &b, sizeof(a));
        if (!block) {
            same = S3RE_EXPECT_EQ(ra.eax, rb.eax) && same;
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  0x%x case %d: block %d, filled %d", pair.address, i, block, a.filled);
            return false;
        }
    }
    return true;
}

// Wrong: the elements destroyed with flags 1 (as if deleted).
std::uint32_t __fastcall WrongElementDestructor(VectorBase* self, void*) {
    return DestroyVector(self, vt::kWCBareVectorBase_CGroup, vt::kWCExcept, kGroupSize, [](std::uint32_t element) {
        const std::uint32_t elementVtable = *reinterpret_cast<const std::uint32_t*>(element);
        (*reinterpret_cast<const ElementDeletingDestructorFn*>(elementVtable))(element, nullptr, 1);
    });
}
// Wrong: the line vector with the column size.
std::uint32_t __fastcall WrongLineDestructor(VectorBase* self, void*) {
    return DestroyDirectElementVector<kDestroyLine>(self, vt::kWCBareVectorBase_CLine, kColumnSize);
}

}  // namespace

S3RE_TEST(WatcomElementVectorDestructors_MatchOriginal) {
    for (const ElementPair& pair : kElementPairs) {
        if (!CompareElementDestructor(ctx, pair, 300)) {
            return;
        }
    }
}

S3RE_TEST(WatcomElementVectorDestructors_ComparisonDetectsWrongImplementation) {
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareElementDestructor(probe, {Original_ElementVectorDestructor_44c580, &WrongElementDestructor, 0x44c580,
                                         kGroupSize}, 50);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareElementDestructor(probe, {Original_ElementVectorDestructor_428070, &WrongLineDestructor, 0x428070,
                                         kLineSize}, 50);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::watcom
