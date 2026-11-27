// More WCSkipListBase<WCSkipListDictKeyVal<K, V>>::base_new_node (vtable
// slot 3) instantiations (libs-r7) and a dictionary constructor with a
// value member. base_new_node: thiscall (const KeyVal* pair, int level)
// ret 8: a node of 4 * level + the pair's size + 4 bytes from the
// dictionary's allocator (+0x5c, fastcall with the size in ECX) or operator
// new; none: EAX = 0. Else the pair copied to the node's start; EAX = node +
// the pair's size (the forward pointers after it).
//   0x00472e60 <unsigned long, CNetSession::CSyncInfo>, pair 0xc: three
//     dwords copied. The allocator gets the caller's EDX.
//   0x00472c70 <unsigned long, WCPtrSortedVector<CExecuteType>>, pair 0x20:
//     the key, then the vector copy-constructed inline (CopyVector, the
//     pointers copied as dwords). The allocator gets the caller's EDX.
//   0x0060a6d0 <unsigned int, Lobby::CLMMessageBlock>, pair 0x28: the key,
//     then CLMMessageBlock's copy constructor 0x601470 (ours) on node + 4.
//     The allocator gets the caller's EDX.
//   0x00459030 <unsigned int, WCValOrderedVector<String>>, pair 0x20: the
//     key, then the vector copied inline, each string _Tidy(false) and
//     assign(source, 0, npos) with the low byte of the node's address as its
//     allocator byte (the compiler reused `level`'s stack slot for the node,
//     as in 0x458e20). The allocator gets the size in ECX and EDX.
// WCValSkipListDict<unsigned int, WCValOrderedVector<String>>::
//   WCValSkipListDict (0x00457c10): (a, b) ret 8: WCSkipNonTempBase(a, b,
//   0), the base vtable 0x65721c, a WCValOrderedVector<String>(10, 5) at
//   +0x7c (inline), the dictionary's vtable 0x6571e4. EAX = this.
// WCBareVectorBase<void*>::base_copy (0x004732f0, was Net_sub_4732f0):
//   (const vector& from) ret 4: +4, filled and capacity from `from`, a new
//   block of capacity * 4 bytes if the capacity is positive (signed; none:
//   data 0; operator new failing: filled = capacity = 0, the error hook),
//   the `filled` pointers copied (filled and both blocks re-read, each
//   destination null-checked). Grow, length and the vtable are left to the
//   callers. EAX is not used by them.
// WCValSkipListDict<unsigned int, Lobby::CLMMessageBlock>'s copy constructor
//   (0x00609a20): (const dict& from) ret 4: WCSkipNonTempBase(from's +0x68,
//   +0x64, 0), vtable 0x65d588, the default value CLMMessageBlock(0, 0) at
//   +0x7c (0x601370), vtable 0x65d56c, base_assign(from) (0x60ba60).
// WCValSkipListDictIter<unsigned long, WCPtrSortedVector<CExecuteType>>::
//   value (0x00472ef0): (result) ret 4, a copy returned by value: on an
//   entry (node at +0xc), WCVectorBase<void*>(0, 0) at the result, vtable
//   0x657670, base_copy (0x4732f0) from the entry's vector (node - 0x1c),
//   its grow, and its length if there is a block (else 0); off an entry
//   (the library's error hook first) the same from a temporary empty
//   vector (10, 5), the result made by WCSortedVectorBase<void*>(0, 0)
//   instead, the temporary destroyed afterwards. EAX = result.

#include "watcom/vector_base.h"

#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace s3re::watcom {

namespace vt = game::vtables;

using NewNodeFn = std::uint32_t(__fastcall*)(std::uint32_t self, void* edx, std::uint32_t pair, std::int32_t level);
using DictConstructFn = std::uint32_t(__fastcall*)(std::uint32_t self, void* edx, std::uint32_t a, std::uint32_t b);
using OneArgFn = std::uint32_t(__fastcall*)(std::uint32_t self, void* edx, std::uint32_t arg);
using BaseCopyFn = void(__fastcall*)(VectorBase* self, void* edx, const VectorBase* from);

std::uint32_t __fastcall SkipNewNode_472e60(std::uint32_t self, void* edx, std::uint32_t pair, std::int32_t level);
std::uint32_t __fastcall SkipNewNode_472c70(std::uint32_t self, void* edx, std::uint32_t pair, std::int32_t level);
std::uint32_t __fastcall SkipNewNode_60a6d0(std::uint32_t self, void* edx, std::uint32_t pair, std::int32_t level);
std::uint32_t __fastcall SkipNewNode_459030(std::uint32_t self, void* edx, std::uint32_t pair, std::int32_t level);
std::uint32_t __fastcall SkipDictConstructor_457c10(std::uint32_t self, void* edx, std::uint32_t a, std::uint32_t b);
std::uint32_t __fastcall SkipDictCopy_609a20(std::uint32_t self, void* edx, std::uint32_t from);
void __fastcall VectorBaseCopy_4732f0(VectorBase* self, void* edx, const VectorBase* from);
std::uint32_t __fastcall SkipIterValue_472ef0(std::uint32_t self, void* edx, std::uint32_t result);

