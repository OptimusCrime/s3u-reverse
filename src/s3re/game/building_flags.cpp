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

constexpr std::uint8_t kFlagEnabled = 0x1;
constexpr std::uint8_t kRequestBits = 0x1e;

constexpr std::uint16_t kMaxRequestCount = 10;

// The player's building request counters live in Player::settler_lists:
// [2] (+0x24) the number of buildings with requests, [0], [1], [3], [4]
// (+0x20, +0x22, +0x26, +0x28) one per request bit.
constexpr std::size_t kRequestCount = 2;

Player& PlayerOf(GameWorld* world, const Building& building) {
    return world->players[building.owner];
}

void ClearRequest(GameWorld* world, std::uint32_t index, std::uint8_t bit, std::size_t counter) {
    Building& building = world->buildings[index];
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
    player.settler_lists[counter] = value == 0x80 ? 0 : value;
}

enum class Condition { TableA, TableB, None, Byte21IsOne };

void SetRequest(GameWorld* world, std::uint32_t index, std::uint8_t bit, std::size_t counter,
                Condition condition) {
    Building& building = world->buildings[index];
    switch (condition) {
    case Condition::TableA:  // byte +0x19 against the amount of pile +0x10
        if (building.site.boards_missing == world->piles[building.piles[0]].amount) {
            return;
        }
        break;
    case Condition::TableB:  // byte +0x1a against the amount of pile +0x12
        if (building.site.stones_missing == world->piles[building.piles[1]].amount) {
            return;
        }
        break;
    case Condition::Byte21IsOne:
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
    player.settler_lists[counter] = static_cast<std::uint16_t>((player.settler_lists[counter] | 0x80) + 1);
}

}  // namespace

using BuildingFn = void(__fastcall*)(GameWorld* world, void* edx, std::uint32_t building);

// Purpose: Withdraws one kind of request a building has made (one of four request bits), keeping the
// player's request counters right. Names are provisional.
void __fastcall GameWorld_ClearBuildingFlag02(GameWorld* world, void*, std::uint32_t b) {
    ClearRequest(world, b, 0x02, 0);  // +0x20
}
// Purpose: Withdraws another kind of building request (see ClearBuildingFlag02).
void __fastcall GameWorld_ClearBuildingFlag04(GameWorld* world, void*, std::uint32_t b) {
    ClearRequest(world, b, 0x04, 1);  // +0x22
}
// Purpose: Withdraws another kind of building request (see ClearBuildingFlag02).
void __fastcall GameWorld_ClearBuildingFlag08(GameWorld* world, void*, std::uint32_t b) {
    ClearRequest(world, b, 0x08, 3);  // +0x26
}
// Purpose: Withdraws another kind of building request (see ClearBuildingFlag02).
void __fastcall GameWorld_ClearBuildingFlag10(GameWorld* world, void*, std::uint32_t b) {
    ClearRequest(world, b, 0x10, 4);  // +0x28
}
// Purpose: Makes one kind of request for a building (e.g. 'needs a worker'), if the player doesn't have too
// many open already, and counts it. Names are provisional.
void __fastcall GameWorld_SetBuildingFlag02(GameWorld* world, void*, std::uint32_t b) {
    SetRequest(world, b, 0x02, 0, Condition::TableA);  // +0x20
}
// Purpose: Makes another kind of building request (see SetBuildingFlag02).
void __fastcall GameWorld_SetBuildingFlag04(GameWorld* world, void*, std::uint32_t b) {
    SetRequest(world, b, 0x04, 1, Condition::TableB);  // +0x22
}
// Purpose: Makes another kind of building request (see SetBuildingFlag02).
void __fastcall GameWorld_SetBuildingFlag08(GameWorld* world, void*, std::uint32_t b) {
    SetRequest(world, b, 0x08, 3, Condition::None);  // +0x26
}
// Purpose: Makes another kind of building request (see SetBuildingFlag02).
void __fastcall GameWorld_SetBuildingFlag10(GameWorld* world, void*, std::uint32_t b) {
    SetRequest(world, b, 0x10, 4, Condition::Byte21IsOne);  // +0x28
}

