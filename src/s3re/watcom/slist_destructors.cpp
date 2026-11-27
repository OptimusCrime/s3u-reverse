// Destructors of the Watcom singly linked lists (WCPtrSList<T>,
// WCValSList<T>, the WCStack / WCQueue built on them) and of their
// intrusive list bases (template instantiations; only the vtables differ).
// thiscall, no arguments. Every one ends in ~WCIsvListBase (0x60b5a0, ours)
// and returns what it leaves in EAX.
//
// WCValListBase<T, WCIsvSListBase<WCNIsvSLink<T>>, ...>::~ (inlined into
// the list, stack and queue destructors): vtable = the value-list base's;
// if the list is not empty (+8, its last link): the library's empty error
// hook 0x59f200, clearAndDestroy (0x60b560, ours); vtable = the
// WCIsvSListBase's; ~WCIsvListBase.
//   address    class                                        value-list base  isv base
//   0x00427ea0 WCStack<CScreenElement*, WCValSList<..>>      0x655bd0         0x655bc4
//   0x00427f10 WCStack<bool, WCValSList<bool>>               0x655bec         0x655be0
//   0x00428290 WCQueue<MsgConsole, WCValSList<MsgConsole>>   0x656a4c         0x656a40
//   0x004628f0 WCPtrSList<Win32::CWin32Waitable>             0x657430         0x657424
//   0x00462bc0 WCValSList<Win32::CWin32Waitable*>            0x657430         0x657424
//   0x00462f80 WCValListBase<Win32::CWin32Waitable*, ..>     0x657430         0x657424
//   0x005b7950 WCPtrSList<DirectX::CGameThread>              0x65c928         0x65c91c
//   0x005b79c0 WCPtrSList<DirectX::CSurface>                 0x65caf8         0x65cb54
//   0x005b7a30 WCPtrSList<DirectX::CPalette>                 0x65cb14         0x65cb08
//   0x005b7d90 WCValSList<DirectX::CSurface*>                0x65caf8         0x65cb54
//   0x005b7e50 WCValSList<DirectX::CPalette*>                0x65cb14         0x65cb08
//   0x005ddaf0 WCQueue<LobbyClient::CStatusMessage, ..>      0x65d2d0         0x65d2c4
//   0x005ddd70 WCValSList<LobbyClient::CStatusMessage>       0x65d2d0         0x65d2c4
//   0x005dfe80 WCValListBase<LobbyClient::CStatusMessage, ..> 0x65d2d0        0x65d2c4
// WCIsvSListBase<WCNIsvSLink<T>>::~: vtable = V; ~WCIsvListBase (a tail
// jump).
//   0x00428fd0 <CScreenElement*> 0x655bc4    0x00428fe0 <bool> 0x655be0
//   0x00428ff0 <MsgConsole> 0x656a40         0x00462f70 <Win32::CWin32Waitable*> 0x657424
//   0x005b7fd0 <DirectX::CGameThread*> 0x65c91c  0x005b7fe0 <DirectX::CSurface*> 0x65cb54
//   0x005b8030 <DirectX::CPalette*> 0x65cb08     0x005dfe30 <LobbyClient::CStatusMessage> 0x65d2c4
// (0x00462f70 and 0x005dfe30 were named as constructors; they are these
// destructors.)

#include "game/vtables.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::watcom {

namespace vt = game::vtables;

#pragma pack(push, 1)
struct SListObject {
    std::uint32_t vtable;
    std::uint32_t unk04;
    std::uint32_t last;  // +0x08 the last link (0: empty)
    std::uint32_t count;  // +0x0c
};
#pragma pack(pop)
static_assert(sizeof(SListObject) == 0x10);

using SListDestructorFn = std::uint32_t(__fastcall*)(SListObject* self, void* edx);