S3RE_HOOK(0x00472e60, SkipNewNode_472e60, NewNodeFn, Original_SkipNewNode_472e60, SkipNewNode_472e60);
S3RE_HOOK(0x00472c70, SkipNewNode_472c70, NewNodeFn, Original_SkipNewNode_472c70, SkipNewNode_472c70);
S3RE_HOOK(0x0060a6d0, SkipNewNode_60a6d0, NewNodeFn, Original_SkipNewNode_60a6d0, SkipNewNode_60a6d0);
S3RE_HOOK(0x00459030, SkipNewNode_459030, NewNodeFn, Original_SkipNewNode_459030, SkipNewNode_459030);
S3RE_HOOK(0x00457c10, SkipDictConstructor_457c10, DictConstructFn, Original_SkipDictConstructor_457c10,
          SkipDictConstructor_457c10);
S3RE_HOOK(0x004732f0, VectorBaseCopy_4732f0, BaseCopyFn, Original_VectorBaseCopy_4732f0, VectorBaseCopy_4732f0);
S3RE_HOOK(0x00609a20, SkipDictCopy_609a20, OneArgFn, Original_SkipDictCopy_609a20, SkipDictCopy_609a20);
S3RE_HOOK(0x00472ef0, SkipIterValue_472ef0, OneArgFn, Original_SkipIterValue_472ef0, SkipIterValue_472ef0);

namespace {

constexpr std::uint32_t kAllocator = 0x5c;  // the dictionary's node allocator (0: operator new)
constexpr std::uintptr_t kMessageBlockCopy = 0x00601470;  // Lobby::CLMMessageBlock's copy constructor (ours)
constexpr std::uintptr_t kSkipBaseConstruct = 0x0060b9f0;  // WCSkipNonTempBase::WCSkipNonTempBase (ours)
constexpr std::uint32_t kWholeString = 0xFFFFFFFF;          // npos as the inlined copy has it
constexpr std::uint32_t kStringSize = 0x10;
constexpr std::uint32_t kDictVector = 0x7c;  // the value vector of 0x457c10's dictionary
constexpr std::uint32_t kDictVectorCapacity = 10;
constexpr std::uint32_t kDictVectorGrow = 5;
constexpr std::uintptr_t kMessageBlockConstruct = 0x00601370;  // Lobby::CLMMessageBlock(int, int)
constexpr std::uintptr_t kSkipBaseAssign = 0x0060ba60;        // WCSkipNonTempBase::base_assign (original)
constexpr std::uint32_t kSkipProbability = 0x68;  // the base constructor's arguments, as kept by base_init
constexpr std::uint32_t kSkipLevels = 0x64;
constexpr std::uint32_t kDictValue = 0x7c;  // the dictionary's default value (a CLMMessageBlock)
constexpr std::uintptr_t kVoidVectorConstruct = 0x00468330;        // WCVectorBase<void*>(capacity, grow) (ours)
constexpr std::uintptr_t kSortedVoidVectorConstruct = 0x00462c70;  // WCSortedVectorBase<void*>(capacity, grow) (ours)
constexpr std::uintptr_t kVoidVectorDestroy = 0x00462eb0;          // ~WCBareVectorBase<void*> (ours)
constexpr std::uintptr_t kPtrVectorAssign = 0x004732f0;            // base_copy (ours, above)
constexpr std::uint32_t kIterCurrent = 0x0c;     // the iterator's current node (its forward pointers)
constexpr std::uint32_t kRequestPairValue = 0x1c;  // node - 0x1c: the pair's vector (the key 4 bytes before it)
constexpr std::uint32_t kDefaultCapacity = 10;
constexpr std::uint32_t kDefaultGrow = 5;

using TwoArgFn = std::uint32_t(__fastcall*)(std::uint32_t self, void* edx, std::uint32_t a, std::uint32_t b);
using VectorConstructFn = VectorBase*(__fastcall*)(VectorBase* self, void* edx, std::uint32_t capacity,
                                                   std::uint32_t grow);
using VectorDestroyFn = std::uint32_t(__fastcall*)(VectorBase* self, void* edx);

using AllocatorFn = std::uint32_t(__fastcall*)(std::uint32_t size, void* edx);
using ThisArgFn = std::uint32_t(__fastcall*)(std::uint32_t self, void* edx, std::uint32_t arg);
using BaseConstructFn = std::uint32_t(__fastcall*)(std::uint32_t self, void* edx, std::uint32_t a, std::uint32_t b,
                                                    std::uint32_t c);

std::uint32_t& Dword(std::uint32_t address) {
    return *reinterpret_cast<std::uint32_t*>(address);
}

// The node block: the dictionary's allocator (ECX = size, EDX as given) or
// operator new.
std::uint32_t AllocateNode(std::uint32_t self, void* edx, std::uint32_t size) {
    const auto allocator = reinterpret_cast<AllocatorFn>(Dword(self + kAllocator));
    return allocator != nullptr ? allocator(size, edx) : reinterpret_cast<std::uintptr_t>(msvc6::GameOperatorNew(size));
}

std::uint32_t NodeSize(std::int32_t level, std::uint32_t pairSize) {
    return static_cast<std::uint32_t>(level) * 4 + pairSize + 4;
}

}  // namespace

