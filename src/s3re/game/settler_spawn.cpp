// Creating settlers and placing them (UpdateSettlers callees, package B).
//
// CGameData::SpawnSettlersAround (0x0050cb20): (count, type, player, x, y):
//   walks the offset table from entry 0 (nearest first) until `count`
//   settlers are made: a tile on the map (signed compares with the map
//   size) without a settler, with terrain >= 0x10 (land), none of the tile
//   flags 0x0f and owned by `player` (byte compared with the full dword)
//   gets CreateSettler(tx, ty, type, player). For each one whose type
//   class (S3.EXE 0x658f64) is 0 or 10..29 the player's counter +0xcc
//   (Player::population_limit_base) grows by one. No limit on the walk (callers
//   make sure there is room). EAX: callers don't use it. ret 0x14.
//   Callers: UpdateSettlers, 0x506cf0.
// CGameData::EnterBuildingSlot (0x0050ccb0): (settler): the counterpart of
//   LeaveBuildingSlot 0x50cc10. Unless the settler is in a slot already
//   (flag 0x40 of +0x14), it takes the first tile without a settler in the
//   building's row (x + 1, x + 2, ... at the building's y): slot byte +0x15
//   = (tile x - building x - 1) + (building byte +0x7 << 5), plus 8 for
//   building type 0x10 or 0x10 for type 0x12 (type 0x0f: nothing added),
//   flag 0x40 set, position = that tile, dword +0x8 = 2, and the tile's
//   settler word = the settler. EAX: callers don't use it. ret 4.
//   Callers: UpdateSettlers, the map loader 0x4fd540.
// CGameData::CreateSevenTileUnit (0x0050dd20): (type, player, x, y): a
//   settler of `type` at (x, y) with slot byte +0x15 = 6, and six settlers
//   of type 0x2a (parts) at (x+1, y), (x+1, y+1), (x, y+1), (x-1, y),
//   (x-1, y-1), (x, y-1), each with job 0x130; word +0x20 of the main
//   settler and of each part but the last holds the next part (a chain).
//   Probably big units that cover seven tiles (ships? catapults?);
//   "Katapultproduktion"/"Gongproduktion" jobs and the map loader call it.
//   EAX: callers don't use it. ret 0x10.
// CGameData::SetSoldierAnimationInBuilding (0x0050e140): (settler):
//   SetSettlerAnimation(settler, kind) with kind 0x125, or 0x126..0x12d
//   for settler types 0x20, 0x23, 8, 0x1e, 0x21, 0x0f, 0x1f, 0x22; +9 in a
//   building of type 0x10, +0x12 in type 0x12; +0x1b for building byte +0x7
//   == 1, +0x36 for 2 (27 kinds per race, so +0x7 is probably the race).
//   EAX = SetSettlerAnimation's. ret 4.
// CGameData::SetSettlerSavedAnimation (0x0050e320): (settler, kind): like
//   SetSettlerAnimation, but into the saved pair: dword +0x1c = animation
//   id 0x6a8060[kind * 4 + race], word +0x20 = GfxData_SpriteWord(id) - 1
//   on the graphics object [0x7a8010]. EAX = SpriteWord - 1. ret 8.
// All thiscall (ECX = world).

#include "game/globals.h"
#include "game/raw.h"
#include "game/replacements.h"
#include "game/tables.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/fake_graphics.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace s3re::game {

std::uint32_t __fastcall GfxData_SpriteWord(void* self, void* edx, std::uint32_t id);
std::uint32_t __fastcall CGameData_SetSettlerAnimation(CGameData* world, void* edx, std::uint32_t settler,
                                                       std::uint32_t kind);

namespace {

using layout::Building;
using layout::MapOffset;
using layout::Player;
using layout::Settler;
using layout::Tile;


constexpr std::uint8_t kSettlerInSlot = 0x40;  // Settler::flags
constexpr std::uint32_t kPartType = 0x2a;
constexpr std::uint16_t kPartJob = 0x130;

}  // namespace

using SpawnFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t count, std::uint32_t type,
                                           std::uint32_t player, std::uint32_t x, std::uint32_t y);
using SettlerFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t settler);
using SevenTileFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t type,
                                               std::uint32_t player, std::uint32_t x, std::uint32_t y);
using SettlerKindFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t settler,
                                                 std::uint32_t kind);

// Purpose: Creates a number of new settlers of one type for a player on free land of that player near a
// place (probably new inhabitants from a residence, or the start settlers).
std::uint32_t __fastcall CGameData_SpawnSettlersAround(CGameData* world, void*, std::uint32_t count,
                                                       std::uint32_t type, std::uint32_t player, std::uint32_t x,
                                                       std::uint32_t y) {
    std::uint32_t remaining = count;
    for (const MapOffset* offset = world->offsets; remaining != 0; ++offset) {
        const auto tx = static_cast<std::int32_t>(offset->dx + x);
        const auto ty = static_cast<std::int32_t>(offset->dy + y);
        const auto size = static_cast<std::int32_t>(world->map_size);
        if (tx < 0 || tx >= size || ty < 0 || ty >= size) {
            continue;
        }
        const Tile& tile = TileAtOffset(world, TileOffset(tx, ty));
        if (tile.settler != 0 || tile.terrain < 0x10 || (tile.flags & 0x0f) != 0 || tile.owner != player) {
            continue;
        }
        CGameData_CreateSettler(world, nullptr, tx, ty, type, player);
        --remaining;
        const std::int32_t typeClass = g_settlerTypeClass[type];
        if (typeClass == 0 || (typeClass >= 10 && typeClass <= 0x1d)) {
            ++world->players[player].population_limit_base;
        }
    }
    return 0;  // callers don't use EAX
}

// Purpose: A worker takes its place inside its building: the first free tile of the building's row
// becomes its spot, and the slot number tells where it is drawn.
std::uint32_t __fastcall CGameData_EnterBuildingSlot(CGameData* world, void*, std::uint32_t settler) {
    Settler& s = world->settlers[settler];
    if ((s.flags & kSettlerInSlot) != 0) {
        return 0;
    }
    const Building& building = world->buildings[s.building];
    const std::uint32_t y = building.y;
    std::uint32_t x = building.x + 1u;
    while (TileAtOffset(world, TileOffset(x, y)).settler != 0) {
        ++x;
    }
    const auto slot = static_cast<std::uint8_t>(x - building.x - 1);
    std::uint8_t value = static_cast<std::uint8_t>((building.race << 5) + slot);
    if (building.art == 0x10) {
        value = static_cast<std::uint8_t>(value + 8);
    } else if (building.art == 0x12) {
        value = static_cast<std::uint8_t>(value + 0x10);
    }
    s.slot = value;
    s.flags |= kSettlerInSlot;
    s.x = static_cast<std::uint16_t>(x);
    s.y = static_cast<std::uint16_t>(y);
    s.direction = 2;
    TileAtOffset(world, TileOffset(x, y)).settler = static_cast<std::uint16_t>(settler);
    return 0;  // callers don't use EAX
}

// Purpose: Creates a unit that covers seven tiles: a main settler and six part settlers around it, linked
// in a chain (probably a catapult or a big ship-like object built in a workshop).
std::uint32_t __fastcall CGameData_CreateSevenTileUnit(CGameData* world, void*, std::uint32_t type,
                                                       std::uint32_t player, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t main = CGameData_CreateSettler(world, nullptr, x, y, type, player);
    world->settlers[main].slot = 6;
    struct Step {
        std::int32_t dx, dy;
    };
    static constexpr Step kParts[] = {{1, 0}, {1, 1}, {0, 1}, {-1, 0}, {-1, -1}, {0, -1}};
    std::uint32_t previous = main;
    for (const Step& step : kParts) {
        const std::uint32_t part = CGameData_CreateSettler(world, nullptr, x + step.dx, y + step.dy, kPartType, player);
        world->settlers[previous].saved_counter = static_cast<std::uint16_t>(part);  // +0x20: the next part
        world->settlers[part].job = kPartJob;
        previous = part;
    }
    return 0;  // callers don't use EAX
}

