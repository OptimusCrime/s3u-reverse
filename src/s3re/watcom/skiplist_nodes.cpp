// WCSkipListBase<WCSkipListDictKeyVal<K, String>>::base_new_node (vtable
// slot 3) of the two string dictionaries (template instantiations, the same
// bytes; only the key type differs, and it is only copied):
//   0x00458e20 <unsigned int, String>    0x00499b90 <int, String>
// thiscall (const KeyVal* pair, int level) ret 8: a node of 4 * level +
// 0x18 bytes from the dictionary's allocator (+0x5c, fastcall with the size
// in ECX and EDX) or operator new; none: EAX = 0. Else node +0 = the key,
// +4 = a copy of the string (its allocator byte is the low byte of the
// node's address: the compiler reused `level`'s stack slot for the node
// before reading the allocator argument from it; the copy is assign(pair's
// string, 0, npos), inlined), EAX = node + 0x14 (the forward pointers after
// the key and value).

// WCSkipListBase<WCSkipListDictKeyVal<K, V>>::base_destroy_node (slot 4),
// the counterpart (the same code for every instantiation; the pair's size
// and destructor differ): (node, level) ret 8. node 0: EAX = 0. Else the
// key/value pair (node - its size) is destroyed (thiscall), then freed by
// the dictionary's deallocator (+0x60; fastcall, ECX = the block, EDX =
// 4 * level + the pair's size + 4) or operator delete; EAX = what that
// returns.
//   0x00499d60 <int, String>                                      pair 0x14, its destructor 0x49a070
//   0x00458ff0 <unsigned int, String>                             pair 0x14, its destructor 0x459440
//   0x004591b0 <unsigned int, WCValOrderedVector<String>>         pair 0x20, its destructor 0x459550
//   0x00472dc0 <unsigned long, WCPtrSortedVector<CExecuteType>>   pair 0x20, its destructor 0x473290
// (0x00472eb0, <unsigned long, CNetSession::CSyncInfo>, is left to the
// original: its pair destructor is folded into DebugLog's bare `ret`, which
// the original calls with the caller's saved EDI where DebugLog's format
// argument would be; the emulator records that and C++ can't reproduce it.)
//   0x0060a760 <unsigned int, Lobby::CLMMessageBlock>             pair 0x28, its destructor 0x60b2d0

#include "hooks.h"
#include "test/callee_stub.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::watcom {

#pragma pack(push, 1)
struct StringKeyVal {
    std::uint32_t key;
    msvc6::String value;
};
#pragma pack(pop)
static_assert(sizeof(StringKeyVal) == 0x14);

using SkipNewNodeFn = std::uint32_t(__fastcall*)(std::uint32_t self, void* edx, const StringKeyVal* pair,
                                                 std::int32_t level);

namespace {

constexpr std::uint32_t kAllocator = 0x5c;     // the dictionary's node allocator (0: operator new)
constexpr std::uint32_t kNodeFixedBytes = 0x18;  // key, value and the first forward pointer
constexpr std::uint32_t kForwardPointers = sizeof(StringKeyVal);
constexpr std::uint32_t kWholeString = 0xFFFFFFFF;  // npos as the inlined copy has it (a constant)

using AllocatorFn = std::uint32_t(__fastcall*)(std::uint32_t size, std::uint32_t sizeAgain);

std::uint32_t NewStringNode(std::uint32_t self, const StringKeyVal* pair, std::int32_t level) {
    const std::uint32_t size = static_cast<std::uint32_t>(level) * 4 + kNodeFixedBytes;
    const auto allocator = *reinterpret_cast<const AllocatorFn*>(self + kAllocator);
    const std::uint32_t node = allocator != nullptr
                                   ? allocator(size, size)
                                   : reinterpret_cast<std::uintptr_t>(msvc6::GameOperatorNew(size));
    if (node == 0) {
        return 0;
    }
    auto* copy = reinterpret_cast<StringKeyVal*>(node);
    copy->key = pair->key;
    copy->value.allocator = static_cast<std::uint8_t>(node);  // deliberate: the original's reused stack slot
    copy->value.ptr = nullptr;
    copy->value.len = 0;
    copy->value.res = 0;
    msvc6::String_AssignSubstr(&copy->value, nullptr, &pair->value, 0, kWholeString);
    return node + kForwardPointers;
}

}  // namespace

