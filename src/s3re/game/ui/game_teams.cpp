// The teams of a game's setup (Dirk::CTeam, RTTI name; 0x40 bytes, in a
// WCValSortedVector<Dirk::CTeam> of the game settings) and the comparison
// operators the Watcom vectors of teams and players use (Dirk\nethelp.cpp's
// unit). All thiscall.
//
// Layout (Dirk::CTeam):
//   +0x00 id (-1 from the default constructor; the sort key)
//   +0x04 (not touched here)
//   +0x08 WCValSortedVector<int> (vtable 0x655a0c; capacity 10, grow 5)
//   +0x24 WCValOrderedVector<Dirk::CPlayer> (vtable 0x656b6c; capacity 10,
//         grow 5; elements are our GamePlayerSetup, 0xcc bytes)
//
// Dirk::CTeam::CTeam (0x004a0200): () -> this: id = -1, then the two
//   vectors as above (operator new(40), then operator new(2040)).
// Dirk::CTeam::CTeam_id (0x004a02d0): (id) ret 4 -> this: the same with
//   the given id.
// Dirk::CTeam::~CTeam (0x004a04d0): the player vector's destructor inlined
//   (vtable 0x655a50; if it has a block: ~GamePlayerSetup 0x49ec80 on each
//   filled element, operator delete; vtable WCExcept), then the int
//   vector's (vtable 0x655a5c; operator delete of its block; WCExcept).
//   EAX = the int vector's operator delete's, or 0 without a block.
// Dirk::CTeam::operator< (0x004a05a0): (other*) ret 4 -> AL = id < other's
//   id (signed). The rest of EAX is this team's id (the `mov al, cl` keeps
//   it); the one caller (0x437559, WCValSortedVector<CTeam>) masks AL.
// GamePlayerSetup::operator== (0x0049f100; Dirk::CPlayer): (other*) ret 4
//   -> AL = (+0x10 == other's +0x10); the rest of EAX is this +0x10. The
//   one caller (0x437539) masks AL.

#include "game/ui/game_settings_layout.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "watcom/vector_base.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

#pragma pack(push, 1)
struct Team {
    std::int32_t id;
    std::uint32_t unk04;
    watcom::VectorBase intList;  // +0x08, probably the ids of the team's players
    watcom::VectorBase players;  // +0x24
};
#pragma pack(pop)
static_assert(sizeof(Team) == 0x40);
static_assert(offsetof(Team, intList) == 0x08);
static_assert(offsetof(Team, players) == 0x24);

constexpr std::uint32_t kInitialCapacity = 10;
constexpr std::uint32_t kGrowStep = 5;
constexpr std::int32_t kNoId = -1;
constexpr std::uintptr_t kDestroyPlayer = 0x0049ec80;  // GamePlayerSetup::~GamePlayerSetup (ours)

void ConstructMembers(Team* team) {
    watcom::ConstructVector(&team->intList, kInitialCapacity, kGrowStep, sizeof(std::int32_t),
                            vtables::kWCBareVectorBase_int, vtables::kWCValSortedVector_int);
    watcom::ConstructVector(&team->players, kInitialCapacity, kGrowStep, sizeof(GamePlayerSetup),
                            vtables::kWCBareVectorBase_CPlayer, vtables::kWCValOrderedVector_CPlayer);
}

// EAX of the byte-returning comparisons: the low byte replaced in `upper`.
std::uint32_t WithLowByte(std::uint32_t upper, bool value) {
    return (upper & 0xFFFFFF00u) | (value ? 1u : 0u);
}

}  // namespace

using TeamFn = Team*(__fastcall*)(Team* self, void* edx);
using TeamIdFn = Team*(__fastcall*)(Team* self, void* edx, std::int32_t id);
using TeamDestructorFn = std::uint32_t(__fastcall*)(Team* self, void* edx);
using TeamLessFn = std::uint32_t(__fastcall*)(const Team* self, void* edx, const Team* other);
using PlayerEqualFn = std::uint32_t(__fastcall*)(const GamePlayerSetup* self, void* edx, const GamePlayerSetup* other);

// Purpose: Default constructor of a team of the game setup (no id yet,
// empty player lists).
Team* __fastcall CTeam_Construct(Team* self, void*) {
    self->id = kNoId;
    ConstructMembers(self);
    return self;
}

