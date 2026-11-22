// Taking a settler off its job (UpdateSettlers callees, package B).
//
// GameWorld::ResetSettlerToIdle (0x0050cf50): (settler): undoes whatever
//   the settler's current job (+0xe) had reserved, in this order:
//   1. jobs 0x37, 0x35, 0xb0, 0x8e (waiting jobs): the settler leaves its
//      player's idle list for its type (links +0x10 next / +0x12 previous,
//      heads Player::settler_lists).
//   2. types 0x28 and 0x16: jobs 0x35/0xb0 clear tile flag 0x2 at the
//      target (+0x24/+0x26); jobs 0xd4/0xd5/0x12e/0x12f at its own tile.
//   3. jobs 1, 2: the settler leaves its area's list for its type
//      (Area::settlers_by_type; area = the area word of its tile); area 0
//      logs "JobNothing/StepAside auf economysector 0 ..." and the tile.
//   4. jobs 0x68, 0x69: PileDecAmountLeaving(pile at the target).
//   5. the work jobs listed in kWorkJobs: the building (+0x22) loses the
//      worker: type 0 in jobs 3/4/0x8a by word +0x20 (1: byte +0x19, 2:
//      +0x1a, 3: +0x1b -= 1); otherwise by the type's class (0x658f64;
//      1: +0x19, 2: +0x1b, 3: +0x1f -= 1); class 0: type 1 (or jobs
//      0x68/0x69 with byte +0x18 == 1) clears the highest set bit of
//      building byte +0x16, type 2 (or byte +0x18 == 2) of +0x17, others
//      clear building word +0x8 and byte +0x19.
//   6. jobs 5..9: clears the highest set bit of building byte +0x16 and,
//      if there was one, building byte +0x1d -= 1; 7. jobs 0xe, 0xf, 0x11,
//      0x12: the same with +0x17.
//   8. jobs 6..9, 0x11, 0x12: tile flag 0x2 at the target cleared.
//   9. jobs 0x133, 0x134: the target tile's pile: leaving -= 1, twice if
//      settler byte +0x17 != 0.
//   10. jobs 0xa, 0xb, 0xa9, 0x4c, 0x4d: PileDecAmountLeaving(target pile).
//   11. jobs 0x4c..0x4f: a carried good (+0x15) is dropped (DropGoodNear,
//      SubtractPlayerCounter); then the pile in word +0x22: kind 0xfc:
//      leaving byte += 0xf0; else PileDecAmountComing and, if it is now
//      empty with nothing coming, MakePileEmpty (and SetPileGood with
//      PileNeighbourBuildingByteE's goods unless that is 0xff).
//   12. jobs 0xa, 0xb, 0xa9, 0xaa, 0xc, 0xd, 0x50: the building's input
//      pile for the good (+0x1a for jobs 0xa/0xb/0xa9, else +0x15) gets
//      PileDecAmountComing (under construction: good 1 -> pile 0, 2 ->
//      pile 1; otherwise the first of its three piles with that goods
//      type).
//   13. jobs 0xaa, 0xc, 0xd, 0x50: the carried good is dropped and +0x15
//      cleared.
//   14. if the settler's tile has another settler word: PlaceSettlerNear.
//   15. a tile without walk region logs "Siedler steht auf sector 0 ...".
//   16. job = 0xac (idle), +0x15 = 0, +0x22 = 0, counter = 0; a dword +0x8
//      above 5 becomes RandomBelow6().
//   EAX = dword +0x8 (callers use AL at most). ret 4. 14 callers (walking,
//   building removal, ExecuteCommands, ...).
// thiscall (ECX = world).

#include "game/debug_log.h"
#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace s3re::game {

