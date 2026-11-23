// The spells that change the land (Volker\GodlyEvents.cpp): spells 0..3
// transform the terrain around a spot, spell 4 puts stones on free land,
// spell 30 (TreeCurse) plants trees. The terrain transform is also used by
// UpdateEnvironment for a burning tree (object 0xf4, kind 32).
//
// CGameData::SpellTransformLand (0x00548340): (player, x, y, spell) spells
//   0..3: HasManaForSpell; TransformTerrainAround(spell, x, y); paid if it
//   changed a tile. ret 0x10.
// CGameData::TransformTerrainAround (0x00548380): (kind, x, y) -> AL = a
//   tile changed. Builds a TerrainGrid (below) of side g_spellGridSides[kind]
//   (32) on its stack, placed around (x, y) and clamped to the map (signed;
//   on a map smaller than the grid the origin becomes negative and the grid
//   reaches before the map, as in the original). Per cell: the current
//   terrain, and whether it may change (IsTransformableTerrain; never on the
//   grid's border). Then for the first g_spellRingCounts[kind] ring offsets
//   around the centre whose cell may change, SpreadTerrainTransform. Then
//   every cell whose terrain changed is written back: redraw bit, bit 0x400
//   of seen_object cleared on the first 7 ring offsets' tiles (IsOnMap), and
//   per kind: 0 object cleared + UnblockTile, 1 UnblockTile, 2/3/32 object
//   cleared unless the tile is blocked or holds a stone (0x73..0x7f). Kind
//   3 finally calls WearPath on every grid tile of a building footprint.
//   The stack probe 0x61ff50 is the CRT's _chkstk (8 KB of locals); ours
//   gets the compiler's own. ret 0xc.
// CGameData::SpreadTerrainTransform (0x00548760): (grid, gx, gy): a flood
//   fill over the grid from (gx, gy) with the world's tile ring (world
//   +0xdfdc48, indices +0xe07888) as its queue: the start becomes the
//   kind's first terrain (NextTransformTerrain(0xff)); a neighbour (six
//   directions, NeighbourX/Y) whose terrain can't border the current cell's
//   (CanTerrainsBorder) and that may change becomes the next terrain of the
//   chain and is queued; one that may not change (or was already done and
//   still can't border) makes the whole fill fail, and the grid's terrain is
//   restored. Works on a copy of the may-change marks (2 = done); only the
//   start's mark in the grid is cleared. EAX not used. ret 0xc.
// CGameData::IsTransformableTerrain (0x00548a10): (kind, terrain) -> AL:
//   whether a kind may change this terrain (jump table 0x548b60 / index
//   bytes 0x548b80); EAX's upper bytes are the caller's. ret 8.
// CGameData::NextTransformTerrain (0x00548bb0): (grid, from, unused) -> AL:
//   the next terrain of the grid kind's chain after `from` (0xff = start),
//   0 if `from` isn't in it; the last terrain of kinds 0 and 1 is an
//   assertion ("HILFE", GodlyEvents.cpp lines 588 / 598). ret 0xc.
// CGameData::CanTerrainsBorder (0x00548d50): (grid, a, b) -> AL: whether
//   terrain b may lie next to a (a pure table, the grid is not read). ret 0xc.
// CGameData::SpellCreateStones (0x00548fb0): (player, x, y) spell 4: for
//   each of the first g_spellRingCounts[4] (500) ring offsets: Random() &
//   0xffff < 5000; then a tile on the map whose terrain and on-map
//   neighbours' terrains are all transformable by kind 4, and whose first 19
//   ring offsets' tiles are empty (no object, settler, building/pile, flags
//   0x0f), gets a stone 0x73 + (Random() & 0xffff) * 11 / 65536, is blocked
//   with its six neighbours (BlockTile), and gets two map effects (moving
//   0xd0030 and stationary 0x60) starting rand() % 8 ticks later (the CRT's
//   rand 0x61f802, not the simulation's Random). Paid if any stone was
//   placed. ret 0xc.
// CGameData::SpellTreeCurse (0x005492a0): (player, x, y) spell 30
//   ("EventTreeCurse"): DebugLog of the offset count, then for the first
//   g_spellRingCounts[30] (1000) ring offsets a tile on the map that is
//   empty (object, building/pile, settler, flags 0x0d), whose 18
//   neighbours up to two tiles away (read unchecked, as the original) have
//   neither flag 0x01 nor 0x08 and whose terrain is of class 0x10 but not
//   0x1c, gets object 0x54 (a young tree, see UpdateEnvironment), BlockTile
//   and a stationary map effect 0x60 starting rand() % 8 ticks later.
//   DebugLog of the count; paid if any. ret 0xc.
// thiscall (ECX = world).

#include "game/debug_log.h"
#include "game/enums.h"
#include "game/globals.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/world.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace s3re::game {

