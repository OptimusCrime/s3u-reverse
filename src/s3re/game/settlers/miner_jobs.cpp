// Mine and terrain helpers of the settler jobs (UpdateSettlers callees,
// package B).
//
// CGameData::TileSlope (0x004faa70): (x, y) -> the steepness at a tile:
//   the largest absolute height difference (tile byte +0x4) between
//   opposite neighbours: (x+1, y) / (x-1, y), (x, y+1) / (x, y-1),
//   (x+1, y+1) / (x-1, y-1). 0 unless 1 <= x, y < map size - 1 (unsigned;
//   map size 0 makes the limit 0xffffffff). Callers compare it with 7
//   (UpdateSettlers' work jobs, the AI 0x5540f0, 0x599a30). ret 8.
// The mines (building types): coal 7, gold 8, iron 9, gem 37 (0x25),
// sulfur 29 (0x1d) and the Amazon sulfur mine 56 (0x38), which behaves like
// the sulfur mine. Each has its own four miner jobs (MinerEat*, MinerDig*,
// MinerFoundNothing*, MinerFound*; re/enums.md).
//
// CGameData::MinerJobEnter (0x004f9a00): (settler): a miner goes into his
//   mine: job (+0xe) by the mine's building type: coal -> MinerEatCoalMine
//   (85, 0x55), gold -> 86 (0x56), iron -> 87 (0x57), gem -> 216 (0xd8),
//   sulfur -> 217 (0xd9), others unchanged (jump table); the building's
//   byte +0x19 += 1 (miners inside?), byte +0x1a = 0, words +0x30/+0x32 =
//   0; the settler leaves its tile (tile settler word = 0), counter +0xc =
//   0, byte +0x17 = 10. EAX = world + settler * 0x40 (the original's
//   register; UpdateSettlers overwrites it). ret 4. Caller: UpdateSettlers
//   (the MinerEnter* jobs of the miner, settler type 10).
// CGameData::MinerJobFood (0x004f9b80): (settler): if the miner's food
//   level (byte +0x17) is 0, he eats from the mine's input slots in the
//   mine type's order (coal: 3, 2, 1; gold: 1, 3, 2; iron: 2, 1, 3; gem:
//   1, 3, 2; sulfur: 1, 2, only two tries): the first slot with goods
//   (BuildingPileHasGoods) gives one unit (PileIncAmountLeaving,
//   TakeGoodFromPile, SubtractPlayerCounter of the pile's goods type),
//   g_playerTallies[owner].meals += 1, food level += 10 for the first slot
//   or 2 for the others, 4 more for computer players (player +0x0 == 2).
//   Then, if building flag 0x1 of +0x18 is set, he has food and the
//   building's output pile (+0xe) holds at most 7 units: food level -= 1,
//   job = the dig job of the mine type (MinerDig*: 88/92/96/218/222,
//   0x58/0x5c/0x60/0xda/0xde), counter 100; else counter 10. EAX unused.
//   ret 4.
// CGameData::MinerJobDig (0x004fa220): (settler): one dig. A tile of the
//   offset table entry Random() & 0x3f around the mine (building +0x2/+0x4)
//   is on the map and holds the mine type's resource (tile +0x11 high
//   nibble: coal 0x10, iron 0x20, gold 0x30, gems 0x40, sulfur 0x50):
//   found if (Random() & 3) < the amount (low nibble), which then
//   drops by one (the byte becomes 0 at amount 0). In a campaign
//   (g_campaignMission 0x7dfdb0 != 0) with the config byte +0x189 set,
//   computer players' digs fail when Random() > 0x55f0 (22000). Building
//   byte +0x1a counts digs up to 32; words +0x32:+0x30 are a 32-bit history
//   (shifted left, bit 0 = found). Found: g_playerTallies[owner].ore_found
//   += 1 (and .points += 4 for gems and an Egyptian player), job MinerFound*
//   and its animation (by type); not found: job MinerFoundNothing* and its
//   animation (by type) and, after 16 digs with no find among the last 16,
//   NotifyLocalPlayer(owner, 17, x, y) (mine exhausted). The settler goes back on its tile.
//   Other building types leave the jobs as the original's uninitialized
//   stack (not reproduced; only mines call it). EAX unused. ret 4.
// thiscall (ECX = world).

