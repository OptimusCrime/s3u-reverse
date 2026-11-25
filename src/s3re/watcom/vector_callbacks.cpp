// The comparison callbacks of Watcom value vectors (WCValOrderedVector's
// base_equivalent, WCValSortedVector's base_less_than: static functions
// in the vtables). Each calls the element type's operator through its
// address and returns its AL zero-extended. stdcall (a*, b*) ret 8:
//   0x00437530 WCValOrderedVector<Dirk::CPlayer> slot 2: a->operator==(b)
//              (GamePlayerSetup::operator== 0x49f100)
//   0x00437550 WCValSortedVector<Dirk::CTeam> slot 4: a->operator<(b)
//              (Dirk::CTeam::operator< 0x4a05a0)
//   0x0044c4a0 a lobby vector's slot 2: Lobby::EqualField4 0x606ac0
// Callbacks with the element comparison inlined (EAX = 0 or 1):
//   0x00462850 WCValOrderedVector<unsigned short> slot 2: *a == *b (16 bits)
//   0x0044c910 WCValSortedVector<String> slot 4: a < b (the strings' bytes
//              compared unsigned up to the shorter length, then the
//              lengths; b's null buffer reads as the empty string 0x6553a0)
//   0x00499050 WCValOrderedVector<GameChunkDescription> slot 2: equal when
//              +0x14, +0x1c, the names at +0x20 (strncmp 0x6213d0, 256
//              characters), the words +0 and +2 and the dword +4 are (in
//              that order of tests)
// The skip-list dictionaries' key callbacks get a pointer to a node's
// value; its key sits before it:
//   0x00457a70 WCValSkipListDict<unsigned int, String> slot 2: key at a-0x14 < *b (unsigned)
//   0x004a6b20 WCValSkipListDict<unsigned int, ...> base_less_than: key at a-8 < *b (unsigned)
//   0x00428100 WCValSkipListDict<int, ...> base_less_than: key at a-8 < *b (signed)
//   0x004a6b00 WCValSkipListDict<unsigned int, ...> base_equivalent: key at a-8 == *b

#include "hooks.h"
#include "msvc6/string.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace s3re::watcom {

namespace {

using MemberCompareFn = std::uint32_t(__fastcall*)(std::uint32_t self, void* edx, std::uint32_t other);

std::uint32_t CompareThrough(std::uintptr_t member, std::uint32_t a, std::uint32_t b) {
    return reinterpret_cast<MemberCompareFn>(member)(a, nullptr, b) & 0xFF;
}

constexpr std::uintptr_t kPlayerEqual = 0x0049f100;  // GamePlayerSetup::operator== (ours)
constexpr std::uintptr_t kTeamLess = 0x004a05a0;     // Dirk::CTeam::operator< (ours)
constexpr std::uintptr_t kLobbyEqual = 0x00606ac0;   // Lobby::EqualField4 (ours)

}  // namespace

using VectorCallbackFn = std::uint32_t(__stdcall*)(std::uint32_t a, std::uint32_t b);

// Purpose: Equality callback of the vector of player setups.
std::uint32_t __stdcall PlayerVectorEquivalent(std::uint32_t a, std::uint32_t b) {
    return CompareThrough(kPlayerEqual, a, b);
}

// Purpose: Ordering callback of the sorted vector of teams.
std::uint32_t __stdcall TeamVectorLessThan(std::uint32_t a, std::uint32_t b) {
    return CompareThrough(kTeamLess, a, b);
}

// Purpose: Equality callback of a lobby vector.
std::uint32_t __stdcall LobbyVectorEquivalent(std::uint32_t a, std::uint32_t b) {
    return CompareThrough(kLobbyEqual, a, b);
}

namespace {

template <typename T>
T ReadAt(std::uint32_t address) {
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}

using StrncmpFn = int(__cdecl*)(const char* a, const char* b, std::uint32_t count);
constexpr std::uintptr_t kStrncmp = 0x006213d0;  // the CRT's strncmp

#pragma pack(push, 1)
struct GameChunkDescription {  // 0x120 bytes (only the compared fields named)
    std::uint16_t word00;
    std::uint16_t word02;
    std::uint32_t dword04;
    std::uint8_t unk08[0x14 - 0x08];
    std::uint32_t dword14;
    std::uint8_t unk18[4];
    std::uint32_t dword1c;
    char name[0x100];  // +0x20
};
#pragma pack(pop)
static_assert(sizeof(GameChunkDescription) == 0x120);

}  // namespace

// Purpose: Equality callback of a vector of 16-bit values.
std::uint32_t __stdcall UShortVectorEquivalent(std::uint32_t a, std::uint32_t b) {
    return ReadAt<std::uint16_t>(a) == ReadAt<std::uint16_t>(b) ? 1 : 0;
}

