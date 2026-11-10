// Building sites: where a building may go, how much the ground has to be
// levelled for it, and putting down a new construction site.
//
// Building layouts. S3.EXE's data holds one layout per race and building
// type at 0x6aa170 + (race * 57 + type) * 2400 (600 int32 each; types 1..56
// are filled for all four races). A layout is a header followed by lists of
// (dx, dy) tile offsets from the building's position, each list ended by a
// marker:
//   [0]          a small number (copied to building +0x1c when a site is placed)
//   [1]          construction materials: high byte -> building +0x19, low
//                byte -> +0x1a (compared with the amounts of the site's piles 1
//                and 2, probably boards and stones)
//   0x80000005   (a list not used here)
//   0x80000004   door offset (dx, dy), then more offsets whose object byte
//                is cleared to 0xff for some types (see AddBuildingSite)
//   0x80000000   footprint: the tiles the building stands on; they are
//                levelled to one height. A 0x80000001 inside the list ends
//                the part whose outermost tile per direction gets a stake
//                when the site is placed.
//   0x80000002   border: tiles around the footprint that must be free but
//                keep their height
//   0x80000003   end
//
// GameWorld::CheckBuildingSite (0x004d99e0): (x, y, type, player, mode):
//   mode 0 only checks, mode 1 also places the construction site; mode + 2
//   does the same for unowned land (the tiles must belong to nobody, 0xff,
//   instead of the player; used for the start castle). Returns 0 if the
//   building cannot go there, else a size class 1..10 of the earth to move
//   (1: flat; thresholds 30, 70, 120, 180, 250, 330, 420, 510, 600).
//   Checks: x and y in 15..mapSize-16; every footprint and border tile owned,
//   free (flags 0x1/0x4/0x8 clear, object word +0x2 0) and in the same
//   economy area as the building's tile; ground class (terrain high nibble)
//   0x10 for the race group 0 (races 0, 3), 0x10/0x40 for race 1, 0x10/0x90
//   for race 2. Mines (types 7, 8, 9, 0x1d, 0x25, 0x38) only need mountain
//   (0x20) under the building's own tile and are never levelled. Types 0x20
//   and 0x21 (shipyard, harbour?) also need a water tile (terrain byte 4)
//   within distance 20. The levelling plan (PlanSiteLevelling) for the
//   rounded mean height of the footprint must exist.
//   A border tile off the map is only logged (DebugLog "illegal building
//   offset"), as in the original.
//   Placing (mode 1): needs a free levelling slot of the player (30 per
//   player, the site cache at world+0x1196024); AddBuildingSite creates the
//   record; QueuePendingEvent(2, building, 0); every footprint and border
//   tile gets the footprint flag 0x08; per direction sector the outermost
//   stake tile gets object 0x71, the building's tile object 0x70. Mines get
//   buildup 2 (no levelling). Others: building +0x22 = slot, +0x30 = earth
//   amount, +0x1b = min(8, (amount + 16) / 32 + 2) (8-bit arithmetic); the
//   cache slot is marked used and receives the 31x31 target heights (0 where
//   the height stays); tiles whose height changes get flag 0x04 ("being
//   levelled") and building word +0x28 sums |height - target|. ret 0x14.
// GameWorld::PlanSiteLevelling (0x004da440): (x, y, type, player, height)
//   plans the levelling in a 31x31 window centred on (x, y): grid H
//   (world+0x1222ed4, cell [i][j] = i * 31 + j for tile (x-15+i, y-15+j))
//   gets the current heights, grid S (world+0x1223295) a state per cell:
//   0 free, 1 to propagate, 2 changed in this round, 100 fixed. Fixed: the
//   window edge, tiles off the map, blocked/footprint tiles (flags 0x1/0x8),
//   tiles not owned by `player`, ground class 0x30, and a tile with flag 0x04
//   together with five of its neighbours. The footprint cells get the target
//   height and state 1. Then rounds over the inner 29x29 cells: a state-1
//   cell becomes fixed and pulls four of its neighbours ((i-1, j-1) and
//   (i, j-1) at most 5 below / 7 above it, (i+1, j+1) and (i, j+1) at most 7
//   below / 5 above) into range; a free neighbour is moved and propagates
//   in the next round, a neighbour in state 1, 2 or 100 that would have to
//   move makes the plan fail. EAX = 0 on failure, else the sum of
//   |planned - current| over the inner cells, or 1 if that is 0. The grids
//   stay as computed (CheckBuildingSite copies H when it places a site).
//   ret 0x14.
// GameWorld::AddBuildingSite (0x004dd280): (x, y, player, type): takes the
//   first free building record 1..0xf9e (owner byte 0xff; 0xf9f counts as
//   "none": EAX = 0), raises the high-water mark (world+0x1499e60), and
//   fills it as a construction site: owner, x, y, type, race, buildup 1,
//   +0x18 = 1, materials from the layout (+0x19, +0x1a, their sum +0x1f),
//   +0x32 = the player's countdown +0x84 (decremented; at 60000 or below
//   it restarts at 0xffff), door position +0xa/+0xc, everything else 0.
//   The tile gets the building number (word +0x2, flag 0x20). For some types
//   the object byte of the tiles listed after the door becomes 0xff. Two
//   empty construction piles (goods 1 left of the door, goods 2 right of it,
//   kind 0xfd, CreatePile) go to +0x10/+0x12. +0x0e/+0x14 (work centre):
//   0xff for type 1; the position for types 2, 3, 5, 0x19, 0x1e, 0x33; the
//   position + (4, 8) / (5, 10) / (6, 12) clamped to the map for types 0xe /
//   0x18 / 0x24. EAX = the building. ret 0x10.
// GameWorld::IsFreeCastleSite (0x00508420): (player, x, y): 1 if
//   CheckBuildingSite(x, y, castle 0xf, player, mode 2) is non-zero, else 0.
//   ret 0xc.
// GameWorld::IsGoodStartPosition (0x005081a0): (player, x, y, attempts):
//   0 unless a castle fits (as above); then counts over the offset table
//   (world+0x110a588) around (x, y), tiles off the map skipped: within the
//   first 3000 entries grass (ground class 0x10), trees (objects
//   0x44..0x53), stone (sum of 0x7f - object for 0x73..0x7e) and unowned
//   tiles; within the first 9995 the resource kinds (tile +0x11 high
//   nibble 1 coal, 2 iron, 3 gold, 4 gems, 5 sulfur). Demands by attempts:
//   < 30000: unowned >= 2900, grass >= 2000, trees 20..30, stone 50..100,
//   coal >= 40, iron >= 20, gold >= 10, sulfur >= 5, gems >= 5; < 60000:
//   2500, 1500, trees >= 12, stone >= 30, 30, 15, 5; < 100000: 1500, 1000,
//   5, 15, 20, 10; otherwise anything. EAX 1 or 0. ret 0x10.

#include "game/debug_log.h"
#include "game/raw.h"
#include "game/replacements.h"
#include "game/world.h"
#include "hooks.h"
#include "log.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

using layout::Building;
using layout::MapOffset;
using layout::PlayerSiteCache;
using layout::Tile;

constexpr std::uint8_t kFlagBlocked = layout::kTileBlocked;
constexpr std::uint8_t kFlagLevelling = 0x04;
constexpr std::uint8_t kFlagFootprint = layout::kTileBuildingFootprint;
constexpr std::uint8_t kFlagBuilding = layout::kTileHasBuilding;

constexpr std::uint32_t kSiteSlots = 30;

// The levelling window: two 31 x 31 byte grids in GameWorld::footprint_scratch
// (heights at world+0x1222ed4, states at world+0x1223295).
constexpr std::int32_t kGrid = 31;
constexpr std::int32_t kGridCentre = 15;

constexpr std::uint32_t kBuildingLimit = 0xf9f;

constexpr std::uintptr_t kLayouts = 0x006aa170;
constexpr std::uint32_t kLayoutBytes = 2400;
constexpr std::uint32_t kLayoutTypes = 57;
constexpr std::uint32_t kMarkFootprint = 0x80000000;
constexpr std::uint32_t kMarkNoStakes = 0x80000001;
constexpr std::uint32_t kMarkBorder = 0x80000002;
constexpr std::uint32_t kMarkEnd = 0x80000003;
constexpr std::uint32_t kMarkDoor = 0x80000004;

