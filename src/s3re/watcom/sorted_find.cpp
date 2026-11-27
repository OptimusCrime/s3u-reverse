// WCSortedVectorBase<T>::base_find_with_index (vtable slot 3) of the sorted
// vectors (template instantiations; only the element size differs).
// thiscall (const T* element, int mode, int* index) ret 0xc. The vector's
// own vtable gives the comparisons, called with ECX = the vector and two
// element pointers: slot 2 (+8) "equivalent", slot 4 (+0x10) "less than".
//   mode 2 ("at index"): *index < length (signed) and equivalent(data[*index],
//     element): 1, else 0 (*index unchanged).
//   other modes: a binary search: lo = 0, hi = length - 1; while lo < hi:
//     mid = lo + (hi - lo) / 2 (signed, toward zero); less(data[mid],
//     element) ? lo = mid + 1 : hi = mid. Then equivalent(element, data[lo])
//     (arguments in this order; data[0] is compared even when the vector is
//     empty, as the original does):
//     equal: mode 3 ("after the last equal one") advances lo while lo <
//       length and equivalent(data[lo], element); *index = lo; 1.
//     not equal: mode 3: lo + 1 if less(data[lo], element); *index = lo;
//       other modes leave *index alone; 0.
//   address    size   class
//   0x00437eb0  0x40  WCValSortedVector<Dirk::CTeam>
//   0x0044d330  0x54  WCSortedVectorBase<LobbyClient::CPlayer>
//   0x0044d730 0x11c  WCSortedVectorBase<LobbyClient::CGroup>
//   0x0044d850  0x10  WCSortedVectorBase<String>
//   0x004585f0  0x04  WCSortedVectorBase<unsigned int> (was Lobby_sub_4585f0)
//   0x00463840  0x60  WCSortedVectorBase<DirectX::CRunningSession>
//   0x00541790  0x18  WCValSortedVector<Lobby::CGamePlayerStatus>
//   0x005bdd60  0x08  WCValSortedVector<DirectX::CGameNetSession::CPlayerIndex>
//   0x005e0280  0x20  WCSortedVectorBase<LobbyClient::priv::CLobbyServer>
// WCOrderedVectorBase<T>::base_find (slot 3) of the ordered vectors, the
// same arguments (the mode is not used): from *index (signed) while below
// the length (re-read), the first element with equivalent(data[i],
// element): *index = i, 1; none: 0.
//   0x005b83e0  0x6c  WCValOrderedVector<DirectX::CDisplayMode>
//   0x005b8290  0x30  WCValOrderedVector<DirectX::CDirectDrawDevice>
//   0x00499b30 0x120  WCValOrderedVector<GameChunkDescription>
//   0x00468550  0x40  WCValOrderedVector<DirectX::CConnectionShortcut>

#include "watcom/vector_base.h"

#include "hooks.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::watcom {

using SortedFindFn = std::uint32_t(__fastcall*)(VectorBase* self, void* edx, std::uint32_t element, std::int32_t mode,
                                                std::int32_t* index);

