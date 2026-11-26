// Constructors of Watcom intrusive list bases and skip-list iterators
// (template instantiations; only the vtable differs). thiscall, EAX = this.
//
// Intrusive list bases, (): +4 = +8 = +0xc = 0, vtable.
//   0x0041b080 WCIsvListBase                                    0x655fa4
//   0x00428bd0 WCIsvSListBase<WCNIsvSLink<CScreenElement*>>     0x655bc4
//   0x00428bf0 WCIsvSListBase<WCNIsvSLink<bool>>                0x655be0
//   0x005de110 WCIsvSListBase<WCNIsvSLink<LobbyClient::CStatusMessage>> 0x65d2c4
// Skip-list iterator bases, (dictionary) ret 4: +4 = 0, +8 = the
// dictionary, +0xc = +0x10 = 0, vtable.
//   0x004726e0 WCSkipListIterBase<WCSkipListDictKeyVal<unsigned long, WCPtrSortedVector<CExecuteType>>> 0x65778c
//   0x00472780 WCSkipListIterBase<WCSkipListDictKeyVal<unsigned long, CNetSession::CSyncInfo>>         0x657794
//   0x00499600 WCSkipListIterBase<WCSkipListDictKeyVal<int, String>>                                   0x65854c
//   0x005c34f0 WCSkipListIterBase<WCSkipListDictKeyVal<int, DirectX::CSoundEffectBuffer>>             0x65cf94
//   0x005ffc60 WCSkipListIterBase<WCSkipListDictKeyVal<unsigned int, DirectX::CSoundEffect*>>          0x65d478
//   0x00609fd0 WCSkipListIterBase<WCSkipListDictKeyVal<unsigned int, Lobby::CLMMessageBlock>>          0x65d61c
// List iterators, (list) ret 4: the same, but +0x10 = 1 (the state "before
// the first"; WCListIterBase 0x5b72b0 in directx_drawer.cpp is the third copy).
//   0x00463be0 WCPtrSListIter<Win32::CWin32Waitable>                                                  0x657464
//   0x005b7d10 WCValListIterBase<DirectX::CGameThread*, WCPtrSList<DirectX::CGameThread>, ..>       0x65cb60
// Destructors that only store their base's vtable (EAX is left as the
// caller had it; the library declares them void):
//   0x00499620 WCIterExcept::~WCIterExcept   WCIterExcept 0x656a38
//   0x00423a10 WCSkipNonTempBase::~          WCExcept 0x655438
// WCSkipListIterBase::operator++ (0x00499df0; shared by the instantiations,
// 10 callers): () -> bool. Without a dictionary, or past the end (+0x10):
// past the end = 1, the library's empty error hook 0x59f200, false. A fresh
// iterator (current 0) starts at the dictionary's header node (+0x70);
// current = current->next (+0); none: past the end = 1, false; else true.

#include "game/vtables.h"
#include "hooks.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <vector>

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
    std::uint32_t current;     // +0x0c the current node (0: before the first)
    std::uint32_t at_end;      // +0x10 set once the iteration ran past the end
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
    self->at_end = 0;
    self->vtable = vtable;
    return self;
}

// The list iterators share the layout; their state (+0x10) starts at 1.
constexpr std::uint32_t kListIterBeforeFirst = 1;