constexpr std::uint32_t kCastleType = 0xf;
constexpr std::uint32_t kModeCheckUnowned = 2;

constexpr std::uintptr_t kIllegalOffsetMessage = 0x006a587c;  // "illegal building offset %u %u %u %u"

// Ground requirements (CheckBuildingSite).
enum class SiteGroup { kPlain, kRace1, kRace2, kMine };

// Tile (x, y); the index wraps in 32 bits like the original's.
Tile& TileAt(GameWorld* world, std::uint32_t x, std::uint32_t y) {
    return TileAtOffset(world, TileOffset(x, y));
}

std::uint32_t PlayerRace(GameWorld* world, std::uint32_t player) {
    return static_cast<std::uint32_t>(world->players[player].race);
}

const std::uint32_t* Layout(std::uint32_t race, std::uint32_t type) {
    return reinterpret_cast<const std::uint32_t*>(kLayouts + (race * kLayoutTypes + type) * kLayoutBytes);
}

// The entry after the first `marker` of the layout.
const std::uint32_t* After(const std::uint32_t* p, std::uint32_t marker) {
    while (*p++ != marker) {
    }
    return p;
}

bool IsMine(std::uint32_t type) {
    return type == 7 || type == 9 || type == 8 || type == 0x25 || type == 0x1d || type == 0x38;
}

// The levelling window: cell [i][j] (index i * 31 + j) is tile
// (x - 15 + i, y - 15 + j). The index may lie outside 0..960 for odd
// layouts; the original's address arithmetic is the same.
std::uint8_t& HeightCell(GameWorld* world, std::int32_t cell) {
    return world->footprint_scratch[0][cell];
}
std::uint8_t& StateCell(GameWorld* world, std::int32_t cell) {
    return world->footprint_scratch[1][cell];
}
std::uint8_t& HeightCell(GameWorld* world, std::int32_t i, std::int32_t j) {
    return HeightCell(world, i * kGrid + j);
}
std::uint8_t& StateCell(GameWorld* world, std::int32_t i, std::int32_t j) {
    return StateCell(world, i * kGrid + j);
}

constexpr std::uint8_t kCellFree = 0;
constexpr std::uint8_t kCellPending = 1;
constexpr std::uint8_t kCellChanged = 2;
constexpr std::uint8_t kCellFixed = 100;

// One neighbour of a cell at height `h` must lie within [h - maxBelow,
// h + maxAbove]. A free neighbour is moved to the limit and propagates in
// the next round; a pending, changed or fixed one fails the plan.
void PullNeighbour(GameWorld* world, std::int32_t i, std::int32_t j, std::int32_t h, std::int32_t maxBelow,
                   std::int32_t maxAbove, bool& changed, bool& failed) {
    std::uint8_t& height = HeightCell(world, i, j);
    std::uint8_t& state = StateCell(world, i, j);
    auto pull = [&](std::int32_t target) {
        if (state == kCellFree) {
            state = kCellChanged;
            height = static_cast<std::uint8_t>(target);
            changed = true;
        } else if (state == kCellPending || state == kCellChanged || state == kCellFixed) {
            failed = true;
        }
    };
    if (height + maxBelow < h) {
        pull(h - maxBelow);
    }
    if (height - maxAbove > h) {
        pull(h + maxAbove);
    }
}

}  // namespace

using CheckBuildingSiteFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x,
                                                       std::uint32_t y, std::uint32_t type, std::uint32_t player,
                                                       std::uint32_t mode);
using PlanSiteLevellingFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x,
                                                       std::uint32_t y, std::uint32_t type, std::uint32_t player,
                                                       std::uint32_t height);
using AddBuildingSiteFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                     std::uint32_t player, std::uint32_t type);

std::uint32_t __fastcall GameWorld_CheckBuildingSite(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                     std::uint32_t type, std::uint32_t player, std::uint32_t mode);
std::uint32_t __fastcall GameWorld_PlanSiteLevelling(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                     std::uint32_t type, std::uint32_t player, std::uint32_t height);
std::uint32_t __fastcall GameWorld_AddBuildingSite(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                   std::uint32_t player, std::uint32_t type);

S3RE_HOOK(0x004d99e0, GameWorld_CheckBuildingSite, CheckBuildingSiteFn, Original_GameWorld_CheckBuildingSite,
          GameWorld_CheckBuildingSite);
S3RE_HOOK(0x004da440, GameWorld_PlanSiteLevelling, PlanSiteLevellingFn, Original_GameWorld_PlanSiteLevelling,
          GameWorld_PlanSiteLevelling);
S3RE_HOOK(0x004dd280, GameWorld_AddBuildingSite, AddBuildingSiteFn, Original_GameWorld_AddBuildingSite,
          GameWorld_AddBuildingSite);

// Purpose: Works out how the ground around a planned building has to be
// levelled: the building's tiles get one height and the slopes around them
// are evened out; returns how much earth that moves (0 if impossible).
// Used for the placement preview and when a building site is placed.
std::uint32_t __fastcall GameWorld_PlanSiteLevelling(GameWorld* world, void* /*edx*/, std::uint32_t x,
                                                     std::uint32_t y, std::uint32_t type, std::uint32_t player,
                                                     std::uint32_t height) {
    std::memset(&StateCell(world, 0, 0), kCellFree, kGrid * kGrid);
    const std::uint32_t mapSize = world->map_size;
    for (std::int32_t j = 0; j < kGrid; ++j) {
        for (std::int32_t i = 0; i < kGrid; ++i) {
            const std::uint32_t tx = x - kGridCentre + i;
            const std::uint32_t ty = y - kGridCentre + j;
            const Tile& tile = TileAt(world, tx, ty);
            HeightCell(world, i, j) = tile.height;
            const bool edge = tx >= mapSize || ty >= mapSize || i == 0 || j == 0 || i == kGrid - 1 || j == kGrid - 1;
            const std::uint8_t flags = tile.flags;
            if (!edge && (flags & kFlagLevelling) != 0) {
                // Another site is levelling this tile: keep it and its neighbours.
                StateCell(world, i, j) = kCellFixed;
                StateCell(world, i + 1, j) = kCellFixed;
                StateCell(world, i - 1, j) = kCellFixed;
                StateCell(world, i, j + 1) = kCellFixed;
                StateCell(world, i, j - 1) = kCellFixed;
                StateCell(world, i + 1, j + 1) = kCellFixed;
                StateCell(world, i - 1, j - 1) = kCellFixed;
            } else if (edge || (flags & (kFlagBlocked | kFlagFootprint)) != 0 || tile.owner != player ||
                       (tile.terrain & 0xf0) == 0x30) {
                StateCell(world, i, j) = kCellFixed;
            }
        }
    }

    // The footprint is levelled to `height`.
    const std::uint32_t* p = After(Layout(PlayerRace(world, player), type), kMarkFootprint);
    while (*p != kMarkBorder) {
        if (*p == kMarkNoStakes) {
            ++p;
        }
        const std::int32_t cell = static_cast<std::int32_t>(p[0]) * kGrid + static_cast<std::int32_t>(p[1]) +
                                  kGridCentre * kGrid + kGridCentre;
        p += 2;
        HeightCell(world, cell) = static_cast<std::uint8_t>(height);
        StateCell(world, cell) = kCellPending;
    }

    // Spread the slope limits outwards until nothing changes.
    bool failed = false;
    for (;;) {
        bool changed = false;
        for (std::int32_t j = 1; j < kGrid - 1; ++j) {
            for (std::int32_t i = 1; i < kGrid - 1; ++i) {
                if (StateCell(world, i, j) != kCellPending) {
                    continue;
                }
                StateCell(world, i, j) = kCellFixed;
                const std::int32_t h = HeightCell(world, i, j);
                PullNeighbour(world, i - 1, j - 1, h, 5, 7, changed, failed);
                PullNeighbour(world, i, j - 1, h, 5, 7, changed, failed);
                PullNeighbour(world, i + 1, j + 1, h, 7, 5, changed, failed);
                PullNeighbour(world, i, j + 1, h, 7, 5, changed, failed);
            }
        }
        for (std::int32_t j = 1; j < kGrid - 1; ++j) {
            for (std::int32_t i = 1; i < kGrid - 1; ++i) {
                if (StateCell(world, i, j) == kCellChanged) {
                    StateCell(world, i, j) = kCellPending;
                }
            }
        }
        if (failed) {
            return 0;
        }
        if (!changed) {
            break;
        }
    }

    std::uint32_t earth = 0;
    for (std::int32_t j = 1; j < kGrid - 1; ++j) {
        for (std::int32_t i = 1; i < kGrid - 1; ++i) {
            const std::int32_t planned = HeightCell(world, i, j);
            const std::int32_t current = TileAt(world, x - kGridCentre + i, y - kGridCentre + j).height;
            earth += static_cast<std::uint32_t>(planned > current ? planned - current : current - planned);
        }
    }
    return earth != 0 ? earth : 1;
}