// Purpose: Shows a soldier inside a military building with the animation for its kind of soldier, the
// building's size and the race.
std::uint32_t __fastcall CGameData_SetSoldierAnimationInBuilding(CGameData* world, void*, std::uint32_t settler) {
    std::uint32_t kind = 0x125;
    switch (world->settlers[settler].art) {
    case 0x20: kind = 0x126; break;
    case 0x23: kind = 0x127; break;
    case 0x08: kind = 0x128; break;
    case 0x1e: kind = 0x129; break;
    case 0x21: kind = 0x12a; break;
    case 0x0f: kind = 0x12b; break;
    case 0x1f: kind = 0x12c; break;
    case 0x22: kind = 0x12d; break;
    default: break;
    }
    const Building& building = world->buildings[world->settlers[settler].building];
    if (building.art == 0x10) {
        kind += 9;
    }
    if (building.art == 0x12) {
        kind += 0x12;
    }
    if (building.race == 1) {
        kind += 0x1b;
    }
    if (building.race == 2) {
        kind += 0x36;
    }
    return CGameData_SetSettlerAnimation(world, nullptr, settler, kind);
}

// Purpose: Prepares the animation a settler goes back to after an interruption (e.g. after a pause in
// its work): the animation id and its frame count are stored in the saved fields.
std::uint32_t __fastcall CGameData_SetSettlerSavedAnimation(CGameData* world, void*, std::uint32_t settler,
                                                            std::uint32_t kind) {
    Settler& s = world->settlers[settler];
    s.saved_animation = g_settlerAnimations[s.race + kind * 4];
    const std::uint32_t frames =
        GfxData_SpriteWord(g_gfxData.get(), nullptr, g_settlerAnimations[s.race + kind * 4]) - 1;
    s.saved_counter = static_cast<std::uint16_t>(frames);
    return frames;
}

S3RE_HOOK(0x0050cb20, CGameData_SpawnSettlersAround, SpawnFn, Original_CGameData_SpawnSettlersAround,
          CGameData_SpawnSettlersAround);
S3RE_HOOK(0x0050ccb0, CGameData_EnterBuildingSlot, SettlerFn, Original_CGameData_EnterBuildingSlot,
          CGameData_EnterBuildingSlot);
S3RE_HOOK(0x0050dd20, CGameData_CreateSevenTileUnit, SevenTileFn, Original_CGameData_CreateSevenTileUnit,
          CGameData_CreateSevenTileUnit);
S3RE_HOOK(0x0050e140, CGameData_SetSoldierAnimationInBuilding, SettlerFn,
          Original_CGameData_SetSoldierAnimationInBuilding, CGameData_SetSoldierAnimationInBuilding);
S3RE_HOOK(0x0050e320, CGameData_SetSettlerSavedAnimation, SettlerKindFn, Original_CGameData_SetSettlerSavedAnimation,
          CGameData_SetSettlerSavedAnimation);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTestSettlers = 0x400;
constexpr std::uint32_t kTestBuildings = 0x100;