namespace {

constexpr std::uintptr_t kClearAndDestroy = 0x0060b560;   // WCIsvSListBase::clearAndDestroy (ours)
constexpr std::uintptr_t kIsvListBaseDestroy = 0x0060b5a0;  // WCIsvListBase::~WCIsvListBase (ours)

using ThisFn = std::uint32_t(__fastcall*)(SListObject* self, void* edx);

std::uint32_t DestroyIsvSList(SListObject* self, std::uint32_t isvVtable) {
    self->vtable = isvVtable;
    return reinterpret_cast<ThisFn>(kIsvListBaseDestroy)(self, nullptr);
}

std::uint32_t DestroyValList(SListObject* self, std::uint32_t valueListVtable, std::uint32_t isvVtable) {
    self->vtable = valueListVtable;
    if (self->last != 0) {
        // (the library's empty error hook 0x59f200: "not empty")
        reinterpret_cast<ThisFn>(kClearAndDestroy)(self, nullptr);
    }
    return DestroyIsvSList(self, isvVtable);
}

}  // namespace

// Purpose: Destructors of the screens' element stacks and message queues,
// the network thread's waitable lists, the draw thread's surface, palette
// and thread lists and the lobby's status-message queues: destroy every
// link, then the list base.
std::uint32_t __fastcall SListDestructor_427ea0(SListObject* self, void*) {
    return DestroyValList(self, vt::kWCValListBase_CScreenElementP, vt::kWCIsvSListBase_CScreenElementP);
}
std::uint32_t __fastcall SListDestructor_427f10(SListObject* self, void*) {
    return DestroyValList(self, vt::kWCValListBase_bool, vt::kWCIsvSListBase_bool);
}
std::uint32_t __fastcall SListDestructor_428290(SListObject* self, void*) {
    return DestroyValList(self, vt::kWCValListBase_MsgConsole, vt::kWCIsvSListBase_MsgConsole);
}
std::uint32_t __fastcall SListDestructor_4628f0(SListObject* self, void*) {
    return DestroyValList(self, vt::kWaitableValListBase, vt::kWaitableIsvSListBase);
}
std::uint32_t __fastcall SListDestructor_462bc0(SListObject* self, void*) {
    return DestroyValList(self, vt::kWaitableValListBase, vt::kWaitableIsvSListBase);
}
std::uint32_t __fastcall SListDestructor_462f80(SListObject* self, void*) {
    return DestroyValList(self, vt::kWaitableValListBase, vt::kWaitableIsvSListBase);
}
std::uint32_t __fastcall SListDestructor_5b7950(SListObject* self, void*) {
    return DestroyValList(self, vt::kWCValListBase_CGameThreadP, vt::kWCIsvSListBase_CGameThreadP);
}
std::uint32_t __fastcall SListDestructor_5b79c0(SListObject* self, void*) {
    return DestroyValList(self, vt::kWCValListBase_CSurfaceP, vt::kWCIsvSListBase_CSurfaceP);
}
std::uint32_t __fastcall SListDestructor_5b7a30(SListObject* self, void*) {
    return DestroyValList(self, vt::kWCValListBase_CPaletteP, vt::kWCIsvSListBase_CPaletteP);
}
std::uint32_t __fastcall SListDestructor_5b7d90(SListObject* self, void*) {
    return DestroyValList(self, vt::kWCValListBase_CSurfaceP, vt::kWCIsvSListBase_CSurfaceP);
}
std::uint32_t __fastcall SListDestructor_5b7e50(SListObject* self, void*) {
    return DestroyValList(self, vt::kWCValListBase_CPaletteP, vt::kWCIsvSListBase_CPaletteP);
}
std::uint32_t __fastcall SListDestructor_5ddaf0(SListObject* self, void*) {
    return DestroyValList(self, vt::kWCValListBase_CStatusMessage, vt::kWCIsvSListBase_CStatusMessage);
}
std::uint32_t __fastcall SListDestructor_5ddd70(SListObject* self, void*) {
    return DestroyValList(self, vt::kWCValListBase_CStatusMessage, vt::kWCIsvSListBase_CStatusMessage);
}
std::uint32_t __fastcall SListDestructor_5dfe80(SListObject* self, void*) {
    return DestroyValList(self, vt::kWCValListBase_CStatusMessage, vt::kWCIsvSListBase_CStatusMessage);
}