std::uint32_t __fastcall GameWorld_PileDecAmountLeaving(GameWorld* world, void* edx, std::uint32_t pile);
std::uint32_t __fastcall GameWorld_PileDecAmountComing(GameWorld* world, void* edx, std::uint32_t pile);
std::uint32_t __fastcall GameWorld_PileNeighbourBuildingByteE(GameWorld* world, void* edx, std::uint32_t pile);
std::uint32_t __fastcall GameWorld_MakePileEmpty(GameWorld* world, void* edx, std::uint32_t pile);
std::uint32_t __fastcall GameWorld_SetPileGood(GameWorld* world, void* edx, std::uint32_t pile, std::uint32_t good);
std::uint32_t __fastcall GameWorld_DropGoodNear(GameWorld* world, void* edx, std::uint32_t good, std::uint32_t amount,
                                                std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall GameWorld_SubtractPlayerCounter(GameWorld* world, void* edx, std::uint32_t player,
                                                         std::uint32_t index, std::uint32_t amount);
std::uint32_t __fastcall GameWorld_PlaceSettlerNear(GameWorld* world, void* edx, std::uint32_t settler,
                                                    std::uint32_t x, std::uint32_t y);

namespace {

using layout::Building;
using layout::Pile;
using layout::Settler;
using layout::Tile;

const std::int32_t* const kTypeClass = reinterpret_cast<const std::int32_t*>(0x00658f64);
constexpr std::uintptr_t kSectorZeroText = 0x006a6784;   // "JobNothing/StepAside auf economysector 0 ..."
constexpr std::uintptr_t kTileText = 0x006a6740;         // "Owner %u, world[x][y].owner %u, object %u ..."
constexpr std::uintptr_t kNoRegionText = 0x006a6704;     // "Siedler steht auf sector 0, art %u ..."

constexpr std::uint16_t kWorkJobs[] = {
    3,    4,    0x8a, 0x68, 0x69, 0x1c, 0x1d, 0x20, 0x21, 0x22, 0x23, 0x2a, 0x2b, 0x2d, 0x2e, 0x2f, 0x30,
    0x90, 0x91, 0x92, 0x93, 0x96, 0x97, 0x98, 0x9c, 0xb5, 0xb2, 0xbb, 0xbc, 0xe5, 0xe6, 0xe7, 0xe8, 0xeb,
    0xec, 0xed, 0xf1, 0xf6, 0xf7, 0xf8, 0xf9, 0xfc, 0xfd, 0xfe, 0x102, 0x165, 0x168};

bool IsWorkJob(std::uint16_t job) {
    for (const std::uint16_t j : kWorkJobs) {
        if (j == job) {
            return true;
        }
    }
    return false;
}

bool OneOf(std::uint16_t job, std::initializer_list<std::uint16_t> jobs) {
    for (const std::uint16_t j : jobs) {
        if (j == job) {
            return true;
        }
    }
    return false;
}

// Clears the highest set bit of `byte` (the original's chain of tests);
// false if none was set.
bool ClearHighestBit(std::uint8_t& byte) {
    for (std::uint32_t bit = 0x80; bit != 0; bit >>= 1) {
        if ((byte & bit) != 0) {
            byte = static_cast<std::uint8_t>(byte - bit);
            return true;
        }
    }
    return false;
}

Tile& TileAt(GameWorld* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

// Removes the settler from a doubly linked settler list whose head is `head`.
void Unlink(GameWorld* world, Settler& s, std::uint16_t& head) {
    if (s.prev != 0) {
        world->settlers[s.prev].next = s.next;
    } else {
        head = s.next;
    }
    if (s.next != 0) {
        world->settlers[s.next].prev = s.prev;
    }
}

}  // namespace

using SettlerFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t settler);

// Purpose: Takes a settler off its current job (e.g. when its building is destroyed or the player
// changes something): reservations of piles and building slots are given back, carried goods are
// dropped, and the settler becomes idle.
std::uint32_t __fastcall GameWorld_ResetSettlerToIdle(GameWorld* world, void*, std::uint32_t settler) {
    Settler& s = world->settlers[settler];
    // The original keeps one dword of its frame for the goods byte of
    // step 11; its upper bytes are ECX's (the world pointer) at entry.
    std::uint32_t savedGoods = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(world));

    if (OneOf(s.job, {0x37, 0x35, 0xb0, 0x8e})) {
        // The list head: player record word +0x20 + type * 2 (for owner 0xff
        // past the players, as in the original's arithmetic).
        Unlink(world, s, world->players[s.owner].settler_lists[s.art]);
    }
    if (s.art == 0x28 || s.art == 0x16) {
        if (s.job == 0x35 || s.job == 0xb0) {
            TileAt(world, s.target_x, s.target_y).flags &= 0xfd;
        }
        if (OneOf(s.job, {0xd4, 0xd5, 0x12e, 0x12f})) {
            TileAt(world, s.x, s.y).flags &= 0xfd;
        }
    }
    if (s.job == 1 || s.job == 2) {
        const std::uint32_t area = TileAt(world, s.x, s.y).area;
        if (area == 0) {
            GameDebugLog(kSectorZeroText, static_cast<std::uint32_t>(s.art), static_cast<std::uint32_t>(s.x),
                         static_cast<std::uint32_t>(s.y));
            const Tile& tile = TileAt(world, s.x, s.y);
            const std::uint32_t owner = s.owner;
            // Argument order as the original pushes them.
            GameDebugLog(kTileText, owner, static_cast<std::uint32_t>(tile.owner), owner,
                         static_cast<std::uint32_t>(tile.object), owner, static_cast<std::uint32_t>(tile.area), owner,
                         static_cast<std::uint32_t>(tile.terrain));
        }
        Unlink(world, s, world->areas[area].settlers_by_type[s.art]);
    }
    if (s.job == 0x68 || s.job == 0x69) {
        GameWorld_PileDecAmountLeaving(world, nullptr, TileAt(world, s.target_x, s.target_y).object_number);
    }
    if (IsWorkJob(s.job)) {
        const std::uint8_t art = s.art;
        if (art == 0 && (s.job == 3 || s.job == 4 || s.job == 0x8a)) {
            if (s.saved_counter == 1) {
                --world->buildings[s.building].recruiting.recruits[0];
            }
            if (s.saved_counter == 2) {
                --world->buildings[s.building].recruiting.recruits[1];
            }
            if (s.saved_counter == 3) {
                --world->buildings[s.building].recruiting.recruits[2];
            }
        } else {
            const std::int32_t typeClass = kTypeClass[art];
            if (typeClass != 0) {
                if (typeClass == 1) {
                    --world->buildings[s.building].military.class1_coming;
                } else if (typeClass == 2) {
                    --world->buildings[s.building].military.class2_coming;
                } else if (typeClass == 3) {
                    --world->buildings[s.building].military.class3_coming;
                }
            } else {
                const bool carrierJob = s.job == 0x68 || s.job == 0x69;
                if (art == 1 || (carrierJob && s.tool_type == 1)) {
                    ClearHighestBit(world->buildings[s.building].digger_slots);
                } else if (art == 2 || (carrierJob && s.tool_type == 2)) {
                    ClearHighestBit(world->buildings[s.building].builder_slots);
                } else {
                    world->buildings[s.building].worker = 0;
                    world->buildings[s.building].production.worker_inside = 0;
                }
            }
        }
    }
    if (s.job >= 5 && s.job <= 9) {
        Building& b = world->buildings[s.building];
        if (ClearHighestBit(b.digger_slots)) {
            --b.site.workers_at_work;
        }
    }
    if (OneOf(s.job, {0xe, 0xf, 0x11, 0x12})) {
        Building& b = world->buildings[s.building];
        if (ClearHighestBit(b.builder_slots)) {
            --b.site.workers_at_work;
        }
    }
    if (OneOf(s.job, {6, 7, 9, 8, 0x11, 0x12})) {
        Tile& target = TileAt(world, s.target_x, s.target_y);
        if ((target.flags & 2) != 0) {
            target.flags &= 0xfd;
        }
    }
    if (s.job == 0x133 || s.job == 0x134) {
        --world->piles[TileAt(world, s.target_x, s.target_y).object_number].leaving;
        if (s.second_unit != 0) {
            --world->piles[TileAt(world, s.target_x, s.target_y).object_number].leaving;
        }
    }
    if (OneOf(s.job, {0xa, 0xb, 0xa9, 0x4c, 0x4d})) {
        GameWorld_PileDecAmountLeaving(world, nullptr, TileAt(world, s.target_x, s.target_y).object_number);
    }
    if (OneOf(s.job, {0x4c, 0x4d, 0x4e, 0x4f})) {
        if (s.slot != 0) {
            GameWorld_DropGoodNear(world, nullptr, s.slot, 1, s.x, s.y);
            GameWorld_SubtractPlayerCounter(world, nullptr, s.owner, s.slot, 1);
        }
        const std::uint32_t pile = s.building;  // word +0x22 holds a pile in these jobs
        Pile& p = world->piles[pile];
        if (p.art == 0xfc) {
            p.leaving = static_cast<std::uint8_t>(p.leaving + 0xf0);
        } else {
            GameWorld_PileDecAmountComing(world, nullptr, pile);
            if (p.amount == 0 && p.coming == 0) {
                const std::uint32_t goods = GameWorld_PileNeighbourBuildingByteE(world, nullptr, pile);
                savedGoods = (savedGoods & 0xffffff00u) | (goods & 0xff);
                GameWorld_MakePileEmpty(world, nullptr, pile);
                if ((goods & 0xff) != 0xff) {
                    GameWorld_SetPileGood(world, nullptr, pile, savedGoods);
                }
            }
        }
    }
    if (OneOf(s.job, {0xa, 0xb, 0xa9, 0xaa, 0xc, 0xd, 0x50})) {
        std::uint32_t good = s.slot;
        if (s.job == 0xa || s.job == 0xb || s.job == 0xa9) {
            good = s.good;
        }
        const Building& b = world->buildings[s.building];
        if (b.buildup != 0) {
            if (good == 1) {
                GameWorld_PileDecAmountComing(world, nullptr, b.piles[0]);
            } else if (good == 2) {
                GameWorld_PileDecAmountComing(world, nullptr, b.piles[1]);
            }
        } else {
            for (const std::uint16_t pile : b.piles) {
                if (world->piles[pile].good == good) {
                    GameWorld_PileDecAmountComing(world, nullptr, pile);
                    break;
                }
            }
        }
    }
    if (OneOf(s.job, {0xaa, 0xc, 0xd, 0x50})) {
        GameWorld_DropGoodNear(world, nullptr, s.slot, 1, s.x, s.y);
        GameWorld_SubtractPlayerCounter(world, nullptr, s.owner, s.slot, 1);
        s.slot = 0;
    }
    if (TileAt(world, s.x, s.y).settler != settler) {
        GameWorld_PlaceSettlerNear(world, nullptr, settler, s.x, s.y);
    }
    if (TileAt(world, s.x, s.y).walk_region == 0) {
        GameDebugLog(kNoRegionText, static_cast<std::uint32_t>(s.art), static_cast<std::uint32_t>(s.x),
                     static_cast<std::uint32_t>(s.y), static_cast<std::uint32_t>(s.job));
        const Tile& tile = TileAt(world, s.x, s.y);
        GameDebugLog(kTileText, static_cast<std::uint32_t>(s.owner), static_cast<std::uint32_t>(tile.owner),
                     static_cast<std::uint32_t>(tile.object), static_cast<std::uint32_t>(tile.area),
                     static_cast<std::uint32_t>(tile.terrain));
    }
    s.job = 0xac;
    s.slot = 0;
    s.building = 0;
    s.counter = 0;
    if (s.direction > 5) {
        s.direction = GameWorld_RandomBelow6(world, nullptr);
    }
    return s.direction;
}

