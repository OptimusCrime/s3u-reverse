// Proposed layout of the world object (CGameData) and its records.
//
// NOT part of the build yet: a reference for adopting typed access instead
// of raw offsets (world.h's WorldField). Field names follow re/structs.md;
// "unkXX" means the meaning is unknown, names ending in a question mark in
// the comments are guesses. Every size and offset is checked by a
// static_assert against the evidence in re/world_layout.md, so a wrong
// edit fails to compile.
//
// Layout rules:
// - All structs are packed (1-byte alignment) and contain only fixed-width
//   integers, so the layout does not depend on the compiler or on 32/64-bit.
// - The vtable pointer of CGameData is a uint32_t (a 32-bit pointer in
//   S3.EXE).
// - Record arrays include record 0 (never used: numbers start at 1).

#pragma once

#include <cstddef>
#include <cstdint>

namespace s3re::game::layout {

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
    std::uint16_t x2;              // 0x28 a second position?
    std::uint16_t y2;              // 0x2a
    std::uint8_t new_target;       // 0x2c
    std::uint8_t unk2d;            // 0x2d pathing
    std::uint16_t unk2e;           // 0x2e pathing
    std::uint16_t path_cache[6];   // 0x30 0xffff = empty
    std::uint8_t unk3c;            // 0x3c
    std::uint8_t unused3d[3];      // 0x3d
};
static_assert(sizeof(Settler) == 0x40);
static_assert(offsetof(Settler, job) == 0x0e);
static_assert(offsetof(Settler, saved_direction) == 0x1c);
static_assert(offsetof(Settler, building) == 0x22);
static_assert(offsetof(Settler, path_cache) == 0x30);