// Purpose: Constructor of a team with the given id (the game settings
// create one per team when a game is set up or loaded).
Team* __fastcall CTeam_ConstructId(Team* self, void*, std::int32_t id) {
    self->id = id;
    ConstructMembers(self);
    return self;
}

// Purpose: Destructor of a team: destroys its player setups and frees both
// lists.
std::uint32_t __fastcall CTeam_Destroy(Team* self, void*) {
    watcom::DestroyVector(&self->players, vtables::kWCBareVectorBase_CPlayer, vtables::kWCExcept,
                          sizeof(GamePlayerSetup), [](std::uint32_t player) {
                              reinterpret_cast<void(__fastcall*)(std::uint32_t, void*)>(kDestroyPlayer)(player,
                                                                                                    nullptr);
                          });
    return watcom::DestroyVector(&self->intList, vtables::kWCBareVectorBase_int, vtables::kWCExcept, 0,
                                 [](std::uint32_t) {});
}

// Purpose: Orders teams by id (the sorted vector of teams).
std::uint32_t __fastcall CTeam_Less(const Team* self, void*, const Team* other) {
    return WithLowByte(static_cast<std::uint32_t>(self->id), self->id < other->id);
}

// Purpose: Equality of two player setups (by their +0x10 value), for the
// vector of player setups' searches.
std::uint32_t __fastcall GamePlayerSetup_Equal(const GamePlayerSetup* self, void*, const GamePlayerSetup* other) {
    return WithLowByte(self->value10, self->value10 == other->value10);
}

S3RE_HOOK(0x004a0200, CTeam_Construct, TeamFn, Original_CTeam_Construct, CTeam_Construct);
S3RE_HOOK(0x004a02d0, CTeam_ConstructId, TeamIdFn, Original_CTeam_ConstructId, CTeam_ConstructId);
S3RE_HOOK(0x004a04d0, CTeam_Destroy, TeamDestructorFn, Original_CTeam_Destroy, CTeam_Destroy);
S3RE_HOOK(0x004a05a0, CTeam_Less, TeamLessFn, Original_CTeam_Less, CTeam_Less);
S3RE_HOOK(0x0049f100, GamePlayerSetup_Equal, PlayerEqualFn, Original_GamePlayerSetup_Equal, GamePlayerSetup_Equal);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// Constructors: random objects, compared bytewise with the data pointers
// only as null or not (blocks freed). Destructor: random teams with blocks
// from the game's heap (or none) and filled -2..6, ~GamePlayerSetup replaced
// by a recording stub (calls compared as offsets into each player block);
// the bytes and EAX (without an int block). Comparisons: random ids/values
// (often equal), full EAX.

namespace {

bool SameConstructedTeams(test::Context& ctx, Team& a, Team& b) {
    bool same = true;
    for (watcom::VectorBase Team::*member : {&Team::intList, &Team::players}) {
        same = S3RE_EXPECT_EQ((a.*member).data == 0, (b.*member).data == 0) && same;
        msvc6::GameOperatorDelete(reinterpret_cast<void*>((a.*member).data));
        msvc6::GameOperatorDelete(reinterpret_cast<void*>((b.*member).data));
        (a.*member).data = (b.*member).data = 0;
    }
    return S3RE_EXPECT_MEM_EQ(&a, &b, sizeof(a)) && same;
}

void CompareConstructors(test::Context& ctx, TeamFn construct, TeamIdFn constructId, int cases) {
    for (int i = 0; i < cases; ++i) {
        Team a, b;
        ctx.rng().Fill(&a, sizeof(a));
        std::memcpy(&b, &a, sizeof(a));
        const bool withId = ctx.rng().Chance(1, 2);
        const auto id = static_cast<std::int32_t>(ctx.rng().Next32());
        Team* ra = withId ? Original_CTeam_ConstructId(&a, nullptr, id) : Original_CTeam_Construct(&a, nullptr);
        Team* rb = withId ? constructId(&b, nullptr, id) : construct(&b, nullptr);
        if (!S3RE_EXPECT(ra == &a && rb == &b) || !SameConstructedTeams(ctx, a, b)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: with id %d", i, withId);
            return;
        }
    }
}

struct DestroyRun {
    std::uint32_t eax;
    std::vector<test::StubCall> calls;
};

DestroyRun RunDestroy(TeamDestructorFn destroy, Team& team) {
    test::CalleeStubs stubs;
    stubs.Install<0, kDestroyPlayer>();
    stubs.SetWorld(reinterpret_cast<CGameData*>(team.players.data));
    DestroyRun run{destroy(&team, nullptr), {}};
    run.calls = stubs.Take();
    return run;
}

void CompareDestructor(test::Context& ctx, TeamDestructorFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        Team a, b;
        ctx.rng().Fill(&a, sizeof(a));
        a.players.filled = ctx.rng().Range(-2, 6);
        a.intList.filled = ctx.rng().Range(-2, 6);
        std::memcpy(&b, &a, sizeof(a));
        const bool intBlock = ctx.rng().Chance(3, 4);
        const bool playerBlock = ctx.rng().Chance(3, 4);
        for (Team* team : {&a, &b}) {
            team->intList.data = intBlock ? reinterpret_cast<std::uintptr_t>(msvc6::GameOperatorNew(40)) : 0;
            team->players.data =
                playerBlock ? reinterpret_cast<std::uintptr_t>(msvc6::GameOperatorNew(6 * sizeof(GamePlayerSetup))) : 0;
        }
        const DestroyRun ra = RunDestroy(Original_CTeam_Destroy, a);
        const DestroyRun rb = RunDestroy(candidate, b);
        a.intList.data = b.intList.data = a.players.data = b.players.data = 0;  // freed (or none)
        bool same = S3RE_EXPECT(ra.calls == rb.calls) && S3RE_EXPECT_MEM_EQ(&a, &b, sizeof(a));
        if (!intBlock) {
            same = S3RE_EXPECT_EQ(ra.eax, rb.eax) && same;
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: blocks %d/%d", i, intBlock, playerBlock);
            return;
        }
    }
}

