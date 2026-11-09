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
    # fake CLogicThread whose +0x3a0 points at the world), "stdcall", or
    # "fastcall" (args[0] in ECX, args[1] in EDX, the rest on the stack)
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
    # EDX for the call (for __fastcall functions); otherwise random.
    edx: Generator | None = None
    # Coverage: analyze the original up to this address instead of the next
    # known function start (code that continues past a start Ghidra found
    # inside it), and leave out these block starts: blocks only reachable
    # through a C++ exception (catch handlers) or dead code (inlined code
    # that re-checks a condition already decided); say why at the spec.
    cover_until: int | None = None
    exempt_blocks: tuple[int, ...] = ()
    # Virtual calls through emu.FAKE_VTABLE (point an object's vtable there
    # with a preset): {slot offset: stack argument count} or {offset:
    # (count, {argument index: bytes})} for arguments that point into the
    # stack, whose contents are compared instead of their address. Calls to
    # undeclared slots are reported as spec errors.
    virtuals: dict | None = None
    # World offsets whose final bytes are not compared. Only for bytes that
    # provably hold leftovers of each function's own stack (e.g. padding of a
    # stack temporary copied into the result), which differ between any two
    # correct implementations; say why at the spec. A self-test pairing must
    # show that the neighbouring bytes are still compared.
    ignore_bytes: tuple[int, ...] = ()
    # Side effects of virtual calls: {slot offset: effect(machine, ecx, args,
    # h)}, run after the call is recorded; `h` is a random 64-bit value fixed
    # by the case and the call's position, so both functions see the same
    # effects as long as they make the same calls. Effects write with
    # machine.guest_write (e.g. a child that removes itself from a list),
    # to reach code that only runs when a callee changes the caller's state.
    virtual_effects: dict | None = None
    # Chance per case that every operator new of the case returns null (to
    # reach the out-of-memory paths).
    new_fails: float = 0.0


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


# -- batch 17 ---------------------------------------------------------------

BUILDING_COUNT_FIELD = 0x1499e60


def pile_leaving(rng, pool, args):
    """A pile on the map with its links, often all units leaving after this
    one, sometimes 8+ leaving (DebugLog and the building scan, with a small
    building count and maybe a building referring to the pile)."""
    record = PILES + args[0] * PILE_BYTES
    x, y = rng.randrange(384), rng.randrange(384)
    leaving = rng.randrange(12) if rng.random() < 0.85 else rng.getrandbits(8)
    amount = (leaving + 1) & 0xFF if rng.random() < 0.5 else rng.randrange(12)
    goods = rng.randrange(36)
    area = rng.randrange(AREA_LIMIT)
    count = 0 if rng.random() < 0.15 else rng.randrange(1, 60)
    fields = [(record + 0x4, rng.choice([0xff, 0xfe, 0xff, 0, 1, 7]), 1), (record + 0x2, leaving, 1),
              (record + 0x1, amount, 1), (record + 0x0, goods, 1), (record + 0x6, x, 2), (record + 0x8, y, 2),
              (record + 0xa, 0 if rng.random() < 0.4 else rng.randrange(1, 0x1000), 2),
              (record + 0xc, 0 if rng.random() < 0.4 else rng.randrange(1, 0x1000), 2),
              (tile_area_offset(x, y), area, 2), (BUILDING_COUNT_FIELD, count, 4)]
    for b in range(1, count + 1):
        for word in (0x10, 0x12, 0x14, 0xe):
            fields.append((BUILDINGS + b * BUILDING_BYTES + word, rng.randrange(0x1000, 0x10000), 2))
    if count and rng.random() < 0.6:
        b = rng.randrange(1, count + 1)
        fields.append((BUILDINGS + b * BUILDING_BYTES + rng.choice((0x10, 0x12, 0x14, 0xe)), args[0], 2))
    return fields


def player_on_map(rng, pool, args):
    record = PLAYER_RECORDS + args[0] * PLAYER_RECORD_BYTES
    n = rng.choice([384, rng.randrange(40, 384)])
    return [(MAP_SIZE, n, 4), (record + 0x10, rng.randrange(n), 4), (record + 0x14, rng.randrange(n), 4)]


def colour_table(rng, pool, args):
    """World +0xa0 points at a 257-slot uint16 table in scratch 0: slots
    1..n-1 filled (sometimes all), sometimes holding the colour asked for."""
    fields = [(0xa0, WORLD_BASE + SCRATCH, 4)]
    filled = 257 if rng.random() < 0.3 else rng.randrange(257)
    for slot in range(1, 257):
        value = rng.randrange(1, 0x10000) if slot < filled else 0
        fields.append((SCRATCH + slot * 2, value, 2))
    if rng.random() < 0.4:
        slot = rng.randrange(1, 257)
        fields.append((SCRATCH + slot * 2, args[0] & 0xFFFF, 2))
    return fields


def colour_arg(rng, pool):
    r = rng.random()
    if r < 0.05:
        return rng.getrandbits(16) << 16  # low word 0
    return rng.getrandbits(32)


def line_args() -> list[Generator]:
    """(x0, y0, x1, y1, value): end points on the map, often close; rarely
    a far end point whose tile address wraps around to (x0, y0) (deltas of
    0x50000000 and -0x60000000: distance 2^31 or more)."""
    state = {}

    def x0(rng, pool):
        state["far"] = rng.random() < 0.05
        state["x0"] = rng.randrange(384)
        return state["x0"]

    def y0(rng, pool):
        state["y0"] = rng.randrange(384)
        return state["y0"]

    def x1(rng, pool):
        if state["far"]:
            return (state["x0"] + 0x50000000) & 0xFFFFFFFF
        if rng.random() < 0.3:
            return min(383, max(0, state["x0"] + rng.randint(-12, 12)))
        return rng.randrange(384)

    def y1(rng, pool):
        if state["far"]:
            return (state["y0"] - 0x60000000) & 0xFFFFFFFF
        if rng.random() < 0.3:
            return min(383, max(0, state["y0"] + rng.randint(-12, 12)))
        return rng.randrange(384)

    return [x0, y0, x1, y1, anything]


def line_map(rng, pool, args):
    return [(MAP_SIZE, rng.choice([384, rng.randrange(1, 384), rng.getrandbits(32)]), 4)]


WORK_AREA_TYPES = {0x02: 3000, 0x03: 3000, 0x05: 1500, 0x0e: 150, 0x18: 300, 0x19: 2000, 0x1e: 800,
                   0x24: 400, 0x33: 1500}


def work_area(rng, pool, args):
    """The building's type (mostly one with a small working area), buildup,
    centre and tile; the offset table up to the type's entry with
    nondecreasing values (sometimes all equal: nothing drawn) and small
    offsets; areas of the tiles near the centre 0 or 1."""
    record = BUILDINGS + args[0] * BUILDING_BYTES
    r = rng.random()
    if r < 0.6:
        btype = rng.choice([0x0e, 0x18, 0x24, 0x1e])
    elif r < 0.85:
        btype = rng.choice(list(WORK_AREA_TYPES))
    else:
        btype = rng.choice([0, 1, 0x30, 0xff, rng.randrange(256)])
    n = rng.choice([384, rng.randrange(20, 384)])
    cx, cy = rng.randrange(n), rng.randrange(n)
    bx, by = min(n - 1, max(0, cx + rng.randint(-3, 3))), min(n - 1, max(0, cy + rng.randint(-3, 3)))
    fields = [(MAP_SIZE, n, 4), (record + 0x6, btype, 1),
              (record + 0x21, 0 if rng.random() < 0.5 else rng.randrange(1, 256), 1),
              (record + 0xa, bx, 2), (record + 0xc, by, 2), (tile_area_offset(bx, by), 1, 2),
              (SEARCH_GENERATION, rng.choice([0xff, rng.randrange(255)]), 1)]
    for field in ((0x30, 0x32), (0xe, 0x14)):
        fields += [(record + field[0], cx, 2), (record + field[1], cy, 2)]
    entries = WORK_AREA_TYPES.get(btype, 0)
    flat = rng.random() < 0.05
    value = rng.randrange(3)
    reach = 1
    for i in range(entries + 1):
        if not flat and rng.random() < 0.4:
            value += rng.randrange(1, 3)
        if i % 60 == 59:
            reach += 1
        fields += [(NEIGHBOUR_OFFSETS + i * 16, rng.randint(-reach, reach) & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + i * 16 + 4, rng.randint(-reach, reach) & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + i * 16 + 8, value, 4)]
    if flat or rng.random() < 0.05:
        fields.append((NEIGHBOUR_OFFSETS + 8, value + 5, 4))  # entry 0 above r: nothing drawn
    for x in range(max(cx - reach - 1, 0), min(cx + reach + 2, n)):
        for y in range(max(cy - reach - 1, 0), min(cy + reach + 2, n)):
            fields.append((tile_area_offset(x, y), 1 if rng.random() < 0.7 else 0, 2))
    return fields


RING2 = [(2, 0), (2, 1), (2, 2), (1, 2), (0, 2), (-1, 1), (-2, 0), (-2, -1), (-2, -2), (-1, -2), (0, -2), (1, -1)]


def split_regions(field: int, size: int, ids_mark: int, ids_table: int, ids_count: int,
                  labelled: bool | float) -> Callable:
    """A 12x12 map: region ids in tile `field` (random 1..3 / 0, or each
    connected stretch its own id), the frame off the map without one, the
    real offsets in entries 0..18, random flags, and the ids in use marked
    (so a new id never equals one in use). A float `labelled` is the
    chance per case of labelled regions."""
    def presets(rng, pool, args):
        n = 12
        labelled_case = labelled if isinstance(labelled, bool) else rng.random() < labelled
        fields = [(MAP_SIZE, n, 4)]
        for k, (dx, dy) in enumerate(AROUND + RING2):
            fields += [(NEIGHBOUR_OFFSETS + k * 16, dx & 0xFFFFFFFF, 4),
                       (NEIGHBOUR_OFFSETS + k * 16 + 4, dy & 0xFFFFFFFF, 4)]
        density = rng.randrange(3, 8)
        walk = {(x, y): rng.randrange(8) < density for x in range(n) for y in range(n)}
        label = {}
        if labelled_case:
            next_id = 1
            for start in walk:
                if not walk[start] or start in label:
                    continue
                label[start] = next_id
                stack = [start]
                while stack:
                    x, y = stack.pop()
                    for dx, dy in AROUND[1:]:
                        q = (x + dx, y + dy)
                        if walk.get(q) and q not in label:
                            label[q] = next_id
                            stack.append(q)
                next_id += 1
        else:
            kinds = rng.randrange(1, 4)
            label = {t: rng.randrange(1, kinds + 1) for t in walk if walk[t]}
            next_id = 4
        if args[0] < n and args[1] < n and (args[0], args[1]) not in label and not labelled_case:
            label[(args[0], args[1])] = 1
        for x in range(-1, n + 1):
            for y in range(-1, n + 1):
                t = tile(x & 0xFFFFFFFF, y & 0xFFFFFFFF)
                fields += [(t + field, label.get((x, y), 0), size), (t + 0xa, rng.getrandbits(8), 1)]
        for i in range(next_id + 2):
            fields.append((ids_table + i, 1 if 0 < i < next_id else 0, 1))
        fields.append((ids_mark, rng.randrange(next_id + 2), 4))
        return fields
    return presets