// Purpose: Creates a node of the network's sync-info dictionary (a copy of
// the key and the 8-byte value).
std::uint32_t __fastcall SkipNewNode_472e60(std::uint32_t self, void* edx, std::uint32_t pair, std::int32_t level) {
    constexpr std::uint32_t kPair = 0x0c;
    const std::uint32_t node = AllocateNode(self, edx, NodeSize(level, kPair));
    if (node == 0) {
        return 0;
    }
    for (std::uint32_t k = 0; k < kPair; k += 4) {
        Dword(node + k) = Dword(pair + k);
    }
    return node + kPair;
}

// Purpose: Creates a node of the network's request table (a copy of the
// key and of its vector of requests).
std::uint32_t __fastcall SkipNewNode_472c70(std::uint32_t self, void* edx, std::uint32_t pair, std::int32_t level) {
    constexpr std::uint32_t kPair = 0x20;
    const std::uint32_t node = AllocateNode(self, edx, NodeSize(level, kPair));
    if (node == 0) {
        return 0;
    }
    Dword(node) = Dword(pair);
    CopyVector(reinterpret_cast<VectorBase*>(node + 4), reinterpret_cast<const VectorBase*>(pair + 4),
               vt::kWCPtrSortedVector_CExecuteType, 4,
               [](std::int32_t, std::uint32_t to, std::uint32_t from) { Dword(to) = Dword(from); });
    return node + kPair;
}

// Purpose: Creates a node of the lobby protocol's message-block dictionary
// (a copy of the key and of the message block).
std::uint32_t __fastcall SkipNewNode_60a6d0(std::uint32_t self, void* edx, std::uint32_t pair, std::int32_t level) {
    constexpr std::uint32_t kPair = 0x28;
    const std::uint32_t node = AllocateNode(self, edx, NodeSize(level, kPair));
    if (node == 0) {
        return 0;
    }
    Dword(node) = Dword(pair);
    reinterpret_cast<ThisArgFn>(kMessageBlockCopy)(node + 4, nullptr, pair + 4);
    return node + kPair;
}

// Purpose: Creates a node of the lobby's dictionary of string lists (a
// copy of the key and of its strings).
std::uint32_t __fastcall SkipNewNode_459030(std::uint32_t self, void*, std::uint32_t pair, std::int32_t level) {
    constexpr std::uint32_t kPair = 0x20;
    const std::uint32_t size = NodeSize(level, kPair);
    const std::uint32_t node = AllocateNode(self, reinterpret_cast<void*>(static_cast<std::uintptr_t>(size)), size);
    if (node == 0) {
        return 0;
    }
    Dword(node) = Dword(pair);
    const auto allocatorByte = static_cast<std::uint8_t>(node);  // deliberate: the original's reused stack slot
    CopyVector(reinterpret_cast<VectorBase*>(node + 4), reinterpret_cast<const VectorBase*>(pair + 4),
               vt::kWCValOrderedVector_String, kStringSize,
               [allocatorByte](std::int32_t, std::uint32_t to, std::uint32_t from) {
                   auto* const copy = reinterpret_cast<msvc6::String*>(to);
                   copy->allocator = allocatorByte;
                   msvc6::String_Tidy(copy, nullptr, 0);
                   msvc6::String_AssignSubstr(copy, nullptr, reinterpret_cast<const msvc6::String*>(from), 0,
                                              kWholeString);
               });
    return node + kPair;
}