// Purpose: Ordering callback of a sorted vector of strings (byte-wise, the
// shorter first on a tie).
std::uint32_t __stdcall StringVectorLessThan(std::uint32_t a, std::uint32_t b) {
    const auto* left = reinterpret_cast<const msvc6::String*>(a);
    const auto* right = reinterpret_cast<const msvc6::String*>(b);
    const char* rightText = msvc6::detail::CStr(right);
    const std::uint32_t common = left->len < right->len ? left->len : right->len;
    for (std::uint32_t i = 0; i < common; ++i) {
        const auto l = static_cast<std::uint8_t>(left->ptr[i]);
        const auto r = static_cast<std::uint8_t>(rightText[i]);
        if (l != r) {
            return l < r ? 1 : 0;
        }
    }
    return left->len < right->len ? 1 : 0;
}

// Purpose: Equality callback of the map file's vector of chunk descriptions.
std::uint32_t __stdcall ChunkDescriptionEquivalent(std::uint32_t a, std::uint32_t b) {
    const auto* left = reinterpret_cast<const GameChunkDescription*>(a);
    const auto* right = reinterpret_cast<const GameChunkDescription*>(b);
    const bool same = left->dword14 == right->dword14 && left->dword1c == right->dword1c &&
                      reinterpret_cast<StrncmpFn>(kStrncmp)(left->name, right->name, sizeof(left->name)) == 0 &&
                      left->word00 == right->word00 && left->word02 == right->word02 &&
                      left->dword04 == right->dword04;
    return same ? 1 : 0;
}

// Purpose: Key comparisons of the skip-list dictionaries (the menus'
// string tables, the map file's chunk cache): the node's key against a key.
std::uint32_t __stdcall SkipDictKeyLess_457a70(std::uint32_t node, std::uint32_t key) {
    return ReadAt<std::uint32_t>(node - 0x14) < ReadAt<std::uint32_t>(key) ? 1 : 0;
}
std::uint32_t __stdcall SkipDictKeyLess_4a6b20(std::uint32_t node, std::uint32_t key) {
    return ReadAt<std::uint32_t>(node - 8) < ReadAt<std::uint32_t>(key) ? 1 : 0;
}
std::uint32_t __stdcall SkipDictKeyLess_428100(std::uint32_t node, std::uint32_t key) {
    return ReadAt<std::int32_t>(node - 8) < ReadAt<std::int32_t>(key) ? 1 : 0;
}
std::uint32_t __stdcall SkipDictKeyEqual_4a6b00(std::uint32_t node, std::uint32_t key) {
    return ReadAt<std::uint32_t>(node - 8) == ReadAt<std::uint32_t>(key) ? 1 : 0;
}

S3RE_HOOK(0x00462850, UShortVectorEquivalent, VectorCallbackFn, Original_UShortVectorEquivalent,
          UShortVectorEquivalent);
S3RE_HOOK(0x0044c910, StringVectorLessThan, VectorCallbackFn, Original_StringVectorLessThan, StringVectorLessThan);
S3RE_HOOK(0x00499050, ChunkDescriptionEquivalent, VectorCallbackFn, Original_ChunkDescriptionEquivalent,
          ChunkDescriptionEquivalent);
S3RE_HOOK(0x00457a70, SkipDictKeyLess_457a70, VectorCallbackFn, Original_SkipDictKeyLess_457a70,
          SkipDictKeyLess_457a70);
S3RE_HOOK(0x004a6b20, SkipDictKeyLess_4a6b20, VectorCallbackFn, Original_SkipDictKeyLess_4a6b20,
          SkipDictKeyLess_4a6b20);
S3RE_HOOK(0x00428100, SkipDictKeyLess_428100, VectorCallbackFn, Original_SkipDictKeyLess_428100,
          SkipDictKeyLess_428100);
S3RE_HOOK(0x004a6b00, SkipDictKeyEqual_4a6b00, VectorCallbackFn, Original_SkipDictKeyEqual_4a6b00,
          SkipDictKeyEqual_4a6b00);
S3RE_HOOK(0x00437530, PlayerVectorEquivalent, VectorCallbackFn, Original_PlayerVectorEquivalent,
          PlayerVectorEquivalent);
S3RE_HOOK(0x00437550, TeamVectorLessThan, VectorCallbackFn, Original_TeamVectorLessThan, TeamVectorLessThan);
S3RE_HOOK(0x0044c4a0, LobbyVectorEquivalent, VectorCallbackFn, Original_LobbyVectorEquivalent,
          LobbyVectorEquivalent);

// ---------------------------------------------------------------------------
// Tests: the operator replaced by a recording stub that returns a random
// 32-bit value (fixed per case); compared: the calls and EAX.
// ---------------------------------------------------------------------------

namespace {

std::uint32_t g_stubResult = 0;

template <std::uintptr_t Member>
bool CompareCallback(test::Context& ctx, VectorCallbackFn original, VectorCallbackFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t a = ctx.rng().Next32();
        const std::uint32_t b = ctx.rng().Next32();
        g_stubResult = ctx.rng().Next32();
        std::uint32_t ra = 0, rb = 0;
        std::vector<test::StubCall> callsA, callsB;
        {
            test::CalleeStubs stubs;
            stubs.Install<1, Member>([](game::CGameData*, std::uint32_t, const std::uint32_t*) { return g_stubResult; });
            ra = original(a, b);
            callsA = stubs.Take();
            rb = candidate(a, b);
            callsB = stubs.Take();
        }
        if (!S3RE_EXPECT_EQ(ra, rb) || !S3RE_EXPECT(callsA == callsB)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return false;
        }
    }
    return true;
}

