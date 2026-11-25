// Compiler-generated deleting destructors (MSVC 6.0) of several classes:
// the "scalar deleting destructor" that a vtable's slot 0 (or a `delete p`)
// calls with flags, and the complete-object destructors of classes with a
// virtual base. Each is a few instructions around the class's own
// destructor; ours call those through their S3.EXE addresses.
//
// Scalar deleting destructors (flags) ret 4, EAX = the object:
//   destructor(this); if (flags & 1) operator delete(this).
//     0x0044e300 String (called directly by menu constructors)  ~string 0x401b60
//     0x0044e320 a lobby class with a string at +8             0x441740
//     0x004639c0 a menu class with a string at +0x50           0x45f060
//     0x0046db20 a network class                               Net::DeletePointee 0x49d980
//     0x0041d990 CStaticControl                                0x41d9b0 (jmp ~CScreenElement)
//     0x004993d0 Watcom iterators (copy of 0x4281d0's slot)    ~WCIterExcept 0x499620
//     0x0049cb50 Lobby::CLM* messages (shared by 24 vtables)   0x434680 (jmp ~CLMGeneric)
//     0x004a5b20 Dirk::CMapFile                                ~CMapFile 0x4a5b50
// Scalar deleting destructors of classes with the virtual base
// CScreenElement (reached through a thunk with `this` at the virtual base,
// at +offset of the object): destructor(this), ~CScreenElement 0x4156b0
// (this), then operator delete(this - offset) if (flags & 1); EAX = this -
// offset (the object).
//     0x0042a710 CScreen                       +0x98   ~CScreen 0x42a750
//     0x0042af60 CListBox                      +0x444  ~CListBox 0x421fc0
//     0x0042b340 CDropdown                     +0x66c  ~CDropdown 0x42b380
//     0x0049b610 Menus::priv::CLoadGameDialog  +0x914  0x49b650
// Scalar deleting destructors with the destructor inlined (it only stores
// the vtable): vtable = V; if (flags & 1) operator delete(this); EAX = this.
//     0x00405360 WCExcept      0x655438
//     0x004281d0 WCIterExcept  0x656a38
// Complete-object destructors of a member with a virtual base (no flags):
// p = this + offset; destructor(p); ~CScreenElement(p); EAX = what
// ~CScreenElement leaves in it (callers don't use it).
//     0x00421fa0 CListBox   +0x444  ~CListBox 0x421fc0
//     0x004816d0 CDropdown  +0x66c  ~CDropdown 0x42b380

#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::msvc6 {

namespace {

using ThiscallFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);
constexpr std::uintptr_t kScreenElementDestructor = 0x004156b0;  // CScreenElement::~CScreenElement (ours)

std::uint32_t CallDestructor(std::uintptr_t destructor, std::uintptr_t self) {
    return reinterpret_cast<ThiscallFn>(destructor)(self, nullptr);
}

std::uintptr_t DeletingDestructor(std::uintptr_t destructor, std::uintptr_t self, std::uint32_t flags) {
    CallDestructor(destructor, self);
    if ((flags & 1) != 0) {
        GameOperatorDelete(reinterpret_cast<void*>(self));
    }
    return self;
}

std::uintptr_t VirtualBaseDeletingDestructor(std::uintptr_t destructor, std::uint32_t offset, std::uintptr_t self,
                                             std::uint32_t flags) {
    const std::uintptr_t object = self - offset;
    CallDestructor(destructor, self);
    CallDestructor(kScreenElementDestructor, self);
    if ((flags & 1) != 0) {
        GameOperatorDelete(reinterpret_cast<void*>(object));
    }
    return object;
}

std::uintptr_t InlineDeletingDestructor(std::uint32_t vtable, std::uintptr_t self, std::uint32_t flags) {
    *reinterpret_cast<std::uint32_t*>(self) = vtable;
    if ((flags & 1) != 0) {
        GameOperatorDelete(reinterpret_cast<void*>(self));
    }
    return self;
}

std::uint32_t VirtualBaseDestructor(std::uintptr_t destructor, std::uint32_t offset, std::uintptr_t self) {
    const std::uintptr_t member = self + offset;
    CallDestructor(destructor, member);
    return CallDestructor(kScreenElementDestructor, member);
}

}  // namespace

using DeletingDestructorFn = std::uintptr_t(__fastcall*)(std::uintptr_t self, void* edx, std::uint32_t flags);
using CompleteDestructorFn = std::uint32_t(__fastcall*)(std::uintptr_t self, void* edx);

