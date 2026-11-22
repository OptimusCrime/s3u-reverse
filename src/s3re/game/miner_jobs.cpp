// Mine and terrain helpers of the settler jobs (UpdateSettlers callees,
// package B).
//
// GameWorld::TileSlope (0x004faa70): (x, y) -> the steepness at a tile:
//   the largest absolute height difference (tile byte +0x4) between
//   opposite neighbours: (x+1, y) / (x-1, y), (x, y+1) / (x, y-1),
//   (x+1, y+1) / (x-1, y-1). 0 unless 1 <= x, y < map size - 1 (unsigned;
//   map size 0 makes the limit 0xffffffff). Callers compare it with 7
//   (UpdateSettlers' work jobs, the AI 0x5540f0, 0x599a30). ret 8.
// GameWorld::MinerJobEnter (0x004f9a00): (settler): a miner goes into his
//   mine: job (+0xe) by the mine's building type: 7 -> 0x55, 8 -> 0x56,
//   9 -> 0x57, 0x25 -> 0xd8, 0x1d and 0x38 -> 0xd9, others unchanged
//   (jump table); the building's byte +0x19 += 1 (miners inside?), byte
//   +0x1a = 0, words +0x30/+0x32 = 0; the settler leaves its tile (tile
//   settler word = 0), counter +0xc = 0, byte +0x17 = 10. EAX = world +
//   settler * 0x40 (the original's register; UpdateSettlers overwrites
//   it). ret 4. Caller: UpdateSettlers (job of settler type 0xa).
// GameWorld::MinerJobFood (0x004f9b80): (settler): if the miner's food
//   level (byte +0x17) is 0, he eats from the mine's input slots in the
//   mine type's order (7: 3, 2, 1; 8: 1, 3, 2; 9: 2, 1, 3; 0x25: 1, 3, 2;
//   0x1d/0x38: 1, 2, only two tries): the first slot with goods
//   (BuildingPileHasGoods) gives one unit (PileIncAmountLeaving,
//   TakeGoodFromPile, SubtractPlayerCounter of the pile's goods type),
//   g_playerTallies[owner].meals += 1, food level += 10 for the first slot
//   or 2 for the others, 4 more for computer players (player +0x0 == 2).
//   Then, if building flag 0x1 of +0x18 is set, he has food and the
//   building's output pile (+0xe) holds at most 7 units: food level -= 1,
//   job = the dig job of the mine type (0x58/0x5c/0x60/0xda/0xde),
//   counter 100; else counter 10. EAX unused. ret 4.
// GameWorld::MinerJobDig (0x004fa220): (settler): one dig. A tile of the
//   offset table entry Random() & 0x3f around the mine (building +0x2/+0x4)
//   is on the map and holds the mine type's resource (tile +0x11 high
//   nibble: 7 -> 0x10, 9 -> 0x20, 8 -> 0x30, 0x25 -> 0x40, 0x1d/0x38 ->
//   0x50): found if (Random() & 3) < the amount (low nibble), which then
//   drops by one (the byte becomes 0 at amount 0). In a campaign
//   (g_campaignMission 0x7dfdb0 != 0) with the config byte +0x189 set,
//   computer players' digs fail when Random() > 0x55f0. Building byte
//   +0x1a counts digs up to 32; words +0x32:+0x30 are a 32-bit history
//   (shifted left, bit 0 = found). Found: g_playerTallies[owner].ore_found
//   += 1 (and .points += 4 for resource 0x40 and race 1), job/animation
//   0x5a/0x5f (by type); not found: job/animation 0x59/0x5e (by type) and,
//   after 16 digs with no find among the last 16, NotifyLocalPlayer(owner,
//   0x11, x, y) (mine exhausted). The settler goes back on its tile.
//   Other building types leave the jobs as the original's uninitialized
//   stack (not reproduced; only mines call it). EAX unused. ret 4.
// thiscall (ECX = world).

#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/fake_graphics.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

using TileSlopeFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y);