def worker_and_list(rng, pool, args):
    """A building on the map, its player's list args[1] of settlers near it
    (on the map), their tiles in the building's region or another; the
    chosen one's lists for RemoveSettlerFromLists short; event ring indices
    in range."""
    record = BUILDINGS + args[0] * BUILDING_BYTES
    player = rng.randrange(20)
    x, y = rng.randrange(2, 382), rng.randrange(2, 382)
    region = rng.randrange(1, 3)
    fields = [(MAP_SIZE, 384, 4), (record + 0x0, player, 1), (record + 0xa, x, 2), (record + 0xc, y, 2),
              (record + 0x2, x + rng.randrange(3), 2), (record + 0x4, y + rng.randrange(3), 2),
              (tile(x, y) + 0x10, region, 1),
              (0x366fc, rng.randrange(1000), 4), (0x32870, 0, 4), (0x32874, 0, 4)]
    head = PLAYER_RECORDS + 0x20 + (args[1] + player * (PLAYER_RECORD_BYTES // 2)) * 2
    length = 0 if rng.random() < 0.1 else rng.randint(1, 10)
    settlers = rng.sample(range(1, 0x3000), length)
    fields.append((head, settlers[0] if settlers else 0, 2))
    owners = set()
    for i, settler in enumerate(settlers):
        s = SETTLERS + settler * SETTLER_BYTES
        sx = min(383, max(0, x + rng.randint(-40, 40)))
        sy = min(383, max(0, y + rng.randint(-40, 40)))
        owner = rng.randrange(20)
        owners.add(owner)
        fields += [(s + 0x12, settlers[i - 1] if i else 0, 2),
                   (s + 0x10, settlers[i + 1] if i + 1 < length else 0, 2),
                   (s + 0x4, sx, 2), (s + 0x6, sy, 2), (s + 0x0, owner, 1),
                   (tile(sx, sy) + 0x10, region if rng.random() < 0.7 else 3 - region, 1)]
    for owner in owners:
        for i in range(10):
            fields.append((0x11940bc + owner * 40 + i * 4, rng.randrange(4), 4))
    return fields


def worker_globals(rng, pool, args):
    return [(0x7ad620 + i * 2, rng.randrange(4), 2) for i in range(10)]


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
    # batch 17
    Spec("GameWorld_PileIncAmountLeaving", 0x005058c0, "thiscall", [between(1, 0xfff)], presets=pile_leaving,
         max_cases=1000),
    Spec("GameWorld_LogPlayerLandResources", 0x00507f60, "thiscall", [below(20)], presets=player_on_map,
         max_cases=6),
    Spec("Landscape_FindOrAddColour", 0x0054c940, "thiscall", [colour_arg], presets=colour_table,
         max_cases=500),
    Spec("CLogicThread_DrawOverlayLine", 0x00523560, "logicthread", line_args(), presets=line_map,
         max_cases=500),
    Spec("CLogicThread_DrawWorkAreaOverlay", 0x00523840, "logicthread", [between(1, 0x100)],
         presets=work_area, max_cases=150),
    Spec("GameWorld_SplitTileWord16Region", 0x0051c1f0, "thiscall", [below(12), below(12)], eax_mask=0,
         presets=split_regions(0x16, 2, 0xe125e4, 0xe125e8, 0x2000, 0.1)),
    Spec("GameWorld_SplitTileByte10Region", 0x0051caf0, "thiscall", [below(12), below(12)], eax_mask=0,
         presets=split_regions(0x10, 1, 0xe114d8, 0xe114dc, 0x100, 0.1)),
    Spec("GameWorld_SplitWord16RegionsAround", 0x0051c080, "thiscall", [between(1, 10), between(1, 10)],
         presets=split_regions(0x16, 2, 0xe125e4, 0xe125e8, 0x2000, True), max_cases=200),
    Spec("GameWorld_SendNearestWorkerToBuilding", 0x004db500, "thiscall",
         [between(1, 0x800), below(40), lambda rng, pool: rng.choice([rng.randrange(70), rng.getrandbits(32)])],
         presets=worker_and_list, globals=worker_globals, max_cases=1000),
]


# ---------------------------------------------------------------------------
# batch 18
# ---------------------------------------------------------------------------

BUILDING_EXTRAS = 0x10f45e8
BUILDING_EXTRA_BYTES = 0x5a
AREA_HIGH_WATER = 0xe115dc
PLAYER_RACE = PLAYER_RECORDS + 4


def pending_type(rng, pool):
    return rng.choice([4, 4, 4, 6, rng.randrange(10), anything(rng, pool)])


def pending_rings(rng, pool, args):
    """The 12-byte ring: read/write indices (sometimes the write index at the
    end), entries in between often {4, 0..3}."""
    read = rng.choice([rng.randrange(1000), 999, 995])
    length = rng.choice([0, 1, rng.randrange(12)])
    write = (read + length) % 1000
    fields = []
    for i in range(length):
        entry = PENDING_RING + ((read + i) % 1000) * 12
        fields += [(entry, 4 if rng.random() < 0.5 else rng.randrange(10), 4), (entry + 4, rng.randrange(4), 4)]
    if rng.random() < 0.1:
        write = 999
    return fields + [(0x32870, read, 4), (0x32874, write, 4)]


def building_route(rng, pool, args):
    """The building's type (mostly 0x21/0x22), its start words, its extra
    record with waypoints (sometimes unset) and end point (rarely unset)."""
    record = BUILDINGS + args[0] * BUILDING_BYTES
    number = rng.randrange(1, 999)
    extra = BUILDING_EXTRAS + number * BUILDING_EXTRA_BYTES
    n = rng.choice([384, rng.randrange(1, 384)])
    fields = [(MAP_SIZE, n, 4), (record + 0x6, rng.choice([0x21, 0x22, 0x21, 0x22, 0, 0x20, 0xff]), 1),
              (record + 0x34, number, 2), (SEARCH_GENERATION, 0xff if rng.random() < 0.05 else rng.randrange(255), 1)]
    for field in (0x2, 0x4, 0x30, 0x32):
        fields.append((record + field, rng.randrange(384), 2))
    for point in range(4):
        unset = rng.random() < (0.1 if point == 3 else 0.3)
        fields += [(extra + 0x2 + point * 2, 0xffff if unset else rng.randrange(384), 2),
                   (extra + 0xa + point * 2, rng.randrange(384), 2)]
    return fields


def fetch_goods(rng, pool, args):
    """(area, building, goods): the area's piles of the goods type near the
    building (on tiles of that area), carriers in the area's list, and the
    building's six piles as a ring."""
    area, building, goods = args
    record = BUILDINGS + building * BUILDING_BYTES
    x, y = rng.randrange(32, 352), rng.randrange(32, 352)
    fields = [(MAP_SIZE, 384, 4), (BUILDING_COUNT, rng.randrange(4), 4), (record + 0x2, x, 2),
              (record + 0x4, y, 2), (record + 0x34, rng.randrange(1, 999), 2)]
    head = AREAS + area * AREA_BYTES + 0xa + goods * 2
    length = 0 if rng.random() < 0.1 else rng.randint(1, 8)
    piles = rng.sample(range(1, 0x800), length)
    fields.append((head, piles[0] if piles else 0, 2))
    for i, pile in enumerate(piles):
        r = PILES + pile * PILE_BYTES
        px, py = x + rng.randint(-30, 30), y + rng.randint(-30, 30)
        amount = rng.randrange(10)
        leaving = amount if rng.random() < 0.25 else rng.randrange(10)
        fields += [(r + 0xc, piles[i + 1] if i + 1 < length else 0, 2), (r + 0x6, px, 2), (r + 0x8, py, 2),
                   (r + 0x1, amount, 1), (r + 0x2, leaving, 1), (r + 0x4, rng.choice([0xff, 0xff, 0, 0xfe, 1]), 1),
                   (r + 0x0, goods, 1), (tile_area_offset(px, py), area, 2)]
    fields += waiting_list(rng, AREAS + area * AREA_BYTES + 0x12a, x, y)
    ring = [rng.randrange(0x800, 0x1000) for _ in range(6)]
    fields.append((record + 0xe, ring[0], 2))
    for k, pile in enumerate(ring):
        r = PILES + pile * PILE_BYTES
        c = rng.randrange(4)
        fields += [(r + 0xc, ring[(k + 1) % 6], 2),
                   (r + 0x0, goods if c == 0 else 0xff if c == 1 else rng.randrange(36), 1),
                   (r + 0x1, rng.randrange(9), 1), (r + 0x2, rng.randrange(0x80), 1)]
    return fields


def init_area(rng, pool, args):
    race = rng.choice([0, 1, 2, 3, 0, 1, 2, 3, 4, rng.getrandbits(32)])
    return event_rings(rng, pool, args) + [(PLAYER_RACE + args[1] * PLAYER_RECORD_BYTES, race, 4)]


def alloc_area(rng, pool, args):
    """Areas 1..first-1 used, `first` free: near the start, 0xffe, 0xfff
    (never handed out) or none."""
    r = rng.random()
    if r < 0.1:
        first = 0xffe
    elif r < 0.2:
        first = 0xfff
    elif r < 0.3:
        first = 0x1000
    else:
        first = rng.randrange(1, 64)
    fields = event_rings(rng, pool, args)
    for area in range(1, min(first, 0xfff) + 1):
        fields.append((AREAS + area * AREA_BYTES, 0xff if area == first else rng.randrange(20), 1))
    fields.append((AREA_HIGH_WATER, rng.choice([first - 1, first, first + 1, rng.getrandbits(32)]) & 0xFFFFFFFF, 4))
    fields.append((PLAYER_RACE + args[0] * PLAYER_RECORD_BYTES, rng.randrange(4), 4))
    return fields


def area_lists(rng, area: int, n: int, counters: dict) -> list:
    """Valid terminated lists of area `area` (pile list +0x8, 36 goods pile
    lists, 48 settler lists), mostly empty, members on tiles of the map."""
    r = AREAS + area * AREA_BYTES
    fields = []

    def fill(head, table, size, x_field, y_field, previous_field, next_field, key):
        length = rng.randint(1, 4) if rng.random() < 0.2 else 0
        fields.append((head, counters[key] if length else 0, 2))
        previous = 0
        for k in range(length):
            member = counters[key]
            counters[key] += 1
            record = table + member * size
            fields.extend([(record + previous_field, previous, 2),
                           (record + next_field, counters[key] if k + 1 < length else 0, 2),
                           (record + x_field, rng.randrange(n), 2), (record + y_field, rng.randrange(n), 2)])
            previous = member

    fill(r + 0x8, PILES, PILE_BYTES, 0x6, 0x8, 0xa, 0xc, "pile")
    for i in range(36):
        fill(r + 0xa + 2 * i, PILES, PILE_BYTES, 0x6, 0x8, 0xa, 0xc, "pile")
    for i in range(48):
        fill(r + 0x12a + 2 * i, SETTLERS, SETTLER_BYTES, 0x4, 0x6, 0x12, 0x10, "settler")
    return fields


def area_map(rng, n: int, full: bool = False, stripes: bool = False) -> list:
    """An n x n map in areas 1..3 (patches, some tiles in none), the frame off
    the map in none; areas 1..3 used with valid lists, 4..16 used or free
    (16 free unless `full`), races 0..3 (sometimes other), the real offsets
    in entries 0..18. With `stripes` every tile is in an area and each area
    is one connected stripe (merging assumes connected areas: a second
    piece of a merged area would link its stale lists twice)."""
    patch = rng.randint(1, 4)
    empty = rng.randrange(3)
    orientation = rng.randrange(3)
    first = rng.randint(-2, n)
    second = first + rng.randint(1, n)
    fields = [(MAP_SIZE, n, 4)]
    for k, (dx, dy) in enumerate(AROUND + RING2):
        fields += [(NEIGHBOUR_OFFSETS + k * 16, dx & 0xFFFFFFFF, 4),
                   (NEIGHBOUR_OFFSETS + k * 16 + 4, dy & 0xFFFFFFFF, 4)]
    for x in range(-1, n + 1):
        for y in range(-1, n + 1):
            area = 0
            if 0 <= x < n and 0 <= y < n and stripes:
                f = x if orientation == 0 else y if orientation == 1 else x - y
                area = 1 if f < first else 2 if f < second else 3
            elif 0 <= x < n and 0 <= y < n and rng.randrange(8) >= empty:
                area = 1 + ((x // patch) * 5 + (y // patch) * 3 + (1 if rng.random() < 1 / 6 else 0)) % 3
            xx, yy = x & 0xFFFFFFFF, y & 0xFFFFFFFF
            fields += [(tile_area_offset(xx, yy), area, 2), (tile(xx, yy) + 0xa, rng.getrandbits(8), 1),
                       (tile(xx, yy) + 0x8, rng.randrange(20), 1)]
    counters = {"pile": rng.randrange(1, 0x1000), "settler": rng.randrange(1, 0x1000)}
    for area in range(1, 17):
        used = area <= 3 or (area < 16 and rng.random() < 0.5) or full
        fields.append((AREAS + area * AREA_BYTES, rng.randrange(20) if used else 0xff, 1))
        if area <= 3:
            fields += area_lists(rng, area, n, counters)
    for player in range(20):
        race = rng.getrandbits(32) if rng.random() < 0.1 else rng.randrange(4)
        fields.append((PLAYER_RACE + player * PLAYER_RECORD_BYTES, race, 4))
    fields.append((AREA_HIGH_WATER, rng.randrange(18), 4))
    return fields


def two_areas() -> list[Generator]:
    """(old, new): two different areas of 1..3."""
    state = {}

    def old(rng, pool):
        state["old"] = rng.randint(1, 3)
        return state["old"]

    def new(rng, pool):
        return rng.choice([a for a in (1, 2, 3) if a != state["old"]])

    return [old, new]


def move_area_contents(rng, pool, args):
    return event_rings(rng, pool, args) + area_map(rng, rng.randint(2, 10))


def split_area(rng, pool, args):
    """The tile (args) on an n x n map (sometimes on its edge), in one of the
    areas 1..3."""
    n = rng.randint(max(args[0], args[1]) + 1, 10)
    fields = event_rings(rng, pool, args) + area_map(rng, n, full=rng.random() < 0.03)
    return fields + [(tile_area_offset(args[0], args[1]), rng.randint(1, 3), 2)]


def word16_regions(rng, n: int) -> list:
    """Word-16 regions: each connected stretch of random walkable tiles its
    own id (a shared id could be freed and handed out again while in use:
    the flood fill would loop for ever), ids in use marked; byte-10 regions:
    one region over the whole map (one split per call is safe)."""
    density = rng.randrange(4, 9)
    walk = {(x, y): rng.randrange(8) < density for x in range(n) for y in range(n)}
    label = {}
    next_id = 1
    for start in walk:
        if not walk[start] or start in label:
            continue
        label[start] = next_id
        stack = [start]
        while stack:
            x, y = stack.pop()
            for dx, dy in AROUND[1:]:
                q = (x + dx, y + dy)
                if walk.get(q) and q not in label:
                    label[q] = next_id
                    stack.append(q)
        next_id += 1
    fields = []
    for x in range(-1, n + 1):
        for y in range(-1, n + 1):
            t = tile(x & 0xFFFFFFFF, y & 0xFFFFFFFF)
            on = 0 <= x < n and 0 <= y < n
            fields += [(t + 0x16, label.get((x, y), 0), 2), (t + 0x10, 1 if on and rng.random() < 7 / 8 else 0, 1)]
    for i in range(next_id + 2):
        fields.append((0xe125e8 + i, 1 if 0 < i < next_id else 0, 1))
    fields.append((0xe125e4, rng.randrange(next_id + 2), 4))
    for i in range(8):
        fields.append((0xe114dc + i, 1 if i == 1 else 0, 1))
    fields.append((0xe114d8, rng.randrange(4), 4))
    return fields


def block_tile(rng, pool, args):
    """As split_area, plus regions; the tile mostly not yet blocked,
    sometimes with a marked (>= 0x8000) or no area."""
    x, y = args
    n = rng.randint(max(x, y) + 1, 10)
    fields = event_rings(rng, pool, args) + area_map(rng, n, full=rng.random() < 0.03) + word16_regions(rng, n)
    r = rng.random()
    area = rng.randint(1, 3) if r < 0.75 else (0x8000 + rng.randrange(4)) if r < 0.88 else 0
    flags = rng.getrandbits(8)
    flags = flags | 1 if rng.random() < 0.15 else flags & 0xFE
    return fields + [(tile_area_offset(x, y), area, 2), (tile(x, y) + 0xa, flags, 1)]


def merge_area_contents(rng, pool, args):
    fields = event_rings(rng, pool, args) + area_map(rng, rng.randint(2, 10))
    return fields + [(AREAS + area * AREA_BYTES + 0x4, rng.randrange(4), 4) for area in args]


def join_areas_map(rng, n: int) -> list:
    """An n x n map of connected stripe areas; owners of 1..3 players on the
    map's tiles, none (0xfe) on the frame; most tiles not blocked."""
    fields = event_rings(rng, None, None) + area_map(rng, n, full=rng.random() < 0.03, stripes=True)
    owners = rng.randint(1, 3)
    for tx in range(-1, n + 1):
        for ty in range(-1, n + 1):
            t = tile(tx & 0xFFFFFFFF, ty & 0xFFFFFFFF)
            on = 0 <= tx < n and 0 <= ty < n
            flags = rng.getrandbits(8)
            fields += [(t + 0x8, rng.randrange(owners) if on else 0xfe, 1),
                       (t + 0xa, flags if rng.random() < 0.25 else flags & 0xFE, 1)]
    return fields


def join_areas(rng, pool, args):
    """The tile (args, inside a join_areas_map) without an area."""
    x, y = args
    fields = join_areas_map(rng, rng.randint(max(x, y) + 2, 10))
    return fields + [(tile(x, y) + 0x8, rng.randrange(3), 1), (tile_area_offset(x, y), 0, 2)]


def unblock_tile(rng, pool, args):
    """As join_areas plus regions (word 16 labelled per connected stretch,
    byte 10 one region); the tile 3 inside the map, mostly blocked, often
    without byte-10 region or area, sometimes without owner."""
    x, y = args
    n = rng.randint(max(x, y) + 4, 12)
    fields = join_areas_map(rng, n) + word16_regions(rng, n)
    flags = rng.getrandbits(8)
    flags = flags | 1 if rng.random() < 0.85 else flags & 0xFE
    area = 0 if rng.random() < 0.75 else rng.randint(1, 3)
    owner = 0xff if rng.random() < 0.12 else rng.randrange(3)
    return fields + [(tile(x, y) + 0xa, flags, 1), (tile(x, y) + 0x10, rng.choice([0, 1]), 1),
                     (tile_area_offset(x, y), area, 2), (tile(x, y) + 0x8, owner, 1)]


def free_tile_owner(rng, pool, args):
    """(x, y, owner): the tile in a join map, mostly without owner and area,
    sometimes with a pile (small goods type and amount)."""
    x, y, _ = args
    n = rng.randint(max(x, y) + 2, 10)
    fields = join_areas_map(rng, n)
    pile = 0 if rng.random() < 0.5 else rng.randrange(0x2000, 0x3000)
    fields += [(tile(x, y) + 0x8, 0xff if rng.random() < 0.8 else rng.randrange(3), 1),
               (tile_area_offset(x, y), 0 if rng.random() < 0.8 else rng.randint(1, 3), 2), (tile(x, y) + 0x2, pile, 2)]
    if pile:
        fields += [(PILES + pile * PILE_BYTES, rng.randrange(36), 1), (PILES + pile * PILE_BYTES + 1, rng.randrange(9), 1)]
    return fields


SPECS += [
    Spec("GameWorld_UnblockTile", 0x0051b770, "thiscall", [between(3, 8), between(3, 8)], presets=unblock_tile,
         max_cases=300),
    Spec("GameWorld_SetOwnerOfFreeTile", 0x0051b5d0, "thiscall",
         [between(1, 8), between(1, 8), lambda rng, pool: rng.choice([0, 1, 2, 0xff, 0x100, rng.getrandbits(32)])],
         presets=free_tile_owner, max_cases=400),
    Spec("GameWorld_MergeAreaContents", 0x004e5a20, "thiscall", two_areas(), eax_mask=0,
         presets=merge_area_contents, max_cases=300),
    Spec("GameWorld_JoinAreasAtTile", 0x004e6150, "thiscall", [between(1, 8), between(1, 8)], eax_mask=0,
         presets=join_areas, max_cases=400),
    Spec("GameWorld_QueuePendingEvent", 0x004e8ba0, "thiscall", [pending_type, below(4, 0.1), anything],
         presets=pending_rings),
    Spec("CLogicThread_DrawBuildingRouteOverlay", 0x005236b0, "logicthread", [between(1, 0x100)], eax_mask=0,
         presets=building_route, max_cases=400),
    Spec("GameWorld_FetchGoodsToTradeBuilding", 0x004de0a0, "thiscall",
         [between(1, 0x40), between(1, 0x800), below(36)], eax_mask=0, presets=fetch_goods, max_cases=1000),
    Spec("GameWorld_InitArea", 0x004e6510, "thiscall",
         [between(1, 0xffe), lambda rng, pool: rng.choice([rng.randrange(20), rng.randrange(256)])], eax_mask=0,
         presets=init_area, max_cases=500),
    Spec("GameWorld_AllocArea", 0x004e64c0, "thiscall", [below(20)], presets=alloc_area, max_cases=300),
    Spec("GameWorld_MoveAreaContents", 0x004e54f0, "thiscall", two_areas(), eax_mask=0,
         presets=move_area_contents, max_cases=300),
    Spec("GameWorld_SplitAreaAtTile", 0x004e5d50, "thiscall", [below(10), below(10)], eax_mask=0,
         presets=split_area, max_cases=400),
    Spec("GameWorld_BlockTile", 0x0051b6f0, "thiscall", [below(10), below(10)], presets=block_tile,
         max_cases=300),
]


# --- batch 19 ---------------------------------------------------------------

SETTLER_HIGH_WATER = 0x12a5e5c
LAST_SETTLER = 0x7cff  # never handed out


def settler_health(rng, pool, args):
    """The settler's race (+0x2) often 3, its type (+0x1) mostly a real one."""
    record = SETTLERS + args[0] * SETTLER_BYTES
    return [(record + 0x2, 3 if rng.random() < 0.5 else rng.randrange(256), 1),
            (record + 0x1, rng.randrange(0x50) if rng.random() < 0.8 else rng.randrange(256), 1)]


def free_settler(rng) -> list:
    """Settlers 1..first-1 in use, `first` free: near the start, 0x7cfe,
    0x7cff (never handed out) or none."""
    r = rng.random()
    if r < 0.05:
        first = LAST_SETTLER - 1
    elif r < 0.1:
        first = LAST_SETTLER
    elif r < 0.15:
        first = LAST_SETTLER + 1
    else:
        first = rng.randrange(1, 64)
    fields = [(SETTLERS + s * SETTLER_BYTES, 0xff if s == first else rng.randrange(20), 1)
              for s in range(1, min(first, LAST_SETTLER) + 1)]
    fields.append((SETTLER_HIGH_WATER, rng.choice([first - 1, first, first + 1, rng.getrandbits(32)]) & 0xFFFFFFFF, 4))
    return fields


def create_settler(rng, pool, args):
    race = rng.choice([0, 1, 2, 3, 3, 3, rng.getrandbits(32)])
    return free_settler(rng) + [(PLAYER_RACE + args[3] * PLAYER_RECORD_BYTES, race, 4)]


def pile_on_map(rng, pool, args):
    record = PILES + args[0] * PILE_BYTES
    return [(record + 0x6, rng.randrange(384), 2), (record + 0x8, rng.randrange(384), 2),
            (record + 0x3, rng.choice([0, 0xff, rng.randrange(256)]), 1)]


def flood_assign(rng, pool, args):
    """Tiles mostly marked (flag 0x10, not blocked) and owned by the player
    (args[3]), area words 0..3; the big map all marked and the player's (a
    fill of all of it wraps the 10,000-entry ring)."""
    n, big = flood_map(rng, args)
    player = args[3]
    density = 1.0 if big else rng.choice([0.3, 0.6, 0.9])
    fields = [(MAP_SIZE, n, 4)]
    for x in range(n):
        for y in range(n):
            flags = rng.getrandbits(8)
            if rng.random() < density:
                flags = (flags | 0x10) & ~0x01
            owner = player if big or rng.random() < 0.75 else rng.randrange(3)
            fields += [(tile(x, y) + 0xa, flags, 1), (tile(x, y) + 0x8, owner, 1),
                       (tile_area_offset(x, y), rng.randrange(4), 2)]
    return fields


EXTRA_OFFSETS = {0x1f: (-2, 4), 0x2f: (-2, 6), 0x30: (-2, 4), 0x32: (-1, 7)}  # S3.EXE 0x659624
EXTRA_STEPS = [(-1, -1), (0, -1), (-1, 0), (0, 0), (1, 0), (0, 1), (1, 1)]
SHIP_STEPS = [(1, 0), (1, 1), (0, 1), (-1, 0), (-1, -1), (0, -1)]  # NeighbourX/Y per direction


def extra_tiles_map(rng, n: int) -> list:
    """A join map with word-16 regions, one byte-10 region over the map
    (connected: a call may split it several times) and few occupants
    (tile word +0x0), their settlers often of type 0x29."""
    fields = join_areas_map(rng, n) + word16_regions(rng, n)
    for x in range(-1, n + 1):
        for y in range(-1, n + 1):
            t = tile(x & 0xFFFFFFFF, y & 0xFFFFFFFF)
            on = 0 <= x < n and 0 <= y < n
            occupant = rng.randrange(1, 0x8000) if rng.random() < 1 / 40 else 0
            fields += [(t + 0x10, 1 if on else 0, 1), (t, occupant, 2)]
            if occupant:
                fields.append((SETTLERS + occupant * SETTLER_BYTES + 1,
                               0x29 if rng.random() < 0.5 else rng.randrange(0x50), 1))
    return fields


def extra_tiles_building(rng, n: int, building: int, unset: bool):
    """A building with extra tiles (seven tiles 4 inside the map), a shipyard
    (ship tile 4 inside the map; with `unset` the ship position sometimes
    unset, which only the take function checks) or another type. Returns
    (fields, extra tiles or [])."""
    record = BUILDINGS + building * BUILDING_BYTES
    kind = rng.choice([0x1f, 0x2f, 0x30, 0x32, 0x20, 0x20, 0x20, 0x12]) if rng.random() > 0.1 else rng.randrange(256)
    state = rng.choice([0x20, 0x30, 0x40, 0, 0x30, 0x40, 0x50]) | (0 if rng.random() < 0.5 else rng.randrange(16))
    if rng.random() < 0.1:
        state = 0
    fields = [(record + 0x6, kind, 1), (record + 0x0, rng.randrange(20), 1),
              (record + 0x20, 0xfe if rng.random() < 0.125 else rng.randrange(0xfe), 1),
              (record + 0x1f, state, 1), (record + 0x36, rng.randrange(0x10, 0x20), 1)]
    extra = []
    if kind in EXTRA_OFFSETS:
        dx, dy = EXTRA_OFFSETS[kind]
        cx, cy = rng.randrange(4, n - 4), rng.randrange(8, n - 4)  # y = cy - dy is a word: not negative
        fields += [(record + 0x2, (cx - dx) & 0xFFFF, 2), (record + 0x4, (cy - dy) & 0xFFFF, 2)]
        extra = [(cx + sx, cy + sy) for sx, sy in EXTRA_STEPS]
    else:
        d = rng.randrange(6)
        tx, ty = rng.randrange(4, n - 4), rng.randrange(4, n - 4)
        sx, sy = SHIP_STEPS[d]
        fields += [(record + 0x22, (d + 3) % 6 + 6 * rng.randrange(100), 2),
                   (record + 0x30, 0xffff if unset and rng.random() < 0.1 else tx - 2 * sx, 2),
                   (record + 0x32, ty - 2 * sy, 2),
                   (tile(tx, ty), rng.randrange(1, 0x8000) if rng.random() < 0.25 else 0, 2)]
    return fields, extra


def take_extra_tiles(rng, pool, args):
    n = rng.randint(14, 18)
    fields, _ = extra_tiles_building(rng, n, args[0], True)
    return [(MAP_SIZE, n, 4)] + extra_tiles_map(rng, n) + fields


def release_extra_tiles(rng, pool, args):
    """As take_extra_tiles, the extra tiles mostly blocked (area kept; the
    centre sometimes without area, which can't cut an area in two)."""
    n = rng.randint(14, 18)
    fields, extra = extra_tiles_building(rng, n, args[0], False)
    fields = [(MAP_SIZE, n, 4)] + extra_tiles_map(rng, n) + fields
    for k, (x, y) in enumerate(extra):
        if rng.random() < 7 / 8:
            fields.append((tile(x, y) + 0xa, rng.getrandbits(8) | 1, 1))
        if k == 3 and rng.random() < 0.3:
            fields.append((tile_area_offset(x, y), 0, 2))
    return fields


def type12_buildings(rng, pool, args):
    """1..5 buildings (sometimes none), mostly of type 0x12, of players of
    race 0 or 1; their tiles (x-2, y+3), (x, y+7), (x, y+6) inside the map,
    mostly blocked."""
    n = rng.randint(16, 22)
    fields = [(MAP_SIZE, n, 4)] + extra_tiles_map(rng, n)
    count = 0 if rng.random() < 0.1 else rng.randint(1, 5)
    fields.append((BUILDING_COUNT, count, 4))
    for building in range(1, count + 1):
        record = BUILDINGS + building * BUILDING_BYTES
        player = rng.randrange(20)
        x, y = rng.randrange(5, n - 3), rng.randrange(3, n - 10)
        fields += [(record + 0x6, 0x12 if rng.random() < 2 / 3 else rng.randrange(0x40), 1), (record + 0x0, player, 1),
                   (PLAYER_RACE + player * PLAYER_RECORD_BYTES, rng.randrange(4) if rng.random() < 1 / 6 else rng.randrange(2), 4),
                   (record + 0x2, x, 2), (record + 0x4, y, 2)]
        for tx, ty in ((x - 2, y + 3), (x, y + 7), (x, y + 6)):
            if rng.random() < 0.75:
                fields.append((tile(tx, ty) + 0xa, rng.getrandbits(8) | 1, 1))
    return fields


SPECS += [
    Spec("GameWorld_SettlerStartHealth", 0x00510470, "thiscall", [below(LAST_SETTLER + 1)], presets=settler_health),
    Spec("GameWorld_CreateSettler", 0x0050c9e0, "thiscall",
         [below(384), below(384), lambda rng, pool: rng.randrange(0x50) if rng.random() < 0.8 else anything(rng, pool),
          below(20)], presets=create_settler, max_cases=300),
    Spec("GameWorld_ClearSettlerJob", 0x00510440, "thiscall", [below(LAST_SETTLER + 1)]),
    Spec("GameWorld_RestoreSettlerDirection", 0x0050e380, "thiscall", [below(LAST_SETTLER + 1)]),
    Spec("GameWorld_PileIncAmountComing", 0x00505bc0, "thiscall", [below(0x10000)], presets=pile_on_map),
    Spec("GameWorld_PileDecAmountComing", 0x00505bf0, "thiscall", [below(0x10000)], presets=pile_on_map),
    Spec("GameWorld_RemovePileFromTile", 0x00504650, "thiscall", [below(0x10000)], presets=pile_on_map),
    Spec("GameWorld_ClearAreaGoodsSettings1D2", 0x004ff3c0, "thiscall", [below(0x1000)]),
    Spec("GameWorld_FloodAssignMarkedTiles", 0x004e6cd0, "thiscall",
         [below(12), below(12), between(1, 6), below(3)], presets=flood_assign, max_cases=150),
    Spec("GameWorld_TakeBuildingExtraTiles", 0x004dd6f0, "thiscall", [between(1, 64)], presets=take_extra_tiles,
         max_cases=300),
    Spec("GameWorld_ReleaseBuildingExtraTiles", 0x004dda10, "thiscall", [between(1, 64)],
         presets=release_extra_tiles, max_cases=300),
    Spec("GameWorld_UnblockType12BuildingTiles", 0x004dfa60, "thiscall", [], presets=type12_buildings,
         max_cases=200),
]


# --- batch 20 ---------------------------------------------------------------

NGO_OBJECTIVES = 0x1195dcd


def ngo_id(rng, pool):
    """Objective ids 0..39, sometimes with high bits (only the low byte counts)."""
    return rng.randrange(40) | (rng.getrandbits(32) & ~0xFF if rng.random() < 0.25 else 0)


def ngo_objectives(rng, pool, args):
    return [(NGO_OBJECTIVES + k, rng.randrange(40), 1) for k in range(8)]


def job_value(rng, pool):
    r = rng.random()
    if r < 0.4:
        return rng.choice([1, 0x37, 2, 0x35, 0, 0x36, 0x38])
    if r < 0.6:
        return (rng.getrandbits(32) & ~0xFF) | rng.choice([1, 0x37])
    return anything(rng, pool)


AREA_SPAN = 12  # settlers stand on tiles 0..11 x 0..11


def area_settlers(rng, pool, args):
    """Up to 40 settlers of players 0..3 with types 0..0x2f (sometimes any)
    near (0, 0), most on their own tile; tile areas and the area's player
    0..3 (sometimes free); count 0..40."""
    count = rng.choice([0, 1, rng.randrange(41), rng.randrange(41)])
    area = args[0]
    fields = [(SETTLER_COUNT, count, 4),
              (AREAS + area * AREA_BYTES, 0xff if rng.random() < 0.125 else rng.randrange(4), 1)]
    for x in range(AREA_SPAN):
        for y in range(AREA_SPAN):
            fields.append((tile_area_offset(x, y), rng.getrandbits(16) if rng.random() < 1 / 16 else rng.randrange(4), 2))
            fields.append((tile(x, y), 0, 2))
    for s in range(1, count + 1):
        record = SETTLERS + s * SETTLER_BYTES
        x, y = rng.randrange(AREA_SPAN), rng.randrange(AREA_SPAN)
        fields += [(record, rng.randrange(4), 1),
                   (record + 1, rng.randrange(256) if rng.random() < 0.1 else rng.randrange(0x30), 1),
                   (record + 4, x, 2), (record + 6, y, 2)]
        if rng.random() < 2 / 3:
            fields.append((tile(x, y), s, 2))
    return fields


def area_settler_type(rng, pool):
    return rng.randrange(0x30) | (0x100 if rng.random() < 0.125 else 0)


def linked_pile(rng, pool, args):
    """The pile on a tile of the map (area below AREA_LIMIT), kind, amount
    and leaving count often in the relinking state, links and the list
    heads often 0."""
    record = PILES + args[0] * PILE_BYTES
    x, y = rng.randrange(384), rng.randrange(384)
    area = rng.randrange(AREA_LIMIT)
    leaving = rng.getrandbits(8) if rng.random() < 0.125 else rng.randrange(9)
    goods = rng.getrandbits(8) if rng.random() < 0.1 else rng.randrange(36)

    def link():
        return 0 if rng.random() < 1 / 3 else rng.randrange(1, 0x1000)
    return [(record + 4, rng.choice([0xff, 0xfe, 0xff, 0xfe, 0, 1, 0xfd]), 1),
            (record + 2, leaving, 1),
            (record + 1, leaving if rng.random() < 0.5 else rng.randrange(9), 1),
            (record + 0, goods, 1), (record + 6, x, 2), (record + 8, y, 2),
            (tile_area_offset(x, y), area, 2),
            (record + 0xa, link(), 2), (record + 0xc, link(), 2),
            (AREAS + 0xa + (area * 0x170 + goods) * 2, link(), 2),
            (AREAS + area * AREA_BYTES + 0x8, link(), 2)]


def terrain_value(rng, pool):
    return rng.getrandbits(32) if rng.random() < 0.1 else rng.randrange(4)


def small_terrain_map(rng, pool, args):
    """Maps of size -4..3 (no loop) or 4..20, terrain bytes 0..3."""
    n = rng.randrange(-4, 4) if rng.random() < 0.1 else rng.randint(4, 20)
    fields = [(MAP_SIZE, n & 0xFFFFFFFF, 4)]
    for x in range(max(n, 0)):
        for y in range(max(n, 0)):
            fields.append((tile(x, y) + 0x6, rng.randrange(4), 1))
    return fields


def shade_heights(rng, pool, args):
    """Heights of the tile and its neighbours: near each other (all branches
    of the difference mapping) or anywhere (clamping)."""
    x, y = args
    wide = rng.random() < 0.25
    base = rng.randrange(256)
    return [(tile(x + dx, y + dy) + 0x4, rng.randrange(256) if wide else (base + rng.randrange(25)) & 0xFF, 1)
            for dx in (-1, 0, 1) for dy in (-1, 0, 1)]


SPECS += [
    Spec("GameWorld_IsNgoObjectiveChosen", 0x004ffb90, "thiscall", [ngo_id], eax_mask=0xFF, presets=ngo_objectives),
    Spec("GameWorld_GoodsRequestPriority", 0x004ff380, "thiscall", [anything, anything]),
    Spec("GameWorld_IsSettlerJob01Or37", 0x004fa6b0, "thiscall", [job_value]),
    Spec("GameWorld_JobAfter01Or37", 0x004faa40, "thiscall", [job_value]),
    Spec("GameWorld_CountAreaSettlersOfClass0", 0x0050e750, "thiscall", [below(4)], presets=area_settlers,
         max_cases=500),
    Spec("GameWorld_CountAreaSettlersOfType", 0x0050e820, "thiscall", [below(4), area_settler_type],
         presets=area_settlers, max_cases=500),
    Spec("GameWorld_PileDecAmountLeaving", 0x00505ac0, "thiscall", [between(1, 0xfff)], presets=linked_pile),
    Spec("GameWorld_MakePileEmpty", 0x00506080, "thiscall", [between(1, 0xfff)], presets=linked_pile),
    Spec("GameWorld_ReplaceTerrainNextTo", 0x00519270, "thiscall", [terrain_value, terrain_value, anything],
         eax_mask=0, presets=small_terrain_map, max_cases=300),
    Spec("GameWorld_TileShade", 0x00519720, "thiscall", [between(1, 380), between(1, 380)], presets=shade_heights),
    Spec("MapEffect_DefaultConstruct", 0x00543830, "thiscall", []),
    Spec("MapEffect_Construct", 0x00543880, "thiscall", [anything] * 20),
]



# --- std::string (MSVC 6.0, src/s3re/msvc6/string.cpp) ----------------------
# Strings live in the world area: `this` (thiscall) at world offset 0 with its
# buffer in scratch slot 4, a second string in slot 0 with its buffer in slot
# 1 (or sharing this one's), C text in slot 2, an operator+ result in slot 3.
# Buffers come from the emulator's fake heap when the functions allocate.

STRING_SELF = 0x0
STRING_OTHER = SCRATCH
STRING_OTHER_BUFFER = SCRATCH + 1 * SCRATCH_SLOT_BYTES
STRING_TEXT = SCRATCH + 2 * SCRATCH_SLOT_BYTES
STRING_RESULT = SCRATCH + 3 * SCRATCH_SLOT_BYTES
STRING_SELF_BUFFER = SCRATCH + 4 * SCRATCH_SLOT_BYTES


def string_length(rng) -> int:
    if rng.random() < 0.5:
        return rng.choice([0, 0, 1, 2, 3, 0x1e, 0x1f, 0x20, 0x21, 0x3f, 0x40])
    return rng.randrange(0x60)


def string_fields(rng, offset: int, buffer: int, shared_ptr: int | None = None,
                  overlong: bool = False):
    """A std::string object at world `offset`: mostly consistent (length <=
    capacity, a count byte before the text, the text, its zero), sometimes
    odd (no buffer but a length, text with an earlier or later zero, frozen
    or nearly full counts; with `overlong`, sometimes 0xFFFFFFFF characters,
    which reaches std::_Xlen when copied from position 1). An over-long
    string's text pointer is unmapped, so whatever reads it faults at once
    instead of copying gigabytes. Returns (fields, ptr)."""
    length = string_length(rng)
    capacity = rng.choice([length, length, max(length, 0x1f), length + rng.randrange(0x30),
                           rng.randrange(0x60)])
    fields = [(offset, rng.getrandbits(8), 1)]
    roll = rng.random()
    if roll < 0.12:
        ptr = 0
        if rng.random() < 0.6:
            length, capacity = 0, 0
    elif shared_ptr and roll < 0.4:
        ptr = shared_ptr
    else:
        ptr = WORLD_BASE + buffer + 1
        count = rng.choice([0, 0, 0, 1, 2, 3, 0xfd, 0xfe, 0xff, rng.getrandbits(8)])
        fields.append((buffer, count, 1))
        text = length
        if rng.random() < 0.15:
            text = rng.randrange(length + 6)  # _Split and ~string use strlen, not the length
        for i in range(text):
            fields.append((buffer + 1 + i, rng.randint(1, 255), 1))
        fields.append((buffer + 1 + text, 0, 1))
    if overlong and rng.random() < 0.04:
        length, ptr = 0xFFFFFFFF, 0x7FFF0000
    fields += [(offset + 4, ptr, 4), (offset + 8, length, 4), (offset + 12, capacity, 4)]
    return fields, ptr


def string_world(rng, pool, args):
    """`this`, a second string (sometimes sharing its buffer) and C text."""
    fields, ptr = string_fields(rng, STRING_SELF, STRING_SELF_BUFFER)
    # Only the second string is ever over-long: `this` would make _Copy
    # crawl through the emulated world (its size wraps to a tiny block).
    other, _ = string_fields(rng, STRING_OTHER, STRING_OTHER_BUFFER, shared_ptr=ptr, overlong=True)
    fields += other
    text = rng.randrange(0x60) if rng.random() < 0.8 else rng.choice([0, 1, 0x1f, 0x20])
    for i in range(text):
        fields.append((STRING_TEXT + i, rng.randint(1, 255), 1))
    fields.append((STRING_TEXT + text, 0, 1))
    return fields


def string_count(rng, pool) -> int:
    """Lengths, positions and counts: small, around the 31-character minimum
    capacity, npos and its neighbours (too long: std::_Xlen), or large
    (beyond the fake heap, so an allocation returns null and the copy
    faults at once instead of crawling through megabytes)."""
    roll = rng.random()
    if roll < 0.55:
        return rng.randrange(0x62)
    if roll < 0.7:
        return rng.choice([0, 1, 0x1f, 0x20])
    if roll < 0.85:
        # Not 0xFFFFFFFD (nor anything from 0x7FFFFFFE up): valid for
        # _Grow, but _Copy's size turns negative, it allocates 0 bytes and
        # the copy crawls through the emulated world before it faults.
        return rng.choice([0xFFFFFFFF, 0xFFFFFFFE])
    return (rng.getrandbits(31) | 0x10000000)  # positive as an allocation size


def copy_count(rng, pool) -> int:
    """For _Copy alone, which copies only the current text: also sizes whose
    allocation size turns negative (the allocator then asks for 0 bytes)."""
    if rng.random() < 0.1:
        return rng.choice([0x7FFFFFFE, 0xFFFFFFFD, 0x80000000 | rng.getrandbits(31)])
    return string_count(rng, pool)


def string_bool(rng, pool) -> int:
    return rng.choice([0, 1, 0x100, 0x101, rng.getrandbits(32)])


STRING_SELF_PTR = WORLD_BASE + STRING_SELF
STRING_OTHER_PTR = WORLD_BASE + STRING_OTHER
STRING_TEXT_PTR = WORLD_BASE + STRING_TEXT
STRING_RESULT_PTR = WORLD_BASE + STRING_RESULT


def text_or_null(rng, pool) -> int:
    return 0 if rng.random() < 0.15 else STRING_TEXT_PTR


SPECS += [
    Spec("String_Eos", 0x00401f60, "thiscall", [below(0x60)], eax_mask=0, presets=string_world),
    Spec("String_Tidy", 0x00402170, "thiscall", [string_bool], eax_mask=0, presets=string_world),
    Spec("String_TidyFree", 0x00401d20, "thiscall", [], eax_mask=0, presets=string_world),
    Spec("String_MaxSize", 0x00402260, "thiscall", []),
    # _Copy continues at 0x4022f0 (a separate Ghidra function); its catch
    # handler (0x4022c9..0x4022ef) only runs if operator new throws.
    Spec("String_Copy", 0x00402270, "thiscall", [copy_count], eax_mask=0, presets=string_world,
         cover_until=0x00402380, exempt_blocks=(0x004022c9, 0x004022d6, 0x004022d8, 0x004022ea)),
    # Dead: 0x401fe4 and 0x402046 decrement a count that the inlined _Tidy
    # re-checks after _Grow already found it to be 0 or 0xFF.
    Spec("String_Grow", 0x00401f80, "thiscall", [string_count, string_bool], eax_mask=0xFF,
         presets=string_world, exempt_blocks=(0x00401fe4, 0x00402046)),
    # Dead: 0x4020e9 is _Xlen for a strlen above 0xFFFFFFFD; 0x4020f7..0x402106
    # re-check the buffer pointer that the inlined _Tidy just set to null.
    Spec("String_Split", 0x004020a0, "thiscall", [], eax_mask=0, presets=string_world,
         exempt_blocks=(0x004020e9, 0x004020f7, 0x004020fe, 0x00402102, 0x00402106)),
    Spec("String_AssignChars", 0x004021c0, "thiscall", [one_of(STRING_TEXT_PTR), string_count],
         presets=string_world),
    # Dead: 0x401da0 (_Xran in erase(pos + n) of a self-assignment: pos + n
    # <= length by construction) and 0x401e01 (the second erase finds a
    # length below pos: the first erase leaves at least pos characters).
    Spec("String_AssignSubstr", 0x00401d60, "thiscall",
         [one_of(STRING_SELF_PTR, STRING_OTHER_PTR, STRING_OTHER_PTR), string_count, string_count],
         presets=string_world, exempt_blocks=(0x00401da0, 0x00401e01)),
    # Dead: erase() removes everything, so its _Grow(0) is false (0x401c19),
    # and after _Split the buffer is unshared, so the final release never
    # just decrements (0x401c3a).
    Spec("String_Destroy", 0x00401b60, "thiscall", [], eax_mask=0, presets=string_world,
         exempt_blocks=(0x00401c19, 0x00401c3a)),
    # Dead: 0x401b07 frees the second temporary, which `this` always shares
    # (it is non-empty with a fresh count 0, or empty without a buffer).
    Spec("String_FromChars", 0x00401a10, "thiscall", [text_or_null, string_count],
         presets=string_world, exempt_blocks=(0x00401b07,)),
    Spec("String_Default", 0x00407880, "thiscall", [one_of(STRING_TEXT_PTR)], presets=string_world),
    # Dead: 0x40248a (PlusChars) and 0x401ce6 (ConcatChars) free the
    # temporary sum, which the result always shares (same reason as above).
    # Unreachable in practice: 0x4023ee (_Xlen in append) needs a string of
    # about 4 GB.
    Spec("String_PlusChars", 0x00402380, "stdcall",
         [one_of(STRING_RESULT_PTR), one_of(STRING_SELF_PTR, STRING_OTHER_PTR), one_of(STRING_TEXT_PTR)],
         presets=string_world, exempt_blocks=(0x0040248a, 0x004023ee)),
    Spec("String_ConcatChars", 0x00401c70, "thiscall", [text_or_null], presets=string_world,
         edx=one_of(STRING_SELF_PTR, STRING_OTHER_PTR, STRING_OTHER_PTR), exempt_blocks=(0x00401ce6,)),
]

# --- Batch 22: the other std::string members, string members of game
# objects (src/s3re/game/string_members.cpp) and std::logic_error /
# out_of_range (src/s3re/msvc6/exceptions.cpp). `this` is the world base;
# its string members get buffers in scratch slots 5 and up, a second object
# at STRING_OTHER (scratch slot 0) gets its strings' buffers in slots 9 and
# up (sometimes sharing the first object's).

def member_strings(self_offsets, other_offsets=()):
    def preset(rng, pool, args):
        fields, ptrs, slot = [], {}, 5
        for offset in self_offsets:
            f, ptr = string_fields(rng, offset, SCRATCH + slot * SCRATCH_SLOT_BYTES)
            fields += f
            ptrs[offset] = ptr
            slot += 1
        slot = 9
        for offset in other_offsets:
            f, _ = string_fields(rng, STRING_OTHER + offset, SCRATCH + slot * SCRATCH_SLOT_BYTES,
                                 shared_ptr=ptrs.get(offset), overlong=True)
            fields += f
            slot += 1
        text = rng.randrange(0x60) if rng.random() < 0.8 else rng.choice([0, 1, 0x1f, 0x20])
        for i in range(text):
            fields.append((STRING_TEXT + i, rng.randint(1, 255), 1))
        fields.append((STRING_TEXT + text, 0, 1))
        return fields
    return preset


def session_desc(rng, pool, args):
    """The DirectPlay record at STRING_OTHER: its session name (+0x30) is the
    C text or null."""
    fields = member_strings(())(rng, pool, args)
    fields.append((STRING_OTHER + 0x30, rng.choice([STRING_TEXT_PTR, STRING_TEXT_PTR, 0]), 4))
    return fields


BY_VALUE_BUFFER = SCRATCH + 6 * SCRATCH_SLOT_BYTES


def by_value_string(rng, pool, args):
    """The buffer of the string passed by value (args 0..3): count byte, text,
    sometimes a zero earlier or later than its length."""
    fields = member_strings(())(rng, pool, args)
    fields.append((BY_VALUE_BUFFER, rng.choice([0, 0, 1, 2, 0xfd, 0xfe, 0xff]), 1))
    text = args[2] if rng.random() < 0.8 else rng.randrange(0x66)
    for i in range(text):
        fields.append((BY_VALUE_BUFFER + 1 + i, rng.randint(1, 255), 1))
    fields.append((BY_VALUE_BUFFER + 1 + text, 0, 1))
    return fields


def error_objects(rng, pool, args):
    """std::logic_error objects (exception: +4 what, +8 free flag; message
    string at +0xc) at `this` and at STRING_OTHER."""
    fields = member_strings((0xc,), (0xc,))(rng, pool, args)
    for base in (STRING_SELF, STRING_OTHER):
        fields.append((base + 4, rng.choice([STRING_TEXT_PTR, STRING_TEXT_PTR, 0x00655390]), 4))
        fields.append((base + 8, rng.choice([0, 0, 1, rng.getrandbits(32)]), 4))
    return fields


SELF_OR_OTHER = one_of(STRING_SELF_PTR, STRING_OTHER_PTR, STRING_OTHER_PTR)

EMPTY_TEXT = 0x007a7868        # the text assigned for a null C string ("" in the image)
NULL_STRING_COUNT = 0x0065539f  # "count byte" of the static empty string 0x6553a0


def empty_text(rng, pool, args):
    """Sometimes a non-empty text at 0x7a7868 (a writable global), so that
    the code that copies it is reached."""
    # Always written: globals keep what an earlier case wrote.
    if rng.random() < 0.5:
        return [(EMPTY_TEXT, 0, 1)]
    n = rng.choice([1, 2, 0x1f, 0x20, 0x40])
    return [(EMPTY_TEXT + i, rng.randint(1, 255), 1) for i in range(n)] + [(EMPTY_TEXT + n, 0, 1)]


def null_string_count(rng, pool, args):
    """Sometimes a count of 0xfe or more on the static empty string, so that
    assigning a string without buffer copies instead of sharing it."""
    # Always written (globals keep what an earlier case wrote); 0x3f is the
    # image's value (the byte before "" in .rdata).
    return [(NULL_STRING_COUNT, 0x3f if rng.random() < 0.6 else rng.choice([0xfd, 0xfe, 0xff]), 1)]


# Dead blocks shared by many of these (the reasons, referred to below):
#   DROP: deleting the temporary the result was built from; the result
#     always shares its buffer (non-empty with a fresh count) or it has none.
#   XLEN: std::_Xlen for a strlen (or a length minus a removed part) above
#     0xFFFFFFFD, which needs a string of about 4 GB.
#   CLEARED: the shared-buffer branch of an inlined _Grow on a string that a
#     constructor has just emptied (no buffer).
#   UNSHARED: the not-shared branches of an inlined _Grow after the code
#     found the buffer shared (or the shared branch after _Split).
#   DESTROY: the inlined ~string: erase() removes everything, so its
#     _Grow(0) is false (no _Eos), and after _Split the buffer is unshared,
#     so the release never just decrements.
#   NPOS: `count = npos` when npos < the source length (never: lengths are
#     at most 0xFFFFFFFF = npos), and the self-assignment's _Xran (the count
#     never exceeds the length).


SPECS += [
    Spec("String_FromSubstr", 0x004078a0, "thiscall",
         [SELF_OR_OTHER, string_count, string_count, one_of(STRING_TEXT_PTR)], presets=string_world,
         # Dead: the self-assignment path on a just-emptied string (pos and
         # count are 0: no _Xran, nothing to move or terminate) and the
         # release of its (null) old buffer before sharing.
         exempt_blocks=(0x004078f4, 0x0040790f, 0x00407938, 0x00407950, 0x0040795a, 0x00407984,
                        0x004079b8, 0x004079c2, 0x004079c6, 0x004079cc)),
    Spec("String_FromText", 0x00407a50, "thiscall", [one_of(STRING_TEXT_PTR), one_of(STRING_TEXT_PTR)],
         presets=string_world,  # XLEN 0x407a7f, CLEARED 0x407a8b..0x407a9a
         exempt_blocks=(0x00407a7f, 0x00407a8b, 0x00407a92, 0x00407a96, 0x00407a9a)),
    Spec("String_AssignRepeat", 0x0041f430, "thiscall", [string_count, anything], presets=string_world),
    Spec("String_FromRepeat", 0x005f3bf0, "thiscall", [string_count, anything, one_of(STRING_TEXT_PTR)],
         presets=string_world,  # CLEARED
         exempt_blocks=(0x005f3c2a, 0x005f3c31, 0x005f3c35, 0x005f3c39)),
    Spec("String_Erase", 0x00407b10, "thiscall", [string_count, string_count], presets=string_world,
         exempt_blocks=(0x00407bbf, 0x00407bd6, 0x00407bda)),  # XLEN, UNSHARED (after _Split)
    Spec("String_AssignText", 0x00409a90, "thiscall", [one_of(STRING_TEXT_PTR)], presets=string_world,
         exempt_blocks=(0x00409aad,)),  # XLEN
    Spec("String_AssignTextOrEmpty", 0x00413d70, "thiscall", [text_or_null], presets=string_world,
         globals=empty_text, exempt_blocks=(0x00413e2b, 0x00413e4f)),  # DROP, XLEN
    Spec("String_Freeze", 0x00414490, "thiscall", [], eax_mask=0, presets=string_world,
         # XLEN (a shared 4 GB string), UNSHARED
         exempt_blocks=(0x004144ae, 0x004144d9, 0x004144dd, 0x004144e1, 0x004144eb)),
    Spec("String_At", 0x004142d0, "thiscall", [string_count], presets=string_world,
         exempt_blocks=(0x004142fa, 0x00414325, 0x00414329, 0x0041432d, 0x00414334)),  # as _Freeze
    Spec("String_PlusString", 0x00409e60, "stdcall",
         [one_of(STRING_RESULT_PTR), SELF_OR_OTHER, SELF_OR_OTHER], presets=string_world,
         exempt_blocks=(0x00409f71,)),  # DROP
    Spec("String_CharsPlus", 0x00409f90, "stdcall",
         [one_of(STRING_RESULT_PTR), one_of(STRING_TEXT_PTR), SELF_OR_OTHER], presets=string_world,
         exempt_blocks=(0x0040a0ba,)),  # DROP
    Spec("String_PlusChar", 0x00421aa0, "stdcall",
         [one_of(STRING_RESULT_PTR), SELF_OR_OTHER, anything], presets=string_world,
         # XLEN (append to a 4 GB string), DROP (0x421b86 and the exit after it;
         # the sum is never empty)
         exempt_blocks=(0x00421afa, 0x00421b86, 0x00421b8f)),
    Spec("String_ConcatString", 0x004077f0, "thiscall", [SELF_OR_OTHER], presets=string_world,
         edx=SELF_OR_OTHER, exempt_blocks=(0x00407862,)),  # DROP
    Spec("String_ConcatCharsLeft", 0x00408e50, "thiscall", [SELF_OR_OTHER], presets=string_world,
         edx=text_or_null, exempt_blocks=(0x00408ec6,)),  # DROP
    Spec("String_ConcatCharsLeft2", 0x0043d8e0, "thiscall", [SELF_OR_OTHER], presets=string_world,
         edx=text_or_null, exempt_blocks=(0x0043d956,)),  # DROP
    Spec("String_ConcatChar", 0x00420cb0, "thiscall", [anything], presets=string_world,
         edx=SELF_OR_OTHER, exempt_blocks=(0x00420d22,)),  # DROP
]

SPECS += [
    # DESTROY: the blocks at +0xbc (_Eos) and +0xdd (decrement) of each.
    Spec(name, address, "thiscall", [], eax_mask=0, presets=member_strings(offsets),
         exempt_blocks=(address + 0xbc, address + 0xdd) if len(offsets) == 1 else
         (0x005e1bb5, 0x005e1bd5, 0x005e1c60, 0x005e1c7d))
    for name, address, offsets in (
        ("DestroyStringMember14_411980", 0x00411980, (0x14,)),
        ("DestroyStringMember08_441740", 0x00441740, (0x08,)),
        ("DestroyStringMember14_450ae0", 0x00450ae0, (0x14,)),
        ("DestroyStringMember14_455170", 0x00455170, (0x14,)),
        ("DestroyStringMember04_4581a0", 0x004581a0, (0x04,)),
        ("DestroyStringMember04_459440", 0x00459440, (0x04,)),
        ("DestroyStringMember50_45f060", 0x0045f060, (0x50,)),
        ("DestroyStringMember04_499470", 0x00499470, (0x04,)),
        ("DestroyStringMember04_49a070", 0x0049a070, (0x04,)),
        ("DestroyStringMembers10And00_5e1b10", 0x005e1b10, (0x10, 0x00)),
    )
]

SPECS += [
    Spec("SetStringMember14_412130", 0x00412130, "thiscall",
         [one_of(WORLD_BASE + 0x14, STRING_OTHER_PTR + 0x14, STRING_OTHER_PTR + 0x14)], eax_mask=0,
         presets=member_strings((0x14,), (0x14,)), globals=null_string_count,
         exempt_blocks=(0x0041214a, 0x00412155)),  # NPOS
    Spec("SetStringMember10_4181f0", 0x004181f0, "thiscall",
         [one_of(WORLD_BASE + 0x10, STRING_OTHER_PTR + 0x10, STRING_OTHER_PTR + 0x10)], eax_mask=0,
         presets=member_strings((0x10,), (0x10,)), globals=null_string_count,
         exempt_blocks=(0x0041820a, 0x00418215)),  # NPOS
    Spec("AssignTwoDwordsAndString_4499e0", 0x004499e0, "thiscall", [SELF_OR_OTHER],
         presets=member_strings((0x08,), (0x08,)), globals=null_string_count,
         exempt_blocks=(0x00449a0e, 0x00449a1d)),  # NPOS
    Spec("SessionDesc_Assign_5be000", 0x005be000, "thiscall", [SELF_OR_OTHER],
         presets=member_strings((0x50,), (0x50,)), globals=null_string_count,
         exempt_blocks=(0x005be02f, 0x005be03a)),  # NPOS
    # Dead: 0x5ed5d8 (NPOS) and the whole self-assignment path from 0x5ed5de
    # on: the function returns at once when this == x, so this + 0x14 ==
    # x + 0x14 never holds.
    Spec("LobbyRecord_Assign_5ed590", 0x005ed590, "thiscall", [SELF_OR_OTHER],
         presets=member_strings((0x14,), (0x14,)), globals=null_string_count,
         exempt_blocks=(0x005ed5d8, 0x005ed5de, 0x005ed5e7, 0x005ed5ec, 0x005ed5fc, 0x005ed5fe,
                        0x005ed602, 0x005ed62a, 0x005ed632)),
    Spec("SessionDesc_Construct_5bb0b0", 0x005bb0b0, "thiscall",
         [one_of(0, STRING_OTHER_PTR, STRING_OTHER_PTR, STRING_OTHER_PTR)], presets=session_desc,
         globals=empty_text),
    Spec("GuidNames_Construct_5b6790", 0x005b6790, "thiscall",
         [one_of(0, STRING_OTHER_PTR), text_or_null, text_or_null], presets=member_strings(()),
         exempt_blocks=(0x005b689a, 0x005b6978)),  # DROP (the first temporary)
    Spec("CRegistryValueInfo_Construct", 0x006161e0, "thiscall",
         [below(256), one_of(0, WORLD_BASE + BY_VALUE_BUFFER + 1, WORLD_BASE + BY_VALUE_BUFFER + 1),
          below(0x60), below(0x80), anything], presets=by_value_string,
         exempt_blocks=(0x006162ab, 0x006162cb)),  # DESTROY (of the parameter)
]

SPECS += [
    Spec("LogicError_What", 0x00559630, "thiscall", [], presets=error_objects),
    Spec("LogicError_Destroy", 0x005597e0, "thiscall", [], eax_mask=0, presets=error_objects),
    Spec("LogicError_DeletingDestructor", 0x005597c0, "thiscall", [string_bool], presets=error_objects),
    # Dead: 0x559694 (NPOS) and, in the copy onto itself, _Xran, the move
    # and _Eos of the inlined erase(): the message was just emptied.
    Spec("LogicError_Copy", 0x00559640, "thiscall", [SELF_OR_OTHER], presets=error_objects,
         globals=null_string_count, exempt_blocks=(0x00559694, 0x005596a0, 0x005596bb, 0x005596e3)),
    Spec("OutOfRange_Copy", 0x00559850, "thiscall", [SELF_OR_OTHER], presets=error_objects,
         globals=null_string_count, exempt_blocks=(0x005598a3, 0x005598af, 0x005598ca, 0x005598f2)),
]


# -- batch 23: framework constructors (win32_classes.cpp) ------------------

SPECS += [
    Spec("Win32_CObject_Construct", 0x00610320, "thiscall", []),
    # Ghidra's function also takes in the unrelated code at 0x610340.
    Spec("Win32_CObject_Destroy", 0x00610330, "thiscall", [], eax_mask=0, cover_until=0x00610340),
    Spec("Win32_CObject_DeletingDestroy", 0x0060bfc0, "thiscall", [string_bool]),
    Spec("Win32_CWin32Handle_Construct", 0x0060be60, "thiscall", [anything, anything]),
    Spec("Win32_CWaitable_Construct", 0x0060bf50, "thiscall", [anything, anything]),
    Spec("Win32_CWindowBase_Construct", 0x0060f960, "thiscall", []),
    Spec("Win32_CMessageQueue_Construct", 0x0060d690, "thiscall", [anything]),
    Spec("DirectX_CSimpleSurface_Construct", 0x005befb0, "thiscall", []),
    Spec("DirectX_CSimpleSurface_Construct4", 0x005befe0, "thiscall",
         [anything, anything, anything, anything]),
    Spec("DirectX_CDirectSoundBuffer_Construct", 0x005c1f70, "thiscall", []),
]

# -- batch 23: computer-player helpers (ai_helpers.cpp) ---------------------

AI_WORLD = 0x007e3408
AI_PLAYER_DATA = 0x007e2eb0
AI_AREAS = 0xe145e8
AI_PILES = 0x123885c


def ai_world(rng, pool, args):
    """g_aiWorld points at the emulated world."""
    return [(AI_WORLD, WORLD_BASE, 4)]


def ai_player_data(rng, pool, args):
    """g_aiPlayerData points at scratch buffer 0 (its candidate records)."""
    return [(AI_PLAYER_DATA, WORLD_BASE + SCRATCH, 4)]


def word_list(rng, pool, args):
    """A word list at scratch 0: free slots (0xffff) and small values, so
    the searched value is often present."""
    return [(SCRATCH + 2 * i, 0xFFFF if rng.random() < 0.3 else rng.randrange(6), 2)
            for i in range(16)]


def ai_map_size(rng, pool, args):
    return [(MAP_SIZE, rng.randint(-3, 400) & 0xFFFFFFFF, 4)]


def clamp_points(rng, pool, args):
    fields = ai_map_size(rng, pool, args)
    for slot in (0, 1):
        value = rng.randint(-20, 420) if rng.random() < 0.8 else rng.getrandbits(32)
        fields.append((SCRATCH + slot * SCRATCH_SLOT_BYTES, value & 0xFFFFFFFF, 4))
    return fields


def area_pile_list(rng, pool, args):
    """Goods type (byte, scratch 0) and area (word < 4096, scratch 1), and
    a list of up to 7 distinct piles for them."""
    goods = rng.randrange(256)
    area = rng.randrange(4096)
    fields = [(SCRATCH, goods, 1), (SCRATCH + SCRATCH_SLOT_BYTES, area, 2)]
    start, stride = rng.randint(1, 20000), rng.randint(1, 1000)
    head = 0
    for k in range(rng.randrange(8)):
        pile = start + k * stride
        fields.append((AI_PILES + pile * 14 + 0xc, head, 2))
        head = pile
    fields.append((AI_AREAS + area * 0x2e0 + 0xa + goods * 2, head, 2))
    return fields


def building_lookup(rng, pool, args):
    fields = [(BUILDING_COUNT, rng.randrange(3000), 4)]
    if rng.random() < 0.3:
        fields.append((BUILDINGS + (args[0] & 0xFFFF) * BUILDING_BYTES, 0xFF, 1))
    return fields


def best_candidate(rng, pool, args):
    """Often a stored best score next to the new one (args[1])."""
    index = args[0] & 0xFFFF
    if rng.random() < 0.6:
        return [(SCRATCH + 0x26 + index * 6, (args[1] + rng.randint(-2, 2)) & 0xFFFF, 2)]
    return []


def id_with_noise(limit: int) -> Generator:
    """An index below `limit`, sometimes with garbage in the upper half."""
    def generate(rng, pool):
        value = rng.randrange(limit)
        return value | (rng.getrandbits(16) << 16) if rng.random() < 0.3 else value
    return generate


SPECS += [
    Spec("AI_SetCurrentPlayer", 0x00551a30, "fastcall", [scratch(0)]),
    Spec("AI_WordListInsert", 0x00551970, "stdcall", [id_with_noise(8), scratch(0), id_with_noise(17)],
         presets=word_list),
    Spec("AI_WordListRemove", 0x005519b0, "stdcall", [id_with_noise(8), scratch(0), id_with_noise(17)],
         presets=word_list),
    Spec("AI_IsOnMap", 0x00572090, "fastcall", [between(-5, 410)], eax_mask=0xFF,
         presets=ai_map_size, globals=ai_world),
    Spec("AI_ClampToMap", 0x00572050, "fastcall", [scratch(0), scratch(1)], presets=clamp_points,
         globals=ai_world),
    Spec("AI_AreaGoodsAmount", 0x0059dc10, "fastcall", [scratch(0), scratch(1)],
         presets=area_pile_list, globals=ai_world),
    Spec("AI_GetBuilding", 0x0058e170, "fastcall", [id_with_noise(3200), scratch(1)],
         presets=building_lookup, globals=ai_world),
    Spec("AI_RecordBestCandidate", 0x0058ec50, "fastcall",
         [id_with_noise(0x300), anything, anything, anything], presets=best_candidate,
         globals=ai_player_data),
    Spec("AI_GridCell", 0x00554700, "thiscall", [anything, anything]),
    Spec("AI_GridFree", 0x00554040, "thiscall", [], eax_mask=0,
         presets=lambda rng, pool, args: [(0x158, 0, 4)] if rng.random() < 0.3 else []),
    Spec("AI_CopySixBytes", 0x0055a420, "fastcall", [one_of(0, WORLD_BASE + SCRATCH), scratch(1)],
         eax_mask=0),
    Spec("AI_SetWord4", 0x0055ca60, "thiscall", [anything], eax_mask=0xFFFF),
]

# -- batch 23: graphics helpers (graphics_helpers.cpp), MapEffect_SetLinks --

COLOUR_GLOBALS = 0x007dfba0


def colour_format(rng, pool, args):
    """Shifts (mostly 0..23) and scales (mostly 0..0x1ff) of the pixel format."""
    fields = []
    for i in range(3):
        fields.append((COLOUR_GLOBALS + 4 * i,
                       rng.randrange(24) if rng.random() < 0.9 else rng.getrandbits(32), 4))
        fields.append((COLOUR_GLOBALS + 12 + 4 * i,
                       rng.randrange(0x200) if rng.random() < 0.9 else rng.getrandbits(32), 4))
    return fields


def sprite_tables(rng, pool, args):
    """The group's table (scratch 0) and the entry it points to, placed so
    that entry + the group's offset lands in scratch 2."""
    group, index = args[0] >> 16, args[0] & 0xFFFF
    offset = rng.getrandbits(32)
    target = WORLD_BASE + SCRATCH + 2 * SCRATCH_SLOT_BYTES + rng.randrange(0x800)
    return [(0xb24 + group * 4, WORLD_BASE + SCRATCH, 4),
            (0x264 + group * 4, offset, 4),
            (SCRATCH + 8 + index * 4, (target - offset) & 0xFFFFFFFF, 4)]


def keep_or_value(rng, pool) -> int:
    return 0xFFFFFFFF if rng.random() < 0.5 else rng.getrandbits(32)


SPECS += [
    Spec("Render_PackColour", 0x004c7b00, "stdcall", [anything, anything, anything],
         globals=colour_format),
    Spec("GfxData_SpriteWord", 0x005fad20, "thiscall",
         [lambda rng, pool: (rng.randrange(0x100) << 16) | rng.randrange(0x100)],
         presets=sprite_tables),
    Spec("GfxData_SetDword", 0x005fc540, "thiscall", [anything]),
    Spec("Point16_Construct", 0x005fdab0, "thiscall", [anything, anything]),
    Spec("LogicRecord24_Construct", 0x005437b0, "thiscall", []),
    Spec("MapEffect_SetLinks", 0x00544290, "thiscall", [anything, keep_or_value, keep_or_value]),
]


# -- batch 23: CScreenElement and friends (screen_element.cpp) --------------
#
# Objects at UI_BASE (offsets as in the in-process test): the element at
# +0x100 (vtable = emu.FAKE_VTABLE), its parent at +0x200 (vbtable at +4,
# CScreenElement part at +0x224), the group subobject at +0x300 (vbptr
# -0x7c -> the element, focus child at -0x58, child object at +0x380), the
# 0x429fd0 subobject at +0x400 (vbptr -0xb4 -> the element). Rectangles and
# messages passed by pointer are in scratch 0 / 1.

FAKE_VTABLE = 0x00340000  # emu.FAKE_VTABLE
UI_OFFSET = 0x10000
UI_BASE = WORLD_BASE + UI_OFFSET
UI_VBTABLES = 0x20000  # three vbtables of 8 bytes
ELEMENT = UI_BASE + 0x100
UI_PARENT = UI_BASE + 0x200
UI_GROUP = UI_BASE + 0x300
UI_CHILD = UI_BASE + 0x380
UI_ENABLE = UI_BASE + 0x400
UI_RECTS = WORLD_BASE + SCRATCH
UI_MESSAGE = WORLD_BASE + SCRATCH + SCRATCH_SLOT_BYTES

UI_VIRTUALS = {0x04: 1, 0x0c: 1, 0x10: 0, 0x1c: 0, 0x20: (1, {0: 20}), 0x24: 5,
               0x28: (3, {1: 16, 2: 16}), 0x2c: 2, 0x54: 0, 0x64: 1, 0x68: 1, 0x6c: 1,
               0x70: (2, {0: 16, 1: 8}), 0x74: (2, {0: 16, 1: 8}), 0x78: 0}
UI_VIRTUALS.update({slot: 5 for slot in (0x30, 0x34, 0x38, 0x3c, 0x40, 0x44, 0x48, 0x4c, 0x50, 0x58,
                                         0x5c, 0x60)})

MESSAGE_TYPES = (1, 2, 3, 0x10, 0x12, 0x13, 0x15, 0x16, 0x20, 0x30, 0x40, 0, 5, 0x41, 0xFF)


def small_coordinate(rng) -> int:
    return rng.randint(-8, 40) & 0xFFFFFFFF


def ui_objects(rng, pool, args):
    """The object graph above, small rectangles, a message at scratch 1."""
    fields = [(UI_OFFSET + 0x100, FAKE_VTABLE, 4), (UI_OFFSET + 0x200, FAKE_VTABLE, 4),
              (UI_OFFSET + 0x204, WORLD_BASE + UI_VBTABLES, 4), (UI_VBTABLES + 4, 0x20, 4),
              (UI_OFFSET + 0x224, FAKE_VTABLE, 4), (UI_OFFSET + 0x380, FAKE_VTABLE, 4),
              (UI_OFFSET + 0x300 - 0x7c, WORLD_BASE + UI_VBTABLES + 8, 4),
              (UI_VBTABLES + 12, (0x100 - 0x284) & 0xFFFFFFFF, 4),
              (UI_OFFSET + 0x400 - 0xb4, WORLD_BASE + UI_VBTABLES + 16, 4),
              (UI_VBTABLES + 20, (0x100 - 0x34c) & 0xFFFFFFFF, 4),
              (UI_OFFSET + 0x118, 0 if rng.random() < 0.3 else UI_PARENT, 4),
              (UI_OFFSET + 0x11c, rng.randrange(2), 1), (UI_OFFSET + 0x11d, rng.randrange(3), 1),
              (UI_OFFSET + 0x300 - 0x58, 0 if rng.random() < 0.3 else UI_CHILD, 4),
              (UI_OFFSET + 0x300 - 0x78, rng.randrange(2), 1)]
    for i in range(4):
        fields.append((UI_OFFSET + 0x108 + 4 * i, small_coordinate(rng), 4))
    for i in range(8):
        fields.append((SCRATCH + 4 * i, small_coordinate(rng), 4))
    fields.append((SCRATCH + SCRATCH_SLOT_BYTES, rng.choice(MESSAGE_TYPES), 1))
    fields.append((SCRATCH + SCRATCH_SLOT_BYTES + 8, small_coordinate(rng) & 0xFFFF, 2))
    fields.append((SCRATCH + SCRATCH_SLOT_BYTES + 10, small_coordinate(rng) & 0xFFFF, 2))
    return fields


def message_type(rng, pool) -> int:
    return rng.choice(MESSAGE_TYPES) | (rng.getrandbits(24) << 8)


def message_position(rng, pool) -> int:
    return (small_coordinate(rng) & 0xFFFF) | ((small_coordinate(rng) & 0xFFFF) << 16)


MESSAGE = [message_type, anything, message_position, anything, anything]
small = lambda rng, pool: rng.choice([0, 1, 2]) if rng.random() < 0.5 else rng.getrandbits(32)  # noqa: E731


def ui_spec(name, address, args, this=ELEMENT, **kwargs):
    """A thiscall UI member: ECX = `this`, EDX random, then `args`."""
    return Spec(name, address, "fastcall", [one_of(this), anything, *args], presets=ui_objects,
                virtuals=UI_VIRTUALS, **kwargs)


def dialog_globals(rng, pool, args):
    return [(0x007a8020 + 4 * i, rng.getrandbits(32), 4) for i in range(8)] + \
           [(address, rng.getrandbits(32), 4) for address in (0x007a8238, 0x007a823c, 0x007a8240)]


SPECS += [
    ui_spec("CScreenElement_Construct", 0x00415610, []),
    ui_spec("CScreenElement_ConstructRect", 0x00415660, [one_of(UI_RECTS, ELEMENT + 8)]),
    ui_spec("CScreenElement_Destroy", 0x004156b0, []),
    ui_spec("CScreenElement_DeletingDestroy", 0x00415640, [string_bool]),
    ui_spec("CScreenElement_Rect", 0x004156d0, []),
    ui_spec("CScreenElement_SetRect", 0x004156e0, [one_of(UI_RECTS, UI_RECTS, ELEMENT + 8)]),
    ui_spec("CScreenElement_MoveBy", 0x00415780, [small, small]),
    ui_spec("CScreenElement_SetParent", 0x004157d0, [one_of(0, UI_PARENT, UI_PARENT, 0x1234)]),
    ui_spec("CScreenElement_ContainsPoint", 0x00415800, [message_position, message_position]),
    ui_spec("CScreenElement_NeedsRedraw", 0x00415830, [], eax_mask=0xFF),
    ui_spec("CScreenElement_IsQuery78Zero", 0x00415840, []),
    ui_spec("CScreenElement_OnMessage", 0x00415850, MESSAGE),
    ui_spec("CScreenElement_WantsMessage", 0x00415890, [one_of(UI_MESSAGE)], eax_mask=0xFF),
    # The jump table's data (0x415a22..0x415a93) follows the last ret.
    ui_spec("CScreenElement_DispatchMessage", 0x004158d0, MESSAGE, cover_until=0x00415a22),
    ui_spec("CScreenElement_DrawClipped", 0x00415aa0, [anything, one_of(UI_RECTS + 16)]),
    ui_spec("CScreenElement_DrawRect", 0x00415af0, [anything, one_of(UI_RECTS, UI_RECTS + 16), anything]),
    ui_spec("CScreenElement_IntersectRects", 0x00415b20,
            [one_of(UI_BASE + 0x40), one_of(UI_RECTS), one_of(UI_RECTS + 16)]),
    ui_spec("CScreenElement_SetFocus", 0x00415ba0, [anything]),
    ui_spec("CScreenElement_KillFocus", 0x00415bd0, [], eax_mask=0xFF),
    ui_spec("CScreenElement_Invalidate", 0x00415bf0, []),
    ui_spec("CScreenElement_TrackMouse", 0x00415c10, MESSAGE),
    ui_spec("CScreenElement_Enable", 0x00415ca0, [small]),
    ui_spec("CScreenElement_ParentQuery78", 0x004160d0, []),
    ui_spec("CScreenElement_SetFlag1F", 0x00416c60, [anything], eax_mask=0),
    ui_spec("UiGroup_WantsMessage", 0x00417100, [one_of(UI_MESSAGE)], this=UI_GROUP, eax_mask=0xFF),
    ui_spec("UiGroup_SetFocus", 0x00417650, [one_of(0, UI_CHILD, UI_CHILD)], this=UI_GROUP),
    ui_spec("UiGroup_KillFocus", 0x00417680, [], this=UI_GROUP, eax_mask=0xFF),
    ui_spec("UiGroup_SetFocus_429d70", 0x00429d70, [one_of(0, UI_CHILD, UI_CHILD)], this=UI_GROUP),
    ui_spec("UiGroup_WantsMessage_429df0", 0x00429df0, [one_of(UI_MESSAGE)], this=UI_GROUP,
            eax_mask=0xFF),
    ui_spec("UiGroup_Enable_429fd0", 0x00429fd0, [small], this=UI_ENABLE),
    Spec("Dialog_Global7a823c", 0x00416870, "stdcall", [], globals=dialog_globals),
    Spec("Dialog_Global7a8238", 0x004168c0, "stdcall", [], globals=dialog_globals),
    Spec("Dialog_Global7a8240", 0x00416c50, "stdcall", [], globals=dialog_globals),
    Spec("Dialog_CopyGlobals7a8020", 0x00416880, "stdcall", [scratch(0)], globals=dialog_globals),
    Spec("Dialog_Table7a809c", 0x004168a0, "stdcall", [below(14)]),
    Spec("Dialog_Table7a8064", 0x004168b0, "stdcall", [below(14)]),
]


# -- batch 24: MapEffect make functions (map_effects.cpp) -------------------
#
# Each builds the record in a stack temporary and copies all 0x4c bytes into
# ECX, so the five padding bytes the constructor never writes hold each
# function's own stack leftovers: they are not compared (ignore_bytes). The
# self-test pairing 0x5439a0 / ours 0x543e80 differs only in bytes +0x38 and
# +0x39, next to the padding byte +0x3b, and must still be caught.

EFFECT_PADDING = (0x2a, 0x2b, 0x3b, 0x3e, 0x3f)


def make_effect(address: str, count: int, original: int | None = None) -> Spec:
    return Spec(f"MapEffect_Make_{address}", original or int(address, 16), "thiscall", [anything] * count,
                ignore_bytes=EFFECT_PADDING)


SPECS += [
    make_effect("543930", 6),
    make_effect("5439a0", 7),
    make_effect("543a10", 10),
    make_effect("543aa0", 13),
    make_effect("543b40", 8),
    make_effect("543bc0", 10),
    make_effect("543c40", 12),
    make_effect("543ce0", 9),
    make_effect("543d60", 10),
    make_effect("543df0", 11),
    make_effect("543e80", 7),
    make_effect("543ef0", 8),
    make_effect("543f70", 3),
]


# -- batch 24: Watcom pointer vector (watcom/ptr_vector.cpp) ----------------
#
# The vector at world offset 0 (ECX = the world): vtable = emu.FAKE_VTABLE
# (slot 0x04 is the "length changed" call), data in scratch 0.

VECTOR_VIRTUALS = {0x04: 1}


def vector_state(rng, pool, args):
    capacity = rng.choice([0, 0, 1, 2, rng.randrange(8), rng.getrandbits(32)])
    length = rng.choice([0, 0, 1, 2, 3, rng.randrange(8), 0x80000000, 0x80000001, rng.getrandbits(32)])
    return [(0, FAKE_VTABLE, 4), (8, WORLD_BASE + SCRATCH, 4), (0xc, capacity, 4),
            (0x10, rng.randint(-2, 6) & 0xFFFFFFFF, 4), (0x14, length, 4)]


def small_or_anything(low, high):
    return lambda rng, pool: (rng.randint(low, high) if rng.random() < 0.8 else rng.getrandbits(32)) & 0xFFFFFFFF


SPECS += [
    # n * 4 wraps for large n; the fake heap refuses blocks it can't hold.
    Spec("PtrVector_Resize", 0x00429a00, "thiscall", [small_or_anything(0, 6)], presets=vector_state),
    Spec("PtrVector_ResizeClampLength", 0x00437fe0, "thiscall", [small_or_anything(0, 6)], presets=vector_state),
    # 0x428f7f (clamp the length to 1 after growing) is dead: this path is
    # only taken when the length was 0.
    Spec("PtrVector_Index", 0x00428ee0, "thiscall", [small_or_anything(-3, 8)], presets=vector_state,
         virtuals=VECTOR_VIRTUALS, new_fails=0.2, exempt_blocks=(0x428f7f,)),
]


# -- batch 24: CScreenGroup, CControl, CGroupControl (screen_element.cpp) ---
#
# On top of ui_objects: the group at UI_GROUP has its children vector at
# -0x74 (vtable = emu.FAKE_VTABLE, data in scratch 2 pointing at four child
# elements at UI_BASE + 0x800 + 0x40 * k), the captured child at -0x50, the
# "use the first rectangle" byte at -0x78. Child calls (virtual_effects)
# sometimes shorten the vector or make it huge (length 0x80000001: then
# every index is out of range, and data[0x80000000] wraps around to
# data[0]); a huge vector is always emptied by the next child call, which
# ends the loop. CControl: its CScreenElement part ("this") at UI_BASE +
# 0x600 (control at -0x3c, vbtable at -0x38 -> the element), and for the
# CGroupControl wrappers at UI_GROUP - 0x7c (vbtable at UI_GROUP - 0xb4).
#
# Exempt blocks (EMPTY): the inlined operator[] check's "vector is empty"
# branch (with the grow-to-one code after it) right after a loop's `index <
# length` check, with no call in between that could change the vector, is
# dead code. The same code is covered in PtrVector_Index and in
# CScreenGroup_HandleMessage's second check (after the 0x20 call).

# NEG: the "index < 0" branch of the same check is dead too (the loop index
# starts at 0 and would need 2^31 iterations to turn negative). LEN0: the
# clamp of the length to 1 after growing an empty vector (the length is 0
# there).
EMPTY_417BE0 = (0x417c2b, 0x417c40, 0x417c48, 0x417c4d, 0x417c5d, 0x417c68, 0x417c6e, 0x417c70, 0x417c74,
                0x417c7c, 0x417c84, 0x417ca5, 0x417cb8, 0x417cbd, 0x417cd1)
EMPTY_417AB0 = (0x417b00, 0x417b15, 0x417b1d, 0x417b22, 0x417b32, 0x417b3d, 0x417b43, 0x417b45, 0x417b49,
                0x417b51, 0x417b59, 0x417b7a, 0x417b8d, 0x417b92, 0x417ba6)
EMPTY_417480 = (0x4174d2, 0x4174ec, 0x4174f8, 0x4174fd, 0x417500, 0x41750c, 0x417514,
                0x417545, 0x41755f, 0x41756b, 0x417570, 0x417573, 0x41757f, 0x417587,
                0x4175c4, 0x4175de, 0x4175ea, 0x4175ef, 0x4175f2, 0x4175fe, 0x417606)
EMPTY_417130 = (0x417224, 0x417239, 0x417246, 0x41724a, 0x417263,  # the 0x16 loop
                0x4172cf, 0x4172e4, 0x4172f5, 0x4172fa, 0x4172fd, 0x41730d, 0x417315,  # first check
                0x4173d0, 0x4173eb)  # second check: LEN0, NEG
EMPTY_4176B0 = (0x417706, 0x41771b, 0x417723, 0x417728, 0x417738, 0x417743, 0x417749, 0x41774b, 0x41774f,
                0x417757, 0x41775f, 0x417780, 0x417793, 0x417798, 0x4177ac)

GROUP_DATA = SCRATCH + 2 * SCRATCH_SLOT_BYTES
CHILDREN = [UI_BASE + 0x800 + 0x40 * k for k in range(4)]
CONTROL = UI_BASE + 0x600
CONTROL_VBTABLE = UI_VBTABLES + 24
GROUP_CONTROL_VBTABLE = UI_VBTABLES + 32
GROUP_VIRTUALS = dict(UI_VIRTUALS)
GROUP_VIRTUALS.update({0x00: 1, 0x18: 1})


def shrink_children(machine, ecx, args, h):
    """A child call that changes the group's vector length (see above)."""
    address = UI_GROUP - 0x60
    length = int.from_bytes(machine.guest_read(address, 4), "little")
    choice = h & 7
    if length >= 0x80000000 or choice == 0:
        value = 0
    elif choice == 1 and not machine.probe_calls[-1][1] in (0x5c, 0x60):
        # (not from TrackMouse's calls: later children may not call at all)
        value = 0x80000001
    elif choice == 2:
        value = (h >> 8) % 3
    else:
        return
    machine.guest_write(address, value.to_bytes(4, "little"))


GROUP_EFFECTS = {slot: shrink_children for slot in (0x18, 0x20, 0x24, 0x28, 0x5c, 0x60, 0x70, 0x74)}


def group_objects(rng, pool, args):
    fields = ui_objects(rng, pool, args)
    group = UI_GROUP - WORLD_BASE
    count = rng.choice([0, 1, 2, 3, 4, 4, 0x80000001])
    fields += [(group - 0x74, FAKE_VTABLE, 4), (group - 0x6c, WORLD_BASE + GROUP_DATA, 4),
               (group - 0x68, rng.choice([0, 1, 4, rng.getrandbits(32)]), 4),
               (group - 0x64, rng.randint(-1, 4) & 0xFFFFFFFF, 4), (group - 0x60, count, 4),
               (group - 0x50, 0 if rng.random() < 0.6 else rng.choice(CHILDREN), 4),
               (group - 0x78, rng.randrange(2), 1)]
    for k, child in enumerate(CHILDREN):
        offset = child - WORLD_BASE
        fields.append((GROUP_DATA + 4 * k, child, 4))
        fields += [(offset, FAKE_VTABLE, 4), (offset + 0x18, rng.choice([0, UI_PARENT]), 4),
                   (offset + 0x1c, rng.randrange(2), 1)]
        if count == 0x80000001 and k == 0:
            # The first child sees the mouse arrive (TrackMouse calls 0x5c),
            # so the huge vector is emptied by an effect in every loop.
            fields.append((offset + 0x1d, 0, 1))
            for i, v in enumerate((0, 0, 0x10000, 0x10000)):
                fields.append((offset + 8 + 4 * i, v & 0xFFFFFFFF, 4))
        else:
            fields.append((offset + 0x1d, rng.randrange(3), 1))
            for i in range(4):
                fields.append((offset + 8 + 4 * i, small_coordinate(rng), 4))
    # The CControl parts: vbtable, tooltip, where the tooltip was opened.
    for this, vbtable in ((CONTROL, CONTROL_VBTABLE), (UI_GROUP - 0x7c, GROUP_CONTROL_VBTABLE)):
        t = this - WORLD_BASE
        fields += [(t - 0x38, WORLD_BASE + vbtable, 4), (vbtable + 4, (ELEMENT - (this - 0x38)) & 0xFFFFFFFF, 4),
                   (t - 0x1c, 0 if rng.random() < 0.3 else UI_CHILD, 4)]
        for field in (-0xc, -0x8):
            value = rng.randint(-40, 120) if rng.random() < 0.9 else rng.getrandbits(32)
            fields.append((t + field, value & 0xFFFFFFFF, 4))
    # A non-zero move offset at scratch 0 + 0x40 (MoveBy calls 0x70 then).
    fields += [(SCRATCH + 0x40, rng.choice([1, -1, 5]) & 0xFFFFFFFF, 4),
               (SCRATCH + 0x44, rng.choice([0, 2, -3]) & 0xFFFFFFFF, 4)]
    return fields


def move_delta(rng, pool) -> int:
    return WORLD_BASE + SCRATCH + 0x40


def group_spec(name, address, args, this=UI_GROUP, **kwargs):
    return Spec(name, address, "fastcall", [one_of(this), anything, *args], presets=group_objects,
                virtuals=GROUP_VIRTUALS, virtual_effects=GROUP_EFFECTS, **kwargs)


DRAW_ARGS = [anything, one_of(UI_RECTS), one_of(UI_RECTS + 16)]

SPECS += [
    group_spec("CScreenGroup_SetFlag1F", 0x00417be0, [anything], exempt_blocks=EMPTY_417BE0),
    group_spec("CScreenGroup_OnMoved", 0x00417ab0, [one_of(UI_RECTS), move_delta], exempt_blocks=EMPTY_417AB0),
    group_spec("CScreenGroup_Draw", 0x00417480, DRAW_ARGS, exempt_blocks=EMPTY_417480),
    # AL is undefined in the original when the captured child declines the
    # message (an uninitialized stack byte), so EAX is not compared here; the
    # in-process test compares AL on every other path.
    group_spec("CScreenGroup_HandleMessage", 0x00417130, MESSAGE, eax_mask=0, exempt_blocks=EMPTY_417130,
               new_fails=0.1),
    group_spec("CScreenGroup_OnMouseMove", 0x004176b0, MESSAGE, exempt_blocks=EMPTY_4176B0),
    group_spec("CScreenGroup_SetFlag1F_429dd0", 0x00429dd0, [anything]),
    group_spec("CScreenGroup_OnMoved_42a030", 0x0042a030, [one_of(UI_RECTS), move_delta]),
    group_spec("CScreenGroup_Draw_42a180", 0x0042a180, DRAW_ARGS),
    group_spec("CScreenGroup_HandleMessage_429e10", 0x00429e10, MESSAGE, eax_mask=0),
    group_spec("CScreen_HandleMessage", 0x00417f60, MESSAGE, this=UI_GROUP + 0x18, eax_mask=0),
    # CControl (EAX: only AL is defined for the message handlers' results,
    # but ours reproduce the whole register where it is known).
    group_spec("CControl_DeleteTooltip", 0x004187b0, [], this=CONTROL - 0x3c, eax_mask=0),
    group_spec("CControl_OnMessage10", 0x00418380, MESSAGE, this=CONTROL),
    group_spec("CControl_OnMouseMove", 0x00418430, MESSAGE, this=CONTROL, eax_mask=0xFF),
    group_spec("CControl_OnMouseEnter", 0x004184b0, MESSAGE, this=CONTROL),
    group_spec("CControl_OnMouseLeave", 0x004184e0, MESSAGE, this=CONTROL),
    group_spec("CGroupControl_OnMessage10", 0x00429e80, MESSAGE),
    group_spec("CGroupControl_OnMouseMove", 0x00429ec0, MESSAGE, eax_mask=0xFF),
    group_spec("CGroupControl_OnMouseEnter", 0x00429f50, MESSAGE),
    group_spec("CGroupControl_OnMouseLeave", 0x00429f90, MESSAGE),
]

# -- batch 24: CStaticControl, CSurface, AI area lookups, CreatePile, -------
# -- UpdateGroupMenu -------------------------------------------------------

AI_MAP_SIZES = 0x007e3412  # uint16 y size, then uint16 x size at 0x7e3414
AI_MAP_WORLD = 0x007e7b60


def building_position(rng, pool, args):
    """The building record of the id in scratch 0: x/y around the map size."""
    building = rng.randrange(3200)
    record = BUILDINGS + building * BUILDING_BYTES
    coordinate = lambda: rng.randrange(420) if rng.random() < 0.9 else rng.getrandbits(16)  # noqa: E731
    return [(SCRATCH, building, 2), (record + 0xa, coordinate(), 2), (record + 0xc, coordinate(), 2)]


def ai_map_sizes(rng, pool, args):
    return [(AI_WORLD, WORLD_BASE, 4), (AI_MAP_SIZES, rng.randrange(420), 2), (AI_MAP_SIZES + 2, rng.randrange(420), 2)]


def free_tile_world(rng, pool, args):
    """A small map; a run of blocked tiles from (x, y) on, often to the edge."""
    size = rng.randint(1, 40)
    fields = [(MAP_SIZE, size, 4)]
    x, y = args
    if 0 <= x < size and 0 <= y < size:
        run = 0 if rng.random() < 0.3 else rng.randint(1, size - x + 1)
        for k in range(run + 1):
            if x + k > size:
                break
            tile = 0x67444 + ((x + k) * 0x300 + y) * 24
            flags = rng.getrandbits(8)
            fields.append((tile + 0xa, flags | 1 if k < run else flags & ~1, 1))
    return fields


def ai_both_worlds(rng, pool, args):
    return [(AI_WORLD, WORLD_BASE, 4), (AI_MAP_WORLD, WORLD_BASE, 4)]


PILE_LIMIT = 0x7cff


def create_pile_state(rng, pool, args):
    """Sometimes every pile record is in use (the "none free" path); the
    tile's area word below 0x400 mostly (else 0 or >= 0x8000 with goods)."""
    x, y, goods, amount, kind = args
    fields = []
    if rng.random() < 0.03:
        fields += [(0x123885c + pile * 14, 1, 1) for pile in range(1, PILE_LIMIT)]
    tile = 0x67444 + (x * 0x300 + y) * 24
    if rng.random() < 0.15 and amount != 0:
        area = rng.choice([0, 0x8000 + rng.randrange(0x8000)])
    else:
        area = rng.randrange(0x400)
    fields.append((tile + 0x14, area, 2))
    return fields


def pile_goods(rng, pool) -> int:
    return 0xff if rng.random() < 0.25 else rng.randrange(36)


def pile_amount(rng, pool) -> int:
    return rng.choice([0, 0, 1, 5, 0x100])


def pile_kind(rng, pool) -> int:
    return rng.choice([0, 1, 0x32, 0x33, 0xfd, 0xfe, 0xff, 0x100])


GROUP_COUNTS = 0x007ad620
GROUP_SETTLERS = 0x007ad634
SETTLERS = 0x12a5e60


def group_menu_state(rng, pool, args):
    group = args[0]
    count = rng.randrange(2) if rng.random() < 0.3 else rng.randrange(101)
    kind = rng.choice([0x24, 0x2c, 0x2b, 0x29, 0x28, 0x16, 0x17, 0x01])
    same = rng.random() < 0.7
    local = rng.randrange(4)
    world = [(0x2d2d4, local, 4), (0x2d298, rng.randrange(8) if rng.random() < 0.7 else rng.getrandbits(32), 4)]
    exe = [(GROUP_COUNTS + group * 2, count, 2)]
    for k in range(count):
        settler = rng.randrange(0x10000)
        exe.append((GROUP_SETTLERS + group * 200 + k * 2, settler, 2))
        world.append((SETTLERS + settler * 0x40 + 1,
                      kind if same or rng.random() < 0.5 else rng.choice([0x24, 0x2c, 0x17, 0x02]), 1))
        world.append((SETTLERS + settler * 0x40, local if rng.random() < 0.6 else rng.randrange(4), 1))
    return world, exe


def group_menu_presets(rng, pool, args):
    """Draws the state once per case (world part); the S3.EXE part is
    replayed by group_menu_globals from the same draw."""
    world, exe = group_menu_state(rng, pool, args)
    GROUP_MENU_EXE[:] = exe
    return world


GROUP_MENU_EXE: list = []


def group_menu_globals(rng, pool, args):
    return list(GROUP_MENU_EXE)


SPECS += [
    ui_spec("CStaticControl_Construct", 0x0041c3c0, [one_of(UI_RECTS, ELEMENT + 8)]),
    Spec("DirectX_CSurface_Construct", 0x005bf200, "thiscall", []),
    Spec("AI_BuildingArea", 0x00599610, "fastcall", [scratch(0)], presets=building_position,
         globals=ai_map_sizes),
    Spec("AI_FreeTileArea", 0x0058b480, "stdcall", [between(-3, 42), between(-3, 42)],
         presets=free_tile_world, globals=ai_both_worlds),
    Spec("GameWorld_CreatePile", 0x00504420, "thiscall",
         [below(384), below(384), pile_goods, pile_amount, pile_kind], presets=create_pile_state,
         max_cases=800),
    Spec("CLogicThread_UpdateGroupMenu", 0x00542990, "logicthread", [below(10)],
         presets=group_menu_presets, globals=group_menu_globals),
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
    # ~string erases first, so even on an unshared buffer it writes before freeing.
    Spec("String_TidyFree", 0x00401b60, "thiscall", [], eax_mask=0, presets=string_world),
    # The same destructor shape on a member at another offset (+0x8, not +0x14).
    Spec("DestroyStringMember14_411980", 0x00441740, "thiscall", [], eax_mask=0,
         presets=member_strings((0x08, 0x14))),
    # Virtual-call probes: the same parent call, but slot 0x1c instead of 0x78.
    ui_spec("CScreenElement_Invalidate", 0x004160d0, [], eax_mask=0),
    # API probes: the clip is intersected with the element (PtInRect is not called).
    ui_spec("CScreenElement_DrawClipped", 0x00415af0, [anything, one_of(UI_RECTS + 16)], eax_mask=0),
    # ignore_bytes: 0x5439a0 and 0x543e80 differ only in bytes +0x38 / +0x39,
    # next to the ignored padding byte +0x3b.
    make_effect("543e80", 7, original=0x005439a0),
]