namespace {

using layout::MapEffect;
using layout::MapEffectTable;
using layout::MapOffset;
using layout::Tile;
using U = std::uint32_t;

constexpr std::uintptr_t kHasManaForSpell = 0x0054b240;
constexpr std::uintptr_t kPayForSpell = 0x0054b2b0;
constexpr std::uintptr_t kRandom = 0x004feb70;
constexpr std::uintptr_t kCrtRand = 0x0061f802;  // the C runtime's rand()
constexpr std::uintptr_t kNeighbourX = 0x00503700;
constexpr std::uintptr_t kNeighbourY = 0x00503730;
constexpr std::uintptr_t kIsOnMap = 0x005194e0;
constexpr std::uintptr_t kBlockTile = 0x0051b6f0;
constexpr std::uintptr_t kUnblockTile = 0x0051b770;
constexpr std::uintptr_t kWearPath = 0x0051b7f0;
constexpr std::uintptr_t kTileScreenX = 0x00519610;
constexpr std::uintptr_t kTileScreenY = 0x00519640;
constexpr std::uintptr_t kMakeStationary = 0x00543f70;
constexpr std::uintptr_t kMakeMoving = 0x00543930;
constexpr std::uintptr_t kAddMapEffect = 0x005445e0;
constexpr std::uintptr_t kThrowAssertmentFailed = 0x006119b0;

constexpr std::uintptr_t kTransformTerrainAround = 0x00548380;
constexpr std::uintptr_t kSpreadTerrainTransform = 0x00548760;
constexpr std::uintptr_t kIsTransformableTerrain = 0x00548a10;
constexpr std::uintptr_t kNextTransformTerrain = 0x00548bb0;
constexpr std::uintptr_t kCanTerrainsBorder = 0x00548d50;

using Fn0 = U(__fastcall*)(CGameData*, void*);
using Fn2 = U(__fastcall*)(CGameData*, void*, U, U);
using Fn3 = U(__fastcall*)(CGameData*, void*, U, U, U);
using CrtRandFn = int(__cdecl*)();
using Make3Fn = void*(__fastcall*)(MapEffect* self, void* edx, U, U, U);
using Make6Fn = void*(__fastcall*)(MapEffect* self, void* edx, U, U, U, U, U, U);
using AddFn = U(__fastcall*)(MapEffectTable* self, void* edx, const MapEffect* effect);
using AssertFn = void(__fastcall*)(const char* expression, const char* file, std::int32_t line);

template <typename F>
F Callee(std::uintptr_t address) {
    return reinterpret_cast<F>(address);
}

// The transform kinds: spells 0..3, and the ground around a burning tree.
constexpr U kKindSwamp = Value(Spell::Spell0);
constexpr U kKindSnow = Value(Spell::Spell1);
constexpr U kKindDesertify = Value(Spell::Spell2);
constexpr U kKindGreenDesert = Value(Spell::Spell3);
constexpr U kKindStones = Value(Spell::Spell4);   // only IsTransformableTerrain: where stones may lie
constexpr U kKindTreeCurse = Value(Spell::TreeCurse);  // only IsTransformableTerrain (TreeCurse checks inline)
constexpr U kKindBurntGround = 32;                // UpdateEnvironment, burning tree 0xf4

constexpr U kChainStart = 0xff;  // NextTransformTerrain's "from" for the fill's start

// The grid of TransformTerrainAround, on its stack; SpreadTerrainTransform
// gets a pointer to it.
constexpr std::int32_t kGridStride = 64;
struct TerrainGrid {
    U kind;                                         // 0x0000
    std::int32_t x0, y0;                            // 0x0004 map position of cell (0, 0)
    std::int32_t cx, cy;                            // 0x000c the centre's cell
    std::int32_t side;                              // 0x0014 cells per row and column (at most 64)
    std::uint8_t can_change[kGridStride][kGridStride];  // 0x0018 [gx][gy] 1 = may change, 0 = not (border)
    std::uint8_t terrain[kGridStride][kGridStride];     // 0x1018 [gx][gy] the new terrain
};
static_assert(sizeof(TerrainGrid) == 0x2018);
static_assert(offsetof(TerrainGrid, side) == 0x14);
static_assert(offsetof(TerrainGrid, can_change) == 0x18);
static_assert(offsetof(TerrainGrid, terrain) == 0x1018);

// SpreadTerrainTransform's marks (its copy of can_change).
constexpr std::uint8_t kMarkFixed = 0;
constexpr std::uint8_t kMarkOpen = 1;
constexpr std::uint8_t kMarkDone = 2;

constexpr U kTileRingSize = 10000;
constexpr std::int32_t kDirections = 6;
constexpr std::int32_t kSeenRingOffsets = 7;    // tiles whose seen_object loses bit 0x400
constexpr std::uint16_t kSeenWornPath = 0x400;  // seen_object: worn path (re/structs.md)

// Tile::object: stones 0x73..0x7e (kept by the transform up to 0x7f), a
// young tree planted by TreeCurse.
constexpr std::uint8_t kFirstStone = 0x73;
constexpr std::uint8_t kLastKeptObject = 0x7f;
constexpr U kStoneVariants = 11;
constexpr std::uint8_t kCursedTree = 0x54;

constexpr std::uint8_t kCreateStonesBlockingFlags = 0x0f;  // Tile::flags
constexpr std::uint8_t kTreeCurseBlockingFlags = 0x0d;
constexpr std::uint8_t kTreeCurseNeighbourFlags = layout::kTileBlocked | layout::kTileBuildingFootprint;
constexpr std::uint8_t kTerrainClassGrass = 0x10;
constexpr std::uint8_t kTerrainPath = 0x1c;  // worn path (WearPath)

constexpr U kStoneChance = 5000;      // of 65536, per ring offset
constexpr std::int32_t kStoneClearance = 19;  // ring offsets that must be empty
constexpr U kEffectDelays = 8;        // rand() % 8 ticks
constexpr U kStoneSparkleA0 = 12;
constexpr U kStoneSparkleGraphics = 0xd0030;  // raw-ok: graphics id (file 0xd, image 0x30), not an offset
constexpr U kSpellFlashKind = 0x60;

// GodlyEvents.cpp lines of the assertions in NextTransformTerrain.
constexpr std::int32_t kSwampChainEndLine = 588;  // 0x24c
constexpr std::int32_t kSnowChainEndLine = 598;   // 0x256

bool IsOneOf(U value, std::initializer_list<U> values) {
    for (const U v : values) {
        if (value == v) {
            return true;
        }
    }
    return false;
}

bool HasMana(CGameData* world, U player, U spell) {
    return (Callee<Fn2>(kHasManaForSpell)(world, nullptr, player, spell) & 0xff) != 0;
}

void Pay(CGameData* world, U player, U spell) {
    Callee<Fn2>(kPayForSpell)(world, nullptr, player, spell);
}

U Random(CGameData* world) {
    return Callee<Fn0>(kRandom)(world, nullptr);
}

U NeighbourX(CGameData* world, U x, U direction) {
    return Callee<Fn2>(kNeighbourX)(world, nullptr, x, direction);
}

U NeighbourY(CGameData* world, U y, U direction) {
    return Callee<Fn2>(kNeighbourY)(world, nullptr, y, direction);
}

bool OnMap(const CGameData* world, std::int32_t x, std::int32_t y) {
    const auto size = static_cast<std::int32_t>(world->map_size);
    return x >= 0 && x < size && y >= 0 && y < size;
}

// The tile at map (x, y) by the original's 32-bit address arithmetic (the
// grid may reach before the map on maps smaller than it).
Tile& TileAtMap(CGameData* world, std::int32_t x, std::int32_t y) {
    return TileAtOffset(world, TileOffset(static_cast<U>(x), static_cast<U>(y)));
}

bool IsTransformable(CGameData* world, U kind, std::uint8_t terrain) {
    return (Callee<Fn2>(kIsTransformableTerrain)(world, nullptr, kind, terrain) & 0xff) != 0;
}

std::uint8_t NextTerrain(CGameData* world, TerrainGrid* grid, U from, U to) {
    return static_cast<std::uint8_t>(
        Callee<Fn3>(kNextTransformTerrain)(world, nullptr, reinterpret_cast<std::uintptr_t>(grid), from, to));
}

bool CanBorder(CGameData* world, TerrainGrid* grid, std::uint8_t a, std::uint8_t b) {
    return (Callee<Fn3>(kCanTerrainsBorder)(world, nullptr, reinterpret_cast<std::uintptr_t>(grid), a, b) & 0xff) !=
           0;
}

// Two map effects of a spell at (x, y), starting `delay` ticks from now
// (SpellCreateStones: moving + stationary; SpellTreeCurse: stationary).
void AddMovingEffect(CGameData* world, U x, U y, U delay, U a0, U graphics) {
    const U sy = Callee<Fn2>(kTileScreenY)(world, nullptr, x, y);
    const U sx = Callee<Fn2>(kTileScreenX)(world, nullptr, x, y);
    MapEffect effect;
    void* const tick = reinterpret_cast<void*>(static_cast<std::uintptr_t>(g_gameTick + delay));
    Callee<Make6Fn>(kMakeMoving)(&effect, tick, a0, sx, sy, x, y, graphics);
    Callee<AddFn>(kAddMapEffect)(&world->map_effects, nullptr, &effect);
}

void AddStationaryEffect(CGameData* world, U x, U y, U delay, U kind) {
    const U sy = Callee<Fn2>(kTileScreenY)(world, nullptr, x, y);
    const U sx = Callee<Fn2>(kTileScreenX)(world, nullptr, x, y);
    MapEffect effect;
    void* const tick = reinterpret_cast<void*>(static_cast<std::uintptr_t>(g_gameTick + delay));
    Callee<Make3Fn>(kMakeStationary)(&effect, tick, sx, sy, kind);
    Callee<AddFn>(kAddMapEffect)(&world->map_effects, nullptr, &effect);
}

// rand() % 8 with C's signed remainder (rand() is never negative).
U EffectDelay() {
    return static_cast<U>(Callee<CrtRandFn>(kCrtRand)() % static_cast<int>(kEffectDelays));
}

// TransformTerrainAround's grid origin on one axis: centred, clamped to
// 0..size-side (signed; the upper limit wins, so it is negative on a map
// smaller than the grid).
std::int32_t GridOrigin(std::int32_t centre, std::int32_t half, std::int32_t limit) {
    std::int32_t origin = centre - half;
    if (origin < 0) {
        origin = 0;
    }
    return origin < limit ? origin : limit;
}

}  // namespace