// Purpose: Destructors of the intrusive list bases of those lists.
std::uint32_t __fastcall IsvSListDestructor_428fd0(SListObject* self, void*) {
    return DestroyIsvSList(self, vt::kWCIsvSListBase_CScreenElementP);
}
std::uint32_t __fastcall IsvSListDestructor_428fe0(SListObject* self, void*) {
    return DestroyIsvSList(self, vt::kWCIsvSListBase_bool);
}
std::uint32_t __fastcall IsvSListDestructor_428ff0(SListObject* self, void*) {
    return DestroyIsvSList(self, vt::kWCIsvSListBase_MsgConsole);
}
std::uint32_t __fastcall IsvSListDestructor_462f70(SListObject* self, void*) {
    return DestroyIsvSList(self, vt::kWaitableIsvSListBase);
}
std::uint32_t __fastcall IsvSListDestructor_5b7fd0(SListObject* self, void*) {
    return DestroyIsvSList(self, vt::kWCIsvSListBase_CGameThreadP);
}
std::uint32_t __fastcall IsvSListDestructor_5b7fe0(SListObject* self, void*) {
    return DestroyIsvSList(self, vt::kWCIsvSListBase_CSurfaceP);
}
std::uint32_t __fastcall IsvSListDestructor_5b8030(SListObject* self, void*) {
    return DestroyIsvSList(self, vt::kWCIsvSListBase_CPaletteP);
}
std::uint32_t __fastcall IsvSListDestructor_5dfe30(SListObject* self, void*) {
    return DestroyIsvSList(self, vt::kWCIsvSListBase_CStatusMessage);
}

S3RE_HOOK(0x00427ea0, SListDestructor_427ea0, SListDestructorFn, Original_SListDestructor_427ea0,
          SListDestructor_427ea0);
S3RE_HOOK(0x00427f10, SListDestructor_427f10, SListDestructorFn, Original_SListDestructor_427f10,
          SListDestructor_427f10);
S3RE_HOOK(0x00428290, SListDestructor_428290, SListDestructorFn, Original_SListDestructor_428290,
          SListDestructor_428290);
S3RE_HOOK(0x004628f0, SListDestructor_4628f0, SListDestructorFn, Original_SListDestructor_4628f0,
          SListDestructor_4628f0);
S3RE_HOOK(0x00462bc0, SListDestructor_462bc0, SListDestructorFn, Original_SListDestructor_462bc0,
          SListDestructor_462bc0);
S3RE_HOOK(0x00462f80, SListDestructor_462f80, SListDestructorFn, Original_SListDestructor_462f80,
          SListDestructor_462f80);
S3RE_HOOK(0x005b7950, SListDestructor_5b7950, SListDestructorFn, Original_SListDestructor_5b7950,
          SListDestructor_5b7950);
S3RE_HOOK(0x005b79c0, SListDestructor_5b79c0, SListDestructorFn, Original_SListDestructor_5b79c0,
          SListDestructor_5b79c0);
S3RE_HOOK(0x005b7a30, SListDestructor_5b7a30, SListDestructorFn, Original_SListDestructor_5b7a30,
          SListDestructor_5b7a30);
S3RE_HOOK(0x005b7d90, SListDestructor_5b7d90, SListDestructorFn, Original_SListDestructor_5b7d90,
          SListDestructor_5b7d90);
S3RE_HOOK(0x005b7e50, SListDestructor_5b7e50, SListDestructorFn, Original_SListDestructor_5b7e50,
          SListDestructor_5b7e50);
S3RE_HOOK(0x005ddaf0, SListDestructor_5ddaf0, SListDestructorFn, Original_SListDestructor_5ddaf0,
          SListDestructor_5ddaf0);
S3RE_HOOK(0x005ddd70, SListDestructor_5ddd70, SListDestructorFn, Original_SListDestructor_5ddd70,
          SListDestructor_5ddd70);