#include "game/enums/races.h"
#include "game/enums/buildings.h"
#include "game/enums/job_ids.h"
#include "game/globals.h"
#include "game/replacements.h"
#include "game/typed_fields.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/fake_graphics.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

namespace s3re::game {

using TileSlopeFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y);

// Purpose: How steep the land is at a tile (largest height step across it), e.g. to decide whether a
// tile can be worked or built on (callers accept up to 7).
std::uint32_t __fastcall CGameData_TileSlope(CGameData* world, void*, std::uint32_t x, std::uint32_t y) {
    // Unsigned: map size 0 makes the limit 0xffffffff.
    const std::uint32_t limit = world->map_size - 1;
    if (x < 1 || x >= limit || y < 1 || y >= limit) {
        return 0;
    }
    const std::uint32_t at = TileOffset(x, y);
    auto height = [&](std::uint32_t step) -> std::int32_t { return TileAtOffset(world, at + step).height; };
    // The height difference across the tile along one axis (step: the byte
    // offset of the neighbour on one side; the other side is -step).
    auto across = [&](std::uint32_t step) -> std::int32_t {
        const std::int32_t difference = height(step) - height(0u - step);
        return difference < 0 ? -difference : difference;
    };
    const std::int32_t alongX = across(kTileRowBytes);
    const std::int32_t alongY = across(kTileBytes);
    const std::int32_t diagonal = across(kTileRowBytes + kTileBytes);
    std::int32_t slope = alongX;
    if (slope < alongY) {
        slope = alongY;
    }
    if (slope < diagonal) {
        slope = diagonal;
    }
    return static_cast<std::uint32_t>(slope);
}

using SettlerFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t settler);

std::uint32_t __fastcall GetMemberByte189(void* self, void* edx);
std::uint32_t __fastcall CGameData_BuildingPileHasGoods(CGameData* world, void* edx, std::uint32_t building,
                                                        std::uint32_t slot);
std::uint32_t __fastcall CGameData_TakeGoodFromPile(CGameData* world, void* edx, std::uint32_t pile);
std::uint32_t __fastcall CGameData_SubtractPlayerCounter(CGameData* world, void* edx, std::uint32_t player,
                                                         std::uint32_t index, std::uint32_t amount);
std::uint32_t __fastcall CGameData_SetSettlerAnimation(CGameData* world, void* edx, std::uint32_t settler,
                                                       std::uint32_t kind);
std::uint32_t __fastcall CGameData_NotifyLocalPlayer(CGameData* world, void* edx, std::uint32_t player,
                                                     std::uint32_t type, std::uint32_t x, std::uint32_t y);

namespace {

// Per-player tallies in S3.EXE's data (0x44 bytes per player from
// 0x7acfd4; more fields follow, used elsewhere): probably the statistics
// shown after a game (re/globals.csv g_playerTallies).
PlayerTally& Tally(std::uint32_t player) {
    return g_playerTallies[player];
}

std::int32_t CampaignMission() {
    return g_campaignMission;
}

// The building's pile word for an input slot 1..3 (any other value: the third).
std::uint16_t SlotPile(const layout::Building& b, std::uint32_t slot) {
    return slot == 1 ? b.piles[0] : slot == 2 ? b.piles[1] : b.piles[2];
}

using B = BuildingType;
using Job = SettlerJob;

bool IsSulfurMine(BuildingType type) {
    return type == B::SulfurMine || type == B::AmazonSulfurMine;
}

// MinerJobFood: what a miner eats and how much it fills him.
constexpr std::uint8_t kFirstChoiceFood = 10;   // the mine's preferred food (first slot tried)
constexpr std::uint8_t kOtherFood = 2;          // any other food
constexpr std::uint8_t kComputerExtraFood = 4;  // computer players' miners
constexpr std::int32_t kComputerPlayer = 2;     // Player::active
// He goes digging only while the output pile holds at most 7 units.
constexpr std::uint8_t kMaxOutputBeforeDigging = 7;
constexpr std::uint8_t kRequestFlagWork = 0x01;  // Building::request_flags bit 0
constexpr std::uint16_t kDigCounter = 100;
constexpr std::uint16_t kWaitCounter = 10;

// MinerJobDig.
constexpr std::uint32_t kDigOffsets = 0x3f;            // Random() & 63: offset table entries 0..63
constexpr std::uint8_t kResourceKindMask = 0xf0;       // tile +0x11: high nibble kind, low nibble amount
constexpr std::uint8_t kResourceAmountMask = 0x0f;
// The resource kinds (high nibble of tile +0x11).
constexpr std::uint32_t kResourceCoal = 0x10;
constexpr std::uint32_t kResourceIron = 0x20;
constexpr std::uint32_t kResourceGold = 0x30;
constexpr std::uint32_t kResourceGems = 0x40;
constexpr std::uint32_t kResourceSulfur = 0x50;
constexpr std::uint32_t kDigChanceMask = 3;            // found if (Random() & 3) < amount
constexpr std::uint32_t kCampaignComputerFailAbove = 22000;  // 0x55f0
constexpr std::uint8_t kMaxDigsCounted = 32;
constexpr std::uint8_t kDigsBeforeExhaustedNotice = 16;
constexpr std::uint16_t kHistoryTopBit = 0x8000;
constexpr std::uint32_t kNoticeMineExhausted = 17;     // NotifyLocalPlayer type 0x11
constexpr std::int32_t kGemPointsForEgyptians = 4;

// MinerJobEnter.
constexpr std::uint8_t kFoodOnEntering = 10;

}  // namespace