// Purpose: Creates the record of a new construction site (a building with
// buildup 1) at (x, y) for a player, with its two empty material piles
// next to the door. Returns the building number, 0 if all are in use.
std::uint32_t __fastcall GameWorld_AddBuildingSite(GameWorld* world, void* /*edx*/, std::uint32_t x,
                                                   std::uint32_t y, std::uint32_t player, std::uint32_t type) {
    std::uint32_t building = 1;
    while (world->buildings[building].owner != 0xff && building != kBuildingLimit) {
        ++building;
    }
    if (building == kBuildingLimit) {
        return 0;
    }
    if (building > world->building_high_water) {
        world->building_high_water = building;
    }

    Building& b = world->buildings[building];
    layout::Player& playerRecord = world->players[player];
    const std::uint32_t race = static_cast<std::uint32_t>(playerRecord.race);
    const std::uint32_t* const layout = Layout(race, type);

    b.x = static_cast<std::uint16_t>(x);
    b.y = static_cast<std::uint16_t>(y);
    b.owner = static_cast<std::uint8_t>(player);
    b.race = static_cast<std::uint8_t>(race);  // the owner's race
    b.art = static_cast<std::uint8_t>(type);
    b.buildup = 1;  // a construction site
    // Words +0x0e..+0x14.
    b.pile_ring = 0;
    b.piles[0] = 0;
    b.piles[1] = 0;
    b.piles[2] = 0;
    b.unk16 = 0;
    b.unk17 = 0;
    b.request_flags = 1;
    b.unk19 = static_cast<std::uint8_t>(static_cast<std::int32_t>(layout[1]) >> 8);  // materials
    b.unk1a = static_cast<std::uint8_t>(layout[1]);
    b.unk1b[0] = 0;
    b.unk1b[1] = 0;
    b.unk1b[2] = 0;
    b.unk1b[3] = 0;
    b.state = static_cast<std::uint8_t>(b.unk19 + b.unk1a);
    b.extra_tiles = 0;
    // Words +0x24..+0x34.
    for (std::uint16_t& word : b.input_amount) {
        word = 0;
    }
    for (std::uint16_t& word : b.input_next) {
        word = 0;
    }
    b.work_x = 0;
    b.work_y = 0;
    b.trade = 0;
    b.unk36 = 0;

    // The player's countdown (int32 +0x84), used as uint32 like the original.
    std::uint32_t countdown = static_cast<std::uint32_t>(playerRecord.unk084);
    countdown -= 1;
    if (countdown <= 60000) {
        countdown = 0xffff;
    }
    playerRecord.unk084 = static_cast<std::int32_t>(countdown);
    b.work_y = static_cast<std::uint16_t>(countdown);
    b.unk08 = 0;

    Tile& tile = TileAt(world, x, y);
    tile.object_number = static_cast<std::uint16_t>(building);
    tile.flags |= kFlagBuilding;

    // The door, then the tiles listed after it (the door included).
    const std::uint32_t* p = After(Layout(b.race, type), kMarkDoor);
    const std::uint16_t doorX = static_cast<std::uint16_t>(p[0] + x);
    const std::uint16_t doorY = static_cast<std::uint16_t>(p[1] + y);
    b.door_x = doorX;
    b.door_y = doorY;
    const bool clearsObjects = type == 1 || type == 5 || type == 6 || type == 0xf || type == 0x10 || type == 0x12 ||
                               type == 0x13 || type == 0x1a || type == 0x1b || type == 0x1c || type == 0x1f ||
                               type == 0x20 || type == 0x21 || (type >= 0x29 && type <= 0x32) || type == 0x36 ||
                               type == 0x37;
    for (; *p != kMarkFootprint; p += 2) {
        if (clearsObjects) {
            TileAt(world, x + p[0], y + p[1]).object = 0xff;
        }
    }

    // Empty construction piles left and right of the door.
    b.piles[0] = static_cast<std::uint16_t>(GameWorld_CreatePile(world, nullptr, doorX - 1u, doorY, 1, 0, 0xfd));
    b.piles[1] = static_cast<std::uint16_t>(GameWorld_CreatePile(world, nullptr, doorX + 1u, doorY, 2, 0, 0xfd));

    // Work centre: words +0x0e (x) and +0x14 (y) of a construction site.
    if (type == 1) {
        b.pile_ring = 0xff;
        return building;
    }
    if (type == 2 || type == 3 || type == 5 || type == 0x19 || type == 0x1e || type == 0x33) {
        b.pile_ring = static_cast<std::uint16_t>(x);
        b.piles[2] = static_cast<std::uint16_t>(y);
    }
    std::int32_t dx = 0;
    std::int32_t dy = 0;
    if (type == 0xe) {
        dx = 4;
        dy = 8;
    } else if (type == 0x18) {
        dx = 5;
        dy = 10;
    } else if (type == 0x24) {
        dx = 6;
        dy = 12;
    }
    if (dx != 0) {
        const auto mapSize = static_cast<std::int32_t>(world->map_size);
        std::int32_t cx = static_cast<std::uint16_t>(x) + dx;
        std::int32_t cy = static_cast<std::uint16_t>(y) + dy;
        if (cx >= mapSize) {
            cx = mapSize - 1;
        }
        if (cy >= mapSize) {
            cy = mapSize - 1;
        }
        b.pile_ring = static_cast<std::uint16_t>(cx);
        b.piles[2] = static_cast<std::uint16_t>(cy);
    }
    return building;
}

namespace {

// Whether a footprint or border tile suits the building: owned by `owner`,
// free and in the building's area, on the right ground for the group.
bool TileSuitsSite(const Tile& tile, std::uint32_t owner, std::uint32_t area, SiteGroup group, bool checkGround,
                   bool mineCentre) {
    if (tile.owner != owner) {
        return false;
    }
    const std::uint8_t ground = tile.terrain & 0xf0;
    switch (group) {
        case SiteGroup::kPlain:
            if (ground != 0x10) {
                return false;
            }
            break;
        case SiteGroup::kRace1:
            if (ground != 0x10 && ground != 0x40) {
                return false;
            }
            break;
        case SiteGroup::kRace2:
            if (ground != 0x10 && ground != 0x90) {
                return false;
            }
            break;
        case SiteGroup::kMine:
            // Only the building's own tile must be mountain.
            if (checkGround && mineCentre && ground != 0x20) {
                return false;
            }
            break;
    }
    return (tile.flags & (kFlagBlocked | kFlagLevelling | kFlagFootprint)) == 0 && tile.area == area &&
           tile.object_number == 0;
}

std::uint32_t EarthClass(std::uint32_t earth) {
    static constexpr std::uint32_t kLimits[] = {30, 70, 120, 180, 250, 330, 420, 510, 600};
    if (earth == 0) {
        return 10;
    }
    for (std::uint32_t k = 0; k < 9; ++k) {
        if (earth < kLimits[k]) {
            return k + 1;
        }
    }
    return 10;
}

struct Stake {
    std::uint32_t ring;
    std::int32_t dx;
    std::int32_t dy;
};

}  // namespace

