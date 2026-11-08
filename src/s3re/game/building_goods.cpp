// Per-building counters for the goods a building takes in.
//
// Building records: 0x3a bytes at world+0x1499e64 (+0x6 type, +0x21 a byte
// that is non-zero while the building is a construction site). A finished
// building takes up to three input goods, listed per building type in the
// table at 0x006a6824 in S3.EXE's data (3 int32 per type, e.g. type 0x1f:
// goods 1, 11, 2); a construction site takes goods 1 and 2 (probably
// boards and stones). Each input slot has two uint16 counters: +0x24/+0x26/
// +0x28 ("word 24") and +0x2a/+0x2c/+0x2e ("word 2A"); their meaning is not
// settled (stock and requested amount are likely).
//
// The slot of goods g in building b: on a construction site slot 0 if g == 1,
// slot 1 if g == 2; otherwise slot i if g == table[type][i] (first match).
// No slot: DebugLog("TROUBLE: illegal goodcheck N at building %u %u", g,
// type). Goods are compared as full 32-bit values.
//
// GameWorld::BuildingInputWord2A (0x004df150): (building, goods) -> the
//   slot's word 2A, zero-extended; 0 if there is no slot. ret 8.
// GameWorld::BuildingInputWord24 (0x004df220): the same for word 24.
//   ret 8.
// GameWorld::SetBuildingInputWord2A (0x004df2f0): (building, goods, value):
//   sets the slot's word 2A to the low word of value; nothing if no slot.
//   EAX (one caller tail-returns it) = world + building*0x3a. ret 0xc.
// GameWorld::SetBuildingInputWord24 (0x004df3c0): the same for word 24.
//   ret 0xc.
// All thiscall (ECX = world).

#include "game/debug_log.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstdint>

namespace s3re::game {

namespace {

constexpr std::uint32_t kBuildings = 0x1499e64;
constexpr std::uint32_t kBuildingBytes = 0x3a;
constexpr std::uint32_t kBuildingType = 0x6;
constexpr std::uint32_t kBuildingConstruction = 0x21;
constexpr std::uint32_t kBuildingWord24 = 0x24;
constexpr std::uint32_t kBuildingWord2A = 0x2a;

// int32[type][3]: the input goods of each building type, in S3.EXE.
const std::int32_t (*const kInputGoods)[3] = reinterpret_cast<const std::int32_t (*)[3]>(0x006a6824);

// "TROUBLE: illegal goodcheck 1..4 at building %u %u" in S3.EXE.
constexpr std::uintptr_t kTroubleGet2A = 0x006a59ac;
constexpr std::uintptr_t kTroubleGet24 = 0x006a59dc;
constexpr std::uintptr_t kTroubleSet2A = 0x006a5a0c;
constexpr std::uintptr_t kTroubleSet24 = 0x006a5a3c;

constexpr int kNoSlot = -1;

std::uint8_t* Record(GameWorld* world, std::uint32_t building) {
    return reinterpret_cast<std::uint8_t*>(world) + kBuildings + building * kBuildingBytes;
}

int InputSlot(const std::uint8_t* record, std::uint32_t goods) {
    if (record[kBuildingConstruction] != 0) {
        return goods == 1 ? 0 : goods == 2 ? 1 : kNoSlot;
    }
    const std::int32_t* inputs = kInputGoods[record[kBuildingType]];
    for (int slot = 0; slot < 3; ++slot) {
        if (goods == static_cast<std::uint32_t>(inputs[slot])) {
            return slot;
        }
    }
    return kNoSlot;
}

std::uint16_t& Word(std::uint8_t* record, std::uint32_t base, int slot) {
    return *reinterpret_cast<std::uint16_t*>(record + base + slot * 2);
}

std::uint32_t GetWord(GameWorld* world, std::uint32_t building, std::uint32_t goods, std::uint32_t base,
                      std::uintptr_t trouble) {
    std::uint8_t* record = Record(world, building);
    const int slot = InputSlot(record, goods);
    if (slot == kNoSlot) {
        GameDebugLog(trouble, goods, static_cast<std::uint32_t>(record[kBuildingType]));
        return 0;
    }
    return Word(record, base, slot);
}

std::uint32_t SetWord(GameWorld* world, std::uint32_t building, std::uint32_t goods, std::uint32_t value,
                      std::uint32_t base, std::uintptr_t trouble) {
    std::uint8_t* record = Record(world, building);
    const int slot = InputSlot(record, goods);
    if (slot == kNoSlot) {
        GameDebugLog(trouble, goods, static_cast<std::uint32_t>(record[kBuildingType]));
    } else {
        Word(record, base, slot) = static_cast<std::uint16_t>(value);
    }
    // world + building * 0x3a, computed in 32 bits as the original does.
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world)) + building * kBuildingBytes;
}

}  // namespace

using GetInputFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t building,
                                              std::uint32_t goods);
using SetInputFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t building,
                                              std::uint32_t goods, std::uint32_t value);

std::uint32_t __fastcall GameWorld_BuildingInputWord2A(GameWorld* world, void*, std::uint32_t building,
                                                       std::uint32_t goods) {
    return GetWord(world, building, goods, kBuildingWord2A, kTroubleGet2A);
}

std::uint32_t __fastcall GameWorld_BuildingInputWord24(GameWorld* world, void*, std::uint32_t building,
                                                       std::uint32_t goods) {
    return GetWord(world, building, goods, kBuildingWord24, kTroubleGet24);
}

std::uint32_t __fastcall GameWorld_SetBuildingInputWord2A(GameWorld* world, void*, std::uint32_t building,
                                                          std::uint32_t goods, std::uint32_t value) {
    return SetWord(world, building, goods, value, kBuildingWord2A, kTroubleSet2A);
}