// Purpose: Creates a node of the menus' string dictionaries (a copy of the
// key and the string, room for `level` forward pointers).
std::uint32_t __fastcall SkipNewNode_458e20(std::uint32_t self, void*, const StringKeyVal* pair, std::int32_t level) {
    return NewStringNode(self, pair, level);
}
std::uint32_t __fastcall SkipNewNode_499b90(std::uint32_t self, void*, const StringKeyVal* pair, std::int32_t level) {
    return NewStringNode(self, pair, level);
}

S3RE_HOOK(0x00458e20, SkipNewNode_458e20, SkipNewNodeFn, Original_SkipNewNode_458e20, SkipNewNode_458e20);
S3RE_HOOK(0x00499b90, SkipNewNode_499b90, SkipNewNodeFn, Original_SkipNewNode_499b90, SkipNewNode_499b90);

// ---------------------------------------------------------------------------
// Tests: a dictionary without an allocator; the pair's string without a
// buffer or with one shared or not (count byte 0..0xff, lengths 0..20),
// levels 1..12. Compared: EAX (as null or not), the
// node's key and string fields (ptr as shared-with-the-source or not), the
// copied text and the source's count byte.
// ---------------------------------------------------------------------------

namespace {

bool CompareNewNode(test::Context& ctx, SkipNewNodeFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        std::uint8_t dictionary[0x80];
        ctx.rng().Fill(dictionary, sizeof(dictionary));
        std::memset(dictionary + kAllocator, 0, 4);
        const std::int32_t level = 1 + static_cast<std::int32_t>(ctx.rng().Below(12));
        const std::uint32_t len = ctx.rng().Below(21);
        const bool buffer = ctx.rng().Chance(4, 5);
        const std::uint8_t count = static_cast<std::uint8_t>(ctx.rng().Next32());
        const std::uint32_t key = ctx.rng().Next32();
        char text[24];
        for (std::uint32_t c = 0; c < len; ++c) {
            text[c] = static_cast<char>('a' + ctx.rng().Below(26));
        }
        struct Result {
            std::uint32_t key = 0;
            std::uint8_t allocator = 0;
            bool shared = false, null = false;
            std::uint32_t len = 0, res = 0;
            std::vector<char> chars;
            std::uint8_t sourceCount = 0;
        };
        auto run = [&](SkipNewNodeFn fn) {
            Result r;
            std::uint8_t source[24];
            source[0] = count;
            std::memcpy(source + 1, text, len);
            source[1 + len] = 0;
            StringKeyVal pair{};
            pair.key = key;
            pair.value.ptr = buffer ? reinterpret_cast<char*>(source + 1) : nullptr;
            pair.value.len = buffer ? len : 0;
            pair.value.res = buffer ? len : 0;
            const std::uint32_t eax = fn(reinterpret_cast<std::uint32_t>(dictionary), nullptr, &pair, level);
            r.null = eax == 0;
            if (eax != 0) {
                auto* node = reinterpret_cast<StringKeyVal*>(eax - kForwardPointers);
                r.key = node->key;
                // relative to the node's low byte (the blocks differ between the runs)
                r.allocator = static_cast<std::uint8_t>(node->value.allocator - reinterpret_cast<std::uintptr_t>(node));
                r.shared = node->value.ptr == pair.value.ptr;
                r.len = node->value.len;
                r.res = node->value.res;
                if (node->value.ptr != nullptr) {
                    r.chars.assign(node->value.ptr, node->value.ptr + node->value.len + 1);
                }
                if (!r.shared) {
                    msvc6::String_Destroy(&node->value, nullptr);
                }
                msvc6::GameOperatorDelete(node);
            }
            r.sourceCount = source[0];
            return r;
        };
        const Result a = run(Original_SkipNewNode_499b90);
        const Result b = run(candidate);
        if (!S3RE_EXPECT(a.null == b.null) || !S3RE_EXPECT_EQ(a.key, b.key) ||
            !S3RE_EXPECT_EQ(a.allocator, b.allocator) || !S3RE_EXPECT(a.shared == b.shared) ||
            !S3RE_EXPECT_EQ(a.len, b.len) || !S3RE_EXPECT_EQ(a.res, b.res) || !S3RE_EXPECT(a.chars == b.chars) ||
            !S3RE_EXPECT_EQ(a.sourceCount, b.sourceCount)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: len %u, count %u, level %d", i, len, count, level);
            return false;
        }
    }
    return true;
}

