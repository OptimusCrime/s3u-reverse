// Building "request" flags and their per-player counters.
//
// Building records (see counting.cpp): 0x3a bytes at world+0x1499e64. Byte
// +0x18 holds flags: bit 0x1 enables the four request bits 0x2, 0x4, 0x8 and
// 0x10 (meaning unknown, names provisional). Per player (records of 0x148
// bytes at world+0x11943dc), uint16 counters:
//   +0x24  buildings with at least one request bit set (capped at 10 by the
//          setters)
//   +0x20, +0x22, +0x26, +0x28  one per request bit 0x2, 0x4, 0x8, 0x10;
//          bit 7 is set the first time (setters store (n | 0x80) + 1), and a
//          clearer that brings it down to exactly 0x80 resets it to 0.
//
// Clear (bit 0x2: 0x004df490, 0x4: 0x004df510, 0x8: 0x004df590,
// 0x10: 0x004df610), argument: building index. If the bit is set: clear it;
// if no request bit is left, decrement +0x24; decrement the bit's counter,
// and if it is then 0x80 store 0.
//
// Set (0x2: 0x004df690, 0x4: 0x004df730, 0x8: 0x004df7d0,
// 0x10: 0x004df850), argument: building index. Does nothing unless
//   - bit 0x2: byte +0x19 differs from byte +0x1 of the 14-byte record
//     (world+0x123885c, a Pile: its amount) selected by word +0x10 (piles[0]);
//     bit 0x4: likewise byte +0x1a and word +0x12;
//     bit 0x10: byte +0x21 is 1;
//   - the player's +0x24 counter is below 10, and
//   - flag 0x1 is set.
// Then: if no request bit was set, increment +0x24; if the bit was clear,
// set it and store (counter | 0x80) + 1 in the bit's counter.
//
// All eight: thiscall (ECX = world), one stack argument, ret 4. Callers
// don't use EAX.

#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace s3re::game {

namespace {

using layout::Building;
using layout::Player;

// Building::request_flags (+0x18): bit 0x01 enables the four request bits; their meaning is unknown, so
// they are named after their bit.
constexpr std::uint8_t kFlagEnabled = 0x01;
constexpr std::uint8_t kRequest02 = 0x02;
constexpr std::uint8_t kRequest04 = 0x04;
constexpr std::uint8_t kRequest08 = 0x08;
constexpr std::uint8_t kRequest10 = 0x10;
constexpr std::uint8_t kRequestBits = kRequest02 | kRequest04 | kRequest08 | kRequest10;  // 0x1e

constexpr std::uint16_t kMaxRequestCount = 10;

// The player's building request counters live in Player::settler_lists:
// [2] (+0x24) the number of buildings with requests, [0], [1], [3], [4]
// (+0x20, +0x22, +0x26, +0x28) one per request bit.
constexpr std::size_t kRequestCount = 2;
constexpr std::size_t kCounter02 = 0;  // +0x20
constexpr std::size_t kCounter04 = 1;  // +0x22
constexpr std::size_t kCounter08 = 3;  // +0x26
constexpr std::size_t kCounter10 = 4;  // +0x28

// A per-bit counter gets bit 7 the first time it is set ((n | 0x80) + 1); a clear that brings it down to
// exactly the marker resets it to 0.
constexpr std::uint16_t kCounterFirstTimeMarker = 0x80;

Player& PlayerOf(CGameData* world, const Building& building) {
    return world->players[building.owner];
}

void ClearRequest(CGameData* world, std::uint32_t buildingId, std::uint8_t bit, std::size_t counter) {
    Building& building = world->buildings[buildingId];
    std::uint8_t flags = building.request_flags;
    if ((flags & bit) == 0) {
        return;
    }
    Player& player = PlayerOf(world, building);
    flags = static_cast<std::uint8_t>(flags & ~bit);
    building.request_flags = flags;
    if ((flags & kRequestBits) == 0) {
        player.settler_lists[kRequestCount] = static_cast<std::uint16_t>(player.settler_lists[kRequestCount] - 1);
    }
    const auto value = static_cast<std::uint16_t>(player.settler_lists[counter] - 1);
    player.settler_lists[counter] = value == kCounterFirstTimeMarker ? 0 : value;
}

// What a setter checks before it sets its bit.
enum class Condition {
    BoardsMissingDiffer,  // byte +0x19 differs from the amount of pile +0x10 (bit 0x02)
    StonesMissingDiffer,  // byte +0x1a differs from the amount of pile +0x12 (bit 0x04)
    None,                 // bit 0x08
    BuildupIsOne,         // byte +0x21 is 1 (bit 0x10)
};

void SetRequest(CGameData* world, std::uint32_t buildingId, std::uint8_t bit, std::size_t counter,
                Condition condition) {
    Building& building = world->buildings[buildingId];
    switch (condition) {
    case Condition::BoardsMissingDiffer:
        if (building.site.boards_missing == world->piles[building.piles[0]].amount) {
            return;
        }
        break;
    case Condition::StonesMissingDiffer:
        if (building.site.stones_missing == world->piles[building.piles[1]].amount) {
            return;
        }
        break;
    case Condition::BuildupIsOne:
        if (building.buildup != 1) {
            return;
        }
        break;
    case Condition::None:
        break;
    }
    Player& player = PlayerOf(world, building);
    const std::uint16_t requests = player.settler_lists[kRequestCount];
    if (requests >= kMaxRequestCount || (building.request_flags & kFlagEnabled) == 0) {
        return;
    }
    if ((building.request_flags & kRequestBits) == 0) {
        player.settler_lists[kRequestCount] = static_cast<std::uint16_t>(requests + 1);
    }
    if ((building.request_flags & bit) != 0) {
        return;
    }
    building.request_flags = static_cast<std::uint8_t>(building.request_flags | bit);
    player.settler_lists[counter] =
        static_cast<std::uint16_t>((player.settler_lists[counter] | kCounterFirstTimeMarker) + 1);
}

}  // namespace