// Purpose: Constructor of the lobby's dictionary of string lists (with an
// empty list as the default value).
std::uint32_t __fastcall SkipDictConstructor_457c10(std::uint32_t self, void*, std::uint32_t a, std::uint32_t b) {
    reinterpret_cast<BaseConstructFn>(kSkipBaseConstruct)(self, nullptr, a, b, 0);
    Dword(self) = vt::kWCSkipListBase_UIntStringVector;
    ConstructVector(reinterpret_cast<VectorBase*>(self + kDictVector), kDictVectorCapacity, kDictVectorGrow,
                    kStringSize, vt::kWCBareVectorBase_String, vt::kWCValOrderedVector_String);
    Dword(self) = vt::kWCValSkipListDict_UIntStringVector;
    return self;
}

// Purpose: Copies the elements of a vector of pointers into another (the
// assignment used by the request tables' iterators, the game settings' and
// the DirectPlay player lists).
void __fastcall VectorBaseCopy_4732f0(VectorBase* self, void*, const VectorBase* from) {
    self->unk04 = from->unk04;
    self->filled = from->filled;
    self->capacity = from->capacity;
    if (static_cast<std::int32_t>(self->capacity) <= 0) {
        self->data = 0;
        return;
    }
    self->data = reinterpret_cast<std::uintptr_t>(msvc6::GameOperatorNew(self->capacity * 4));
    if (self->data == 0) {
        self->filled = 0;
        self->capacity = 0;  // (then the library's empty error hook 0x59f200)
        return;
    }
    for (std::int32_t i = 0; i < self->filled; ++i) {  // (filled and both blocks re-read per element)
        const std::uint32_t to = self->data + static_cast<std::uint32_t>(i) * 4;
        if (to != 0) {
            Dword(to) = Dword(from->data + static_cast<std::uint32_t>(i) * 4);
        }
    }
}

// Purpose: Copy constructor of the lobby protocol's message-block
// dictionary (its parameters, an empty default block, then every entry).
std::uint32_t __fastcall SkipDictCopy_609a20(std::uint32_t self, void*, std::uint32_t from) {
    reinterpret_cast<BaseConstructFn>(kSkipBaseConstruct)(self, nullptr, Dword(from + kSkipProbability),
                                                          Dword(from + kSkipLevels), 0);
    Dword(self) = vt::kWCSkipListBase_UIntMessageBlock;
    reinterpret_cast<TwoArgFn>(kMessageBlockConstruct)(self + kDictValue, nullptr, 0, 0);
    Dword(self) = vt::kWCValSkipListDict_UIntMessageBlock;
    reinterpret_cast<ThisArgFn>(kSkipBaseAssign)(self, nullptr, from);
    return self;
}

// Purpose: The value at the network request table iterator's position, a
// copy of its vector of requests (an empty vector when the iterator is not
// on an entry).
std::uint32_t __fastcall SkipIterValue_472ef0(std::uint32_t self, void*, std::uint32_t result) {
    const std::uint32_t node = Dword(self + kIterCurrent);
    auto* const out = reinterpret_cast<VectorBase*>(result);
    if (node == 0) {
        // (The library's error hook 0x59f200 here.) A default value: an empty
        // WCVectorBase<void*>(10, 5), copied.
        VectorBase empty;
        reinterpret_cast<VectorConstructFn>(kVoidVectorConstruct)(&empty, nullptr, kDefaultCapacity, kDefaultGrow);
        empty.vtable = vt::kWCPtrSortedVector_CExecuteType;
        reinterpret_cast<VectorConstructFn>(kSortedVoidVectorConstruct)(out, nullptr, 0, 0);
        out->vtable = vt::kWCPtrSortedVector_CExecuteType;
        reinterpret_cast<ThisArgFn>(kPtrVectorAssign)(result, nullptr, reinterpret_cast<std::uint32_t>(&empty));
        out->grow = empty.grow;
        out->length = out->data != 0 ? empty.length : 0;
        empty.vtable = vt::kWCVectorBase_voidp;
        reinterpret_cast<VectorDestroyFn>(kVoidVectorDestroy)(&empty, nullptr);
        return result;
    }
    const auto* const value = reinterpret_cast<const VectorBase*>(node - kRequestPairValue);
    reinterpret_cast<VectorConstructFn>(kVoidVectorConstruct)(out, nullptr, 0, 0);
    out->vtable = vt::kWCPtrSortedVector_CExecuteType;
    reinterpret_cast<ThisArgFn>(kPtrVectorAssign)(result, nullptr, reinterpret_cast<std::uint32_t>(value));
    out->grow = value->grow;
    out->length = out->data != 0 ? value->length : 0;
    return result;
}