// Purpose: Tells whether a land-changing spell (or the fire of a burning
// tree) may change a tile of this terrain.
U __fastcall CGameData_IsTransformableTerrain(CGameData*, void*, U kind, U terrain) {
    // 0x548a10, jump table 0x548b60 by kind.
    switch (kind) {
    case kKindSwamp:
        return IsOneOf(terrain, {0x50, 0x51, 0x15});
    case kKindSnow:
        return IsOneOf(terrain, {0x80, 0x81, 0x23});
    case kKindDesertify:
        return IsOneOf(terrain, {0x10, 0x12, 0x13, 0x41, 0x14, 0x18, 0x1c, 0x16});
    case kKindGreenDesert:
        return IsOneOf(terrain, {0x40, 0x41, 0x14});
    case kKindStones:
        return IsOneOf(terrain, {0x10, 0x12, 0x13, 0x41, 0x14, 0x18, 0x1c, 0x40});
    case kKindTreeCurse:
        return (terrain & kTerrainClassMask) == kTerrainClassGrass && terrain != kTerrainPath;
    case kKindBurntGround:
        return IsOneOf(terrain, {0x10, 0x12, 0x13, 0x18, 0x17, 0x91});
    default:
        return 0;
    }
}

// Purpose: The terrain a tile gets when the transform reaches it from a
// tile of terrain `from`: each kind changes the land along a chain (e.g.
// swamp: grass in the middle, then grass-swamp edges).
U __fastcall CGameData_NextTransformTerrain(CGameData*, void*, const TerrainGrid* grid, U from, U) {
    // 0x548bb0, jump table 0x548d0c by kind. Each chain: start, then the
    // terrain after each element; 0 for anything else.
    struct Chain {
        U start;
        U steps[3][2];
        std::int32_t assert_line;  // the last element is an assertion (0: none)
        U last;
    };
    static constexpr Chain kSwamp = {0x10, {{0x10, 0x15}, {0x15, 0x51}, {0, 0}}, kSwampChainEndLine, 0x51};
    static constexpr Chain kSnow = {0x20, {{0x20, 0x23}, {0x23, 0x81}, {0, 0}}, kSnowChainEndLine, 0x81};
    static constexpr Chain kDesertify = {0x40, {{0x40, 0x41}, {0x41, 0x14}, {0x14, 0x10}}, 0, 0};
    static constexpr Chain kGreenDesert = {0x10, {{0x10, 0x14}, {0x14, 0x41}, {0, 0}}, 0, 0};
    static constexpr Chain kBurntGround = {0x90, {{0x90, 0x91}, {0x91, 0x17}, {0x17, 0x10}}, 0, 0};
    const Chain* chain = nullptr;
    switch (grid->kind) {
    case kKindSwamp: chain = &kSwamp; break;
    case kKindSnow: chain = &kSnow; break;
    case kKindDesertify: chain = &kDesertify; break;
    case kKindGreenDesert: chain = &kGreenDesert; break;
    case kKindBurntGround: chain = &kBurntGround; break;
    default: return 0;
    }
    if (from == kChainStart) {
        return chain->start;
    }
    for (const auto& step : chain->steps) {
        if (step[0] != 0 && from == step[0]) {
            return step[1];
        }
    }
    if (chain->assert_line != 0 && from == chain->last) {
        Callee<AssertFn>(kThrowAssertmentFailed)(texts::kHelp.c_str(), texts::kGodlyEventsFile.c_str(),
                                                 chain->assert_line);
    }
    return 0;
}

// Purpose: Tells whether a tile of terrain b may lie next to one of terrain
// a, so that the transform needs no edge between them.
U __fastcall CGameData_CanTerrainsBorder(CGameData*, void*, const TerrainGrid*, U a, U b) {
    // 0x548d50.
    switch (a) {
    case 0x10:
        return IsOneOf(b, {0x10, 0x11, 0x30, 0x12, 0x13, 0x14, 0x15, 0x60, 0x61, 0x62, 0x63, 0x70, 0x71, 0x16, 0x17,
                           0x18, 0x1c});
    case 0x15: return IsOneOf(b, {0x10, 0x15, 0x51});
    case 0x51: return IsOneOf(b, {0x15, 0x51, 0x50});
    case 0x20: return IsOneOf(b, {0x20, 0x21, 0x22, 0x23});
    case 0x23: return IsOneOf(b, {0x20, 0x23, 0x81});
    case 0x81: return IsOneOf(b, {0x23, 0x81, 0x80});
    case 0x40: return IsOneOf(b, {0x40, 0x41});
    case 0x41: return IsOneOf(b, {0x40, 0x41, 0x14});
    case 0x14: return IsOneOf(b, {0x41, 0x14, 0x10});
    case 0x90: return IsOneOf(b, {0x90, 0x91});
    case 0x91: return IsOneOf(b, {0x90, 0x91, 0x17});
    case 0x17: return IsOneOf(b, {0x91, 0x17, 0x10});
    default: return 0;
    }
}

// Purpose: Spreads a terrain transform from one grid cell: the cell gets the
// kind's main terrain and the land around it changes outwards as far as it
// can, with edge terrains in between; if it would reach land that may not
// change, the whole spread is undone.
U __fastcall CGameData_SpreadTerrainTransform(CGameData* world, void*, TerrainGrid* grid, U gx, U gy) {
    const std::size_t bytes = static_cast<U>(grid->side) * kGridStride;
    std::uint8_t marks[kGridStride * kGridStride];
    std::uint8_t savedTerrain[kGridStride * kGridStride];
    std::memcpy(marks, grid->can_change, bytes);
    std::memcpy(savedTerrain, grid->terrain, bytes);
    std::uint8_t* const terrain = &grid->terrain[0][0];
    bool failed = false;

    layout::TilePosition* const ring = world->tile_ring;
    layout::Ring1000Indices& index = world->tile_ring_index;
    ring[0].x = static_cast<std::uint16_t>(gx);
    index.read = 0;
    index.write = 1;
    ring[0].y = static_cast<std::uint16_t>(gy);
    const U start = gx * kGridStride + gy;
    terrain[start] = NextTerrain(world, grid, kChainStart, kChainStart);
    marks[start] = kMarkDone;
    (&grid->can_change[0][0])[start] = 0;

    while (index.read != index.write) {
        const U cx = ring[index.read].x;
        const U cy = ring[index.read].y;
        const U current = cx * kGridStride + cy;
        for (std::int32_t direction = 0; direction < kDirections; ++direction) {
            const U nx = NeighbourX(world, cx, static_cast<U>(direction));
            const U ny = NeighbourY(world, cy, static_cast<U>(direction));
            const U next = nx * kGridStride + ny;
            std::uint8_t& mark = marks[next];
            // A done neighbour that can't border this cell spoils the fill
            // (0x54889c; the original then asks CanTerrainsBorder again).
            if (mark == kMarkDone && !CanBorder(world, grid, terrain[current], terrain[next])) {
                failed = true;
                continue;
            }
            if (CanBorder(world, grid, terrain[current], terrain[next])) {
                continue;
            }
            if (mark == kMarkFixed) {
                failed = true;
                continue;
            }
            if (mark != kMarkOpen) {
                continue;
            }
            terrain[next] = NextTerrain(world, grid, terrain[current], terrain[next]);
            ring[index.write].x = static_cast<std::uint16_t>(nx);
            mark = kMarkDone;
            ring[index.write].y = static_cast<std::uint16_t>(ny);
            if (++index.write == kTileRingSize) {
                index.write = 0;
            }
        }
        if (++index.read == kTileRingSize) {
            index.read = 0;
        }
        if (failed) {
            std::memcpy(grid->terrain, savedTerrain, bytes);
            break;
        }
    }
    return 0;
}