namespace {

constexpr std::int32_t kModeAtIndex = 2;
constexpr std::int32_t kModeAfterLast = 3;
constexpr std::uint32_t kEquivalentSlot = 2;
constexpr std::uint32_t kLessThanSlot = 4;

using CompareSlotFn = std::uint32_t(__fastcall*)(VectorBase* self, void* edx, std::uint32_t a, std::uint32_t b);

std::uint32_t CallSlot(VectorBase* self, std::uint32_t slot, std::uint32_t a, std::uint32_t b) {
    const auto* vtable = reinterpret_cast<const CompareSlotFn*>(self->vtable);
    return vtable[slot](self, nullptr, a, b);
}

std::uint32_t FindWithIndex(VectorBase* self, std::uint32_t element, std::int32_t mode, std::int32_t* index,
                            std::uint32_t elementSize) {
    const auto at = [&](std::int32_t i) { return self->data + static_cast<std::uint32_t>(i) * elementSize; };
    const auto length = [&] { return static_cast<std::int32_t>(self->length); };
    if (mode == kModeAtIndex) {
        const std::int32_t i = *index;
        return i < length() && CallSlot(self, kEquivalentSlot, at(i), element) != 0 ? 1 : 0;
    }
    std::int32_t lo = 0;
    std::int32_t hi = length() - 1;
    while (lo < hi) {
        const std::int32_t mid = lo + (hi - lo) / 2;
        if (CallSlot(self, kLessThanSlot, at(mid), element) != 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (CallSlot(self, kEquivalentSlot, element, at(lo)) != 0) {
        if (mode == kModeAfterLast) {
            while (lo < length() && CallSlot(self, kEquivalentSlot, at(lo), element) != 0) {
                ++lo;
            }
        }
        *index = lo;
        return 1;
    }
    if (mode == kModeAfterLast) {
        if (CallSlot(self, kLessThanSlot, at(lo), element) != 0) {
            ++lo;
        }
        *index = lo;
    }
    return 0;
}

std::uint32_t FindFrom(VectorBase* self, std::uint32_t element, std::int32_t* index, std::uint32_t elementSize) {
    for (std::int32_t i = *index; i < static_cast<std::int32_t>(self->length); ++i) {
        if (CallSlot(self, kEquivalentSlot, self->data + static_cast<std::uint32_t>(i) * elementSize, element) != 0) {
            *index = i;
            return 1;
        }
    }
    return 0;
}

}  // namespace

// Purpose: Finds the next equal element in the display code's device and
// mode lists, the map file's chunk list and the network's shortcuts.
std::uint32_t __fastcall OrderedFind_5b83e0(VectorBase* self, void*, std::uint32_t element, std::int32_t,
                                            std::int32_t* index) {
    return FindFrom(self, element, index, 0x6c);
}
std::uint32_t __fastcall OrderedFind_5b8290(VectorBase* self, void*, std::uint32_t element, std::int32_t,
                                            std::int32_t* index) {
    return FindFrom(self, element, index, 0x30);
}
std::uint32_t __fastcall OrderedFind_499b30(VectorBase* self, void*, std::uint32_t element, std::int32_t,
                                            std::int32_t* index) {
    return FindFrom(self, element, index, 0x120);
}
std::uint32_t __fastcall OrderedFind_468550(VectorBase* self, void*, std::uint32_t element, std::int32_t,
                                            std::int32_t* index) {
    return FindFrom(self, element, index, 0x40);
}

// Purpose: Finds an element (or where it belongs) in the game settings'
// sorted teams and the lobby's and network's sorted vectors.
std::uint32_t __fastcall SortedFind_437eb0(VectorBase* self, void*, std::uint32_t element, std::int32_t mode,
                                           std::int32_t* index) {
    return FindWithIndex(self, element, mode, index, 0x40);
}
std::uint32_t __fastcall SortedFind_44d330(VectorBase* self, void*, std::uint32_t element, std::int32_t mode,
                                           std::int32_t* index) {
    return FindWithIndex(self, element, mode, index, 0x54);
}
std::uint32_t __fastcall SortedFind_44d730(VectorBase* self, void*, std::uint32_t element, std::int32_t mode,
                                           std::int32_t* index) {
    return FindWithIndex(self, element, mode, index, 0x11c);
}
std::uint32_t __fastcall SortedFind_44d850(VectorBase* self, void*, std::uint32_t element, std::int32_t mode,
                                           std::int32_t* index) {
    return FindWithIndex(self, element, mode, index, 0x10);
}
std::uint32_t __fastcall SortedFind_4585f0(VectorBase* self, void*, std::uint32_t element, std::int32_t mode,
                                           std::int32_t* index) {
    return FindWithIndex(self, element, mode, index, 0x04);
}
std::uint32_t __fastcall SortedFind_463840(VectorBase* self, void*, std::uint32_t element, std::int32_t mode,
                                           std::int32_t* index) {
    return FindWithIndex(self, element, mode, index, 0x60);
}
std::uint32_t __fastcall SortedFind_541790(VectorBase* self, void*, std::uint32_t element, std::int32_t mode,
                                           std::int32_t* index) {
    return FindWithIndex(self, element, mode, index, 0x18);
}
std::uint32_t __fastcall SortedFind_5bdd60(VectorBase* self, void*, std::uint32_t element, std::int32_t mode,
                                           std::int32_t* index) {
    return FindWithIndex(self, element, mode, index, 0x08);
}
std::uint32_t __fastcall SortedFind_5e0280(VectorBase* self, void*, std::uint32_t element, std::int32_t mode,
                                           std::int32_t* index) {
    return FindWithIndex(self, element, mode, index, 0x20);
}

S3RE_HOOK(0x00437eb0, SortedFind_437eb0, SortedFindFn, Original_SortedFind_437eb0, SortedFind_437eb0);
S3RE_HOOK(0x0044d330, SortedFind_44d330, SortedFindFn, Original_SortedFind_44d330, SortedFind_44d330);
S3RE_HOOK(0x0044d730, SortedFind_44d730, SortedFindFn, Original_SortedFind_44d730, SortedFind_44d730);
S3RE_HOOK(0x0044d850, SortedFind_44d850, SortedFindFn, Original_SortedFind_44d850, SortedFind_44d850);
S3RE_HOOK(0x004585f0, SortedFind_4585f0, SortedFindFn, Original_SortedFind_4585f0, SortedFind_4585f0);
S3RE_HOOK(0x00463840, SortedFind_463840, SortedFindFn, Original_SortedFind_463840, SortedFind_463840);
S3RE_HOOK(0x00541790, SortedFind_541790, SortedFindFn, Original_SortedFind_541790, SortedFind_541790);
S3RE_HOOK(0x005bdd60, SortedFind_5bdd60, SortedFindFn, Original_SortedFind_5bdd60, SortedFind_5bdd60);
S3RE_HOOK(0x005e0280, SortedFind_5e0280, SortedFindFn, Original_SortedFind_5e0280, SortedFind_5e0280);
S3RE_HOOK(0x005b83e0, OrderedFind_5b83e0, SortedFindFn, Original_OrderedFind_5b83e0, OrderedFind_5b83e0);
S3RE_HOOK(0x005b8290, OrderedFind_5b8290, SortedFindFn, Original_OrderedFind_5b8290, OrderedFind_5b8290);
S3RE_HOOK(0x00499b30, OrderedFind_499b30, SortedFindFn, Original_OrderedFind_499b30, OrderedFind_499b30);
S3RE_HOOK(0x00468550, OrderedFind_468550, SortedFindFn, Original_OrderedFind_468550, OrderedFind_468550);

// ---------------------------------------------------------------------------
// Tests: a vector of 0..8 elements whose first dword is a small key (sorted,
// with repeats), a fake vtable whose comparisons compare those keys (and
// record their calls as element indexes), the searched key 0..9, every mode
// 0..4 and a random index. Compared: EAX, *index and the calls.
// ---------------------------------------------------------------------------

namespace {

struct FindPair {
    SortedFindFn original;
    SortedFindFn ours;
    std::uint32_t address;
    std::uint32_t elementSize;
};

const FindPair kFindPairs[] = {
    {Original_SortedFind_437eb0, &SortedFind_437eb0, 0x437eb0, 0x40},
    {Original_SortedFind_44d330, &SortedFind_44d330, 0x44d330, 0x54},
    {Original_SortedFind_44d730, &SortedFind_44d730, 0x44d730, 0x11c},
    {Original_SortedFind_44d850, &SortedFind_44d850, 0x44d850, 0x10},
    {Original_SortedFind_4585f0, &SortedFind_4585f0, 0x4585f0, 0x04},
    {Original_SortedFind_463840, &SortedFind_463840, 0x463840, 0x60},
    {Original_SortedFind_541790, &SortedFind_541790, 0x541790, 0x18},
    {Original_SortedFind_5bdd60, &SortedFind_5bdd60, 0x5bdd60, 0x08},
    {Original_SortedFind_5e0280, &SortedFind_5e0280, 0x5e0280, 0x20},
    {Original_OrderedFind_5b83e0, &OrderedFind_5b83e0, 0x5b83e0, 0x6c},
    {Original_OrderedFind_5b8290, &OrderedFind_5b8290, 0x5b8290, 0x30},
    {Original_OrderedFind_499b30, &OrderedFind_499b30, 0x499b30, 0x120},
    {Original_OrderedFind_468550, &OrderedFind_468550, 0x468550, 0x40},
};

std::vector<std::uint32_t> g_calls;
std::uint32_t g_base = 0;  // the block (element pointers recorded relative to it)

std::uint32_t Key(std::uint32_t p) {
    return *reinterpret_cast<const std::uint32_t*>(p);
}
std::uint32_t __fastcall FakeEquivalent(VectorBase*, void*, std::uint32_t a, std::uint32_t b) {
    g_calls.insert(g_calls.end(), {2, a - g_base, b - g_base});
    return Key(a) == Key(b) ? 0x100 + Key(a) : 0;  // any nonzero value counts
}
std::uint32_t __fastcall FakeLessThan(VectorBase*, void*, std::uint32_t a, std::uint32_t b) {
    g_calls.insert(g_calls.end(), {4, a - g_base, b - g_base});
    return Key(a) < Key(b) ? 1 : 0;
}
const std::uint32_t kFakeVtable[5] = {0, 0, reinterpret_cast<std::uint32_t>(&FakeEquivalent), 0,
                                      reinterpret_cast<std::uint32_t>(&FakeLessThan)};

bool CompareFind(test::Context& ctx, const FindPair& pair, int cases) {
    std::vector<std::uint8_t> block(9 * pair.elementSize + 4);
    for (int i = 0; i < cases; ++i) {
        ctx.rng().Fill(block.data(), block.size());
        const std::uint32_t length = ctx.rng().Below(9);
        std::uint32_t key = 0;
        for (std::uint32_t k = 0; k < 9; ++k) {
            key += ctx.rng().Below(3);
            std::memcpy(block.data() + k * pair.elementSize, &key, 4);
        }
        // The searched element sits behind the vector's elements.
        const std::uint32_t searched = ctx.rng().Below(10) + (ctx.rng().Chance(1, 8) ? 100 : 0);
        std::uint8_t elementBytes[4];
        std::memcpy(elementBytes, &searched, 4);
        const std::int32_t mode = ctx.rng().Range(0, 4);
        const std::int32_t startIndex = ctx.rng().Range(-2, 10);
        g_base = reinterpret_cast<std::uint32_t>(block.data());
        auto run = [&](SortedFindFn fn, std::int32_t& index) {
            VectorBase vector{};
            vector.vtable = reinterpret_cast<std::uint32_t>(kFakeVtable);
            vector.data = g_base;
            vector.length = length;
            index = startIndex;
            g_calls.clear();
            const std::uint32_t eax = fn(&vector, nullptr, reinterpret_cast<std::uint32_t>(elementBytes), mode, &index);
            std::vector<std::uint32_t> calls = g_calls;
            calls.push_back(eax);
            return calls;
        };
        std::int32_t ia = 0, ib = 0;
        const auto ra = run(pair.original, ia);
        const auto rb = run(pair.ours, ib);
        if (!S3RE_EXPECT(ra == rb) || !S3RE_EXPECT_EQ(ia, ib)) {
            ctx.Fail(__FILE__, __LINE__, "  0x%x case %d: length %u, key %u, mode %d", pair.address, i, length,
                     searched, mode);
            return false;
        }
    }
    return true;
}

// Wrong: mode 3 does not advance past the equal elements.
std::uint32_t __fastcall WrongFind(VectorBase* self, void*, std::uint32_t element, std::int32_t mode,
                                   std::int32_t* index) {
    return FindWithIndex(self, element, mode == kModeAfterLast ? 1 : mode, index, 0x10);
}

}  // namespace

S3RE_TEST(WatcomSortedFind_MatchOriginal) {
    for (const FindPair& pair : kFindPairs) {
        if (!CompareFind(ctx, pair, 1000)) {
            return;
        }
    }
}

S3RE_TEST(WatcomSortedFind_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareFind(probe, {Original_SortedFind_44d850, &WrongFind, 0x44d850, 0x10}, 300);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::watcom