// Purpose: A miner going down into his mine eats if he is hungry: one food from the mine's input piles
// (the preferred food first) fills his food level (+0x17); with food left he starts the dig cycle for
// the mine type, otherwise he waits.
std::uint32_t __fastcall CGameData_MinerJobFood(CGameData* world, void*, std::uint32_t settler) {
    layout::Settler& miner = world->settlers[settler];
    // The input slots in the order the miner tries them, and the dig job.
    std::uint8_t slots[3] = {0, 0, 0};
    Job digJob = Job::New;
    switch (TypeOf(world->buildings[miner.building])) {
    case B::CoalMine: slots[0] = 3; slots[1] = 2; slots[2] = 1; digJob = Job::MinerDigCoalMine; break;
    case B::GoldMine: slots[0] = 1; slots[1] = 3; slots[2] = 2; digJob = Job::MinerDigGoldMine; break;
    case B::IronMine: slots[0] = 2; slots[1] = 1; slots[2] = 3; digJob = Job::MinerDigIronMine; break;
    case B::GemMine: slots[0] = 1; slots[1] = 3; slots[2] = 2; digJob = Job::MinerDigGemMine; break;
    case B::SulfurMine:
    case B::AmazonSulfurMine: slots[0] = 1; slots[1] = 2; slots[2] = 2; digJob = Job::MinerDigSulfurMine; break;
    default: break;
    }
    // The original keeps the first try's building number in a register and
    // re-reads miner.building for the later tries and pile reads (the same
    // value unless a callee changes it).
    const std::uint32_t building = miner.building;
    if (miner.food == 0) {
        for (std::uint32_t k = 0; k < 3; ++k) {
            if (k == 2 && IsSulfurMine(TypeOf(world->buildings[miner.building]))) {
                break;  // these mines have only two kinds of food
            }
            auto current = [&] { return k == 0 ? building : std::uint32_t{miner.building}; };
            if ((CGameData_BuildingPileHasGoods(world, nullptr, current(), slots[k]) & 0xff) == 0) {
                continue;
            }
            ++Tally(miner.owner).meals;
            // The pile is looked up again after each call, as in the original.
            const std::uint32_t pile = SlotPile(world->buildings[current()], slots[k]);
            CGameData_PileIncAmountLeaving(world, nullptr, pile);
            CGameData_TakeGoodFromPile(world, nullptr, SlotPile(world->buildings[current()], slots[k]));
            const std::uint32_t taken = SlotPile(world->buildings[current()], slots[k]);
            CGameData_SubtractPlayerCounter(world, nullptr, miner.owner, world->piles[taken].good, 1);
            miner.food = static_cast<std::uint8_t>(miner.food + (k == 0 ? kFirstChoiceFood : kOtherFood));
            if (world->players[miner.owner].active == kComputerPlayer) {
                miner.food = static_cast<std::uint8_t>(miner.food + kComputerExtraFood);
            }
            break;
        }
    }
    const layout::Building& mine = world->buildings[miner.building];
    if ((mine.request_flags & kRequestFlagWork) != 0 && miner.food != 0 &&
        world->piles[mine.pile_ring].amount <= kMaxOutputBeforeDigging) {
        miner.food = static_cast<std::uint8_t>(miner.food - 1);
        SetJob(miner, digJob);
        miner.counter = kDigCounter;
        return 0;
    }
    miner.counter = kWaitCounter;
    return 0;  // UpdateSettlers overwrites EAX
}