using BuildingFn = void(__fastcall*)(CGameData* world, void* edx, std::uint32_t building);

// Purpose: Withdraws one kind of request a building has made (one of four request bits), keeping the
// player's request counters right. Names are provisional.
void __fastcall CGameData_ClearBuildingFlag02(CGameData* world, void*, std::uint32_t b) {
    ClearRequest(world, b, kRequest02, kCounter02);  // 0x4df490
}
// Purpose: Withdraws another kind of building request (see ClearBuildingFlag02).
void __fastcall CGameData_ClearBuildingFlag04(CGameData* world, void*, std::uint32_t b) {
    ClearRequest(world, b, kRequest04, kCounter04);  // 0x4df510
}
// Purpose: Withdraws another kind of building request (see ClearBuildingFlag02).
void __fastcall CGameData_ClearBuildingFlag08(CGameData* world, void*, std::uint32_t b) {
    ClearRequest(world, b, kRequest08, kCounter08);  // 0x4df590
}
// Purpose: Withdraws another kind of building request (see ClearBuildingFlag02).
void __fastcall CGameData_ClearBuildingFlag10(CGameData* world, void*, std::uint32_t b) {
    ClearRequest(world, b, kRequest10, kCounter10);  // 0x4df610
}
// Purpose: Makes one kind of request for a building (e.g. 'needs a worker'), if the player doesn't have too
// many open already, and counts it. Names are provisional.
void __fastcall CGameData_SetBuildingFlag02(CGameData* world, void*, std::uint32_t b) {
    SetRequest(world, b, kRequest02, kCounter02, Condition::BoardsMissingDiffer);  // 0x4df690
}
// Purpose: Makes another kind of building request (see SetBuildingFlag02).
void __fastcall CGameData_SetBuildingFlag04(CGameData* world, void*, std::uint32_t b) {
    SetRequest(world, b, kRequest04, kCounter04, Condition::StonesMissingDiffer);  // 0x4df730
}
// Purpose: Makes another kind of building request (see SetBuildingFlag02).
void __fastcall CGameData_SetBuildingFlag08(CGameData* world, void*, std::uint32_t b) {
    SetRequest(world, b, kRequest08, kCounter08, Condition::None);  // 0x4df7d0
}
// Purpose: Makes another kind of building request (see SetBuildingFlag02).
void __fastcall CGameData_SetBuildingFlag10(CGameData* world, void*, std::uint32_t b) {
    SetRequest(world, b, kRequest10, kCounter10, Condition::BuildupIsOne);  // 0x4df850
}