// ---------------------------------------------------------------------------
// Tests: the dictionary's allocator is a test function that logs (size,
// EDX) and returns one fixed block (or null), so both runs build the node
// at the same address (operator new's path: the emulator specs); the pair
// is random (vectors of 0..6 elements, capacity -1..8, strings with or
// without a buffer, count bytes near the sharing limit). CLMMessageBlock's
// copy constructor and the skip-list base constructor are recording stubs.
// Compared: EAX, the node block (the vector blocks by contents, their
// addresses as null or not, strings as shared or not), the allocator log,
// the stub calls and the source strings' count bytes.
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kNodeBlock = 0x100;
std::uint8_t g_node[kNodeBlock];
bool g_allocFails = false;
std::vector<std::uint32_t> g_allocLog;

std::uint32_t __fastcall TestAllocate(std::uint32_t size, void* edx) {
    g_allocLog.push_back(size);
    g_allocLog.push_back(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(edx)));
    return g_allocFails ? 0 : reinterpret_cast<std::uint32_t>(g_node);
}

// The pair: a key and a vector of `filled` dwords or strings (fixed
// storage, rebuilt identically before each run, so both runs see the same
// addresses).
struct PairData {
    std::uint8_t bytes[0x40];
    std::uint32_t dwords[8];
    msvc6::String strings[8];
    char texts[8][16];
};
PairData g_pair;

void MakePair(test::Rng& rng, bool strings) {
    PairData& p = g_pair;
    rng.Fill(p.bytes, sizeof(p.bytes));
    auto* v = reinterpret_cast<VectorBase*>(p.bytes + 4);
    const std::int32_t filled = static_cast<std::int32_t>(rng.Below(7));
    const std::int32_t capacity = rng.Chance(1, 8) ? rng.Range(-1, 0) : filled + rng.Range(0, 2);
    v->capacity = static_cast<std::uint32_t>(capacity);
    v->filled = rng.Chance(1, 10) ? rng.Range(-1, filled) : filled;  // (never more than the capacity: a heap overflow)
    for (int k = 0; k < 8; ++k) {
        p.dwords[k] = rng.Next32();
        char* text = p.texts[k];
        const std::uint32_t len = rng.Below(12);
        text[0] = static_cast<char>(rng.Chance(1, 4) ? 0xfe + rng.Below(2) : rng.Below(3));  // the count byte
        for (std::uint32_t c = 0; c < len; ++c) {
            text[1 + c] = static_cast<char>('a' + rng.Below(26));
        }
        text[1 + len] = 0;
        msvc6::String& str = p.strings[k];
        str.allocator = static_cast<std::uint8_t>(rng.Next32());
        const bool buffer = rng.Chance(4, 5);
        str.ptr = buffer ? text + 1 : nullptr;
        str.len = buffer ? len : 0;
        str.res = str.len;
    }
    v->data = rng.Chance(1, 10) ? 0
                                : (strings ? reinterpret_cast<std::uint32_t>(p.strings)
                                           : reinterpret_cast<std::uint32_t>(p.dwords));
    if (v->data == 0) {
        v->filled = 0;  // (elements without a block: both fault, and the original's catch(...) then frees the node)
    }
}

struct NodeRun {
    std::uint32_t eax = 0;
    std::uint8_t node[kNodeBlock] = {};
    std::vector<std::uint32_t> log;
    std::vector<test::StubCall> calls;
    std::vector<std::uint32_t> vectorDwords;
    std::vector<std::string> vectorStrings;
    std::vector<std::uint8_t> counts;  // the source strings' count bytes afterwards
};

// (The copied strings are not freed: a shared buffer points into g_pair.)
NodeRun RunNode(NewNodeFn fn, const std::uint8_t* initialNode, std::int32_t level, std::uint32_t edx, bool strings) {
    NodeRun run;
    std::memcpy(g_node, initialNode, kNodeBlock);
    g_allocLog.clear();
    std::uint8_t dictionary[0x80] = {};
    Dword(reinterpret_cast<std::uint32_t>(dictionary) + kAllocator) = reinterpret_cast<std::uint32_t>(&TestAllocate);
    {
        test::CalleeStubs stubs;
        stubs.Install<1, kMessageBlockCopy>([](game::CGameData*, std::uint32_t, const std::uint32_t*) { return 0u; });
        stubs.SetWorld(reinterpret_cast<game::CGameData*>(g_node));
        run.eax = fn(reinterpret_cast<std::uint32_t>(dictionary), reinterpret_cast<void*>(static_cast<std::uintptr_t>(edx)),
                     reinterpret_cast<std::uint32_t>(g_pair.bytes), level);
        run.calls = stubs.Take();
    }
    run.log = g_allocLog;
    std::memcpy(run.node, g_node, kNodeBlock);
    if (run.eax != 0) {
        run.eax -= reinterpret_cast<std::uint32_t>(g_node);
        auto* v = reinterpret_cast<VectorBase*>(g_node + 4);
        if (v->data != 0 && run.eax == 0x20) {  // (0x472c70 / 0x459030: a vector copy at +4)
            for (std::int32_t k = 0; k < v->filled && k < 8; ++k) {
                if (strings) {
                    const auto& str = reinterpret_cast<const msvc6::String*>(v->data)[k];
                    const bool shared = str.ptr != nullptr && str.ptr == g_pair.strings[k].ptr;
                    run.vectorStrings.emplace_back(str.ptr != nullptr ? std::string(str.ptr, str.len) : "<null>");
                    run.vectorStrings.back() += shared ? "=" : "+";
                    run.vectorStrings.back() += static_cast<char>(str.allocator);
                    run.vectorStrings.back() += std::to_string(str.len) + "/" + std::to_string(str.res);
                } else {
                    run.vectorDwords.push_back(reinterpret_cast<const std::uint32_t*>(v->data)[k]);
                }
            }
            msvc6::GameOperatorDelete(reinterpret_cast<void*>(v->data));
            std::memset(run.node + 4 + offsetof(VectorBase, data), 1, 4);  // compared as "a block"
        }
    }
    for (const auto& text : g_pair.texts) {
        run.counts.push_back(static_cast<std::uint8_t>(text[0]));
    }
    return run;
}