// Purpose: One dig of a miner: a random tile near the mine is tried; if it holds the mine's resource,
// a unit may be found (and taken from the tile). The last 32 results are kept in the building; when
// the mine has been failing for long, the local player is told that it is exhausted.
std::uint32_t __fastcall CGameData_MinerJobDig(CGameData* world, void*, std::uint32_t settler) {
    layout::Settler& miner = world->settlers[settler];
    // Resource kind (tile +0x11 high nibble), jobs and animation kinds after
    // a failed / successful dig. Other building types leave all but the kind
    // as the original's uninitialized stack (not reproduced; only mines call it).
    std::uint32_t kind = 0, failJob = 0, failAnimation = 0, foundJob = 0, foundAnimation = 0;
    switch (TypeOf(world->buildings[miner.building])) {
    case B::CoalMine:
        kind = kResourceCoal;
        failJob = Value(Job::MinerFoundNothingCoalMine), failAnimation = 94;
        foundJob = Value(Job::MinerFoundCoalMine), foundAnimation = 95;
        break;
    case B::IronMine:
        kind = kResourceIron;
        failJob = Value(Job::MinerFoundNothingIronMine), failAnimation = 100;
        foundJob = Value(Job::MinerFoundIronMine), foundAnimation = 101;
        break;
    case B::GoldMine:
        kind = kResourceGold;
        failJob = Value(Job::MinerFoundNothingGoldMine), failAnimation = 97;
        foundJob = Value(Job::MinerFoundGoldMine), foundAnimation = 98;
        break;
    case B::GemMine:
        kind = kResourceGems;
        failJob = Value(Job::MinerFoundNothingGemMine), failAnimation = 216;
        foundJob = Value(Job::MinerFoundGemMine), foundAnimation = 217;
        break;
    case B::SulfurMine:
    case B::AmazonSulfurMine:
        kind = kResourceSulfur;
        failJob = Value(Job::MinerFoundNothingSulfurMine), failAnimation = 219;
        foundJob = Value(Job::MinerFoundSulfurMine), foundAnimation = 220;
        break;
    default: break;
    }
    // 1. Try one tile around the mine (0x4fa2c1..).
    bool found = false;
    const layout::MapOffset& offset = world->offsets[CGameData_Random(world, nullptr) & kDigOffsets];
    const layout::Building& mine = world->buildings[miner.building];
    const std::uint32_t tx = mine.x + static_cast<std::uint32_t>(offset.dx);
    const std::uint32_t ty = mine.y + static_cast<std::uint32_t>(offset.dy);
    if (CGameData_IsOnMap(world, nullptr, tx, ty) != 0) {
        std::uint8_t& resources = TileAtOffset(world, TileOffset(tx, ty)).resources;
        if (static_cast<std::uint32_t>(resources & kResourceKindMask) == kind) {
            const std::uint8_t before = resources;
            if (static_cast<std::uint16_t>(CGameData_Random(world, nullptr) & kDigChanceMask) <
                static_cast<std::uint16_t>(before & kResourceAmountMask)) {
                found = true;
                resources = static_cast<std::uint8_t>(before - 1);
                if ((resources & kResourceAmountMask) == 0) {
                    resources = 0;
                }
                if (CampaignMission() != 0 && (GetMemberByte189(&g_config, nullptr) & 0xff) != 0 &&
                    world->players[miner.owner].active == kComputerPlayer &&
                    CGameData_Random(world, nullptr) > kCampaignComputerFailAbove) {
                    found = false;  // in campaigns computer players sometimes find nothing
                }
            }
        }
    }
    // 2. Count the dig and shift the history (the original re-reads the
    // building through miner.building for each access).
    layout::Building& building = world->buildings[miner.building];
    if (building.production.digs != kMaxDigsCounted) {
        ++building.production.digs;  // digs so far, up to 32
    }
    // work_y:work_x is a 32-bit history of the last digs (1 = found).
    building.work_y = static_cast<std::uint16_t>(building.work_y << 1);
    if ((building.work_x & kHistoryTopBit) != 0) {
        ++building.work_y;
    }
    building.work_x = static_cast<std::uint16_t>(building.work_x << 1);
    // 3. Back on the tile with the job and animation for the result.
    if (!found) {
        TileAtOffset(world, TileOffset(miner.x, miner.y)).settler = static_cast<std::uint16_t>(settler);
        miner.job = static_cast<std::uint16_t>(failJob);
        CGameData_SetSettlerAnimation(world, nullptr, settler, failAnimation);
        const layout::Building& after = world->buildings[miner.building];
        if (after.production.digs >= kDigsBeforeExhaustedNotice && after.work_x == 0) {
            CGameData_NotifyLocalPlayer(world, nullptr, miner.owner, kNoticeMineExhausted, miner.x, miner.y);
        }
        return 0;
    }
    ++world->buildings[miner.building].work_x;
    ++Tally(miner.owner).ore_found;
    if (kind == kResourceGems && world->players[miner.owner].race == Value(Race::Egyptian)) {
        Tally(miner.owner).points += kGemPointsForEgyptians;
    }
    TileAtOffset(world, TileOffset(miner.x, miner.y)).settler = static_cast<std::uint16_t>(settler);
    miner.job = static_cast<std::uint16_t>(foundJob);
    CGameData_SetSettlerAnimation(world, nullptr, settler, foundAnimation);
    return 0;  // UpdateSettlers overwrites EAX
}