// Purpose: How steep the land is at a tile (largest height step across it), e.g. to decide whether a
// tile can be worked or built on (callers accept up to 7).
std::uint32_t __fastcall GameWorld_TileSlope(GameWorld* world, void*, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t limit = world->map_size - 1;
    if (x < 1 || x >= limit || y < 1 || y >= limit) {
        return 0;
    }
    const std::uint32_t at = TileOffset(x, y);
    auto height = [&](std::uint32_t step) -> std::int32_t { return TileAtOffset(world, at + step).height; };
    const std::int32_t a = height(kTileRowBytes) - height(0u - kTileRowBytes);
    const std::int32_t b = height(kTileBytes) - height(0u - kTileBytes);
    const std::int32_t c = height(kTileRowBytes + kTileBytes) - height(0u - kTileRowBytes - kTileBytes);
    std::int32_t slope = a < 0 ? -a : a;
    const std::int32_t absB = b < 0 ? -b : b;
    const std::int32_t absC = c < 0 ? -c : c;
    if (slope < absB) {
        slope = absB;
    }
    if (slope < absC) {
        slope = absC;
    }
    return static_cast<std::uint32_t>(slope);
}

using SettlerFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t settler);

std::uint32_t __fastcall GetMemberByte189(void* self, void* edx);
std::uint32_t __fastcall GameWorld_BuildingPileHasGoods(GameWorld* world, void* edx, std::uint32_t building,
                                                        std::uint32_t slot);
std::uint32_t __fastcall GameWorld_TakeGoodFromPile(GameWorld* world, void* edx, std::uint32_t pile);
std::uint32_t __fastcall GameWorld_SubtractPlayerCounter(GameWorld* world, void* edx, std::uint32_t player,
                                                         std::uint32_t index, std::uint32_t amount);
std::uint32_t __fastcall GameWorld_SetSettlerAnimation(GameWorld* world, void* edx, std::uint32_t settler,
                                                       std::uint32_t kind);
std::uint32_t __fastcall GameWorld_NotifyLocalPlayer(GameWorld* world, void* edx, std::uint32_t player,
                                                     std::uint32_t type, std::uint32_t x, std::uint32_t y);

namespace {

// Per-player tallies in S3.EXE's data (0x44 bytes per player from
// 0x7acfd4; more fields follow, used elsewhere): probably the statistics
// shown after a game (re/globals.csv g_playerTallies).
#pragma pack(push, 1)
struct PlayerTally {
    std::int32_t meals;       // 0x7acfd4 +1 per food a miner eats (MinerJobFood)
    std::int32_t ore_found;   // 0x7acfd8 +1 per successful dig (MinerJobDig)
    std::int32_t points;      // 0x7acfdc +4 for resource 0x40 by race 1 (MinerJobDig); +2 in UpdateSettlers
    std::uint8_t unk0c[0x38];
};
#pragma pack(pop)
static_assert(sizeof(PlayerTally) == 0x44);

PlayerTally& Tally(std::uint32_t player) {
    return reinterpret_cast<PlayerTally*>(0x007acfd4)[player];
}

// int32: number of the running campaign mission (0 outside campaigns), S3.EXE 0x7dfdb0.
std::int32_t CampaignMission() {
    return *reinterpret_cast<const std::int32_t*>(0x007dfdb0);
}

void* const kConfig = reinterpret_cast<void*>(0x007a7af0);

// The building's pile word for an input slot 1..3 (any other value: the third).
std::uint16_t SlotPile(const layout::Building& b, std::uint32_t slot) {
    return slot == 1 ? b.piles[0] : slot == 2 ? b.piles[1] : b.piles[2];
}

}  // namespace

