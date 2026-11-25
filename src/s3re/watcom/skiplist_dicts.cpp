// Destructors of the sound code's Watcom skip-list dictionaries (template
// instantiations; only the vtables differ) and the effects iterator's
// key/value accessor. thiscall; the callers ignore the destructors' EAX
// (ours returns what the original leaves: clear's result).
//
// Without a key/value member to destroy (WCSkipListBase<...> and the
// WCValSkipListDicts whose pair needs no destructor): vtable = V; if the
// count (+0x74) is not 0, the library's empty error hook 0x59f200 ("not
// empty"); clear (0x60bd80); vtable = WCExcept (0x655438).
//   0x005c3450 WCSkipListBase<KeyVal<int, CSoundEffectBuffer>>            0x65cf70
//   0x005ffa60 WCValSkipListDict<unsigned int, CSoundEffect*>             0x65d438 (the base's vtable)
//   0x005ffb60 WCSkipListBase<KeyVal<unsigned int, CSoundEffect*>>        0x65d438
//   0x005ffac0 WCValSkipListDict<unsigned int, unsigned int>              0x65d454 (the base's vtable)
//   0x005ffc00 WCSkipListBase<KeyVal<unsigned int, unsigned int>>         0x65d454
// WCValSkipListDict<int, DirectX::CSoundEffectBuffer> (0x005c33e0): vtable
//   0x65cf54, ~CDirectSoundBuffer (0x5c1fe0) of the key/value pair's buffer
//   (+0x7c), then as above with 0x65cf70.
// WCValSkipListDictIter<unsigned int, CSoundEffect*>'s current key/value
//   pair (0x005ffc80): the node (+0xc) - 8, or 0 without a node.

#include "game/vtables.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::watcom {

namespace vt = game::vtables;

namespace {

constexpr std::uintptr_t kDictClear = 0x0060bd80;      // WCSkipNonTempBase::clear
constexpr std::uintptr_t kNoOp = 0x0059f200;           // the library's empty error hook
constexpr std::uintptr_t kBufferDestroy = 0x005c1fe0;  // DirectX::CDirectSoundBuffer::~CDirectSoundBuffer
constexpr std::uint32_t kCount = 0x74;                 // the dictionary's node count
constexpr std::uint32_t kPairBuffer = 0x7c;            // the key/value pair's CSoundEffectBuffer
constexpr std::uint32_t kIterNode = 0x0c;
constexpr std::uint32_t kNodePair = 8;                 // key and value sit before a node's forward array

using ThisFn = std::uint32_t(__fastcall*)(std::uint32_t self, void* edx);

std::uint32_t& Field(std::uint32_t self, std::uint32_t offset) {
    return *reinterpret_cast<std::uint32_t*>(self + offset);
}

std::uint32_t DestroySkipList(std::uint32_t self, std::uint32_t vtable) {
    Field(self, 0) = vtable;
    if (Field(self, kCount) != 0) {
        reinterpret_cast<ThisFn>(kNoOp)(self, nullptr);
    }
    const std::uint32_t result = reinterpret_cast<ThisFn>(kDictClear)(self, nullptr);
    Field(self, 0) = vt::kWCExcept;
    return result;
}

}  // namespace

using SkipListDestroyFn = std::uint32_t(__fastcall*)(std::uint32_t self, void* edx);

// Purpose: destructors of the sound effects' and the sound data's
// dictionaries (delete every node).
std::uint32_t __fastcall SkipListDestructor_5c3450(std::uint32_t self, void*) {
    return DestroySkipList(self, vt::kEffectBufferSkipListBase);
}
std::uint32_t __fastcall SkipListDestructor_5ffa60(std::uint32_t self, void*) {
    return DestroySkipList(self, vt::kSoundEffectSkipListBase);
}
std::uint32_t __fastcall SkipListDestructor_5ffb60(std::uint32_t self, void*) {
    return DestroySkipList(self, vt::kSoundEffectSkipListBase);
}
std::uint32_t __fastcall SkipListDestructor_5ffac0(std::uint32_t self, void*) {
    return DestroySkipList(self, vt::kSoundUseSkipListBase);
}
std::uint32_t __fastcall SkipListDestructor_5ffc00(std::uint32_t self, void*) {
    return DestroySkipList(self, vt::kSoundUseSkipListBase);
}

// Purpose: destructor of an effect's dictionary of duplicate buffers.
std::uint32_t __fastcall SkipListDestructor_5c33e0(std::uint32_t self, void*) {
    Field(self, 0) = vt::kEffectBufferDict;
    reinterpret_cast<ThisFn>(kBufferDestroy)(self + kPairBuffer, nullptr);
    return DestroySkipList(self, vt::kEffectBufferSkipListBase);
}

using IterPairFn = std::uint32_t(__fastcall*)(std::uint32_t self, void* edx);

// Purpose: the key/value pair at the sound data's effect iterator.
std::uint32_t __fastcall SoundEffectDictIter_KeyValue(std::uint32_t self, void*) {
    const std::uint32_t node = Field(self, kIterNode);
    return node != 0 ? node - kNodePair : 0;
}

S3RE_HOOK(0x005c3450, SkipListDestructor_5c3450, SkipListDestroyFn, Original_SkipListDestructor_5c3450,
          SkipListDestructor_5c3450);
S3RE_HOOK(0x005ffa60, SkipListDestructor_5ffa60, SkipListDestroyFn, Original_SkipListDestructor_5ffa60,
          SkipListDestructor_5ffa60);
