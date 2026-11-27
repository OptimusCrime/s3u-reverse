// The Watcom skip-list base (WCSkipNonTempBase) constructor, the
// constructors of the dictionaries built on it (template instantiations;
// only the vtable differs) and two dictionaries' insert wrappers.
//
// WCSkipNonTempBase::WCSkipNonTempBase (0x0060b9f0; 17 callers):
//   (probability, levels, allocatorArg) ret 0xc: +4 = +0x5c = +0x60 = +0x6c
//   = +0x74 = 0, +0x58 = the third argument, vtable 0x65d638, then
//   base_init(probability, levels) (0x60bb80). EAX = this.
// WCSkipListBase<WCSkipListDictKeyVal<K, V>> constructors, (a, b, c) ret
//   0xc: WCSkipNonTempBase(a, b, c), the class's vtable; EAX = this.
//   0x00609f20 <unsigned int, int>                     0x6587cc
//   0x00609ed0 <unsigned int, Lobby::CLMMessageBlock>  0x65d588
//   0x00458030 <unsigned int, String>                  0x657200
// WCValSkipListDict<HWND, Win32::CCDAudio*> (0x00616020): (a, b) ret 8:
//   WCSkipNonTempBase(a, b, 0), vtable 0x65d94c; EAX = this.
// The dictionaries' insert (slot 5), (const KeyVal* value) ret 4:
//   base_insert(the node pointer for the value: value - the pair's size)
//   (0x60bbe0) != 0.
//   0x00609b90 <unsigned int, Lobby::CLMMessageBlock> (pair 0x28)
//   0x00472400 <unsigned long, CNetSession::CSyncInfo> (pair 0xc)

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
struct SkipNonTempBase {
    std::uint32_t vtable;
    std::uint32_t unk04;
    std::uint8_t unk08[0x58 - 0x08];
    std::uint32_t allocatorArg;  // +0x58 the third constructor argument
    std::uint32_t allocate;      // +0x5c the node allocator (0: operator new)
    std::uint32_t deallocate;    // +0x60 the node deallocator (0: operator delete)
    std::uint8_t unk64[0x6c - 0x64];
    std::uint32_t unk6c;
    std::uint32_t unk70;
    std::uint32_t count;  // +0x74
};
#pragma pack(pop)
static_assert(sizeof(SkipNonTempBase) == 0x78);

using SkipBaseConstructFn = SkipNonTempBase*(__fastcall*)(SkipNonTempBase* self, void* edx, std::uint32_t a,
                                                          std::uint32_t b, std::uint32_t c);
using SkipDictConstructFn = SkipNonTempBase*(__fastcall*)(SkipNonTempBase* self, void* edx, std::uint32_t a,
                                                          std::uint32_t b);
using SkipInsertFn = std::uint32_t(__fastcall*)(std::uint32_t self, void* edx, std::uint32_t value);

namespace {

constexpr std::uintptr_t kBaseInit = 0x0060bb80;    // WCSkipNonTempBase::base_init
constexpr std::uintptr_t kBaseInsert = 0x0060bbe0;  // WCSkipNonTempBase::base_insert
constexpr std::uintptr_t kBaseConstruct = 0x0060b9f0;

using BaseInitFn = std::uint32_t(__fastcall*)(SkipNonTempBase* self, void* edx, std::uint32_t probability,
                                              std::uint32_t levels);
using InsertFn = std::uint32_t(__fastcall*)(std::uint32_t self, void* edx, std::uint32_t node);

SkipNonTempBase* ConstructDerived(SkipNonTempBase* self, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                  std::uint32_t vtable) {
    reinterpret_cast<SkipBaseConstructFn>(kBaseConstruct)(self, nullptr, a, b, c);
    self->vtable = vtable;
    return self;
}

std::uint32_t InsertValue(std::uint32_t self, std::uint32_t value, std::uint32_t pairSize) {
    return reinterpret_cast<InsertFn>(kBaseInsert)(self, nullptr, value - pairSize) != 0 ? 1 : 0;
}

}  // namespace

// Purpose: Constructor of the skip-list base of every Watcom dictionary.
SkipNonTempBase* __fastcall WCSkipNonTempBase_Construct(SkipNonTempBase* self, void*, std::uint32_t probability,
                                                        std::uint32_t levels, std::uint32_t allocatorArg) {
    self->unk04 = 0;
    self->allocate = 0;
    self->deallocate = 0;
    self->unk6c = 0;
    self->count = 0;
    self->allocatorArg = allocatorArg;
    self->vtable = vt::kWCSkipNonTempBase;
    reinterpret_cast<BaseInitFn>(kBaseInit)(self, nullptr, probability, levels);
    return self;
}