// Purpose: Destroys an object and frees it when asked (`delete p`, and
// slot 0 of the class's vtable); one per class.
std::uintptr_t __fastcall ScalarDeletingDestructor_44e300(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00401b60, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_44e320(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00441740, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_4639c0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x0045f060, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_46db20(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x0049d980, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_41d990(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x0041d9b0, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_4993d0(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00499620, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_49cb50(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x00434680, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_4a5b20(std::uintptr_t self, void*, std::uint32_t flags) {
    return DeletingDestructor(0x004a5b50, self, flags);
}

// Purpose: The same for the screen classes with the virtual base
// CScreenElement (called with `this` at the virtual base).
std::uintptr_t __fastcall ScalarDeletingDestructor_42a710(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x0042a750, 0x98, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_42af60(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x00421fc0, 0x444, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_42b340(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x0042b380, 0x66c, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_49b610(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x0049b650, 0x914, self, flags);
}

// Purpose: The same for the Watcom container library's exception base
// classes, whose destructors only store the vtable.
std::uintptr_t __fastcall ScalarDeletingDestructor_405360(std::uintptr_t self, void*, std::uint32_t flags) {
    return InlineDeletingDestructor(game::vtables::kWCExcept, self, flags);
}
std::uintptr_t __fastcall ScalarDeletingDestructor_4281d0(std::uintptr_t self, void*, std::uint32_t flags) {
    return InlineDeletingDestructor(game::vtables::kWCIterExcept, self, flags);
}

// Purpose: Destroys a list box or dropdown that is a member of a dialog
// (its own destructor, then its virtual base CScreenElement).
std::uint32_t __fastcall CompleteDestructor_421fa0(std::uintptr_t self, void*) {
    return VirtualBaseDestructor(0x00421fc0, 0x444, self);
}
std::uint32_t __fastcall CompleteDestructor_4816d0(std::uintptr_t self, void*) {
    return VirtualBaseDestructor(0x0042b380, 0x66c, self);
}

S3RE_HOOK(0x0044e300, ScalarDeletingDestructor_44e300, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_44e300, ScalarDeletingDestructor_44e300);
S3RE_HOOK(0x0044e320, ScalarDeletingDestructor_44e320, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_44e320, ScalarDeletingDestructor_44e320);
S3RE_HOOK(0x004639c0, ScalarDeletingDestructor_4639c0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_4639c0, ScalarDeletingDestructor_4639c0);
S3RE_HOOK(0x0046db20, ScalarDeletingDestructor_46db20, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_46db20, ScalarDeletingDestructor_46db20);
S3RE_HOOK(0x0041d990, ScalarDeletingDestructor_41d990, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_41d990, ScalarDeletingDestructor_41d990);
S3RE_HOOK(0x004993d0, ScalarDeletingDestructor_4993d0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_4993d0, ScalarDeletingDestructor_4993d0);
S3RE_HOOK(0x0049cb50, ScalarDeletingDestructor_49cb50, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_49cb50, ScalarDeletingDestructor_49cb50);
S3RE_HOOK(0x004a5b20, ScalarDeletingDestructor_4a5b20, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_4a5b20, ScalarDeletingDestructor_4a5b20);
S3RE_HOOK(0x0042a710, ScalarDeletingDestructor_42a710, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_42a710, ScalarDeletingDestructor_42a710);
S3RE_HOOK(0x0042af60, ScalarDeletingDestructor_42af60, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_42af60, ScalarDeletingDestructor_42af60);
S3RE_HOOK(0x0042b340, ScalarDeletingDestructor_42b340, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_42b340, ScalarDeletingDestructor_42b340);
S3RE_HOOK(0x0049b610, ScalarDeletingDestructor_49b610, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_49b610, ScalarDeletingDestructor_49b610);
S3RE_HOOK(0x00405360, ScalarDeletingDestructor_405360, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_405360, ScalarDeletingDestructor_405360);
S3RE_HOOK(0x004281d0, ScalarDeletingDestructor_4281d0, DeletingDestructorFn,
          Original_ScalarDeletingDestructor_4281d0, ScalarDeletingDestructor_4281d0);
S3RE_HOOK(0x00421fa0, CompleteDestructor_421fa0, CompleteDestructorFn, Original_CompleteDestructor_421fa0,
          CompleteDestructor_421fa0);
S3RE_HOOK(0x004816d0, CompleteDestructor_4816d0, CompleteDestructorFn, Original_CompleteDestructor_4816d0,
          CompleteDestructor_4816d0);

// ---------------------------------------------------------------------------
// Tests: each destructor with its callees (the class destructors and
// ~CScreenElement) replaced by recording stubs, on objects from the game's
// heap with random bytes, flags 0..3. Compared: the stub calls (`this`
// relative to the object), EAX relative to the object (the virtual-base
// destructors: as returned by the stub), and the object's first bytes when
// it is not freed.
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kObjectBytes = 0x980;  // more than the largest virtual-base offset

struct DeletingCase {
    DeletingDestructorFn original;
    DeletingDestructorFn ours;
    std::uintptr_t callee;  // 0: the inline kind
    std::uint32_t offset;   // `this` inside the object
};

const DeletingCase kDeletingCases[] = {
    {Original_ScalarDeletingDestructor_44e300, &ScalarDeletingDestructor_44e300, 0x00401b60, 0},
    {Original_ScalarDeletingDestructor_44e320, &ScalarDeletingDestructor_44e320, 0x00441740, 0},
    {Original_ScalarDeletingDestructor_4639c0, &ScalarDeletingDestructor_4639c0, 0x0045f060, 0},
    {Original_ScalarDeletingDestructor_46db20, &ScalarDeletingDestructor_46db20, 0x0049d980, 0},
    {Original_ScalarDeletingDestructor_41d990, &ScalarDeletingDestructor_41d990, 0x0041d9b0, 0},
    {Original_ScalarDeletingDestructor_4993d0, &ScalarDeletingDestructor_4993d0, 0x00499620, 0},
    {Original_ScalarDeletingDestructor_49cb50, &ScalarDeletingDestructor_49cb50, 0x00434680, 0},
    {Original_ScalarDeletingDestructor_4a5b20, &ScalarDeletingDestructor_4a5b20, 0x004a5b50, 0},
    {Original_ScalarDeletingDestructor_42a710, &ScalarDeletingDestructor_42a710, 0x0042a750, 0x98},
    {Original_ScalarDeletingDestructor_42af60, &ScalarDeletingDestructor_42af60, 0x00421fc0, 0x444},
    {Original_ScalarDeletingDestructor_42b340, &ScalarDeletingDestructor_42b340, 0x0042b380, 0x66c},
    {Original_ScalarDeletingDestructor_49b610, &ScalarDeletingDestructor_49b610, 0x0049b650, 0x914},
    {Original_ScalarDeletingDestructor_405360, &ScalarDeletingDestructor_405360, 0, 0},
    {Original_ScalarDeletingDestructor_4281d0, &ScalarDeletingDestructor_4281d0, 0, 0},
};

struct CompleteCase {
    CompleteDestructorFn original;
    CompleteDestructorFn ours;
    std::uintptr_t callee;
    std::uint32_t offset;  // the member inside the object
};

const CompleteCase kCompleteCases[] = {
    {Original_CompleteDestructor_421fa0, &CompleteDestructor_421fa0, 0x00421fc0, 0x444},
    {Original_CompleteDestructor_4816d0, &CompleteDestructor_4816d0, 0x0042b380, 0x66c},
};

// Installs the stubs a case needs (the class destructor, ~CScreenElement).
// The stub of ~CScreenElement returns a value fixed by the call, so the
// complete-object destructors' EAX is comparable.
template <std::uintptr_t Callee>
void InstallIf(test::CalleeStubs& stubs, std::uintptr_t callee) {
    if (callee == Callee) {
        stubs.Install<0, Callee>();
    }
}

void InstallStubs(test::CalleeStubs& stubs, std::uintptr_t callee) {
    InstallIf<0x00401b60>(stubs, callee);
    InstallIf<0x00441740>(stubs, callee);
    InstallIf<0x0045f060>(stubs, callee);
    InstallIf<0x0049d980>(stubs, callee);
    InstallIf<0x0041d9b0>(stubs, callee);
    InstallIf<0x00499620>(stubs, callee);
    InstallIf<0x00434680>(stubs, callee);
    InstallIf<0x004a5b50>(stubs, callee);
    InstallIf<0x0042a750>(stubs, callee);
    InstallIf<0x00421fc0>(stubs, callee);
    InstallIf<0x0042b380>(stubs, callee);
    InstallIf<0x0049b650>(stubs, callee);
    stubs.Install<0, kScreenElementDestructor>(
        [](game::CGameData*, std::uint32_t self, const std::uint32_t*) { return self * 7 + 1; });
}

struct Run {
    std::uintptr_t eax;  // relative to the object
    std::vector<test::StubCall> calls;
    std::uint8_t bytes[16];  // the object's first bytes (when not freed)
};

template <typename Call>
Run RunCase(std::uintptr_t callee, const std::uint8_t* initial, bool freed, Call call) {
    auto* object = static_cast<std::uint8_t*>(GameOperatorNew(kObjectBytes));
    std::memcpy(object, initial, kObjectBytes);
    Run run{};
    {
        test::CalleeStubs stubs;
        InstallStubs(stubs, callee);
        stubs.SetWorld(reinterpret_cast<game::CGameData*>(object));
        run.eax = call(reinterpret_cast<std::uintptr_t>(object)) - reinterpret_cast<std::uintptr_t>(object);
        run.calls = stubs.Take();
    }
    if (!freed) {
        std::memcpy(run.bytes, object, sizeof(run.bytes));
        GameOperatorDelete(object);
    }
    return run;
}

bool CompareDeleting(test::Context& ctx, const DeletingCase& c, int cases) {
    std::vector<std::uint8_t> initial(kObjectBytes);
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(initial.data(), initial.size());
        const std::uint32_t flags = ctx.rng().Below(4);
        const bool freed = (flags & 1) != 0;
        const Run a = RunCase(c.callee, initial.data(), freed, [&](std::uintptr_t object) {
            return c.original(object + c.offset, nullptr, flags);
        });
        const Run b = RunCase(c.callee, initial.data(), freed, [&](std::uintptr_t object) {
            return c.ours(object + c.offset, nullptr, flags);
        });
        if (!S3RE_EXPECT_EQ(a.eax, b.eax) || !S3RE_EXPECT(a.calls == b.calls) ||
            !S3RE_EXPECT_MEM_EQ(a.bytes, b.bytes, sizeof(a.bytes))) {
            ctx.Fail(__FILE__, __LINE__, "  callee 0x%x case %d: flags %u", c.callee, i, flags);
            return false;
        }
    }
    return true;
}

bool CompareComplete(test::Context& ctx, const CompleteCase& c, int cases) {
    std::vector<std::uint8_t> initial(kObjectBytes);
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(initial.data(), initial.size());
        const Run a = RunCase(c.callee, initial.data(), false,
                              [&](std::uintptr_t object) { return c.original(object, nullptr); });
        const Run b = RunCase(c.callee, initial.data(), false,
                              [&](std::uintptr_t object) { return c.ours(object, nullptr); });
        if (!S3RE_EXPECT_EQ(a.eax, b.eax) || !S3RE_EXPECT(a.calls == b.calls) ||
            !S3RE_EXPECT_MEM_EQ(a.bytes, b.bytes, sizeof(a.bytes))) {
            ctx.Fail(__FILE__, __LINE__, "  callee 0x%x case %d", c.callee, i);
            return false;
        }
    }
    return true;
}

// Wrong implementations.
std::uintptr_t __fastcall WrongVirtualBaseDeleting(std::uintptr_t self, void*, std::uint32_t flags) {
    return VirtualBaseDeletingDestructor(0x0042b380, 0x66c, self, flags) + 0x66c;  // returns `this`, not the object
}
std::uintptr_t __fastcall WrongInline(std::uintptr_t self, void*, std::uint32_t flags) {
    return InlineDeletingDestructor(game::vtables::kWCIterExcept, self, flags);  // 0x405360 stores WCExcept
}
std::uint32_t __fastcall WrongComplete(std::uintptr_t self, void*) {
    return VirtualBaseDestructor(0x0042b380, 0x444, self);  // the list box's offset
}

}  // namespace

S3RE_TEST(DeletingDestructors_MatchOriginal) {
    for (const DeletingCase& c : kDeletingCases) {
        if (!CompareDeleting(ctx, c, 200)) {
            return;
        }
    }
    for (const CompleteCase& c : kCompleteCases) {
        if (!CompareComplete(ctx, c, 200)) {
            return;
        }
    }
}

S3RE_TEST(DeletingDestructors_ComparisonDetectsWrongImplementation) {
    const DeletingCase wrong[] = {
        {Original_ScalarDeletingDestructor_42b340, &WrongVirtualBaseDeleting, 0x0042b380, 0x66c},
        {Original_ScalarDeletingDestructor_405360, &WrongInline, 0, 0},
    };
    for (const DeletingCase& c : wrong) {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareDeleting(probe, c, 50);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareComplete(probe, {Original_CompleteDestructor_4816d0, &WrongComplete, 0x0042b380, 0x66c}, 50);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::msvc6