// Purpose: A miner going down into his mine eats if he is hungry: one food from the mine's input piles
// (the preferred food first) fills his food level (+0x17); with food left he starts the dig cycle for
// the mine type, otherwise he waits.
std::uint32_t __fastcall GameWorld_MinerJobFood(GameWorld* world, void*, std::uint32_t settler) {
    layout::Settler& s = world->settlers[settler];
    std::uint8_t slots[3] = {0, 0, 0};
    std::uint16_t job = 0;
    switch (world->buildings[s.building].art) {
    case 0x07: slots[0] = 3; slots[1] = 2; slots[2] = 1; job = 0x58; break;
    case 0x08: slots[0] = 1; slots[1] = 3; slots[2] = 2; job = 0x5c; break;
    case 0x09: slots[0] = 2; slots[1] = 1; slots[2] = 3; job = 0x60; break;
    case 0x25: slots[0] = 1; slots[1] = 3; slots[2] = 2; job = 0xda; break;
    case 0x1d:
    case 0x38: slots[0] = 1; slots[1] = 2; slots[2] = 2; job = 0xde; break;
    default: break;
    }
    const std::uint32_t building = s.building;
    if (s.unk17 == 0) {
        for (std::uint32_t k = 0; k < 3; ++k) {
            if (k == 2) {
                const std::uint8_t art = world->buildings[s.building].art;
                if (art == 0x1d || art == 0x38) {
                    break;  // these mines have only two kinds of food
                }
            }
            if ((GameWorld_BuildingPileHasGoods(world, nullptr, k == 0 ? building : s.building, slots[k]) & 0xff) == 0) {
                continue;
            }
            ++Tally(s.owner).meals;
            const std::uint32_t pile = SlotPile(world->buildings[k == 0 ? building : s.building], slots[k]);
            GameWorld_PileIncAmountLeaving(world, nullptr, pile);
            GameWorld_TakeGoodFromPile(world, nullptr, SlotPile(world->buildings[k == 0 ? building : s.building], slots[k]));
            const std::uint32_t taken = SlotPile(world->buildings[k == 0 ? building : s.building], slots[k]);
            GameWorld_SubtractPlayerCounter(world, nullptr, s.owner, world->piles[taken].good, 1);
            s.unk17 = static_cast<std::uint8_t>(s.unk17 + (k == 0 ? 10 : 2));
            if (world->players[s.owner].active == 2) {
                s.unk17 = static_cast<std::uint8_t>(s.unk17 + 4);  // computer players' miners eat more
            }
            break;
        }
    }
    const layout::Building& b = world->buildings[s.building];
    if ((b.request_flags & 1) != 0 && s.unk17 != 0 && world->piles[b.pile_ring].amount <= 7) {
        s.unk17 = static_cast<std::uint8_t>(s.unk17 - 1);
        s.job = job;
        s.counter = 100;
        return 0;
    }
    s.counter = 10;
    return 0;  // UpdateSettlers overwrites EAX
}

// Purpose: One dig of a miner: a random tile near the mine is tried; if it holds the mine's resource,
// a unit may be found (and taken from the tile). The last 32 results are kept in the building; when
// the mine has been failing for long, the local player is told that it is exhausted.
std::uint32_t __fastcall GameWorld_MinerJobDig(GameWorld* world, void*, std::uint32_t settler) {
    layout::Settler& s = world->settlers[settler];
    // Resource kind (tile +0x11 high nibble), jobs and animations after a
    // failed / successful dig. Other building types leave all but the kind
    // as the original's uninitialized stack (not reproduced).
    std::uint32_t kind = 0, failJob = 0, failAnimation = 0, foundJob = 0, foundAnimation = 0;
    switch (world->buildings[s.building].art) {
    case 0x07: kind = 0x10; failJob = 0x59; failAnimation = 0x5e; foundJob = 0x5a; foundAnimation = 0x5f; break;
    case 0x09: kind = 0x20; failJob = 0x61; failAnimation = 0x64; foundJob = 0x62; foundAnimation = 0x65; break;
    case 0x08: kind = 0x30; failJob = 0x5d; failAnimation = 0x61; foundJob = 0x5e; foundAnimation = 0x62; break;
    case 0x25: kind = 0x40; failJob = 0xdb; failAnimation = 0xd8; foundJob = 0xdc; foundAnimation = 0xd9; break;
    case 0x1d:
    case 0x38: kind = 0x50; failJob = 0xdf; failAnimation = 0xdb; foundJob = 0xe0; foundAnimation = 0xdc; break;
    default: break;
    }
    std::uint32_t found = 0;
    const layout::MapOffset& offset = world->offsets[GameWorld_Random(world, nullptr) & 0x3f];
    const layout::Building& mine = world->buildings[s.building];
    const std::uint32_t tx = mine.x + static_cast<std::uint32_t>(offset.dx);
    const std::uint32_t ty = mine.y + static_cast<std::uint32_t>(offset.dy);
    if (GameWorld_IsOnMap(world, nullptr, tx, ty) != 0) {
        std::uint8_t& resources = TileAtOffset(world, TileOffset(tx, ty)).resources;
        if (static_cast<std::uint32_t>(resources & 0xf0) == kind) {
            const std::uint8_t before = resources;
            if (static_cast<std::uint16_t>(GameWorld_Random(world, nullptr) & 3) <
                static_cast<std::uint16_t>(before & 0x0f)) {
                found = 1;
                resources = static_cast<std::uint8_t>(before - 1);
                if ((resources & 0x0f) == 0) {
                    resources = 0;
                }
                if (CampaignMission() != 0 && (GetMemberByte189(kConfig, nullptr) & 0xff) != 0 &&
                    world->players[s.owner].active == 2 && GameWorld_Random(world, nullptr) > 0x55f0) {
                    found = 0;  // in campaigns computer players sometimes find nothing
                }
            }
        }
    }
    layout::Building& b = world->buildings[s.building];
    if (b.unk1a != 0x20) {
        ++b.unk1a;  // digs so far, up to 32
    }
    // work_y:work_x is a 32-bit history of the last digs (1 = found).
    world->buildings[s.building].work_y = static_cast<std::uint16_t>(world->buildings[s.building].work_y << 1);
    if ((world->buildings[s.building].work_x & 0x8000) != 0) {
        ++world->buildings[s.building].work_y;
    }
    world->buildings[s.building].work_x = static_cast<std::uint16_t>(world->buildings[s.building].work_x << 1);
    if (found == 0) {
        TileAtOffset(world, TileOffset(s.x, s.y)).settler = static_cast<std::uint16_t>(settler);
        s.job = static_cast<std::uint16_t>(failJob);
        GameWorld_SetSettlerAnimation(world, nullptr, settler, failAnimation);
        const layout::Building& after = world->buildings[s.building];
        if (after.unk1a >= 0x10 && after.work_x == 0) {
            GameWorld_NotifyLocalPlayer(world, nullptr, s.owner, 0x11, s.x, s.y);
        }
        return 0;
    }
    ++world->buildings[s.building].work_x;
    ++Tally(s.owner).ore_found;
    if (kind == 0x40 && world->players[s.owner].race == 1) {
        Tally(s.owner).points += 4;
    }
    TileAtOffset(world, TileOffset(s.x, s.y)).settler = static_cast<std::uint16_t>(settler);
    s.job = static_cast<std::uint16_t>(foundJob);
    GameWorld_SetSettlerAnimation(world, nullptr, settler, foundAnimation);
    return 0;  // UpdateSettlers overwrites EAX
}