template <typename Case>
void Compare(test::Context& ctx, int cases, int checkpointEvery, Case run) {
    test::WorldPair worlds(ctx, sizeof(CGameData));
    worlds.Both([&](CGameData* w) {
        // CreateSettler hands out the first free record: keep the free ones
        // (owner 0xff) at the start of the table.
        for (std::uint32_t s = 1; s < 0x7d00; ++s) {
            w->settlers[s].owner = s < kTestSettlers && (s % 7) != 0 ? 0xff : 1;
        }
        w->settler_high_water = kTestSettlers;
    });
    for (int i = 0; i < cases; ++i) {
        std::uint32_t expected = 0, actual = 0;
        run(worlds, expected, actual);
        if (!S3RE_EXPECT_EQ(expected, actual) || ((i % checkpointEvery == checkpointEvery - 1 || i == cases - 1) &&
                                                  !worlds.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (or one of the %d before it)", i, checkpointEvery - 1);
            return;
        }
    }
}

// The game's offset table: all tiles by TileDistance from (0, 0), nearest first.
void FillOffsetSpiral(test::WorldPair& worlds, std::uint32_t entries) {
    std::uint32_t k = 0;
    for (std::int32_t r = 0; k < entries; ++r) {
        for (std::int32_t dx = -r; dx <= r && k < entries; ++dx) {
            for (std::int32_t dy = -r; dy <= r && k < entries; ++dy) {
                if (TileDistance(0, 0, static_cast<std::uint32_t>(dx), static_cast<std::uint32_t>(dy)) ==
                    static_cast<std::uint32_t>(r)) {
                    worlds.Both([&](CGameData* w) {
                        w->offsets[k].dx = dx;
                        w->offsets[k].dy = dy;
                    });
                    ++k;
                }
            }
        }
    }
}

// Frees the settler records the last case used (so CreateSettler keeps
// finding records below kTestSettlers).
void FreeSettlers(test::WorldPair& worlds) {
    worlds.Both([&](CGameData* w) {
        for (std::uint32_t s = 1; s < kTestSettlers; ++s) {
            if ((s % 7) != 0) {
                w->settlers[s].owner = 0xff;
            }
        }
    });
}

void CompareSpawn(test::Context& ctx, SpawnFn candidate, int cases) {
    Compare(ctx, cases, 16, [&](test::WorldPair& worlds, std::uint32_t&, std::uint32_t&) {
        FillOffsetSpiral(worlds, 400);
        FreeSettlers(worlds);
        const std::uint32_t size = ctx.rng().Chance(1, 4) ? 20 + ctx.rng().Below(40) : 384;
        const std::uint32_t x = ctx.rng().Below(size), y = ctx.rng().Below(size);
        const std::uint32_t player = ctx.rng().Below(4);
        const std::uint32_t type = ctx.rng().Chance(1, 10) ? ctx.rng().Below(0x40) : ctx.rng().Below(0x30);
        std::uint32_t usable = 0;
        for (std::int32_t dx = -10; dx <= 10; ++dx) {
            for (std::int32_t dy = -10; dy <= 10; ++dy) {
                const std::uint32_t tx = x + dx, ty = y + dy;
                if (tx >= size || ty >= size) {
                    continue;
                }
                Tile tile{};
                const bool good = ctx.rng().Chance(1, 2);
                tile.owner = static_cast<std::uint8_t>(good || ctx.rng().Chance(1, 2) ? player : ctx.rng().Below(4));
                tile.terrain = static_cast<std::uint8_t>(good || ctx.rng().Chance(1, 2) ? 0x10 + ctx.rng().Below(0x30)
                                                                                        : ctx.rng().Below(0x20));
                tile.flags = static_cast<std::uint8_t>(good ? ctx.rng().Next32() & 0xf0 : ctx.rng().Next32());
                tile.settler = static_cast<std::uint16_t>(good || ctx.rng().Chance(1, 2) ? 0 : 1 + ctx.rng().Below(100));
                if (TileDistance(0, 0, dx, dy) <= 6 && tile.owner == player && tile.terrain >= 0x10 &&
                    (tile.flags & 0xf) == 0 && tile.settler == 0) {
                    ++usable;
                }
                worlds.Both([&](CGameData* w) {
                    Tile& t = w->tiles[tx][ty];
                    const std::uint8_t height = t.height;
                    t = tile;
                    t.height = height;
                });
            }
        }
        if (usable == 0) {
            return;
        }
        const std::uint32_t count = 1 + ctx.rng().Below(usable < 6 ? usable : 6);
        worlds.Both([&](CGameData* w) { w->map_size = size; });
        Original_CGameData_SpawnSettlersAround(worlds.original(), nullptr, count, type, player, x, y);
        candidate(worlds.ours(), nullptr, count, type, player, x, y);
    });
}

// Wrong: counts every settler type for the player's counter.
std::uint32_t __fastcall WrongSpawnSettlersAround(CGameData* world, void* edx, std::uint32_t count, std::uint32_t type,
                                                  std::uint32_t player, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t before = world->players[player].population_limit_base;
    CGameData_SpawnSettlersAround(world, edx, count, type, player, x, y);
    world->players[player].population_limit_base = before + count;
    return 0;
}

void CompareEnterSlot(test::Context& ctx, SettlerFn candidate, int cases) {
    Compare(ctx, cases, 64, [&](test::WorldPair& worlds, std::uint32_t&, std::uint32_t&) {
        const std::uint32_t settler = 1 + ctx.rng().Below(kTestSettlers);
        const std::uint32_t building = 1 + ctx.rng().Below(kTestBuildings);
        const std::uint32_t bx = 1 + ctx.rng().Below(370), by = 1 + ctx.rng().Below(380);
        static constexpr std::uint8_t kArts[] = {0x0f, 0x10, 0x12, 0x11, 0x05};
        const std::uint8_t art = kArts[ctx.rng().Below(5)];
        const auto race = static_cast<std::uint8_t>(ctx.rng().Chance(1, 8) ? ctx.rng().Next32() : ctx.rng().Below(4));
        const auto flags = static_cast<std::uint8_t>(ctx.rng().Next32() & (ctx.rng().Chance(1, 4) ? 0xff : ~0x40));
        const std::uint32_t taken = ctx.rng().Below(6);
        worlds.Both([&](CGameData* w) {
            Settler& s = w->settlers[settler];
            s.flags = flags;
            s.building = static_cast<std::uint16_t>(building);
            Building& b = w->buildings[building];
            b.x = static_cast<std::uint16_t>(bx);
            b.y = static_cast<std::uint16_t>(by);
            b.art = art;
            b.race = race;
            for (std::uint32_t k = 1; k <= 7; ++k) {
                w->tiles[bx + k][by].settler = static_cast<std::uint16_t>(k <= taken ? 1 + k : 0);
            }
        });
        Original_CGameData_EnterBuildingSlot(worlds.original(), nullptr, settler);
        candidate(worlds.ours(), nullptr, settler);
    });
}

// Wrong: adds 8 for type 0x0f too.
std::uint32_t __fastcall WrongEnterBuildingSlot(CGameData* world, void* edx, std::uint32_t settler) {
    Building& b = world->buildings[world->settlers[settler].building];
    const std::uint8_t art = b.art;
    if (art == 0x0f) {
        b.art = 0x10;
    }
    CGameData_EnterBuildingSlot(world, edx, settler);
    b.art = art;
    return 0;
}

void CompareSevenTile(test::Context& ctx, SevenTileFn candidate, int cases) {
    Compare(ctx, cases, 16, [&](test::WorldPair& worlds, std::uint32_t&, std::uint32_t&) {
        FreeSettlers(worlds);
        const std::uint32_t x = 1 + ctx.rng().Below(380), y = 1 + ctx.rng().Below(380);
        const std::uint32_t type = ctx.rng().Below(0x40), player = ctx.rng().Below(4);
        Original_CGameData_CreateSevenTileUnit(worlds.original(), nullptr, type, player, x, y);
        candidate(worlds.ours(), nullptr, type, player, x, y);
    });
}

// Wrong: the last part also links to itself.
std::uint32_t __fastcall WrongCreateSevenTileUnit(CGameData* world, void* edx, std::uint32_t type, std::uint32_t player,
                                                  std::uint32_t x, std::uint32_t y) {
    CGameData_CreateSevenTileUnit(world, edx, type, player, x, y);
    const std::uint32_t last = world->tiles[x][y - 1].settler;
    world->settlers[last].saved_counter = static_cast<std::uint16_t>(last);
    return 0;
}

void CompareSavedAnimation(test::Context& ctx, SettlerKindFn candidate, int cases) {
    test::FakeGraphics gfx;
    Compare(ctx, cases, 64, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        const std::uint32_t settler = ctx.rng().Below(0x7d00);
        const std::uint32_t race = ctx.rng().Below(4);
        const std::uint32_t kind = ctx.rng().Below(0x180);
        if (!gfx.PrepareSprite(ctx, g_settlerAnimations[kind * 4 + race])) {
            return;
        }
        worlds.Both([&](CGameData* w) { w->settlers[settler].race = static_cast<std::uint8_t>(race); });
        expected = Original_CGameData_SetSettlerSavedAnimation(worlds.original(), nullptr, settler, kind);
        actual = candidate(worlds.ours(), nullptr, settler, kind);
    });
}