// Purpose: Checks whether a player may put a building of a type at (x, y)
// and how much levelling it needs (the size of the placement-preview
// marks); with mode 1 it also places the construction site. Used by the
// placement preview, the build command and the computer players.
std::uint32_t __fastcall GameWorld_CheckBuildingSite(GameWorld* world, void* /*edx*/, std::uint32_t x,
                                                     std::uint32_t y, std::uint32_t type, std::uint32_t player,
                                                     std::uint32_t mode) {
    std::uint32_t owner = player;
    if (mode >= 2) {
        mode -= 2;
        owner = 0xff;  // unowned land
    }
    const std::uint32_t race = PlayerRace(world, player);
    SiteGroup group = SiteGroup::kPlain;
    if (race == 1) {
        group = SiteGroup::kRace1;
    } else if (race == 2) {
        group = SiteGroup::kRace2;
    }
    if (IsMine(type)) {
        group = SiteGroup::kMine;
    }

    const std::uint32_t mapSize = world->map_size;
    if (x < 15 || x > mapSize - 16 || y < 15 || y > mapSize - 16) {
        return 0;
    }
    const std::uint32_t area = TileAt(world, x, y).area;

    bool unsuitable = false;
    std::uint32_t heightSum = 0;
    std::uint32_t tileCount = 0;
    const std::uint32_t* p = After(Layout(race, type), kMarkFootprint);
    while (*p != kMarkBorder) {
        if (*p == kMarkNoStakes) {
            ++p;
        }
        const std::int32_t dx = static_cast<std::int32_t>(p[0]);
        const std::int32_t dy = static_cast<std::int32_t>(p[1]);
        p += 2;
        const Tile& tile = TileAt(world, x + dx, y + dy);
        if (!TileSuitsSite(tile, owner, area, group, true, dx == 0 && dy == 0)) {
            unsuitable = true;
        } else if (group != SiteGroup::kMine) {
            heightSum += tile.height;
            ++tileCount;
        }
    }
    for (++p; *p != kMarkEnd; p += 2) {
        const std::int32_t dx = static_cast<std::int32_t>(p[0]);
        const std::int32_t dy = static_cast<std::int32_t>(p[1]);
        const std::uint32_t tx = x + dx;
        const std::uint32_t ty = y + dy;
        if (GameWorld_IsOnMap(world, nullptr, tx, ty) == 0) {
            GameDebugLog(kIllegalOffsetMessage, x, y, dx, dy);
        }
        if (!TileSuitsSite(TileAt(world, tx, ty), owner, area, group, false, false)) {
            unsuitable = true;
        }
    }
    if (unsuitable) {
        return 0;
    }

    if (type == 0x20 || type == 0x21) {
        // Needs water (terrain 4) within distance 20.
        const MapOffset* entry = world->offsets;
        if (entry->distance > 20) {
            return 0;
        }
        bool water = false;
        do {
            const std::int32_t tx = static_cast<std::int32_t>(x) + entry->dx;
            const std::int32_t ty = static_cast<std::int32_t>(y) + entry->dy;
            const std::int32_t size = static_cast<std::int32_t>(mapSize);
            if (tx >= 0 && tx < size && ty >= 0 && ty < size &&
                TileAt(world, static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty)).terrain == 4) {
                water = true;
            }
            ++entry;
        } while (entry->distance <= 20);
        if (!water) {
            return 0;
        }
    }

    std::uint32_t earth = 1;  // mines are not levelled
    if (group != SiteGroup::kMine) {
        const std::uint32_t meanHeight = (tileCount / 2 + heightSum) / tileCount;
        earth = GameWorld_PlanSiteLevelling(world, nullptr, x, y, type, player, meanHeight);
        if (earth == 0) {
            return 0;
        }
    }
    if (mode != 1) {
        return EarthClass(earth);
    }

    // Place the construction site.
    std::uint32_t slot = 0;
    if (group != SiteGroup::kMine) {
        while (slot < kSiteSlots && world->site_cache[player][slot].valid != 0) {
            ++slot;
        }
        if (slot == kSiteSlots) {
            return 0;
        }
    }
    const std::uint32_t building = GameWorld_AddBuildingSite(world, nullptr, x, y, player, type);
    if (building == 0) {
        return 0;
    }
    GameWorld_QueuePendingEvent(world, nullptr, 2, building, 0);

    const std::uint32_t* const layout = Layout(PlayerRace(world, player), type);
    Building& b = world->buildings[building];
    b.unk1b[1] = static_cast<std::uint8_t>(layout[0]);  // +0x1c

    // Footprint and border tiles get the footprint flag; per direction
    // sector the outermost tile of the first part of the footprint gets a stake.
    Stake stakes[6];
    for (Stake& stake : stakes) {
        stake.ring = 0;
    }
    bool stakeCandidates = true;
    for (p = After(layout, kMarkFootprint); *p != kMarkEnd; p += 2) {
        if (*p == kMarkNoStakes) {
            ++p;
            stakeCandidates = false;
        }
        if (*p == kMarkBorder) {
            ++p;
        }
        const std::int32_t dx = static_cast<std::int32_t>(p[0]);
        const std::int32_t dy = static_cast<std::int32_t>(p[1]);
        TileAt(world, x + dx, y + dy).flags |= kFlagFootprint;
        if (!stakeCandidates || (dx == 0 && dy == 0)) {
            continue;
        }
        const MapOffset* entry = world->offsets;
        while (entry->dx != dx || entry->dy != dy) {
            ++entry;
        }
        const auto ring = static_cast<std::uint32_t>(entry->distance);
        const auto sector = static_cast<std::uint32_t>(entry->sector);
        if (ring >= stakes[sector].ring) {
            stakes[sector] = {ring, dx, dy};
        }
    }
    for (const Stake& stake : stakes) {
        if (stake.ring >= 1) {
            TileAt(world, x + stake.dx, y + stake.dy).object = 0x71;
        }
    }
    TileAt(world, x, y).object = 0x70;

    if (group == SiteGroup::kMine) {
        b.buildup = 2;
        return EarthClass(earth);
    }

    b.unk22 = static_cast<std::uint16_t>(slot);
    b.work_x = static_cast<std::uint16_t>(earth);  // a construction site: the earth to move
    const std::uint8_t work = static_cast<std::uint8_t>(static_cast<std::uint8_t>((earth + 16) >> 5) + 2);
    b.unk1b[0] = work > 8 ? 8 : work;  // +0x1b, probably the number of diggers

    // The levelling job: target heights where the ground changes.
    PlayerSiteCache& cache = world->site_cache[player][slot];
    cache.valid = 1;
    std::uint16_t moved = b.input_amount[2];  // word +0x28: the height differences
    for (std::int32_t j = 0; j < kGrid; ++j) {
        for (std::int32_t i = 0; i < kGrid; ++i) {
            Tile& tile = TileAt(world, x - kGridCentre + i, y - kGridCentre + j);
            const std::uint8_t target = HeightCell(world, i, j);
            std::uint8_t& cell = cache.grid[i * kGrid + j];
            if (tile.height == target) {
                cell = 0;
                continue;
            }
            tile.flags |= kFlagLevelling;
            cell = target;
            if (target != 0) {
                const std::int32_t difference = tile.height - target;
                moved = static_cast<std::uint16_t>(moved + (difference > 0 ? difference : -difference));
            }
        }
    }
    b.input_amount[2] = moved;
    return EarthClass(earth);
}


using CastleSiteFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t player, std::uint32_t x,
                                                std::uint32_t y);
using StartPositionFn = std::uint32_t(__fastcall*)(GameWorld* world, void* edx, std::uint32_t player,
                                                   std::uint32_t x, std::uint32_t y, std::uint32_t attempts);
std::uint32_t __fastcall GameWorld_IsFreeCastleSite(GameWorld* world, void* edx, std::uint32_t player,
                                                    std::uint32_t x, std::uint32_t y);
std::uint32_t __fastcall GameWorld_IsGoodStartPosition(GameWorld* world, void* edx, std::uint32_t player,
                                                       std::uint32_t x, std::uint32_t y, std::uint32_t attempts);

S3RE_HOOK(0x00508420, GameWorld_IsFreeCastleSite, CastleSiteFn, Original_GameWorld_IsFreeCastleSite,
          GameWorld_IsFreeCastleSite);
S3RE_HOOK(0x005081a0, GameWorld_IsGoodStartPosition, StartPositionFn, Original_GameWorld_IsGoodStartPosition,
          GameWorld_IsGoodStartPosition);

// Purpose: Whether a player's castle could stand at (x, y) on land nobody
// owns yet; used when the start positions are chosen (0x5074b0).
std::uint32_t __fastcall GameWorld_IsFreeCastleSite(GameWorld* world, void* /*edx*/, std::uint32_t player,
                                                    std::uint32_t x, std::uint32_t y) {
    return GameWorld_CheckBuildingSite(world, nullptr, x, y, kCastleType, player, kModeCheckUnowned) != 0 ? 1 : 0;
}