S3RE_HOOK(0x004f9b80, CGameData_MinerJobFood, SettlerFn, Original_CGameData_MinerJobFood, CGameData_MinerJobFood);
S3RE_HOOK(0x004fa220, CGameData_MinerJobDig, SettlerFn, Original_CGameData_MinerJobDig, CGameData_MinerJobDig);

// Purpose: A miner enters his mine and starts the work cycle that fits the mine type (coal, iron,
// gold, stone, sulphur...); he disappears from the map while inside.
std::uint32_t __fastcall CGameData_MinerJobEnter(CGameData* world, void*, std::uint32_t settler) {
    layout::Settler& miner = world->settlers[settler];
    switch (TypeOf(world->buildings[miner.building])) {
    case B::CoalMine: SetJob(miner, Job::MinerEatCoalMine); break;
    case B::GoldMine: SetJob(miner, Job::MinerEatGoldMine); break;
    case B::IronMine: SetJob(miner, Job::MinerEatIronMine); break;
    case B::GemMine: SetJob(miner, Job::MinerEatGemMine); break;
    case B::SulfurMine:
    case B::AmazonSulfurMine: SetJob(miner, Job::MinerEatSulfurMine); break;
    default: break;
    }
    layout::Building& mine = world->buildings[miner.building];
    mine.production.worker_inside = static_cast<std::uint8_t>(mine.production.worker_inside + 1);
    mine.production.digs = 0;
    mine.work_x = 0;
    mine.work_y = 0;
    TileAtOffset(world, TileOffset(miner.x, miner.y)).settler = 0;
    miner.counter = 0;
    miner.food = kFoodOnEntering;
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world)) + settler * sizeof(layout::Settler);
}

S3RE_HOOK(0x004f9a00, CGameData_MinerJobEnter, SettlerFn, Original_CGameData_MinerJobEnter, CGameData_MinerJobEnter);
S3RE_HOOK(0x004faa70, CGameData_TileSlope, TileSlopeFn, Original_CGameData_TileSlope, CGameData_TileSlope);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kTestWorld = offsetof(CGameData, tiles) + 400 * sizeof(CGameData::tiles[0]);