// Purpose: Changes the terrain around a spot (a land spell, or the ground
// around a burning tree): returns whether any tile changed.
U __fastcall CGameData_TransformTerrainAround(CGameData* world, void*, U kind, std::int32_t x, std::int32_t y) {
    TerrainGrid grid;
    grid.kind = kind;
    const std::int32_t side = g_spellGridSides[kind];
    const std::int32_t half = side / 2;
    const std::int32_t limit = static_cast<std::int32_t>(world->map_size) - side;
    grid.side = side;
    grid.x0 = GridOrigin(x, half, limit);
    grid.y0 = GridOrigin(y, half, limit);
    grid.cx = x - grid.x0;
    grid.cy = y - grid.y0;

    // 0x548434: the cells; the border never changes.
    for (std::int32_t gx = 0; gx < grid.side; ++gx) {
        for (std::int32_t gy = 0; gy < grid.side; ++gy) {
            const std::uint8_t terrain = TileAtMap(world, grid.x0 + gx, grid.y0 + gy).terrain;
            grid.terrain[gx][gy] = terrain;
            const bool border = gx == 0 || gx == grid.side - 1 || gy == 0 || gy == grid.side - 1;
            grid.can_change[gx][gy] = border ? 0 : static_cast<std::uint8_t>(IsTransformable(world, kind, terrain));
        }
    }

    // 0x5484c6: spread from the ring offsets around the centre (the count is
    // re-read every step, as in the original).
    for (std::int32_t i = 0; i < g_spellRingCounts[kind]; ++i) {
        const MapOffset& o = world->offsets[i];
        const std::int32_t gx = o.dx + grid.cx;
        const std::int32_t gy = o.dy + grid.cy;
        if (gx >= 0 && gx < grid.side && gy >= 0 && gy < grid.side && grid.can_change[gx][gy] != 0) {
            Callee<Fn3>(kSpreadTerrainTransform)(world, nullptr, reinterpret_cast<std::uintptr_t>(&grid),
                                                 static_cast<U>(gx), static_cast<U>(gy));
        }
    }

    // 0x548537: write the changed cells back.
    bool changed = false;
    for (std::int32_t gx = 0; gx < grid.side; ++gx) {
        for (std::int32_t gy = 0; gy < grid.side; ++gy) {
            const std::int32_t tx = grid.x0 + gx;
            const std::int32_t ty = grid.y0 + gy;
            Tile& tile = TileAtMap(world, tx, ty);
            if (tile.terrain == grid.terrain[gx][gy]) {
                continue;
            }
            tile.terrain = grid.terrain[gx][gy];
            changed = true;
            tile.draw_flags |= layout::kTileDrawRedraw;
            for (std::int32_t k = 0; k < kSeenRingOffsets; ++k) {
                const MapOffset& o = world->offsets[k];
                const U nx = static_cast<U>(o.dx + gx + grid.x0);
                const U ny = static_cast<U>(o.dy + gy + grid.y0);
                if (Callee<Fn2>(kIsOnMap)(world, nullptr, nx, ny) != 0) {
                    TileAtOffset(world, TileOffset(nx, ny)).seen_object &= static_cast<std::uint16_t>(~kSeenWornPath);
                }
            }
            if (kind == kKindSwamp) {
                tile.object = 0;
                Callee<Fn2>(kUnblockTile)(world, nullptr, static_cast<U>(tx), static_cast<U>(ty));
            } else if (kind == kKindSnow) {
                Callee<Fn2>(kUnblockTile)(world, nullptr, static_cast<U>(tx), static_cast<U>(ty));
            } else if (kind == kKindDesertify || kind == kKindGreenDesert || kind == kKindBurntGround) {
                const bool keep = (tile.flags & layout::kTileBlocked) != 0 ||
                                  (tile.object >= kFirstStone && tile.object <= kLastKeptObject);
                if (!keep) {
                    tile.object = 0;
                }
            }
        }
    }

    // 0x5486f4: green desert wears paths on building footprints.
    if (kind == kKindGreenDesert) {
        for (std::int32_t gx = 0; gx < grid.side; ++gx) {
            for (std::int32_t gy = 0; gy < grid.side; ++gy) {
                const std::int32_t tx = grid.x0 + gx;
                const std::int32_t ty = grid.y0 + gy;
                if ((TileAtMap(world, tx, ty).flags & layout::kTileBuildingFootprint) != 0) {
                    Callee<Fn2>(kWearPath)(world, nullptr, static_cast<U>(tx), static_cast<U>(ty));
                }
            }
        }
    }
    return changed ? 1 : 0;
}

// Purpose: Spells 0..3: change the land around a spot (dry a swamp, melt
// snow, turn land into desert, green a desert).
U __fastcall CGameData_SpellTransformLand(CGameData* world, void*, U player, std::int32_t x, std::int32_t y, U spell) {
    if (!HasMana(world, player, spell)) {
        return 0;
    }
    const U changed = Callee<Fn3>(kTransformTerrainAround)(world, nullptr, spell, static_cast<U>(x), static_cast<U>(y));
    if ((changed & 0xff) != 0) {
        Pay(world, player, spell);
    }
    return 0;
}

// Purpose: Spell 4: puts stones on free land around a spot.
U __fastcall CGameData_SpellCreateStones(CGameData* world, void*, U player, std::int32_t x, std::int32_t y) {
    constexpr U kSpell = Value(Spell::Spell4);
    if (!HasMana(world, player, kSpell)) {
        return 0;
    }
    bool placed = false;
    for (std::int32_t i = 0; i < g_spellRingCounts[kSpell]; ++i) {
        const MapOffset& o = world->offsets[i];
        if ((Random(world) & 0xffff) >= kStoneChance) {
            continue;
        }
        const std::int32_t tx = o.dx + x;
        const std::int32_t ty = o.dy + y;
        if (!OnMap(world, tx, ty)) {
            continue;
        }
        Tile& tile = world->tiles[tx][ty];
        // 0x549047: the tile and its neighbours on the map must be land for stones.
        bool blocked = !IsTransformable(world, kKindStones, tile.terrain);
        for (std::int32_t direction = 0; direction < kDirections; ++direction) {
            const auto nx = static_cast<std::int32_t>(NeighbourX(world, static_cast<U>(tx), static_cast<U>(direction)));
            const auto ny = static_cast<std::int32_t>(NeighbourY(world, static_cast<U>(ty), static_cast<U>(direction)));
            if (OnMap(world, nx, ny) && !IsTransformable(world, kKindStones, world->tiles[nx][ny].terrain)) {
                blocked = true;
            }
        }
        if (blocked) {
            continue;
        }
        // 0x5490ee: the first 19 ring offsets around it must be empty.
        for (std::int32_t k = 0; k < kStoneClearance; ++k) {
            const MapOffset& n = world->offsets[k];
            const std::int32_t nx = n.dx + tx;
            const std::int32_t ny = n.dy + ty;
            if (!OnMap(world, nx, ny)) {
                continue;
            }
            const Tile& other = world->tiles[nx][ny];
            if (other.object != 0 || other.settler != 0 || other.object_number != 0 ||
                (other.flags & kCreateStonesBlockingFlags) != 0) {
                blocked = true;
            }
        }
        if (blocked) {
            continue;
        }
        placed = true;
        const U r = Random(world) & 0xffff;
        tile.object = static_cast<std::uint8_t>(kFirstStone + ((r * kStoneVariants) >> 16));
        Callee<Fn2>(kBlockTile)(world, nullptr, static_cast<U>(tx), static_cast<U>(ty));
        for (std::int32_t direction = 0; direction < kDirections; ++direction) {
            const U nx = NeighbourX(world, static_cast<U>(tx), static_cast<U>(direction));
            const U ny = NeighbourY(world, static_cast<U>(ty), static_cast<U>(direction));
            Callee<Fn2>(kBlockTile)(world, nullptr, nx, ny);
        }
        const U delay = EffectDelay();
        AddMovingEffect(world, static_cast<U>(tx), static_cast<U>(ty), delay, kStoneSparkleA0, kStoneSparkleGraphics);
        AddStationaryEffect(world, static_cast<U>(tx), static_cast<U>(ty), delay, kSpellFlashKind);
    }
    if (placed) {
        Pay(world, player, kSpell);
    }
    return 0;
}