struct NodePair {
    NewNodeFn original, ours;
    std::uint32_t address;
    bool strings;
    std::uint32_t compared;  // node bytes compared (the pair)
};

bool CompareNewNodes(test::Context& ctx, const NodePair& f, int cases) {
    for (int i = 0; i < cases; ++i) {
        std::uint8_t initialNode[kNodeBlock];
        ctx.rng().Fill(initialNode, sizeof(initialNode));
        const std::int32_t level = 1 + static_cast<std::int32_t>(ctx.rng().Below(12));
        g_allocFails = ctx.rng().Chance(1, 8);
        const std::uint32_t edx = ctx.rng().Next32();
        test::Rng pairRng = ctx.rng();
        ctx.rng().Next32();
        test::Rng again = pairRng;
        MakePair(pairRng, f.strings);
        const NodeRun ra = RunNode(f.original, initialNode, level, edx, f.strings);
        MakePair(again, f.strings);
        const NodeRun rb = RunNode(f.ours, initialNode, level, edx, f.strings);
        if (!S3RE_EXPECT_EQ(ra.eax, rb.eax) || !S3RE_EXPECT_MEM_EQ(ra.node, rb.node, f.compared) ||
            !S3RE_EXPECT(ra.log == rb.log) || !S3RE_EXPECT(ra.calls == rb.calls) ||
            !S3RE_EXPECT(ra.vectorDwords == rb.vectorDwords) || !S3RE_EXPECT(ra.vectorStrings == rb.vectorStrings) ||
            !S3RE_EXPECT(ra.counts == rb.counts)) {
            ctx.Fail(__FILE__, __LINE__, "  0x%x case %d, level %d", f.address, i, level);
            return false;
        }
    }
    return true;
}

const NodePair kNodePairs[] = {
    {Original_SkipNewNode_472e60, &SkipNewNode_472e60, 0x472e60, false, 0x0c},
    {Original_SkipNewNode_472c70, &SkipNewNode_472c70, 0x472c70, false, 0x20},
    {Original_SkipNewNode_60a6d0, &SkipNewNode_60a6d0, 0x60a6d0, false, 0x04},
    {Original_SkipNewNode_459030, &SkipNewNode_459030, 0x459030, true, 0x20},
};

// Wrong: the request vector's length not copied.
std::uint32_t __fastcall WrongNewNode(std::uint32_t self, void* edx, std::uint32_t pair, std::int32_t level) {
    const std::uint32_t node = SkipNewNode_472c70(self, edx, pair, level);
    if (node != 0) {
        reinterpret_cast<VectorBase*>(node - 0x20 + 4)->length = 0;
    }
    return node;
}