S3RE_HOOK(0x004df490, GameWorld_ClearBuildingFlag02, BuildingFn,
          Original_GameWorld_ClearBuildingFlag02, GameWorld_ClearBuildingFlag02);
S3RE_HOOK(0x004df510, GameWorld_ClearBuildingFlag04, BuildingFn,
          Original_GameWorld_ClearBuildingFlag04, GameWorld_ClearBuildingFlag04);
S3RE_HOOK(0x004df590, GameWorld_ClearBuildingFlag08, BuildingFn,
          Original_GameWorld_ClearBuildingFlag08, GameWorld_ClearBuildingFlag08);
S3RE_HOOK(0x004df610, GameWorld_ClearBuildingFlag10, BuildingFn,
          Original_GameWorld_ClearBuildingFlag10, GameWorld_ClearBuildingFlag10);
S3RE_HOOK(0x004df690, GameWorld_SetBuildingFlag02, BuildingFn,
          Original_GameWorld_SetBuildingFlag02, GameWorld_SetBuildingFlag02);
S3RE_HOOK(0x004df730, GameWorld_SetBuildingFlag04, BuildingFn,
          Original_GameWorld_SetBuildingFlag04, GameWorld_SetBuildingFlag04);
S3RE_HOOK(0x004df7d0, GameWorld_SetBuildingFlag08, BuildingFn,
          Original_GameWorld_SetBuildingFlag08, GameWorld_SetBuildingFlag08);
S3RE_HOOK(0x004df850, GameWorld_SetBuildingFlag10, BuildingFn,
          Original_GameWorld_SetBuildingFlag10, GameWorld_SetBuildingFlag10);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestBuildings = 2000;
constexpr std::size_t kTestWorld = offsetof(GameWorld, buildings) + (kTestBuildings + 1) * sizeof(Building) + 0x100;

// Runs random cases of one setter or clearer on biased building and player
// state, comparing the building and player records after every case and the
// whole world now and then.
void CompareBuildingFlag(test::Context& ctx, BuildingFn original, BuildingFn candidate,
                         int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t index = 1 + ctx.rng().Below(kTestBuildings);
        auto building = [&](GameWorld* w) -> Building& { return w->buildings[index]; };
        const std::uint8_t player = static_cast<std::uint8_t>(
            ctx.rng().Chance(1, 20) ? ctx.rng().Below(256) : ctx.rng().Below(20));
        const auto flags = static_cast<std::uint8_t>(ctx.rng().Next32() & (ctx.rng().Chance(1, 2) ? 0x1f : 0xff));
        worlds.Both([&](GameWorld* w) {
            building(w).owner = player;
            building(w).request_flags = flags;
        });
        // Pile lookups that match half the time.
        for (int which = 0; which < 2; ++which) {
            const auto pile = static_cast<std::uint16_t>(ctx.rng().Next32());
            const std::uint8_t value = static_cast<std::uint8_t>(ctx.rng().Below(4));
            const std::uint8_t amount =
                ctx.rng().Chance(1, 2) ? value : static_cast<std::uint8_t>(ctx.rng().Below(4));
            worlds.Both([&](GameWorld* w) {
                building(w).piles[which] = pile;
                (which == 0 ? building(w).site.boards_missing : building(w).site.stones_missing) = value;
                w->piles[pile].amount = amount;
            });
        }
        const auto buildup = static_cast<std::uint8_t>(ctx.rng().Below(3));
        worlds.Both([&](GameWorld* w) { building(w).buildup = buildup; });
        // Counters around the interesting values.
        const auto requests =
            static_cast<std::uint16_t>(ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : 7 + ctx.rng().Below(5));
        worlds.Both([&](GameWorld* w) { w->players[player].settler_lists[kRequestCount] = requests; });
        for (std::size_t counter : {0u, 1u, 3u, 4u}) {
            const auto value =
                static_cast<std::uint16_t>(ctx.rng().Chance(1, 4) ? ctx.rng().Next32() : 0x7e + ctx.rng().Below(5));
            worlds.Both([&](GameWorld* w) { w->players[player].settler_lists[counter] = value; });
        }

        original(worlds.original(), nullptr, index);
        candidate(worlds.ours(), nullptr, index);
        bool same = worlds.SameField(ctx, building) &&
                    worlds.SameField(ctx, [&](GameWorld* w) -> auto& { return w->players[player]; });
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
void __fastcall WrongSetBuildingFlag08(GameWorld* world, void*, std::uint32_t index) {
    Building& building = world->buildings[index];
    Player& player = PlayerOf(world, building);
    const std::uint16_t requests = player.settler_lists[kRequestCount];
    if (requests >= kMaxRequestCount || (building.request_flags & kFlagEnabled) == 0) {
        return;
    }
    if ((building.request_flags & kRequestBits) == 0) {
        player.settler_lists[kRequestCount] = static_cast<std::uint16_t>(requests + 1);
    }
    if ((building.request_flags & 0x08) != 0) {
        return;
    }
    building.request_flags = static_cast<std::uint8_t>(building.request_flags | 0x08);
    player.settler_lists[3] = static_cast<std::uint16_t>(player.settler_lists[3] + 1);
}

// Wrong: no reset of a counter that drops to 0x80.
void __fastcall WrongClearBuildingFlag02(GameWorld* world, void*, std::uint32_t index) {
    Building& building = world->buildings[index];
    std::uint8_t flags = building.request_flags;
    if ((flags & 0x02) == 0) {
        return;
    }
    Player& player = PlayerOf(world, building);
    flags = static_cast<std::uint8_t>(flags & ~0x02);
    building.request_flags = flags;
    if ((flags & kRequestBits) == 0) {
        player.settler_lists[kRequestCount] = static_cast<std::uint16_t>(player.settler_lists[kRequestCount] - 1);
    }
    player.settler_lists[0] = static_cast<std::uint16_t>(player.settler_lists[0] - 1);
}

}  // namespace