S3RE_HOOK(0x004f9b80, GameWorld_MinerJobFood, SettlerFn, Original_GameWorld_MinerJobFood, GameWorld_MinerJobFood);
S3RE_HOOK(0x004fa220, GameWorld_MinerJobDig, SettlerFn, Original_GameWorld_MinerJobDig, GameWorld_MinerJobDig);

// Purpose: A miner enters his mine and starts the work cycle that fits the mine type (coal, iron,
// gold, stone, sulphur...); he disappears from the map while inside.
std::uint32_t __fastcall GameWorld_MinerJobEnter(GameWorld* world, void*, std::uint32_t settler) {
    layout::Settler& s = world->settlers[settler];
    switch (world->buildings[s.building].art) {
    case 0x07: s.job = 0x55; break;
    case 0x08: s.job = 0x56; break;
    case 0x09: s.job = 0x57; break;
    case 0x25: s.job = 0xd8; break;
    case 0x1d:
    case 0x38: s.job = 0xd9; break;
    default: break;
    }
    layout::Building& b = world->buildings[s.building];
    b.unk19 = static_cast<std::uint8_t>(b.unk19 + 1);
    world->buildings[s.building].unk1a = 0;
    world->buildings[s.building].work_x = 0;
    world->buildings[s.building].work_y = 0;
    TileAtOffset(world, TileOffset(s.x, s.y)).settler = 0;
    s.counter = 0;
    s.unk17 = 10;
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world)) + settler * sizeof(layout::Settler);
}

S3RE_HOOK(0x004f9a00, GameWorld_MinerJobEnter, SettlerFn, Original_GameWorld_MinerJobEnter, GameWorld_MinerJobEnter);
S3RE_HOOK(0x004faa70, GameWorld_TileSlope, TileSlopeFn, Original_GameWorld_TileSlope, GameWorld_TileSlope);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kTestWorld = offsetof(GameWorld, tiles) + 400 * sizeof(GameWorld::tiles[0]);

// Wrong: leaves out the diagonal pair.
std::uint32_t __fastcall WrongTileSlope(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y) {
    if (x >= 1 && y >= 1 && x + 1 < world->map_size && y + 1 < world->map_size) {
        layout::Tile& t = world->tiles[x + 1][y + 1];
        const std::uint8_t saved = t.height;
        t.height = world->tiles[x - 1][y - 1].height;
        const std::uint32_t result = GameWorld_TileSlope(world, edx, x, y);
        t.height = saved;
        return result;
    }
    return GameWorld_TileSlope(world, edx, x, y);
}