S3RE_HOOK(0x005ffb60, SkipListDestructor_5ffb60, SkipListDestroyFn, Original_SkipListDestructor_5ffb60,
          SkipListDestructor_5ffb60);
S3RE_HOOK(0x005ffac0, SkipListDestructor_5ffac0, SkipListDestroyFn, Original_SkipListDestructor_5ffac0,
          SkipListDestructor_5ffac0);
S3RE_HOOK(0x005ffc00, SkipListDestructor_5ffc00, SkipListDestroyFn, Original_SkipListDestructor_5ffc00,
          SkipListDestructor_5ffc00);
S3RE_HOOK(0x005c33e0, SkipListDestructor_5c33e0, SkipListDestroyFn, Original_SkipListDestructor_5c33e0,
          SkipListDestructor_5c33e0);
S3RE_HOOK(0x005ffc80, SoundEffectDictIter_KeyValue, IterPairFn, Original_SoundEffectDictIter_KeyValue,
          SoundEffectDictIter_KeyValue);

// ---------------------------------------------------------------------------
// Tests: each destructor on a random 0x90-byte object with clear, the error
// hook and ~CDirectSoundBuffer replaced by recording stubs (clear returns a
// value fixed per case); compared: the calls, EAX and the object bytes.
// The key/value accessor on iterators with random or null nodes.
// ---------------------------------------------------------------------------

namespace {

struct DestructorCase {
    SkipListDestroyFn original;
    SkipListDestroyFn ours;
};

const DestructorCase kDestructorCases[] = {
    {Original_SkipListDestructor_5c3450, &SkipListDestructor_5c3450},
    {Original_SkipListDestructor_5ffa60, &SkipListDestructor_5ffa60},
    {Original_SkipListDestructor_5ffb60, &SkipListDestructor_5ffb60},
    {Original_SkipListDestructor_5ffac0, &SkipListDestructor_5ffac0},
    {Original_SkipListDestructor_5ffc00, &SkipListDestructor_5ffc00},
    {Original_SkipListDestructor_5c33e0, &SkipListDestructor_5c33e0},
};

std::uint32_t g_clearResult = 0;

bool CompareDestructor(test::Context& ctx, const DestructorCase& c, int cases) {
    constexpr std::size_t kBytes = 0x90;
    for (int i = 0; i < cases; ++i) {
        std::uint8_t initial[kBytes];
        ctx.rng().Fill(initial, sizeof(initial));
        if (ctx.rng().Chance(1, 2)) {
            std::memset(initial + kCount, 0, 4);
        }
        g_clearResult = ctx.rng().Next32();
        std::uint8_t a[kBytes], b[kBytes];
        std::memcpy(a, initial, kBytes);
        std::memcpy(b, initial, kBytes);
        std::uint32_t ra = 0, rb = 0;
        std::vector<test::StubCall> callsA, callsB;
        {
            test::CalleeStubs stubs;
            stubs.Install<0, kDictClear>([](game::CGameData*, std::uint32_t, const std::uint32_t*) {
                return g_clearResult;
            });
            stubs.Install<0, kNoOp>();
            stubs.Install<0, kBufferDestroy>();
            stubs.SetWorld(reinterpret_cast<game::CGameData*>(a));
            ra = c.original(reinterpret_cast<std::uint32_t>(a), nullptr);
            callsA = stubs.Take();
            stubs.SetWorld(reinterpret_cast<game::CGameData*>(b));
            rb = c.ours(reinterpret_cast<std::uint32_t>(b), nullptr);
            callsB = stubs.Take();
        }
        if (!S3RE_EXPECT_EQ(ra, rb) || !S3RE_EXPECT(callsA == callsB) || !S3RE_EXPECT_MEM_EQ(a, b, kBytes)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return false;
        }
    }
    return true;
}

bool CompareKeyValue(test::Context& ctx, IterPairFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        std::uint32_t iter[5];
        ctx.rng().Fill(iter, sizeof(iter));
        if (ctx.rng().Chance(1, 3)) {
            iter[3] = 0;
        }
        const auto self = reinterpret_cast<std::uint32_t>(iter);
        if (!S3RE_EXPECT_EQ(Original_SoundEffectDictIter_KeyValue(self, nullptr), candidate(self, nullptr))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return false;
        }
    }
    return true;
}

// Wrong: the count not checked / the pair 4 bytes off.
std::uint32_t __fastcall WrongDestructor(std::uint32_t self, void*) {
    Field(self, 0) = vt::kSoundUseSkipListBase;
    const std::uint32_t result = reinterpret_cast<ThisFn>(kDictClear)(self, nullptr);
    Field(self, 0) = vt::kWCExcept;
    return result;
}
std::uint32_t __fastcall WrongKeyValue(std::uint32_t self, void*) {
    const std::uint32_t node = Field(self, kIterNode);
    return node != 0 ? node - 4 : 0;
}

}  // namespace

S3RE_TEST(SoundSkipLists_MatchOriginal) {
    for (const DestructorCase& c : kDestructorCases) {
        if (!CompareDestructor(ctx, c, 200)) {
            return;
        }
    }
    CompareKeyValue(ctx, &SoundEffectDictIter_KeyValue, 1000);
}

S3RE_TEST(SoundSkipLists_ComparisonDetectsWrongImplementation) {
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareDestructor(probe, {Original_SkipListDestructor_5ffac0, &WrongDestructor}, 100);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 2, /*quiet=*/true);
        CompareKeyValue(probe, &WrongKeyValue, 100);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::watcom