S3RE_HOOK(0x005dfe80, SListDestructor_5dfe80, SListDestructorFn, Original_SListDestructor_5dfe80,
          SListDestructor_5dfe80);
S3RE_HOOK(0x00428fd0, IsvSListDestructor_428fd0, SListDestructorFn, Original_IsvSListDestructor_428fd0,
          IsvSListDestructor_428fd0);
S3RE_HOOK(0x00428fe0, IsvSListDestructor_428fe0, SListDestructorFn, Original_IsvSListDestructor_428fe0,
          IsvSListDestructor_428fe0);
S3RE_HOOK(0x00428ff0, IsvSListDestructor_428ff0, SListDestructorFn, Original_IsvSListDestructor_428ff0,
          IsvSListDestructor_428ff0);
S3RE_HOOK(0x00462f70, IsvSListDestructor_462f70, SListDestructorFn, Original_IsvSListDestructor_462f70,
          IsvSListDestructor_462f70);
S3RE_HOOK(0x005b7fd0, IsvSListDestructor_5b7fd0, SListDestructorFn, Original_IsvSListDestructor_5b7fd0,
          IsvSListDestructor_5b7fd0);
S3RE_HOOK(0x005b7fe0, IsvSListDestructor_5b7fe0, SListDestructorFn, Original_IsvSListDestructor_5b7fe0,
          IsvSListDestructor_5b7fe0);
S3RE_HOOK(0x005b8030, IsvSListDestructor_5b8030, SListDestructorFn, Original_IsvSListDestructor_5b8030,
          IsvSListDestructor_5b8030);
S3RE_HOOK(0x005dfe30, IsvSListDestructor_5dfe30, SListDestructorFn, Original_IsvSListDestructor_5dfe30,
          IsvSListDestructor_5dfe30);

// ---------------------------------------------------------------------------
// Tests: random objects (the last link null in a third of the cases);
// clearAndDestroy and ~WCIsvListBase replaced by recording stubs that
// return a value fixed per case. Compared: the calls (with the vtable the
// object holds at each call), EAX and the object bytes.
// ---------------------------------------------------------------------------

namespace {

struct SListPair {
    SListDestructorFn original;
    SListDestructorFn ours;
    std::uint32_t address;
};

const SListPair kSListPairs[] = {
    {Original_SListDestructor_427ea0, &SListDestructor_427ea0, 0x427ea0},
    {Original_SListDestructor_427f10, &SListDestructor_427f10, 0x427f10},
    {Original_SListDestructor_428290, &SListDestructor_428290, 0x428290},
    {Original_SListDestructor_4628f0, &SListDestructor_4628f0, 0x4628f0},
    {Original_SListDestructor_462bc0, &SListDestructor_462bc0, 0x462bc0},
    {Original_SListDestructor_462f80, &SListDestructor_462f80, 0x462f80},
    {Original_SListDestructor_5b7950, &SListDestructor_5b7950, 0x5b7950},
    {Original_SListDestructor_5b79c0, &SListDestructor_5b79c0, 0x5b79c0},
    {Original_SListDestructor_5b7a30, &SListDestructor_5b7a30, 0x5b7a30},
    {Original_SListDestructor_5b7d90, &SListDestructor_5b7d90, 0x5b7d90},
    {Original_SListDestructor_5b7e50, &SListDestructor_5b7e50, 0x5b7e50},
    {Original_SListDestructor_5ddaf0, &SListDestructor_5ddaf0, 0x5ddaf0},
    {Original_SListDestructor_5ddd70, &SListDestructor_5ddd70, 0x5ddd70},
    {Original_SListDestructor_5dfe80, &SListDestructor_5dfe80, 0x5dfe80},
    {Original_IsvSListDestructor_428fd0, &IsvSListDestructor_428fd0, 0x428fd0},
    {Original_IsvSListDestructor_428fe0, &IsvSListDestructor_428fe0, 0x428fe0},
    {Original_IsvSListDestructor_428ff0, &IsvSListDestructor_428ff0, 0x428ff0},
    {Original_IsvSListDestructor_462f70, &IsvSListDestructor_462f70, 0x462f70},
    {Original_IsvSListDestructor_5b7fd0, &IsvSListDestructor_5b7fd0, 0x5b7fd0},
    {Original_IsvSListDestructor_5b7fe0, &IsvSListDestructor_5b7fe0, 0x5b7fe0},
    {Original_IsvSListDestructor_5b8030, &IsvSListDestructor_5b8030, 0x5b8030},
    {Original_IsvSListDestructor_5dfe30, &IsvSListDestructor_5dfe30, 0x5dfe30},
};

std::uint32_t g_stubResult = 0;
std::vector<std::uint32_t> g_vtablesAtCalls;

struct Run {
    std::uint32_t eax = 0;
    std::vector<test::StubCall> calls;
    std::vector<std::uint32_t> vtables;
};

Run RunDestructor(SListDestructorFn fn, SListObject& object) {
    Run run;
    g_vtablesAtCalls.clear();
    test::CalleeStubs stubs;
    const auto effect = [](game::CGameData* world, std::uint32_t self, const std::uint32_t*) {
        // ECX comes relative to the world (here: the object).
        g_vtablesAtCalls.push_back(*reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uintptr_t>(world) + self));
        return g_stubResult;
    };
    stubs.Install<0, kClearAndDestroy>(effect);
    stubs.Install<0, kIsvListBaseDestroy>(effect);
    stubs.SetWorld(reinterpret_cast<game::CGameData*>(&object));
    run.eax = fn(&object, nullptr);
    run.calls = stubs.Take();
    run.vtables = g_vtablesAtCalls;
    return run;
}