// Wrong: stores the frame count without the -1.
std::uint32_t __fastcall WrongSetSettlerSavedAnimation(CGameData* world, void* edx, std::uint32_t settler,
                                                       std::uint32_t kind) {
    const std::uint32_t result = CGameData_SetSettlerSavedAnimation(world, edx, settler, kind);
    ++world->settlers[settler].saved_counter;
    return result;
}

void CompareSoldierAnimation(test::Context& ctx, SettlerFn candidate, int cases) {
    test::FakeGraphics gfx;
    Compare(ctx, cases, 64, [&](test::WorldPair& worlds, std::uint32_t& expected, std::uint32_t& actual) {
        const std::uint32_t settler = 1 + ctx.rng().Below(kTestSettlers);
        const std::uint32_t building = 1 + ctx.rng().Below(kTestBuildings);
        static constexpr std::uint8_t kTypes[] = {0x20, 0x23, 0x08, 0x1e, 0x21, 0x0f, 0x1f, 0x22, 0x05, 0x30};
        static constexpr std::uint8_t kArts[] = {0x10, 0x12, 0x11, 0x0f};
        const std::uint8_t type = kTypes[ctx.rng().Below(10)];
        const std::uint8_t art = kArts[ctx.rng().Below(4)];
        const auto race = static_cast<std::uint8_t>(ctx.rng().Below(3));
        const std::uint8_t settlerRace = static_cast<std::uint8_t>(ctx.rng().Below(4));
        worlds.Both([&](CGameData* w) {
            Settler& s = w->settlers[settler];
            s.art = type;
            s.race = settlerRace;
            s.building = static_cast<std::uint16_t>(building);
            w->buildings[building].art = art;
            w->buildings[building].race = race;
        });
        // The kind the original will use, to prepare the graphics data.
        std::uint32_t kind = 0x125;
        static constexpr std::uint8_t kOrder[] = {0x20, 0x23, 0x08, 0x1e, 0x21, 0x0f, 0x1f, 0x22};
        for (std::uint32_t k = 0; k < 8; ++k) {
            if (kOrder[k] == type) {
                kind = 0x126 + k;
            }
        }
        kind += art == 0x10 ? 9 : art == 0x12 ? 0x12 : 0;
        kind += race == 1 ? 0x1b : race == 2 ? 0x36 : 0;
        if (!gfx.PrepareSprite(ctx, g_settlerAnimations[kind * 4 + settlerRace])) {
            return;
        }
        expected = Original_CGameData_SetSoldierAnimationInBuilding(worlds.original(), nullptr, settler);
        actual = candidate(worlds.ours(), nullptr, settler);
    });
}