SkipListIterBase* ConstructListIter(SkipListIterBase* self, std::uint32_t list, std::uint32_t vtable) {
    ConstructSkipListIter(self, list, vtable);
    self->at_end = kListIterBeforeFirst;
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
IsvListBase* __fastcall IsvListConstructor_5de110(IsvListBase* self, void*) {
    return ConstructIsvList(self, vt::kWCIsvSListBase_CStatusMessage);
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
SkipListIterBase* __fastcall SkipListIterConstructor_5c34f0(SkipListIterBase* self, void*, std::uint32_t dictionary) {
    return ConstructSkipListIter(self, dictionary, vt::kEffectBufferIterBase);
}
SkipListIterBase* __fastcall SkipListIterConstructor_5ffc60(SkipListIterBase* self, void*, std::uint32_t dictionary) {
    return ConstructSkipListIter(self, dictionary, vt::kSoundEffectIterBase);
}
SkipListIterBase* __fastcall SkipListIterConstructor_609fd0(SkipListIterBase* self, void*, std::uint32_t dictionary) {
    return ConstructSkipListIter(self, dictionary, vt::kWCSkipListIterBase_UIntMessageBlock);
}

// Purpose: Constructors of iterators over the network thread's waitables
// and the game threads' list.
SkipListIterBase* __fastcall ListIterConstructor_463be0(SkipListIterBase* self, void*, std::uint32_t list) {
    return ConstructListIter(self, list, vt::kWCPtrSListIter_CWin32Waitable);
}
SkipListIterBase* __fastcall ListIterConstructor_5b7d10(SkipListIterBase* self, void*, std::uint32_t list) {
    return ConstructListIter(self, list, vt::kWCValListIterBase_CGameThreadP);
}

using BaseDestructorFn = void(__fastcall*)(std::uint32_t* self, void* edx);

// Purpose: Destructors of the iterators' and the skip lists' library base
// classes (they only restore the base's vtable).
void __fastcall WCIterExcept_Destroy(std::uint32_t* self, void*) {
    *self = vt::kWCIterExcept;
}
void __fastcall WCSkipNonTempBase_Destroy(std::uint32_t* self, void*) {
    *self = vt::kWCExcept;
}

constexpr std::uint32_t kDictionaryHeader = 0x70;  // the skip list's header node in the dictionary

using SkipListIterIncrementFn = std::uint32_t(__fastcall*)(SkipListIterBase* self, void* edx);

// Purpose: Advances an iterator over a skip-list dictionary (the menus'
// string tables, the network's request tables); false at the end.
std::uint32_t __fastcall SkipListIter_Increment(SkipListIterBase* self, void*) {
    if (self->dictionary == 0 || self->at_end != 0) {
        self->at_end = 1;  // (then the library's empty error hook 0x59f200)
        return 0;
    }
    if (self->current == 0) {
        self->current = *reinterpret_cast<const std::uint32_t*>(self->dictionary + kDictionaryHeader);
    }
    self->current = *reinterpret_cast<const std::uint32_t*>(self->current);
    if (self->current == 0) {
        self->at_end = 1;
        return 0;
    }
    return 1;
}

S3RE_HOOK(0x00499df0, SkipListIter_Increment, SkipListIterIncrementFn, Original_SkipListIter_Increment,
          SkipListIter_Increment);
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
S3RE_HOOK(0x005c34f0, SkipListIterConstructor_5c34f0, SkipListIterConstructFn,
          Original_SkipListIterConstructor_5c34f0, SkipListIterConstructor_5c34f0);
S3RE_HOOK(0x005ffc60, SkipListIterConstructor_5ffc60, SkipListIterConstructFn,
          Original_SkipListIterConstructor_5ffc60, SkipListIterConstructor_5ffc60);
S3RE_HOOK(0x005de110, IsvListConstructor_5de110, IsvListConstructFn, Original_IsvListConstructor_5de110,
          IsvListConstructor_5de110);
S3RE_HOOK(0x00609fd0, SkipListIterConstructor_609fd0, SkipListIterConstructFn,
          Original_SkipListIterConstructor_609fd0, SkipListIterConstructor_609fd0);
S3RE_HOOK(0x00463be0, ListIterConstructor_463be0, SkipListIterConstructFn, Original_ListIterConstructor_463be0,
          ListIterConstructor_463be0);
S3RE_HOOK(0x005b7d10, ListIterConstructor_5b7d10, SkipListIterConstructFn, Original_ListIterConstructor_5b7d10,
          ListIterConstructor_5b7d10);
S3RE_HOOK(0x00499620, WCIterExcept_Destroy, BaseDestructorFn, Original_WCIterExcept_Destroy, WCIterExcept_Destroy);
S3RE_HOOK(0x00423a10, WCSkipNonTempBase_Destroy, BaseDestructorFn, Original_WCSkipNonTempBase_Destroy,
          WCSkipNonTempBase_Destroy);

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
    {Original_IsvListConstructor_5de110, &IsvListConstructor_5de110},
};
const IterPair kIterPairs[] = {
    {Original_SkipListIterConstructor_4726e0, &SkipListIterConstructor_4726e0},
    {Original_SkipListIterConstructor_472780, &SkipListIterConstructor_472780},
    {Original_SkipListIterConstructor_499600, &SkipListIterConstructor_499600},
    {Original_SkipListIterConstructor_5c34f0, &SkipListIterConstructor_5c34f0},
    {Original_SkipListIterConstructor_5ffc60, &SkipListIterConstructor_5ffc60},
    {Original_SkipListIterConstructor_609fd0, &SkipListIterConstructor_609fd0},
    {Original_ListIterConstructor_463be0, &ListIterConstructor_463be0},
    {Original_ListIterConstructor_5b7d10, &ListIterConstructor_5b7d10},
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

// An iterator over a chain of up to 4 nodes in a buffer (the dictionary at
// +0, its header pointer at +0x70, nodes at +0x80 + 0x10 * k), stepped 1..6
// times; compared: the iterator, the buffer and every EAX.
bool CompareIncrement(test::Context& ctx, SkipListIterIncrementFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        std::uint8_t initial[0xc0];
        ctx.rng().Fill(initial, sizeof(initial));
        const std::uint32_t nodes = ctx.rng().Below(5);
        SkipListIterBase start;
        ctx.rng().Fill(&start, sizeof(start));
        const int dictionaryKind = static_cast<int>(ctx.rng().Below(8));
        const int currentKind = static_cast<int>(ctx.rng().Below(4));
        const bool atEnd = ctx.rng().Chance(1, 5);
        const std::uint32_t steps = 1 + ctx.rng().Below(6);
        auto run = [&](SkipListIterIncrementFn fn, std::uint8_t* buffer, std::vector<std::uint32_t>& results) {
            std::memcpy(buffer, initial, sizeof(initial));
            const auto base = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(buffer));
            // The header's next chain: header -> node 0 -> ... -> node n-1 -> 0.
            const std::uint32_t header = base + 0xb0;
            std::memcpy(buffer + 0x70, &header, 4);
            for (std::uint32_t k = 0; k <= nodes; ++k) {
                const std::uint32_t at = k == 0 ? 0xb0 : 0x80 + 0x10 * (k - 1);
                const std::uint32_t next = k < nodes ? base + 0x80 + 0x10 * k : 0;
                std::memcpy(buffer + at, &next, 4);
            }
            SkipListIterBase it = start;
            it.dictionary = dictionaryKind == 0 ? 0 : base;
            it.current = currentKind == 0 || nodes == 0 ? 0 : base + 0x80 + 0x10 * (currentKind % nodes);
            it.at_end = atEnd ? 1 : 0;
            for (std::uint32_t k = 0; k < steps; ++k) {
                results.push_back(fn(&it, nullptr));
                results.push_back(it.current == 0 ? 0 : it.current - base + 1);
                results.push_back(it.at_end);
            }
        };
        std::uint8_t a[0xc0], b[0xc0];
        std::vector<std::uint32_t> ra, rb;
        run(Original_SkipListIter_Increment, a, ra);
        run(candidate, b, rb);
        if (!S3RE_EXPECT(ra == rb)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: nodes %u", i, nodes);
            return false;
        }
    }
    return true;
}