// Purpose: Whether (x, y) is a good random start position for a player
// (0x5074b0, "AddPlayerTowns"): a castle must fit on unowned land and the
// surroundings must offer enough free land, grass, trees, stone and
// resources; the demands drop as the number of attempts grows.
std::uint32_t __fastcall GameWorld_IsGoodStartPosition(GameWorld* world, void* /*edx*/, std::uint32_t player,
                                                       std::uint32_t x, std::uint32_t y, std::uint32_t attempts) {
    if (GameWorld_CheckBuildingSite(world, nullptr, x, y, kCastleType, player, kModeCheckUnowned) == 0) {
        return 0;
    }
    // Land within the first 3000 offsets (about 31 tiles), resources within 9995.
    std::uint32_t grass = 0;
    std::uint32_t trees = 0;
    std::uint32_t stone = 0;  // stone left: 0x7f - object for objects 0x73..0x7e
    std::uint32_t unowned = 0;
    std::uint32_t coal = 0;
    std::uint32_t iron = 0;
    std::uint32_t gold = 0;
    std::uint32_t sulfur = 0;
    std::uint32_t gems = 0;
    const std::uint32_t mapSize = world->map_size;
    for (std::uint32_t k = 0; k < 9995; ++k) {
        const std::int32_t dx = world->offsets[k].dx;
        const std::int32_t dy = world->offsets[k].dy;
        const std::uint32_t tx = x + dx;
        const std::uint32_t ty = y + dy;
        if (tx >= mapSize || ty >= mapSize) {
            continue;
        }
        const Tile& tile = TileAt(world, tx, ty);
        if (k < 3000) {
            if ((tile.terrain & 0xf0) == 0x10) {
                ++grass;
            }
            const std::uint8_t object = tile.object;
            if (object >= 0x44 && object <= 0x53) {
                ++trees;
            }
            if (object >= 0x73 && object <= 0x7e) {
                stone += 0x7f - object;
            }
            if (tile.owner == 0xff) {
                ++unowned;
            }
        }
        switch (tile.resources & 0xf0) {
            case 0x10: ++coal; break;
            case 0x20: ++iron; break;
            case 0x30: ++gold; break;
            case 0x50: ++sulfur; break;
            case 0x40: ++gems; break;
            default: break;
        }
    }
    if (attempts < 30000) {
        return unowned >= 2900 && grass >= 2000 && trees >= 20 && trees <= 30 && stone >= 50 && stone <= 100 &&
                       coal >= 40 && iron >= 20 && gold >= 10 && sulfur >= 5 && gems >= 5
                   ? 1
                   : 0;
    }
    if (attempts < 60000) {
        return unowned >= 2500 && grass >= 1500 && trees >= 12 && stone >= 30 && coal >= 30 && iron >= 15 && gold >= 5
                   ? 1
                   : 0;
    }
    if (attempts < 100000) {
        return unowned >= 1500 && grass >= 1000 && trees >= 5 && stone >= 15 && coal >= 20 && iron >= 10 ? 1 : 0;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// The test worlds look like a game in progress: smooth heights with some
// mountains (ground 0x20), water patches (terrain 4), patches of ground
// 0x30/0x40/0x90, four players owning the land around their start
// positions (one area per player, a second one beyond a line), sparse
// blocked / levelling / footprint flags and objects, the real-shaped offset
// table (rings and direction sectors), free building records and levelling
// slots. Mode-1 placements change the world, so later cases see earlier
// construction sites.

namespace {

constexpr std::size_t kTestWorld = offsetof(GameWorld, buildings) + (kBuildingLimit + 1) * sizeof(Building);
constexpr std::uint32_t kTestPlayers = 8;
constexpr std::uint32_t kOwningPlayers = 4;

struct SiteWorld {
    std::uint32_t size;
    std::uint32_t centreX[kOwningPlayers];
    std::uint32_t centreY[kOwningPlayers];
};

// Sets `field` (a field of the original world) to `value` in both worlds.
template <typename T, typename V>
void SetField(test::WorldPair& worlds, T& field, V value) {
    worlds.Set<T>(OffsetIn(worlds.original(), &field), static_cast<T>(value));
}

std::int32_t HexDistance(std::int32_t dx, std::int32_t dy) {
    const std::int32_t ax = dx < 0 ? -dx : dx;
    const std::int32_t ay = dy < 0 ? -dy : dy;
    if ((dx >= 0) == (dy >= 0)) {
        return ax > ay ? ax : ay;
    }
    return ax + ay;
}

// Offsets up to distance 58 (10,267 entries; the start position check
// reads 9995) ordered by ring (as built by 0x516530), each with
// a direction sector 0..5 from its angle on the hexagonal grid.
void SetUpOffsetTable(test::WorldPair& worlds) {
    GameWorld* o = worlds.original();
    std::uint32_t entry = 0;
    for (std::int32_t ring = 0; ring <= 58; ++ring) {
        for (std::int32_t dx = -ring; dx <= ring; ++dx) {
            for (std::int32_t dy = -ring; dy <= ring; ++dy) {
                if (HexDistance(dx, dy) != ring) {
                    continue;
                }
                const double px = dx - 0.5 * dy;
                const double py = 0.8660254 * dy;
                double angle = std::atan2(py, px) * 3.0 / 3.14159265358979;
                if (angle < 0) {
                    angle += 6.0;
                }
                const std::int32_t sector = ring == 0 ? 0 : static_cast<std::int32_t>(angle) % 6;
                MapOffset& e = o->offsets[entry];
                SetField(worlds, e.dx, dx);
                SetField(worlds, e.dy, dy);
                SetField(worlds, e.distance, ring);
                SetField(worlds, e.sector, sector);
                ++entry;
            }
        }
    }
}

SiteWorld SetUpSiteWorld(test::Context& ctx, test::WorldPair& worlds, std::uint32_t size, std::uint32_t noise) {
    SiteWorld site{};
    site.size = size;
    GameWorld* o = worlds.original();
    SetField(worlds, o->map_size, size);
    SetField(worlds, o->pending_event_index.read, ctx.rng().Below(1000));
    SetField(worlds, o->pending_event_index.write, ctx.rng().Below(1000));
    SetUpOffsetTable(worlds);
    for (std::uint32_t player = 0; player < 20; ++player) {
        layout::Player& record = o->players[player];
        SetField(worlds, record.race, ctx.rng().Below(4));
        SetField(worlds, record.unk084,
                 ctx.rng().Chance(1, 4) ? 60001 + ctx.rng().Below(3) : 60000 + ctx.rng().Below(6000));
        const bool full = ctx.rng().Chance(1, 10);
        for (std::uint32_t slot = 0; slot < kSiteSlots; ++slot) {
            SetField(worlds, o->site_cache[player][slot].valid, full || ctx.rng().Chance(1, 4) ? 1 : 0);
        }
    }
    // A third of the building records free (owner 0xff).
    for (std::uint32_t b = 1; b <= kBuildingLimit; ++b) {
        if (ctx.rng().Chance(1, 3)) {
            SetField(worlds, o->buildings[b].owner, 0xff);
        }
    }
    for (std::uint32_t player = 0; player < kOwningPlayers; ++player) {
        site.centreX[player] = size / 4 + (player & 1) * (size / 2);
        site.centreY[player] = size / 4 + (player >> 1) * (size / 2);
    }

    // Heights: a coarse random grid, interpolated, with a little noise.
    constexpr std::uint32_t kCell = 8;
    const std::uint32_t cells = size / kCell + 2;
    std::vector<std::int32_t> coarse(cells * cells);
    const std::int32_t hills = ctx.rng().Chance(1, 3) ? 25 : 5;  // hilly or gentle land
    for (std::int32_t& h : coarse) {
        h = ctx.rng().Chance(1, 12) ? 110 + ctx.rng().Range(0, 90) : 20 + ctx.rng().Range(0, hills);
    }
    // Some blobs of water and of other ground.
    struct Blob {
        std::int32_t x, y, radius;
        std::uint8_t terrain;
    };
    static constexpr std::uint8_t kGrounds[] = {4, 4, 7, 0x30, 0x40, 0x41, 0x90, 0x92, 0x60, 0x15};
    std::vector<Blob> blobs;
    for (int k = 0; k < 24; ++k) {
        blobs.push_back({static_cast<std::int32_t>(ctx.rng().Below(size)), static_cast<std::int32_t>(ctx.rng().Below(size)),
                         static_cast<std::int32_t>(2 + ctx.rng().Below(9)), kGrounds[ctx.rng().Below(sizeof(kGrounds))]});
    }
    for (std::uint32_t x = 0; x < size; ++x) {
        for (std::uint32_t y = 0; y < size; ++y) {
            const std::uint32_t cx = x / kCell;
            const std::uint32_t cy = y / kCell;
            const std::int32_t fx = static_cast<std::int32_t>(x % kCell);
            const std::int32_t fy = static_cast<std::int32_t>(y % kCell);
            const std::int32_t a = coarse[cx * cells + cy];
            const std::int32_t b = coarse[(cx + 1) * cells + cy];
            const std::int32_t c = coarse[cx * cells + cy + 1];
            const std::int32_t d = coarse[(cx + 1) * cells + cy + 1];
            std::int32_t height = (a * (8 - fx) * (8 - fy) + b * fx * (8 - fy) + c * (8 - fx) * fy + d * fx * fy) / 64;
            if (ctx.rng().Chance(1, 4)) {
                height += ctx.rng().Range(-1, 1);
            }
            std::uint8_t terrain = static_cast<std::uint8_t>(
                height >= 90 ? 0x20 + ctx.rng().Below(4) : 0x10 + (ctx.rng().Chance(1, 8) ? ctx.rng().Below(4) : 0));
            for (const Blob& blob : blobs) {
                if (HexDistance(static_cast<std::int32_t>(x) - blob.x, static_cast<std::int32_t>(y) - blob.y) <=
                    blob.radius) {
                    terrain = blob.terrain;
                }
            }
            std::uint8_t owner = 0xff;
            std::uint16_t area = 0;
            for (std::uint32_t player = 0; player < kOwningPlayers; ++player) {
                const std::int32_t distance =
                    HexDistance(static_cast<std::int32_t>(x) - static_cast<std::int32_t>(site.centreX[player]),
                                static_cast<std::int32_t>(y) - static_cast<std::int32_t>(site.centreY[player]));
                if (distance <= static_cast<std::int32_t>(size / 5)) {
                    owner = static_cast<std::uint8_t>(player);
                    // A second area for the player beyond a line.
                    area = static_cast<std::uint16_t>(1 + player + (x > site.centreX[player] + size / 8 ? 10 : 0));
                }
            }
            std::uint8_t flags = 0;
            std::uint16_t object = 0;
            if (noise != 0) {
                if (ctx.rng().Chance(noise, 2000)) flags |= kFlagBlocked;
                if (ctx.rng().Chance(noise, 4000)) flags |= kFlagLevelling;
                if (ctx.rng().Chance(noise, 4000)) flags |= kFlagFootprint;
                if (ctx.rng().Chance(noise, 2000)) object = static_cast<std::uint16_t>(1 + ctx.rng().Below(500));
            }
            if (ctx.rng().Chance(1, 50)) flags |= 0x02;
            if (ctx.rng().Chance(1, 50)) flags |= 0x40;
            Tile& tile = o->tiles[x][y];
            SetField(worlds, tile.height, height < 0 ? 0 : height);
            SetField(worlds, tile.terrain, terrain);
            SetField(worlds, tile.owner, owner);
            SetField(worlds, tile.flags, flags);
            SetField(worlds, tile.object_number, object);
            SetField(worlds, tile.area, area);
        }
    }
    return site;
}

std::uint32_t RandomBuildingType(test::Context& ctx) {
    static constexpr std::uint32_t kInteresting[] = {7, 8, 9, 0x1d, 0x25, 0x38, 0x20, 0x21, 1, 2, 3, 5, 0xe, 0xf,
                                                     0x18, 0x19, 0x1e, 0x24, 0x33};
    if (ctx.rng().Chance(1, 2)) {
        return kInteresting[ctx.rng().Below(sizeof(kInteresting) / sizeof(kInteresting[0]))];
    }
    return 1 + ctx.rng().Below(56);
}

// A position for a site: mostly inside a player's land (the player is its
// owner), sometimes anywhere (also just off the map) and any player.
void RandomSitePosition(test::Context& ctx, const SiteWorld& site, std::uint32_t& x, std::uint32_t& y,
                        std::uint32_t& player) {
    player = ctx.rng().Below(kOwningPlayers);
    if (ctx.rng().Chance(1, 10)) {
        x = ctx.rng().Below(site.size + 4);
        y = ctx.rng().Below(site.size + 4);
        player = ctx.rng().Below(kTestPlayers);
        return;
    }
    // Inside the player's land (hexagonal), sometimes reaching over its edge.
    const std::int32_t reach = static_cast<std::int32_t>(site.size / 5) + (ctx.rng().Chance(1, 4) ? 2 : -4);
    std::int32_t dx;
    std::int32_t dy;
    do {
        dx = ctx.rng().Range(-reach, reach);
        dy = ctx.rng().Range(-reach, reach);
    } while (HexDistance(dx, dy) > reach);
    x = static_cast<std::uint32_t>(static_cast<std::int32_t>(site.centreX[player]) + dx);
    y = static_cast<std::uint32_t>(static_cast<std::int32_t>(site.centreY[player]) + dy);
}

bool SameDebugLogs(test::Context& ctx, const std::vector<DebugLogCall>& expected,
                   const std::vector<DebugLogCall>& actual) {
    if (expected.size() != actual.size()) {
        ctx.Fail(__FILE__, __LINE__, "  DebugLog calls: original %u, ours %u", static_cast<unsigned>(expected.size()),
                 static_cast<unsigned>(actual.size()));
        return false;
    }
    for (std::size_t k = 0; k < expected.size(); ++k) {
        if (expected[k] != actual[k]) {
            ctx.Fail(__FILE__, __LINE__, "  DebugLog call %u differs", static_cast<unsigned>(k));
            return false;
        }
    }
    return true;
}

// Runs CheckBuildingSite on evolving worlds of several kinds (clean or
// noisy land; every fifth a tiny map size, so that the size check lets
// border tiles lie off the map and the "illegal building offset" message
// appears). outcomes[result] counts checks, outcomes[12 + result] placements.
void CompareCheckBuildingSite(test::Context& ctx, CheckBuildingSiteFn candidate, int worldsToRun, int cases,
                              std::uint32_t* outcomes) {
    for (int w = 0; w < worldsToRun; ++w) {
        test::WorldPair worlds(ctx, kTestWorld);
        const bool tiny = w % 5 == 4;
        static constexpr std::uint32_t kSizes[] = {128, 256, 384, 192};
        static constexpr std::uint32_t kNoise[] = {0, 1, 0, 10};
        const SiteWorld site = SetUpSiteWorld(ctx, worlds, kSizes[w % 4], kNoise[(w / 2) % 4]);
        if (tiny) {
            SetField(worlds, worlds.original()->map_size, 8 + ctx.rng().Below(8));
        }
        DebugLogRecorder recorder;
        for (int i = 0; i < cases; ++i) {
            std::uint32_t x, y, player;
            RandomSitePosition(ctx, site, x, y, player);
            const std::uint32_t type = RandomBuildingType(ctx);
            std::uint32_t mode = ctx.rng().Chance(1, 2) ? 1 : 0;
            if (ctx.rng().Chance(1, 12)) {
                mode = 2 + ctx.rng().Below(2);
            }
            if (tiny) {
                mode &= 2;  // no placing: the sites would reach tiles off the test map
                x = 15 + ctx.rng().Below(40);
                y = 15 + ctx.rng().Below(40);
            }
            if (ctx.rng().Chance(1, 400)) {
                // Every building record in use.
                for (std::uint32_t b = 1; b <= kBuildingLimit; ++b) {
                    SetField(worlds, worlds.original()->buildings[b].owner, 0);
                }
            }
            const std::uint32_t expected =
                Original_GameWorld_CheckBuildingSite(worlds.original(), nullptr, x, y, type, player, mode);
            const auto expectedLogs = recorder.Take();
            const std::uint32_t actual = candidate(worlds.ours(), nullptr, x, y, type, player, mode);
            const auto actualLogs = recorder.Take();
            if (outcomes != nullptr && expected <= 10) {
                ++outcomes[expected + ((mode & 1) != 0 ? 12 : 0)];
            }
            if (!S3RE_EXPECT_EQ(expected, actual) || !SameDebugLogs(ctx, expectedLogs, actualLogs) ||
                ((i % 32 == 31 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
                ctx.Fail(__FILE__, __LINE__,
                         "  world %d case %d (or one of the 31 before it): (%u, %u) type %u player %u mode %u", w, i,
                         x, y, type, player, mode);
                return;
            }
        }
    }
}

// outcomes: [0] failed plans, [1] flat (1), [2] levelling needed.
void ComparePlanSiteLevelling(test::Context& ctx, PlanSiteLevellingFn candidate, int worldsToRun, int cases,
                              std::uint32_t* outcomes) {
    for (int w = 0; w < worldsToRun; ++w) {
        test::WorldPair worlds(ctx, kTestWorld);
        const SiteWorld site = SetUpSiteWorld(ctx, worlds, w % 2 ? 256 : 160, w % 3 == 2 ? 4 : 0);
        for (int i = 0; i < cases; ++i) {
            std::uint32_t x, y, player;
            RandomSitePosition(ctx, site, x, y, player);
            if (x < 15) x = 15;
            if (y < 15) y = 15;
            const std::uint32_t type = RandomBuildingType(ctx);
            GameWorld* o = worlds.original();
            const std::int32_t here = TileAt(o, x, y).height;
            const std::uint32_t height = ctx.rng().Chance(1, 10)
                                             ? ctx.rng().Below(256)
                                             : static_cast<std::uint32_t>(here + ctx.rng().Range(-3, 3)) & 0xff;
            if (ctx.rng().Chance(1, 4)) {
                // A flat footprint: the plan may need no levelling at all.
                const std::uint32_t* p = After(Layout(PlayerRace(o, player) & 3, type), kMarkFootprint);
                for (; *p != kMarkBorder; p += 2) {
                    if (*p == kMarkNoStakes) {
                        ++p;
                    }
                    SetField(worlds, TileAt(o, x + p[0], y + p[1]).height, height);
                }
            }
            const std::uint32_t expected =
                Original_GameWorld_PlanSiteLevelling(worlds.original(), nullptr, x, y, type, player, height);
            const std::uint32_t actual = candidate(worlds.ours(), nullptr, x, y, type, player, height);
            if (outcomes != nullptr) {
                ++outcomes[expected == 0 ? 0 : expected == 1 ? 1 : 2];
            }
            if (!S3RE_EXPECT_EQ(expected, actual) ||
                !worlds.SameField(ctx, [](GameWorld* w) -> auto& { return w->footprint_scratch; }) ||
                ((i % 64 == 63 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
                ctx.Fail(__FILE__, __LINE__, "  world %d case %d: (%u, %u) type %u player %u height %u", w, i, x, y,
                         type, player, height);
                return;
            }
        }
    }
}

void CompareAddBuildingSite(test::Context& ctx, AddBuildingSiteFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    const SiteWorld site = SetUpSiteWorld(ctx, worlds, 384, 1);
    for (int i = 0; i < cases; ++i) {
        std::uint32_t x, y, player;
        RandomSitePosition(ctx, site, x, y, player);
        if (x < 15) x = 15;
        if (y < 15) y = 15;
        if (ctx.rng().Chance(1, 4)) {
            player = ctx.rng().Below(20);
        }
        const std::uint32_t type = ctx.rng().Chance(1, 3) ? RandomBuildingType(ctx) : 1 + ctx.rng().Below(56);
        GameWorld* o = worlds.original();
        if (ctx.rng().Chance(1, 8)) {
            SetField(worlds, o->building_high_water, ctx.rng().Below(kBuildingLimit + 10));
        }
        if (i == cases / 2) {
            // Every record in use (the "none free" path); then a few free again.
            for (std::uint32_t b = 1; b <= kBuildingLimit; ++b) {
                SetField(worlds, o->buildings[b].owner, 1);
            }
        } else if (i == cases / 2 + 1) {
            for (std::uint32_t b = 1; b <= kBuildingLimit; ++b) {
                SetField(worlds, o->buildings[b].owner, ctx.rng().Chance(1, 50) ? 0xff : 1);
            }
        }
        const std::uint32_t expected =
            Original_GameWorld_AddBuildingSite(worlds.original(), nullptr, x, y, player, type);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, x, y, player, type);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 32 == 31 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the 31 before it): (%u, %u) player %u type %u", i, x, y,
                     player, type);
            return;
        }
    }
}

// Wrong: odd target heights are rounded up by one.
std::uint32_t __fastcall WrongPlanSiteLevelling(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                std::uint32_t type, std::uint32_t player, std::uint32_t height) {
    return GameWorld_PlanSiteLevelling(world, edx, x, y, type, player, height + (height & 1));
}

// Wrong: the player's countdown +0x84 is not counted down.
std::uint32_t __fastcall WrongAddBuildingSite(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y,
                                              std::uint32_t player, std::uint32_t type) {
    std::int32_t& field = world->players[player].unk084;
    const std::int32_t saved = field;
    const std::uint32_t result = GameWorld_AddBuildingSite(world, edx, x, y, player, type);
    field = saved;
    return result;
}

// Wrong: a placed site's own tile does not get the footprint flag.
std::uint32_t __fastcall WrongCheckBuildingSite(GameWorld* world, void* edx, std::uint32_t x, std::uint32_t y,
                                                std::uint32_t type, std::uint32_t player, std::uint32_t mode) {
    const std::uint8_t saved = TileAt(world, x, y).flags;
    const std::uint32_t result = GameWorld_CheckBuildingSite(world, edx, x, y, type, player, mode);
    if ((mode & 1) != 0 && result != 0) {
        std::uint8_t& flags = TileAt(world, x, y).flags;
        flags = static_cast<std::uint8_t>((flags & ~kFlagFootprint) | (saved & kFlagFootprint));
    }
    return result;
}

}  // namespace

S3RE_TEST(GameWorld_PlanSiteLevelling_MatchesOriginal) {
    std::uint32_t outcomes[3] = {};
    ComparePlanSiteLevelling(ctx, &GameWorld_PlanSiteLevelling, 6, 1500, outcomes);
    log::Write("%s: " "plans: %u failed, %u flat, %u levelled", ctx.name(), outcomes[0], outcomes[1], outcomes[2]);
    S3RE_EXPECT(outcomes[0] > 100 && outcomes[1] > 20 && outcomes[2] > 500);
}
S3RE_TEST(GameWorld_PlanSiteLevelling_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    ComparePlanSiteLevelling(probe, &WrongPlanSiteLevelling, 1, 500, nullptr);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_AddBuildingSite_MatchesOriginal) {
    CompareAddBuildingSite(ctx, &GameWorld_AddBuildingSite, 3000);
}
S3RE_TEST(GameWorld_AddBuildingSite_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareAddBuildingSite(probe, &WrongAddBuildingSite, 200);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_CheckBuildingSite_MatchesOriginal) {
    std::uint32_t outcomes[24] = {};
    CompareCheckBuildingSite(ctx, &GameWorld_CheckBuildingSite, 10, 1500, outcomes);
    // Placed sites and several size classes must occur.
    std::uint32_t placed = 0;
    std::uint32_t classes = 0;
    for (std::uint32_t k = 1; k <= 10; ++k) {
        placed += outcomes[12 + k];
        classes += (outcomes[k] != 0 || outcomes[12 + k] != 0) ? 1 : 0;
    }
    log::Write("%s: " "checks: %u impossible, %u placed, %u size classes seen", ctx.name(), outcomes[0] + outcomes[12], placed, classes);
    S3RE_EXPECT(placed > 100);
    S3RE_EXPECT(classes >= 6);
}
S3RE_TEST(GameWorld_CheckBuildingSite_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareCheckBuildingSite(probe, &WrongCheckBuildingSite, 1, 1500, nullptr);
    S3RE_EXPECT(probe.failures() > 0);
}

namespace {

void CompareIsFreeCastleSite(test::Context& ctx, CastleSiteFn candidate, int cases) {
    test::WorldPair worlds(ctx, kTestWorld);
    const SiteWorld site = SetUpSiteWorld(ctx, worlds, 256, 0);
    for (int i = 0; i < cases; ++i) {
        std::uint32_t x, y, player;
        RandomSitePosition(ctx, site, x, y, player);
        if (ctx.rng().Chance(1, 2)) {
            // Unowned land between the players.
            x = ctx.rng().Below(site.size);
            y = ctx.rng().Below(site.size);
        }
        const std::uint32_t expected = Original_GameWorld_IsFreeCastleSite(worlds.original(), nullptr, player, x, y);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, player, x, y);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 64 == 63 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: player %u (%u, %u)", i, player, x, y);
            return;
        }
    }
}

// Fills the tiles of the first 9995 offset-table entries around (x, y)
// with unowned, even land whose grass, trees, stone, ownership and
// resources have densities drawn around the thresholds of
// IsGoodStartPosition.
void SetUpStartRegion(test::Context& ctx, test::WorldPair& worlds, std::uint32_t x, std::uint32_t y) {
    auto chance = [&](double p) { return ctx.rng().Below(1000000) < static_cast<std::uint32_t>(p * 1000000); };
    auto uniform = [&](double low, double high) { return low + (high - low) * ctx.rng().Below(10001) / 10000.0; };
    // Half of the regions aim at the strictest demands, the others vary.
    const bool ideal = ctx.rng().Chance(1, 2);
    const double grass = ideal ? 0.9 : uniform(0.4, 1.0);
    const double trees = ideal ? 0.0083 : uniform(0.002, 0.012);
    const double stones = ideal ? 0.0038 : uniform(0.0005, 0.006);
    static constexpr double kUnowned[] = {1.0, 1.0, 0.985, 0.9, 0.7};
    const double unowned = ideal ? 1.0 : kUnowned[ctx.rng().Below(5)];
    const double coal = ideal ? 0.008 : uniform(0, 0.008);
    const double iron = ideal ? 0.004 : uniform(0, 0.004);
    const double gold = ideal ? 0.002 : uniform(0, 0.002);
    const double sulfur = ideal ? 0.0012 : uniform(0, 0.001);
    const double gems = ideal ? 0.0012 : uniform(0, 0.001);
    const std::uint8_t base = static_cast<std::uint8_t>(20 + ctx.rng().Below(40));
    GameWorld* o = worlds.original();
    for (std::uint32_t k = 0; k < 9995; ++k) {
        const std::int32_t dx = o->offsets[k].dx;
        const std::int32_t dy = o->offsets[k].dy;
        const std::uint32_t tx = x + dx;
        const std::uint32_t ty = y + dy;
        if (tx >= 0x300 || ty >= 0x300) {
            continue;
        }
        Tile& tile = o->tiles[tx][ty];
        // The castle's site (offsets up to distance 8) is mostly clean.
        const bool site = k < 217 && !chance(0.002);
        std::uint8_t object = 0;
        if (site) {
        } else if (chance(trees)) {
            object = static_cast<std::uint8_t>(0x44 + ctx.rng().Below(16));
        } else if (chance(stones)) {
            object = static_cast<std::uint8_t>(0x73 + ctx.rng().Below(12));
        } else if (chance(0.01)) {
            object = static_cast<std::uint8_t>(ctx.rng().Below(256));
        }
        std::uint8_t resources = static_cast<std::uint8_t>(ctx.rng().Below(16));
        if (chance(coal)) {
            resources |= 0x10;
        } else if (chance(iron)) {
            resources |= 0x20;
        } else if (chance(gold)) {
            resources |= 0x30;
        } else if (chance(gems)) {
            resources |= 0x40;
        } else if (chance(sulfur)) {
            resources |= 0x50;
        } else if (chance(0.01)) {
            resources |= 0x60;
        }
        static constexpr std::uint8_t kOtherGround[] = {0x20, 0x30, 0x40, 0x04, 0x60};
        SetField(worlds, tile.height, base + ctx.rng().Below(2));
        SetField(worlds, tile.terrain,
                 site || chance(grass) ? static_cast<std::uint8_t>(0x10 + ctx.rng().Below(4))
                                       : kOtherGround[ctx.rng().Below(5)]);
        SetField(worlds, tile.object, object);
        SetField(worlds, tile.owner, site || chance(unowned) ? 0xff : static_cast<std::uint8_t>(ctx.rng().Below(4)));
        SetField(worlds, tile.flags, 0);
        SetField(worlds, tile.object_number, 0);
        SetField(worlds, tile.area, 0);
        SetField(worlds, tile.resources, resources);
    }
}

// outcomes[tier][result]: tier 0..3 by the attempts thresholds.
void CompareIsGoodStartPosition(test::Context& ctx, StartPositionFn candidate, int cases, std::uint32_t (*outcomes)[2]) {
    test::WorldPair worlds(ctx, kTestWorld);
    const SiteWorld site = SetUpSiteWorld(ctx, worlds, 384, 0);
    for (int i = 0; i < cases; ++i) {
        std::uint32_t x = 15 + ctx.rng().Below(site.size - 30);
        std::uint32_t y = 15 + ctx.rng().Below(site.size - 30);
        if (ctx.rng().Chance(1, 20)) {
            x = ctx.rng().Below(site.size + 10);
        }
        const std::uint32_t player = ctx.rng().Below(kTestPlayers);
        if (ctx.rng().Chance(9, 10)) {
            SetUpStartRegion(ctx, worlds, x, y);
        }
        static constexpr std::uint32_t kAttempts[] = {0, 29999, 30000, 59999, 60000, 99999, 100000, 250000};
        const std::uint32_t tier = ctx.rng().Below(4);
        const std::uint32_t attempts = ctx.rng().Chance(1, 4) ? kAttempts[tier * 2 + ctx.rng().Below(2)]
                                                              : kAttempts[tier * 2] + ctx.rng().Below(100);
        const std::uint32_t expected =
            Original_GameWorld_IsGoodStartPosition(worlds.original(), nullptr, player, x, y, attempts);
        const std::uint32_t actual = candidate(worlds.ours(), nullptr, player, x, y, attempts);
        if (outcomes != nullptr && expected <= 1) {
            ++outcomes[tier][expected];
        }
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % 16 == 15 || i == cases - 1) && !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: player %u (%u, %u) attempts %u", i, player, x, y, attempts);
            return;
        }
    }
}