// Wrong: leaves out the diagonal pair.
std::uint32_t __fastcall WrongTileSlope(CGameData* world, void* edx, std::uint32_t x, std::uint32_t y) {
    if (x >= 1 && y >= 1 && x + 1 < world->map_size && y + 1 < world->map_size) {
        layout::Tile& t = world->tiles[x + 1][y + 1];
        const std::uint8_t saved = t.height;
        t.height = world->tiles[x - 1][y - 1].height;
        const std::uint32_t result = CGameData_TileSlope(world, edx, x, y);
        t.height = saved;
        return result;
    }
    return CGameData_TileSlope(world, edx, x, y);
}

void CompareTileSlope(test::Context& ctx, TileSlopeFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = ctx.rng().Chance(1, 10) ? ctx.rng().Below(5) : 384;
        // Map size 0 lets any x, y pass: keep them on the buffer then.
        const bool wild = size != 0 && ctx.rng().Chance(1, 10);
        const std::uint32_t x = wild ? ctx.rng().Next32() : ctx.rng().Below(386);
        const std::uint32_t y = wild && ctx.rng().Chance(1, 2) ? ctx.rng().Next32() : ctx.rng().Below(386);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        if (x >= 1 && x < 384 && y >= 1 && y < 384) {
            const std::uint32_t base = ctx.rng().Below(256);
            for (std::int32_t dx = -1; dx <= 1; ++dx) {
                for (std::int32_t dy = -1; dy <= 1; ++dy) {
                    const auto h = static_cast<std::uint8_t>(base + ctx.rng().Below(20));
                    worlds.Both([&](CGameData* w) { w->tiles[x + dx][y + dy].height = h; });
                }
            }
        }
        const std::uint32_t expected = Original_CGameData_TileSlope(worlds.original(), nullptr, x, y);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, x, y);
        if (!S3RE_EXPECT_EQ(expected, actual)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%u, %u) map %u", i, x, y, size);
            return;
        }
    }
    S3RE_EXPECT(worlds.Checkpoint(ctx));
}

// Wrong: the Amazon sulfur mine gets the gem mine's job.
std::uint32_t __fastcall WrongMinerJobEnter(CGameData* world, void* edx, std::uint32_t settler) {
    const std::uint32_t result = CGameData_MinerJobEnter(world, edx, settler);
    layout::Settler& s = world->settlers[settler];
    if (TypeOf(world->buildings[s.building]) == B::AmazonSulfurMine) {
        SetJob(s, Job::MinerEatGemMine);
    }
    return result;
}

void CompareMinerJobEnter(test::Context& ctx, SettlerFn candidate, int cases) {
    test::WorldPair worlds(ctx, offsetof(CGameData, buildings) + 0x101 * sizeof(layout::Building));
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t settler = ctx.rng().Below(0x7d00);
        const std::uint32_t building = ctx.rng().Below(0x100);
        static constexpr std::uint8_t kArts[] = {
            Value(B::CoalMine),   Value(B::GoldMine),         Value(B::IronMine),     Value(B::SulfurMine),
            Value(B::GemMine),    Value(B::AmazonSulfurMine), Value(B::LookoutTower), Value(B::GoldSmelter),
            57 /* past the last type */, Value(B::RiceFarm)};
        const std::uint8_t art = ctx.rng().Chance(1, 10) ? static_cast<std::uint8_t>(ctx.rng().Next32())
                                                          : kArts[ctx.rng().Below(10)];
        const auto x = static_cast<std::uint16_t>(ctx.rng().Below(384));
        const auto y = static_cast<std::uint16_t>(ctx.rng().Below(384));
        worlds.Both([&](CGameData* w) {
            w->settlers[settler].building = static_cast<std::uint16_t>(building);
            w->settlers[settler].x = x;
            w->settlers[settler].y = y;
            w->buildings[building].art = art;
        });
        const std::uint32_t expected = Original_CGameData_MinerJobEnter(worlds.original(), nullptr, settler) -
                                       static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(worlds.original()));
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, settler) -
                                     static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(worlds.ours()));
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 256 == 255 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 255 before it): settler %u art 0x%x", i, settler,
                     art);
            return;
        }
    }
}

}  // namespace