S3RE_HOOK(0x0050cf50, GameWorld_ResetSettlerToIdle, SettlerFn, Original_GameWorld_ResetSettlerToIdle,
          GameWorld_ResetSettlerToIdle);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestSettlers = 0x400;
constexpr std::uint32_t kTestBuildings = 0x100;
constexpr std::uint32_t kTestPiles = 0x400;

// The game's offset table (DropGoodNear, PlaceSettlerNear walk it).
void FillOffsetSpiral(test::WorldPair& worlds) {
    std::uint32_t k = 0;
    for (std::int32_t r = 0; k < 10000; ++r) {
        for (std::int32_t dx = -r; dx <= r && k < 10000; ++dx) {
            for (std::int32_t dy = -r; dy <= r && k < 10000; ++dy) {
                if (TileDistance(0, 0, static_cast<std::uint32_t>(dx), static_cast<std::uint32_t>(dy)) ==
                    static_cast<std::uint32_t>(r)) {
                    worlds.Both([&](GameWorld* w) {
                        w->offsets[k].dx = dx;
                        w->offsets[k].dy = dy;
                    });
                    ++k;
                }
            }
        }
    }
}

// Jobs that reach every step (and some others).
constexpr std::uint16_t kTestJobs[] = {
    0x37, 0x35, 0xb0, 0x8e, 0xd4, 0xd5, 0x12e, 0x12f, 1,    2,    0x68, 0x69, 3,    4,    0x8a,
    0x1c, 0x2a, 0xe5, 0x168, 5,   6,    7,    8,     9,    0xe,  0xf,  0x11, 0x12, 0x133, 0x134,
    0xa,  0xb,  0xa9, 0x4c, 0x4d, 0x4e, 0x4f, 0xaa,  0xc,  0xd,  0x50, 0xac, 0x100, 0x55};