// Wrong: a fresh iterator starts at the first node instead of the header.
std::uint32_t __fastcall WrongIncrement(SkipListIterBase* self, void* edx) {
    if (self->current == 0 && self->dictionary != 0 && self->at_end == 0) {
        self->current = *reinterpret_cast<const std::uint32_t*>(self->dictionary + kDictionaryHeader);
        self->current = *reinterpret_cast<const std::uint32_t*>(self->current);
    }
    return SkipListIter_Increment(self, edx);
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

// The base destructors on random words.
bool CompareBaseDestructor(test::Context& ctx, BaseDestructorFn original, BaseDestructorFn ours, int cases) {
    for (int i = 0; i < cases; ++i) {
        std::uint32_t a[2], b[2];
        ctx.rng().Fill(a, sizeof(a));
        std::memcpy(b, a, sizeof(a));
        original(a, nullptr);
        ours(b, nullptr);
        if (!S3RE_EXPECT_MEM_EQ(a, b, sizeof(a))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return false;
        }
    }
    return true;
}

S3RE_TEST(WatcomBaseDestructors_MatchOriginal) {
    CompareBaseDestructor(ctx, Original_WCIterExcept_Destroy, &WCIterExcept_Destroy, 50) &&
        CompareBaseDestructor(ctx, Original_WCSkipNonTempBase_Destroy, &WCSkipNonTempBase_Destroy, 50);
}

S3RE_TEST(WatcomBaseDestructors_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareBaseDestructor(probe, Original_WCIterExcept_Destroy, &WCSkipNonTempBase_Destroy, 5);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(SkipListIter_Increment_MatchesOriginal) {
    CompareIncrement(ctx, &SkipListIter_Increment, 3000);
}

S3RE_TEST(SkipListIter_Increment_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareIncrement(probe, &WrongIncrement, 200);
    S3RE_EXPECT(probe.failures() > 0);
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