bool CompareDictConstructor(test::Context& ctx, DictConstructFn ours, int cases) {
    constexpr std::size_t kDict = 0x98;
    for (int i = 0; i < cases; ++i) {
        std::uint8_t initial[kDict];
        ctx.rng().Fill(initial, sizeof(initial));
        const std::uint32_t a = ctx.rng().Next32(), b = ctx.rng().Next32();
        auto run = [&](DictConstructFn fn, std::uint8_t* object, std::vector<test::StubCall>& calls) {
            std::memcpy(object, initial, kDict);
            test::CalleeStubs stubs;
            stubs.Install<3, kSkipBaseConstruct>(
                [](game::CGameData*, std::uint32_t self, const std::uint32_t*) { return self; });
            stubs.SetWorld(reinterpret_cast<game::CGameData*>(object));
            const std::uint32_t eax = fn(reinterpret_cast<std::uint32_t>(object), nullptr, a, b) -
                                      reinterpret_cast<std::uint32_t>(object);
            calls = stubs.Take();
            auto* v = reinterpret_cast<VectorBase*>(object + kDictVector);
            msvc6::GameOperatorDelete(reinterpret_cast<void*>(v->data));
            v->data = v->data != 0 ? 1 : 0;
            return eax;
        };
        std::uint8_t oa[kDict], ob[kDict];
        std::vector<test::StubCall> ca, cb;
        const std::uint32_t ea = run(Original_SkipDictConstructor_457c10, oa, ca);
        const std::uint32_t eb = run(ours, ob, cb);
        if (!S3RE_EXPECT_EQ(ea, eb) || !S3RE_EXPECT_MEM_EQ(oa, ob, kDict) || !S3RE_EXPECT(ca == cb)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return false;
        }
    }
    return true;
}

// Wrong: the vector's growth step 10.
std::uint32_t __fastcall WrongDictConstructor(std::uint32_t self, void* edx, std::uint32_t a, std::uint32_t b) {
    SkipDictConstructor_457c10(self, edx, a, b);
    reinterpret_cast<VectorBase*>(self + kDictVector)->grow = 10;
    return self;
}

// The message-block dictionary's copy constructor: its callees stubbed
// (the base constructor returns `this`), the source fixed.
bool CompareDictCopy(test::Context& ctx, OneArgFn ours, int cases) {
    constexpr std::size_t kDict = 0xa4;
    std::uint8_t from[kDict];
    for (int i = 0; i < cases; ++i) {
        std::uint8_t initial[kDict];
        ctx.rng().Fill(initial, sizeof(initial));
        ctx.rng().Fill(from, sizeof(from));
        auto run = [&](OneArgFn fn, std::uint8_t* object, std::vector<test::StubCall>& calls) {
            std::memcpy(object, initial, kDict);
            test::CalleeStubs stubs;
            const auto self = [](game::CGameData*, std::uint32_t self, const std::uint32_t*) { return self; };
            stubs.Install<3, kSkipBaseConstruct>(self);
            stubs.Install<2, kMessageBlockConstruct>(self);
            stubs.Install<1, kSkipBaseAssign>(self);
            stubs.SetWorld(reinterpret_cast<game::CGameData*>(object));
            const std::uint32_t eax = fn(reinterpret_cast<std::uint32_t>(object), nullptr,
                                         reinterpret_cast<std::uint32_t>(from)) -
                                      reinterpret_cast<std::uint32_t>(object);
            calls = stubs.Take();
            return eax;
        };
        std::uint8_t oa[kDict], ob[kDict];
        std::vector<test::StubCall> ca, cb;
        const std::uint32_t ea = run(Original_SkipDictCopy_609a20, oa, ca);
        const std::uint32_t eb = run(ours, ob, cb);
        if (!S3RE_EXPECT_EQ(ea, eb) || !S3RE_EXPECT_MEM_EQ(oa, ob, kDict) || !S3RE_EXPECT(ca == cb)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return false;
        }
    }
    return true;
}

// Wrong: the base constructor gets the probability twice.
std::uint32_t __fastcall WrongDictCopy(std::uint32_t self, void* edx, std::uint32_t from) {
    Dword(from + kSkipLevels) = Dword(from + kSkipProbability);
    return SkipDictCopy_609a20(self, edx, from);
}

// A source vector of 0..8 pointers (capacity -1..9, filled <= capacity,
// sometimes no block) for base_copy and the iterator's value().
struct CopySource {
    std::uint8_t pair[0x20];  // key, then the vector
    std::uint32_t block[9];
};
CopySource g_source;

VectorBase* MakeSource(test::Rng& rng) {
    rng.Fill(&g_source, sizeof(g_source));
    auto* v = reinterpret_cast<VectorBase*>(g_source.pair + 4);
    const std::int32_t capacity = rng.Chance(1, 8) ? rng.Range(-1, 0) : rng.Range(1, 9);
    v->capacity = static_cast<std::uint32_t>(capacity);
    v->filled = capacity > 0 ? rng.Range(-1, capacity) : rng.Range(-1, 2);
    v->data = rng.Chance(1, 10) && capacity <= 0 ? 0 : reinterpret_cast<std::uint32_t>(g_source.block);
    return v;
}