namespace {

constexpr std::size_t kTallyBytes = 20 * sizeof(PlayerTally);

// Runs both on full worlds with mines: compares the worlds, the tallies
// (S3.EXE data, restored between the runs) and the DebugLog-free result.
void CompareMinerJob(test::Context& ctx, SettlerFn original, SettlerFn candidate, int cases, bool dig) {
    test::FakeGraphics gfx;  // every sprite valid, for SetSettlerAnimation
    gfx.MakeEverySpriteValid(7);
    test::WorldPair worlds(ctx, sizeof(CGameData));
    worlds.Both([&](CGameData* w) {
        w->map_size = 384;
        w->building_high_water = 0x100;
        for (std::int32_t k = 0; k < 64; ++k) {  // radius 0..4 around the mine
            w->offsets[k].dx = k % 9 - 4;
            w->offsets[k].dy = k / 9 - 3;
        }
        for (std::uint32_t p = 0; p < 0x7d00; ++p) {
            layout::Pile& pile = w->piles[p];
            pile.x = static_cast<std::uint16_t>(pile.x % 384);
            pile.y = static_cast<std::uint16_t>(pile.y % 384);
            pile.prev = static_cast<std::uint16_t>(pile.prev % 0x400);
            pile.next = static_cast<std::uint16_t>(pile.next % 0x400);
        }
        for (auto& row : w->tiles) {
            for (layout::Tile& tile : row) {
                tile.area = static_cast<std::uint16_t>(tile.area % 0x200);
                tile.object_number = static_cast<std::uint16_t>(tile.object_number % 0x100);
            }
        }
    });
    std::int32_t& mission = g_campaignMission;
    std::uint8_t& configByte = g_config->unk189;
    const std::int32_t savedMission = mission;
    const std::uint8_t savedConfig = configByte;
    std::vector<std::uint8_t> tallies(kTallyBytes), before(kTallyBytes);
    auto* const tallyBytes = reinterpret_cast<std::uint8_t*>(&Tally(0));
    std::memcpy(before.data(), tallyBytes, kTallyBytes);
    const std::vector<std::uint8_t> savedTallies = before;
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t settler = 1 + ctx.rng().Below(0x400);
        const std::uint32_t building = 1 + ctx.rng().Below(0xff);
        static constexpr std::uint8_t kMines[] = {
            Value(B::CoalMine),   Value(B::GoldMine), Value(B::IronMine), Value(B::GemMine),
            Value(B::SulfurMine), Value(B::AmazonSulfurMine), Value(B::CoalMine), Value(B::GoldMine)};
        const std::uint8_t art = kMines[ctx.rng().Below(8)];
        const std::uint32_t bx = 10 + ctx.rng().Below(360), by = 10 + ctx.rng().Below(360);
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Below(4));
        const auto food = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Below(20));
        mission = ctx.rng().Chance(1, 2) ? 0 : 1 + static_cast<std::int32_t>(ctx.rng().Below(20));
        configByte = static_cast<std::uint8_t>(ctx.rng().Below(2));
        worlds.Both([&](CGameData* w) {
            layout::Settler& s = w->settlers[settler];
            s.building = static_cast<std::uint16_t>(building);
            s.owner = owner;
            s.x = static_cast<std::uint16_t>(bx);
            s.y = static_cast<std::uint16_t>(by + 1);
            s.food = food;
            layout::Building& b = w->buildings[building];
            b.art = art;
            b.x = static_cast<std::uint16_t>(bx);
            b.y = static_cast<std::uint16_t>(by);
            w->players[owner].active = static_cast<std::int32_t>(1 + (owner & 1));
            w->players[owner].race = static_cast<std::int32_t>(owner >> 1);
        });
        if (dig) {
            // Resources around the mine: mostly the mine's kind.
            static constexpr std::uint8_t kKinds[] = {0x10, 0x20, 0x30, 0x40, 0x50};
            const std::uint16_t history = static_cast<std::uint16_t>(ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Next32());
            const auto digs = static_cast<std::uint8_t>(ctx.rng().Below(0x22));
            worlds.Both([&](CGameData* w) {
                w->buildings[building].work_x = history;
                w->buildings[building].production.digs = digs;
            });
            for (std::int32_t dx = -4; dx <= 4; ++dx) {
                for (std::int32_t dy = -3; dy <= 4; ++dy) {
                    const auto r = static_cast<std::uint8_t>(kKinds[ctx.rng().Below(5)] | ctx.rng().Below(16));
                    worlds.Both([&](CGameData* w) { w->tiles[bx + dx][by + dy].resources = r; });
                }
            }
        } else {
            // Input piles with or without goods, an output pile around 7.
            for (std::uint32_t k = 0; k < 3; ++k) {
                const auto pile = static_cast<std::uint16_t>(ctx.rng().Chance(1, 6) ? 0 : 1 + ctx.rng().Below(0x3ff));
                const auto amount = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0 : 1 + ctx.rng().Below(3));
                const auto kind = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0 : 0xfe);
                worlds.Both([&](CGameData* w) {
                    w->buildings[building].piles[k] = pile;
                    w->piles[pile].amount = amount;
                    w->piles[pile].art = kind;
                    w->piles[pile].good = static_cast<std::uint8_t>(1 + k);
                });
            }
            const auto out = static_cast<std::uint16_t>(1 + ctx.rng().Below(0x3ff));
            const auto outAmount = static_cast<std::uint8_t>(5 + ctx.rng().Below(5));
            const auto flags = static_cast<std::uint8_t>(ctx.rng().Next32());
            worlds.Both([&](CGameData* w) {
                w->buildings[building].pile_ring = out;
                w->buildings[building].request_flags = flags;
                w->piles[out].amount = outAmount;
            });
        }
        std::memcpy(tallyBytes, before.data(), kTallyBytes);
        original(worlds.original(), nullptr, settler);
        std::memcpy(tallies.data(), tallyBytes, kTallyBytes);
        std::memcpy(tallyBytes, before.data(), kTallyBytes);
        candidate(worlds.ours(), nullptr, settler);
        const bool sameTallies = S3RE_EXPECT_MEM_EQ(tallies.data(), tallyBytes, kTallyBytes);
        std::memcpy(before.data(), tallyBytes, kTallyBytes);
        if (!sameTallies || ((i % 64 == 63 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 63 before it): settler %u art 0x%x", i, settler, art);
            break;
        }
    }
    std::memcpy(tallyBytes, savedTallies.data(), kTallyBytes);
    mission = savedMission;
    configByte = savedConfig;
}

