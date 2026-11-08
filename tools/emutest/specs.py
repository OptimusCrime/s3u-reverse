"""What tools/emutest/run.py tests: one Spec per replaced function.

A Spec names the original function (address in S3.EXE) and ours (the C++
function name in s3re.dll), the calling convention, how to generate each
stack argument, which bits of EAX must match, and optionally world fields
to pin per case (`presets`, e.g. a small map size so map-wide loops stay
fast in the emulator) and S3.EXE globals to set per case (`globals`). Everything else in the world is lazily random (see
emu.py).

Argument generators take (rng, pool); `pool` holds small numbers and the
constants found in the original function's code.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

Generator = Callable[..., int]


def anything(rng, pool) -> int:
    """Mostly pool values, sometimes random 32-bit values."""
    if rng.random() < 0.6:
        return rng.choice(pool)
    return rng.getrandbits(32)


def below(n: int, edge_chance: float = 0.0) -> Generator:
    """0..n-1, sometimes (edge_chance) anything."""
    def generate(rng, pool):
        if rng.random() < edge_chance:
            return anything(rng, pool)
        return rng.randrange(n)
    return generate


def between(low: int, high: int) -> Generator:
    return lambda rng, pool: rng.randint(low, high) & 0xFFFFFFFF


def one_of(*values: int) -> Generator:
    return lambda rng, pool: rng.choice(values)


# Unused space at the end of the emulated world (32 MB; the game's world is
# about 22 MB) for objects other than the world: a fake CLogicThread (see
# the "logicthread" convention) and scratch buffers that pointer arguments
# point into (lazily random and compared like the world).
LOGIC_THREAD = 0x1f80000
SCRATCH = 0x1f90000
SCRATCH_SLOT_BYTES = 0x1000
WORLD_BASE = 0x40000000  # emu.WORLD_BASE


def scratch(slot: int) -> Generator:
    """A pointer to scratch buffer `slot` (world offset SCRATCH + slot * 0x1000)."""
    return lambda rng, pool: WORLD_BASE + SCRATCH + slot * SCRATCH_SLOT_BYTES


@dataclass
class Spec:
    ours: str            # C++ name of our replacement in s3re.dll
    original: int        # address in S3.EXE
    # "thiscall" (ECX = world; ours is __fastcall), "logicthread" (ECX = a
    # fake CLogicThread whose +0x3a0 points at the world) or "stdcall"
    convention: str
    args: list[Generator]
    eax_mask: int = 0xFFFFFFFF  # 0: EAX not compared (callers don't use it)
    # (rng, pool, args) -> [(world offset, value, size)]: fields pinned for the case.
    presets: Callable | None = None
    # Upper limit on cases (for functions that are slow to emulate).
    max_cases: int | None = None
    # (rng, pool, args) -> [(absolute address in S3.EXE, value, size)]: globals
    # set for the case (otherwise they keep their values from the image).
    globals: Callable | None = None


MAP_SIZE = 0x2d2d0
SEARCH_GENERATION = 0x2d300
BUILDING_COUNT = 0x1499e60
SETTLER_COUNT = 0x12a5e5c
PLAYER_RECORDS = 0x11943dc
PLAYER_RECORD_BYTES = 0x148


def map_size(size_generator: Generator) -> Callable:
    return lambda rng, pool, args: [(MAP_SIZE, size_generator(rng, pool), 4)]


def full_map(rng, pool, args):
    return [(MAP_SIZE, 384, 4)]


def small_map_and_generation(rng, pool, args):
    return [(MAP_SIZE, rng.randint(-2, 12) & 0xFFFFFFFF, 4),
            (SEARCH_GENERATION, 0xFF if rng.random() < 0.5 else rng.randrange(256), 1)]


def record_counts(rng, pool, args):
    return [(BUILDING_COUNT, rng.randrange(40), 4), (SETTLER_COUNT, rng.randrange(40), 4)]


def one_active_player(rng, pool, args):
    """ShiftStatistics moves 9,180 bytes per active player; keep it to one or
    two so the emulator stays fast."""
    active = {rng.randrange(20), rng.randrange(20)}
    flags = []
    for player in range(20):
        value = 0
        if player in active:
            value = rng.choice([1, 0x100, rng.getrandbits(32) | 1])
        flags.append((PLAYER_RECORDS + player * PLAYER_RECORD_BYTES, value, 4))
    return flags


BUILDINGS = 0x1499e64
BUILDING_BYTES = 0x3a
REQUEST_COUNTERS = (0x20, 0x22, 0x24, 0x26, 0x28)  # in the player record


def building_request_state(rng, pool, args):
    """For the building in args[0]: a real player, often flag 0x1, byte +0x21
    often 1, and that player's request counters near the limits (count < 10,
    per-bit counters around 0x80)."""
    building = BUILDINGS + args[0] * BUILDING_BYTES
    player = rng.randrange(20)
    record = PLAYER_RECORDS + player * PLAYER_RECORD_BYTES
    fields = [(building + 0x0, player, 1),
              (building + 0x18, rng.getrandbits(8) | (1 if rng.random() < 0.7 else 0), 1),
              (building + 0x21, 1 if rng.random() < 0.5 else rng.randrange(256), 1)]
    for offset in REQUEST_COUNTERS:
        if offset == 0x24:
            value = rng.randrange(12) if rng.random() < 0.8 else rng.getrandbits(16)
        else:
            value = rng.choice([0x80, 0x81, 0x82, 0x7f, 0, 1]) if rng.random() < 0.7                 else rng.getrandbits(16)
        fields.append((record + offset, value, 2))
    return fields


UI_FIELDS = {"GameWorld_IsUiSelectionMode": 0x2d284, "GameWorld_IsUiField298": 0x2d298,
             "GameWorld_IsUiSelectionParam1": 0x2d288, "GameWorld_IsUiSelectionParam2": 0x2d28c}


def field_equals_arg_half_the_time(offset: int) -> Callable:
    def presets(rng, pool, args):
        return [(offset, args[0] if rng.random() < 0.5 else anything(rng, pool), 4)]
    return presets


def mode4_field298(rng, pool, args):
    return [(0x2d284, 4 if rng.random() < 0.6 else anything(rng, pool), 4),
            (0x2d298, 2 if rng.random() < 0.6 else anything(rng, pool), 4)]


SCREEN_WIDTH = 0x730f18
SCREEN_HEIGHT = 0x730f1c
GAME_OUTCOME = 0x730f64


def screen_size(rng, pool, args):
    def size(high):
        return rng.randint(0, high) if rng.random() < 0.5 else rng.getrandbits(32)
    return [(SCREEN_WIDTH, size(2048), 4), (SCREEN_HEIGHT, size(1536), 4)]


def game_outcome(rng, pool, args):
    return [(GAME_OUTCOME, rng.choice([1, 0, 0xFFFFFFFF, 2, rng.getrandbits(32)]), 4)]


def ui_feedback_count(rng, pool, args):
    return [(0x15394ac, rng.randrange(11) if rng.random() < 0.8 else 10, 4)]


def id_table(mark: int, table: int, size: int) -> Callable:
    """Ids 1..firstFree-1 in use, firstFree free (often near or past the end
    of the table), the high-water mark near it or random."""
    def presets(rng, pool, args):
        r = rng.random()
        if r < 0.25:
            first_free = size - 1 - rng.randrange(3)
        elif r < 0.4:
            first_free = size + rng.randrange(3)  # none free in range
        else:
            first_free = 1 + rng.randrange(min(size, 300))
        fields = [(table + i, rng.randrange(1, 256), 1)
                  for i in range(1, min(first_free, size + 2))]
        fields.append((table + first_free, 0, 1))
        if rng.random() < 0.6:
            value = (first_free + rng.randint(-2, 2)) & 0xFFFFFFFF
        else:
            value = rng.getrandbits(32)
        fields.append((mark, value, 4))
        return fields
    return presets


AREA_LIMIT = 0x800  # keeps the area records inside the emulated world


def tile_area_offset(x: int, y: int) -> int:
    return ((x * 0x300 + y + 0x44d9) * 24) & 0xFFFFFFFF


def tile_area_of_args(rng, pool, args):
    return [(tile_area_offset(args[0], args[1]), rng.randrange(AREA_LIMIT), 2)]


def building_on_map(rng, pool, args):
    building = BUILDINGS + args[0] * BUILDING_BYTES
    x, y = rng.randrange(384), rng.randrange(384)
    return [(building + 0xa, x, 2), (building + 0xc, y, 2),
            (tile_area_offset(x, y), rng.randrange(AREA_LIMIT), 2)]


def diagonal_map_size(rng, pool, args):
    """Never 0 (a 2^32-iteration loop in both); 1..3 often (edge cases)."""
    size = rng.randint(1, 3) if rng.random() < 0.3 else rng.randint(1, 400)
    return [(MAP_SIZE, size, 4)]


def slice_map_size(rng, pool, args):
    """Mostly small maps: a slice clears size * size / 8 bytes."""
    r = rng.random()
    size = rng.randint(1, 3) if r < 0.2 else rng.randint(1, 48) if r < 0.9 else rng.randint(1, 400)
    return [(MAP_SIZE, size, 4)]


# --- batch 9 ----------------------------------------------------------------

TILES = 0x67444  # tile records (building_tiles.cpp)
SETTLERS = 0x12a5e60
SETTLER_BYTES = 0x40
PILES = 0x123885c
PILE_BYTES = 14
PILE_COUNT = 0x1238858


def tile(x: int, y: int) -> int:
    return (TILES + (x * 0x300 + y) * 24) & 0xFFFFFFFF


def building_tile_fields(rng, building: int):
    record = BUILDINGS + building * BUILDING_BYTES
    fields = []
    for field in (0x2, 0xa):
        fields += [(record + field, rng.randrange(384), 2),
                   (record + field + 2, rng.randrange(384), 2)]
    return fields


def building_arg_on_map(rng, pool, args):
    return building_tile_fields(rng, args[0])


def settler_in_building(rng, pool, args):
    settler = SETTLERS + args[0] * SETTLER_BYTES
    building = rng.randrange(2001)
    return [(settler + 0x4, rng.randrange(384), 2), (settler + 0x6, rng.randrange(384), 2),
            (settler + 0x22, building, 2)] + building_tile_fields(rng, building)


def building_pile_slots(rng, pool, args):
    record = BUILDINGS + args[0] * BUILDING_BYTES
    fields = []
    for slot in range(3):
        pile = 0 if rng.random() < 0.3 else rng.randrange(1, 0x10000)
        fields.append((record + 0x10 + slot * 2, pile, 2))
        fields.append((PILES + pile * PILE_BYTES + 1,
                       0 if rng.random() < 0.4 else rng.randrange(256), 1))
    return fields


PILE_KINDS = (0, 7, 8, 9, 0xfd, 0xfe, 0xff)


def pile_fields(rng, pile: int):
    record = PILES + pile * PILE_BYTES
    x, y = rng.randrange(384), rng.randrange(384)
    return [(record + 0x0, rng.choice([0xff, rng.randrange(36), rng.randrange(256)]), 1),
            (record + 0x4, rng.choice(PILE_KINDS), 1),
            (record + 0x6, x, 2), (record + 0x8, y, 2),
            (tile(x, y) + 0x8, rng.choice([0xff, rng.randrange(20), rng.randrange(256)]), 1)]


def goods_piles(rng, pool, args):
    count = rng.choice([0, 1, 2, rng.randrange(40)])
    fields = [(PILE_COUNT, count, 4)]
    for pile in range(1, count + 1):
        fields += pile_fields(rng, pile)
    return fields


NEIGHBOUR_OFFSETS = 0x110a588


def pile_neighbourhood(rng, pool, args):
    """Pile on the map, small offsets; most neighbour tiles without a
    building (so that later offsets and the 'none' result are reached)."""
    record = PILES + args[0] * PILE_BYTES
    x, y = rng.randrange(384), rng.randrange(384)
    fields = [(record + 0x6, x, 2), (record + 0x8, y, 2)]
    no_building = rng.random() < 0.3
    for k in range(19):
        dx, dy = rng.randint(-3, 3), rng.randint(-3, 3)
        fields += [(NEIGHBOUR_OFFSETS + k * 16, dx & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + k * 16 + 4, dy & 0xFFFFFFFF, 4)]
        t = tile((x + dx) & 0xFFFFFFFF, (y + dy) & 0xFFFFFFFF)
        r = rng.random()
        if no_building or r < 0.4:
            fields.append((t + 0xa, rng.getrandbits(8) & ~0x20, 1))
        elif r < 0.6:
            fields.append((t + 0x2, 0, 2))
    return fields


def alloc_table(table: int, stride: int, limit: int, high_water: int, size: int,
                free_value: int) -> Callable:
    """Records 1..first_free-1 in use, first_free free (often at or past the
    limit), the high-water mark near it or random."""
    def presets(rng, pool, args):
        r = rng.random()
        if r < 0.25:
            first_free = limit + 1 - rng.randrange(3)
        elif r < 0.4:
            first_free = limit + 2  # none free
        else:
            first_free = 1 + rng.randrange(300)
        fields = []
        for record in range(1, min(first_free, limit + 2)):
            value = rng.randrange(1, 1 << (8 * size))
            if value == free_value:
                value ^= 1
            fields.append((table + record * stride, value, size))
        if first_free <= limit + 1:
            fields.append((table + first_free * stride, free_value, size))
        fields.append((high_water, (first_free + rng.randint(-2, 2)) & 0xFFFFFFFF
                       if rng.random() < 0.6 else rng.getrandbits(32), 4))
        return fields
    return presets


PENDING_RING = 0x2f990


def event_type(rng, pool):
    return 6 if rng.random() < 0.6 else anything(rng, pool)


def event_rings(rng, pool, args):
    read = rng.choice([rng.randrange(1000), 999, 995])
    length = rng.choice([0, 1, rng.randrange(12)])
    write = (read + length) % 1000
    fields = [(0x32870, read, 4), (0x32874, write, 4),
              (0x366fc, rng.choice([999, rng.randrange(1000)]), 4)]
    for i in range(length):
        entry = PENDING_RING + ((read + i) % 1000) * 12
        fields += [(entry, 6 if rng.random() < 0.5 else rng.randrange(10), 4),
                   (entry + 4, rng.randrange(4), 4)]
    return fields


def marked_tile(rng, pool, args):
    return [(tile(args[0], args[1]) + 0x8, rng.randrange(4), 1),
            (0xe0788c, rng.choice([9999, rng.randrange(10000)]), 4)]


def replace_tile(offset, size):
    """Tile field at `offset` often equal to args[2]; ring index near the end."""
    def presets(rng, pool, args):
        value = args[2] if rng.random() < 0.7 else rng.randrange(4)
        return [(tile(args[0], args[1]) + offset, value & ((1 << (8 * size)) - 1), size),
                (0xe0788c, rng.choice([9999, rng.randrange(10000)]), 4)]
    return presets


def uniform_terrain(rng, pool, args):
    """The tile and its six neighbours mostly share args[2] as terrain."""
    fields = []
    for dx, dy in [(0, 0), (1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1)]:
        value = args[2] & 0xFF if rng.random() < 0.9 else rng.randrange(256)
        fields.append((tile(args[0] + dx, args[1] + dy) + 0x6, value, 1))
    return fields


def offset_tiles(rng, pool, args):
    """Small offsets in the table at 0x110a588; target tiles mostly clear."""
    fields = []
    for k in range(20):
        dx, dy = rng.randint(-3, 3), rng.randint(-3, 3)
        fields += [(NEIGHBOUR_OFFSETS + k * 16, dx & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + k * 16 + 4, dy & 0xFFFFFFFF, 4)]
        t = tile(args[0] + dx, args[1] + dy)
        if rng.random() < 0.95:
            fields += [(t + 0x7, 0, 1), (t + 0xa, rng.getrandbits(8) & 0xFE, 1)]
    return fields


def marked_tile_terrain(rng, pool, args):
    return [(tile(args[0], args[1]) + 0x6, rng.choice([4, 5, 6, 7, 8, rng.randrange(256)]), 1),
            (tile(args[0], args[1]) + 0xa, rng.getrandbits(8) | (0x10 if rng.random() < 0.7 else 0), 1),
            (0xe0788c, rng.choice([9999, rng.randrange(10000)]), 4)]


def camera(rng, pool, args):
    def value():
        return rng.choice([rng.randint(-5, 5), rng.randint(0, 10000), rng.getrandbits(32)])
    return [(0x2d25c, value() & 0xFFFFFFFF, 4), (0x2d260, value() & 0xFFFFFFFF, 4),
            (MAP_SIZE, rng.choice([0, 64, 384, rng.getrandbits(32)]), 4)]


def timers(rng, pool, args):
    return [(0x2e5e0 + entry * 12, rng.choice([0, 1, 1, 2, 3, rng.getrandbits(32)]), 4)
            for entry in range(420)]


RECENT_POSITIONS = 0x1195dd4


def recent_positions(rng, pool, args):
    fields = []
    for slot in range(128):
        empty = rng.random() < 0.05
        fields += [(RECENT_POSITIONS + slot * 4, 0 if empty else rng.randrange(8), 2),
                   (RECENT_POSITIONS + slot * 4 + 2, 0 if empty else rng.randrange(8), 2)]
    fields.append((RECENT_POSITIONS + rng.randrange(128) * 4, 0, 4))  # never full
    return fields


def recent_positions_cursor(rng, pool, args):
    return [(0x7ace60, rng.choice([0, 1, 127, rng.randrange(128)]), 2)]


def players_field0c(rng, pool, args):
    fields = []
    for player in range(20):
        record = PLAYER_RECORDS + player * PLAYER_RECORD_BYTES
        fields += [(record + 0x0, 0 if rng.random() < 0.25 else rng.getrandbits(32) | 1, 4),
                   (record + 0x18, rng.getrandbits(32) | 1 if rng.random() < 0.3 else 0, 4),
                   (record + 0xc, rng.randrange(6), 4)]
    return fields


def building_type(rng, pool):
    return rng.randint(0x14, 0x24) if rng.random() < 0.8 else anything(rng, pool)


# --- batch 12 ---------------------------------------------------------------

BIG_FLOOD = 101  # a 101 x 101 map: a fill of all of it wraps the 10,000-entry ring


def flood_map(rng, args, big_chance=0.02):
    """A map size that holds the start tile (args[0], args[1]); sometimes the
    start is on the last row/column, rarely the big map."""
    if rng.random() < big_chance:
        return BIG_FLOOD, True
    low = max(args[0], args[1]) + 1
    return (low if rng.random() < 0.3 else rng.randint(low, 12)), False


def flood_replace(offset: int, size: int) -> Callable:
    """Every tile of the map preset: `old` (never the low bytes of args[2],
    that would loop for ever) or another value; the start tile is `old`."""
    mask = (1 << (8 * size)) - 1
    def presets(rng, pool, args):
        n, big = flood_map(rng, args)
        old = rng.choice([v for v in range(3) if v != args[2] & mask])
        density = 1.0 if big else rng.choice([0.3, 0.6, 0.9])
        fields = [(MAP_SIZE, n, 4)]
        for x in range(n):
            for y in range(n):
                start = (x, y) == (args[0], args[1])
                value = old if start or rng.random() < density else rng.choice(
                    [v for v in range(6) if v != old])
                fields.append((tile(x, y) + offset, value, size))
        return fields
    return presets


def flood_marked(terrain: bool) -> Callable:
    """Tiles mostly flagged 0x10 (tile +0xa) and, with `terrain`, of terrain
    5..7 without flag 0x80, else without flag 0x01."""
    clear = 0x80 if terrain else 0x01
    def presets(rng, pool, args):
        n, big = flood_map(rng, args)
        density = 1.0 if big else rng.choice([0.3, 0.6, 0.9])
        fields = [(MAP_SIZE, n, 4)]
        for x in range(n):
            for y in range(n):
                flags = rng.getrandbits(8)
                if rng.random() < density:
                    flags = (flags | 0x10) & ~clear
                fields.append((tile(x, y) + 0xa, flags, 1))
                if terrain:
                    value = rng.randint(5, 7) if big or rng.random() < 0.8 else rng.randrange(10)
                    fields.append((tile(x, y) + 0x6, value, 1))
        return fields
    return presets


def area_of_args_matching(rng, pool, args):
    area = args[2] & 0xFFFF if rng.random() < 0.7 else rng.randrange(AREA_LIMIT)
    return [(tile_area_offset(args[0], args[1]), area, 2),
            (0xe0788c, rng.choice([9999, rng.randrange(10000)]), 4)]


def small_map_terrain(rng, pool, args):
    """Map size -1..14; all its tiles get terrain 0..3 (so tiles match)."""
    n = rng.randint(-1, 14)
    fields = [(MAP_SIZE, n & 0xFFFFFFFF, 4)]
    for x in range(max(n, 0)):
        for y in range(max(n, 0)):
            fields.append((tile(x, y) + 0x6, rng.randrange(4), 1))
    return fields


AROUND = [(0, 0), (1, 0), (1, 1), (0, 1), (-1, 0), (-1, -1), (0, -1)]


def territory(rng, pool, args):
    """Map of 3..16 (or 384) tiles, the real 7-entry offset table (sometimes
    with a random entry), owners of a few players and mostly land."""
    n = rng.choice([384, rng.randint(3, 16), rng.randint(3, 16)])
    fields = [(MAP_SIZE, n, 4)]
    for k, (dx, dy) in enumerate(AROUND):
        if rng.random() < 0.05:
            dx, dy = rng.randint(-3, 3), rng.randint(-3, 3)
        fields += [(NEIGHBOUR_OFFSETS + k * 16, dx & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + k * 16 + 4, dy & 0xFFFFFFFF, 4)]
    for x in range(max(args[0] - 4, 0), min(args[0] + 5, n)):
        for y in range(max(args[1] - 4, 0), min(args[1] + 5, n)):
            owner = 0xff if rng.random() < 0.15 else rng.randrange(3)
            terrain = rng.getrandbits(8)
            if rng.random() < 0.2:
                terrain &= 0x0f
            fields += [(tile(x, y) + 0x8, owner, 1), (tile(x, y) + 0x6, terrain, 1)]
    return fields


def camera_tile(rng, pool, args):
    return [(MAP_SIZE, rng.choice([64, 384, rng.randrange(400), rng.getrandbits(32)]), 4)]


def settler_list(rng, pool, args):
    """A list of distinct settlers from args[0] (linked by +0x10, ending at
    0) near (args[2], args[3]), many on tiles whose byte +0x10 is args[4]."""
    fields = []
    settler = args[0]
    used = {0, settler}
    for _ in range(rng.randrange(8)):
        if settler == 0:
            break
        record = SETTLERS + settler * SETTLER_BYTES
        x = args[2] if rng.random() < 0.3 else min(max(args[2] + rng.randint(-8, 8), 0), 383)
        y = args[3] if rng.random() < 0.3 else min(max(args[3] + rng.randint(-8, 8), 0), 383)
        following = 0
        if rng.random() < 0.85:
            following = rng.randrange(1, 0x3000)
            while following in used:
                following = rng.randrange(1, 0x3000)
            used.add(following)
        fields += [(record + 0x4, x, 2), (record + 0x6, y, 2), (record + 0x10, following, 2),
                   (tile(x, y) + 0x10, args[4] & 0xFF if rng.random() < 0.7 else rng.randrange(4), 1)]
        settler = following
    if settler != 0:
        record = SETTLERS + settler * SETTLER_BYTES
        x, y = rng.randrange(384), rng.randrange(384)
        fields += [(record + 0x4, x, 2), (record + 0x6, y, 2), (record + 0x10, 0, 2)]  # ends here
    return fields


def ring_offsets(rng, k):
    """Entry k of a plausible offset table: rings of growing distance."""
    radius = 1 if k <= 6 else 2 if k <= 18 else 3
    return rng.randint(-radius, radius), rng.randint(-radius, radius)


def settlers_around(rng, pool, args):
    """Offset entries 1..36; tiles around (args[1], args[2]) with settlers,
    some of type 0x29 and of args[0]'s player. Sometimes a small map, so
    that tiles are off the map."""
    n = 384 if rng.random() < 0.6 else rng.randint(1, 30)
    me = SETTLERS + args[0] * SETTLER_BYTES
    player = rng.randrange(3)
    fields = [(MAP_SIZE, n, 4), (me + 0x0, player, 1)]
    sparse = rng.random()
    for k in range(1, 37):
        dx, dy = ring_offsets(rng, k)
        fields += [(NEIGHBOUR_OFFSETS + k * 16, dx & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + k * 16 + 4, dy & 0xFFFFFFFF, 4)]
        x, y = (args[1] + dx) & 0xFFFFFFFF, (args[2] + dy) & 0xFFFFFFFF
        if x >= 384 or y >= 384:
            continue
        settler = 0 if rng.random() < sparse else rng.randrange(1, 0x3000)
        fields.append((tile(x, y) + 0x0, settler, 2))
        if settler:
            record = SETTLERS + settler * SETTLER_BYTES
            fields += [(record + 0x1, 0x29 if rng.random() < 0.15 else rng.randrange(0x40), 1),
                       (record + 0x0, player if rng.random() < 0.5 else rng.randrange(3), 1)]
    return fields


def game_end_buildings(rng, pool, args):
    count = rng.choice([0, 1, rng.randrange(40)])
    fields = [(0x1499e60, count, 4)]
    for player in range(255):
        fields.append((PLAYER_RECORDS + player * PLAYER_RECORD_BYTES,
                       0 if rng.random() < 0.25 else rng.getrandbits(32) | 1, 4))
    for building in range(1, count + 1):
        record = BUILDINGS + building * BUILDING_BYTES
        player = 0xff if rng.random() < 0.15 else rng.randrange(255) if rng.random() < 0.05 \
            else rng.randrange(20)
        fields += [(record + 0x0, player, 1),
                   (record + 0x21, 0 if rng.random() < 0.7 else rng.randrange(256), 1),
                   (record + 0x6, rng.randrange(0xe, 0x14), 1)]
    return fields


def owned_points(rng, pool, args):
    """20 (int32 x, int32 y) points in scratch buffer 0 on tiles of a few owners."""
    fields = []
    for i in range(20):
        x, y = rng.randrange(384), rng.randrange(384)
        fields += [(SCRATCH + i * 8, x, 4), (SCRATCH + i * 8 + 4, y, 4),
                   (tile(x, y) + 0x8, 0xff if rng.random() < 0.1 else rng.randrange(4), 1)]
    return fields


AREAS = 0xe145e8
AREA_BYTES = 0x2e0


def area_piles(rng, pool, args):
    count = rng.choice([0, 1, rng.randrange(30)])
    fields = [(0xe115dc, count, 4)]
    next_pile = 1
    for area in range(1, count + 1):
        record = AREAS + area * AREA_BYTES
        player = 0xff if rng.random() < 0.15 else rng.randrange(255) if rng.random() < 0.05 \
            else rng.randrange(20)
        first = 0
        for _ in range(rng.randrange(5)):
            fields += [(PILES + next_pile * PILE_BYTES + 0xc, first, 2),
                       (PILES + next_pile * PILE_BYTES + 0x1, rng.getrandbits(8), 1)]
            first = next_pile
            next_pile += 1
        fields += [(record + 0x0, player, 1), (record + 0x1e, first, 2)]
    return fields


# --- batch 13 ---------------------------------------------------------------


def landscape(offsets: int, reach_near: int = 5, reach_far: int = 12) -> Callable:
    """A 64..80 map (one 64-tile block, so few rounds) of 8x8 patches of
    terrain 0x20/0x21 with some noise, mostly free tiles, the first
    `offsets` offset entries small, and a random RNG state."""
    def presets(rng, pool, args):
        n = rng.randint(64, 80) if rng.random() < 0.9 else rng.choice([0, 63, 128])
        fields = [(MAP_SIZE, n, 4)]
        for x in range(min(n, 80)):
            for y in range(min(n, 80)):
                patch = ((x >> 3) + (y >> 3) * 3) & 1
                terrain = (0x20, 0x21)[patch] if rng.random() < 0.94 else rng.choice([0x20, 0x21, 0x80, 0x10])
                fields += [(tile(x, y) + 0x6, terrain, 1),
                           (tile(x, y) + 0x7, 0 if rng.random() < 0.97 else 1, 1),
                           (tile(x, y) + 0xa, rng.getrandbits(8) & (0xfe if rng.random() < 0.97 else 0xff), 1),
                           (tile(x, y) + 0x11, 0 if rng.random() < 0.75 else 5, 1)]
        for k in range(offsets):
            reach = reach_near if k < 96 else reach_far
            fields += [(NEIGHBOUR_OFFSETS + k * 16, rng.randint(-reach, reach) & 0xFFFFFFFF, 4),
                       (NEIGHBOUR_OFFSETS + k * 16 + 4, rng.randint(-reach, reach) & 0xFFFFFFFF, 4)]
        return fields
    return presets


def block_mode_extra(rng, pool):
    return rng.choice([0, 1, 2, 3, rng.getrandbits(32)])


def input_goods(rng, pool, args):
    """A building of a random type, often a construction site."""
    record = BUILDINGS + args[0] * BUILDING_BYTES
    kind = rng.randrange(0x50) if rng.random() < 0.95 else rng.randrange(256)
    return [(record + 0x6, kind, 1),
            (record + 0x21, rng.randrange(1, 256) if rng.random() < 0.3 else 0, 1)]


def mana(rng, pool, args):
    record = PLAYER_RECORDS + args[0] * PLAYER_RECORD_BYTES
    def value():
        return rng.choice([rng.getrandbits(32), rng.randrange(0x200), rng.getrandbits(32) | 0x80000000])
    fields = [(record + 0x4, rng.randrange(5), 4), (record + 0x8c, value(), 4)]
    for slot in range(9):
        fields.append((record + 0x94 + slot * 4, value(), 4))
    return fields


def tile_pile(rng, pool, args):
    t = tile(args[0], args[1])
    pile = 0 if rng.random() < 0.1 else rng.randrange(1, 0x1000)
    area = rng.randrange(AREA_LIMIT)
    goods = rng.randrange(36)
    fields = [(t + 0x2, pile, 2), (t + 0x14, area, 2),
              (t + 0xa, rng.getrandbits(8) & (0xff if rng.random() < 0.15 else 0xdf), 1),
              (t + 0x8, 0xff if rng.random() < 0.1 else rng.randrange(20), 1)]
    if pile:
        head = 0 if rng.random() < 0.3 else rng.randrange(1, 0x1000)
        fields += [(PILES + pile * PILE_BYTES + 0x0, goods, 1),
                   (AREAS + 0xa + (area * 0x170 + goods) * 2, head, 2)]
    return fields


PRESENCE = 0xde7448


def presence(rng, pool, args):
    """Sparse presence bytes in the 26x26 blocks of a 384 map (or smaller)."""
    n = rng.choice([384, rng.randrange(16, 384)])
    fields = [(MAP_SIZE, n, 4), (0xde7444, rng.randrange(20), 4),
              (PLAYER_RECORDS + args[3] * PLAYER_RECORD_BYTES + 0xc,
               rng.randrange(20) if rng.random() < 0.9 else rng.getrandbits(32), 4)]
    density = rng.random() * 0.05
    for bx in range(26):
        for by in range(26):
            for index in range(20):
                fields.append((PRESENCE + bx * 960 + by * 20 + index,
                               1 if rng.random() < density else 0, 1))
    return fields


# --- batch 14 ---------------------------------------------------------------


def flood_area(rng, pool, args):
    """Areas 1..3 over the map; the start tile in area `old` != args[2]."""
    n, big = flood_map(rng, args)
    old = rng.choice([v for v in (1, 2, 3) if v != args[2]])
    density = 1.0 if big else rng.choice([0.3, 0.6, 0.9])
    fields = [(MAP_SIZE, n, 4), (0xe0788c, 1, 4)]
    for x in range(n):
        for y in range(n):
            start = (x, y) == (args[0], args[1])
            area = old if start or rng.random() < density else rng.choice([v for v in (1, 2, 3) if v != old])
            fields.append((tile_area_offset(x, y), area, 2))
    return fields


def merge_regions(field: int, size: int, ids_mark: int, ids_table: int, ids_count: int,
                  blocked: bool) -> Callable:
    """A 12x12 map of region ids 0..3 in tile `field`, the real neighbour
    offsets in entries 1..6, the id table partly used; with `blocked`, flag
    bit 0 of +0xa often set."""
    ids = id_table(ids_mark, ids_table, ids_count)
    def presets(rng, pool, args):
        fields = [(MAP_SIZE, 12, 4)]
        for k, (dx, dy) in enumerate(AROUND):
            fields += [(NEIGHBOUR_OFFSETS + k * 16, dx & 0xFFFFFFFF, 4),
                       (NEIGHBOUR_OFFSETS + k * 16 + 4, dy & 0xFFFFFFFF, 4)]
        empty = rng.random() < 0.15
        for x in range(12):
            for y in range(12):
                fields.append((tile(x, y) + field, 0 if empty and not blocked else rng.randrange(4), size))
                if blocked:
                    flags = rng.getrandbits(8)
                    fields.append((tile(x, y) + 0xa, flags | 1 if empty or rng.random() < 0.3 else flags & 0xfe, 1))
        return fields + ids(rng, pool, args)
    return presets


def goods_request(rng, pool, args):
    record = BUILDINGS + args[0] * BUILDING_BYTES
    x, y = rng.randrange(384), rng.randrange(384)
    area = rng.randrange(64)
    fields = [(record + 0xa, x, 2), (record + 0xc, y, 2), (tile_area_offset(x, y), area, 2),
              (AREAS + area * AREA_BYTES + 0x245 + args[3], rng.randrange(2), 1)]
    return fields + input_goods(rng, pool, args)


def request_lists(rng, pool, args):
    """63 buildings in 4 areas, mostly construction sites (inputs: goods 1 in
    slot 0, goods 2 in slot 1), some finished of type 0x21/0x22/0x1f; per
    area acyclic request lists for goods 1 and 2 through word +0x2a/+0x2c,
    random words at area +0x9a/+0xe2, other heads 0 or a random building."""
    fields = []
    area_of = {}
    construction = set()
    for b in range(1, 64):
        record = BUILDINGS + b * BUILDING_BYTES
        x, y = rng.randrange(384), rng.randrange(384)
        area = rng.randrange(4)
        area_of[b] = area
        site = rng.random() < 0.85
        if site:
            construction.add(b)
        fields += [(record + 0xa, x, 2), (record + 0xc, y, 2), (tile_area_offset(x, y), area, 2),
                   (record + 0x21, 1 if site else 0, 1),
                   (record + 0x6, rng.choice([0x21, 0x22, 0x1f, 0x07]), 1)]
    for area in range(4):
        base = AREAS + area * AREA_BYTES
        for goods in range(36):
            head = 0
            if goods in (1, 2):
                for b in range(1, 64):
                    if area_of[b] == area and b in construction and rng.random() < 0.5:
                        fields.append((BUILDINGS + b * BUILDING_BYTES + 0x2a + (goods - 1) * 2, head, 2))
                        head = b
            elif rng.random() < 0.1:
                head = rng.randrange(1, 64)
            fields += [(base + 0x52 + goods * 2, head, 2),
                       (base + 0x9a + goods * 2, rng.randrange(64), 2),
                       (base + 0xe2 + goods * 2, rng.randrange(64), 2)]
    return fields


def place_settler(rng, pool, args):
    n = 384 if rng.random() < 0.7 else rng.randint(1, 40)
    fields = [(MAP_SIZE, n, 4)]
    for k in range(1000):
        reach = 1 + k // 60
        fields += [(NEIGHBOUR_OFFSETS + k * 16, rng.randint(-reach, reach) & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + k * 16 + 4, rng.randint(-reach, reach) & 0xFFFFFFFF, 4)]
    free = rng.choice([0.0, 0.002, 0.02, 0.1])
    for x in range(max(args[1] - 18, 0), min(args[1] + 19, 384)):
        for y in range(max(args[2] - 18, 0), min(args[2] + 19, 384)):
            empty = rng.random() < free
            fields += [(tile(x, y) + 0x0, 0 if empty or rng.random() < 0.5 else rng.randrange(1, 100), 2),
                       (tile(x, y) + 0x10, rng.randrange(1, 4) if empty or rng.random() < 0.5 else 0, 1)]
    return fields


def small_territory(rng, pool, args):
    """A map of 0..10 tiles with the real 7-entry offset table and owners."""
    n = rng.randint(0, 10)
    fields = [(MAP_SIZE, n, 4)]
    for k, (dx, dy) in enumerate(AROUND):
        fields += [(NEIGHBOUR_OFFSETS + k * 16, dx & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + k * 16 + 4, dy & 0xFFFFFFFF, 4)]
    for x in range(n):
        for y in range(n):
            terrain = rng.getrandbits(8)
            fields += [(tile(x, y) + 0x8, 0xff if rng.random() < 0.15 else rng.randrange(3), 1),
                       (tile(x, y) + 0x6, terrain & 0x0f if rng.random() < 0.2 else terrain, 1)]
    return fields


def local_player_camera(rng, pool, args):
    player = rng.randrange(20)
    record = PLAYER_RECORDS + player * PLAYER_RECORD_BYTES
    return [(0x2d2d4, player, 4), (record + 0x10, rng.randrange(384), 4), (record + 0x14, rng.randrange(384), 4),
            (MAP_SIZE, rng.choice([64, 384, rng.randrange(400)]), 4)]


# --- batch 15 ---------------------------------------------------------------

RING2 = [(2, 0), (2, 1), (2, 2), (1, 2), (0, 2), (-1, 1), (-2, 0), (-2, -1), (-2, -2), (-1, -2), (0, -2),
         (1, -1)]


def region_assignment(rng, pool, args):
    """A 6..14 map, offset entries 0..18 (the tile and two rings), tiles
    mostly without region id, some blocked or of terrain kind 0x20."""
    n = rng.randint(10, 14)
    fields = [(MAP_SIZE, n, 4)]
    for k, (dx, dy) in enumerate(AROUND + RING2):
        fields += [(NEIGHBOUR_OFFSETS + k * 16, dx & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + k * 16 + 4, dy & 0xFFFFFFFF, 4)]
    blocked = rng.choice([0.0, 0.01, 0.05, 0.2])
    for x in range(16):
        for y in range(16):
            flags = rng.getrandbits(8)
            fields += [(tile(x, y) + 0x16, 0 if rng.random() < 0.7 else rng.randrange(4), 2),
                       (tile(x, y) + 0xa, flags | 1 if rng.random() < blocked else flags & 0xfe, 1),
                       (tile(x, y) + 0x6, 0x21 if rng.random() < blocked else 0x10, 1)]
    return fields + id_table(0xe125e4, 0xe125e8, 0x2000)(rng, pool, args)


def waiting_list(rng, head: int, x: int, y: int):
    """A doubly linked list (+0x10 next, +0x12 previous) of up to 12 distinct
    settlers at `head`, near (x, y)."""
    length = 0 if rng.random() < 0.1 else rng.randint(1, 12)
    settlers = rng.sample(range(1, 0x3000), length)
    fields = [(head, settlers[0] if settlers else 0, 2)]
    for i, settler in enumerate(settlers):
        record = SETTLERS + settler * SETTLER_BYTES
        reach = rng.choice([8, 60])
        fields += [(record + 0x12, settlers[i - 1] if i else 0, 2),
                   (record + 0x10, settlers[i + 1] if i + 1 < length else 0, 2),
                   (record + 0x4, (x + rng.randint(-reach, reach)) & 0xFFFF, 2),
                   (record + 0x6, (y + rng.randint(-reach, reach)) & 0xFFFF, 2)]
    return fields


def pile_and_waiting(rng, pool, args):
    record = PILES + args[0] * PILE_BYTES
    x, y = rng.randrange(384), rng.randrange(384)
    fields = [(record + 0x6, x, 2), (record + 0x8, y, 2), (record + 0x1, rng.randrange(10), 1),
              (record + 0x2, rng.randrange(10) if rng.random() < 0.6 else rng.getrandbits(8), 1)]
    head = PLAYER_RECORDS + args[1] * PLAYER_RECORD_BYTES + 0x6e
    return fields + waiting_list(rng, head, x, y)


def building_and_waiting(rng, pool, args):
    record = BUILDINGS + args[0] * BUILDING_BYTES
    player = rng.randrange(20)
    x, y = rng.randrange(2, 382), rng.randrange(2, 382)
    fields = [(record + 0x0, player, 1), (record + 0x30, x, 2), (record + 0x32, y, 2),
              (record + 0x22, rng.randrange(6) if rng.random() < 0.9 else rng.getrandbits(16), 2)]
    head = PLAYER_RECORDS + player * PLAYER_RECORD_BYTES + 0x76
    return fields + waiting_list(rng, head, x, y)


def settler_lists_world(rng, pool, args):
    """The settler's player and that player's ten lists (int32 counts at
    +0x11940bc + player*40, 100 words each at +0x118a47c + player*2000)
    with some copies of the settler, sometimes adjacent."""
    player = rng.randrange(20)
    fields = [(SETTLERS + args[0] * SETTLER_BYTES + 0x0, player, 1),
              (0x366fc, rng.randrange(1000), 4), (0x32870, 0, 4), (0x32874, 0, 4)]
    for i in range(10):
        count = 0 if rng.random() < 0.15 else rng.randint(0, 100)
        fields.append((0x11940bc + player * 40 + i * 4, count, 4))
        for w in range(100):
            fields.append((0x118a47c + player * 2000 + i * 200 + w * 2,
                           args[0] if rng.random() < 0.15 else rng.randrange(8), 2))
    return fields


def settler_lists_globals(rng, pool, args):
    """The ten global lists in S3.EXE (uint16 counts at 0x7ad620, 100 words
    each at 0x7ad634)."""
    fields = []
    for i in range(10):
        fields.append((0x7ad620 + i * 2, 0 if rng.random() < 0.15 else rng.randint(0, 100), 2))
        for w in range(100):
            fields.append((0x7ad634 + i * 200 + w * 2, args[0] if rng.random() < 0.15 else rng.randrange(8), 2))
    return fields


def pile_add(rng, pool, args):
    record = PILES + args[0] * PILE_BYTES
    x, y = rng.randrange(384), rng.randrange(384)
    amount = rng.randrange(10) if rng.random() < 0.85 else rng.getrandbits(8)
    goods = rng.randrange(36)
    area = rng.randrange(AREA_LIMIT)
    return [(record + 0x4, rng.choice([0, 0xfe, 0xff, 0xff, 1, 7]), 1), (record + 0x1, amount, 1),
            (record + 0x2, amount if rng.random() < 0.5 else rng.randrange(10), 1),
            (record + 0x3, rng.randrange(3), 1), (record + 0x0, goods, 1),
            (record + 0x6, x, 2), (record + 0x8, y, 2), (tile_area_offset(x, y), area, 2),
            (AREAS + 0xa + (area * 0x170 + goods) * 2, 0 if rng.random() < 0.3 else rng.randrange(1, 0x1000), 2)]


TILE = between(2, 381)  # tiles at least 2 from the edge of a 384 map

SPECS = [
    # search / statistics (world_search.cpp, statistics.cpp)
    Spec("TileDistance", 0x004ff320, "stdcall", [anything, anything, anything, anything]),
    Spec("GameWorld_NextSearchGeneration", 0x004ff3f0, "thiscall", [], eax_mask=0,
         presets=small_map_and_generation),
    Spec("GameWorld_InitStatisticsKernel", 0x004ff4c0, "thiscall", [], eax_mask=0),
    Spec("GameWorld_ShiftStatistics", 0x004ff470, "thiscall", [], eax_mask=0,
         presets=one_active_player, max_cases=60),
    Spec("GameWorld_AddStatistic", 0x004ff510, "thiscall", [below(20), below(36), anything],
         eax_mask=0),
    Spec("GameWorld_SeedRandom_Impl", 0x004feb40, "thiscall", [anything], eax_mask=0),
    Spec("GameWorld_Random", 0x004feb70, "thiscall", [], eax_mask=0xFFFF),
    # directions (map_directions.cpp)
    Spec("NeighbourX", 0x00503700, "stdcall", [anything, below(8, 0.2)]),
    Spec("NeighbourY", 0x00503730, "stdcall", [anything, below(8, 0.2)]),
    Spec("PreviousDirection", 0x005038a0, "stdcall", [below(8, 0.2)]),
    Spec("NextDirection", 0x005038c0, "stdcall", [below(8, 0.2)]),
    Spec("DirectionFromDelta", 0x005037f0, "stdcall", [between(-20, 20), between(-20, 20)]),
    # players (players.cpp)
    Spec("GameWorld_AddPlayerCounter", 0x004ffb00, "thiscall",
         [one_of(*range(20), 0xFF), below(64), anything]),
    Spec("GameWorld_SubtractPlayerCounter", 0x004ffb40, "thiscall",
         [one_of(*range(20), 0xFF), below(64), anything]),
    Spec("GameWorld_ClearPlayerListLengths", 0x0050c9c0, "thiscall", []),
    # settlers (settlers.cpp)
    Spec("GameWorld_CanSettlerEnterTile", 0x00503760, "thiscall",
         [below(384), below(384), below(20), below(48)], presets=full_map),
    Spec("GameWorld_SettlerGroup", 0x005038e0, "thiscall", [below(4096)]),
    # map (map.cpp)
    Spec("GameWorld_IsOnMap", 0x005194e0, "thiscall", [anything, anything],
         presets=map_size(between(0, 400))),
    Spec("GameWorld_IsInsideMapBorder", 0x00519510, "thiscall", [anything, anything],
         presets=map_size(between(0, 400))),
    Spec("GameWorld_TileScreenX", 0x00519610, "thiscall", [anything, anything],
         presets=map_size(anything)),
    Spec("GameWorld_TileScreenY", 0x00519640, "thiscall", [below(384), below(384)]),
    # neighbourhood (map_neighbourhood.cpp)
    Spec("GameWorld_ReplaceTerrainType", 0x00519210, "thiscall", [below(4), anything],
         presets=map_size(between(-1, 14))),
    Spec("GameWorld_IsNeighbourhoodByte9Clear", 0x00519860, "thiscall", [TILE, TILE]),
    Spec("GameWorld_HasAtMostTwoType70Neighbours", 0x005198d0, "thiscall", [TILE, TILE]),
    Spec("GameWorld_IsNeighbourhoodTerrain", 0x0051b450, "thiscall", [TILE, TILE, below(8, 0.1)],
         presets=uniform_terrain),
    Spec("GameWorld_AreOffsetTilesClear", 0x0051b3c0, "thiscall",
         [between(4, 379), between(4, 379), below(21)], presets=offset_tiles),
    Spec("GameWorld_IsClearOfType70Nearby", 0x00519960, "thiscall", [TILE, TILE]),
    Spec("GameWorld_IsFreeOfSettlerType29Nearby", 0x005199e0, "thiscall", [TILE, TILE],
         presets=full_map),
    # counting (counting.cpp)
    Spec("GameWorld_CountBuildings", 0x00515850, "thiscall", [below(5), below(5)],
         presets=record_counts),
    Spec("GameWorld_CountBuildingsWithFlag21Clear", 0x005158a0, "thiscall", [below(5), below(5)],
         presets=record_counts),
    Spec("GameWorld_CountBuildingsWithWord8", 0x005159d0, "thiscall", [below(5), below(5)],
         presets=record_counts),
    Spec("GameWorld_CountSettlers", 0x00515a30, "thiscall", [below(5), below(5)],
         presets=record_counts),
    Spec("GameWorld_CountSettlersWithJob", 0x00515a80, "thiscall", [below(5), below(5)],
         presets=record_counts),
    # building request flags (building_flags.cpp)
    *[Spec(f"GameWorld_{kind}BuildingFlag{bit}", address, "thiscall", [between(1, 2000)],
           eax_mask=0, presets=building_request_state)
      for kind, bit, address in [
          ("Clear", "02", 0x004df490), ("Clear", "04", 0x004df510),
          ("Clear", "08", 0x004df590), ("Clear", "10", 0x004df610),
          ("Set", "02", 0x004df690), ("Set", "04", 0x004df730),
          ("Set", "08", 0x004df7d0), ("Set", "10", 0x004df850)]],
    # UI state queries (ui_state.cpp)
    *[Spec(name, address, "thiscall", [anything],
           presets=field_equals_arg_half_the_time(UI_FIELDS[name]))
      for name, address in [("GameWorld_IsUiSelectionMode", 0x005157b0),
                            ("GameWorld_IsUiField298", 0x005157d0),
                            ("GameWorld_IsUiSelectionParam1", 0x005157f0),
                            ("GameWorld_IsUiSelectionParam2", 0x00515810)]],
    Spec("GameWorld_IsUiSelectionMode4Field298Is2", 0x00515830, "thiscall", [],
         presets=mode4_field298),
    Spec("GameWorld_IsGameOutcome1", 0x00515ad0, "thiscall", [anything], globals=game_outcome),
    Spec("GameWorld_ScreenCenterTileX", 0x005158f0, "thiscall", [], globals=screen_size),
    Spec("GameWorld_ScreenCenterTileY", 0x00515980, "thiscall", [], globals=screen_size),
    Spec("GameWorld_ClampCamera", 0x0051b4d0, "thiscall", [], presets=camera,
         globals=screen_size),
    Spec("GameWorld_PushUiFeedback", 0x004febe0, "thiscall", [anything],
         presets=ui_feedback_count),
    # free ids (id_allocators.cpp)
    Spec("GameWorld_FindFreeId256", 0x0051d5b0, "thiscall", [],
         presets=id_table(0xe114d8, 0xe114dc, 0x100)),
    Spec("GameWorld_FindFreeId4096", 0x0051d570, "thiscall", [],
         presets=id_table(0xe115e0, 0xe115e4, 0x1000), max_cases=400),
    Spec("GameWorld_FindFreeId8192", 0x0051c040, "thiscall", [],
         presets=id_table(0xe125e4, 0xe125e8, 0x2000), max_cases=200),
    # map areas (map_areas.cpp)
    Spec("GameWorld_AssignTileToArea", 0x004e5450, "thiscall",
         [below(384), below(384), below(AREA_LIMIT)]),
    Spec("GameWorld_RemoveTileFromArea", 0x004e54a0, "thiscall", [below(384), below(384)],
         presets=tile_area_of_args),
    Spec("GameWorld_SetTileAreaMarked", 0x005194b0, "thiscall",
         [below(384, 0.05), below(384, 0.05), anything]),
    Spec("GameWorld_SetAreaBuildingSlotA", 0x004ddfe0, "thiscall",
         [between(0, 2000), below(64)], presets=building_on_map),
    Spec("GameWorld_SetAreaBuildingSlotB", 0x004de040, "thiscall",
         [between(0, 2000), below(64)], presets=building_on_map),
    # map marks (map_marks.cpp)
    Spec("GameWorld_SetDiagonalTileByteB", 0x00518d10, "thiscall", [anything], eax_mask=0,
         presets=diagonal_map_size, max_cases=300),
    Spec("GameWorld_ClearDiagonalTileByteB", 0x00518dc0, "thiscall", [anything], eax_mask=0,
         presets=diagonal_map_size, max_cases=300),
    Spec("GameWorld_ClearTileByte11Slice", 0x00518ec0, "thiscall", [below(60, 0.1)],
         presets=slice_map_size, max_cases=300),
    # building/settler tile references (building_tiles.cpp)
    Spec("GameWorld_RemoveBuildingFromTiles", 0x004d9940, "thiscall", [between(0, 2000)],
         eax_mask=0, presets=building_arg_on_map),
    Spec("GameWorld_LeaveBuildingSlot", 0x0050cc10, "thiscall", [between(0, 4000)],
         presets=settler_in_building),
    Spec("GameWorld_MoveSettlerToBuildingTileA", 0x0050de90, "thiscall", [between(0, 4000)],
         presets=settler_in_building),
    Spec("GameWorld_MoveSettlerToBuildingTileB", 0x0050df40, "thiscall", [between(0, 4000)],
         presets=settler_in_building),
    # goods piles (goods_piles.cpp)
    Spec("GameWorld_BuildingPileHasGoods", 0x004df9b0, "thiscall",
         [between(0, 2000), below(5, 0.15)], eax_mask=0xff, presets=building_pile_slots),
    Spec("GameWorld_RecountPlayerGoods", 0x004ffa50, "thiscall", [], presets=goods_piles,
         max_cases=600),
    Spec("GameWorld_PileNeighbourBuildingByteE", 0x00505fd0, "thiscall", [between(1, 3000)],
         presets=pile_neighbourhood),
    # tables, rings, timers (world_tables.cpp)
    Spec("GameWorld_AllocBuildingExtra", 0x004df0a0, "thiscall", [anything],
         presets=alloc_table(0x10f45e8, 0x5a, 998, 0x110a578, 1, 0xff), max_cases=600),
    Spec("GameWorld_AllocTable36700Record", 0x004fefd0, "thiscall", [anything] * 10,
         presets=alloc_table(0x36700, 20, 9998, 0x67440, 2, 0), max_cases=150),
    Spec("GameWorld_QueueEvent", 0x004e8ad0, "thiscall",
         [event_type, below(4, 0.1), anything, anything], presets=event_rings),
    Spec("GameWorld_QueueMarkedTile", 0x004e6c40, "thiscall",
         [below(384), below(384), below(4, 0.1)], eax_mask=0, presets=marked_tile),
    Spec("GameWorld_QueueMarkedTileTerrain5To7", 0x0051ce50, "thiscall",
         [below(384), below(384)], eax_mask=0, presets=marked_tile_terrain),
    Spec("GameWorld_QueueMarkedTileNoOwner", 0x0051d3d0, "thiscall",
         [below(384), below(384)], eax_mask=0, presets=marked_tile_terrain),
    Spec("GameWorld_ReplaceTileWord16AndQueue", 0x0051c560, "thiscall",
         [below(384), below(384), below(4, 0.1), anything], eax_mask=0,
         presets=replace_tile(0x16, 2)),
    Spec("GameWorld_ReplaceTileByte10AndQueue", 0x0051c820, "thiscall",
         [below(384), below(384), below(4, 0.1), anything], eax_mask=0,
         presets=replace_tile(0x10, 1)),
    Spec("GameWorld_CountDownTimers", 0x004fee00, "thiscall", [], eax_mask=0, presets=timers,
         max_cases=200),
    Spec("GameWorld_AddToRecentPositions", 0x00510770, "thiscall", [below(8, 0.1), below(8, 0.1)],
         eax_mask=0, presets=recent_positions, globals=recent_positions_cursor, max_cases=800),
    # player queries (player_queries.cpp)
    Spec("GameWorld_FindPlayerByField0C", 0x00507f10, "thiscall", [below(6, 0.1)],
         presets=players_field0c),
    Spec("BuildingTypeAllowedForRace", 0x004ffd60, "stdcall", [anything, building_type],
         eax_mask=0xff),
    # flood fills (flood_fills.cpp)
    Spec("GameWorld_FloodFillTileWord16", 0x0051c6c0, "thiscall",
         [below(12), below(12), anything], presets=flood_replace(0x16, 2), max_cases=150),
    Spec("GameWorld_FloodFillTileByte10", 0x0051c8a0, "thiscall",
         [below(12), below(12), anything], presets=flood_replace(0x10, 1), max_cases=150),
    Spec("GameWorld_FloodMarkAreaTerrain5To7", 0x0051cee0, "thiscall",
         [below(12), below(12), anything], eax_mask=0, presets=flood_marked(True), max_cases=150),
    Spec("GameWorld_FloodSetByte10MarkedTiles", 0x0051d450, "thiscall",
         [below(12), below(12), anything], eax_mask=0, presets=flood_marked(False), max_cases=150),
    Spec("GameWorld_MoveTileToAreaAndQueue", 0x004e6430, "thiscall",
         [below(384), below(384), below(AREA_LIMIT, 0.1), below(AREA_LIMIT)],
         presets=area_of_args_matching),
    # terrain and borders (map_terrain.cpp)
    Spec("GameWorld_ReplaceTerrainF3", 0x0051a400, "thiscall", [anything],
         presets=map_size(between(-1, 14))),
    Spec("GameWorld_SpreadTerrainRandomly", 0x00519330, "thiscall",
         [below(4), below(4), anything, between(-5, 110)], presets=small_map_terrain,
         max_cases=600),
    Spec("GameWorld_UpdateBorderFlagsAround", 0x0051a460, "thiscall", [below(16), below(16)],
         presets=territory),
    # camera, random, logic thread (ui_state.cpp, random.cpp)
    Spec("GameWorld_CenterCameraOnTile", 0x0051b560, "thiscall", [below(384), below(384)],
         presets=camera_tile, globals=screen_size),
    Spec("GameWorld_RandomBelow6", 0x00503880, "thiscall", []),
    Spec("CLogicThread_PushUiFeedback", 0x0053f560, "logicthread", [anything],
         presets=ui_feedback_count),
    # settlers (settler_queries.cpp)
    Spec("GameWorld_SetSettlerTarget", 0x00503610, "thiscall",
         [below(0x10000), below(400, 0.1), below(400, 0.1)], presets=map_size(between(0, 400))),
    Spec("GameWorld_CountListSettlersNear", 0x0050e8f0, "thiscall",
         [lambda rng, pool: 0 if rng.random() < 0.1 else rng.randrange(1, 0x3000),
          below(20, 0.2), below(384), below(384), below(3, 0.1)],
         presets=settler_list),
    Spec("GameWorld_FindNearbySettlerType29", 0x0050cdb0, "thiscall",
         [between(1, 0x2fff), below(384), below(384)], presets=settlers_around),
    # game-end counts (game_end.cpp)
    Spec("CLogicThread_CountBuildingsOfTypes15To18", 0x00514a00, "logicthread", [scratch(0)],
         presets=game_end_buildings, max_cases=500),
    Spec("CLogicThread_CountTilesOwnedBy", 0x00514a90, "logicthread",
         [below(4, 0.1), scratch(0), between(-2, 20)], presets=owned_points),
    Spec("CLogicThread_CountTilesOwnedByEither", 0x00514ae0, "logicthread",
         [below(4, 0.1), below(4, 0.1), scratch(0), between(-2, 20)], presets=owned_points),
    Spec("CLogicThread_SumAreaGoods10PerPlayer", 0x00514b40, "logicthread", [scratch(0)],
         presets=area_piles),
    # map generation (map_generation.cpp)
    Spec("GameWorld_ScatterTileByte11", 0x0051ad40, "thiscall", [anything, below(41)], eax_mask=0,
         presets=landscape(96), max_cases=300),
    Spec("GameWorld_ScatterTerrainNear", 0x0051aed0, "thiscall",
         [one_of(0x20, 0x21, 0x80), anything, between(-3, 24)], eax_mask=0,
         presets=landscape(0), max_cases=300),
    Spec("GameWorld_ScatterObjects", 0x0051b010, "thiscall",
         [one_of(0x20, 0x21, 0x80), anything, below(12), block_mode_extra, block_mode_extra],
         eax_mask=0, presets=landscape(20), max_cases=300),
    Spec("GameWorld_ScatterObjectGroups", 0x0051b1a0, "thiscall",
         [one_of(0x20, 0x21, 0x80), below(8, 0.2), below(16, 0.2), below(48), block_mode_extra,
          block_mode_extra], eax_mask=0, presets=landscape(512), max_cases=300),
    # building input goods (building_goods.cpp)
    Spec("GameWorld_BuildingInputWord2A", 0x004df150, "thiscall", [below(4096), below(36, 0.1)],
         presets=input_goods),
    Spec("GameWorld_BuildingInputWord24", 0x004df220, "thiscall", [below(4096), below(36, 0.1)],
         presets=input_goods),
    Spec("GameWorld_SetBuildingInputWord2A", 0x004df2f0, "thiscall",
         [below(4096), below(36, 0.1), anything], presets=input_goods),
    Spec("GameWorld_SetBuildingInputWord24", 0x004df3c0, "thiscall",
         [below(4096), below(36, 0.1), anything], presets=input_goods),
    # god events, piles, presence (godly_events.cpp, area_piles.cpp, player_presence.cpp)
    Spec("GameWorld_HasManaForSpell", 0x0054b240, "thiscall", [below(20), below(36, 0.1)],
         presets=mana),
    Spec("GameWorld_AddTilePileToArea", 0x005051b0, "thiscall", [below(384), below(384)],
         presets=tile_pile),
    Spec("GameWorld_IsOtherPlayerNear", 0x00510650, "thiscall",
         [below(400), below(400), below(40), below(20)], presets=presence, max_cases=400),
    # batch 14: region merges, area flood, requests, placement, terrain passes
    Spec("GameWorld_FloodMoveArea", 0x004e62c0, "thiscall", [below(12), below(12), between(1, 6)],
         presets=flood_area, max_cases=150),
    Spec("GameWorld_MergeTileWord16Regions", 0x0051c5e0, "thiscall", [between(1, 10), between(1, 10)],
         presets=merge_regions(0x16, 2, 0xe125e4, 0xe125e8, 0x2000, False), max_cases=500),
    Spec("GameWorld_MergeTileByte10Regions", 0x0051ca00, "thiscall", [between(1, 10), between(1, 10)],
         presets=merge_regions(0x10, 1, 0xe114d8, 0xe114dc, 0x100, True), max_cases=500),
    Spec("GameWorld_AddBuildingGoodsRequest", 0x004ddf30, "thiscall",
         [below(2000), below(36, 0.1), one_of(0, 1, 7, 0xFFFFFFFF), below(64)], presets=goods_request),
    Spec("GameWorld_RemoveBuildingFromAreaRequests", 0x004def80, "thiscall", [between(1, 63)],
         presets=request_lists, max_cases=300),
    Spec("GameWorld_PlaceSettlerNear", 0x0050ce80, "thiscall",
         [below(0x3000), below(384, 0.03), below(384)], presets=place_settler, max_cases=400),
    Spec("GameWorld_GrowTerrain", 0x00519470, "thiscall", [below(4), below(4), between(-5, 110)],
         presets=small_map_terrain, max_cases=600),
    Spec("GameWorld_UpdateAllBorderFlags", 0x0051a420, "thiscall", [], presets=small_territory,
         max_cases=400),
    Spec("GameWorld_CenterCameraOnLocalPlayer", 0x0051b6b0, "thiscall", [],
         presets=local_player_camera, globals=screen_size),
    # batch 15
    Spec("GameWorld_AssignWord16RegionsAround", 0x0051c0f0, "thiscall", [between(3, 12), between(3, 12)],
         presets=region_assignment, max_cases=400),
    Spec("GameWorld_SendNearestSettlerToPile", 0x004ddbd0, "thiscall",
         [between(1, 0x800), below(20), anything], presets=pile_and_waiting),
    Spec("GameWorld_SendNearestSettlerToBuilding", 0x004ddd70, "thiscall", [between(1, 0x800)],
         presets=building_and_waiting),
    Spec("GameWorld_RemoveSettlerFromLists", 0x0050dff0, "thiscall", [between(1, 8)],
         presets=settler_lists_world, globals=settler_lists_globals, max_cases=500),
    Spec("GameWorld_AddGoodsToPile", 0x00505650, "thiscall", [between(1, 0xfff)], presets=pile_add),
]


# Deliberately wrong pairings for `run.py --self-test`: each must be reported
# as a mismatch, which shows that the harness notices wrong return values,
# wrong memory writes and wrong conditions.
SELF_TEST = [
    Spec("NeighbourX", 0x00503730, "stdcall", [anything, below(8)]),
    Spec("GameWorld_CountBuildingsWithFlag21Clear", 0x00515850, "thiscall", [below(5), below(5)],
         presets=record_counts),
    Spec("GameWorld_SetBuildingFlag04", 0x004df690, "thiscall", [between(1, 2000)], eax_mask=0,
         presets=building_request_state),
    Spec("GameWorld_IsOnMap", 0x00519510, "thiscall", [anything, anything],
         presets=map_size(between(0, 400))),
    Spec("GameWorld_ClearBuildingFlag08", 0x004df490, "thiscall", [between(1, 2000)],
         eax_mask=0, presets=building_request_state),
    Spec("GameWorld_SetAreaBuildingSlotA", 0x004de040, "thiscall",
         [between(0, 2000), below(64)], presets=building_on_map),
    Spec("GameWorld_ScreenCenterTileX", 0x00515980, "thiscall", [], globals=screen_size),
    Spec("GameWorld_FindFreeId4096", 0x0051d5b0, "thiscall", [],
         presets=id_table(0xe114d8, 0xe114dc, 0x100)),
    Spec("GameWorld_ClearDiagonalTileByteB", 0x00518d10, "thiscall", [anything], eax_mask=0,
         presets=diagonal_map_size, max_cases=100),
    Spec("CLogicThread_CountTilesOwnedBy", 0x00514ae0, "logicthread",
         [below(4, 0.1), below(4, 0.1), scratch(0), between(-2, 20)], presets=owned_points),
    Spec("GameWorld_FloodFillTileByte10", 0x0051c6c0, "thiscall",
         [below(12), below(12), anything], presets=flood_replace(0x16, 2), max_cases=100),
]