// Wrong: a castle on the player's own land counts as free.
std::uint32_t __fastcall WrongIsFreeCastleSite(GameWorld* world, void* /*edx*/, std::uint32_t player, std::uint32_t x,
                                               std::uint32_t y) {
    return GameWorld_CheckBuildingSite(world, nullptr, x, y, kCastleType, player, 0) != 0 ? 1 : 0;
}

// Wrong: in the third tier (60000..99999 attempts) every third refused
// position that the last tier would take is accepted.
std::uint32_t __fastcall WrongIsGoodStartPosition(GameWorld* world, void* edx, std::uint32_t player, std::uint32_t x,
                                                  std::uint32_t y, std::uint32_t attempts) {
    const std::uint32_t result = GameWorld_IsGoodStartPosition(world, edx, player, x, y, attempts);
    if (result == 0 && attempts >= 60000 && attempts < 100000 &&
        GameWorld_IsGoodStartPosition(world, edx, player, x, y, 100000) == 1) {
        return attempts % 3 == 0 ? 1 : 0;
    }
    return result;
}

}  // namespace

S3RE_TEST(GameWorld_IsFreeCastleSite_MatchesOriginal) {
    CompareIsFreeCastleSite(ctx, &GameWorld_IsFreeCastleSite, 2000);
}
S3RE_TEST(GameWorld_IsFreeCastleSite_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareIsFreeCastleSite(probe, &WrongIsFreeCastleSite, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameWorld_IsGoodStartPosition_MatchesOriginal) {
    std::uint32_t outcomes[4][2] = {};
    CompareIsGoodStartPosition(ctx, &GameWorld_IsGoodStartPosition, 600, outcomes);
    log::Write("%s: accepted/refused per tier: %u/%u %u/%u %u/%u %u/%u", ctx.name(), outcomes[0][1], outcomes[0][0],
               outcomes[1][1], outcomes[1][0], outcomes[2][1], outcomes[2][0], outcomes[3][1], outcomes[3][0]);
    // Each of the first three tiers must both accept and refuse.
    for (int tier = 0; tier < 3; ++tier) {
        S3RE_EXPECT(outcomes[tier][0] > 5 && outcomes[tier][1] > 5);
    }
}
S3RE_TEST(GameWorld_IsGoodStartPosition_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareIsGoodStartPosition(probe, &WrongIsGoodStartPosition, 300, nullptr);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