constexpr std::uint8_t kTestTypes[] = {0, 1, 2, 0x16, 0x28, 0x0f, 0x20, 0x08, 0x1e, 0x05, 0x0a, 0x30};

void SetUpCase(test::Context& ctx, test::WorldPair& worlds, std::uint32_t settler) {
    auto& rng = ctx.rng();
    const std::uint16_t job = kTestJobs[rng.Below(sizeof(kTestJobs) / sizeof(kTestJobs[0]))];
    const std::uint8_t art = kTestTypes[rng.Below(sizeof(kTestTypes))];
    const auto owner = static_cast<std::uint8_t>(rng.Below(4));
    const std::uint32_t x = 10 + rng.Below(360), y = 10 + rng.Below(360);
    const std::uint32_t tx = x + rng.Below(5) - 2, ty = y + rng.Below(5) - 2;
    const std::uint32_t building = 1 + rng.Below(kTestBuildings);
    const std::uint32_t area = rng.Chance(1, 8) ? 0 : 1 + rng.Below(0x100);
    auto link = [&]() { return static_cast<std::uint16_t>(rng.Chance(1, 3) ? 0 : 1 + rng.Below(kTestSettlers)); };
    const std::uint16_t next = link(), prev = link();
    const auto direction = rng.Chance(1, 4) ? rng.Next32() : rng.Below(6);
    const auto slot = static_cast<std::uint8_t>(rng.Chance(1, 3) ? 0 : 1 + rng.Below(35));
    const auto good = static_cast<std::uint8_t>(rng.Below(36));
    const auto saved = static_cast<std::uint16_t>(rng.Below(5));
    const auto secondUnit = static_cast<std::uint8_t>(rng.Below(2));
    const auto toolType = static_cast<std::uint8_t>(rng.Below(4));
    const std::uint32_t pileWord = (job >= 0x4c && job <= 0x4f) ? 1 + rng.Below(kTestPiles) : building;
    const bool onTile = rng.Chance(3, 4);
    const auto region = static_cast<std::uint8_t>(rng.Chance(1, 8) ? 0 : 1 + rng.Below(5));
    const auto targetPile = static_cast<std::uint16_t>(1 + rng.Below(kTestPiles));
    worlds.Both([&](GameWorld* w) {
        Settler& s = w->settlers[settler];
        s.job = job;
        s.art = art;
        s.owner = owner;
        s.x = static_cast<std::uint16_t>(x);
        s.y = static_cast<std::uint16_t>(y);
        s.target_x = static_cast<std::uint16_t>(tx);
        s.target_y = static_cast<std::uint16_t>(ty);
        s.building = static_cast<std::uint16_t>(pileWord);
        s.next = next;
        s.prev = prev;
        s.direction = direction;
        s.slot = slot;
        s.good = good;
        s.saved_counter = saved;
        s.second_unit = secondUnit;
        s.tool_type = toolType;
        Tile& tile = w->tiles[x][y];
        tile.area = static_cast<std::uint16_t>(area);
        tile.settler = static_cast<std::uint16_t>(onTile ? settler : 1 + (settler + 7) % kTestSettlers);
        tile.walk_region = region;
        w->tiles[tx][ty].object_number = targetPile;
    });
    // The building's piles: goods types around the settler's.
    for (std::uint32_t k = 0; k < 3; ++k) {
        const auto pile = static_cast<std::uint16_t>(1 + rng.Below(kTestPiles));
        const auto pileGood = static_cast<std::uint8_t>(rng.Chance(1, 3) ? (rng.Chance(1, 2) ? slot : good) : rng.Below(36));
        const auto buildup = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : 1 + rng.Below(3));
        worlds.Both([&](GameWorld* w) {
            w->buildings[building].piles[k] = pile;
            w->piles[pile].good = pileGood;
            w->buildings[building].buildup = buildup;
        });
    }
    if (job >= 0x4c && job <= 0x4f) {
        const auto art2 = static_cast<std::uint8_t>(rng.Chance(1, 4) ? 0xfc : rng.Next32());
        const auto amount = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : rng.Below(5));
        const auto coming = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 1 : rng.Below(5));
        const auto px = static_cast<std::uint16_t>(10 + rng.Below(360));
        const auto py = static_cast<std::uint16_t>(10 + rng.Below(360));
        worlds.Both([&](GameWorld* w) {
            Pile& p = w->piles[pileWord];
            p.art = art2;
            p.amount = amount;
            p.coming = coming;
            p.x = px;
            p.y = py;
            w->tiles[px][py].area = static_cast<std::uint16_t>(area);
        });
    }
}