void CompareComparisons(test::Context& ctx, TeamLessFn less, PlayerEqualFn equal, int cases) {
    for (int i = 0; i < cases; ++i) {
        Team ta{}, tb{};
        GamePlayerSetup pa{}, pb{};
        const std::uint32_t x = ctx.rng().Next32();
        const std::uint32_t y = ctx.rng().Chance(1, 3) ? x : ctx.rng().Chance(1, 2) ? x + 1 : ctx.rng().Next32();
        ta.id = static_cast<std::int32_t>(x);
        tb.id = static_cast<std::int32_t>(y);
        pa.value10 = x;
        pb.value10 = y;
        if (!S3RE_EXPECT_EQ(Original_CTeam_Less(&ta, nullptr, &tb), less(&ta, nullptr, &tb)) ||
            !S3RE_EXPECT_EQ(Original_GamePlayerSetup_Equal(&pa, nullptr, &pb), equal(&pa, nullptr, &pb))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: 0x%x, 0x%x", i, x, y);
            return;
        }
    }
}

// Wrong implementations.
Team* __fastcall WrongConstruct(Team* self, void* edx) {
    CTeam_Construct(self, edx);
    self->players.grow = 10;
    return self;
}
std::uint32_t __fastcall WrongDestroy(Team* self, void* edx) {
    --self->players.filled;
    const std::uint32_t result = CTeam_Destroy(self, edx);
    ++self->players.filled;
    return result;
}
std::uint32_t __fastcall WrongLess(const Team* self, void*, const Team* other) {
    return WithLowByte(static_cast<std::uint32_t>(self->id),
                       static_cast<std::uint32_t>(self->id) < static_cast<std::uint32_t>(other->id));  // unsigned
}

}  // namespace

S3RE_TEST(CTeam_Constructors_MatchOriginal) {
    CompareConstructors(ctx, &CTeam_Construct, &CTeam_ConstructId, 1000);
}
S3RE_TEST(CTeam_Destroy_MatchesOriginal) {
    CompareDestructor(ctx, &CTeam_Destroy, 1000);
}
S3RE_TEST(TeamComparisons_MatchOriginal) {
    CompareComparisons(ctx, &CTeam_Less, &GamePlayerSetup_Equal, 3000);
}
S3RE_TEST(CTeam_ComparisonDetectsWrongImplementation) {
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareConstructors(probe, &WrongConstruct, &CTeam_ConstructId, 50);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareDestructor(probe, &WrongDestroy, 100);
        S3RE_EXPECT(probe.failures() > 0);
    }
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareComparisons(probe, &WrongLess, &GamePlayerSetup_Equal, 200);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
