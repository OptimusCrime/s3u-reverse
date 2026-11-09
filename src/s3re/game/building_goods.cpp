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
//
// Requests: area records (0x2e0 bytes at world+0xe145e8) keep, per goods
// type (36), the head of a list of buildings that want those goods (uint16
// at area +0x52 + goods*2); word 2A of the building's input slot links to
// the next building, word 24 holds the requested amount. The area is the
// one of the building's tile B (+0xa/+0xc, area word at tile +0x14).
//
// GameWorld::AddBuildingGoodsRequest (0x004ddf30): (building, goods,
//   amount, condition): unless condition != 0 and the area's byte +0x245 +
//   condition is 0, or amount is 0: puts the building at the head of the
//   area's list for the goods (SetBuildingInputWord2A(building, goods, old
//   head); head = building) and sets the amount (SetBuildingInputWord24).
//   EAX (some callers use AL/the value) reproduced: the area number on the
//   early returns, else SetBuildingInputWord24's EAX. ret 0x10.
// GameWorld::RemoveBuildingFromAreaRequests (0x004def80): (building): for
//   finished buildings of type 0x21 or 0x22, clears the 2 x 36 words at
//   area +0x9a and +0xe2 that equal the building; for all others, unlinks
//   the building from the area's 36 request lists (head or via the word 2A
//   of its predecessor, BuildingInputWord2A/SetBuildingInputWord2A; the
//   whole list is walked). EAX (two callers tail-return it) reproduced from
//   the last step. ret 4.
// All thiscall (ECX = world).

#include "game/debug_log.h"
#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace s3re::game {

namespace {

using layout::Area;
using layout::Building;
using layout::kGoodsTypes;

// int32[type][3]: the input goods of each building type, in S3.EXE.
const std::int32_t (*const kInputGoods)[3] = reinterpret_cast<const std::int32_t (*)[3]>(0x006a6824);

// "TROUBLE: illegal goodcheck 1..4 at building %u %u" in S3.EXE.
constexpr std::uintptr_t kTroubleGet2A = 0x006a59ac;
constexpr std::uintptr_t kTroubleGet24 = 0x006a59dc;
constexpr std::uintptr_t kTroubleSet2A = 0x006a5a0c;
constexpr std::uintptr_t kTroubleSet24 = 0x006a5a3c;

constexpr int kNoSlot = -1;

// One of the two per-slot word arrays: "word 24" (input_amount) or "word 2A"
// (input_next).
using InputWords = std::uint16_t (Building::*)[3];
constexpr InputWords kWord24 = &Building::input_amount;
constexpr InputWords kWord2A = &Building::input_next;

int InputSlot(const Building& record, std::uint32_t goods) {
    if (record.buildup != 0) {
        return goods == 1 ? 0 : goods == 2 ? 1 : kNoSlot;
    }
    const std::int32_t* inputs = kInputGoods[record.art];
    for (int slot = 0; slot < 3; ++slot) {
        if (goods == static_cast<std::uint32_t>(inputs[slot])) {
            return slot;
        }
    }
    return kNoSlot;
}

std::uint32_t GetWord(GameWorld* world, std::uint32_t building, std::uint32_t goods, InputWords words,
                      std::uintptr_t trouble) {
    Building& record = world->buildings[building];
    const int slot = InputSlot(record, goods);
    if (slot == kNoSlot) {
        GameDebugLog(trouble, goods, static_cast<std::uint32_t>(record.art));
        return 0;
    }
    return (record.*words)[slot];
}

std::uint32_t SetWord(GameWorld* world, std::uint32_t building, std::uint32_t goods, std::uint32_t value,
                      InputWords words, std::uintptr_t trouble) {
    Building& record = world->buildings[building];
    const int slot = InputSlot(record, goods);
    if (slot == kNoSlot) {
        GameDebugLog(trouble, goods, static_cast<std::uint32_t>(record.art));
    } else {
        (record.*words)[slot] = static_cast<std::uint16_t>(value);
    }
    // world + building * 0x3a, computed in 32 bits as the original does.
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world)) +
           building * static_cast<std::uint32_t>(sizeof(Building));
}

}  // namespace

using GetInputFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t building,
                                              std::uint32_t goods);
using SetInputFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t building,
                                              std::uint32_t goods, std::uint32_t value);

// Purpose: Every building takes in up to three kinds of goods (a construction site takes building
// materials). For one kind of goods, this reads the link to the next building in the area's 'wants
// these goods' list.
std::uint32_t __fastcall GameWorld_BuildingInputWord2A(GameWorld* world, void*, std::uint32_t building,
                                                       std::uint32_t goods) {
    return GetWord(world, building, goods, kWord2A, kTroubleGet2A);
}

// Purpose: For one kind of goods a building takes in, reads how many it has asked for.
std::uint32_t __fastcall GameWorld_BuildingInputWord24(GameWorld* world, void*, std::uint32_t building,
                                                       std::uint32_t goods) {
    return GetWord(world, building, goods, kWord24, kTroubleGet24);
}

// Purpose: Sets the 'next building wanting these goods' link of one of a building's inputs (see
// BuildingInputWord2A).
std::uint32_t __fastcall GameWorld_SetBuildingInputWord2A(GameWorld* world, void*, std::uint32_t building,
                                                          std::uint32_t goods, std::uint32_t value) {
    return SetWord(world, building, goods, value, kWord2A, kTroubleSet2A);
}

// Purpose: Sets how many of one kind of goods a building has asked for.
std::uint32_t __fastcall GameWorld_SetBuildingInputWord24(GameWorld* world, void*, std::uint32_t building,
                                                          std::uint32_t goods, std::uint32_t value) {
    return SetWord(world, building, goods, value, kWord24, kTroubleSet24);
}

S3RE_HOOK(0x004df150, GameWorld_BuildingInputWord2A, GetInputFn, Original_GameWorld_BuildingInputWord2A,
          GameWorld_BuildingInputWord2A);
S3RE_HOOK(0x004df220, GameWorld_BuildingInputWord24, GetInputFn, Original_GameWorld_BuildingInputWord24,
          GameWorld_BuildingInputWord24);
S3RE_HOOK(0x004df2f0, GameWorld_SetBuildingInputWord2A, SetInputFn, Original_GameWorld_SetBuildingInputWord2A,
          GameWorld_SetBuildingInputWord2A);
S3RE_HOOK(0x004df3c0, GameWorld_SetBuildingInputWord24, SetInputFn, Original_GameWorld_SetBuildingInputWord24,
          GameWorld_SetBuildingInputWord24);

namespace {

// The area of the building's tile B (door_x, door_y), with the original's
// 32-bit tile arithmetic.
std::uint32_t BuildingArea(GameWorld* world, std::uint32_t building) {
    const Building& record = world->buildings[building];
    const std::uint32_t x = record.door_x;
    const std::uint32_t y = record.door_y;
    return TileAtOffset(world, TileOffset(x, y)).area;
}

// The head of the area's request list for the goods. The original computes
// its offset as 0xe145e8 + 0x52 + ((area * 0x170 + goods) * 2) in 32 bits;
// indexing gives the same address for any area and goods.
std::uint16_t& RequestHead(GameWorld* world, std::uint32_t area, std::uint32_t goods) {
    return world->areas[area].requests_by_goods[goods];
}

std::uint32_t Address(const void* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
}

}  // namespace

using AddRequestFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t building,
                                                std::uint32_t goods, std::uint32_t amount, std::uint32_t condition);
using RemoveRequestsFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t building);

// Purpose: A building asks for goods: it is added to its area's list of buildings that want that kind of
// goods, together with the amount. Carriers serve these lists.
std::uint32_t __fastcall GameWorld_AddBuildingGoodsRequest(GameWorld* world, void*, std::uint32_t building,
                                                           std::uint32_t goods, std::uint32_t amount,
                                                           std::uint32_t condition) {
    const std::uint32_t area = BuildingArea(world, building);
    if (condition != 0 && world->areas[area].condition_enabled[condition] == 0) {
        return area;
    }
    if (amount == 0) {
        return area;
    }
    std::uint16_t& head = RequestHead(world, area, goods);
    GameWorld_SetBuildingInputWord2A(world, nullptr, building, goods, head);
    head = static_cast<std::uint16_t>(building);
    return GameWorld_SetBuildingInputWord24(world, nullptr, building, goods, amount);
}

