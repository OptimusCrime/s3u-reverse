// The comparison callbacks of Watcom value vectors (WCValOrderedVector's
// base_equivalent, WCValSortedVector's base_less_than: static functions
// in the vtables). Each calls the element type's operator through its
// address and returns its AL zero-extended. stdcall (a*, b*) ret 8:
//   0x00437530 WCValOrderedVector<Dirk::CPlayer> slot 2: a->operator==(b)
//              (GamePlayerSetup::operator== 0x49f100)
//   0x00437550 WCValSortedVector<Dirk::CTeam> slot 4: a->operator<(b)
//              (Dirk::CTeam::operator< 0x4a05a0)
//   0x0044c4a0 a lobby vector's slot 2: Lobby::EqualField4 0x606ac0
// More of the same (wip/watcom-r6), a->member(b) with AL zero-extended:
//   0x0044cb00 WCValOrderedVector<LobbyClient::CPrivateChat> slot 2: 0x5dc780
//   0x0044cbc0 WCValOrderedVector<LobbyClient::CChatMessage> slot 2: 0x5dc460
//   0x00462960 WCValSortedVector<DirectX::CRunningSession> slot 2: 0x5bb340
//   0x00462980 WCValSortedVector<DirectX::CRunningSession> slot 4: 0x5bb3a0
//   0x004682f0 WCValOrderedVector<DirectX::CConnectionShortcut> slot 2: 0x5ba970
//   0x00541650 WCValSortedVector<Lobby::CGamePlayerStatus> slot 2: 0x5ec200
//   0x00541670 WCValSortedVector<Lobby::CGamePlayerStatus> slot 4: 0x5ec250
//   0x005b7aa0 WCValOrderedVector<DirectX::CDirectDrawDevice> slot 2 (IsEqual): 0x5b69c0
//   0x005b7bf0 WCValOrderedVector<DirectX::CDisplayMode> slot 2 (IsEqual): 0x5b6c60
//   0x005bdbb0 WCValSortedVector<DirectX::CGameNetSession::CPlayerIndex> slot 4: 0x5bd260
//   0x005dd740 WCValOrderedVector<Lobby::CImageDescription> slot 2: 0x5ebfd0
//   0x005dd880 WCValOrderedVector<Lobby::CSocketAddress> slot 2: 0x5ed220
//   0x005ddb80 WCValSortedVector<LobbyClient::priv::CLobbyServer> slot 4: 0x5c4830
//   0x00609920 WCValOrderedVector<Lobby::CLMGeneric> slot 2 (Equal): 0x6027d0
// The pointer vectors' callbacks get pointers to the elements (pointers)
// and call the member on what they point to, (*a)->member(*b); EAX as the
// member returns it:
//   0x00472180 WCPtrSortedVector<CExecuteType> slot 2: 0x49d990
//   0x004721a0 WCPtrSortedVector<CExecuteType> slot 4: 0x49d9c0
//   0x00472220 WCPtrOrderedVector<CRequestType> slot 2: 0x49d7f0
//   0x004722a0 WCPtrSortedVector<CAnnounceType> slot 2: 0x49d940
//   0x004722c0 WCPtrSortedVector<CAnnounceType> slot 4: 0x49d900
//   0x00472340 WCPtrOrderedVector<CAcknowledgeType> slot 2: 0x49d9f0
//   0x005b7930 WCPtrSList<DirectX::CGameThread> base_equivalent (Win32::CThread::operator==): 0x60ec00
// the same with AL zero-extended:
//   0x00462ac0 WCPtrSortedVector<DirectX::CPlayer> slot 2: 0x5c0f80
//   0x00462ae0 WCPtrSortedVector<DirectX::CPlayer> slot 4: 0x5c0fa0
//   0x005f3da0 WCPtrSortedVector<Lobby::CSocketAddress> slot 2: 0x5ed220
//   0x005f3dc0 WCPtrSortedVector<Lobby::CSocketAddress> slot 4: 0x5ed3e0
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
//   0x005c33a0 WCValSkipListDict<int, DirectX::CSoundEffectBuffer> base_equivalent: key at a-0x14 == *b
//   0x005c33c0 WCValSkipListDict<int, DirectX::CSoundEffectBuffer> base_less_than: key at a-0x14 < *b (signed)

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