namespace {

// SpellTreeCurse's neighbours up to two tiles away (tile steps in the
// order of the original's tests at 0x549374..0x549451).
constexpr std::int8_t kTreeCurseNeighbours[18][2] = {
    {1, 0},  {-1, 0}, {0, 1},  {0, -1}, {1, 1},   {-1, -1}, {2, 0},  {2, 1},  {2, 2},
    {1, 2},  {0, 2},  {-1, 1}, {-2, 0}, {-2, -1}, {-2, -2}, {-1, -2}, {0, -2}, {1, -1}};

bool CanPlantCursedTree(CGameData* world, std::int32_t x, std::int32_t y) {
    const Tile& tile = world->tiles[x][y];
    if (tile.object != 0 || tile.object_number != 0 || tile.settler != 0 ||
        (tile.flags & kTreeCurseBlockingFlags) != 0) {
        return false;
    }
    // Unchecked reads, as in the original (near the edge they reach other
    // world fields).
    const U base = TileOffset(static_cast<U>(x), static_cast<U>(y));
    for (const auto& d : kTreeCurseNeighbours) {
        const U offset = base + static_cast<U>(d[0]) * kTileRowBytes + static_cast<U>(d[1]) * kTileBytes;
        if ((TileAtOffset(world, offset).flags & kTreeCurseNeighbourFlags) != 0) {
            return false;
        }
    }
    return (tile.terrain & kTerrainClassMask) == kTerrainClassGrass && tile.terrain != kTerrainPath;
}

}  // namespace

// Purpose: Spell 30 ("TreeCurse"): plants young trees on free grass around a
// spot, probably to block an enemy's building land.
U __fastcall CGameData_SpellTreeCurse(CGameData* world, void*, U player, std::int32_t x, std::int32_t y) {
    constexpr U kSpell = Value(Spell::TreeCurse);
    if (!HasMana(world, player, kSpell)) {
        return 0;
    }
    GameDebugLog(texts::kTreeCurseOffsets, g_spellRingCounts[kSpell]);
    U planted = 0;
    for (std::int32_t i = 0; i < g_spellRingCounts[kSpell]; ++i) {
        const MapOffset& o = world->offsets[i];
        const std::int32_t tx = o.dx + x;
        const std::int32_t ty = o.dy + y;
        if (!OnMap(world, tx, ty) || !CanPlantCursedTree(world, tx, ty)) {
            continue;
        }
        ++planted;
        world->tiles[tx][ty].object = kCursedTree;
        Callee<Fn2>(kBlockTile)(world, nullptr, static_cast<U>(tx), static_cast<U>(ty));
        AddStationaryEffect(world, static_cast<U>(tx), static_cast<U>(ty), EffectDelay(), kSpellFlashKind);
    }
    GameDebugLog(texts::kTreeCurseTreesSet, planted);
    if (planted != 0) {
        Pay(world, player, kSpell);
    }
    return 0;
}

using KindTerrainFn = U(__fastcall*)(CGameData*, void*, U kind, U terrain);
using GridTerrainFn = U(__fastcall*)(CGameData*, void*, const TerrainGrid* grid, U a, U b);
using SpreadFn = U(__fastcall*)(CGameData*, void*, TerrainGrid* grid, U gx, U gy);
using TransformFn = U(__fastcall*)(CGameData*, void*, U kind, std::int32_t x, std::int32_t y);
using TransformLandFn = U(__fastcall*)(CGameData*, void*, U player, std::int32_t x, std::int32_t y, U spell);
using LandSpellFn = U(__fastcall*)(CGameData*, void*, U player, std::int32_t x, std::int32_t y);

S3RE_HOOK(0x00548a10, CGameData_IsTransformableTerrain, KindTerrainFn, Original_CGameData_IsTransformableTerrain,
          CGameData_IsTransformableTerrain);
S3RE_HOOK(0x00548bb0, CGameData_NextTransformTerrain, GridTerrainFn, Original_CGameData_NextTransformTerrain,
          CGameData_NextTransformTerrain);
S3RE_HOOK(0x00548d50, CGameData_CanTerrainsBorder, GridTerrainFn, Original_CGameData_CanTerrainsBorder,
          CGameData_CanTerrainsBorder);
S3RE_HOOK(0x00548760, CGameData_SpreadTerrainTransform, SpreadFn, Original_CGameData_SpreadTerrainTransform,
          CGameData_SpreadTerrainTransform);
S3RE_HOOK(0x00548380, CGameData_TransformTerrainAround, TransformFn, Original_CGameData_TransformTerrainAround,
          CGameData_TransformTerrainAround);
S3RE_HOOK(0x00548340, CGameData_SpellTransformLand, TransformLandFn, Original_CGameData_SpellTransformLand,
          CGameData_SpellTransformLand);
S3RE_HOOK(0x00548fb0, CGameData_SpellCreateStones, LandSpellFn, Original_CGameData_SpellCreateStones,
          CGameData_SpellCreateStones);
S3RE_HOOK(0x005492a0, CGameData_SpellTreeCurse, LandSpellFn, Original_CGameData_SpellTreeCurse,
          CGameData_SpellTreeCurse);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kWorld = sizeof(CGameData);