// Purpose: Takes a building off all of its area's 'wants goods' lists (and some special lists for two
// building types). Used when a building no longer needs goods or is removed.
std::uint32_t __fastcall GameWorld_RemoveBuildingFromAreaRequests(GameWorld* world, void*, std::uint32_t building) {
    const std::uint32_t area = BuildingArea(world, building);
    const Building& record = world->buildings[building];
    const std::uint8_t type = record.art;
    if ((type == 0x22 || type == 0x21) && record.buildup == 0) {
        Area& areaRecord = world->areas[area];
        for (std::uint32_t k = 0; k < kGoodsTypes; ++k) {
            for (std::uint16_t* words : {areaRecord.buildings_a, areaRecord.buildings_b}) {
                std::uint16_t& word = words[k];
                if (word == building) {
                    word = 0;
                }
            }
        }
        // The address just past buildings_b (the original's last pointer).
        return Address(areaRecord.buildings_b + kGoodsTypes);
    }
    std::uint32_t last = 0;
    for (std::uint32_t goods = 0; goods < kGoodsTypes; ++goods) {
        std::uint16_t& head = RequestHead(world, area, goods);
        std::uint32_t other = head;
        if (other == building) {
            last = GameWorld_BuildingInputWord2A(world, nullptr, building, goods);
            head = static_cast<std::uint16_t>(last);
        } else if (other == 0) {
            last = Address(&head);
        } else {
            do {
                if (GameWorld_BuildingInputWord2A(world, nullptr, other, goods) == building) {
                    GameWorld_SetBuildingInputWord2A(world, nullptr, other, goods,
                                                     GameWorld_BuildingInputWord2A(world, nullptr, building, goods));
                }
                other = GameWorld_BuildingInputWord2A(world, nullptr, other, goods);
            } while (other != 0);
            last = 0;
        }
    }
    return last;
}

S3RE_HOOK(0x004ddf30, GameWorld_AddBuildingGoodsRequest, AddRequestFn, Original_GameWorld_AddBuildingGoodsRequest,
          GameWorld_AddBuildingGoodsRequest);
S3RE_HOOK(0x004def80, GameWorld_RemoveBuildingFromAreaRequests, RemoveRequestsFn,
          Original_GameWorld_RemoveBuildingFromAreaRequests, GameWorld_RemoveBuildingFromAreaRequests);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestBuildings = 4096;
constexpr std::size_t kTestWorld = offsetof(GameWorld, buildings) + (kTestBuildings + 1) * sizeof(Building);