// Purpose: Constructors of the map file's chunk-reference, the lobby's
// message-block and the menus' string dictionaries.
SkipNonTempBase* __fastcall SkipListConstructor_609f20(SkipNonTempBase* self, void*, std::uint32_t a,
                                                       std::uint32_t b, std::uint32_t c) {
    return ConstructDerived(self, a, b, c, vt::kReferenceSkipListBase);
}
SkipNonTempBase* __fastcall SkipListConstructor_609ed0(SkipNonTempBase* self, void*, std::uint32_t a,
                                                       std::uint32_t b, std::uint32_t c) {
    return ConstructDerived(self, a, b, c, vt::kWCSkipListBase_UIntMessageBlock);
}
SkipNonTempBase* __fastcall SkipListConstructor_458030(SkipNonTempBase* self, void*, std::uint32_t a,
                                                       std::uint32_t b, std::uint32_t c) {
    return ConstructDerived(self, a, b, c, vt::kWCSkipListBase_UIntString);
}

// Purpose: Constructor of the CD audio's dictionary of windows.
SkipNonTempBase* __fastcall SkipDictConstructor_616020(SkipNonTempBase* self, void*, std::uint32_t a, std::uint32_t b) {
    return ConstructDerived(self, a, b, 0, vt::kWCValSkipListDict_HWNDCDAudio);
}

// Purpose: Inserts a value into the lobby's message-block and the
// network's sync dictionaries.
std::uint32_t __fastcall SkipInsert_609b90(std::uint32_t self, void*, std::uint32_t value) {
    return InsertValue(self, value, 0x28);
}
std::uint32_t __fastcall SkipInsert_472400(std::uint32_t self, void*, std::uint32_t value) {
    return InsertValue(self, value, 0x0c);
}

S3RE_HOOK(0x0060b9f0, WCSkipNonTempBase_Construct, SkipBaseConstructFn, Original_WCSkipNonTempBase_Construct,
          WCSkipNonTempBase_Construct);
S3RE_HOOK(0x00609f20, SkipListConstructor_609f20, SkipBaseConstructFn, Original_SkipListConstructor_609f20,
          SkipListConstructor_609f20);
S3RE_HOOK(0x00609ed0, SkipListConstructor_609ed0, SkipBaseConstructFn, Original_SkipListConstructor_609ed0,
          SkipListConstructor_609ed0);
S3RE_HOOK(0x00458030, SkipListConstructor_458030, SkipBaseConstructFn, Original_SkipListConstructor_458030,
          SkipListConstructor_458030);
S3RE_HOOK(0x00616020, SkipDictConstructor_616020, SkipDictConstructFn, Original_SkipDictConstructor_616020,
          SkipDictConstructor_616020);
S3RE_HOOK(0x00609b90, SkipInsert_609b90, SkipInsertFn, Original_SkipInsert_609b90, SkipInsert_609b90);
S3RE_HOOK(0x00472400, SkipInsert_472400, SkipInsertFn, Original_SkipInsert_472400, SkipInsert_472400);

// ---------------------------------------------------------------------------
// Tests: random objects and arguments; base_init, the base constructor (for
// the derived ones) and base_insert replaced by recording stubs (insert
// returns a random value). Compared: the objects' bytes, EAX and the calls
// (with the vtable at each call).
// ---------------------------------------------------------------------------

namespace {

std::uint32_t g_stubResult = 0;
std::vector<std::uint32_t> g_vtables;

struct Run {
    std::uint32_t eax;
    std::uint8_t bytes[sizeof(SkipNonTempBase)];
    std::vector<test::StubCall> calls;
    std::vector<std::uint32_t> vtables;
};

template <typename Call>
Run RunCase(const std::uint8_t* initial, bool stubBase, Call call) {
    Run run{};
    std::memcpy(run.bytes, initial, sizeof(run.bytes));
    g_vtables.clear();
    {
        test::CalleeStubs stubs;
        const auto effect = [](game::CGameData* world, std::uint32_t self, const std::uint32_t*) {
            g_vtables.push_back(*reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uintptr_t>(world) + self));
            return g_stubResult;
        };
        stubs.Install<2, kBaseInit>(effect);
        stubs.Install<1, kBaseInsert>([](game::CGameData*, std::uint32_t, const std::uint32_t*) { return g_stubResult; });
        if (stubBase) {
            stubs.Install<3, kBaseConstruct>(effect);
        }
        stubs.SetWorld(reinterpret_cast<game::CGameData*>(run.bytes));
        run.eax = call(reinterpret_cast<SkipNonTempBase*>(run.bytes)) - reinterpret_cast<std::uint32_t>(run.bytes);
        run.calls = stubs.Take();
    }
    run.vtables = g_vtables;
    return run;
}

bool SameRuns(test::Context& ctx, const Run& a, const Run& b, int i) {
    if (!S3RE_EXPECT_EQ(a.eax, b.eax) || !S3RE_EXPECT_MEM_EQ(a.bytes, b.bytes, sizeof(a.bytes)) ||
        !S3RE_EXPECT(a.calls == b.calls) || !S3RE_EXPECT(a.vtables == b.vtables)) {
        ctx.Fail(__FILE__, __LINE__, "  case %d", i);
        return false;
    }
    return true;
}