// Wrong: the race byte 2 of the building adds 0x1b (as race 1) instead
// of 0x36. (The graphics data is only prepared for the right kind, so the
// mistake is applied to the result: kind 0x1b lower means a different id.)
std::uint32_t __fastcall WrongSetSoldierAnimationInBuilding(CGameData* world, void* edx, std::uint32_t settler) {
    const std::uint32_t result = CGameData_SetSoldierAnimationInBuilding(world, edx, settler);
    Settler& s = world->settlers[settler];
    if (world->buildings[s.building].race == 2) {
        s.animation = g_settlerAnimations[s.race + (s.direction == 0 ? 0 : 0)];  // some other id
    }
    return result;
}

}  // namespace

S3RE_TEST(CGameData_SpawnSettlersAround_MatchesOriginal) {
    CompareSpawn(ctx, &CGameData_SpawnSettlersAround, 2000);
}
S3RE_TEST(CGameData_SpawnSettlersAround_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSpawn(probe, &WrongSpawnSettlersAround, 500);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_EnterBuildingSlot_MatchesOriginal) {
    CompareEnterSlot(ctx, &CGameData_EnterBuildingSlot, 10000);
}
S3RE_TEST(CGameData_EnterBuildingSlot_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareEnterSlot(probe, &WrongEnterBuildingSlot, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_CreateSevenTileUnit_MatchesOriginal) {
    CompareSevenTile(ctx, &CGameData_CreateSevenTileUnit, 2000);
}
S3RE_TEST(CGameData_CreateSevenTileUnit_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSevenTile(probe, &WrongCreateSevenTileUnit, 200);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_SetSoldierAnimationInBuilding_MatchesOriginal) {
    CompareSoldierAnimation(ctx, &CGameData_SetSoldierAnimationInBuilding, 5000);
}
S3RE_TEST(CGameData_SetSoldierAnimationInBuilding_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSoldierAnimation(probe, &WrongSetSoldierAnimationInBuilding, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_SetSettlerSavedAnimation_MatchesOriginal) {
    CompareSavedAnimation(ctx, &CGameData_SetSettlerSavedAnimation, 5000);
}
S3RE_TEST(CGameData_SetSettlerSavedAnimation_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSavedAnimation(probe, &WrongSetSettlerSavedAnimation, 2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