// A building of a random type (all 0x50 types; most have no inputs), often a
// construction site, and goods that often match one of its inputs.
std::uint32_t SetUpBuilding(test::Context& ctx, test::WorldPair& worlds, std::uint32_t& goods) {
    const std::uint32_t building = ctx.rng().Below(kTestBuildings);
    const std::uint8_t type = static_cast<std::uint8_t>(ctx.rng().Chance(1, 16) ? ctx.rng().Below(256) : ctx.rng().Below(0x50));
    const auto buildup = static_cast<std::uint8_t>(ctx.rng().Chance(1, 3) ? 1 + ctx.rng().Below(255) : 0);
    worlds.Both([&](GameWorld* w) {
        w->buildings[building].art = type;
        w->buildings[building].buildup = buildup;
    });
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
    Building& record = world->buildings[building];
    const std::uint8_t construction = record.buildup;
    if (construction != 0 && goods != 1 && goods != 2) {
        record.buildup = 0;
        const std::uint32_t result = GameWorld_BuildingInputWord2A(world, edx, building, goods);
        record.buildup = construction;
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
            !worlds.SameField(ctx, [&](GameWorld* w) -> auto& { return w->buildings[building]; }) ||
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

namespace {

constexpr std::uint32_t kRequestAreas = 16;
constexpr std::uint32_t kRequestBuildings = 64;

// Buildings 1..63 in a few areas (tile B per building), some construction
// sites, types with and without inputs (and 0x21/0x22); each area's request
// lists built by linking random buildings that have the goods as an input
// (acyclic: each building appears once per list).
void SetUpRequests(test::Context& ctx, test::WorldPair& worlds) {
    static constexpr std::uint8_t kTypes[] = {0x1f, 0x20, 0x07, 0x0a, 0x13, 0x21, 0x22, 0x14, 0x30, 0x47};
    for (std::uint32_t building = 1; building < kRequestBuildings; ++building) {
        const std::uint32_t x = ctx.rng().Below(384);
        const std::uint32_t y = ctx.rng().Below(384);
        const auto area = static_cast<std::uint16_t>(ctx.rng().Below(kRequestAreas));
        const std::uint8_t type = kTypes[ctx.rng().Below(sizeof(kTypes))];
        const auto buildup = static_cast<std::uint8_t>(ctx.rng().Chance(1, 4) ? 1 : 0);
        worlds.Both([&](GameWorld* w) {
            Building& record = w->buildings[building];
            record.door_x = static_cast<std::uint16_t>(x);
            record.door_y = static_cast<std::uint16_t>(y);
            w->tiles[x][y].area = area;
            record.art = type;
            record.buildup = buildup;
        });
    }
    for (std::uint32_t area = 0; area < kRequestAreas; ++area) {
        for (std::uint32_t goods = 0; goods < kGoodsTypes; ++goods) {
            std::uint32_t head = 0;
            for (std::uint32_t building = 1; building < kRequestBuildings; ++building) {
                if (BuildingArea(worlds.original(), building) != area || !ctx.rng().Chance(1, 2)) {
                    continue;
                }
                const int slot = InputSlot(worlds.original()->buildings[building], goods);
                if (slot == kNoSlot) {
                    continue;
                }
                worlds.Both([&](GameWorld* w) { w->buildings[building].input_next[slot] = static_cast<std::uint16_t>(head); });
                head = building;
            }
            const auto a = static_cast<std::uint16_t>(ctx.rng().Below(kRequestBuildings));
            const auto b = static_cast<std::uint16_t>(ctx.rng().Below(kRequestBuildings));
            worlds.Both([&](GameWorld* w) {
                w->areas[area].requests_by_goods[goods] = static_cast<std::uint16_t>(head);
                w->areas[area].buildings_a[goods] = a;
                w->areas[area].buildings_b[goods] = b;
            });
        }
    }
}

std::uint32_t RelativeToWorld(std::uint32_t value, GameWorld* world) {
    const auto base = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));
    return value >= base && value - base < kTestWorld ? value - base : value;
}

// Wrong: stops at the first match instead of walking the whole list.
std::uint32_t __fastcall WrongRemoveBuildingFromAreaRequests(GameWorld* world, void* edx, std::uint32_t building) {
    const Building& record = world->buildings[building];
    if ((record.art == 0x22 || record.art == 0x21) && record.buildup == 0) {
        return GameWorld_RemoveBuildingFromAreaRequests(world, edx, building);
    }
    // Clears the building's own link first: later predecessors keep pointing at it.
    for (std::uint32_t goods = 0; goods < kGoodsTypes; ++goods) {
        if (InputSlot(record, goods) != kNoSlot) {
            GameWorld_SetBuildingInputWord2A(world, edx, building, goods, 0);
        }
    }
    return GameWorld_RemoveBuildingFromAreaRequests(world, edx, building);
}

void CompareRemoveRequests(test::Context& ctx, RemoveRequestsFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        if (i % 8 == 0) {
            SetUpRequests(ctx, worlds);
        }
        const std::uint32_t building = 1 + ctx.rng().Below(kRequestBuildings - 1);
        const std::uint32_t expected =
            RelativeToWorld(Original_GameWorld_RemoveBuildingFromAreaRequests(worlds.original(), nullptr, building),
                            worlds.original());
        const std::uint32_t actual = RelativeToWorld(candidate(worlds.ours(), nullptr, building), worlds.ours());
        if (!S3RE_EXPECT_EQ(expected, actual) ||
            !worlds.Same(ctx, offsetof(GameWorld, areas), kRequestAreas * sizeof(Area)) ||
            !worlds.Same(ctx, offsetof(GameWorld, buildings), kRequestBuildings * sizeof(Building)) ||
            ((i % 16 == 15 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: building %u", i, building);
            return;
        }
    }
}

}  // namespace

S3RE_TEST(GameWorld_AddBuildingGoodsRequest_MatchesOriginal) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < 3000; ++i) {
        if (i % 64 == 0) {
            SetUpRequests(ctx, worlds);
        }
        const std::uint32_t building = 1 + ctx.rng().Below(kRequestBuildings - 1);
        const std::uint32_t goods = ctx.rng().Below(kGoodsTypes);
        const std::uint32_t amount = ctx.rng().Chance(1, 4) ? 0 : ctx.rng().Next32();
        const std::uint32_t condition = ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Below(64);
        const std::uint32_t area = BuildingArea(worlds.original(), building);
        const auto enabled = static_cast<std::uint8_t>(ctx.rng().Below(2));
        worlds.Both([&](GameWorld* w) { w->areas[area].condition_enabled[condition] = enabled; });
        const std::uint32_t expected = RelativeToWorld(
            Original_GameWorld_AddBuildingGoodsRequest(worlds.original(), nullptr, building, goods, amount, condition),
            worlds.original());
        const std::uint32_t actual = RelativeToWorld(
            GameWorld_AddBuildingGoodsRequest(worlds.ours(), nullptr, building, goods, amount, condition), worlds.ours());
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 64 == 63) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 63 before it)", i);
            return;
        }
    }
    worlds.Checkpoint(ctx);
}