std::uint32_t Pointee(std::uint32_t address) {
    return *reinterpret_cast<const std::uint32_t*>(address);
}

// The pointer vectors' form: the member on the pointed-to elements.
std::uint32_t ComparePointees(std::uintptr_t member, std::uint32_t a, std::uint32_t b) {
    return reinterpret_cast<MemberCompareFn>(member)(Pointee(a), nullptr, Pointee(b));
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

// Purpose: Equality and ordering callbacks of the network's, the lobby's
// and the display code's value vectors.
std::uint32_t __stdcall VectorCallback_44cb00(std::uint32_t a, std::uint32_t b) {
    return CompareThrough(0x005dc780, a, b);
}
std::uint32_t __stdcall VectorCallback_44cbc0(std::uint32_t a, std::uint32_t b) {
    return CompareThrough(0x005dc460, a, b);
}
std::uint32_t __stdcall VectorCallback_462960(std::uint32_t a, std::uint32_t b) {
    return CompareThrough(0x005bb340, a, b);
}
std::uint32_t __stdcall VectorCallback_462980(std::uint32_t a, std::uint32_t b) {
    return CompareThrough(0x005bb3a0, a, b);
}
std::uint32_t __stdcall VectorCallback_4682f0(std::uint32_t a, std::uint32_t b) {
    return CompareThrough(0x005ba970, a, b);
}
std::uint32_t __stdcall VectorCallback_541650(std::uint32_t a, std::uint32_t b) {
    return CompareThrough(0x005ec200, a, b);
}
std::uint32_t __stdcall VectorCallback_541670(std::uint32_t a, std::uint32_t b) {
    return CompareThrough(0x005ec250, a, b);
}
std::uint32_t __stdcall VectorCallback_5b7aa0(std::uint32_t a, std::uint32_t b) {
    return CompareThrough(0x005b69c0, a, b);
}
std::uint32_t __stdcall VectorCallback_5b7bf0(std::uint32_t a, std::uint32_t b) {
    return CompareThrough(0x005b6c60, a, b);
}
std::uint32_t __stdcall VectorCallback_5bdbb0(std::uint32_t a, std::uint32_t b) {
    return CompareThrough(0x005bd260, a, b);
}
std::uint32_t __stdcall VectorCallback_5dd740(std::uint32_t a, std::uint32_t b) {
    return CompareThrough(0x005ebfd0, a, b);
}
std::uint32_t __stdcall VectorCallback_5dd880(std::uint32_t a, std::uint32_t b) {
    return CompareThrough(0x005ed220, a, b);
}
std::uint32_t __stdcall VectorCallback_5ddb80(std::uint32_t a, std::uint32_t b) {
    return CompareThrough(0x005c4830, a, b);
}
std::uint32_t __stdcall VectorCallback_609920(std::uint32_t a, std::uint32_t b) {
    return CompareThrough(0x006027d0, a, b);
}

// Purpose: Equality and ordering callbacks of the network's and the
// lobby's pointer vectors and the game-thread list.
std::uint32_t __stdcall VectorCallback_472180(std::uint32_t a, std::uint32_t b) {
    return ComparePointees(0x0049d990, a, b);
}
std::uint32_t __stdcall VectorCallback_4721a0(std::uint32_t a, std::uint32_t b) {
    return ComparePointees(0x0049d9c0, a, b);
}
std::uint32_t __stdcall VectorCallback_472220(std::uint32_t a, std::uint32_t b) {
    return ComparePointees(0x0049d7f0, a, b);
}
std::uint32_t __stdcall VectorCallback_4722a0(std::uint32_t a, std::uint32_t b) {
    return ComparePointees(0x0049d940, a, b);
}
std::uint32_t __stdcall VectorCallback_4722c0(std::uint32_t a, std::uint32_t b) {
    return ComparePointees(0x0049d900, a, b);
}
std::uint32_t __stdcall VectorCallback_472340(std::uint32_t a, std::uint32_t b) {
    return ComparePointees(0x0049d9f0, a, b);
}
std::uint32_t __stdcall VectorCallback_5b7930(std::uint32_t a, std::uint32_t b) {
    return ComparePointees(0x0060ec00, a, b);
}
std::uint32_t __stdcall VectorCallback_462ac0(std::uint32_t a, std::uint32_t b) {
    return ComparePointees(0x005c0f80, a, b) & 0xFF;
}
std::uint32_t __stdcall VectorCallback_462ae0(std::uint32_t a, std::uint32_t b) {
    return ComparePointees(0x005c0fa0, a, b) & 0xFF;
}
std::uint32_t __stdcall VectorCallback_5f3da0(std::uint32_t a, std::uint32_t b) {
    return ComparePointees(0x005ed220, a, b) & 0xFF;
}
std::uint32_t __stdcall VectorCallback_5f3dc0(std::uint32_t a, std::uint32_t b) {
    return ComparePointees(0x005ed3e0, a, b) & 0xFF;
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
// The sound effects' dictionaries of duplicate buffers.
std::uint32_t __stdcall SkipDictKeyEqual_5c33a0(std::uint32_t node, std::uint32_t key) {
    return ReadAt<std::int32_t>(node - 0x14) == ReadAt<std::int32_t>(key) ? 1 : 0;
}
std::uint32_t __stdcall SkipDictKeyLess_5c33c0(std::uint32_t node, std::uint32_t key) {
    return ReadAt<std::int32_t>(node - 0x14) < ReadAt<std::int32_t>(key) ? 1 : 0;
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
S3RE_HOOK(0x005c33a0, SkipDictKeyEqual_5c33a0, VectorCallbackFn, Original_SkipDictKeyEqual_5c33a0,
          SkipDictKeyEqual_5c33a0);
S3RE_HOOK(0x005c33c0, SkipDictKeyLess_5c33c0, VectorCallbackFn, Original_SkipDictKeyLess_5c33c0,
          SkipDictKeyLess_5c33c0);
S3RE_HOOK(0x00437530, PlayerVectorEquivalent, VectorCallbackFn, Original_PlayerVectorEquivalent,
          PlayerVectorEquivalent);
S3RE_HOOK(0x00437550, TeamVectorLessThan, VectorCallbackFn, Original_TeamVectorLessThan, TeamVectorLessThan);
S3RE_HOOK(0x0044c4a0, LobbyVectorEquivalent, VectorCallbackFn, Original_LobbyVectorEquivalent,
          LobbyVectorEquivalent);
S3RE_HOOK(0x0044cb00, VectorCallback_44cb00, VectorCallbackFn, Original_VectorCallback_44cb00,
          VectorCallback_44cb00);
S3RE_HOOK(0x0044cbc0, VectorCallback_44cbc0, VectorCallbackFn, Original_VectorCallback_44cbc0,
          VectorCallback_44cbc0);
S3RE_HOOK(0x00462960, VectorCallback_462960, VectorCallbackFn, Original_VectorCallback_462960,
          VectorCallback_462960);
S3RE_HOOK(0x00462980, VectorCallback_462980, VectorCallbackFn, Original_VectorCallback_462980,
          VectorCallback_462980);
S3RE_HOOK(0x004682f0, VectorCallback_4682f0, VectorCallbackFn, Original_VectorCallback_4682f0,
          VectorCallback_4682f0);
S3RE_HOOK(0x00541650, VectorCallback_541650, VectorCallbackFn, Original_VectorCallback_541650,
          VectorCallback_541650);
S3RE_HOOK(0x00541670, VectorCallback_541670, VectorCallbackFn, Original_VectorCallback_541670,
          VectorCallback_541670);
S3RE_HOOK(0x005b7aa0, VectorCallback_5b7aa0, VectorCallbackFn, Original_VectorCallback_5b7aa0,
          VectorCallback_5b7aa0);
S3RE_HOOK(0x005b7bf0, VectorCallback_5b7bf0, VectorCallbackFn, Original_VectorCallback_5b7bf0,
          VectorCallback_5b7bf0);
S3RE_HOOK(0x005bdbb0, VectorCallback_5bdbb0, VectorCallbackFn, Original_VectorCallback_5bdbb0,
          VectorCallback_5bdbb0);
S3RE_HOOK(0x005dd740, VectorCallback_5dd740, VectorCallbackFn, Original_VectorCallback_5dd740,
          VectorCallback_5dd740);
S3RE_HOOK(0x005dd880, VectorCallback_5dd880, VectorCallbackFn, Original_VectorCallback_5dd880,
          VectorCallback_5dd880);
S3RE_HOOK(0x005ddb80, VectorCallback_5ddb80, VectorCallbackFn, Original_VectorCallback_5ddb80,
          VectorCallback_5ddb80);
S3RE_HOOK(0x00609920, VectorCallback_609920, VectorCallbackFn, Original_VectorCallback_609920,
          VectorCallback_609920);
S3RE_HOOK(0x00472180, VectorCallback_472180, VectorCallbackFn, Original_VectorCallback_472180,
          VectorCallback_472180);
S3RE_HOOK(0x004721a0, VectorCallback_4721a0, VectorCallbackFn, Original_VectorCallback_4721a0,
          VectorCallback_4721a0);
S3RE_HOOK(0x00472220, VectorCallback_472220, VectorCallbackFn, Original_VectorCallback_472220,
          VectorCallback_472220);
S3RE_HOOK(0x004722a0, VectorCallback_4722a0, VectorCallbackFn, Original_VectorCallback_4722a0,
          VectorCallback_4722a0);
S3RE_HOOK(0x004722c0, VectorCallback_4722c0, VectorCallbackFn, Original_VectorCallback_4722c0,
          VectorCallback_4722c0);
S3RE_HOOK(0x00472340, VectorCallback_472340, VectorCallbackFn, Original_VectorCallback_472340,
          VectorCallback_472340);
S3RE_HOOK(0x005b7930, VectorCallback_5b7930, VectorCallbackFn, Original_VectorCallback_5b7930,
          VectorCallback_5b7930);
S3RE_HOOK(0x00462ac0, VectorCallback_462ac0, VectorCallbackFn, Original_VectorCallback_462ac0,
          VectorCallback_462ac0);
S3RE_HOOK(0x00462ae0, VectorCallback_462ae0, VectorCallbackFn, Original_VectorCallback_462ae0,
          VectorCallback_462ae0);
S3RE_HOOK(0x005f3da0, VectorCallback_5f3da0, VectorCallbackFn, Original_VectorCallback_5f3da0,
          VectorCallback_5f3da0);
S3RE_HOOK(0x005f3dc0, VectorCallback_5f3dc0, VectorCallbackFn, Original_VectorCallback_5f3dc0,
          VectorCallback_5f3dc0);

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
    {Original_SkipDictKeyEqual_5c33a0, &SkipDictKeyEqual_5c33a0},
    {Original_SkipDictKeyLess_5c33c0, &SkipDictKeyLess_5c33c0},
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


// The other member callbacks: a and b point at two random words (the
// pointer vectors' elements); the member replaced by a recording stub that
// returns a random value. Compared: the calls (ECX and the argument) and EAX.
template <std::uintptr_t Member>
bool CompareMember(test::Context& ctx, VectorCallbackFn original, VectorCallbackFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        std::uint32_t words[2];
        ctx.rng().Fill(words, sizeof(words));
        const auto a = reinterpret_cast<std::uint32_t>(&words[0]);
        const auto b = reinterpret_cast<std::uint32_t>(&words[1]);
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
            ctx.Fail(__FILE__, __LINE__, "  member 0x%x case %d", static_cast<unsigned>(Member), i);
            return false;
        }
    }
    return true;
}

struct MemberCase {
    VectorCallbackFn original;
    VectorCallbackFn ours;
    bool (*compare)(test::Context&, VectorCallbackFn, VectorCallbackFn, int);
};

const MemberCase kMemberCases[] = {
    {Original_VectorCallback_44cb00, &VectorCallback_44cb00, &CompareMember<0x005dc780>},
    {Original_VectorCallback_44cbc0, &VectorCallback_44cbc0, &CompareMember<0x005dc460>},
    {Original_VectorCallback_462960, &VectorCallback_462960, &CompareMember<0x005bb340>},
    {Original_VectorCallback_462980, &VectorCallback_462980, &CompareMember<0x005bb3a0>},
    {Original_VectorCallback_4682f0, &VectorCallback_4682f0, &CompareMember<0x005ba970>},
    {Original_VectorCallback_541650, &VectorCallback_541650, &CompareMember<0x005ec200>},
    {Original_VectorCallback_541670, &VectorCallback_541670, &CompareMember<0x005ec250>},
    {Original_VectorCallback_5b7aa0, &VectorCallback_5b7aa0, &CompareMember<0x005b69c0>},
    {Original_VectorCallback_5b7bf0, &VectorCallback_5b7bf0, &CompareMember<0x005b6c60>},
    {Original_VectorCallback_5bdbb0, &VectorCallback_5bdbb0, &CompareMember<0x005bd260>},
    {Original_VectorCallback_5dd740, &VectorCallback_5dd740, &CompareMember<0x005ebfd0>},
    {Original_VectorCallback_5dd880, &VectorCallback_5dd880, &CompareMember<0x005ed220>},
    {Original_VectorCallback_5ddb80, &VectorCallback_5ddb80, &CompareMember<0x005c4830>},
    {Original_VectorCallback_609920, &VectorCallback_609920, &CompareMember<0x006027d0>},
    {Original_VectorCallback_472180, &VectorCallback_472180, &CompareMember<0x0049d990>},
    {Original_VectorCallback_4721a0, &VectorCallback_4721a0, &CompareMember<0x0049d9c0>},
    {Original_VectorCallback_472220, &VectorCallback_472220, &CompareMember<0x0049d7f0>},
    {Original_VectorCallback_4722a0, &VectorCallback_4722a0, &CompareMember<0x0049d940>},
    {Original_VectorCallback_4722c0, &VectorCallback_4722c0, &CompareMember<0x0049d900>},
    {Original_VectorCallback_472340, &VectorCallback_472340, &CompareMember<0x0049d9f0>},
    {Original_VectorCallback_5b7930, &VectorCallback_5b7930, &CompareMember<0x0060ec00>},
    {Original_VectorCallback_462ac0, &VectorCallback_462ac0, &CompareMember<0x005c0f80>},
    {Original_VectorCallback_462ae0, &VectorCallback_462ae0, &CompareMember<0x005c0fa0>},
    {Original_VectorCallback_5f3da0, &VectorCallback_5f3da0, &CompareMember<0x005ed220>},
    {Original_VectorCallback_5f3dc0, &VectorCallback_5f3dc0, &CompareMember<0x005ed3e0>},
};

// Wrong: the pointer form without dereferencing / AL not zero-extended.
std::uint32_t __stdcall WrongPointerCallback(std::uint32_t a, std::uint32_t b) {
    return reinterpret_cast<MemberCompareFn>(0x0049d990)(a, nullptr, b);
}
std::uint32_t __stdcall WrongByteCallback(std::uint32_t a, std::uint32_t b) {
    return ComparePointees(0x005c0f80, a, b);
}

}  // namespace

S3RE_TEST(WatcomMemberCallbacks_MatchOriginal) {
    for (const MemberCase& c : kMemberCases) {
        if (!c.compare(ctx, c.original, c.ours, 200)) {
            return;
        }
    }
}

S3RE_TEST(WatcomMemberCallbacks_ComparisonDetectsWrongImplementation) {
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareMember<0x0049d990>(probe, Original_VectorCallback_472180, &WrongPointerCallback, 20);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareMember<0x005c0f80>(probe, Original_VectorCallback_462ac0, &WrongByteCallback, 20);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

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