// Wrong: computer players' miners get no extra food.
std::uint32_t __fastcall WrongMinerJobFood(CGameData* world, void* edx, std::uint32_t settler) {
    layout::Settler& s = world->settlers[settler];
    std::int32_t& active = world->players[s.owner].active;
    const std::int32_t saved = active;
    active = 1;
    const std::uint32_t result = CGameData_MinerJobFood(world, edx, settler);
    active = saved;
    return result;
}

// Wrong: the dig history is not shifted when nothing is found.
std::uint32_t __fastcall WrongMinerJobDig(CGameData* world, void* edx, std::uint32_t settler) {
    layout::Building& b = world->buildings[world->settlers[settler].building];
    const std::uint16_t x = b.work_x, y = b.work_y;
    const std::uint32_t result = CGameData_MinerJobDig(world, edx, settler);
    if ((b.work_x & 1) == 0) {
        b.work_x = x;
        b.work_y = y;
    }
    return result;
}

}  // namespace

S3RE_TEST(CGameData_MinerJobFood_MatchesOriginal) {
    CompareMinerJob(ctx, Original_CGameData_MinerJobFood, &CGameData_MinerJobFood, 4000, false);
}
S3RE_TEST(CGameData_MinerJobFood_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMinerJob(probe, Original_CGameData_MinerJobFood, &WrongMinerJobFood, 2000, false);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_MinerJobDig_MatchesOriginal) {
    CompareMinerJob(ctx, Original_CGameData_MinerJobDig, &CGameData_MinerJobDig, 4000, true);
}
S3RE_TEST(CGameData_MinerJobDig_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMinerJob(probe, Original_CGameData_MinerJobDig, &WrongMinerJobDig, 2000, true);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CGameData_MinerJobEnter_MatchesOriginal) {
    CompareMinerJobEnter(ctx, &CGameData_MinerJobEnter, 20000);
}
S3RE_TEST(CGameData_MinerJobEnter_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMinerJobEnter(probe, &WrongMinerJobEnter, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(CGameData_TileSlope_MatchesOriginal) {
    CompareTileSlope(ctx, &CGameData_TileSlope, 20000);
}
S3RE_TEST(CGameData_TileSlope_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareTileSlope(probe, &WrongTileSlope, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
