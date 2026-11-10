// Layout of the world object (CGameData, our GameWorld) and its records.
//
// Typed access to the original game's memory: our replacements read and
// write world fields through these structs (world->tiles[x][y].owner,
// world->settlers[n].job, ...) instead of raw offsets. Field names follow
// re/structs.md; "unkXXXX" means the meaning is unknown (XXXX = offset),
// names ending in a question mark in the comments are guesses. Every size
// and offset is checked by a static_assert against the evidence in
// re/world_layout.md, so a wrong edit fails to compile. New field knowledge
// goes here (and into re/structs.md / re/world_layout.md), never into raw
// offsets in .cpp files.
//
// Layout rules:
// - All structs are packed (1-byte alignment) and contain only fixed-width
//   integers, so the layout does not depend on the compiler. The DLL is
//   32-bit, like S3.EXE: pointers and pointer arithmetic wrap at 2^32
//   exactly like the original's address arithmetic (see TileAtOffset).
// - The vtable pointer of CGameData is a uint32_t (a 32-bit pointer in
//   S3.EXE).
// - Record arrays include record 0 (never used: numbers start at 1).
// - Tile records start at world+0x67444 (settled, re/structs.md "Tile"):
//   tile (x, y) is tiles[x][y], a step in x is 0x4800 bytes, in y 0x18.

#pragma once

#include <cstddef>
#include <cstdint>