// The result vector's bytes with its block replaced by its contents.
std::vector<std::uint32_t> Flatten(VectorBase& v) {
    std::vector<std::uint32_t> out(reinterpret_cast<const std::uint32_t*>(&v),
                                   reinterpret_cast<const std::uint32_t*>(&v) + sizeof(VectorBase) / 4);
    out[2] = v.data != 0 ? 1 : 0;
    if (v.data != 0) {
        for (std::int32_t k = 0; k < v.filled && k < 9; ++k) {
            out.push_back(Dword(v.data + 4u * static_cast<std::uint32_t>(k)));
        }
        msvc6::GameOperatorDelete(reinterpret_cast<void*>(v.data));
    }
    return out;
}

bool CompareBaseCopy(test::Context& ctx, BaseCopyFn ours, int cases) {
    for (int i = 0; i < cases; ++i) {
        VectorBase initial;
        ctx.rng().Fill(&initial, sizeof(initial));
        const VectorBase* from = MakeSource(ctx.rng());
        VectorBase a = initial, b = initial;
        Original_VectorBaseCopy_4732f0(&a, nullptr, from);
        ours(&b, nullptr, from);
        if (!S3RE_EXPECT(Flatten(a) == Flatten(b))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: capacity %d, filled %d", i, from->capacity, from->filled);
            return false;
        }
    }
    return true;
}

bool CompareIterValue(test::Context& ctx, OneArgFn ours, int cases) {
    for (int i = 0; i < cases; ++i) {
        std::uint8_t iterator[0x20];
        ctx.rng().Fill(iterator, sizeof(iterator));
        MakeSource(ctx.rng());
        Dword(reinterpret_cast<std::uint32_t>(iterator) + kIterCurrent) =
            ctx.rng().Chance(1, 4) ? 0 : reinterpret_cast<std::uint32_t>(g_source.pair) + 0x20;
        VectorBase initial;
        ctx.rng().Fill(&initial, sizeof(initial));
        VectorBase a = initial, b = initial;
        const std::uint32_t ea = Original_SkipIterValue_472ef0(reinterpret_cast<std::uint32_t>(iterator), nullptr,
                                                               reinterpret_cast<std::uint32_t>(&a));
        const std::uint32_t eb = ours(reinterpret_cast<std::uint32_t>(iterator), nullptr,
                                      reinterpret_cast<std::uint32_t>(&b));
        if (!S3RE_EXPECT(ea == reinterpret_cast<std::uint32_t>(&a) && eb == reinterpret_cast<std::uint32_t>(&b)) ||
            !S3RE_EXPECT(Flatten(a) == Flatten(b))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return false;
        }
    }
    return true;
}

// Wrong: base_copy copies one element less.
void __fastcall WrongBaseCopy(VectorBase* self, void* edx, const VectorBase* from) {
    VectorBaseCopy_4732f0(self, edx, from);
    if (self->data != 0 && self->filled > 0) {
        Dword(self->data + 4u * static_cast<std::uint32_t>(self->filled - 1)) = 0;
    }
}

// Wrong: value() leaves the length at 0.
std::uint32_t __fastcall WrongIterValue(std::uint32_t self, void* edx, std::uint32_t result) {
    SkipIterValue_472ef0(self, edx, result);
    reinterpret_cast<VectorBase*>(result)->length = 0;
    return result;
}

}  // namespace

S3RE_TEST(WatcomSkipNewNodes2_MatchOriginal) {
    for (const NodePair& pair : kNodePairs) {
        if (!CompareNewNodes(ctx, pair, 1500)) {
            return;
        }
    }
    CompareDictConstructor(ctx, &SkipDictConstructor_457c10, 300);
    CompareDictCopy(ctx, &SkipDictCopy_609a20, 300);
    CompareBaseCopy(ctx, &VectorBaseCopy_4732f0, 2000);
    CompareIterValue(ctx, &SkipIterValue_472ef0, 2000);
}

S3RE_TEST(WatcomSkipNewNodes2_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareNewNodes(probe, {Original_SkipNewNode_472c70, &WrongNewNode, 0x472c70, false, 0x20}, 1500);
    S3RE_EXPECT(probe.failures() > 0);
    test::Context probe2(ctx.name(), 1, /*quiet=*/true);
    CompareDictConstructor(probe2, &WrongDictConstructor, 300);
    S3RE_EXPECT(probe2.failures() > 0);
    test::Context probe3(ctx.name(), 1, /*quiet=*/true);
    CompareDictCopy(probe3, &WrongDictCopy, 300);
    S3RE_EXPECT(probe3.failures() > 0);
    test::Context probe4(ctx.name(), 1, /*quiet=*/true);
    CompareBaseCopy(probe4, &WrongBaseCopy, 2000);
    S3RE_EXPECT(probe4.failures() > 0);
    test::Context probe5(ctx.name(), 1, /*quiet=*/true);
    CompareIterValue(probe5, &WrongIterValue, 2000);
    S3RE_EXPECT(probe5.failures() > 0);
}

}  // namespace s3re::watcom