// Calls run(context) and reports whether a C++ exception (the assertion
// of NextTransformTerrain) left it. No C++ objects in this frame.
int CxxExceptionFilter(DWORD code) {
    return code == 0xE06D7363 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

bool Threw(void (*run)(void*), void* context) {
    __try {
        run(context);
    } __except (CxxExceptionFilter(GetExceptionCode())) {
        return true;
    }
    return false;
}

// While it lives, the hooked helpers run as original code (for the
// original side of a comparison).
struct OriginalHelpers {
    test::ScopedJump transform{kTransformTerrainAround,
                               reinterpret_cast<const void*>(Original_CGameData_TransformTerrainAround)};
    test::ScopedJump spread{kSpreadTerrainTransform,
                            reinterpret_cast<const void*>(Original_CGameData_SpreadTerrainTransform)};
    test::ScopedJump transformable{kIsTransformableTerrain,
                                   reinterpret_cast<const void*>(Original_CGameData_IsTransformableTerrain)};
    test::ScopedJump next{kNextTransformTerrain, reinterpret_cast<const void*>(Original_CGameData_NextTransformTerrain)};
    test::ScopedJump border{kCanTerrainsBorder, reinterpret_cast<const void*>(Original_CGameData_CanTerrainsBorder)};
};

// The CRT's rand() during a test: a fixed sequence restarted for each side.
U g_fakeRandState = 0;
int __cdecl FakeRand() {
    g_fakeRandState = g_fakeRandState * 1103515245u + 12345u;
    return static_cast<int>((g_fakeRandState >> 16) & 0x7fff);
}

// The terrains the transforms know, and a few others.
constexpr std::uint8_t kTerrains[] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x1c, 0x20, 0x21,
                                      0x22, 0x23, 0x30, 0x40, 0x41, 0x50, 0x51, 0x60, 0x61, 0x62, 0x63, 0x70,
                                      0x71, 0x80, 0x81, 0x90, 0x91, 0x00, 0x05, 0x1f};

std::uint8_t SomeTerrain(test::Context& ctx) {
    return ctx.rng().Chance(1, 10) ? static_cast<std::uint8_t>(ctx.rng().Next32())
                                   : kTerrains[ctx.rng().Below(sizeof(kTerrains))];
}

U SomeKindValue(test::Context& ctx) {
    return ctx.rng().Chance(1, 10) ? ctx.rng().Next32() : ctx.rng().Below(40);
}

// --- the pure helpers -------------------------------------------------------

struct PureCall {
    GridTerrainFn fn;
    const TerrainGrid* grid;
    U a, b, eax;
};

void RunPure(void* context) {
    auto* call = static_cast<PureCall*>(context);
    call->eax = call->fn(nullptr, nullptr, call->grid, call->a, call->b);
}

// IsTransformableTerrain doesn't take the grid: (kind, terrain).
void CompareTransformable(test::Context& ctx, KindTerrainFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        U kind = 0;
        U terrain = 0;
        if (i < 40 * 0x101) {
            kind = static_cast<U>(i) / 0x101;
            terrain = static_cast<U>(i) % 0x101;
        } else {
            kind = SomeKindValue(ctx);
            terrain = ctx.rng().Chance(1, 2) ? SomeTerrain(ctx) : ctx.rng().Next32();
        }
        const U expected = Original_CGameData_IsTransformableTerrain(nullptr, nullptr, kind, terrain) & 0xff;
        const U actual = candidate(nullptr, nullptr, kind, terrain) & 0xff;
        if (!S3RE_EXPECT_EQ(expected, actual)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: kind %u, terrain %#x", i, kind, terrain);
            return;
        }
    }
}

// NextTransformTerrain: (grid of a kind, from); the third argument unused.
void CompareNext(test::Context& ctx, GridTerrainFn candidate, int cases) {
    TerrainGrid grid{};
    for (int i = 0; i < cases; ++i) {
        U from = 0;
        if (i < 40 * 0x101) {
            grid.kind = static_cast<U>(i) / 0x101;
            from = static_cast<U>(i) % 0x101;
        } else {
            grid.kind = SomeKindValue(ctx);
            from = ctx.rng().Chance(1, 2) ? SomeTerrain(ctx) : ctx.rng().Next32();
        }
        const U unused = ctx.rng().Next32();
        PureCall expected{Original_CGameData_NextTransformTerrain, &grid, from, unused, 0};
        PureCall actual{candidate, &grid, from, unused, 0};
        const bool expectedThrew = Threw(&RunPure, &expected);
        const bool actualThrew = Threw(&RunPure, &actual);
        if (!S3RE_EXPECT_EQ(expectedThrew, actualThrew) ||
            (!expectedThrew && !S3RE_EXPECT_EQ(expected.eax & 0xff, actual.eax & 0xff))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: kind %u, from %#x", i, grid.kind, from);
            return;
        }
    }
}

// CanTerrainsBorder: (a, b), the grid unused.
void CompareBorder(test::Context& ctx, GridTerrainFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        U a = 0;
        U b = 0;
        if (i < 0x101 * 0x101) {
            a = static_cast<U>(i) / 0x101;
            b = static_cast<U>(i) % 0x101;
        } else {
            a = ctx.rng().Chance(1, 2) ? SomeTerrain(ctx) : ctx.rng().Next32();
            b = ctx.rng().Chance(1, 2) ? SomeTerrain(ctx) : ctx.rng().Next32();
        }
        const U expected = Original_CGameData_CanTerrainsBorder(nullptr, nullptr, nullptr, a, b) & 0xff;
        const U actual = candidate(nullptr, nullptr, nullptr, a, b) & 0xff;
        if (!S3RE_EXPECT_EQ(expected, actual)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: a %#x, b %#x", i, a, b);
            return;
        }
    }
}

// --- SpreadTerrainTransform --------------------------------------------------

// A grid of a kind with patches of terrain; the border can't change.
void RandomGrid(test::Context& ctx, TerrainGrid& grid) {
    auto& rng = ctx.rng();
    static constexpr U kKinds[] = {kKindSwamp, kKindSnow, kKindDesertify, kKindGreenDesert, kKindBurntGround};
    grid.kind = rng.Chance(1, 20) ? rng.Below(40) : kKinds[rng.Below(5)];
    rng.Fill(&grid.can_change, sizeof(grid.can_change));
    rng.Fill(&grid.terrain, sizeof(grid.terrain));
    grid.side = static_cast<std::int32_t>(rng.Chance(1, 4) ? 3 + rng.Below(62) : 32);
    grid.x0 = static_cast<std::int32_t>(rng.Next32());
    grid.y0 = static_cast<std::int32_t>(rng.Next32());
    grid.cx = static_cast<std::int32_t>(rng.Next32());
    grid.cy = static_cast<std::int32_t>(rng.Next32());
    const std::uint8_t base = SomeTerrain(ctx);
    const U mix = rng.Below(4);  // 0: one terrain, 3: mostly mixed
    const bool marksFollowTerrain = rng.Chance(3, 4);
    for (std::int32_t gx = 0; gx < grid.side; ++gx) {
        for (std::int32_t gy = 0; gy < grid.side; ++gy) {
            const std::uint8_t terrain = rng.Below(4) < mix ? SomeTerrain(ctx) : base;
            grid.terrain[gx][gy] = terrain;
            const bool border = gx == 0 || gx == grid.side - 1 || gy == 0 || gy == grid.side - 1;
            std::uint8_t mark = static_cast<std::uint8_t>(rng.Below(4) == 0 ? rng.Below(4) : 1);
            if (marksFollowTerrain) {
                mark = static_cast<std::uint8_t>(
                    Original_CGameData_IsTransformableTerrain(nullptr, nullptr, grid.kind, terrain) & 0xff);
            }
            grid.can_change[gx][gy] = border ? 0 : mark;
        }
    }
}

struct SpreadCall {
    SpreadFn fn;
    CGameData* world;
    TerrainGrid* grid;
    U gx, gy;
};

void RunSpread(void* context) {
    auto* call = static_cast<SpreadCall*>(context);
    call->fn(call->world, nullptr, call->grid, call->gx, call->gy);
}