bool CompareConstruct(test::Context& ctx, SkipBaseConstructFn original, SkipBaseConstructFn ours, bool stubBase,
                      int cases) {
    for (int i = 0; i < cases; ++i) {
        std::uint8_t initial[sizeof(SkipNonTempBase)];
        ctx.rng().Fill(initial, sizeof(initial));
        const std::uint32_t a = ctx.rng().Next32(), b = ctx.rng().Next32(), c = ctx.rng().Next32();
        g_stubResult = ctx.rng().Next32();
        const Run ra = RunCase(initial, stubBase, [&](SkipNonTempBase* s) {
            return reinterpret_cast<std::uint32_t>(original(s, nullptr, a, b, c));
        });
        const Run rb = RunCase(initial, stubBase, [&](SkipNonTempBase* s) {
            return reinterpret_cast<std::uint32_t>(ours(s, nullptr, a, b, c));
        });
        if (!SameRuns(ctx, ra, rb, i)) {
            return false;
        }
    }
    return true;
}

bool CompareDictConstruct(test::Context& ctx, int cases) {
    for (int i = 0; i < cases; ++i) {
        std::uint8_t initial[sizeof(SkipNonTempBase)];
        ctx.rng().Fill(initial, sizeof(initial));
        const std::uint32_t a = ctx.rng().Next32(), b = ctx.rng().Next32();
        const Run ra = RunCase(initial, true, [&](SkipNonTempBase* s) {
            return reinterpret_cast<std::uint32_t>(Original_SkipDictConstructor_616020(s, nullptr, a, b));
        });
        const Run rb = RunCase(initial, true, [&](SkipNonTempBase* s) {
            return reinterpret_cast<std::uint32_t>(SkipDictConstructor_616020(s, nullptr, a, b));
        });
        if (!SameRuns(ctx, ra, rb, i)) {
            return false;
        }
    }
    return true;
}

bool CompareInsert(test::Context& ctx, SkipInsertFn original, SkipInsertFn ours, int cases) {
    for (int i = 0; i < cases; ++i) {
        std::uint8_t initial[sizeof(SkipNonTempBase)];
        ctx.rng().Fill(initial, sizeof(initial));
        const std::uint32_t value = ctx.rng().Next32();
        g_stubResult = ctx.rng().Chance(1, 3) ? 0 : ctx.rng().Next32();
        const Run ra = RunCase(initial, false, [&](SkipNonTempBase* s) {
            return original(reinterpret_cast<std::uint32_t>(s), nullptr, value) + reinterpret_cast<std::uint32_t>(s);
        });
        const Run rb = RunCase(initial, false, [&](SkipNonTempBase* s) {
            return ours(reinterpret_cast<std::uint32_t>(s), nullptr, value) + reinterpret_cast<std::uint32_t>(s);
        });
        if (!SameRuns(ctx, ra, rb, i)) {
            return false;
        }
    }
    return true;
}

// Wrong: +0x58 not set / the insert's result passed through.
SkipNonTempBase* __fastcall WrongConstruct(SkipNonTempBase* self, void* edx, std::uint32_t a, std::uint32_t b,
                                           std::uint32_t c) {
    const std::uint32_t keep = self->allocatorArg;
    WCSkipNonTempBase_Construct(self, edx, a, b, c);
    self->allocatorArg = keep;
    return self;
}
std::uint32_t __fastcall WrongInsert(std::uint32_t self, void*, std::uint32_t value) {
    return reinterpret_cast<InsertFn>(kBaseInsert)(self, nullptr, value - 0x28);
}

}  // namespace

S3RE_TEST(WatcomSkipListBase_MatchOriginal) {
    CompareConstruct(ctx, Original_WCSkipNonTempBase_Construct, &WCSkipNonTempBase_Construct, false, 300) &&
        CompareConstruct(ctx, Original_SkipListConstructor_609f20, &SkipListConstructor_609f20, true, 100) &&
        CompareConstruct(ctx, Original_SkipListConstructor_609ed0, &SkipListConstructor_609ed0, true, 100) &&
        CompareConstruct(ctx, Original_SkipListConstructor_458030, &SkipListConstructor_458030, true, 100) &&
        CompareDictConstruct(ctx, 100) && CompareInsert(ctx, Original_SkipInsert_609b90, &SkipInsert_609b90, 300) &&
        CompareInsert(ctx, Original_SkipInsert_472400, &SkipInsert_472400, 300);
}

S3RE_TEST(WatcomSkipListBase_ComparisonDetectsWrongImplementation) {
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareConstruct(probe, Original_WCSkipNonTempBase_Construct, &WrongConstruct, false, 20);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareInsert(probe, Original_SkipInsert_609b90, &WrongInsert, 50);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::watcom