std::uint32_t __fastcall GameWorld_SetBuildingInputWord24(GameWorld* world, void*, std::uint32_t building,
                                                          std::uint32_t goods, std::uint32_t value) {
    return SetWord(world, building, goods, value, kBuildingWord24, kTroubleSet24);
}

S3RE_HOOK(0x004df150, GameWorld_BuildingInputWord2A, GetInputFn, Original_GameWorld_BuildingInputWord2A,
          GameWorld_BuildingInputWord2A);
S3RE_HOOK(0x004df220, GameWorld_BuildingInputWord24, GetInputFn, Original_GameWorld_BuildingInputWord24,
          GameWorld_BuildingInputWord24);
S3RE_HOOK(0x004df2f0, GameWorld_SetBuildingInputWord2A, SetInputFn, Original_GameWorld_SetBuildingInputWord2A,
          GameWorld_SetBuildingInputWord2A);
S3RE_HOOK(0x004df3c0, GameWorld_SetBuildingInputWord24, SetInputFn, Original_GameWorld_SetBuildingInputWord24,
          GameWorld_SetBuildingInputWord24);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestBuildings = 4096;
constexpr std::size_t kTestWorld = kBuildings + (kTestBuildings + 1) * kBuildingBytes;

// A building of a random type (all 0x50 types; most have no inputs), often a
// construction site, and goods that often match one of its inputs.
std::uint32_t SetUpBuilding(test::Context& ctx, test::WorldPair& worlds, std::uint32_t& goods) {
    const std::uint32_t building = ctx.rng().Below(kTestBuildings);
    const std::uint32_t record = kBuildings + building * kBuildingBytes;
    const std::uint8_t type = static_cast<std::uint8_t>(ctx.rng().Chance(1, 16) ? ctx.rng().Below(256) : ctx.rng().Below(0x50));
    worlds.Set<std::uint8_t>(record + kBuildingType, type);
    worlds.Set<std::uint8_t>(record + kBuildingConstruction,
                             static_cast<std::uint8_t>(ctx.rng().Chance(1, 3) ? 1 + ctx.rng().Below(255) : 0));
    switch (ctx.rng().Below(4)) {
    case 0:
        goods = ctx.rng().Next32();
        break;
    case 1:
        goods = ctx.rng().Below(4);
        break;
    default:
        goods = static_cast<std::uint32_t>(kInputGoods[type][ctx.rng().Below(3)]);
        break;
    }
    return building;
}

// Wrong: on a construction site, looks the goods up in the type table too.
std::uint32_t __fastcall WrongBuildingInputWord2A(GameWorld* world, void* edx, std::uint32_t building,
                                                  std::uint32_t goods) {
    std::uint8_t* record = Record(world, building);
    const std::uint8_t construction = record[kBuildingConstruction];
    if (construction != 0 && goods != 1 && goods != 2) {
        record[kBuildingConstruction] = 0;
        const std::uint32_t result = GameWorld_BuildingInputWord2A(world, edx, building, goods);
        record[kBuildingConstruction] = construction;
        return result;
    }
    return GameWorld_BuildingInputWord2A(world, edx, building, goods);
}

void CompareGet(test::Context& ctx, GetInputFn original, GetInputFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        std::uint32_t goods = 0;
        const std::uint32_t building = SetUpBuilding(ctx, worlds, goods);
        const std::uint32_t expected = original(worlds.original(), nullptr, building, goods);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, building, goods);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 512 == 511) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: building %u, goods %u", i, building, goods);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

std::uint32_t Relative(std::uint32_t value, GameWorld* world) {
    return value - static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));
}

void CompareSet(test::Context& ctx, SetInputFn original, SetInputFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        std::uint32_t goods = 0;
        const std::uint32_t building = SetUpBuilding(ctx, worlds, goods);
        const std::uint32_t value = ctx.rng().Next32();
        const std::uint32_t expected = Relative(original(worlds.original(), nullptr, building, goods, value), worlds.original());
        const std::uint32_t actual = Relative(candidate(worlds.ours(), nullptr, building, goods, value), worlds.ours());
        if (!S3RE_EXPECT_EQ(expected, actual) ||
            !worlds.Same(ctx, kBuildings + building * kBuildingBytes, kBuildingBytes) ||
            ((i % 512 == 511) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: building %u, goods %u", i, building, goods);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

}  // namespace

S3RE_TEST(GameWorld_BuildingInputWord2A_MatchesOriginal) {
    CompareGet(ctx, Original_GameWorld_BuildingInputWord2A, &GameWorld_BuildingInputWord2A, 20000);
}
S3RE_TEST(GameWorld_BuildingInputWord2A_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareGet(probe, Original_GameWorld_BuildingInputWord2A, &WrongBuildingInputWord2A, 5000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_BuildingInputWord24_MatchesOriginal) {
    CompareGet(ctx, Original_GameWorld_BuildingInputWord24, &GameWorld_BuildingInputWord24, 20000);
}
S3RE_TEST(GameWorld_SetBuildingInputWord2A_MatchesOriginal) {
    CompareSet(ctx, Original_GameWorld_SetBuildingInputWord2A, &GameWorld_SetBuildingInputWord2A, 20000);
}
S3RE_TEST(GameWorld_SetBuildingInputWord24_MatchesOriginal) {
    CompareSet(ctx, Original_GameWorld_SetBuildingInputWord24, &GameWorld_SetBuildingInputWord24, 20000);
}

}  // namespace s3re::game