S3RE_HOOK(0x004df490, CGameData_ClearBuildingFlag02, BuildingFn,
          Original_CGameData_ClearBuildingFlag02, CGameData_ClearBuildingFlag02);
S3RE_HOOK(0x004df510, CGameData_ClearBuildingFlag04, BuildingFn,
          Original_CGameData_ClearBuildingFlag04, CGameData_ClearBuildingFlag04);
S3RE_HOOK(0x004df590, CGameData_ClearBuildingFlag08, BuildingFn,
          Original_CGameData_ClearBuildingFlag08, CGameData_ClearBuildingFlag08);
S3RE_HOOK(0x004df610, CGameData_ClearBuildingFlag10, BuildingFn,
          Original_CGameData_ClearBuildingFlag10, CGameData_ClearBuildingFlag10);
S3RE_HOOK(0x004df690, CGameData_SetBuildingFlag02, BuildingFn,
          Original_CGameData_SetBuildingFlag02, CGameData_SetBuildingFlag02);
S3RE_HOOK(0x004df730, CGameData_SetBuildingFlag04, BuildingFn,
          Original_CGameData_SetBuildingFlag04, CGameData_SetBuildingFlag04);
S3RE_HOOK(0x004df7d0, CGameData_SetBuildingFlag08, BuildingFn,
          Original_CGameData_SetBuildingFlag08, CGameData_SetBuildingFlag08);
S3RE_HOOK(0x004df850, CGameData_SetBuildingFlag10, BuildingFn,
          Original_CGameData_SetBuildingFlag10, CGameData_SetBuildingFlag10);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestBuildings = 2000;
constexpr std::size_t kTestWorld = offsetof(CGameData, buildings) + (kTestBuildings + 1) * sizeof(Building) + 0x100;