bool CompareSList(test::Context& ctx, const SListPair& pair, int cases) {
    for (int i = 0; i < cases; ++i) {
        SListObject a, b;
        ctx.rng().Fill(&a, sizeof(a));
        if (ctx.rng().Chance(1, 3)) {
            a.last = 0;
        }
        std::memcpy(&b, &a, sizeof(a));
        g_stubResult = ctx.rng().Next32();
        const Run ra = RunDestructor(pair.original, a);
        const Run rb = RunDestructor(pair.ours, b);
        if (!S3RE_EXPECT_EQ(ra.eax, rb.eax) || !S3RE_EXPECT(ra.calls == rb.calls) ||
            !S3RE_EXPECT(ra.vtables == rb.vtables) || !S3RE_EXPECT_MEM_EQ(&a, &b, sizeof(a))) {
            ctx.Fail(__FILE__, __LINE__, "  0x%x case %d: last 0x%x", pair.address, i, a.last);
            return false;
        }
    }
    return true;
}

// Wrong: clearAndDestroy also for an empty list.
std::uint32_t __fastcall WrongSListDestructor(SListObject* self, void*) {
    self->vtable = vt::kWCValListBase_bool;
    reinterpret_cast<ThisFn>(kClearAndDestroy)(self, nullptr);
    return DestroyIsvSList(self, vt::kWCIsvSListBase_bool);
}
// Wrong: the other list's base vtable.
std::uint32_t __fastcall WrongIsvDestructor(SListObject* self, void*) {
    return DestroyIsvSList(self, vt::kWCIsvSListBase_CSurfaceP);
}

}  // namespace

S3RE_TEST(WatcomSListDestructors_MatchOriginal) {
    for (const SListPair& pair : kSListPairs) {
        if (!CompareSList(ctx, pair, 200)) {
            return;
        }
    }
}

S3RE_TEST(WatcomSListDestructors_ComparisonDetectsWrongImplementation) {
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareSList(probe, {Original_SListDestructor_427f10, &WrongSListDestructor, 0x427f10}, 50);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareSList(probe, {Original_IsvSListDestructor_5b8030, &WrongIsvDestructor, 0x5b8030}, 20);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::watcom
