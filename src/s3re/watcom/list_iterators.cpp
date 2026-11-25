// Constructors of Watcom intrusive list bases and skip-list iterators
// (template instantiations; only the vtable differs). thiscall, EAX = this.
//
// Intrusive list bases, (): +4 = +8 = +0xc = 0, vtable.
//   0x0041b080 WCIsvListBase                                    0x655fa4
//   0x00428bd0 WCIsvSListBase<WCNIsvSLink<CScreenElement*>>     0x655bc4
//   0x00428bf0 WCIsvSListBase<WCNIsvSLink<bool>>                0x655be0
// Skip-list iterator bases, (dictionary) ret 4: +4 = 0, +8 = the
// dictionary, +0xc = +0x10 = 0, vtable.
//   0x004726e0 WCSkipListIterBase<WCSkipListDictKeyVal<unsigned long, WCPtrSortedVector<CExecuteType>>> 0x65778c
//   0x00472780 WCSkipListIterBase<WCSkipListDictKeyVal<unsigned long, CNetSession::CSyncInfo>>         0x657794
//   0x00499600 WCSkipListIterBase<WCSkipListDictKeyVal<int, String>>                                   0x65854c

#include "game/vtables.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>

namespace s3re::watcom {

namespace vt = game::vtables;

#pragma pack(push, 1)
struct IsvListBase {
    std::uint32_t vtable;
    std::uint32_t unk04;
    std::uint32_t last;   // +0x08 the last node (0: empty)
    std::uint32_t count;  // +0x0c
};
struct SkipListIterBase {
    std::uint32_t vtable;
    std::uint32_t unk04;
    std::uint32_t dictionary;  // +0x08 the container iterated over
    std::uint32_t current;     // +0x0c (probably the current node)
    std::uint32_t unk10;
};
#pragma pack(pop)
static_assert(sizeof(IsvListBase) == 0x10);
static_assert(sizeof(SkipListIterBase) == 0x14);

namespace {

IsvListBase* ConstructIsvList(IsvListBase* self, std::uint32_t vtable) {
    self->unk04 = 0;
    self->last = 0;
    self->count = 0;
    self->vtable = vtable;
    return self;
}

SkipListIterBase* ConstructSkipListIter(SkipListIterBase* self, std::uint32_t dictionary, std::uint32_t vtable) {
    self->unk04 = 0;
    self->dictionary = dictionary;
    self->current = 0;
    self->unk10 = 0;
    self->vtable = vtable;
    return self;
}

}  // namespace

using IsvListConstructFn = IsvListBase*(__fastcall*)(IsvListBase* self, void* edx);
using SkipListIterConstructFn = SkipListIterBase*(__fastcall*)(SkipListIterBase* self, void* edx,
                                                               std::uint32_t dictionary);

// Purpose: Constructors of empty intrusive lists (the screen groups' child
// lists, the network thread's waitables).
IsvListBase* __fastcall IsvListConstructor_41b080(IsvListBase* self, void*) {
    return ConstructIsvList(self, vt::kWCIsvListBase);
}
IsvListBase* __fastcall IsvListConstructor_428bd0(IsvListBase* self, void*) {
    return ConstructIsvList(self, vt::kWCIsvSListBase_CScreenElementP);
}
IsvListBase* __fastcall IsvListConstructor_428bf0(IsvListBase* self, void*) {
    return ConstructIsvList(self, vt::kWCIsvSListBase_bool);
}

// Purpose: Constructors of iterators over the skip-list dictionaries (the
// network's request tables, the string dictionaries of the menus).
SkipListIterBase* __fastcall SkipListIterConstructor_4726e0(SkipListIterBase* self, void*, std::uint32_t dictionary) {
    return ConstructSkipListIter(self, dictionary, vt::kWCSkipListIterBase_CExecuteTypes);
}
SkipListIterBase* __fastcall SkipListIterConstructor_472780(SkipListIterBase* self, void*, std::uint32_t dictionary) {
    return ConstructSkipListIter(self, dictionary, vt::kWCSkipListIterBase_CSyncInfo);
}
SkipListIterBase* __fastcall SkipListIterConstructor_499600(SkipListIterBase* self, void*, std::uint32_t dictionary) {
    return ConstructSkipListIter(self, dictionary, vt::kWCSkipListIterBase_IntString);
}

S3RE_HOOK(0x0041b080, IsvListConstructor_41b080, IsvListConstructFn, Original_IsvListConstructor_41b080,
          IsvListConstructor_41b080);
S3RE_HOOK(0x00428bd0, IsvListConstructor_428bd0, IsvListConstructFn, Original_IsvListConstructor_428bd0,
          IsvListConstructor_428bd0);
S3RE_HOOK(0x00428bf0, IsvListConstructor_428bf0, IsvListConstructFn, Original_IsvListConstructor_428bf0,
          IsvListConstructor_428bf0);
S3RE_HOOK(0x004726e0, SkipListIterConstructor_4726e0, SkipListIterConstructFn,
          Original_SkipListIterConstructor_4726e0, SkipListIterConstructor_4726e0);
S3RE_HOOK(0x00472780, SkipListIterConstructor_472780, SkipListIterConstructFn,
          Original_SkipListIterConstructor_472780, SkipListIterConstructor_472780);
S3RE_HOOK(0x00499600, SkipListIterConstructor_499600, SkipListIterConstructFn,
          Original_SkipListIterConstructor_499600, SkipListIterConstructor_499600);

// ---------------------------------------------------------------------------
// Tests: random objects and arguments, every instantiation; the bytes and
// EAX.
// ---------------------------------------------------------------------------

namespace {

struct IsvPair {
    IsvListConstructFn original;
    IsvListConstructFn ours;
};
struct IterPair {
    SkipListIterConstructFn original;
    SkipListIterConstructFn ours;
};

const IsvPair kIsvPairs[] = {
    {Original_IsvListConstructor_41b080, &IsvListConstructor_41b080},
    {Original_IsvListConstructor_428bd0, &IsvListConstructor_428bd0},
    {Original_IsvListConstructor_428bf0, &IsvListConstructor_428bf0},
};
const IterPair kIterPairs[] = {
    {Original_SkipListIterConstructor_4726e0, &SkipListIterConstructor_4726e0},
    {Original_SkipListIterConstructor_472780, &SkipListIterConstructor_472780},
    {Original_SkipListIterConstructor_499600, &SkipListIterConstructor_499600},
};

bool CompareIsv(test::Context& ctx, const IsvPair& pair, int cases) {
    for (int i = 0; i < cases; ++i) {
        IsvListBase a, b;
        ctx.rng().Fill(&a, sizeof(a));
        std::memcpy(&b, &a, sizeof(a));
        IsvListBase* ra = pair.original(&a, nullptr);
        IsvListBase* rb = pair.ours(&b, nullptr);
        if (!S3RE_EXPECT(ra == &a && rb == &b) || !S3RE_EXPECT_MEM_EQ(&a, &b, sizeof(a))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return false;
        }
    }
    return true;
}

bool CompareIter(test::Context& ctx, const IterPair& pair, int cases) {
    for (int i = 0; i < cases; ++i) {
        SkipListIterBase a, b;
        ctx.rng().Fill(&a, sizeof(a));
        std::memcpy(&b, &a, sizeof(a));
        const std::uint32_t dictionary = ctx.rng().Next32();
        SkipListIterBase* ra = pair.original(&a, nullptr, dictionary);
        SkipListIterBase* rb = pair.ours(&b, nullptr, dictionary);
        if (!S3RE_EXPECT(ra == &a && rb == &b) || !S3RE_EXPECT_MEM_EQ(&a, &b, sizeof(a))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return false;
        }
    }
    return true;
}

// Wrong: the other list's vtable / the dictionary not stored.
IsvListBase* __fastcall WrongIsv(IsvListBase* self, void* edx) {
    return IsvListConstructor_428bf0(self, edx);
}
SkipListIterBase* __fastcall WrongIter(SkipListIterBase* self, void*, std::uint32_t dictionary) {
    return ConstructSkipListIter(self, dictionary ^ 1, vt::kWCSkipListIterBase_IntString);
}

}  // namespace

S3RE_TEST(WatcomListIteratorConstructors_MatchOriginal) {
    for (const IsvPair& pair : kIsvPairs) {
        if (!CompareIsv(ctx, pair, 200)) {
            return;
        }
    }
    for (const IterPair& pair : kIterPairs) {
        if (!CompareIter(ctx, pair, 200)) {
            return;
        }
    }
}

S3RE_TEST(WatcomListIteratorConstructors_ComparisonDetectsWrongImplementation) {
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareIsv(probe, {Original_IsvListConstructor_428bd0, &WrongIsv}, 20);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareIter(probe, {Original_SkipListIterConstructor_499600, &WrongIter}, 20);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::watcom