// Runs random cases of one setter or clearer on biased building and player
// state, comparing the building and player records after every case and the
// whole world now and then.
void CompareBuildingFlag(test::Context& ctx, BuildingFn original, BuildingFn candidate,
                         int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t index = 1 + ctx.rng().Below(kTestBuildings);
        auto building = [&](CGameData* w) -> Building& { return w->buildings[index]; };
        const std::uint8_t player = static_cast<std::uint8_t>(
            ctx.rng().Chance(1, 20) ? ctx.rng().Below(256) : ctx.rng().Below(20));
        const auto flags = static_cast<std::uint8_t>(ctx.rng().Next32() & (ctx.rng().Chance(1, 2) ? 0x1f : 0xff));
        worlds.Both([&](CGameData* w) {
            building(w).owner = player;
            building(w).request_flags = flags;
        });
        // Pile lookups that match half the time.
        for (int which = 0; which < 2; ++which) {
            const auto pile = static_cast<std::uint16_t>(ctx.rng().Next32());
            const std::uint8_t value = static_cast<std::uint8_t>(ctx.rng().Below(4));
            const std::uint8_t amount =
                ctx.rng().Chance(1, 2) ? value : static_cast<std::uint8_t>(ctx.rng().Below(4));
            worlds.Both([&](CGameData* w) {
                building(w).piles[which] = pile;
                (which == 0 ? building(w).site.boards_missing : building(w).site.stones_missing) = value;
                w->piles[pile].amount = amount;
            });
        }
        const auto buildup = static_cast<std::uint8_t>(ctx.rng().Below(3));
        worlds.Both([&](CGameData* w) { building(w).buildup = buildup; });
        // Counters around the interesting values.
        const auto requests =
            static_cast<std::uint16_t>(ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : 7 + ctx.rng().Below(5));
        worlds.Both([&](CGameData* w) { w->players[player].settler_lists[kRequestCount] = requests; });
        for (std::size_t counter : {kCounter02, kCounter04, kCounter08, kCounter10}) {
            const auto value =
                static_cast<std::uint16_t>(ctx.rng().Chance(1, 4) ? ctx.rng().Next32() : 0x7e + ctx.rng().Below(5));
            worlds.Both([&](CGameData* w) { w->players[player].settler_lists[counter] = value; });
        }

        original(worlds.original(), nullptr, index);
        candidate(worlds.ours(), nullptr, index);
        bool same = worlds.SameField(ctx, building) &&
                    worlds.SameField(ctx, [&](CGameData* w) -> auto& { return w->players[player]; });
        if (i % 256 == 255) {
            same = same && worlds.Checkpoint(ctx);
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: building %u, player %u", i, index, player);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

// Wrong: the "first time" marker handling is missing (plain counter + 1).
void __fastcall WrongSetBuildingFlag08(CGameData* world, void*, std::uint32_t index) {
    Building& building = world->buildings[index];
    Player& player = PlayerOf(world, building);
    const std::uint16_t requests = player.settler_lists[kRequestCount];
    if (requests >= kMaxRequestCount || (building.request_flags & kFlagEnabled) == 0) {
        return;
    }
    if ((building.request_flags & kRequestBits) == 0) {
        player.settler_lists[kRequestCount] = static_cast<std::uint16_t>(requests + 1);
    }
    if ((building.request_flags & kRequest08) != 0) {
        return;
    }
    building.request_flags = static_cast<std::uint8_t>(building.request_flags | kRequest08);
    player.settler_lists[kCounter08] = static_cast<std::uint16_t>(player.settler_lists[kCounter08] + 1);
}

// Wrong: no reset of a counter that drops to 0x80.
void __fastcall WrongClearBuildingFlag02(CGameData* world, void*, std::uint32_t index) {
    Building& building = world->buildings[index];
    std::uint8_t flags = building.request_flags;
    if ((flags & kRequest02) == 0) {
        return;
    }
    Player& player = PlayerOf(world, building);
    flags = static_cast<std::uint8_t>(flags & ~kRequest02);
    building.request_flags = flags;
    if ((flags & kRequestBits) == 0) {
        player.settler_lists[kRequestCount] = static_cast<std::uint16_t>(player.settler_lists[kRequestCount] - 1);
    }
    player.settler_lists[kCounter02] = static_cast<std::uint16_t>(player.settler_lists[kCounter02] - 1);
}

}  // namespace

S3RE_TEST(CGameData_ClearBuildingFlag02_MatchesOriginal) {
    CompareBuildingFlag(ctx, Original_CGameData_ClearBuildingFlag02, &CGameData_ClearBuildingFlag02, 20000);
}
S3RE_TEST(CGameData_ClearBuildingFlag02_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareBuildingFlag(probe, Original_CGameData_ClearBuildingFlag02, &WrongClearBuildingFlag02, 5000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_ClearBuildingFlag04_MatchesOriginal) {
    CompareBuildingFlag(ctx, Original_CGameData_ClearBuildingFlag04, &CGameData_ClearBuildingFlag04, 20000);
}
S3RE_TEST(CGameData_ClearBuildingFlag08_MatchesOriginal) {
    CompareBuildingFlag(ctx, Original_CGameData_ClearBuildingFlag08, &CGameData_ClearBuildingFlag08, 20000);
}
S3RE_TEST(CGameData_ClearBuildingFlag10_MatchesOriginal) {
    CompareBuildingFlag(ctx, Original_CGameData_ClearBuildingFlag10, &CGameData_ClearBuildingFlag10, 20000);
}
S3RE_TEST(CGameData_SetBuildingFlag02_MatchesOriginal) {
    CompareBuildingFlag(ctx, Original_CGameData_SetBuildingFlag02, &CGameData_SetBuildingFlag02, 20000);
}
S3RE_TEST(CGameData_SetBuildingFlag04_MatchesOriginal) {
    CompareBuildingFlag(ctx, Original_CGameData_SetBuildingFlag04, &CGameData_SetBuildingFlag04, 20000);
}
S3RE_TEST(CGameData_SetBuildingFlag08_MatchesOriginal) {
    CompareBuildingFlag(ctx, Original_CGameData_SetBuildingFlag08, &CGameData_SetBuildingFlag08, 20000);
}
S3RE_TEST(CGameData_SetBuildingFlag08_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareBuildingFlag(probe, Original_CGameData_SetBuildingFlag08, &WrongSetBuildingFlag08, 5000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_SetBuildingFlag10_MatchesOriginal) {
    CompareBuildingFlag(ctx, Original_CGameData_SetBuildingFlag10, &CGameData_SetBuildingFlag10, 20000);
}

}  // namespace s3re::game
