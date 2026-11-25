// The comparison callbacks of Watcom value vectors (WCValOrderedVector's
// base_equivalent, WCValSortedVector's base_less_than: static functions
// in the vtables). Each calls the element type's operator through its
// address and returns its AL zero-extended. stdcall (a*, b*) ret 8:
//   0x00437530 WCValOrderedVector<Dirk::CPlayer> slot 2: a->operator==(b)
//              (GamePlayerSetup::operator== 0x49f100)
//   0x00437550 WCValSortedVector<Dirk::CTeam> slot 4: a->operator<(b)
//              (Dirk::CTeam::operator< 0x4a05a0)
//   0x0044c4a0 a lobby vector's slot 2: Lobby::EqualField4 0x606ac0

#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstdint>
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

}  // namespace

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