namespace s3re::game {

// Record types and constants live in the nested namespace `layout`, so they
// don't clash with local helpers; converted files bring in the names they
// use (`using layout::Tile;`). The world itself is s3re::game::GameWorld
// (below), which derives from layout::GameWorldFields.
namespace layout {

#pragma pack(push, 1)

constexpr std::size_t kGoodsTypes = 36;
constexpr std::size_t kSettlerTypes = 48;
constexpr std::size_t kPlayers = 20;
constexpr std::size_t kMapRowTiles = 0x300;

// ---------------------------------------------------------------- Tile
struct Tile {
    std::uint16_t settler;        // 0x00 settler standing here (0 = none)
    std::uint16_t object_number;  // 0x02 building if flags & kTileHasBuilding, else pile
    std::uint8_t height;          // 0x04
    std::uint8_t shade;           // 0x05 computed from heights (0x519670)
    std::uint8_t terrain;         // 0x06 "worldart"
    std::uint8_t object;          // 0x07 tree / stone / other landscape object
    std::uint8_t owner;           // 0x08 player, 0xff = nobody
    std::uint8_t draw_flags;      // 0x09 0x40 border, 0x80 redraw (UI)
    std::uint8_t flags;           // 0x0a see kTile* below
    std::uint8_t search_mark;     // 0x0b search generation (UI/scratch)
    std::uint8_t overlay_value;   // 0x0c overlay drawing value (UI)
    std::uint8_t slice_byte;      // 0x0d cleared in eighths by UpdateEnvironment
    std::uint16_t original_object;  // 0x0e copy of the object byte at map load
    std::uint8_t walk_region;     // 0x10 id from the 256-id table
    std::uint8_t resources;       // 0x11 high nibble kind, low nibble amount
    std::uint8_t traffic;         // 0x12 foot traffic (paths)
    std::uint8_t unused13;        // 0x13 never accessed
    std::uint16_t area;           // 0x14 economy area, 0x8000 + n = water body n
    std::uint16_t wide_region;    // 0x16 id from the 8192-id table (pathfinder?)
};
static_assert(sizeof(Tile) == 0x18);
static_assert(offsetof(Tile, owner) == 0x08);
static_assert(offsetof(Tile, flags) == 0x0a);
static_assert(offsetof(Tile, search_mark) == 0x0b);
static_assert(offsetof(Tile, walk_region) == 0x10);
static_assert(offsetof(Tile, area) == 0x14);
static_assert(offsetof(Tile, wide_region) == 0x16);

constexpr std::uint8_t kTileBlocked = 0x01;
constexpr std::uint8_t kTileFlag02 = 0x02;
constexpr std::uint8_t kTileBuildingFootprint = 0x08;
constexpr std::uint8_t kTileFloodMark = 0x10;
constexpr std::uint8_t kTileHasBuilding = 0x20;
constexpr std::uint8_t kTileOwned = 0x40;
constexpr std::uint8_t kTileFlag80 = 0x80;
constexpr std::uint8_t kTileDrawBorder = 0x40;  // draw_flags
constexpr std::uint8_t kTileDrawRedraw = 0x80;  // draw_flags
constexpr std::uint16_t kTileAreaWater = 0x8000;

// ---------------------------------------------------------------- Area
struct Area {  // "Econ" (economy sector)
    std::uint8_t player;                         // 0x000 0xff = free
    std::uint8_t pad001[3];
    std::uint32_t tile_count;                    // 0x004
    std::uint16_t pile_list;                     // 0x008
    std::uint16_t piles_by_goods[kGoodsTypes];   // 0x00a
    std::uint16_t requests_by_goods[kGoodsTypes];  // 0x052
    std::uint16_t buildings_a[kGoodsTypes];      // 0x09a
    std::uint16_t buildings_b[kGoodsTypes];      // 0x0e2
    std::uint16_t settlers_by_type[kSettlerTypes];  // 0x12a
    std::uint8_t settings1[kGoodsTypes];         // 0x18a
    std::uint8_t settings2[kGoodsTypes];         // 0x1ae
    std::uint8_t settings3[kGoodsTypes];         // 0x1d2
    std::uint8_t settings4[kGoodsTypes];         // 0x1f6
    std::uint8_t settings5[kGoodsTypes];         // 0x21a
    std::uint8_t settings_extra[3];              // 0x23e
    std::uint8_t setting_241;                    // 0x241
    std::uint8_t counters_242[3];                // 0x242 summed on merge
    std::uint8_t condition_enabled[51];          // 0x245
    std::uint16_t counters_278[51];              // 0x278
    std::uint8_t pad2de[2];
};
static_assert(sizeof(Area) == 0x2e0);
static_assert(offsetof(Area, piles_by_goods) == 0x0a);
static_assert(offsetof(Area, requests_by_goods) == 0x52);
static_assert(offsetof(Area, buildings_a) == 0x9a);
static_assert(offsetof(Area, buildings_b) == 0xe2);
static_assert(offsetof(Area, settlers_by_type) == 0x12a);
static_assert(offsetof(Area, settings1) == 0x18a);
static_assert(offsetof(Area, settings3) == 0x1d2);
static_assert(offsetof(Area, settings5) == 0x21a);
static_assert(offsetof(Area, condition_enabled) == 0x245);
static_assert(offsetof(Area, counters_278) == 0x278);

// ---------------------------------------------------------------- Pile
struct Pile {
    std::uint8_t good;      // 0x0 0 = free record, 0xff = building pile without goods
    std::uint8_t amount;    // 0x1
    std::uint8_t leaving;   // 0x2 (building piles: high nibble = units coming)
    std::uint8_t coming;    // 0x3
    std::uint8_t art;       // 0x4 kind: 0xff free-standing, 0xfe/0xfd building piles
    std::uint8_t unused5;   // 0x5
    std::uint16_t x;        // 0x6
    std::uint16_t y;        // 0x8
    std::uint16_t prev;     // 0xa
    std::uint16_t next;     // 0xc
};
static_assert(sizeof(Pile) == 0x0e);

// ---------------------------------------------------------------- Settler
struct Settler {
    std::uint8_t owner;            // 0x00 0xff = free
    std::uint8_t art;              // 0x01 settler type
    std::uint8_t race;             // 0x02
    std::uint8_t health;           // 0x03
    std::uint16_t x;               // 0x04
    std::uint16_t y;               // 0x06
    std::uint32_t direction;       // 0x08 0..5
    std::uint16_t counter;         // 0x0c
    std::uint16_t job;             // 0x0e
    std::uint16_t next;            // 0x10
    std::uint16_t prev;            // 0x12
    std::uint8_t flags;            // 0x14 0x40 = in a building slot
    std::uint8_t slot;             // 0x15 low 3 bits
    std::uint8_t unk16;            // 0x16
    std::uint8_t unk17;            // 0x17
    std::uint8_t unk18;            // 0x18
    std::uint8_t unk19;            // 0x19
    std::uint8_t good;             // 0x1a
    std::uint8_t unk1b;            // 0x1b
    std::uint32_t saved_direction; // 0x1c
    std::uint16_t saved_counter;   // 0x20
    std::uint16_t building;        // 0x22
    std::uint16_t target_x;        // 0x24
    std::uint16_t target_y;        // 0x26
    std::uint16_t waypoint_x;      // 0x28 end of the current straight leg (Pathing::PlanLine)
    std::uint16_t waypoint_y;      // 0x2a
    std::uint8_t path_mode;        // 0x2c see kPath* below; 1 with a new target
    std::uint8_t path_steps;       // 0x2d steps left along an obstacle's edge
    std::uint8_t path_direction;   // 0x2e direction 0..5 of the edge walk / blocked step
    std::uint8_t unk2f;            // 0x2f
    std::uint16_t path_cache[6];   // 0x30 0xffff = empty
    std::uint8_t unk3c;            // 0x3c
    std::uint8_t unused3d[3];      // 0x3d
};
static_assert(sizeof(Settler) == 0x40);
static_assert(offsetof(Settler, job) == 0x0e);
static_assert(offsetof(Settler, saved_direction) == 0x1c);
static_assert(offsetof(Settler, building) == 0x22);
static_assert(offsetof(Settler, path_cache) == 0x30);
static_assert(offsetof(Settler, path_mode) == 0x2c);

// Settler::path_mode: what Pathing::FindPath (0x4a1ee0) does next.
constexpr std::uint8_t kPathPlanLine = 0x01;     // plan a straight leg to the target (0x4a3ab0)
constexpr std::uint8_t kPathWalkLine = 0x02;     // walk towards the waypoint (0x4a22f0)
constexpr std::uint8_t kPathStepOut = 0x04;      // blocked: look for a way around (0x4a2e20)
constexpr std::uint8_t kPathFollowEdge = 0x08;   // walk along the obstacle's edge (0x4a3170)
constexpr std::uint8_t kPathEdgeLeft = 0x10;     // edge walk turns the other way
constexpr std::uint8_t kPathWaiting = 0x20;      // path_direction holds a blocked step

// ---------------------------------------------------------------- Building
struct Building {
    std::uint8_t owner;            // 0x00 0xff = free
    std::uint8_t unk01;            // 0x01
    std::uint16_t x;               // 0x02 position (tile A)
    std::uint16_t y;               // 0x04
    std::uint8_t art;              // 0x06 building type
    union {
        std::uint8_t race;         // 0x07 the owner's race (layout tables, animations)
        std::uint8_t unk07;        // old name, still used by settler_spawn.cpp (TODO.md)
    };
    std::uint16_t unk08;           // 0x08 garrison?
    std::uint16_t door_x;          // 0x0a tile B (gives the area)
    std::uint16_t door_y;          // 0x0c
    std::uint16_t pile_ring;       // 0x0e
    std::uint16_t piles[3];        // 0x10
    std::uint8_t unk16;            // 0x16
    std::uint8_t unk17;            // 0x17
    std::uint8_t request_flags;    // 0x18
    std::uint8_t unk19;            // 0x19
    std::uint8_t unk1a;            // 0x1a
    std::uint8_t unk1b[4];         // 0x1b..0x1e
    std::uint8_t state;            // 0x1f high nibble
    std::uint8_t extra_tiles;      // 0x20 0xfe = taken
    std::uint8_t buildup;          // 0x21 non-zero while under construction
    std::uint16_t unk22;           // 0x22 shipyard direction / soldier
    std::uint16_t input_amount[3]; // 0x24 "word 24"
    std::uint16_t input_next[3];   // 0x2a "word 2A"
    std::uint16_t work_x;          // 0x30
    std::uint16_t work_y;          // 0x32
    std::uint16_t trade;           // 0x34 Trade record number
    std::uint8_t unk36;            // 0x36
    std::uint8_t unk37;            // 0x37
    std::uint16_t unk38;           // 0x38 hit points?
};
static_assert(sizeof(Building) == 0x3a);
static_assert(offsetof(Building, request_flags) == 0x18);
static_assert(offsetof(Building, buildup) == 0x21);
static_assert(offsetof(Building, input_amount) == 0x24);
static_assert(offsetof(Building, input_next) == 0x2a);
static_assert(offsetof(Building, trade) == 0x34);

// ---------------------------------------------------------------- Player
struct Player {
    std::int32_t active;             // 0x000 0 = not playing
    std::int32_t race;               // 0x004
    std::int32_t unk008;             // 0x008 (savegame version 0x89+)
    std::int32_t presence_index;     // 0x00c
    std::int32_t start_x;            // 0x010
    std::int32_t start_y;            // 0x014
    std::int32_t unk018;             // 0x018
    std::int32_t unk01c;             // 0x01c -1 in the constructor
    // 0x020: idle-settler list heads per type; entries 0..4 double as the
    // building request counters (use the accessors below for those).
    std::uint16_t settler_lists[kSettlerTypes];  // 0x020
    std::int32_t unk080;             // 0x080
    std::int32_t unk084;             // 0x084
    std::int32_t unk088;             // 0x088 UI
    std::int32_t mana;               // 0x08c
    std::int32_t unk090;             // 0x090
    std::int32_t spell_mana[8];      // 0x094
    std::int32_t unk0b4[4];          // 0x0b4
    // 0x0c4..0x0d4: recomputed by RecountPlayerPopulation (0x5104c0) in
    // UpdatePhase2; meanings partly guesses (re/structs.md). unk0c4 is the
    // old name, still used by settler_spawn.cpp (TODO.md).
#pragma warning(push)
#pragma warning(disable : 4201)  // nameless struct
    union {
        std::int32_t unk0c4[6];
        struct {
            std::uint32_t population;        // 0x0c4 civilians (settler class 0 or 10..29)
            std::uint32_t population_limit;  // 0x0c8 limit_base + 10/30/100 per finished residence 0x1a/0x1b/0x1c
            std::uint32_t population_limit_base;  // 0x0cc +1 per civilian created by 0x50cb20 (probably)
            std::uint32_t carriers;          // 0x0d0 settlers of type 0
            std::uint32_t surplus_carriers;  // 0x0d4 carriers beyond the limit's share (0 if within)
            std::uint32_t surplus_carriers_marked;  // 0x0d8 carriers with flag 0x80 (UpdateSettlers job 1)
        };
    };
#pragma warning(pop)
    std::uint16_t goods[kGoodsTypes];  // 0x0dc
    std::uint8_t unk124;             // 0x124
    std::uint8_t pad125[3];
    std::int32_t unk128;             // 0x128
    std::int32_t unk12c[3];          // 0x12c
    std::uint8_t unused138[0x10];    // 0x138
};
static_assert(sizeof(Player) == 0x148);
static_assert(offsetof(Player, settler_lists) == 0x20);
static_assert(offsetof(Player, mana) == 0x8c);
static_assert(offsetof(Player, spell_mana) == 0x94);
static_assert(offsetof(Player, population) == 0xc4);
static_assert(offsetof(Player, surplus_carriers_marked) == 0xd8);
static_assert(offsetof(Player, goods) == 0xdc);
static_assert(offsetof(Player, unk124) == 0x124);
// Building request counters: settler_lists[0..4] = +0x20, +0x22, +0x24
// (total), +0x26, +0x28.

// ---------------------------------------------------------------- Trade
struct Trade {  // "Trad"; our "building extra"
    std::uint8_t kind;               // 0x00 0xff = free
    std::uint8_t unk01;              // 0x01
    std::uint16_t route_x[4];        // 0x02 points 1..3 and the end (0xffff = unset)
    std::uint16_t route_y[4];        // 0x0a
    std::uint8_t unk12[0x24];        // 0x12..0x35
    std::uint8_t coming[kGoodsTypes];  // 0x36 units on the way per goods type
};
static_assert(sizeof(Trade) == 0x5a);
static_assert(offsetof(Trade, route_y) == 0x0a);
static_assert(offsetof(Trade, coming) == 0x36);

// ---------------------------------------------------------------- small records
struct Effect {  // table at world+0x36700
    std::uint16_t type;              // 0 = free
    std::uint16_t countdown;
    std::uint16_t params[8];
};
static_assert(sizeof(Effect) == 0x14);

struct Event {  // ring at world+0x32878
    std::int32_t type, a, b, c;
};
static_assert(sizeof(Event) == 0x10);

struct PendingEvent {  // ring at world+0x2f990
    std::int32_t type, a, b;
};
static_assert(sizeof(PendingEvent) == 0x0c);

struct Notification {  // world+0x2d320, local player only
    std::int32_t type, x, y;
};
static_assert(sizeof(Notification) == 0x0c);

struct NotificationTimer {  // world+0x2e5e0
    std::int32_t countdown, x, y;
};
static_assert(sizeof(NotificationTimer) == 0x0c);

struct MapOffset {  // world+0x110a588
    std::int32_t dx, dy, distance, sector;
};
static_assert(sizeof(MapOffset) == 0x10);

struct TilePosition {  // tile rings
    std::uint16_t x, y;
};
static_assert(sizeof(TilePosition) == 4);

struct PresenceBlock {  // 16 x 16 tiles
    std::uint8_t player[kPlayers];
};
static_assert(sizeof(PresenceBlock) == 0x14);

struct PlayerSiteCache {  // world+0x1196024
    std::uint8_t valid;
    std::uint8_t grid[31 * 31];
};
static_assert(sizeof(PlayerSiteCache) == 0x3c2);

// Map effects (our name; probably visual effects such as smoke or spell
// effects): the member object at world+0x14d2bc8 (Volker\LogicInit.cpp,
// constructor 0x5442c0); see map_effects.cpp.
struct MapEffect {  // constructors 0x543830 / 0x543880 (argument a<i>)
    std::uint32_t start_tick;        // 0x00 a1; 0 = free record
    std::int16_t duration;           // 0x04 a2 (ticks, signed in PositionAt)
    std::uint16_t unk06;             // 0x06 a3
    std::int32_t start_x;            // 0x08 a6 (pixels)
    std::int32_t start_y;            // 0x0c a7
    std::int32_t end_x;              // 0x10 a8 (gives the block list)
    std::int32_t end_y;              // 0x14 a9
    std::uint32_t unk18;             // 0x18 a11
    std::uint32_t unk1c;             // 0x1c a12
    std::uint32_t unk20;             // 0x20 a13
    std::uint32_t unk24;             // 0x24 a14
    std::int16_t arc;                // 0x28 a10 hop height (PositionAt)
    std::uint8_t pad2a[2];           // 0x2a never written (copied garbage)
    std::uint32_t unk2c;             // 0x2c a15 graphics id? (-1 default)
    std::uint16_t unk30;             // 0x30 a16 (left alone by the default ctor)
    std::uint16_t unk32;             // 0x32 a17
    std::uint32_t unk34;             // 0x34 a20 (-1 default)
    std::uint8_t unk38;              // 0x38 a18 (0xff default)
    std::uint8_t unk39;              // 0x39 a19
    std::uint8_t unk3a;              // 0x3a a4
    std::uint8_t pad3b;              // 0x3b never written
    std::uint16_t unk3c;             // 0x3c a5
    std::uint8_t pad3e[2];           // 0x3e never written
    std::uint32_t list;              // 0x40 block list (cell) it is linked into
    std::uint32_t previous;          // 0x44 previous slot in that list
    std::uint32_t next;              // 0x48 next slot (0 = end)
};
static_assert(sizeof(MapEffect) == 0x4c);
static_assert(offsetof(MapEffect, duration) == 0x04);
static_assert(offsetof(MapEffect, start_x) == 0x08);
static_assert(offsetof(MapEffect, end_x) == 0x10);
static_assert(offsetof(MapEffect, unk18) == 0x18);
static_assert(offsetof(MapEffect, arc) == 0x28);
static_assert(offsetof(MapEffect, pad2a) == 0x2a);
static_assert(offsetof(MapEffect, unk2c) == 0x2c);
static_assert(offsetof(MapEffect, unk30) == 0x30);
static_assert(offsetof(MapEffect, unk34) == 0x34);
static_assert(offsetof(MapEffect, unk38) == 0x38);
static_assert(offsetof(MapEffect, pad3b) == 0x3b);
static_assert(offsetof(MapEffect, unk3c) == 0x3c);
static_assert(offsetof(MapEffect, pad3e) == 0x3e);
static_assert(offsetof(MapEffect, list) == 0x40);
static_assert(offsetof(MapEffect, previous) == 0x44);
static_assert(offsetof(MapEffect, next) == 0x48);

struct MapEffectTable {  // records 1..4999 (0 unused)
    MapEffect records[5000];         // 0x00000
    std::uint32_t cell_heads[10000]; // 0x5cc60 list head per 256-pixel block (cell)
    std::int32_t slots_used;         // 0x668a0 highest slot handed out so far
    std::int32_t blocks_x;           // 0x668a4 blocks per row
    std::int32_t blocks_y;           // 0x668a8 blocks per column
    std::uint32_t unk668ac;
    std::int32_t block_count_16;     // 0x668b0 blocks_x * blocks_y / 16
    std::int32_t width;              // 0x668b4 map width in pixels + margin
    std::int32_t height;             // 0x668b8 map height in pixels + margin
};
static_assert(sizeof(MapEffectTable) == 0x668bc);
static_assert(offsetof(MapEffectTable, cell_heads) == 0x5cc60);
static_assert(offsetof(MapEffectTable, slots_used) == 0x668a0);
static_assert(offsetof(MapEffectTable, blocks_x) == 0x668a4);
static_assert(offsetof(MapEffectTable, blocks_y) == 0x668a8);
static_assert(offsetof(MapEffectTable, block_count_16) == 0x668b0);
static_assert(offsetof(MapEffectTable, width) == 0x668b4);
static_assert(offsetof(MapEffectTable, height) == 0x668b8);

struct Ring1000Indices {
    std::uint32_t read, write;
};

// ---------------------------------------------------------------- CGameData
struct GameWorldFields {
    std::uint32_t vtable;                                   // 0x0000000 0x658c18
    std::int32_t statistics_kernel[13];                     // 0x0000004
    std::uint8_t statistics[kPlayers][kGoodsTypes][256];    // 0x0000038 ProdStatArray
    std::uint8_t chart[0x200];                              // 0x002d038 (UI)
    std::uint16_t chart_max;                                // 0x002d238
    std::uint8_t pad2d23a[0xa];
    std::int32_t chart_selection;                           // 0x002d244 (UI)
    std::int32_t objective_timeout;                         // 0x002d248
    std::int32_t unk2d24c;
    std::uint8_t counters_disabled;                         // 0x002d250
    std::uint8_t pad2d251[3];
    std::int32_t camera_drawn_x, camera_drawn_y;            // 0x002d254 (UI)
    std::int32_t camera_x, camera_y;                        // 0x002d25c (UI)
    std::int32_t redraw;                                    // 0x002d264 (UI)
    std::int32_t ui_2d268[3];                               // 0x002d268
    std::int32_t unk2d274;
    std::int32_t minimap_ready;                             // 0x002d278
    std::int32_t refresh_ui;                                // 0x002d27c
    std::int32_t draw_flag_2d280;
    std::int32_t ui_selection_mode;                         // 0x002d284
    std::int32_t ui_selection_param1, ui_selection_param2;  // 0x002d288
    std::int32_t ui_2d290[2];                               // 0x002d290..0x002d294
    std::int32_t ui_special_menu;                           // 0x002d298 special menu of a settler group (UpdateGroupMenu)
    std::int32_t ui_2d29c;                                  // 0x002d29c
    std::int32_t ui_2d2a0;                                  // 0x002d2a0
    std::int32_t ui_2d2a4[9];                               // 0x002d2a4..0x002d2c4
    std::int32_t notification_blink;                        // 0x002d2c8 (UI) bits for the draw thread, PrepareDrawData 0x51f110
    std::int32_t ui_2d2cc;                                  // 0x002d2cc
    std::uint32_t map_size;                                 // 0x002d2d0
    std::int32_t local_player;                              // 0x002d2d4
    std::int32_t unk2d2d8;
    std::uint16_t random_state[3];                          // 0x002d2dc
    std::uint8_t pad2d2e2[2];
    std::int32_t last_search_x, last_search_y;              // 0x002d2e4 (volatile)
    std::int32_t unk2d2ec;
    std::int32_t ui_cursor_mode;                            // 0x002d2f0
    std::int32_t environment_2d2f4[3];                      // 0x002d2f4
    std::uint8_t search_generation;                         // 0x002d300
    std::uint8_t draw_call_counter;                         // 0x002d301 (volatile)
    std::uint8_t pad2d302[2];
    std::int32_t unk2d304, unk2d308;                        // 0x002d304 (saved)
    std::int32_t unk2d30c, unk2d310;
    std::int32_t ui_2d314[3];                               // 0x002d314
    Notification notifications[4][100];                     // 0x002d320
    NotificationTimer notification_timers[42][10];          // 0x002e5e0
    PendingEvent pending_events[1000];                      // 0x002f990
    Ring1000Indices pending_event_index;                    // 0x0032870
    Event events[1000];                                     // 0x0032878
    Ring1000Indices event_index;                            // 0x00366f8
    Effect effects[10000];                                  // 0x0036700
    std::uint32_t effects_high_water;                       // 0x0067440
    Tile tiles[kMapRowTiles][kMapRowTiles];                 // 0x0067444 [x][y]
    std::uint32_t presence_max_player;                      // 0x0de7444
    PresenceBlock presence_a[48][48];                       // 0x0de7448
    PresenceBlock presence_b[48][48];                       // 0x0df2848
    TilePosition tile_ring[10000];                          // 0x0dfdc48
    Ring1000Indices tile_ring_index;                        // 0x0e07888
    TilePosition tile_ring2[10000];                         // 0x0e07890
    Ring1000Indices tile_ring2_index;                       // 0x0e114d0
    std::uint32_t walk_region_high_water;                   // 0x0e114d8
    std::uint8_t walk_region_used[256];                     // 0x0e114dc
    std::uint32_t area_high_water;                          // 0x0e115dc
    std::uint32_t ids4096_high_water;                       // 0x0e115e0
    std::uint8_t ids4096_used[4096];                        // 0x0e115e4
    std::uint32_t wide_region_high_water;                   // 0x0e125e4
    std::uint8_t wide_region_used[8192];                    // 0x0e125e8
    Area areas[4096];                                       // 0x0e145e8
    Trade trades[1000];                                     // 0x10f45e8
    std::uint32_t trade_high_water;                         // 0x110a578
    std::uint32_t counter_110a57c;                          // 0x110a57c
    std::uint32_t tile_slice;                               // 0x110a580 0..7
    std::uint32_t counter_110a584;                          // 0x110a584
    MapOffset offsets[20000];                               // 0x110a588
    PresenceBlock territory_scratch[101][101];              // 0x1158788
    std::uint16_t player_lists[kPlayers][10][100];          // 0x118a47c
    std::int32_t player_list_lengths[kPlayers][10];         // 0x11940bc
    Player players[kPlayers];                               // 0x11943dc
    std::int32_t per_player_1195d7c[kPlayers];              // 0x1195d7c
    std::uint8_t new_game_objective[8];                     // 0x1195dcc
    std::uint16_t recent_positions[128][2];                 // 0x1195dd4 {y, x}
    std::int32_t player_races[kPlayers];                    // 0x1195fd4
    PlayerSiteCache site_cache[kPlayers][30];               // 0x1196024
    std::uint8_t footprint_scratch[2][31 * 31];             // 0x1222ed4
    std::uint16_t minimap[0xa900];                          // 0x1223656
    std::uint8_t pad1238856[2];
    std::uint32_t pile_high_water;                          // 0x1238858
    Pile piles[32000];                                      // 0x123885c
    std::uint32_t settler_high_water;                       // 0x12a5e5c
    Settler settlers[32000];                                // 0x12a5e60
    std::uint32_t building_high_water;                      // 0x1499e60
    Building buildings[4000];                               // 0x1499e64
    std::uint32_t table14d28a8_high_water;                  // 0x14d28a4
    std::uint8_t table14d28a8[50][16];                      // 0x14d28a8
    MapEffectTable map_effects;                             // 0x14d2bc8
    std::uint32_t ui_feedback[10];                          // 0x1539484
    std::uint32_t ui_feedback_count;                        // 0x15394ac
    std::uint8_t flag15394b0;                               // 0x15394b0
    std::uint8_t victory_conditions[119];                   // 0x15394b1
    std::int32_t reciprocal[768];                           // 0x1539528 0x10000 / i
};
#pragma pack(pop)

}  // namespace layout

// The world object. All fields are in layout::GameWorldFields; GameWorld
// adds nothing (same size and offsets), it only gives the type the name our
// function signatures use.
struct GameWorld : layout::GameWorldFields {};

static_assert(sizeof(GameWorld) == 0x153a128, "size passed to operator new in OnStart");
static_assert(offsetof(GameWorld, statistics) == 0x38);
static_assert(offsetof(GameWorld, chart) == 0x2d038);
static_assert(offsetof(GameWorld, objective_timeout) == 0x2d248);
static_assert(offsetof(GameWorld, counters_disabled) == 0x2d250);
static_assert(offsetof(GameWorld, camera_x) == 0x2d25c);
static_assert(offsetof(GameWorld, redraw) == 0x2d264);
static_assert(offsetof(GameWorld, minimap_ready) == 0x2d278);
static_assert(offsetof(GameWorld, ui_selection_mode) == 0x2d284);
static_assert(offsetof(GameWorld, ui_special_menu) == 0x2d298);
static_assert(offsetof(GameWorld, ui_2d29c) == 0x2d29c);
static_assert(offsetof(GameWorld, ui_2d2a4) == 0x2d2a4);
static_assert(offsetof(GameWorld, notification_blink) == 0x2d2c8);
static_assert(offsetof(GameWorld, map_size) == 0x2d2d0);
static_assert(offsetof(GameWorld, local_player) == 0x2d2d4);
static_assert(offsetof(GameWorld, random_state) == 0x2d2dc);
static_assert(offsetof(GameWorld, last_search_x) == 0x2d2e4);
static_assert(offsetof(GameWorld, ui_cursor_mode) == 0x2d2f0);
static_assert(offsetof(GameWorld, search_generation) == 0x2d300);
static_assert(offsetof(GameWorld, unk2d304) == 0x2d304);
static_assert(offsetof(GameWorld, ui_2d314) == 0x2d314);
static_assert(offsetof(GameWorld, notifications) == 0x2d320);
static_assert(offsetof(GameWorld, notification_timers) == 0x2e5e0);
static_assert(offsetof(GameWorld, pending_events) == 0x2f990);
static_assert(offsetof(GameWorld, pending_event_index) == 0x32870);
static_assert(offsetof(GameWorld, events) == 0x32878);
static_assert(offsetof(GameWorld, event_index) == 0x366f8);
static_assert(offsetof(GameWorld, effects) == 0x36700);
static_assert(offsetof(GameWorld, effects_high_water) == 0x67440);
static_assert(offsetof(GameWorld, tiles) == 0x67444);
static_assert(offsetof(GameWorld, presence_max_player) == 0xde7444);
static_assert(offsetof(GameWorld, presence_a) == 0xde7448);
static_assert(offsetof(GameWorld, presence_b) == 0xdf2848);
static_assert(offsetof(GameWorld, tile_ring) == 0xdfdc48);
static_assert(offsetof(GameWorld, tile_ring_index) == 0xe07888);
static_assert(offsetof(GameWorld, tile_ring2) == 0xe07890);
static_assert(offsetof(GameWorld, walk_region_high_water) == 0xe114d8);
static_assert(offsetof(GameWorld, walk_region_used) == 0xe114dc);
static_assert(offsetof(GameWorld, area_high_water) == 0xe115dc);
static_assert(offsetof(GameWorld, ids4096_used) == 0xe115e4);
static_assert(offsetof(GameWorld, wide_region_used) == 0xe125e8);
static_assert(offsetof(GameWorld, areas) == 0xe145e8);
static_assert(offsetof(GameWorld, trades) == 0x10f45e8);
static_assert(offsetof(GameWorld, trade_high_water) == 0x110a578);
static_assert(offsetof(GameWorld, tile_slice) == 0x110a580);
static_assert(offsetof(GameWorld, offsets) == 0x110a588);
static_assert(offsetof(GameWorld, territory_scratch) == 0x1158788);
static_assert(offsetof(GameWorld, player_lists) == 0x118a47c);
static_assert(offsetof(GameWorld, player_list_lengths) == 0x11940bc);
static_assert(offsetof(GameWorld, players) == 0x11943dc);
static_assert(offsetof(GameWorld, new_game_objective) == 0x1195dcc);
static_assert(offsetof(GameWorld, recent_positions) == 0x1195dd4);
static_assert(offsetof(GameWorld, player_races) == 0x1195fd4);
static_assert(offsetof(GameWorld, site_cache) == 0x1196024);
static_assert(offsetof(GameWorld, footprint_scratch) == 0x1222ed4);
static_assert(offsetof(GameWorld, minimap) == 0x1223656);
static_assert(offsetof(GameWorld, pile_high_water) == 0x1238858);
static_assert(offsetof(GameWorld, piles) == 0x123885c);
static_assert(offsetof(GameWorld, settler_high_water) == 0x12a5e5c);
static_assert(offsetof(GameWorld, settlers) == 0x12a5e60);
static_assert(offsetof(GameWorld, building_high_water) == 0x1499e60);
static_assert(offsetof(GameWorld, buildings) == 0x1499e64);
static_assert(offsetof(GameWorld, table14d28a8) == 0x14d28a8);
static_assert(offsetof(GameWorld, map_effects) == 0x14d2bc8);
static_assert(offsetof(GameWorld, ui_feedback) == 0x1539484);
static_assert(offsetof(GameWorld, ui_feedback_count) == 0x15394ac);
static_assert(offsetof(GameWorld, victory_conditions) == 0x15394b1);
static_assert(offsetof(GameWorld, reciprocal) == 0x1539528);

// ---------------------------------------------------------------- tile helpers
// Tile (x, y) is world->tiles[x][y]. Some original functions instead carry a
// tile as a 32-bit byte offset from the world (0x67444 + (x * 0x300 + y) *
// 24) and step it by +-0x4800 / +-0x18, reading tiles before or after the
// map (other world fields) near the edges; the arithmetic wraps at 2^32.
// These helpers keep that arithmetic explicit and identical.
constexpr std::uint32_t kTileBytes = sizeof(layout::Tile);                   // y + 1
constexpr std::uint32_t kTileRowBytes = layout::kMapRowTiles * sizeof(layout::Tile);  // x + 1

// Byte offset of tile (x, y) from the start of the world, computed in 32
// bits like the original (wraps for out-of-range coordinates).
inline std::uint32_t TileOffset(std::uint32_t x, std::uint32_t y) {
    return static_cast<std::uint32_t>(offsetof(GameWorld, tiles)) + (x * layout::kMapRowTiles + y) * kTileBytes;
}

// The tile record at a 32-bit byte offset from the world (TileOffset plus
// steps). The offset need not be a real tile of the map: the original reads
// whatever lies there.
inline layout::Tile& TileAtOffset(GameWorld* world, std::uint32_t offset) {
    return *reinterpret_cast<layout::Tile*>(reinterpret_cast<std::uint8_t*>(world) + offset);
}

namespace layout {

// Record numbers start at 1; these are the allocators' limits.
constexpr std::uint32_t kMaxArea = 0xffe;        // AllocArea
constexpr std::uint32_t kMaxSettler = 0x7cfe;    // CreateSettler
constexpr std::uint32_t kMaxTrade = 998;         // AllocBuildingExtra
constexpr std::uint32_t kMaxEffect = 9998;       // AllocTable36700Record

}  // namespace layout

}  // namespace s3re::game
