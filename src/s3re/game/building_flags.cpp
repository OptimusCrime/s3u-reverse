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
//     (world+0x123885c) selected by word +0x10;
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

#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace s3re::game {

namespace {

constexpr std::size_t kBuildings = 0x1499e64;
constexpr std::size_t kBuildingBytes = 0x3a;
constexpr std::size_t kBuildingPlayer = 0x0;
constexpr std::size_t kBuildingTableIndexA = 0x10;  // uint16
constexpr std::size_t kBuildingTableIndexB = 0x12;  // uint16
constexpr std::size_t kBuildingFlags = 0x18;
constexpr std::size_t kBuildingValueA = 0x19;
constexpr std::size_t kBuildingValueB = 0x1a;
constexpr std::size_t kBuildingByte21 = 0x21;

constexpr std::uint8_t kFlagEnabled = 0x1;
constexpr std::uint8_t kRequestBits = 0x1e;

constexpr std::size_t kPlayerRequestCount = 0x24;  // uint16, in the player record
constexpr std::uint16_t kMaxRequestCount = 10;

constexpr std::size_t kTable = 0x123885c;  // 14-byte records; byte +0x1 compared
constexpr std::size_t kTableBytes = 14;

std::uint8_t* Building(GameWorld* world, std::uint32_t index) {
    return reinterpret_cast<std::uint8_t*>(world) + kBuildings + index * kBuildingBytes;
}

std::uint8_t* PlayerRecord(GameWorld* world, std::uint8_t player) {
    return reinterpret_cast<std::uint8_t*>(world) + world_offset::kPlayerRecords +
           player * world_offset::kPlayerRecordBytes;
}

std::uint16_t Word(const std::uint8_t* p) {
    std::uint16_t value;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

void SetWord(std::uint8_t* p, std::uint16_t value) {
    std::memcpy(p, &value, sizeof(value));
}

void ClearRequest(GameWorld* world, std::uint32_t index, std::uint8_t bit,
                  std::size_t counterField) {
    std::uint8_t* building = Building(world, index);
    std::uint8_t flags = building[kBuildingFlags];
    if ((flags & bit) == 0) {
        return;
    }
    std::uint8_t* player = PlayerRecord(world, building[kBuildingPlayer]);
    flags = static_cast<std::uint8_t>(flags & ~bit);
    building[kBuildingFlags] = flags;
    if ((flags & kRequestBits) == 0) {
        SetWord(player + kPlayerRequestCount,
                static_cast<std::uint16_t>(Word(player + kPlayerRequestCount) - 1));
    }
    const auto counter = static_cast<std::uint16_t>(Word(player + counterField) - 1);
    SetWord(player + counterField, counter == 0x80 ? 0 : counter);
}

enum class Condition { TableA, TableB, None, Byte21IsOne };

void SetRequest(GameWorld* world, std::uint32_t index, std::uint8_t bit,
                std::size_t counterField, Condition condition) {
    std::uint8_t* building = Building(world, index);
    const auto* base = reinterpret_cast<const std::uint8_t*>(world);
    switch (condition) {
    case Condition::TableA:
        if (building[kBuildingValueA] ==
            base[kTable + 1 + Word(building + kBuildingTableIndexA) * kTableBytes]) {
            return;
        }
        break;
    case Condition::TableB:
        if (building[kBuildingValueB] ==
            base[kTable + 1 + Word(building + kBuildingTableIndexB) * kTableBytes]) {
            return;
        }
        break;
    case Condition::Byte21IsOne:
        if (building[kBuildingByte21] != 1) {
            return;
        }
        break;
    case Condition::None:
        break;
    }
    std::uint8_t* player = PlayerRecord(world, building[kBuildingPlayer]);
    const std::uint16_t requests = Word(player + kPlayerRequestCount);
    if (requests >= kMaxRequestCount || (building[kBuildingFlags] & kFlagEnabled) == 0) {
        return;
    }
    if ((building[kBuildingFlags] & kRequestBits) == 0) {
        SetWord(player + kPlayerRequestCount, static_cast<std::uint16_t>(requests + 1));
    }
    if ((building[kBuildingFlags] & bit) != 0) {
        return;
    }
    building[kBuildingFlags] = static_cast<std::uint8_t>(building[kBuildingFlags] | bit);
    SetWord(player + counterField, static_cast<std::uint16_t>((Word(player + counterField) | 0x80) + 1));
}

}  // namespace

using BuildingFn = void(__fastcall*)(GameWorld* world, void* edx, std::uint32_t building);

// Purpose: Withdraws one kind of request a building has made (one of four request bits), keeping the
// player's request counters right. Names are provisional.
void __fastcall GameWorld_ClearBuildingFlag02(GameWorld* world, void*, std::uint32_t b) {
    ClearRequest(world, b, 0x02, 0x20);
}
// Purpose: Withdraws another kind of building request (see ClearBuildingFlag02).
void __fastcall GameWorld_ClearBuildingFlag04(GameWorld* world, void*, std::uint32_t b) {
    ClearRequest(world, b, 0x04, 0x22);
}
// Purpose: Withdraws another kind of building request (see ClearBuildingFlag02).
void __fastcall GameWorld_ClearBuildingFlag08(GameWorld* world, void*, std::uint32_t b) {
    ClearRequest(world, b, 0x08, 0x26);
}
// Purpose: Withdraws another kind of building request (see ClearBuildingFlag02).
void __fastcall GameWorld_ClearBuildingFlag10(GameWorld* world, void*, std::uint32_t b) {
    ClearRequest(world, b, 0x10, 0x28);
}
// Purpose: Makes one kind of request for a building (e.g. 'needs a worker'), if the player doesn't have too
// many open already, and counts it. Names are provisional.
void __fastcall GameWorld_SetBuildingFlag02(GameWorld* world, void*, std::uint32_t b) {
    SetRequest(world, b, 0x02, 0x20, Condition::TableA);
}
// Purpose: Makes another kind of building request (see SetBuildingFlag02).
void __fastcall GameWorld_SetBuildingFlag04(GameWorld* world, void*, std::uint32_t b) {
    SetRequest(world, b, 0x04, 0x22, Condition::TableB);
}
// Purpose: Makes another kind of building request (see SetBuildingFlag02).
void __fastcall GameWorld_SetBuildingFlag08(GameWorld* world, void*, std::uint32_t b) {
    SetRequest(world, b, 0x08, 0x26, Condition::None);
}
// Purpose: Makes another kind of building request (see SetBuildingFlag02).
void __fastcall GameWorld_SetBuildingFlag10(GameWorld* world, void*, std::uint32_t b) {
    SetRequest(world, b, 0x10, 0x28, Condition::Byte21IsOne);
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
constexpr std::size_t kTestWorld = kBuildings + (kTestBuildings + 1) * kBuildingBytes + 0x100;

// Runs random cases of one setter or clearer on biased building and player
// state, comparing the building and player records after every case and the
// whole world now and then.
void CompareBuildingFlag(test::Context& ctx, BuildingFn original, BuildingFn candidate,
                         int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t index = 1 + ctx.rng().Below(kTestBuildings);
        const std::size_t b = kBuildings + index * kBuildingBytes;
        const std::uint8_t player = static_cast<std::uint8_t>(
            ctx.rng().Chance(1, 20) ? ctx.rng().Below(256) : ctx.rng().Below(20));
        worlds.Set<std::uint8_t>(b + kBuildingPlayer, player);
        worlds.Set<std::uint8_t>(b + kBuildingFlags,
                                 static_cast<std::uint8_t>(ctx.rng().Next32() &
                                                           (ctx.rng().Chance(1, 2) ? 0x1f : 0xff)));
        // Table lookups that match half the time.
        for (std::size_t field : {kBuildingTableIndexA, kBuildingTableIndexB}) {
            const auto tableIndex = static_cast<std::uint16_t>(ctx.rng().Next32());
            worlds.Set<std::uint16_t>(b + field, tableIndex);
            const std::size_t valueField =
                field == kBuildingTableIndexA ? kBuildingValueA : kBuildingValueB;
            const std::uint8_t value = static_cast<std::uint8_t>(ctx.rng().Below(4));
            worlds.Set<std::uint8_t>(b + valueField, value);
            worlds.Set<std::uint8_t>(kTable + 1 + tableIndex * kTableBytes,
                                     ctx.rng().Chance(1, 2) ? value
                                                            : static_cast<std::uint8_t>(ctx.rng().Below(4)));
        }
        worlds.Set<std::uint8_t>(b + kBuildingByte21, static_cast<std::uint8_t>(ctx.rng().Below(3)));
        // Counters around the interesting values.
        const std::size_t p = world_offset::kPlayerRecords + player * world_offset::kPlayerRecordBytes;
        worlds.Set<std::uint16_t>(p + kPlayerRequestCount,
                                  static_cast<std::uint16_t>(ctx.rng().Chance(1, 8)
                                                                 ? ctx.rng().Next32()
                                                                 : 7 + ctx.rng().Below(5)));
        for (std::size_t counter : {0x20u, 0x22u, 0x26u, 0x28u}) {
            worlds.Set<std::uint16_t>(p + counter,
                                      static_cast<std::uint16_t>(ctx.rng().Chance(1, 4)
                                                                     ? ctx.rng().Next32()
                                                                     : 0x7e + ctx.rng().Below(5)));
        }

        original(worlds.original(), nullptr, index);
        candidate(worlds.ours(), nullptr, index);
        bool same = worlds.Same(ctx, b, kBuildingBytes) &&
                    worlds.Same(ctx, p, world_offset::kPlayerRecordBytes);
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
    std::uint8_t* building = Building(world, index);
    std::uint8_t* player = PlayerRecord(world, building[kBuildingPlayer]);
    const std::uint16_t requests = Word(player + kPlayerRequestCount);
    if (requests >= kMaxRequestCount || (building[kBuildingFlags] & kFlagEnabled) == 0) {
        return;
    }
    if ((building[kBuildingFlags] & kRequestBits) == 0) {
        SetWord(player + kPlayerRequestCount, static_cast<std::uint16_t>(requests + 1));
    }
    if ((building[kBuildingFlags] & 0x08) != 0) {
        return;
    }
    building[kBuildingFlags] = static_cast<std::uint8_t>(building[kBuildingFlags] | 0x08);
    SetWord(player + 0x26, static_cast<std::uint16_t>(Word(player + 0x26) + 1));
}

// Wrong: no reset of a counter that drops to 0x80.
void __fastcall WrongClearBuildingFlag02(GameWorld* world, void*, std::uint32_t index) {
    std::uint8_t* building = Building(world, index);
    std::uint8_t flags = building[kBuildingFlags];
    if ((flags & 0x02) == 0) {
        return;
    }
    std::uint8_t* player = PlayerRecord(world, building[kBuildingPlayer]);
    flags = static_cast<std::uint8_t>(flags & ~0x02);
    building[kBuildingFlags] = flags;
    if ((flags & kRequestBits) == 0) {
        SetWord(player + kPlayerRequestCount,
                static_cast<std::uint16_t>(Word(player + kPlayerRequestCount) - 1));
    }
    SetWord(player + 0x20, static_cast<std::uint16_t>(Word(player + 0x20) - 1));
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