S3RE_TEST(GameWorld_ClearBuildingFlag02_MatchesOriginal) {
    CompareBuildingFlag(ctx, Original_GameWorld_ClearBuildingFlag02, &GameWorld_ClearBuildingFlag02, 20000);
}
S3RE_TEST(GameWorld_ClearBuildingFlag02_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareBuildingFlag(probe, Original_GameWorld_ClearBuildingFlag02, &WrongClearBuildingFlag02, 5000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_ClearBuildingFlag04_MatchesOriginal) {
    CompareBuildingFlag(ctx, Original_GameWorld_ClearBuildingFlag04, &GameWorld_ClearBuildingFlag04, 20000);
}
S3RE_TEST(GameWorld_ClearBuildingFlag08_MatchesOriginal) {
    CompareBuildingFlag(ctx, Original_GameWorld_ClearBuildingFlag08, &GameWorld_ClearBuildingFlag08, 20000);
}
S3RE_TEST(GameWorld_ClearBuildingFlag10_MatchesOriginal) {
    CompareBuildingFlag(ctx, Original_GameWorld_ClearBuildingFlag10, &GameWorld_ClearBuildingFlag10, 20000);
}
S3RE_TEST(GameWorld_SetBuildingFlag02_MatchesOriginal) {
    CompareBuildingFlag(ctx, Original_GameWorld_SetBuildingFlag02, &GameWorld_SetBuildingFlag02, 20000);
}
S3RE_TEST(GameWorld_SetBuildingFlag04_MatchesOriginal) {
    CompareBuildingFlag(ctx, Original_GameWorld_SetBuildingFlag04, &GameWorld_SetBuildingFlag04, 20000);
}
S3RE_TEST(GameWorld_SetBuildingFlag08_MatchesOriginal) {
    CompareBuildingFlag(ctx, Original_GameWorld_SetBuildingFlag08, &GameWorld_SetBuildingFlag08, 20000);
}
S3RE_TEST(GameWorld_SetBuildingFlag08_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareBuildingFlag(probe, Original_GameWorld_SetBuildingFlag08, &WrongSetBuildingFlag08, 5000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_SetBuildingFlag10_MatchesOriginal) {
    CompareBuildingFlag(ctx, Original_GameWorld_SetBuildingFlag10, &GameWorld_SetBuildingFlag10, 20000);
}

}  // namespace s3re::game