S3RE_TEST(GameWorld_RemoveBuildingFromAreaRequests_MatchesOriginal) {
    CompareRemoveRequests(ctx, &GameWorld_RemoveBuildingFromAreaRequests, 1000);
}
S3RE_TEST(GameWorld_RemoveBuildingFromAreaRequests_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareRemoveRequests(probe, &WrongRemoveBuildingFromAreaRequests, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

// ---------------------------------------------------------------------------
// GameWorld::GoodsRequestPriority (0x004ff380): (amount, coming) -> 64000 -
// 8000 * amount - 4000 * coming (32-bit). Called three times by
// UpdatePhase2 with the amount (pile +0x1) and units on the way (+0x3) of a
// building's input pile, just before AddBuildingGoodsRequest, so probably
// the request's priority: the emptier the pile, the more urgent. ECX
// unused (overwritten). ret 8.
// ---------------------------------------------------------------------------

using PriorityFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t amount,
                                              std::uint32_t coming);

// Purpose: How urgently a building asks for more of an input good: high when its input pile is empty and
// nothing is on the way, lower for every unit there or coming (probably the request priority).
std::uint32_t __fastcall GameWorld_GoodsRequestPriority(GameWorld*, void*, std::uint32_t amount, std::uint32_t coming) {
    return 64000u - amount * 8000u - coming * 4000u;
}

S3RE_HOOK(0x004ff380, GameWorld_GoodsRequestPriority, PriorityFn, Original_GameWorld_GoodsRequestPriority,
          GameWorld_GoodsRequestPriority);

namespace {

void ComparePriority(test::Context& ctx, PriorityFn candidate) {
    for (int i = 0; i < 20000; ++i) {
        const std::uint32_t amount = i < 0x100 ? static_cast<std::uint32_t>(i) >> 4 : (ctx.rng().Chance(1, 2) ? ctx.rng().Below(256) : ctx.rng().Next32());
        const std::uint32_t coming = i < 0x100 ? static_cast<std::uint32_t>(i) & 15 : (ctx.rng().Chance(1, 2) ? ctx.rng().Below(256) : ctx.rng().Next32());
        if (!S3RE_EXPECT_EQ(Original_GameWorld_GoodsRequestPriority(nullptr, nullptr, amount, coming),
                            candidate(nullptr, nullptr, amount, coming))) {
            ctx.Fail(__FILE__, __LINE__, "  amount 0x%x, coming 0x%x", amount, coming);
            return;
        }
    }
}

// Wrong: the weights swapped.
std::uint32_t __fastcall WrongGoodsRequestPriority(GameWorld*, void*, std::uint32_t amount, std::uint32_t coming) {
    return 64000u - amount * 4000u - coming * 8000u;
}

}  // namespace

S3RE_TEST(GameWorld_GoodsRequestPriority_MatchesOriginal) {
    ComparePriority(ctx, &GameWorld_GoodsRequestPriority);
}
S3RE_TEST(GameWorld_GoodsRequestPriority_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePriority(probe, &WrongGoodsRequestPriority);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