void CompareSpread(test::Context& ctx, SpreadFn candidate, int cases) {
    test::WorldPair worlds(ctx, offsetof(CGameData, tile_ring_index) + sizeof(layout::Ring1000Indices));
    std::vector<TerrainGrid> grids(2);
    for (int i = 0; i < cases; ++i) {
        RandomGrid(ctx, grids[0]);
        grids[1] = grids[0];
        const TerrainGrid& g = grids[0];
        const U gx = 1 + ctx.rng().Below(static_cast<U>(g.side - 2));
        const U gy = 1 + ctx.rng().Below(static_cast<U>(g.side - 2));
        SpreadCall expected{Original_CGameData_SpreadTerrainTransform, worlds.original(), &grids[0], gx, gy};
        SpreadCall actual{candidate, worlds.ours(), &grids[1], gx, gy};
        bool expectedThrew = false;
        {
            OriginalHelpers originals;
            expectedThrew = Threw(&RunSpread, &expected);
        }
        const bool actualThrew = Threw(&RunSpread, &actual);
        // The ring and its indices: compare all of it every few cases.
        const bool same = i % 16 == 0 ? worlds.Checkpoint(ctx)
                                      : worlds.SameField(ctx, [](CGameData* w) -> auto& { return w->tile_ring_index; });
        if (!S3RE_EXPECT_EQ(expectedThrew, actualThrew) ||
            !S3RE_EXPECT(std::memcmp(&grids[0], &grids[1], sizeof(TerrainGrid)) == 0) || !same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: kind %u, side %d, start (%u, %u)", i, g.kind, g.side, gx, gy);
            return;
        }
    }
}

// --- the spells ---------------------------------------------------------------

// A map with patches of the transforms' terrains, objects, stones and
// flags, a ring table of small offsets, players with some mana and an
// empty map effect table.
void SetUpLandWorld(test::Context& ctx, test::WorldPair& worlds) {
    auto& rng = ctx.rng();
    CGameData* w = worlds.original();
    // Small maps put the transform's grid partly before the map (rows of
    // other world fields; at least 20 so that it stays inside the world).
    const U size = rng.Chance(1, 10) ? 20 + rng.Below(18) : 40 + rng.Below(90);
    w->map_size = size;
    for (U n = 0; n < 1100; ++n) {
        const auto d = static_cast<std::int32_t>(1 + n / 12);
        const auto limit = d > 20 ? 20 : d;
        w->offsets[n].dx = rng.Range(-limit, limit);
        w->offsets[n].dy = rng.Range(-limit, limit);
    }
    const std::uint8_t base = SomeTerrain(ctx);
    const U mix = rng.Below(5);
    // How often a tile holds something (an object, a settler, flags): the
    // stones and trees need empty land around them.
    const U busy = rng.Chance(1, 2) ? 1 + rng.Below(20) : 100 + rng.Below(400);
    for (U x = 0; x < 140; ++x) {
        for (U y = 0; y < 140; ++y) {
            Tile& t = w->tiles[x][y];
            t.terrain = rng.Below(6) < mix ? SomeTerrain(ctx) : base;
            t.object = static_cast<std::uint8_t>(rng.Below(1000) < busy ? rng.Next32() : 0);
            t.settler = static_cast<std::uint16_t>(rng.Below(1000) < busy / 4 ? rng.Next32() : 0);
            t.object_number = static_cast<std::uint16_t>(rng.Below(1000) < busy / 4 ? rng.Next32() : 0);
            t.flags = static_cast<std::uint8_t>(rng.Below(1000) < busy ? rng.Next32() : 0);
        }
    }
    for (U p = 0; p < layout::kPlayers; ++p) {
        layout::Player& player = w->players[p];
        player.race = static_cast<std::int32_t>(rng.Below(4));
        player.mana = static_cast<std::int32_t>(rng.Below(100));
        for (auto& needed : player.spell_mana) {
            needed = static_cast<std::int32_t>(rng.Below(60));
        }
    }
    std::memset(&w->map_effects, 0, sizeof(w->map_effects));
    w->map_effects.blocks_x = 30;
    w->map_effects.blocks_y = 30;
    std::memcpy(worlds.oursBytes(), worlds.originalBytes(), kWorld);
}

// The effect table without the records' padding (the make functions'
// stack leftovers, see spell_effects.cpp).
void ClearEffectPadding(test::WorldPair& worlds) {
    const std::size_t base = offsetof(CGameData, map_effects);
    for (std::size_t slot = 0; slot < 5000; ++slot) {
        for (std::size_t pad : {std::size_t{0x2a}, std::size_t{0x2b}, std::size_t{0x3b}, std::size_t{0x3e},
                                std::size_t{0x3f}}) {
            worlds.originalBytes()[base + slot * sizeof(MapEffect) + pad] = 0;
            worlds.oursBytes()[base + slot * sizeof(MapEffect) + pad] = 0;
        }
    }
}

std::int32_t SpellCoordinate(test::Context& ctx, U size) {
    return ctx.rng().Chance(1, 10) ? ctx.rng().Range(-20, static_cast<std::int32_t>(size) + 20)
                                   : static_cast<std::int32_t>(ctx.rng().Below(size));
}

// The callees with effects beyond the tile bytes are recorded instead.
void InstallLandStubs(test::CalleeStubs& stubs) {
    stubs.Install<2, kUnblockTile>();
    stubs.Install<2, kWearPath>();
    stubs.Install<2, kBlockTile>();
}

struct LandCall {
    void (*call)(CGameData* world, const U* args);
    CGameData* world;
    const U* args;
};

void RunLand(void* context) {
    auto* c = static_cast<LandCall*>(context);
    c->call(c->world, c->args);
}

// Runs the original and our version of a land function (args: up to 4
// dwords) on both worlds and compares calls, logs, exceptions and the
// world. `pick` draws the arguments.
template <typename Original, typename Candidate, typename Pick>
void CompareLand(test::Context& ctx, Original original, Candidate candidate, Pick pick, int cases) {
    test::WorldPair worlds(ctx, kWorld);
    test::CalleeStubs stubs;
    InstallLandStubs(stubs);
    DebugLogRecorder logs;
    const U savedTick = g_gameTick;
    test::ScopedJump rand(kCrtRand, reinterpret_cast<const void*>(&FakeRand));
    for (int i = 0; i < cases; ++i) {
        if (i % 25 == 0) {
            SetUpLandWorld(ctx, worlds);
        }
        U args[4] = {};
        pick(ctx, worlds.original()->map_size, args);
        g_gameTick = ctx.rng().Next32();
        const U randSeed = ctx.rng().Next32();
        LandCall expected{original, worlds.original(), args};
        LandCall actual{candidate, worlds.ours(), args};
        stubs.SetWorld(worlds.original());
        g_fakeRandState = randSeed;
        bool expectedThrew = false;
        {
            OriginalHelpers originals;
            expectedThrew = Threw(&RunLand, &expected);
        }
        const auto expectedCalls = stubs.Take();
        const auto expectedLogs = logs.Take();
        stubs.SetWorld(worlds.ours());
        g_fakeRandState = randSeed;
        const bool actualThrew = Threw(&RunLand, &actual);
        ClearEffectPadding(worlds);
        if (!S3RE_EXPECT_EQ(expectedThrew, actualThrew) || !S3RE_EXPECT(expectedCalls == stubs.Take()) ||
            !S3RE_EXPECT(expectedLogs == logs.Take()) || !worlds.Checkpoint(ctx)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: args %u %d %d %u", i, args[0], static_cast<std::int32_t>(args[1]),
                     static_cast<std::int32_t>(args[2]), args[3]);
            break;
        }
    }
    g_gameTick = savedTick;
}