void Compare(test::Context& ctx, SettlerFn candidate, int cases) {
    test::WorldPair worlds(ctx, sizeof(GameWorld));
    FillOffsetSpiral(worlds);
    worlds.Both([&](GameWorld* w) {
        w->map_size = 384;
        // Pile records with coordinates on the map, linked within the test
        // range; buildings piles on the map too.
        for (std::uint32_t p = 0; p < 0x7d00; ++p) {
            Pile& pile = w->piles[p];
            pile.x = static_cast<std::uint16_t>(pile.x % 384);
            pile.y = static_cast<std::uint16_t>(pile.y % 384);
            pile.prev = static_cast<std::uint16_t>(pile.prev % kTestPiles);
            pile.next = static_cast<std::uint16_t>(pile.next % kTestPiles);
            if (p > kTestPiles) {
                pile.good = 1;  // CreatePile finds free records below
            }
        }
        for (std::uint32_t s = 0; s < 0x7d00; ++s) {
            Settler& r = w->settlers[s];
            r.x = static_cast<std::uint16_t>(r.x % 384);
            r.y = static_cast<std::uint16_t>(r.y % 384);
            r.target_x = static_cast<std::uint16_t>(r.target_x % 384);
            r.target_y = static_cast<std::uint16_t>(r.target_y % 384);
            r.building = static_cast<std::uint16_t>(r.building % kTestBuildings);
        }
        for (auto& row : w->tiles) {
            for (Tile& tile : row) {
                tile.object_number = static_cast<std::uint16_t>(tile.object_number % kTestPiles);
                tile.area = static_cast<std::uint16_t>(tile.area % 0x200);
            }
        }
    });
    DebugLogRecorder logs;
    std::size_t logged = 0;
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t settler = 1 + ctx.rng().Below(kTestSettlers);
        SetUpCase(ctx, worlds, settler);
        const std::uint32_t expected = Original_GameWorld_ResetSettlerToIdle(worlds.original(), nullptr, settler);
        const std::vector<DebugLogCall> expectedLogs = logs.Take();
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, settler);
        const std::vector<DebugLogCall> actualLogs = logs.Take();
        logged += expectedLogs.size();
        if (!S3RE_EXPECT_EQ(expected, actual) || !S3RE_EXPECT(expectedLogs == actualLogs) ||
            ((i % 64 == 63 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 63 before it): settler %u", i, settler);
            return;
        }
    }
    S3RE_EXPECT(logged > 0);
}

// Wrong: the class-3 soldiers give back building byte +0x1b instead of +0x1f.
std::uint32_t __fastcall WrongResetSettlerToIdle(GameWorld* world, void* edx, std::uint32_t settler) {
    Settler& s = world->settlers[settler];
    if (IsWorkJob(s.job) && s.art != 0 && kTypeClass[s.art] == 3) {
        Building& b = world->buildings[s.building];
        ++b.military.class3_coming;
        --b.military.class2_coming;
    }
    return GameWorld_ResetSettlerToIdle(world, edx, settler);
}

}  // namespace

S3RE_TEST(GameWorld_ResetSettlerToIdle_MatchesOriginal) {
    Compare(ctx, &GameWorld_ResetSettlerToIdle, 6000);
}
S3RE_TEST(GameWorld_ResetSettlerToIdle_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Compare(probe, &WrongResetSettlerToIdle, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