void CompareTileSlope(test::Context& ctx, TileSlopeFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t size = ctx.rng().Chance(1, 10) ? ctx.rng().Below(5) : 384;
        // Map size 0 lets any x, y pass: keep them on the buffer then.
        const bool wild = size != 0 && ctx.rng().Chance(1, 10);
        const std::uint32_t x = wild ? ctx.rng().Next32() : ctx.rng().Below(386);
        const std::uint32_t y = wild && ctx.rng().Chance(1, 2) ? ctx.rng().Next32() : ctx.rng().Below(386);
        worlds.Both([&](GameWorld* w) { w->map_size = size; });
        if (x >= 1 && x < 384 && y >= 1 && y < 384) {
            const std::uint32_t base = ctx.rng().Below(256);
            for (std::int32_t dx = -1; dx <= 1; ++dx) {
                for (std::int32_t dy = -1; dy <= 1; ++dy) {
                    const auto h = static_cast<std::uint8_t>(base + ctx.rng().Below(20));
                    worlds.Both([&](GameWorld* w) { w->tiles[x + dx][y + dy].height = h; });
                }
            }
        }
        const std::uint32_t expected = Original_GameWorld_TileSlope(worlds.original(), nullptr, x, y);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, x, y);
        if (!S3RE_EXPECT_EQ(expected, actual)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: (%u, %u) map %u", i, x, y, size);
            return;
        }
    }
    S3RE_EXPECT(worlds.Checkpoint(ctx));
}

// Wrong: type 0x38 gets job 0xd8.
std::uint32_t __fastcall WrongMinerJobEnter(GameWorld* world, void* edx, std::uint32_t settler) {
    const std::uint32_t result = GameWorld_MinerJobEnter(world, edx, settler);
    layout::Settler& s = world->settlers[settler];
    if (world->buildings[s.building].art == 0x38) {
        s.job = 0xd8;
    }
    return result;
}