U TransformKind(test::Context& ctx) {
    static constexpr U kKinds[] = {kKindSwamp, kKindSnow, kKindDesertify, kKindGreenDesert, kKindBurntGround};
    return kKinds[ctx.rng().Below(5)];
}

void PickTransform(test::Context& ctx, U size, U* args) {
    args[0] = TransformKind(ctx);
    args[1] = static_cast<U>(SpellCoordinate(ctx, size));
    args[2] = static_cast<U>(SpellCoordinate(ctx, size));
}

void PickSpell(test::Context& ctx, U size, U* args) {
    args[0] = ctx.rng().Below(4);  // player
    args[1] = static_cast<U>(SpellCoordinate(ctx, size));
    args[2] = static_cast<U>(SpellCoordinate(ctx, size));
    args[3] = ctx.rng().Below(4);  // spell 0..3 (SpellTransformLand)
}

template <TransformFn F>
void CallTransform(CGameData* world, const U* args) {
    F(world, nullptr, args[0], static_cast<std::int32_t>(args[1]), static_cast<std::int32_t>(args[2]));
}

template <TransformLandFn F>
void CallTransformLand(CGameData* world, const U* args) {
    F(world, nullptr, args[0], static_cast<std::int32_t>(args[1]), static_cast<std::int32_t>(args[2]), args[3]);
}

template <LandSpellFn F>
void CallLandSpell(CGameData* world, const U* args) {
    F(world, nullptr, args[0], static_cast<std::int32_t>(args[1]), static_cast<std::int32_t>(args[2]));
}

// The trampolines are variables, so the original side goes through these.
void OriginalTransform(CGameData* world, const U* args) {
    Original_CGameData_TransformTerrainAround(world, nullptr, args[0], static_cast<std::int32_t>(args[1]),
                                              static_cast<std::int32_t>(args[2]));
}
void OriginalTransformLand(CGameData* world, const U* args) {
    Original_CGameData_SpellTransformLand(world, nullptr, args[0], static_cast<std::int32_t>(args[1]),
                                          static_cast<std::int32_t>(args[2]), args[3]);
}
void OriginalCreateStones(CGameData* world, const U* args) {
    Original_CGameData_SpellCreateStones(world, nullptr, args[0], static_cast<std::int32_t>(args[1]),
                                         static_cast<std::int32_t>(args[2]));
}
void OriginalTreeCurse(CGameData* world, const U* args) {
    Original_CGameData_SpellTreeCurse(world, nullptr, args[0], static_cast<std::int32_t>(args[1]),
                                      static_cast<std::int32_t>(args[2]));
}

// --- deliberately wrong versions ------------------------------------------

// Wrong: the swamp chain's middle edge is 0x51 instead of 0x15.
U __fastcall WrongNextTransformTerrain(CGameData* world, void*, const TerrainGrid* grid, U from, U to) {
    if (grid->kind == kKindSwamp && from == 0x10) {
        return 0x51;
    }
    return CGameData_NextTransformTerrain(world, nullptr, grid, from, to);
}

// Wrong: the start's may-change mark in the grid stays.
U __fastcall WrongSpreadTerrainTransform(CGameData* world, void*, TerrainGrid* grid, U gx, U gy) {
    const std::uint8_t mark = grid->can_change[gx][gy];
    CGameData_SpreadTerrainTransform(world, nullptr, grid, gx, gy);
    grid->can_change[gx][gy] = mark;
    return 0;
}

// Wrong: changed tiles don't get the redraw bit.
U __fastcall WrongTransformTerrainAround(CGameData* world, void*, U kind, std::int32_t x, std::int32_t y) {
    std::vector<Tile> before(world->tiles[0], world->tiles[0] + layout::kMapRowTiles * 140);
    const U result = CGameData_TransformTerrainAround(world, nullptr, kind, x, y);
    for (std::size_t n = 0; n < before.size(); ++n) {
        Tile& t = world->tiles[0][n];
        if (t.terrain != before[n].terrain) {
            t.draw_flags = before[n].draw_flags;
        }
    }
    return result;
}

// Wrong: stones come in 10 variants.
U __fastcall WrongSpellCreateStones(CGameData* world, void*, U player, std::int32_t x, std::int32_t y) {
    CGameData_SpellCreateStones(world, nullptr, player, x, y);
    for (U tx = 0; tx < world->map_size; ++tx) {
        for (U ty = 0; ty < world->map_size; ++ty) {
            if (world->tiles[tx][ty].object == kFirstStone + 10) {
                world->tiles[tx][ty].object = kFirstStone + 9;
            }
        }
    }
    return 0;
}

// Wrong: the cursed trees may also stand on worn paths.
U __fastcall WrongSpellTreeCurse(CGameData* world, void*, U player, std::int32_t x, std::int32_t y) {
    for (U tx = 0; tx < world->map_size; ++tx) {
        for (U ty = 0; ty < world->map_size; ++ty) {
            if (world->tiles[tx][ty].terrain == kTerrainPath) {
                world->tiles[tx][ty].terrain = 0x10;
            }
        }
    }
    return CGameData_SpellTreeCurse(world, nullptr, player, x, y);
}

}  // namespace

S3RE_TEST(CGameData_IsTransformableTerrain_MatchesOriginal) {
    CompareTransformable(ctx, &CGameData_IsTransformableTerrain, 40 * 0x101 + 5000);
}
S3RE_TEST(CGameData_NextTransformTerrain_MatchesOriginal) {
    CompareNext(ctx, &CGameData_NextTransformTerrain, 40 * 0x101 + 5000);
}
S3RE_TEST(CGameData_NextTransformTerrain_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareNext(probe, &WrongNextTransformTerrain, 40 * 0x101);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_CanTerrainsBorder_MatchesOriginal) {
    CompareBorder(ctx, &CGameData_CanTerrainsBorder, 0x101 * 0x101 + 5000);
}
S3RE_TEST(CGameData_SpreadTerrainTransform_MatchesOriginal) {
    CompareSpread(ctx, &CGameData_SpreadTerrainTransform, 3000);
}
S3RE_TEST(CGameData_SpreadTerrainTransform_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSpread(probe, &WrongSpreadTerrainTransform, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_TransformTerrainAround_MatchesOriginal) {
    CompareLand(ctx, &OriginalTransform, &CallTransform<CGameData_TransformTerrainAround>, &PickTransform, 300);
}
S3RE_TEST(CGameData_TransformTerrainAround_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareLand(probe, &OriginalTransform, &CallTransform<WrongTransformTerrainAround>, &PickTransform, 300);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_SpellTransformLand_MatchesOriginal) {
    CompareLand(ctx, &OriginalTransformLand, &CallTransformLand<CGameData_SpellTransformLand>, &PickSpell, 200);
}
S3RE_TEST(CGameData_SpellCreateStones_MatchesOriginal) {
    CompareLand(ctx, &OriginalCreateStones, &CallLandSpell<CGameData_SpellCreateStones>, &PickSpell, 200);
}
S3RE_TEST(CGameData_SpellCreateStones_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareLand(probe, &OriginalCreateStones, &CallLandSpell<WrongSpellCreateStones>, &PickSpell, 200);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CGameData_SpellTreeCurse_MatchesOriginal) {
    CompareLand(ctx, &OriginalTreeCurse, &CallLandSpell<CGameData_SpellTreeCurse>, &PickSpell, 200);
}
S3RE_TEST(CGameData_SpellTreeCurse_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareLand(probe, &OriginalTreeCurse, &CallLandSpell<WrongSpellTreeCurse>, &PickSpell, 200);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