// Wrong: the operands swapped.
std::uint32_t __stdcall WrongTeamLessThan(std::uint32_t a, std::uint32_t b) {
    return CompareThrough(kTeamLess, b, a);
}

// The inline callbacks on two random 0x140-byte buffers (often alike):
// keys/values, strings (a length up to 12, buffers sometimes null or
// sharing a prefix), chunk descriptions with names alike or not.
struct InlineCase {
    VectorCallbackFn original;
    VectorCallbackFn ours;
};

const InlineCase kInlineCases[] = {
    {Original_UShortVectorEquivalent, &UShortVectorEquivalent},
    {Original_StringVectorLessThan, &StringVectorLessThan},
    {Original_ChunkDescriptionEquivalent, &ChunkDescriptionEquivalent},
    {Original_SkipDictKeyLess_457a70, &SkipDictKeyLess_457a70},
    {Original_SkipDictKeyLess_4a6b20, &SkipDictKeyLess_4a6b20},
    {Original_SkipDictKeyLess_428100, &SkipDictKeyLess_428100},
    {Original_SkipDictKeyEqual_4a6b00, &SkipDictKeyEqual_4a6b00},
};

constexpr std::size_t kBuffer = 0x160;
constexpr std::size_t kKeyOffset = 0x20;  // a and b point this far into their buffers

void PutString(test::Rng& rng, std::uint8_t* buffer, char* text, const char* other) {
    const std::uint32_t len = rng.Below(13);
    static constexpr char kLetters[] = {'a', 'b', static_cast<char>(0x80), static_cast<char>(0xff)};
    for (std::uint32_t k = 0; k < len; ++k) {
        text[k] = other != nullptr && rng.Chance(3, 4) ? other[k] : kLetters[rng.Below(4)];
    }
    text[len] = 0;
    const bool none = len == 0 && rng.Chance(1, 2);
    const std::uint32_t ptr = none ? 0 : reinterpret_cast<std::uint32_t>(text);
    std::memcpy(buffer + kKeyOffset + 4, &ptr, 4);
    std::memcpy(buffer + kKeyOffset + 8, &len, 4);
}

bool CompareInline(test::Context& ctx, const InlineCase& c, int cases) {
    std::uint8_t a[kBuffer], b[kBuffer];
    char textA[16], textB[16];
    for (int i = 0; i < cases; ++i) {
        test::Rng& rng = ctx.rng();
        rng.Fill(a, sizeof(a));
        // b: a copy with a few bytes changed (or all random).
        std::memcpy(b, a, sizeof(b));
        if (rng.Chance(1, 4)) {
            rng.Fill(b, sizeof(b));
        } else {
            for (std::uint32_t k = rng.Below(3); k > 0; --k) {
                b[rng.Below(sizeof(b))] = static_cast<std::uint8_t>(rng.Next32());
            }
            if (rng.Chance(1, 3)) {
                b[kKeyOffset - 0x14 + rng.Below(0x18)] ^= 1;  // near the keys
            }
        }
        // Strings (allocator, ptr, len, res) at the key offset (12 random
        // letters each; b often repeats a's).
        if (c.ours == &StringVectorLessThan) {
            std::memset(textA, 0, sizeof(textA));
            PutString(rng, a, textA, nullptr);
            PutString(rng, b, textB, textA);
        }
        const auto pa = reinterpret_cast<std::uint32_t>(a + kKeyOffset);
        const auto pb = reinterpret_cast<std::uint32_t>(b + kKeyOffset);
        if (!S3RE_EXPECT_EQ(c.original(pa, pb), c.ours(pa, pb))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return false;
        }
    }
    return true;
}

// Wrong: signed instead of unsigned.
std::uint32_t __stdcall WrongKeyLess(std::uint32_t node, std::uint32_t key) {
    return ReadAt<std::int32_t>(node - 8) < ReadAt<std::int32_t>(key) ? 1 : 0;
}

}  // namespace

S3RE_TEST(WatcomInlineCallbacks_MatchOriginal) {
    for (const InlineCase& c : kInlineCases) {
        if (!CompareInline(ctx, c, 3000)) {
            return;
        }
    }
}

S3RE_TEST(WatcomInlineCallbacks_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareInline(probe, {Original_SkipDictKeyLess_4a6b20, &WrongKeyLess}, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(WatcomVectorCallbacks_MatchOriginal) {
    CompareCallback<kPlayerEqual>(ctx, Original_PlayerVectorEquivalent, &PlayerVectorEquivalent, 300) &&
        CompareCallback<kTeamLess>(ctx, Original_TeamVectorLessThan, &TeamVectorLessThan, 300) &&
        CompareCallback<kLobbyEqual>(ctx, Original_LobbyVectorEquivalent, &LobbyVectorEquivalent, 300);
}

S3RE_TEST(WatcomVectorCallbacks_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareCallback<kTeamLess>(probe, Original_TeamVectorLessThan, &WrongTeamLessThan, 20);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::watcom