// Wrong: the allocator byte from the level.
std::uint32_t __fastcall WrongNewNode(std::uint32_t self, void* edx, const StringKeyVal* pair, std::int32_t level) {
    const std::uint32_t result = NewStringNode(self, pair, level);
    if (result != 0) {
        reinterpret_cast<StringKeyVal*>(result - kForwardPointers)->value.allocator = static_cast<std::uint8_t>(level);
    }
    return result;
}

}  // namespace

S3RE_TEST(WatcomSkipNewNode_MatchesOriginal) {
    CompareNewNode(ctx, &SkipNewNode_499b90, 2000) && CompareNewNode(ctx, &SkipNewNode_458e20, 2000);
}

S3RE_TEST(WatcomSkipNewNode_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareNewNode(probe, &WrongNewNode, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

using SkipDestroyNodeFn = std::uint32_t(__fastcall*)(std::uint32_t self, void* edx, std::uint32_t node,
                                                     std::uint32_t level);

namespace {

constexpr std::uint32_t kDeallocator = 0x60;  // the dictionary's node deallocator (0: operator delete)

using DeallocatorFn = std::uint32_t(__fastcall*)(std::uint32_t block, std::uint32_t size);
using PairDestructorFn = void(__fastcall*)(std::uint32_t pair, void* edx);

std::uint32_t DestroyNode(std::uint32_t self, std::uint32_t node, std::uint32_t level, std::uint32_t pairSize,
                          std::uintptr_t pairDestructor) {
    if (node == 0) {
        return 0;
    }
    const std::uint32_t pair = node - pairSize;
    reinterpret_cast<PairDestructorFn>(pairDestructor)(pair, nullptr);
    const auto deallocator = *reinterpret_cast<const DeallocatorFn*>(self + kDeallocator);
    if (deallocator != nullptr) {
        return deallocator(pair, level * 4 + pairSize + 4);
    }
    return msvc6::GameOperatorDelete(reinterpret_cast<void*>(pair));
}

}  // namespace

// Purpose: Frees a node of the menus' string dictionaries, the network's
// request and sync tables and the lobby's message blocks.
std::uint32_t __fastcall SkipDestroyNode_499d60(std::uint32_t self, void*, std::uint32_t node, std::uint32_t level) {
    return DestroyNode(self, node, level, 0x14, 0x0049a070);
}
std::uint32_t __fastcall SkipDestroyNode_458ff0(std::uint32_t self, void*, std::uint32_t node, std::uint32_t level) {
    return DestroyNode(self, node, level, 0x14, 0x00459440);
}
std::uint32_t __fastcall SkipDestroyNode_4591b0(std::uint32_t self, void*, std::uint32_t node, std::uint32_t level) {
    return DestroyNode(self, node, level, 0x20, 0x00459550);
}
std::uint32_t __fastcall SkipDestroyNode_472dc0(std::uint32_t self, void*, std::uint32_t node, std::uint32_t level) {
    return DestroyNode(self, node, level, 0x20, 0x00473290);
}
std::uint32_t __fastcall SkipDestroyNode_60a760(std::uint32_t self, void*, std::uint32_t node, std::uint32_t level) {
    return DestroyNode(self, node, level, 0x28, 0x0060b2d0);
}

S3RE_HOOK(0x00499d60, SkipDestroyNode_499d60, SkipDestroyNodeFn, Original_SkipDestroyNode_499d60,
          SkipDestroyNode_499d60);
S3RE_HOOK(0x00458ff0, SkipDestroyNode_458ff0, SkipDestroyNodeFn, Original_SkipDestroyNode_458ff0,
          SkipDestroyNode_458ff0);
S3RE_HOOK(0x004591b0, SkipDestroyNode_4591b0, SkipDestroyNodeFn, Original_SkipDestroyNode_4591b0,
          SkipDestroyNode_4591b0);
S3RE_HOOK(0x00472dc0, SkipDestroyNode_472dc0, SkipDestroyNodeFn, Original_SkipDestroyNode_472dc0,
          SkipDestroyNode_472dc0);
S3RE_HOOK(0x0060a760, SkipDestroyNode_60a760, SkipDestroyNodeFn, Original_SkipDestroyNode_60a760,
          SkipDestroyNode_60a760);

// Tests: dictionaries with a recording deallocator or none (then a block
// from operator new), nodes null or not, levels 0..12; the pair destructors
// replaced by recording stubs. Compared: EAX (with operator delete: as 0 or
// not), the pair destructor's calls (relative to the block) and the
// deallocator's (block offset, size).
namespace {

struct DestroyNodePair {
    SkipDestroyNodeFn original;
    SkipDestroyNodeFn ours;
    std::uint32_t address;
    std::uint32_t pairSize;
};

const DestroyNodePair kDestroyNodePairs[] = {
    {Original_SkipDestroyNode_499d60, &SkipDestroyNode_499d60, 0x499d60, 0x14},
    {Original_SkipDestroyNode_458ff0, &SkipDestroyNode_458ff0, 0x458ff0, 0x14},
    {Original_SkipDestroyNode_4591b0, &SkipDestroyNode_4591b0, 0x4591b0, 0x20},
    {Original_SkipDestroyNode_472dc0, &SkipDestroyNode_472dc0, 0x472dc0, 0x20},
    {Original_SkipDestroyNode_60a760, &SkipDestroyNode_60a760, 0x60a760, 0x28},
};

std::vector<std::uint32_t> g_deallocations;
std::uint32_t g_deallocBlock = 0;
std::uint32_t __fastcall RecordDeallocate(std::uint32_t block, std::uint32_t size) {
    g_deallocations.push_back(block - g_deallocBlock);
    g_deallocations.push_back(size);
    return 0x5eed0000 + size;
}

bool CompareDestroyNode(test::Context& ctx, const DestroyNodePair& pair, int cases) {
    for (int i = 0; i < cases; ++i) {
        const bool withDeallocator = ctx.rng().Chance(1, 2);
        const bool null = ctx.rng().Chance(1, 6);
        const std::uint32_t level = ctx.rng().Below(13);
        struct Result {
            std::uint32_t eax;
            std::vector<test::StubCall> calls;
            std::vector<std::uint32_t> deallocations;
        };
        auto run = [&](SkipDestroyNodeFn fn) {
            std::uint8_t dictionary[0x80] = {};
            std::uint8_t fixedBlock[0x100] = {};
            const DeallocatorFn deallocate = &RecordDeallocate;
            if (withDeallocator) {
                std::memcpy(dictionary + kDeallocator, &deallocate, 4);
            }
            auto* block = withDeallocator ? fixedBlock : static_cast<std::uint8_t*>(msvc6::GameOperatorNew(0x100));
            g_deallocBlock = reinterpret_cast<std::uint32_t>(block);
            g_deallocations.clear();
            const std::uint32_t node = null ? 0 : g_deallocBlock + pair.pairSize;
            Result r{};
            test::CalleeStubs stubs;
            stubs.Install<0, 0x0049a070>();
            stubs.Install<0, 0x00459440>();
            stubs.Install<0, 0x00459550>();
            stubs.Install<0, 0x00473290>();
            stubs.Install<0, 0x0060b2d0>();
            stubs.SetWorld(reinterpret_cast<game::CGameData*>(block));
            r.eax = fn(reinterpret_cast<std::uint32_t>(dictionary), nullptr, node, level);
            if (!withDeallocator) {
                r.eax = r.eax != 0;
                if (null) {
                    msvc6::GameOperatorDelete(block);
                }
            }
            r.calls = stubs.Take();
            r.deallocations = g_deallocations;
            return r;
        };
        const Result a = run(pair.original);
        const Result b = run(pair.ours);
        if (!S3RE_EXPECT_EQ(a.eax, b.eax) || !S3RE_EXPECT(a.calls == b.calls) ||
            !S3RE_EXPECT(a.deallocations == b.deallocations)) {
            ctx.Fail(__FILE__, __LINE__, "  0x%x case %d: deallocator %d, null %d, level %u", pair.address, i,
                     withDeallocator, null, level);
            return false;
        }
    }
    return true;
}

// Wrong: the other string dictionary's pair destructor.
std::uint32_t __fastcall WrongDestroyNode(std::uint32_t self, void*, std::uint32_t node, std::uint32_t level) {
    return DestroyNode(self, node, level, 0x14, 0x00459440);
}

}  // namespace

S3RE_TEST(WatcomSkipDestroyNode_MatchOriginal) {
    for (const DestroyNodePair& pair : kDestroyNodePairs) {
        if (!CompareDestroyNode(ctx, pair, 300)) {
            return;
        }
    }
}

S3RE_TEST(WatcomSkipDestroyNode_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareDestroyNode(probe, {Original_SkipDestroyNode_499d60, &WrongDestroyNode, 0x499d60, 0x14}, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::watcom