void CompareMinerJobEnter(test::Context& ctx, SettlerFn candidate, int cases) {
    test::WorldPair worlds(ctx, offsetof(GameWorld, buildings) + 0x101 * sizeof(layout::Building));
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t settler = ctx.rng().Below(0x7d00);
        const std::uint32_t building = ctx.rng().Below(0x100);
        static constexpr std::uint8_t kArts[] = {7, 8, 9, 0x1d, 0x25, 0x38, 6, 0x0a, 0x39, 0x24};
        const std::uint8_t art = ctx.rng().Chance(1, 10) ? static_cast<std::uint8_t>(ctx.rng().Next32())
                                                          : kArts[ctx.rng().Below(10)];
        const auto x = static_cast<std::uint16_t>(ctx.rng().Below(384));
        const auto y = static_cast<std::uint16_t>(ctx.rng().Below(384));
        worlds.Both([&](GameWorld* w) {
            w->settlers[settler].building = static_cast<std::uint16_t>(building);
            w->settlers[settler].x = x;
            w->settlers[settler].y = y;
            w->buildings[building].art = art;
        });
        const std::uint32_t expected = Original_GameWorld_MinerJobEnter(worlds.original(), nullptr, settler) -
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
    test::WorldPair worlds(ctx, sizeof(GameWorld));
    worlds.Both([&](GameWorld* w) {
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
    std::int32_t& mission = *reinterpret_cast<std::int32_t*>(0x007dfdb0);
    std::uint8_t& configByte = *reinterpret_cast<std::uint8_t*>(0x007a7af0 + 0x189);
    const std::int32_t savedMission = mission;
    const std::uint8_t savedConfig = configByte;
    std::vector<std::uint8_t> tallies(kTallyBytes), before(kTallyBytes);
    auto* const tallyBytes = reinterpret_cast<std::uint8_t*>(&Tally(0));
    std::memcpy(before.data(), tallyBytes, kTallyBytes);
    const std::vector<std::uint8_t> savedTallies = before;
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t settler = 1 + ctx.rng().Below(0x400);
        const std::uint32_t building = 1 + ctx.rng().Below(0xff);
        static constexpr std::uint8_t kMines[] = {7, 8, 9, 0x25, 0x1d, 0x38, 7, 8};
        const std::uint8_t art = kMines[ctx.rng().Below(8)];
        const std::uint32_t bx = 10 + ctx.rng().Below(360), by = 10 + ctx.rng().Below(360);
        const auto owner = static_cast<std::uint8_t>(ctx.rng().Below(4));
        const auto food = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0 : ctx.rng().Below(20));
        mission = ctx.rng().Chance(1, 2) ? 0 : 1 + static_cast<std::int32_t>(ctx.rng().Below(20));
        configByte = static_cast<std::uint8_t>(ctx.rng().Below(2));
        worlds.Both([&](GameWorld* w) {
            layout::Settler& s = w->settlers[settler];
            s.building = static_cast<std::uint16_t>(building);
            s.owner = owner;
            s.x = static_cast<std::uint16_t>(bx);
            s.y = static_cast<std::uint16_t>(by + 1);
            s.unk17 = food;
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
            worlds.Both([&](GameWorld* w) {
                w->buildings[building].work_x = history;
                w->buildings[building].unk1a = digs;
            });
            for (std::int32_t dx = -4; dx <= 4; ++dx) {
                for (std::int32_t dy = -3; dy <= 4; ++dy) {
                    const auto r = static_cast<std::uint8_t>(kKinds[ctx.rng().Below(5)] | ctx.rng().Below(16));
                    worlds.Both([&](GameWorld* w) { w->tiles[bx + dx][by + dy].resources = r; });
                }
            }
        } else {
            // Input piles with or without goods, an output pile around 7.
            for (std::uint32_t k = 0; k < 3; ++k) {
                const auto pile = static_cast<std::uint16_t>(ctx.rng().Chance(1, 6) ? 0 : 1 + ctx.rng().Below(0x3ff));
                const auto amount = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0 : 1 + ctx.rng().Below(3));
                const auto kind = static_cast<std::uint8_t>(ctx.rng().Chance(1, 2) ? 0 : 0xfe);
                worlds.Both([&](GameWorld* w) {
                    w->buildings[building].piles[k] = pile;
                    w->piles[pile].amount = amount;
                    w->piles[pile].art = kind;
                    w->piles[pile].good = static_cast<std::uint8_t>(1 + k);
                });
            }
            const auto out = static_cast<std::uint16_t>(1 + ctx.rng().Below(0x3ff));
            const auto outAmount = static_cast<std::uint8_t>(5 + ctx.rng().Below(5));
            const auto flags = static_cast<std::uint8_t>(ctx.rng().Next32());
            worlds.Both([&](GameWorld* w) {
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
std::uint32_t __fastcall WrongMinerJobFood(GameWorld* world, void* edx, std::uint32_t settler) {
    layout::Settler& s = world->settlers[settler];
    std::int32_t& active = world->players[s.owner].active;
    const std::int32_t saved = active;
    active = 1;
    const std::uint32_t result = GameWorld_MinerJobFood(world, edx, settler);
    active = saved;
    return result;
}

// Wrong: the dig history is not shifted when nothing is found.
std::uint32_t __fastcall WrongMinerJobDig(GameWorld* world, void* edx, std::uint32_t settler) {
    layout::Building& b = world->buildings[world->settlers[settler].building];
    const std::uint16_t x = b.work_x, y = b.work_y;
    const std::uint32_t result = GameWorld_MinerJobDig(world, edx, settler);
    if ((b.work_x & 1) == 0) {
        b.work_x = x;
        b.work_y = y;
    }
    return result;
}

}  // namespace

S3RE_TEST(GameWorld_MinerJobFood_MatchesOriginal) {
    CompareMinerJob(ctx, Original_GameWorld_MinerJobFood, &GameWorld_MinerJobFood, 4000, false);
}
S3RE_TEST(GameWorld_MinerJobFood_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMinerJob(probe, Original_GameWorld_MinerJobFood, &WrongMinerJobFood, 2000, false);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_MinerJobDig_MatchesOriginal) {
    CompareMinerJob(ctx, Original_GameWorld_MinerJobDig, &GameWorld_MinerJobDig, 4000, true);
}
S3RE_TEST(GameWorld_MinerJobDig_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMinerJob(probe, Original_GameWorld_MinerJobDig, &WrongMinerJobDig, 2000, true);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_MinerJobEnter_MatchesOriginal) {
    CompareMinerJobEnter(ctx, &GameWorld_MinerJobEnter, 20000);
}
S3RE_TEST(GameWorld_MinerJobEnter_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMinerJobEnter(probe, &WrongMinerJobEnter, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameWorld_TileSlope_MatchesOriginal) {
    CompareTileSlope(ctx, &GameWorld_TileSlope, 20000);
}
S3RE_TEST(GameWorld_TileSlope_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareTileSlope(probe, &WrongTileSlope, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