// ---------------------------------------------------------------- Building
struct Building {
    std::uint8_t owner;            // 0x00 0xff = free
    std::uint8_t unk01;            // 0x01
    std::uint16_t x;               // 0x02 position (tile A)
    std::uint16_t y;               // 0x04
    std::uint8_t art;              // 0x06 building type
    std::uint8_t unk07;            // 0x07
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
    std::int32_t unk0c4[6];          // 0x0c4
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

struct DrawRecord4C {  // member at world+0x14d2bc8, constructor 0x543830
    std::uint8_t bytes[0x4c];
};

struct DrawCells {  // member at world+0x14d2bc8 (Volker\LogicInit.cpp)
    DrawRecord4C records[5000];      // 0x00000
    std::uint32_t cells[10000];      // 0x5cc60
    std::uint32_t unk668a0;
    std::uint32_t cells_x;           // 0x668a4
    std::uint32_t cells_y;           // 0x668a8
    std::uint32_t unk668ac;
    std::uint32_t cell_count;        // 0x668b0
    std::uint32_t width;             // 0x668b4
    std::uint32_t height;            // 0x668b8
};
static_assert(sizeof(DrawCells) == 0x668bc);
static_assert(offsetof(DrawCells, cells) == 0x5cc60);

struct Ring1000Indices {
    std::uint32_t read, write;
};

// ---------------------------------------------------------------- CGameData
struct GameData {
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
    std::int32_t ui_2d290[5];                               // 0x002d290..0x002d2a0
    std::int32_t ui_2d2a4[11];                              // 0x002d2a4..0x002d2cc
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
    DrawCells draw_cells;                                   // 0x14d2bc8
    std::uint32_t ui_feedback[10];                          // 0x1539484
    std::uint32_t ui_feedback_count;                        // 0x15394ac
    std::uint8_t flag15394b0;                               // 0x15394b0
    std::uint8_t victory_conditions[119];                   // 0x15394b1
    std::int32_t reciprocal[768];                           // 0x1539528 0x10000 / i
};
#pragma pack(pop)

static_assert(sizeof(GameData) == 0x153a128, "size passed to operator new in OnStart");
static_assert(offsetof(GameData, statistics) == 0x38);
static_assert(offsetof(GameData, chart) == 0x2d038);
static_assert(offsetof(GameData, objective_timeout) == 0x2d248);
static_assert(offsetof(GameData, counters_disabled) == 0x2d250);
static_assert(offsetof(GameData, camera_x) == 0x2d25c);
static_assert(offsetof(GameData, redraw) == 0x2d264);
static_assert(offsetof(GameData, minimap_ready) == 0x2d278);
static_assert(offsetof(GameData, ui_selection_mode) == 0x2d284);
static_assert(offsetof(GameData, ui_2d2a4) == 0x2d2a4);
static_assert(offsetof(GameData, map_size) == 0x2d2d0);
static_assert(offsetof(GameData, local_player) == 0x2d2d4);
static_assert(offsetof(GameData, random_state) == 0x2d2dc);
static_assert(offsetof(GameData, last_search_x) == 0x2d2e4);
static_assert(offsetof(GameData, ui_cursor_mode) == 0x2d2f0);
static_assert(offsetof(GameData, search_generation) == 0x2d300);
static_assert(offsetof(GameData, unk2d304) == 0x2d304);
static_assert(offsetof(GameData, ui_2d314) == 0x2d314);
static_assert(offsetof(GameData, notifications) == 0x2d320);
static_assert(offsetof(GameData, notification_timers) == 0x2e5e0);
static_assert(offsetof(GameData, pending_events) == 0x2f990);
static_assert(offsetof(GameData, pending_event_index) == 0x32870);
static_assert(offsetof(GameData, events) == 0x32878);
static_assert(offsetof(GameData, event_index) == 0x366f8);
static_assert(offsetof(GameData, effects) == 0x36700);
static_assert(offsetof(GameData, effects_high_water) == 0x67440);
static_assert(offsetof(GameData, tiles) == 0x67444);
static_assert(offsetof(GameData, presence_max_player) == 0xde7444);
static_assert(offsetof(GameData, presence_a) == 0xde7448);
static_assert(offsetof(GameData, presence_b) == 0xdf2848);
static_assert(offsetof(GameData, tile_ring) == 0xdfdc48);
static_assert(offsetof(GameData, tile_ring_index) == 0xe07888);
static_assert(offsetof(GameData, tile_ring2) == 0xe07890);
static_assert(offsetof(GameData, walk_region_high_water) == 0xe114d8);
static_assert(offsetof(GameData, walk_region_used) == 0xe114dc);
static_assert(offsetof(GameData, area_high_water) == 0xe115dc);
static_assert(offsetof(GameData, ids4096_used) == 0xe115e4);
static_assert(offsetof(GameData, wide_region_used) == 0xe125e8);
static_assert(offsetof(GameData, areas) == 0xe145e8);
static_assert(offsetof(GameData, trades) == 0x10f45e8);
static_assert(offsetof(GameData, trade_high_water) == 0x110a578);
static_assert(offsetof(GameData, tile_slice) == 0x110a580);
static_assert(offsetof(GameData, offsets) == 0x110a588);
static_assert(offsetof(GameData, territory_scratch) == 0x1158788);
static_assert(offsetof(GameData, player_lists) == 0x118a47c);
static_assert(offsetof(GameData, player_list_lengths) == 0x11940bc);
static_assert(offsetof(GameData, players) == 0x11943dc);
static_assert(offsetof(GameData, new_game_objective) == 0x1195dcc);
static_assert(offsetof(GameData, recent_positions) == 0x1195dd4);
static_assert(offsetof(GameData, player_races) == 0x1195fd4);
static_assert(offsetof(GameData, site_cache) == 0x1196024);
static_assert(offsetof(GameData, footprint_scratch) == 0x1222ed4);
static_assert(offsetof(GameData, minimap) == 0x1223656);
static_assert(offsetof(GameData, pile_high_water) == 0x1238858);
static_assert(offsetof(GameData, piles) == 0x123885c);
static_assert(offsetof(GameData, settler_high_water) == 0x12a5e5c);
static_assert(offsetof(GameData, settlers) == 0x12a5e60);
static_assert(offsetof(GameData, building_high_water) == 0x1499e60);
static_assert(offsetof(GameData, buildings) == 0x1499e64);
static_assert(offsetof(GameData, table14d28a8) == 0x14d28a8);
static_assert(offsetof(GameData, draw_cells) == 0x14d2bc8);
static_assert(offsetof(GameData, ui_feedback) == 0x1539484);
static_assert(offsetof(GameData, ui_feedback_count) == 0x15394ac);
static_assert(offsetof(GameData, victory_conditions) == 0x15394b1);
static_assert(offsetof(GameData, reciprocal) == 0x1539528);

// Record numbers start at 1; these are the allocators' limits.
constexpr std::uint32_t kMaxArea = 0xffe;        // AllocArea
constexpr std::uint32_t kMaxSettler = 0x7cfe;    // CreateSettler
constexpr std::uint32_t kMaxTrade = 998;         // AllocBuildingExtra
constexpr std::uint32_t kMaxEffect = 9998;       // AllocTable36700Record

}  // namespace s3re::game::layout
